#include "HsBgEffects.h"

#include <algorithm>
#include <utility>

namespace Hs
{
    namespace Bg
    {
        namespace
        {
            struct Batch
            {
                std::string name;
                EffectBatchFn fn;
            };

            std::vector<Batch>& Batches()
            {
                static std::vector<Batch> batches;
                return batches;
            }
        }

        uint32_t CombatCtx::Summon(int card, bool goldenMinion)
        {
            return Summon(MakeMinion(card, goldenMinion));
        }

        uint32_t CombatCtx::Summon(Minion const& proto)
        {
            uint32_t last = 0;
            int const times = api.SummonTimes(side);
            for (int i = 0; i < times; ++i)
            {
                uint32_t const id = SummonOne(proto);
                if (id != 0)
                    last = id;
                else
                    ++failedSummons;
            }
            return last;
        }

        uint32_t CombatCtx::SummonOne(int card, bool goldenMinion)
        {
            return SummonOne(MakeMinion(card, goldenMinion));
        }

        uint32_t CombatCtx::SummonOne(Minion const& proto)
        {
            uint32_t const id = api.SummonMinion(side, proto, position);
            if (id != 0)
                ++position;
            return id;
        }

        EffectRegistry const& CombatApi::Effects() const { return EffectRegistry::Global(); }

        int CombatApi::RandomPoolCard(int side, uint32_t tribeAny, bool deathrattleOnly, int excludeCard)
        {
            std::vector<int> c = PoolCandidates(side, tribeAny, deathrattleOnly);
            c.erase(std::remove(c.begin(), c.end(), excludeCard), c.end());
            if (c.empty())
                return -1;
            return c[Random().Uniform(static_cast<uint32_t>(c.size()))];
        }

        std::vector<uint32_t> CombatApi::Neighbors(uint32_t id)
        {
            std::vector<uint32_t> out;
            int const side = SideOf(id);
            if (side < 0)
                return out;
            std::vector<uint32_t> const ids = BoardIds(side);
            for (std::size_t i = 0; i < ids.size(); ++i)
                if (ids[i] == id)
                {
                    if (i > 0)
                        out.push_back(ids[i - 1]);
                    if (i + 1 < ids.size())
                        out.push_back(ids[i + 1]);
                    break;
                }
            return out;
        }

        // ------------------------------------------------------------------------------------------ helpers

        int CountTribe(Board const& board, uint32_t tribe, int exceptIndex)
        {
            int n = 0;
            for (int i = 0; i < board.Size(); ++i)
                if (i != exceptIndex && board[i].IsTribe(tribe))
                    ++n;
            return n;
        }

        uint32_t TribesNotControlled(Board const& board)
        {
            uint32_t have = 0;
            for (int i = 0; i < board.Size(); ++i)
            {
                uint32_t const t = board[i].Tribes();
                have |= (t & kTribeAll) ? (kTribeAll - 1u) : t;
            }
            return (kTribeAll - 1u) & ~have;
        }

        namespace
        {
            constexpr int kTypeCount = 9; // single-bit tribes

            // Kuhn augmenting path: minion m takes a type, displacing an owner who then looks for another.
            bool TryMatch(int m, std::vector<uint32_t> const& masks, std::vector<int> const& typeOrder,
                          std::vector<int>& typeOwner, std::vector<char>& seen)
            {
                uint32_t const mask = masks[static_cast<std::size_t>(m)];
                uint32_t const types = (mask & kTribeAll) ? (kTribeAll - 1u) : (mask & (kTribeAll - 1u));
                for (int t : typeOrder)
                {
                    if (!(types & (1u << t)) || seen[static_cast<std::size_t>(t)])
                        continue;
                    seen[static_cast<std::size_t>(t)] = 1;
                    int const owner = typeOwner[static_cast<std::size_t>(t)];
                    if (owner < 0 || TryMatch(owner, masks, typeOrder, typeOwner, seen))
                    {
                        typeOwner[static_cast<std::size_t>(t)] = m;
                        return true;
                    }
                }
                return false;
            }

            std::vector<int> Match(std::vector<uint32_t> const& masks, std::vector<int> const& order, std::vector<int> const& typeOrder)
            {
                std::vector<int> owner(kTypeCount, -1);
                for (int m : order)
                {
                    std::vector<char> seen(kTypeCount, 0);
                    TryMatch(m, masks, typeOrder, owner, seen);
                }
                std::vector<int> matched;
                for (int t = 0; t < kTypeCount; ++t)
                    if (owner[static_cast<std::size_t>(t)] >= 0)
                        matched.push_back(owner[static_cast<std::size_t>(t)]);
                return matched;
            }

            std::vector<int> Iota(int n)
            {
                std::vector<int> v;
                for (int i = 0; i < n; ++i)
                    v.push_back(i);
                return v;
            }
        }

        std::vector<int> PickDistinctTypes(std::vector<uint32_t> const& tribeMasks, Rng& rng, int maxPicks)
        {
            std::vector<int> order = Iota(static_cast<int>(tribeMasks.size()));
            std::vector<int> typeOrder = Iota(kTypeCount);
            rng.Shuffle(order);
            rng.Shuffle(typeOrder);
            std::vector<int> matched = Match(tribeMasks, order, typeOrder);
            rng.Shuffle(matched);
            if (static_cast<int>(matched.size()) > maxPicks)
                matched.resize(static_cast<std::size_t>(std::max(0, maxPicks)));
            std::sort(matched.begin(), matched.end());
            return matched;
        }

        int CountDistinctTypes(std::vector<uint32_t> const& tribeMasks)
        {
            return static_cast<int>(Match(tribeMasks, Iota(static_cast<int>(tribeMasks.size())), Iota(kTypeCount)).size());
        }

        std::vector<uint32_t> TribeMasks(Board const& board)
        {
            std::vector<uint32_t> out;
            for (int i = 0; i < board.Size(); ++i)
                out.push_back(board[i].Tribes());
            return out;
        }

        namespace
        {
            // Sneed's Replicator: "Deathrattle: Summon a random minion from a Tavern Tier lower." The host's Tier is
            // read from the dead copy; it summons from exactly one Tier below, and a Tier 1 host summons nothing.
            void SneedLowerTierDeathrattle(CombatCtx& c, bool)
            {
                int const tier = c.dead ? c.dead->Tier() : 0;
                if (tier <= kMinTier)
                    return;
                std::vector<int> candidates;
                for (int card : c.api.PoolCandidates(c.side, 0, false))
                    if (CardAt(card).tier == tier - 1)
                        candidates.push_back(card);
                if (candidates.empty())
                    return;
                int const card = candidates[c.api.Random().Uniform(static_cast<uint32_t>(candidates.size()))];
                c.Summon(card, false);
            }

            // Bru'kan's Earth Invocation, "Deathrattle: Summon a 1/1 Elemental" (the Stone Elemental).
            void StoneElementalDeathrattle(CombatCtx& c, bool)
            {
                int const card = FindCard("BG22_HERO_001p_t1et");
                if (card >= 0)
                    c.Summon(card, false);
            }
        }

        std::vector<GrantedEffect> DeathrattleSources(EffectRegistry const& registry, Minion const& m)
        {
            std::vector<GrantedEffect> out;
            auto add = [&](int card, bool golden)
            {
                EffectDef const* e = registry.Find(card);
                if (e && e->deathrattle)
                {
                    GrantedEffect g;
                    g.card = card;
                    g.golden = golden;
                    out.push_back(g);
                }
            };
            if (m.card >= 0)
                add(m.card, m.golden);
            for (Attachment const& a : m.attachments)
                add(a.card, a.golden);
            for (GrantedEffect const& g : m.granted)
            {
                if (g.kind == kGrantedSneedLowerTier || g.kind == kGrantedStoneElemental)
                    out.push_back(g);
                else
                    add(g.card, g.golden);
            }
            return out;
        }

        std::vector<std::pair<CombatHook, bool>> DeathrattleInstances(EffectRegistry const& registry, Minion const& m)
        {
            std::vector<std::pair<CombatHook, bool>> out;
            for (GrantedEffect const& g : DeathrattleSources(registry, m))
            {
                if (g.kind == kGrantedSneedLowerTier)
                    out.push_back(std::make_pair(&SneedLowerTierDeathrattle, g.golden));
                else if (g.kind == kGrantedStoneElemental)
                    out.push_back(std::make_pair(&StoneElementalDeathrattle, g.golden));
                else
                {
                    EffectDef const* e = registry.Find(g.card);
                    if (e && e->deathrattle)
                        out.push_back(std::make_pair(e->deathrattle, g.golden));
                }
            }
            return out;
        }

        bool HasDeathrattle(EffectRegistry const& registry, Minion const& m)
        {
            return !DeathrattleInstances(registry, m).empty();
        }

        int TimesFor(EffectRegistry const& registry, Board const& board, TimesKind kind)
        {
            int best = 1;
            for (int i = 0; i < board.Size(); ++i)
            {
                EffectDef const* e = board[i].card < 0 ? nullptr : registry.Find(board[i].card);
                if (!e)
                    continue;
                int n = 0;
                bool const g = board[i].golden;
                switch (kind)
                {
                case TimesKind::Deathrattle: n = g && e->deathrattleTimesGolden > 0 ? e->deathrattleTimesGolden : e->deathrattleTimes; break;
                case TimesKind::Battlecry: n = g && e->battlecryTimesGolden > 0 ? e->battlecryTimesGolden : e->battlecryTimes; break;
                case TimesKind::Summon: n = g && e->summonTimesGolden > 0 ? e->summonTimesGolden : e->summonTimes; break;
                }
                best = std::max(best, n);
            }
            return best;
        }

        std::vector<int> SpellcraftSpells(int maxTier)
        {
            std::vector<int> out;
            EffectRegistry const& reg = EffectRegistry::Global();
            for (int c = 0; c < CardCount(); ++c)
            {
                CardDef const& d = CardAt(c);
                if (d.role != CardRole::Spell || d.tier > maxTier)
                    continue;
                EffectDef const* e = reg.Find(c);
                if (e && e->isSpellcraftSpell)
                    out.push_back(c);
            }
            return out;
        }

        std::vector<Minion>& ShopApi::Offers(int)
        {
            static std::vector<Minion> none;
            none.clear();
            return none;
        }

        Minion* ShopApi::FindMinion(int player, uint32_t uid)
        {
            int const b = PlayerBoard(player).IndexOfUid(uid);
            if (b >= 0)
                return &PlayerBoard(player)[b];
            int const h = PlayerHand(player).IndexOfUid(uid);
            return h >= 0 ? &PlayerHand(player)[h] : nullptr;
        }

        EffectRegistry const& ShopApi::Effects() const { return EffectRegistry::Global(); }

        void RecomputeAuras(Board& board, EffectRegistry const& registry, AuraBonus const& extra)
        {
            std::vector<AuraBonus> bonus(static_cast<std::size_t>(board.Size()));
            for (int i = 0; i < board.Size(); ++i)
            {
                Minion const& m = board[i];
                if (m.card < 0)
                    continue;
                EffectDef const* e = registry.Find(m.card);
                if (e && e->aura)
                {
                    AuraCtx ctx{ board, i, m.golden, bonus };
                    e->aura(ctx, m.golden);
                }
            }
            for (int i = 0; i < board.Size(); ++i)
            {
                Minion& m = board[i];
                AuraBonus b = bonus[static_cast<std::size_t>(i)];
                b.attack += extra.attack;      // a player-level aura (The Smoking Gun) applies to every minion
                b.health += extra.health;
                b.keywords |= extra.keywords;
                m.attack += b.attack - m.auraAttack;
                int const dHealth = b.health - m.auraHealth;
                m.maxHealth += dHealth;
                if (dHealth > 0)
                    m.health += dHealth;
                else if (dHealth < 0)
                    m.health = std::max(1, std::min(m.health, m.maxHealth));
                m.auraAttack = b.attack;
                m.auraHealth = b.health;
                m.auraKeywords = b.keywords;
            }
        }

        EffectRegistry::EffectRegistry() : m_slot(static_cast<std::size_t>(CardCount()), -1) {}

        bool EffectRegistry::Register(char const* cardId, EffectDef const& def)
        {
            bool golden = false;
            int const card = FindCardAny(cardId, golden);
            if (card < 0)
            {
                m_errors.push_back(std::string("unknown card id ") + cardId);
                return false;
            }
            if (golden)
            {
                m_errors.push_back(std::string("golden card id ") + cardId + " (register the normal id)");
                return false;
            }
            if (m_slot[static_cast<std::size_t>(card)] >= 0)
            {
                m_errors.push_back(std::string("duplicate entry for ") + cardId);
                return false;
            }
            m_slot[static_cast<std::size_t>(card)] = static_cast<int>(m_defs.size());
            m_defs.push_back(def);
            return true;
        }

        EffectDef const* EffectRegistry::Find(int cardIndex) const
        {
            if (cardIndex < 0 || cardIndex >= static_cast<int>(m_slot.size()))
                return nullptr;
            int const s = m_slot[static_cast<std::size_t>(cardIndex)];
            return s < 0 ? nullptr : &m_defs[static_cast<std::size_t>(s)];
        }

        EffectRegistry const& EffectRegistry::Global()
        {
            static EffectRegistry const registry = []
            {
                EffectRegistry r;
                RegisterAllEffects(r);
                return r;
            }();
            return registry;
        }

        EffectBatchRegistrar::EffectBatchRegistrar(char const* name, EffectBatchFn fn)
        {
            Batches().push_back(Batch{ name, fn });
        }

        void RegisterAllEffects(EffectRegistry& registry)
        {
            std::vector<Batch> sorted = Batches();
            std::sort(sorted.begin(), sorted.end(), [](Batch const& a, Batch const& b) { return a.name < b.name; });
            for (Batch const& b : sorted)
                b.fn(registry);
        }
    }
}
