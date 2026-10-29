# Card data pipeline

Turns the pinned Hearthstone build 1.1.0.6024 (22 July 2014, Curse of Naxxramas launch; Basic, Classic and Naxxramas
sets) into one normalized dataset, `data/cards/cards.normalized.json`. The server SQL migration, the addon `CardDB.lua` and
the C++ `src/HsDataset.h` are generated from that file (see "Generators"). Python 3, standard library only. Run everything from the repository root (`modules/mod-hearthstone`).

## Commands

| Task | Command |
|---|---|
| Download the pinned sources (not in git) | `python tools/carddata/fetch_sources.py` (`--crosscheck` also CardDefs.xml) |
| Regenerate the dataset | `python tools/carddata/import_cards.py` |
| Check the file on disk equals a fresh build | `python tools/carddata/import_cards.py --check` |
| Validate + print totals and spot checks | `python tools/carddata/validate_cards.py` (exit 1 on hard errors) |
| Run the tests | `python -m unittest discover -s tools/carddata -v` (or `python tools/carddata/test_snapshot.py`) |
| Regenerate SQL + CardDB.lua + HsDataset.h | `python tools/carddata/generate_all.py` (`--check` to verify only) |
| Cross-check against raw CardDefs.xml | `python tools/carddata/crosscheck_carddefs.py <path to CardDefs.xml>` |

The CardDefs.xml (2.5 MB, 12 locales) is not stored in the repo; get it from the URL in
`data/cards/source/PROVENANCE.md`. The cross-check verifies its sha256 first.

## Reproducibility

The importer reads only `data/cards/source/*` (sha256-pinned in `cardlib.py`; a mismatch aborts with
exit code 2), `data/cards/links/`, `data/cards/overrides/` and `data/cards/custom/`. The output has no
timestamps, sorted cards and fixed key order, so two runs give byte-identical files (verified by
`import_cards.py --check` and by the `Reproducibility` tests). `content_hash` is the sha256 of the canonical
(sorted-key, compact) JSON of the card list; generators should copy it into their output as the shared
dataset version.

## Selection rules

Build 6024 differs from the earlier 5834 pin in two Basic/Classic rules (Ancestral Spirit: "Give a minion
Deathrattle: Resummon this minion."; Eaglehorn Bow: "friendly Secret") and four wordings (Faerie Dragon, Ice Barrier,
Archmage Antonidas, Lord Jaraxxus); details in `data/cards/source/PROVENANCE.md`. The 6485 and 7234 changes stay absent.

* All entities of sets `CORE` (Basic), `EXPERT1` (Classic) and `NAXX` (Curse of Naxxramas) are kept (`cardlib.INCLUDED_SETS`):
  the collectible cards and every non-collectible runtime entity (heroes, hero powers, tokens, enchantments,
  generated cards). `NAXX` has 161 entities: 30 collectible cards (`acquisition: quest`) and 131 encounter-only
  entities (15 Normal and 15 Heroic boss heroes `NAX*_01`, their hero powers, boss cards such as `NAX7_05`, tokens,
  weapons, enchantments, and the boss-deck minions `FP1_006` Deathcharger, `NAXM_001` Necroknight, `NAXM_002`
  Skeletal Smith) with `acquisition: none`. The Heroic variants (ids ending `H`, `NAX1h_*`) are kept for a complete
  set; the game never uses them. The 30 boss heroes are linked to their hero power in `data/cards/links/hero_powers.json`
  by boss slot (the source has no link). `NAX6_02e` (Necrotic Aura) is missing from the dbf manifest and keeps
  `dbf_id: null` like the other dbf-less enchantments (validator warning).
* Four collectible cards from other sets are also imported, keeping their original `set`: Old Murk-Eye and
  Captain's Parrot (REWARD), Gelbin Mekkatorque and Elite Tauren Chieftain (PROMO). They are listed in
  `cardlib.REWARD_ONLY_IDS` and get `acquisition: "reward_only"`: they stay out of packs and crafting and
  can only be earned later as special quest rewards. The non-collectible entities they reference
  (Mekka1-4, PRO_001a-c) come along with `included_by: "reference"`.
* Two entities of the build never existed in the released game and are dropped (`cardlib.REMOVED_IDS`, owner,
  session 14): `NEW1_006` Adrenaline Rush (flagged collectible in 6024, but removed in the closed alpha and replaced
  by Shiv) and `EX1_016t` Skeleton 3/3 (no card creates it). The importer fails if a kept card references one.
* Tokens that a card script creates but the source does not list as an entourage entry come along with their card
  (`cardlib.IMPLICIT_REFERENCES`): `PRO_001at` Murloc (I Am Murloc) and `Mekka4t` Chicken (Poultryizer), both PROMO
  set entities with `included_by: "reference"` (cost 1 and 0 as in build 6024).
* Other entities from other sets (CHEAT, MISSIONS, CREDITS, remaining PROMO, NONE) are dropped unless a
  kept card references them (entourage or hero-power link); the importer prints what was excluded.
* `collectible` is copied from the source. The nine class heroes are flagged collectible there but are not
  cards (acquisition `none`).
* `acquisition`: `standard` = normal Basic/Classic collectible (MINION/SPELL/WEAPON, set CORE/EXPERT1);
  `reward_only` = the four cards above (promo cards with their own arena milestone rules);
  `quest` = the 30 collectible Naxxramas cards, earned only through quest rewards (never in packs, never craftable or
  scrappable, never offered by the daily card trader, not in arena drafts for now; usable in decks once owned; see
  `src/match/HsEncounters.cpp`); `none` = every non-collectible runtime entity and the heroes;
  `unreleased` = flagged collectible in the build but never obtainable in the released game, set by a sourced
  override (not used by any card at the moment: Adrenaline Rush is dropped instead, see below): not in packs,
  crafting or rewards, not shown in the collection, kept in the dataset and the SQL. Pack, crafting and
  starter-collection logic must use only
  `standard` (a `quest` card is never a pack or crafting candidate); the 378-card catalog total (CORE 133, EXPERT1 245, matching the historical Classic set) is the
  `standard` count.

## Schema (dataset_id `hs-6024-basic-classic`, schema_version 1)

Top level: `dataset_id`, `schema_version`, `source` (build, hsdata commit, file URLs/sizes/sha256, CardDefs
cross-check pin), `sets_included`, `hero_power_links`, `overrides_applied`, `custom_files`, `excluded`,
`card_count`, `content_hash`, `cards`.

Every card has all of these keys (null / empty when absent):
`id`, `dbf_id`, `name`, `text` (raw English with `<b>`/`<i>` markup and `$n` spell-damage markers),
`collectible`, `set`, `class` (NEUTRAL when the source has none; DREAM for Dream cards), `rarity`
(FREE kept as-is), `type` (MINION/SPELL/WEAPON/HERO/HERO_POWER/ENCHANTMENT), `cost`, `attack`, `health`
(heroes: starting health), `durability`, `race`, `mechanics` (sorted, source names), `faction`,
`spell_damage`, `overload`, `entourage` (ids of cards this can generate), `hero_power` (hero -> hero-power id,
from `data/cards/links/hero_powers.json`), `play_requirements` (source targeting flags), `text_in_play`,
`targeting_arrow_text`, `acquisition` (`standard`, `reward_only`, `quest`, `none`, `unreleased`), `origin` (`blizzard` or
`custom`), `included_by` (`set`, `reward`, `reference` or `custom`). Top level also has `reward_only_ids`.

Importing a card does not mean its gameplay is implemented.

## Overrides and custom cards

See `data/cards/overrides/README.md`. Overrides cite a source URL and reason and are recorded in the
dataset metadata; there is currently none. Custom cards use ids starting `WR_` and dbf ids >= 10,000,000
and live in `data/cards/custom/`.

## Generators

`python tools/carddata/generate_all.py [--check]` rewrites (or verifies) all three outputs from the same
normalized file. They are deterministic (byte-identical on re-run) and carry the same dataset id and hash:

| Output | Generator | Content |
|---|---|---|
| `src/HsDataset.h` | `scripts/gen_dataset_header.py` | id, 16-hex wire hash, full hash, card count (WELCOME) |
| `data/sql/world/0001_hearthstone_card_template.sql` | `gen_world_sql.py` | tables `hearthstone_dataset` and `hearthstone_card_template`, all cards (776 rows, batched INSERTs of 25) |
| `addon/Hearthstone/CardDB.lua` | `gen_addon_carddb.py` | global `HS_CardDB` (Lua 5.0), client subset |

Shared helpers are in `gencommon.py`. After changing the dataset (or a generator) run `generate_all.py` and commit
all three outputs together; the tests fail when any of them is stale.

### World SQL migration and dataset versions

The core's AutoUpdater applies `modules/<module>/data/sql/world/*.sql` at startup, keyed by module name plus the
SHA1 of the file bytes (the file name is not the key). It splits statements on `;` outside quotes, turns newlines
into spaces, strips comments, and treats a quote as escaped when the previous character is a backslash. Consequences:

* The migration is one self-contained file, `0001_hearthstone_card_template.sql` (zero-padded counter like the other
  modules; alphabetical order matters only when several files exist). Both tables are derived data, so the file
  does `DROP TABLE IF EXISTS`, `CREATE TABLE`, `INSERT ...`. It is idempotent and does not depend on the run order
  of other files, so an ALTER is never needed when the columns change.
* Shipping a new dataset version = regenerate the same file. The new bytes have a new SHA1, so the updater runs it
  again on the next startup and replaces the rows (`hearthstone_dataset` names the dataset id, content hash, card
  count and schema version). No new file is created per dataset, so the repository does not accumulate copies. The
  old hash stays in the `migrations` table and is logged once as "exists in DB but not as file"; that is harmless.
  Only add a new numbered file (`0002_...`) for something else, such as spawning an NPC.
* The generator never writes a backslash: quotes are doubled, and newlines/backslashes are built with
  `CHAR(n USING utf8mb4)` inside `CONCAT(...)`. That avoids the updater's previous-character quote heuristic and does
  not depend on `NO_BACKSLASH_ESCAPES`. Statements stay under ~15 KB. Non-BMP characters are rejected (the core
  connection is utf8/utf8mb3); the tables are `utf8mb4`. The server text keeps the raw markup of the dataset.
* At startup the module compares `hearthstone_dataset` with `src/HsDataset.h` and logs an error line if the table
  is missing or differs.

Column `acquisition` is an ENUM (`standard`, `reward_only`, `quest`, `none`, `unreleased`). The primary key is `(dataset_id, id)`.

### CardDB.lua

Global `HS_CardDB`: `datasetId`, `hash` (the same 16 hex as WELCOME), `fullHash`, `schema`, `datasetCount` (all
cards of the dataset), `count` (cards in this file, 634), `classes` (browse order), `cards` (id -> entry) and `order`
(412 collectible ids: standard, reward_only and quest cards, class order, then cost, then name). Entry keys: `dbf name text set class rarity type cost atk
hp dur race mech power coll acq`; absent values are simply omitted. Heroes carry `power` (their hero power id) and
`hp` (starting health).

Entities kept (`cardlib.client_visible`): everything except `ENCHANTMENT`, that is the collectible cards (standard,
reward_only and quest), the heroes, the hero powers, and the non-collectible tokens/generated minions, spells and
weapons, including the Naxxramas boss heroes, hero powers and encounter cards. The 141 enchantments only describe
buffs for the server and stay in the SQL; add `ENCHANTMENT` to `INCLUDED_TYPES` in `gen_addon_carddb.py` if buff
tooltips are needed later. The collection browser lists `acq` standard, reward_only and quest cards (quest and
reward_only cards show a quest-reward marker, also before they are owned). File size is about 140 KB.

Text transform for display (`display_text`, applied to name and text; the SQL keeps the raw text):

* `<b>..</b>` becomes `|cffffd100..|r` (keywords), `<i>..</i>` becomes `|cffb8b8b8..|r` (reminder text);
  other tags are dropped, a literal `|` is doubled;
* `$3` (spell damage marker) and `#4` (heal marker) become plain `3` and `4`; `[x]` markers are removed;
* typographic quotes/dashes/ellipsis become ASCII;
* blanks collapse, blanks around newlines are removed, empty lines are dropped, a source newline stays a newline.

Plain text in Lua: `gsub(text, "|c%x%x%x%x%x%x%x%x", "")` and then `gsub(text, "|r", "")`.

The addon compares `HS_CardDB.hash`/`datasetId` with the WELCOME `hash`/`dataset` and prints a "Card data
mismatch" error when they differ (`/hs` shows the result). The output uses only Lua 5.0 syntax: double-quoted
strings with the escapes `\\`, `\"`, `\n`, `\r` and 3-digit decimal `\ddd`, no `#`, no long strings.

## Files

* `cardlib.py`: schema, pins, loading, normalization, overrides, custom cards (shared by everything)
* `import_cards.py`, `validate_cards.py`, `crosscheck_carddefs.py`: the three importer CLIs
* `gencommon.py`, `gen_world_sql.py`, `gen_addon_carddb.py`, `generate_all.py`: the generators
* `test_generators.py`: generator tests (determinism, escaping, updater-splitter port, Lua round trip)
* `snapshot_expectations.py`: historic spot-check values used by the validator and the tests
* `test_snapshot.py`: unit tests
