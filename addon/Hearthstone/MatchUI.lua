-- Hearthstone addon: the match window.
--
-- One large native-styled frame, about 80% of the screen and centred, with the world visible around
-- it. The opponent is on top and the player at the bottom. Each side shows its hero (art cropped
-- from the hero card texture, health / armor / attack gems), the hero power, the weapon, the mana
-- crystals and the hand and deck counts; the opponent's hand is a row of card backs. In the middle
-- are the two boards (minion art cropped from the card texture, attack / health gems, Taunt and
-- Divine Shield tags, a green "can attack" glow), the message bubble, the turn banner and the End
-- Turn button. The player's hand stands at the bottom in a shallow fan that reaches over the field's bottom rim
-- (card textures, gems only where cost / attack / health differ from the card database); the hovered hand card
-- grows in place (MU.HandPop), while a minion, hero or secret shows the large card preview. A history column of
-- card tiles (what was played, what attacked, hero powers, triggers; hover for the card and the text) fills the
-- left edge (MU.RefreshLog). A mulligan view (cards dealt from the deck pile and turned over, click to mark, Done,
-- replaced cards fly back and their new cards are dealt; see "mulligan animation") and a result view (Victory /
-- Defeat / Draw / Interrupted, Leave) cover the board while they apply.
--
-- Clicking: a hand card is played by dragging it onto the board (a click only says so; with /hs drag off a click
-- plays it at once, or enters targeting when the legal list (lg) names targets). A ready attacker or the hero
-- power is clicked and enters targeting. While targeting, the valid targets
-- glow (green for friends, red for enemies) and a click on one sends the action; a right click
-- anywhere, a click on the source again or Escape cancels. Minions are played rightmost. Closing
-- the window (X or Escape) while the match runs asks "Surrender this match?" (StaticPopup): Yes
-- sends concede, No keeps the window open. Everything the window shows comes from Match.lua; the
-- server judges every action, and one action is in flight at a time (Hearthstone.lua).
--
-- In a draft arena duel (HsArena.IsArenaDuel: the run is a duel this character plays) the close button and
-- Escape PAUSE the duel instead (GAME_PAUSE: the server saves it, the window closes when it answers, the run resumes at
-- the host), and the surrender is a separate button in the title bar with its own popup that says it ends the run.
-- Ordinary matches are unchanged.
--
-- State markers on minions and heroes (frozen: icy tint, blue rim and icon; immune:
-- gold rim; stealth: faded art; silenced: grey tint; poisonous; enraged: red tint and attack number;
-- can't attack: icons from Interface\Icons, see MU.MARKERS); secrets as small medallions on the dome of each
-- hero (tooltip: own name and text; the opponent's: "Secret"; see MU.SecretPositions); Overload as red locked
-- crystals plus a line "Overload: N locked, M next turn"; Choose One: a click on such a hand card opens
-- a chooser with the option cards, then targeting for that option (from lg), then play with c=<option>;
-- a pending pick (Tracking) as a centred panel whose click sends pick, "Opponent is choosing..." while
-- the opponent picks; the difficulty in the window title; a yellow glow instead of green on playable Combo cards while Combo is on.
--
-- Part A: animations. The window draws a presented snapshot (HsMatchAnim.PresentedGame()) that may
-- lag behind HsMatch.game while beats play (see MatchAnimations.lua): a full-contact attack lunge (the attacker
-- travels to the defender, overlaps it, and returns to its exact anchor), a flash and shake on what is hit,
-- floating -N / +N numbers, a shield pop, a death fade, the opponent's card reveal and a generic effect pulse.
-- Everything temporary lives in pools created once (MU.Build): a Fx frame with textures and font strings, and
-- the reveal card. Ordinary inputs are disabled while the board is behind (MU.Behind()); surrender, close,
-- leave and the resync stay available. Every temporary point, alpha and frame level is restored when a plan
-- ends, is skipped, or the window hides (MU.RestoreAll). MU.Fx is the hook of part B (native effects).
--
-- Board art pieces come from HS_BoardTextures (BoardTextures.lua, written by the deploy script):
-- board_field (with the hero arch anchors), hero_ring, hero_ring_armor, hero_power_ring, card_back,
-- banner_your_turn, banner_opponent_turn, mana_crystal, mana_crystal_empty, glow_green, glow_red, glow_yellow,
-- minion_oval, gem_attack, gem_health, gem_mana, gem_armor, weapon_frame and speech_bubble, plus
-- glow_round / glow_arch / glow_oval (shape-matched white glows, tinted here), secret_paladin /
-- secret_hunter / secret_mage (the secret medallions on the hero's dome) and the oval minion portraits of
-- HS_MinionTextures (CardTextures.lua). Every piece is optional: without it the native WoW look (tooltip borders,
-- flat colours, text) is used.
--
-- All frames are created once in MU.Build. All positions are in design pixels of the window (the
-- window is scaled to fit the screen: MU.Fit).
--
-- Lua 5.0 / WoW 1.12: SetScript handlers get no arguments and use this / arg1; no '#', no '...'.

Hearthstone = Hearthstone or {}
local H = Hearthstone
local MA = HsMatch
local AN = HsMatchAnim
local CM = HsCollection
local AR = HsArena
H.MatchUI = H.MatchUI or {}
local MU = H.MatchUI

-- The client clock; 0 where GetTime does not exist (the test harness), like MatchFx.
local function clock()
    return GetTime and GetTime() or 0
end

local WHITE = "Interface\\Buttons\\WHITE8X8"
local TOOLTIP_EDGE = "Interface\\Tooltips\\UI-Tooltip-Border"
local TOOLTIP_BG = "Interface\\Tooltips\\UI-Tooltip-Background"
local DIALOG_BG = "Interface\\DialogFrame\\UI-DialogBox-Background"
local DIALOG_EDGE = "Interface\\DialogFrame\\UI-DialogBox-Border"
local NUMBER_FONT = "Fonts\\FRIZQT__.TTF"

-- layout (design pixels)
MU.DESIGN_H = 860             -- window height (the field art is 1225 x 941: the taller, the less it is squashed)
MU.MIN_W, MU.MAX_W = 766, 1240   -- (MAX_W 1240: a bit wider than 1180, so the field art is stretched a little more)
MU.FILL = 0.8                 -- share of the screen the window covers
MU.TOP = 36                   -- title bar
MU.PAD = 12
MU.GAP = 8
MU.LOG_W = 0                  -- width reserved beside the board for the history column: none, it stands on the board's stone strip
MU.LOG_TILE = 26              -- a history tile (its picture, inside the rim)
MU.LOG_TILE_GAP = 2           -- between two tiles (their borders)
MU.LOG_BORDER = 5             -- the coloured rim around a tile's picture (each side; thick so mine / the opponent's can be told apart at a glance)
MU.LOG_STRIP = { x = 0.107, top = 0.356, bottom = 0.684 }   -- the pale stone plate at the left edge of board_field, as shares of the field picture: centre x, top and bottom
MU.LOG_MARGIN = 4             -- between the top / bottom of that plate and the first / last tile
MU.LOG_MAX_TILES = 20         -- tile frames created (as many as fit are shown)
MU.LOG_SMALL = 15             -- the little target picture in the corner of a tile
MU.LOG_ICON = 14              -- the kind icon (sword, skull, ...) in the opposite corner
MU.LOG_ICONS = {              -- the corner icon of a tile kind (standard 1.12 icons)
    atk = "Interface\\Icons\\INV_Sword_04", dr = "Interface\\Icons\\Spell_Shadow_DeathCoil",
    trig = "Interface\\Icons\\Spell_Nature_Lightning", rev = "Interface\\Icons\\INV_Misc_QuestionMark",
}
MU.LOG_RIM = { m = { 0.72, 0.74, 0.78 }, o = { 0.95, 0.08, 0.08 } }   -- the rim colour: mine light grey, the opponent's strong red
MU.MINION_W, MU.MINION_H = 76, 92
MU.MINION_GAP = 14             -- (the attack / health gems reach 9 px beyond the frame)
MU.HAND_W, MU.HAND_H = 96, 146   -- a hand card at rest (the 256x388 card aspect)
MU.HAND_STRIP = 70            -- the hand dock below the field; the cards stand in it and reach over the field's bottom rim
MU.HAND_COST_AT = { 0.15, 0.176 }  -- middle of the mana gem printed on the card render (shares of its width and height); the changed cost gem covers it
MU.HAND_ROW_W = 760           -- widest row of resting hand cards
-- The fan: step = how far the next card
-- is to the right as a share of the card width (0.55: each card shows about half its width, as in Hearthstone), tilt =
-- degrees the outermost cards lean outwards, arc = pixels the outermost cards stand lower than the middle one, base =
-- the middle card's bottom edge above the bottom of the board area.
-- glowx / glowtop / glowbottom = pixels the playable highlight reaches beyond the visible card on the left and right /
-- on top / at the bottom (negative: smaller).
MU.HAND_FAN = { step = 0.55, tilt = 12, arc = 16, base = 28, glowx = 6, glowtop = 0.1, glowbottom = 7 }
MU.GLOW_STRIPS = 16          -- strips of a tilted hand highlight
MU.HAND_POP_W = 184           -- width of the hovered hand card (it grows from its bottom edge, in place)
-- Frame levels above the board. Every hand card owns a band of HAND_BAND levels (its highlight at the bottom, then the
-- card, its text and its gems), card i+1 stands above the whole band of card i. So a card's highlight lies behind that
-- card but over the cards behind it, and never over the cards in front of it. The ten bands end below HAND_POP_LEVEL
-- (the hovered card), which ends below the overlays (mulligan, chooser, pick, result) and the Fx layer.
MU.HAND_LEVEL, MU.HAND_BAND = 2, 10
MU.HAND_POP_LEVEL = 102
MU.OVERLAY_LEVEL = 120
MU.FRAME_TINT = { 0.84, 0.84, 0.86, 1 }   -- the window frame's tint if CollectionUI.lua is not loaded (it is CollectionUI.FRAME_TINT)
MU.PLAY_MARGIN = 10          -- a dragged card is played above the top of the resting hand cards plus this
MU.BACK_W, MU.BACK_H = 30, 42 -- the opponent's card backs
MU.MINION_GEM = 34             -- attack / health gems of a minion
MU.HERO_HP_GEM = 38            -- health gem of a hero
MU.HERO_ARMOR_GEM = 50         -- armor shield of a hero (the steel shield picture, about 1.5 times the health gem, as in the reference)
MU.HERO_ARMOR_W = 43           -- width of the armor shield (the art's own proportion, 0.86 of its height)
MU.HERO_ARMOR_FONT = 23        -- font size of the armor number
MU.TAUNT_GROW = 0              -- pixels the taunt frame's opening is larger than the minion's oval on every side
MU.MINION_GLOW_PAD = -3        -- pixels the green / red glow of a minion's oval is grown (negative: smaller) on every side
MU.HERO_HP_FONT = 20            -- font size of the hero health number (the gem is bigger than the others)
MU.ATTACK_TEXT_DX, MU.ATTACK_TEXT_DY = 0.04, -0.11   -- the attack number's place in its gem, as shares of the gem size (x right, y up)
MU.END_GLOW = 9                -- pixels the End Turn glow reaches beyond the button
MU.PILL_W, MU.PILL_H = 50, 26      -- the blue pill with "mana / maximum" (mana_pill)
MU.MANA_GEM_H = 28            -- height of a mana crystal (mana_gem_full / mana_gem_empty / mana_lock); it shrinks only if ten do not fit
MU.MANA_GLOW = 1.5            -- the glow of a crystal a hovered card would spend (mana_gem_glow) is this many times its width
MU.SHIELD_GROW = 15            -- pixels the Divine Shield bubble's oval reaches beyond the minion's oval on every side
MU.HERO_MIN = 78
MU.POWER = 62                 -- hero power button (nominal; MU.Place sizes it from the hero ring, MU.PowerSize)
MU.HERO_RING_PAD = 6          -- the hero_ring piece reaches this far beyond the hero frame
MU.POWER_RING_PAD = 4         -- the hero_power_ring piece reaches this far beyond the power button
MU.POWER_ART_GROW = 2         -- pixels the hero power picture reaches beyond the ring's opening on every side (the ring covers the overflow)
MU.SIDE_GAP = 10              -- between the hero's outer frame (arch frame / ring) and the hero power ring
MU.ROW_CLEAR = 20             -- between a minion row and the hero arch frames of the board art
MU.OPP_ROW_LIFT = 3           -- the opponent's minion row stands this many pixels higher than ROW_CLEAR below his arch frame
MU.MY_ROW_LIFT = 8            -- the player's minion row stands this many pixels higher than ROW_CLEAR above his arch frame
MU.END_W, MU.END_H = 115, 54    -- the End Turn / Enemy Turn button (end_turn_button / enemy_turn_button): its size
MU.END_DX, MU.END_DY = -89, -32    -- its place: pixels from the board's right edge (negative = inwards) and down from the mid line (MID_Y)
-- the card decks at the right edge of the field, beside the End Turn button: a hover area each (the count in the tooltip).
-- Its centre: pixels right of the button's centre, and up (+) / down (-) from it. Tune here if the art moves.
MU.DECK_W, MU.DECK_H = 64, 112
MU.DECK_DX = 56
MU.DECK_OPP_DY, MU.DECK_MY_DY = 92, -95
-- The stack drawn in a deck's slot of the board art (deck_top / deck_bottom anchors of board_field) by the cards left:
-- deck_full from DECK_FULL_MIN cards, deck_half from DECK_HALF_MIN, deck_thin below that, the glowing deck_empty bar at 0.
-- The sprite is as high as the slot (times DECK_STACK_H) and stands DECK_STACK_PAD pixels left of the slot's left edge;
-- deck_empty fills the slot itself (its "inner" anchor on the slot, the glow reaching beyond).
MU.DECK_FULL_MIN, MU.DECK_HALF_MIN = 20, 6
MU.DECK_STACK_H, MU.DECK_STACK_PAD = 1.06, 3
MU.MANA_MINE_DROP = 114     -- my mana (pill and crystals) stands this many pixels lower than my hero power (clamped to the board)
MU.MANA_OPP_GAP = 20           -- the opponent's mana pill stands this many pixels above his hero power's ring
MU.MANA_MINE_SHIFT = 40       -- my whole mana area (pill, crystals, overload line) moves this many pixels to the right
MU.MANA_X = -10              -- my pill's left edge: right of my hero power's ring by this much (negative: tucked under the ring)
MU.MANA_PILL_GAP = 4          -- between my pill and my first mana crystal
MU.MANA_GEM_WIDTH = 0.8       -- a mana crystal's width as a share of its picture's natural width (the hexagons are drawn a little narrower)
MU.MANA_GEM_SPACE = 1         -- between two mana crystals
MU.MANA_RIGHT = 14             -- the last crystal's distance to the board's right edge
MU.MANA_OPP_DX = 32            -- the opponent's pill: its left edge relative to the cost gem's right edge
MU.POWER_COST_GEM = 30        -- the cost gem on top of a hero power
MU.FROZEN_GROW = 0            -- pixels the ice ring (frozen_ice) of a frozen character is larger than the minion's oval / hero's arch on every side
MU.BANNER_IN, MU.BANNER_HOLD, MU.BANNER_OUT = 0.15, 1.05, 0.3   -- the turn banner flash (seconds)
MU.WEAPON_W, MU.WEAPON_H = 64, 64     -- the weapon's round button (its ring, weapon_ring, reaches WEAPON_RING_PAD beyond it)
MU.WEAPON_RING_PAD = 4
MU.WEAPON_GEM = 32            -- attack / durability gems of a weapon
MU.POWER_FLIP_TIME = 0.25    -- a hero power turns over (ready <-> used): it squashes to an edge and opens again (seconds)
MU.CRYSTAL_W, MU.CRYSTAL_H = 14, 17
MU.MAX_CRYSTALS = 10
MU.MAX_HAND = 10
MU.MAX_BOARD = 7
MU.MULL_W = 170               -- cards of the mulligan view (scaled down when the board is narrow)
MU.MULL_GAP, MU.MULL_TOP = 28, 172
MU.MULL_RIBBON_W, MU.MULL_RIBBON_H = 330, 103   -- the "Starting Hand" ribbon (ribbon_blue) when the banner is not installed
MU.MULL_BANNER_W = 390        -- the "Starting Hand / Keep or Replace Cards" banner (banner_starting_hand)
MU.MULL_SLOTS = 5
MU.MULL_H = math.floor(MU.MULL_W * 388 / 256 + 0.5)
-- The mulligan animation (seconds): the cards are dealt from the deck pile one after the other and turn over; after
-- Done the replaced cards fly back into the pile, their new cards are dealt and turn over, and the hand flies down.
MU.MULL_DEAL_STAGGER, MU.MULL_DEAL_FLY, MU.MULL_DEAL_FLIP = 0.13, 0.40, 0.36
MU.MULL_OUT_STAGGER, MU.MULL_OUT_FLY = 0.07, 0.32
MU.MULL_GAP_BEFORE_IN, MU.MULL_HOLD, MU.MULL_TO_HAND = 0.12, 0.55, 0.40
MU.MULL_CHROME_FADE = 0.30    -- the banner and the hint fade out when Done is clicked; the first card leaves after it
MU.MULL_DECK_SCALE = 0.28     -- size of a card in the deck pile, as a share of the mulligan card
MU.PREVIEW_W, MU.PREVIEW_H = 272, 470
-- vertical shares of the field (the board area above the hand strip)
MU.OPP_BOARD_Y, MU.MY_BOARD_Y, MU.MID_Y = 0.30, 0.565, 0.5
-- art crops: u1, u2, v1, v2 as fractions of the 256x388 card image (the portrait oval of a minion
-- or hero, the art window of a spell, hero power or weapon)
MU.CROP_MINION = { 0.31, 0.69, 0.16, 0.40 }
-- The hero portrait in the board field's arch (when the board_field piece is installed). The arch box comes from
-- the piece's hero_top / hero_bottom anchors (the whole opening incl. its round top); the round top is filled with
-- MU.DOME_SLICES strips that narrow like the arch. Fine tuning, in design pixels unless noted:
--   dx, dy, dw, dh  move / resize the arch box;  dome  height of the round top (-1: from the frame's / the "safe" anchor);
--   round  1 = elliptic top, 0.7 = the pointed golden arch, larger = flatter sides;  cx, cy  centre of the portrait in the card image (0..1);
--   zoom  portrait height as a share of the card image height;  ox, oy  move the arch cut-out (arch_top /
--   arch_bottom, laid over the portrait so the field's own frame hides its edges).
-- With the golden arch (arch_top / arch_bottom, anchors "opening" and "straight") the box is the frame's outer edge
-- on the board's alcove (hero_top / hero_bottom) and the portrait fills the frame's opening; round and the dome
-- (-1: from the frame's "straight" anchor) are fitted to that arch. The portrait reaches MU.ARCH_BLEED px under the
-- golden frame on the sides, the top and the bottom so no gap shows between them.
MU.ARCH = { dx = -3, dy = -6, dw = 6, dh = 7, dome = -1, round = 0.7, cx = 0.5, cy = 0.315, zoom = 0.255,
    ox = 0, oy = 0 }
MU.ARCH_REF_W, MU.ARCH_REF_H = 1170, 639   -- the field picture (board minus FIELD_INSET) the tuning above was made on (a 1400 px wide, 800 px high window)
MU.DOME_SLICES = 20
MU.ARCH_BLEED = 3             -- pixels the hero portrait reaches under the golden frame beyond its opening
MU.OPP_ARCH_DY = 6               -- the opponent's arch box stands this many pixels lower (the new board's top alcove sits lower than the bottom one)
MU.FIELD_INSET = 4             -- the board_field texture's distance from the edges of the field area
MU.FIELD_CROP_TOP = 0.06       -- share of the field picture's height that lies above the board's top: the picture, its arches and rows move up by that much
MU.FIELD_TOP_LIMIT = 24        -- most pixels the picture reaches above the board: up to the window's inner top edge (MU.TOP 36 minus the border's 12), not over the border
MU.HEAD_LEVEL = 3             -- frame level (above the window's) of the title and the buttons in the title bar, which the field picture overlaps
-- Pixels the field picture reaches above the board's top, for a field of height fh.
function MU.FieldOverflow(fh)
    local c = MU.FIELD_CROP_TOP
    return math.floor((fh - 2 * MU.FIELD_INSET) * c / (1 - c) + 0.5)
end
-- A vertical share of the whole field picture (an anchor's top / bottom) as a share of the part of it below the board's top.
function MU.FieldY(share)
    local c = MU.FIELD_CROP_TOP
    return (share - c) / (1 - c)
end
-- The same for an anchor table: a copy with its top and bottom remapped.
function MU.CropAnchor(a)
    if type(a) ~= "table" then
        return a
    end
    return { left = a.left, right = a.right, top = MU.FieldY(a.top), bottom = MU.FieldY(a.bottom) }
end
MU.CROP_SPELL = { 0.27, 0.73, 0.18, 0.46 }
-- the round art window of a hero power card (centre (128, 112), radius 44 of the 256x388 image): the square inside it.
-- (CROP_SPELL reached down into the name ribbon, which showed as odd colours at the bottom of the ring.)
MU.CROP_POWER = { 0.328, 0.672, 0.175, 0.402 }
-- hero arches of the board field (fractions of the board area; the owner's field art puts them here)
MU.DEFAULT_ANCHORS = {
    hero_top_safe = { left = 0.44789, top = 0.16547, right = 0.54342, bottom = 0.26799 },
    hero_bottom_safe = { left = 0.44789, top = 0.76619, right = 0.54342, bottom = 0.88489 },
}

local GEM_COLORS = {
    attack = { 0.78, 0.58, 0.06 }, health = { 0.72, 0.12, 0.12 }, mana = { 0.12, 0.32, 0.8 },
    armor = { 0.5, 0.5, 0.56 },
}
local CLASS_COLORS = {
    DRUID = { 0.45, 0.3, 0.12 }, HUNTER = { 0.2, 0.4, 0.15 }, MAGE = { 0.15, 0.3, 0.6 },
    PALADIN = { 0.7, 0.55, 0.25 }, PRIEST = { 0.6, 0.6, 0.6 }, ROGUE = { 0.5, 0.5, 0.15 },
    SHAMAN = { 0.1, 0.3, 0.6 }, WARLOCK = { 0.4, 0.15, 0.5 }, WARRIOR = { 0.55, 0.15, 0.15 },
    NEUTRAL = { 0.4, 0.33, 0.22 },
}
local GLOW_COLORS = { green = { 0.3, 1, 0.3 }, red = { 1, 0.25, 0.25 }, gold = { 1, 0.85, 0.2 },
    combo = { 1, 0.92, 0.1 }, ice = { 0.55, 0.85, 1 } }

-- State markers on minions and heroes: small icons of the WoW spell icon set in a row at the
-- top of the frame, in this order; a tint over the art, a coloured rim and a hover line for each. The icons
-- are standard 1.12 client textures (Interface\Icons); without one the frame still shows the tint, rim and
-- the hover text.
MU.MARKERS = {
    frozen = { icon = "Interface\\Icons\\Spell_Frost_FrostNova", name = "Frozen", tint = { 0.45, 0.7, 1, 0.45 },
        rim = { 0.55, 0.8, 1 } },
    immune = { icon = "Interface\\Icons\\Spell_Holy_SealOfProtection", name = "Immune", tint = { 1, 0.85, 0.3, 0.22 },
        rim = { 1, 0.85, 0.2 } },
    stealth = { icon = "Interface\\Icons\\Ability_Stealth", name = "Stealth" },
    poisonous = { icon = "Interface\\Icons\\Ability_Poisons", name = "Poisonous" },
    silenced = { icon = "Interface\\Icons\\Spell_Holy_Silence", name = "Silenced", tint = { 0.3, 0.3, 0.3, 0.45 } },
    enraged = { icon = "Interface\\Icons\\Spell_Shadow_UnholyFrenzy", name = "Enraged", tint = { 1, 0.15, 0.1, 0.25 } },
    cantAttack = { icon = "Interface\\Icons\\Ability_Warrior_Disarm", name = "Can't attack" },
}
MU.MARKER_ORDER = { "frozen", "immune", "stealth", "poisonous", "silenced", "enraged", "cantAttack" }
MU.MAX_MARKS = 6
-- The keyword icons of a played minion, at the bottom centre of its frame: a lightning bolt for a continuous effect
-- (AURA) and a skull for a deathrattle, as in the game. A silenced minion shows none. The owner's emblems are the
-- pieces; without them the spell icon from Interface\Icons is shown.
MU.MECHANIC_ICONS = {
    DEATHRATTLE = { piece = "kw_deathrattle", icon = "Interface\\Icons\\Spell_Shadow_DeathCoil", name = "Deathrattle" },
    AURA = { piece = "kw_continuous", icon = "Interface\\Icons\\Spell_Nature_Lightning", name = "Continuous effect", size = 24, lift = -5 },
}
MU.MECHANIC_ORDER = { "DEATHRATTLE", "AURA" }
MU.MECHANIC_SIZE = 16         -- pixels the icon is high (its width follows the piece's aspect)
MU.MECHANIC_GAP = 2
MU.MECHANIC_LIFT = 3          -- pixels between the icons and the bottom of the frame
MU.SILENCE_ALPHA = 0.6        -- strength of the pink swirl over the portrait of a silenced minion
MU.STEALTH_ALPHA = 0.5
MU.MAX_SECRETS = 5            -- secret icons shown per side (the rules allow 5)
MU.SECRET_SIZE = 22
-- Where the secret medallions sit on the hero's dome, in the order they are placed (degrees from the top, negative =
-- left): the first on top of the dome, the next ones alternate left and right and go down along the arch, like the
-- original game (centre, upper left, upper right, lower left, lower right).
MU.SECRET_ANGLES = { 0, -64, 64, -94, 94 }
MU.SECRET_LIFT = 0            -- pixels the icons sit outside the dome's edge (their centres are on it)
MU.SECRET_PIECES = { PALADIN = "secret_paladin", HUNTER = "secret_hunter", MAGE = "secret_mage" }
MU.CHOOSE_W = MU.MULL_W       -- option cards of the Choose One chooser: the mulligan's size (scaled down on a narrow board)

MU.BANNER_SHARE = 0.40        -- the Choose One scroll banner: its width as a share of the board (the art fills about a sixth of its height)
MU.CHOOSE_SHARE = 0.185       -- an option card: its width as a share of the board (the reference's size)
MU.CHOOSE_STEP = 0.29         -- the distance between the centres of two option cards, as a share of the board
MU.CHOOSE_CENTER_Y = 0.52     -- the row of option cards is centred this far down the board
MU.CHOOSE_DIM = 0.25          -- the dimming of the board behind the chooser
MU.PLATE_W, MU.PLATE_H = 130, 76      -- the icy Hide / Show plates (art is 1448 : 1086; drawn less tall on purpose)
MU.PICK_W = 134               -- cards of the pending pick panel
MU.PICK_SLOTS = 5
MU.OPP_CHOOSING = "Opponent is choosing..."
local RESULT_COLORS = { win = { 1, 0.82, 0 }, loss = { 1, 0.25, 0.25 }, draw = { 0.85, 0.85, 0.85 }, int = { 1, 0.6, 0.2 } }

-- The end-of-match emblems (owner decision): a golden Victory emblem (also for a draw) or the Defeat emblem
-- (also for an interrupted match) with the player's hero portrait behind its arch. Textures\UI\VictoryEmblem /
-- DefeatEmblem (tools/art/ui_icons.py makes them and prints these numbers; the deploy script copies them):
--   right, bottom  the emblem's share of the 512 x 512 texture;  aspect  its width / height;
--   arch  the transparent opening (left, top, right, bottom as fractions of the emblem);
--   strips  the portrait in horizontal bands that follow the round top of the arch (x1, y1, x2, y2 as fractions of the
--   arch box, a little wider than the opening so the frame hides the edges; a plain rectangle would stick out at the top).
-- Without the textures the old dialog box is shown.
MU.EMBLEM_PATH = "Interface\\AddOns\\Hearthstone\\Textures\\UI\\"
MU.EMBLEM_H = 380             -- height of the emblem on screen
MU.EMBLEM_BOX_W = 460         -- width of the result block (the emblem, the reason, the summary, Leave)
MU.EMBLEM_MAX_STRIPS = 24     -- portrait strips that can be created (the emblems use 21)
-- The art window of a hero card (256 x 388 render, measured on HERO_01 ... 09): centre x 134 (right of the card's
-- middle), centre y 138, 106 px wide (the opening is 110 x 130, a little is left out so the card's frame never shows).
MU.EMBLEM_CROP = { cx = 134 / 256, cy = 138 / 388, w = 106 / 256 }
MU.RESULT_EMBLEM = { win = "victory", draw = "victory", loss = "defeat", int = "defeat" }
MU.EMBLEMS = {
    victory = {
        file = "VictoryEmblem", right = 1.00000, bottom = 0.88086, aspect = 1.13525,
        arch = { 0.34460, 0.19757, 0.64798, 0.61049 },
        strips = {
            { 0.3587, -0.0068, 0.6495, 0.0227 },
            { 0.3016, 0.0227, 0.7038, 0.0431 },
            { 0.2554, 0.0431, 0.7473, 0.0658 },
            { 0.2174, 0.0658, 0.7826, 0.0884 },
            { 0.1875, 0.0884, 0.8125, 0.1088 },
            { 0.1603, 0.1088, 0.8424, 0.1315 },
            { 0.1359, 0.1315, 0.8668, 0.1542 },
            { 0.1141, 0.1542, 0.8886, 0.1769 },
            { 0.0951, 0.1769, 0.9076, 0.1973 },
            { 0.0788, 0.1973, 0.9239, 0.2200 },
            { 0.0625, 0.2200, 0.9402, 0.2426 },
            { 0.0489, 0.2426, 0.9538, 0.2630 },
            { 0.0380, 0.2630, 0.9647, 0.2857 },
            { 0.0272, 0.2857, 0.9728, 0.3084 },
            { 0.0163, 0.3084, 0.9810, 0.3311 },
            { 0.0082, 0.3311, 0.9891, 0.3515 },
            { 0.0027, 0.3515, 0.9946, 0.3741 },
            { -0.0027, 0.3741, 1.0000, 0.3968 },
            { -0.0027, 0.3968, 1.0027, 0.4172 },
            { -0.0054, 0.4172, 1.0054, 0.4399 },
            { -0.0082, 0.4399, 1.0082, 1.0068 },
        },
    },
    defeat = {
        file = "DefeatEmblem", right = 1.00000, bottom = 0.98633, aspect = 1.01386,
        arch = { 0.33167, 0.19358, 0.65167, 0.56720 },
        strips = {
            { 0.3542, -0.0068, 0.6536, 0.0226 },
            { 0.2943, 0.0226, 0.7135, 0.0452 },
            { 0.2500, 0.0452, 0.7604, 0.0679 },
            { 0.2135, 0.0679, 0.7943, 0.0905 },
            { 0.1823, 0.0905, 0.8229, 0.1131 },
            { 0.1536, 0.1131, 0.8516, 0.1380 },
            { 0.1276, 0.1380, 0.8750, 0.1606 },
            { 0.1068, 0.1606, 0.8932, 0.1833 },
            { 0.0885, 0.1833, 0.9141, 0.2059 },
            { 0.0729, 0.2059, 0.9297, 0.2285 },
            { 0.0573, 0.2285, 0.9427, 0.2511 },
            { 0.0443, 0.2511, 0.9557, 0.2738 },
            { 0.0339, 0.2738, 0.9661, 0.2964 },
            { 0.0234, 0.2964, 0.9740, 0.3190 },
            { 0.0130, 0.3190, 0.9844, 0.3439 },
            { 0.0078, 0.3439, 0.9896, 0.3665 },
            { 0.0026, 0.3665, 0.9948, 0.3891 },
            { -0.0026, 0.3891, 1.0000, 0.4118 },
            { -0.0052, 0.4118, 1.0026, 0.4344 },
            { -0.0052, 0.4344, 1.0026, 0.4570 },
            { -0.0078, 0.4570, 1.0078, 1.0068 },
        },
    },
}

MU.frame = nil
MU.ui = nil
MU.sel = nil                  -- targeting: { kind ("p", "a" or "h"), source, entry, seq, targetSet, choice }
MU.choose = nil               -- the Choose One chooser is open for this hand card: { source, seq }
MU.chooserHidden = nil        -- Hide was clicked on the chooser: its options are out of sight, the board stays blocked
MU.hint = nil                 -- the transient line in the message bubble
MU.hiding = false             -- the window is being hidden by this file (not by Escape)
MU.reshowing = false
MU.hoverId = nil

-- ------------------------------------------------------------------ state markers
-- The marker keys that apply to a minion or hero (fields frozen, immune, stealth, ...), in marker order. noFrozen: left
-- out of the list (the ice ring shows it; no small icon).
function MU.StateKeys(c, noFrozen)
    local out = {}
    for i = 1, table.getn(MU.MARKER_ORDER) do
        local key = MU.MARKER_ORDER[i]
        if c[key] and not (noFrozen and key == "frozen") then
            table.insert(out, key)
        end
    end
    return out
end

-- The keyword keys (DEATHRATTLE, AURA) of a minion's card that show an icon, in MU.MECHANIC_ORDER. Silenced: none.
-- The lightning (AURA) also marks a triggered effect ("Whenever ...", "At the end of your turn ..."): the card data tags
-- those TRIGGER_VISUAL (data/cards/overrides/001_trigger_visual.json). One lightning, however many tags the card has.
function MU.MechanicKeys(cardId, silenced)
    local out = {}
    if silenced then
        return out
    end
    for i = 1, table.getn(MU.MECHANIC_ORDER) do
        local key = MU.MECHANIC_ORDER[i]
        if MA.HasMechanic(cardId, key) or (key == "AURA" and MA.HasMechanic(cardId, "TRIGGER_VISUAL")) then
            table.insert(out, MU.MECHANIC_ORDER[i])
        end
    end
    return out
end

-- The tint over the art of a character: r, g, b, a of the strongest tinting state, or nil.
function MU.TintOf(c)
    for i = 1, table.getn(MU.MARKER_ORDER) do
        local m = MU.MARKERS[MU.MARKER_ORDER[i]]
        if c[MU.MARKER_ORDER[i]] and m.tint then
            return m.tint[1], m.tint[2], m.tint[3], m.tint[4]
        end
    end
    return nil
end

-- The rim colour of a character (immune gold, frozen icy blue): r, g, b or nil. noFrozen: the frozen rim is left out
-- (the ice ring picture shows that state instead).
function MU.RimOf(c, noFrozen)
    for i = 1, table.getn(MU.MARKER_ORDER) do
        local m = MU.MARKERS[MU.MARKER_ORDER[i]]
        if c[MU.MARKER_ORDER[i]] and m.rim and not (noFrozen and MU.MARKER_ORDER[i] == "frozen") then
            return m.rim[1], m.rim[2], m.rim[3]
        end
    end
    return nil
end

-- "Frozen, Stealth" for the hover text, or nil.
function MU.StateNames(c)
    local names = {}
    local keys = MU.StateKeys(c)
    for i = 1, table.getn(keys) do
        table.insert(names, MU.MARKERS[keys[i]].name)
    end
    if table.getn(names) == 0 then
        return nil
    end
    return table.concat(names, ", ")
end

-- ------------------------------------------------------------------ board pieces
-- The HS_BoardTextures entry of a piece, or nil when it is not installed.
-- The golden hero arch of the duel is the one of the arena (the ArenaArch texture, ArenaUI.lua AU.ART.ArenaArch): the
-- arch_top / arch_bottom pieces are answered with it as a piece of its own (the same anchors as the board's own arch
-- frame piece: "opening" for the portrait and "straight" where its sides stop curving), so the frame, its glows and the
-- portrait box all follow it. The board's own arch frame is the fallback while the texture is not installed.
local ARENA_ARCH_PIECES = { arch_top = true, arch_bottom = true }

function MU.ArenaArchPiece()
    local AU = H.ArenaUI
    local art = AU and AU.ART and AU.ART.ArenaArch
    if not art or not AU.TEXTURE_PATH or not CreateFrame then
        return nil
    end
    if MU.arenaArchInstalled == nil then
        local probe = CreateFrame("Frame")
        local tex = probe:CreateTexture(nil, "ARTWORK")
        MU.arenaArchInstalled = tex:SetTexture(AU.TEXTURE_PATH .. "ArenaArch") and true or false
    end
    if not MU.arenaArchInstalled then
        return nil
    end
    local o = art.opening
    local last = art.strips[table.getn(art.strips)]
    return {
        path = AU.TEXTURE_PATH .. "ArenaArch", right = art.right, bottom = art.bottom, arena = art,
        anchors = {
            opening = { left = o[1], top = o[2], right = o[3], bottom = o[4] },
            straight = { left = o[1], top = o[2] + last[2] * (o[4] - o[2]), right = o[3], bottom = o[4] },
        },
    }
end

function MU.Piece(name)
    if ARENA_ARCH_PIECES[name] then
        local arena = MU.ArenaArchPiece()
        if arena then
            return arena
        end
    end
    local t = HS_BoardTextures
    if type(t) ~= "table" or type(t.pieces) ~= "table" then
        return nil
    end
    local p = t.pieces[name]
    if type(p) ~= "table" or type(p.path) ~= "string" or p.path == "" then
        return nil
    end
    return p
end

-- Puts a piece into a texture. Returns false (texture untouched) when the piece is not installed.
function MU.SetPiece(tex, name)
    local p = MU.Piece(name)
    if not p then
        return false
    end
    tex:SetTexture(p.path)
    local right, bottom = p.right, p.bottom
    if type(right) ~= "number" or type(bottom) ~= "number" then
        right, bottom = 1, 1
    end
    tex:SetTexCoord(0, right, 0, bottom)
    tex:SetVertexColor(1, 1, 1)
    return true
end

-- The speech bubble (the speech_bubble piece, 192x128 texels, made by tools/art/slice_board.py): the tail sits at the
-- bottom left. It is shown as three parts of one texture: the left end (with the tail) and the right end keep their
-- size, the middle is stretched to the width. MU.SetBubbleParts makes the three textures of a bubble frame (false when
-- the piece is not installed); MU.PlaceBubble fits them to the frame's current size.
MU.BUBBLE_W, MU.BUBBLE_H = 192, 128
MU.BUBBLE_CAP_L, MU.BUBBLE_CAP_R = 76, 24
MU.BUBBLE_HEIGHT = 56

function MU.SetBubbleParts(frame)
    local p = MU.Piece("speech_bubble")
    if not p then
        return false
    end
    frame.bubbleParts = {
        piece = p,
        left = frame:CreateTexture(nil, "BACKGROUND"),
        middle = frame:CreateTexture(nil, "BACKGROUND"),
        right = frame:CreateTexture(nil, "BACKGROUND"),
    }
    for _, tex in ipairs({ frame.bubbleParts.left, frame.bubbleParts.middle, frame.bubbleParts.right }) do
        tex:SetTexture(p.path)
        tex:SetVertexColor(1, 1, 1)
    end
    return true
end

MU.BUBBLE_MIN_W, MU.BUBBLE_MAX_W = 180, 420
MU.BUBBLE_TIP_X = 22            -- the tail's tip, texels from the bubble's left edge

-- Fits the bubble to its text: a long line wraps at MU.BUBBLE_MAX_W, and the bubble grows taller for more lines (the
-- text keeps 10 pixels of room above and below). The 1.12 client has no FontString:GetStringHeight, so the lines are
-- counted from the unwrapped width. Then it puts the tail's tip on the top middle of the hero's portrait, a few pixels
-- above it.
MU.BUBBLE_LINE_H = 14
function MU.PlaceSpeech(bubble, text, hero)
    local full = text:GetStringWidth()
    local w = math.min(MU.BUBBLE_MAX_W, math.max(MU.BUBBLE_MIN_W, math.ceil(full + 40)))
    local lines = math.max(1, math.ceil(full / (w - 28)))
    bubble:SetWidth(w)
    bubble:SetHeight(math.max(MU.BUBBLE_HEIGHT, lines * MU.BUBBLE_LINE_H + 20))
    MU.PlaceBubble(bubble)
    local tipX = MU.BUBBLE_TIP_X * bubble:GetHeight() / MU.BUBBLE_H
    bubble:ClearAllPoints()
    bubble:SetPoint("BOTTOMLEFT", hero, "TOP", -tipX, 4)
end

function MU.PlaceBubble(frame)
    local parts = frame.bubbleParts
    if not parts then
        return
    end
    local p = parts.piece
    local w, h = frame:GetWidth(), frame:GetHeight()
    local s = h / MU.BUBBLE_H
    local capL, capR = MU.BUBBLE_CAP_L * s, MU.BUBBLE_CAP_R * s
    local function u(x)
        return x / MU.BUBBLE_W * p.right
    end
    local function v(y)
        return y / MU.BUBBLE_H * p.bottom
    end
    parts.left:ClearAllPoints()
    parts.left:SetPoint("TOPLEFT", frame, "TOPLEFT", 0, 0)
    parts.left:SetWidth(capL)
    parts.left:SetHeight(h)
    parts.left:SetTexCoord(u(0), u(MU.BUBBLE_CAP_L), v(0), v(MU.BUBBLE_H))
    parts.middle:ClearAllPoints()
    parts.middle:SetPoint("TOPLEFT", frame, "TOPLEFT", capL, 0)
    parts.middle:SetWidth(math.max(0, w - capL - capR))
    parts.middle:SetHeight(h)
    parts.middle:SetTexCoord(u(MU.BUBBLE_CAP_L), u(MU.BUBBLE_W - MU.BUBBLE_CAP_R), v(0), v(MU.BUBBLE_H))
    parts.right:ClearAllPoints()
    parts.right:SetPoint("TOPRIGHT", frame, "TOPRIGHT", 0, 0)
    parts.right:SetWidth(capR)
    parts.right:SetHeight(h)
    parts.right:SetTexCoord(u(MU.BUBBLE_W - MU.BUBBLE_CAP_R), u(MU.BUBBLE_W), v(0), v(MU.BUBBLE_H))
end

local function validAnchor(a)
    return type(a) == "table" and type(a.left) == "number" and type(a.right) == "number"
        and type(a.top) == "number" and type(a.bottom) == "number"
        and a.left >= 0 and a.right <= 1 and a.left < a.right and a.top >= 0 and a.bottom <= 1 and a.top < a.bottom
end

-- The named anchor of a board piece (fractions of the used image), or nil.
function MU.PieceAnchor(piece, name)
    local p = MU.Piece(piece)
    local a = p and type(p.anchors) == "table" and p.anchors[name]
    if validAnchor(a) then
        return a
    end
    return nil
end

-- ---- oval minion portraits (made by tools/art/minion_portraits.py, listed in HS_MinionTextures)
MU.MINION_TEXTURE_PATH = "Interface\\AddOns\\Hearthstone\\Textures\\Minions\\"

-- True when the pre-cut oval portrait of a card is installed.
function MU.HasMinionTexture(cardId)
    local t = HS_MinionTextures
    return cardId ~= nil and type(t) == "table" and type(t.set) == "table" and t.set[cardId] == true
end

-- ---- round hero power portraits (made by tools/art/power_portraits.py, listed in HS_PowerTextures)
MU.POWER_TEXTURE_PATH = "Interface\\AddOns\\Hearthstone\\Textures\\Powers\\"

-- True when the pre-cut round portrait of a hero power card is installed.
function MU.HasPowerTexture(cardId)
    local t = HS_PowerTextures
    return cardId ~= nil and type(t) == "table" and type(t.set) == "table" and t.set[cardId] == true
end

-- Places art in the hole of a ring piece that is drawn over frame with pad pixels on every side (the
-- piece's anchor, "inner" unless anchorName says otherwise). Without the piece (or its anchor) the art fills
-- frame minus inset. Returns whether the piece's anchor was used, then the art's rectangle as offsets from the frame's
-- top left corner (left, top (up is positive), right, bottom): the flip of a hero power squashes it (MU.SquashTex).
function MU.ArtInRing(art, frame, pieceName, pad, inset, anchorName, grow)
    local p = MU.Piece(pieceName)
    local inner = p and type(p.anchors) == "table" and p.anchors[anchorName or "inner"]
    grow = grow or 0
    art:ClearAllPoints()
    if validAnchor(inner) then
        local w = frame:GetWidth() + 2 * pad
        local h = frame:GetHeight() + 2 * pad
        local l, t = math.floor(-pad + inner.left * w + 0.5) - grow, math.floor(pad - inner.top * h + 0.5) + grow
        local r, b = math.floor(-pad + inner.right * w + 0.5) + grow, math.floor(pad - inner.bottom * h + 0.5) - grow
        art:SetPoint("TOPLEFT", frame, "TOPLEFT", l, t)
        art:SetPoint("BOTTOMRIGHT", frame, "TOPLEFT", r, b)
        return true, l, t, r, b
    end
    art:SetPoint("TOPLEFT", frame, "TOPLEFT", inset - grow, -(inset - grow))
    art:SetPoint("BOTTOMRIGHT", frame, "BOTTOMRIGHT", -(inset - grow), inset - grow)
    return false, inset - grow, -(inset - grow), frame:GetWidth() - (inset - grow), -(frame:GetHeight() - (inset - grow))
end

-- The hero arch of the board field: left, top, width, height and dome height in field pixels, or nil
-- without the board_field piece (then the hero is a square in its ring).
function MU.HeroArch(which, bw, fh)
    local piece = MU.Piece("board_field")
    if not piece or type(piece.anchors) ~= "table" then
        return nil
    end
    local full = piece.anchors[which == "m" and "hero_bottom" or "hero_top"]
    local safe = piece.anchors[which == "m" and "hero_bottom_safe" or "hero_top_safe"]
    if not validAnchor(full) then
        return nil
    end
    full = MU.CropAnchor(full)
    safe = validAnchor(safe) and MU.CropAnchor(safe) or nil
    local frameName = which == "m" and "arch_bottom" or "arch_top"
    local op = MU.PieceAnchor(frameName, "opening")
    local straight = MU.PieceAnchor(frameName, "straight")
    local a = MU.ARCH
    -- The box is the anchor rectangle of the field picture (the picture lies inside the board with FIELD_INSET on every
    -- side), moved and resized by the tuning dx / dy / dw / dh. The tuning was made on a field of ARCH_REF_W x ARCH_REF_H
    -- pixels, so it scales with the field: a taller or wider window keeps the arch on the art's own arch.
    local i = MU.FIELD_INSET
    local tw, th = bw - 2 * i, fh - 2 * i
    local sx, sy = tw / MU.ARCH_REF_W, th / MU.ARCH_REF_H
    local aw, ah = full.right - full.left, full.bottom - full.top
    -- (the tuning values were made against anchor * board size, which differs from the picture by FIELD_INSET: converted)
    local kx = a.dx + i * (2 * full.left - 1)
    local ky = a.dy + (which == "m" and 0 or MU.OPP_ARCH_DY) + i * (2 * full.top - 1)
    local kw = a.dw + aw * 2 * i
    local kh = a.dh + ah * 2 * i
    local l = math.floor(i + full.left * tw + kx * sx + 0.5)
    local t = math.floor(i + full.top * th + ky * sy + 0.5)
    local w = math.floor(aw * tw + kw * sx + 0.5)
    local h = math.floor(ah * th + kh * sy + 0.5)
    if w < 8 then
        w = 8
    end
    if h < 8 then
        h = 8
    end
    -- a frame of its own (arch_top / arch_bottom, golden): the box above is the frame's outer edge, the portrait
    -- fills the frame's opening and its round top ends where the frame's straight sides begin
    local ow, oh = w, h
    if op then
        l = math.floor(l + op.left * ow + 0.5)
        t = math.floor(t + op.top * oh + 0.5)
        w = math.max(8, math.floor((op.right - op.left) * ow + 0.5))
        h = math.max(8, math.floor((op.bottom - op.top) * oh + 0.5))
    end
    local dome = a.dome
    if dome < 0 then
        dome = 0
        if op and straight and straight.top > op.top then
            dome = (straight.top - op.top) / (op.bottom - op.top) * h
        elseif validAnchor(safe) and safe.top > full.top then
            dome = (safe.top - full.top) * fh
        end
    end
    dome = math.floor(dome + 0.5)
    if dome > h - 4 then
        dome = h - 4
    end
    return l, t, w, h, dome
end

-- Lays out the portrait strips of a hero for a frame of w x h whose top dome pixels round off: one strip for
-- the straight part, MU.DOME_SLICES strips above it, each as wide as the arch at its LOWER edge (a strip may reach a
-- little under the golden frame, which hides it; narrower strips left gaps at the sides and a blunt, cut-off tip).
function MU.LayoutArch(hero, w, h, dome, key)
    local a = MU.ARCH
    local round = a.round
    if round < 0.3 then
        round = 0.3
    end
    -- the portrait reaches `bleed` px beyond the box on the sides and the bottom, and above its top (the golden frame
    -- hides it); without a golden frame it stays inside the box
    local bleed = MU.PieceAnchor(key == "m" and "arch_bottom" or "arch_top", "opening") and MU.ARCH_BLEED or 0
    hero.slices = { { tex = hero.art, x1 = -bleed, y1 = dome, x2 = w + bleed, y2 = h + bleed } }
    local n = table.getn(hero.dome)
    local arena = MU.Piece(key == "m" and "arch_bottom" or "arch_top")
    arena = arena and arena.arena
    if arena then
        -- the arena arch: its portrait strips (fractions of the opening) follow its round top exactly; the last one is the
        -- straight part under them
        local strips = arena.strips
        local ns = table.getn(strips)
        local function put(sl, st)
            sl.x1, sl.y1 = math.floor(w * st[1] + 0.5), math.floor(h * st[2] + 0.5)
            sl.x2, sl.y2 = math.floor(w * st[3] + 0.5), math.floor(h * st[4] + 0.5)
        end
        put(hero.slices[1], strips[ns])
        for i = 1, n do
            local tex = hero.dome[i]
            if i < ns then
                local sl = { tex = tex }
                put(sl, strips[i])
                table.insert(hero.slices, sl)
            else
                tex:Hide()
            end
        end
        n = 0
    end
    for i = 1, n do
        local tex = hero.dome[i]
        local y1 = math.floor(dome * (i - 1) / n + 0.5)
        local y2 = math.floor(dome * i / n + 0.5)
        local half = 0
        if dome > 0 then
            local t = (dome - y2) / dome
            local k = 1 - t * t
            if k > 0 then
                half = (w / 2) * math.exp(math.log(k) / (2 * round))
            end
        end
        local x1 = math.floor(w / 2 - half + 0.5) - bleed
        local x2 = math.floor(w / 2 + half + 0.5) + bleed
        if i == 1 then
            y1 = -bleed
        end
        if y2 > y1 and x2 > x1 then
            table.insert(hero.slices, { tex = tex, x1 = x1, y1 = y1, x2 = x2, y2 = y2 })
        else
            tex:Hide()
        end
    end
    for i = 1, table.getn(hero.slices) do
        local sl = hero.slices[i]
        sl.tex:ClearAllPoints()
        sl.tex:SetPoint("TOPLEFT", hero.frame, "TOPLEFT", sl.x1, -sl.y1)
        sl.tex:SetPoint("BOTTOMRIGHT", hero.frame, "TOPLEFT", sl.x2, -sl.y2)
    end
    hero.archW, hero.archH, hero.domePx, hero.bleed = w, h, dome, bleed
end

-- Puts the hero portrait into the strips of an arch frame (the class colour without a card texture).
function MU.SetHeroArt(hero, cardId)
    local w, h = hero.archW or 1, hero.archH or 1
    -- an alternative portrait of the player's own hero (HsHeroes): its texture and crop bottom
    local altPath, altBottom = HsHeroes and HsHeroes.Texture(cardId)
    local has = cardId and (altPath ~= nil or CM.HasTexture(cardId))
    local u1, u2, v1, v2 = 0, 1, 0, 1
    if has then
        local a = MU.ARCH
        local bottom = altBottom or 388 / 512
        if not altBottom and H.CardBottom then
            bottom = H.CardBottom(cardId)
        end
        -- the portrait has the frame's aspect: zoom of the image height, as wide as that needs (256 x 388 image)
        -- (the picture covers the box plus the bleed on every side)
        local bl = hero.bleed or 0
        local vh = a.zoom
        local uw = vh * 388 * ((w + 2 * bl) / (h + 2 * bl)) / 256
        u1, u2 = a.cx - uw / 2, a.cx + uw / 2
        v1, v2 = (a.cy - vh / 2) * bottom, (a.cy + vh / 2) * bottom
    end
    local c = MU.ClassColor(cardId)
    for i = 1, table.getn(hero.slices or {}) do
        local sl = hero.slices[i]
        if has then
            sl.tex:SetTexture(altPath or ((H.CARD_TEXTURE_PATH or "Interface\\AddOns\\Hearthstone\\Textures\\Cards\\") .. cardId))
            local bl = hero.bleed or 0
            sl.tex:SetTexCoord(u1 + (u2 - u1) * (sl.x1 + bl) / (w + 2 * bl), u1 + (u2 - u1) * (sl.x2 + bl) / (w + 2 * bl),
                v1 + (v2 - v1) * (sl.y1 + bl) / (h + 2 * bl), v1 + (v2 - v1) * (sl.y2 + bl) / (h + 2 * bl))
            sl.tex:SetVertexColor(1, 1, 1)
        else
            sl.tex:SetTexture(WHITE)
            sl.tex:SetTexCoord(0, 1, 0, 1)
            sl.tex:SetVertexColor(c[1], c[2], c[3])
        end
        sl.tex:Show()
    end
    return has
end

-- Lays the arch cut-out of the board art (arch_top / arch_bottom: the field's frame with a transparent opening)
-- over the hero portrait, exactly where it is in the field texture. Hidden without the pieces. The overlay is
-- anchored to the hero frame (offsets from its home), so it moves with a lunge or a shake of the hero and is back
-- at its place when the frame is.
function MU.PlaceArchOverlay(key, hero, bw, fh)
    local tex = hero.overlay
    local name = key == "m" and "arch_bottom" or "arch_top"
    local field = MU.Piece("board_field")
    local r = field and type(field.anchors) == "table" and field.anchors[name .. "_frame"]
    local opening = MU.PieceAnchor(name, "opening")
    if not tex or not MU.Piece(name) or not (opening or validAnchor(r)) then
        if tex then
            tex:Hide()
        end
        return false
    end
    local a = MU.ARCH
    local home = hero.home or { x = 0, y = 0 }
    local l, t, rr, b
    if opening and hero.archW and hero.archH then
        -- a frame of its own (the golden gothic arch): its opening lies exactly over the portrait box, whatever
        -- the box is tuned to, so the frame always hugs the portrait (offsets from the hero frame's top left)
        local fw = hero.archW / (opening.right - opening.left)
        local fh2 = hero.archH / (opening.bottom - opening.top)
        l = math.floor(a.ox - opening.left * fw + home.x + 0.5)
        t = math.floor(a.oy - opening.top * fh2 - home.y + 0.5)
        rr = math.floor(l + fw + 0.5)
        b = math.floor(t + fh2 + 0.5)
    else
        -- the cut-out of the board's own frame, where it is in the field texture (board position, down positive)
        local i = MU.FIELD_INSET
        local tw, th = bw - 2 * i, fh - 2 * i
        l = math.floor(i + r.left * tw + a.ox + 0.5)
        t = math.floor(i + MU.FieldY(r.top) * th + a.oy + 0.5)
        rr = math.floor(i + r.right * tw + a.ox + 0.5)
        b = math.floor(i + MU.FieldY(r.bottom) * th + a.oy + 0.5)
    end
    tex:ClearAllPoints()
    tex:SetPoint("TOPLEFT", hero.frame, "TOPLEFT", l - home.x, -(t + home.y))
    tex:SetPoint("BOTTOMRIGHT", hero.frame, "TOPLEFT", rr - home.x, -(b + home.y))
    MU.SetPiece(tex, name)
    tex:Show()
    return true
end

-- Centre (x, y) and size of a hero arch in a board area of bw x bh pixels. which: "o" (top arch) or
-- "m" (bottom arch). Uses the board_field anchors (the "safe" rectangle) when the piece has them.
function MU.HeroRect(which, bw, bh)
    local name = "hero_top_safe"
    if which == "m" then
        name = "hero_bottom_safe"
    end
    local a = MU.DEFAULT_ANCHORS[name]
    local piece = MU.Piece("board_field")
    if piece and type(piece.anchors) == "table" and validAnchor(piece.anchors[name]) then
        a = piece.anchors[name]
    end
    local size = (a.bottom - a.top) * bh
    if size < MU.HERO_MIN then
        size = MU.HERO_MIN
    end
    return math.floor((a.left + a.right) / 2 * bw + 0.5), math.floor((a.top + a.bottom) / 2 * bh + 0.5), math.floor(size + 0.5)
end

-- Diameter of the hero ring: the size the hero_ring piece gets around the hero (the hero rect width of
-- MU.HeroRect plus the ring's reach on both sides).
function MU.HeroRingSize(bw, fh)
    local _, _, size = MU.HeroRect("m", bw, fh)
    return size + 2 * MU.HERO_RING_PAD
end

-- Size of the hero power button: its ring (the hero_power_ring piece reaches POWER_RING_PAD beyond the
-- button) is as big as the hero ring (owner decision).
function MU.PowerSize(bw, fh)
    return MU.HeroRingSize(bw, fh) - 2 * MU.POWER_RING_PAD
end

-- Top of a side's minion row (field pixels). With the board art both rows sit between the hero arch frames
-- (the arch_top_frame / arch_bottom_frame anchors of board_field): the opponent's row hangs MU.ROW_CLEAR below
-- the top frame, the player's row stands MU.ROW_CLEAR above the bottom frame. Without the art the old shares
-- of the field are used. key: "o" or "m".
function MU.RowTop(key, fh)
    local top = MU.PieceAnchor("board_field", "arch_top_frame")
    local bottom = MU.PieceAnchor("board_field", "arch_bottom_frame")
    if top and bottom then
        local th = fh - 2 * MU.FIELD_INSET
        if key == "m" then
            return math.floor(MU.FIELD_INSET + MU.FieldY(bottom.top) * th - MU.ROW_CLEAR - MU.MY_ROW_LIFT - MU.MINION_H + 0.5)
        end
        return math.floor(MU.FIELD_INSET + MU.FieldY(top.bottom) * th + MU.ROW_CLEAR - MU.OPP_ROW_LIFT + 0.5)
    end
    if key == "m" then
        return math.floor(fh * MU.MY_BOARD_Y)
    end
    return math.floor(fh * MU.OPP_BOARD_Y)
end

-- Left and right edge of the hero's outer frame in board pixels (the arch frame of the board art, else the hero
-- ring around the hero square): the hero power and the weapon keep clear of it.
function MU.SideBounds(key, bw, fh)
    local a = MU.PieceAnchor("board_field", key == "m" and "arch_bottom_frame" or "arch_top_frame")
    if a then
        local tw = bw - 2 * MU.FIELD_INSET
        return math.floor(MU.FIELD_INSET + a.left * tw + 0.5), math.floor(MU.FIELD_INSET + a.right * tw + 0.5)
    end
    local al, _, aw = MU.HeroArch(key, bw, fh)
    if al then
        return al - 8, al + aw + 8
    end
    local cx, _, size = MU.HeroRect(key, bw, fh)
    return cx - math.floor(size / 2) - MU.HERO_RING_PAD, cx + math.floor(size / 2) + MU.HERO_RING_PAD
end

-- The window's background: the blue cloth of the owner's table runner, six 512 x 512 tiles (cloth_c<col>r<row> of
-- HS_BoardTextures, 3 columns, 2 rows) stretched over the window inside its frame. Without all six the dark dialog
-- background stays.
MU.CLOTH_COLS, MU.CLOTH_ROWS, MU.CLOTH_INSET = 3, 2, 4
function MU.ClothInstalled()
    for r = 0, MU.CLOTH_ROWS - 1 do
        for c = 0, MU.CLOTH_COLS - 1 do
            if not MU.Piece("cloth_c" .. c .. "r" .. r) then
                return false
            end
        end
    end
    return true
end

function MU.LayoutCloth()
    local f = MU.frame
    local tiles = MU.cloth
    if not f or not tiles then
        return
    end
    local i = MU.CLOTH_INSET
    local w, h = MU.geom.w - 2 * i, MU.geom.h - 2 * i
    for k = 1, table.getn(tiles) do
        local t = tiles[k]
        local x1, x2 = i + math.floor(t.c * w / MU.CLOTH_COLS), i + math.floor((t.c + 1) * w / MU.CLOTH_COLS)
        local y1, y2 = i + math.floor(t.r * h / MU.CLOTH_ROWS), i + math.floor((t.r + 1) * h / MU.CLOTH_ROWS)
        t.tex:ClearAllPoints()
        t.tex:SetPoint("TOPLEFT", f, "TOPLEFT", x1, -y1)
        t.tex:SetPoint("BOTTOMRIGHT", f, "TOPLEFT", x2, -y2)
    end
end

-- Scale, width and height of the window for a screen: about 80% of it, never narrower than MIN_W
-- design pixels, so a 4:3 and a 16:9 screen both get a fitting layout.
function MU.Fit(screenW, screenH)
    local sw = screenW or 1024
    local sh = screenH or 768
    local s = math.min(MU.FILL * sw / MU.MIN_W, MU.FILL * sh / MU.DESIGN_H)
    local w = math.floor(MU.FILL * sw / s)
    if w > MU.MAX_W then
        w = MU.MAX_W
    end
    if w < MU.MIN_W then
        w = MU.MIN_W
    end
    return math.floor(s * 1000) / 1000, w, MU.DESIGN_H
end

-- Width and height of the board area inside a window of width w (the field plus the hand strip).
function MU.BoardSize(w)
    return w - 2 * MU.PAD - MU.LOG_W - MU.GAP, MU.DESIGN_H - MU.TOP - MU.PAD
end

-- Height of the field: the board area without the hand strip at its bottom.
function MU.FieldHeight(bh)
    return bh - MU.HAND_STRIP
end

-- Positions of n resting hand cards as board pixels from the top left of MU.board: { x, y, angle } is the top left
-- corner of each card (before it is tilted) and its lean in radians (clockwise), in a fan (the middle card highest and
-- upright, the outer ones lower and leaning outwards). Neighbours overlap: each card is 'step' of its width to the
-- right of the previous one, squeezed further once the row would be wider than HAND_ROW_W.
function MU.HandLayout(n, bw, bh)
    local out = {}
    if n < 1 then
        return out
    end
    local set = MU.HAND_FAN
    local rw = bw - 16
    if rw > MU.HAND_ROW_W then
        rw = MU.HAND_ROW_W
    end
    local step = set.step * MU.HAND_W
    if n > 1 and (n - 1) * step + MU.HAND_W > rw then
        step = (rw - MU.HAND_W) / (n - 1)
    end
    local x0 = MU.CenterX(bw) - ((n - 1) * step + MU.HAND_W) / 2
    local base = bh - set.base - MU.HAND_H
    for i = 1, n do
        local t = 0
        if n > 1 then
            t = (i - (n + 1) / 2) / ((n - 1) / 2)
        end
        out[i] = { x = math.floor(x0 + (i - 1) * step + 0.5), y = base + math.floor(set.arc * t * t + 0.5),
            angle = math.rad(set.tilt * t) }
    end
    return out
end

-- The y (board pixels) above which a dragged card is played: the top of the resting hand cards plus a margin.
function MU.PlayLine(bh)
    return bh - MU.HAND_FAN.base - MU.HAND_H - MU.PLAY_MARGIN
end

-- The x (board pixels) the rows of minions and cards are centred on: the middle of the hero arch frames of the board
-- art (the arena is not symmetric: its arches stand right of the middle), else the middle of the board.
function MU.CenterX(bw)
    local a = MU.PieceAnchor("board_field", "arch_bottom_frame")
    if a then
        return math.floor(MU.FIELD_INSET + (a.left + a.right) / 2 * (bw - 2 * MU.FIELD_INSET) + 0.5)
    end
    return math.floor(bw / 2)
end

-- Left edges of n items of width iw with a gap, centred in a row of width rw (items never overlap
-- unless they would not fit: then they are squeezed together over the whole row). cx (optional): the x of the row's
-- middle instead of rw / 2 (the row then stays inside 0 .. rw).
function MU.RowPositions(n, iw, gap, rw, cx)
    local out = {}
    if n < 1 then
        return out
    end
    local step = iw + gap
    if n > 1 and (n - 1) * step + iw > rw then
        step = (rw - iw) / (n - 1)
    end
    local total = (n - 1) * step + iw
    local x0 = (rw - total) / 2
    if cx then
        x0 = cx - total / 2
        if x0 + total > rw then
            x0 = rw - total
        end
        if x0 < 0 then
            x0 = 0
        end
    end
    for i = 1, n do
        out[i] = math.floor(x0 + (i - 1) * step + 0.5)
    end
    return out
end

-- ------------------------------------------------------------------ art
local function classColor(cardId)
    local db = HS_CardDB
    local c = nil
    if type(db) == "table" and type(db.cards) == "table" then
        c = db.cards[cardId or ""]
    end
    return CLASS_COLORS[(c and c.class) or "NEUTRAL"] or CLASS_COLORS.NEUTRAL
end
MU.ClassColor = classColor

-- Crops the card texture of cardId into tex (crop: u1, u2, v1, v2 of the card image). Without the
-- texture the area is a flat colour of the card's class. Returns true when the texture is used.
function MU.SetArt(tex, cardId, crop)
    if cardId and CM.HasTexture(cardId) then
        local bottom = 388 / 512
        if H.CardBottom then
            bottom = H.CardBottom(cardId)
        end
        tex:SetTexture((H.CARD_TEXTURE_PATH or "Interface\\AddOns\\Hearthstone\\Textures\\Cards\\") .. cardId)
        tex:SetTexCoord(crop[1], crop[2], crop[3] * bottom, crop[4] * bottom)
        tex:SetVertexColor(1, 1, 1)
        return true
    end
    local c = classColor(cardId)
    tex:SetTexture(WHITE)
    tex:SetTexCoord(0, 1, 0, 1)
    tex:SetVertexColor(c[1], c[2], c[3])
    return false
end

-- ------------------------------------------------------------------ small widgets
local function newText(parent, font, justify, layer)
    local fs = parent:CreateFontString(nil, layer or "OVERLAY", font)
    fs:SetJustifyH(justify or "CENTER")
    return fs
end

-- A text with an outline and an own size (the numbers on gems and tags).
local function outlineText(parent, size, justify)
    local fs = parent:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    fs:SetFont(NUMBER_FONT, size, "OUTLINE")
    fs:SetJustifyH(justify or "CENTER")
    return fs
end

local function button(name, parent, width, text)
    local b = CreateFrame("Button", name, parent, "UIPanelButtonTemplate")
    b:SetWidth(width)
    b:SetHeight(24)
    b:SetText(text)
    return b
end

-- The glowing Done plate of the collection sheet (btn_done), or a plain Done button without the art. Both views of the
-- match window (mulligan, result) use it.
local function doneButton(name, parent)
    local UIc = H.CollectionUI
    local donePiece = UIc and UIc.Piece and UIc.Piece("btn_done")
    local b
    if donePiece then
        b = CreateFrame("Button", name, parent)
        b:SetWidth(160)
        b:SetHeight(80)
        b:SetFrameLevel(parent:GetFrameLevel() + 3)
        b:SetNormalTexture(donePiece.path)
        b:GetNormalTexture():SetTexCoord(0, donePiece.right, 0, donePiece.bottom)
        b:SetHighlightTexture(donePiece.path, "ADD")
        local ht = b:GetHighlightTexture()
        ht:SetTexCoord(0, donePiece.right, 0, donePiece.bottom)
        ht:SetAlpha(0.35)
        if b.SetHitRectInsets then
            b:SetHitRectInsets(20, 20, 12, 12)
        end
        b.art = donePiece
        if UIc.ButtonGlow then UIc.ButtonGlow(b) end
    elseif UIc and UIc.MakeButton then
        b = UIc.MakeButton(name, parent, 170, 44, "Done", parent:GetFrameLevel() + 3)
    else
        b = button(name, parent, 130, "Done")
    end
    return b
end
MU.DoneButton = doneButton

local function showIf(f, yes)
    if yes then
        f:Show()
    else
        f:Hide()
    end
end

local function sound(name)
    if PlaySound then
        PlaySound(name)
    end
end

-- A number gem. kind: attack, health, mana or armor. With the gem_<kind> piece it is that picture;
-- without it a small coloured plate with a tooltip border.
local function makeGem(parent, kind, size, fontSize)
    local f = CreateFrame("Frame", nil, parent)
    f:SetWidth(size)
    f:SetHeight(size)
    f:SetFrameLevel(parent:GetFrameLevel() + 6)
    local tex = f:CreateTexture(nil, "BACKGROUND")
    tex:SetAllPoints(f)
    local gem = { frame = f, tex = tex, kind = kind }
    if MU.SetPiece(tex, "gem_" .. kind) then
        gem.native = false
    else
        gem.native = true
        tex:Hide()
        local c = GEM_COLORS[kind]
        f:SetBackdrop({
            bgFile = WHITE, edgeFile = TOOLTIP_EDGE, tile = true, tileSize = 8, edgeSize = 10,
            insets = { left = 2, right = 2, top = 2, bottom = 2 },
        })
        f:SetBackdropColor(c[1], c[2], c[3], 1)
        f:SetBackdropBorderColor(0.85, 0.7, 0.35, 1)
    end
    gem.text = outlineText(f, fontSize or math.floor(size * 0.6), "CENTER")
    if kind == "attack" then
        -- the sword sticks out of the gem's top left, so the gold disc sits right of and below the middle of the picture
        gem.text:SetPoint("CENTER", f, "CENTER", size * MU.ATTACK_TEXT_DX, size * MU.ATTACK_TEXT_DY)
    else
        gem.text:SetPoint("CENTER", f, "CENTER", 0, 0)
    end
    f:Hide()
    return gem
end

-- Shows value in the gem; tint "up" (green), "down" (red) or nil (white). value nil hides it.
local function setGem(gem, value, tint)
    if value == nil then
        gem.frame:Hide()
        return
    end
    gem.text:SetText(tostring(value))
    if tint == "up" then
        gem.text:SetTextColor(0.35, 1, 0.35)
    elseif tint == "down" then
        gem.text:SetTextColor(1, 0.35, 0.35)
    elseif tint == "rage" then
        gem.text:SetTextColor(1, 0.5, 0.2)
    else
        gem.text:SetTextColor(1, 1, 1)
    end
    gem.frame:Show()
end

-- A row of small state icons at the top-left of a frame (size px each, MU.MAX_MARKS of them).
local function makeMarks(parent, size)
    local marks = { size = size, tex = {} }
    for i = 1, MU.MAX_MARKS do
        local t = parent:CreateTexture(nil, "OVERLAY")
        t:SetWidth(size)
        t:SetHeight(size)
        t:SetPoint("TOPLEFT", parent, "TOPLEFT", 3 + (i - 1) * (size + 1), -3)
        t:Hide()
        marks.tex[i] = t
    end
    return marks
end

-- Shows the icons of the marker keys (extra keys beyond MU.MAX_MARKS are left out).
local function setMarks(marks, keys)
    marks.shown = {}
    for i = 1, MU.MAX_MARKS do
        local t = marks.tex[i]
        local m = keys[i] and MU.MARKERS[keys[i]]
        if m then
            t:SetTexture(m.icon)
            t:SetTexCoord(0.08, 0.92, 0.08, 0.92)
            t:SetVertexColor(1, 1, 1)
            table.insert(marks.shown, keys[i])
            t:Show()
        else
            t:Hide()
        end
    end
end

-- A coloured border frame around a frame (the rim of an immune or frozen character).
local function makeRim(parent, pad)
    local f = CreateFrame("Frame", nil, parent)
    f:SetPoint("TOPLEFT", parent, "TOPLEFT", -pad, pad)
    f:SetPoint("BOTTOMRIGHT", parent, "BOTTOMRIGHT", pad, -pad)
    f:SetBackdrop({ edgeFile = TOOLTIP_EDGE, edgeSize = 12, insets = { left = 3, right = 3, top = 3, bottom = 3 } })
    f:Hide()
    return f
end

local function setRim(rim, r, g, b)
    if not r then
        rim:Hide()
        return
    end
    rim:SetBackdropBorderColor(r, g, b, 1)
    rim:Show()
end

-- A flat coloured plate over the art of a frame (anchored to 'over'), hidden without a tint.
local function makeTint(parent, over)
    local t = parent:CreateTexture(nil, "ARTWORK")
    t:SetAllPoints(over)
    t:SetTexture(WHITE)
    t:Hide()
    return t
end

local function setTint(tex, r, g, b, a)
    if not r then
        tex:Hide()
        return
    end
    tex:SetTexture(WHITE)
    tex:SetVertexColor(r, g, b, a)
    tex:Show()
end

-- A glow around a frame: the glow_green / glow_red piece when installed, else a coloured tooltip
-- border. Colours: "green", "red", "gold" (nil hides it).
-- With a shape ("round", "arch" or "oval") and the white soft-glow piece glow_<shape> installed (made by
-- tools/art/slice_board.py), the glow follows that shape and is tinted with SetVertexColor: the piece's "inner"
-- anchor is the shape's rectangle inside the texture and MU.LayoutGlow maps it onto the frame grown by shapePad
-- pixels. Without the piece the glow is the old rectangle (glow_green / glow_red, else a coloured border).
local function makeGlow(parent, pad, shape, shapePad)
    local f = CreateFrame("Frame", nil, parent)
    local glow = { frame = f, parent = parent }
    if shape and MU.Piece("glow_" .. shape) then
        glow.shape = shape
        glow.piece = true
        glow.shapePiece = "glow_" .. shape
        glow.shapePad = shapePad or 2
        local tex = f:CreateTexture(nil, "OVERLAY")
        tex:SetAllPoints(f)
        tex:SetBlendMode("ADD")
        glow.tex = tex
        MU.LayoutGlow(glow, parent:GetWidth(), parent:GetHeight())
        f:Hide()
        return glow
    end
    f:SetPoint("TOPLEFT", parent, "TOPLEFT", -pad, pad)
    f:SetPoint("BOTTOMRIGHT", parent, "BOTTOMRIGHT", pad, -pad)
    if MU.Piece("glow_green") then
        glow.piece = true
        local tex = f:CreateTexture(nil, "OVERLAY")
        tex:SetAllPoints(f)
        tex:SetBlendMode("ADD")
        glow.tex = tex
    else
        glow.piece = false
        f:SetBackdrop({ edgeFile = TOOLTIP_EDGE, edgeSize = 14, insets = { left = 4, right = 4, top = 4, bottom = 4 } })
    end
    f:Hide()
    return glow
end

local function setGlow(glow, color)
    if glow.hand then
        glow.color = color
        MU.LayoutHandGlow(glow)
        return
    end
    if not color then
        glow.frame:Hide()
        return
    end
    local c = GLOW_COLORS[color]
    if glow.shapePiece then
        MU.SetPiece(glow.tex, glow.shapePiece)
        glow.tex:SetVertexColor(c[1], c[2], c[3])
    elseif glow.piece then
        local name = "glow_green"
        if color == "red" and MU.Piece("glow_red") then
            name = "glow_red"
            c = { 1, 1, 1 }
        elseif color == "combo" and MU.Piece("glow_yellow") then
            name = "glow_yellow"
            c = { 1, 1, 1 }
        elseif color == "green" then
            c = { 1, 1, 1 }
        end
        MU.SetPiece(glow.tex, name)
        glow.tex:SetVertexColor(c[1], c[2], c[3])
    else
        glow.frame:SetBackdropBorderColor(c[1], c[2], c[3], 1)
    end
    glow.frame:Show()
end

-- The playable highlight of a hand card. It is a frame of its own at the bottom level of the card's band, so it lies
-- behind its card (and over the cards behind that one). With the glow_green / glow_red piece it is drawn as strips
-- like the tilted card art (CardWidget.PlaceStrips), turned by glow.angle around the same pivot; without the piece it
-- is a plain coloured border that cannot tilt. Its rectangle is the visible card body (W.VISIBLE) grown by the
-- glowx / glowtop / glowbottom of MU.HAND_FAN.
function MU.MakeHandGlow(parent, level, cardW, padK)
    local f = CreateFrame("Frame", nil, parent)
    f:SetFrameLevel(level)
    f:SetAllPoints(parent)
    local glow = { frame = f, parent = parent, hand = true, strips = {}, angle = 0, piece = MU.Piece("glow_green") ~= nil,
        cardW = cardW or MU.HAND_W, padK = padK }
    if not glow.piece then
        f:SetBackdrop({ edgeFile = TOOLTIP_EDGE, edgeSize = 14, insets = { left = 4, right = 4, top = 4, bottom = 4 } })
    end
    f:Hide()
    return glow
end

function MU.LayoutHandGlow(glow)
    local color = glow.color
    if not color then
        glow.frame:Hide()
        H.CardWidget.HideStrips(glow.strips)
        return
    end
    local c = GLOW_COLORS[color]
    local set = MU.HAND_FAN
    local W = H.CardWidget
    local k = glow.cardW / 256
    local pk = glow.padK or glow.cardW / MU.HAND_W      -- the glowx / glowy pixels are for a hand card; bigger cards get proportionally more
    local gx, gt, gb = set.glowx * pk, set.glowtop * pk, set.glowbottom * pk
    local v = W.VISIBLE
    local xr, xb = glow.extraR or 0, glow.extraB or 0     -- more reach on the right / bottom (an untilted card)
    local rect = { l = v.left * k - gx, t = v.top * k - gt, r = v.right * k + gx + xr, b = v.bottom * k + gb + xb }
    if not glow.piece then
        -- no art: an upright border around the same rectangle
        glow.frame:ClearAllPoints()
        glow.frame:SetPoint("TOPLEFT", glow.parent, "TOPLEFT", rect.l, -rect.t)
        glow.frame:SetPoint("BOTTOMRIGHT", glow.parent, "TOPLEFT", rect.r, -rect.b)
        glow.frame:SetBackdropBorderColor(c[1], c[2], c[3], 1)
        glow.frame:Show()
        return
    end
    local name = "glow_green"
    if color == "red" and MU.Piece("glow_red") then
        name = "glow_red"
        c = { 1, 1, 1 }
    elseif color == "combo" and MU.Piece("glow_yellow") then
        name = "glow_yellow"
        c = { 1, 1, 1 }
    elseif color == "green" then
        c = { 1, 1, 1 }
    end
    local piece = MU.Piece(name)
    local right, bottom = piece.right, piece.bottom
    if type(right) ~= "number" or type(bottom) ~= "number" then
        right, bottom = 1, 1
    end
    -- the card art turns around the middle of W.TILT_SRC; so does the glow
    local src = W.TILT_SRC
    local px, py = (src.left + src.right) / 2 * k, (src.top + src.bottom) / 2 * k
    local rw, rh = rect.r - rect.l, rect.b - rect.t
    local map = { a = right * (px - rect.l) / rw, b = right / rw, c = bottom * (py - rect.t) / rh, d = bottom / rh }
    W.PlaceStrips(glow.strips, glow.frame, "BACKGROUND", piece.path, rect, px, py, map, glow.angle or 0, MU.GLOW_STRIPS,
        function(tex)
            tex:SetBlendMode("ADD")
        end)
    for i = 1, table.getn(glow.strips) do
        glow.strips[i].tex:SetVertexColor(c[1], c[2], c[3])
    end
    glow.frame:Show()
end

-- Places a shaped glow around its parent of w x h pixels: the shape's rectangle (the piece's "inner" anchor) is the
-- parent grown by glow.shapePad on every side, or by pads = { l, t, r, b } when given (the golden hero arch: the
-- glow follows the frame's outer edge). No-op for the rectangular glows.
function MU.LayoutGlow(glow, w, h, pads)
    if not glow or not glow.shapePiece then
        return
    end
    local inner = MU.PieceAnchor(glow.shapePiece, "inner") or { left = 0.1, top = 0.1, right = 0.9, bottom = 0.9 }
    local pad = glow.shapePad or 0
    local pl, pt, pr, pb = pad, pad, pad, pad
    if pads then
        pl, pt, pr, pb = pads.l, pads.t, pads.r, pads.b
    end
    local gw = (w + pl + pr) / (inner.right - inner.left)
    local gh = (h + pt + pb) / (inner.bottom - inner.top)
    local l = -pl - inner.left * gw
    local t = pt + inner.top * gh
    glow.w, glow.h = w, h
    local f = glow.frame
    f:ClearAllPoints()
    -- both corners hang on the matching corners of the parent, so the glow stays centred on it whatever size the parent
    -- really has (a glow tied only to the top left would drift if the parent's height differed from h)
    f:SetPoint("TOPLEFT", glow.parent, "TOPLEFT", l, t)
    f:SetPoint("BOTTOMRIGHT", glow.parent, "BOTTOMRIGHT", pr + (1 - inner.right) * gw, -(pb + (1 - inner.bottom) * gh))
end

-- How far the golden frame of a hero reaches beyond the portrait box on each side ({ l, t, r, b }), or nil without it.
function MU.HeroFramePads(key, w, h)
    local op = MU.PieceAnchor(key == "m" and "arch_bottom" or "arch_top", "opening")
    if not op then
        return nil
    end
    local ow, oh = w / (op.right - op.left), h / (op.bottom - op.top)
    return { l = op.left * ow, t = op.top * oh, r = (1 - op.right) * ow, b = (1 - op.bottom) * oh }
end

-- A dark plate with a gold border (the native look of frames and card backs).
local function plate(f, alpha)
    f:SetBackdrop({
        bgFile = WHITE, edgeFile = TOOLTIP_EDGE, tile = true, tileSize = 16, edgeSize = 14,
        insets = { left = 3, right = 3, top = 3, bottom = 3 },
    })
    f:SetBackdropColor(0.08, 0.06, 0.04, alpha or 0.9)
    f:SetBackdropBorderColor(0.75, 0.6, 0.3, 1)
end

local function tooltip(owner, anchor, title, l1, l2)
    if not GameTooltip then
        return
    end
    GameTooltip:SetOwner(owner, anchor)
    GameTooltip:SetText(title)
    if l1 then
        GameTooltip:AddLine(l1, 1, 1, 1)
    end
    if l2 then
        GameTooltip:AddLine(l2, 0.6, 0.6, 0.6)
    end
    GameTooltip:Show()
end

local function hideTip()
    if GameTooltip then
        GameTooltip:Hide()
    end
end

-- ------------------------------------------------------------------ the large card preview
function MU.HidePreview()
    local pv = MU.preview
    if pv then
        pv.frame:Hide()
    end
    if MU.manaPreview then
        MU.SetManaPreview(nil)
    end
    MU.hoverId = nil
end

-- Lines under the large card: the card database lines, then the numbers it has right now.
function MU.CaptionLines(cardId, extra)
    local db = CM.GetDB()
    local card = nil
    if db then
        card = db.cards[cardId]
    end
    local lines = CM.Caption(cardId, card, CM.HasTexture(cardId))
    if extra then
        for i = 1, table.getn(extra) do
            table.insert(lines, 2, extra[i])
        end
    end
    return lines
end

-- Shows the large card for cardId next to the window: on the side away from 'anchor' (the hovered
-- element), so the element itself stays visible. 'bare': only the card itself, without the plate and the caption
-- under it (the log and the hero powers).
function MU.ShowPreview(cardId, anchor, extra, bare)
    local pv = MU.preview
    if not pv or not cardId or not MU.frame then
        return
    end
    H.CardWidget.SetCard(pv.widget, cardId)
    if bare then
        pv.caption:SetText("")
        pv.frame:SetBackdropColor(0, 0, 0, 0)
        pv.frame:SetBackdropBorderColor(0.75, 0.6, 0.3, 0)
        pv.widget.frame:ClearAllPoints()
        pv.widget.frame:SetPoint("CENTER", pv.frame, "CENTER", 0, 0)
    else
        pv.caption:SetText(table.concat(MU.CaptionLines(cardId, extra), "\n"))
        pv.frame:SetBackdropColor(0, 0, 0, 0.92)
        pv.frame:SetBackdropBorderColor(0.75, 0.6, 0.3, 1)
        pv.widget.frame:ClearAllPoints()
        pv.widget.frame:SetPoint("TOP", pv.frame, "TOP", 0, -8)
    end
    pv.frame:SetScale(MU.frame:GetScale() or 1)
    local right = true
    local ax = anchor and anchor.GetCenter and anchor:GetCenter()
    local fx = MU.frame:GetCenter()
    if ax and fx and ax > fx then
        right = false
    end
    pv.frame:ClearAllPoints()
    if right then
        pv.frame:SetPoint("RIGHT", MU.frame, "RIGHT", -MU.PAD, 0)
    else
        pv.frame:SetPoint("LEFT", MU.frame, "LEFT", MU.PAD, 0)
    end
    pv.frame:Show()
end

local function buildPreview()
    local f = CreateFrame("Frame", "HearthstoneMatchPreview", UIParent)
    f:SetWidth(MU.PREVIEW_W)
    f:SetHeight(MU.PREVIEW_H)
    f:SetFrameStrata("TOOLTIP")
    f:SetClampedToScreen(true)
    f:SetBackdrop({
        bgFile = TOOLTIP_BG, edgeFile = TOOLTIP_EDGE, tile = true, tileSize = 16, edgeSize = 16,
        insets = { left = 4, right = 4, top = 4, bottom = 4 },
    })
    f:SetBackdropColor(0, 0, 0, 0.92)
    f:SetBackdropBorderColor(0.75, 0.6, 0.3, 1)
    local widget = H.CardWidget.Create(f, "HearthstoneMatchPreviewCard", 256, true)
    widget.frame:SetPoint("TOP", f, "TOP", 0, -8)
    local caption = f:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    caption:SetPoint("TOPLEFT", f, "TOPLEFT", 12, -404)
    caption:SetWidth(MU.PREVIEW_W - 24)
    caption:SetJustifyH("LEFT")
    f:Hide()
    MU.preview = { frame = f, widget = widget, caption = caption }
end

-- ------------------------------------------------------------------ logic
-- The snapshot on screen: the one before the beats that play (it lags behind HsMatch.game), else the newest.
local function game()
    return AN.PresentedGame() or MA.game
end

-- True while the board shows an older snapshot than the newest: ordinary inputs are disabled.
function MU.Behind()
    return AN.Busy()
end

-- Shows a line in the message bubble for MU.BUBBLE_SECONDS (then MU.BubbleTick clears it).
MU.BUBBLE_SECONDS = 2
function MU.Say(text)
    MU.hint = text
    MU.bubbleLine = nil            -- a repeated line starts its seconds again
    MU.RefreshMid()
end

-- Runs every frame while the bubble is shown: 5 seconds after a line appeared, the line is cleared. The opponent's
-- "is choosing" line is not timed (it stays while they choose).
function MU.BubbleTick(bubble)
    if not MU.bubbleAt or not bubble:IsShown() or clock() - MU.bubbleAt < MU.BUBBLE_SECONDS then
        return
    end
    MU.bubbleAt, MU.bubbleLine = nil, nil
    MU.hint = nil
    MA.message = nil
    MU.RefreshMid()
end

-- Right click / Escape: closes the Choose One chooser, else cancels targeting. True when it did something.
function MU.CancelTarget()
    if MU.drag then
        MU.EndDrag()
        MU.sel, MU.choose, MU.hint = nil, nil, nil
        MU.Refresh()
        return true
    end
    if MU.choose then
        MU.choose = nil
        MU.Refresh()
        return true
    end
    if MU.sel then
        MU.sel = nil
        MU.EndDrag()
        MU.hint = nil
        MU.Refresh()
        return true
    end
    return false
end

-- Starts targeting for a legal entry: its valid targets glow until one is clicked.
function MU.BeginTarget(kind, entry, choice)
    local set = {}
    local targets = MA.Targets(entry)
    for i = 1, table.getn(targets) do
        set[targets[i]] = true
    end
    local g = game()
    MU.sel = { kind = kind, source = entry.source, entry = entry, seq = g and g.seq, targetSet = set, choice = choice }
    -- an attack shows no line: the glowing targets say it; the bubble is kept for the refusals (MU.Say)
    MU.hint = kind ~= "a" and "Choose a target. Right click or Escape cancels." or nil
    sound("igMainMenuOptionCheckBoxOn")
    MU.Refresh()
end

-- Sends an action through Hearthstone.lua; a refusal shows in the bubble.
function MU.Send(kind, a, b, c, d)
    if MU.Behind() then
        return false                  -- the board is still showing an older snapshot
    end
    MU.EndDrag()
    MU.sel = nil
    MU.choose = nil
    MU.hint = nil
    MU.bubbleLine = nil
    local ok, why = true, nil
    if H.MatchAct then
        ok, why = H.MatchAct(kind, a, b, c, d)
    else
        ok, why = nil, "Hearthstone.lua is not loaded."
    end
    if not ok then
        MU.hint = why
    end
    MU.Refresh()
    return ok
end

-- Why a hand card cannot be played right now.
function MU.PlayBlocker(g, card)
    if MA.pending then
        return "The last action is still being sent."
    end
    if MA.IsPicking(g) then
        return "Choose one of the offered cards first."
    end
    if not MA.IsMyTurn(g) then
        return "It is not your turn."
    end
    if card.cost > g.me.mana then
        return "Not enough mana."
    end
    local base = MA.CardBase(card.cardId)
    if base and base.type == "MINION" and table.getn(g.me.board) >= MU.MAX_BOARD then
        return "Your board is full."
    end
    return "You cannot play that card now."
end

-- Why a minion or hero cannot attack right now.
function MU.AttackBlocker(g)
    if MA.pending then
        return "The last action is still being sent."
    end
    if not MA.IsMyTurn(g) then
        return "It is not your turn."
    end
    return "That cannot attack now."
end

function MU.OnHandClick(i, button)
    local g = game()
    if not g or not MA.IsRunning(g) or MU.Behind() then
        return
    end
    if button == "RightButton" then
        MU.CancelTarget()
        return
    end
    local card = g.hand[i]
    if not card then
        return
    end
    if MU.choose then
        MU.Say("Choose one of the options. Right click or Escape cancels.")
        return
    end
    if MU.sel and MU.sel.source == card.id then
        MU.CancelTarget()
        return
    end
    if MU.sel then
        MU.Say("Choose a highlighted target. Right click or Escape cancels.")
        return
    end
    local entry = nil
    if not MA.pending then
        entry = MA.LegalFor(g, "p", card.id)
    end
    if not entry then
        MU.Say(MU.PlayBlocker(g, card))
        return
    end
    if MU.DragEnabled() then
        -- cards are played by dragging them (/hs drag off brings the click back)
        MU.Say("Drag the card onto the board to play it.")
        return
    end
    if MA.IsChoiceCard(g, card.id) then
        MU.OpenChooser(card.id)
        return
    end
    if MA.NeedsTarget(entry) then
        MU.BeginTarget("p", entry)
    else
        MU.Send("play", card.id, 0, nil)
    end
end

-- Dragging is on unless /hs drag off saved otherwise: then hand cards are played by drag and drop only.
function MU.DragEnabled()
    return not H.db or H.db.drag ~= false
end

-- Choose One: a click on the card opens the chooser with its options.
function MU.OpenChooser(handId)
    local g = game()
    if not g or not MA.IsChoiceCard(g, handId) then
        return false
    end
    MU.choose = { source = handId, seq = g.seq }
    MU.chooserHidden = nil
    MU.hint = nil
    sound("igMainMenuOptionCheckBoxOn")
    MU.Refresh()
    return true
end

-- A click on option i of the chooser: its targets (from lg) decide whether targeting follows.
function MU.OnChooseClick(i, button)
    local g = game()
    local ch = MU.choose
    if not g or not ch or MU.Behind() then
        return
    end
    if button == "RightButton" then
        MU.CancelTarget()
        return
    end
    local entry = nil
    if not MA.pending then
        entry = MA.LegalFor(g, "p", ch.source, i)
    end
    if not entry then
        MU.Say("That option cannot be played now.")
        return
    end
    MU.choose = nil
    if MA.NeedsTarget(entry) then
        MU.BeginTarget("p", entry, i)
    else
        MU.Send("play", ch.source, 0, nil, i)
    end
end

-- A click on a card of the pending pick panel answers it.
function MU.OnPickClick(i, button)
    local g = game()
    if not g or button == "RightButton" or not MA.IsPicking(g) or MA.pending or MU.Behind() then
        return
    end
    local opt = g.pick[i]
    if opt then
        MU.Send("pick", opt.id)
        sound("igMainMenuOptionCheckBoxOn")
    end
end

-- A click on a character (minion or hero) of either side: a target while targeting, else the
-- player's own ready attacker starts an attack.
function MU.OnCharacterClick(id, mine, button)
    local g = game()
    if not g or not MA.IsRunning(g) or MU.Behind() then
        return
    end
    if button == "RightButton" then
        MU.CancelTarget()
        return
    end
    if MU.choose then
        MU.Say("Choose one of the options. Right click or Escape cancels.")
        return
    end
    local sel = MU.sel
    if sel then
        if sel.source == id then
            MU.CancelTarget()
        elseif sel.targetSet[id] then
            if sel.kind == "p" then
                MU.Send("play", sel.source, id, sel.position, sel.choice)
            elseif sel.kind == "a" then
                MU.Send("atk", sel.source, id)
            else
                MU.Send("hp", id)
            end
        else
            MU.Say("That is not a valid target. Right click or Escape cancels.")
        end
        return
    end
    if not mine then
        return
    end
    local entry = nil
    if not MA.pending then
        entry = MA.LegalFor(g, "a", id)
    end
    if not entry then
        MU.Say(MU.AttackBlocker(g))
        return
    end
    MU.BeginTarget("a", entry)
end

function MU.OnMinionClick(key, i, button)
    local g = game()
    if not g then
        return
    end
    local side = g.me
    if key == "o" then
        side = g.opp
    end
    local m = side.board[i]
    if m then
        MU.OnCharacterClick(m.id, key == "m", button)
    end
end

function MU.OnHeroClick(key, button)
    local g = game()
    if not g then
        return
    end
    local side = g.me
    if key == "o" then
        side = g.opp
    end
    MU.OnCharacterClick(side.hero.id, key == "m", button)
end

function MU.OnPowerClick(key, button)
    local g = game()
    if not g or not MA.IsRunning(g) or key ~= "m" or MU.Behind() then
        return
    end
    if button == "RightButton" then
        MU.CancelTarget()
        return
    end
    if MU.choose then
        MU.Say("Choose one of the options. Right click or Escape cancels.")
        return
    end
    if MU.sel then
        if MU.sel.kind == "h" then
            MU.CancelTarget()
        else
            MU.Say("Choose a highlighted target. Right click or Escape cancels.")
        end
        return
    end
    local entry = nil
    if not MA.pending then
        entry = MA.PowerEntry(g)
    end
    if not entry then
        if MA.pending then
            MU.Say("The last action is still being sent.")
        elseif not MA.IsMyTurn(g) then
            MU.Say("It is not your turn.")
        elseif g.me.power.used then
            MU.Say("You already used your hero power this turn.")
        elseif g.me.power.cost > g.me.mana then
            MU.Say("Not enough mana.")
        else
            MU.Say("You cannot use your hero power now.")
        end
        return
    end
    if MA.NeedsTarget(entry) then
        MU.BeginTarget("h", entry)
    else
        MU.Send("hp", 0)
    end
end

-- The glow around the End Turn button while the cursor is on it (only when it can be clicked).
function MU.EndHover(on)
    local mid = MU.ui and MU.ui.mid
    if not mid or not mid.endGlow then
        return
    end
    local g = game()
    if on and g and MA.IsMyTurn(g) and mid.endButton:IsEnabled() then
        mid.endGlow:Show()
    else
        mid.endGlow:Hide()
    end
end

function MU.OnEndTurnClick()
    local g = game()
    if not g or not MA.IsMyTurn(g) or MU.choose or MA.IsPicking(g) or MU.Behind() then
        return
    end
    MU.Send("end")
    sound("igMainMenuOptionCheckBoxOn")
end

-- Mulligan: a click on an offered card marks it for replacement (or takes the mark back).
function MU.OnMullClick(i)
    local offered = MA.Offered()
    local id = offered[i]
    if id and not MA.pending and not MU.Behind() and not MU.mull.anim then
        MA.ToggleMark(id)
        sound("igMainMenuOptionCheckBoxOn")
        MU.Refresh()
    end
end

function MU.OnMullConfirm()
    local g = game()
    if not g or g.ph ~= "mull" or MU.Behind() or MU.mull.anim then
        return
    end
    MU.MullRememberOffer(g)
    MU.Send("mull", MA.MarkedList())
end

-- Leave button of the result view (also X / Escape on a finished match).
function MU.Leave()
    MU.sel = nil
    MU.choose = nil
    MU.hint = nil
    if H.MatchLeave then
        H.MatchLeave()
    end
    MU.HideWindow()
end

-- ------------------------------------------------------------------ surrender
function MU.EnsurePopups()
    if not StaticPopupDialogs then
        return false
    end
    if StaticPopupDialogs["HEARTHSTONE_SURRENDER"] then
        return true
    end
    StaticPopupDialogs["HEARTHSTONE_SURRENDER"] = {
        text = "Surrender this match?",
        button1 = YES or "Yes",
        button2 = NO or "No",
        OnAccept = function()
            MU.AcceptSurrender()
        end,
        OnCancel = function()
            MU.surrenderMid = nil
        end,
        showAlert = 1,
        timeout = 0,
        whileDead = 1,
        hideOnEscape = 1,
    }
    StaticPopupDialogs["HEARTHSTONE_ARENA_SURRENDER"] = {
        text = "Surrender this duel?\n\nIt counts as a LOSS and ENDS your " .. AR.NAME .. " run. To keep the run, "
            .. "close the window instead: the duel is paused and you can resume it at " .. AR.HOSTS_SHORT .. ".",
        button1 = "Surrender and end the run",
        button2 = "Keep playing",
        OnAccept = function()
            MU.AcceptSurrender()
        end,
        OnCancel = function()
            MU.surrenderMid = nil
        end,
        showAlert = 1,
        timeout = 0,
        whileDead = 1,
        hideOnEscape = 1,
    }
    -- The surrender of a ranked ladder duel
    StaticPopupDialogs["HEARTHSTONE_LADDER_SURRENDER"] = {
        text = "Surrender this duel?\n\nIt counts as a LOSS on the ladder. To keep playing it later, close the window "
            .. "instead: the duel is paused and you can resume it at a ladder chair.",
        button1 = "Surrender",
        button2 = "Keep playing",
        OnAccept = function()
            MU.AcceptSurrender()
        end,
        OnCancel = function()
            MU.surrenderMid = nil
        end,
        showAlert = 1,
        timeout = 0,
        whileDead = 1,
        hideOnEscape = 1,
    }
    return true
end

-- Is the running match a draft arena duel of this character (the run says so; the GAME payload does not)?
function MU.IsArenaDuel()
    return AR.IsArenaDuel() and true or false
end

-- Is the running match a ranked ladder duel (the GAME payload says dur=1)?
function MU.IsLadderDuel()
    local g = MA.game
    return (g and g.dur and MA.IsRunning(g)) and true or false
end

-- An arena or a ladder duel: closing the window pauses it, the surrender is a button of its own.
function MU.IsDurableDuel()
    return MU.IsArenaDuel() or MU.IsLadderDuel()
end

-- Asks "Surrender this duel?" (the button of a durable duel: the arena's text ends the run, the ladder's is a loss).
-- Returns false when no such duel runs.
function MU.AskArenaSurrender()
    local g = MA.game
    if not MU.IsDurableDuel() or not MA.IsRunning(g) or not MU.EnsurePopups() or not StaticPopup_Show then
        return false
    end
    MU.surrenderMid = g.mid
    if MU.IsLadderDuel() then
        StaticPopup_Show("HEARTHSTONE_LADDER_SURRENDER")
    else
        StaticPopup_Show("HEARTHSTONE_ARENA_SURRENDER")
    end
    return true
end

-- Close button and Escape of an arena duel: pause it (GAME_PAUSE). The window stays until the server confirms; the
-- answer is GAME ph=none (Hearthstone.lua prints "Duel paused. Resume it at Ezra or Ibelin.").
function MU.PauseDuel()
    local ok, why = H.MatchPause()
    if ok then
        MU.hint = "Pausing the duel..."
    else
        MU.hint = why
        if H.PrintError then
            H.PrintError("Could not pause the duel: " .. tostring(why))
        end
    end
    MU.Refresh()
    return ok and true or false
end

-- What closing a running match does: an arena duel pauses, any other match asks for the surrender.
function MU.CloseRunning()
    if MU.IsDurableDuel() then
        return MU.PauseDuel()
    end
    return MU.AskSurrender()
end

-- The title of the window (an arena duel names the mode) and the surrender button of the title bar, which is shown only
-- while an arena duel runs. Also called when the run changes (Hearthstone.lua H.NotifyArena): the run says whether the
-- running match is an arena duel, and it can arrive after the first GAME.
function MU.RefreshSurrender()
    local f = MU.frame
    if not f or not f.surrender then
        return
    end
    local g = MA.game
    if g and g.ph ~= "none" and f.title then
        local title = MA.TitleText(g)
        if MU.IsArenaDuel() then
            title = title .. " - " .. AR.NAME
        elseif MU.IsLadderDuel() then
            title = title .. " - Ladder"
        end
        f.title:SetText(title)
    end
    if MU.IsDurableDuel() and MA.IsRunning(MA.game) then
        f.surrender:Show()
    else
        f.surrender:Hide()
    end
end

-- Asks "Surrender this match?". Returns false when the match is not running or the popup is missing.
function MU.AskSurrender()
    local g = MA.game
    if not MA.IsRunning(g) or not MU.EnsurePopups() or not StaticPopup_Show then
        return false
    end
    MU.surrenderMid = g.mid
    StaticPopup_Show("HEARTHSTONE_SURRENDER")
    return true
end

-- Yes on the popup: concede, but only the match it was asked for (a popup is not modal).
function MU.AcceptSurrender()
    local mid = MU.surrenderMid
    MU.surrenderMid = nil
    MU.choose = nil
    local g = MA.game
    if not g or g.mid ~= mid or not MA.IsRunning(g) then
        return
    end
    MU.sel = nil
    MU.hint = nil
    local ok, why = H.MatchAct("concede")
    if not ok then
        MU.hint = why
        if H.PrintError then
            H.PrintError("Could not surrender: " .. tostring(why))
        end
    end
    MU.Refresh()
end

-- X button and Escape: a running match asks for the surrender (a running arena duel is paused instead), a finished one is
-- left.
function MU.RequestClose()
    local g = MA.game
    if MA.IsRunning(g) then
        MU.CloseRunning()
    elseif g and g.ph == "over" then
        MU.Leave()
    else
        MU.HideWindow()
    end
end

-- ------------------------------------------------------------------ show / hide
function MU.HideWindow()
    local f = MU.frame
    if f and f:IsShown() then
        MU.hiding = true
        f:Hide()
        MU.hiding = false
    end
    if StaticPopup_Hide then
        StaticPopup_Hide("HEARTHSTONE_SURRENDER")
        StaticPopup_Hide("HEARTHSTONE_ARENA_SURRENDER")
        StaticPopup_Hide("HEARTHSTONE_LADDER_SURRENDER")
    end
    MU.surrenderMid = nil
end

-- The window was hidden. By this file (MU.hiding): nothing more to do. By Escape (UISpecialFrames):
-- a running match cannot be closed that way, so the window comes back and asks (an arena duel is paused; while
-- targeting, Escape only cancels the targeting); a finished match is left.
function MU.OnHide()
    local dragging = MU.drag ~= nil
    MU.EndDrag()
    MU.AnimAbort()
    MU.EndBanner()
    MU.HandUnpopAll()
    MU.HidePreview()
    hideTip()
    if MU.hiding or MU.reshowing then
        return
    end
    local g = MA.game
    if MA.IsRunning(g) then
        MU.reshowing = true
        MU.frame:Show()
        MU.reshowing = false
        if dragging or MU.sel or MU.choose then
            MU.CancelTarget()
        else
            MU.CloseRunning()
        end
    elseif g and g.ph == "over" then
        MU.Leave()
    end
end

function MU.OnShow()
    MU.ApplyFit()
    if not MU.reshowing then
        sound("igCharacterInfoOpen")
    end
    MU.AnimAbort()                    -- a window that opens shows the newest snapshot
    MU.EndBanner()
    MU.SyncBanner(game())             -- ... and does not announce a turn that began while it was closed
    MU.Refresh()
end

-- ------------------------------------------------------------------ refreshing
local function glowOf(g, id, canAct)
    local sel = MU.sel
    if sel then
        if sel.source == id then
            return "gold"
        end
        if sel.targetSet[id] then
            local _, side = MA.WhoIs(g, id)
            if side == "o" then
                return "red"
            end
            return "green"
        end
        return nil
    end
    if canAct and not MA.pending and not MU.Behind() then
        return "green"
    end
    return nil
end

-- The words under a minion's portrait: none. Taunt is drawn as a steel frame around the portrait
-- (slot.taunt, MU.LayoutTaunt); only without that art the word stays so that Taunt is not lost.
local function tagsOf(m, slot)
    if m.taunt and not slot.taunt then
        return "|cffd8d8d8Taunt|r"
    end
    return ""
end

-- Fits a piece over the minion's oval: the piece's "inner" anchor (its opening for the taunt frame, its oval for the
-- Divine Shield bubble) goes onto the oval of the minion_oval piece (its "oval" anchor, else the whole frame) grown by
-- 'grow' pixels. A piece without an "inner" anchor is stretched over the oval itself.
function MU.FitOverOval(slot, tex, pieceName, grow)
    local piece = MU.Piece(pieceName)
    if not tex or not piece then
        return
    end
    local hole = type(piece.anchors) == "table" and piece.anchors.inner
    if not validAnchor(hole) then
        hole = { left = 0, top = 0, right = 1, bottom = 1 }
    end
    local w, h = slot.frame:GetWidth(), slot.frame:GetHeight()
    local oval = MU.PieceAnchor("minion_oval", "oval") or { left = 0.09, top = 0.074, right = 0.9, bottom = 0.926 }
    local tl, tt = oval.left * w - grow, oval.top * h - grow
    local tr, tb = oval.right * w + grow, oval.bottom * h + grow
    local qw = (tr - tl) / (hole.right - hole.left)
    local qh = (tb - tt) / (hole.bottom - hole.top)
    tex:ClearAllPoints()
    tex:SetPoint("TOPLEFT", slot.frame, "TOPLEFT", tl - hole.left * qw, -(tt - hole.top * qh))
    tex:SetWidth(qw)
    tex:SetHeight(qh)
end

function MU.LayoutTaunt(slot)
    MU.FitOverOval(slot, slot.taunt, "taunt_frame", MU.TAUNT_GROW)
end

-- The glow of a minion lies around its oval; a Taunt minion's glow lies around its steel frame instead, in the
-- frame's own shape (glow_taunt, a soft glow traced from the frame's outline by tools/art/slice_board.py). Without
-- that piece the taunt glow is the oval glow laid around the frame's rectangle.
function MU.LayoutMinionGlow(slot)
    local glow = slot.glow
    if not glow or not glow.shapePiece then
        return
    end
    glow.ovalPiece = glow.ovalPiece or glow.shapePiece
    if slot.taunt and slot.taunt:IsShown() then
        glow.shapePiece = MU.Piece("glow_taunt") and "glow_taunt" or glow.ovalPiece
        glow.shapePad = 2
        glow.parent = slot.taunt
        MU.LayoutGlow(glow, slot.taunt:GetWidth(), slot.taunt:GetHeight())
    else
        glow.shapePiece = glow.ovalPiece
        glow.shapePad = MU.MINION_GLOW_PAD
        glow.parent = slot.frame
        MU.LayoutGlow(glow, slot.frame:GetWidth(), slot.frame:GetHeight())
    end
end

function MU.LayoutShield(slot)
    MU.FitOverOval(slot, slot.shield, "divine_shield", MU.SHIELD_GROW)
end

-- The ice ring of a frozen character: the piece's "inner" anchor (the middle of the ice band) goes onto the minion's
-- oval, so the ice lies over the portrait's edge and its spikes reach a little beyond it.
function MU.LayoutIce(slot)
    MU.FitOverOval(slot, slot.ice, "frozen_ice", MU.FROZEN_GROW)
end

-- The same for a hero: the anchor goes onto the whole hero frame (the arch), so a taller arch gives an oval ring.
function MU.LayoutHeroIce(hero)
    local tex, piece = hero.ice, MU.Piece("frozen_ice")
    if not tex or not piece then
        return
    end
    local hole = type(piece.anchors) == "table" and piece.anchors.inner
    if not validAnchor(hole) then
        hole = { left = 0, top = 0, right = 1, bottom = 1 }
    end
    local w, h, g = hero.frame:GetWidth(), hero.frame:GetHeight(), MU.FROZEN_GROW
    local qw = (w + 2 * g) / (hole.right - hole.left)
    local qh = (h + 2 * g) / (hole.bottom - hole.top)
    tex:ClearAllPoints()
    tex:SetPoint("TOPLEFT", hero.frame, "TOPLEFT", -g - hole.left * qw, g + hole.top * qh)
    tex:SetWidth(qw)
    tex:SetHeight(qh)
end

-- The minion portrait: the pre-cut oval of the card (with soft edges) inside the minion_oval piece's "oval"
-- anchor when installed, else the rectangular crop of the card art. Returns true when art is shown.
function MU.SetMinionArt(slot, cardId)
    local oval = MU.PieceAnchor("minion_oval", "oval") or MU.PieceAnchor("minion_oval", "inner")
    local use = oval ~= nil and not slot.native and MU.HasMinionTexture(cardId)
    local mode = use and "oval" or "crop"
    if slot.artMode ~= mode then
        slot.artMode = mode
        local art, b = slot.art, slot.frame
        art:ClearAllPoints()
        if use then
            local w, h = b:GetWidth(), b:GetHeight()
            art:SetPoint("TOPLEFT", b, "TOPLEFT", math.floor(oval.left * w + 0.5), -math.floor(oval.top * h + 0.5))
            art:SetPoint("BOTTOMRIGHT", b, "TOPLEFT", math.floor(oval.right * w + 0.5), -math.floor(oval.bottom * h + 0.5))
        else
            art:SetWidth(58)
            art:SetHeight(56)
            art:SetPoint("TOP", b, "TOP", 0, -10)
        end
    end
    if use then
        slot.art:SetTexture(MU.MINION_TEXTURE_PATH .. cardId)
        slot.art:SetTexCoord(0, 1, 0, 1)
        slot.art:SetVertexColor(1, 1, 1)
        return true
    end
    return MU.SetArt(slot.art, cardId, MU.CROP_MINION)
end

-- Shows the keyword icons (keys of MU.MechanicKeys) side by side at the bottom centre of a minion's frame.
local function setMechanics(slot, keys)
    for i = 1, table.getn(MU.MECHANIC_ORDER) do
        slot.mechs[MU.MECHANIC_ORDER[i]]:Hide()
    end
    local n = table.getn(keys)
    -- each icon has its own size (MU.MECHANIC_ICONS[...].size), so the row is laid out by the widths
    local widths, total = {}, 0
    for i = 1, n do
        local def = MU.MECHANIC_ICONS[keys[i]]
        local size = def.size or MU.MECHANIC_SIZE
        local p = MU.Piece(def.piece)
        local w = size
        if p and type(p.w) == "number" and type(p.h) == "number" and p.h > 0 then
            w = size * p.w / p.h
        end
        widths[i] = w
        total = total + w + (i > 1 and MU.MECHANIC_GAP or 0)
    end
    local left = -(total / 2)
    for i = 1, n do
        local def = MU.MECHANIC_ICONS[keys[i]]
        local t = slot.mechs[keys[i]]
        local p = MU.Piece(def.piece)
        local size = def.size or MU.MECHANIC_SIZE
        if p then
            MU.SetPiece(t, def.piece)
        else
            t:SetTexture(def.icon)
            t:SetTexCoord(0.08, 0.92, 0.08, 0.92)
            t:SetVertexColor(1, 1, 1)
        end
        t:SetWidth(widths[i])
        t:SetHeight(size)
        t:ClearAllPoints()
        -- centred as a row: the gap between two icons is MU.MECHANIC_GAP, the row is centred on the frame
        t:SetPoint("BOTTOM", slot.frame, "BOTTOM", left + widths[i] / 2, def.lift or MU.MECHANIC_LIFT)
        left = left + widths[i] + MU.MECHANIC_GAP
        t:Show()
    end
end

local function refreshMinion(slot, m, g, mine)
    slot.entity = m.id
    slot.cardId = m.cardId
    local hasArt = MU.SetMinionArt(slot, m.cardId)
    if hasArt then
        slot.label:Hide()
    else
        slot.label:SetText(MA.CardName(m.cardId) or "")
        slot.label:Show()
    end
    local base = MA.CardBase(m.cardId)
    local atkTint, hpTint = nil, nil
    if base and base.attack ~= nil then
        if m.attack > base.attack then
            atkTint = "up"
        elseif m.attack < base.attack then
            atkTint = "down"
        end
    end
    if m.health < m.maxHealth then
        hpTint = "down"
    elseif base and base.health ~= nil and m.maxHealth > base.health then
        hpTint = "up"
    end
    if m.enraged then
        atkTint = "rage"
    end
    setGem(slot.atk, m.attack, atkTint)
    setGem(slot.hp, m.health, hpTint)
    slot.tags:SetText(tagsOf(m, slot))
    if slot.taunt then
        if m.taunt then
            slot.taunt:Show()
        else
            slot.taunt:Hide()
        end
    end
    if slot.shield then
        if m.divine then
            slot.shield:Show()
        else
            slot.shield:Hide()
        end
    end
    -- state markers: icons, a tint over the art, a rim, and a fainter picture for Stealth
    setMarks(slot.marks, MU.StateKeys(m, slot.ice ~= nil))
    setMechanics(slot, MU.MechanicKeys(m.cardId, m.silenced))
    if slot.silence then
        if m.silenced then
            slot.silence:Show()
        else
            slot.silence:Hide()
        end
    end
    setTint(slot.tint, MU.TintOf(m))
    if slot.artMode == "oval" and slot.tint:IsShown() then
        -- the tint takes the oval's shape: the portrait itself, multiplied by the tint colour
        slot.tint:SetTexture(MU.MINION_TEXTURE_PATH .. m.cardId)
        slot.tint:SetTexCoord(0, 1, 0, 1)
    end
    setRim(slot.rim, MU.RimOf(m, slot.ice ~= nil))
    if slot.ice then
        if m.frozen then
            slot.ice:Show()
        else
            slot.ice:Hide()
        end
    end
    if m.stealth then
        slot.art:SetAlpha(MU.STEALTH_ALPHA)
        slot.label:SetAlpha(MU.STEALTH_ALPHA)
    else
        slot.art:SetAlpha(1)
        slot.label:SetAlpha(1)
    end
    if slot.native then
        if m.taunt then
            slot.frame:SetBackdropBorderColor(0.9, 0.9, 0.95, 1)
        else
            slot.frame:SetBackdropBorderColor(0.75, 0.6, 0.3, 1)
        end
    end
    local canAct = mine and MA.IsMyTurn(g) and m.canAttack and MA.LegalFor(g, "a", m.id) ~= nil
    MU.LayoutMinionGlow(slot)
    setGlow(slot.glow, glowOf(g, m.id, canAct))
    slot.frame:Show()
end

local function refreshBoard(key, s, g)
    local u = MU.ui[key]
    local n = table.getn(s.board)
    local xs = MU.RowPositions(n, MU.MINION_W, MU.MINION_GAP, MU.geom.bw, MU.CenterX(MU.geom.bw))
    local y = MU.RowTop(key, MU.geom.fh)
    for i = 1, MU.MAX_BOARD do
        local slot = u.minions[i]
        local m = s.board[i]
        if m then
            -- home: where the frame rests; the animations move it away and back (MU.PlaceSlot)
            slot.home = { point = "TOPLEFT", x = xs[i], y = -math.floor(y) }
            slot.rect = { x = xs[i], y = math.floor(y), w = MU.MINION_W, h = MU.MINION_H }
            MU.PlaceSlot(slot)
            refreshMinion(slot, m, g, key == "m")
        else
            slot.entity = nil
            slot.frame:Hide()
        end
    end
end

local function refreshHero(key, s, g)
    local u = MU.ui[key]
    local h = u.hero
    local hero = s.hero
    h.entity = hero.id
    h.cardId = hero.cardId
    if h.arch then
        -- only the player's own hero may show an alternative portrait, and only its art: the card id (name, power,
        -- rules) stays the class hero's
        MU.SetHeroArt(h, HsHeroes and HsHeroes.ArtFor(hero.cardId, key == "m") or hero.cardId)
    else
        MU.SetArt(h.art, hero.cardId, MU.CROP_MINION)
    end
    local tint = nil
    if hero.health < hero.maxHealth then
        tint = "down"
    end
    setGem(h.hp, hero.health, tint)
    -- an immune hero shows a light blue glow (below) instead of the gold rim, tint and icon of the other characters
    local look = {}
    for k, v in pairs(hero) do
        look[k] = v
    end
    look.immune = false
    setMarks(h.marks, MU.StateKeys(look, h.ice ~= nil))
    setTint(h.tint, MU.TintOf(look))
    setRim(h.rim, MU.RimOf(look, h.ice ~= nil))
    if h.ice then
        if hero.frozen then
            h.ice:Show()
        else
            h.ice:Hide()
        end
    end
    if hero.attack > 0 then
        setGem(h.atk, hero.attack, nil)
    else
        setGem(h.atk, nil)
    end
    if hero.armor > 0 then
        setGem(h.armor, hero.armor, nil)
    else
        setGem(h.armor, nil)
    end
    if h.ring then
        local name = "hero_ring"
        if hero.armor > 0 and MU.Piece("hero_ring_armor") then
            name = "hero_ring_armor"
        end
        MU.SetPiece(h.ring, name)
        MU.ArtInRing(h.art, h.frame, name, 6, 7)
    end
    local name = g.oppName
    if key == "m" then
        name = g.myName
    end
    u.name:SetText("")      -- the names of the players are not shown
    local canAct = key == "m" and MA.IsMyTurn(g) and hero.canAttack and MA.LegalFor(g, "a", hero.id) ~= nil
    local glow = glowOf(g, hero.id, canAct and not hero.immune)
    if hero.immune and (glow == nil or glow == "green") then
        glow = "ice"
    end
    setGlow(h.glow, glow)
    h.frame:Show()
end

-- Re-places a texture over frame as the rectangle (l, t, r, b: offsets from the frame's top left corner, up is
-- positive, see MU.ArtInRing) squashed to the share f of its width around its middle.
function MU.SquashTex(tex, frame, l, t, r, b, f)
    local w = (r - l) * f
    tex:ClearAllPoints()
    tex:SetPoint("TOPLEFT", frame, "TOPLEFT", (l + r) / 2 - w / 2, t)
    tex:SetWidth(w)
    tex:SetHeight(t - b)
end

-- One frame of the power's flip (p.flip = { t, to }): the old face squashes to an edge (first half), the new one opens
-- from the edge (second half). The portrait and the ring are one face, the medallion the other; the cost gem, the label
-- and the glow wait until it has ended (MU.PowerFlipTick refreshes the board then).
function MU.PowerFlipApply(p)
    local fl = p.flip
    local half = MU.POWER_FLIP_TIME / 2
    local usedFace, f
    if fl.t < half then
        usedFace = not fl.to
        f = 1 - fl.t / half
    else
        usedFace = fl.to
        local u = (fl.t - half) / half
        if u > 1 then
            u = 1
        end
        f = 1 - (1 - u) * (1 - u)
    end
    if f < 0.03 then
        f = 0.03
    end
    local pad = MU.POWER_RING_PAD
    local w, h = p.frame:GetWidth(), p.frame:GetHeight()
    setGem(p.cost, nil)
    p.label:Hide()
    setGlow(p.glow, nil)
    if usedFace then
        p.art:Hide()
        p.ring:Hide()
        MU.SquashTex(p.back, p.frame, -pad, pad, w + pad, -(h + pad), f)
        p.back:Show()
    else
        p.back:Hide()
        p.art:Show()
        p.ring:Show()
        if p.artRect then
            MU.SquashTex(p.art, p.frame, p.artRect[1], p.artRect[2], p.artRect[3], p.artRect[4], f)
        end
        MU.SquashTex(p.ring, p.frame, -pad, pad, w + pad, -(h + pad), f)
    end
end

-- Advances the hero power flips in real time (called from MU.AnimTick); the end puts the plain faces back.
function MU.PowerFlipTick(dt)
    local ui = MU.ui
    if not ui then
        return
    end
    for _, key in ipairs({ "o", "m" }) do
        local p = ui[key].power
        if p.flip then
            p.flip.t = p.flip.t + dt
            if p.flip.t >= MU.POWER_FLIP_TIME then
                p.flip = nil
                if MU.frame and MU.frame:IsShown() then
                    MU.Refresh()
                end
            else
                MU.PowerFlipApply(p)
            end
        end
    end
end

local function refreshPower(key, s, g)
    local p = MU.ui[key].power
    local pw = s.power
    p.entity = pw.id
    p.cardId = pw.cardId
    -- the round portrait fills the ring's circular opening (anchor "circle"); without it today's crop in the
    -- rectangle inside the opening
    local hasArt
    if p.ring and MU.PieceAnchor("hero_power_ring", "circle") and MU.HasPowerTexture(pw.cardId) then
        local _, l, t, r, b = MU.ArtInRing(p.art, p.frame, "hero_power_ring", MU.POWER_RING_PAD, 6, "circle", MU.POWER_ART_GROW)
        p.artRect = { l, t, r, b }
        p.art:SetTexture(MU.POWER_TEXTURE_PATH .. pw.cardId)
        p.art:SetTexCoord(0, 1, 0, 1)
        p.art:SetVertexColor(1, 1, 1)
        hasArt = true
        p.artMode = "round"
    else
        if p.ring then
            local _, l, t, r, b = MU.ArtInRing(p.art, p.frame, "hero_power_ring", MU.POWER_RING_PAD, 6, nil, MU.POWER_ART_GROW)
            p.artRect = { l, t, r, b }
        else
            p.artRect = nil
        end
        hasArt = MU.SetArt(p.art, pw.cardId, MU.CROP_POWER)
        p.artMode = "crop"
    end
    if hasArt then
        p.label:Hide()
    else
        p.label:SetText(MA.CardName(pw.cardId) or "")
        p.label:Show()
    end
    local tint = nil
    if pw.cost > s.mana and not pw.used then
        tint = "down"
    end
    setGem(p.cost, pw.cost, tint)
    p.art:Show()
    if p.ring then
        -- the flip (MU.SquashTex) leaves the ring on a single anchor with a fixed size, and its last frame stops short
        -- of the full width: put the two-corner anchoring back so the ring always matches the button
        p.ring:ClearAllPoints()
        p.ring:SetPoint("TOPLEFT", p.frame, "TOPLEFT", -MU.POWER_RING_PAD, MU.POWER_RING_PAD)
        p.ring:SetPoint("BOTTOMRIGHT", p.frame, "BOTTOMRIGHT", MU.POWER_RING_PAD, -MU.POWER_RING_PAD)
        p.ring:Show()
    end
    if p.back then
        p.back:Hide()
    end
    if pw.used then
        if p.back then
            -- turned over: only the medallion, no portrait, ring or cost
            p.art:Hide()
            p.label:Hide()
            p.ring:Hide()
            setGem(p.cost, nil)
            p.back:ClearAllPoints()
            p.back:SetPoint("TOPLEFT", p.frame, "TOPLEFT", -MU.POWER_RING_PAD, MU.POWER_RING_PAD)
            p.back:SetPoint("BOTTOMRIGHT", p.frame, "BOTTOMRIGHT", MU.POWER_RING_PAD, -MU.POWER_RING_PAD)
            p.back:Show()
        else
            p.art:SetVertexColor(0.35, 0.35, 0.35)
        end
    end
    -- ready <-> used while the window is open turns the power over (not at a new match or when the window was closed)
    if p.seenUsed ~= nil and p.seenMid == g.mid and p.seenUsed ~= pw.used and p.back and MU.frame and MU.frame:IsShown()
        and AN.Mode() ~= "off" then
        p.flip = { t = 0, to = pw.used and true or false }
    elseif p.seenMid ~= g.mid then
        p.flip = nil
    end
    p.seenUsed, p.seenMid = pw.used, g.mid
    if p.flip then
        MU.PowerFlipApply(p)
    end
    local canAct = key == "m" and MA.IsMyTurn(g) and MA.PowerEntry(g) ~= nil
    local color = nil
    if pw.used or p.flip then
        color = nil
    elseif MU.sel and key == "m" and MU.sel.kind == "h" then
        color = "gold"
    elseif canAct and not MU.sel and not MA.pending and not MU.Behind() then
        color = "green"
    end
    setGlow(p.glow, color)
    p.frame:Show()
end

local function refreshWeapon(key, s, g)
    local w = MU.ui[key].weapon
    local wp = s.weapon
    if not wp then
        w.entity = nil
        w.frame:Hide()
        return
    end
    w.entity = wp.id
    w.cardId = wp.cardId
    local hasArt
    if w.ring and MU.PieceAnchor("weapon_ring", "circle") and MU.HasPowerTexture(wp.cardId) then
        -- the round portrait in the ring's opening (like the hero power)
        MU.ArtInRing(w.art, w.frame, "weapon_ring", MU.WEAPON_RING_PAD, 6, "circle", MU.POWER_ART_GROW)
        w.art:SetTexture(MU.POWER_TEXTURE_PATH .. wp.cardId)
        w.art:SetTexCoord(0, 1, 0, 1)
        w.art:SetVertexColor(1, 1, 1)
        hasArt = true
    else
        if w.ring then
            MU.ArtInRing(w.art, w.frame, "weapon_ring", MU.WEAPON_RING_PAD, 6, nil, MU.POWER_ART_GROW)
        end
        hasArt = MU.SetArt(w.art, wp.cardId, MU.CROP_SPELL)
    end
    if hasArt then
        w.label:Hide()
    else
        w.label:SetText(MA.CardName(wp.cardId) or "")
        w.label:Show()
    end
    setGem(w.atk, wp.attack, nil)
    setGem(w.dur, wp.durability, nil)
    w.frame:Show()
end

-- "Overload: 2 locked, 1 next turn" (nil when the side has no overload).
function MU.OverloadText(s)
    local parts = {}
    if s.overloadLocked > 0 then
        table.insert(parts, "|cffff6060" .. s.overloadLocked .. " locked|r")
    end
    if s.overloadOwed > 0 then
        table.insert(parts, "|cffffa040" .. s.overloadOwed .. " next turn|r")
    end
    if table.getn(parts) == 0 then
        return nil
    end
    return "Overload: " .. table.concat(parts, ", ")
end

-- Mana with the owner's art: the blue pill "mana / maximum" and, for me, the row of crystals (full, empty, or the lock
-- emblem for the ones Overload locks this turn). The opponent only has the pill, as in the original.
local function refreshManaArt(key, s)
    local u = MU.ui[key]
    u.pill:Show()
    u.pillText:SetText(s.mana .. "/" .. s.maxMana)
    u.manaText:SetText("")
    u.overload:SetText(MU.OverloadText(s) or "")
    local full = MU.Piece("mana_gem_full")
    local cs = u.manaGemH or MU.MANA_GEM_H
    local gw = cs * full.w / full.h * MU.MANA_GEM_WIDTH
    local lockedFrom = s.maxMana - s.overloadLocked   -- crystals above this are locked this turn
    for i = 1, MU.MAX_CRYSTALS do
        local t = u.crystals[i]
        if key == "m" and i <= s.maxMana then
            local name = "mana_gem_empty"
            if i > lockedFrom and i > s.mana then
                name = "mana_lock"
            elseif i <= s.mana then
                name = "mana_gem_full"
            end
            MU.SetPiece(t, name)
            local piece = MU.Piece(name)
            t:SetWidth(gw)
            t:SetHeight(gw / MU.MANA_GEM_WIDTH * piece.h / piece.w-5)
            t:Show()
        else
            t:Hide()
        end
    end
end

local function refreshMana(key, s)
    local u = MU.ui[key]
    if MU.ManaArt() and u.pill then
        refreshManaArt(key, s)
    else
        if u.pill then
            u.pill:Hide()
        end
        u.manaText:SetText(s.mana .. "/" .. s.maxMana)
        local overload = MU.OverloadText(s)
        u.overload:SetText(overload or "")
        local lockedFrom = s.maxMana - s.overloadLocked   -- crystals above this are locked this turn
        for i = 1, MU.MAX_CRYSTALS do
            local t = u.crystals[i]
            t:SetWidth(MU.CRYSTAL_W)
            t:SetHeight(MU.CRYSTAL_H)
            if i <= s.maxMana then
                if i <= s.mana then
                    if not MU.SetPiece(t, "mana_crystal") then
                        t:SetTexture(WHITE)
                        t:SetTexCoord(0, 1, 0, 1)
                        t:SetVertexColor(0.25, 0.55, 1)
                    end
                elseif i > lockedFrom then
                    -- locked by Overload: a red crystal
                    if MU.SetPiece(t, "mana_crystal") then
                        t:SetVertexColor(1, 0.3, 0.3)
                    else
                        t:SetTexture(WHITE)
                        t:SetTexCoord(0, 1, 0, 1)
                        t:SetVertexColor(0.75, 0.15, 0.15)
                    end
                else
                    if not MU.SetPiece(t, "mana_crystal_empty") then
                        t:SetTexture(WHITE)
                        t:SetTexCoord(0, 1, 0, 1)
                        t:SetVertexColor(0.14, 0.14, 0.22)
                    end
                end
                t:Show()
            else
                t:Hide()
            end
        end
    end
    local text = ""
    if s.fatigue > 0 then
        text = "|cffff6060Fatigue " .. s.fatigue .. "|r"
    end
    u.counts:SetText(text)
    if key == "m" then
        MU.UpdateManaGlow()
    end
end

-- The crystals a hovered card (or my hero power) would spend glow: the rightmost full ones, as many as it costs.
function MU.UpdateManaGlow()
    local u = MU.ui and MU.ui.m
    if not u or not u.glows then
        return
    end
    for i = 1, MU.MAX_CRYSTALS do
        u.glows[i]:Hide()
    end
    local cost = MU.manaPreview
    local g = game()
    local s = g and g.me
    local glow = MU.Piece("mana_gem_glow")
    if not cost or cost <= 0 or not s or not glow or not MU.ManaArt() then
        return
    end
    local n = cost
    if n > s.mana then
        n = s.mana
    end
    local full = MU.Piece("mana_gem_full")
    local gw = (u.manaGemH or MU.MANA_GEM_H) * full.w / full.h * MU.MANA_GLOW * MU.MANA_GEM_WIDTH
    for i = s.mana - n + 1, s.mana do
        local t = u.glows[i]
        if t and u.crystals[i]:IsShown() then
            MU.SetPiece(t, "mana_gem_glow")
            t:SetVertexColor(1, 1, 1)
            t:SetWidth(gw)
            t:SetHeight(gw / MU.MANA_GEM_WIDTH * glow.h / glow.w)
            t:Show()
        end
    end
end

function MU.SetManaPreview(cost)
    MU.manaPreview = cost
    MU.UpdateManaGlow()
end

local function refreshBacks(g)
    local u = MU.ui.o
    local n = g.opp.handCount
    if n > MU.MAX_HAND then
        n = MU.MAX_HAND
    end
    local xs = MU.RowPositions(n, MU.BACK_W, -8, MU.geom.bw, MU.CenterX(MU.geom.bw))
    for i = 1, MU.MAX_HAND do
        local b = u.backs[i]
        if i <= n then
            b:ClearAllPoints()
            b:SetPoint("TOPLEFT", MU.board, "TOPLEFT", xs[i], -2)
            b:Show()
        else
            b:Hide()
        end
    end
end

-- The frames of a hand slot that must rise together with it (the card, its text fallback, the gems, the glow).
local function handFrames(slot)
    local list = { slot.body, slot.widget.frame, slot.widget.fb }
    local keys = { "cost", "atk", "hp", "glow" }
    for i = 1, table.getn(keys) do
        local part = slot[keys[i]]
        if part and part.frame then
            table.insert(list, part.frame)
        end
    end
    return list
end

-- Puts hand slot i where its resting position (slot.pos, set by refreshHand) says. A hovered (popped) card is
-- bigger: it grows from its bottom edge, centred on the resting card and kept inside the board area, so the
-- cursor stays inside it. The frame's own scale does the growing, so its gems and glow come along; the offsets
-- of a scaled frame are in its own units, hence the division.
function MU.HandPlace(i)
    local slot = MU.ui and MU.ui.hand[i]
    local p = slot and slot.pos
    if not p or not MU.board then
        return
    end
    -- the slot frame is the mouse area: it always stays where the resting card is, at its resting size
    slot.frame:ClearAllPoints()
    slot.frame:SetScale(1)
    slot.frame:SetPoint("TOPLEFT", MU.board, "TOPLEFT", p.x, -p.y)
    -- the lean: a hovered card stands upright; the gems at the corners and the glow follow the tilted card art
    local angle = 0
    if not slot.popped and slot.widget.usingTexture then
        angle = p.angle or 0
    end
    H.CardWidget.SetTilt(slot.widget, angle)
    local hw, hh = MU.HAND_W / 2, MU.HAND_H / 2
    local corners = { cost = { -hw + MU.HAND_COST_AT[1] * MU.HAND_W, hh - MU.HAND_COST_AT[2] * MU.HAND_H }, atk = { -hw + 6, -hh + 6 }, hp = { hw - 6, -hh + 6 } }
    for name, c in pairs(corners) do
        local x, y = H.CardWidget.TiltPoint(c[1], c[2], angle)
        slot[name].frame:ClearAllPoints()
        slot[name].frame:SetPoint("CENTER", slot.body, "CENTER", x, y)
    end
    slot.glow.angle = angle
    MU.LayoutHandGlow(slot.glow)
    if slot.popped then
        local s = MU.HAND_POP_W / MU.HAND_W
        local left = p.x + MU.HAND_W / 2 - MU.HAND_POP_W / 2
        if left < 0 then
            left = 0
        elseif left > MU.geom.bw - MU.HAND_POP_W then
            left = MU.geom.bw - MU.HAND_POP_W
        end
        -- only the body (card, gems, highlight) grows, from the resting card's bottom edge; the offset is in the body's
        -- own scaled units
        slot.body:SetScale(s)
        slot.body:ClearAllPoints()
        slot.body:SetPoint("BOTTOMLEFT", slot.frame, "BOTTOMLEFT", (left - p.x) / s, 0)
    else
        slot.body:SetScale(1)
        slot.body:ClearAllPoints()
        slot.body:SetPoint("TOPLEFT", slot.frame, "TOPLEFT", 0, 0)
    end
end

-- The cursor is on hand card i: it grows in place and rises above its neighbours.
function MU.HandPop(i)
    local slot = MU.ui and MU.ui.hand[i]
    if not slot or slot.popped or not slot.entity or MU.drag then
        return
    end
    slot.popped = true
    local list = handFrames(slot)
    local delta = MU.board:GetFrameLevel() + MU.HAND_POP_LEVEL - slot.body:GetFrameLevel()
    if delta < 0 then
        delta = 0
    end
    slot.popLevels = {}
    for k = 1, table.getn(list) do
        local lvl = list[k]:GetFrameLevel()
        table.insert(slot.popLevels, { list[k], lvl })
        list[k]:SetFrameLevel(lvl + delta)
    end
    MU.HandPlace(i)
end

-- Back to the resting size and level (also when the card left the hand or the window closes).
function MU.HandUnpop(i)
    local slot = MU.ui and MU.ui.hand[i]
    if not slot or not slot.popped then
        return
    end
    slot.popped = nil
    if slot.popLevels then
        for k = 1, table.getn(slot.popLevels) do
            slot.popLevels[k][1]:SetFrameLevel(slot.popLevels[k][2])
        end
        slot.popLevels = nil
    end
    MU.HandPlace(i)
end

function MU.HandUnpopAll()
    if not MU.ui then
        return
    end
    for i = 1, MU.MAX_HAND do
        MU.HandUnpop(i)
    end
end

local function refreshHand(g)
    if MU.MullSwapping() then
        -- the new cards are shown by the mulligan animation first and fly into the hand at its end
        for i = 1, MU.MAX_HAND do
            MU.HandUnpop(i)
            MU.ui.hand[i].frame:Hide()
        end
        return
    end
    local layout = MU.HandLayout(table.getn(g.hand), MU.geom.bw, MU.geom.bh)
    for i = 1, MU.MAX_HAND do
        local slot = MU.ui.hand[i]
        local card = g.hand[i]
        if card then
            slot.entity = card.id
            slot.cardId = card.cardId
            slot.pos = layout[i]
            H.CardWidget.SetCard(slot.widget, card.cardId)
            MU.HandPlace(i)
            local costD, atkD, hpD = MA.HandOverlay(card)
            -- cost: cheaper is good (green), dearer bad (red); attack / health: higher is good
            local costTint = nil
            if costD == "down" then
                costTint = "up"
            elseif costD == "up" then
                costTint = "down"
            end
            if costD then
                setGem(slot.cost, card.cost, costTint)
            else
                setGem(slot.cost, nil)
            end
            if atkD then
                setGem(slot.atk, card.attack, atkD)
            else
                setGem(slot.atk, nil)
            end
            if hpD then
                setGem(slot.hp, card.health, hpD)
            else
                setGem(slot.hp, nil)
            end
            local canAct = MA.IsMyTurn(g) and MA.IsPlayable(g, card.id)
            local color = glowOf(g, card.id, canAct)
            -- Combo is on: a playable card with a Combo effect glows yellow instead of green
            slot.combo = color == "green" and MA.ComboActive(g) and MA.IsComboCard(card.cardId) and not MU.sel and not MU.choose
            if slot.combo then
                color = "combo"
            end
            setGlow(slot.glow, color)
            slot.frame:Show()
        else
            MU.HandUnpop(i)
            slot.entity = nil
            slot.pos = nil
            slot.frame:Hide()
        end
    end
end

-- The message bubble, the banner and the End Turn button.
function MU.RefreshMid()
    local ui = MU.ui
    local g = game()
    if not ui or not g then
        return
    end
    local mid = ui.mid
    local text = MU.hint
    if (not text or text == "") and MA.OpponentPicking(g) then
        text = MU.OPP_CHOOSING
    end
    if not text or text == "" then
        text = MA.message
    end
    if text and text ~= "" then
        if text ~= MU.bubbleLine and text ~= MU.OPP_CHOOSING then
            MU.bubbleLine, MU.bubbleAt = text, clock()     -- a new line: its seconds start now
        end
        mid.bubbleText:SetText(text)
        if ui.m and ui.m.hero then
            MU.PlaceSpeech(mid.bubble, mid.bubbleText, ui.m.hero.frame)
        end
        mid.bubble:Show()
    else
        mid.bubbleText:SetText("")
        mid.bubble:Hide()
    end
    -- the turn banner is not drawn here: it flashes when the turn changes (MU.CheckBanner)
    mid.turnText:SetText("")      -- the turn number is not shown
    local mine = MA.IsMyTurn(g)
    local enabled = mine and not MA.pending and not MA.IsPicking(g) and not MU.Behind()
    if enabled then
        mid.endButton:Enable()
    else
        mid.endButton:Disable()
    end
    -- the button shows "End Turn" on my turn and the stone "Enemy Turn" plate on the opponent's (dimmed only while my
    -- own turn is blocked, e.g. an action is being sent)
    if mid.endTex then
        MU.SetPiece(mid.endTex, mine and "end_turn_button" or "enemy_turn_button")
        mid.endButton:SetAlpha((mine and not enabled) and 0.6 or 1)
        if not enabled and mid.endGlow then
            mid.endGlow:Hide()
        end
    else
        mid.endButton:SetText(mine and "End Turn" or "Enemy Turn")
    end
    MU.timerShown = nil
    MU.RefreshTimer()
end

-- ---- the turn banner: "Your Turn" / "Opponent's Turn" flashes when the turn changes, then is hidden
-- (fade in MU.BANNER_IN, hold, fade out MU.BANNER_OUT; about 1.5 s). It takes no mouse clicks. The turn is the
-- one of the presented snapshot: while an animation plan runs the flash waits for the plan's commit. A window that
-- opens, a reload and a resync only note the turn (MU.SyncBanner) and never replay the banner.
function MU.BannerKey(g)
    if g and g.ph == "play" and g.turn > 0 then
        return tostring(g.mid) .. ":" .. g.turn
    end
    return nil
end

-- The turn of g counts as seen: no banner for it.
function MU.SyncBanner(g)
    MU.bannerSeen = MU.BannerKey(g) or "-"
end

function MU.EndBanner()
    MU.bannerRt = nil
    local mid = MU.ui and MU.ui.mid
    if mid then
        mid.banner:Hide()
        mid.banner:SetAlpha(1)
    end
end

function MU.StartBanner(g)
    local mid = MU.ui and MU.ui.mid
    if not mid then
        return
    end
    local mine = g.act == "m"
    local name = mine and "banner_your_turn" or "banner_opponent_turn"
    local piece = MU.Piece(name)
    if piece then
        MU.SetPiece(mid.bannerTex, name)
        local h = 80
        mid.banner:SetWidth(math.floor(h * (piece.w or 2) / (piece.h or 1) + 0.5))
        mid.banner:SetHeight(h)
        mid.bannerTex:Show()
        mid.bannerText:SetText("")
    else
        mid.bannerTex:Hide()
        mid.banner:SetWidth(190)
        mid.banner:SetHeight(40)
        mid.bannerText:SetText(mine and "|cff66ff66Your Turn|r" or "|cffff8844Opponent's Turn|r")
    end
    MU.bannerRt = { t = 0, mine = mine }
    mid.banner:SetAlpha(0)
    mid.banner:Show()
end

-- Opacity of the banner t seconds into its flash (0 .. 1), nil when it is over.
function MU.BannerAlpha(t)
    local total = MU.BANNER_IN + MU.BANNER_HOLD + MU.BANNER_OUT
    if t >= total then
        return nil
    elseif t < MU.BANNER_IN then
        return t / MU.BANNER_IN
    elseif t <= MU.BANNER_IN + MU.BANNER_HOLD then
        return 1
    end
    return (total - t) / MU.BANNER_OUT
end

function MU.BannerTick(dt)
    local rt = MU.bannerRt
    if not rt then
        return
    end
    rt.t = rt.t + dt
    local a = MU.BannerAlpha(rt.t)
    if not a then
        MU.EndBanner()
    else
        MU.ui.mid.banner:SetAlpha(a)
    end
end

-- Called with the presented snapshot on every refresh: a new turn starts the flash.
function MU.CheckBanner(g)
    if MU.MullSwapping() then
        return      -- the turn banner flashes when the mulligan animation ended
    end
    if g.ph ~= "play" then
        if MU.bannerRt then
            MU.EndBanner()
        end
        return
    end
    local key = MU.BannerKey(g)
    if MU.bannerSeen == nil then
        MU.bannerSeen = key or "-"
    elseif key and key ~= MU.bannerSeen then
        MU.bannerSeen = key
        MU.StartBanner(g)
    end
end

-- The Nozdormu turn countdown beside the End Turn button (whole seconds, red below
-- MA.TIMER_LOW, hidden without a timer). The model ticks in HsMatch.TimerTick (Hearthstone.OnUpdate);
-- this only repaints when the shown text changes.
function MU.RefreshTimer()
    local ui = MU.ui
    if not ui or not ui.mid or not ui.mid.timerText then
        return
    end
    local text = ui.mid.timerText
    local secs = MA.TimerSeconds()
    if secs == nil then
        if MU.timerShown ~= "" then
            MU.timerShown = ""
            text:SetText("")
            text:Hide()
        end
        return
    end
    local low = MA.TimerLow()
    local key = secs .. (low and "r" or "g")
    if MU.timerShown == key then
        return
    end
    MU.timerShown = key
    if low then
        text:SetText("|cffff2020" .. secs .. "|r")
    else
        text:SetText("|cffffd100" .. secs .. "|r")
    end
    text:Show()
end

-- The art crop for the picture of a card in a history tile: the oval of a minion or hero, the round window of a hero
-- power, else the art window of a spell or weapon.
function MU.LogCrop(cardId)
    local base = MA.CardBase(cardId)
    local ty = base and base.type
    if ty == "MINION" or ty == "HERO" then
        return MU.CROP_MINION
    elseif ty == "HERO_POWER" then
        return MU.CROP_POWER
    end
    return MU.CROP_SPELL
end

-- The history column: the newest tiles that fit, the newest at the top and the older ones below it (see MA.history).
-- Each tile shows the card that acted (a light grey rim: mine, red: the opponent's), a corner icon for the kind (attack, deathrattle, trigger, secret) and a
-- small picture of the target in the lower corner.
function MU.RefreshLog()
    local ui = MU.ui
    if not ui or not ui.log then
        return
    end
    local tiles = ui.log.tiles
    local items = MA.HistoryTail(ui.log.count)
    local n = table.getn(items)
    for i = 1, table.getn(tiles) do
        local tile = tiles[i]
        local entry = items[n - i + 1]          -- HistoryTail is oldest first: the column runs from the newest down
        tile.entry = entry
        if entry then
            local rim = MU.LOG_RIM[entry.side] or MU.LOG_RIM.m
            tile.rim:SetVertexColor(rim[1], rim[2], rim[3], 1)
            MU.SetArt(tile.art, entry.cardId, MU.LogCrop(entry.cardId))
            local icon = MU.LOG_ICONS[entry.kind]
            if icon then
                tile.icon:SetTexture(icon)
                tile.icon:Show()
            else
                tile.icon:Hide()
            end
            if entry.targetCardId then
                MU.SetArt(tile.small, entry.targetCardId, MU.LogCrop(entry.targetCardId))
                tile.smallRim:Show()
                tile.small:Show()
            else
                tile.smallRim:Hide()
                tile.small:Hide()
            end
            tile.frame:Show()
        else
            tile.frame:Hide()
        end
    end
end

-- Hovering a tile: just the large card of what acted, at the side (no plate, caption or tooltip).
function MU.OnLogEnter(tile)
    local entry = tile.entry
    if not entry then
        return
    end
    MU.HidePreview()
    MU.ShowPreview(entry.cardId, tile.frame, nil, true)
end

function MU.OnLogLeave()
    MU.HidePreview()
end

local function refreshMulligan(g)
    local m = MU.mull
    if m.anim and m.anim.kind == "swap" then
        -- the replaced cards are on their way: the animation owns the slots (the snapshot is already the play one)
        m.frame:Show()
        MU.MullApply()
        return
    end
    if g.ph ~= "mull" then
        m.frame:Hide()
        return
    end
    local offered = g.mulligan
    local n = table.getn(offered)
    -- the cards shrink (frame scale) when the board is too narrow for them
    local cw = MU.MULL_W
    if n > 0 then
        local fit = math.floor((MU.geom.bw - 48 - (n - 1) * MU.MULL_GAP) / n)
        if fit < cw then
            cw = fit
        end
    end
    local sc = cw / MU.MULL_W
    local xs = MU.RowPositions(n, cw, MU.MULL_GAP, MU.geom.bw)
    local cardH = MU.MULL_H * sc
    m.layout = {}
    for i = 1, MU.MULL_SLOTS do
        local slot = m.slots[i]
        local id = offered[i]
        if id then
            m.layout[i] = { cx = xs[i] + cw / 2, cy = MU.MULL_TOP + cardH / 2, sc = sc }
            local card = MA.FindHand(g, id)
            slot.entity = id
            slot.cardId = card and card.cardId
            slot.frame:SetScale(sc)
            slot.frame:ClearAllPoints()
            slot.frame:SetPoint("TOPLEFT", m.frame, "TOPLEFT", xs[i] / sc, -MU.MULL_TOP / sc)
            H.CardWidget.SetCard(slot.widget, slot.cardId)
            H.CardWidget.SetDimmed(slot.widget, false)
            if MA.marked[id] then
                slot.glow.color = nil
                slot.mark:Show()
            else
                slot.glow.color = "green"
                slot.mark:Hide()
            end
            MU.LayoutHandGlow(slot.glow)
            slot.frame:Show()
        else
            slot.entity = nil
            slot.glow.color = nil
            MU.LayoutHandGlow(slot.glow)
            slot.mark:Hide()
            slot.frame:Hide()
        end
    end
    -- the first view of an offer deals the cards from the deck (the Done button waits until they lie there)
    if n > 0 and not m.anim and m.dealMid ~= g.mid then
        MU.MullBeginDeal(g.mid, n)
    end
    if n > 0 then
        if m.bannerH then
            m.hint:SetText("")
        else
            m.hint:SetText("|cffffd100Keep or Replace Cards|r")
        end
        m.note:Hide()
        m.confirm:ClearAllPoints()
        m.confirm:SetPoint("TOP", m.frame, "TOP", 0, -(MU.MULL_TOP + cardH + 36))
        if MA.pending then
            m.confirm:Disable()
        else
            m.confirm:Enable()
        end
        if m.confirm.art then
            m.confirm:SetAlpha(MA.pending and 0.5 or 1)
        elseif m.confirm.label then
            m.confirm.label:SetAlpha(MA.pending and 0.5 or 1)
        end
        m.confirm:Show()
    else
        m.hint:SetText("|cffffd100Waiting for the match to start...|r")
        m.note:Hide()
        m.confirm:Hide()
    end
    m.frame:Show()
    if m.anim then
        -- being dealt: only the banner shows until the cards lie there
        m.note:Hide()
        m.confirm:Hide()
        MU.MullApply()
    end
end

-- Puts the player's hero portrait into the strips behind the emblem's arch (the card art with the crop of the
-- board's hero arch, or the flat colour of the hero's class without the card texture).
local function emblemBounds(e)
    local strips = e.strips
    local x0, y0, x1, y1 = strips[1][1], strips[1][2], strips[1][3], strips[1][4]
    for i = 2, table.getn(strips) do
        local st = strips[i]
        if st[1] < x0 then x0 = st[1] end
        if st[2] < y0 then y0 = st[2] end
        if st[3] > x1 then x1 = st[3] end
        if st[4] > y1 then y1 = st[4] end
    end
    return x0, y0, x1, y1
end

-- Width / height of the arch box on screen (the aspect the picture behind the arch must have).
function MU.EmblemBoxAspect(e)
    local x0, y0, x1, y1 = emblemBounds(e)
    return (x1 - x0) * (e.arch[3] - e.arch[1]) * e.aspect / ((y1 - y0) * (e.arch[4] - e.arch[2]))
end

-- The picture behind a card's arch: { path, u1, u2, v1, v2 } (the card art window) or { color = { r, g, b } } (the
-- flat colour of the card's class without the card texture).
local function cardEmblemPic(e, cardId)
    local altPath, altBottom = HsHeroes and HsHeroes.Texture(cardId)
    local has = cardId and (altPath ~= nil or CM.HasTexture(cardId))
    if not has then
        return { color = MU.ClassColor(cardId) }
    end
    local c = MU.EMBLEM_CROP
    local bottom = altBottom or 388 / 512
    if not altBottom and H.CardBottom then
        bottom = H.CardBottom(cardId)
    end
    -- the card's art window, with the aspect of the arch box (a 256 x 388 card image)
    local vh = c.w * 256 / MU.EmblemBoxAspect(e) / 388
    return { path = altPath or ((H.CARD_TEXTURE_PATH or "Interface\\AddOns\\Hearthstone\\Textures\\Cards\\") .. tostring(cardId)),
        u1 = c.cx - c.w / 2, u2 = c.cx + c.w / 2, v1 = (c.cy - vh / 2) * bottom, v2 = (c.cy + vh / 2) * bottom }
end

-- Puts a picture into the strips behind the emblem's arch (see cardEmblemPic).
local function setEmblemPic(r, e, pic)
    local strips = e.strips
    local x0, y0, x1, y1 = emblemBounds(e)
    local u1, u2, v1, v2 = pic.u1, pic.u2, pic.v1, pic.v2
    for i = 1, table.getn(r.strips) do
        local tex = r.strips[i]
        local st = strips[i]
        if st then
            if pic.path then
                tex:SetTexture(pic.path)
                tex:SetTexCoord(u1 + (u2 - u1) * (st[1] - x0) / (x1 - x0), u1 + (u2 - u1) * (st[3] - x0) / (x1 - x0),
                    v1 + (v2 - v1) * (st[2] - y0) / (y1 - y0), v1 + (v2 - v1) * (st[4] - y0) / (y1 - y0))
                tex:SetVertexColor(1, 1, 1)
            else
                tex:SetTexture(WHITE)
                tex:SetTexCoord(0, 1, 0, 1)
                tex:SetVertexColor(pic.color[1], pic.color[2], pic.color[3])
            end
            tex:Show()
        else
            tex:Hide()
        end
    end
end

-- The parts of an emblem on a box: its portrait strips (BORDER) under the emblem (ARTWORK). Every result view makes
-- its own (the match window and the Battlegrounds result screen).
function MU.CreateEmblem(box)
    local r = { box = box, strips = {} }
    for i = 1, MU.EMBLEM_MAX_STRIPS do
        local t = box:CreateTexture(nil, "BORDER")
        t:Hide()
        r.strips[i] = t
    end
    r.emblem = box:CreateTexture(nil, "ARTWORK")
    r.emblem:Hide()
    return r
end

function MU.HideEmblem(r)
    r.emblem:Hide()
    for i = 1, table.getn(r.strips) do
        r.strips[i]:Hide()
    end
end

-- An emblem of MU.EMBLEMS on the box, with the picture behind its arch. `kind` is "victory" or "defeat", `pic` the
-- picture (nil: the arch stays empty), `boxW` the width of the box the emblem is centred on, `h` its height on screen.
-- Returns the emblem's definition, or nil when the emblem texture is not installed (SetTexture answers nil for a file the
-- client cannot load): the caller shows its plain view instead.
function MU.ShowEmblem(r, kind, pic, boxW, h)
    local e = MU.EMBLEMS[kind or "defeat"]
    if not e or not r.emblem:SetTexture(MU.EMBLEM_PATH .. e.file) then
        return nil
    end
    h = h or MU.EMBLEM_H
    local w = math.floor(h * e.aspect + 0.5)
    r.emblem:SetTexCoord(0, e.right, 0, e.bottom)
    r.emblem:ClearAllPoints()
    r.emblem:SetPoint("TOP", r.box, "TOP", 0, 0)
    r.emblem:SetWidth(w)
    r.emblem:SetHeight(h)
    -- the arch box in pixels from the box's top left (the emblem is centred on the box)
    local left = math.floor((boxW - w) / 2 + 0.5)
    local ax, ay = left + w * e.arch[1], h * e.arch[2]
    local aw, ah = w * (e.arch[3] - e.arch[1]), h * (e.arch[4] - e.arch[2])
    for i = 1, table.getn(r.strips) do
        local st = e.strips[i]
        local tex = r.strips[i]
        tex:ClearAllPoints()
        if st then
            tex:SetPoint("TOPLEFT", r.box, "TOPLEFT", math.floor(ax + aw * st[1] + 0.5), -math.floor(ay + ah * st[2] + 0.5))
            tex:SetPoint("BOTTOMRIGHT", r.box, "TOPLEFT", math.floor(ax + aw * st[3] + 0.5), -math.floor(ay + ah * st[4] + 0.5))
        end
    end
    if pic then
        setEmblemPic(r, e, pic)
    else
        for i = 1, table.getn(r.strips) do
            r.strips[i]:Hide()
        end
    end
    r.emblem:Show()
    return e
end

local function showEmblem(r, g)
    local kind = MU.RESULT_EMBLEM[g.res] or "defeat"
    local ownHero = g.me and g.me.hero and g.me.hero.cardId
    return MU.ShowEmblem(r, kind, cardEmblemPic(MU.EMBLEMS[kind], HsHeroes and HsHeroes.ArtFor(ownHero, true) or ownHero),
        MU.EMBLEM_BOX_W, MU.EMBLEM_H) ~= nil
end

local function layoutResult(r, emblem, withTitle)
    r.reason:ClearAllPoints()
    r.summary:ClearAllPoints()
    r.leave:ClearAllPoints()
    r.title:ClearAllPoints()
    if emblem then
        r.box:SetWidth(MU.EMBLEM_BOX_W)
        r.box:SetHeight(MU.EMBLEM_H + 80)
        r.box:SetBackdrop(nil)
        -- only the Done button under the emblem (a draw or an interrupted match also names itself)
        local y = -(MU.EMBLEM_H - 6)
        if withTitle then
            r.title:SetPoint("TOP", r.box, "TOP", 0, -(MU.EMBLEM_H + 2))
            y = -(MU.EMBLEM_H + 30)
        end
        r.leave:SetPoint("TOP", r.box, "TOP", 0, y)
        r.reason:Hide()
        r.summary:Hide()
    else
        r.box:SetWidth(440)
        r.box:SetHeight(230)
        r.box:SetBackdrop({
            bgFile = DIALOG_BG, edgeFile = DIALOG_EDGE, tile = true, tileSize = 32, edgeSize = 32,
            insets = { left = 11, right = 12, top = 12, bottom = 11 },
        })
        r.title:SetPoint("TOP", r.box, "TOP", 0, -34)
        r.reason:SetPoint("TOP", r.box, "TOP", 0, -92)
        r.summary:SetPoint("TOP", r.box, "TOP", 0, -130)
        r.leave:SetPoint("BOTTOM", r.box, "BOTTOM", 0, 8)
        r.reason:Show()
        r.summary:Show()
    end
end

local function refreshResult(g)
    local r = MU.result
    if g.ph ~= "over" then
        r.frame:Hide()
        return
    end
    r.title:SetText(MA.ResultText(g))
    local c = RESULT_COLORS[g.res] or RESULT_COLORS.draw
    r.title:SetTextColor(c[1], c[2], c[3])
    r.reason:SetText(MA.ReasonText(g))
    r.summary:SetText("Turns: " .. g.turn .. "   " .. g.myName .. ": " .. g.me.hero.health .. "   "
        .. g.oppName .. ": " .. g.opp.hero.health)
    -- the emblem says Victory / Defeat itself; a draw and an interrupted match (which borrow one) name themselves
    local emblem = showEmblem(r, g)
    if not emblem then
        r.emblem:Hide()
        for i = 1, table.getn(r.strips) do
            r.strips[i]:Hide()
        end
    end
    local named = g.res == "draw" or g.res == "int"
    layoutResult(r, emblem, emblem and named)
    if emblem and not named then
        r.title:Hide()
    else
        r.title:Show()
    end
    r.frame:Show()
end

-- Size of a secret medallion for a hero frame of width w: half the frame width (the medallion art has padding).
function MU.SecretSize(w)
    local s = math.floor(w / 2 + 0.5)
    if s < MU.SECRET_SIZE then
        s = MU.SECRET_SIZE
    end
    return s
end

-- The medallion piece of a secret: the one of the class of the card (own secrets) or of the hero (the opponent's
-- hidden ones); an unknown class takes the mage one. Nil when no secret piece is installed.
function MU.SecretPiece(cardId)
    local db = HS_CardDB
    local c = cardId and type(db) == "table" and type(db.cards) == "table" and db.cards[cardId] or nil
    local name = MU.SECRET_PIECES[(c and c.class) or ""] or "secret_mage"
    if MU.Piece(name) then
        return name
    end
    if MU.Piece("secret_mage") then
        return "secret_mage"
    end
    return nil
end

-- Centres of n secret icons (x, y from the top left of the hero frame, y down) on the dome of the hero, like the
-- original game: the dome is an ellipse of half width w / 2 and half height dome (a round hero: dome = nil takes
-- h / 2); the icons sit at the angles of MU.SECRET_ANGLES (first on top), MU.SECRET_LIFT pixels outside the edge.
function MU.SecretPositions(n, w, h, dome)
    local out = {}
    local a = w / 2
    local b = dome
    if not b or b <= 0 then
        b = h / 2
    end
    for i = 1, n do
        local t = (MU.SECRET_ANGLES[i] or 0) * math.pi / 180
        local s, c = math.sin(t), math.cos(t)
        local nx, ny = s / a, c / b
        local len = math.sqrt(nx * nx + ny * ny)
        out[i] = { x = a + a * s + MU.SECRET_LIFT * nx / len, y = b - b * c - MU.SECRET_LIFT * ny / len }
    end
    return out
end

-- The secret icons on the arc over the hero (children of the hero frame, so they move with it): the player's own
-- show the card (tooltip: name and text), the opponent's are "?" (tooltip: Secret).
local function refreshSecrets(key, g)
    local u = MU.ui[key]
    local list = MA.Secrets(g, key)
    local n = table.getn(list)
    if n > MU.MAX_SECRETS then
        n = MU.MAX_SECRETS
    end
    local hero = u.hero
    local pos = MU.SecretPositions(n, hero.frame:GetWidth(), hero.frame:GetHeight(), hero.domePx)
    local size = MU.SecretSize(hero.frame:GetWidth())
    local heroCard = (key == "m" and g.me or g.opp).hero.cardId
    for i = 1, MU.MAX_SECRETS do
        local slot = u.secrets[i]
        local sec = list[i]
        if sec and i <= n then
            slot.frame:SetWidth(size)
            slot.frame:SetHeight(size)
            slot.frame:ClearAllPoints()
            slot.frame:SetPoint("CENTER", hero.frame, "TOPLEFT", math.floor(pos[i].x + 0.5), -math.floor(pos[i].y + 0.5))
            slot.entity = sec.id
            slot.cardId = sec.cardId
            -- the medallion of the class (own secret: the card's, the opponent's hidden one: the hero's)
            local piece = MU.SecretPiece(sec.cardId or heroCard)
            if piece then
                if slot.look ~= "icon" then
                    slot.look = "icon"
                    slot.frame:SetBackdrop(nil)
                end
                MU.SetPiece(slot.icon, piece)
                slot.icon:Show()
                slot.art:Hide()
                slot.label:SetText("")
            elseif slot.look ~= "plain" then
                slot.look = "plain"
                plate(slot.frame, 0.95)
                slot.icon:Hide()
                slot.art:Show()
            end
            if piece then
                -- nothing else to draw
            elseif sec.cardId then
                local has = MU.SetArt(slot.art, sec.cardId, MU.CROP_SPELL)
                if has then
                    slot.label:SetText("")
                else
                    slot.label:SetText("|cffffd100S|r")
                end
            else
                slot.art:SetTexture(WHITE)
                slot.art:SetTexCoord(0, 1, 0, 1)
                slot.art:SetVertexColor(0.3, 0.12, 0.42)
                slot.label:SetText("|cffffd100?|r")
            end
            slot.frame:Show()
        else
            slot.entity = nil
            slot.cardId = nil
            slot.frame:Hide()
        end
    end
end

-- The pending pick (Tracking): the options as cards in a centred panel; a click answers.
local function refreshPick(g)
    local p = MU.pick
    if not MA.IsPicking(g) then
        p.frame:Hide()
        for i = 1, MU.PICK_SLOTS do
            p.slots[i].entity = nil
            p.slots[i].frame:Hide()
        end
        return
    end
    local n = table.getn(g.pick)
    if n > MU.PICK_SLOTS then
        n = MU.PICK_SLOTS
    end
    local xs = MU.RowPositions(n, MU.PICK_W, 14, MU.geom.bw)
    for i = 1, MU.PICK_SLOTS do
        local slot = p.slots[i]
        local opt = g.pick[i]
        if opt and i <= n then
            slot.entity = opt.id
            slot.cardId = opt.cardId
            slot.frame:ClearAllPoints()
            slot.frame:SetPoint("TOPLEFT", p.frame, "TOPLEFT", xs[i], -110)
            H.CardWidget.SetCard(slot.widget, opt.cardId)
            slot.label:SetText("|cff66ff66Choose|r")
            slot.frame:Show()
        else
            slot.entity = nil
            slot.frame:Hide()
        end
    end
    p.title:SetText("Choose a card")
    p.hint:SetText("Click the card you want. You must choose before anything else.")
    p.frame:Show()
end

-- The Choose One chooser: the options of the hand card as cards; one that cannot be played now is dimmed.
local function refreshChooser(g)
    local c = MU.chooser
    local ch = MU.choose
    local opts = ch and MA.OptionsOf(g, ch.source)
    if not ch or not opts then
        c.frame:Hide()
        MU.chooserHidden = nil
        for i = 1, MA.MAX_OPTIONS do
            c.slots[i].entity = nil
            c.slots[i].frame:Hide()
        end
        return
    end
    local n = table.getn(opts)
    -- the layout of the reference: the cards a fraction of the board wide, centred across the middle of the board, with
    -- the banner over them. The row only narrows when four cards would run past the board's edges.
    local bw, bh = MU.geom.bw, MU.geom.bh
    local cw = math.floor(bw * MU.CHOOSE_SHARE + 0.5)
    local sc = cw / MU.CHOOSE_W
    local step = bw * MU.CHOOSE_STEP
    if n > 1 and (n - 1) * step + cw > bw * 0.9 then
        step = (bw * 0.9 - cw) / (n - 1)
    end
    local cardH = math.floor(MU.CHOOSE_W * 388 / 256 + 0.5) * sc
    local top = bh * MU.CHOOSE_CENTER_Y - cardH / 2
    if c.banner then
        c.banner:ClearAllPoints()
        c.banner:SetWidth(bw * MU.BANNER_SHARE)
        c.banner:SetHeight(bw * MU.BANNER_SHARE / 2)
        c.banner:SetPoint("TOP", c.panel, "TOP", 0, -bh * 0.03)
    end
    local entries = MA.ChoiceEntries(g, ch.source)
    for i = 1, MA.MAX_OPTIONS do
        local slot = c.slots[i]
        if i <= n then
            local centre = bw / 2 + (i - (n + 1) / 2) * step
            slot.entity = ch.source
            slot.cardId = opts[i]
            slot.choice = i
            slot.frame:SetScale(sc)
            slot.frame:ClearAllPoints()
            -- the frame is scaled about its corner, so its offsets are in its own units
            slot.frame:SetPoint("TOPLEFT", c.panel, "TOPLEFT", (centre - cw / 2) / sc, -top / sc)
            H.CardWidget.SetCard(slot.widget, opts[i])
            -- a playable option has the green glow of a playable card; one that cannot be played now is dimmed
            H.CardWidget.SetDimmed(slot.widget, not entries[i])
            -- the same padding in board pixels as a hand card (the frame is scaled by sc)
            slot.glow.padK = 1 / sc
            slot.glow.extraR, slot.glow.extraB = 8 / sc, 9 / sc
            setGlow(slot.glow, entries[i] and "green" or nil)
            slot.frame:Show()
        else
            slot.entity = nil
            slot.frame:Hide()
        end
    end
    c.title:SetText("Choose One")
    -- Hide takes the options away but keeps the board blocked (the frame still takes the clicks); Show brings them back
    if MU.chooserHidden then
        c.frame:SetBackdropColor(0, 0, 0, 0)
        c.panel:Hide()
        c.hideButton:Hide()
        c.showButton:Show()
    else
        c.frame:SetBackdropColor(0, 0, 0, MU.CHOOSE_DIM)
        c.panel:Show()
        c.hideButton:Show()
        c.showButton:Hide()
    end
    -- while an option card is dragged only that card stays (a hidden frame gets no mouse up)
    local dragged = MU.drag and MU.drag.choice and MU.drag.slot
    if dragged then
        c.frame:SetBackdropColor(0, 0, 0, 0)
        c.panel:Show()
        c.hideButton:Hide()
        c.showButton:Hide()
        if c.banner then c.banner:Hide() end
        for i = 1, MA.MAX_OPTIONS do
            if c.slots[i] ~= dragged then c.slots[i].frame:Hide() end
        end
    elseif c.banner then
        c.banner:Show()
    end
    c.frame:Show()
end

function MU.Refresh()
    local f = MU.frame
    if not f or not MU.ui then
        return
    end
    local g = game()
    if not g then
        f.title:SetText("Hearthstone")
        MU.RefreshSurrender()
        MU.MullStop()
        MU.mull.dealMid = nil
        MU.mull.swapPrep = nil
        MU.mull.frame:Hide()
        MU.pick.frame:Hide()
        MU.choose = nil
        MU.chooser.frame:Hide()
        MU.result.frame:Hide()
        MU.EndBanner()
        MU.bannerSeen = nil
        return
    end
    -- targeting only lives within one board state
    if MU.sel and (MU.sel.seq ~= g.seq or not MA.IsMyTurn(g) or MA.pending) then
        MU.sel = nil
    end
    if MU.choose and (MU.choose.seq ~= g.seq or not MA.IsMyTurn(g) or MA.pending or not MA.IsChoiceCard(g, MU.choose.source)) then
        MU.choose = nil
    end
    if MU.lastSeq ~= g.seq or MU.lastMid ~= g.mid then
        if MU.lastMid ~= g.mid or g.seq > (MU.lastSeq or 0) then
            MU.hint = nil
        end
        MU.lastSeq, MU.lastMid = g.seq, g.mid
    end
    MU.RefreshSurrender()
    refreshBoard("o", g.opp, g)
    refreshBoard("m", g.me, g)
    refreshHero("o", g.opp, g)
    refreshHero("m", g.me, g)
    refreshPower("o", g.opp, g)
    refreshPower("m", g.me, g)
    refreshWeapon("o", g.opp, g)
    refreshWeapon("m", g.me, g)
    refreshMana("o", g.opp)
    refreshMana("m", g.me)
    refreshBacks(g)
    MU.RefreshDecks(g)
    refreshHand(g)
    MU.RefreshMid()
    MU.CheckBanner(g)
    MU.RefreshLog()
    refreshSecrets("o", g)
    refreshSecrets("m", g)
    refreshMulligan(g)
    refreshPick(g)
    refreshChooser(g)
    refreshResult(g)
end

-- ------------------------------------------------------------------ hover
function MU.OnEnter(slot, extra)
    if not slot.cardId then
        return
    end
    MU.hoverId = slot.entity
    MU.ShowPreview(slot.cardId, slot.frame, extra)
end

function MU.OnMinionEnter(key, i)
    local g = game()
    local u = MU.ui[key]
    local m = g and ((key == "m" and g.me or g.opp).board[i])
    local slot = u.minions[i]
    if m and slot.cardId then
        -- just the card, at the right edge where the cards show (no anchor: always right)
        MU.hoverId = slot.entity
        MU.ShowPreview(slot.cardId, nil, nil, true)
    end
end

function MU.OnPowerEnter(key)
    MU.HidePreview()
    local g = game()
    local side = g and (key == "m" and g.me or g.opp)
    local u = MU.ui and MU.ui[key]
    if side and side.power and u then
        MU.ShowPreview(side.power.cardId, nil, nil, true)   -- no anchor: always on the right, where the cards show
    end
    local pw = g and key == "m" and g.me.power
    if pw and not pw.used then
        MU.SetManaPreview(pw.cost)
    end
end

-- Hovering a hero shows nothing: its numbers are in the gems.
function MU.OnHeroEnter(key)
    MU.HidePreview()
end

-- A hand card is not previewed at the side of the window: it grows in place (MU.HandPop). Its changed cost, attack
-- and health already show in its gems.
function MU.OnHandEnter(i)
    local g = game()
    if g and g.hand[i] then
        MU.HandPop(i)
        MU.SetManaPreview(g.hand[i].cost)
    end
end

-- Tooltip of a secret icon: the own card (name, text) or "Secret" for the opponent's.
function MU.OnSecretEnter(key, i)
    local g = game()
    local u = MU.ui[key]
    local sec = g and MA.Secrets(g, key)[i]
    if not sec then
        return
    end
    if sec.cardId then
        tooltip(u.secrets[i].frame, "ANCHOR_RIGHT", MA.CardName(sec.cardId), MA.CardText(sec.cardId), "Secret")
    else
        tooltip(u.secrets[i].frame, "ANCHOR_RIGHT", "Secret", "Your opponent's Secret.", "It is revealed when it triggers.")
    end
end

-- The art piece of a deck stack for n cards left (nil: no stack, deck_empty: the empty slot's glowing bar).
function MU.DeckPieceName(n)
    if not n then
        return nil
    end
    if n <= 0 then
        return "deck_empty"
    end
    if n >= MU.DECK_FULL_MIN then
        return "deck_full"
    end
    if n >= MU.DECK_HALF_MIN then
        return "deck_half"
    end
    return "deck_thin"
end

-- Shows each side's stack by its cards left, or the glowing bar in the empty slot at 0 cards (nothing without the pieces,
-- or while the deck is not placed on the board art).
function MU.RefreshDecks(g)
    local decks = MU.ui and MU.ui.deck
    if not decks then
        return
    end
    for _, key in ipairs({ "o", "m" }) do
        local d = decks[key]
        local s = g and (key == "m" and g.me or g.opp)
        local name = s and MU.DeckPieceName(s.deckCount)
        if d and d.stack then
            local tex = name == "deck_empty" and d.empty or d.stack
            local other = tex == d.stack and d.empty or d.stack
            if other then
                other:Hide()
            end
            if name and tex and (tex ~= d.empty or d.emptyPlaced) and MU.SetPiece(tex, name) then
                tex:Show()
            elseif tex then
                tex:Hide()
            end
        end
    end
end

-- Puts a deck's hover frame and its stack on the board art's slot (anchor name, board size bw x fh). False without the
-- anchor: the caller then keeps the place beside the End Turn button.
function MU.PlaceDeck(d, anchorName, bw, fh)
    local a = MU.PieceAnchor("board_field", anchorName)
    local p = MU.Piece("deck_full")
    d.emptyPlaced = false
    if not a or not p or type(p.w) ~= "number" or type(p.h) ~= "number" or p.h <= 0 or not d.stack then
        return false
    end
    local i = MU.FIELD_INSET
    local tw, th = bw - 2 * i, fh - 2 * i
    local l = i + a.left * tw - MU.DECK_STACK_PAD
    local t = i + MU.FieldY(a.top) * th
    local b = i + MU.FieldY(a.bottom) * th
    local h = (b - t) * MU.DECK_STACK_H
    local w = h * (p.w / p.h)
    t = t - (h - (b - t)) / 2
    d:ClearAllPoints()
    d:SetPoint("TOPLEFT", MU.board, "TOPLEFT", math.floor(l + 0.5), -math.floor(t + 0.5))
    d:SetWidth(math.floor(w + 0.5))
    d:SetHeight(math.floor(h + 0.5))
    d.stack:ClearAllPoints()
    d.stack:SetAllPoints(d)
    -- the empty slot's bar: its "inner" rectangle on the slot (relative to d), its glow around it
    local inner = MU.PieceAnchor("deck_empty", "inner")
    if d.empty and inner and inner.right > inner.left and inner.bottom > inner.top then
        local sl, sr = i + a.left * tw, i + a.right * tw
        local st, sb = i + MU.FieldY(a.top) * th, i + MU.FieldY(a.bottom) * th
        local gw = (sr - sl) / (inner.right - inner.left)
        local gh = (sb - st) / (inner.bottom - inner.top)
        local gl = sl - inner.left * gw - math.floor(l + 0.5)
        local gt = st - inner.top * gh - math.floor(t + 0.5)
        d.empty:ClearAllPoints()
        d.empty:SetPoint("TOPLEFT", d, "TOPLEFT", math.floor(gl + 0.5), -math.floor(gt + 0.5))
        d.empty:SetWidth(math.floor(gw + 0.5))
        d.empty:SetHeight(math.floor(gh + 0.5))
        d.emptyPlaced = true
    end
    return true
end

-- Tooltip of a card deck: how many cards that side has left to draw (key "m" mine, "o" the opponent's).
function MU.OnDeckEnter(key)
    local g = game()
    local s = g and (key == "m" and g.me or g.opp)
    if not s then
        return
    end
    local n = s.deckCount or 0
    local who = key == "m" and "Your deck" or "Opponent's deck"
    tooltip(MU.ui.deck[key], "ANCHOR_LEFT", who, n .. (n == 1 and " card left" or " cards left"))
end

-- The cards of the mulligan show nothing on hover: they are already shown large.
function MU.OnMullEnter(i)
    MU.HidePreview()
end

-- ------------------------------------------------------------------ animation (part A)
-- The beats come from MatchAnimations.lua (HsMatchAnim); this part draws them. Board coordinates: x to the
-- right and y downwards from the top left of MU.board, in design pixels (the window scale applies to all).
MU.OVERLAP = 20               -- design pixels an attacker overlaps its defender at the contact
MU.REACH_SCALE = { minion = 1, hero = 0.9, power = 1, weapon = 1 }  -- share of a frame that collides, by kind
MU.ANTICIPATE_PX = 8          -- the pull back before the strike
MU.LUNGE_LEVEL = 20           -- frame level of an attacker above the board (children keep their distance)
MU.FX_LEVEL = 150             -- frame level of the Fx layer above the board (over the overlays and the hand)
MU.SHAKE_PX, MU.SHAKE_TIME = 4, 0.22
MU.FLASH_TIME = 0.28
MU.DEATH_DRIFT = 12           -- pixels a dying minion sinks
MU.WBREAK_CRACK = 0.25        -- share of the weapon break that is the crack (flash, tremble) before the fall
MU.WBREAK_FALL, MU.WBREAK_DRIFT, MU.WBREAK_SHRINK = 12, 3, 0.2   -- pixels down, pixels aside, share of the size lost
MU.POOL_TEXTURES, MU.POOL_TEXTS, MU.MAX_FX = 24, 16, 48
MU.REVEAL_W = 200             -- the revealed card
MU.FATIGUE_W = 200            -- the Fatigue card (fatigue_card piece, 320x508 used texels)
MU.FATIGUE_TEXT = { left = 0.17, right = 0.83, top = 0.60, bottom = 0.77 }   -- the parchment panel of the piece, as shares of it
MU.FAMILY_COLORS = {
    fire = { 1, 0.5, 0.1 }, frost = { 0.5, 0.8, 1 }, arcane = { 0.7, 0.4, 1 }, shadow = { 0.45, 0.2, 0.6 },
    holy = { 1, 0.9, 0.5 }, heal = { 0.3, 1, 0.4 }, area = { 1, 0.5, 0.2 }, buff = { 1, 0.85, 0.3 },
    silence = { 0.6, 0.6, 0.75 }, transform = { 0.9, 0.5, 1 }, summon = { 0.85, 0.85, 0.85 },
    destroy = { 0.6, 0.15, 0.8 }, secret = { 0.8, 0.4, 1 }, generic = { 1, 1, 0.7 }, nature = { 0.4, 0.9, 0.5 },
}
local SHIELD_TEXTURE = "Interface\\Cooldown\\star4"

MU.touched = {}               -- slots with a temporary point / alpha / frame level: restored by MU.RestoreAll
MU.fxItems = {}               -- running timed items (flashes, numbers, shakes ...)
MU.pool = nil                 -- { tex = { { obj, used } }, text = { { obj, used } } }
MU.fxFrame = nil              -- the layer above the board that holds the pooled textures and font strings
MU.reveal = nil               -- { frame, widget, caption }: the large card of an opponent's play
MU.fatigue = nil              -- { frame, body }: the Fatigue card shown before fatigue damage
MU.beatRt = nil               -- runtime state of the beat that plays

-- Hook of part B (native effects): PlayEffect(beat, ctx) runs when an effect beat starts. ctx: { beat, dur,
-- family, color = { r, g, b }, src = anchor or nil (the casting entity, else its hero), aim = anchor or nil
-- (the chosen target), targets = { anchor ... } (a future summon uses its result slot; other absent entities get
-- the centre of their side's board with .missing = true), area = bool, board = anchor of the middle of the field }.
-- An anchor is { id, kind ("hero"|"minion"|"power"|"weapon"|"side"), side ("m"|"o"), x, y (centre), w, h, slot }.
-- A replacement may call MU.Fx.DefaultEffect(beat, ctx) for the 2D pulse; an error in it falls back to the
-- pulse. Stop() is called when the effects of a plan must end now (skip, hide, new match): a replacement
-- stops its loops and returns its frames to its pool there.
-- Init(fxFrame) (optional) runs once when the window is built (MatchFx.lua creates its model frames there).
MU.Fx = MU.Fx or {}

local function easeOut(u)
    return 1 - (1 - u) * (1 - u)
end

-- ---- slots: the frames that move (minions, heroes)
-- Puts a slot's frame at its home plus the temporary displacement (lunge lx, ly; shake sx, sy; sink ay) and alpha.
function MU.PlaceSlot(slot)
    local h = slot.home
    if not h or not MU.board then
        return
    end
    slot.frame:ClearAllPoints()
    slot.frame:SetPoint(h.point, MU.board, "TOPLEFT", h.x + (slot.lx or 0) + (slot.sx or 0),
        h.y - ((slot.ly or 0) + (slot.sy or 0) + (slot.ay or 0)))
    if slot.alpha then
        slot.frame:SetAlpha(slot.alpha)
    end
    if slot.scale and slot.baseW then
        slot.frame:SetWidth(slot.baseW * slot.scale)
        slot.frame:SetHeight(slot.baseH * slot.scale)
    end
end

local function touch(slot)
    MU.touched[slot] = true
end

-- The frames of a slot that must rise with it (gems, glow, rim).
local function slotFrames(slot)
    local list = { slot.frame }
    local keys = { "atk", "hp", "armor" }
    for i = 1, table.getn(keys) do
        local g = slot[keys[i]]
        if g and g.frame then
            table.insert(list, g.frame)
        end
    end
    if slot.glow and slot.glow.frame then
        table.insert(list, slot.glow.frame)
    end
    if slot.rim then
        table.insert(list, slot.rim)
    end
    for i = 1, table.getn(slot.followers or {}) do
        table.insert(list, slot.followers[i])
    end
    return list
end

local function raiseSlot(slot)
    if slot.levels or not MU.board then
        return
    end
    local delta = MU.board:GetFrameLevel() + MU.LUNGE_LEVEL - slot.frame:GetFrameLevel()
    if delta < 0 then
        delta = 0
    end
    slot.levels = {}
    local list = slotFrames(slot)
    for i = 1, table.getn(list) do
        local lvl = list[i]:GetFrameLevel()
        table.insert(slot.levels, { list[i], lvl })
        list[i]:SetFrameLevel(lvl + delta)
    end
end

local function restoreSlot(slot)
    slot.lx, slot.ly, slot.sx, slot.sy, slot.ay, slot.alpha = nil, nil, nil, nil, nil, nil
    if slot.levels then
        for i = 1, table.getn(slot.levels) do
            slot.levels[i][1]:SetFrameLevel(slot.levels[i][2])
        end
        slot.levels = nil
    end
    if slot.scale then
        slot.scale = nil
        if slot.baseW then
            slot.frame:SetWidth(slot.baseW)
            slot.frame:SetHeight(slot.baseH)
        end
    end
    slot.frame:SetAlpha(1)
    MU.PlaceSlot(slot)
end

-- Restores every temporary point, alpha, frame level and hidden piece of the moved frames.
function MU.RestoreAll()
    for slot in pairs(MU.touched) do
        restoreSlot(slot)
        MU.touched[slot] = nil
    end
end

-- Re-applies the displacement of the frames that are mid-animation (after a layout pass moved them home).
function MU.ReapplyAnim()
    for slot in pairs(MU.touched) do
        MU.PlaceSlot(slot)
    end
end

-- ---- anchors
local function anchorOf(slot, kind, key)
    local r = slot.dragRect or slot.rect
    if not r then
        return nil
    end
    return { id = slot.entity, kind = kind, side = key, slot = slot, x = r.x + r.w / 2, y = r.y + r.h / 2, w = r.w,
        h = r.h }
end

-- The frame that shows entity id on the presented board as an anchor (centre in board coordinates), or nil.
function MU.AnchorOf(id)
    local ui = MU.ui
    if not ui or not id or id == 0 then
        return nil
    end
    local keys = { "o", "m" }
    for k = 1, 2 do
        local key = keys[k]
        local u = ui[key]
        if u.hero.entity == id and u.hero.frame:IsShown() then
            return anchorOf(u.hero, "hero", key)
        end
        for i = 1, MU.MAX_BOARD do
            local s = u.minions[i]
            if s.entity == id and s.frame:IsShown() then
                return anchorOf(s, "minion", key)
            end
        end
        if u.power.entity == id and u.power.frame:IsShown() then
            return anchorOf(u.power, "power", key)
        end
        if u.weapon.entity == id and u.weapon.frame:IsShown() then
            return anchorOf(u.weapon, "weapon", key)
        end
    end
    return nil
end

-- The middle of the board row of a side ("m" or "o") as an anchor (kind "side", missing = true).
function MU.SideCenter(key)
    local y = MU.RowTop(key, MU.geom.fh)
    return { id = 0, kind = "side", side = key, missing = true, x = MU.CenterX(MU.geom.bw), y = y + MU.MINION_H / 2,
        w = MU.MINION_W, h = MU.MINION_H }
end

-- The middle of the field (between the two rows) as an anchor.
function MU.BoardCenter()
    return { id = 0, kind = "board", side = "-", x = MU.CenterX(MU.geom.bw), y = MU.geom.fh * MU.MID_Y, w = MU.geom.bw * 0.7,
        h = MU.geom.fh * 0.3 }
end

-- The hero of a side as an anchor, or nil.
function MU.HeroAnchor(key)
    local u = MU.ui and MU.ui[key]
    if u and u.hero.entity and u.hero.frame:IsShown() then
        return anchorOf(u.hero, "hero", key)
    end
    return nil
end

-- Where an attacker's centre ends up: from the centres and the collision extents of both frames, the travel
-- is the distance minus the combined reach plus MU.OVERLAP. The combined reach of two rectangles along the
-- direction is where their edges touch (it equals the sum of both reaches along an axis). Returns the contact
-- centre cx, cy, the travel, and the unit direction ux, uy.
function MU.ContactPoint(a, t)
    local dx, dy = t.x - a.x, t.y - a.y
    local dist = math.sqrt(dx * dx + dy * dy)
    if dist < 0.001 then
        return a.x, a.y, 0, 0, 1
    end
    local ux, uy = dx / dist, dy / dist
    local sa, st = MU.REACH_SCALE[a.kind] or 1, MU.REACH_SCALE[t.kind] or 1
    local combined = 1e30
    if math.abs(ux) > 0.0001 then
        combined = (a.w * sa + t.w * st) / 2 / math.abs(ux)
    end
    if math.abs(uy) > 0.0001 then
        local c = (a.h * sa + t.h * st) / 2 / math.abs(uy)
        if c < combined then
            combined = c
        end
    end
    local travel = dist - (combined - MU.OVERLAP)
    if travel < 0 then
        travel = 0
    end
    if travel > dist then
        travel = dist
    end
    return a.x + ux * travel, a.y + uy * travel, travel, ux, uy
end

-- ---- pools and timed items
local function buildFx(board)
    local f = CreateFrame("Frame", "HearthstoneMatchFx", board)
    f:SetAllPoints(board)
    f:SetFrameLevel(board:GetFrameLevel() + MU.FX_LEVEL)
    MU.fxFrame = f
    local pool = { tex = {}, text = {} }
    for i = 1, MU.POOL_TEXTURES do
        local t = f:CreateTexture(nil, "OVERLAY")
        t:SetTexture(WHITE)
        t:SetBlendMode("ADD")
        t:Hide()
        pool.tex[i] = { obj = t, used = false }
    end
    for i = 1, MU.POOL_TEXTS do
        local fs = f:CreateFontString(nil, "OVERLAY", "GameFontNormal")
        fs:SetFont(NUMBER_FONT, 22, "OUTLINE")
        fs:SetJustifyH("CENTER")
        fs:Hide()
        pool.text[i] = { obj = fs, used = false }
    end
    MU.pool = pool
    MU.fxItems = {}
    -- the card of an opponent's play, large, in the middle of the field
    local rf = CreateFrame("Frame", "HearthstoneMatchReveal", board)
    rf:SetWidth(MU.REVEAL_W)
    rf:SetHeight(math.floor(MU.REVEAL_W * 388 / 256 + 0.5) + 22)
    rf:SetFrameLevel(board:GetFrameLevel() + MU.FX_LEVEL - 5)
    rf:EnableMouse(false)              -- a click on the revealed card reaches the board (skips the animation)
    local widget = H.CardWidget.Create(rf, "HearthstoneMatchRevealCard", MU.REVEAL_W, true)
    widget.frame:EnableMouse(false)
    if widget.fb then
        widget.fb:EnableMouse(false)
    end
    widget.frame:SetPoint("BOTTOM", rf, "BOTTOM", 0, 0)
    local caption = rf:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    caption:SetPoint("BOTTOM", widget.frame, "TOP", 0, 4)
    rf:Hide()
    MU.reveal = { frame = rf, widget = widget, caption = caption }
    -- the Fatigue card: art with the damage text on its parchment panel (only when the fatigue_card piece is installed)
    local fp = MU.Piece("fatigue_card")
    if fp and type(fp.w) == "number" and type(fp.h) == "number" and fp.h > 0 then
        local ff = CreateFrame("Frame", "HearthstoneMatchFatigue", board)
        local fw = MU.FATIGUE_W
        local fh = math.floor(fw * fp.h / fp.w + 0.5)
        ff:SetWidth(fw)
        ff:SetHeight(fh)
        ff:SetFrameLevel(board:GetFrameLevel() + MU.FX_LEVEL - 5)
        ff:EnableMouse(false)
        local art = ff:CreateTexture(nil, "ARTWORK")
        art:SetAllPoints(ff)
        MU.SetPiece(art, "fatigue_card")
        local tx = MU.FATIGUE_TEXT
        local body = ff:CreateFontString(nil, "OVERLAY", "GameFontNormal")
        body:SetPoint("TOPLEFT", ff, "TOPLEFT", math.floor(tx.left * fw), -math.floor(tx.top * fh))
        body:SetPoint("BOTTOMRIGHT", ff, "TOPLEFT", math.floor(tx.right * fw), -math.floor(tx.bottom * fh))
        body:SetJustifyH("CENTER")
        body:SetJustifyV("MIDDLE")
        body:SetTextColor(0.12, 0.1, 0.08)       -- dark ink on the parchment
        body:SetShadowColor(0, 0, 0, 0)
        ff:Hide()
        MU.fatigue = { frame = ff, body = body }
    end
    -- part B (MatchFx.lua): the pool of native effect models is built once with the window
    if MU.Fx.Init then
        pcall(MU.Fx.Init, f)
    end
end

-- A pooled texture { obj = Texture, used = true }, or nil when the pool is empty. It is ADD blended, white,
-- hidden and unplaced; the caller shows it and gives it back with MU.FreeTexture.
function MU.AllocTexture()
    local pool = MU.pool
    if not pool then
        return nil
    end
    for i = 1, table.getn(pool.tex) do
        if not pool.tex[i].used then
            pool.tex[i].used = true
            return pool.tex[i]
        end
    end
    return nil
end

function MU.FreeTexture(h)
    if not h or not h.used then
        return
    end
    h.used = false
    local t = h.obj
    t:Hide()
    t:ClearAllPoints()
    t:SetTexture(WHITE)
    t:SetTexCoord(0, 1, 0, 1)
    t:SetVertexColor(1, 1, 1, 1)
    t:SetBlendMode("ADD")
    t:SetAlpha(1)
end

-- A pooled font string { obj = FontString (22 pt, outline), used = true }, or nil.
function MU.AllocText()
    local pool = MU.pool
    if not pool then
        return nil
    end
    for i = 1, table.getn(pool.text) do
        if not pool.text[i].used then
            pool.text[i].used = true
            return pool.text[i]
        end
    end
    return nil
end

function MU.FreeText(h)
    if not h or not h.used then
        return
    end
    h.used = false
    local fs = h.obj
    fs:Hide()
    fs:ClearAllPoints()
    fs:SetText("")
    fs:SetTextColor(1, 1, 1)
    fs:SetAlpha(1)
end

-- Centres a pooled region on (x, y) of the board, w x h.
function MU.PlaceRect(obj, x, y, w, h)
    obj:ClearAllPoints()
    obj:SetWidth(w)
    obj:SetHeight(h)
    obj:SetPoint("CENTER", MU.board, "TOPLEFT", x, -y)
end

-- Pool numbers: how many of each kind are in use, and the running items (a leak shows as a number that
-- stays above 0 after everything ended).
function MU.FxStats()
    local out = { textures = 0, texts = 0, items = table.getn(MU.fxItems), touched = 0 }
    if MU.pool then
        for i = 1, table.getn(MU.pool.tex) do
            if MU.pool.tex[i].used then
                out.textures = out.textures + 1
            end
        end
        for i = 1, table.getn(MU.pool.text) do
            if MU.pool.text[i].used then
                out.texts = out.texts + 1
            end
        end
    end
    for _ in pairs(MU.touched) do
        out.touched = out.touched + 1
    end
    return out
end

-- Runs a timed item: item = { dur, update = function(item, u) (u = 0..1), finish = function(item), kind }.
-- It is driven by MU.AnimTick; finish always runs (also on a skip) and must free what the item holds. Returns
-- the item, or nil when too many run (finish ran at once).
function MU.AddFx(item)
    if table.getn(MU.fxItems) >= MU.MAX_FX then
        if item.finish then
            item.finish(item)
        end
        return nil
    end
    item.t = 0
    table.insert(MU.fxItems, item)
    if item.update then
        item.update(item, 0)
    end
    return item
end

function MU.FxTick(dt)
    local list = MU.fxItems
    for i = table.getn(list), 1, -1 do
        local it = list[i]
        if it then
            if it.manual then
                -- The owning animation beat supplies exact elapsed time through MU.Fx.TickEffect.
            elseif it.skipNextTick then
                it.skipNextTick = nil
            else
                it.t = it.t + dt
                if it.t >= it.dur then
                    table.remove(list, i)
                    if it.finish then
                        it.finish(it)
                    end
                elseif it.update then
                    it.update(it, it.t / it.dur)
                end
            end
        end
    end
end

-- Ends every running item now (finish runs).
function MU.ClearFx()
    local list = MU.fxItems
    MU.fxItems = {}
    for i = table.getn(list), 1, -1 do
        if list[i].finish then
            list[i].finish(list[i])
        end
    end
end

-- Ends the shakes now: they move frames, and a plan that commits must leave the frames at home.
local function finishShakes()
    local list = MU.fxItems
    for i = table.getn(list), 1, -1 do
        if list[i].kind == "shake" then
            local it = table.remove(list, i)
            it.finish(it)
        end
    end
end

function MU.FamilyColor(family)
    return MU.FAMILY_COLORS[family or "generic"] or MU.FAMILY_COLORS.generic
end

-- ---- feedback pieces
-- A flash over an anchor that fades out.
function MU.Flash(a, r, g, b, alpha, dur, pad)
    local h = MU.AllocTexture()
    if not h then
        return nil
    end
    MU.PlaceRect(h.obj, a.x, a.y, a.w + 2 * pad, a.h + 2 * pad)
    h.obj:SetVertexColor(r, g, b, alpha)
    h.obj:SetAlpha(1)
    h.obj:Show()
    return MU.AddFx({ dur = dur, tex = h, kind = "flash",
        update = function(it, u)
            it.tex.obj:SetAlpha(math.max(0, 1 - u))
        end,
        finish = function(it)
            MU.FreeTexture(it.tex)
        end })
end

-- The default 2D effect: an expanding, fading glow of the family colour over an anchor.
function MU.Pulse(a, color, dur)
    local h = MU.AllocTexture()
    if not h then
        return nil
    end
    h.obj:SetVertexColor(color[1], color[2], color[3], 0.7)
    h.obj:Show()
    return MU.AddFx({ dur = dur or AN.EFFECT, tex = h, a = a, kind = "pulse",
        update = function(it, u)
            local s = 0.7 + 0.5 * easeOut(u)
            MU.PlaceRect(it.tex.obj, it.a.x, it.a.y, it.a.w * s, it.a.h * s)
            it.tex.obj:SetAlpha(math.max(0, 1 - u))
        end,
        finish = function(it)
            MU.FreeTexture(it.tex)
        end })
end

-- The flash of a lost Divine Shield: a golden burst that grows and fades.
function MU.ShieldPop(a)
    local h = MU.AllocTexture()
    if not h then
        return nil
    end
    h.obj:SetTexture(SHIELD_TEXTURE)
    h.obj:SetVertexColor(1, 0.9, 0.4, 1)
    h.obj:Show()
    return MU.AddFx({ dur = 0.3, tex = h, a = a, kind = "pop",
        update = function(it, u)
            local s = 0.6 + 0.9 * easeOut(u)
            MU.PlaceRect(it.tex.obj, it.a.x, it.a.y, it.a.w * s, it.a.h * s)
            it.tex.obj:SetAlpha(math.max(0, 1 - u))
        end,
        finish = function(it)
            MU.FreeTexture(it.tex)
        end })
end

-- A number that rises MU.NUMBER_RISE pixels and fades (index: the second number on one entity sits beside it).
function MU.FloatText(a, text, r, g, b, index)
    local h = MU.AllocText()
    if not h then
        return nil
    end
    local fs = h.obj
    fs:SetText(text)
    fs:SetTextColor(r, g, b)
    fs:SetAlpha(1)
    fs:Show()
    local x0 = a.x + (index or 0) * 16
    local y0 = a.y - a.h / 2 + 14
    return MU.AddFx({ dur = AN.NUMBER_FADE, text = h, kind = "number",
        update = function(it, u)
            local fo = it.text.obj
            fo:ClearAllPoints()
            fo:SetPoint("CENTER", MU.board, "TOPLEFT", x0, -(y0 - AN.NUMBER_RISE * easeOut(u)))
            fo:SetAlpha(math.max(0, 1 - u))
        end,
        finish = function(it)
            MU.FreeText(it.text)
        end })
end

-- A short shake of a frame around its home.
function MU.Shake(slot)
    if not slot or not slot.home then
        return nil
    end
    touch(slot)
    return MU.AddFx({ dur = MU.SHAKE_TIME, slot = slot, kind = "shake",
        update = function(it, u)
            local amp = MU.SHAKE_PX * (1 - u)
            it.slot.sx = amp * math.sin(u * 28)
            it.slot.sy = amp * 0.5 * math.cos(u * 36)
            MU.PlaceSlot(it.slot)
        end,
        finish = function(it)
            it.slot.sx, it.slot.sy = nil, nil
            MU.PlaceSlot(it.slot)
        end })
end

-- The feedback of the hits of one step, all at once: red -N and a flash and shake on what lost health, green
-- +N for healing, the shield pop instead of a number, +N armor. lunger: the slot that is attacking right now
-- (it is not shaken). Returns the number of entities that were on the board.
function MU.ShowHits(hits, lunger)
    local count = {}
    local shown = 0
    for k = 1, table.getn(hits or {}) do
        local h = hits[k]
        local a = MU.AnchorOf(h.id)
        if a then
            local n = count[h.id] or 0
            if n == 0 then
                shown = shown + 1
            end
            count[h.id] = n + 1
            if h.kind == "dmg" then
                if h.amount > 0 then
                    MU.FloatText(a, "-" .. h.amount, 1, 0.2, 0.2, n)
                end
                if not h.effectVisual then
                    MU.Flash(a, 1, 0.55, 0.15, 0.55, MU.FLASH_TIME, 4)
                end
                if a.slot ~= lunger then
                    MU.Shake(a.slot)
                end
            elseif h.kind == "heal" then
                if h.amount > 0 then
                    MU.FloatText(a, "+" .. h.amount, 0.3, 1, 0.3, n)
                end
                if not h.effectVisual then
                    MU.Flash(a, 0.3, 1, 0.4, 0.4, MU.FLASH_TIME, 2)
                end
            elseif h.kind == "pop" then
                MU.ShieldPop(a)
            elseif h.kind == "arm" then
                if h.amount > 0 then
                    MU.FloatText(a, "+" .. h.amount, 0.75, 0.8, 0.9, n)
                end
            end
        end
    end
    return shown
end

-- ---- the generic effect (2D)
function MU.EffectContext(beat)
    local ctx = { beat = beat, dur = beat.dur, family = beat.family, color = MU.FamilyColor(beat.family),
        visual = beat.visual, flight = beat.flight, tint = beat.tint, fxScale = beat.fxScale,
        targets = {}, area = beat.area, board = MU.BoardCenter() }
    ctx.src = MU.AnchorOf(beat.source)
    if beat.aim and beat.aim ~= 0 then
        ctx.aim = MU.AnchorOf(beat.aim)
    end
    local ids = beat.targets or {}
    local function futureSummon(id, key)
        local pos = beat.slots and beat.slots[id]
        if type(pos) ~= "number" or pos ~= pos or pos ~= math.floor(pos) or pos < 1 or pos > MU.MAX_BOARD
            or (key ~= "m" and key ~= "o") then
            return nil
        end
        local plan = AN.Current()
        local function boardCount(g)
            local side = g and (key == "m" and g.me or g.opp)
            return side and type(side.board) == "table" and table.getn(side.board) or 0
        end
        local current = game()
        local count = math.max(pos, boardCount(current), boardCount(plan and plan.prev), boardCount(plan and plan.game))
        if count > MU.MAX_BOARD then count = MU.MAX_BOARD end
        local xs = MU.RowPositions(count, MU.MINION_W, MU.MINION_GAP, MU.geom.bw, MU.CenterX(MU.geom.bw))
        local left = xs[pos]
        if not left then return nil end
        return { id = id, kind = "minion", side = key, x = left + MU.MINION_W / 2,
            y = MU.RowTop(key, MU.geom.fh) + MU.MINION_H / 2, w = MU.MINION_W, h = MU.MINION_H }
    end
    local function futureWeapon(id, key)
        if key ~= "m" and key ~= "o" then return nil end
        local u = MU.ui and MU.ui[key]
        local r = u and u.weapon and u.weapon.rect
        if not r or type(r.x) ~= "number" or type(r.y) ~= "number" then return nil end
        return { id = id, kind = "weapon", side = key, x = r.x + r.w / 2, y = r.y + r.h / 2,
            w = r.w, h = r.h }
    end
    if not ctx.src and beat.source and beat.source ~= 0 then
        ctx.src = futureSummon(beat.source, beat.side)
    end
    if not ctx.src and (beat.side == "m" or beat.side == "o") then
        ctx.src = MU.HeroAnchor(beat.side)
    end
    local function heroTarget(id, key)
        local a = MU.HeroAnchor(key)
        if not a then return nil end
        return { id = id, kind = a.kind, side = a.side, slot = a.slot,
            x = a.x, y = a.y, w = a.w, h = a.h }
    end
    for i = 1, table.getn(ids) do
        local side = beat.sides and beat.sides[ids[i]]
        local a = beat.heroTargets and beat.heroTargets[ids[i]] and heroTarget(ids[i], side) or MU.AnchorOf(ids[i])
        if not a then
            if beat.equipments and beat.equipments[ids[i]] then
                a = futureWeapon(ids[i], side)
            elseif side == "m" or side == "o" then
                a = futureSummon(ids[i], side) or MU.SideCenter(side)
            end
        end
        if a then
            -- Anchors are cached/shared by hit and impact renderers. Add per-result visual metadata
            -- only to a shallow copy so simultaneous mixed outcomes cannot contaminate one another.
            local visual = beat.targetVisuals and beat.targetVisuals[ids[i]] or beat.visual
            table.insert(ctx.targets, { id = a.id, kind = a.kind, side = a.side, slot = a.slot,
                missing = a.missing, x = a.x, y = a.y, w = a.w, h = a.h, visual = visual })
        end
    end
    ctx.targetVisuals = beat.targetVisuals
    return ctx
end

-- The default effect: one pulse of the family colour over each target (a burst over the middle of the field
-- for an area effect; one pulse per side for entities that are not on the board; the source when there is no
-- target).
function MU.Fx.DefaultEffect(beat, ctx)
    local dur = ctx.dur or AN.EFFECT
    local done = {}
    if ctx.area then
        MU.Pulse(ctx.board, ctx.color, dur)
    end
    for i = 1, table.getn(ctx.targets) do
        local a = ctx.targets[i]
        local key = a.missing and ("side" .. tostring(a.side)) or a.id
        if not done[key] then
            done[key] = true
            MU.Pulse(a, ctx.color, dur)
        end
    end
    if table.getn(ctx.targets) == 0 and ctx.src then
        MU.Pulse(ctx.src, ctx.color, dur)
    end
end
MU.Fx.PlayEffect = MU.Fx.PlayEffect or MU.Fx.DefaultEffect
MU.Fx.Stop = MU.Fx.Stop or function() end

-- ---- beats
local function revealAlpha(beat, t)
    if t < AN.REVEAL_IN then
        return t / AN.REVEAL_IN
    elseif t > beat.dur - AN.REVEAL_OUT then
        return math.max(0, (beat.dur - t) / AN.REVEAL_OUT)
    end
    return 1
end

local function startReveal(beat, rt)
    local rv = MU.reveal
    if not rv or not beat.cardId then
        beat.dur = 0.01
        return
    end
    H.CardWidget.SetCard(rv.widget, beat.cardId)
    local name = MA.CardName(beat.cardId) or ""
    local g = game()
    local who = g and g.oppName or ""
    if who == "" then
        who = "Your opponent"
    end
    if beat.secret then
        if beat.side == "m" then
            rv.caption:SetText("|cffcc66ffYour Secret|r  " .. name)
        else
            rv.caption:SetText("|cffcc66ff" .. who .. "'s Secret|r  " .. name)
        end
    else
        rv.caption:SetText("")      -- no "<opponent> plays <card>" line: the card itself says it
    end
    rv.frame:SetAlpha(0)
    rv.frame:Show()
    rt.reveal = true
end

local function startFatigue(beat, rt)
    local fv = MU.fatigue
    if not fv then
        beat.dur = 0.01
        return
    end
    local n = tonumber(beat.amount) or 1
    fv.body:SetText("Out of cards! Take " .. n .. " damage.")
    fv.frame:SetAlpha(0)
    fv.frame:Show()
    rt.fatigue = true
end

local function startAttack(beat, rt)
    local a = MU.AnchorOf(beat.source)
    local t = MU.AnchorOf(beat.target)
    if not a or not t or not a.slot.home or a.slot == t.slot then
        rt.skip = true
        beat.dur = 0.05              -- nothing to move: the hits still show
        return
    end
    local cx, cy, travel, ux, uy = MU.ContactPoint(a, t)
    rt.slot, rt.travel, rt.ux, rt.uy = a.slot, travel, ux, uy
    rt.contact = { x = cx, y = cy }
    touch(a.slot)
    raiseSlot(a.slot)
    -- a hero's arch cut-out and its secrets are anchored to the hero frame: they travel with it
end

local function attackImpact(beat, rt)
    rt.impacted = true
    MU.ShowHits(beat.hits, rt.slot)
    if beat.hero and rt.slot then
        -- the weapon flashes at the hero's hit; it is not a second attacker
        local w = MU.ui[rt.slot == MU.ui.m.hero and "m" or "o"].weapon
        if w.entity and w.frame:IsShown() and w.rect then
            MU.Flash({ x = w.rect.x + w.rect.w / 2, y = w.rect.y + w.rect.h / 2, w = w.rect.w, h = w.rect.h }, 1, 0.9,
                0.4, 0.5, MU.FLASH_TIME, 2)
        end
    end
end

local function startDeath(beat, rt)
    rt.slots = {}
    for i = 1, table.getn(beat.ids) do
        local a = MU.AnchorOf(beat.ids[i])
        if a and a.slot.home then
            touch(a.slot)
            table.insert(rt.slots, a.slot)
        end
    end
    if table.getn(rt.slots) == 0 then
        beat.dur = 0.02              -- not on the board (summoned and killed in one action): nothing to fade
    end
end

-- A weapon that breaks (the wbrk event, or a weapon gone from the next snapshot): the old weapon frame stays while
-- the beat runs, a crack flash and a tremble, then the frame shrinks, sinks a few pixels, drifts sideways and fades
-- (1.12 has no texture rotation: the drift stands in for a tilt). The frame stays invisible until the plan commits.
local function startWeaponBreak(beat, rt)
    local a = MU.AnchorOf(beat.id)
    if not a or a.kind ~= "weapon" or not a.slot.home then
        beat.dur = 0.02              -- not on the board (equipped and broken in one action): nothing to break
        return
    end
    rt.slot, rt.anchor = a.slot, a
    touch(a.slot)
    MU.Flash(a, 1, 0.95, 0.75, 0.8, 0.15, 3)
end

local function tickWeaponBreak(beat, rt, t)
    local slot = rt.slot
    if not slot then
        return
    end
    local p = t / beat.dur
    if p > 1 then
        p = 1
    end
    if p < MU.WBREAK_CRACK then
        slot.sx = 2 * math.sin(p * 90)
    else
        if not rt.cracked then
            rt.cracked = true
            MU.Flash(rt.anchor, 1, 1, 1, 0.9, 0.2, 7)
        end
        local q = (p - MU.WBREAK_CRACK) / (1 - MU.WBREAK_CRACK)
        slot.sx = MU.WBREAK_DRIFT * q
        slot.ay = MU.WBREAK_FALL * q * q
        slot.scale = 1 - MU.WBREAK_SHRINK * q
        slot.alpha = 1 - q
    end
    MU.PlaceSlot(slot)
end

function MU.BeatStart(beat)
    local rt = {}
    MU.beatRt = rt
    local k = beat.kind
    if k == "weapon_break" then
        startWeaponBreak(beat, rt)
    elseif k == "attack" then
        startAttack(beat, rt)
    elseif k == "reveal" then
        startReveal(beat, rt)
    elseif k == "fatigue" then
        startFatigue(beat, rt)
    elseif k == "impact" then
        if MU.ShowHits(beat.hits, nil) == 0 then
            beat.dur = 0.02
        end
    elseif k == "death" then
        startDeath(beat, rt)
    elseif k == "effect" then
        if beat.visual and MU.Fx and MU.Fx.TickEffect then
            beat.fxManual = true
        end
        local ctx = MU.EffectContext(beat)
        if table.getn(ctx.targets) == 0 and not ctx.src then
            beat.dur = 0.02
        else
            local ok = pcall(MU.Fx.PlayEffect, beat, ctx)
            if not ok then
                pcall(MU.Fx.DefaultEffect, beat, ctx)
            end
        end
    end
end

function MU.BeatTick(beat, t)
    local rt = MU.beatRt
    if not rt then
        return
    end
    local k = beat.kind
    if k == "weapon_break" then
        tickWeaponBreak(beat, rt, t)
    elseif k == "attack" then
        if rt.slot then
            local pos = AN.AttackPose(beat, t)
            local px = pos * rt.travel
            if pos < 0 then
                px = pos * MU.ANTICIPATE_PX
            end
            rt.slot.lx, rt.slot.ly = rt.ux * px, rt.uy * px
            MU.PlaceSlot(rt.slot)
        end
        if not rt.impacted and beat.impactAt and rt.slot and t >= beat.impactAt - 0.0001 then
            attackImpact(beat, rt)
        end
    elseif k == "reveal" then
        if rt.reveal then
            MU.reveal.frame:SetAlpha(revealAlpha(beat, t))
        end
    elseif k == "fatigue" then
        if rt.fatigue then
            local a = 1
            if t < AN.FATIGUE_IN then
                a = t / AN.FATIGUE_IN
            elseif t > beat.dur - AN.FATIGUE_OUT then
                a = math.max(0, (beat.dur - t) / AN.FATIGUE_OUT)
            end
            MU.fatigue.frame:SetAlpha(a)
        end
    elseif k == "death" then
        local p = t / beat.dur
        if p > 1 then
            p = 1
        end
        for i = 1, table.getn(rt.slots) do
            rt.slots[i].alpha = 1 - p
            rt.slots[i].ay = MU.DEATH_DRIFT * p
            MU.PlaceSlot(rt.slots[i])
        end
    elseif k == "effect" and beat.fxManual and MU.Fx and MU.Fx.TickEffect then
        pcall(MU.Fx.TickEffect, beat, t)
    end
end

function MU.BeatEnd(beat)
    if beat.kind == "effect" and beat.fxManual and MU.Fx and MU.Fx.EndEffect then
        pcall(MU.Fx.EndEffect, beat)
    end
    local rt = MU.beatRt
    MU.beatRt = nil
    if not rt then
        return
    end
    local k = beat.kind
    if k == "weapon_break" then
        if rt.slot then
            -- gone: it stays invisible until the plan commits (the next beats must not show it again)
            rt.slot.sx, rt.slot.ay, rt.slot.scale, rt.slot.alpha = MU.WBREAK_DRIFT, MU.WBREAK_FALL, 1 - MU.WBREAK_SHRINK, 0
            MU.PlaceSlot(rt.slot)
        end
    elseif k == "attack" then
        if not rt.impacted and not beat.fizzle then
            attackImpact(beat, rt)   -- a beat that could not move still shows what it hit
        end
        if rt.slot then
            -- back to the exact saved anchor, level and cut-out
            rt.slot.lx, rt.slot.ly = nil, nil
            restoreSlot(rt.slot)
            MU.touched[rt.slot] = nil
        end
    elseif k == "reveal" then
        if rt.reveal and MU.reveal then
            MU.reveal.frame:Hide()
            MU.reveal.frame:SetAlpha(1)
        end
    elseif k == "fatigue" then
        if rt.fatigue and MU.fatigue then
            MU.fatigue.frame:Hide()
            MU.fatigue.frame:SetAlpha(1)
        end
    end
end

-- ---- the clock's listener
-- All temporary visuals end: items, the reveal card, moved frames, the effects of part B.
function MU.AnimCleanup()
    MU.beatRt = nil
    MU.MullStop()
    if MU.pool then
        MU.ClearFx()
    end
    pcall(MU.Fx.Stop)
    MU.RestoreAll()
    if MU.reveal then
        MU.reveal.frame:Hide()
        MU.reveal.frame:SetAlpha(1)
    end
    if MU.fatigue then
        MU.fatigue.frame:Hide()
        MU.fatigue.frame:SetAlpha(1)
    end
end

-- Drops all animation work and shows the newest snapshot (a hidden window, a new match, the end of the match
-- view): the clock forgets its plans and every temporary visual ends.
function MU.AnimAbort()
    AN.Reset()
    MU.AnimCleanup()
end

-- Skips the running animations: the newest snapshot is shown now. Returns true when something ran.
function MU.AnimSkip()
    local was = AN.FastForward()
    MU.AnimCleanup()
    return was
end

-- ------------------------------------------------------------------ mulligan animation
-- Real time (MU.MullTick from MU.AnimTick), independent of the beat clock; the server and HsMatch.game never wait for
-- it. m.anim = { kind = "deal" | "swap", t, total, ... } owns the slots while it runs (refreshMulligan hands over to
-- MU.MullApply). Cards are placed by their middle and scale on the board (MU.MullPlace).
--   deal  the offered cards fly one after the other from the deck pile to their places (face down) and turn over.
--   swap  after Done: the replaced cards fly back into the pile, their new cards are dealt and turn over, the hand
--         holds for a moment, then all cards fly down to their places in the hand while the dim layer fades.
local function mullEaseOut(u)
    return 1 - (1 - u) * (1 - u)
end

local function mullEaseIn(u)
    return u * u
end

local function mullClamp(u)
    if u < 0 then
        return 0
    end
    if u > 1 then
        return 1
    end
    return u
end

-- The middle of the deck pile (board pixels): the right side, above the hand dock.
function MU.MullDeck()
    return MU.geom.bw - 90, MU.geom.bh - MU.HAND_STRIP - 70
end

-- Places the slot's card with its middle at (cx, cy) and the given scale.
function MU.MullPlace(slot, cx, cy, s)
    if s < 0.05 then
        s = 0.05
    end
    slot.frame:SetScale(s)
    slot.frame:ClearAllPoints()
    slot.frame:SetPoint("TOPLEFT", MU.mull.frame, "TOPLEFT", (cx - MU.MULL_W * s / 2) / s, -(cy - MU.MULL_H * s / 2) / s)
end

-- u: nil or >= 1 face up; <= 0 face down; between: turning. The back squashes to an edge (first half), the face opens
-- from the edge (second half), as the packs do.
function MU.MullLook(slot, u)
    local vl, vt, vr, vb = H.CardWidget.VisibleRect(MU.MULL_W)
    local showBack, f = false, 1
    if u ~= nil then
        if u <= 0 then
            showBack = true
        elseif u < 0.5 then
            showBack = true
            f = 1 - 2 * u
        elseif u < 1 then
            f = mullEaseOut((u - 0.5) * 2)
        end
    end
    if f < 0.03 then
        f = 0.03
    end
    if showBack then
        slot.widget.frame:Hide()
        local mid, arm = (vl + vr) / 2, (vr - vl) / 2 * f
        slot.back:ClearAllPoints()
        slot.back:SetPoint("TOPLEFT", slot.frame, "TOPLEFT", mid - arm, -vt)
        slot.back:SetPoint("BOTTOMRIGHT", slot.frame, "TOPLEFT", mid + arm, -vb)
        slot.back:Show()
    else
        slot.back:Hide()
        slot.widget.frame:SetWidth(MU.MULL_W * f)
        slot.widget.frame:Show()
    end
end

-- The card shown on a slot's face (set only when it changes).
local function mullFace(slot, cardId)
    if slot.animCard ~= cardId then
        slot.animCard = cardId
        H.CardWidget.SetCard(slot.widget, cardId)
    end
end

-- The point of a flight from a to b (middle and scale) at progress p, with a little lift in between.
local function mullFly(ax, ay, as, bx, by, bs, p)
    return ax + (bx - ax) * p, ay + (by - ay) * p - 36 * math.sin(3.14159 * p), as + (bs - as) * p
end

function MU.MullShowDeck(on)
    local m = MU.mull
    for i = 1, table.getn(m.deck) do
        if on then
            local dx, dy = MU.MullDeck()
            m.deck[i]:ClearAllPoints()
            m.deck[i]:SetPoint("CENTER", m.frame, "TOPLEFT", dx + (i - 2) * 2, -(dy + (i - 2) * 2))
            m.deck[i]:Show()
        else
            m.deck[i]:Hide()
        end
    end
end

local function mullChrome(m, alpha)
    local list = { m.banner, m.ribbon, m.title, m.hint, m.note, m.confirm }
    for i = 1, table.getn(list) do
        list[i]:SetAlpha(alpha)
    end
end

-- Everything the animation changed goes back to the plain view (the next refresh lays it out again).
function MU.MullStop()
    local m = MU.mull
    if not m or not m.anim then
        return
    end
    m.anim = nil
    mullChrome(m, 1)
    m.frame:SetBackdropColor(0, 0, 0, 0.78)
    MU.MullShowDeck(false)
    for i = 1, MU.MULL_SLOTS do
        local slot = m.slots[i]
        slot.animCard = nil
        slot.back:Hide()
        slot.widget.frame:SetWidth(MU.MULL_W)
        slot.widget.frame:Show()
        slot.frame:SetScale(1)
    end
end

function MU.MullSwapping()
    local m = MU.mull
    return m ~= nil and m.anim ~= nil and m.anim.kind == "swap"
end

function MU.MullBeginDeal(mid, n)
    local m = MU.mull
    m.dealMid = mid
    if AN.Mode() == "off" or AN.failed or not MU.geom then
        return
    end
    m.anim = { kind = "deal", t = 0, n = n,
        total = (n - 1) * MU.MULL_DEAL_STAGGER + MU.MULL_DEAL_FLY + MU.MULL_DEAL_FLIP + 0.1 }
    MU.MullShowDeck(true)
end

local function mullApplyDeal(m, a)
    local dx, dy = MU.MullDeck()
    local ds = MU.MULL_DECK_SCALE
    for i = 1, MU.MULL_SLOTS do
        local slot = m.slots[i]
        local lay = i <= a.n and m.layout and m.layout[i] or nil
        if not lay then
            slot.frame:Hide()
        else
            slot.mark:Hide()
            if slot.glow.color then
                slot.glow.color = nil
                MU.LayoutHandGlow(slot.glow)
            end
            local t = a.t - (i - 1) * MU.MULL_DEAL_STAGGER
            if t < 0 then
                slot.frame:Hide()         -- still in the pile
            else
                slot.frame:Show()
                local p = mullEaseOut(mullClamp(t / MU.MULL_DEAL_FLY))
                local x, y, s = mullFly(dx, dy, ds, lay.cx, lay.cy, lay.sc, p)
                MU.MullPlace(slot, x, y, s)
                MU.MullLook(slot, (t - MU.MULL_DEAL_FLY) / MU.MULL_DEAL_FLIP)
            end
        end
    end
end

-- Called when Done is clicked: what is needed to animate the answer (the offer is gone from the snapshot by then).
function MU.MullRememberOffer(g)
    local m = MU.mull
    if not m.layout then
        return
    end
    local slots, kept = {}, {}
    for i = 1, table.getn(g.mulligan) do
        local id = g.mulligan[i]
        local replaced = MA.marked[id] and true or false
        if not m.layout[i] then
            return
        end
        slots[i] = { id = id, cardId = m.slots[i].cardId, replaced = replaced, lay = m.layout[i] }
        if not replaced then
            table.insert(kept, id)
        end
    end
    m.swapPrep = { mid = g.mid, slots = slots, kept = kept }
end

-- The answer to Done arrived (the offer is gone): the swap animation, when the new hand is what it should be (the
-- kept cards first, in their order, then the new ones). Anything else just shows the new hand.
function MU.MullBeginSwap(g)
    local m = MU.mull
    local prep = m and m.swapPrep
    if not prep then
        return
    end
    if prep.mid ~= g.mid then
        m.swapPrep = nil
        return
    end
    if table.getn(g.mulligan) > 0 then
        return
    end
    m.swapPrep = nil
    if AN.Mode() == "off" or AN.failed or not MU.geom or not MU.frame or not MU.frame:IsShown() then
        return
    end
    if g.ph ~= "play" and g.ph ~= "mull" then
        return
    end
    local nKept = table.getn(prep.kept)
    local nRep = table.getn(prep.slots) - nKept
    for k = 1, nKept do
        if not g.hand[k] or g.hand[k].id ~= prep.kept[k] then
            return
        end
    end
    for k = 1, nRep do
        if not g.hand[nKept + k] then
            return
        end
    end
    local a = { kind = "swap", t = 0, n = table.getn(prep.slots), slots = {}, nRep = nRep, toHand = g.ph == "play" }
    local handPos = MU.HandLayout(table.getn(g.hand), MU.geom.bw, MU.geom.bh)
    local keptSeen, repSeen = 0, 0
    for i = 1, a.n do
        local ps = prep.slots[i]
        local s = { replaced = ps.replaced, cardId = ps.cardId, lay = ps.lay }
        local hi
        if ps.replaced then
            s.k = repSeen
            repSeen = repSeen + 1
            hi = nKept + repSeen
            s.newCardId = g.hand[hi].cardId
        else
            keptSeen = keptSeen + 1
            hi = keptSeen
        end
        local hp = handPos[hi]
        if hp then
            s.hand = { cx = hp.x + MU.HAND_W / 2, cy = hp.y + MU.HAND_H / 2, sc = MU.HAND_W / MU.MULL_W }
        end
        a.slots[i] = s
    end
    a.in0 = 0
    a.inEnd = 0
    local hold = 0.3
    if nRep > 0 then
        a.in0 = MU.MULL_CHROME_FADE + (nRep - 1) * MU.MULL_OUT_STAGGER + MU.MULL_OUT_FLY + MU.MULL_GAP_BEFORE_IN
        a.inEnd = a.in0 + (nRep - 1) * MU.MULL_DEAL_STAGGER + MU.MULL_DEAL_FLY + MU.MULL_DEAL_FLIP
        hold = MU.MULL_HOLD
    end
    a.handAt = a.inEnd + hold
    a.total = a.handAt
    if a.toHand then
        a.total = a.handAt + MU.MULL_TO_HAND
    end
    for i = 1, MU.MULL_SLOTS do
        m.slots[i].animCard = nil
        local s = a.slots[i]
        if s then
            mullFace(m.slots[i], s.cardId)
        end
    end
    m.anim = a
    m.note:Hide()
    m.confirm:Hide()
    m.frame:Show()
    MU.MullShowDeck(true)
    MU.HidePreview()
    MU.MullApply()
end

local function mullApplySwap(m, a)
    local dx, dy = MU.MullDeck()
    local ds = MU.MULL_DECK_SCALE
    local t = a.t
    -- the banner and the hint go first, the dim layer fades while the cards fly into the hand
    mullChrome(m, 1 - mullClamp(t / MU.MULL_CHROME_FADE))
    local dim = 0.78
    if a.toHand and t > a.handAt then
        dim = 0.78 * (1 - mullClamp((t - a.handAt) / (MU.MULL_TO_HAND * 0.7)))
    end
    m.frame:SetBackdropColor(0, 0, 0, dim)
    m.note:Hide()
    m.confirm:Hide()
    for i = 1, MU.MULL_SLOTS do
        local slot = m.slots[i]
        local s = a.slots[i]
        if not s then
            slot.frame:Hide()
        else
            slot.glow.color = nil
            MU.LayoutHandGlow(slot.glow)
            local lay = s.lay
            local x, y, sc = lay.cx, lay.cy, lay.sc
            local look = nil
            local cardId = s.cardId
            local visible = true
            local marked = false
            if s.replaced then
                local tOut = t - MU.MULL_CHROME_FADE - s.k * MU.MULL_OUT_STAGGER     -- the X shows while the banner fades
                local tIn = t - a.in0 - s.k * MU.MULL_DEAL_STAGGER
                if tIn >= 0 then
                    cardId = s.newCardId
                    local p = mullEaseOut(mullClamp(tIn / MU.MULL_DEAL_FLY))
                    x, y, sc = mullFly(dx, dy, ds, lay.cx, lay.cy, lay.sc, p)
                    look = (tIn - MU.MULL_DEAL_FLY) / MU.MULL_DEAL_FLIP
                    if look >= 1 then
                        look = nil
                    end
                elseif tOut >= MU.MULL_OUT_FLY then
                    visible = false           -- back in the pile
                elseif tOut >= 0 then
                    local p = mullEaseIn(tOut / MU.MULL_OUT_FLY)
                    x, y, sc = mullFly(lay.cx, lay.cy, lay.sc, dx, dy, ds, p)
                else
                    marked = true             -- still waiting, with its red X
                end
            end
            if visible and a.toHand and s.hand and t > a.handAt then
                local p = mullEaseIn(mullClamp((t - a.handAt) / MU.MULL_TO_HAND))
                x, y, sc = mullFly(lay.cx, lay.cy, lay.sc, s.hand.cx, s.hand.cy, s.hand.sc, p)
            end
            if visible then
                mullFace(slot, cardId)
                slot.frame:Show()
                MU.MullPlace(slot, x, y, sc)
                MU.MullLook(slot, look)
                if marked then
                    slot.mark:Show()
                else
                    slot.mark:Hide()
                end
            else
                slot.frame:Hide()
            end
        end
    end
end

function MU.MullApply()
    local m = MU.mull
    local a = m and m.anim
    if not a then
        return
    end
    if a.kind == "deal" then
        mullApplyDeal(m, a)
    else
        mullApplySwap(m, a)
    end
end

-- The animation ran its time or was skipped: the plain view of the newest snapshot.
function MU.MullFinish()
    local m = MU.mull
    if not m or not m.anim then
        return
    end
    MU.MullStop()
    if MU.frame and MU.frame:IsShown() then
        MU.Refresh()
    end
end

function MU.MullSkip()
    local m = MU.mull
    if m and m.anim then
        MU.MullFinish()
    end
end

function MU.MullTick(dt)
    local m = MU.mull
    local a = m and m.anim
    if not a then
        return
    end
    if not MU.frame or not MU.frame:IsShown() then
        MU.MullStop()
        return
    end
    a.t = a.t + dt * AN.Speed()
    if a.t >= a.total then
        MU.MullFinish()
    else
        MU.MullApply()
    end
end

MU.AnimListener = {
    OnPlanStart = function(plan)
        MU.HidePreview()
        MU.sel = nil
        MU.choose = nil
    end,
    OnBeatStart = function(beat, plan)
        MU.BeatStart(beat)
    end,
    OnBeatTick = function(beat, t, plan)
        MU.BeatTick(beat, t)
    end,
    OnBeatEnd = function(beat, plan)
        MU.BeatEnd(beat)
    end,
    -- the plan ended: the new snapshot is drawn, survivors recenter
    OnPlanEnd = function(plan)
        finishShakes()
        MU.RestoreAll()
        if MU.frame and MU.frame:IsShown() then
            MU.Refresh()
        end
    end,
    -- all work was dropped: the newest snapshot is drawn
    OnSnap = function(reason)
        MU.AnimCleanup()
        if reason == "error" and H.PrintError then
            H.PrintError("Match animation error: " .. tostring(AN.failed) .. " (animations stay off until /reload).")
        end
        if MU.frame and MU.frame:IsShown() then
            MU.Refresh()
        end
    end,
}
AN.SetListener(MU.AnimListener)

-- Drives the beats and the timed items; called from Hearthstone.OnUpdate (independent of the network). An
-- error only turns the animations off (the match view must never depend on them).
local function animTick(dt)
    if AN.Busy() then
        if MU.frame and not MU.frame:IsShown() then
            MU.AnimAbort()
        else
            AN.Tick(dt)
        end
    end
    if table.getn(MU.fxItems) > 0 then
        if dt > AN.MAX_DT then
            dt = AN.MAX_DT
        end
        MU.FxTick(dt * AN.Speed())
    end
end

function MU.AnimTick(dt)
    if not MU.pool or type(dt) ~= "number" then
        return
    end
    if MU.bannerRt then
        MU.BannerTick(dt > AN.MAX_DT and AN.MAX_DT or dt)   -- real time, independent of /hs anim fast
    end
    MU.PowerFlipTick(dt > AN.MAX_DT and AN.MAX_DT or dt)
    MU.MullTick(dt > AN.MAX_DT and AN.MAX_DT or dt)
    if MU.MullSwapping() then
        return      -- the beats wait behind the dim layer until the new hand is in place
    end
    if not AN.Busy() and table.getn(MU.fxItems) == 0 then
        return
    end
    local ok, err = pcall(animTick, dt)
    if not ok then
        AN.failed = AN.failed or tostring(err)
        pcall(MU.AnimAbort)
        if H.PrintError then
            H.PrintError("Match animation error: " .. tostring(err) .. " (animations stay off until /reload).")
        end
        if MU.frame and MU.frame:IsShown() then
            MU.Refresh()
        end
    end
end

-- A new change of the match (a transition from HsMatch.TakeTransition()). Nobody looks when the window is
-- hidden: the newest snapshot is shown when it opens.
function MU.AnimEnqueue(tr)
    if not MU.pool or not MU.frame or not MU.frame:IsShown() then
        AN.Reset()
        MU.AnimCleanup()
        return nil
    end
    return AN.Enqueue(tr)
end

-- /hs anim [on|off|fast|skip]
function MU.AnimCommand(arg)
    arg = string.lower(arg or "")
    if arg == "on" or arg == "off" or arg == "fast" then
        AN.SetMode(arg)
        if H.db then
            H.db.anim = arg
        end
        if arg == "off" then
            MU.AnimSkip()
        else
            AN.ClearFailure()
        end
        local what = { on = "on", off = "off (the board always jumps to the newest state)", fast = "fast (3x speed)" }
        return "match animations: " .. what[arg]
    elseif arg == "skip" then
        if MU.AnimSkip() then
            return "animations skipped"
        end
        return "no animation is running"
    elseif arg == "" then
        return "match animations: " .. AN.Mode() .. "  (/hs anim on | off | fast | skip)"
    end
    return "usage: /hs anim [on|off|fast|skip]"
end

-- ------------------------------------------------------------------ build
local function makeMinion(parent, key, i)
    local name = "HearthstoneMatchMinion" .. string.upper(key) .. i
    local b = CreateFrame("Button", name, parent)
    b:SetWidth(MU.MINION_W)
    b:SetHeight(MU.MINION_H)
    b:RegisterForClicks("LeftButtonUp", "RightButtonUp")
    local slot = { frame = b }
    local bg = b:CreateTexture(nil, "BACKGROUND")
    bg:SetAllPoints(b)
    if MU.SetPiece(bg, "minion_oval") then
        slot.native = false
    else
        slot.native = true
        bg:Hide()
        plate(b, 0.9)
    end
    local art = b:CreateTexture(nil, "ARTWORK")
    art:SetWidth(58)
    art:SetHeight(56)
    art:SetPoint("TOP", b, "TOP", 0, -10)
    slot.art = art
    slot.tint = makeTint(b, art)
    local label = newText(b, "GameFontNormalSmall", "CENTER")
    label:SetPoint("CENTER", art, "CENTER", 0, 0)
    label:SetWidth(54)
    label:Hide()
    slot.label = label
    slot.marks = makeMarks(b, 12)
    slot.rim = makeRim(b, 2)
    local tags = outlineText(b, 9, "CENTER")
    tags:SetPoint("BOTTOM", b, "BOTTOM", 0, 22)
    tags:SetWidth(MU.MINION_W - 8)
    tags:SetHeight(40)
    tags:SetJustifyV("BOTTOM")
    slot.tags = tags
    slot.atk = makeGem(b, "attack", MU.MINION_GEM)
    slot.atk.frame:SetPoint("CENTER", b, "BOTTOMLEFT", 8, 9)
    slot.hp = makeGem(b, "health", MU.MINION_GEM)
    slot.hp.frame:SetPoint("CENTER", b, "BOTTOMRIGHT", -8, 9)
    -- two child frames for the overlays, so their order is by frame level, not by texture creation order:
    -- shieldLayer (+2) holds the Divine Shield, topLayer (+4) the ice, the silence swirl and the keyword icons; the gems (+6) stay on top
    local shieldLayer = CreateFrame("Frame", nil, b)
    shieldLayer:SetAllPoints(b)
    shieldLayer:SetFrameLevel(b:GetFrameLevel() + 2)
    local topLayer = CreateFrame("Frame", nil, b)
    topLayer:SetAllPoints(b)
    topLayer:SetFrameLevel(b:GetFrameLevel() + 4)
    -- the steel frame of a Taunt minion: over the portrait, under the Divine Shield and the gems
    if MU.Piece("taunt_frame") then
        slot.taunt = b:CreateTexture(nil, "OVERLAY")
        MU.SetPiece(slot.taunt, "taunt_frame")
        MU.LayoutTaunt(slot)
        slot.taunt:Hide()
    end
    -- the Divine Shield: the golden bubble of the owner's art over the portrait and the taunt frame
    if MU.Piece("divine_shield") then
        slot.shield = shieldLayer:CreateTexture(nil, "OVERLAY")
        MU.SetPiece(slot.shield, "divine_shield")
        MU.LayoutShield(slot)
        slot.shield:Hide()
    end
    -- frozen: the ice ring of the owner's art over everything on the portrait (replaces the blue rim)
    if MU.Piece("frozen_ice") then
        slot.ice = topLayer:CreateTexture(nil, "OVERLAY")
        MU.SetPiece(slot.ice, "frozen_ice")
        MU.LayoutIce(slot)
        slot.ice:Hide()
    end
    -- the pink swirl of a silenced minion: over the portrait (and the frames), under the gems and the icons
    if MU.Piece("silence_swirl") then
        slot.silence = topLayer:CreateTexture(nil, "OVERLAY")
        MU.SetPiece(slot.silence, "silence_swirl")
        slot.silence:SetAlpha(MU.SILENCE_ALPHA)
        MU.FitOverOval(slot, slot.silence, "silence_swirl", 0)
        slot.silence:Hide()
    end
    -- the keyword icons (lightning for a continuous effect, skull for a deathrattle), one texture per kind
    slot.mechs = {}
    for j = 1, table.getn(MU.MECHANIC_ORDER) do
        local t = topLayer:CreateTexture(nil, "OVERLAY")
        t:Hide()
        slot.mechs[MU.MECHANIC_ORDER[j]] = t
    end
    slot.glow = makeGlow(b, 6, "oval", 2)
    local k, idx = key, i
    b:SetScript("OnClick", function()
        MU.OnMinionClick(k, idx, arg1)
    end)
    b:SetScript("OnEnter", function()
        MU.OnMinionEnter(k, idx)
    end)
    b:SetScript("OnLeave", function()
        MU.HidePreview()
    end)
    b:Hide()
    return slot
end

local function makeHero(parent, key)
    local b = CreateFrame("Button", "HearthstoneMatchHero" .. string.upper(key), parent)
    b:SetWidth(MU.HERO_MIN)
    b:SetHeight(MU.HERO_MIN)
    b:RegisterForClicks("LeftButtonUp", "RightButtonUp")
    local h = { frame = b }
    -- layers: the portrait (BORDER), the arch cut-out over it (ARTWORK), the name (OVERLAY); gems and glow are
    -- child frames above all of them
    local art = b:CreateTexture(nil, "BORDER")
    art:SetPoint("TOPLEFT", b, "TOPLEFT", 7, -7)
    art:SetPoint("BOTTOMRIGHT", b, "BOTTOMRIGHT", -7, 7)
    h.art = art
    h.dome = {}
    for i = 1, MU.DOME_SLICES do
        h.dome[i] = b:CreateTexture(nil, "BORDER")
        h.dome[i]:Hide()
    end
    h.overlay = b:CreateTexture(nil, "ARTWORK")
    h.overlay:Hide()
    h.tint = makeTint(b, b)
    h.marks = makeMarks(b, 14)
    h.rim = makeRim(b, 2)
    if MU.Piece("frozen_ice") then
        h.ice = b:CreateTexture(nil, "OVERLAY")
        MU.SetPiece(h.ice, "frozen_ice")
        h.ice:Hide()
    end
    if MU.Piece("board_field") then
        -- the field's arch is the frame: the portrait fills it (MU.Place, MU.LayoutArch)
    elseif MU.Piece("hero_ring") then
        local ring = b:CreateTexture(nil, "OVERLAY")
        ring:SetPoint("TOPLEFT", b, "TOPLEFT", -6, 6)
        ring:SetPoint("BOTTOMRIGHT", b, "BOTTOMRIGHT", 6, -6)
        MU.SetPiece(ring, "hero_ring")
        h.ring = ring
    else
        b:SetBackdrop({ edgeFile = TOOLTIP_EDGE, edgeSize = 14, insets = { left = 4, right = 4, top = 4, bottom = 4 } })
        b:SetBackdropBorderColor(0.75, 0.6, 0.3, 1)
    end
    h.atk = makeGem(b, "attack", MU.WEAPON_GEM)      -- as big as a weapon's
    h.atk.frame:SetPoint("CENTER", b, "BOTTOMLEFT", 3, 5)
    h.hp = makeGem(b, "health", MU.HERO_HP_GEM, MU.HERO_HP_FONT)
    h.hp.frame:SetPoint("CENTER", b, "BOTTOMRIGHT", -2, 8)
    h.hp.text:ClearAllPoints()
    h.hp.text:SetPoint("CENTER", h.hp.frame, "CENTER", 0, -1)     -- the number sits a pixel lower in the gem
    h.armor = makeGem(b, "armor", MU.HERO_ARMOR_GEM, MU.HERO_ARMOR_FONT)
    h.armor.frame:SetWidth(MU.HERO_ARMOR_W)
    h.armor.frame:SetFrameLevel(h.hp.frame:GetFrameLevel() + 2)     -- drawn over the health gem
    h.armor.frame:SetPoint("CENTER", b, "BOTTOMRIGHT", -2, 43)  -- on the arch's right edge, just above the health gem (overlapping it a little)
    h.armor.text:ClearAllPoints()
    h.armor.text:SetPoint("CENTER", h.armor.frame, "CENTER", 0, -1)  -- the number sits in the shield's middle
    -- an arch-shaped glow around the field's arch, a round one around the ring of the fallback look
    if MU.Piece("board_field") then
        h.glow = makeGlow(b, 8, "arch", 2)
    else
        h.glow = makeGlow(b, 8, "round", MU.HERO_RING_PAD)
    end
    local k = key
    b:SetScript("OnClick", function()
        MU.OnHeroClick(k, arg1)
    end)
    b:SetScript("OnEnter", function()
        MU.OnHeroEnter(k)
    end)
    b:SetScript("OnLeave", function()
        MU.HidePreview()
    end)
    return h
end

local function makePower(parent, key)
    local b = CreateFrame("Button", "HearthstoneMatchPower" .. string.upper(key), parent)
    b:SetWidth(MU.POWER)
    b:SetHeight(MU.POWER)
    b:RegisterForClicks("LeftButtonUp", "RightButtonUp")
    local p = { frame = b }
    local art = b:CreateTexture(nil, "ARTWORK")
    art:SetPoint("TOPLEFT", b, "TOPLEFT", 6, -6)
    art:SetPoint("BOTTOMRIGHT", b, "BOTTOMRIGHT", -6, 6)
    p.art = art
    if MU.Piece("hero_power_ring") then
        local ring = b:CreateTexture(nil, "OVERLAY")
        ring:SetPoint("TOPLEFT", b, "TOPLEFT", -4, 4)
        ring:SetPoint("BOTTOMRIGHT", b, "BOTTOMRIGHT", 4, -4)
        MU.SetPiece(ring, "hero_power_ring")
        p.ring = ring
    else
        b:SetBackdrop({ edgeFile = TOOLTIP_EDGE, edgeSize = 14, insets = { left = 4, right = 4, top = 4, bottom = 4 } })
        b:SetBackdropBorderColor(0.75, 0.6, 0.3, 1)
    end
    local label = newText(b, "GameFontNormalSmall", "CENTER")
    label:SetPoint("CENTER", b, "CENTER", 0, 0)
    label:SetWidth(MU.POWER - 12)
    label:Hide()
    p.label = label
    -- a used power is turned over: the stone medallion of the owner's art covers the whole ring (no "Used" text, no cost)
    if MU.Piece("hero_power_used") and p.ring then
        local back = b:CreateTexture(nil, "OVERLAY")
        back:SetPoint("TOPLEFT", b, "TOPLEFT", -MU.POWER_RING_PAD, MU.POWER_RING_PAD)
        back:SetPoint("BOTTOMRIGHT", b, "BOTTOMRIGHT", MU.POWER_RING_PAD, -MU.POWER_RING_PAD)
        MU.SetPiece(back, "hero_power_used")
        back:Hide()
        p.back = back
    end
    p.cost = makeGem(b, "mana", MU.POWER_COST_GEM)
    p.cost.frame:SetPoint("CENTER", b, "TOP", 0, -2)
    p.glow = makeGlow(b, 6, "round", MU.POWER_RING_PAD)
    local k = key
    b:SetScript("OnClick", function()
        MU.OnPowerClick(k, arg1)
    end)
    b:SetScript("OnEnter", function()
        MU.OnPowerEnter(k)
    end)
    b:SetScript("OnLeave", function()
        MU.HidePreview()
    end)
    return p
end

local function makeWeapon(parent, key)
    local b = CreateFrame("Button", "HearthstoneMatchWeapon" .. string.upper(key), parent)
    b:SetWidth(MU.WEAPON_W)
    b:SetHeight(MU.WEAPON_H)
    local w = { frame = b }
    local bg = b:CreateTexture(nil, "BACKGROUND")
    bg:SetAllPoints(b)
    local art = b:CreateTexture(nil, "ARTWORK")
    art:SetPoint("TOPLEFT", b, "TOPLEFT", 9, -9)
    art:SetPoint("BOTTOMRIGHT", b, "BOTTOMRIGHT", -9, 9)
    w.art = art
    if MU.Piece("weapon_ring") then
        -- a round weapon: the portrait inside a ring, no plate behind it
        bg:Hide()
        local ring = b:CreateTexture(nil, "OVERLAY")
        ring:SetPoint("TOPLEFT", b, "TOPLEFT", -MU.WEAPON_RING_PAD, MU.WEAPON_RING_PAD)
        ring:SetPoint("BOTTOMRIGHT", b, "BOTTOMRIGHT", MU.WEAPON_RING_PAD, -MU.WEAPON_RING_PAD)
        MU.SetPiece(ring, "weapon_ring")
        w.ring = ring
    elseif not MU.SetPiece(bg, "weapon_frame") then
        bg:Hide()
        plate(b, 0.9)
    end
    local label = newText(b, "GameFontNormalSmall", "CENTER")
    label:SetPoint("CENTER", b, "CENTER", 0, 0)
    label:SetWidth(MU.WEAPON_W - 14)
    label:Hide()
    w.label = label
    w.atk = makeGem(b, "attack", MU.WEAPON_GEM)
    w.atk.frame:SetPoint("CENTER", b, "BOTTOMLEFT", 2, 4)
    w.dur = makeGem(b, "health", MU.WEAPON_GEM)
    w.dur.frame:SetPoint("CENTER", b, "BOTTOMRIGHT", -2, 4)
    b:SetScript("OnEnter", function()
        local g = game()
        local s = g and (key == "m" and g.me or g.opp)
        local extra = nil
        if s and s.weapon then
            extra = { "Now: Attack " .. s.weapon.attack .. "   Durability " .. s.weapon.durability }
        end
        MU.OnEnter(w, extra)
    end)
    b:SetScript("OnLeave", function()
        MU.HidePreview()
    end)
    b:Hide()
    return w
end

local function makeHandSlot(parent, i)
    local b = CreateFrame("Button", "HearthstoneMatchHand" .. i, parent)
    b:SetWidth(MU.HAND_W)
    b:SetHeight(MU.HAND_H)
    b:RegisterForClicks("LeftButtonUp", "RightButtonUp")
    -- the card's band of frame levels (MU.HAND_LEVEL): the highlight at its bottom, the slot frame one above; the
    -- children made below (body, card, text, gems) get the levels after that
    local band = parent:GetFrameLevel() + MU.HAND_LEVEL + MU.HAND_BAND * (i - 1)
    b:SetFrameLevel(band + 1)
    local slot = { frame = b }
    -- The slot frame is only the mouse area and keeps the size and place of the resting card, also while the card is
    -- hovered. Everything visible lives in the body, which is what grows on hover (MU.HandPlace), so the cursor can
    -- still go straight onto the neighbouring cards.
    local body = CreateFrame("Frame", nil, b)
    body:SetWidth(MU.HAND_W)
    body:SetHeight(MU.HAND_H)
    body:SetPoint("TOPLEFT", b, "TOPLEFT", 0, 0)
    slot.body = body
    slot.widget = H.CardWidget.Create(body, nil, MU.HAND_W, false)
    slot.widget.frame:SetPoint("CENTER", body, "CENTER", 0, 0)
    slot.cost = makeGem(body, "mana", 24)
    slot.cost.frame:SetPoint("CENTER", body, "TOPLEFT", MU.HAND_COST_AT[1] * MU.HAND_W, -MU.HAND_COST_AT[2] * MU.HAND_H)
    slot.atk = makeGem(body, "attack", 22)
    slot.atk.frame:SetPoint("CENTER", body, "BOTTOMLEFT", 6, 6)
    slot.hp = makeGem(body, "health", 22)
    slot.hp.frame:SetPoint("CENTER", body, "BOTTOMRIGHT", -6, 6)
    slot.glow = MU.MakeHandGlow(body, band)
    local idx = i
    b:SetScript("OnClick", function()
        MU.OnHandClick(idx, arg1)
    end)
    b:SetScript("OnEnter", function()
        MU.OnHandEnter(idx)
    end)
    b:SetScript("OnLeave", function()
        MU.HandUnpop(idx)
        MU.SetManaPreview(nil)
    end)
    b:Hide()
    return slot
end

local function makeBack(parent)
    local b = CreateFrame("Frame", nil, parent)
    b:SetWidth(MU.BACK_W)
    b:SetHeight(MU.BACK_H)
    local tex = b:CreateTexture(nil, "ARTWORK")
    tex:SetAllPoints(b)
    if not MU.SetPiece(tex, "card_back") then
        tex:Hide()
        b:SetBackdrop({
            bgFile = DIALOG_BG, edgeFile = TOOLTIP_EDGE, tile = true, tileSize = 32, edgeSize = 12,
            insets = { left = 3, right = 3, top = 3, bottom = 3 },
        })
        b:SetBackdropBorderColor(0.75, 0.6, 0.3, 1)
    end
    b:Hide()
    return b
end

-- A secret icon: a small plate with the card art (own secret) or a "?" (the opponent's).
local function makeSecret(parent, key, i)
    local b = CreateFrame("Button", "HearthstoneMatchSecret" .. string.upper(key) .. i, parent)
    b:SetWidth(MU.SECRET_SIZE)
    b:SetHeight(MU.SECRET_SIZE)
    b:SetFrameLevel(parent:GetFrameLevel() + 8)   -- above the hero's arch cut-out and gems
    plate(b, 0.95)
    local slot = { frame = b }
    local art = b:CreateTexture(nil, "ARTWORK")
    art:SetPoint("TOPLEFT", b, "TOPLEFT", 3, -3)
    art:SetPoint("BOTTOMRIGHT", b, "BOTTOMRIGHT", -3, 3)
    slot.art = art
    -- the class medallion of the secret_* pieces (when installed), else the plate with art / "?" above
    slot.icon = b:CreateTexture(nil, "OVERLAY")
    slot.icon:SetAllPoints(b)
    slot.icon:Hide()
    slot.label = outlineText(b, 13, "CENTER")
    slot.label:SetPoint("CENTER", b, "CENTER", 0, 0)
    local k, idx = key, i
    b:SetScript("OnEnter", function()
        MU.OnSecretEnter(k, idx)
    end)
    b:SetScript("OnLeave", function()
        hideTip()
    end)
    b:Hide()
    return slot
end

-- One side of the board: hero, hero power, weapon, mana, counts, name and the minion slots.
local function makeSide(parent, key)
    local u = { minions = {}, crystals = {}, backs = {} }
    u.hero = makeHero(parent, key)
    u.power = makePower(parent, key)
    u.weapon = makeWeapon(parent, key)
    u.name = newText(u.hero.frame, "GameFontNormal", "CENTER") -- above the arch cut-out
    u.counts = newText(parent, "GameFontHighlightSmall", "RIGHT")
    u.counts:SetWidth(110)
    u.manaText = newText(parent, "GameFontNormalSmall", "LEFT")
    u.overload = outlineText(parent, 11, "LEFT")
    -- the owner's mana art: the pill with the numbers, and the glows of the crystals a hovered card would spend
    u.pill = CreateFrame("Frame", nil, parent)
    u.pill:SetWidth(MU.PILL_W)
    u.pill:SetHeight(MU.PILL_H)
    u.pillTex = u.pill:CreateTexture(nil, "BACKGROUND")
    u.pillTex:SetAllPoints(u.pill)
    u.pillText = outlineText(u.pill, 13, "CENTER")
    u.pillText:SetPoint("CENTER", u.pill, "CENTER", 0, 1)
    u.pill:Hide()
    u.glows = {}
    for i = 1, MU.MAX_CRYSTALS do
        u.glows[i] = parent:CreateTexture(nil, "OVERLAY")
        u.glows[i]:SetBlendMode("ADD")
        u.glows[i]:Hide()
    end
    u.secrets = {}
    -- the secrets sit on the hero's dome and are children of the hero frame: they move with its lunge and shake
    u.hero.followers = {}
    for i = 1, MU.MAX_SECRETS do
        u.secrets[i] = makeSecret(u.hero.frame, key, i)
        table.insert(u.hero.followers, u.secrets[i].frame)
    end
    for i = 1, MU.MAX_CRYSTALS do
        u.crystals[i] = parent:CreateTexture(nil, "ARTWORK")
        u.crystals[i]:SetWidth(MU.CRYSTAL_W)
        u.crystals[i]:SetHeight(MU.CRYSTAL_H)
        u.crystals[i]:Hide()
    end
    for i = 1, MU.MAX_BOARD do
        u.minions[i] = makeMinion(parent, key, i)
    end
    return u
end

-- True when the owner's mana art (pill, full and empty crystal) is installed.
function MU.ManaArt()
    return (MU.Piece("mana_pill") and MU.Piece("mana_gem_full") and MU.Piece("mana_gem_empty")) and true or false
end

-- Places the mana of side key beside its hero power: the pill, then (mine only) the crystals; the crystals shrink when the
-- board is too narrow for ten of them. Returns false without the art (then the old row is placed).
function MU.LayoutMana(key)
    local u = MU.ui[key]
    if not MU.ManaArt() or not u.pill then
        return false
    end
    local full = MU.Piece("mana_gem_full")
    local aspect = full.w / full.h * MU.MANA_GEM_WIDTH
    local x0 = u.power.rect.x + u.power.rect.w + MU.POWER_RING_PAD + MU.MANA_X
    local cy = u.power.rect.y + u.power.rect.h / 2
    if key == "m" then
        -- mine: down below the field, clear of the hero power, and further right (pill and crystals move together)
        x0 = x0 + MU.MANA_MINE_SHIFT
        cy = cy + MU.MANA_MINE_DROP
        local limit = MU.geom.bh - MU.PILL_H / 2 - 4
        if cy > limit then
            cy = limit
        end
    end
    local avail = MU.geom.bw - MU.MANA_RIGHT - x0 - MU.PILL_W - MU.MANA_PILL_GAP
    local cs = MU.MANA_GEM_H
    local fit = math.floor((avail - (MU.MAX_CRYSTALS - 1) * MU.MANA_GEM_SPACE) / (MU.MAX_CRYSTALS * aspect))
    if fit < cs then
        cs = fit
    end
    if cs < 14 then
        cs = 14
    end
    u.manaGemH = cs
    u.pill:SetWidth(MU.PILL_W)
    u.pill:SetHeight(MU.PILL_H)
    u.pill:ClearAllPoints()
    if key == "o" then
        -- the opponent's: just above his hero power's ring, starting right of the cost gem on its top (his card backs
        -- stay visible beside it)
        local pr = u.power.rect
        u.pill:SetPoint("BOTTOMLEFT", MU.board, "TOPLEFT", pr.x + pr.w / 2 + MU.POWER_COST_GEM / 2 + MU.MANA_OPP_DX,
            -(pr.y - MU.POWER_RING_PAD - MU.MANA_OPP_GAP))
    else
        u.pill:SetPoint("LEFT", MU.board, "TOPLEFT", x0, -cy)
    end
    MU.SetPiece(u.pillTex, "mana_pill")
    local cell = cs * aspect + MU.MANA_GEM_SPACE
    for i = 1, MU.MAX_CRYSTALS do
        local t = u.crystals[i]
        t:ClearAllPoints()
        t:SetPoint("LEFT", MU.board, "TOPLEFT", x0 + MU.PILL_W + MU.MANA_PILL_GAP + (i - 1) * cell, -cy)
        u.glows[i]:ClearAllPoints()
        u.glows[i]:SetPoint("CENTER", t, "CENTER", 0, 0)
    end
    u.manaText:ClearAllPoints()
    u.manaText:SetPoint("LEFT", u.pill, "LEFT", 0, 0)
    u.overload:ClearAllPoints()
    if key == "o" then
        u.overload:SetPoint("LEFT", u.pill, "RIGHT", 6, 0)
    else
        u.overload:SetPoint("TOPLEFT", u.pill, "BOTTOMLEFT", 0, -3)
    end
    return true
end

-- Positions everything that does not move with the card counts (hero rows, mid strip, log).
function MU.Place()
    local ui = MU.ui
    if not ui then
        return
    end
    local bw, fh = MU.geom.bw, MU.geom.fh
    for _, key in ipairs({ "o", "m" }) do
        local u = ui[key]
        local al, at, aw, ah, dome = MU.HeroArch(key, bw, fh)
        u.hero.frame:ClearAllPoints()
        if al then
            u.hero.frame:SetWidth(aw)
            u.hero.frame:SetHeight(ah)
            u.hero.frame:SetPoint("TOPLEFT", MU.board, "TOPLEFT", al, -at)
            u.hero.home = { point = "TOPLEFT", x = al, y = -at }
            u.hero.rect = { x = al, y = at, w = aw, h = ah }
            MU.LayoutArch(u.hero, aw, ah, dome, key)
            MU.PlaceArchOverlay(key, u.hero, bw, fh)
            u.hero.arch = true
            MU.LayoutGlow(u.hero.glow, aw, ah, MU.HeroFramePads(key, aw, ah))
        else
            local cx, cy, size = MU.HeroRect(key, bw, fh)
            u.hero.frame:SetWidth(size)
            u.hero.frame:SetHeight(size)
            u.hero.frame:SetPoint("CENTER", MU.board, "TOPLEFT", cx, -cy)
            u.hero.home = { point = "CENTER", x = cx, y = -cy }
            u.hero.rect = { x = cx - size / 2, y = cy - size / 2, w = size, h = size }
            u.hero.overlay:Hide()
            u.hero.arch = false
            u.hero.domePx = nil
            MU.LayoutGlow(u.hero.glow, size, size)
        end
        MU.LayoutHeroIce(u.hero)
        -- the hero power sits right of the hero's outer frame (arch frame / ring), as big as the hero ring; the
        -- weapon left of it (anchored below): their rectangles for the effects
        local hr = u.hero.rect
        local sideL, sideR = MU.SideBounds(key, bw, fh)
        local ps = MU.PowerSize(bw, fh)
        local cy = hr.y + hr.h / 2
        local px = sideR + MU.SIDE_GAP + MU.POWER_RING_PAD       -- left edge of the power button
        local wx = sideL - MU.SIDE_GAP - MU.WEAPON_RING_PAD                       -- right edge of the weapon frame
        u.power.rect = { x = px, y = cy - ps / 2, w = ps, h = ps }
        u.power.ringSize = ps + 2 * MU.POWER_RING_PAD
        u.weapon.rect = { x = wx - MU.WEAPON_W, y = cy - MU.WEAPON_H / 2, w = MU.WEAPON_W, h = MU.WEAPON_H }
        u.power.frame:SetWidth(ps)
        u.power.frame:SetHeight(ps)
        u.power.label:SetWidth(ps - 12)
        MU.LayoutGlow(u.power.glow, ps, ps)
        u.weapon.home = { point = "RIGHT", x = wx, y = -cy }
        u.weapon.baseW, u.weapon.baseH = MU.WEAPON_W, MU.WEAPON_H
        -- everything beside the hero is anchored to the board at its home coordinates, not to the hero frame:
        -- the lunge and the shake of a hero attack move the hero only (the rest hangs off the power / weapon)
        u.name:ClearAllPoints()
        u.name:SetPoint("BOTTOM", MU.board, "TOPLEFT", hr.x + hr.w / 2, -hr.y + 8)
        u.power.frame:ClearAllPoints()
        u.power.frame:SetPoint("LEFT", MU.board, "TOPLEFT", px, -cy)
        MU.PlaceSlot(u.weapon)
        u.counts:ClearAllPoints()
        u.counts:SetPoint("RIGHT", u.weapon.frame, "LEFT", -12, 0)
        if not MU.LayoutMana(key) then
            u.manaText:ClearAllPoints()
            u.manaText:SetPoint("BOTTOMLEFT", u.power.frame, "RIGHT", MU.POWER_RING_PAD + 12, 4)
            for i = 1, MU.MAX_CRYSTALS do
                u.crystals[i]:ClearAllPoints()
                u.crystals[i]:SetPoint("TOPLEFT", u.power.frame, "RIGHT", MU.POWER_RING_PAD + 12 + (i - 1) * (MU.CRYSTAL_W + 2), 0)
            end
            u.overload:ClearAllPoints()
            u.overload:SetPoint("TOPLEFT", u.power.frame, "RIGHT", MU.POWER_RING_PAD + 12, -MU.CRYSTAL_H - 3)
        end
    end
    local midY = math.floor(fh * MU.MID_Y)
    local mid = ui.mid
    mid.banner:ClearAllPoints()
    mid.banner:SetPoint("CENTER", MU.board, "TOPLEFT", MU.CenterX(bw), -midY)
    mid.turnText:ClearAllPoints()
    mid.turnText:SetPoint("BOTTOM", mid.endButton, "TOP", 0, 4)
    mid.endButton:ClearAllPoints()
    mid.endButton:SetPoint("RIGHT", MU.board, "TOPRIGHT", MU.END_DX, -midY - MU.END_DY)
    if not MU.PlaceDeck(ui.deck.o, "deck_top", bw, fh) then
        ui.deck.o:ClearAllPoints()
        ui.deck.o:SetPoint("CENTER", mid.endButton, "CENTER", MU.DECK_DX, MU.DECK_OPP_DY)
    end
    if not MU.PlaceDeck(ui.deck.m, "deck_bottom", bw, fh) then
        ui.deck.m:ClearAllPoints()
        ui.deck.m:SetPoint("CENTER", mid.endButton, "CENTER", MU.DECK_DX, MU.DECK_MY_DY)
    end
    mid.timerText:ClearAllPoints()
    mid.timerText:SetPoint("RIGHT", mid.endButton, "LEFT", -8, 0)
    -- the bubble is the player's hero talking: it sits just above their portrait
    MU.PlaceSpeech(mid.bubble, mid.bubbleText, ui.m.hero.frame)
    MU.mull.frame:ClearAllPoints()
    MU.mull.frame:SetAllPoints(MU.board)
    MU.pick.frame:ClearAllPoints()
    MU.pick.frame:SetAllPoints(MU.board)
    MU.chooser.frame:ClearAllPoints()
    MU.chooser.frame:SetAllPoints(MU.board)
    MU.result.frame:ClearAllPoints()
    MU.result.frame:SetAllPoints(MU.board)
    if MU.reveal then
        MU.reveal.frame:ClearAllPoints()
        MU.reveal.frame:SetPoint("CENTER", MU.board, "TOPLEFT", MU.CenterX(bw), -math.floor(fh * MU.MID_Y))
    end
    if MU.fatigue then
        MU.fatigue.frame:ClearAllPoints()
        MU.fatigue.frame:SetPoint("CENTER", MU.board, "TOPLEFT", MU.CenterX(bw), -math.floor(fh * MU.MID_Y))
    end
    MU.PlaceLog()
    MU.ReapplyAnim()
end

-- The history column stands on the pale stone plate at the left edge of the board field (MU.LOG_STRIP, shares of the
-- field picture, so it follows the art when the board is wider): the tiles that fit between its top and bottom are used.
function MU.PlaceLog()
    local log = MU.ui and MU.ui.log
    if not log or not MU.geom then
        return
    end
    local inset = MU.FIELD_INSET
    local fw = MU.geom.bw - 2 * inset
    local fh = MU.geom.fh - 2 * inset
    local st = MU.LOG_STRIP
    local height = (MU.FieldY(st.bottom) - MU.FieldY(st.top)) * fh
    log.frame:SetHeight(height)
    log.frame:ClearAllPoints()
    log.frame:SetPoint("TOP", MU.board, "TOPLEFT", inset + st.x * fw, -(inset + MU.FieldY(st.top) * fh))
    local count = math.floor((height - 2 * MU.LOG_MARGIN + MU.LOG_TILE_GAP) / (MU.LOG_TILE + 2 * MU.LOG_BORDER + MU.LOG_TILE_GAP))
    if count > MU.LOG_MAX_TILES then
        count = MU.LOG_MAX_TILES
    end
    if count ~= log.count then
        log.count = count
        MU.RefreshLog()
    end
end

-- The red X over a replaced mulligan card: two crossed bars along the diagonals of the visible card, each a tilted
-- solid strip set (CardWidget.PlaceStrips) with a darker, slightly bigger copy under it as the shadow. Also places the
-- red "REPLACED" ribbon under the card.
local function layoutMullX(slot)
    local W = H.CardWidget
    local k = MU.MULL_W / 256
    local v = W.VISIBLE
    local cx, cy = (v.left + v.right) / 2 * k, (v.top + v.bottom) / 2 * k
    local vw, vh = (v.right - v.left) * k, (v.bottom - v.top) * k
    if MU.Piece("mull_x") then
        -- the painted red X of the owner's art over the card
        slot.xImg = slot.mark:CreateTexture(nil, "ARTWORK")
        MU.SetPiece(slot.xImg, "mull_x")
        local piece = MU.Piece("mull_x")
        local h = vh * 0.92
        slot.xImg:SetHeight(h)
        slot.xImg:SetWidth(h * piece.w / piece.h)
        slot.xImg:ClearAllPoints()
        slot.xImg:SetPoint("CENTER", slot.frame, "TOPLEFT", cx, -cy)
    else
        local len = math.sqrt(vw * vw + vh * vh) * 0.8
        local thick = MU.MULL_W * 0.085
        local ang = math.atan(vh / vw)
        local flat = { a = 0.5, b = 0, c = 0.5, d = 0 }
        local bars = {
            { list = slot.xShadow1, grow = 7, angle = ang, layer = "BORDER", color = { 0, 0, 0, 0.55 } },
            { list = slot.xShadow2, grow = 7, angle = -ang, layer = "BORDER", color = { 0, 0, 0, 0.55 } },
            { list = slot.xBar1, grow = 0, angle = ang, layer = "ARTWORK", color = { 0.86, 0.1, 0.1, 1 } },
            { list = slot.xBar2, grow = 0, angle = -ang, layer = "ARTWORK", color = { 0.86, 0.1, 0.1, 1 } },
        }
        for i = 1, table.getn(bars) do
            local bar = bars[i]
            local rect = { l = cx - len / 2 - bar.grow / 2, t = cy - thick / 2 - bar.grow / 2, r = cx + len / 2 + bar.grow / 2,
                b = cy + thick / 2 + bar.grow / 2 }
            W.PlaceStrips(bar.list, slot.mark, bar.layer, WHITE, rect, cx, cy, flat, bar.angle, 14)
            for q = 1, table.getn(bar.list) do
                bar.list[q].tex:SetVertexColor(bar.color[1], bar.color[2], bar.color[3], bar.color[4])
            end
        end
    end
    -- the "REPLACED" label under the card, overlapping its bottom edge a little: the owner's painted label, else the
    -- red ribbon with text
    slot.tag:ClearAllPoints()
    slot.tag:SetPoint("CENTER", slot.frame, "TOPLEFT", cx, -(v.bottom * k + 12))
    local label = MU.Piece("mull_replaced")
    if label then
        MU.SetPiece(slot.tag, "mull_replaced")
        local tw = MU.MULL_W * 1.05
        slot.tag:SetWidth(tw)
        slot.tag:SetHeight(tw * label.h / label.w)
        slot.tagText:Hide()
    else
        local tw = MU.MULL_W * 0.92
        slot.tag:SetWidth(tw)
        slot.tag:SetHeight(tw * 0.3125 * 1.25)
        if not MU.SetPiece(slot.tag, "ribbon_red") then
            slot.tag:SetTexture(WHITE)
            slot.tag:SetTexCoord(0, 1, 0, 1)
            slot.tag:SetVertexColor(0.75, 0.1, 0.1, 1)
        end
        slot.tagText:ClearAllPoints()
        slot.tagText:SetPoint("CENTER", slot.tag, "CENTER", 0, 1)
    end
end

local function makeMulligan(board)
    local f = CreateFrame("Frame", "HearthstoneMatchMulligan", board)
    f:SetFrameLevel(board:GetFrameLevel() + MU.OVERLAY_LEVEL)
    f:SetAllPoints(board)
    f:EnableMouse(true)
    f:SetBackdrop({ bgFile = WHITE, tile = true, tileSize = 16, insets = { left = 0, right = 0, top = 0, bottom = 0 } })
    f:SetBackdropColor(0, 0, 0, 0.78)
    local m = { frame = f, slots = {} }
    -- the title ribbon with "Starting Hand", the subtitle under it
    m.ribbon = f:CreateTexture(nil, "ARTWORK")
    m.ribbon:SetWidth(MU.MULL_RIBBON_W)
    m.ribbon:SetHeight(MU.MULL_RIBBON_H)
    m.ribbon:SetPoint("TOP", f, "TOP", 0, -10)
    if not MU.SetPiece(m.ribbon, "ribbon_blue") then
        m.ribbon:Hide()
    end
    local title = outlineText(f, 26, "CENTER")
    title:SetPoint("CENTER", m.ribbon, "CENTER", 0, 3)
    title:SetText("Starting Hand")
    m.title = title
    m.hint = outlineText(f, 17, "CENTER")
    m.hint:SetPoint("TOP", f, "TOP", 0, -(10 + MU.MULL_RIBBON_H + 4))
    m.hint:SetWidth(520)
    -- the owner's banner has both lines painted in: it replaces the ribbon, the title and the subtitle
    m.banner = f:CreateTexture(nil, "ARTWORK")
    m.banner:SetPoint("TOP", f, "TOP", 0, -2)
    if MU.SetPiece(m.banner, "banner_starting_hand") then
        local piece = MU.Piece("banner_starting_hand")
        m.bannerH = MU.MULL_BANNER_W * piece.h / piece.w
        m.banner:SetWidth(MU.MULL_BANNER_W)
        m.banner:SetHeight(m.bannerH)
        m.ribbon:Hide()
        m.title:Hide()
        m.hint:ClearAllPoints()
        m.hint:SetPoint("TOP", f, "TOP", 0, -(2 + m.bannerH + 4))
    else
        m.banner:Hide()
    end
    m.note = newText(f, "GameFontDisableSmall", "CENTER")
    m.note:SetWidth(420)
    for i = 1, MU.MULL_SLOTS do
        local b = CreateFrame("Button", "HearthstoneMatchMull" .. i, f)
        b:SetFrameLevel(f:GetFrameLevel() + 2)
        b:SetWidth(MU.MULL_W)
        b:SetHeight(math.floor(MU.MULL_W * 388 / 256 + 0.5))
        b:RegisterForClicks("LeftButtonUp")
        local slot = { frame = b }
        -- a kept card has the green highlight behind it (a frame below the card), a replaced one the red X and ribbon
        slot.glow = MU.MakeHandGlow(b, f:GetFrameLevel() + 1, MU.MULL_W)
        slot.widget = H.CardWidget.Create(b, nil, MU.MULL_W, false)
        slot.widget.frame:SetPoint("CENTER", b, "CENTER", 0, 0)
        -- the card back over the visible card while it is dealt and turns over (MU.MullLook)
        slot.back = CreateFrame("Frame", nil, b)
        slot.back:SetFrameLevel(b:GetFrameLevel() + 4)
        local backTex = slot.back:CreateTexture(nil, "ARTWORK")
        backTex:SetAllPoints(slot.back)
        if not MU.SetPiece(backTex, "card_back") then
            backTex:Hide()
            slot.back:SetBackdrop({
                bgFile = DIALOG_BG, edgeFile = TOOLTIP_EDGE, tile = true, tileSize = 32, edgeSize = 12,
                insets = { left = 3, right = 3, top = 3, bottom = 3 },
            })
            slot.back:SetBackdropBorderColor(0.75, 0.6, 0.3, 1)
        end
        slot.back:Hide()
        slot.mark = CreateFrame("Frame", nil, b)
        slot.mark:SetAllPoints(b)
        slot.mark:SetFrameLevel(b:GetFrameLevel() + 6)
        slot.xShadow1, slot.xShadow2, slot.xBar1, slot.xBar2 = {}, {}, {}, {}
        slot.tag = slot.mark:CreateTexture(nil, "ARTWORK")
        slot.tagText = outlineText(slot.mark, 15, "CENTER")
        slot.tagText:SetText("REPLACED")
        layoutMullX(slot)
        slot.mark:Hide()
        local idx = i
        b:SetScript("OnClick", function()
            MU.OnMullClick(idx)
        end)
        b:SetScript("OnEnter", function()
            MU.OnMullEnter(idx)
        end)
        b:SetScript("OnLeave", function()
            MU.HidePreview()
        end)
        b:Hide()
        m.slots[i] = slot
    end
    -- the same glowing Done plate as the pack reveal when its art is installed
    m.confirm = doneButton("HearthstoneMatchMullConfirm", f)
    m.confirm:SetScript("OnClick", function()
        MU.OnMullConfirm()
    end)
    -- the deck pile the cards come from and go back to: a few card backs, shown while the mulligan view is animated
    m.deck = {}
    for i = 1, 3 do
        local back = makeBack(f)
        back:SetWidth(MU.MULL_W * MU.MULL_DECK_SCALE * 0.8)
        back:SetHeight(MU.MULL_H * MU.MULL_DECK_SCALE * 0.8)
        back:SetFrameLevel(f:GetFrameLevel() + 1)
        m.deck[i] = back
    end
    -- a click on the view ends the animation that is running
    f:SetScript("OnMouseDown", function()
        MU.MullSkip()
    end)
    f:Hide()
    MU.mull = m
end

-- The pending pick panel (Tracking): a dim layer over the board with the option cards.
local function makePick(board)
    local f = CreateFrame("Frame", "HearthstoneMatchPick", board)
    f:SetFrameLevel(board:GetFrameLevel() + MU.OVERLAY_LEVEL)
    f:SetAllPoints(board)
    f:EnableMouse(true)
    f:SetBackdrop({ bgFile = WHITE, tile = true, tileSize = 16, insets = { left = 0, right = 0, top = 0, bottom = 0 } })
    f:SetBackdropColor(0, 0, 0, 0.78)
    local p = { frame = f, slots = {} }
    p.title = newText(f, "GameFontNormalLarge", "CENTER")
    p.title:SetPoint("TOP", f, "TOP", 0, -50)
    p.hint = newText(f, "GameFontHighlight", "CENTER")
    p.hint:SetPoint("TOP", f, "TOP", 0, -78)
    p.hint:SetWidth(520)
    for i = 1, MU.PICK_SLOTS do
        local b = CreateFrame("Button", "HearthstoneMatchPickCard" .. i, f)
        b:SetWidth(MU.PICK_W)
        b:SetHeight(math.floor(MU.PICK_W * 388 / 256 + 0.5))
        b:RegisterForClicks("LeftButtonUp", "RightButtonUp")
        local slot = { frame = b }
        slot.widget = H.CardWidget.Create(b, nil, MU.PICK_W, false)
        slot.widget.frame:SetPoint("CENTER", b, "CENTER", 0, 0)
        slot.label = newText(b, "GameFontNormalLarge", "CENTER")
        slot.label:SetPoint("TOP", b, "BOTTOM", 0, -8)
        local idx = i
        b:SetScript("OnClick", function()
            MU.OnPickClick(idx, arg1)
        end)
        b:SetScript("OnEnter", function()
            MU.OnEnter(MU.pick.slots[idx], nil)
        end)
        b:SetScript("OnLeave", function()
            MU.HidePreview()
        end)
        b:Hide()
        p.slots[i] = slot
    end
    f:Hide()
    MU.pick = p
end

-- The art piece of a plate or banner (ButtonTextures.lua), or nil while it is not deployed.
local function chooserPiece(name)
    local UIc = H.CollectionUI
    return UIc and UIc.Piece and UIc.Piece(name)
end

-- An icy plate with its word in the art (btn_show, btn_hide): a plain button with the word without the art.
local function plateButton(name, parent, piece, word, w, h, onClick)
    if not piece then
        local b = button(name, parent, w, word)
        b:SetScript("OnClick", onClick)
        return b
    end
    local b = CreateFrame("Button", name, parent)
    b:SetWidth(w)
    b:SetHeight(h)
    b:SetFrameLevel(parent:GetFrameLevel() + 3)
    b:SetNormalTexture(piece.path)
    b:GetNormalTexture():SetTexCoord(0, piece.right, 0, piece.bottom)
    b:SetHighlightTexture(piece.path, "ADD")
    local ht = b:GetHighlightTexture()
    ht:SetTexCoord(0, piece.right, 0, piece.bottom)
    ht:SetAlpha(0.35)
    if b.SetHitRectInsets then
        b:SetHitRectInsets(20, 20, 12, 12)
    end
    -- the same pulsing halo as the Done and Choose plates (ButtonGlow, btn_glow)
    if chooserPiece("btn_glow") and H.CollectionUI.ButtonGlow then
        H.CollectionUI.ButtonGlow(b)
    end
    b:SetScript("OnClick", onClick)
    return b
end

-- The Choose One chooser: the options of a hand card as cards, plus Cancel, under the scroll banner. The board is dimmed
-- behind a frame that takes every click; Hide shows only that frame and the Show plate, so the board stays blocked.
local function makeChooser(board)
    local f = CreateFrame("Frame", "HearthstoneMatchChooser", board)
    f:SetFrameLevel(board:GetFrameLevel() + MU.OVERLAY_LEVEL + 1)
    f:SetAllPoints(board)
    f:EnableMouse(true)
    f:SetBackdrop({ bgFile = WHITE, tile = true, tileSize = 16, insets = { left = 0, right = 0, top = 0, bottom = 0 } })
    f:SetBackdropColor(0, 0, 0, MU.CHOOSE_DIM)
    f:SetScript("OnMouseUp", function()
        if arg1 == "RightButton" then
            MU.CancelTarget()
        end
    end)
    local c = { frame = f, slots = {} }
    -- everything but the Hide / Show plate sits on the panel, so Hide takes it all away at once
    local p = CreateFrame("Frame", "HearthstoneMatchChooserPanel", f)
    p:SetAllPoints(f)
    c.panel = p
    local banner = chooserPiece("choose_banner")
    if banner then
        c.banner = p:CreateTexture(nil, "ARTWORK")
        c.banner:SetTexture(banner.path)
        c.banner:SetTexCoord(0, banner.right, 0, banner.bottom)
    end
    c.title = newText(p, "GameFontNormalLarge", "CENTER")
    c.title:SetPoint("TOP", p, "TOP", 0, -50)
    if c.banner then
        c.title:Hide()
    end
    for i = 1, MA.MAX_OPTIONS do
        local b = CreateFrame("Button", "HearthstoneMatchChoice" .. i, p)
        b:SetWidth(MU.CHOOSE_W)
        b:SetHeight(math.floor(MU.CHOOSE_W * 388 / 256 + 0.5))
        b:RegisterForClicks("LeftButtonUp", "RightButtonUp")
        local slot = { frame = b }
        -- the playable highlight of a hand card, made first so it sits behind the card (the same soft strips and settings)
        slot.glow = MU.MakeHandGlow(b, b:GetFrameLevel() + 1, MU.CHOOSE_W, 0.6)
        slot.widget = H.CardWidget.Create(b, nil, MU.CHOOSE_W, false)
        slot.widget.frame:SetPoint("CENTER", b, "CENTER", 0, 0)
        local idx = i
        b:SetScript("OnClick", function()
            MU.OnChooseClick(idx, arg1)
        end)
        -- no large preview on hover: the option cards are already shown at a readable size
        b:Hide()
        c.slots[i] = slot
    end
    -- the Hide and Show plates share one corner of the board, so the one that is not shown takes no room
    local hideArt, showArt = chooserPiece("btn_hide"), chooserPiece("btn_show")
    c.hideButton = plateButton("HearthstoneMatchChooserHide", f, hideArt, "Hide", MU.PLATE_W, MU.PLATE_H, function()
        MU.chooserHidden = true
        MU.Refresh()
    end)
    c.showButton = plateButton("HearthstoneMatchChooserShow", f, showArt, "Show", MU.PLATE_W, MU.PLATE_H, function()
        MU.chooserHidden = nil
        MU.Refresh()
    end)
    for _, b in ipairs({ c.hideButton, c.showButton }) do
        b:ClearAllPoints()
        b:SetPoint("BOTTOMLEFT", f, "BOTTOMLEFT", 46, 36)
        b:Hide()
    end
    f:Hide()
    MU.chooser = c
end

local function makeResult(board)
    local f = CreateFrame("Frame", "HearthstoneMatchResult", board)
    f:SetFrameLevel(board:GetFrameLevel() + MU.OVERLAY_LEVEL + 2)
    f:SetAllPoints(board)
    f:EnableMouse(true)
    f:SetBackdrop({ bgFile = WHITE, tile = true, tileSize = 16, insets = { left = 0, right = 0, top = 0, bottom = 0 } })
    f:SetBackdropColor(0, 0, 0, 0.6)
    local r = { frame = f }
    local box = CreateFrame("Frame", nil, f)
    box:SetWidth(440)
    box:SetHeight(230)
    box:SetPoint("CENTER", f, "CENTER", 0, 0)
    r.box = box
    -- the emblem (ARTWORK) over the portrait strips (BORDER), see MU.EMBLEMS
    r.strips = {}
    for i = 1, MU.EMBLEM_MAX_STRIPS do
        local t = box:CreateTexture(nil, "BORDER")
        t:Hide()
        r.strips[i] = t
    end
    r.emblem = box:CreateTexture(nil, "ARTWORK")
    r.emblem:Hide()
    r.title = box:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
    r.title:SetFont(NUMBER_FONT, 34, "OUTLINE")
    r.reason = newText(box, "GameFontHighlight", "CENTER")
    r.reason:SetWidth(380)
    r.summary = newText(box, "GameFontNormalSmall", "CENTER")
    r.summary:SetWidth(380)
    r.leave = doneButton("HearthstoneMatchLeaveButton", box)
    r.leave:SetScript("OnClick", function()
        MU.Leave()
    end)
    layoutResult(r, false, false)
    f:Hide()
    MU.result = r
end

function MU.Build()
    if MU.frame then
        return MU.frame
    end
    if not CreateFrame or not UIParent then
        return nil
    end
    local scale, w, h = MU.Fit(GetScreenWidth and GetScreenWidth() or 1024, GetScreenHeight and GetScreenHeight() or 768)
    local bw, bh = MU.BoardSize(w)
    MU.geom = { w = w, h = h, bw = bw, bh = bh, fh = MU.FieldHeight(bh) }

    local f = CreateFrame("Frame", "HearthstoneMatchFrame", UIParent)
    f:SetWidth(w)
    f:SetHeight(h)
    f:SetScale(scale)
    f:SetPoint("CENTER", UIParent, "CENTER", 0, 0)
    f:SetFrameStrata("HIGH")
    f:SetToplevel(true)
    f:EnableMouse(true)
    f:SetClampedToScreen(true)
    local cloth = MU.ClothInstalled()
    f:SetBackdrop({
        bgFile = (not cloth) and DIALOG_BG or nil, edgeFile = DIALOG_EDGE, tile = true, tileSize = 32, edgeSize = 32,
        insets = { left = 11, right = 12, top = 12, bottom = 11 },
    })
    if cloth then
        MU.cloth = {}
        for r = 0, MU.CLOTH_ROWS - 1 do
            for c = 0, MU.CLOTH_COLS - 1 do
                local tex = f:CreateTexture(nil, "BACKGROUND")
                MU.SetPiece(tex, "cloth_c" .. c .. "r" .. r)
                table.insert(MU.cloth, { tex = tex, c = c, r = r })
            end
        end
    else
        MU.cloth = nil
    end
    -- the stone silver frame of the collection and pack windows (the same dialog border, tinted light grey)
    local tint = H.CollectionUI and H.CollectionUI.FRAME_TINT or MU.FRAME_TINT
    f:SetBackdropBorderColor(tint[1], tint[2], tint[3], tint[4])
    f:SetScript("OnMouseUp", function()
        if arg1 == "RightButton" then
            MU.CancelTarget()
        end
    end)
    f:Hide()
    MU.frame = f

    -- (the field picture reaches above the board, over the title bar: the title and the buttons stand above the board)
    local head = CreateFrame("Frame", nil, f)
    head:SetFrameLevel(f:GetFrameLevel() + MU.HEAD_LEVEL)
    f.title = head:CreateFontString(nil, "ARTWORK", "GameFontNormalLarge")
    f.title:SetPoint("TOP", f, "TOP", 0, -14)
    local close = CreateFrame("Button", "HearthstoneMatchCloseButton", f, "UIPanelCloseButton")
    close:SetFrameLevel(f:GetFrameLevel() + MU.HEAD_LEVEL)
    close:SetPoint("TOPRIGHT", f, "TOPRIGHT", -6, -6)
    close:SetScript("OnClick", function()
        MU.RequestClose()
    end)
    -- the surrender of a draft arena duel (the close button pauses it): shown by MU.RefreshSurrender
    local give = button("HearthstoneMatchSurrenderButton", f, 130, "Surrender")
    give:SetHeight(20)
    give:SetFrameLevel(f:GetFrameLevel() + MU.HEAD_LEVEL)
    give:SetPoint("TOPRIGHT", f, "TOPRIGHT", -40, -9)
    give:SetScript("OnClick", function()
        MU.AskArenaSurrender()
    end)
    give:SetScript("OnEnter", function()
        if MU.IsLadderDuel() then
            tooltip(this, "ANCHOR_BOTTOM", "Surrender", "Ends this duel as a loss on the ladder.",
                "The close button (X) pauses the duel instead.")
        else
            tooltip(this, "ANCHOR_BOTTOM", "Surrender", "Ends this duel as a loss and ends your " .. AR.NAME .. " run.",
                "The close button (X) pauses the duel instead.")
        end
    end)
    give:SetScript("OnLeave", function()
        hideTip()
    end)
    give:Hide()
    f.surrender = give

    -- the board area
    local board = CreateFrame("Frame", "HearthstoneMatchBoard", f)
    board:SetWidth(bw)
    board:SetHeight(bh)
    board:SetPoint("TOPLEFT", f, "TOPLEFT", MU.PAD + MU.LOG_W + MU.GAP, -MU.TOP)
    plate(board, 0.95)
    board:SetBackdropColor(0.13, 0.1, 0.07, 0.95)
    if MU.cloth then
        -- the cloth of the window shows around the field and under the hand: no plate behind the board
        board:SetBackdropColor(0, 0, 0, 0)
        board:SetBackdropBorderColor(0, 0, 0, 0)
    end
    board:EnableMouse(true)
    board:SetScript("OnMouseUp", function()
        if arg1 == "RightButton" then
            MU.CancelTarget()
        elseif MU.Behind() then
            MU.AnimSkip()            -- a click on the empty board skips the running animation
        end
    end)
    local field = board:CreateTexture(nil, "BACKGROUND")
    -- the picture reaches MU.FieldOverflow pixels above the board (up over the title bar, past the window's top edge):
    -- the field's arches and rows stand that much higher (MU.FieldY) without cutting the art's top
    -- (the part above the window's inner top edge, MU.FIELD_TOP_LIMIT above the board, is cut off with the texture coordinates)
    local over = MU.FieldOverflow(MU.geom.fh)
    local shown = math.min(over, MU.FIELD_TOP_LIMIT)
    field:SetPoint("TOPLEFT", board, "TOPLEFT", MU.FIELD_INSET, -MU.FIELD_INSET + shown)
    field:SetPoint("BOTTOMRIGHT", board, "BOTTOMRIGHT", -MU.FIELD_INSET, MU.FIELD_INSET + MU.HAND_STRIP)
    if MU.SetPiece(field, "board_field") then
        local p = MU.Piece("board_field")
        local right, bottom = p.right, p.bottom
        if type(right) ~= "number" or type(bottom) ~= "number" then
            right, bottom = 1, 1
        end
        local full = MU.geom.fh - 2 * MU.FIELD_INSET + over      -- height of the whole picture on screen
        field:SetTexCoord(0, right, bottom * (over - shown) / full, bottom)
    else
        field:Hide()
    end
    MU.board = board

    MU.ui = { hand = {} }
    MU.ui.o = makeSide(board, "o")
    MU.ui.m = makeSide(board, "m")
    MU.ui.o.backs = {}
    for i = 1, MU.MAX_HAND do
        MU.ui.o.backs[i] = makeBack(board)
    end
    for i = 1, MU.MAX_HAND do
        MU.ui.hand[i] = makeHandSlot(board, i)
    end

    -- the middle strip: message bubble, turn banner, End Turn
    local mid = {}
    mid.banner = CreateFrame("Frame", nil, board)
    mid.banner:SetWidth(150)
    mid.banner:SetHeight(32)
    mid.banner:SetFrameLevel(board:GetFrameLevel() + MU.FX_LEVEL - 8)   -- over the board, under the revealed card
    mid.banner:EnableMouse(false)                                      -- a flash never takes a click
    mid.bannerTex = mid.banner:CreateTexture(nil, "ARTWORK")
    mid.bannerTex:SetAllPoints(mid.banner)
    mid.bannerText = newText(mid.banner, "GameFontNormalLarge", "CENTER")
    mid.bannerText:SetAllPoints(mid.banner)
    mid.banner:Hide()
    mid.turnText = newText(board, "GameFontNormalSmall", "CENTER")
    mid.turnText:SetAlpha(0.7)                                         -- small and quiet
    if MU.Piece("end_turn_button") and MU.Piece("enemy_turn_button") then
        mid.endButton = CreateFrame("Button", "HearthstoneMatchEndTurnButton", board)
        mid.endButton:SetWidth(MU.END_W)
        mid.endButton:SetHeight(MU.END_H)
        mid.endTex = mid.endButton:CreateTexture(nil, "ARTWORK")
        mid.endTex:SetAllPoints(mid.endButton)
        MU.SetPiece(mid.endTex, "end_turn_button")
        -- the hover glow: the plate again, bigger, tinted gold and added behind the plate, so a gold halo shows around it
        mid.endGlow = mid.endButton:CreateTexture(nil, "BACKGROUND")
        mid.endGlow:SetBlendMode("ADD")
        mid.endGlow:SetPoint("TOPLEFT", mid.endButton, "TOPLEFT", -MU.END_GLOW, MU.END_GLOW * 0.8)
        mid.endGlow:SetPoint("BOTTOMRIGHT", mid.endButton, "BOTTOMRIGHT", MU.END_GLOW, -MU.END_GLOW * 0.8)
        MU.SetPiece(mid.endGlow, "end_turn_button")
        mid.endGlow:SetVertexColor(1, 0.85, 0.25)
        mid.endGlow:Hide()
        mid.endButton:SetScript("OnEnter", function()
            MU.EndHover(true)
        end)
        mid.endButton:SetScript("OnLeave", function()
            MU.EndHover(false)
        end)
    else
        mid.endButton = button("HearthstoneMatchEndTurnButton", board, 120, "End Turn")
        mid.endButton:SetHeight(28)
    end
    mid.endButton:SetScript("OnClick", function()
        MU.OnEndTurnClick()
    end)
    -- the two card decks beside the button: invisible hover areas (placed in MU.Layout beside the button)
    MU.ui.deck = {}
    for _, key in ipairs({ "o", "m" }) do
        local d = CreateFrame("Frame", nil, board)
        d:SetWidth(MU.DECK_W)
        d:SetHeight(MU.DECK_H)
        d:SetFrameLevel(board:GetFrameLevel() + 2)   -- under the hand, the minions and the bubble
        d:EnableMouse(true)
        d.stack = d:CreateTexture(nil, "ARTWORK")
        d.stack:Hide()
        d.empty = d:CreateTexture(nil, "ARTWORK")
        d.empty:Hide()
        local k = key
        d:SetScript("OnEnter", function()
            MU.OnDeckEnter(k)
        end)
        d:SetScript("OnLeave", function()
            hideTip()
        end)
        MU.ui.deck[key] = d
    end
    mid.timerText = newText(board, "GameFontNormalLarge", "RIGHT")   -- Nozdormu countdown
    mid.timerText:SetWidth(44)
    mid.timerText:SetHeight(28)
    mid.timerText:Hide()
    mid.bubble = CreateFrame("Frame", nil, board)
    mid.bubble:SetHeight(MU.BUBBLE_HEIGHT)
    mid.bubble:SetFrameStrata("DIALOG")      -- over the board's icons and frames, as the game's speech bubbles are
    mid.bubble:SetScript("OnUpdate", function() MU.BubbleTick(mid.bubble) end)
    local bubbleArt = MU.SetBubbleParts(mid.bubble)
    if not bubbleArt then
        plate(mid.bubble, 0.9)
    end
    mid.bubbleText = newText(mid.bubble, "GameFontNormal", "LEFT")
    if bubbleArt then
        mid.bubbleText:SetTextColor(0.12, 0.1, 0.08)   -- dark ink on the light bubble (the plate fallback is dark: light text)
        mid.bubbleText:SetShadowColor(0, 0, 0, 0)      -- the game's speech text has no shadow (GameFontNormal does)
    end
    mid.bubbleText:SetPoint("TOPLEFT", mid.bubble, "TOPLEFT", 14, -5)
    mid.bubbleText:SetPoint("BOTTOMRIGHT", mid.bubble, "BOTTOMRIGHT", -14, 11)
    mid.bubbleText:SetJustifyV("MIDDLE")
    mid.bubble:Hide()
    MU.ui.mid = mid

    -- the event log
    -- (a child of the board, over the stone plate of the field art; MU.PlaceLog sizes and places it)
    local logFrame = CreateFrame("Frame", "HearthstoneMatchLog", board)
    logFrame:SetWidth(MU.LOG_TILE + 2 * MU.LOG_BORDER)
    logFrame:SetHeight(bh)
    -- No backdrop: the field art is the background of the tiles.
    local logStep = MU.LOG_TILE + 2 * MU.LOG_BORDER + MU.LOG_TILE_GAP
    local logTiles = {}
    local rimSize = MU.LOG_TILE + 2 * MU.LOG_BORDER
    for i = 1, MU.LOG_MAX_TILES do
        local tf = CreateFrame("Button", nil, logFrame)
        tf:SetWidth(rimSize)
        tf:SetHeight(rimSize)
        tf:SetPoint("TOP", logFrame, "TOP", 0, -(MU.LOG_MARGIN + (i - 1) * logStep))
        tf:EnableMouse(true)
        local tile = { frame = tf }
        -- the rim: a dark outline, the colour inside it, then the picture (inset LOG_BORDER) over both
        tile.outline = tf:CreateTexture(nil, "BACKGROUND")
        tile.outline:SetTexture(WHITE)
        tile.outline:SetVertexColor(0.04, 0.03, 0.02, 1)
        tile.outline:SetAllPoints(tf)
        tile.rim = tf:CreateTexture(nil, "BORDER")
        tile.rim:SetTexture(WHITE)
        tile.rim:SetVertexColor(MU.LOG_RIM.m[1], MU.LOG_RIM.m[2], MU.LOG_RIM.m[3], 1)
        tile.rim:SetPoint("TOPLEFT", tf, "TOPLEFT", 1, -1)
        tile.rim:SetPoint("BOTTOMRIGHT", tf, "BOTTOMRIGHT", -1, 1)
        tile.art = tf:CreateTexture(nil, "ARTWORK")
        tile.art:SetPoint("TOPLEFT", tf, "TOPLEFT", MU.LOG_BORDER, -MU.LOG_BORDER)
        tile.art:SetPoint("BOTTOMRIGHT", tf, "BOTTOMRIGHT", -MU.LOG_BORDER, MU.LOG_BORDER)
        tile.smallRim = tf:CreateTexture(nil, "OVERLAY")
        tile.smallRim:SetTexture(WHITE)
        tile.smallRim:SetVertexColor(0.05, 0.05, 0.05)
        tile.smallRim:SetWidth(MU.LOG_SMALL + 2)
        tile.smallRim:SetHeight(MU.LOG_SMALL + 2)
        tile.smallRim:SetPoint("BOTTOMRIGHT", tf, "BOTTOMRIGHT", 0, 0)
        tile.small = tf:CreateTexture(nil, "OVERLAY")
        tile.small:SetWidth(MU.LOG_SMALL)
        tile.small:SetHeight(MU.LOG_SMALL)
        tile.small:SetPoint("BOTTOMRIGHT", tf, "BOTTOMRIGHT", -1, 1)
        tile.icon = tf:CreateTexture(nil, "OVERLAY")
        tile.icon:SetWidth(MU.LOG_ICON)
        tile.icon:SetHeight(MU.LOG_ICON)
        tile.icon:SetPoint("TOPRIGHT", tf, "TOPRIGHT", 3, 3)
        tf:SetScript("OnEnter", function()
            MU.OnLogEnter(tile)
        end)
        tf:SetScript("OnLeave", function()
            MU.OnLogLeave()
        end)
        tf:Hide()
        logTiles[i] = tile
    end
    MU.ui.log = { frame = logFrame, tiles = logTiles, count = 0 }
    MU.PlaceLog()

    makeMulligan(board)
    makePick(board)
    makeChooser(board)
    makeResult(board)
    buildFx(board)
    buildPreview()
    MU.BuildDrag()
    MU.Place()
    MU.LayoutCloth()

    f:SetScript("OnShow", function()
        MU.OnShow()
    end)
    f:SetScript("OnHide", function()
        MU.OnHide()
        -- closed for good (not shown again for a running match): the arena window may come back
        if not f:IsShown() and H.OnMatchWindowHidden then
            H.OnMatchWindowHidden()
        end
    end)
    if UISpecialFrames then
        local have = false
        for i = 1, table.getn(UISpecialFrames) do
            if UISpecialFrames[i] == "HearthstoneMatchFrame" then
                have = true
            end
        end
        if not have then
            table.insert(UISpecialFrames, "HearthstoneMatchFrame")
        end
    end
    return f
end

-- The window about 80% of the screen (also after a resolution change): scale, width and positions.
function MU.ApplyFit()
    local f = MU.frame
    if not f then
        return
    end
    local scale, w, h = MU.Fit(GetScreenWidth and GetScreenWidth() or 1024, GetScreenHeight and GetScreenHeight() or 768)
    f:SetScale(scale)
    if w ~= MU.geom.w then
        local bw, bh = MU.BoardSize(w)
        MU.geom = { w = w, h = h, bw = bw, bh = bh, fh = MU.FieldHeight(bh) }
        f:SetWidth(w)
        MU.board:SetWidth(bw)
        MU.Place()
        MU.LayoutCloth()
    end
end

-- Builds the window on first use. A failure is reported once instead of raising.
function MU.EnsureBuilt()
    if MU.frame then
        return true
    end
    if not CreateFrame or MU.buildFailed then
        return false   -- a failed build is not tried again (its named frames exist already): /reload after fixing it
    end
    local ok, err = pcall(MU.Build)
    if not ok or not MU.frame or not MU.ui or not MU.mull or not MU.pick or not MU.chooser or not MU.result then
        -- a partial build may have left named frames behind: hide them so they cannot pop up later
        local stale = getglobal and getglobal("HearthstoneMatchFrame")
        if stale then
            stale:Hide()
        end
        MU.frame, MU.ui, MU.mull, MU.result, MU.board, MU.preview = nil, nil, nil, nil, nil, nil
        MU.pick, MU.chooser = nil, nil
        MU.pool, MU.fxFrame, MU.reveal, MU.fatigue, MU.fxItems, MU.touched = nil, nil, nil, nil, {}, {}
        MU.buildFailed = true
        if H.PrintError then
            H.PrintError("Could not build the match window: " .. tostring(err))
        end
        return false
    end
    return true
end

function MU.Show()
    if not MU.EnsureBuilt() then
        return false
    end
    if not MU.frame:IsShown() then
        MU.frame:Show()
    else
        MU.Refresh()
    end
    return true
end

-- /hs game: shows the window when a match is known.
function H.ShowMatch()
    local g = MA.game
    if g and (MA.IsRunning(g) or g.ph == "over") then
        return MU.Show()
    end
    return false
end

-- The match state changed (Hearthstone.lua): a running or finished match opens the window if it
-- is closed, no match closes it, and an open window is redrawn.
function MU.OnMatchChanged()
    MU.EndDrag()
    MU.sel, MU.choose = nil, nil
    local g = MA.game
    local tr = MA.TakeTransition()
    if not g then
        MU.AnimAbort()
    elseif tr then
        if tr.snap and (tr.reason == "resync" or tr.reason == "first") then
            MU.SyncBanner(g)              -- a reload or a resync is no new turn: the banner is not replayed
        end
        MU.AnimEnqueue(tr)
    end
    if g then
        MU.MullBeginSwap(g)
    end
    if g and (MA.IsRunning(g) or g.ph == "over") then
        if MU.frame and MU.frame:IsShown() then
            MU.Refresh()
        else
            MU.Show()
        end
    elseif MU.frame and MU.frame:IsShown() then
        MU.sel = nil
        MU.HideWindow()
    end
end

function H.OnMatchChanged()
    MU.OnMatchChanged()
end

-- Pooled drag input. Cursor and rects use board design pixels; physical pixel spacing is
-- converted by the board's effective scale. No OnEnter events participate in targeting.
function MU.DragAllowed(option)
    local g = MA.game
    return g and g.ph == "play" and MA.IsMyTurn(g) and not MA.pending
        and not MA.IsPicking(g) and not MU.Behind() and (not MU.choose or option or (MU.drag and MU.drag.choice))
        and MU.frame and MU.frame:IsShown() and MU.DragEnabled()
end

function MU.DragCursor()
    local x, y = GetCursorPosition()
    local root = UIParent:GetEffectiveScale()
    local scale = MU.board:GetEffectiveScale() / root
    local left, top = MU.board:GetLeft(), MU.board:GetTop()
    if not left or not top then return nil end
    return (x / root - left * scale) / scale, (top * scale - y / root) / scale
end

local function inRect(x, y, r)
    return r and x >= r.x and x <= r.x + r.w and y >= r.y and y <= r.y + r.h
end

function MU.DragHit(x, y)
    local keys = { "m", "o" }
    for k = 1, 2 do
        local u = MU.ui[keys[k]]
        local slots = { u.hero, u.power }
        for i = 1, MU.MAX_BOARD do table.insert(slots, u.minions[i]) end
        for i = 1, table.getn(slots) do
            local s = slots[i]
            if s.entity and s.frame:IsShown() and inRect(x, y, s.dragRect or s.rect) then return s.entity end
        end
    end
    return nil
end

function MU.HandAnchor(source)
    local g = MA.game
    local layout = MU.HandLayout(table.getn(g.hand), MU.geom.bw, MU.geom.bh)
    for i = 1, table.getn(g.hand) do
        if g.hand[i].id == source then
            return { x = layout[i].x + MU.HAND_W / 2, y = layout[i].y + MU.HAND_H / 2 }
        end
    end
    return nil
end

function MU.EndDrag()
    local d = MU.drag
    local stage = MU.dragStage
    MU.drag = nil
    MU.dragStage = nil
    if d and d.slot then d.slot.frame:SetAlpha(d.alpha) end
    if stage then stage.slot.frame:SetAlpha(stage.alpha) end
    if MU.dragPool then
        MU.dragPool.copy.frame:Hide()
        MU.dragPool.copy.frame:ClearAllPoints()
        MU.dragPool.head:Hide()
        MU.dragPool.head:ClearAllPoints()
        MU.dragPool.head:SetWidth(24)
        MU.dragPool.head:SetHeight(24)
        MU.dragPool.head:SetTexCoord(0, 1, 0, 1)
        MU.dragPool.head:SetVertexColor(1, 1, 1)
        for i = 1, table.getn(MU.dragPool.dots) do
            local dot = MU.dragPool.dots[i]
            dot:Hide()
            dot:ClearAllPoints()
            dot:SetWidth(4)
            dot:SetHeight(4)
        end
    end
    if MU.ui then
        for i = 1, MU.MAX_BOARD do
            MU.ui.m.minions[i].dragRect = nil
            MU.PlaceSlot(MU.ui.m.minions[i])
        end
        local keys = { "m", "o" }
        for k = 1, 2 do
            local u = MU.ui[keys[k]]
            u.hero.glow.frame:SetAlpha(1)
            for i = 1, MU.MAX_BOARD do u.minions[i].glow.frame:SetAlpha(1) end
        end
    end
end

function MU.DragGap(x, y)
    -- the whole board above the play line is a drop area (the enemy's half too: the card still lands in my row)
    if x < 0 or x > MU.geom.bw or y < 0 or y >= MU.PlayLine(MU.geom.bh) then return nil end
    local n = table.getn(MA.game.me.board)
    if n >= MU.MAX_BOARD then return nil end
    local xs = MU.RowPositions(n + 1, MU.MINION_W, MU.MINION_GAP, MU.geom.bw, MU.CenterX(MU.geom.bw))
    local p = 0
    for i = 1, n do
        if x > (xs[i] + xs[i + 1] + MU.MINION_W) / 2 then p = i end
    end
    return p
end

-- Keep the insertion preview visible while choosing a Battlecry target.
function MU.StageDrag()
    local stage = MU.dragStage
    if not stage or not MU.sel then return end
    local n = table.getn(MA.game.me.board)
    local xs = MU.RowPositions(n + 1, MU.MINION_W, MU.MINION_GAP, MU.geom.bw, MU.CenterX(MU.geom.bw))
    for i = 1, n do
        local j = i
        if i > stage.position then j = i + 1 end
        local slot = MU.ui.m.minions[i]
        slot.frame:ClearAllPoints()
        slot.frame:SetPoint("TOPLEFT", MU.board, "TOPLEFT", xs[j], slot.home.y)
        slot.dragRect = { x = xs[j], y = slot.rect.y, w = slot.rect.w, h = slot.rect.h }
    end
    stage.slot.frame:SetAlpha(0.35)
    local a = MU.sel.dragAnchor
    local copy = MU.dragPool.copy.frame
    copy:ClearAllPoints()
    copy:SetPoint("CENTER", MU.board, "TOPLEFT", a.x, -a.y)
    copy:Show()
end

function MU.DragArrow(a, x, y, invalid)
    local pool = MU.dragPool
    local dx, dy = x - a.x, y - a.y
    local length = math.sqrt(dx * dx + dy * dy)
    local scale = MU.board:GetEffectiveScale()
    local count = math.min(24, math.max(16, math.floor(length * scale / 18) + 1))
    local bow = math.min(38 / scale, length * 0.12)
    for i = 1, 24 do
        local dot = pool.dots[i]
        if i <= count then
            local t = (i - 1) / count
            MU.PlaceRect(dot, a.x + dx * t - 2 / scale, a.y + dy * t - 4 * bow * t * (1 - t) - 2 / scale,
                4 / scale, 4 / scale)
            dot:Show()
        else dot:Hide() end
    end
    local head = pool.head
    MU.PlaceRect(head, x - 12 / scale, y - 12 / scale, 24 / scale, 24 / scale)
    -- MinimapArrow points up. Inverse UV rotation, corners UL, LL, UR, LR (1.12).
    local tx, ty = dx, dy + 4 * bow
    local len = math.sqrt(tx * tx + ty * ty)
    local c, s = 1, 0
    if len > 0 then c, s = -ty / len, tx / len end
    local function uv(u, v) return 0.5 + c * u + s * v, 0.5 - s * u + c * v end
    local u1, v1 = uv(-0.5, -0.5)
    local u2, v2 = uv(-0.5, 0.5)
    local u3, v3 = uv(0.5, -0.5)
    local u4, v4 = uv(0.5, 0.5)
    head:SetTexCoord(u1, v1, u2, v2, u3, v3, u4, v4)
    if invalid then head:SetVertexColor(1, 0.15, 0.15) else head:SetVertexColor(0.3, 1, 0.3) end
    head:Show()
end

function MU.DragTick(dt)
    local d = MU.drag
    if not d then
        local sel = MU.sel
        if sel and MU.DragAllowed() and sel.seq == MA.game.seq then
            MU.StageDrag()
            local x, y = MU.DragCursor()
            local a = sel.dragAnchor or MU.AnchorOf(sel.source)
            if not a and sel.kind == "p" then a = MU.HandAnchor(sel.source) end
            if x and a then
                local hit = MU.DragHit(x, y)
                MU.DragArrow(a, x, y, hit and not MA.IsValidTarget(sel.entry, hit))
            end
        elseif sel then
            MU.CancelTarget()
        elseif MU.dragPool and (MU.dragPool.head:IsShown() or MU.dragPool.copy.frame:IsShown()) then
            MU.EndDrag()
        end
        return
    end
    if not MU.DragAllowed() or d.seq ~= MA.game.seq or d.mid ~= MA.game.mid then
        MU.CancelTarget()
        return
    end
    local x, y = MU.DragCursor()
    if not x then return end
    d.x, d.y, d.hit = x, y, MU.DragHit(x, y)
    if d.minion and not d.targeting then
        d.position = MU.DragGap(x, y)
        local n = table.getn(MA.game.me.board)
        local xs = MU.RowPositions(n + 1, MU.MINION_W, MU.MINION_GAP, MU.geom.bw, MU.CenterX(MU.geom.bw))
        for i = 1, n do
            local slot = MU.ui.m.minions[i]
            MU.PlaceSlot(slot)
            local goal = slot.home.x
            if d.position then
                local j = i
                if i > d.position then j = i + 1 end
                goal = xs[j]
            end
            local old = d.gapX[i] or slot.home.x
            local blend = dt and math.min(1, dt * 18) or 1
            local at = old + (goal - old) * blend
            d.gapX[i] = at
            slot.frame:ClearAllPoints()
            slot.frame:SetPoint("TOPLEFT", MU.board, "TOPLEFT", at, slot.home.y)
            slot.dragRect = { x = at, y = slot.rect.y, w = slot.rect.w, h = slot.rect.h }
        end
    end
    if d.cardId and not d.targeting then
        local copy = MU.dragPool.copy.frame
        copy:ClearAllPoints()
        copy:SetPoint("CENTER", MU.board, "TOPLEFT", x, -y)
        copy:Show()
    end
    if d.targeting or d.kind == "h" then
        local a = d.anchor or MU.AnchorOf(d.source)
        if a then MU.DragArrow(a, x, y, d.hit and not MA.IsValidTarget(d.entry, d.hit)) end
        local targets = MA.Targets(d.entry)
        for i = 1, table.getn(targets) do
            local target = MU.AnchorOf(targets[i])
            if target and target.slot.glow then
                setGlow(target.slot.glow, target.side == "m" and "green" or "red")
                target.slot.glow.frame:SetAlpha(d.hit == targets[i] and 1 or 0.5)
            end
        end
    end
end

function MU.StartDrag(kind, source, slot, cardId, choice)
    if not MU.DragAllowed(choice) then return false end
    local sel = MU.sel
    if sel and (sel.source ~= source or sel.kind ~= kind) then return false end
    if sel then choice = sel.choice end
    local g = MA.game
    local entry = MA.LegalFor(g, kind, source, choice)
    if not entry then return false end
    local hand = kind == "p" and MA.FindHand(g, source)
    local base = hand and MA.CardBase(hand.cardId) or (cardId and MA.CardBase(cardId))
    -- a Choose One card is dragged like an untargeted spell (no arrow, no gap in the board): its targets depend on the
    -- option, which is chosen in the chooser it opens when it is dropped over the field
    local chooseCard = kind == "p" and not choice and MA.IsChoiceCard(g, source)
    local minion = not chooseCard and base and base.type == "MINION"
    local targeting = not chooseCard and (kind == "a" or (MA.NeedsTarget(entry) and (not minion or sel)))
    MU.EndDrag()
    MU.HandUnpopAll()
    MU.drag = { kind = kind, source = source, entry = entry, seq = g.seq, mid = g.mid,
        slot = slot, alpha = slot.frame:GetAlpha(), cardId = cardId, minion = minion,
        targeting = targeting, choice = choice, position = sel and sel.position, gapX = {},
        anchor = sel and sel.dragAnchor or (cardId and MU.HandAnchor(source) or nil) }
    MU.sel = nil
    -- a dragged option card stays shown (a hidden frame gets no mouse up) while the rest of the chooser is cleared away;
    -- MU.choose stays set, so a drop that plays nothing returns to the chooser
    if choice and MU.choose then MU.Refresh() end
    if cardId then
        slot.frame:SetAlpha(0.35)
        H.CardWidget.SetCard(MU.dragPool.copy.widget, cardId)
    end
    MU.HidePreview()
    MU.DragTick()
    return true
end

function MU.DropDrag(button)
    if button == "RightButton" then MU.CancelTarget() return end
    if button ~= "LeftButton" or not MU.drag then return end
    MU.DragTick()
    local d = MU.drag
    if not d then return end
    local g = MA.game
    local entry = MA.LegalFor(g, d.kind, d.source, d.choice)
    local target = d.hit and MA.IsValidTarget(entry, d.hit) and d.hit or nil
    local above = d.x and d.x >= 0 and d.x <= MU.geom.bw and d.y >= 0 and d.y < MU.PlayLine(MU.geom.bh)
    MU.EndDrag()
    MU.Refresh()
    if not entry then MU.Refresh() return end
    if d.kind == "p" and not d.choice and MA.IsChoiceCard(g, d.source) then
        -- a Choose One card dropped over the field opens the chooser; anywhere else it goes back to the hand
        if above then MU.OpenChooser(d.source) end
        return
    end
    if d.minion and not d.targeting and d.position then
        if MA.NeedsTarget(entry) then
            MU.BeginTarget("p", entry, d.choice)
            MU.sel.position = d.position
            -- Place first, then select the Battlecry target; the wire action is atomic.
            local xs = MU.RowPositions(table.getn(g.me.board) + 1, MU.MINION_W, MU.MINION_GAP, MU.geom.bw, MU.CenterX(MU.geom.bw))
            MU.sel.dragAnchor = { x = xs[d.position + 1] + MU.MINION_W / 2,
                y = MU.RowTop("m", MU.geom.fh) + MU.MINION_H / 2 }
            MU.dragStage = { slot = d.slot, alpha = d.alpha, position = d.position }
            MU.DragTick()
        else MU.Send("play", d.source, 0, d.position, d.choice) end
    elseif d.targeting and target then
        if d.kind == "p" then MU.Send("play", d.source, target, d.position, d.choice)
        elseif d.kind == "a" then MU.Send("atk", d.source, target)
        else MU.Send("hp", target) end
    elseif not d.minion and not d.targeting and above then
        if d.kind == "h" then MU.Send("hp", 0) else MU.Send("play", d.source, 0, nil, d.choice) end
    else MU.sel = nil MU.Refresh() end
end

local function dragHook(slot, kind, index, key)
    local f = slot.frame
    local click = f:GetScript("OnClick")
    f:RegisterForDrag("LeftButton")
    f:SetScript("OnMouseDown", function()
        this.hsDragged = nil
        if arg1 == "RightButton" then MU.CancelTarget() end
    end)
    f:SetScript("OnDragStart", function()
        this.hsDragged = true
        local g = MA.game
        if not g then return end
        local source, cardId, choice
        local actionKind = kind
        if kind == "c" then
            local ch = MU.choose
            if ch then
                source, choice = ch.source, index
                cardId = MA.OptionsOf(g, source)[index]
                actionKind = "p"
            end
        elseif kind == "p" then
            local card = g.hand[index]
            if card then source, cardId = card.id, card.cardId end
        elseif key == "m" then source = slot.entity end
        if source then MU.StartDrag(actionKind, source, slot, cardId, choice) end
    end)
    f:SetScript("OnDragStop", function() MU.DropDrag("LeftButton") end)
    f:SetScript("OnMouseUp", function() MU.DropDrag(arg1) end)
    f:SetScript("OnClick", function()
        if this.hsDragged then this.hsDragged = nil return end
        if click then click() end
    end)
end

function MU.BuildDrag()
    MU.drag = nil
    local f = CreateFrame("Frame", nil, MU.board)
    f:SetAllPoints(MU.board)
    f:SetFrameLevel(MU.board:GetFrameLevel() + MU.FX_LEVEL + 10)
    f:EnableMouse(false)
    local pool = { frame = f, dots = {} }
    for i = 1, 24 do
        local dot = f:CreateTexture(nil, "OVERLAY")
        dot:SetTexture(WHITE)
        dot:SetVertexColor(1, 0.8, 0.2)
        dot:Hide()
        pool.dots[i] = dot
    end
    pool.head = f:CreateTexture(nil, "OVERLAY")
    pool.head:SetTexture("Interface\\Minimap\\MinimapArrow")
    pool.head:Hide()
    local copy = CreateFrame("Frame", nil, f)
    copy:SetWidth(MU.HAND_W)
    copy:SetHeight(MU.HAND_H)
    copy:EnableMouse(false)
    pool.copy = { frame = copy, widget = H.CardWidget.Create(copy, nil, MU.HAND_W, false) }
    pool.copy.widget.frame:SetPoint("CENTER", copy, "CENTER", 0, 0)
    copy:Hide()
    MU.dragPool = pool
    for i = 1, MU.MAX_HAND do dragHook(MU.ui.hand[i], "p", i) end
    for i = 1, MA.MAX_OPTIONS do dragHook(MU.chooser.slots[i], "c", i) end
    local keys = { "m", "o" }
    for k = 1, 2 do
        local key = keys[k]
        local u = MU.ui[key]
        dragHook(u.hero, "a", nil, key)
        dragHook(u.power, "h", nil, key)
        for i = 1, MU.MAX_BOARD do dragHook(u.minions[i], "a", i, key) end
    end
end

function MU.DragCommand(args)
    local mode = string.lower(args or "")
    if mode ~= "" and mode ~= "on" and mode ~= "off" then return "Usage: /hs drag on|off" end
    HearthstoneDB = HearthstoneDB or {}
    H.db = H.db or HearthstoneDB
    if mode ~= "" then H.db.drag = mode == "on" MU.CancelTarget() end
    return "Dragging: " .. (H.db.drag == false and "off (click a card to play it)" or "on (drag a card to play it)") .. ". /hs drag on|off"
end
