/*
 * Draft arena AI drafter: picks one card of a three-card offer for an AI opponent and drafts a
 * whole 30-card deck with the same offer rules a player gets (ArenaOffer).
 *
 * Score of an offered card = card value + curve need + synergies + a small random tie-break (numbers in the .cpp).
 * Card values come from the generated table HsArenaDraftScores.inc (tools/arena/gen_draft_scores.py); a card that
 * table never saw gets a fallback value from its mana cost, stats and rarity.
 *
 * PURE C++ (HsArenaRules and the match engine's CardDb, no core headers). Randomness is the injected PackRng, so a
 * seeded generator reproduces a draft exactly.
 */

#ifndef MOD_HEARTHSTONE_HS_ARENA_DRAFT_AI_H
#define MOD_HEARTHSTONE_HS_ARENA_DRAFT_AI_H

#include "HsArenaRules.h"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace Hs
{
    // Mana cost buckets of the curve: 0..1, 2, 3, 4, 5, 6, 7 or more.
    constexpr std::size_t kArenaCurveBuckets = 7;
    std::size_t ArenaCurveBucket(int cost);
    // Wanted cards per bucket in a deck of 30 (sums to kArenaDraftPicks).
    std::array<uint32_t, kArenaCurveBuckets> const& ArenaCurveTarget();

    // The score the drafter gives card `id` for a deck of deckClass that already holds picksSoFar (higher is
    // better, no random part). An id the catalog or the CardDb does not know scores -1000.
    double ArenaDraftScore(std::string const& deckClass, std::string const& id,
                           std::vector<std::string> const& picksSoFar, CardCatalog const& catalog,
                           Match::CardDb const& db);

    // The id of the offered card the drafter takes: the highest ArenaDraftScore plus a random tie-break of less
    // than kArenaDraftJitter (one rng draw per offered card, in offer order). Empty string for an empty offer.
    constexpr double kArenaDraftJitter = 2.0;
    std::string ArenaDraftChoose(std::string const& deckClass, std::vector<std::string> const& offer,
                                 std::vector<std::string> const& picksSoFar, CardCatalog const& catalog,
                                 Match::CardDb const& db, PackRng const& rng);

    // A whole draft: kArenaDraftPicks offers (ArenaOffer from pool by weights) and one choice each, in pick order.
    // Empty when the pool cannot make an offer (fewer than three cards).
    std::vector<std::string> ArenaDraftDeck(std::string const& deckClass, ArenaPool const& pool,
                                            std::array<uint32_t, kPackRarityCount> const& weights,
                                            CardCatalog const& catalog, Match::CardDb const& db, PackRng const& rng);

    // The class of a new AI opponent: one of the nine, uniformly.
    std::string ArenaRandomClass(PackRng const& rng);
}

#endif
