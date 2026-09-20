#!/usr/bin/env python3
"""Check Yoshi's Island 2 camera coherence against native sky and terrain pixels.

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

FRAMES = tuple(sorted(set((2700, 2720, 2740, 2760, 2780, 2800, 2820, 2840, 2860, 2880)) |
                      set(range(2798, 2813))))


def sprite_spans(cap, y, native=False):
    """Captured OBJ rectangles, so falling shells cannot masquerade as terrain."""
    line = 0x20000+y*LINE_SIZE
    sizes = ((8,16),(8,32),(8,64),(16,32),(16,64),(32,64),(16,32),(16,32))
    spans = []
    for slot in range(128):
        pos, attr = struct.unpack_from('<HH', cap.raw, line+576+slot*4)
        high = cap.raw[line+66624+slot//4] >> (slot%4*2)
        size = sizes[cap.raw[line+1] >> 5][(high >> 1) & 1]
        if (y-(pos >> 8)) % 256 >= size: continue
        x = (pos & 255) | ((high & 1) << 8)
        if x >= 256: x -= 512
        if not native:
            owner, owner_pos, owner_attr, valid = struct.unpack_from('<iHH?', cap.raw, OWNER_OFFSET+slot*12)
            if valid and (owner_pos, owner_attr) == (pos, attr): x = owner
            elif x+size <= 0 or x >= 256: continue
            x += cap.offset
        spans.append((x, x+size))
    return spans


def check_terrain(root):
    # Fixed world samples taken from the native PPU when they are in its view.
    # Ground below Mario and the platform interior avoid moving sprites.
    oracle = Capture(root, 2880)
    samples = []
    for left, right, top, bottom in ((240,304,400,416), (400,432,352,384)):
        for wy in range(top, bottom):
            y = wy-oracle.word(0x1c)-1
            line = 0x20000+y*LINE_SIZE
            scroll = struct.unpack_from('<H', oracle.raw, line+14)[0]
            camera = oracle.camera+((scroll-oracle.camera+512) % 1024)-512
            spans = sprite_spans(oracle, y, native=True)
            for wx in range(left, right):
                assert 0 <= wx-camera < 256
                if any(left <= wx-camera < right for left, right in spans): continue
                color = struct.unpack_from('<I', oracle.raw,
                    OWNER_OFFSET+128*12+(y*256+wx-camera)*4)[0] & 0xffffff
                samples.append((wx, wy, color))
    compared = 0
    for frame in FRAMES:
        cap = Capture(root, frame)
        scroll = struct.unpack_from('<H', cap.raw, 0x20000+80*LINE_SIZE+14)[0]
        # Derive the desired visible camera from the actual uploaded PPU scroll,
        # independently of the renderer's captured offset or live RAM camera.
        camera = cap.camera+((scroll-cap.camera+512) % 1024)-512
        origin = max(0, min(camera-(cap.width-256)//2, max(0,(cap.ram[0x5e]+1)*256-cap.width)))
        span_cache = {}
        for wx, wy, expected in samples:
            sx, sy = wx-origin, wy-cap.word(0x1c)-1
            if not 0 <= sx < cap.width: continue
            if sy not in span_cache: span_cache[sy] = sprite_spans(cap, sy)
            if any(left <= sx < right for left, right in span_cache[sy]): continue
            actual = struct.unpack_from('<I', cap.bmp, cap.pixels+((223-sy)*cap.width+sx)*4)[0] & 0xffffff
            assert actual == expected, f'foreground jitter at frame {frame}, world {wx},{wy}, screen {sx},{sy}'
            compared += 1
    assert compared >= 30000, 'not enough unobscured terrain was checked'
    return compared


def check(root):
    terrain = check_terrain(root)
    samples = []
    compared = 0
    for frame in FRAMES:
        cap = Capture(root, frame)
        assert cap.ram[0x100] == 20 and cap.ram[0x1413] == 2
        assert cap.ram[0x1925] == 0 and cap.ram[0x13d4] == 0
        origin = cap.camera - cap.offset
        # $00:F79D derives BG2 = floor(BG1 / 2). Replace its camera component
        # with the visible origin, using the same frame as the captured PPU.
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
    assert set(phases) == {0}, 'stationary scenery drifted inside the camera clamp'
    crossed = any(s['origin'] > 0 for s in samples)
    if cap.width == 342: assert crossed, '16:9 route never crossed the camera clamp'
    trace = rows(root/'frames.csv')
    assert len(trace) == 2900 and max(int(r['unexplained_differences']) for r in trace) == 0
    moving = [r for r in trace if int(r['frame']) >= 2700]
    assert all(int(r['camera']) % 1024 == int(r['raster_camera']) for r in moving), 'mixed camera frames'
    assert any(r['camera'] != r['simulation_camera'] for r in moving), 'no presentation/simulation timing exercised'
    if cap.width == 558: assert crossed, 'reported-width route never crossed the camera clamp'
    benchmarks = [json.loads(line.removeprefix('SNESRECOMP_BENCHMARK ')) for line in
                  (root/'stdout.log').read_text().splitlines() if line.startswith('SNESRECOMP_BENCHMARK ')]
    assert len(benchmarks) == 1 and benchmarks[0]['frames'] == len(trace)
    assert not any(p.stat().st_size for p in root.rglob('dispatch_misses.log'))
    for path in root.glob('tier2_*.json'):
        coverage = json.loads(path.read_text())
        assert coverage['overflowed_tuples'] == coverage['journal_write_failures'] == 0
        assert not any(site['bail_hits'] for site in coverage['discoveries'])
    return dict(compared_sky_pixels=compared, compared_terrain_pixels=terrain,
                width=cap.width, widths=sorted({int(r['width']) for r in trace}),
                crossed_camera_clamp=crossed,
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
    # Controller polling reads keybinds.ini independently of [KeyMap]. Keep a
    # user's desktop input out of the scripted child without touching playtest.
    buttons = ('a','b','x','y','l','r','start','select','up','down','left','right')
    (root/'keybinds.ini').write_text(''.join(f'[player{player}]\n'+
        ''.join(f'{button}=None\n' for button in buttons) for player in (1,2)))
    (root/'route.script').write_text(ROUTE)
    (root/'config.ini').write_text('[General]\nAutosave=0\nDisableFrameDelay=1\nSkipLauncher=1\n'
        f'[Graphics]\nWindowSize={args.window}\nNewRenderer=1\nNoSpriteLimits=1\nOutputMethod=SDL\n'
        '[Sound]\nEnableAudio=0\n[GamepadMap]\nEnableGamepad1=0\nEnableGamepad2=0\n'
        '[KeyMap]\nControls=\nControlsP2=\nTurbo=\nPause=\nPauseDimmed=\nReset=\nLoad=\nSave=\n')
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
    print(json.dumps({k:v for k,v in report.items() if k != 'samples'}), flush=True)


if __name__ == '__main__': main()
