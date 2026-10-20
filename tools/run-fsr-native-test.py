"""Isolated Killzone FSR3 dispatch integration test, at native resolution without jitter."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'launcher'))
import kzsf_local as ui

def build_spec(values,folder,validation=False,quality=False,taa=False):
    spec=ui.launch_spec(values,folder)
    spec['command'][0]=str(ui.ROOT/('out/gow3-probe-fsr-quality.exe' if quality else 'out/gow3-probe-fsr-native.exe'))
    if taa: spec['command'][0]=str(ui.ROOT/'out/gow3-probe-taa.exe')
    env=spec['env']
    for key in ('KZSF_FSR_QUALITY','KZSF_ENGINE_TURN','KZSF_ALLOCATION_DUMP','GOW3_RENDER_RES','GOW3_PRESET_FILE',
                'GOW3_UPSCALE_BEFORE_CS','GOW3_UI_TRIGGER_VS','GOW3_DEBUG_MOTION','GOW3_OBJECT_MOTION_ALL'):
        env.pop(key,None)
    env.update(KZSF_FSR_NATIVE='1',KZSF_DIRECT_TSC='1',KZSF_HEALTH_TRACE='0',
        GOW3_UPSCALER='taa' if taa else 'fsr3',GOW3_UPSCALE_PRESET='0',GOW3_OUTPUT_RES='1920x1080',
        GOW3_JITTER='0',GOW3_REACTIVE='0',GOW3_OBJECT_MOTION='0',GOW3_RCAS='0',
        GOW3_READBACK_LINEAR='1',GOW3_PRECOMPILE='1',GOW3_BACKGROUND_WARMUP='0',
        GOW3_FREEZE_SECONDS='0',GOW3_UPSCALER_DIAGNOSTICS='1',
        GOW3_CONFIG=str(folder/'settings.ini'))
    if quality:
        env.update(KZSF_FSR_QUALITY='1',GOW3_UPSCALE_PRESET='1',
                   GOW3_SCENE_DEBUG=str(folder/'scene-debug.trigger'))
    if validation:
        env.update(GOW3_VK_VALIDATION='1',GOW3_VK_STOP_ON_ERROR='1',
                   VK_LAYER_PATH=str(ui.ROOT/'out/vulkan-validation/clang64/bin'))
    return spec

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--report-only',action='store_true')
    parser.add_argument('--validation',action='store_true')
    parser.add_argument('--taa',action='store_true',help='Native 1080p temporal AA without projection jitter')
    parser.add_argument('--quality',action='store_true',help='Experimental 720p scene raster -> 1080p FSR')
    args=parser.parse_args()
    if args.taa and args.quality: parser.error("TAA requires native resolution; omit --quality")
    values=ui.load_preferences()
    folder=ui.ROOT/'out'/(('taa-native-' if args.taa else 'fsr-quality-' if args.quality else 'fsr-native-')+time.strftime('%Y%m%d-%H%M%S'))
    spec=build_spec(values,folder,args.validation,args.quality,args.taa)
    keys=('KZSF_FSR_NATIVE','KZSF_DIRECT_TSC','GOW3_FPS_LIMIT','GOW3_DMEM_MB','GOW3_UPSCALER',
          'GOW3_UPSCALE_PRESET','GOW3_OUTPUT_RES','GOW3_JITTER','GOW3_REACTIVE','GOW3_OBJECT_MOTION',
          'GOW3_READBACK_LINEAR','GOW3_PRECOMPILE','GOW3_FREEZE_SECONDS',
          'KZSF_UNLIMITED_HEALTH','KZSF_INFINITE_AMMO')
    report={'executable':spec['command'][0],'settings':{k:spec['env'][k] for k in keys},
            'phase':('experimental native 1080p TAA, camera-only motion, jitter OFF; visual validation pending' if args.taa else 'experimental 720p scene proxies -> 1080p FSR; FPS/image validation pending' if args.quality else
                     'native-resolution camera-only integration; no FPS gain or complete temporal quality claimed')}
    report['settings']['KZSF_FSR_QUALITY']=spec['env'].get('KZSF_FSR_QUALITY','0')
    if args.report_only: print(json.dumps(report,indent=2));return 0
    running=subprocess.run(['tasklist','/FI','IMAGENAME eq gow3-probe*','/FO','CSV','/NH'],
        capture_output=True,text=True,creationflags=subprocess.CREATE_NO_WINDOW)
    if 'gow3-probe' in running.stdout.lower():print('Close the current game first.');return 1
    ok,reason=ui.validate_game(values['game_dir'])
    if not ok:print(reason);return 1
    if not Path(spec['command'][0]).is_file():print('Native FSR test executable missing.');return 1
    folder.mkdir(parents=True)
    shutil.copyfile(ui.CONFIG,folder/'settings.ini')
    ui.write_keys(folder/'settings.ini',dict(upscaler='taa' if args.taa else 'fsr3',preset='1' if args.quality else '0',output_res='1920x1080',
        jitter='0',reactive='0',object_motion='0',sharpen='0',rcas='0',debug_view='0'))
    (folder/'comparison.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    (folder/'run.txt').write_text(report['phase']+'\n',encoding='utf-8')
    manifest=folder/'session.json'
    # Pass environment directly to the monitor; do not persist inherited secrets.
    manifest.write_text(json.dumps({k:v for k,v in spec.items() if k!='env'},indent=2),encoding='utf-8')
    print('Killzone TAA test: native 1080p temporal filtering, jitter OFF.' if args.taa else
          'Killzone FSR3 Quality test: 720p scene -> 1080p, jitter OFF.' if args.quality else
          'Killzone FSR3 integration test: native1080p, jitter OFF, camera-only motion.')
    print('Precompile and assists retained. Normal launcher settings are unchanged.')
    print('Load the same gameplay area, check trees, pan the camera and look at the weapon for30 seconds.')
    print('Report image stability, ghosting/flicker, foliage and FPS. No new frame capture needed.')
    print('Logs:',folder,flush=True)
    monitor=subprocess.Popen([sys.executable,str(Path(ui.__file__)),'--run-session',str(manifest)],
        env=spec['env'],cwd=ui.ROOT,creationflags=subprocess.CREATE_NO_WINDOW)
    if args.quality:
        # One lightweight pass census after the first real gameplay FSR dispatch.
        # No debugger or separate capture step needed from the user.
        offset=0
        tail=b''
        requested=False
        while monitor.poll() is None:
            log_path=folder/'stdout.log'
            if not requested and log_path.exists():
                with log_path.open('rb') as stream:
                    stream.seek(offset)
                    chunk=stream.read()
                    offset=stream.tell()
                data=tail+chunk
                if b'Killzone FSR: recorded real FSR3 dispatch' in data:
                    (folder/'scene-debug.trigger').write_text('one scene frame\n',encoding='utf-8')
                    requested=True
                tail=data[-256:]
            time.sleep(.5)
    else:
        monitor.wait()
    log=(folder/'stdout.log').read_text(encoding='utf-8',errors='replace')
    diagnostics=re.findall(r'Upscaler diagnostics:[^\r\n]+',log)
    recorded=('Killzone TAA: recorded real TAA dispatch' if args.taa else 'Killzone FSR: recorded real FSR3 dispatch') in log
    summary={'provider': 'taa' if args.taa else 'fsr3', 'recorded_dispatch':recorded, 'recorded_fsr_dispatch':recorded if not args.taa else False,'last_counters':diagnostics[-1] if diagnostics else None}
    if args.quality:
        quality_lines=re.findall(r'Killzone FSR Quality:[^\r\n]+',log)
        draws=[int(n) for n in re.findall(r'reduced-size draws (\d+)/frame',log)]
        summary.update(last_quality_dispatch=quality_lines[-1] if quality_lines else None,
                       max_reduced_draws_per_frame=max(draws,default=0))
    (folder/'fsr-result.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')
    print('Game closed. Temporal dispatch recorded:',recorded)
    if diagnostics:print(diagnostics[-1])
    return 0

if __name__=='__main__': raise SystemExit(main())
