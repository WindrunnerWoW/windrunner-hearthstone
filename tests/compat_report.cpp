// Compatibility report: every card of the frozen 6024 dataset with its state in the match engine.
//   compat_report <testsDir>          checks; exit code 0 = no standard card is incomplete, no missing dependency
//                                     (the collectible Naxxramas cards, group "quest", are reported with their state
//                                     but do not fail the check: they are imported before their gameplay exists, so
//                                     they show as "unsupported" until the engine implements them)
//   compat_report <testsDir> --emit   writes the report (JSON) to stdout; build_tests.ps1 compares it with the
//                                     committed data/cards/compat_report.json (STALE when they differ)
//
// States (derived, never copied from a source):
//   imported      the card is in the dataset (always true here)
//   implemented   CardDb::Supported: a script, or a minion / weapon whose text is keywords only; every card its
//                 script refers to (tokens, options, generated cards) is known and supported
//   smoke         every legal play of the card in three board positions is accepted, and the turns after it
//                 run without a refusal; the AI (Normal and Hard) acts without a refusal with the card in hand
//   tested        the card id appears at least twice in the per-card test programs (a coverage list mentions
//                 every id once; a focused scenario adds more)
//   complete      implemented, smoke ok, and tested (keyword-only cards: smoke ok suffices, the keywords
//                 themselves are covered by the engine tests)
// Artwork and ownership are independent of these states.

#include "../src/match/HsMatch.h"
#include "../src/match/HsMatchAi.h"
#include "../src/match/HsMatchInternal.h"
#include "card_catalog.h"
#include "match_all_cards.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace Hs::Match;
using TA = TestAccess;

namespace
{
    CardDb const& Db() { return MatchAllCards::Db(); }

    char const* const kTestFiles[] = {"match_tests.cpp", "protocol_tests.cpp", "match_core_tests.cpp", "match_cards_basic_tests.cpp",
        "match_cards_classic_tests.cpp", "match_cards_rest_tests.cpp", "match_cards_engine_tests.cpp",
        "match_cards_naxx_tests.cpp", "match_ai_tests.cpp"};

    std::string ReadFile(std::string const& path)
    {
        std::ifstream f(path.c_str(), std::ios::binary);
        std::ostringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    int CountOccurrences(std::string const& hay, std::string const& needle)
    {
        int n = 0;
        for (std::size_t p = hay.find(needle); p != std::string::npos; p = hay.find(needle, p + needle.size()))
            ++n;
        return n;
    }

    std::string Json(std::string const& s)
    {
        std::string o = "\"";
        for (char c : s)
        {
            if (c == '"' || c == '\\')
                o += '\\';
            if (static_cast<unsigned char>(c) < 0x20)
                continue;
            o += c;
        }
        return o + "\"";
    }

    Action End()
    {
        Action a;
        a.type = ActionType::EndTurn;
        return a;
    }

    // ---- smoke ------------------------------------------------------------------------------

    // Board positions the card is tried in: 0 empty boards; 1 a mixed board on both sides (races, a damaged
    // minion, weapons); 2 the own board full.
    std::unique_ptr<Match> Position(std::string const& card, std::string const& cls, int pos, EntityId& handId)
    {
        MatchSetup s;
        s.seed = 99;
        s.firstSide = 0;
        for (int i = 0; i < 2; ++i)
        {
            s.sides[i].name = i ? "B" : "A";
            s.sides[i].heroClass = i ? "MAGE" : cls;
            s.sides[i].deck.assign(30, "CS2_182");
        }
        std::string error;
        std::unique_ptr<Match> m = Match::Create(Db(), s, error);
        if (!m)
            return m;
        Action mull;
        mull.type = ActionType::Mulligan;
        m->Apply(0, mull);
        m->Apply(1, mull);
        TA::SetMana(*m, 0, 10, 10);
        handId = TA::SetHand(*m, 0, {card, "CS2_182"})[0];
        if (pos == 1)
        {
            // Yeti (damaged), Murloc Raider, River Crocolisk, Voidwalker, Southsea Deckhand, Boulderfist Ogre;
            // a fuller hand (Mountain Giant)
            std::vector<std::string> const mixed = {"CS2_182", "CS2_168", "CS2_120", "CS2_065", "CS2_146", "CS2_200"};
            handId = TA::SetHand(*m, 0, {card, "CS2_182", "CS2_182", "CS2_182", "CS2_182", "CS2_182", "CS2_182", "CS2_182"})[0];
            for (int side = 0; side < 2; ++side)
            {
                std::vector<EntityId> const b = TA::SetBoard(*m, side, mixed);
                TA::SetMinion(*m, b[0], 4, 3, 5);
                TA::SetWeapon(*m, side, "CS2_106");
                TA::SetHero(*m, side, 20, 0);
            }
            TA::SetHand(*m, 1, {"CS2_182", "CS2_029", "CS2_168"});
        }
        else if (pos == 2)
        {
            TA::SetBoard(*m, 0, std::vector<std::string>(7, "CS2_182"));
            TA::SetBoard(*m, 1, {"CS2_120"});
        }
        TA::ReadyAll(*m);
        return m;
    }

    // Answers a pending pick of either side with its first option. False when that is refused.
    bool ResolvePicks(Match& m)
    {
        for (int s = 0; s < 2; ++s)
        {
            MatchView const v = m.ViewFor(s, m.LastEventSeq());
            if (v.pickPending && !v.pick.empty())
            {
                Action a;
                a.type = ActionType::Choose;
                a.source = v.pick[0].id;
                if (m.Apply(s, a) != ActionError::None)
                    return false;
            }
        }
        return true;
    }

    // Plays out a few turns with ended turns only; false on any refusal.
    bool RunOn(Match& m)
    {
        for (int t = 0; t < 3 && m.GetPhase() == Phase::Play; ++t)
        {
            if (!ResolvePicks(m))
                return false;
            if (m.GetPhase() != Phase::Play)
                break;
            if (m.Apply(m.ActiveSide(), End()) != ActionError::None)
                return false;
        }
        return ResolvePicks(m);
    }

    struct Smoke
    {
        int plays = 0;  // legal plays of the card tried
        int aiRuns = 0; // AI decisions taken with the card in hand
        std::string failure;
    };

    Smoke SmokeCard(std::string const& card, std::string const& cls)
    {
        Smoke r;
        for (int pos = 0; pos < 3 && r.failure.empty(); ++pos)
        {
            EntityId hand = 0;
            std::unique_ptr<Match> probe = Position(card, cls, pos, hand);
            if (!probe)
            {
                r.failure = "create";
                return r;
            }
            std::vector<Action> legal = probe->LegalActions(0);
            for (std::size_t i = 0; i < legal.size() && r.failure.empty(); ++i)
            {
                if (legal[i].type != ActionType::Play || legal[i].source != hand)
                    continue;
                EntityId h2 = 0;
                std::unique_ptr<Match> m = Position(card, cls, pos, h2);
                ActionError const e = m->Apply(0, legal[i]);
                ++r.plays;
                if (e != ActionError::None)
                    r.failure = std::string("play refused (") + ActionErrorCode(e) + ") in position " + std::to_string(pos);
                else if (!RunOn(*m))
                    r.failure = "a later turn was refused in position " + std::to_string(pos);
            }
            for (int prof = 0; prof < 2 && r.failure.empty(); ++prof)
            {
                EntityId h3 = 0;
                std::unique_ptr<Match> m = Position(card, cls, pos, h3);
                for (int n = 0; n < kAiMaxActionsPerTurn && m->GetPhase() == Phase::Play && m->ActiveSide() == 0; ++n)
                {
                    if (!ResolvePicks(*m))
                    {
                        r.failure = "AI pick refused";
                        break;
                    }
                    if (m->GetPhase() != Phase::Play || m->ActiveSide() != 0)
                        break;
                    Action const a = ChooseAiAction(*m, 0, Db(), n, prof ? AiProfile::Hard : AiProfile::Normal,
                        static_cast<uint32_t>(1000 + n));
                    ActionError const e = m->Apply(0, a);
                    ++r.aiRuns;
                    if (e != ActionError::None)
                    {
                        r.failure = std::string("AI action refused (") + ActionErrorCode(e) + ") in position " +
                            std::to_string(pos);
                        break;
                    }
                    if (a.type == ActionType::EndTurn)
                        break;
                }
            }
        }
        if (r.failure.empty() && r.plays == 0)
            r.failure = "never playable in the smoke positions";
        return r;
    }

    // ---- report -------------------------------------------------------------------------------

    struct Entry
    {
        std::string id, name, cls, type, set, acquisition;
        std::string kind; // script, keywords, none
        bool implemented = false;
        std::vector<std::string> refs;
        std::vector<std::string> missingRefs;
        int testRefs = 0;
        bool tested = false;
        bool smokeRun = false;
        Smoke smoke;
        std::string state;
    };

    char const* TypeName(CardType t)
    {
        switch (t)
        {
            case CardType::Minion: return "MINION";
            case CardType::Spell: return "SPELL";
            case CardType::Weapon: return "WEAPON";
            case CardType::Hero: return "HERO";
            case CardType::HeroPower: return "HERO_POWER";
            default: return "OTHER";
        }
    }
}

int main(int argc, char** argv)
{
    std::string const dir = argc > 1 ? argv[1] : ".";
    bool const emit = argc > 2 && std::string(argv[2]) == "--emit";

    std::string tests;
    for (char const* f : kTestFiles)
        tests += ReadFile(dir + "/" + f);

    std::map<std::string, CardCatalog::Row const*> catalog;
    for (CardCatalog::Row const& r : CardCatalog::kRows)
        catalog[r.id] = &r;

    // Every non-collectible card some script refers to (tokens, options, generated cards): a dependency.
    std::set<std::string> deps;
    std::vector<Entry> entries;
    for (CardCatalog::Row const& r : CardCatalog::kRows)
    {
        CardDef const* def = Db().Find(r.id);
        if (!def)
            continue;
        Entry e;
        e.id = r.id;
        e.name = def->name;
        e.cls = def->cardClass;
        e.type = TypeName(def->type);
        e.set = r.set;
        e.acquisition = r.acquisition;
        Detail::Script const* sc = Detail::FindScript(r.id);
        e.implemented = Db().Supported(r.id);
        e.kind = sc ? "script" : (e.implemented ? "keywords" : "none");
        if (sc)
        {
            for (std::string const& ref : sc->refs)
            {
                if (ref == e.id)
                    continue;
                e.refs.push_back(ref);
                if (!Db().Find(ref) || !Db().Supported(ref))
                    e.missingRefs.push_back(ref);
                auto c = catalog.find(ref);
                if (c != catalog.end() && !c->second->collectible)
                    deps.insert(ref);
            }
            std::sort(e.refs.begin(), e.refs.end());
            e.refs.erase(std::unique(e.refs.begin(), e.refs.end()), e.refs.end());
        }
        e.testRefs = CountOccurrences(tests, "\"" + e.id + "\"");
        e.tested = e.testRefs >= 2;
        entries.push_back(e);
    }

    // Smoke and states for the collectible, playable cards (standard, reward-only, quest, unreleased).
    int incomplete = 0, missing = 0;
    std::map<std::string, std::map<std::string, int>> totals; // group -> state -> n
    for (Entry& e : entries)
    {
        CardCatalog::Row const* r = catalog[e.id];
        bool const playable = r->collectible && (e.type == "MINION" || e.type == "SPELL" || e.type == "WEAPON");
        std::string group;
        if (playable)
            group = e.acquisition; // standard, reward_only, quest, unreleased
        else if (deps.count(e.id))
            group = "dependency";
        else
            continue;
        if (!e.missingRefs.empty())
            ++missing;
        if (playable && e.implemented)
        {
            std::string const cls = (e.cls.empty() || e.cls == "NEUTRAL") ? "MAGE" : e.cls;
            e.smoke = SmokeCard(e.id, cls);
            e.smokeRun = true;
        }
        if (!e.implemented)
            e.state = "unsupported";
        else if (!e.missingRefs.empty())
            e.state = "missing-dependency";
        else if (!playable)
            e.state = "complete"; // dependencies are exercised through the card that creates them
        else if (!e.smoke.failure.empty())
            e.state = "implemented";
        else if (e.tested || e.kind == "keywords")
            e.state = "complete";
        else
            e.state = "implemented";
        ++totals[group][e.state];
        ++totals[group]["total"];
        if (group == "standard" && e.state != "complete")
            ++incomplete;
    }

    if (emit)
    {
        std::printf("{\n  \"dataset\": \"hs-6024-basic-classic\",\n  \"generated_by\": \"tests/compat_report.cpp\",\n");
        std::printf("  \"totals\": {");
        bool firstG = true;
        for (auto const& g : totals)
        {
            std::printf("%s\n    %s: {", firstG ? "" : ",", Json(g.first).c_str());
            firstG = false;
            bool firstS = true;
            for (auto const& s : g.second)
            {
                std::printf("%s%s: %d", firstS ? "" : ", ", Json(s.first).c_str(), s.second);
                firstS = false;
            }
            std::printf("}");
        }
        std::printf("\n  },\n  \"cards\": [");
        bool first = true;
        for (Entry const& e : entries)
        {
            if (e.state.empty())
                continue;
            CardCatalog::Row const* r = catalog[e.id];
            std::string refs;
            for (std::string const& x : e.refs)
                refs += (refs.empty() ? "" : ", ") + Json(x);
            std::printf("%s\n    {\"id\": %s, \"name\": %s, \"class\": %s, \"type\": %s, \"set\": %s, \"group\": %s, "
                        "\"state\": %s, \"kind\": %s, \"smoke\": %s, \"tested\": %s, \"refs\": [%s]}",
                first ? "" : ",", Json(e.id).c_str(), Json(e.name).c_str(), Json(e.cls).c_str(), Json(e.type).c_str(),
                Json(e.set).c_str(),
                Json(r->collectible ? e.acquisition : std::string("dependency")).c_str(), Json(e.state).c_str(),
                Json(e.kind).c_str(),
                Json(!e.smokeRun ? std::string("n/a") : e.smoke.failure.empty() ? std::string("ok") : e.smoke.failure).c_str(),
                e.tested ? "true" : "false", refs.c_str());
            first = false;
        }
        std::printf("\n  ]\n}\n");
        return 0;
    }

    for (auto const& g : totals)
    {
        std::printf("  %-12s", g.first.c_str());
        for (auto const& s : g.second)
            std::printf(" %s=%d", s.first.c_str(), s.second);
        std::printf("\n");
    }
    for (Entry const& e : entries)
        if (!e.state.empty() && e.state != "complete")
            std::printf("  %-10s %-26s %-18s %s\n", e.id.c_str(), e.name.c_str(), e.state.c_str(),
                !e.smoke.failure.empty() ? e.smoke.failure.c_str() : e.missingRefs.empty() ? "" : "missing refs");
    std::printf("%d standard cards incomplete, %d cards with a missing dependency\n", incomplete, missing);
    return (incomplete == 0 && missing == 0) ? 0 : 1;
}
