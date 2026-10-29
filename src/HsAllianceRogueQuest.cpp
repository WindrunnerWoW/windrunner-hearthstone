#include "HsAllianceRogueQuest.h"
#include "HsCardTrader.h"
#include "quests/HsAllianceRogueRules.h"
#include "quests/HsHordeDruidRules.h"
#include "quests/HsHordeWarriorRules.h"
#include "Creature.h"
#include "GameObject.h"
#include "HsQuestPickup.h"
#include "GossipDef.h"
#include "Item.h"
#include "Map.h"
#include "MapManager.h"
#include "Player.h"
#include "Opcodes.h"
#include "WorldPacket.h"
#include "ScriptMgr.h"
#include "UpdateData.h"
#include "UpdateMask.h"
#include "WorldSession.h"
#include <cmath>

namespace
{
    constexpr uint32 kInvitation = 0x48530100u;
    constexpr uint32 kClueFirst = 0x48530101u; // Three pages per contact.
    constexpr uint32 kDirectionFirst = 0x4853010au;
    constexpr uint32 kGamble = 0x4853010du;
    constexpr uint32 kRoll = 0x4853010eu;
    constexpr uint32 kPersuade = 0x48530110u;
    constexpr uint32 kReason = 0x48530111u;
    constexpr uint32 kPromise = 0x48530112u;
    constexpr uint32 kPickupDirections = 0x48530114u;
    constexpr uint32 kDiceExplanation = 0x48530115u;
    constexpr uint32 kTakeDice = 0x48530116u;
    constexpr uint32 kDistract = 0x48530117u;
    constexpr uint32 kLast = kDistract;

    bool Open(Player const* player, uint32 quest)
    {
        return player && player->GetTeam() == ALLIANCE &&
            !player->GetQuestRewardStatus(quest) && player->GetQuestStatus(quest) == QUEST_STATUS_INCOMPLETE;
    }
    std::array<uint32_t, 3> Clues(Player const* player)
    {
        auto const* status = player->GetQuestStatusData(kHsAllianceRogueInvestigation);
        return status ? std::array<uint32_t, 3>{ status->m_creatureOrGOcount[0],
            status->m_creatureOrGOcount[1], status->m_creatureOrGOcount[2] } : std::array<uint32_t, 3>{};
    }
    bool InvitationOpen(Player* player)
    {
        return player && HsAllianceRogueInvitationAllowed(player->GetTeam() == ALLIANCE,
            player->GetLevel(), player->GetQuestRewardStatus(919714),
            player->GetQuestStatus(kHsAllianceRogueInvestigation) == QUEST_STATUS_NONE,
            player->GetQuestRewardStatus(kHsAllianceRogueInvestigation),
            player->GetItemCount(kHsAllianceRogueInvitation, true) != 0);
    }
    bool CardOpen(Player const* player, uint32 index)
    {
        return player && index < kHsAllianceRogueCardItems.size() && HsAllianceRogueCardAllowed(player->GetTeam() == ALLIANCE,
            Open(player, kHsAllianceRogueCards),
            player->GetItemCount(kHsAllianceRogueCardItems[index], true) != 0);
    }
    bool DiceOpen(Player const* player)
    {
        return player && HsAllianceRogueDiceAllowed(player->GetTeam() == ALLIANCE,
            Open(player, kHsAllianceRogueCards), player->GetItemCount(kHsAllianceRogueDice, true) != 0);
    }
    bool DirectionsOpen(Player* player, uint32 contact)
    {
        auto const status = player->GetQuestStatus(kHsAllianceRogueInvestigation);
        return player->GetTeam() == ALLIANCE && !player->GetQuestRewardStatus(kHsAllianceRogueInvestigation) &&
            (status == QUEST_STATUS_INCOMPLETE || status == QUEST_STATUS_COMPLETE) &&
            contact < 2 && HsAllianceRogueClueStep(Clues(player)) == contact + 1;
    }
    void Option(GossipMenu& menu, char const* label, uint32 sender, uint32 action)
    {
        if (menu.MenuItemCount() >= GOSSIP_MAX_MENU_ITEMS) return;
        menu.AddMenuItem(GOSSIP_ICON_CHAT, label, sender, action, "", false);
        menu.AddGossipMenuItemData(0, 0, 0);
    }
    void Page(Player* player, Creature* creature, uint32 text, char const* reply, uint32 sender, uint32 action)
    {
        player->PlayerTalkClass->ClearMenus();
        Option(player->PlayerTalkClass->GetGossipMenu(), reply, sender, action);
        player->PlayerTalkClass->SendGossipMenu(text, creature->GetObjectGuid());
    }
    void Ending(Player* player, Creature* creature, uint32 text)
    {
        player->PlayerTalkClass->ClearMenus();
        player->PrepareQuestMenu(creature->GetObjectGuid());
        player->PlayerTalkClass->SendGossipMenu(text, creature->GetObjectGuid());
    }
    bool Give(Player* player, uint32 entry)
    {
        if (player->GetItemCount(entry, true)) return true;
        ItemPosCountVec destination;
        InventoryResult const result = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, destination, entry, 1);
        if (result != EQUIP_ERR_OK)
        {
            player->SendEquipError(result, nullptr, nullptr, entry);
            return false;
        }
        Item* item = player->StoreNewItem(destination, entry, true);
        if (!item) return false;
        player->SendNewItem(item, 1, true, false);
        return true;
    }

    // Native quest items are the only saved state. The shared object
    // stays available to other eligible characters when one player collects it.
    class HiddenCard final : public GameObject
    {
    public:
        bool IsVisibleForInState(WorldObject const* detector, WorldObject const* viewpoint, bool visible) const override
        {
            return detector && HsQuestPickupAvailable(this) && CardOpen(detector->ToPlayer(), 2) &&
                GameObject::IsVisibleForInState(detector, viewpoint, visible);
        }
    };
    class HiddenCardAI final : public HsQuestPickupAI
    {
    public:
        explicit HiddenCardAI(GameObject* go) : HsQuestPickupAI(go, kHsAllianceRogueCardItems[2]) {}
    protected:
        bool IsEligible(Player const* player) const override { return CardOpen(player, 2); }
    };
    class PickupScript final : public AllGameObjectScript
    {
    public:
        PickupScript() : AllGameObjectScript("hearthstone_alliance_rogue_pickup") {}
        GameObjectAI* GetGameObjectAI(GameObject* go) const override
        {
            return go->GetEntry() == kHsAllianceRoguePickup ? new HiddenCardAI(go) : nullptr;
        }
    };
    ObjectGuid g_pickup;
    uint32 g_sweep = 0;
}

bool HsAllianceRogueGossipActive(Player* player, Creature* creature)
{
    if (!player || !creature || player->GetTeam() != ALLIANCE) return false;
    uint32 const entry = creature->GetEntry();
    if (entry == kHsAllianceRogueOperative) return InvitationOpen(player);
    if (HsAllianceRogueContactAllowed(true, Open(player, kHsAllianceRogueInvestigation), Clues(player), entry)) return true;
    for (uint32 i = 0; i < 2; ++i)
        if (entry == kHsAllianceRogueContacts[i] && (DirectionsOpen(player, i) || CardOpen(player, i))) return true;
    return entry == kHsAllianceRogue && (DiceOpen(player) || CardOpen(player, 2) || player->GetQuestRewardStatus(kHsAllianceRogueCards));
}

void HsAllianceRogueAppendGossip(Player* player, Creature* creature, GossipMenu& menu, uint32_t sender)
{
    if (!HsAllianceRogueGossipActive(player, creature)) return;
    uint32 const entry = creature->GetEntry();
    if (entry == kHsAllianceRogueOperative)
        Option(menu, "How can I unlock Rogue in Hearthstone?", sender, kInvitation);
    else if (HsAllianceRogueContactAllowed(true, Open(player, kHsAllianceRogueInvestigation), Clues(player), entry))
    {
        uint32 const step = HsAllianceRogueClueStep(Clues(player));
        static char const* const openings[] = { "Does this marked card mean anything to you?",
            "Aedis said you know who uses a silver dagger mark.", "I'm looking for a Rogue Hearthstone teacher." };
        Option(menu, openings[step], sender, kClueFirst + step * 3);
    }
    else if (entry == kHsAllianceRogueAedis && CardOpen(player, 0))
        Option(menu, "I heard you like gambling. Want to play for a card?", sender, kGamble);
    else if (entry == kHsAllianceRogueOsric && CardOpen(player, 1))
        Option(menu, "Could you spare a Hearthstone card?", sender, kPersuade);
    else if (entry == kHsAllianceRogue && DiceOpen(player))
        Option(menu, "You mentioned rigged dice. I'll need a set.", sender, kDiceExplanation);
    else if (entry == kHsAllianceRogue && CardOpen(player, 2))
        Option(menu, "Where is the last card?", sender, kPickupDirections);
    else
        for (uint32 i = 0; i < 2; ++i)
            if (entry == kHsAllianceRogueContacts[i] && DirectionsOpen(player, i))
                Option(menu, i == 0 ? "Where should I look next?" : "Where can I find Alex Horley?", sender, kDirectionFirst + i);
    if (entry == kHsAllianceRogue) HsCardTraderAppendGossip(player, creature, menu, sender);
}

bool HsAllianceRogueHandleSelect(Player* player, Creature* creature, uint32_t action, uint32_t sender)
{
    if (action < kInvitation || action > kLast) return false;
    if (!player || !player->PlayerTalkClass) return true;
    if (!HsAllianceRogueGossipActive(player, creature) || !player->GetSession() || !player->IsAlive() ||
        !creature->IsAlive() || !player->IsWithinDistInMap(creature, INTERACTION_DISTANCE) ||
        !player->IsWithinLOSInMap(creature))
    {
        player->PlayerTalkClass->CloseGossip();
        return true;
    }
    bool offered = false;
    auto& menu = player->PlayerTalkClass->GetGossipMenu();
    for (uint32 i = 0; i < menu.MenuItemCount(); ++i)
        offered |= menu.MenuItemSender(i) == sender && menu.MenuItemAction(i) == action;
    if (!offered) { player->PlayerTalkClass->CloseGossip(); return true; }
    uint32 const entry = creature->GetEntry();
    if ((action == kDiceExplanation || action == kTakeDice) && entry == kHsAllianceRogue && DiceOpen(player))
    {
        if (action == kDiceExplanation)
            Page(player, creature, 9195241, "I'll keep his eyes off the cup.", sender, kTakeDice);
        else if (Give(player, kHsAllianceRogueDice))
            Ending(player, creature, 9195241);
        return true;
    }
    if (action == kInvitation && entry == kHsAllianceRogueOperative && InvitationOpen(player))
    {
        if (Give(player, kHsAllianceRogueInvitation)) Ending(player, creature, 9195222);
        return true;
    }
    if (action >= kClueFirst && action < kDirectionFirst)
    {
        uint32 const contact = (action - kClueFirst) / 3;
        uint32 const page = (action - kClueFirst) % 3;
        if (!HsAllianceRogueContactAllowed(true, Open(player, kHsAllianceRogueInvestigation), Clues(player), entry) ||
            kHsAllianceRogueContacts[contact] != entry) { player->PlayerTalkClass->CloseGossip(); return true; }
        static char const* const replies[3][2] = {
            { "Someone slipped it into my bag.", "I'll ask Osric Strang." },
            { "I'm trying to learn the game.", "I'll find him. Thanks." },
            { "You were expecting me?", "Then teach me how Rogue plays." }
        };
        if (page < 2) Page(player, creature, 9195223 + contact * 3 + page, replies[contact][page], sender, action + 1);
        else
        {
            player->KilledMonsterCredit(kHsAllianceRogueClues[contact]);
            if (contact < 2) Page(player, creature, 9195225 + contact * 3, "Remind me where to go.", sender, kDirectionFirst + contact);
            else Ending(player, creature, 9195231);
        }
        return true;
    }
    if (action >= kDirectionFirst && action < kGamble)
    {
        uint32 const contact = action - kDirectionFirst;
        if (contact < 2 && entry == kHsAllianceRogueContacts[contact] && DirectionsOpen(player, contact))
            Ending(player, creature, 9195225 + contact * 3);
        else player->PlayerTalkClass->CloseGossip();
        return true;
    }
    if (entry == kHsAllianceRogueAedis && CardOpen(player, 0))
    {
        bool const hasDice = player->GetItemCount(kHsAllianceRogueDice, false) != 0;
        if (action == kGamble)
        {
            if (hasDice)
                Page(player, creature, 9195232, "Sure let me roll. When did you last win in Hearthstone? Any good stories?", sender, kDistract);
            else Ending(player, creature, 9195243);
        }
        else if (action == kDistract && hasDice)
            Page(player, creature, 9195242, "Slip Alex's dice into the cup and roll.", sender, kRoll);
        else if (action == kRoll && HsAllianceRogueRiggedWin(offered, hasDice))
        {
            // This action exists only on the distraction page. The loaded dice
            // guarantee six on the first attempt. With a full bag nothing is
            // consumed, so the player can make room and roll again.
            if (Give(player, kHsAllianceRogueCardItems[0]))
            {
                player->DestroyItemCount(kHsAllianceRogueDice, 1, true);
                Ending(player, creature, 9195235);
            }
            else Page(player, creature, 9195242, "Slip Alex's dice into the cup and roll.", sender, kRoll);
        }
        else player->PlayerTalkClass->CloseGossip();
        return true;
    }
    if (entry == kHsAllianceRogueOsric && CardOpen(player, 1))
    {
        if (action == kPersuade) Page(player, creature, 9195236, "A spare card could help someone learn.", sender, kReason);
        else if (action == kReason) Page(player, creature, 9195237, "I'll put it to use. Alex is teaching me.", sender, kPromise);
        else if (action == kPromise)
        {
            if (Give(player, kHsAllianceRogueCardItems[1])) Ending(player, creature, 9195238);
            else Page(player, creature, 9195237, "I'll put it to use. Alex is teaching me.", sender, kPromise);
        }
        else player->PlayerTalkClass->CloseGossip();
        return true;
    }
    if (action == kPickupDirections && entry == kHsAllianceRogue && CardOpen(player, 2))
        Ending(player, creature, 9195240);
    else player->PlayerTalkClass->CloseGossip();
    return true;
}

void HsAllianceRogueAfterQuestChange(Player* player)
{
    if (!player || player->GetQuestStatus(kHsAllianceRogueCards) != QUEST_STATUS_NONE) return;
    for (uint32 item : kHsAllianceRogueCardItems)
    {
        uint32 const count = player->GetItemCount(item, true);
        if (count) player->DestroyItemCount(item, count, true, false, true);
    }
    uint32 const dice = player->GetItemCount(kHsAllianceRogueDice, true);
    if (dice) player->DestroyItemCount(kHsAllianceRogueDice, dice, true, false, true);
}
void AddHsAllianceRogueQuestScripts() { new PickupScript(); }
void HsAllianceRogueUpdate(uint32_t diff)
{
    g_sweep += diff;
    if (g_sweep < 250) return;
    g_sweep = 0;
    Map* map = sMapMgr.FindMap(0, 0);
    if (!map) { g_pickup.Clear(); return; }
    constexpr float x = -8653.173828f, y = 442.103241f, z = 111.817772f, orientation = 4.279852f;
    bool needed = false;
    for (auto const& reference : map->GetPlayers())
    {
        Player* player = reference.getSource();
        if (CardOpen(player, 2) && player->IsWithinDist3d(x, y, z, 100.0f)) { needed = true; break; }
    }
    GameObject* card = map->GetGameObject(g_pickup);
    if (card && card->IsDeleted()) return;
    if (!needed) { if (card) card->Delete(); return; }
    if (card) return;
    auto* pickup = new HiddenCard();
    if (!pickup->Create(map->GenerateLocalLowGuid(HIGHGUID_GAMEOBJECT), kHsAllianceRoguePickup, map,
        x, y, z, orientation, 0, 0, std::sin(orientation / 2.0f), std::cos(orientation / 2.0f), 100, GO_STATE_READY))
    { delete pickup; return; }
    pickup->SetRespawnTime(0);
    g_pickup = pickup->GetObjectGuid();
    map->Add(static_cast<GameObject*>(pickup));
}
