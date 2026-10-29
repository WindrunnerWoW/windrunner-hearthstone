# Card overrides

Versioned, sourced corrections applied by `tools/carddata/import_cards.py` on top of the pinned
Hearthstone build 6024 data. Build 1.1.0.6024 (2014-07-22, the Curse of Naxxramas launch) is the snapshot
chosen by the owner on 2026-10-11 for the whole dataset (it replaced build 5834), so no patch-note back-porting is
needed; its Basic/Classic wording changes against 5834 (Ancestral Spirit and Eaglehorn Bow rules, four wording-only
changes) are accepted as they are. Later changes (6485 Leeroy Jenkins / Starving Buzzard, 7234 Flare / Gadgetzan
Auctioneer / Soulfire) stay absent. This directory exists so that any deviation from the snapshot is explicit and reviewable rather than a
silent edit of the source data.

The Naxxramas (`NAXX`) set of the same build is part of the dataset since 2026-10-11 and needs no override either.

Currently there is **one** override file: `001_trigger_visual.json` (2026-10-13, owner request). It adds the display
tag `TRIGGER_VISUAL` to the 53 minions (including a few tokens) whose text describes a triggered effect ("Whenever ...",
"After you ...", "At the start / end of ..."; Battlecry cards are left out). Later builds carry this tag, build 6024 does
not. It only drives the lightning bolt the board shows on a minion (`MU.MechanicKeys` in `MatchUI.lua`); rules text and
the match engine ignore it. Add a card to the file (or remove it) to change which minions show the lightning. The file
was generated once from the card text; new cards need an entry by hand.

(Until session 14 `001_adrenaline_rush_unreleased.json` set `acquisition: "unreleased"`
on `NEW1_006` Adrenaline Rush; the card is dropped from the dataset altogether now, `cardlib.REMOVED_IDS`.) The
reason, kept for reference: build 6024 still flags it collectible, but it was removed in the alpha (replaced
by Shiv) and was never obtainable in the released game; with it excluded the Classic set has the
historical 245 collectible cards (Blizzard's Classic-format `VAN_` set: 382 = 378 standard + 4 reward-only).
The file records the sources and reasoning.

## Format

Every `*.json` file in this directory is loaded in filename order (use a numeric prefix such as
`001_reason.json`). Each file:

```json
{
  "schema_version": 1,
  "overrides": [
    {
      "card_id": "EX1_116",
      "fields": { "cost": 5 },
      "expect": { "cost": 4 },
      "source_url": "https://...",
      "reason": "Why the value differs from the snapshot, with patch/date."
    }
  ]
}
```

* `card_id`, `fields`, `source_url` (http/https) and `reason` are required.
* `fields` may only contain overridable card fields (everything except `id`, `dbf_id`, `origin`,
  `included_by`). `acquisition` (`standard`, `reward_only`, `none`, `unreleased`) is overridable, but the four
  reward-only ids are pinned by the validator, so change `cardlib.REWARD_ONLY_IDS` for those.
  `unreleased` (flagged collectible in the build but never obtainable: not in packs, crafting or rewards,
  not shown in the collection) is only valid together with an override like the one above and an entry
  in `snapshot_expectations.EXPECTED_UNRELEASED`. Use the normalized names and value types (`cost`, `text`, `mechanics`, ...).
* `expect` (optional but recommended) lists the value each field must currently have. If the dataset
  no longer matches, the import fails ("stale override"), so overrides cannot silently outlive a
  source change.
* A field of a card may be overridden only once across all files.
* Applied overrides are recorded in `cards.normalized.json` under `overrides_applied` (file, file
  sha256, card, fields, source URL) and change `content_hash`.
* Never mix contemporary (post-July 2014) values in without an override that says so.
* Adding a brand new card is not an override; use the custom namespace below.

## Custom cards (reserved namespace)

Custom cards live in `data/cards/custom/*.json` (directory created when the first one is added):

```json
{ "schema_version": 1, "cards": [ { "id": "WR_0001", "dbf_id": 10000001, "name": "...", "set": "WR_CUSTOM",
  "type": "MINION", "rarity": "COMMON", "cost": 1, "attack": 1, "health": 1 } ] }
```

Rules, enforced by the importer and the validator:

* `id` must start with `WR_` (no Blizzard id does).
* `dbf_id` must be an integer `>= 10,000,000` (Blizzard dbf ids are far below this).
* Keys are the same as a normalized card (see `tools/carddata/README.md`); omitted keys take defaults.
  `acquisition` defaults to `standard` for a collectible minion/spell/weapon and `none` otherwise; set
  `reward_only` to keep a custom card out of packs and crafting.
* Custom cards get `origin: "custom"` and never overwrite or renumber Blizzard cards.
