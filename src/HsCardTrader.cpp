#include "HsCardTrader.h"
#include "HearthstoneConfig.h"
#include "HsCollectionStore.h"
#include "HsEconomyGlue.h"
#include "collection/HsTraderRules.h"
#include "quests/HsHordeRogueRules.h"
#include "quests/HsAllianceRogueRules.h"
#include "Chat.h"
#include "Creature.h"
#include "Database/DatabaseEnv.h"
#include "GossipDef.h"
#include "Log.h"
#include "Player.h"
#include "WorldSession.h"
#include <algorithm>
#include <ctime>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace
{
    // A trader sells the daily card to characters that rewarded its unlock quest.
    struct Trader
    {
        uint32 entry;
        uint32 unlockQuest;
    };
    constexpr Trader kTraders[] = {
        { kHsHordeRogueSable, kHsHordeRogueRecovery },
        { kHsAllianceRogue, kHsAllianceRogueCards }
    };
    constexpr uint32 kActionBuy = 0x485300e0u;

    struct Daily
    {
        uint64 period = 0;
        std::string id;
        std::string name;
        uint32 price = 0;
    };
    std::mutex g_mutex;
    Daily g_daily;
    bool g_purchasesReady = false;

    // The card is a pure function of the UTC period and the exclusions: no stored rotation,
    // so a restart never rerolls. Reloading the exclusions can change today's pick.
    Daily Today()
    {
        uint64 const period = Hs::TraderPeriod(static_cast<uint64>(std::time(nullptr)),
            sHearthstoneConfig.GetEconomy().traderSwapSeconds);
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_daily.period == period)
            return g_daily;
        auto catalog = HsCollections().Catalog();
        if (!catalog)
            return Daily();
        g_daily = Daily();
        g_daily.period = period;
        auto const excluded = Hs::TraderExclusions(sHearthstoneConfig.GetEconomy().traderExcludedCards, *catalog);
        std::vector<std::string> pool;
        catalog->ForEach([&](std::string const& id, Hs::CardRule const& rule)
        {
            if (Hs::TraderEligible(id, rule, excluded))
                pool.push_back(id);
        });
        if (pool.empty())
            return g_daily;
        std::sort(pool.begin(), pool.end());
        g_daily.id = pool[Hs::TraderPick(pool.size(), period)];
        g_daily.price = Hs::TraderPrice(catalog->Find(g_daily.id)->rarity);
        g_daily.name = g_daily.id;
        // Card ids are [A-Za-z0-9_]{1,32}, safe to inline.
        std::unique_ptr<QueryResult> row(WorldDatabase.PQuery(
            "SELECT `name` FROM `hearthstone_card_template` WHERE `id`='%s' LIMIT 1", g_daily.id.c_str()));
        if (row)
            g_daily.name = row->Fetch()[0].GetCppString();
        return g_daily;
    }

    bool CanTrade(Player* player, Creature* creature)
    {
        if (!player || !creature || !sHearthstoneConfig.Get().enable)
            return false;
        for (Trader const& trader : kTraders)
            if (trader.entry == creature->GetEntry() && player->GetQuestRewardStatus(trader.unlockQuest))
                return true;
        return false;
    }

    void Say(Player* player, std::string const& text)
    {
        ChatHandler(player->GetSession()).SendSysMessage(text.c_str());
    }

    std::string Gold(uint32 copper)
    {
        return std::to_string(copper / 10000) + "g";
    }
}

void HsCardTraderLoad()
{
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_daily = Daily();
    }
    std::unique_ptr<QueryResult> purchases(CharacterDatabase.Query("SELECT COUNT(*) FROM hearthstone_trader_purchase"));
    g_purchasesReady = purchases != nullptr;
    if (!g_purchasesReady)
        sLog.outError("Hearthstone: daily card purchases unavailable, apply character migration 0009");
}

void HsCardTraderAppendGossip(Player* player, Creature* creature, GossipMenu& menu, uint32_t sender)
{
    if (!CanTrade(player, creature) || menu.MenuItemCount() >= GOSSIP_MAX_MENU_ITEMS)
        return;
    Daily const daily = Today();
    if (daily.id.empty())
        return;
    std::string const price = Gold(daily.price);
    menu.AddMenuItem(GOSSIP_ICON_MONEY_BAG, "Today's card: " + daily.name + " (" + price + ")",
        sender, kActionBuy, "Buy " + daily.name + " for " + price + "?", false);
    menu.AddGossipMenuItemData(0, 0, 0);
}

bool HsCardTraderHandleSelect(Player* player, Creature* creature, uint32_t action)
{
    if (action != kActionBuy)
        return false;
    if (!player || !player->PlayerTalkClass)
        return true;
    player->PlayerTalkClass->CloseGossip();
    if (!CanTrade(player, creature) || !player->IsAlive())
        return true;
    Daily const daily = Today();
    if (daily.id.empty() || !g_purchasesReady)
    {
        Say(player, "No card is for sale right now.");
        return true;
    }
    if (player->GetMoney() < daily.price)
    {
        Say(player, "You need " + Gold(daily.price) + " for today's card.");
        return true;
    }
    uint32 const account = player->GetSession()->GetAccountId();
    std::string const period = std::to_string(daily.period);
    // One card per account and period, whichever character or trader: the primary key is the limit.
    // Reserve first so concurrent buyers cannot both pass; release if the grant fails.
    if (!CharacterDatabase.DirectPExecute(
        "INSERT INTO hearthstone_trader_purchase (period_start,account_id,card_id) VALUES (%s,%u,'%s')",
        period.c_str(), account, daily.id.c_str()))
    {
        Say(player, "You already bought a card today. Come back after the next swap.");
        return true;
    }
    // The gold is taken and saved before the card is added: a crash in between costs the player, it never gives a
    // free card.
    player->LogModifyMoney(-int32(daily.price), "HsDailyCard", creature->GetObjectGuid(), 0);
    player->SaveInventoryAndGoldToDB();
    Hs::MutationResult const result = HsCollections().Grant(account, daily.id, 1, Hs::GrantSource::Pack,
        Hs::ProtectionMode::Unprotected, "daily_card", "char:" + std::to_string(player->GetGUIDLow()));
    if (result.status == Hs::MutationStatus::CommitFailed)
    {
        // The outcome is unknown (the card may be in the collection): the purchase and the payment stand, so a retry
        // cannot add a second card. A GM can check with .hs coll show.
        sLog.outError("Hearthstone: daily card %s for account %u (%s): the commit outcome is unknown; purchase kept",
            daily.id.c_str(), account, player->GetName());
        Say(player, "Something went wrong while adding the card. If it is not in your collection, ask a game master.");
        return true;
    }
    if (result.status != Hs::MutationStatus::Ok)
    {
        if (!CharacterDatabase.DirectPExecute(
            "DELETE FROM hearthstone_trader_purchase WHERE period_start=%s AND account_id=%u", period.c_str(), account))
            sLog.outError("Hearthstone: failed to release daily card allowance for account %u", account);
        player->LogModifyMoney(int32(daily.price), "HsDailyCardRefund", creature->GetObjectGuid(), 0);
        player->SaveInventoryAndGoldToDB();
        Say(player, "The card could not be added. Try again later.");
        return true;
    }
    HsPushCardChanged(account, result.mutation.versionAfter);
    Say(player, "Added to your Hearthstone collection: " + daily.name + ".");
    return true;
}
