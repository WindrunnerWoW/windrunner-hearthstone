-- Sequential projectile results keep one visual/impact pair per emitted hit.
return function(T)
    local H, check, AN = T.H, T.check, HsMatchAnim
    local FX = H.MatchFx

    local function ev(line, seq)
        local _, _, ty, side, source, target, amount, cardId = string.find(line,
            "^(%l+)%.(.)%.(%d+)%.(%d+)%.(-?%d+)%.(.+)$")
        return { type = ty, side = side, source = tonumber(source), target = tonumber(target),
            amount = tonumber(amount), cardId = cardId ~= "-" and cardId or nil, seq = seq }
    end
    local function build(lines)
        local events = {}
        for i = 1, table.getn(lines) do events[i] = ev(lines[i], i) end
        return AN.Build(events, HsMatch.game, HsMatch.game)
    end
    local function assertVolleyPairs(beats, hitIds, label, cardId)
        cardId = cardId or "EX1_277"
        check(table.getn(beats) == table.getn(hitIds) * 2, label .. " has one beat pair per actual hit")
        for i = 1, table.getn(hitIds) do
            local effect, impact = beats[i * 2 - 1], beats[i * 2]
            check(effect and effect.kind == "effect" and effect.visual == "volley", label .. " projectile " .. i .. " is an effect")
            check(effect and table.getn(effect.targets) == 1 and effect.targets[1] == hitIds[i], label .. " projectile " .. i .. " uses its real target")
            check(effect and effect.source == 12 and effect.cardId == cardId, label .. " projectile " .. i .. " keeps cast source")
            check(effect and effect.seq == i + 1 and impact and impact.seq == i + 1, label .. " projectile " .. i .. " uses hit sequence")
            check(impact and impact.kind == "impact" and table.getn(impact.hits) == 1, label .. " projectile " .. i .. " has its own impact")
            check(impact and impact.hits[1].id == hitIds[i] and impact.hits[1].effectVisual == "volley", label .. " projectile " .. i .. " suppresses only its own generic flash")
        end
    end

    local profile = FX.Resolve("EX1_277", "arcane")
    check(profile and profile.visual == "volley", "Arcane Missiles opts into volley")
    if profile and profile.visual == "volley" then
        assertVolleyPairs(build({ "play.m.12.0.0.EX1_277", "dmg.o.12.45.1.-", "dmg.o.12.45.1.-", "dmg.o.12.46.1.-" }),
            { 45, 45, 46 }, "Arcane Missiles repeated target")
        local wrath = FX.Resolve("EX1_384", "holy")
        check(wrath and wrath.visual == "volley", "Avenging Wrath opts into volley")
        assertVolleyPairs(build({ "play.m.12.0.0.EX1_384", "dmg.o.12.45.1.-", "dmg.o.12.46.1.-", "dmg.o.12.47.1.-",
            "dmg.o.12.48.1.-", "dmg.o.12.49.1.-", "dmg.o.12.50.1.-", "dmg.o.12.51.1.-", "dmg.o.12.52.1.-" }),
            { 45, 46, 47, 48, 49, 50, 51, 52 }, "Avenging Wrath", "EX1_384")

        local absorbed = build({ "play.m.12.0.0.EX1_277", "pop.o.12.45.0.-", "dmg.o.12.46.1.-" })
        check(table.getn(absorbed) == 4 and absorbed[1].visual == "volley" and absorbed[2].hits[1].kind == "pop"
            and absorbed[2].hits[1].effectVisual == "volley" and absorbed[3].visual == "volley"
            and absorbed[4].hits[1].id == 46, "absorbed missile keeps pop feedback and later projectile")

        local chain = build({ "play.m.12.0.0.EX1_277", "dmg.o.12.45.1.-", "die.o.0.45.0.CS2_172",
            "dmg.o.12.46.1.-", "trig.o.40.46.0.EX1_412", "dmg.m.40.41.1.-",
            "dr.o.42.0.0.EX1_412", "dmg.o.42.43.1.-", "rev.o.60.40.0.EX1_609",
            "dmg.m.60.40.1.-", "play.m.12.0.0.CS2_029", "dmg.o.12.44.6.-" })
        local volleyTargets, volleyCount = {}, 0
        for i = 1, table.getn(chain) do
            local b = chain[i]
            if b.visual == "volley" then volleyCount = volleyCount + 1; volleyTargets[volleyCount] = b.targets[1] end
        end
        check(volleyCount == 2 and volleyTargets[1] == 45 and volleyTargets[2] == 46,
            "death stays between projectiles; trigger, deathrattle, reveal and next play break volley attribution")
        check(chain[1].visual == "volley" and chain[2].kind == "impact" and chain[3].kind == "death"
            and chain[4].visual == "volley" and chain[5].kind == "impact", "death beat remains between projectile pairs")
        local hp = build({ "hp.m.12.0.0.EX1_277", "dmg.o.12.45.1.-", "dmg.o.12.46.1.-" })
        local hpVolley = false
        for i = 1, table.getn(hp) do if hp[i].visual == "volley" or (hp[i].hits and hp[i].hits[1].effectVisual == "volley") then hpVolley = true end end
        check(not hpVolley and hp[1].kind == "impact" and table.getn(hp[1].hits) == 2,
            "hero power hits remain grouped and never borrow volley visuals")
        local aoe = build({ "play.m.12.0.0.CS2_032", "dmg.o.12.45.2.-", "dmg.o.12.46.2.-", "dmg.o.12.47.2.-" })
        check(table.getn(aoe) == 2 and aoe[1].visual == "rain" and aoe[2].kind == "impact"
            and table.getn(aoe[2].hits) == 3, "ordinary AoE keeps its grouped effect and impact")
        local empty = build({ "play.m.12.0.0.EX1_277", "draw.m.0.90.0.CS2_029" })
        local fabricated = false
        for i = 1, table.getn(empty) do if empty[i].visual == "volley" then fabricated = true end end
        check(not fabricated, "empty volley has no fabricated projectile")
        for _, boundary in ipairs({ "turn.o.0.0.2.-", "end.o.0.0.0.-", "mull.m.0.0.0.-" }) do
            local broken = build({ "play.m.12.0.0.EX1_277", "dmg.o.12.45.1.-", boundary, "dmg.o.12.44.1.-" })
            local count = 0
            for i = 1, table.getn(broken) do if broken[i].visual == "volley" then count = count + 1 end end
            check(count == 1, boundary .. " ends active volley attribution")
        end
    end

    -- Exercise the actual timeline: one projectile arrives and presents one hit number before the next launches.
    local F, MU, X = T.animSetup()
    local base = T.baselineLevels(MU)
    local originalShowHits = MU.ShowHits
    local arrived = {}
    MU.ShowHits = function(hits, lunger)
        local entry = {}
        for i = 1, table.getn(hits or {}) do
            entry[i] = { id = hits[i].id, amount = hits[i].amount, kind = hits[i].kind,
                effectVisual = hits[i].effectVisual }
        end
        table.insert(arrived, entry)
        return originalShowHits(hits, lunger)
    end
    X.push(T.nextE({ "play.m.12.0.0.EX1_277", "dmg.o.12.45.1.-", "dmg.o.12.45.1.-", "dmg.o.12.44.1.-" }))
    local function tick(step)
        F.time = F.time + step
        MU.AnimTick(step)
    end
    tick(0.01)
    local plan, beat = AN.Current()
    check(plan and table.getn(plan.beats) == 6, "timeline contains three sequential volley arrivals")
    check(table.getn(arrived) == 0, "volley numbers wait for the first projectile arrival")
    local soundCount = table.getn(F.soundFiles) + table.getn(F.sounds)
    local soundsPerVolley = profile and profile.sound and 1 or 0
    for projectile = 1, 3 do
        check(table.getn(arrived) == projectile - 1, "projectile " .. projectile .. " has no hit number before its flight")
        local guard = 0
        while beat and beat.kind ~= "impact" and guard < 200 do
            tick(0.01)
            plan, beat = AN.Current()
            guard = guard + 1
        end
        check(beat and beat.kind == "impact" and table.getn(beat.hits) == 1,
            "timeline projectile " .. projectile .. " reaches its one-hit impact")
        check(table.getn(arrived) == projectile and table.getn(arrived[projectile]) == 1
            and arrived[projectile][1].amount == 1 and arrived[projectile][1].effectVisual == "volley",
            "timeline presents exactly projectile " .. projectile .. " at arrival")
        check(table.getn(F.soundFiles) + table.getn(F.sounds) == soundCount + projectile * soundsPerVolley,
            "projectile " .. projectile .. " sound is synchronized to arrival")
        guard = 0
        while beat and beat.kind == "impact" and guard < 50 do
            tick(0.01)
            plan, beat = AN.Current()
            guard = guard + 1
        end
    end
    T.runTicks(MU, 2)
    T.checkAtRest(MU, base, "volley timeline releases textures and text")
    MU.ShowHits = originalShowHits
    T.animTeardown(F, MU)
end
