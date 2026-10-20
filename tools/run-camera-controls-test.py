"""Isolated gameplay comparison of native FOV, camera shake and DOF controls."""
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
    parser.add_argument('mode',choices=('original','fov','shake','dof','all'))
    parser.add_argument('--report-only',action='store_true')
    args=parser.parse_args()
    values=ui.load_preferences()
    values.update(fov_percent='120' if args.mode in ('fov','all') else '100',
                  camera_shake=args.mode not in ('shake','all'),
                  depth_of_field=args.mode not in ('dof','all'))
    folder=ui.ROOT/'out'/('camera-'+args.mode+'-'+time.strftime('%Y%m%d-%H%M%S'))
    spec=ui.launch_spec(values,folder)
    spec['command'][0]=str(ui.ROOT/'out/gow3-probe-camera-controls.exe')
    spec['env']['GOW3_CONFIG']=str(folder/'settings.ini')
    report={key:values[key] for key in ('fov_percent','camera_shake','depth_of_field','motion_blur')}
    if args.report_only:
        print(json.dumps(report,indent=2));return 0
    running=subprocess.run(['tasklist','/FI','IMAGENAME eq gow3-probe*','/FO','CSV','/NH'],capture_output=True,text=True,
                           creationflags=subprocess.CREATE_NO_WINDOW)
    if 'gow3-probe' in running.stdout.lower():
        print('Close the current game before starting this test.');return 1
    ok,reason=ui.validate_game(values['game_dir'])
    if not ok:
        print(reason);return 1
    folder.mkdir(parents=True)
    if ui.CONFIG.is_file():shutil.copyfile(ui.CONFIG,folder/'settings.ini')
    ui.write_keys(folder/'settings.ini',{k:str(int(v)) if isinstance(v,bool) else v for k,v in report.items()})
    (folder/'comparison.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    manifest=folder/'session.json'
    manifest.write_text(json.dumps({k:v for k,v in spec.items() if k!='env'}),encoding='utf-8')
    print('FOV:',values['fov_percent']+'%; shake:',values['camera_shake'],'DOF:',values['depth_of_field'])
    print('Compare the same gameplay spot, ADS, and a shake-producing explosion or impact.')
    print('FOV and shake can be adjusted live in overlay Image. DOF requires restarting.')
    print('Saved launcher preferences stay unchanged. Logs:',folder,flush=True)
    subprocess.Popen([sys.executable,str(Path(ui.__file__)),'--run-session',str(manifest)],
                     env=spec['env'],cwd=ui.ROOT,creationflags=subprocess.CREATE_NO_WINDOW).wait()
    return 0


if __name__=='__main__':raise SystemExit(main())
