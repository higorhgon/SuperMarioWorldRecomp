#!/usr/bin/env python3
"""Check combined and legacy SMW game save chunks without running the game."""
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent.parent
build = ROOT / 'build-save-tests'
build.mkdir(exist_ok=True)
compiler = shutil.which('gcc')
if os.name == 'nt':
    compiler = 'C:/msys64/mingw64/bin/gcc.exe'
exe = build / ('savestate_test.exe' if os.name == 'nt' else 'savestate_test')
sources = ['test/falcon_harness/falcon_savestate_test.c', 'src/smw_cpu_infra.c',
           'src/smw_renderer.c', 'src/smw_renderer_hooks.c', 'src/smw_video.c',
           'src/mods/falcon/falcon_locomotion.c',
           'src/mods/falcon/captain_falcon_foreign.c', 'src/foreign_controller.c']
subprocess.run([compiler, '-std=c11', '-O2', '-flto', '-fwhole-program',
                '-ffunction-sections', '-fdata-sections', '-DSNESRECOMP_TRACE=0',
                '-I.', '-Isrc', '-Isnesrecomp/runner/src', *sources,
                '-Wl,--gc-sections', '-lm', '-o', str(exe)], cwd=ROOT, check=True)
subprocess.run([str(exe)], cwd=ROOT, check=True)
