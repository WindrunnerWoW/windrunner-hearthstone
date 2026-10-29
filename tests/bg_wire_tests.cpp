// Standalone tests of the Battlegrounds session and wire format (src/battlegrounds: HsBgSession, HsBgWire on
// top of the game, the real NPC policy and the WRHS codec).
//   bg_wire_tests.exe [directory]      run all tests; exit code 0 = all checks passed.
//
// The full-game tests drive a seeded game ONLY through the wire: a small test client parses the BG_STATE / BG_COMBAT /
// BG_END frames it is given, decides from that text alone and answers with encoded BG_ACT payloads, which the "server"
// side decodes and applies. If a state field the addon needs is missing, these tests cannot play.

#include "../src/battlegrounds/HsBgNames.h"
#include "../src/battlegrounds/HsBgWire.h"
#include "../src/protocol/HsProtocol.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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
        fn();                                                                                         \
    } while (0)

namespace
{
    // ------------------------------------------------------------------------------------------ frame helpers

    struct Message
    {
        std::string op;
        std::string reqId;
        std::map<std::string, std::string> kv;
    };

    // Reassembles the frames of one batch into messages (consecutive parts of one op), checking every frame on the way.
    std::vector<Message> Reassemble(std::vector<std::string> const& frames)
    {
        std::vector<Message> out;
        std::string op;
        std::string reqId;
        std::string payload;
        uint32_t expected = 1;
        for (std::string const& wire : frames)
        {
            CHECK(wire.size() + 5 <= Hs::kMaxWireBytes); // "WRBG" + TAB + frame
            Hs::Frame f;
            Hs::ParseResult const pr = Hs::ParseFrame(wire, f);
            CHECK(pr.status == Hs::ParseStatus::Ok);
            if (pr.status != Hs::ParseStatus::Ok)
                continue;
            if (f.part == 1)
            {
                CHECK(expected == 1);
                op = f.op;
                reqId = f.reqId;
                payload.clear();
            }
            else
            {
                CHECK(f.op == op && f.reqId == reqId && f.part == expected);
            }
            payload += f.payload;
            if (f.part == f.total)
            {
                Message m;
                m.op = op;
                m.reqId = reqId;
                if (!payload.empty())
                    CHECK(Hs::KvDecode(payload, m.kv));
                out.push_back(m);
                expected = 1;
            }
            else
                expected = f.part + 1;
        }
        CHECK(expected == 1);
        return out;
    }

    std::vector<std::string> Split(std::string const& s, char sep)
    {
        std::vector<std::string> out;
        if (s.empty())
            return out;
        std::size_t from = 0;
        while (true)
        {
            std::size_t const at = s.find(sep, from);
            if (at == std::string::npos)
            {
                out.push_back(s.substr(from));
                return out;
            }
            out.push_back(s.substr(from, at - from));
            from = at + 1;
        }
    }

    struct CMinion
    {
        std::string id;
        bool golden = false;
        int attack = 0;
        int health = 0;
        uint32_t keywords = 0;
        uint32_t uid = 0;
    };

    std::vector<CMinion> ParseMinions(std::string const& s)
    {
        std::vector<CMinion> out;
        for (std::string const& item : Split(s, ','))
        {
            std::vector<std::string> const f = Split(item, '.');
            CHECK(f.size() == 6);
            if (f.size() != 6)
                continue;
            CMinion m;
            m.id = f[0];
            m.golden = f[1] == "1";
            m.attack = std::atoi(f[2].c_str());
            m.health = std::atoi(f[3].c_str());
            m.keywords = static_cast<uint32_t>(std::strtoul(f[4].c_str(), nullptr, 10));
            m.uid = static_cast<uint32_t>(std::strtoul(f[5].c_str(), nullptr, 10));
            CHECK(FindCard(m.id) >= 0 || [&] { bool g = false; return FindCardAny(m.id, g) >= 0; }());
            out.push_back(m);
        }
        return out;
    }

    int Int(std::map<std::string, std::string> const& kv, char const* key)
    {
        auto const it = kv.find(key);
        CHECK(it != kv.end());
        return it == kv.end() ? 0 : std::atoi(it->second.c_str());
    }

    std::string Str(std::map<std::string, std::string> const& kv, char const* key)
    {
        auto const it = kv.find(key);
        CHECK(it != kv.end());
        return it == kv.end() ? std::string() : it->second;
    }

    // ------------------------------------------------------------------------------------------ combat replay

    struct Live
    {
        int attack = 0;
        int health = 0;
    };

    // Replays the event log on the start boards; the survivors must come out. Returns false on a mismatch.
    bool ReplayMatches(Message const& m)
    {
        std::map<uint32_t, Live> live;
        std::map<uint32_t, Live> dead;
        for (char const* key : { "s0", "s1" })
            for (CMinion const& c : ParseMinions(Str(m.kv, key)))
                live[c.uid] = Live{ c.attack, c.health };
        for (std::string const& item : Split(Str(m.kv, "ev"), ','))
        {
            std::vector<std::string> const f = Split(item, '.');
            if (f.size() != 10)
                return false;
            int const type = std::atoi(f[0].c_str());
            uint32_t const actor = static_cast<uint32_t>(std::strtoul(f[2].c_str(), nullptr, 10));
            uint32_t const target = static_cast<uint32_t>(std::strtoul(f[3].c_str(), nullptr, 10));
            int const amount = std::atoi(f[4].c_str());
            int const amount2 = std::atoi(f[5].c_str());
            switch (static_cast<EventType>(type))
            {
            case EventType::Damage:
                if (!live.count(target))
                    return false;
                live[target].health -= amount;
                break;
            case EventType::Death:
                if (!live.count(target))
                    return false;
                dead[target] = live[target];
                live.erase(target);
                break;
            case EventType::Summon:
                live[target] = Live{ amount, amount2 };
                break;
            case EventType::Reborn:
            {
                Live l = dead.count(actor) ? dead[actor] : Live();
                l.health = 1;
                live[target] = l;
                break;
            }
            case EventType::Buff:
                if (!live.count(target))
                    return false;
                live[target].attack += amount;
                live[target].health += amount2;
                break;
            default:
                break;
            }
        }
        std::set<uint32_t> want;
        for (char const* key : { "v0", "v1" })
            for (CMinion const& c : ParseMinions(Str(m.kv, key)))
                want.insert(c.uid);
        std::set<uint32_t> have;
        for (auto const& kv : live)
            have.insert(kv.first);
        return want == have;
    }

    // ------------------------------------------------------------------------------------------ the test client

    struct ClientState
    {
        bool valid = false;
        bool alive = true;
        bool ended = false;
        bool over = false;
        int round = 0;
        int gold = 0;
        int tier = 1;
        int upgradeCost = 0;
        int health = 0;
        std::vector<CMinion> offers, hand, board, options;
        std::vector<std::string> powerOptions;   // an open hero-power choice
        int lobbyHealth[8] = { 0 };
    };

    struct Stats
    {
        int games = 0;
        int actions = 0;
        int refused = 0;
        int combats = 0;
        int truncated = 0;
        int replayed = 0;
        int ends = 0;
        int won = 0;
    };

    // The "server" (Session) and the client in one place; every exchange goes through encoded text.
    struct Link
    {
        Session session;
        ClientState cs;
        std::vector<Message> combats;
        std::vector<Message> ends;
        Stats* stats;
        int reqCounter = 0;

        Link(uint32_t seed, Stats* st) : session(MakeOptions(seed)), stats(st) {}

        static SessionOptions MakeOptions(uint32_t seed)
        {
            SessionOptions o;
            o.seed = seed;
            return o;
        }

        void Take(std::vector<std::string> const& frames)
        {
            for (Message const& m : Reassemble(frames))
            {
                if (m.op == "BG_STATE")
                {
                    cs.valid = true;
                    cs.alive = Int(m.kv, "al") == 1;
                    cs.ended = Int(m.kv, "en") == 1;
                    cs.over = Str(m.kv, "ph") == "o";
                    cs.round = Int(m.kv, "rd");
                    cs.gold = Int(m.kv, "g");
                    cs.tier = Int(m.kv, "t");
                    cs.upgradeCost = Int(m.kv, "uc");
                    cs.health = Int(m.kv, "hp");
                    cs.offers = ParseMinions(Str(m.kv, "of"));
                    cs.hand = ParseMinions(Str(m.kv, "hd"));
                    cs.board = ParseMinions(Str(m.kv, "bd"));
                    cs.options = ParseMinions(Str(m.kv, "op"));
                    cs.powerOptions = Split(Str(m.kv, "opw"), ',');
                    CHECK(cs.options.empty() || cs.powerOptions.empty());
                    std::vector<std::string> const lobby = Split(Str(m.kv, "pl"), ',');
                    CHECK(lobby.size() == 8);
                    for (std::size_t i = 0; i < lobby.size() && i < 8; ++i)
                        cs.lobbyHealth[i] = std::atoi(lobby[i].c_str());
                    CHECK(cs.board.size() <= 7 && cs.hand.size() <= 10);
                    CHECK(cs.gold >= 0);
                }
                else if (m.op == "BG_COMBAT")
                {
                    combats.push_back(m);
                    ++stats->combats;
                    if (Int(m.kv, "tr") == 1)
                        ++stats->truncated;
                    else
                    {
                        CHECK(ReplayMatches(m));
                        ++stats->replayed;
                    }
                }
                else if (m.op == "BG_END")
                {
                    ends.push_back(m);
                    ++stats->ends;
                    if (Int(m.kv, "won") == 1)
                        ++stats->won;
                }
            }
        }

        // Sends one action as text; returns true when the server accepted it.
        bool Send(Action const& a)
        {
            std::string const text = EncodeBgAction(a);
            Action decoded;
            CHECK(DecodeBgAction(text, decoded));
            ActionResult const r = session.Apply(decoded);
            std::vector<std::string> frames;
            AppendBgActionReply(frames, std::to_string(++reqCounter % 9000 + 1), session, r);
            ++stats->actions;
            if (r != ActionResult::Ok)
            {
                ++stats->refused;
                std::vector<Message> const msgs = Reassemble(frames);
                CHECK(msgs.size() == 1 && msgs[0].op == "ERR" && Str(msgs[0].kv, "code") == Hs::ErrorCodeName(Hs::ErrorCode::Refused));
                return false;
            }
            Take(frames);
            return true;
        }

        void Start()
        {
            std::vector<std::string> frames;
            AppendBgSnapshot(frames, "0", session);
            Take(frames);
        }
    };

    Action Make(ActionType t, int index = 0, int slot = 0, uint32_t uid = 0)
    {
        Action a;
        a.type = t;
        a.index = index;
        a.slot = slot;
        a.uid = uid;
        return a;
    }

    // One shop turn of a plain client: discover, play, buy, upgrade now and then, sell when stuck, then end.
    void PlayTurn(Link& l, int mode)
    {
        if (mode == 1) // passive: only ends turns
        {
            l.Send(Make(ActionType::EndTurn));
            return;
        }
        for (int guard = 0; guard < 60 && l.cs.alive && !l.cs.over; ++guard)
        {
            ClientState const& c = l.cs;
            if (!c.options.empty())
            {
                l.Send(Make(ActionType::Discover, 0));
                continue;
            }
            if (!c.hand.empty())
            {
                // A spell in the hand is cast, not played (the client knows the table too).
                int const card = FindCard(c.hand[0].id);
                if (card >= 0 && CardAt(card).role == CardRole::Spell)
                {
                    EffectDef const* const e = EffectRegistry::Global().Find(card);
                    bool castable = e && !e->spellNeedsTarget;
                    uint32_t target = 0;
                    if (e && e->spellNeedsTarget)
                        for (CMinion const& m : c.board)
                        {
                            int const mc = FindCard(m.id);
                            if (mc < 0 || (e->spellTargetTribe != 0 && !HasTribe(CardAt(mc).tribes, e->spellTargetTribe)))
                                continue;
                            target = m.uid;
                            castable = true;
                            break;
                        }
                    if (castable && l.Send(Make(ActionType::Cast, 0, 0, target)))
                        continue;
                    // A spell it cannot use now: leave it for a later turn instead of a refused play.
                    break;
                }
            }
            if (!c.hand.empty() && c.board.size() < 7)
            {
                uint32_t const target = c.board.empty() ? 0u : c.board.back().uid;
                if (l.Send(Make(ActionType::Play, 0, static_cast<int>(c.board.size()), target)))
                    continue;
                if (l.Send(Make(ActionType::Play, 0, static_cast<int>(c.board.size()), 0)))
                    continue;
            }
            if (c.upgradeCost >= 0 && c.gold >= c.upgradeCost && (c.round % 2 == 0 || c.upgradeCost <= 2))
            {
                if (l.Send(Make(ActionType::Upgrade)))
                    continue;
            }
            if (c.gold >= 3 && !c.offers.empty() && c.hand.size() < 10)
            {
                int best = 0;
                for (int i = 1; i < static_cast<int>(c.offers.size()); ++i)
                    if (c.offers[static_cast<std::size_t>(i)].attack + c.offers[static_cast<std::size_t>(i)].health >
                        c.offers[static_cast<std::size_t>(best)].attack + c.offers[static_cast<std::size_t>(best)].health)
                        best = i;
                if (l.Send(Make(ActionType::Buy, best)))
                    continue;
            }
            if (c.board.size() >= 7 && c.hand.size() >= 10)
            {
                Action sell = Make(ActionType::Sell, 0);
                sell.zone = Zone::Hand;
                l.Send(sell);
                continue;
            }
            if (c.board.size() >= 2 && c.round % 3 == 0 && guard < 2)
            {
                l.Send(Make(ActionType::Move, 0, static_cast<int>(c.board.size()) - 1));
                continue;
            }
            if (c.gold >= 1 && c.round % 4 == 1 && guard < 3)
            {
                l.Send(Make(ActionType::Refresh));
                continue;
            }
            if (c.gold < 3 && !c.offers.empty() && c.round % 5 == 0 && guard < 4)
            {
                Action fz = Make(ActionType::Freeze);
                fz.flag = true;
                l.Send(fz);
                continue;
            }
            break;
        }
        if (l.cs.alive && !l.cs.over)
            l.Send(Make(ActionType::EndTurn));
    }

    void PlayGame(uint32_t seed, int mode, Stats& stats)
    {
        Link l(seed, &stats);
        l.Start();
        CHECK(l.cs.valid && l.cs.round == 1 && l.cs.gold == 3 && l.cs.tier == 1 && l.cs.health == 40);
        CHECK(l.cs.offers.size() == 3);
        int turns = 0;
        int lastRound = 0;
        while (!l.cs.over && turns < 400)
        {
            CHECK(l.cs.round >= lastRound);
            lastRound = l.cs.round;
            PlayTurn(l, mode);
            ++turns;
        }
        ++stats.games;
        CHECK(l.cs.over);
        CHECK(l.session.Over());
        CHECK(l.ends.size() == 1);
        if (!l.ends.empty())
        {
            Message const& e = l.ends[0];
            CHECK(Int(e.kv, "win") == l.session.GetGame().WinnerFaction());
            int const po = Int(e.kv, "po");
            CHECK(po >= 1 && po <= 8);
            std::vector<std::string> const order = Split(Str(e.kv, "pl"), ',');
            CHECK(order.size() == 8);
            std::set<int> places;
            for (std::string const& p : order)
                places.insert(std::atoi(p.c_str()));
            CHECK(places.size() == 8 && *places.begin() == 1 && *places.rbegin() == 8);
        }
        std::string why;
        CHECK(l.session.GetGame().GetShop().CheckConservation(&why));
        // After the end every action is refused and nothing changes.
        std::string const before = EncodeBgState(l.session);
        CHECK(!l.Send(Make(ActionType::EndTurn)));
        CHECK(!l.Send(Make(ActionType::Buy, 0)));
        CHECK(EncodeBgState(l.session) == before);
        // The combat of every round the human lived through arrived, with the opponent named.
        for (Message const& c : l.combats)
        {
            int const opp = Int(c.kv, "opp");
            CHECK(opp == -1 || (opp >= 0 && opp < 8 && opp / 4 != l.session.Seat() / 4));
        }
    }

    // ------------------------------------------------------------------------------------------ tests

    void ActionRoundTrip()
    {
        std::vector<Action> list;
        list.push_back(Make(ActionType::Buy, 5));
        Action sell = Make(ActionType::Sell, 3);
        sell.zone = Zone::Hand;
        list.push_back(sell);
        sell.zone = Zone::Board;
        list.push_back(sell);
        list.push_back(Make(ActionType::Play, 2, 4));
        list.push_back(Make(ActionType::Play, 2, 4, 123456));
        list.push_back(Make(ActionType::Cast, 2, 0, 123456));
        list.push_back(Make(ActionType::Cast, 4, 0, 0));
        list.push_back(Make(ActionType::Move, 6, 0));
        list.push_back(Make(ActionType::Refresh));
        list.push_back(Make(ActionType::Upgrade));
        list.push_back(Make(ActionType::EndTurn));
        Action fz = Make(ActionType::Freeze);
        fz.flag = true;
        list.push_back(fz);
        fz.flag = false;
        list.push_back(fz);
        list.push_back(Make(ActionType::Discover, 2));
        for (Action const& a : list)
        {
            Action back;
            CHECK(DecodeBgAction(EncodeBgAction(a), back));
            CHECK(back.type == a.type && back.index == a.index && back.slot == a.slot && back.uid == a.uid &&
                  back.flag == a.flag && (a.type != ActionType::Sell || back.zone == a.zone));
        }
    }

    void MalformedActions()
    {
        char const* const bad[] = {
            "", "act=", "act=dance", "i=1", "act=buy", "act=buy;i=x", "act=buy;i=16", "act=buy;i=-1", "act=buy;i=1;i=2",
            "act=buy;i=1;extra=1", "act=sell;i=1", "act=sell;z=x;i=1", "act=sell;z=b", "act=play;i=1", "act=play;s=1",
            "act=play;i=1;s=1;u=abc", "act=play;i=1;s=1;u=1234567890", "act=move;f=1", "act=freeze", "act=freeze;on=2",
            "act=cast", "act=cast;i=1;u=abc", "act=cast;i=1;s=1", "act=discover", "act=refresh;i=1", "act=end;x=1", "act=buy;i=1;", ";act=end", "act=end;;", "act", "=1",
            "act=buy;i=1 ", "act=buy;i=01234567890",
        };
        for (char const* text : bad)
        {
            Action a;
            CHECK(!DecodeBgAction(text, a));
        }
        Action ok;
        CHECK(DecodeBgAction("act=end", ok));
    }

    void RefusedActionsChangeNothing()
    {
        Stats stats;
        Link l(7, &stats);
        l.Start();
        std::string const before = EncodeBgState(l.session);
        Action a = Make(ActionType::Buy, 9); // no such offer
        CHECK(!l.Send(a));
        a = Make(ActionType::Play, 0, 0); // empty hand
        CHECK(!l.Send(a));
        a = Make(ActionType::Upgrade); // 3 gold, the upgrade costs 5
        CHECK(!l.Send(a));
        a = Make(ActionType::Discover, 0); // nothing open
        CHECK(!l.Send(a));
        a = Make(ActionType::Move, 0, 1); // empty board
        CHECK(!l.Send(a));
        Action sell = Make(ActionType::Sell, 0);
        sell.zone = Zone::Hand;
        CHECK(!l.Send(sell));
        CHECK(EncodeBgState(l.session) == before);
        CHECK(stats.refused == 6);

        // The error frame names the reason.
        std::vector<std::string> frames;
        AppendBgActionReply(frames, "12", l.session, ActionResult::NotEnoughGold);
        std::vector<Message> const msgs = Reassemble(frames);
        CHECK(msgs.size() == 1 && msgs[0].op == "ERR" && msgs[0].reqId == "12");
        CHECK(Str(msgs[0].kv, "why") == "NotEnoughGold");
        // A bad request id falls back to the push id.
        frames.clear();
        AppendBgMalformed(frames, "bad id");
        std::vector<Message> const mal = Reassemble(frames);
        CHECK(mal.size() == 1 && mal[0].op == "ERR" && mal[0].reqId == Hs::kServerPushReqId && Str(mal[0].kv, "code") == Hs::ErrorCodeName(Hs::ErrorCode::Malformed));
    }

    void StateShape()
    {
        Stats stats;
        Link l(11, &stats);
        std::vector<std::string> frames;
        AppendBgSnapshot(frames, "5", l.session);
        std::vector<Message> const msgs = Reassemble(frames);
        CHECK(msgs.size() == 1 && msgs[0].op == "BG_STATE" && msgs[0].reqId == "5");
        Message const& m = msgs[0];
        CHECK(Int(m.kv, "me") == 0 && Int(m.kv, "rd") == 1 && Str(m.kv, "ph") == "s" && Int(m.kv, "al") == 1);
        CHECK(Int(m.kv, "g") == 3 && Int(m.kv, "t") == 1 && Int(m.kv, "uc") == 5 && Int(m.kv, "hp") == 40);
        CHECK(Int(m.kv, "cap") == 1 && Int(m.kv, "win") == -1 && Int(m.kv, "fz") == 0 && Int(m.kv, "en") == 0);
        CHECK(ParseMinions(Str(m.kv, "of")).size() == 3);
        CHECK(Str(m.kv, "hd").empty() && Str(m.kv, "bd").empty() && Str(m.kv, "op").empty());
        CHECK(Str(m.kv, "opw").empty());   // no power choice at the start
        CHECK(Str(m.kv, "tm").empty());   // no temporary spell in hand
        CHECK(Split(Str(m.kv, "tb"), ',').size() == 5);
        std::vector<std::string> const lobby = Split(Str(m.kv, "pl"), ',');
        CHECK(lobby.size() == 8);
        for (std::string const& p : lobby)
            CHECK(p == "40.1.1.0.-2..0....");   // hero id + armor, then the quest + reward and the second pair
        // The hero keys are present and empty in a hero-free game.
        CHECK(Str(m.kv, "hc").empty() && Str(m.kv, "hero").empty() && Int(m.kv, "arm") == 0 && Int(m.kv, "mhp") == 40);
        CHECK(Int(m.kv, "rh") == 0);   // nobody rested before the first fight
        CHECK(Str(m.kv, "pw").empty() && Int(m.kv, "pwc") == 0 && Int(m.kv, "pwu") == 0 && Str(m.kv, "pwt") == "n");
        CHECK(Int(m.kv, "snd") == 0 && Str(m.kv, "gift").empty());
        CHECK(Split(Str(m.kv, "nm"), '|').size() == 8);
        // The NPCs already played their turn 1 (they are ended); the human is not.
        CHECK(!l.session.GetGame().TurnEnded(0) && l.session.GetGame().TurnEnded(1));

        // A gift is announced once: in the answer to the action that picked it up, not in a later BG_GET.
        int const card = l.session.Me().offers[0].card;
        l.session.GetGame().GetShop().State(0).gifts.push_back(card);
        Action refresh;
        refresh.type = ActionType::Refresh;
        ActionResult const r = l.session.Apply(refresh);
        CHECK(r == ActionResult::Ok);
        frames.clear();
        AppendBgActionReply(frames, "6", l.session, r);
        std::vector<Message> const reply = Reassemble(frames);
        CHECK(reply.size() == 1 && Str(reply[0].kv, "gift") == CardAt(card).id);
        frames.clear();
        AppendBgSnapshot(frames, "7", l.session);
        std::vector<Message> const again = Reassemble(frames);
        CHECK(again.size() == 1 && Str(again[0].kv, "gift").empty());

        // A Wheel of Yogg-Saron spin is announced once as well: one digit per spin (`whl`), in the answer to that action.
        l.session.GetGame().GetShop().State(0).wheelSpins.push_back(3);
        l.session.GetGame().GetShop().State(0).wheelSpins.push_back(5);
        Action again2;
        again2.type = ActionType::Refresh;
        ActionResult const r2 = l.session.Apply(again2);
        frames.clear();
        AppendBgActionReply(frames, "6b", l.session, r2);
        std::vector<Message> const spun = Reassemble(frames);
        CHECK(spun.size() == 1 && Str(spun[0].kv, "whl") == "35");
        frames.clear();
        AppendBgSnapshot(frames, "7b", l.session);
        std::vector<Message> const spunAgain = Reassemble(frames);
        CHECK(spunAgain.size() == 1 && Str(spunAgain[0].kv, "whl").empty());

        // ofz carries one digit per offer; fz is 1 only when every offer is Frozen.
        frames.clear();
        AppendBgSnapshot(frames, "8", l.session);
        std::vector<Message> const none = Reassemble(frames);
        CHECK(none.size() == 1 && Str(none[0].kv, "ofz") == std::string(l.session.Me().offers.size(), '0') && Int(none[0].kv, "fz") == 0);
        l.session.GetGame().GetShop().FreezeOffer(0, 1);
        frames.clear();
        AppendBgSnapshot(frames, "9", l.session);
        std::vector<Message> const some = Reassemble(frames);
        CHECK(some.size() == 1 && Str(some[0].kv, "ofz") == "010" + std::string(l.session.Me().offers.size() - 3, '0') &&
            Int(some[0].kv, "fz") == 0);
        Action fz;
        fz.type = ActionType::Freeze;
        fz.flag = true;
        CHECK(l.session.Apply(fz) == ActionResult::Ok);
        frames.clear();
        AppendBgSnapshot(frames, "10", l.session);
        std::vector<Message> const all = Reassemble(frames);
        CHECK(all.size() == 1 && Str(all[0].kv, "ofz") == std::string(l.session.Me().offers.size(), '1') && Int(all[0].kv, "fz") == 1);
    }

    // The Secrets in play travel as "sec" (card ids in play order).
    void StateSecrets()
    {
        Stats stats;
        Link l(12, &stats);
        PlayerShop& ps = l.session.GetGame().GetShop().State(0);
        int const first = FindCard("TB_Bacon_Secrets_01");
        int const second = FindCard("TB_Bacon_Secrets_12");
        CHECK(first >= 0 && second >= 0);
        ps.secrets.push_back(first);
        ps.secrets.push_back(second);
        std::vector<std::string> frames;
        AppendBgSnapshot(frames, "7", l.session);
        std::vector<Message> const msgs = Reassemble(frames);
        CHECK(msgs.size() == 1 && msgs[0].op == "BG_STATE");
        CHECK(Str(msgs[0].kv, "sec") == "TB_Bacon_Secrets_01,TB_Bacon_Secrets_12");
    }

    void HeroActionsRoundTrip()
    {
        Action b;
        CHECK(EncodeBgAction(Make(ActionType::Hero, 2)) == "act=hero;i=2");
        CHECK(DecodeBgAction("act=hero;i=2", b) && b.type == ActionType::Hero && b.index == 2);
        Action const p = Make(ActionType::Power, -1, 0, 12345);
        CHECK(DecodeBgAction(EncodeBgAction(p), b) && b.type == ActionType::Power && b.uid == 12345 && b.index == -1);
        Action p3 = Make(ActionType::Power, -1, 0, 12345);
        p3.uid2 = 777;
        CHECK(DecodeBgAction(EncodeBgAction(p3), b) && b.uid == 12345 && b.uid2 == 777);
        Action const p2 = Make(ActionType::Power, 3, 0, 0);
        CHECK(DecodeBgAction(EncodeBgAction(p2), b) && b.index == 3 && b.uid == 0);
        Action const sd = Make(ActionType::Send, 1, 4);
        CHECK(DecodeBgAction(EncodeBgAction(sd), b) && b.type == ActionType::Send && b.index == 1 && b.slot == 4);
        Action bad;
        CHECK(!DecodeBgAction("act=hero", bad));
        CHECK(!DecodeBgAction("act=send;i=1", bad));
        CHECK(!DecodeBgAction("act=send;i=1;to=9", bad));   // seat 9 does not exist
        CHECK(!DecodeBgAction("act=power;o=99", bad));     // no such offer
    }

    void HeroGameThroughTheWire()
    {
        SessionOptions o;
        o.seed = 5;
        o.heroes = true;
        o.factionPools = true;
        o.seatNames = { "", "Alpha", "Beta", "Gamma", "", "Delta", "Epsilon", "Zeta" };
        Session s(o);
        CHECK(s.Picking());
        std::vector<Message> const snap = Reassemble([&] {
            std::vector<std::string> f;
            AppendBgSnapshot(f, "0", s);
            return f;
        }());
        CHECK(snap.size() == 1);
        Message const& m = snap[0];
        CHECK(Str(m.kv, "ph") == "p");
        std::vector<std::string> const hc = Split(Str(m.kv, "hc"), ',');
        CHECK(hc.size() == 4);
        for (std::string const& id : hc)
            CHECK(FindHero(id) >= 0);
        std::vector<std::string> const nm = Split(Str(m.kv, "nm"), '|');
        CHECK(nm.size() == 8 && nm[0].empty() && nm[1] == "Alpha" && nm[7] == "Zeta");
        // Picking the first choice starts round 1 with the hero, its armor and its power on the wire.
        std::vector<std::string> frames;
        AppendBgActionReply(frames, "1", s, s.Apply(Make(ActionType::Hero, 0)));
        CHECK(!s.Picking() && s.GetGame().Round() == 1);
        std::vector<Message> const after = Reassemble([&] {
            std::vector<std::string> f;
            AppendBgSnapshot(f, "2", s);
            return f;
        }());
        Message const& a = after[0];
        CHECK(Str(a.kv, "ph") == "s");
        std::string const myHero = Str(a.kv, "hero");
        int const heroIdx = FindHero(myHero);
        CHECK(heroIdx >= 0);
        CHECK(Int(a.kv, "mhp") == HeroAt(heroIdx).health);
        CHECK(Int(a.kv, "arm") >= 0);
        CHECK(!Str(a.kv, "pw").empty());
        std::string const kind = Str(a.kv, "pwt");
        CHECK(kind == "n" || kind == "f" || kind == "o");
        std::vector<std::string> const lobby = Split(Str(a.kv, "pl"), ',');
        CHECK(lobby.size() == 8);
        std::vector<std::string> const mine = Split(lobby[0], '.');
        CHECK(mine.size() == 11 && mine[5] == myHero);   // the seat entry carries the hero id (and both quest/reward pairs)
    }

    // A hero-power choice on the wire (opw instead of op), the chosen power as pw and the counters.
    void PowerChoiceThroughTheWire()
    {
        SessionOptions o;
        o.seed = 21;
        Session s(o);
        Shop& shop = s.GetGame().GetShop();
        CHECK(!s.Over() && shop.State(0).hero < 0);
        shop.SetHero(0, FindHero("TB_BaconShop_HERO_40"));   // Sir Finley: a choice of 3 powers at game start
        CHECK(shop.DiscoverOpen(0));
        std::vector<Message> const open = Reassemble([&] {
            std::vector<std::string> f;
            AppendBgSnapshot(f, "0", s);
            return f;
        }());
        CHECK(open.size() == 1 && Str(open[0].kv, "op").empty());
        std::vector<std::string> const opts = Split(Str(open[0].kv, "opw"), ',');
        CHECK(opts.size() == 3);
        for (std::string const& id : opts)
            CHECK(shop.FindPower(id) != nullptr);
        Action pick;
        pick.type = ActionType::Discover;
        pick.index = 1;
        CHECK(s.Apply(pick) == ActionResult::Ok);
        CHECK(!shop.DiscoverOpen(0) && shop.State(0).power.powerOverride == opts[1]);
        std::vector<Message> const after = Reassemble([&] {
            std::vector<std::string> f;
            AppendBgSnapshot(f, "1", s);
            return f;
        }());
        CHECK(after.size() == 1 && Str(after[0].kv, "pw") == opts[1] && Str(after[0].kv, "opw").empty());

        // Ragnaros: the power button counts the 25 kills left, then the power becomes Sulfuras.
        shop.SetHero(0, FindHero("TB_BaconShop_HERO_11"));
        std::vector<Message> const ragnaros = Reassemble([&] {
            std::vector<std::string> f;
            AppendBgSnapshot(f, "2", s);
            return f;
        }());
        CHECK(Str(ragnaros[0].kv, "pw") == "TB_BaconShop_HP_087" && Int(ragnaros[0].kv, "pwx") == 25);
        shop.CountEnemyKills(0, 5);
        std::vector<Message> const counted = Reassemble([&] {
            std::vector<std::string> f;
            AppendBgSnapshot(f, "3", s);
            return f;
        }());
        CHECK(Int(counted[0].kv, "pwx") == 20);
        shop.CountEnemyKills(0, 20);
        std::vector<Message> const sulfuras = Reassemble([&] {
            std::vector<std::string> f;
            AppendBgSnapshot(f, "4", s);
            return f;
        }());
        CHECK(Str(sulfuras[0].kv, "pw") == "TB_BaconShop_HP_087t" && Int(sulfuras[0].kv, "pwx") == 0);

        // Tickatus's Prize Wall counter, the prize Discover (spell options) and the buy price field.
        shop.SetHero(0, FindHero("TB_BaconShop_HERO_94"));
        shop.StartTurn(0, 4);
        std::vector<Message> const tickatus = Reassemble([&] {
            std::vector<std::string> f;
            AppendBgSnapshot(f, "5", s);
            return f;
        }());
        CHECK(Str(tickatus[0].kv, "pw") == "TB_BaconShop_HP_106");
        CHECK(Int(tickatus[0].kv, "pwx") == DarkmoonPrizeTurnsLeft(s.GetGame().Round(), shop.State(0).power.prizesTaken));
        CHECK(Int(tickatus[0].kv, "bc") == 3);
        std::vector<Message> const prizes = Reassemble([&] {
            std::vector<std::string> f;
            AppendBgSnapshot(f, "6", s);
            return f;
        }());
        std::vector<std::string> const options = Split(Str(prizes[0].kv, "op"), ',');
        CHECK(options.size() == 3);
        for (std::string const& m : options)
        {
            std::vector<std::string> const parts = Split(m, '.');
            CHECK(!parts.empty() && DarkmoonPrizeTurnOf(FindCard(parts[0])) == 1);
        }
        Action prizePick;
        prizePick.type = ActionType::Discover;
        prizePick.index = 0;
        CHECK(s.Apply(prizePick) == ActionResult::Ok);
        CHECK(shop.State(0).hand.Size() == 1);
    }

    void SessionPlacementsAndOptions()
    {
        // The human can sit on another seat; the wire names it.
        SessionOptions o;
        o.seed = 3;
        o.seat = 5;
        Session s(o);
        CHECK(s.Seat() == 5 && s.GetGame().Kind(5) == PlayerKind::External && s.GetGame().Kind(0) == PlayerKind::Npc);
        std::vector<Message> const msgs = Reassemble([&] {
            std::vector<std::string> f;
            AppendBgSnapshot(f, "0", s);
            return f;
        }());
        CHECK(msgs.size() == 1 && Int(msgs[0].kv, "me") == 5);
        // An out-of-range seat falls back to 0.
        o.seat = 99;
        Session t(o);
        CHECK(t.Seat() == 0);
    }

    void FullGamesThroughTheWire()
    {
        Stats stats;
        for (uint32_t seed = 1; seed <= 60; ++seed)
            PlayGame(seed * 7919u, 0, stats);
        std::printf("  active clients: %d games, %d actions (%d refused), %d fights (%d replayed, %d truncated), %d end frames, %d won\n",
            stats.games, stats.actions, stats.refused, stats.combats, stats.replayed, stats.truncated, stats.ends, stats.won);
        CHECK(stats.games == 60 && stats.ends == 60);
        CHECK(stats.combats > 200 && stats.replayed > 200);
        // The test client may be refused now and then (a Magnetic target, a full board), never often.
        CHECK(stats.refused * 10 < stats.actions);
    }

    void PassiveClientIsEliminated()
    {
        Stats stats;
        for (uint32_t seed = 1; seed <= 20; ++seed)
            PlayGame(seed * 104729u, 1, stats);
        CHECK(stats.games == 20 && stats.ends == 20);
        // Nobody buys anything: the human's faction loses nearly always, and the game ends within the safety net.
        CHECK(stats.won < 20);
    }

    void EliminatedHumanSeesTheEnd()
    {
        // A passive human dies; the rest of the game is played out at once and the end frames follow that very action.
        for (uint32_t seed = 1; seed <= 30; ++seed)
        {
            Stats stats;
            Link l(seed, &stats);
            l.Start();
            bool diedOnAction = false;
            for (int i = 0; i < 200 && !l.cs.over; ++i)
            {
                bool const wasAlive = l.cs.alive;
                l.Send(Make(ActionType::EndTurn));
                if (wasAlive && !l.cs.alive)
                    diedOnAction = true;
            }
            CHECK(l.cs.over && l.ends.size() == 1);
            if (diedOnAction)
            {
                CHECK(Int(l.ends[0].kv, "al") == 0);
                CHECK(l.session.GetGame().Over());
                CHECK(l.session.OverallPlacement(l.session.Seat()) >= 1);
            }
        }
    }

    void LongCombatLogIsTruncated()
    {
        // A log that cannot fit the frame limit is left out, but the boards and the result stay.
        Stats stats;
        Link l(21, &stats);
        l.Start();
        for (int i = 0; i < 6 && !l.cs.over; ++i)
            PlayTurn(l, 0);
        CHECK(!l.combats.empty());
        Message const& c = l.combats.back();
        CHECK(c.kv.count("s0") && c.kv.count("s1") && c.kv.count("v0") && c.kv.count("v1") && c.kv.count("w") && c.kv.count("dm"));
        // Direct check of the size fallback: the payload of any real fight is far below the cap, so the encoder keeps
        // the events; a payload of 100 parts is refused by the codec.
        std::string big(30000, 'a');
        std::vector<std::string> frames;
        CHECK(!Hs::ChunkPayload("BG_COMBAT", "0", big, frames));
        CHECK(Int(c.kv, "tr") == 0 || !c.kv.count("ev"));
    }

    void SeatNames()
    {
        // Seven distinct seat names, of the right faction, deterministic in the seed.
        std::vector<NamePoolEntry> pool = {
            { "a1", NameSide::Alliance }, { "a2", NameSide::Alliance }, { "a3", NameSide::Alliance },
            { "a4", NameSide::Alliance }, { "a5", NameSide::Alliance },
            { "h1", NameSide::Horde }, { "h2", NameSide::Horde }, { "h3", NameSide::Horde },
            { "h4", NameSide::Horde }, { "h5", NameSide::Horde },
            { "n1", NameSide::Any }, { "n2", NameSide::Any },
        };
        for (int alliance = 0; alliance < 2; ++alliance)
        {
            std::vector<std::string> const names = PickSeatNames(pool, alliance != 0, 424242);
            CHECK(names.size() == 7);
            std::set<std::string> const distinct(names.begin(), names.end());
            CHECK(distinct.size() == 7);
            for (std::string const& n : names)
                CHECK(!n.empty());
            // The first three are the human's own faction, the last four the other one (never a wrong-faction pool name).
            char const own = alliance ? 'a' : 'h';
            char const foe = alliance ? 'h' : 'a';
            for (int i = 0; i < 3; ++i)
                CHECK(names[i][0] == own || names[i][0] == 'n');
            for (int i = 3; i < 7; ++i)
                CHECK(names[i][0] == foe || names[i][0] == 'n');
            // Same seed and faction: the same seven names.
            CHECK(PickSeatNames(pool, alliance != 0, 424242) == names);
        }
        // An `any`-only pool fills both sides; a completely empty pool falls back to the generic names.
        std::vector<NamePoolEntry> anyOnly;
        for (int i = 0; i < 9; ++i)
            anyOnly.push_back({ "any" + std::to_string(i), NameSide::Any });
        std::vector<std::string> const anys = PickSeatNames(anyOnly, true, 7);
        CHECK(anys.size() == 7 && std::set<std::string>(anys.begin(), anys.end()).size() == 7);
        std::vector<std::string> const generic = PickSeatNames({}, false, 7);
        CHECK(generic.size() == 7 && std::set<std::string>(generic.begin(), generic.end()).size() == 7);
        // A pool too small for a side is topped up with that side's generic names (never a blank seat).
        std::vector<NamePoolEntry> tiny = { { "onlyAlliance", NameSide::Alliance } };
        std::vector<std::string> const filled = PickSeatNames(tiny, true, 7);
        CHECK(filled.size() == 7 && filled[0] == "onlyAlliance");
        std::set<std::string> const gen2(filled.begin(), filled.end());
        CHECK(gen2.size() == 7);
        std::vector<std::string> const hordeGeneric = GenericSeatNames(false);
        CHECK(!hordeGeneric.empty() && std::find(hordeGeneric.begin(), hordeGeneric.end(), filled[3]) != hordeGeneric.end());
        // A name the addon cannot take (too long, or with the '|' separator) is skipped.
        std::vector<NamePoolEntry> bad = { { std::string(kMaxSeatName + 1, 'x'), NameSide::Alliance },
            { "a|b", NameSide::Alliance }, { std::string(kMaxSeatName, 'y'), NameSide::Alliance } };
        std::vector<std::string> const kept = PickSeatNames(bad, true, 7);
        CHECK(kept.size() == 7 && kept[0] == std::string(kMaxSeatName, 'y'));
        for (std::string const& n : kept)
            CHECK(n.size() <= kMaxSeatName && n.find('|') == std::string::npos);
    }

    void CloseAndSnapshot()
    {
        std::vector<std::string> frames;
        AppendBgClose(frames);
        std::vector<Message> const msgs = Reassemble(frames);
        CHECK(msgs.size() == 1 && msgs[0].op == "BG_CLOSE" && msgs[0].reqId == Hs::kServerPushReqId && msgs[0].kv.empty());
    }

    void Determinism()
    {
        // The same seed and the same actions give the same wire text.
        std::vector<std::string> a;
        std::vector<std::string> b;
        for (int run = 0; run < 2; ++run)
        {
            Stats stats;
            Link l(99, &stats);
            std::vector<std::string>& out = run == 0 ? a : b;
            out.push_back(EncodeBgState(l.session));
            l.Start();
            for (int i = 0; i < 4; ++i)
            {
                PlayTurn(l, 0);
                out.push_back(EncodeBgState(l.session));
                if (l.session.HasFight())
                    out.push_back(EncodeBgCombat(l.session));
            }
        }
        CHECK(a == b && a.size() >= 5);
    }
}

    void QuestThroughTheWire()
    {
        SessionOptions o;
        o.seed = 77;
        o.quests = true;
        Session s(o);
        ActionResult r = ActionResult::Ok;
        for (int i = 0; i < 3 && r == ActionResult::Ok; ++i)
            r = s.Apply(Action{ ActionType::EndTurn });
        CHECK(r == ActionResult::Ok && s.HumanAlive());
        auto state = [](Session const& sess) {
            std::vector<std::string> f;
            AppendBgSnapshot(f, "0", sess);
            return Reassemble(f);
        };
        std::vector<Message> const a = state(s);
        CHECK(!a.empty() && a[0].op == "BG_STATE");
        CHECK(Str(a[0].kv, "ph") == "q");                 // the quest pick is open at turn 4
        CHECK(Split(Str(a[0].kv, "qo"), '|').size() == 3);   // three pairs
        std::vector<QuestTypes> const& offered = s.Me().quest.offerTypes;
        {
            std::vector<std::string> const pairs = Split(Str(a[0].kv, "qo"), '|');
            for (std::size_t i = 0; i < pairs.size() && i < offered.size(); ++i)
            {
                // questId:rewardId:typeA:typeB:rewardType, the types as tribe bits (0 = none)
                std::vector<std::string> const f = Split(pairs[i], ':');
                CHECK(f.size() == 5);
                if (f.size() == 5)
                {
                    CHECK(f[2] == std::to_string(offered[i].typeA));
                    CHECK(f[3] == std::to_string(offered[i].typeB));
                    CHECK(f[4] == std::to_string(offered[i].rewardType));
                }
            }
        }
        QuestTypes const chosen = offered.size() > 1 ? offered[1] : QuestTypes{};
        Action pick;
        pick.type = ActionType::Quest;
        pick.index = 1;
        Action back;
        CHECK(DecodeBgAction(EncodeBgAction(pick), back) && back.type == ActionType::Quest && back.index == 1);
        CHECK(s.Apply(pick) == ActionResult::Ok);
        std::vector<Message> const b = state(s);
        CHECK(Str(b[0].kv, "ph") != "q");
        CHECK(!Str(b[0].kv, "q").empty());
        CHECK(Int(b[0].kv, "qr") > 0);
        CHECK(Str(b[0].kv, "rw").empty());                  // the reward is not earned yet
        CHECK(Int(b[0].kv, "qa") == static_cast<int>(chosen.typeA));   // the taken quest's types
        CHECK(Int(b[0].kv, "qb") == static_cast<int>(chosen.typeB));
        CHECK(Int(b[0].kv, "rt") == static_cast<int>(chosen.rewardType));
        std::vector<std::string> const lobby = Split(Str(b[0].kv, "pl"), ',');
        CHECK(lobby.size() == 8);
        CHECK(Split(lobby[0], '.').size() == 11);             // the quest fields + the four extra seat fields
        // Play the game out: BG_END carries the payout keys.
        int guard = 0;
        while (!s.Over() && guard++ < 400)
            s.Apply(Action{ ActionType::EndTurn });
        CHECK(s.Over());
        std::vector<Message> const e = state(s);
        CHECK(!e.empty() && e.back().op == "BG_END");
        // The coins are totals, never added: first place 7, else the winning faction 5, else the base 3; no pack.
        int const expected = s.OverallPlacement(0) == 1 ? 7 : (s.HumanFactionWon() ? 5 : 3);
        CHECK(Int(e.back().kv, "rc") == expected && Int(e.back().kv, "rp") == 0);
        CHECK(s.PayoutCoins() == expected && s.PayoutPacks() == 0);
        // The honor has the same shape (70 first, 50 winning faction, 10 base) and the record carries the game's stats.
        CHECK(s.PayoutHonor() == (s.OverallPlacement(0) == 1 ? 70 : (s.HumanFactionWon() ? 50 : 10)));
        Hs::Bg::GameRecord const rec = s.Record();
        CHECK(rec.placement == s.OverallPlacement(0) && rec.teamWon == s.HumanFactionWon() && rec.tier >= 1 && rec.tier <= 6);
    }

    // The shared need score: a pair completes a triple, a blocked send scores 0 and a minion
    // that beats the ally's weakest board minion is an upgrade.
    void NeedScores()
    {
        Stats stats;
        Link l(31, &stats);
        Shop& shop = l.session.GetGame().GetShop();
        PlayerShop& ally = shop.State(1);
        std::vector<int> const tier1 = CardsOfTier(1);
        std::vector<int> const tier6 = CardsOfTier(6);
        CHECK(tier1.size() >= 1 && tier6.size() >= 1);
        int const card = tier1[0];
        int const other = tier1.size() >= 2 ? tier1[tier1.size() - 1] : tier1[0];
        ally.board.Clear();
        ally.hand.Clear();
        ally.health = 30;
        // Two copies on the ally's board: the third one completes the triple.
        ally.board.Push(MakeMinion(card));
        ally.board.Push(MakeMinion(card));
        Need n = Ai::NeedScore(shop, 1, MakeMinion(card), 0);
        CHECK((n.reasons & kNeedTriple) != 0 && n.score >= kSendNeedMin);
        // A golden copy never completes a normal triple.
        n = Ai::NeedScore(shop, 1, MakeMinion(card, true), 0);
        CHECK((n.reasons & kNeedTriple) == 0);
        // Blockers: a dead ally, another faction, the sender itself and a full hand score 0.
        ally.health = 0;
        CHECK(Ai::NeedScore(shop, 1, MakeMinion(card), 0).score == 0);
        ally.health = 30;
        CHECK(Ai::NeedScore(shop, 4, MakeMinion(card), 0).score == 0);
        CHECK(Ai::NeedScore(shop, 0, MakeMinion(card), 0).score == 0);
        ally.board.Clear();
        ally.hand.Clear();
        for (int i = 0; i < kHandMax; ++i)
            ally.hand.Push(MakeMinion(other));
        CHECK(Ai::NeedScore(shop, 1, MakeMinion(card), 0).score == 0);
        // A tier 6 minion beats a board of tier 1 minions: a clear upgrade.
        ally.hand.Clear();
        ally.board.Clear();
        for (int i = 0; i < kBoardMax; ++i)
            ally.board.Push(MakeMinion(other));
        n = Ai::NeedScore(shop, 1, MakeMinion(tier6[0]), 0);
        CHECK((n.reasons & kNeedUpgrade) != 0);
    }

    // The sell price of a card travels as svh / svb (Freedealing Gambler pays more, a spell pays 0).
    void SellPricesOnTheWire()
    {
        Stats stats;
        Link l(51, &stats);
        Shop& shop = l.session.GetGame().GetShop();
        PlayerShop& me = shop.State(0);
        int const gambler = FindCard("BGS_049");
        int const spell = FindCard("BG20_GEM");
        CHECK(gambler >= 0 && spell >= 0);
        me.hand.Clear();
        me.board.Clear();
        me.hand.Push(MakeMinion(gambler));
        me.hand.Push(MakeMinion(spell));
        me.board.Push(MakeMinion(gambler, true));
        std::vector<Message> const msgs = Reassemble([&] {
            std::vector<std::string> f;
            AppendBgSnapshot(f, "1", l.session);
            return f;
        }());
        CHECK(msgs.size() == 1 && msgs[0].op == "BG_STATE");
        CHECK(Str(msgs[0].kv, "svh") == "3,0");
        CHECK(Str(msgs[0].kv, "svb") == "6");
        // rk: every seat's live placing, the server's own count
        std::vector<std::string> const rk = Split(Str(msgs[0].kv, "rk"), ',');
        CHECK(rk.size() == static_cast<std::size_t>(kPlayers));
        for (int p = 0; p < kPlayers && p < static_cast<int>(rk.size()); ++p)
            CHECK(std::atoi(rk[static_cast<std::size_t>(p)].c_str()) == l.session.OverallPlacement(p));
        CHECK(shop.SellGold(MakeMinion(spell)) == 0 && shop.SellGold(MakeMinion(gambler)) == 3);
    }

    // `nd` names the allies a hand card helps, and BG_ACT peek answers with BG_ALLY for a living ally only.
    void AllyNeedAndPeekWire()
    {
        Stats stats;
        Link l(41, &stats);
        Shop& shop = l.session.GetGame().GetShop();
        PlayerShop& me = shop.State(0);
        PlayerShop& ally = shop.State(1);
        std::vector<int> const tier1 = CardsOfTier(1);
        CHECK(tier1.size() >= 1);
        int const card = tier1[0];
        uint32_t const uid = shop.NewUid();
        me.hand.Clear();
        me.hand.Push(MakeMinion(card, false, uid));
        me.sentThisTurn = false;
        ally.board.Clear();
        ally.hand.Clear();
        ally.health = 30;
        ally.board.Push(MakeMinion(card));
        ally.board.Push(MakeMinion(card));

        std::vector<Message> msgs = Reassemble([&] {
            std::vector<std::string> f;
            AppendBgSnapshot(f, "1", l.session);
            return f;
        }());
        CHECK(msgs.size() == 1 && msgs[0].op == "BG_STATE");
        std::string const nd = Str(msgs[0].kv, "nd");
        CHECK(nd.find(std::to_string(uid) + ".1T") != std::string::npos);
        // Once the send is used the hint is empty.
        me.sentThisTurn = true;
        msgs = Reassemble([&] {
            std::vector<std::string> f;
            AppendBgSnapshot(f, "2", l.session);
            return f;
        }());
        CHECK(Str(msgs[0].kv, "nd").empty());
        me.sentThisTurn = false;

        // A peek at a living ally answers with its field; an enemy or the human's own seat is refused.
        Action const peek = Make(ActionType::Peek, 0, 1);
        ActionResult const r = l.session.Apply(peek);
        CHECK(r == ActionResult::Ok);
        msgs = Reassemble([&] {
            std::vector<std::string> f;
            AppendBgActionReply(f, "9", l.session, r, &peek);
            return f;
        }());
        CHECK(msgs.size() == 1 && msgs[0].op == "BG_ALLY" && msgs[0].reqId == "9");
        CHECK(Int(msgs[0].kv, "se") == 1 && ParseMinions(Str(msgs[0].kv, "bd")).size() == 2);
        CHECK(l.session.Apply(Make(ActionType::Peek, 0, 4)) == ActionResult::NotAlly);
        CHECK(l.session.Apply(Make(ActionType::Peek, 0, 0)) == ActionResult::NotAlly);
        // The action round-trips through the text form.
        Action back;
        CHECK(DecodeBgAction(EncodeBgAction(peek), back));
        CHECK(back.type == ActionType::Peek && back.slot == 1);
    }

int main(int, char**)
{
    RUN(ActionRoundTrip);
    RUN(MalformedActions);
    RUN(RefusedActionsChangeNothing);
    RUN(StateShape);
    RUN(StateSecrets);
    RUN(NeedScores);
    RUN(AllyNeedAndPeekWire);
    RUN(SellPricesOnTheWire);
    RUN(HeroActionsRoundTrip);
    RUN(HeroGameThroughTheWire);
    RUN(PowerChoiceThroughTheWire);
    RUN(QuestThroughTheWire);
    RUN(SessionPlacementsAndOptions);
    RUN(FullGamesThroughTheWire);
    RUN(PassiveClientIsEliminated);
    RUN(EliminatedHumanSeesTheEnd);
    RUN(LongCombatLogIsTruncated);
    RUN(SeatNames);
    RUN(CloseAndSnapshot);
    RUN(Determinism);
    std::printf("bg_wire_tests: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
