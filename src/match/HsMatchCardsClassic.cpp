/*
 * Scripted Classic (EXPERT1) cards of the 5834 snapshot: the representative list
 * with every token, option and generated card it refers to. See
 * HsMatchInternal.h for the builder and the primitives. Keyword-only minions and weapons (Stealth, Divine
 * Shield, Charge, Taunt, Windfury, Spell Damage, Overload, Can't Attack ...) need no script: the keyword
 * parser accepts them; they are listed in the comments where they belong to the list.
 *
 * Rules of thumb used throughout (5834):
 *  - "$N" in the card text: Spell Damage applies (spells and secrets) = kStepSpellPower; "#N": healing, never
 *    Spell Damage. Prophet Velen doubles both for spells and the hero power (the engine does it in the steps; the
 *    Custom handlers below call fx.DamageMultiplier()).
 *  - Battlecry targets are optional (kTgtOptional) when the card can be played without one.
 *  - Minions that carry a keyword in their text only through the mechanics column (Patient Assassin, Emperor Cobra
 *    ...) get an empty script: the script makes the card "Supported", the keywords come from the card definition.
 */

#include "HsMatchInternal.h"

#include <string>
#include <vector>

namespace Hs
{
namespace Match
{
namespace Detail
{
    namespace
    {
        Step OneStep(Op op, Sel sel, int a = 0, int b = 0)
        {
            Step st;
            st.op = op;
            st.sel = sel;
            st.a = a;
            st.b = b;
            return st;
        }

        // ---- Custom handlers ------------------------------------------------------------------

        // Edwin VanCleef: Combo: gain +2/+2 for each card played earlier this turn (the card itself is counted
        // in CardsPlayedThisTurn already).
        void EdwinCombo(Effects& fx)
        {
            int const n = fx.CardsPlayedThisTurn(fx.Side()) - 1;
            if (n > 0)
                fx.Buff(fx.Source(), 2 * n, 2 * n);
        }

        // Brawl: destroy all minions except one, chosen randomly (needs two or more minions).
        void BrawlFx(Effects& fx)
        {
            std::vector<EntityId> const all = fx.Select(Sel::AllMinions);
            if (all.size() < 2)
                return;
            std::size_t const keep = static_cast<std::size_t>(fx.Rand(static_cast<int>(all.size())));
            for (std::size_t i = 0; i < all.size(); ++i)
                if (i != keep)
                    fx.Destroy(all[i]);
        }

        // Ysera Awakens (Dream card): 5 damage to all characters except Ysera (Spell Damage counts).
        void YseraAwakensFx(Effects& fx)
        {
            int const dmg = (5 + fx.SpellPower()) * fx.DamageMultiplier();
            for (EntityId id : fx.Select(Sel::AllCharacters))
                if (fx.CardOf(id) != "EX1_572")
                    fx.Damage(id, dmg);
        }

        // Shadowform: Mind Spike (2 damage); when already in Shadowform Mind Shatter (3 damage).
        void ShadowformFx(Effects& fx)
        {
            std::string const cur = fx.HeroPowerOf(fx.Side());
            Step st = OneStep(Op::ReplaceHeroPower, Sel::None);
            st.card = (cur == "EX1_625t" || cur == "EX1_625t2") ? "EX1_625t2" : "EX1_625t";
            fx.Run(st);
        }

        // Mind Control Tech: Battlecry: if the opponent has 4 or more minions, take control of one at random.
        void MindControlTechFx(Effects& fx)
        {
            if (fx.Select(Sel::EnemyMinions).size() >= 4)
                fx.Run(OneStep(Op::TakeControlRandom, Sel::None, 1));
        }

        // The Faceless Manipulator: Battlecry: become a copy of the chosen minion (no target: stays itself).
        void FacelessFx(Effects& fx)
        {
            if (fx.Target() != 0 && fx.IsMinion(fx.Target()))
                fx.CopyMinion(fx.Target(), fx.Source());
        }

        // Lightwell: at the start of your turn, restore 3 Health to a damaged friendly character (random).
        void LightwellFx(Effects& fx)
        {
            std::vector<EntityId> pool;
            for (EntityId id : fx.Select(Sel::FriendlyCharacters))
                if (fx.Health(id) < fx.MaxHealth(id))
                    pool.push_back(id);
            if (pool.empty())
                return;
            fx.Heal(pool[static_cast<std::size_t>(fx.Rand(static_cast<int>(pool.size())))], 3);
        }

        // Battle Rage: draw a card for each damaged friendly character.
        void BattleRageFx(Effects& fx)
        {
            int n = 0;
            for (EntityId id : fx.Select(Sel::FriendlyCharacters))
                if (fx.Health(id) < fx.MaxHealth(id))
                    ++n;
            if (n > 0)
                fx.Draw(fx.Side(), n);
        }

        // Mortal Strike: 4 damage; 6 instead while the own hero has 12 or less Health.
        void MortalStrikeFx(Effects& fx)
        {
            if (fx.Target() == 0)
                return;
            int const base = fx.HeroHealth(fx.Side()) <= 12 ? 6 : 4;
            fx.Damage(fx.Target(), (base + fx.SpellPower()) * fx.DamageMultiplier());
        }

        // Shield Slam: 1 damage to a minion for each Armor of the own hero (the text has no $: no Spell Damage).
        void ShieldSlamFx(Effects& fx)
        {
            if (fx.Target() != 0)
                fx.Damage(fx.Target(), fx.Armor(fx.Side()));
        }

        // Shadowflame: destroy a friendly minion and deal its Attack to all enemy minions (no $: no Spell Damage).
        void ShadowflameFx(Effects& fx)
        {
            EntityId const t = fx.Target();
            if (t == 0)
                return;
            int const attack = fx.Attack(t);
            fx.Destroy(t);
            for (EntityId id : fx.Select(Sel::EnemyMinions))
                fx.Damage(id, attack);
        }

        // Sense Demons: put 2 random Demons from your deck into your hand; each missing one is a Worthless Imp.
        void SenseDemonsFx(Effects& fx)
        {
            int const side = fx.Side();
            std::vector<std::string> demons;
            for (std::string const& id : fx.DeckCards(side))
            {
                CardDef const* def = fx.CardInfo(id);
                if (def && def->type == CardType::Minion && def->race == "DEMON")
                    demons.push_back(id);
            }
            for (int k = 0; k < 2; ++k)
            {
                if (demons.empty())
                {
                    Step st = OneStep(Op::AddToHand, Sel::OwnHero, 1);
                    st.card = "EX1_317t";
                    fx.Run(st);
                    continue;
                }
                std::size_t const i = static_cast<std::size_t>(fx.Rand(static_cast<int>(demons.size())));
                fx.TakeFromDeck(side, demons[i]);
                demons.erase(demons.begin() + static_cast<std::ptrdiff_t>(i));
            }
        }

        // Bane of Doom: summon a random Demon (the collectible Warlock Demons except Lord Jaraxxus; summoned, so
        // no battlecry runs). Only the Demons scripted here are listed as references: an unscripted Demon is a
        // plain body, which is fine for a summoned minion.
        void SummonRandomDemonFx(Effects& fx)
        {
            static char const* const demons[] = {"CS2_059", "CS2_064", "CS2_065", "EX1_301", "EX1_304",
                "EX1_306", "EX1_310", "EX1_313", "EX1_319"};
            std::vector<std::string> pool;
            for (char const* id : demons)
                if (fx.CardInfo(id))
                    pool.push_back(id);
            if (pool.empty())
                return;
            fx.Summon(fx.Side(), pool[static_cast<std::size_t>(fx.Rand(static_cast<int>(pool.size())))], -1);
        }

        // Bloodsail Raider: Battlecry: gain Attack equal to the Attack of your weapon.
        void BloodsailRaiderFx(Effects& fx)
        {
            int const a = fx.WeaponAttack(fx.Side());
            if (a > 0)
                fx.Buff(fx.Source(), a, 0);
        }
    }

    void RegisterClassicScripts(ScriptTable& t)
    {
        // ================= Neutral: Deathrattle =================
        // Leper Gnome: Deathrattle: 2 damage to the enemy hero.
        t.Add("EX1_029").Deathrattle().Do(Op::Damage, Sel::EnemyHero, 2);
        // Harvest Golem: Deathrattle: summon a 2/1 Damaged Golem.
        t.Add("EX1_556").Deathrattle().DoCard(Op::Summon, Sel::None, "skele21");
        // Loot Hoarder: Deathrattle: draw a card.
        t.Add("EX1_096").Deathrattle().Do(Op::Draw, Sel::None, 1);
        // Abomination: Taunt. Deathrattle: 2 damage to ALL characters.
        t.Add("EX1_097").Deathrattle().Do(Op::Damage, Sel::AllCharacters, 2);
        // Cairne Bloodhoof: Deathrattle: summon a 4/5 Baine Bloodhoof.
        t.Add("EX1_110").Deathrattle().DoCard(Op::Summon, Sel::None, "EX1_110t");
        // Sylvanas Windrunner: Deathrattle: take control of a random enemy minion.
        t.Add("EX1_016").Deathrattle().Do(Op::TakeControlRandom, Sel::None, 1);
        // Savannah Highmane: Deathrattle: summon two 2/2 Hyenas.
        t.Add("EX1_534").Deathrattle().DoCard(Op::Summon, Sel::None, "EX1_534t", 2);
        // Bloodmage Thalnos: Spell Damage +1 (keyword). Deathrattle: draw a card.
        t.Add("EX1_012").Deathrattle().Do(Op::Draw, Sel::None, 1);

        // ================= Secrets =================
        // Counterspell: Secret: when your opponent casts a spell, counter it.
        t.Add("EX1_287").On(On::SpellCast, Who::Enemy).Do(Op::Counter, Sel::None);
        // Mirror Entity: Secret: when your opponent plays a minion, summon a copy of it.
        t.Add("EX1_294")
            .On(On::Played, Who::Enemy, kTrigMinion)
            .When(Cond::FreeSlot)
            .Do(Op::SummonCopy, Sel::Subject);
        // Ice Barrier: Secret: as soon as your hero is attacked, gain 8 Armor.
        t.Add("EX1_289").On(On::Attacked, Who::OwnHero).Do(Op::Armor, Sel::OwnHero, 8);
        // Ice Block: Secret: when your hero takes fatal damage, prevent it and become Immune this turn.
        t.Add("EX1_295")
            .On(On::FatalDamage, Who::OwnHero)
            .Do(Op::PreventDamage, Sel::None)
            .Do(Op::KeywordTemp, Sel::OwnHero, kKwImmune);
        // Vaporize: Secret: when a minion attacks your hero, destroy it.
        t.Add("EX1_594").On(On::Attacked, Who::OwnHero, kTrigMinion).Do(Op::Destroy, Sel::Subject);
        // Explosive Trap: Secret: when your hero is attacked, deal $2 damage to all enemies.
        t.Add("EX1_610").On(On::Attacked, Who::OwnHero).Do(Op::Damage, Sel::EnemyCharacters, 2, 0, kStepSpellPower);
        // Freezing Trap: Secret: when an enemy minion attacks, return it to its owner's hand; it costs (2) more.
        t.Add("EX1_611").On(On::Attacked, Who::Friendly, kTrigMinion).Do(Op::Bounce, Sel::Subject, 0, 2);
        // Snake Trap: Secret: when one of your minions is attacked, summon three 1/1 Snakes.
        t.Add("EX1_554")
            .On(On::Attacked, Who::Friendly, kTrigDefenderMinion)
            .When(Cond::FreeSlot)
            .DoCard(Op::Summon, Sel::None, "EX1_554t", 3);
        // Noble Sacrifice: Secret: when an enemy attacks, summon a 2/1 Defender as the new target.
        t.Add("EX1_130")
            .On(On::Attacked, Who::Friendly)
            .When(Cond::FreeSlot)
            .DoCard(Op::Summon, Sel::None, "EX1_130a")
            .Do(Op::RedirectAttack, Sel::LastSummoned);
        // Eye for an Eye: Secret: when your hero takes damage, deal that much damage to the enemy hero.
        t.Add("EX1_132").On(On::Damaged, Who::OwnHero).Do(Op::Damage, Sel::EnemyHero, 0, 0, kStepTriggerAmount);
        // Redemption: Secret: when one of your minions dies, return it to life with 1 Health.
        t.Add("EX1_136")
            .On(On::Died, Who::Friendly, kTrigMinion)
            .When(Cond::FreeSlot)
            .Do(Op::Resummon, Sel::Subject, 1);
        // Repentance: Secret: when your opponent plays a minion, reduce its Health to 1.
        t.Add("EX1_379").On(On::Played, Who::Enemy, kTrigMinion).Do(Op::SetHealth, Sel::Subject, 1);
        // Misdirection: Secret: when a character attacks your hero, instead it attacks another random character.
        t.Add("EX1_533").On(On::Attacked, Who::OwnHero).Do(Op::RedirectAttack, Sel::RandomOtherCharacterForAttack);
        // Snipe: Secret: when your opponent plays a minion, deal $4 damage to it.
        t.Add("EX1_609").On(On::Played, Who::Enemy, kTrigMinion).Do(Op::Damage, Sel::Subject, 4, 0, kStepSpellPower);
        // Spellbender: Secret: when an enemy casts a spell on a minion, summon a 1/3 as the new target (needs room).
        t.Add("tt_010")
            .On(On::SpellCast, Who::Enemy)
            .When(Cond::TargetMinionFreeSlot)
            .DoCard(Op::Summon, Sel::None, "tt_010a")
            .Do(Op::RedirectSpell, Sel::LastSummoned);
        t.Add("tt_010a"); // the 1/3 Spellbender minion (its text is empty markup)

        // ================= Enrage =================
        // Amani Berserker / Tauren Warrior (Taunt keyword) / Angry Chicken / Grommash Hellscream (Charge keyword).
        t.Add("EX1_393").Enrage(AuraKind::Attack, 3);
        t.Add("EX1_390").Enrage(AuraKind::Attack, 3);
        t.Add("EX1_009").Enrage(AuraKind::Attack, 5);
        t.Add("EX1_414").Enrage(AuraKind::Attack, 6);
        // Raging Worgen: Enrage: Windfury and +1 Attack.
        t.Add("EX1_412").Enrage(AuraKind::Attack, 1).Enrage(AuraKind::Keyword, kKwWindfury);
        // Spiteful Smith: Enrage: your weapon has +2 Attack.
        t.Add("CS2_221").Enrage(AuraKind::WeaponAttack, 2);
        // Frothing Berserker: whenever a minion takes damage, gain +1 Attack.
        t.Add("EX1_604").On(On::Damaged, Who::Any, kTrigMinion).Do(Op::Buff, Sel::Self, 1, 0);

        // ================= Silence =================
        // Ironbeak Owl / Spellbreaker: Battlecry: silence a minion.
        t.Add("CS2_203").Target(TargetKind::AnyMinion, kTgtOptional).Do(Op::Silence, Sel::Target);
        t.Add("EX1_048").Target(TargetKind::AnyMinion, kTgtOptional).Do(Op::Silence, Sel::Target);
        // Silence: silence a minion.
        t.Add("EX1_332").Target(TargetKind::AnyMinion).Do(Op::Silence, Sel::Target);
        // Mass Dispel: silence all enemy minions. Draw a card.
        t.Add("EX1_626").Do(Op::Silence, Sel::EnemyMinions).Do(Op::Draw, Sel::None, 1);
        // Earth Shock: silence a minion, then deal $1 damage to it.
        t.Add("EX1_245")
            .Target(TargetKind::AnyMinion)
            .Do(Op::Silence, Sel::Target)
            .Do(Op::Damage, Sel::Target, 1, 0, kStepSpellPower);

        // ================= Stealth =================
        // Worgen Infiltrator, Jungle Panther, Stranglethorn Tiger: keyword only.
        // Master of Disguise: Battlecry: give a friendly minion Stealth (permanent: that is the 5834 text).
        t.Add("NEW1_014").Target(TargetKind::FriendlyMinion, kTgtOptional).Do(Op::Keyword, Sel::Target, kKwStealth);
        // Conceal: give your minions Stealth until your next turn.
        t.Add("EX1_128").Do(Op::KeywordTemp, Sel::FriendlyMinions, kKwStealth, 0, kStepUntilNextTurn);
        // Patient Assassin: Stealth, Poisonous (both from the card definition; the text is not keyword-only).
        t.Add("EX1_522");
        // Emperor Cobra: Poisonous (from the definition).
        t.Add("EX1_170");
        // Blood Imp: Stealth. At the end of your turn, give another random friendly minion +1 Health.
        t.Add("CS2_059").On(On::TurnEnd, Who::Friendly).Do(Op::Buff, Sel::RandomOtherFriendlyMinion, 0, 1);
        // Young Priestess: at the end of your turn, give another random friendly minion +1 Health.
        t.Add("EX1_004").On(On::TurnEnd, Who::Friendly).Do(Op::Buff, Sel::RandomOtherFriendlyMinion, 0, 1);

        // ================= Freeze =================
        // Cone of Cold: freeze a minion and the minions next to it, and deal $1 damage to them.
        t.Add("EX1_275")
            .Target(TargetKind::AnyMinion)
            .Do(Op::Freeze, Sel::Target)
            .Do(Op::Freeze, Sel::AdjacentToTarget)
            .Do(Op::Damage, Sel::Target, 1, 0, kStepSpellPower)
            .Do(Op::Damage, Sel::AdjacentToTarget, 1, 0, kStepSpellPower);
        // Blizzard: deal $2 damage to all enemy minions and freeze them.
        t.Add("CS2_028")
            .Do(Op::Damage, Sel::EnemyMinions, 2, 0, kStepSpellPower)
            .Do(Op::Freeze, Sel::EnemyMinions);
        // Frost Elemental: Battlecry: freeze a character.
        t.Add("EX1_283").Target(TargetKind::AnyCharacter, kTgtOptional).Do(Op::Freeze, Sel::Target);
        // Ice Lance: freeze a character. If it was already frozen, deal $4 damage instead.
        t.Add("CS2_031")
            .Target(TargetKind::AnyCharacter)
            .If(Cond::TargetFrozen)
            .Do(Op::Damage, Sel::Target, 4, 0, kStepSpellPower)
            .If(Cond::TargetNotFrozen)
            .Do(Op::Freeze, Sel::Target);

        // ================= Rogue: Combo =================
        // Eviscerate: deal $2 damage. Combo: $4 instead.
        t.Add("EX1_124")
            .Target(TargetKind::AnyCharacter)
            .Do(Op::Damage, Sel::Target, 2, 0, kStepSpellPower)
            .Combo()
            .Do(Op::Damage, Sel::Target, 4, 0, kStepSpellPower);
        // SI:7 Agent: Combo: deal 2 damage.
        t.Add("EX1_134").ComboTarget(TargetKind::AnyCharacter, kTgtOptional).Combo().Do(Op::Damage, Sel::Target, 2);
        // Defias Ringleader: Combo: summon a 2/1 Defias Bandit.
        t.Add("EX1_131").Combo().DoCard(Op::Summon, Sel::None, "EX1_131t");
        // Kidnapper: Combo: return a minion to its owner's hand.
        t.Add("NEW1_005").ComboTarget(TargetKind::AnyMinion, kTgtOptional).Combo().Do(Op::Bounce, Sel::Target);
        // Perdition's Blade: Battlecry: deal 1 damage. Combo: 2 instead.
        t.Add("EX1_133")
            .Target(TargetKind::AnyCharacter)
            .Do(Op::Damage, Sel::Target, 1)
            .Combo()
            .Do(Op::Damage, Sel::Target, 2);
        // Cold Blood: give a minion +2 Attack. Combo: +4 instead.
        t.Add("CS2_073")
            .Target(TargetKind::AnyMinion)
            .Do(Op::Buff, Sel::Target, 2, 0)
            .Combo()
            .Do(Op::Buff, Sel::Target, 4, 0);
        // Edwin VanCleef: Combo: gain +2/+2 for each card played earlier this turn.
        t.Add("EX1_613").Combo().Run(EdwinCombo);
        // Preparation: the next spell you cast this turn costs (3) less.
        t.Add("EX1_145").Do(Op::DiscountNextSpell, Sel::None, 3);

        // ================= Shaman: Overload (the amount comes from the card definition) =================
        // Lightning Bolt: deal $3 damage. Overload: (1)
        t.Add("EX1_238").Target(TargetKind::AnyCharacter).Do(Op::Damage, Sel::Target, 3, 0, kStepSpellPower);
        // Feral Spirit: summon two 2/3 Spirit Wolves with Taunt. Overload: (2)
        t.Add("EX1_248").Req(kReqFreeSlot).DoCard(Op::Summon, Sel::None, "EX1_tk11", 2);
        // Dust Devil, Earth Elemental, Doomhammer: keyword only.
        // Unbound Elemental: whenever you play a card with Overload, gain +1/+1.
        t.Add("EX1_258").On(On::Played, Who::Friendly, kTrigOverload).Do(Op::Buff, Sel::Self, 1, 1);
        // Lava Burst: deal $5 damage. Overload: (2)
        t.Add("EX1_241").Target(TargetKind::AnyCharacter).Do(Op::Damage, Sel::Target, 5, 0, kStepSpellPower);
        // Forked Lightning: deal $2 damage to 2 random enemy minions (needs two). Overload: (2)
        t.Add("EX1_251")
            .Req(kReqMinEnemyMinions, 2)
            .Do(Op::Damage, Sel::RandomEnemyMinion, 2, 0, kStepSpellPower)
            .Times(2);
        // Lightning Storm: deal $2-$3 damage to all enemy minions (rolled per minion). Overload: (2)
        t.Add("EX1_259").Do(Op::Damage, Sel::EnemyMinions, 2, 1, kStepSpellPower | kStepRandomBonus);

        // ================= Druid: Choose One (option order = card text order) =================
        // Wrath: $3 damage to a minion; or $1 damage and draw a card.
        t.Add("EX1_154").ChooseOne("EX1_154a", "EX1_154b");
        t.Add("EX1_154a").Target(TargetKind::AnyMinion).Do(Op::Damage, Sel::Target, 3, 0, kStepSpellPower);
        t.Add("EX1_154b")
            .Target(TargetKind::AnyMinion)
            .Do(Op::Damage, Sel::Target, 1, 0, kStepSpellPower)
            .Do(Op::Draw, Sel::None, 1);
        // Starfall: $5 damage to a minion; or $2 damage to all enemy minions.
        t.Add("NEW1_007").ChooseOne("NEW1_007b", "NEW1_007a");
        t.Add("NEW1_007b").Target(TargetKind::AnyMinion).Do(Op::Damage, Sel::Target, 5, 0, kStepSpellPower);
        t.Add("NEW1_007a").Do(Op::Damage, Sel::EnemyMinions, 2, 0, kStepSpellPower);
        // Nourish: gain 2 Mana Crystals (full); or draw 3 cards.
        t.Add("EX1_164").ChooseOne("EX1_164a", "EX1_164b");
        t.Add("EX1_164a").Do(Op::GainCrystals, Sel::None, 2, 1);
        t.Add("EX1_164b").Do(Op::Draw, Sel::None, 3);
        // Keeper of the Grove: deal 2 damage; or silence a minion.
        t.Add("EX1_166").ChooseOne("EX1_166a", "EX1_166b");
        t.Add("EX1_166a").Target(TargetKind::AnyCharacter).Do(Op::Damage, Sel::Target, 2);
        t.Add("EX1_166b").Target(TargetKind::AnyMinion).Do(Op::Silence, Sel::Target);
        // Druid of the Claw: Charge; or +2 Health and Taunt (it transforms).
        t.Add("EX1_165").ChooseOne("EX1_165a", "EX1_165b");
        t.Add("EX1_165a").DoCard(Op::Transform, Sel::Self, "EX1_165t1");
        t.Add("EX1_165b").DoCard(Op::Transform, Sel::Self, "EX1_165t2");
        // Power of the Wild: give your minions +1/+1; or summon a 3/2 Panther.
        t.Add("EX1_160").ChooseOne("EX1_160b", "EX1_160a");
        t.Add("EX1_160b").Do(Op::Buff, Sel::FriendlyMinions, 1, 1);
        t.Add("EX1_160a").Req(kReqFreeSlot).DoCard(Op::Summon, Sel::None, "EX1_160t");
        // Mark of Nature: give a minion +4 Attack; or +4 Health and Taunt.
        t.Add("EX1_155").ChooseOne("EX1_155a", "EX1_155b");
        t.Add("EX1_155a").Target(TargetKind::AnyMinion).Do(Op::Buff, Sel::Target, 4, 0);
        t.Add("EX1_155b").Target(TargetKind::AnyMinion).Do(Op::Buff, Sel::Target, 0, 4).Do(Op::Keyword, Sel::Target, kKwTaunt);
        // Ancient of Lore: draw 2 cards; or restore 5 Health (a character).
        t.Add("NEW1_008").ChooseOne("NEW1_008a", "NEW1_008b");
        t.Add("NEW1_008a").Do(Op::Draw, Sel::None, 2);
        t.Add("NEW1_008b").Target(TargetKind::AnyCharacter).Do(Op::Heal, Sel::Target, 5);
        // Ancient of War: +5 Attack; or +5 Health and Taunt (the minion itself).
        t.Add("EX1_178").ChooseOne("EX1_178b", "EX1_178a");
        t.Add("EX1_178b").Do(Op::Buff, Sel::Self, 5, 0);
        t.Add("EX1_178a").Do(Op::Buff, Sel::Self, 0, 5).Do(Op::Keyword, Sel::Self, kKwTaunt);
        // Bite: give your hero +4 Attack this turn and 4 Armor.
        t.Add("EX1_570").Do(Op::BuffTemp, Sel::OwnHero, 4).Do(Op::Armor, Sel::OwnHero, 4);

        // ================= Weapons =================
        // Eaglehorn Bow: whenever a friendly Secret is revealed, gain +1 Durability. (Build 5834 had no "friendly"
        // in the text; from 6024 on only the owner's own secrets count: Who::Friendly.)
        t.Add("EX1_536").On(On::SecretRevealed, Who::Friendly).Do(Op::WeaponBuff, Sel::None, 0, 1);
        // Gorehowl: attacking a minion costs 1 Attack instead of 1 Durability.
        t.Add("EX1_411").Aura(AuraKind::AttackCostsAttack, AuraScope::OwnSide, 0);
        // Sword of Justice: whenever you summon a minion, give it +1/+1 and this loses 1 Durability.
        t.Add("EX1_366")
            .On(On::Summoned, Who::Friendly)
            .Do(Op::Buff, Sel::Subject, 1, 1)
            .Do(Op::WeaponBuff, Sel::None, 0, -1);
        // Arathi Weaponsmith: Battlecry: equip a 2/2 weapon.
        t.Add("EX1_398").DoCard(Op::Equip, Sel::None, "EX1_398t");
        // Gladiator's Longbow: your hero is Immune while attacking.
        t.Add("DS1_188").Aura(AuraKind::ImmuneWhileAttacking, AuraScope::OwnSide, 0);
        // Doomhammer (keyword only: Windfury, Overload: (2)).
        // Upgrade!: if you have a weapon give it +1/+1, otherwise equip a 1/3 weapon.
        t.Add("EX1_409")
            .If(Cond::HaveWeapon)
            .Do(Op::WeaponBuff, Sel::None, 1, 1)
            .If(Cond::NoWeapon)
            .DoCard(Op::Equip, Sel::None, "EX1_409t");
        // Bloodsail Corsair: Battlecry: remove 1 Durability from your opponent's weapon.
        t.Add("NEW1_025").Do(Op::WeaponBuff, Sel::EnemyHero, 0, -1);
        // Captain Greenskin: Battlecry: give your weapon +1/+1.
        t.Add("NEW1_024").Do(Op::WeaponBuff, Sel::None, 1, 1);
        // Bloodsail Raider: Battlecry: gain Attack equal to the Attack of your weapon.
        t.Add("NEW1_018").Run(BloodsailRaiderFx);
        // Southsea Deckhand: has Charge while you have a weapon equipped.
        t.Add("CS2_146").Aura(AuraKind::Keyword, AuraScope::Self, kKwCharge).WhileWeapon();

        // ================= Spell Damage, auras, cost =================
        // Azure Drake: Spell Damage +1 (keyword). Battlecry: draw a card. Malygos: keyword only.
        t.Add("EX1_284").Do(Op::Draw, Sel::None, 1);
        // Dire Wolf Alpha: adjacent minions have +1 Attack.
        t.Add("EX1_162").Aura(AuraKind::Attack, AuraScope::Adjacent, 1);
        // Murloc Warleader: ALL other Murlocs have +2/+1.
        t.Add("EX1_507").Aura(AuraKind::Attack, AuraScope::AllOtherMinions, 2, "MURLOC").Aura(AuraKind::Health, AuraScope::AllOtherMinions, 1, "MURLOC");
        // Southsea Captain: your other Pirates have +1/+1.
        t.Add("NEW1_027").Aura(AuraKind::AttackHealth, AuraScope::OtherFriendlyMinions, 1, "PIRATE");
        // Sorcerer's Apprentice: your spells cost (1) less.
        t.Add("EX1_608").Aura(AuraKind::Cost, AuraScope::OwnSpellCards, -1);
        // Venture Co. Mercenary: your minions cost (3) more.
        t.Add("CS2_227").Aura(AuraKind::Cost, AuraScope::OwnMinionCards, 3);
        // Summoning Portal: your minions cost (2) less, but not less than (1).
        t.Add("EX1_315").Aura(AuraKind::Cost, AuraScope::OwnMinionCards, -2).AuraFloor(1);
        // The Giants.
        t.Add("EX1_105").Cost(CostRule::MinusPerOtherHandCard, 1);
        t.Add("EX1_586").Cost(CostRule::MinusPerMinion, 1);
        t.Add("EX1_620").Cost(CostRule::MinusPerHeroDamage, 1);
        // Dread Corsair: Taunt. Costs (1) less per Attack of your weapon.
        t.Add("NEW1_022").Cost(CostRule::MinusWeaponAttack, 1);
        // Kirin Tor Mage: Battlecry: the next Secret you play this turn costs (0).
        t.Add("EX1_612").Do(Op::FreeNextSecret, Sel::None);
        // Millhouse Manastorm: Battlecry: enemy spells cost (0) next turn.
        t.Add("NEW1_029").Do(Op::EnemySpellsFreeNext, Sel::None);

        // ================= Triggered minions =================
        // Acolyte of Pain: whenever this minion takes damage, draw a card.
        t.Add("EX1_007").On(On::Damaged, Who::Self).Do(Op::Draw, Sel::None, 1);
        // Knife Juggler: after you summon a minion, deal 1 damage to a random enemy.
        t.Add("NEW1_019").On(On::Summoned, Who::OtherFriendly).Do(Op::Damage, Sel::RandomEnemyCharacter, 1);
        // Cult Master: whenever one of your other minions dies, draw a card.
        t.Add("EX1_595").On(On::Died, Who::OtherFriendly, kTrigMinion).Do(Op::Draw, Sel::None, 1);
        // Armorsmith: whenever a friendly minion takes damage, gain 1 Armor.
        t.Add("EX1_402").On(On::Damaged, Who::Friendly, kTrigMinion).Do(Op::Armor, Sel::OwnHero, 1);
        // Mana Wyrm: whenever you cast a spell, gain +1 Attack (on the cast, before the spell resolves).
        t.Add("NEW1_012").On(On::SpellCast, Who::Friendly).Do(Op::Buff, Sel::Self, 1, 0);
        // Questing Adventurer: whenever you play a card, gain +1/+1.
        t.Add("EX1_044").On(On::Played, Who::Friendly).Do(Op::Buff, Sel::Self, 1, 1);
        // Wild Pyromancer: after you cast a spell, deal 1 damage to ALL minions.
        t.Add("NEW1_020").On(On::AfterSpell, Who::Friendly).Do(Op::Damage, Sel::AllMinions, 1);
        // Imp Master: at the end of your turn, deal 1 damage to this minion and summon a 1/1 Imp.
        t.Add("EX1_597")
            .On(On::TurnEnd, Who::Friendly)
            .Do(Op::Damage, Sel::Self, 1)
            .DoCard(Op::Summon, Sel::None, "EX1_598");
        // Lightwarden: whenever a character is healed, gain +2 Attack.
        t.Add("EX1_001").On(On::Healed, Who::Any).Do(Op::Buff, Sel::Self, 2, 0);
        // Flesheating Ghoul: whenever a minion dies, gain +1 Attack.
        t.Add("tt_004").On(On::Died, Who::Any, kTrigMinion).Do(Op::Buff, Sel::Self, 1, 0);
        // Murloc Tidecaller: whenever a Murloc is summoned, gain +1 Attack (not for itself).
        t.Add("EX1_509").On(On::Summoned, Who::AnyOther, kTrigRace, 0, "MURLOC").Do(Op::Buff, Sel::Self, 1, 0);
        // Gadgetzan Auctioneer: whenever you cast a spell, draw a card.
        t.Add("EX1_095").On(On::SpellCast, Who::Friendly).Do(Op::Draw, Sel::None, 1);
        // Violet Teacher: whenever you cast a spell, summon a 1/1 Violet Apprentice.
        t.Add("NEW1_026").On(On::SpellCast, Who::Friendly).DoCard(Op::Summon, Sel::None, "NEW1_026t");
        // Ragnaros the Firelord: Can't Attack (keyword). At the end of your turn, deal 8 damage to a random enemy.
        t.Add("EX1_298").On(On::TurnEnd, Who::Friendly).Do(Op::Damage, Sel::RandomEnemyCharacter, 8);
        // Doomsayer: at the start of your turn, destroy ALL minions.
        t.Add("NEW1_021").On(On::TurnStart, Who::Friendly).Do(Op::Destroy, Sel::AllMinions);
        // Demolisher: at the start of your turn, deal 2 damage to a random enemy.
        t.Add("EX1_102").On(On::TurnStart, Who::Friendly).Do(Op::Damage, Sel::RandomEnemyCharacter, 2);
        // Alarm-o-Bot: at the start of your turn, swap this minion with a random one in your hand.
        t.Add("EX1_006").On(On::TurnStart, Who::Friendly).Do(Op::SwapWithHandMinion, Sel::Self);
        // Lightwell: at the start of your turn, restore 3 Health to a damaged friendly character.
        t.Add("EX1_341").On(On::TurnStart, Who::Friendly).Run(LightwellFx);
        // Lightspawn: this minion's Attack is always equal to its Health.
        t.Add("EX1_335").Aura(AuraKind::AttackEqualsHealth, AuraScope::Self, 0);
        // Ysera: at the end of your turn, draw a Dream Card.
        t.Add("EX1_572")
            .On(On::TurnEnd, Who::Friendly)
            .DoList(Op::AddRandomToHand, Sel::None, {"DREAM_01", "DREAM_02", "DREAM_03", "DREAM_04", "DREAM_05"}, 1);
        // The Dream cards: Laughing Sister (Can't be targeted by Spells or Hero Powers: from the definition),
        // Ysera Awakens, Emerald Drake (no text), Dream, Nightmare.
        t.Add("DREAM_01");
        t.Add("DREAM_02").Run(YseraAwakensFx);
        t.Add("DREAM_04").Target(TargetKind::AnyMinion).Do(Op::Bounce, Sel::Target);
        t.Add("DREAM_05")
            .Target(TargetKind::AnyMinion)
            .Do(Op::Buff, Sel::Target, 5, 5)
            .Do(Op::DelayedDestroy, Sel::Target, 0, 0);

        // ================= Priest =================
        // Auchenai Soulpriest: your cards and powers that restore Health now deal damage instead.
        t.Add("EX1_591").Aura(AuraKind::HealIsDamage, AuraScope::OwnSide, 1);
        // Prophet Velen: double the damage and healing of your spells and Hero Power.
        t.Add("EX1_350").Aura(AuraKind::DoubleSpellHeal, AuraScope::OwnSide, 1);
        // Mindgames: put a copy of a random minion from your opponent's deck into the battlefield.
        t.Add("EX1_345").Req(kReqFreeSlot).DoCard(Op::SummonFromEnemyDeck, Sel::None, "EX1_345t");
        t.Add("EX1_345t"); // Shadow of Nothing (flavor text only)
        // Inner Fire: change a minion's Attack to be equal to its Health.
        t.Add("CS1_129").Target(TargetKind::AnyMinion).Do(Op::AttackToHealth, Sel::Target);
        // Circle of Healing: restore #4 Health to ALL minions.
        t.Add("EX1_621").Do(Op::Heal, Sel::AllMinions, 4);
        // Holy Fire: deal $5 damage. Restore #5 Health to your hero.
        t.Add("EX1_624")
            .Target(TargetKind::AnyCharacter)
            .Do(Op::Damage, Sel::Target, 5, 0, kStepSpellPower)
            .Do(Op::Heal, Sel::OwnHero, 5);
        // Shadowform: your Hero Power becomes Mind Spike (2 damage); if already in Shadowform: Mind Shatter (3).
        t.Add("EX1_625").Run(ShadowformFx, {"EX1_625t", "EX1_625t2"});
        t.Add("EX1_625t").Target(TargetKind::AnyCharacter).Do(Op::Damage, Sel::Target, 2);
        t.Add("EX1_625t2").Target(TargetKind::AnyCharacter).Do(Op::Damage, Sel::Target, 3);

        // ================= Battlecry minions =================
        // Mad Bomber: Battlecry: deal 3 damage randomly split between all other characters.
        t.Add("EX1_082").Do(Op::Damage, Sel::RandomOtherCharacter, 1, 0, kStepRepeatRandom).Times(3);
        // Leeroy Jenkins: Charge (keyword). Battlecry: summon two 1/1 Whelps for your opponent.
        t.Add("EX1_116").DoCard(Op::Summon, Sel::None, "EX1_116t", 2, 1);
        // Arcane Golem: Charge (keyword). Battlecry: give your opponent a Mana Crystal (a full one).
        t.Add("EX1_089").Do(Op::GainCrystals, Sel::EnemyHero, 1, 1);
        // Youthful Brewmaster: Battlecry: return a friendly minion from the battlefield to your hand.
        t.Add("EX1_049").Target(TargetKind::FriendlyMinion, kTgtOptional).Do(Op::Bounce, Sel::Target);
        // The Faceless Manipulator: Battlecry: choose a minion and become a copy of it.
        t.Add("EX1_564").Target(TargetKind::AnyMinion, kTgtOptional).Run(FacelessFx);
        // Big Game Hunter: Battlecry: destroy a minion with an Attack of 7 or more.
        t.Add("EX1_005").Target(TargetKind::AnyMinion, kTgtOptional | kTgtMinAttack, 7).Do(Op::Destroy, Sel::Target);
        // Abusive Sergeant / Dark Iron Dwarf: Battlecry: give a minion +2 Attack this turn.
        t.Add("CS2_188").Target(TargetKind::AnyMinion, kTgtOptional).Do(Op::BuffTemp, Sel::Target, 2);
        t.Add("EX1_046").Target(TargetKind::AnyMinion, kTgtOptional).Do(Op::BuffTemp, Sel::Target, 2);
        // Argent Protector: Battlecry: give a friendly minion Divine Shield.
        t.Add("EX1_362").Target(TargetKind::FriendlyMinion, kTgtOptional).Do(Op::Keyword, Sel::Target, kKwDivineShield);
        // Earthen Ring Farseer: Battlecry: restore 3 Health.
        t.Add("CS2_117").Target(TargetKind::AnyCharacter, kTgtOptional).Do(Op::Heal, Sel::Target, 3);
        // Injured Blademaster: Battlecry: deal 4 damage to HIMSELF.
        t.Add("CS2_181").Do(Op::Damage, Sel::Self, 4);
        // Priestess of Elune: Battlecry: restore 4 Health to your hero.
        t.Add("EX1_583").Do(Op::Heal, Sel::OwnHero, 4);
        // Mind Control Tech: Battlecry: if your opponent has 4 or more minions, take control of one at random.
        t.Add("EX1_085").Run(MindControlTechFx);
        // King Mukla: Battlecry: give your opponent 2 Bananas.
        t.Add("EX1_014").DoCard(Op::AddToHand, Sel::EnemyHero, "EX1_014t", 2);
        // Bananas: give a minion +1/+1.
        t.Add("EX1_014t").Target(TargetKind::AnyMinion).Do(Op::Buff, Sel::Target, 1, 1);
        // Defender of Argus: Battlecry: give adjacent minions +1/+1 and Taunt.
        t.Add("EX1_093").Do(Op::Buff, Sel::AdjacentToSelf, 1, 1).Do(Op::Keyword, Sel::AdjacentToSelf, kKwTaunt);
        // Felguard: Taunt. Battlecry: destroy one of your Mana Crystals.
        t.Add("EX1_301").Do(Op::DestroyCrystals, Sel::None, 1);
        // Pit Lord: Battlecry: deal 5 damage to your hero. Flame Imp: 3 damage to your hero.
        t.Add("EX1_313").Do(Op::Damage, Sel::OwnHero, 5);
        t.Add("EX1_319").Do(Op::Damage, Sel::OwnHero, 3);
        // Doomguard: Charge (keyword). Battlecry: discard two random cards.
        t.Add("EX1_310").Do(Op::DiscardRandom, Sel::None, 2);
        // The Black Knight: Battlecry: destroy an enemy minion with Taunt.
        t.Add("EX1_002").Target(TargetKind::EnemyMinion, kTgtOptional | kTgtTaunt).Do(Op::Destroy, Sel::Target);
        // Cruel Taskmaster: Battlecry: deal 1 damage to a minion and give it +2 Attack.
        t.Add("EX1_603")
            .Target(TargetKind::AnyMinion, kTgtOptional)
            .Do(Op::Damage, Sel::Target, 1)
            .Do(Op::Buff, Sel::Target, 2, 0);
        // Worthless Imp (Sense Demons, flavor text only).
        t.Add("EX1_317t");

        // ================= Spells: Warrior, Warlock, hunter, neutral =================
        // Brawl: destroy all minions except one (chosen randomly). Needs two minions on the battlefield.
        t.Add("EX1_407").Req(kReqMinTotalMinions, 2).Run(BrawlFx);
        // Deadly Shot: destroy a random enemy minion.
        t.Add("EX1_617").Req(kReqMinEnemyMinions, 1).Do(Op::Destroy, Sel::RandomEnemyMinion);
        // Twisting Nether: destroy all minions.
        t.Add("EX1_312").Do(Op::Destroy, Sel::AllMinions);
        // Power Overwhelming: give a friendly minion +4/+4 until end of turn. Then, it dies.
        t.Add("EX1_316")
            .Target(TargetKind::FriendlyMinion)
            .Do(Op::Buff, Sel::Target, 4, 4)
            .Do(Op::DelayedDestroy, Sel::Target, 0, 1);
        // Sense Demons: put 2 random Demons from your deck into your hand.
        t.Add("EX1_317").Run(SenseDemonsFx, {"EX1_317t"});
        // Bane of Doom: deal $2 damage to a character. If that kills it, summon a random Demon.
        t.Add("EX1_320")
            .Target(TargetKind::AnyCharacter)
            .Do(Op::Damage, Sel::Target, 2, 0, kStepSpellPower)
            .If(Cond::TargetDead)
            .Run(SummonRandomDemonFx);
        // Shadowflame: destroy a friendly minion and deal its Attack damage to all enemy minions.
        t.Add("EX1_303").Target(TargetKind::FriendlyMinion).Run(ShadowflameFx);
        // Siphon Soul: destroy a minion. Restore #3 Health to your hero.
        t.Add("EX1_309").Target(TargetKind::AnyMinion).Do(Op::Destroy, Sel::Target).Do(Op::Heal, Sel::OwnHero, 3);
        // Slam: deal $2 damage to a minion. If it survives, draw a card.
        t.Add("EX1_391")
            .Target(TargetKind::AnyMinion)
            .Do(Op::Damage, Sel::Target, 2, 0, kStepSpellPower)
            .If(Cond::TargetAlive)
            .Do(Op::Draw, Sel::None, 1);
        // Battle Rage: draw a card for each damaged friendly character.
        t.Add("EX1_392").Run(BattleRageFx);
        // Mortal Strike: deal $4 damage. If you have 12 or less Health, deal $6 instead.
        t.Add("EX1_408").Target(TargetKind::AnyCharacter).Run(MortalStrikeFx);
        // Shield Slam: deal 1 damage to a minion for each Armor you have.
        t.Add("EX1_410").Target(TargetKind::AnyMinion).Run(ShieldSlamFx);
    }
}
}
}
