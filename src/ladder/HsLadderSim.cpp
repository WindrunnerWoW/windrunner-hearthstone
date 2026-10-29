/*
 * World NPC Ladder: the headless game runner. See HsLadderSim.h.
 */

#include "HsLadderSim.h"

#include <memory>

namespace Hs
{
namespace Ladder
{
    namespace
    {
        char const* ActionTypeName(Match::ActionType t)
        {
            switch (t)
            {
                case Match::ActionType::Mulligan: return "mulligan";
                case Match::ActionType::Play: return "play";
                case Match::ActionType::Attack: return "attack";
                case Match::ActionType::HeroPower: return "hero power";
                case Match::ActionType::EndTurn: return "end turn";
                case Match::ActionType::Concede: return "concede";
                case Match::ActionType::Choose: return "choose";
            }
            return "?";
        }

        uint64_t Fnv1a64(std::string const& text)
        {
            uint64_t h = 0xCBF29CE484222325ull;
            for (unsigned char c : text)
            {
                h ^= c;
                h *= 0x100000001B3ull;
            }
            return h;
        }

        Match::MatchSetup MakeSetup(SimEntrant const& a, SimEntrant const& b, uint32_t seed, int firstSide)
        {
            Match::MatchSetup s;
            s.seed = seed;
            s.firstSide = firstSide;
            SimEntrant const* const entrants[2] = {&a, &b};
            for (int side = 0; side < 2; ++side)
            {
                s.sides[side].name = entrants[side]->name;
                s.sides[side].heroClass = entrants[side]->heroClass;
                s.sides[side].deck = entrants[side]->deck;
            }
            return s;
        }

        SimResult Fail(SimResult r, std::string const& error)
        {
            r.outcome = SimOutcome::Failed;
            r.error = error;
            return r;
        }
    }

    char const* SimOutcomeCode(SimOutcome outcome)
    {
        switch (outcome)
        {
            case SimOutcome::Win0: return "win0";
            case SimOutcome::Win1: return "win1";
            case SimOutcome::Draw: return "draw";
            case SimOutcome::Failed: return "failed";
        }
        return "failed";
    }

    uint32_t SimAiSeed(uint32_t gameSeed, int side, uint32_t seq)
    {
        // The "lowbias32" finalizer over the three numbers; the side gets its own salt so that both AIs of a game
        // never share a seed.
        uint32_t x = gameSeed ^ (seq * 0x9E3779B9u + 0x7F4A7C15u) ^ (static_cast<uint32_t>(side) * 0x85EBCA6Bu + 0x68E31DA4u);
        x ^= x >> 16;
        x *= 0x7FEB352Du;
        x ^= x >> 15;
        x *= 0x846CA68Bu;
        x ^= x >> 16;
        return x;
    }

    SimResult SimulateGame(Match::CardDb const& db, SimEntrant const& a, SimEntrant const& b, uint32_t seed,
                           int firstSide, SimOptions const& options)
    {
        SimResult r;
        if (firstSide < -1 || firstSide > 1)
            return Fail(r, "bad first side");

        std::string error;
        std::unique_ptr<Match::Match> m = Match::Match::Create(db, MakeSetup(a, b, seed, firstSide), error);
        if (!m)
            return Fail(r, "setup refused: " + error);

        Match::AiProfile const profile[2] = {a.profile, b.profile};
        auto decide = [&](int side, int actionsThisTurn) {
            ++r.decisions[side];
            if (options.chooser)
                return options.chooser(*m, side, actionsThisTurn);
            Match::AiDecisionInfo info;
            Match::Action const action = Match::ChooseAiAction(*m, side, db, actionsThisTurn, profile[side],
                SimAiSeed(seed, side, m->Seq()), &info);
            r.simulations[side] += info.simulations;
            return action;
        };
        auto apply = [&](int side, Match::Action const& action) {
            Match::ActionError const err = m->Apply(side, action);
            if (err == Match::ActionError::None)
            {
                ++r.actions;
                return std::string();
            }
            return std::string("side ") + std::to_string(side) + " " + ActionTypeName(action.type) +
                " refused (" + Match::ActionErrorCode(err) + ") on turn " + std::to_string(m->Turn());
        };

        // Both sides choose their mulligan (the engine starts turn 1 once both have).
        for (int side = 0; side < 2; ++side)
        {
            if (!m->MulliganPending(side))
                continue;
            Match::Action const action = decide(side, 0);
            if (action.type != Match::ActionType::Mulligan)
                return Fail(r, std::string("side ") + std::to_string(side) + " answered the mulligan with " +
                    ActionTypeName(action.type));
            std::string const refused = apply(side, action);
            if (!refused.empty())
                return Fail(r, refused);
        }
        if (m->GetPhase() == Match::Phase::Play)
            r.firstSide = m->ActiveSide();

        int turn = -1;
        int perTurn[2] = {0, 0};
        while (m->GetPhase() == Match::Phase::Play)
        {
            if (r.actions >= options.maxActions)
                return Fail(r, "action limit " + std::to_string(options.maxActions) + " reached on turn " +
                    std::to_string(m->Turn()));
            if (m->Turn() != turn)
            {
                turn = m->Turn();
                perTurn[0] = perTurn[1] = 0;
            }
            int const active = m->ActiveSide();
            // A pending pick belongs to whoever must answer it; normally the active side, but the engine allows a
            // pick of the other side, which then has to answer before anything else happens.
            int side = active;
            if (m->ViewFor(active, m->LastEventSeq()).opponentPicking)
                side = 1 - active;
            Match::Action const action = decide(side, perTurn[side]);
            std::string const refused = apply(side, action);
            if (!refused.empty())
                return Fail(r, refused);
            ++perTurn[side];
        }

        Match::Result const result = m->GetResult();
        r.reason = result.reason;
        r.turns = m->Turn();
        for (int side = 0; side < 2; ++side)
            r.heroHealth[side] = m->HeroHealth(side);
        r.stateHash = Fnv1a64(m->DebugState());
        if (!result.over || result.reason == Match::EndReason::Aborted || result.reason == Match::EndReason::None)
            return Fail(r, "the game ended without a result");
        r.outcome = result.winner == 0 ? SimOutcome::Win0 : result.winner == 1 ? SimOutcome::Win1 : SimOutcome::Draw;
        return r;
    }
}
}
