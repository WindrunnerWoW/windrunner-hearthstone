# Card artwork pipeline

Turns the pinned card dataset (build 6024) into one texture per card for the 1.12-family client:
pre-rendered full card images -> fixes so the images match the 5834 data -> BLP2 textures.
Python 3 standard library for download and BLP writing; Pillow (in `tools/art/.venv`) for the image work.
Run everything from the module root (`modules/mod-hearthstone`).

## Licence and redistribution

The images are Blizzard artwork and text, served by HearthstoneJSON (`art.hearthstonejson.com`; the
service allows free use and asks users to cache instead of hot-linking). They are cached locally for
private, non-commercial use only. **Nothing Blizzard-derived under `art/` is committed and the images, patched
images and BLP files must not be redistributed** (`art/cache`, `art/patched`, `art/out/Cards`, `art/out/Minions`,
`art/out/Powers`, `art/out/Collection/hero_*.blp` are in `.gitignore`). What is committed: the scripts,
`data/art/renders.manifest.json` (ids, source URLs, sha256, sizes), `data/art/fixes.json`,
`data/art/banner_curves.json` and `data/art/textures.manifest.json`, plus the owner's own work that cannot be
fetched: `build/assets` (sheets), `art/source` and the BLPs cut from the sheets
(`art/out/Board`, `art/out/UI`, `art/out/Collection` without the hero strips).

## Fresh clone

`python tools/art/setup_art.py` runs the whole chain below (venv + Pillow, download, patch, card / minion / power
BLPs, hero strips) and skips steps whose output exists; `--check` lists what is missing, `--from <step>` resumes,
`--force` rebuilds. Afterwards `git status data/art` should be clean (the outputs match the committed hashes);
then `tools/deploy/deploy-addon.ps1`.

## Steps

| Step | Command | Result |
|---|---|---|
| 0. venv (once) | `python -m venv tools/art/.venv` then `tools/art/.venv/Scripts/python -m pip install Pillow` | Pillow 12 |
| 1. download | `python tools/art/fetch_renders.py` | `art/cache/<id>.png` (256x388), `data/art/renders.manifest.json` |
| 2. donors | `python tools/art/fetch_renders.py --hires` | `art/cache/512/<id>.png` for the ids in `fixes.json` `hires_sources` |
| 3. patch | `tools/art/.venv/Scripts/python tools/art/patch_renders.py` | `art/patched/<id>.png`, `art/review/fixes_before_after.png`, manifest gets `patched_sha256` |
| 4. BLP | `tools/art/.venv/Scripts/python tools/art/convert_blp.py [--all] [--verify]` | `art/out/Cards/<id>.blp`, `data/art/textures.manifest.json` |

Reruns are cheap and resumable: downloads skip files already cached (404 answers are remembered in
`art/cache/negative.json`, `--refresh` forgets them), request rate is at most about 4.5 per second with
retries and back-off, and the manifest and patched PNGs are deterministic (sorted keys, no timestamps;
two patch runs give identical bytes). `fetch_renders.py --plan` prints the source decision without
downloading. `tools/art/fit_curves.py` (needs `fetch_renders.py --hires-all`, about 170 MB) only has to be
rerun if the banner-title method changes; its result is stored in `data/art/banner_curves.json`.

## Source rule

For every visually relevant entity of `cards.normalized.json` (acquisition `standard`, `reward_only` and `quest`
collectibles, plus non-collectible minions, spells, weapons, hero powers and heroes; enchantments skipped;
594 ids; entities that never appear in the released game are left out: the Skeleton token `skele11` that no
card creates, see `cardlib.UNSUMMONED_TOKENS`; Adrenaline Rush and `EX1_016t` are not in the dataset any more;
the 40 Heroic-only Naxxramas entities are left out too, see below):

1. `https://art.hearthstonejson.com/v1/render/latest/enUS/256x/VAN_<id>.png` (Classic-format renders, which
   Blizzard states mirror Basic/Classic as of June 2014). 441 ids.
2. Otherwise the plain-id render, only if the current HearthstoneJSON data for that id agrees with our
   name/cost/attack/health, or if the id is absent from the current data (hero powers, the class heroes and
   some tokens have no balance history but cannot be checked by data). 46 ids, flagged `source_kind: plain`.
   The 30 Naxxramas collectibles (`FP1_*`, acquisition `quest`) have no `VAN_` render (HTTP 404 for all 30 at 256x and
   512x), so they use the plain renders: 27 as they are, 2 with a gem fix (below; the Nerubian token also has one), the Slime token `FP1_012t` accepted via
   `plain_render_ok`. Frame era and fonts match the `VAN_` renders; the two visible differences are post-6024 banners:
   a tribe banner "Undead" on 18 minions (and the Nerubian token) and a spell school banner (Arcane, Nature, Holy)
   on Duplicate, Poison Seeds, Avenge and Reincarnate; they are left as rendered (see "Other differences").
   The 74 non-collectible Naxxramas entities (the 5 tokens of collectible cards were always in; the other 69 since
   2026-10-12, when the phase 13 boss encounters began to use them): the 15 boss heroes
   (`NAX<n>_01`), 16 boss hero powers, 4 boss weapons, 12 boss spells, 27 minions (encounter minions, their tokens, the
   Horsemen, `FP1_006` Deathcharger, `NAXM_001`, `NAXM_002` and the five tokens of collectible cards `FP1_002t`, `FP1_007t`,
   `FP1_012t`, `FP1_014t`, `FP1_019t`) also come from plain renders (no `VAN_` render exists). Cost, attack, health,
   durability, name and rules text of all 69 new ones agree with our data and with the render (checked on contact sheets of every
   card, nothing needed a fix); the only differences are the post-6024 banners (tribe on `FP1_006` "Undead Beast" and
   `NAX15_05` "Beast", spell school on 9 spells) and the "Hero Power" / "Passive Hero Power" label that hero power renders
   print above their text. The boss heroes show a cost gem "0" on the card render (the match window only uses the art
   window, see "Hero portraits" below). **Heroic-only entities are never used (owner: no Heroic, ever) and get no art**:
   `artlib.art_heroic_only` drops the non-collectible `NAXX` ids that match `NAX<n>h_<nn>` or `NAX<n>_<nn>H` (40 entities:
   the Heroic bosses, powers, weapons and minions, including the Heroic-only Feugen and Stalagg). Their renders may
   still sit in `art/cache` from earlier exploration; they are in no manifest and no BLP is made.
   A plain render whose id is listed in `fixes.json` `plain_render_ok` is also accepted although the current data
   disagrees (the service's data and its renders can be from different builds; checked by eye).
3. Otherwise `missing` (none left: `EX1_614t` Flame of Azzinoth, whose plain render is now "Xavian Satyr", is generated).

Manifest fields per id: `source_id`, `source_url`, `sha256`, `size`, `width`, `height`, `status`
(`ok`, `patched`, `generated`, `missing`), `data_diff` (numeric/name differences to the current data
card the image came from), `text_differs` with `our_text` / `source_text`, `note`.

Result: 593 downloaded (579 ok, 14 patched), 1 generated, 0 missing (594 ids). `text_differs` compares our text with the
current data text of the source id, which is not always what the render shows (Webspinner: flagged, render identical to ours).

## Fixes (`data/art/fixes.json`)

Each entry has the reason (with the 5834 value), a list of operations and, for generated cards, a template.
Operations: `gem` (copy the inside of a stat gem from a donor render that shows the right digit), `text`
(rules text), `title` (banner name), `art` (grey placeholder in the art window).

Text and titles are painted at 512x and box-downscaled to 256x (the 256x renders are exactly the 2x2 box
average of the 512x ones, so the edit blends in). Body text: the old text is removed with a smooth
background estimate; the new lines are assembled from word images cut out of the card itself and of
donor cards (typeface, weight and antialiasing stay native; light or dark ink is handled through a coverage
map). Titles: letters are cut out of other cards' banner titles, un-rotated with the banner baseline curve
of that card type, re-set with the median letter spacing and rotated along the curve at the new position.
`glyphs` pins which donor supplies which letter, so the result does not depend on the rest of the cache.
A `text` op can also carry `compose` (a word that exists on no render is assembled from pieces of other words: whole words, or the left/right part of a word cut at the n-th gap between letters, with an optional blank `gap`) and `text_overrides` (replacement label text for a donor whose segmentation differs from the data text, for example a closing quote mark that stands apart from the word before it).

| Id | Card | Fix | 5834 value |
|---|---|---|---|
| EX1_131 | Defias Ringleader | attack gem 3 -> 2 (gem copied from Frostwolf Grunt) | 2/2 |
| EX1_320 | Bane of Doom | text "Deal 3" -> "Deal 2 damage" (digit from Holy Smite) | 2 damage |
| EX1_306 | Succubus | title "Felstalker" -> "Succubus" (letters from 5 other titles) | name |
| EX1_366 | Sword of Justice | "After" -> "Whenever you summon a minion, ..." | text |
| EX1_379 | Repentance | "After" -> "When your opponent plays a minion, ..." | text |
| EX1_609 | Snipe | "After" -> "When your opponent plays a minion, ..." | text |
| EX1_595 | Cult Master | "Whenever one of your other minions dies, draw a card." | text |
| CS2_038 | Ancestral Spirit | "Give a minion "Deathrattle: Resummon this minion."" (6024 text; "Resummon" = "Re" of Restore + "summon", closing quote cut from Soul of the Forest) | text |
| EX1_536 | Eaglehorn Bow | "Whenever a friendly Secret is revealed, ..." (6024 text) | text |
| EX1_289 | Ice Barrier | "Secret: When your hero is attacked, ..." (6024 text, render says "As soon as") | text |
| FP1_012 | Sludge Belcher | health gem 6 -> 5 (gem copied from Anub'ar Ambusher; the render shows the later 3/6) | 3/5 |
| FP1_025 | Reincarnate | cost gem 1 -> 2 (gem copied from Haunted Creeper) | cost 2 |
| FP1_007t | Nerubian (token of Nerubian Egg) | cost gem 4 -> 3 (gem copied from Shade of Naxxramas) | cost 3 |
| PRO_001at | Murloc (token of I Am Murloc) | cost gem 0 -> 1 (gem copied from Murloc Raider); the plain render is accepted although the current data says cost 0, because a fix covers the card | cost 1 |
| EX1_614t | Flame of Azzinoth | generated on the frame of Magma Rager: new title, cost 1, 2/1, real portrait art (`art_sources`, 512x jpg fitted to a 178 px square at (38, 38)) | 1 mana 2/1 |

`NEW1_006` Adrenaline Rush and `EX1_016t` Skeleton have no art anywhere (art.hearthstonejson.com answers 404);
earlier versions generated grey-placeholder frames for them, but neither card appears in the released game, so
they are no longer part of the pipeline or of the dataset (the old fix definitions are in the git history). Known leftover: the Bane of Doom render keeps
the later wording "If it dies" (our text: "If that kills it"); the word "kills" occurs on no render.
`art/review/fixes_before_after.png` shows every fixed card before and after.

## Other differences noticed (not fixed)

* About 25 more cards use the later wording of the same effect (for example Redemption, Misdirection, Freezing
  Trap, Arcane Missiles, Kill Command, Ysera "add a Dream Card", Faerie Dragon "Elusive"); the numbers agree.
  They are listed with both texts in the manifest (`text_differs`).
* Choose One option spells (`EX1_154a/b`, `EX1_155a/b`, `EX1_160a/b`, `EX1_164a/b`, `EX1_166a/b`, `EX1_178a/b`,
  `EX1_573a/b`, `NEW1_007a/b`, `NEW1_008a/b`) show the parent card's cost and in some cases a later name
  ("Solar Wrath" vs "Wrath"). `EX1_finkle` shows "Pip Quickwit", cost 3 (ours: Finkle Einhorn, cost 2),
  `DREAM_01` costs 2 (ours 3), `EX1_tk9` Treant has no keyword text.
* Naxxramas collectibles: wording-only drift (rules identical) on Spectral Knight (render "Elusive", ours "Can't be targeted by spells or
  Hero Powers."), Maexxna ("Poisonous", ours "Destroy any minion damaged by this minion."). The other 28 collectible
  renders carry the 6024 text, numbers and names (Undertaker still reads +1/+1 and Webspinner "Add a random Beast card to your
  hand."). The plain renders add the tribe / school banners described above, which did not exist in 6024.
* All renders use the modern frame (race banner, modern fonts and gems), not the 2014 one.

## Texture format and texcoords

`convert_blp.py` pads each render (256x388) top-aligned onto a transparent **256x512**
canvas and writes BLP2, DXT5 (alpha depth 8, alpha encoding 7), full mip chain (10 levels), 175,972 bytes
each. Texcoords for the addon: `SetTexCoord(0, 1, 0, 388/512)` = `(0, 1, 0, 0.7578125)`; the exact
per-card values are in `data/art/textures.manifest.json`. Output goes to `art/out/Cards/<id>.blp`, so the
client path would be `Interface\...\Cards\<id>` or wherever the patch MPQ places the folder.

`C:/TurtleDev/00_tools/blp-converter-win-x64/BlpConverter.exe` is an Avalonia GUI application: it ignores
`--help` and file arguments and just opens a window, so it cannot be scripted. The script therefore writes BLP2
itself. The output decodes with Pillow's BLP reader (`--verify`: mean abs error about 5 to 7 per channel on
visible pixels, alpha error below 0.1), but it has not been loaded by the actual client yet.

## Board art

The match window's decorative pieces (board field, hero rings, number gems, turn banners, card back, mana
crystals, glows) are cut from the owner's three Hearthstone-styled UI sheets (`build/assets/*.png`, 1536x1024,
black background; ignored like everything under `build/`). Only pieces WoW has no equivalent for are used;
windows, tabs and buttons stay native. Pieces with baked text or digits are not used, except the two turn
banners whose baked text is exactly their meaning ("Your Turn", "Opponent's Turn"). The number gems are the
clean ones from the card template of the labeled sheet (blue mana hex, gold sword circle, red drop), cut out
with polygon clips; `gem_armor` and `mana_crystal_empty` are recoloured copies made by the script.

| Step | Command | Result |
|---|---|---|
| slice | `tools/art/.venv/Scripts/python tools/art/slice_board.py` | `art/out/Board/<name>.blp`, `art/review/board/<name>.png` and `_contact.png`, `data/art/board_textures.manifest.json` |
| one piece | `... slice_board.py --only gem_health card_back` (`--no-blp` previews only, `--verify` decode check) | |
| deploy | `powershell -File tools/deploy/deploy-addon.ps1` (`-Board None` to skip) | `Textures\Board\*.blp` and a generated `BoardTextures.lua` in the client copy |

The coordinates live in `data/art/board_pieces.json` (committed: sheet, crop rectangle, background mode and
tolerance, clip polygons, target size, anchors); the field meanings are documented in the header of
`slice_board.py`. Background modes: `flood` (black connected to the crop edge becomes transparent, soft
edge), `luma` (brightness as alpha for glows and banners with a glow), `none` (+ polygon/ellipse clip). Each
piece is scaled with premultiplied alpha, placed top-left on a power-of-two canvas (at most 512 per side;
`board_field` is stretched to 512x512, its native aspect 1195:896 = 1.3337 is in the manifest as `aspect`; the sheet is `Orcish Warboard Bare Slots.png`, its two empty deck slots are the `deck_top`/`deck_bottom` anchors, and `deck_thin`/`deck_half`/`deck_full` are the stack sprites drawn in them by `MU.RefreshDecks`, `deck_empty` the glowing bar laid into an empty slot, its glow from the piece's `halo`) and
written as BLP2 DXT5 with mips by `convert_blp.py`'s encoder. Output is deterministic. The manifest holds
`w`/`h` (used pixels), `right`/`bottom` texcoords (`SetTexCoord(0, right, 0, bottom)`) and `anchors`
(fractions of the used image): `board_field` has `hero_top`/`hero_bottom` (the arch interiors) and
`hero_top_safe`/`hero_bottom_safe` (the rectangles below the arch curve) and `arch_top_frame`/`arch_bottom_frame`
(where the `arch_top`/`arch_bottom` cut-outs come from: the field's arch frames with the opening made transparent by
`hole_flood` and a `feather`ed border, laid over the hero portrait); rings and the minion oval have
`inner` (the largest square/rectangle inside the opaque interior, for a portrait drawn above the piece).
Like the card textures the BLPs have not been loaded by the real client yet (they decode with Pillow).

### Procedural glows, secret medallions and oval minion portraits

* `glow_round`, `glow_oval`, `glow_arch` are our own drawings (no sheet): white soft glows with alpha around an
  ellipse / an arch (rectangle with an elliptic top, dome 39% of the height), made by `slice_board.py` from the
  `procedural` entries of `board_pieces.json` (128x128, BLP2 DXT5). The shape's rectangle is the piece's `inner`
  anchor; the addon (MatchUI `MU.LayoutGlow`) maps it onto the frame it glows around and tints the white glow with
  `SetVertexColor` (green, red, gold). `anchors_out` in a piece gives anchors measured on the finished image
  (`minion_oval` got `oval`, the real ellipse of its opening, which the oval portraits fill).
* `secret_paladin`, `secret_hunter`, `secret_mage` come from the owner's `build/assets/secret icons.png` (sheet
  `secrets`, top row; flood-cut like the other pieces).
* `minion_portraits.py` writes one extra texture per minion card (322: collectible minions and tokens of the art
  set, incl. the 27 Naxxramas encounter minions): the art oval of the render (centre x 128.5, y 44..178) cropped to the aspect of the ring's opening, scaled to
  128x128 and cut with an elliptic alpha with a 2.5 px soft edge, BLP2 DXT5 with mips (23,044 bytes), into
  `art/out/Minions/<cardId>.blp`, with `data/art/minions.manifest.json` (committed) and a contact sheet in
  `art/review/minions/`. Input is the same render (`art/patched` else `art/cache`) as `convert_blp.py`.

| Step | Command | Result |
|---|---|---|
| glows, ring anchor, medallions | `tools/art/.venv/Scripts/python tools/art/slice_board.py --only minion_oval glow_round glow_oval glow_arch secret_paladin secret_hunter secret_mage` | `art/out/Board/*.blp`, manifest |
| oval portraits | `tools/art/.venv/Scripts/python tools/art/minion_portraits.py [--verify]` | `art/out/Minions/<id>.blp`, `data/art/minions.manifest.json` |
| round hero power portraits | `tools/art/.venv/Scripts/python tools/art/power_portraits.py [--verify]` (after `slice_board.py --only hero_power_ring`) | `art/out/Powers/<id>.blp` (50, 128x128), `data/art/powers.manifest.json` |
| deploy | `powershell -File tools/deploy/deploy-addon.ps1` (`-Minions None` / `-Powers None` to skip) | `Textures\Minions\`, `Textures\Powers\`, `HS_MinionTextures` / `HS_PowerTextures` in the generated `CardTextures.lua` |

`power_portraits.py` does the same for hero powers and weapons (every `HERO_POWER` and `WEAPON` card of the art set, 50:
12 class / Jaraxxus hero powers incl. `EX1_tk33` INFERNO!, 18 weapons, and since 2026-10-12 the 16 Naxxramas boss hero powers
and 4 boss weapons): the bare 512x512 portrait of HearthstoneJSON (`--fetch` downloads
`https://art.hearthstonejson.com/v1/512x/<id>.jpg` into `art/cache/powers512/`; all 50 exist there) as a 128x128 circle with
a 2.5 px soft edge; without the bare portrait the round art window of the render (centre (128, 112), radius 44) is used.
The ring piece `hero_power_ring` got the anchor `circle` (its transparent opening grown by 2.5 px, so the ring, drawn above
the portrait, hides the soft edge) via `anchors_out`.

### Hero portraits (class heroes and Naxxramas bosses)

Heroes need no extra texture: the match window's arch portrait (`MU.SetHeroArt` in `MatchUI.lua`) cuts the art window out of
the hero's card texture `Textures\Cards\<heroId>` with the global crop `MU.ARCH_DEFAULTS` (`cx` 0.5, `cy` 0.315, `zoom` 0.255
of the 256x388 image, fitted to the arch's aspect; the saved arch overrides in `HearthstoneDB.arch` tune it). There are no
per-hero parameters. The hero card frame is the same for the class heroes and the boss heroes, so the 15 boss heroes
(`NAX<n>_01`) work with the
same numbers; checked on a side-by-side of the crop for `HERO_01..09` and all 15 bosses (heads sit well inside the arch, the
card's gold rim only reaches the top corners, which the arch frame covers as it does for the class heroes). A few boss
renders use other frames (Kel'Thuzad silver, Noth blue, Heigan purple) or a bar under the portrait (Loatheb, Gothik, Razuvious,
Rivendare); the art window is in the same place.

### Arena window (2026-10-12)

`arena_ui.py` turns the owner's arena screen images (`art/source/ui/arena/`: `background.png` 1672 x 941, `play.png`,
`retire.png`, `back_frame.png`, all committed) into `art/out/UI/ArenaBg_c<col>r<row>.blp` (the background without its bottom
39 rows, scaled to 1536 x 829 and cut into 3 x 2 tiles of 512), `ArenaArch.blp` (the golden hero arch cut out of the
background: gold only, opening and outside transparent), `ArenaPlay.blp`, `ArenaRetire.blp` and `ArenaPlate.blp` (the empty
plate: Back and the heading banner, text written by the addon). It prints the table `AU.ART` (texture shares, the arch's
opening and portrait strips) that `ArenaUI.lua` holds; paste it there when a source changes. `--review` writes
`art/review/arena_*.png`. All other positions in `ArenaUI.lua` are pixels of `background.png`. It also cuts `lava_bar.png` into `art/out/UI/ArenaLava.blp` (64 x 256, whole texture, no table entry): the addon stretches it to the height of each mana curve bar.

### Craft panel (session 15)

`ui_icons.py --only craft` turns the owner's crystal status panel (`art/source/ui/craft_panel.png`, 1774 x 887, Blizzard-style
art, not committed) into `art/out/UI/CraftPanel.blp` (the whole image scaled to 512 x 256, texcoords 0..1, BLP2 DXT5 with
mips). `CraftUI.lua` shows it under the card of the craft window: the left slot is the Disenchant button (green bar "+N"
dust), the right slot the Craft button (red bar "-N"), the plate below shows the dust. The rectangles of slots, bars and
plate are measured on the source and kept in `CU.RECTS` (source pixels). `deploy-addon.ps1` copies the BLP with the other
UI textures; without it the slots fall back to plain tooltip-style boxes.

`ui_icons.py --only banner` makes `art/out/UI/BannerBlank.blp`: the "x2" ownership banner of the book with its text
refilled from the plate (needs `slice_collection.py` to have made `art/out/Collection/banner_x2.blp`). The sheet only has
"x1" and "x2" banners; for 3 and more `CollectionUI.lua` (`UI.SetOwnedMark`) puts this blank plate under the card and writes
"x3" on it in ink. Without the texture the old dark "x3" badge is used.

`ui_icons.py --only glow` draws `SlotGlow.blp` (a blue-tintable outline with a soft glow) and `SlotShade.blp` (the same arched
shape filled, black at 55%) for the craft panel slots: a slot that can be used gets the outline, one that cannot is dimmed
and shows no text. Both are generated, no source image is needed.

### Warsong Battlegrounds art

`bg_minions.py` cuts one oval portrait per 15A minion, exactly like `minion_portraits.py` (it imports that script for the
crop, the ring aspect and the elliptic mask): 188 minion cards (167 pool minions + 21 tokens) and 396 full cards (209
normal minions and spells + 187 golden twins) taken from the engine's own table
`src/battlegrounds/HsBgCardData.inc`, into `art/out/BgMinions/<id>.blp`, with `data/art/bg_minions.manifest.json`
(committed) and a contact sheet in `art/review/bg_minions/`. The addon sends the *normal* id for a golden minion (with a
golden flag), so only the normal cards are cut; the gold ring is a shared texture drawn over the portrait.

A bare run downloads what is missing (a User-Agent is required by the CDN, `--plan` lists the ids without downloading).
Twelve ids have no normal 256x render; the script uses the Battlegrounds variant's constructed twin (`BG_` prefix) or
the golden / "up" render instead (`RENDER_FALLBACK`) - the artwork is the same, only the frame differs and the crop
takes the art oval.

`bg_textures.py` draws the three small textures the window needs that nothing else fits (committed, like
`effect_textures.py`): the gold `golden_ring` for a golden minion (128x128), the `coin` for the Gold line and the
`tier_pip` for the tavern tier (32x32 each), into `addon/Hearthstone/Textures/Bg/`, with
`data/art/bg_textures.manifest.json` and a contact sheet in `art/review/`. The button plates are the collection's
(`HS_ButtonTextures`), so none are made here.

`bg_quests.py` cuts one round 128x128 portrait per in-pool 24.2 quest and quest reward through the
same pipeline as the hero powers (`power_portraits.py`, imported for the circle and the size): 12 quests and 23
rewards, selected by the `in_pool` flags of `data/battlegrounds/quest_values.json` and looked up in the git-ignored
`bg.normalized.json` (dbf_id and name), so the set can never drift from the engine. Output is
`art/out/BgQuests/<id>.blp`, the committed `data/art/bg_quests.manifest.json` and the contact sheet
`art/review/bg_quests/_contact.png`. A card with no 512x art falls back to its 256x render (marked `render:<id>` in the
manifest); one with neither is skipped with a logged note.

| Step | Command | Result |
|---|---|---|
| portraits | `tools/art/.venv/Scripts/python tools/art/bg_minions.py [--verify]` | `art/out/BgMinions/<id>.blp` (179), `data/art/bg_minions.manifest.json` |
| quest/reward portraits | `tools/art/.venv/Scripts/python tools/art/bg_quests.py [--verify]` | `art/out/BgQuests/<id>.blp` (35), `data/art/bg_quests.manifest.json` |
| mode textures | `tools/art/.venv/Scripts/python tools/art/bg_textures.py` | `addon/Hearthstone/Textures/Bg/*.blp` (3, committed), `data/art/bg_textures.manifest.json` |
| tests | `tools/art/.venv/Scripts/python tools/art/test_bg_art.py` | determinism / hash / drift checks |
| deploy | `powershell -File tools/deploy/deploy-addon.ps1` (`-BgMinions None` to skip) | `Textures\BgMinions\`, `HS_BgTextures` in the generated `BgTextures.lua` (client copy) |
