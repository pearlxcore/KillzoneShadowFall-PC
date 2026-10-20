"""Capture native gameplay icon records; does not replace any prompt artwork."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "launcher"))
import kzsf_local as ui


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report-only", action="store_true")
    args = parser.parse_args()
    values = ui.load_preferences()
    folder = ui.ROOT / "out" / ("native-prompts-" + time.strftime("%Y%m%d-%H%M%S"))
    spec = ui.launch_spec(values, folder)
    spec["command"][0] = str(ui.ROOT / "out/gow3-probe-prompt-trace.exe")
    spec["env"]["GOW3_CONFIG"] = str(folder / "settings.ini")
    spec["env"]["KZSF_PROMPT_TRACE"] = "1"
    report = {
        "diagnostic": "native-gameplay-prompts",
        "artwork_replacement": False,
        "command": spec["command"],
        "config": spec["env"]["GOW3_CONFIG"],
        "log_folder": str(folder),
        "key_map": spec["env"].get("GOW3_KEY_MAP", "runtime defaults"),
    }
    if args.report_only:
        print(json.dumps(report, indent=2))
        return 0
    running = subprocess.run(
        ["tasklist", "/FI", "IMAGENAME eq gow3-probe*", "/FO", "CSV", "/NH"],
        capture_output=True, text=True, creationflags=subprocess.CREATE_NO_WINDOW,
    )
    if "gow3-probe" in running.stdout.lower():
        print("Close the current Killzone game, then run this BAT again.")
        return 1
    ok, reason = ui.validate_game(values["game_dir"])
    if not ok:
        print(reason)
        return 1
    if not Path(spec["command"][0]).is_file():
        print("The diagnostic build is missing: out/gow3-probe-prompt-trace.exe")
        return 1
    folder.mkdir(parents=True, exist_ok=False)
    if ui.CONFIG.is_file():
        shutil.copyfile(ui.CONFIG, folder / "settings.ini")
    (folder / "capture.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    manifest = folder / "session.json"
    manifest.write_text(json.dumps({k: v for k, v in spec.items() if k != "env"}, indent=2), encoding="utf-8")
    print("Native prompt diagnostic: PlayStation artwork stays unchanged.")
    print("1. Reach a contextual prompt, for example an interact/reload or hold action.")
    print("2. Reach a gameplay tutorial displaying a controller button, if available.")
    print("3. Close the game and reply done. No screenshots are needed for this capture.")
    print("Settings are isolated; shader-cache preparation stays enabled as configured.")
    print("Logs:", folder, flush=True)
    return subprocess.Popen(
        [sys.executable, str(Path(ui.__file__)), "--run-session", str(manifest)],
        env=spec["env"], cwd=ui.ROOT, creationflags=subprocess.CREATE_NO_WINDOW,
    ).wait()


if __name__ == "__main__":
    raise SystemExit(main())
