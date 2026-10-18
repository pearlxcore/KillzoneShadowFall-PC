"""Prepare a user's own dump for the portable Windows release."""
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'launcher'))
from kzsf_local import validate_game


def main():
    folder = Path((sys.argv[1] if len(sys.argv) > 1 else
                   input('Game folder containing eboot.bin: ')).strip().strip('"')).resolve()
    valid, reason = validate_game(folder)
    if not valid:
        raise ValueError(reason)
    for script, extra in (
        ('prepare.py', []),
        ('link_modules.py', ['--modules', 'libc.prx', 'libSceFios2.prx',
                             '../all_programs.prx', '--dynamic-exports']),
        ('content_profile.py', ['--sku', 'full']),
    ):
        subprocess.run([sys.executable, str(ROOT / 'scripts' / script),
                        str(folder), '--out', str(ROOT / 'out'), *extra],
                       cwd=ROOT, check=True)
    preferences = ROOT / 'user' / 'launcher-local.json'
    values = json.loads(preferences.read_text(encoding='utf-8')) if preferences.exists() else {}
    values['game_dir'] = str(folder)
    preferences.parent.mkdir(parents=True, exist_ok=True)
    temporary = preferences.with_suffix('.tmp')
    temporary.write_text(json.dumps(values, indent=2) + '\n', encoding='utf-8')
    temporary.replace(preferences)
    print('Ready. Open Killzone Launcher.exe to play.')


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f'Setup failed: {error}', file=sys.stderr)
        sys.exit(1)
