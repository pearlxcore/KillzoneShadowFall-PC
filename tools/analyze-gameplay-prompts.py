"""Decode the bounded native prompt trace; never changes game assets or caches.

Validates atlas dimensions, icon rectangles and UV coordinates against one
another. This is evidence for integration, not a PC-prompt implementation.
"""
import argparse
import json
import math
from pathlib import Path
import re
import struct

EVENT = re.compile(r"^KILLZONE_PROMPT path=(\S+) request=(-?\d+) collection=([0-9a-fA-F]+) caller=0x([0-9a-fA-F]+)$")
ENTRY = re.compile(r"^  entry=(\d+) button=(-?\d+) axis=(-?\d+) icon=([0-9a-fA-F]+)$")
BYTES = re.compile(r"^  (collection|texture-reference-target|entry-bytes|uv-quad|icon-record) address=[0-9a-fA-F]+ ([0-9a-fA-F ]+)$")
# Verified in EDeviceFunction (descriptor 0x16a1f98, enum entries 0x16a1fe0,
# stride 0x28) in the supported eboot.elf. These are action IDs, not button IDs.
ACTIONS = {0: "PrimaryFire", 1: "SwitchFire", 2: "MeleeWeaponAttack", 3: "ZoomSwitch",
           4: "ZoomModeSwitch", 7: "Jump", 8: "Use", 18: "Vault", 24: "CloseCombat",
           76: "SelectUp", 79: "SelectRight", 80: "IntelScreenCampaign", 82: "OptionScreenCampaign"}


def decode(text):
    events, collections = [], {}
    collection = entry = None
    for line in text.splitlines():
        match = EVENT.match(line)
        if match:
            path, request, address, caller = match.groups()
            request = int(request)
            event = dict(path=path, request=request, action=ACTIONS.get(request) if path.startswith("gamefunction") else None,
                         collection="0x" + address.lower(), caller="0x" + caller.lower())
            events.append(event)
            collection = collections.setdefault(event["collection"], {"paths": [], "entries": []})
            if path not in collection["paths"]:
                collection["paths"].append(path)
            entry = None
            continue
        if collection is None:
            continue
        match = ENTRY.match(line)
        if match:
            n, button, axis, icon = match.groups()
            entry = dict(index=int(n), button=int(button), axis=int(axis), icon="0x" + icon.lower())
            collection["entries"].append(entry)
            continue
        match = BYTES.match(line)
        if not match:
            continue
        name, hex_bytes = match.groups()
        data = bytes.fromhex(hex_bytes)
        if name == "collection" and len(data) >= 0x50:
            collection["declared_count"] = struct.unpack_from("<i", data, 0x18)[0]
        elif name == "texture-reference-target" and len(data) >= 0x38:
            collection["atlas_size"] = list(struct.unpack_from("<II", data, 0x18))
            collection["native_format_field"] = struct.unpack_from("<I", data, 0x20)[0]
            collection["texture_data_address"] = hex(struct.unpack_from("<Q", data, 0x30)[0])
        elif entry is not None:
            if name == "entry-bytes" and len(data) == 0x30:
                entry["legacy_rect"] = list(struct.unpack_from("<4i", data, 0x10))
                entry["legacy_metrics"] = list(struct.unpack_from("<2i", data, 0x20))
            elif name == "icon-record" and len(data) >= 0x30:
                entry["icon_rect"] = list(struct.unpack_from("<4i", data, 0x20))
            elif name == "uv-quad" and len(data) == 0x40:
                floats = struct.unpack("<16f", data)
                entry["uv"] = [[floats[k], floats[k+1]] for k in (0, 4, 8, 12)]

    errors = []
    for address, record in collections.items():
        width, height = record.get("atlas_size", (0, 0))
        if not 0 < width <= 16384 or not 0 < height <= 16384:
            errors.append(f"{address}: atlas dimensions missing or invalid")
            continue
        seen = set()
        for icon in record["entries"]:
            index = icon["index"]
            if index in seen:
                errors.append(f"{address}: duplicate entry {index}")
            seen.add(index)
            rect = icon.get("icon_rect", icon.get("legacy_rect"))
            uv = icon.get("uv")
            if not rect or not uv:
                errors.append(f"{address} entry {index}: incomplete rectangle/UV record")
                continue
            x0, y0, x1, y1 = rect
            expected = ((x0/width, y0/height), (x1/width, y0/height),
                        (x1/width, y1/height), (x0/width, y1/height))
            icon["uv_matches_rect"] = all(math.isfinite(v) and abs(v-e) < 1e-6
                                           for pair, want in zip(uv, expected) for v, e in zip(pair, want))
            if not icon["uv_matches_rect"]:
                errors.append(f"{address} entry {index}: UV/rectangle mismatch")
            if not (0 <= x0 < x1 <= width and 0 <= y0 < y1 <= height):
                errors.append(f"{address} entry {index}: rectangle outside atlas")
        record["captured_count"] = len(record["entries"])
        declared = record.get("declared_count", 0)
        if len(record["entries"]) != min(32, declared):
            errors.append(f"{address}: missing entries ({len(record['entries'])}/{declared})")
    return dict(events=events, collections=collections, validation_errors=errors)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path, help="native-prompts-* session directory")
    args = parser.parse_args()
    result = decode((args.capture / "stderr.log").read_text(encoding="utf-8", errors="replace"))
    status_file = args.capture / "status.json"
    if status_file.is_file():
        result["session"] = json.loads(status_file.read_text(encoding="utf-8"))
    target = args.capture / "prompt-analysis.json"
    target.write_text(json.dumps(result, indent=2, allow_nan=False), encoding="utf-8")
    print(f"{len(result['events'])} requests; {len(result['collections'])} atlas collections")
    for address, record in result["collections"].items():
        print(address, record.get("atlas_size"), "entries:", record["captured_count"])
    for error in result["validation_errors"]:
        print("INVALID:", error)
    print("Report:", target)
    return int(bool(result["validation_errors"]) or not result["collections"])


if __name__ == "__main__":
    raise SystemExit(main())
