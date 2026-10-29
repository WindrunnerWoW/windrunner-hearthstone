#!/usr/bin/env python3
"""Generate the addon card database (addon/Hearthstone/CardDB.lua).

Usage (from the module root):
    python tools/carddata/gen_addon_carddb.py           # write addon/Hearthstone/CardDB.lua
    python tools/carddata/gen_addon_carddb.py --check   # exit 1 if the file on disk is stale

Reads only data/cards/cards.normalized.json (through cardlib). Deterministic.

The output is a Lua 5.0 chunk (WoW 1.12): a single global table ``HS_CardDB``. It only uses
double-quoted strings with the escapes \\\\ \\" \\n \\r and 3-digit decimal \\ddd (Lua 5.0 has no
\\x and no \\z), no long strings, no '#', no varargs.

Text transform (``display_text``), applied to card text and names for the client only. The
server tables keep the raw text from the dataset.
  * <b>..</b>  -> |cffffd100..|r  (WoW gold, for keywords like Taunt)
  * <i>..</i>  -> |cffb8b8b8..|r  (light grey, for reminder text)
  * any other tag is dropped; a literal '|' is doubled to '||'
  * $n (spell damage marker) and #n (heal marker) -> n
  * [x] layout markers removed
  * typographic quotes/dashes/ellipsis -> ASCII (the 1.12 fonts lack some of them)
  * runs of blanks collapse to one space, blanks around newlines are removed, empty lines and
    leading/trailing blanks are dropped; a newline in the source stays a real newline ("\\n")
  To get plain text in Lua: gsub("|c%x%x%x%x%x%x%x%x", ""), then gsub("|r", "").

Entities kept: every type except ENCHANTMENT, i.e. collectible cards (standard, reward_only and quest),
heroes, hero powers and the non-collectible tokens/generated minions, spells and weapons (including the
Naxxramas boss heroes, hero powers and encounter cards). The enchantments are only buff descriptions the server needs; they are left out of the client file
(add ENCHANTMENT to INCLUDED_TYPES when buff tooltips are needed). Also left out, because they never
appear in the released game: acquisition "unreleased" cards (none at the moment) and the tokens listed in
``cardlib.UNSUMMONED_TOKENS`` (the Skeleton no card creates). They stay in the dataset and the SQL.
"""

from __future__ import annotations

import re
import sys

import cardlib
import gencommon as gc

OUT_PATH = gc.REPO_ROOT / "addon" / "Hearthstone" / "CardDB.lua"

INCLUDED_TYPES = ("MINION", "SPELL", "WEAPON", "HERO", "HERO_POWER")
BROWSE_TYPES = ("MINION", "SPELL", "WEAPON")
BROWSE_ACQUISITION = ("standard", "reward_only", "quest")

BOLD_OPEN = "|cffffd100"
ITALIC_OPEN = "|cffb8b8b8"
COLOR_CLOSE = "|r"

_TYPOGRAPHY = {
    "‘": "'", "’": "'", "‚": "'", "‛": "'",
    "“": '"', "”": '"', "„": '"',
    "–": "-", "—": "-", "−": "-",
    "…": "...", " ": " ",
}
_BOLD_RE = re.compile(r"<b>(.*?)</b>", re.S)
_ITALIC_RE = re.compile(r"<i>(.*?)</i>", re.S)
_TAG_RE = re.compile(r"</?[a-zA-Z][^>]*>")
_MARKER_RE = re.compile(r"[$#](\d+)")


def display_text(raw) -> str:
    """Convert dataset markup to client display text (see module docstring)."""
    if not raw:
        return ""
    t = raw.replace("[x]", "").replace("\r", "")
    t = t.replace("|", "||")
    for src, dst in _TYPOGRAPHY.items():
        t = t.replace(src, dst)
    t = _MARKER_RE.sub(r"\1", t)
    t = _BOLD_RE.sub(lambda m: BOLD_OPEN + m.group(1) + COLOR_CLOSE, t)
    t = _ITALIC_RE.sub(lambda m: ITALIC_OPEN + m.group(1) + COLOR_CLOSE, t)
    t = _TAG_RE.sub("", t)
    lines = [re.sub(r"[ \t]+", " ", line).strip() for line in t.split("\n")]
    return "\n".join(line for line in lines if line)


def lua_str(value: str) -> str:
    """Quote ``value`` as a Lua 5.0 double-quoted string (ASCII output only)."""
    out = ['"']
    for ch in value:
        o = ord(ch)
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\r":
            out.append("\\r")
        elif 0x20 <= o < 0x7F:
            out.append(ch)
        else:
            out.extend("\\%03d" % b for b in ch.encode("utf-8"))
    out.append('"')
    return "".join(out)


def _class_index(card_class: str) -> int:
    try:
        return gc.CLASS_ORDER.index(card_class)
    except ValueError:
        raise gc.GeneratorError(f"unknown card class {card_class!r}: extend gencommon.CLASS_ORDER")


def included(card: dict) -> bool:
    return card["type"] in INCLUDED_TYPES and cardlib.client_visible(card)


def browse_order(cards: list) -> list:
    """Collection browsing order: class order, then cost, then name, then id."""
    rows = [c for c in cards
            if c["type"] in BROWSE_TYPES and c["acquisition"] in BROWSE_ACQUISITION and c["collectible"]]
    rows.sort(key=lambda c: (_class_index(c["class"]), c["cost"] if c["cost"] is not None else 0,
                             display_text(c["name"]).lower(), c["id"]))
    return [c["id"] for c in rows]


def card_entry(card: dict) -> str:
    fields = []

    def add(key, value):
        if value is None:
            return
        fields.append(f"{key}={value if isinstance(value, int) else lua_str(value)}")

    add("dbf", card["dbf_id"])
    add("name", display_text(card["name"]))
    text = display_text(card["text"])
    add("text", text if text else None)
    add("set", card["set"])
    add("class", card["class"])
    add("rarity", card["rarity"])
    add("type", card["type"])
    add("cost", card["cost"])
    add("atk", card["attack"])
    add("hp", card["health"])
    add("dur", card["durability"])
    add("race", card["race"])
    if card["mechanics"]:
        fields.append("mech={" + ",".join(lua_str(m) for m in card["mechanics"]) + "}")
    add("power", card["hero_power"])
    add("coll", 1 if card["collectible"] else 0)
    add("acq", card["acquisition"])
    return "{ " + ", ".join(fields) + " }"


def render(ds: dict) -> str:
    cards = [c for c in gc.sorted_cards(ds) if included(c)]
    order = browse_order(cards)
    lines = [
        "-- GENERATED by tools/carddata/gen_addon_carddb.py from data/cards/cards.normalized.json.",
        "-- Do not edit by hand: run `python tools/carddata/generate_all.py` after the card dataset changes.",
        "-- Lua 5.0 (WoW 1.12). Card text uses WoW color codes (|cffffd100 keyword, |cffb8b8b8 reminder, |r).",
        "",
        "HS_CardDB = {",
        f"    datasetId = {lua_str(ds['dataset_id'])},",
        f"    hash = {lua_str(gc.wire_hash(ds))},",
        f"    fullHash = {lua_str(ds['content_hash'])},",
        f"    schema = {ds['schema_version']},",
        f"    datasetCount = {ds['card_count']},",
        f"    count = {len(cards)},",
        "    classes = { " + ", ".join(lua_str(c) for c in gc.CLASS_ORDER) + " },",
        "    cards = {",
    ]
    for c in cards:
        lines.append(f"        [{lua_str(c['id'])}] = {card_entry(c)},")
    lines.append("    },")
    lines.append("    order = {")
    for i in range(0, len(order), 6):
        lines.append("        " + ", ".join(lua_str(x) for x in order[i:i + 6]) + ",")
    lines.append("    },")
    lines.append("}")
    lines.append("")
    return "\n".join(lines)


def main(argv) -> int:
    ds = gc.load()
    ok = gc.check_or_write(OUT_PATH, render(ds), "--check" in argv, gc.relpath(OUT_PATH))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(gc.main_wrapper(main))
