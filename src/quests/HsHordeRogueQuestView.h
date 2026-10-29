#ifndef MOD_HEARTHSTONE_HORDE_ROGUE_QUEST_VIEW_H
#define MOD_HEARTHSTONE_HORDE_ROGUE_QUEST_VIEW_H
#include "HsHordeRogueRules.h"
#include <string>
#include <vector>

// Native 1.12 SMSG_QUEST_QUERY_RESPONSE. Hide display requirements only; never alter
// the shared quest template, saved objective slots, or server turn-in requirements.
inline bool HsHordeRogueQuestView(std::vector<uint8_t> const& native, uint32_t stage, std::vector<uint8_t>& out)
{
    constexpr std::size_t header = 156; // 15 uint32, 4 reward pairs, 6 choice pairs, 4 point fields
    if (stage > 4 || native.size() < header + 4 + 64 + 4) return false;
    uint32_t const id = uint32_t(native[0]) | uint32_t(native[1]) << 8 |
        uint32_t(native[2]) << 16 | uint32_t(native[3]) << 24;
    if (id != kHsHordeRogueInvestigation) return false;
    std::size_t cursor = header;
    auto readString = [&native, &cursor](std::string& value)
    {
        std::size_t const start = cursor;
        while (cursor < native.size() && native[cursor]) ++cursor;
        if (cursor == native.size()) return false;
        value.assign(native.begin() + start, native.begin() + cursor);
        ++cursor;
        return true;
    };
    std::array<std::string, 4> text;
    for (auto& value : text) if (!readString(value)) return false;
    if (native.size() - cursor < 64) return false;
    std::size_t const goals = cursor;
    cursor += 64;
    std::array<std::string, 4> objectives;
    for (auto& value : objectives) if (!readString(value)) return false;
    if (cursor != native.size()) return false;

    static char const* const summaries[] = {
        "Talk to Rikka about the missing deck.", "Talk to Gora about the missing deck.",
        "Talk to Zikko about the missing deck.", "Talk to Sable about the missing deck.",
        "Return to For'sen and report what you learned." };
    std::vector<uint8_t> view(native.begin(), native.begin() + header);
    auto appendString = [&view](std::string const& value)
    {
        view.insert(view.end(), value.begin(), value.end());
        view.push_back(0);
    };
    text[1] = summaries[stage];
    for (auto const& value : text) appendString(value);
    for (std::size_t slot = 0; slot < 4; ++slot)
    {
        // Keep the original slot: the client's packed native counters use these indices.
        for (std::size_t byte = 0; byte < 16; ++byte)
        {
            bool const visible = byte < 8 ? stage == slot : stage == 4 && slot == 0;
            view.push_back(visible ? native[goals + slot * 16 + byte] : 0);
        }
    }
    for (std::size_t slot = 0; slot < 4; ++slot)
        appendString(stage == slot ? objectives[slot] : std::string());
    out = std::move(view);
    return true;
}
#endif
