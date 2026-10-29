/*
 * Card definitions helpers, the keyword parser, the script table and its builder, the heroes and
 * the engine's own scripts (hero powers, The Coin, engine tokens). The cards themselves are in
 * HsMatchCardsBasic.cpp (CORE) and HsMatchCardsClassic.cpp (EXPERT1). See HsMatchInternal.h.
 */

#include "HsMatch.h"
#include "HsMatchInternal.h"

#include <cstddef>
#include <cstdlib>
#include <unordered_map>

namespace Hs
{
namespace Match
{
    namespace
    {
        std::string Upper(std::string s)
        {
            for (char& c : s)
                if (c >= 'a' && c <= 'z')
                    c = static_cast<char>(c - 'a' + 'A');
            return s;
        }

        std::string Lower(std::string s)
        {
            for (char& c : s)
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c - 'A' + 'a');
            return s;
        }

        std::string Trim(std::string const& s)
        {
            std::size_t b = 0;
            std::size_t e = s.size();
            auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
            while (b < e && isSpace(s[b]))
                ++b;
            while (e > b && isSpace(s[e - 1]))
                --e;
            return s.substr(b, e - b);
        }

        struct HeroRow
        {
            char const* cls;
            char const* hero;
            char const* power;
        };

        HeroRow const kHeroes[] = {
            {"WARRIOR", "HERO_01", "CS2_102"}, {"SHAMAN", "HERO_02", "CS2_049"}, {"ROGUE", "HERO_03", "CS2_083b"},
            {"PALADIN", "HERO_04", "CS2_101"}, {"HUNTER", "HERO_05", "DS1h_292"}, {"DRUID", "HERO_06", "CS2_017"},
            {"WARLOCK", "HERO_07", "CS2_056"}, {"MAGE", "HERO_08", "CS2_034"},   {"PRIEST", "HERO_09", "CS1h_001"},
        };

        // Strips <...> tags. Newlines and the separators . , ; end a token.
        std::vector<std::string> KeywordTokens(std::string const& text)
        {
            std::vector<std::string> tokens;
            std::string cur;
            bool inTag = false;
            for (char c : text)
            {
                if (c == '<')
                {
                    inTag = true;
                    continue;
                }
                if (c == '>')
                {
                    inTag = false;
                    continue;
                }
                if (inTag)
                    continue;
                if (c == '.' || c == ',' || c == ';' || c == '\n' || c == '\r')
                {
                    tokens.push_back(Trim(cur));
                    cur.clear();
                    continue;
                }
                cur.push_back(c);
            }
            tokens.push_back(Trim(cur));
            return tokens;
        }

        bool SmallNumber(std::string const& s)
        {
            if (s.empty() || s.size() > 3)
                return false;
            for (char c : s)
                if (c < '0' || c > '9')
                    return false;
            return true;
        }

        Detail::ScriptTable const& Table()
        {
            static Detail::ScriptTable const table = []() {
                Detail::ScriptTable t;
                Detail::RegisterEngineScripts(t);
                Detail::RegisterBasicScripts(t);
                Detail::RegisterClassicScripts(t);
                Detail::RegisterClassicRestScripts(t);
                Detail::RegisterClassicEngineScripts(t);
                Detail::RegisterNaxxScripts(t);
                t.Find(std::string()); // builds the index once, inside the thread-safe static init
                return t;
            }();
            return table;
        }
    }

    // ---- public helpers ---------------------------------------------------

    CardType ParseCardType(std::string const& cardType)
    {
        std::string n;
        for (char c : cardType)
            if (c != '_' && c != ' ' && c != '-')
                n.push_back(c);
        n = Upper(n);
        if (n == "MINION")
            return CardType::Minion;
        if (n == "SPELL")
            return CardType::Spell;
        if (n == "WEAPON")
            return CardType::Weapon;
        if (n == "HEROPOWER")
            return CardType::HeroPower;
        if (n == "HERO")
            return CardType::Hero;
        return CardType::Other;
    }

    void ApplyMechanics(std::string const& mechanicsCsv, CardDef& def)
    {
        def.taunt = def.charge = def.divineShield = def.windfury = false;
        def.stealth = def.poisonous = def.secret = def.cantAttack = def.elusive = false;
        def.battlecry = def.deathrattle = false;
        std::string cur;
        auto flush = [&]() {
            std::string const m = Upper(Trim(cur));
            cur.clear();
            if (m == "TAUNT")
                def.taunt = true;
            else if (m == "CHARGE")
                def.charge = true;
            else if (m == "DIVINE_SHIELD")
                def.divineShield = true;
            else if (m == "WINDFURY")
                def.windfury = true;
            else if (m == "STEALTH")
                def.stealth = true;
            else if (m == "POISONOUS")
                def.poisonous = true;
            else if (m == "SECRET")
                def.secret = true;
            else if (m == "CANT_ATTACK")
                def.cantAttack = true;
            else if (m == "CANT_BE_TARGETED_BY_SPELLS" || m == "CANT_BE_TARGETED_BY_HERO_POWERS")
                def.elusive = true;
            else if (m == "BATTLECRY")
                def.battlecry = true;
            else if (m == "DEATHRATTLE")
                def.deathrattle = true;
        };
        for (char c : mechanicsCsv)
        {
            if (c == ',')
                flush();
            else
                cur.push_back(c);
        }
        flush();
    }

    void CardDb::Add(CardDef const& def)
    {
        m_cards[def.id] = def;
    }

    CardDef const* CardDb::Find(std::string const& id) const
    {
        auto it = m_cards.find(id);
        return it == m_cards.end() ? nullptr : &it->second;
    }

    bool CardDb::Supported(std::string const& id) const
    {
        CardDef const* def = Find(id);
        if (!def)
            return false;
        if (Detail::Script const* sc = Detail::FindScript(id))
        {
            for (std::string const& ref : sc->refs)
                if (!Find(ref) || (ref != id && !Supported(ref)))
                    return false;
            return true;
        }
        if (def->type == CardType::Minion || def->type == CardType::Weapon)
        {
            Detail::Keywords k;
            return Detail::ParseKeywordText(def->text, k);
        }
        return false;
    }

    std::size_t CardDb::SupportedCount() const
    {
        std::size_t n = 0;
        for (auto const& entry : m_cards)
            if (Supported(entry.first))
                ++n;
        return n;
    }

    std::string HeroCardOf(std::string const& heroClass)
    {
        std::string const c = Upper(heroClass);
        for (HeroRow const& r : kHeroes)
            if (c == r.cls)
                return r.hero;
        return std::string();
    }

    std::string HeroPowerOf(std::string const& heroClass)
    {
        std::string const c = Upper(heroClass);
        for (HeroRow const& r : kHeroes)
            if (c == r.cls)
                return r.power;
        return std::string();
    }

    std::vector<std::string> EngineCardIds()
    {
        std::vector<std::string> ids;
        ids.push_back(Detail::kCoinId);
        ids.push_back(Detail::kSheepId);
        ids.push_back("CS2_101t");
        ids.push_back("CS2_082");
        for (std::string const& t : Detail::TotemIds())
            ids.push_back(t);
        for (HeroRow const& r : kHeroes)
        {
            ids.push_back(r.hero);
            ids.push_back(r.power);
        }
        return ids;
    }

    char const* ActionErrorCode(ActionError e)
    {
        switch (e)
        {
            case ActionError::None: return "";
            case ActionError::Over: return "over";
            case ActionError::Phase: return "phase";
            case ActionError::Turn: return "turn";
            case ActionError::Source: return "source";
            case ActionError::Mana: return "mana";
            case ActionError::Target: return "target";
            case ActionError::BoardFull: return "board";
            case ActionError::Attack: return "attack";
            case ActionError::Taunt: return "taunt";
            case ActionError::Used: return "used";
            case ActionError::Requirement: return "requirement";
            case ActionError::Mulligan: return "mulligan";
            case ActionError::Position: return "position";
            case ActionError::Unsupported: return "unsupported";
            case ActionError::Choice: return "choice";
            case ActionError::Pending: return "pending";
            case ActionError::Secret: return "secret";
        }
        return "";
    }

    char const* EventTypeCode(EventType t)
    {
        switch (t)
        {
            case EventType::Mulligan: return "mull";
            case EventType::TurnStart: return "turn";
            case EventType::Draw: return "draw";
            case EventType::Burn: return "burn";
            case EventType::Fatigue: return "fat";
            case EventType::Play: return "play";
            case EventType::Summon: return "sum";
            case EventType::Attack: return "atk";
            case EventType::Damage: return "dmg";
            case EventType::Heal: return "heal";
            case EventType::Armor: return "arm";
            case EventType::ShieldPop: return "pop";
            case EventType::Death: return "die";
            case EventType::Equip: return "eq";
            case EventType::WeaponBreak: return "wbrk";
            case EventType::Transform: return "tf";
            case EventType::Buff: return "buff";
            case EventType::HeroPower: return "hp";
            case EventType::ManaGain: return "mana";
            case EventType::GameOver: return "end";
            case EventType::Silence: return "sil";
            case EventType::Freeze: return "frz";
            case EventType::Secret: return "sec";
            case EventType::Reveal: return "rev";
            case EventType::Trigger: return "trig";
            case EventType::Deathrattle: return "dr";
            case EventType::Discard: return "disc";
            case EventType::Bounce: return "bnc";
            case EventType::Control: return "ctl";
            case EventType::Overload: return "ovl";
            case EventType::Generate: return "gen";
            case EventType::Destroy: return "dest";
            case EventType::Counter: return "cnt";
            case EventType::Pick: return "pick";
            case EventType::Crystal: return "crys";
            case EventType::Stealth: return "stl";
        }
        return "";
    }

    // ---- internals ----------------------------------------------------------

    namespace Detail
    {
        Script const* FindScript(std::string const& cardId)
        {
            return Table().Find(cardId);
        }

        bool ParseKeywordText(std::string const& text, Keywords& out)
        {
            out = Keywords();
            for (std::string const& tok : KeywordTokens(text))
            {
                if (tok.empty())
                    continue;
                std::string const l = Lower(tok);
                if (l == "taunt")
                    out.taunt = true;
                else if (l == "charge")
                    out.charge = true;
                else if (l == "divine shield")
                    out.divineShield = true;
                else if (l == "windfury")
                    out.windfury = true;
                else if (l == "stealth")
                    out.stealth = true;
                else if (l == "poisonous")
                    out.poisonous = true;
                else if (l == "can't attack")
                    out.cantAttack = true;
                else if (l.compare(0, 14, "spell damage +") == 0 && SmallNumber(l.substr(14)))
                    out.spellDamage = std::atoi(l.c_str() + 14);
                else if (l.compare(0, 11, "overload: (") == 0 && l.size() > 12 && l[l.size() - 1] == ')' &&
                         SmallNumber(l.substr(11, l.size() - 12)))
                    out.overload = std::atoi(l.c_str() + 11);
                else
                    return false;
            }
            return true;
        }

        Keywords KeywordsOf(CardDef const& def)
        {
            Keywords k;
            k.taunt = def.taunt;
            k.charge = def.charge;
            k.divineShield = def.divineShield;
            k.windfury = def.windfury;
            k.stealth = def.stealth;
            k.poisonous = def.poisonous;
            k.cantAttack = def.cantAttack;
            k.spellDamage = def.spellDamage;
            k.overload = def.overload;
            if (def.type == CardType::Minion || def.type == CardType::Weapon)
            {
                Keywords t;
                if (ParseKeywordText(def.text, t))
                {
                    k.taunt = k.taunt || t.taunt;
                    k.charge = k.charge || t.charge;
                    k.divineShield = k.divineShield || t.divineShield;
                    k.windfury = k.windfury || t.windfury;
                    k.stealth = k.stealth || t.stealth;
                    k.poisonous = k.poisonous || t.poisonous;
                    k.cantAttack = k.cantAttack || t.cantAttack;
                    if (t.spellDamage > k.spellDamage)
                        k.spellDamage = t.spellDamage;
                    if (t.overload > k.overload)
                        k.overload = t.overload;
                }
            }
            return k;
        }

        std::vector<std::string> const& TotemIds()
        {
            static std::vector<std::string> const ids = {"CS2_050", "CS2_051", "CS2_052", "NEW1_009"};
            return ids;
        }

        // ---- builder -----------------------------------------------------------

        std::vector<Step>& ScriptBuilder::Cur()
        {
            switch (m_list)
            {
                case 1: return m_s->combo;
                case 2: return m_s->deathrattle;
                case 3: return m_s->triggers.back().steps;
                default: return m_s->play;
            }
        }

        void ScriptBuilder::AddRef(std::string const& id)
        {
            if (id.empty())
                return;
            for (std::string const& r : m_s->refs)
                if (r == id)
                    return;
            m_s->refs.push_back(id);
        }

        static TargetRule MakeRule(TargetKind kind, unsigned flags, int value, char const* race)
        {
            TargetRule r;
            r.kind = kind;
            r.flags = flags;
            r.value = value;
            if (race)
                r.race = race;
            if (kind == TargetKind::EnemyDamagedMinion)
            {
                r.kind = TargetKind::EnemyMinion;
                r.flags |= kTgtDamaged;
            }
            return r;
        }

        ScriptBuilder& ScriptBuilder::Target(TargetKind kind, unsigned flags, int value, char const* race)
        {
            m_s->target = MakeRule(kind, flags, value, race);
            return *this;
        }

        ScriptBuilder& ScriptBuilder::ComboTarget(TargetKind kind, unsigned flags, int value, char const* race)
        {
            m_s->hasComboTarget = true;
            m_s->comboTarget = MakeRule(kind, flags, value, race);
            return *this;
        }

        ScriptBuilder& ScriptBuilder::Req(unsigned flag, int value)
        {
            m_s->reqs |= flag;
            if (flag == kReqMinEnemyMinions || flag == kReqMinTotalMinions)
                m_s->reqValue = value;
            return *this;
        }

        ScriptBuilder& ScriptBuilder::Do(Op op, Sel sel, int a, int b, unsigned flags)
        {
            Step st;
            st.op = op;
            st.sel = sel;
            st.a = a;
            st.b = b;
            st.flags = flags;
            st.cond = m_nextCond;
            st.condRace = m_nextCondRace;
            m_nextCond = Cond::None;
            m_nextCondRace.clear();
            Cur().push_back(st);
            return *this;
        }

        ScriptBuilder& ScriptBuilder::DoCard(Op op, Sel sel, char const* card, int a, int b, unsigned flags)
        {
            Do(op, sel, a, b, flags);
            Cur().back().card = card ? card : "";
            AddRef(Cur().back().card);
            return *this;
        }

        ScriptBuilder& ScriptBuilder::DoList(Op op, Sel sel, std::vector<std::string> const& list, int a, unsigned flags)
        {
            Do(op, sel, a, 0, flags);
            Cur().back().list = list;
            for (std::string const& id : list)
                AddRef(id);
            return *this;
        }

        ScriptBuilder& ScriptBuilder::Run(CustomFn fn, std::vector<std::string> const& refs)
        {
            Do(Op::Custom, Sel::None);
            Cur().back().custom = fn;
            for (std::string const& id : refs)
                AddRef(id);
            return *this;
        }

        ScriptBuilder& ScriptBuilder::Race(char const* race)
        {
            if (!Cur().empty() && race)
                Cur().back().race = race;
            return *this;
        }

        ScriptBuilder& ScriptBuilder::Times(int count)
        {
            if (!Cur().empty())
                Cur().back().count = count;
            return *this;
        }

        ScriptBuilder& ScriptBuilder::Flags(unsigned flags)
        {
            if (!Cur().empty())
                Cur().back().flags |= flags;
            return *this;
        }

        ScriptBuilder& ScriptBuilder::If(Cond cond, char const* race)
        {
            m_nextCond = cond;
            m_nextCondRace = race ? race : "";
            return *this;
        }

        ScriptBuilder& ScriptBuilder::Combo()
        {
            m_list = 1;
            return *this;
        }

        ScriptBuilder& ScriptBuilder::Deathrattle()
        {
            m_list = 2;
            return *this;
        }

        ScriptBuilder& ScriptBuilder::On(Detail::On on, Who who, unsigned flags, int value, char const* race)
        {
            TriggerSpec t;
            t.on = on;
            t.who = who;
            t.flags = flags;
            t.value = value;
            if (race)
                t.race = race;
            m_s->triggers.push_back(t);
            m_list = 3;
            return *this;
        }

        ScriptBuilder& ScriptBuilder::When(Cond cond, char const* race)
        {
            if (!m_s->triggers.empty())
            {
                m_s->triggers.back().cond = cond;
                m_s->triggers.back().condRace = race ? race : "";
            }
            return *this;
        }

        ScriptBuilder& ScriptBuilder::Aura(AuraKind kind, AuraScope scope, int value, char const* race)
        {
            AuraSpec a;
            a.kind = kind;
            a.scope = scope;
            a.value = value;
            if (race)
                a.race = race;
            m_s->auras.push_back(a);
            return *this;
        }

        ScriptBuilder& ScriptBuilder::Enrage(AuraKind kind, int value)
        {
            AuraSpec a;
            a.kind = kind;
            a.scope = (kind == AuraKind::WeaponAttack) ? AuraScope::OwnSide : AuraScope::Self;
            a.value = value;
            a.whileDamaged = true;
            m_s->auras.push_back(a);
            return *this;
        }

        ScriptBuilder& ScriptBuilder::AuraFloor(int floor)
        {
            if (!m_s->auras.empty())
                m_s->auras.back().costFloor = floor;
            return *this;
        }

        ScriptBuilder& ScriptBuilder::WhileWeapon()
        {
            if (!m_s->auras.empty())
                m_s->auras.back().whileWeapon = true;
            return *this;
        }

        ScriptBuilder& ScriptBuilder::FirstMinionOnly()
        {
            if (!m_s->auras.empty())
                m_s->auras.back().firstMinionOnly = true;
            return *this;
        }

        ScriptBuilder& ScriptBuilder::BattlecryOnly()
        {
            if (!m_s->auras.empty())
                m_s->auras.back().battlecryOnly = true;
            return *this;
        }

        ScriptBuilder& ScriptBuilder::DoublesDeathrattles()
        {
            m_s->doublesDeathrattles = true;
            return *this;
        }

        ScriptBuilder& ScriptBuilder::Passive()
        {
            m_s->passive = true;
            return *this;
        }

        ScriptBuilder& ScriptBuilder::AutoCast()
        {
            m_s->autoCast = true;
            return *this;
        }

        ScriptBuilder& ScriptBuilder::PermanentlyFrozen()
        {
            m_s->permFrozen = true;
            return *this;
        }

        ScriptBuilder& ScriptBuilder::PhaseTwo(char const* powerId, char const* summonId, int summons, int forcedTurn)
        {
            m_s->phase.present = true;
            m_s->phase.powerId = powerId;
            m_s->phase.summonId = summonId;
            m_s->phase.summons = summons;
            m_s->phase.forcedTurn = forcedTurn;
            AddRef(powerId);
            AddRef(summonId);
            return *this;
        }

        ScriptBuilder& ScriptBuilder::ChooseOne(char const* option1, char const* option2)
        {
            m_s->options[0] = option1;
            m_s->options[1] = option2;
            AddRef(option1);
            AddRef(option2);
            return *this;
        }

        ScriptBuilder& ScriptBuilder::Cost(CostRule rule, int value)
        {
            m_s->costRule = rule;
            m_s->costValue = value;
            return *this;
        }

        ScriptBuilder& ScriptBuilder::Ref(char const* card)
        {
            AddRef(card ? card : "");
            return *this;
        }

        ScriptBuilder ScriptTable::Add(char const* id)
        {
            for (std::size_t i = 0; i < m_scripts.size(); ++i)
                if (m_scripts[i]->id == id)
                {
                    *m_scripts[i] = Script();
                    m_scripts[i]->id = id;
                    return ScriptBuilder(*m_scripts[i]);
                }
            m_scripts.emplace_back(new Script());
            m_scripts.back()->id = id;
            return ScriptBuilder(*m_scripts.back());
        }

        Script const* ScriptTable::Find(std::string const& id) const
        {
            // The table is small (a few hundred); an index is built lazily on first use.
            if (m_index.size() != m_scripts.size())
            {
                m_index.clear();
                for (std::size_t i = 0; i < m_scripts.size(); ++i)
                    m_index[m_scripts[i]->id] = m_scripts[i].get();
            }
            auto it = m_index.find(id);
            return it == m_index.end() ? nullptr : it->second;
        }

        std::vector<std::string> ScriptTable::Ids() const
        {
            std::vector<std::string> ids;
            for (auto const& s : m_scripts)
                ids.push_back(s->id);
            return ids;
        }

        // ---- the engine's own scripts: hero powers, The Coin, engine tokens --------

        void RegisterEngineScripts(ScriptTable& t)
        {
            // The Coin: +1 mana this turn.
            t.Add(kCoinId).Do(Op::GainMana, Sel::None, 1);
            // Excess Mana (Wild Growth at 10 crystals): draw a card.
            t.Add(kExcessManaId).Do(Op::Draw, Sel::None, 1);
            // Nozdormu: the 15 second turn timer is a rule of the service, not an effect
            // (Match::TurnTimeLimitSec); the empty script makes the card Supported.
            t.Add("EX1_560");
            // Healing Totem: at the end of your turn, restore 1 Health to all friendly minions.
            t.Add("NEW1_009").On(On::TurnEnd, Who::Friendly).Do(Op::Heal, Sel::FriendlyMinions, 1);

            // Hero powers (cost 2, once per turn).
            // Fireblast: 1 damage.
            t.Add("CS2_034").Target(TargetKind::AnyCharacter).Do(Op::Damage, Sel::Target, 1);
            // Armor Up!: 2 armor.
            t.Add("CS2_102").Do(Op::Armor, Sel::OwnHero, 2);
            // Lesser Heal: restore 2 health.
            t.Add("CS1h_001").Target(TargetKind::AnyCharacter).Do(Op::Heal, Sel::Target, 2);
            // Steady Shot: 2 damage to the enemy hero.
            t.Add("DS1h_292").Do(Op::Damage, Sel::EnemyHero, 2);
            // Life Tap: draw a card and take 2 damage.
            t.Add("CS2_056").Do(Op::Draw, Sel::None, 1).Do(Op::Damage, Sel::OwnHero, 2);
            // Reinforce: summon a 1/1 Silver Hand Recruit.
            t.Add("CS2_101").Req(kReqFreeSlot).DoCard(Op::Summon, Sel::None, "CS2_101t");
            // Dagger Mastery: equip a 1/2 dagger.
            t.Add("CS2_083b").DoCard(Op::Equip, Sel::None, "CS2_082");
            // Shapeshift: +1 attack this turn, +1 armor.
            t.Add("CS2_017").Do(Op::BuffTemp, Sel::OwnHero, 1).Do(Op::Armor, Sel::OwnHero, 1);
            // Totemic Call: summon a random basic totem not already on the board.
            t.Add("CS2_049")
                .Req(kReqFreeSlot)
                .Req(kReqMissingTotem)
                .DoList(Op::SummonRandomList, Sel::None, TotemIds(), 1, kStepUnique);
        }
    }
}
}
