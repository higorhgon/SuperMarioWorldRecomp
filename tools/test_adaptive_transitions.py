#!/usr/bin/env python3
"""Replay local stage-exit/pipe fixtures, or measure normally paced audio.

Uses copied saves, Fit and Screen-based spawning. Never pauses the runtime.
The exit fixture is the reported F1 near a right-hand exit; the pipe fixture
is the older Yoshi's Island 2 pipe-color save (Mario at 2927,336 on Yoshi).
"""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import time

from renderer_visibility import Capture, LINE_SIZE
from test_adaptive_renderer import ROOT, environment, rows

CAPTURES = dict(exit=(329, 330, 331, 340, 350, 358, 360, 361, 391),
                pipe=(1002, 1003, 1010, 1020, 1030, 1036, 1045, 1055, 1066))
FRAMES = dict(exit=950, pipe=1250)
ROUTES = dict(exit='wait 240\nloadstate 0\nwait 30\npress right 180\n',
    pipe='wait 240\nloadstate 0\nwait 30\n' +
         'press right+y+b 40\npress right+y 20\n'*6 +
         'press right+y+b 25\npress down 120\npress right+b 8\n'
         'press down 140\npress right 10\npress down 120\n')


def pixel(cap, x, y):
    return struct.unpack_from('<I', cap.bmp,
                              cap.pixels+((223-y)*cap.width+x)*4)[0] & 0xffffff


def check_coverage(root):
    manifests = list(root.glob('tier2_*.json'))
    assert len(manifests) == 1, 'missing completed execution coverage'
    coverage = json.loads(manifests[0].read_text())
    assert not coverage['overflowed_tuples'] and not coverage['journal_write_failures']
    assert not any(site['bail_hits'] for site in coverage['discoveries']), 'interpreter bailout'
    for path in root.glob('*miss*.log'):
        assert not path.read_text().strip(), f'dispatch misses: {path}'


def check_video(root, scenario):
    data = rows(root/'frames.csv')
    assert len(data) == FRAMES[scenario], 'incomplete replay'
    assert max(int(row['unexplained_differences']) for row in data) == 0
    benchmarks = [json.loads(line.removeprefix('SNESRECOMP_BENCHMARK '))
                  for line in (root/'stdout.log').read_text().splitlines()
                  if line.startswith('SNESRECOMP_BENCHMARK ')]
    assert len(benchmarks) == 1 and benchmarks[0]['frames'] == len(data)
    caps = [Capture(root, frame) for frame in CAPTURES[scenario]]
    assert all(not cap.ram[0x13d4] for cap in caps), 'fixture is paused'
    assert all(cap.width == caps[0].width and cap.width > 512 for cap in caps)
    checked = 0
    if scenario == 'exit':
        reference = caps[0]
        assert reference.ram[0x100] == 0x14
        for cap in caps[1:6]:
            assert cap.ram[0x100] == 0x0b, 'did not reach outgoing stage fade'
            # Static scenery beyond the native view must fade with INIDISP,
            # rather than disappear into black bars on the first fade frame.
            for y in range(48, 200):
                line = 0x20000+y*LINE_SIZE
                assert reference.raw[line] == 15
                brightness = cap.raw[line] & 15
                assert 0 < brightness <= 15
                for x in range(cap.width-128, cap.width):
                    source = pixel(reference, x, y)
                    expected = sum(((source >> shift & 255)*brightness//15) << shift
                                   for shift in (0, 8, 16))
                    assert pixel(cap, x, y) == expected, (cap.ram[0x100], x, y)
                    checked += 1
        assert caps[-1].ram[0x100] == 0x0e, 'did not reach overworld'
    else:
        modes = [cap.ram[0x100] for cap in caps]
        assert 0x0f in modes and 0x13 in modes and modes[-1] == 0x14
        for cap in caps:
            if cap.ram[0x100] not in (0x0f, 0x13):
                continue
            # The fixture has textured scenery on the right in both rooms.
            # Mosaic can change individual pixels; require lit scenery well
            # outside the native viewport throughout both fades instead.
            lit = visible = 0
            for y in range(48, 200):
                if cap.raw[0x20000+y*LINE_SIZE] & 15:
                    for x in range(cap.width-128, cap.width):
                        lit += 1
                        visible += pixel(cap, x, y) != 0
            if lit:
                assert visible > lit//8, 'pipe fade collapsed to native width'
                checked += lit
        assert checked > 19000, 'no visible pipe fade sampled'
    check_coverage(root)
    return dict(scenario=scenario, frames=len(data), width=caps[0].width,
                fade_pixels=checked, unexplained_native_differences=0)


def check_audio(root):
    lines = (root/'audio-stats.txt').read_text().splitlines()
    header = lines[0].removeprefix('# ').split()
    data = [dict(zip(header, map(int, line.split()))) for line in lines[1:]]
    assert len(data) >= 20 and data[-1]['ms']-data[0]['ms'] >= 20000
    assert max(row['underflows'] for row in data) == 0, 'audio starvation'
    assert max(row['dropped'] for row in data) == 0, 'audio FIFO overflow'
    assert 0 < data[-1]['priming'] <= 12, 'excessive buffer recovery'
    assert data[-1]['produced'] > 600000 and data[-1]['consumed'] > 600000
    assert data[-1]['prod_audio'] == 0, 'consumer advanced guest audio clock'
    check_coverage(root)
    return dict(seconds=(data[-1]['ms']-data[0]['ms'])/1000,
                underflows=0, dropped=0, priming_callbacks=data[-1]['priming'])


def owned_windows(pid, close=False):
    """Hide/close only this test's window, never the owner's game."""
    user = ctypes.WinDLL('user32')
    callback = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user.ShowWindow.argtypes = [wintypes.HWND, ctypes.c_int]
    user.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    @callback
    def visit(hwnd, _):
        owner = wintypes.DWORD()
        user.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid:
            if close:
                user.PostMessageW(hwnd, 0x10, 0, 0)
            else:
                user.ShowWindow(hwnd, 0)
        return True
    user.EnumWindows(visit, 0)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--build', type=Path, default=ROOT/'build-adaptive')
    ap.add_argument('--state', type=Path)
    ap.add_argument('--scenario', choices=ROUTES, default='exit')
    ap.add_argument('--window', default='3319x720')
    ap.add_argument('--audio', action='store_true', help='35 seconds of real paced playback (Windows)')
    ap.add_argument('--check', type=Path, help='check an existing artifact directory')
    args = ap.parse_args()
    if args.check:
        report = check_audio(args.check) if args.audio else check_video(args.check, args.scenario)
        print(json.dumps(report, indent=2))
        return
    if not args.state:
        ap.error('--state is required')
    if args.audio and os.name != 'nt':
        ap.error('paced audio window control currently requires Windows')
    build, state = args.build.resolve(), args.state.resolve(strict=True)
    digest = hashlib.sha256(state.read_bytes()).hexdigest()
    root = Path(tempfile.mkdtemp(prefix='smw-transition-', dir=build))
    print(f'Artifacts: {root}', flush=True)
    (root/'saves').mkdir()
    shutil.copy2(state, root/'saves/save0.sav')
    (root/'route.script').write_text(ROUTES[args.scenario])
    buttons = ('a','b','x','y','l','r','start','select','up','down','left','right')
    (root/'keybinds.ini').write_text(''.join(f'[player{p}]\n'+''.join(f'{b}=None\n' for b in buttons)
                                          for p in (1, 2)))
    (root/'config.ini').write_text(f'[General]\nAutosave=0\nDisableFrameDelay={int(not args.audio)}\nSkipLauncher=1\n'
        f'[Graphics]\nWindowSize={args.window}\nNewRenderer=1\nNoSpriteLimits=1\nOutputMethod=SDL\n'
        f'[Sound]\nEnableAudio={int(args.audio)}\n[GamepadMap]\nEnableGamepad1=0\nEnableGamepad2=0\n'
        '[KeyMap]\nControls=\nControlsP2=\nTurbo=\nPause=\nPauseDimmed=\nReset=\nLoad=\nSave=\n')
    env = environment()
    env.update(SMW_RENDER_ASPECT='Fit', SMW_ENEMY_SPAWN='adaptive',
               SNESRECOMP_AUDIO_STATS=str(root/'audio-stats.txt'))
    if not args.audio:
        env.update(SMW_RENDER_DIAGNOSTICS=str(root),
                   SMW_RENDER_CAPTURE_FRAME=','.join(map(str, CAPTURES[args.scenario])))
    exe = build/('SuperMarioWorldSNESRecomp.exe' if os.name == 'nt' else 'SuperMarioWorldSNESRecomp')
    command = [str(exe), '--config', str(root/'config.ini'), '--script', str(root/'route.script')]
    # Benchmark modes disable pacing, including --benchmark-audio. They cannot
    # measure whether the renderer keeps up with the real device clock.
    if not args.audio:
        command += ['--benchmark', str(FRAMES[args.scenario])]
    command += [str(ROOT/'smw.sfc')]
    try:
        with (root/'stdout.log').open('w') as out, (root/'stderr.log').open('w') as err:
            process = subprocess.Popen(command, cwd=root, env=env, stdout=out, stderr=err)
            try:
                if args.audio:
                    deadline = time.monotonic()+120
                    while time.monotonic() < deadline and process.poll() is None:
                        if 'first frame simulated' in (root/'stderr.log').read_text():
                            break
                        time.sleep(.5)
                    else:
                        raise RuntimeError('game did not reach its first frame')
                    owned_windows(process.pid)
                    time.sleep(35)
                    owned_windows(process.pid, close=True)
                assert process.wait(timeout=160) == 0, 'game exited abnormally'
            finally:
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=10)
    finally:
        assert hashlib.sha256(state.read_bytes()).hexdigest() == digest, 'original save changed'
    report = check_audio(root) if args.audio else check_video(root, args.scenario)
    report['source_sha256'] = digest
    (root/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report), flush=True)


if __name__ == '__main__':
    main()
