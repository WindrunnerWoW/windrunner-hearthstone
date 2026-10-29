-- Strict fake of the WoW 1.12 frame API for tests/lua_tests.lua.
--
-- Unlike a permissive recorder, a widget only has the methods that exist in the 1.12 client,
-- so a call to a method the client does not have (SetShown, SetColorTexture, SetEnabled, ...)
-- fails the test like it would raise a Lua error in game. Arguments are validated where the
-- client would reject them (anchor names, layers, fonts, strata, ...). Script handlers are run
-- like 1.12 does it: without arguments, with the globals this / event / arg1 ... set.
--
-- This file is test code and runs on any Lua 5.x; the addon itself is Lua 5.0.

local Fake = {}

local ANCHORS = { TOPLEFT = 1, TOP = 1, TOPRIGHT = 1, LEFT = 1, CENTER = 1, RIGHT = 1,
    BOTTOMLEFT = 1, BOTTOM = 1, BOTTOMRIGHT = 1 }
local LAYERS = { BACKGROUND = 1, BORDER = 1, ARTWORK = 1, OVERLAY = 1, HIGHLIGHT = 1 }
local STRATA = { BACKGROUND = 1, LOW = 1, MEDIUM = 1, HIGH = 1, DIALOG = 1, FULLSCREEN = 1,
    FULLSCREEN_DIALOG = 1, TOOLTIP = 1 }
local FONTS = { GameFontNormal = 1, GameFontNormalSmall = 1, GameFontNormalLarge = 1,
    GameFontHighlight = 1, GameFontHighlightSmall = 1, GameFontDisableSmall = 1, GameFontDisable = 1,
    ChatFontNormal = 1, NumberFontNormal = 1 }
local BLEND = { DISABLE = 1, BLEND = 1, ALPHAKEY = 1, ADD = 1, MOD = 1 }
local JUSTIFY_H = { LEFT = 1, CENTER = 1, RIGHT = 1 }
local JUSTIFY_V = { TOP = 1, MIDDLE = 1, BOTTOM = 1 }
local FRAME_KINDS = { Frame = 1, Button = 1, CheckButton = 1, EditBox = 1, GameTooltip = 1, Model = 1 }

local function isnum(...)
    for i = 1, select("#", ...) do
        if type(select(i, ...)) ~= "number" then
            return false
        end
    end
    return true
end

local function fail(msg)
    error("fake WoW: " .. msg, 3)
end

-- --------------------------------------------------------------- method sets
local M = { Region = {}, Texture = {}, FontString = {}, Frame = {}, Button = {}, CheckButton = {}, EditBox = {}, Model = {} }

-- every widget
function M.Region.SetWidth(o, w) if not isnum(w) then fail("SetWidth needs a number") end o._.w = w end
function M.Region.SetHeight(o, h) if not isnum(h) then fail("SetHeight needs a number") end o._.h = h end
function M.Region.GetWidth(o) return o._.w or 0 end
function M.Region.GetHeight(o) return o._.h or 0 end
function M.Region.SetPoint(o, point, rel, relPoint, x, y)
    if not ANCHORS[point] then fail("bad anchor point " .. tostring(point)) end
    if rel ~= nil and type(rel) ~= "table" and type(rel) ~= "string" then fail("bad relativeTo") end
    if relPoint ~= nil and not ANCHORS[relPoint] then fail("bad relativePoint " .. tostring(relPoint)) end
    if x ~= nil and not isnum(x) then fail("SetPoint x must be a number, got " .. tostring(x)) end
    if y ~= nil and not isnum(y) then fail("SetPoint y must be a number, got " .. tostring(y)) end
    table.insert(o._.points, { point = point, rel = rel, relPoint = relPoint, x = x or 0, y = y or 0 })
end
function M.Region.GetPoint(o, i)
    local pt = o._.points[i or 1]
    if not pt then return nil end
    return pt.point, pt.rel, pt.relPoint, pt.x, pt.y
end
function M.Region.ClearAllPoints(o) o._.points = {} end
function M.Region.SetAllPoints(o, rel) o._.allPoints = rel or true end
function M.Region.Show(o)
    local was = o._.shown
    o._.shown = true
    if not was and o._.scripts and o._.scripts.OnShow then Fake.fire(o, "OnShow") end
end
function M.Region.Hide(o)
    local was = o._.shown
    o._.shown = false
    if was and o._.scripts and o._.scripts.OnHide then Fake.fire(o, "OnHide") end
end
function M.Region.IsShown(o) return o._.shown end
function M.Region.IsVisible(o)
    if not o._.shown then return false end
    if o._.parent and o._.parent._ then return o._.parent:IsVisible() end
    return true
end
function M.Region.SetAlpha(o, a)
    if not isnum(a) or a < 0 or a > 1 then fail("SetAlpha needs a number from 0 to 1, got " .. tostring(a)) end
    o._.alpha = a
end
function M.Region.GetParent(o) return o._.parent end
function M.Region.GetAlpha(o) return o._.alpha or 1 end
function M.Region.GetName(o) return o._.name end

-- textures
for k, v in pairs(M.Region) do M.Texture[k] = v end
function M.Texture.SetTexture(o, a, b, c, d)
    if type(a) == "string" then
        o._.texture = a
        o._.color = nil
        if Fake.missingTextures and Fake.missingTextures[a] then
            return nil
        end
        return 1                    -- 1.12: 1 when the file loaded, nil when not
    elseif isnum(a, b, c) then
        o._.color = { a, b, c, d }
        o._.texture = nil
    elseif a ~= nil then
        fail("SetTexture needs a path or r,g,b")
    else
        o._.texture = nil
    end
end
function M.Texture.GetTexture(o) return o._.texture end
function M.Texture.SetTexCoord(o, ...)
    local n = select("#", ...)
    if n ~= 4 and n ~= 8 then fail("SetTexCoord needs 4 or 8 numbers") end
    if not isnum(...) then fail("SetTexCoord needs numbers") end
    o._.texcoord = { ... }
end
function M.Texture.SetVertexColor(o, r, g, b, a)
    if not isnum(r, g, b) then fail("SetVertexColor needs numbers") end
    if a ~= nil and (not isnum(a) or a < 0 or a > 1) then fail("SetVertexColor alpha must be 0 to 1, got " .. tostring(a)) end
    o._.vertex = { r, g, b, a }
end
function M.Texture.SetBlendMode(o, m)
    if not BLEND[m] then fail("bad blend mode " .. tostring(m)) end
    o._.blend = m
end
function M.Texture.SetDesaturated(o, v) o._.desat = v end

-- font strings
for k, v in pairs(M.Region) do M.FontString[k] = v end
function M.FontString.SetText(o, t)
    if t ~= nil and type(t) ~= "string" and type(t) ~= "number" then fail("SetText needs a string") end
    o._.text = t == nil and "" or tostring(t)
end
function M.FontString.GetText(o) return o._.text end
function M.FontString.SetJustifyH(o, j) if not JUSTIFY_H[j] then fail("bad JustifyH " .. tostring(j)) end o._.justifyH = j end
function M.FontString.SetJustifyV(o, j) if not JUSTIFY_V[j] then fail("bad JustifyV " .. tostring(j)) end o._.justifyV = j end
function M.FontString.SetTextColor(o, r, g, b)
    if not isnum(r, g, b) then fail("SetTextColor needs numbers") end
    o._.textColor = { r, g, b }
end
function M.FontString.SetShadowColor(o, r, g, b, a)
    if not isnum(r, g, b) then fail("SetShadowColor needs numbers") end
    o._.shadowColor = { r, g, b, a }
end
function M.FontString.GetStringWidth(o) return string.len(o._.text or "") * 6 end
function M.FontString.SetFontObject(o, f) o._.font = f end
function M.FontString.SetFont(o, path, height, flags)
    if type(path) ~= "string" or not isnum(height) or height <= 0 then fail("SetFont needs a font path and a height") end
    if flags ~= nil and flags ~= "" and flags ~= "OUTLINE" and flags ~= "THICKOUTLINE" and flags ~= "MONOCHROME" then
        fail("bad SetFont flags " .. tostring(flags))
    end
    o._.fontPath, o._.fontHeight = path, height
end

-- frames
for k, v in pairs(M.Region) do M.Frame[k] = v end
function M.Frame.SetScale(o, s) if not isnum(s) or s <= 0 then fail("bad scale") end o._.scale = s end
function M.Frame.GetScale(o) return o._.scale end
function M.Frame.GetEffectiveScale(o) return o._.scale * ((o._.parent and o._.parent._) and o._.parent:GetEffectiveScale() or 1) end
function M.Frame.GetLeft(o) return o._.left end
function M.Frame.GetRight(o) return o._.right end
function M.Frame.GetTop(o) return o._.top end
function M.Frame.GetBottom(o) return o._.bottom end
function M.Frame.GetCenter(o)
    if o._.cx and o._.cy then return o._.cx, o._.cy end
    return nil
end
function M.Frame.SetFrameStrata(o, s) if not STRATA[s] then fail("bad strata " .. tostring(s)) end o._.strata = s end
function M.Frame.GetFrameStrata(o) return o._.strata end
function M.Frame.SetHitRectInsets(o, l, r, t, b) if not isnum(l, r, t, b) then fail("SetHitRectInsets needs 4 numbers") end o._.hitInsets = { l, r, t, b } end
function M.Frame.SetFrameLevel(o, l) if not isnum(l) then fail("bad frame level") end o._.level = l end
function M.Frame.GetFrameLevel(o) return o._.level end
function M.Frame.SetToplevel(o, v) o._.toplevel = v end
function M.Frame.SetMovable(o, v) o._.movable = v end
function M.Frame.StartMoving(o) if not o._.movable then fail("StartMoving on a frame that is not movable") end o._.moving = true end
function M.Frame.StopMovingOrSizing(o) o._.moving = false end
function M.Frame.SetClampedToScreen(o, v) o._.clamped = v end
function M.Frame.EnableMouse(o, v) o._.mouse = v end
function M.Frame.EnableKeyboard(o, v) o._.keyboard = v end
function M.Frame.EnableMouseWheel(o, v) o._.wheel = v end
function M.Frame.RegisterForDrag(o, ...)
    for i = 1, select("#", ...) do
        local b = select(i, ...)
        if b ~= "LeftButton" and b ~= "RightButton" and b ~= "MiddleButton" then fail("bad drag button " .. tostring(b)) end
    end
    o._.drag = true
end
function M.Frame.SetID(o, id) o._.id = id end
function M.Frame.GetID(o) return o._.id or 0 end
function M.Frame.SetScript(o, name, fn)
    if fn ~= nil and type(fn) ~= "function" then fail("SetScript needs a function") end
    if name == "OnLoad" then fail("OnLoad cannot be set from Lua after creation") end
    o._.scripts[name] = fn
end
function M.Frame.GetScript(o, name) return o._.scripts[name] end
function M.Frame.RegisterEvent(o, e) o._.events[e] = true end
function M.Frame.UnregisterEvent(o, e) o._.events[e] = nil end
function M.Frame.SetBackdrop(o, t)
    if t ~= nil and type(t) ~= "table" then fail("SetBackdrop needs a table") end
    o._.backdrop = t
end
function M.Frame.SetBackdropColor(o, r, g, b, a) if not isnum(r, g, b) then fail("SetBackdropColor needs numbers") end o._.bdColor = { r, g, b, a } end
function M.Frame.SetBackdropBorderColor(o, r, g, b, a) if not isnum(r, g, b) then fail("SetBackdropBorderColor needs numbers") end o._.bdBorder = { r, g, b, a } end
function M.Frame.CreateTexture(o, name, layer, inherits)
    if layer ~= nil and not LAYERS[layer] then fail("bad layer " .. tostring(layer)) end
    local t = Fake.new("Texture", name, o)
    t._.layer = layer or "ARTWORK"
    table.insert(o._.regions, t)
    return t
end
function M.Frame.CreateFontString(o, name, layer, inherits)
    if layer ~= nil and not LAYERS[layer] then fail("bad layer " .. tostring(layer)) end
    if inherits ~= nil and not FONTS[inherits] then fail("unknown font object " .. tostring(inherits)) end
    local t = Fake.new("FontString", name, o)
    t._.font = inherits
    table.insert(o._.regions, t)
    return t
end

-- buttons
for k, v in pairs(M.Frame) do M.Button[k] = v end
function M.Button.SetText(o, t)
    if type(t) ~= "string" and type(t) ~= "number" then fail("Button:SetText needs a string") end
    o._.text = tostring(t)
end
function M.Button.GetText(o) return o._.text end
function M.Button.Enable(o) o._.enabled = true end
function M.Button.Disable(o) o._.enabled = false end
function M.Button.IsEnabled(o) return o._.enabled end
function M.Button.SetButtonState(o, state, locked)
    if state ~= "NORMAL" and state ~= "PUSHED" then fail("bad button state " .. tostring(state)) end
    if locked ~= nil and locked ~= 1 then fail("SetButtonState lock must be 1 or nil") end
    o._.buttonState, o._.stateLocked = state, locked
end
function M.Button.GetButtonState(o) return o._.buttonState or "NORMAL" end
function M.Button.RegisterForClicks(o, ...)
    for i = 1, select("#", ...) do
        local c = select(i, ...)
        if c ~= "LeftButtonUp" and c ~= "LeftButtonDown" and c ~= "RightButtonUp" and c ~= "RightButtonDown"
            and c ~= "MiddleButtonUp" and c ~= "MiddleButtonDown" then fail("bad click type " .. tostring(c)) end
    end
end
function M.Button.LockHighlight(o) o._.locked = true end
function M.Button.UnlockHighlight(o) o._.locked = false end
function M.Button.GetFontString(o) return o._.fontString end
local function texSetter(field)
    return function(o, path, mode)
        if type(path) ~= "string" then fail(field .. " needs a texture path") end
        if mode ~= nil and not BLEND[mode] then fail("bad blend mode " .. tostring(mode)) end
        local t = Fake.new("Texture", nil, o)
        t._.texture = path
        o._[field] = t
    end
end
local function texGetter(field)
    return function(o) return o._[field] end
end
M.Button.SetNormalTexture = texSetter("normal")
M.Button.GetNormalTexture = texGetter("normal")
M.Button.SetPushedTexture = texSetter("pushed")
M.Button.GetPushedTexture = texGetter("pushed")
M.Button.SetDisabledTexture = texSetter("disabled")
M.Button.GetDisabledTexture = texGetter("disabled")
M.Button.SetHighlightTexture = texSetter("highlight")
M.Button.GetHighlightTexture = texGetter("highlight")

for k, v in pairs(M.Button) do M.CheckButton[k] = v end
function M.CheckButton.SetChecked(o, v)
    if v ~= nil and v ~= 1 then fail("SetChecked takes 1 or nil in 1.12, got " .. tostring(v)) end
    o._.checked = v
end
function M.CheckButton.GetChecked(o) return o._.checked end
M.CheckButton.SetCheckedTexture = texSetter("checkedTex")
M.CheckButton.GetCheckedTexture = texGetter("checkedTex")

for k, v in pairs(M.Frame) do M.EditBox[k] = v end
function M.EditBox.SetText(o, t)
    if type(t) ~= "string" then fail("EditBox:SetText needs a string") end
    local changed = (o._.text ~= t)
    o._.text = t
    if changed and o._.scripts.OnTextChanged then Fake.fire(o, "OnTextChanged") end
end
function M.EditBox.SetTextInsets(o, l, r, t, b) if not isnum(l, r, t, b) then fail("SetTextInsets needs 4 numbers") end end
function M.EditBox.SetJustifyH(o, j) if not JUSTIFY_H[j] then fail("bad JustifyH " .. tostring(j)) end o._.justifyH = j end
function M.EditBox.GetText(o) return o._.text or "" end
function M.EditBox.SetAutoFocus(o, v) o._.autoFocus = v end
function M.EditBox.SetMaxLetters(o, n) if not isnum(n) then fail("SetMaxLetters needs a number") end end
function M.EditBox.SetFocus(o)
    o._.focus = true
    if o._.scripts.OnEditFocusGained then Fake.fire(o, "OnEditFocusGained") end
end
function M.EditBox.ClearFocus(o)
    local was = o._.focus
    o._.focus = false
    if was and o._.scripts.OnEditFocusLost then Fake.fire(o, "OnEditFocusLost") end
end
function M.EditBox.HighlightText(o, a, b) o._.highlight = { a, b } end
function M.EditBox.SetShadowColor(o, r, g, b, a) o._.shadowColor = { r, g, b, a } end
function M.EditBox.SetTextColor(o, r, g, b)
    if not isnum(r, g, b) then fail("SetTextColor needs numbers") end
    o._.textColor = { r, g, b }
end


-- model frames (the 1.12 "Model" frame type: a frame that draws one .mdx model). Only the methods the 1.12 client has.
-- Every call is recorded in o._.calls ({ name, a, b, c }); the state is kept in o._.model* fields. A test can make
-- SetModel fail with Fake.failSetModel = true, and CreateFrame("Model") with Fake.failCreateModel = true.
for k, v in pairs(M.Frame) do M.Model[k] = v end
local function modelCall(o, name, a, b, c)
    table.insert(o._.calls, { name = name, a = a, b = b, c = c })
end
function M.Model.SetModel(o, path)
    if type(path) ~= "string" then fail("SetModel needs a model path") end
    modelCall(o, "SetModel", path)
    if Fake.failSetModel then fail("SetModel failed for " .. path) end
    o._.modelPath = path
end
function M.Model.GetModel(o) return o._.modelPath end
function M.Model.ClearModel(o)
    modelCall(o, "ClearModel")
    o._.modelPath = nil
end
function M.Model.SetModelScale(o, s)
    if not isnum(s) or s <= 0 then fail("SetModelScale needs a positive number") end
    modelCall(o, "SetModelScale", s)
    o._.modelScale = s
end
function M.Model.GetModelScale(o) return o._.modelScale or 1 end
function M.Model.SetPosition(o, x, y, z)
    if not isnum(x, y, z) then fail("SetPosition needs x, y, z") end
    modelCall(o, "SetPosition", x, y, z)
    o._.modelPos = { x, y, z }
end
function M.Model.GetPosition(o)
    local p = o._.modelPos or { 0, 0, 0 }
    return p[1], p[2], p[3]
end
function M.Model.SetFacing(o, r)
    if not isnum(r) then fail("SetFacing needs a number (radians)") end
    modelCall(o, "SetFacing", r)
    o._.modelFacing = r
end
function M.Model.GetFacing(o) return o._.modelFacing or 0 end
function M.Model.SetLight(o, enabled, omni, dx, dy, dz, ai, ar, ag, ab, di, dr, dg, db)
    if enabled ~= nil and enabled ~= 0 and enabled ~= 1 and enabled ~= true and enabled ~= false then fail("SetLight needs enabled") end
    modelCall(o, "SetLight", enabled)
end
function M.Model.SetCamera(o, i)
    if not isnum(i) then fail("SetCamera needs a camera index") end
    modelCall(o, "SetCamera", i)
end
function M.Model.SetSequence(o, n)
    if not isnum(n) then fail("SetSequence needs an animation number") end
    modelCall(o, "SetSequence", n)
end
function M.Model.SetSequenceTime(o, n, t)
    if not isnum(n, t) then fail("SetSequenceTime needs a sequence and a time") end
    modelCall(o, "SetSequenceTime", n, t)
end
function M.Model.AdvanceTime(o) modelCall(o, "AdvanceTime") end
function M.Model.ClearFog(o) modelCall(o, "ClearFog") end
function M.Model.SetFogColor(o, r, g, b) modelCall(o, "SetFogColor", r, g, b) end
function M.Model.SetFogNear(o, n) modelCall(o, "SetFogNear", n) end
function M.Model.SetFogFar(o, n) modelCall(o, "SetFogFar", n) end

-- ------------------------------------------------------------------ objects
local KIND_METHODS = {
    Texture = M.Texture, FontString = M.FontString, Frame = M.Frame, Button = M.Button,
    CheckButton = M.CheckButton, EditBox = M.EditBox, GameTooltip = M.Frame, Model = M.Model,
}

function Fake.new(kind, name, parent)
    local o = {}
    local methods = KIND_METHODS[kind]
    rawset(o, "_", {
        kind = kind, name = name, parent = parent, shown = true, points = {}, scripts = {}, events = {},
        regions = {}, children = {}, calls = {}, scale = 1, level = (parent and parent._ and parent._.level or 0) + 1,
        strata = parent and parent._ and parent._.strata or "MEDIUM", enabled = true,
    })
    setmetatable(o, { __index = function(_, k) return methods[k] end })
    if name then
        Fake.globals[name] = o
        _G[name] = o
    end
    return o
end

local TEMPLATE_CHILDREN = {
    UIPanelButtonTemplate = { fontString = "Text" },
    UICheckButtonTemplate = { fontString = "Text" },     -- FrameXML/UIPanelTemplates.xml: a 32 x 32 check box, its label "$parentText"
    UIPanelCloseButton = {},
    CharacterFrameTabButtonTemplate = { textures = { "Left", "Middle", "Right", "LeftDisabled", "MiddleDisabled", "RightDisabled", "HighlightTexture" }, fontString = "Text" },
    UIDropDownMenuTemplate = { textures = { "Left", "Middle", "Right" }, fontString = "Text" },
    InputBoxTemplate = { textures = { "Left", "Middle", "Right" } },
}

function Fake.CreateFrame(kind, name, parent, template)
    if not FRAME_KINDS[kind] then fail("CreateFrame: unknown frame type " .. tostring(kind)) end
    if kind == "Model" and Fake.failCreateModel then fail("CreateFrame: cannot create a Model frame") end
    if name ~= nil and type(name) ~= "string" then fail("CreateFrame: name must be a string") end
    if parent ~= nil and type(parent) ~= "table" then fail("CreateFrame: parent must be a frame") end
    local o = Fake.new(kind, name, parent or Fake.UIParent)
    table.insert(Fake.frames, o)
    if kind == "Model" then table.insert(Fake.models, o) end
    if template ~= nil then
        local t = TEMPLATE_CHILDREN[template]
        if not t then fail("CreateFrame: template " .. tostring(template) .. " does not exist in 1.12") end
        if t.fontString then
            local fs = Fake.new("FontString", name and (name .. t.fontString) or nil, o)
            o._.fontString = fs
        end
        for _, suffix in ipairs(t.textures or {}) do
            Fake.new("Texture", name and (name .. suffix) or nil, o)
        end
        if template == "UIDropDownMenuTemplate" then
            Fake.new("Button", name and (name .. "Button") or nil, o)
        end
        o._.template = template
    end
    if kind == "Button" or kind == "CheckButton" then
        o._.fontString = o._.fontString or Fake.new("FontString", nil, o)
    end
    return o
end

-- Runs a script handler like the 1.12 client: no arguments, globals set, restored afterwards.
function Fake.fire(o, name, globals)
    local fn = o._.scripts[name]
    if not fn then return false end
    local saved = { this = _G.this, event = _G.event }
    for i = 1, 9 do saved["arg" .. i] = _G["arg" .. i] end
    _G.this = o
    for i = 1, 9 do _G["arg" .. i] = nil end
    _G.event = nil
    for k, v in pairs(globals or {}) do _G[k] = v end
    local ok, err = pcall(fn)
    for k, v in pairs(saved) do _G[k] = v end
    if not ok then error(err, 0) end
    return true
end

-- ------------------------------------------------------------------- install
local INSTALLED = {}

local function set(name, value)
    if INSTALLED[name] == nil then
        INSTALLED[name] = { old = _G[name] }
    end
    _G[name] = value
end

-- Installs the fake globals. opts: screenW, screenH, class ("Mage","MAGE"), realm.
function Fake.install(opts)
    opts = opts or {}
    Fake.globals = {}
    Fake.frames = {}
    Fake.time = 100
    Fake.cursor = { 0, 0 }
    Fake.dropdowns = {}
    Fake.sounds = {}
    Fake.soundFiles = {}
    Fake.models = {}
    Fake.failSetModel, Fake.failCreateModel = nil, nil
    Fake.screenW = opts.screenW or 1024
    Fake.screenH = opts.screenH or 768
    Fake.class = opts.class or { "Mage", "MAGE" }
    Fake.tooltip = { lines = {}, shown = false }

    set("CreateFrame", Fake.CreateFrame)
    set("getglobal", function(n) return Fake.globals[n] end)
    set("this", nil)

    local ui = Fake.new("Frame", "UIParent", nil)
    ui._.w, ui._.h = Fake.screenW, Fake.screenH
    Fake.UIParent = ui
    set("UIParent", ui)

    local mm = Fake.new("Frame", "Minimap", ui)
    mm._.cx, mm._.cy = 100, 100
    mm._.w, mm._.h = 140, 140
    set("Minimap", mm)
    Fake.Minimap = mm

    -- Default micro-menu map button: mirror its click, tooltip binding event and
    -- Blizzard's pressed-state updates so addon replacements exercise all three.
    Fake.microUpdates, Fake.mapToggles = 0, 0
    Fake.hearthstoneKey = nil
    set("GetBindingKey", function(action)
        if action == "TOGGLEWORLDMAP" then return "M" end
        if action == "HEARTHSTONE_TOGGLE" then return Fake.hearthstoneKey end
    end)
    set("MicroButtonTooltipText", function(text, action)
        local key = GetBindingKey(action)
        return key and (text .. " (" .. key .. ")") or text
    end)
    local map = Fake.new("Frame", "WorldMapFrame", ui)
    map:Hide()
    local bar = Fake.new("Frame", "MainMenuBarArtFrame", ui)
    local mapButton = Fake.new("Button", "WorldMapMicroButton", bar)
    mapButton:SetWidth(29)
    mapButton:SetHeight(58)
    mapButton:SetPoint("BOTTOMLEFT", "SocialsMicroButton", "BOTTOMRIGHT", -2, 0)
    local menuButton = Fake.new("Button", "MainMenuMicroButton", bar)
    menuButton:SetPoint("BOTTOMLEFT", mapButton, "BOTTOMRIGHT", -3, 0)
    set("MainMenuMicroButton", menuButton)
    mapButton:SetNormalTexture("Interface\\Buttons\\UI-MicroButton-World-Up")
    mapButton:SetPushedTexture("Interface\\Buttons\\UI-MicroButton-World-Down")
    mapButton:SetDisabledTexture("Interface\\Buttons\\UI-MicroButton-World-Disabled")
    mapButton:SetHighlightTexture("Interface\\Buttons\\UI-MicroButton-Hilight")
    mapButton.tooltipText = "World Map (M)"
    mapButton:SetScript("OnEvent", function() this.tooltipText = "World Map (M)" end)
    mapButton:SetScript("OnClick", function() ToggleWorldMap() end)
    set("UpdateMicroButtons", function()
        Fake.microUpdates = Fake.microUpdates + 1
        if map:IsVisible() then
            mapButton:SetButtonState("PUSHED", 1)
        else
            mapButton:SetButtonState("NORMAL")
        end
    end)
    set("ToggleWorldMap", function()
        Fake.mapToggles = Fake.mapToggles + 1
        if map:IsShown() then map:Hide() else map:Show() end
        UpdateMicroButtons()
    end)

    local tt = Fake.new("GameTooltip", "GameTooltip", ui)
    tt.SetOwner = function(self, owner, anchor)
        if type(owner) ~= "table" then fail("GameTooltip:SetOwner needs a frame") end
        if not (anchor == "ANCHOR_LEFT" or anchor == "ANCHOR_RIGHT" or anchor == "ANCHOR_TOP" or anchor == "ANCHOR_BOTTOM"
            or anchor == "ANCHOR_CURSOR" or anchor == "ANCHOR_PRESERVE" or anchor == "ANCHOR_NONE") then
            fail("bad tooltip anchor " .. tostring(anchor))
        end
        Fake.tooltip.owner = owner
        Fake.tooltip.lines = {}
    end
    tt.SetText = function(self, text) table.insert(Fake.tooltip.lines, text) end
    tt.AddLine = function(self, text) table.insert(Fake.tooltip.lines, text) end
    tt.Show = function(self) Fake.tooltip.shown = true end
    tt.Hide = function(self) Fake.tooltip.shown = false end
    tt.IsOwned = function(self, f) return Fake.tooltip.owner == f end
    set("GameTooltip", tt)

    set("UISpecialFrames", {})
    set("GetTime", function() return Fake.time end)
    set("GetScreenWidth", function() return Fake.screenW end)
    set("GetScreenHeight", function() return Fake.screenH end)
    set("GetCursorPosition", function() return Fake.cursor[1], Fake.cursor[2] end)
    set("PlaySound", function(s) table.insert(Fake.sounds, s) end)
    set("PlaySoundFile", function(path)
        if type(path) ~= "string" then error("PlaySoundFile needs a file path", 2) end
        table.insert(Fake.soundFiles, path)
    end)
    Fake.realm = opts.realm or "Test Realm"
    set("GetRealmName", function() return Fake.realm end)
    set("UnitClass", function(u)
        if u ~= "player" then return nil end
        return Fake.class[1], Fake.class[2]
    end)
    Fake.playerName = opts.name or "Jaina"
    set("UnitName", function(u)
        if u ~= "player" then return nil end
        return Fake.playerName
    end)

    -- StaticPopup (1.12 FrameXML): the dialog table and StaticPopup_Show / StaticPopup_Hide. One
    -- dialog frame "StaticPopup1" with "StaticPopup1EditBox" and two buttons; OnShow runs with
    -- this = the dialog, OnAccept / OnCancel with this = the button (see Fake.popupClick).
    set("StaticPopupDialogs", {})
    Fake.popup = nil
    set("StaticPopup_Show", function(which, a1, a2, data)
        local info = _G.StaticPopupDialogs[which]
        if not info then return nil end
        if type(info.text) ~= "string" or type(info.button1) ~= "string" then
            error("StaticPopupDialogs." .. tostring(which) .. " needs text and button1", 2)
        end
        local dlg = Fake.globals.StaticPopup1
        if not dlg then
            dlg = Fake.new("Frame", "StaticPopup1", Fake.UIParent)
            Fake.new("EditBox", "StaticPopup1EditBox", dlg)
            Fake.new("Button", "StaticPopup1Button1", dlg)
            Fake.new("Button", "StaticPopup1Button2", dlg)
        end
        dlg._.shown = false
        dlg.which = which
        dlg.data = data
        local eb = Fake.globals.StaticPopup1EditBox
        eb._.text = ""
        eb._.shown = info.hasEditBox and true or false
        Fake.popup = { which = which, text = string.format(info.text, a1 or "", a2 or ""), dialog = dlg }
        dlg._.shown = true
        if info.OnShow then
            local saved = _G.this
            _G.this = dlg
            local ok, err = pcall(info.OnShow)
            _G.this = saved
            if not ok then error(err, 0) end
        end
        return dlg
    end)
    set("StaticPopup_Hide", function(which)
        if Fake.popup and Fake.popup.which == which then
            Fake.popup.dialog._.shown = false
            Fake.popup = nil
        end
    end)

    -- Blizzard UI helper functions (1.12 signatures)
    set("PanelTemplates_SetNumTabs", function(frame, n) frame.numTabs = n end)
    set("PanelTemplates_SetTab", function(frame, id)
        if type(frame) ~= "table" then error("PanelTemplates_SetTab(frame, id) needs the frame first", 2) end
        if not frame.numTabs then error("PanelTemplates_SetNumTabs was not called", 2) end
        frame.selectedTab = id
        for i = 1, frame.numTabs do
            if not Fake.globals[frame._.name .. "Tab" .. i] then error("tab " .. i .. " is missing", 2) end
        end
    end)
    set("UIDropDownMenu_Initialize", function(frame, fn)
        if type(frame) ~= "table" or type(fn) ~= "function" then error("UIDropDownMenu_Initialize(frame, initFunction)", 2) end
        frame.initialize = fn
        Fake.openMenu = frame
        Fake.dropdowns[frame._.name] = { items = {} }
        fn(1)
        Fake.openMenu = nil
    end)
    set("UIDropDownMenu_AddButton", function(info, level)
        if type(info) ~= "table" or not Fake.openMenu then error("UIDropDownMenu_AddButton outside of an init function", 2) end
        table.insert(Fake.dropdowns[Fake.openMenu._.name].items, info)
    end)
    set("UIDropDownMenu_SetWidth", function(width, frame)
        if type(width) ~= "number" or type(frame) ~= "table" then error("UIDropDownMenu_SetWidth(width, frame) is the 1.12 order", 2) end
        frame.ddWidth = width
    end)
    set("UIDropDownMenu_SetText", function(text, frame)
        if type(text) ~= "string" or type(frame) ~= "table" then error("UIDropDownMenu_SetText(text, frame) is the 1.12 order", 2) end
        frame.ddText = text
    end)
end

function Fake.uninstall()
    for name, v in pairs(INSTALLED) do
        _G[name] = v.old
    end
    INSTALLED = {}
    for name, obj in pairs(Fake.globals or {}) do
        if _G[name] == obj then _G[name] = nil end
    end
    _G.this = nil
end

-- Clicks button 1 (accept) or 2 (cancel) of the shown StaticPopup like 1.12 StaticPopup_OnClick:
-- this = the button, then the dialog hides.
function Fake.popupClick(index)
    local p = Fake.popup
    if not p then error("no StaticPopup is shown", 2) end
    local info = _G.StaticPopupDialogs[p.which]
    local fn = info.OnAccept
    if index == 2 then fn = info.OnCancel end
    local saved = _G.this
    _G.this = Fake.globals["StaticPopup1Button" .. index]
    local ok, err = true, nil
    if fn then ok, err = pcall(fn, p.dialog.data) end
    _G.this = saved
    p.dialog._.shown = false
    Fake.popup = nil
    if not ok then error(err, 0) end
end

-- Enter in the popup's edit box: EditBoxOnEnterPressed with this = the edit box.
function Fake.popupEnter()
    local p = Fake.popup
    if not p then error("no StaticPopup is shown", 2) end
    local info = _G.StaticPopupDialogs[p.which]
    local saved = _G.this
    _G.this = Fake.globals.StaticPopup1EditBox
    local ok, err = pcall(info.EditBoxOnEnterPressed)
    _G.this = saved
    if not p.dialog._.shown then Fake.popup = nil end
    if not ok then error(err, 0) end
end

-- Simulates a click on a dropdown item: 1.12 calls func(button.arg1, button.arg2) with this = item.
function Fake.clickDropDownItem(info)
    local item = { value = info.value, arg1 = info.arg1, arg2 = info.arg2 }
    local saved = _G.this
    _G.this = item
    local ok, err = pcall(info.func, item.arg1, item.arg2)
    _G.this = saved
    if not ok then error(err, 0) end
end
return Fake
