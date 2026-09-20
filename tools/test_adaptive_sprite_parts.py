#!/usr/bin/env python3
"""Check winged-block wings or a jumping Piranha head from local F1/F2 saves.

Copies saves into an isolated running game with Fit and Screen-based spawning.
Requires actual OBJ pixels outside the native view; never pauses the runtime.
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

from renderer_visibility import Capture, OWNER_OFFSET
from test_adaptive_renderer import ROOT, environment, rows

FRAMES = dict(wings=(242, 246, 250, 260, 280, 300, 320, 340, 360),
              # The plant descends behind its pipe after frame 260. Sample
              # the complete head above the pipe, in both animation poses.
              plant=(242, 246, 250, 254, 260))


def check(root, scenario):
    checks = []
    outside = 0
    for frame in FRAMES[scenario]:
        cap = Capture(root, frame)
        assert not cap.ram[0x13d4], 'fixture must be running, not paused'
        sprite_id = 0x83 if scenario == 'wings' else 0x4f
        slots = [i for i in cap.active() if cap.ram[0x9e+i] == sprite_id]
        assert len(slots) == 1, f'expected one {scenario} sprite, got {slots}'
        first = 64 + cap.ram[0x15ea+slots[0]]//4
        # Winged block restores its allocation after drawing both wings;
        # jumping Piranha leaves it advanced past the head to the stem.
        parts = (first+1, first+2) if scenario == 'wings' else (first-1,)
        occluders = []
        if scenario == 'wings':
            # The block's lower OAM index draws over the inner wing pixels.
            # Check the block itself and every wing pixel outside its footprint.
            cap.piece(first)
            x, pos = struct.unpack_from('<iH', cap.raw, OWNER_OFFSET+first*12)
            occluders = [(cap.offset+x, pos>>8, cap.offset+x+16, (pos>>8)+16)]
        elif frame == FRAMES[scenario][0]:
            cap.piece(first)
            cap.piece(first+1)
        pixels = [cap.piece(part, occluders) for part in parts]
        xs = [struct.unpack_from('<i', cap.raw, OWNER_OFFSET+part*12)[0] for part in parts]
        outside += sum(x+16 <= 0 or x >= 256 for x in xs)
        checks.append(dict(frame=frame, camera=cap.camera, width=cap.width,
                           parts=list(parts), x=xs, visible_pixels=pixels))
    assert outside >= 5, 'fixture did not exercise the expanded view'
    data = rows(root/'frames.csv')
    assert len(data) == 380, f'missing rendered frames: {len(data)}/380'
    assert max(int(row['unexplained_differences']) for row in data) == 0
    log = (root/'stdout.log').read_text()
    benchmark = [json.loads(line.removeprefix('SNESRECOMP_BENCHMARK '))
                 for line in log.splitlines() if line.startswith('SNESRECOMP_BENCHMARK ')]
    assert len(benchmark) == 1 and benchmark[0]['frames'] == 380
    manifests = list(root.glob('tier2_*.json'))
    assert len(manifests) == 1, 'missing completed execution coverage'
    coverage = json.loads(manifests[0].read_text())
    assert not coverage['overflowed_tuples'] and not coverage['journal_write_failures']
    assert not any(site['bail_hits'] for site in coverage['discoveries']), 'interpreter bailout'
    for path in root.glob('*miss*.log'):
        assert not path.read_text().strip(), f'dispatch misses: {path}'
    return dict(scenario=scenario, outside_parts=outside,
                visible_pixels=sum(sum(row['visible_pixels']) for row in checks), captures=checks)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--build', type=Path, default=ROOT/'build-adaptive')
    ap.add_argument('--state', type=Path)
    ap.add_argument('--scenario', choices=('wings', 'plant'), required=True)
    ap.add_argument('--window', default='2095x720')
    ap.add_argument('--check', type=Path, help='recheck an existing capture directory')
    args = ap.parse_args()
    if args.check:
        print(json.dumps(check(args.check.resolve(), args.scenario), indent=2))
        return
    if not args.state:
        ap.error('--state is required for a live run')
    build, state = args.build.resolve(), args.state.resolve(strict=True)
    digest = hashlib.sha256(state.read_bytes()).hexdigest()
    root = Path(tempfile.mkdtemp(prefix=f'smw-parts-{args.scenario}-', dir=build))
    print(f'Artifacts: {root}', flush=True)
    (root/'saves').mkdir()
    shutil.copy2(state, root/'saves/save0.sav')
    (root/'route.script').write_text('wait 240\nloadstate 0\n')
    buttons = ('a','b','x','y','l','r','start','select','up','down','left','right')
    (root/'keybinds.ini').write_text(''.join(f'[player{p}]\n'+''.join(f'{b}=None\n' for b in buttons)
                                           for p in (1,2)))
    (root/'config.ini').write_text('[General]\nAutosave=0\nDisableFrameDelay=1\nSkipLauncher=1\n'
        f'[Graphics]\nWindowSize={args.window}\nNewRenderer=1\nNoSpriteLimits=1\nOutputMethod=SDL\n'
        '[Sound]\nEnableAudio=0\n[GamepadMap]\nEnableGamepad1=0\nEnableGamepad2=0\n'
        '[KeyMap]\nControls=\nControlsP2=\nTurbo=\nPause=\nPauseDimmed=\nReset=\nLoad=\nSave=\n')
    env = environment()
    env.update(SMW_RENDER_ASPECT='Fit', SMW_ENEMY_SPAWN='adaptive',
               SMW_RENDER_DIAGNOSTICS=str(root), SMW_RENDER_CAPTURE_FRAME=','.join(map(str, FRAMES[args.scenario])))
    exe = build/('SuperMarioWorldSNESRecomp.exe' if os.name == 'nt' else 'SuperMarioWorldSNESRecomp')
    try:
        with (root/'stdout.log').open('w') as out, (root/'stderr.log').open('w') as err:
            subprocess.run([str(exe), '--config', str(root/'config.ini'), '--script', str(root/'route.script'),
                            '--benchmark', '380', str(ROOT/'smw.sfc')], cwd=root, env=env,
                           stdout=out, stderr=err, check=True, timeout=160)
    finally:
        assert hashlib.sha256(state.read_bytes()).hexdigest() == digest, 'original save changed'
    report = check(root, args.scenario)
    report['source_sha256'] = digest
    (root/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report), flush=True)


if __name__ == '__main__':
    main()
