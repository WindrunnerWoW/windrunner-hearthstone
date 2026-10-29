/*
 * Rules of the Naxxramas quests. Pure C++ (no core headers,
 * only the generated quest data) so the tests can check them.
 *
 *   - Lookups over the generated tables: quest by id, quest by boss encounter / challenge key, boss stand-in by creature
 *     entry or encounter key, and the Boss-kind quest of a stand-in.
 *   - A boss stand-in offers its normal match only while its Boss quest is incomplete (NaxxBossDuelAllowed).
 *   - A match credits its quest only when that quest was incomplete at the start and the player won by killing the hero
 *     (NaxxShouldCredit).
 *
 * The challenge -> encounter mapping lives in match/HsEncounters.h (Challenge::encounterKey), which this header does
 * not include: NaxxChallengesAtEntry takes the mapping as a function, so these rules stay data-only and the glue passes
 * Hs::FindChallenge.
 */

#ifndef MOD_HEARTHSTONE_HS_NAXX_QUEST_RULES_H
#define MOD_HEARTHSTONE_HS_NAXX_QUEST_RULES_H

#include "HsNaxxQuestData.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Hs
{
    inline NaxxQuest const* NaxxQuestById(uint32_t questId)
    {
        for (NaxxQuest const& quest : kNaxxQuests)
            if (quest.questId == questId)
                return &quest;
        return nullptr;
    }

    // A boss encounter key (Boss quests) or a challenge key (Challenge quests). Wing quests have the key "" and are never
    // found, and neither is the empty key.
    inline NaxxQuest const* NaxxQuestByKey(std::string const& key)
    {
        if (key.empty())
            return nullptr;
        for (NaxxQuest const& quest : kNaxxQuests)
            if (key == quest.key)
                return &quest;
        return nullptr;
    }

    inline NaxxBoss const* NaxxBossByEntry(uint32_t entry)
    {
        for (NaxxBoss const& boss : kNaxxBosses)
            if (boss.entry == entry)
                return &boss;
        return nullptr;
    }

    inline NaxxBoss const* NaxxBossByEncounter(std::string const& encounterKey)
    {
        if (encounterKey.empty())
            return nullptr;
        for (NaxxBoss const& boss : kNaxxBosses)
            if (encounterKey == boss.encounterKey)
                return &boss;
        return nullptr;
    }

    // The Boss-kind quest of a stand-in; nullptr when entry is not a stand-in.
    inline NaxxQuest const* NaxxBossQuestOfEntry(uint32_t entry)
    {
        NaxxBoss const* boss = NaxxBossByEntry(entry);
        if (!boss)
            return nullptr;
        NaxxQuest const* quest = NaxxQuestByKey(boss->encounterKey);
        return quest && quest->kind == NaxxKind::Boss ? quest : nullptr;
    }

    // The Challenge-kind quests that are fought at a stand-in, in adventure order. encounterOfChallenge maps a challenge key
    // to the encounter key it is played against ("" for an unknown key).
    inline std::vector<NaxxQuest const*> NaxxChallengesAtEntry(uint32_t entry,
        std::function<std::string(std::string const&)> const& encounterOfChallenge)
    {
        std::vector<NaxxQuest const*> out;
        NaxxBoss const* boss = NaxxBossByEntry(entry);
        if (!boss || !encounterOfChallenge)
            return out;
        for (NaxxQuest const& quest : kNaxxQuests)
            if (quest.kind == NaxxKind::Challenge && encounterOfChallenge(quest.key) == boss->encounterKey)
                out.push_back(&quest);
        return out;
    }

    // "DRUID" -> "Druid": the class of a challenge for gossip and match history text.
    inline std::string NaxxClassLabel(std::string const& heroClass)
    {
        std::string out = heroClass;
        for (std::size_t i = 0; i < out.size(); ++i)
        {
            char& c = out[i];
            if (i == 0 && c >= 'a' && c <= 'z')
                c = static_cast<char>(c - 'a' + 'A');
            else if (i > 0 && c >= 'A' && c <= 'Z')
                c = static_cast<char>(c - 'A' + 'a');
        }
        return out;
    }

    // The normal boss match: only while the character has the Boss quest accepted and not finished.
    constexpr bool NaxxBossDuelAllowed(bool bossQuestIncomplete)
    {
        return bossQuestIncomplete;
    }

    // A stand-in is seen only by the characters it is currently for: the ones with its Boss quest open, or with a class
    // challenge fought against it open. For everyone else it is not there, so a quarter shows only the boss being studied.
    constexpr bool NaxxStandInVisible(bool bossQuestIncomplete, bool challengeIncomplete)
    {
        return bossQuestIncomplete || challengeIncomplete;
    }

    // Credit for the quest this match can credit: it has to have been incomplete when the match started, and the match has
    // to be a win by killing the hero (not a concession, interruption or draw).
    inline bool NaxxShouldCredit(bool questIncompleteAtStart, std::string const& res, std::string const& why)
    {
        return questIncompleteAtStart && res == "win" && why == "hero";
    }
}

#endif
