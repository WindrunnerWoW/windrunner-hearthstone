// Tests of the Battlegrounds minion effects (src/battlegrounds/HsBgEffects*.cpp) and of the core API the
// effects use (Brann / Rivendare / Khadgar multipliers, cleave, persistent combat changes, ...).
//   bg_effects_tests.exe [directory]      run all tests; exit code 0 = all checks passed.
//
// LAYOUT. This file holds the shared helpers (below), the tests of the core API (RunCoreTests, using small private
// registries so they do not depend on the real effects) and the registry coverage report. The tests of the real
// effects live in ten files, one per batch, that are #included at the end and each expose one function:
//   bg_effects_beast_tests.inc      RunBeastTests()
//   bg_effects_mech_tests.inc       RunMechTests()
//   bg_effects_murloc_tests.inc     RunMurlocTests()
//   bg_effects_demon_tests.inc      RunDemonTests()
//   bg_effects_dragon_tests.inc     RunDragonTests()
//   bg_effects_elemental_tests.inc  RunElementalTests()
//   bg_effects_pirate_tests.inc     RunPirateTests()
//   bg_effects_naga_tests.inc       RunNagaTests()
//   bg_effects_quilboar_tests.inc   RunQuilboarTests()
//   bg_effects_neutral_tests.inc    RunNeutralTests()
// A new batch adds its .inc (and its HsBgEffects<Batch>.cpp) plus an #include and a Run call below. Inside an .inc use
// CHECK(cond) and write each test as a static function called from the Run<Batch>Tests function with RUN(fn).
// The real effects are taken from EffectRegistry::Global(), which holds every batch linked into the program.
//
// HELPERS (all at file scope, usable from the .inc files):
//   Cd("BG19_010")                  card index of a normal id (CHECKs that it exists)
//   Mn("BG19_010", golden)          a fresh minion with the card's own stats
//   Body(attack, health, keywords)  a tribeless vanilla body (the Golden Monkey token, no effect); BodyOf(id, a, h) keeps
//                                   the tribe of another token (Hyena beast, Imp demon, Microbot mech)
//   With(minion, attack, health)    the same minion with stats overridden
//   Fight({a...}, {b...}, seed, tierA, tierB)   SimulateCombat with the real effects; boards are left to right
//   Alive(r, side), Survivor(r, side, i), Events(r, type), Triggers(r, actor, kind), SummonedCards(r, card)
//   Lobby L(seed, players)          a shop with a pool and the real effects (second argument of the constructor: a private
//                                   registry for core tests). L.S(p) is the PlayerShop.
//   OnBoard(L, p, "id", golden, pos)  puts a minion on the board WITHOUT playing it (no battlecry); returns the uid
//   InHand(L, p, "id", golden)        adds a minion to the hand; returns the uid
//   PlayCard(L, p, "id", golden, slot, targetUid)   puts the card in the hand and plays it (CHECKs Ok); returns the uid
//   NewOffers(L, p, {"id", ...})      replaces Bob's Tavern; SetGold / SetTier; Find(L, p, uid); BoardIds(L, p)
//   L.shop.Buy / Sell / Refresh / Upgrade / StartTurn / EndTurn are the real actions; call L.shop.Settle(p) after editing a
//   board by hand so auras, tavern bonuses and triples are refreshed.
//   ManualGame G(seed, registry)    a Game of 8 external players for tests of what happens between rounds (persistent
//                                   combat changes, last opponent): fill G.game.GetShop().State(p).board, then G.EndRound().
//   PlainOf(kTribeX)                an effect-free minion of a tribe where one exists (Beast, Mech, Demon: tokens; Dragon:
//                                   Bronze Warden without keywords); murloc tests use a real Murloc whose entry is inert.

#include "../src/battlegrounds/HsBgCombat.h"
#include "../src/battlegrounds/HsBgEffects.h"
#include "../src/battlegrounds/HsBgGame.h"
#include "../src/battlegrounds/HsBgPool.h"
#include "../src/battlegrounds/HsBgShop.h"
#include "../src/battlegrounds/HsBgTypes.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <set>
#include <string>
#include <vector>

using namespace Hs::Bg;

// On: a pool minion with effect text and no registry entry fails.
constexpr bool kRequireAllEffects = true;

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

// ------------------------------------------------------------------------------------------------ minions

[[maybe_unused]] static int Cd(char const* id)
{
    int const c = FindCard(id);
    CHECK(c >= 0);
    return c < 0 ? 0 : c;
}

[[maybe_unused]] static Minion Mn(char const* id, bool golden = false, uint32_t uid = 0) { return MakeMinion(Cd(id), golden, uid); }

[[maybe_unused]] static Minion With(Minion m, int attack, int health)
{
    m.attack = attack;
    m.health = m.maxHealth = health;
    return m;
}

[[maybe_unused]] static Minion BodyOf(char const* tokenId, int attack, int health, uint32_t keywords = 0)
{
    Minion m = With(Mn(tokenId), attack, health);
    m.keywords = keywords;
    return m;
}

// Tribeless, no effect: the Golden Monkey token.
[[maybe_unused]] static Minion Body(int attack, int health, uint32_t keywords = 0) { return BodyOf("BG23_353_Gt", attack, health, keywords); }

[[maybe_unused]] static Minion PlainOf(uint32_t tribe, bool golden = false)
{
    Minion m;
    if (tribe == kTribeBeast)
        m = Mn("BG_EX1_534t", golden);
    else if (tribe == kTribeMech)
        m = Mn("BG_BOT_312t", golden);
    else if (tribe == kTribeDemon)
        m = Mn("BG_BRM_006t", golden);
    else if (tribe == kTribeDragon)
    {
        m = Mn("BGS_034", golden);
        m.keywords = 0;
    }
    else if (tribe == kTribeElemental)
    {
        m = Mn("BGS_115t", golden);   // Water Droplet: an inert Elemental token
    }
    else
    {
        CHECK(false); // no plain minion of this tribe: use a real card whose entry is inert in the scenario
        m = Mn("BG23_353_Gt");
    }
    return m;
}

// ------------------------------------------------------------------------------------------------ combat

[[maybe_unused]] static CombatResult FightWith(EffectRegistry const& reg, std::vector<Minion> const& a, std::vector<Minion> const& b,
                              uint32_t seed = 1, int tierA = 1, int tierB = 1, bool cap = false)
{
    CombatSide sa;
    CombatSide sb;
    sa.tavernTier = tierA;
    sb.tavernTier = tierB;
    for (Minion const& m : a)
        sa.board.Push(m);
    for (Minion const& m : b)
        sb.board.Push(m);
    CombatSettings st;
    st.effects = &reg;
    st.capDamage = cap;
    return SimulateCombat(sa, sb, seed, st);
}

[[maybe_unused]] static CombatResult Fight(std::vector<Minion> const& a, std::vector<Minion> const& b, uint32_t seed = 1, int tierA = 1,
                          int tierB = 1)
{
    return FightWith(EffectRegistry::Global(), a, b, seed, tierA, tierB);
}

[[maybe_unused]] static int Alive(CombatResult const& r, int side) { return r.survivors[side].Size(); }
[[maybe_unused]] static Minion const& Survivor(CombatResult const& r, int side, int i) { return r.survivors[side][i]; }

[[maybe_unused]] static std::vector<CombatEvent> Events(CombatResult const& r, EventType type)
{
    std::vector<CombatEvent> out;
    for (CombatEvent const& e : r.events)
        if (e.type == type)
            out.push_back(e);
    return out;
}

[[maybe_unused]] static int Triggers(CombatResult const& r, uint32_t actor, HookKind kind)
{
    int n = 0;
    for (CombatEvent const& e : r.events)
        if (e.type == EventType::Trigger && e.actor == actor && e.amount == static_cast<int>(kind))
            ++n;
    return n;
}

// Number of Summon events of a card.
[[maybe_unused]] static int SummonedCards(CombatResult const& r, int card)
{
    int n = 0;
    for (CombatEvent const& e : r.events)
        if (e.type == EventType::Summon && e.card == card)
            ++n;
    return n;
}

// ------------------------------------------------------------------------------------------------ shop

struct Lobby
{
    Rng rng;
    PoolSet pool;
    Shop shop;
    explicit Lobby(uint32_t seed = 1, int players = 2, EffectRegistry const& reg = EffectRegistry::Global())
        : rng(seed), shop(pool, reg, rng, players)
    {
        pool.BuildLobby(rng);
    }
    PlayerShop& S(int p = 0) { return shop.State(p); }
};

[[maybe_unused]] static uint32_t OnBoard(Lobby& L, int p, char const* id, bool golden = false, int pos = -1)
{
    Minion m = MakeMinion(Cd(id), golden, L.shop.NewUid());
    PlayerShop& ps = L.S(p);
    L.S(p).board.Insert(pos < 0 ? ps.board.Size() : pos, m);
    L.shop.Settle(p);
    return m.uid;
}

[[maybe_unused]] static uint32_t InHand(Lobby& L, int p, char const* id, bool golden = false)
{
    Minion m = MakeMinion(Cd(id), golden, L.shop.NewUid());
    L.S(p).hand.Push(m);
    return m.uid;
}

[[maybe_unused]] static uint32_t PlayCard(Lobby& L, int p, char const* id, bool golden = false, int slot = -1, uint32_t target = 0)
{
    uint32_t const uid = InHand(L, p, id, golden);
    PlayerShop& ps = L.S(p);
    ActionResult const r = L.shop.Play(p, ps.hand.IndexOfUid(uid), slot < 0 ? ps.board.Size() : slot, target);
    CHECK(r == ActionResult::Ok);
    return uid;
}

[[maybe_unused]] static Minion* Find(Lobby& L, int p, uint32_t uid) { return L.shop.FindMinion(p, uid); }
[[maybe_unused]] static void SetGold(Lobby& L, int p, int gold) { L.S(p).gold = gold; }
[[maybe_unused]] static void SetTier(Lobby& L, int p, int tier) { L.S(p).tier = tier; }

[[maybe_unused]] static std::vector<std::string> BoardIds(Lobby& L, int p)
{
    std::vector<std::string> out;
    for (Minion const& m : L.S(p).board.Items())
        out.push_back(CardAt(m.card).id);
    return out;
}

// Replaces Bob's Tavern with the given cards (real pool copies, so the pool stays balanced).
[[maybe_unused]] static void NewOffers(Lobby& L, int p, std::initializer_list<char const*> ids)
{
    PlayerShop& ps = L.S(p);
    for (Minion const& o : ps.offers)
        L.pool.Return(o.card, o.poolCopies);
    ps.offers.clear();
    for (char const* id : ids)
    {
        int const card = Cd(id);
        Minion m = MakeMinion(card, false, L.shop.NewUid());
        if (L.pool.Take(card))
            m.poolCopies = 1;
        ps.offers.push_back(m);
    }
    L.shop.Settle(p);
}

// A Game of eight external players (no NPC turns run): put minions on the shop boards, then EndRound() ends every living
// player's turn, which fights the round and starts the next.
struct ManualGame
{
    Game game;
    static GameSetup Setup(uint32_t seed, EffectRegistry const& reg)
    {
        GameSetup s;
        s.seed = seed;
        s.effects = &reg;
        for (int i = 0; i < kPlayers; ++i)
            s.kinds[i] = PlayerKind::External;
        s.autoRun = false;
        return s;
    }
    explicit ManualGame(uint32_t seed = 1, EffectRegistry const& reg = EffectRegistry::Global()) : game(Setup(seed, reg)) {}
    PlayerShop& S(int p) { return game.GetShop().State(p); }
    void EndRound()
    {
        for (int p = 0; p < kPlayers; ++p)
            if (game.Alive(p))
                game.EndTurn(p);
    }
};

// ------------------------------------------------------------------------------------------------ core API tests
// These use small private registries: real card ids with test hooks, so they hold whatever the batches register.

namespace
{
    int g_count = 0;
    int g_probe = 0;
    int g_probe2 = 0;
    bool g_flag = false;

    void BcCount(ShopCtx&, bool) { ++g_count; }
    void BcSummon(ShopCtx& c, bool golden) { c.Summon(Cd("BG_CFM_315t"), golden, c.position + 1); }
    void DrSummon(CombatCtx& c, bool golden) { c.Summon(Cd("BG_BOT_312t"), golden); }
    void DrCount(CombatCtx&, bool) { ++g_count; }
    void SocBuffSelf(CombatCtx& c, bool golden) { c.api.Buff(c.self, golden ? 6 : 3, golden ? 4 : 2); }
    void AttackBuffPermanent(CombatCtx& c, bool) { c.api.BuffPermanent(c.self, 1, 0); }
    void DrAddToHand(CombatCtx& c, bool) { c.api.RecordPersistentAddToHand(c.side, Cd("BG_BRM_006t"), false); }
    void AttackedPermanent(CombatCtx& c, bool golden)
    {
        Minion* m = c.api.Find(c.other);
        if (m && m->Has(kKwTaunt))
            c.api.BuffPermanent(c.other, golden ? 4 : 2, 0);
    }
    void KrushAttack(CombatCtx& c, bool)
    {
        uint32_t const id = c.SummonOne(Cd("BG22_001t2"), false);
        if (id != 0)
            c.api.ExtraAttack(id, c.other);
    }
    void MacawAfterAttack(CombatCtx& c, bool)
    {
        std::vector<uint32_t> const ids = c.Friends([&c](Minion const& m) { return HasDeathrattle(c.api.Effects(), m); });
        uint32_t const t = c.RandomOf(ids);
        if (t != 0)
            c.api.TriggerDeathrattle(t);
    }
    void DrProbeDead(CombatCtx& c, bool) { g_probe = static_cast<int>(c.api.DeadFriendlies(c.side).size()); }
    void SocProbePool(CombatCtx& c, bool)
    {
        g_probe = static_cast<int>(c.api.PoolCandidates(c.side, kTribeDemon, false).size());
        g_probe2 = static_cast<int>(c.api.PoolCandidates(c.side, 0, true).size());
    }
    void BuffedGiveHealth(CombatCtx& c, bool)
    {
        Minion* m = c.api.Find(c.other);
        if (m && m->IsTribe(kTribeDragon))
            c.api.Buff(c.other, 0, 1);
    }
    void SocBuffAttack(CombatCtx& c, bool) { c.api.Buff(c.self, 2, 0); }
    void ShopBuffedGiveHealth(ShopCtx& c, bool)
    {
        if (c.other && c.other->IsTribe(kTribeDragon))
            c.Buff(*c.other, 0, 1);
    }
    void PlayedProbe(ShopCtx& c, bool)
    {
        g_probe = c.playedCard;
        g_flag = c.magnetic;
        g_probe2 = c.other ? c.other->card : -1;
    }
    void SocFail(CombatCtx& c, bool)
    {
        for (int i = 0; i < 4; ++i)
            c.Summon(Cd("BG_BOT_312t"), false);
        g_probe = c.failedSummons;
    }

    EffectRegistry const& RegBrannKhadgar()
    {
        static EffectRegistry const reg = []
        {
            EffectRegistry r;
            EffectDef d;
            d.battlecry = &BcCount;
            r.Register("BG_EX1_103", d);                  // Coldlight Seer: counts its Battlecries
            d = EffectDef();
            d.battlecry = &BcSummon;
            r.Register("BG_CFM_315", d);                  // Alleycat: summons a cat
            d = EffectDef();
            d.battlecryTimes = 2;
            d.battlecryTimesGolden = 3;
            r.Register("BG_LOE_077", d);                  // Brann
            d = EffectDef();
            d.summonTimes = 2;
            d.summonTimesGolden = 3;
            r.Register("BG_DAL_575", d);                  // Khadgar
            return r;
        }();
        return reg;
    }

    void TestBrannKhadgarShop()
    {
        {
            Lobby L(1, 2, RegBrannKhadgar());
            g_count = 0;
            PlayCard(L, 0, "BG_EX1_103");
            CHECK(g_count == 1);
            OnBoard(L, 0, "BG_LOE_077");
            PlayCard(L, 0, "BG_EX1_103");
            CHECK(g_count == 3);
            // two Branns do not stack: the best one counts
            OnBoard(L, 0, "BG_LOE_077");
            CHECK(L.shop.BattlecryTimes(0) == 2);
            Lobby G(1, 2, RegBrannKhadgar());
            OnBoard(G, 0, "BG_LOE_077", true);
            g_count = 0;
            uint32_t const seer = PlayCard(G, 0, "BG_EX1_103");
            CHECK(g_count == 3);
            g_count = 0;
            G.shop.TriggerBattlecry(0, seer, 0);           // a triggered Battlecry is multiplied too
            CHECK(g_count == 3);
        }
        {
            Lobby L(2, 2, RegBrannKhadgar());
            PlayCard(L, 0, "BG_CFM_315");
            CHECK(L.S(0).board.Size() == 2);
            Lobby K(2, 2, RegBrannKhadgar());
            OnBoard(K, 0, "BG_DAL_575");
            PlayCard(K, 0, "BG_CFM_315");
            CHECK(K.S(0).board.Size() == 4);              // Khadgar, Alleycat, two cats
            Lobby KG(2, 2, RegBrannKhadgar());
            OnBoard(KG, 0, "BG_DAL_575", true);
            PlayCard(KG, 0, "BG_CFM_315", true);
            CHECK(KG.S(0).board.Size() == 5);             // three golden cats
            // a full board: the summons find no room
            Lobby F(2, 2, RegBrannKhadgar());
            for (char const* id : { "BG_EX1_534t", "BG_EX1_534t", "BG_CFM_315t", "BG_CFM_315t", "BG_CFM_316t" })   // no three alike: no triple
                OnBoard(F, 0, id);
            OnBoard(F, 0, "BG_DAL_575");
            PlayCard(F, 0, "BG_CFM_315");                  // Alleycat takes the 7th slot, both cats fail to fit
            CHECK(F.S(0).board.Size() == 7);
        }
    }

    void TestCombatMultipliers()
    {
        static EffectRegistry const reg = []
        {
            EffectRegistry r;
            EffectDef d;
            d.deathrattle = &DrSummon;
            r.Register("BG19_010", d);                    // Sewer Rat: a Microbot
            d = EffectDef();
            d.deathrattleTimes = 2;
            d.deathrattleTimesGolden = 3;
            r.Register("BG_FP1_031", d);                  // Baron Rivendare
            d = EffectDef();
            d.summonTimes = 2;
            d.summonTimesGolden = 3;
            r.Register("BG_DAL_575", d);                  // Khadgar
            d = EffectDef();
            d.startOfCombat = &SocFail;
            r.Register("BG21_025", d);                    // Omega Buster stand-in: counts failed summons
            return r;
        }();
        int const micro = Cd("BG_BOT_312t");
        std::vector<Minion> const enemy = { Body(3, 60) };
        auto rat = [] { return With(Mn("BG19_010"), 1, 1); };
        auto tough = [](char const* id, bool golden = false) { return With(Mn(id, golden), 0, 200); };
        CombatResult r = FightWith(reg, { rat() }, enemy);
        CHECK(SummonedCards(r, micro) == 1);
        r = FightWith(reg, { tough("BG_FP1_031"), rat() }, enemy);
        CHECK(SummonedCards(r, micro) == 2);
        r = FightWith(reg, { tough("BG_FP1_031", true), rat() }, enemy);
        CHECK(SummonedCards(r, micro) == 3);
        r = FightWith(reg, { tough("BG_DAL_575"), rat() }, enemy);
        CHECK(SummonedCards(r, micro) == 2);
        r = FightWith(reg, { tough("BG_DAL_575"), tough("BG_FP1_031"), rat() }, enemy);
        CHECK(SummonedCards(r, micro) == 4);              // Baron x Khadgar
        // 4 attempts, 3 slots left on a board of Khadgar + stand-in + 2 bodies -> counted failures
        g_probe = -1;
        r = FightWith(reg, { Body(0, 5), Body(0, 5), Body(0, 5), Mn("BG21_025"), Body(0, 5) }, { Body(0, 5) });
        CHECK(g_probe == 2);                              // 5 on board, 2 free slots: 2 summoned, 2 failed
    }

    void TestAttachedAndGrantedDeathrattles()
    {
        static EffectRegistry const reg = []
        {
            EffectRegistry r;
            EffectDef d;
            d.deathrattle = &DrSummon;
            r.Register("BG19_010", d);
            return r;
        }();
        int const micro = Cd("BG_BOT_312t");
        Minion host = With(Mn("BG_EX1_556t"), 1, 1);
        Attachment a;
        a.card = Cd("BG19_010");
        a.golden = true;
        host.attachments.push_back(a);
        CombatResult r = FightWith(reg, { host }, { Body(3, 50) });
        std::vector<CombatEvent> s = Events(r, EventType::Summon);
        CHECK(!s.empty() && s[0].card == micro && s[0].golden);   // the attachment's Deathrattle with its golden flag
        Minion host2 = With(Mn("BG_EX1_556t"), 1, 1);
        GrantedEffect g;
        g.card = Cd("BG19_010");
        host2.granted.push_back(g);
        r = FightWith(reg, { host2 }, { Body(3, 50) });
        CHECK(SummonedCards(r, micro) == 1);
        CHECK(HasDeathrattle(reg, host) && HasDeathrattle(reg, host2) && !HasDeathrattle(reg, Body(1, 1)));
    }

    void TestCleaveAndAttackedHook()
    {
        static EffectRegistry const reg = []
        {
            EffectRegistry r;
            EffectDef d;
            d.cleave = true;
            r.Register("BG_LOOT_078", d);                 // Cave Hydra
            d = EffectDef();
            d.onFriendAttacked = &AttackedPermanent;
            r.Register("BGS_110", d);                     // Arm of the Empire
            return r;
        }();
        for (uint32_t seed = 1; seed <= 12; ++seed)
        {
            CombatResult r = FightWith(reg, { With(Mn("BG_LOOT_078"), 3, 4) }, { Body(0, 20), Body(0, 20), Body(0, 20) }, seed);
            std::vector<CombatEvent> const atk = Events(r, EventType::Attack);
            CHECK(!atk.empty());
            if (atk.empty())
                continue;
            int const slot = static_cast<int>(atk[0].target) - 8;       // side 1 ids start at 8
            int const expected = 1 + (slot > 0 ? 1 : 0) + (slot < 2 ? 1 : 0);
            int dmg = 0;
            bool afterFirst = false;
            for (CombatEvent const& e : r.events)
            {
                if (e.type == EventType::Attack)
                {
                    if (afterFirst)
                        break;
                    afterFirst = true;
                }
                if (e.type == EventType::Damage && e.side == 1)
                    ++dmg;
            }
            CHECK(dmg == expected);
        }
        // onFriendAttacked + permanent buff -> persistent change on the input slot
        CombatResult r = FightWith(reg, { Mn("BGS_110"), Body(1, 5, kKwTaunt) }, { Body(1, 20) });
        bool found = false;
        for (PersistentChange const& c : r.persistent)
            if (c.kind == kPersistBuff && c.inputIndex == 1 && c.side == 0 && c.attack == 2)
                found = true;
        CHECK(found);
        r = FightWith(reg, { Mn("BGS_110", true), Body(1, 5, kKwTaunt) }, { Body(1, 20) });
        found = false;
        for (PersistentChange const& c : r.persistent)
            if (c.kind == kPersistBuff && c.inputIndex == 1 && c.attack == 4)
                found = true;
        CHECK(found);
    }

    void TestTarecgosaKeeps()
    {
        static EffectRegistry const reg = []
        {
            EffectRegistry r;
            EffectDef d;
            d.startOfCombat = &SocBuffSelf;
            d.keepCombatBuffs = 1;
            d.keepCombatBuffsGolden = 2;
            r.Register("BG21_015", d);
            return r;
        }();
        CombatResult r = FightWith(reg, { Mn("BG21_015") }, { Body(1, 3) });
        CHECK(r.persistent.size() == 1 && r.persistent[0].kind == kPersistBuff && r.persistent[0].attack == 3 &&
              r.persistent[0].health == 2 && r.persistent[0].inputIndex == 0);
        r = FightWith(reg, { Mn("BG21_015", true) }, { Body(1, 3) });
        CHECK(r.persistent.size() == 1 && r.persistent[0].attack == 12 && r.persistent[0].health == 8);   // doubled gains
    }

    void TestExtraAttackAndDeathrattleTrigger()
    {
        static EffectRegistry const reg = []
        {
            EffectRegistry r;
            EffectDef d;
            d.onAttack = &KrushAttack;
            r.Register("BG22_001", d);                    // Baby Krush
            d = EffectDef();
            d.afterAttack = &MacawAfterAttack;
            r.Register("BGS_078", d);                     // Monstrous Macaw
            d = EffectDef();
            d.deathrattle = &DrSummon;
            r.Register("BG19_010", d);                    // Sewer Rat
            d = EffectDef();
            d.deathrattle = &DrProbeDead;
            r.Register("BGS_012", d);                     // Kangor's Apprentice
            d = EffectDef();
            d.startOfCombat = &SocProbePool;
            r.Register("BG21_014", d);
            d = EffectDef();
            d.deathrattle = &DrCount;
            r.Register("BG21_000", d);
            return r;
        }();
        // Baby Krush: the Devilsaur attacks the same target first
        CombatResult r = FightWith(reg, { Mn("BG22_001") }, { Body(0, 10) });
        std::vector<CombatEvent> const atk = Events(r, EventType::Attack);
        CHECK(atk.size() >= 2 && atk[0].actor == 1 && atk[1].actor == kFirstSummonId && atk[1].target == atk[0].target);
        CHECK(r.winner == 0);
        // Macaw triggers a living Rat's Deathrattle: a Microbot appears before anything died
        r = FightWith(reg, { Mn("BGS_078"), With(Mn("BG19_010"), 1, 9) }, { Body(0, 40) });
        bool before = false;
        for (CombatEvent const& e : r.events)
        {
            if (e.type == EventType::Death)
                break;
            if (e.type == EventType::Summon && e.card == Cd("BG_BOT_312t"))
                before = true;
        }
        CHECK(before);
        // dead friendlies include the minion itself, in order
        g_probe = -1;
        FightWith(reg, { With(Mn("BGS_012"), 1, 1) }, { Body(5, 5) });
        CHECK(g_probe == 1);
        // pool candidates: Demons (and the Amalgam) of the slice; Deathrattle-only candidates need a registry entry
        g_probe = g_probe2 = -1;
        FightWith(reg, { Mn("BG21_014") }, { Body(0, 1) });
        int demons = 0;
        for (int c : CardsOfTribe(kTribeDemon, true))
            if (CardAt(c).role == CardRole::Pool)
                ++demons;
        CHECK(g_probe == demons);
        CHECK(g_probe2 == 3);                             // Rat, Kangor, Leapfrogger in this registry
    }

    void TestBuffedHooks()
    {
        static EffectRegistry const reg = []
        {
            EffectRegistry r;
            EffectDef d;
            d.onFriendBuffed = &BuffedGiveHealth;
            d.onFriendlyBuffedShop = &ShopBuffedGiveHealth;
            r.Register("BG21_013", d);                    // Whelp Smuggler
            d = EffectDef();
            d.startOfCombat = &SocBuffAttack;
            r.Register("BGS_019", d);                     // Red Whelp (a Dragon)
            return r;
        }();
        CombatResult r = FightWith(reg, { Mn("BG21_013"), With(Mn("BGS_019"), 2, 2) }, { Body(0, 100) });
        bool found = false;
        for (int i = 0; i < Alive(r, 0); ++i)
            if (Survivor(r, 0, i).card == Cd("BGS_019") && Survivor(r, 0, i).attack == 4 && Survivor(r, 0, i).health == 3)
                found = true;
        CHECK(found);
        Lobby L(1, 2, reg);
        OnBoard(L, 0, "BG21_013");
        uint32_t const whelp = OnBoard(L, 0, "BGS_019");
        Minion* w = Find(L, 0, whelp);
        int const h = w->health;
        L.shop.Buff(0, *w, 2, 0);
        CHECK(Find(L, 0, whelp)->health == h + 1 && Find(L, 0, whelp)->attack == 4);
    }

    void TestPlayedCardAndMagnetic()
    {
        static EffectRegistry const reg = []
        {
            EffectRegistry r;
            EffectDef d;
            d.onFriendlyPlayed = &PlayedProbe;
            r.Register("BGS_041", d);                     // Kalecgos
            return r;
        }();
        Lobby L(1, 2, reg);
        OnBoard(L, 0, "BGS_041");
        uint32_t const mech = OnBoard(L, 0, "BG_GVG_048");
        PlayCard(L, 0, "BG_BOT_563", false, -1, mech);   // Wargear (Magnetic) merges into the Mech
        CHECK(g_probe == Cd("BG_BOT_563") && g_flag && g_probe2 == Cd("BG_GVG_048"));
        PlayCard(L, 0, "BG20_401");
        CHECK(g_probe == Cd("BG20_401") && !g_flag);
    }

    void TestTavernBonusAndImmunity()
    {
        static EffectRegistry const reg = []
        {
            EffectRegistry r;
            EffectDef d;
            d.tavernAttack = 2;
            d.tavernHealth = 2;
            d.tavernAttackGolden = 4;
            d.tavernHealthGolden = 4;
            r.Register("BG23_361", d);                    // Legion Overseer
            d = EffectDef();
            d.heroImmune = true;
            r.Register("BG21_039", d);                    // Kathra'natir
            return r;
        }();
        Lobby L(1, 2, reg);
        NewOffers(L, 0, { "BG19_010", "BG19_010" });
        int const base = CardAt(Cd("BG19_010")).attack;
        uint32_t const legion = OnBoard(L, 0, "BG23_361");
        CHECK(L.S(0).offers[0].attack == base + 2 && L.S(0).offers[0].health == CardAt(Cd("BG19_010")).health + 2);
        SetGold(L, 0, 10);
        CHECK(L.shop.Buy(0, 0) == ActionResult::Ok);
        CHECK(L.S(0).hand[0].attack == base + 2 && L.S(0).hand[0].health == CardAt(Cd("BG19_010")).health + 2);   // a bought minion keeps the bonus
        Minion eaten;
        CHECK(L.shop.ConsumeOffer(0, 0, eaten) && eaten.attack == base + 2);
        NewOffers(L, 0, { "BG19_010" });
        L.S(0).board.Remove(L.S(0).board.IndexOfUid(legion));
        L.shop.Settle(0);
        CHECK(L.S(0).offers[0].attack == base);
        OnBoard(L, 0, "BG23_361", true);
        CHECK(L.S(0).offers[0].attack == base + 4);
        // Immune hero
        Lobby H(1, 2, reg);
        H.shop.DamageHero(0, 3);
        CHECK(H.shop.HeroHealth(0) == kStartHealth - 3);
        OnBoard(H, 0, "BG21_039");
        H.shop.DamageHero(0, 3);
        CHECK(H.shop.HeroHealth(0) == kStartHealth - 3 && H.shop.HeroMaxHealth(0) == kStartHealth);
    }

    void TestTypeHelpers()
    {
        Rng rng(5);
        std::vector<uint32_t> masks = { kTribeBeast, kTribeBeast, kTribeMech, kTribeAll, 0 };
        CHECK(CountDistinctTypes(masks) == 3);
        for (int i = 0; i < 20; ++i)
        {
            std::vector<int> const p = PickDistinctTypes(masks, rng, 9);
            CHECK(p.size() == 3);
            int beasts = 0;
            for (int x : p)
            {
                CHECK(x != 4);
                if (x <= 1)
                    ++beasts;
            }
            CHECK(beasts == 1 && std::count(p.begin(), p.end(), 2) == 1 && std::count(p.begin(), p.end(), 3) == 1);
            CHECK(PickDistinctTypes(masks, rng, 2).size() == 2);
        }
        Board b;
        b.Push(Mn("BG19_010"));
        b.Push(Mn("BG_BOT_312t"));
        uint32_t const absent = TribesNotControlled(b);
        CHECK((absent & kTribeBeast) == 0 && (absent & kTribeMech) == 0 && (absent & kTribeMurloc) != 0 &&
              (absent & kTribeAll) == 0);
        b.Push(Mn("BG_GIL_681"));
        CHECK(TribesNotControlled(b) == 0);
        CHECK(CountTribe(b, kTribeBeast) == 2 && CountTribe(b, kTribeBeast, 2) == 1);
    }

    void TestPersistentThroughGame()
    {
        static EffectRegistry const reg = []
        {
            EffectRegistry r;
            EffectDef d;
            d.onAttack = &AttackBuffPermanent;
            r.Register("BG_CFM_315", d);
            d = EffectDef();
            d.deathrattle = &DrAddToHand;
            r.Register("BG19_010", d);
            return r;
        }();
        ManualGame G(3, reg);
        for (int p = 0; p < kPlayers; ++p)
        {
            Minion cat = With(Mn("BG_CFM_315", false, G.game.GetShop().NewUid()), 1, 30);
            Minion rat = With(Mn("BG19_010", false, G.game.GetShop().NewUid()), 1, 1);
            G.S(p).board.Push(cat);
            G.S(p).board.Push(rat);
        }
        G.EndRound();
        int buffed = 0;
        int hands = 0;
        for (int p = 0; p < kPlayers; ++p)
        {
            if (G.S(p).board.Size() >= 1 && G.S(p).board[0].attack > 1)
                ++buffed;
            hands += G.S(p).hand.Size();
            CHECK(G.game.GetShop().LastOpponentBoard(p) != nullptr);
        }
        CHECK(buffed == kPlayers);                       // every Alleycat attacked, so every one grew permanently
        CHECK(hands >= 1);                               // at least one Rat died: a card was added to its owner's hand
        CHECK(G.game.GetShop().LastOpponentBoard(0)->Size() == 2);
    }

    void RunCoreTests()
    {
        RUN(TestBrannKhadgarShop);
        RUN(TestCombatMultipliers);
        RUN(TestAttachedAndGrantedDeathrattles);
        RUN(TestCleaveAndAttackedHook);
        RUN(TestTarecgosaKeeps);
        RUN(TestExtraAttackAndDeathrattleTrigger);
        RUN(TestBuffedHooks);
        RUN(TestPlayedCardAndMagnetic);
        RUN(TestTavernBonusAndImmunity);
        RUN(TestTypeHelpers);
        RUN(TestPersistentThroughGame);
    }

    // ---------------------------------------------------------------------------------------- registry coverage

    // Pool minions whose text is more than keywords and that therefore need an entry in the registry. Not listed:
    // Pupbot, Bronze Warden, Wargear, Annoy-o-Module (keywords only), Nightmare Amalgam (its tribe is table data) and
    // Agamaggan (only boosts Blood Gems).
    char const* const kNeedsEntry[] = {
        "BG19_010", "BG20_401", "BG21_000", "BG21_001", "BG21_002", "BG21_003", "BG21_004", "BG21_005", "BG21_006",
        "BG21_007", "BG21_008", "BG21_010", "BG21_013", "BG21_014", "BG21_015", "BG21_023", "BG21_024", "BG21_025",
        "BG21_027", "BG21_039", "BG22_001", "BG22_202", "BG22_401", "BG22_402", "BG22_403", "BG23_016", "BG23_190",
        "BG23_318", "BG23_350", "BG23_353", "BG23_357", "BG23_361", "BG23_362", "BG24_003", "BG24_009", "BG24_020",
        "BG24_022", "BG24_500", "BGS_002", "BGS_004", "BGS_008", "BGS_009", "BGS_010", "BGS_012", "BGS_014",
        "BGS_018", "BGS_019", "BGS_020", "BGS_021", "BGS_022", "BGS_030", "BGS_036", "BGS_038", "BGS_040",
        "BGS_041", "BGS_043", "BGS_044", "BGS_045", "BGS_067", "BGS_071", "BGS_075", "BGS_078", "BGS_083",
        "BGS_110", "BGS_204", "BG_AT_069", "BG_AV_309", "BG_BOT_312", "BG_BOT_606", "BG_BT_010", "BG_CFM_315",
        "BG_CFM_316", "BG_DAL_077", "BG_DAL_575", "BG_DAL_775", "BG_DMF_533", "BG_DS1_070", "BG_EX1_103",
        "BG_EX1_507", "BG_EX1_531", "BG_EX1_534", "BG_EX1_556", "BG_FP1_031", "BG_GVG_027", "BG_GVG_048",
        "BG_GVG_113", "BG_ICC_807", "BG_LOE_077", "BG_LOOT_078", "BG_LOOT_368", "BG_OG_221", "BG_OG_256",
        "BG_ULD_217", "BG_UNG_073",
        // Elementals: Crackling Cyclone is keywords only; the rest need an entry.
        "BG21_020", "BG21_021", "BG21_036", "BG21_040", "BG23_352", "BG24_018", "BGS_100", "BGS_104",
        "BGS_105", "BGS_115", "BGS_116", "BGS_120", "BGS_121", "BGS_122", "BGS_123", "BGS_124", "BGS_126",
        "BGS_127",
        // Pirates: every one of them has an entry (the Sky Pirate token has none).
        "BG21_016", "BG21_019", "BG21_031", "BG23_192", "BGS_047", "BGS_048", "BGS_049", "BGS_055",
        "BGS_056", "BGS_060", "BGS_061", "BGS_066", "BGS_072", "BGS_081", "BG_NEW1_027",
        // Naga: every minion has an entry; the spells have their onCast entries too.
        "BG23_000", "BG23_001", "BG23_002", "BG23_003", "BG23_004", "BG23_006", "BG23_007", "BG23_008",
        "BG23_009", "BG23_010", "BG23_011", "BG23_012", "BG23_013", "BG23_014", "BG23_015", "BGS_200",
        // Quilboar: every minion has an entry, plus Agamaggan and the Blood Gem spell.
        "BG20_GEM", "BG20_100", "BG20_101", "BG20_102", "BG20_103", "BG20_104", "BG20_105", "BG20_106",
        "BG20_201", "BG20_202", "BG20_203", "BG20_204", "BG20_205", "BG20_207", "BG20_301", "BG20_302",
        "BG20_303", "BG21_037", "BG23_018",
        // The Fish of N'Zoth (a hero token) has its own entry; the Tentacle is stats and Taunt only.
        "TB_BaconShop_HP_105t",
    };

    void TestRegistryCoverage()
    {
        EffectRegistry const& reg = EffectRegistry::Global();
        CHECK(reg.Errors().empty());
        for (std::string const& e : reg.Errors())
            std::printf("    registry error: %s\n", e.c_str());
        std::vector<std::string> missing;
        for (char const* id : kNeedsEntry)
        {
            int const c = FindCard(id);
            CHECK(c >= 0 && CardAt(c).slice15A);
            if (c >= 0 && !reg.Find(c))
                missing.push_back(id);
        }
        std::printf("    registry: %d entries, %d of %d effect minions missing%s\n", reg.Size(), static_cast<int>(missing.size()),
                    static_cast<int>(sizeof(kNeedsEntry) / sizeof(kNeedsEntry[0])),
                    kRequireAllEffects ? "" : " (report only, kRequireAllEffects = false)");
        if (!missing.empty())
        {
            std::string line = "    missing:";
            for (std::string const& m : missing)
                line += " " + m;
            std::printf("%s\n", line.c_str());
        }
        if (kRequireAllEffects)
            CHECK(missing.empty());
    }
}

// ------------------------------------------------------------------------------------------------ batch tests

#include "bg_effects_beast_tests.inc"
#include "bg_effects_mech_tests.inc"
#include "bg_effects_murloc_tests.inc"
#include "bg_effects_demon_tests.inc"
#include "bg_effects_dragon_tests.inc"
#include "bg_effects_elemental_tests.inc"
#include "bg_effects_pirate_tests.inc"
#include "bg_effects_naga_tests.inc"
#include "bg_effects_quilboar_tests.inc"
#include "bg_effects_neutral_tests.inc"

int main(int, char**)
{
    RunCoreTests();
    RUN(TestRegistryCoverage);
    RunBeastTests();
    RunMechTests();
    RunMurlocTests();
    RunDemonTests();
    RunDragonTests();
    RunElementalTests();
    RunPirateTests();
    RunNagaTests();
    RunQuilboarTests();
    RunNeutralTests();
    std::printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
