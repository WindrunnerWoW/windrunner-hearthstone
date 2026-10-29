/*
 * Battlegrounds session: one Game with one human seat. The human acts through Apply (the
 * Game action API in a plain-data form, so the wire decoder and the tests share it); the other seven players are NPCs of
 * the real policy (HsBgAi). Apply(EndTurn) runs the NPC turns and the round's fights; the human's own fight of that
 * round is kept with its full event log for the playback.
 *
 * PURE C++ (standard library and the other src/battlegrounds headers). The server glue owns the Session per character;
 * HsBgWire.* turns it into the WRBG payloads.
 *
 * The human sits at `seat` (default 0, faction 0). When the human is eliminated the rest of the game is played out at
 * once (nobody external is left to wait for), so the session is Over() right after the EndTurn that eliminated them.
 */

#ifndef MOD_HEARTHSTONE_HS_BG_SESSION_H
#define MOD_HEARTHSTONE_HS_BG_SESSION_H

#include "HsBgAi.h"
#include "HsBgGame.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Hs
{
    namespace Bg
    {
        enum class ActionType : uint8_t { Buy, Sell, Play, Move, Refresh, Freeze, Upgrade, Discover, EndTurn, Hero, Power, Send, Quest, Cast, Peek };

        // One request of the human. Fields by type: Buy index = offer; Sell zone + index; Play index = hand slot, slot =
        // board position, uid = target (0 none); Cast index = hand slot, uid = spell target (0 none); Move index = from,
        // slot = to; Freeze flag; Discover index = option;
        // Hero index = the choice; Power index = offer index (-1 none), uid = target (0 none); Send index = hand slot,
        // slot = the ally seat.
        struct Action
        {
            ActionType type = ActionType::EndTurn;
            Zone zone = Zone::Board;
            int index = 0;
            int slot = 0;
            uint32_t uid = 0;
            uint32_t uid2 = 0;   // a power's second target (Vol'jin)
            bool flag = false;
        };

        // The human's fight of one round.
        struct HumanFight
        {
            int round = 0;
            int opponent = -1;         // the opposing seat
            int mySide = 0;            // 0 or 1: the side of the human in `result`
            ExternalFight fight;       // the engine's record; result.events / start / survivors are in combat ids
        };

        struct SessionOptions
        {
            uint32_t seed = 1;
            int seat = 0;
            // The game shape (the glue sets both on). false keeps the single-pool, hero-free game.
            bool heroes = false;
            bool factionPools = false;
            bool quests = false;   // the turn-4 quest and reward offer
            // The seat names of the lobby (the human's own stays empty). Padded/trimmed to kPlayers.
            std::vector<std::string> seatNames;
            // The coins of a finished game (the glue fills these from the config keys): everyone gets the base, a human
            // on the winning faction the team amount, the overall winner the first amount. They are totals, not added.
            int payoutBase = 3;
            int payoutTeam = 5;
            int payoutFirst = 7;
            int honorBase = 10;    // the honor, on the same shape (totals): base, winning faction, first place
            int honorTeam = 50;
            int honorFirst = 70;
        };

        // What the human did in a game and how it ended, for the Battlegrounds weeklies (Session::Record).
        struct GameRecord
        {
            int placement = 8;          // overall 1 (best) .. 8
            bool teamWon = false;       // the human's faction won the game
            int tier = 1;               // the highest tavern tier reached
            int heroPowers = 0;         // hero power uses
            GameStats stats;
        };

        class Session
        {
        public:
            explicit Session(SessionOptions const& options);

            Game& GetGame() { return *m_game; }
            Game const& GetGame() const { return *m_game; }
            int Seat() const { return m_seat; }
            uint32_t Seed() const { return m_seed; }
            bool Over() const { return m_game->Over(); }
            bool HumanAlive() const { return m_game->Alive(m_seat); }
            bool Picking() const { return m_game->Picking(); }
            // The quest pick (turn 4) and the human's three offered pairs.
            bool QuestPicking() const { return m_game->QuestPicking(); }
            std::vector<std::pair<int, int>> const& QuestOffer() const { return m_game->QuestOffer(m_seat); }
            PlayerShop const& Me() const { return m_game->GetShop().State(m_seat); }
            // The human's hero choices during the pick (4 of them), empty outside it.
            std::vector<int> const& HeroChoices() const { return m_game->HeroChoices(m_seat); }
            // The human's seat name (from seatNames), empty when the glue supplied none.
            std::string const& SeatName(int seat) const;
            // A living ally of the human's faction (not the human): the only seats a peek may show.
            bool IsLivingAlly(int seat) const
            {
                return seat >= 0 && seat < kPlayers && seat != m_seat &&
                    FactionOf(seat) == FactionOf(m_seat) && m_game->Alive(seat);
            }

            // Applies one action. Nothing changes unless the result is Ok. EndTurn plays the round out.
            ActionResult Apply(Action const& action);

            // The human's fight of the last Apply (EndTurn only); empty when there was none (the human was out already).
            bool HasFight() const { return m_hasFight; }
            HumanFight const& Fight() const { return m_fight; }

            // 1 (best) .. 8 over the whole lobby: survivors by health, then the eliminated, the later out the better.
            int OverallPlacement(int player) const;
            // The human fought and won the game with their faction.
            bool HumanFactionWon() const { return Over() && m_game->WinnerFaction() == FactionOf(m_seat); }
            // The payout of a finished game (0 while the game runs; the glue pays it once).
            GameRecord Record() const;
            int PayoutCoins() const;
            int PayoutHonor() const;
            int PayoutPacks() const;
            // The cards allies sent the human during the last action (card indexes, oldest first).
            std::vector<int> const& LastGifts() const { return m_lastGifts; }
            // The Wheel of Yogg-Saron outcomes (0-5) spun for the human during the last action, oldest first.
            std::vector<int> const& LastWheel() const { return m_lastWheel; }

        private:
            uint32_t m_seed;
            int m_seat;
            Ai m_ai;
            std::unique_ptr<Game> m_game;
            std::vector<std::string> m_seatNames;
            std::vector<int> m_lastGifts;
            std::vector<int> m_lastWheel;
            int m_payoutBase = 3;
            int m_payoutTeam = 5;
            int m_payoutFirst = 7;
            int m_honorBase = 10;
            int m_honorTeam = 50;
            int m_honorFirst = 70;
            bool m_hasFight = false;
            HumanFight m_fight;
        };
    }
}

#endif
