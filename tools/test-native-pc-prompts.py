"""Isolated comparison of native PC artwork against the original PS glyphs.

Native artwork with automatic input selection and live manual overrides.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "launcher"))
import kzsf_local as ui


def startup_smoke(spec, folder):
    """Drive only the menu of our own test process, using a copied save profile."""
    profile = folder / "user"
    profile.mkdir()
    if (ui.ROOT / "user/savedata").is_dir():
        shutil.copytree(ui.ROOT / "user/savedata", profile / "savedata")
    pad = folder / "pad.txt"
    pad.write_text("")
    spec["env"].update(KZSF_PROMPT_TEXTURE_SMOKE="1", KZSF_PRECOMPILE="0",
                       KZSF_USER_DIR=str(profile), KZSF_PAD_FILE=str(pad))
    with (folder / "stdout.log").open("wb") as out, (folder / "stderr.log").open("wb") as err:
        game = subprocess.Popen(spec["command"], cwd=ui.ROOT, env=spec["env"], stdout=out, stderr=err,
                                creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            for second in range(40):
                if game.poll() is not None:
                    break
                time.sleep(1)
                if second in (10, 20):
                    pad.write_text("cross")
                if second in (11, 21):
                    pad.write_text("")
                if "constructor smoke=" in (folder / "stderr.log").read_text(errors="replace"):
                    break
        finally:
            terminated = game.poll() is None
            if terminated:
                game.terminate()
            game.wait(timeout=10)
    log = (folder / "stderr.log").read_text(errors="replace")
    passed = "constructor smoke=pass" in log
    (folder / "status.json").write_text(json.dumps(dict(pid=game.pid, exit_code=game.returncode,
        terminated_by_smoke=terminated, constructor_passed=passed)), encoding="utf-8")
    print("Constructor smoke:", "PASS" if passed else "FAILED", "logs:", folder)
    for line in log.splitlines():
        if "PROMPT_PC" in line or "Fault:" in line:
            print(line)
    return 0 if passed else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("auto", "pc", "original"))
    parser.add_argument("--mouse-off", action="store_true")
    parser.add_argument("--report-only", action="store_true")
    parser.add_argument("--startup-smoke", action="store_true", help="automated menu/constructor test; uses copied saves")
    args = parser.parse_args()
    if args.startup_smoke and args.mode != "pc":
        parser.error("--startup-smoke requires pc mode")
    values = ui.load_preferences()
    values["prompt_style"] = {"auto":"0", "pc":"1", "original":"2"}[args.mode]
    if args.mouse_off:
        values["mouse_aim"] = False
    folder = ui.ROOT / "out" / ("pc-prompts-" + args.mode + "-" + time.strftime("%Y%m%d-%H%M%S"))
    spec = ui.launch_spec(values, folder)
    spec["command"][0] = str(ui.ROOT / "out/kzsf-probe-pc-prompts.exe")
    spec["env"]["KZSF_CONFIG"] = str(folder / "settings.ini")
    spec["env"]["KZSF_NATIVE_PROMPTS"] = "1"
    spec["env"]["KZSF_PROMPT_PC_TEST"] = "0"
    spec["env"]["KZSF_PROMPT_TRACE"] = "1"
    spec["env"]["KZSF_PROMPT_TEXTURE_SMOKE"] = "0"
    report = dict(mode=args.mode, mouse_aim=bool(values["mouse_aim"]),
                  executable=spec["command"][0], settings=spec["env"]["KZSF_CONFIG"],
                  effective_key_spec=spec["env"].get("KZSF_KEY_MAP", "runtime defaults"))
    if args.report_only:
        print(json.dumps(report, indent=2))
        return 0
    running = subprocess.run(["tasklist", "/FI", "IMAGENAME eq kzsf-probe*", "/FO", "CSV", "/NH"],
                             capture_output=True, text=True, creationflags=subprocess.CREATE_NO_WINDOW)
    if "kzsf-probe" in running.stdout.lower():
        print("Close the current Killzone game, then run this BAT again.")
        return 1
    ok, reason = ui.validate_game(values["game_dir"])
    if not ok:
        print(reason)
        return 1
    if not Path(spec["command"][0]).is_file():
        print("Missing local prototype build: out/kzsf-probe-pc-prompts.exe")
        return 1
    folder.mkdir(parents=True, exist_ok=False)
    if ui.CONFIG.is_file():
        shutil.copyfile(ui.CONFIG, folder / "settings.ini")
    if args.mouse_off:
        ui.write_keys(folder / "settings.ini", {"mouse_aim": "0"})
    (folder / "comparison.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    if args.startup_smoke:
        return startup_smoke(spec, folder)
    manifest = folder / "session.json"
    manifest.write_text(json.dumps({k: v for k, v in spec.items() if k != "env"}, indent=2), encoding="utf-8")
    print("Mode:", args.mode, "- mouse aiming:", report["mouse_aim"])
    print("Check a Jump tutorial and an Interact/Vault prompt during normal gameplay.")
    print("PC mode should draw keycaps inside the native prompts. Stick/gesture art stays original.")
    print("Automatic follows accepted input; forced modes can be changed live in overlay Game.")
    print("Normal launcher preferences and game files are unchanged. Shader preparation is retained.")
    print("Logs:", folder, flush=True)
    return subprocess.Popen([sys.executable, str(Path(ui.__file__)), "--run-session", str(manifest)],
                            cwd=ui.ROOT, env=spec["env"], creationflags=subprocess.CREATE_NO_WINDOW).wait()


if __name__ == "__main__":
    raise SystemExit(main())
