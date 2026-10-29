#include "HsLadderPlay.h"

#include <cmath>

namespace Hs
{
namespace Ladder
{
    namespace
    {
        // Cuts at a character boundary: a UTF-8 continuation byte (10xxxxxx) never ends the text alone.
        std::string CutBytes(std::string const& text, std::size_t maxBytes)
        {
            if (text.size() <= maxBytes)
                return text;
            std::size_t n = maxBytes;
            while (n > 0 && (static_cast<unsigned char>(text[n]) & 0xC0) == 0x80)
                --n;
            return text.substr(0, n);
        }

        std::string ClassWord(std::string const& heroClass)
        {
            std::string s = heroClass;
            for (std::size_t i = 0; i < s.size(); ++i)
            {
                char& c = s[i];
                if (i == 0)
                    c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
                else
                    c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
            }
            return s;
        }
    }

    bool PlayerResultOfMatch(std::string const& res, Result& out)
    {
        if (res == "win")
            out = Result::Win;
        else if (res == "loss")
            out = Result::Loss;
        else if (res == "draw")
            out = Result::Draw;
        else
            return false;
        return true;
    }

    std::string RecordText(uint32_t wins, uint32_t losses, uint32_t draws)
    {
        return std::to_string(wins) + "-" + std::to_string(losses) + "-" + std::to_string(draws);
    }

    std::string PositionText(int rank, int stars)
    {
        if (rank == kLegend)
            return "Legend";
        return "Rank " + std::to_string(rank) + " (" + std::to_string(stars) + (stars == 1 ? " star)" : " stars)");
    }

    std::string FactionText(Faction faction)
    {
        return faction == Faction::Alliance ? "Alliance" : faction == Faction::Horde ? "Horde" : "Neutral";
    }

    std::string StandingText(bool onLadder, int rank, int stars, uint32_t wins, uint32_t losses, uint32_t draws)
    {
        if (!onLadder)
            return "You are not on the ladder yet (you start at " + PositionText(kBottomRank, 0) + ").";
        return "Your standing: " + PositionText(rank, stars) + ", record " + RecordText(wins, losses, draws) + ".";
    }

    std::string OfferLineText(std::string const& name, std::string const& heroClass, Faction faction, int rank, int stars,
                              uint32_t wins, uint32_t losses, uint32_t draws, std::size_t maxBytes)
    {
        std::string const tail = " - " + ClassWord(heroClass) + " - " + FactionText(faction) + " - " +
            PositionText(rank, stars) + " - your record " + RecordText(wins, losses, draws);
        if (name.size() + tail.size() <= maxBytes)
            return name + tail;
        if (tail.size() >= maxBytes)
            return CutBytes(name + tail, maxBytes);
        return CutBytes(name, maxBytes - tail.size()) + tail;
    }

    float Distance(WorldPoint const& a, WorldPoint const& b)
    {
        double const dx = static_cast<double>(a.x) - b.x;
        double const dy = static_cast<double>(a.y) - b.y;
        double const dz = static_cast<double>(a.z) - b.z;
        return static_cast<float>(std::sqrt(dx * dx + dy * dy + dz * dz));
    }

    std::vector<WorldPoint> WalkInPoints(LadderTable const& table, WorldPoint const& seat)
    {
        std::vector<WorldPoint> points = table.path;
        points.push_back(table.stop);
        if (Distance(table.stop, seat) > 0.01f)
            points.push_back(seat);
        return points;
    }

    std::vector<WorldPoint> WalkBackPoints(LadderTable const& table, std::vector<WorldPoint> const& walkIn,
                                           std::size_t reached)
    {
        std::vector<WorldPoint> back;
        if (reached > walkIn.size())
            reached = walkIn.size();
        // Legs 0 .. reached-1 were reached: the nearest passed point is the one before leg `reached`.
        for (std::size_t i = reached; i > 0; --i)
            back.push_back(walkIn[i - 1]);
        back.push_back(table.spawn);
        return back;
    }
}
}
