"""Observe native camera resources during normal gameplay; F6 takes a snapshot."""
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
    parser.add_argument('--call-bridge',action='store_true',help='Use isolated direct-call bridge diagnostic.')
    parser.add_argument('--third-person',action='store_true',help='Use follow-camera prototype with native sphere collision.')
    parser.add_argument('--character-inventory',action='store_true',help='Read player body variants using an isolated follow-camera build.')
    parser.add_argument('--character-resources',action='store_true',help='Automatically inspect loaded model parts and skeleton links during gameplay.')
    parser.add_argument('--character-swap',action='store_true',help='Isolated F7 default/loaded HGH Commando appearance prototype.')
    parser.add_argument('--character-selector',action='store_true',help='Cycle compatible loaded character variants; audit other rigs.')
    parser.add_argument('--character-retarget',action='store_true',help='Inspect native temporary bone maps; do not apply retargeted poses.')
    parser.add_argument('--character-engineer',action='store_true',help='Experimental Engineer/default body with native render-pose retargeting.')
    parser.add_argument('--character-engineer-face',action='store_true',help='Confirmed revision13 head build; weapon stretching remains unresolved.')
    parser.add_argument('--character-engineer-weapon',action='store_true',help='Revision16: retain pose recovery when restoring default after an Engineer weapon switch.')
    parser.add_argument('--character-face-watch',action='store_true',help='Play revision16 with bounded read-only facial pose snapshots.')
    parser.add_argument('--character-single-body',action='store_true',help='Revision17 isolated native visibility test for overlapping full-body copies.')
    parser.add_argument('--character-commando',action='store_true',help='Revision18 HGH Commando/default with strict binding checks and single-body visibility.')
    parser.add_argument('--character-model-overlay',action='store_true',help='Revision20 model dropdown and F7+arrows for seven known Chapter2 soldier bodies.')
    parser.add_argument('--character-all-chapters',action='store_true',help='Experimental native background bundle loading for cross-chapter model selection.')
    parser.add_argument('--character-fall-control',action='store_true',help='A/B drop-crash investigation: retain player facial animation library.')
    args=parser.parse_args()
    if args.character_fall_control:args.character_all_chapters=True
    if args.character_all_chapters:args.character_model_overlay=True
    if args.character_model_overlay:args.character_single_body=True
    if args.character_commando:args.character_single_body=True
    if args.character_single_body:args.character_face_watch=True
    if args.character_face_watch:args.character_engineer_weapon=True
    if args.character_engineer_weapon:args.character_engineer_face=True
    if args.character_engineer_face:args.character_engineer=True
    if args.character_engineer:args.character_selector=True
    if args.character_retarget:args.character_selector=True
    if args.character_selector:args.character_swap=True
    if args.character_swap:args.character_inventory=True
    if args.character_resources:args.character_inventory=True
    if args.character_inventory:args.third_person=True
    if args.third_person:args.call_bridge=True
    values=ui.load_preferences()
    prefix='character-resources-' if args.character_resources else 'third-person-' if args.third_person else 'camera-observer-'
    folder=ui.ROOT/'out'/(prefix+time.strftime('%Y%m%d-%H%M%S'))
    spec=ui.launch_spec(values,folder)
    spec['command'][0]=str(ui.ROOT/'out/kzsf-probe-camera-observer.exe')
    spec['env'].update(KZSF_CAMERA_OBSERVER='1',KZSF_CONFIG=str(folder/'settings.ini'))
    if args.call_bridge:
        spec['command'][0]=str(ui.ROOT/'out/kzsf-probe-camera-bridge.exe')
        spec['env']['KZSF_CAMERA_CALL_BRIDGE']='1'
    if args.third_person:
        spec['command'][0]=str(ui.ROOT/'out/kzsf-probe-third-person-animation-owned.exe')
        spec['env']['KZSF_THIRD_PERSON_TEST']='1'
    if args.character_inventory:
        spec['command'][0]=str(ui.ROOT/'out/kzsf-probe-character-inventory.exe')
    if args.character_resources:
        spec['command'][0]=str(ui.ROOT/'out/kzsf-probe-character-resources-nested.exe')
        spec['env']['KZSF_CHARACTER_RESOURCE_TEST']='1'
    if args.character_swap:
        spec['command'][0]=str(ui.ROOT/'out/kzsf-probe-character-swap.exe')
        spec['env']['KZSF_CHARACTER_SWAP_TEST']='1'
    if args.character_selector:
        spec['command'][0]=str(ui.ROOT/'out/kzsf-probe-character-selector.exe')
        spec['env']['KZSF_CHARACTER_SELECTOR_TEST']='1'
    if args.character_retarget:
        spec['command'][0]=str(ui.ROOT/'out/kzsf-probe-character-retarget.exe')
        spec['env']['KZSF_CHARACTER_RETARGET_AUDIT']='1'
    if args.character_engineer:
        # Revision11/12 introduced gameplay crashes; retain their binaries/logs
        # for investigation and use the previously exercised body-only build.
        spec['command'][0]=str(ui.ROOT/'out/kzsf-probe-character-engineer-v10.exe')
        spec['env']['KZSF_CHARACTER_ENGINEER_TEST']='1'
        spec['env']['KZSF_CHARACTER_ENGINEER_DRAW_TEST']='1'
        spec['env']['KZSF_FOLLOW_FREE_CAMERA']='1'
        spec['env'].pop('KZSF_CHARACTER_BODY_ROUTE_TEST',None)
        if args.character_engineer_face:
            spec['command'][0]=str(ui.ROOT/'out/kzsf-probe-character-engineer-v13-face.exe')
        if args.character_engineer_weapon:
            spec['command'][0]=str(ui.ROOT/'out/kzsf-probe-character-engineer-v16-restore-pose.exe')
            spec['env']['KZSF_CHARACTER_BODY_ROUTE_TEST']='1'
        spec['env'].pop('KZSF_CHARACTER_SINGLE_BODY_TEST',None)
        spec['env'].pop('KZSF_CHARACTER_COMMANDO_TEST',None)
        spec['env'].pop('KZSF_CHARACTER_OVERLAY_TEST',None)
        spec['env'].pop('KZSF_CHARACTER_ALL_MODELS_TEST',None)
        spec['env'].pop('KZSF_CHARACTER_BUNDLE_TEST',None)
        if args.character_single_body:
            spec['command'][0]=str(ui.ROOT/'out/kzsf-probe-character-engineer-v17-single-body.exe')
            spec['env']['KZSF_CHARACTER_SINGLE_BODY_TEST']='1'
        if args.character_commando:
            spec['command'][0]=str(ui.ROOT/'out/kzsf-probe-character-commando-v18.exe')
            spec['env']['KZSF_CHARACTER_COMMANDO_TEST']='1'
        if args.character_model_overlay:
            spec['command'][0]=str(ui.ROOT/'out/kzsf-probe-character-selector-v21.exe')
            spec['env']['KZSF_CHARACTER_OVERLAY_TEST']='1'
            spec['env']['KZSF_CHARACTER_ALL_MODELS_TEST']='1'
            if args.character_all_chapters:
                spec['command'][0]=str(ui.ROOT/'out/kzsf-probe-character-selector-v44-complete-draw-batch.exe')
                spec['env']['KZSF_CHARACTER_BUNDLE_TEST']='1'
                spec['env']['KZSF_CHARACTER_PRESERVE_SOURCE_BODY']='0'
                spec['env']['KZSF_CHARACTER_FACE_PLAYER_ANIMATION_CONTROL']='1' if args.character_fall_control else '0'
    report={'diagnostic':'native-camera-observer','observer_schema':4,'executable':spec['command'][0],
            'execution_context':'direct-call' if args.call_bridge else 'VEH',
            'capture_goal':'One standing gameplay snapshot: campaign ownership and engine allocator context.',
            'controls':'Normal player controls. F6 takes a camera snapshot (64 maximum; press once per phase).',
            'free_camera':False,'save_policy':'Copied campaign saves; diagnostic changes stay in the session folder.'}
    if args.call_bridge:report['capture_goal']='One standing gameplay snapshot through the direct-call diagnostic bridge.'
    if args.third_person:
        report.update(diagnostic='third-person-follow-prototype',observer_schema=5,
            prototype_revision='animation-owned-3',
            controls='F8 toggles follow view. Normal keyboard/mouse/controller movement and look. Aim uses first person.',
            capture_goal='Visible follow view, walking/turning, aim fallback and F8 return.',
            limitations='Knife follow-up confirmed. Door/scanner/drop remain first person: unresolved and deferred at user request; ambush unresolved. Wall/vent and ladders pending; grenade flash deferred; hip-fire and shaking TODOs.')
    if args.character_inventory:
        report.update(diagnostic='player-character-inventory',observer_schema=6,
            prototype_revision='character-inventory-1',
            capture_goal='One standing gameplay F6 snapshot of the player-owned body variants.',
            controls='Load adult-player gameplay, stand normally with aim released, hold F6 briefly and release. F8 remains optional.',
            model_swapping=False,
            limitations='Read-only inventory; no playable appearance selection. Standing camera ownership must be established.')
    if args.character_resources:
        report.update(diagnostic='loaded-character-resources',observer_schema=7,
            prototype_revision='character-resources-4-nested-soldier',
            capture_goal='Automatic loaded-part inventory and mesh/skeleton links after stable local-player ownership.',
            controls='Load ordinary adult-player gameplay and stand normally with aim released for five seconds, then close the game. F8 and F6 are optional.',
            limitations='Uses the native loaded-object query on the verified engine callback and frees its temporary array. No asset loading or playable appearance selection. Hash equality alone is not rig compatibility.')
    if args.character_swap:
        report.update(diagnostic='commando-appearance-prototype',prototype_revision='commando-3-mat44-numeric',
            controls='Load gameplay, release aim and stand normally. F8 toggles third person. F7 toggles HGH Commando/default appearance.',
            capture_goal='Verify native body part swap and restoration, walking, weapon and animations.',
            model_swapping=True,
            limitations='Experimental: only already-loaded HGH Commando with the same skeleton, exact bone names/order and finite inverse-bind matrices agreeing within 1e-6 absolute plus 1e-6 relative tolerance. Refuses larger differences. Other character models unsupported; gameplay swap unverified.')
    if args.character_selector:
        report.update(diagnostic='loaded-character-selector',prototype_revision='selector-1-joint-prefix-audit',
            controls='In normal gameplay with aim released: F8 third person, F7 next compatible loaded body; default follows the last compatible body.',
            capture_goal='Cycle compatible loaded variants and identify joint-prefix mismatches in other rigs.',
            limitations='Only already-loaded bodies passing the original strict rig/binding gate can change appearance. Different rigs are audited but refused. Missing bundles are not loaded. Expanded selector gameplay remains unverified.')
    if args.character_retarget:
        report.update(diagnostic='native-character-retarget-audit',prototype_revision='retarget-1-native-map',
            controls='Load normal adult-player gameplay, release aim, press F7 once, then close the game. F8 remains optional.',
            capture_goal='Inspect native source-to-destination joint maps for the first skinned leaf of each incompatible loaded body.',
            limitations='Calls native map construction using temporary allocations, releases both arrays and applies no retargeted poses. Skeleton lookup caches may initialize. Existing compatible-body cycling remains; other rigs and unloaded models remain unsupported.')
    if args.character_engineer:
        report.update(diagnostic='engineer-render-retarget-prototype',prototype_revision='engineer-10-body-only-rollback',
            free_camera=True,
            controls='F8 third person. F7 Engineer/default body. F9 freecam toggle; WASD move, Q/E down/up, mouse/arrows look, Shift faster, Ctrl slower, F10 recenter.',
            capture_goal='Verify Engineer appearance, walking/turning, weapon use and default restoration with native render-pose retargeting.',
            limitations='Rolled back to revision10 after revision11/12 gameplay crashes. Engineer head is missing immediately after F7; traversal may reveal an overlapping default body. Switching to a secondary weapon can cause stretching. Native facial companion changes remain disabled in this launcher. These defects are unresolved; other rigs and missing bundles remain unsupported.')
    if args.character_engineer_face:
        report.update(prototype_revision='engineer-13-confirmed-head-rollback',
            capture_goal='Preserve confirmed immediate Engineer head; inspect weapon-transition stretching read-only.',
            limitations='Revision13 immediate head visibility is confirmed. Revision14 crashed at the native103 transition and is withdrawn. Weapon switching still causes stretching in revision13; this launcher is a rollback, not a weapon fix. Broader transition stability remains unverified.')
    if args.character_engineer_weapon:
        report.update(prototype_revision='engineer-16-default-restore-recovery',
            capture_goal='F8, F7 Engineer, switch weapons, F7 default. Check default deformation, repeat Engineer/default, then switch weapons on default.',
            limitations='User confirms revision16 fixes weapon switching and default restoration. Intermittent distorted face and pink skin remain unresolved. Revision13 Head launcher and revision15 executable are retained.')
    if args.character_face_watch:
        report.update(capture_goal='Play normally as Engineer; no reproduction sequence required. If facial corruption appears, leave the game running for live inspection.',
                      face_capture='Read-only facial pose snapshots every five seconds, plus numeric anomalies, for up to30 minutes. No new native patches. Pink skin/material correctness requires visual correlation.')
    if args.character_single_body:
        report.update(prototype_revision='engineer-17-native-single-body',
            capture_goal='F8/F7, inspect face, switch weapons and restore default. Repeat Engineer/default a few times; inspect for pink skin, doubled face and regressions.',
            limitations='Gameplay unverified. Live revision16 had seven enabled default bodies and no facial animator attached while corruption was reported; recorded facial poses were finite. Revision17 disables extra exact full-body slots through native begin/visibility/end transactions. Body overlap is a hypothesis for the face corruption, not a confirmed cause. Revision16 remains the normal Weapon launcher.')
    if args.character_commando:
        report.update(diagnostic='commando-appearance-prototype',prototype_revision='commando-18-single-body',
            controls='F8 third person. F7 toggles loaded HGH Commando/default. F9 freecam; F10 recenter.',
            capture_goal='Check Commando head/face, walking, weapon switching, F7 restoration and repeated swaps.',
            limitations='New Commando integration is gameplay unverified. Requires the existing strict same-skeleton and inverse-bind gate; unsupported models stay refused. Reuses native face setup, restoration tracking and revision17 single-body visibility. Engineer Single Body launcher remains revision17.')
    if args.character_model_overlay:
        report.update(diagnostic='overlay-character-selector',prototype_revision='selector-21-face-variant',
            controls='Insert > Game > Player model > Model. F8 third person. Hold F7 + Right Arrow for next; F7 + Left Arrow for previous. F7 alone toggles chosen/default on release. F9 freecam.',
            capture_goal='Test the seven known Chapter2 soldier body entries, keyboard cycling, head/face, weapons and default restoration.',
            limitations='Four additional rigs remain experimental. Revision21 resolves shared-body face ambiguity by attaching the selected variant0 face for Special Forces and Infantry, and checks native body visibility every normal callback for these models. New head attachment and jump-flash mitigation require gameplay verification. Other face variants are not separate dropdown entries; unloaded bundles are not loaded.')
    if args.character_all_chapters:
        report.update(prototype_revision='selector-44-complete-draw-batch',
            capture_goal='Revision44 restores the earlier complete native draw-record processing rule to generic rigs: original bodies, repeated selected meshes and attachments all processed in native order with corresponding poses. Removes revision43 added-body experiment. Test HGH Security Trooper selection, jump/drop, weapon switch and default restoration. Execution-partner camera candidate and fatal capture retained. Gameplay crash fix requires confirmation; overlapping bodies may need native visibility follow-up.',
            limitations='Revision39 removes Massar at the user request. Revision38 anchors finger translations to the target parent while retaining converted rotations. Live Massar replay reduces maximum finger stretch from5.9237x to1.0000x; appearance still requires visual retry. Sinclair and Massar cliff drops crash at308490 with the same null-source call chain onv37; this is unresolved. A/B fall-control mode disables the selected-character facial animation library override only, so facial distortion may return during that diagnostic. Native Echo leg and heat-color issues remain unresolved; roster and idle-scan/freecam fixes retained.')
        if args.character_fall_control:
            report.update(prototype_revision='selector-44-fall-control',capture_goal='Compare the same Sinclair/default cliff drop with player facial animation libraries retained and all native draw records processed. Gameplay confirmation required.')
    if args.report_only:
        print(json.dumps(report,indent=2));return 0
    running=subprocess.run(['tasklist','/FI','IMAGENAME eq kzsf-probe*','/FO','CSV','/NH'],
                           capture_output=True,text=True,creationflags=subprocess.CREATE_NO_WINDOW)
    if 'kzsf-probe' in running.stdout.lower():
        print('Close the running game before starting this camera investigation.');return 1
    ok,reason=ui.validate_game(values['game_dir'])
    if not ok:print(reason);return 1
    if not Path(spec['command'][0]).is_file():
        print('Missing isolated camera-observer executable.');return 1
    folder.mkdir(parents=True)
    if ui.CONFIG.is_file():shutil.copyfile(ui.CONFIG,folder/'settings.ini')
    profile=folder/'user';profile.mkdir()
    if (ui.ROOT/'user/savedata').is_dir():
        shutil.copytree(ui.ROOT/'user/savedata',profile/'savedata')
    spec['env']['KZSF_USER_DIR']=str(profile)
    (folder/'comparison.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    if args.startup_smoke:
        spec['env'].update(KZSF_PRECOMPILE='0',KZSF_WINDOW_FOCUS='0')
        with (folder/'stdout.log').open('wb') as out,(folder/'stderr.log').open('wb') as err:
            game=subprocess.Popen(spec['command'],cwd=ui.ROOT,env=spec['env'],stdout=out,stderr=err,
                                  creationflags=subprocess.CREATE_NO_WINDOW)
            try:game.wait(timeout=20)
            except subprocess.TimeoutExpired:pass
            alive=game.poll() is None
            if alive:game.terminate()
            game.wait(timeout=10)
        log=(folder/'stdout.log').read_text(errors='replace')+(folder/'stderr.log').read_text(errors='replace')
        view_seen='KILLZONE_CAMERA_OBSERVER snapshot=' in log
        passed=alive and 'camera observer installed' in log and 'Fault:' not in log
        if args.call_bridge:passed=passed and 'camera direct-call bridge installed' in log
        if args.third_person:passed=passed and 'third-person follow prototype installed' in log
        (folder/'status.json').write_text(json.dumps({'startup_passed':passed,'terminated_by_smoke':alive,
                                                     'view_hook_observed':view_seen,
                                                     'pid':game.pid,'exit_code':game.returncode}))
        print('Startup smoke:', 'PASS' if passed else 'FAIL',folder)
        print('Gameplay camera hook:', 'observed' if view_seen else 'not reached; gameplay capture still required')
        return 0 if passed else 1
    manifest=folder/'session.json'
    manifest.write_text(json.dumps({k:v for k,v in spec.items() if k!='env'},indent=2),encoding='utf-8')
    print(report['controls'])
    if args.character_inventory:
        if args.character_engineer:
            print('F9 toggles freecam; WASD move, Q/E down/up, mouse/arrows look, Shift faster, Ctrl slower, F10 recenter.')
            if args.character_model_overlay:
                print('Insert > Game > Player model > Model. F7 + arrows cycles models; F7 alone toggles selected/default.')
                if args.character_all_chapters:print('Experimental: missing bundles load in the background for each chapter; level exit restores default and releases imported bundles.')
            elif args.character_commando:
                print('F7 toggles the loaded HGH Commando/default body. Exact skeleton/binding compatibility is required.')
            else:
                print('F7 toggles the loaded Engineer/default body. Retargeting changes only the rendered pose.')
            print('Check the body while walking and turning, fire the weapon, then press F7 to restore default and close.')
            if not args.character_commando:
                print('Scarf and antenna use a fallback pose; visual deformation still needs checking.')
        elif args.character_retarget:
            print('F7 inspects the native bone maps for the other loaded character rigs, then cycles the existing compatible body.')
            print('Only one F7 press is needed. Close the game afterward; this does not yet enable the other bodies.')
        elif args.character_selector:
            print('F7 scans every loaded SoldierResource body variant and cycles compatible unique parts, then default.')
            print('Other rigs are logged with the first joint-name/parent mismatch; this audit does not enable them.')
            print('Press F7 three times while standing with aim released, then close the game.')
        elif args.character_swap:
            print('F7 changes appearance through the native model-part action, then F7 again restores the default body.')
            print('If HGH Commando is not loaded or binding data differs, the request is refused and logged.')
            print('Check walking, turning, shooting, then restore the default with F7 and close the game.')
        elif args.character_resources:
            print('Loaded character parts and skeleton links are captured automatically after stable gameplay ownership.')
        else:
            print('This records native body-variant resources; it does not change your appearance.')
            print('One F6 snapshot in normal gameplay is enough. Close the game afterward.')
        print(report['save_policy']);print('Logs:',folder,flush=True)
        runner=subprocess.Popen([sys.executable,str(Path(ui.__file__)),'--run-session',str(manifest)],
                                cwd=ui.ROOT,env=spec['env'],creationflags=subprocess.CREATE_NO_WINDOW)
        watcher=None
        if args.character_face_watch:
            watcher=subprocess.Popen([sys.executable,str(ui.ROOT/'tools/watch-character-face.py'),'--session',str(folder)],
                                     cwd=ui.ROOT,creationflags=subprocess.CREATE_NO_WINDOW)
            print('Facial snapshots enabled. Play normally; if corruption appears, leave the game open and report it.',flush=True)
        result=runner.wait()
        if watcher:
            try:watcher.wait(timeout=3)
            except subprocess.TimeoutExpired:watcher.terminate();watcher.wait(timeout=3)
        if args.character_resources:
            subprocess.run([sys.executable,str(ui.ROOT/'tools/analyze-character-resources.py'),
                            '--session',str(folder)],cwd=ui.ROOT,check=False)
        if args.character_retarget:
            subprocess.run([sys.executable,str(ui.ROOT/'tools/analyze-character-retarget.py'),
                            '--session',str(folder)],cwd=ui.ROOT,check=False)
        return result
    if args.third_person:
        print('Load gameplay, press F8, walk and turn, try aiming, then press F8 again to return.')
        print('Prototype: native sphere collision pulls the camera inward at walls. Aim remains native first person.')
        print('Follow view permits traversal, grenades and zipline when camera ownership allows.')
        print('Hip-fire camera converges toward native aim-line geometry. Bullet spread and penetration remain native.')
        print('Previous follow behavior restored. Grenade release flash remains unresolved.')
        print('Owned ladder camera enabled for testing; check mounting, climbing and dismounting.')
        print('Executions, post-execution fade and zipline confirmed third person.')
        print('Doors, scanners and drop executions still use first person; fixes deferred. Knife follow-up confirmed third person.')
        print('Wall collision retains follow view. Player body hides at very short camera distances to prevent interior mesh clipping.')
        print('F6 optionally records a snapshot. No diagnostic sequence is required.')
        print(report['save_policy']);print('Logs:',folder,flush=True)
        return subprocess.Popen([sys.executable,str(Path(ui.__file__)),'--run-session',str(manifest)],
                                cwd=ui.ROOT,env=spec['env'],creationflags=subprocess.CREATE_NO_WINDOW).wait()
    print('Load gameplay and stand normally with aim released and the overlay closed.')
    print('Hold F6 for half a second, then release it. One gameplay snapshot is enough for this stage.')
    print('This checks ownership and engine thread context. No repeated ADS or scripted interaction test is needed.')
    print('Close the game after the capture. Camera observer blocks are in stderr.log.')
    print('This observes normal gameplay; third-person mode is not enabled.')
    if args.call_bridge:print('Direct-call bridge test: camera behavior unchanged; physics calls remain disabled.')
    print(report['save_policy'])
    print('Logs:',folder,flush=True)
    return subprocess.Popen([sys.executable,str(Path(ui.__file__)),'--run-session',str(manifest)],
                            cwd=ui.ROOT,env=spec['env'],creationflags=subprocess.CREATE_NO_WINDOW).wait()


if __name__=='__main__':raise SystemExit(main())
