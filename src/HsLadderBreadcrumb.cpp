/*
 * mod-hearthstone: the ladder breadcrumb quests 919970 / 919971, see HsLadderBreadcrumb.h.
 *
 * The conversation has two steps: the first gossip option ("I heard there is some kind of league?") opens a page where the
 * Hearthstone Master says what it has heard, the second ("Who should I speak to about joining?") opens the
 * quest of that master.
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
#include "HsIntroGlue.h"
#include "HsLadderBreadcrumb.h"
#include "HsLadderGlue.h"

#include <string>

namespace
{
    // Gossip action of the breadcrumb option (sender 0x4853). Block 0x48530500 is the Naxxramas one, 0x48530501 is free.
    constexpr uint32 kSender = 0x4853u; // the module's gossip sender ('HS', HsMatchGlue.cpp)
    constexpr uint32 kActLadderAsk = 0x48530501u;
    constexpr uint32 kActLadderMore = 0x48530502u; // the second step: the quest details
    constexpr uint32 kTextLadderTalk = 9195950;    // world migration 0054: what the master has heard about the league
    constexpr char const* kBreadcrumbOption = "I heard there is some kind of league?";
    constexpr char const* kMoreOption = "Who should I speak to about joining?";

    // The quest of this giver (0: not a giver).
    uint32 QuestOfGiver(uint32 entry)
    {
        for (std::size_t i = 0; i < 2; ++i)
            if (Hs::kLadderBreadcrumbGivers[i] == entry)
                return Hs::kLadderBreadcrumbQuests[i];
        return 0;
    }

    // The character has or had either of the two quests.
    bool HasAnyQuest(Player* player)
    {
        for (uint32 quest : Hs::kLadderBreadcrumbQuests)
            if (player->GetQuestStatus(quest) != QUEST_STATUS_NONE || player->GetQuestRewardStatus(quest))
                return true;
        return false;
    }

    // The account has made enough paid arena entries for the ladder to open. False on a database failure.
    bool ArenaEntriesMet(Player* player)
    {
        if (!player->GetSession())
            return false;
        uint32 entries = 0;
        if (!HsArenaCountPaidEntries(player->GetSession()->GetAccountId(), entries))
            return false;
        return entries >= Hs::kLadderArenaEntries;
    }
}

bool HsLadderBreadcrumbOffered(Player* player, Creature* creature)
{
    if (!player || !creature || !player->IsInWorld() || QuestOfGiver(creature->GetEntry()) == 0 || !HsLadderEnabled())
        return false;
    if (HasAnyQuest(player))
        return false;
    return HsLessonDone(player) && ArenaEntriesMet(player);
}

void HsLadderBreadcrumbAppendGossip(Player* player, Creature* creature, GossipMenu& menu, uint32_t sender)
{
    if (menu.MenuItemCount() >= GOSSIP_MAX_MENU_ITEMS || !HsLadderBreadcrumbOffered(player, creature))
        return;
    menu.AddMenuItem(GOSSIP_ICON_CHAT, kBreadcrumbOption, sender, kActLadderAsk, "", false);
    // The core reads the parallel action data vector unchecked: keep it aligned.
    menu.AddGossipMenuItemData(0, 0, 0);
}

bool HsLadderBreadcrumbHandleSelect(Player* player, Creature* creature, uint32_t action)
{
    if (action != kActLadderAsk && action != kActLadderMore)
        return false;
    PlayerMenu* talk = player ? player->PlayerTalkClass : nullptr;
    if (!talk)
        return true;
    if (!HsLadderBreadcrumbOffered(player, creature))
    {
        talk->CloseGossip();
        return true;
    }
    if (action == kActLadderAsk)
    {
        // The first step: the master tells what it has heard, and the player can ask who knows more.
        talk->ClearMenus();
        GossipMenu& menu = talk->GetGossipMenu();
        menu.AddMenuItem(GOSSIP_ICON_CHAT, kMoreOption, kSender, kActLadderMore, "", false);
        menu.AddGossipMenuItemData(0, 0, 0); // the core reads the parallel action data vector unchecked: keep it aligned
        talk->SendGossipMenu(kTextLadderTalk, creature->GetObjectGuid());
        return true;
    }
    Quest const* quest = sObjectMgr.GetQuestTemplate(QuestOfGiver(creature->GetEntry()));
    talk->CloseGossip();
    if (quest)
        talk->SendQuestGiverQuestDetails(quest, creature->GetObjectGuid(), true);
    return true;
}

bool HsLadderBreadcrumbHandleAccept(Player* player, WorldPacket const& packet)
{
    if (!player || packet.size() < sizeof(uint64) + sizeof(uint32))
        return false;
    WorldPacket copy(packet);
    copy.rpos(0);
    ObjectGuid giver;
    uint32 questId = 0;
    copy >> giver >> questId;
    if (questId != Hs::kLadderBreadcrumbQuests[0] && questId != Hs::kLadderBreadcrumbQuests[1])
        return false;
    // From here on the packet is ours: the core would refuse it (the quests have no start relation).
    if (!player->IsInWorld())
        return true;
    Creature* creature = giver.IsAnyTypeCreature() ? player->GetNPCIfCanInteractWith(giver, UNIT_NPC_FLAG_NONE) : nullptr;
    // Only the quest of the master that offers it (the accept packet names the giver).
    if (!HsLadderBreadcrumbOffered(player, creature) || QuestOfGiver(creature->GetEntry()) != questId)
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
        sLog.outString("Hearthstone: %s accepted the ladder breadcrumb quest %u", player->GetName(), questId);
    }
    if (player->PlayerTalkClass)
        player->PlayerTalkClass->CloseGossip();
    return true;
}

bool HsLadderChairUnlocked(Player* player)
{
    if (!player)
        return false;
    if (player->GetSession() && player->GetSession()->GetSecurity() >= SEC_GAMEMASTER)
        return true;
    // The chairs open with the second quest of the chain (the rules), not with the breadcrumb, and only once the account
    // has made its paid arena entries.
    if (!ArenaEntriesMet(player))
        return false;
    for (uint32 quest : Hs::kLadderRulesQuests)
        if (player->GetQuestRewardStatus(quest))
            return true;
    return false;
}
