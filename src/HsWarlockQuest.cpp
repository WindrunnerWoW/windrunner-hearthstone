#include "HsWarlockQuest.h"
#include "quests/HsWarlockRules.h"
#include "Creature.h"
#include "GossipDef.h"
#include "Item.h"
#include "Player.h"
#include "WorldSession.h"

namespace
{
    bool HordeEntry(uint32 entry)
    {
        return entry == kHsVarka || entry == kHsSkrit;
    }

    bool OurEntry(uint32 entry)
    {
        return HordeEntry(entry) || entry == kHsMaren || entry == kHsNib || entry == kHsOwen;
    }

    bool Eligible(Player const* player, uint32 entry)
    {
        return player && OurEntry(entry) && player->GetLevel() >= 10 &&
            (player->GetTeam() == HORDE) == HordeEntry(entry) &&
            player->GetQuestRewardStatus(HordeEntry(entry) ? 919704 : 919714);
    }

    bool Open(Player const* player, uint32 quest)
    {
        uint32 const intro = quest < kHsWarlockWitness ? 919704 : 919714;
        uint32 const previous = quest == kHsWarlockScars || quest == kHsWarlockWitness ? intro : quest - 1;
        auto const state = player ? player->GetQuestStatus(quest) : QUEST_STATUS_NONE;
        return player && HsWarlockLessonAllowed(
            (player->GetTeam() == HORDE) == (quest < kHsWarlockWitness), player->GetLevel(),
            player->GetQuestRewardStatus(intro), state == QUEST_STATUS_INCOMPLETE || state == QUEST_STATUS_COMPLETE,
            player->GetQuestRewardStatus(quest), player->GetQuestRewardStatus(previous));
    }

    uint32 Lesson(Player const* player, uint32 entry)
    {
        if (!Eligible(player, entry)) return 0;
        for (uint32 quest : HordeEntry(entry) ? std::array<uint32, 3>{kHsWarlockScars, kHsWarlockCard, kHsVarkaDuel} :
            std::array<uint32, 3>{kHsWarlockWitness, kHsWarlockFee, kHsMarenDuel})
            if (Open(player, quest)) return quest;
        return 0;
    }

    uint32 Step(Player const* player, uint32 quest)
    {
        auto const* data = player->GetQuestStatusData(quest);
        if (!data) return 3;
        return HsWarlockNextStep({data->m_creatureOrGOcount[0], data->m_creatureOrGOcount[1],
            data->m_creatureOrGOcount[2]}, quest == kHsWarlockScars ? 3 : quest == kHsWarlockWitness ? 2 : 1);
    }

    uint32 Exchange(Player const* player, uint32 entry, uint32 quest)
    {
        if (!quest || player->GetQuestStatus(quest) != QUEST_STATUS_INCOMPLETE) return 7;
        uint32 const step = Step(player, quest);
        uint32 const index = quest == kHsWarlockScars ? step : quest == kHsWarlockCard ?
            (HsWarlockCardAllowed(true, player->GetItemCount(kHsWarlockPracticeCard, true) != 0) ? 3 : 7) :
            quest == kHsWarlockWitness ? (step < 2 ? 4 + step : 7) :
            quest == kHsWarlockFee && step == 0 ? 6 : 7;
        return HsWarlockExchangeCreature(index, entry) ? index : 7;
    }

    void Option(GossipMenu& menu, char const* label, uint32 sender, uint32 action)
    {
        if (menu.MenuItemCount() >= GOSSIP_MAX_MENU_ITEMS) return;
        menu.AddMenuItem(GOSSIP_ICON_CHAT, label, sender, action, "", false);
        menu.AddGossipMenuItemData(0, 0, 0);
    }

    struct Conversation
    {
        char const* opening;
        uint32 firstText;
        char const* replies[3];
    };
    Conversation const kConversations[] = {
        {"Show me the scorched card.", 9195344, {"The fire reaches your side too.", nullptr, nullptr}},
        {"Show me the stained card.", 9195346, {"If I could afford the blood.", nullptr, nullptr}},
        {"Show me the torn card.", 9195348, {"What else I might lose.", nullptr, nullptr}},
        {"Varka sent me for a marked practice card.", 9195350,
            {"No future favors.", "What about some Burnt Bone Ashes instead?",
                "Here are the ashes. That is the whole payment."}},
        {"Maren needs you to witness her agreement with Nib.", 9195354,
            {"Maren needs you to witness her agreement with Nib.",
                "You're only confirming the original terms. Nothing else.", "I'll make sure she knows."}},
        {"He signed. He also mentioned something about his hat.", 9195358,
            {"He signed. He also mentioned something about his hat.", nullptr, nullptr}},
        {"You already agreed to one flask.", 9195360,
            {"You already agreed to one flask.", "Then I'll tell Maren you're refusing to help with the lesson.",
                "One flask. No additional payment. Agreed."}},
    };

    bool GiveCard(Player* player)
    {
        if (!HsWarlockTradeAllowed(Open(player, kHsWarlockCard),
            player->GetItemCount(kHsWarlockPracticeCard, true) != 0,
            player->GetItemCount(kHsWarlockAshes, false) != 0)) return false;
        ItemPosCountVec destination;
        InventoryResult const result = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, destination,
            kHsWarlockPracticeCard, 1);
        if (result != EQUIP_ERR_OK)
        {
            player->SendEquipError(result, nullptr, nullptr, kHsWarlockPracticeCard);
            return false;
        }
        Item* item = player->StoreNewItem(destination, kHsWarlockPracticeCard, true);
        if (!item) return false;
        // Store successfully before taking payment; a full-bag failure leaves the ashes intact.
        player->DestroyItemCount(kHsWarlockAshes, 1, true);
        player->SendNewItem(item, 1, true, false);
        return true;
    }
}

bool HsWarlockGossipActive(Player* player, Creature* creature)
{
    if (!creature) return false;
    uint32 const entry = creature->GetEntry();
    if (Eligible(player, entry) && !HordeEntry(entry) && player->GetQuestRewardStatus(kHsMarenDuel))
        return true;
    uint32 const quest = Lesson(player, entry);
    return quest && (HordeEntry(entry) ? entry == kHsVarka || quest == kHsWarlockCard :
        entry == kHsMaren || (entry == kHsOwen && quest == kHsWarlockWitness) ||
        (entry == kHsNib && quest == kHsWarlockFee));
}

void HsWarlockAppendGossip(Player* player, Creature* creature, GossipMenu& menu, uint32_t sender)
{
    if (!HsWarlockGossipActive(player, creature)) return;
    uint32 const entry = creature->GetEntry();
    if (!HordeEntry(entry) && player->GetQuestRewardStatus(kHsMarenDuel))
    {
        menu.SetMenuId(entry == kHsMaren ? 65060 : entry == kHsNib ? 65061 : 65062);
        return;
    }
    uint32 const quest = Lesson(player, entry);
    uint32 greeting = HordeEntry(entry) ? (entry == kHsVarka ? 65046 : 65047) :
        entry == kHsMaren ? 65048 : entry == kHsNib ? 65063 : 65050;
    if (entry == kHsVarka)
        greeting = quest == kHsVarkaDuel ? 65058 : quest == kHsWarlockCard ? 65052 :
            Step(player, quest) == 3 ? 65051 : 65046;
    else if (entry == kHsSkrit && player->GetItemCount(kHsWarlockPracticeCard, true)) greeting = 65053;
    else if (entry == kHsMaren)
        greeting = quest == kHsMarenDuel ? 65059 : quest == kHsWarlockFee ? 65057 :
            Step(player, quest) == 0 ? 65054 : Step(player, quest) == 1 ? 65055 : 65056;
    else if (entry == kHsOwen && Step(player, quest) > 0) greeting = 65064;
    else if (entry == kHsNib && Step(player, quest) > 0) greeting = 65049;
    menu.SetMenuId(greeting);
    uint32 const index = Exchange(player, entry, quest);
    if (index < 7)
    {
        // Alliance opening pages already contain the first NPC line; offer its supplied reply directly.
        uint32 const page = index >= 4 ? 1 : 0;
        Option(menu, page ? kConversations[index].replies[0] : kConversations[index].opening,
            sender, kHsWarlockActionFirst + index * 4 + page);
    }
}

bool HsWarlockHandleSelect(Player* player, Creature* creature, uint32_t action, uint32_t sender)
{
    if (action < kHsWarlockActionFirst || action > kHsWarlockActionLast) return false;
    if (!player || !player->PlayerTalkClass) return true;
    uint32 const entry = creature ? creature->GetEntry() : 0;
    uint32 const quest = Lesson(player, entry);
    uint32 const index = Exchange(player, entry, quest);
    auto& menu = player->PlayerTalkClass->GetGossipMenu();
    bool offered = false;
    for (uint32 i = 0; i < menu.MenuItemCount(); ++i)
        offered |= menu.MenuItemSender(i) == sender && menu.MenuItemAction(i) == action;
    if (!HsWarlockGossipActive(player, creature) || !player->GetSession() || !player->IsInWorld() ||
        !player->IsAlive() || !creature->IsAlive() ||
        player->GetSession()->GetCurrentGossipGUID() != creature->GetObjectGuid() ||
        !player->IsWithinDistInMap(creature, INTERACTION_DISTANCE) || !player->IsWithinLOSInMap(creature) ||
        !HsWarlockActionAllowed(index, action, offered))
    {
        player->PlayerTalkClass->CloseGossip();
        return true;
    }
    uint32 const page = (action - kHsWarlockActionFirst) % 4;
    auto const& conversation = kConversations[index];
    bool const last = page + 1 == HsWarlockPageCount(index);
    if (last && index == 3 && !GiveCard(player))
    {
        if (!player->GetItemCount(kHsWarlockAshes, false))
        {
            // A stale payment choice cannot produce a free card.
            player->PlayerTalkClass->ClearMenus();
            player->PlayerTalkClass->SendGossipMenu(9195352, creature->GetObjectGuid());
        }
        // Preserve the payment reply after a full-bag failure for a retry.
        return true;
    }
    player->PlayerTalkClass->ClearMenus();
    if (!last)
    {
        // Payment is offered only after the bargain and only with ashes in bags.
        if (index != 3 || page != 2 || player->GetItemCount(kHsWarlockAshes, false))
            Option(player->PlayerTalkClass->GetGossipMenu(), conversation.replies[page], sender, action + 1);
    }
    else
    {
        if (kHsWarlockCredits[index]) player->KilledMonsterCredit(kHsWarlockCredits[index]);
        if (index == 4)
        {
            // Owen's final supplied acknowledgement saves the signature and ends the conversation.
            player->PlayerTalkClass->CloseGossip();
            return true;
        }
        uint32 const next = Exchange(player, entry, quest);
        if (quest == kHsWarlockScars && index < 3 && next < 3)
            Option(player->PlayerTalkClass->GetGossipMenu(), kConversations[next].opening, sender,
                kHsWarlockActionFirst + next * 4);
        else
            player->PrepareQuestMenu(creature->GetObjectGuid());
    }
    player->PlayerTalkClass->SendGossipMenu(conversation.firstText + page, creature->GetObjectGuid());
    return true;
}

void HsWarlockAfterQuestChange(Player* player)
{
    if (player && player->GetQuestStatus(kHsWarlockCard) == QUEST_STATUS_NONE)
        for (uint32 const item : {kHsWarlockPracticeCard, kHsWarlockAshes})
            player->DestroyItemCount(item, player->GetItemCount(item, true), true, false, true);
}
