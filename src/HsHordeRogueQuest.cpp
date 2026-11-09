#include "HsHordeRogueQuest.h"
#include "HsCardTrader.h"
#include "quests/HsHordeRogueRules.h"
#include "quests/HsHordeRogueQuestView.h"
#include "Creature.h"
#include "GossipDef.h"
#include "Item.h"
#include <map>
#include <mutex>
#include "Opcodes.h"
#include "Player.h"
#include "WorldPacket.h"
#include "WorldSession.h"

namespace
{
    std::mutex g_objectiveMutex;
    std::map<uint32, uint32> g_visibleStage; // Transport cache only, never quest progress.
    thread_local bool g_sendingQuestView = false;
    constexpr uint32 kContactFirst = 0x485300b0u;
    constexpr uint32 kActionsPerContact = 4; // Start, then three successive replies.
    // 0x485300c0-0x485300c8 belong to Byron's class hints (HsIntroGlue.cpp), which are dispatched first.
    constexpr uint32 kReportFirst = 0x485300d0u;
    constexpr uint32 kReportLast = kReportFirst + 5;
    constexpr uint32 kDirectionsFirst = kReportLast + 1;
    constexpr uint32 kDirectionsLast = kDirectionsFirst + 3;
    constexpr uint32 kReturnFirst = kDirectionsLast + 1;
    constexpr uint32 kReturnLast = kReturnFirst + 2;
    static_assert(kContactFirst + 4 * kActionsPerContact <= 0x485300c0u, "contact actions overlap the class hints");
    static_assert(kReturnLast < 0x485300e0u, "Horde Rogue actions overlap the card trader");
    char const* const kDirections[4] = {
        "Where can I find Gora?", "Where can I find Zikko?",
        "Where does Sable trade?", "What should I tell For'sen?",
    };

    struct Conversation
    {
        char const* opening;
        char const* replies[3];
    };
    Conversation const kContacts[4] = {
        { "For'sen sent me about a missing deck.",
          { "What's so special about the Deck? Do you know where he got it from?", "What did you get out of it?", "I'll find Gora." } },
        { "Rikka says you carried a deck through here.",
          { "What happened to the delivery?", "And you handed over someone else's deck?", "I'll speak to Zikko." } },
        { "I'm looking for the deck you won from Gora.",
          { "Where is it now?", "Was the wager rigged?", "Sable, then." } },
        { "Zikko says you have a deck with a dagger on the case.",
          { "You marked the cards?", "Do you know who took it?", "I'll tell him your price." } },
    };
    char const* const kReportReplies[5] = {
        "Sable says that deck was stolen from her.",
        "You told Rikka it belonged to your cousin. That's what you told me, too.",
        "So you stole it yourself.",
        "And you sent me to clean up after you.",
        "Then finish the lesson.",
    };

    std::array<uint32_t, 4> Counters(Player* player)
    {
        QuestStatusData const* status = player->GetQuestStatusData(kHsHordeRogueInvestigation);
        return status ? std::array<uint32_t, 4>{ status->m_creatureOrGOcount[0],
            status->m_creatureOrGOcount[1], status->m_creatureOrGOcount[2],
            status->m_creatureOrGOcount[3] } : std::array<uint32_t, 4>{};
    }

    uint32 VisibleStage(Player* player)
    {
        QuestStatus const status = player->GetQuestStatus(kHsHordeRogueInvestigation);
        return !player->GetQuestRewardStatus(kHsHordeRogueInvestigation) &&
            (status == QUEST_STATUS_INCOMPLETE || status == QUEST_STATUS_COMPLETE) ?
            HsHordeRogueStep(Counters(player)) : 0;
    }

    bool ContactOpen(Player* player, uint32 entry)
    {
        return player && HsHordeRogueContactAllowed(player->GetTeam() == HORDE,
            player->GetQuestStatus(kHsHordeRogueInvestigation) == QUEST_STATUS_INCOMPLETE,
            Counters(player), entry);
    }

    bool ReportOpen(Player* player)
    {
        if (!player)
            return false;
        return HsHordeRogueReportAllowed(player->GetTeam() == HORDE,
            player->GetQuestStatus(kHsHordeRogueRecovery) == QUEST_STATUS_INCOMPLETE,
            player->GetItemCount(kHsHordeRogueEvidence, false) != 0,
            player->GetItemCount(kHsHordeRogueConfrontation, true) != 0);
    }

    bool ReturnOpen(Player* player)
    {
        QuestStatus const status = player ? player->GetQuestStatus(kHsHordeRogueInvestigation) : QUEST_STATUS_NONE;
        return player && HsHordeRogueReturnAllowed(player->GetTeam() == HORDE,
            (status == QUEST_STATUS_INCOMPLETE || status == QUEST_STATUS_COMPLETE) &&
                !player->GetQuestRewardStatus(kHsHordeRogueInvestigation),
            Counters(player), player->GetItemCount(kHsHordeRogueInvestigationReport, true) != 0);
    }

    // Grants a native objective item at the end of a talk with For'sen, then shows the quest menu.
    void FinishWithItem(Player* player, Creature* creature, uint32 item, uint32 text)
    {
        ItemPosCountVec destination;
        InventoryResult const result = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, destination, item, 1);
        if (result != EQUIP_ERR_OK)
        {
            player->SendEquipError(result, nullptr, nullptr, item);
            return; // Keep the current final reply available to retry when a bag slot is free.
        }
        if (!player->StoreNewItem(destination, item, true))
            return;
        HsHordeRogueRefreshObjectives(player);
        player->PlayerTalkClass->ClearMenus();
        player->PrepareQuestMenu(creature->GetObjectGuid());
        player->PlayerTalkClass->SendGossipMenu(text, creature->GetObjectGuid());
    }

    uint32 ContactIndex(uint32 entry)
    {
        for (uint32 i = 0; i < kHsHordeRogueContacts.size(); ++i)
            if (kHsHordeRogueContacts[i] == entry)
                return i;
        return uint32(kHsHordeRogueContacts.size());
    }

    bool DirectionsOpen(Player* player, uint32 entry)
    {
        if (!player)
            return false;
        QuestStatus const status = player->GetQuestStatus(kHsHordeRogueInvestigation);
        return HsHordeRogueDirectionsAllowed(player->GetTeam() == HORDE,
            (status == QUEST_STATUS_INCOMPLETE || status == QUEST_STATUS_COMPLETE) &&
                !player->GetQuestRewardStatus(kHsHordeRogueInvestigation),
            Counters(player), ContactIndex(entry));
    }

    bool TradeOpen(Player* player)
    {
        return player && HsHordeRogueTradeAllowed(player->GetTeam() == HORDE,
            player->GetQuestRewardStatus(kHsHordeRogueInvestigation),
            player->GetQuestStatus(kHsHordeRogueRecovery) == QUEST_STATUS_INCOMPLETE);
    }

    void AddOption(GossipMenu& menu, char const* label, uint32 sender, uint32 action)
    {
        menu.AddMenuItem(GOSSIP_ICON_CHAT, label, sender, action, "", false);
        menu.AddGossipMenuItemData(0, 0, 0);
    }

    void Exchange(Player* player, Creature* creature, uint32 text, char const* reply,
        uint32 sender, uint32 action)
    {
        player->PlayerTalkClass->ClearMenus();
        AddOption(player->PlayerTalkClass->GetGossipMenu(), reply, sender, action);
        player->PlayerTalkClass->SendGossipMenu(text, creature->GetObjectGuid());
    }

    // No transient scene state: unfinished pages restart; only completed exchanges save quest credit.
    void Finish(Player* player, Creature* creature, uint32 credit, uint32 text)
    {
        player->KilledMonsterCredit(credit);
        HsHordeRogueRefreshObjectives(player);
        player->PlayerTalkClass->ClearMenus();
        player->PrepareQuestMenu(creature->GetObjectGuid());
        player->PlayerTalkClass->SendGossipMenu(text, creature->GetObjectGuid());
    }
}

bool HsHordeRogueGossipActive(Player* player, Creature* creature)
{
    return creature && (ContactOpen(player, creature->GetEntry()) ||
        DirectionsOpen(player, creature->GetEntry()) ||
        (creature->GetEntry() == kHsHordeRogue && (ReturnOpen(player) || ReportOpen(player))));
}

void HsHordeRogueAppendGossip(Player* player, Creature* creature, GossipMenu& menu, uint32_t sender)
{
    if (!HsHordeRogueGossipActive(player, creature) || menu.MenuItemCount() >= GOSSIP_MAX_MENU_ITEMS)
        return;
    if (creature->GetEntry() == kHsHordeRogue)
    {
        if (ReturnOpen(player))
            AddOption(menu, "I followed the deck's trail.", sender, kReturnFirst);
        else
            AddOption(menu, "I have the deck. We need to talk.", sender, kReportFirst);
    }
    else if (ContactOpen(player, creature->GetEntry()))
    {
        uint32 const step = HsHordeRogueStep(Counters(player));
        AddOption(menu, kContacts[step].opening, sender, kContactFirst + step * kActionsPerContact);
    }
    else
    {
        uint32 const contact = ContactIndex(creature->GetEntry());
        AddOption(menu, kDirections[contact], sender, kDirectionsFirst + contact);
    }
}

bool HsHordeRogueHandleHello(Player* player, Creature* creature, uint32_t sender)
{
    if (!player || !player->PlayerTalkClass || !creature || creature->GetEntry() != kHsHordeRogueSable)
        return false;
    player->PlayerTalkClass->ClearMenus();
    // Quest-deck sale while recovery is incomplete (character-local); the daily card is added below.
    if (TradeOpen(player))
    {
        // The core lets GMs bypass SQL gossip conditions. Supply exactly one native vendor option here;
        // sender/action/data still dispatch through the core's normal merchant handler and purchases.
        GossipMenu& menu = player->PlayerTalkClass->GetGossipMenu();
        menu.SetMenuId(creature->GetDefaultGossipMenuId());
        menu.AddMenuItem(GOSSIP_ICON_VENDOR, "I want to buy the deck.", 0, GOSSIP_OPTION_VENDOR, "", false);
        menu.AddGossipMenuItemData(0, 0, 0);
    }
    HsHordeRogueAppendGossip(player, creature, player->PlayerTalkClass->GetGossipMenu(), sender);
    HsCardTraderAppendGossip(player, creature, player->PlayerTalkClass->GetGossipMenu(), sender);
    player->PlayerTalkClass->SendGossipMenu(9195184, creature->GetObjectGuid());
    return true;
}

bool HsHordeRogueCanVendorPacket(Player* player, WorldPacket const& packet)
{
    uint32 const opcode = packet.GetOpcode();
    if (opcode != CMSG_LIST_INVENTORY && opcode != CMSG_BUY_ITEM &&
        opcode != CMSG_BUY_ITEM_IN_SLOT && opcode != CMSG_GOSSIP_SELECT_OPTION)
        return true;
    if (!player || !player->IsInWorld() || packet.size() < sizeof(uint64))
        return true;
    WorldPacket copy(packet);
    copy.rpos(0);
    ObjectGuid guid;
    copy >> guid;
    if (!guid.IsAnyTypeCreature() || guid.GetEntry() != kHsHordeRogueSable || TradeOpen(player))
        return true;
    if (opcode == CMSG_GOSSIP_SELECT_OPTION)
    {
        if (!player->PlayerTalkClass || copy.size() < sizeof(uint64) + sizeof(uint32))
            return true;
        uint32 index;
        copy >> index;
        GossipMenu& menu = player->PlayerTalkClass->GetGossipMenu();
        if (index >= menu.MenuItemCount() || menu.MenuItemSender(index) != 0 ||
            menu.MenuItemAction(index) != GOSSIP_OPTION_VENDOR)
            return true; // Investigation/directions remain available before trading unlocks.
    }
    if (player->PlayerTalkClass)
        player->PlayerTalkClass->CloseGossip();
    return false;
}

bool HsHordeRogueHandleSelect(Player* player, Creature* creature, uint32_t action, uint32_t sender)
{
    if (action < kContactFirst || action > kReturnLast ||
        (action >= kContactFirst + 4 * kActionsPerContact && action < kReportFirst))
        return false;
    if (!player || !player->PlayerTalkClass)
        return true;
    if (!HsHordeRogueGossipActive(player, creature) || !player->GetSession() || !player->IsAlive() ||
        !creature->IsAlive() || !player->IsWithinDistInMap(creature, INTERACTION_DISTANCE) ||
        !player->IsWithinLOSInMap(creature))
    {
        player->PlayerTalkClass->CloseGossip();
        return true;
    }
    // Shared packet dispatch derives the action from the currently displayed menu index and checks its NPC guid.
    // Also require the action here, so stale pages cannot be submitted directly to this handler.
    bool offered = false;
    for (uint32 i = 0; i < player->PlayerTalkClass->GetGossipMenu().MenuItemCount(); ++i)
        offered |= player->PlayerTalkClass->GossipOptionSender(i) == sender &&
            player->PlayerTalkClass->GossipOptionAction(i) == action;
    if (!offered)
    {
        player->PlayerTalkClass->CloseGossip();
        return true;
    }
    if (action >= kReturnFirst)
    {
        if (creature->GetEntry() != kHsHordeRogue || !ReturnOpen(player))
            player->PlayerTalkClass->CloseGossip();
        else if (action == kReturnLast)
            FinishWithItem(player, creature, kHsHordeRogueInvestigationReport, 9195214);
        else
            Exchange(player, creature, 9195212 + action - kReturnFirst,
                action == kReturnFirst ? "Sable has it. She'll sell it for three silver." :
                    "That's everyone. You know where your deck is now.", sender, action + 1);
        return true;
    }
    if (action >= kDirectionsFirst)
    {
        uint32 const contact = action - kDirectionsFirst;
        if (creature->GetEntry() != kHsHordeRogueContacts[contact] ||
            !DirectionsOpen(player, creature->GetEntry()))
            player->PlayerTalkClass->CloseGossip();
        else
        {
            player->PlayerTalkClass->ClearMenus();
            player->PrepareQuestMenu(creature->GetObjectGuid());
            player->PlayerTalkClass->SendGossipMenu(9195208 + contact, creature->GetObjectGuid());
        }
        return true;
    }
    if (action < kReportFirst)
    {
        uint32 const contact = (action - kContactFirst) / kActionsPerContact;
        uint32 const page = (action - kContactFirst) % kActionsPerContact;
        if (!ContactOpen(player, creature->GetEntry()) ||
            kHsHordeRogueContacts[contact] != creature->GetEntry())
            player->PlayerTalkClass->CloseGossip();
        else if (page == 3)
            Finish(player, creature, kHsHordeRogueCredits[contact], 9195208 + contact);
        else
            Exchange(player, creature, 9195190 + contact * 3 + page,
                kContacts[contact].replies[page], sender, action + 1);
        return true;
    }
    if (creature->GetEntry() != kHsHordeRogue || !ReportOpen(player))
        player->PlayerTalkClass->CloseGossip();
    else if (action == kReportLast)
        FinishWithItem(player, creature, kHsHordeRogueConfrontation, 9195207);
    else
    {
        uint32 const page = action - kReportFirst;
        Exchange(player, creature, 9195202 + page, kReportReplies[page], sender, action + 1);
    }
    return true;
}

void HsHordeRogueAfterQuestChange(Player* player)
{
    if (player && player->GetQuestStatus(kHsHordeRogueInvestigation) == QUEST_STATUS_NONE)
    {
        uint32 const count = player->GetItemCount(kHsHordeRogueInvestigationReport, true);
        if (count) player->DestroyItemCount(kHsHordeRogueInvestigationReport, count, true, false, true);
    }
    if (player && player->GetQuestStatus(kHsHordeRogueRecovery) == QUEST_STATUS_NONE)
    {
        uint32 const count = player->GetItemCount(kHsHordeRogueConfrontation, true);
        if (count) player->DestroyItemCount(kHsHordeRogueConfrontation, count, true, false, true);
    }
}

void HsHordeRogueRefreshObjectives(Player* player, bool force)
{
    if (!player || !player->GetSession()) return;
    uint32 const stage = VisibleStage(player);
    {
        std::lock_guard<std::mutex> lock(g_objectiveMutex);
        auto const last = g_visibleStage.find(player->GetGUIDLow());
        if (!force && last != g_visibleStage.end() && last->second == stage) return;
        g_visibleStage[player->GetGUIDLow()] = stage;
    }
    WorldPacket query(CMSG_QUEST_QUERY, 4);
    query << uint32(kHsHordeRogueInvestigation);
    // Reuse the core encoder; the outgoing hook masks this session's native response.
    player->GetSession()->HandleQuestQueryOpcode(query);
}

void HsHordeRogueForgetObjectives(uint32_t guid)
{
    std::lock_guard<std::mutex> lock(g_objectiveMutex);
    g_visibleStage.erase(guid);
}

bool HsHordeRogueCanSendQuest(Player* player, WorldPacket const& packet)
{
    if (!player || !player->GetSession() || packet.size() < 4) return true;
    uint32 const opcode = packet.GetOpcode();
    uint32 const first = packet.read<uint32>(0);
    // The vendor purchase already makes the client draw the deck's objective line itself, so the core's
    // item push is a second copy of the same line. Only the display packet is dropped: the saved item
    // count, CanCompleteQuest and the turn-in requirement stay untouched, and item lines are rebuilt
    // from the bags whenever the quest log is redrawn.
    if (opcode == SMSG_QUESTUPDATE_ADD_ITEM && first == kHsHordeRogueEvidence &&
        player->GetQuestStatus(kHsHordeRogueRecovery) == QUEST_STATUS_INCOMPLETE) return false;
    if (g_sendingQuestView || opcode != SMSG_QUEST_QUERY_RESPONSE || first != kHsHordeRogueInvestigation)
        return true;
    std::vector<uint8_t> native(packet.contents(), packet.contents() + packet.size()), view;
    if (!HsHordeRogueQuestView(native, VisibleStage(player), view)) return true;
    WorldPacket response(SMSG_QUEST_QUERY_RESPONSE, view.size());
    response.append(view.data(), view.size());
    // Recursion is thread-local: other sessions/map threads still receive their own view.
    g_sendingQuestView = true;
    player->GetSession()->SendPacket(&response);
    g_sendingQuestView = false;
    return false;
}
