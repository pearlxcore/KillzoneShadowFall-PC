"""One-variable aiming comparisons with isolated configuration/logs."""
import argparse,json,shutil,subprocess,sys,time
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'launcher'))
import kzsf_local as ui
def main():
 p=argparse.ArgumentParser();p.add_argument('--mode',choices=('linear','original','ramp','assist','responsive'),default='linear');p.add_argument('--report-only',action='store_true');a=p.parse_args()
 v=ui.load_preferences();v.update(look_curve='0',look_deadzone='8',look_ramp=True,look_assist=True)
 if a.mode=='linear':v['look_curve']='1'
 if a.mode=='ramp':v['look_ramp']=False
 if a.mode=='assist':v['look_assist']=False
 if a.mode=='responsive':v.update(look_curve='1',look_ramp=False)
 folder=ui.ROOT/'out'/('aim-controls-'+a.mode+'-'+time.strftime('%Y%m%d-%H%M%S'))
 spec=ui.launch_spec(v,folder);spec['env']['KZSF_CONFIG']=str(folder/'settings.ini')
 spec['command'][0]=str(ui.ROOT/'out/kzsf-probe-aim-controls.exe')
 if a.mode=='responsive':spec['env']['KZSF_PAD_DRIFT_LOG']='1'
 report={k:v[k] for k in ('look_curve','look_deadzone','look_ramp','look_assist')}
 if a.report_only:print(json.dumps(report,indent=2));return 0
 running=subprocess.run(['tasklist','/FI','IMAGENAME eq kzsf-probe*','/FO','CSV','/NH'],capture_output=True,text=True,creationflags=subprocess.CREATE_NO_WINDOW)
 if 'kzsf-probe' in running.stdout.lower():print('Close the current game first.');return 1
 ok,reason=ui.validate_game(v['game_dir'])
 if not ok:print(reason);return 1
 folder.mkdir(parents=True)
 if ui.CONFIG.exists():shutil.copyfile(ui.CONFIG,folder/'settings.ini')
 ui.write_keys(folder/'settings.ini',{k:str(int(value)) if isinstance(value,bool) else value for k,value in report.items()})
 (folder/'comparison.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
 manifest=folder/'session.json';manifest.write_text(json.dumps({k:x for k,x in spec.items() if k!='env'}),encoding='utf-8')
 print('Aiming comparison:',a.mode,'; saved launcher settings unchanged.')
 if a.mode=='responsive':
  print('Play normally. No comparisons needed. Report only unwanted stick drift or difficulty aiming.')
  print('Linear response, 8% look deadzone, ramp smoothing OFF; aim assistance retained.')
 else:
  print('First compare option 1 against 2: small look-stick movement, full turns, ADS and sprint.')
  print('Option 3 isolates input ramp; option 4 isolates enemy slowdown/tracking.')
 print('Logs:',folder,flush=True)
 return subprocess.call([sys.executable,str(Path(ui.__file__)),'--run-session',str(manifest)],env=spec['env'],cwd=ui.ROOT)
if __name__=='__main__':raise SystemExit(main())
