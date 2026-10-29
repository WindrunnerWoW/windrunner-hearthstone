/*
 * mod-hearthstone: the Warsong Battlegrounds unlock quests 920300 / 920301, see HsBgBreadcrumb.h.
 *
 * The conversation has two steps, like the ladder one: the first gossip option opens a page where the Hearthstone Master says
 * what it has heard, the second ("Who should I speak to about joining?") opens the quest of that master.
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

#include "HsBgBreadcrumb.h"
#include "HsBgGlue.h"
#include "HsIntroGlue.h"

namespace
{
    // Gossip actions of the breadcrumb (sender 0x4853). 0x48530500 / 0x48530503 are the Naxxramas ones, 0x48530501 / 0x48530502
    // the ladder ones.
    constexpr uint32 kSender = 0x4853u; // the module's gossip sender ('HS', HsMatchGlue.cpp)
    constexpr uint32 kActBgAsk = 0x48530504u;
    constexpr uint32 kActBgMore = 0x48530505u; // the second step: the quest details
    constexpr uint32 kTextBgTalk[2] = { 9195952, 9195953 }; // world migration 0060: what the master has heard, per giver
    constexpr char const* kBreadcrumbOption = "What is all this fuss about missing recruits and the Battlegrounds?";
    constexpr char const* kMoreOption = "Who should I speak to about joining?";

    // The index of this giver (-1: not a giver).
    int GiverIndex(uint32 entry)
    {
        for (int i = 0; i < 2; ++i)
            if (Hs::kBgBreadcrumbGivers[i] == entry)
                return i;
        return -1;
    }

    // The character has or had either of the two quests.
    bool HasAnyQuest(Player* player)
    {
        for (uint32 quest : Hs::kBgBreadcrumbQuests)
            if (player->GetQuestStatus(quest) != QUEST_STATUS_NONE || player->GetQuestRewardStatus(quest))
                return true;
        return false;
    }
}

bool HsBgBreadcrumbOffered(Player* player, Creature* creature)
{
    if (!player || !creature || !player->IsInWorld() || GiverIndex(creature->GetEntry()) < 0 || !HsBgEnabled())
        return false;
    if (HasAnyQuest(player))
        return false;
    return HsLessonDone(player);
}

void HsBgBreadcrumbAppendGossip(Player* player, Creature* creature, GossipMenu& menu, uint32_t sender)
{
    if (menu.MenuItemCount() >= GOSSIP_MAX_MENU_ITEMS || !HsBgBreadcrumbOffered(player, creature))
        return;
    menu.AddMenuItem(GOSSIP_ICON_CHAT, kBreadcrumbOption, sender, kActBgAsk, "", false);
    // The core reads the parallel action data vector unchecked: keep it aligned.
    menu.AddGossipMenuItemData(0, 0, 0);
}

bool HsBgBreadcrumbHandleSelect(Player* player, Creature* creature, uint32_t action)
{
    if (action != kActBgAsk && action != kActBgMore)
        return false;
    PlayerMenu* talk = player ? player->PlayerTalkClass : nullptr;
    if (!talk)
        return true;
    if (!HsBgBreadcrumbOffered(player, creature))
    {
        talk->CloseGossip();
        return true;
    }
    int const giver = GiverIndex(creature->GetEntry());
    if (action == kActBgAsk)
    {
        // The first step: the master tells what it has heard, and the player can ask who knows more.
        talk->ClearMenus();
        GossipMenu& menu = talk->GetGossipMenu();
        menu.AddMenuItem(GOSSIP_ICON_CHAT, kMoreOption, kSender, kActBgMore, "", false);
        menu.AddGossipMenuItemData(0, 0, 0); // the core reads the parallel action data vector unchecked: keep it aligned
        talk->SendGossipMenu(kTextBgTalk[giver], creature->GetObjectGuid());
        return true;
    }
    Quest const* quest = sObjectMgr.GetQuestTemplate(Hs::kBgBreadcrumbQuests[giver]);
    talk->CloseGossip();
    if (quest)
        talk->SendQuestGiverQuestDetails(quest, creature->GetObjectGuid(), true);
    return true;
}

bool HsBgBreadcrumbHandleAccept(Player* player, WorldPacket const& packet)
{
    if (!player || packet.size() < sizeof(uint64) + sizeof(uint32))
        return false;
    WorldPacket copy(packet);
    copy.rpos(0);
    ObjectGuid giver;
    uint32 questId = 0;
    copy >> giver >> questId;
    if (questId != Hs::kBgBreadcrumbQuests[0] && questId != Hs::kBgBreadcrumbQuests[1])
        return false;
    // From here on the packet is ours: the core would refuse it (the quests have no start relation).
    if (!player->IsInWorld())
        return true;
    Creature* creature = giver.IsAnyTypeCreature() ? player->GetNPCIfCanInteractWith(giver, UNIT_NPC_FLAG_NONE) : nullptr;
    // Only the quest of the master that offers it (the accept packet names the giver).
    int const index = creature ? GiverIndex(creature->GetEntry()) : -1;
    if (!HsBgBreadcrumbOffered(player, creature) || index < 0 || Hs::kBgBreadcrumbQuests[index] != questId)
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
        sLog.outString("Hearthstone: %s accepted the Battlegrounds breadcrumb quest %u", player->GetName(), questId);
    }
    if (player->PlayerTalkClass)
        player->PlayerTalkClass->CloseGossip();
    return true;
}
