#include "HsBgAi.h"

#include <algorithm>

namespace Hs
{
    namespace Bg
    {
        namespace
        {
            constexpr int kTripleBonus = 1000;
            constexpr int kPairBonus = 8;
            constexpr int kSwapMargin = 4;      // a replacement must beat the weakest board minion by this much

            // The tuning of the policy that is playing right now (Value() is static); the defaults otherwise.
            AiTuning const kDefaultTuning;
            AiTuning const* g_tune = &kDefaultTuning;

            // Copies of a normal card on the board and in the hand (what a triple needs three of).
            int Copies(PlayerShop const& ps, int card)
            {
                int n = 0;
                for (Minion const& m : ps.board.Items())
                    if (m.card == card && !m.golden)
                        ++n;
                for (Minion const& m : ps.hand.Items())
                    if (m.card == card && !m.golden)
                        ++n;
                return n;
            }

            bool IsSpell(Minion const& m)
            {
                return m.card >= 0 && CardAt(m.card).role == CardRole::Spell;
            }

            // How good a Darkmoon Prize is for the NPC (the plan: gold and tavern prizes first, ones that
            // need a target last). A negative value means the NPC cannot use it just now.
            int PrizeValue(PlayerShop const& ps, int card)
            {
                std::string const id = CardAt(card).id;
                auto hasNonGolden = [&]()
                {
                    for (Minion const& m : ps.board.Items())
                        if (!m.golden)
                            return true;
                    return false;
                };
                if (id == "BGS_Treasures_001") return 8;    // Pocket Change (2 Gold)
                if (id == "BGS_Treasures_004") return 6;    // Gacha Gift (a Tier 1 minion)
                if (id == "BGS_Treasures_007") return ps.board.Empty() ? 1 : 5;   // Might of Stormwind
                if (id == "BGS_Treasures_013") return 6;    // The Good Stuff (tavern +1 Attack)
                if (id == "BGS_Treasures_029") return 6;    // Rocking and Rolling (three free Refreshes)
                if (id == "BGS_Treasures_033") return 7;    // New Recruit (an extra tavern slot)
                if (id == "BGS_Treasures_040") return 7;    // Banana Bunch
                if (id == "BGS_Treasures_006") return 7;    // Evolving Tavern
                if (id == "BGS_Treasures_009") return ps.board.Empty() ? -5 : 5;   // Gruul Rules
                if (id == "BGS_Treasures_010") return ps.lastOpponent.Empty() ? -5 : 4;   // Time Thief
                if (id == "BGS_Treasures_012") return 7;    // On the House
                if (id == "BGS_Treasures_014") return 5;    // The Unlimited Coin
                if (id == "BGS_Treasures_018") return ps.board.Empty() ? -5 : 4;   // I'm Still Just a Rat in a Cage
                if (id == "BGS_Treasures_026") return ps.board.Empty() ? -5 : 4;   // The Bouncer
                if (id == "BGS_Treasures_030") return 5;    // Brann's Blessing
                if (id == "BGS_Treasures_036") return 5;    // Great Deal
                if (id == "BGS_Treasures_011") return 6;    // Training Session (a new hero power)
                if (id == "BGS_Treasures_015") return ps.board.Empty() ? -5 : 4;   // Buy the Holy Light
                if (id == "BGS_Treasures_019") return 8;    // B.A.N.A.N.A.S. (a full hand of Bananas)
                if (id == "BGS_Treasures_020") return 9;    // Top Shelf (a Tier 6 minion)
                if (id == "BGS_Treasures_034") return hasNonGolden() ? 5 : -5;     // Repeat Customer
                if (id == "BGS_Treasures_037") return ps.offers.empty() ? -5 : 6;  // All That Glitters
                if (id == "BGS_Treasures_039") return ps.offers.empty() ? 2 : 7;   // Mindflayer Goggles
                if (id == "BGS_Treasures_016") return hasNonGolden() ? 5 : -5;     // Raise the Stakes
                if (id == "BGS_Treasures_022") return 7;    // Friends and Family Discount
                if (id == "BGS_Treasures_023") return 6;    // Open Bar
                if (id == "BGS_Treasures_025") return ps.gold < 10 ? 8 : 1;        // Fresh Tab
                if (id == "BGS_Treasures_028") return ps.board.Empty() ? -5 : 4;   // Give A Dog A Bone
                if (id == "BGS_Treasures_032") return 9;    // Big Winner! (one prize per earlier turn)
                return 5;
            }

            int OfferValue(PlayerShop const& ps, Minion const& m, uint32_t fav)
            {
                if (DarkmoonPrizeTurnOf(m.card) != 0)
                    return PrizeValue(ps, m.card);   // the prize's own value, not a minion's
                int v = Ai::Value(m, fav);
                if ((m.flags & kMinionTicket) != 0)
                    v += 8;   // Silas Darkmoon: a third of a free Discover rides on it
                if (!m.golden)
                {
                    int const c = Copies(ps, m.card);
                    if (c >= 2)
                        v += kTripleBonus;
                    else if (c == 1)
                        v += kPairBonus;
                }
                // Spells in the hand that only fit this minion's tribe and have no target on the board (King Mukla's
                // Wild Bananas need a Beast): the first such minion unlocks them, and they cannot be sold to make room.
                // Each spell is judged on its own: one that already has a target on the board does not count.
                for (Minion const& h : ps.hand.Items())
                {
                    EffectDef const* const e = IsSpell(h) ? EffectRegistry::Global().Find(h.card) : nullptr;
                    if (!e || !e->spellNeedsTarget || e->spellTargetTribe == 0 || !m.IsTribe(e->spellTargetTribe))
                        continue;
                    bool hasTarget = false;
                    for (Minion const& b : ps.board.Items())
                        hasTarget = hasTarget || b.IsTribe(e->spellTargetTribe);
                    if (!hasTarget)
                    {
                        v += 8;
                        break;
                    }
                }
                return v;
            }

            // The tribe the player leans on most, from the board and the hand (0 when none). The AI keeps its own favourite
            // per player; the wire has only the shop state and derives it here instead.
            uint32_t DominantTribe(PlayerShop const& ps)
            {
                int score[10] = { 0 };
                auto count = [&score](Minion const& m)
                {
                    for (int t = 0; t < 9; ++t)
                        if (m.IsTribe(1u << t))
                            ++score[t];
                };
                for (Minion const& m : ps.board.Items())
                    count(m);
                for (Minion const& m : ps.hand.Items())
                    count(m);
                int best = -1;
                for (int t = 0; t < 9; ++t)
                    if (score[t] > 0 && (best < 0 || score[t] > score[best]))
                        best = t;
                return best < 0 ? 0u : (1u << best);
            }

            int Weakest(std::vector<Minion> const& list, uint32_t fav)
            {
                int w = -1;
                for (int i = 0; i < static_cast<int>(list.size()); ++i)
                    if (!IsSpell(list[static_cast<std::size_t>(i)]) &&
                        (w < 0 || Ai::Value(list[static_cast<std::size_t>(i)], fav) < Ai::Value(list[static_cast<std::size_t>(w)], fav)))
                        w = i;
                return w;
            }

            int BestOffer(PlayerShop const& ps, uint32_t fav)
            {
                int best = -1;
                for (int i = 0; i < static_cast<int>(ps.offers.size()); ++i)
                    if (best < 0 || OfferValue(ps, ps.offers[static_cast<std::size_t>(i)], fav) >
                        OfferValue(ps, ps.offers[static_cast<std::size_t>(best)], fav))
                        best = i;
                return best;
            }

            int TripleOffer(PlayerShop const& ps)
            {
                for (int i = 0; i < static_cast<int>(ps.offers.size()); ++i)
                    if (!ps.offers[static_cast<std::size_t>(i)].golden && Copies(ps, ps.offers[static_cast<std::size_t>(i)].card) >= 2)
                        return i;
                return -1;
            }

            // Where a minion stands on the board: the front of the line is on the left.
            int OrderKey(Minion const& m)
            {
                int k = m.health + m.attack / 2;
                if (m.Has(kKwTaunt))
                    k += 400;
                if (m.Has(kKwDivineShield))
                    k += 200;
                if (m.Has(kKwPoisonous | kKwVenomous))
                    k += 100;
                if (m.Has(kKwWindfury | kKwMegaWindfury))
                    k += 50;
                return k;
            }

            // How good a hero power is for a power choice (Finley, Nguyen, Galewing, Bru'kan, Tavish):
            // the fixed PowerPickValue plus how it fits the board the NPC has now.
            int PowerChoiceValue(Shop const& s, PlayerShop const& ps, std::string const& id)
            {
                int v = PowerPickValue(id) * 10;
                HeroPowerDef const* const def = s.FindPower(id);
                if (!def)
                    return v;
                if (def->activate && def->target == PowerTarget::FriendlyMinion)
                    v += ps.board.Size();
                if (def->startMinion && !ps.board.Full())
                    v += 2;
                if (!def->activate && def->summonAttack != 0)
                    v += 1;
                // The 5b choices: what matches the board at hand.
                if (id == "BG20_HERO_283p_t1")
                    v += ps.board.Empty() ? -4 : 2;
                else if (id == "BG20_HERO_283p_t3")
                    v += 2;
                else if (id == "BG22_HERO_001p_t1")
                    v += ps.board.Size();
                else if (id == "BG22_HERO_001p_t2")
                    v += ps.board.Empty() ? -3 : (ps.board[0].attack >= 5 ? 3 : 1);
                else if (id == "BG22_HERO_001p_t3")
                    v += ps.board.Empty() ? -3 : 1;
                else if (id == "BG22_HERO_001p_t4")
                    v += 1;
                else if (id == "TB_BaconShop_HP_087t")
                    v += 2;
                return v;
            }
        }

        int WantedTier(int turn, int bias)
        {
            return std::max(kMinTier, std::min(kMaxTier, (turn + 1) / 2 + bias));
        }

        int Ai::Value(Minion const& m, uint32_t fav)
        {
            if (m.card < 0)
                return 0;
            int v = (m.attack + m.health) * 2 + m.Tier() * g_tune->tierWeight;
            if (m.Has(kKwTaunt))
                v += 3;
            if (m.Has(kKwDivineShield))
                v += 2 + m.attack;
            if (m.Has(kKwPoisonous | kKwVenomous))
                v += 6;
            if (m.Has(kKwMegaWindfury))
                v += m.attack * 3;
            else if (m.Has(kKwWindfury))
                v += m.attack;
            if (m.Has(kKwReborn))
                v += 4;
            if (m.Has(kKwMagnetic))
                v += 2;
            if (fav != 0 && m.IsTribe(fav))
                v += g_tune->tribeBonus;
            if (m.golden)
                v += 4;
            // What the stats do not show: value left behind when it dies, a fight opener, a spell every turn, a minion
            // that grows the warband over the turns. Without it the NPC played the stat tribes (Demon, Pirate) well
            // and the effect tribes (Beast Deathrattles, Naga Spellcraft) badly.
            if (EffectDef const* const e = EffectRegistry::Global().Find(m.card))
            {
                int const t = m.Tier();
                if (e->deathrattle)
                    v += 2 + 2 * t;
                if (e->avenge || e->startOfCombat)
                    v += 2 + t;
                if (e->grantsSpellcraft)
                    v += 2 + 2 * t;
                if (e->onEndOfTurn || e->onStartOfTurn || e->onSpellCast || e->onFriendlyPlayed ||
                    e->onFriendlySummonedShop || e->onBloodGemPlayed || e->onBattlecryTriggered || e->onSpendGold ||
                    e->onFriendlyBuffedShop)
                    v += 2 + 2 * t;
                if (e->onFriendDied || e->onFriendSummoned || e->onDamaged)
                    v += 1 + t;
            }
            return v;
        }

        Need Ai::NeedScore(Shop const& shop, int receiver, Minion const& m, int sender, uint32_t allyFav)
        {
            Need n;
            if (receiver < 0 || receiver >= kPlayers || receiver == sender)
                return n;
            if (sender >= 0 && FactionOf(receiver) != FactionOf(sender))
                return n;
            if (m.card < 0 || CardAt(m.card).role == CardRole::Spell)
                return n;
            PlayerShop const& ps = shop.State(receiver);
            if (ps.health <= 0)
                return n;   // eliminated
            if (!shop.CanReceive(receiver, m.card, m.golden))
                return n;   // a full hand; the game would refuse the send
            uint32_t const fav = allyFav != 0 ? allyFav : DominantTribe(ps);
            n.score = Value(m, fav);
            if (!m.golden)
            {
                int const copies = Copies(ps, m.card);
                if (copies >= 2)
                {
                    n.score += kTripleBonus;   // sending it completes the ally's triple
                    n.reasons |= kNeedTriple;
                }
                else if (copies == 1)
                    n.score += kPairBonus;     // a pair: the next copy is one step closer
            }
            if (fav != 0 && m.IsTribe(fav))
                n.reasons |= kNeedTribe;
            if (!ps.board.Full())
                n.reasons |= kNeedUpgrade | kNeedBody;   // any minion is a body in a free slot
            else
            {
                int const w = Weakest(ps.board.Items(), fav);
                if (w >= 0 && Value(m, fav) > Value(ps.board[static_cast<std::size_t>(w)], fav) + kSwapMargin)
                    n.reasons |= kNeedUpgrade; // a clear replacement for the ally's weakest minion
            }
            return n;
        }

        // What PlayTurn works with: the game, its shop, the player and the favourite tribe of this turn.
        struct Ai::Ctx
        {
            Game& g;
            Shop& s;
            int p;
            uint32_t fav;
            PlayerShop& State() { return s.State(p); }
        };

        void Ai::Count(ActionResult r)
        {
            ++m_stats.actions;
            if (r != ActionResult::Ok)
                ++m_stats.refused[static_cast<int>(r)];
        }

        void Ai::ChooseFavourite(Game& g, int p)
        {
            PlayerShop const& ps = g.GetShop().State(p);
            int held[16] = { 0 };
            int score[16] = { 0 };
            std::vector<uint32_t> const& tribes = g.FactionTribes(FactionOf(p));
            int const n = std::min(static_cast<int>(tribes.size()), 16);
            for (int t = 0; t < n; ++t)
            {
                score[t] += m_taste[p][t];
                uint32_t const bit = tribes[static_cast<std::size_t>(t)];
                for (Minion const& m : ps.board.Items())
                    if (m.IsTribe(bit))
                    {
                        score[t] += 3;
                        ++held[t];
                    }
                for (Minion const& m : ps.hand.Items())
                    if (m.IsTribe(bit))
                    {
                        score[t] += 3;
                        ++held[t];
                    }
                for (Minion const& m : ps.offers)
                    if (m.IsTribe(bit))
                        score[t] += 1;
            }
            int cur = -1;
            for (int t = 0; t < n; ++t)
                if (tribes[static_cast<std::size_t>(t)] == m_fav[p])
                    cur = t;
            if (cur >= 0 && (held[cur] >= m_tune.lockHeld || g.Round() > m_tune.lockRound))
                return;
            int best = 0;
            for (int t = 1; t < n; ++t)
                if (score[t] > score[best])
                    best = t;
            if (n > 0)
                m_fav[p] = tribes[static_cast<std::size_t>(best)];
        }

        void Ai::Arrange(Ctx& c)
        {
            for (int i = 0; i + 1 < c.State().board.Size(); ++i)
            {
                PlayerShop const& ps = c.State();
                int best = i;
                for (int j = i + 1; j < ps.board.Size(); ++j)
                    if (OrderKey(ps.board[j]) > OrderKey(ps.board[best]))
                        best = j;
                if (best != i)
                {
                    ActionResult const r = c.g.Move(c.p, best, i);
                    Count(r);
                    if (r != ActionResult::Ok)
                        return;
                }
            }
        }

        void Ai::PlayTurn(Game& g, int p)
        {
            if (m_owner[p] != &g || g.Round() < m_lastRound[p])
            {
                m_owner[p] = &g;
                m_fav[p] = 0;
                for (int& t : m_taste[p])
                    t = m_tune.tribeVariety > 0 ? static_cast<int>(g.Random().Uniform(
                        static_cast<uint32_t>(m_tune.tribeVariety + 1))) : 0;
                m_bias[p] = m_tune.tierShift;
                if (m_tune.biasSpread > 1)
                    m_bias[p] += static_cast<int>(g.Random().Uniform(static_cast<uint32_t>(m_tune.biasSpread))) -
                        m_tune.biasSpread / 2;
            }
            m_lastRound[p] = g.Round();
            ++m_stats.turns;
            struct TuneScope
            {
                explicit TuneScope(AiTuning const* t) { g_tune = t; }
                ~TuneScope() { g_tune = &kDefaultTuning; }
            } const tuneScope(&m_tune);
            ChooseFavourite(g, p);
            Ctx c{ g, g.GetShop(), p, m_fav[p] };

            auto discover = [&]
            {
                while (c.s.DiscoverOpen(p))
                {
                    PlayerShop const& ps = c.State();
                    int best = 0;
                    if (!ps.powerChoices.empty())
                    {
                        // A hero-power choice (Finley, Nguyen, Galewing, Bru'kan, Tavish).
                        for (int i = 1; i < static_cast<int>(ps.powerChoices.size()); ++i)
                            if (PowerChoiceValue(c.s, ps, ps.powerChoices[static_cast<std::size_t>(i)]) >
                                PowerChoiceValue(c.s, ps, ps.powerChoices[static_cast<std::size_t>(best)]))
                                best = i;
                    }
                    else
                    {
                        for (int i = 1; i < static_cast<int>(ps.options.size()); ++i)
                            if (OfferValue(ps, ps.options[static_cast<std::size_t>(i)], c.fav) >
                                OfferValue(ps, ps.options[static_cast<std::size_t>(best)], c.fav))
                                best = i;
                    }
                    ActionResult const r = g.Choose(p, best);
                    Count(r);
                    if (r != ActionResult::Ok)
                        break;
                }
            };

            // Casts every spell in the hand (Gold Coins, Bananas, Spellcraft). A spell that needs a minion
            // is tried on the best board minion first, then on the others (Wild Banana wants a Beast).
            auto castSpells = [&]
            {
                for (int guard = 0; guard < 16; ++guard)
                {
                    PlayerShop const& ps = c.State();
                    bool cast = false;
                    for (int idx = 0; idx < ps.hand.Size() && !cast; ++idx)
                    {
                        if (!IsSpell(ps.hand[idx]))
                            continue;
                        int const card = ps.hand[idx].card;
                        EffectDef const* const def = c.s.Effects().Find(card);
                        bool const needsTarget = def && def->spellNeedsTarget;
                        ActionResult r = ActionResult::Ok;
                        if (!needsTarget)
                            r = g.Cast(p, idx, 0);
                        else
                        {
                            std::vector<uint32_t> targets;
                            for (Minion const& m : ps.board.Items())
                                if (def->spellTargetTribe == 0 || m.IsTribe(def->spellTargetTribe))
                                    if (!def->spellTargetNotGolden || !m.golden)
                                        targets.push_back(m.uid);
                            std::sort(targets.begin(), targets.end(), [&](uint32_t a, uint32_t b)
                            {
                                Minion const* ma = c.s.FindMinion(p, a);
                                Minion const* mb = c.s.FindMinion(p, b);
                                return ma && mb && Value(*ma, c.fav) > Value(*mb, c.fav);
                            });
                            if (targets.empty())
                                continue;   // no valid target now: keep this one for later
                            r = g.Cast(p, idx, targets[0]);
                        }
                        if (r == ActionResult::BadTarget)
                            continue;
                        Count(r);
                        if (r != ActionResult::Ok)
                            return;   // a rule refusal: the turn cannot go on with this action
                        cast = true;
                    }
                    if (!cast)
                        return;
                    discover();
                }
            };

            // Plays hand minions left to right while the board has room: a Magnetic minion onto the best Mech, a
            // battlecry onto the most valuable friendly minion (the game refuses a target the card cannot use).
            auto playAll = [&]
            {
                for (int guard = 0; guard < 24; ++guard)
                {
                    PlayerShop const& ps = c.State();
                    if (ps.hand.Empty() || ps.board.Full())
                        return;
                    // A minion without a Battlecry first: most Battlecries count or buff what is already on the
                    // board ("your other Murlocs +3/+3", Swolefin), so they come last.
                    int hi = -1;
                    for (int pass = 0; pass < 2 && hi < 0; ++pass)
                        for (int i = 0; i < ps.hand.Size(); ++i)
                        {
                            if (IsSpell(ps.hand[i]))
                                continue;
                            EffectDef const* const e = EffectRegistry::Global().Find(ps.hand[i].card);
                            if (pass == 0 && e && e->battlecry)
                                continue;
                            hi = i;
                            break;
                        }
                    if (hi < 0)
                        return;
                    uint32_t target = 0;
                    bool const magnetic = (ps.hand[hi].keywords & kKwMagnetic) != 0;
                    int bestValue = -1;
                    for (int i = 0; i < ps.board.Size(); ++i)
                    {
                        if (magnetic && !ps.board[i].IsTribe(kTribeMech))
                            continue;
                        int const v = Value(ps.board[i], c.fav);
                        if (v > bestValue)
                        {
                            bestValue = v;
                            target = ps.board[i].uid;
                        }
                    }
                    int const slot = ps.board.Size();
                    ActionResult r = g.Play(p, hi, slot, target);
                    if (r == ActionResult::BadTarget)
                        r = g.Play(p, hi, slot, 0);   // probing for a target is not a mistake
                    Count(r);
                    if (r != ActionResult::Ok)
                        return;
                    discover();
                    castSpells();
                }
            };

            discover();
            castSpells();
            playAll();
            // Use the hero power when it helps (a target power picks the best NON-golden board minion,
            // so a once-per-game power such as Reno's is not wasted). Before the buys (afterBuys false) a paid power
            // only spends the gold the turn's due upgrade does not need; after them it takes what is left.
            auto usePower = [&](bool afterBuys)
            {
                PlayerShop const& ps = c.State();
                if (ps.hero < 0 || ps.hero >= HeroCount())
                    return;
                HeroPowerDef const* const def = c.s.Power(p);
                if (!def || !def->activate)
                    return;
                if (def->oncePerGame && ps.power.Uses(def->id) > 0)
                    return;
                if (def->usesPerGame > 0 && ps.power.Uses(def->id) >= def->usesPerGame)
                    return;
                if (def->usesPerTurn > 0 ? ps.power.usesThisTurn >= def->usesPerTurn
                                        : (def->oncePerTurn && ps.power.usesThisTurn > 0))
                    return;
                if (def->armsCombat && ps.power.combatArmed)
                    return;   // Y'Shaarj / Rafaam: the next combat is already armed
                if (std::string(def->id) == "TB_BaconShop_HP_049" && ps.health <= 8)
                    return;   // Lich Baz'hial: stealing costs health it cannot spare
                if (std::string(def->id) == "TB_BaconShop_HP_102" && c.s.PairForWish(p) < 0)
                    return;   // Zephrys: no pair, no wish
                if (std::string(def->id) == "BG23_HERO_303p2" &&
                    (!c.s.LastOpponentBoard(p) || c.s.LastOpponentBoard(p)->Empty()))
                    return;   // Murloc Holmes: nothing to look at
                if ((std::string(def->id) == "TB_BaconShop_HP_077" || std::string(def->id) == "BG21_HERO_010p") &&
                    (!c.s.LastOpponentBoard(p) || c.s.LastOpponentBoard(p)->Empty()))
                    return;   // Tess Greymane / Scabbs Cutterbutter: no warband yet
                if (def->stayFrosty && ps.gold >= c.s.BuyCost(p))
                    return;   // Sindragosa: freeze only what it cannot buy this turn, after the buys
                if (def->target == PowerTarget::None && def->targetOk && !def->targetOk(c.s, p, 0))
                    return;   // Akazamzarak with the secret cap reached (and any other refused no-target power)
                int const cost = HeroPowerCost(*def, ps.power);
                if (ps.gold < cost)
                    return;
                if (!afterBuys && cost > 0 && ps.tier < kMaxTier && ps.tier < WantedTier(g.Round(), m_bias[p]) &&
                    ps.gold - cost < c.s.UpgradeCost(p))
                    return;   // the upgrade comes first (Elise, Blackthorn and every paid power fell behind on tiers)
                if (def->costGrowthPerUse > 0 && cost > c.s.BuyCost(p))
                    return;   // Elise: a Discover that costs more than buying a minion is not worth it
                if (def->pot && !ps.board.Full())
                    return;   // Cookie: only a minion that would be sold for a better one goes in the pot
                if (ps.hand.Size() > kHandMax - 3)
                    return;   // keep room for what the power gives and a buy (a spell cannot be sold to make room)
                if (std::string(def->id) == "BG20_HERO_103p" && ps.board.Empty())
                    return;   // Blackthorn: Blood Gems need a minion to land on, or they jam the hand
                uint32_t target = 0;
                if (def->target == PowerTarget::FriendlyMinion)
                {
                    if (ps.board.Empty())
                        return;
                    if (def->consumesTarget && ps.board.Size() < 2)
                        return;   // Hooktusk / Cookie: do not throw away the only minion
                    int best = -1;
                    for (int i = 0; i < ps.board.Size(); ++i)
                    {
                        if (ps.board[i].golden)
                            continue;
                        if (def->targetOk && !def->targetOk(c.s, p, ps.board[i].uid))
                            continue;
                        bool const devour = std::string(def->id) == "BG20_HERO_301p";
                        bool const swap = std::string(def->id) == "TB_BaconShop_HP_084";   // Jandice: traded away
                        bool const low = devour || swap || def->consumesTarget;   // the weakest minion goes
                        if (best < 0 || (low ? Value(ps.board[i], c.fav) < Value(ps.board[best], c.fav)
                                             : Value(ps.board[i], c.fav) > Value(ps.board[best], c.fav)))
                            best = i;
                    }
                    if (best < 0)
                        return;   // nothing the power would change: keep the use
                    if (std::string(def->id) == "TB_BaconShop_HP_084")
                    {
                        // Jandice gets a RANDOM tavern minion: swap only when the tavern is better on average
                        int sum = 0;
                        for (Minion const& o : ps.offers)
                            sum += Value(o, c.fav);
                        if (ps.offers.empty() || sum <= Value(ps.board[best], c.fav) * static_cast<int>(ps.offers.size()))
                            return;
                    }
                    target = ps.board[best].uid;
                }
                int offer = -1;
                uint32_t second = 0;
                if (def->target == PowerTarget::Seat)
                {
                    // Lord Barov: guess the living player with the highest Tavern Tier (the likeliest winner).
                    for (int q = 0; q < kPlayers; ++q)
                        if (g.Alive(q) && (offer < 0 || c.s.State(q).tier > c.s.State(offer).tier))
                            offer = q;
                    if (offer < 0)
                        return;
                }
                else if (def->target == PowerTarget::TwoMinions)
                {
                    // Vol'jin: the weakest board minion takes the stats of the strongest tavern minion, when that is a gain.
                    int weak = -1;
                    for (int i = 0; i < ps.board.Size(); ++i)
                        if (weak < 0 || ps.board[i].attack + ps.board[i].health < ps.board[weak].attack + ps.board[weak].health)
                            weak = i;
                    int strong = -1;
                    for (int i = 0; i < static_cast<int>(ps.offers.size()); ++i)
                        if (strong < 0 || ps.offers[static_cast<std::size_t>(i)].attack + ps.offers[static_cast<std::size_t>(i)].health >
                                              ps.offers[static_cast<std::size_t>(strong)].attack + ps.offers[static_cast<std::size_t>(strong)].health)
                            strong = i;
                    if (weak < 0 || strong < 0)
                        return;
                    Minion const& o = ps.offers[static_cast<std::size_t>(strong)];
                    if (o.attack + o.health < ps.board[weak].attack + ps.board[weak].health + 4)
                        return;   // not worth the use
                    target = ps.board[weak].uid;
                    second = o.uid;
                }
                if (def->target == PowerTarget::TavernOffer && def->stayFrosty)
                {
                    // Sindragosa: the best offer that is not Frozen yet
                    for (int i = 0; i < static_cast<int>(ps.offers.size()); ++i)
                    {
                        Minion const& o = ps.offers[static_cast<std::size_t>(i)];
                        if ((o.flags & kMinionFrozen) != 0)
                            continue;
                        if (offer < 0 || OfferValue(ps, o, c.fav) > OfferValue(ps, ps.offers[static_cast<std::size_t>(offer)], c.fav))
                            offer = i;
                    }
                    if (offer < 0)
                        return;
                }
                else if (def->target == PowerTarget::TavernOffer)
                {
                    // the best offer whose Tier the hero can afford in health (Lich Baz'hial); Maiev only holds it
                    for (int i = 0; i < static_cast<int>(ps.offers.size()); ++i)
                    {
                        Minion const& o = ps.offers[static_cast<std::size_t>(i)];
                        if (!def->holdsOffer && o.Tier() >= ps.health + ps.armor - 4)
                            continue;
                        if (offer < 0 || Value(o, c.fav) > Value(ps.offers[static_cast<std::size_t>(offer)], c.fav))
                            offer = i;
                    }
                    if (offer < 0)
                        return;
                }
                ActionResult const r = g.HeroPower(p, target, offer, second);
                Count(r);
                if (r == ActionResult::Ok)
                    ++m_stats.powerUses;
            };
            usePower(false);
            usePower(false);   // a twice-per-turn power (Malygos); every other power refuses the second use
            discover();

            // A card the sender itself still wants: a pair or triple of its own, or a strong fit for its tribe.
            auto senderNeeds = [&](Minion const& m) -> bool
            {
                if (m.golden || m.card < 0)
                    return false;
                if (Copies(c.State(), m.card) >= 2)
                    return true;   // its own pair (this copy plus another) is still a triple in the making
                return c.fav != 0 && m.IsTribe(c.fav) && Value(m, c.fav) >= 28;
            };

            // Send a hand minion to the ally that gains the most from it (the game allows one send per turn).
            // `minGain`: the need must beat the card's value for the sender by this much (0 = the overflow path,
            // which only asks whether the ally wants it at all).
            auto trySend = [&](int handIndex, int minGain) -> bool
            {
                if (c.State().sentThisTurn)
                    return false;
                if (handIndex < 0 || handIndex >= c.State().hand.Size() || IsSpell(c.State().hand[handIndex]))
                    return false;
                Minion const& m = c.State().hand[handIndex];
                if (senderNeeds(m))
                    return false;
                int ally = -1;
                Need best;
                for (int q = 0; q < kPlayers; ++q)
                {
                    if (q == p || FactionOf(q) != FactionOf(p) || !g.Alive(q))
                        continue;
                    Need const n = NeedScore(c.s, q, m, p, m_fav[q]);
                    if (n.score > best.score)
                    {
                        best = n;
                        ally = q;
                    }
                }
                if (ally < 0 || best.score < kSendNeedMin)
                    return false;
                if (best.score - Value(m, c.fav) < minGain)
                    return false;
                int const copiesBefore = m.golden ? 0 : Copies(c.s.State(ally), m.card);
                ActionResult const r = g.Send(p, handIndex, ally);
                Count(r);
                if (r == ActionResult::Ok)
                {
                    ++m_stats.sends;
                    if (copiesBefore >= 2)
                        ++m_stats.sendTriples;
                    return true;
                }
                return false;
            };

            // Before buying and playing: the best card in the hand that an ally needs much more than the sender does.
            // A card that completes an ally's triple comes first, then a clear tribe or board upgrade; the gain over
            // what the card is worth to the sender breaks the tie.
            auto tryProactiveSend = [&]() -> bool
            {
                if (c.State().sentThisTurn)
                    return false;
                int best = -1;
                int bestRank = -1;
                int bestGain = 0;
                for (int i = 0; i < c.State().hand.Size(); ++i)
                {
                    Minion const& m = c.State().hand[i];
                    if (IsSpell(m) || senderNeeds(m))
                        continue;
                    Need need;
                    for (int q = 0; q < kPlayers; ++q)
                    {
                        if (q == p || FactionOf(q) != FactionOf(p) || !g.Alive(q))
                            continue;
                        Need const n = NeedScore(c.s, q, m, p, m_fav[q]);
                        if (n.score > need.score)
                            need = n;
                    }
                    int const gain = need.score - Value(m, c.fav);
                    int const rank = (need.reasons & kNeedTriple) != 0 ? 2
                        : (need.reasons & (kNeedTribe | kNeedUpgrade)) != 0 ? 1 : 0;
                    if (need.score >= kSendNeedMin && gain >= kSendGainMin
                        && (rank > bestRank || (rank == bestRank && gain > bestGain)))
                    {
                        bestRank = rank;
                        bestGain = gain;
                        best = i;
                    }
                }
                return best >= 0 && trySend(best, kSendGainMin);
            };

            // One send per turn: a card an ally needs much more than the sender is worth more to the team than to its hand.
            tryProactiveSend();

            int refreshes = 0;
            int const refreshLimit = 2 + g.Round() / 3 + m_tune.refreshExtra;
            int partnerBuys = 0;   // at most one buy per turn that is only for an ally

            // A bonus for a tavern offer that completes an ally's triple (or is a big upgrade for them) and is of
            // little use to the sender. It buys the card so the proactive send can hand it over next turn.
            auto partnerBonus = [&](Minion const& m) -> int
            {
                if (partnerBuys >= 1 || m.golden || m.card < 0)
                    return 0;
                int const own = Value(m, c.fav);
                if (own >= 40)
                    return 0;   // it is not "of little use to me"
                for (int q = 0; q < kPlayers; ++q)
                {
                    if (q == p || FactionOf(q) != FactionOf(p) || !g.Alive(q))
                        continue;
                    Need const n = NeedScore(c.s, q, m, p, m_fav[q]);
                    if (n.score < kSendNeedMin)
                        continue;
                    if ((n.reasons & kNeedTriple) != 0)
                        return kTripleBonus;
                    if ((n.reasons & kNeedUpgrade) != 0 && n.score >= own + kSendGainMin + 20)
                        return n.score;   // a clear board upgrade for the ally
                }
                return 0;
            };
            for (int it = 0; it < 80; ++it)
            {
                PlayerShop& ps = c.State();

                // 1. A third copy in the tavern: buy it first.
                int const triple = TripleOffer(ps);
                if (triple >= 0 && ps.gold >= c.s.BuyCost(p) && !ps.hand.Full())
                {
                    int const goldens = static_cast<int>(std::count_if(ps.board.Items().begin(), ps.board.Items().end(),
                        [](Minion const& m) { return m.golden; }));
                    ActionResult const r = g.Buy(p, triple);
                    Count(r);
                    if (r == ActionResult::Ok)
                    {
                        ++m_stats.buys;
                        int const after = static_cast<int>(std::count_if(ps.board.Items().begin(), ps.board.Items().end(),
                            [](Minion const& m) { return m.golden; })) +
                            static_cast<int>(std::count_if(ps.hand.Items().begin(), ps.hand.Items().end(),
                            [](Minion const& m) { return m.golden; }));
                        if (after > goldens)
                            ++m_stats.triples;
                        castSpells();
                        playAll();
                        discover();
                        continue;
                    }
                }

                // 2. The tavern upgrade, when it is due or nearly free.
                if (ps.tier < kMaxTier && ps.gold >= c.s.UpgradeCost(p) &&
                    (ps.tier < WantedTier(g.Round(), m_bias[p]) || c.s.UpgradeCost(p) <= m_tune.cheapUpgrade))
                {
                    ActionResult const r = g.Upgrade(p);
                    Count(r);
                    if (r == ActionResult::Ok)
                    {
                        ++m_stats.upgrades;
                        continue;
                    }
                }

                // 3. The hand is stuck behind a full board: swap in the better minion, or sell the weaker one.
                if (!ps.hand.Empty() && ps.board.Full())
                {
                    int const wb = Weakest(ps.board.Items(), c.fav);
                    int const wh = Weakest(ps.hand.Items(), c.fav);
                    int bh = 0;
                    for (int i = 1; i < ps.hand.Size(); ++i)
                        if (!IsSpell(ps.hand[i]) && Value(ps.hand[i], c.fav) > Value(ps.hand[bh], c.fav))
                            bh = i;
                    if (wh < 0)
                    {
                        // only spells in the hand: cast what can be cast, then give up this turn
                        castSpells();
                        if (!ps.hand.Empty() && ps.board.Full())
                            break;
                        continue;
                    }
                    if (OfferValue(ps, ps.hand[bh], c.fav) > Value(ps.board[wb], c.fav) + m_tune.swapMargin)
                    {
                        ActionResult const r = g.Sell(p, Zone::Board, wb);
                        Count(r);
                        if (r == ActionResult::Ok)
                            ++m_stats.sells;
                        castSpells();
                        playAll();
                        discover();
                    }
                    else if (trySend(wh, 0))
                    {
                        // the weak hand minion helped an ally instead of being sold
                    }
                    else
                    {
                        ActionResult const r = g.Sell(p, Zone::Hand, wh);
                        Count(r);
                        if (r == ActionResult::Ok)
                            ++m_stats.sells;
                    }
                    continue;
                }

                // 4. Buy the best offer when it is worth a slot (or an ally needs it and the sender barely does).
                if (ps.gold >= c.s.BuyCost(p) && !ps.offers.empty())
                {
                    int best = -1;
                    int bestValue = 0;
                    int bestPartner = 0;
                    for (int i = 0; i < static_cast<int>(ps.offers.size()); ++i)
                    {
                        int const v = OfferValue(ps, ps.offers[static_cast<std::size_t>(i)], c.fav);
                        int const pb = partnerBonus(ps.offers[static_cast<std::size_t>(i)]);
                        if (best < 0 || v + pb > bestValue + bestPartner)
                        {
                            best = i;
                            bestValue = v;
                            bestPartner = pb;
                        }
                    }
                    int const combined = bestValue + bestPartner;
                    bool const worth = !ps.board.Full() ||
                        combined > Value(ps.board[Weakest(ps.board.Items(), c.fav)], c.fav) + m_tune.swapMargin;
                    if (worth)
                    {
                        ActionResult const r = g.Buy(p, best);
                        if (r == ActionResult::HandFull)
                        {
                            // Make room with the weakest hand minion if the offer beats it.
                            int const wh = Weakest(ps.hand.Items(), c.fav);
                            if (wh < 0)
                            {
                                castSpells();
                                continue;
                            }
                            if (combined > Value(ps.hand[wh], c.fav))
                            {
                                if (trySend(wh, 0))
                                    continue;
                                ActionResult const sr = g.Sell(p, Zone::Hand, wh);
                                Count(sr);
                                if (sr == ActionResult::Ok)
                                {
                                    ++m_stats.sells;
                                    continue;
                                }
                            }
                        }
                        else
                        {
                            Count(r);
                            if (r == ActionResult::Ok)
                            {
                                ++m_stats.buys;
                                if (bestPartner > 0)
                                    ++partnerBuys;
                                castSpells();
                                playAll();
                                discover();
                                continue;
                            }
                        }
                    }
                }

                // 5. Gold that cannot buy anything: look at new offers while a purchase is still possible afterwards.
                if (refreshes < refreshLimit && ps.gold >= c.s.BuyCost(p) + kRefreshCost)
                {
                    ++refreshes;
                    ActionResult const r = g.Refresh(p);
                    Count(r);
                    if (r == ActionResult::Ok)
                    {
                        ++m_stats.refreshes;
                        continue;
                    }
                }
                break;
            }
            discover();
            castSpells();
            playAll();
            usePower(true);   // again after the buys: a power that grows with them (Edwin) is refused before the first one
            usePower(true);
            discover();
            castSpells();
            playAll();
            Arrange(c);

            // Freeze a tavern worth keeping that the player could not afford this turn.
            PlayerShop const& ps = c.State();
            if (!ps.offers.empty() && ps.gold < c.s.BuyCost(p))
            {
                bool keep = TripleOffer(ps) >= 0;
                if (!keep && !ps.board.Empty())
                {
                    int const best = BestOffer(ps, c.fav);
                    keep = OfferValue(ps, ps.offers[static_cast<std::size_t>(best)], c.fav) >
                        Value(ps.board[Weakest(ps.board.Items(), c.fav)], c.fav) + 10;
                }
                if (keep)
                {
                    ActionResult const r = g.Freeze(p, true);
                    Count(r);
                    if (r == ActionResult::Ok)
                        ++m_stats.freezes;
                }
            }

            // An NPC plays a little toward its quest. Track the Footprints (refresh 10) refreshes up to twice more,
            // until the quest is done.
            QuestState const* footprints = nullptr;   // Denathrius may hold it in his second slot
            for (QuestState const* q : { &ps.quest, &ps.quest2 })
                if (q->quest >= 0 && !q->done && std::string(QuestAt(q->quest).id) == "BG24_Quest_112")
                    footprints = q;
            if (footprints)
                for (int k = 0; k < 2 && !footprints->done; ++k)
                {
                    ActionResult const r = g.Refresh(p);
                    Count(r);
                    if (r != ActionResult::Ok)
                        break;
                    ++m_stats.refreshes;
                }
        }
    }
}
