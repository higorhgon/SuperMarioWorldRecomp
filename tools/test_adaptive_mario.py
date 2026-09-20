#!/usr/bin/env python3
"""Replay the reported split-Mario F1 (level $21, Mario 341,352, camera 235).

Uses a copied, unpaused save and ordinary running input with Fit/Screen-based
spawning. Check both Mario tiles against native raster data while the extra
Eeries are alive; checking far ghosts alone missed their overlap with Mario.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import time

from renderer_visibility import Capture
from test_adaptive_renderer import ROOT, environment, rows
from test_adaptive_spawns import snapshot_ram
from test_adaptive_transitions import check_coverage, owned_windows

CAPTURES = (242, 260, 280, 300, 320, 340)


def check(root):
    mario_pixels = ghost_pixels = 0
    for frame in CAPTURES:
        cap = Capture(root, frame)
        assert not cap.ram[0x13d4] and not cap.ram[0x71], 'Mario must be alive and running'
        assert cap.ram[0x19] == 0 and cap.word(0x96) == 352, 'wrong small-Mario fixture'
        # $00:E2BD draws this pose at $0310/$0314 (OAM 68/69). Require
        # both original pieces: a ghost overwriting one also corrupts the
        # native PPU, so a native-area comparison alone is insufficient.
        parts = [struct.unpack_from('<HH', cap.ram, 0x200+slot*4) for slot in (68,69)]
        assert [attr & 0x1fff for pos, attr in parts] == [0,2], 'Mario tiles overwritten'
        assert parts[1][0] == parts[0][0]+0x1000, 'Mario head/body separated'
        mario_pixels += sum(cap.piece(slot, native=True) for slot in (68,69))
        for slot in (8,9):
            assert cap.ram[0x14c8+slot] and cap.ram[0x9e+slot] == 0x39
            first = 64+cap.ram[0x15ea+slot]//4
            ghost_pixels += cap.piece(first)
    data = rows(root/'frames.csv')
    assert len(data) >= 350
    assert max(int(row['unexplained_differences']) for row in data) == 0
    check_coverage(root)
    return dict(frames=len(data), samples=len(CAPTURES), mario_pixels=mario_pixels,
                extra_ghost_pixels=ghost_pixels, unexplained_native_differences=0)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--build', type=Path, default=ROOT/'build-adaptive')
    ap.add_argument('--state', type=Path)
    ap.add_argument('--check', type=Path)
    args = ap.parse_args()
    if args.check:
        print(json.dumps(check(args.check.resolve()), indent=2))
        return
    if not args.state:
        ap.error('--state is required')
    build, state = args.build.resolve(), args.state.resolve(strict=True)
    digest = hashlib.sha256(state.read_bytes()).hexdigest()
    ram = snapshot_ram(state)
    assert ram[0x13bf] == 0x21 and ram[0x1692] == 0x11 and not ram[0x13d4]
    assert int.from_bytes(ram[0x1a:0x1c], 'little') == 235, 'wrong F1 fixture'
    root = Path(tempfile.mkdtemp(prefix='smw-mario-', dir=build))
    print(f'Artifacts: {root}', flush=True)
    (root/'saves').mkdir()
    shutil.copy2(state, root/'saves/save0.sav')
    (root/'route.script').write_text('wait 240\nloadstate 0\nwait 20\npress right+y 100\n')
    buttons = ('a','b','x','y','l','r','start','select','up','down','left','right')
    (root/'keybinds.ini').write_text(''.join(f'[player{p}]\n'+''.join(f'{b}=None\n' for b in buttons) for p in (1,2)))
    (root/'config.ini').write_text('[General]\nAutosave=0\nDisableFrameDelay=1\nSkipLauncher=1\n'
        '[Graphics]\nWindowSize=2048x442\nNewRenderer=1\nNoSpriteLimits=1\nOutputMethod=SDL\n'
        '[Sound]\nEnableAudio=0\n[GamepadMap]\nEnableGamepad1=0\nEnableGamepad2=0\n'
        '[KeyMap]\nControls=\nControlsP2=\nTurbo=\nPause=\nPauseDimmed=\nReset=\nLoad=\nSave=\n')
    env = environment()
    env.update(SMW_RENDER_ASPECT='Fit', SMW_ENEMY_SPAWN='adaptive', SMW_RENDER_DIAGNOSTICS=str(root),
               SMW_RENDER_CAPTURE_FRAME=','.join(map(str, CAPTURES)))
    exe = build/('SuperMarioWorldSNESRecomp.exe' if os.name == 'nt' else 'SuperMarioWorldSNESRecomp')
    try:
        with (root/'stdout.log').open('w') as out, (root/'stderr.log').open('w') as err:
            process = subprocess.Popen([str(exe), '--config', str(root/'config.ini'), '--script', str(root/'route.script'),
                '--benchmark', '350', str(ROOT/'smw.sfc')], cwd=root, env=env, stdout=out, stderr=err)
            try:
                if os.name == 'nt':
                    deadline = time.monotonic()+160
                    while process.poll() is None and time.monotonic() < deadline:
                        owned_windows(process.pid)
                        time.sleep(.1)
                assert process.wait(timeout=160) == 0
            finally:
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=10)
    finally:
        assert hashlib.sha256(state.read_bytes()).hexdigest() == digest, 'original save changed'
    report = check(root)
    report['source_sha256'] = digest
    (root/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report), flush=True)


if __name__ == '__main__':
    main()
