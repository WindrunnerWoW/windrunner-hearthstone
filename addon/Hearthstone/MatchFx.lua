-- Hearthstone addon: native WoW spell effects of the match animations (part B).
--
-- Installs MU.Fx.PlayEffect / MU.Fx.Stop / MU.Fx.Init (the hook of MatchUI.lua) and the /hs fx development
-- bench. An effect beat (spell, hero power, trigger, summon ...) is drawn in three layers; every one of them is
-- cosmetic and the beat reads without the others:
--   1. a native "Model" frame (WoW 1.12 frame type) that plays an installed spell model: a missile moves from the
--      source anchor to the target anchor, an impact or aura plays in place. A small pool of model frames is
--      created once (MU.Fx.Init, with the window); every model has a hard lifetime (FX.MAX_LIFE), is cleared and
--      returned to the pool when its run ends, and all of them end in MU.Fx.Stop (skip, hide, new match).
--      If a Model frame cannot be created or SetModel fails, the model layer is switched off for the session
--      (FX.modelsDisabled, /hs fx reset brings it back) and the beat goes on with the other two layers.
--   2. a 2D fallback from the pooled textures of MatchUI.lua: a coloured trail for missiles, and a family style
--      at the target (pulse, ring, flash, implode, rise, ripple, burst); area effects get one burst over the
--      affected rows and a ring on every target; destroy shakes the target. It is always shown.
--      CS2_029 is the reference exception: its complete 2D fireball uses shipped alpha textures instead of these
--      generic marks. Its flight clock is driven by the owning beat and its sound plays at arrival. Native models
--      can be auditioned independently on the bench; they are not required by this card's match effect.
--   3. a sound: PlaySoundFile for a path, PlaySound for a name; at most one per effect, and a minimum gap
--      between two sounds (FX.SOUND_GAP). /hs fx sound on|off (HearthstoneDB.fxSound).
--
-- What plays comes from HS_EffectProfiles (MatchFxProfiles.lua, generated from data/fx/effect_profiles.json by
-- tools/fx/gen_effect_profiles.py): presets (model path, kind, scale, position, facing, duration, ...), the default
-- presets of every family, and per-card overrides (a card may also set its family: HsMatchAnim.CARD_FAMILY). The
-- mapping is presentation only. Model and sound paths point into the player's WoW data; every preset starts
-- verified = false until the owner has checked it in game; /hs fx models all|verified|off (HearthstoneDB.fxModels,
-- default all) chooses whether unverified presets play their model (the 2D layer always plays).
--
-- Model methods used (all guarded by a type check and pcall; none is needed): SetModel (required), ClearModel,
-- SetModelScale, SetPosition, SetFacing, SetCamera (preset.camera), SetLight (preset.light), SetSequence
-- (preset.sequence).
--
-- Placement and size of a model (the "calibration"). A Model frame of the 1.12 client is not clipped to its rectangle,
-- and SetPosition(x, 0, 0) puts the model origin at the frame's BOTTOM-LEFT corner, not its centre; position units
-- are world units (x = depth, y = horizontal, z = vertical) and one unit covers a fixed number of screen pixels for a
-- frame of a given screen size. So every model frame has one fixed size ON THE SCREEN (cal.frame pixels: its design
-- size is cal.frame / GetEffectiveScale(), so the UI scale and the fit scale of the match window drop out), is
-- centred on its target, and the model is moved from the bottom-left corner to the middle with
--     y = (cal.frame / 2 - cal.ox) / cal.ky        z = (cal.frame / 2 - cal.oz) / cal.kz
-- (cal.ox, cal.oz: where the origin appears for y = z = 0; cal.ky, cal.kz: screen pixels per unit). The preset's pos is
-- a small offset in the same units on top of that and cal.x is the depth. The size comes from the anchor: the model is
-- scaled so that its natural diameter (2 x preset.ext yards, or the default of its role) covers the wanted diameter
-- (impact about 1.25x the target frame, aura 1.1x, area burst about the area it covers, missile half a minion) with
-- cal.ppy screen pixels per yard; preset.scale (1 = default) only corrects it, preset.size (design pixels) overrides the
-- wanted diameter. The calibration is one global table (HearthstoneDB.fxCal, shipped defaults FX.CAL_DEFAULT until it
-- is saved) that the owner makes with /hs fx calibrate on the bench.
--
-- The bench (/hs fx help) is a small standalone frame with two test anchors, so it works with and without a
-- match. It uses the same layers through a "stage" (frame, texture pool, timed items), so what plays there is what
-- plays on the board.
--
-- Lua 5.0 / WoW 1.12: no '#', no select(), no '...', no '%'; SetScript handlers use this / arg1.

Hearthstone = Hearthstone or {}
local H = Hearthstone
local AN = HsMatchAnim
local MU = H.MatchUI
H.MatchFx = H.MatchFx or {}
local FX = H.MatchFx

local WHITE = "Interface\\Buttons\\WHITE8X8"
local RING = "Interface\\Buttons\\UI-ActionButton-Border"
local STAR = "Interface\\Cooldown\\star4"
local FIRE_CORE = "Interface\\Hearthstone\\Textures\\Effects\\fire_core"
local SOFT_GLOW = "Interface\\Hearthstone\\Textures\\Effects\\soft_glow"
local SPARK = "Interface\\Hearthstone\\Textures\\Effects\\spark"
local SHOCKWAVE = "Interface\\Hearthstone\\Textures\\Effects\\shockwave"
local NEUTRAL_CORE = "Interface\\Hearthstone\\Textures\\Effects\\neutral_core"
local NEUTRAL_GLOW = "Interface\\Hearthstone\\Textures\\Effects\\neutral_glow"
local NEUTRAL_SPARK = "Interface\\Hearthstone\\Textures\\Effects\\neutral_spark"
local NEUTRAL_RING = "Interface\\Hearthstone\\Textures\\Effects\\neutral_ring"
local FROST_SHARD = "Interface\\Hearthstone\\Textures\\Effects\\frost_shard"
local SLASH_ARC = "Interface\\Hearthstone\\Textures\\Effects\\slash_arc"
local LIGHTNING_ARC = "Interface\\Hearthstone\\Textures\\Effects\\lightning_arc"
local SMOKE_PUFF = "Interface\\Hearthstone\\Textures\\Effects\\smoke_puff"
local PORTAL_RING = "Interface\\Hearthstone\\Textures\\Effects\\portal_ring"
local SHIELD_SHELL = "Interface\\Hearthstone\\Textures\\Effects\\shield_shell"

-- ---------------------------------------------------------------- constants
FX.POOL = 6                    -- model frames per stage
FX.MAX_LIFE = 2.5              -- seconds: no model lives longer
FX.DEFAULT_SIZE = 96           -- model frame size in design pixels
FX.FLIGHT = 0.28               -- missile flight time (shortened to fit the beat)
FX.MAX_MISSILES = 3            -- missiles of one effect
FX.MAX_IMPACTS = 3             -- impact models of one single-target style effect
FX.MAX_PULSES = 8              -- 2D marks of one effect
FX.SOUND_GAP = 0.15            -- seconds between two sounds
FX.MAX_BENCH_ITEMS = 48
FX.BENCH_TEXTURES = 16
FX.FIREBALL_FLIGHT = 0.34
FX.FIREBALL_IMPACT = 0.32
FX.FIREBALL_SPARKS = 3
FX.benchLayer = "2d"
FX.COLOR_DEFAULT = { 1, 1, 0.7 }
-- seconds the 2D layer of a family lasts (bounded: always <= FX.MAX_LIFE)
FX.DUR = { fire = 0.5, frost = 0.55, arcane = 0.5, shadow = 0.6, holy = 0.6, nature = 0.45, heal = 0.7, area = 0.6,
    buff = 0.55, silence = 0.7, transform = 0.6, summon = 0.55, destroy = 0.6, secret = 0.7, generic = 0.4 }
-- the 2D styles a profile may name (fallback = "...")
FX.STYLES = { pulse = true, ring = true, flash = true, implode = true, rise = true, ripple = true, burst = true }
FX.VISUALS = { fireball = true, bolt = true, impact = true, slash = true, nova = true, rain = true, heal = true, buff = true,
    lightning = true, silence = true, transform = true, summon = true, destroy = true, shield = true,
    bounce = true, control = true, secret = true, drain = true, resource = true,
    volley = true, freeze = true, stealth = true, curse = true }
FX.MAX_VISUAL_TARGETS = 8

FX.modelsDisabled = false      -- the model layer is off for this session (a failure)
FX.modelError = nil
FX.broken = false              -- PlayEffect failed repeatedly: the 2D pulse of MatchUI.lua is used
FX.lastError = nil
FX.lastSound = nil
FX.soundOff = false
FX.modelMode = "all"
FX.runs = {}                   -- running model runs of every stage
FX.stats = { sounds = 0, throttled = 0, models = 0, missiles = 0, impacts = 0, errors = 0 }
FX.last = nil                  -- what the last effect started (tests, /hs fx status)

-- ---------------------------------------------------------------- settings
function FX.SoundOn()
    if FX.soundOff then
        return false
    end
    if H.db and H.db.fxSound == false then
        return false
    end
    return true
end

function FX.SetSound(on)
    FX.soundOff = not on
    if H.db then
        if on then
            H.db.fxSound = nil
        else
            H.db.fxSound = false
        end
    end
end

-- "all" (default: unverified presets play their model too) | "verified" | "off"
function FX.ModelMode()
    local m = H.db and H.db.fxModels
    if m == "off" or m == "verified" or m == "all" then
        return m
    end
    return FX.modelMode
end

function FX.SetModelMode(mode)
    if mode ~= "all" and mode ~= "verified" and mode ~= "off" then
        return false
    end
    FX.modelMode = mode
    if H.db then
        if mode == "all" then
            H.db.fxModels = nil
        else
            H.db.fxModels = mode
        end
    end
    return true
end

-- ---------------------------------------------------------------- placement calibration
local function say(msg)
    if H.Print then
        H.Print(msg)
    end
end

-- Shipped defaults: a guess from the first in-game look (a lightning missile in a frame of about 110 screen pixels was
-- about 200 pixels wide for a model of 1.3 yards: about 150-170 pixels per yard; the origin at the bottom-left corner;
-- +y to the right as seen from the camera, +z up).
FX.CAL_DEFAULT = { x = 0, frame = 128, ox = 0, oz = 0, ky = 160, kz = 160, ppy = 160 }
local CAL_KEYS = { "x", "frame", "ox", "oz", "ky", "kz", "ppy" }
FX.EXT_MIN = 0.3               -- yards: the natural radius of a model is kept inside this range
FX.EXT_MAX = 12
FX.EXT_ROLE = { missile = 0.6, impact = 1.8, aura = 1.8, area = 8 }   -- radius when a preset has no ext
FX.SCALE_MIN = 0.01            -- SetModelScale is kept inside this range
FX.SCALE_MAX = 12
-- wanted on-screen diameter relative to the anchor (width or height, the larger one)
FX.SIZE = { impact = 1.25, aura = 1.1, area = 1.0, missile = 0.5 }
FX.MISSILE_REF = 92            -- a missile is half of a minion (design pixels), at most
FX.DIAM_MIN = 24               -- design pixels
FX.DIAM_MAX = 420
-- the calibration marker: a compact sphere that plays once and is replayed (Spells\Fireball_Missile_Low: 51 vertices)
FX.MARKER = { model = "Spells\\Fireball_Missile_Low.mdx", ext = 0.52, dur = 1.0, diam = 36 }
FX.hinted = false              -- the "not calibrated" hint was printed in this session

local function isNum(v)
    return type(v) == "number" and v == v and v > -1e9 and v < 1e9
end

local function inRange(v, lo, hi)
    return isNum(v) and v >= lo and v <= hi
end

local function validCal(c)
    if type(c) ~= "table" then
        return false
    end
    for i = 1, table.getn(CAL_KEYS) do
        if not isNum(c[CAL_KEYS[i]]) then
            return false
        end
    end
    return inRange(math.abs(c.ky), 10, 5000) and inRange(math.abs(c.kz), 10, 5000) and inRange(c.ppy, 10, 5000)
        and inRange(c.frame, 32, 1024) and inRange(c.x, -50, 50) and inRange(c.ox, -5000, 5000) and inRange(c.oz, -5000, 5000)
end

local function copyCal(c)
    local out = {}
    for i = 1, table.getn(CAL_KEYS) do
        out[CAL_KEYS[i]] = c[CAL_KEYS[i]]
    end
    return out
end

-- true when the owner saved a calibration (HearthstoneDB.fxCal, valid)
function FX.CalSaved()
    return H.db ~= nil and validCal(H.db.fxCal)
end

-- The calibration in use: the saved one, or the shipped defaults. Do not change the returned table.
function FX.Cal()
    if H.db and validCal(H.db.fxCal) then
        return H.db.fxCal
    end
    return FX.CAL_DEFAULT
end

-- Prints "Effects are not calibrated yet" once per session while nothing is saved. True when it printed.
function FX.Hint()
    if FX.hinted or FX.CalSaved() then
        return false
    end
    FX.hinted = true
    say("Effects are not calibrated yet: /hs fx calibrate")
    return true
end

-- Design size of a model frame whose effective scale is e: it is cal.frame pixels on the screen whatever the scale.
function FX.FrameSide(cal, e)
    if not isNum(e) or e <= 0 then
        e = 1
    end
    return cal.frame / e
end

-- The model units (y, z) that put the model origin at the pixel (px, pz) of the frame, counted in screen pixels from the
-- frame's bottom-left corner (to the right and up).
function FX.UnitsFor(cal, px, pz)
    return (px - cal.ox) / cal.ky, (pz - cal.oz) / cal.kz
end

-- The pixel (from the bottom-left corner of the frame) where the origin appears for the units y, z.
function FX.PixelFor(cal, y, z)
    return cal.ox + cal.ky * y, cal.oz + cal.kz * z
end

-- The units that centre a model on its frame (the frame is centred on the target).
function FX.CenterUnits(cal)
    return FX.UnitsFor(cal, cal.frame / 2, cal.frame / 2)
end

-- SetPosition arguments of a preset: the depth, and the centring units plus the preset's small offset pos = { x, y, z }.
function FX.Position(cal, pos)
    pos = pos or {}
    local y, z = FX.CenterUnits(cal)
    return cal.x + (tonumber(pos[1]) or 0), y + (tonumber(pos[2]) or 0), z + (tonumber(pos[3]) or 0)
end

-- Natural radius of a preset's model in yards (preset.ext, else the default of the role), kept in a sane range.
function FX.Extent(p, role)
    local r = p and tonumber(p.ext)
    if not r or r <= 0 then
        r = FX.EXT_ROLE[role] or FX.EXT_ROLE.impact
    end
    if r < FX.EXT_MIN then
        r = FX.EXT_MIN
    elseif r > FX.EXT_MAX then
        r = FX.EXT_MAX
    end
    return r
end

-- SetModelScale for a preset that should cover `diam` design pixels on a frame of effective scale e: the model of
-- 2 x ext yards covers 2 x ext x cal.ppy screen pixels at scale 1. preset.scale (1 = default) corrects it.
function FX.ModelScale(cal, p, role, diam, e)
    if not isNum(e) or e <= 0 then
        e = 1
    end
    if type(p.size) == "number" and p.size > 0 then
        diam = p.size
    end
    local s = diam * e / (2 * FX.Extent(p, role) * cal.ppy)
    local corr = tonumber(p.scale)
    if corr and corr > 0 then
        s = s * corr
    end
    if s < FX.SCALE_MIN then
        s = FX.SCALE_MIN
    elseif s > FX.SCALE_MAX then
        s = FX.SCALE_MAX
    end
    return s
end

-- Two-point solve. A point is { px, pz (where the target is, screen pixels from the frame's bottom-left corner), y, z (the
-- model units that put the origin there, found by the owner) }. Returns a copy of `base` with ox, oz, ky, kz changed, and
-- whether the y and z scales were solved (needs two points that differ in both pixels and units; otherwise the scale of
-- `base` stays and only the offset follows p1 / p2).
function FX.Solve(base, p1, p2)
    local c = copyCal(base)
    local solvedY, solvedZ = false, false
    if p2 then
        local dy, dz = p2.y - p1.y, p2.z - p1.z
        local dpx, dpz = p2.px - p1.px, p2.pz - p1.pz
        if math.abs(dy) > 1e-6 and math.abs(dpx) > 1e-6 and inRange(math.abs(dpx / dy), 10, 5000) then
            c.ky = dpx / dy
            solvedY = true
        end
        if math.abs(dz) > 1e-6 and math.abs(dpz) > 1e-6 and inRange(math.abs(dpz / dz), 10, 5000) then
            c.kz = dpz / dz
            solvedZ = true
        end
    end
    local ox, oz = p1.px - c.ky * p1.y, p1.pz - c.kz * p1.z
    if p2 then
        ox = (ox + p2.px - c.ky * p2.y) / 2
        oz = (oz + p2.pz - c.kz * p2.z) / 2
    end
    c.ox, c.oz = ox, oz
    return c, solvedY, solvedZ
end

-- ---------------------------------------------------------------- profiles
local function profiles()
    local P = HS_EffectProfiles
    if type(P) ~= "table" then
        return { presets = {}, families = {}, cards = {}, cardFamily = {} }
    end
    return P
end

local ROLES = { "missile", "impact", "area" }

-- Copies the card -> family overrides of the profiles into HsMatchAnim.CARD_FAMILY (a table another file owns:
-- an entry that is already set stays).
function FX.InstallCardFamilies()
    local P = profiles()
    if AN and type(P.cardFamily) == "table" then
        for id, fam in pairs(P.cardFamily) do
            if AN.CARD_FAMILY[id] == nil then
                AN.CARD_FAMILY[id] = fam
            end
        end
    end
end

function FX.FamilyDuration(family)
    local d = FX.DUR[family or "generic"] or FX.DUR.generic
    if d > FX.MAX_LIFE then
        d = FX.MAX_LIFE
    end
    return d
end

-- What an effect of this family (and card) plays: { missile, impact, area (presets or nil), fallback (2D style),
-- trail (bool), sound (string or nil), color (r,g,b of a preset tint or nil) }. Unknown cards and families, and
-- presets that do not exist, degrade to nothing: the 2D layer plays.
function FX.Resolve(cardId, family)
    local P = profiles()
    local fams = P.families or {}
    local F = fams[family or "generic"] or fams.generic or {}
    local C = nil
    if cardId and P.cards then
        C = P.cards[cardId]
    end
    local r = { family = family or "generic", fallback = "pulse", trail = false }
    for i = 1, 3 do
        local role = ROLES[i]
        local key = F[role]
        if C and C[role] ~= nil then
            key = C[role]             -- false: no preset for this role
        end
        if key then
            local p = P.presets and P.presets[key]
            if type(p) == "table" and type(p.model) == "string" then
                local isMissile = (p.kind == "missile")
                if (role == "missile") == isMissile then
                    r[role] = p
                    r[role .. "Key"] = key
                end
            end
        end
    end
    local fb = F.fallback
    if C and C.fallback then
        fb = C.fallback
    end
    if fb and FX.STYLES[fb] then
        r.fallback = fb
    end
    local trail = F.trail
    if C and C.trail ~= nil then
        trail = C.trail
    end
    r.trail = trail and true or false
    local sound = C and C.sound
    if not sound then
        local order = { r.missile, r.impact, r.area }
        for i = 1, 3 do
            if not sound and order[i] and order[i].sound then
                sound = order[i].sound
            end
        end
    end
    r.sound = sound or F.sound
    r.visual = C and C.visual or nil
    r.flight = C and C.flight or nil
    r.tint = C and C.tint or nil
    r.fxScale = C and C.fxScale or nil
    r.resultVisuals = (C and C.resultVisuals) or F.resultVisuals or nil
    local tint = (r.impact and r.impact.tint) or (r.area and r.area.tint) or (r.missile and r.missile.tint)
    r.color = tint
    return r
end

-- ---------------------------------------------------------------- sound
-- Plays a sound: a path (it contains a slash) with PlaySoundFile, a name with PlaySound. Returns true when it
-- was played. Off, empty, and two sounds closer than FX.SOUND_GAP are not played.
function FX.PlaySound(spec)
    if type(spec) ~= "string" or spec == "" or not FX.SoundOn() then
        return false
    end
    local now = GetTime and GetTime() or 0
    if FX.lastSound and now >= FX.lastSound and now - FX.lastSound < FX.SOUND_GAP then
        FX.stats.throttled = FX.stats.throttled + 1
        return false
    end
    local ok = false
    if string.find(spec, "[\\/]") then
        if type(PlaySoundFile) == "function" then
            ok = pcall(PlaySoundFile, spec)
        end
    elseif type(PlaySound) == "function" then
        ok = pcall(PlaySound, spec)
    end
    if ok then
        FX.lastSound = now
        FX.stats.sounds = FX.stats.sounds + 1
    end
    return ok
end

-- ---------------------------------------------------------------- model frames (layer 1)
local function callSafe(f, name, a, b, c)
    local fn = f[name]
    if type(fn) ~= "function" then
        FX.lastError = "Model:" .. name .. " unavailable"
        return false
    end
    local ok, err = pcall(fn, f, a, b, c)
    if not ok then FX.lastError = "Model:" .. name .. " failed: " .. tostring(err) end
    return ok
end

local release, finishRun

-- Switches the model layer off for the session and returns every model to its pool.
function FX.Disable(reason)
    FX.modelsDisabled = true
    FX.modelError = tostring(reason)
    FX.StopAll()
end

function FX.Enable()
    FX.modelsDisabled = false
    FX.modelError = nil
    FX.broken = false
    FX.lastError = nil
end

-- The model pool of a stage (built once; rebuilt when the stage's parent frame is a new one). nil when the model
-- layer is off or no frame can be made.
local function ensurePool(stage)
    if FX.modelsDisabled or not CreateFrame then
        return nil
    end
    local parent = stage.parent()
    if not parent then
        return nil
    end
    local pool = stage.mpool
    if pool and pool.parent == parent then
        return pool
    end
    pool = { parent = parent }
    for i = 1, FX.POOL do
        local ok, f = pcall(CreateFrame, "Model", nil, parent)
        if not ok or type(f) ~= "table" then
            for k = 1, table.getn(pool) do
                pool[k].frame:Hide()
            end
            FX.Disable("cannot create a Model frame: " .. tostring(f))
            return nil
        end
        f:SetWidth(FX.DEFAULT_SIZE)
        f:SetHeight(FX.DEFAULT_SIZE)
        f:Hide()
        pool[i] = { frame = f, used = false }
    end
    stage.mpool = pool
    return pool
end

local function acquire(stage)
    local pool = ensurePool(stage)
    if not pool then
        return nil
    end
    for i = 1, table.getn(pool) do
        if not pool[i].used then
            pool[i].used = true
            return pool[i]
        end
    end
    return nil
end

-- Clears a model and gives its frame back.
release = function(h)
    if not h or not h.used then
        return
    end
    h.used = false
    local f = h.frame
    callSafe(f, "ClearModel")
    f:Hide()
    f:ClearAllPoints()
end

-- Sets a preset on a pooled model frame. False when the model could not be set (the layer is then disabled).
-- role: "missile" | "impact" | "aura" | "area"; diam: the wanted on-screen diameter in design pixels (the anchor decides).
-- over (the calibration marker only) = { scale, x, y, z } replaces the derived scale and position.
local function applyPreset(h, p, role, diam, over)
    local f = h.frame
    local cal = FX.Cal()
    local e = 1
    if type(f.GetEffectiveScale) == "function" then
        local okE, v = pcall(f.GetEffectiveScale, f)
        if okE and isNum(v) and v > 0 then
            e = v
        end
    end
    local size = FX.FrameSide(cal, e)      -- cal.frame screen pixels, whatever the UI scale and the window's fit scale
    f:SetWidth(size)
    f:SetHeight(size)
    if type(f.SetModel) ~= "function" then
        FX.Disable("Model:SetModel does not exist")
        return false
    end
    local ok, err = pcall(f.SetModel, f, p.model)
    if not ok then
        FX.lastError = "SetModel(" .. tostring(p.model) .. ") failed: " .. tostring(err)
        FX.Disable("SetModel(" .. tostring(p.model) .. ") failed: " .. tostring(err))
        return false
    end
    local scale, px, py, pz
    if over then
        scale, px, py, pz = over.scale, over.x, over.y, over.z
    else
        scale = FX.ModelScale(cal, p, role or p.kind, diam or 0, e)
        px, py, pz = FX.Position(cal, p.pos)
    end
    callSafe(f, "SetModelScale", scale)
    callSafe(f, "SetPosition", px, py, pz)
    h.scale, h.px, h.py, h.pz = scale, px, py, pz
    FX.lastPlacement = { scale = scale, x = px, y = py, z = pz }
    callSafe(f, "SetFacing", p.facing or 0)
    if p.camera then
        callSafe(f, "SetCamera", p.camera)
    end
    if p.light and type(f.SetLight) == "function" then
        pcall(f.SetLight, f, 1, 0, 0, 0, -1, 1, 1, 1, 1, 0, 1, 1, 1)
    end
    if p.sequence then
        callSafe(f, "SetSequence", p.sequence)
    end
    return true
end

local function placeModel(stage, h, x, y)
    local f = h.frame
    f:ClearAllPoints()
    f:SetPoint("CENTER", stage.board(), "TOPLEFT", x, -y)
end

local function runUpdate(it)
    local run = it.run
    if run.done then
        return
    end
    local sp = run.spec
    local t = it.t
    if run.m then
        if t < sp.flight then
            local q = t / sp.flight
            q = q * q
            placeModel(run.stage, run.m, sp.x0 + (sp.x1 - sp.x0) * q, sp.y0 + (sp.y1 - sp.y0) * q)
            if not run.mShown then
                run.m.frame:Show()
                run.mShown = true
            end
        else
            release(run.m)
            run.m = nil
        end
    end
    if run.i and not run.iShown and t >= sp.flight - 0.0001 then
        placeModel(run.stage, run.i, sp.x1, sp.y1)
        run.i.frame:Show()
        run.iShown = true
    end
end

local function runFinish(it)
    finishRun(it.run)
end

-- Ends a run: its models go back to the pool (also from a skip or Stop). Safe to call twice.
finishRun = function(run)
    if run.done then
        return
    end
    run.done = true
    release(run.m)
    release(run.i)
    run.m, run.i = nil, nil
    local runs = FX.runs
    for i = table.getn(runs), 1, -1 do
        if runs[i] == run then
            table.remove(runs, i)
        end
    end
end

-- Starts the model(s) of one effect: spec = { missile = preset or nil, impact = preset or nil, x0, y0 (missile
-- start), x1, y1 (arrival or impact point), flight (seconds the missile flies; the impact starts then) }. The
-- run is one timed item of the stage, so its frames are returned when it ends, is cleared, or the stage stops.
-- Returns the run, or nil when no model could be started (the 2D layer is unaffected).
local function startModels(stage, spec)
    spec.flight = spec.flight or 0
    local m, i = nil, nil
    if spec.missile then
        m = acquire(stage)
        if m and not applyPreset(m, spec.missile, "missile", spec.mDiam) then
            release(m)
            m = nil
        end
    end
    if spec.impact and not FX.modelsDisabled then
        i = acquire(stage)
        if i and not applyPreset(i, spec.impact, spec.iRole or "impact", spec.iDiam, spec.iOver) then
            release(i)
            i = nil
        end
    end
    if not m and not i then
        return nil
    end
    local life = spec.flight
    if i then
        life = life + (spec.impact.dur or 0.5)
    elseif m then
        life = life + 0.05
    end
    if life > FX.MAX_LIFE then
        life = FX.MAX_LIFE
    end
    if life < 0.05 then
        life = 0.05
    end
    local run = { stage = stage, m = m, i = i, spec = spec, done = false }
    table.insert(FX.runs, run)
    if m then
        FX.stats.missiles = FX.stats.missiles + 1
    end
    if i then
        FX.stats.impacts = FX.stats.impacts + 1
    end
    FX.stats.models = FX.stats.models + 1
    run.item = stage.add({ dur = life, kind = "model", run = run, update = runUpdate, finish = runFinish })
    if not run.item then
        finishRun(run)
        return nil
    end
    if not spec.marker then
        FX.Hint()                    -- once per session while the owner has not calibrated the placement
    end
    return run
end

local function stopStage(stage)
    local runs = FX.runs
    for i = table.getn(runs), 1, -1 do
        local run = runs[i]
        if run and run.stage == stage then
            local item = run.item
            finishRun(run)
            if item and stage.remove then
                stage.remove(item)
            end
        end
    end
    local pool = stage.mpool
    if pool then
        for i = 1, table.getn(pool) do
            release(pool[i])
        end
    end
end

-- ---------------------------------------------------------------- 2D layer (layer 2)
local function clamp01(v)
    if v < 0 then
        return 0
    elseif v > 1 then
        return 1
    end
    return v
end

local function spriteUpdate(it)
    local s = it.s
    local lt = it.t - (s.delay or 0)
    if lt < 0 then
        return
    end
    local q = lt / s.dur
    if q > 1 then
        q = 1
    end
    local e = q
    if s.ease == "in" then
        e = q * q
    elseif s.ease == "out" then
        e = 1 - (1 - q) * (1 - q)
    end
    local w = s.w0 + (s.w1 - s.w0) * e
    local h = s.h0 + (s.h1 - s.h0) * e
    if w < 1 then
        w = 1
    end
    if h < 1 then
        h = 1
    end
    local x = s.x0 + ((s.x1 or s.x0) - s.x0) * e
    local y = s.y0 + ((s.y1 or s.y0) - s.y0) * e
    local obj = it.tex.obj
    it.stage.place(obj, x, y, w, h)
    local alpha
    if s.fade == "end" then
        if q < 0.7 then
            alpha = 1
        else
            alpha = (1 - q) / 0.3
        end
    elseif s.fade == "inout" then
        alpha = math.sin(math.pi * q)
    else
        alpha = 1 - q
    end
    obj:SetAlpha(clamp01(alpha))
    if not it.shown then
        obj:Show()
        it.shown = true
    end
end

local function spriteFinish(it)
    it.stage.free(it.tex)
end

-- One textured mark that moves / grows / fades: s = { tex, r, g, b, a (vertex alpha), x0, y0, x1, y1, w0, h0, w1,
-- h1, delay, dur, ease ("in"|"out"), fade ("out"|"end"|"inout") }. Returns the item, or nil (no free texture).
local function sprite(stage, s)
    local h = stage.alloc()
    if not h then
        return nil
    end
    local o = h.obj
    o:SetTexture(s.tex or WHITE)
    o:SetVertexColor(s.r, s.g, s.b, s.a or 1)
    o:SetAlpha(0)
    o:Hide()
    return stage.add({ dur = (s.delay or 0) + s.dur, kind = "fx2d", tex = h, s = s, stage = stage,
        update = spriteUpdate, finish = spriteFinish })
end

local function side(a)
    local m = a.w
    if a.h > m then
        m = a.h
    end
    return m
end

-- The on-screen diameter (design pixels of the stage) a model of this role should cover at the anchor a: an impact about
-- 1.25x the target frame, an aura 1.1x, an area burst about the area it covers (a = the bounds of the targets), a
-- missile half of a minion. Always inside FX.DIAM_MIN .. FX.DIAM_MAX.
function FX.Diameter(role, a)
    local m = side(a)
    local d
    if role == "missile" then
        if m > FX.MISSILE_REF then
            m = FX.MISSILE_REF
        end
        d = m * FX.SIZE.missile
    else
        d = m * (FX.SIZE[role] or FX.SIZE.impact)
    end
    if d < FX.DIAM_MIN then
        d = FX.DIAM_MIN
    elseif d > FX.DIAM_MAX then
        d = FX.DIAM_MAX
    end
    return d
end

-- The family styles at an anchor a. dur: seconds, delay: seconds before it starts.
local STYLE = {}

STYLE.pulse = function(stage, a, c, dur, delay)
    sprite(stage, { tex = WHITE, r = c[1], g = c[2], b = c[3], a = 0.7, x0 = a.x, y0 = a.y, w0 = a.w * 0.7, h0 = a.h * 0.7,
        w1 = a.w * 1.2, h1 = a.h * 1.2, delay = delay, dur = dur, ease = "out" })
end

STYLE.ring = function(stage, a, c, dur, delay)
    local m = side(a)
    sprite(stage, { tex = RING, r = c[1], g = c[2], b = c[3], x0 = a.x, y0 = a.y, w0 = m * 0.6, h0 = m * 0.6, w1 = m * 1.7,
        h1 = m * 1.7, delay = delay, dur = dur, ease = "out" })
    sprite(stage, { tex = WHITE, r = c[1], g = c[2], b = c[3], a = 0.25, x0 = a.x, y0 = a.y, w0 = a.w, h0 = a.h, w1 = a.w,
        h1 = a.h, delay = delay, dur = dur * 0.7 })
end

STYLE.flash = function(stage, a, c, dur, delay)
    local m = side(a)
    sprite(stage, { tex = WHITE, r = c[1], g = c[2], b = c[3], a = 0.6, x0 = a.x, y0 = a.y, w0 = a.w + 8, h0 = a.h + 8,
        w1 = a.w + 8, h1 = a.h + 8, delay = delay, dur = dur * 0.6 })
    sprite(stage, { tex = RING, r = c[1], g = c[2], b = c[3], x0 = a.x, y0 = a.y, w0 = m * 0.8, h0 = m * 0.8, w1 = m * 1.4,
        h1 = m * 1.4, delay = delay, dur = dur, ease = "out" })
end

STYLE.implode = function(stage, a, c, dur, delay)
    local m = side(a)
    sprite(stage, { tex = RING, r = c[1], g = c[2], b = c[3], x0 = a.x, y0 = a.y, w0 = m * 2, h0 = m * 2, w1 = m * 0.4,
        h1 = m * 0.4, delay = delay, dur = dur * 0.7, ease = "in", fade = "end" })
    sprite(stage, { tex = WHITE, r = c[1], g = c[2], b = c[3], a = 0.5, x0 = a.x, y0 = a.y, w0 = a.w, h0 = a.h, w1 = a.w,
        h1 = a.h, delay = delay + dur * 0.6, dur = dur * 0.4 })
end

STYLE.rise = function(stage, a, c, dur, delay)
    local m = side(a)
    sprite(stage, { tex = WHITE, r = c[1], g = c[2], b = c[3], a = 0.45, x0 = a.x, y0 = a.y + a.h * 0.2, x1 = a.x,
        y1 = a.y - a.h * 0.35, w0 = a.w * 0.6, h0 = a.h * 0.5, w1 = a.w * 0.4, h1 = a.h * 0.7, delay = delay, dur = dur,
        fade = "inout" })
    sprite(stage, { tex = RING, r = c[1], g = c[2], b = c[3], x0 = a.x, y0 = a.y, w0 = m * 0.6, h0 = m * 0.6, w1 = m * 1.3,
        h1 = m * 1.3, delay = delay, dur = dur, ease = "out" })
end

STYLE.ripple = function(stage, a, c, dur, delay)
    local m = side(a)
    for k = 0, 1 do
        sprite(stage, { tex = RING, r = c[1], g = c[2], b = c[3], x0 = a.x, y0 = a.y, w0 = m * 0.5, h0 = m * 0.5,
            w1 = m * 1.8, h1 = m * 1.8, delay = delay + k * 0.18, dur = dur * 0.7, ease = "out" })
    end
end

STYLE.burst = function(stage, a, c, dur, delay)
    local m = side(a)
    sprite(stage, { tex = STAR, r = c[1], g = c[2], b = c[3], x0 = a.x, y0 = a.y, w0 = m * 0.5, h0 = m * 0.5, w1 = m * 1.8,
        h1 = m * 1.8, delay = delay, dur = dur, ease = "out" })
    sprite(stage, { tex = RING, r = c[1], g = c[2], b = c[3], x0 = a.x, y0 = a.y, w0 = m * 0.6, h0 = m * 0.6, w1 = m * 1.4,
        h1 = m * 1.4, delay = delay, dur = dur * 0.8, ease = "out" })
end

-- The glow head and two fading marks behind it that fly from anchor a to anchor t in `flight` seconds.
local function trail(stage, a, t, c, flight)
    local marks = { { 0, 28, 1 }, { 0.03, 16, 0.6 }, { 0.06, 10, 0.35 } }
    for k = 1, 3 do
        local mk = marks[k]
        sprite(stage, { tex = (k == 1) and STAR or WHITE, r = c[1], g = c[2], b = c[3], a = mk[3], x0 = a.x, y0 = a.y, x1 = t.x,
            y1 = t.y, w0 = mk[2], h0 = mk[2], w1 = mk[2], h1 = mk[2], delay = mk[1], dur = flight, ease = "in",
            fade = "end" })
    end
end

-- The first two leases are deliberately the moving core and impact ring. If a bounded pool is
-- nearly full, embers disappear before the readable projectile or its arrival cue.
local function fireball2D(stage, from, target, flight, sound, anchorSize, beat)
    local limit = FX.MAX_LIFE - FX.FIREBALL_IMPACT
    if flight > limit then flight = limit end
    if flight < 0.1 then flight = 0.1 end
    local core = stage.alloc()
    local ring = stage.alloc()
    if not core or not ring then
        if core then stage.free(core) end
        if ring then stage.free(ring) end
        FX.lastError = "fireball texture pool exhausted (core/impact reserved)"
        return false
    end
    local coreObj, ringObj = core.obj, ring.obj
    local coreSize = math.max(22, math.min(46, anchorSize * 0.42))
    local waveSize = math.max(42, math.min(160, anchorSize * 1.6))
    local function setTexture(obj, path)
        local ok, result = pcall(function() return obj:SetTexture(path) end)
        return ok and result ~= false
    end
    local coreOK = setTexture(coreObj, FIRE_CORE)
    local ringOK = setTexture(ringObj, SHOCKWAVE)
    local decor = {}
    local function allocate(path, rgb, kind, x, y, size, angle, pathFraction)
        local h = stage.alloc()
        if not h then return end
        if not setTexture(h.obj, path) then
            stage.free(h)
            FX.lastError = "fireball texture unavailable: " .. path
            return
        end
        local setupOK = pcall(function()
            h.obj:SetVertexColor(rgb[1], rgb[2], rgb[3], 1)
            h.obj:SetAlpha(0); h.obj:Hide()
        end)
        if not setupOK then stage.free(h); FX.lastError = "fireball decoration setup failed"; return end
        table.insert(decor, { tex = h, kind = kind, x = x, y = y, size = size, angle = angle, path = pathFraction })
    end
    if coreOK and ringOK then
        local emberColors = { { 1, 0.34, 0.06 }, { 1, 0.58, 0.12 }, { 1, 0.82, 0.3 } }
        for i = 1, FX.FIREBALL_SPARKS do
            local u = i / (FX.FIREBALL_SPARKS + 1)
            allocate((i == 2) and SOFT_GLOW or SPARK, emberColors[i], "trail", from.x + (target.x - from.x) * u,
                from.y + (target.y - from.y) * u, math.max(5, coreSize * (0.22 + 0.06 * i)), nil, u)
        end
        for i = 1, 3 do
            local a = (i - 1) * math.pi * 2 / 3
            allocate(SPARK, emberColors[i], "impact", target.x, target.y, math.max(7, coreSize * 0.28), a)
        end
    end
    if not coreOK or not ringOK then
        stage.free(core); stage.free(ring)
        FX.lastError = "fireball textures unavailable"
        return false
    end
    local main = { stage = stage, tex = core, dur = flight + FX.FIREBALL_IMPACT, t = 0, sound = sound, manual = beat and beat.fxManual,
        update = function(it, q)
            local ok, err = pcall(function()
            local t = it.t
            local obj = it.tex.obj
            if t < flight then
                local u = t / flight
                stage.place(obj, from.x + (target.x - from.x) * u, from.y + (target.y - from.y) * u, coreSize, coreSize)
                obj:SetAlpha(1)
                if not it.shown then obj:Show(); it.shown = true end
            else
                if not it.arrived then
                    it.arrived = true
                    it.ring = true
                    if it.sound then FX.PlaySound(it.sound) end
                end
                local u = math.min(1, (t - flight) / 0.1)
                stage.place(obj, target.x, target.y, coreSize * (1.4 - 0.4 * u), coreSize * (1.4 - 0.4 * u))
                obj:SetAlpha(math.max(0, 1 - (t - flight) / 0.12))
                if it.ring then
                    local wave = math.min(1, (t - flight) / FX.FIREBALL_IMPACT)
                    stage.place(ringObj, target.x, target.y, waveSize * (0.22 + 0.78 * wave), waveSize * (0.22 + 0.78 * wave))
                    ringObj:SetAlpha(math.max(0, 1 - wave))
                    if not it.ringShown then ringObj:Show(); it.ringShown = true end
                end
            end
            for i = 1, table.getn(it.decor) do
                local d, o = it.decor[i], it.decor[i].tex.obj
                local elapsed, x, y, size, alpha
                if d.kind == "trail" then
                    elapsed = t - flight * d.path
                    x, y, size = d.x, d.y, d.size
                    alpha = elapsed < 0 and 0 or math.max(0, 1 - elapsed / 0.18)
                else
                    elapsed = t - flight
                    local u = math.min(1, math.max(0, elapsed / FX.FIREBALL_IMPACT))
                    local radius = 16 + 42 * u
                    x = target.x + math.cos(d.angle) * radius
                    y = target.y + math.sin(d.angle) * radius
                    size = d.size * (1 - 0.35 * u)
                    alpha = elapsed < 0 and 0 or math.max(0, 1 - u)
                end
                if alpha > 0 then
                    stage.place(o, x, y, size, size); o:SetAlpha(alpha)
                    if not d.shown then o:Show(); d.shown = true end
                elseif d.shown then
                    o:SetAlpha(0); o:Hide()
                end
            end
            end)
            if not ok then
                FX.lastError = "fireball draw failed: " .. tostring(err)
                if stage.name == "bench" and FX.bench then
                    pcall(function() FX.bench.info:SetText("fireball | error " .. FX.lastError) end)
                end
            end
        end,
        decor = decor, finish = function(it) stage.free(it.tex); stage.free(ring); for i = 1, table.getn(it.decor) do stage.free(it.decor[i].tex) end end }
    if not stage.add(main) then return false end
    if beat then beat.fxItem = main end
    return true
end

-- Generic card visuals share one timeline-owned controller and one pooled texture per target. They deliberately use
-- alpha artwork (never WHITE/border masks), so tint remains meaningful for every family color.
local function visual2D(stage, visual, targetVisuals, family, from, goals, flight, sound, color, scale, isArea, beat)
    if not isNum(flight) then flight = FX.FIREBALL_FLIGHT end
    if flight < 0.1 then flight = 0.1 elseif flight > 1 then flight = 1 end
    if not isNum(scale) or scale < 0.5 or scale > 2 then scale = 1 end
    local parts = {}
    local count = math.min(table.getn(goals), FX.MAX_VISUAL_TARGETS)
    local setupFailed, primaryFailed = false, false
    local function lease(path, target, index, role, angle, partVisual, tintOverride)
        local h = stage.alloc()
        if not h then
            if role == "primary" then FX.lastError = "visual target cues limited by texture pool" end
            return false
        end
        local ok, result = pcall(function()
            local loaded = h.obj:SetTexture(path)
            if loaded == false then error("asset rejected") end
            local tint = tintOverride or color
            h.obj:SetVertexColor(tint[1], tint[2], tint[3], 1)
            h.obj:SetAlpha(0); h.obj:Hide()
        end)
        if not ok or result == false then
            pcall(stage.free, h)
            setupFailed = true
            FX.lastError = "visual texture unavailable: " .. path
            return false
        end
        table.insert(parts, { tex = h, path = path, target = target, index = index, role = role, angle = angle or 0,
            visual = partVisual or visual,
            size = math.min(120, math.max(target.w, target.h) * scale), shown = false })
        return true
    end
    -- Reserve every target's primary cue before spending the pool on decorative leases.
    for i = 1, count do
        local v = targetVisuals[i] or visual
        local path = NEUTRAL_SPARK
        if v == "fireball" or v == "bolt" or v == "control" or v == "volley" then
            path = NEUTRAL_CORE
        elseif v == "slash" then path = SLASH_ARC
        elseif v == "nova" or v == "buff" or v == "silence" or v == "destroy" or v == "curse" then path = NEUTRAL_RING
        elseif v == "rain" then path = FROST_SHARD
        elseif v == "heal" then path = NEUTRAL_GLOW
        elseif v == "lightning" then path = LIGHTNING_ARC
        elseif v == "transform" then path = SMOKE_PUFF
        elseif v == "summon" or v == "bounce" or v == "secret" then path = PORTAL_RING
        elseif v == "shield" then path = SHIELD_SHELL
        elseif v == "drain" then path = NEUTRAL_GLOW
        elseif v == "resource" then path = NEUTRAL_GLOW
        elseif v == "freeze" then path = FROST_SHARD
        elseif v == "stealth" then path = SMOKE_PUFF end
        local target = goals[i]
        if v == "resource" and from then target = from end
        if v == "secret" and from then target = from end
        if not lease(path, target, i, "primary", 0, v) then primaryFailed = true end
    end
    if table.getn(parts) == 0 then
        if not setupFailed then FX.lastError = "visual texture pool exhausted" end
        return false, true, setupFailed
    end
    -- Decorations are best effort and bounded by the same 24/16 pooled leases as all other 2D effects.
    for i = 1, count do
        local v, a = targetVisuals[i] or visual, goals[i]
        if v == "bolt" and family == "frost" then
        -- Preserve an arrival ring at every target, then add two small shards to the leading projectile.
            lease(NEUTRAL_RING, a, i, "arrival", 0, v)
            if i == 1 then
                lease(FROST_SHARD, a, i, "frostAccent", 0.08, v)
                lease(FROST_SHARD, a, i, "frostAccent", 0.16, v)
            end
            lease(NEUTRAL_GLOW, a, i, "trail", 0, v)
        end
    end
    for i = 1, count do
        local a, v = goals[i], targetVisuals[i] or visual
        if (v == "secret" or v == "resource") and from then a = from end
        if v == "bolt" or v == "fireball" then
            if not (v == "bolt" and family == "frost") then
                lease(NEUTRAL_GLOW, a, i, "trail", 0, v)
                lease(NEUTRAL_RING, a, i, "arrival", 0, v)
            end
        elseif v == "impact" or v == "slash" then
            lease(NEUTRAL_SPARK, a, i, "burst", math.pi * 0.5, v)
            lease(NEUTRAL_SPARK, a, i, "burst", math.pi * 1.5, v)
        elseif v == "rain" then
            lease(FROST_SHARD, a, i, "shard", math.pi, v)
            lease(NEUTRAL_SPARK, a, i, "arrival", 0, v)
        elseif v == "heal" then
            lease(NEUTRAL_SPARK, a, i, "mote", math.pi * 0.5, v)
            lease(NEUTRAL_SPARK, a, i, "mote", math.pi * 1.5, v)
        elseif v == "buff" then
            lease(NEUTRAL_SPARK, a, i, "mote", math.pi * 0.5, v)
        elseif v == "lightning" then
            lease(NEUTRAL_RING, a, i, "flare", 0, v)
            lease(NEUTRAL_SPARK, a, i, "pulseSpark", math.pi * 0.5, v)
            lease(NEUTRAL_SPARK, a, i, "pulseSpark", math.pi * 1.5, v)
        elseif v == "silence" then
            lease(NEUTRAL_SPARK, a, i, "fadeMote", math.pi * 0.5, v)
            lease(NEUTRAL_SPARK, a, i, "fadeMote", math.pi * 1.5, v)
        elseif v == "transform" then
            lease(SMOKE_PUFF, a, i, "coverPuff", 0, v)
        elseif v == "summon" then
            lease(NEUTRAL_CORE, a, i, "risingCore", 0, v)
            lease(NEUTRAL_SPARK, a, i, "mote", math.pi * 0.5, v)
        elseif v == "destroy" then
            lease(NEUTRAL_SPARK, a, i, "spiral", math.pi * 0.5, v)
            lease(NEUTRAL_SPARK, a, i, "spiral", math.pi * 1.5, v)
        elseif v == "shield" then
            lease(NEUTRAL_SPARK, a, i, "shieldMote", math.pi * 0.5, v)
            lease(NEUTRAL_SPARK, a, i, "shieldMote", math.pi * 1.5, v)
        elseif v == "bounce" then
            lease(SMOKE_PUFF, a, i, "bounceSmoke", 0, v)
        elseif v == "control" then
            lease(NEUTRAL_GLOW, a, i, "controlTether", 0, v, { 0.3, 0.75, 1 })
            lease(NEUTRAL_GLOW, a, i, "controlTether", 1, v, { 0.78, 0.38, 1 })
        elseif v == "secret" then
            lease(NEUTRAL_SPARK, a, i, "secretCross", math.pi * 0.25, v)
            lease(NEUTRAL_SPARK, a, i, "secretCross", math.pi * 0.75, v)
        elseif v == "drain" then
            lease(NEUTRAL_SPARK, a, i, "drainMote", math.pi * 0.5, v, { 0.7, 0.25, 1 })
            lease(NEUTRAL_SPARK, a, i, "drainMote", math.pi * 1.5, v, { 0.7, 0.25, 1 })
        elseif v == "resource" then
            lease(NEUTRAL_SPARK, from or a, i, "resourceMote", math.pi * 0.5, v, { 0.25, 0.68, 1 })
            lease(NEUTRAL_SPARK, from or a, i, "resourceMote", math.pi * 1.5, v, { 1, 0.78, 0.25 })
        elseif v == "volley" then
            lease(NEUTRAL_GLOW, a, i, "volleyTrail", 0.16, v)
            lease(NEUTRAL_RING, a, i, "arrival", 0, v)
            lease(NEUTRAL_SPARK, a, i, "burst", math.pi * 0.5, v)
        elseif v == "freeze" then
            lease(NEUTRAL_RING, a, i, "freezeRing", 0, v)
            lease(FROST_SHARD, a, i, "freezeShard", math.pi * 0.5, v)
            lease(FROST_SHARD, a, i, "freezeShard", math.pi * 1.5, v)
        elseif v == "stealth" then
            lease(NEUTRAL_SPARK, a, i, "stealthWisp", math.pi * 0.25, v)
            lease(NEUTRAL_SPARK, a, i, "stealthWisp", math.pi * 0.75, v)
        elseif v == "curse" then
            -- A pending mark blooms gently, then fades; it never contracts like the destroy cue.
            lease(SMOKE_PUFF, a, i, "curseSmoke", math.pi * 0.5, v)
            lease(SMOKE_PUFF, a, i, "curseSmoke", math.pi * 1.5, v)
            lease(NEUTRAL_SPARK, a, i, "curseMote", math.pi * 0.25, v)
        end
    end
    local novaGoals = {}
    for i = 1, count do if (targetVisuals[i] or visual) == "nova" then table.insert(novaGoals, goals[i]) end end
    if table.getn(novaGoals) > 0 and isArea then
        local minx, maxx, miny, maxy = novaGoals[1].x, novaGoals[1].x, novaGoals[1].y, novaGoals[1].y
        for i = 1, table.getn(novaGoals) do
            local a = novaGoals[i]
            if a.x < minx then minx = a.x end; if a.x > maxx then maxx = a.x end
            if a.y < miny then miny = a.y end; if a.y > maxy then maxy = a.y end
        end
        local center = { x = (minx + maxx) / 2, y = (miny + maxy) / 2,
            w = math.min(420, math.max(80, maxx-minx)), h = math.min(420, math.max(80, maxy-miny)) }
        lease(NEUTRAL_RING, center, 0, "groupRing", 0, "nova")
    end
    local it = { stage = stage, dur = flight + 0.45, t = 0, manual = beat and beat.fxManual, parts = parts,
        update = function(item, q)
            local t = item.t
            if t >= flight and not item.arrived then
                item.arrived = true
                if sound then FX.PlaySound(sound) end
            end
            for i = 1, table.getn(item.parts) do
                local c, a, obj = item.parts[i], item.parts[i].target, item.parts[i].tex.obj
                local vk = c.visual or visual
                local u = t / flight
                if u > 1 then u = 1 end
                local x, y, w, h, alpha
                local m = c.size
                local p = math.max(0, math.min(1, (t-flight) / 0.42))
                local delay = c.role == "trail" and 0.12 or (c.role == "shard" and 0.16 or 0)
                local v
                if c.role == "trail" then v = math.max(0, math.min(1, u - delay))
                else v = math.max(0, math.min(1, (u-delay)/(1-delay))) end
                if c.role == "primary" and vk == "volley" then
                    local sx, sy = from and from.x or a.x, from and from.y or a.y
                    local dx, dy = a.x-sx, a.y-sy
                    x, y = sx+dx*u, sy+dy*u-m*0.28*math.sin(math.pi*u)
                    w, h = math.max(18,m*0.4), math.max(18,m*0.4)
                    alpha = t <= flight and 1 or math.max(0,1-(t-flight)/0.2)
                elseif c.role == "primary" and vk == "freeze" then
                    local k = t < flight and (0.24+0.15*u) or (0.38+1.05*p)
                    x, y = a.x, a.y; w, h = m*0.34*k, m*0.68*k
                    alpha = t < flight and (0.25+0.35*u) or math.max(0,1-p)
                elseif c.role == "primary" and vk == "stealth" then
                    local k = 1.1-0.75*p
                    x, y = a.x, a.y; w, h = m*k, m*k
                    alpha = t < flight and 0 or (p < 0.25 and 0.78*(p/0.25) or 0.78*math.max(0,(1-p)/0.75))
                elseif c.role == "primary" and vk == "curse" then
                    local bloom = math.min(1, p * 3)
                    local k = 0.72 + 0.46 * bloom
                    x, y = a.x, a.y
                    w, h = m * k, m * k
                    alpha = t < flight and 0 or 0.78 * math.max(0, 1 - p)
                elseif c.role == "primary" and (vk == "bolt" or vk == "fireball" or vk == "control") then
                    x = from and from.x + (a.x - from.x) * u or a.x; y = from and from.y + (a.y - from.y) * u or a.y
                    local core = (vk == "bolt" and family == "frost") and 0.48 or (vk == "control" and 0.5 or 0.42)
                    w, h = math.max(18, m * core), math.max(18, m * core)
                    alpha = t <= flight and 1 or math.max(0, 1 - (t-flight) / 0.2)
                elseif c.role == "primary" and vk == "lightning" then
                    x, y = a.x, a.y-a.h*0.52*(1-u)
                    w, h = math.min(90,math.max(34,m*0.62)), math.min(190,math.max(64,m*1.4))
                    alpha = t < flight and (0.18+0.62*u) or
                        (p < 0.35 and (0.45+0.5*math.abs(math.sin(t*58))) or math.max(0,1-p))
                elseif c.role == "primary" and vk == "silence" then
                    local k = 1.75-1.35*p
                    x, y = a.x, a.y; w, h = m*k, m*k; alpha = t < flight and 0 or 1-p
                elseif c.role == "primary" and vk == "transform" then
                    local k = p < 0.5 and (0.35+1.3*p) or (1.0-0.7*(p-0.5))
                    local swirl = (1-p)*m*0.12
                    x, y = a.x+math.cos(t*15)*swirl, a.y+math.sin(t*15)*swirl
                    w, h = m*k, m*k; alpha = t < flight and 0 or math.max(0,1-p)
                elseif c.role == "primary" and vk == "summon" then
                    local k = 0.4+1.25*p
                    x, y = a.x, a.y; w, h = m*k, m*k; alpha = t < flight and 0 or math.max(0,1-p)
                elseif c.role == "primary" and vk == "destroy" then
                    local k = 1.7-1.45*p
                    x, y = a.x, a.y; w, h = m*k, m*k; alpha = t < flight and 0 or math.max(0,1-p)
                elseif c.role == "primary" and vk == "shield" then
                    local pulse = 1+0.08*math.sin((t-flight)*math.pi*6)
                    x, y = a.x, a.y; w, h = m*pulse, m*pulse
                    alpha = t < flight and 0 or (p < 0.7 and 0.9 or 0.9*math.max(0,(1-p)/0.3))
                elseif c.role == "primary" and vk == "bounce" then
                    local pulse = 1+0.12*math.sin((t-flight)*math.pi*5)
                    x, y = a.x, a.y; w, h = m*pulse, m*pulse; alpha = t < flight and 0 or 1-p
                elseif c.role == "primary" and vk == "secret" then
                    x, y = a.x, a.y; w, h = m*0.82, m*0.82; alpha = t < flight and 0 or 0.38*(1-p)
                elseif c.role == "primary" and vk == "drain" then
                    local pulse = 0.8+0.2*math.sin((t-flight)*math.pi*8)
                    x, y = a.x, a.y; w, h = m*pulse, m*pulse; alpha = t < flight and 0 or 0.85*(1-p)
                elseif c.role == "primary" and vk == "resource" then
                    local pulse = 0.88+0.16*math.sin((t-flight)*math.pi*5)
                    x, y = a.x, a.y; w, h = m*pulse, m*pulse; alpha = t < flight and 0 or 0.8*(1-p)
                elseif c.role == "trail" then
                    x = from and from.x + (a.x-from.x)*v or a.x; y = from and from.y + (a.y-from.y)*v or a.y
                    w, h = math.max(14, m*0.27), math.max(14, m*0.27); alpha = t <= flight and (1-v)*0.65 or 0
                elseif c.role == "volleyTrail" then
                    local sx, sy = from and from.x or a.x, from and from.y or a.y
                    local dx, dy = a.x-sx, a.y-sy
                    local lag = math.max(0,math.min(1,u-c.angle))
                    x, y = sx+dx*lag, sy+dy*lag-m*0.28*math.sin(math.pi*lag)
                    w, h = math.max(12,m*0.2), math.max(12,m*0.2)
                    alpha = t <= flight and (0.5*(1-lag)) or 0
                elseif c.role == "frostAccent" then
                    local sx, sy = from and from.x or a.x, from and from.y or a.y
                    local dx, dy = a.x-sx, a.y-sy
                    local length = math.sqrt(dx*dx+dy*dy)
                    local side = c.angle < 0.12 and -1 or 1
                    local lagged = math.max(0, math.min(1, u-c.angle))
                    local nx, ny = 0, 1
                    if length > 0 then nx, ny = -dy/length, dx/length end
                    x, y = sx+dx*lagged+nx*side*m*0.16, sy+dy*lagged+ny*side*m*0.16
                    w, h = math.max(12,m*0.24), math.max(12,m*0.24)
                    alpha = t <= flight and 0.72 or 0
                elseif c.role == "bounceSmoke" then
                    local progress = math.max(0,math.min(1,(t-flight)/0.42))
                    local handY = a.side == "m" and (a.y+a.h*1.15) or (a.y-a.h*1.15)
                    x = a.x+math.sin(math.pi*progress)*m*0.16
                    y = a.y+(handY-a.y)*progress
                    w, h = m*(0.8-0.48*progress), m*(0.8-0.48*progress)
                    alpha = t < flight and 0 or (1-progress)
                elseif c.role == "controlTether" then
                    local sx, sy = from and from.x or a.x, from and from.y or a.y
                    local dx, dy = a.x-sx, a.y-sy
                    local length = math.sqrt(dx*dx+dy*dy)
                    local sign = c.angle == 0 and -1 or 1
                    x = sx+dx*(0.25+0.65*u)
                    y = sy+dy*(0.25+0.65*u)+sign*math.sin(u*math.pi)*m*0.3
                    w, h = math.max(18,math.min(420,length*(0.58-0.38*u))), math.max(10,m*0.13)
                    alpha = t <= flight and 0.78 or math.max(0,1-p)
                elseif c.role == "secretCross" then
                    local angle = c.angle+(t-flight)*math.pi*2
                    local radius = m*(0.34+0.16*math.sin((t-flight)*math.pi*3))
                    x, y = a.x+math.cos(angle)*radius*(1-p), a.y+math.sin(angle)*radius*(1-p)
                    w, h = m*0.16, m*0.16; alpha = t < flight and 0 or 0.52*(1-p)
                elseif c.role == "drainMote" then
                    local progress = math.max(0,math.min(1,(t-flight)/0.42))
                    local sx, sy = from and from.x or a.x, from and from.y or a.y
                    x = a.x+(sx-a.x)*progress
                    y = a.y+(sy-a.y)*progress+math.sin(progress*math.pi*2+c.angle)*m*0.1
                    w, h = m*0.2, m*0.2; alpha = t < flight and 0 or (1-progress)
                elseif c.role == "resourceMote" then
                    local angle = c.angle+(t-flight)*math.pi*3
                    local radius = m*(0.48+0.08*math.sin((t-flight)*math.pi*5))
                    x, y = a.x+math.cos(angle)*radius, a.y+math.sin(angle)*radius
                    w, h = m*0.18, m*0.18
                    alpha = t < flight and 0 or (p < 0.7 and 0.9 or 0.9*math.max(0,(1-p)/0.3))
                elseif c.role == "flare" then
                    x, y = a.x, a.y; w, h = m*(0.45+0.9*p), m*(0.45+0.9*p)
                    alpha = t < flight and 0 or 1-p
                elseif c.role == "pulseSpark" then
                    local pulse = math.abs(math.sin((t-flight)*math.pi*12))
                    x, y = a.x, a.y; w, h = m*(0.18+0.2*pulse), m*(0.18+0.2*pulse)
                    alpha = t < flight and 0 or (p < 0.42 and (1-p)*pulse or 0)
                elseif c.role == "coverPuff" then
                    local cover = math.max(0,math.min(1,(t-flight-0.12)/0.3))
                    local swirl = m*0.12*(1-cover)
                    x, y = a.x+math.cos(t*13+c.angle)*swirl, a.y+math.sin(t*13+c.angle)*swirl
                    w, h = m*(0.55+0.75*cover), m*(0.55+0.75*cover)
                    alpha = t < flight+0.12 and 0 or math.max(0,1-cover)
                elseif c.role == "risingCore" then
                    local rise = math.max(0,math.min(1,(t-flight)/0.42))
                    x, y = a.x, a.y-a.h*0.38*rise
                    w, h = m*(0.35+0.45*rise), m*(0.35+0.45*rise)
                    alpha = t < flight and 0 or math.max(0,1-rise)
                elseif c.role == "fadeMote" then
                    local radius = m*(0.7*(1-p)+0.05)
                    x, y = a.x+math.cos(c.angle)*radius, a.y+math.sin(c.angle)*radius
                    w, h = m*0.2, m*0.2; alpha = t < flight and 0 or 1-p
                elseif c.role == "spiral" then
                    local angle = c.angle+(t-flight)*12
                    local radius = m*(0.62*(1-p)+0.02)
                    x, y = a.x+math.cos(angle)*radius, a.y+math.sin(angle)*radius
                    w, h = m*0.23, m*0.23; alpha = t < flight and 0 or 1-p
                elseif c.role == "shieldMote" then
                    local angle = c.angle+p*math.pi*2
                    local radius = m*0.58
                    x, y = a.x+math.cos(angle)*radius, a.y+math.sin(angle)*radius
                    w, h = m*0.2, m*0.2
                    alpha = t < flight and 0 or (p < 0.7 and 0.9 or 0.9*math.max(0,(1-p)/0.3))
                elseif c.role == "arrival" then
                    x, y = a.x, a.y; w, h = m*(0.35+1.05*p), m*(0.35+1.05*p); alpha = t < flight and 0 or 1-p
                elseif c.role == "freezeRing" then
                    local k = 0.28+1.2*p
                    x, y = a.x, a.y; w, h = m*k, m*k; alpha = t < flight and 0 or 1-p
                elseif c.role == "freezeShard" then
                    local radius = m*(0.08+0.48*p)
                    x = a.x+math.cos(c.angle)*radius
                    y = a.y+math.sin(c.angle)*radius-a.h*0.16*p
                    w, h = math.max(12,m*0.2), math.max(18,m*0.4)
                    alpha = t < flight and 0 or 1-p
                elseif c.role == "stealthWisp" then
                    local radius = m*(0.16+0.42*p)
                    x = a.x+math.cos(c.angle)*radius
                    y = a.y+math.sin(c.angle)*radius-a.h*0.12*p
                    w, h = m*0.16, m*0.16
                    alpha = t < flight and 0 or 0.5*(1-p)
                elseif c.role == "curseSmoke" then
                    local drift = m * (0.12 + 0.18 * p)
                    x = a.x + math.cos(c.angle) * drift
                    y = a.y + math.sin(c.angle) * drift - a.h * 0.1 * p
                    w, h = m * (0.26 + 0.12 * p), m * (0.26 + 0.12 * p)
                    alpha = t < flight and 0 or 0.42 * math.max(0, 1 - p)
                elseif c.role == "curseMote" then
                    local radius = m * (0.28 + 0.24 * p)
                    x = a.x + math.cos(c.angle + p * math.pi) * radius
                    y = a.y + math.sin(c.angle + p * math.pi) * radius
                    w, h = m * 0.14, m * 0.14
                    alpha = t < flight and 0 or 0.58 * math.max(0, 1 - p)
                elseif c.role == "groupRing" then
                    x, y = a.x, a.y
                    w, h = math.min(420, a.w*(0.45+1.0*p)), math.min(420, a.h*(0.45+1.0*p))
                    alpha = t < flight and 0 or 1-p
                elseif c.role == "burst" then
                    local radius = m*(0.08+0.5*p)
                    x, y = a.x+math.cos(c.angle)*radius, a.y+math.sin(c.angle)*radius
                    w, h = m*0.34, m*0.34; alpha = t < flight and 0 or 1-p
                elseif c.role == "shard" or (c.role == "primary" and vk == "rain") then
                    x = a.x + (c.role == "shard" and math.cos(c.angle)*m*0.22 or 0)
                    y = a.y - a.h*1.1*(1-v); w, h = math.max(18,m*0.36), math.max(24,m*0.52)
                    alpha = t <= flight and 1 or math.max(0,1-(t-flight)/0.12)
                elseif c.role == "mote" then
                    local phase = (t-flight)/0.42
                    local radius = m*(0.24+0.18*math.max(0,phase))
                    x, y = a.x+math.cos(c.angle)*radius, a.y+math.sin(c.angle)*radius-a.h*0.2*math.max(0,phase)
                    w, h = m*0.22, m*0.22; alpha = t < flight and 0 or math.max(0,1-p)
                elseif c.role == "primary" and vk == "slash" then
                    x, y = a.x, a.y; w, h = m*1.4, m*0.55; alpha = t < flight and 0 or 1-p
                elseif c.role == "primary" and vk == "heal" then
                    x, y = a.x, a.y-a.h*0.25*p; w, h = m*(0.7+0.35*p), m*(0.7+0.35*p)
                    alpha = t < flight and 0 or 1-p
                elseif c.role == "primary" and vk == "buff" then
                    local pulse = 0.88+0.18*math.sin((t-flight)*math.pi*5)
                    x, y = a.x, a.y; w, h = m*pulse, m*pulse; alpha = t < flight and 0 or 1-p
                elseif c.role == "primary" and vk == "nova" then
                    x, y = a.x, a.y; w, h = m*0.7, m*0.7; alpha = t < flight and 0 or 1-p
                else -- impact burst
                    x, y = a.x, a.y; w, h = m*(0.4+0.6*p), m*(0.4+0.6*p); alpha = t < flight and 0 or 1-p
                end
                if not c.released then
                    local ok, err = pcall(function()
                        if alpha > 0 then
                            item.stage.place(obj, x, y, w, h); obj:SetAlpha(clamp01(alpha))
                            if not c.shown then obj:Show(); c.shown = true end
                        elseif c.shown then obj:SetAlpha(0); obj:Hide() end
                    end)
                    if not ok then
                        FX.lastError = "visual draw failed: " .. tostring(err)
                        pcall(stage.free, c.tex); c.released = true
                    end
                end
            end
        end,
        finish = function(item)
            for i = 1, table.getn(item.parts) do
                if not item.parts[i].released then pcall(stage.free, item.parts[i].tex) end
            end
        end }
    if not stage.add(it) then return false end
    if beat then beat.fxItem = it end
    return true, primaryFailed, setupFailed
end

-- ---------------------------------------------------------------- the effect
local function bounds(goals)
    local minx, maxx, miny, maxy = nil, nil, nil, nil
    for i = 1, table.getn(goals) do
        local a = goals[i]
        local l, r, t, b = a.x - a.w / 2, a.x + a.w / 2, a.y - a.h / 2, a.y + a.h / 2
        if not minx or l < minx then minx = l end
        if not maxx or r > maxx then maxx = r end
        if not miny or t < miny then miny = t end
        if not maxy or b > maxy then maxy = b end
    end
    local w, h = maxx - minx, maxy - miny
    if w > 420 then w = 420 end
    if h > 420 then h = 420 end
    return { id = 0, kind = "board", side = "-", x = (minx + maxx) / 2, y = (miny + maxy) / 2, w = w, h = h }
end

local function modelAllowed(p)
    if not p or FX.modelsDisabled then
        return false
    end
    local mode = FX.ModelMode()
    if mode == "off" then
        return false
    end
    if mode == "verified" and p.verified ~= true then
        return false
    end
    return true
end

local function flightTime(r, ctx)
    local f = FX.FLIGHT
    if r.missile and type(r.missile.dur) == "number" and r.missile.dur < f then
        f = r.missile.dur
    end
    local limit = (ctx.dur or 0.35) * 0.8
    if f > limit then
        f = limit
    end
    if f < 0.1 then
        f = 0.1
    end
    return f
end

local function distance(a, b)
    local dx, dy = a.x - b.x, a.y - b.y
    return math.sqrt(dx * dx + dy * dy)
end

-- Plays the effect of a beat on a stage. ctx is the context of MatchUI.lua (family, color, src, targets, area,
-- board, dur); beat supplies the card id. Returns false when there is nothing to show. FX.last tells what started.
function FX.Play(stage, ctx, beat)
    local family = ctx.family or "generic"
    local r = FX.Resolve(beat and beat.cardId or nil, family)
    if ctx.sound then r.sound = ctx.sound end
    local draw2d = stage.name ~= "bench" or FX.benchLayer ~= "model"
    local drawModels = stage.name ~= "bench" or FX.benchLayer ~= "2d"
    local color = ctx.tint or r.tint or r.color or ctx.color or FX.COLOR_DEFAULT
    if type(color) ~= "table" then color = FX.COLOR_DEFAULT end
    color = { clamp01(tonumber(color[1]) or 1), clamp01(tonumber(color[2]) or 1), clamp01(tonumber(color[3]) or 1) }
    local dur2d = FX.FamilyDuration(family)
    local goals, seen = {}, {}
    for i = 1, table.getn(ctx.targets or {}) do
        local a = ctx.targets[i]
        local key = a.missing and ("side" .. tostring(a.side)) or a.id
        if not seen[key] then
            seen[key] = true
            table.insert(goals, a)
        end
    end
    if table.getn(goals) == 0 and ctx.src then
        goals[1] = ctx.src
    end
    if table.getn(goals) == 0 then
        FX.last = nil
        return false
    end
    local visual = ctx.visual or r.visual
    local targetVisuals, hasTargetVisualOverrides = {}, false
    local resultVisuals = ctx.targetVisuals or r.resultVisuals or {}
    for i = 1, table.getn(goals) do
        local a = goals[i]
        local v = (type(a.visual) == "string" and a.visual) or resultVisuals[a.id]
        if FX.VISUALS[v] then
            targetVisuals[i] = v
            if v ~= visual then hasTargetVisualOverrides = true end
        else
            targetVisuals[i] = visual
        end
    end
    if not FX.VISUALS[visual] and hasTargetVisualOverrides then
        visual = targetVisuals[1]
    end
    local isVolley = visual == "volley"
    for i = 1, table.getn(targetVisuals) do
        if targetVisuals[i] == "volley" then isVolley = true end
    end
    if isVolley then
        color = ctx.tint or r.tint or ctx.color or r.color or FX.COLOR_DEFAULT
        if type(color) ~= "table" then color = FX.COLOR_DEFAULT end
        color = { clamp01(tonumber(color[1]) or 1), clamp01(tonumber(color[2]) or 1), clamp01(tonumber(color[3]) or 1) }
    end
    local last = { family = family, cardId = beat and beat.cardId or nil, goals = table.getn(goals), launches = {}, models = 0,
        sound = false, style = r.fallback, missileKey = r.missileKey, impactKey = r.impactKey, areaKey = r.areaKey,
        targetVisuals = targetVisuals }
    last.visual = FX.VISUALS[visual] and visual or nil
    if visual == "fireball" and not hasTargetVisualOverrides and not ctx.tint and not r.tint and not ctx.fxScale and not r.fxScale
        and not ctx.area and ctx.src and table.getn(goals) > 0 then
        local target = goals[1]
        local duration = ctx.flight or r.flight or ctx.dur or FX.FIREBALL_FLIGHT
        if not isNum(duration) then duration = FX.FIREBALL_FLIGHT end
        if duration < 0.1 then duration = 0.1 elseif duration > 1 then duration = 1 end
        local bench = stage.name == "bench"
        local layer = bench and FX.benchLayer or "2d"
        local nativeMissile = ctx.modelPreset or r.missile
        local nativeAllowed = modelAllowed(nativeMissile) or (ctx.audition and nativeMissile and not FX.modelsDisabled)
        if bench and layer ~= "2d" and drawModels and nativeAllowed then
            local flight = duration
            if flight > FX.MAX_LIFE then flight = FX.MAX_LIFE end
            if startModels(stage, { missile = nativeMissile, mDiam = FX.Diameter("missile", target), x0 = ctx.src.x, y0 = ctx.src.y,
                x1 = target.x, y1 = target.y, flight = flight }) then
                last.models = last.models + 1
            end
        end
        -- SetModel failure clears the model stage; start the independent 2D layer afterwards.
        if layer ~= "model" and draw2d then
            last.fireball = fireball2D(stage, ctx.src, target, duration, r.sound, side(target), beat)
            if not last.fireball then last.error = FX.lastError or "fireball pool exhausted" end
        end
        last.flight = duration
        last.sound = "at-arrival"
        FX.last = last
        return true
    end
    if FX.VISUALS[visual] or hasTargetVisualOverrides then
        local duration = ctx.flight or r.flight or ctx.dur or FX.FIREBALL_FLIGHT
        if not isNum(duration) then duration = FX.FIREBALL_FLIGHT end
        if duration < 0.1 then duration = 0.1 elseif duration > 1 then duration = 1 end
        local customScale = ctx.fxScale or r.fxScale or 1
        last.flight = duration
        last.sound = "at-arrival"
        local visualArea = ctx.area or table.getn(goals) >= 3
        -- Bench previews can audition the profile's native preset. Start it first: a failed SetModel disables
        -- that optional layer, then the independent 2D controller below is still installed.
        if stage.name == "bench" and drawModels then
            local missilePreset = (visual == "bolt" or visual == "fireball") and r.missile or nil
            local impactPreset = visualArea and (r.area or r.impact) or (r.impact or r.area)
            local a = goals[1]
            if visualArea then
                local minx, maxx, miny, maxy = a.x, a.x, a.y, a.y
                for i = 2, math.min(table.getn(goals), FX.MAX_VISUAL_TARGETS) do
                    local g = goals[i]
                    if g.x < minx then minx = g.x end; if g.x > maxx then maxx = g.x end
                    if g.y < miny then miny = g.y end; if g.y > maxy then maxy = g.y end
                end
                a = { x = (minx+maxx)/2, y = (miny+maxy)/2,
                    w = math.min(420, math.max(80,maxx-minx)), h = math.min(420, math.max(80,maxy-miny)) }
            end
            local spec = { x1 = a.x, y1 = a.y, flight = duration }
            if modelAllowed(missilePreset) and ctx.src then
                spec.missile = missilePreset; spec.mDiam = FX.Diameter("missile", a)
                spec.x0, spec.y0 = ctx.src.x, ctx.src.y
            end
            if modelAllowed(impactPreset) then
                spec.impact = impactPreset
                spec.iRole = visualArea and "area" or ((impactPreset.kind == "aura") and "aura" or "impact")
                spec.iDiam = FX.Diameter(spec.iRole, a)
            end
            if spec.missile or spec.impact then
                if startModels(stage, spec) then last.models = last.models + 1 end
            end
        end
        if draw2d then
            local partial, setupFailed
            last.visualStarted, partial, setupFailed = visual2D(stage, visual, targetVisuals, family, ctx.src, goals, duration, r.sound, color, customScale,
                visualArea, beat)
            if not last.visualStarted then last.error = FX.lastError or "visual texture pool exhausted"
            elseif setupFailed then last.error = FX.lastError
            elseif partial then last.error = "visual target cues limited by texture pool" end
        elseif stage.name == "bench" then
            -- Model-only bench previews still keep the single sound on the same arrival time.
            local soundItem = { dur = duration + 0.05, t = 0, update = function(item)
                if item.t >= duration and not item.arrived then
                    item.arrived = true
                    if r.sound then FX.PlaySound(r.sound) end
                end
            end }
            stage.add(soundItem)
        end
        FX.last = last
        return true
    end
    local area = ctx.area or table.getn(goals) >= 3
    last.area = area and true or false
    if area then
        -- one burst over the affected targets, then a mark on every one of them (all at the same moment)
        local c = bounds(goals)
        if draw2d then
            local look = STYLE[r.fallback] or STYLE.pulse
            look(stage, c, color, dur2d, 0)
            for i = 1, table.getn(goals) do
                if i <= FX.MAX_PULSES then
                    STYLE.pulse(stage, goals[i], color, dur2d * 0.8, 0)
                end
            end
        end
        local ap = r.area or r.impact
        if drawModels and modelAllowed(ap) and startModels(stage, { impact = ap, iRole = "area", iDiam = FX.Diameter("area", c), x1 = c.x, y1 = c.y }) then
            last.models = last.models + 1
        end
        last.center = c
    else
        local launched, impacts = 0, 0
        local look = STYLE[r.fallback] or STYLE.pulse
        for i = 1, table.getn(goals) do
            if i <= FX.MAX_PULSES then
                local a = goals[i]
                local src = ctx.src
                local travels = src and not a.missing and not (src.id == a.id and src.kind == a.kind) and distance(src, a) > 8
                local flight = 0
                local spec = nil
                if travels and launched < FX.MAX_MISSILES and (r.missile or r.trail) then
                    launched = launched + 1
                    flight = flightTime(r, ctx)
                    if draw2d then trail(stage, src, a, color, flight) end
                    spec = { x0 = src.x, y0 = src.y, x1 = a.x, y1 = a.y, flight = flight }
                    if drawModels and modelAllowed(r.missile) then
                        spec.missile = r.missile
                        spec.mDiam = FX.Diameter("missile", a)
                    end
                    table.insert(last.launches, { from = src.id, to = a.id, flight = flight })
                end
                if drawModels and modelAllowed(r.impact) and (spec or impacts < FX.MAX_IMPACTS) then
                    spec = spec or { x1 = a.x, y1 = a.y, flight = 0 }
                    spec.impact = r.impact
                    spec.iRole = (r.impact.kind == "aura") and "aura" or "impact"
                    spec.iDiam = FX.Diameter(spec.iRole, a)
                    impacts = impacts + 1
                end
                if spec and (spec.missile or spec.impact) and startModels(stage, spec) then
                    last.models = last.models + 1
                end
                if draw2d then look(stage, a, color, dur2d, flight) end
                if family == "destroy" and stage.shake and a.slot then
                    stage.shake(a.slot)
                end
            end
        end
    end
    last.sound = FX.PlaySound(r.sound)
    FX.last = last
    return true
end

-- ---------------------------------------------------------------- the match stage
FX.matchStage = {
    name = "match",
    parent = function()
        return MU.fxFrame
    end,
    board = function()
        return MU.board
    end,
    alloc = function()
        return MU.AllocTexture()
    end,
    free = function(h)
        MU.FreeTexture(h)
    end,
    add = function(item)
        return MU.AddFx(item)
    end,
    remove = function(item)
        local list = MU.fxItems
        for i = table.getn(list), 1, -1 do
            if list[i] == item then
                table.remove(list, i)
                return true
            end
        end
        return false
    end,
    place = function(obj, x, y, w, h)
        MU.PlaceRect(obj, x, y, w, h)
    end,
    shake = function(slot)
        MU.Shake(slot)
    end,
}

-- Hook of MatchUI.lua: an effect beat starts. An error is passed on (the 2D pulse of MatchUI.lua runs instead); after
-- three errors this layer stays out of the way for the session.
function FX.PlayEffect(beat, ctx)
    if FX.broken or not MU.pool or not MU.fxFrame or not MU.board then
        return MU.Fx.DefaultEffect(beat, ctx)
    end
    local ok, err = pcall(FX.Play, FX.matchStage, ctx, beat)
    if not ok then
        FX.stats.errors = FX.stats.errors + 1
        FX.lastError = tostring(err)
        if FX.stats.errors >= 3 then
            FX.broken = true
        end
        error(err, 0)
    end
end

-- Hook of MatchUI.lua: the effects must end now (skip, hide, new match): every model is cleared and returned.
function FX.Stop()
    stopStage(FX.matchStage)
end

-- Hook of MatchUI.lua: the window was built (MU.fxFrame exists): the model pool is created once.
function FX.Init(frame)
    ensurePool(FX.matchStage)
end

-- The match timeline owns the flight clock for the reference fireball. This puts the target flash
-- on the same tick that advances to the damage step; the remaining ring fade then uses FxTick.
function FX.TickEffect(beat, t)
    local it = beat and beat.fxItem
    if not it or not it.manual or type(t) ~= "number" then return end
    if t < 0 then t = 0 end
    if t > it.dur then t = it.dur end
    it.t = t
    if it.update then it.update(it, t / it.dur) end
end

function FX.EndEffect(beat)
    local it = beat and beat.fxItem
    if it and it.manual then
        it.manual = false
        it.skipNextTick = true
    end
end

-- Everything of every stage ends (the bench too).
function FX.StopAll()
    stopStage(FX.matchStage)
    if FX.bench then
        FX.BenchClear()
    end
end

-- Pool numbers for tests and /hs fx status: models made / in use by the match stage, running runs.
function FX.PoolStats()
    local out = { models = 0, used = 0, runs = table.getn(FX.runs), disabled = FX.modelsDisabled }
    local stages = { FX.matchStage }
    if FX.bench then
        table.insert(stages, FX.bench.stage)
    end
    for k = 1, table.getn(stages) do
        local pool = stages[k].mpool
        if pool then
            for i = 1, table.getn(pool) do
                out.models = out.models + 1
                if pool[i].used then
                    out.used = out.used + 1
                end
            end
        end
    end
    return out
end

-- ---------------------------------------------------------------- bench (/hs fx)
local BENCH_W, BENCH_H = 560, 390
local WORDS_MAX = 8

-- Splits a string into words (no gfind: 1.12 has it, the tests' Lua may not).
local function words(s)
    local out = {}
    s = s or ""
    while table.getn(out) < WORDS_MAX do
        local _, _, w, rest = string.find(s, "^%s*(%S+)(.*)$")
        if not w then
            break
        end
        table.insert(out, w)
        s = rest
    end
    return out
end

local function benchAlloc()
    local b = FX.bench
    for i = 1, table.getn(b.tex) do
        if not b.tex[i].used then
            b.tex[i].used = true
            return b.tex[i]
        end
    end
    return nil
end

local function benchFree(h)
    if not h or not h.used then
        return
    end
    h.used = false
    local t = h.obj
    t:Hide()
    t:ClearAllPoints()
    t:SetTexture(WHITE)
    t:SetVertexColor(1, 1, 1, 1)
    t:SetBlendMode("ADD")
    t:SetAlpha(1)
end

local function benchAdd(item)
    local b = FX.bench
    if table.getn(b.items) >= FX.MAX_BENCH_ITEMS then
        if item.finish then
            item.finish(item)
        end
        return nil
    end
    item.t = 0
    table.insert(b.items, item)
    if item.update then
        item.update(item, 0)
    end
    return item
end

local function benchRemove(item)
    local list = FX.bench.items
    for i = table.getn(list), 1, -1 do
        if list[i] == item then
            table.remove(list, i)
            return true
        end
    end
    return false
end

-- Ends every running item of the bench (textures and models go back to their pools).
function FX.BenchClear()
    local b = FX.bench
    if not b then
        return
    end
    local list = b.items
    b.items = {}
    for i = table.getn(list), 1, -1 do
        if list[i].finish then
            list[i].finish(list[i])
        end
    end
    stopStage(b.stage)
    b.loopAt = nil
end

local function benchAnchor(name, x, y)
    return { id = name, kind = "minion", side = "o", x = x, y = y, w = 76, h = 92 }
end

function FX.EnsureBench()
    if FX.bench then
        return FX.bench
    end
    if not CreateFrame or not UIParent then
        return nil
    end
    local f = CreateFrame("Frame", "HearthstoneFxBench", UIParent)
    f:SetWidth(BENCH_W)
    f:SetHeight(BENCH_H)
    f:SetPoint("CENTER", UIParent, "CENTER", 0, 140)
    f:SetFrameStrata("DIALOG")
    f:SetMovable(true)
    f:EnableMouse(true)
    f:RegisterForDrag("LeftButton")
    f:SetBackdrop({ bgFile = WHITE,
        edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border", tile = true, tileSize = 32, edgeSize = 16,
        insets = { left = 4, right = 4, top = 4, bottom = 4 } })
    f:SetBackdropColor(0.035, 0.035, 0.045, 0.96)
    local title = f:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    title:SetPoint("TOP", f, "TOP", 0, -10)
    title:SetText("Effect bench  (/hs fx help)")
    local layer = CreateFrame("Frame", nil, f)
    layer:SetAllPoints(f)
    layer:SetFrameLevel(f:GetFrameLevel() + 3)
    local info = f:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    info:SetPoint("TOPLEFT", f, "TOPLEFT", 14, -292)
    info:SetWidth(BENCH_W - 28)
    info:SetHeight(40)
    info:SetJustifyH("LEFT")
    info:SetJustifyV("TOP")
    local b = { frame = f, layer = layer, items = {}, tex = {}, cur = nil, loop = false, title = title, info = info,
        quiet = true, showBoxes = false }
    b.boxes = {}
    -- a and b differ in height as well as in width: the calibration solves the horizontal and the vertical scale from them
    b.anchors = { a = benchAnchor("a", 130, 105), b = benchAnchor("b", 430, 235), mid = benchAnchor("mid", 280, 170) }
    b.anchors.mid.w, b.anchors.mid.h = 120, 92
    -- the test anchors are drawn as dim boxes so a model has something to land on
    local names = { "a", "b" }
    for i = 1, 2 do
        local a = b.anchors[names[i]]
        local box = f:CreateTexture(nil, "ARTWORK")
        box:SetTexture(WHITE)
        box:SetVertexColor(1, 1, 1, 0.15)
        box:SetWidth(a.w)
        box:SetHeight(a.h)
        box:SetPoint("CENTER", f, "TOPLEFT", a.x, -a.y)
        b.boxes[i] = box
        box:Hide()
        local label = f:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
        label:SetPoint("TOP", box, "BOTTOM", 0, -2)
        label:SetText(names[i])
    end
    for i = 1, FX.BENCH_TEXTURES do
        local t = layer:CreateTexture(nil, "OVERLAY")
        t:SetTexture(WHITE)
        t:SetBlendMode("ADD")
        t:Hide()
        b.tex[i] = { obj = t, used = false }
    end
    b.stage = {
        name = "bench",
        parent = function()
            return b.layer
        end,
        board = function()
            return b.frame
        end,
        alloc = benchAlloc,
        free = benchFree,
        add = benchAdd,
        remove = benchRemove,
        place = function(obj, x, y, w, h)
            obj:ClearAllPoints()
            obj:SetWidth(w)
            obj:SetHeight(h)
            obj:SetPoint("CENTER", b.frame, "TOPLEFT", x, -y)
        end,
    }
    FX.bench = b
    f:SetScript("OnDragStart", function()
        this:StartMoving()
    end)
    f:SetScript("OnDragStop", function()
        this:StopMovingOrSizing()
    end)
    f:SetScript("OnUpdate", function()
        FX.BenchTick(arg1)
    end)
    f:SetScript("OnHide", function()
        FX.CalEnd()
        FX.BenchClear()
    end)
    if UISpecialFrames then
        table.insert(UISpecialFrames, "HearthstoneFxBench")
    end
    f:Hide()
    FX.CalRefresh()
    return b
end

-- Drives the bench's items; a looping preset plays again when everything ended.
function FX.BenchTick(dt)
    local b = FX.bench
    if not b or type(dt) ~= "number" then
        return
    end
    if dt > 0.25 then
        dt = 0.25
    end
    local list = b.items
    for i = table.getn(list), 1, -1 do
        local it = list[i]
        if it then
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
    local cb = b.calib
    if cb and cb.loop and table.getn(b.items) == 0 then
        cb.gap = (cb.gap or 0) + dt
        if cb.gap >= 0.25 then
            cb.gap = 0
            FX.CalPlay()
        end
    end
    if b.loop and b.cur and table.getn(b.items) == 0 then
        b.loopAt = (b.loopAt or 0) + dt
        if b.loopAt >= 0.3 then
            b.loopAt = 0
            FX.BenchPlay()
        end
    end
end

local function benchDiagnostics(b, label, path, size, anchor, dur)
    local pos = anchor and (string.format("%.1f", anchor.x) .. ", " .. string.format("%.1f", anchor.y)) or "n/a"
    local text = label .. " | path " .. tostring(path or "2D asset") .. " | size " .. tostring(size or "auto")
        .. " | position " .. pos .. " | duration " .. string.format("%.2f", dur or 0) .. "s"
    local placement = FX.lastPlacement
    if placement and FX.benchLayer ~= "2d" then
        text = text .. "\nModel scale " .. string.format("%.3f", placement.scale) .. " / xyz "
            .. string.format("%.3f, %.3f, %.3f", placement.x, placement.y, placement.z)
    end
    text = text .. " | error " .. tostring(FX.lastError or FX.modelError or "none")
    b.info:SetHeight(72)
    pcall(function() b.info:SetText(text) end)
end

local function copyPreset(p)
    local c = {}
    for k, v in pairs(p) do
        if type(v) == "table" then
            local t = {}
            for k2, v2 in pairs(v) do
                t[k2] = v2
            end
            c[k] = t
        else
            c[k] = v
        end
    end
    return c
end

-- Plays the current bench preset once (see /hs fx play): kind missile flies from `from` to `to`, impact and aura
-- play on `to`. The 2D layer and the sound play with it.
function FX.BenchPlay()
    local b = FX.EnsureBench()
    local cur = b and b.cur
    if not cur then
        return false
    end
    FX.CalEnd()
    b.frame:Show()
    local stage = b.stage
    local list = b.items
    b.items = {}
    for i = table.getn(list), 1, -1 do
        if list[i].finish then
            list[i].finish(list[i])
        end
    end
    stopStage(stage)
    local p = cur.p
    FX.lastError = nil
    FX.lastPlacement = nil
    local from, to = b.anchors[cur.from] or b.anchors.a, b.anchors[cur.to] or b.anchors.b
    local color = p.tint or FX.COLOR_DEFAULT
    local fallback = p.fallback
    if not (fallback and STYLE[fallback]) then
        fallback = "ring"
    end
    local kind = cur.kind or p.kind or "impact"
    if cur.visual == "fireball" and kind == "missile" then
        local flight = math.max(0.1, math.min(FX.MAX_LIFE - FX.FIREBALL_IMPACT, p.dur or FX.FIREBALL_FLIGHT))
        local ctx = { family = "fire", visual = "fireball", flight = flight,
            dur = flight, src = from, targets = { to }, color = color, sound = p.sound, modelPreset = p, audition = true }
        local resolved = FX.Resolve("CS2_029", "fire")
        if p.sound == nil then ctx.sound = resolved.sound end
        FX.Play(stage, ctx, { cardId = "CS2_029", sound = cur.p.sound or resolved.sound })
        local native = ctx.modelPreset
        benchDiagnostics(b, "CS2_029 fireball / " .. FX.benchLayer, native and native.model or "none (2D)",
            FX.Diameter("missile", to), to, ctx.flight)
        b.started = nil
        return true
    end
    local started = nil
    local draw2d, drawModels = FX.benchLayer ~= "model", FX.benchLayer ~= "2d"
    if kind == "missile" then
        local flight = p.dur or FX.FLIGHT
        if flight > FX.MAX_LIFE then
            flight = FX.MAX_LIFE
        end
        if flight < 0.1 then
            flight = 0.1
        end
        if draw2d then trail(stage, from, to, color, flight) end
        if drawModels then
            started = startModels(stage, { missile = p, mDiam = FX.Diameter("missile", to), x0 = from.x, y0 = from.y, x1 = to.x,
                y1 = to.y, flight = flight })
        end
        if draw2d then STYLE[fallback](stage, to, color, 0.5, flight) end
    else
        local role = (kind == "aura") and "aura" or "impact"
        if drawModels then
            started = startModels(stage, { impact = p, iRole = role, iDiam = FX.Diameter(role, to), x1 = to.x, y1 = to.y, flight = 0 })
        end
        if draw2d then STYLE[fallback](stage, to, color, 0.5, 0) end
    end
    FX.PlaySound(p.sound)
    benchDiagnostics(b, cur.key .. " / " .. kind, p.model, p.size or FX.Diameter(kind == "missile" and "missile" or "impact", to), to, p.dur)
    b.started = started
    return true
end

-- ---------------------------------------------------------------- calibration (/hs fx calibrate)
-- The owner cannot tune numbers by typing, so the bench has a guided calibration: a green cross marks the centre of box a
-- (then of box b), a ring shows the size the marker should have, and a small fireball model (FX.MARKER) is replayed there. The
-- owner nudges the marker with buttons until it sits on the cross (point 1 = box a, point 2 = box b, Next / Back switch)
-- and resizes it to the ring. The two found positions give the pixels per unit (ky, kz) and the origin offset (ox, oz) of
-- the model frame (FX.Solve), the ring gives the size factor (ppy). Save stores it in HearthstoneDB.fxCal.
local STEP_COARSE, STEP_FINE = 16, 2        -- design pixels of the bench moved by one press
local SIZE_STEP = 1.15                      -- Bigger / Smaller change the marker scale by this factor

local function fmt(n, d)
    if not isNum(n) then
        return "?"
    end
    return string.format("%." .. d .. "f", n)
end

local function benchScale(b)
    local e = 1
    if type(b.frame.GetEffectiveScale) == "function" then
        local ok, v = pcall(b.frame.GetEffectiveScale, b.frame)
        if ok and isNum(v) and v > 0 then
            e = v
        end
    end
    return e
end

-- Where box a (i = 1) or box b (i = 2) is in screen pixels from the bottom-left corner of the calibration frame, which is
-- centred on box a (so point 1 is the production placement: the frame centred on its target).
local function calTarget(b, i, cal)
    local e = benchScale(b)
    local a = b.anchors.a
    local t = a
    if i == 2 then
        t = b.anchors.b
    end
    return cal.frame / 2 + (t.x - a.x) * e, cal.frame / 2 - (t.y - a.y) * e
end

local function calPoints(b)
    local c = b.calib
    local px1, pz1 = calTarget(b, 1, c.start)
    local px2, pz2 = calTarget(b, 2, c.start)
    return { px = px1, pz = pz1, y = c.u[1][1], z = c.u[1][2] }, { px = px2, pz = pz2, y = c.u[2][1], z = c.u[2][2] }
end

-- The calibration the current marker positions and size give (not yet saved).
local function calWork(b)
    local c = b.calib
    local p1, p2 = calPoints(b)
    local w, sy, sz = FX.Solve(c.start, p1, c.two and p2 or nil)
    local ppy = FX.MARKER.diam * benchScale(b) / (2 * FX.MARKER.ext * c.ms)
    if ppy < 10 then
        ppy = 10
    elseif ppy > 5000 then
        ppy = 5000
    end
    w.ppy = ppy
    return w, sy, sz
end

local function calClampScale(s)
    if s < FX.SCALE_MIN then
        return FX.SCALE_MIN
    elseif s > FX.SCALE_MAX then
        return FX.SCALE_MAX
    end
    return s
end

local function ensureCalUI(b)
    if b.calUI then
        return b.calUI
    end
    local f = b.frame
    local ui = { btn = {}, all = {} }
    -- the cross and the ring live on a frame above the model frames, so the marker never hides them
    local cf = CreateFrame("Frame", nil, f)
    cf:SetAllPoints(f)
    cf:SetFrameLevel(f:GetFrameLevel() + 12)
    ui.cross = cf
    local function cross(w, h)
        local t = cf:CreateTexture(nil, "OVERLAY")
        t:SetTexture(WHITE)
        t:SetVertexColor(0.3, 1, 0.3, 0.95)
        t:SetWidth(w)
        t:SetHeight(h)
        t:Hide()
        table.insert(ui.all, t)
        return t
    end
    ui.hline, ui.vline = cross(70, 1), cross(1, 70)
    ui.ring = cf:CreateTexture(nil, "OVERLAY")
    ui.ring:SetTexture(RING)
    ui.ring:SetVertexColor(0.3, 1, 0.3, 0.9)
    ui.ring:SetWidth(FX.MARKER.diam)
    ui.ring:SetHeight(FX.MARKER.diam)
    ui.ring:Hide()
    table.insert(ui.all, ui.ring)
    local function button(id, label, w, x, y, fn)
        local btn = CreateFrame("Button", "HearthstoneFxCal" .. id, f, "UIPanelButtonTemplate")
        btn:SetWidth(w)
        btn:SetHeight(20)
        btn:SetPoint("TOPLEFT", f, "TOPLEFT", x, -y)
        btn:SetText(label)
        btn:SetScript("OnClick", function()
            local ok, err = pcall(fn)
            if not ok then
                say("fx error: " .. tostring(err))
            end
        end)
        btn:Hide()
        ui.btn[id] = btn
        table.insert(ui.all, btn)
        return btn
    end
    local row1, row2 = 334, 360
    local dirs = { { "L", "left", "<" }, { "R", "right", ">" }, { "U", "up", "^" }, { "D", "down", "v" } }
    for i = 1, 4 do
        local d = dirs[i]
        local x = 62 + (i - 1) * 44
        button(d[1] .. "2", d[3] .. d[3], 40, x, row1, function() FX.CalNudge(d[2], false) end)
        button(d[1] .. "1", d[3], 40, x, row2, function() FX.CalNudge(d[2], true) end)
    end
    for i = 1, 2 do
        local fs = f:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
        fs:SetPoint("TOPLEFT", f, "TOPLEFT", 14, -((i == 1 and row1 or row2) - 4))
        fs:SetText(i == 1 and "coarse" or "fine")
        fs:Hide()
        table.insert(ui.all, fs)
    end
    button("bigger", "Bigger", 64, 250, row1, function() FX.CalSize(true) end)
    button("smaller", "Smaller", 64, 250, row2, function() FX.CalSize(false) end)
    button("next", "Next (b)", 70, 326, row1, function() FX.CalNext() end)
    button("reset", "Reset", 70, 326, row2, function() FX.CalReset() end)
    button("save", "Save", 70, 402, row1, function() FX.CalSave() end)
    button("done", "Done", 70, 402, row2, function() FX.CalEnd() end)
    b.calUI = ui
    return ui
end

-- Redraws the numbers, the cross and the button labels of the bench.
function FX.CalRefresh()
    local b = FX.bench
    if not b then
        return
    end
    local cal = FX.Cal()
    local c = b.calib
    local lines = {}
    if c then
        local u = c.u[c.point]
        local w, sy, sz = calWork(b)
        table.insert(lines, "point " .. c.point .. "/2 (box " .. (c.point == 1 and "a" or "b") .. "): marker y " .. fmt(u[1], 3) .. " z "
            .. fmt(u[2], 3) .. ", size " .. fmt(c.ms, 3))
        table.insert(lines, "found: " .. fmt(w.ky, 1) .. " / " .. fmt(w.kz, 1) .. " px per unit (y / z)" .. ((c.two and sy and sz) and "" or " (guess)")
            .. ", origin " .. fmt(w.ox, 1) .. " / " .. fmt(w.oz, 1) .. ", " .. fmt(w.ppy, 1) .. " px per yard")
        table.insert(lines, "saved: " .. (FX.CalSaved() and "yes" or "no, defaults") .. "   (Save stores it; Reset = shipped defaults)")
    else
        table.insert(lines, "placement: " .. (FX.CalSaved() and "calibrated" or "shipped defaults, not calibrated yet (/hs fx calibrate)"))
        table.insert(lines, fmt(cal.ky, 1) .. " / " .. fmt(cal.kz, 1) .. " px per unit (y / z), origin " .. fmt(cal.ox, 1) .. " / " .. fmt(cal.oz, 1)
            .. ", " .. fmt(cal.ppy, 1) .. " px per yard, frame " .. fmt(cal.frame, 0) .. " px, depth " .. fmt(cal.x, 2))
    end
    b.info:SetText(table.concat(lines, "\n"))
    local ui = b.calUI
    if ui then
        for i = 1, table.getn(ui.all) do
            if c then
                ui.all[i]:Show()
            else
                ui.all[i]:Hide()
            end
        end
        if c then
            local t = b.anchors.a
            if c.point == 2 then
                t = b.anchors.b
            end
            for _, o in ipairs({ ui.hline, ui.vline, ui.ring }) do
                o:ClearAllPoints()
                o:SetPoint("CENTER", b.frame, "TOPLEFT", t.x, -t.y)
            end
            ui.btn.next:SetText(c.point == 1 and "Next (b)" or "Back (a)")
        end
    end
end

-- Plays the marker once (a calibration run is replayed by the bench until it ends).
function FX.CalPlay()
    local b = FX.bench
    local c = b and b.calib
    if not c then
        return false
    end
    FX.BenchClear()
    local u = c.u[c.point]
    local m = FX.MARKER
    local p = { model = m.model, kind = "impact", scale = 1, pos = { 0, 0, 0 }, facing = 0, dur = m.dur, ext = m.ext }
    local a = b.anchors.a
    local run = startModels(b.stage, { impact = p, iRole = "impact", iDiam = m.diam, marker = true,
        iOver = { scale = calClampScale(c.ms), x = c.start.x, y = u[1], z = u[2] }, x1 = a.x, y1 = a.y, flight = 0 })
    return run ~= nil
end

-- /hs fx calibrate: shows the cross at box a and the marker; true when the calibration is running.
function FX.CalStart()
    local b = FX.EnsureBench()
    if not b then
        return false
    end
    FX.BenchClear()
    b.loop = false
    b.frame:Show()
    local cal = copyCal(FX.Cal())
    local e = benchScale(b)
    local c = { point = 1, start = cal, two = false, loop = true, gap = 0, u = {} }
    c.ms = calClampScale(FX.MARKER.diam * e / (2 * FX.MARKER.ext * cal.ppy))
    local px1, pz1 = calTarget(b, 1, cal)
    local px2, pz2 = calTarget(b, 2, cal)
    local y1, z1 = FX.UnitsFor(cal, px1, pz1)
    local y2, z2 = FX.UnitsFor(cal, px2, pz2)
    c.u[1], c.u[2] = { y1, z1 }, { y2, z2 }
    b.calib = c
    ensureCalUI(b)
    FX.CalRefresh()
    FX.CalPlay()
    return true
end

-- Moves the marker of the current point one step: dir left|right|up|down, fine (2) or coarse (16 design pixels). The step
-- follows the sign of the scale the calibration started from: if the marker moves the wrong way, press the opposite one.
function FX.CalNudge(dir, fine)
    local b = FX.bench
    local c = b and b.calib
    if not c then
        return false
    end
    local step = (fine and STEP_FINE or STEP_COARSE) * benchScale(b)
    local u = c.u[c.point]
    if dir == "left" then
        u[1] = u[1] - step / c.start.ky
    elseif dir == "right" then
        u[1] = u[1] + step / c.start.ky
    elseif dir == "up" then
        u[2] = u[2] + step / c.start.kz
    elseif dir == "down" then
        u[2] = u[2] - step / c.start.kz
    else
        return false
    end
    FX.CalRefresh()
    FX.CalPlay()
    return true
end

-- Bigger / Smaller: the size of the marker (match it to the ring).
function FX.CalSize(bigger)
    local b = FX.bench
    local c = b and b.calib
    if not c then
        return false
    end
    if bigger then
        c.ms = calClampScale(c.ms * SIZE_STEP)
    else
        c.ms = calClampScale(c.ms / SIZE_STEP)
    end
    FX.CalRefresh()
    FX.CalPlay()
    return true
end

-- Next: the marker moves to box b (its first position is the best guess from point 1); Back: to box a again.
function FX.CalNext()
    local b = FX.bench
    local c = b and b.calib
    if not c then
        return false
    end
    if c.point == 1 then
        if not c.two then
            local p1 = calPoints(b)
            local w = FX.Solve(c.start, p1, nil)
            local px2, pz2 = calTarget(b, 2, c.start)
            local y2, z2 = FX.UnitsFor(w, px2, pz2)
            c.u[2] = { y2, z2 }
            c.two = true
        end
        c.point = 2
    else
        c.point = 1
    end
    FX.CalRefresh()
    FX.CalPlay()
    return true
end

local function calFinishLine(cal)
    return "depth " .. fmt(cal.x, 2) .. ", frame " .. fmt(cal.frame, 0) .. " px, origin " .. fmt(cal.ox, 1) .. " / " .. fmt(cal.oz, 1)
        .. " px, " .. fmt(cal.ky, 1) .. " / " .. fmt(cal.kz, 1) .. " px per unit, " .. fmt(cal.ppy, 1) .. " px per yard"
end

-- Save: solves the two points and stores HearthstoneDB.fxCal. The marker then restarts with the saved values: it should sit
-- on the cross at box a and at box b.
function FX.CalSave()
    local b = FX.bench
    local c = b and b.calib
    if not c then
        return false
    end
    if not c.two then
        say("move the marker onto the cross at box b too (Next), then Save")
        return false
    end
    if not H.db then
        say("cannot save: the saved variables are not loaded yet")
        return false
    end
    local w, sy, sz = calWork(b)
    if not validCal(w) then
        say("that calibration is not usable (" .. calFinishLine(w) .. "); not saved. Reset and try again")
        return false
    end
    H.db.fxCal = copyCal(w)
    say("effect calibration saved: " .. calFinishLine(w) .. (sy and sz and "" or "   (one axis was not solved: move the marker at b further from a)"))
    FX.CalStart()
    return true
end

-- Reset: forgets the saved calibration (the shipped defaults apply) and restarts.
function FX.CalReset()
    if H.db then
        H.db.fxCal = nil
    end
    say("effect calibration reset to the shipped defaults: " .. calFinishLine(FX.CAL_DEFAULT))
    local b = FX.bench
    if b and b.calib then
        FX.CalStart()
    else
        FX.CalRefresh()
    end
    return true
end

-- Done (also: the bench closes): the cross, the buttons and the marker go away.
function FX.CalEnd()
    local b = FX.bench
    if not b then
        return false
    end
    if b.calib then
        b.calib = nil
        FX.BenchClear()
        FX.CalRefresh()
        return true
    end
    return false
end

local CAL_HELP = {
    "Calibration of the effect models, 5 steps (box a is the left grey box, box b the right one):",
    "1. A green cross marks the centre of box a and a green ring the size a marker should have. A small fireball is replayed there.",
    "2. Press the arrow buttons (>> ^^ big steps, > ^ small steps; if one moves the wrong way press the opposite) until the fireball sits on the cross.",
    "3. Press Bigger / Smaller until the fireball is about as big as the ring.",
    "4. Next (b): do the same on the cross at box b. 5. Save. The fireball is then replayed with the saved values and should sit on both crosses. Reset = shipped defaults, Done closes.",
}

local function calCommand(w)
    local sub = string.lower(w[2] or "")
    local b = FX.bench
    local running = b and b.calib
    if sub == "" or sub == "start" then
        if not FX.CalStart() then
            say("the bench needs a client frame")
            return
        end
        for i = 1, table.getn(CAL_HELP) do
            say(CAL_HELP[i])
        end
    elseif sub == "reset" then
        FX.CalReset()
    elseif sub == "status" then
        say("effect calibration: " .. (FX.CalSaved() and "saved" or "not saved (shipped defaults)") .. ": " .. calFinishLine(FX.Cal()))
    elseif not running then
        say("no calibration is running: /hs fx calibrate")
    elseif sub == "left" or sub == "right" or sub == "up" or sub == "down" then
        FX.CalNudge(sub, w[3] == "fine")
    elseif sub == "bigger" or sub == "smaller" then
        FX.CalSize(sub == "bigger")
    elseif sub == "next" then
        if b.calib.point == 1 then
            FX.CalNext()
        end
    elseif sub == "back" then
        if b.calib.point == 2 then
            FX.CalNext()
        end
    elseif sub == "save" then
        FX.CalSave()
    elseif sub == "done" or sub == "stop" then
        FX.CalEnd()
    else
        say("usage: /hs fx calibrate [left|right|up|down [fine]|bigger|smaller|next|back|save|reset|done|status]")
    end
end

-- A fake effect context for the bench: src = a, targets = { b } (or the middle for an area effect).
local function benchCtx(b, family, area)
    local ctx = { dur = AN and AN.EFFECT or 0.35, family = family, area = area, targets = {}, board = b.anchors.mid,
        src = b.anchors.a }
    ctx.color = MU.FamilyColor and MU.FamilyColor(family) or FX.COLOR_DEFAULT
    if area then
        ctx.targets = { b.anchors.b, benchAnchor("b2", 380, 100), benchAnchor("b3", 480, 240) }
    else
        ctx.targets = { b.anchors.b }
    end
    return ctx
end

local function sortedKeys(t)
    local list = {}
    for k in pairs(t) do
        table.insert(list, k)
    end
    table.sort(list)
    return list
end

local function sayList(prefix, items)
    local line = prefix
    for i = 1, table.getn(items) do
        local part = items[i]
        if string.len(line) + string.len(part) > 210 then
            say(line)
            line = "  "
        end
        line = line .. part .. "  "
    end
    if line ~= prefix and line ~= "  " then
        say(line)
    end
end

local HELP = {
    "/hs fx calibrate   FIRST: place and size the models right (a guided calibration on the bench; the owner workflow is in the README)",
    "/hs fx list [text]   the effect presets (* = verified in game)",
    "/hs fx play <preset> [missile|impact] [from] [to]   play a preset on the bench (from / to: a, b or mid; default a b)",
    "/hs fx play fireball   play the CS2_029 2D reference effect; /hs fx layer model|2d|both selects the bench layer",
    "/hs fx boxes on|off | markers on|off | backdrop quiet|normal   diagnostic bench view",
    "/hs fx model <path>   try any model path, e.g. Spells\\Fireball_Missile_High.mdx",
    "/hs fx scale <n> (relative size, 1 = the default for the anchor) | pos <x> <y> <z> (a small offset in model units from the centre) | facing <radians> | dur <seconds> | size <px|auto> (on-screen size in pixels) | cam <n> | light on|off | kind <missile|impact|aura>   tune the current preset (replays it)",
    "/hs fx dump   print the tuned values in manifest form (data/fx/effect_profiles.json)",
    "/hs fx family <family> [area]   the whole effect of a family   /hs fx card <cardId> [area]   the effect of a card",
    "/hs fx loop on|off   replay the preset again and again   /hs fx stop   end everything   /hs fx hide   close the bench",
    "/hs fx sound on|off   effect sounds   /hs fx snd <name or path>   play one sound",
    "/hs fx models all|verified|off   which presets show their model   /hs fx status   /hs fx reset   bring the model layer back",
    "/hs fx calibrate left|right|up|down [fine] | bigger | smaller | next | back | save | reset | done | status   the calibration without the buttons",
}

local function tuneNumber(cur, name, v)
    local n = tonumber(v)
    if not n then
        return false
    end
    cur.p[name] = n
    return true
end

local function runCommand(msg)
    local w = words(msg)
    local cmd = string.lower(w[1] or "help")
    local b = FX.EnsureBench()
    local P = profiles()
    if cmd == "help" or cmd == "?" then
        for i = 1, table.getn(HELP) do
            say(HELP[i])
        end
    elseif cmd == "list" then
        local filter = string.lower(w[2] or "")
        local out = {}
        local keys = sortedKeys(P.presets or {})
        for i = 1, table.getn(keys) do
            local p = P.presets[keys[i]]
            if filter == "" or string.find(string.lower(keys[i]), filter, 1, true) or string.find(string.lower(p.model or ""), filter, 1, true) then
                table.insert(out, keys[i] .. (p.verified and "*" or "") .. "(" .. tostring(p.kind) .. ")")
            end
        end
        say(table.getn(out) .. " presets:")
        sayList("  ", out)
    elseif cmd == "play" then
        local key = w[2]
        local p = key and P.presets and P.presets[key]
        if not p then
            say("no preset '" .. tostring(key) .. "' (/hs fx list)")
            return
        end
        if not b then
            say("the bench needs a client frame")
            return
        end
        local cur = { key = key, p = copyPreset(p), from = "a", to = "b" }
        if key == "fireball" then
            cur.visual = "fireball"
            cur.p.dur = (P.cards and P.cards.CS2_029 and P.cards.CS2_029.flight) or FX.FIREBALL_FLIGHT
        end
        local pos = 3
        if w[pos] == "missile" or w[pos] == "impact" or w[pos] == "aura" then
            cur.kind = w[pos]
            pos = pos + 1
        end
        if w[pos] and b.anchors[w[pos]] then
            cur.from = w[pos]
            pos = pos + 1
        end
        if w[pos] and b.anchors[w[pos]] then
            cur.to = w[pos]
        end
        b.cur = cur
        FX.BenchPlay()
        say("playing " .. key .. " (" .. (cur.kind or p.kind) .. ") " .. cur.from .. " -> " .. cur.to .. (p.verified and "" or "   [not verified yet]"))
    elseif cmd == "model" then
        local path = w[2]
        if not path then
            say("usage: /hs fx model <path>")
            return
        end
        if not b then
            say("the bench needs a client frame")
            return
        end
        b.cur = { key = "(model)", p = { model = path, kind = "impact", scale = 1, pos = { 0, 0, 0 }, facing = 0, dur = 1,
            verified = false }, from = "a", to = "b" }
        FX.BenchPlay()
        say("trying " .. path .. "  (then /hs fx scale, pos, facing, dur, size, kind missile; /hs fx dump)")
    elseif cmd == "scale" or cmd == "facing" or cmd == "dur" or cmd == "size" or cmd == "cam" or cmd == "pos" or cmd == "light"
        or cmd == "kind" then
        local cur = b and b.cur
        if not cur then
            say("no preset yet: /hs fx play <preset> or /hs fx model <path> first")
            return
        end
        local ok = true
        if cmd == "pos" then
            local x, y, z = tonumber(w[2]), tonumber(w[3]), tonumber(w[4])
            if x and y and z then
                cur.p.pos = { x, y, z }
            else
                ok = false
            end
        elseif cmd == "cam" then
            ok = tuneNumber(cur, "camera", w[2])
        elseif cmd == "light" then
            if w[2] == "on" or w[2] == "off" then
                cur.p.light = (w[2] == "on") or nil
            else
                ok = false
            end
        elseif cmd == "kind" then
            if w[2] == "missile" or w[2] == "impact" or w[2] == "aura" then
                cur.p.kind = w[2]
                cur.kind = w[2]
            else
                ok = false
            end
        else
            if cmd == "size" and w[2] == "auto" then
                cur.p.size = nil
                FX.BenchPlay()
                say("size set to auto (derived from the anchor); replaying")
                return
            end
            ok = tuneNumber(cur, cmd, w[2])
            if ok and cmd == "scale" and cur.p.scale <= 0 then
                cur.p.scale = 1
            end
            if ok and cmd == "dur" then
                if cur.p.dur > FX.MAX_LIFE then
                    cur.p.dur = FX.MAX_LIFE
                elseif cur.p.dur < 0.05 then
                    cur.p.dur = 0.05
                end
            end
            if ok and cmd == "size" then
                if cur.p.size < 16 then
                    cur.p.size = 16
                elseif cur.p.size > 512 then
                    cur.p.size = 512
                end
            end
        end
        if not ok then
            say("usage: /hs fx " .. cmd .. " " .. (cmd == "pos" and "<x> <y> <z>" or cmd == "light" and "on|off" or cmd == "kind" and "missile|impact|aura" or cmd == "size" and "<px>|auto" or "<number>"))
            return
        end
        FX.BenchPlay()
        say(cmd .. " set; replaying (/hs fx dump prints the values)")
    elseif cmd == "dump" then
        local cur = b and b.cur
        if not cur then
            say("no preset yet: /hs fx play <preset> or /hs fx model <path> first")
            return
        end
        local p = cur.p
        local pos = p.pos or { 0, 0, 0 }
        local esc = string.gsub(tostring(p.model), "\\", "\\\\")
        say("preset " .. tostring(cur.key) .. " in manifest form:")
        say("  {\"key\": \"" .. tostring(cur.key) .. "\", \"model\": \"" .. esc .. "\", \"kind\": \"" .. tostring(cur.kind or p.kind) .. "\",")
        say("   \"scale\": " .. tostring(p.scale) .. ", \"pos\": [" .. tostring(pos[1]) .. ", " .. tostring(pos[2]) .. ", " .. tostring(pos[3])
            .. "], \"facing\": " .. tostring(p.facing) .. ", \"dur\": " .. tostring(p.dur)
            .. (p.ext and (", \"ext\": " .. tostring(p.ext)) or "") .. (p.size and (", \"size\": " .. tostring(p.size)) or "")
            .. (p.camera and (", \"camera\": " .. tostring(p.camera)) or "") .. ", ... \"verified\": true}")
    elseif cmd == "calibrate" or cmd == "cal" then
        calCommand(w)
    elseif cmd == "family" or cmd == "card" then
        if not b then
            say("the bench needs a client frame")
            return
        end
        local name = w[2]
        local family, cardId = name, nil
        if cmd == "card" then
            cardId = name
            family = (P.cardFamily and name and P.cardFamily[name]) or "generic"
        end
        if not name or not (P.families and P.families[family]) then
            say("usage: /hs fx " .. cmd .. " <" .. cmd .. "> [area]")
            return
        end
        FX.CalEnd()
        b.frame:Show()
        FX.BenchClear()
        local ctx = benchCtx(b, family, w[3] == "area")
        FX.Play(b.stage, ctx, { cardId = cardId })
        local l = FX.last
        say("played " .. family .. (w[3] == "area" and " (area)" or "") .. ": missile " .. tostring(l and l.missileKey) .. ", impact "
            .. tostring(l and l.impactKey) .. ", 2D style " .. tostring(l and l.style))
    elseif cmd == "loop" then
        if b and b.cur then
            b.loop = (w[2] == "on")
            b.loopAt = 0
            say("loop " .. (b.loop and "on (/hs fx stop ends it)" or "off"))
        else
            say("no preset yet: /hs fx play <preset> first")
        end
    elseif cmd == "stop" then
        if b then
            b.loop = false
            if b.calib then
                b.calib.loop = false
            end
        end
        FX.StopAll()
        say("effects stopped")
    elseif cmd == "hide" then
        if b then
            b.loop = false
            b.frame:Hide()
        end
        FX.StopAll()
    elseif cmd == "layer" then
        local mode = w[2]
        if mode ~= "model" and mode ~= "2d" and mode ~= "both" then
            say("usage: /hs fx layer model|2d|both"); return
        end
        FX.benchLayer = mode
        if b and b.cur then FX.BenchPlay() elseif b then FX.BenchClear() end
        say("bench layer: " .. mode)
    elseif cmd == "boxes" or cmd == "markers" then
        if not b then say("the bench needs a client frame"); return end
        if w[2] ~= "on" and w[2] ~= "off" then say("usage: /hs fx " .. cmd .. " on|off"); return end
        local on = w[2] == "on"
        if cmd == "boxes" then b.showBoxes = on end
        for i = 1, table.getn(b.boxes) do
            if cmd == "boxes" then
                if on then b.boxes[i]:Show() else b.boxes[i]:Hide() end
            end
        end
        if cmd == "markers" then
            if not b.markers and CreateFrame then
                b.markers = {}
                for i = 1, 2 do
                    local a = b.anchors[i == 1 and "a" or "b"]
                    local h = b.frame:CreateTexture(nil, "OVERLAY")
                    h:SetTexture(WHITE); h:SetVertexColor(0.35, 1, 0.45, 1); h:SetWidth(12); h:SetHeight(2)
                    h:SetPoint("CENTER", b.frame, "TOPLEFT", a.x, -a.y)
                    local v = b.frame:CreateTexture(nil, "OVERLAY")
                    v:SetTexture(WHITE); v:SetVertexColor(0.35, 1, 0.45, 1); v:SetWidth(2); v:SetHeight(12)
                    v:SetPoint("CENTER", b.frame, "TOPLEFT", a.x, -a.y)
                    b.markers[i] = { h, v }
                end
            end
            for i = 1, table.getn(b.markers or {}) do
                for j = 1, table.getn(b.markers[i]) do
                    if on then b.markers[i][j]:Show() else b.markers[i][j]:Hide() end
                end
            end
        end
        say(cmd .. " " .. (on and "on" or "off"))
    elseif cmd == "backdrop" then
        if not b or (w[2] ~= "quiet" and w[2] ~= "normal") then say("usage: /hs fx backdrop quiet|normal"); return end
        b.quiet = w[2] == "quiet"
        if b.quiet then
            pcall(function() b.frame:SetBackdrop({ bgFile = "Interface\\Buttons\\WHITE8X8", tile = true, tileSize = 16 }); b.frame:SetBackdropColor(0.035, 0.035, 0.045, 0.92) end)
        else
            pcall(function() b.frame:SetBackdrop({ bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background", edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border", tile = true, tileSize = 32, edgeSize = 16, insets = { left = 4, right = 4, top = 4, bottom = 4 } }) end)
            b.frame:SetBackdropColor(1, 1, 1, 1)
        end
        say("bench backdrop: " .. w[2])
    elseif cmd == "sound" then
        if w[2] == "on" or w[2] == "off" then
            FX.SetSound(w[2] == "on")
        end
        say("effect sounds: " .. (FX.SoundOn() and "on" or "off") .. "  (/hs fx sound on|off)")
    elseif cmd == "snd" then
        if not w[2] then
            say("usage: /hs fx snd <sound name or Sound\\path.wav>")
            return
        end
        FX.lastSound = nil
        local wasOff = FX.soundOff
        FX.soundOff = false
        local ok = false
        if not (H.db and H.db.fxSound == false) then
            ok = FX.PlaySound(w[2])
        end
        FX.soundOff = wasOff
        say(ok and ("played " .. w[2]) or ("could not play " .. w[2] .. " (sounds are off in the settings, or the sound call failed)"))
    elseif cmd == "models" then
        if w[2] then
            if not FX.SetModelMode(w[2]) then
                say("usage: /hs fx models all|verified|off")
                return
            end
        end
        say("effect models: " .. FX.ModelMode() .. "  (all: unverified presets show their model too; verified: only verified ones; off: 2D only)")
    elseif cmd == "reset" then
        FX.Enable()
        say("model layer enabled again")
    elseif cmd == "status" then
        local ps = FX.PoolStats()
        say("effects: models " .. FX.ModelMode() .. ", model layer " .. (FX.modelsDisabled and ("OFF (" .. tostring(FX.modelError) .. ")") or "on")
            .. ", sounds " .. (FX.SoundOn() and "on" or "off"))
        say("  model frames " .. ps.models .. " (in use " .. ps.used .. "), runs " .. ps.runs .. ", played: models " .. FX.stats.models
            .. ", sounds " .. FX.stats.sounds .. " (throttled " .. FX.stats.throttled .. "), errors " .. FX.stats.errors
            .. (FX.lastError and (", last error: " .. FX.lastError) or ""))
        say("  placement " .. (FX.CalSaved() and "calibrated" or "NOT calibrated (shipped defaults; /hs fx calibrate)") .. ": " .. calFinishLine(FX.Cal()))
    else
        say("unknown /hs fx command '" .. cmd .. "' (/hs fx help)")
    end
end

-- /hs fx <command>: prints its answer itself (several lines).
function FX.Command(msg)
    local ok, err = pcall(runCommand, msg)
    if not ok then
        say("fx error: " .. tostring(err))
    end
end

-- ---------------------------------------------------------------- install
FX.InstallCardFamilies()
MU.Fx.Init = FX.Init
MU.Fx.PlayEffect = FX.PlayEffect
MU.Fx.Stop = FX.Stop
MU.Fx.TickEffect = FX.TickEffect
MU.Fx.EndEffect = FX.EndEffect
