/*
 * Rules of the Draft Arena introduction quest ("Access"). Pure C++ (no core
 * headers) so the tests can check them.
 *
 *   - The quest is offered at Ezra Chatterton (Horde) / Ibelin Redmoore (Alliance) only while the ACCOUNT has all nine
 *     classes unlocked and owns at least kArenaIntroMinCards different collectible cards (82 of them come from the
 *     nine starter decks).
 *   - Accepting it lets the character start ONE free run at the host. The quest completes when the free run ends,
 *     whatever its result (the normal prize table applies through settlement).
 *   - Paid entry is available only after the quest was turned in, by any character of the account.
 */

#ifndef MOD_HEARTHSTONE_HS_ARENA_INTRO_RULES_H
#define MOD_HEARTHSTONE_HS_ARENA_INTRO_RULES_H

#include <cstdint>

namespace Hs
{
    constexpr uint32_t kArenaIntroQuestHorde = 919921;    // "Nothing but the Draw" (Ezra Chatterton 919501)
    constexpr uint32_t kArenaIntroQuestAlliance = 919931; // "The Luck of the Draw" (Ibelin Redmoore 919502)
    constexpr uint32_t kArenaIntroClasses = 9;            // every class unlocked for the account
    constexpr uint32_t kArenaIntroMinCards = 140;         // different collectible cards owned by the account

    // Whether the account may be offered the quest.
    constexpr bool ArenaIntroGate(uint32_t unlockedClasses, uint32_t distinctCards)
    {
        return unlockedClasses >= kArenaIntroClasses && distinctCards >= kArenaIntroMinCards;
    }

    // The newest run of the account as the quest sees it.
    struct ArenaIntroRunFacts
    {
        bool exists = false;
        bool free = false;  // the run was a free one (pay = free)
        bool open = false;  // the run is not over yet (not settled, or a special-card choice is pending)
        bool ended = false; // finished, retired or settled
    };

    // The free run of the quest may start: no run is open and the account has not had a free run (the newest run
    // would be that one: a paid run only exists after the quest was rewarded).
    constexpr bool ArenaIntroFreeRunAllowed(ArenaIntroRunFacts const& run)
    {
        return !run.exists || (!run.open && !run.free);
    }

    // The quest of a character that holds it completes: the account's free run has ended.
    constexpr bool ArenaIntroShouldComplete(ArenaIntroRunFacts const& run)
    {
        return run.exists && run.free && run.ended;
    }

    // The quest id of the host: Ezra 919501 gives the Horde one, Ibelin 919502 the Alliance one, others 0.
    constexpr uint32_t ArenaIntroQuestOfHost(uint32_t creatureEntry)
    {
        return creatureEntry == 919501 ? kArenaIntroQuestHorde : creatureEntry == 919502 ? kArenaIntroQuestAlliance : 0;
    }
    constexpr bool IsArenaIntroQuest(uint32_t questId)
    {
        return questId == kArenaIntroQuestHorde || questId == kArenaIntroQuestAlliance;
    }
}

#endif
