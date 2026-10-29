-- Combined results, invisible secrets and targetless resource spells exercise the real timeline.
return function(T)
    local H, check, AN = T.H, T.check, HsMatchAnim
    local FX = H.MatchFx
    local function beforeHit(MU, flight)
        local n = math.floor((flight - 0.001) / 0.01)
        for _ = 1, n do MU.AnimTick(0.01) end
        MU.AnimTick(flight - 0.001 - n*0.01)
    end
    local cases = {
        { card="CS1_112", events={"dmg.o.12.45.2.-","heal.m.12.40.2.-"}, expected={[45]="nova",[40]="heal"} },
        { card="EX1_624", events={"dmg.o.12.45.5.-","heal.m.12.1.5.-"}, expected={[45]="impact",[1]="heal"} },
        { card="EX1_309", events={"dest.o.12.45.0.-","heal.m.12.1.3.-"}, expected={[45]="destroy",[1]="heal"} },
        { card="EX1_596", events={"dmg.o.12.45.2.-"}, expected={[45]="impact"} },
        { card="EX1_596", events={"buff.m.12.40.2.-"}, expected={[40]="buff"} },
        { card="EX1_160", events={"buff.m.12.40.1.-"}, expected={[40]="buff"} },
        { card="EX1_160", events={"sum.m.0.90.4.EX1_160t"}, expected={[90]="summon"},
            over={mb="40.CS2_172.3.2.2.a,41.CS2_179.3.5.5.t,42.CS2_168.2.1.1.a,90.EX1_160t.3.2.2.-"} },
        { card="EX1_275", events={"frz.o.12.44.0.-","frz.o.12.45.0.-","dmg.o.12.44.1.-","dmg.o.12.45.1.-"},
            expected={[44]="freeze",[45]="freeze"} },
        { card="EX1_245", events={"sil.o.12.45.0.-","dmg.o.12.45.1.-"}, expected={[45]="silence"} },
        { card="EX1_128", events={"buff.m.12.40.0.-","buff.m.12.41.0.-","buff.m.12.42.0.-"},
            expected={[40]="stealth",[41]="stealth",[42]="stealth"} },
    }
    for _, c in ipairs(cases) do
        local F, MU, X = T.animSetup()
        local base = T.baselineLevels(MU)
        F.time = F.time + 10
        local sound = table.getn(F.soundFiles) + table.getn(F.sounds)
        local events = {"play.m.12.0.0." .. c.card}
        for _, e in ipairs(c.events) do table.insert(events,e) end
        X.push(T.nextE(events,c.over))
        local profile = FX.Resolve(c.card,(HS_EffectProfiles.cardFamily or {})[c.card] or "generic")
        beforeHit(MU,profile.flight)
        local _, beat = AN.Current()
        check(beat and beat.kind=="effect" and beat.fxItem and beat.fxItem.manual,c.card .. " combined controller uses timeline")
        local ctx = beat and MU.EffectContext(beat)
        local expectedCount = 0
        for id, visual in pairs(c.expected) do
            expectedCount = expectedCount + 1
            check(beat and beat.targetVisuals and beat.targetVisuals[id]==visual,c.card .. " actual result selects " .. id .. ":" .. visual)
            local found = false
            for _, a in ipairs(ctx and ctx.targets or {}) do if a.id==id and a.visual==visual then found=true end end
            check(found,c.card .. " renderer receives result visual for " .. id)
            local ordinary = MU.AnchorOf(id)
            check(not ordinary or ordinary.visual==nil,c.card .. " does not contaminate ordinary anchor metadata")
        end
        check(FX.last and FX.last.goals==expectedCount and not FX.last.error,c.card .. " preserves every result target")
        check(table.getn(T.usedTexts(MU))==0,c.card .. " numbers wait for arrival")
        check(table.getn(F.soundFiles)+table.getn(F.sounds)==sound,c.card .. " sound waits for arrival")
        MU.AnimTick(0.002)
        check(table.getn(F.soundFiles)+table.getn(F.sounds)==sound+(profile.sound and 1 or 0),c.card .. " exactly one synchronized sound")
        for _, it in ipairs(MU.fxItems) do check(it.kind~="flash",c.card .. " mapped hits avoid rectangles") end
        if c.card=="CS1_112" then
            local hasRing, hasGlow = false,false
            for _, obj in ipairs(T.usedTextures(MU)) do
                local path = obj:GetTexture() or ""
                if obj:IsShown() and string.find(path,"neutral_ring",1,true) then hasRing=true end
                if obj:IsShown() and string.find(path,"neutral_glow",1,true) then hasGlow=true end
            end
            check(hasRing and hasGlow,"Holy Nova renders different artwork for damage and healing")
            check(table.getn(T.usedTexts(MU))==2,"Holy Nova shows both result numbers together")
        end
        T.runTicks(MU,2)
        T.checkAtRest(MU,base,c.card .. " combined completion")
        T.animTeardown(F,MU)
    end

    -- A full Holy Nova board must retain both outcomes under the existing eight-cue limit.
    local F,MU,X = T.animSetup()
    local fullEvents = {{type="play",side="m",source=12,target=0,amount=0,cardId="CS1_112",seq=1}}
    for i=1,8 do table.insert(fullEvents,{type="dmg",side="o",source=12,target=100+i,amount=2,seq=i+1}) end
    for i=1,8 do table.insert(fullEvents,{type="heal",side="m",source=12,target=200+i,amount=2,seq=i+9}) end
    local beats = AN.Build(fullEvents,HsMatch.game,HsMatch.game)
    local mixed = beats[1]
    local damage,healing = 0,0
    for _, id in ipairs(mixed and mixed.targets or {}) do
        if mixed.targetVisuals[id]=="nova" and mixed.sides[id]=="o" then damage=damage+1 end
        if mixed.targetVisuals[id]=="heal" and mixed.sides[id]=="m" then healing=healing+1 end
    end
    check(mixed and table.getn(mixed.targets)==8 and damage==4 and healing==4,"full-board Holy Nova balances damage and healing cues within eight leases")
    local hitCount = 0
    for _, b in ipairs(beats) do hitCount=hitCount+table.getn(b.hits or {}) end
    check(hitCount==16,"visual sampling retains all sixteen result numbers")
    T.animTeardown(F,MU)

    -- Outcomes from a subsequent cause must never inherit the previous spell's selected mode.
    F,MU,X = T.animSetup()
    X.push(T.nextE({"play.m.12.45.0.EX1_596","buff.m.12.40.2.-","trig.o.45.0.0.EX1_412","dmg.m.45.40.1.-"}))
    MU.AnimTick(0.01)
    local plan = AN.Current()
    local tagged, ordinary = false,false
    for _, b in ipairs(plan and plan.beats or {}) do
        if b.targetVisuals and b.targetVisuals[40]=="buff" then tagged=true end
        for _, hit in ipairs(b.hits or {}) do if hit.source==45 and not hit.effectVisual then ordinary=true end end
    end
    check(tagged and ordinary,"new cause clears conditional result attribution")
    MU.AnimSkip()
    T.animTeardown(F,MU)

    F,MU,X = T.animSetup()
    X.push(T.nextE({"play.m.12.45.0.EX1_596","buff.m.12.40.2.-","rev.o.60.40.0.EX1_609","dmg.m.60.40.4.-"}))
    MU.AnimTick(0.01)
    plan = AN.Current()
    ordinary = false
    for _, b in ipairs(plan and plan.beats or {}) do
        for _, hit in ipairs(b.hits or {}) do if hit.source==60 and not hit.effectVisual then ordinary=true end end
    end
    check(ordinary,"revealed secret results cannot inherit the previous spell profile")
    MU.AnimSkip()
    T.animTeardown(F,MU)

    -- Returning an enemy card points toward the enemy hand, even when the caster is friendly.
    for _, c in ipairs({{card="EX1_581",id=45,side="o"},{card="EX1_144",id=40,side="m"}}) do
        F,MU,X = T.animSetup()
        local target = MU.AnchorOf(c.id)
        local beat = {kind="effect",cardId=c.card,visual="bounce",family="generic",source=40,side="m",targets={c.id},sides={[c.id]=c.side}}
        FX.PlayEffect(beat,MU.EffectContext(beat))
        MU.FxTick(FX.Resolve(c.card,"generic").flight)
        MU.FxTick(0.2)
        local moved = false
        for _, obj in ipairs(T.usedTextures(MU)) do
            if obj:IsShown() and string.find(obj:GetTexture() or "","smoke_puff",1,true) then
                local pos = obj._.points[1]
                moved = pos and ((c.side=="o" and pos.y > -target.y) or (c.side=="m" and pos.y < -target.y))
            end
        end
        check(moved,c.card .. " return cue moves toward target owner's hand")
        MU.AnimSkip()
        T.animTeardown(F,MU)
    end

    -- Generic secret placement reveals no card id, and never borrows the previous cause's profile.
    F,MU,X = T.animSetup()
    local base = T.baselineLevels(MU)
    X.push(T.nextE({"play.o.80.0.0.-","sec.o.80.80.0.-"}))
    MU.AnimTick(0.02)
    local secretPlan, secretBeat = AN.Current()
    check(secretBeat and secretBeat.kind=="effect" and secretBeat.visual=="secret","hidden opponent secret gets an anonymous glyph")
    check(secretBeat and not secretBeat.cardId and FX.last and not FX.last.cardId,"secret glyph has no inferred card identity")
    local revealed = false
    for _, b in ipairs(secretPlan and secretPlan.beats or {}) do if b.kind=="reveal" then revealed=true end end
    check(not revealed,"hidden secret placement does not add a reveal")
    T.runTicks(MU,2)
    T.checkAtRest(MU,base,"anonymous secret completion")
    T.animTeardown(F,MU)

    -- Draw-only and mana-only spells have readable owner cues without targeting the concealed hand.
    for _, c in ipairs({{id="CS2_023",event="draw.m.0.90.0.CS2_029"},{id="EX1_169",event="crys.m.0.0.2.-"},
        {id="EX1_164",event="crys.m.0.0.2.-"},{id="EX1_164",event="draw.m.0.90.0.CS2_029"},
        {id="EX1_145"},{id="EX1_625"}}) do
        F,MU,X = T.animSetup()
        base = T.baselineLevels(MU)
        local events = {"play.m.12.0.0." .. c.id}
        if c.event then table.insert(events,c.event) end
        X.push(T.nextE(events))
        MU.AnimTick(0.02)
        local _, b = AN.Current()
        local ctx = b and MU.EffectContext(b)
        check(b and b.visual=="resource" and b.fxItem and b.fxItem.manual,c.id .. " targetless resource spell has an effect")
        check(ctx and ctx.src and ctx.src.kind=="hero" and ctx.src.side=="m",c.id .. " cue stays on owner hero")
        check(FX.last and FX.last.goals==1 and not FX.last.error,c.id .. " resource cue renders")
        T.runTicks(MU,2)
        T.checkAtRest(MU,base,c.id .. " resource completion")
        T.animTeardown(F,MU)
    end
end
