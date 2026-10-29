-- Battlecries, triggers, deathrattles, weapons, and Choose One results use their emitted causes.
return function(T)
    local check, AN = T.check, HsMatchAnim
    local function event(line, seq)
        local _, _, ty, side, source, target, amount, cardId = string.find(line,
            "^(%l+)%.(.)%.(%d+)%.(%d+)%.(-?%d+)%.(.+)$")
        return { type = ty, side = side, source = tonumber(source), target = tonumber(target),
            amount = tonumber(amount), cardId = cardId ~= "-" and cardId or nil, seq = seq }
    end
    local function build(lines, prev, game)
        local events = {}
        for i = 1, table.getn(lines) do events[i] = event(lines[i], i) end
        return AN.Build(events, prev or HsMatch.game, game or HsMatch.game)
    end
    local function effectFor(beats, cardId)
        for _, b in ipairs(beats) do
            if b.kind == "effect" and b.cardId == cardId then return b end
        end
    end
    local function hasKind(beats, kind)
        for _, b in ipairs(beats) do if b.kind == kind then return true end end
        return false
    end

    -- Profiled Battlecries must wait for their real result event. The played minion's own sum is not a cause.
    for _, item in ipairs({
        { "CS2_042", "dmg.o.12.45.3.-", "fire" },
        { "CS2_088", "heal.m.12.40.6.-", "heal" },
        { "EX1_015", "draw.m.0.90.0.CS2_029", "resource" },
    }) do
        local beats = build({ "play.m.12.0.0." .. item[1], item[2] })
        local b = effectFor(beats, item[1])
        check(b and b.causeType == "play" and b.source == 12,
            item[1] .. " Battlecry uses its play as the effect cause")
        if item[1] == "EX1_015" then
            check(b and table.getn(b.targets) == 0 and b.visual == "resource",
                "Novice Engineer draw cue does not expose the drawn card as a target")
        else
            check(b and b.targets[1] == (item[1] == "CS2_042" and 45 or 40),
                item[1] .. " Battlecry anchors to the emitted result")
        end
    end
    local owl = build({ "play.m.12.45.0.CS2_203", "sil.o.12.45.0.-" })
    check(effectFor(owl, "CS2_203") and effectFor(owl, "CS2_203").targetVisuals[45] == "silence",
        "Ironbeak Owl uses the emitted silence target")

    -- Ability events identify the owning board entity and card, so source attribution stays with that owner.
    for _, item in ipairs({
        { "CS2_042", "dmg.o.45.40.3.-", "fire" },
        { "EX1_019", "buff.m.45.40.1.-", "buff" },
    }) do
        local beats = build({ "trig.m.45.40.0." .. item[1], item[2] })
        local b = effectFor(beats, item[1])
        check(b and b.causeType == "trig" and b.source == 45,
            item[1] .. " trigger is attributed to its owning minion")
    end

    -- Deathrattles have result-driven visuals even though their dead source is no longer on the board.
    local dr = build({ "dr.m.45.40.0.EX1_556", "sum.m.0.90.2.EX1_556t", "dmg.o.0.46.2.-" })
    local foundSummon = false
    for _, b in ipairs(dr) do
        if b.kind == "effect" and b.targetVisuals[90] == "summon" and b.slots[90] == 2 then foundSummon = true end
    end
    check(foundSummon, "Deathrattle summon keeps its emitted side and slot")
    check(hasKind(dr, "impact"), "Deathrattle damage keeps its ordinary impact number")

    -- A played vanilla minion or weapon gets no ability pulse from placement/equip alone.
    local vanilla = build({ "play.m.12.0.0.CS2_172", "sum.m.0.12.2.CS2_172" })
    check(not hasKind(vanilla, "effect"), "ordinary minion summon is not an ability effect")
    local equipped = build({ "play.m.12.0.0.CS2_097", "eq.m.12.12.0.CS2_097" })
    check(not hasKind(equipped, "effect"), "ordinary equip is not an ability effect")
    local bareTrigger = build({ "trig.m.45.40.0.EX1_412" })
    check(not hasKind(bareTrigger, "effect"), "a trigger without an emitted result has no ability effect")
    local triggerDraw = build({ "trig.m.45.40.0.EX1_095", "draw.m.0.90.0.CS2_029" })
    local drawCue = effectFor(triggerDraw, "EX1_095")
    check(drawCue and drawCue.visual == "resource" and table.getn(drawCue.targets) == 0,
        "a draw trigger gets an owner cue without exposing the drawn card")
    local deathDraw = build({ "dr.m.45.40.0.EX1_096", "draw.m.0.90.0.CS2_029" })
    local deathCue = effectFor(deathDraw, "EX1_096")
    check(deathCue and deathCue.visual == "resource" and table.getn(deathCue.targets) == 0,
        "a deathrattle draw cue does not expose the drawn card")
    for _, id in ipairs({ "EX1_612", "NEW1_029", "FP1_030" }) do
        local cue = build({ "play.m.12.0.0." .. id, "sum.m.0.12.2." .. id })
        local b = effectFor(cue, id)
        check(HS_EffectProfiles.cards[id].cueOnPlay and b and b.visual and b.slots[12] == 2,
            id .. " explicit play cue requires the emitted self-summon")
    end

    -- Chosen option profiles can be supplied as profile.choices on the base card.
    local profiles = HS_EffectProfiles
    local copy = { cards = {} }
    for id, p in pairs(profiles.cards) do copy.cards[id] = p end
    copy.cards.EX1_160 = { visual = "generic", flight = 0.3, choices = { "EX1_160a", "EX1_160b" } }
    copy.cards.EX1_160b = { visual = "buff", flight = 0.3, resultVisuals = { buff = "buff" } }
    HS_EffectProfiles = copy
    local chosen = build({ "play.m.12.45.2.EX1_160", "buff.m.12.40.2.-" })
    HS_EffectProfiles = profiles
    local option = effectFor(chosen, "EX1_160b")
    check(option and option.causeType == "play" and option.targetVisuals[40] == "buff",
        "Choose One uses the selected option's presentation profile")

    -- Exercise a newly played minion's source anchor before the completed board is committed.
    local F, MU, X = T.animSetup()
    local base = T.baselineLevels(MU)
    X.push(T.nextE({}, { mb = "40.CS2_172.2.3.3.-", ob = "45.CS2_172.2.3.3.-" }))
    X.push(T.nextE({ "play.m.12.45.0.CS2_042", "sum.m.0.12.2.CS2_042", "dmg.o.12.45.3.-" },
        { mb = "40.CS2_172.2.3.3.-,12.CS2_042.3.4.4.-", ob = "45.CS2_172.2.3.3.-" }))
    MU.AnimTick(0.01)
    local plan = AN.Current()
    local actual = effectFor(plan and plan.beats or {}, "CS2_042")
    local ctx = actual and MU.EffectContext(actual)
    check(actual and actual.slots[12] == 2 and ctx and ctx.src and ctx.src.id == 12
        and ctx.src.kind == "minion", "Fire Elemental starts from its future board slot")
    T.runTicks(MU, 2)
    T.checkAtRest(MU, base, "Fire Elemental play cleanup")
    T.animTeardown(F, MU)

    -- A trigger may intervene, but the emitted source still ties a later result to the original spell.
    F, MU, X = T.animSetup()
    base = T.baselineLevels(MU)
    X.push(T.nextE({}, { mb = "45.NEW1_012.1.3.3.-", ob = "46.CS2_172.2.3.3.-" }))
    X.push(T.nextE({ "play.m.12.46.0.CS2_029", "trig.m.45.45.0.NEW1_012",
        "buff.m.45.45.1.-", "dmg.o.12.46.6.-" },
        { mb = "45.NEW1_012.1.3.3.-", ob = "46.CS2_172.2.3.3.-" }))
    MU.AnimTick(0.01)
    plan = AN.Current()
    local fireball, impact, debug = false, false, {}
    local triggerTargetsOnlyItsBuff = false
    for _, b in ipairs(plan and plan.beats or {}) do
        table.insert(debug, tostring(b.kind) .. ":" .. tostring(b.cardId))
        if b.causeType == "trig" then
            triggerTargetsOnlyItsBuff = table.getn(b.targets) == 1 and b.targets[1] == 45
        end
        if b.kind == "effect" and b.cardId == "CS2_029" and b.targets[1] == 46 then fireball = true end
        for _, h in ipairs(b.hits or {}) do
            table.insert(debug, "hit:" .. tostring(h.id) .. "/" .. tostring(h.source) .. "/" .. tostring(h.effectVisual))
            if h.id == 46 and h.effectVisual == "fireball" then impact = true end
        end
    end
    check(fireball and impact and triggerTargetsOnlyItsBuff,
        "Fireball remains source-attributed after Mana Addict's trigger without inheriting its enemy target")
    local missiles = build({ "play.m.12.0.0.EX1_277", "trig.m.45.45.0.NEW1_012", "buff.m.45.45.1.-",
        "dmg.o.12.46.1.-", "dmg.o.12.47.1.-", "dmg.m.12.48.1.-" })
    local projectileCount, projectileImpacts = 0, 0
    for _, b in ipairs(missiles) do
        if b.kind == "effect" and b.cardId == "EX1_277" and b.visual == "volley" then projectileCount = projectileCount + 1 end
        for _, h in ipairs(b.hits or {}) do
            if h.source == 12 and h.effectVisual == "volley" then projectileImpacts = projectileImpacts + 1 end
        end
    end
    check(projectileCount == 3 and projectileImpacts == 3,
        "interrupted Arcane Missiles keeps one projectile and impact per exact-source hit")
    local separated = build({ "play.m.12.0.0.EX1_277", "trig.m.45.45.0.NEW1_012",
        "dmg.o.45.48.2.-", "dmg.o.12.46.1.-", "dmg.o.12.47.1.-", "dmg.o.12.48.1.-" })
    local separatedVolley, triggerDamageFirst = 0, false
    for _, b in ipairs(separated) do
        if b.kind == "effect" and b.visual == "volley" then separatedVolley = separatedVolley + 1 end
        if b.kind == "impact" and b.hits[1] and b.hits[1].source == 45 then triggerDamageFirst = true end
    end
    check(separatedVolley == 3 and triggerDamageFirst,
        "a trigger hit before the spell's volley starts a separate resolution group")
    local nova = build({ "play.m.12.0.0.CS2_025", "trig.m.45.45.0.NEW1_012", "buff.m.45.45.1.-",
        "dmg.o.12.46.2.-", "dmg.o.12.47.2.-" })
    local novaEffects, novaImpacts = 0, 0
    for _, b in ipairs(nova) do
        if b.kind == "effect" and b.cardId == "CS2_025" then
            novaEffects = novaEffects + 1
            check(table.getn(b.targets) == 2, "interrupted area spell retains both actual targets in one effect")
        elseif b.kind == "impact" and b.cardId == "CS2_025" then
            novaImpacts = novaImpacts + 1
            check(table.getn(b.hits) == 2, "interrupted area spell retains one simultaneous hit group")
        end
    end
    check(novaEffects == 1 and novaImpacts == 1, "interrupted area spell schedules one effect and one impact")
    T.runTicks(MU, 2)
    T.checkAtRest(MU, base, "interleaved Fireball cleanup")
    T.animTeardown(F, MU)
end
