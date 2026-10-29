# Provenance of the raw Battlegrounds data

Dataset: `bg-148526` - Hearthstone Battlegrounds as of game patch **24.2** (released 2022-08-30), HearthstoneJSON
build **148526** (hsdata tag date 2022-08-31, the first build after the release; build 148211 of 2022-08-26 has the
same pool flags). Retrieved **2026-10-17**. Used by the Battlegrounds mode (`src/battlegrounds`); it is independent of
the constructed dataset `hs-6024-basic-classic`.

All card data is copyright Blizzard Entertainment (all rights reserved). The file below was extracted from the
game client by third-party tooling (HearthSim / HearthstoneJSON). It is kept for private, local, non-commercial use
only and must not be redistributed. Not affiliated with Blizzard.

The importer (`tools/battlegrounds/import_bg.py`) refuses to run if either file differs from the sha256 below.
`data/battlegrounds/.gitattributes` marks them `-text` so that line-ending conversion cannot change them. Like the
constructed source they are not stored in git (`data/battlegrounds/.gitignore`); fetch them with
`python -I tools/battlegrounds/fetch_bg_source.py`.

## Inputs

### cards.enUS.148526.json
* URL: https://api.hearthstonejson.com/v1/148526/enUS/cards.json
* Build: HearthstoneJSON 148526 (game patch 24.2, hsdata tag date 2022-08-31; patch released 2022-08-30)
* Retrieved: 2026-10-17
* Size: 6016336 bytes
* sha256: `8e849ff5d91b8a4218cb93363e178040e1676bdb3b9f362b2a2f08a6e42cef90`
* Licence: HearthstoneJSON packaging is CC0; the card data inside is Copyright (c) Blizzard Entertainment.
* 23465 entities, English only, all game modes. Battlegrounds content is flagged with `isBattlegroundsPoolMinion`,
  `battlegroundsHero`, `battlegroundsPremiumDbfId` and type `BATTLEGROUND_QUEST_REWARD`.

### CardDefs.148526.xml
* URL: https://raw.githubusercontent.com/HearthSim/hsdata/714a8780ea5f9c32b635d3f44f48596713b10b18/CardDefs.xml
* Commit: HearthSim/hsdata `714a8780ea5f9c32b635d3f44f48596713b10b18` (annotated tag `148526`, tag object `c840e179e1bd55f9d97eb1333b5589128f3061dc`, "Patch 24.2.0.148526", 2022-08-31); the same build as the JSON
* Retrieved: 2026-10-17
* Size: 67948443 bytes (all locales, all game modes)
* sha256: `f40e46e74b20de9ca564e5d0f10914e47dcbb9180a7410b9533680d88c407cd5`
* Licence: hsdata has no licence file; the content is extracted Blizzard client data (private local use). Not stored in git, like the JSON.
* Used only for the hero to hero power link: tag `HERO_POWER` (enumID 380) on the hero entities.

## What the files do not contain

HearthstoneJSON has no hero to hero power field (CardDefs.xml supplies it), no quest to reward field, no token list
per card (no `entourage`). The importer derives the card to token links from ids and texts and lists the rest in
its console summary.
The 24.2.1 hotfix (build 149417, 2022-09-07) temporarily removed the quest Witness Protection and the reward
Another Hidden Body; this build predates it, so both are present.
