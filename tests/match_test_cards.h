// The card definitions of the match tests (tests/match_tests.cpp, tests/protocol_tests.cpp), built by hand
// with the real Hearthstone 1.0.0.5834 stats: both GM fixture decks, a few keyword minions, two unsupported
// cards, and every card the engine creates on its own (EngineCardIds).

#ifndef MOD_HEARTHSTONE_MATCH_TEST_CARDS_H
#define MOD_HEARTHSTONE_MATCH_TEST_CARDS_H

#include "../src/match/HsMatch.h"

#include <algorithm>
#include <string>
#include <vector>

namespace MatchTestCards
{
    using namespace Hs::Match;

    // ---- the card database (5834 stats) ---------------------------------------

    struct Row
    {
        char const* id;
        char const* name;
        char const* cls;
        char const* type;
        int cost;
        int attack;
        int health;
        int durability;
        int spellDamage;
        char const* mechanics;
        char const* text;
    };

    // clang-format off
    static Row const kRows[] = {
        // Mage fixture
        {"EX1_277", "Arcane Missiles", "MAGE", "SPELL", 1, 0, 0, 0, 0, "", "Deal $3 damage randomly split among enemy characters."},
        {"CS2_168", "Murloc Raider", "NEUTRAL", "MINION", 1, 2, 1, 0, 0, "", ""},
        {"CS2_025", "Arcane Explosion", "MAGE", "SPELL", 2, 0, 0, 0, 0, "", "Deal $1 damage to all enemy minions."},
        {"CS2_172", "Bloodfen Raptor", "NEUTRAL", "MINION", 2, 3, 2, 0, 0, "", ""},
        {"EX1_015", "Novice Engineer", "NEUTRAL", "MINION", 2, 1, 1, 0, 0, "BATTLECRY", "<b>Battlecry:</b> Draw a card."},
        {"CS2_120", "River Crocolisk", "NEUTRAL", "MINION", 2, 2, 3, 0, 0, "", ""},
        {"CS2_023", "Arcane Intellect", "MAGE", "SPELL", 3, 0, 0, 0, 0, "", "Draw 2 cards."},
        {"CS2_122", "Raid Leader", "NEUTRAL", "MINION", 3, 2, 2, 0, 0, "AURA", "Your other minions have +1 Attack."},
        {"CS2_124", "Wolfrider", "NEUTRAL", "MINION", 3, 3, 1, 0, 0, "CHARGE", "<b>Charge</b>"},
        {"CS2_029", "Fireball", "MAGE", "SPELL", 4, 0, 0, 0, 0, "", "Deal $6 damage."},
        {"CS2_119", "Oasis Snapjaw", "NEUTRAL", "MINION", 4, 2, 7, 0, 0, "", ""},
        {"CS2_022", "Polymorph", "MAGE", "SPELL", 4, 0, 0, 0, 0, "", "Transform a minion into a 1/1 Sheep."},
        {"CS2_179", "Sen'jin Shieldmasta", "NEUTRAL", "MINION", 4, 3, 5, 0, 0, "TAUNT", "<b>Taunt</b>"},
        {"EX1_593", "Nightblade", "NEUTRAL", "MINION", 5, 4, 4, 0, 0, "BATTLECRY", "<b>Battlecry: </b>Deal 3 damage to the enemy hero."},
        {"CS2_200", "Boulderfist Ogre", "NEUTRAL", "MINION", 6, 6, 7, 0, 0, "", ""},
        // Warrior fixture
        {"CS2_108", "Execute", "WARRIOR", "SPELL", 1, 0, 0, 0, 0, "", "Destroy a damaged enemy minion."},
        {"EX1_400", "Whirlwind", "WARRIOR", "SPELL", 1, 0, 0, 0, 0, "", "Deal $1 damage to ALL minions."},
        {"CS2_106", "Fiery War Axe", "WARRIOR", "WEAPON", 2, 3, 0, 2, 0, "", ""},
        {"CS2_105", "Heroic Strike", "WARRIOR", "SPELL", 2, 0, 0, 0, 0, "", "Give your hero +4 Attack this turn."},
        {"CS2_114", "Cleave", "WARRIOR", "SPELL", 2, 0, 0, 0, 0, "", "Deal $2 damage to two random enemy minions."},
        {"CS2_103", "Charge", "WARRIOR", "SPELL", 3, 0, 0, 0, 0, "", "Give a friendly minion +2 Attack and <b>Charge</b>."},
        {"EX1_606", "Shield Block", "WARRIOR", "SPELL", 3, 0, 0, 0, 0, "", "Gain 5 Armor.  Draw a card."},
        {"EX1_084", "Warsong Commander", "WARRIOR", "MINION", 3, 2, 3, 0, 0, "", "Whenever you summon a minion with 3 or less Attack, give it <b>Charge</b>."},
        {"NEW1_011", "Kor'kron Elite", "WARRIOR", "MINION", 4, 4, 3, 0, 0, "CHARGE", "<b>Charge</b>"},
        {"CS2_112", "Arcanite Reaper", "WARRIOR", "WEAPON", 5, 5, 0, 2, 0, "", ""},
        {"CS2_182", "Chillwind Yeti", "NEUTRAL", "MINION", 4, 4, 5, 0, 0, "", ""},
        // Keyword minions and unsupported cards
        {"EX1_008", "Argent Squire", "NEUTRAL", "MINION", 1, 1, 1, 0, 0, "DIVINE_SHIELD", "<b>Divine Shield</b>"},
        {"CS2_169", "Young Dragonhawk", "NEUTRAL", "MINION", 1, 1, 1, 0, 0, "WINDFURY", "<b>Windfury</b>"},
        {"CS2_142", "Kobold Geomancer", "NEUTRAL", "MINION", 2, 2, 2, 0, 1, "", "<b>Spell Damage +1</b>"},
        {"CS2_121", "Frostwolf Grunt", "NEUTRAL", "MINION", 2, 2, 2, 0, 0, "TAUNT", "<b>Taunt</b>"},
        {"CS2_131", "Stormwind Knight", "NEUTRAL", "MINION", 4, 2, 5, 0, 0, "CHARGE", "<b>Charge</b>"},
        {"EX1_011", "Voodoo Doctor", "NEUTRAL", "MINION", 1, 2, 1, 0, 0, "BATTLECRY", "<b>Battlecry:</b> Restore 2 Health."},
        {"EX1_323", "Lord Jaraxxus", "WARLOCK", "MINION", 9, 3, 15, 0, 0, "BATTLECRY", "<b>Battlecry:</b> Destroy your hero and replace him with Lord Jaraxxus."},
        {"CS2_024", "Frostbolt", "MAGE", "SPELL", 2, 0, 0, 0, 0, "FREEZE", "Deal $3 damage to a character and <b>Freeze</b> it."},
        {"EX1_062", "Old Murk-Eye", "NEUTRAL", "MINION", 4, 2, 4, 0, 0, "CHARGE", "<b>Charge</b>. Has +1 Attack for each other Murloc on the battlefield."},
        // Engine cards
        {"GAME_005", "The Coin", "NEUTRAL", "SPELL", 0, 0, 0, 0, 0, "", "Gain 1 Mana Crystal this turn only."},
        {"CS2_tk1", "Sheep", "NEUTRAL", "MINION", 0, 1, 1, 0, 0, "", ""},
        {"CS2_101t", "Silver Hand Recruit", "PALADIN", "MINION", 1, 1, 1, 0, 0, "", ""},
        {"CS2_082", "Wicked Knife", "ROGUE", "WEAPON", 1, 1, 0, 2, 0, "", ""},
        {"CS2_050", "Searing Totem", "SHAMAN", "MINION", 1, 1, 1, 0, 0, "", ""},
        {"CS2_051", "Stoneclaw Totem", "SHAMAN", "MINION", 1, 0, 2, 0, 0, "TAUNT", "<b>Taunt</b>"},
        {"CS2_052", "Wrath of Air Totem", "SHAMAN", "MINION", 1, 0, 2, 0, 0, "", "<b>Spell Damage +1</b>"},
        {"NEW1_009", "Healing Totem", "SHAMAN", "MINION", 1, 0, 2, 0, 0, "", "At the end of your turn, restore 1 Health to all friendly minions."},
        {"HERO_01", "Garrosh Hellscream", "WARRIOR", "HERO", 0, 0, 30, 0, 0, "", ""},
        {"HERO_02", "Thrall", "SHAMAN", "HERO", 0, 0, 30, 0, 0, "", ""},
        {"HERO_03", "Valeera Sanguinar", "ROGUE", "HERO", 0, 0, 30, 0, 0, "", ""},
        {"HERO_04", "Uther Lightbringer", "PALADIN", "HERO", 0, 0, 30, 0, 0, "", ""},
        {"HERO_05", "Rexxar", "HUNTER", "HERO", 0, 0, 30, 0, 0, "", ""},
        {"HERO_06", "Malfurion Stormrage", "DRUID", "HERO", 0, 0, 30, 0, 0, "", ""},
        {"HERO_07", "Gul'dan", "WARLOCK", "HERO", 0, 0, 30, 0, 0, "", ""},
        {"HERO_08", "Jaina Proudmoore", "MAGE", "HERO", 0, 0, 30, 0, 0, "", ""},
        {"HERO_09", "Anduin Wrynn", "PRIEST", "HERO", 0, 0, 30, 0, 0, "", ""},
        {"CS2_102", "Armor Up!", "WARRIOR", "HERO_POWER", 2, 0, 0, 0, 0, "", "<b>Hero Power</b> \n Gain 2 Armor."},
        {"CS2_049", "Totemic Call", "SHAMAN", "HERO_POWER", 2, 0, 0, 0, 0, "", "<b>Hero Power</b> \n Summon a random Totem."},
        {"CS2_083b", "Dagger Mastery", "ROGUE", "HERO_POWER", 2, 0, 0, 0, 0, "", "<b>Hero Power</b>\nEquip a 1/2 Dagger."},
        {"CS2_101", "Reinforce", "PALADIN", "HERO_POWER", 2, 0, 0, 0, 0, "", "<b>Hero Power</b> \n Summon a 1/1 Silver Hand Recruit."},
        {"DS1h_292", "Steady Shot", "HUNTER", "HERO_POWER", 2, 0, 0, 0, 0, "", "<b>Hero Power</b> \n Deal 2 damage to the enemy hero."},
        {"CS2_017", "Shapeshift", "DRUID", "HERO_POWER", 2, 0, 0, 0, 0, "", "<b>Hero Power</b>\n+1 Attack this turn.\n+1 Armor."},
        {"CS2_056", "Life Tap", "WARLOCK", "HERO_POWER", 2, 0, 0, 0, 0, "", "<b>Hero Power</b> \n Draw a card and take 2 damage."},
        {"CS2_034", "Fireblast", "MAGE", "HERO_POWER", 2, 0, 0, 0, 0, "", "<b>Hero Power</b> \n Deal 1 damage."},
        {"CS1h_001", "Lesser Heal", "PRIEST", "HERO_POWER", 2, 0, 0, 0, 0, "", "<b>Hero Power</b> \n Restore 2 Health."},
    };
    // clang-format on

    inline CardDb BuildDb()
    {
        CardDb db;
        for (Row const& r : kRows)
        {
            CardDef d;
            d.id = r.id;
            d.name = r.name;
            d.cardClass = r.cls;
            d.type = ParseCardType(r.type);
            d.cost = r.cost;
            d.attack = r.attack;
            d.health = r.health;
            d.durability = r.durability;
            d.spellDamage = r.spellDamage;
            d.text = r.text;
            ApplyMechanics(r.mechanics, d);
            db.Add(d);
        }
        return db;
    }

    inline CardDb const& Db()
    {
        static CardDb const db = BuildDb();
        return db;
    }

    // The GM fixture recipes of src/decks/HsDecks.cpp (GetDeckFixture), 15 cards x 2.
    inline std::vector<std::string> Fixture(bool mage)
    {
        static char const* const kMage[] = {"EX1_277", "CS2_168", "CS2_025", "CS2_172", "EX1_015", "CS2_120", "CS2_023", "CS2_122",
            "CS2_124", "CS2_029", "CS2_119", "CS2_022", "CS2_179", "EX1_593", "CS2_200"};
        static char const* const kWarrior[] = {"CS2_108", "EX1_400", "CS2_106", "CS2_105", "CS2_114", "CS2_103", "EX1_606",
            "EX1_084", "NEW1_011", "CS2_112", "CS2_168", "CS2_172", "CS2_124", "CS2_182", "CS2_200"};
        std::vector<std::string> out;
        for (int i = 0; i < 15; ++i)
        {
            out.push_back(mage ? kMage[i] : kWarrior[i]);
            out.push_back(mage ? kMage[i] : kWarrior[i]);
        }
        std::sort(out.begin(), out.end()); // ExpandDeck order
        return out;
    }

}

#endif
