#!/usr/bin/env python3
"""Check ghost/ledge-hole activation from the reported local ghost-house F1.

Copies the unpaused level-$21 save (Mario 1275,352; camera 1157) and uses
ordinary movement. Fit and Screen-based spawning stay enabled throughout.
"""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

from renderer_visibility import Capture
from test_adaptive_renderer import ROOT, environment, rows
from test_adaptive_spawns import snapshot_ram
from test_adaptive_transitions import check_coverage, owned_windows

ROUTE = ('wait 240\nloadstate 0\npress right+y 12\n' +
         'press right+y+b 50\npress right+y 4\n'*2 +
         'press right+y+b 50\npress right+y 34\npress right+y+b 50\n'
         'press right+y 20\npress right+y+b 50\npress right+y 210\n'
         'press right+y+b 50\npress right+y 60\npress right+y+b 50\n'
         'press right+y 40\npress right+y+b 50\npress right+y 30\n')
CAPTURES = dict(run=(260, 780, 860, 960, 1060, 1140), standing=(250, 260, 280, 300, 320, 360))
FRAMES = dict(run=1150, standing=400)
# These source records are untouched at the start of this fixture. Include
# every later placement, not just the two ghosts that exposed the bottleneck.
PLACEMENTS = dict(zip(range(13, 25), (1888,2240,2464,2784,2864,2864,3008,3024,3040,3264,3376,3504)))


def check(root, scenario):
    data = rows(root/'frames.csv')
    assert len(data) == FRAMES[scenario]
    widths = {int(row['width']) for row in data}
    assert len(widths) == 1, 'test window was resized'
    assert max(int(row['unexplained_differences']) for row in data) == 0
    benchmarks = [json.loads(line.removeprefix('SNESRECOMP_BENCHMARK '))
                  for line in (root/'stdout.log').read_text().splitlines()
                  if line.startswith('SNESRECOMP_BENCHMARK ')]
    assert len(benchmarks) == 1 and benchmarks[0]['frames'] == len(data)
    events = list(csv.DictReader((root/'spawns.csv').open()))
    loaded = {int(row['record']): int(row['frame']) for row in events
              if row['event'] == 'loaded' and int(row['frame']) > 240}
    records = PLACEMENTS if scenario == 'run' else {key:PLACEMENTS[key] for key in (13,14)}
    delays = {}
    for record, x in records.items():
        eligible = [int(row['frame']) for row in data if int(row['frame']) > 240 and row['mode'] == '20'
                    and int(row['camera'])-int(row['native_offset'])-32 <= x
                    < int(row['camera'])-int(row['native_offset'])+int(row['width'])+32]
        assert eligible, f'placement {record} never entered the expanded view'
        assert record in loaded, f'placement {record} never loaded'
        # The loader runs every second guest frame. Scene latching can place
        # its camera one frame behind the simulation's decision.
        delays[record] = loaded[record]-eligible[0]
        assert -2 <= delays[record] <= 3, f'placement {record} loaded {delays[record]} frames late'
    pixels = 0
    samples = ((780,17),(780,18),(780,16),(860,20),(960,22),(1060,24)) if scenario == 'run' else ((260,13),(260,14))
    for frame, record in samples:
        cap = Capture(root, frame)
        assert not cap.ram[0x13d4] and not cap.ram[0x71], 'Mario must be alive and running'
        slots = [slot for slot in cap.active() if cap.ram[0x161a+slot] == record]
        assert len(slots) == 1, f'placement {record} missing/duplicated at frame {frame}'
        slot = slots[0]
        x = cap.ram[0xe4+slot]+256*cap.ram[0x14e0+slot]-cap.camera
        assert x >= 256 or x+48 <= 0, 'sample must be outside the native view'
        first = 64+cap.ram[0x15ea+slot]//4
        count = 4 if cap.ram[0x9e+slot] == 0x52 else 1
        pixels += sum(cap.piece(first+part) for part in range(count))
    if scenario == 'run':
        final = Capture(root, 1140)
        assert final.word(0x94) > 3250 and not final.ram[0x71], 'running route did not clear the first three holes'
    else:
        cap = Capture(root, 260)
        assert cap.width == 2134 and cap.camera == 1157
        assert all(cap.ram[0x14c8+slot] for slot in (8,9)), 'spare allocations not exercised'
        assert cap.ram[0x9e+7] == 0xae, 'Fishin Boo reservation changed'
    check_coverage(root)
    return dict(scenario=scenario, frames=len(data), width=widths.pop(),
                activation_delay_frames=delays, outside_sprite_pixels=pixels, unexplained_native_differences=0)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--build', type=Path, default=ROOT/'build-adaptive')
    ap.add_argument('--state', type=Path)
    ap.add_argument('--scenario', choices=FRAMES, default='run')
    ap.add_argument('--check', type=Path)
    args = ap.parse_args()
    if args.check:
        print(json.dumps(check(args.check.resolve(), args.scenario), indent=2))
        return
    if not args.state:
        ap.error('--state is required')
    build, state = args.build.resolve(), args.state.resolve(strict=True)
    digest = hashlib.sha256(state.read_bytes()).hexdigest()
    ram = snapshot_ram(state)
    assert ram[0x13bf] == 0x21 and ram[0x1692] == 0x11 and not ram[0x13d4]
    assert int.from_bytes(ram[0x1a:0x1c], 'little') == 1157, 'wrong F1 fixture'
    root = Path(tempfile.mkdtemp(prefix='smw-ghosts-', dir=build))
    print(f'Artifacts: {root}', flush=True)
    (root/'saves').mkdir()
    shutil.copy2(state, root/'saves/save0.sav')
    (root/'route.script').write_text(ROUTE if args.scenario == 'run' else 'wait 240\nloadstate 0\n')
    buttons = ('a','b','x','y','l','r','start','select','up','down','left','right')
    (root/'keybinds.ini').write_text(''.join(f'[player{p}]\n'+''.join(f'{b}=None\n' for b in buttons) for p in (1,2)))
    window = '2048x672' if args.scenario == 'run' else '2000x180'
    (root/'config.ini').write_text('[General]\nAutosave=0\nDisableFrameDelay=1\nSkipLauncher=1\n'
        f'[Graphics]\nWindowSize={window}\nNewRenderer=1\nNoSpriteLimits=1\nOutputMethod=SDL\n'
        '[Sound]\nEnableAudio=0\n[GamepadMap]\nEnableGamepad1=0\nEnableGamepad2=0\n'
        '[KeyMap]\nControls=\nControlsP2=\nTurbo=\nPause=\nPauseDimmed=\nReset=\nLoad=\nSave=\n')
    env = environment()
    env.update(SMW_RENDER_ASPECT='Fit', SMW_ENEMY_SPAWN='adaptive', SMW_RENDER_DIAGNOSTICS=str(root),
               SMW_RENDER_CAPTURE_FRAME=','.join(map(str, CAPTURES[args.scenario])))
    exe = build/('SuperMarioWorldSNESRecomp.exe' if os.name == 'nt' else 'SuperMarioWorldSNESRecomp')
    try:
        with (root/'stdout.log').open('w') as out, (root/'stderr.log').open('w') as err:
            process = subprocess.Popen([str(exe), '--config', str(root/'config.ini'), '--script', str(root/'route.script'),
                '--benchmark', str(FRAMES[args.scenario]), str(ROOT/'smw.sfc')], cwd=root, env=env, stdout=out, stderr=err)
            try:
                if os.name == 'nt':
                    deadline = time.monotonic()+160
                    while process.poll() is None and time.monotonic() < deadline:
                        owned_windows(process.pid)  # keep QA out of the owner's interactive game
                        time.sleep(.1)
                assert process.wait(timeout=160) == 0
            finally:
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=10)
    finally:
        assert hashlib.sha256(state.read_bytes()).hexdigest() == digest, 'original save changed'
    report = check(root, args.scenario)
    report['source_sha256'] = digest
    (root/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report), flush=True)


if __name__ == '__main__':
    main()
