/*
 * mod-hearthstone: economy glue, see HsEconomyGlue.h.
 *
 * Threading. CMSG_BUY_ITEM and CMSG_OPEN_ITEM are answered in the packet hook right before the core's own handler,
 * on the thread that handler would run on (it touches only the player's own items, like the core). The gossip hooks
 * (opening the crafting access) run on map threads; the CRAFT_* / WALLET_GET ops (chat), the access sweep, the coin
 * delivery and the GM commands on the world thread. Match coins are queued where the match ends and delivered by
 * HsEconomyUpdate (the world update), which is where items and mail are created. The vendor table is an immutable
 * snapshot (shared_ptr) behind g_vendorMutex, the crafting accesses and the coin queue live behind their own mutexes;
 * all are leaf locks: nothing is called while holding them. The pushes take the session registry mutex
 * (HearthstoneScripts.cpp), so they are sent after every lock here was released.
 *
 * The crafting access never touches the NPC again after it was opened: the NPC's map and position are stored
 * and the check compares the character's own position with them (the creature may belong to another thread).
 */

#include "ScriptObjects.h"
#include "Chat.h"
#include "Creature.h"
#include "Database/DatabaseEnv.h"
#include "GossipDef.h"
#include "Item.h"
#include "Mail.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "SharedDefines.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include "HearthstoneConfig.h"
#include "HsArenaSettle.h"
#include "HsCollectionStore.h"
#include "HsEconomyGlue.h"
#include "HsIntroGlue.h"
#include "HsMatchGlue.h"

#include <cctype>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
    struct VendorRow
    {
        bool packs = false;
        bool crafting = false;
    };

    std::mutex g_vendorMutex;
    std::shared_ptr<std::map<uint32, VendorRow> const> g_vendors;

    // Character guid -> the crafting NPC it opened (where it stood).
    struct CraftAccess
    {
        uint32 accountId = 0;
        uint32 mapId = 0;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        std::string npc;
    };
    std::mutex g_craftMutex;
    std::unordered_map<uint32, CraftAccess> g_craft;
    uint32 g_sweepTimer = 0;

    // Match coins waiting for the world update (HsEconomyUpdate) to put them into the bags.
    struct CoinReward
    {
        uint32 guid = 0;
        uint32 accountId = 0;
        uint32 matchId = 0;
        uint32 coins = 0;
        bool horde = false;
    };
    std::mutex g_coinMutex;
    std::vector<CoinReward> g_coinQueue;

    std::shared_ptr<std::map<uint32, VendorRow> const> Vendors()
    {
        std::lock_guard<std::mutex> lock(g_vendorMutex);
        return g_vendors;
    }

    bool FindVendor(uint32 entry, VendorRow& out)
    {
        std::shared_ptr<std::map<uint32, VendorRow> const> const vendors = Vendors();
        if (!vendors)
            return false;
        auto const it = vendors->find(entry);
        if (it == vendors->end())
            return false;
        out = it->second;
        return true;
    }

    void SysMessage(Player* player, std::string const& text)
    {
        if (player && player->GetSession())
            ChatHandler(player->GetSession()).SendSysMessage("Hearthstone: " + text);
    }

    uint32 AccountOf(Player* player)
    {
        return player && player->GetSession() ? player->GetSession()->GetAccountId() : 0;
    }

    std::string Plural(uint32 n, char const* one, char const* many)
    {
        return std::to_string(n) + " " + (n == 1 ? one : many);
    }

    bool LoadVendorRows(std::map<uint32, VendorRow>& out)
    {
        out.clear();
        std::unique_ptr<QueryResult> table(WorldDatabase.PQuery("SHOW TABLES LIKE 'hearthstone_vendor'"));
        if (!table)
        {
            sLog.outError("Hearthstone: table hearthstone_vendor is missing in the world database "
                "(migration 0005_hearthstone_vendors.sql not applied? check Database.AutoUpdate.*): no pack vendor and "
                "no crafting NPC");
            return false;
        }
        std::unique_ptr<QueryResult> rows(WorldDatabase.PQuery(
            "SELECT `creature_entry`, `packs`, `crafting` FROM `hearthstone_vendor` ORDER BY `creature_entry`"));
        if (!rows)
            return true;
        do
        {
            Field* f = rows->Fetch();
            VendorRow row;
            row.packs = f[1].GetUInt32() != 0;
            row.crafting = f[2].GetUInt32() != 0;
            if (row.packs || row.crafting)
                out[f[0].GetUInt32()] = row;
        } while (rows->NextRow());
        return true;
    }

    void LoadEconomy()
    {
        Hs::EconomySettings const economy = sHearthstoneConfig.GetEconomy();
        HsCollections().SetDustValues(economy.dust);

        auto vendors = std::make_shared<std::map<uint32, VendorRow>>();
        LoadVendorRows(*vendors);
        uint32 packs = 0, crafting = 0;
        std::string list;
        for (auto const& entry : *vendors)
        {
            packs += entry.second.packs ? 1 : 0;
            crafting += entry.second.crafting ? 1 : 0;
            list += (list.empty() ? "" : ", ") + std::to_string(entry.first);
        }
        sLog.outString("Hearthstone: %u pack vendor(s) and %u crafting NPC(s)%s%s", packs, crafting,
            list.empty() ? "" : ": ", list.c_str());
        std::lock_guard<std::mutex> lock(g_vendorMutex);
        g_vendors = vendors;
    }

    std::string Source(Player* player)
    {
        return "char:" + std::to_string(player->GetGUIDLow());
    }

    void OpenCrafting(Player* player, Creature* creature)
    {
        uint32 const accountId = AccountOf(player);
        if (!accountId)
            return;
        if (!sHearthstoneConfig.Get().enable || !HsSessionHandshaken(player->GetGUIDLow()))
        {
            SysMessage(player, "the Hearthstone addon is not running. Install it and /reload, then talk to me again.");
            return;
        }
        CraftAccess access;
        access.accountId = accountId;
        access.mapId = creature->GetMapId();
        access.x = creature->GetPositionX();
        access.y = creature->GetPositionY();
        access.z = creature->GetPositionZ();
        access.npc = creature->GetName();
        {
            std::lock_guard<std::mutex> lock(g_craftMutex);
            g_craft[player->GetGUIDLow()] = access;
        }
        HsPushCraftAccess(accountId, true, access.npc);
        sLog.outString("Hearthstone: %s (account %u) opened crafting at %s (%u)", player->GetName(), accountId,
            creature->GetName(), creature->GetEntry());
    }

    bool InRange(Player* player, CraftAccess const& access)
    {
        return player && player->IsInWorld() && player->IsAlive() && player->GetMapId() == access.mapId &&
            player->GetDistance(access.x, access.y, access.z, SizeFactor::None) <= kCraftRange;
    }
}

void HsEconomyStartup()
{
    LoadEconomy();
}

void HsEconomyReloadConfig()
{
    LoadEconomy();
}

bool HsEconomyIsVendor(uint32_t entry)
{
    VendorRow row;
    return FindVendor(entry, row);
}

bool HsEconomyHasOptions(Player* player, uint32_t entry)
{
    VendorRow row;
    if (!FindVendor(entry, row))
        return false;
    return row.packs || (row.crafting && HsCardMasterDone(player));
}

void HsEconomyAppendGossip(Player* player, Creature* creature, GossipMenu& menu, uint32_t sender)
{
    VendorRow row;
    if (!creature || !FindVendor(creature->GetEntry(), row) || !row.crafting || !HsCardMasterDone(player))
        return;
    if (menu.MenuItemCount() >= GOSSIP_MAX_MENU_ITEMS)
        return;
    menu.AddMenuItem(GOSSIP_ICON_TRAINER, "Craft and scrap cards", sender, kHsActionCraft, "", false);
    // The core reads the parallel action data vector unchecked: keep it aligned.
    menu.AddGossipMenuItemData(0, 0, 0);
}

bool HsEconomyHandleSelect(Player* player, Creature* creature, uint32_t action)
{
    if (action != kHsActionCraft)
        return false;
    if (player && player->PlayerTalkClass)
        player->PlayerTalkClass->CloseGossip();
    VendorRow row;
    if (!player || !creature || !FindVendor(creature->GetEntry(), row) || !row.crafting)
        return true;
    if (!HsCardMasterDone(player))
    {
        SysMessage(player, "finish the introduction quests first: Byron has not shown you how crafting works yet.");
        return true;
    }
    if (!HsCollections().Catalog())
    {
        SysMessage(player, "card collections are not available right now (see the server log).");
        return true;
    }
    OpenCrafting(player, creature);
    return true;
}

uint32_t HsCoinCount(Player* player)
{
    return player ? player->GetItemCount(kHsCoinItem) : 0;
}

bool HsEconomyHandleBuy(Player* player, WorldPacket const& packet)
{
    if (!player || packet.size() < sizeof(uint64) + sizeof(uint32) + 2)
        return false;
    WorldPacket copy(packet);
    copy.rpos(0);
    ObjectGuid vendorGuid;
    uint32 item = 0;
    uint8 count = 0, unk = 0;
    copy >> vendorGuid >> item >> count >> unk;
    if (item != kHsPackCoinListing)
        return false;

    // From here on the purchase is ours: the core never sells this listing (it has no gold price).
    Creature* vendor = player->GetNPCIfCanInteractWith(vendorGuid, UNIT_NPC_FLAG_VENDOR);
    if (!vendor)
    {
        player->SendBuyError(BUY_ERR_DISTANCE_TOO_FAR, nullptr, item, 0);
        return true;
    }
    VendorRow row;
    if (!FindVendor(vendor->GetEntry(), row) || !row.packs)
    {
        player->SendBuyError(BUY_ERR_CANT_FIND_ITEM, vendor, item, 0);
        return true;
    }
    uint32 const packs = std::min<uint32>(std::max<uint32>(count, 1), kHsMaxPacksPerBuy);
    uint32 const price = sHearthstoneConfig.GetEconomy().packPriceCoins * packs;
    uint32 const have = HsCoinCount(player);
    if (have < price)
    {
        player->SendBuyError(BUY_ERR_NOT_ENOUGHT_MONEY, vendor, item, 0);
        SysMessage(player, std::string(packs == 1 ? "a booster pack costs " : std::to_string(packs) + " booster packs cost ") +
            Plural(price, "Hearthstone Coin", "Hearthstone Coins") + ", you have " + std::to_string(have) +
            ". Matches against NPCs earn coins.");
        return true;
    }
    ItemPosCountVec dest;
    if (player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, kHsPackItem, packs) != EQUIP_ERR_OK)
    {
        player->SendBuyError(BUY_ERR_CANT_CARRY_MORE, vendor, item, 0);
        return true;
    }
    // The packs first: when they cannot be stored after all, the coins stay.
    Item* bought = player->StoreNewItem(dest, kHsPackItem, true);
    if (!bought)
    {
        player->SendBuyError(BUY_ERR_CANT_CARRY_MORE, vendor, item, 0);
        return true;
    }
    player->SendNewItem(bought, packs, true, false);
    player->DestroyItemCount(kHsCoinItem, price, true);
    uint32 const accountId = AccountOf(player);
    sLog.outString("Hearthstone: %s (account %u) bought %u pack item(s) for %u coins at %s (%u): coins %u -> %u",
        player->GetName(), accountId, packs, price, vendor->GetName(), vendor->GetEntry(), have, HsCoinCount(player));
    HsPushWalletChanged(accountId, "buy", 0);
    return true;
}

bool HsEconomyHandleOpenItem(Player* player, WorldPacket const& packet)
{
    if (!player || packet.size() < 2)
        return false;
    WorldPacket copy(packet);
    copy.rpos(0);
    uint8 bag = 0, slot = 0;
    copy >> bag >> slot;
    Item* item = player->GetItemByPos(bag, slot);
    if (!item || item->GetEntry() != kHsPackItem)
        return false;

    // From here on the item is ours (it has no loot of its own).
    if (item->IsInTrade())
        return true;
    uint32 const accountId = AccountOf(player);
    if (!accountId || !HsCollections().Catalog())
    {
        SysMessage(player, "card collections are not available right now (see the server log). The pack stays in your bag.");
        return true;
    }
    // The pack first, then the item: a failed grant keeps the item.
    Hs::PackStockResult const r = HsCollections().GrantPacks(accountId, 1, "pack_item", Source(player));
    if (r.status == Hs::PackStatus::CommitFailed)
    {
        // The outcome is unknown (the pack may be on the account): the item is used up all the same, so a second
        // right-click cannot add the pack twice. A GM can check with .hs pack show.
        uint32 one = 1;
        player->DestroyItemCount(item, one, true);
        player->SaveInventoryAndGoldToDB();
        sLog.outError("Hearthstone: %s (account %u) pack item: the commit outcome is unknown; the item was used up",
            player->GetName(), accountId);
        SysMessage(player, "something went wrong while adding the pack. If it is not under /hs packs, ask a game master.");
        return true;
    }
    if (r.status != Hs::PackStatus::Ok)
    {
        if (r.status == Hs::PackStatus::Overflow)
            SysMessage(player, "you cannot hold more unopened packs. Open some first (/hs packs).");
        else
        {
            SysMessage(player, "the pack could not be added right now. It stays in your bag; try again later.");
            sLog.outError("Hearthstone: %s (account %u) could not add a pack item: %s", player->GetName(), accountId,
                Hs::PackStatusText(r.status));
        }
        return true;
    }
    uint32 one = 1;
    player->DestroyItemCount(item, one, true);
    player->SaveInventoryAndGoldToDB(); // the pack is committed: persist the removal too, so a crash cannot keep both
    sLog.outString("Hearthstone: %s (account %u) added a pack item to the account: packs %u -> %u", player->GetName(),
        accountId, r.before.stock, r.after.stock);
    HsPushPackChanged(accountId, r.after.stock);
    return true;
}

void HsEconomyMatchEnded(Hs::MatchEnd const& end)
{
    if (!end.meta.accountId)
        return;
    uint32 const coins = Hs::MatchCoins(sHearthstoneConfig.GetEconomy().rewards, end.res, end.why, end.difficulty);
    if (coins == 0)
        return;
    CoinReward reward;
    reward.guid = end.guid;
    reward.accountId = end.meta.accountId;
    reward.matchId = end.matchId;
    reward.coins = coins;
    std::lock_guard<std::mutex> lock(g_coinMutex);
    g_coinQueue.push_back(reward);
}

void HsEconomyBattlegroundEnded(uint32_t guid, uint32_t accountId, uint32_t coins)
{
    if (!accountId || coins == 0)
        return;
    CoinReward reward;
    reward.guid = guid;
    reward.accountId = accountId;
    reward.coins = coins;
    std::lock_guard<std::mutex> lock(g_coinMutex);
    g_coinQueue.push_back(reward);
}

namespace
{
    // Mails coins to a character, one stack per mail (a 1.12 mail carries one item), from a Hearthstone Master.
    void MailCoins(ObjectGuid guid, Player* player, uint32 coins, bool horde)
    {
        uint32 const stack = 250;
        while (coins > 0)
        {
            uint32 const n = std::min(coins, stack);
            coins -= n;
            Item* item = Item::CreateItem(kHsCoinItem, n, player);
            if (!item)
            {
                sLog.outError("Hearthstone: could not create %u coin items for guid %u", n, guid.GetCounter());
                return;
            }
            item->SaveToDB(); // the mail loads it from the database
            MailDraft("Hearthstone Coins", "Your bags were full after your Hearthstone match. Here are your " +
                std::to_string(n) + " Hearthstone Coins.")
                .AddItem(item)
                .SendMailTo(player ? MailReceiver(player) : MailReceiver(guid),
                    MailSender(MAIL_CREATURE, horde ? 919501u : 919502u));
        }
    }

    void DeliverCoins(CoinReward const& reward)
    {
        ObjectGuid const guid(HIGHGUID_PLAYER, reward.guid);
        Player* player = ObjectAccessor::FindPlayer(guid);
        if (!player || !player->IsInWorld())
        {
            MailCoins(guid, nullptr, reward.coins, reward.horde);
            sLog.outString("Hearthstone: match #%u: %u coins mailed to guid %u (not in the world)", reward.matchId,
                reward.coins, reward.guid);
            return;
        }
        uint32 stored = reward.coins;
        uint32 noSpace = 0;
        ItemPosCountVec dest;
        if (player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, kHsCoinItem, stored, &noSpace) != EQUIP_ERR_OK)
            stored = stored > noSpace ? stored - noSpace : 0;
        if (stored > 0 && !dest.empty())
        {
            if (Item* item = player->StoreNewItem(dest, kHsCoinItem, true))
                player->SendNewItem(item, stored, true, false);
            else
                stored = 0;
        }
        else
            stored = 0;
        uint32 const mailed = reward.coins - stored;
        if (mailed > 0)
        {
            MailCoins(guid, player, mailed, player->GetTeam() == HORDE);
            SysMessage(player, "your bags are full: " + Plural(mailed, "Hearthstone Coin was", "Hearthstone Coins were") +
                " sent to you by mail.");
        }
        sLog.outString("Hearthstone: match #%u paid %u coins to %s (account %u): %u in the bags, %u by mail", reward.matchId,
            reward.coins, player->GetName(), reward.accountId, stored, mailed);
        HsPushWalletChanged(reward.accountId, "match", reward.coins);
    }
}

bool HsCraftAccess(Player* player, std::string& npcName)
{
    if (!player)
        return false;
    uint32 const guid = player->GetGUIDLow();
    std::lock_guard<std::mutex> lock(g_craftMutex);
    auto const it = g_craft.find(guid);
    if (it == g_craft.end())
        return false;
    if (!InRange(player, it->second))
        return false; // the sweep closes it and tells the client
    npcName = it->second.npc;
    return true;
}

void HsEconomyUpdate(uint32_t diff)
{
    // Arena settlement recovery and the delivery of arena prize coins (also on the world thread).
    HsArenaSettleUpdate(diff);

    std::vector<CoinReward> rewards;
    {
        std::lock_guard<std::mutex> lock(g_coinMutex);
        rewards.swap(g_coinQueue);
    }
    for (CoinReward const& reward : rewards)
        DeliverCoins(reward);

    g_sweepTimer += diff;
    if (g_sweepTimer < 500)
        return;
    g_sweepTimer = 0;
    std::unordered_map<uint32, CraftAccess> open;
    {
        std::lock_guard<std::mutex> lock(g_craftMutex);
        if (g_craft.empty())
            return;
        open = g_craft;
    }
    // Checked without the lock (FindPlayer takes the core's own lock).
    std::vector<std::pair<uint32, uint32>> closed; // guid, account
    for (auto const& entry : open)
        if (!InRange(ObjectAccessor::FindPlayer(ObjectGuid(HIGHGUID_PLAYER, entry.first)), entry.second))
            closed.emplace_back(entry.first, entry.second.accountId);
    if (closed.empty())
        return;
    {
        std::lock_guard<std::mutex> lock(g_craftMutex);
        for (auto const& c : closed)
            g_craft.erase(c.first);
    }
    for (auto const& c : closed)
        HsPushCraftAccess(c.second, false, std::string());
}

void HsEconomyLogout(uint32_t guidLow)
{
    std::lock_guard<std::mutex> lock(g_craftMutex);
    g_craft.erase(guidLow);
}

// ---- GM commands ----------------------------------------------------------------------

void HsWalletUsage(ChatHandler* handler)
{
    handler->SendSysMessage("Usage: .hs wallet show <player|#accountId>");
    handler->SendSysMessage("       .hs wallet dust <player|#accountId> <+n|-n>  (at most 1000000 at a time)");
    handler->SendSysMessage("       (coins are items: .additem 919601 <count>, packs: .additem 919602 <count>)");
}

void HsWalletCommand(ChatHandler* handler, int security, std::vector<std::string> const& args)
{
    std::string sub = args.size() > 1 ? args[1] : std::string();
    for (char& c : sub)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    bool const mutating = sub == "dust";
    if ((sub != "show" && !mutating) || args.size() < 3 || (sub == "show" && args.size() != 3) ||
        (mutating && args.size() != 4))
    {
        HsWalletUsage(handler);
        return;
    }
    if (mutating && security < static_cast<int>(SEC_DEVELOPER))
    {
        handler->SendSysMessage("Hearthstone: .hs wallet dust needs security level 3 (developer) or higher.");
        return;
    }
    if (!HsCollections().Catalog())
    {
        handler->SendSysMessage("Hearthstone: wallets are UNAVAILABLE: the card data could not be loaded at startup "
            "(see the server log).");
        return;
    }
    uint32_t accountId = 0;
    std::string label;
    if (!HsResolveAccountTarget(handler, args[2], accountId, label))
        return;

    if (sub == "show")
    {
        Hs::Wallet wallet;
        if (!HsCollections().GetWallet(accountId, wallet))
        {
            handler->SendSysMessage("Hearthstone: the wallet of " + label + " could not be read (see the server log).");
            return;
        }
        handler->SendSysMessage("Hearthstone: wallet of " + label + ": " + std::to_string(wallet.dust) +
            " dust (Hearthstone Coins are items in the bags).");
        return;
    }

    std::string const& amount = args[3];
    bool const negative = !amount.empty() && amount[0] == '-';
    std::string const digits = (!amount.empty() && (amount[0] == '-' || amount[0] == '+')) ? amount.substr(1) : amount;
    uint32 value = 0;
    bool ok = !digits.empty() && digits.size() <= 7;
    for (char c : digits)
        ok = ok && c >= '0' && c <= '9';
    if (ok)
        value = static_cast<uint32>(std::stoul(digits));
    if (!ok || value == 0 || value > Hs::kMaxBalanceChange)
    {
        handler->SendSysMessage("Hearthstone: '" + amount + "' is not an amount (+1 .. +1000000 or -1 .. -1000000).");
        return;
    }
    int64 const delta = negative ? -static_cast<int64>(value) : static_cast<int64>(value);
    Hs::WalletResult const r = HsCollections().ChangeDust(accountId, delta, "gm_dust", HsGmSource(handler));
    if (r.status != Hs::WalletStatus::Ok)
    {
        handler->SendSysMessage("Hearthstone: rejected, " + std::string(Hs::WalletStatusText(r.status)) + " (" +
            std::to_string(r.before.dust) + " dust). Nothing was changed.");
        return;
    }
    handler->SendSysMessage("Hearthstone: dust of " + label + " changed by " + amount + ": now " +
        std::to_string(r.after.dust) + " dust.");
    sLog.outString("Hearthstone: GM %s changed the dust of account %u by %s: %u -> %u", HsGmName(handler).c_str(),
        accountId, amount.c_str(), r.before.dust, r.after.dust);
    HsPushWalletChanged(accountId, "gm", 0);
}

void HsEconomyStatus(ChatHandler* handler)
{
    Hs::EconomySettings const e = sHearthstoneConfig.GetEconomy();
    std::shared_ptr<std::map<uint32, VendorRow> const> const vendors = Vendors();
    std::size_t open = 0;
    {
        std::lock_guard<std::mutex> lock(g_craftMutex);
        open = g_craft.size();
    }
    uint32 crafting = 0;
    if (vendors)
        for (auto const& entry : *vendors)
            crafting += entry.second.crafting ? 1 : 0;
    handler->SendSysMessage("Economy: pack " + std::to_string(e.packPriceCoins) + " coins (item " +
        std::to_string(kHsCoinItem) + "), match coins loss " + std::to_string(e.rewards.loss) + " win " +
        std::to_string(e.rewards.winNormal) + "/" + std::to_string(e.rewards.winHard) + "/" +
        std::to_string(e.rewards.winBoss) + ", vendors " + std::to_string(vendors ? vendors->size() : 0) + " (" +
        std::to_string(crafting) + " crafting), open crafting " + std::to_string(open));
}
