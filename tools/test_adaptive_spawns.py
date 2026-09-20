#!/usr/bin/env python3
"""Replay the reported Koopa duplication from a local Yoshi's Island 2 F3 save.

All phases use Fit and Screen-based activation. Preparation uses normal input
to leave the old, already-duplicated enemies behind, then approaches a fresh
shell/Koopa pair. No guest RAM is patched and the supplied save is never changed.
"""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

from renderer_visibility import Capture
from test_adaptive_renderer import ROOT, environment, rows


def snapshot_ram(path):
    data = path.read_bytes()
    assert struct.unpack_from('<II',data) == (0x52544c53,7), 'fixture requires RTLS v7'
    foreign_size = 12 + 4096 + 256  # SFW1 header + fixed controller capacity
    combined_size = 4 + 141 + foreign_size
    if data[-combined_size:-combined_size+4] == b'SMX1':
        data = data[:-combined_size]
    elif data[-foreign_size:-foreign_size+4] == b'SFW1':
        data = data[:-foreign_size]
    elif data[-141:-137] == b'SMWS':
        data = data[:-141]
    # snes_saveload ends with WRAM, ramAdr (4), then the v7 joypad fields (7).
    ram = data[-0x20000-11:-11]
    assert len(ram) == 0x20000 and ram[0x100] == 20, 'fixture must be in a level'
    return ram


def run(build, root, state, window, route, frames, captures):
    root.mkdir();(root/'saves').mkdir()
    shutil.copy2(state,root/'saves/save0.sav')
    (root/'route.script').write_text('wait 240\nloadstate 0\n'+route)
    (root/'config.ini').write_text('[General]\nAutosave=0\nDisableFrameDelay=1\nSkipLauncher=1\n'
        f'[Graphics]\nWindowSize={window}\nNewRenderer=1\nNoSpriteLimits=1\nOutputMethod=SDL\n'
        '[Sound]\nEnableAudio=0\n[KeyMap]\nTurbo=\nPause=\nPauseDimmed=\nReset=\nLoad=\nSave=\n')
    env=environment()
    env.update(SMW_RENDER_ASPECT='Fit',SMW_ENEMY_SPAWN='adaptive',
               SMW_RENDER_DIAGNOSTICS=str(root),SMW_RENDER_CAPTURE_FRAME=captures)
    exe=build/('SuperMarioWorldSNESRecomp.exe' if os.name=='nt' else 'SuperMarioWorldSNESRecomp')
    with (root/'stdout.log').open('w') as out,(root/'stderr.log').open('w') as err:
        subprocess.run([str(exe),'--config',str(root/'config.ini'),'--script',str(root/'route.script'),
                        '--benchmark',str(frames),str(ROOT/'smw.sfc')],cwd=root,env=env,
                       stdout=out,stderr=err,check=True,timeout=160)
    lines=(root/'stdout.log').read_text().splitlines()
    results=[json.loads(line.removeprefix('SNESRECOMP_BENCHMARK ')) for line in lines
             if line.startswith('SNESRECOMP_BENCHMARK ')]
    assert len(results)==1 and results[0]['frames']==frames, 'incomplete simulation'
    trace=rows(root/'frames.csv')
    assert len(trace)==frames, 'missing rendered frames'
    assert max(int(row['unexplained_differences']) for row in trace)==0, 'native rendering regressed'


def check(root):
    sprites=[{k:int(v) for k,v in row.items()} for row in csv.DictReader((root/'sprites.csv').open())]
    pair=[row for row in sprites if row['frame']>240 and row['record'] in (10,11)]
    assert pair and all(row['paused']==0 for row in pair), 'fixture did not run freely'
    assert any(row['record']==10 and row['status']==9 for row in pair), 'missing empty shell'
    # The next simulation tick decrements 2 to 1, then consumes the Koopa.
    entry=next((row for row in pair if row['record']==11 and row['enter_timer']==2),None)
    assert entry is not None, 'Koopa never entered its shell'
    assert any(row['record']==10 and row['frame']==entry['frame'] and row['slot']==entry['target']
               for row in pair), 'Koopa targeted the wrong shell'
    consumed=entry['frame']+1
    assert not any(row['record']==11 and row['frame']>=consumed for row in pair), 'consumed Koopa respawned'
    assert any(row['record']==10 and row['status']==8 and row['frame']>consumed for row in pair), 'shell never became a Koopa'
    events=list(csv.DictReader((root/'spawns.csv').open()))
    assert not any(row['record']=='11' and row['event']=='candidate' and int(row['frame'])>=consumed
                   for row in events), 'consumed trigger became eligible again'
    # Confirm the filled Koopa is actually drawn, before and after the rewind.
    visible=0
    for frame in (340,500):
        cap=Capture(root,frame)
        slots=[i for i in cap.active() if cap.ram[0x161a+i]==10]
        assert len(slots)==1 and cap.ram[0x14c8+slots[0]]==8
        first=64+cap.ram[0x15ea+slots[0]]//4
        visible+=cap.piece(first+1)+cap.piece(first+2)
    saved=root/'saves/save1.sav'
    ram=snapshot_ram(saved)
    assert ram[0x1938+11]==0, 'save did not exercise a consumed native trigger'
    assert saved.read_bytes()[-141:-137]==b'SMWS', 'missing host gameplay save chunk'
    assert saved.read_bytes()[-141+12+11]!=0, 'consumed activation guard was not serialized'
    assert '*** Loading slot 1:' in (root/'stdout.log').read_text(), 'snapshot was not reloaded'
    return dict(shell_entry_frame=consumed,extra_koopas=0,visible_koopa_pixels=visible,
                save_load_guard=True,rendered_frames=len(rows(root/'frames.csv')))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--state',type=Path,required=True,help='reported local F3 (save2.sav)')
    parser.add_argument('--build',type=Path,default=ROOT/'build-adaptive')
    parser.add_argument('--window',default='2048x352')
    args=parser.parse_args()
    state=args.state.resolve(strict=True);build=args.build.resolve(strict=True)
    before=hashlib.sha256(state.read_bytes()).hexdigest()
    ram=snapshot_ram(state)
    assert int.from_bytes(ram[0xce:0xd1],'little')==0x07c532, 'fixture is not Yoshi Island 2'
    root=Path(tempfile.mkdtemp(prefix='smw-spawns-',dir=build));print(f'Artifacts: {root}',flush=True)
    # Only send Start when the supplied snapshot is already paused.
    resume=f'wait {ram[0x13d3]+2}\npress start 5\n' if ram[0x13d4] else ''
    run(build,root/'prepare',state,'1280x720',resume+
        'wait 30\npress left 150\npress left+b+y 100\nwait 60\n'
        'press right+b+y 70\nwait 30\nsavestate 1\n',800,'790')
    prepared=root/'prepare/saves/save1.sav';ram=snapshot_ram(prepared)
    assert ram[0x13d4]==0 and not any(ram[0x1938+10:0x1938+14]), 'old pair did not despawn'
    # Approach at the reported width before widening to 100:9 if requested.
    # This ensures the native ten-slot allocation budget already contains the
    # target pair; an empty pool test would never exercise shell consumption.
    run(build,root/'approach',prepared,'2048x352',
        'wait 20\npress right 150\nsavestate 1\n',440,'420')
    ready=root/'approach/saves/save1.sav';ram=snapshot_ram(ready)
    assert any(ram[0x161a+i]==10 and ram[0x14c8+i]==9 for i in range(12)), 'shell not prepared'
    assert any(ram[0x161a+i]==11 and ram[0x14c8+i]==8 for i in range(12)), 'Koopa not prepared'
    run(build,root/'verify',ready,args.window,
        'press right 130\nwait 40\nsavestate 1\nwait 20\nloadstate 1\n',580,
        '260,300,340,380,420,460,500,560')
    report=check(root/'verify')
    report['window']=args.window
    assert hashlib.sha256(state.read_bytes()).hexdigest()==before, 'source save changed'
    (root/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report),flush=True)


if __name__=='__main__': main()
