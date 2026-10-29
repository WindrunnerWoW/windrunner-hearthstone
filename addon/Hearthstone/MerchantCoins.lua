-- Hearthstone addon: the coin price of a booster pack in the vendor window (economy).
--
-- The 1.12 vendor window knows only gold prices (item costs arrived with TBC's ItemExtendedCost), so the coin
-- listing of a Hearthstone Master (item 919603, no gold price; the server takes Hearthstone.Pack.PriceCoins coin items
-- and gives a booster pack) would show as free. This file decorates the window after Blizzard's own update
-- (MerchantFrame_UpdateMerchantInfo): on that listing the money frame is hidden and "<price> [coin icon]" shown
-- instead, red with a red item icon when the bags hold fewer coins, like the client marks what you cannot afford. The
-- tooltip gets "You have N Hearthstone Coins". The price comes from the server (WALLET packcoins=). Display only: the
-- server still checks every purchase.
--
-- MC.State(link, coins, price) decides everything without frames (the tests use it).
--
-- Lua 5.0 / WoW 1.12: no '#', no '...'.

Hearthstone = Hearthstone or {}
local H = Hearthstone
local W = HsWallet
H.MerchantCoins = H.MerchantCoins or {}
local MC = H.MerchantCoins

MC.LISTING_ITEM = 919603      -- "Hearthstone Booster Pack" for coins (world migration 0006)
MC.ICON = "Interface\\Icons\\INV_Misc_Coin_02"
MC.overlays = MC.overlays or {}

-- Is the merchant item link the coin listing?
function MC.IsCoinListing(link)
    return type(link) == "string" and string.find(link, "item:" .. MC.LISTING_ITEM .. ":", 1, true) ~= nil
end

-- The coin price of a pack, or nil while the server has not told it.
function MC.Price()
    if W and W.packCoins and W.packCoins > 0 then
        return W.packCoins
    end
    return nil
end

-- Hearthstone Coin items in the bags, counted now (the vendor window must not wait for a WALLET_UPD).
function MC.Coins()
    local n = W and W.CountBags(GetContainerNumSlots, GetContainerItemLink, GetContainerItemInfo)
    if n == nil then
        n = W and W.coins or 0
    end
    return n
end

-- nil when the link is not the coin listing; otherwise { text, affordable, coins, price }. With an unknown price the
-- listing is shown as "?" and never marked unaffordable.
function MC.State(link, coins, price)
    if not MC.IsCoinListing(link) then
        return nil
    end
    coins = coins or 0
    local st = { coins = coins, price = price }
    if price then
        st.text = tostring(price)
        st.affordable = coins >= price
    else
        st.text = "?"
        st.affordable = true
    end
    return st
end

function MC.TooltipLine(st)
    local have = st.coins == 1 and "1 Hearthstone Coin" or (st.coins .. " Hearthstone Coins")
    if st.price and not st.affordable then
        return "|cffff2020You have " .. have .. ".|r"
    end
    return "|cffffffffYou have " .. have .. ".|r"
end

-- The overlay of vendor slot i: "<price>" and the coin icon where the money frame was.
local function overlay(i, item, money)
    local o = MC.overlays[i]
    if o then
        return o
    end
    o = CreateFrame("Frame", nil, item)
    o:SetWidth(90)
    o:SetHeight(14)
    o:SetPoint("LEFT", money, "LEFT", 0, 0)
    o.text = o:CreateFontString(nil, "OVERLAY", "NumberFontNormal")
    o.text:SetPoint("LEFT", o, "LEFT", 0, 0)
    o.icon = o:CreateTexture(nil, "OVERLAY")
    o.icon:SetWidth(13)
    o.icon:SetHeight(13)
    o.icon:SetPoint("LEFT", o.text, "RIGHT", 2, 0)
    o.icon:SetTexture(MC.ICON)
    MC.overlays[i] = o
    return o
end

-- Redecorates the visible vendor slots. Called after Blizzard's update and when the coins in the bags change.
function MC.Update()
    if not MerchantFrame or not MerchantFrame:IsVisible() or type(GetMerchantItemLink) ~= "function" then
        return
    end
    local perPage = MERCHANT_ITEMS_PER_PAGE or 10
    local page = MerchantFrame.page or 1
    local buyback = MerchantFrame.selectedTab == 2
    local coins, price = MC.Coins(), MC.Price()
    for i = 1, perPage do
        local item = getglobal("MerchantItem" .. i)
        local money = getglobal("MerchantItem" .. i .. "MoneyFrame")
        local st = nil
        if item and money and not buyback then
            st = MC.State(GetMerchantItemLink((page - 1) * perPage + i), coins, price)
        end
        local o = MC.overlays[i]
        if st then
            o = overlay(i, item, money)
            money:Hide()
            o.text:SetText(st.text)
            local button = getglobal("MerchantItem" .. i .. "ItemButton")
            if st.affordable then
                o.text:SetTextColor(1, 1, 1)
            else
                o.text:SetTextColor(1, 0.1, 0.1)
            end
            if button and SetItemButtonTextureVertexColor then
                if st.affordable then
                    SetItemButtonTextureVertexColor(button, 1, 1, 1)
                else
                    SetItemButtonTextureVertexColor(button, 0.9, 0, 0)
                end
            end
            o:Show()
            o.hidMoney = money
        elseif o then
            o:Hide()
            if o.hidMoney and item and item:IsShown() and not buyback then
                o.hidMoney:Show()  -- the slot now holds another item: give its gold price back
            end
            o.hidMoney = nil
        end
    end
end

-- Hooks the vendor window and its tooltip once (FrameXML loads before the addons).
function MC.Install()
    if MC.installed or type(MerchantFrame_UpdateMerchantInfo) ~= "function" then
        return
    end
    MC.installed = true
    local origUpdate = MerchantFrame_UpdateMerchantInfo
    MerchantFrame_UpdateMerchantInfo = function()
        origUpdate()
        MC.Update()
    end
    if GameTooltip and type(GameTooltip.SetMerchantItem) == "function" then
        local origTip = GameTooltip.SetMerchantItem
        GameTooltip.SetMerchantItem = function(self, index)
            local r = origTip(self, index)
            local st = MC.State(GetMerchantItemLink and GetMerchantItemLink(index), MC.Coins(), MC.Price())
            if st then
                self:AddLine(MC.TooltipLine(st))
                self:Show()
            end
            return r
        end
    end
end

MC.Install()
