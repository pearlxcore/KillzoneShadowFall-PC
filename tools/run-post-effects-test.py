"""Isolated gameplay comparison of the native post-process toggles.

Bloom and film grain are startup patches of the game's own post-process feature
mask (bits 2 and 3), so each mode needs its own launch. Everything else - timing,
memory profile, FSR setting, assists and controls - is taken from the user's
current launcher preferences and is not modified.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'launcher'))
import kzsf_local as ui

MODES=('original','bloom','grain','both')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode',choices=MODES)
    parser.add_argument('--report-only',action='store_true')
    args=parser.parse_args()
    values=ui.load_preferences()
    # original = both effects on (the game default).
    values.update(bloom=args.mode not in ('bloom','both'),
                  film_grain=args.mode not in ('grain','both'))
    folder=ui.ROOT/'out'/('post-fx-'+args.mode+'-'+time.strftime('%Y%m%d-%H%M%S'))
    spec=ui.launch_spec(values,folder)
    spec['command'][0]=str(ui.ROOT/'out/kzsf-probe-post-effects.exe')
    spec['env']['KZSF_CONFIG']=str(folder/'settings.ini')
    report={key:values.get(key) for key in ('motion_blur','depth_of_field','bloom','film_grain')}
    report['mode']=args.mode
    if args.report_only:
        print(json.dumps(report,indent=2));return 0
    running=subprocess.run(['tasklist','/FI','IMAGENAME eq kzsf-probe*','/FO','CSV','/NH'],
                           capture_output=True,text=True,creationflags=subprocess.CREATE_NO_WINDOW)
    if 'kzsf-probe' in running.stdout.lower():
        print('Close the current Killzone game first, then run this BAT again.');return 1
    ok,reason=ui.validate_game(values['game_dir'])
    if not ok:
        print(reason);return 1
    if not Path(spec['command'][0]).is_file():
        print('The post-effects test build is missing: out/kzsf-probe-post-effects.exe');return 1
    folder.mkdir(parents=True)
    if ui.CONFIG.is_file():shutil.copyfile(ui.CONFIG,folder/'settings.ini')
    ui.write_keys(folder/'settings.ini',{k:str(int(v)) for k,v in report.items() if isinstance(v,bool)})
    (folder/'comparison.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    manifest=folder/'session.json'
    manifest.write_text(json.dumps({k:v for k,v in spec.items() if k!='env'},indent=2),encoding='utf-8')
    print('Mode:',args.mode,'- bloom:',values['bloom'],'film grain:',values['film_grain'],
          '(unchanged from your preferences: motion blur',values['motion_blur'],', DOF',values['depth_of_field'],')')
    print('Look at a dark area with a bright light source (bloom) and a large flat mid-tone wall')
    print('or the sky (film grain) in unpaused gameplay, then compare with mode "original".')
    print('These are startup patches: start a fresh session per mode. Overlay Image shows the same switches.')
    print('Logs:',folder,flush=True)
    subprocess.Popen([sys.executable,str(Path(ui.__file__)),'--run-session',str(manifest)],
                     env=spec['env'],cwd=ui.ROOT,creationflags=subprocess.CREATE_NO_WINDOW).wait()
    return 0

if __name__=='__main__':raise SystemExit(main())
