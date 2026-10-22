"""Controlled foliage comparisons using the current working executable and settings."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import time

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'launcher'))
import kzsf_local as ui

MODES={
    'baseline': {},
    'linear-readback': {'KZSF_READBACK_LINEAR':'1'},
    'strict-readback': {'KZSF_STALE_READBACK':'0','KZSF_DEFERRED_READBACK':'0'},
    'no-fsr': {'KZSF_UPSCALER':'none'},
    'native-linear': {'KZSF_UPSCALER':'none','KZSF_READBACK_LINEAR':'1'},
}
KEYS=('KZSF_BETA_MEMORY_LAYOUT','KZSF_DMEM_MB','KZSF_FPS_LIMIT','KZSF_UPSCALER',
      'KZSF_READBACK_LINEAR','KZSF_STALE_READBACK','KZSF_DEFERRED_READBACK',
      'KZSF_ASYNC_SHADERS','KZSF_DRAW_PIPE','KZSF_UNLIMITED_HEALTH','KZSF_INFINITE_AMMO')

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('mode',choices=MODES)
    parser.add_argument('--report-only',action='store_true')
    args=parser.parse_args()
    values=ui.load_preferences()
    folder=ui.ROOT/'out'/('foliage-'+args.mode+'-'+time.strftime('%Y%m%d-%H%M%S'))
    spec=ui.launch_spec(values,folder)
    # Isolate these comparisons from diagnostics inherited through a BAT window.
    spec['env']['KZSF_READBACK_LINEAR']='0'
    spec['env']['KZSF_HEALTH_TRACE']='0'
    spec['env'].pop('KZSF_ALLOCATION_DUMP',None)
    spec['env'].update(MODES[args.mode])
    report={'mode':args.mode,'executable':spec['command'][0],
            'settings':{k:spec['env'][k] for k in KEYS}}
    if args.report_only:
        print(json.dumps(report,indent=2)); return 0
    running=subprocess.run(['tasklist','/FI','IMAGENAME eq kzsf-probe*','/FO','CSV','/NH'],
                           capture_output=True,text=True,creationflags=subprocess.CREATE_NO_WINDOW)
    if 'kzsf-probe' in running.stdout.lower():
        print('Close the current game before running this comparison.'); return 1
    ok,reason=ui.validate_game(values['game_dir'])
    if not ok: print(reason); return 1
    if not Path(spec['command'][0]).is_file(): print('Selected game executable is missing.'); return 1
    folder.mkdir(parents=True)
    manifest=folder/'session.json'
    env=spec.pop('env')
    manifest.write_text(json.dumps(spec,indent=2),encoding='utf-8')
    (folder/'comparison.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    (folder/'run.txt').write_text('Foliage comparison: '+args.mode+'\n',encoding='utf-8')
    print('Mode:',args.mode)
    print('Use the same checkpoint and look at the same broken tree for 20 seconds.')
    print('Report whether the white/striped leaves change, and the approximate FPS.')
    print('Your normal launcher preferences and caches are preserved.')
    print('Logs:',folder,flush=True)
    os.environ.update(env)
    ui.run_session(manifest)
    print('Game closed. Comparison logs saved.')
    return 0

if __name__=='__main__': raise SystemExit(main())
