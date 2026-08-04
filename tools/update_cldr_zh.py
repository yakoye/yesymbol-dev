#!/usr/bin/env python3
"""Download and cache pinned CLDR Simplified-Chinese character short names.

YeSymbol uses the ``type="tts"`` annotation as the concise display name.  The
cache is optional at build time: normal regeneration keeps working offline, and
``refresh`` can be used when a network connection is available.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import tempfile
import urllib.error
import urllib.request
import xml.etree.ElementTree as ET
from pathlib import Path
from typing import Dict, Tuple

ROOT = Path(__file__).resolve().parents[1]
CACHE_PATH = ROOT / "data-source" / "cldr-annotations-zh.tts.json"
COMMIT = "c9a5503bf238114a1993377b87841fb76031371d"
GIT_BLOB_SHA = "765c963b2ba18ce5844bc737b1d4570b0b45648e"
SOURCE_PATH = "common/annotations/zh.xml"
SOURCE_URL = f"https://github.com/unicode-org/cldr/blob/{COMMIT}/{SOURCE_PATH}"
RAW_URL = f"https://raw.githubusercontent.com/unicode-org/cldr/{COMMIT}/{SOURCE_PATH}"


def normalize_key(text: str) -> str:
    # The pinned CLDR file explicitly stores cp values without U+FE0F.
    return text.replace("\ufe0f", "")


def git_blob_sha(payload: bytes) -> str:
    header = f"blob {len(payload)}\0".encode("ascii")
    return hashlib.sha1(header + payload).hexdigest()


def parse_xml(payload: bytes) -> Dict[str, str]:
    root = ET.fromstring(payload)
    output: Dict[str, str] = {}
    for node in root.iter("annotation"):
        if node.attrib.get("type") != "tts":
            continue
        cp = normalize_key(node.attrib.get("cp", ""))
        name = (node.text or "").strip()
        if cp and name:
            output[cp] = name
    if not output:
        raise ValueError("CLDR XML contains no type=tts annotations")
    return output


def load_cache(path: Path = CACHE_PATH) -> Tuple[dict, Dict[str, str]]:
    if not path.exists():
        return {}, {}
    data = json.loads(path.read_text(encoding="utf-8"))
    if isinstance(data, dict) and "tts" in data:
        metadata = dict(data.get("metadata", {}))
        names = {normalize_key(str(k)): str(v) for k, v in data.get("tts", {}).items() if str(k) and str(v)}
        return metadata, names
    # Accept the early plain-map cache format for migration.
    return {"complete": False}, {normalize_key(str(k)): str(v) for k, v in data.items() if str(k) and str(v)}


def write_json_atomic(path: Path, data: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = (json.dumps(data, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode("utf-8")
    fd, temp_name = tempfile.mkstemp(prefix=path.name + ".", suffix=".tmp", dir=str(path.parent))
    temp = Path(temp_name)
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temp, path)
    finally:
        try:
            temp.unlink(missing_ok=True)
        except OSError:
            pass


def download_xml(timeout: float = 30.0) -> bytes:
    request = urllib.request.Request(
        RAW_URL,
        headers={"User-Agent": "YeSymbol-CLDR-Updater/1.0", "Accept": "application/xml,text/xml,*/*"},
    )
    with urllib.request.urlopen(request, timeout=timeout) as response:
        payload = response.read()
    actual = git_blob_sha(payload)
    if actual != GIT_BLOB_SHA:
        raise ValueError(f"unexpected CLDR Git blob SHA: {actual}; expected {GIT_BLOB_SHA}")
    return payload


def refresh(path: Path = CACHE_PATH) -> int:
    payload = download_xml()
    names = parse_xml(payload)
    data = {
        "metadata": {
            "source": SOURCE_URL,
            "raw_source": RAW_URL,
            "commit": COMMIT,
            "git_blob_sha": GIT_BLOB_SHA,
            "license": "Unicode-3.0",
            "complete": True,
            "normalization": "U+FE0F removed from lookup keys",
            "annotation_count": len(names),
        },
        "tts": names,
    }
    write_json_atomic(path, data)
    print(f"cached {len(names)} CLDR Chinese TTS names -> {path}")
    return 0


def ensure(path: Path = CACHE_PATH) -> int:
    metadata, names = load_cache(path)
    if metadata.get("complete") and metadata.get("commit") == COMMIT and names:
        print(f"CLDR Chinese cache ready: {len(names)} names")
        return 0
    try:
        return refresh(path)
    except (OSError, ValueError, ET.ParseError, urllib.error.URLError) as exc:
        if names:
            print(f"[WARN] Could not refresh CLDR Chinese names: {exc}")
            print(f"[WARN] Continuing with bundled partial cache: {len(names)} names")
            return 0
        print(f"[WARN] Could not download CLDR Chinese names: {exc}")
        print("[WARN] Continuing with names already stored in catalog.txt")
        return 0


def check(path: Path = CACHE_PATH) -> int:
    metadata, names = load_cache(path)
    if not names:
        print("CLDR Chinese cache is missing or empty")
        return 1
    state = "complete" if metadata.get("complete") else "partial"
    print(f"CLDR Chinese cache OK: {len(names)} names ({state}), commit={metadata.get('commit', 'unknown')}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("ensure", "refresh", "check"), nargs="?", default="ensure")
    parser.add_argument("--cache", type=Path, default=CACHE_PATH)
    args = parser.parse_args()
    try:
        if args.command == "ensure":
            return ensure(args.cache)
        if args.command == "refresh":
            return refresh(args.cache)
        return check(args.cache)
    except (OSError, ValueError, ET.ParseError, urllib.error.URLError) as exc:
        print(f"ERROR: {exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
