#include "HsBgCombat.h"

#include <algorithm>

namespace Hs
{
    namespace Bg
    {
        namespace
        {
            bool IsDead(Minion const& m) { return m.health <= 0 || (m.flags & kMinionDestroyed) != 0; }

            // The fight itself. One instance per SimulateCombat call. Boards are indexed by side (0 / 1).
            class Sim : public CombatApi
            {
            public:
                Sim(CombatSide const& a, CombatSide const& b, Rng& rng, CombatSettings const& settings)
                    : m_rng(rng), m_fx(settings.effects ? *settings.effects : EffectRegistry::Global()),
                      m_cap(settings.capDamage), m_lobby{ settings.lobbyTribes[0], settings.lobbyTribes[1] },
                      m_slice(settings.slice15AOnly),
                      m_reward{ settings.rewardState[0], settings.rewardState[1] },
                      m_reward2{ settings.rewardState2[0], settings.rewardState2[1] },
                      m_summonTribe{ settings.summonAttackTribe[0], settings.summonAttackTribe[1] },
                      m_summonAttack{ settings.summonAttack[0], settings.summonAttack[1] },
                      m_hero{ settings.hero[0], settings.hero[1] }, m_round(settings.round)
                {
                    CombatSide const* in[2] = { &a, &b };
                    for (int s = 0; s < 2; ++s)
                    {
                        m_board[s] = in[s]->board;
                        m_tier[s] = in[s]->tavernTier;
                        for (int i = 0; i < m_board[s].Size(); ++i)
                        {
                            Minion& m = m_board[s][i];
                            m.uid = static_cast<uint32_t>(s * kBoardMax + i + 1);
                            m.flags = 0;
                            m.killer = 0;
                            m.counters[kCounterAvenge] = 0;
                        }
                        RecomputeAuras(m_board[s], m_fx);
                        m_secrets[s] = in[s]->secrets;   // the side's Secrets, in play order
                    }
                    m_secretVenomstrike = FindCard("TB_Bacon_Secrets_01");
                    m_secretSnakeTrap = FindCard("TB_Bacon_Secrets_02");
                    m_secretSplittingImage = FindCard("TB_Bacon_Secrets_04");
                    m_secretEffigy = FindCard("TB_Bacon_Secrets_05");
                    m_secretAutodefense = FindCard("TB_Bacon_Secrets_07");
                    m_secretAvenge = FindCard("TB_Bacon_Secrets_08");
                    m_secretRedemption = FindCard("TB_Bacon_Secrets_10");
                    m_secretSalvation = FindCard("TB_Bacon_Secrets_11");
                    m_secretCobra = FindCard("EX1_170");
                    m_secretSnake = FindCard("EX1_554t");
                }

                CombatResult Run()
                {
                    int const n0 = m_board[0].Size();
                    int const n1 = m_board[1].Size();
                    int first = n0 > n1 ? 0 : (n1 > n0 ? 1 : static_cast<int>(m_rng.Uniform(2)));
                    m_res.firstSide = first;
                    m_active = first;
                    m_res.start[0] = m_board[0];
                    m_res.start[1] = m_board[1];

                    // Start of combat: the first-attacking side, then the other, each minion left to right, then the
                    // side's reward start-of-combat (Stolen Gold, Evil Twin, Staff of Origination).
                    for (int k = 0; k < 2; ++k)
                    {
                        int const s = k == 0 ? first : 1 - first;
                        m_active = s;
                        for (uint32_t id : BoardIds(s))
                        {
                            FireSelf(HookKind::StartOfCombat, &EffectDef::startOfCombat, id);
                            ResolveDeaths();
                        }
                        RewardStartOfCombat(s);
                    }

                    // Hero powers resolve after both sides' start-of-combat effects.
                    for (int k = 0; k < 2; ++k)
                    {
                        m_active = k == 0 ? first : 1 - first;
                        HeroStartOfCombat(m_active);
                    }

                    int side = first;
                    int passes = 0;
                    int turns = 0;
                    while (!Ended())
                    {
                        if (++turns > kMaxTurns)
                        {
                            m_res.timedOut = true;
                            break;
                        }
                        m_active = side;
                        passes = TakeTurn(side) ? 0 : passes + 1;
                        if (passes >= 2)
                        {
                            m_res.stalled = true;
                            break;
                        }
                        side = 1 - side;
                    }

                    m_res.survivors[0] = m_board[0];
                    m_res.survivors[1] = m_board[1];
                    bool const alive0 = !m_board[0].Empty();
                    bool const alive1 = !m_board[1].Empty();
                    if (alive0 != alive1)
                    {
                        m_res.winner = alive0 ? 0 : 1;
                        m_res.damage = CombatDamage(m_tier[m_res.winner], m_board[m_res.winner], m_cap);
                    }
                    return std::move(m_res);
                }

                // ---- CombatApi
                Minion* Find(uint32_t id) override
                {
                    for (int s = 0; s < 2; ++s)
                        for (int i = 0; i < m_board[s].Size(); ++i)
                            if (m_board[s][i].uid == id)
                                return &m_board[s][i];
                    return nullptr;
                }
                int SideOf(uint32_t id) const override
                {
                    int s, i;
                    return Locate(id, s, i) ? s : -1;
                }
                int PositionOf(uint32_t id) const override
                {
                    int s, i;
                    return Locate(id, s, i) ? i : -1;
                }
                std::vector<uint32_t> BoardIds(int side) const override
                {
                    std::vector<uint32_t> ids;
                    for (int i = 0; i < m_board[side].Size(); ++i)
                        if (!IsDead(m_board[side][i]))
                            ids.push_back(m_board[side][i].uid);
                    return ids;
                }
                int TavernTier(int side) const override { return m_tier[side]; }
                Rng& Random() override { return m_rng; }

                uint32_t SummonMinion(int side, Minion const& proto, int position) override
                {
                    return Place(side, proto, position, EventType::Summon, m_source);
                }

                void Buff(uint32_t id, int attack, int health) override
                {
                    Minion* m = Find(id);
                    if (!m)
                        return;
                    m->Buff(attack, health);
                    CombatEvent e;
                    e.type = EventType::Buff;
                    e.side = SideOf(id);
                    e.actor = m_source;
                    e.target = id;
                    e.amount = attack;
                    e.amount2 = health;
                    Log(e);
                    int const keep = KeepFactor(*m);
                    if (keep > 0 && !m_inPermanent)
                        RecordPersistentBuff(id, attack * keep, health * keep, 0);
                    if (attack > 0)
                        Broadcast(HookKind::FriendBuffed, &EffectDef::onFriendBuffed, SideOf(id), id, attack, nullptr, id,
                                  0, health);
                }

                void BuffPermanent(uint32_t id, int attack, int health) override
                {
                    m_inPermanent = true;
                    Buff(id, attack, health);
                    m_inPermanent = false;
                    RecordPersistentBuff(id, attack, health, 0);
                }

                void GrantKeywords(uint32_t id, uint32_t keywords) override
                {
                    Minion* m = Find(id);
                    if (!m)
                        return;
                    m->keywords |= keywords;
                    CombatEvent e;
                    e.type = EventType::Buff;
                    e.side = SideOf(id);
                    e.actor = m_source;
                    e.target = id;
                    e.keywords = keywords;
                    Log(e);
                    if (KeepFactor(*m) > 0)
                        RecordPersistentBuff(id, 0, 0, keywords);
                }

                void Damage(uint32_t source, uint32_t target, int amount) override
                {
                    ApplyDamage(source, target, amount, false);
                }

                void Destroy(uint32_t target) override
                {
                    if (Minion* m = Find(target))
                    {
                        m->flags |= kMinionDestroyed;
                        m->killer = m_source;
                    }
                }

                void RecordPersistent(uint32_t id, int kind, int value) override
                {
                    PersistentChange c = PersistentFor(id);
                    c.kind = kind;
                    c.value = value;
                    m_res.persistent.push_back(c);
                }

                void RecordPersistentBuff(uint32_t id, int attack, int health, uint32_t keywords) override
                {
                    PersistentChange c = PersistentFor(id);
                    c.kind = kPersistBuff;
                    c.attack = attack;
                    c.health = health;
                    c.keywords = keywords;
                    m_res.persistent.push_back(c);
                }

                void RecordPersistentAddToHand(int side, int card, bool golden) override
                {
                    PersistentChange c;
                    c.side = side;
                    c.kind = kPersistAddToHand;
                    c.card = card;
                    c.golden = golden;
                    m_res.persistent.push_back(c);
                }

                void RecordPersistentAddToPoolHand(int side, int card, bool golden) override
                {
                    PersistentChange c;
                    c.side = side;
                    c.kind = kPersistAddPoolToHand;
                    c.card = card;
                    c.golden = golden;
                    m_res.persistent.push_back(c);
                }

                EffectRegistry const& Effects() const override { return m_fx; }

                int SummonTimes(int side) const override { return TimesFor(m_fx, m_board[side], TimesKind::Summon); }

                std::vector<Minion> DeadFriendlies(int side) const override { return m_dead[side]; }

                std::vector<int> PoolCandidates(int side, uint32_t tribeAny, bool deathrattleOnly) const override
                {
                    uint32_t lobby = (side == 0 || side == 1) ? m_lobby[side] : 0;
                    if (lobby == 0)
                        lobby = kTribeAll - 1u;
                    std::vector<int> out;
                    for (int i = 0; i < CardCount(); ++i)
                    {
                        CardDef const& c = CardAt(i);
                        if (c.role != CardRole::Pool || (m_slice && !c.slice15A) ||
                            !(c.tribes == 0 || (c.tribes & kTribeAll) != 0 || (c.tribes & lobby) != 0))
                            continue;
                        if (tribeAny != 0 && !HasTribe(c.tribes, tribeAny))
                            continue;
                        if (deathrattleOnly)
                        {
                            EffectDef const* e = m_fx.Find(i);
                            if (!e || !e->deathrattle)
                                continue;
                        }
                        out.push_back(i);
                    }
                    return out;
                }

                bool ExtraAttack(uint32_t attacker, uint32_t target) override
                {
                    int const s = SideOf(attacker);
                    if (s < 0 || (target != 0 && SideOf(target) != 1 - s))
                        return false;
                    if (m_extraAttackDepth >= 32)
                        return false;   // Yo-Ho-Ogres answering each other: a hard stop
                    ++m_extraAttackDepth;
                    bool const ok = DoAttack(s, attacker, target);
                    --m_extraAttackDepth;
                    return ok;
                }

                bool MakeGolden(uint32_t id) override
                {
                    Minion* m = Find(id);
                    if (!m || m->golden || m->card < 0)
                        return false;
                    CardDef const& d = CardAt(m->card);
                    if (d.goldenId[0] == '\0')
                        return false;
                    m->attack += d.goldenAttack - d.attack;
                    int const dh = d.goldenHealth - d.health;
                    m->health += dh;
                    m->maxHealth += dh;
                    uint32_t extra = d.goldenKeywords & ~d.keywords;
                    if (extra & kKwMegaWindfury)
                        m->keywords &= ~static_cast<uint32_t>(kKwWindfury);
                    m->keywords |= extra;
                    m->golden = true;
                    CombatEvent e;
                    e.type = EventType::Buff;
                    e.side = SideOf(id);
                    e.actor = m_source;
                    e.target = id;
                    e.amount = d.goldenAttack - d.attack;
                    e.amount2 = dh;
                    e.keywords = extra;
                    Log(e);
                    RecordPersistent(id, kPersistMakeGolden, 0);
                    return true;
                }

                bool TriggerDeathrattle(uint32_t id) override
                {
                    Minion* m = Find(id);
                    if (!m || IsDead(*m))
                        return false;
                    std::vector<std::pair<CombatHook, bool>> const inst = DeathrattleInstances(m_fx, *m);
                    if (inst.empty())
                        return false;
                    int const side = SideOf(id);
                    Minion const copy = *m;
                    int const times = DeathrattleTimes(side);
                    int position = PositionOf(id) + 1;
                    for (int t = 0; t < times; ++t)
                        for (auto const& in : inst)
                        {
                            CombatCtx ctx(*this, side, id, in.second);
                            ctx.dead = &copy;
                            ctx.position = position;
                            Call(HookKind::Deathrattle, in.first, ctx);
                            position = ctx.position;
                        }
                    return true;
                }

            private:
                PersistentChange PersistentFor(uint32_t id) const
                {
                    PersistentChange c;
                    c.side = id <= static_cast<uint32_t>(kBoardMax) ? 0 : 1;
                    c.inputIndex = id <= static_cast<uint32_t>(2 * kBoardMax)
                        ? static_cast<int>((id - 1) % static_cast<uint32_t>(kBoardMax)) : -1;
                    if (c.inputIndex < 0)
                    {
                        int const s = SideOf(id);
                        c.side = s < 0 ? 0 : s;
                    }
                    return c;
                }

                // Factor by which combat buffs on this minion persist (Tarecgosa), 0 = not at all.
                int KeepFactor(Minion const& m) const
                {
                    EffectDef const* e = m.card < 0 ? nullptr : m_fx.Find(m.card);
                    if (!e)
                        return 0;
                    return m.golden && e->keepCombatBuffsGolden > 0 ? e->keepCombatBuffsGolden : e->keepCombatBuffs;
                }

                // A dead minion waiting for its Deathrattle / Reborn. `slot` is where its replacements go, kept
                // current while earlier Deathrattles of the same batch insert minions.
                struct Pending
                {
                    int side = 0;
                    int slot = 0;
                    bool behind = false; // stood left of the attack pointer (had already attacked this cycle)
                    bool processed = false;
                    int originalIndex = 0;
                    Minion m;
                };

                bool Locate(uint32_t id, int& side, int& index) const
                {
                    for (int s = 0; s < 2; ++s)
                        for (int i = 0; i < m_board[s].Size(); ++i)
                            if (m_board[s][i].uid == id)
                            {
                                side = s;
                                index = i;
                                return true;
                            }
                    return false;
                }

                bool Ended() const { return m_board[0].Empty() || m_board[1].Empty(); }

                // The side of a minion id that may have died already (combat damage, an effect, the same batch).
                int KillerSide(uint32_t id) const
                {
                    if (id == 0)
                        return -1;
                    for (int s = 0; s < 2; ++s)
                    {
                        for (int i = 0; i < m_board[s].Size(); ++i)
                            if (m_board[s][i].uid == id)
                                return s;
                        for (Pending const& p : m_pending)
                            if (p.side == s && p.m.uid == id)
                                return s;
                        for (Minion const& m : m_dead[s])
                            if (m.uid == id)
                                return s;
                    }
                    return -1;
                }
                void Log(CombatEvent const& e) { m_res.events.push_back(e); }
                // A change made in place (a hero power, Stolen Gold, an aura) still reaches the client's replay as a
                // Buff event; nothing is recorded for after the combat.
                void LogBuff(uint32_t id, int attack, int health, uint32_t keywords)
                {
                    if (attack == 0 && health == 0 && keywords == 0)
                        return;
                    CombatEvent e;
                    e.type = EventType::Buff;
                    e.side = SideOf(id);
                    e.actor = m_source;
                    e.target = id;
                    e.amount = attack;
                    e.amount2 = health;
                    e.keywords = keywords;
                    Log(e);
                }
                EffectDef const* EffectOf(Minion const& m) const { return m.card < 0 ? nullptr : m_fx.Find(m.card); }
                // The side's reward start-of-combat effects (Stolen Gold, Evil Twin, Staff of Origination).
                void MakeGoldenEdge(uint32_t id)
                {
                    Minion* m = Find(id);
                    if (!m || m->golden || m->card < 0)
                        return;
                    CardDef const& d = CardAt(m->card);
                    if (d.goldenId[0] == '\0')
                        return;
                    m->attack += d.goldenAttack - d.attack;
                    m->health += d.goldenHealth - d.health;
                    m->maxHealth += d.goldenHealth - d.health;
                    uint32_t extra = d.goldenKeywords & ~d.keywords;
                    if (extra & kKwMegaWindfury)
                        m->keywords &= ~static_cast<uint32_t>(kKwWindfury);
                    m->keywords |= extra;
                    m->golden = true;   // the golden twin's stats / keywords live in this row (see Shop::MakeGolden)
                    LogBuff(id, d.goldenAttack - d.attack, d.goldenHealth - d.health, extra);
                }

                void RewardStartOfCombat(int side)
                {
                    RewardState const* const states[2] = { m_reward[side], m_reward2[side] };
                    for (RewardState const* st : states)
                    {
                        RewardRule const* d = st ? RewardFor(*st) : nullptr;
                        if (!d)
                            continue;
                        switch (d->kind)
                        {
                    case RewardKind::StaffOfOrigination:
                        for (uint32_t id : BoardIds(side))
                            Buff(id, 15, 15);
                        ResolveDeaths();
                        break;
                    case RewardKind::EvilTwin:
                    {
                        uint32_t best = 0;
                        int bestHealth = -1;
                        for (uint32_t id : BoardIds(side))
                        {
                            Minion* m = Find(id);
                            if (m && m->health > bestHealth)
                            {
                                bestHealth = m->health;
                                best = id;
                            }
                        }
                        if (best != 0)
                        {
                            Minion copy = *Find(best);
                            copy.uid = 0;
                            SummonMinion(side, copy, PositionOf(best) + 1);
                        }
                        ResolveDeaths();
                        break;
                    }
                    case RewardKind::StolenGold:
                    {
                        std::vector<uint32_t> const ids = BoardIds(side);
                        if (!ids.empty())
                        {
                            MakeGoldenEdge(ids.front());
                            if (ids.size() > 1)
                                MakeGoldenEdge(ids.back());
                        }
                        ResolveDeaths();
                        break;
                    }
                    default:
                        break;
                    }
                    }
                }

                // Start-of-combat hero powers (Deathwing, Al'Akir, Illidan).
                void HeroStartOfCombat(int side)
                {
                    HeroCombatDef const& h = m_hero[side];
                    if (h.deathwing)
                    {
                        // Deathwing: give ALL minions (both boards) +2 Attack permanently.
                        for (int s = 0; s < 2; ++s)
                            for (uint32_t id : BoardIds(s))
                                BuffPermanent(id, 2, 0);
                    }
                    if (h.alkair)
                    {
                        std::vector<uint32_t> const ids = BoardIds(side);
                        if (!ids.empty())
                            if (Minion* m = Find(ids.front()))
                            {
                                m->keywords |= kKwWindfury | kKwDivineShield | kKwTaunt;
                                LogBuff(ids.front(), 0, 0, kKwWindfury | kKwDivineShield | kKwTaunt);
                            }
                    }
                    if (h.illidan)
                    {
                        std::vector<uint32_t> ids = BoardIds(side);
                        if (ids.empty())
                            return;
                        std::vector<uint32_t> fighters = ids;
                        if (fighters.size() > 2)
                            fighters = { ids.front(), ids.back() };
                        for (uint32_t id : fighters)
                            if (Minion* m = Find(id))
                            {
                                m->attack += 2;
                                LogBuff(id, 2, 0, 0);
                            }
                        for (uint32_t id : fighters)
                        {
                            if (Ended())
                                break;
                            if (Find(id) && !IsDead(*Find(id)))
                                DoAttack(side, id);
                        }
                    }
                    if (h.wagtoggle)
                    {
                        // Queen Wagtoggle: up to 3 friendly minions of different types get stats equal to their Tier.
                        for (int i : PickDistinctTypes(TribeMasks(m_board[side]), m_rng, 3))
                        {
                            Minion& m = m_board[side][i];
                            int const t = m.Tier();
                            m.attack += t;
                            m.health += t;
                            m.maxHealth += t;
                            LogBuff(m.uid, t, t, 0);
                        }
                    }
                    if (h.phylacteryTargets > 0)
                    {
                        // Tamsin Roame: destroy the lowest-Health friendly minion, give its stats to five friends.
                        std::vector<uint32_t> const ids = BoardIds(side);
                        if (!ids.empty())
                        {
                            uint32_t victim = ids.front();
                            int lowest = Find(victim)->health;
                            for (uint32_t id : ids)
                            {
                                int const health = Find(id)->health;
                                if (health < lowest)
                                {
                                    lowest = health;
                                    victim = id;
                                }
                            }
                            Minion const* dead = Find(victim);
                            int const attack = dead->attack;
                            int const health = dead->health;
                            std::vector<uint32_t> others;
                            for (uint32_t id : ids)
                                if (id != victim)
                                    others.push_back(id);
                            m_rng.Shuffle(others);
                            if (static_cast<int>(others.size()) > h.phylacteryTargets)
                                others.resize(static_cast<std::size_t>(h.phylacteryTargets));
                            for (uint32_t id : others)
                                Buff(id, attack, health);
                            Destroy(victim);
                        }
                    }
                    if (h.ozumat)
                    {
                        // Ozumat: a 2/2 Taunt Tentacle, +1/+1 for every minion sold this game.
                        int const tentacle = FindCard("BG23_HERO_201pt");
                        if (tentacle >= 0)
                        {
                            Minion t = MakeMinion(tentacle);
                            if (h.ozumatSold > 0)
                                t.Buff(h.ozumatSold, h.ozumatSold);
                            SummonMinion(side, t, m_board[side].Size());
                        }
                    }
                    if (h.yshaarj)
                    {
                        // Y'Shaarj: a pool minion of the player's Tavern Tier to the hand, a copy to the fight.
                        std::vector<int> candidates;
                        for (int card : PoolCandidates(side, 0, false))
                            if (CardAt(card).tier == m_tier[side])
                                candidates.push_back(card);
                        if (!candidates.empty())
                        {
                            int const card = candidates[m_rng.Uniform(static_cast<uint32_t>(candidates.size()))];
                            RecordPersistentAddToPoolHand(side, card, false);
                            SummonMinion(side, MakeMinion(card), m_board[side].Size());
                        }
                    }
                    // Bru'kan's invocations (the chosen one stays until changed).
                    if (h.invocationEarth)
                    {
                        // Give 4 random friendly minions "Deathrattle: Summon a 1/1 Elemental".
                        int const token = FindCard("BG22_HERO_001p_t1et");
                        if (token >= 0)
                        {
                            std::vector<uint32_t> ids = BoardIds(side);
                            m_rng.Shuffle(ids);
                            if (ids.size() > 4)
                                ids.resize(4);
                            for (uint32_t id : ids)
                                if (Minion* m = Find(id))
                                {
                                    GrantedEffect g;
                                    g.kind = kGrantedStoneElemental;
                                    m->granted.push_back(g);
                                }
                        }
                    }
                    if (h.invocationFire)
                    {
                        // Double your left-most minion's Attack.
                        std::vector<uint32_t> const ids = BoardIds(side);
                        if (!ids.empty())
                            if (Minion* m = Find(ids.front()))
                                if (m->attack > 0)
                                    Buff(ids.front(), m->attack, 0);
                    }
                    if (h.invocationWater)
                    {
                        // Give your right-most minion +3 Health and Taunt.
                        std::vector<uint32_t> const ids = BoardIds(side);
                        if (!ids.empty())
                        {
                            uint32_t const id = ids.back();
                            Buff(id, 0, 3);
                            GrantKeywords(id, kKwTaunt);
                        }
                    }
                    if (h.invocationLightning)
                    {
                        // Deal 1 damage to 5 random enemy minions.
                        std::vector<uint32_t> ids = BoardIds(1 - side);
                        m_rng.Shuffle(ids);
                        if (ids.size() > 5)
                            ids.resize(5);
                        for (uint32_t id : ids)
                            Damage(0, id, 1);
                    }
                    if (h.tavishAim != 0)
                    {
                        // Tavish: Deal 2 damage to the aimed enemy, doubled every 3 turns (turns 1-3: 2, 4-6: 4 ...).
                        std::vector<uint32_t> const ids = BoardIds(1 - side);
                        if (!ids.empty())
                        {
                            uint32_t target = ids.front();
                            int best = Find(target)->health;
                            for (uint32_t id : ids)
                            {
                                int const health = Find(id)->health;
                                if ((h.tavishAim == 2 && health < best) || (h.tavishAim == 3 && health > best))
                                {
                                    best = health;
                                    target = id;
                                }
                            }
                            if (h.tavishAim == 4)
                                target = ids.back();
                            int const doublings = std::min(8, (std::max(1, m_round) - 1) / 3);
                            Damage(0, target, 2 << doublings);
                        }
                    }
                    ResolveDeaths();
                }

                void RecomputeBoth()
                {
                    // A side's reward board aura (The Smoking Gun, +5 Attack) rides on every minion of that side.
                    for (int s = 0; s < 2; ++s)
                    {
                        std::vector<Minion> const before = m_board[s].Items();
                        AuraBonus extra;
                        RewardState const* const states[2] = { m_reward[s], m_reward2[s] };
                        for (RewardState const* st : states)
                            if (RewardRule const* d = st ? RewardFor(*st) : nullptr)
                                extra.attack += d->boardAttack;
                        RecomputeAuras(m_board[s], m_fx, extra);
                        // what the aura changed is logged, so the client's replay keeps the same numbers
                        for (Minion const& was : before)
                            if (Minion const* now = was.uid != m_placing ? Find(was.uid) : nullptr)
                                LogBuff(was.uid, now->attack - was.attack, now->health - was.health,
                                        now->Keywords() & ~was.Keywords());
                    }
                }

                // Runs a hook with the trigger logged; m_source is the minion whose effect is running.
                void Call(HookKind kind, CombatHook hook, CombatCtx& ctx)
                {
                    CombatEvent e;
                    e.type = EventType::Trigger;
                    e.side = ctx.side;
                    e.actor = ctx.self;
                    e.amount = static_cast<int>(kind);
                    Log(e);
                    uint32_t const prev = m_source;
                    m_source = ctx.self;
                    hook(ctx, ctx.golden);
                    m_source = prev;
                }

                // Fires hook `member` of the living minion `id`, if its card has one.
                void FireSelf(HookKind kind, CombatHook EffectDef::*member, uint32_t id, uint32_t other = 0,
                              int amount = 0, Minion const* dead = nullptr, uint32_t source = 0, int amount2 = 0)
                {
                    Minion* m = Find(id);
                    if (!m || IsDead(*m))
                        return;
                    EffectDef const* e = EffectOf(*m);
                    if (!e || !(e->*member))
                        return;
                    CombatCtx ctx(*this, SideOf(id), id, m->golden);
                    ctx.other = other;
                    ctx.amount = amount;
                    ctx.dead = dead;
                    ctx.source = source;
                    ctx.amount2 = amount2;
                    ctx.position = PositionOf(id) + 1;
                    Call(kind, e->*member, ctx);
                }

                // Fires hook `member` of every living minion of `side` (left to right), except `skip`.
                void Broadcast(HookKind kind, CombatHook EffectDef::*member, int side, uint32_t other, int amount,
                               Minion const* dead, uint32_t skip, uint32_t source = 0, int amount2 = 0)
                {
                    for (uint32_t id : BoardIds(side))
                        if (id != skip)
                            FireSelf(kind, member, id, other, amount, dead, source, amount2);
                }

                // Inserts a minion; used by Summon and Reborn. 0 when the board is full.
                uint32_t Place(int side, Minion const& proto, int position, EventType type, uint32_t actor)
                {
                    Board& b = m_board[side];
                    if (b.Full())
                        return 0;
                    position = std::max(0, std::min(position, b.Size()));
                    Minion m = proto;
                    m.uid = m_nextId++;
                    m.flags = 0;
                    if (m_summonAttack[side] != 0 && m.IsTribe(m_summonTribe[side]))
                        m.Buff(m_summonAttack[side], 0);   // the side's hero power (Millificent)
                    if (m_hero[side].greybough)
                    {
                        m.Buff(1, 2);                       // Greybough: +1/+2 and Taunt to combat summons
                        m.keywords |= kKwTaunt;
                    }
                    b.Insert(position, m);
                    // The attack pointer stays on the same minion: a minion inserted left of it, or right at it from a
                    // summoner that already attacked, waits for the next cycle. A replacement for a dead minion that
                    // had NOT attacked yet (m_noShiftAtPtr) takes that minion's turn.
                    if (position < m_ptr[side] || (position == m_ptr[side] && !m_noShiftAtPtr))
                        ++m_ptr[side];
                    for (Pending& p : m_pending)
                        if (!p.processed && p.side == side && p.slot >= position)
                            ++p.slot;
                    m_placing = m.uid;          // its Summon / Reborn event below carries the stats with the auras
                    RecomputeBoth();
                    m_placing = 0;
                    Minion const* in = Find(m.uid);
                    CombatEvent e;
                    e.type = type;
                    e.side = side;
                    e.actor = actor;
                    e.target = m.uid;
                    e.amount = in->attack;
                    e.amount2 = in->health;
                    e.card = in->card;
                    e.golden = in->golden;
                    e.position = position;
                    e.keywords = in->Keywords();
                    Log(e);
                    Broadcast(HookKind::FriendSummoned, &EffectDef::onFriendSummoned, side, m.uid, 0, nullptr, m.uid);
                    return m.uid;
                }

                // Damage from combat or an effect; returns the health lost.
                int ApplyDamage(uint32_t source, uint32_t target, int amount, bool poisonous)
                {
                    Minion* t = Find(target);
                    if (!t || amount <= 0)
                        return 0;
                    int const side = SideOf(target);
                    CombatEvent e;
                    e.side = side;
                    e.actor = source;
                    e.target = target;
                    if (t->Has(kKwDivineShield))
                    {
                        t->keywords &= ~static_cast<uint32_t>(kKwDivineShield);
                        t->auraKeywords &= ~static_cast<uint32_t>(kKwDivineShield);
                        e.type = EventType::ShieldPop;
                        Log(e);
                        Broadcast(HookKind::ShieldLost, &EffectDef::onShieldLost, side, target, 0, nullptr, 0);
                        return 0;
                    }
                    t->health -= amount;
                    t->killer = source;
                    if (poisonous)
                    {
                        t->flags |= kMinionDestroyed;
                        // Venomous is Poisonous once: it is used up by the first minion it destroys
                        Minion* src = Find(source);
                        if (src && !src->Has(kKwPoisonous) && src->Has(kKwVenomous))
                        {
                            src->keywords &= ~static_cast<uint32_t>(kKwVenomous);
                            src->auraKeywords &= ~static_cast<uint32_t>(kKwVenomous);
                        }
                    }
                    e.type = EventType::Damage;
                    e.amount = amount;
                    Log(e);
                    Broadcast(HookKind::Damaged, &EffectDef::onDamaged, side, target, amount, nullptr, 0);
                    return amount;
                }

                // Removes every dead minion (the attacking side's first, left to right), then resolves their
                // Deathrattles, Reborn and friend-died / Avenge triggers one by one. Repeats while new deaths appear.
                void ResolveDeaths()
                {
                    if (m_resolving)
                        return;
                    m_resolving = true;
                    for (int round = 0; round < 1000; ++round)
                    {
                        std::vector<Pending> batch;
                        for (int k = 0; k < 2; ++k)
                        {
                            int const s = k == 0 ? m_active : 1 - m_active;
                            int removedBefore = 0;
                            for (int i = 0; i < m_board[s].Size(); ++i)
                                if (IsDead(m_board[s][i]))
                                {
                                    Pending p;
                                    p.side = s;
                                    p.slot = i - removedBefore;
                                    p.behind = i < m_ptr[s];
                                    p.originalIndex = i;
                                    p.m = m_board[s][i];
                                    batch.push_back(p);
                                    ++removedBefore;
                                }
                        }
                        if (batch.empty())
                            break;
                        for (int s = 0; s < 2; ++s)
                            for (int i = m_board[s].Size() - 1; i >= 0; --i)
                                if (IsDead(m_board[s][i]))
                                {
                                    if (i < m_ptr[s])
                                        --m_ptr[s];
                                    m_board[s].Remove(i);
                                }
                        for (Pending const& p : batch)
                        {
                            CombatEvent e;
                            e.type = EventType::Death;
                            e.side = p.side;
                            e.target = p.m.uid;
                            e.amount = (p.m.flags & kMinionDestroyed) ? 1 : 0;
                            e.card = p.m.card;
                            e.golden = p.m.golden;
                            e.position = p.originalIndex;
                            Log(e);
                        }
                        RecomputeBoth();     // after the Death events: an aura the dead took along is lost after they go
                        for (Pending const& p : batch)
                            m_dead[p.side].push_back(p.m);
                        m_pending = std::move(batch);
                        for (std::size_t k = 0; k < m_pending.size(); ++k)
                        {
                            m_pending[k].processed = true;
                            Pending const p = m_pending[k];
                            Minion const dead = p.m;
                            // The side of the enemy minions each side killed, in death order.
                            int const killerSide = KillerSide(dead.killer);
                            if (killerSide >= 0 && killerSide != p.side && dead.card >= 0)
                                m_res.kills[killerSide].push_back(dead.card);
                            // Rokara: the friendly minion that killed it gets +1 Attack permanently.
                            if (dead.killer != 0 && m_hero[1 - p.side].rokara)
                            {
                                Minion* killer = Find(dead.killer);
                                if (killer && !IsDead(*killer))
                                    BuffPermanent(dead.killer, 1, 0);
                            }
                            RunDeathrattle(dead, p);
                            if (dead.Has(kKwReborn))
                            {
                                Minion back = MakeMinion(dead.card, dead.golden);
                                back.health = 1;
                                back.keywords &= ~static_cast<uint32_t>(kKwReborn);
                                m_noShiftAtPtr = !p.behind;
                                Place(p.side, back, p.slot, EventType::Reborn, dead.uid);
                                m_noShiftAtPtr = false;
                            }
                            FriendDied(p.side, dead);
                            FireDeathSecrets(p.side, dead, p);
                        }
                        m_pending.clear();
                    }
                    m_resolving = false;
                }

                // Times a Deathrattle of a minion of this side fires: the best Baron on the board, also one that died in
                // the batch being resolved (it died at the same moment).
                int DeathrattleTimes(int side) const
                {
                    int best = TimesFor(m_fx, m_board[side], TimesKind::Deathrattle);
                    for (Pending const& p : m_pending)
                        if (p.side == side)
                        {
                            EffectDef const* e = EffectOf(p.m);
                            if (!e)
                                continue;
                            int const n = p.m.golden && e->deathrattleTimesGolden > 0 ? e->deathrattleTimesGolden : e->deathrattleTimes;
                            best = std::max(best, n);
                        }
                    return best;
                }

                void RunDeathrattle(Minion const& dead, Pending const& p)
                {
                    std::vector<std::pair<CombatHook, bool>> const inst = DeathrattleInstances(m_fx, dead);
                    if (inst.empty())
                        return;
                    int const times = DeathrattleTimes(p.side);
                    int position = p.slot;
                    m_noShiftAtPtr = !p.behind;
                    for (int t = 0; t < times; ++t)
                        for (auto const& in : inst)
                        {
                            CombatCtx ctx(*this, p.side, dead.uid, in.second);
                            ctx.dead = &dead;
                            ctx.position = position;
                            Call(HookKind::Deathrattle, in.first, ctx);
                            position = ctx.position;
                        }
                    m_noShiftAtPtr = false;
                }

                // A Secret leaves play when it triggers; the log gets one Trigger event.
                void UseSecret(int side, int card)
                {
                    std::vector<int>& list = m_secrets[side];
                    list.erase(std::remove(list.begin(), list.end(), card), list.end());
                    m_res.secretsUsed[side].push_back(card);
                    CombatEvent e;
                    e.type = EventType::Trigger;
                    e.side = side;
                    e.amount = static_cast<int>(HookKind::SecretTrigger);
                    e.card = card;
                    Log(e);
                }

                // "When one of your minions is attacked": the Secrets of the attacked side, in play order, before the
                // damage of the attack.
                void FireAttackSecrets(int side, uint32_t target)
                {
                    for (std::size_t i = 0; i < m_secrets[side].size();)
                    {
                        int const card = m_secrets[side][i];
                        bool const venom = card == m_secretVenomstrike;
                        bool const snakeTrap = card == m_secretSnakeTrap;
                        bool const splitting = card == m_secretSplittingImage;
                        bool const autodefense = card == m_secretAutodefense;
                        if (!venom && !snakeTrap && !splitting && !autodefense)
                        {
                            ++i;
                            continue;
                        }
                        Minion* t = Find(target);
                        if (!t || IsDead(*t))
                        {
                            ++i;
                            continue;
                        }
                        int const position = PositionOf(target) + 1;
                        UseSecret(side, card);   // removes list[i]: the next Secret shifts into i
                        if (venom && m_secretCobra >= 0)
                        {
                            SummonMinion(side, MakeMinion(m_secretCobra), position);
                        }
                        else if (snakeTrap && m_secretSnake >= 0)
                        {
                            for (int k = 0; k < 3; ++k)
                                if (!SummonMinion(side, MakeMinion(m_secretSnake), position + k))
                                    break;
                        }
                        else if (splitting)
                        {
                            Minion copy = *t;
                            copy.uid = 0;
                            copy.flags = 0;
                            copy.killer = 0;
                            for (int& c : copy.counters)
                                c = 0;
                            SummonMinion(side, copy, position);
                        }
                        else if (autodefense)
                        {
                            GrantKeywords(target, kKwDivineShield);
                        }
                    }
                }

                // "When a friendly minion dies": Effigy, Avenge, Redemption and Hand of Salvation. The dead minion has
                // already left the board, run its Deathrattle and Reborn, and been seen by FriendDied.
                void FireDeathSecrets(int side, Minion const& dead, Pending const& p)
                {
                    ++m_friendlyDeaths[side];
                    for (std::size_t i = 0; i < m_secrets[side].size();)
                    {
                        int const card = m_secrets[side][i];
                        bool const effigy = card == m_secretEffigy;
                        bool const avenge = card == m_secretAvenge;
                        bool const redemption = card == m_secretRedemption;
                        bool const salvation = card == m_secretSalvation && m_friendlyDeaths[side] == 2;
                        if (!effigy && !avenge && !redemption && !salvation)
                        {
                            ++i;
                            continue;
                        }
                        UseSecret(side, card);
                        if (effigy)
                        {
                            // A random minion of the dead minion's Tavern Tier (BG costs are flat: the Tier stands in).
                            std::vector<int> cands;
                            for (int c : PoolCandidates(side, 0, false))
                                if (CardAt(c).tier == dead.Tier())
                                    cands.push_back(c);
                            if (!cands.empty())
                            {
                                int const summon = cands[m_rng.Uniform(static_cast<uint32_t>(cands.size()))];
                                m_noShiftAtPtr = !p.behind;
                                SummonMinion(side, MakeMinion(summon), p.slot);
                                m_noShiftAtPtr = false;
                            }
                        }
                        else if (avenge)
                        {
                            std::vector<uint32_t> const friends = BoardIds(side);
                            if (!friends.empty())
                                BuffPermanent(friends[m_rng.Uniform(static_cast<uint32_t>(friends.size()))], 3, 2);
                        }
                        else if (redemption || salvation)
                        {
                            Minion back = dead;
                            back.flags = 0;
                            back.killer = 0;
                            back.health = redemption ? 1 : back.maxHealth;
                            m_noShiftAtPtr = !p.behind;
                            Place(p.side, back, p.slot, EventType::Summon, dead.uid);
                            m_noShiftAtPtr = false;
                        }
                    }
                }

                // onFriendDied for every living friend, then the Avenge count of the ones that have it.
                void FriendDied(int side, Minion const& dead)
                {
                    // Ritual Dagger (a friendly Deathrattle minion that dies gets +4/+4 permanently).
                    RewardState const* const rewardStates[2] = { m_reward[side], m_reward2[side] };
                    for (RewardState const* const st : rewardStates)
                        if (RewardRule const* const d = st ? RewardFor(*st) : nullptr)
                            if (d->kind == RewardKind::RitualDagger && !DeathrattleInstances(m_fx, dead).empty())
                                RecordPersistentBuff(dead.uid, 4, 4, 0);
                    for (uint32_t id : BoardIds(side))
                    {
                        FireSelf(HookKind::FriendDied, &EffectDef::onFriendDied, id, dead.uid, 0, &dead);
                        Minion* m = Find(id);
                        if (!m || IsDead(*m))
                            continue;
                        EffectDef const* e = EffectOf(*m);
                        if (!e || e->avengeCount <= 0)
                            continue;
                        int const need = (m->golden && e->avengeCountGolden > 0) ? e->avengeCountGolden : e->avengeCount;
                        if (++m->counters[kCounterAvenge] >= need)
                        {
                            m->counters[kCounterAvenge] = 0;
                            FireSelf(HookKind::Avenge, &EffectDef::avenge, id, dead.uid, 0, &dead);
                        }
                    }
                    // Hero Avenge powers (Drek'Thar, Vanndar Stormpike, Onyxia).
                    HeroCombatDef const& h = m_hero[side];
                    if (h.avengeCount > 0 && ++m_heroAvenge[side] >= h.avengeCount)
                    {
                        m_heroAvenge[side] = 0;
                        if (h.avengeAttack != 0 || h.avengeHealth != 0)
                            for (uint32_t id : BoardIds(side))
                                BuffPermanent(id, h.avengeAttack, h.avengeHealth);
                        if (h.avengeWhelp)
                        {
                            int const whelp = FindCard("BG22_HERO_305t");
                            if (whelp >= 0)
                            {
                                uint32_t const id = SummonMinion(side, MakeMinion(whelp), m_board[side].Size());
                                if (id != 0)
                                    DoAttack(side, id);
                            }
                        }
                    }
                }

                uint32_t ChooseTarget(int side, uint32_t attackerId)
                {
                    int const enemy = 1 - side;
                    Minion* a = Find(attackerId);
                    EffectDef const* e = a ? EffectOf(*a) : nullptr;
                    if (e && e->chooseTarget)
                    {
                        CombatCtx ctx(*this, side, attackerId, a->golden);
                        ctx.position = PositionOf(attackerId) + 1;
                        CombatEvent ev;
                        ev.type = EventType::Trigger;
                        ev.side = side;
                        ev.actor = attackerId;
                        ev.amount = static_cast<int>(HookKind::ChooseTarget);
                        Log(ev);
                        uint32_t const t = e->chooseTarget(ctx, ctx.golden);
                        if (t != 0 && SideOf(t) == enemy)
                        {
                            Minion* tm = Find(t);
                            if (tm && !IsDead(*tm))
                                return t;
                        }
                    }
                    std::vector<uint32_t> all = BoardIds(enemy);
                    std::vector<uint32_t> taunts;
                    std::vector<uint32_t> visible;   // Stealthed minions are not a valid attack target
                    for (uint32_t id : all)
                    {
                        Minion* m = Find(id);
                        if (m->Has(kKwStealth))
                            continue;
                        visible.push_back(id);
                        if (m->Has(kKwTaunt))
                            taunts.push_back(id);
                    }
                    // A board that is entirely Stealthed can still be attacked (no stall); a Stealthed Taunt does not taunt.
                    std::vector<uint32_t> const& pool = visible.empty() ? all : visible;
                    std::vector<uint32_t> const& from = taunts.empty() ? pool : taunts;
                    if (from.empty())
                        return 0;
                    return from[m_rng.Uniform(static_cast<uint32_t>(from.size()))];
                }

                // One attack of `aid`. False when it could not attack at all.
                bool DoAttack(int side, uint32_t aid, uint32_t forcedTarget = 0)
                {
                    Minion* a = Find(aid);
                    if (!a || IsDead(*a) || a->attack <= 0)
                        return false;
                    uint32_t const did = forcedTarget != 0 ? forcedTarget : ChooseTarget(side, aid);
                    if (did == 0)
                        return false;
                    CombatEvent ev;
                    ev.type = EventType::Attack;
                    ev.side = side;
                    ev.actor = aid;
                    ev.target = did;
                    Log(ev);
                    ++m_res.attacks;
                    a->keywords &= ~static_cast<uint32_t>(kKwStealth);   // attacking breaks Stealth
                    FireSelf(HookKind::Attack, &EffectDef::onAttack, aid, did);
                    ResolveDeaths();
                    a = Find(aid);
                    Minion* d = Find(did);
                    if (!a || !d || IsDead(*a) || IsDead(*d))
                        return true; // the attack was cancelled by its own trigger
                    Broadcast(HookKind::FriendAttacked, &EffectDef::onFriendAttacked, SideOf(did), did, 0, nullptr, 0, aid);
                    Broadcast(HookKind::FriendlyAttack, &EffectDef::onFriendlyAttack, side, aid, 0, nullptr, 0, aid);
                    ResolveDeaths();
                    // The Secrets of the attacked side trigger before the damage.
                    FireAttackSecrets(1 - side, did);
                    ResolveDeaths();
                    a = Find(aid);
                    d = Find(did);
                    if (!a || !d)
                        return true;
                    EffectDef const* ae = EffectOf(*a);
                    std::vector<uint32_t> cleaved;
                    if (ae && ae->cleave)
                        cleaved = Neighbors(did);
                    int const aAttack = a->attack;
                    int const dAttack = d->attack;
                    bool const dShielded = d->Has(kKwDivineShield);
                    int const dHealthBefore = d->health;
                    bool const aPoison = a->Has(kKwPoisonous | kKwVenomous);
                    bool const dPoison = d->Has(kKwPoisonous | kKwVenomous);
                    bool const excessToAdjacent = ae && ae->excessToAdjacent;
                    bool const aGolden = a->golden;
                    std::vector<uint32_t> const adjacent = excessToAdjacent ? Neighbors(did) : std::vector<uint32_t>();
                    int const dealt = ApplyDamage(aid, did, aAttack, aPoison);
                    for (uint32_t n : cleaved)
                        ApplyDamage(aid, n, aAttack, aPoison);
                    ApplyDamage(did, aid, dAttack, dPoison);
                    d = Find(did); // a damage trigger may have summoned into the board and moved the minions
                    bool const killed = !dShielded && d && (d->health <= 0 || (d->flags & kMinionDestroyed) != 0);
                    ResolveDeaths();
                    // Wildfire Elemental: the excess damage of a killing attack hits an adjacent minion (golden: both).
                    if (excessToAdjacent && killed && aAttack > dHealthBefore && Find(aid))
                    {
                        int const excess = aAttack - dHealthBefore;
                        std::vector<uint32_t> living;
                        for (uint32_t n : adjacent)
                            if (Find(n) && !IsDead(*Find(n)))
                                living.push_back(n);
                        if (!living.empty())
                        {
                            if (aGolden)
                                for (uint32_t n : living)
                                    ApplyDamage(aid, n, excess, false);
                            else
                                ApplyDamage(aid, living[m_rng.Uniform(static_cast<uint32_t>(living.size()))], excess, false);
                            ResolveDeaths();
                        }
                    }
                    FireSelf(HookKind::Attacked, &EffectDef::onAttacked, did, aid, dealt);
                    FireSelf(HookKind::AfterAttack, &EffectDef::afterAttack, aid, did, dealt);
                    ResolveDeaths();
                    return true;
                }

                // The side's next attacker (from the attack pointer, skipping 0 attack) attacks once, or
                // twice / four times with Windfury. False if no minion of the side can attack (it passes).
                bool TakeTurn(int side)
                {
                    Board& b = m_board[side];
                    int const n = b.Size();
                    if (n == 0)
                        return false;
                    if (m_ptr[side] >= n)
                        m_ptr[side] = 0;
                    int found = -1;
                    for (int k = 0; k < n; ++k)
                    {
                        int const i = (m_ptr[side] + k) % n;
                        if (!IsDead(b[i]) && b[i].attack > 0)
                        {
                            found = i;
                            break;
                        }
                    }
                    if (found < 0)
                        return false;
                    uint32_t const aid = b[found].uid;
                    int const count = AttacksPerTurn(b[found].Keywords());
                    m_ptr[side] = found + 1;
                    for (int k = 0; k < count; ++k)
                    {
                        Minion* a = Find(aid);
                        if (!a || IsDead(*a) || Ended())
                            break;
                        if (!DoAttack(side, aid) || Ended())
                            break;
                    }
                    return true;
                }

                Board m_board[2];
                int m_tier[2] = { 1, 1 };
                int m_ptr[2] = { 0, 0 };   // index of the next attacker on each side
                Rng& m_rng;
                EffectRegistry const& m_fx;
                bool m_cap;
                uint32_t m_lobby[2] = { 0, 0 };   // each side's own faction tribes
                bool m_slice;
                // Each side's Secrets (card indexes) and the card ids the engine looks up once.
                std::vector<int> m_secrets[2];
                int m_friendlyDeaths[2] = { 0, 0 };
                int m_secretVenomstrike = -1;
                int m_secretSnakeTrap = -1;
                int m_secretSplittingImage = -1;
                int m_secretEffigy = -1;
                int m_secretAutodefense = -1;
                int m_secretAvenge = -1;
                int m_secretRedemption = -1;
                int m_secretSalvation = -1;
                int m_secretCobra = -1;
                int m_secretSnake = -1;
                RewardState const* m_reward[2] = { nullptr, nullptr };   // the reward of each side
                RewardState const* m_reward2[2] = { nullptr, nullptr };  // the second reward (Denathrius)
                uint32_t m_summonTribe[2] = { 0, 0 };   // the hero power buff of each side's summons (Millificent)
                int m_summonAttack[2] = { 0, 0 };
                HeroCombatDef m_hero[2];                 // hero powers that act in combat
                int m_round = 1;                         // Tavish's aim doubling
                int m_heroAvenge[2] = { 0, 0 };          // their Avenge progress
                bool m_inPermanent = false;
                std::vector<Minion> m_dead[2];   // minions that died, per side, in order
                CombatResult m_res;
                uint32_t m_nextId = kFirstSummonId;
                uint32_t m_placing = 0;   // the minion Place is inserting, not logged yet (RecomputeBoth skips it)
                uint32_t m_source = 0;     // the minion whose effect is running
                int m_extraAttackDepth = 0; // nested ExtraAttack / AttackNow calls
                int m_active = 0;          // side whose deaths resolve first
                bool m_resolving = false;
                bool m_noShiftAtPtr = false;
                std::vector<Pending> m_pending;
            };
        }

        int CombatDamage(int tavernTier, Board const& survivors, bool cap)
        {
            int d = tavernTier;
            for (int i = 0; i < survivors.Size(); ++i)
                d += survivors[i].Tier();
            return cap ? std::min(d, kDamageCap) : d;
        }

        CombatResult SimulateCombat(CombatSide const& a, CombatSide const& b, Rng& rng, CombatSettings const& settings)
        {
            Sim sim(a, b, rng, settings);
            return sim.Run();
        }

        CombatResult SimulateCombat(CombatSide const& a, CombatSide const& b, uint32_t seed, CombatSettings const& settings)
        {
            Rng rng(seed);
            return SimulateCombat(a, b, rng, settings);
        }
    }
}
