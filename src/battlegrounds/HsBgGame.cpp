#include "HsBgGame.h"

#include <algorithm>
#include <climits>
#include <string>

namespace Hs
{
    namespace Bg
    {
        namespace
        {
            int Score(Minion const& m) { return m.attack + m.health + m.Tier() * 2; }

            // Sort It All Out: the board is non-decreasing by current Attack (2-7 minions; a board of 0-1 is not counted).
            bool BoardOrdered(Board const& b)
            {
                if (b.Size() < 2)
                    return false;
                for (int i = 1; i < b.Size(); ++i)
                    if (b[i].attack < b[i - 1].attack)
                        return false;
                return true;
            }

            // How well a quest fits what an NPC already holds at the pick (board and hand). Only quests whose
            // progress depends on the cards held get a bonus; the rest are judged by QuestPickValue alone.
            int QuestBoardFit(int questIndex, QuestTypes const& types, PlayerShop const& ps, EffectRegistry const& fx)
            {
                if (questIndex < 0 || questIndex >= QuestCount())
                    return 0;
                std::string const id = QuestAt(questIndex).id;
                int fit = 0;
                auto each = [&](Minion const& m, bool onBoard)
                {
                    if (m.card < 0)
                        return;
                    EffectDef const* const e = fx.Find(m.card);
                    if (id == "BG24_Quest_151" && types.typeA != 0 &&
                        (m.IsTribe(types.typeA) || m.IsTribe(types.typeB)))
                        fit += 8;                                   // Unlikely Duo: minions of its types to replay
                    else if (id == "BG24_Quest_311" && e && e->battlecry)
                        fit += 8;                                   // Cry for Help: a Battlecry build
                    else if (id == "BG24_Quest_320" && onBoard && !DeathrattleInstances(fx, m).empty())
                        fit += 8;                                   // Exhume the Bones: Deathrattles already down
                    else if (id == "BG24_Quest_124" && onBoard)
                        fit += 2;                                   // Reenact the Murder: more bodies, more deaths
                };
                for (Minion const& m : ps.board.Items())
                    each(m, true);
                for (Minion const& m : ps.hand.Items())
                    each(m, false);
                if (id == "BG24_Quest_125" && BoardOrdered(ps.board))
                    fit += 12;                                      // Sort It All Out: the board is already in order
                return fit;
            }
        }

        // ------------------------------------------------------------------------------------------ placeholder NPC

        void SimpleNpcPolicy::PlayTurn(Game& g, int p)
        {
            Shop& s = g.GetShop();

            auto discover = [&]
            {
                while (s.DiscoverOpen(p))
                {
                    std::vector<Minion> const& opts = s.State(p).options;
                    int best = 0;
                    for (int i = 1; i < static_cast<int>(opts.size()); ++i)
                        if (Score(opts[static_cast<std::size_t>(i)]) > Score(opts[static_cast<std::size_t>(best)]))
                            best = i;
                    if (g.Choose(p, best) != ActionResult::Ok)
                        break;
                }
            };

            auto playAll = [&]
            {
                for (int guard = 0; guard < 24; ++guard)
                {
                    PlayerShop& ps = s.State(p);
                    if (ps.hand.Empty() || ps.board.Full())
                        return;
                    uint32_t target = 0;
                    if (ps.hand[0].keywords & kKwMagnetic)
                    {
                        for (int i = 0; i < ps.board.Size(); ++i)
                            if (ps.board[i].IsTribe(kTribeMech))
                            {
                                target = ps.board[i].uid;
                                break;
                            }
                    }
                    else if (!ps.board.Empty() && g.Random().Coin())
                        target = ps.board[static_cast<int>(g.Random().Uniform(static_cast<uint32_t>(ps.board.Size())))].uid;
                    ActionResult r = g.Play(p, 0, ps.board.Size(), target);
                    if (r == ActionResult::BadTarget)
                        r = g.Play(p, 0, ps.board.Size(), 0);
                    if (r != ActionResult::Ok)
                        return;
                    discover();
                }
            };

            auto weakest = [](std::vector<Minion> const& list) -> int
            {
                int w = -1;
                for (int i = 0; i < static_cast<int>(list.size()); ++i)
                    if (w < 0 || Score(list[static_cast<std::size_t>(i)]) < Score(list[static_cast<std::size_t>(w)]))
                        w = i;
                return w;
            };

            discover();
            playAll();
            int refreshes = 0;
            for (int it = 0; it < 60; ++it)
            {
                PlayerShop& ps = s.State(p);
                int const wanted = std::min(kMaxTier, 1 + g.Round() / 3);
                if (ps.tier < wanted && ps.tier < kMaxTier && ps.gold >= s.UpgradeCost(p))
                {
                    if (g.Upgrade(p) == ActionResult::Ok)
                        continue;
                }
                // The hand is stuck behind a full board: swap the weakest board minion for the best hand minion, or
                // sell a hand minion that is not better.
                if (!ps.hand.Empty() && ps.board.Full())
                {
                    int const wb = weakest(ps.board.Items());
                    int const wh = weakest(ps.hand.Items());
                    int bh = 0;
                    for (int i = 1; i < ps.hand.Size(); ++i)
                        if (Score(ps.hand[i]) > Score(ps.hand[bh]))
                            bh = i;
                    if (Score(ps.hand[bh]) > Score(ps.board[wb]))
                    {
                        g.Sell(p, Zone::Board, wb);
                        playAll();
                    }
                    else
                        g.Sell(p, Zone::Hand, wh);
                    continue;
                }
                if (ps.gold >= s.BuyCost(p) && !ps.offers.empty())
                {
                    int best = 0;
                    for (int i = 1; i < static_cast<int>(ps.offers.size()); ++i)
                        if (Score(ps.offers[static_cast<std::size_t>(i)]) > Score(ps.offers[static_cast<std::size_t>(best)]))
                            best = i;
                    bool const worth = !ps.board.Full() ||
                        Score(ps.offers[static_cast<std::size_t>(best)]) > Score(ps.board[weakest(ps.board.Items())]);
                    if (worth)
                    {
                        ActionResult const r = g.Buy(p, best);
                        if (r == ActionResult::Ok)
                        {
                            playAll();
                            continue;
                        }
                        if (r == ActionResult::HandFull && !ps.hand.Empty())
                        {
                            g.Sell(p, Zone::Hand, weakest(ps.hand.Items()));
                            continue;
                        }
                    }
                }
                if (refreshes < 2 && ps.gold > s.BuyCost(p))
                {
                    ++refreshes;
                    if (g.Refresh(p) == ActionResult::Ok)
                        continue;
                }
                break;
            }
            discover();
            PlayerShop& ps = s.State(p);
            if (ps.gold < s.BuyCost(p) && !ps.offers.empty() && g.Random().Uniform(4) == 0)
                g.Freeze(p, true);
        }

        // ------------------------------------------------------------------------------------------ setup

        Game::Game(GameSetup const& setup)
            : m_setup(setup),
              m_effects(setup.effects ? *setup.effects : EffectRegistry::Global()),
              m_rng(setup.seed),
              m_shop(m_pool, m_effects, m_rng, kPlayers)
        {
            if (setup.policy)
                m_policy = setup.policy;
            else
            {
                m_ownedPolicy.reset(new SimpleNpcPolicy());
                m_policy = m_ownedPolicy.get();
            }
            // Each faction rolls its own set of the playable tribes (the same set when faction pools are off).
            std::vector<uint32_t> const playable = SliceTribes(setup.slice15AOnly);
            m_tribes[0] = ChooseTribes(m_rng, playable, setup.tribeCount);
            m_tribes[1] = setup.factionPools ? ChooseTribes(m_rng, playable, setup.tribeCount) : m_tribes[0];
            m_shop.SetLobbyTribes(m_tribes[0]);   // the global fallback (tests and older callers)
            for (int f = 0; f < 2; ++f)
            {
                uint32_t tribeMask = 0;
                for (uint32_t t : m_tribes[f])
                    tribeMask |= t;
                if (setup.factionPools)
                    m_pool.AddTribeCards(f, tribeMask, setup.slice15AOnly, true);
                else
                    m_pool.AddTribeCards(0, tribeMask, setup.slice15AOnly, false);
            }
            for (int p = 0; p < kPlayers; ++p)
            {
                m_kinds[p] = setup.kinds[p];
                m_alive[p] = true;
                m_ended[p] = false;
                m_elimRound[p] = 0;
                m_lastOpponent[p] = -2;
                m_lastFought[p] = 0;
                m_restCount[p] = 0;
                m_restHealed[p] = 0;
                m_shop.State(p).pools = std::vector<int>{ setup.factionPools ? FactionOf(p) : 0 };
                m_shop.SetLobbyTribes(p, m_tribes[FactionOf(p)]);   // The Friends Along the Way picks my type
            }
            if (setup.heroPick)
            {
                m_picking = true;
                DealHeroChoices();
            }
            else
                StartRound();
            if (setup.autoRun)
                Pump();
        }

        int Game::LivingInFaction(int faction) const
        {
            int n = 0;
            for (int p = faction * kFactionSize; p < (faction + 1) * kFactionSize; ++p)
                if (m_alive[p])
                    ++n;
            return n;
        }

        std::vector<int> Game::FactionRanking(int faction) const
        {
            std::vector<int> alive;
            for (int p = faction * kFactionSize; p < (faction + 1) * kFactionSize; ++p)
                if (m_alive[p])
                    alive.push_back(p);
            std::sort(alive.begin(), alive.end(), [this](int a, int b)
            {
                PlayerShop const& sa = m_shop.State(a);
                PlayerShop const& sb = m_shop.State(b);
                if (sa.health + sa.armor != sb.health + sb.armor)   // armor is part of what keeps a hero alive
                    return sa.health + sa.armor > sb.health + sb.armor;
                if (sa.tier != sb.tier)
                    return sa.tier > sb.tier;
                return a < b;
            });
            std::vector<int> out = alive;
            for (auto it = m_elimOrder.rbegin(); it != m_elimOrder.rend(); ++it)
                if (FactionOf(*it) == faction)
                    out.push_back(*it);
            return out;
        }

        int Game::Placement(int player) const
        {
            std::vector<int> const r = FactionRanking(FactionOf(player));
            for (int i = 0; i < static_cast<int>(r.size()); ++i)
                if (r[static_cast<std::size_t>(i)] == player)
                    return i + 1;
            return 0;
        }

        // ------------------------------------------------------------------------------------------ actions

        ActionResult Game::Check(int player) const
        {
            if (player < 0 || player >= kPlayers)
                return ActionResult::BadPlayer;
            if (m_picking)
                return ActionResult::NotShopPhase;   // only ChooseHero is open during the pick
            if (m_questPicking)
                return ActionResult::NotShopPhase;   // only ChooseQuest is open during the quest pick
            if (m_over)
                return ActionResult::GameOver;
            if (!m_alive[player])
                return ActionResult::Eliminated;
            if (m_ended[player])
                return ActionResult::NotShopPhase;
            return ActionResult::Ok;
        }

        ActionResult Game::Buy(int player, int offerIndex)
        {
            ActionResult const r = Check(player);
            return r != ActionResult::Ok ? r : m_shop.Buy(player, offerIndex);
        }

        ActionResult Game::Sell(int player, Zone zone, int index)
        {
            ActionResult const r = Check(player);
            return r != ActionResult::Ok ? r : m_shop.Sell(player, zone, index);
        }

        ActionResult Game::Play(int player, int handIndex, int slot, uint32_t targetUid)
        {
            ActionResult const r = Check(player);
            return r != ActionResult::Ok ? r : m_shop.Play(player, handIndex, slot, targetUid);
        }

        ActionResult Game::Cast(int player, int handIndex, uint32_t targetUid)
        {
            ActionResult const r = Check(player);
            return r != ActionResult::Ok ? r : m_shop.Cast(player, handIndex, targetUid);
        }

        ActionResult Game::Move(int player, int fromSlot, int toSlot)
        {
            ActionResult const r = Check(player);
            return r != ActionResult::Ok ? r : m_shop.Move(player, fromSlot, toSlot);
        }

        ActionResult Game::Refresh(int player)
        {
            ActionResult const r = Check(player);
            return r != ActionResult::Ok ? r : m_shop.Refresh(player);
        }

        ActionResult Game::Freeze(int player, bool frozen)
        {
            ActionResult const r = Check(player);
            return r != ActionResult::Ok ? r : m_shop.Freeze(player, frozen);
        }

        ActionResult Game::Upgrade(int player)
        {
            ActionResult const r = Check(player);
            return r != ActionResult::Ok ? r : m_shop.Upgrade(player);
        }

        ActionResult Game::Choose(int player, int optionIndex)
        {
            ActionResult const r = Check(player);
            return r != ActionResult::Ok ? r : m_shop.Choose(player, optionIndex);
        }

        ActionResult Game::Send(int player, int handIndex, int ally)
        {
            ActionResult const r = Check(player);
            if (r != ActionResult::Ok)
                return r;
            if (ally < 0 || ally >= kPlayers || ally == player || FactionOf(ally) != FactionOf(player) || !m_alive[ally])
                return ActionResult::NotAlly;
            return m_shop.Send(player, handIndex, ally);
        }

        ActionResult Game::EndTurn(int player)
        {
            ActionResult r = Check(player);
            if (r != ActionResult::Ok)
                return r;
            r = m_shop.EndTurn(player);
            if (r != ActionResult::Ok)
                return r;
            m_ended[player] = true;
            Pump();
            return ActionResult::Ok;
        }

        ActionResult Game::ChooseHero(int player, int choiceIndex)
        {
            if (player < 0 || player >= kPlayers)
                return ActionResult::BadPlayer;
            if (m_over)
                return ActionResult::GameOver;
            if (!m_picking || !m_alive[player] || m_heroPicked[player])
                return ActionResult::NotPicking;
            std::vector<int> const& choices = m_heroChoices[player];
            if (choiceIndex < 0 || choiceIndex >= static_cast<int>(choices.size()))
                return ActionResult::BadIndex;
            m_shop.SetHero(player, choices[static_cast<std::size_t>(choiceIndex)]);
            m_heroPicked[player] = true;
            Pump();   // may finish the pick and start round 1
            return ActionResult::Ok;
        }

        ActionResult Game::HeroPower(int player, uint32_t targetUid, int offerIndex, uint32_t secondUid)
        {
            ActionResult const r = Check(player);
            if (r != ActionResult::Ok)
                return r;
            HeroPowerDef const* const power = m_shop.Power(player);
            if (power && power->target == PowerTarget::Seat && !Alive(offerIndex))
                return ActionResult::BadTarget;   // Lord Barov guesses a player who is still in the game
            return m_shop.HeroPower(player, targetUid, offerIndex, secondUid);
        }

        ActionResult Game::ChooseQuest(int player, int choiceIndex)
        {
            if (player < 0 || player >= kPlayers)
                return ActionResult::BadPlayer;
            if (m_over)
                return ActionResult::GameOver;
            if (!m_alive[player])
                return ActionResult::Eliminated;
            int const slot = m_shop.OfferQuestSlot(player);
            if (slot < 0)
                return ActionResult::NotPicking;
            QuestState const& qs = m_shop.QuestSlot(player, slot);
            if (choiceIndex < 0 || choiceIndex >= static_cast<int>(qs.offer.size()))
                return ActionResult::BadIndex;
            TakeQuest(player, choiceIndex);
            Pump();   // may finish the quest pick
            return ActionResult::Ok;
        }

        std::vector<std::pair<int, int>> const& Game::QuestOffer(int player) const
        {
            static std::vector<std::pair<int, int>> const empty;
            if (player < 0 || player >= kPlayers)
                return empty;
            int const slot = m_shop.OfferQuestSlot(player);
            return slot < 0 ? empty : m_shop.QuestSlot(player, slot).offer;
        }

        int Game::PlayerQuest(int player) const
        {
            return player >= 0 && player < kPlayers ? m_shop.State(player).quest.quest : -1;
        }

        int Game::PlayerReward(int player) const
        {
            return player >= 0 && player < kPlayers ? m_shop.State(player).quest.reward : -1;
        }

        int Game::QuestProgress(int player) const
        {
            return player >= 0 && player < kPlayers ? m_shop.State(player).quest.count : 0;
        }

        int Game::QuestRequired(int player) const
        {
            return player >= 0 && player < kPlayers ? m_shop.State(player).quest.required : 0;
        }

        bool Game::QuestDone(int player) const
        {
            return player >= 0 && player < kPlayers && m_shop.State(player).quest.done;
        }

        // Sire Denathrius's second quest (slot 1).
        int Game::PlayerQuest2(int player) const
        {
            return player >= 0 && player < kPlayers ? m_shop.State(player).quest2.quest : -1;
        }

        int Game::PlayerReward2(int player) const
        {
            return player >= 0 && player < kPlayers ? m_shop.State(player).quest2.reward : -1;
        }

        int Game::QuestProgress2(int player) const
        {
            return player >= 0 && player < kPlayers ? m_shop.State(player).quest2.count : 0;
        }

        int Game::QuestRequired2(int player) const
        {
            return player >= 0 && player < kPlayers ? m_shop.State(player).quest2.required : 0;
        }

        bool Game::QuestDone2(int player) const
        {
            return player >= 0 && player < kPlayers && m_shop.State(player).quest2.done;
        }

        std::vector<int> const& Game::HeroChoices(int player) const
        {
            static std::vector<int> const empty;
            return player >= 0 && player < kPlayers ? m_heroChoices[player] : empty;
        }

        int Game::PlayerHero(int player) const
        {
            if (player < 0 || player >= kPlayers)
                return -1;
            return m_shop.State(player).hero;
        }

        void Game::DealHeroChoices()
        {
            // 18 distinct curated heroes: 4 for the human seat, 2 for each NPC. Shuffled from the game Rng.
            std::vector<int> pool;
            for (int i = 0; i < HeroCount(); ++i)
                if (HeroAt(i).curated)
                    pool.push_back(i);
            m_rng.Shuffle(pool);
            if (pool.size() < static_cast<std::size_t>(4 + 2 * (kPlayers - 1)))
            {
                // A short curated list must not leave a seat without a choice (the pick would never finish):
                // top it up with the other heroes of the table.
                std::vector<int> rest;
                for (int i = 0; i < HeroCount(); ++i)
                    if (!HeroAt(i).curated)
                        rest.push_back(i);
                m_rng.Shuffle(rest);
                pool.insert(pool.end(), rest.begin(), rest.end());
            }
            for (int p = 0; p < kPlayers; ++p)
                m_heroChoices[p].clear();
            // A hero whose power draws a tribe from the lobby pool (Patches, Flurgl, Ini Stormcoil) is skipped for a
            // seat whose faction lobby lacks that tribe; it stays in the pool for the other seats. Without skips the
            // deal is the plain front-to-back one. King Mukla needs Beasts in BOTH factions: his Wild Bananas (a
            // friendly Beast +1/+1) go to every player, and a spell cannot be sold, so a lobby with no Beasts fills
            // its hands with Bananas it can never cast.
            std::vector<bool> used(pool.size(), false);
            auto const usable = [this](int hero, int seat)
            {
                HeroPowerDef const* const power = HeroPowerRegistry::Global().Find(HeroAt(hero).powerId);
                if (!power || power->needsLobbyTribe == 0)
                    return true;
                auto const has = [&](int faction)
                {
                    for (uint32_t t : m_tribes[faction])
                        if (t == power->needsLobbyTribe)
                            return true;
                    return false;
                };
                return has(FactionOf(seat)) && (!power->bananarama || has(1 - FactionOf(seat)));
            };
            auto const deal = [&](int seat, int count)
            {
                for (int pass = 0; pass < 2; ++pass)   // the second pass tops up from the skipped heroes
                    for (std::size_t i = 0; i < pool.size() && static_cast<int>(m_heroChoices[seat].size()) < count; ++i)
                        if (!used[i] && (pass == 1 || usable(pool[i], seat)))
                        {
                            used[i] = true;
                            m_heroChoices[seat].push_back(pool[i]);
                        }
            };
            int const humanSeat = m_setup.humanSeat >= 0 && m_setup.humanSeat < kPlayers ? m_setup.humanSeat : 0;
            deal(humanSeat, 4);
            for (int p = 0; p < kPlayers; ++p)
                if (p != humanSeat)
                    deal(p, 2);
        }

        void Game::NpcPickHeroes()
        {
            for (int p = 0; p < kPlayers; ++p)
            {
                if (m_heroPicked[p] || !m_alive[p] || m_kinds[p] != PlayerKind::Npc)
                    continue;
                int best = -1;
                int bestValue = 0;
                for (int h : m_heroChoices[p])
                {
                    int const v = HeroPickValue(h);
                    if (best < 0 || v > bestValue)
                    {
                        best = h;
                        bestValue = v;
                    }
                }
                if (best >= 0)
                {
                    m_shop.SetHero(p, best);
                    m_heroPicked[p] = true;
                }
            }
        }

        bool Game::AllHeroesPicked() const
        {
            for (int p = 0; p < kPlayers; ++p)
                if (m_alive[p] && !m_heroPicked[p])
                    return false;
            return true;
        }

        void Game::DealQuestOffers()
        {
            // Three quest + reward pairs per living player, all three quests distinct and all three rewards
            // distinct, drawn from the game Rng (so the offer is deterministic for a seed). A player who
            // already holds a quest (Denathrius) gets the offer in their second slot.
            for (int p = 0; p < kPlayers; ++p)
            {
                if (!m_alive[p])
                    continue;
                int const slot = m_shop.FreeQuestSlot(p);
                if (slot >= 0)
                    DealQuestOffers(p, slot, 3);
            }
            m_questPicking = AnyQuestPending();
        }

        // Sire Denathrius's start-of-game choice (two pairs) into a given quest slot.
        void Game::DealStartQuestOffers()
        {
            for (int p = 0; p < kPlayers; ++p)
            {
                if (!m_alive[p])
                    continue;
                HeroPowerDef const* const power = m_shop.Power(p);
                if (!power || !power->startQuestOffer)
                    continue;
                int const slot = m_shop.FreeQuestSlot(p);
                if (slot >= 0)
                    DealQuestOffers(p, slot, 2);
            }
            m_questPicking = AnyQuestPending();
        }

        void Game::DealQuestOffers(int player, int slot, int count)
        {
            PlayerShop& ps = m_shop.State(player);
            QuestState& qs = slot == 1 ? ps.quest2 : ps.quest;
            qs.offer.clear();
            qs.offerTypes.clear();
            // Denathrius never gets the quest or the reward of his other slot a second time.
            QuestState const& other = slot == 1 ? ps.quest : ps.quest2;
            std::vector<int> qsPool, rsPool;
            for (int i = 0; i < QuestCount(); ++i)
                if (QuestAt(i).inPool && i != other.quest)
                    qsPool.push_back(i);
            for (int i = 0; i < RewardCount(); ++i)
                if (RewardAt(i).inPool && i != other.reward)
                    rsPool.push_back(i);
            if (qsPool.empty() || rsPool.empty())
                return;
            std::vector<int> q = qsPool, r = rsPool;
            m_rng.Shuffle(q);
            m_rng.Shuffle(r);
            int const n = std::min<int>({ static_cast<int>(q.size()), static_cast<int>(r.size()), count });
            for (int i = 0; i < n; ++i)
            {
                int const qi = q[static_cast<std::size_t>(i)];
                int const ri = r[static_cast<std::size_t>(i)];
                qs.offer.push_back(std::make_pair(qi, ri));
                qs.offerTypes.push_back(DrawQuestTypes(qi, ri, m_tribes[FactionOf(player)], m_rng));   // shown on the offer
            }
        }

        void Game::NpcPickQuests()
        {
            for (int p = 0; p < kPlayers; ++p)
            {
                if (!m_alive[p] || m_kinds[p] != PlayerKind::Npc)
                    continue;
                for (;;)
                {
                    int const slot = m_shop.OfferQuestSlot(p);
                    if (slot < 0)
                        break;
                    QuestState const& qs = m_shop.QuestSlot(p, slot);
                    int best = 0;
                    int bestValue = INT_MIN;
                    for (int i = 0; i < static_cast<int>(qs.offer.size()); ++i)
                    {
                        std::size_t const k = static_cast<std::size_t>(i);
                        QuestTypes const types = k < qs.offerTypes.size() ? qs.offerTypes[k] : QuestTypes{};
                        // The pair's value plus how well the quest fits the board and hand the NPC already has.
                        int const v = QuestPickValue(qs.offer[k].first, qs.offer[k].second) +
                            QuestBoardFit(qs.offer[k].first, types, m_shop.State(p), m_shop.Effects());
                        if (v > bestValue)
                        {
                            bestValue = v;
                            best = i;
                        }
                    }
                    TakeQuest(p, best);
                }
            }
        }

        bool Game::AnyQuestPending() const
        {
            for (int p = 0; p < kPlayers; ++p)
            {
                if (!m_alive[p])
                    continue;
                if (m_shop.OfferQuestSlot(p) >= 0)
                    return true;
            }
            return false;
        }

        void Game::TakeQuest(int player, int choiceIndex)
        {
            int const slot = m_shop.OfferQuestSlot(player);
            if (slot < 0)
                return;
            PlayerShop& ps = m_shop.State(player);
            QuestState& qs = slot == 1 ? ps.quest2 : ps.quest;
            std::size_t const k = static_cast<std::size_t>(choiceIndex);
            std::pair<int, int> const pick = k < qs.offer.size() ? qs.offer[k] : std::make_pair(-1, -1);
            QuestTypes const types = k < qs.offerTypes.size() ? qs.offerTypes[k] : QuestTypes{};
            qs.offer.clear();
            qs.offerTypes.clear();
            if (pick.first < 0 || pick.first >= QuestCount() || pick.second < 0 || pick.second >= RewardCount())
            {
                qs.quest = -1;
                qs.reward = -1;
                qs.count = qs.required = 0;
                qs.done = false;
                return;
            }
            qs.quest = pick.first;
            qs.reward = pick.second;
            qs.required = QuestAt(pick.first).progress;
            qs.count = 0;
            qs.done = false;   // in-pool quests have progress >= 3, so none is done on the pick
            qs.typeA = types.typeA;   // drawn with the offer (DealQuestOffers), so the pick showed them
            qs.typeB = types.typeB;
            qs.rewardType = types.rewardType;
        }

        bool Game::AllEnded() const
        {
            for (int p = 0; p < kPlayers; ++p)
                if (m_alive[p] && !m_ended[p])
                    return false;
            return true;
        }

        void Game::Pump()
        {
            if (m_pumping)
                return;
            m_pumping = true;
            while (!m_over)
            {
                if (m_picking)
                {
                    NpcPickHeroes();
                    if (!AllHeroesPicked())
                        break;
                    m_picking = false;
                    StartRound();
                    continue;
                }
                if (m_questPicking)
                {
                    NpcPickQuests();
                    if (AnyQuestPending())
                        break;
                    m_questPicking = false;
                    continue;
                }
                for (int p = 0; p < kPlayers; ++p)
                {
                    if (!m_alive[p] || m_ended[p] || m_kinds[p] != PlayerKind::Npc)
                        continue;
                    m_policy->PlayTurn(*this, p);
                    if (m_ended[p])
                        continue;
                    while (m_shop.DiscoverOpen(p) && m_shop.Choose(p, 0) == ActionResult::Ok)
                    {
                    }
                    m_shop.EndTurn(p);
                    m_ended[p] = true;
                }
                if (!AllEnded())
                    break;
                ResolveRound();
            }
            m_pumping = false;
        }

        // ------------------------------------------------------------------------------------------ rounds

        void Game::StartRound()
        {
            ++m_round;
            m_capThisRound = m_elimOrder.empty();
            for (int p = 0; p < kPlayers; ++p)
            {
                m_ended[p] = !m_alive[p];
                if (m_alive[p])
                {
                    m_shop.StartTurn(p, m_round);
                    // A. F. Kay and Ambassador Faelin skip their first turns: the shop turn ends at
                    // once (the gold curve and the upgrade discount already advanced), and the empty board fights.
                    if (m_shop.State(p).skipTurn)
                    {
                        // A Discover a start-of-turn effect left open is settled with its first option, like the
                        // round's own cleanup, so the turn can end.
                        while (m_shop.DiscoverOpen(p) && m_shop.Choose(p, 0) == ActionResult::Ok)
                        {
                        }
                        m_shop.EndTurn(p);
                        m_ended[p] = true;
                    }
                }
            }
            if (m_setup.quests && m_round == m_setup.questRound)
                DealQuestOffers();   // the quest options of turn 4
            // Sire Denathrius chooses one of two quests at the start of the game.
            if (m_round == 1)
                DealStartQuestOffers();
        }

        void Game::ApplyPersistent(int player, int side, std::vector<PersistentChange> const& changes)
        {
            if (player < 0 || player >= kPlayers)
                return;
            PlayerShop& ps = m_shop.State(player);
            // Buffs address the board slot the minion had when the fight began, so they all go first: a card added to
            // the hand can complete a triple and shift the board.
            for (PersistentChange const& c : changes)
            {
                if (c.side != side)
                    continue;
                if (c.kind == kPersistBuff)
                {
                    if (c.inputIndex < 0 || c.inputIndex >= ps.board.Size())
                        continue;
                    Minion& m = ps.board[c.inputIndex];
                    if (c.attack != 0 || c.health != 0)
                        m_shop.Buff(player, m, c.attack, c.health);
                    if (c.keywords != 0)
                        m_shop.GiveKeywords(player, m, c.keywords);
                }
            }
            for (PersistentChange const& c : changes)
                if (c.side == side && c.kind == kPersistMakeGolden && c.inputIndex >= 0 &&
                    c.inputIndex < ps.board.Size() && !ps.board[c.inputIndex].golden)
                    m_shop.MakeGolden(player, ps.board[c.inputIndex].uid);   // Tony Two-Tusk
            for (PersistentChange const& c : changes)
                if (c.side == side && c.kind == kPersistAddToHand && c.card >= 0)
                    m_shop.AddToHand(player, c.card, c.golden);
            for (PersistentChange const& c : changes)
                if (c.side == side && c.kind == kPersistAddPoolToHand && c.card >= 0)
                    if (!m_shop.AddPoolCardToHand(player, c.card, c.golden))
                        m_shop.AddToHand(player, c.card, c.golden);   // the pool copy was taken already
            for (PersistentChange const& c : changes)
                if (c.side == side && c.kind == kPersistElementalTavernBonus && c.value > 0)
                    m_shop.AddElementalTavernBonus(player, c.value, c.value);   // Dazzling Lightspawn
        }

        void Game::Eliminate(int player)
        {
            // Mr. Bigglesworth. Before the dead board returns to the pool, every living Bigglesworth
            // sees it; several deaths in one round queue several Discovers.
            Board const warband = m_shop.State(player).board;
            for (int q = 0; q < kPlayers; ++q)
            {
                if (q == player || !m_alive[q])
                    continue;
                HeroPowerDef const* const power = m_shop.Power(q);
                if (power && power->bigglesworth)
                    m_shop.OfferWarbandDiscover(q, warband);
            }
            m_shop.ReleasePlayer(player);
            m_alive[player] = false;
            m_ended[player] = true;
            m_elimRound[player] = m_round;
            m_elimOrder.push_back(player);
        }

        void Game::CheckGameOver()
        {
            int const a = LivingInFaction(0);
            int const b = LivingInFaction(1);
            if (a > 0 && b > 0)
                return;
            m_over = true;
            if (a == 0 && b == 0)
                m_winner = m_elimOrder.empty() ? 0 : FactionOf(m_elimOrder.back());
            else
                m_winner = a > 0 ? 0 : 1;
        }

        std::vector<std::pair<int, int>> Game::Pair()
        {
            std::vector<int> la;
            std::vector<int> lb;
            for (int p = 0; p < kPlayers; ++p)
                if (m_alive[p])
                    (FactionOf(p) == 0 ? la : lb).push_back(p);
            std::vector<int> big = la.size() >= lb.size() ? la : lb;
            std::vector<int> const& small = la.size() >= lb.size() ? lb : la;
            // The larger faction takes turns: those who have rested least fight now (ties: whoever fought most
            // recently rests), the rest rest.
            if (big.size() > small.size())
            {
                m_rng.Shuffle(big);
                std::stable_sort(big.begin(), big.end(), [this](int x, int y) {
                    if (m_restCount[x] != m_restCount[y])
                        return m_restCount[x] < m_restCount[y];
                    return m_lastFought[x] > m_lastFought[y];
                });
                std::size_t const rest = big.size() - small.size();
                big.erase(big.begin(), big.begin() + static_cast<std::ptrdiff_t>(rest));
            }

            std::vector<std::pair<int, int>> best;
            int bestCost = INT_MAX;
            for (int attempt = 0; attempt < 64 && bestCost > 0; ++attempt)
            {
                std::vector<int> vb = big;
                std::vector<int> vs = small;
                m_rng.Shuffle(vb);
                m_rng.Shuffle(vs);
                std::vector<std::pair<int, int>> pairs;
                int cost = 0;
                for (std::size_t i = 0; i < vb.size(); ++i)
                {
                    int const x = std::min(vb[i], vs[i]);
                    int const y = std::max(vb[i], vs[i]);
                    pairs.push_back(std::make_pair(x, y));
                    cost += (m_lastOpponent[x] == y ? 1 : 0) + (m_lastOpponent[y] == x ? 1 : 0);
                }
                if (cost < bestCost)
                {
                    bestCost = cost;
                    best = pairs;
                }
            }
            std::sort(best.begin(), best.end());
            return best;
        }

        void Game::ResolveRound()
        {
            // A Discover left open is settled with its first option.
            for (int p = 0; p < kPlayers; ++p)
                while (m_alive[p] && m_shop.DiscoverOpen(p) && m_shop.Choose(p, 0) == ActionResult::Ok)
                {
                }

            // Heroes killed by an effect during the shop phase go out before the fights.
            {
                std::vector<int> dead;
                for (int p = 0; p < kPlayers; ++p)
                    if (m_alive[p] && m_shop.State(p).health <= 0)
                        dead.push_back(p);
                std::sort(dead.begin(), dead.end(), [this](int a, int b)
                {
                    int const ha = m_shop.State(a).health;
                    int const hb = m_shop.State(b).health;
                    return ha != hb ? ha < hb : a < b;
                });
                for (int p : dead)
                    Eliminate(p);
                CheckGameOver();
                if (m_over)
                    return;
            }

            std::vector<std::pair<int, int>> const pairs = Pair();
            struct Damage
            {
                int player;
                int amount;
            };
            std::vector<Damage> damage;
            CombatSettings settings;
            settings.effects = &m_effects;
            settings.capDamage = m_capThisRound;
            settings.slice15AOnly = m_setup.slice15AOnly;
            settings.round = m_round;   // Tavish's aim doubles every 3 turns
            for (int f = 0; f < 2; ++f)
                for (uint32_t t : m_tribes[f])
                    settings.lobbyTribes[f] |= t;
            for (std::pair<int, int> const& pr : pairs)
            {
                CombatSide a;
                a.board = m_shop.State(pr.first).board;
                a.tavernTier = m_shop.State(pr.first).tier;
                a.secrets = m_shop.State(pr.first).secrets;
                CombatSide b;
                b.board = m_shop.State(pr.second).board;
                b.tavernTier = m_shop.State(pr.second).tier;
                b.secrets = m_shop.State(pr.second).secrets;
                settings.rewardState[0] = &m_shop.State(pr.first).rewardState;
                settings.rewardState[1] = &m_shop.State(pr.second).rewardState;
                settings.rewardState2[0] = &m_shop.State(pr.first).rewardState2;   // Denathrius
                settings.rewardState2[1] = &m_shop.State(pr.second).rewardState2;
                bool rafaamArmed[2] = { false, false };
                bool armedSide[2] = { false, false };
                for (int s = 0; s < 2; ++s)
                {
                    int const pl = s == 0 ? pr.first : pr.second;
                    PlayerShop const& ps = m_shop.State(pl);
                    HeroPowerDef const* const hp = m_shop.Power(pl);
                    settings.summonAttackTribe[s] = hp ? hp->summonAttackTribe : 0u;
                    settings.summonAttack[s] = hp ? hp->summonAttack : 0;
                    // Tavish's aim and Bru'kan's invocation act in combat without replacing the power.
                    settings.hero[s] = HeroCombatDefFor(m_shop.CombatPowerId(pl));
                    bool const armed = hp && hp->armsCombat && ps.power.combatArmed;
                    if (!armed)
                        settings.hero[s].yshaarj = false;   // Y'Shaarj only acts on the combat the power armed
                    settings.hero[s].ozumatSold = ps.power.minionsSoldThisGame;
                    armedSide[s] = armed;
                    rafaamArmed[s] = armed && std::string(hp->id) == "TB_BaconShop_HP_053";
                    // Each side's own faction set (the two may differ).
                    settings.lobbyTribes[s] = 0;
                    for (uint32_t t : m_tribes[FactionOf(pl)])
                        settings.lobbyTribes[s] |= t;
                }
                CombatResult const res = SimulateCombat(a, b, m_rng.Next(), settings);
                // Aranna Starseeker counts the attacks of the player's minions over the whole game.
                for (CombatEvent const& e : res.events)
                    if (e.type == EventType::Attack)
                        ++m_shop.State(e.side == 0 ? pr.first : pr.second).power.friendlyAttacks;
                // Victim's Specter (a reward that was active in this combat), then the combat's quest events, then
                // the activation of a reward whose quest this combat finished (so a start-of-turn reward works on the
                // very next turn).
                for (int s = 0; s < 2; ++s)
                {
                    int const pl = s == 0 ? pr.first : pr.second;
                    RewardState const* const states[2] = { &m_shop.State(pl).rewardState, &m_shop.State(pl).rewardState2 };
                    for (RewardState const* st : states)
                    {
                        RewardRule const* const rd = RewardFor(*st);
                        if (!rd || rd->kind != RewardKind::VictimsSpecter)
                            continue;
                        int last = -1;
                        for (CombatEvent const& e : res.events)
                            if (e.type == EventType::Death && e.side == s)
                                last = e.card;
                        if (last >= 0)
                            m_shop.AddToHand(pl, last, false);   // a plain copy of the last friendly minion that died
                    }
                }
                if (m_setup.quests)
                {
                    // The quest events a combat carries. Side 0 is pr.first, side 1 the opponent.
                    // CombatOrdered reads the board as it entered the fight, before Start of Combat.
                    for (int s = 0; s < 2; ++s)
                    {
                        int const pl = s == 0 ? pr.first : pr.second;
                        PlayerShop& ps = m_shop.State(pl);
                        auto const fireQuest = [&ps](QuestEventInfo const& ev)
                        {
                            ApplyQuestEvent(ps.quest, ev);
                            ApplyQuestEvent(ps.quest2, ev);   // the second quest counts too
                        };
                        if (BoardOrdered(s == 0 ? a.board : b.board))
                            fireQuest(QuestEventInfo{ QuestEvent::CombatOrdered });
                        int deaths = 0;
                        int rattles = 0;
                        for (CombatEvent const& e : res.events)
                        {
                            if (e.side != s)
                                continue;
                            if (e.type == EventType::Death)
                                ++deaths;
                            else if (e.type == EventType::Trigger && e.amount == static_cast<int>(HookKind::Deathrattle))
                                ++rattles;
                        }
                        if (deaths > 0)
                            fireQuest(QuestEventInfo{ QuestEvent::FriendlyDied, deaths, 0 });
                        if (rattles > 0)
                            fireQuest(QuestEventInfo{ QuestEvent::DeathrattleTriggered, rattles, 0 });
                        if (res.winner != s)
                            fireQuest(QuestEventInfo{ QuestEvent::LoseOrTieCombat });
                        m_shop.CheckReward(pl);
                    }
                }
                // Ini Stormcoil (MechGyver) counts friendly combat deaths.
                for (int s = 0; s < 2; ++s)
                {
                    int const pl = s == 0 ? pr.first : pr.second;
                    int deaths = 0;
                    for (CombatEvent const& e : res.events)
                        if (e.side == s && e.type == EventType::Death)
                            ++deaths;
                    m_shop.CountFriendlyDeaths(pl, deaths);
                }
                // Salty Looter counts the Pirates summoned (or reborn) on the player's own side in combat.
                for (int s = 0; s < 2; ++s)
                {
                    int pirates = 0;
                    for (CombatEvent const& e : res.events)
                        if (e.side == s && (e.type == EventType::Summon || e.type == EventType::Reborn) && e.card >= 0 &&
                            HasTribe(CardAt(e.card).tribes, kTribePirate))
                            ++pirates;
                    m_shop.CountCombatPirateSummons(s == 0 ? pr.first : pr.second, pirates);
                }
                // Murozond: each player remembers the board it just fought (as it entered the fight).
                m_shop.SetLastOpponent(pr.first, b.board);
                m_shop.SetLastOpponent(pr.second, a.board);
                // Tortollan Blue Shell (a draw is not a loss).
                m_shop.SetLastCombatWon(pr.first, res.winner == -1 || res.winner == 0);
                m_shop.SetLastCombatWon(pr.second, res.winner == -1 || res.winner == 1);
                ApplyPersistent(pr.first, 0, res.persistent);
                ApplyPersistent(pr.second, 1, res.persistent);
                // Rafaam's first kill and the spent combat powers (Y'Shaarj, Rafaam).
                for (int s = 0; s < 2; ++s)
                {
                    int const pl = s == 0 ? pr.first : pr.second;
                    if (rafaamArmed[s] && !res.kills[s].empty())
                        m_shop.AddPoolCardToHand(pl, res.kills[s].front(), false);   // a plain pool copy, none if no copy left
                    if (armedSide[s])
                        m_shop.State(pl).power.combatArmed = false;
                    // Ragnaros counts the enemy minions his side killed; Tavish's aim lasts one combat.
                    m_shop.CountEnemyKills(pl, static_cast<int>(res.kills[s].size()));
                    // A Secret that triggered in combat leaves play.
                    for (int card : res.secretsUsed[s])
                    {
                        std::vector<int>& secrets = m_shop.State(pl).secrets;
                        secrets.erase(std::remove(secrets.begin(), secrets.end(), card), secrets.end());
                    }
                    PlayerShop& ps = m_shop.State(pl);
                    if (ps.power.combatPowerOnce)
                    {
                        ps.power.combatPower.clear();
                        ps.power.combatPowerOnce = false;
                    }
                }

                CombatRecord rec;
                rec.round = m_round;
                rec.a = pr.first;
                rec.b = pr.second;
                rec.capped = m_capThisRound;
                rec.attacks = res.attacks;
                if (res.winner == 0)
                {
                    rec.winner = pr.first;
                    rec.damage = res.damage;
                    damage.push_back(Damage{ pr.second, res.damage });
                }
                else if (res.winner == 1)
                {
                    rec.winner = pr.second;
                    rec.damage = res.damage;
                    damage.push_back(Damage{ pr.first, res.damage });
                }
                m_combats.push_back(rec);
                // What the weeklies count of this combat, for both sides.
                for (int s = 0; s < 2; ++s)
                {
                    GameStats& stats = m_shop.State(s == 0 ? pr.first : pr.second).stats;
                    for (CombatEvent const& e : res.events)
                        if (e.side == s && e.type == EventType::Trigger && e.amount == static_cast<int>(HookKind::Deathrattle))
                            ++stats.deathrattles;
                    stats.kills += static_cast<int>(res.kills[s].size());
                    if (res.winner == s)
                    {
                        ++stats.combatWins;
                        stats.damageDealt += res.damage;
                    }
                }
                if (m_kinds[pr.first] == PlayerKind::External || m_kinds[pr.second] == PlayerKind::External)
                    m_externalFights.push_back(ExternalFight{ m_round, pr.first, pr.second, res });
                m_lastOpponent[pr.first] = pr.second;
                m_lastOpponent[pr.second] = pr.first;
            }
            // Everyone living without a fight this round rested: kRestHeal health, up to the hero's maximum (none from
            // round kRestHealEndRound on).
            for (int p = 0; p < kPlayers; ++p)
            {
                m_restHealed[p] = 0;
                if (!m_alive[p])
                    continue;
                bool fought = false;
                for (std::pair<int, int> const& pr : pairs)
                    fought = fought || pr.first == p || pr.second == p;
                if (fought)
                {
                    m_lastFought[p] = m_round;
                    continue;
                }
                PlayerShop& ps = m_shop.State(p);
                int const healed =
                    m_round >= kRestHealEndRound ? 0 : std::max(0, std::min(kRestHeal, ps.maxHealth - ps.health));
                ps.health += healed;
                m_restHealed[p] = healed;
                ++m_restCount[p];
                m_lastOpponent[p] = kRested;
            }
            // A Barov guess on a player who rested (no combat) simply fails.
            m_shop.ClearBarovGuesses();
            for (Damage const& d : damage)
            {
                // Ice Block prevents the fatal damage of the round (and then leaves play).
                if (m_shop.PreventFatalDamage(d.player, d.amount))
                    continue;
                m_shop.TakeDamage(d.player, d.amount);
            }

            std::vector<int> dead;
            for (int p = 0; p < kPlayers; ++p)
                if (m_alive[p] && m_shop.State(p).health <= 0)
                    dead.push_back(p);
            std::sort(dead.begin(), dead.end(), [this](int a, int b)
            {
                int const ha = m_shop.State(a).health;
                int const hb = m_shop.State(b).health;
                return ha != hb ? ha < hb : a < b;
            });
            for (int p : dead)
                Eliminate(p);
            CheckGameOver();
            if (!m_over && m_round >= kMaxRounds)
            {
                int total[2] = { 0, 0 };
                for (int p = 0; p < kPlayers; ++p)
                    if (m_alive[p])
                        total[FactionOf(p)] += m_shop.State(p).health + m_shop.State(p).armor;
                m_over = true;
                m_timedOut = true;
                m_winner = total[1] > total[0] ? 1 : 0;
            }
            if (!m_over)
                StartRound();
        }
    }
}
