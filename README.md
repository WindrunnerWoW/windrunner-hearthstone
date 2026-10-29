# mod-hearthstone

Single-player Hearthstone-style card game for the Windrunner (TurtleWoW 1.12 era) server: a
server module plus a client addon. You collect Basic and Classic cards, build decks, and play
matches against NPCs and bosses that you start through their gossip menus. There is no PvP.

The wire protocol is specified in [`docs/PROTOCOL.md`](docs/PROTOCOL.md).

## Requirements

- The Windrunner server tree this module belongs to (a Tortoise-WoW 1.18.1 fork; its
  `INSTALL-WINDOWS.md` covers the compiler, vcpkg, the databases and a first start).
- A 1.12-family client (Interface 11200), Python 3 and PowerShell.
- The card data and card pictures are Blizzard's and are not in this repository. The setup
  script downloads and generates them; see
  [`tools/art/README.md`](tools/art/README.md) for licensing and redistribution.

## Getting started

1. **Get the module** into the server tree (it is its own git repository, like the other
   modules):

   ```
   git clone https://github.com/WrkX/windrunner-hearthstone <server>/modules/mod-hearthstone
   ```

2. **Generate the card data** (needs internet once). This downloads the pinned Hearthstone
   1.1.0.6024 sources, checks their sha256, and generates `src/HsDataset.h` (compiled into the
   server), the world SQL template and `addon/Hearthstone/CardDB.lua`:

   ```
   python tools/setup_all.py --cards-only   # card data only
   python tools/setup_all.py                # card data, then the card and board art
   ```

3. **Build the server** (see "Build" below): `-DMODULES=static`, or
   `-DMODULES=disabled -DMODULE_MOD_HEARTHSTONE=static` for this module only.

4. **Configure** (see "Install" below): copy `conf/mod-hearthstone.conf.dist` to
   `<mangosd.conf directory>/modules/mod-hearthstone.conf` (the server will not start without
   it) and keep `AddonChannel = 1`. The defaults are fine; opponents, the match finder and the
   vendors come from the world migrations, not from the conf.

5. **Start the server once.** The core AutoUpdater applies `data/sql/world/*.sql` and
   `data/sql/character/*.sql` (card definitions, vendors, items, quests, the Hearthstone
   Masters, the account tables). `Database.AutoUpdate` must be on for the world and character
   databases, and if your conf restricts `Database.AutoUpdate.AllowedModules`,
   `mod-hearthstone` has to be in it. Keep `Database.AutoUpdate.SortByName = 1` (the default of `mangosd.conf.dist`):
   the migrations build on each other and must run in name order, not in file-date order. The updater knows a
   migration by its content hash, so never edit a migration file that already ran: a changed file runs again. The log
   shows `Hearthstone: enabled ...`, the dataset
   line and the loaded card rules; a missing table is named in the log.

6. **Build the card art** (needs internet once, about 300 MB cache, a few minutes):

   ```
   python tools/art/setup_art.py          # venv + Pillow, download, patch, textures, hero strips
   python tools/art/setup_art.py --check  # shows what is still missing
   ```

   The owner's UI art (board, collection, UI icons) is already committed under `art/out/Board`,
   `art/out/UI` and `art/out/Collection`; only the Blizzard-derived textures are generated.
   The addon also works without textures (cards are drawn as text), so this step can be done
   later.

7. **Install the addon and textures into the client** and restart the client (new texture files
   are not picked up while it runs):

   ```
   powershell -File tools/deploy/deploy-addon.ps1 -ClientDir "D:/Games/TurtleWoW" -WhatIf   # dry run
   powershell -File tools/deploy/deploy-addon.ps1 -ClientDir "D:/Games/TurtleWoW"
   ```

   `-ClientDir` is the folder with `WoW.exe`.

8. **Try it** with a GM account: `.hs status` (module state, card rules),
   `.hs pack grant <character> 3`, then `/hs` in game (the micro-menu button or `/hs collection`)
   opens the catalog, the Packs tab and the deck editor. A match starts at an opponent NPC's
   gossip menu or at the Hearthstone Masters (Ezra Chatterton, Horde; Ibelin Redmoore,
   Alliance), and only after one character of the account finished the fourth introduction
   quest ("A Seat at the Table" / "Your First Hand"). The masters, the two students and Byron, the
   Card Master (packs and crafting), are spawned by world migration 0007.

Optional: `Hearthstone.MatchAuraSpellId` (the "In Card Match" spell, a `Spell.dbc` entry you
create) adds root and damage immunity during matches; without it matches run normally and the
startup log warns.

## Layout

```
conf/mod-hearthstone.conf.dist   config template (keys Hearthstone.*)
src/HearthstoneLoader.cpp        Addmod_hearthstoneScripts() for the generated module loader
src/HearthstoneConfig.*          config cache (read in OnStartup and on .reload config)
src/HearthstoneScripts.cpp       world / player / command script glue (.hs commands)
src/HsMatchGlue.*                match glue: opponent gossip and deck menu, match start, the optional
                                 In Card Match effect, the action block with the Help > Stuck intercept,
                                 every end condition, the GAME pushes, .hs match
src/HsMatchStore.*               the match engine's card definitions (world DB), the match service
                                 singleton and the match history rows
src/HsCollectionStore.*          collection and pack database glue (character DB rows, card rules from the world DB)
src/HsDeckStore.*                deck and class unlock database glue (character DB rows)
src/HsEconomyGlue.*              coin and pack items, the coin purchase in the vendor window, right-click
                                 a pack, the crafting NPC gossip and access, .hs wallet
src/HsDataset.h                  generated: dataset id + hash (scripts/gen_dataset_header.py)
src/protocol/                    pure C++ (no core headers): codec, chunker, reassembler, rate limiter,
                                 per-player session state machine
src/collection/                  pure C++: ownership rules, pack pool, rolls and payloads, dust, match coins,
                                 crafting rules and payloads, per-account cache, mutation order
src/decks/                       pure C++: deck rules, states and payloads, per-account cache, saves, deletes,
                                 class unlocks, match-start check
src/match/                       pure C++: rules engine and card scripts, AI, GAME wire codec, match service
src/arena/, src/ladder/,         draft arena, world NPC ladder and Warsong Battlegrounds
src/battlegrounds/
addon/Hearthstone/               client addon (Lua 5.0, Interface 11200)
tests/                           standalone unit tests (C++ and Lua), golden files
docs/PROTOCOL.md                 wire protocol reference
data/sql/world                   world migrations (card definitions, opponents, vendors, items, quests, spawns)
data/sql/character               character migrations (collection, packs, decks, history, wallet)
data/cards, tools/carddata       card data pipeline
data/art, tools/art              card render pipeline and the board/UI art slicers
build/assets                     owner art sheets
```

## Build

The module is discovered because `src/` exists; no `mod-hearthstone.cmake` is needed.

```
cmake -S <server> -B <build> ... -DMODULES=static                       # with all modules
cmake -S <server> -B <build> ... -DMODULES=disabled -DMODULE_MOD_HEARTHSTONE=static   # only this one
```

`-DMODULE_MOD_HEARTHSTONE=disabled` removes it from the binary. Note that a module's
`conf/*.conf.dist` is listed in the binary even for a disabled module, so the active `.conf`
file must exist either way (see below).

## Install

1. Copy `conf/mod-hearthstone.conf.dist` to `<directory of mangosd.conf>/modules/mod-hearthstone.conf`.
   **mangosd refuses to start if this file is missing** once a build contains the module. If
   your build script only auto-creates the active conf for some modules by name, add
   `mod-hearthstone` to its list.
2. Set `AddonChannel = 1` in `mangosd.conf`.
3. Copy `addon/Hearthstone` to `<client>/Interface/AddOns/Hearthstone`.
4. Restart the server. The log shows `Hearthstone: enabled ...` and the dataset line at startup.

`.reload config` re-reads the `Hearthstone.*` keys.

## Configuration

See `conf/mod-hearthstone.conf.dist` for every key.

- `Hearthstone.MatchAuraSpellId` is the custom "In Card Match" aura (a DBC spell the realm owner
  creates); the default is 61006. `0` or an unknown id starts matches without it (no root, no
  damage immunity; a startup warning says so).
- `Hearthstone.Match.Enable` (default 1), `Hearthstone.Match.Opponents`
  (`<creature entry>:<class>,...`, default empty), `Hearthstone.Match.AiActionDelayMs`
  (default 1500) and `Hearthstone.Match.History.Enable` (default 1) configure matches.
- `Hearthstone.Pack.Weight.*` and `Hearthstone.Pack.LegendaryPity` set the booster odds.
- `Hearthstone.Deck.MaxSaved` (default and maximum 20) caps the saved decks per account.
- The draft arena and the Warsong Battlegrounds are always enabled; they are unavailable only when their
  data or the Battlegrounds effect registry failed to load.
- `Hearthstone.Debug` (default 0) adds per-frame logging and enables the ECHO protocol self-test
  for moderators.

## GM commands

`.hs status` (moderator or higher, or the server console) prints the module flags, protocol and
module version, dataset id and hash, handshaken sessions, pending reassemblies, limits, the
configured `MatchAuraSpellId`, loaded card rules and cached collections. Any other `.hs ...`
prints the usage. Players below moderator do not see the command.

`<player>` is a character name, online or offline; the data belongs to its account. `#5` means
account id 5. Card ids are dataset ids (`CS2_022`, `EX1_116`), case-insensitive.

| Command | Security |
|---|---|
| `.hs coll show <player\|#accountId> [cardId]` | moderator |
| `.hs coll grant <player\|#accountId> <cardId> [count=1] [unprotected\|protected\|reward]` | developer |
| `.hs coll remove <player\|#accountId> <cardId> [count\|all]` | developer |
| `.hs pack show <player\|#accountId>` | moderator |
| `.hs pack grant <player\|#accountId> [count=1]` | developer |
| `.hs pack remove <player\|#accountId> [count\|all]` | developer |
| `.hs deck show <player\|#accountId> [deckId]` | moderator |
| `.hs deck check <player> <deckId>` | moderator |
| `.hs deck delete <player\|#accountId> <deckId>` | developer |
| `.hs class show <player\|#accountId>` | moderator |
| `.hs class unlock\|lock <player\|#accountId> <class>` | developer |
| `.hs starter show <player\|#accountId>` | moderator |
| `.hs starter grant <player\|#accountId> <class>` | developer |
| `.hs npc show <player\|#accountId>` | moderator |
| `.hs npc unlock <player\|#accountId> <creature entry\|all>` | developer |
| `.hs match show [player]` | moderator |
| `.hs match decks` | moderator |
| `.hs match history <player\|#accountId> [count]` | moderator |
| `.hs match end <player>` | developer |
| `.hs wallet show <player\|#accountId>` | moderator |
| `.hs wallet dust <player\|#accountId> <+n\|-n>` | developer |
| `.hs hero show <player\|#accountId>` | moderator |
| `.hs hero credit\|grant\|revoke\|reset ...` | developer |
| `.hs arena show\|repair\|retire\|start ...` | moderator |
| `.hs ladder show\|player\|npc\|offers\|duel\|round\|result\|walk\|resume\|reset ...` | moderator (changes: developer) |
| `.hs bg start\|leave\|status` | any player while the mode is on (start is GM-only) |

Notes:

- `coll grant` stores `reward_only` copies protected. `count` is 1..99; an account owns at most
  999 copies of a card. `reward` fills missing playable copies (2, a Legendary 1) as protected
  and grants the rest unprotected. `remove` takes unprotected copies first; `all` removes every
  copy. Every change is logged and written to the matching ledger in the same transaction.
- `pack grant` / `remove` move 1..100 packs; an account holds at most 100000. The account's
  character gets a `PACK_CHANGED` push.
- `deck check` runs the match-start check for that character: rules, ownership, class access and
  match-engine support. Every deck and class change is written to its ledger and pushes
  `DECK_CHANGED`.
- `starter grant` awards a class's starter deck without unlocking the class: missing protected
  cards every time, the deck once per account.
- `npc unlock` unlocks as if the quest had been turned in (for tests; WoW quest records are not
  touched). Delete the row from `hearthstone_account_npc` while the account is offline to undo it.
- `match history` lists the recorded matches of a character (by name) or of every character of
  an account (`#<account id>`), newest first. `count` is 1 to 50, default 10. `match end`
  interrupts the player's running match.
- Coins and packs are items: `.additem 919601 <count>`, `.additem 919602 <count>`.

## Addon

`/hs` or `/hs status` prints the connection, collection, pack, deck, match and arena state.
`/hs collection` (or `/hs c`) opens the card catalog anywhere, `/hs packs` its Packs tab,
`/hs open` opens one pack, `/hs decks` opens the deck panel, `/hs game` reopens the match
window, `/hs arena` opens the draft arena, and `/hs heroes` opens the alternate hero portraits.
`/hs drag on|off` switches between dragging a card to play it and clicking it. `/hs anim
on|off|fast|skip` sets match animations. `/hs fx help` lists the spell-effect settings and the
effect bench. `/hs bookart on|off` falls back to plain book pages if a texture shows green, and
`/hs center` recovers an off-screen collection window. The handshake runs automatically two
seconds after logging in or `/reload`.

## Protocol summary

Prefix `WRHS`, transport `SendAddonMessage(..., "GUILD")`, frames
`v1 TAB OP TAB reqId TAB part TAB total TAB payload`, each at most 240 bytes with prefix.
Payloads are escaped so that no core addon trigger (`LFT`, `TW_`, ...) can appear. Full details:
`docs/PROTOCOL.md`. The Battlegrounds use a second prefix, `WRBG`, over the same framing and
escaping.

## Tests

```
powershell -File tests/build_tests.ps1
```

Compiles `tests/protocol_tests.cpp` and `tests/match_tests.cpp` (the match engine and AI,
including 600 AI-vs-AI games) with `cl /W4 /WX` into `02_build/hearthstone-check/tests`, checks
that the golden files still match the C++ output, runs the C++ tests, and runs
`tests/lua_tests.lua` with a Lua interpreter (`LUA` environment variable, `lua` on PATH, or the
`lua52.exe` of the companion-recruiter checks). The Lua tests contain a lint that fails on
syntax or library use newer than Lua 5.0.

Regenerate the golden files after an intentional protocol change:

```
protocol_tests.exe --emit-vectors    > tests/golden_vectors.txt
protocol_tests.exe --emit-transcript > tests/transcript.txt
```

## Card data

`data/cards/cards.normalized.json` is the single source. After it changes run

```
python tools/carddata/generate_all.py            # --check to verify only
```

which regenerates `src/HsDataset.h`, the world migration
`data/sql/world/0001_hearthstone_card_template.sql` (tables `hearthstone_dataset` and
`hearthstone_card_template`) and the addon `addon/Hearthstone/CardDB.lua`. All three carry the
same dataset id and hash. The server logs at startup whether the world database matches the
binary, and the addon warns when its CardDB hash differs from the server's WELCOME hash.
Details in `tools/carddata/README.md`.

Python tests: `python -m unittest discover -s tools/carddata -v`.

## Art

`art/out` holds the committed textures. The Blizzard-derived card renders, minions and hero
strips are rebuilt with `tools/art/setup_art.py`; the owner's sheets in `build/assets` are cut
with `tools/art/slice_board.py`, `slice_collection.py` and `ui_icons.py`. See
`tools/art/README.md`.
