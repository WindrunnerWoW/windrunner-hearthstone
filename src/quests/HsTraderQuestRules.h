#ifndef MOD_HEARTHSTONE_HS_TRADER_QUEST_RULES_H
#define MOD_HEARTHSTONE_HS_TRADER_QUEST_RULES_H

#include <array>
#include <cstdint>

// Quests whose objective item is bought from a trader. The client draws that objective line itself when
// the item is bought, so the core's SMSG_QUESTUPDATE_ADD_ITEM push would print the same line twice.
struct HsTraderQuestItem
{
    uint32_t quest;
    uint32_t item;
};
constexpr std::array<HsTraderQuestItem, 8> kHsTraderQuestItems = {{
    { 919722, 2596 },   // An Army Marches on Its Stomach: Skin of Dwarven Stout
    { 919722, 117 },    // ... and Tough Jerky
    { 919742, 919631 }, // A Combobulated Family Affair: Magical Card Combobulator
    { 919772, 919636 }, // Possession Is Nine-Tenths: Recovered Card Deck
    { 919811, 919645 }, // Spirited Research: Ikae Earth Totem Kit
    { 919811, 2595 },   // ... and Jug of Bourbon
    { 919824, 159 },    // Something I Can Give: Refreshing Spring Water
    { 919842, 2593 },   // A Small Consideration: Flask of Port
}};

// The quest that owns this purchased objective item, or 0 when it is not a trader objective.
inline uint32_t HsTraderQuestForItem(uint32_t item)
{
    for (HsTraderQuestItem const& entry : kHsTraderQuestItems)
        if (entry.item == item)
            return entry.quest;
    return 0;
}

#endif
