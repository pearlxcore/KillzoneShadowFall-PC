"""One short damage capture using the user's launcher settings; no external debugger."""
import sys, os, json, subprocess, time
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'launcher'))
import kzsf_local as ui

def main():
    capture='--play' not in sys.argv
    running=subprocess.run(['tasklist','/FI','IMAGENAME eq kzsf-probe*','/FO','CSV','/NH'],capture_output=True,text=True)
    if 'kzsf-probe' in running.stdout.lower():
        print('Close the current game first, then run this BAT again.'); return 1
    values=ui.load_preferences()
    ok,reason=ui.validate_game(values['game_dir'])
    if not ok: print(reason); return 1
    values['unlimited_health']=True
    if not capture: values['infinite_ammo']=True
    folder=ui.ROOT/'out'/(('health-test-' if capture else 'player-assists-')+time.strftime('%Y%m%d-%H%M%S'))
    folder.mkdir(parents=True)
    spec=ui.launch_spec(values,folder)
    if capture: spec['command'][0]=str(ui.ROOT/'out/kzsf-probe-health-trace.exe')
    spec['env']['KZSF_HEALTH_TRACE']='1' if capture else '0'
    if not Path(spec['command'][0]).is_file(): print('Health capture build missing.'); return 1
    os.environ.update(spec.pop('env'))
    manifest=folder/'session.json'
    manifest.write_text(json.dumps(spec,indent=2),encoding='utf-8')
    (folder/'run.txt').write_text(('Health identification capture' if capture else 'Player invulnerability v2 and infinite ammo')+'; original launch settings retained.\n',encoding='utf-8')
    if capture:
        print('Continue campaign. Do not shoot; stand still and let an enemy shoot you for a few seconds.')
        print('Then close the game and reply captured. This is a diagnostic, not a new immortality fix.')
    else:
        print('Player invulnerability v2 + infinite ammo. Current launcher FPS and memory settings retained.')
    print('Logs:',folder,flush=True)
    ui.run_session(manifest)
    print('Game closed. Return to chat with your result.')
    return 0

if __name__=='__main__': raise SystemExit(main())
