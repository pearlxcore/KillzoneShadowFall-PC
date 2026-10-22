"""Isolated experimental native gameplay free-camera test. No saved preference changes."""
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
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report-only',action='store_true')
    parser.add_argument('--startup-smoke',action='store_true')
    parser.add_argument('--body',action='store_true',help='test native full-model visibility while free camera is active')
    args=parser.parse_args()
    values=ui.load_preferences()
    folder=ui.ROOT/'out'/('free-camera-'+time.strftime('%Y%m%d-%H%M%S'))
    spec=ui.launch_spec(values,folder)
    spec['command'][0]=str(ui.ROOT/('out/kzsf-probe-free-camera-body.exe' if args.body else 'out/kzsf-probe-free-camera.exe'))
    spec['env'].update(KZSF_FREE_CAMERA_TEST='1',KZSF_FREE_CAMERA_BODY='1' if args.body else '0',
                       KZSF_CONFIG=str(folder/'settings.ini'))
    report={'executable':spec['command'][0],'flag':spec['env']['KZSF_FREE_CAMERA_TEST'],'body_test':args.body,
            'controls':'F8 toggle, F7 recenter; W/S or left stick up/down rise/lower; A/D or left stick left/right strafe; R1/E forward, L1/Q backward; mouse/right stick/arrows look; PageUp/PageDown also rise/lower; Shift fast, Ctrl slow'}
    if args.report_only:
        print(json.dumps(report,indent=2));return 0
    running=subprocess.run(['tasklist','/FI','IMAGENAME eq kzsf-probe*','/FO','CSV','/NH'],
                           capture_output=True,text=True,creationflags=subprocess.CREATE_NO_WINDOW)
    if 'kzsf-probe' in running.stdout.lower():
        print('Close the running game before starting the free-camera test.');return 1
    ok,reason=ui.validate_game(values['game_dir'])
    if not ok:print(reason);return 1
    folder.mkdir(parents=True)
    if ui.CONFIG.is_file():shutil.copyfile(ui.CONFIG,folder/'settings.ini')
    (folder/'comparison.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    if args.startup_smoke:
        # Do not touch the user's campaign for an automated startup check.
        profile=folder/'user';profile.mkdir()
        if (ui.ROOT/'user/savedata').is_dir():
            shutil.copytree(ui.ROOT/'user/savedata',profile/'savedata')
        spec['env'].update(KZSF_USER_DIR=str(profile),KZSF_PRECOMPILE='0',KZSF_WINDOW_FOCUS='0')
        with (folder/'stdout.log').open('wb') as out,(folder/'stderr.log').open('wb') as err:
            game=subprocess.Popen(spec['command'],cwd=ui.ROOT,env=spec['env'],stdout=out,stderr=err,
                                  creationflags=subprocess.CREATE_NO_WINDOW)
            try:game.wait(timeout=20)
            except subprocess.TimeoutExpired:pass
            alive=game.poll() is None
            if alive:game.terminate()
            game.wait(timeout=10)
        log=(folder/'stdout.log').read_text(errors='replace')+(folder/'stderr.log').read_text(errors='replace')
        passed=alive and 'isolated free camera installed' in log and 'Fault:' not in log
        if args.body:passed=passed and 'native model-view test enabled' in log
        (folder/'status.json').write_text(json.dumps({'startup_passed':passed,'terminated_by_smoke':alive,
                                                     'pid':game.pid,'exit_code':game.returncode}))
        print('Startup smoke:', 'PASS' if passed else 'FAIL',folder)
        return 0 if passed else 1
    manifest=folder/'session.json'
    manifest.write_text(json.dumps({k:v for k,v in spec.items() if k!='env'}),encoding='utf-8')
    print('Load gameplay first, then press F8. Press F8 again to return to the player.')
    print(report['controls'])
    print('Click the game window to capture mouse look. Left Alt releases the pointer.')
    print('Experimental: world keeps running; no camera collision.')
    if args.body:
        print('Body test: while F8 is active, native first-person selection is suppressed and world-model visibility enabled.')
        print('Fly backward and look at the player. Check torso/head, floating arms/legs and F8 return to normal.')
    print('Camera resets after a view change or a frame gap over 250 ms. Logs:',folder,flush=True)
    return subprocess.Popen([sys.executable,str(Path(ui.__file__)),'--run-session',str(manifest)],
                            cwd=ui.ROOT,env=spec['env'],creationflags=subprocess.CREATE_NO_WINDOW).wait()

if __name__=='__main__':raise SystemExit(main())
