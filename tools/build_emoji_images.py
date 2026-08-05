#!/usr/bin/env python3
"""Download Twemoji artwork for catalog symbols and embed it as C data.

Why images at all: Windows' Segoe UI Emoji suppresses every regional-indicator
flag ligature when the system Region is set to certain countries, so flags fall
back to bare letter pairs no matter what the application does.  Shipping the
artwork ourselves side-steps that entirely, and also makes the first paint of a
symbol a plain bitmap blit instead of a color-glyph rasterization.

Format note: Twemoji's own 72x72 PNGs are embedded verbatim.  They are already
palette-optimized (about 570 bytes each on average -- smaller than re-encoding
them at cell size, and smaller than JPEG) and, unlike JPEG, they keep the alpha
channel that emoji need to sit on the grid's white or highlighted background.

Twemoji graphics are CC-BY 4.0; see THIRD_PARTY_NOTICES.md.

Usage:
    python tools/build_emoji_images.py flags     # flags only (default)
    python tools/build_emoji_images.py all       # every emoji in the catalog
"""
from __future__ import annotations

import json
import os
import stat
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.request
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from typing import Dict, List, Tuple

# The 'all' scope pulls a few thousand files; fetching them one at a time is
# dominated by round-trip latency, so download in parallel.  Kept modest to
# stay polite to the CDN.
DOWNLOAD_WORKERS = 8

ROOT = Path(__file__).resolve().parents[1]
CATALOG = ROOT / "data-source" / "catalog.generated.json"
CACHE_DIR = ROOT / "data-source" / "emoji-images"
OUTPUT = ROOT / "src" / "emoji_image_data.c"

# Pinned so a rebuild cannot silently pick up different artwork.
TWEMOJI_REF = "v17.0.3"
BASE_URL = f"https://cdn.jsdelivr.net/gh/jdecked/twemoji@{TWEMOJI_REF}/assets/72x72/{{}}.png"

VARIATION_SELECTOR = 0xFE0F


def _join(points: List[int]) -> str:
    return "-".join(f"{cp:x}" for cp in points)


def twemoji_names(text: str) -> List[str]:
    """Candidate Twemoji filenames for a symbol, best guess first.

    Twemoji is not consistent about the U+FE0F variation selector: plain
    single-symbol files drop it (`2764` for U+2764 U+FE0F), while many ZWJ and
    skin-tone sequences keep it (`26f9-1f3fb-200d-2640-fe0f`).  Rather than
    trying to model that rule, try both spellings and use whichever exists --
    a miss just means falling back to font rendering, so guessing wrong is
    only a quality loss, never a failure.
    """
    points = [ord(ch) for ch in text]
    stripped = [cp for cp in points if cp != VARIATION_SELECTOR] or points
    names = [_join(stripped)]
    if stripped != points:
        names.append(_join(points))
    return names


def twemoji_name(text: str) -> str:
    """The primary candidate name, used for cache filenames and reporting."""
    return twemoji_names(text)[0]


def is_regional_flag(text: str) -> bool:
    return len(text) == 2 and all(0x1F1E6 <= ord(ch) <= 0x1F1FF for ch in text)


def is_tag_flag(text: str) -> bool:
    return len(text) >= 3 and ord(text[0]) == 0x1F3F4 and ord(text[-1]) == 0xE007F


def select_symbols(scope: str) -> List[str]:
    data = json.loads(CATALOG.read_text(encoding="utf-8"))
    out: List[str] = []
    seen = set()
    for record in data["symbols"]:
        text = record["text"]
        if text in seen:
            continue
        if scope == "flags":
            keep = is_regional_flag(text) or is_tag_flag(text)
        else:
            keep = bool(record.get("emoji"))
        if keep:
            seen.add(text)
            out.append(text)
    return out


def fetch(name: str, retries: int = 3) -> bytes | None:
    url = BASE_URL.format(name)
    for attempt in range(retries):
        try:
            with urllib.request.urlopen(url, timeout=30) as response:
                return response.read()
        except urllib.error.HTTPError as exc:
            if exc.code == 404:
                return None          # Twemoji genuinely has no art for this one.
            if attempt == retries - 1:
                raise
        except Exception:
            if attempt == retries - 1:
                raise
        time.sleep(1.0 + attempt)
    return None


def load_or_download(text: str) -> bytes | None:
    """Returns the PNG bytes for `text`, using the on-disk cache when present.

    Cached under the primary candidate name regardless of which spelling the
    CDN actually served, so the cache stays a simple 1:1 map from symbol to
    file and a rerun does not re-probe the alternatives.
    """
    path = CACHE_DIR / f"{twemoji_name(text)}.png"
    if path.exists():
        return path.read_bytes()
    for name in twemoji_names(text):
        payload = fetch(name)
        if payload is not None:
            CACHE_DIR.mkdir(parents=True, exist_ok=True)
            path.write_bytes(payload)
            return payload
    return None


def utf16_units(text: str) -> List[int]:
    """The string's UTF-16 code units, so it can be emitted as a pure-ASCII
    C wide-string literal (same convention as generate_bilingual_data.py)."""
    data = text.encode("utf-16-le")
    return [int.from_bytes(data[i:i + 2], "little") for i in range(0, len(data), 2)]


def write_output(entries: List[Tuple[str, bytes]]) -> None:
    """Emit a C file: one blob of PNG bytes plus a text -> (offset, size) index.

    The symbol text is stored as a UTF-16 literal so the runtime can compare it
    directly against the WCHAR strings in the compiled symbol pool.
    """
    lines: List[str] = []
    lines.append('#include "emoji_image_data.h"\n')

    blob: List[int] = []
    index: List[Tuple[str, int, int]] = []
    for text, payload in entries:
        index.append((text, len(blob), len(payload)))
        blob.extend(payload)

    lines.append("const unsigned char g_ys_emoji_image_blob[] = {")
    for start in range(0, len(blob), 20):
        chunk = blob[start:start + 20]
        lines.append("    " + ",".join(str(value) for value in chunk) + ",")
    lines.append("};\n")

    lines.append("const YSEmojiImageRecord g_ys_emoji_images[] = {")
    for text, offset, size in index:
        escaped = "".join(f"\\x{unit:04x}" for unit in utf16_units(text))
        lines.append(f'    {{L"{escaped}", {offset}u, {size}u}},')
    lines.append("};\n")

    lines.append(f"const size_t g_ys_emoji_image_count = {len(index)}u;\n")
    lines.append(f"const size_t g_ys_emoji_image_blob_size = {len(blob)}u;\n")

    text_out = "\n".join(lines)
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    if OUTPUT.exists():
        try:
            OUTPUT.chmod(OUTPUT.stat().st_mode | stat.S_IWRITE)
        except OSError:
            pass
    tmp = tempfile.NamedTemporaryFile("w", encoding="ascii", dir=str(OUTPUT.parent),
                                      delete=False, newline="\n")
    try:
        tmp.write(text_out)
        tmp.close()
        os.replace(tmp.name, OUTPUT)
    except BaseException:
        os.unlink(tmp.name)
        raise


def main() -> int:
    scope = sys.argv[1] if len(sys.argv) > 1 else "flags"
    if scope not in ("flags", "all"):
        print(f"unknown scope {scope!r}; use 'flags' or 'all'", file=sys.stderr)
        return 2

    symbols = select_symbols(scope)
    print(f"scope={scope}: {len(symbols)} candidate symbols")

    CACHE_DIR.mkdir(parents=True, exist_ok=True)
    done = 0
    lock = threading.Lock()

    def work(text: str) -> Tuple[str, bytes | None]:
        nonlocal done
        try:
            payload = load_or_download(text)
        except Exception as exc:                     # noqa: BLE001 - report and continue
            print(f"  {twemoji_name(text)}: {exc}")
            payload = None
        with lock:
            done += 1
            if done % 250 == 0 or done == len(symbols):
                print(f"  {done}/{len(symbols)}")
        return text, payload

    with ThreadPoolExecutor(max_workers=DOWNLOAD_WORKERS) as pool:
        results = list(pool.map(work, symbols))

    # Keep catalog order rather than completion order so the generated file is
    # deterministic across runs.
    entries: List[Tuple[str, bytes]] = [(t, p) for t, p in results if p is not None]
    missing: List[str] = [t for t, p in results if p is None]

    write_output(entries)
    total = sum(len(payload) for _, payload in entries)
    print(f"embedded {len(entries)} images, {total} bytes "
          f"({total / 1024:.0f} KB, avg {total // max(1, len(entries))} B)")
    if missing:
        print(f"no Twemoji artwork for {len(missing)}: "
              f"{[twemoji_name(text) for text in missing[:8]]}")
    print(f"wrote {OUTPUT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
