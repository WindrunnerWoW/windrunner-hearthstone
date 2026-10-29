/*
 * Battlegrounds game loop: a lobby of 8 players, 4 against 4, played round by round. Players 0-3 are
 * faction A (index 0), players 4-7 faction B (index 1). Any player is an NPC (decided by a policy the game calls) or
 * "external" (acts through the action API, like the human).
 *
 * PURE C++ (standard library and the other src/battlegrounds headers, no core headers).
 *
 * A ROUND
 *  1. Start of turn: every living player gets gold, hooks and new offers (Shop::StartTurn).
 *  2. Shop phase: NPCs play their turn at once (NpcPolicy::PlayTurn, then EndTurn); external players act through the
 *     action API and finish with EndTurn. The phase ends when every living player has ended the turn.
 *  3. End of turn hooks run at each player's EndTurn. A hero that died in the shop phase (an effect) is eliminated now.
 *  4. Pairing: ALWAYS cross-faction; the previous opponent is avoided where another pairing exists. With uneven living
 *     counts the larger faction takes turns: as many of its players fight as the smaller faction has, those who fought
 *     longest ago first (ties at random), and the others rest - no fight, they heal kRestHeal health up to their
 *     hero's maximum (replacing the ghost fights: a ghost could only cost health), and
 *     nothing from round kRestHealEndRound on.
 *  5. Combat (SimulateCombat, one seed from the game Rng per fight), then damage to the loser's hero: the winner's
 *     tavern tier + surviving tiers, capped at 15 per combat until the first elimination. The cap is read at the START
 *     of the round, so every fight of the round in which the first player dies is still capped.
 *  6. Elimination at health <= 0: the player's board, hand and tavern return to the pool. Players eliminated in the
 *     same round go out in the order lowest health first.
 *  7. The game ends when a faction has no living player (that faction loses). If everybody dies in one round, the faction
 *     of the player eliminated last wins. After kMaxRounds the faction with the higher total health wins (tie: A).
 *
 * THE ACTION API. Buy, Sell, Play, Move, Refresh, Freeze, Upgrade, Choose (a Discover option) and EndTurn. Each returns an
 * ActionResult (Ok, or the reason it was refused with nothing changed). NPCs use it too, so the human and the NPCs
 * play by the same rules. Game-level refusals: GameOver, Eliminated, NotShopPhase (turn already ended).
 *
 * SEAMS. NpcPolicy is the hook for NPC decisions (HsBgAi is the real one; SimpleNpcPolicy is a fallback for tests).
 * Sending minions to allies would be a new action here and a pool/hand change in Shop; nothing prepares it beyond
 * the faction numbering. Hero powers and armor would extend PlayerShop and the damage step.
 */

#ifndef MOD_HEARTHSTONE_HS_BG_GAME_H
#define MOD_HEARTHSTONE_HS_BG_GAME_H

#include "HsBgCombat.h"
#include "HsBgEffects.h"
#include "HsBgPool.h"
#include "HsBgShop.h"
#include "HsBgTypes.h"

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace Hs
{
    namespace Bg
    {
        constexpr int kPlayers = 8;
        constexpr int kFactionSize = 4;
        constexpr int kMaxRounds = 120;       // safety net: the game is decided on total health
        constexpr int kRested = -3;           // LastOpponent of a player who rested (had no fight) last round
        constexpr int kRestHeal = 3;          // health a resting player heals (up to the hero's maximum)
        // resting heals nothing from this round on (a dampening like WoW Arena's), so a lone
        // survivor can still wear the larger faction down. bg_sim: games average 14 rounds, 90% end by round 18.
        constexpr int kRestHealEndRound = 20;

        inline int FactionOf(int player) { return player / kFactionSize; }

        enum class PlayerKind : uint8_t { Npc, External };
        enum class GamePhase : uint8_t { Pick, Quest, Shop, Over };

        class Game;

        // Decides what an NPC player does in its shop phase, through the action API of the game. PlayTurn should return
        // with the turn finished or not: the game closes any open Discover (first option) and ends the turn itself.
        class NpcPolicy
        {
        public:
            virtual ~NpcPolicy() {}
            virtual void PlayTurn(Game& game, int player) = 0;
        };

        // Fallback policy (the game uses HsBgAi): upgrades on a simple turn schedule, buys the best offer it
        // can afford, plays everything, sells the weakest minion when the board is full, sometimes refreshes or freezes.
        class SimpleNpcPolicy : public NpcPolicy
        {
        public:
            void PlayTurn(Game& game, int player) override;
        };

        struct GameSetup
        {
            uint32_t seed = 1;
            PlayerKind kinds[kPlayers] = { PlayerKind::Npc, PlayerKind::Npc, PlayerKind::Npc, PlayerKind::Npc,
                PlayerKind::Npc, PlayerKind::Npc, PlayerKind::Npc, PlayerKind::Npc };
            NpcPolicy* policy = nullptr;                  // nullptr = a built-in SimpleNpcPolicy
            EffectRegistry const* effects = nullptr;      // nullptr = EffectRegistry::Global()
            bool slice15AOnly = true;
            int tribeCount = kTribesPerLobby;
            // One pool per faction (pool 0 = players 0-3, pool 1 = players 4-7), each with its own half-size
            // copies (FactionCopiesForTier); false keeps the single shared pool 0 with the full copies.
            bool factionPools = false;
            // Open a hero-pick phase before round 1. `humanSeat` gets 4 choices, each NPC 2 (18 distinct).
            bool heroPick = false;
            int humanSeat = 0;
            // Offer one quest + reward pair per player at the start of `questRound` (turn 4), 3 pairs each.
            bool quests = false;
            int questRound = 4;
            bool autoRun = true;                          // run NPC turns (and whole NPC-only games) inside the constructor
        };

        // One fight of a round, for the log and the tests.
        struct CombatRecord
        {
            int round = 0;
            int a = 0;            // the fighting players
            int b = 0;
            int winner = -1;      // the winning player, -1 for a draw
            int damage = 0;       // damage to the loser
            bool capped = false;  // the 15-damage cap was on this round
            int attacks = 0;
            bool operator==(CombatRecord const& o) const
            {
                return round == o.round && a == o.a && b == o.b && winner == o.winner && damage == o.damage &&
                    capped == o.capped && attacks == o.attacks;
            }
        };

        // A fight that involved an external player, kept with its full event log for its playback. `a` fought `b`,
        // `result.winner` 0 means `a`.
        struct ExternalFight
        {
            int round = 0;
            int a = 0;
            int b = 0;
            CombatResult result;
        };

        class Game
        {
        public:
            explicit Game(GameSetup const& setup);

            // ------------------------------------------------------------------------ state
            GamePhase Phase() const { return m_over ? GamePhase::Over : (m_picking ? GamePhase::Pick : (m_questPicking ? GamePhase::Quest : GamePhase::Shop)); }
            bool Over() const { return m_over; }
            bool Picking() const { return m_picking; }
            bool QuestPicking() const { return m_questPicking; }
            // The hero choice offered to a player (4 for the human seat, 2 for an NPC), empty outside the pick.
            std::vector<int> const& HeroChoices(int player) const;
            bool HeroPicked(int player) const { return player >= 0 && player < kPlayers && m_heroPicked[player]; }
            int PlayerHero(int player) const;   // the picked hero table index, -1 before the pick
            // The quest offer of a player (quest table index, reward table index), empty when it is not pending.
            std::vector<std::pair<int, int>> const& QuestOffer(int player) const;
            int PlayerQuest(int player) const;      // the taken quest table index, -1 before the pick
            int PlayerReward(int player) const;     // the paired reward table index, -1 before the pick
            int QuestProgress(int player) const;    // the finished count
            int QuestRequired(int player) const;    // the quest's number
            bool QuestDone(int player) const;       // the reward is active
            // Sire Denathrius's second quest (slot 1).
            int PlayerQuest2(int player) const;
            int PlayerReward2(int player) const;
            int QuestProgress2(int player) const;
            int QuestRequired2(int player) const;
            bool QuestDone2(int player) const;
            int Round() const { return m_round; }                    // 1-based turn number of the current shop phase
            int WinnerFaction() const { return m_winner; }           // -1 while running
            bool TimedOut() const { return m_timedOut; }
            bool Alive(int player) const { return player >= 0 && player < kPlayers && m_alive[player]; }
            bool TurnEnded(int player) const { return player >= 0 && player < kPlayers && m_ended[player]; }
            PlayerKind Kind(int player) const { return m_kinds[player]; }
            int LivingInFaction(int faction) const;
            int EliminationRound(int player) const { return m_elimRound[player]; } // 0 = still alive
            std::vector<int> const& EliminationOrder() const { return m_elimOrder; } // first out first
            // The players of a faction, best first: survivors by health, then the eliminated, the later out the better.
            std::vector<int> FactionRanking(int faction) const;
            int Placement(int player) const;                         // 1..4 within the faction
            std::vector<CombatRecord> const& Combats() const { return m_combats; }
            // Each faction's own five (both are the same set when factionPools is off).
            std::vector<uint32_t> const& FactionTribes(int faction) const
            {
                static std::vector<uint32_t> const none;
                return faction == 0 || faction == 1 ? m_tribes[faction] : none;
            }
            bool DamageCapped() const { return m_capThisRound; }
            Shop& GetShop() { return m_shop; }
            Shop const& GetShop() const { return m_shop; }
            PoolSet& Pool() { return m_pool; }
            PoolSet const& Pool() const { return m_pool; }
            Rng& Random() { return m_rng; }
            // The player a player fought last round (a player index, kRested when it rested, -2 before the first fight).
            int LastOpponent(int player) const { return m_lastOpponent[player]; }
            // The health a player healed by resting last round (0 when it fought, or rested at full health).
            int RestHealed(int player) const { return m_restHealed[player]; }
            // Fights with an external player since the last ClearExternalFights, oldest first (NPC-only fights are not kept).
            std::vector<ExternalFight> const& ExternalFights() const { return m_externalFights; }
            void ClearExternalFights() { m_externalFights.clear(); }

            // ------------------------------------------------------------------------ the action API
            ActionResult Buy(int player, int offerIndex);
            ActionResult Sell(int player, Zone zone, int index);
            ActionResult Play(int player, int handIndex, int slot, uint32_t targetUid = 0);
            // Cast a spell from the hand (a friendly board minion target when the spell needs one).
            ActionResult Cast(int player, int handIndex, uint32_t targetUid = 0);
            ActionResult Move(int player, int fromSlot, int toSlot);
            ActionResult Refresh(int player);
            ActionResult Freeze(int player, bool frozen);
            ActionResult Upgrade(int player);
            ActionResult Choose(int player, int optionIndex);
            // Pick a hero from HeroChoices(player) during the pick phase.
            ActionResult ChooseHero(int player, int choiceIndex);
            // Pick a quest + reward pair from QuestOffer(player) during the quest-pick phase.
            ActionResult ChooseQuest(int player, int choiceIndex);
            // Use a player's active hero power (passive powers refuse with NoPower).
            ActionResult HeroPower(int player, uint32_t targetUid = 0, int offerIndex = -1, uint32_t secondUid = 0);
            // Send a hand minion to a living ally of the same faction (free, once per shop turn).
            ActionResult Send(int player, int handIndex, int ally);
            // Ends the player's turn. When every living player has ended, the round is fought and the next one starts
            // (NPC turns of the next round run inside this call).
            ActionResult EndTurn(int player);

            // Runs the NPC turns of the current shop phase and, while nobody external is left to wait for, the rounds
            // after it. Called by the constructor (autoRun) and by EndTurn; safe to call again.
            void Pump();

        private:
            ActionResult Check(int player) const;
            void DealHeroChoices();
            void NpcPickHeroes();
            bool AllHeroesPicked() const;
            void DealQuestOffers();                          // 3 pairs for each living player at turn `questRound`
            void DealStartQuestOffers();                    // Denathrius's 2 pairs at round 1
            void DealQuestOffers(int player, int slot, int count);
            void NpcPickQuests();
            bool AnyQuestPending() const;
            void TakeQuest(int player, int choiceIndex);   // take an offered pair with the types it was offered with
            void StartRound();
            void ResolveRound();
            void Eliminate(int player);
            // Writes the persistent changes of a combat (CombatResult::persistent) back to the shop board / hand of
            // `player`, the owner of `side`.
            void ApplyPersistent(int player, int side, std::vector<PersistentChange> const& changes);
            void CheckGameOver();
            std::vector<std::pair<int, int>> Pair();
            bool AllEnded() const;

            GameSetup m_setup;
            std::unique_ptr<NpcPolicy> m_ownedPolicy;
            NpcPolicy* m_policy = nullptr;
            EffectRegistry const& m_effects;
            Rng m_rng;
            PoolSet m_pool;
            Shop m_shop;
            std::vector<uint32_t> m_tribes[2];
            PlayerKind m_kinds[kPlayers];
            bool m_alive[kPlayers];
            bool m_ended[kPlayers];
            bool m_picking = false;
            bool m_questPicking = false;
            std::vector<int> m_heroChoices[kPlayers];
            bool m_heroPicked[kPlayers] = { false, false, false, false, false, false, false, false };
            int m_elimRound[kPlayers];
            int m_lastOpponent[kPlayers];
            int m_lastFought[kPlayers];       // the round a player last fought in (0 = not yet): the resting rotation
            int m_restCount[kPlayers];        // how often a player has rested: the rotation rests the fewest first
            int m_restHealed[kPlayers];
            std::vector<int> m_elimOrder;
            std::vector<CombatRecord> m_combats;
            std::vector<ExternalFight> m_externalFights;
            int m_round = 0;
            bool m_over = false;
            bool m_pumping = false;
            bool m_capThisRound = true;
            bool m_timedOut = false;
            int m_winner = -1;
        };
    }
}

#endif
