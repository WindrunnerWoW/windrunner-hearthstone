-- Hearthstone addon: match animation timeline (part A).
--
-- Pure logic, no WoW frames: turns a transition of the match (HsMatch.TakeTransition(): the game before,
-- the new game and the events between them) into an ordered list of beats, and runs a clock over the beats
-- of the queued plans. MatchUI.lua listens to the clock and draws the beats; the server never waits for any
-- of this and HsMatch.game always stays the newest truth. The UI shows a snapshot that may lag behind it
-- (AN.PresentedGame()) while beats play, and commits the new snapshot when the last beat of a plan ended.
--
-- Beats (tables; every beat has kind, dur (nominal seconds), side ("m"|"o"|"-": the side of its first event),
-- seq (of its first event) and cause (the play / hp / trig / dr event it follows, or nil)):
--   reveal   cardId, secret (true: a Secret that triggered, from rev), source, target. The card is shown large.
--   fatigue  amount, side: the Fatigue card ("Out of cards! Take <amount> damage.") shown large before the damage lands
--   attack   source, target (the entity that is really hit), planned (the target of the atk event), redirected
--            (target ~= planned), fizzle (true: no impact followed: anticipation and back only), hits (list of
--            hit), impactAt (seconds into the beat, nil for a fizzle), reach (share of the way to the contact
--            point that is reached: 1, or AN.FIZZLE_REACH), hero (the attacker is a hero)
--   impact   hits (simultaneous dmg / heal / pop / arm of one resolution step), family, area, cardId, source
--   death    ids (die events, in order), cardIds { [id] = cardId }, sides { [id] = side }, positions
--            { [id] = 1-based board position before the removal, 0 = unknown }
--   weapon_break  id (the weapon entity), cardId: a weapon breaks (the wbrk event, or a weapon that is on the
--            presented board and gone from the new game). The old weapon stays during the beat, which cracks it
--            and lets it fall apart (AN.WEAPON_BREAK); the new game is committed after it. No effect beat.
--   discard  side, items { { id (hand card entity), cardId }, ... } (at most AN.MAX_DISCARDS): cards discarded from a
--            hand (disc events with amount 0) slide out of the hand upwards and fade; the hand is redrawn at the commit
--   effect   family (see AN.FAMILIES), cardId (of the cause), causeType ("play"|"hp"|"trig"|"dr"|nil), source
--            (entity id of the cause; a hand card id for a play), sourceOnBoard, aim (target of the play /
--            hp event, 0 = none), targets (entity ids the results land on, at most AN.MAX_TARGETS), sides
--            { [id] = side }, area (several enemies damaged), slots { [id] = 1-based board position a minion
--            entered, 0 = unknown }, types (the event types of the results), events (the result events),
--            visual (card-specific renderer), flight (its impact time in seconds), tint, fxScale
-- hit: { id, kind ("dmg"|"heal"|"pop"|"arm"), amount, side, source, seq }
-- Damage / healing hits tied to a mapped card also carry effectVisual/effectFlight so only their generic hit flash is suppressed.
--
-- A beat may be shortened by the listener while it starts (beat.dur = x), for example when the frame it
-- needs is not on the board. Beats refer to entity ids only; the listener looks the frames up. Part B
-- (native effects) maps cardId / family to effect profiles; AN.CARD_FAMILY overrides the family guess.
--
-- The clock (AN.Tick) calls listener functions, all optional (AN.SetListener):
--   OnPlanStart(plan)  OnBeatStart(beat, plan)  OnBeatTick(beat, t, plan)  OnBeatEnd(beat, plan)
--   OnPlanEnd(plan)    commit: the UI draws plan.game now
--   OnSnap(reason)     all work was dropped (snap / skip / reset / overflow): draw the newest game now
-- An error in a listener function stops the animations (AN.failed holds the text) and snaps.
--
-- Plan decisions (AN.Plan): a transition is not animated (a snap: the UI shows the newest game at once) when
-- animations are off, it is the first game of a match, a resync (no new events), the event numbers have a gap
-- (events are missing: no story is invented), it has too many events or beats or too much duration, or the
-- match was interrupted. Queue limits (AN.Enqueue): the queued duration and event count are bounded; beyond
-- them everything is dropped and the newest game is shown. A human turn with a Nozdormu timer (g.timer > 0)
-- caps the remaining work to AN.TIMED_CAP seconds by running the clock faster (or snaps when that is too
-- much). A finished match keeps only the beats that explain the end, and never waits long.
--
-- Lua 5.0 / WoW 1.12: no '#', no select(), no '...', no '%'.

HsMatchAnim = HsMatchAnim or {}
local AN = HsMatchAnim
local MA = HsMatch

-- ---------------------------------------------------------------- timing (seconds)
AN.ANTICIPATE = 0.07           -- pull back a few pixels
AN.STRIKE = 0.16               -- across the gap, ease-in
AN.HOLD = 0.06                 -- stay in the overlap
AN.RETURN = 0.17               -- back to the exact start, ease-out
AN.ATTACK = 0.46               -- ANTICIPATE + STRIKE + HOLD + RETURN
AN.IMPACT_AT = 0.23            -- the impact is at the end of the strike
AN.FIZZLE_REACH = 0.3          -- a cancelled attack only goes this far (share of the way)
AN.FIZZLE_STRIKE = 0.08
AN.FIZZLE_RETURN = 0.12
AN.FIZZLE = 0.27               -- ANTICIPATE + FIZZLE_STRIKE + FIZZLE_RETURN
AN.IMPACT = 0.25               -- grouped damage / healing that is not an attack
AN.DEATH = 0.40                -- fade of the dying minions
AN.WEAPON_BREAK = 0.50         -- a weapon breaks: crack flash, then shrinks, falls and fades
AN.DISCARD = 0.60              -- discarded hand cards slide up and fade
AN.MAX_DISCARDS = 5            -- cards shown by one discard beat (the rest just leave with the commit)
AN.EFFECT = 0.35               -- generic effect pulse
AN.REVEAL_IN, AN.REVEAL_HOLD, AN.REVEAL_OUT = 0.15, 1.3, 0.2
AN.REVEAL = 1.65               -- opponent card reveal: in + hold + out
AN.FATIGUE_IN, AN.FATIGUE_OUT = 0.15, 0.2
AN.FATIGUE = 1.35              -- the Fatigue card before the damage number: in + hold + out
AN.SECRET_HOLD = 0.9
AN.SECRET = 1.25               -- a Secret that triggers: in + short hold + out
AN.FLARE_REVEAL = 0.45         -- public Flare removals stay readable without pushing five secrets over the plan cap
AN.NUMBER_FADE = 0.6           -- floating numbers (the UI uses these two)
AN.NUMBER_RISE = 25            -- pixels

-- ---------------------------------------------------------------- bounds
AN.MAX_BEATS = 24              -- beats of one plan
AN.MAX_PLAN = 6.0              -- seconds of one plan
AN.MAX_EVENTS = 80             -- events of one transition
AN.MAX_TOTAL = 8.0             -- seconds of queued work (the running plan's rest and the queued plans)
AN.MAX_QUEUE_EVENTS = 120      -- events of the queued plans
AN.MAX_TARGETS = 8             -- targets of one effect beat
AN.TIMED_CAP = 0.5             -- Nozdormu turn: the remaining work takes about this long
AN.TIMED_SNAP = 2.0            -- more remaining work than this at the start of a timed turn: skipped
AN.OVER_CAP = 3.0              -- a finished match: seconds before the result
AN.MAX_DT = 0.25               -- one tick never advances more than this (a frame hitch must not skip a beat)
AN.FAST = 3                    -- /hs anim fast
AN.FAMILIES = { "fire", "frost", "arcane", "shadow", "holy", "nature", "heal", "area", "buff", "silence", "transform",
    "summon", "destroy", "secret", "generic" }

-- Part B (MatchFx.lua, from MatchFxProfiles.lua) fills this: cardId -> family (overrides the guess from the events).
AN.CARD_FAMILY = AN.CARD_FAMILY or {}

AN.mode = AN.mode or "on"      -- "on" | "off" (always snap) | "fast" (3x speed)
AN.listener = AN.listener or nil
AN.failed = nil                -- the text of a listener error (animations are off until a reload)
AN.timedScale = 1

local queue = {}               -- plans waiting
local cur = nil                -- the running plan: { plan, i (index of the beat started last), beat, t }
local gen = 0                  -- counts clear-alls: a listener that resets the clock is noticed

-- ---------------------------------------------------------------- mode
function AN.SetMode(mode)
    if mode == "on" or mode == "off" or mode == "fast" then
        AN.mode = mode
        return true
    end
    return false
end

function AN.Mode()
    return AN.mode
end

-- Clock speed of the mode (the UI runs its floating numbers with it too).
function AN.Speed()
    if AN.mode == "fast" then
        return AN.FAST
    end
    return 1
end

-- Clock speed including the acceleration of a timed turn.
function AN.Scale()
    return AN.Speed() * (AN.timedScale or 1)
end

function AN.SetListener(l)
    AN.listener = l
end

-- ---------------------------------------------------------------- entities
-- What an entity id is on the board of a game: "hero", "minion", "power" or "weapon", the side, and for a
-- minion its 1-based position.
local function kindOf(g, id)
    if not g or not id or id == 0 then
        return nil
    end
    local sides = { m = g.me, o = g.opp }
    for _, key in ipairs({ "m", "o" }) do
        local s = sides[key]
        if s.hero.id == id then
            return "hero", key
        end
        if s.power and s.power.id == id then
            return "power", key
        end
        if s.weapon and s.weapon.id == id then
            return "weapon", key
        end
        for i = 1, table.getn(s.board) do
            if s.board[i].id == id then
                return "minion", key, i
            end
        end
    end
    return nil
end
AN.KindOf = kindOf

-- Replacing an equipped weapon updates the slot directly; the outgoing weapon does not get a break animation.
local function weaponWasReplaced(prev, game, id)
    local _, side = kindOf(prev, id)
    if not side then
        return false
    end
    local before, after
    if side == "m" then
        before = prev.me
        after = game and game.me
    else
        before = prev.opp
        after = game and game.opp
    end
    return before and before.weapon and before.weapon.id == id and after and after.weapon and after.weapon.id ~= id
end

-- ---------------------------------------------------------------- events -> beats
local HIT_KIND = { dmg = "dmg", heal = "heal", pop = "pop", arm = "arm", fat = "dmg" }
local CAUSES = { play = true, hp = true, trig = true, dr = true }
-- events that start another action or step: a run of results never reaches over them
local BOUNDS = { atk = true, turn = true, ["end"] = true, mull = true, rev = true }
-- result events that do something to their target but are no damage (the effect beat shows them)
local RESULTS = { frz = true, sil = true, tf = true, buff = true, dest = true, wbrk = true, sum = true,
    sec = true, bnc = true, ctl = true, eq = true, stl = true, draw = true, disc = true, crys = true,
    mana = true, gen = true, burn = true }
local RESOURCE_RESULTS = { draw = true, disc = true, crys = true, mana = true, gen = true, burn = true }
local SPECIAL_CASTS = { CS2_038 = true, CS2_063 = true, CS2_074 = true, EX1_409 = true, EX1_544 = true }

local function hitOf(e)
    return { id = e.target, kind = HIT_KIND[e.type], amount = e.amount, side = e.side, source = e.source, seq = e.seq }
end

-- Per-card presentation metadata is generated from data/fx/effect_profiles.json. The profiles file loads
-- after this module, but plans are built only after the addon finishes loading.
local function cardEffect(cardId)
    local profiles = HS_EffectProfiles
    local cards = type(profiles) == "table" and profiles.cards or nil
    local c = type(cards) == "table" and cardId and cards[cardId] or nil
    if type(c) ~= "table" or type(c.visual) ~= "string" or type(c.flight) ~= "number"
        or c.flight ~= c.flight or c.flight < 0.1 or c.flight > 1 then
        return nil
    end
    return c
end

-- Choose One play events retain the base card id; amount carries the selected option (1 or 2).
-- The option profile is presentation metadata only. The event and public reveal keep the base identity.
local function effectCardId(e)
    if not e or e.type ~= "play" or not e.cardId or not e.amount or e.amount < 1 or e.amount > 2 then
        return e and e.cardId or nil
    end
    local profiles = HS_EffectProfiles
    local cards = type(profiles) == "table" and profiles.cards or nil
    local profile = type(cards) == "table" and cards[e.cardId] or nil
    local choices = profile and profile.choices
    local option = type(choices) == "table" and choices[e.amount] or nil
    return type(option) == "string" and option or e.cardId
end

local function eventCardEffect(e)
    return cardEffect(effectCardId(e))
end

local function mappedSpecialResult(causeEvent, resultType)
    if resultType ~= "eq" and resultType ~= "stl" then return true end
    local profile = causeEvent and CAUSES[causeEvent.type] and eventCardEffect(causeEvent) or nil
    return profile and profile.resultVisuals and profile.resultVisuals[resultType] ~= nil or false
end

local function addUnique(list, id)
    for i = 1, table.getn(list) do
        if list[i] == id then
            return false
        end
    end
    table.insert(list, id)
    return true
end

-- Family of an effect from the result events (types: a set of event types, damaged: number of different
-- damaged targets). The card family table of part B wins.
local function guessFamily(cardId, types, damaged)
    local f = cardId and AN.CARD_FAMILY[cardId]
    if f then
        return f
    end
    if types.sec then
        return "secret"
    elseif damaged >= 2 then
        return "area"
    elseif types.frz then
        return "frost"
    elseif types.sil then
        return "silence"
    elseif types.tf then
        return "transform"
    elseif types.dest or types.wbrk then
        return "destroy"
    elseif types.sum then
        return "summon"
    elseif types.heal then
        return "heal"
    elseif types.buff or types.arm then
        return "buff"
    end
    return "generic"
end

-- The beats of the events of a transition (prev: the game before; used to tell which entities are on
-- the board that is presented while the beats play).
function AN.Build(events, prev, game)
    local n = table.getn(events)
    local beats = {}
    local consumed, covered = {}, {}
    local cause = nil
    local causeFamily = nil       -- the family of the effect beat of the current cause (its impact beats follow it)
    local causeResultVisuals = nil
    local specialPlay = nil       -- public special cast retained only for actual same-source result markers
    local activePlay = nil        -- public play identity retained across intervening triggers for exact-source results
    local summonMeta = {}          -- actual entity -> public side/slot from an earlier sum in this transition
    local process

    local function matchesSpecialResult(e)
        if not specialPlay or not cardEffect(specialPlay.cardId) then return false end
        local p = cardEffect(specialPlay.cardId)
        if e.type == "eq" then
            return specialPlay.cardId == "EX1_409" and e.side == specialPlay.side and e.cardId == "EX1_409t"
                and p.resultVisuals and p.resultVisuals.eq ~= nil
        end
        if e.type == "buff" or e.type == "stl" then
            return e.source ~= 0 and e.source == specialPlay.source and p.resultVisuals
                and p.resultVisuals[e.type] ~= nil
        end
        return false
    end

    local function add(b, e)
        b.seq = e and e.seq or 0
        b.cause = cause
        b.side = b.side or (e and e.side) or "-"
        table.insert(beats, b)
    end

    -- the result events after index i up to the next cause / bound (neutral events do not end the run)
    local function runAfter(i)
        local list = {}
        local j = i + 1
        while j <= n do
            local t = events[j].type
            if CAUSES[t] or BOUNDS[t] then
                break
            end
            if activePlay and events[i] ~= activePlay and events[j].source ~= 0
                and events[j].source == activePlay.source and (RESULTS[t] or HIT_KIND[t]) then
                break -- leave the original play's exact-source result for its own later beat
            end
            if matchesSpecialResult(events[j]) and events[i] ~= specialPlay then
                break
            end
            if (RESULTS[t] or HIT_KIND[t]) and t ~= "wbrk" and not consumed[j]
                and mappedSpecialResult(events[i], t) then
                table.insert(list, j)         -- a broken weapon has its own beat (weapon_break), no effect pulse
            end
            j = j + 1
        end
        return list
    end

    -- one effect beat for the result events (indexes) of a cause (e may be nil: results without a cause)
    local function effectBeat(e, idxs, markCovered, sequenceEvent)
        local targets, sides, slots, evs, types, equipments = {}, {}, {}, {}, {}, {}
        local damaged = {}
        local ndmg = 0
        local resourceResult = false
        local playedSummon = false
        local first = e
        for k = 1, table.getn(idxs) do
            local x = events[idxs[k]]
            first = first or x
            local skip = false
            if x.type == "sum" and x.amount >= 1 then
                slots[x.target] = x.amount
                summonMeta[x.target] = { side = x.side, slot = x.amount }
            end
            if RESOURCE_RESULTS[x.type] then
                resourceResult = true
                types[x.type] = true
                skip = true -- draw/discard identities are private and are never effect targets
            end
            if x.type == "sum" and e and e.type == "play" and x.target == e.source
                and x.cardId == e.cardId and x.side == e.side then
                skip = true               -- the minion that was played itself: nothing to point at
                playedSummon = true
            end
            if x.type == "eq" and e and e.type == "play" and x.target == e.source
                and x.cardId == e.cardId and x.side == e.side then
                local card = HS_CardDB and HS_CardDB.cards and HS_CardDB.cards[e.cardId]
                if card and card.type == "WEAPON" then skip = true end
            end
            if markCovered and RESULTS[x.type] then
                covered[idxs[k]] = true
            end
            if not skip then
                types[x.type] = true
                if x.type == "dmg" or x.type == "fat" then
                    if addUnique(damaged, x.target) then
                        ndmg = ndmg + 1
                    end
                end
                if x.target ~= 0 then
                    -- Keep metadata for every eligible target. The capped visual subset is chosen below,
                    -- after result visuals are known, so mixed area spells can represent each outcome.
                    sides[x.target] = x.side
                    addUnique(targets, x.target)
                end
                if x.type == "eq" and x.target ~= 0 then
                    equipments[x.target] = true
                end
                table.insert(evs, x)
            end
        end
        -- Later actual results (for example a redirected spell marker) can target an entity introduced by an
        -- earlier sum in this same event stream. Carry only that public entity's emitted side and slot forward.
        for k = 1, table.getn(targets) do
            local id = targets[k]
            local known = summonMeta[id]
            if known then
                if slots[id] == nil then slots[id] = known.slot end
                if sides[id] == nil then sides[id] = known.side end
            end
        end
        local onBoard = false
        if e and e.source ~= 0 and kindOf(prev, e.source) then
            onBoard = true
        end
        local causeType = e and CAUSES[e.type] and e.type or nil
        local cardId = e and CAUSES[e.type] and e.cardId or nil
        local visualProfile = causeType ~= "hp" and eventCardEffect(e) or nil
        if causeType == "play" then cardId = effectCardId(e) end
        if causeType == "trig" and visualProfile then
            local mapped = false
            for k = 1, table.getn(evs) do
                if visualProfile.resultVisuals and visualProfile.resultVisuals[evs[k].type] then mapped = true end
            end
            if not mapped and not (visualProfile.visual == "resource" and resourceResult) then
                visualProfile, cardId = nil, nil
            end
        end
        local fallbackResultVisuals = nil
        if not visualProfile and e == nil and types.dest then
            -- A deferred destruction after a turn boundary has no trustworthy card cause.
            visualProfile = { visual = "destroy", resultVisuals = { dest = "destroy" } }
        elseif causeType == "dr" and table.getn(evs) > 0 then
            -- A dead minion's profile applies only to result types it explicitly maps. Other emitted
            -- outcomes use a generic result visual, never the dead card's main spell visual.
            fallbackResultVisuals = { sum = "summon", dest = "destroy", buff = "buff", dmg = "impact",
                fat = "impact", heal = "heal", pop = "impact", arm = "shield", frz = "freeze", sil = "silence",
                tf = "transform", bnc = "bounce", ctl = "control", eq = "buff", stl = "silence", sec = "secret" }
            local mapped = false
            local firstVisual = nil
            for k = 1, table.getn(evs) do
                local x = evs[k]
                local mappedVisual = visualProfile and visualProfile.resultVisuals
                    and visualProfile.resultVisuals[x.type]
                if mappedVisual then mapped = true end
                if not firstVisual then firstVisual = mappedVisual or fallbackResultVisuals[x.type] end
            end
            if not mapped then cardId = nil end
            local resultVisuals = {}
            for resultType, resultVisual in pairs(fallbackResultVisuals) do resultVisuals[resultType] = resultVisual end
            if visualProfile and visualProfile.resultVisuals then
                for resultType, resultVisual in pairs(visualProfile.resultVisuals) do
                    resultVisuals[resultType] = resultVisual
                end
            end
            local mainVisual = types.sum and "summon" or (mapped and visualProfile.visual or firstVisual)
            visualProfile = { visual = mainVisual or "generic", flight = mapped and visualProfile.flight or nil,
                resultVisuals = resultVisuals }
        end
        -- Secret resolution is anonymous unless the cause is the known secret placement.
        local genericSecret = types.sec and not (causeType == "play" and visualProfile
            and visualProfile.visual == "secret")
        if genericSecret then
            cardId = nil
            visualProfile = { visual = "secret", flight = AN.SECRET }
        end
        local visual = visualProfile and visualProfile.visual
        local flight = visualProfile and visualProfile.flight
        local family = genericSecret and "secret" or guessFamily(cardId, types, ndmg)
        local targetVisuals = {}
        if visualProfile then
            for k = 1, table.getn(evs) do
                local x = evs[k]
                if x.target and x.target ~= 0 and not targetVisuals[x.target] then
                    local resultVisual = visualProfile.resultVisuals and visualProfile.resultVisuals[x.type]
                    if resultVisual or (causeType ~= "trig" and causeType ~= "dr") then
                        targetVisuals[x.target] = resultVisual or visualProfile.visual
                    end
                end
            end
        end
        if table.getn(targets) > AN.MAX_TARGETS then
            if visualProfile and visualProfile.resultVisuals then
                -- Under the existing target cap, take a deterministic round-robin sample across the
                -- result-visual groups in first-appearance order (e.g. Nova damage and healing).
                local groups, groupByVisual = {}, {}
                for k = 1, table.getn(targets) do
                    local id = targets[k]
                    local visual = targetVisuals[id] or visualProfile.visual
                    local group = groupByVisual[visual]
                    if not group then
                        group = { visual = visual, ids = {}, cursor = 1 }
                        groupByVisual[visual] = group
                        table.insert(groups, group)
                    end
                    table.insert(group.ids, id)
                end
                local selected, progressed = {}, true
                while table.getn(selected) < AN.MAX_TARGETS and progressed do
                    progressed = false
                    for k = 1, table.getn(groups) do
                        local group = groups[k]
                        if table.getn(selected) < AN.MAX_TARGETS and group.cursor <= table.getn(group.ids) then
                            table.insert(selected, group.ids[group.cursor])
                            group.cursor = group.cursor + 1
                            progressed = true
                        end
                    end
                end
                targets = selected
            else
                while table.getn(targets) > AN.MAX_TARGETS do
                    table.remove(targets)
                end
            end
        end
        local playType = cardId and HS_CardDB and HS_CardDB.cards and HS_CardDB.cards[cardId]
            and HS_CardDB.cards[cardId].type
        local allowTargetless = (visualProfile and visualProfile.visual == "resource"
            and (resourceResult or (causeType == "play" and playType == "SPELL")))
            or (causeType == "play" and visualProfile and visualProfile.cueOnPlay and playedSummon)
        allowTargetless = allowTargetless or (causeType == "play" and visualProfile
            and visualProfile.visual == "secret" and table.getn(evs) == 0)
        allowTargetless = allowTargetless or genericSecret
        if table.getn(targets) == 0 then
            -- nothing to point at: a hero power or ability still pulses on its own entity
            if not allowTargetless then
                return false
            end
        end
        if e and CAUSES[e.type] then
            causeFamily = family
        end
        if causeType == "hp" then
            -- a hero power plays no effect of its own: the power just turns over; its results (damage
            -- numbers, a summoned minion ...) show as for any other cause
            return false
        end
        local isArea = ndmg >= 2 or visual == "nova" or visual == "rain"
        add({ kind = "effect", dur = flight or AN.EFFECT, family = family, cardId = cardId, visual = visual,
            flight = flight, tint = visualProfile and visualProfile.tint, fxScale = visualProfile and visualProfile.fxScale,
            causeType = causeType,
            source = e and CAUSES[e.type] and e.source or 0, sourceOnBoard = onBoard,
            aim = (e and CAUSES[e.type]) and e.target or 0, targets = targets, sides = sides, area = isArea,
            slots = slots, equipments = equipments, types = types, events = evs, targetVisuals = targetVisuals,
            side = e and e.side or (first and first.side) or "-" }, sequenceEvent or first)
        return true
    end

    local function doCause(i)
        local e = events[i]
        cause = e
        causeFamily = nil
        causeResultVisuals = nil
        if e.type == "play" and e.side == "o" and e.cardId then
            add({ kind = "reveal", dur = AN.REVEAL, cardId = e.cardId, secret = false, source = e.source,
                target = e.target, side = "o" }, e)
        end
        if e.type == "play" then activePlay = e end
        local profile = e.type ~= "hp" and e.type ~= "dr" and eventCardEffect(e) or nil
        -- Volley cards are a sequence of projectiles, not one aggregate cast on their whole result set.
        if not profile or profile.visual ~= "volley" then
            local idxs = runAfter(i)
            local scheduled = effectBeat(e, idxs, true)
            if e.type == "dr" and scheduled then
                causeResultVisuals = { sum = "summon", dest = "destroy", buff = "buff", dmg = "impact",
                    fat = "impact", heal = "heal", arm = "shield" }
            end
        end
    end

    local function doAttack(i)
        local e = events[i]
        local S, T = e.source, e.target
        -- the window of the attack: up to the next action; its impact is the first dmg / pop of the attacker
        local impactIdx = nil
        local j = i + 1
        local winEnd = n
        while j <= n do
            local x = events[j]
            local t = x.type
            if t == "atk" or t == "turn" or t == "end" or t == "mull" or t == "play" or t == "hp" then
                winEnd = j - 1
                break
            end
            if impactIdx == nil and (t == "dmg" or t == "pop") and x.source == S and not consumed[j] then
                impactIdx = j
            end
            j = j + 1
        end
        local impactTarget = T
        local hits = {}
        local combat = {}
        if impactIdx then
            impactTarget = events[impactIdx].target
            for k = impactIdx, winEnd do
                local x = events[k]
                if (x.type == "dmg" or x.type == "pop") and not consumed[k]
                    and ((x.source == S and x.target == impactTarget) or (x.source == impactTarget and x.target == S)) then
                    table.insert(combat, k)
                end
            end
        else
            -- no damage from the attacker: a counter-hit of the defender still shows the attack connected
            for k = i + 1, winEnd do
                local x = events[k]
                if (x.type == "dmg" or x.type == "pop") and not consumed[k] and x.source == T and x.target == S then
                    table.insert(combat, k)
                end
            end
        end
        for k = 1, table.getn(combat) do
            consumed[combat[k]] = true
            table.insert(hits, hitOf(events[combat[k]]))
        end
        local attackerThere = kindOf(prev, S) ~= nil
        if impactIdx then
            -- what happened before the impact (a Secret revealing and redirecting) is shown first
            process(i + 1, impactIdx - 1)
        end
        local hero = kindOf(prev or game, S) == "hero"
        if table.getn(combat) > 0 and attackerThere then
            add({ kind = "attack", dur = AN.ATTACK, source = S, target = impactTarget, planned = T,
                redirected = impactTarget ~= T, fizzle = false, hits = hits, impactAt = AN.IMPACT_AT, reach = 1,
                hero = hero }, e)
        elseif table.getn(combat) > 0 then
            add({ kind = "impact", dur = AN.IMPACT, hits = hits, source = S, cardId = nil }, e)
        else
            add({ kind = "attack", dur = AN.FIZZLE, source = S, target = T, planned = T, redirected = false,
                fizzle = true, hits = {}, impactAt = nil, reach = AN.FIZZLE_REACH, hero = hero }, e)
        end
        -- the rest of the window (triggers, deaths ...) is handled in order by the caller
        return impactIdx and impactIdx or i
    end

    process = function(from, to)
        local i = from
        while i <= to do
            local e = events[i]
            local t = e.type
            if consumed[i] then
                i = i + 1
            elseif t == "atk" then
                cause, causeFamily, causeResultVisuals, specialPlay, activePlay = nil, nil, nil, nil, nil
                local after = doAttack(i)
                if after > i then
                    -- the events before the impact were processed inside; continue behind the impact
                    i = after + 1
                else
                    i = i + 1
                end
            elseif CAUSES[t] then
                if t == "play" then
                    specialPlay = e.cardId and SPECIAL_CASTS[e.cardId] and e.source ~= 0 and e or nil
                elseif t == "hp" then
                    specialPlay = nil
                    activePlay = nil
                end
                doCause(i)
                i = i + 1
            elseif t == "rev" then
                cause, causeFamily, causeResultVisuals = nil, nil, nil
                local flare = specialPlay and specialPlay.cardId == "EX1_544" and e.source == specialPlay.source
                    and cardEffect(specialPlay.cardId)
                if flare then
                    -- Flare only removes the other player's secrets; malformed/own-side events reveal nothing.
                    local enemySecret = (specialPlay.side == "m" and e.side == "o")
                        or (specialPlay.side == "o" and e.side == "m")
                    if enemySecret then
                        -- The server made this identity public; keep its reveal and add a concise removal cue.
                        add({ kind = "reveal", dur = AN.FLARE_REVEAL, cardId = e.cardId, secret = true, source = e.source,
                            target = e.target, side = e.side }, e)
                    end
                    if enemySecret and e.target ~= 0 and (e.side == "m" or e.side == "o") then
                        local targetVisuals, sides, heroTargets = {}, {}, {}
                        targetVisuals[e.target], sides[e.target], heroTargets[e.target] = "silence", e.side, true
                        add({ kind = "effect", dur = flare.flight or AN.EFFECT, family = "silence", visual = "silence",
                            cardId = nil, source = 0, aim = 0, targets = { e.target }, sides = sides,
                            heroTargets = heroTargets, targetVisuals = targetVisuals, types = { rev = true }, events = { e },
                            side = e.side }, e)
                    end
                    -- Same-source public secret removals belong to Flare; keep only this whitelisted cast context.
                    i = i + 1
                else
                    if specialPlay and e.source ~= specialPlay.source then specialPlay = nil end
                    -- Ordinary secret reveals keep their existing full trigger/reveal timing.
                    if e.cardId then
                        add({ kind = "reveal", dur = AN.SECRET, cardId = e.cardId, secret = true, source = e.source,
                            target = e.target, side = e.side }, e)
                    end
                    i = i + 1
                end
            elseif BOUNDS[t] then
                -- Turn boundaries end any active resolution context, including a volley with no later hit.
                cause, causeFamily, causeResultVisuals, specialPlay, activePlay = nil, nil, nil, nil, nil
                i = i + 1
            elseif HIT_KIND[t] then
                -- a run of simultaneous hits: one step of the resolution
                if t == "fat" then
                    add({ kind = "fatigue", dur = AN.FATIGUE, amount = e.amount, side = e.side }, e)
                end
                local hits = {}
                local types, damaged = {}, {}
                local j = i
                local effectiveCause = cause
                local recoverPlay = activePlay and cause ~= activePlay and events[i].source ~= 0
                    and events[i].source == activePlay.source
                if recoverPlay then effectiveCause = activePlay end
                local volleyProfile = effectiveCause and effectiveCause.type ~= "hp" and CAUSES[effectiveCause.type]
                    and eventCardEffect(effectiveCause) or nil
                local volley = volleyProfile and volleyProfile.visual == "volley"
                local hitEnd = volley and (i + 1) or (to + 1)
                local recoveredIndexes = {}
                while j <= to and j < hitEnd and (HIT_KIND[events[j].type] and not consumed[j]) do
                    local x = events[j]
                    if activePlay and cause ~= activePlay then
                        local belongsToPlay = x.source ~= 0 and x.source == activePlay.source
                        if (recoverPlay and not belongsToPlay) or (not recoverPlay and belongsToPlay) then
                            break -- a source change starts a distinct hit resolution group
                        end
                    end
                    local hitCause = cause
                    if recoverPlay then
                        -- Recover the exact public source, then keep its full hit group (or volley) intact.
                        hitCause = activePlay
                        table.insert(recoveredIndexes, j)
                    end
                    local scheduledVolley = false
                    local hitProfile = hitCause and hitCause.type ~= "hp" and CAUSES[hitCause.type]
                        and eventCardEffect(hitCause) or nil
                    if volley and hitCause == effectiveCause then
                        -- Preserve event order and repeat targets: each actual hit gets its own arrival beat.
                        scheduledVolley = effectBeat(effectiveCause, { j }, false, x) == true
                    end
                    local h = hitOf(x)
                    local profile = hitProfile
                    if profile and profile.visual == "volley" and scheduledVolley and (h.kind == "dmg" or h.kind == "pop") then
                        h.effectVisual = "volley"
                        h.effectFlight = profile.flight
                    elseif profile and profile.visual ~= "secret" and profile.visual ~= "volley"
                        and (h.kind == "dmg" or h.kind == "heal")
                        and (hitCause.type ~= "trig" and hitCause.type ~= "dr"
                            or (profile.resultVisuals and profile.resultVisuals[x.type])) then
                        local resultVisual = profile.resultVisuals and profile.resultVisuals[x.type]
                        if resultVisual or (hitCause.type ~= "trig" and hitCause.type ~= "dr") then
                            h.effectVisual = resultVisual or profile.visual
                        end
                        h.effectFlight = profile.flight
                    elseif hitCause == cause and causeResultVisuals and (h.kind == "dmg" or h.kind == "heal")
                        and causeResultVisuals[x.type] then
                        h.effectVisual = causeResultVisuals[x.type]
                    elseif hitCause == cause and cause and cause.type == "dr" and (h.kind == "dmg" or h.kind == "heal") then
                        h.effectVisual = h.kind == "heal" and "heal" or "impact"
                    end
                    table.insert(hits, h)
                    types[HIT_KIND[x.type]] = true
                    if HIT_KIND[x.type] == "dmg" then
                        addUnique(damaged, x.target)
                    end
                    consumed[j] = true
                    j = j + 1
                end
                if table.getn(recoveredIndexes) > 0 and not volley then
                    -- Group all exact-source results into one arrival effect before their simultaneous numbers.
                    local savedFamily = causeFamily
                    effectBeat(activePlay, recoveredIndexes, false, events[recoveredIndexes[1]])
                    causeFamily = savedFamily
                end
                local impactCause = cause
                if activePlay and cause ~= activePlay then
                    for k = 1, table.getn(hits) do
                        if hits[k].source == activePlay.source then impactCause = activePlay; break end
                    end
                end
                local cardId = impactCause and CAUSES[impactCause.type] and effectCardId(impactCause) or nil
                add({ kind = "impact", dur = AN.IMPACT, hits = hits, family = (impactCause == cause and causeFamily)
                    or guessFamily(cardId, types,
                    table.getn(damaged)), area = table.getn(damaged) >= 2, cardId = cardId,
                    source = impactCause and impactCause.source or 0 }, e)
                i = j
            elseif t == "die" then
                local ids, cardIds, sides, positions = {}, {}, {}, {}
                local j = i
                while j <= to and events[j].type == "die" do
                    local x = events[j]
                    if addUnique(ids, x.target) then
                        summonMeta[x.target] = nil
                        cardIds[x.target] = x.cardId
                        sides[x.target] = x.side
                        positions[x.target] = x.amount >= 1 and x.amount or 0
                    end
                    j = j + 1
                end
                add({ kind = "death", dur = AN.DEATH, ids = ids, cardIds = cardIds, sides = sides,
                    positions = positions }, e)
                i = j
            elseif t == "cnt" then
                if specialPlay and e.source == specialPlay.source then specialPlay = nil end
                if activePlay and e.source == activePlay.source then activePlay = nil end
                i = i + 1
            elseif t == "wbrk" then
                -- a weapon breaks: the old weapon stays on screen while it cracks and falls apart (only a weapon
                -- that was on the presented board has anything to show)
                if (not prev or kindOf(prev, e.target) == "weapon") and not weaponWasReplaced(prev, game, e.target) then
                    add({ kind = "weapon_break", dur = AN.WEAPON_BREAK, id = e.target, cardId = e.cardId,
                        side = e.side }, e)
                end
                i = i + 1
            elseif t == "disc" and e.amount == 0 and e.target ~= 0 and e.cardId and prev then
                -- cards discarded from a hand: one beat for the run of the same side. Only cards that are in the
                -- presented hand count (the opponent's hand is just a number); a card that was drawn and discarded
                -- within the transition has nothing to slide out of.
                local items = {}
                local j = i
                local hand = e.side == "m" and prev.hand or nil
                while j <= to and events[j].type == "disc" and events[j].side == e.side and not consumed[j] do
                    local x = events[j]
                    if x.amount == 0 and x.target ~= 0 and x.cardId and table.getn(items) < AN.MAX_DISCARDS then
                        local present = false
                        if hand then
                            for k = 1, table.getn(hand) do
                                if hand[k].id == x.target then
                                    present = true
                                end
                            end
                        else
                            present = e.side == "o" and prev.opp and (prev.opp.handCount or 0) > table.getn(items)
                        end
                        if present then
                            table.insert(items, { id = x.target, cardId = x.cardId })
                        end
                    end
                    j = j + 1
                end
                if table.getn(items) > 0 then
                    add({ kind = "discard", dur = AN.DISCARD, items = items, side = e.side }, e)
                end
                i = j
            elseif matchesSpecialResult(e) and not covered[i] then
                -- Explicit source/side/card-result match recovers only the five whitelisted cast markers.
                local idxs = {}
                local j = i
                while j <= to and matchesSpecialResult(events[j]) and not covered[j] and not consumed[j] do
                    table.insert(idxs, j)
                    j = j + 1
                end
                effectBeat(specialPlay, idxs, false, e)
                i = j
            elseif (t == "eq" or t == "stl") and not covered[i] then
                -- These are renderer results only for cards that explicitly opt in to them.
                i = i + 1
            elseif t == "dest" and cause == nil and not covered[i] then
                -- Corruption's delayed destruction has no live play cause after the turn boundary.
                effectBeat(nil, { i }, false)
                i = i + 1
            elseif RESULTS[t] and not covered[i] then
                -- results without a cause: the consecutive ones are one effect
                local idxs = {}
                local j = i
                local resultCause = cause
                if activePlay and cause ~= activePlay and e.source ~= 0 and e.source == activePlay.source then
                    resultCause = activePlay
                end
                while j <= to and RESULTS[events[j].type] and events[j].type ~= "wbrk" and not covered[j]
                    and not consumed[j] and not (events[j].type == "disc" and events[j].amount == 0) do
                    local x = events[j]
                    if matchesSpecialResult(x) then
                        break
                    end
                    local belongsToPlay = activePlay and cause ~= activePlay and x.source ~= 0
                        and x.source == activePlay.source
                    if belongsToPlay and resultCause ~= activePlay then
                        if table.getn(idxs) > 0 then break end
                        resultCause = activePlay
                    elseif not belongsToPlay and resultCause == activePlay then
                        break
                    end
                    if not mappedSpecialResult(resultCause, x.type) then
                        break
                    end
                    table.insert(idxs, j)
                    j = j + 1
                end
                if table.getn(idxs) == 0 then
                    i = i + 1
                else
                    local savedFamily = causeFamily
                    effectBeat(resultCause, idxs, false)
                    if resultCause ~= cause then causeFamily = savedFamily end
                    i = j
                end
            else
                i = i + 1
            end
        end
    end

    process(1, n)
    return beats
end

-- The 1-based event index lists are not needed outside; a plan only keeps the beats.

local function sumDur(beats)
    local total = 0
    for i = 1, table.getn(beats) do
        total = total + beats[i].dur
    end
    return total
end

-- True when the events of a transition have a hole (they would tell an incomplete story).
local function hasGap(tr)
    local ev = tr.events
    local n = table.getn(ev)
    if n == 0 then
        return false
    end
    if (tr.lastSeen or 0) > 0 and ev[1].seq > tr.lastSeen + 1 then
        return true
    end
    for i = 2, n do
        if ev[i].seq ~= ev[i - 1].seq + 1 then
            return true
        end
    end
    return false
end

-- A weapon that is on the presented board but whose slot is empty in the new game broke, whatever the events say
-- (a lost event must not make it vanish without a word): a weapon_break beat at the end, unless the events gave it one.
local function addMissingBreaks(beats, prev, game)
    if not prev or game.ph == "over" then
        return
    end
    local sides = { { "m", prev.me, game.me }, { "o", prev.opp, game.opp } }
    for k = 1, 2 do
        local key, before, after = sides[k][1], sides[k][2], sides[k][3]
        local w = before and before.weapon
        if w and not (after and after.weapon) then
            local has = false
            for i = 1, table.getn(beats) do
                if beats[i].kind == "weapon_break" and beats[i].id == w.id then
                    has = true
                end
            end
            if not has then
                table.insert(beats, { kind = "weapon_break", dur = AN.WEAPON_BREAK, id = w.id, cardId = w.cardId,
                    side = key, seq = 0 })
            end
        end
    end
end

-- The plan of a transition: { mid, prev, game, beats, total, events, snap, reason }. A snap plan has no
-- beats; the newest game is shown at once.
function AN.Plan(tr)
    if type(tr) ~= "table" or not tr.game then
        return nil
    end
    local plan = { mid = tr.mid, prev = tr.prev, game = tr.game, beats = {}, total = 0, snap = false,
        events = table.getn(tr.events or {}) }
    local function snap(reason)
        plan.snap = true
        plan.reason = reason
        plan.beats = {}
        plan.total = 0
        return plan
    end
    local g = tr.game
    if AN.failed then
        return snap("error")
    elseif AN.mode == "off" then
        return snap("off")
    elseif tr.snap then
        return snap(tr.reason or "snap")
    elseif not tr.prev or tr.prev.mid ~= g.mid then
        return snap("first")
    elseif hasGap(tr) then
        return snap("gap")
    elseif plan.events > AN.MAX_EVENTS then
        return snap("overflow")
    elseif g.ph == "over" and g.res == "int" then
        return snap("interrupted")
    end
    local beats = AN.Build(tr.events, tr.prev, g)
    addMissingBreaks(beats, tr.prev, g)
    local total = sumDur(beats)
    if g.ph == "over" and total > AN.OVER_CAP then
        -- the result must not wait: keep the beats that show how it ended
        local kept = {}
        for i = 1, table.getn(beats) do
            local k = beats[i].kind
            if k ~= "reveal" and k ~= "effect" and k ~= "fatigue" then
                table.insert(kept, beats[i])
            end
        end
        beats = kept
        total = sumDur(beats)
        if total > AN.OVER_CAP then
            return snap("overflow")
        end
    end
    if table.getn(beats) > AN.MAX_BEATS or total > AN.MAX_PLAN then
        return snap("overflow")
    end
    plan.beats = beats
    plan.total = total
    return plan
end

-- The kinds of the beats of a plan, for tests and the debug state ("attack,death").
function AN.Kinds(plan)
    local out = {}
    for i = 1, table.getn(plan and plan.beats or {}) do
        table.insert(out, plan.beats[i].kind)
    end
    return table.concat(out, ",")
end

-- ---------------------------------------------------------------- attack pose
-- The position of an attacker along its path at t seconds into an attack beat: 0 = at home, -1 = fully pulled
-- back (anticipation, a few pixels), 1 = at the contact point. A fizzle goes only beat.reach of the way.
-- Returns pos, phase ("anticipate" | "strike" | "hold" | "return" | "done").
function AN.AttackPose(beat, t)
    local reach = beat.reach or 1
    if t < 0 then
        t = 0
    end
    local a = AN.ANTICIPATE
    if t < a then
        return -math.sin(math.pi / 2 * t / a), "anticipate"
    end
    t = t - a
    if beat.fizzle then
        local s, r = AN.FIZZLE_STRIKE, AN.FIZZLE_RETURN
        if t < s then
            local u = t / s
            return -1 + (1 + reach) * u * u, "strike"
        end
        t = t - s
        if t < r then
            local u = t / r
            return reach * (1 - u) * (1 - u), "return"
        end
        return 0, "done"
    end
    if t < AN.STRIKE then
        local u = t / AN.STRIKE
        return -1 + 2 * u * u, "strike"
    end
    t = t - AN.STRIKE
    if t < AN.HOLD then
        return 1, "hold"
    end
    t = t - AN.HOLD
    if t < AN.RETURN then
        local u = t / AN.RETURN
        return (1 - u) * (1 - u), "return"
    end
    return 0, "done"
end

-- ---------------------------------------------------------------- the clock
local function fire(name, a, b, c)
    local l = AN.listener
    if not l or not l[name] then
        return
    end
    local ok, err = pcall(l[name], a, b, c)
    if not ok and not AN.failed then
        AN.failed = tostring(err)
        queue = {}
        cur = nil
        gen = gen + 1
        local snap = l.OnSnap
        if snap then
            pcall(snap, "error")
        end
    end
end

local function clearAll()
    queue = {}
    cur = nil
    gen = gen + 1
    AN.timedScale = 1
end

-- True while a plan runs or waits: the presented game is behind HsMatch.game.
function AN.Busy()
    return cur ~= nil or table.getn(queue) > 0
end

-- The game that is on screen now: the game before the plan that runs (or waits first), or nil when the
-- newest game is shown.
function AN.PresentedGame()
    if cur then
        return cur.plan.prev
    end
    if queue[1] then
        return queue[1].prev
    end
    return nil
end

-- The running plan, its beat (nil between beats) and the seconds into that beat.
function AN.Current()
    if not cur then
        return nil, nil, 0
    end
    return cur.plan, cur.beat, cur.t
end

function AN.QueuedPlans()
    return table.getn(queue)
end

-- Seconds of work left at nominal speed.
function AN.Remaining()
    local r = 0
    if cur then
        local beats = cur.plan.beats
        for i = cur.i + 1, table.getn(beats) do
            r = r + beats[i].dur
        end
        if cur.beat then
            r = r + math.max(0, cur.beat.dur - cur.t)
        end
    end
    for i = 1, table.getn(queue) do
        r = r + queue[i].total
    end
    return r
end

local function queuedEvents()
    local n = 0
    for i = 1, table.getn(queue) do
        n = n + queue[i].events
    end
    return n
end

local function snapAll(reason)
    local was = AN.Busy()
    clearAll()
    if was then
        fire("OnSnap", reason)
    end
end

-- Adds the transition of a new game. Returns its plan. A snap plan (plan.snap, plan.reason) clears all queued
-- work and tells the listener (OnSnap) when something was running; the caller then shows the newest game.
-- A plan without beats while nothing runs has plan.instant = true (nothing to do either).
function AN.Enqueue(tr)
    local plan = AN.Plan(tr)
    if not plan then
        return nil
    end
    if plan.snap then
        snapAll(plan.reason)
        return plan
    end
    if table.getn(plan.beats) == 0 and not AN.Busy() then
        plan.instant = true
        AN.timedScale = 1
        return plan
    end
    table.insert(queue, plan)
    if AN.Remaining() > AN.MAX_TOTAL or queuedEvents() > AN.MAX_QUEUE_EVENTS then
        -- the network outruns the presentation: drop the cosmetics and show the newest game
        plan.snap = true
        plan.reason = "overflow"
        snapAll("overflow")
        return plan
    end
    -- a timed human turn must not lose its seconds to cosmetics
    local g = plan.game
    AN.timedScale = 1
    if MA.IsMyTurn(g) and (g.timer or 0) > 0 then
        local rem = AN.Remaining()
        if rem > AN.TIMED_SNAP then
            plan.snap = true
            plan.reason = "timed"
            snapAll("timed")
            return plan
        elseif rem > AN.TIMED_CAP * AN.Speed() then
            AN.timedScale = rem / (AN.TIMED_CAP * AN.Speed())
        end
    end
    return plan
end

-- Advances the clock by dt seconds (real time; the mode speed and the timed-turn scale are applied here).
function AN.Tick(dt)
    if type(dt) ~= "number" or dt <= 0 then
        return
    end
    if dt > AN.MAX_DT then
        dt = AN.MAX_DT
    end
    dt = dt * AN.Scale()
    local startGen = gen
    local guard = 0
    while guard < 400 do
        guard = guard + 1
        if gen ~= startGen then
            return
        end
        if cur == nil then
            if table.getn(queue) == 0 then
                return
            end
            cur = { plan = table.remove(queue, 1), i = 0, beat = nil, t = 0 }
            fire("OnPlanStart", cur.plan)
        elseif cur.beat == nil then
            local plan = cur.plan
            cur.i = cur.i + 1
            local b = plan.beats[cur.i]
            if b == nil then
                cur = nil
                if table.getn(queue) == 0 then
                    AN.timedScale = 1
                end
                fire("OnPlanEnd", plan)
            else
                cur.beat = b
                cur.t = 0
                fire("OnBeatStart", b, plan)
            end
        else
            local b = cur.beat
            local plan = cur.plan
            local left = b.dur - cur.t
            if left < 0 then
                left = 0
            end
            if dt >= left and (dt > 0 or left == 0) then
                dt = dt - left
                cur.t = b.dur
                fire("OnBeatTick", b, cur.t, plan)
                if gen ~= startGen then
                    return
                end
                cur.beat = nil
                fire("OnBeatEnd", b, plan)
            else
                if dt <= 0 then
                    return
                end
                cur.t = cur.t + dt
                fire("OnBeatTick", b, cur.t, plan)
                return
            end
        end
    end
end

-- Skips everything: the queue is dropped and the newest game is shown (the listener restores what it moved).
-- Returns true when something was running.
function AN.FastForward()
    if not AN.Busy() then
        return false
    end
    snapAll("skip")
    return true
end

-- Forgets all work (a new match, no match, a hidden window, /reload). The listener is told when something ran.
function AN.Reset()
    snapAll("reset")
end

-- Clears the error state (tests, and a deliberate re-enable).
function AN.ClearFailure()
    AN.failed = nil
end
