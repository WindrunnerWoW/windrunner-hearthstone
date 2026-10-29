#include "HsBgSession.h"

#include <algorithm>

namespace Hs
{
    namespace Bg
    {
        namespace
        {
            GameSetup MakeSetup(SessionOptions const& o, NpcPolicy* policy)
            {
                GameSetup setup;
                setup.seed = o.seed;
                setup.policy = policy;
                setup.kinds[o.seat] = PlayerKind::External;
                setup.heroPick = o.heroes;
                setup.factionPools = o.factionPools;
                setup.humanSeat = o.seat;
                setup.quests = o.quests;   // the turn-4 quest offer
                return setup;
            }
        }

        Session::Session(SessionOptions const& options)
            : m_seed(options.seed),
              m_seat(options.seat >= 0 && options.seat < kPlayers ? options.seat : 0)
        {
            m_seatNames.assign(kPlayers, std::string());
            for (std::size_t i = 0; i < options.seatNames.size() && i < static_cast<std::size_t>(kPlayers); ++i)
                m_seatNames[i] = options.seatNames[i];
            m_payoutBase = options.payoutBase;
            m_payoutTeam = options.payoutTeam;
            m_payoutFirst = options.payoutFirst;
            m_honorBase = options.honorBase;
            m_honorTeam = options.honorTeam;
            m_honorFirst = options.honorFirst;
            SessionOptions o = options;
            o.seat = m_seat;
            m_game.reset(new Game(MakeSetup(o, &m_ai)));
        }

        std::string const& Session::SeatName(int seat) const
        {
            static std::string const empty;
            return seat >= 0 && seat < kPlayers ? m_seatNames[static_cast<std::size_t>(seat)] : empty;
        }

        GameRecord Session::Record() const
        {
            GameRecord r;
            PlayerShop const& ps = m_game->GetShop().State(m_seat);
            r.placement = OverallPlacement(m_seat);
            r.teamWon = HumanFactionWon();
            r.tier = ps.tier;
            r.heroPowers = ps.power.usesThisGame;
            r.stats = ps.stats;
            return r;
        }

        int Session::PayoutCoins() const
        {
            if (!Over())
                return 0;
            // Totals, never added: first place, else the winning faction, else the base (a loss pays like a duel win).
            if (OverallPlacement(m_seat) == 1)
                return m_payoutFirst;
            return HumanFactionWon() ? m_payoutTeam : m_payoutBase;
        }

        int Session::PayoutHonor() const
        {
            if (!Over())
                return 0;
            if (OverallPlacement(m_seat) == 1)
                return m_honorFirst;
            return HumanFactionWon() ? m_honorTeam : m_honorBase;
        }

        int Session::PayoutPacks() const
        {
            if (!Over())
                return 0;
            return 0;   // packs come from the Battlegrounds weeklies now; the wire still carries the field
        }

        ActionResult Session::Apply(Action const& a)
        {
            m_hasFight = false;
            Game& g = *m_game;
            ActionResult r = ActionResult::Ok;
            switch (a.type)
            {
            case ActionType::Buy: r = g.Buy(m_seat, a.index); break;
            case ActionType::Sell: r = g.Sell(m_seat, a.zone, a.index); break;
            case ActionType::Play: r = g.Play(m_seat, a.index, a.slot, a.uid); break;
            case ActionType::Cast: r = g.Cast(m_seat, a.index, a.uid); break;
            case ActionType::Move: r = g.Move(m_seat, a.index, a.slot); break;
            case ActionType::Refresh: r = g.Refresh(m_seat); break;
            case ActionType::Freeze: r = g.Freeze(m_seat, a.flag); break;
            case ActionType::Upgrade: r = g.Upgrade(m_seat); break;
            case ActionType::Discover: r = g.Choose(m_seat, a.index); break;
            case ActionType::Hero: r = g.ChooseHero(m_seat, a.index); break;
            case ActionType::Quest: r = g.ChooseQuest(m_seat, a.index); break;
            case ActionType::Power: r = g.HeroPower(m_seat, a.uid, a.index, a.uid2); break;
            case ActionType::Send: r = g.Send(m_seat, a.index, a.slot); break;
            case ActionType::Peek:
                r = IsLivingAlly(a.slot) ? ActionResult::Ok : ActionResult::NotAlly;
                break;
            case ActionType::EndTurn:
                g.ClearExternalFights();
                r = g.EndTurn(m_seat);
                if (r == ActionResult::Ok)
                {
                    // The human takes part in at most one fight per round, and the human's turn ends once per round.
                    for (ExternalFight const& f : g.ExternalFights())
                        if (f.a == m_seat || f.b == m_seat)
                        {
                            m_fight.round = f.round;
                            m_fight.mySide = f.a == m_seat ? 0 : 1;
                            m_fight.opponent = f.a == m_seat ? f.b : f.a;
                            m_fight.fight = f;
                            m_hasFight = true;
                        }
                    g.ClearExternalFights();
                }
                break;
            }
            // Pick up (and clear) any gift an ally sent the human during this action.
            PlayerShop& state = m_game->GetShop().State(m_seat);
            m_lastGifts = state.gifts;
            state.gifts.clear();
            m_lastWheel = state.wheelSpins;
            state.wheelSpins.clear();
            return r;
        }

        int Session::OverallPlacement(int player) const
        {
            Game const& g = *m_game;
            std::vector<int> const& out = g.EliminationOrder();
            // Rank key, higher is better: survivors above everyone, by health; the eliminated by how late they went out.
            auto const key = [&](int p) -> long
            {
                if (g.Alive(p))
                    return 1000000L + g.GetShop().State(p).health * 100L - p;
                auto const it = std::find(out.begin(), out.end(), p);
                return static_cast<long>(it - out.begin());
            };
            int place = 1;
            for (int p = 0; p < kPlayers; ++p)
                if (p != player && key(p) > key(player))
                    ++place;
            return place;
        }
    }
}
