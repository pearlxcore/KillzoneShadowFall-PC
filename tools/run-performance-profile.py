"""Profile the current native renderer without a debugger or persistent setting changes."""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'launcher'))
import kzsf_local as ui


def summarize(log):
    windows=[]
    cpu_windows=[]
    cpu_current=None
    wait_lines=[]
    current=None
    for line in log.splitlines():
        cpu_header=re.match(r'CPU render profile: frames=(\d+) elapsed_ms=([\d.]+)',line)
        if cpu_header:
            cpu_current=dict(frames=int(cpu_header[1]),elapsed_ms=float(cpu_header[2]),stages=[])
            cpu_windows.append(cpu_current)
        cpu_row=re.match(r'\s+CPU stage: ([\d.]+) ms/frame ([\d.]+) calls/frame (.+)',line)
        if cpu_row and cpu_current:
            cpu_current['stages'].append(dict(ms_per_frame=float(cpu_row[1]),calls_per_frame=float(cpu_row[2]),label=cpu_row[3]))
        if line.startswith('Wait profile:'):wait_lines.append(line)
        header=re.match(r'GPU profile: ([\d.]+) ms/frame over (\d+) frames, (\d+) labels',line)
        if header:
            current=dict(timestamp_span_ms=float(header[1]),frames=int(header[2]),labels=int(header[3]),passes=[])
            windows.append(current)
        elif current:
            row=re.match(r'\s+([\d.]+) ms/frame\s+([\d.]+)/frame\s+(.+)',line)
            if row:
                current['passes'].append(dict(ms_per_frame=float(row[1]),segments_per_frame=float(row[2]),label=row[3]))
    stats=re.findall(r'Frame stats:[^\r\n]+',log)
    texture_sets=[dict(hits=int(h),misses=int(m)) for h,m in
                  re.findall(r'texture sets: (\d+) hits, (\d+) misses',log)]
    return dict(profile_windows=windows,cpu_windows=cpu_windows,wait_profiles=wait_lines,final_frame_stats=stats[-10:],
        texture_set_windows=texture_sets,
        note='Instrumented timestamp spans include barriers, copies and possible GPU idle gaps; compare pass costs, not raw benchmark FPS.')


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--report-only',action='store_true')
    parser.add_argument('--texture-memo',action='store_true',help='Isolated direct texture-set cache experiment')
    args=parser.parse_args()
    if args.texture_memo:
        print('Texture-cache experiment retired after gameplay feedback: use the normal launcher.')
        return 1
    values=ui.load_preferences()
    folder=ui.ROOT/'out'/(('texture-memo-' if args.texture_memo else 'performance-profile-')+time.strftime('%Y%m%d-%H%M%S'))
    spec=ui.launch_spec(values,folder)
    spec['command'][0]=str(ui.ROOT/'out/gow3-probe-profile.exe')
    spec['env'].update(GOW3_GPU_PROFILE='1',KZSF_CPU_PROFILE='1',GOW3_PERF_DIAG='1',GOW3_BUFFER_STATS='1',GOW3_UPSCALER='none',
                       GOW3_CONFIG=str(folder/'settings.ini'))
    spec['env'].pop('KZSF_FSR_NATIVE',None)
    if args.texture_memo:spec['env']['KZSF_DIRECT_TEXTURE_MEMO']='1'
    report=dict(executable=spec['command'][0],renderer='native1080p, no FSR',
        gpu_profile=True,cpu_stage_profile=True,wait_profile=True,direct_texture_memo=args.texture_memo,
        precompile=spec['env']['GOW3_PRECOMPILE'],
        player_turn=spec['env'].get('KZSF_PLAYER_TURN','0'),
        assists={k:spec['env'][k] for k in ('KZSF_UNLIMITED_HEALTH','KZSF_INFINITE_AMMO')})
    if args.report_only:
        print(json.dumps(report,indent=2));return 0
    running=subprocess.run(['tasklist','/FI','IMAGENAME eq gow3-probe*','/FO','CSV','/NH'],
        capture_output=True,text=True,creationflags=subprocess.CREATE_NO_WINDOW)
    if 'gow3-probe' in running.stdout.lower():print('Close the current game first.');return 1
    ok,reason=ui.validate_game(values['game_dir'])
    if not ok:print(reason);return 1
    if not Path(spec['command'][0]).is_file():print('Current launcher build missing.');return 1
    folder.mkdir(parents=True)
    shutil.copyfile(ui.CONFIG,folder/'settings.ini')
    ui.write_keys(folder/'settings.ini',dict(upscaler='none'))
    (folder/'profile-settings.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    manifest=folder/'session.json'
    manifest.write_text(json.dumps({k:v for k,v in spec.items() if k!='env'},indent=2),encoding='utf-8')
    print('Native rendering profile: shader precompile and your controls/cheats retained.')
    if args.texture_memo:print('Direct texture-set cache experiment ON. Check lighting/textures as well as FPS.')
    print('Face a nearby wall for20 seconds, then face the slow open area for20 seconds. Stay in gameplay.')
    print('Close the game normally and reply done. Timing data is saved automatically.')
    print('Profiling adds overhead; use this run for diagnosis, not an FPS comparison.')
    print('Logs:',folder,flush=True)
    monitor=subprocess.Popen([sys.executable,str(Path(ui.__file__)),'--run-session',str(manifest)],
        cwd=ui.ROOT,env=spec['env'],creationflags=subprocess.CREATE_NO_WINDOW)
    monitor.wait()
    log=(folder/'stdout.log').read_text(encoding='utf-8',errors='replace')
    result=summarize(log)
    (folder/'profile-summary.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print('Saved',len(result['profile_windows']),'GPU timing windows. Reply done.')
    return 0


if __name__=='__main__':raise SystemExit(main())
