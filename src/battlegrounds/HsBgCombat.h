/*
 * Battlegrounds combat simulator: fights two boards on COPIES and returns the winner, the damage
 * to the loser's hero and an event log for addon playback. The effect hooks it dispatches to: HsBgEffects.h.
 *
 * PURE C++ (standard library, HsBgTypes.h, HsBgEffects.h).
 *
 * Summary of the rules implemented:
 *  - The side with more minions attacks first; equal counts: a coin flip from the Rng. Start-of-combat effects resolve
 *    first, the first-attacking side's minions left to right, then the other side's.
 *  - Sides alternate; each side attacks with its minions from left to right and starts again from the left after the
 *    last one. A minion with 0 attack is skipped (a side with no minion that can attack passes). Windfury attacks twice
 *    in a row, Mega-Windfury four times.
 *  - Target: a random Taunt minion if the defender has any, else a random minion (a chooseTarget hook may override).
 *  - Damage is simultaneous. Divine Shield absorbs a hit (whatever its size) and pops; Poisonous destroys a minion it
 *    damages. Deaths resolve left to right, the attacking side before the defending side; Deathrattles summon at the
 *    dead minion's slot; Reborn returns the minion with 1 health, without Reborn, at its slot. A board holds 7:
 *    surplus summons are lost.
 *  - Minions summoned during the fight join the attack order (a summon never
 *    attacks before the attack pointer reaches its slot again).
 *  - The fight ends when a side is empty (both empty = draw) or when neither side has a minion with attack > 0, or
 *    after kMaxTurns turns (draw, flagged). Damage = the winner's tavern tier + the tiers of its surviving minions
 *    (tokens count 1), capped at 15 while capDamage is set (no player eliminated yet).
 *
 * INSTANCE IDS. The simulator renumbers its copies so that both sides' ids are unique and stable for the playback:
 * side 0 minion at slot i of the input board has id 1 + i, side 1 has id 8 + i (kBoardMax + 1 + i), summoned and
 * reborn minions get 15, 16, ... in the order they appear. Minion::uid of the input is not used.
 *
 * The input boards are never modified. Effects that must outlive the combat are collected in
 * CombatResult::persistent (BuffPermanent, RecordPersistentBuff, RecordPersistentAddToHand, Tarecgosa); Game writes
 * them back to the shop boards and hands after each fight.
 */

#ifndef MOD_HEARTHSTONE_HS_BG_COMBAT_H
#define MOD_HEARTHSTONE_HS_BG_COMBAT_H

#include "HsBgEffects.h"
#include "HsBgHeroes.h"
#include "HsBgRewards.h"
#include "HsBgTypes.h"

#include <cstdint>
#include <vector>

namespace Hs
{
    namespace Bg
    {
        constexpr int kDamageCap = 15;        // max damage per combat while no player has been eliminated
        constexpr int kMaxTurns = 5000;       // safety net against effect loops: the fight is declared a draw
        constexpr uint32_t kFirstSummonId = 15; // kBoardMax * 2 + 1

        enum class EventType : uint8_t
        {
            Attack,     // actor attacks target; side = attacker's side
            Damage,     // target loses `amount` health (actor = source minion, 0 if none); side = target's side
            ShieldPop,  // target's Divine Shield absorbed a hit from actor
            Death,      // target died at `position` (slot on its board before removal); amount = 1 if destroyed by
                        // Poisonous or an effect rather than damage; card/golden say what it was
            Summon,     // target is a new minion at `position` (card, golden, attack/amount, health/amount2); actor =
                        // the minion whose effect summoned it, 0 for none
            Reborn,     // target came back from the dead minion `actor` at `position`; amount / amount2 / keywords as
                        // it came back (health 1 plus any aura / Greybough bonus, its Divine Shield)
            Buff,       // target gets +amount attack, +amount2 health and `keywords`
            Trigger,    // actor's effect fires; amount = HookKind
        };

        struct CombatEvent
        {
            EventType type = EventType::Attack;
            int side = 0;
            uint32_t actor = 0;
            uint32_t target = 0;
            int amount = 0;
            int amount2 = 0;
            int card = -1;
            bool golden = false;
            int position = 0;
            uint32_t keywords = 0;
            bool operator==(CombatEvent const& o) const
            {
                return type == o.type && side == o.side && actor == o.actor && target == o.target && amount == o.amount &&
                    amount2 == o.amount2 && card == o.card && golden == o.golden && position == o.position &&
                    keywords == o.keywords;
            }
        };

        // A change an effect wants to keep after the combat; Game applies them (HsBgGame.cpp). `kind` 1 and 2 are the
        // engine's (below); any other kind is free for tests. inputIndex -1 (summoned in combat) is ignored by Game.
        constexpr int kPersistBuff = 1;       // attack / health / keywords added to the minion of the input board
        constexpr int kPersistAddToHand = 2;  // `card` / `golden` is added to the hand of the side's player
        constexpr int kPersistElementalTavernBonus = 3; // `value` x +1/+1 for Elementals in Bob's Tavern
        constexpr int kPersistMakeGolden = 4; // the minion of the input board becomes Golden (Tony Two-Tusk)
        constexpr int kPersistAddPoolToHand = 5; // `card` / `golden` is added from the pool if a copy is left (Y'Shaarj)
        struct PersistentChange
        {
            int side = 0;
            int inputIndex = -1; // slot in the input board, -1 = summoned in combat
            int kind = 0;
            int value = 0;
            int attack = 0;      // kPersistBuff
            int health = 0;
            uint32_t keywords = 0;
            int card = -1;       // kPersistAddToHand / kPersistAddPoolToHand
            bool golden = false;
            bool operator==(PersistentChange const& o) const
            {
                return side == o.side && inputIndex == o.inputIndex && kind == o.kind && value == o.value &&
                    attack == o.attack && health == o.health && keywords == o.keywords && card == o.card && golden == o.golden;
            }
        };

        // One player's side of a combat: the board and the tavern tier (for the damage formula).
        struct CombatSide
        {
            Board board;
            int tavernTier = 1;
            // The player's Secrets (card indexes), in play order. They trigger in this order and the used
            // ones come back in CombatResult::secretsUsed.
            std::vector<int> secrets;
        };

        struct CombatSettings
        {
            bool capDamage = true;                      // apply kDamageCap (true until the first player is eliminated)
            EffectRegistry const* effects = nullptr;    // nullptr = EffectRegistry::Global()
            // Tribes of each side's faction lobby for "random Demon" style pools; 0 = all. The two factions
            // roll their own sets, so a side only sees its own.
            uint32_t lobbyTribes[2] = { 0, 0 };
            bool slice15AOnly = true;                   // pool candidates: playable pool minions only
            // The quest reward of each side (Stolen Gold, Evil Twin, Ritual Dagger, Staff of Origination). nullptr
            // for a ghost side or a player with no reward.
            RewardState const* rewardState[2] = { nullptr, nullptr };
            // Sire Denathrius's second reward (a second Stolen Gold / Evil Twin / Ritual Dagger /
            // Staff of Origination can be active at once).
            RewardState const* rewardState2[2] = { nullptr, nullptr };
            // The hero power of each side that buffs its summons (Millificent: a summoned Mech gets +2 Attack).
            uint32_t summonAttackTribe[2] = { 0, 0 };
            int summonAttack[2] = { 0, 0 };
            // The combat-side hero powers (Deathwing, Al'Akir, Greybough, Rokara, the Avenge heroes, Illidan).
            HeroCombatDef hero[2];
            // The game turn (Tavish's aim doubles every 3 turns).
            int round = 1;
        };

        struct CombatResult
        {
            int winner = -1;        // 0 or 1; -1 = draw (no damage)
            int damage = 0;         // damage to the loser's hero (0 on a draw)
            int firstSide = 0;      // the side that attacked first
            bool timedOut = false;  // ended by kMaxTurns (a draw)
            bool stalled = false;   // ended because no minion on either side could attack (a draw)
            int attacks = 0;        // attacks made
            // The enemy minions each side killed, in the order they died (Rafaam, Ragnaros).
            std::vector<int> kills[2];
            // The Secrets of each side that triggered in this combat, in trigger order (Game removes them).
            std::vector<int> secretsUsed[2];
            std::vector<CombatEvent> events;
            Board start[2];         // the copies as they entered combat, with their combat ids
            Board survivors[2];     // what is alive at the end
            std::vector<PersistentChange> persistent;
        };

        // Damage the winner deals: tavernTier + the tiers of the surviving minions, capped when `cap` is set.
        int CombatDamage(int tavernTier, Board const& survivors, bool cap);

        CombatResult SimulateCombat(CombatSide const& a, CombatSide const& b, Rng& rng, CombatSettings const& settings = CombatSettings());
        // Same, with a fresh Rng(seed): the same seed and boards always give the same result and log.
        CombatResult SimulateCombat(CombatSide const& a, CombatSide const& b, uint32_t seed, CombatSettings const& settings = CombatSettings());
    }
}

#endif
