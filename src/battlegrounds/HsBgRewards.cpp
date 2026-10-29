/*
 * Battlegrounds quest rewards: the registry. The Shop behaviours live in HsBgRewardsShop.cpp
 * (they need Shop), so a combat-only unit test can link this file alone. See HsBgRewards.h.
 */

#include "HsBgRewards.h"

namespace Hs
{
    namespace Bg
    {
        void RegisterAllRewards(RewardRegistry& reg)
        {
            // The 23 in-pool rewards. Only the passive fields live here; the behaviour is the RewardKind.
            reg.Register(RewardRule{ "BG24_Reward_107", RewardKind::SnickerSnacks });
            reg.Register(RewardRule{ "BG24_Reward_109", RewardKind::StolenGold });
            reg.Register(RewardRule{ "BG24_Reward_111", RewardKind::EvilTwin });
            reg.Register(RewardRule{ "BG24_Reward_113", RewardKind::RitualDagger });
            reg.Register(RewardRule{ "BG24_Reward_115", RewardKind::Parasol });
            reg.Register(RewardRule{ "BG24_Reward_125", RewardKind::SmokingGun, 5 });
            reg.Register(RewardRule{ "BG24_Reward_128", RewardKind::MirrorShield });
            reg.Register(RewardRule{ "BG24_Reward_129", RewardKind::SecretSinstone });
            {
                RewardRule g;
                g.id = "BG24_Reward_130";
                g.kind = RewardKind::GhastlyMask;
                g.endOfTurnTwice = true;
                reg.Register(g);
            }
            reg.Register(RewardRule{ "BG24_Reward_131", RewardKind::RedHand });
            reg.Register(RewardRule{ "BG24_Reward_134", RewardKind::Friends });
            reg.Register(RewardRule{ "BG24_Reward_135", RewardKind::Yogg });
            reg.Register(RewardRule{ "BG24_Reward_136", RewardKind::TinyHenchmen });
            reg.Register(RewardRule{ "BG24_Reward_138", RewardKind::VictimsSpecter });
            reg.Register(RewardRule{ "BG24_Reward_305", RewardKind::AnimaBribe });
            reg.Register(RewardRule{ "BG24_Reward_306", RewardKind::CookedBook });
            {
                RewardRule t;
                t.id = "BG24_Reward_308";
                t.kind = RewardKind::TealTiger;
                t.tavernBonusPerRefresh = 1;
                reg.Register(t);
            }
            reg.Register(RewardRule{ "BG24_Reward_309", RewardKind::DevilsInTheDetails });
            reg.Register(RewardRule{ "BG24_Reward_312", RewardKind::StaffOfOrigination });
            reg.Register(RewardRule{ "BG24_Reward_313", RewardKind::Wisdomball });
            {
                RewardRule a;
                a.id = "BG24_Reward_321";
                a.kind = RewardKind::AlterEgo;
                a.alterEgoBonus = 6;
                reg.Register(a);
            }
            reg.Register(RewardRule{ "BG24_Reward_331", RewardKind::MenagerieMayhem });
            {
                RewardRule p;
                p.id = "BG24_Reward_350";
                p.kind = RewardKind::PilferedLamps;
                p.twoForGolden = true;
                reg.Register(p);
            }
        }

        void RewardRegistry::Register(RewardRule const& def)
        {
            auto it = m_defs.find(def.id);
            if (it != m_defs.end())
            {
                m_errors.push_back(std::string("duplicate reward ") + def.id);
                return;
            }
            m_defs[def.id] = def;
        }

        RewardRule const* RewardRegistry::Find(std::string const& rewardId) const
        {
            auto it = m_defs.find(rewardId);
            return it == m_defs.end() ? nullptr : &it->second;
        }

        RewardRegistry const& RewardRegistry::Global()
        {
            static RewardRegistry const reg = []
            {
                RewardRegistry r;
                RegisterAllRewards(r);
                return r;
            }();
            return reg;
        }

        RewardRule const* RewardFor(RewardState const& st)
        {
            if (!st.active || st.id < 0 || st.id >= RewardCount())
                return nullptr;
            return RewardRegistry::Global().Find(RewardAt(st.id).id);
        }

        char const* WheelOutcomeName(int roll)
        {
            static char const* const names[] = { "Mysterybox", "Hand of Fate", "Curse of Flesh", "Mindflayer Goggles",
                "Devouring Hunger", "Rod of Roasting" };
            return roll >= 0 && roll < 6 ? names[roll] : "";
        }
        int WheelOutcomeCount() { return 6; }
    }
}
