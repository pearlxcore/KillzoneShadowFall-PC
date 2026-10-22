"""Opt-in diagnostic run: dump the per-view post-process object.

Prints the runtime values behind the lens/ILR post-process stages (the 24-float
per-view constant block, the bloom/vignette rect+scale, and the ILR element lists)
so the chromatic-aberration / cosmetic-vignette parameters and the exposure
metronome can be identified from real data instead of guessed.

This is read-only instrumentation: the hook replays the displaced LEA exactly and
never writes to the guest. Values from the user's normal launcher preferences
(timing, memory profile, upscaler, controls, assists) are used unchanged.
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

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report-only',action='store_true')
    args=parser.parse_args()
    values=ui.load_preferences()
    folder=ui.ROOT/'out'/('postfx-dump-'+time.strftime('%Y%m%d-%H%M%S'))
    spec=ui.launch_spec(values,folder)
    spec['command'][0]=str(ui.ROOT/'out/kzsf-probe-postfx-dump.exe')
    spec['env']['KZSF_CONFIG']=str(folder/'settings.ini')
    spec['env']['KZSF_POSTFX_DUMP']='1'
    # Keep the frame rate low so the three dumped frames are well separated.
    spec['env']['KZSF_STALL_SECONDS']='0'; spec['env']['KZSF_FREEZE_SECONDS']='0'
    report={'diagnostic':'postfx-dump','executable':spec['command'][0],
            'config':spec['env']['KZSF_CONFIG']}
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
        print('The diagnostic build is missing: out/kzsf-probe-postfx-dump.exe');return 1
    folder.mkdir(parents=True)
    if ui.CONFIG.is_file():shutil.copyfile(ui.CONFIG,folder/'settings.ini')
    (folder/'comparison.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    manifest=folder/'session.json'
    manifest.write_text(json.dumps({k:v for k,v in spec.items() if k!='env'},indent=2),encoding='utf-8')
    print('Post-process diagnostic. The hook prints three "KILLZONE_POSTFX" blocks in stderr.log.')
    print('1. Load a save and reach normal gameplay (no cutscene).')
    print('2. Stand still for ~10 seconds looking at a scene with light and dark areas.')
    print('3. Close the game, then send the "KILLZONE_POSTFX" lines from stderr.log.')
    print('No game files, saves or preferences are modified; the run is read-only instrumentation.')
    print('Logs:',folder,flush=True)
    subprocess.Popen([sys.executable,str(Path(ui.__file__)),'--run-session',str(manifest)],
                     env=spec['env'],cwd=ui.ROOT,creationflags=subprocess.CREATE_NO_WINDOW).wait()
    return 0

if __name__=='__main__':raise SystemExit(main())
