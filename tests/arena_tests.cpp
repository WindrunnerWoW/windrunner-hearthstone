// Standalone tests of the draft arena rules and run service (src/arena/HsArenaRules.cpp,
// HsArenaService.cpp) over the full card dataset (tests/card_catalog.h, tests/match_all_cards.h).
//   arena_tests.exe [directory]      run all tests; exit code 0 = all checks passed.

#include "../src/arena/HsArenaDraftAi.h"
#include "../src/arena/HsArenaEntry.h"
#include "../src/arena/HsArenaPayout.h"
#include "../src/arena/HsArenaRows.h"
#include "../src/arena/HsArenaRules.h"
#include "../src/arena/HsArenaService.h"
#include "../src/collection/HsPacks.h"
#include "../src/collection/HsTraderRules.h"
#include "../src/collection/HsWallet.h"
#include "card_catalog.h"
#include "match_all_cards.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace Hs;

static int g_checks = 0;
static int g_failed = 0;
static char const* g_test = "";

#define CHECK(cond)                                                                                   \
    do {                                                                                              \
        ++g_checks;                                                                                   \
        if (!(cond)) {                                                                                \
            ++g_failed;                                                                               \
            std::printf("FAIL [%s] %s:%d: %s\n", g_test, __FILE__, __LINE__, #cond);                  \
        }                                                                                             \
    } while (0)

#define RUN(fn)                                                                                       \
    do {                                                                                              \
        g_test = #fn;                                                                                 \
        int const before = g_failed;                                                                  \
        fn();                                                                                         \
        std::printf("%-44s %s\n", #fn, g_failed == before ? "ok" : "FAILED");                         \
    } while (0)

namespace
{
    // The dataset as the module sees it: set / collectible / acquisition from card_catalog.h, class and rarity
    // from match_all_cards.h (both generated from the same cards.normalized.json).
    std::shared_ptr<Hs::CardCatalog const> Catalog()
    {
        static std::shared_ptr<Hs::CardCatalog const> const catalog = []
        {
            auto c = std::make_shared<Hs::CardCatalog>();
            std::map<std::string, MatchAllCards::Row const*> rows;
            for (MatchAllCards::Row const& r : MatchAllCards::kRows)
                rows[r.id] = &r;
            for (::CardCatalog::Row const& r : ::CardCatalog::kRows)
            {
                auto it = rows.find(r.id);
                if (it == rows.end())
                    continue;
                c->Add(r.id, r.collectible != 0, r.acquisition, it->second->rarity, it->second->cls);
            }
            return std::shared_ptr<Hs::CardCatalog const>(c);
        }();
        return catalog;
    }

    std::shared_ptr<Match::CardDb const> Db()
    {
        return std::shared_ptr<Match::CardDb const>(&MatchAllCards::Db(), [](Match::CardDb const*) {});
    }

    Hs::ArenaPool PoolOf(std::string const& deckClass)
    {
        return Hs::ArenaPool::FromCatalog(*Catalog(), *Db(), deckClass);
    }

    ArenaTerms Terms(uint32_t base = 3, uint32_t bonus = 10)
    {
        ArenaSettings s;
        s.baseRewardPacks = base;
        s.bonusCoins = bonus;
        return MakeArenaTerms(s, ArenaPay::Free, "hash");
    }

    // An eligible card of the Catalog with this rarity and class, the first by id.
    std::string FirstCard(Rarity rarity, std::string const& cls)
    {
        std::string best;
        Catalog()->ForEach([&](std::string const& id, CardRule const& rule)
        {
            if (rule.rarity == rarity && rule.cardClass == cls && ArenaCardOk(id, rule, *Db()) &&
                (best.empty() || id < best))
                best = id;
        });
        return best;
    }
}

// ---------------------------------------------------------------------------
// Settings, terms, prizes, special cards, period

static void TestSettings()
{
    ArenaSettings d;
    CHECK(d.entryCostCopper == 150000 && d.entryCostCoins == 30 && d.baseRewardPacks == 3 &&
        d.bonusCoins == 10);
    CHECK(d.rarityWeights[0] == 716 && d.rarityWeights[1] == 228 && d.rarityWeights[2] == 44 &&
        d.rarityWeights[3] == 11);
    ArenaSettings ok = SanitizeArenaSettings(d);
    CHECK(ok.entryCostCopper == 150000 && ok.entryCostCoins == 30 && ok.baseRewardPacks == 3 && ok.bonusCoins == 10);

    ArenaSettings bad;
    bad.entryCostCopper = 0;
    bad.entryCostCoins = 0;
    bad.baseRewardPacks = 0;
    bad.bonusCoins = 5000;
    bad.rarityWeights = { { 0, 0, 0, 0 } };
    ArenaSettings s = SanitizeArenaSettings(bad);
    CHECK(s.entryCostCopper == 1 && s.entryCostCoins == 1 && s.baseRewardPacks == 1 && s.bonusCoins == kMaxArenaBonusCoins);
    CHECK(s.rarityWeights == ArenaSettings().rarityWeights);

    bad.entryCostCopper = 0xFFFFFFFFu;
    bad.entryCostCoins = 5000;
    bad.baseRewardPacks = 1000;
    bad.rarityWeights = { { 5000000, 1, 0, 0 } };
    s = SanitizeArenaSettings(bad);
    CHECK(s.entryCostCopper == kMaxArenaEntryCopper && s.entryCostCoins == kMaxArenaEntryCoins);
    CHECK(s.baseRewardPacks == kMaxArenaBaseRewardPacks && s.baseRewardPacks + 1 <= kMaxPacksPerOp);
    CHECK(s.rarityWeights[0] == kMaxArenaRarityWeight && s.rarityWeights[1] == 1);
    bad.bonusCoins = 0;
    CHECK(SanitizeArenaSettings(bad).bonusCoins == 0); // no bonus is a valid setting

    std::array<uint32_t, kPackRarityCount> w {};
    CHECK(ParseArenaRarityWeights("716,228,44,11", w) && w[0] == 716 && w[3] == 11);
    CHECK(ParseArenaRarityWeights(" 1 , 2,3 ,4", w) && w[0] == 1 && w[1] == 2 && w[2] == 3 && w[3] == 4);
    CHECK(ParseArenaRarityWeights("0,0,0,0", w) && w[0] == 0);
    std::array<uint32_t, kPackRarityCount> const keep = w;
    for (char const* text : { "", "1,2,3", "1,2,3,4,5", "1,2,3,", ",1,2,3", "1,2,x,4", "1,-2,3,4", "1,,3,4", "1.5,2,3,4",
                              "1234567890,1,1,1", "1 2,3,4,5" })
    {
        CHECK(!ParseArenaRarityWeights(text, w));
        CHECK(w == keep); // untouched on failure
    }
    CHECK(FormatArenaRarityWeights(ArenaSettings().rarityWeights) == "716,228,44,11");
    CHECK(ParseArenaRarityWeights(FormatArenaRarityWeights({ { 9, 8, 7, 6 } }), w) && w[0] == 9 && w[3] == 6);

    ArenaPay pay;
    CHECK(ParseArenaPay("free", pay) && pay == ArenaPay::Free);
    CHECK(ParseArenaPay("gold", pay) && pay == ArenaPay::Gold);
    CHECK(ParseArenaPay("coins", pay) && pay == ArenaPay::Coins);
    CHECK(ParseArenaPay("practice", pay) && pay == ArenaPay::Practice && std::string(ArenaPayCode(pay)) == "practice");
    CHECK(!ParseArenaPay("Gold", pay) && !ParseArenaPay("", pay) && !ParseArenaPay("none", pay));
    CHECK(!ArenaPayUsesDailySlot(ArenaPay::Free) && ArenaPayUsesDailySlot(ArenaPay::Gold) &&
        ArenaPayUsesDailySlot(ArenaPay::Coins) && !ArenaPayUsesDailySlot(ArenaPay::Practice));
}

static void TestTerms()
{
    ArenaSettings s;
    s.baseRewardPacks = 5;
    s.bonusCoins = 7;
    s.entryCostCopper = 200000;
    s.entryCostCoins = 40;
    s.rarityWeights = { { 1, 2, 3, 4 } };
    ArenaTerms gold = MakeArenaTerms(s, ArenaPay::Gold, "abc");
    CHECK(gold.pay == ArenaPay::Gold && gold.payAmount == 200000 && gold.basePacks == 5 && gold.bonusCoins == 7);
    CHECK(gold.rarityWeights[3] == 4 && gold.datasetHash == "abc" &&
        gold.rulesVersion == static_cast<uint32_t>(Match::kRulesVersion));
    ArenaTerms coins = MakeArenaTerms(s, ArenaPay::Coins, "abc");
    CHECK(coins.payAmount == 40 && coins.basePacks == 5);
    ArenaTerms free = MakeArenaTerms(s, ArenaPay::Free, "abc");
    CHECK(free.pay == ArenaPay::Free && free.payAmount == 0 && free.basePacks == 5);
}

static void TestPrizeTiers()
{
    ArenaTerms t = Terms();
    ArenaPrize const none;
    CHECK(PrizeFor(0, t, false) == none && PrizeFor(0, t, true) == none);
    // A practice run pays nothing, whatever it wins (and never offers the special card).
    ArenaTerms practice = MakeArenaTerms(ArenaSettings(), ArenaPay::Practice, "abc");
    CHECK(practice.payAmount == 0);
    for (uint32_t wins = 0; wins <= 12; ++wins)
        CHECK(PrizeFor(wins, practice, false) == none && PrizeFor(wins, practice, true) == none);
    for (uint32_t wins : { 1u, 2u })
    {
        ArenaPrize p = PrizeFor(wins, t, false);
        CHECK(p.packs == 1 && p.coins == 0 && !p.specialChoice);
    }
    for (uint32_t wins : { 3u, 4u, 5u })
    {
        ArenaPrize p = PrizeFor(wins, t, false);
        CHECK(p.packs == 3 && p.coins == 0 && !p.specialChoice);
    }
    for (uint32_t wins : { 6u, 7u, 8u, 9u })
    {
        ArenaPrize p = PrizeFor(wins, t, false);
        CHECK(p.packs == 3 && p.coins == 10 && !p.specialChoice);
        CHECK(PrizeFor(wins, t, true) == p); // the special flag matters only at ten
    }
    ArenaPrize missing = PrizeFor(10, t, false);
    CHECK(missing.packs == 3 && missing.coins == 10 && missing.specialChoice);
    ArenaPrize complete = PrizeFor(10, t, true);
    CHECK(complete.packs == 4 && complete.coins == 10 && !complete.specialChoice);
    CHECK(PrizeFor(11, t, false) == missing && PrizeFor(500, t, true) == complete); // clamped to ten

    // The base reward and the bonus change the 3..10 tiers; the one/two win consolation stays one pack.
    ArenaTerms t5 = Terms(5, 25);
    CHECK(PrizeFor(1, t5, false).packs == 1 && PrizeFor(2, t5, false).packs == 1);
    CHECK(PrizeFor(3, t5, false).packs == 5 && PrizeFor(5, t5, false).coins == 0);
    CHECK(PrizeFor(6, t5, false).packs == 5 && PrizeFor(6, t5, false).coins == 25);
    CHECK(PrizeFor(10, t5, true).packs == 6 && PrizeFor(10, t5, true).coins == 25);
    ArenaTerms t0 = Terms(3, 0);
    CHECK(PrizeFor(9, t0, false).coins == 0 && PrizeFor(10, t0, false).coins == 0);
    ArenaTerms t1 = Terms(1, 10);
    CHECK(PrizeFor(2, t1, false).packs == 1 && PrizeFor(3, t1, false).packs == 1 && PrizeFor(10, t1, true).packs == 2);

    // The entry price never enters the prize.
    ArenaSettings s;
    s.entryCostCopper = 1000000;
    s.entryCostCoins = 500;
    CHECK(PrizeFor(6, MakeArenaTerms(s, ArenaPay::Gold, "h"), false) == PrizeFor(6, t, false));
    CHECK(PrizeFor(6, MakeArenaTerms(s, ArenaPay::Coins, "h"), false) == PrizeFor(6, t, false));
}

static void TestSpecialCards()
{
    std::vector<ArenaSpecialCard> const& cards = ArenaSpecialCards();
    CHECK(cards.size() == 4);
    CHECK(std::string(cards[0].id) == "EX1_062" && cards[0].copies == 1);
    CHECK(std::string(cards[1].id) == "NEW1_016" && cards[1].copies == 2);
    CHECK(std::string(cards[2].id) == "EX1_112" && cards[2].copies == 1);
    CHECK(std::string(cards[3].id) == "PRO_001" && cards[3].copies == 1);
    CHECK(FindArenaSpecialCard("NEW1_016") && FindArenaSpecialCard("NEW1_016")->copies == 2);
    CHECK(!FindArenaSpecialCard("CS2_032") && !FindArenaSpecialCard("new1_016") && !FindArenaSpecialCard(""));
    // They are exactly the reward-only cards of the dataset.
    std::set<std::string> rewardOnly;
    Catalog()->ForEach([&](std::string const& id, CardRule const& rule)
    {
        if (rule.collectible && rule.acquisition == Acquisition::RewardOnly)
            rewardOnly.insert(id);
    });
    std::set<std::string> special;
    for (ArenaSpecialCard const& c : cards)
        special.insert(c.id);
    CHECK(rewardOnly == special);

    Collection none;
    CHECK(ArenaMissingSpecials(none).size() == 4 && !ArenaSpecialSetsComplete(none));
    CHECK(ArenaSpecialMissingCopies(cards[1], none) == 2);
    Collection c;
    c.Set("NEW1_016", { 1, 0 });
    CHECK(ArenaSpecialMissingCopies(cards[1], c) == 1);
    c.Set("NEW1_016", { 0, 1 }); // an unprotected copy counts like a protected one
    CHECK(ArenaSpecialMissingCopies(cards[1], c) == 1);
    c.Set("NEW1_016", { 1, 1 });
    CHECK(ArenaSpecialMissingCopies(cards[1], c) == 0);
    c.Set("NEW1_016", { 5, 5 });
    CHECK(ArenaSpecialMissingCopies(cards[1], c) == 0); // extra copies are no negative
    c.Set("EX1_062", { 0, 1 });
    std::vector<ArenaSpecialMissing> missing = ArenaMissingSpecials(c);
    CHECK(missing.size() == 2 && missing[0].id == "EX1_112" && missing[0].copies == 1 && missing[1].id == "PRO_001");
    c.Set("EX1_112", { 1, 0 });
    c.Set("PRO_001", { 1, 0 });
    CHECK(ArenaMissingSpecials(c).empty() && ArenaSpecialSetsComplete(c));
}

static void TestPeriod()
{
    uint32_t swap = 0;
    CHECK(ParseTraderSwapTime("04:00", swap) && swap == 4 * 3600);
    uint64_t const day = 1760000000ull / 86400 * 86400; // a UTC midnight
    CHECK(ArenaPeriod(day + swap, swap) == day + swap);
    CHECK(ArenaPeriod(day + swap - 1, swap) == day - 86400 + swap);
    CHECK(ArenaPeriod(day + swap + 86399, swap) == day + swap);
    CHECK(ArenaPeriod(day + swap + 86400, swap) == day + swap + 86400);
    for (uint64_t now : { day, day + 1, day + 12345, day + 86399 })
        CHECK(ArenaPeriod(now, swap) == TraderPeriod(now, swap));
    CHECK(ArenaPeriod(100, swap) == 0); // before the first boundary, like the trader
}

// ---------------------------------------------------------------------------
// Eligibility and pools

static void TestEligibility()
{
    Match::CardDb const& db = *Db();
    CardRule rule;
    rule.collectible = true;
    rule.acquisition = Acquisition::Standard;
    rule.rarity = Rarity::Common;
    rule.cardClass = "MAGE";
    CHECK(ArenaCardOk("CS2_032", rule, db) && db.Supported("CS2_032")); // Flamestrike
    CHECK(ArenaEligible("CS2_032", rule, db, "MAGE"));
    CHECK(!ArenaEligible("CS2_032", rule, db, "WARRIOR")); // another class's card
    CardRule neutral = rule;
    neutral.cardClass = "NEUTRAL";
    CHECK(ArenaEligible("CS2_032", neutral, db, "WARRIOR") && ArenaEligible("CS2_032", neutral, db, "MAGE"));
    CardRule r = rule;
    r.collectible = false; // tokens
    CHECK(!ArenaCardOk("CS2_032", r, db));
    r = rule;
    r.acquisition = Acquisition::RewardOnly;
    CHECK(!ArenaCardOk("CS2_032", r, db));
    r.acquisition = Acquisition::Unreleased;
    CHECK(!ArenaCardOk("CS2_032", r, db));
    r.acquisition = Acquisition::None;
    CHECK(!ArenaCardOk("CS2_032", r, db));
    r = rule;
    r.rarity = Rarity::None;
    CHECK(!ArenaCardOk("CS2_032", r, db));
    r.rarity = Rarity::Free; // Basic is allowed
    CHECK(ArenaCardOk("CS2_032", r, db));
    // Engine support: an id the engine does not know is refused even with an eligible catalog row.
    Match::CardDb empty;
    CHECK(!ArenaCardOk("CS2_032", rule, empty));
    CHECK(!ArenaCardOk("NO_SUCH_CARD", rule, db));

    // The real dataset: no class pool holds a token, a reward-only card, another class's card or an unsupported one.
    std::set<std::string> special;
    for (ArenaSpecialCard const& c : ArenaSpecialCards())
        special.insert(c.id);
    for (std::string const& cls : DeckClasses())
    {
        Hs::ArenaPool pool = PoolOf(cls);
        std::size_t counted = 0;
        Catalog()->ForEach([&](std::string const& id, CardRule const& cr)
        {
            bool const expected = cr.collectible && cr.acquisition == Acquisition::Standard &&
                cr.rarity != Rarity::None && db.Supported(id) && (cr.cardClass == cls || cr.cardClass == "NEUTRAL");
            counted += expected ? 1 : 0;
        });
        CHECK(pool.Size() == counted && counted > 100);
        std::set<std::string> seen;
        for (std::size_t b = 0; b < kPackRarityCount; ++b)
        {
            std::vector<std::string> const& cards = pool.Cards(static_cast<PackRarity>(b));
            CHECK(!cards.empty());
            CHECK(std::is_sorted(cards.begin(), cards.end()));
            for (std::string const& id : cards)
            {
                CardRule const* cr = Catalog()->Find(id);
                CHECK(cr && cr->collectible && cr->acquisition == Acquisition::Standard);
                CHECK(cr && (cr->cardClass == cls || cr->cardClass == "NEUTRAL"));
                CHECK(special.count(id) == 0 && db.Supported(id));
                CHECK(seen.insert(id).second); // in exactly one bucket
                PackRarity bucket;
                CHECK(cr && PackRarityOf(cr->rarity, bucket) && static_cast<std::size_t>(bucket) == b);
            }
        }
        // Basic cards sit in the Common bucket.
        std::string const basic = FirstCard(Rarity::Free, cls);
        CHECK(!basic.empty());
        std::vector<std::string> const& common = pool.Cards(PackRarity::Common);
        CHECK(std::find(common.begin(), common.end(), basic) != common.end());
        CHECK(pool.Size() == seen.size());
    }
    // Other classes' cards are absent: a Mage-only card is not in the Warrior pool.
    Hs::ArenaPool warrior = PoolOf("WARRIOR");
    bool mageCard = false;
    for (std::size_t b = 0; b < kPackRarityCount; ++b)
        for (std::string const& id : warrior.Cards(static_cast<PackRarity>(b)))
            mageCard = mageCard || id == "CS2_032";
    CHECK(!mageCard);
}

// ---------------------------------------------------------------------------
// Offers

static void TestOffersDistinctAndDeterministic()
{
    Hs::ArenaPool pool = PoolOf("PRIEST");
    std::set<std::string> poolIds;
    for (std::size_t b = 0; b < kPackRarityCount; ++b)
        for (std::string const& id : pool.Cards(static_cast<PackRarity>(b)))
            poolIds.insert(id);
    ArenaSettings const defaults;
    PackRng a = MakeSeededRng(42);
    PackRng b = MakeSeededRng(42);
    PackRng c = MakeSeededRng(43);
    std::vector<std::vector<std::string>> seqA, seqB, seqC;
    for (int i = 0; i < 300; ++i)
    {
        std::vector<std::string> oa, ob, oc;
        CHECK(ArenaOffer(pool, defaults.rarityWeights, a, oa));
        CHECK(ArenaOffer(pool, defaults.rarityWeights, b, ob));
        CHECK(ArenaOffer(pool, defaults.rarityWeights, c, oc));
        CHECK(oa.size() == 3 && std::set<std::string>(oa.begin(), oa.end()).size() == 3);
        for (std::string const& id : oa)
            CHECK(poolIds.count(id) == 1);
        seqA.push_back(oa);
        seqB.push_back(ob);
        seqC.push_back(oc);
    }
    CHECK(seqA == seqB); // the same seed gives the same draft
    CHECK(seqA != seqC); // another seed another draft
    PackRng s1 = MakeSeededRng(7);
    PackRng s2 = MakeSeededRng(7);
    bool same = true;
    for (int i = 0; i < 100; ++i)
        same = same && s1(1000) == s2(1000);
    CHECK(same);
    CHECK(MakeSeededRng(1)(1) == 0 && MakeSeededRng(1)(0) == 0);
}

static void TestOfferRarityWeights()
{
    Hs::ArenaPool pool = PoolOf("MAGE");
    ArenaSettings const defaults;
    PackRng rng = MakeSeededRng(2026);
    std::array<uint64_t, kPackRarityCount> counts {};
    uint64_t total = 0;
    for (int i = 0; i < 30000; ++i)
    {
        std::vector<std::string> offer;
        CHECK(ArenaOffer(pool, defaults.rarityWeights, rng, offer));
        for (std::string const& id : offer)
        {
            PackRarity bucket;
            CHECK(PackRarityOf(Catalog()->Find(id)->rarity, bucket));
            ++counts[static_cast<std::size_t>(bucket)];
            ++total;
        }
    }
    double const expected[] = { 716.0 / 999, 228.0 / 999, 44.0 / 999, 11.0 / 999 };
    for (std::size_t i = 0; i < kPackRarityCount; ++i)
    {
        double const share = static_cast<double>(counts[i]) / static_cast<double>(total);
        CHECK(share > expected[i] - 0.01 && share < expected[i] + 0.01);
    }
    // Only legendaries when only legendaries have weight; all weights 0 rolls like the defaults.
    for (int i = 0; i < 200; ++i)
    {
        std::vector<std::string> offer;
        CHECK(ArenaOffer(pool, { { 0, 0, 0, 1 } }, rng, offer));
        for (std::string const& id : offer)
            CHECK(Catalog()->Find(id)->rarity == Rarity::Legendary);
    }
    std::array<uint64_t, kPackRarityCount> zero {};
    for (int i = 0; i < 2000; ++i)
    {
        std::vector<std::string> offer;
        CHECK(ArenaOffer(pool, { { 0, 0, 0, 0 } }, rng, offer));
        for (std::string const& id : offer)
        {
            PackRarity bucket;
            PackRarityOf(Catalog()->Find(id)->rarity, bucket);
            ++zero[static_cast<std::size_t>(bucket)];
        }
    }
    CHECK(zero[0] > zero[1] && zero[1] > zero[2] && zero[2] >= zero[3]);
}

// A pool of the NEUTRAL cards of one rarity set only, built from the real catalog.
static Hs::ArenaPool PoolOfRarities(std::vector<std::pair<Rarity, std::size_t>> const& wanted)
{
    auto catalog = std::make_shared<Hs::CardCatalog>();
    std::map<Rarity, std::size_t> left;
    for (auto const& w : wanted)
        left[w.first] = w.second;
    std::vector<std::pair<std::string, CardRule>> rules;
    Catalog()->ForEach([&](std::string const& id, CardRule const& rule)
    {
        if (rule.cardClass == "NEUTRAL" && ArenaCardOk(id, rule, *Db()))
            rules.push_back({ id, rule });
    });
    std::sort(rules.begin(), rules.end(), [](auto const& x, auto const& y) { return x.first < y.first; });
    auto name = [](Rarity r)
    {
        switch (r)
        {
            case Rarity::Free: return "FREE";
            case Rarity::Common: return "COMMON";
            case Rarity::Rare: return "RARE";
            case Rarity::Epic: return "EPIC";
            default: return "LEGENDARY";
        }
    };
    for (auto const& entry : rules)
    {
        auto it = left.find(entry.second.rarity);
        if (it == left.end() || it->second == 0)
            continue;
        --it->second;
        catalog->Add(entry.first, true, "standard", name(entry.second.rarity), "NEUTRAL");
    }
    return Hs::ArenaPool::FromCatalog(*catalog, *Db(), "MAGE");
}

static void TestOfferRarityFallback()
{
    PackRng rng = MakeSeededRng(5);
    // No Epic, no Legendary: rolling them falls to Rare, then Common.
    Hs::ArenaPool commonRare = PoolOfRarities({ { Rarity::Common, 6 }, { Rarity::Rare, 5 } });
    CHECK(commonRare.Size() == 11);
    for (int i = 0; i < 100; ++i)
    {
        std::vector<std::string> offer;
        CHECK(ArenaOffer(commonRare, { { 0, 0, 1, 0 } }, rng, offer)); // always rolls Epic
        for (std::string const& id : offer)
            CHECK(Catalog()->Find(id)->rarity == Rarity::Rare);
        CHECK(std::set<std::string>(offer.begin(), offer.end()).size() == 3);
    }
    // Only two Rares: the third slot falls to Common, and the offer stays distinct.
    Hs::ArenaPool twoRare = PoolOfRarities({ { Rarity::Common, 6 }, { Rarity::Rare, 2 } });
    for (int i = 0; i < 100; ++i)
    {
        std::vector<std::string> offer;
        CHECK(ArenaOffer(twoRare, { { 0, 0, 0, 1 } }, rng, offer));
        int rares = 0;
        for (std::string const& id : offer)
            rares += Catalog()->Find(id)->rarity == Rarity::Rare ? 1 : 0;
        CHECK(rares == 2 && std::set<std::string>(offer.begin(), offer.end()).size() == 3);
    }
    // Nothing lower than the rolled rarity: the next higher one is the last resort.
    Hs::ArenaPool legendaryOnly = PoolOfRarities({ { Rarity::Legendary, 4 } });
    for (int i = 0; i < 50; ++i)
    {
        std::vector<std::string> offer;
        CHECK(ArenaOffer(legendaryOnly, { { 1, 0, 0, 0 } }, rng, offer));
        for (std::string const& id : offer)
            CHECK(Catalog()->Find(id)->rarity == Rarity::Legendary);
    }
    // Too small a pool gives no offer and leaves out untouched.
    Hs::ArenaPool tiny = PoolOfRarities({ { Rarity::Common, 2 } });
    std::vector<std::string> keep = { "x" };
    CHECK(!ArenaOffer(tiny, ArenaSettings().rarityWeights, rng, keep) && keep.size() == 1);
    // Exactly three cards: always that trio.
    Hs::ArenaPool three = PoolOfRarities({ { Rarity::Common, 1 }, { Rarity::Rare, 1 }, { Rarity::Legendary, 1 } });
    std::vector<std::string> offer;
    CHECK(ArenaOffer(three, ArenaSettings().rarityWeights, rng, offer) && offer.size() == 3);
}

static void TestClassOffer()
{
    std::vector<std::string> const classes = DeckClasses();
    CHECK(classes.size() == 9);
    PackRng a = MakeSeededRng(9);
    PackRng b = MakeSeededRng(9);
    std::set<std::string> covered;
    for (int i = 0; i < 300; ++i)
    {
        std::vector<std::string> oa = ArenaClassOffer(a);
        std::vector<std::string> ob = ArenaClassOffer(b);
        CHECK(oa == ob);
        CHECK(oa.size() == 3 && std::set<std::string>(oa.begin(), oa.end()).size() == 3);
        for (std::string const& cls : oa)
        {
            CHECK(IsDeckClass(cls));
            covered.insert(cls);
        }
    }
    CHECK(covered.size() == 9); // every class can be offered
}

// ---------------------------------------------------------------------------
// Arena deck

static std::vector<std::string> DeckOf(std::string const& cls, std::size_t size)
{
    // Distinct eligible cards of the class pool, then repeats as needed.
    Hs::ArenaPool pool = PoolOf(cls);
    std::vector<std::string> cards;
    for (std::size_t b = 0; b < kPackRarityCount; ++b)
        for (std::string const& id : pool.Cards(static_cast<PackRarity>(b)))
            cards.push_back(id);
    std::sort(cards.begin(), cards.end());
    std::vector<std::string> deck;
    for (std::size_t i = 0; i < size; ++i)
        deck.push_back(cards[i % cards.size()]);
    return deck;
}

static void TestDeckValidation()
{
    Hs::CardCatalog const& catalog = *Catalog();
    Match::CardDb const& db = *Db();
    std::vector<std::string> deck = DeckOf("MAGE", 30);
    CHECK(ValidateArenaDeck(catalog, db, "MAGE", deck) == ArenaDeckWhy::None);

    // No copy cap: thirty copies of a card, a Legendary included, are legal.
    std::string const common = FirstCard(Rarity::Common, "NEUTRAL");
    std::string const legendary = FirstCard(Rarity::Legendary, "NEUTRAL");
    CHECK(!common.empty() && !legendary.empty());
    CHECK(ValidateArenaDeck(catalog, db, "MAGE", std::vector<std::string>(30, common)) == ArenaDeckWhy::None);
    CHECK(ValidateArenaDeck(catalog, db, "MAGE", std::vector<std::string>(30, legendary)) == ArenaDeckWhy::None);
    std::vector<std::string> mixed = DeckOf("MAGE", 27);
    for (int i = 0; i < 3; ++i)
        mixed.push_back(legendary);
    mixed[0] = mixed[1] = mixed[2] = legendary; // up to six copies of one Legendary
    CHECK(ValidateArenaDeck(catalog, db, "MAGE", mixed) == ArenaDeckWhy::None);
    // The ordinary deck rules do refuse the same list (copy cap), so arena validation really differs.
    DeckCards ordinary;
    ordinary[legendary] = 3;
    ordinary[common] = 3;
    CHECK(ValidateDeckCards(catalog, "MAGE", ordinary) == DeckWhy::Copies);

    CHECK(ValidateArenaDeck(catalog, db, "MAGE", DeckOf("MAGE", 29)) == ArenaDeckWhy::Size);
    CHECK(ValidateArenaDeck(catalog, db, "MAGE", DeckOf("MAGE", 31)) == ArenaDeckWhy::Size);
    CHECK(ValidateArenaDeck(catalog, db, "MAGE", {}) == ArenaDeckWhy::Size);
    CHECK(ValidateArenaDeck(catalog, db, "DREAM", deck) == ArenaDeckWhy::Class);
    CHECK(ValidateArenaDeck(catalog, db, "NEUTRAL", deck) == ArenaDeckWhy::Class);
    CHECK(ValidateArenaDeck(catalog, db, "mage", deck) == ArenaDeckWhy::Class);
    CHECK(ValidateArenaDeck(catalog, db, "DREAM", DeckOf("MAGE", 3)) == ArenaDeckWhy::Class); // class before size

    // The Mage deck is not a Warrior deck.
    CHECK(ValidateArenaDeck(catalog, db, "WARRIOR", deck) == ArenaDeckWhy::CardClass);
    // Tokens, reward-only cards, unknown ids and unsupported cards.
    std::string token;
    catalog.ForEach([&](std::string const& id, CardRule const& rule)
    {
        if (!rule.collectible && rule.cardClass == "NEUTRAL" && (token.empty() || id < token))
            token = id;
    });
    CHECK(!token.empty());
    for (std::string const& bad : { token, std::string("EX1_062"), std::string("PRO_001"), std::string("NO_SUCH_CARD") })
    {
        std::vector<std::string> d = deck;
        d[17] = bad;
        CHECK(ValidateArenaDeck(catalog, db, "MAGE", d) == ArenaDeckWhy::Card);
    }
    Match::CardDb empty;
    CHECK(ValidateArenaDeck(catalog, empty, "MAGE", deck) == ArenaDeckWhy::Card);
    // Card is checked before class: a wrong-class token is a Card problem.
    std::vector<std::string> both = deck;
    both[3] = FirstCard(Rarity::Common, "WARRIOR");
    both[4] = token;
    CHECK(ValidateArenaDeck(catalog, db, "MAGE", both) == ArenaDeckWhy::Card);
    CHECK(std::string(ArenaDeckWhyCode(ArenaDeckWhy::CardClass)) == "cardclass" &&
        std::string(ArenaDeckWhyCode(ArenaDeckWhy::None)).empty());
    CHECK(std::string(ArenaDeckWhyText(ArenaDeckWhy::Size)).size() > 5);
}


// The 30 collectible Naxxramas cards (acquisition quest) are in the dataset but in no pool: not in packs, not
// craftable or scrappable, not offered by the trader, not offered by the arena (owned-card eligibility is a later step).
static void TestQuestCardsStayOutOfPools()
{
    Match::CardDb const& db = *Db();
    std::vector<std::string> quest;
    std::size_t standard = 0;
    Catalog()->ForEach([&](std::string const& id, CardRule const& rule)
    {
        if (rule.acquisition == Acquisition::Quest)
            quest.push_back(id);
        if (rule.collectible && rule.acquisition == Acquisition::Standard)
            ++standard;
    });
    std::sort(quest.begin(), quest.end());
    CHECK(quest.size() == 30 && quest.front() == "FP1_001" && quest.back() == "FP1_031");
    CHECK(standard == 378); // the Basic + Classic catalog is unchanged by the expansion

    // Packs: the pool is exactly the 378 standard cards (227 common incl. Basic, 81 rare, 37 epic, 33 legendary).
    PackPool const packs = PackPool::FromCatalog(*Catalog());
    CHECK(packs.Size() == 378 && packs.Complete());
    CHECK(packs.Cards(PackRarity::Common).size() == 227 && packs.Cards(PackRarity::Rare).size() == 81);
    CHECK(packs.Cards(PackRarity::Epic).size() == 37 && packs.Cards(PackRarity::Legendary).size() == 33);

    std::set<std::string> const questSet(quest.begin(), quest.end());
    for (std::string const& id : quest)
    {
        CardRule const* rule = Catalog()->Find(id);
        CHECK(rule && rule->collectible && rule->rarity != Rarity::None);
        if (!rule)
            continue;
        CHECK(!IsCraftable(*rule));                         // no crafting, no scrapping
        CHECK(!TraderEligible(id, *rule, {}));              // no daily trader
        CHECK(!ArenaCardOk(id, *rule, db));                 // no arena offers (for now)
        std::string canonical;
        CardRule found;
        CHECK(ValidateGrant(*Catalog(), id, GrantSource::Pack, canonical, found) == OwnershipError::SourceNotAllowed);
        CHECK(ValidateGrant(*Catalog(), id, GrantSource::SpecialReward, canonical, found) == OwnershipError::SourceNotAllowed);
        CHECK(ValidateGrant(*Catalog(), id, GrantSource::Quest, canonical, found) == OwnershipError::None);
        CHECK(ValidateGrant(*Catalog(), id, GrantSource::Gm, canonical, found) == OwnershipError::None);
        for (std::size_t b = 0; b < kPackRarityCount; ++b)
        {
            std::vector<std::string> const& cards = packs.Cards(static_cast<PackRarity>(b));
            CHECK(std::find(cards.begin(), cards.end(), id) == cards.end());
        }
    }
    for (std::string const& cls : DeckClasses())
    {
        Hs::ArenaPool const pool = PoolOf(cls);
        for (std::size_t b = 0; b < kPackRarityCount; ++b)
            for (std::string const& id : pool.Cards(static_cast<PackRarity>(b)))
                CHECK(questSet.count(id) == 0);
    }

    // The ordinary deck rules accept them; an arena draft deck does not.
    DeckCards ordinary;
    ordinary["FP1_028"] = 2; // Undertaker
    ordinary["FP1_010"] = 1; // Maexxna
    CHECK(ValidateDeckCards(*Catalog(), "MAGE", ordinary) == DeckWhy::None);
    ordinary["FP1_018"] = 2; // Duplicate (Mage)
    CHECK(ValidateDeckCards(*Catalog(), "MAGE", ordinary) == DeckWhy::None);
    CHECK(ValidateDeckCards(*Catalog(), "WARRIOR", ordinary) == DeckWhy::CardClass);
    std::vector<std::string> deck = DeckOf("MAGE", 30);
    deck[3] = "FP1_028";
    CHECK(ValidateArenaDeck(*Catalog(), db, "MAGE", deck) == ArenaDeckWhy::Card);
}

// ---------------------------------------------------------------------------
// Run service

namespace
{
    constexpr uint32_t kAccount = 7;
    constexpr uint32_t kGuid = 101;
    constexpr uint64_t kDay = 1760000000ull / 86400 * 86400; // a UTC midnight
    constexpr uint64_t kNow = kDay + 12 * 3600;              // noon, after the 04:00 swap
    constexpr uint32_t kSwap = 4 * 3600;

    struct Env
    {
        ArenaMemoryStorage store;
        std::unique_ptr<ArenaService> svc;

        explicit Env(uint64_t seed = 1)
        {
            svc.reset(new ArenaService(store));
            svc->SetData(Catalog(), Db(), "hash1");
            svc->SetRng(MakeSeededRng(seed));
        }

        ArenaStartRequest Request(ArenaPay pay, uint64_t now = kNow, uint32_t account = kAccount) const
        {
            ArenaStartRequest r;
            r.accountId = account;
            r.pay = pay;
            r.payingGuid = kGuid;
            r.now = now;
            r.swapSeconds = kSwap;
            return r;
        }

        // A confirmed run (free, or paid and confirmed) in ChoosingClass.
        ArenaRun Begin(ArenaPay pay = ArenaPay::Free, uint64_t now = kNow, uint32_t account = kAccount)
        {
            ArenaOutcome o = svc->Start(Request(pay, now, account));
            CHECK(o.ok);
            if (o.needsDebit)
                CHECK(svc->ConfirmEntry(account, o.run.id).ok);
            return svc->Get(account).run;
        }

        // Class chosen (the first offered) and all thirty picks (cycling through the three slots).
        ArenaRun Draft(ArenaRun run)
        {
            ArenaOutcome o = svc->ChooseClass(run.accountId, run.id, run.picks[0].offer[0]);
            CHECK(o.ok);
            for (uint32_t pick = 1; pick <= kArenaDraftPicks; ++pick)
            {
                CHECK(o.ok && o.run.OpenPick() == static_cast<int>(pick));
                o = svc->Pick(run.accountId, run.id, pick, o.run.picks[pick].offer[pick % 3]);
                CHECK(o.ok && !o.replay);
            }
            CHECK(o.run.state == ArenaState::Ready);
            return o.run;
        }

        ArenaRun Ready(ArenaPay pay = ArenaPay::Free, uint64_t now = kNow)
        {
            return Draft(Begin(pay, now));
        }

        // Plays one duel with a result; returns the outcome of RecordResult.
        ArenaOutcome Duel(ArenaRun const& run, ArenaDuelResult result, bool specialsComplete = false)
        {
            ArenaOutcome b = svc->BeginDuel(run.accountId, run.id, kGuid);
            CHECK(b.ok && b.run.state == ArenaState::InDuel);
            return svc->RecordResult(run.accountId, run.id, b.run.matchNo, result, kGuid, specialsComplete, kNow);
        }

        // Plays `wins` winning duels (the draw positions given are draws) and optionally a final loss.
        ArenaRun Play(ArenaRun run, uint32_t wins, bool finalLoss, bool specialsComplete = false)
        {
            for (uint32_t i = 0; i < wins; ++i)
            {
                ArenaOutcome o = Duel(run, i % 4 == 3 ? ArenaDuelResult::Draw : ArenaDuelResult::Win, specialsComplete);
                CHECK(o.ok);
                run = o.run;
            }
            if (finalLoss)
            {
                ArenaOutcome o = Duel(run, ArenaDuelResult::Loss, specialsComplete);
                CHECK(o.ok);
                run = o.run;
            }
            return run;
        }
    };
}

static void TestRunHappyPath()
{
    Env env(11);
    ArenaOutcome start = env.svc->Start(env.Request(ArenaPay::Free));
    CHECK(start.ok && !start.replay && !start.needsDebit && start.hasRun);
    ArenaRun run = start.run;
    CHECK(run.id != 0 && run.accountId == kAccount && run.state == ArenaState::ChoosingClass && run.entryConfirmed);
    CHECK(run.picks.size() == 1 && run.picks[0].offer.size() == 3 && run.picks[0].chosen.empty());
    CHECK(run.terms.pay == ArenaPay::Free && run.terms.payAmount == 0 && run.terms.datasetHash == "hash1");
    // The class offer is persisted before it is returned.
    CHECK(env.store.runs.at(run.id).picks[0].offer == run.picks[0].offer);

    ArenaRun ready = env.Draft(run);
    CHECK(ready.deckClass == run.picks[0].offer[0] && ready.picks.size() == 31 && ready.Deck().size() == 30);
    CHECK(ValidateArenaDeck(*Catalog(), *Db(), ready.deckClass, ready.Deck()) == ArenaDeckWhy::None);
    CHECK(ready.OpenPick() == -1 && ready.matchNo == 0);
    // Every offer was three distinct cards of the class pool, and every pick came from its offer.
    for (uint32_t i = 1; i <= 30; ++i)
    {
        ArenaPick const& p = ready.picks[i];
        CHECK(p.offer.size() == 3 && std::set<std::string>(p.offer.begin(), p.offer.end()).size() == 3);
        CHECK(std::find(p.offer.begin(), p.offer.end(), p.chosen) != p.offer.end());
        for (std::string const& id : p.offer)
        {
            CardRule const* rule = Catalog()->Find(id);
            CHECK(rule && ArenaEligible(id, *rule, *Db(), ready.deckClass));
        }
    }
    CHECK(env.store.runs.at(run.id).state == ArenaState::Ready);

    ArenaOutcome duel = env.svc->BeginDuel(kAccount, run.id, kGuid);
    CHECK(duel.ok && duel.run.state == ArenaState::InDuel && duel.run.matchNo == 1 && duel.run.controllerGuid == kGuid);
    ArenaOutcome win = env.svc->RecordResult(kAccount, run.id, 1, ArenaDuelResult::Win, kGuid, false, kNow);
    CHECK(win.ok && win.run.state == ArenaState::Ready && win.run.wins == 1 && win.run.losses == 0);
    ArenaOutcome loss = env.Duel(win.run, ArenaDuelResult::Loss);
    CHECK(loss.ok && loss.run.state == ArenaState::Finished && loss.run.wins == 1 && loss.run.losses == 1);
    CHECK(loss.run.prize.packs == 1 && loss.run.endedGuid == kGuid && loss.run.ended == kNow && !loss.run.retired);
    ArenaOutcome settled = env.svc->Settle(kAccount, run.id);
    CHECK(settled.ok && settled.run.state == ArenaState::Settled && !settled.run.Open());
    CHECK(settled.settlement.runId == run.id && settled.settlement.wins == 1 && settled.settlement.prize.packs == 1 &&
        settled.settlement.prize.coins == 0 && !settled.settlement.specialChoicePending &&
        settled.settlement.coinGuid == kGuid && !settled.settlement.retired);
}

static void TestEveryPrizeTier()
{
    struct Case
    {
        uint32_t wins;
        bool loss; // ends with a loss (else the run goes on / ends at ten wins)
        bool specialsComplete;
        uint32_t packs, coins;
        bool special;
    };
    Case const cases[] = {
        { 0, true, false, 0, 0, false },   // lost the first duel
        { 1, true, false, 1, 0, false },
        { 2, true, false, 1, 0, false },
        { 3, true, false, 3, 0, false },
        { 4, true, false, 3, 0, false },
        { 5, true, false, 3, 0, false },
        { 6, true, false, 3, 10, false },
        { 9, true, false, 3, 10, false },
        { 10, false, false, 3, 10, true },  // ten wins, a special set missing
        { 10, false, true, 4, 10, false },  // ten wins, all four complete
    };
    for (Case const& c : cases)
    {
        Env env(c.wins * 3 + (c.specialsComplete ? 1 : 0));
        ArenaRun ready = env.Ready(ArenaPay::Free, kNow);
        ArenaRun end = env.Play(ready, c.wins, c.loss, c.specialsComplete);
        CHECK(end.state == ArenaState::Finished);
        CHECK(end.wins == c.wins && end.losses == (c.loss ? 1u : 0u) && end.draws == c.wins / 4);
        CHECK(end.prize.packs == c.packs && end.prize.coins == c.coins && end.prize.specialChoice == c.special);
        CHECK(end.prize == PrizeFor(c.wins, end.terms, c.specialsComplete));
        ArenaOutcome s = env.svc->Settle(kAccount, end.id);
        CHECK(s.ok && s.settlement.prize == end.prize && s.settlement.specialChoicePending == c.special);
        CHECK(s.run.Open() == c.special);
    }
    // Ten wins end the run at the tenth win, not before.
    Env env(99);
    ArenaRun run = env.Ready();
    run = env.Play(run, 9, false);
    CHECK(run.state == ArenaState::Ready && run.wins == 9);
    ArenaOutcome tenth = env.Duel(run, ArenaDuelResult::Draw); // a draw counts as a win
    CHECK(tenth.ok && tenth.run.state == ArenaState::Finished && tenth.run.wins == 10 && tenth.run.draws == 3);
    CHECK(tenth.run.prize.specialChoice);
}

static void TestTermsSnapshot()
{
    Env env(3);
    ArenaStartRequest request = env.Request(ArenaPay::Free);
    request.settings.baseRewardPacks = 5;
    request.settings.bonusCoins = 50;
    request.settings.rarityWeights = { { 1, 1, 1, 1 } };
    ArenaOutcome start = env.svc->Start(request);
    CHECK(start.ok && start.run.terms.basePacks == 5 && start.run.terms.bonusCoins == 50);
    ArenaRun end = env.Play(env.Draft(start.run), 6, true);
    // The run pays what it started with, whatever the settings are now.
    CHECK(end.prize.packs == 5 && end.prize.coins == 50);
    CHECK(env.store.runs.at(end.id).terms.rarityWeights[3] == 1);
}

static void TestRetire()
{
    // Retire from every active state.
    for (int stage = 0; stage < 5; ++stage)
    {
        Env env(20 + stage);
        ArenaRun run = env.Begin();
        uint32_t wins = 0;
        if (stage >= 1)
            run = env.svc->ChooseClass(kAccount, run.id, run.picks[0].offer[1]).run;
        if (stage >= 2)
        {
            for (uint32_t pick = 1; pick <= 30; ++pick)
                run = env.svc->Pick(kAccount, run.id, pick, run.picks[pick].offer[0]).run;
            run = env.Play(run, 4, false);
            wins = 4;
        }
        if (stage == 3)
            run = env.svc->BeginDuel(kAccount, run.id, kGuid).run;
        if (stage == 4)
        {
            run = env.svc->BeginDuel(kAccount, run.id, kGuid).run;
            run = env.svc->PauseDuel(kAccount, run.id).run;
        }
        ArenaOutcome r = env.svc->Retire(kAccount, run.id, 555, false, kNow + 5);
        CHECK(r.ok && !r.replay && r.run.state == ArenaState::Retired && r.run.retired);
        CHECK(r.run.wins == wins && r.run.prize == PrizeFor(wins, r.run.terms, false));
        CHECK(r.run.endedGuid == 555 && r.run.ended == kNow + 5);
        // Retiring again is a replay and changes nothing.
        ArenaOutcome again = env.svc->Retire(kAccount, run.id, 777, true, kNow + 9);
        CHECK(again.ok && again.replay && again.run.endedGuid == 555 && again.run.prize == r.run.prize);
        ArenaOutcome s = env.svc->Settle(kAccount, run.id);
        CHECK(s.ok && s.settlement.retired && s.settlement.coinGuid == 555 && s.settlement.wins == wins);
        // Still a replay after the settlement.
        CHECK(env.svc->Retire(kAccount, run.id, 1, false, kNow).replay);
    }
    // A finished run cannot be retired.
    Env env(31);
    ArenaRun end = env.Play(env.Ready(), 1, true);
    ArenaOutcome r = env.svc->Retire(kAccount, end.id, kGuid, false, kNow);
    CHECK(!r.ok && r.why == "bad_state");
    // Retiring pays the prize of the wins so far: retire at 6 wins.
    Env env2(32);
    ArenaRun six = env2.Play(env2.Ready(), 6, false);
    ArenaOutcome r6 = env2.svc->Retire(kAccount, six.id, kGuid, false, kNow);
    CHECK(r6.ok && r6.run.prize.packs == 3 && r6.run.prize.coins == 10 && !r6.run.prize.specialChoice);
}

static void TestIllegalTransitions()
{
    Env env(40);
    CHECK(env.svc->Get(kAccount).why == "no_run");
    CHECK(env.svc->ChooseClass(kAccount, 1, "MAGE").why == "no_run");
    CHECK(env.svc->Retire(kAccount, 1, kGuid, false, kNow).why == "no_run");

    ArenaRun run = env.Begin();
    uint32_t const id = run.id;
    // Wrong run id.
    CHECK(env.svc->ChooseClass(kAccount, id + 5, run.picks[0].offer[0]).why == "stale");
    // Steps of a later state.
    CHECK(env.svc->Pick(kAccount, id, 1, "CS2_032").why == "bad_state"); // no offer open yet
    CHECK(env.svc->BeginDuel(kAccount, id, kGuid).why == "bad_state");
    CHECK(env.svc->PauseDuel(kAccount, id).why == "bad_state");
    CHECK(env.svc->ResumeDuel(kAccount, id, kGuid).why == "bad_state");
    CHECK(env.svc->RecordResult(kAccount, id, 1, ArenaDuelResult::Win, kGuid, false, kNow).why == "bad_state");
    CHECK(env.svc->Settle(kAccount, id).why == "bad_state");
    Collection none;
    CHECK(env.svc->ClaimSpecial(kAccount, id, "EX1_062", none).why == "bad_state");
    CHECK(env.svc->RepairRebind(kAccount, id).why == "bad_state");
    // A class that was not offered, however valid.
    std::set<std::string> offered(run.picks[0].offer.begin(), run.picks[0].offer.end());
    std::string notOffered;
    for (std::string const& cls : DeckClasses())
        if (!offered.count(cls))
            notOffered = cls;
    CHECK(env.svc->ChooseClass(kAccount, id, notOffered).why == "not_offered");
    CHECK(env.svc->ChooseClass(kAccount, id, "DREAM").why == "not_offered");
    CHECK(env.svc->ChooseClass(kAccount, id, "").why == "not_offered");
    // Nothing changed after the refusals.
    CHECK(env.svc->Get(kAccount).run.state == ArenaState::ChoosingClass);

    ArenaOutcome chosen = env.svc->ChooseClass(kAccount, id, run.picks[0].offer[0]);
    CHECK(chosen.ok && chosen.run.state == ArenaState::Drafting);
    CHECK(env.svc->ChooseClass(kAccount, id, run.picks[0].offer[1]).why == "bad_state"); // another class
    CHECK(env.svc->BeginDuel(kAccount, id, kGuid).why == "bad_state"); // before the draft is done
    CHECK(env.svc->Pick(kAccount, id, 0, "x").why == "stale");
    CHECK(env.svc->Pick(kAccount, id, 31, "x").why == "stale");
    CHECK(env.svc->Pick(kAccount, id, 2, chosen.run.picks[1].offer[0]).why == "stale"); // not the open offer yet

    ArenaRun ready = env.svc->Get(kAccount).run;
    for (uint32_t pick = 1; pick <= 30; ++pick)
        ready = env.svc->Pick(kAccount, id, pick, ready.picks[pick].offer[0]).run;
    CHECK(ready.state == ArenaState::Ready);
    CHECK(env.svc->Pick(kAccount, id, 31, "x").why == "stale");
    CHECK(env.svc->PauseDuel(kAccount, id).why == "bad_state");
    CHECK(env.svc->ResumeDuel(kAccount, id, kGuid).why == "bad_state");
    CHECK(env.svc->RecordResult(kAccount, id, 1, ArenaDuelResult::Win, kGuid, false, kNow).why == "bad_state");
    CHECK(env.svc->Settle(kAccount, id).why == "bad_state");

    // In a duel.
    ArenaOutcome duel = env.svc->BeginDuel(kAccount, id, kGuid);
    CHECK(duel.ok);
    CHECK(env.svc->RecordResult(kAccount, id, 2, ArenaDuelResult::Win, kGuid, false, kNow).why == "stale"); // wrong match
    CHECK(env.svc->Settle(kAccount, id).why == "bad_state");
    CHECK(env.svc->Pick(kAccount, id, 30, "x").why == "picked");
    // Paused: no result can arrive.
    CHECK(env.svc->PauseDuel(kAccount, id).ok);
    CHECK(env.svc->RecordResult(kAccount, id, 1, ArenaDuelResult::Win, kGuid, false, kNow).why == "bad_state");
    CHECK(env.svc->BeginDuel(kAccount, id, kGuid).why == "bad_state"); // resume, not begin
    CHECK(env.svc->ResumeDuel(kAccount, id, kGuid).ok);

    // Finished, then settled: nothing else moves.
    ArenaOutcome lost = env.svc->RecordResult(kAccount, id, 1, ArenaDuelResult::Loss, kGuid, false, kNow);
    CHECK(lost.ok && lost.run.state == ArenaState::Finished);
    CHECK(env.svc->BeginDuel(kAccount, id, kGuid).why == "bad_state");
    CHECK(env.svc->PauseDuel(kAccount, id).why == "bad_state");
    CHECK(env.svc->ResumeDuel(kAccount, id, kGuid).why == "bad_state");
    CHECK(env.svc->Pick(kAccount, id, 1, "x").why == "picked");
    CHECK(env.svc->ClaimSpecial(kAccount, id, "EX1_062", none).why == "bad_state");
    CHECK(env.svc->Retire(kAccount, id, kGuid, false, kNow).why == "bad_state");
    CHECK(env.svc->Settle(kAccount, id).ok);
    CHECK(env.svc->Settle(kAccount, id).replay);
    CHECK(env.svc->BeginDuel(kAccount, id, kGuid).why == "bad_state");
    CHECK(env.svc->ClaimSpecial(kAccount, id, "EX1_062", none).why == "bad_state"); // no choice was pending
    CHECK(env.svc->Retire(kAccount, id, kGuid, false, kNow).why == "bad_state");
}

static void TestIdempotentPick()
{
    Env env(50);
    ArenaRun run = env.Begin();
    std::string const cls = run.picks[0].offer[2];
    ArenaOutcome c1 = env.svc->ChooseClass(kAccount, run.id, cls);
    ArenaOutcome c2 = env.svc->ChooseClass(kAccount, run.id, cls);
    CHECK(c1.ok && !c1.replay && c2.ok && c2.replay);
    CHECK(c1.run.picks.size() == 2 && c2.run.picks.size() == 2 && c1.run.picks[1].offer == c2.run.picks[1].offer);

    std::vector<std::string> const offer1 = c1.run.picks[1].offer;
    ArenaOutcome bad = env.svc->Pick(kAccount, run.id, 1, "NO_SUCH_CARD");
    CHECK(!bad.ok && bad.why == "not_offered");
    // An eligible card of the pool but not of this offer is refused too.
    std::string other;
    Hs::ArenaPool const clsPool = PoolOf(cls);
    for (std::string const& id : clsPool.Cards(PackRarity::Common))
        if (std::find(offer1.begin(), offer1.end(), id) == offer1.end())
        {
            other = id;
            break;
        }
    CHECK(!other.empty() && env.svc->Pick(kAccount, run.id, 1, other).why == "not_offered");
    CHECK(env.svc->Get(kAccount).run.picks[1].chosen.empty()); // refusals changed nothing

    ArenaOutcome p1 = env.svc->Pick(kAccount, run.id, 1, offer1[1]);
    CHECK(p1.ok && !p1.replay && p1.run.picks[1].chosen == offer1[1] && p1.run.picks.size() == 3);
    std::vector<std::string> const offer2 = p1.run.picks[2].offer;
    // The retry returns the stored state: no second card, no new offer.
    for (int i = 0; i < 3; ++i)
    {
        ArenaOutcome again = env.svc->Pick(kAccount, run.id, 1, offer1[1]);
        CHECK(again.ok && again.replay && again.run.picks.size() == 3 && again.run.picks[2].offer == offer2);
        CHECK(again.run.Deck().size() == 1);
    }
    // Another card for a pick that was answered is refused.
    CHECK(env.svc->Pick(kAccount, run.id, 1, offer1[0]).why == "picked");
    CHECK(env.svc->Get(kAccount).run.Deck().size() == 1);
    // A pick of the next offer works and the earlier one is still a replay.
    ArenaOutcome p2 = env.svc->Pick(kAccount, run.id, 2, offer2[2]);
    CHECK(p2.ok && !p2.replay && p2.run.Deck().size() == 2);
    CHECK(env.svc->Pick(kAccount, run.id, 1, offer1[1]).replay);
    CHECK(env.svc->Pick(kAccount, run.id, 2, offer2[2]).replay);
    CHECK(env.svc->Pick(kAccount, run.id, 4, offer2[0]).why == "stale");

    // A relog: a fresh service on the same storage (with another generator) shows the same offers.
    ArenaService relog(env.store);
    relog.SetData(Catalog(), Db(), "hash1");
    relog.SetRng(MakeSeededRng(999));
    ArenaOutcome seen = relog.Get(kAccount);
    CHECK(seen.ok && seen.run.picks.size() == 4 && seen.run.picks[1].offer == offer1 && seen.run.picks[2].offer == offer2);
    CHECK(seen.run.picks[3].offer == p2.run.picks[3].offer);
    CHECK(relog.Pick(kAccount, run.id, 2, offer2[2]).replay);
    CHECK(relog.Pick(kAccount, run.id, 3, p2.run.picks[3].offer[0]).ok);

    // The last pick completes the deck; repeating it is a replay of Ready.
    ArenaRun r = relog.Get(kAccount).run;
    for (uint32_t pick = 4; pick <= 30; ++pick)
        r = relog.Pick(kAccount, run.id, pick, r.picks[pick].offer[0]).run;
    CHECK(r.state == ArenaState::Ready);
    ArenaOutcome last = relog.Pick(kAccount, run.id, 30, r.picks[30].chosen);
    CHECK(last.ok && last.replay && last.run.state == ArenaState::Ready && last.run.Deck().size() == 30);
}

static void TestDeterministicDrafts()
{
    Env a(777), b(777), c(778);
    ArenaRun ra = a.Ready();
    ArenaRun rb = b.Ready();
    ArenaRun rc = c.Ready();
    CHECK(ra.deckClass == rb.deckClass && ra.Deck() == rb.Deck());
    for (std::size_t i = 0; i < ra.picks.size(); ++i)
        CHECK(ra.picks[i].offer == rb.picks[i].offer);
    CHECK(ra.picks[0].offer != rc.picks[0].offer || ra.Deck() != rc.Deck());
}

static void TestDuelPauseResume()
{
    Env env(60);
    ArenaRun ready = env.Ready();
    ArenaOutcome b = env.svc->BeginDuel(kAccount, ready.id, 1);
    CHECK(b.ok && !b.replay && b.run.matchNo == 1);
    ArenaOutcome b2 = env.svc->BeginDuel(kAccount, ready.id, 2); // a retry while the duel runs
    CHECK(b2.ok && b2.replay && b2.run.matchNo == 1 && b2.run.controllerGuid == 1);
    ArenaOutcome p = env.svc->PauseDuel(kAccount, ready.id);
    CHECK(p.ok && p.run.state == ArenaState::Paused);
    CHECK(env.svc->PauseDuel(kAccount, ready.id).replay);
    ArenaOutcome r = env.svc->ResumeDuel(kAccount, ready.id, 9); // another character resumes
    CHECK(r.ok && !r.replay && r.run.state == ArenaState::InDuel && r.run.controllerGuid == 9 && r.run.matchNo == 1);
    CHECK(env.svc->ResumeDuel(kAccount, ready.id, 9).replay);
    // The result is recorded once.
    ArenaOutcome w = env.svc->RecordResult(kAccount, ready.id, 1, ArenaDuelResult::Win, 9, false, kNow);
    CHECK(w.ok && !w.replay && w.run.wins == 1 && w.run.state == ArenaState::Ready);
    for (int i = 0; i < 3; ++i)
    {
        ArenaOutcome again = env.svc->RecordResult(kAccount, ready.id, 1, ArenaDuelResult::Win, 9, false, kNow);
        CHECK(again.ok && again.replay && again.run.wins == 1 && again.run.state == ArenaState::Ready);
    }
    // A duplicate result with a different outcome does not turn into a loss either.
    CHECK(env.svc->RecordResult(kAccount, ready.id, 1, ArenaDuelResult::Loss, 9, false, kNow).replay);
    CHECK(env.svc->Get(kAccount).run.losses == 0);
    // The next duel is number 2.
    ArenaOutcome n = env.svc->BeginDuel(kAccount, ready.id, 9);
    CHECK(n.ok && n.run.matchNo == 2);
    // The result of the finished match 1 stays a replay while duel 2 runs, and a result for duel 3 is stale.
    CHECK(env.svc->RecordResult(kAccount, ready.id, 1, ArenaDuelResult::Win, 9, false, kNow).replay);
    CHECK(env.svc->RecordResult(kAccount, ready.id, 3, ArenaDuelResult::Win, 9, false, kNow).why == "stale");
    // A loss and its replay.
    ArenaOutcome l = env.svc->RecordResult(kAccount, ready.id, 2, ArenaDuelResult::Loss, 9, false, kNow);
    CHECK(l.ok && l.run.state == ArenaState::Finished && l.run.losses == 1);
    CHECK(env.svc->RecordResult(kAccount, ready.id, 2, ArenaDuelResult::Loss, 9, false, kNow).replay);
    CHECK(env.svc->Get(kAccount).run.losses == 1);
}

static void TestFreeAndPaidSlot()
{
    Env env(70);
    // A paid entry reserves the slot and owes a debit; nothing can happen before it is confirmed.
    ArenaOutcome gold = env.svc->Start(env.Request(ArenaPay::Gold));
    CHECK(gold.ok && gold.needsDebit && !gold.replay);
    CHECK(gold.run.terms.pay == ArenaPay::Gold && gold.run.terms.payAmount == 150000 && !gold.run.entryConfirmed);
    CHECK(env.store.slots.size() == 1);
    CHECK(env.svc->ChooseClass(kAccount, gold.run.id, gold.run.picks[0].offer[0]).why == "unpaid");
    CHECK(env.svc->Retire(kAccount, gold.run.id, kGuid, false, kNow).why == "unpaid");
    // A retry with another payment returns the same entry with the original method: never a second charge.
    ArenaOutcome retry = env.svc->Start(env.Request(ArenaPay::Coins));
    CHECK(retry.ok && retry.replay && retry.needsDebit && retry.run.id == gold.run.id);
    CHECK(retry.run.terms.pay == ArenaPay::Gold && retry.run.terms.payAmount == 150000);
    CHECK(env.store.runs.size() == 1 && env.store.slots.size() == 1);
    // The failure path: abort deletes the run and releases the slot, exactly once.
    ArenaOutcome abort = env.svc->AbortEntry(kAccount, gold.run.id);
    CHECK(abort.ok && !abort.replay && env.store.runs.empty() && env.store.slots.empty());
    CHECK(env.svc->AbortEntry(kAccount, gold.run.id).replay); // a second abort is a no-op
    CHECK(env.svc->Get(kAccount).why == "no_run");

    // Pay with coins instead, confirm; the slot stays used even after the run is over.
    ArenaOutcome coins = env.svc->Start(env.Request(ArenaPay::Coins));
    CHECK(coins.ok && coins.needsDebit && coins.run.terms.payAmount == 30);
    CHECK(env.svc->ConfirmEntry(kAccount, coins.run.id).ok);
    CHECK(env.svc->ConfirmEntry(kAccount, coins.run.id).replay);
    CHECK(env.svc->AbortEntry(kAccount, coins.run.id).why == "bad_state"); // a confirmed entry cannot be aborted
    CHECK(env.store.slots.size() == 1);
    ArenaRun run = env.svc->Get(kAccount).run;
    CHECK(run.entryConfirmed && env.svc->ChooseClass(kAccount, run.id, run.picks[0].offer[0]).ok);
    CHECK(env.svc->Retire(kAccount, run.id, kGuid, false, kNow).ok);
    CHECK(env.svc->Settle(kAccount, run.id).ok);

    std::string why;
    // Today's paid slot is used, with either method.
    CHECK(!env.svc->EntryAvailable(kAccount, ArenaPay::Gold, kNow, kSwap, why) && why == "daily_used");
    CHECK(!env.svc->EntryAvailable(kAccount, ArenaPay::Coins, kNow + 3600, kSwap, why) && why == "daily_used");
    CHECK(env.svc->Start(env.Request(ArenaPay::Gold, kNow)).why == "daily_used");
    CHECK(env.svc->Start(env.Request(ArenaPay::Coins, kNow + 100)).why == "daily_used");
    CHECK(env.store.runs.size() == 1); // refusals created nothing
    // A free run does not need the slot, and does not use it.
    CHECK(env.svc->EntryAvailable(kAccount, ArenaPay::Free, kNow, kSwap, why) && why.empty());
    ArenaOutcome free = env.svc->Start(env.Request(ArenaPay::Free));
    CHECK(free.ok && !free.needsDebit && free.run.entryConfirmed && free.run.terms.pay == ArenaPay::Free &&
        free.run.terms.payAmount == 0 && free.run.slotPeriod == 0);
    CHECK(env.store.slots.size() == 1); // still only the paid one
    CHECK(env.svc->Retire(kAccount, free.run.id, kGuid, false, kNow).ok);
    CHECK(env.svc->Settle(kAccount, free.run.id).ok);
    CHECK(env.store.slots.size() == 1);
    // Practice runs: as many as wanted after the paid one, no slot, no debit, and their prize is empty.
    for (int i = 0; i < 3; ++i)
    {
        ArenaOutcome practice = env.svc->Start(env.Request(ArenaPay::Practice));
        CHECK(practice.ok && !practice.needsDebit && practice.run.entryConfirmed &&
            practice.run.terms.pay == ArenaPay::Practice && practice.run.terms.payAmount == 0 && practice.run.slotPeriod == 0);
        CHECK(env.svc->Retire(kAccount, practice.run.id, kGuid, false, kNow).ok);
        ArenaOutcome settled = env.svc->Settle(kAccount, practice.run.id);
        CHECK(settled.ok && settled.run.prize == ArenaPrize());
    }
    CHECK(env.store.slots.size() == 1);
    // Next paid entry: refused until the swap time passes.
    uint64_t const period = ArenaPeriod(kNow, kSwap);
    CHECK(!env.svc->EntryAvailable(kAccount, ArenaPay::Gold, period + 86400 - 1, kSwap, why) && why == "daily_used");
    CHECK(env.svc->EntryAvailable(kAccount, ArenaPay::Gold, period + 86400, kSwap, why));
    ArenaOutcome next = env.svc->Start(env.Request(ArenaPay::Gold, period + 86400));
    CHECK(next.ok && next.needsDebit && next.run.slotPeriod == period + 86400 && env.store.slots.size() == 2);
    CHECK(env.svc->ConfirmEntry(kAccount, next.run.id).ok);
    // Another account has its own slot.
    ArenaOutcome other = env.svc->Start(env.Request(ArenaPay::Gold, period + 86400, kAccount + 1));
    CHECK(other.ok && other.needsDebit);

    // A free run first, a paid run the same day afterwards.
    Env env2(71);
    ArenaOutcome f = env2.svc->Start(env2.Request(ArenaPay::Free));
    CHECK(f.ok && env2.svc->Retire(kAccount, f.run.id, kGuid, false, kNow).ok && env2.svc->Settle(kAccount, f.run.id).ok);
    ArenaOutcome p = env2.svc->Start(env2.Request(ArenaPay::Gold));
    CHECK(p.ok && p.needsDebit && env2.store.slots.size() == 1);
}

static void TestContinuingRunKeepsSlot()
{
    // A run continuing on a later day does not use that day's slot, and the slot of the run's own day stays used.
    Env env(75);
    ArenaOutcome start = env.svc->Start(env.Request(ArenaPay::Coins, kNow));
    CHECK(start.ok && env.svc->ConfirmEntry(kAccount, start.run.id).ok);
    ArenaRun run = env.Draft(env.svc->Get(kAccount).run);
    std::string why;
    uint64_t const tomorrow = kNow + 86400;
    CHECK(!env.svc->EntryAvailable(kAccount, ArenaPay::Coins, tomorrow, kSwap, why) && why == "open_run");
    ArenaRun later = env.Play(run, 3, false);
    CHECK(later.state == ArenaState::Ready && env.store.slots.size() == 1); // playing on consumed nothing
    CHECK(env.svc->Retire(kAccount, later.id, kGuid, false, tomorrow).ok && env.svc->Settle(kAccount, later.id).ok);
    // Tomorrow's slot is still free.
    CHECK(env.svc->EntryAvailable(kAccount, ArenaPay::Coins, tomorrow, kSwap, why));
}

static void TestOneOpenRun()
{
    Env env(80);
    std::string why;
    ArenaRun run = env.Begin(ArenaPay::Free);
    // Free or paid, a second run is refused while the first is open, in any state.
    for (ArenaPay pay : { ArenaPay::Free, ArenaPay::Gold, ArenaPay::Coins })
    {
        ArenaOutcome second = env.svc->Start(env.Request(pay));
        CHECK(!second.ok && second.why == "open_run" && second.hasRun && second.run.id == run.id);
        CHECK(!env.svc->EntryAvailable(kAccount, pay, kNow, kSwap, why) && why == "open_run");
    }
    CHECK(env.store.runs.size() == 1 && env.store.slots.empty());
    run = env.Draft(run);
    CHECK(env.svc->Start(env.Request(ArenaPay::Free)).why == "open_run");
    run = env.Play(run, 0, true); // Finished, not settled yet
    CHECK(run.state == ArenaState::Finished);
    CHECK(env.svc->Start(env.Request(ArenaPay::Free)).why == "open_run");
    CHECK(env.svc->Settle(kAccount, run.id).ok);
    // Settled and nothing pending: a new run may start.
    CHECK(env.svc->EntryAvailable(kAccount, ArenaPay::Free, kNow, kSwap, why));
    ArenaOutcome next = env.svc->Start(env.Request(ArenaPay::Free));
    CHECK(next.ok && next.run.id != run.id);
    CHECK(env.svc->Get(kAccount).run.id == next.run.id);
    CHECK(env.svc->ChooseClass(kAccount, run.id, "MAGE").why == "stale"); // the old run id is gone
    // The storage itself refuses a second open run (the unique rule), whatever the service checked.
    ArenaRun dup;
    dup.accountId = kAccount;
    dup.terms = Terms();
    CHECK(env.store.CreateRun(dup) == ArenaStoreResult::OpenRun);
}

static void TestSpecialCardClaims()
{
    Env env(90);
    Collection owned;
    std::string why;
    std::set<std::string> claimed;
    for (int round = 0; round < 4; ++round)
    {
        bool const complete = ArenaSpecialSetsComplete(owned);
        CHECK(!complete);
        ArenaRun end = env.Play(env.Ready(), 10, false, complete);
        CHECK(end.state == ArenaState::Finished && end.prize.specialChoice && end.prize.packs == 3);
        ArenaOutcome s = env.svc->Settle(kAccount, end.id);
        CHECK(s.ok && s.settlement.specialChoicePending && s.run.specialPending && s.run.Open());
        // Blocked until the choice is made.
        CHECK(env.svc->Start(env.Request(ArenaPay::Free)).why == "special_pending");
        CHECK(!env.svc->EntryAvailable(kAccount, ArenaPay::Free, kNow, kSwap, why) && why == "special_pending");
        CHECK(env.svc->ClaimSpecial(kAccount, end.id, "CS2_032", owned).why == "not_special");
        CHECK(env.svc->ClaimSpecial(kAccount, end.id + 9, "EX1_062", owned).why == "stale");
        // A card whose set is complete cannot be chosen again.
        if (!claimed.empty())
            CHECK(env.svc->ClaimSpecial(kAccount, end.id, *claimed.begin(), owned).why == "not_missing");
        std::vector<ArenaSpecialMissing> missing = ArenaMissingSpecials(owned);
        CHECK(missing.size() == static_cast<std::size_t>(4 - round));
        ArenaSpecialMissing const pick = missing.back();
        ArenaOutcome claim = env.svc->ClaimSpecial(kAccount, end.id, pick.id, owned);
        CHECK(claim.ok && !claim.replay && claim.grantCopies == pick.copies && !claim.convertedToPack);
        CHECK(claim.run.specialCard == pick.id && !claim.run.specialPending && !claim.run.Open());
        // The retry is a replay with the same grant, and another card is refused.
        ArenaOutcome again = env.svc->ClaimSpecial(kAccount, end.id, pick.id, owned);
        CHECK(again.ok && again.replay && again.grantCopies == pick.copies);
        CHECK(env.svc->ClaimSpecial(kAccount, end.id, missing.front().id, owned).why == "bad_state" ||
            missing.size() == 1);
        // The glue grants the copies.
        owned.Set(pick.id, { owned.Get(pick.id).protectedQty + pick.copies, owned.Get(pick.id).unprotectedQty });
        claimed.insert(pick.id);
    }
    CHECK(claimed.size() == 4 && ArenaSpecialSetsComplete(owned) && owned.Get("NEW1_016").Total() == 2);

    // The fifth ten-win run: all complete, four packs and no choice, so nothing blocks the next run.
    ArenaRun fifth = env.Play(env.Ready(), 10, false, ArenaSpecialSetsComplete(owned));
    CHECK(fifth.prize.packs == 4 && fifth.prize.coins == 10 && !fifth.prize.specialChoice);
    ArenaOutcome s5 = env.svc->Settle(kAccount, fifth.id);
    CHECK(s5.ok && !s5.settlement.specialChoicePending && !s5.run.Open());
    CHECK(env.svc->EntryAvailable(kAccount, ArenaPay::Free, kNow, kSwap, why));

    // Sets that became complete while the choice was pending (a GM grant): the choice turns into a pack.
    Env env2(91);
    ArenaRun end = env2.Play(env2.Ready(), 10, false, false);
    CHECK(env2.svc->Settle(kAccount, end.id).run.specialPending);
    Collection all = owned; // everything complete
    ArenaOutcome conv = env2.svc->ClaimSpecial(kAccount, end.id, "EX1_062", all);
    CHECK(conv.ok && conv.convertedToPack && conv.grantCopies == 0 && !conv.run.specialPending &&
        conv.run.prize.packs == 4 && !conv.run.prize.specialChoice && conv.run.specialCard.empty());
    CHECK(!conv.run.Open());

    // Partial ownership: one Captain's Parrot owned means one more copy is granted.
    Env env3(92);
    ArenaRun e3 = env3.Play(env3.Ready(), 10, false, false);
    CHECK(env3.svc->Settle(kAccount, e3.id).ok);
    Collection part;
    part.Set("NEW1_016", { 0, 1 });
    ArenaOutcome pc = env3.svc->ClaimSpecial(kAccount, e3.id, "NEW1_016", part);
    CHECK(pc.ok && pc.grantCopies == 1 && pc.run.specialCopies == 1);
}

static void TestStorageFailures()
{
    Env env(100);
    // A failed entry write creates nothing and returns no run.
    env.store.failWrites = true;
    ArenaOutcome start = env.svc->Start(env.Request(ArenaPay::Gold));
    CHECK(!start.ok && start.why == "storage" && !start.hasRun && env.store.runs.empty() && env.store.slots.empty());
    env.store.failWrites = false;
    env.store.failReads = true;
    CHECK(env.svc->Start(env.Request(ArenaPay::Gold)).why == "storage");
    CHECK(env.svc->Get(kAccount).why == "storage");
    std::string why;
    CHECK(!env.svc->EntryAvailable(kAccount, ArenaPay::Gold, kNow, kSwap, why) && why == "storage");
    env.store.failReads = false;

    ArenaRun run = env.Begin(ArenaPay::Free);
    // A failed save leaves the stored run untouched and does not reveal the new offer.
    env.store.failWrites = true;
    ArenaOutcome c = env.svc->ChooseClass(kAccount, run.id, run.picks[0].offer[0]);
    CHECK(!c.ok && c.why == "storage" && !c.hasRun);
    env.store.failWrites = false;
    CHECK(env.store.runs.at(run.id).state == ArenaState::ChoosingClass && env.store.runs.at(run.id).picks.size() == 1);
    ArenaOutcome c2 = env.svc->ChooseClass(kAccount, run.id, run.picks[0].offer[0]);
    CHECK(c2.ok && !c2.replay);
    env.store.failWrites = true;
    CHECK(env.svc->Pick(kAccount, run.id, 1, c2.run.picks[1].offer[0]).why == "storage");
    env.store.failWrites = false;
    CHECK(env.store.runs.at(run.id).picks[1].chosen.empty() && env.store.runs.at(run.id).picks.size() == 2);
    CHECK(env.svc->Pick(kAccount, run.id, 1, c2.run.picks[1].offer[0]).ok);

    // A failed abort keeps the unconfirmed run and its slot.
    Env env2(101);
    ArenaOutcome paid = env2.svc->Start(env2.Request(ArenaPay::Coins));
    env2.store.failWrites = true;
    CHECK(env2.svc->AbortEntry(kAccount, paid.run.id).why == "storage");
    CHECK(env2.store.runs.size() == 1 && env2.store.slots.size() == 1);
    CHECK(env2.svc->ConfirmEntry(kAccount, paid.run.id).why == "storage");
    CHECK(!env2.store.runs.at(paid.run.id).entryConfirmed);
    env2.store.failWrites = false;
    CHECK(env2.svc->AbortEntry(kAccount, paid.run.id).ok && env2.store.slots.empty());

    // A failed result write does not count the win.
    Env env3(102);
    ArenaRun ready = env3.Ready();
    ArenaOutcome b = env3.svc->BeginDuel(kAccount, ready.id, kGuid);
    env3.store.failWrites = true;
    CHECK(env3.svc->RecordResult(kAccount, ready.id, 1, ArenaDuelResult::Win, kGuid, false, kNow).why == "storage");
    env3.store.failWrites = false;
    CHECK(env3.store.runs.at(ready.id).wins == 0 && env3.store.runs.at(ready.id).state == ArenaState::InDuel);
    CHECK(env3.svc->RecordResult(kAccount, ready.id, 1, ArenaDuelResult::Win, kGuid, false, kNow).ok);
    (void)b;
}

static void TestRepair()
{
    // A run created under another dataset is kept in Repair, not played on and not scored.
    Env env(110);
    ArenaRun ready = env.Ready();
    env.svc->BeginDuel(kAccount, ready.id, kGuid);
    ArenaOutcome same = env.svc->Get(kAccount);
    CHECK(same.ok && same.run.state == ArenaState::InDuel);
    env.svc->SetData(Catalog(), Db(), "hash2"); // the dataset changed
    ArenaOutcome seen = env.svc->Get(kAccount);
    CHECK(seen.ok && seen.run.state == ArenaState::Repair && seen.run.repairFrom == ArenaState::InDuel);
    CHECK(env.store.runs.at(ready.id).state == ArenaState::Repair); // persisted
    CHECK(env.svc->PauseDuel(kAccount, ready.id).why == "repair");
    CHECK(env.svc->RecordResult(kAccount, ready.id, 1, ArenaDuelResult::Loss, kGuid, false, kNow).why == "repair");
    CHECK(env.svc->BeginDuel(kAccount, ready.id, kGuid).why == "repair");
    CHECK(env.svc->Start(env.Request(ArenaPay::Free)).why == "open_run");
    CHECK(env.store.runs.at(ready.id).wins == 0 && env.store.runs.at(ready.id).losses == 0);
    // The administrator rebinds it: it continues paused (the duel is resumed, not lost).
    ArenaOutcome rebound = env.svc->RepairRebind(kAccount, ready.id);
    CHECK(rebound.ok && rebound.run.state == ArenaState::Paused && rebound.run.terms.datasetHash == "hash2");
    CHECK(env.svc->ResumeDuel(kAccount, ready.id, kGuid).ok);
    CHECK(env.svc->RepairRebind(kAccount, ready.id).why == "bad_state");

    // The rules version counts too; a run in Repair can be retired and then pays for its wins.
    Env env2(111);
    ArenaRun run = env2.Play(env2.Ready(), 3, false);
    env2.store.runs.at(run.id).terms.rulesVersion = 99999;
    ArenaOutcome r = env2.svc->Retire(kAccount, run.id, kGuid, false, kNow);
    CHECK(r.ok && r.run.state == ArenaState::Retired && r.run.prize.packs == 3);
    CHECK(env2.svc->Settle(kAccount, run.id).ok);

    // Drafting and ready runs go to Repair as well; a finished run does not need the data.
    Env env3(112);
    ArenaRun drafting = env3.Begin();
    env3.svc->ChooseClass(kAccount, drafting.id, drafting.picks[0].offer[0]);
    env3.svc->SetData(Catalog(), Db(), "other");
    CHECK(env3.svc->Pick(kAccount, drafting.id, 1, "x").why == "repair");
    CHECK(env3.svc->Get(kAccount).run.state == ArenaState::Repair);
    Env env4(113);
    ArenaRun fin = env4.Play(env4.Ready(), 1, true);
    env4.svc->SetData(Catalog(), Db(), "other");
    CHECK(env4.svc->Settle(kAccount, fin.id).ok); // settles with the prize fixed at the end
    // An unconfirmed paid entry is not a real run yet and is not moved to Repair.
    Env env5(114);
    ArenaOutcome paid = env5.svc->Start(env5.Request(ArenaPay::Gold));
    env5.svc->SetData(Catalog(), Db(), "other");
    CHECK(env5.svc->ConfirmEntry(kAccount, paid.run.id).ok);
    CHECK(env5.svc->Get(kAccount).run.state == ArenaState::Repair); // confirmed, then checked
}

static void TestNoData()
{
    ArenaMemoryStorage store;
    ArenaService svc(store);
    ArenaStartRequest request;
    request.accountId = kAccount;
    CHECK(svc.Start(request).why == "data");
    CHECK(svc.Get(kAccount).why == "data");
    CHECK(svc.Pick(kAccount, 1, 1, "x").why == "data");
    svc.SetData(Catalog(), Db(), "h");
    request.swapSeconds = kSwap;
    request.now = kNow;
    CHECK(svc.Start(request).ok); // default settings, a free run
    svc.SetData(nullptr, nullptr, "h");
    CHECK(svc.Get(kAccount).why == "data");
}

static void TestStateCodes()
{
    for (ArenaState s : { ArenaState::ChoosingClass, ArenaState::Drafting, ArenaState::Ready, ArenaState::InDuel,
                          ArenaState::Paused, ArenaState::Finished, ArenaState::Retired, ArenaState::Settled,
                          ArenaState::Repair })
    {
        ArenaState back = ArenaState::Repair;
        CHECK(ParseArenaState(ArenaStateCode(s), back) && back == s);
    }
    ArenaState out;
    CHECK(!ParseArenaState("", out) && !ParseArenaState("DRAFT", out) && !ParseArenaState("nope", out));
    ArenaRun run;
    CHECK(run.Open() && run.OpenPick() == -1 && run.Deck().empty());
    run.state = ArenaState::Settled;
    CHECK(!run.Open());
    run.specialPending = true;
    CHECK(run.Open());
}

// ---------------------------------------------------------------------------
// Draft AI

namespace
{
    std::array<uint32_t, kPackRarityCount> const kDefaultWeights = { { 716, 228, 44, 11 } };

    std::vector<std::string> AiDeck(std::string const& cls, uint64_t seed)
    {
        return ArenaDraftDeck(cls, PoolOf(cls), kDefaultWeights, *Catalog(), *Db(), MakeSeededRng(seed));
    }

    Match::CardDef const& DefOf(std::string const& id)
    {
        static Match::CardDef const none;
        Match::CardDef const* def = Db()->Find(id);
        return def ? *def : none;
    }
}

static void TestDraftAiDeterministic()
{
    for (std::string const& cls : DeckClasses())
    {
        std::vector<std::string> const a = AiDeck(cls, 4242);
        CHECK(a.size() == kArenaDraftPicks);
        CHECK(a == AiDeck(cls, 4242));
        CHECK(a != AiDeck(cls, 4243));
    }
    // The opponent class draw is deterministic and covers the nine classes.
    std::set<std::string> seen;
    PackRng const rng = MakeSeededRng(9);
    for (int i = 0; i < 400; ++i)
        seen.insert(ArenaRandomClass(rng));
    CHECK(seen.size() == 9);
    for (std::string const& c : seen)
        CHECK(IsDeckClass(c));
    CHECK(ArenaRandomClass(MakeSeededRng(31)) == ArenaRandomClass(MakeSeededRng(31)));
}

static void TestDraftAiDecksAreLegal()
{
    std::array<uint32_t, kPackRarityCount> const legendary = { { 1, 1, 1, 100 } }; // legendary heavy offers
    int decks = 0;
    for (std::string const& cls : DeckClasses())
    {
        ArenaPool const pool = PoolOf(cls);
        for (uint64_t seed = 1; seed <= 40; ++seed)
        {
            std::vector<std::string> const deck = ArenaDraftDeck(cls, pool, seed % 5 == 0 ? legendary : kDefaultWeights,
                *Catalog(), *Db(), MakeSeededRng(seed * 7919u));
            CHECK(ValidateArenaDeck(*Catalog(), *Db(), cls, deck) == ArenaDeckWhy::None);
            ++decks;
        }
    }
    CHECK(decks == 360);
    // A pool that cannot make an offer drafts nothing.
    ArenaPool const empty = PoolOfRarities({});
    CHECK(ArenaDraftDeck("MAGE", empty, kDefaultWeights, *Catalog(), *Db(), MakeSeededRng(1)).empty());
}

static void TestDraftAiChoosesFromOffer()
{
    CHECK(ArenaDraftChoose("MAGE", {}, {}, *Catalog(), *Db(), MakeSeededRng(1)).empty());
    for (std::string const& cls : DeckClasses())
    {
        ArenaPool const pool = PoolOf(cls);
        PackRng const rng = MakeSeededRng(555);
        std::vector<std::string> picks;
        for (uint32_t pick = 0; pick < kArenaDraftPicks; ++pick)
        {
            std::vector<std::string> offer;
            CHECK(ArenaOffer(pool, kDefaultWeights, rng, offer));
            std::string const chosen = ArenaDraftChoose(cls, offer, picks, *Catalog(), *Db(), rng);
            CHECK(std::find(offer.begin(), offer.end(), chosen) != offer.end());
            picks.push_back(chosen);
        }
    }
    // One draw per offered card, so the generator is in the same place after any offer.
    PackRng const a = MakeSeededRng(77);
    PackRng const b = MakeSeededRng(77);
    ArenaDraftChoose("MAGE", { FirstCard(Rarity::Common, "MAGE"), FirstCard(Rarity::Rare, "MAGE"),
        FirstCard(Rarity::Epic, "MAGE") }, {}, *Catalog(), *Db(), a);
    b(1000);
    b(1000);
    b(1000);
    CHECK(a(1000000) == b(1000000));
}

// Average cost, early cards and minion count over many drafts of each class.
static void TestDraftAiCurve()
{
    for (std::string const& cls : DeckClasses())
    {
        double costSum = 0;
        int cards = 0;
        int fewestLow = 99;
        int fewestMinions = 99;
        for (uint64_t seed = 1; seed <= 40; ++seed)
        {
            int low = 0;
            int minions = 0;
            for (std::string const& id : AiDeck(cls, seed * 104729u))
            {
                Match::CardDef const& def = DefOf(id);
                costSum += def.cost;
                ++cards;
                low += def.cost <= 3 ? 1 : 0;
                minions += def.type == Match::CardType::Minion ? 1 : 0;
            }
            fewestLow = std::min(fewestLow, low);
            fewestMinions = std::min(fewestMinions, minions);
        }
        double const avg = costSum / cards;
        std::printf("  %-8s avg cost %.2f, fewest cards <= 3 mana %d, fewest minions %d\n", cls.c_str(), avg, fewestLow,
            fewestMinions);
        CHECK(cards == 40 * 30);
        CHECK(avg >= 2.5 && avg <= 4.5);
        CHECK(fewestLow >= 11);
        CHECK(fewestMinions >= 13);
    }
}

static void TestDraftAiScoring()
{
    // The best and the worst cards of a pool by score with no picks yet: an offer with an obvious winner.
    for (std::string const cls : { "MAGE", "WARRIOR", "HUNTER" })
    {
        ArenaPool const pool = PoolOf(cls);
        std::vector<std::pair<double, std::string>> ranked;
        for (std::size_t r = 0; r < kPackRarityCount; ++r)
            for (std::string const& id : pool.Cards(static_cast<PackRarity>(r)))
                ranked.push_back({ ArenaDraftScore(cls, id, {}, *Catalog(), *Db()), id });
        std::sort(ranked.begin(), ranked.end());
        CHECK(ranked.size() > 50);
        CHECK(ranked.back().first - ranked[1].first > kArenaDraftJitter);
        std::vector<std::string> const offer = { ranked[0].second, ranked.back().second, ranked[1].second };
        for (uint64_t seed = 1; seed <= 20; ++seed)
            CHECK(ArenaDraftChoose(cls, offer, {}, *Catalog(), *Db(), MakeSeededRng(seed)) == ranked.back().second);
    }
    CHECK(ArenaDraftScore("MAGE", "NO_SUCH_CARD", {}, *Catalog(), *Db()) < -100);

    // The curve target is a full deck and buckets by mana.
    uint32_t total = 0;
    for (uint32_t n : ArenaCurveTarget())
        total += n;
    CHECK(total == kArenaDraftPicks);
    CHECK(ArenaCurveBucket(0) == 0 && ArenaCurveBucket(1) == 0 && ArenaCurveBucket(2) == 1 && ArenaCurveBucket(7) == 6 &&
        ArenaCurveBucket(12) == 6);

    // Curve need: Wolfrider (3 mana) is worth less to a deck full of 3-drops than to one full of 2-drops.
    std::vector<std::string> const twos(6, "CS2_120"); // River Crocolisk, 2 mana
    std::vector<std::string> const threes(6, "CS2_124");
    CHECK(DefOf("CS2_120").cost == 2 && DefOf("CS2_124").cost == 3);
    CHECK(ArenaDraftScore("HUNTER", "CS2_124", twos, *Catalog(), *Db()) >
        ArenaDraftScore("HUNTER", "CS2_124", threes, *Catalog(), *Db()));

    // Race synergy: Houndmaster (its text names Beasts) is worth more to a deck of Beasts than to one of the
    // same curve without them ...
    std::vector<std::string> const beasts = { "CS2_120", "CS2_120", "CS2_120", "CS2_171" };
    std::vector<std::string> const others = { "CS2_173", "CS2_173", "CS2_173", "CS1_042" };
    CHECK(DefOf("CS2_173").cost == 2 && DefOf("CS1_042").cost == 1 && DefOf("CS2_171").cost == 1);
    CHECK(ArenaDraftScore("HUNTER", "DS1_070", beasts, *Catalog(), *Db()) >
        ArenaDraftScore("HUNTER", "DS1_070", others, *Catalog(), *Db()));
    // ... and a Beast (River Crocolisk) is worth more to a deck whose cards name Beasts than to one with the
    // same curve that does not (Houndmaster 4 mana / Starving Buzzard 2 mana against Chillwind Yeti / Bluegill).
    std::vector<std::string> const payoffs = { "DS1_070", "CS2_237", "DS1_070", "CS2_237" };
    std::vector<std::string> const plain = { "CS2_182", "CS2_173", "CS2_182", "CS2_173" };
    CHECK(DefOf("DS1_070").cost == 4 && DefOf("CS2_182").cost == 4 && DefOf("CS2_237").cost == 2);
    CHECK(ArenaDraftScore("HUNTER", "CS2_120", payoffs, *Catalog(), *Db()) >
        ArenaDraftScore("HUNTER", "CS2_120", plain, *Catalog(), *Db()));
}

// ---------------------------------------------------------------------------
// Row encodings, the opponent and duel parameters, MarkRepair and the entry order

static LoggedAction Logged(int side, Match::ActionType type, Match::EntityId source = 0, Match::EntityId target = 0)
{
    LoggedAction l;
    l.side = side;
    l.action.type = type;
    l.action.source = source;
    l.action.target = target;
    return l;
}

static void TestIdLists()
{
    std::vector<std::string> ids;
    CHECK(DecodeArenaIds("", 3, ids) && ids.empty());
    CHECK(DecodeArenaIds("CS2_182", 3, ids) && ids.size() == 1 && ids[0] == "CS2_182");
    CHECK(DecodeArenaIds("A,B_2,c3", 3, ids) && ids.size() == 3 && ids[1] == "B_2");
    CHECK(EncodeArenaIds(ids) == "A,B_2,c3" && EncodeArenaIds({}) == "");
    std::vector<std::string> const keep = ids;
    for (char const* text : { ",", "A,", ",A", "A,,B", " A", "A ", "A,B,C,D", "A-B", "A.1", "\xC3\xA9",
                              "123456789012345678901234567890123" })
    {
        CHECK(!DecodeArenaIds(text, 3, ids));
        CHECK(ids == keep); // untouched on failure
    }
    std::vector<std::string> deck;
    for (int i = 0; i < 30; ++i)
        deck.push_back("CS2_182");
    // 30 ids fit the stored column and decode back in the same order
    CHECK(EncodeArenaIds(deck).size() <= kArenaOpponentDeckBytes);
    CHECK(DecodeArenaIds(EncodeArenaIds(deck), kDeckSize, ids) && ids == deck);
    CHECK(!DecodeArenaIds(EncodeArenaIds(deck), kDeckSize - 1, ids));
    // the longest possible ids still fit the columns
    static_assert(30 * (kMaxCardIdLen + 1) <= kArenaOpponentDeckBytes + 1, "the opponent deck column");
    static_assert(3 * (kMaxCardIdLen + 1) <= kArenaOfferBytes + 1, "the offer column");
}

namespace
{
    ArenaOpponent MakeOpponent(std::string const& cls, uint64_t seed, uint32_t runId = 1, uint32_t matchNo = 1)
    {
        ArenaOpponent o;
        o.runId = runId;
        o.matchNo = matchNo;
        o.deckClass = cls;
        o.name = "Innkeeper Gryshka";
        ArenaSettings const defaults;
        o.deck = ArenaDraftDeck(cls, PoolOf(cls), defaults.rarityWeights, *Catalog(), *Db(), MakeSeededRng(seed));
        o.seed = 424242;
        o.timerMs = 7000;
        o.rulesVersion = Match::kRulesVersion;
        o.datasetHash = "hash1";
        o.startedAt = 1760000000;
        return o;
    }
}

static void TestOpponentRow()
{
    ArenaOpponent o = MakeOpponent("MAGE", 5);
    CHECK(o.deck.size() == kArenaDraftPicks);
    CHECK(ArenaOpponentOk(o, *Catalog(), *Db()));
    // the stored form round-trips in drafted order (the shuffle depends on it)
    std::vector<std::string> back;
    CHECK(DecodeArenaIds(EncodeArenaIds(o.deck), kDeckSize, back) && back == o.deck);

    ArenaOpponent bad = o;
    bad.name.clear();
    CHECK(!ArenaOpponentOk(bad, *Catalog(), *Db()));
    bad = o;
    bad.name = std::string(kArenaOpponentNameBytes + 1, 'x');
    CHECK(!ArenaOpponentOk(bad, *Catalog(), *Db()));
    bad = o;
    bad.deck.pop_back();
    CHECK(!ArenaOpponentOk(bad, *Catalog(), *Db()));
    bad = o;
    bad.deckClass = "WARLOCK"; // a Mage deck is not a Warlock deck
    CHECK(!ArenaOpponentOk(bad, *Catalog(), *Db()));
    bad = o;
    bad.deckClass = "NEUTRAL";
    CHECK(!ArenaOpponentOk(bad, *Catalog(), *Db()));
    bad = o;
    bad.deck[3] = "NOT_A_CARD";
    CHECK(!ArenaOpponentOk(bad, *Catalog(), *Db()));
    // every class drafts a playable opponent
    for (std::string const& cls : DeckClasses())
        CHECK(ArenaOpponentOk(MakeOpponent(cls, 9), *Catalog(), *Db()));
}

static void TestDuelParams()
{
    Env env(300);
    ArenaRun run = env.Ready();
    ArenaOpponent o = MakeOpponent("PRIEST", 6, run.id, run.matchNo + 1);
    MatchStartParams p = MakeArenaDuelParams(run, o, 55, "Eqwe", 919501);
    CHECK(p.arena && p.arenaRunId == run.id && p.guid == 55 && p.humanName == "Eqwe");
    CHECK(p.humanClass == run.deckClass && p.humanDeck == run.Deck() && p.humanDeck.size() == kArenaDraftPicks);
    CHECK(p.opponentClass == "PRIEST" && p.opponentDeck == o.deck && p.opponentName == o.name && p.seed == 424242);
    CHECK(p.difficulty == kArenaDifficulty && p.difficulty == "hard" && p.profile == Match::AiProfile::Hard);
    CHECK(p.meta.accountId == kAccount && p.meta.opponentEntry == 919501 && p.meta.startedAt == 1760000000);
    CHECK(p.meta.opponentDeckKey.size() <= kHistoryKeyBytes);
    // a different character resumes the same duel: only the guid and the name differ
    MatchStartParams q = MakeArenaDuelParams(run, o, 77, "Other", 919502);
    CHECK(q.guid == 77 && q.humanName == "Other" && q.seed == p.seed && q.humanDeck == p.humanDeck &&
        q.opponentDeck == p.opponentDeck && q.arenaRunId == p.arenaRunId);

    std::vector<LoggedAction> log = { Logged(1, Match::ActionType::Mulligan), Logged(0, Match::ActionType::EndTurn) };
    SuspendedDuel d = MakeSuspendedDuel(p, log, o);
    CHECK(d.params.arena && d.log.size() == 2 && d.timerMs == 7000 && d.rulesVersion == Match::kRulesVersion &&
        d.datasetHash == "hash1" && !d.over);
}

static void TestLogRows()
{
    std::vector<LoggedAction> log;
    LoggedAction mull = Logged(1, Match::ActionType::Mulligan);
    mull.action.replace = { 5, 7 };
    LoggedAction play = Logged(0, Match::ActionType::Play, 12, 3);
    play.action.position = 2;
    play.action.choice = 1;
    std::vector<LoggedAction> const original = { mull, play, Logged(1, Match::ActionType::Attack, 20, 8),
                                                 Logged(0, Match::ActionType::EndTurn) };
    std::vector<std::string> rows;
    for (LoggedAction const& l : original)
    {
        rows.push_back(EncodeAction(l));
        CHECK(!rows.back().empty() && rows.back().size() <= kArenaActionBytes);
    }
    std::size_t failedAt = 99;
    std::string error;
    CHECK(DecodeArenaLog(rows, log, failedAt, error) && log.size() == original.size());
    for (std::size_t i = 0; i < original.size(); ++i)
        CHECK(SameLoggedAction(log[i], original[i]));
    CHECK(DecodeArenaLog({}, log, failedAt, error) && log.empty());

    // a row that does not decode names its index and keeps out untouched
    std::vector<LoggedAction> keep(1, Logged(0, Match::ActionType::EndTurn));
    log = keep;
    std::vector<std::string> broken = rows;
    broken[2] = "v1,1,A,20";
    CHECK(!DecodeArenaLog(broken, log, failedAt, error) && failedAt == 2 && !error.empty() && log.size() == 1);
    broken = rows;
    broken[0] = "v2,1,M,0,0,-1,0";
    CHECK(!DecodeArenaLog(broken, log, failedAt, error) && failedAt == 0 && error == "version");
}

static void TestDuelResultWords()
{
    ArenaDuelResult r = ArenaDuelResult::Loss;
    CHECK(ArenaDuelResultOf("win", r) && r == ArenaDuelResult::Win);
    CHECK(ArenaDuelResultOf("draw", r) && r == ArenaDuelResult::Draw);
    CHECK(ArenaDuelResultOf("loss", r) && r == ArenaDuelResult::Loss);
    r = ArenaDuelResult::Win;
    for (char const* word : { "int", "", "surrender", "Win", "unknown" })
        CHECK(!ArenaDuelResultOf(word, r) && r == ArenaDuelResult::Win);
    CHECK(std::string(ArenaDuelResultCode(ArenaDuelResult::Win)) == "win");
    CHECK(std::string(ArenaDuelResultCode(ArenaDuelResult::Draw)) == "draw");
    CHECK(std::string(ArenaDuelResultCode(ArenaDuelResult::Loss)) == "loss");
}

static void TestMarkRepair()
{
    Env env(310);
    ArenaRun ready = env.Ready();
    CHECK(env.svc->BeginDuel(kAccount, ready.id, kGuid).ok);
    ArenaOutcome m = env.svc->MarkRepair(kAccount, ready.id);
    CHECK(m.ok && !m.replay && m.run.state == ArenaState::Repair && m.run.repairFrom == ArenaState::InDuel);
    CHECK(env.store.runs.at(ready.id).state == ArenaState::Repair);
    ArenaOutcome again = env.svc->MarkRepair(kAccount, ready.id);
    CHECK(again.ok && again.replay && again.run.repairFrom == ArenaState::InDuel);
    // not scored, not played on, but retirable and rebindable (a duel comes back paused)
    CHECK(env.svc->RecordResult(kAccount, ready.id, 1, ArenaDuelResult::Win, kGuid, false, kNow).why == "repair");
    ArenaOutcome rebound = env.svc->RepairRebind(kAccount, ready.id);
    CHECK(rebound.ok && rebound.run.state == ArenaState::Paused);
    CHECK(env.svc->ResumeDuel(kAccount, ready.id, kGuid).ok);

    CHECK(env.svc->MarkRepair(kAccount, ready.id + 100).why == "stale");
    // an unconfirmed entry is no run yet, a finished one is not active
    Env env2(311);
    ArenaOutcome reserved = env2.svc->Start(env2.Request(ArenaPay::Gold));
    CHECK(env2.svc->MarkRepair(kAccount, reserved.run.id).why == "unpaid");
    CHECK(env2.svc->ConfirmEntry(kAccount, reserved.run.id).ok);
    CHECK(env2.svc->MarkRepair(kAccount, reserved.run.id).ok); // a draft can be marked too
    Env env3(312);
    ArenaRun r3 = env3.Ready();
    ArenaOutcome lost = env3.Duel(r3, ArenaDuelResult::Loss);
    CHECK(lost.ok && lost.run.state == ArenaState::Finished);
    CHECK(env3.svc->MarkRepair(kAccount, r3.id).why == "bad_state");
}

namespace
{
    // The ports of the entry debit over memory storage. Every call is recorded; Commit applies the confirmation to
    // the storage (the transaction) unless it is set to fail, and may apply it and still report a failure.
    struct FakePorts : ArenaEntryPorts
    {
        Env& env;
        uint32_t runId;
        bool funds = true;
        bool begin = true;
        bool confirm = true;
        bool debit = true;
        bool commit = true;          // false: Commit reports a failure
        bool commitApplies = false;  // with commit == false: the database applied it anyway
        std::vector<std::string> calls;
        bool txOpen = false;
        bool debited = false;

        FakePorts(Env& e, uint32_t id) : env(e), runId(id) {}

        bool HasFunds() override { calls.push_back("funds"); return funds; }
        bool Begin() override { calls.push_back("begin"); txOpen = begin; return begin; }
        ArenaOutcome Confirm() override
        {
            calls.push_back("confirm");
            ArenaOutcome o;
            o.ok = confirm;
            if (!confirm)
                o.why = "storage";
            return o;
        }
        bool Debit() override { calls.push_back("debit"); debited = debit; return debit; }
        bool Commit() override
        {
            calls.push_back("commit");
            txOpen = false;
            if (commit || commitApplies)
                env.store.runs.at(runId).entryConfirmed = true;
            return commit;
        }
        void Rollback() override { calls.push_back("rollback"); txOpen = false; }
        bool Landed() override { calls.push_back("landed"); return env.store.runs.at(runId).entryConfirmed; }
        void Undo() override { calls.push_back("undo"); debited = false; }

        std::string Calls() const
        {
            std::string s;
            for (std::string const& c : calls)
                s += (s.empty() ? "" : " ") + c;
            return s;
        }
    };
}

static void TestEntryOrder()
{
    // The happy path: funds, begin, confirm, debit, commit; the run is paid and holds the slot.
    {
        Env env(320);
        ArenaOutcome start = env.svc->Start(env.Request(ArenaPay::Gold));
        CHECK(start.ok && start.needsDebit);
        FakePorts ports(env, start.run.id);
        ArenaEntryResult r = RunArenaEntry(*env.svc, ports, kAccount, start.run.id);
        CHECK(r.ok && !r.landedAfterError && !r.aborted);
        CHECK(ports.Calls() == "funds begin confirm debit commit");
        CHECK(env.store.runs.at(start.run.id).entryConfirmed && env.store.slots.size() == 1 && ports.debited);
        // a later entry is refused: the account has its open run
        CHECK(env.svc->Start(env.Request(ArenaPay::Gold)).why == "open_run");
    }
    // No funds: nothing is touched, the reservation is released once, the slot is free again.
    {
        Env env(321);
        ArenaOutcome start = env.svc->Start(env.Request(ArenaPay::Coins));
        FakePorts ports(env, start.run.id);
        ports.funds = false;
        ArenaEntryResult r = RunArenaEntry(*env.svc, ports, kAccount, start.run.id);
        CHECK(!r.ok && r.why == "funds" && r.aborted && r.abortOk);
        CHECK(ports.Calls() == "funds");
        CHECK(env.store.runs.empty() && env.store.slots.empty());
        // the slot is back: another method can be chosen now
        ArenaOutcome again = env.svc->Start(env.Request(ArenaPay::Gold));
        CHECK(again.ok && again.needsDebit && again.run.terms.pay == ArenaPay::Gold);
    }
    // The transaction cannot be opened.
    {
        Env env(322);
        ArenaOutcome start = env.svc->Start(env.Request(ArenaPay::Gold));
        FakePorts ports(env, start.run.id);
        ports.begin = false;
        ArenaEntryResult r = RunArenaEntry(*env.svc, ports, kAccount, start.run.id);
        CHECK(!r.ok && r.why == "storage" && r.aborted && r.abortOk && ports.Calls() == "funds begin");
        CHECK(env.store.runs.empty() && env.store.slots.empty());
    }
    // The confirmation cannot be queued: the queue is rolled back and nothing was debited.
    {
        Env env(323);
        ArenaOutcome start = env.svc->Start(env.Request(ArenaPay::Gold));
        FakePorts ports(env, start.run.id);
        ports.confirm = false;
        ArenaEntryResult r = RunArenaEntry(*env.svc, ports, kAccount, start.run.id);
        CHECK(!r.ok && r.why == "storage" && r.aborted && r.abortOk);
        CHECK(ports.Calls() == "funds begin confirm rollback" && !ports.debited && !ports.txOpen);
        CHECK(env.store.runs.empty() && env.store.slots.empty());
    }
    // The amount cannot be taken (the funds changed): rolled back, nothing was taken.
    {
        Env env(324);
        ArenaOutcome start = env.svc->Start(env.Request(ArenaPay::Coins));
        FakePorts ports(env, start.run.id);
        ports.debit = false;
        ArenaEntryResult r = RunArenaEntry(*env.svc, ports, kAccount, start.run.id);
        CHECK(!r.ok && r.why == "debit" && r.aborted && r.abortOk);
        CHECK(ports.Calls() == "funds begin confirm debit rollback" && !ports.debited);
        CHECK(env.store.runs.empty() && env.store.slots.empty());
    }
    // The commit fails and nothing was written: the debit is undone and the reservation released, once.
    {
        Env env(325);
        ArenaOutcome start = env.svc->Start(env.Request(ArenaPay::Gold));
        FakePorts ports(env, start.run.id);
        ports.commit = false;
        ArenaEntryResult r = RunArenaEntry(*env.svc, ports, kAccount, start.run.id);
        CHECK(!r.ok && r.why == "commit" && r.aborted && r.abortOk);
        CHECK(ports.Calls() == "funds begin confirm debit commit landed undo" && !ports.debited);
        CHECK(env.store.runs.empty() && env.store.slots.empty());
    }
    // The commit reports a failure but the database applied it: the entry stands, nothing is undone or aborted.
    {
        Env env(326);
        ArenaOutcome start = env.svc->Start(env.Request(ArenaPay::Gold));
        FakePorts ports(env, start.run.id);
        ports.commit = false;
        ports.commitApplies = true;
        ArenaEntryResult r = RunArenaEntry(*env.svc, ports, kAccount, start.run.id);
        CHECK(r.ok && r.landedAfterError && !r.aborted);
        CHECK(ports.Calls() == "funds begin confirm debit commit landed" && ports.debited);
        CHECK(env.store.runs.size() == 1 && env.store.runs.at(start.run.id).entryConfirmed && env.store.slots.size() == 1);
    }
    // The release itself fails: the reservation stays and a retry resumes it with the ORIGINAL method.
    {
        Env env(327);
        ArenaOutcome start = env.svc->Start(env.Request(ArenaPay::Gold));
        FakePorts ports(env, start.run.id);
        ports.funds = false;
        env.store.failWrites = true;
        ArenaEntryResult r = RunArenaEntry(*env.svc, ports, kAccount, start.run.id);
        env.store.failWrites = false;
        CHECK(!r.ok && r.why == "funds" && r.aborted && !r.abortOk);
        CHECK(env.store.runs.size() == 1 && env.store.slots.size() == 1);
        ArenaOutcome retry = env.svc->Start(env.Request(ArenaPay::Coins)); // another method asked for
        CHECK(retry.ok && retry.replay && retry.needsDebit && retry.run.id == start.run.id &&
            retry.run.terms.pay == ArenaPay::Gold && retry.run.terms.payAmount == start.run.terms.payAmount);
        FakePorts ports2(env, retry.run.id);
        CHECK(RunArenaEntry(*env.svc, ports2, kAccount, retry.run.id).ok);
        CHECK(env.store.runs.at(retry.run.id).entryConfirmed && env.store.slots.size() == 1);
    }
    // Server death between the reservation and the commit: the run is unconfirmed, the slot held. The next Start is a
    // replay for the original method (another account is unaffected).
    {
        Env env(328);
        ArenaOutcome start = env.svc->Start(env.Request(ArenaPay::Coins));
        CHECK(start.ok && start.needsDebit);
        ArenaOutcome after = env.svc->Start(env.Request(ArenaPay::Gold));
        CHECK(after.ok && after.replay && after.needsDebit && after.run.terms.pay == ArenaPay::Coins);
        CHECK(env.svc->Start(env.Request(ArenaPay::Gold, kNow, kAccount + 1)).needsDebit); // not blocked
        // an unconfirmed run cannot be played or retired
        CHECK(env.svc->ChooseClass(kAccount, start.run.id, start.run.picks[0].offer[0]).why == "unpaid");
        CHECK(env.svc->Retire(kAccount, start.run.id, kGuid, false, kNow).why == "unpaid");
    }
}

// ---------------------------------------------------------------------------
// Settlement: HsArenaPayout over a fake transactional database

static void TestCoinPlan()
{
    ArenaCoinPlan p = PlanArenaCoins(10, 100);
    CHECK(p.bags == 10 && p.mail.empty());
    p = PlanArenaCoins(10, 0);
    CHECK(p.bags == 0 && p.mail.size() == 1 && p.mail[0] == 10);
    p = PlanArenaCoins(10, 4);
    CHECK(p.bags == 4 && p.mail.size() == 1 && p.mail[0] == 6);
    p = PlanArenaCoins(600, 0);
    CHECK(p.mail.size() == 3 && p.mail[0] == 250 && p.mail[1] == 250 && p.mail[2] == 100);
    p = PlanArenaCoins(0, 5);
    CHECK(p.bags == 0 && p.mail.empty());

    ArenaRun run;
    run.payingGuid = 3;
    CHECK(ArenaCoinRecipient(run) == 3);
    run.controllerGuid = 2;
    CHECK(ArenaCoinRecipient(run) == 2);
    run.endedGuid = 1;
    CHECK(ArenaCoinRecipient(run) == 1);
}

static void TestSpecialOptions()
{
    Collection none;
    ArenaMilestones claimed;
    CHECK(ArenaSpecialOptions(none, claimed).size() == 4);
    claimed["NEW1_016"] = 4;
    std::vector<ArenaSpecialMissing> opts = ArenaSpecialOptions(none, claimed);
    CHECK(opts.size() == 3);
    for (ArenaSpecialMissing const& m : opts)
        CHECK(m.id != "NEW1_016");
    // Copies the account owns count toward a set; a claimed card counts as complete even when the copies are gone.
    Collection part;
    part.Set("EX1_062", { 1, 0 });
    CHECK(ArenaSpecialOptions(part, claimed).size() == 2);
    for (ArenaSpecialCard const& c : ArenaSpecialCards())
        claimed[c.id] = 9;
    CHECK(ArenaSpecialOptions(none, claimed).empty());
    CHECK(ArenaSpecialSetsComplete(ArenaEffectiveCollection(none, claimed)));
    // The effective collection never lowers what is owned.
    Collection many;
    many.Set("NEW1_016", { 0, 5 });
    CHECK(ArenaEffectiveCollection(many, claimed).Get("NEW1_016").Total() == 5);
}

namespace
{
    // The character database of the settlement: runs (in Env's memory storage), pack stock, collection, payout
    // keys, pending coin rows and milestones. Begin takes a snapshot, Rollback restores it, so a queued statement is
    // only durable after a successful Commit, like the real transaction.
    struct PayDb
    {
        Env& env;
        PackState packs;
        Collection coll;
        std::map<std::pair<uint32_t, std::string>, uint32_t> payouts; // (run, kind) -> packs
        struct Coin
        {
            uint32_t guid;
            uint32_t coins;
        };
        std::map<uint32_t, Coin> coinRows;
        ArenaMilestones milestones;

        struct Snap
        {
            std::map<uint32_t, ArenaRun> runs;
            std::set<std::pair<uint32_t, uint64_t>> slots;
            PackState packs;
            Collection coll;
            std::map<std::pair<uint32_t, std::string>, uint32_t> payouts;
            std::map<uint32_t, Coin> coinRows;
            ArenaMilestones milestones;
        };
        std::unique_ptr<Snap> snap;
        bool open = false;
        bool failBegin = false;
        bool failQueuePack = false;
        bool failQueuePayout = false;
        bool failCoinRow = false;
        bool failCard = false;
        bool failMilestone = false;
        bool commitOk = true;       // false: Commit reports a failure
        bool commitApplies = false; // with commitOk == false: the database applied it anyway
        std::vector<std::string> calls;

        explicit PayDb(Env& e) : env(e) {}

        std::unique_ptr<Snap> Take() const
        {
            std::unique_ptr<Snap> s(new Snap());
            s->runs = env.store.runs;
            s->slots = env.store.slots;
            s->packs = packs;
            s->coll = coll;
            s->payouts = payouts;
            s->coinRows = coinRows;
            s->milestones = milestones;
            return s;
        }
        void Restore(Snap const& s)
        {
            env.store.runs = s.runs;
            env.store.slots = s.slots;
            packs = s.packs;
            coll = s.coll;
            payouts = s.payouts;
            coinRows = s.coinRows;
            milestones = s.milestones;
        }
        std::string Calls() const
        {
            std::string s;
            for (std::string const& c : calls)
                s += (s.empty() ? "" : " ") + c;
            return s;
        }
    };

    struct FakeCollectionStorage : CollectionStorage
    {
        PayDb& db;
        explicit FakeCollectionStorage(PayDb& d) : db(d) {}
        bool Load(uint32_t, Collection& out) override { out = db.coll; return true; }
        bool Commit(uint32_t, Mutation const&) override { return false; } // every arena grant goes through the callbacks
        bool LoadPacks(uint32_t, PackState& out, std::vector<PackOpening>& recent) override
        {
            out = db.packs;
            recent.clear();
            return true;
        }
        bool FindPackOpening(uint32_t, std::string const&, bool& found, PackOpening&) override { found = false; return true; }
        bool CommitPackStock(uint32_t, PackStockChange const&) override { return false; }
        bool CommitPackOpen(uint32_t, PackOpenCommit const&) override { return false; }
        bool CommitCards(uint32_t, std::vector<Mutation> const&) override { return false; }
    };

    struct PayPorts : ArenaPayoutPorts
    {
        PayDb& db;
        ArenaService& svc;
        PayPorts(PayDb& d, ArenaService& s) : db(d), svc(s) {}

        bool Begin() override
        {
            db.calls.push_back("begin");
            if (db.failBegin || db.open)
                return false;
            db.open = true;
            db.snap = db.Take();
            return true;
        }
        bool Commit() override
        {
            db.calls.push_back("commit");
            db.open = false;
            if (db.commitOk || db.commitApplies)
                return db.commitOk;
            db.Restore(*db.snap);
            return false;
        }
        void Rollback() override
        {
            db.calls.push_back("rollback");
            db.open = false;
            db.Restore(*db.snap);
        }
        ArenaOutcome SettleJoined(uint32_t accountId, uint32_t runId) override
        {
            db.calls.push_back("settle");
            return svc.Settle(accountId, runId);
        }
        bool QueuePackChange(uint32_t, PackStockChange const& change) override
        {
            db.calls.push_back("pack");
            if (db.failQueuePack)
                return false;
            db.packs = change.after;
            return true;
        }
        bool QueuePayout(uint32_t runId, uint32_t, char const* kind, uint32_t packs, uint32_t, uint32_t) override
        {
            db.calls.push_back(std::string("payout-") + kind);
            if (db.failQueuePayout || db.payouts.count({ runId, kind }))
                return false;
            db.payouts[{ runId, kind }] = packs;
            return true;
        }
        bool QueueCoinDelivery(uint32_t runId, uint32_t, uint32_t guid, uint32_t coins) override
        {
            db.calls.push_back("coins");
            if (db.failCoinRow || db.coinRows.count(runId))
                return false;
            db.coinRows[runId] = { guid, coins };
            return true;
        }
        bool QueueCardMutation(uint32_t, Mutation const& m) override
        {
            db.calls.push_back("card");
            if (db.failCard)
                return false;
            db.coll.Set(m.cardId, m.after);
            db.coll.version = m.versionAfter;
            return true;
        }
        bool QueueMilestone(uint32_t, std::string const& cardId, uint32_t runId) override
        {
            db.calls.push_back("milestone");
            if (db.failMilestone || db.milestones.count(cardId))
                return false;
            db.milestones[cardId] = runId;
            return true;
        }
        bool LoadPayout(uint32_t runId, char const* kind, bool& found, uint32_t& packs) override
        {
            auto it = db.payouts.find({ runId, kind });
            found = it != db.payouts.end();
            packs = found ? it->second : 0;
            return true;
        }
        bool LoadMilestones(uint32_t, ArenaMilestones& out) override { out = db.milestones; return true; }
        bool LoadRunForClaim(uint32_t runId, bool& found, ArenaRun& out) override
        {
            auto it = db.env.store.runs.find(runId);
            found = it != db.env.store.runs.end();
            if (found)
                out = it->second;
            return true;
        }
    };

    struct PayFixture
    {
        Env env;
        PayDb db;
        FakeCollectionStorage storage;
        CollectionService coll;
        PayPorts ports;

        explicit PayFixture(uint64_t seed) : env(seed), db(env), storage(db), coll(storage), ports(db, *env.svc)
        {
            coll.SetCatalog(Catalog());
        }
        ArenaSettleResult Settle(uint32_t runId) { return SettleArenaRun(*env.svc, coll, ports, kAccount, runId); }
        ArenaClaimResult Claim(uint32_t runId, std::string const& card)
        {
            return ClaimArenaSpecial(*env.svc, coll, ports, kAccount, runId, card);
        }
        ArenaClaimResult Finish(uint32_t runId) { return FinishArenaClaim(*env.svc, coll, ports, kAccount, runId); }
        ArenaRun Stored(uint32_t runId) const { return env.store.runs.at(runId); }
        uint32_t Payout(uint32_t runId, char const* kind) const
        {
            auto it = db.payouts.find({ runId, kind });
            return it == db.payouts.end() ? 9999u : it->second;
        }
        // Fills every special set (the account owns all four reward cards).
        void OwnAllSpecials()
        {
            for (ArenaSpecialCard const& c : ArenaSpecialCards())
                db.coll.Set(c.id, { c.copies, 0 });
        }
    };
}

static void TestSettleOnce()
{
    PayFixture f(500);
    ArenaRun end = f.env.Play(f.env.Ready(), 6, true);
    CHECK(end.state == ArenaState::Finished && end.prize.packs == 3 && end.prize.coins == 10 && end.endedGuid == kGuid);
    f.db.packs.stock = 2;
    ArenaSettleResult s = f.Settle(end.id);
    CHECK(s.ok && !s.replay && !s.landedAfterError && s.packs == 3 && s.coins == 10 && s.coinGuid == kGuid &&
        s.stockAfter == 5 && !s.specialPending);
    CHECK(f.db.Calls() == "begin pack settle payout-run coins commit");
    CHECK(f.db.packs.stock == 5 && f.Payout(end.id, "run") == 3);
    CHECK(f.db.coinRows.size() == 1 && f.db.coinRows.at(end.id).guid == kGuid && f.db.coinRows.at(end.id).coins == 10);
    CHECK(f.Stored(end.id).state == ArenaState::Settled && !f.Stored(end.id).Open());

    // The same settlement again (a retry, a restart, the recovery pass): nothing is granted twice.
    f.db.calls.clear();
    ArenaSettleResult again = f.Settle(end.id);
    CHECK(again.ok && again.replay && again.packs == 0 && f.db.calls.empty());
    CHECK(f.db.packs.stock == 5 && f.db.coinRows.size() == 1 && f.db.payouts.size() == 1);
    CHECK(f.Settle(0).ok && f.Settle(0).replay);
    CHECK(f.Settle(end.id + 7).why == "stale");

    // A run that has not ended cannot be settled.
    PayFixture g(501);
    ArenaRun ready = g.env.Ready();
    ArenaSettleResult early = g.Settle(ready.id);
    CHECK(!early.ok && early.why == "bad_state" && g.db.calls.empty() && g.db.packs.stock == 0);
    PayFixture none(502);
    CHECK(none.Settle(0).why == "no_run");
}

static void TestSettleTiers()
{
    // Zero wins: nothing is granted, but the run is settled and keyed.
    {
        PayFixture f(510);
        ArenaRun end = f.env.Play(f.env.Ready(), 0, true);
        ArenaSettleResult s = f.Settle(end.id);
        CHECK(s.ok && s.packs == 0 && s.coins == 0 && f.db.packs.stock == 0 && f.db.coinRows.empty());
        CHECK(f.db.Calls() == "begin settle payout-run commit" && f.Payout(end.id, "run") == 0);
        CHECK(f.Stored(end.id).state == ArenaState::Settled && f.Settle(end.id).replay);
    }
    // One win: one pack, no coins.
    {
        PayFixture f(511);
        ArenaRun end = f.env.Play(f.env.Ready(), 1, true);
        ArenaSettleResult s = f.Settle(end.id);
        CHECK(s.ok && s.packs == 1 && s.coins == 0 && f.db.packs.stock == 1 && f.db.coinRows.empty());
    }
    // Retirement pays the wins so far, to the retiring character.
    {
        PayFixture f(512);
        ArenaRun run = f.env.Play(f.env.Ready(), 7, false);
        ArenaOutcome r = f.env.svc->Retire(kAccount, run.id, 202, false, kNow);
        CHECK(r.ok && r.run.state == ArenaState::Retired);
        ArenaSettleResult s = f.Settle(run.id);
        CHECK(s.ok && s.packs == 3 && s.coins == 10 && s.coinGuid == 202 && f.db.coinRows.at(run.id).guid == 202);
        CHECK(f.Stored(run.id).retired && f.Stored(run.id).state == ArenaState::Settled);
    }
    // Ten wins with every set complete: four packs, no choice, nothing blocks the next run.
    {
        PayFixture f(513);
        ArenaRun end = f.env.Play(f.env.Ready(), 10, false, true);
        CHECK(end.prize.packs == 4 && !end.prize.specialChoice);
        ArenaSettleResult s = f.Settle(end.id);
        CHECK(s.ok && s.packs == 4 && s.coins == 10 && !s.specialPending && f.db.packs.stock == 4);
        std::string why;
        CHECK(f.env.svc->EntryAvailable(kAccount, ArenaPay::Free, kNow, kSwap, why));
    }
    // The terms are the run's snapshot, not the current settings.
    {
        PayFixture f(514);
        ArenaStartRequest rq = f.env.Request(ArenaPay::Free);
        rq.settings.baseRewardPacks = 7;
        rq.settings.bonusCoins = 25;
        ArenaOutcome st = f.env.svc->Start(rq);
        CHECK(st.ok);
        ArenaRun run = f.env.Draft(f.env.svc->Get(kAccount).run);
        ArenaRun end = f.env.Play(run, 6, true);
        ArenaSettleResult s = f.Settle(end.id);
        CHECK(s.ok && s.packs == 7 && s.coins == 25 && f.db.packs.stock == 7 && f.db.coinRows.at(end.id).coins == 25);
    }
}

static void TestSettleFailures()
{
    // Every failure before the commit leaves nothing behind; the retry then grants exactly once.
    struct Case
    {
        char const* name;
        void (*arm)(PayDb&);
        char const* why;
    };
    Case const cases[] = {
        { "begin", [](PayDb& d) { d.failBegin = true; }, "storage" },
        { "pack row", [](PayDb& d) { d.failQueuePack = true; }, "storage" },
        { "payout key", [](PayDb& d) { d.failQueuePayout = true; }, "storage" },
        { "coin row", [](PayDb& d) { d.failCoinRow = true; }, "storage" },
        { "commit", [](PayDb& d) { d.commitOk = false; }, "commit" },
    };
    uint64_t seed = 520;
    for (Case const& c : cases)
    {
        PayFixture f(seed++);
        ArenaRun end = f.env.Play(f.env.Ready(), 6, true);
        f.db.packs.stock = 1;
        c.arm(f.db);
        ArenaSettleResult s = f.Settle(end.id);
        CHECK(!s.ok && s.why == c.why);
        CHECK(f.db.packs.stock == 1 && f.db.payouts.empty() && f.db.coinRows.empty());
        CHECK(f.Stored(end.id).state == ArenaState::Finished && f.Stored(end.id).Open());
        // blocked meanwhile, as intended
        std::string why;
        CHECK(!f.env.svc->EntryAvailable(kAccount, ArenaPay::Free, kNow, kSwap, why) && why == "open_run");
        // the next pass
        f.db.failBegin = f.db.failQueuePack = f.db.failQueuePayout = f.db.failCoinRow = false;
        f.db.commitOk = true;
        ArenaSettleResult retry = f.Settle(end.id);
        CHECK(retry.ok && !retry.replay && retry.packs == 3 && f.db.packs.stock == 4 && f.db.payouts.size() == 1 &&
            f.db.coinRows.size() == 1);
        CHECK(f.Settle(end.id).replay && f.db.packs.stock == 4);
    }
    // The commit reports an error but the database applied it: the payout key is found, nothing is granted again.
    {
        PayFixture f(530);
        ArenaRun end = f.env.Play(f.env.Ready(), 6, true);
        f.db.commitOk = false;
        f.db.commitApplies = true;
        ArenaSettleResult s = f.Settle(end.id);
        CHECK(s.ok && s.landedAfterError && s.packs == 3 && f.db.packs.stock == 3);
        f.db.commitOk = true;
        CHECK(f.Settle(end.id).replay && f.db.packs.stock == 3 && f.db.coinRows.size() == 1);
    }
    // The pack stock cannot take the packs: refused, nothing changes, the run stays open.
    {
        PayFixture f(531);
        ArenaRun end = f.env.Play(f.env.Ready(), 6, true);
        f.db.packs.stock = kMaxPackStock - 1;
        ArenaSettleResult s = f.Settle(end.id);
        CHECK(!s.ok && s.why == "overflow" && f.db.calls.empty() && f.Stored(end.id).state == ArenaState::Finished);
        f.db.packs.stock = 0;
        CHECK(f.Settle(end.id).ok);
    }
    // The payout key exists but the run is behind (not settled): only the state is finished, no second grant.
    {
        PayFixture f(532);
        ArenaRun end = f.env.Play(f.env.Ready(), 6, true);
        f.db.payouts[{ end.id, "run" }] = 3;
        f.db.packs.stock = 3;
        ArenaSettleResult s = f.Settle(end.id);
        CHECK(s.ok && s.replay && f.db.packs.stock == 3 && f.Stored(end.id).state == ArenaState::Settled);
    }
}

static void TestSpecialClaimDelivery()
{
    PayFixture f(540);
    std::set<std::string> taken;
    for (int round = 0; round < 4; ++round)
    {
        ArenaRun end = f.env.Play(f.env.Ready(), 10, false, false);
        CHECK(end.prize.specialChoice && end.prize.packs == 3);
        ArenaSettleResult s = f.Settle(end.id);
        CHECK(s.ok && s.specialPending && f.db.packs.stock == 3u * (round + 1));
        // The options exclude what was claimed already.
        std::vector<ArenaSpecialMissing> options = ArenaSpecialOptions(f.db.coll, f.db.milestones);
        CHECK(options.size() == static_cast<std::size_t>(4 - round));
        // A claimed card of an earlier run is refused, a non-special card too.
        for (std::string const& old : taken)
            CHECK(f.Claim(end.id, old).why == "not_missing");
        CHECK(f.Claim(end.id, "CS2_032").why == "not_special");
        CHECK(f.Claim(end.id + 9, options.front().id).why == "stale");
        ArenaSpecialMissing const pick = options.back();

        if (round == 0)
        {
            // A crash between recording the claim and the grant: the milestone insert fails, nothing is granted.
            f.db.failMilestone = true;
            ArenaClaimResult broken = f.Claim(end.id, pick.id);
            f.db.failMilestone = false;
            CHECK(!broken.ok && broken.why == "storage");
            CHECK(f.Stored(end.id).specialCard == pick.id && !f.Stored(end.id).specialPending);
            CHECK(f.db.milestones.empty() && f.db.coll.Get(pick.id).Total() == 0);
            // The recovery pass finishes it: exactly the recorded copies.
            ArenaClaimResult fin = f.Finish(end.id);
            CHECK(fin.ok && fin.delivered && fin.card == pick.id && fin.copies == pick.copies);
            CHECK(f.db.milestones.at(pick.id) == end.id && f.db.coll.Get(pick.id).Total() == pick.copies);
            CHECK(f.db.coll.Get(pick.id).protectedQty == pick.copies);
            ArenaClaimResult finAgain = f.Finish(end.id);
            CHECK(finAgain.ok && !finAgain.delivered && f.db.coll.Get(pick.id).Total() == pick.copies);
            // the player's own retry is a replay
            ArenaClaimResult retry = f.Claim(end.id, pick.id);
            CHECK(retry.ok && retry.replay && !retry.delivered && f.db.coll.Get(pick.id).Total() == pick.copies);
        }
        else
        {
            ArenaClaimResult c = f.Claim(end.id, pick.id);
            CHECK(c.ok && !c.replay && c.delivered && c.card == pick.id && c.copies == pick.copies && !c.convertedToPack);
            ArenaClaimResult again = f.Claim(end.id, pick.id);
            CHECK(again.ok && again.replay && !again.delivered);
            CHECK(f.db.coll.Get(pick.id).Total() == pick.copies);
            // another card after the choice is refused
            CHECK(options.size() == 1 || !f.Claim(end.id, options.front().id).ok);
        }
        taken.insert(pick.id);
        CHECK(!f.Stored(end.id).Open());
        CHECK(f.db.milestones.size() == taken.size() && f.db.packs.stock == 3u * (round + 1));
    }
    CHECK(ArenaSpecialSetsComplete(f.db.coll) && f.db.coll.Get("NEW1_016").Total() == 2);

    // The fifth ten-win run: four packs, no choice.
    ArenaRun fifth = f.env.Play(f.env.Ready(), 10, false, true);
    CHECK(fifth.prize.packs == 4 && !fifth.prize.specialChoice);
    CHECK(f.Settle(fifth.id).ok && f.db.packs.stock == 12 + 4);
}

static void TestSpecialClaimAmbiguousAndPartial()
{
    // A commit that reports an error but was applied: delivered, the milestone is the marker.
    {
        PayFixture f(550);
        ArenaRun end = f.env.Play(f.env.Ready(), 10, false, false);
        CHECK(f.Settle(end.id).ok);
        f.db.commitOk = false;
        f.db.commitApplies = true;
        ArenaClaimResult c = f.Claim(end.id, "NEW1_016");
        f.db.commitOk = true;
        CHECK(c.ok && c.delivered && c.copies == 2 && f.db.milestones.at("NEW1_016") == end.id);
        CHECK(f.db.coll.Get("NEW1_016").Total() == 2);
        CHECK(f.Claim(end.id, "NEW1_016").replay && f.db.coll.Get("NEW1_016").Total() == 2);
    }
    // A commit that fails and is not applied: the claim stays recorded, a retry grants it.
    {
        PayFixture f(551);
        ArenaRun end = f.env.Play(f.env.Ready(), 10, false, false);
        CHECK(f.Settle(end.id).ok);
        f.db.commitOk = false;
        ArenaClaimResult c = f.Claim(end.id, "EX1_112");
        f.db.commitOk = true;
        CHECK(!c.ok && c.why == "storage" && f.db.milestones.empty() && f.db.coll.Get("EX1_112").Total() == 0);
        ArenaClaimResult retry = f.Claim(end.id, "EX1_112");
        CHECK(retry.ok && retry.delivered && retry.copies == 1 && f.db.coll.Get("EX1_112").Total() == 1);
        CHECK(f.db.milestones.size() == 1);
    }
    // Partial ownership: one Captain's Parrot owned means one more copy is granted, the milestone still records it.
    {
        PayFixture f(552);
        f.db.coll.Set("NEW1_016", { 0, 1 });
        ArenaRun end = f.env.Play(f.env.Ready(), 10, false, false);
        CHECK(f.Settle(end.id).ok);
        ArenaClaimResult c = f.Claim(end.id, "NEW1_016");
        CHECK(c.ok && c.copies == 1 && f.db.coll.Get("NEW1_016").Total() == 2);
        // the reward copy is protected; the copy owned before stays what it was
        CHECK(f.db.coll.Get("NEW1_016").protectedQty == 1 && f.db.coll.Get("NEW1_016").unprotectedQty == 1);
    }
    // A card already owned in full cannot be chosen.
    {
        PayFixture f(553);
        f.db.coll.Set("EX1_062", { 1, 0 });
        ArenaRun end = f.env.Play(f.env.Ready(), 10, false, false);
        CHECK(f.Settle(end.id).ok);
        CHECK(f.Claim(end.id, "EX1_062").why == "not_missing" && f.db.milestones.empty());
    }
}

static void TestSpecialConvertedToPack()
{
    // Every set is complete by the time of the claim: the choice becomes one pack, keyed by the run.
    {
        PayFixture f(560);
        ArenaRun end = f.env.Play(f.env.Ready(), 10, false, false);
        CHECK(f.Settle(end.id).ok && f.db.packs.stock == 3);
        f.OwnAllSpecials();
        ArenaClaimResult c = f.Claim(end.id, "EX1_062");
        CHECK(c.ok && c.convertedToPack && c.delivered && c.packs == 1 && c.stockAfter == 4 && c.card.empty());
        CHECK(f.db.packs.stock == 4 && f.Payout(end.id, "convert") == 1 && f.db.milestones.empty());
        CHECK(f.Stored(end.id).prize.packs == 4 && !f.Stored(end.id).Open());
        // the retry (a lost reply): a replay, no second pack
        ArenaClaimResult again = f.Claim(end.id, "EX1_062");
        CHECK(again.ok && again.replay && again.convertedToPack && !again.delivered && f.db.packs.stock == 4);
        ArenaClaimResult fin = f.Finish(end.id);
        CHECK(fin.ok && !fin.delivered && f.db.packs.stock == 4);
    }
    // The extra pack's transaction fails after the choice was recorded: the retry grants it once.
    {
        PayFixture f(561);
        ArenaRun end = f.env.Play(f.env.Ready(), 10, false, false);
        CHECK(f.Settle(end.id).ok);
        f.OwnAllSpecials();
        f.db.failQueuePayout = true;
        ArenaClaimResult c = f.Claim(end.id, "EX1_062");
        f.db.failQueuePayout = false;
        CHECK(!c.ok && c.why == "storage" && f.db.packs.stock == 3 && f.Payout(end.id, "convert") == 9999u);
        CHECK(f.Stored(end.id).prize.packs == 4 && !f.Stored(end.id).specialPending);
        ArenaClaimResult retry = f.Finish(end.id);
        CHECK(retry.ok && retry.convertedToPack && retry.delivered && f.db.packs.stock == 4);
        CHECK(f.Claim(end.id, "EX1_062").replay && f.db.packs.stock == 4);
    }
    {
        PayFixture f(562);
        ArenaRun end = f.env.Play(f.env.Ready(), 10, false, false);
        CHECK(f.Settle(end.id).ok);
        f.OwnAllSpecials();
        f.db.commitOk = false;
        CHECK(!f.Claim(end.id, "EX1_062").ok);
        f.db.commitOk = true;
        ArenaClaimResult fin = f.Finish(end.id);
        CHECK(fin.ok && fin.delivered && fin.packs == 1 && f.db.packs.stock == 4);
        CHECK(!f.Finish(end.id).delivered && f.db.packs.stock == 4);
    }
    // Recovery of a normal settled run does nothing.
    {
        PayFixture f(563);
        ArenaRun end = f.env.Play(f.env.Ready(), 6, true);
        CHECK(f.Settle(end.id).ok);
        CHECK(f.Finish(end.id).ok && !f.Finish(end.id).delivered && f.db.packs.stock == 3);
        CHECK(f.Finish(end.id + 5).why == "no_run");
    }
}

// ---------------------------------------------------------------------------
// Arena drafts A1: owned quest (Naxxramas) cards. A run snapshots the account's owned quest cards at
// entry; the pool, the offers, the deck check and the AI opponents of that run use the snapshot only.

namespace
{
    std::vector<std::string> QuestIds()
    {
        std::vector<std::string> quest;
        Catalog()->ForEach([&](std::string const& id, CardRule const& rule)
        {
            if (rule.acquisition == Acquisition::Quest)
                quest.push_back(id);
        });
        std::sort(quest.begin(), quest.end());
        return quest;
    }

    // A collection holding one copy of each id.
    Collection OwningOf(std::vector<std::string> const& ids)
    {
        Collection c;
        for (std::string const& id : ids)
            c.Set(id, CardCount { 0, 1 });
        return c;
    }

    std::set<std::string> AllCardsOf(ArenaPool const& pool)
    {
        std::set<std::string> out;
        for (std::size_t b = 0; b < kPackRarityCount; ++b)
            for (std::string const& id : pool.Cards(static_cast<PackRarity>(b)))
                out.insert(id);
        return out;
    }

    bool IsQuest(std::string const& id)
    {
        CardRule const* rule = Catalog()->Find(id);
        return rule && rule->acquisition == Acquisition::Quest;
    }
}

static void TestOwnedQuestRules()
{
    Match::CardDb const& db = *Db();
    std::vector<std::string> const quest = QuestIds();
    CHECK(quest.size() == 30);
    ArenaQuestSet const none;
    ArenaQuestSet const all(quest.begin(), quest.end());

    // Empty snapshot: the pool is the one without quest cards, in every class.
    // With every quest card owned, a class pool gains exactly the supported quest cards of that class or NEUTRAL
    // (most of the 30 are NEUTRAL; FP1_018..FP1_026 belong to one class each).
    for (std::string const& cls : DeckClasses())
    {
        ArenaPool const base = PoolOf(cls);
        ArenaPool const empty = ArenaPool::FromCatalog(*Catalog(), db, cls, &none);
        CHECK(AllCardsOf(base) == AllCardsOf(empty) && base.Size() == empty.Size());
        std::set<std::string> expected;
        for (std::string const& id : quest)
        {
            CardRule const* r = Catalog()->Find(id);
            if (r && db.Supported(id) && (r->cardClass == cls || r->cardClass == "NEUTRAL"))
                expected.insert(id);
        }
        CHECK(!expected.empty());
        ArenaPool const owned = ArenaPool::FromCatalog(*Catalog(), db, cls, &all);
        std::set<std::string> const added = [&]
        {
            std::set<std::string> diff = AllCardsOf(owned);
            for (std::string const& id : AllCardsOf(base))
                diff.erase(id);
            return diff;
        }();
        CHECK(added == expected && owned.Size() == base.Size() + expected.size());
    }

    // One owned card: only that card is added, and only to its own rarity bucket.
    ArenaQuestSet const one = { "FP1_001" };
    CHECK(db.Supported("FP1_001"));
    ArenaPool const mage = ArenaPool::FromCatalog(*Catalog(), db, "MAGE", &one);
    CHECK(AllCardsOf(mage).count("FP1_001") == 1 && AllCardsOf(mage).count("FP1_002") == 0);
    CardRule const* owned1 = Catalog()->Find("FP1_001");
    CardRule const* unowned = Catalog()->Find("FP1_002");
    CardRule const* token = Catalog()->Find("FP1_002t");
    CardRule const* reward = Catalog()->Find("PRO_001");
    CHECK(owned1 && unowned && token && reward);
    if (!owned1 || !unowned || !token || !reward)
        return;
    CHECK(ArenaCardOk("FP1_001", *owned1, db, &one));
    CHECK(!ArenaCardOk("FP1_001", *owned1, db));       // no snapshot: the old rule
    CHECK(!ArenaCardOk("FP1_001", *owned1, db, &none)); // empty snapshot
    CHECK(!ArenaCardOk("FP1_002", *unowned, db, &one)); // never offered unless owned
    CHECK(!ArenaCardOk("FP1_002t", *token, db, &all));   // tokens stay out
    CHECK(!ArenaCardOk("PRO_001", *reward, db, &all));   // reward-only stays out
    CHECK(ArenaEligible("FP1_001", *owned1, db, "WARRIOR", &one));  // NEUTRAL: any class
    CHECK(!ArenaEligible("FP1_001", *owned1, db, "WARRIOR"));

    // The snapshot: owned quest cards (count >= 1), sorted; standard cards are not part of it.
    Collection c = OwningOf({ "FP1_003", "FP1_001" });
    c.Set("CS2_032", CardCount { 0, 1 }); // a standard card
    CHECK(ArenaOwnedQuestCards(*Catalog(), c) == std::vector<std::string>({ "FP1_001", "FP1_003" }));
    CHECK(ArenaOwnedQuestCards(*Catalog(), Collection()).empty());

    // Deck check: an owned quest card is legal only with the snapshot that owns it.
    std::vector<std::string> deck = DeckOf("MAGE", 30);
    deck[5] = "FP1_001";
    CHECK(ValidateArenaDeck(*Catalog(), db, "MAGE", deck, &one) == ArenaDeckWhy::None);
    CHECK(ValidateArenaDeck(*Catalog(), db, "MAGE", deck) == ArenaDeckWhy::Card);
    CHECK(ValidateArenaDeck(*Catalog(), db, "MAGE", deck, &none) == ArenaDeckWhy::Card);
    deck[6] = "FP1_002"; // not owned in this snapshot
    CHECK(ValidateArenaDeck(*Catalog(), db, "MAGE", deck, &one) == ArenaDeckWhy::Card);
}

static void TestOwnedQuestDeterminism()
{
    // An empty snapshot draws exactly what the run drew before same seed, same offers.
    Collection empty;
    Env plain(5);
    Env snap(5);
    ArenaStartRequest rp = plain.Request(ArenaPay::Free);
    ArenaStartRequest rs = snap.Request(ArenaPay::Free);
    rs.collection = &empty;
    ArenaOutcome op = plain.svc->Start(rp);
    ArenaOutcome os = snap.svc->Start(rs);
    CHECK(op.ok && os.ok && os.run.questCards.empty());
    CHECK(op.run.picks[0].offer == os.run.picks[0].offer);
    ArenaOutcome cp = plain.svc->ChooseClass(kAccount, op.run.id, op.run.picks[0].offer[0]);
    ArenaOutcome cs = snap.svc->ChooseClass(kAccount, os.run.id, os.run.picks[0].offer[0]);
    CHECK(cp.ok && cs.ok && cp.run.picks[1].offer == cs.run.picks[1].offer);
}

static void TestOwnedQuestOffers()
{
    // All thirty quest cards owned at entry: the drafted deck can hold them, and the deck check accepts them.
    Collection owned = OwningOf(QuestIds());
    Env env(31);
    ArenaStartRequest req = env.Request(ArenaPay::Free);
    req.collection = &owned;
    ArenaOutcome start = env.svc->Start(req);
    CHECK(start.ok && start.run.questCards.size() == 30);
    if (!start.ok)
        return;
    ArenaRun const run = start.run;
    ArenaOutcome o = env.svc->ChooseClass(kAccount, run.id, run.picks[0].offer[0]);
    CHECK(o.ok);
    std::size_t questOffered = 0;
    for (uint32_t pick = 1; pick <= kArenaDraftPicks && o.ok; ++pick)
    {
        std::string choice = o.run.picks[pick].offer[0];
        for (std::string const& id : o.run.picks[pick].offer)
            if (IsQuest(id))
            {
                choice = id; // take a quest card whenever one is offered
                ++questOffered;
                break;
            }
        o = env.svc->Pick(kAccount, run.id, pick, choice);
        CHECK(o.ok && !o.replay);
    }
    CHECK(questOffered > 0);
    CHECK(o.ok && o.run.state == ArenaState::Ready);
    std::size_t questInDeck = 0;
    for (std::string const& id : o.run.Deck())
        questInDeck += IsQuest(id) ? 1 : 0;
    CHECK(questInDeck > 0);

    // A run entered without the cards: no quest card is offered, and the same card cannot be picked.
    Env bare(31);
    ArenaOutcome b = bare.svc->Start(bare.Request(ArenaPay::Free));
    CHECK(b.ok && b.run.questCards.empty());
    ArenaOutcome bc = bare.svc->ChooseClass(kAccount, b.run.id, b.run.picks[0].offer[0]);
    CHECK(bc.ok);
    ArenaOutcome bp = bare.svc->Pick(kAccount, b.run.id, 1, "FP1_001");
    CHECK(!bp.ok && bp.why == "not_offered");
    for (std::string const& cls : DeckClasses())
        for (std::string const& id : AllCardsOf(PoolOf(cls)))
            CHECK(!IsQuest(id));
}

static void TestOwnedQuestSnapshotPersists()
{
    // The snapshot is fixed at entry: a grant made later changes nothing of the running run.
    Collection owned;
    Env env(41);
    ArenaStartRequest req = env.Request(ArenaPay::Free);
    req.collection = &owned;
    ArenaOutcome start = env.svc->Start(req);
    CHECK(start.ok && start.run.questCards.empty());
    owned.Set("FP1_001", CardCount { 0, 1 }); // granted after entry
    owned.Set("FP1_002", CardCount { 0, 2 });
    ArenaRun run = env.svc->Get(kAccount).run;
    CHECK(run.questCards.empty());
    ArenaOutcome chosen = env.svc->ChooseClass(kAccount, run.id, run.picks[0].offer[0]);
    CHECK(chosen.ok);
    ArenaOutcome bad = env.svc->Pick(kAccount, run.id, 1, "FP1_001");
    CHECK(!bad.ok && bad.why == "not_offered"); // the grant is not in the run's pool

    // A snapshot taken at entry survives a restart (a new service over the same storage) and a duel pause.
    Collection later = OwningOf({ "FP1_005", "FP1_002" });
    Env second(42);
    ArenaStartRequest req2 = second.Request(ArenaPay::Free);
    req2.collection = &later;
    ArenaOutcome s2 = second.svc->Start(req2);
    CHECK(s2.ok && s2.run.questCards == std::vector<std::string>({ "FP1_002", "FP1_005" }));
    ArenaRun ready = second.Draft(s2.run);
    CHECK(ready.questCards == std::vector<std::string>({ "FP1_002", "FP1_005" }));
    CHECK(second.svc->BeginDuel(kAccount, ready.id, kGuid).ok);
    CHECK(second.svc->PauseDuel(kAccount, ready.id).ok);

    ArenaService restarted(second.store);
    restarted.SetData(Catalog(), Db(), "hash1");
    ArenaOutcome reloaded = restarted.Get(kAccount);
    CHECK(reloaded.ok && reloaded.run.state == ArenaState::Paused);
    CHECK(reloaded.run.questCards == std::vector<std::string>({ "FP1_002", "FP1_005" }));
    CHECK(reloaded.run.QuestSet() == ArenaQuestSet({ "FP1_002", "FP1_005" }));
    // The restored run keeps its pool: its deck check still accepts the owned card.
    std::vector<std::string> deck = DeckOf("MAGE", 30);
    deck[7] = "FP1_005";
    ArenaQuestSet const restoredSet = reloaded.run.QuestSet();
    CHECK(ValidateArenaDeck(*Catalog(), *Db(), "MAGE", deck, &restoredSet) == ArenaDeckWhy::None);
}

static void TestOwnedQuestDraftAi()
{
    // The AI opponent drafts from the same snapshot as the player: with every quest card owned the deck can hold
    // them, the shared pool never does, and an opponent is playable only with its run's snapshot.
    std::vector<std::string> const quest = QuestIds();
    ArenaQuestSet const set(quest.begin(), quest.end());
    ArenaPool const pool = ArenaPool::FromCatalog(*Catalog(), *Db(), "MAGE", &set);
    std::vector<std::string> const drafted = ArenaDraftDeck("MAGE", pool, ArenaSettings().rarityWeights, *Catalog(),
        *Db(), MakeSeededRng(9));
    CHECK(drafted.size() == kArenaDraftPicks);
    std::size_t questIn = 0;
    for (std::string const& id : drafted)
        questIn += IsQuest(id) ? 1 : 0;
    CHECK(questIn > 0);
    CHECK(ValidateArenaDeck(*Catalog(), *Db(), "MAGE", drafted, &set) == ArenaDeckWhy::None);
    CHECK(ValidateArenaDeck(*Catalog(), *Db(), "MAGE", drafted) == ArenaDeckWhy::Card);

    ArenaPool const shared = PoolOf("MAGE");
    std::vector<std::string> const plain = ArenaDraftDeck("MAGE", shared, ArenaSettings().rarityWeights, *Catalog(),
        *Db(), MakeSeededRng(9));
    for (std::string const& id : plain)
        CHECK(!IsQuest(id));

    ArenaOpponent opp;
    opp.runId = 1;
    opp.matchNo = 1;
    opp.deckClass = "MAGE";
    opp.name = "Arena Opponent";
    opp.deck = drafted;
    CHECK(ArenaOpponentOk(opp, *Catalog(), *Db(), &set));
    CHECK(!ArenaOpponentOk(opp, *Catalog(), *Db()));
}

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    RUN(TestSettings);
    RUN(TestTerms);
    RUN(TestPrizeTiers);
    RUN(TestSpecialCards);
    RUN(TestPeriod);
    RUN(TestEligibility);
    RUN(TestOffersDistinctAndDeterministic);
    RUN(TestOfferRarityWeights);
    RUN(TestOfferRarityFallback);
    RUN(TestClassOffer);
    RUN(TestDeckValidation);
    RUN(TestQuestCardsStayOutOfPools);
    RUN(TestRunHappyPath);
    RUN(TestEveryPrizeTier);
    RUN(TestTermsSnapshot);
    RUN(TestRetire);
    RUN(TestIllegalTransitions);
    RUN(TestIdempotentPick);
    RUN(TestDeterministicDrafts);
    RUN(TestDuelPauseResume);
    RUN(TestFreeAndPaidSlot);
    RUN(TestContinuingRunKeepsSlot);
    RUN(TestOneOpenRun);
    RUN(TestSpecialCardClaims);
    RUN(TestStorageFailures);
    RUN(TestRepair);
    RUN(TestNoData);
    RUN(TestStateCodes);
    RUN(TestDraftAiDeterministic);
    RUN(TestDraftAiDecksAreLegal);
    RUN(TestDraftAiChoosesFromOffer);
    RUN(TestDraftAiCurve);
    RUN(TestDraftAiScoring);
    RUN(TestIdLists);
    RUN(TestOpponentRow);
    RUN(TestDuelParams);
    RUN(TestLogRows);
    RUN(TestDuelResultWords);
    RUN(TestMarkRepair);
    RUN(TestEntryOrder);
    RUN(TestCoinPlan);
    RUN(TestSpecialOptions);
    RUN(TestSettleOnce);
    RUN(TestSettleTiers);
    RUN(TestSettleFailures);
    RUN(TestSpecialClaimDelivery);
    RUN(TestSpecialClaimAmbiguousAndPartial);
    RUN(TestSpecialConvertedToPack);
    RUN(TestOwnedQuestRules);
    RUN(TestOwnedQuestDeterminism);
    RUN(TestOwnedQuestOffers);
    RUN(TestOwnedQuestSnapshotPersists);
    RUN(TestOwnedQuestDraftAi);
    std::printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
