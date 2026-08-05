from __future__ import annotations

import importlib.util
import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools" / "build_emoji_images.py"

spec = importlib.util.spec_from_file_location("build_emoji_images_test", SCRIPT)
emoji_images = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(emoji_images)

# Keep the artwork source pinned, but pin it to the catalog's Unicode version.
assert emoji_images.TWEMOJI_REF == "v17.0.3"

catalog = json.loads((ROOT / "data-source" / "catalog.generated.json").read_text(encoding="utf-8"))
by_text = {item["text"]: item for item in catalog["symbols"]}

# Bare keycap bases and CJK ideographs are text symbols, not standalone color emoji.
for text in ("#", "*", *"0123456789", "𤤴", "𰻝"):
    assert not by_text[text]["emoji"], text

symbols = emoji_images.select_symbols("all")
missing = [
    text
    for text in symbols
    if not (emoji_images.CACHE_DIR / f"{emoji_images.twemoji_name(text)}.png").is_file()
]
assert not missing, [emoji_images.twemoji_name(text) for text in missing]

# The normal data command must embed every color emoji, not just flags.
regenerate = (ROOT / "regenerate-data.cmd").read_text(encoding="utf-8")
assert "python tools\\build_emoji_images.py all" in regenerate

generated = (ROOT / "src" / "emoji_image_data.c").read_text(encoding="ascii")
match = re.search(r"g_ys_emoji_image_count = (\d+)u", generated)
assert match and int(match.group(1)) == len(symbols)

# Embedded artwork remains on the bitmap path even in the differently-sized
# recent-use strip; font-baked fallback bitmaps still keep the size guard.
renderer = (ROOT / "src" / "emoji_renderer.c").read_text(encoding="utf-8")
assert "has_embedded_image ||" in renderer
assert "#define YS_EMOJI_IMAGE_SIDE 36" in renderer
assert "draw_side = min(YS_EMOJI_IMAGE_SIDE, min(dest_w, dest_h))" in renderer

print(f"emoji image checks passed: {len(symbols)} images, 0 missing")
