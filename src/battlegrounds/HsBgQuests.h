/*
 * Battlegrounds quests: the 12 live 24.2 quests and their counting rules. Like the effect
 * registry (HsBgEffects.h) and the hero powers (HsBgHeroes.h), a quest is DATA keyed by its id: a QuestRule says which
 * shop/combat event it counts. The engine fires an event at the matching point (Shop and Game); one event only ever
 * applies to one player, so a quest state is a plain counter per player (PlayerShop::quest).
 *
 * PURE C++ (the standard library and HsBgTypes.h, no core headers), so it can be unit-tested headlessly.
 *
 * The counting decisions are the rule table in
 * HsBgQuests.cpp; `bg_quests_tests` pins one test per quest. Progress numbers and the offer list are the generated
 * quest table (HsBgQuestData.inc). The offer at turn 4 (3 distinct quest + reward pairs) and the ChooseQuest
 * action live in Game.
 */

#ifndef MOD_HEARTHSTONE_HS_BG_QUESTS_H
#define MOD_HEARTHSTONE_HS_BG_QUESTS_H

#include "HsBgTypes.h"

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace Hs
{
    namespace Bg
    {
        // The events a quest can count. The engine fires one at the matching point.
        enum class QuestEvent : uint8_t
        {
            Refresh,               // Bob's Tavern Refreshed (manual or free; not the start-of-turn restock)
            Summon,                // a minion summoned to the board in the shop phase (played or from an effect)
            LoseOrTieCombat,       // the player lost or tied a combat
            StatsIncreased,        // a friendly board/hand minion's stats went up (Shop::Buff)
            FriendlyDied,          // a friendly minion died in combat
            CombatOrdered,         // the player's board was low-to-high Attack when a combat started
            GoldSpent,             // gold spent on an action (Buy / Refresh / Upgrade / hero power)
            PlayTribe,             // a minion was played from hand (Magnetic included); `tribe` is its tribe mask
            PlayBattlecry,         // a minion with a Battlecry was played from hand
            Buy,                   // a minion was bought
            CardToHand,            // a card was added to the hand
            DeathrattleTriggered,  // a friendly Deathrattle trigger fired in combat
            Count,
        };

        // One event. `amount` is how many times it happened (usually 1); `tribe` is set for PlayTribe.
        struct QuestEventInfo
        {
            QuestEvent kind = QuestEvent::Count;
            int amount = 1;
            uint32_t tribe = 0;
        };

        // The minion types an offered pair names, drawn with the offer so the pick can show them.
        struct QuestTypes
        {
            uint32_t typeA = 0;        // Unlikely Duo's two lobby minion types (0 for every other quest)
            uint32_t typeB = 0;
            uint32_t rewardType = 0;   // The Friends Along the Way's minion type (0 for every other reward)
        };

        // Per-player quest state, kept in PlayerShop::quest.
        struct QuestState
        {
            int quest = -1;      // quest table index (HsBgTypes.h), -1 = none taken
            int reward = -1;     // the paired reward table index, -1 = none
            int count = 0;
            int required = 0;    // the quest's `{0}` (the generated progress number)
            bool done = false;   // the reward is active
            uint32_t typeA = 0;  // Unlikely Duo's two lobby minion types (0 for every other quest)
            uint32_t typeB = 0;
            uint32_t rewardType = 0;   // The Friends Along the Way's type (0 = drawn when the reward is gained)
            std::vector<std::pair<int, int>> offer;   // pending offer: (quest index, reward index); empty = none
            std::vector<QuestTypes> offerTypes;       // the types of each offered pair, parallel to `offer`
        };

        // One quest's counting rule. `useAmount` counts the event's amount (gold spent, the deaths and Deathrattles of a
        // combat) instead of 1; `tribeCheck` is
        // Unlikely Duo (the event's tribe must be typeA or typeB).
        struct QuestRule
        {
            char const* id = "";
            QuestEvent kind = QuestEvent::Count;
            bool useAmount = false;
            bool tribeCheck = false;
        };

        class QuestRegistry
        {
        public:
            void Register(QuestRule const& rule);
            QuestRule const* Find(std::string const& questId) const;
            int Size() const { return static_cast<int>(m_rules.size()); }
            std::vector<std::string> const& Errors() const { return m_errors; }
            static QuestRegistry const& Global();
        private:
            std::map<std::string, QuestRule> m_rules;
            std::vector<std::string> m_errors;
        };

        // Registers every implemented quest (a duplicate id is an error).
        void RegisterAllQuests(QuestRegistry& reg);

        // How much one event adds to one quest (0 when the event is not this quest's).
        int QuestIncrement(QuestRule const& rule, QuestEventInfo const& ev, QuestState const& st);

        // Applies an event to a player's quest: adds the increment and completes it at `required` (idempotent once done).
        void ApplyQuestEvent(QuestState& st, QuestEventInfo const& ev);

        // Draws the types an offered pair names from the lobby's tribes: Unlikely Duo's two distinct types
        // (BG24_Quest_151) and The Friends Along the Way's type (BG24_Reward_134). Zero for every other quest/reward.
        QuestTypes DrawQuestTypes(int questIndex, int rewardIndex, std::vector<uint32_t> const& lobbyTribes, Rng& rng);

        // The board-free value of a quest/reward pair for an NPC pick (higher is better): the reward's strength minus
        // the quest's number. Game adds how well the quest fits the NPC's board.
        int QuestPickValue(int questIndex, int rewardIndex);
    }
}

#endif
