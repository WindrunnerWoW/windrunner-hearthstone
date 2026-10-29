# Provenance of the raw card data

Dataset: `hs-6024-basic-classic` - Hearthstone client build **1.1.0.6024** (patch of 22 July 2014), the Curse of
Naxxramas launch build. Retrieved **2026-10-11**. Owner decision of 2026-10-11: the whole dataset uses build 6024
(it replaced build 5834, which was the last build before Naxxramas). The Basic (`CORE`), Classic (`EXPERT1`) and
Curse of Naxxramas (`NAXX`) sets are imported; the dataset id keeps its name `hs-6024-basic-classic`.

All card data is copyright Blizzard Entertainment (all rights reserved). The files below were extracted
from the game client by third-party tooling (HearthSim / HearthstoneJSON). They are kept for private,
local, non-commercial use only and must not be redistributed. Not affiliated with Blizzard.

The importer (`tools/carddata/import_cards.py`) refuses to run if either committed file differs from the
sha256 below. `.gitattributes` marks these files `-text` so that line-ending conversion cannot change them.

## Committed inputs

### cards.enUS.6024.json
* URL: https://api.hearthstonejson.com/v1/6024/enUS/cards.json
* Build: 1.1.0.6024 (HearthstoneJSON build 6024, generated from the client, regenerated 2016)
* Retrieved: 2026-10-11
* Size: 238448 bytes
* sha256: `bc24fb05c433ced4f32e491fa8d7fe7babfc2f1386e6e1b7a0c41c3e89fff604`
* Licence: HearthstoneJSON packaging is CC0; the card data inside is Copyright (c) Blizzard Entertainment.
* 882 entities, English only, no dbf ids. Basic = set `CORE`, Classic = set `EXPERT1`, Curse of Naxxramas = set `NAXX`.

### manifest-cards.ff82e0a.csv
* URL: https://raw.githubusercontent.com/HearthSim/hsdata/ff82e0ab42e3d3f783c6a5e5fbd5c9d7bd9c8acb/manifest-cards.csv
* Commit: HearthSim/hsdata `ff82e0ab42e3d3f783c6a5e5fbd5c9d7bd9c8acb` ("Update to patch 1.1.0.6024", tag `6024`, annotated tag object `110fe363e302096a8b6a655523ce061096a310c2`, 2014-07-22)
* Retrieved: 2026-10-11
* Size: 14802 bytes (871 rows: `dbfId,CardID,flag,flag`; the last two columns are unexplained and unused)
* sha256: `0cac012d34cd9e620227e3ef22e74d6486f4e6fac2fa05fba7e2a2a6eb129c4e`
* Licence: hsdata has no licence file; the content is extracted Blizzard client data (private local use).
* Used only for `dbf_id`.

## Cross-check input (NOT committed)

### CardDefs.xml
* URL: https://raw.githubusercontent.com/HearthSim/hsdata/ff82e0ab42e3d3f783c6a5e5fbd5c9d7bd9c8acb/CardDefs.xml
* Commit: same hsdata commit as above (build 6024)
* Retrieved: 2026-10-11
* Size: 2454456 bytes (12 locales, 882 entities)
* sha256: `1c7717e9a5851b3eaeffe77cc0973305dd95feef709c639fce7247a503ca6ba3`
* Licence: as above (Blizzard client data).
* Used only by `tools/carddata/crosscheck_carddefs.py`, which verifies the hash before comparing. Download
  it to any path outside the repository to re-run the cross-check.

## Derived inputs (authored in this repository)

* `data/cards/links/hero_powers.json`: hero to hero-power links, which the JSON source lacks (class
  pairing for the nine heroes; Lord Jaraxxus from the CardDefs.xml `HERO_POWER` tag; the 30 Naxxramas boss heroes by
  their boss slot, `NAXn_01` to the hero power of the same slot, because neither the JSON nor CardDefs.xml of this
  build carries a hero-power tag on them; checked against the encounter table in `src/match/HsEncounters.cpp`).

## Selection notes

Besides all CORE/EXPERT1 entities, the four collectible PROMO/REWARD cards (EX1_062, EX1_112,
NEW1_016, PRO_001) and the entities they reference are imported with `acquisition: reward_only` /
`none`; see `tools/carddata/README.md`. This is a selection rule, not a data correction.

All 161 entities of the `NAXX` set are imported too: the 30 collectible cards
(`FP1_001`..`FP1_031`, all unchanged in every later build through 6898) get `acquisition: quest`; the 131 encounter-only
entities (15 Normal and 15 Heroic boss heroes, their hero powers, boss cards such as `NAX7_05` Mind Control
Crystal, tokens, weapons, enchantments, and `FP1_006` Deathcharger, `NAXM_001` Necroknight, `NAXM_002` Skeletal Smith
used by the boss decks) are imported with `acquisition: none`. Heroic variants (ids ending `H`, and `NAX1h_*`) are
kept so the set stays complete; they are never used by the game (no Heroic mode). Every entity a NAXX card
references (entourage, hero power) is in the dataset: the Webspinner beast pool is CORE/EXPERT1/REWARD cards
already imported. `NAX6_02e` (Necrotic Aura enchantment) has no row in `manifest-cards.csv`; it is imported with
`dbf_id: null`, like the four other dbf-less enchantments (a validator warning, not an error). This is a selection
rule, not a data correction.

## Corrections

None. Two entities of the build are dropped from the dataset (`cardlib.REMOVED_IDS`): NEW1_006 Adrenaline Rush
(still flagged collectible in the build, but removed in the closed alpha and never obtainable; sources in the git
history of `overrides/001_adrenaline_rush_unreleased.json`) and the Skeleton token EX1_016t that no card creates.
Two promo tokens are added as dependencies of their cards (PRO_001at, Mekka4t). No card value is changed.

Differences against the previous pin (build 5834), measured on the 615 selected entities (same selection, no dbf id
changed): two rule changes of the Naxxramas patch and four wording-only changes in collectible Basic/Classic cards,
plus text-only changes in non-collectible entities.

* Rule changes: CS2_038 Ancestral Spirit (now `Give a minion "<b>Deathrattle:</b> Resummon this minion."`, enchantment
  CS2_038e `<b>Deathrattle:</b> Resummon this minion.`) and EX1_536 Eaglehorn Bow (`Whenever a friendly <b>Secret</b>
  is revealed, gain +1 Durability.`).
* Wording only: NEW1_023 Faerie Dragon (`spells`), EX1_289 Ice Barrier (`When your hero is attacked`), EX1_559
  Archmage Antonidas (`add a 'Fireball' spell to your hand`), EX1_323 Lord Jaraxxus (`replace it`).
* Non-collectible: the whitespace around the newline of 11 hero-power texts (`<b>Hero Power</b> 
 Deal ...` became
  `<b>Hero Power</b>
Deal ...`), DREAM_01 Laughing Sister (`spells`), tt_010a Spellbender (text `<b></b>` became
  empty), PRO_001c Power of the Horde (play requirement `REQ_NUM_MINION_SLOTS` added).
* No cost, attack, health, rarity or mechanics change. Build 6024 already contains the pre-Naxxramas values of
  5834 (e.g. Unleash the Hounds cost 3 since patch 1.0.0.5314). Later changes (Leeroy Jenkins / Starving Buzzard in
  6485, Flare / Gadgetzan Auctioneer / Soulfire in 7234) are deliberately absent. See `overrides/README.md`.
