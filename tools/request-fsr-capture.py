"""Request one frame from the dedicated FSR capture session; never attach a debugger."""
import json
import argparse
from pathlib import Path
import subprocess
import time

ROOT=Path(__file__).resolve().parents[1]

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--sequence',type=int,choices=(1,3),default=1)
    args=parser.parse_args()
    try:
        spec=json.loads((ROOT/'out/fsr-capture-session.json').read_text(encoding='utf-8'))
        folder=Path(spec['folder']).resolve()
        if folder.parent != (ROOT/'out').resolve() or not folder.name.startswith('fsr-'):
            raise ValueError('Unexpected capture session location.')
        status=json.loads((folder/'status.json').read_text(encoding='utf-8'))
        if 'exit_code' in status: raise ValueError('The capture game has already closed.')
        tasks=subprocess.run(['tasklist','/FI',f'PID eq {int(status["pid"])}','/FO','CSV','/NH'],
            capture_output=True,text=True,creationflags=subprocess.CREATE_NO_WINDOW)
        if not any(name in tasks.stdout.lower() for name in ('gow3-probe-fsr-test.exe','gow3-probe-fsr-capture.exe')):
            raise ValueError('The FSR capture game is not running.')
    except (OSError,ValueError,KeyError) as exc:
        print('Start python tools/run-fps-test.py --fsr-test temporal-no-jitter --capture first.',exc); return 1
    frames=folder/'frames'
    existing=set(frames.glob('frame_*.txt'))
    print('Switch back to unpaused gameplay now. Capture requested in 5 seconds.',flush=True)
    if args.sequence==3: print('Slowly turn the camera for about 15 seconds. Three frames will be saved.',flush=True)
    time.sleep(5)
    for index in range(args.sequence):
        # Use the native completion log rather than file creation: the frame text can
        # still be writing when its filename first appears.
        log_path=folder/'stdout.log'
        log_offset=log_path.stat().st_size
        (folder/'capture.trigger').write_text('capture one frame\n',encoding='utf-8')
        for _ in range(40):
            captured=set(frames.glob('frame_*.txt'))-existing
            with log_path.open('rb') as log:
                log.seek(log_offset)
                completed=b'passes written to' in log.read()
            if captured and completed:
                print('Frame saved:',max(captured,key=lambda p:p.stat().st_mtime),flush=True)
                existing.update(captured)
                break
            time.sleep(0.5)
        else:
            print('No completed capture yet. Keep gameplay unpaused; logs:',folder); return 1
        if index+1<args.sequence: time.sleep(2)
    print('Capture complete. You can close the game and tell Codex done.'); return 0

if __name__=='__main__': raise SystemExit(main())
