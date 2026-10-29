// Standalone tests of the Battlegrounds shop, pool and game loop (src/battlegrounds: HsBgPool, HsBgShop,
// HsBgGame on top of the core types, effect registry and combat simulator).
//   bg_shop_tests.exe [directory]      run all tests; exit code 0 = all checks passed.
//
// The sample effects below exist only to prove that the shop hooks fire; they are registered into test registries,
// never into the real minion set.

#include "../src/battlegrounds/HsBgCombat.h"
#include "../src/battlegrounds/HsBgEffects.h"
#include "../src/battlegrounds/HsBgGame.h"
#include "../src/battlegrounds/HsBgPool.h"
#include "../src/battlegrounds/HsBgShop.h"
#include "../src/battlegrounds/HsBgTypes.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace Hs::Bg;

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
    // ------------------------------------------------------------------------------------ helpers

    struct HookEvent
    {
        std::string hook;
        int card = -1;
        bool golden = false;
        int position = 0;
        int otherCard = -1;
        int targetCard = -1;
        bool operator==(HookEvent const& o) const { return hook == o.hook && card == o.card && golden == o.golden; }
    };
    std::vector<HookEvent> g_events;

    void Log(char const* hook, ShopCtx& c)
    {
        HookEvent e;
        e.hook = hook;
        e.card = c.self ? c.self->card : -1;
        e.golden = c.golden;
        e.position = c.position;
        e.otherCard = c.other ? c.other->card : -1;
        e.targetCard = c.target ? c.target->card : -1;
        g_events.push_back(e);
    }
    int Count(char const* hook)
    {
        int n = 0;
        for (HookEvent const& e : g_events)
            if (e.hook == hook)
                ++n;
        return n;
    }
    HookEvent const* Last(char const* hook)
    {
        for (auto it = g_events.rbegin(); it != g_events.rend(); ++it)
            if (it->hook == hook)
                return &*it;
        return nullptr;
    }

    // The cards used as hosts for sample effects: distinct tier 1 slice cards, none of them Magnetic.
    int HostCard(int n)
    {
        std::vector<int> const t1 = CardsOfTier(1, true);
        int seen = 0;
        for (int c : t1)
        {
            if (CardAt(c).keywords & kKwMagnetic)
                continue;
            if (seen++ == n)
                return c;
        }
        return -1;
    }
    enum { H_BC, H_BUY, H_SELL, H_START, H_END, H_REFRESH, H_PLAYED, H_SUMMONED, H_UPGRADE, H_AURA, H_COUNT };

    void BcHook(ShopCtx& c, bool) { Log("battlecry", c); if (c.target) c.Buff(*c.target, 2, 2); }
    void BuyHook(ShopCtx& c, bool) { Log("buy", c); }
    void SellHook(ShopCtx& c, bool) { Log("sell", c); }
    void StartHook(ShopCtx& c, bool) { Log("start", c); c.AddGold(1); }
    void EndHook(ShopCtx& c, bool) { Log("end", c); if (Minion* m = c.Self()) c.Buff(*m, 1, 1); }
    void RefreshHook(ShopCtx& c, bool) { Log("refresh", c); }
    void PlayedHook(ShopCtx& c, bool) { Log("played", c); }
    void SummonedHook(ShopCtx& c, bool) { Log("summoned", c); }
    void UpgradeHook(ShopCtx& c, bool) { Log("upgrade", c); if (Minion* m = c.Self()) c.GiveKeywords(*m, kKwTaunt); }
    void AuraHook(AuraCtx& a, bool golden)    // +1 attack (+2 golden) to every OTHER minion
    {
        for (int i = 0; i < a.board.Size(); ++i)
            if (i != a.index)
                a.Give(i, golden ? 2 : 1, 0);
    }

    EffectRegistry const& Samples()
    {
        static EffectRegistry const reg = []
        {
            EffectRegistry r;
            EffectDef d;
            d.battlecry = &BcHook;        r.Register(CardAt(HostCard(H_BC)).id, d);
            d = EffectDef(); d.onBuy = &BuyHook;                 r.Register(CardAt(HostCard(H_BUY)).id, d);
            d = EffectDef(); d.onSell = &SellHook;               r.Register(CardAt(HostCard(H_SELL)).id, d);
            d = EffectDef(); d.onStartOfTurn = &StartHook;       r.Register(CardAt(HostCard(H_START)).id, d);
            d = EffectDef(); d.onEndOfTurn = &EndHook;           r.Register(CardAt(HostCard(H_END)).id, d);
            d = EffectDef(); d.onRefresh = &RefreshHook;         r.Register(CardAt(HostCard(H_REFRESH)).id, d);
            d = EffectDef(); d.onFriendlyPlayed = &PlayedHook;   r.Register(CardAt(HostCard(H_PLAYED)).id, d);
            d = EffectDef(); d.onFriendlySummonedShop = &SummonedHook; r.Register(CardAt(HostCard(H_SUMMONED)).id, d);
            d = EffectDef(); d.onTavernUpgrade = &UpgradeHook;   r.Register(CardAt(HostCard(H_UPGRADE)).id, d);
            d = EffectDef(); d.aura = &AuraHook;                 r.Register(CardAt(HostCard(H_AURA)).id, d);
            return r;
        }();
        return reg;
    }

    // A small lobby: a seeded pool with every playable tribe (so a test may name any slice card) and a shop of
    // `players` players.
    struct Env
    {
        Rng rng;
        PoolSet pool;
        Shop shop;
        explicit Env(uint32_t seed = 1, int players = 2, EffectRegistry const& reg = Samples())
            : rng(seed), shop(pool, reg, rng, players)
        {
            pool.BuildLobby(rng, 0, true, 16);
        }
        PlayerShop& S(int p = 0) { return shop.State(p); }
    };

    bool Conserved(Env& e)
    {
        std::string why;
        bool const ok = e.shop.CheckConservation(&why);
        if (!ok)
            std::printf("    conservation: %s\n", why.c_str());
        return ok;
    }

    // Puts a minion straight into a zone (a pool copy when poolCopies > 0), returns its uid.
    uint32_t Put(Env& e, int p, Zone z, int card, bool golden = false, int poolCopies = 0)
    {
        if (poolCopies > 0)
            for (int i = 0; i < poolCopies; ++i)
                e.pool.Take(card);
        Minion m = MakeMinion(card, golden, e.shop.NewUid());
        m.poolCopies = poolCopies;
        if (z == Zone::Board)
            e.S(p).board.Push(m);
        else
            e.S(p).hand.Push(m);
        return m.uid;
    }

    // Replaces an offer by a pool copy of `card` (the old copy goes back).
    void ForceOffer(Env& e, int p, int index, int card)
    {
        Minion& old = e.S(p).offers[static_cast<std::size_t>(index)];
        e.pool.Return(old.card, old.poolCopies);
        e.pool.Take(card);
        Minion m = MakeMinion(card, false, e.shop.NewUid());
        m.poolCopies = 1;
        old = m;
    }

    int MechCard(bool magnetic, int skip = 0)
    {
        int seen = 0;
        for (int c : CardsOfTribe(kTribeMech, true))
        {
            bool const mag = (CardAt(c).keywords & kKwMagnetic) != 0;
            if (mag == magnetic && seen++ == skip)
                return c;
        }
        return -1;
    }

    // ------------------------------------------------------------------------------------ pool

    void TestPoolCopies()
    {
        CHECK(CopiesForTier(1) == 15 && CopiesForTier(2) == 15 && CopiesForTier(3) == 13);
        CHECK(CopiesForTier(4) == 11 && CopiesForTier(5) == 9 && CopiesForTier(6) == 7);
        CHECK(CopiesForTier(0) == 0 && CopiesForTier(7) == 0);
        // A faction pool (four players) halves the copies, rounded up.
        CHECK(FactionCopiesForTier(1) == 8 && FactionCopiesForTier(2) == 8 && FactionCopiesForTier(3) == 7);
        CHECK(FactionCopiesForTier(4) == 6 && FactionCopiesForTier(5) == 5 && FactionCopiesForTier(6) == 4);
        CHECK(FactionCopiesForTier(0) == 0 && FactionCopiesForTier(7) == 0);
        // ... and a Demon has kFactionDemonCopiesLess fewer there (Imprisoner, tier 1; Famished Felbat, tier 6).
        CHECK(FactionCopiesFor(FindCard("BGS_014")) == 6 && FactionCopiesFor(FindCard("BG21_005")) == 2);
        CHECK(FactionCopiesFor(FindCard("BGS_004")) == 8);   // Wrath Weaver is no Demon

        Rng rng(7);
        PoolSet pool;
        std::vector<uint32_t> const tribes = pool.BuildLobby(rng);
        CHECK(tribes.size() == kTribesPerLobby);           // 5 of the playable tribes (5 of 7)
        uint32_t const mask = [&tribes]()
        {
            uint32_t m = 0;
            for (uint32_t t : tribes)
                m |= t;
            return m;
        }();
        int slice = 0;
        int total = 0;
        for (int i = 0; i < CardCount(); ++i)
        {
            CardDef const& c = CardAt(i);
            bool const expected = CardInLobby(c, mask) && c.slice15A;
            CHECK(pool.Has(i) == expected);                // pool cards of the rolled tribes only, no tokens
            if (expected)
            {
                ++slice;
                CHECK(pool.Left(i) == CopiesForTier(c.tier) && pool.Initial(i) == pool.Left(i));
                total += pool.Left(i);
                CHECK(pool.HomePool(i) == 0);
            }
            else
                CHECK(pool.Left(i) == 0 && pool.HomePool(i) == -1);
        }
        CHECK(slice > 0 && static_cast<int>(pool.Cards().size()) == slice);
        CHECK(pool.TotalLeft() == total);
        // Tribeless and the All minion are in.
        bool tribeless = false;
        bool all = false;
        for (int c : pool.Cards())
        {
            tribeless = tribeless || CardAt(c).tribes == 0;
            all = all || (CardAt(c).tribes & kTribeAll) != 0;
        }
        CHECK(tribeless && all);
    }

    void TestLobbyTribes()
    {
        std::vector<uint32_t> const slice = SliceTribes(true);
        CHECK(slice.size() == 9);
        CHECK(slice[0] == kTribeBeast && slice[1] == kTribeDemon && slice[2] == kTribeDragon &&
            slice[3] == kTribeElemental && slice[4] == kTribeMech && slice[5] == kTribeMurloc &&
            slice[6] == kTribeNaga && slice[7] == kTribePirate && slice[8] == kTribeQuilboar);
        Rng a(3);
        CHECK(ChooseTribes(a, slice).size() == kTribesPerLobby);   // 5 of the playable tribes

        // All nine tribes (a later slice): 5 of 9, deterministic per seed, different across seeds.
        std::vector<uint32_t> nine;
        for (uint32_t bit = 1; bit < kTribeAll; bit <<= 1)
            nine.push_back(bit);
        CHECK(nine.size() == 9);
        std::set<std::vector<uint32_t>> seen;
        for (uint32_t seed = 1; seed <= 40; ++seed)
        {
            Rng r1(seed);
            Rng r2(seed);
            std::vector<uint32_t> const p = ChooseTribes(r1, nine);
            CHECK(p.size() == 5 && p == ChooseTribes(r2, nine));
            CHECK(std::set<uint32_t>(p.begin(), p.end()).size() == 5);
            CHECK(std::is_sorted(p.begin(), p.end()));
            seen.insert(p);
        }
        CHECK(seen.size() > 10);

        // A lobby of a tribe subset holds only those tribes, the tribeless and the All minion.
        PoolSet pool;
        Rng rng(1);
        pool.BuildLobby(rng, 0, true, 2);
        std::set<uint32_t> tribes;
        for (int c : pool.Cards())
            if (CardAt(c).tribes != 0 && !(CardAt(c).tribes & kTribeAll))
                tribes.insert(CardAt(c).tribes);
        CHECK(tribes.size() == 2);
        bool amalgam = false;
        for (int c : pool.Cards())
            amalgam = amalgam || (CardAt(c).tribes & kTribeAll) != 0;
        CHECK(amalgam);
    }

    void TestPoolDrawAndReturn()
    {
        Rng rng(11);
        PoolSet pool;
        pool.BuildLobby(rng);
        std::vector<int> const ids = { 0 };
        int const start = pool.TotalLeft();

        // A draw respects the tier range.
        for (int tier = 1; tier <= 6; ++tier)
            for (int i = 0; i < 30; ++i)
            {
                int const c = pool.Draw(rng, ids, 1, tier);
                CHECK(c >= 0 && CardAt(c).tier <= tier);
                pool.Return(c);
            }
        CHECK(pool.TotalLeft() == start);
        for (int i = 0; i < 30; ++i)
        {
            int const c = pool.Draw(rng, ids, 3, 3);
            CHECK(c >= 0 && CardAt(c).tier == 3);
            pool.Return(c);
        }
        // Tribe filter.
        int const m = pool.Draw(rng, ids, 1, 6, kTribeMech);
        CHECK(m >= 0 && HasTribe(CardAt(m).tribes, kTribeMech));
        pool.Return(m);

        // Draws remove copies, returns restore them, never above the initial count. Pick a tier-6 card that the
        // lobby actually stocks (a random 5-of-8 tribe lobby may leave a given tribe out).
        int card = -1;
        for (int c : pool.Cards())
            if (CardAt(c).tier == 6)
            {
                card = c;
                break;
            }
        CHECK(card >= 0);
        for (int i = 0; i < 7; ++i)
            CHECK(pool.Take(card));
        CHECK(pool.Left(card) == 0 && !pool.Take(card));
        pool.Return(card, 3);
        CHECK(pool.Left(card) == 3);
        pool.Return(card, 100);
        CHECK(pool.Left(card) == 7);
        pool.Return(CardsOfTier(1, false)[0] + 0, 0);     // a zero return is ignored
        pool.Return(-1, 3);                                // so is a bad card
        CHECK(pool.Left(card) == 7);

        // An exhausted tier range returns -1; a card with no copy left is never drawn.
        PoolSet small;
        small.AddCards(0, { card });
        for (int i = 0; i < 7; ++i)
            CHECK(small.Draw(rng, ids, 1, 6) == card);
        CHECK(small.Draw(rng, ids, 1, 6) == -1);

        // Distinct draws.
        for (int i = 0; i < 50; ++i)
        {
            std::vector<int> const d = pool.DrawDistinct(rng, ids, 2, 2, 0, 3);
            CHECK(d.size() == 3);
            CHECK(std::set<int>(d.begin(), d.end()).size() == 3);
            for (int c : d)
            {
                CHECK(CardAt(c).tier == 2);
                pool.Return(c);
            }
        }
        PoolSet two;
        int second = -1;
        for (int c : CardsOfTier(6, true))
            if (c != card)
            {
                second = c;
                break;
            }
        CHECK(second >= 0);
        two.AddCards(0, { card, second });
        CHECK(two.DrawDistinct(rng, ids, 6, 6, 0, 3).size() == 2); // only two different cards exist
        CHECK(pool.TotalLeft() == start);
    }

    void TestPoolWeighted()
    {
        // A card with more copies left is likelier: 15 copies of A against 1 copy of B.
        std::vector<int> const t1 = CardsOfTier(1, true);
        std::vector<int> const ids = { 0 };
        PoolSet pool;
        pool.AddCards(0, { t1[0], t1[1] });
        for (int i = 0; i < 14; ++i)
            pool.Take(t1[1]);
        CHECK(pool.Left(t1[0]) == 15 && pool.Left(t1[1]) == 1);
        Rng rng(5);
        int a = 0;
        int b = 0;
        for (int i = 0; i < 1600; ++i)
        {
            int const c = pool.Draw(rng, ids, 1, 1);
            (c == t1[0] ? a : b)++;
            pool.Return(c);
        }
        CHECK(a > 1300 && a < 1550 && b > 50);
    }

    void TestPoolIdsSeam()
    {
        // One pool per faction shop. A draw sees only the pools it is given; the same card may be stocked in more
        // than one pool, each with its own copies (AddCards keyed by (pool, card)).
        std::vector<int> const t2 = CardsOfTier(2, true);
        PoolSet pool;
        pool.AddCards(0, { t2[0], t2[1] });
        pool.AddCards(1, { t2[2], t2[3] });
        pool.AddCards(2, { t2[4], t2[0] });               // t2[0] is stocked again in pool 2
        CHECK(pool.PoolCount() == 3);
        CHECK(pool.HomePool(t2[0]) == 0 && pool.HomePool(t2[2]) == 1 && pool.HomePool(t2[4]) == 2);
        CHECK(pool.HasIn(0, t2[0]) && pool.HasIn(2, t2[0]) && !pool.HasIn(1, t2[0]));
        CHECK(pool.LeftIn(0, t2[0]) == 15 && pool.LeftIn(2, t2[0]) == 15);
        CHECK(pool.Initial(t2[0]) == 30);                  // the two pools have their own copies
        Rng rng(2);
        for (int i = 0; i < 100; ++i)
        {
            int const c = pool.Draw(rng, { 1 }, 1, 6);
            CHECK(c == t2[2] || c == t2[3]);
            pool.Return(c, 1, 1);
            int const d = pool.Draw(rng, { 0 }, 1, 6);
            CHECK(d == t2[0] || d == t2[1]);
            pool.Return(d, 0, 1);
            int const e2 = pool.Draw(rng, { 2 }, 1, 6);
            CHECK(e2 == t2[4] || e2 == t2[0]);
            pool.Return(e2, 2, 1);
        }
        CHECK(pool.Draw(rng, { 5 }, 1, 6) == -1);
        // A return names the pool: each pool keeps its own count.
        pool.Take(t2[0], 2);
        CHECK(pool.LeftIn(2, t2[0]) == 14 && pool.LeftIn(0, t2[0]) == 15);
        pool.Return(t2[0], 2, 2);
        CHECK(pool.LeftIn(2, t2[0]) == 15 && pool.Left(t2[3]) == 15);
        // HomeIn picks the given pool that holds the card (the shop's return-pool lookup).
        CHECK(pool.HomeIn(t2[0], { 1, 2 }) == 2 && pool.HomeIn(t2[0], { 1 }) == -1);
    }

    // ------------------------------------------------------------------------------------ sending

    void TestSend()
    {
        // One free send per shop turn; the minion keeps its stats and can complete a triple for the ally.
        Env e(51, 2);
        e.shop.StartTurn(0, 1);
        e.shop.StartTurn(1, 1);
        int const card = CardsOfTier(1, true)[0];
        Put(e, 0, Zone::Hand, card, false, 1);
        Minion const before = e.S(0).hand[0];
        CHECK(e.shop.Send(0, 5, 1) == ActionResult::BadIndex);
        CHECK(e.shop.Send(0, 0, 0) == ActionResult::NotAlly);
        CHECK(e.shop.Send(0, 0, 9) == ActionResult::NotAlly);
        CHECK(e.S(0).hand.Size() == 1);                       // refusals changed nothing
        CHECK(e.shop.Send(0, 0, 1) == ActionResult::Ok);
        CHECK(e.S(0).hand.Empty() && e.S(1).hand.Size() == 1);
        CHECK(e.S(1).hand[0].card == before.card && e.S(1).hand[0].attack == before.attack &&
            e.S(1).hand[0].maxHealth == before.maxHealth);
        CHECK(e.shop.CheckConservation());
        Put(e, 0, Zone::Hand, CardsOfTier(1, true)[1], false, 1);
        CHECK(e.shop.Send(0, 0, 1) == ActionResult::SendUsed);  // once per shop turn
        CHECK(e.S(0).hand.Size() == 1 && e.shop.CheckConservation());
        e.shop.StartTurn(0, 2);                                 // a new turn resets it
        Put(e, 0, Zone::Hand, CardsOfTier(2, true)[0], false, 1);
        CHECK(e.shop.Send(0, 0, 1) == ActionResult::Ok);

        // A full ally hand refuses the send (nothing changes).
        Env f(52, 2);
        f.shop.StartTurn(0, 1);
        f.shop.StartTurn(1, 1);
        std::vector<int> const t1 = CardsOfTier(1, true);
        for (int i = 0; i < kHandMax; ++i)
            Put(f, 1, Zone::Hand, t1[static_cast<std::size_t>(i)], true, 0);   // golden: never triples
        CHECK(f.S(1).hand.Full());
        Put(f, 0, Zone::Hand, t1[static_cast<std::size_t>(kHandMax)], false, 1);
        CHECK(f.shop.Send(0, 0, 1) == ActionResult::HandFull);
        CHECK(f.S(0).hand.Size() == 1 && f.S(1).hand.Size() == kHandMax);
        CHECK(f.shop.CheckConservation());

        // A send that completes a triple: the golden minion keeps all three copies.
        Env t(53, 2);
        t.shop.StartTurn(0, 1);
        t.shop.StartTurn(1, 1);
        int const x = CardsOfTier(3, true)[0];
        Put(t, 1, Zone::Hand, x, false, 1);
        Put(t, 1, Zone::Hand, x, false, 1);
        Put(t, 0, Zone::Hand, x, false, 1);
        CHECK(t.shop.Send(0, 0, 1) == ActionResult::Ok);
        CHECK(t.S(1).hand.Size() == 1 && t.S(1).hand[0].golden && t.S(1).hand[0].card == x);
        CHECK(t.S(1).hand[0].poolCopies == kTripleSize);
        CHECK(t.shop.CheckConservation());

        // The game enforces the faction and life rules.
        GameSetup s;
        s.seed = 44;
        s.autoRun = false;
        for (int p = 0; p < kPlayers; ++p)
            s.kinds[p] = PlayerKind::External;
        Game g(s);
        CHECK(g.Buy(0, 0) == ActionResult::Ok);
        CHECK(g.Send(-1, 0, 1) == ActionResult::BadPlayer);
        CHECK(g.Send(0, 0, 0) == ActionResult::NotAlly);
        CHECK(g.Send(0, 0, 4) == ActionResult::NotAlly);          // player 4 is the other faction
        CHECK(g.Send(0, 0, 1) == ActionResult::Ok);
        CHECK(g.GetShop().CheckConservation());
    }

    // ------------------------------------------------------------------------------------ gold, upgrades, offers

    void TestGold()
    {
        CHECK(GoldForTurn(1) == 3 && GoldForTurn(2) == 4 && GoldForTurn(4) == 6 && GoldForTurn(8) == 10 &&
            GoldForTurn(9) == 10 && GoldForTurn(40) == 10);
        Env e;
        e.shop.StartTurn(0, 1);
        CHECK(e.S().gold == 3);
        CHECK(e.shop.Buy(0, 0) == ActionResult::Ok && e.S().gold == 0);
        e.shop.StartTurn(0, 2);
        CHECK(e.S().gold == 4);                            // unspent gold is not carried: it is set
        e.shop.StartTurn(0, 3);
        CHECK(e.S().gold == 5);
        e.shop.StartTurn(0, 8);
        CHECK(e.S().gold == 10);
        e.shop.StartTurn(0, 12);
        CHECK(e.S().gold == 10);
        // Above 10 inside the turn: allowed; the next start resets to 10. Never below 0.
        e.shop.AddGold(0, 5);
        CHECK(e.S().gold == 15);
        e.shop.AddGold(0, -100);
        CHECK(e.S().gold == 0);
        e.shop.StartTurn(0, 13);
        CHECK(e.S().gold == 10 && e.shop.Gold(0) == 10);
        // Selling raises gold above 10 as well.
        uint32_t const u = Put(e, 0, Zone::Hand, HostCard(H_COUNT - 1));
        CHECK(u != 0 && e.shop.Sell(0, Zone::Hand, 0) == ActionResult::Ok && e.S().gold == 11);
    }

    void TestUpgradeCost()
    {
        CHECK(UpgradeBaseCost(2) == 5 && UpgradeBaseCost(3) == 7 && UpgradeBaseCost(4) == 8 && UpgradeBaseCost(5) == 9 &&
            UpgradeBaseCost(6) == 11 && UpgradeBaseCost(1) == 0 && UpgradeBaseCost(7) == 0);
        Env e;
        e.shop.StartTurn(0, 1);
        CHECK(e.shop.UpgradeCost(0) == 5);
        // Each turn without an upgrade: -1, down to 0.
        int const expected[] = { 4, 3, 2, 1, 0, 0, 0 };
        for (int t = 2; t <= 8; ++t)
        {
            e.shop.StartTurn(0, t);
            CHECK(e.shop.UpgradeCost(0) == expected[t - 2]);
        }
    }

    void TestUpgradePath()
    {
        Env e;
        // Three turns at tier 1, then the discount: 5 - 3 = 2 on turn 4.
        for (int t = 1; t <= 4; ++t)
            e.shop.StartTurn(0, t);
        CHECK(e.shop.UpgradeCost(0) == 2 && e.S().gold == 6);
        CHECK(e.shop.Upgrade(0) == ActionResult::Ok);
        CHECK(e.S().tier == 2 && e.S().gold == 4);
        CHECK(e.shop.UpgradeCost(0) == 7);                  // reset to the full price of tier 3
        e.shop.StartTurn(0, 5);
        CHECK(e.shop.UpgradeCost(0) == 6);                  // -1 the turn after

        // Upgrade every turn with plenty of gold: pays 5, 6, 7, 8, 10 for tier 2..6.
        Env f;
        int const paid[] = { 5, 6, 7, 8, 10 };
        for (int t = 1; t <= 5; ++t)
        {
            f.shop.StartTurn(0, t);
            f.shop.AddGold(0, 50);
            int const before = f.S().gold;
            CHECK(f.shop.Upgrade(0) == ActionResult::Ok);
            CHECK(before - f.S().gold == paid[t - 1]);
            CHECK(f.S().tier == t + 1);
        }
        CHECK(f.S().tier == 6 && f.shop.UpgradeCost(0) == -1);
        CHECK(f.shop.Upgrade(0) == ActionResult::MaxTier);
        f.shop.StartTurn(0, 6);
        CHECK(f.shop.UpgradeCost(0) == -1);

        // Short of gold: refused with nothing changed.
        Env g;
        g.shop.StartTurn(0, 1);                              // 3 gold, cost 5
        CHECK(g.shop.Upgrade(0) == ActionResult::NotEnoughGold && g.S().gold == 3 && g.S().tier == 1);
    }

    void TestOffers()
    {
        CHECK(OffersForTier(1) == 3 && OffersForTier(2) == 4 && OffersForTier(3) == 4 && OffersForTier(4) == 5 &&
            OffersForTier(5) == 5 && OffersForTier(6) == 6 && OffersForTier(0) == 0 && OffersForTier(7) == 0);
        Env e(3);
        for (int tier = 1; tier <= 6; ++tier)
        {
            e.S().tier = tier;
            e.shop.StartTurn(0, tier);
            CHECK(static_cast<int>(e.S().offers.size()) == OffersForTier(tier));
            for (Minion const& m : e.S().offers)
            {
                CHECK(m.Tier() <= tier && m.poolCopies == 1 && !m.golden);
                CHECK(m.uid != 0 && e.pool.Has(m.card));
            }
            CHECK(Conserved(e));
            // The tavern is renewed each turn: the old copies go back.
            std::set<uint32_t> uids;
            for (Minion const& m : e.S().offers)
                uids.insert(m.uid);
            e.shop.StartTurn(0, tier + 10);
            for (Minion const& m : e.S().offers)
                CHECK(!uids.count(m.uid));
            CHECK(Conserved(e));
        }
        // An upgrade changes the size at the next refresh / turn, not at once.
        Env f(4);
        f.shop.StartTurn(0, 1);
        f.shop.AddGold(0, 20);
        CHECK(f.shop.Upgrade(0) == ActionResult::Ok);
        CHECK(f.S().offers.size() == 3);
        CHECK(f.shop.Refresh(0) == ActionResult::Ok && f.S().offers.size() == 4);
        // Higher tiers appear: over many refreshes at tier 6 we see a tier 6 minion.
        f.S().tier = 6;
        bool tier6 = false;
        for (int i = 0; i < 80 && !tier6; ++i)
        {
            f.shop.AddGold(0, 1);
            f.shop.Refresh(0);
            for (Minion const& m : f.S().offers)
                tier6 = tier6 || m.Tier() == 6;
        }
        CHECK(tier6 && Conserved(f));
    }

    // ------------------------------------------------------------------------------------ buy / sell / refresh / freeze

    void TestBuySellRefresh()
    {
        Env e(5);
        e.shop.StartTurn(0, 1);
        int const left0 = e.pool.TotalLeft();
        uint32_t const uid = e.S().offers[1].uid;
        int const card = e.S().offers[1].card;
        CHECK(e.shop.Buy(0, 1) == ActionResult::Ok);
        CHECK(e.S().gold == 0 && e.S().offers.size() == 2 && e.S().hand.Size() == 1);
        CHECK(e.S().hand[0].uid == uid && e.S().hand[0].card == card && e.S().hand[0].poolCopies == 1);
        CHECK(e.pool.TotalLeft() == left0);                // the copy left the pool when the tavern was dealt
        CHECK(e.shop.Buy(0, 0) == ActionResult::NotEnoughGold);
        CHECK(e.shop.Buy(0, 9) == ActionResult::BadIndex && e.shop.Buy(0, -1) == ActionResult::BadIndex);
        CHECK(e.shop.Refresh(0) == ActionResult::NotEnoughGold);
        CHECK(Conserved(e));

        // Sell: +1 gold, the copy returns.
        int const before = e.pool.Left(card);
        CHECK(e.shop.Sell(0, Zone::Hand, 0) == ActionResult::Ok);
        CHECK(e.S().gold == 1 && e.S().hand.Empty() && e.pool.Left(card) == before + 1);
        CHECK(e.S().stats.bought == 1 && e.S().stats.sold == 1);   // the weeklies count them
        CHECK(e.shop.Sell(0, Zone::Hand, 0) == ActionResult::BadIndex);
        CHECK(Conserved(e));

        // A Wild Banana (a spell that needs a Beast) is thrown away for 0 Gold; another spell cannot be sold.
        int const gold = e.S().gold;
        CHECK(e.shop.AddToHand(0, FindCard("TB_BaconShop_HP_038t"), false) != 0 && e.S().hand.Size() == 1);
        CHECK(e.shop.Sell(0, Zone::Hand, 0) == ActionResult::Ok && e.S().hand.Empty() && e.S().gold == gold);
        CHECK(e.S().stats.sold == 1);                               // a thrown-away Banana is no sold minion
        CHECK(e.shop.AddToHand(0, FindCard("BGS_Treasures_040"), false) != 0);
        CHECK(e.shop.Sell(0, Zone::Hand, 0) == ActionResult::BadIndex && e.S().hand.Size() == 1);

        // Refresh: 1 gold, new offers, the old ones return.
        std::set<uint32_t> old;
        for (Minion const& m : e.S().offers)
            old.insert(m.uid);
        CHECK(e.shop.Refresh(0) == ActionResult::Ok && e.S().gold == 0 && e.S().offers.size() == 3);
        for (Minion const& m : e.S().offers)
            CHECK(!old.count(m.uid));
        CHECK(Conserved(e));

        // Sell from the board; generated minions return nothing.
        Env f(6);
        f.shop.StartTurn(0, 1);
        int const left = f.pool.TotalLeft();
        Put(f, 0, Zone::Board, HostCard(H_COUNT - 1));
        CHECK(f.shop.Sell(0, Zone::Board, 0) == ActionResult::Ok && f.S().board.Empty());
        CHECK(f.pool.TotalLeft() == left && Conserved(f));
        CHECK(f.shop.Sell(0, Zone::Board, 0) == ActionResult::BadIndex);
        CHECK(f.shop.Buy(7, 0) == ActionResult::BadPlayer && f.shop.Refresh(-1) == ActionResult::BadPlayer);
    }

    void TestFreeze()
    {
        Env e(8);
        e.shop.StartTurn(0, 1);
        std::vector<uint32_t> uids;
        for (Minion const& m : e.S().offers)
            uids.push_back(m.uid);
        CHECK(e.shop.Freeze(0, true) == ActionResult::Ok && e.shop.OffersFrozen(0));
        // Freeze costs nothing.
        CHECK(e.S().gold == 3);
        e.shop.StartTurn(0, 2);
        CHECK(!e.shop.AnyOfferFrozen(0) && e.S().offers.size() == 3);
        for (std::size_t i = 0; i < 3; ++i)
            CHECK(e.S().offers[i].uid == uids[i]);
        CHECK(Conserved(e));
        // The next turn the tavern is renewed (not frozen again).
        e.shop.StartTurn(0, 3);
        CHECK(e.S().offers[0].uid != uids[0]);

        // Frozen after a purchase: the rest stays, the bought slot is refilled at the next turn start.
        e.shop.Buy(0, 0);
        uint32_t const keep = e.S().offers[0].uid;
        e.shop.Freeze(0, true);
        e.shop.StartTurn(0, 4);
        CHECK(e.S().offers.size() == 3 && e.S().offers[0].uid == keep);
        CHECK(Conserved(e));

        // Frozen over a tier-up: the extra slot is filled at the next turn start.
        e.shop.AddGold(0, 30);
        CHECK(e.shop.Upgrade(0) == ActionResult::Ok);
        e.shop.Freeze(0, true);
        uint32_t const first = e.S().offers[0].uid;
        e.shop.StartTurn(0, 5);
        CHECK(e.S().offers.size() == 4 && e.S().offers[0].uid == first && Conserved(e));

        // Refresh thaws; un-freezing works; frozen minions can still be bought.
        e.shop.AddGold(0, 10);
        e.shop.Freeze(0, true);
        CHECK(e.shop.Buy(0, 0) == ActionResult::Ok);
        CHECK(e.shop.Refresh(0) == ActionResult::Ok && !e.shop.AnyOfferFrozen(0));
        e.shop.Freeze(0, true);
        e.shop.Freeze(0, false);
        CHECK(!e.shop.AnyOfferFrozen(0) && e.shop.Freeze(3, true) == ActionResult::BadPlayer);
    }

    // Frozen is per offer.
    void TestFreezePerOffer()
    {
        Env e(9);
        e.shop.StartTurn(0, 1);
        std::vector<uint32_t> uids;
        for (Minion const& m : e.S().offers)
            uids.push_back(m.uid);
        CHECK(uids.size() == 3 && !e.shop.AnyOfferFrozen(0) && !e.shop.OffersFrozen(0));
        e.shop.FreezeOffer(0, 1);
        CHECK(e.shop.AnyOfferFrozen(0) && !e.shop.OffersFrozen(0));
        CHECK((e.S().offers[1].flags & kMinionFrozen) != 0 && (e.S().offers[0].flags & kMinionFrozen) == 0);
        e.shop.FreezeOffer(0, 7);   // out of range: nothing
        CHECK(!e.shop.OffersFrozen(0));
        // The Freeze action with one offer Frozen freezes them all; again with all Frozen it unfreezes them all.
        CHECK(e.shop.Freeze(0, true) == ActionResult::Ok && e.shop.OffersFrozen(0));
        CHECK(e.shop.Freeze(0, false) == ActionResult::Ok && !e.shop.AnyOfferFrozen(0));

        // Next turn: the Frozen offer stays (and thaws), the others are replaced, the tavern is full again.
        e.shop.FreezeOffer(0, 1);
        e.shop.StartTurn(0, 2);
        CHECK(e.S().offers.size() == 3 && !e.shop.AnyOfferFrozen(0));
        CHECK(e.S().offers[0].uid == uids[1]);
        CHECK(e.S().offers[1].uid != uids[0] && e.S().offers[1].uid != uids[2]);
        CHECK(e.S().offers[2].uid != uids[0] && e.S().offers[2].uid != uids[2]);
        CHECK(Conserved(e));

        // A bought Frozen offer is not Frozen in the hand; a Refresh replaces everything and thaws.
        e.shop.Freeze(0, true);
        e.shop.AddGold(0, 10);
        CHECK(e.shop.Buy(0, 0) == ActionResult::Ok);
        CHECK((e.S().hand[e.S().hand.Size() - 1].flags & kMinionFrozen) == 0);
        CHECK(e.shop.OffersFrozen(0) && e.S().offers.size() == 2);
        e.shop.FreezeOffer(0, 0);
        CHECK(e.shop.Refresh(0) == ActionResult::Ok && !e.shop.AnyOfferFrozen(0) && Conserved(e));
    }

    void TestHandBoardLimits()
    {
        Env e(9);
        e.shop.StartTurn(0, 1);
        // Hand 10: ten distinct cards fill it; an eleventh is refused.
        std::vector<int> cards = CardsOfTier(2, true);
        for (int i = 0; i < 10; ++i)
            CHECK(e.shop.AddToHand(0, cards[static_cast<std::size_t>(i)], false) != 0);
        CHECK(e.S().hand.Size() == 10);
        CHECK(e.shop.AddToHand(0, cards[10], false) == 0 && e.S().hand.Size() == 10);
        CHECK(e.shop.AddRandomToHand(0, 1, 1, 0) == 0);
        ForceOffer(e, 0, 0, CardsOfTier(1, true)[0]);
        CHECK(e.shop.Buy(0, 0) == ActionResult::HandFull && e.S().gold == 3 && e.S().offers.size() == 3);
        CHECK(Conserved(e));

        // Board 7: the eighth play is refused and the minion stays in the hand.
        Env f(9);
        f.shop.StartTurn(0, 1);
        for (int i = 0; i < 7; ++i)
            CHECK(f.shop.AddToHand(0, cards[static_cast<std::size_t>(i)], false) != 0);
        for (int i = 0; i < 7; ++i)
            CHECK(f.shop.Play(0, 0, 99) == ActionResult::Ok);
        CHECK(f.S().board.Size() == 7 && f.S().hand.Empty());
        f.shop.AddToHand(0, cards[8], false);
        CHECK(f.shop.Play(0, 0, 0) == ActionResult::BoardFull && f.S().hand.Size() == 1 && f.S().board.Size() == 7);
        CHECK(f.shop.SummonToBoard(0, cards[8], false, 0) == 0);
        CHECK(f.shop.Play(0, 5, 0) == ActionResult::BadIndex);
        // The board keeps the order of the slots given (99 appends).
        for (int i = 0; i < 7; ++i)
            CHECK(f.S().board[i].card == cards[static_cast<std::size_t>(i)]);
    }

    void TestPlayAndMove()
    {
        Env e(10);
        e.shop.StartTurn(0, 1);
        std::vector<int> const c = CardsOfTier(3, true);
        for (int i = 0; i < 4; ++i)
            e.shop.AddToHand(0, c[static_cast<std::size_t>(i)], false);
        CHECK(e.shop.Play(0, 0, 0) == ActionResult::Ok);   // c0
        CHECK(e.shop.Play(0, 0, 0) == ActionResult::Ok);   // c1 to the left
        CHECK(e.shop.Play(0, 0, 1) == ActionResult::Ok);   // c2 between
        CHECK(e.shop.Play(0, 0, -5) == ActionResult::Ok);  // c3 clamped to the left
        CHECK(e.S().board[0].card == c[3] && e.S().board[1].card == c[1] && e.S().board[2].card == c[2] &&
            e.S().board[3].card == c[0]);
        CHECK(e.shop.Move(0, 0, 3) == ActionResult::Ok);
        CHECK(e.S().board[3].card == c[3] && e.S().board[0].card == c[1]);
        CHECK(e.shop.Move(0, 3, 0) == ActionResult::Ok && e.S().board[0].card == c[3]);
        CHECK(e.shop.Move(0, 2, 2) == ActionResult::Ok);
        CHECK(e.shop.Move(0, 4, 0) == ActionResult::BadIndex && e.shop.Move(0, 0, 4) == ActionResult::BadIndex &&
            e.shop.Move(0, -1, 0) == ActionResult::BadIndex);
        // A target that is not on the board is refused; nothing changes.
        e.shop.AddToHand(0, c[5], false);
        CHECK(e.shop.Play(0, 0, 0, 424242) == ActionResult::BadTarget && e.S().hand.Size() == 1);
    }

    // ------------------------------------------------------------------------------------ auras

    void TestAurasAfterBoardChange()
    {
        Env e(12);
        e.shop.StartTurn(0, 1);
        int const aura = HostCard(H_AURA);
        int const other = HostCard(H_COUNT - 1);
        e.shop.AddToHand(0, other, false);
        e.shop.AddToHand(0, aura, false);
        int const base = CardAt(other).attack;
        CHECK(e.shop.Play(0, 0, 0) == ActionResult::Ok);   // other
        CHECK(e.S().board[0].attack == base);
        CHECK(e.shop.Play(0, 0, 1) == ActionResult::Ok);   // aura source to the right
        CHECK(e.S().board[0].attack == base + 1);
        CHECK(e.shop.Sell(0, Zone::Board, 1) == ActionResult::Ok);
        CHECK(e.S().board[0].attack == base && e.S().board[0].auraAttack == 0);
        // A buff through the API keeps the aura bookkeeping.
        e.shop.AddToHand(0, aura, false);
        e.shop.Play(0, 0, 1);
        e.shop.Buff(0, e.S().board[0], 3, 1);
        CHECK(e.S().board[0].attack == base + 3 + 1);
        e.shop.Move(0, 1, 0);                              // source moves left: still buffs the other minion
        CHECK(e.S().board[1].attack == base + 3 + 1);
    }

    // ------------------------------------------------------------------------------------ Magnetic

    void TestMagnetic()
    {
        int const host = MechCard(false);
        int const mag = FindCard("BG_BOT_312");           // 3/1 Magnetic Mech
        int const mag2 = FindCard("BG_BOT_911");          // 2/4 Taunt + Divine Shield + Magnetic
        CHECK(host >= 0 && mag >= 0 && mag2 >= 0);
        CHECK((CardAt(mag).keywords & kKwMagnetic) != 0 && HasTribe(CardAt(host).tribes, kTribeMech));

        Env e(13);
        e.shop.StartTurn(0, 1);
        uint32_t const hostUid = Put(e, 0, Zone::Board, host, false, 1);
        Put(e, 0, Zone::Hand, mag, false, 1);
        int const hostAtk = e.S().board[0].attack;
        int const hostHp = e.S().board[0].health;
        CHECK(e.shop.Play(0, 0, 0, hostUid) == ActionResult::Ok);
        CHECK(e.S().board.Size() == 1 && e.S().hand.Empty());
        Minion const& h = e.S().board[0];
        CHECK(h.attack == hostAtk + CardAt(mag).attack && h.health == hostHp + CardAt(mag).health);
        CHECK(h.maxHealth == h.health && h.attachments.size() == 1);
        CHECK(h.attachments[0].card == mag && h.attachments[0].attack == CardAt(mag).attack);
        CHECK(!(h.keywords & kKwMagnetic));
        CHECK(Conserved(e));

        // Keywords merge too.
        Put(e, 0, Zone::Hand, mag2, false, 1);
        CHECK(e.shop.Play(0, 0, 0, hostUid) == ActionResult::Ok);
        Minion const& h2 = e.S().board[0];
        CHECK(h2.Has(kKwTaunt) && h2.Has(kKwDivineShield) && h2.attachments.size() == 2);
        CHECK(Conserved(e));

        // Selling the host returns the copies of the host and of both attachments.
        int const l0 = e.pool.Left(host);
        int const l1 = e.pool.Left(mag);
        CHECK(e.shop.Sell(0, Zone::Board, 0) == ActionResult::Ok);
        CHECK(e.pool.Left(host) == l0 + 1 && e.pool.Left(mag) == l1 + 1 && Conserved(e));

        // A target that is not a Mech is refused, the minion stays in the hand.
        uint32_t const beast = Put(e, 0, Zone::Board, CardsOfTribe(kTribeBeast, true)[0], false, 1);
        Put(e, 0, Zone::Hand, mag, false, 1);
        CHECK(e.shop.Play(0, 0, 0, beast) == ActionResult::BadTarget && e.S().hand.Size() == 1);
        // Without a target a Magnetic minion is simply played.
        CHECK(e.shop.Play(0, 0, 1) == ActionResult::Ok && e.S().board.Size() == 2);
        CHECK(e.S().board[1].card == mag && e.S().board[1].Has(kKwMagnetic));
        CHECK(Conserved(e));

        // A full board does not block the merge (it takes no slot), but a plain play is refused.
        Env f(14);
        f.shop.StartTurn(0, 1);
        uint32_t const mech = Put(f, 0, Zone::Board, host);
        std::vector<int> const fill = CardsOfTier(2, true);
        for (int i = 0; i < 6; ++i)
            Put(f, 0, Zone::Board, fill[static_cast<std::size_t>(i)]);
        Put(f, 0, Zone::Hand, mag);
        Put(f, 0, Zone::Hand, mag);
        CHECK(f.shop.Play(0, 0, 7) == ActionResult::BoardFull && f.S().hand.Size() == 2);
        CHECK(f.shop.Play(0, 0, 0, mech) == ActionResult::Ok && f.S().hand.Size() == 1 && f.S().board.Size() == 7);
    }

    // ------------------------------------------------------------------------------------ triples and Discover

    void TestTriple()
    {
        int const x = CardsOfTier(2, true)[0];
        CardDef const& d = CardAt(x);
        Env e(15);
        e.shop.StartTurn(0, 1);
        // Two copies (one in hand, one on the board), the third bought: they merge at once.
        Put(e, 0, Zone::Board, x, false, 1);
        Put(e, 0, Zone::Hand, x, false, 1);
        e.shop.Buff(0, e.S().board[0], 2, 3);              // a buff on one original
        ForceOffer(e, 0, 0, x);
        CHECK(e.shop.Buy(0, 0) == ActionResult::Ok);
        CHECK(e.S().board.Empty() && e.S().hand.Size() == 1);
        Minion const& g = e.S().hand[0];
        CHECK(g.golden && g.card == x);
        CHECK(g.attack == d.goldenAttack + 2 && g.health == d.goldenHealth + 3 && g.maxHealth == g.health);
        CHECK(g.poolCopies == 3);
        CHECK(e.S().gold == 0 && Conserved(e));

        // Playing the golden minion gives a Triple Reward Discover from tier + 1.
        CHECK(e.shop.Play(0, 0, 0) == ActionResult::Ok);
        CHECK(e.S().board.Size() == 1 && e.S().options.size() == 3);
        std::set<int> distinct;
        for (Minion const& m : e.S().options)
        {
            CHECK(m.Tier() == 2 && !m.golden && m.poolCopies == 1);   // tavern tier 1 + 1
            distinct.insert(m.card);
        }
        CHECK(distinct.size() == 3 && Conserved(e));
        // Blocked until chosen.
        CHECK(e.shop.Buy(0, 0) == ActionResult::DiscoverPending && e.shop.Refresh(0) == ActionResult::DiscoverPending);
        CHECK(e.shop.Play(0, 0, 0) == ActionResult::DiscoverPending && e.shop.Sell(0, Zone::Board, 0) == ActionResult::DiscoverPending);
        CHECK(e.shop.Upgrade(0) == ActionResult::DiscoverPending && e.shop.EndTurn(0) == ActionResult::DiscoverPending);
        CHECK(e.shop.Move(0, 0, 0) == ActionResult::DiscoverPending && e.shop.Freeze(0, true) == ActionResult::DiscoverPending);
        CHECK(e.shop.Choose(0, 3) == ActionResult::BadIndex && e.shop.Choose(0, -1) == ActionResult::BadIndex);
        int const pick = e.S().options[2].card;
        CHECK(e.shop.Choose(0, 2) == ActionResult::Ok);
        CHECK(e.S().options.empty() && e.S().hand.Size() == 1 && e.S().hand[0].card == pick && !e.S().hand[0].golden);
        CHECK(Conserved(e));
        CHECK(e.shop.Choose(0, 0) == ActionResult::NoDiscover);

        // Selling a golden returns its three copies.
        int const before = e.pool.Left(x);
        CHECK(e.shop.Sell(0, Zone::Board, 0) == ActionResult::Ok);
        CHECK(e.pool.Left(x) == before + 3 && Conserved(e));

        // The reward tier is the tavern tier at the time of playing, capped at 6.
        for (int tier : { 3, 5, 6 })
        {
            Env t(16);
            t.S().tier = tier;
            t.shop.StartTurn(0, 1);
            Put(t, 0, Zone::Hand, x, true, 3);
            CHECK(t.shop.Play(0, 0, 0) == ActionResult::Ok && t.S().options.size() == 3);
            for (Minion const& m : t.S().options)
                CHECK(m.Tier() == std::min(tier + 1, 6));
            CHECK(Conserved(t));
        }
    }

    void TestTripleEdges()
    {
        int const x = CardsOfTier(3, true)[0];
        std::vector<int> const fill = CardsOfTier(2, true);

        // Three copies spread over hand and board in any mix.
        for (int onBoard = 0; onBoard <= 3; ++onBoard)
        {
            Env e(20 + static_cast<uint32_t>(onBoard));
            e.shop.StartTurn(0, 1);
            for (int i = 0; i < 3; ++i)
                Put(e, 0, i < onBoard ? Zone::Board : Zone::Hand, x, false, 1);
            // Any action settles; selling a filler does it.
            Put(e, 0, Zone::Hand, fill[0]);
            e.shop.Sell(0, Zone::Hand, e.S().hand.Size() - 1);
            CHECK(e.S().board.Empty() && e.S().hand.Size() == 1 && e.S().hand[0].golden);
            CHECK(Conserved(e));
        }

        // A full hand still takes the copy that completes a triple (with a copy in the hand).
        {
            Env e(30);
            e.shop.StartTurn(0, 1);
            Put(e, 0, Zone::Hand, x, false, 1);
            for (int i = 0; i < 9; ++i)
                Put(e, 0, Zone::Hand, fill[static_cast<std::size_t>(i)]);
            Put(e, 0, Zone::Board, x, false, 1);
            CHECK(e.S().hand.Full());
            ForceOffer(e, 0, 0, x);
            CHECK(e.shop.Buy(0, 0) == ActionResult::Ok);
            CHECK(e.S().hand.Size() == 10 && e.S().board.Empty());
            int goldens = 0;
            for (Minion const& m : e.S().hand.Items())
                goldens += m.golden ? 1 : 0;
            CHECK(goldens == 1 && Conserved(e));
        }

        // Three copies on the board and a full hand: no room for the golden yet; a freed slot merges them.
        {
            Env e(31);
            e.shop.StartTurn(0, 1);
            for (int i = 0; i < 3; ++i)
                Put(e, 0, Zone::Board, x, false, 1);
            for (int i = 0; i < 10; ++i)
                Put(e, 0, Zone::Hand, fill[static_cast<std::size_t>(i)]);
            e.shop.Move(0, 0, 0);
            CHECK(e.S().board.Size() == 3);                // nothing settled yet
            CHECK(e.shop.Sell(0, Zone::Hand, 0) == ActionResult::Ok);
            CHECK(e.S().board.Empty() && e.S().hand.Size() == 10 && e.S().hand[9].golden);
        }

        // Golden copies do not count towards a triple; a second triple gives a second golden.
        {
            Env e(32);
            e.shop.StartTurn(0, 1);
            Put(e, 0, Zone::Hand, x, true, 3);
            Put(e, 0, Zone::Hand, x, false, 1);
            Put(e, 0, Zone::Hand, x, false, 1);
            Put(e, 0, Zone::Hand, fill[0]);
            e.shop.Sell(0, Zone::Hand, 3);
            CHECK(e.S().hand.Size() == 3 && e.pool.Left(x) == CopiesForTier(3) - 5);
            CHECK(Conserved(e));
        }

        // A generated minion can complete a triple.
        {
            Env e(33);
            e.shop.StartTurn(0, 1);
            Put(e, 0, Zone::Hand, x, false, 1);
            Put(e, 0, Zone::Board, x, false, 1);
            CHECK(e.shop.AddToHand(0, x, false) != 0);
            CHECK(e.S().hand.Size() == 1 && e.S().hand[0].golden && e.S().hand[0].poolCopies == 2);
            CHECK(Conserved(e));
        }
    }

    void TestDiscover()
    {
        Env e(40);
        e.shop.StartTurn(0, 1);
        int const left0 = e.pool.TotalLeft();
        // A tribe-limited Discover of tiers 1-3.
        CHECK(e.shop.Discover(0, 1, 3, kTribeDragon));
        CHECK(e.S().options.size() == 3 && e.pool.TotalLeft() == left0 - 3);   // options are real pool copies
        for (Minion const& m : e.S().options)
            CHECK(m.Tier() <= 3 && HasTribe(m.Tribes(), kTribeDragon));
        CHECK(Conserved(e));
        CHECK(e.shop.Choose(0, 0) == ActionResult::Ok);
        CHECK(e.pool.TotalLeft() == left0 - 1 && e.S().hand.Size() == 1 && Conserved(e));

        // Two Discovers queue up; the second opens when the first is chosen.
        CHECK(e.shop.Discover(0, 2, 2, 0));
        CHECK(e.shop.Discover(0, 5, 5, 0));
        CHECK(e.S().options.size() == 3 && e.S().options[0].Tier() == 2 && e.S().queue.size() == 1);
        CHECK(e.shop.Choose(0, 1) == ActionResult::Ok);
        CHECK(e.S().options.size() == 3 && e.S().options[0].Tier() == 5 && e.S().queue.empty());
        CHECK(Conserved(e));
        CHECK(e.shop.Choose(0, 1) == ActionResult::Ok && e.S().options.empty() && e.S().hand.Size() == 3);

        // A Discover with nothing to show is refused.
        PoolSet empty;
        Rng rng(1);
        Shop s(empty, Samples(), rng, 1);
        CHECK(!s.Discover(0, 1, 6, 0));

        // A full hand loses the chosen minion; its copy returns.
        Env g(42);
        g.shop.StartTurn(0, 1);
        std::vector<int> const fill = CardsOfTier(2, true);
        for (int i = 0; i < 10; ++i)
            g.shop.AddToHand(0, fill[static_cast<std::size_t>(i)], false);
        int const total = g.pool.TotalLeft();
        CHECK(g.shop.Discover(0, 4, 4, 0) && g.shop.Choose(0, 0) == ActionResult::Ok);
        CHECK(g.S().hand.Size() == 10 && g.pool.TotalLeft() == total && Conserved(g));

        // Elimination returns the options too.
        Env h(43);
        h.shop.StartTurn(0, 1);
        h.shop.Discover(0, 3, 3, 0);
        h.shop.ReleasePlayer(0);
        CHECK(h.S().options.empty() && Conserved(h));
        std::vector<int> const held = h.shop.HeldCopies();
        CHECK(std::all_of(held.begin(), held.end(), [](int n) { return n == 0; }));
    }

    void TestRelease()
    {
        Env e(44, 3);
        for (int p = 0; p < 3; ++p)
            e.shop.StartTurn(p, 1);
        e.shop.AddGold(1, 30);
        e.shop.Buy(1, 0);
        e.shop.Buy(1, 0);
        e.shop.Buy(1, 0);
        e.shop.Play(1, 0, 0);
        e.shop.Freeze(1, true);
        CHECK(e.S(1).hand.Size() == 2 && e.S(1).board.Size() == 1);
        CHECK(Conserved(e));
        e.shop.ReleasePlayer(1);
        CHECK(e.S(1).hand.Empty() && e.S(1).board.Empty() && e.S(1).offers.empty() && e.S(1).gold == 0);
        CHECK(Conserved(e));
        // The others are untouched and still conserve.
        CHECK(e.S(0).offers.size() == 3 && e.S(2).offers.size() == 3);
        e.shop.ReleasePlayer(0);
        e.shop.ReleasePlayer(2);
        CHECK(e.pool.TotalLeft() == [&] { int n = 0; for (int c : e.pool.Cards()) n += e.pool.Initial(c); return n; }());
    }

    // ------------------------------------------------------------------------------------ hooks

    void TestShopHooks()
    {
        g_events.clear();
        Env e(50);
        e.S().tier = 1;
        Shop& s = e.shop;
        // Player 1 has a start-of-turn minion too: it must not fire for player 0.
        Put(e, 1, Zone::Board, HostCard(H_START));
        Put(e, 0, Zone::Board, HostCard(H_START));
        uint32_t const endUid = Put(e, 0, Zone::Board, HostCard(H_END));
        Put(e, 0, Zone::Board, HostCard(H_REFRESH));
        Put(e, 0, Zone::Board, HostCard(H_PLAYED));
        Put(e, 0, Zone::Board, HostCard(H_SUMMONED));
        uint32_t const upUid = Put(e, 0, Zone::Board, HostCard(H_UPGRADE));

        s.StartTurn(0, 1);
        CHECK(Count("start") == 1 && Last("start")->card == HostCard(H_START) && Last("start")->position == 0);
        CHECK(e.S().gold == 4);                             // the hook added 1
        CHECK(Count("refresh") + Count("end") + Count("upgrade") + Count("buy") + Count("sell") + Count("battlecry") == 0);

        // Refresh.
        g_events.clear();
        CHECK(s.Refresh(0) == ActionResult::Ok);
        CHECK(Count("refresh") == 1 && g_events.size() == 1 && e.S().gold == 3);

        // Upgrade (needs 5): the hook gave Taunt.
        s.AddGold(0, 2);
        g_events.clear();
        CHECK(s.Upgrade(0) == ActionResult::Ok);
        CHECK(Count("upgrade") == 1 && g_events.size() == 1);
        CHECK(e.S().board.IndexOfUid(upUid) >= 0 && e.S().board[e.S().board.IndexOfUid(upUid)].Has(kKwTaunt));

        // Buy fires onBuy for the bought minion only, in the hand.
        s.AddGold(0, 3);
        ForceOffer(e, 0, 0, HostCard(H_BUY));
        g_events.clear();
        CHECK(s.Buy(0, 0) == ActionResult::Ok);
        CHECK(Count("buy") == 1 && g_events.size() == 1 && Last("buy")->card == HostCard(H_BUY));
        CHECK(e.S().hand.Size() == 1 && e.S().hand[0].card == HostCard(H_BUY));
        s.Sell(0, Zone::Hand, 0);

        // Sell fires onSell for the sold minion, still in place, then it is gone.
        Put(e, 0, Zone::Hand, HostCard(H_SELL));
        int const gold = e.S().gold;
        g_events.clear();
        CHECK(s.Sell(0, Zone::Hand, 0) == ActionResult::Ok);
        CHECK(Count("sell") == 1 && g_events.size() == 1 && e.S().hand.Empty() && e.S().gold == gold + 1);
        Put(e, 0, Zone::Board, HostCard(H_SELL));
        g_events.clear();
        CHECK(s.Sell(0, Zone::Board, e.S().board.Size() - 1) == ActionResult::Ok && Count("sell") == 1);

        // Play: battlecry with its target, then played and summoned on the OTHER board minions, in this order.
        Put(e, 0, Zone::Hand, HostCard(H_BC));
        int const targetAtk = e.S().board[static_cast<int>(e.S().board.IndexOfUid(endUid))].attack;
        g_events.clear();
        CHECK(s.Play(0, 0, 2, endUid) == ActionResult::Ok);
        CHECK(g_events.size() == 3);
        CHECK(g_events[0].hook == "battlecry" && g_events[0].card == HostCard(H_BC) && !g_events[0].golden &&
            g_events[0].position == 2 && g_events[0].targetCard == HostCard(H_END));
        CHECK(g_events[1].hook == "played" && g_events[1].card == HostCard(H_PLAYED) && g_events[1].otherCard == HostCard(H_BC));
        CHECK(g_events[2].hook == "summoned" && g_events[2].card == HostCard(H_SUMMONED) && g_events[2].otherCard == HostCard(H_BC));
        CHECK(e.S().board[e.S().board.IndexOfUid(endUid)].attack == targetAtk + 2);   // the battlecry buffed its target

        // A golden battlecry receives golden = true (and opens the Triple Reward). Make room first.
        for (uint32_t uid : { upUid })
            s.Sell(0, Zone::Board, e.S().board.IndexOfUid(uid));
        s.Sell(0, Zone::Board, 3);                          // the refresh host (board: start, end, bc, refresh, ...)
        Put(e, 0, Zone::Hand, HostCard(H_BC), true);
        g_events.clear();
        CHECK(s.Play(0, 0, 0) == ActionResult::Ok);
        CHECK(Last("battlecry") && Last("battlecry")->golden && Last("battlecry")->targetCard == -1);
        CHECK(e.S().options.size() == 3);
        s.Choose(0, 0);

        // SummonToBoard (an effect summon) fires summoned but not played.
        g_events.clear();
        std::vector<int> const filler = CardsOfTier(2, true);
        uint32_t const sUid = s.SummonToBoard(0, filler[0], false, 0);
        CHECK(sUid != 0 && Count("summoned") == 1 && Count("played") == 0 && Count("battlecry") == 0);
        CHECK(e.S().board[0].uid == sUid);

        // End of turn: the hook buffed itself.
        Minion const* endMinion = &e.S().board[e.S().board.IndexOfUid(endUid)];
        int const atk = endMinion->attack;
        g_events.clear();
        CHECK(s.EndTurn(0) == ActionResult::Ok);
        CHECK(Count("end") == 1 && g_events.size() == 1);
        endMinion = &e.S().board[e.S().board.IndexOfUid(endUid)];
        CHECK(endMinion->attack == atk + 1);

        CHECK(Conserved(e));
    }

    void TestHookOrderAndFailure()
    {
        // A refused action fires nothing.
        g_events.clear();
        Env e(51);
        e.shop.StartTurn(0, 1);
        Put(e, 0, Zone::Board, HostCard(H_REFRESH));
        Put(e, 0, Zone::Board, HostCard(H_UPGRADE));
        e.shop.AddGold(0, -3);
        g_events.clear();
        CHECK(e.shop.Refresh(0) == ActionResult::NotEnoughGold && e.shop.Upgrade(0) == ActionResult::NotEnoughGold);
        CHECK(g_events.empty());
        // Hooks run left to right.
        Put(e, 0, Zone::Board, HostCard(H_REFRESH));
        e.shop.AddGold(0, 5);
        e.shop.Refresh(0);
        CHECK(Count("refresh") == 2 && g_events[0].position == 0 && g_events[1].position == 2);
    }

    // ------------------------------------------------------------------------------------ the game

    CombatRecord const* FindFight(Game const& g, int round, int player)
    {
        for (CombatRecord const& r : g.Combats())
            if (r.round == round && (r.a == player || r.b == player))
                return &r;
        return nullptr;
    }

    GameSetup AllExternal(uint32_t seed)
    {
        GameSetup s;
        s.seed = seed;
        for (int p = 0; p < kPlayers; ++p)
            s.kinds[p] = PlayerKind::External;
        return s;
    }

    void EndAll(Game& g)
    {
        for (int p = 0; p < kPlayers; ++p)
            if (g.Alive(p))
                g.EndTurn(p);
    }

    Minion Strong(int attack, int health, int tier = 6, int variant = 0)
    {
        Minion m = MakeMinion(CardsOfTier(tier, true)[static_cast<std::size_t>(variant)]);
        m.attack = attack;
        m.health = m.maxHealth = health;
        m.keywords = 0;
        return m;
    }

    void TestGameSetup()
    {
        GameSetup setup = AllExternal(77);
        Game g(setup);
        CHECK(g.Round() == 1 && !g.Over() && g.WinnerFaction() == -1);
        CHECK(g.FactionTribes(0).size() == 5 && g.FactionTribes(1).size() == 5);
        CHECK(g.FactionTribes(0) == g.FactionTribes(1));   // one shared set when faction pools are off
        for (int p = 0; p < kPlayers; ++p)
        {
            PlayerShop const& s = g.GetShop().State(p);
            CHECK(s.health == 40 && s.gold == 3 && s.tier == 1 && s.offers.size() == 3);
            CHECK(g.Alive(p) && !g.TurnEnded(p) && FactionOf(p) == p / 4);
            CHECK(g.Kind(p) == PlayerKind::External);
        }
        CHECK(FactionOf(0) == 0 && FactionOf(3) == 0 && FactionOf(4) == 1 && FactionOf(7) == 1);
        CHECK(g.LivingInFaction(0) == 4 && g.LivingInFaction(1) == 4);
        CHECK(g.GetShop().CheckConservation());
        // The pool is dealt: 8 players x 3 offers have left it.
        CHECK(g.Pool().TotalLeft() > 0);
    }

    void TestFactionPools()
    {
        // One pool per faction, each with its own half-size copies; a faction's play never touches the other.
        for (uint32_t seed : { 3u, 21u, 55u, 88u })
        {
            GameSetup s = AllExternal(seed);
            s.factionPools = true;
            Game g(s);
            CHECK(g.Pool().PoolCount() == 2);
            CHECK(g.GetShop().CheckConservation());
            for (int p = 0; p < kPlayers; ++p)
                CHECK(g.GetShop().State(p).pools == std::vector<int>{ FactionOf(p) });
            // Each faction rolls its own 5 of the playable tribes, so a pool holds either none of a card or its
            // own half-size copies.
            for (int pool = 0; pool < 2; ++pool)
                for (int c : g.Pool().Cards())
                {
                    int const init = g.Pool().InitialIn(pool, c);
                    CHECK(init == 0 || init == FactionCopiesFor(c));
                }
            auto heldByFaction = [&](int card, int faction)
            {
                int n = 0;
                for (int p = faction * kFactionSize; p < (faction + 1) * kFactionSize; ++p)
                {
                    PlayerShop const& ps = g.GetShop().State(p);
                    auto add = [&](Minion const& m)
                    {
                        if (m.card == card)
                            n += m.poolCopies;
                        for (Attachment const& a : m.attachments)
                            if (a.card == card)
                                n += a.poolCopies;
                    };
                    for (Minion const& m : ps.board.Items())
                        add(m);
                    for (Minion const& m : ps.hand.Items())
                        add(m);
                    for (Minion const& m : ps.offers)
                        add(m);
                    for (Minion const& m : ps.options)
                        add(m);
                }
                return n;
            };
            auto poolTotal = [&](int pool)
            {
                int n = 0;
                for (int c : g.Pool().Cards())
                    n += g.Pool().LeftIn(pool, c);
                return n;
            };
            // Per-pool conservation: pool copies left + copies held by that faction == the pool's initial count.
            for (int pool = 0; pool < 2; ++pool)
                for (int c : g.Pool().Cards())
                    CHECK(g.Pool().LeftIn(pool, c) + heldByFaction(c, pool) == g.Pool().InitialIn(pool, c));
            int const otherBefore = poolTotal(1);
            CHECK(g.Buy(0, 0) == ActionResult::Ok);
            CHECK(poolTotal(1) == otherBefore);        // faction 0's buy left faction 1's pool untouched
            CHECK(g.GetShop().CheckConservation());
            EndAll(g);
            CHECK(g.Combats().size() == 4 && g.GetShop().CheckConservation());
        }
        // Whole faction games run to the end with per-pool conservation intact.
        for (uint32_t seed : { 4u, 9u, 30u })
        {
            GameSetup s;
            s.seed = seed;
            s.factionPools = true;
            Game g(s);
            CHECK(g.Over() && g.GetShop().CheckConservation());
        }
    }

    void TestGameActionApi()
    {
        Game g(AllExternal(78));
        CHECK(g.Buy(0, 0) == ActionResult::Ok && g.GetShop().State(0).gold == 0);
        CHECK(g.Play(0, 0, 0) == ActionResult::Ok);
        CHECK(g.Refresh(0) == ActionResult::NotEnoughGold && g.Upgrade(0) == ActionResult::NotEnoughGold);
        CHECK(g.Freeze(0, true) == ActionResult::Ok);
        CHECK(g.Move(0, 0, 0) == ActionResult::Ok);
        CHECK(g.Sell(0, Zone::Board, 0) == ActionResult::Ok);
        CHECK(g.Choose(0, 0) == ActionResult::NoDiscover);
        CHECK(g.Buy(9, 0) == ActionResult::BadPlayer && g.Buy(-1, 0) == ActionResult::BadPlayer);
        CHECK(g.EndTurn(0) == ActionResult::Ok);
        CHECK(g.TurnEnded(0));
        CHECK(g.Buy(0, 0) == ActionResult::NotShopPhase && g.Sell(0, Zone::Hand, 0) == ActionResult::NotShopPhase);
        CHECK(g.EndTurn(0) == ActionResult::NotShopPhase);
        CHECK(g.Round() == 1);
        for (int p = 1; p < kPlayers; ++p)
            CHECK(g.EndTurn(p) == ActionResult::Ok);
        CHECK(g.Round() == 2 && !g.TurnEnded(0) && g.GetShop().State(0).gold == 4);
        CHECK(g.Buy(0, 0) == ActionResult::Ok);
        CHECK(std::string(ResultName(ActionResult::Ok)) == "Ok" && std::string(ResultName(ActionResult::HandFull)) == "HandFull");
    }

    void TestGameWithNpcAndExternal()
    {
        // Player 0 external: the NPCs play their turns at once and wait for the round to be closed.
        GameSetup s;
        s.seed = 79;
        s.kinds[0] = PlayerKind::External;
        Game g(s);
        CHECK(g.Round() == 1 && !g.TurnEnded(0));
        for (int p = 1; p < kPlayers; ++p)
            CHECK(g.TurnEnded(p));
        CHECK(g.GetShop().State(1).hand.Size() + g.GetShop().State(1).board.Size() > 0);   // the NPC bought
        CHECK(g.EndTurn(0) == ActionResult::Ok);
        CHECK(g.Round() == 2 && g.Combats().size() == 4);
        for (int p = 1; p < kPlayers; ++p)
            CHECK(!g.Over());
        CHECK(g.GetShop().CheckConservation());

        // A custom policy is called once per living NPC per round.
        struct CountingPolicy : NpcPolicy
        {
            std::map<int, int> calls;
            void PlayTurn(Game&, int player) override { ++calls[player]; }
        } policy;
        GameSetup t;
        t.seed = 80;
        t.policy = &policy;
        t.kinds[3] = PlayerKind::External;
        Game h(t);
        CHECK(policy.calls.size() == 7 && policy.calls.count(3) == 0);
        for (auto const& kv : policy.calls)
            CHECK(kv.second == 1);
        h.EndTurn(3);
        for (auto const& kv : policy.calls)
            CHECK(kv.second == 2);
    }

    void TestPairings()
    {
        // 4 v 4, nobody ever dies (empty boards): cross-faction always, never the same opponent twice in a row.
        Game g(AllExternal(90));
        int const rounds = 40;
        for (int r = 0; r < rounds; ++r)
            EndAll(g);
        CHECK(!g.Over() && g.Round() == rounds + 1);
        std::map<int, int> last;
        for (int p = 0; p < kPlayers; ++p)
            last[p] = -2;
        std::set<std::pair<int, int>> pairsSeen;
        int repeats = 0;
        for (int r = 1; r <= rounds; ++r)
        {
            int fights = 0;
            std::set<int> in;
            for (CombatRecord const& c : g.Combats())
            {
                if (c.round != r)
                    continue;
                ++fights;
                CHECK(c.b >= 0 && FactionOf(c.a) != FactionOf(c.b));
                CHECK(in.insert(c.a).second && in.insert(c.b).second);   // everybody fights exactly once
                if (last[c.a] == c.b || last[c.b] == c.a)
                    ++repeats;
                last[c.a] = c.b;
                last[c.b] = c.a;
                pairsSeen.insert(std::make_pair(c.a, c.b));
            }
            CHECK(fights == 4);
        }
        CHECK(repeats == 0);
        CHECK(pairsSeen.size() == 16);                      // over 40 rounds all 4 x 4 pairs occur

        // Same seed, same pairings.
        Game h(AllExternal(90));
        for (int r = 0; r < rounds; ++r)
            EndAll(h);
        CHECK(h.Combats() == g.Combats());
    }

    void TestRest()
    {
        Game g(AllExternal(91));
        // Player 4 dies in the shop phase of round 1: 3 v 4, so one faction A player rests each round.
        PlayerShop& dead = g.GetShop().State(4);
        dead.board.Push(Strong(2, 5, 3));
        dead.health = 0;
        for (int p = 0; p < 4; ++p)
            g.GetShop().State(p).health = 30;
        g.GetShop().State(0).health = 39;          // one below the maximum: heals only 1
        EndAll(g);
        CHECK(!g.Alive(4) && g.EliminationRound(4) == 1 && g.EliminationOrder().size() == 1);
        CHECK(g.GetShop().State(4).board.Empty());
        CHECK(g.GetShop().CheckConservation());
        // Empty boards draw, so the only health change is the rest.
        std::set<int> rested;
        for (int r = 1; r <= 4; ++r)
        {
            if (r > 1)
                EndAll(g);
            int fights = 0;
            for (CombatRecord const& c : g.Combats())
                if (c.round == r)
                {
                    ++fights;
                    CHECK(c.a >= 0 && c.b >= 0 && FactionOf(c.a) != FactionOf(c.b));
                }
            CHECK(fights == 3);
            int resters = 0;
            for (int p = 0; p < 4; ++p)
                if (g.LastOpponent(p) == kRested)
                {
                    ++resters;
                    CHECK(rested.insert(p).second);   // the faction takes turns: nobody rests twice in four rounds
                }
                else
                    CHECK(g.RestHealed(p) == 0);
            CHECK(resters == 1);
            for (int p = 5; p < 8; ++p)
                CHECK(g.LastOpponent(p) >= 0 && g.LastOpponent(p) < 4 && g.RestHealed(p) == 0);
        }
        CHECK(rested.size() == 4);
        CHECK(g.GetShop().State(0).health == 40);   // capped at the hero's maximum
        for (int p = 1; p < 4; ++p)
            CHECK(g.GetShop().State(p).health == 33);

        // 4 v 1: one fight a round, the three others rest, and the four take turns.
        Game h(AllExternal(92));
        for (int p = 5; p < 8; ++p)
            h.GetShop().State(p).health = 0;
        for (int p = 0; p < 4; ++p)
            h.GetShop().State(p).health = 20;
        EndAll(h);
        CHECK(h.LivingInFaction(1) == 1 && !h.Over());
        std::set<int> fighters;
        for (int r = 1; r <= 4; ++r)
        {
            if (r > 1)
                EndAll(h);
            int fights = 0;
            for (CombatRecord const& c : h.Combats())
                if (c.round == r)
                {
                    ++fights;
                    int const ally = c.a == 4 ? c.b : c.a;
                    CHECK(FactionOf(ally) == 0 && fighters.insert(ally).second);
                }
            CHECK(fights == 1);
            int resting = 0;
            for (int p = 0; p < 4; ++p)
                if (h.LastOpponent(p) == kRested)
                {
                    ++resting;
                    CHECK(h.RestHealed(p) == 3);
                }
            CHECK(resting == 3);
            CHECK(h.LastOpponent(4) >= 0 && h.LastOpponent(4) < 4);
        }
        CHECK(fighters.size() == 4);
        for (int p = 0; p < 4; ++p)
            CHECK(h.GetShop().State(p).health == 20 + 3 * 3);   // rested three of the four rounds

        // Resting heals through round kRestHealEndRound - 1 and nothing from kRestHealEndRound on.
        while (h.Round() < kRestHealEndRound - 1 && !h.Over())
            EndAll(h);
        for (int r = kRestHealEndRound - 1; r <= kRestHealEndRound; ++r)
        {
            CHECK(h.Round() == r && !h.Over());
            for (int p = 0; p < 4; ++p)
                h.GetShop().State(p).health = 20;
            EndAll(h);
            int resting = 0;
            for (int p = 0; p < 4; ++p)
                if (h.LastOpponent(p) == kRested)
                {
                    ++resting;
                    int const want = r < kRestHealEndRound ? 3 : 0;
                    CHECK(h.RestHealed(p) == want && h.GetShop().State(p).health == 20 + want);
                }
            CHECK(resting == 3);
        }
    }

    void TestDamageCap()
    {
        Game g(AllExternal(93));
        // Player 0: tavern tier 6 and seven big minions: tier 6 + 7 x 6 = 48 damage uncapped.
        PlayerShop& big = g.GetShop().State(0);
        big.tier = 6;
        for (int i = 0; i < 7; ++i)
            big.board.Push(Strong(30, 30, 6, i));
        CHECK(g.DamageCapped());
        EndAll(g);
        CombatRecord const* f1 = FindFight(g, 1, 0);
        CHECK(f1 && f1->winner == 0 && f1->damage == 15 && f1->capped);
        int const victim = f1->a == 0 ? f1->b : f1->a;
        CHECK(g.GetShop().State(victim).health == 40 - 15);
        CHECK(g.Round() == 2 && g.DamageCapped());           // nobody has died yet

        // The first elimination (an effect kills a hero in the shop phase of round 2): this round is still capped.
        g.GetShop().State(3).health = 0;   // an ally: player 0 is in the smaller faction, so it never rests
        EndAll(g);
        CombatRecord const* f2 = FindFight(g, 2, 0);
        CHECK(f2 && f2->winner == 0 && f2->capped && f2->damage == 15);
        CHECK(!g.Alive(3) && g.EliminationOrder().size() == 1);
        CHECK(!g.DamageCapped());                            // from round 3 on the cap is off

        // Round 3: the full 48.
        EndAll(g);
        CombatRecord const* f3 = FindFight(g, 3, 0);
        CHECK(f3 && f3->winner == 0 && !f3->capped && f3->damage == 48);
    }

    void TestEliminationAndWin()
    {
        Game g(AllExternal(94));
        PlayerShop& big = g.GetShop().State(0);
        big.tier = 6;
        for (int i = 0; i < 7; ++i)
            big.board.Push(Strong(30, 30, 6, i));
        // Everybody in faction B except one has 1 health; player 0's opponent dies if it is one of them.
        for (int p = 4; p < 8; ++p)
            g.GetShop().State(p).health = 1;
        // Give player 4 a board and some hand/offers to see them go back to the pool.
        g.GetShop().Buy(4, 0);
        g.GetShop().Buy(4, 0);
        EndAll(g);
        int out = 0;
        for (int p = 4; p < 8; ++p)
            out += g.Alive(p) ? 0 : 1;
        CHECK(out == 1);                                     // exactly player 0's opponent
        CHECK(g.EliminationOrder().size() == 1);
        int const dead = g.EliminationOrder()[0];
        CHECK(FactionOf(dead) == 1 && g.EliminationRound(dead) == 1);
        CHECK(g.GetShop().State(dead).hand.Empty() && g.GetShop().State(dead).board.Empty() && g.GetShop().State(dead).offers.empty());
        CHECK(g.GetShop().CheckConservation());
        // An eliminated player cannot act.
        CHECK(g.Buy(dead, 0) == ActionResult::Eliminated && g.EndTurn(dead) == ActionResult::Eliminated);
        CHECK(g.Placement(dead) == 4 && g.LivingInFaction(1) == 3);
        // Placement of the survivors: by health.
        std::vector<int> const rank = g.FactionRanking(1);
        CHECK(rank.size() == 4 && rank[3] == dead);

        // Faction win: the rest of faction B dies in a shop phase.
        for (int p = 4; p < 8; ++p)
            if (g.Alive(p))
                g.GetShop().State(p).health = 0;
        EndAll(g);
        CHECK(g.Over() && g.WinnerFaction() == 0 && g.Phase() == GamePhase::Over && !g.TimedOut());
        CHECK(g.LivingInFaction(1) == 0 && g.LivingInFaction(0) == 4);
        CHECK(g.Buy(0, 0) == ActionResult::GameOver && g.EndTurn(0) == ActionResult::GameOver);
        CHECK(g.GetShop().CheckConservation());
        for (int p = 4; p < 8; ++p)
            CHECK(!g.Alive(p));

        // The reverse faction can win too, and a mutual wipe goes to the faction of the player out last.
        Game h(AllExternal(95));
        for (int p = 0; p < 4; ++p)
            h.GetShop().State(p).health = 0;
        EndAll(h);
        CHECK(h.Over() && h.WinnerFaction() == 1);
        Game k(AllExternal(96));
        for (int p = 0; p < kPlayers; ++p)
            k.GetShop().State(p).health = -p;                // player 7 has the lowest health: out first
        EndAll(k);
        CHECK(k.Over() && k.EliminationOrder().size() == 8 && k.EliminationOrder()[0] == 7 && k.EliminationOrder()[7] == 0);
        CHECK(k.WinnerFaction() == 0);
    }

    void TestPlacements()
    {
        Game g(AllExternal(97));
        // Round 1: player 5 dies (shop). Round 2: players 7 dies. Survivors 4 (30 hp) and 6 (20 hp).
        g.GetShop().State(5).health = 0;
        EndAll(g);
        g.GetShop().State(7).health = 0;
        EndAll(g);
        g.GetShop().State(4).health = 30;
        g.GetShop().State(6).health = 20;
        std::vector<int> const rank = g.FactionRanking(1);
        CHECK(rank == std::vector<int>({ 4, 6, 7, 5 }));
        CHECK(g.Placement(4) == 1 && g.Placement(6) == 2 && g.Placement(7) == 3 && g.Placement(5) == 4);
        g.GetShop().State(6).health = 35;
        CHECK(g.Placement(6) == 1 && g.Placement(4) == 2);
        // Same round: the lower health goes out first.
        g.GetShop().State(4).health = -3;
        g.GetShop().State(6).health = -1;
        EndAll(g);
        CHECK(g.EliminationOrder().size() == 4 && g.EliminationOrder()[2] == 4 && g.EliminationOrder()[3] == 6);
        CHECK(g.Over() && g.WinnerFaction() == 0);
        CHECK(g.FactionRanking(1) == std::vector<int>({ 6, 4, 7, 5 }));
        // Faction A's placement of a finished game is by health.
        g.GetShop().State(0).health = 10;
        g.GetShop().State(1).health = 40;
        g.GetShop().State(2).health = 25;
        g.GetShop().State(3).health = 25;
        CHECK(g.FactionRanking(0) == std::vector<int>({ 1, 2, 3, 0 }));
    }

    // ------------------------------------------------------------------------------------ full games

    // Checks the invariants of a lobby at this moment.
    void Audit(Game const& g, int& violations)
    {
        Shop const& s = g.GetShop();
        std::string why;
        if (!s.CheckConservation(&why))
        {
            ++violations;
            std::printf("    audit: %s\n", why.c_str());
        }
        std::set<uint32_t> uids;
        for (int p = 0; p < kPlayers; ++p)
        {
            PlayerShop const& ps = s.State(p);
            bool ok = ps.gold >= 0 && ps.board.Size() <= kBoardMax && ps.hand.Size() <= kHandMax;
            ok = ok && ps.tier >= kMinTier && ps.tier <= kMaxTier && ps.upgradeCost >= 0;
            ok = ok && static_cast<int>(ps.offers.size()) <= OffersForTier(ps.tier) + 2;   // Stasis Elemental
            ok = ok && ps.options.size() <= 3;
            if (!g.Alive(p))
                ok = ok && ps.board.Empty() && ps.hand.Empty() && ps.offers.empty() && ps.options.empty();
            for (Minion const& m : ps.board.Items())
                ok = ok && m.health > 0 && m.card >= 0 && uids.insert(m.uid).second;
            for (Minion const& m : ps.hand.Items())
                ok = ok && m.card >= 0 && uids.insert(m.uid).second;
            for (Minion const& m : ps.offers)
                ok = ok && m.card >= 0 && uids.insert(m.uid).second;
            if (!ok)
            {
                ++violations;
                std::printf("    audit: player %d breaks an invariant\n", p);
            }
        }
    }

    // Plays the placeholder policy and audits the lobby before and after every NPC turn.
    class AuditedPolicy : public NpcPolicy
    {
    public:
        void PlayTurn(Game& g, int player) override
        {
            Audit(g, violations);
            m_inner.PlayTurn(g, player);
            Audit(g, violations);
            ++turns;
        }
        int violations = 0;
        int turns = 0;
    private:
        SimpleNpcPolicy m_inner;
    };

    void TestFullGames()
    {
        int const games = 300;
        int finished = 0;
        int timedOut = 0;
        int totalRounds = 0;
        int longest = 0;
        int winsByFaction[2] = { 0, 0 };
        int violations = 0;
        int turns = 0;
        int cappedAbove15 = 0;
        for (int seed = 1; seed <= games; ++seed)
        {
            AuditedPolicy policy;
            GameSetup setup;
            setup.seed = static_cast<uint32_t>(seed);
            setup.policy = &policy;
            Game g(setup);
            if (g.Over() && (g.WinnerFaction() == 0 || g.WinnerFaction() == 1))
            {
                ++finished;
                ++winsByFaction[g.WinnerFaction()];
            }
            else
                CHECK(false);
            if (g.TimedOut())
                ++timedOut;
            totalRounds += g.Round();
            longest = std::max(longest, g.Round());
            Audit(g, policy.violations);
            violations += policy.violations;
            turns += policy.turns;
            // The losing faction is wiped out (unless the game timed out).
            if (!g.TimedOut())
                CHECK(g.LivingInFaction(1 - g.WinnerFaction()) == 0);
            for (CombatRecord const& c : g.Combats())
            {
                CHECK(c.b >= 0 && FactionOf(c.a) != FactionOf(c.b));
                if (c.capped && c.damage > 15)
                    ++cappedAbove15;
            }
            // Eliminated players are exactly those with health <= 0 or all of the losing faction.
            for (int p = 0; p < kPlayers; ++p)
                if (!g.Alive(p))
                    CHECK(g.GetShop().State(p).health <= 0);
            // Placement 1..4 is a permutation inside each faction.
            for (int f = 0; f < 2; ++f)
            {
                std::set<int> places;
                for (int p = f * 4; p < f * 4 + 4; ++p)
                    places.insert(g.Placement(p));
                CHECK(places == std::set<int>({ 1, 2, 3, 4 }));
            }
        }
        CHECK(finished == games && violations == 0 && cappedAbove15 == 0);
        CHECK(timedOut == 0);
        CHECK(winsByFaction[0] > 50 && winsByFaction[1] > 50);   // neither faction is structurally favoured
        std::printf("    %d games: %d finished, avg %d rounds, longest %d, %d timed out, wins A/B %d/%d, %d NPC turns, %d audit violations\n",
            games, finished, totalRounds / games, longest, timedOut, winsByFaction[0], winsByFaction[1], turns, violations);
    }

    void TestDeterminism()
    {
        for (uint32_t seed : { 1u, 2u, 3u, 17u, 99u })
        {
            GameSetup s;
            s.seed = seed;
            Game a(s);
            Game b(s);
            CHECK(a.Over() && b.Over());
            CHECK(a.Combats() == b.Combats());
            CHECK(a.WinnerFaction() == b.WinnerFaction() && a.Round() == b.Round());
            CHECK(a.EliminationOrder() == b.EliminationOrder());
            CHECK(a.FactionTribes(0) == b.FactionTribes(0) && a.FactionTribes(1) == b.FactionTribes(1));
            for (int p = 0; p < kPlayers; ++p)
                CHECK(a.GetShop().State(p).health == b.GetShop().State(p).health &&
                    a.GetShop().State(p).tier == b.GetShop().State(p).tier);
        }
        // Different seeds give different games.
        std::set<std::vector<int>> orders;
        for (uint32_t seed = 1; seed <= 20; ++seed)
        {
            GameSetup s;
            s.seed = seed;
            Game g(s);
            orders.insert(g.EliminationOrder());
        }
        CHECK(orders.size() > 5);
        // Manual driving reaches the same state as the constructor: autoRun off, then Pump.
        GameSetup m;
        m.seed = 5;
        m.autoRun = false;
        Game manual(m);
        CHECK(manual.Round() == 1 && !manual.TurnEnded(0));
        manual.Pump();
        GameSetup n;
        n.seed = 5;
        Game automatic(n);
        CHECK(manual.Over() && manual.Combats() == automatic.Combats());
    }

    // Counts what the placeholder policy reaches over many games, so the invariants above cover those paths.
    class SpyPolicy : public NpcPolicy
    {
    public:
        void PlayTurn(Game& game, int player) override
        {
            m_inner.PlayTurn(game, player);
            PlayerShop const& ps = game.GetShop().State(player);
            for (Minion const& m : ps.board.Items())
                if (m.golden)
                    ++goldens;
            if (game.GetShop().OffersFrozen(player))
                ++frozen;
            if (ps.tier == kMaxTier)
                ++tier6;
        }
        int goldens = 0;
        int frozen = 0;
        int tier6 = 0;
    private:
        SimpleNpcPolicy m_inner;
    };

    void TestPolicyReach()
    {
        SpyPolicy spy;
        for (uint32_t seed = 1; seed <= 40; ++seed)
        {
            GameSetup s;
            s.seed = seed;
            s.policy = &spy;
            Game g(s);
            CHECK(g.Over());
        }
        CHECK(spy.goldens > 0 && spy.frozen > 0 && spy.tier6 > 0);
        std::printf("    placeholder policy over 40 games: %d golden-minion turns, %d frozen turns, %d tier-6 turns\n", spy.goldens,
            spy.frozen, spy.tier6);
    }
}

int main(int, char**)
{
    RUN(TestPoolCopies);
    RUN(TestLobbyTribes);
    RUN(TestPoolDrawAndReturn);
    RUN(TestPoolWeighted);
    RUN(TestPoolIdsSeam);
    RUN(TestSend);
    RUN(TestGold);
    RUN(TestUpgradeCost);
    RUN(TestUpgradePath);
    RUN(TestOffers);
    RUN(TestBuySellRefresh);
    RUN(TestFreeze);
    RUN(TestFreezePerOffer);
    RUN(TestHandBoardLimits);
    RUN(TestPlayAndMove);
    RUN(TestAurasAfterBoardChange);
    RUN(TestMagnetic);
    RUN(TestTriple);
    RUN(TestTripleEdges);
    RUN(TestDiscover);
    RUN(TestRelease);
    RUN(TestShopHooks);
    RUN(TestHookOrderAndFailure);
    RUN(TestGameSetup);
    RUN(TestFactionPools);
    RUN(TestGameActionApi);
    RUN(TestGameWithNpcAndExternal);
    RUN(TestPairings);
    RUN(TestRest);
    RUN(TestDamageCap);
    RUN(TestEliminationAndWin);
    RUN(TestPlacements);
    RUN(TestFullGames);
    RUN(TestDeterminism);
    RUN(TestPolicyReach);
    std::printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
