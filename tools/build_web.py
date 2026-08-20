#!/usr/bin/env python3
"""Build the static YeSymbol web release from the shared catalog.generated.json."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import sys
from pathlib import Path

WEB_VERSION = "1.1.1"
REQUIRED_KEYS = {
    "categories",
    "symbols",
    "default_common",
    "ui_categories",
    "ui_category_groups",
}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_and_validate_catalog(path: Path) -> dict:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise SystemExit(f"[ERROR] Missing shared catalog: {path}") from exc
    except json.JSONDecodeError as exc:
        raise SystemExit(f"[ERROR] Invalid JSON in {path}: {exc}") from exc

    missing = REQUIRED_KEYS.difference(data)
    if missing:
        raise SystemExit(f"[ERROR] Catalog is missing keys: {', '.join(sorted(missing))}")
    if not isinstance(data["symbols"], list) or not isinstance(data["categories"], list):
        raise SystemExit("[ERROR] Catalog symbols/categories must be arrays")

    texts = [item.get("text") for item in data["symbols"] if isinstance(item, dict)]
    if len(texts) != len(data["symbols"]) or any(not isinstance(text, str) or not text for text in texts):
        raise SystemExit("[ERROR] Every symbol record must contain a non-empty text string")
    if len(set(texts)) != len(texts):
        raise SystemExit("[ERROR] Duplicate symbol records found in catalog.generated.json")
    return data


def make_service_worker(cache_token: str, assets: list[str]) -> str:
    asset_json = json.dumps(assets, ensure_ascii=False, indent=2)
    return f"""'use strict';

const CACHE_NAME = 'yesymbol-web-{WEB_VERSION}-{cache_token}';
const CORE_ASSETS = {asset_json};

self.addEventListener('install', event => {{
  event.waitUntil(caches.open(CACHE_NAME).then(cache => cache.addAll(CORE_ASSETS)));
  self.skipWaiting();
}});

self.addEventListener('activate', event => {{
  event.waitUntil(
    caches.keys().then(keys => Promise.all(
      keys.filter(key => key.startsWith('yesymbol-web-') && key !== CACHE_NAME)
          .map(key => caches.delete(key))
    ))
  );
  self.clients.claim();
}});

self.addEventListener('fetch', event => {{
  if (event.request.method !== 'GET') return;
  const url = new URL(event.request.url);
  if (url.origin !== self.location.origin) return;

  if (url.pathname.endsWith('/data/catalog.generated.json') || url.pathname.endsWith('/data/build-info.json')) {{
    event.respondWith(
      fetch(event.request).then(response => {{
        const copy = response.clone();
        caches.open(CACHE_NAME).then(cache => cache.put(event.request, copy));
        return response;
      }}).catch(() => caches.match(event.request))
    );
    return;
  }}

  event.respondWith(
    caches.match(event.request).then(cached => cached || fetch(event.request).then(response => {{
      if (response.ok) {{
        const copy = response.clone();
        caches.open(CACHE_NAME).then(cache => cache.put(event.request, copy));
      }}
      return response;
    }}))
  );
}});
"""


def build(root: Path, output: Path) -> None:
    source_dir = root / "web" / "src"
    catalog_path = root / "data-source" / "catalog.generated.json"

    if not source_dir.is_dir():
        raise SystemExit(f"[ERROR] Missing web source directory: {source_dir}")

    data = load_and_validate_catalog(catalog_path)
    catalog_hash = sha256_file(catalog_path)

    if output.exists():
        shutil.rmtree(output)
    shutil.copytree(source_dir, output)

    data_dir = output / "data"
    data_dir.mkdir(parents=True, exist_ok=True)
    output_catalog = data_dir / "catalog.generated.json"
    shutil.copy2(catalog_path, output_catalog)

    if sha256_file(output_catalog) != catalog_hash:
        raise SystemExit("[ERROR] Web catalog copy differs from shared source catalog")

    build_info = {
        "web_version": WEB_VERSION,
        "source": "data-source/catalog.generated.json",
        "source_sha256": catalog_hash,
        "symbol_count": len(data["symbols"]),
        "category_count": len(data["categories"]),
        "ui_category_count": len(data["ui_categories"]),
    }
    (data_dir / "build-info.json").write_text(
        json.dumps(build_info, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
        newline="\n",
    )

    # Static hosting services often route missing paths to 404.html. The app itself has no router,
    # but keeping an identical fallback makes accidental nested links recover cleanly.
    shutil.copy2(output / "index.html", output / "404.html")

    core_assets = [
        "./",
        "./index.html",
        "./assets/styles.css",
        "./assets/app.js",
        "./icons/icon-32.png",
        "./icons/icon-64.png",
        "./icons/icon-192.png",
        "./icons/icon-512.png",
        "./manifest.webmanifest",
        "./data/catalog.generated.json",
        "./data/build-info.json",
    ]
    (output / "sw.js").write_text(
        make_service_worker(catalog_hash[:12], core_assets),
        encoding="utf-8",
        newline="\n",
    )

    print(
        f"built {output}: {len(data['symbols'])} symbols, "
        f"{len(data['categories'])} data categories, catalog sha256 {catalog_hash}"
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--root",
        type=Path,
        default=Path(__file__).resolve().parents[1],
        help="YeSymbol project root",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=None,
        help="Output directory (default: <root>/dist-web)",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    root = args.root.resolve()
    output = (args.output or (root / "dist-web")).resolve()
    build(root, output)
    return 0


if __name__ == "__main__":
    sys.exit(main())
