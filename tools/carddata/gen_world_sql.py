#!/usr/bin/env python3
"""Generate the world-database migration with the card definitions.

Usage (from the module root):
    python tools/carddata/gen_world_sql.py           # write data/sql/world/0001_hearthstone_card_template.sql
    python tools/carddata/gen_world_sql.py --check   # exit 1 if the file on disk is stale

Reads only data/cards/cards.normalized.json (through cardlib). Deterministic: identical bytes
on every run. See tools/carddata/README.md for the migration and dataset-versioning design.

The core's AutoUpdater splits a migration file on ';' outside quotes, replaces newlines by spaces,
strips comments, and decides "is this quote escaped" by looking at the previous character. The
emitted SQL therefore contains NO backslashes at all: quotes are doubled, and newlines/backslashes
are produced with CHAR(n USING utf8mb4) inside CONCAT(). That also makes it independent of
sql_mode NO_BACKSLASH_ESCAPES.
"""

from __future__ import annotations

import sys

import gencommon as gc

MIGRATION_NAME = "0001_hearthstone_card_template.sql"
OUT_PATH = gc.REPO_ROOT / "data" / "sql" / "world" / MIGRATION_NAME
BATCH_ROWS = 25          # rows per INSERT; ~10 KB per statement

# (column, SQL type). The order is the INSERT column order.
COLUMNS = (
    ("dataset_id", "VARCHAR(64) NOT NULL"),
    ("id", "VARCHAR(32) NOT NULL"),
    ("dbf_id", "INT UNSIGNED NULL"),
    ("name", "VARCHAR(64) NOT NULL"),
    ("text", "TEXT NULL"),
    ("card_set", "VARCHAR(16) NOT NULL"),
    ("card_class", "VARCHAR(16) NOT NULL"),
    ("rarity", "VARCHAR(16) NULL"),
    ("card_type", "VARCHAR(16) NOT NULL"),
    ("cost", "SMALLINT UNSIGNED NULL"),
    ("attack", "SMALLINT UNSIGNED NULL"),
    ("health", "SMALLINT UNSIGNED NULL"),
    ("durability", "SMALLINT UNSIGNED NULL"),
    ("race", "VARCHAR(16) NULL"),
    ("mechanics", "VARCHAR(255) NOT NULL DEFAULT ''"),
    ("spell_damage", "SMALLINT UNSIGNED NULL"),
    ("overload", "SMALLINT UNSIGNED NULL"),
    ("entourage", "VARCHAR(255) NOT NULL DEFAULT ''"),
    ("hero_power", "VARCHAR(32) NULL"),
    ("collectible", "TINYINT UNSIGNED NOT NULL"),
    ("acquisition", "ENUM('standard','reward_only','quest','none','unreleased') NOT NULL"),
    ("origin", "VARCHAR(16) NOT NULL"),
)
UINT_MAX = {"SMALLINT": 65535, "INT": 4294967295}

# Characters that cannot (or must not) appear literally inside a quoted string.
_SPECIAL = {"\\": 92, "\n": 10, "\r": 13, "\0": 0, "\x1a": 26}


def _special_code(ch: str):
    if ch in _SPECIAL:
        return _SPECIAL[ch]
    if ord(ch) < 0x20 and ch != "\t":
        return ord(ch)
    return None


def sql_str(value: str) -> str:
    """Quote ``value`` as a SQL string expression free of backslashes."""
    parts, buf = [], []
    for ch in value:
        if ord(ch) > 0xFFFF:
            raise gc.GeneratorError(
                f"character U+{ord(ch):X} is outside the BMP (the core connection is utf8/utf8mb3)")
        code = _special_code(ch)
        if code is None:
            buf.append(ch)
            continue
        if buf:
            parts.append("'" + "".join(buf).replace("'", "''") + "'")
            buf = []
        parts.append(f"CHAR({code} USING utf8mb4)")
    if buf or not parts:
        parts.append("'" + "".join(buf).replace("'", "''") + "'")
    return parts[0] if len(parts) == 1 else "CONCAT(" + ", ".join(parts) + ")"


def sql_val(value) -> str:
    if value is None:
        return "NULL"
    if isinstance(value, bool):
        return "1" if value else "0"
    if isinstance(value, int):
        return str(value)
    return sql_str(str(value))


def _uint(value, column: str, card_id: str, type_name: str):
    if value is None:
        return None
    if not isinstance(value, int) or value < 0 or value > UINT_MAX[type_name]:
        raise gc.GeneratorError(f"{card_id}.{column}={value!r} does not fit {type_name} UNSIGNED")
    return value


def row_values(card: dict, dataset_id: str) -> list:
    cid = card["id"]
    if len(cid) > 32 or len(card["name"]) > 64:
        raise gc.GeneratorError(f"{cid}: id/name too long for its column")
    mech = ",".join(card["mechanics"])
    entourage = ",".join(card["entourage"])
    if len(mech) > 255 or len(entourage) > 255:
        raise gc.GeneratorError(f"{cid}: mechanics/entourage list too long for its column")
    return [
        dataset_id,
        cid,
        _uint(card["dbf_id"], "dbf_id", cid, "INT"),
        card["name"],
        card["text"],
        card["set"],
        card["class"],
        card["rarity"],
        card["type"],
        _uint(card["cost"], "cost", cid, "SMALLINT"),
        _uint(card["attack"], "attack", cid, "SMALLINT"),
        _uint(card["health"], "health", cid, "SMALLINT"),
        _uint(card["durability"], "durability", cid, "SMALLINT"),
        card["race"],
        mech,
        _uint(card["spell_damage"], "spell_damage", cid, "SMALLINT"),
        _uint(card["overload"], "overload", cid, "SMALLINT"),
        entourage,
        card["hero_power"],
        1 if card["collectible"] else 0,
        card["acquisition"],
        card["origin"],
    ]


def render(ds: dict) -> str:
    dataset_id = ds["dataset_id"]
    cards = gc.sorted_cards(ds)
    if len(dataset_id) > 64:
        raise gc.GeneratorError("dataset_id too long")
    cols = ", ".join(f"`{name}`" for name, _ in COLUMNS)
    out = [
        "-- GENERATED by tools/carddata/gen_world_sql.py from data/cards/cards.normalized.json.",
        "-- Do not edit by hand: rerun `python tools/carddata/generate_all.py` after the dataset changes.",
        f"-- dataset {dataset_id}, schema_version {ds['schema_version']}, {len(cards)} cards,",
        f"-- content_hash {ds['content_hash']}",
        "--",
        "-- Idempotent and self-contained. Both tables are derived data that this file rebuilds in full",
        "-- (DROP + CREATE + INSERT), so a changed dataset or column list needs no ALTER: regenerating",
        "-- the file changes its SHA1 and the core AutoUpdater runs it again at the next startup.",
        "-- The file contains no backslashes (the updater guesses string state from the previous",
        "-- character); keep it that way, see gen_world_sql.py.",
        "",
        "DROP TABLE IF EXISTS `hearthstone_card_template`;",
        "DROP TABLE IF EXISTS `hearthstone_dataset`;",
        "",
        "CREATE TABLE `hearthstone_dataset` (",
        "  `dataset_id` VARCHAR(64) NOT NULL,",
        "  `content_hash` CHAR(64) NOT NULL,",
        "  `card_count` INT UNSIGNED NOT NULL,",
        "  `schema_version` INT UNSIGNED NOT NULL,",
        "  PRIMARY KEY (`dataset_id`)",
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Card dataset version shipped with mod-hearthstone';",
        "",
        "CREATE TABLE `hearthstone_card_template` (",
    ]
    for name, typ in COLUMNS:
        out.append(f"  `{name}` {typ},")
    out += [
        "  PRIMARY KEY (`dataset_id`, `id`),",
        "  KEY `idx_dbf_id` (`dbf_id`),",
        "  KEY `idx_browse` (`dataset_id`, `acquisition`, `card_class`, `cost`)",
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Card definitions of the Hearthstone dataset';",
        "",
        "INSERT INTO `hearthstone_dataset` (`dataset_id`, `content_hash`, `card_count`, `schema_version`) VALUES",
        f"({sql_str(dataset_id)}, {sql_str(ds['content_hash'])}, {len(cards)}, {ds['schema_version']});",
        "",
    ]
    for start in range(0, len(cards), BATCH_ROWS):
        chunk = cards[start:start + BATCH_ROWS]
        out.append(f"INSERT INTO `hearthstone_card_template` ({cols}) VALUES")
        rows = ["(" + ", ".join(sql_val(v) for v in row_values(c, dataset_id)) + ")" for c in chunk]
        out.append(",\n".join(rows) + ";")
        out.append("")
    return "\n".join(out)


def main(argv) -> int:
    ds = gc.load()
    ok = gc.check_or_write(OUT_PATH, render(ds), "--check" in argv, gc.relpath(OUT_PATH))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(gc.main_wrapper(main))
