/*
 * The economy rules: crafting dust (an account-wide balance from scrapping cards, spent on crafting
 * cards at a crafting NPC), the dust values of the rarities, the match rewards in Hearthstone coins (the coins
 * are a real bag item, see HsEconomyGlue.h) and the WALLET / CRAFTED / WALLET_UPD / CRAFT_ACCESS
 * payload encoders (docs/PROTOCOL.md "Wallet and crafting").
 *
 * PURE C++ (standard library only, no core headers) so it can be unit-tested headlessly. The service is
 * part of HsCollectionService (crafting changes a card and the dust in one transaction), the database glue
 * lives in src/HsCollectionStore.*.
 */

#ifndef MOD_HEARTHSTONE_HS_WALLET_H
#define MOD_HEARTHSTONE_HS_WALLET_H

#include "HsOwnership.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace Hs
{
    // Dust an account may hold (INT UNSIGNED column, far below its limit); also the bound of a coin count in a payload.
    constexpr uint32_t kMaxBalance = 1000000000;
    // One GM change of the dust.
    constexpr uint32_t kMaxBalanceChange = 1000000;
    // Copies one CRAFT_SCRAP may scrap.
    constexpr uint32_t kMaxScrapPerOp = 99;
    // Copies one CRAFT_EXTRAS may scrap (the craft op row stores the count in a SMALLINT UNSIGNED; a collection
    // never comes near it: 999 copies of every one of the ~380 craftable cards would be 380000).
    constexpr uint32_t kMaxExtrasPerOp = 60000;
    // The card id of a CRAFT_EXTRAS op in its craft op row and in CRAFTED (no card has this id).
    constexpr char const* kExtrasCardId = "all";
    // CRAFT_MAKE / CRAFT_SCRAP key: [0-9a-z], 12..16 characters (the PACK_OPEN rules).
    constexpr std::size_t kMinCraftKeyLen = 12;
    constexpr std::size_t kMaxCraftKeyLen = 16;

    // The account's stored balances. Hearthstone coins are not here: they are items in the character's bags.
    struct Wallet
    {
        uint32_t dust = 0;
        bool operator==(Wallet const& o) const { return dust == o.dust; }
        bool operator!=(Wallet const& o) const { return !(*this == o); }
    };

    // Dust per rarity bucket: index 0 Common (Basic / FREE cards use it), 1 Rare, 2 Epic, 3 Legendary.
    // Defaults are the 2014 values.
    struct DustValues
    {
        std::array<uint32_t, 4> scrap = { { 5, 20, 100, 400 } };
        std::array<uint32_t, 4> craft = { { 40, 100, 400, 1600 } };
    };
    // Clamps every value to 1..1000000 (0 would make crafting free or scrapping pointless).
    DustValues SanitizeDustValues(DustValues values);

    // Coins for a normally completed match. The defaults (10 / 25 / 40 / 75, pack 100) are divided by 10
    // because a coin item stacks to 250: a win on Normal pays 3.
    struct MatchRewards
    {
        uint32_t loss = 1;
        uint32_t winNormal = 3;
        uint32_t winHard = 4;
        uint32_t winBoss = 8;
    };
    MatchRewards SanitizeMatchRewards(MatchRewards rewards); // each at most 1000

    // The coins a match end pays. res: win loss draw int (the human's view), why: the end reason
    // (concede for a surrender), difficulty: normal hard boss. A draw pays the win amount; a surrender
    // and an interrupted match ("int": Stuck, teleport, logout, ...) pay nothing.
    uint32_t MatchCoins(MatchRewards const& rewards, std::string const& res, std::string const& why,
                        std::string const& difficulty);

    // The rarity bucket of a card for dust: FREE and COMMON -> 0 ... LEGENDARY -> 3. False for Rarity::None.
    bool DustBucket(Rarity rarity, std::size_t& out);
    char const* DustBucketName(std::size_t bucket); // "COMMON", "RARE", "EPIC", "LEGENDARY"

    // Can the card be crafted and scrapped at all (single craft, single scrap and scrap-extras)? Collectible,
    // acquisition standard and a known rarity. The four promo cards (reward_only) never can, and neither
    // can the Naxxramas cards (quest): they cannot be crafted, so crafting cannot bypass the quests, and
    // they cannot be scrapped, because dust could never buy them back and every copy a quest grants is a protected
    // reward copy anyway (an unprotected copy exists only through a GM grant).
    bool IsCraftable(CardRule const& rule);
    // How many copies crafting fills: the playable copies of a deck (2, or 1 for a Legendary). Crafting stops
    // once the account owns that many (no dust is spent on copies a deck can
    // never use; scrapping extra pack copies is still possible).
    uint32_t CraftLimit(CardRule const& rule);

    // Extras: every extra copy of the collection in one op (CRAFT_EXTRAS). A copy is extra when the account
    // owns more than a deck can use (2, or 1 for a Legendary) and it is not protected.
    enum class CraftKind { Make, Scrap, Extras };
    char const* CraftKindCode(CraftKind kind); // "craft", "scrap", "extras"

    // Why a craft or a scrap was refused (REFUSED why=<code>).
    enum class CraftWhy
    {
        None,
        Card,     // unknown card, or not craftable (not collectible, reward_only, quest, ...)
        Dust,     // not enough dust
        Limit,    // the account already owns the playable copies (craft), or the dust would overflow (scrap)
        Copies,   // not enough unprotected copies to scrap
        Count,    // scrap count outside 1..kMaxScrapPerOp
        Npc,      // no crafting NPC is open (the glue checks it)
        Nothing   // scrap extras: no copy above what a deck can use
    };
    char const* CraftWhyCode(CraftWhy why); // card dust limit copies count npc nothing
    char const* CraftWhyText(CraftWhy why); // player-readable

    // [0-9a-z]{12,16}
    bool IsValidCraftKey(std::string const& key);

    // One committed craft or scrap (hearthstone_craft_op row): the stored outcome a replay of the key returns.
    struct CraftRecord
    {
        std::string key;
        CraftKind kind = CraftKind::Make;
        std::string cardId;
        uint32_t count = 0;          // copies made or scrapped
        int32_t dustDelta = 0;       // -cost or +yield
        uint32_t dustAfter = 0;
        uint32_t collectionVersion = 0;
        CardCount cardAfter;         // the card's counts afterwards
    };

    // The view the client gets (WALLET): coins in the bags, dust, the dust values and the crafting NPC that is open.
    struct WalletView
    {
        uint32_t coins = 0;          // Hearthstone Coin items in the character's bags
        Wallet wallet;
        DustValues values;
        uint32_t packPriceCoins = 0;
        bool craftOpen = false;
        std::string craftNpc;        // the crafting NPC's name while craftOpen
    };

    // WALLET: coins=;dust=;craft=0|1;npc=;scrap=<c>.<r>.<e>.<l>;make=<c>.<r>.<e>.<l>;packcoins=
    std::string EncodeWalletPayload(WalletView const& view);
    // WALLET_UPD push: coins=<coins in the bags>;dust=;why=<match|buy|gm>[;gain=<coins earned>]
    std::string EncodeWalletChangedPayload(uint32_t coins, Wallet const& wallet, std::string const& why, uint32_t gain);
    // CRAFTED: key=;replay=0|1;kind=craft|scrap|extras;card=;n=;delta=;dust=;ver=;total=;prot=
    // (kind=extras: card=all, n = the copies scrapped over all cards, total=0, prot=0)
    std::string EncodeCraftedPayload(CraftRecord const& record, bool replay);
    // CRAFT_ACCESS push: open=0|1[;npc=<name>]
    std::string EncodeCraftAccessPayload(bool open, std::string const& npc);
}

#endif
