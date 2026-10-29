/*
 * Boss encounters ("Boss recreation"): the 15 Normal-mode bosses of the Curse of Naxxramas adventure as
 * data. One Encounter says everything an opponent row cannot: the boss hero, health, armor and hero power, the exact
 * original deck, what is on each board when the match starts, a weapon, cards in the human's hand, who goes first and
 * (Kel'Thuzad) the second phase.
 *
 * PURE C++ (standard library and the match headers), tested headless in tests/match_encounter_tests.cpp.
 *
 * How an encounter is chosen: a hearthstone_opponent row with difficulty `boss` whose deck_key is an encounter key
 * ("naxx_anubrekhan" ...) instead of a library deck. OpponentRegistry::Build recognises the key, checks the encounter
 * with CheckEncounter (requireSupported = true) and skips the row with the first problem when it is not ready; the
 * row's hero_health / hero_power / hero_card / hero_armor must be NULL (the encounter owns them). The picked
 * OpponentPick carries encounterKey, and the match start glue calls ApplyEncounter on the MatchStartParams. The human
 * keeps their own deck and class; the encounter only changes their start state (a board, extra hand cards) where the
 * original did.
 *
 * Encounter decks are not library decks: they are not checked against class, the copy limit or "collectible", only
 * against the encounter itself (every id exists in the CardDb and is a minion, spell or weapon, 30 cards or, for a side
 * that never draws, none) so the boss-only cards (acquisition `none`) and the 2-per-card original lists are fine.
 *
 * "Ready": every card, the hero power and the start cards are Supported() by the match engine. Since steps E2a / E2b all
 * 15 encounters are ready; CheckEncounter says which card is the first one missing if one is not, and EncounterUnsupported
 * lists all of them.
 */

#ifndef MOD_HEARTHSTONE_HS_ENCOUNTERS_H
#define MOD_HEARTHSTONE_HS_ENCOUNTERS_H

#include "HsMatch.h"
#include "HsMatchService.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Hs
{
    // Who plays first. Human / Boss force MatchSetup::firstSide; the other side gets The Coin as always.
    enum class EncounterFirst : uint8_t { Random, Human, Boss };

    struct EncounterCard
    {
        std::string id;
        int count = 1;
    };

    // Kel'Thuzad's second phase. The rules live in the script of his phase 1 hero power (Script::phase of Frost Blast, set
    // up in HsMatchCardsNaxx.cpp); this is the same data for the definition, and CheckEncounter(requireSupported) demands
    // that both say the same. Phase 1: the hero power Frost Blast (`Encounter::heroPowerId`) and 10 Armor. Phase 2 starts
    // the moment that Armor is gone (even in the middle of the player's turn, which then ends) or, if it still stands,
    // when the boss's forcedTurn-th own turn starts: the hero power becomes `heroPowerId`, and `summons` are put onto the
    // boss's board.
    struct EncounterPhase2
    {
        bool present = false;
        std::string heroPowerId;           // Chains
        std::vector<std::string> summons;  // 2 Guardians of Icecrown
        int forcedTurn = 0;                // the boss's own turn number that forces the phase (11)
        bool onArmorGone = true;
    };

    struct Encounter
    {
        std::string key;        // hearthstone_opponent.deck_key, [0-9A-Za-z_]{1,32}
        std::string name;       // the boss, for logs and tests
        std::string heroClass;  // the engine needs a class for the hero; the hero card and power below replace its look and power
        std::string heroCardId; // the boss hero
        int heroHealth = 0;     // the boss's hero health (1..200)
        int heroArmor = 0;      // the boss's starting armor
        std::string heroPowerId;
        std::vector<EncounterCard> deck;  // the boss's deck: counts as in the original lists; empty only with boss.noDraw
        Match::SideStart boss;            // the boss's start board / weapon / hand / noDraw
        Match::SideStart human;           // the human's start state (board, extra hand cards); never a deck
        EncounterFirst first = EncounterFirst::Random;
        EncounterPhase2 phase2;
    };

    // The 15 Normal bosses in adventure order (wing, then boss). Immutable.
    std::vector<Encounter> const& Encounters();
    // nullptr when key is not an encounter key.
    Encounter const* FindEncounter(std::string const& key);

    // One entry per copy, sorted by card id (the same shape as a library deck's ids; the match shuffles with its seed).
    std::vector<std::string> ExpandEncounterDeck(Encounter const& e);

    // "" when the encounter is consistent, else the first problem. Rules, in order: hero class and card (a Hero),
    // health 1..200, armor 0..200, hero power (a HeroPower), the deck (known ids, minion / spell / weapon cards, no id
    // listed twice, counts 1..60, 30 cards in total, or none for a boss that never draws), the start cards of both sides
    // (known; minions on a board, a weapon, a minion / spell / weapon in a hand; board <= kMaxBoard), phase 2 (known
    // hero power and summons). requireSupported also demands CardDb::Supported for the deck, the start cards, the hero
    // power and the phase 2 cards, and that the boss's hero power script starts exactly this phase 2. false = data checks only.
    std::string CheckEncounter(Match::CardDb const& db, Encounter const& e, bool requireSupported);
    bool EncounterReady(Match::CardDb const& db, Encounter const& e);
    // Every distinct card id of the encounter (deck, start cards, hero power, phase 2) that is known but not Supported(),
    // sorted. These are the cards still to script before the encounter can be played.
    std::vector<std::string> EncounterUnsupported(Match::CardDb const& db, Encounter const& e);

    // Fills params for a match against this boss: the opponent's class, deck, hero overrides, start state, the human's
    // start state and the forced first side. The human's name, class and deck, the opponent's name, the seed and the
    // meta stay as they are; difficulty becomes "boss" with the Hard AI.
    void ApplyEncounter(Encounter const& e, MatchStartParams& params);

    // Class challenges (Naxxramas): the nine class decks of the original adventure, each played against the
    // Normal setup of one boss (`encounterKey`). LOANER RULE: the human borrows the challenge's class and 30-card deck for
    // that one match only, like Draft Arena's borrowed cards. It adds nothing to the collection, grants no class access
    // and is not a saved deck; the player's own deck and collection are not checked (CheckChallenge checks the
    // challenge itself, never the player). Only the challenge's reward card is granted later, by the quest step.
    struct Challenge
    {
        std::string key;                 // naxx_challenge_<class>, [0-9A-Za-z_]{1,32}
        std::string heroClass;           // the loaner class: MAGE ... (the engine's codes)
        std::vector<std::string> deck;   // 30 ids, one entry per copy, sorted (the same shape as a library deck)
        std::string encounterKey;        // the opponent: the boss's Normal encounter, played unchanged
        std::string rewardCardId;        // the class card granted by the quest (not by playing the challenge)
    };

    // The 9 challenges in adventure order (wing, then class). Immutable.
    std::vector<Challenge> const& Challenges();
    // nullptr when key is not a challenge key.
    Challenge const* FindChallenge(std::string const& key);

    // "" when the challenge is consistent, else the first problem. Rules: key, heroClass (one of the nine), the deck (exactly
    // 30 known minion / spell / weapon cards; with requireSupported every card is Supported()), the encounterKey (an
    // existing encounter) and the rewardCardId (a known card). The player's collection, copy limits and class access are
    // deliberately not part of it.
    std::string CheckChallenge(Match::CardDb const& db, Challenge const& c, bool requireSupported);
    // CheckChallenge(db, c, true) is empty and the encounter it fights is ready.
    bool ChallengeReady(Match::CardDb const& db, Challenge const& c);

    // Fills the human side of params with the loaner class and deck of the challenge (the loaner rule above). Call it
    // together with ApplyEncounter(FindEncounter(c.encounterKey)) for a challenge match; the opponent is not touched here.
    // The human's name, the seed and the meta stay as they are.
    void ApplyChallenge(Challenge const& c, MatchStartParams& params);
}

#endif
