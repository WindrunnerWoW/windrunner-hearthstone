-- Runs with the main suite's protocol/data/UI globals and strict WoW 1.12 fake.
return function(check)
    local H, LU, P = Hearthstone, Hearthstone.LadderUI, HsProtocol
    local F = dofile("tests/fake_wow.lua")
    F.install({ screenW = 1920, screenH = 1080 })
    LU.frame, LU.request, LU.view, LU.chosen, LU.deckId, LU.standing = nil, nil, nil, nil, nil, nil
    local kv = {{"st", "offers"}, {"rank", "12"}, {"stars", "2"}, {"wins", "7"}, {"losses", "4"},
        {"draws", "1"}, {"maxstars", "3"}, {"legendpos", "0"}, {"n", "3"}, {"dn", "8"}}
    local names, classes = {"Amaz Chan", "The Firebat", "Ibelin Redmoore"}, {"MAGE", "WARRIOR", "PRIEST"}
    for i = 1, 3 do
        local p = "o" .. i
        table.insert(kv, {p .. "id", tostring(100 + i)})
        table.insert(kv, {p .. "name", names[i]})
        table.insert(kv, {p .. "class", classes[i]})
        table.insert(kv, {p .. "faction", i == 2 and "Horde" or "Alliance"})
        table.insert(kv, {p .. "rank", "12"}); table.insert(kv, {p .. "stars", "1"})
        table.insert(kv, {p .. "wins", "2"}); table.insert(kv, {p .. "losses", "1"})
        table.insert(kv, {p .. "draws", "0"})
    end
    for i = 1, 8 do
        table.insert(kv, {"d" .. i .. "id", tostring(i)})
        table.insert(kv, {"d" .. i .. "name", "Deck " .. i .. " ; test=escape"})
    end
    local payload = P.KvEncode(kv)
    check(LU.Parse(payload) and LU.Parse(payload).decks[1].name == "Deck 1 ; test=escape", "escaped deck labels")
    check(not LU.Parse("st=offers;n=3;dn=0"), "incomplete view rejected")
    check(not LU.Parse(string.gsub(payload, "o2id=102", "o2id=101")), "duplicate rivals rejected")
    check(not LU.Parse(string.gsub(payload, "o1class=MAGE", "o1class=INVALID")), "invalid hero rejected")
    local savedRequest = H.Request
    local requests = {}
    H.Request = function(op, body)
        table.insert(requests, {op = op, body = P.KvDecode(body)})
        return tostring(table.getn(requests))
    end
    H.Dispatch({op = "LADDER_OPEN", reqId = "0", payload = payload})
    local f = LU.frame
    check(f and f:IsShown() and f.opponents[3]:IsShown(), "chair push opens three rivals")
    local badge = f.rankBadge
    check(badge:IsShown() and badge.icon:GetTexture() == "Interface\\AddOns\\Hearthstone\\Textures\\UI\\LadderRank_12",
        "top-left badge uses the actual rank artwork")
    check(badge.stars[3]:IsShown() and not badge.stars[4]:IsShown(), "rank 12 has three star slots")
    check(string.find(badge.stars[2]:GetTexture(), "LadderStar_full", 1, true)
        and string.find(badge.stars[3]:GetTexture(), "LadderStar_empty", 1, true), "earned and missing stars use their own textures")
    check(f.opponents[2].name:GetText() == names[2] and not string.find(f.opponents[2].stats:GetText(), "Rank", 1, true)
        and not string.find(f.opponents[2].stats:GetText(), "record", 1, true), "name and class only")
    check(LU.deckId == 1, "first ready deck selected")
    F.fire(f.deck, "OnClick", {})
    check(f.menu:IsShown() and f.menu.next:IsEnabled(), "deck list paginates")
    F.fire(f.menu.next, "OnClick", {})
    F.fire(f.menu.rows[2], "OnClick", {})
    check(LU.deckId == 8 and not f.menu:IsShown(), "choose a deck from second page")
    F.fire(f.opponents[2], "OnClick", {})
    check(LU.chosen.id == 102 and f.heroView.host:IsShown() and f.heroView.dim:IsShown(), "hero opens over dim background")
    check(not badge:IsShown(), "hero preview hides the underlying rank display")
    check(table.getn(requests) == 0, "preview sends no request")
    F.fire(f.heroView.dim, "OnMouseUp", {})
    check(not LU.chosen and not f.heroView.host:IsShown(), "click beside hero goes back")
    check(badge:IsShown(), "rank display returns after Back")
    F.fire(f.opponents[1], "OnClick", {})
    F.fire(f.heroView.done, "OnClick", {})
    check(requests[1].op == "LADDER_PLAY" and requests[1].body.npc == "101" and requests[1].body.deck == "8", "Done sends opponent and deck")
    LU.Confirm()
    check(table.getn(requests) == 1 and not f.heroView.done:IsEnabled(), "double click blocked")
    H.Dispatch({op = "ERR", reqId = "1", payload = P.KvEncode({{"code", "REFUSED"}, {"msg", "Deck changed"}})})
    check(not LU.request and LU.message == "Deck changed" and f.heroView.done:IsEnabled(), "refusal keeps selection and allows retry")
    H.Dispatch({op = "LADDER_OPEN", reqId = "0", payload = "st=paused;duel=92;npc=101;name=Amaz;class=MAGE"})
    check(f.resume:IsShown() and not f.deck:IsShown() and not f.opponents[1]:IsShown(), "paused duel replaces offers")
    check(badge:IsShown() and LU.standing.rank == 12, "legacy paused reply retains the last known standing")
    F.fire(f.surrender, "OnClick", {})
    check(F.popup and table.getn(requests) == 1, "surrender asks before sending")
    F.popupClick(2)
    check(table.getn(requests) == 1, "cancel surrender sends nothing")
    F.fire(f.surrender, "OnClick", {})
    F.popupClick(1)
    check(requests[2].op == "LADDER_GIVE" and requests[2].body.duel == "92", "confirmed surrender pins the paused duel")
    H.Dispatch({op = "ERR", reqId = "2", payload = P.KvEncode({{"code", "REFUSED"}, {"msg", "Result pending"}})})
    F.fire(f.resume, "OnClick", {})
    check(requests[3].op == "LADDER_RES" and requests[3].body.duel == "92", "resume request pins the paused duel")
    H.Dispatch({op = "LADDER", reqId = "3", payload = "st=waiting;name=Amaz"})
    check(not f.resume:IsShown() and not LU.request and LU.view.st == "waiting", "walk-up waiting state")
    H.Dispatch({op = "LADDER_CLOSE", reqId = "0", payload = ""})
    check(not f:IsShown(), "standing up closes the screen")
    H.Dispatch({op = "LADDER", reqId = "3", payload = payload})
    check(not f:IsShown() and LU.view.st == "waiting", "late reply does not reopen or change state")
    local noDecks = string.gsub(payload, "dn=8", "dn=0")
    H.Dispatch({op = "LADDER_OPEN", reqId = "0", payload = noDecks})
    F.fire(f.opponents[1], "OnClick", {})
    check(not f.heroView.done:IsEnabled() and not LU.deckId, "no playable deck disables Done")
    LU.Confirm()
    check(table.getn(requests) == 3, "no-deck confirmation sends nothing")
    -- All 25 ranks use the server's supplied capacity, including the future 2- and 4-star bands.
    for rank = 1, 25 do
        local max = rank >= 21 and 2 or (rank >= 16 and 4 or (rank >= 11 and 3 or 5))
        local future = string.gsub(payload, "rank=12", "rank=" .. rank)
        future = string.gsub(future, "maxstars=3", "maxstars=" .. max)
        H.Dispatch({op = "LADDER_OPEN", reqId = "0", payload = future})
        check(LU.standing.rank == rank and LU.standing.maxStars == max, "rank " .. rank .. " parses its server capacity")
        check(badge.stars[max]:IsShown() and (max == 5 or not badge.stars[max+1]:IsShown()), "rank " .. rank .. " shows exactly its slots")
        check(badge.icon:IsShown() and not badge.fallback:IsShown()
            and badge.icon:GetTexture() == "Interface\\AddOns\\Hearthstone\\Textures\\UI\\LadderRank_" .. rank,
            "rank " .. rank .. " uses its own artwork")
    end
    local legend = string.gsub(payload, "rank=12", "rank=0")
    legend = string.gsub(legend, ";stars=2", ";stars=0")
    legend = string.gsub(legend, "maxstars=3", "maxstars=0")
    legend = string.gsub(legend, "legendpos=0", "legendpos=7")
    H.Dispatch({op = "LADDER_OPEN", reqId = "0", payload = legend})
    check(badge.number:GetText() == "7" and badge.number:IsShown() and string.find(badge.icon:GetTexture(), "LadderLegend", 1, true),
        "Legend gem displays the actual server position")
    for i = 1, 5 do check(not badge.stars[i]:IsShown(), "Legend has no star " .. i) end
    H.Dispatch({op = "LADDER_OPEN", reqId = "0", payload = string.gsub(legend, "legendpos=7", "legendpos=12345")})
    check(badge.number:GetText() == "12345" and badge.number:IsShown(), "five-digit Legend position is shown")
    check(not LU.Parse(string.gsub(payload, "maxstars=3", "maxstars=1")), "standing with too many stars is rejected")
    check(not LU.Parse(string.gsub(payload, "rank=12", "rank=26")), "rank above the supported range is rejected")
    F.missingTextures = { ["Interface\\AddOns\\Hearthstone\\Textures\\UI\\LadderRank_12"] = true }
    H.Dispatch({op = "LADDER_OPEN", reqId = "0", payload = payload})
    check(badge.fallback:IsShown() and badge.fallback.label:GetText() == "Rank 12", "missing deployed artwork has a readable rank fallback")
    F.missingTextures = nil
    LU.request = "99"
    LU.Hide()
    check(LU.request == nil, "closing the window drops a pending request")
    -- the default deck (middle click in the Decks tab): opened selected, listed first and starred
    HearthstoneDB = { defaultDeck = 5 }
    H.Dispatch({op = "LADDER_OPEN", reqId = "0", payload = payload})
    check(LU.deckId == 5 and LU.MenuDecks()[1].id == 5 and LU.MenuDecks()[2].id == 1, "default deck selected and listed first")
    F.fire(f.deck, "OnClick", {})
    check(f.menu.rows[1].star:IsShown() and not f.menu.rows[2].star:IsShown(), "only the default row shows the star")
    F.fire(f.menu.rows[2], "OnClick", {})
    check(LU.deckId == 1, "another deck can still be chosen")
    HearthstoneDB.defaultDeck = 99
    H.Dispatch({op = "LADDER_OPEN", reqId = "0", payload = payload})
    check(LU.deckId == 1 and LU.MenuDecks()[1].id == 1, "a default that is not listed is ignored")
    HearthstoneDB = nil
    H.Request = savedRequest
    F.uninstall()
end
