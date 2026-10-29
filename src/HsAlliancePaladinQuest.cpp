#include "HsAlliancePaladinQuest.h"
#include "quests/HsAlliancePaladinRules.h"
#include "Creature.h"
#include "GossipDef.h"
#include "Player.h"
#include "WorldSession.h"

namespace
{
    bool OurEntry(uint32 entry)
    {
        return entry == kHsSirRowan || entry == kHsSquireTess || entry == kHsAdaSparkplugz;
    }

    bool Eligible(Player const* player, uint32 entry)
    {
        return player && OurEntry(entry) && player->GetTeam() == ALLIANCE && player->GetLevel() >= 10 &&
            player->GetQuestRewardStatus(919714);
    }

    bool Open(Player const* player, uint32 quest)
    {
        uint32 const previous = quest == kHsAlliancePaladinReturn ? 919714 : quest - 1;
        auto const state = player ? player->GetQuestStatus(quest) : QUEST_STATUS_NONE;
        return player && HsAlliancePaladinLessonAllowed(player->GetTeam() == ALLIANCE, player->GetLevel(),
            player->GetQuestRewardStatus(919714), state == QUEST_STATUS_INCOMPLETE || state == QUEST_STATUS_COMPLETE,
            player->GetQuestRewardStatus(quest), player->GetQuestRewardStatus(previous));
    }

    uint32 Lesson(Player const* player, uint32 entry)
    {
        if (!Eligible(player, entry)) return 0;
        for (uint32 quest : {kHsAlliancePaladinReturn, kHsAlliancePaladinTable, kHsAlliancePaladinDuel})
            if (Open(player, quest)) return quest;
        return 0;
    }

    uint32 Step(Player const* player)
    {
        auto const* data = player->GetQuestStatusData(kHsAlliancePaladinTable);
        if (!data) return 2;
        return HsAlliancePaladinNextStep({data->m_creatureOrGOcount[0], data->m_creatureOrGOcount[1],
            0}, 2);
    }

    uint32 Exchange(Player const* player, uint32 entry, uint32 quest)
    {
        if (quest != kHsAlliancePaladinTable || player->GetQuestStatus(quest) != QUEST_STATUS_INCOMPLETE) return 2;
        uint32 const index = Step(player);
        return HsAlliancePaladinExchangeCreature(index, entry) ? index : 2;
    }

    void Option(GossipMenu& menu, char const* label, uint32 sender, uint32 action)
    {
        if (menu.MenuItemCount() >= GOSSIP_MAX_MENU_ITEMS) return;
        menu.AddMenuItem(GOSSIP_ICON_CHAT, label, sender, action, "", false);
        menu.AddGossipMenuItemData(0, 0, 0);
    }

    struct Conversation
    {
        uint32 firstText;
        char const* replies[3];
    };

    Conversation const kConversations[] = {
        {9195376, {"He saved you a chair. Will you join the lesson?",
            "We're both learning. There's room for mistakes.", "Come and learn with us."}},
        {9195380, {"She agreed. She was worried about making mistakes.", nullptr, nullptr}},
    };
}

bool HsAlliancePaladinGossipActive(Player* player, Creature* creature)
{
    if (!creature) return false;
    uint32 const entry = creature->GetEntry();
    if (!Eligible(player, entry)) return false;
    if (entry == kHsAdaSparkplugz)
        return Open(player, kHsAlliancePaladinReturn) || player->GetQuestRewardStatus(kHsAlliancePaladinReturn);
    if (Eligible(player, entry) && player->GetQuestRewardStatus(kHsAlliancePaladinDuel)) return true;
    uint32 const quest = Lesson(player, entry);
    return entry == kHsSirRowan ? quest || player->GetQuestRewardStatus(kHsAlliancePaladinReturn) :
        quest == kHsAlliancePaladinTable;
}

void HsAlliancePaladinAppendGossip(Player* player, Creature* creature, GossipMenu& menu, uint32_t sender)
{
    if (!HsAlliancePaladinGossipActive(player, creature)) return;
    uint32 const entry = creature->GetEntry();
    if (entry == kHsAdaSparkplugz)
    {
        menu.SetMenuId(player->GetQuestRewardStatus(kHsAlliancePaladinReturn) ? 65079 : 65078);
        return;
    }
    if (player->GetQuestRewardStatus(kHsAlliancePaladinDuel))
    {
        menu.SetMenuId(entry == kHsSirRowan ? 65075 : 65076);
        return;
    }
    uint32 const quest = Lesson(player, entry);
    uint32 greeting = Step(player) > 0 ? 65077 : 65066;
    if (entry == kHsSirRowan)
    {
        uint32 const step = Step(player);
        greeting = quest == kHsAlliancePaladinReturn ? 65067 : quest == kHsAlliancePaladinDuel ? 65074 :
            quest == kHsAlliancePaladinTable ? (step == 0 ? 65071 : step == 1 ? 65072 : 65073) :
            player->GetQuestRewardStatus(kHsAlliancePaladinTable) ? 65073 : 65070;
    }
    menu.SetMenuId(greeting);
    uint32 const index = Exchange(player, entry, quest);
    if (index < kHsAlliancePaladinCredits.size())
        // The greeting already contains the opening NPC line.
        Option(menu, kConversations[index].replies[0], sender, kHsAlliancePaladinActionFirst + index * 4 + 1);
}

bool HsAlliancePaladinHandleSelect(Player* player, Creature* creature, uint32_t action, uint32_t sender)
{
    if (action < kHsAlliancePaladinRetiredActionFirst || action > kHsAlliancePaladinActionLast) return false;
    if (!player || !player->PlayerTalkClass) return true;
    uint32 const entry = creature ? creature->GetEntry() : 0;
    uint32 const quest = Lesson(player, entry);
    uint32 const index = Exchange(player, entry, quest);
    auto& menu = player->PlayerTalkClass->GetGossipMenu();
    bool offered = false;
    for (uint32 i = 0; i < menu.MenuItemCount(); ++i)
        offered |= menu.MenuItemSender(i) == sender && menu.MenuItemAction(i) == action;
    if (!creature || !HsAlliancePaladinGossipActive(player, creature) || !player->GetSession() ||
        !player->IsInWorld() || !creature->IsInWorld() || !player->IsAlive() || !creature->IsAlive() ||
        player->GetSession()->GetCurrentGossipGUID() != creature->GetObjectGuid() ||
        !player->IsWithinDistInMap(creature, INTERACTION_DISTANCE) || !player->IsWithinLOSInMap(creature) ||
        !HsAlliancePaladinActionAllowed(index, action, offered))
    {
        player->PlayerTalkClass->ClearMenus();
        player->PlayerTalkClass->CloseGossip();
        return true;
    }

    uint32 const page = (action - kHsAlliancePaladinActionFirst) % 4;
    auto const& conversation = kConversations[index];
    bool const last = page + 1 == HsAlliancePaladinPageCount(index);
    player->PlayerTalkClass->ClearMenus();
    if (!last)
        Option(player->PlayerTalkClass->GetGossipMenu(), conversation.replies[page], sender, action + 1);
    else
    {
        player->KilledMonsterCredit(kHsAlliancePaladinCredits[index]);
        player->PrepareQuestMenu(creature->GetObjectGuid());
    }
    player->PlayerTalkClass->SendGossipMenu(conversation.firstText + page, creature->GetObjectGuid());
    return true;
}

void HsAlliancePaladinAfterQuestChange(Player* player)
{
    if (!player) return;
    if (player->GetQuestStatus(kHsAlliancePaladinReturn) == QUEST_STATUS_NONE &&
        !player->GetQuestRewardStatus(kHsAlliancePaladinReturn))
        // Native abandonment removes bag source items; also remove any banked case.
        player->DestroyItemCount(kHsAdaCardCase, player->GetItemCount(kHsAdaCardCase, true), true, false, true);
    if (!player->PlayerTalkClass) return;
    auto& menu = player->PlayerTalkClass->GetGossipMenu();
    for (uint32 i = 0; i < menu.MenuItemCount(); ++i)
    {
        uint32 const action = menu.MenuItemAction(i);
        if (action < kHsAlliancePaladinRetiredActionFirst || action > kHsAlliancePaladinActionLast) continue;
        uint32 const quest = action < kHsAlliancePaladinActionFirst ?
            kHsAlliancePaladinReturn : kHsAlliancePaladinTable;
        if (player->GetQuestStatus(quest) != QUEST_STATUS_NONE) continue;
        // CloseGossip only sends a packet; remove abandoned replies before reacceptance.
        player->PlayerTalkClass->ClearMenus();
        player->PlayerTalkClass->CloseGossip();
        return;
    }
}
