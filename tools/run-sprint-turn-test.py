"""Compare sprint turn response using the current launcher build and isolated settings."""
import argparse,json,shutil,subprocess,sys,time
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'launcher'))
import kzsf_local as ui
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--percent',type=int,choices=(100,200),default=200)
    parser.add_argument('--report-only',action='store_true');args=parser.parse_args()
    values=ui.load_preferences();values['sprint_turn_speed']=str(args.percent)
    folder=ui.ROOT/'out'/('sprint-turn-'+str(args.percent)+'-'+time.strftime('%Y%m%d-%H%M%S'))
    spec=ui.launch_spec(values,folder);spec['env']['GOW3_CONFIG']=str(folder/'settings.ini')
    if args.report_only:
        print(json.dumps(dict(percent=args.percent,executable=spec['command'][0],hook=spec['env']['KZSF_PLAYER_TURN']),indent=2));return 0
    running=subprocess.run(['tasklist','/FI','IMAGENAME eq gow3-probe*','/FO','CSV','/NH'],capture_output=True,text=True,creationflags=subprocess.CREATE_NO_WINDOW)
    if 'gow3-probe' in running.stdout.lower():print('Close the current game first.');return 1
    ok,reason=ui.validate_game(values['game_dir'])
    if not ok:print(reason);return 1
    folder.mkdir(parents=True)
    if ui.CONFIG.exists():shutil.copyfile(ui.CONFIG,folder/'settings.ini')
    ui.write_keys(folder/'settings.ini',{'sprint_turn_speed':str(args.percent)})
    manifest=folder/'session.json';manifest.write_text(json.dumps({k:v for k,v in spec.items() if k!='env'}),encoding='utf-8')
    print('Sprint horizontal turn speed:',str(args.percent)+'%')
    print('In an open area, compare turning while walking and while sprinting.')
    print('Try controller and mouse if available. Walking and vertical look should be unchanged.')
    print('Saved launcher settings are unchanged. Logs:',folder,flush=True)
    return subprocess.call([sys.executable,str(Path(ui.__file__)),'--run-session',str(manifest)],env=spec['env'],cwd=ui.ROOT)
if __name__=='__main__':raise SystemExit(main())
