// Standalone tests of the Battlegrounds core (src/battlegrounds): the generated card table, the types, the
// effect registry with its hooks and the combat simulator with keyword rules.
//   bg_combat_tests.exe [directory]      run all tests; exit code 0 = all checks passed.
//
// The sample effects below exist only to prove that the hooks fire; they are registered into test registries (or, for
// one card, through HS_BG_EFFECT_BATCH to prove the batch path), never into the real minion set.

#include "../src/battlegrounds/HsBgCombat.h"
#include "../src/battlegrounds/HsBgEffects.h"
#include "../src/battlegrounds/HsBgTypes.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
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
    constexpr uint32_t kKeep = 0xFFFFFFFFu;

    // A vanilla body of a tier: a real card (never one with a sample effect) with the stats overridden.
    int VanillaCard(int tier)
    {
        static char const* const ids[] = { "", "BG21_027", "BG21_013", "BG23_361", "BG23_362", "BG20_401", "BG24_020" };
        return FindCard(ids[tier]);
    }

    Minion V(int attack, int health, uint32_t keywords = 0, int tier = 1)
    {
        Minion m = MakeMinion(VanillaCard(tier));
        m.attack = attack;
        m.health = m.maxHealth = health;
        m.keywords = keywords;
        return m;
    }

    // A minion of a named card; stats / keywords overridden when given.
    Minion C(char const* id, int attack = -1, int health = -1, uint32_t keywords = kKeep, bool golden = false)
    {
        int const card = FindCard(id);
        CHECK(card >= 0);
        Minion m = MakeMinion(card < 0 ? 0 : card, golden);
        if (attack >= 0)
            m.attack = attack;
        if (health >= 0)
            m.health = m.maxHealth = health;
        if (keywords != kKeep)
            m.keywords = keywords;
        return m;
    }

    CombatSide Side(std::initializer_list<Minion> minions, int tavernTier = 1)
    {
        CombatSide s;
        s.tavernTier = tavernTier;
        for (Minion const& m : minions)
            s.board.Push(m);
        return s;
    }

    EffectRegistry const& None()
    {
        static EffectRegistry const r;
        return r;
    }

    CombatSettings Plain(bool cap = true)
    {
        CombatSettings s;
        s.effects = &None();
        s.capDamage = cap;
        return s;
    }

    // The Secrets of The Great Akazamzarak. A side's Secrets live in CombatSide::secrets, trigger in play
    // order and come back in CombatResult::secretsUsed.
    CombatSide WithSecret(CombatSide s, char const* id)
    {
        int const card = FindCard(id);
        CHECK(card >= 0);
        if (card >= 0)
            s.secrets.push_back(card);
        return s;
    }

    bool HasSummon(CombatResult const& r, int side, int card, int amount = -999, int amount2 = -999)
    {
        for (CombatEvent const& e : r.events)
            if (e.type == EventType::Summon && e.side == side && e.card == card &&
                (amount == -999 || e.amount == amount) && (amount2 == -999 || e.amount2 == amount2))
                return true;
        return false;
    }

    void TestSecretAttackTriggers()
    {
        int const venom = FindCard("TB_Bacon_Secrets_01");
        int const snakeTrap = FindCard("TB_Bacon_Secrets_02");
        int const splitting = FindCard("TB_Bacon_Secrets_04");
        int const autodefense = FindCard("TB_Bacon_Secrets_07");
        int const cobra = FindCard("EX1_170");
        int const snake = FindCard("EX1_554t");
        CHECK(venom >= 0 && snakeTrap >= 0 && splitting >= 0 && autodefense >= 0 && cobra >= 0 && snake >= 0);

        {
            // Venomstrike Trap: a 2/3 Poisonous Cobra, and the secret is spent.
            CombatResult r = SimulateCombat(Side({ V(10, 10), V(10, 10) }), WithSecret(Side({ V(1, 1) }), "TB_Bacon_Secrets_01"), 21, Plain());
            CHECK(r.secretsUsed[1].size() == 1 && r.secretsUsed[1][0] == venom);
            CHECK(HasSummon(r, 1, cobra, 2, 3));
            CHECK(r.secretsUsed[0].empty());
        }
        {
            // Snake Trap: three 1/1 Snakes; two secrets on the same side both trigger, in play order.
            CombatSide b = WithSecret(WithSecret(Side({ V(1, 1) }), "TB_Bacon_Secrets_01"), "TB_Bacon_Secrets_02");
            CombatResult r = SimulateCombat(Side({ V(10, 10), V(10, 10) }), b, 22, Plain());
            CHECK(r.secretsUsed[1].size() == 2 && r.secretsUsed[1][0] == venom && r.secretsUsed[1][1] == snakeTrap);
            int snakes = 0;
            for (CombatEvent const& e : r.events)
                if (e.type == EventType::Summon && e.side == 1 && e.card == snake)
                    ++snakes;
            CHECK(snakes == 3);
        }
        {
            // Splitting Image: a copy of the attacked minion with its current stats.
            CombatResult r = SimulateCombat(Side({ V(4, 4), V(4, 4) }), WithSecret(Side({ V(3, 3, 0, 2) }), "TB_Bacon_Secrets_04"), 23, Plain());
            CHECK(r.secretsUsed[1].size() == 1 && r.secretsUsed[1][0] == splitting);
            CHECK(HasSummon(r, 1, VanillaCard(2), 3, 3));
        }
        {
            // Autodefense Matrix: Divine Shield before the damage, so the attacked minion survives it.
            CombatResult r = SimulateCombat(Side({ V(3, 3), V(3, 3) }), WithSecret(Side({ V(5, 5) }), "TB_Bacon_Secrets_07"), 24, Plain());
            CHECK(r.secretsUsed[1].size() == 1 && r.secretsUsed[1][0] == autodefense);
            bool shielded = false;
            for (CombatEvent const& e : r.events)
                if (e.type == EventType::Buff && e.side == 1 && (e.keywords & kKwDivineShield) != 0)
                    shielded = true;
            CHECK(shielded);
            CHECK(r.survivors[1].Size() == 1);
        }
    }

    void TestSecretDeathTriggers()
    {
        int const effigy = FindCard("TB_Bacon_Secrets_05");
        int const avenge = FindCard("TB_Bacon_Secrets_08");
        int const redemption = FindCard("TB_Bacon_Secrets_10");
        int const salvation = FindCard("TB_Bacon_Secrets_11");
        CHECK(effigy >= 0 && avenge >= 0 && redemption >= 0 && salvation >= 0);

        {
            // Effigy: a random minion of the dead minion's Tavern Tier (2 here).
            CombatResult r = SimulateCombat(Side({ V(10, 10) }), WithSecret(Side({ V(1, 1, 0, 2) }), "TB_Bacon_Secrets_05"), 31, Plain());
            CHECK(r.secretsUsed[1].size() == 1 && r.secretsUsed[1][0] == effigy);
            bool tier2 = false;
            for (CombatEvent const& e : r.events)
                if (e.type == EventType::Summon && e.side == 1 && e.card >= 0 && CardAt(e.card).tier == 2)
                    tier2 = true;
            CHECK(tier2);
        }
        {
            // Avenge: +3/+2 to a random friend (the survivor here).
            CombatResult r = SimulateCombat(Side({ V(2, 2) }), WithSecret(Side({ V(1, 1), V(1, 1) }), "TB_Bacon_Secrets_08"), 32, Plain());
            CHECK(r.secretsUsed[1].size() == 1 && r.secretsUsed[1][0] == avenge);
            bool buffed = false;
            for (CombatEvent const& e : r.events)
                if (e.type == EventType::Buff && e.side == 1 && e.amount == 3 && e.amount2 == 2)
                    buffed = true;
            CHECK(buffed);
        }
        {
            // Redemption: the dead minion comes back with 1 Health.
            CombatResult r = SimulateCombat(Side({ V(10, 10) }), WithSecret(Side({ V(1, 1) }), "TB_Bacon_Secrets_10"), 33, Plain());
            CHECK(r.secretsUsed[1].size() == 1 && r.secretsUsed[1][0] == redemption);
            CHECK(HasSummon(r, 1, VanillaCard(1), 1, 1));
        }
        {
            // Hand of Salvation: the second friendly death of the combat comes back (full Health).
            CombatResult r = SimulateCombat(Side({ V(10, 10) }), WithSecret(Side({ V(1, 1), V(1, 1) }), "TB_Bacon_Secrets_11"), 34, Plain());
            CHECK(r.secretsUsed[1].size() == 1 && r.secretsUsed[1][0] == salvation);
            bool revived = false;
            for (CombatEvent const& e : r.events)
                if (e.type == EventType::Summon && e.side == 1 && e.card == VanillaCard(1) && e.amount2 == 1)
                    revived = true;
            CHECK(revived);
        }
        {
            // A secret that never triggers stays in the list (Hand of Salvation on the first death only).
            CombatResult r = SimulateCombat(Side({ V(10, 10) }), WithSecret(Side({ V(1, 1) }), "TB_Bacon_Secrets_11"), 35, Plain());
            CHECK(r.secretsUsed[1].empty());
        }
    }

    std::vector<CombatEvent> Of(CombatResult const& r, EventType type)
    {
        std::vector<CombatEvent> out;
        for (CombatEvent const& e : r.events)
            if (e.type == type)
                out.push_back(e);
        return out;
    }

    // Targets / actors of the attacks of one side, in order.
    std::vector<uint32_t> Attackers(CombatResult const& r, int side)
    {
        std::vector<uint32_t> out;
        for (CombatEvent const& e : r.events)
            if (e.type == EventType::Attack && e.side == side)
                out.push_back(e.actor);
        return out;
    }

    int Triggers(CombatResult const& r, uint32_t actor, HookKind kind)
    {
        int n = 0;
        for (CombatEvent const& e : r.events)
            if (e.type == EventType::Trigger && e.actor == actor && e.amount == static_cast<int>(kind))
                ++n;
        return n;
    }

    bool SameBoard(Board const& a, Board const& b)
    {
        if (a.Size() != b.Size())
            return false;
        for (int i = 0; i < a.Size(); ++i)
            if (a[i].card != b[i].card || a[i].golden != b[i].golden || a[i].attack != b[i].attack ||
                a[i].health != b[i].health || a[i].keywords != b[i].keywords || a[i].uid != b[i].uid)
                return false;
        return true;
    }

    // Replays the event log and checks it against the result: ids unique, nothing acts after death, boards never above
    // 7, health accounting (damage and buffs) equals the final health of every survivor, winner and damage consistent.
    void CheckLog(CombatResult const& r, bool exactHealth = true)
    {
        struct Track { int side; int health; bool alive; };
        std::map<uint32_t, Track> t;
        int count[2] = { 0, 0 };
        for (int s = 0; s < 2; ++s)
            for (int i = 0; i < r.start[s].Size(); ++i)
            {
                Minion const& m = r.start[s][i];
                CHECK(m.uid == static_cast<uint32_t>(s * kBoardMax + i + 1));
                t[m.uid] = Track{ s, m.health, true };
                ++count[s];
            }
        for (CombatEvent const& e : r.events)
        {
            switch (e.type)
            {
            case EventType::Attack:
                CHECK(t.count(e.actor) && t[e.actor].alive && t.count(e.target) && t[e.target].alive);
                CHECK(t[e.actor].side != t[e.target].side);
                break;
            case EventType::Damage:
                CHECK(t.count(e.target) && t[e.target].alive && e.amount > 0);
                t[e.target].health -= e.amount;
                break;
            case EventType::ShieldPop:
                CHECK(t.count(e.target) && t[e.target].alive);
                break;
            case EventType::Death:
                CHECK(t.count(e.target) && t[e.target].alive);
                t[e.target].alive = false;
                --count[e.side];
                CHECK(count[e.side] >= 0);
                break;
            case EventType::Summon:
            case EventType::Reborn:
                CHECK(!t.count(e.target));
                t[e.target] = Track{ e.side, e.amount2, true };
                ++count[e.side];
                CHECK(count[e.side] <= kBoardMax);
                break;
            case EventType::Buff:
                CHECK(t.count(e.target) && t[e.target].alive);
                t[e.target].health += e.amount2;
                break;
            case EventType::Trigger:
                break;
            }
        }
        int alive[2] = { 0, 0 };
        for (int s = 0; s < 2; ++s)
        {
            CHECK(r.survivors[s].Size() == count[s]);
            CHECK(r.survivors[s].Size() <= kBoardMax);
            for (int i = 0; i < r.survivors[s].Size(); ++i)
            {
                Minion const& m = r.survivors[s][i];
                CHECK(t.count(m.uid) && t[m.uid].alive && t[m.uid].side == s);
                if (exactHealth)
                    CHECK(t[m.uid].health == m.health);
                CHECK(m.health > 0);
                ++alive[s];
            }
        }
        if (!r.timedOut && !r.stalled)
            CHECK(alive[0] == 0 || alive[1] == 0);
        if (alive[0] > 0 && alive[1] == 0)
            CHECK(r.winner == 0);
        else if (alive[1] > 0 && alive[0] == 0)
            CHECK(r.winner == 1);
        else
            CHECK(r.winner == -1 && r.damage == 0);
    }

    // ------------------------------------------------------------------------------------ sample effects (test only)

    int g_fakeShopGold = 0;

    void SummonHalfShell(CombatCtx& c, bool golden) { c.Summon(FindCard("BG19_010t"), golden); }            // Sewer Rat
    void SummonRats(CombatCtx& c, bool golden)                                                               // Rat Pack
    {
        for (int i = 0; i < 3; ++i)
            c.Summon(FindCard("BG_CFM_316t"), golden);
    }
    void WhelpStart(CombatCtx& c, bool golden) { c.api.Buff(c.self, golden ? 2 : 1, 0); }                   // Red Whelp
    void HyenaFriendDied(CombatCtx& c, bool)                                                                 // Scavenging Hyena
    {
        if (c.dead && c.dead->IsTribe(kTribeBeast))
            c.api.Buff(c.self, 2, 1);
    }
    void TankAvenge(CombatCtx& c, bool golden) { c.api.Buff(c.self, golden ? 4 : 2, 0); }                   // Mechano-Tank
    void WarleaderAura(AuraCtx& a, bool golden)                                                              // Murloc Warleader
    {
        for (int i = 0; i < a.board.Size(); ++i)
            if (i != a.index && a.board[i].IsTribe(kTribeMurloc))
                a.Give(i, golden ? 4 : 2, 0);
    }
    void AgamaggAura(AuraCtx& a, bool)                                                                       // health aura sample
    {
        for (int i = 0; i < a.board.Size(); ++i)
            if (i != a.index)
                a.Give(i, 0, 2);
    }
    void WeaverDamaged(CombatCtx& c, bool)                                                                   // Wrath Weaver
    {
        if (c.other != c.self)
            return;
        if (Minion* m = c.Self())
        {
            ++m->counters[1];
            m->counters[2] += c.amount;
        }
    }
    uint32_t ZappTarget(CombatCtx& c, bool)                                                                  // Zapp Slywick
    {
        uint32_t best = 0;
        int bestAttack = 0;
        for (uint32_t id : c.api.BoardIds(c.EnemySide()))
        {
            int const a = c.api.Find(id)->attack;
            if (best == 0 || a < bestAttack)
            {
                best = id;
                bestAttack = a;
            }
        }
        return best;
    }
    void PupbotShieldLost(CombatCtx& c, bool)                                                                // Pupbot
    {
        if (Minion* m = c.Self())
            ++m->counters[1];
    }
    void KaboomDeathrattle(CombatCtx& c, bool golden)                                                        // Kaboom Bot
    {
        for (uint32_t id : c.api.BoardIds(c.EnemySide()))
            c.api.Damage(c.self, id, golden ? 2 : 1);
    }
    void AlleycatSummoned(CombatCtx& c, bool)                                                                // Alleycat
    {
        if (Minion* m = c.Self())
            ++m->counters[1];
    }
    void BearOnAttack(CombatCtx& c, bool) { c.api.Buff(c.self, 1, 1); }                                     // Mama Bear
    void LeapAfterAttack(CombatCtx& c, bool)                                                                 // Leapfrogger
    {
        if (Minion* m = c.Self())
            m->counters[1] += c.amount;
        c.api.RecordPersistent(c.self, 7, c.amount);
    }
    void AlleycatBattlecry(ShopCtx& c, bool golden) { c.api.AddGold(c.player, golden ? 2 : 1); }

    EffectRegistry const& Samples()
    {
        static EffectRegistry const reg = []
        {
            EffectRegistry r;
            EffectDef d;
            d.deathrattle = &SummonHalfShell;
            r.Register("BG19_010", d);
            d = EffectDef();
            d.deathrattle = &SummonRats;
            r.Register("BG_CFM_316", d);
            d = EffectDef();
            d.startOfCombat = &WhelpStart;
            r.Register("BGS_019", d);
            d = EffectDef();
            d.onFriendDied = &HyenaFriendDied;
            r.Register("BG_EX1_531", d);
            d = EffectDef();
            d.avenge = &TankAvenge;
            d.avengeCount = 2;
            d.avengeCountGolden = 1;
            r.Register("BG21_023", d);
            d = EffectDef();
            d.aura = &WarleaderAura;
            r.Register("BG_EX1_507", d);
            d = EffectDef();
            d.aura = &AgamaggAura;
            r.Register("BG20_205", d);
            d = EffectDef();
            d.onDamaged = &WeaverDamaged;
            r.Register("BGS_004", d);
            d = EffectDef();
            d.chooseTarget = &ZappTarget;
            r.Register("BGS_022", d);
            d = EffectDef();
            d.onShieldLost = &PupbotShieldLost;
            r.Register("BG21_022", d);
            d = EffectDef();
            d.deathrattle = &KaboomDeathrattle;
            r.Register("BG_BOT_606", d);
            d = EffectDef();
            d.onFriendSummoned = &AlleycatSummoned;
            d.battlecry = &AlleycatBattlecry;
            r.Register("BG_CFM_315", d);
            d = EffectDef();
            d.onAttack = &BearOnAttack;
            r.Register("BGS_021", d);
            d = EffectDef();
            d.afterAttack = &LeapAfterAttack;
            r.Register("BG21_000", d);
            return r;
        }();
        return reg;
    }

    CombatSettings WithSamples()
    {
        CombatSettings s;
        s.effects = &Samples();
        return s;
    }

    // Registered through the batch macro into EffectRegistry::Global(): Razorgore gains +1 attack at Start of Combat.
    void RazorgoreStart(CombatCtx& c, bool) { c.api.Buff(c.self, 1, 0); }
    void RegisterTestBatch(EffectRegistry& r)
    {
        EffectDef d;
        d.startOfCombat = &RazorgoreStart;
        r.Register("BGS_036", d);
    }
    HS_BG_EFFECT_BATCH(TestOnly, &RegisterTestBatch);

    // ------------------------------------------------------------------------------------------------ card table

    void TestCardTable()
    {
        int pool = 0, tokens = 0, spells = 0, slice = 0;
        std::set<std::string> ids;
        for (int i = 0; i < CardCount(); ++i)
        {
            CardDef const& d = CardAt(i);
            CHECK(ids.insert(d.id).second);
            CHECK(FindCard(d.id) == i);
            if (d.role == CardRole::Spell)
            {
                ++spells;
                CHECK(d.tier >= 0 && d.tier <= kMaxTier);
                CHECK(d.attack == 0 && d.health == 0);
                CHECK(d.tribes == 0 && d.keywords == 0);
                CHECK(d.slice15A);
                if (d.goldenId[0])
                {
                    bool g = false;
                    CHECK(FindCardAny(d.goldenId, g) == i && g);
                }
                continue;
            }
            CHECK(d.tier >= kMinTier && d.tier <= kMaxTier);
            CHECK(d.attack >= 0 && d.health > 0);
            if (d.goldenId[0])
            {
                bool g = false;
                CHECK(FindCardAny(d.goldenId, g) == i && g);
                CHECK(d.goldenAttack >= d.attack && d.goldenHealth >= d.health);
            }
            if (d.role == CardRole::Pool)
            {
                ++pool;
                CHECK(d.goldenId[0] != '\0');
                // Monstrous Macaw follows its renders (4/3, golden 10/6; data/battlegrounds/render_overrides.json)
                if (std::string(d.id) != "BGS_078")
                    CHECK(d.goldenAttack == 2 * d.attack && d.goldenHealth == 2 * d.health);
                if (d.slice15A)
                    ++slice;
            }
            else
            {
                ++tokens;
                CHECK(d.tier == 1 && d.slice15A);
            }
        }
        // The 16 Quilboar (+ Agamaggan) and the Blood Gem spell, the Fish and the Tentacle,
        // Bru'kan's Stone Elemental and the 29 Darkmoon Prizes.
        CHECK(CardCount() == 239 && pool == 167 && tokens == 21 && spells == 51 && slice == 167);
        CHECK(FindCard("NOPE") < 0);
        bool g = true;
        CHECK(FindCardAny("NOPE", g) < 0);
        // tiers: 20 / 30 / 33 / 34 / 28 / 22 pool minions
        int const perTier[] = { 20, 30, 33, 34, 28, 22 };
        for (int t = 1; t <= 6; ++t)
            CHECK(static_cast<int>(CardsOfTier(t).size()) == perTier[t - 1]);
        // tribes: Beast 16 (+Amalgam), Mech 15, Murloc 12 ...
        CHECK(CardsOfTribe(kTribeBeast).size() == 17);
        CHECK(CardsOfTribe(kTribeMech).size() == 16);
        CHECK(CardsOfTribe(kTribeMurloc).size() == 13);
        CHECK(CardsOfTribe(kTribeBeast, true).size() == 17);
        CHECK(CardsOfTribe(kTribePirate, true).size() == 16); // the 15 Pirates + Nightmare Amalgam
        int const amalgam = FindCard("BG_GIL_681");
        CHECK(amalgam >= 0 && CardAt(amalgam).tribes == kTribeAll && HasTribe(CardAt(amalgam).tribes, kTribeDragon));
        // tokens
        int const half = FindCard("BG19_010t");
        CHECK(half >= 0 && CardAt(half).role == CardRole::Token && CardAt(half).attack == 2 && CardAt(half).health == 3);
        CHECK(CardAt(half).keywords == kKwTaunt && CardAt(half).goldenKeywords == kKwTaunt);
        CHECK(FindCard("BG23_353_Gt") >= 0 && CardAt(FindCard("BG23_353_Gt")).goldenId[0] == '\0');
        CardQuery q;
        q.pool = false;
        q.tokens = true;
        CHECK(QueryCards(q).size() == 21);   // adds the Cobra and the Snake
        // keywords: Sewer Rat has none; Zapp has Windfury (only in its text), its golden too (render override); Divine Shield + Reborn; Magnetic on the Mechs
        CHECK(CardAt(FindCard("BG19_010")).keywords == 0);
        CHECK(CardAt(FindCard("BGS_022")).keywords == kKwWindfury && CardAt(FindCard("BGS_022")).goldenKeywords == kKwWindfury);
        CHECK(CardAt(FindCard("BGS_034")).keywords == (kKwDivineShield | kKwReborn));
        CHECK(CardAt(FindCard("BG_BOT_911")).keywords == (kKwTaunt | kKwDivineShield | kKwMagnetic));
        CHECK(CardAt(FindCard("BG_AT_069")).keywords == kKwTaunt);      // Taunt only in the text of the data
        CHECK(CardAt(FindCard("BGS_071")).keywords == kKwDivineShield); // likewise
        CHECK(CardAt(FindCard("BG22_402")).keywords == kKwVenomous);    // Mantid Queen: Venomous (render override)
        CHECK(CardAt(FindCard("BG21_024")).keywords == kKwDivineShield); // Grease Bot: Divine Shield (render override)
        int magnetic = 0, windfury = 0, mega = 0;
        for (int i = 0; i < CardCount(); ++i)
        {
            magnetic += (CardAt(i).keywords & kKwMagnetic) ? 1 : 0;
            windfury += (CardAt(i).keywords & kKwWindfury) ? 1 : 0;
            mega += (CardAt(i).goldenKeywords & kKwMegaWindfury) ? 1 : 0;
            CHECK(!(CardAt(i).keywords & kKwMegaWindfury));
            if (std::string(CardAt(i).id) != "BGS_022" && std::string(CardAt(i).id) != "BG20_104" &&
                std::string(CardAt(i).id) != "BG20_204")   // golden Bonker / Bristleback Knight keep Windfury (render overrides)
                CHECK(!(CardAt(i).goldenKeywords & kKwWindfury));
        }
        CHECK(magnetic == 3 && windfury == 4 && mega == 1);
        CHECK(AttacksPerTurn(0) == 1 && AttacksPerTurn(kKwWindfury) == 2 && AttacksPerTurn(kKwMegaWindfury) == 4);
    }

    void TestRng()
    {
        Rng a(42), b(42), c(43);
        bool same = true, differs = false;
        for (int i = 0; i < 100; ++i)
        {
            uint32_t const x = a.Next(), y = b.Next(), z = c.Next();
            same = same && x == y;
            differs = differs || x != z;
        }
        CHECK(same && differs);
        // the first outputs of mt19937 seed 5489 (the standard's reference value) pin the engine
        Rng ref(5489);
        CHECK(ref.Next() == 3499211612u);
        Rng u(7);
        int bins[6] = { 0, 0, 0, 0, 0, 0 };
        for (int i = 0; i < 6000; ++i)
        {
            uint32_t const v = u.Uniform(6);
            CHECK(v < 6);
            ++bins[v];
        }
        for (int n : bins)
            CHECK(n > 850 && n < 1150);
        CHECK(u.Uniform(0) == 0 && u.Uniform(1) == 0);
        for (int i = 0; i < 200; ++i)
        {
            int const r = u.Range(-3, 3);
            CHECK(r >= -3 && r <= 3);
        }
        CHECK(u.Range(5, 5) == 5 && u.Range(9, 2) == 9);
        std::vector<int> v = { 1, 2, 3, 4, 5, 6, 7, 8 };
        u.Shuffle(v);
        std::sort(v.begin(), v.end());
        CHECK(v == std::vector<int>({ 1, 2, 3, 4, 5, 6, 7, 8 }));
    }

    void TestSlots()
    {
        Board b;
        for (int i = 0; i < kBoardMax; ++i)
            CHECK(b.Push(V(i + 1, 1)));
        CHECK(b.Full() && !b.Push(V(9, 9)) && b.Size() == 7 && Board::Capacity() == 7);
        Minion const gone = b.Remove(0);
        CHECK(gone.attack == 1 && b.Size() == 6 && b[0].attack == 2);
        CHECK(b.Insert(100, V(8, 8)) && b[6].attack == 8);   // clamped to the end
        CHECK(b.Insert(-5, V(9, 9)) == false);               // full again
        b.Remove(6);
        CHECK(b.Insert(-5, V(9, 9)) && b[0].attack == 9);    // clamped to the start
        Hand h;
        for (int i = 0; i < kHandMax; ++i)
            CHECK(h.Push(V(1, 1)));
        CHECK(h.Full() && !h.Push(V(1, 1)) && Hand::Capacity() == 10);
        Minion m = MakeMinion(FindCard("BG19_010"), true, 77);
        CHECK(m.attack == 6 && m.health == 4 && m.maxHealth == 4 && m.golden && m.uid == 77);
        m.attachments.push_back(Attachment{ 1, false, 3, 3, kKwTaunt });
        m.Buff(1, 2);
        CHECK(m.attack == 7 && m.health == 6 && m.maxHealth == 6);
        Board c;
        c.Push(m);
        CHECK(c.IndexOfUid(77) == 0 && c.IndexOfUid(5) == -1 && c[0].attachments.size() == 1);
        CHECK(m.Tier() == 2 && m.IsTribe(kTribeBeast) && !m.IsTribe(kTribeMech));
    }

    // -------------------------------------------------------------------------------------------- registry and auras

    void TestRegistry()
    {
        EffectRegistry r;
        EffectDef d;
        CHECK(r.Register("BG19_010", d));
        CHECK(!r.Register("BG19_010", d));        // duplicate
        CHECK(!r.Register("BG19_010_G", d));      // golden id
        CHECK(!r.Register("NOPE", d));            // unknown
        CHECK(r.Errors().size() == 3 && r.Size() == 1);
        CHECK(r.Find(FindCard("BG19_010")) != nullptr && r.Find(FindCard("BGS_019")) == nullptr);
        CHECK(r.Find(-1) == nullptr && r.Find(100000) == nullptr);
        // the batch macro put the test card into the global registry; no batch has an error
        EffectRegistry const& g = EffectRegistry::Global();
        CHECK(g.Errors().empty());
        EffectDef const* z = g.Find(FindCard("BGS_036"));
        CHECK(z != nullptr && z->startOfCombat == &RazorgoreStart);
        // an all-null entry has no hooks
        EffectDef const* e = r.Find(FindCard("BG19_010"));
        CHECK(e && !e->deathrattle && !e->battlecry && !e->aura && e->avengeCount == 0);
    }

    class FakeShop : public ShopApi
    {
    public:
        Board& PlayerBoard(int) override { return m_board; }
        Hand& PlayerHand(int) override { return m_hand; }
        int Gold(int) const override { return m_gold; }
        void AddGold(int, int amount) override { m_gold += amount; g_fakeShopGold += amount; }
        int TavernTier(int) const override { return 1; }
        Rng& Random() override { return m_rng; }
        Board m_board;
        Hand m_hand;
        int m_gold = 3;
        Rng m_rng;
    };

    void TestShopHookShape()
    {
        FakeShop shop;
        EffectDef const* e = Samples().Find(FindCard("BG_CFM_315"));
        CHECK(e && e->battlecry);
        ShopCtx ctx(shop, 0, false);
        Minion cat = MakeMinion(FindCard("BG_CFM_315"));
        ctx.self = &cat;
        e->battlecry(ctx, false);
        ShopCtx gctx(shop, 0, true);
        e->battlecry(gctx, true);
        CHECK(shop.Gold(0) == 6 && g_fakeShopGold == 3);
    }

    void TestAuraRecompute()
    {
        EffectRegistry const& reg = Samples();
        Board b;
        b.Push(C("BG_EX1_507"));                  // Murloc Warleader 5/4: other Murlocs +2 attack
        b.Push(C("BG21_008"));                    // Saltscale Honcho 5/3 Murloc
        b.Push(C("BG19_010"));                    // Sewer Rat 3/2 Beast
        RecomputeAuras(b, reg);
        CHECK(b[0].attack == 5 && b[1].attack == 7 && b[2].attack == 3);
        RecomputeAuras(b, reg);                   // idempotent
        CHECK(b[1].attack == 7 && b[1].auraAttack == 2);
        b.Remove(0);
        RecomputeAuras(b, reg);
        CHECK(b[0].attack == 5 && b[0].auraAttack == 0);
        // Health aura: undamaged minion loses the health, a damaged one keeps its current health if it fits.
        Board h;
        h.Push(C("BG20_205"));
        h.Push(C("BG19_010", 3, 3));
        h.Push(C("BG19_010", 3, 3));
        RecomputeAuras(h, reg);
        CHECK(h[1].health == 5 && h[1].maxHealth == 5);
        h[2].health = 5;
        h[2].health -= 4;                         // damaged: 1 / 5
        h.Remove(0);
        RecomputeAuras(h, reg);
        CHECK(h[0].health == 3 && h[0].maxHealth == 3);
        CHECK(h[1].health == 1 && h[1].maxHealth == 3);
        // keyword aura bit is not part of the own keywords
        CHECK(!h[0].Has(kKwTaunt));
    }

    // ----------------------------------------------------------------------------------------------- combat: basics

    void TestFirstSide()
    {
        for (uint32_t seed = 1; seed <= 10; ++seed)
        {
            CombatResult a = SimulateCombat(Side({ V(1, 3), V(1, 3) }), Side({ V(1, 3) }), seed, Plain());
            CHECK(a.firstSide == 0 && Of(a, EventType::Attack)[0].side == 0);
            CombatResult b = SimulateCombat(Side({ V(1, 3) }), Side({ V(1, 3), V(1, 3) }), seed, Plain());
            CHECK(b.firstSide == 1 && Of(b, EventType::Attack)[0].side == 1);
        }
        int seen[2] = { 0, 0 };
        for (uint32_t seed = 1; seed <= 60; ++seed)
            ++seen[SimulateCombat(Side({ V(1, 3) }), Side({ V(1, 3) }), seed, Plain()).firstSide];
        CHECK(seen[0] > 10 && seen[1] > 10);
    }

    void TestSimpleTrade()
    {
        CombatResult r = SimulateCombat(Side({ V(3, 3) }, 2), Side({ V(2, 2) }, 1), 5, Plain());
        CHECK(r.winner == 0 && r.survivors[0].Size() == 1 && r.survivors[1].Empty());
        CHECK(r.survivors[0][0].health == 1);               // took 2
        CHECK(r.damage == 2 + 1);                           // tavern tier 2 + a tier 1 survivor
        CheckLog(r);
        // simultaneous damage: both die = draw, no damage
        CombatResult d = SimulateCombat(Side({ V(2, 2) }, 3), Side({ V(2, 2) }, 3), 5, Plain());
        CHECK(d.winner == -1 && d.damage == 0 && d.survivors[0].Empty() && d.survivors[1].Empty());
        CHECK(Of(d, EventType::Death).size() == 2);
        CheckLog(d);
    }

    void TestEmptyBoards()
    {
        CombatResult both = SimulateCombat(Side({}), Side({}), 1, Plain());
        CHECK(both.winner == -1 && both.damage == 0 && both.events.empty());
        CombatResult one = SimulateCombat(Side({}, 4), Side({ V(1, 1, 0, 3) }, 2), 1, Plain());
        CHECK(one.winner == 1 && one.damage == 2 + 3 && one.attacks == 0);
        CombatResult two = SimulateCombat(Side({ V(1, 1), V(1, 1) }, 5), Side({}, 1), 1, Plain());
        CHECK(two.winner == 0 && two.damage == 5 + 2);
    }

    void TestAttackOrder()
    {
        // B never attacks (0 attack); A's three minions attack left to right and start again from the left.
        CombatResult r = SimulateCombat(Side({ V(1, 100), V(1, 100), V(1, 100) }), Side({ V(0, 100) }), 3, Plain());
        std::vector<uint32_t> const a = Attackers(r, 0);
        CHECK(a.size() > 7);
        uint32_t const expect[] = { 1, 2, 3, 1, 2, 3, 1 };
        for (int i = 0; i < 7; ++i)
            CHECK(a[static_cast<std::size_t>(i)] == expect[i]);
        CHECK(Attackers(r, 1).empty());                     // 0 attack is skipped
        CHECK(r.winner == 0);
        CheckLog(r);
        // Alternation between the sides.
        CombatResult alt = SimulateCombat(Side({ V(1, 100) }), Side({ V(1, 100) }), 9, Plain());
        std::vector<CombatEvent> const at = Of(alt, EventType::Attack);
        for (std::size_t i = 0; i < 10; ++i)
            CHECK(at[i].side == static_cast<int>((static_cast<std::size_t>(alt.firstSide) + i) % 2));
        // A zero attack minion between two others is skipped.
        CombatResult skip = SimulateCombat(Side({ V(1, 100), V(0, 100), V(1, 100) }), Side({ V(0, 50) }), 3, Plain());
        std::vector<uint32_t> const s = Attackers(skip, 0);
        CHECK(s[0] == 1 && s[1] == 3 && s[2] == 1 && s[3] == 3);
    }

    void TestStall()
    {
        CombatResult r = SimulateCombat(Side({ V(0, 5) }, 4), Side({ V(0, 5), V(0, 5) }, 4), 1, Plain());
        CHECK(r.stalled && !r.timedOut && r.winner == -1 && r.damage == 0 && r.attacks == 0);
        CheckLog(r);
    }

    void TestTaunt()
    {
        CombatResult r = SimulateCombat(Side({ V(1, 1000), V(1, 1000), V(1, 1000), V(1, 1000) }),
                                        Side({ V(1, 100), V(1, 100, kKwTaunt), V(1, 100) }), 11, Plain());
        CHECK(r.firstSide == 0);
        std::vector<CombatEvent> const at = Of(r, EventType::Attack);
        int checked = 0;
        for (CombatEvent const& e : at)
            if (e.side == 0 && checked < 60)
            {
                CHECK(e.target == 9);                       // the Taunt minion (side 1, slot 1)
                ++checked;
            }
        CHECK(checked == 60);
        // Without Taunt every target is hit at random.
        CombatResult rnd = SimulateCombat(Side({ V(1, 1000) }), Side({ V(0, 1000), V(0, 1000), V(0, 1000) }), 4, Plain());
        std::set<uint32_t> targets;
        int n = 0;
        for (CombatEvent const& e : rnd.events)
            if (e.type == EventType::Attack && n++ < 100)
                targets.insert(e.target);
        CHECK(targets == std::set<uint32_t>({ 8, 9, 10 }));
        // two Taunts: both are hit, a non-Taunt never
        CombatResult two = SimulateCombat(Side({ V(1, 1000) }), Side({ V(0, 1000, kKwTaunt), V(0, 1000), V(0, 1000, kKwTaunt) }), 4, Plain());
        std::set<uint32_t> hit;
        n = 0;
        for (CombatEvent const& e : two.events)
            if (e.type == EventType::Attack && n++ < 100)
                hit.insert(e.target);
        CHECK(hit == std::set<uint32_t>({ 8, 10 }));
    }

    void TestDivineShield()
    {
        for (uint32_t seed = 1; seed <= 6; ++seed)
        {
            CombatResult r = SimulateCombat(Side({ V(5, 5, kKwDivineShield) }), Side({ V(3, 3) }), seed, Plain());
            CHECK(r.winner == 0 && r.survivors[0][0].health == 5);       // the hit was absorbed completely
            CHECK(!r.survivors[0][0].Has(kKwDivineShield));
            CHECK(Of(r, EventType::ShieldPop).size() == 1);
            CheckLog(r);
        }
        // a huge hit is absorbed too; the second one kills
        CombatResult big = SimulateCombat(Side({ V(1, 1, kKwDivineShield) }), Side({ V(100, 100) }), 2, Plain());
        CHECK(Of(big, EventType::ShieldPop).size() == 1 && big.winner == 1);
        // 0 damage does not pop the shield
        CombatResult zero = SimulateCombat(Side({ V(1, 20, kKwDivineShield) }), Side({ V(0, 2) }), 2, Plain());
        CHECK(Of(zero, EventType::ShieldPop).empty() && zero.winner == 0);
        // both shielded: both pop, nobody loses health, then the fight goes on
        CombatResult both = SimulateCombat(Side({ V(2, 2, kKwDivineShield) }), Side({ V(2, 2, kKwDivineShield) }), 2, Plain());
        CHECK(Of(both, EventType::ShieldPop).size() == 2 && both.winner == -1);
        CheckLog(both);
    }

    void TestPoisonous()
    {
        CombatResult r = SimulateCombat(Side({ V(1, 5, kKwPoisonous) }), Side({ V(2, 100) }), 3, Plain());
        CHECK(r.winner == 0);
        std::vector<CombatEvent> const deaths = Of(r, EventType::Death);
        CHECK(deaths.size() == 1 && deaths[0].amount == 1 && deaths[0].side == 1);   // destroyed, not worn down
        CheckLog(r);
        // a Divine Shield absorbs the poisonous hit; the poison does not carry over
        CombatResult sh = SimulateCombat(Side({ V(1, 5, kKwPoisonous) }), Side({ V(1, 100, kKwDivineShield) }), 3, Plain());
        CHECK(sh.winner == 0 && Of(sh, EventType::ShieldPop).size() == 1);
        // the hit that popped the shield did not kill; the next hit did
        int pop = -1, death = -1;
        for (std::size_t i = 0; i < sh.events.size(); ++i)
        {
            if (sh.events[i].type == EventType::ShieldPop && pop < 0)
                pop = static_cast<int>(i);
            if (sh.events[i].type == EventType::Death && death < 0)
                death = static_cast<int>(i);
        }
        CHECK(pop >= 0 && death > pop);
        CheckLog(sh);
        // a poisonous minion with 0 attack deals no damage and kills nothing
        CombatResult z = SimulateCombat(Side({ V(0, 5, kKwPoisonous) }), Side({ V(0, 5) }), 3, Plain());
        CHECK(z.stalled);
        // the poisonous minion that is hit back dies as well (draw)
        CombatResult trade = SimulateCombat(Side({ V(1, 1, kKwPoisonous) }), Side({ V(1, 100) }), 3, Plain());
        CHECK(trade.winner == -1 && Of(trade, EventType::Death).size() == 2);
    }

    void TestWindfury()
    {
        CombatResult w = SimulateCombat(Side({ V(1, 100, kKwWindfury), V(1, 100) }), Side({ V(0, 100) }), 3, Plain());
        std::vector<uint32_t> const a = Attackers(w, 0);
        uint32_t const expect[] = { 1, 1, 2, 1, 1, 2 };
        for (int i = 0; i < 6; ++i)
            CHECK(a[static_cast<std::size_t>(i)] == expect[i]);
        CombatResult m = SimulateCombat(Side({ V(1, 100, kKwMegaWindfury), V(1, 100) }), Side({ V(0, 100) }), 3, Plain());
        std::vector<uint32_t> const b = Attackers(m, 0);
        uint32_t const expectMega[] = { 1, 1, 1, 1, 2, 1, 1, 1, 1, 2 };
        for (int i = 0; i < 10; ++i)
            CHECK(b[static_cast<std::size_t>(i)] == expectMega[i]);
        // the second attack is lost when the attacker dies in the first
        CombatResult dies = SimulateCombat(Side({ V(5, 1, kKwWindfury), V(1, 1) }), Side({ V(2, 50) }), 3, Plain());
        int own = 0;
        for (uint32_t id : Attackers(dies, 0))
            own += id == 1 ? 1 : 0;
        CHECK(own == 1);
        // windfury stops when the enemy board is empty
        CombatResult empty = SimulateCombat(Side({ V(9, 9, kKwWindfury), V(1, 1) }), Side({ V(1, 1) }), 3, Plain());
        CHECK(empty.attacks == 1 && empty.winner == 0);
        CheckLog(w);
        CheckLog(m);
        CheckLog(dies);
    }

    void TestReborn()
    {
        // Bronze Warden 2/1 with Divine Shield and Reborn (tier 3)
        for (uint32_t seed = 1; seed <= 4; ++seed)
        {
            CombatResult r = SimulateCombat(Side({ C("BGS_034") }), Side({ V(10, 10) }), seed, Plain());
            std::vector<CombatEvent> const re = Of(r, EventType::Reborn);
            CHECK(re.size() == 1);
            CHECK(re[0].amount2 == 1 && re[0].amount == 2 && re[0].keywords == kKwDivineShield);   // 1 health, no Reborn
            CHECK(re[0].position == 0 && re[0].side == 0 && re[0].actor == 1 && re[0].target >= kFirstSummonId);
            CHECK(Of(r, EventType::Death).size() == 3 || Of(r, EventType::Death).size() == 2);
            CHECK(r.winner == 1);
            CheckLog(r);
        }
        // golden: golden stats, still 1 health
        CombatResult g = SimulateCombat(Side({ C("BGS_034", -1, -1, kKeep, true) }), Side({ V(10, 10) }), 1, Plain());
        std::vector<CombatEvent> const re = Of(g, EventType::Reborn);
        CHECK(re.size() == 1 && re[0].golden && re[0].amount == 4 && re[0].amount2 == 1);
        // Reborn comes back at its slot, between the neighbours
        CombatResult pos = SimulateCombat(Side({ V(1, 100, kKwTaunt), C("BG_ULD_217", 1, 1, kKwReborn | kKwTaunt), V(1, 100, kKwTaunt) }),
                                          Side({ V(3, 100) }), 7, Plain());
        std::vector<CombatEvent> const rp = Of(pos, EventType::Reborn);
        CHECK(rp.size() == 1 && rp[0].position == 1);
        CheckLog(pos);
    }

    void TestMinionCopiesNotShared()
    {
        CombatSide a = Side({ V(2, 2), V(3, 3) }), b = Side({ V(5, 5) });
        CombatSide a0 = a, b0 = b;
        CombatResult r = SimulateCombat(a, b, 12, Plain());
        CHECK(SameBoard(a.board, a0.board) && SameBoard(b.board, b0.board));
        CHECK(r.start[0][0].uid == 1 && r.start[0][1].uid == 2 && r.start[1][0].uid == 8);
    }

    void TestDamageFormula()
    {
        Board b;
        b.Push(V(1, 1, 0, 2));
        b.Push(V(1, 1, 0, 3));
        CHECK(CombatDamage(4, b, false) == 4 + 2 + 3);
        Board big;
        for (int i = 0; i < 7; ++i)
            big.Push(V(1, 1, 0, 6));
        CHECK(CombatDamage(6, big, false) == 48 && CombatDamage(6, big, true) == kDamageCap && kDamageCap == 15);
        CHECK(CombatDamage(1, b, true) == 6);
        // full fight
        CombatSide strong = Side({ V(10, 10, 0, 6), V(10, 10, 0, 6), V(10, 10, 0, 6), V(10, 10, 0, 6),
                                   V(10, 10, 0, 6), V(10, 10, 0, 6), V(10, 10, 0, 6) }, 6);
        CombatSide weak = Side({ V(1, 1) });
        CombatResult capped = SimulateCombat(strong, weak, 2, Plain(true));
        CombatResult free = SimulateCombat(strong, weak, 2, Plain(false));
        CHECK(capped.winner == 0 && capped.damage == 15);
        CHECK(free.winner == 0 && free.damage == 6 + 42);
        // a token counts as tier 1
        CombatResult tok = SimulateCombat(Side({ C("BG19_010t", 5, 5) }, 2), Side({ V(1, 1) }), 2, Plain());
        CHECK(tok.damage == 2 + 1);
        // survivors decide: the loser's survivors do not count
        CombatResult mixed = SimulateCombat(Side({ V(9, 9, 0, 4), V(9, 9, 0, 5) }, 3), Side({ V(1, 1, 0, 6) }, 6), 2, Plain());
        CHECK(mixed.winner == 0 && mixed.damage == 3 + 4 + 5);
    }

    void TestDeterminism()
    {
        CombatSide a = Side({ V(2, 5, kKwTaunt), V(3, 3, kKwDivineShield), V(1, 8, kKwWindfury), V(4, 2) });
        CombatSide b = Side({ V(2, 4), V(3, 5, kKwPoisonous), V(5, 3), V(1, 9, kKwTaunt) });
        CombatResult x = SimulateCombat(a, b, 99, Plain());
        CombatResult y = SimulateCombat(a, b, 99, Plain());
        CHECK(x.events == y.events && x.winner == y.winner && x.damage == y.damage && x.attacks == y.attacks);
        // a shared Rng continues its stream
        Rng rng(99);
        CombatResult z = SimulateCombat(a, b, rng, Plain());
        CHECK(z.events == x.events);
        // different seeds produce different fights
        std::set<std::size_t> lengths;
        for (uint32_t s = 1; s <= 30; ++s)
            lengths.insert(SimulateCombat(a, b, s, Plain()).events.size());
        CHECK(lengths.size() > 1);
        CheckLog(x);
    }

    // -------------------------------------------------------------------------------------------- combat: hooks

    void TestStartOfCombatHook()
    {
        CombatResult r = SimulateCombat(Side({ C("BGS_019") }), Side({ V(1, 3), C("BGS_019", -1, -1, kKeep, true), V(1, 3) }), 1,
                                        WithSamples());
        // B has more minions: it goes first. Its Whelp (id 9, golden +2) triggers before ours (id 1, +1).
        CHECK(r.firstSide == 1);
        std::vector<uint32_t> order;
        for (CombatEvent const& e : r.events)
            if (e.type == EventType::Trigger && e.amount == static_cast<int>(HookKind::StartOfCombat))
                order.push_back(e.actor);
        CHECK(order == std::vector<uint32_t>({ 9, 1 }));
        std::vector<CombatEvent> const buffs = Of(r, EventType::Buff);
        CHECK(buffs.size() >= 2 && buffs[0].target == 9 && buffs[0].amount == 2 && buffs[1].target == 1 && buffs[1].amount == 1);
        // the trigger is logged right before its effect
        for (std::size_t i = 0; i + 1 < r.events.size(); ++i)
            if (r.events[i].type == EventType::Trigger)
            {
                CHECK(r.events[i + 1].type == EventType::Buff || r.events[i + 1].type == EventType::Trigger);
                break;
            }
        CHECK(r.start[0][0].attack == 2);                    // the copy before any effect (Red Whelp 2/2)
        CheckLog(r);
        // equal boards: our side first when the coin says so, otherwise the other
        std::set<uint32_t> firsts;
        for (uint32_t s = 1; s <= 30; ++s)
        {
            CombatResult q = SimulateCombat(Side({ C("BGS_019") }), Side({ C("BGS_019") }), s, WithSamples());
            for (CombatEvent const& e : q.events)
                if (e.type == EventType::Trigger)
                {
                    firsts.insert(e.actor);
                    break;
                }
        }
        CHECK(firsts == std::set<uint32_t>({ 1, 8 }));
        // the default registry (Global) carries the batch-registered entry
        CombatResult g = SimulateCombat(Side({ C("BGS_036", 4, 50) }), Side({ V(0, 3) }), 1);
        CHECK(g.survivors[0][0].attack == 5);
    }

    void TestAttackHooks()
    {
        CombatResult r = SimulateCombat(Side({ C("BGS_021") }), Side({ V(0, 100) }), 1, WithSamples());   // Mama Bear 3/3
        CHECK(r.winner == 0 && r.survivors[0][0].attack == 3 + r.attacks && r.survivors[0][0].health == 3 + r.attacks);
        // the order: Attack, Trigger(Attack), Buff
        CHECK(r.events[0].type == EventType::Attack && r.events[1].type == EventType::Trigger &&
              r.events[1].amount == static_cast<int>(HookKind::Attack) && r.events[2].type == EventType::Buff);
        CheckLog(r);
        // afterAttack: Leapfrogger (3/3) sees the damage dealt and records a persistent change
        CombatResult l = SimulateCombat(Side({ V(1, 5), C("BG21_000") }), Side({ V(0, 10) }), 1, WithSamples());
        CHECK(l.winner == 0);
        int leapAttacks = 0;
        for (uint32_t id : Attackers(l, 0))
            leapAttacks += id == 2 ? 1 : 0;
        CHECK(l.survivors[0][1].counters[1] == 3 * leapAttacks && leapAttacks > 0);
        CHECK(l.persistent.size() == static_cast<std::size_t>(leapAttacks));
        CHECK(l.persistent[0].side == 0 && l.persistent[0].inputIndex == 1 && l.persistent[0].kind == 7 && l.persistent[0].value == 3);
        CheckLog(l);
        // chooseTarget: Zapp always hits the lowest attack enemy and ignores Taunt
        CombatResult z = SimulateCombat(Side({ C("BGS_022", 1, 10000) }), Side({ V(9, 100, kKwTaunt), V(5, 100), V(2, 100) }), 3, WithSamples());
        int n = 0;
        for (CombatEvent const& e : z.events)
            if (e.type == EventType::Attack && e.side == 0 && n++ < 12)
                CHECK(e.target == 10);
        CHECK(n >= 12);
    }

    void TestDamagedAndShieldHooks()
    {
        // Wrath Weaver (1/3 -> 1/50): counts the damage it takes, nobody else's
        CombatResult w = SimulateCombat(Side({ C("BGS_004", 1, 50), V(1, 100) }), Side({ V(2, 6) }), 3, WithSamples());
        int hits = 0, taken = 0;
        for (CombatEvent const& e : w.events)
            if (e.type == EventType::Damage && e.target == 1)
            {
                ++hits;
                taken += e.amount;
            }
        CHECK(hits > 0 && w.survivors[0].Size() >= 1 && w.survivors[0][0].uid == 1);
        CHECK(w.survivors[0][0].counters[1] == hits && w.survivors[0][0].counters[2] == taken);
        CHECK(Triggers(w, 1, HookKind::Damaged) >= hits);   // broadcast: also fires for the other minion's damage
        // Pupbots: every friendly shield loss reaches both
        CombatResult p = SimulateCombat(Side({ C("BG21_022", 2, 200), C("BG21_022", 2, 200) }), Side({ V(1, 100) }), 3, WithSamples());
        CHECK(p.winner == 0 && p.survivors[0].Size() == 2);
        CHECK(p.survivors[0][0].counters[1] == 2 && p.survivors[0][1].counters[1] == 2);
        CheckLog(p);
    }

    void TestFriendDiedAndAvenge()
    {
        // Hyena: +2/+1 for every friendly Beast that dies (the Sewer Rat and its Half-Shell)
        CombatResult h = SimulateCombat(Side({ C("BG_EX1_531", 2, 1000), C("BG19_010", 1, 1, kKwTaunt) }), Side({ V(1, 100) }), 3, WithSamples());
        CHECK(h.winner == 0 && h.survivors[0][0].uid == 1);
        CHECK(h.survivors[0][0].attack == 2 + 4);
        // a non-Beast friend does not count
        CombatResult nb = SimulateCombat(Side({ C("BG_EX1_531", 2, 1000), V(1, 1, kKwTaunt) }), Side({ V(1, 100) }), 3, WithSamples());
        CHECK(nb.survivors[0][0].attack == 2);
        // Avenge (2): four friends die, two triggers
        CombatResult a = SimulateCombat(Side({ C("BG21_023", 5, 1000), V(1, 1, kKwTaunt), V(1, 1, kKwTaunt), V(1, 1, kKwTaunt), V(1, 1, kKwTaunt) }),
                                        Side({ V(1, 100) }), 3, WithSamples());
        CHECK(Triggers(a, 1, HookKind::Avenge) == 2 && a.survivors[0][0].attack == 5 + 4);
        // golden Tank: Avenge (1) -> four triggers, +4 each
        CombatResult g = SimulateCombat(Side({ C("BG21_023", 5, 1000, kKeep, true), V(1, 1, kKwTaunt), V(1, 1, kKwTaunt), V(1, 1, kKwTaunt), V(1, 1, kKwTaunt) }),
                                        Side({ V(1, 100) }), 3, WithSamples());
        CHECK(Triggers(g, 1, HookKind::Avenge) == 4 && g.survivors[0][0].attack == 5 + 16);
        // the count restarts every combat: the same input board gives the same result again
        CombatResult a2 = SimulateCombat(Side({ C("BG21_023", 5, 1000), V(1, 1, kKwTaunt), V(1, 1, kKwTaunt) }), Side({ V(1, 100) }), 3, WithSamples());
        CHECK(Triggers(a2, 1, HookKind::Avenge) == 1);
        // Avenge needs the minion alive: when it dies together with the friend, nothing fires
        CombatResult late = SimulateCombat(Side({ C("BG21_023", 5, 1, kKwTaunt, true) }), Side({ V(9, 100) }), 3, WithSamples());
        CHECK(Triggers(late, 1, HookKind::Avenge) == 0);
    }

    void TestAuraInCombat()
    {
        // The Warleader dies; the Honcho's aura bonus goes with it.
        CombatResult r = SimulateCombat(Side({ C("BG_EX1_507", 3, 1, kKwTaunt), C("BG21_008", 3, 100) }), Side({ V(1, 100) }), 3, WithSamples());
        CHECK(r.start[0][1].attack == 5);
        CHECK(r.survivors[0].Size() == 1 && r.survivors[0][0].uid == 2 && r.survivors[0][0].attack == 3);
    }

    void TestDeathrattleSummon()
    {
        for (uint32_t seed = 1; seed <= 4; ++seed)
        {
            CombatResult r = SimulateCombat(Side({ C("BG19_010") }), Side({ V(5, 50) }), seed, WithSamples());
            std::vector<CombatEvent> const s = Of(r, EventType::Summon);
            CHECK(s.size() == 1 && s[0].card == FindCard("BG19_010t") && s[0].position == 0 && !s[0].golden);
            CHECK(s[0].actor == 1 && s[0].target == kFirstSummonId && s[0].amount == 2 && s[0].amount2 == 3);
            CHECK(s[0].keywords == kKwTaunt && Triggers(r, 1, HookKind::Deathrattle) == 1);
            CheckLog(r);
        }
        CombatResult g = SimulateCombat(Side({ C("BG19_010", -1, -1, kKeep, true) }), Side({ V(50, 50) }), 1, WithSamples());
        std::vector<CombatEvent> const s = Of(g, EventType::Summon);
        CHECK(s.size() == 1 && s[0].golden && s[0].amount == 4 && s[0].amount2 == 6);
        // the Deathrattle comes after the Death event
        std::size_t death = 0, summon = 0;
        for (std::size_t i = 0; i < g.events.size(); ++i)
        {
            if (g.events[i].type == EventType::Death && death == 0)
                death = i + 1;
            if (g.events[i].type == EventType::Summon && summon == 0)
                summon = i + 1;
        }
        CHECK(death > 0 && summon > death);
    }

    void TestSummonPositionAndCap()
    {
        // Sewer Rat in the middle (Taunt, 1 health): its Half-Shell takes slot 1.
        CombatResult r = SimulateCombat(Side({ V(1, 100), C("BG19_010", 1, 1, kKwTaunt), V(1, 100) }), Side({ V(1, 100) }), 3, WithSamples());
        std::vector<CombatEvent> const d = Of(r, EventType::Death);
        std::vector<CombatEvent> const s = Of(r, EventType::Summon);
        CHECK(d[0].position == 1 && s[0].position == 1);
        // Rat Pack summons three rats; on a board of seven only one fits.
        CombatResult cap = SimulateCombat(Side({ V(1, 100), V(1, 100), V(1, 100), C("BG_CFM_316", 1, 1, kKwTaunt), V(1, 100), V(1, 100), V(1, 100) }),
                                          Side({ V(1, 100) }), 3, WithSamples());
        CHECK(cap.firstSide == 0);
        CHECK(Of(cap, EventType::Summon).size() == 1);
        CheckLog(cap);
        // a board with room takes all three
        CombatResult room = SimulateCombat(Side({ C("BG_CFM_316", 1, 1, kKwTaunt), V(1, 100) }), Side({ V(1, 100) }), 3, WithSamples());
        CHECK(Of(room, EventType::Summon).size() == 3);
        std::vector<CombatEvent> const rs = Of(room, EventType::Summon);
        CHECK(rs[0].position == 0 && rs[1].position == 1 && rs[2].position == 2);   // in order, each right of the last
        CheckLog(room);
    }

    void TestSummonedMinionAttackOrder()
    {
        // A summoner that already attacked: its summon waits for the next cycle (the pointer goes on to Z).
        CombatResult a = SimulateCombat(Side({ V(1, 100, kKwTaunt), C("BG19_010", 5, 1, kKeep, true), V(1, 100, kKwTaunt) }),
                                        Side({ V(1, 1000) }), 3, WithSamples());
        std::vector<uint32_t> const at = Attackers(a, 0);
        uint32_t const expect[] = { 1, 2, 3, 1, kFirstSummonId, 3 };
        CHECK(at.size() >= 6);
        for (std::size_t i = 0; i < 6 && i < at.size(); ++i)
            CHECK(at[i] == expect[i]);
        // A minion that dies BEFORE its turn is replaced by its summon, which then takes the turn.
        CombatResult b = SimulateCombat(Side({ V(1, 100), C("BG19_010", 1, 1, kKwTaunt, true), V(1, 100) }),
                                        Side({ V(1, 1000) }), 3, WithSamples());
        std::vector<uint32_t> const bt = Attackers(b, 0);
        uint32_t const expectB[] = { 1, kFirstSummonId, 3, 1 };
        CHECK(bt.size() >= 4);
        for (std::size_t i = 0; i < 4 && i < bt.size(); ++i)
            CHECK(bt[i] == expectB[i]);
        CheckLog(a);
        CheckLog(b);
    }

    void TestDeathrattleOrder()
    {
        // Both sides' minions die in one attack: the attacking side's Deathrattle resolves first.
        CombatResult r = SimulateCombat(Side({ C("BG19_010", 5, 1, kKwTaunt) }), Side({ C("BG19_010", 5, 1, kKwTaunt) }), 3, WithSamples());
        std::vector<CombatEvent> const s = Of(r, EventType::Summon);
        std::vector<CombatEvent> const at = Of(r, EventType::Attack);
        CHECK(s.size() >= 2 && s[0].side == at[0].side && s[1].side == 1 - at[0].side);
        CHECK(Of(r, EventType::Death).size() >= 2 && Of(r, EventType::Death)[0].side == at[0].side);
        // One side, two dead at once: Kaboom Bot's Deathrattle damages every enemy; the two Sewer Rats with 1 health
        // die together and resolve left to right, the second one's slot moved right by the first one's summon.
        CombatResult k = SimulateCombat(Side({ C("BG_BOT_606", 1, 1, kKwTaunt) }),
                                        Side({ V(1, 100), C("BG19_010", 1, 1), C("BG19_010", 1, 1) }), 3, WithSamples());
        CHECK(k.firstSide == 1);
        std::vector<CombatEvent> const deaths = Of(k, EventType::Death);
        std::vector<CombatEvent> const sums = Of(k, EventType::Summon);
        CHECK(deaths.size() >= 3 && deaths[0].side == 0);            // the Kaboom Bot
        CHECK(deaths[1].side == 1 && deaths[1].position == 1 && deaths[2].side == 1 && deaths[2].position == 2);
        CHECK(sums.size() >= 2 && sums[0].position == 1 && sums[1].position == 2 && sums[0].side == 1 && sums[1].side == 1);
        CheckLog(k);
    }

    void TestSummonHooks()
    {
        // Alleycat counts the summon next to it (the Rat's Half-Shell)
        CombatResult r = SimulateCombat(Side({ C("BG_CFM_315", 1, 1000), C("BG19_010", 1, 1, kKwTaunt) }), Side({ V(1, 100) }), 3, WithSamples());
        CHECK(r.survivors[0][0].uid == 1 && r.survivors[0][0].counters[1] == 1);
        // a Reborn return is a summon for the hooks as well
        CombatResult b = SimulateCombat(Side({ C("BG_CFM_315", 1, 1000), C("BG_ULD_217", 1, 1, kKwReborn | kKwTaunt) }), Side({ V(1, 100) }), 3, WithSamples());
        CHECK(b.survivors[0][0].counters[1] == 1);
    }

    // --------------------------------------------------------------------------------------- random keyword boards

    void TestRandomKeywordBoards()
    {
        std::vector<int> cards;
        CardQuery q;
        q.slice15AOnly = true;
        q.tokens = true;
        cards = QueryCards(q);
        CHECK(cards.size() == 188);   // 167 playable pool minions + the 21 minion tokens (spells are not summoned)
        Rng pick(2026);
        int ended = 0, draws = 0, stalls = 0, longest = 0;
        for (uint32_t seed = 1; seed <= 3000; ++seed)
        {
            CombatSide s[2];
            for (int k = 0; k < 2; ++k)
            {
                s[k].tavernTier = pick.Range(1, 6);
                int const n = pick.Range(0, 7);
                for (int i = 0; i < n; ++i)
                {
                    int const card = cards[pick.Uniform(static_cast<uint32_t>(cards.size()))];
                    bool const golden = CardAt(card).goldenId[0] != '\0' && pick.Uniform(4) == 0;
                    s[k].board.Push(MakeMinion(card, golden, pick.Next()));
                }
            }
            CombatSide const in0 = s[0], in1 = s[1];
            bool const cap = pick.Coin();
            CombatResult r = SimulateCombat(s[0], s[1], seed, Plain(cap));
            CHECK(SameBoard(s[0].board, in0.board) && SameBoard(s[1].board, in1.board));
            CHECK(!r.timedOut);
            CheckLog(r);
            if (r.winner >= 0)
            {
                ++ended;
                CHECK(r.damage == CombatDamage(s[r.winner].tavernTier, r.survivors[r.winner], cap));
                CHECK(r.damage >= 1 && (!cap || r.damage <= kDamageCap));
            }
            else
                ++draws;
            stalls += r.stalled ? 1 : 0;
            longest = std::max(longest, r.attacks);
            if (seed % 10 == 0)
            {
                CombatResult again = SimulateCombat(s[0], s[1], seed, Plain(cap));
                CHECK(again.events == r.events && again.winner == r.winner && again.damage == r.damage);
            }
        }
        CHECK(ended > 1500 && draws > 0);
        CHECK(longest < kMaxTurns);
        std::printf("    random boards: %d decided, %d draws (%d stalled), longest fight %d attacks\n", ended, draws, stalls, longest);
    }

    // Stealth. A Stealthed defender is not attacked while a visible defender lives; an entirely
    // Stealthed board can still be attacked (no stall).
    void TestStealthTargeting()
    {
        {
            CombatSide a = Side({ V(0, 50, kKwStealth), V(1, 1) }, 1);   // side 0 slot 0 stealthed (id 1), slot 1 plain (id 2)
            CombatSide b = Side({ V(5, 5), V(5, 5), V(5, 5) }, 1);        // more minions: b attacks first
            CombatResult r = SimulateCombat(a, b, 12345, Plain());
            bool firstIsPlain = false;
            for (CombatEvent const& e : r.events)
                if (e.type == EventType::Attack)
                {
                    firstIsPlain = e.target == 2;
                    break;
                }
            CHECK(firstIsPlain);
            CHECK(r.winner == 1);
        }
        {
            CombatSide a = Side({ V(0, 50, kKwStealth) }, 1);            // only a stealthed wall
            CombatSide b = Side({ V(5, 5), V(5, 5) }, 1);
            CombatResult r = SimulateCombat(a, b, 777, Plain());
            CHECK(!r.stalled);
            CHECK(r.winner == 1);
        }
    }
}

int main(int, char**)
{
    RUN(TestCardTable);
    RUN(TestRng);
    RUN(TestSlots);
    RUN(TestRegistry);
    RUN(TestShopHookShape);
    RUN(TestAuraRecompute);
    RUN(TestFirstSide);
    RUN(TestSimpleTrade);
    RUN(TestEmptyBoards);
    RUN(TestAttackOrder);
    RUN(TestStall);
    RUN(TestTaunt);
    RUN(TestDivineShield);
    RUN(TestPoisonous);
    RUN(TestWindfury);
    RUN(TestReborn);
    RUN(TestMinionCopiesNotShared);
    RUN(TestDamageFormula);
    RUN(TestDeterminism);
    RUN(TestStealthTargeting);
    RUN(TestSecretAttackTriggers);
    RUN(TestSecretDeathTriggers);
    RUN(TestStartOfCombatHook);
    RUN(TestAttackHooks);
    RUN(TestDamagedAndShieldHooks);
    RUN(TestFriendDiedAndAvenge);
    RUN(TestAuraInCombat);
    RUN(TestDeathrattleSummon);
    RUN(TestSummonPositionAndCap);
    RUN(TestSummonedMinionAttackOrder);
    RUN(TestDeathrattleOrder);
    RUN(TestSummonHooks);
    RUN(TestRandomKeywordBoards);
    std::printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
