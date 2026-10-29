/*
 * Battlegrounds quest rewards: the 23 live 24.2 rewards and their behaviours. Like the hero
 * powers (HsBgHeroes.h), a reward is DATA keyed by its id; the passive modifiers are read by Shop, and the behaviours are
 * dispatched by Shop (RewardStartTurn / RewardEndTurn / RewardRefresh / RewardBuy / RewardSell / RewardDiscover, defined in
 * HsBgRewards.cpp) and by the combat simulator (Stolen Gold, Evil Twin, Ritual Dagger, Staff of Origination). A player has
 * at most ONE reward (the one paired with their quest), so the state is a single RewardState per player (PlayerShop).
 *
 * PURE C++ (the standard library and HsBgTypes.h, no core headers).
 */

#ifndef MOD_HEARTHSTONE_HS_BG_REWARDS_H
#define MOD_HEARTHSTONE_HS_BG_REWARDS_H

#include "HsBgTypes.h"

#include <cstdint>
#include <map>
#include <string>

namespace Hs
{
    namespace Bg
    {
        // The behaviour of one reward. Shop and Combat switch on this; the passive fields below are read directly.
        enum class RewardKind : uint8_t
        {
            None,
            SnickerSnacks,    // end of turn: 2 random friendly minions trigger their Battlecries
            StolenGold,       // start of combat: left/right-most minions become Golden
            EvilTwin,         // start of combat: summon a copy of your highest-Health minion
            RitualDagger,     // a friendly Deathrattle minion that dies gets +4/+4 permanently
            Parasol,          // end of turn: right-most minion gets Stealth and +8 Health
            SmokingGun,       // passive: your minions have +5 Attack
            MirrorShield,     // after a Refresh: a tavern minion gets +4/+4 and Divine Shield
            SecretSinstone,   // after a Discover: an extra copy of the chosen card
            GhastlyMask,      // gain Lightfang Enforcer; your end of turn effects trigger twice
            RedHand,          // start of turn: a hand minion gets +12/+12
            Friends,          // start of turn: 2 random minions of a chosen type
            Yogg,             // start of turn: spin the Wheel of Yogg-Saron
            TinyHenchmen,     // end of turn: +2/+2 to 3 friendly minions of Tier 3 or lower
            VictimsSpecter,   // after a combat: a copy of the last friendly minion that died
            AnimaBribe,       // after a sell: the sold stats go to a tavern minion
            CookedBook,       // after a buy: the bought minion gets +n/+n and n grows
            TealTiger,        // passive: tavern minions get +1/+1 per Refresh this turn
            DevilsInTheDetails, // end of turn: the left/right-most minions consume a tavern minion
            StaffOfOrigination, // start of combat: your minions get +15/+15
            Wisdomball,       // occasionally a helpful Refresh
            AlterEgo,         // passive: +6/+6 on even-tier tavern minions (odd next turn)
            MenagerieMayhem,  // end of turn: +1 Attack for each friendly minion type
            PilferedLamps,    // passive: two copies make a Golden
        };

        // Per-player reward state. `id` is the reward table index (HsBgTypes.h); `active` is set when the quest is done.
        struct RewardState
        {
            int id = -1;
            bool active = false;
            int cookedBook = 1;          // Cooked Book's next bonus
            bool alterEgoOdd = false;    // the Odd form is active (Alter Ego swaps each turn)
            uint32_t friendType = 0;     // The Friends Along the Way's chosen minion type
            int refreshesThisTurn = 0;   // Teal Tiger Sapphire
            bool powerExtraTrigger = false; // Wheel: Mysterybox
            int doneTurn = 0;            // the round the reward became active (the quest completed)
        };

        // One reward. The passive fields are read by Shop / Combat; the behaviour is dispatched by RewardKind.
        struct RewardRule
        {
            char const* id = "";
            RewardKind kind = RewardKind::None;
            int boardAttack = 0;             // The Smoking Gun: +5 Attack to your minions
            int tavernBonusPerRefresh = 0;   // Teal Tiger Sapphire
            int alterEgoBonus = 0;           // Alter Ego: +6/+6 on the active tier parity
            bool endOfTurnTwice = false;     // Ghastly Mask
            bool twoForGolden = false;       // Pilfered Lamps
        };

        class RewardRegistry
        {
        public:
            void Register(RewardRule const& def);
            RewardRule const* Find(std::string const& rewardId) const;
            int Size() const { return static_cast<int>(m_defs.size()); }
            std::vector<std::string> const& Errors() const { return m_errors; }
            static RewardRegistry const& Global();
        private:
            std::map<std::string, RewardRule> m_defs;
            std::vector<std::string> m_errors;
        };

        // Registers every implemented reward (a duplicate id is an error).
        void RegisterAllRewards(RewardRegistry& reg);

        // The active reward's definition, or nullptr when none is active.
        RewardRule const* RewardFor(RewardState const& st);

        // The Wheel of Yogg-Saron outcome names, in dbfId order. `Repr` is for tests/logs.
        char const* WheelOutcomeName(int roll);
        int WheelOutcomeCount();
    }
}

#endif
