#include "HsBgTypes.h"

#include <map>

namespace Hs
{
    namespace Bg
    {
        namespace
        {
            CardDef const kCards[] = {
#include "HsBgCardData.inc"
            };

            HeroDef const kHeroes[] = {
#include "HsBgHeroData.inc"
            };

            QuestDef const kQuests[] = {
#include "HsBgQuestData.inc"
            };

            RewardDef const kRewards[] = {
#include "HsBgRewardData.inc"
            };

            // id -> index, normal ids and golden ids (golden flagged), built once.
            struct Index
            {
                std::map<std::string, int> normal;
                std::map<std::string, int> golden;
                std::map<std::string, int> heroes;
                std::map<std::string, int> quests;
                std::map<std::string, int> rewards;
                Index()
                {
                    for (int i = 0; i < static_cast<int>(sizeof(kCards) / sizeof(kCards[0])); ++i)
                    {
                        normal[kCards[i].id] = i;
                        if (kCards[i].goldenId[0] != '\0')
                            golden[kCards[i].goldenId] = i;
                    }
                    for (int i = 0; i < static_cast<int>(sizeof(kHeroes) / sizeof(kHeroes[0])); ++i)
                        heroes[kHeroes[i].id] = i;
                    for (int i = 0; i < static_cast<int>(sizeof(kQuests) / sizeof(kQuests[0])); ++i)
                        quests[kQuests[i].id] = i;
                    for (int i = 0; i < static_cast<int>(sizeof(kRewards) / sizeof(kRewards[0])); ++i)
                        rewards[kRewards[i].id] = i;
                }
            };

            Index const& TheIndex()
            {
                static Index const index;
                return index;
            }
        }

        int CardCount() { return static_cast<int>(sizeof(kCards) / sizeof(kCards[0])); }
        CardDef const& CardAt(int index) { return kCards[index]; }

        int FindCard(std::string const& id)
        {
            auto it = TheIndex().normal.find(id);
            return it == TheIndex().normal.end() ? -1 : it->second;
        }

        int FindCardAny(std::string const& id, bool& golden)
        {
            int const n = FindCard(id);
            if (n >= 0)
            {
                golden = false;
                return n;
            }
            auto it = TheIndex().golden.find(id);
            if (it == TheIndex().golden.end())
                return -1;
            golden = true;
            return it->second;
        }

        int HeroCount() { return static_cast<int>(sizeof(kHeroes) / sizeof(kHeroes[0])); }
        HeroDef const& HeroAt(int index) { return kHeroes[index]; }

        int FindHero(std::string const& id)
        {
            auto it = TheIndex().heroes.find(id);
            return it == TheIndex().heroes.end() ? -1 : it->second;
        }

        void ArmorTierRange(int tier, int& lo, int& hi)
        {
            static int const loByTier[] = { 0, 0, 2, 3, 4, 5, 6, 7 };
            static int const hiByTier[] = { 0, 0, 5, 6, 7, 8, 9, 10 };
            if (tier < 1 || tier > 7)
            {
                lo = hi = 0;
                return;
            }
            lo = loByTier[tier];
            hi = hiByTier[tier];
        }

        int QuestCount() { return static_cast<int>(sizeof(kQuests) / sizeof(kQuests[0])); }
        QuestDef const& QuestAt(int index) { return kQuests[index]; }

        int FindQuest(std::string const& id)
        {
            auto it = TheIndex().quests.find(id);
            return it == TheIndex().quests.end() ? -1 : it->second;
        }

        int RewardCount() { return static_cast<int>(sizeof(kRewards) / sizeof(kRewards[0])); }
        RewardDef const& RewardAt(int index) { return kRewards[index]; }

        int FindReward(std::string const& id)
        {
            auto it = TheIndex().rewards.find(id);
            return it == TheIndex().rewards.end() ? -1 : it->second;
        }

        std::vector<int> QueryCards(CardQuery const& q)
        {
            std::vector<int> out;
            for (int i = 0; i < CardCount(); ++i)
            {
                CardDef const& c = kCards[i];
                if (c.role == CardRole::Spell)
                    continue;   // a spell is never drawn for the tavern, a Discover or a summon
                if (c.role == CardRole::Pool ? !q.pool : !q.tokens)
                    continue;
                if (c.tier < q.minTier || c.tier > q.maxTier)
                    continue;
                if (q.slice15AOnly && !c.slice15A)
                    continue;
                if (q.tribeAny != 0 && !HasTribe(c.tribes, q.tribeAny))
                    continue;
                out.push_back(i);
            }
            return out;
        }

        std::vector<int> CardsOfTier(int tier, bool slice15AOnly)
        {
            CardQuery q;
            q.minTier = q.maxTier = tier;
            q.slice15AOnly = slice15AOnly;
            return QueryCards(q);
        }

        std::vector<int> CardsOfTribe(uint32_t tribe, bool slice15AOnly)
        {
            CardQuery q;
            q.tribeAny = tribe;
            q.slice15AOnly = slice15AOnly;
            return QueryCards(q);
        }

        uint32_t Rng::Uniform(uint32_t bound)
        {
            if (bound <= 1)
                return 0;
            // Reject the top sliver of the 32-bit range so every residue is equally likely.
            uint64_t const range = uint64_t(1) << 32;
            uint64_t const limit = range - range % bound; // values below this are accepted
            uint32_t r;
            do
                r = Next();
            while (r >= limit);
            return r % bound;
        }

        int Rng::Range(int lo, int hi)
        {
            if (hi <= lo)
                return lo;
            return lo + static_cast<int>(Uniform(static_cast<uint32_t>(hi - lo) + 1u));
        }

        Minion MakeMinion(int card, bool golden, uint32_t uid)
        {
            Minion m;
            m.card = card;
            m.golden = golden;
            m.uid = uid;
            CardDef const& d = CardAt(card);
            m.attack = golden ? d.goldenAttack : d.attack;
            m.health = m.maxHealth = golden ? d.goldenHealth : d.health;
            m.keywords = golden ? d.goldenKeywords : d.keywords;
            return m;
        }
    }
}
