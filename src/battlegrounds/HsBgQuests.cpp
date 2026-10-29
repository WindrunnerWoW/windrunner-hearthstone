/*
 * Battlegrounds quest rules. The 12 live quests of 24.2 and their
 * counting rules, plus the NPC pick value. Pure C++; see HsBgQuests.h.
 */

#include "HsBgQuests.h"

#include <algorithm>

namespace Hs
{
    namespace Bg
    {
        void RegisterAllQuests(QuestRegistry& reg)
        {
            // One rule per live quest. The event names say which engine point fires it; the counting decisions
            // (nothing before the pick, a triple is not a summon, combat summons do not count, Magnetic is
            // a play but not a Battlecry) are enforced by WHERE the engine fires the event, not here.
            reg.Register(QuestRule{ "BG24_Quest_112", QuestEvent::Refresh, false, false });             // refresh 10
            reg.Register(QuestRule{ "BG24_Quest_114", QuestEvent::Summon, false, false });              // summon 14
            reg.Register(QuestRule{ "BG24_Quest_120", QuestEvent::LoseOrTieCombat, false, false });     // lose/tie 3
            reg.Register(QuestRule{ "BG24_Quest_123", QuestEvent::StatsIncreased, false, false });      // stats 15
            reg.Register(QuestRule{ "BG24_Quest_124", QuestEvent::FriendlyDied, true, false });         // deaths 18, each
            reg.Register(QuestRule{ "BG24_Quest_125", QuestEvent::CombatOrdered, false, false });       // ordered 4
            reg.Register(QuestRule{ "BG24_Quest_126", QuestEvent::GoldSpent, true, false });            // spend 25 gold
            reg.Register(QuestRule{ "BG24_Quest_151", QuestEvent::PlayTribe, false, true });            // 5 of two types
            reg.Register(QuestRule{ "BG24_Quest_311", QuestEvent::PlayBattlecry, false, false });       // 6 battlecries
            reg.Register(QuestRule{ "BG24_Quest_313", QuestEvent::Buy, false, false });                 // buy 7
            reg.Register(QuestRule{ "BG24_Quest_314", QuestEvent::CardToHand, false, false });          // add 15 cards
            reg.Register(QuestRule{ "BG24_Quest_320", QuestEvent::DeathrattleTriggered, true, false });  // 6 deathrattles, each
        }

        void QuestRegistry::Register(QuestRule const& rule)
        {
            auto it = m_rules.find(rule.id);
            if (it != m_rules.end())
            {
                m_errors.push_back(std::string("duplicate quest rule ") + rule.id);
                return;
            }
            m_rules[rule.id] = rule;
        }

        QuestRule const* QuestRegistry::Find(std::string const& questId) const
        {
            auto it = m_rules.find(questId);
            return it == m_rules.end() ? nullptr : &it->second;
        }

        QuestRegistry const& QuestRegistry::Global()
        {
            static QuestRegistry const reg = []
            {
                QuestRegistry r;
                RegisterAllQuests(r);
                return r;
            }();
            return reg;
        }

        int QuestIncrement(QuestRule const& rule, QuestEventInfo const& ev, QuestState const& st)
        {
            if (ev.kind != rule.kind)
                return 0;
            if (rule.tribeCheck)
            {
                if (ev.tribe == 0)
                    return 0;
                bool const a = st.typeA != 0 && (ev.tribe & (st.typeA | kTribeAll)) != 0;
                bool const b = st.typeB != 0 && (ev.tribe & (st.typeB | kTribeAll)) != 0;
                return (a || b) ? 1 : 0;
            }
            return rule.useAmount ? std::max(0, ev.amount) : 1;
        }

        void ApplyQuestEvent(QuestState& st, QuestEventInfo const& ev)
        {
            if (st.quest < 0 || st.done || st.quest >= QuestCount())
                return;
            QuestRule const* rule = QuestRegistry::Global().Find(QuestAt(st.quest).id);
            if (!rule)
                return;
            int const inc = QuestIncrement(*rule, ev, st);
            if (inc <= 0)
                return;
            st.count += inc;
            if (st.count >= st.required)
            {
                st.count = st.required;
                st.done = true;
            }
        }

        QuestTypes DrawQuestTypes(int questIndex, int rewardIndex, std::vector<uint32_t> const& lobbyTribes, Rng& rng)
        {
            QuestTypes t;
            // a lobby with fewer than two tribes cannot name two types
            if (questIndex >= 0 && questIndex < QuestCount() && std::string(QuestAt(questIndex).id) == "BG24_Quest_151" &&
                lobbyTribes.size() >= 2)
            {
                std::vector<uint32_t> tribes = lobbyTribes;
                rng.Shuffle(tribes);
                t.typeA = tribes[0];
                t.typeB = tribes[1];
            }
            if (rewardIndex >= 0 && rewardIndex < RewardCount() && std::string(RewardAt(rewardIndex).id) == "BG24_Reward_134" &&
                !lobbyTribes.empty())
                t.rewardType = lobbyTribes[rng.Uniform(static_cast<uint32_t>(lobbyTribes.size()))];
            return t;
        }

        int QuestPickValue(int questIndex, int rewardIndex)
        {
            // A rough reward strength minus the quest's requirement, so an NPC prefers a strong reward with an easy
            // quest. A missing entry falls back to 3. Game::NpcPickQuests adds the board fit.
            static std::map<std::string, int> const rewardValue = {
                { "BG24_Reward_312", 5 },   // Staff of Origination
                { "BG24_Reward_109", 4 },   // Stolen Gold
                { "BG24_Reward_130", 4 },   // Ghastly Mask
                { "BG24_Reward_125", 4 },   // The Smoking Gun
                { "BG24_Reward_107", 3 }, { "BG24_Reward_111", 3 }, { "BG24_Reward_113", 3 },
                { "BG24_Reward_115", 3 }, { "BG24_Reward_128", 3 }, { "BG24_Reward_131", 3 },
                { "BG24_Reward_134", 3 }, { "BG24_Reward_135", 3 }, { "BG24_Reward_136", 3 },
                { "BG24_Reward_138", 3 }, { "BG24_Reward_308", 3 }, { "BG24_Reward_309", 3 },
                { "BG24_Reward_321", 3 }, { "BG24_Reward_331", 3 }, { "BG24_Reward_350", 3 },
                { "BG24_Reward_305", 2 }, { "BG24_Reward_306", 2 }, { "BG24_Reward_129", 2 },
                { "BG24_Reward_313", 2 },   // Wondrous Wisdomball
            };
            int const rv = (rewardIndex >= 0 && rewardIndex < RewardCount())
                ? [&] { auto it = rewardValue.find(RewardAt(rewardIndex).id); return it == rewardValue.end() ? 3 : it->second; }()
                : 3;
            int const progress = (questIndex >= 0 && questIndex < QuestCount()) ? QuestAt(questIndex).progress : 0;
            return rv * 16 - progress;
        }
    }
}
