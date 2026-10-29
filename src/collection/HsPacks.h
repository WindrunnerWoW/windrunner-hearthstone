/*
 * Booster packs: the pack pool, the rolls with their guarantees, pack stock and
 * openings, and the PACKS / PACK_OPENED / PACK_CHANGED payload encoders
 * (implementation contract, docs/PROTOCOL.md "Packs").
 *
 * PURE C++ (standard library only, no core headers) so it can be unit-tested
 * headlessly. The database glue lives in src/HsCollectionStore.*.
 */

#ifndef MOD_HEARTHSTONE_HS_PACKS_H
#define MOD_HEARTHSTONE_HS_PACKS_H

#include "HsOwnership.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Hs
{
    constexpr std::size_t kPackSize = 5;
    // The only pack type of the initial release: the combined Basic + Classic booster.
    constexpr char const* kPackTypeClassic = "classic";
    // Unopened packs an account may hold, and packs one GM grant or remove may move.
    constexpr uint32_t kMaxPackStock = 100000;
    constexpr uint32_t kMaxPacksPerOp = 100;
    // Openings listed in PACKS.recent (newest first).
    constexpr std::size_t kRecentOpenings = 5;
    // PACK_OPEN key: [0-9a-z], 12..16 characters.
    constexpr std::size_t kMinPackKeyLen = 12;
    constexpr std::size_t kMaxPackKeyLen = 16;

    // Rarity buckets of a pack slot. Basic (FREE) cards are in Common.
    enum class PackRarity { Common = 0, Rare = 1, Epic = 2, Legendary = 3 };
    constexpr std::size_t kPackRarityCount = 4;
    char const* PackRarityName(PackRarity r); // "Common", "Rare", ...
    // Bucket of a card rarity; false for Rarity::None.
    bool PackRarityOf(Rarity r, PackRarity& out);

    // Relative per-card weights (tenths of a percent by default) and the
    // Legendary bad-luck protection. Configurable, community estimates, not official.
    struct PackOdds
    {
        std::array<uint32_t, kPackRarityCount> weight = { { 716, 228, 44, 11 } };
        // The pack opened after legendaryPity - 1 packs in a row without a
        // Legendary contains one. 0 turns the guarantee off.
        uint32_t legendaryPity = 40;
    };
    // Clamps every weight to 0..1000000 and the pity to 0..1000. All weights 0: defaults.
    PackOdds SanitizePackOdds(PackOdds odds);

    // The cards a pack can contain: collectible, acquisition standard, with a
    // known rarity. Each bucket is sorted by id so seeded rolls are reproducible.
    class PackPool
    {
    public:
        static PackPool FromCatalog(CardCatalog const& catalog);
        std::vector<std::string> const& Cards(PackRarity r) const { return m_cards[static_cast<std::size_t>(r)]; }
        // True when every bucket has at least one card (required to roll).
        bool Complete() const;
        std::size_t Size() const;

    private:
        std::array<std::vector<std::string>, kPackRarityCount> m_cards;
    };

    // Uniform integer in [0, bound); bound is at least 1.
    using PackRng = std::function<uint32_t(uint32_t bound)>;

    struct PackRoll
    {
        std::array<std::string, kPackSize> cards;
        std::array<PackRarity, kPackSize> rarities {};
        bool pityApplied = false;  // a slot was turned into a Legendary by the bad-luck protection
        bool rareUpgraded = false; // all five rolled Common: the last slot became a Rare
        bool HasLegendary() const;
    };

    // Rolls one pack. sinceLegendary: packs opened in a row without a Legendary
    // before this one. Each slot rolls a bucket by weight, then a card uniformly
    // within it. Then the bad-luck protection turns the first lowest-rarity slot
    // into a Legendary, and if all five are Common the last slot becomes a Rare.
    // False (out untouched) if the pool is incomplete.
    bool RollPack(PackPool const& pool, PackOdds const& odds, uint32_t sinceLegendary, PackRng const& rng,
                  PackRoll& out);
    // The bad-luck counter after opening roll.
    uint32_t SinceLegendaryAfter(PackRoll const& roll, uint32_t before);

    // Unopened packs and the bad-luck counter of one account and pack type.
    struct PackState
    {
        uint32_t stock = 0;
        uint32_t opened = 0;         // packs ever opened = seq of the newest opening
        uint32_t sinceLegendary = 0; // packs opened in a row without a Legendary
        bool operator==(PackState const& o) const
        {
            return stock == o.stock && opened == o.opened && sinceLegendary == o.sinceLegendary;
        }
        bool operator!=(PackState const& o) const { return !(*this == o); }
    };

    // One committed opening (hearthstone_pack_opening row).
    struct PackOpening
    {
        uint32_t seq = 0;            // per account and pack type: 1, 2, ...
        std::string key;             // the client's idempotency key
        std::array<std::string, kPackSize> cards;
        std::array<bool, kPackSize> isNew {};
        uint32_t collectionVersion = 0; // collection version after the opening
        uint32_t stockAfter = 0;
        uint32_t sinceLegendaryBefore = 0;
        uint32_t sinceLegendaryAfter = 0;
        bool pityApplied = false;

        std::string NewMask() const; // "10010"
        bool SetNewMask(std::string const& mask); // false unless exactly five 0/1 digits
        std::string CardList(char sep) const; // ids joined by sep
        bool SetCardList(std::string const& list, char sep); // false unless exactly five valid ids
    };

    // [0-9a-z]{12,16}
    bool IsValidPackKey(std::string const& key);

    // Slot i is new if the collection held no copy of cards[i] before and no earlier slot has it.
    std::array<bool, kPackSize> NewFlags(Collection const& before, std::array<std::string, kPackSize> const& cards);

    // PACKS: type=classic;stock=<n>;opened=<m>;recent=<seq>.<mask>.<id1>...<id5>,...
    std::string EncodePacksPayload(PackState const& state, std::vector<PackOpening> const& recent);
    // PACK_OPENED: id=;key=;replay=;stock=;ver=;new=;cards=
    std::string EncodePackOpenedPayload(PackOpening const& opening, bool replay, uint32_t stockNow);
    // PACK_CHANGED push: stock=<n>
    std::string EncodePackChangedPayload(uint32_t stock);

    // Statistics of a pack simulation (starts with a counter of 0).
    struct PackSimStats
    {
        uint32_t packs = 0;
        std::array<uint64_t, kPackRarityCount> cards {};
        uint32_t packsWithLegendary = 0;
        uint32_t pityApplied = 0;
        uint32_t rareUpgraded = 0;
        uint32_t longestWithoutLegendary = 0; // most packs in a row without a Legendary
        uint32_t invalidPacks = 0;            // not five cards, or no Rare or better (must stay 0)
    };
    PackSimStats SimulatePacks(PackPool const& pool, PackOdds const& odds, uint32_t packs, PackRng const& rng);
}

#endif
