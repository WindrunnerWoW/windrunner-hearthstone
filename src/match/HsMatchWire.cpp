#include "HsMatchWire.h"

#include "../protocol/HsProtocol.h"

#include <algorithm>
#include <map>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

namespace Hs
{
    namespace
    {
        using Match::EntityId;

        std::string Num(long long v)
        {
            return std::to_string(v);
        }

        std::string Join(std::vector<std::string> const& parts, char sep)
        {
            std::string out;
            for (std::size_t i = 0; i < parts.size(); ++i)
            {
                if (i)
                    out.push_back(sep);
                out += parts[i];
            }
            return out;
        }

        // Minion flag letters in the order t c d w s f x p e i n a ("-" when none): Taunt, Charge,
        // Divine Shield, Windfury, Stealth, frozen, silenced, Poisonous, enraged, immune, can't attack,
        // can attack now.
        std::string MinionFlags(Match::MinionView const& m)
        {
            std::string flags;
            auto add = [&flags](bool on, char letter)
            {
                if (on)
                    flags.push_back(letter);
            };
            add(m.taunt, 't');
            add(m.charge, 'c');
            add(m.divineShield, 'd');
            add(m.windfury, 'w');
            add(m.stealth, 's');
            add(m.frozen, 'f');
            add(m.silenced, 'x');
            add(m.poisonous, 'p');
            add(m.enraged, 'e');
            add(m.immune, 'i');
            add(m.cantAttack, 'n');
            add(m.canAttack, 'a');
            return flags.empty() ? std::string("-") : flags;
        }

        // Hero flag letters in the order f i a ("-" when none): frozen, immune, can attack.
        std::string HeroFlags(Match::HeroView const& h)
        {
            std::string flags;
            if (h.frozen)
                flags.push_back('f');
            if (h.immune)
                flags.push_back('i');
            if (h.canAttack)
                flags.push_back('a');
            return flags.empty() ? std::string("-") : flags;
        }

        // <id>.<cardId>.<health>.<maxHealth>.<armor>.<attack>.<flags>
        std::string HeroText(Match::HeroView const& h)
        {
            return Num(h.id) + "." + h.cardId + "." + Num(h.health) + "." + Num(h.maxHealth) + "." + Num(h.armor) + "." +
                Num(h.attack) + "." + HeroFlags(h);
        }

        // <id>.<cardId>.<cost>.<used 0|1>
        std::string PowerText(Match::HeroView const& h)
        {
            return Num(h.powerId) + "." + h.powerCardId + "." + Num(h.powerCost) + "." + (h.powerUsed ? "1" : "0");
        }

        // <id>.<cardId>.<attack>.<durability>; empty without a weapon
        std::string WeaponText(Match::HeroView const& h)
        {
            if (h.weaponId == 0)
                return std::string();
            return Num(h.weaponId) + "." + h.weaponCardId + "." + Num(h.weaponAttack) + "." + Num(h.weaponDurability);
        }

        std::string ManaText(Match::HeroView const& h)
        {
            return Num(h.mana) + "." + Num(h.maxMana);
        }

        std::string CountsText(Match::SideView const& s)
        {
            return Num(s.handCount) + "." + Num(s.deckCount) + "." + Num(s.hero.fatigue);
        }

        // <id>.<cardId>.<attack>.<health>.<maxHealth>.<flags>, left to right
        std::string BoardText(Match::SideView const& s)
        {
            std::vector<std::string> parts;
            for (Match::MinionView const& m : s.board)
                parts.push_back(Num(m.id) + "." + m.cardId + "." + Num(m.attack) + "." + Num(m.health) + "." +
                    Num(m.maxHealth) + "." + MinionFlags(m));
            return Join(parts, ',');
        }

        // <id>.<cardId>.<cost>.<attack>.<health>, in hand order
        std::string HandText(Match::SideView const& s)
        {
            std::vector<std::string> parts;
            for (Match::HandCardView const& c : s.hand)
                parts.push_back(Num(c.id) + "." + c.cardId + "." + Num(c.cost) + "." + Num(c.attack) + "." + Num(c.health));
            return Join(parts, ',');
        }

        // mo=<handId>.<option1>/<option2>,...: the Choose One options of the viewer's hand cards, in hand order.
        std::string OptionsText(Match::SideView const& s)
        {
            std::vector<std::string> parts;
            for (Match::HandCardView const& c : s.hand)
                if (!c.options.empty())
                    parts.push_back(Num(c.id) + "." + Join(c.options, '/'));
            return Join(parts, ',');
        }

        // ms=<id>.<cardId>,...: the viewer's own secrets, in play order.
        std::string OwnSecretsText(Match::SideView const& s)
        {
            std::vector<std::string> parts;
            for (Match::SecretView const& x : s.secrets)
                parts.push_back(Num(x.id) + "." + x.cardId);
            return Join(parts, ',');
        }

        // os=<id>,...: the other side's secrets (ids only: the cards stay hidden).
        std::string OtherSecretsText(Match::SideView const& s)
        {
            std::vector<std::string> parts;
            for (Match::SecretView const& x : s.secrets)
                parts.push_back(Num(x.id));
            return Join(parts, ',');
        }

        // <cardsPlayedThisTurn>.<overloadLocked>.<overloadOwed>
        std::string TurnText(Match::SideView const& s)
        {
            return Num(s.cardsPlayed) + "." + Num(s.overloadLocked) + "." + Num(s.overloadOwed);
        }

        // pk=<id>.<cardId>,...: the viewer's pending pick options (empty without a pending pick).
        std::string PickText(Match::MatchView const& v)
        {
            std::vector<std::string> parts;
            if (v.pickPending)
                for (Match::PickView const& p : v.pick)
                    parts.push_back(Num(p.id) + "." + p.cardId);
            return Join(parts, ',');
        }

        // lg=<entry>,...  entry = <k><source>[:<choice>].<t1>/<t2>/...  Order: plays in hand order (a Choose
        // One card has one entry per option, in option order), attacks (hero first, then minions left to
        // right), the hero power; targets ascending.
        std::string LegalText(Match::MatchView const& v)
        {
            std::map<EntityId, std::size_t> handRank;
            for (std::size_t i = 0; i < v.self.hand.size(); ++i)
                handRank[v.self.hand[i].id] = i;
            std::map<EntityId, std::size_t> attackerRank;
            attackerRank[v.self.hero.id] = 0;
            for (std::size_t i = 0; i < v.self.board.size(); ++i)
                attackerRank[v.self.board[i].id] = i + 1;

            struct Group
            {
                char kind;
                EntityId source;
                int choice;
                std::size_t rank;
                std::set<EntityId> targets;
            };
            std::map<std::tuple<char, EntityId, int>, Group> groups;
            for (Match::Action const& a : v.legal)
            {
                char kind = 0;
                std::size_t rank = 0;
                switch (a.type)
                {
                    case Match::ActionType::Play:
                    {
                        kind = 'p';
                        auto const it = handRank.find(a.source);
                        rank = it == handRank.end() ? handRank.size() : it->second;
                        break;
                    }
                    case Match::ActionType::Attack:
                    {
                        kind = 'a';
                        auto const it = attackerRank.find(a.source);
                        rank = it == attackerRank.end() ? attackerRank.size() : it->second;
                        break;
                    }
                    case Match::ActionType::HeroPower:
                        kind = 'h';
                        break;
                    default:
                        continue;
                }
                int const choice = kind == 'p' ? a.choice : 0;
                Group& g = groups[std::make_tuple(kind, a.source, choice)];
                g.kind = kind;
                g.source = a.source;
                g.choice = choice;
                g.rank = rank;
                g.targets.insert(a.target);
            }

            std::vector<Group> ordered;
            for (auto const& entry : groups)
                ordered.push_back(entry.second);
            auto kindOrder = [](char k) { return k == 'p' ? 0 : (k == 'a' ? 1 : 2); };
            std::sort(ordered.begin(), ordered.end(), [&](Group const& x, Group const& y)
            {
                if (kindOrder(x.kind) != kindOrder(y.kind))
                    return kindOrder(x.kind) < kindOrder(y.kind);
                if (x.rank != y.rank)
                    return x.rank < y.rank;
                if (x.source != y.source)
                    return x.source < y.source;
                return x.choice < y.choice;
            });

            std::vector<std::string> parts;
            for (Group const& g : ordered)
            {
                std::string entry;
                entry.push_back(g.kind);
                entry += Num(g.source);
                if (g.choice != 0)
                    entry += ":" + Num(g.choice);
                entry += ".";
                bool first = true;
                for (EntityId t : g.targets)
                {
                    if (!first)
                        entry.push_back('/');
                    first = false;
                    entry += Num(t);
                }
                parts.push_back(entry);
            }
            return Join(parts, ',');
        }

        // <type>.<side m|o|->.<source>.<target>.<amount>.<cardId or ->
        std::string EventText(Match::Event const& e, int viewer)
        {
            char side = '-';
            if (e.side >= 0)
                side = e.side == viewer ? 'm' : 'o';
            return std::string(Match::EventTypeCode(e.type)) + "." + side + "." + Num(e.source) + "." + Num(e.target) + "." +
                Num(e.amount) + "." + (e.cardId.empty() ? std::string("-") : e.cardId);
        }

        // Decimal digits only, 1..10 of them, within 32 bits.
        bool ParseU32(std::string const& s, uint32_t& out)
        {
            if (s.empty() || s.size() > 10)
                return false;
            unsigned long long v = 0;
            for (char c : s)
            {
                if (c < '0' || c > '9')
                    return false;
                v = v * 10 + static_cast<unsigned long long>(c - '0');
            }
            if (v > 0xFFFFFFFFull)
                return false;
            out = static_cast<uint32_t>(v);
            return true;
        }

        // r=<id>,<id>,...; the empty string is the empty list.
        bool ParseIdList(std::string const& s, std::vector<EntityId>& out)
        {
            out.clear();
            if (s.empty())
                return true;
            std::size_t start = 0;
            while (true)
            {
                std::size_t const comma = s.find(',', start);
                std::string const part = s.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
                uint32_t id = 0;
                if (!ParseU32(part, id) || out.size() >= kMaxMulliganIds)
                    return false;
                out.push_back(id);
                if (comma == std::string::npos)
                    return true;
                start = comma + 1;
            }
        }
    }

    std::string EncodeGameNone()
    {
        KvList kv;
        kv.push_back(std::make_pair("ph", std::string("none")));
        return KvEncode(kv);
    }

    std::string GameResultCode(Match::MatchView const& view)
    {
        if (!view.result.over)
            return "-";
        if (view.result.reason == Match::EndReason::Aborted)
            return "int";
        if (view.result.winner < 0)
            return "draw";
        return view.result.winner == view.viewer ? "win" : "loss";
    }

    std::string GameWhyCode(Match::MatchView const& view, std::string const& abortWhy)
    {
        if (!view.result.over)
            return "-";
        switch (view.result.reason)
        {
            case Match::EndReason::HeroDied:
                return "hero";
            case Match::EndReason::Concede:
                return "concede";
            case Match::EndReason::TurnLimit:
                return "turns";
            case Match::EndReason::Aborted:
                return abortWhy.empty() ? std::string("error") : abortWhy;
            case Match::EndReason::None:
                break;
        }
        return "-";
    }

    std::string EncodeGame(GameHeader const& header, Match::MatchView const& v)
    {
        char const* phase = "play";
        switch (v.phase)
        {
            case Match::Phase::Mulligan:
                phase = "mull";
                break;
            case Match::Phase::Play:
                phase = "play";
                break;
            case Match::Phase::Over:
                phase = "over";
                break;
        }
        bool const over = v.phase == Match::Phase::Over || v.result.over;
        if (over)
            phase = "over";
        char act = '-';
        if (!over && v.phase == Match::Phase::Play)
            act = v.activeSide == v.viewer ? 'm' : 'o';

        std::vector<std::string> mulligan;
        if (v.mulliganPending)
            for (EntityId id : v.mulligan)
                mulligan.push_back(Num(id));

        std::size_t const firstEvent = v.events.size() > kMaxGameEvents ? v.events.size() - kMaxGameEvents : 0;
        std::vector<std::string> events;
        for (std::size_t i = firstEvent; i < v.events.size(); ++i)
            events.push_back(EventText(v.events[i], v.viewer));
        uint32_t const firstSeq = events.empty() ? 0 : v.events[firstEvent].seq;

        Match::SideView const& me = v.self;
        Match::SideView const& op = v.other;

        KvList kv;
        auto add = [&kv](char const* key, std::string const& value) { kv.push_back(std::make_pair(std::string(key), value)); };
        add("mid", Num(header.matchId));
        add("seq", Num(v.seq));
        add("ph", phase);
        add("turn", Num(v.turn));
        add("act", std::string(1, act));
        add("res", GameResultCode(v));
        add("why", GameWhyCode(v, header.abortWhy));
        add("mn", header.myName);
        add("on", header.opponentName);
        add("dif", header.difficulty.empty() ? std::string("normal") : header.difficulty);
        if (header.durable)
            add("dur", "1");
        add("mh", HeroText(me.hero));
        add("mp", PowerText(me.hero));
        add("mw", WeaponText(me.hero));
        add("mm", ManaText(me.hero));
        add("mk", CountsText(me));
        add("mb", BoardText(me));
        add("mc", HandText(me));
        add("mo", OptionsText(me));
        add("ms", OwnSecretsText(me));
        add("mx", TurnText(me));
        add("oh", HeroText(op.hero));
        add("op", PowerText(op.hero));
        add("ow", WeaponText(op.hero));
        add("om", ManaText(op.hero));
        add("ok", CountsText(op));
        add("ob", BoardText(op));
        add("os", OtherSecretsText(op));
        add("ox", TurnText(op));
        add("mul", Join(mulligan, ','));
        add("pk", PickText(v));
        add("opk", v.opponentPicking ? "1" : "0");
        add("tmr", Num(header.timerSec < 0 ? 0 : header.timerSec));
        add("lg", LegalText(v));
        add("evs", Num(firstSeq));
        add("ev", Join(events, ','));
        return KvEncode(kv);
    }

    bool DecodeGameAct(std::string const& payload, GameActRequest& out)
    {
        std::map<std::string, std::string> kv;
        if (!KvDecode(payload, kv))
            return false;
        for (auto const& entry : kv)
        {
            std::string const& k = entry.first;
            if (k != "mid" && k != "seq" && k != "a" && k != "s" && k != "t" && k != "p" && k != "r" && k != "c")
                return false;
        }

        GameActRequest req;
        auto const mid = kv.find("mid");
        auto const act = kv.find("a");
        if (mid == kv.end() || act == kv.end() || !ParseU32(mid->second, req.matchId))
            return false;

        std::string const& a = act->second;
        Match::Action& action = req.action;
        if (a == "mull")
            action.type = Match::ActionType::Mulligan;
        else if (a == "play")
            action.type = Match::ActionType::Play;
        else if (a == "atk")
            action.type = Match::ActionType::Attack;
        else if (a == "hp")
            action.type = Match::ActionType::HeroPower;
        else if (a == "end")
            action.type = Match::ActionType::EndTurn;
        else if (a == "concede")
            action.type = Match::ActionType::Concede;
        else if (a == "pick")
            action.type = Match::ActionType::Choose;
        else
            return false;

        auto const seq = kv.find("seq");
        if (seq != kv.end())
        {
            if (!ParseU32(seq->second, req.seq))
                return false;
        }
        else if (action.type != Match::ActionType::Concede)
            return false;

        auto const s = kv.find("s");
        auto const t = kv.find("t");
        auto const p = kv.find("p");
        auto const r = kv.find("r");
        auto const c = kv.find("c");
        uint32_t value = 0;
        bool haveSource = false;
        bool haveTarget = false;
        bool haveList = false;
        if (s != kv.end())
        {
            if (!ParseU32(s->second, value))
                return false;
            action.source = value;
            haveSource = true;
        }
        if (t != kv.end())
        {
            if (!ParseU32(t->second, value))
                return false;
            action.target = value;
            haveTarget = true;
        }
        if (p != kv.end())
        {
            if (!ParseU32(p->second, value) || value > static_cast<uint32_t>(kMaxWirePosition))
                return false;
            action.position = static_cast<int>(value);
        }
        if (r != kv.end())
        {
            if (!ParseIdList(r->second, action.replace))
                return false;
            haveList = true;
        }
        if (c != kv.end())
        {
            // Choose One option: only with a=play, 1 or 2.
            if (action.type != Match::ActionType::Play || !ParseU32(c->second, value) || (value != 1 && value != 2))
                return false;
            action.choice = static_cast<int>(value);
        }

        switch (action.type)
        {
            case Match::ActionType::Mulligan:
                if (!haveList)
                    return false;
                action.source = 0;
                action.target = 0;
                action.position = -1;
                break;
            case Match::ActionType::Play:
                if (!haveSource)
                    return false;
                action.replace.clear();
                break;
            case Match::ActionType::Attack:
                if (!haveSource || !haveTarget)
                    return false;
                action.position = -1;
                action.replace.clear();
                break;
            case Match::ActionType::HeroPower:
                // The hero power is the acting side's own: the service fills the source in.
                action.source = 0;
                action.position = -1;
                action.replace.clear();
                break;
            case Match::ActionType::Choose:
                // a=pick;s=<option id> answers a pending pick.
                if (!haveSource)
                    return false;
                action.target = 0;
                action.position = -1;
                action.replace.clear();
                break;
            case Match::ActionType::EndTurn:
            case Match::ActionType::Concede:
                action.source = 0;
                action.target = 0;
                action.position = -1;
                action.replace.clear();
                break;
        }
        out = req;
        return true;
    }

    bool DecodeGameLeave(std::string const& payload, uint32_t& matchId)
    {
        std::map<std::string, std::string> kv;
        if (!KvDecode(payload, kv) || kv.size() != 1)
            return false;
        auto const mid = kv.find("mid");
        uint32_t id = 0;
        if (mid == kv.end() || !ParseU32(mid->second, id))
            return false;
        matchId = id;
        return true;
    }

    char const* GameWhyText(std::string const& why)
    {
        if (why == kWhyStale)
            return "Your view of the match is out of date.";
        if (why == kWhyMatch)
            return "There is no running match with this id.";
        if (why == kWhyOver)
            return "The match is over.";
        if (why == kWhyActive)
            return "The match is still running (concede to leave it).";
        if (why == "ordinary")
            return "Only an arena duel can be paused.";
        if (why == "phase")
            return "That is not possible in this phase of the match.";
        if (why == "turn")
            return "It is not your turn.";
        if (why == "source")
            return "That card or character cannot be used.";
        if (why == "mana")
            return "Not enough mana.";
        if (why == "target")
            return "That is not a valid target.";
        if (why == "board")
            return "Your board is full.";
        if (why == "attack")
            return "That character cannot attack now.";
        if (why == "taunt")
            return "A Taunt minion must be attacked first.";
        if (why == "used")
            return "The hero power was already used this turn.";
        if (why == "requirement")
            return "A requirement of that card is not met.";
        if (why == "mulligan")
            return "That is not a valid mulligan.";
        if (why == "position")
            return "That board position is not valid.";
        if (why == "unsupported")
            return "That card is not supported yet.";
        if (why == "choice")
            return "That card needs a valid choice (option 1 or 2).";
        if (why == "pending")
            return "You have to pick one of the offered cards first.";
        if (why == "secret")
            return "You already have that secret in play.";
        return "The action was refused.";
    }
}
