/*
 * World NPC Ladder: the three offers of the ladder chair.
 *
 * PURE C++ (standard library and the ladder rules, no core headers, no database, no clock, no global RNG). The glue
 * keeps one OfferSet per account (character table hearthstone_ladder_offer, see HsLadderRows.h), calls EnsureOffers
 * when a player sits down and OffersAfterMatch when a ladder match against one of the offers ended.
 *
 * Which NPCs: only enabled NPCs of the ladder that are not blocked (the world table hearthstone_ladder_block) and
 * not the account's last ladder opponent (unless no other NPC is left). The candidates stand near the player in
 * the ladder order of the NPCs (the order of LadderOrder with the player entries left out): a window of kOfferWindow
 * positions above and kOfferWindow below the player's place, widened by one position on each side until it holds at
 * least `count` candidates (or the whole ladder). A player without an entry counts as rank 15, 0 stars, rating
 * 1000.00 and takes the place where that would sort.
 *
 * Which of the candidates: picked one by one, deterministically from the seed. Each pick prefers (1) a class not
 * offered yet, then (2) from the second pick on, a faction not offered yet (alliance and horde; "any" counts as
 * neither); among the candidates of the best tier a seeded draw chooses, nearer positions being likelier (weight =
 * farthest distance - own distance + 1). The offers come back in ladder order, best first. Fewer candidates than
 * `count`: all of them.
 *
 * Seed: Mix of the ladder seed, the account and a per-account draw counter. The counter is stored, so a reroll
 * (counter + 1) gives new offers and the same history of draws gives the same offers on every start. (A reroll that
 * would hand out the very same set draws again, with the next counter, up to 8 times.)
 */

#ifndef MOD_HEARTHSTONE_HS_LADDER_OFFERS_H
#define MOD_HEARTHSTONE_HS_LADDER_OFFERS_H

#include "HsLadderRules.h"

#include <cstdint>
#include <set>
#include <vector>

namespace Hs
{
namespace Ladder
{
    constexpr int kOfferCount = 3;  // offers per set
    constexpr int kOfferWindow = 5; // positions above and below the player before the window widens

    // What is stored per account (hearthstone_ladder_offer and hearthstone_ladder_offer_state).
    struct OfferSet
    {
        uint32_t account = 0;
        std::vector<EntrantId> slots;  // NPC ids in slot order (ladder order when drawn), at most kOfferCount;
                                       // empty: nothing drawn yet, or the last match cleared the set
        uint32_t round = 0;            // state.roundsPlayed when the slots were drawn
        uint32_t drawCounter = 0;      // draws made for the account so far (the next draw uses counter + 1)
        EntrantId lastOpponent = 0;    // the NPC of the account's last ladder match (0 = none)
    };

    // The seed of the draw with that number.
    uint32_t OfferSeed(uint32_t ladderSeed, uint32_t account, uint32_t drawCounter);

    // The NPC ids offered to playerId (PlayerId(account)); see the file comment. Pure: the same inputs give the same
    // offers. blocked holds NPC ids (NpcId(creature entry)); lastOpponent 0 = none.
    std::vector<EntrantId> DrawOffers(LadderState const& state, EntrantId playerId, EntrantId lastOpponent, uint32_t seed,
                                      std::set<EntrantId> const& blocked, std::size_t count = kOfferCount);

    // True when the stored set can still be shown: not empty, no repeated slot, and every NPC exists, is enabled
    // and is not blocked. (The last opponent is not checked: it was left out when the set was drawn.)
    bool OfferSetValid(OfferSet const& set, LadderState const& state, std::set<EntrantId> const& blocked);

    // A fresh draw: draw counter + 1, slots from OfferSeed(state.seed, account, counter) and the set's last
    // opponent, round = state.roundsPlayed. Sets set.account. When the set held slots and the draw gives the same
    // ones, it draws again with the next counter (at most 8 draws in all), so a reroll shows other NPCs whenever the
    // ladder has other NPCs to show.
    void RedrawOffers(OfferSet& set, LadderState const& state, uint32_t account, std::set<EntrantId> const& blocked,
                      std::size_t count = kOfferCount);

    // Redraws the set when it is not valid (see OfferSetValid). True: the set changed and has to be saved. A set of
    // another account is dropped first (a fresh set for `account`).
    bool EnsureOffers(OfferSet& set, LadderState const& state, uint32_t account, std::set<EntrantId> const& blocked,
                      std::size_t count = kOfferCount);

    // A ladder match against npc ended (any outcome that counted): remembers the NPC as the last opponent and clears
    // the slots, so the next EnsureOffers draws from the ladder after the NPC round that follows the match.
    void OffersAfterMatch(OfferSet& set, EntrantId npc);
}
}

#endif
