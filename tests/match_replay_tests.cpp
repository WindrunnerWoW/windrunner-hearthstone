// Standalone tests of the durable duels (src/match/HsMatchReplay.*, the arena part of
// src/match/HsMatchService.*): a match rebuilt from its setup and the log of applied actions equals the match that was
// played without interruption, and a service that suspends and restores a duel at any point plays on exactly like
// one that was never interrupted.
//   match_replay_tests.exe [directory [games]]   run all tests; exit code 0 = all checks passed.
// Optional second argument: Hard-vs-Hard games of the replay corpus (default 60; the Normal games scale with it).
//
//  1. The text form of an action (EncodeAction / DecodeAction) round-trips and refuses everything else.
//  2. Engine level: many seeded AI-vs-AI games (Hard-Hard, Hard-Normal, Normal-Normal, decks built around Tracking,
//     secrets and Choose One, timer-style forced turn ends) are recorded action by action; the same setup rebuilt
//     with Match::Create and the log replayed through Match::Apply gives the same DebugState after EVERY action and
//     the same events and result. Suspended at targeted points (mulligan pending, pending pick, secrets in play,
//     the turn limit), the rebuilt match continues with fresh AI decisions to the same end as the original.
//  3. Service level: two MatchService copies play the same arena duel (the human is scripted by the AI, the
//     Nozdormu timer is driven by an injected clock, some turns are left idle so the timer forces them); one is
//     suspended (with a text round trip of the log) and restored at targeted points, and every payload and the final
//     state must equal the uninterrupted copy. Also: a failed store (Suspend with keepActions), refusals of Restore,
//     ordinary matches unchanged.

#include "../src/match/HsMatch.h"
#include "../src/match/HsMatchAi.h"
#include "../src/match/HsMatchInternal.h"
#include "../src/match/HsMatchReplay.h"
#include "../src/match/HsMatchService.h"
#include "../src/HsDataset.h"
#include "match_all_cards.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <random>
#include <set>
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
    CardDb const& Db() { return MatchAllCards::Db(); }

    char const* const kClasses[9] = {"MAGE", "WARRIOR", "HUNTER", "PALADIN", "PRIEST", "ROGUE", "SHAMAN", "WARLOCK", "DRUID"};

    int g_gamesArg = 0;

    int HardGames()
    {
        return g_gamesArg > 0 ? std::max(4, g_gamesArg) : 60;
    }

    // ---- decks (the same construction as tests/match_ai_tests.cpp) ---------------------------------------

    std::vector<std::string> Pool(std::string const& cls)
    {
        static std::set<std::string> tokens;
        // Steps E2a and E2b scripted the boss cards of all five wings. The decks of these tests were built before
        // that, so the pool stays what it was: the scripted boss cards are out, and the plain boss tokens that were always
        // supported (keyword-only minions) stay in although a script refers to them now.
        static std::set<std::string> const encounterOnly = {"FP1_006", "NAXM_001", "NAXM_002", "NAX1_05", "NAX2_05", "NAX3_03",
            "NAX4_05", "NAX5_03", "NAX6_03", "NAX6_04", "NAX7_04", "NAX7_05", "NAX8_03", "NAX8_04", "NAX8_05", "NAX9_02",
            "NAX9_03", "NAX9_04", "NAX9_05", "NAX9_07", "NAX11_04", "NAX12_03", "NAX12_04", "NAX13_03", "NAX14_03",
            "NAX14_04"};
        if (tokens.empty())
        {
            tokens.insert(Detail::kCoinId);
            for (MatchAllCards::Row const& r : MatchAllCards::kRows)
                if (Detail::Script const* sc = Detail::FindScript(r.id))
                    for (std::string const& ref : sc->refs)
                        tokens.insert(ref);
            for (char const* id : {"NAX1_03", "NAX4_03", "NAX7_02", "NAX11_03", "NAX15_03n"})
                tokens.erase(id);
        }
        std::vector<std::string> out;
        for (MatchAllCards::Row const& r : MatchAllCards::kRows)
        {
            if (!Db().Supported(r.id) || tokens.count(r.id) || encounterOnly.count(r.id))
                continue;
            CardDef const* d = Db().Find(r.id);
            if (!d || (d->type != CardType::Minion && d->type != CardType::Spell && d->type != CardType::Weapon))
                continue;
            if (d->cardClass != cls && d->cardClass != "NEUTRAL")
                continue;
            out.push_back(r.id);
        }
        return out;
    }

    int Bucket(int cost)
    {
        return cost <= 1 ? 0 : std::min(6, cost - 1);
    }

    // A 30 card deck with a sensible curve, deterministic for (class, seed), at most two copies of a card.
    std::vector<std::string> BuildDeck(std::string const& cls, uint32_t seed)
    {
        std::vector<std::string> pool = Pool(cls);
        std::mt19937 rng(seed * 2654435761u + 17u);
        std::shuffle(pool.begin(), pool.end(), rng);
        int const want[7] = {4, 6, 6, 5, 4, 3, 2};
        std::vector<std::string> deck;
        std::map<std::string, int> copies;
        for (int b = 0; b < 7; ++b)
        {
            int have = 0;
            for (std::string const& id : pool)
            {
                if (have >= want[b] || deck.size() >= 30)
                    break;
                if (Bucket(Db().Find(id)->cost) != b)
                    continue;
                for (int c = 0; c < 2 && have < want[b]; ++c)
                {
                    deck.push_back(id);
                    ++copies[id];
                    ++have;
                }
            }
        }
        for (std::string const& id : pool)
            while (deck.size() < 30 && copies[id] < 2)
            {
                deck.push_back(id);
                ++copies[id];
            }
        for (std::size_t i = 0; deck.size() < 30 && !pool.empty(); ++i)
            deck.push_back(pool[i % pool.size()]);
        return deck;
    }

    // A themed deck: `copies` copies of each supported card of `must` first, then the curve deck of BuildDeck.
    std::vector<std::string> ThemedDeck(std::string const& cls, uint32_t seed, std::vector<std::string> const& must, int copies)
    {
        std::vector<std::string> deck;
        std::map<std::string, int> n;
        for (std::string const& id : must)
            if (Db().Supported(id))
                for (int c = 0; c < copies; ++c)
                {
                    deck.push_back(id);
                    ++n[id];
                }
        for (std::string const& id : BuildDeck(cls, seed))
            if (deck.size() < 30 && n[id] < 2)
            {
                deck.push_back(id);
                ++n[id];
            }
        for (std::string const& id : Pool(cls))
            if (deck.size() < 30 && n[id] < 2)
            {
                deck.push_back(id);
                ++n[id];
            }
        deck.resize(std::min<std::size_t>(deck.size(), 30));
        return deck;
    }

    // Cards that make the features of the plan appear: 1 Hunter (Tracking and traps), 2 Mage secrets, 3 Paladin
    // secrets, 4 Druid Choose One.
    char const* ThemeClass(int theme)
    {
        static char const* const c[5] = {"", "HUNTER", "MAGE", "PALADIN", "DRUID"};
        return c[theme];
    }

    std::vector<std::string> ThemeCards(int theme)
    {
        switch (theme)
        {
            case 1:
                return {"DS1_184", "EX1_533", "EX1_554", "EX1_609", "EX1_610", "EX1_611"};
            case 2:
                return {"EX1_287", "EX1_289", "EX1_294", "EX1_295", "EX1_594", "tt_010"};
            case 3:
                return {"EX1_130", "EX1_132", "EX1_136", "EX1_379"};
            case 4:
                return {"EX1_154", "EX1_155", "EX1_160", "EX1_164", "EX1_165", "EX1_166", "EX1_178", "EX1_573", "NEW1_007",
                        "NEW1_008"};
        }
        return {};
    }

    // ---- 1. the text form ---------------------------------------------------------------------------------

    Hs::LoggedAction Logged(int side, ActionType type, EntityId source = 0, EntityId target = 0, int position = -1,
                            int choice = 0, std::vector<EntityId> replace = {})
    {
        Hs::LoggedAction l;
        l.side = side;
        l.action.type = type;
        l.action.source = source;
        l.action.target = target;
        l.action.position = position;
        l.action.choice = choice;
        l.action.replace = std::move(replace);
        return l;
    }

    bool RoundTrips(Hs::LoggedAction const& l)
    {
        std::string const text = Hs::EncodeAction(l);
        Hs::LoggedAction back;
        std::string error;
        return !text.empty() && Hs::DecodeAction(text, back, error) && error.empty() && Hs::SameLoggedAction(l, back) &&
               Hs::EncodeAction(back) == text;
    }

    void TestActionText()
    {
        static_assert(Hs::kActionLogVersion == 1, "the tests below spell the version out");
        CHECK(Hs::EncodeAction(Logged(0, ActionType::Play, 12, 0, -1, 0)) == "v1,0,P,12,0,-1,0");
        CHECK(Hs::EncodeAction(Logged(1, ActionType::Mulligan, 0, 0, -1, 0, {5, 7})) == "v1,1,M,0,0,-1,0,5,7");
        CHECK(Hs::EncodeAction(Logged(0, ActionType::EndTurn)) == "v1,0,E,0,0,-1,0");
        CHECK(Hs::EncodeAction(Logged(0, ActionType::Attack, 31, 4294967295u, 3)) == "v1,0,A,31,4294967295,3,0");
        CHECK(Hs::EncodeAction(Logged(0, ActionType::Play, 9, 11, 2, 2)) == "v1,0,P,9,11,2,2");
        CHECK(Hs::EncodeAction(Logged(2, ActionType::EndTurn)).empty() && Hs::EncodeAction(Logged(-1, ActionType::EndTurn)).empty());

        ActionType const types[7] = {ActionType::Mulligan, ActionType::Play, ActionType::Attack, ActionType::HeroPower,
                                     ActionType::EndTurn, ActionType::Concede, ActionType::Choose};
        std::mt19937 rng(4242);
        int roundTrips = 0;
        for (int i = 0; i < 4000; ++i)
        {
            ActionType const type = types[rng() % 7];
            std::vector<EntityId> replace;
            if (type == ActionType::Mulligan)
                for (uint32_t k = 0, n = rng() % 5; k < n; ++k)
                    replace.push_back(rng() % 3 == 0 ? 0xFFFFFFFFu : rng() % 200);
            Hs::LoggedAction const l = Logged(static_cast<int>(rng() % 2), type, rng() % 4 == 0 ? 0u : rng() % 100000,
                                              rng() % 3 == 0 ? 0u : rng() % 100000, static_cast<int>(rng() % 9) - 1,
                                              static_cast<int>(rng() % 3), replace);
            CHECK(RoundTrips(l));
            ++roundTrips;
        }
        std::printf("    %d actions round-tripped through the text form\n", roundTrips);

        // Everything else is refused, with a reason.
        struct Bad
        {
            char const* text;
            char const* why;
        };
        Bad const bad[] = {
            {"", "malformed"},
            {"v1", "malformed"},
            {"v1,0,P,12,0,-1", "malformed"},
            {"0,P,12,0,-1,0", "malformed"},
            {"v2,0,P,12,0,-1,0", "version"},
            {"v0,0,P,12,0,-1,0", "version"},
            {"V1,0,P,12,0,-1,0", "malformed"},
            {"v1,2,P,12,0,-1,0", "side"},
            {"v1,-1,P,12,0,-1,0", "side"},
            {"v1,,P,12,0,-1,0", "side"},
            {"v1,0,X,12,0,-1,0", "type"},
            {"v1,0,p,12,0,-1,0", "type"},
            {"v1,0,PP,12,0,-1,0", "type"},
            {"v1,0,,12,0,-1,0", "type"},
            {"v1,0,P,012,0,-1,0", "number"},
            {"v1,0,P,-12,0,-1,0", "number"},
            {"v1,0,P,+12,0,-1,0", "number"},
            {"v1,0,P, 12,0,-1,0", "number"},
            {"v1,0,P,12 ,0,-1,0", "number"},
            {"v1,0,P,1x2,0,-1,0", "number"},
            {"v1,0,P,12,0,-2,0", "number"},
            {"v1,0,P,12,0,16,0", "number"},
            {"v1,0,P,12,0,-1,3", "number"},
            {"v1,0,P,12,0,-1,-1", "number"},
            {"v1,0,P,4294967296,0,-1,0", "number"},
            {"v1,0,P,99999999999,0,-1,0", "number"},
            {"v1,0,P,12,0,-1,0,", "replace"},
            {"v1,0,P,12,0,-1,0,5", "replace"},
            {"v1,0,E,0,0,-1,0,1", "replace"},
            {"v1,0,M,0,0,-1,0,1,2,3,4,5,6,7,8,9,10,11", "replace"},
            {"v1,0,M,0,0,-1,0,1,,3", "number"},
            {"v1,0,M,0,0,-1,0,a", "number"},
            {"v1,0,P,12,0,-1,0\n", "number"},
        };
        for (Bad const& b : bad)
        {
            Hs::LoggedAction out;
            std::string error;
            bool const ok = Hs::DecodeAction(b.text, out, error);
            CHECK(!ok);
            CHECK(error == b.why);
            if (ok || error != b.why)
                std::printf("    '%s': ok=%d error='%s' (wanted '%s')\n", b.text, ok ? 1 : 0, error.c_str(), b.why);
        }
        // A good text leaves no error and decodes to the same text; ten replaced ids are the most.
        Hs::LoggedAction ok;
        std::string error = "stale";
        CHECK(Hs::DecodeAction("v1,1,M,0,0,-1,0,1,2,3,4,5,6,7,8,9,10", ok, error) && error.empty() && ok.action.replace.size() == 10);
    }

    // ---- 2. engine level ----------------------------------------------------------------------------------

    struct Cover
    {
        int games = 0, replays = 0, actions = 0, stateChecks = 0, continuations = 0, continuedActions = 0;
        int mulligans = 0, plays = 0, attacks = 0, powers = 0, ends = 0, concedes = 0, chooses = 0, choiceOnes = 0;
        int forced = 0, heroDied = 0, turnLimit = 0;
        int secretEvents = 0, revealEvents = 0, generateEvents = 0, pickEvents = 0, transformEvents = 0, controlEvents = 0,
            deathrattleEvents = 0, counterEvents = 0, overloadEvents = 0;
        int maxTurns = 0;
    };
    Cover g_cover;

    struct Policy
    {
        AiProfile prof[2] = {AiProfile::Hard, AiProfile::Hard};
        uint32_t aiSeed[2] = {1, 2};
        bool force = false; // side 0's turn 3 (mod 5) is ended by "the timer" after two actions
    };

    // The scripted players: the next action of whoever has to act, a pure function of the match and the counter of
    // actions this turn (the same rule as the service's aiActions). Feed applies an action and keeps the counter.
    struct Driver
    {
        Policy pol;
        int turn = -1;
        int actions = 0;

        bool Next(Match const& m, int& side, Action& a, bool& forced)
        {
            forced = false;
            if (m.GetPhase() == Phase::Over)
                return false;
            if (m.GetPhase() == Phase::Mulligan)
            {
                side = m.MulliganPending(0) ? 0 : 1;
                a = ChooseAiAction(m, side, Db(), 0, pol.prof[side], pol.aiSeed[side] + m.Seq() * 2654435761u);
                return true;
            }
            if (m.Turn() != turn)
            {
                turn = m.Turn();
                actions = 0;
            }
            side = m.ActiveSide();
            MatchView const v = m.ViewFor(side, m.LastEventSeq());
            if (!v.pickPending && m.ViewFor(1 - side, m.LastEventSeq()).pickPending)
                side = 1 - side;
            if (pol.force && side == 0 && m.Turn() % 5 == 3 && actions >= 2)
            {
                forced = true;
                a = Action();
                MatchView const own = m.ViewFor(0, m.LastEventSeq());
                if (own.pickPending && !own.pick.empty())
                {
                    a.type = ActionType::Choose;
                    a.source = own.pick[0].id;
                }
                else
                    a.type = ActionType::EndTurn;
                return true;
            }
            a = ChooseAiAction(m, side, Db(), actions, pol.prof[side], pol.aiSeed[side] + m.Seq() * 2654435761u);
            return true;
        }

        ActionError Feed(Match& m, int side, Action const& a)
        {
            bool const play = m.GetPhase() == Phase::Play;
            if (play && m.Turn() != turn)
            {
                turn = m.Turn();
                actions = 0;
            }
            ActionError const err = m.Apply(side, a);
            if (play)
                ++actions;
            return err;
        }
    };

    // Everything a viewer of either side can see of the events and the result.
    std::string EventsText(Match const& m)
    {
        std::string out;
        for (int side = 0; side < 2; ++side)
        {
            MatchView const v = m.ViewFor(side, 0);
            out += "view" + std::to_string(side) + " phase=" + std::to_string(static_cast<int>(v.phase)) +
                " over=" + std::to_string(v.result.over) + " winner=" + std::to_string(v.result.winner) + " reason=" +
                std::to_string(static_cast<int>(v.result.reason)) + "\n";
            for (Event const& e : v.events)
                out += std::to_string(e.seq) + "," + EventTypeCode(e.type) + "," + std::to_string(e.side) + "," +
                    std::to_string(e.source) + "," + std::to_string(e.target) + "," + std::to_string(e.amount) + "," +
                    e.cardId + "," + (e.hideFromOther ? "h" : "-") + ";";
            out += "\n";
        }
        return out;
    }

    struct Record
    {
        MatchSetup setup;
        Policy pol;
        std::vector<Hs::LoggedAction> log;
        std::vector<std::string> states; // [i]: DebugState after i actions
        int firstPick = -1;              // prefix length at which a pick is pending (first time)
        int firstSecret = -1;            // ... a secret is in play
        int forced = 0;
        std::string events;
        Result result;
        int turns = 0;
        std::string label;
    };

    bool SecretsInPlay(Match const& m)
    {
        MatchView const v = m.ViewFor(0, m.LastEventSeq());
        return !v.self.secrets.empty() || !v.other.secrets.empty();
    }

    bool PickPending(Match const& m)
    {
        return m.ViewFor(0, m.LastEventSeq()).pickPending || m.ViewFor(1, m.LastEventSeq()).pickPending;
    }

    void Count(Record const& r)
    {
        for (Hs::LoggedAction const& l : r.log)
        {
            ++g_cover.actions;
            switch (l.action.type)
            {
                case ActionType::Mulligan: ++g_cover.mulligans; break;
                case ActionType::Play: ++g_cover.plays; break;
                case ActionType::Attack: ++g_cover.attacks; break;
                case ActionType::HeroPower: ++g_cover.powers; break;
                case ActionType::EndTurn: ++g_cover.ends; break;
                case ActionType::Concede: ++g_cover.concedes; break;
                case ActionType::Choose: ++g_cover.chooses; break;
            }
            if (l.action.choice > 0)
                ++g_cover.choiceOnes;
        }
        g_cover.forced += r.forced;
        ++g_cover.games;
        if (r.result.reason == EndReason::HeroDied)
            ++g_cover.heroDied;
        if (r.result.reason == EndReason::TurnLimit)
            ++g_cover.turnLimit;
        g_cover.maxTurns = std::max(g_cover.maxTurns, r.turns);
    }

    void CountEvents(Match const& m)
    {
        for (Event const& e : m.ViewFor(0, 0).events)
            switch (e.type)
            {
                case EventType::Secret: ++g_cover.secretEvents; break;
                case EventType::Reveal: ++g_cover.revealEvents; break;
                case EventType::Generate: ++g_cover.generateEvents; break;
                case EventType::Pick: ++g_cover.pickEvents; break;
                case EventType::Transform: ++g_cover.transformEvents; break;
                case EventType::Control: ++g_cover.controlEvents; break;
                case EventType::Deathrattle: ++g_cover.deathrattleEvents; break;
                case EventType::Counter: ++g_cover.counterEvents; break;
                case EventType::Overload: ++g_cover.overloadEvents; break;
                default: break;
            }
    }

    // Plays a game with the scripted players and records every applied action and the state after each.
    bool PlayRecord(MatchSetup const& setup, Policy const& pol, std::string const& label, Record& r)
    {
        r = Record();
        r.setup = setup;
        r.pol = pol;
        r.label = label;
        std::string error;
        std::unique_ptr<Match> m = Match::Create(Db(), setup, error);
        CHECK(m != nullptr);
        if (!m)
        {
            std::printf("  create failed (%s): %s\n", label.c_str(), error.c_str());
            return false;
        }
        r.states.push_back(m->DebugState());
        Driver drv;
        drv.pol = pol;
        for (int guard = 0; guard < 8000; ++guard)
        {
            int side = 0;
            Action a;
            bool forced = false;
            if (!drv.Next(*m, side, a, forced))
                break;
            ActionError const err = drv.Feed(*m, side, a);
            CHECK(err == ActionError::None);
            if (err != ActionError::None)
                return false;
            Hs::LoggedAction l;
            l.side = side;
            l.action = a;
            r.log.push_back(l);
            r.forced += forced ? 1 : 0;
            r.states.push_back(m->DebugState());
            if (r.firstPick < 0 && PickPending(*m))
                r.firstPick = static_cast<int>(r.log.size());
            if (r.firstSecret < 0 && SecretsInPlay(*m))
                r.firstSecret = static_cast<int>(r.log.size());
        }
        CHECK(m->GetPhase() == Phase::Over);
        r.events = EventsText(*m);
        r.result = m->GetResult();
        r.turns = m->Turn();
        Count(r);
        CountEvents(*m);
        return true;
    }

    // The tests' central claim: Create + replayed Apply is the same match, after every single action.
    void CheckReplay(Record const& r)
    {
        std::string error;
        std::unique_ptr<Match> m = Match::Create(Db(), r.setup, error);
        CHECK(m != nullptr);
        if (!m)
            return;
        CHECK(m->DebugState() == r.states[0]);
        for (std::size_t i = 0; i < r.log.size(); ++i)
        {
            ActionError const err = m->Apply(r.log[i].side, r.log[i].action);
            CHECK(err == ActionError::None);
            bool const same = m->DebugState() == r.states[i + 1];
            CHECK(same);
            if (err != ActionError::None || !same)
            {
                std::printf("    %s: replay differs at action %d of %d\n", r.label.c_str(), static_cast<int>(i),
                            static_cast<int>(r.log.size()));
                return;
            }
            ++g_cover.stateChecks;
        }
        CHECK(m->GetPhase() == Phase::Over);
        CHECK(EventsText(*m) == r.events);
        Result const res = m->GetResult();
        CHECK(res.over == r.result.over && res.winner == r.result.winner && res.reason == r.result.reason);
        ++g_cover.replays;
    }

    // Suspend after p actions: rebuild from the prefix, then play on with FRESH decisions to the end; they must be the
    // very actions the uninterrupted match took, and the end the same.
    void CheckContinue(Record const& r, std::size_t p)
    {
        std::string error;
        std::unique_ptr<Match> m = Match::Create(Db(), r.setup, error);
        CHECK(m != nullptr);
        if (!m)
            return;
        Driver drv;
        drv.pol = r.pol;
        for (std::size_t i = 0; i < p; ++i)
        {
            ActionError const err = drv.Feed(*m, r.log[i].side, r.log[i].action);
            CHECK(err == ActionError::None);
            if (err != ActionError::None)
                return;
        }
        CHECK(m->DebugState() == r.states[p]);
        std::size_t i = p;
        for (;;)
        {
            int side = 0;
            Action a;
            bool forced = false;
            if (!drv.Next(*m, side, a, forced))
                break;
            if (i >= r.log.size())
            {
                CHECK(false); // the rebuilt match plays longer than the original
                return;
            }
            Hs::LoggedAction l;
            l.side = side;
            l.action = a;
            bool const same = Hs::SameLoggedAction(l, r.log[i]);
            CHECK(same);
            if (!same)
            {
                std::printf("    %s: after %d actions the rebuilt match chose %s, the original %s\n", r.label.c_str(),
                            static_cast<int>(p), Hs::EncodeAction(l).c_str(), Hs::EncodeAction(r.log[i]).c_str());
                return;
            }
            CHECK(drv.Feed(*m, side, a) == ActionError::None);
            ++i;
            CHECK(m->DebugState() == r.states[i]);
        }
        CHECK(i == r.log.size());
        CHECK(EventsText(*m) == r.events);
        ++g_cover.continuations;
        g_cover.continuedActions += static_cast<int>(r.log.size() - p);
    }

    struct Pairing
    {
        char const* cls0;
        char const* cls1;
        std::vector<std::string> deck0, deck1;
        int theme = 0;
    };

    Pairing MakePairing(int g)
    {
        std::mt19937 rng(static_cast<uint32_t>(g) * 31337u + 5u);
        Pairing p;
        p.theme = g % 5;
        p.cls0 = p.theme ? ThemeClass(p.theme) : kClasses[(g + static_cast<int>(rng() % 3)) % 9];
        p.cls1 = kClasses[(g * 4 + 1 + static_cast<int>(rng() % 5)) % 9];
        p.deck0 = p.theme ? ThemedDeck(p.cls0, static_cast<uint32_t>(g) * 2u + 1u, ThemeCards(p.theme), 2)
                          : BuildDeck(p.cls0, static_cast<uint32_t>(g) * 2u + 1u);
        // Every third opponent is built around the same kind of cards (secrets and picks on both sides).
        int const t1 = g % 3 == 0 ? 1 + (g / 3) % 3 : 0;
        if (t1)
            p.cls1 = ThemeClass(t1);
        p.deck1 = t1 ? ThemedDeck(p.cls1, static_cast<uint32_t>(g) * 2u + 2u, ThemeCards(t1), 2)
                     : BuildDeck(p.cls1, static_cast<uint32_t>(g) * 2u + 2u);
        return p;
    }

    MatchSetup SetupOf(Pairing const& p, uint32_t seed, int firstSide)
    {
        MatchSetup s;
        s.seed = seed;
        s.firstSide = firstSide;
        s.sides[0].name = "A";
        s.sides[0].heroClass = p.cls0;
        s.sides[0].deck = p.deck0;
        s.sides[1].name = "B";
        s.sides[1].heroClass = p.cls1;
        s.sides[1].deck = p.deck1;
        return s;
    }

    // `games` seeded games of one pairing of profiles: full replay of each, and the targeted suspend points for
    // every `contEvery`-th game (0: none).
    void RunCorpus(char const* label, AiProfile p0, AiProfile p1, int games, uint32_t base, bool force, int contEvery)
    {
        int const startChecks = g_cover.stateChecks;
        int const startContinuations = g_cover.continuations;
        int const startGames = g_cover.games;
        for (int g = 0; g < games; ++g)
        {
            Pairing const p = MakePairing(static_cast<int>(base) + g);
            uint32_t const seed = base * 1000u + static_cast<uint32_t>(g);
            Policy pol;
            pol.prof[0] = p0;
            pol.prof[1] = p1;
            pol.aiSeed[0] = seed * 7919u + 1u;
            pol.aiSeed[1] = seed * 104729u + 2u;
            pol.force = force;
            std::string const name = std::string(label) + " #" + std::to_string(g) + " " + p.cls0 + " vs " + p.cls1;
            Record r;
            if (!PlayRecord(SetupOf(p, seed, g % 3 == 0 ? -1 : g % 2), pol, name, r))
                continue;
            CheckReplay(r);
            if (contEvery > 0 && g % contEvery == 0)
            {
                std::set<std::size_t> points = {0, 1, 2, r.log.size() / 2, r.log.size() - 1};
                if (r.firstPick >= 0)
                    points.insert(static_cast<std::size_t>(r.firstPick));
                if (r.firstSecret >= 0)
                    points.insert(static_cast<std::size_t>(r.firstSecret));
                // Two points per game keep the run short; the others are rotated through the games.
                std::vector<std::size_t> const all(points.begin(), points.end());
                CheckContinue(r, all[(static_cast<std::size_t>(g) / static_cast<std::size_t>(contEvery)) % all.size()]);
                CheckContinue(r, all[(static_cast<std::size_t>(g) / static_cast<std::size_t>(contEvery) + 3) % all.size()]);
            }
        }
        std::printf("    %-22s %3d games, %d replayed states compared, %d continuations\n", label,
                    g_cover.games - startGames, g_cover.stateChecks - startChecks, g_cover.continuations - startContinuations);
    }

    void TestReplayHardHard()
    {
        RunCorpus("hard-hard", AiProfile::Hard, AiProfile::Hard, HardGames(), 501, false, 3);
    }

    void TestReplayHardHardTimerForced()
    {
        // Side 0's every fifth turn is cut short by "the timer": a pick answered with its first option, then EndTurn.
        RunCorpus("hard-hard, forced end", AiProfile::Hard, AiProfile::Hard, std::max(4, HardGames() / 2), 601, true, 2);
        CHECK(g_cover.forced > 0);
    }

    void TestReplayNormal()
    {
        RunCorpus("normal-normal", AiProfile::Normal, AiProfile::Normal, HardGames(), 701, false, 2);
        RunCorpus("hard-normal", AiProfile::Hard, AiProfile::Normal, std::max(4, HardGames() / 2), 801, true, 2);
        RunCorpus("normal-hard", AiProfile::Normal, AiProfile::Hard, std::max(4, HardGames() / 2), 901, false, 2);
    }

    // 60 Shieldbearers (0/4) for Priests (the hero power only heals): nobody can deal damage, the match runs into
    // the turn limit.
    std::vector<std::string> PassiveDeck()
    {
        std::vector<std::string> cards;
        if (Db().Supported("EX1_405"))
            cards.assign(60, "EX1_405");
        return cards;
    }

    void TestReplayTurnLimit()
    {
        std::vector<std::string> const deck = PassiveDeck();
        CHECK(deck.size() == 60);
        if (deck.empty())
            return;
        Pairing p;
        p.cls0 = "PRIEST";
        p.cls1 = "PRIEST";
        p.deck0 = deck;
        p.deck1 = deck;
        int draws = 0;
        for (uint32_t seed : {3u, 4u})
        {
            Policy pol;
            pol.prof[0] = AiProfile::Hard;
            pol.prof[1] = seed == 3u ? AiProfile::Hard : AiProfile::Normal;
            pol.aiSeed[0] = seed * 11u;
            pol.aiSeed[1] = seed * 13u;
            Record r;
            if (!PlayRecord(SetupOf(p, seed, 0), pol, "turn limit", r))
                continue;
            CHECK(r.result.over && r.result.reason == EndReason::TurnLimit && r.result.winner == -1);
            CHECK(r.turns >= kMaxTurns - 1);
            ++draws;
            CheckReplay(r);
            // Suspended at the last turn, one action before the draw, and early.
            CheckContinue(r, r.log.size() - 1);
            CheckContinue(r, r.log.size() - 3);
            CheckContinue(r, r.log.size() / 2);
        }
        CHECK(draws == 2);
        std::printf("    %d turn-limit draws replayed, longest game %d turns\n", draws, g_cover.maxTurns);
    }

    void TestReplayCoverage()
    {
        Cover const& c = g_cover;
        std::printf("    %d games, %d actions (mulligan %d, play %d, attack %d, hero power %d, end %d, concede %d, choose %d)\n",
                    c.games, c.actions, c.mulligans, c.plays, c.attacks, c.powers, c.ends, c.concedes, c.chooses);
        std::printf("    %d Choose One plays, %d timer-style forced turns, %d hero deaths, %d turn-limit draws, longest %d turns\n",
                    c.choiceOnes, c.forced, c.heroDied, c.turnLimit, c.maxTurns);
        std::printf("    events: %d secrets put into play, %d revealed, %d countered, %d generated cards, %d picks, %d "
                    "transforms, %d control changes, %d deathrattles, %d overloads\n",
                    c.secretEvents, c.revealEvents, c.counterEvents, c.generateEvents, c.pickEvents, c.transformEvents,
                    c.controlEvents, c.deathrattleEvents, c.overloadEvents);
        std::printf("    %d full replays, %d state comparisons, %d suspend points continued (%d actions decided afresh)\n",
                    c.replays, c.stateChecks, c.continuations, c.continuedActions);
        CHECK(c.replays == c.games);
        CHECK(c.chooses > 0 && c.choiceOnes > 0 && c.forced > 0);
        CHECK(c.secretEvents > 0 && c.revealEvents > 0 && c.generateEvents > 0 && c.pickEvents > 0);
        CHECK(c.turnLimit >= 2 && c.heroDied > 0);
        CHECK(c.mulligans >= 2 * c.games);
        CHECK(c.continuations >= 40);
    }

    // ---- 3. service level ---------------------------------------------------------------------------------

    constexpr uint64_t kStepMs = 250;
    constexpr uint32_t kDelayMs = 500;

    std::shared_ptr<CardDb const> SharedDb()
    {
        return std::shared_ptr<CardDb const>(&Db(), [](CardDb const*) {});
    }

    // The payload without its match id, which differs between the copies.
    std::string Norm(std::string const& payload)
    {
        if (payload.compare(0, 4, "mid=") != 0)
            return payload;
        std::size_t const semi = payload.find(';');
        return semi == std::string::npos ? std::string() : payload.substr(semi + 1);
    }

    Hs::MatchStartParams MakeParams(char const* cls0, std::vector<std::string> const& deck0, char const* cls1,
                                    std::vector<std::string> const& deck1, uint32_t seed, bool arena)
    {
        Hs::MatchStartParams p;
        p.guid = 7;
        p.humanName = "Jonas";
        p.humanClass = cls0;
        p.humanDeck = deck0;
        p.opponentName = "Arena Foe";
        p.opponentClass = cls1;
        p.opponentDeck = deck1;
        p.seed = seed;
        p.difficulty = "hard";
        p.profile = AiProfile::Hard;
        p.meta.accountId = 3;
        p.arena = arena;
        p.arenaRunId = arena ? 9000000000ull + seed : 0;
        return p;
    }

    enum Flag : unsigned
    {
        FStart = 1u << 0,   // right after Start (human mulligan pending)
        FMull = 1u << 1,    // human mulligan pending after a step
        FHPick = 1u << 2,   // the human has a pending pick
        FAPick = 1u << 3,   // the AI has a pending pick
        FSecret = 1u << 4,  // a secret is in play
        FTimer = 1u << 5,   // the Nozdormu limit applies to the human's turn and the human has acted in it
        FForced = 1u << 6,  // the step was a turn the timer forced to end
        FForcedPick = 1u << 7, // ... with a pending pick answered first
        FAiMid = 1u << 8,   // mid-way through the AI's turn (second or later action)
        FLate = 1u << 9,    // turn 88 or later
        FChoice = 1u << 10, // the human played a Choose One card
        FIdle = 1u << 11,   // an idle step of a human turn with a running deadline (not an action step)
        FHumanAct = 1u << 12 // the step was an action of the human
    };

    struct StepRec
    {
        std::string payload; // the reply / push of the step, without its match id ("" when there was none)
        std::string get;     // GAME_GET after the step
        std::string state;   // DebugState after the step (only when recorded)
        unsigned flags = 0;
        bool action = false; // an action was applied in this step (so the AI delay starts here)
        int aiTurn = -1, aiActions = 0;
    };

    // One side of the experiment: a service holding an arena (or ordinary) duel, a scripted human and a clock.
    struct Rig
    {
        Hs::MatchService svc{1000};
        Hs::MatchStartParams params;
        AiProfile humanProf = AiProfile::Normal;
        bool recordState = false;
        uint32_t guid = 0;
        uint32_t mid = 0;
        uint64_t clock = 100000;
        int hTurn = -1, hActions = 0;
        int aiLogTurn = -1, aiLogCount = 0;
        std::vector<Hs::LoggedAction> log; // everything the outcomes reported, in order
        std::vector<StepRec> steps;
        std::string startGet, startState;
        bool over = false;
        Hs::MatchEnd end;
        int stored = 0;       // log entries the "store" got
        int idleSteps = 0;
        int forcedPicks = 0;

        explicit Rig(Hs::MatchStartParams const& p, AiProfile human = AiProfile::Normal) : params(p), humanProf(human)
        {
            guid = p.guid;
            svc.SetCardDb(SharedDb());
            svc.SetAiDelayMs(kDelayMs);
        }

        Match* M() { return svc.MatchForTests(guid); }

        void Absorb(uint32_t first, std::vector<Hs::LoggedAction> const& logged)
        {
            if (!params.arena)
            {
                CHECK(logged.empty());
                return;
            }
            CHECK(first == log.size());
            log.insert(log.end(), logged.begin(), logged.end());
        }

        void Start()
        {
            Hs::MatchStartResult const s = svc.Start(params, clock);
            CHECK(s.ok);
            if (!s.ok)
            {
                std::printf("    start failed: %s\n", s.error.c_str());
                over = true;
                return;
            }
            mid = s.matchId;
            if (params.arena)
            {
                CHECK(s.logged.size() == 1 && s.logged[0].side == 1 && s.logged[0].action.type == ActionType::Mulligan);
                CHECK(s.arenaRunId == params.arenaRunId);
                Absorb(0, s.logged);
            }
            else
                CHECK(s.logged.empty());
            startGet = Norm(svc.Get(guid));
            if (recordState)
                startState = M()->DebugState();
        }

        static bool IdleTurn(int turn) { return (turn / 2) % 3 == 1; }

        // One step of the scripted client: an action of the human when it is to move, else a tick of the clock.
        bool Step()
        {
            Match* m = M();
            if (!m || over || m->GetPhase() == Phase::Over)
                return false;
            clock += kStepMs;
            StepRec rec;
            MatchView const hv = m->ViewFor(0, m->LastEventSeq());
            bool human = false;
            bool idle = false;
            Action act;
            uint32_t const seed = m->Seed() * 7919u + m->Seq();
            if (m->GetPhase() == Phase::Mulligan)
            {
                if (hv.mulliganPending)
                {
                    human = true;
                    act = ChooseAiAction(*m, 0, Db(), 0, humanProf, seed);
                }
            }
            else
            {
                if (m->Turn() != hTurn)
                {
                    hTurn = m->Turn();
                    hActions = 0;
                }
                bool const myMove = hv.pickPending || (m->ActiveSide() == 0 && !hv.opponentPicking);
                idle = myMove && m->ActiveSide() == 0 && m->TurnTimeLimitSec() > 0 && IdleTurn(m->Turn()) && hActions >= 1;
                if (myMove && !idle)
                {
                    human = true;
                    act = ChooseAiAction(*m, 0, Db(), hActions, humanProf, seed);
                    // On a turn that is going to be left idle under the limit, open with Tracking when the hand has
                    // it, so that the timer finds a pick pending.
                    if (hActions == 0 && m->ActiveSide() == 0 && m->TurnTimeLimitSec() > 0 && IdleTurn(m->Turn()))
                        for (Action const& l : hv.legal)
                            for (HandCardView const& c : hv.self.hand)
                                if (l.type == ActionType::Play && l.source == c.id && c.cardId == "DS1_184")
                                    act = l;
                }
            }
            bool const wasPlay = m->GetPhase() == Phase::Play;
            int const turnBefore = m->Turn();
            if (human)
            {
                Hs::GameActRequest req;
                req.matchId = mid;
                req.seq = m->Seq();
                req.action = act;
                Hs::GameActOutcome const o = svc.Act(guid, req, clock);
                CHECK(o.ok);
                if (!o.ok)
                {
                    std::printf("    the human's action was refused (%s)\n", o.why.c_str());
                    over = true;
                    return false;
                }
                Absorb(o.loggedFirst, o.logged);
                rec.payload = Norm(o.payload);
                rec.action = true;
                rec.flags |= FHumanAct;
                if (params.arena)
                    CHECK(o.logged.size() == 1 && o.logged[0].side == 0 && o.arenaRunId == params.arenaRunId);
                if (act.choice > 0)
                    rec.flags |= FChoice;
                if (wasPlay)
                    ++hActions;
                if (o.ended)
                {
                    over = true;
                    end = o.end;
                }
            }
            else
            {
                std::vector<Hs::MatchPush> const pushes = svc.Tick(clock);
                CHECK(pushes.size() <= 1);
                if (!pushes.empty())
                {
                    Hs::MatchPush const& p = pushes[0];
                    CHECK(p.note.empty() && p.guid == guid && p.matchId == mid);
                    Absorb(p.loggedFirst, p.logged);
                    if (params.arena)
                        CHECK(p.arenaRunId == params.arenaRunId && !p.logged.empty());
                    rec.payload = Norm(p.payload);
                    rec.action = true;
                    bool const aiMove = !p.logged.empty() && p.logged[0].side == 1;
                    if (aiMove)
                    {
                        if (turnBefore != aiLogTurn)
                        {
                            aiLogTurn = turnBefore;
                            aiLogCount = 0;
                        }
                        aiLogCount += static_cast<int>(p.logged.size());
                    }
                    else if (!p.logged.empty() && p.logged.back().action.type == ActionType::EndTurn)
                    {
                        rec.flags |= FForced;
                        if (p.logged.size() > 1 && p.logged[0].action.type == ActionType::Choose)
                        {
                            rec.flags |= FForcedPick;
                            ++forcedPicks;
                        }
                    }
                    if (p.ended)
                    {
                        over = true;
                        end = p.end;
                    }
                }
                else if (idle)
                {
                    rec.flags |= FIdle;
                    ++idleSteps;
                }
            }
            rec.get = Norm(svc.Get(guid));
            m = M();
            if (recordState)
                rec.state = m->DebugState();
            MatchView const v0 = m->ViewFor(0, m->LastEventSeq());
            if (m->MulliganPending(0))
                rec.flags |= FMull;
            if (v0.pickPending)
                rec.flags |= FHPick;
            if (m->ViewFor(1, m->LastEventSeq()).pickPending)
                rec.flags |= FAPick;
            if (!v0.self.secrets.empty() || !v0.other.secrets.empty())
                rec.flags |= FSecret;
            if (m->GetPhase() == Phase::Play)
            {
                if (m->TurnTimeLimitSec() > 0 && m->ActiveSide() == 0 && m->Turn() == hTurn && hActions >= 1)
                    rec.flags |= FTimer;
                if (m->ActiveSide() == 1 && m->Turn() == aiLogTurn && aiLogCount >= 2 && rec.action && !(rec.flags & FHumanAct))
                    rec.flags |= FAiMid;
                if (m->Turn() >= 88)
                    rec.flags |= FLate;
            }
            svc.AiCountersForTests(guid, rec.aiTurn, rec.aiActions);
            steps.push_back(rec);
            return true;
        }

        void Run(int maxSteps = 30000)
        {
            for (int i = 0; i < maxSteps && Step(); ++i)
            {
            }
        }

        // The duel goes to storage and back: the log through its text form, as a database would hold it.
        Hs::MatchRestoreResult SuspendRestore(uint64_t gapMs, std::size_t keep = static_cast<std::size_t>(-1))
        {
            Hs::SuspendedDuel duel;
            CHECK(svc.Suspend(guid, duel, clock, keep));
            CHECK(!svc.Has(guid) && !svc.IsRunning(guid) && svc.RunningCount() == 0);
            CHECK(duel.rulesVersion == kRulesVersion && duel.datasetHash == Hs::kDatasetHash);
            CHECK(duel.params.arena && duel.params.arenaRunId == params.arenaRunId && duel.params.seed == params.seed);
            std::size_t const expected = std::min(keep, log.size());
            CHECK(duel.log.size() == expected);
            for (std::size_t i = 0; i < duel.log.size() && i < log.size(); ++i)
                CHECK(Hs::SameLoggedAction(duel.log[i], log[i]));
            std::vector<Hs::LoggedAction> viaText;
            for (Hs::LoggedAction const& l : duel.log)
            {
                Hs::LoggedAction back;
                std::string error;
                CHECK(Hs::DecodeAction(Hs::EncodeAction(l), back, error));
                viaText.push_back(back);
            }
            duel.log = viaText;
            clock += gapMs;
            Hs::MatchRestoreResult const r = svc.Restore(duel, clock);
            CHECK(r.ok);
            if (r.ok)
            {
                mid = r.matchId;
                log.resize(expected);
                CHECK(svc.IsRunning(guid) || duel.over);
            }
            else
                over = true;
            return r;
        }
    };

    struct Pick
    {
        char const* name;
        unsigned flag;     // 0 with k = -1: right after Start
        bool actionStep;   // the step must have applied an action (the AI delay starts there): strict comparison
        int nth;           // the nth step with the flag
    };

    int FindStep(Rig const& a, unsigned flag, bool actionStep, int nth)
    {
        int seen = 0;
        for (std::size_t i = 0; i < a.steps.size(); ++i)
            if ((a.steps[i].flags & flag) != 0 && (!actionStep || a.steps[i].action) && ++seen == nth)
                return static_cast<int>(i);
        return -2;
    }

    int g_serviceRuns = 0;
    int g_servicePayloads = 0;
    int g_serviceSuspends = 0;
    int g_serviceLogged = 0;
    int g_idleSuspends = 0;

    // Plays the duel uninterrupted (the reference), then again suspending and restoring at step k; strict: every
    // payload from there on must equal the reference's (the clock offset aside); always: the log, the end and the
    // final state equal.
    void CompareSuspended(Rig& ref, Hs::MatchStartParams const& params, AiProfile human, char const* name, int k,
                          bool strict, uint64_t gapMs)
    {
        Rig b(params, human);
        b.Start();
        for (int i = 0; i <= k; ++i)
        {
            CHECK(b.Step());
            if (i < static_cast<int>(ref.steps.size()))
            {
                CHECK(b.steps.back().payload == ref.steps[static_cast<std::size_t>(i)].payload);
                CHECK(b.steps.back().get == ref.steps[static_cast<std::size_t>(i)].get);
            }
        }
        int aiTurn = 0, aiActions = 0;
        CHECK(b.svc.AiCountersForTests(b.guid, aiTurn, aiActions));
        Hs::MatchRestoreResult const r = b.SuspendRestore(gapMs);
        if (!r.ok)
        {
            std::printf("    %s: restore failed (%s, action %u)\n", name, r.error.c_str(), r.failedAt);
            return;
        }
        ++g_serviceSuspends;
        if (strict)
        {
            // A clean resync: the payload equals GAME_GET of the uninterrupted copy at that moment.
            std::string const want = k >= 0 ? ref.steps[static_cast<std::size_t>(k)].get : ref.startGet;
            CHECK(Norm(r.payload) == want);
            if (Norm(r.payload) != want)
                std::printf("    %s: resync differs\n      got  %s\n      want %s\n", name, Norm(r.payload).c_str(), want.c_str());
            // The AI's counters are recomputed from the log.
            int rt = 0, ra = 0;
            CHECK(b.svc.AiCountersForTests(b.guid, rt, ra));
            CHECK(rt == aiTurn && ra == aiActions);
            if (k >= 0)
                CHECK(rt == ref.steps[static_cast<std::size_t>(k)].aiTurn && ra == ref.steps[static_cast<std::size_t>(k)].aiActions);
        }
        else
            ++g_idleSuspends;
        // The restored match is the one that was suspended.
        std::string const stateAtK = k >= 0 && !ref.steps[static_cast<std::size_t>(k)].state.empty()
            ? ref.steps[static_cast<std::size_t>(k)].state : std::string();
        if (!stateAtK.empty())
            CHECK(b.M()->DebugState() == stateAtK);

        int i = k + 1;
        while (b.Step())
        {
            if (strict && i < static_cast<int>(ref.steps.size()))
            {
                bool const same = b.steps.back().payload == ref.steps[static_cast<std::size_t>(i)].payload &&
                                  b.steps.back().get == ref.steps[static_cast<std::size_t>(i)].get;
                CHECK(same);
                if (!same)
                {
                    std::printf("    %s: step %d differs after the restore at step %d\n      got  %s\n      want %s\n", name,
                                i, k, b.steps.back().payload.c_str(), ref.steps[static_cast<std::size_t>(i)].payload.c_str());
                    return;
                }
                ++g_servicePayloads;
            }
            ++i;
        }
        CHECK(b.over);
        if (strict)
            CHECK(static_cast<std::size_t>(i) == ref.steps.size());
        CHECK(b.log.size() == ref.log.size());
        bool same = b.log.size() == ref.log.size();
        for (std::size_t j = 0; same && j < b.log.size(); ++j)
            same = Hs::SameLoggedAction(b.log[j], ref.log[j]);
        CHECK(same);
        if (!same)
            std::printf("    %s: the logs differ\n", name);
        CHECK(b.end.res == ref.end.res && b.end.why == ref.end.why && b.end.turn == ref.end.turn);
        CHECK(b.end.tallies == ref.end.tallies);
        CHECK(b.M() && ref.M() && b.M()->DebugState() == ref.M()->DebugState());
        CHECK(b.M() && ref.M() && b.M()->Seed() == ref.M()->Seed());
        ++g_serviceRuns;
        g_serviceLogged += static_cast<int>(b.log.size());
    }

    // One game, several suspend points.
    void Scenario(char const* name, Hs::MatchStartParams const& params, AiProfile human, std::vector<Pick> const& picks)
    {
        Rig a(params, human);
        a.Start();
        a.Run();
        CHECK(a.over);
        CHECK(!a.log.empty() && a.log.size() == static_cast<std::size_t>(a.M()->Seq()));
        CHECK(a.svc.AiRefusals() == 0);
        std::printf("    %-22s %4d actions logged, %4d steps, %d idle steps, %d timer-forced picks, ended %s/%s after %d turns\n",
                    name, static_cast<int>(a.log.size()), static_cast<int>(a.steps.size()), a.idleSteps, a.forcedPicks,
                    a.end.res.c_str(), a.end.why.c_str(), a.end.turn);
        for (Pick const& p : picks)
        {
            int k = -1;
            if (p.flag != 0)
                k = FindStep(a, p.flag, p.actionStep, p.nth);
            CHECK(k >= -1);
            if (k < -1)
            {
                std::printf("    %s: no step for '%s'\n", name, p.name);
                continue;
            }
            std::string const label = std::string(name) + " / " + p.name;
            CompareSuspended(a, params, human, label.c_str(), k, p.actionStep || p.flag == 0, 123456);
        }
    }

    void Plain(Hs::MatchStartParams& p, uint32_t seed)
    {
        p = MakeParams("MAGE", BuildDeck("MAGE", seed), "WARRIOR", BuildDeck("WARRIOR", seed + 1), seed, true);
    }

    void TestServiceMulliganAndMidTurn()
    {
        Hs::MatchStartParams p;
        Plain(p, 11);
        Scenario("mulligan, mid-turn", p, AiProfile::Normal,
                 {{"start (mulligan pending)", 0, true, 1},
                  {"after the human mulligan", FHumanAct, true, 1},
                  {"AI mid-turn", FAiMid, true, 1},
                  {"AI mid-turn (3rd)", FAiMid, true, 3},
                  {"a human action", FHumanAct, true, 7}});
        // Hard human and Hard AI, the combination of the ladder.
        Plain(p, 12);
        Scenario("hard-hard mid-turn", p, AiProfile::Hard, {{"AI mid-turn", FAiMid, true, 2}, {"a human action", FHumanAct, true, 12}});
    }

    void TestServicePendingPicks()
    {
        // The human's deck is full of Tracking; the opponent's too.
        Hs::MatchStartParams p = MakeParams("HUNTER", ThemedDeck("HUNTER", 21, {"DS1_184"}, 8), "HUNTER",
                                            ThemedDeck("HUNTER", 22, {"DS1_184", "EX1_609"}, 4), 31, true);
        Scenario("pending picks", p, AiProfile::Hard,
                 {{"human pick pending", FHPick, true, 1}, {"AI pick pending", FAPick, true, 1}});
    }

    void TestServiceSecrets()
    {
        Hs::MatchStartParams p = MakeParams("MAGE", ThemedDeck("MAGE", 41, ThemeCards(2), 2), "PALADIN",
                                            ThemedDeck("PALADIN", 42, ThemeCards(3), 2), 51, true);
        Scenario("secrets in play", p, AiProfile::Normal, {{"first secret", FSecret, true, 1}, {"later secret", FSecret, true, 9}});
    }

    void TestServiceChooseOne()
    {
        Hs::MatchStartParams p = MakeParams("DRUID", ThemedDeck("DRUID", 61, ThemeCards(4), 2), "MAGE", BuildDeck("MAGE", 62), 71, true);
        Scenario("choose one", p, AiProfile::Normal, {{"after a Choose One play", FChoice, true, 1}});
    }

    // Nozdormu on both sides: the human's turns are timed (15 s), some are left idle so that the timer forces them,
    // one of them with a Tracking pick pending.
    void TestServiceNozdormuTimer()
    {
        // The human's deck: 8 Nozdormu and 8 Tracking among ordinary cards; the opponent's: 60 Shieldbearers (0/4, a
        // wall that cannot hurt anybody), so the game runs long enough for many turns under the limit.
        std::vector<std::string> const wall = PassiveDeck();
        CHECK(wall.size() == 60 && Db().Supported("EX1_560") && Db().Supported("DS1_184"));
        Hs::MatchStartParams p = MakeParams("HUNTER", ThemedDeck("HUNTER", 81, {"EX1_560", "DS1_184"}, 8), "PRIEST", wall, 91, true);
        p.opponentHeroHealth = 200;
        Scenario("nozdormu timer", p, AiProfile::Normal,
                 {{"mid-turn with a deadline", FTimer, true, 1},
                  {"mid-turn with a deadline (4th)", FTimer, true, 4},
                  {"after a timer-forced end", FForced, true, 1},
                  {"after a timer-forced end (3rd)", FForced, true, 3},
                  {"idle, deadline running", FIdle, false, 5},
                  {"idle, deadline running (later)", FIdle, false, 120}});

        // A human deck of spells only (the engine takes any card in any deck: 40 Wild Growth, 10 Nozdormu, 10 Tracking),
        // so that no board gets in the way of Nozdormu and Tracking is at hand in the timed turns that are left idle.
        CHECK(Db().Supported("CS2_013"));
        std::vector<std::string> spells(60, "CS2_013");
        for (std::size_t i = 0; i < 10; ++i)
        {
            spells[i * 6] = "EX1_560";
            spells[i * 6 + 3] = "DS1_184";
        }
        p = MakeParams("PRIEST", spells, "PRIEST", wall, 93, true);
        p.opponentHeroHealth = 200;
        Scenario("nozdormu and tracking", p, AiProfile::Normal,
                 {{"timer answered a pick", FForcedPick, true, 1}, {"idle, deadline running", FIdle, false, 9}});
    }

    void TestServiceTurnLimit()
    {
        std::vector<std::string> const deck = PassiveDeck();
        CHECK(deck.size() == 60);
        if (deck.empty())
            return;
        Hs::MatchStartParams p = MakeParams("PRIEST", deck, "PRIEST", deck, 5, true);
        Scenario("turn limit draw", p, AiProfile::Normal, {{"turn 88", FLate, true, 1}, {"last turn", FLate, true, 6}});
    }

    // A match that already ended can be suspended and restored too (a duel whose result was not recorded yet).
    void TestServiceFinishedDuel()
    {
        Hs::MatchStartParams p;
        Plain(p, 15);
        Rig a(p);
        a.Start();
        a.Run();
        CHECK(a.over);
        std::string const finalGet = Norm(a.svc.Get(a.guid));
        std::string const finalState = a.M()->DebugState();
        Hs::SuspendedDuel duel;
        CHECK(a.svc.Suspend(a.guid, duel, a.clock));
        CHECK(duel.over && duel.timerMs == 0 && duel.log.size() == a.log.size());
        Hs::MatchRestoreResult const r = a.svc.Restore(duel, a.clock + 1000);
        CHECK(r.ok);
        CHECK(r.ended && r.end.tallies == a.end.tallies && r.end.res == a.end.res && r.end.why == a.end.why);
        CHECK(Norm(r.payload) == finalGet);
        CHECK(a.M() && a.M()->DebugState() == finalState);
        CHECK(!a.svc.IsRunning(a.guid) && a.svc.Has(a.guid) && a.svc.IsArena(a.guid));
        Hs::MatchInfo info;
        CHECK(a.svc.Info(a.guid, info) && !info.running && info.res == a.end.res && info.why == a.end.why);
    }

    // The store failed after an action: Suspend with the number of entries the store got, Restore, and the player's
    // next try gives exactly the payload the uninterrupted copy sent.
    void TestServiceStoreFailure()
    {
        Hs::MatchStartParams p;
        Plain(p, 17);
        Rig a(p);
        a.recordState = true;
        a.Start();
        a.Run();
        CHECK(a.over);
        // A human action in the middle of the game, and an AI action.
        for (int pass = 0; pass < 2; ++pass)
        {
            int const k = FindStep(a, pass == 0 ? FHumanAct : FAiMid, true, pass == 0 ? 9 : 2);
            CHECK(k >= 1);
            if (k < 1)
                continue;
            Rig b(p);
            b.recordState = true;
            b.Start();
            for (int i = 0; i < k; ++i)
                b.Step();
            std::size_t const stored = b.log.size();
            int const hTurn0 = b.hTurn, hActions0 = b.hActions, aiTurn0 = b.aiLogTurn, aiCount0 = b.aiLogCount;
            // Do step k, then pretend its store failed: nothing of it may be shown.
            CHECK(b.Step());
            CHECK(b.log.size() > stored);
            std::string const shown = b.steps.back().payload;
            CHECK(shown == a.steps[static_cast<std::size_t>(k)].payload);
            b.steps.pop_back();
            Hs::MatchRestoreResult const r = b.SuspendRestore(5000, stored);
            CHECK(r.ok && b.log.size() == stored);
            // The restored match is the state before the failed action.
            CHECK(b.M()->DebugState() == a.steps[static_cast<std::size_t>(k) - 1].state);
            CHECK(Norm(r.payload) == a.steps[static_cast<std::size_t>(k) - 1].get);
            b.clock -= kStepMs; // the retry happens at the instant of the failed step
            b.hTurn = hTurn0; // the scripted client did not see the reply either
            b.hActions = hActions0;
            b.aiLogTurn = aiTurn0;
            b.aiLogCount = aiCount0;
            if (pass == 0)
            {
                // The human repeats the action: same payload, events included.
                CHECK(b.Step());
                CHECK(b.steps.back().payload == a.steps[static_cast<std::size_t>(k)].payload);
            }
            while (b.Step())
            {
            }
            CHECK(b.over && b.log.size() == a.log.size());
            bool same = b.log.size() == a.log.size();
            for (std::size_t j = 0; same && j < b.log.size(); ++j)
                same = Hs::SameLoggedAction(b.log[j], a.log[j]);
            CHECK(same);
            CHECK(b.M()->DebugState() == a.M()->DebugState());
            ++g_serviceSuspends;
        }
    }

    // Everything Restore refuses, and what an ordinary match does not get.
    void TestServiceRefusals()
    {
        Hs::MatchStartParams p;
        Plain(p, 19);
        Rig a(p);
        a.Start();
        for (int i = 0; i < 40; ++i)
            a.Step();
        Hs::SuspendedDuel duel;
        CHECK(a.svc.Suspend(a.guid, duel, a.clock));
        CHECK(!a.svc.Suspend(a.guid, duel, a.clock)); // nothing left to suspend

        auto const restore = [&](Hs::SuspendedDuel const& d, char const* want, uint32_t failedAt = 0) {
            Hs::MatchRestoreResult const r = a.svc.Restore(d, a.clock);
            CHECK(!r.ok && r.error == want && r.failedAt == failedAt && r.payload.empty());
            if (r.ok || r.error != want)
                std::printf("    restore: '%s' (wanted '%s')\n", r.error.c_str(), want);
            CHECK(!a.svc.Has(a.guid)); // nothing was stored
        };
        Hs::SuspendedDuel bad = duel;
        bad.rulesVersion = kRulesVersion + 1;
        restore(bad, "rules");
        bad = duel;
        bad.rulesVersion = 0;
        restore(bad, "rules");
        bad = duel;
        bad.datasetHash = "0000000000000000";
        restore(bad, "dataset");
        bad = duel;
        bad.datasetHash.clear();
        restore(bad, "dataset");
        bad = duel;
        bad.params.arena = false;
        restore(bad, "arena");
        // A log with a refused action (a play of an entity that is not in the hand) fails at that index.
        bad = duel;
        bad.log.insert(bad.log.begin() + 5, Logged(0, ActionType::Play, 999999));
        restore(bad, "log", 5);
        bad = duel;
        bad.log[0].side = 2;
        restore(bad, "log", 0);
        // The log runs past the end of the match.
        {
            Hs::MatchStartParams q;
            Plain(q, 20);
            Rig full(q);
            full.Start();
            full.Run();
            Hs::SuspendedDuel done;
            CHECK(full.svc.Suspend(full.guid, done, full.clock));
            bad = done;
            bad.log.push_back(Logged(0, ActionType::EndTurn));
            Hs::MatchRestoreResult const r = full.svc.Restore(bad, full.clock);
            CHECK(!r.ok && r.error == "log" && r.failedAt == done.log.size());
            // The unchanged log restores.
            CHECK(full.svc.Restore(done, full.clock).ok);
        }
        // A setup the engine refuses (an unknown card) fails with the engine's own text, nothing thrown.
        bad = duel;
        bad.params.humanDeck[0] = "NO_SUCH_CARD";
        {
            Hs::MatchRestoreResult const r = a.svc.Restore(bad, a.clock);
            CHECK(!r.ok && r.error.find("NO_SUCH_CARD") != std::string::npos);
        }
        bad = duel;
        bad.params.difficulty = "easy";
        {
            Hs::MatchRestoreResult const r = a.svc.Restore(bad, a.clock);
            CHECK(!r.ok && r.error.find("difficulty") != std::string::npos);
        }
        // No card database.
        {
            Hs::MatchService empty;
            Hs::MatchRestoreResult const r = empty.Restore(duel, 0);
            CHECK(!r.ok && r.error == "engine");
        }
        // The good duel restores, once; a running match refuses a second restore.
        Hs::MatchRestoreResult const good = a.svc.Restore(duel, a.clock);
        CHECK(good.ok && good.matchId != 0 && a.svc.IsRunning(a.guid) && a.svc.IsArena(a.guid));
        Hs::MatchRestoreResult const again = a.svc.Restore(duel, a.clock);
        CHECK(!again.ok && again.error == "running");
        // ... and the same duel can be restored for another character (params.guid decides the key).
        Hs::SuspendedDuel other = duel;
        other.params.guid = 8;
        CHECK(a.svc.Restore(other, a.clock).ok && a.svc.IsArena(8) && a.svc.RunningCount() == 2);
        // Start refuses while the restored duel runs.
        Hs::MatchStartResult const s = a.svc.Start(p, a.clock);
        CHECK(!s.ok && s.error == "running");
        // Abort ends a restored arena match like any other (the glue suspends instead; the service still can).
        CHECK(a.svc.Abort(a.guid, Hs::Interrupt::Gm).ended && !a.svc.IsRunning(a.guid));
        Hs::SuspendedDuel gone;
        CHECK(!a.svc.Suspend(77, gone, 0));
    }

    // Ordinary matches keep no log, cannot be suspended and produce the same payloads as the arena duel.
    void TestServiceOrdinaryUnchanged()
    {
        Hs::MatchStartParams arena;
        Plain(arena, 23);
        Hs::MatchStartParams ordinary = arena;
        ordinary.arena = false;
        ordinary.arenaRunId = 0;
        Rig a(arena);
        Rig o(ordinary);
        a.Start();
        o.Start();
        a.Run();
        o.Run();
        CHECK(a.over && o.over);
        CHECK(o.log.empty() && !a.log.empty());
        CHECK(a.steps.size() == o.steps.size());
        bool same = a.steps.size() == o.steps.size();
        for (std::size_t i = 0; same && i < a.steps.size(); ++i)
            same = a.steps[i].payload == o.steps[i].payload && a.steps[i].get == o.steps[i].get;
        CHECK(same);
        CHECK(o.M()->DebugState() == a.M()->DebugState());
        CHECK(o.end.res == a.end.res && o.end.why == a.end.why && o.end.turn == a.end.turn);
        CHECK(o.end.tallies == a.end.tallies);
        CHECK(!o.svc.IsArena(o.guid) && o.svc.IsArena(0) == false);
        Hs::SuspendedDuel duel;
        CHECK(!o.svc.Suspend(o.guid, duel, 0));
        CHECK(o.svc.Has(o.guid)); // untouched
        // A running ordinary match is not touched by Suspend either.
        Rig r(ordinary);
        r.Start();
        r.Step();
        CHECK(!r.svc.Suspend(r.guid, duel, 0) && r.svc.IsRunning(r.guid));
        CHECK(r.svc.Abort(r.guid, Hs::Interrupt::Gm).ended);
        // An arena duel that is aborted from outside reports the end like an ordinary one.
        Rig x(arena);
        x.Start();
        x.Step();
        Hs::MatchAbortOutcome const ab = x.svc.Abort(x.guid, Hs::Interrupt::Teleport);
        CHECK(ab.ended && ab.end.res == "int" && ab.end.why == "teleport");
    }

    void TestServiceReport()
    {
        std::printf("    %d interrupted duels played to the end (%d at idle moments): %d payloads compared, %d actions logged\n",
                    g_serviceRuns, g_idleSuspends, g_servicePayloads, g_serviceLogged);
        CHECK(g_serviceRuns >= 20);
        CHECK(g_servicePayloads > 1000);
    }
}

int main(int argc, char** argv)
{
    if (argc > 2)
        g_gamesArg = std::atoi(argv[2]);
    std::setvbuf(stdout, nullptr, _IONBF, 0); // keep the output of a crashing test
    RUN(TestActionText);
    RUN(TestReplayHardHard);
    RUN(TestReplayHardHardTimerForced);
    RUN(TestReplayNormal);
    RUN(TestReplayTurnLimit);
    RUN(TestReplayCoverage);
    RUN(TestServiceMulliganAndMidTurn);
    RUN(TestServicePendingPicks);
    RUN(TestServiceSecrets);
    RUN(TestServiceChooseOne);
    RUN(TestServiceNozdormuTimer);
    RUN(TestServiceTurnLimit);
    RUN(TestServiceFinishedDuel);
    RUN(TestServiceStoreFailure);
    RUN(TestServiceRefusals);
    RUN(TestServiceOrdinaryUnchanged);
    RUN(TestServiceReport);

    std::printf("\n%d checks, %d failed\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
