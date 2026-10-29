-- Hearthstone addon: Warsong Battlegrounds fight playback (played on the board).
--
-- Plays the human's own combat (BG_COMBAT, pushed before the next BG_STATE) on the board itself, like the game: the
-- shop's pieces step aside, the opponent's hero stands in Bob's arch, the two warbands face each other (theirs above,
-- mine below) and every attack is a lunge of the attacker into its target, with the damage splats, the Divine Shield
-- bursting, the dead fading out and the rest closing ranks, summons and Reborn fading in, buffs glowing. When the last
-- minion falls the winner's hero strikes the loser's for the damage, the outcome shows until Continue and the shop comes
-- back (UI.OnCombatDone) on Continue; Skip (in the End Turn slot) jumps to the result. The other three
-- pairings of the round are not shown. The window keeps showing the board as it was until the fight is over (BgShopUI
-- holds the next state back), so the health the fight starts from is the shown one (HsBgUI.shownModel).
-- Reads the parsed combat from BgState.lua; built and ticked by BgShopUI.lua; drawn with BgVisuals.lua.
-- Lua 5.0 / WoW 1.12.

Hearthstone = Hearthstone or {}
HsBgCombatUI = HsBgCombatUI or {}
local C = HsBgCombatUI
local BG = HsBg
local BP = HsBgProtocol
local V = HsBgVis

-- timing (seconds of the fight's own clock; a long fight runs it faster, C.MAX_TOTAL)
C.INTRO = 0.7                 -- both warbands stand before the first attack
C.ATTACK_PAUSE = 0.24         -- after an attack's return, before the next beat
C.IMPACT = 0.42               -- damage that is not an attack (a Deathrattle, a Start of Combat effect)
C.DEATH = 0.5                 -- the dead fade out, then the others close ranks
C.SUMMON = 0.42
C.BUFF = 0.34
C.TRIGGER = 0.16
C.HERO_ATTACK = 1.0           -- the winner's hero strikes the loser's
C.TALLY_BASE = 0.6            -- the winner's damage shows its base (the tavern tier) this long before the survivors count
C.TALLY_STEP = 0.5            -- each surviving minion lights up this long while the damage grows by its tier
C.RESULT_HOLD = nil           -- seconds before the outcome puts itself away; nil: only Continue does (the button sits in
                              -- the End Turn slot, so a self-closing result turned a late Continue click into End Turn)
C.TRIM_DUR = 1.4              -- how long a trimmed (event-less) log is shown
C.MAX_TOTAL = 45              -- the whole playback takes at most this long (the clock speeds up past it)
C.SLIDE = 0.22
C.SPLAT = 0.85                -- a damage number stays on the hit minion this long
C.OVERLAP = 18                -- pixels an attacker overlaps its target at the hit
C.ANTICIPATE_PX = 8

-- layout (window pixels)
C.ROW_Y = { foe = 206, mine = 326 }  -- tops of the two warbands
C.CENTER_X = 620
C.GAP = 18                    -- between the minions of a warband (the gems reach 10 px beyond a tile)
C.FOE_HERO = { 555, 54 }             -- the opponent's arch, over Bob's
C.SKIP = { 960, 286, 104, 40 }       -- the Skip / Continue button in the End Turn slot

C.BITS = { 1, 2, 4, 8, 16, 32, 64 }

C.frame = nil
C.playing = false
C.finishing = false
C.beats = nil
C.i = 0
C.t = 0
C.side = { [0] = {}, [1] = {} }   -- combat side -> entries in board order
C.byUid = {}
C.grave = {}
C.mySide = 0
C.tiles = { [0] = {}, [1] = {} }  -- combat side -> the drawn tiles in board order
C.actors = {}                     -- uid -> { frame = tile, x, y, w, h, uid, side }
C.pool = {}                       -- every tile made, free ones are hidden and have no .actor

local function fs(parent, font, justify)
    local s = parent:CreateFontString(nil, "OVERLAY", font or "GameFontNormal")
    s:SetJustifyH(justify or "LEFT")
    return s
end

local function bor(a, b)
    local r = 0
    for i = 1, table.getn(C.BITS) do
        local bit = C.BITS[i]
        local qa = math.floor(a / bit)
        local qb = math.floor(b / bit)
        if (qa - math.floor(qa / 2) * 2 == 1) or (qb - math.floor(qb / 2) * 2 == 1) then
            r = r + bit
        end
    end
    return r
end

local function tileW()
    return HsBgUI and HsBgUI.TILE_W or 82
end

local function tileH()
    return HsBgUI and HsBgUI.TILE_H or 100
end

-- ------------------------------------------------------------------ build
function C.Create(parent)
    if C.frame or not parent then
        return C.frame
    end
    local f = CreateFrame("Frame", "HearthstoneBgCombat", parent)
    f:SetAllPoints(parent)
    f:SetFrameLevel(parent:GetFrameLevel() + (HsBgUI and HsBgUI.OVERLAY_BASE or 0) + 30)
    f:EnableMouse(true)                  -- the shop under it takes no clicks while the fight plays
    C.frame = f
    f.title = fs(f, "GameFontNormal", "CENTER")
    f.title:SetPoint("TOP", f, "TOP", 0, -30)
    f.title:SetWidth(600)
    f.foe = V.MakeHero(f, "HearthstoneBgCombatFoe", HsBgUI and HsBgUI.HERO_W or 118)
    V.Raise(f.foe.frame, 1)
    f.foe.frame:EnableMouse(false)
    f.foeActor = { frame = f.foe.frame, x = C.FOE_HERO[1], y = C.FOE_HERO[2], w = f.foe.w, h = f.foe.h }
    f.foeName = fs(f.foe.top, "GameFontNormalSmall", "CENTER")
    f.foeName:SetPoint("TOP", f.foe.frame, "BOTTOM", 0, 2)
    f.foeName:SetWidth(200)
    -- the outcome in the middle of the board
    f.result = CreateFrame("Frame", nil, f)
    f.result:SetFrameLevel(f:GetFrameLevel() + 25)
    f.result:SetWidth(460)
    f.result:SetHeight(40)
    f.result:SetPoint("CENTER", f, "TOPLEFT", C.CENTER_X, -318)
    f.result:SetBackdrop({ bgFile = "Interface\\Buttons\\WHITE8X8", edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
        edgeSize = 14, insets = { left = 3, right = 3, top = 3, bottom = 3 } })
    f.result:SetBackdropColor(0, 0, 0, 0.82)
    f.result:SetBackdropBorderColor(0.85, 0.7, 0.35, 1)
    f.result:Hide()
    f.status = fs(f.result, "GameFontNormalLarge", "CENTER")
    f.status:SetPoint("CENTER", f.result, "CENTER", 0, 0)
    f.status:SetWidth(440)
    -- Skip / Continue in the stone slot at the right edge
    f.skip = CreateFrame("Button", "HearthstoneBgCombatSkip", f)
    f.skip:SetWidth(C.SKIP[3])
    f.skip:SetHeight(C.SKIP[4])
    f.skip:SetPoint("TOPLEFT", f, "TOPLEFT", C.SKIP[1], -C.SKIP[2])
    f.skip:SetFrameLevel(f:GetFrameLevel() + 20)
    -- no plate: the word stands in the stone slot itself, gold, white while the cursor is on it
    f.skip.text = f.skip:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
    f.skip.text:SetFont("Fonts\\FRIZQT__.TTF", 18, "OUTLINE")
    f.skip.text:SetPoint("CENTER", f.skip, "CENTER", 0, 0)
    f.skip.text:SetTextColor(1, 0.82, 0)
    f.skip.text:SetText("Skip")
    f.skip:SetScript("OnClick", function() C.Button() end)
    V.Hover(f.skip, function(b) b.text:SetTextColor(1, 1, 1) end, function(b) b.text:SetTextColor(1, 0.82, 0) end)
    f:Hide()
    return f
end

-- ------------------------------------------------------------------ tiles and their places
local function rowY(side)
    return side == C.mySide and C.ROW_Y.mine or C.ROW_Y.foe
end

-- A free tile of the pool (made on demand), shown for the entry.
local function acquire(entry, side)
    local t = nil
    for i = 1, table.getn(C.pool) do
        if not C.pool[i].actor then
            t = C.pool[i]
            break
        end
    end
    if not t then
        t = HsBgUI.CreateTile(C.frame, "HearthstoneBgCombatTile" .. (table.getn(C.pool) + 1), tileW(), tileH())
        V.Raise(t, C.frame:GetFrameLevel() + 4 - t:GetFrameLevel())  -- with its Divine Shield and gem layers
        table.insert(C.pool, t)
    end
    local a = { frame = t, x = 0, y = rowY(side), w = tileW(), h = tileH(), uid = entry.uid, side = side }
    t.actor = a
    t.float:Hide()
    t:SetAlpha(1)
    HsBgUI.FillTile(t, entry, "combat")
    t:Show()
    C.actors[entry.uid] = a
    return a
end

local function release(uid)
    local a = C.actors[uid]
    if not a then
        return
    end
    C.actors[uid] = nil
    a.frame.actor = nil
    a.frame.hovered = nil
    if a.frame.glow then
        HsBgUI.SetTileGlow(a.frame, nil)
    end
    a.frame:Hide()
end

local function releaseAll()
    for uid in pairs(C.actors) do
        release(uid)
    end
    C.actors = {}
    for i = 1, table.getn(C.pool) do
        C.pool[i].actor = nil
        C.pool[i]:Hide()
    end
end

-- Puts a side's minions at their places in the row (centred), sliding them there when animate.
local function layout(side, animate)
    local list = C.side[side]
    local n = table.getn(list)
    local w = tileW()
    local x0 = C.CENTER_X - (n * w + (n - 1) * C.GAP) / 2
    C.tiles[side] = {}
    for i = 1, n do
        local e = list[i]
        local a = C.actors[e.uid] or acquire(e, side)
        local x, y = x0 + (i - 1) * (w + C.GAP), rowY(side)
        if animate and a.placed then
            V.Slide(a, x, y, C.SLIDE)
        else
            a.x, a.y = x, y
            V.Place(a)
        end
        a.placed = true
        C.tiles[side][i] = a.frame
    end
end

local function redraw(uid)
    local a = C.actors[uid]
    local e = C.byUid[uid]
    if a and e then
        HsBgUI.FillTile(a.frame, e, "combat")
    end
end

-- The damage splat on a minion: a red burst behind a big number that pops and fades.
local function splat(a, text, r, g, b)
    if not a then
        return
    end
    local t = a.frame
    local cx, cy = V.Center(a)
    V.Pulse(cx, cy, a.w * 0.9, a.h * 0.8, r * 0.9, g * 0.5, b * 0.5, 0.45, nil, 0.35)
    t.float:SetFont(V.NUMBER_FONT, 26, "OUTLINE")
    t.float:SetText(text)
    t.float:SetTextColor(r, g, b)
    t.float:SetAlpha(1)
    t.float:Show()
    t.splatAt = (t.splatAt or 0) + 1
    local mark = t.splatAt
    V.Add({ dur = C.SPLAT, kind = "splat",
        update = function(it, u)
            if t.splatAt ~= mark then
                return
            end
            if u > 0.6 then
                t.float:SetAlpha(1 - (u - 0.6) / 0.4)
            end
        end,
        finish = function()
            if t.splatAt == mark then
                t.float:Hide()
                t.float:SetAlpha(1)
            end
        end })
end

-- ------------------------------------------------------------------ the events
local function removeUid(uid)
    local e = C.byUid[uid]
    if not e then
        return
    end
    C.grave[uid] = e
    C.byUid[uid] = nil
    for s = 0, 1 do
        local list = C.side[s]
        for i = 1, table.getn(list) do
            if list[i].uid == uid then
                table.remove(list, i)
                break
            end
        end
    end
end

local function insertAt(side, pos, entry)
    local list = C.side[side]
    local at = pos + 1
    if at < 1 then
        at = 1
    end
    if at > table.getn(list) + 1 then
        at = table.getn(list) + 1
    end
    table.insert(list, at, entry)
    C.byUid[entry.uid] = entry
end

-- Applies one event to the played-back boards (the model only; the beats draw it). Returns the entry it touched.
local function applyEvent(ev)
    local side = ev.side
    if ev.type == 1 then                        -- damage
        local e = C.byUid[ev.target]
        if e then
            e.hp = e.hp - ev.amount
        end
        return e
    elseif ev.type == 2 then                    -- shield pop
        local e = C.byUid[ev.target]
        if e then
            local kw = e.kw or 0
            if math.floor(kw / 2) - math.floor(kw / 4) * 2 == 1 then
                e.kw = kw - 2
            end
        end
        return e
    elseif ev.type == 3 then                    -- death
        removeUid(ev.target)
    elseif ev.type == 4 then                    -- summon
        local e = { id = ev.card or "?", golden = ev.golden, atk = ev.amount, hp = ev.amount2, kw = ev.kw or 0,
            uid = ev.target }
        insertAt(side, ev.position, e)
        return e
    elseif ev.type == 5 then                    -- reborn
        -- the event carries the minion as it came back (its Divine Shield, a Greybough / aura bonus)
        local grave = C.grave[ev.actor]
        local e = { id = ev.card or (grave and grave.id) or "?", golden = ev.golden or (grave and grave.golden) or false,
            atk = ev.amount, hp = ev.amount2, kw = ev.kw or 0, uid = ev.target }
        insertAt(side, ev.position, e)
        return e
    elseif ev.type == 6 then                    -- buff
        local e = C.byUid[ev.target]
        if e then
            e.atk = e.atk + ev.amount
            e.hp = e.hp + ev.amount2
            e.kw = bor(e.kw or 0, ev.kw or 0)
        end
        return e
    end
    return nil
end

-- The hits of an attack or an impact land: health, splats, shield bursts, shakes. lunger: the attacking actor.
local function landHits(hits, lunger)
    local struck = false
    for k = 1, table.getn(hits) do
        local ev = hits[k]
        applyEvent(ev)
        local a = C.actors[ev.target]
        if a then
            redraw(ev.target)
            local cx, cy = V.Center(a)
            if ev.type == 1 then
                splat(a, "-" .. ev.amount, 1, 0.25, 0.2)
                V.Flash(cx, cy, a.w * 0.8, a.h * 0.8, 1, 0.45, 0.15, 0.5, 0.28)
                if a ~= lunger then
                    V.Shake(a)
                end
                struck = true
            elseif ev.type == 2 then
                V.Pulse(cx, cy, a.w * 1.1, a.h * 1.1, 1, 0.9, 0.4, 0.4, nil, 0.9)
                V.Flash(cx, cy, a.w * 0.9, a.h * 0.9, 1, 0.95, 0.6, 0.6, 0.25)
            end
        end
    end
    if struck then
        V.Sound("Sound\\Item\\Weapons\\Axe2H\\m2hAxeHitFlesh1a.wav")
    end
end

-- ------------------------------------------------------------------ beats
-- The log as beats: an attack with the damage / shield events right behind it (they land at the hit), a run of
-- other damage, a run of deaths, of summons, of buffs, of triggers. Every beat keeps its first event as .ev.
local function group(events, i, types)
    local list = {}
    local j = i
    while j <= table.getn(events) and types[events[j].type] do
        table.insert(list, events[j])
        j = j + 1
    end
    return list, j
end

local HITS = { [1] = true, [2] = true }
local PRE_HIT = { [6] = true, [7] = true }

local function buildBeats()
    local c = C.combat
    local beats = {}
    if c.trimmed or table.getn(c.events) == 0 then
        table.insert(beats, { kind = "pause", dur = C.TRIM_DUR })
    else
        table.insert(beats, { kind = "intro", dur = C.INTRO })
        local ev = c.events
        local i = 1
        local n = table.getn(ev)
        local AN = HsMatchAnim
        local attackDur = (AN and AN.ATTACK or 0.46) + C.ATTACK_PAUSE
        while i <= n do
            local e = ev[i]
            local beat
            if e.type == 0 then
                -- the triggers and buffs logged between the attack and its hits (an "on attack" effect, a Secret's
                -- buff) play with the lunge, so the hits still land at the strike instead of a beat or more later
                local pre, j = group(ev, i + 1, PRE_HIT)
                local hits, k = group(ev, j, HITS)
                if table.getn(hits) == 0 then
                    pre = {}
                    hits, k = group(ev, i + 1, HITS)
                end
                beat = { kind = "attack", ev = e, pre = pre, hits = hits, dur = attackDur }
                i = k
            elseif HITS[e.type] then
                local hits, j = group(ev, i, HITS)
                beat = { kind = "impact", ev = e, hits = hits, dur = C.IMPACT }
                i = j
            elseif e.type == 3 then
                local list, j = group(ev, i, { [3] = true })
                beat = { kind = "death", ev = e, list = list, dur = C.DEATH }
                i = j
            elseif e.type == 4 or e.type == 5 then
                local list, j = group(ev, i, { [4] = true, [5] = true })
                beat = { kind = "summon", ev = e, list = list, dur = C.SUMMON }
                i = j
            elseif e.type == 6 then
                local list, j = group(ev, i, { [6] = true })
                beat = { kind = "buff", ev = e, list = list, dur = C.BUFF }
                i = j
            else
                local list, j = group(ev, i, { [7] = true })
                beat = { kind = "trigger", ev = e, list = list, dur = C.TRIGGER }
                i = j
            end
            table.insert(beats, beat)
        end
    end
    if c.winner ~= -1 and (c.damage or 0) > 0 then
        table.insert(beats, { kind = "tally", dur = C.TALLY_BASE })   -- longer once the survivors are known (BeatStart)
        table.insert(beats, { kind = "hero", dur = C.HERO_ATTACK })
    end
    local total = 0
    for i = 1, table.getn(beats) do
        total = total + beats[i].dur
    end
    C.speed = 1
    if total > C.MAX_TOTAL then
        C.speed = total / C.MAX_TOTAL
    end
    return beats
end

-- The attack pose of an attacker t seconds into its lunge (MatchAnimations' curve): -1..0 pulls back, 0..1 travels.
local function pose(t)
    local AN = HsMatchAnim
    if AN and AN.AttackPose then
        return AN.AttackPose(C.poseBeat, t)
    end
    if t < 0.08 then
        return -t / 0.08
    elseif t < 0.24 then
        local u = (t - 0.08) / 0.16
        return -1 + 2 * u * u
    elseif t < 0.3 then
        return 1
    elseif t < 0.48 then
        local u = (t - 0.3) / 0.18
        return (1 - u) * (1 - u)
    end
    return 0
end
C.poseBeat = { reach = 1 }

local function impactAt()
    local AN = HsMatchAnim
    return AN and AN.IMPACT_AT or 0.23
end

-- Starts a lunge of actor a into actor t (rt: the beat's runtime state).
local function startLunge(rt, a, t, level)
    local ax, ay = V.Center(a)
    local tx, ty = V.Center(t)
    local dx, dy = tx - ax, ty - ay
    local dist = math.sqrt(dx * dx + dy * dy)
    if dist < 1 then
        return false
    end
    local ux, uy = dx / dist, dy / dist
    local reach = 1e9
    if math.abs(ux) > 0.001 then
        reach = (a.w + t.w) / 2 / math.abs(ux)
    end
    if math.abs(uy) > 0.001 then
        local r2 = (a.h + t.h) / 2 / math.abs(uy)
        if r2 < reach then
            reach = r2
        end
    end
    local travel = dist - (reach - C.OVERLAP)
    if travel < 0 then
        travel = 0
    end
    rt.lunger, rt.target, rt.ux, rt.uy, rt.travel = a, t, ux, uy, travel
    local want = level or (C.frame:GetFrameLevel() + 14)
    rt.levels = V.Raise(a.frame, math.max(0, want - a.frame:GetFrameLevel()))
    return true
end

local function tickLunge(rt, t)
    local a = rt.lunger
    if not a then
        return
    end
    local p = pose(t)
    local px = p * rt.travel
    if p < 0 then
        px = p * C.ANTICIPATE_PX
    end
    a.lx, a.ly = rt.ux * px, rt.uy * px
    V.Place(a)
end

local function endLunge(rt)
    local a = rt.lunger
    if a then
        a.lx, a.ly = nil, nil
        V.Place(a)
        V.Lower(rt.levels)
        rt.levels = nil
    end
end

-- The hero actors: mine is the shop's (it stays in the bottom arch), the opponent's this frame's.
local function heroActor(side)
    if side == C.mySide then
        return HsBgUI.frame and HsBgUI.frame.heroActor
    end
    return C.frame.foeActor
end

local function heroWidget(side)
    if side == C.mySide then
        return HsBgUI.frame and HsBgUI.frame.hero
    end
    return C.frame.foe
end

-- The tier of a minion entry (its card's; a golden minion counts its normal twin's, as the server's Minion::Tier).
local function minionTier(e)
    local card = BG.Card(e.id)
    return card and card.tier or 0
end

-- The winner's base damage, its tavern tier: mine from the shop's state, an opponent's from its lobby seat; when that is
-- not known (a ghost) the damage the server reported less the survivors' tiers.
local function baseDamage(side, survivors)
    local c = C.combat
    local m = (HsBgUI and HsBgUI.shownModel) or BG.model
    local tier = nil
    if side == C.mySide then
        tier = m and m.tier
    else
        local seat = c.opponent >= 0 and BG.Seat(c.opponent) or nil
        tier = seat and seat.tier
    end
    if tier then
        return tier
    end
    local d = c.damage or 0
    for i = 1, table.getn(survivors) do
        d = d - minionTier(survivors[i])
    end
    return math.max(0, d)
end

-- Shows the winner's damage on its hero (nil hides the gem).
local function showDamage(side, value)
    local w = heroWidget(side)
    if w and w.atk then
        V.SetGem(w.atk, value, "up")
    end
end

-- The loser's hero takes the damage: armor first, then health (the server's Shop::TakeDamage). quiet: no splat (Skip).
local function hurtHero(side, amount, quiet)
    local hp = C.heroHp[side]
    local w = heroWidget(side)
    local a = heroActor(side)
    if not hp or not w or not hp.health then
        return                           -- a ghost has no health to lose
    end
    local fromArmor = math.min(hp.armor, amount)
    hp.armor = hp.armor - fromArmor
    hp.health = hp.health - (amount - fromArmor)
    V.SetHero(w, hp.hero, hp.health, hp.armor, hp.max, false)
    if a and not quiet then
        local cx, cy = V.Center(a)
        V.Pulse(cx, cy, a.w * 0.9, a.h * 0.7, 0.9, 0.15, 0.1, 0.5, nil, 0.4)
        V.Float(cx, cy - 10, "-" .. amount, 1, 0.25, 0.2, 1.2, 34)
        V.Shake(a, 6, 0.3)
    end
end

-- Buffs (applied and glowing, with their +x/+y) and triggers (a glow on the minion whose effect fires), in log order.
local function showBuffs(list)
    for i = 1, table.getn(list) do
        local ev = list[i]
        if ev.type == 6 then
            local e = applyEvent(ev)
            local a = e and C.actors[ev.target]
            if a then
                redraw(ev.target)
                local cx, cy = V.Center(a)
                V.Pulse(cx, cy, a.w, a.h, 1, 0.82, 0.3, 0.35, "glow_oval", 0.4)
                if ev.amount ~= 0 or ev.amount2 ~= 0 then
                    V.Float(cx, a.y + 10, "+" .. ev.amount .. "/+" .. ev.amount2, 0.4, 1, 0.4, 0.8, 18)
                end
            end
        else
            local a = C.actors[ev.actor]
            if a then
                local cx, cy = V.Center(a)
                V.Pulse(cx, cy, a.w * 0.8, a.h * 0.8, 1, 0.85, 0.35, 0.3, "glow_oval", 0.35)
            end
        end
    end
end

function C.BeatStart(beat)
    local rt = {}
    C.rt = rt
    local k = beat.kind
    if k == "attack" then
        showBuffs(beat.pre)                     -- its "whenever this attacks" effects land as it sets off
        local a = C.actors[beat.ev.actor]
        local t = C.actors[beat.ev.target]
        if a then
            HsBgUI.SetTileGlow(a.frame, "gold")
        end
        if t then
            HsBgUI.SetTileGlow(t.frame, "red", 0.9)
        end
        if not (a and t and startLunge(rt, a, t)) then
            rt.noMove = true
        end
    elseif k == "impact" then
        landHits(beat.hits, nil)
    elseif k == "death" then
        rt.fading = {}
        for i = 1, table.getn(beat.list) do
            local a = C.actors[beat.list[i].target]
            if a then
                local cx, cy = V.Center(a)
                V.Pulse(cx, cy, a.w, a.h, 0.55, 0.5, 0.5, 0.4, "glow_oval", 0.3)
                V.Fade(a, true, C.DEATH * 0.7)
                table.insert(rt.fading, beat.list[i].target)
            end
        end
    elseif k == "summon" then
        local news = {}
        local sides = {}
        for i = 1, table.getn(beat.list) do
            local e = applyEvent(beat.list[i])
            if e then
                table.insert(news, { e = e, side = beat.list[i].side, reborn = beat.list[i].type == 5 })
                sides[beat.list[i].side] = true
            end
        end
        for i = 1, table.getn(news) do
            local a = acquire(news[i].e, news[i].side)
            a.alpha = 0
        end
        for s = 0, 1 do
            if sides[s] then
                layout(s, true)
            end
        end
        for i = 1, table.getn(news) do
            local a = C.actors[news[i].e.uid]
            if a then
                V.Fade(a, false, C.SUMMON * 0.8)
                local cx, cy = V.Center(a)
                if news[i].reborn then
                    V.Pulse(cx, cy, a.w, a.h, 1, 0.85, 0.4, 0.5, "glow_oval", 0.5)
                else
                    V.Pulse(cx, cy, a.w, a.h, 0.9, 0.9, 1, 0.45, "glow_oval", 0.5)
                end
            end
        end
    elseif k == "buff" or k == "trigger" then
        showBuffs(beat.list)
    elseif k == "tally" then
        -- the winner's damage: the base first, then each survivor lights up in turn and adds its tier
        local c = C.combat
        local list = {}
        for i = 1, table.getn(C.side[c.winner] or {}) do
            table.insert(list, C.side[c.winner][i])
        end
        rt.list = list
        rt.base = baseDamage(c.winner, list)
        rt.step = 0
        beat.dur = C.TALLY_BASE + table.getn(list) * C.TALLY_STEP
        showDamage(c.winner, math.min(rt.base, c.damage))
    elseif k == "hero" then
        local c = C.combat
        local winSide = c.winner
        local a, t = heroActor(winSide), heroActor(1 - winSide)
        if a and t and startLunge(rt, a, t, C.frame:GetFrameLevel() + 22) then
            rt.hero = true
        else
            hurtHero(1 - winSide, c.damage)
            rt.impacted = true
        end
    end
end

function C.BeatTick(beat, t)
    local rt = C.rt
    if not rt then
        return
    end
    if beat.kind == "tally" then
        local c = C.combat
        local reached = math.min(table.getn(rt.list), math.floor((t - C.TALLY_BASE) / C.TALLY_STEP) + 1)
        if t < C.TALLY_BASE then
            reached = 0
        end
        while rt.step < reached do
            local prev = rt.list[rt.step]
            local pa = prev and C.actors[prev.uid]
            if pa then
                HsBgUI.SetTileGlow(pa.frame, nil)
            end
            rt.step = rt.step + 1
            local e = rt.list[rt.step]
            local a = C.actors[e.uid]
            if a then
                HsBgUI.SetTileGlow(a.frame, "gold")
                local cx, cy = V.Center(a)
                V.Pulse(cx, cy, a.w * 0.8, a.h * 0.8, 1, 0.85, 0.35, 0.3, "glow_oval", 0.35)
            end
            rt.base = rt.base + minionTier(e)
            showDamage(c.winner, math.min(rt.base, c.damage))
        end
    elseif beat.kind == "attack" or beat.kind == "hero" then
        tickLunge(rt, t)
        if not rt.impacted and t >= impactAt() then
            rt.impacted = true
            if beat.kind == "hero" then
                hurtHero(1 - C.combat.winner, C.combat.damage)
            else
                landHits(beat.hits, rt.lunger)
            end
        end
    end
end

function C.BeatEnd(beat)
    local rt = C.rt
    C.rt = nil
    if not rt then
        return
    end
    local k = beat.kind
    if k == "attack" then
        endLunge(rt)
        if not rt.impacted then
            landHits(beat.hits, rt.lunger)
        end
        local a = C.actors[beat.ev.actor]
        local t = C.actors[beat.ev.target]
        if a then
            HsBgUI.SetTileGlow(a.frame, nil)
        end
        if t then
            HsBgUI.SetTileGlow(t.frame, nil)
        end
    elseif k == "tally" then
        for i = 1, table.getn(rt.list) do
            local a = C.actors[rt.list[i].uid]
            if a then
                HsBgUI.SetTileGlow(a.frame, nil)
            end
        end
        showDamage(C.combat.winner, C.combat.damage)       -- the server's number is the one that lands
    elseif k == "hero" then
        endLunge(rt)
        if not rt.impacted then
            hurtHero(1 - C.combat.winner, C.combat.damage)
        end
        showDamage(C.combat.winner, nil)
    elseif k == "death" then
        local sides = {}
        for i = 1, table.getn(beat.list) do
            local ev = beat.list[i]
            sides[ev.side] = true
            applyEvent(ev)
            release(ev.target)
        end
        for s = 0, 1 do
            if sides[s] then
                layout(s, true)
            end
        end
    end
end

-- ------------------------------------------------------------------ playback
local function header()
    local c = C.combat
    local enemy = c.opponent >= 0 and BG.SeatName(c.opponent) or "a ghost"
    C.frame.title:SetText("|cffffd100Round " .. c.round .. "|r  |cffffffff-  your fight against " .. enemy .. "|r")
end

local function outcomeText()
    local c = C.combat
    if c.timedOut or (c.winner == -1 and not c.stalled) then
        return "The fight ran out of turns."
    end
    if c.winner == -1 then
        return "A draw: both warbands fell."
    end
    if c.winner == c.ms then
        return "|cff66ff66Victory!|r  " .. (c.opponent >= 0 and BG.SeatName(c.opponent) or "The ghost") .. " takes "
            .. c.damage .. " damage."
    end
    return "|cffff5555Defeat.|r  You take " .. c.damage .. " damage."
end

-- The heroes' health and armor as the window shows them (before the fight), per combat side.
local function heroNumbers()
    local c = C.combat
    local m = (HsBgUI and HsBgUI.shownModel) or BG.model or BP.EmptyState()
    local out = {}
    out[c.ms] = { hero = m.hero or "", health = m.health or 0, armor = m.armor or 0, max = m.maxHealth or m.health }
    local seat = c.opponent >= 0 and m.lobby and m.lobby[c.opponent + 1] or nil
    if seat then
        out[1 - c.ms] = { hero = seat.hero or "", health = seat.health, armor = seat.armor or 0,
            max = BG.SeatMaxHealth(c.opponent) }
    else
        out[1 - c.ms] = { hero = "", health = nil, armor = 0, max = nil }   -- a ghost
    end
    return out
end

function C.Play(combat)
    if not combat or not HsBgUI or not HsBgUI.frame then
        return
    end
    C.Create(HsBgUI.frame)
    if C.playing or C.finishing then
        C.Hide()
    end
    V.Clear()
    releaseAll()
    C.combat = combat
    C.mySide = combat.ms
    C.playing = true
    C.finishing = false
    C.side = { [0] = {}, [1] = {} }
    C.tiles = { [0] = {}, [1] = {} }
    C.byUid = {}
    C.grave = {}
    C.i = 0
    C.t = 0
    C.hold = 0
    for s = 0, 1 do
        local start = combat.start[s + 1] or {}
        for j = 1, table.getn(start) do
            local e = start[j]
            table.insert(C.side[s], { id = e.id, golden = e.golden, atk = e.atk, hp = e.hp, kw = e.kw, uid = e.uid })
            C.byUid[e.uid] = C.side[s][table.getn(C.side[s])]
        end
    end
    C.heroHp = heroNumbers()
    local foe = C.heroHp[1 - combat.ms]
    V.SetHero(C.frame.foe, foe.hero, foe.health, foe.armor, foe.max, false)
    C.frame.foeName:SetText(combat.opponent >= 0 and ("|cffffffff" .. BG.SeatName(combat.opponent) .. "|r")
        or "|cffb8b8b8Ghost|r")
    V.Place(C.frame.foeActor)
    C.beats = buildBeats()
    V.speed = C.speed or 1
    header()
    C.frame.status:SetText("")
    C.frame.result:Hide()
    C.frame.skip.text:SetText("Skip")
    C.frame:Show()
    HsBgUI.Refresh()                     -- the shop's pieces step aside
    layout(0, false)
    layout(1, false)
    C.StartBeat()
end

function C.StartBeat()
    C.t = 0
    local beat = C.beats[C.i + 1]
    if not beat then
        C.Finish()
        return
    end
    C.BeatStart(beat)
end

function C.Tick(dt)
    if C.finishing and not C.playing then
        C.hold = (C.hold or 0) + (dt or 0)
        if C.RESULT_HOLD and C.hold >= C.RESULT_HOLD then
            C.Close()
        end
        return
    end
    if not C.playing then
        return
    end
    C.t = C.t + (dt or 0) * (C.speed or 1)
    local n = table.getn(C.beats)
    while C.playing and C.i < n do
        local beat = C.beats[C.i + 1]
        if C.t < beat.dur then
            C.BeatTick(beat, C.t)
            return
        end
        C.BeatTick(beat, beat.dur)
        C.t = C.t - beat.dur
        C.BeatEnd(beat)
        C.i = C.i + 1
        if C.i >= n then
            C.Finish()
            return
        end
        C.BeatStart(C.beats[C.i + 1])
    end
end

-- The Skip button: skip while the fight plays, continue to the shop once the result shows.
function C.Button()
    if C.playing then
        C.Skip()
    else
        C.Close()
    end
end

-- Jumps to the end: the remaining beats are not drawn, the survivors and the heroes' numbers are shown at once.
function C.Skip()
    if not C.playing then
        return
    end
    local beat = C.beats[C.i + 1]
    if beat and C.rt then
        C.BeatEnd(beat)
        C.i = C.i + 1
    end
    V.Clear()
    for i = C.i + 1, table.getn(C.beats) do
        local b = C.beats[i]
        if b.kind == "hero" then
            hurtHero(1 - C.combat.winner, C.combat.damage, true)
        end
    end
    C.i = table.getn(C.beats)
    showDamage(0, nil)
    showDamage(1, nil)
    C.Finish()
end

-- The fight is over: the survivors stand, the outcome shows; the shop comes back on Continue (or after C.RESULT_HOLD).
function C.Finish()
    if not C.playing then
        return
    end
    C.playing = false
    C.finishing = true
    C.hold = 0
    V.speed = 1
    local c = C.combat
    if c then
        local s = c.survivors or {}
        C.side = { [0] = {}, [1] = {} }
        C.byUid = {}
        releaseAll()
        for side = 0, 1 do
            local survivors = s[side + 1] or {}
            for j = 1, table.getn(survivors) do
                local e = survivors[j]
                table.insert(C.side[side], { id = e.id, golden = e.golden, atk = e.atk, hp = e.hp, kw = e.kw, uid = e.uid })
                C.byUid[e.uid] = C.side[side][table.getn(C.side[side])]
            end
            layout(side, false)
        end
        C.frame.status:SetText(outcomeText())
        C.frame.result:Show()
    end
    C.frame.skip.text:SetText("Continue")
    C.frame:Show()
end

local function restoreHero()
    if C.rt then
        endLunge(C.rt)                   -- a lunge cut short gives its levels back
        C.rt = nil
    end
    local a = HsBgUI and HsBgUI.frame and HsBgUI.frame.heroActor
    if a then
        a.lx, a.ly, a.sx, a.sy = nil, nil, nil, nil
        V.Place(a)
    end
end

function C.Close()
    C.playing = false
    C.finishing = false
    V.speed = 1
    V.Clear()
    releaseAll()
    restoreHero()
    if C.frame then
        C.frame:Hide()
    end
    V.HideCard()
    if HsBgUI and HsBgUI.OnCombatDone then
        HsBgUI.OnCombatDone()
    end
end

function C.Hide()
    C.playing = false
    C.finishing = false
    V.speed = 1
    V.Clear()
    releaseAll()
    restoreHero()
    if C.frame then
        C.frame:Hide()
    end
end

function C.IsPlaying()
    return C.playing == true or C.finishing == true
end
