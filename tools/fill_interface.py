"""Fill resource.hash and version into interface.json.

Usage (in CI):
    python -m pip install maafw
    python tools/fill_interface.py \
        --version v1.0.2 \
        --interface install/interface.json
"""

import argparse
import json
import re
import sys
from pathlib import Path


def strip_jsonc_comments(text: str) -> str:
    return re.sub(r"^\s*//.*$", "", text, flags=re.MULTILINE)


def load_jsonc(path: Path) -> dict:
    raw = path.read_text(encoding="utf-8")
    cleaned = strip_jsonc_comments(raw)
    return json.loads(cleaned)


def save_json(path: Path, data: dict) -> None:
    text = json.dumps(data, indent="\t", ensure_ascii=False) + "\n"
    path.write_text(text, encoding="utf-8")


def log(msg: str) -> None:
    sys.stdout.buffer.write((msg + "\n").encode("utf-8"))
    sys.stdout.buffer.flush()


def compute_resource_hashes(interface_data: dict, base_dir: Path) -> None:
    from maa.resource import Resource

    for res_item in interface_data.get("resource", []):
        paths = res_item.get("path", [])
        if not paths:
            continue

        resource = Resource()
        for p in paths:
            clean = re.sub(r"^\.[\\/]", "", p)
            full_path = base_dir / clean
            if not full_path.exists():
                log(f"  ERROR: resource path does not exist: {full_path}")
                sys.exit(1)
            job = resource.post_bundle(str(full_path))
            job.wait()

        h = resource.hash
        res_item["hash"] = h
        log(f"  {res_item['name']}: hash = {h}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Fill interface.json with version and resource hashes")
    parser.add_argument("--version", required=True, help="Version tag to set")
    parser.add_argument("--interface", required=True, help="Path to interface.json to modify")
    args = parser.parse_args()

    interface_path = Path(args.interface)
    base_dir = interface_path.parent

    log(f"Loading {interface_path}")
    data = load_jsonc(interface_path)

    log(f"Setting version = {args.version}")
    data["version"] = args.version

    log("Computing resource hashes...")
    compute_resource_hashes(data, base_dir)

    log(f"Writing {interface_path}")
    save_json(interface_path, data)
    log("Done.")


if __name__ == "__main__":
    main()
