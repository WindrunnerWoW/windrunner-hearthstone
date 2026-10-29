#ifndef MOD_HEARTHSTONE_HS_QUEST_PICKUP_H
#define MOD_HEARTHSTONE_HS_QUEST_PICKUP_H

#include "GameObjectAI.h"
#include "quests/HsQuestPickupRules.h"

class Player;

// Shared native Opening lifetime/delivery for the module's scripted quest chests.
// Each chain supplies its existing quest/item gate and keeps its placement policy.
class HsQuestPickupAI : public GameObjectAI
{
public:
    HsQuestPickupAI(GameObject* go, uint32 item,
        uint32 castTimeIndex = kHsQuestPickupHalfSecondCastIndex, bool sharedDespawn = false);
    bool OnUse(Unit* user) override;
    void UpdateAI(uint32 diff) override;
    bool CanCollect(Player* player) const;
    void FinishPickup(Player* player);
    void SendInteractionState(Player* player);

protected:
    virtual bool IsEligible(Player const* player) const = 0;
    virtual void OnCollected(Player* /*player*/) {}
    uint32 QuestItem() const { return m_item; }

private:
    bool HasBagSpace(Player* player) const;
    uint32 m_item;
    uint32 m_castTimeIndex;
    bool m_sharedDespawn;
    uint32 m_viewTimer = 0;
};

bool HsQuestPickupAvailable(GameObject const* pickup);
void AddHsQuestPickupScripts();

#endif
