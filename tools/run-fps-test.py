"""Run the opt-in clock optimization with the user's working launcher settings."""
import argparse
import json
import os
import shutil
from pathlib import Path
import subprocess
import sys
import time
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'launcher'))
import kzsf_local as ui

def main():
    parser=argparse.ArgumentParser(); parser.add_argument('--report-only',action='store_true')
    parser.add_argument('--precompile',action='store_true')
    parser.add_argument('--fsr-test',choices=('temporal-no-jitter','spatial','temporal'))
    parser.add_argument('--capture',action='store_true')
    parser.add_argument('--controller-test',action='store_true')
    parser.add_argument('--turn-speed-test',action='store_true')
    parser.add_argument('--player-turn-test',action='store_true')
    args=parser.parse_args()
    if args.capture and not args.fsr_test: parser.error('--capture requires --fsr-test')
    if args.fsr_test or args.controller_test or args.turn_speed_test or args.player_turn_test: args.precompile=True
    values=ui.load_preferences()
    prefix=('fsr-'+args.fsr_test+'-') if args.fsr_test else ('fps-precompile-' if args.precompile else 'fps-fastclock-')
    if args.controller_test: prefix='controller-aim-'
    if args.turn_speed_test: prefix='engine-turn-speed-'
    if args.player_turn_test: prefix='player-turn-speed-'
    folder=ui.ROOT/'out'/(prefix+time.strftime('%Y%m%d-%H%M%S'))
    spec=ui.launch_spec(values,folder)
    # Each comparison explicitly opts into its own native hook.
    spec['env'].pop('KZSF_ENGINE_TURN',None)
    spec['env'].pop('KZSF_TURN_LOG',None)
    spec['command'][0]=str(ui.ROOT/('out/kzsf-probe-precompile.exe' if args.precompile else 'out/kzsf-probe-fastclock.exe'))
    if args.fsr_test: spec['command'][0]=str(ui.ROOT/'out/kzsf-probe-fsr-test.exe')
    if args.capture: spec['command'][0]=str(ui.ROOT/'out/kzsf-probe-fsr-capture.exe')
    if args.controller_test:
        spec['command'][0]=str(ui.ROOT/'out/kzsf-probe-controller.exe')
        spec['env'].update(KZSF_PAD_LOOK_GAIN='150',KZSF_PAD_AIM_GAIN='125',
                           KZSF_CONFIG=str(folder/'settings.ini'))
    if args.turn_speed_test:
        spec['command'][0]=str(ui.ROOT/'out/kzsf-probe-turn-speed.exe')
        spec['env'].update(KZSF_ENGINE_TURN='1',KZSF_PAD_TURN_SPEED='150',KZSF_PAD_PITCH_SPEED='150',
                           KZSF_TURN_LOG='1',
                           KZSF_PAD_LOOK_GAIN='100',KZSF_PAD_AIM_GAIN='100',
                           KZSF_CONFIG=str(folder/'settings.ini'))
    if args.player_turn_test:
        spec['command'][0]=str(ui.ROOT/'out/kzsf-probe-player-turn.exe')
        spec['env'].update(KZSF_PLAYER_TURN='1',KZSF_PAD_TURN_SPEED='150',KZSF_PAD_PITCH_SPEED='150',
                           KZSF_PAD_LOOK_GAIN='100',KZSF_PAD_AIM_GAIN='100',
                           KZSF_CONFIG=str(folder/'settings.ini'))
    spec['env'].update(KZSF_DIRECT_TSC='1',KZSF_HEALTH_TRACE='0',KZSF_UPSCALER='none',KZSF_READBACK_LINEAR='1')
    spec['env'].pop('KZSF_ALLOCATION_DUMP',None)
    spec['env']['KZSF_PRECOMPILE']='1' if args.precompile else '0'
    if args.precompile:
        # Startup compilation intentionally runs before the first game frame.
        spec['env'].update(KZSF_BACKGROUND_WARMUP='0',KZSF_FREEZE_SECONDS='0')
    if args.fsr_test:
        spec['env'].update(KZSF_UPSCALER_DIAGNOSTICS='1',KZSF_UPSCALE_PRESET='0',
            KZSF_FSR1='1' if args.fsr_test=='spatial' else '0',
            KZSF_JITTER='1' if args.fsr_test=='temporal' else '0',
            KZSF_UPSCALER='none' if args.fsr_test=='spatial' else 'fsr3',
            KZSF_CONFIG=str(folder/'settings.ini'))
    if args.capture:
        spec['env'].update(KZSF_CAPTURE_TRIGGER=str(folder/'capture.trigger'),
                           KZSF_CAPTURE_DIR=str(folder/'frames'))
    report={'executable':spec['command'][0], 'settings':{k:spec['env'][k] for k in (
        'KZSF_DIRECT_TSC','KZSF_VIRTUAL_TSC','KZSF_FPS_LIMIT','KZSF_DMEM_MB','KZSF_UPSCALER',
        'KZSF_READBACK_LINEAR','KZSF_UNLIMITED_HEALTH','KZSF_INFINITE_AMMO','KZSF_PRECOMPILE',
        'KZSF_FREEZE_SECONDS','KZSF_PAD_LOOK_GAIN','KZSF_PAD_AIM_GAIN','KZSF_PAD_TURN_SPEED',
        'KZSF_PAD_PITCH_SPEED')},'vsync':values['vsync']}
    report['settings']['KZSF_ENGINE_TURN']=spec['env'].get('KZSF_ENGINE_TURN','0')
    report['settings']['KZSF_TURN_LOG']=spec['env'].get('KZSF_TURN_LOG','0')
    report['settings']['KZSF_PLAYER_TURN']=spec['env'].get('KZSF_PLAYER_TURN','0')
    if args.fsr_test:
        report['fsr_test']=args.fsr_test
        report['settings'].update({k:spec['env'][k] for k in
            ('KZSF_FSR1','KZSF_JITTER','KZSF_UPSCALE_PRESET','KZSF_UPSCALER_DIAGNOSTICS')})
    if args.capture:
        report['capture']={'folder':str(folder),'trigger':spec['env']['KZSF_CAPTURE_TRIGGER'],
                           'frames':spec['env']['KZSF_CAPTURE_DIR']}
    if args.report_only: print(json.dumps(report,indent=2)); return 0
    running=subprocess.run(['tasklist','/FI','IMAGENAME eq kzsf-probe*','/FO','CSV','/NH'],
        capture_output=True,text=True,creationflags=subprocess.CREATE_NO_WINDOW)
    if 'kzsf-probe' in running.stdout.lower():
        print('Close the current Killzone game first, then run this BAT again.'); return 1
    ok,reason=ui.validate_game(values['game_dir'])
    if not ok: print(reason); return 1
    if not Path(spec['command'][0]).is_file(): print('Fast-clock test build is missing.'); return 1
    folder.mkdir(parents=True)
    if args.fsr_test or args.controller_test or args.turn_speed_test or args.player_turn_test: shutil.copyfile(ui.CONFIG,folder/'settings.ini')
    if args.capture:
        (ui.ROOT/'out/fsr-capture-session.json').write_text(json.dumps(report['capture'],indent=2),encoding='utf-8')
    env=spec.pop('env'); manifest=folder/'session.json'
    manifest.write_text(json.dumps(spec,indent=2),encoding='utf-8')
    (folder/'comparison.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    (folder/'run.txt').write_text('Direct-clock performance comparison\n'+json.dumps(report,indent=2)+'\n',encoding='utf-8')
    if args.player_turn_test:
        print('PLAYER turn-factor test: heading/pitch150%, original stick response100%. Spectator hook OFF.')
        print('Compare full-stick turning while walking and sprinting. R3+L2 -> Game: compare100% with150%.')
        print('Execution and factor changes are logged automatically as KILLZONE_PLAYER_TURN.')
    elif args.turn_speed_test:
        print('Experimental ENGINE look rate 150% heading and 150% pitch. Stick-response gains stay original at 100%.')
        print('Rate logging is on: stderr gets "KILLZONE_RATE" lines with the engine rate and the stored rate.')
        print('Test: aim fully left/right and up/down while WALKING for ~10s, then while SPRINTING for ~10s.')
        print('R3+L2 -> Game -> Controller aiming: set each to 100% and 150% and compare horizontal and vertical feel.')
        print('Report whether full-stick turning is faster when sprinting, and the KILLZONE_RATE lines from both phases.')
    elif args.controller_test:
        print('Controller test: look response 150%, aiming response 125%. Precompile and native rendering retained.')
        print('R3+L2 -> Game -> Controller aiming: adjust live or restore both to 100%.')
    elif args.fsr_test:
        print('FSR investigation:',args.fsr_test,'- precompile, linear readback, controls and assists retained.')
        if args.fsr_test=='spatial':
            print('FSR 1 scales 1080p to larger windows. At 1080p it provides no rendering/FPS reduction.')
        else:
            print('Diagnostic only: logs check whether Killzone camera/scene/FSR dispatch hooks actually match.')
        print('Check the same tree for stripes and bark color, then move the camera for 30 seconds.')
    else:
        print('Direct-clock test. Native rendering, fixed foliage and your FPS/controls/assists retained.')
    if args.precompile:
        print('Precompiling the recorded cache before gameplay. Startup can take several minutes.')
        print('Wait for the compilation screen to finish. New uncached effects may still stutter.')
    print('Play the same checkpoint/path for 30-60 seconds; compare FPS and stutter.')
    print('Also check that gameplay speed and dialogue stay normal.')
    print('Logs:',folder,flush=True)
    if args.capture:
        print('In gameplay, run python tools/request-fsr-capture.py --sequence 3, switch back within 5 seconds, and slowly turn the camera for 15 seconds.',flush=True)
    os.environ.pop('KZSF_FREEZE_EXIT',None)
    os.environ.update(env); ui.run_session(manifest)
    print('Game closed. Report the FPS and whether timing stayed normal.')
    return 0

if __name__=='__main__': raise SystemExit(main())
