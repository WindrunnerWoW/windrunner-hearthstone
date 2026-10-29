#include "HsArenaDraftAi.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace Hs
{
    namespace
    {
        struct ScoreRow
        {
            char const* cls;
            char const* id;
            int score;
        };

        // The generated table (tools/arena/gen_draft_scores.py): { class, card id, 1..100 }.
        constexpr ScoreRow kScoreRows[] = {
#include "HsArenaDraftScores.inc"
        };

        // class + '|' + id -> table score.
        std::unordered_map<std::string, int> const& ScoreTable()
        {
            static std::unordered_map<std::string, int> const table = []
            {
                std::unordered_map<std::string, int> t;
                for (ScoreRow const& row : kScoreRows)
                    t[std::string(row.cls) + "|" + row.id] = row.score;
                return t;
            }();
            return table;
        }

        template <typename T>
        T Clamp(T value, T lo, T hi)
        {
            return std::min(std::max(value, lo), hi);
        }

        std::string Lower(std::string s)
        {
            for (char& c : s)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }

        // ---- card value ------------------------------------------------------------------------------
        // Both sources land on one scale, about 5..90. A card the library decks used is 30 + 0.6 * table score
        // (30..90, rarity is already in the usage). A card they never used starts at 30 and moves by its stats
        // against the vanilla curve (5..60), plus a small rarity bonus.
        constexpr double kValueBase = 30.0;
        constexpr double kTableScale = 0.6;
        constexpr double kStatWeight = 6.0;    // value per stat point above (or below) the vanilla curve
        constexpr double kKeywordStats = 1.0;  // a keyword counts as one extra stat point
        constexpr double kSpellDefault = 35.0; // a spell the library never used: a bit above an average minion
        constexpr double kFallbackMin = 5.0;
        constexpr double kFallbackMax = 60.0;

        double RarityBonus(Rarity r)
        {
            switch (r)
            {
                case Rarity::Rare: return 3.0;
                case Rarity::Epic: return 6.0;
                case Rarity::Legendary: return 10.0;
                default: return 0.0;
            }
        }

        // The fallback for a card missing from the table, from the engine's definition.
        //   minion : attack + health (+1 per keyword) against the vanilla curve 2 * cost + 1
        //   weapon : attack + durability against cost + 2
        //   spell and the rest : a fixed mid value
        // plus the rarity bonus (rarer cards carry stronger effects than their stats show).
        double FallbackValue(Match::CardDef const& def, Rarity rarity)
        {
            double value = kSpellDefault;
            if (def.type == Match::CardType::Minion)
            {
                int const keywords = (def.taunt ? 1 : 0) + (def.charge ? 1 : 0) + (def.divineShield ? 1 : 0) +
                    (def.windfury ? 1 : 0) + (def.stealth ? 1 : 0) + (def.poisonous ? 1 : 0) + (def.spellDamage > 0 ? 1 : 0);
                double const delta = def.attack + def.health + keywords * kKeywordStats - (2.0 * def.cost + 1.0);
                value = Clamp(kValueBase + kStatWeight * delta, kFallbackMin, kFallbackMax);
            }
            else if (def.type == Match::CardType::Weapon)
            {
                double const delta = def.attack + def.durability - (def.cost + 2.0);
                value = Clamp(kValueBase + kStatWeight * delta, kFallbackMin, kFallbackMax);
            }
            return value + RarityBonus(rarity);
        }

        double CardValue(std::string const& deckClass, std::string const& id, Match::CardDef const& def, Rarity rarity)
        {
            auto const& table = ScoreTable();
            auto it = table.find(deckClass + "|" + id);
            if (it != table.end())
                return kValueBase + kTableScale * it->second;
            return FallbackValue(def, rarity);
        }

        // ---- the deck so far -------------------------------------------------------------------------
        struct DeckProfile
        {
            std::array<int, kArenaCurveBuckets> curve {};
            int minions = 0;
            std::unordered_map<std::string, int> races; // drafted minions per race (lower case)
            std::vector<std::string> texts;             // lower-case text of every drafted card
        };

        DeckProfile ProfileOf(std::vector<std::string> const& picks, Match::CardDb const& db)
        {
            DeckProfile p;
            for (std::string const& id : picks)
            {
                Match::CardDef const* def = db.Find(id);
                if (!def)
                    continue;
                ++p.curve[ArenaCurveBucket(def->cost)];
                if (def->type == Match::CardType::Minion)
                {
                    ++p.minions;
                    if (!def->race.empty())
                        ++p.races[Lower(def->race)];
                }
                p.texts.push_back(Lower(def->text));
            }
            return p;
        }

        // ---- curve and type need -----------------------------------------------------------------------
        // behind = (target share that fits the picks made so far) - (cards of the bucket drafted); positive
        // adds value, negative (bucket full) takes it away.
        constexpr uint32_t kMinionTarget = 20; // minions in 30 cards; fewer and the AI has nothing to fight with
        constexpr double kCurveWeight = 6.0;
        constexpr double kCurveMax = 14.0;
        constexpr double kCurveMin = -18.0;
        constexpr double kMinionWeight = 3.0;
        constexpr double kMinionMax = 8.0;
        constexpr double kMinionMin = -6.0;

        // ---- synergies -----------------------------------------------------------------------------------
        // A card "names" a race when its text contains it ("Murloc", "Beast", "Dragon", ...). Data driven, both ways:
        //  - a payoff naming race R gains per drafted minion of R, and loses a little when there is none;
        //  - a minion of race R gains per drafted card that names R.
        constexpr double kPayoffPerMinion = 2.0;
        constexpr double kPayoffNone = -3.0;
        constexpr int kPayoffCap = 5;
        constexpr double kBodyPerPayoff = 2.5;
        constexpr int kBodyCap = 3;

        double Synergy(Match::CardDef const& def, DeckProfile const& deck, std::vector<std::string> const& raceWords)
        {
            double bonus = 0;
            std::string const text = Lower(def.text);
            for (std::string const& race : raceWords)
            {
                if (text.find(race) == std::string::npos)
                    continue;
                auto it = deck.races.find(race);
                int const have = it == deck.races.end() ? 0 : it->second;
                bonus += have == 0 ? kPayoffNone : kPayoffPerMinion * std::min(have, kPayoffCap);
                break; // one race counts per card
            }
            if (def.type == Match::CardType::Minion && !def.race.empty())
            {
                std::string const race = Lower(def.race);
                int named = 0;
                for (std::string const& t : deck.texts)
                    if (t.find(race) != std::string::npos)
                        ++named;
                bonus += kBodyPerPayoff * std::min(named, kBodyCap);
            }
            return bonus;
        }
    }

    std::size_t ArenaCurveBucket(int cost)
    {
        return static_cast<std::size_t>(Clamp(cost - 1, 0, static_cast<int>(kArenaCurveBuckets) - 1));
    }

    std::array<uint32_t, kArenaCurveBuckets> const& ArenaCurveTarget()
    {
        // 0..1: 4, 2: 6, 3: 6, 4: 5, 5: 4, 6: 3, 7+: 2
        static std::array<uint32_t, kArenaCurveBuckets> const target = { { 4, 6, 6, 5, 4, 3, 2 } };
        return target;
    }

    double ArenaDraftScore(std::string const& deckClass, std::string const& id,
                           std::vector<std::string> const& picksSoFar, CardCatalog const& catalog,
                           Match::CardDb const& db)
    {
        Match::CardDef const* def = db.Find(id);
        CardRule const* rule = catalog.Find(id);
        if (!def || !rule)
            return -1000.0;
        DeckProfile const deck = ProfileOf(picksSoFar, db);

        double score = CardValue(deckClass, id, *def, rule->rarity);

        double const progress = static_cast<double>(picksSoFar.size() + 1) / kArenaDraftPicks;
        std::size_t const bucket = ArenaCurveBucket(def->cost);
        double const behind = ArenaCurveTarget()[bucket] * progress - deck.curve[bucket];
        score += Clamp(kCurveWeight * behind, kCurveMin, kCurveMax);
        if (def->type == Match::CardType::Minion)
        {
            double const minionBehind = kMinionTarget * progress - deck.minions;
            score += Clamp(kMinionWeight * minionBehind, kMinionMin, kMinionMax);
        }

        // The races worth testing against the text: those of the drafted minions plus the card's own.
        std::vector<std::string> words;
        for (auto const& kv : deck.races)
            words.push_back(kv.first);
        if (!def->race.empty() && deck.races.find(Lower(def->race)) == deck.races.end())
            words.push_back(Lower(def->race));
        std::sort(words.begin(), words.end());
        score += Synergy(*def, deck, words);
        return score;
    }

    std::string ArenaDraftChoose(std::string const& deckClass, std::vector<std::string> const& offer,
                                 std::vector<std::string> const& picksSoFar, CardCatalog const& catalog,
                                 Match::CardDb const& db, PackRng const& rng)
    {
        std::string best;
        double bestScore = 0;
        for (std::string const& id : offer)
        {
            // One draw per offered card, whatever the score, so the rng advances the same way for every offer.
            double const jitter = kArenaDraftJitter * static_cast<double>(rng(1000)) / 1000.0;
            double const score = ArenaDraftScore(deckClass, id, picksSoFar, catalog, db) + jitter;
            if (best.empty() || score > bestScore)
            {
                best = id;
                bestScore = score;
            }
        }
        return best;
    }

    std::vector<std::string> ArenaDraftDeck(std::string const& deckClass, ArenaPool const& pool,
                                            std::array<uint32_t, kPackRarityCount> const& weights,
                                            CardCatalog const& catalog, Match::CardDb const& db, PackRng const& rng)
    {
        std::vector<std::string> deck;
        while (deck.size() < kArenaDraftPicks)
        {
            std::vector<std::string> offer;
            if (!ArenaOffer(pool, weights, rng, offer))
                return {};
            deck.push_back(ArenaDraftChoose(deckClass, offer, deck, catalog, db, rng));
        }
        return deck;
    }

    std::string ArenaRandomClass(PackRng const& rng)
    {
        std::vector<std::string> const& classes = DeckClasses();
        return classes[rng(static_cast<uint32_t>(classes.size()))];
    }
}
