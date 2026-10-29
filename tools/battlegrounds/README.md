# Battlegrounds data pipeline

Turns the pinned HearthstoneJSON build 148526 (Battlegrounds patch 24.2, released 2022-08-30) into one normalized
dataset, `data/battlegrounds/bg.normalized.json`. It is
separate from the constructed pipeline (`tools/carddata`, dataset `hs-6024-basic-classic`), which it neither imports nor
changes. The C++ card table is generated from the JSON (see "C++ card table"). Python 3, standard library only; run everything with
`python -I` from the repository root (`modules/mod-hearthstone`).

## Commands

| Task | Command |
|---|---|
| Download the pinned sources (not in git; cards.json 6 MB, CardDefs.xml 68 MB) | `python -I tools/battlegrounds/fetch_bg_source.py` |
| Regenerate the dataset and the report | `python -I tools/battlegrounds/import_bg.py` |
| Check the files on disk equal a fresh build | `python -I tools/battlegrounds/import_bg.py --check` |
| Regenerate the engine's C++ card table | `python -I tools/battlegrounds/gen_bg_cpp.py` |
| Regenerate the addon's card table (Lua) | `python -I tools/battlegrounds/gen_bg_lua.py` |
| Run the tests | `python -I -m unittest discover -s tools/battlegrounds -v` (or `python -I tools/battlegrounds/test_import.py`) |

Fresh clone: fetch, then import. Both source files and the generated JSON are Blizzard data and are git-ignored
(`data/battlegrounds/.gitignore`), like `data/cards`; `source/PROVENANCE.md` is committed.
The download is verified against the sha256 pinned in `tools/battlegrounds/bglib.py`; a mismatch writes nothing
(fetch) or exits with code 2 (import).

## Dataset layout

One flat list `cards`, sorted by role then id, every record with the same keys in the same order:
`id, dbf_id, name, role, type, source_set, slice, tier, cost, attack, health, tribes, mechanics, referenced_tags,
text, golden, tokens, referenced_by, hero_power, extra_hero_powers, hero_ids`. Not applicable fields are `null` or
`[]`. Text keeps the original markup. Tribes are the data's race names (`BEAST`, `MECHANICAL`, ...); `ALL` is Nightmare
Amalgam.

| `role` | Content |
|---|---|
| `pool_minion` | the 167 `isBattlegroundsPoolMinion` cards; `golden` holds the golden card's id, dbf id, stats, mechanics, text; `tokens` the entities it summons or generates |
| `token` | those entities (minions and spells), `referenced_by` lists their owners |
| `hero` | the 86 non-skin heroes; `hero_power` and `extra_hero_powers` |
| `hero_power` | hero powers matched to heroes, `hero_ids` back-links |
| `quest`, `quest_reward` | the 15 `BG24_Quest_*` cards, the 35 `BATTLEGROUND_QUEST_REWARD` cards |

`slice` is `"15A"` or `null` (the value still names the first slice; it is the table's inclusion flag). A minion is in
when its tribe is in `PLAYABLE_TRIBES` (15E batch 4: Beast, Demon, Dragon, Elemental, Mech, Murloc, Naga, Pirate,
Quilboar), or it is Nightmare Amalgam (`ALL`), or (tribeless) its text and tokens need no system the slice lacks;
tokens follow their owners, the hero tokens and spells in `HERO_TOKEN_LINKS` too. Spells (Gold Coin, Bananas, Blood
Gem, the seven Spellcraft spells, Darkgaze Elder's mass gem) are `token` records of type `SPELL`; a Spellcraft spell
takes its source minion's Tier, the other spells are Tier 0. Heroes, hero powers, quests and rewards carry no slice.

## Links the data does not give

HearthstoneJSON has no card to token, quest to reward or hero to hero power fields. Hero powers come from hsdata
`CardDefs.xml` (tag 380 `HERO_POWER` on the hero entity; all 86 heroes have one); `extra_hero_powers` are the powers
whose id extends the main one (ranks, targets). The old id-convention guess is kept only as a cross-check and the
importer's console summary shows where it disagrees (it was wrong for 31 heroes). Tokens are derived by id prefix, by
the Blood Gem and Gold Coin keyword entities and by `A/H Name` mentions in the text. Everything it cannot link is
printed in the console summary (never guessed). Override single tribeless cards in `bglib.TRIBELESS_OVERRIDES`.

## Reproducibility

The importer reads only `data/battlegrounds/source/*` (both files sha256-pinned in `bglib.py`). The output has no timestamps, sorted cards, sorted lists and
fixed key order, so two runs give byte-identical files (`import_bg.py --check` and the `Reproducibility` tests).
`content_hash` is the sha256 of the canonical (sorted-key, compact) JSON of the card list.

## C++ card table

`gen_bg_cpp.py` turns the JSON into `src/battlegrounds/HsBgCardData.inc`, the table the engine (`src/battlegrounds`,
namespace `Hs::Bg`) compiles in, so the server build needs neither the JSON nor the world database. The file is
committed (like `tests/match_all_cards.h`); stats only, no names and no card text.

| Task | Command |
|---|---|
| Regenerate the table | `python -I tools/battlegrounds/gen_bg_cpp.py` |
| Check the committed file equals a fresh build | `python -I tools/battlegrounds/gen_bg_cpp.py --check` |
| Run the generator tests | `python -I -m unittest discover -s tools/battlegrounds -v` (or `python -I tools/battlegrounds/test_gen.py`) |

One row per pool minion (167), per included minion token (18) and per included spell (11), in dataset order: id, dbf id,
tier, attack, health, tribe bits, keyword bits, golden id, golden dbf id, golden attack, golden health, golden keyword
bits, flag, role. The row index is the engine's card index and changes with the dataset, so nothing may store
it. Rules of the mapping:

- Keywords: Taunt, Divine Shield, Poisonous, Windfury, Reborn from the mechanics; Magnetic from `MODULAR`. A golden
  Windfury is `kKwMegaWindfury` (the four golden cards say Mega-Windfury in their text; the generator fails otherwise).
- The data's mechanics miss some keywords, so the bold keywords a text starts with count too. This adds Taunt to the
  token Half-Shell and to Sparring Partner and BGS_060, and Divine Shield to Deflect-o-Bot (and to their goldens). A
  keyword later in the text ("Summon a Turtle with Taunt", "Give a minion Taunt") does not count.
- Tribes: the data's races as bits, `ALL` (Nightmare Amalgam) as its own bit that matches every tribe.
- Frenzy, Avenge, Deathrattle, Battlecry and auras are not table data: they are effects (`HsBgEffects.h`).
- Tokens are tier 1. Only the included minion tokens are in the minion part of the table; the spell cards have
  `CardRole::Spell`, tier 0 (Gold Coin, Bananas) or the source minion's Tier (Spellcraft), attack / health 0, and no
tribe or keyword bits.

## Addon card table

`gen_bg_lua.py` turns the same JSON into `addon/Hearthstone/Battlegrounds/BgCardDB.lua` (global `HS_BgCardDB`), the
addon's tooltip and art side of the table. Unlike the C++ table this one carries the names and the display text. It is
git-ignored and rebuilt by `tools/setup_all.py`, like `CardDB.lua`.

| Task | Command |
|---|---|
| Regenerate the table | `python -I tools/battlegrounds/gen_bg_lua.py` |
| Check the file equals a fresh build | `python -I tools/battlegrounds/gen_bg_lua.py --check` |
| Run the generator tests | `python -I -m unittest discover -s tools/battlegrounds -v` (or `python -I tools/battlegrounds/test_lua.py`) |

One entry per selected card (all 167 pool minions, the 21 included minion tokens and the 22 spell cards, the same
selection and order as the C++ table) and one per golden twin (397 entries in all): `dbf`, `name`, `text`, `tier`, `atk`,
`hp`, `kw` (the keyword icon names), `tribes`, `spell` 1 on a spell, and `golden` (the golden id, on a normal card),
`token` 1 on a minion token and `g` 1 on a golden. A golden minion is rendered from `cards[cards[id].golden]`, which
carries its own (doubled) stats and text. The keyword rule is the one of the C++ table (a test keeps the two totals equal).
