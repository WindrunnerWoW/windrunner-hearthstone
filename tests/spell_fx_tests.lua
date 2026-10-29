-- Integration checks for shipped profiles: timing, bounded resources, failure paths and layer isolation.
return function(T)
    local H = T.H
    local check, FX, AN = T.check, H.MatchFx, HsMatchAnim
    local samples = {
        { "CS2_029", "fireball", "fire" }, { "CS2_024", "bolt", "frost" },
        { "CS1_130", "impact", "holy" }, { "CS2_072", "slash", "physical" },
        { "CS2_025", "nova", "arcane" }, { "CS2_028", "rain", "frost" },
        { "CS2_007", "heal", "nature" }, { "CS2_087", "buff", "holy" },
        { "EX1_238", "lightning", "nature" }, { "EX1_332", "silence", "silence" },
        { "CS2_022", "transform", "transform" }, { "CS2_027", "summon", "summon" },
        { "EX1_312", "destroy", "shadow" }, { "EX1_371", "shield", "holy" },
        { "EX1_581", "bounce", "shadow" }, { "CS1_113", "control", "shadow" },
        { "EX1_287", "secret", "secret" }, { "CS2_061", "drain", "shadow" },
        { "CS2_023", "resource", "arcane" },
        { "EX1_277", "volley", "arcane" }, { "EX1_275", "freeze", "frost" },
        { "EX1_128", "stealth", "shadow" },
        { "CS2_063", "curse", "shadow" },
    }
    local F, MU, X = T.animSetup()
    local base = T.baselineLevels(MU)
    local frames = table.getn(F.frames)
    for _, s in ipairs(samples) do
        local r = FX.Resolve(s[1], s[3])
        check(r.visual == s[2], s[1] .. " resolves to " .. s[2])
        check(type(r.flight) == "number" and r.flight >= 0.1 and r.flight <= 1, s[1] .. " has bounded hit time")
        FX.modelsDisabled = true
        F.time = F.time + 10
        local sound = table.getn(F.soundFiles) + table.getn(F.sounds)
        local ok, err = T.fxEffect(MU, { cardId = s[1], family = s[3], source = 40, aim = 45,
            targets = { 45 }, sides = { [45] = "o" }, dur = r.flight })
        check(ok, s[1] .. " starts: " .. tostring(err))
        check(FX.last and FX.last.visual == s[2] and not FX.last.error, s[1] .. " controller starts without error")
        check(table.getn(T.shownModels(F)) == 0 and MU.FxStats().textures > 0, s[1] .. " works with models disabled")
        for _, obj in ipairs(T.usedTextures(MU)) do
            check(string.find(obj:GetTexture() or "", "Textures\\Effects\\", 1, true) ~= nil, s[1] .. " uses alpha artwork")
        end
        MU.FxTick(r.flight - 0.001)
        check(table.getn(F.soundFiles) + table.getn(F.sounds) == sound, s[1] .. " no premature sound")
        MU.FxTick(0.002)
        check(table.getn(F.soundFiles) + table.getn(F.sounds) == sound + (r.sound and 1 or 0), s[1] .. " one arrival sound")
        MU.FxTick(1.6)
        check(table.getn(F.soundFiles) + table.getn(F.sounds) == sound + (r.sound and 1 or 0), s[1] .. " no repeated sound")
        T.checkAtRest(MU, base, s[1] .. " decay")
    end
    check(table.getn(F.frames) == frames, "all twenty-three behaviors reuse frames")
    FX.modelsDisabled = false

    -- Eight actual targets, including wide AoE. Essential target cues precede decorations.
    local goals = {}
    for i = 1, 8 do goals[i] = { id = 100+i, x = 80+i*65, y = 180, w = 60, h = 84, kind = "minion", side = "o" } end
    for _, s in ipairs({ samples[2], samples[5], samples[6], samples[7], samples[8], samples[9], samples[10],
        samples[11], samples[12], samples[13], samples[14], samples[15], samples[16], samples[17], samples[18], samples[19],
        samples[20], samples[21], samples[22], samples[23] }) do
        F.time = F.time + 10
        local ctx = { family = s[3], src = MU.AnchorOf(40), targets = goals, area = true, dur = 0.3 }
        local ok, err = pcall(FX.Play, FX.matchStage, ctx, { cardId = s[1] })
        check(ok and FX.last.goals == 8, s[2] .. " accepts eight targets: " .. tostring(err))
        check(MU.FxStats().textures >= 8 and MU.FxStats().textures <= MU.POOL_TEXTURES, s[2] .. " reserves bounded target cues")
        MU.AnimSkip()
        T.checkAtRest(MU, base, s[2] .. " AoE skip")
    end
    for _, s in ipairs({samples[6], samples[9], samples[10], samples[11], samples[12], samples[13], samples[14],
        samples[15], samples[16], samples[17], samples[18], samples[19], samples[20], samples[21], samples[22], samples[23]}) do
        local held = {}
        for i = 1, MU.POOL_TEXTURES - 8 do held[i] = MU.AllocTexture() end
        FX.Play(FX.matchStage, { family = s[3], src = MU.AnchorOf(40), targets = goals, area = true }, { cardId = s[1] })
        check(MU.FxStats().textures == MU.POOL_TEXTURES and FX.last and not FX.last.error,
            s[2] .. " eight free slots prioritize all eight target cues")
        MU.AnimSkip()
        for _, h in ipairs(held) do MU.FreeTexture(h) end
        T.checkAtRest(MU, base, s[2] .. " eight-target pool pressure")
    end

    -- Returning false from SetTexture is the client failure convention, not a Lua exception.
    local methods = {}
    for i, h in ipairs(MU.pool.tex) do
        methods[i] = h.obj.SetTexture
        h.obj.SetTexture = function(o, path)
            if type(path) == "string" and string.find(path, "Textures\\Effects\\", 1, true) then return false end
            return methods[i](o, path)
        end
    end
    for _, s in ipairs({samples[2],samples[9],samples[10],samples[11],samples[12],samples[13],samples[14],
        samples[15],samples[16],samples[17],samples[18],samples[19],samples[20],samples[21],samples[22],samples[23]}) do
        T.fxEffect(MU, { cardId = s[1], family = s[3], source = 40, targets = {45}, sides = {[45]="o"} })
        check(MU.FxStats().textures == 0 and FX.last and FX.last.error, s[2] .. " missing assets release every lease and diagnose")
        MU.AnimSkip()
    end
    for i, h in ipairs(MU.pool.tex) do h.obj.SetTexture = methods[i] end
    MU.AnimSkip()
    T.checkAtRest(MU, base, "missing generic assets")

    -- Real damage and healing transitions use the same manual timeline clock.
    for _, s in ipairs({ samples[2], samples[7], samples[9] }) do
        F.time = F.time + 10
        local r = FX.Resolve(s[1], s[3])
        local kind = s[2] == "heal" and "heal" or "dmg"
        local count = table.getn(F.soundFiles) + table.getn(F.sounds)
        X.push(T.nextE({ "play.m.12.45.0." .. s[1], kind .. ".o.12.45.1.-" }))
        local fullSteps = math.floor((r.flight - 0.001) / 0.01)
        for _ = 1, fullSteps do MU.AnimTick(0.01) end
        MU.AnimTick(r.flight - 0.001 - fullSteps * 0.01)
        local _, b = AN.Current()
        check(b and b.kind == "effect" and b.visual == s[2] and b.fxItem and b.fxItem.manual, s[2] .. " live timeline owns controller")
        check(table.getn(T.usedTexts(MU)) == 0, s[2] .. " numbers wait until hit")
        MU.AnimTick(0.002)
        check(table.getn(T.usedTexts(MU)) > 0, s[2] .. " numbers start at hit")
        check(table.getn(F.soundFiles) + table.getn(F.sounds) == count + (r.sound and 1 or 0), s[2] .. " sound synchronized")
        for _, it in ipairs(MU.fxItems) do check(it.kind ~= "flash", s[2] .. " no rectangular flash") end
        T.runTicks(MU, 2)
        T.checkAtRest(MU, base, s[2] .. " live completion")
    end
    local pyro, soul = FX.Resolve("EX1_279", "fire"), FX.Resolve("EX1_308", "shadow")
    check(pyro.visual == "fireball" and pyro.fxScale > 1 and pyro.flight > FX.Resolve("CS2_029", "fire").flight, "Pyroblast is bigger and slower")
    check(soul.visual == "fireball" and soul.tint and soul.tint[2] > soul.tint[1], "Soulfire has green neutral artwork")
    T.animTeardown(F, MU)

    -- Result-only spells still get a timeline-owned presentation and cleanly commit their new state.
    local transitions = {
        { card = "CS2_022", visual = "transform", result = "tf.o.12.45.0.CS2_121",
            over = { ob = "44.CS2_124.3.1.1.c,45.CS2_121.1.1.1.-" } },
        { card = "EX1_332", visual = "silence", result = "sil.o.12.45.0.-",
            over = { ob = "44.CS2_124.3.1.1.c,45.EX1_412.4.2.3.x" } },
        { card = "EX1_371", visual = "shield", result = "buff.o.12.45.0.-" },
        { card = "EX1_312", visual = "destroy", result = "dest.o.12.45.0.-",
            over = { ob = "44.CS2_124.3.1.1.c" }, death = "die.o.0.45.0.EX1_412" },
        { card = "EX1_581", visual = "bounce", result = "bnc.o.0.45.0.EX1_412",
            over = { ob = "44.CS2_124.3.1.1.c" } },
        { card = "CS1_113", visual = "control", result = "ctl.m.0.45.0.EX1_412",
            over = { ob = "44.CS2_124.3.1.1.c", mb = "40.CS2_172.3.2.2.a,41.CS2_179.3.5.5.t,42.CS2_168.2.1.1.a,45.EX1_412.4.2.3.-" } },
    }
    for _, tr in ipairs(transitions) do
        F, MU, X = T.animSetup()
        base = T.baselineLevels(MU)
        local events = { "play.m.12.45.0." .. tr.card, tr.result }
        if tr.death then table.insert(events, tr.death) end
        X.push(T.nextE(events, tr.over))
        MU.AnimTick(0.02)
        local _, beat = AN.Current()
        check(beat and beat.visual == tr.visual and beat.fxItem and beat.fxItem.manual, tr.visual .. " result-only spell owns clock")
        check(FX.last and FX.last.goals == 1 and not FX.last.error, tr.visual .. " targets existing card")
        T.runTicks(MU, 2)
        check(not AN.Busy(), tr.visual .. " commits result state")
        if tr.visual == "transform" then check(MU.ui.o.minions[2].cardId == "CS2_121", "transform reveals replacement card") end
        if tr.visual == "destroy" then check(not MU.ui.o.minions[2].frame:IsShown(), "destroy removes dead target") end
        if tr.visual == "bounce" then check(not MU.ui.o.minions[2].frame:IsShown(), "returned target leaves board") end
        if tr.visual == "control" then check(MU.ui.m.minions[4].entity == 45 and not MU.ui.o.minions[2].frame:IsShown(), "controlled card commits to caster's board") end
        T.checkAtRest(MU, base, tr.visual .. " result completion")
        T.animTeardown(F, MU)
    end

    -- A pair of summoned cards must use distinct future row positions, not collapse at the side center.
    F, MU, X = T.animSetup()
    base = T.baselineLevels(MU)
    local afterSummon = { mb = "40.CS2_172.3.2.2.a,41.CS2_179.3.5.5.t,42.CS2_168.2.1.1.a,90.CS2_mirror.0.2.2.t,91.CS2_mirror.0.2.2.t" }
    X.push(T.nextE({ "play.m.12.0.0.CS2_027", "sum.m.0.90.4.CS2_mirror", "sum.m.0.91.5.CS2_mirror" }, afterSummon))
    MU.AnimTick(0.02)
    local _, summonBeat = AN.Current()
    check(summonBeat and summonBeat.visual == "summon" and summonBeat.fxItem and summonBeat.fxItem.manual, "summon owns clock")
    local summonCtx = summonBeat and MU.EffectContext(summonBeat)
    check(summonCtx and table.getn(summonCtx.targets) == 2 and summonCtx.targets[1].id == 90 and summonCtx.targets[2].id == 91
        and summonCtx.targets[1].x ~= summonCtx.targets[2].x and not summonCtx.targets[1].missing,
        "new summons receive distinct entity anchors")
    local xs = MU.RowPositions(5, MU.MINION_W, MU.MINION_GAP, MU.geom.bw, MU.CenterX(MU.geom.bw))
    check(summonCtx and summonCtx.targets[1] and math.abs(summonCtx.targets[1].x - xs[4] - MU.MINION_W/2) < 0.01,
        "summon cue aligns with final board slot")
    check(FX.last and FX.last.goals == 2, "summon renderer preserves both future targets")
    T.runTicks(MU, 2)
    check(MU.ui.m.minions[4].entity == 90 and MU.ui.m.minions[5].entity == 91, "new minions appear at their destination slots")
    T.checkAtRest(MU, base, "summon completion")
    local ctxBad = MU.EffectContext({ kind = "effect", family = "summon", visual = "summon", side = "m", targets = {92},
        sides = {[92]="m"}, slots = {[92]=1000}, source = 0 })
    check(ctxBad.targets[1] and ctxBad.targets[1].missing, "invalid future slot retains bounded fallback")
    T.animTeardown(F, MU)

    -- Real card profiles can still audition the optional native layer separately.
    F, MU = T.animSetup()
    H.Slash("fx layer 2d")
    H.Slash("fx card CS2_024")
    local b = FX.bench
    check(b and table.getn(b.items) > 0 and table.getn(T.shownModels(F)) == 0, "card bench previews 2D profile")
    H.Slash("fx layer model")
    H.Slash("fx card CS2_024")
    local leases = 0
    for _, h in ipairs(b.tex) do if h.used then leases = leases + 1 end end
    check(leases == 0 and table.getn(T.shownModels(F)) > 0, "card bench isolates native layer")
    H.Slash("fx layer both")
    H.Slash("fx card CS2_024")
    check(table.getn(b.items) > 0 and table.getn(T.shownModels(F)) > 0, "card bench combines layers")
    H.Slash("fx stop")
    leases = 0
    for _, h in ipairs(b.tex) do if h.used then leases = leases + 1 end end
    check(leases == 0 and table.getn(b.items) == 0 and FX.PoolStats().used == 0, "bench stop returns both pools")
    T.animTeardown(F, MU)
end
