// Standalone unit tests of the engine core (src/match/HsMatch.cpp): sequencing, deaths,
// deathrattles, auras, silence, freeze, stealth, secrets, combo, overload, Choose One, picks, cost
// auras, control changes, delayed destruction and the redacted copy. Uses every card of the dataset
// (tests/match_all_cards.h) and the seed scripts of each mechanic.
//   match_core_tests.exe   run all tests; exit code 0 = all checks passed.

#include "../src/match/HsMatch.h"
#include "../src/match/HsMatchInternal.h"
#include "match_all_cards.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Hs::Match;

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
    using TA = TestAccess;

    CardDb const& Db() { return MatchAllCards::Db(); }

    // A match in its play phase, side 0 active on turn 1 with 10 mana; both hands empty, both decks
    // 20 Murloc Raiders, boards empty.
    std::unique_ptr<Match> Fresh(char const* c0 = "MAGE", char const* c1 = "WARRIOR", uint32_t seed = 11)
    {
        MatchSetup s;
        s.seed = seed;
        s.firstSide = 0;
        s.sides[0].name = "Jonas";
        s.sides[0].heroClass = c0;
        s.sides[0].deck = std::vector<std::string>(20, "CS2_168");
        s.sides[1].name = "Innkeeper";
        s.sides[1].heroClass = c1;
        s.sides[1].deck = std::vector<std::string>(20, "CS2_168");
        std::string error;
        std::unique_ptr<Match> m = Match::Create(Db(), s, error);
        if (!m)
        {
            std::printf("  create failed: %s\n", error.c_str());
            return m;
        }
        Action mull;
        mull.type = ActionType::Mulligan;
        m->Apply(0, mull);
        m->Apply(1, mull);
        TA::SetHand(*m, 0, {});
        TA::SetHand(*m, 1, {});
        TA::SetMana(*m, 0, 10, 10);
        TA::SetMana(*m, 1, 10, 10);
        return m;
    }

    Action Play(EntityId source, EntityId target = 0, int choice = 0, int position = -1)
    {
        Action a;
        a.type = ActionType::Play;
        a.source = source;
        a.target = target;
        a.choice = choice;
        a.position = position;
        return a;
    }

    Action Attack(EntityId source, EntityId target)
    {
        Action a;
        a.type = ActionType::Attack;
        a.source = source;
        a.target = target;
        return a;
    }

    Action End()
    {
        Action a;
        a.type = ActionType::EndTurn;
        return a;
    }

    Action Choose(EntityId id)
    {
        Action a;
        a.type = ActionType::Choose;
        a.source = id;
        return a;
    }

    MinionView const* FindMinion(SideView const& s, EntityId id)
    {
        for (MinionView const& m : s.board)
            if (m.id == id)
                return &m;
        return nullptr;
    }

    // The minion as side 0 sees it (id 0 when it is not on a board).
    MinionView MV(Match const& m, EntityId id)
    {
        MatchView const v = m.ViewFor(0, m.LastEventSeq());
        if (MinionView const* p = FindMinion(v.self, id))
            return *p;
        if (MinionView const* o = FindMinion(v.other, id))
            return *o;
        return MinionView();
    }

    bool OnBoard(Match const& m, int side, EntityId id)
    {
        std::vector<EntityId> const b = TA::BoardIds(m, side);
        return std::find(b.begin(), b.end(), id) != b.end();
    }

    int CountEvents(Match const& m, EventType type, uint32_t since = 0)
    {
        int n = 0;
        for (Event const& e : m.ViewFor(0, since).events)
            if (e.type == type)
                ++n;
        return n;
    }

    // Ends side 0's turn and side 1's turn (back to side 0, both refilled to 10 mana).
    void Cycle(Match& m)
    {
        m.Apply(m.ActiveSide(), End());
        m.Apply(m.ActiveSide(), End());
        TA::SetMana(m, 0, 10, 10);
        TA::SetMana(m, 1, 10, 10);
    }

    // ---- tests --------------------------------------------------------------

    void TestSupport()
    {
        for (char const* id : {"EX1_556", "EX1_097", "EX1_287", "EX1_154", "EX1_154a", "EX1_165", "EX1_165t1", "DS1_184",
                 "CS2_222", "EX1_010", "EX1_243", "CS2_013", "GAME_005", "CS2_034", "tt_010a"})
            CHECK(Db().Supported(id));
        CHECK(!Db().Supported("EX1_161o")); // an enchantment: never a playable card
        CHECK(!Db().Supported("nope"));
        Detail::Keywords k;
        CHECK(Detail::ParseKeywordText("<b>Windfury</b>. <b>Overload:</b> (2)", k) && k.windfury && k.overload == 2);
        CHECK(Detail::ParseKeywordText("<b>Stealth</b>", k) && k.stealth);
        CHECK(Detail::ParseKeywordText("Can't Attack.", k) && k.cantAttack);
        CHECK(!Detail::ParseKeywordText("<b>Battlecry:</b> Draw a card.", k));
    }

    void TestDeathrattleSlotAndChain()
    {
        std::unique_ptr<Match> m = Fresh();
        std::vector<EntityId> b = TA::SetBoard(*m, 0, {"CS2_168", "EX1_556", "CS2_168"});
        std::vector<EntityId> h = TA::SetHand(*m, 0, {"CS2_029"});
        CHECK(m->Apply(0, Play(h[0], b[1])) == ActionError::None);
        std::vector<std::string> const cards = TA::BoardCardIds(*m, 0);
        CHECK(cards.size() == 3 && cards[1] == "skele21");
        CHECK(CountEvents(*m, EventType::Deathrattle) == 1);

        // Two Abominations: one dies, its deathrattle kills the other (2 health left), whose deathrattle
        // hits everything again.
        std::unique_ptr<Match> n = Fresh();
        std::vector<EntityId> a = TA::SetBoard(*n, 0, {"EX1_097", "EX1_097"});
        TA::SetMinion(*n, a[1], 4, 2, 4);
        std::vector<EntityId> e = TA::SetBoard(*n, 1, {"CS2_200"}); // Boulderfist 6/7
        std::vector<EntityId> f = TA::SetHand(*n, 0, {"CS2_029"});
        uint32_t const before = n->LastEventSeq();
        CHECK(n->Apply(0, Play(f[0], a[0])) == ActionError::None);
        CHECK(TA::BoardIds(*n, 0).empty());
        CHECK(CountEvents(*n, EventType::Deathrattle, before) == 2);
        CHECK(n->HeroHealth(0) == 26 && n->HeroHealth(1) == 26);
        MinionView ogre = MV(*n, e[0]);
        CHECK(ogre.id && ogre.health == 3);
    }

    // The amount of a Summon event is the 1-based board position the minion entered; the amount of a Death event
    // is the 1-based position it had on its board before the death step removed anything (0 = unknown).
    int AmountOf(Match const& m, EventType type, EntityId target, uint32_t since)
    {
        for (Event const& e : m.ViewFor(0, since).events)
            if (e.type == type && e.target == target)
                return e.amount;
        return -1; // no such event
    }

    // Amounts of every event of a type, in event order.
    std::vector<int> AmountsOf(Match const& m, EventType type, uint32_t since)
    {
        std::vector<int> r;
        for (Event const& e : m.ViewFor(0, since).events)
            if (e.type == type)
                r.push_back(e.amount);
        return r;
    }

    void TestEventSlotSummon()
    {
        // A minion played at a chosen position reports that position (1-based).
        std::unique_ptr<Match> m = Fresh();
        TA::SetBoard(*m, 0, {"CS2_168", "CS2_168", "CS2_168"});
        std::vector<EntityId> h = TA::SetHand(*m, 0, {"CS2_168", "CS2_168", "CS2_168", "CS2_168"});
        uint32_t before = m->LastEventSeq();
        CHECK(m->Apply(0, Play(h[0], 0, 0, 0)) == ActionError::None); // leftmost
        CHECK(AmountOf(*m, EventType::Summon, h[0], before) == 1);
        before = m->LastEventSeq();
        CHECK(m->Apply(0, Play(h[1], 0, 0, 2)) == ActionError::None); // between the 2nd and 3rd
        CHECK(AmountOf(*m, EventType::Summon, h[1], before) == 3);
        before = m->LastEventSeq();
        CHECK(m->Apply(0, Play(h[2])) == ActionError::None); // rightmost (no position given): the 6th
        CHECK(AmountOf(*m, EventType::Summon, h[2], before) == 6);
        // The board is now six minions; a seventh fills it, then a full board refuses without any event.
        before = m->LastEventSeq();
        CHECK(m->Apply(0, Play(h[3], 0, 0, 5)) == ActionError::None);
        CHECK(AmountOf(*m, EventType::Summon, h[3], before) == 6);
        CHECK(TA::BoardIds(*m, 0).size() == 7);
        std::vector<EntityId> extra = TA::SetHand(*m, 0, {"CS2_168"});
        before = m->LastEventSeq();
        CHECK(m->Apply(0, Play(extra[0])) != ActionError::None);
        CHECK(CountEvents(*m, EventType::Summon, before) == 0 && CountEvents(*m, EventType::Death, before) == 0);

        // The seventh minion of a full board reports 7.
        std::unique_ptr<Match> f = Fresh();
        TA::SetBoard(*f, 0, std::vector<std::string>(6, "CS2_168"));
        std::vector<EntityId> g = TA::SetHand(*f, 0, {"CS2_168"});
        before = f->LastEventSeq();
        CHECK(f->Apply(0, Play(g[0])) == ActionError::None);
        CHECK(AmountOf(*f, EventType::Summon, g[0], before) == 7);

        // Mirror Entity: the copy is appended to the secret owner's board (after its one minion: position 2).
        std::unique_ptr<Match> k = Fresh();
        TA::SetSecrets(*k, 1, {"EX1_294"});
        TA::SetBoard(*k, 1, {"CS2_168"});
        std::vector<EntityId> p = TA::SetHand(*k, 0, {"CS2_168"});
        before = k->LastEventSeq();
        CHECK(k->Apply(0, Play(p[0])) == ActionError::None);
        std::vector<int> const sums = AmountsOf(*k, EventType::Summon, before);
        CHECK(sums.size() == 2 && sums[0] == 1 && sums[1] == 2);
        CHECK(TA::BoardIds(*k, 1).size() == 2);
    }

    void TestEventSlotDeathrattleSummon()
    {
        // Harvest Golem dies in the middle: the Damaged Golem takes its place and reports it.
        std::unique_ptr<Match> m = Fresh();
        std::vector<EntityId> b = TA::SetBoard(*m, 0, {"CS2_168", "CS2_168", "EX1_556", "CS2_168"});
        std::vector<EntityId> h = TA::SetHand(*m, 0, {"CS2_029"});
        uint32_t const before = m->LastEventSeq();
        CHECK(m->Apply(0, Play(h[0], b[2])) == ActionError::None);
        CHECK(AmountOf(*m, EventType::Death, b[2], before) == 3);
        std::vector<int> const sums = AmountsOf(*m, EventType::Summon, before);
        CHECK(sums.size() == 1 && sums[0] == 3);
        std::vector<std::string> const cards = TA::BoardCardIds(*m, 0);
        CHECK(cards.size() == 4 && cards[2] == "skele21");

        // On a board of seven the golem's death makes room; its token lands at the same place (the 7th).
        std::unique_ptr<Match> f = Fresh();
        std::vector<std::string> ids(7, "CS2_168");
        ids[6] = "EX1_556";
        std::vector<EntityId> fb = TA::SetBoard(*f, 0, ids);
        std::vector<EntityId> fh = TA::SetHand(*f, 0, {"CS2_029"});
        uint32_t const fbefore = f->LastEventSeq();
        CHECK(f->Apply(0, Play(fh[0], fb[6])) == ActionError::None);
        CHECK(AmountOf(*f, EventType::Death, fb[6], fbefore) == 7);
        std::vector<int> const fs = AmountsOf(*f, EventType::Summon, fbefore);
        CHECK(fs.size() == 1 && fs[0] == 7 && TA::BoardIds(*f, 0).size() == 7);
    }

    void TestEventSlotDeaths()
    {
        // Several minions die in one death step: every Death reports the position it had before the step removed
        // anything. Flamestrike (4 damage) kills the 1-health Murloc Raiders around the Boulderfist Ogres.
        std::unique_ptr<Match> m = Fresh();
        std::vector<EntityId> e = TA::SetBoard(*m, 1, {"CS2_168", "CS2_200", "CS2_168", "CS2_168", "CS2_200"});
        std::vector<EntityId> h = TA::SetHand(*m, 0, {"CS2_032"});
        uint32_t before = m->LastEventSeq();
        CHECK(m->Apply(0, Play(h[0])) == ActionError::None);
        CHECK(AmountOf(*m, EventType::Death, e[0], before) == 1);
        CHECK(AmountOf(*m, EventType::Death, e[2], before) == 3);
        CHECK(AmountOf(*m, EventType::Death, e[3], before) == 4);
        CHECK(AmountOf(*m, EventType::Death, e[1], before) == -1 && AmountOf(*m, EventType::Death, e[4], before) == -1);
        CHECK(CountEvents(*m, EventType::Death, before) == 3);

        // Both sides, and a second death step after the Abomination's deathrattle: its positions are those of the
        // shrunken boards.
        std::unique_ptr<Match> n = Fresh();
        std::vector<EntityId> a = TA::SetBoard(*n, 0, {"CS2_168", "EX1_097", "CS2_168"});
        std::vector<EntityId> o = TA::SetBoard(*n, 1, {"CS2_168", "CS2_200", "CS2_168"});
        std::vector<EntityId> f = TA::SetHand(*n, 0, {"CS2_029"});
        before = n->LastEventSeq();
        CHECK(n->Apply(0, Play(f[0], a[1])) == ActionError::None);
        CHECK(AmountOf(*n, EventType::Death, a[1], before) == 2); // first step: just the Abomination
        CHECK(AmountOf(*n, EventType::Death, a[0], before) == 1); // second step: [R, R] on side 0 ...
        CHECK(AmountOf(*n, EventType::Death, a[2], before) == 2);
        CHECK(AmountOf(*n, EventType::Death, o[0], before) == 1); // ... and [R, B, R] on side 1
        CHECK(AmountOf(*n, EventType::Death, o[2], before) == 3);
        CHECK(AmountOf(*n, EventType::Death, o[1], before) == -1);
        std::vector<int> const order = AmountsOf(*n, EventType::Death, before);
        CHECK(order.size() == 5 && order[0] == 2 && order[1] == 1 && order[2] == 2 && order[3] == 1 && order[4] == 3);

        // A death on a full board: the rightmost of seven.
        std::unique_ptr<Match> q = Fresh();
        std::vector<std::string> ids(7, "CS2_200");
        ids[6] = "CS2_168";
        std::vector<EntityId> qb = TA::SetBoard(*q, 1, ids);
        std::vector<EntityId> qh = TA::SetHand(*q, 0, {"CS2_029"});
        before = q->LastEventSeq();
        CHECK(q->Apply(0, Play(qh[0], qb[6])) == ActionError::None);
        CHECK(AmountOf(*q, EventType::Death, qb[6], before) == 7);

        // Lord Jaraxxus enters at a chosen position and leaves play (a Death event) from that same position.
        std::unique_ptr<Match> j = Fresh("WARLOCK", "WARRIOR");
        TA::SetBoard(*j, 0, {"CS2_168", "CS2_168"});
        std::vector<EntityId> jh = TA::SetHand(*j, 0, {"EX1_323"});
        before = j->LastEventSeq();
        CHECK(j->Apply(0, Play(jh[0], 0, 0, 1)) == ActionError::None);
        CHECK(AmountOf(*j, EventType::Summon, jh[0], before) == 2);
        CHECK(AmountOf(*j, EventType::Death, jh[0], before) == 2);
    }

    void TestAuraHealthNeverKills()
    {
        std::unique_ptr<Match> m = Fresh();
        std::vector<EntityId> b = TA::SetBoard(*m, 0, {"CS2_168", "CS2_222"}); // Murloc Raider 2/1 + Champion
        MinionView raider = MV(*m, b[0]);
        CHECK(raider.id && raider.attack == 3 && raider.health == 2 && raider.maxHealth == 2);
        std::vector<EntityId> h = TA::SetHand(*m, 0, {"CS2_025"}); // Arcane Explosion hits enemies only
        (void)h;
        // Damage the raider by 1 (2/2 -> 1 health left), then kill the champion.
        TA::SetMinion(*m, b[0], 2, 0, 1); // permanent 2/1 with 1 damage: health 0 + aura 1 = 1
        raider = MV(*m, b[0]);
        CHECK(raider.id && raider.health == 1);
        std::vector<EntityId> f = TA::SetHand(*m, 0, {"CS2_029"});
        CHECK(m->Apply(0, Play(f[0], b[1])) == ActionError::None);
        raider = MV(*m, b[0]);
        CHECK(raider.id && raider.health == 1 && raider.maxHealth == 1 && raider.attack == 2);
    }

    void TestSilenceAndEnrage()
    {
        std::unique_ptr<Match> m = Fresh();
        std::vector<EntityId> e = TA::SetBoard(*m, 1, {"EX1_393"}); // Amani Berserker 2/3
        TA::SetMinion(*m, e[0], 2, 2, 3);
        MinionView amani = MV(*m, e[0]);
        CHECK(amani.id && amani.attack == 5 && amani.enraged);
        std::vector<EntityId> o = TA::SetHand(*m, 0, {"CS2_203"});
        CHECK(m->Apply(0, Play(o[0], e[0])) == ActionError::None);
        amani = MV(*m, e[0]);
        CHECK(amani.id && amani.attack == 2 && amani.silenced && !amani.enraged && amani.health == 2);

        // Silence removes buffs; the damage stays (it can kill).
        std::unique_ptr<Match> n = Fresh();
        std::vector<EntityId> s = TA::SetBoard(*n, 1, {"CS2_168"});
        TA::SetMinion(*n, s[0], 4, 1, 3); // a 2/1 buffed to 4/3 with 2 damage
        std::vector<EntityId> owl = TA::SetHand(*n, 0, {"CS2_203"});
        CHECK(n->Apply(0, Play(owl[0], s[0])) == ActionError::None);
        CHECK(!OnBoard(*n, 1, s[0]));

        // Ironbeak Owl without a target is fine when no minion exists.
        std::unique_ptr<Match> k = Fresh();
        std::vector<EntityId> owl2 = TA::SetHand(*k, 0, {"CS2_203"});
        CHECK(k->Apply(0, Play(owl2[0], 0)) == ActionError::None);
    }

    void TestFreezeTiming()
    {
        std::unique_ptr<Match> m = Fresh();
        std::vector<EntityId> e = TA::SetBoard(*m, 1, {"CS2_200"});
        std::vector<EntityId> h = TA::SetHand(*m, 0, {"CS2_024"});
        CHECK(m->Apply(0, Play(h[0], e[0])) == ActionError::None);
        CHECK(MV(*m, e[0]).frozen);
        m->Apply(0, End());
        // Side 1's turn: the ogre is frozen and cannot attack.
        CHECK(m->Apply(1, Attack(e[0], TA::HeroId(*m, 0))) == ActionError::Attack);
        m->Apply(1, End());
        CHECK(!MV(*m, e[0]).frozen); // thawed at the end of its controller's turn

        // Attacking a Water Elemental freezes the attacker after it attacked: frozen through its next turn.
        std::unique_ptr<Match> n = Fresh();
        std::vector<EntityId> mine = TA::SetBoard(*n, 0, {"CS2_200"});
        std::vector<EntityId> we = TA::SetBoard(*n, 1, {"CS2_033"});
        CHECK(n->Apply(0, Attack(mine[0], we[0])) == ActionError::None);
        CHECK(MV(*n, mine[0]).frozen);
        n->Apply(0, End());
        n->Apply(1, End());
        TA::ReadyAll(*n);
        CHECK(MV(*n, mine[0]).frozen);
        CHECK(n->Apply(0, Attack(mine[0], TA::HeroId(*n, 1))) == ActionError::Attack);
        n->Apply(0, End());
        CHECK(!MV(*n, mine[0]).frozen);
    }

    void TestSecrets()
    {
        // Counterspell: only on the opponent's turn; hidden from the opponent; countered spells cost mana.
        std::unique_ptr<Match> m = Fresh();
        std::vector<EntityId> sec = TA::SetHand(*m, 0, {"EX1_287", "EX1_287"});
        CHECK(m->Apply(0, Play(sec[0])) == ActionError::None);
        CHECK(m->Apply(0, Play(sec[1])) == ActionError::Secret); // the same secret twice
        MatchView const other = m->ViewFor(1, 0);
        CHECK(other.other.secrets.size() == 1 && other.other.secrets[0].cardId.empty());
        for (Event const& e : other.events)
            if (e.type == EventType::Secret || (e.type == EventType::Play && e.source == sec[0]))
                CHECK(e.cardId.empty());
        CHECK(m->ViewFor(0, 0).self.secrets.size() == 1 && m->ViewFor(0, 0).self.secrets[0].cardId == "EX1_287");
        m->Apply(0, End());
        TA::SetMana(*m, 1, 10, 10);
        std::vector<EntityId> fb = TA::SetHand(*m, 1, {"CS2_029"});
        CHECK(m->Apply(1, Play(fb[0], TA::HeroId(*m, 0))) == ActionError::None);
        CHECK(m->HeroHealth(0) == 30);
        CHECK(TA::SecretCardIds(*m, 0).empty());
        CHECK(m->ViewFor(1, 0).self.hero.mana == 6);
        CHECK(CountEvents(*m, EventType::Counter) == 1 && CountEvents(*m, EventType::Reveal) == 1);

        // Explosive Trap kills the attacking minion before it hits.
        std::unique_ptr<Match> n = Fresh();
        TA::SetSecrets(*n, 1, {"EX1_610"});
        std::vector<EntityId> a = TA::SetBoard(*n, 0, {"CS2_172"}); // Raptor 3/2
        CHECK(n->Apply(0, Attack(a[0], TA::HeroId(*n, 1))) == ActionError::None);
        CHECK(n->HeroHealth(1) == 30 && n->HeroHealth(0) == 28 && TA::BoardIds(*n, 0).empty());

        // Noble Sacrifice: the Defender becomes the target.
        std::unique_ptr<Match> k = Fresh();
        TA::SetSecrets(*k, 1, {"EX1_130"});
        std::vector<EntityId> r = TA::SetBoard(*k, 0, {"CS2_200"});
        CHECK(k->Apply(0, Attack(r[0], TA::HeroId(*k, 1))) == ActionError::None);
        CHECK(k->HeroHealth(1) == 30 && TA::BoardIds(*k, 1).empty());
        CHECK(MV(*k, r[0]).health == 5);

        // Ice Block prevents fatal damage and makes the hero immune for the turn.
        std::unique_ptr<Match> i = Fresh();
        TA::SetSecrets(*i, 1, {"EX1_295"});
        TA::SetHero(*i, 1, 5, 0);
        std::vector<EntityId> f = TA::SetHand(*i, 0, {"CS2_029", "CS2_029"});
        CHECK(i->Apply(0, Play(f[0], TA::HeroId(*i, 1))) == ActionError::None);
        CHECK(i->HeroHealth(1) == 5 && i->GetPhase() == Phase::Play);
        CHECK(i->Apply(0, Play(f[1], TA::HeroId(*i, 1))) == ActionError::Target); // immune
        i->Apply(0, End());
        CHECK(!i->ViewFor(0, i->LastEventSeq()).other.hero.immune);
    }

    void TestCombo()
    {
        std::unique_ptr<Match> m = Fresh("ROGUE");
        std::vector<EntityId> h = TA::SetHand(*m, 0, {"EX1_124", "GAME_005", "EX1_124"});
        CHECK(m->Apply(0, Play(h[0], TA::HeroId(*m, 1))) == ActionError::None);
        CHECK(m->HeroHealth(1) == 28); // the first card of the turn: no combo, 2 damage
        CHECK(m->Apply(0, Play(h[2], TA::HeroId(*m, 1))) == ActionError::None);
        CHECK(m->HeroHealth(1) == 24); // combo: 4 damage

        // SI:7 Agent only targets with combo.
        std::unique_ptr<Match> n = Fresh("ROGUE");
        std::vector<EntityId> s = TA::SetHand(*n, 0, {"EX1_134", "EX1_134"});
        CHECK(n->Apply(0, Play(s[0], TA::HeroId(*n, 1))) == ActionError::Target);
        CHECK(n->Apply(0, Play(s[0], 0)) == ActionError::None);
        bool targeted = false;
        for (Action const& a : n->LegalActions(0))
            if (a.source == s[1] && a.target == TA::HeroId(*n, 1))
                targeted = true;
        CHECK(targeted);
        CHECK(n->Apply(0, Play(s[1], TA::HeroId(*n, 1))) == ActionError::None);
        CHECK(n->HeroHealth(1) == 28);
    }

    void TestOverload()
    {
        std::unique_ptr<Match> m = Fresh("SHAMAN");
        std::vector<EntityId> h = TA::SetHand(*m, 0, {"EX1_238", "EX1_243"});
        CHECK(m->Apply(0, Play(h[0], TA::HeroId(*m, 1))) == ActionError::None);
        CHECK(m->Apply(0, Play(h[1])) == ActionError::None); // Dust Devil: keyword-only with Overload (2)
        MatchView v = m->ViewFor(0, 0);
        CHECK(v.self.overloadOwed == 3 && v.self.overloadLocked == 0);
        TA::SetMana(*m, 0, 0, 5);
        m->Apply(0, End());
        m->Apply(1, End());
        v = m->ViewFor(0, 0);
        CHECK(v.self.hero.maxMana == 6 && v.self.hero.mana == 3 && v.self.overloadLocked == 3 && v.self.overloadOwed == 0);
    }

    void TestChooseOne()
    {
        std::unique_ptr<Match> m = Fresh("DRUID");
        std::vector<EntityId> e = TA::SetBoard(*m, 1, {"CS2_200"});
        std::vector<EntityId> h = TA::SetHand(*m, 0, {"EX1_154", "EX1_154", "EX1_165", "EX1_165"});
        CHECK(m->Apply(0, Play(h[0], e[0], 0)) == ActionError::Choice);
        CHECK(m->Apply(0, Play(h[0], e[0], 3)) == ActionError::Choice);
        int c1 = 0, c2 = 0;
        for (Action const& a : m->LegalActions(0))
            if (a.source == h[0])
            {
                c1 += a.choice == 1;
                c2 += a.choice == 2;
            }
        CHECK(c1 == 1 && c2 == 1);
        MatchView const v = m->ViewFor(0, 0);
        CHECK(v.self.hand[0].options.size() == 2 && v.self.hand[0].options[1] == "EX1_154b");
        CHECK(m->Apply(0, Play(h[0], e[0], 1)) == ActionError::None);
        CHECK(MV(*m, e[0]).health == 4);
        int const hand = static_cast<int>(TA::HandIds(*m, 0).size());
        CHECK(m->Apply(0, Play(h[1], e[0], 2)) == ActionError::None);
        CHECK(MV(*m, e[0]).health == 3 && static_cast<int>(TA::HandIds(*m, 0).size()) == hand);
        TA::SetMana(*m, 0, 10, 10);
        CHECK(m->Apply(0, Play(h[2], 0, 1)) == ActionError::None);
        CHECK(m->Apply(0, Play(h[3], 0, 2)) == ActionError::None);
        std::vector<std::string> const b = TA::BoardCardIds(*m, 0);
        CHECK(b.size() == 2 && b[0] == "EX1_165t1" && b[1] == "EX1_165t2");
        MinionView cat = MV(*m, TA::BoardIds(*m, 0)[0]);
        CHECK(cat.id && cat.charge && cat.canAttack);
    }

    void TestPick()
    {
        std::unique_ptr<Match> m = Fresh("HUNTER");
        TA::SetDeck(*m, 0, {"CS2_029", "CS2_024", "CS2_172", "CS2_168"});
        std::vector<EntityId> h = TA::SetHand(*m, 0, {"DS1_184"});
        CHECK(m->Apply(0, Play(h[0])) == ActionError::None);
        MatchView v = m->ViewFor(0, 0);
        CHECK(v.pickPending && v.pick.size() == 3 && v.legal.empty());
        CHECK(m->ViewFor(1, 0).opponentPicking && m->ViewFor(1, 0).pick.empty());
        CHECK(m->Apply(0, End()) == ActionError::Pending);
        CHECK(m->Apply(0, Choose(12345)) == ActionError::Source);
        EntityId pickId = 0;
        for (PickView const& p : v.pick)
            if (p.cardId == "CS2_024")
                pickId = p.id;
        CHECK(m->Apply(0, Choose(pickId)) == ActionError::None);
        std::vector<std::string> hand = TA::HandCardIds(*m, 0);
        CHECK(hand.size() == 1 && hand[0] == "CS2_024");
        CHECK(TA::DeckCardIds(*m, 0).size() == 1);
        CHECK(m->Apply(1, Choose(pickId)) == ActionError::Pending);
        for (Event const& e : m->ViewFor(1, 0).events)
            if (e.type == EventType::Pick || (e.type == EventType::Discard && e.amount == 1))
                CHECK(e.cardId.empty());
    }

    void TestCostAura()
    {
        std::unique_ptr<Match> m = Fresh();
        std::vector<EntityId> h = TA::SetHand(*m, 0, {"CS2_029", "CS2_172", "GAME_005"});
        TA::SetBoard(*m, 0, {"EX1_608"});
        CHECK(TA::HandCost(*m, h[0]) == 3 && TA::HandCost(*m, h[1]) == 2 && TA::HandCost(*m, h[2]) == 0);
        MatchView const v = m->ViewFor(0, 0);
        CHECK(v.self.hand[0].cost == 3);
        TA::SetMana(*m, 0, 3, 3);
        CHECK(m->Apply(0, Play(h[0], TA::HeroId(*m, 1))) == ActionError::None);
        CHECK(m->ViewFor(0, 0).self.hero.mana == 0);
    }

    void TestTriggers()
    {
        // Knife Juggler: after you summon a minion, 1 damage to a random enemy.
        std::unique_ptr<Match> m = Fresh();
        TA::SetBoard(*m, 0, {"NEW1_019"});
        std::vector<EntityId> h = TA::SetHand(*m, 0, {"CS2_168"});
        CHECK(m->Apply(0, Play(h[0])) == ActionError::None);
        CHECK(m->HeroHealth(1) == 29);

        // Whirlwind with Cult Master and Acolyte of Pain: the acolyte draws once, Cult Master draws for each
        // other friendly minion that died (it survives).
        std::unique_ptr<Match> n = Fresh("WARRIOR");
        TA::SetBoard(*n, 0, {"EX1_595", "EX1_007", "CS2_168", "CS2_168"});
        std::vector<EntityId> w = TA::SetHand(*n, 0, {"EX1_400"});
        CHECK(n->Apply(0, Play(w[0])) == ActionError::None);
        CHECK(TA::HandIds(*n, 0).size() == 3); // 1 (acolyte) + 2 (two raiders died)
        CHECK(TA::BoardIds(*n, 0).size() == 2);
    }

    void TestControlAndCorruption()
    {
        std::unique_ptr<Match> m = Fresh("PRIEST");
        std::vector<EntityId> e = TA::SetBoard(*m, 1, {"CS2_200"});
        std::vector<EntityId> h = TA::SetHand(*m, 0, {"CS1_113"});
        CHECK(m->Apply(0, Play(h[0], e[0])) == ActionError::None);
        CHECK(OnBoard(*m, 0, e[0]) && !OnBoard(*m, 1, e[0]));
        CHECK(!MV(*m, e[0]).canAttack);

        std::unique_ptr<Match> n = Fresh("WARLOCK");
        std::vector<EntityId> f = TA::SetBoard(*n, 1, {"CS2_200"});
        std::vector<EntityId> c = TA::SetHand(*n, 0, {"CS2_063"});
        CHECK(n->Apply(0, Play(c[0], f[0])) == ActionError::None);
        n->Apply(0, End());
        CHECK(OnBoard(*n, 1, f[0]));
        n->Apply(1, End());
        CHECK(!OnBoard(*n, 1, f[0]));
    }

    void TestStealth()
    {
        std::unique_ptr<Match> m = Fresh("ROGUE");
        std::vector<EntityId> b = TA::SetBoard(*m, 0, {"CS2_200", "EX1_010"});
        std::vector<EntityId> h = TA::SetHand(*m, 0, {"EX1_128"});
        CHECK(m->Apply(0, Play(h[0])) == ActionError::None);
        m->Apply(0, End());
        TA::SetMana(*m, 1, 10, 10);
        std::vector<EntityId> fb = TA::SetHand(*m, 1, {"CS2_029"});
        CHECK(m->Apply(1, Play(fb[0], b[0])) == ActionError::Target);
        std::vector<EntityId> e = TA::SetBoard(*m, 1, {"CS2_172"});
        TA::ReadyAll(*m);
        CHECK(m->Apply(1, Attack(e[0], b[1])) == ActionError::Target);
        m->Apply(1, End());
        // Conceal ended at the start of side 0's turn; the infiltrator keeps its own stealth until it attacks.
        CHECK(!MV(*m, b[0]).stealth && MV(*m, b[1]).stealth);
        CHECK(m->Apply(0, Attack(b[1], TA::HeroId(*m, 1))) == ActionError::None);
        CHECK(!MV(*m, b[1]).stealth);
    }

    void TestWildGrowthExcessMana()
    {
        std::unique_ptr<Match> m = Fresh("DRUID");
        std::vector<EntityId> h = TA::SetHand(*m, 0, {"CS2_013"});
        CHECK(m->Apply(0, Play(h[0])) == ActionError::None);
        std::vector<std::string> const hand = TA::HandCardIds(*m, 0);
        CHECK(hand.size() == 1 && hand[0] == "CS2_013t");
        TA::SetMana(*m, 0, 5, 5);
        std::vector<EntityId> g = TA::SetHand(*m, 0, {"CS2_013"});
        CHECK(m->Apply(0, Play(g[0])) == ActionError::None);
        CHECK(m->ViewFor(0, 0).self.hero.maxMana == 6 && m->ViewFor(0, 0).self.hero.mana == 3);
    }

    void TestRedactedCopy()
    {
        std::unique_ptr<Match> m = Fresh();
        TA::SetHand(*m, 0, {"CS2_029", "CS2_024"});
        TA::SetHand(*m, 1, {"CS2_108", "EX1_400"});
        TA::SetSecrets(*m, 1, {"EX1_287"});
        TA::SetBoard(*m, 1, {"CS2_200"});
        std::string const before = m->DebugState();
        std::unique_ptr<Match> r = m->RedactedCopy(0, 99);
        CHECK(r != nullptr);
        CHECK(m->DebugState() == before);
        std::vector<std::string> const oh = TA::HandCardIds(*r, 1);
        CHECK(oh.size() == 2 && oh[0] == "?" && oh[1] == "?");
        std::vector<std::string> const od = TA::DeckCardIds(*r, 1);
        CHECK(!od.empty() && od[0] == "?");
        CHECK(TA::SecretCardIds(*r, 1)[0] == "?");
        CHECK(TA::HandCardIds(*r, 0) == TA::HandCardIds(*m, 0));
        // The same legal actions for side 0, and they all apply on the copy.
        std::vector<Action> const la = m->LegalActions(0);
        std::vector<Action> const lb = r->LegalActions(0);
        CHECK(la.size() == lb.size());
        CHECK(r->Apply(0, Play(TA::HandIds(*r, 0)[0], TA::HeroId(*r, 1))) == ActionError::None);
        CHECK(r->HeroHealth(1) == 24); // the unknown secret never triggers (and it is side 0's own turn anyway)
        CHECK(m->HeroHealth(1) == 30);
        r->Apply(0, End());
        CHECK(r->ActiveSide() == 1 && r->LegalActions(1).size() <= 2); // unknown cards are never playable
        MatchView const rv = r->ViewFor(0, 0);
        CHECK(rv.events.size() > 0 && rv.events[0].seq > m->LastEventSeq());
    }

    void TestDeterminism()
    {
        std::string states[2];
        for (int run = 0; run < 2; ++run)
        {
            std::unique_ptr<Match> m = Fresh("MAGE", "WARRIOR", 77);
            TA::SetBoard(*m, 0, {"NEW1_019", "EX1_097", "EX1_556"});
            TA::SetBoard(*m, 1, {"EX1_007", "EX1_595", "CS2_168", "CS2_168"});
            std::vector<EntityId> h = TA::SetHand(*m, 0, {"EX1_277", "CS2_168", "CS2_025"});
            m->Apply(0, Play(h[0]));
            m->Apply(0, Play(h[1]));
            m->Apply(0, Play(h[2]));
            Cycle(*m);
            states[run] = m->DebugState();
        }
        CHECK(states[0] == states[1]);
    }
}

int main()
{
    RUN(TestSupport);
    RUN(TestDeathrattleSlotAndChain);
    RUN(TestEventSlotSummon);
    RUN(TestEventSlotDeathrattleSummon);
    RUN(TestEventSlotDeaths);
    RUN(TestAuraHealthNeverKills);
    RUN(TestSilenceAndEnrage);
    RUN(TestFreezeTiming);
    RUN(TestSecrets);
    RUN(TestCombo);
    RUN(TestOverload);
    RUN(TestChooseOne);
    RUN(TestPick);
    RUN(TestCostAura);
    RUN(TestTriggers);
    RUN(TestControlAndCorruption);
    RUN(TestStealth);
    RUN(TestWildGrowthExcessMana);
    RUN(TestRedactedCopy);
    RUN(TestDeterminism);
    std::printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
