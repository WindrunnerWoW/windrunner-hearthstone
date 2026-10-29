// The Curse of Naxxramas quest step (card names from data/cards/cards.normalized.json).
// The ids are those of data/sql/world/0045_hearthstone_naxx_quests.sql.
#ifndef MOD_HEARTHSTONE_HS_NAXX_QUEST_DATA_H
#define MOD_HEARTHSTONE_HS_NAXX_QUEST_DATA_H

#include <cstdint>

namespace Hs
{
    enum class NaxxKind : uint8_t { Boss, Wing, Challenge };

    struct NaxxItem   { uint32_t entry; char const* cardId; uint32_t count; bool replayPack; };
    struct NaxxQuest  { uint32_t row; uint32_t questId; NaxxKind kind; uint32_t quarterNpc; char const* key; uint32_t prevQuestId; NaxxItem items[2]; uint32_t itemCount; };
    struct NaxxBoss   { uint32_t entry; char const* encounterKey; char const* name; };

    inline constexpr uint32_t kNaxxBreadcrumbQuest = 919932;
    inline constexpr uint32_t kNaxxBreadcrumbGivers[2] = { 919501, 919502 };
    inline constexpr uint32_t kNaxxBreadcrumbEnder = 16112;
    inline constexpr uint32_t kNaxxMinArenaWins = 6;

    // Row order of the adventure. The unused second item is { 0, "", 0, false }, the item count is items used.
    inline constexpr NaxxQuest kNaxxQuests[29] = {
        { 1, 919933, NaxxKind::Boss, 16112, "naxx_anubrekhan", 919932, { { 919652, "FP1_002", 2, false }, { 0, "", 0, false } }, 1 },
        { 2, 919934, NaxxKind::Boss, 16112, "naxx_faerlina", 919933, { { 919653, "FP1_017", 2, false }, { 0, "", 0, false } }, 1 },
        { 3, 919935, NaxxKind::Boss, 16112, "naxx_maexxna", 919934, { { 919654, "FP1_007", 2, false }, { 0, "", 0, false } }, 1 },
        { 4, 919936, NaxxKind::Wing, 16112, "", 919935, { { 919655, "FP1_010", 1, true }, { 0, "", 0, false } }, 1 },
        { 5, 919937, NaxxKind::Challenge, 16112, "naxx_challenge_druid", 919936, { { 919656, "FP1_019", 2, false }, { 0, "", 0, false } }, 1 },
        { 6, 919938, NaxxKind::Challenge, 16112, "naxx_challenge_rogue", 919936, { { 919657, "FP1_026", 2, false }, { 0, "", 0, false } }, 1 },
        { 7, 919939, NaxxKind::Boss, 16113, "naxx_noth", 919936, { { 919658, "FP1_027", 2, false }, { 0, "", 0, false } }, 1 },
        { 8, 919940, NaxxKind::Boss, 16113, "naxx_heigan", 919939, { { 919659, "FP1_024", 2, false }, { 0, "", 0, false } }, 1 },
        { 9, 919941, NaxxKind::Boss, 16113, "naxx_loatheb", 919940, { { 919660, "FP1_012", 2, false }, { 0, "", 0, false } }, 1 },
        { 10, 919942, NaxxKind::Wing, 16113, "", 919941, { { 919661, "FP1_030", 1, true }, { 0, "", 0, false } }, 1 },
        { 11, 919943, NaxxKind::Challenge, 16113, "naxx_challenge_mage", 919942, { { 919662, "FP1_018", 2, false }, { 0, "", 0, false } }, 1 },
        { 12, 919944, NaxxKind::Challenge, 16113, "naxx_challenge_hunter", 919942, { { 919663, "FP1_011", 2, false }, { 0, "", 0, false } }, 1 },
        { 13, 919945, NaxxKind::Boss, 16115, "naxx_razuvious", 919942, { { 919664, "FP1_029", 2, false }, { 0, "", 0, false } }, 1 },
        { 14, 919946, NaxxKind::Boss, 16115, "naxx_gothik", 919945, { { 919665, "FP1_008", 2, false }, { 0, "", 0, false } }, 1 },
        { 15, 919947, NaxxKind::Boss, 16115, "naxx_horsemen", 919946, { { 919666, "FP1_009", 2, false }, { 0, "", 0, false } }, 1 },
        { 16, 919948, NaxxKind::Wing, 16115, "", 919947, { { 919667, "FP1_031", 1, true }, { 0, "", 0, false } }, 1 },
        { 17, 919949, NaxxKind::Challenge, 16115, "naxx_challenge_shaman", 919948, { { 919668, "FP1_025", 2, false }, { 0, "", 0, false } }, 1 },
        { 18, 919950, NaxxKind::Challenge, 16115, "naxx_challenge_warlock", 919948, { { 919669, "FP1_022", 2, false }, { 0, "", 0, false } }, 1 },
        { 19, 919951, NaxxKind::Boss, 16116, "naxx_patchwerk", 919948, { { 919670, "FP1_028", 2, false }, { 0, "", 0, false } }, 1 },
        { 20, 919952, NaxxKind::Boss, 16116, "naxx_grobbulus", 919951, { { 919671, "FP1_004", 2, false }, { 0, "", 0, false } }, 1 },
        { 21, 919953, NaxxKind::Boss, 16116, "naxx_gluth", 919952, { { 919672, "FP1_001", 2, false }, { 0, "", 0, false } }, 1 },
        { 22, 919954, NaxxKind::Boss, 16116, "naxx_thaddius", 919953, { { 919673, "FP1_016", 2, false }, { 0, "", 0, false } }, 1 },
        { 23, 919955, NaxxKind::Wing, 16116, "", 919954, { { 919674, "FP1_014", 1, true }, { 919681, "FP1_015", 1, false } }, 2 },
        { 24, 919956, NaxxKind::Challenge, 16116, "naxx_challenge_warrior", 919955, { { 919675, "FP1_021", 2, false }, { 0, "", 0, false } }, 1 },
        { 25, 919957, NaxxKind::Challenge, 16116, "naxx_challenge_priest", 919955, { { 919676, "FP1_023", 2, false }, { 0, "", 0, false } }, 1 },
        { 26, 919958, NaxxKind::Boss, 11034, "naxx_sapphiron", 919955, { { 919677, "FP1_003", 2, false }, { 0, "", 0, false } }, 1 },
        { 27, 919959, NaxxKind::Boss, 11034, "naxx_kelthuzad", 919958, { { 919678, "FP1_005", 2, false }, { 0, "", 0, false } }, 1 },
        { 28, 919960, NaxxKind::Wing, 11034, "", 919959, { { 919679, "FP1_013", 1, true }, { 0, "", 0, false } }, 1 },
        { 29, 919961, NaxxKind::Challenge, 11034, "naxx_challenge_paladin", 919960, { { 919680, "FP1_020", 2, false }, { 0, "", 0, false } }, 1 },
    };

    inline constexpr NaxxBoss kNaxxBosses[15] = {
        { 920000, "naxx_anubrekhan", "Anub'Rekhan" },
        { 920001, "naxx_faerlina", "Grand Widow Faerlina" },
        { 920002, "naxx_maexxna", "Maexxna" },
        { 920003, "naxx_noth", "Noth the Plaguebringer" },
        { 920004, "naxx_heigan", "Heigan the Unclean" },
        { 920005, "naxx_loatheb", "Loatheb" },
        { 920006, "naxx_razuvious", "Instructor Razuvious" },
        { 920007, "naxx_gothik", "Gothik the Harvester" },
        { 920008, "naxx_horsemen", "Baron Rivendare" },
        { 920009, "naxx_patchwerk", "Patchwerk" },
        { 920010, "naxx_grobbulus", "Grobbulus" },
        { 920011, "naxx_gluth", "Gluth" },
        { 920012, "naxx_thaddius", "Thaddius" },
        { 920013, "naxx_sapphiron", "Sapphiron" },
        { 920014, "naxx_kelthuzad", "Kel'Thuzad" },
    };
}

#endif
