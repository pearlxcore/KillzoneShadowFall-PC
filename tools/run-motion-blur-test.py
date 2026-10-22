"""Isolated comparison of the game's native motion-blur on/off feature bit."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'launcher'))
import kzsf_local as ui

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--on',action='store_true',help='Original motion blur for comparison')
    parser.add_argument('--report-only',action='store_true')
    args=parser.parse_args()
    values=ui.load_preferences()
    values.update(motion_blur=args.on,upscaler='none')
    folder=ui.ROOT/'out'/('motion-blur-'+('on-' if args.on else 'off-')+time.strftime('%Y%m%d-%H%M%S'))
    spec=ui.launch_spec(values,folder)
    spec['command'][0]=str(ui.ROOT/'out/kzsf-probe-motion-blur.exe')
    spec['env']['KZSF_CONFIG']=str(folder/'settings.ini')
    report=dict(motion_blur=args.on,upscaler='none',executable=spec['command'][0],
                disable_flag=spec['env']['KZSF_DISABLE_MOTION_BLUR'])
    if args.report_only:
        print(json.dumps(report,indent=2));return 0
    running=subprocess.run(['tasklist','/FI','IMAGENAME eq kzsf-probe*','/FO','CSV','/NH'],capture_output=True,text=True,
                           creationflags=subprocess.CREATE_NO_WINDOW)
    if 'kzsf-probe' in running.stdout.lower():print('Close the current game first.');return 1
    ok,reason=ui.validate_game(values['game_dir'])
    if not ok:print(reason);return 1
    if not Path(spec['command'][0]).is_file():print('Motion blur test build is missing.');return 1
    folder.mkdir(parents=True)
    if ui.CONFIG.is_file():shutil.copyfile(ui.CONFIG,folder/'settings.ini')
    ui.write_keys(folder/'settings.ini',dict(upscaler='none'))
    (folder/'comparison.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    manifest=folder/'session.json'
    manifest.write_text(json.dumps({k:v for k,v in spec.items() if k!='env'}),encoding='utf-8')
    print('Motion blur:', 'ORIGINAL' if args.on else 'OFF (experimental native engine switch)')
    print('Native rendering. Your other settings and assists are retained.')
    print('Load gameplay and turn the camera. Check blur, lighting and other effects.')
    print('Your saved launcher settings are unchanged. Logs:',folder,flush=True)
    monitor=subprocess.Popen([sys.executable,str(Path(ui.__file__)),'--run-session',str(manifest)],
                             env=spec['env'],cwd=ui.ROOT,creationflags=subprocess.CREATE_NO_WINDOW)
    monitor.wait()
    return 0

if __name__=='__main__':raise SystemExit(main())
