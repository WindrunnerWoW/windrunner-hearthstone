#include "HsBgHeroes.h"

#include "HsBgShop.h"

#include <algorithm>

namespace Hs
{
    namespace Bg
    {
        void HeroPowerRegistry::Register(HeroPowerDef const& def)
        {
            if (def.id[0] == '\0')
            {
                m_errors.push_back("hero power with an empty id");
                return;
            }
            if (m_defs.find(def.id) != m_defs.end())
            {
                m_errors.push_back(std::string(def.id) + ": duplicate hero power");
                return;
            }
            m_defs[def.id] = def;
        }

        HeroPowerDef const* HeroPowerRegistry::Find(std::string const& powerId) const
        {
            auto it = m_defs.find(powerId);
            return it == m_defs.end() ? nullptr : &it->second;
        }

        HeroPowerRegistry const& HeroPowerRegistry::Global()
        {
            static HeroPowerRegistry const reg = []()
            {
                HeroPowerRegistry r;
                RegisterAllHeroPowers(r);
                return r;
            }();
            return reg;
        }

        int HeroPowerCost(HeroPowerDef const& def, PowerState const& state)
        {
            if (!def.activate)
                return 0;
            int const cost = def.cost + def.costGrowthPerUse * state.Uses(def.id) - state.powerCostDiscount;
            return std::max(0, cost);
        }

        // ------------------------------------------------------------------------------------------ active powers

        namespace
        {
            void ActKragg(Shop& s, int p, PowerState&, uint32_t, int)
            {
                s.AddGold(p, std::max(1, s.Turn()));   // 1 Gold, +1 per turn (once per game)
            }

            void ActToki(Shop& s, int p, PowerState&, uint32_t, int)
            {
                s.RefreshOffers(p);          // a free refresh
                s.DrawHigherTierOffer(p);    // and two minions from a higher tier
                s.DrawHigherTierOffer(p);
            }

            void ActElise(Shop& s, int p, PowerState&, uint32_t, int)
            {
                int const t = s.TavernTier(p);
                s.Discover(p, t, t, 0);
            }

            void ActGeorge(Shop& s, int p, PowerState&, uint32_t uid, int)
            {
                if (Minion* m = s.FindMinion(p, uid))
                    s.GiveKeywords(p, *m, kKwDivineShield);
            }

            // Bloodfury (live text): the chosen friendly Demon consumes a random minion in Bob's Tavern and gains its
            // stats (the offer leaves the tavern, its pool copy returns), like Mind Muck's Battlecry.
            void ActJaraxxus(Shop& s, int p, PowerState&, uint32_t uid, int)
            {
                std::vector<Minion>& offers = s.Offers(p);
                if (offers.empty())
                    return;
                int const index = static_cast<int>(s.Random().Uniform(static_cast<uint32_t>(offers.size())));
                Minion eaten;
                if (!s.ConsumeOffer(p, index, eaten))
                    return;
                if (Minion* m = s.FindMinion(p, uid))
                    s.Buff(p, *m, eaten.attack, eaten.health);
            }

            // Sharpen Blades: +1/+1, one more for every 4 minions bought this game ("Upgrades after you buy 4 minions").
            void ActEdwin(Shop& s, int p, PowerState& st, uint32_t uid, int)
            {
                int const n = 1 + st.buysThisGame / 4;
                if (Minion* m = s.FindMinion(p, uid))
                    s.Buff(p, *m, n, n);
            }

            // Tavern Lighting: stats equal to the player's Tavern Tier.
            void ActRakanishu(Shop& s, int p, PowerState&, uint32_t uid, int)
            {
                if (Minion* m = s.FindMinion(p, uid))
                {
                    int const t = s.TavernTier(p);
                    s.Buff(p, *m, t, t);
                }
            }

            // Graveyard Shift: steal the chosen tavern minion (to the hand, free) and take damage equal to its Tier
            // (armor first). Shop::HeroPower checked the index and the hand room.
            void ActBazhial(Shop& s, int p, PowerState&, uint32_t, int offerIndex)
            {
                std::vector<Minion> const& offers = s.Offers(p);
                if (offerIndex < 0 || offerIndex >= static_cast<int>(offers.size()))
                    return;
                int const tier = offers[static_cast<std::size_t>(offerIndex)].Tier();
                if (s.TakeOffer(p, offerIndex))
                    s.DamageHero(p, tier);   // armor first
            }

            bool GeorgeOk(Shop& s, int p, uint32_t uid)
            {
                Minion* m = s.FindMinion(p, uid);
                return m && !(m->keywords & kKwDivineShield);
            }

            // A Demon and something in the tavern to consume; otherwise refused, so the gold is not spent for nothing.
            bool JaraxxusOk(Shop& s, int p, uint32_t uid)
            {
                Minion* m = s.FindMinion(p, uid);
                return m && m->IsTribe(kTribeDemon) && !s.Offers(p).empty();
            }

            bool RenoOk(Shop& s, int p, uint32_t uid)
            {
                Minion* m = s.FindMinion(p, uid);
                return m && !m->golden && m->card >= 0 && CardAt(m->card).goldenId[0] != '\0';
            }

            void ActReno(Shop& s, int p, PowerState&, uint32_t uid, int)
            {
                s.MakeGolden(p, uid);   // no Triple Reward (C: Reno)
            }

            // Reborn Rites: Reborn until the owner's next StartTurn (which removes it again, like Stealth).
            bool LichKingOk(Shop& s, int p, uint32_t uid)
            {
                Minion* m = s.FindMinion(p, uid);
                return m && !(m->keywords & kKwReborn);
            }

            void ActLichKing(Shop& s, int p, PowerState&, uint32_t uid, int)
            {
                if (Minion* m = s.FindMinion(p, uid))
                    s.GiveKeywords(p, *m, kKwReborn);
            }

            // Brick by Brick: +2 Health, one more for every turn the power was not used.
            void ActPyramad(Shop& s, int p, PowerState& st, uint32_t uid, int)
            {
                if (Minion* m = s.FindMinion(p, uid))
                    s.Buff(p, *m, 0, 2 + st.healthBuffBonus);
            }

            // ---- Pirates and the simple heroes ----

            // Pirate Parrrrty!: get a Pirate. The discount for bought Pirates is in Shop::Buy.
            void ActPatches(Shop& s, int p, PowerState&, uint32_t, int)
            {
                s.AddRandomToHand(p, kMinTier, s.TavernTier(p), kTribePirate);
            }

            // The Perfect Crime: steal all minions in Bob's Tavern (until the hand is full).
            void ActTogwaggle(Shop& s, int p, PowerState&, uint32_t, int)
            {
                while (!s.Offers(p).empty())
                    if (!s.TakeOffer(p, 0))
                        break;
            }

            // Buried Treasure: dig; after five digs, a random Golden minion joins the hand.
            void ActEudora(Shop& s, int p, PowerState& st, uint32_t, int)
            {
                if (++st.digs >= 5)
                {
                    st.digs = 0;
                    s.AddRandomGoldenToHand(p, kMinTier, s.TavernTier(p), 0);
                }
            }

            // For the Horde!: give a minion in the Tavern +2/+1, doubled every three turns.
            void ActSaurfang(Shop& s, int p, PowerState&, uint32_t, int offerIndex)
            {
                std::vector<Minion>& offers = s.Offers(p);
                if (offerIndex < 0 || offerIndex >= static_cast<int>(offers.size()))
                    return;
                int const doublings = std::min(8, s.Turn() / 3);
                int const mult = 1 << doublings;
                s.Buff(p, offers[static_cast<std::size_t>(offerIndex)], 2 * mult, mult);
            }

            // A Tale of Kings: Discover a minion of this turn's type; Shop::StartTurn swaps the type.
            void ActRatKing(Shop& s, int p, PowerState& st, uint32_t, int)
            {
                if (st.ratKingTribe != 0)
                    s.Discover(p, kMinTier, s.TavernTier(p), st.ratKingTribe);
            }

            // Natural Balance: a friendly minion of each Tavern Tier gets +2/+1.
            void ActGuff(Shop& s, int p, PowerState&, uint32_t, int)
            {
                Board& b = s.PlayerBoard(p);
                std::vector<char> used(static_cast<std::size_t>(b.Size()), 0);
                for (int tier = kMinTier; tier <= kMaxTier; ++tier)
                {
                    std::vector<int> candidates;
                    for (int i = 0; i < b.Size(); ++i)
                        if (!used[static_cast<std::size_t>(i)] && b[i].Tier() == tier)
                            candidates.push_back(i);
                    if (candidates.empty())
                        continue;
                    int const i = candidates[s.Random().Uniform(static_cast<uint32_t>(candidates.size()))];
                    used[static_cast<std::size_t>(i)] = 1;
                    s.Buff(p, b[i], 2, 1);
                }
            }

            // Conviction: two random friendly minions +1/+1. After each Tavern upgrade the player chooses an improvement:
            // United Stance (one more minion), Offensive Stance (+2 Attack to each buff), Defensive Stance (+2 Health).
            void ActCariel(Shop& s, int p, PowerState& st, uint32_t, int)
            {
                Board& b = s.PlayerBoard(p);
                std::vector<int> idx;
                for (int i = 0; i < b.Size(); ++i)
                    idx.push_back(i);
                s.Random().Shuffle(idx);
                int const n = std::min(static_cast<int>(idx.size()), 2 + st.stanceUnited);
                for (int k = 0; k < n; ++k)
                    s.Buff(p, b[idx[static_cast<std::size_t>(k)]], 1 + 2 * st.stanceOffense, 1 + 2 * st.stanceDefense);
            }

            // Saturday C'Thuns!: the end-of-turn buff runs when the power was used; it grows each turn.
            void ActCthun(Shop&, int, PowerState& st, uint32_t, int)
            {
                st.cthunThisTurn = true;
            }

            // ---- Naga and the coin heroes ----

            // Relics of the Deep: Discover a Spellcraft spell of the player's Tier or lower; its effect is permanent.
            void ActVashj(Shop& s, int p, PowerState&, uint32_t, int)
            {
                s.DiscoverSpell(p, s.TavernTier(p), true);
            }

            // Naga Conquest: Discover a Naga.
            void ActAzshara(Shop& s, int p, PowerState&, uint32_t, int)
            {
                s.Discover(p, kMinTier, s.TavernTier(p), kTribeNaga);
            }

            // Friendly Wager: guess the player who wins the next combat (the chosen seat arrives as the offer index). The
            // shop learns the fights of the round (SetLastCombatWon).
            void ActBarov(Shop&, int, PowerState& st, uint32_t, int seat)
            {
                st.barovGuess = seat;
            }

            // Detective for Hire: the last opponent board is the pair to guess; a correct guess is a Gold Coin.
            void ActHolmes(Shop& s, int p, PowerState& st, uint32_t, int)
            {
                Board const* const last = s.LastOpponentBoard(p);
                if (!last || last->Empty())
                    return;
                int const pick = static_cast<int>(s.Random().Uniform(static_cast<uint32_t>(last->Size())));
                st.holmesOpponent = (*last)[pick].card;
                s.DiscoverHolmes(p, st.holmesOpponent);
            }

            // Three Wishes: the third copy of a pair goes to the hand (and completes the triple).
            void ActZephrys(Shop& s, int p, PowerState&, uint32_t, int)
            {
                int const card = s.PairForWish(p);
                if (card >= 0)
                    s.AddToHand(p, card, false);
            }

            // Arcane Alteration: replace a tavern minion with a random one of the same Tier (twice per turn).
            void ActMalygos(Shop& s, int p, PowerState&, uint32_t, int offerIndex)
            {
                std::vector<Minion> const& offers = s.Offers(p);
                if (offerIndex < 0 || offerIndex >= static_cast<int>(offers.size()))
                    return;
                int const tier = offers[static_cast<std::size_t>(offerIndex)].Tier();
                s.ReplaceOffer(p, offerIndex, tier, tier);
            }

            // Galakrond's Greed: replace the chosen tavern minion with one of a higher Tier.
            void ActGalakrond(Shop& s, int p, PowerState&, uint32_t, int offerIndex)
            {
                std::vector<Minion> const& offers = s.Offers(p);
                if (offerIndex < 0 || offerIndex >= static_cast<int>(offers.size()))
                    return;
                int const tier = offers[static_cast<std::size_t>(offerIndex)].Tier();
                if (tier < kMaxTier)
                    s.ReplaceOffer(p, offerIndex, tier + 1, kMaxTier);
            }

            // Swap, Lock, & Shop It: swap a friendly non-golden minion with a random one in the Tavern.
            void ActJandice(Shop& s, int p, PowerState&, uint32_t uid, int)
            {
                s.SwapWithRandomOffer(p, uid);
            }

            // Spirit Swap: two chosen minions (board or Tavern) exchange their stats.
            void ActVoljin(Shop& s, int p, PowerState& st, uint32_t uid, int)
            {
                s.SwapStatsOf(p, uid, st.secondTarget);
            }

            // See the Light: a tavern minion's stats become 2 and it joins the hand.
            void ActXyrella(Shop& s, int p, PowerState&, uint32_t, int offerIndex)
            {
                s.TakeOfferAndBuff(p, offerIndex, 2, 2, true);
            }

            // Puzzle Box: steal a random tavern minion and give it +1/+1.
            void ActYogg(Shop& s, int p, PowerState&, uint32_t, int)
            {
                if (s.Offers(p).empty())
                    return;
                int const offer = static_cast<int>(s.Random().Uniform(static_cast<uint32_t>(s.Offers(p).size())));
                s.TakeOfferAndBuff(p, offer, 1, 1, false);
            }

            // ---- Quilboar and the simple heroes ----

            void ActBlackthorn(Shop& s, int p, PowerState&, uint32_t, int)
            {
                int const gem = FindCard("BG20_GEM");
                for (int i = 0; i < 2; ++i)
                    s.AddSpellToHand(p, gem, false, false, false);
            }

            // Stay Frosty: freeze the chosen tavern offer (the end of turn buff is in Shop::EndTurn).
            void ActSindragosa(Shop& s, int p, PowerState&, uint32_t, int offerIndex)
            {
                s.FreezeOffer(p, offerIndex);
            }

            void ActMutanus(Shop& s, int p, PowerState&, uint32_t uid, int)
            {
                Board& board = s.PlayerBoard(p);
                int const index = board.IndexOfUid(uid);
                if (index < 0)
                    return;
                Minion const eaten = board[index];
                s.RemoveFromBoard(p, uid);
                Board& after = s.PlayerBoard(p);
                if (after.Empty())
                {
                    s.AddGold(p, 1);
                    return;
                }
                Minion& to = after[static_cast<int>(s.Random().Uniform(static_cast<uint32_t>(after.Size())))];
                // Base stats only: aura and "until next turn" stats are not permanent (as for Vol'jin).
                s.Buff(p, to, std::max(0, eaten.attack - eaten.auraAttack - eaten.tempAttack),
                       std::max(0, eaten.health - eaten.auraHealth - eaten.tempHealth));
                s.AddGold(p, 1);
            }

            bool MutanusOk(Shop& s, int p, uint32_t uid)
            {
                Board& board = s.PlayerBoard(p);
                return board.IndexOfUid(uid) >= 0 && board.Size() >= 2;
            }

            void ActTess(Shop& s, int p, PowerState&, uint32_t, int)
            {
                s.RefreshWithLastOpponent(p);
            }

            void ActScabbs(Shop& s, int p, PowerState&, uint32_t, int)
            {
                s.DiscoverLastOpponent(p);
            }

            // ---- the last heroes ----

            // Snicker-snack: run the Battlecry of the chosen friendly minion (Brann and Kalecgos aware).
            void ActShudderwock(Shop& s, int p, PowerState&, uint32_t uid, int)
            {
                s.TriggerBattlecry(p, uid, 0);
            }

            bool ShudderwockOk(Shop& s, int p, uint32_t uid)
            {
                Minion const* const m = s.FindMinion(p, uid);
                if (!m)
                    return false;
                EffectDef const* const e = s.Effects().Find(m->card);
                return e && e->battlecry;
            }

            // Sneed's Replicator: grant the Deathrattle that summons a random minion of a lower Tavern Tier.
            void ActSneed(Shop& s, int p, PowerState&, uint32_t uid, int)
            {
                Minion* const m = s.FindMinion(p, uid);
                if (!m)
                    return;
                GrantedEffect g;
                g.kind = kGrantedSneedLowerTier;
                m->granted.push_back(g);
            }

            // Embrace Your Rage / I'll Take That!: the power is spent now and resolves in the next combat.
            void ActArmCombat(Shop&, int, PowerState& state, uint32_t, int)
            {
                state.combatArmed = true;
            }

            // ---- the power swap ----

            // Dungar's Gryphon: choose a flightpath; the chosen one becomes the passive power and completes after its
            // countdown (the render values: Westfall 1 turn, Ironforge 2, Eastern Plaguelands 3).
            void ActGalewing(Shop& s, int p, PowerState&, uint32_t, int)
            {
                std::vector<std::string> const paths = { "BG20_HERO_283p_t1", "BG20_HERO_283p_t2",
                    "BG20_HERO_283p_t3" };
                s.OfferPowerChoice(p, PowerChoiceKind::Flightpath, paths, 3);
            }

            // Embrace the Elements: choose one of the four invocations; it stays until changed.
            void ActBrukan(Shop& s, int p, PowerState&, uint32_t, int)
            {
                std::vector<std::string> const invocations = { "BG22_HERO_001p_t1", "BG22_HERO_001p_t2",
                    "BG22_HERO_001p_t3", "BG22_HERO_001p_t4" };
                s.OfferPowerChoice(p, PowerChoiceKind::Invocation, invocations, 4);
            }

            // Take aim!: choose one of the four aims for the next combat (the player aims again each turn).
            void ActTavish(Shop& s, int p, PowerState&, uint32_t, int)
            {
                std::vector<std::string> const aims = { "BG22_HERO_000p_t1", "BG22_HERO_000p_t2",
                    "BG22_HERO_000p_t3", "BG22_HERO_000p_t4" };
                s.OfferPowerChoice(p, PowerChoiceKind::Aim, aims, 4);
            }

            // ---- the shop-system heroes ----

            // Trash for Treasure: remove a friendly minion (its pool copies go back), then choose one of two minions
            // from a Tavern Tier lower (a Tier 1 minion gives Tier 1).
            void ActHooktusk(Shop& s, int p, PowerState&, uint32_t uid, int)
            {
                Minion* const m = s.FindMinion(p, uid);
                if (!m || s.PlayerBoard(p).IndexOfUid(uid) < 0)
                    return;
                int const tier = std::max(1, m->Tier() - 1);
                if (!s.RemoveFromBoard(p, uid))
                    return;
                DiscoverRequest req;
                req.minTier = req.maxTier = tier;
                req.optionCount = 2;
                s.QueueCustomDiscover(p, req);
            }

            bool RemovableOk(Shop& s, int p, uint32_t uid)
            {
                Minion const* const m = s.FindMinion(p, uid);
                return m && m->card >= 0 && CardAt(m->card).role != CardRole::Spell &&
                    s.PlayerBoard(p).IndexOfUid(uid) >= 0;
            }

            // Stir the Pot: throw a board minion in the pot (removed, its types recorded). At 3, Discover from those
            // types at the player's Tavern Tier. A tribeless minion adds no type; three tribeless ones give nothing.
            void ActCookie(Shop& s, int p, PowerState& state, uint32_t uid, int)
            {
                Minion* const m = s.FindMinion(p, uid);
                if (!m || s.PlayerBoard(p).IndexOfUid(uid) < 0)
                    return;
                uint32_t const tribes = m->Tribes();
                uint32_t const types = (tribes & kTribeAll) != 0
                    ? (kAllTribesMask & ~static_cast<uint32_t>(kTribeAll))   // Nightmare Amalgam counts as every type
                    : (tribes & ~static_cast<uint32_t>(kTribeAll));
                if (!s.RemoveFromBoard(p, uid))
                    return;
                state.potTypes |= types;
                ++state.potCount;
                if (state.potCount < 3)
                    return;
                if (state.potTypes != 0)
                {
                    DiscoverRequest req;
                    req.minTier = req.maxTier = s.TavernTier(p);
                    req.tribeAny = state.potTypes;
                    s.QueueCustomDiscover(p, req);
                }
                state.potTypes = 0;
                state.potCount = 0;
            }

            // Imprison: take a tavern offer into the held slot; after 3 StartTurns it comes to the hand with +2/+2.
            void ActMaiev(Shop& s, int p, PowerState&, uint32_t, int offerIndex)
            {
                s.HoldOffer(p, offerIndex);
            }

            // ---- The Great Akazamzarak ----

            // Prestidigitation: Discover a Secret and put it into the battlefield.
            void ActAkazamzarak(Shop& s, int p, PowerState&, uint32_t, int)
            {
                s.DiscoverSecret(p);
            }

            bool AkazamzarakOk(Shop& s, int p, uint32_t)
            {
                PlayerShop const& ps = s.State(p);
                if (static_cast<int>(ps.secrets.size()) >= 5)
                    return false;   // the cap (owner point 9: 5, as in Hearthstone)
                for (int c : SecretCardIds())
                    if (std::find(ps.secrets.begin(), ps.secrets.end(), c) == ps.secrets.end())
                        return true;
                return false;       // every Secret is already in play
            }

            void Add(HeroPowerRegistry& reg, HeroPowerDef const& def) { reg.Register(def); }
        }

        void RegisterAllHeroPowers(HeroPowerRegistry& reg)
        {
            // Passives (Patchwerk, Nozdormu, Omu, Gallywix, Millhouse, Millificent, Deryl, Kael'thas, Aranna).
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_035"; Add(reg, d); }               // Patchwerk: 60 health (hero row)
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_063"; d.firstRefreshFree = true; Add(reg, d); }  // Nozdormu
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_082"; d.goldOnUpgrade = 2; Add(reg, d); }        // Forest Warden Omu
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_008"; d.goldOnSellNextTurn = 1; Add(reg, d); }   // Trade Prince Gallywix
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_054"; d.buyCostDelta = -1; d.refreshCostDelta = 1; d.upgradeCostDelta = 1; Add(reg, d); } // Millhouse
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_015"; d.summonAttackTribe = kTribeMech; d.summonAttack = 2; Add(reg, d); } // Millificent
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_042"; d.hatOnBuy = true; Add(reg, d); }          // Dancin' Deryl
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_066"; d.thirdPlayBuff = true; Add(reg, d); }     // Kael'thas Sunstrider
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_065"; d.freeBuyAfterAttacks = 14; Add(reg, d); }  // Aranna Starseeker

            // Passives.
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_088"; d.elementalPlaysPerUpgrade = 3; d.upgradeCostReduction = 3; Add(reg, d); } // Chenvaala
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_061"; Add(reg, d); }                  // Deathwing (combat side)
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_086"; Add(reg, d); }                  // Al'Akir (combat side)
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_107"; Add(reg, d); }                  // Greybough (combat side)
            { HeroPowerDef d; d.id = "BG20_HERO_100p"; Add(reg, d); }                       // Rokara (combat side)
            { HeroPowerDef d; d.id = "BG22_HERO_002p"; Add(reg, d); }                       // Drek'Thar (combat side)
            { HeroPowerDef d; d.id = "BG22_HERO_003p"; Add(reg, d); }                       // Vanndar Stormpike (combat side)
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_069"; Add(reg, d); }                  // Illidan Stormrage (combat side)
            { HeroPowerDef d; d.id = "BG22_HERO_305p"; Add(reg, d); }                       // Onyxia (combat side)
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_033"; d.startMinion = "TB_BaconShop_HP_033t"; Add(reg, d); } // The Curator

            // Actives.
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_049"; d.cost = 1; d.target = PowerTarget::TavernOffer; d.oncePerTurn = true; d.activate = &ActBazhial; Add(reg, d); } // Lich Baz'hial
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_076"; d.cost = 0; d.oncePerGame = true; d.activate = &ActKragg; Add(reg, d); }     // Skycap'n Kragg
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_028"; d.cost = 1; d.oncePerTurn = true; d.activate = &ActToki; Add(reg, d); }      // Infinite Toki
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_047"; d.cost = 1; d.costGrowthPerUse = 1; d.oncePerTurn = true; d.activate = &ActElise; Add(reg, d); } // Elise Starseeker
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_010"; d.cost = 2; d.target = PowerTarget::FriendlyMinion; d.oncePerTurn = true; d.activate = &ActGeorge; d.targetOk = &GeorgeOk; Add(reg, d); } // George the Fallen
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_036"; d.cost = 1; d.target = PowerTarget::FriendlyMinion; d.oncePerTurn = true; d.activate = &ActJaraxxus; d.targetOk = &JaraxxusOk; Add(reg, d); }  // Lord Jaraxxus
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_001"; d.cost = 1; d.target = PowerTarget::FriendlyMinion; d.oncePerTurn = true; d.activate = &ActEdwin; Add(reg, d); } // Edwin VanCleef
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_085"; d.cost = 1; d.target = PowerTarget::FriendlyMinion; d.oncePerTurn = true; d.activate = &ActRakanishu; Add(reg, d); } // Rakanishu
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_046"; d.cost = 0; d.target = PowerTarget::FriendlyMinion; d.oncePerGame = true; d.activate = &ActReno; d.targetOk = &RenoOk; Add(reg, d); } // Reno Jackson
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_024"; d.cost = 0; d.target = PowerTarget::FriendlyMinion; d.oncePerTurn = true; d.activate = &ActLichKing; d.targetOk = &LichKingOk; Add(reg, d); } // The Lich King
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_040"; d.cost = 1; d.target = PowerTarget::FriendlyMinion; d.oncePerTurn = true; d.growsHealthBuff = true; d.activate = &ActPyramad; Add(reg, d); } // Pyramad

            // Passives.
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_062"; d.extraOfferTribe = kTribeDragon; Add(reg, d); }      // Ysera
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_064"; d.discoverTribeAtTier = 5; d.discoverTribe = kTribeDragon; d.discoverCount = 2; Add(reg, d); } // Alexstrasza
            { HeroPowerDef d; d.id = "BG20_HERO_280p"; d.kurtrusPassive = true; Add(reg, d); }                    // Kurtrus Ashfallen
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_037a"; Add(reg, d); }                                       // Queen Wagtoggle (combat side)

            // Actives.
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_072"; d.cost = 3; d.oncePerTurn = true; d.buyOfTribeReducesCost = true; d.needsLobbyTribe = kTribePirate; d.activate = &ActPatches; Add(reg, d); } // Patches the Pirate
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_074"; d.cost = 1; d.digsForGolden = true; d.activate = &ActEudora; Add(reg, d); } // Captain Eudora (repeatable digs)
            { HeroPowerDef d; d.id = "BG23_HERO_305p"; d.cost = 10; d.oncePerGame = true; d.discountPerTurn = 1; d.activate = &ActTogwaggle; Add(reg, d); } // Heistbaron Togwaggle
            { HeroPowerDef d; d.id = "BG20_HERO_242p"; d.cost = 1; d.oncePerTurn = true; d.activate = &ActGuff; Add(reg, d); } // Guff Runetotem
            { HeroPowerDef d; d.id = "BG21_HERO_000p"; d.cost = 1; d.oncePerTurn = true; d.stanceChoice = true; d.activate = &ActCariel; Add(reg, d); } // Cariel Roame
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_104"; d.cost = 2; d.oncePerTurn = true; d.cthunEndOfTurn = true; d.activate = &ActCthun; Add(reg, d); } // C'Thun
            { HeroPowerDef d; d.id = "BG20_HERO_102p"; d.cost = 1; d.target = PowerTarget::TavernOffer; d.oncePerTurn = true; d.activate = &ActSaurfang; Add(reg, d); } // Overlord Saurfang
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_041"; d.cost = 2; d.oncePerTurn = true; d.ratKingDiscover = true; d.activate = &ActRatKing; Add(reg, d); } // The Rat King

            // Naga and the coin heroes.
            { HeroPowerDef d; d.id = "BG23_HERO_304p"; d.cost = 1; d.oncePerTurn = true; d.activate = &ActVashj; Add(reg, d); } // Lady Vashj
            { HeroPowerDef d; d.id = "BG22_HERO_007p"; d.conquestPowerId = "BG22_HERO_007p2"; Add(reg, d); } // Queen Azshara (passive)
            { HeroPowerDef d; d.id = "BG22_HERO_007p2"; d.cost = 1; d.oncePerTurn = true; d.activate = &ActAzshara; Add(reg, d); } // Naga Conquest
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_038"; d.bananarama = true; d.needsLobbyTribe = kTribeBeast; Add(reg, d); } // King Mukla (passive)
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_081"; d.cost = 1; d.oncePerTurn = true; d.barovGuess = true; d.target = PowerTarget::Seat; d.activate = &ActBarov; Add(reg, d); } // Lord Barov
            { HeroPowerDef d; d.id = "BG23_HERO_303p2"; d.cost = 0; d.oncePerTurn = true; d.holmesCoin = true; d.activate = &ActHolmes; Add(reg, d); } // Murloc Holmes
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_102"; d.cost = 3; d.usesPerGame = 3; d.oncePerTurn = true; d.activate = &ActZephrys; Add(reg, d); } // Zephrys
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_052"; d.cost = 0; d.usesPerTurn = 2; d.target = PowerTarget::TavernOffer; d.activate = &ActMalygos; Add(reg, d); } // Malygos
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_011"; d.cost = 1; d.oncePerTurn = true; d.target = PowerTarget::TavernOffer; d.activate = &ActGalakrond; Add(reg, d); } // Galakrond
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_084"; d.cost = 0; d.oncePerTurn = true; d.target = PowerTarget::FriendlyMinion; d.activate = &ActJandice; Add(reg, d); } // Jandice Barov
            { HeroPowerDef d; d.id = "BG20_HERO_201p"; d.cost = 0; d.oncePerTurn = true; d.target = PowerTarget::TwoMinions; d.activate = &ActVoljin; Add(reg, d); } // Vol'jin
            { HeroPowerDef d; d.id = "BG20_HERO_101p"; d.cost = 2; d.oncePerTurn = true; d.target = PowerTarget::TavernOffer; d.activate = &ActXyrella; Add(reg, d); } // Xyrella
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_039"; d.cost = 2; d.oncePerTurn = true; d.activate = &ActYogg; Add(reg, d); } // Yogg-Saron

            // Quilboar and the simple heroes.
            { HeroPowerDef d; d.id = "BG20_HERO_103p"; d.cost = 1; d.usesPerTurn = 2; d.activate = &ActBlackthorn; Add(reg, d); } // Death Speaker Blackthorn
            { HeroPowerDef d; d.id = "BG22_HERO_004p"; d.twiceAsNice = true; Add(reg, d); } // Varden Dawngrasp (passive)
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_014"; d.cost = 0; d.oncePerTurn = true; d.target = PowerTarget::TavernOffer; d.stayFrosty = true; d.activate = &ActSindragosa; Add(reg, d); } // Sindragosa
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_056"; d.murlocPerSells = 5; d.needsLobbyTribe = kTribeMurloc; Add(reg, d); } // Fungalmancer Flurgl (passive)
            { HeroPowerDef d; d.id = "BG22_HERO_200p"; d.mechPerDeaths = 10; d.needsLobbyTribe = kTribeMech; Add(reg, d); } // Ini Stormcoil (passive)
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_048"; d.brannReward = true; d.brannBattlecries = 5; Add(reg, d); } // Dinotamer Brann (passive)
            { HeroPowerDef d; d.id = "BG20_HERO_301p"; d.cost = 0; d.target = PowerTarget::FriendlyMinion; d.oncePerTurn = true; d.activate = &ActMutanus; d.targetOk = &MutanusOk; Add(reg, d); } // Mutanus
            { HeroPowerDef d; d.id = "BG20_HERO_282p"; Add(reg, d); } // Tamsin Roame (combat side)
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_077"; d.cost = 1; d.oncePerTurn = true; d.activate = &ActTess; Add(reg, d); } // Tess Greymane
            { HeroPowerDef d; d.id = "BG21_HERO_010p"; d.cost = 2; d.oncePerTurn = true; d.activate = &ActScabbs; Add(reg, d); } // Scabbs Cutterbutter

            // Combat and simple heroes.
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_022"; d.cost = 0; d.target = PowerTarget::FriendlyMinion; d.oncePerTurn = true; d.activate = &ActShudderwock; d.targetOk = &ShudderwockOk; Add(reg, d); } // Shudderwock (render: cost 0)
            { HeroPowerDef d; d.id = "BG21_HERO_030p"; d.cost = 1; d.target = PowerTarget::FriendlyMinion; d.oncePerTurn = true; d.activate = &ActSneed; Add(reg, d); } // Sneed
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_105"; d.startMinion = "TB_BaconShop_HP_105t"; Add(reg, d); } // N'Zoth (passive)
            { HeroPowerDef d; d.id = "BG23_HERO_201p"; Add(reg, d); } // Ozumat (passive combat side)
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_103"; d.cost = 2; d.oncePerTurn = true; d.armsCombat = true; d.activate = &ActArmCombat; Add(reg, d); } // Y'Shaarj
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_053"; d.cost = 1; d.oncePerTurn = true; d.armsCombat = true; d.activate = &ActArmCombat; Add(reg, d); } // Arch-Villain Rafaam

            // The power swap and the choices.
            { HeroPowerDef d; d.id = "BG20_HERO_283p"; d.cost = 0; d.oncePerTurn = true; d.activate = &ActGalewing; Add(reg, d); } // Galewing
            { HeroPowerDef d; d.id = "BG20_HERO_283p_t1"; d.countdownTurns = 1; d.countdownAttack = 2; d.countdownHealth = 2; Add(reg, d); } // Westfall (render)
            { HeroPowerDef d; d.id = "BG20_HERO_283p_t2"; d.countdownTurns = 2; d.countdownGold = 2; Add(reg, d); } // Ironforge (render)
            { HeroPowerDef d; d.id = "BG20_HERO_283p_t3"; d.countdownTurns = 3; d.countdownDiscover = true; Add(reg, d); } // Eastern Plaguelands (render)
            { HeroPowerDef d; d.id = "BG22_HERO_001p"; d.cost = 0; d.oncePerTurn = true; d.activate = &ActBrukan; Add(reg, d); } // Bru'kan
            { HeroPowerDef d; d.id = "BG22_HERO_001p_t1"; Add(reg, d); } // Earth Invocation (combat side)
            { HeroPowerDef d; d.id = "BG22_HERO_001p_t2"; Add(reg, d); } // Fire Invocation (combat side)
            { HeroPowerDef d; d.id = "BG22_HERO_001p_t3"; Add(reg, d); } // Water Invocation (combat side)
            { HeroPowerDef d; d.id = "BG22_HERO_001p_t4"; Add(reg, d); } // Lightning Invocation (combat side)
            { HeroPowerDef d; d.id = "BG22_HERO_000p"; d.cost = 0; d.oncePerTurn = true; d.activate = &ActTavish; Add(reg, d); } // Tavish Stormpike
            { HeroPowerDef d; d.id = "BG22_HERO_000p_t1"; Add(reg, d); } // Aim Left! (combat side)
            { HeroPowerDef d; d.id = "BG22_HERO_000p_t2"; Add(reg, d); } // Aim Low! (combat side)
            { HeroPowerDef d; d.id = "BG22_HERO_000p_t3"; Add(reg, d); } // Aim High! (combat side)
            { HeroPowerDef d; d.id = "BG22_HERO_000p_t4"; Add(reg, d); } // Aim Right! (combat side)
            { HeroPowerDef d; d.id = "BG20_HERO_202p"; d.swapEachTurn = true; Add(reg, d); } // Master Nguyen (passive)
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_057"; d.startPowerChoice = true; Add(reg, d); } // Sir Finley Mrrgglton (passive)
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_087"; d.killsForSulfuras = 25; Add(reg, d); } // Ragnaros the Firelord (passive)
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_087t"; d.endOfTurnEnds = true; Add(reg, d); } // Sulfuras (passive)

            // The shop-system heroes.
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_075"; d.cost = 0; d.target = PowerTarget::FriendlyMinion; d.oncePerTurn = true; d.consumesTarget = true; d.activate = &ActHooktusk; d.targetOk = &RemovableOk; Add(reg, d); } // Captain Hooktusk
            { HeroPowerDef d; d.id = "BG21_HERO_020p"; d.cost = 0; d.target = PowerTarget::FriendlyMinion; d.oncePerTurn = true; d.consumesTarget = true; d.pot = true; d.activate = &ActCookie; d.targetOk = &RemovableOk; Add(reg, d); } // Cookie the Cook
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_068"; d.cost = 1; d.target = PowerTarget::TavernOffer; d.oncePerTurn = true; d.holdsOffer = true; d.activate = &ActMaiev; Add(reg, d); } // Maiev Shadowsong
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_080"; d.bigglesworth = true; Add(reg, d); } // Mr. Bigglesworth (passive)
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_101"; d.ticketsInTavern = true; Add(reg, d); } // Silas Darkmoon (passive)
            { HeroPowerDef d; d.id = "BG24_HERO_100p"; d.startQuestOffer = true; Add(reg, d); } // Sire Denathrius (passive)
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_044"; d.skipTurns = 2; d.skipDiscoverCount = 2; d.skipDiscoverTier = 3; Add(reg, d); } // A. F. Kay (passive)
            { HeroPowerDef d; d.id = "BG22_HERO_201p"; d.skipTurns = 1; d.expeditionTiers = true; Add(reg, d); } // Ambassador Faelin (passive)

            // The Great Akazamzarak (render: cost 1).
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_020"; d.cost = 1; d.oncePerTurn = true; d.activate = &ActAkazamzarak; d.targetOk = &AkazamzarakOk; Add(reg, d); }

            // Tickatus. Prize Wall is passive: a prize Discover every 4 turns (4, 8, 12, 16).
            { HeroPowerDef d; d.id = "TB_BaconShop_HP_106"; d.prizesEvery = 4; Add(reg, d); }
        }

        std::vector<int> SecretCardIds()
        {
            static char const* const ids[] = {
                "TB_Bacon_Secrets_01", "TB_Bacon_Secrets_02", "TB_Bacon_Secrets_04", "TB_Bacon_Secrets_05",
                "TB_Bacon_Secrets_07", "TB_Bacon_Secrets_08", "TB_Bacon_Secrets_10", "TB_Bacon_Secrets_11",
                "TB_Bacon_Secrets_12", "TB_Bacon_Secrets_13",
            };
            std::vector<int> out;
            for (char const* id : ids)
            {
                int const card = FindCard(id);
                if (card >= 0)
                    out.push_back(card);
            }
            return out;
        }

        // The Darkmoon Prizes by CardDefs tag 1735 (BATTLEGROUNDS_DARKMOON_PRIZE_TURN). Big Banana and
        // Regular Discount carry no tag and are not prizes of 24.2.
        namespace
        {
            struct PrizeRow
            {
                char const* id;
                int turn;
            };
            PrizeRow const kPrizeRows[] = {
                { "BGS_Treasures_001", 1 }, { "BGS_Treasures_004", 1 }, { "BGS_Treasures_007", 1 },
                { "BGS_Treasures_013", 1 }, { "BGS_Treasures_029", 1 }, { "BGS_Treasures_033", 1 },
                { "BGS_Treasures_040", 1 },
                { "BGS_Treasures_006", 2 }, { "BGS_Treasures_009", 2 }, { "BGS_Treasures_010", 2 },
                { "BGS_Treasures_012", 2 }, { "BGS_Treasures_014", 2 }, { "BGS_Treasures_018", 2 },
                { "BGS_Treasures_026", 2 }, { "BGS_Treasures_030", 2 }, { "BGS_Treasures_036", 2 },
                { "BGS_Treasures_011", 3 }, { "BGS_Treasures_015", 3 }, { "BGS_Treasures_019", 3 },
                { "BGS_Treasures_020", 3 }, { "BGS_Treasures_034", 3 }, { "BGS_Treasures_037", 3 },
                { "BGS_Treasures_039", 3 },
                { "BGS_Treasures_016", 4 }, { "BGS_Treasures_022", 4 }, { "BGS_Treasures_023", 4 },
                { "BGS_Treasures_025", 4 }, { "BGS_Treasures_028", 4 }, { "BGS_Treasures_032", 4 },
            };
        }

        std::vector<int> DarkmoonPrizeIds(int prizeTurn)
        {
            std::vector<int> out;
            for (PrizeRow const& row : kPrizeRows)
            {
                if (prizeTurn != -1 && row.turn != prizeTurn)
                    continue;
                int const card = FindCard(row.id);
                if (card >= 0)
                    out.push_back(card);
            }
            return out;
        }

        int DarkmoonPrizeTurnOf(int cardIndex)
        {
            if (cardIndex < 0)
                return 0;
            // card index -> Prize turn, built once (the AI asks for every offer it values)
            static std::map<int, int> const byCard = []
            {
                std::map<int, int> m;
                for (PrizeRow const& row : kPrizeRows)
                {
                    int const card = FindCard(row.id);
                    if (card >= 0)
                        m[card] = row.turn;
                }
                return m;
            }();
            auto const it = byCard.find(cardIndex);
            return it == byCard.end() ? 0 : it->second;
        }

        int DarkmoonPrizeTurnForRound(int round)
        {
            if (round < 4)
                return 0;
            int const turn = round / 4;
            return std::min(4, turn);
        }

        int DarkmoonPrizeTurnsLeft(int round, int taken)
        {
            for (int k = taken + 1; k <= 4; ++k)
                if (k * 4 > round)
                    return k * 4 - round;
            return 0;
        }

        HeroCombatDef HeroCombatDefFor(std::string const& powerId)
        {
            HeroCombatDef d;
            if (powerId == "TB_BaconShop_HP_061")
                d.deathwing = true;
            else if (powerId == "TB_BaconShop_HP_086")
                d.alkair = true;
            else if (powerId == "TB_BaconShop_HP_107")
                d.greybough = true;
            else if (powerId == "TB_BaconShop_HP_069")
                d.illidan = true;
            else if (powerId == "BG20_HERO_100p")
                d.rokara = true;
            else if (powerId == "BG22_HERO_002p")
            {
                d.avengeCount = 3;
                d.avengeAttack = 1;
            }
            else if (powerId == "BG22_HERO_003p")
            {
                d.avengeCount = 2;
                d.avengeHealth = 1;
            }
            else if (powerId == "BG22_HERO_305p")
            {
                d.avengeCount = 4;
                d.avengeWhelp = true;
            }
            else if (powerId == "TB_BaconShop_HP_037a")
                d.wagtoggle = true;
            else if (powerId == "BG20_HERO_282p")
                d.phylacteryTargets = 5;
            else if (powerId == "BG23_HERO_201p")
                d.ozumat = true;   // the +1/+1 per sell is filled by Game from PowerState::minionsSoldThisGame
            else if (powerId == "TB_BaconShop_HP_103")
                d.yshaarj = true;  // Game clears this unless PowerState::combatArmed
            // Bru'kan's invocations and Tavish's aims (a chosen combat power).
            else if (powerId == "BG22_HERO_001p_t1")
                d.invocationEarth = true;
            else if (powerId == "BG22_HERO_001p_t2")
                d.invocationFire = true;
            else if (powerId == "BG22_HERO_001p_t3")
                d.invocationWater = true;
            else if (powerId == "BG22_HERO_001p_t4")
                d.invocationLightning = true;
            else if (powerId == "BG22_HERO_000p_t1")
                d.tavishAim = 1;   // left
            else if (powerId == "BG22_HERO_000p_t2")
                d.tavishAim = 2;   // lowest Health
            else if (powerId == "BG22_HERO_000p_t3")
                d.tavishAim = 3;   // highest Health
            else if (powerId == "BG22_HERO_000p_t4")
                d.tavishAim = 1;   // left-most, as the render says (the name and art say Aim Right; the render decides)
            return d;
        }

        int HeroPickValue(int heroIndex)
        {
            if (heroIndex < 0 || heroIndex >= HeroCount())
                return 0;
            return PowerPickValue(HeroAt(heroIndex).powerId);
        }

        int PowerPickValue(std::string const& powerId)
        {
            static std::map<std::string, int> const value = {
                { "TB_BaconShop_HP_076", 6 },  // Skycap'n Kragg (gold)
                { "TB_BaconShop_HP_065", 4 },  // Aranna Starseeker (a free first buy after 14 attacks)
                { "TB_BaconShop_HP_082", 4 },  // Forest Warden Omu (gold on upgrade)
                { "TB_BaconShop_HP_042", 4 },  // Dancin' Deryl (sell buff)
                { "TB_BaconShop_HP_008", 3 },  // Trade Prince Gallywix (sell gold)
                { "TB_BaconShop_HP_063", 3 },  // Nozdormu (free refresh)
                { "TB_BaconShop_HP_054", 3 },  // Millhouse Manastorm (cheap minions)
                { "TB_BaconShop_HP_015", 3 },  // Millificent Manastorm (summoned Mechs +2 Attack)
                { "TB_BaconShop_HP_036", 3 },  // Lord Jaraxxus (Demon consume)
                { "TB_BaconShop_HP_046", 2 },  // Reno Jackson (golden)
                { "TB_BaconShop_HP_066", 2 },  // Kael'thas Sunstrider
                { "TB_BaconShop_HP_035", 2 },  // Patchwerk (60 health)
                { "TB_BaconShop_HP_028", 2 },  // Infinite Toki
                { "TB_BaconShop_HP_047", 2 },  // Elise Starseeker
                { "TB_BaconShop_HP_010", 2 },  // George the Fallen
                { "TB_BaconShop_HP_001", 2 },  // Edwin VanCleef
                { "TB_BaconShop_HP_085", 1 },  // Rakanishu
                { "TB_BaconShop_HP_049", 2 },  // Lich Baz'hial (steals a minion, damages itself)
                { "TB_BaconShop_HP_088", 3 },  // Chenvaala (cheaper upgrades)
                { "TB_BaconShop_HP_061", 3 },  // Deathwing (all minions +2 Attack permanently)
                { "TB_BaconShop_HP_086", 3 },  // Al'Akir (the left-most minion)
                { "TB_BaconShop_HP_107", 3 },  // Greybough (stronger summons)
                { "BG20_HERO_100p", 3 },       // Rokara (permanent Attack)
                { "BG22_HERO_002p", 3 },       // Drek'Thar (Avenge Attack)
                { "BG22_HERO_003p", 3 },       // Vanndar Stormpike (Avenge Health)
                { "TB_BaconShop_HP_069", 3 },  // Illidan Stormrage (the ends attack)
                { "BG22_HERO_305p", 3 },       // Onyxia (Avenge Whelp)
                { "TB_BaconShop_HP_033", 3 },  // The Curator (a free Amalgam)
                { "TB_BaconShop_HP_024", 2 },  // The Lich King (Reborn)
                { "TB_BaconShop_HP_040", 2 },  // Pyramad (+2 Health and growing)
                { "TB_BaconShop_HP_072", 3 },  // Patches the Pirate (a Pirate, cheaper each buy)
                { "TB_BaconShop_HP_074", 3 },  // Captain Eudora (a Golden minion every five digs)
                { "BG23_HERO_305p", 4 },       // Heistbaron Togwaggle (steals the whole Tavern)
                { "TB_BaconShop_HP_037a", 2 }, // Queen Wagtoggle (one per type)
                { "BG20_HERO_242p", 2 },       // Guff Runetotem (one per tier)
                { "BG21_HERO_000p", 2 },       // Cariel Roame (two minions +1/+1, improved after each upgrade)
                { "BG21_HERO_000p_united", 3 },    // United Stance (one more minion)
                { "BG21_HERO_000p_offense", 2 },   // Offensive Stance (+2 Attack to each buff)
                { "BG21_HERO_000p_defense", 2 },   // Defensive Stance (+2 Health to each buff)
                { "TB_BaconShop_HP_104", 2 },  // C'Thun (an end-of-turn buff that grows)
                { "BG20_HERO_102p", 2 },       // Overlord Saurfang (Attack on the next buy)
                { "TB_BaconShop_HP_041", 2 },  // The Rat King (+2/+2 on the turn's type)
                { "TB_BaconShop_HP_062", 3 },  // Ysera (an extra Dragon every refresh)
                { "TB_BaconShop_HP_064", 3 },  // Alexstrasza (two Dragons at Tier 5)
                { "BG20_HERO_280p", 2 },       // Kurtrus Ashfallen (+2/+2 for three buys)
                { "BG23_HERO_304p", 3 },       // Lady Vashj (a permanent Spellcraft spell)
                { "BG22_HERO_007p", 3 },       // Queen Azshara (Naga Conquest at 25 Attack)
                { "TB_BaconShop_HP_038", 3 },  // King Mukla (Bananas for the table)
                { "TB_BaconShop_HP_081", 2 },  // Lord Barov (a guess for three Gold Coins)
                { "BG23_HERO_303p2", 2 },      // Murloc Holmes (a guessed coin)
                { "TB_BaconShop_HP_102", 4 },  // Zephrys (a third copy, three times)
                { "TB_BaconShop_HP_052", 3 },  // Malygos (two trades a turn)
                { "TB_BaconShop_HP_011", 3 },  // Galakrond (a higher-Tier replacement)
                { "TB_BaconShop_HP_084", 2 },  // Jandice Barov (a tavern trade)
                { "BG20_HERO_201p", 2 },       // Vol'jin (a stats swap)
                { "BG20_HERO_101p", 2 },       // Xyrella (a 2/2 from the Tavern)
                { "TB_BaconShop_HP_039", 2 },  // Yogg-Saron (a stolen minion)
                { "BG20_HERO_103p", 2 },       // Death Speaker Blackthorn (2 Blood Gems twice a turn)
                { "BG22_HERO_004p", 2 },       // Varden Dawngrasp (a copy of the best offer every refresh)
                { "TB_BaconShop_HP_014", 2 },  // Sindragosa (a frozen, growing tavern)
                { "TB_BaconShop_HP_056", 2 },  // Fungalmancer Flurgl (a Murloc every five sells)
                { "BG22_HERO_200p", 2 },       // Ini Stormcoil (a Mech every ten deaths)
                { "TB_BaconShop_HP_048", 3 },  // Dinotamer Brann (a free Brann Bronzebeard)
                { "BG20_HERO_301p", 2 },       // Mutanus (stats moved, 1 Gold)
                { "BG20_HERO_282p", 2 },       // Tamsin Roame (stats to five minions)
                { "TB_BaconShop_HP_077", 2 },  // Tess Greymane (the last warband in the tavern)
                { "BG21_HERO_010p", 2 },       // Scabbs Cutterbutter (a Discover from the last warband)
                { "TB_BaconShop_HP_022", 3 },  // Shudderwock (a Battlecry again)
                { "BG21_HERO_030p", 2 },       // Sneed (a Deathrattle for a lower Tier)
                { "TB_BaconShop_HP_105", 3 },  // N'Zoth (a free Fish that keeps Deathrattles)
                { "BG23_HERO_201p", 2 },       // Ozumat (a free growing Tentacle)
                { "TB_BaconShop_HP_103", 2 },  // Y'Shaarj (a Tier minion for the fight)
                { "TB_BaconShop_HP_053", 2 },  // Arch-Villain Rafaam (a copy of the first kill)
                // The six heroes and the powers their choices offer.
                { "BG20_HERO_283p", 3 },       // Galewing (a flightpath bonus)
                { "BG20_HERO_283p_t1", 2 },    // Westfall (+2/+2 now)
                { "BG20_HERO_283p_t2", 2 },    // Ironforge (2 Gold)
                { "BG20_HERO_283p_t3", 3 },    // Eastern Plaguelands (a Discover)
                { "BG22_HERO_000p", 3 },       // Tavish Stormpike (a doubled start-of-combat hit)
                { "BG22_HERO_000p_t1", 2 },    // Aim Left!
                { "BG22_HERO_000p_t2", 2 },    // Aim Low!
                { "BG22_HERO_000p_t3", 2 },    // Aim High!
                { "BG22_HERO_000p_t4", 2 },    // Aim Right!
                { "BG22_HERO_001p", 3 },       // Bru'kan (a start-of-combat invocation)
                { "BG22_HERO_001p_t1", 2 },    // Earth: four Deathrattles
                { "BG22_HERO_001p_t2", 3 },    // Fire: double the left-most Attack
                { "BG22_HERO_001p_t3", 2 },    // Water: +3 Health and Taunt
                { "BG22_HERO_001p_t4", 2 },    // Lightning: 1 damage to five
                { "BG20_HERO_202p", 3 },       // Master Nguyen (a new power every turn)
                { "TB_BaconShop_HP_057", 4 },  // Sir Finley Mrrgglton (a power of your choice)
                { "TB_BaconShop_HP_087", 3 },  // Ragnaros the Firelord (Sulfuras at 25 kills)
                { "TB_BaconShop_HP_087t", 3 }, // Sulfuras (+3/+3 to the ends each turn)
                // The shop-system heroes.
                { "TB_BaconShop_HP_075", 2 },  // Captain Hooktusk (a lower-Tier minion for a board slot)
                { "BG21_HERO_020p", 2 },       // Cookie the Cook (a Discover after three throws)
                { "TB_BaconShop_HP_068", 3 },  // Maiev Shadowsong (an offer with +2/+2 after three turns)
                { "TB_BaconShop_HP_080", 3 },  // Mr. Bigglesworth (a minion from every dead warband)
                { "TB_BaconShop_HP_101", 3 },  // Silas Darkmoon (a ticket Discover every few buys)
                { "BG24_HERO_100p", 4 },       // Sire Denathrius (two quests)
                { "TB_BaconShop_HP_044", 2 },  // A. F. Kay (two Tier 3 minions)
                { "BG22_HERO_201p", 3 },       // Ambassador Faelin (a Tier 2, 4 and 6 minion)
                { "TB_BaconShop_HP_020", 3 },  // The Great Akazamzarak (a Secret every turn)
                { "TB_BaconShop_HP_106", 4 },  // Tickatus (a Darkmoon Prize on turns 4, 8, 12 and 16)
            };
            auto it = value.find(powerId);
            return it == value.end() ? 1 : it->second;
        }
    }
}
