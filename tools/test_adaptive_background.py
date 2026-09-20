#!/usr/bin/env python3
"""Check Yoshi's Island 2 parallax against captured native PPU sky pixels.

Fresh save, ordinary controller input, Fit and Screen-based spawning. The
private raster captures remain in the selected build directory.
"""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile

from renderer_visibility import Capture, LINE_SIZE, OWNER_OFFSET
from test_adaptive_renderer import ROOT, ROUTE, environment, rows

FRAMES = (2700, 2720, 2740, 2760, 2780, 2800, 2820, 2840, 2860, 2880)


def check(root):
    samples = []
    compared = 0
    for frame in FRAMES:
        cap = Capture(root, frame)
        assert cap.ram[0x100] == 20 and cap.ram[0x1413] == 2
        assert cap.ram[0x1925] == 0 and cap.ram[0x13d4] == 0
        origin = cap.camera - cap.offset
        # $00:F79D derives BG2 = floor(BG1 / 2). Replace its camera component
        # with the visible origin, retaining the captured PPU's pipeline delay.
        shift = cap.offset + origin // 2 - cap.camera // 2
        line = 0x20000 + 64 * LINE_SIZE
        native_scroll = struct.unpack_from('<H', cap.raw, line + 16)[0]
        samples.append(dict(frame=frame, camera=cap.camera, origin=origin,
                            native_scroll=native_scroll,
                            expected_phase=native_scroll-cap.camera//2+origin//2))
        # Sky at y=48..95 is clear of terrain, HUD and objects on this route.
        # Compare to the ORIGINAL PPU image, translated by the parallax delta;
        # no host tile decoder or host-reported scroll is used as the oracle.
        for y in range(48, 96):
            for x in range(256):
                sx = x + cap.offset - shift
                if not 0 <= sx < cap.width: continue
                expected = struct.unpack_from('<I', cap.raw, OWNER_OFFSET+128*12+(y*256+x)*4)[0] & 0xffffff
                actual = struct.unpack_from('<I', cap.bmp, cap.pixels+((223-y)*cap.width+sx)*4)[0] & 0xffffff
                assert actual == expected, f'background drift at frame {frame}, camera {cap.camera}, pixel {sx},{y}'
                compared += 1
    assert samples[0]['camera'] == 0 and samples[-1]['camera'] > 100
    clamped = [s for s in samples if s['origin'] == 0]
    assert len({s['camera'] for s in clamped}) > 1, 'left clamp was not exercised'
    phases = [s['expected_phase'] for s in clamped]
    assert max(phases)-min(phases) <= 2, 'unexpected native raster timing'
    crossed = any(s['origin'] > 0 for s in samples)
    if cap.width == 342: assert crossed, '16:9 route never crossed the camera clamp'
    trace = rows(root/'frames.csv')
    assert len(trace) == 2900 and max(int(r['unexplained_differences']) for r in trace) == 0
    benchmarks = [json.loads(line.removeprefix('SNESRECOMP_BENCHMARK ')) for line in
                  (root/'stdout.log').read_text().splitlines() if line.startswith('SNESRECOMP_BENCHMARK ')]
    assert len(benchmarks) == 1 and benchmarks[0]['frames'] == len(trace)
    assert not any(p.stat().st_size for p in root.rglob('dispatch_misses.log'))
    for path in root.glob('tier2_*.json'):
        coverage = json.loads(path.read_text())
        assert coverage['overflowed_tuples'] == coverage['journal_write_failures'] == 0
        assert not any(site['bail_hits'] for site in coverage['discoveries'])
    return dict(compared_sky_pixels=compared, width=cap.width, crossed_camera_clamp=crossed,
                rendered_frames=len(trace), unexplained_differences=0, samples=samples)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=ROOT/'build-adaptive')
    parser.add_argument('--window', default='2048x352')
    parser.add_argument('--check', type=Path, help='check an existing capture directory')
    args = parser.parse_args()
    if args.check:
        print(json.dumps(check(args.check), indent=2));return
    build = args.build.resolve(strict=True)
    root = Path(tempfile.mkdtemp(prefix='smw-background-', dir=build))
    print(f'Artifacts: {root}', flush=True)
    (root/'route.script').write_text(ROUTE)
    (root/'config.ini').write_text('[General]\nAutosave=0\nDisableFrameDelay=1\nSkipLauncher=1\n'
        f'[Graphics]\nWindowSize={args.window}\nNewRenderer=1\nNoSpriteLimits=1\nOutputMethod=SDL\n'
        '[Sound]\nEnableAudio=0\n[KeyMap]\nTurbo=\nPause=\nPauseDimmed=\nReset=\nLoad=\nSave=\n')
    env = environment()
    env.update(SMW_RENDER_ASPECT='Fit', SMW_ENEMY_SPAWN='adaptive',
               SMW_RENDER_DIAGNOSTICS=str(root),
               SMW_RENDER_CAPTURE_FRAME=','.join(map(str, FRAMES)))
    exe = build/('SuperMarioWorldSNESRecomp.exe' if os.name == 'nt' else 'SuperMarioWorldSNESRecomp')
    with (root/'stdout.log').open('w') as out, (root/'stderr.log').open('w') as err:
        subprocess.run([str(exe), '--config', str(root/'config.ini'), '--script', str(root/'route.script'),
                        '--benchmark', '2900', str(ROOT/'smw.sfc')], cwd=root, env=env,
                       stdout=out, stderr=err, check=True, timeout=160)
    report = check(root)
    (root/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report), flush=True)


if __name__ == '__main__': main()
