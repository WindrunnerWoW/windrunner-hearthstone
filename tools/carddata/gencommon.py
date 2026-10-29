"""Shared helpers for the card generators (server SQL, addon CardDB.lua).

All generators read the normalized dataset through ``cardlib.load_dataset()`` and share the
identity helpers below, so the SQL, the Lua table and ``src/HsDataset.h`` cannot disagree.
Standard library only.
"""

from __future__ import annotations

import sys
from pathlib import Path

import cardlib

REPO_ROOT = cardlib.REPO_ROOT

# Hash characters that go on the wire (WELCOME.hash); must equal scripts/gen_dataset_header.py.
WIRE_HASH_CHARS = 16

# Dataset schema versions these generators understand.
SUPPORTED_SCHEMA_VERSIONS = (1,)

# Class display order for collection browsing: the nine classes alphabetically, then neutral,
# then the special DREAM class (Ysera dream cards, never collectible).
CLASS_ORDER = ("DRUID", "HUNTER", "MAGE", "PALADIN", "PRIEST", "ROGUE", "SHAMAN",
               "WARLOCK", "WARRIOR", "NEUTRAL", "DREAM")


class GeneratorError(Exception):
    pass


def load() -> dict:
    """Load and sanity-check the normalized dataset."""
    ds = cardlib.load_dataset()
    if ds.get("schema_version") not in SUPPORTED_SCHEMA_VERSIONS:
        raise GeneratorError(
            f"dataset schema_version {ds.get('schema_version')!r} not supported by the generators "
            f"(supported: {SUPPORTED_SCHEMA_VERSIONS})")
    if ds["card_count"] != len(ds["cards"]):
        raise GeneratorError("dataset card_count does not match the number of cards")
    if ds["content_hash"] != cardlib.content_hash(ds["cards"]):
        raise GeneratorError("dataset content_hash does not match its cards: rerun import_cards.py")
    ids = [c["id"] for c in ds["cards"]]
    if len(set(ids)) != len(ids):
        raise GeneratorError("duplicate card ids in dataset")
    return ds


def wire_hash(ds: dict) -> str:
    return ds["content_hash"][:WIRE_HASH_CHARS]


def sorted_cards(ds: dict) -> list:
    """Cards in a stable, codepoint-sorted order by id."""
    return sorted(ds["cards"], key=lambda c: c["id"])


def check_or_write(path: Path, text: str, check: bool, label: str) -> bool:
    """Write ``text`` (LF endings) to ``path``, or with ``check`` compare. Returns True if OK."""
    path = Path(path)
    data = text.encode("utf-8")
    if check:
        try:
            current = path.read_bytes().replace(b"\r\n", b"\n")
        except OSError:
            current = None
        if current != data:
            print(f"{label} is stale: {path}")
            return False
        print(f"{label} is up to date")
        return True
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "wb") as fh:
        fh.write(data)
    print(f"wrote {path} ({len(data)} bytes)")
    return True


def relpath(path: Path) -> str:
    try:
        return Path(path).resolve().relative_to(REPO_ROOT).as_posix()
    except ValueError:
        return str(path)


def main_wrapper(fn):
    try:
        return fn(sys.argv[1:])
    except (GeneratorError, cardlib.CardDataError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
