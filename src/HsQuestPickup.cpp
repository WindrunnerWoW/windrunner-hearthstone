#include "HsQuestPickup.h"
#include "GameObject.h"
#include "Map.h"
#include "Opcodes.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "Spell.h"
#include "SpellMgr.h"
#include "UpdateData.h"
#include "UpdateMask.h"
#include "WorldPacket.h"
#include "WorldSession.h"

namespace
{
    HsQuestPickupAI* PickupAI(GameObject* pickup)
    {
        return pickup ? dynamic_cast<HsQuestPickupAI*>(pickup->AI()) : nullptr;
    }

    // Never retain a GameObject/AI/Player pointer over the native cast lifetime.
    class PickupCast final : public SpellScript
    {
    public:
        explicit PickupCast(GameObject* pickup) : m_pickup(pickup->GetObjectGuid()),
            m_entry(pickup->GetEntry()), m_map(pickup->GetMapId()),
            m_instance(pickup->GetInstanceId()) {}

        SpellCastResult OnCheckCast(Spell* spell, bool /*strict*/) const override
        {
            Player* player = spell->GetCaster()->ToPlayer();
            HsQuestPickupAI* ai = Resolve(player);
            return ai && ai->CanCollect(player) ? SPELL_CAST_OK : SPELL_FAILED_BAD_TARGETS;
        }

        bool OnEffectExecute(Spell* /*spell*/, SpellEffectIndex /*effect*/) const override
        {
            return false; // No OPEN_LOCK, native loot window or duplicate AI callback.
        }

        void OnSuccessfulFinish(Spell* spell) const override
        {
            if (m_finished)
                return;
            m_finished = true; // One delivery attempt, including a full-bag failure.
            Player* player = spell->GetCaster()->ToPlayer();
            if (HsQuestPickupAI* ai = Resolve(player))
                ai->FinishPickup(player);
        }

    private:
        HsQuestPickupAI* Resolve(Player* player) const
        {
            if (!player || !player->IsInWorld() || player->GetMapId() != m_map ||
                player->GetInstanceId() != m_instance)
                return nullptr;
            GameObject* pickup = player->GetMap()->GetGameObject(m_pickup);
            return pickup && pickup->GetEntry() == m_entry ? PickupAI(pickup) : nullptr;
        }
        mutable bool m_finished = false;
        ObjectGuid m_pickup;
        uint32 m_entry;
        uint32 m_map;
        uint32 m_instance;
    };

    class PickupSpell final : public Spell
    {
    public:
        PickupSpell(Player* player, GameObject* pickup, SpellEntry const* opening)
            : Spell(player, opening, false)
        {
            m_spellScript = new PickupCast(pickup);
            MarkCustom(); // Spell::Delete frees the per-attempt SpellEntry copy.
        }
    };

    // Native chest clicks can arrive as either GO_USE or an Opening cast. GO_USE
    // already calls AI once. Route native Opening through the same AI before its
    // OPEN_LOCK effect can call it twice and continue into shared native loot.
    class PickupPacketScript final : public ServerScript
    {
    public:
        PickupPacketScript() : ServerScript("hearthstone_quest_pickup_opening",
            { SERVERHOOK_CAN_PACKET_RECEIVE }) {}

        bool CanPacketReceive(WorldSession* session, WorldPacket const& packet) override
        {
            if (packet.GetOpcode() != CMSG_CAST_SPELL && packet.GetOpcode() != CMSG_LOOT)
                return true;
            Player* player = session ? session->GetPlayer() : nullptr;
            if (!player || !player->IsInWorld())
                return true;
            WorldPacket copy(packet);
            copy.rpos(0);
            if (packet.GetOpcode() == CMSG_LOOT)
            {
                ObjectGuid guid;
                copy >> guid;
                return !PickupAI(player->GetMap()->GetGameObject(guid));
            }
            uint32 spellId;
            copy >> spellId;
            SpellEntry const* info = sSpellMgr.GetSpellEntry(spellId);
            bool opening = false;
            if (info)
                for (uint32 effect = 0; effect < MAX_EFFECT_INDEX; ++effect)
                    opening = opening || info->Effect[effect] == SPELL_EFFECT_OPEN_LOCK;
            if (!opening)
                return true;
            SpellCastTargets targets;
            copy >> targets.ReadForCaster(player);
            HsQuestPickupAI* ai = PickupAI(targets.getGOTarget());
            if (!ai)
                return true;
            if (player->IsSelfMover() && player->HasActiveSpell(spellId) && !info->IsPassiveSpell())
            {
                player->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_USE);
                ai->OnUse(player);
            }
            return false;
        }
    };
}

bool HsQuestPickupAvailable(GameObject const* pickup)
{
    return pickup && HsQuestPickupAvailable(pickup->isSpawned(), pickup->IsDeleted(),
        pickup->getLootState() == GO_READY);
}

HsQuestPickupAI::HsQuestPickupAI(GameObject* go, uint32 item, uint32 castTimeIndex, bool sharedDespawn)
    : GameObjectAI(go), m_item(item), m_castTimeIndex(castTimeIndex), m_sharedDespawn(sharedDespawn) {}

bool HsQuestPickupAI::CanCollect(Player* player) const
{
    return player && player->IsInWorld() && player->GetSession() && player->IsAlive() &&
        player->IsSelfMover() && HsQuestPickupAvailable(me) && me->IsInWorld() &&
        IsEligible(player) && me->IsWithinDistInMap(player, INTERACTION_DISTANCE) &&
        me->IsWithinLOSInMap(player);
}

bool HsQuestPickupAI::HasBagSpace(Player* player) const
{
    ItemPosCountVec destination;
    InventoryResult const result = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, destination, m_item, 1);
    if (result != EQUIP_ERR_OK)
        player->SendEquipError(result, nullptr, nullptr, m_item);
    return result == EQUIP_ERR_OK;
}

bool HsQuestPickupAI::OnUse(Unit* user)
{
    Player* player = user ? user->ToPlayer() : nullptr;
    if (!CanCollect(player))
    {
        SendInteractionState(player);
        return true;
    }
    if (player->IsNonMeleeSpellCasted(false) || !HasBagSpace(player))
        return true; // Duplicate clicks never restart a running native cast.
    SpellEntry const* native = sSpellMgr.GetSpellEntry(3365); // Opening.
    if (!native)
        return true;
    auto* opening = new SpellEntry(*native);
    opening->CastingTimeIndex = m_castTimeIndex;
    opening->ScriptId = 0;
    opening->Effect[EFFECT_INDEX_0] = SPELL_EFFECT_DUMMY;
    opening->EffectMiscValue[EFFECT_INDEX_0] = 0;
    SpellCastTargets targets;
    targets.setGOTarget(me);
    auto* spell = new PickupSpell(player, me, opening);
    spell->prepare(targets);
    return true;
}

void HsQuestPickupAI::FinishPickup(Player* player)
{
    if (!CanCollect(player))
        return;
    ItemPosCountVec destination;
    InventoryResult const result = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, destination, m_item, 1);
    if (result != EQUIP_ERR_OK)
    {
        player->SendEquipError(result, nullptr, nullptr, m_item);
        return;
    }
    if (Item* item = player->StoreNewItem(destination, m_item, true))
    {
        player->SendNewItem(item, 1, true, false);
        if (m_sharedDespawn)
        {
            me->SetLootState(GO_JUST_DEACTIVATED);
            me->UpdateObjectVisibility();
        }
        OnCollected(player);
        SendInteractionState(player);
    }
}

void HsQuestPickupAI::SendInteractionState(Player* player)
{
    if (!player || !player->IsInWorld() || !player->GetSession())
        return;
    player->UpdateVisibilityOf(player, me);
    if (!player->IsInVisibleList(me))
        return;
    bool const eligible = player->IsAlive() && HsQuestPickupAvailable(me) && IsEligible(player);
    static_assert(GO_FLAG_NO_INTERACT == 0x10 && GO_DYNFLAG_LO_ACTIVATE == 0x01 &&
        GO_DYNFLAG_LO_NO_INTERACT == 0x04, "Quest pickup view flags must match the core");
    UpdateMask mask;
    mask.SetCount(GAMEOBJECT_END);
    mask.SetBit(GAMEOBJECT_FLAGS);
    mask.SetBit(GAMEOBJECT_DYN_FLAGS);
    UpdateData updates;
    ByteBuffer& block = updates.AddUpdateBlockAndGetBuffer();
    block << uint8(UPDATETYPE_VALUES) << me->GetPackGUID();
    block << uint8(mask.GetBlockCount());
    block.append(mask.GetMask(), mask.GetLength());
    for (uint32 field = 0; field < GAMEOBJECT_END; ++field)
    {
        if (field == GAMEOBJECT_FLAGS)
        {
            uint32 const flags = me->GetUInt32Value(field);
            block << uint32(eligible ? flags & ~uint32(GO_FLAG_NO_INTERACT) : flags | GO_FLAG_NO_INTERACT);
        }
        else if (field == GAMEOBJECT_DYN_FLAGS)
            block << uint32(eligible ? GO_DYNFLAG_LO_ACTIVATE : GO_DYNFLAG_LO_NO_INTERACT);
    }
    updates.Send(player->GetSession());
}

void HsQuestPickupAI::UpdateAI(uint32 diff)
{
    m_viewTimer += diff;
    if (m_viewTimer < 250)
        return;
    m_viewTimer = 0;
    for (auto const& reference : me->GetMap()->GetPlayers())
        SendInteractionState(reference.getSource());
}

void AddHsQuestPickupScripts() { new PickupPacketScript(); }
