#!/usr/bin/env python3
"""Download the pinned Battlegrounds sources into data/battlegrounds/source (cards.json and hsdata CardDefs.xml).

Usage (from the module root):
    python -I tools/battlegrounds/fetch_bg_source.py

The files are Blizzard client data and are not stored in the repository (see data/battlegrounds/source/PROVENANCE.md).
Each download is checked against the sha256 pinned in bglib.PINNED_SOURCES; a mismatch is an error and nothing is
written. Files that already match its pin is left alone. Standard library only.
"""

from __future__ import annotations

import sys
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bglib  # noqa: E402

USER_AGENT = "mod-hearthstone-setup (fetch_bg_source.py)"


def download(url: str) -> bytes:
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=120) as response:
        return response.read()


def fetch(pin: dict, dest_dir: Path) -> str:
    path = dest_dir / pin["file"]
    if path.exists() and bglib.sha256_file(path) == pin["sha256"]:
        return "ok (already present)"
    data = download(pin["url"])
    actual = bglib.sha256_bytes(data)
    if actual != pin["sha256"]:
        raise SystemExit(
            f"sha256 mismatch for {pin['url']}\n"
            f"  expected {pin['sha256']}\n"
            f"  got      {actual}\n"
            "The upstream file changed; nothing was written. Update the pin in bglib.py only after checking the new file."
        )
    dest_dir.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return f"downloaded {len(data)} bytes"


def main() -> int:
    for pin in bglib.PINNED_SOURCES:
        print(f"{pin['file']}: {fetch(pin, bglib.SOURCE_DIR)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
