/*
 * mod-hearthstone: the Naxxramas breadcrumb quest 919932, see HsNaxxBreadcrumb.h.
 */

#include "Creature.h"
#include "GossipDef.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"
#include "SharedDefines.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include "HsArenaStore.h"
#include "HsNaxxBreadcrumb.h"
#include "quests/HsNaxxQuestData.h"

#include <mutex>
#include <string>
#include <unordered_set>

namespace
{
    // Gossip action of the breadcrumb option (sender 0x4853). Block 0x48530500 is free: the other blocks end at 0x4853041d.
    constexpr uint32 kSender = 0x4853u; // the module's gossip sender ('HS', HsMatchGlue.cpp)
    constexpr uint32 kActBreadcrumbAsk = 0x48530500u;
    constexpr uint32 kActBreadcrumbMore = 0x48530503u; // the second step: the quest details
    constexpr uint32 kTextNaxxTalk = 9195951;          // world migration 0054: what the master has heard from the Chapel
    constexpr char const* kBreadcrumbOption = "I overheard you talking about the Chapel of Light.";
    constexpr char const* kMoreOption = "What do they need me to do?";

    std::mutex g_goodRunMutex;
    std::unordered_set<uint32> g_goodRunAccounts; // positive answers only

    uint32 AccountOf(Player* player)
    {
        return player && player->GetSession() ? player->GetSession()->GetAccountId() : 0;
    }

    bool IsGiver(uint32 entry)
    {
        for (uint32 giver : Hs::kNaxxBreadcrumbGivers)
            if (giver == entry)
                return true;
        return false;
    }

    // The account has an ended arena run with the minimum number of wins. Cached when positive; never when negative.
    bool AccountHasGoodRun(uint32 accountId)
    {
        if (!accountId)
            return false;
        {
            std::lock_guard<std::mutex> lock(g_goodRunMutex);
            if (g_goodRunAccounts.count(accountId))
                return true;
        }
        uint32 count = 0;
        if (!HsArenaCountGoodRuns(accountId, Hs::kNaxxMinArenaWins, count) || count == 0)
            return false;
        std::lock_guard<std::mutex> lock(g_goodRunMutex);
        g_goodRunAccounts.insert(accountId);
        return true;
    }
}

bool HsNaxxBreadcrumbOffered(Player* player, Creature* creature)
{
    if (!player || !creature || !player->IsInWorld() || !IsGiver(creature->GetEntry()))
        return false;
    if (player->GetQuestStatus(Hs::kNaxxBreadcrumbQuest) != QUEST_STATUS_NONE ||
        player->GetQuestRewardStatus(Hs::kNaxxBreadcrumbQuest))
        return false;
    return AccountHasGoodRun(AccountOf(player));
}

void HsNaxxBreadcrumbAppendGossip(Player* player, Creature* creature, GossipMenu& menu, uint32_t sender)
{
    if (menu.MenuItemCount() >= GOSSIP_MAX_MENU_ITEMS || !HsNaxxBreadcrumbOffered(player, creature))
        return;
    menu.AddMenuItem(GOSSIP_ICON_CHAT, kBreadcrumbOption, sender, kActBreadcrumbAsk, "", false);
    // The core reads the parallel action data vector unchecked: keep it aligned.
    menu.AddGossipMenuItemData(0, 0, 0);
}

bool HsNaxxBreadcrumbHandleSelect(Player* player, Creature* creature, uint32_t action)
{
    if (action != kActBreadcrumbAsk && action != kActBreadcrumbMore)
        return false;
    PlayerMenu* talk = player ? player->PlayerTalkClass : nullptr;
    if (!talk)
        return true;
    if (!HsNaxxBreadcrumbOffered(player, creature))
    {
        talk->CloseGossip();
        return true;
    }
    if (action == kActBreadcrumbAsk)
    {
        // The first step: the master tells what it has heard, and the player can ask who to speak to.
        talk->ClearMenus();
        GossipMenu& menu = talk->GetGossipMenu();
        menu.AddMenuItem(GOSSIP_ICON_CHAT, kMoreOption, kSender, kActBreadcrumbMore, "", false);
        menu.AddGossipMenuItemData(0, 0, 0); // the core reads the parallel action data vector unchecked: keep it aligned
        talk->SendGossipMenu(kTextNaxxTalk, creature->GetObjectGuid());
        return true;
    }
    Quest const* quest = sObjectMgr.GetQuestTemplate(Hs::kNaxxBreadcrumbQuest);
    talk->CloseGossip();
    if (quest)
        talk->SendQuestGiverQuestDetails(quest, creature->GetObjectGuid(), true);
    return true;
}

bool HsNaxxBreadcrumbHandleAccept(Player* player, WorldPacket const& packet)
{
    if (!player || packet.size() < sizeof(uint64) + sizeof(uint32))
        return false;
    WorldPacket copy(packet);
    copy.rpos(0);
    ObjectGuid giver;
    uint32 questId = 0;
    copy >> giver >> questId;
    if (questId != Hs::kNaxxBreadcrumbQuest)
        return false;
    // From here on the packet is ours: the core would refuse it (the quest has no start relation).
    if (!player->IsInWorld())
        return true;
    Creature* creature = giver.IsAnyTypeCreature() ? player->GetNPCIfCanInteractWith(giver, UNIT_NPC_FLAG_NONE) : nullptr;
    if (!HsNaxxBreadcrumbOffered(player, creature))
    {
        if (player->PlayerTalkClass)
            player->PlayerTalkClass->CloseGossip();
        return true;
    }
    Quest const* quest = sObjectMgr.GetQuestTemplate(questId);
    if (quest && player->CanTakeQuest(quest, true) && player->CanAddQuest(quest, true))
    {
        player->AddQuest(quest, creature);
        // The core's accept handler does this right after AddQuest; this quest has no objectives, so without it the
        // quest stays incomplete and cannot be turned in.
        if (player->CanCompleteQuest(questId))
            player->CompleteQuest(questId);
        sLog.outString("Hearthstone: %s (account %u) accepted the Naxxramas breadcrumb quest %u", player->GetName(),
            AccountOf(player), questId);
    }
    if (player->PlayerTalkClass)
        player->PlayerTalkClass->CloseGossip();
    return true;
}
