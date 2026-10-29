-- Source-bound late results, deferred destruction, equip anchors, and Flare removals.
return function(T)
    local H, check, AN = T.H, T.check, HsMatchAnim
    local FX = H.MatchFx

    local function event(line, seq)
        local _, _, ty, side, source, target, amount, cardId = string.find(line,
            "^(%l+)%.(.)%.(%d+)%.(%d+)%.(-?%d+)%.(.+)$")
        return { type = ty, side = side, source = tonumber(source), target = tonumber(target),
            amount = tonumber(amount), cardId = cardId ~= "-" and cardId or nil, seq = seq }
    end
    local function build(lines)
        local events = {}
        for i = 1, table.getn(lines) do events[i] = event(lines[i], i) end
        return AN.Build(events, HsMatch.game, HsMatch.game)
    end
    local function current(F, MU, X, lines, over)
        X.push(T.nextE(lines, over))
        MU.AnimTick(0.01)
        return AN.Current()
    end
    local function checkAtRest(MU, base, label, seconds)
        T.runTicks(MU, seconds or 3)
        T.checkAtRest(MU, base, label)
    end

    for _, item in ipairs({
        { id = "CS2_038", family = "nature", visual = "buff" },
        { id = "CS2_063", family = "shadow", visual = "curse" },
        { id = "CS2_074", family = "nature", visual = "buff" },
        { id = "EX1_409", family = "generic", visual = "buff" },
        { id = "EX1_544", family = "holy", visual = "resource" },
    }) do
        local profile = FX.Resolve(item.id, item.family)
        check(profile and profile.visual == item.visual, item.id .. " profile is enabled")
    end

    -- Corruption's curse marker is attributed only from the exact public play source and its actual result target.
    local F, MU, X = T.animSetup()
    local base = T.baselineLevels(MU)
    local plan = current(F, MU, X, { "play.m.12.45.0.CS2_063", "buff.o.12.45.0.-" })
    local _, beat = AN.Current()
    check(beat and beat.kind == "effect" and beat.visual == "curse" and beat.targets[1] == 45
        and beat.targetVisuals[45] == "curse", "Corruption uses its actual buff marker target")
    checkAtRest(MU, base, "Corruption cast marker cleanup")
    T.animTeardown(F, MU)

    F, MU, X = T.animSetup()
    base = T.baselineLevels(MU)
    plan = current(F, MU, X, { "play.m.12.45.0.CS2_063", "rev.o.12.60.0.tt_010",
        "sum.o.0.90.2.tt_010a", "buff.o.12.90.0.-" },
        { ob = "44.CS2_124.3.1.1.c,45.CS2_121.1.1.1.-,90.tt_010a.3.2.2.-" })
    local curse90, curse45 = false, false
    local redirectedCurse
    for _, b in ipairs(plan and plan.beats or {}) do
        if b.visual == "curse" then
            redirectedCurse = b
            for _, id in ipairs(b.targets or {}) do
                if id == 90 then curse90 = true elseif id == 45 then curse45 = true end
            end
        end
    end
    check(curse90 and not curse45, "Spellbender's actual later marker targets only the summoned entity")
    local redirectedCtx = redirectedCurse and MU.EffectContext(redirectedCurse)
    local redirectedAnchor = redirectedCtx and redirectedCtx.targets[1]
    local row = MU.RowPositions(3, MU.MINION_W, MU.MINION_GAP, MU.geom.bw, MU.CenterX(MU.geom.bw))
    check(redirectedCurse and redirectedCurse.slots[90] == 2 and redirectedAnchor and redirectedAnchor.id == 90
        and not redirectedAnchor.missing and redirectedAnchor.x == row[2] + MU.MINION_W / 2,
        "redirected result reuses the actual new entity's public summon slot")
    check(plan and plan.beats[1] and plan.beats[1].kind == "reveal" and plan.beats[1].dur == AN.SECRET,
        "ordinary Spellbender reveal remains unchanged")
    checkAtRest(MU, base, "redirected Corruption cleanup")
    T.animTeardown(F, MU)

    -- Countering the cast removes the source token; an unrelated secret source hit is never re-tagged.
    plan = build({ "play.m.12.45.0.CS2_063", "rev.o.12.60.0.tt_010", "cnt.m.12.0.0.-", "buff.o.12.45.0.-" })
    local leaked = false
    for _, b in ipairs(plan) do if b.visual == "curse" then leaked = true end end
    check(not leaked, "Countered Corruption does not recover a stale curse context")
    plan = build({ "play.m.12.45.0.CS2_063", "trig.o.99.45.0.EX1_412", "buff.o.12.90.0.-" })
    local curse90, curse45 = false, false
    for _, b in ipairs(plan) do
        if b.visual == "curse" then
            for _, id in ipairs(b.targets or {}) do
                if id == 90 then curse90 = true elseif id == 45 then curse45 = true end
            end
        end
    end
    check(curse90 and not curse45, "a trigger cannot consume the actual Corruption marker")
    plan = build({ "play.m.12.45.0.CS2_038", "trig.o.99.45.0.EX1_412", "buff.m.12.90.0.-" })
    local ancestral90, ancestral45 = false, false
    for _, b in ipairs(plan) do
        if b.visual == "buff" then
            for _, id in ipairs(b.targets or {}) do
                if id == 90 then ancestral90 = true elseif id == 45 then ancestral45 = true end
            end
        end
    end
    check(ancestral90 and not ancestral45, "a trigger cannot consume the actual Ancestral Spirit marker")
    plan = build({ "play.m.12.0.0.EX1_409", "trig.o.99.45.0.EX1_412", "eq.m.0.90.0.EX1_409t" })
    local recoveredEquip = false
    for _, b in ipairs(plan) do
        if b.equipments and b.equipments[90] and b.targetVisuals[90] == "buff" then recoveredEquip = true end
    end
    check(recoveredEquip, "Upgrade! recovers only its actual weapon marker after an intervening trigger")
    plan = build({ "play.m.12.45.0.CS2_063", "rev.o.12.60.0.tt_010", "dmg.o.60.45.2.-" })
    for _, b in ipairs(plan) do
        for _, h in ipairs(b.hits or {}) do
            if h.source == 60 then check(not h.effectVisual, "secret trigger hits do not inherit the spell profile") end
        end
    end

    -- Delayed Corruption destruction is an anonymous actual-destination cue after turn cause reset.
    plan = build({ "turn.o.0.0.3.-", "dest.o.0.45.0.-", "die.o.0.45.0.CS2_172" })
    check(plan[1] and plan[1].kind == "effect" and plan[1].visual == "destroy" and not plan[1].cardId
        and plan[1].targets[1] == 45, "orphan destruction uses a cardless cue at its emitted target ("
        .. tostring(plan[1] and plan[1].kind) .. ":" .. tostring(plan[1] and plan[1].visual) .. ":"
        .. tostring(plan[1] and plan[1].cardId) .. ":" .. tostring(plan[1] and plan[1].targets
            and plan[1].targets[1]) .. ")")
    F, MU, X = T.animSetup()
    base = T.baselineLevels(MU)
    X.push(T.nextE({ "turn.o.0.0.3.-", "dest.o.0.45.0.-", "die.o.0.45.0.EX1_412" },
        { ob = "44.CS2_124.3.1.1.c" }))
    MU.AnimTick(0.02)
    _, beat = AN.Current()
    check(beat and beat.visual == "destroy" and beat.fxItem and beat.fxItem.manual,
        "orphan Corruption destruction runs through the live destroy controller")
    T.runTicks(MU, 3)
    check(not MU.ui.o.minions[2].frame:IsShown(), "orphan Corruption destruction commits the actual death")
    T.checkAtRest(MU, base, "orphan Corruption destroy completion")
    T.animTeardown(F, MU)

    -- Ancestral Spirit's immediate buff is profiled; its later unprofiled deathrattle summons by actual new ID.
    plan = build({ "play.m.12.45.0.CS2_038", "buff.m.12.45.0.-", "die.m.0.45.0.CS2_172",
        "dr.m.45.0.0.CS2_172", "sum.m.0.90.2.CS2_172" })
    local spiritBuff, resurrection = nil, nil
    for _, b in ipairs(plan) do
        if b.visual == "buff" and b.cardId == "CS2_038" then spiritBuff = b end
        if b.causeType == "dr" then resurrection = b end
    end
    check(spiritBuff and spiritBuff.targets[1] == 45, "Ancestral Spirit profiles the actual cast buff")
    check(resurrection and resurrection.visual == "summon" and resurrection.cardId == nil
        and resurrection.targets[1] == 90 and resurrection.slots[90] == 2,
        "later resurrection uses the fresh summoned entity, independent of the dead minion card")
    F, MU, X = T.animSetup()
    base = T.baselineLevels(MU)
    X.push(T.nextE({ "play.m.12.45.0.CS2_038", "buff.m.12.45.0.-", "die.m.0.45.0.CS2_172",
        "dr.m.45.0.0.CS2_172", "sum.m.0.90.2.CS2_172" },
        { mb = "40.CS2_172.3.2.2.a,41.CS2_179.3.5.5.t,42.CS2_168.2.1.1.a,90.CS2_172.3.2.2.-" }))
    MU.AnimTick(0.02)
    local resurrectionBeat
    for _ = 1, 6 do
        local _, active = AN.Current()
        if active and active.visual == "summon" then resurrectionBeat = active; break end
        MU.AnimTick(0.5)
    end
    local resurrectionCtx = resurrectionBeat and MU.EffectContext(resurrectionBeat)
    check(resurrectionBeat and resurrectionBeat.fxItem and resurrectionBeat.fxItem.manual
        and resurrectionCtx and resurrectionCtx.targets[1] and resurrectionCtx.targets[1].id == 90,
        "Ancestral Spirit deathrattle runs a summon controller at its fresh entity")
    T.runTicks(MU, 3)
    check(MU.ui.m.minions[4].entity == 90, "Ancestral Spirit resurrection commits the new board entity")
    T.checkAtRest(MU, base, "Ancestral Spirit resurrection completion")
    T.animTeardown(F, MU)

    local mixedDr = build({ "dr.o.45.0.0.EX1_412", "dmg.m.45.46.2.-", "buff.m.45.47.1.-",
        "sum.o.0.90.3.CS2_172" })
    local mixed = mixedDr[1]
    check(mixed and mixed.visual == "summon" and mixed.targetVisuals[46] == "impact"
        and mixed.targetVisuals[47] == "buff" and mixed.targetVisuals[90] == "summon",
        "mixed deathrattle outcomes use each actual result type")
    local taggedDamage = false
    for _, b in ipairs(mixedDr) do
        for _, h in ipairs(b.hits or {}) do if h.id == 46 and h.effectVisual == "impact" then taggedDamage = true end end
    end
    check(taggedDamage, "deathrattle damage suppresses only its already scheduled impact flash")

    -- The new-weapon Upgrade! branch gets the actual weapon rectangle; existing-weapon buffs keep their real anchor.
    F, MU, X = T.animSetup()
    base = T.baselineLevels(MU)
    local eqLines = { "play.m.12.0.0.EX1_409", "eq.m.0.80.0.EX1_409t" }
    plan = build(eqLines)
    local eqBeat = nil
    for _, b in ipairs(plan) do if b.kind == "effect" and b.equipments and b.equipments[80] then eqBeat = b end end
    local eqCtx = eqBeat and MU.EffectContext(eqBeat)
    local eqGoal = eqCtx and eqCtx.targets[1]
    local rect = MU.ui.m.weapon.rect
    check(eqGoal and eqGoal.kind == "weapon" and eqGoal.id == 80 and eqGoal.side == "m"
        and eqGoal.x == rect.x + rect.w / 2 and eqGoal.y == rect.y + rect.h / 2,
        "new friendly weapon cue uses the future weapon slot")
    local enemyEq = build({ "play.o.13.0.0.EX1_409", "eq.o.0.81.0.EX1_409t" })
    local enemyBeat = nil
    for _, b in ipairs(enemyEq) do if b.kind == "effect" and b.equipments and b.equipments[81] then enemyBeat = b end end
    local enemyCtx = enemyBeat and MU.EffectContext(enemyBeat)
    local enemyGoal = enemyCtx and enemyCtx.targets[1]
    rect = MU.ui.o.weapon.rect
    check(enemyGoal and enemyGoal.kind == "weapon" and enemyGoal.id == 81 and enemyGoal.side == "o"
        and enemyGoal.x == rect.x + rect.w / 2 and enemyGoal.y == rect.y + rect.h / 2,
        "new enemy weapon cue uses the opponent weapon slot")

    local oldEntity, oldShown = MU.ui.m.weapon.entity, MU.ui.m.weapon.frame:IsShown()
    MU.ui.m.weapon.entity = 82
    MU.ui.m.weapon.frame:Show()
    local buffPlan = build({ "play.m.12.0.0.EX1_409", "buff.m.12.82.1.-" })
    local buffCtx = buffPlan[1] and MU.EffectContext(buffPlan[1])
    check(buffCtx and buffCtx.targets[1] and buffCtx.targets[1].id == 82
        and buffCtx.targets[1].kind == "weapon", "Upgrade! existing weapon branch preserves the live weapon anchor")
    MU.ui.m.weapon.entity = oldEntity
    if not oldShown then MU.ui.m.weapon.frame:Hide() end
    checkAtRest(MU, base, "Upgrade weapon anchor cleanup")
    T.animTeardown(F, MU)

    -- Deadly Poison's actual weapon buff uses the live weapon, while ordinary eq/stl events stay legacy.
    F, MU, X = T.animSetup()
    X.push(T.nextE({}, { mw = "83.CS2_082.1.2" }))
    MU.AnimTick(0.01)
    base = T.baselineLevels(MU)
    plan = current(F, MU, X, { "play.m.12.0.0.CS2_074", "buff.m.12.83.1.-" })
    _, beat = AN.Current()
    local poisonCtx = beat and MU.EffectContext(beat)
    check(beat and beat.visual == "buff" and poisonCtx and poisonCtx.targets[1].kind == "weapon",
        "Deadly Poison uses the existing weapon anchor")
    T.animTeardown(F, MU)
    plan = build({ "play.m.12.0.0.CS2_106", "eq.m.0.80.0.CS2_106" })
    local ordinaryWeapon = false
    for _, b in ipairs(plan) do if b.kind == "effect" then ordinaryWeapon = true end end
    check(not ordinaryWeapon, "ordinary weapon equip does not gain a new spell pulse")
    plan = build({ "atk.m.40.44.0.-", "stl.m.0.40.0.-", "dmg.o.40.44.2.-" })
    local silencePulses = 0
    for _, b in ipairs(plan) do if b.visual == "silence" then silencePulses = silencePulses + 1 end end
    check(silencePulses == 0, "stealth removal during an ordinary attack remains legacy")

    -- Flare retains public card reveals, adds one concise owner-hero removal cue per secret, and never exposes own secrets.
    F, MU, X = T.animSetup()
    base = T.baselineLevels(MU)
    local flareEvents = { "play.m.12.0.0.EX1_544", "stl.o.12.45.0.-" }
    for i = 1, 5 do table.insert(flareEvents, "rev.o.12." .. (60 + i) .. ".0.EX1_" .. (609 + i)) end
    table.insert(flareEvents, "draw.m.0.90.0.CS2_029")
    plan = current(F, MU, X, flareEvents)
    local publicReveals, removals = 0, 0
    local flareCues = {}
    for _, b in ipairs(plan and plan.beats or {}) do
        if b.kind == "reveal" and b.secret then
            publicReveals = publicReveals + 1
            check(b.dur == AN.FLARE_REVEAL and b.cardId ~= nil, "Flare public reveal stays identifiable and short")
        elseif b.visual == "silence" and b.heroTargets then
            removals = removals + 1
            local id = b.targets[1]
            flareCues[id] = b
            check(b.cardId == nil and b.heroTargets[id] and b.sides[id] == "o", "Flare cue is anonymous and owner anchored")
            local ctx = MU.EffectContext(b)
            check(ctx.targets[1] and ctx.targets[1].kind == "hero" and ctx.targets[1].side == "o"
                and ctx.targets[1].id == id, "Flare cue resolves secret ID metadata to the secret owner's hero")
        end
    end
    check(publicReveals == 5 and removals == 5, "each of five enemy secrets gets reveal then removal")
    for i = 1, 5 do
        local revBeat, cue = nil, flareCues[60 + i]
        for _, b in ipairs(plan and plan.beats or {}) do
            if b.kind == "reveal" and b.target == 60 + i then revBeat = b end
        end
        check(revBeat and cue and revBeat.seq == cue.seq and revBeat.dur == AN.FLARE_REVEAL,
            "Flare reveal precedes its matching removal cue")
    end
    check(plan and not plan.snap and plan.total <= AN.MAX_PLAN, "five-secret Flare fits inside the animation plan cap")
    checkAtRest(MU, base, "Flare removal cleanup", 6)
    T.animTeardown(F, MU)

    -- A trigger may precede the same-source Flare markers; group fourteen stealth removals and keep five reveals bounded.
    for _, side in ipairs({ "m", "o" }) do
        local other = side == "m" and "o" or "m"
        local source = 12
        local flareAfterTrigger = { "play." .. side .. "." .. source .. ".0.0.EX1_544",
            "trig." .. other .. ".99.45.0.EX1_412" }
        for i = 1, 14 do table.insert(flareAfterTrigger, "stl." .. other .. ".12." .. (40 + i) .. ".0.-") end
        for i = 1, 5 do table.insert(flareAfterTrigger, "rev." .. other .. ".12." .. (60 + i) .. ".0.EX1_" .. (609 + i)) end
        plan = build(flareAfterTrigger)
        local grouped, revealCount, removalCount = nil, 0, 0
        for _, b in ipairs(plan) do
            if b.types and b.types.stl then grouped = b end
            if b.kind == "reveal" and b.secret then revealCount = revealCount + 1 end
            if b.heroTargets then removalCount = removalCount + 1 end
        end
        check(grouped and table.getn(grouped.targets) == 8 and revealCount == 5 and removalCount == 5,
            "Flare recovers its fourteen actual stealth markers as one bounded cue after a trigger")
        local total = 0
        for _, b in ipairs(plan or {}) do total = total + (b.dur or 0) end
        check(plan and total <= AN.MAX_PLAN,
            "triggered fourteen-target Flare with five secrets stays within the plan cap")
    end

    -- The same five-secret volley remains bounded when the opponent casts Flare and their play card is revealed.
    F, MU, X = T.animSetup()
    local enemyFlare = { "play.o.12.0.0.EX1_544", "stl.m.12.40.0.-" }
    for i = 1, 5 do table.insert(enemyFlare, "rev.m.12." .. (60 + i) .. ".0.EX1_" .. (609 + i)) end
    plan = current(F, MU, X, enemyFlare)
    local enemyRemovalCount = 0
    for _, b in ipairs(plan and plan.beats or {}) do if b.heroTargets then enemyRemovalCount = enemyRemovalCount + 1 end end
    check(plan and not plan.snap and plan.total <= AN.MAX_PLAN and enemyRemovalCount == 5,
        "opponent five-secret Flare fits its plan cap with every removal cue")
    T.animTeardown(F, MU)

    plan = build({ "play.m.12.0.0.EX1_544", "rev.m.12.60.0.EX1_610", "rev.o.99.61.0.EX1_611" })
    local ownReveal, unrelatedReveal = false, false
    for _, b in ipairs(plan) do
        if b.kind == "reveal" and b.target == 60 then ownReveal = true end
        if b.kind == "reveal" and b.target == 61 and b.dur == AN.SECRET then unrelatedReveal = true end
    end
    check(not ownReveal and unrelatedReveal, "Flare cannot reveal own secrets and unrelated public reveals stay legacy")

    -- Flare stealth results stay a single silence group before its public removals; unprofiled source markers do not inherit it.
    plan = build({ "play.m.12.0.0.EX1_544", "stl.o.12.45.0.-", "stl.o.12.46.0.-", "rev.o.12.60.0.EX1_609" })
    local groupedStl, flareRemoval = nil, nil
    for _, b in ipairs(plan) do
        if b.types and b.types.stl then groupedStl = b end
        if b.heroTargets then flareRemoval = b end
    end
    check(groupedStl and groupedStl.targetVisuals[45] == "silence" and groupedStl.targetVisuals[46] == "silence"
        and flareRemoval and flareRemoval.seq > groupedStl.seq, "Flare stealth removal groups before secret removal")
end
