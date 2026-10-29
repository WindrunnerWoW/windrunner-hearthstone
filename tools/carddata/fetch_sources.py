#!/usr/bin/env python3
"""Download the pinned Hearthstone card sources into data/cards/source.

Usage (from the module root):
    python tools/carddata/fetch_sources.py              # fetch what is missing or wrong
    python tools/carddata/fetch_sources.py --crosscheck # also CardDefs.xml (for crosscheck_carddefs.py)

The files are Blizzard client data and are not stored in the repository (see data/cards/source/PROVENANCE.md).
Each download is checked against the sha256 pinned in cardlib.PINNED_SOURCES; a mismatch is an error and nothing
is written. Files that already match their pin are left alone. Standard library only.
"""

from __future__ import annotations

import argparse
import sys
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import cardlib  # noqa: E402

USER_AGENT = "mod-hearthstone-setup (fetch_sources.py)"


def download(url: str) -> bytes:
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=120) as response:
        return response.read()


def fetch(key: str, pin: dict, dest_dir: Path) -> str:
    path = dest_dir / pin["file"]
    if path.exists() and cardlib.sha256_file(path) == pin["sha256"]:
        return "ok (already present)"
    data = download(pin["url"])
    actual = cardlib.sha256_bytes(data)
    if actual != pin["sha256"]:
        raise SystemExit(
            f"{key}: sha256 mismatch for {pin['url']}\n"
            f"  expected {pin['sha256']}\n"
            f"  got      {actual}\n"
            "The upstream file changed; nothing was written. Update the pin in cardlib.py only after checking the new file."
        )
    dest_dir.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return f"downloaded {len(data)} bytes"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    ap.add_argument("--crosscheck", action="store_true", help="also fetch CardDefs.xml (not committed, large)")
    args = ap.parse_args()
    for key, pin in cardlib.PINNED_SOURCES.items():
        if not pin["committed"] and not args.crosscheck:
            continue
        print(f"{pin['file']}: {fetch(key, pin, cardlib.SOURCE_DIR)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
