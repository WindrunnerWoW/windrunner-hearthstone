#include "HsMatchReplay.h"

#include <cstdint>
#include <vector>

namespace Hs
{
    namespace
    {
        char TypeCode(Match::ActionType t)
        {
            switch (t)
            {
                case Match::ActionType::Mulligan:
                    return 'M';
                case Match::ActionType::Play:
                    return 'P';
                case Match::ActionType::Attack:
                    return 'A';
                case Match::ActionType::HeroPower:
                    return 'H';
                case Match::ActionType::EndTurn:
                    return 'E';
                case Match::ActionType::Concede:
                    return 'C';
                case Match::ActionType::Choose:
                    return 'K';
            }
            return '?';
        }

        bool TypeOf(char c, Match::ActionType& out)
        {
            switch (c)
            {
                case 'M':
                    out = Match::ActionType::Mulligan;
                    return true;
                case 'P':
                    out = Match::ActionType::Play;
                    return true;
                case 'A':
                    out = Match::ActionType::Attack;
                    return true;
                case 'H':
                    out = Match::ActionType::HeroPower;
                    return true;
                case 'E':
                    out = Match::ActionType::EndTurn;
                    return true;
                case 'C':
                    out = Match::ActionType::Concede;
                    return true;
                case 'K':
                    out = Match::ActionType::Choose;
                    return true;
            }
            return false;
        }

        // Decimal digits only, no leading zero (except "0" itself), at most max. With allowMinusOne the single
        // text "-1" is accepted too (the board position of "rightmost").
        bool ParseNumber(std::string const& s, uint32_t max, bool allowMinusOne, int64_t& out)
        {
            if (allowMinusOne && s == "-1")
            {
                out = -1;
                return true;
            }
            if (s.empty() || s.size() > 10 || (s.size() > 1 && s[0] == '0'))
                return false;
            uint64_t v = 0;
            for (char c : s)
            {
                if (c < '0' || c > '9')
                    return false;
                v = v * 10 + static_cast<uint64_t>(c - '0');
            }
            if (v > max)
                return false;
            out = static_cast<int64_t>(v);
            return true;
        }

        std::vector<std::string> Split(std::string const& text)
        {
            std::vector<std::string> parts;
            std::size_t start = 0;
            while (true)
            {
                std::size_t const comma = text.find(',', start);
                if (comma == std::string::npos)
                {
                    parts.push_back(text.substr(start));
                    break;
                }
                parts.push_back(text.substr(start, comma - start));
                start = comma + 1;
            }
            return parts;
        }
    }

    bool SameLoggedAction(LoggedAction const& a, LoggedAction const& b)
    {
        return a.side == b.side && a.action.type == b.action.type && a.action.source == b.action.source &&
               a.action.target == b.action.target && a.action.position == b.action.position &&
               a.action.choice == b.action.choice && a.action.replace == b.action.replace;
    }

    std::string EncodeAction(LoggedAction const& logged)
    {
        if (logged.side < 0 || logged.side > 1)
            return std::string();
        Match::Action const& a = logged.action;
        std::string text = "v" + std::to_string(kActionLogVersion) + "," + std::to_string(logged.side) + "," +
            TypeCode(a.type) + "," + std::to_string(a.source) + "," + std::to_string(a.target) + "," +
            std::to_string(a.position) + "," + std::to_string(a.choice);
        for (Match::EntityId id : a.replace)
            text += "," + std::to_string(id);
        return text;
    }

    bool DecodeAction(std::string const& text, LoggedAction& out, std::string& error)
    {
        error.clear();
        std::vector<std::string> const f = Split(text);
        if (f.size() < 7)
        {
            error = "malformed";
            return false;
        }
        if (f[0] != "v" + std::to_string(kActionLogVersion))
        {
            error = f[0].size() > 1 && f[0][0] == 'v' ? "version" : "malformed";
            return false;
        }
        if (f[1] != "0" && f[1] != "1")
        {
            error = "side";
            return false;
        }
        LoggedAction r;
        r.side = f[1] == "1" ? 1 : 0;
        if (f[2].size() != 1 || !TypeOf(f[2][0], r.action.type))
        {
            error = "type";
            return false;
        }
        int64_t source = 0, target = 0, position = 0, choice = 0;
        if (!ParseNumber(f[3], 0xFFFFFFFFu, false, source) || !ParseNumber(f[4], 0xFFFFFFFFu, false, target) ||
            !ParseNumber(f[5], 15, true, position) || !ParseNumber(f[6], 2, false, choice))
        {
            error = "number";
            return false;
        }
        r.action.source = static_cast<Match::EntityId>(source);
        r.action.target = static_cast<Match::EntityId>(target);
        r.action.position = static_cast<int>(position);
        r.action.choice = static_cast<int>(choice);
        if (f.size() > 7 && (r.action.type != Match::ActionType::Mulligan || f.size() - 7 > kMaxLoggedReplace))
        {
            error = "replace";
            return false;
        }
        for (std::size_t i = 7; i < f.size(); ++i)
        {
            int64_t id = 0;
            if (!ParseNumber(f[i], 0xFFFFFFFFu, false, id))
            {
                error = "number";
                return false;
            }
            r.action.replace.push_back(static_cast<Match::EntityId>(id));
        }
        out = r;
        return true;
    }
}
