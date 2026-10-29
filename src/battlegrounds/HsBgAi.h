/*
 * Battlegrounds NPC logic: the real NpcPolicy. It plays a shop phase ONLY through the action
 * API of Game (Buy, Sell, Play, Move, Refresh, Freeze, Upgrade, Choose), so an NPC and the human play by the same
 * rules. It reads the public state (State(player), Round(), FactionTribes()) and never edits a board.
 *
 * PURE C++ (standard library and the other src/battlegrounds headers).
 *
 * WHAT IT DECIDES (all deterministic from the game seed: the only randomness is Game::Random())
 *  - Favourite tribe: the lobby tribe with the most weight on the board, in the hand and in the tavern, plus a taste each
 *    player draws per tribe at turn 1 (0..tribeVariety, so the bots do not all chase the same tribe); it is kept once
 *    the player holds three minions of it or after turn 4, and only changed before that.
 *  - Level-up timing: a wanted tavern tier by turn, (turn + 1) / 2 (tier 2 on turn 3, 3 on turn 5 ... 6 on turn 11);
 *    an upgrade that costs at most 1 gold is always taken. A random per-player bias of -1..+1 was dropped: bg_ab
 *    showed a fixed schedule beating it by 25 points, and levelling a turn earlier or later than that loses clearly.
 *  - Buy and sell values: Value() scores stats, keywords, tier, tribe fit and a pair/triple bonus; a triple completer
 *    is always bought; when the board is full a better offer replaces the weakest board minion.
 *  - Board order: Taunt and Divine Shield minions to the left, then by health; poisonous and Windfury attackers next.
 *  - Freeze: when the player cannot afford a good offer, or has a pair whose third copy is in the tavern.
 * Discovers pick the best Value.
 *
 * AiStats counts the actions the policy tried and the refusals it got (a refusal is a policy mistake, not a rule
 * violation: the game changed nothing), for the simulation report.
 */

#ifndef MOD_HEARTHSTONE_HS_BG_AI_H
#define MOD_HEARTHSTONE_HS_BG_AI_H

#include "HsBgGame.h"

#include <cstdint>

namespace Hs
{
    namespace Bg
    {
        // Why an ally wants a minion: reasons are a bitmask, the score is how much it is worth to that ally.
        enum NeedReason : uint32_t
        {
            kNeedTriple = 1u << 0,
            kNeedTribe = 1u << 1,
            kNeedUpgrade = 1u << 2,
            kNeedBody = 1u << 3,       // set with kNeedUpgrade when the only gain is a free board slot
        };

        struct Need
        {
            int score = 0;
            uint32_t reasons = 0;
        };

        // A need at or above this score is worth showing to the human and worth sending by a bot.
        constexpr int kSendNeedMin = 12;
        // A proactive send must gain this much over what the card is worth to its owner.
        constexpr int kSendGainMin = 10;

        // Knobs of the policy, for A/B runs (tests/bg_ab.cpp). The defaults are the shipped play.
        struct AiTuning
        {
            int tribeBonus = 6;      // Value() bonus for a minion of the favourite tribe
            int tierShift = 0;       // added to the wanted tavern tier
            int refreshExtra = 0;    // added to the refreshes allowed per turn
            int lockHeld = 3;        // minions of the favourite tribe that lock it in
            int lockRound = 4;       // from this round on the favourite is kept
            int biasSpread = 1;      // the per-player level-up bias is drawn from this many values centred on 0 (1 = none)
            int swapMargin = 8;      // a replacement must beat the weakest board minion by this much
            int cheapUpgrade = 1;    // an upgrade that costs at most this much is always taken
            int tribeVariety = 8;    // each player draws a taste of 0..this for every tribe, added to its pick score
            int tierWeight = 2;      // Value() points per tavern tier of a minion
        };

        struct AiStats
        {
            int turns = 0;
            int actions = 0;
            int refused[16] = { 0 };   // by ActionResult value
            int buys = 0;
            int sells = 0;
            int upgrades = 0;
            int refreshes = 0;
            int freezes = 0;
            int triples = 0;           // buys that completed a triple (a golden minion appeared)
            int powerUses = 0;         // active hero powers used
            int sends = 0;             // minions sent to an ally
            int sendTriples = 0;       // sends that completed a triple for the receiver
            int refusedTotal() const
            {
                int n = 0;
                for (int v : refused)
                    n += v;
                return n;
            }
        };

        class Ai : public NpcPolicy
        {
        public:
            void PlayTurn(Game& game, int player) override;

            AiStats const& Stats() const { return m_stats; }
            void SetTuning(AiTuning const& t) { m_tune = t; }
            // The tribe bit the player aims for (0 = none yet); for the simulation report.
            uint32_t Favourite(int player) const { return m_fav[player]; }
            // Scores a minion for buying, selling and swapping; higher is better. `fav` is the favourite tribe bit.
            static int Value(Minion const& m, uint32_t fav);
            // How much the receiver gains from receiving `m`, with the reasons for it. `sender` (when >= 0) blocks
            // another faction; `allyFav` 0 derives the receiver's dominant tribe from its board and hand.
            static Need NeedScore(Shop const& shop, int receiver, Minion const& m, int sender = -1, uint32_t allyFav = 0);

        private:
            struct Ctx;
            void Reset(Game& game);
            void ChooseFavourite(Game& game, int player);
            void Arrange(Ctx& c);
            void Count(ActionResult r);

            AiStats m_stats;
            AiTuning m_tune;
            uint32_t m_fav[kPlayers] = { 0 };
            int m_bias[kPlayers] = { 0 };
            int m_taste[kPlayers][16] = {};   // per-player taste for each faction tribe (tribeVariety)
            int m_lastRound[kPlayers] = { 0 };
            Game const* m_owner[kPlayers] = { nullptr };   // the game a player's state belongs to
        };

        // The tavern tier an NPC with the given bias wants to have reached in a shop phase of the given turn.
        int WantedTier(int turn, int bias);
    }
}

#endif
