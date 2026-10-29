#include "HsEncounters.h"
#include "HsMatchInternal.h"

#include <algorithm>
#include <map>
#include <set>
#include <utility>

namespace Hs
{
    namespace
    {
        // Kel'Thuzad's second phase has its rules since step E2b (Script::phase of his Frost Blast: the hero power swap, the
        // Armor trigger, the forced turn). If this were false an encounter with a phase 2 would never be ready.
        constexpr bool kPhaseTwoImplemented = true;

        constexpr int kEncounterDeckSize = 30;
        constexpr std::size_t kMaxKeyChars = 32;

        bool IsKey(std::string const& s)
        {
            if (s.empty() || s.size() > kMaxKeyChars)
                return false;
            for (char c : s)
                if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'))
                    return false;
            return true;
        }

        std::string NameOf(Match::CardDb const& db, std::string const& id)
        {
            Match::CardDef const* def = db.Find(id);
            return def && !def->name.empty() ? def->name + " (" + id + ")" : id;
        }

        // The decks are the settled Normal-mode lists of the hearthstone.wiki.gg boss pages (
        // cross-checked against Icy Veins 2014; Maexxna and Kel'Thuzad use the Icy Veins counts), in
        // adventure order. The class of the hero is only a label for the engine (the hero card and power below replace
        // everything a class gives): Noth and Heigan use the class the dataset gives their hero, every other boss hero is
        // Neutral and takes Warlock.
        std::vector<Encounter> BuildEncounters()
        {
            std::vector<Encounter> all;
        // Anub'Rekhan
        {
            Encounter e;
            e.key = "naxx_anubrekhan";
            e.name = "Anub'Rekhan";
            e.heroClass = "WARLOCK";
            e.heroCardId = "NAX1_01";
            e.heroHealth = 30;
            e.heroPowerId = "NAX1_04";
            e.deck = {
                {"FP1_006", 2}, // Deathcharger
                {"NAX1_05", 3}, // Locust Swarm
                {"CS2_024", 2}, // Frostbolt
                {"FP1_026", 2}, // Anub'ar Ambusher
                {"EX1_302", 2}, // Mortal Coil
                {"CS2_057", 2}, // Shadow Bolt
                {"EX1_303", 1}, // Shadowflame
                {"EX1_059", 2}, // Crazed Alchemist
                {"FP1_002", 2}, // Haunted Creeper
                {"FP1_017", 2}, // Nerub'ar Weblord
                {"FP1_007", 2}, // Nerubian Egg
                {"FP1_009", 2}, // Deathlord
                {"FP1_005", 2}, // Shade of Naxxramas
                {"FP1_027", 2}, // Stoneskin Gargoyle
                {"EX1_097", 2}, // Abomination
            };
            all.push_back(std::move(e));
        }
        // Grand Widow Faerlina
        {
            Encounter e;
            e.key = "naxx_faerlina";
            e.name = "Grand Widow Faerlina";
            e.heroClass = "WARLOCK";
            e.heroCardId = "NAX2_01";
            e.heroHealth = 30;
            e.heroPowerId = "NAX2_03";
            e.deck = {
                {"FP1_006", 2}, // Deathcharger
                {"NAX2_05", 5}, // Worshipper
                {"NAXM_001", 2}, // Necroknight
                {"FP1_023", 2}, // Dark Cultist
                {"CS2_062", 2}, // Hellfire
                {"EX1_607", 2}, // Inner Rage
                {"EX1_392", 2}, // Battle Rage
                {"CS2_104", 2}, // Rampage
                {"CS2_188", 2}, // Abusive Sergeant
                {"FP1_028", 2}, // Undertaker
                {"FP1_007", 2}, // Nerubian Egg
                {"EX1_007", 2}, // Acolyte of Pain
                {"FP1_029", 2}, // Dancing Swords
                {"EX1_595", 1}, // Cult Master
            };
            all.push_back(std::move(e));
        }
        // Maexxna
        {
            Encounter e;
            e.key = "naxx_maexxna";
            e.name = "Maexxna";
            e.heroClass = "WARLOCK";
            e.heroCardId = "NAX3_01";
            e.heroHealth = 30;
            e.heroPowerId = "NAX3_02";
            e.deck = {
                {"NAX3_03", 1}, // Necrotic Poison
                {"FP1_011", 3}, // Webspinner
                {"FP1_026", 2}, // Anub'ar Ambusher
                {"FP1_025", 2}, // Reincarnate
                {"EX1_066", 2}, // Acidic Swamp Ooze
                {"EX1_162", 2}, // Dire Wolf Alpha
                {"FP1_002", 6}, // Haunted Creeper
                {"FP1_007", 2}, // Nerubian Egg
                {"EX1_170", 2}, // Emperor Cobra
                {"FP1_005", 2}, // Shade of Naxxramas
                {"FP1_027", 2}, // Stoneskin Gargoyle
                {"FP1_012", 2}, // Sludge Belcher
                {"EX1_586", 2}, // Sea Giant
            };
            all.push_back(std::move(e));
        }
        // Noth the Plaguebringer
        {
            Encounter e;
            e.key = "naxx_noth";
            e.name = "Noth the Plaguebringer";
            e.heroClass = "MAGE";
            e.heroCardId = "NAX4_01";
            e.heroHealth = 30;
            e.heroPowerId = "NAX4_04";
            e.deck = {
                {"NAXM_002", 2}, // Skeletal Smith
                {"NAXM_001", 2}, // Necroknight
                {"NAX4_05", 2}, // Plague
                {"EX1_287", 2}, // Counterspell
                {"FP1_018", 2}, // Duplicate
                {"EX1_295", 1}, // Ice Block
                {"EX1_302", 2}, // Mortal Coil
                {"FP1_028", 2}, // Undertaker
                {"EX1_012", 1}, // Bloodmage Thalnos
                {"EX1_059", 1}, // Crazed Alchemist
                {"FP1_004", 2}, // Mad Scientist
                {"FP1_024", 2}, // Unstable Ghoul
                {"EX1_007", 2}, // Acolyte of Pain
                {"tt_004", 2}, // Flesheating Ghoul
                {"EX1_595", 2}, // Cult Master
                {"EX1_097", 2}, // Abomination
                {"EX1_002", 1}, // The Black Knight
            };
            all.push_back(std::move(e));
        }
        // Heigan the Unclean
        {
            Encounter e;
            e.key = "naxx_heigan";
            e.name = "Heigan the Unclean";
            e.heroClass = "WARLOCK";
            e.heroCardId = "NAX5_01";
            e.heroHealth = 30;
            e.heroPowerId = "NAX5_02";
            e.deck = {
                {"FP1_006", 2}, // Deathcharger
                {"NAX5_03", 2}, // Mindpocalypse
                {"CS2_059", 2}, // Blood Imp
                {"EX1_319", 2}, // Flame Imp
                {"EX1_308", 2}, // Soulfire
                {"CS2_065", 2}, // Voidwalker
                {"EX1_596", 2}, // Demonfire
                {"EX1_301", 2}, // Felguard
                {"FP1_022", 4}, // Voidcaller
                {"EX1_320", 2}, // Bane of Doom
                {"EX1_310", 2}, // Doomguard
                {"CS2_064", 2}, // Dread Infernal
                {"EX1_066", 2}, // Acidic Swamp Ooze
                {"EX1_564", 2}, // Faceless Manipulator
            };
            all.push_back(std::move(e));
        }
        // Loatheb
        {
            Encounter e;
            e.key = "naxx_loatheb";
            e.name = "Loatheb";
            e.heroClass = "WARLOCK";
            e.heroCardId = "NAX6_01";
            e.heroHealth = 75;
            e.heroPowerId = "NAX6_02";
            e.deck = {
                {"FP1_006", 2}, // Deathcharger
                {"NAX6_04", 4}, // Sporeburst
                {"NAX6_03", 4}, // Deathbloom
                {"FP1_019", 2}, // Poison Seeds
                {"EX1_029", 2}, // Leper Gnome
                {"EX1_066", 2}, // Acidic Swamp Ooze
                {"FP1_024", 2}, // Unstable Ghoul
                {"FP1_029", 2}, // Dancing Swords
                {"tt_004", 2}, // Flesheating Ghoul
                {"FP1_005", 2}, // Shade of Naxxramas
                {"EX1_564", 2}, // Faceless Manipulator
                {"CS1_069", 2}, // Fen Creeper
                {"FP1_008", 2}, // Spectral Knight
            };
            all.push_back(std::move(e));
        }
        // Instructor Razuvious
        {
            Encounter e;
            e.key = "naxx_razuvious";
            e.name = "Instructor Razuvious";
            e.heroClass = "WARLOCK";
            e.heroCardId = "NAX7_01";
            e.heroHealth = 40;
            e.heroPowerId = "NAX7_03";
            e.deck = {
                {"FP1_006", 2}, // Deathcharger
                {"NAX7_04", 4}, // Massive Runeblade
                {"NAXM_001", 2}, // Necroknight
                {"EX1_384", 1}, // Avenging Wrath
                {"CS2_114", 2}, // Cleave
                {"EX1_407", 2}, // Brawl
                {"CS2_188", 2}, // Abusive Sergeant
                {"NEW1_037", 2}, // Master Swordsmith
                {"EX1_007", 2}, // Acolyte of Pain
                {"FP1_029", 2}, // Dancing Swords
                {"FP1_009", 2}, // Deathlord
                {"FP1_016", 2}, // Wailing Soul
                {"FP1_008", 2}, // Spectral Knight
                {"CS2_221", 2}, // Spiteful Smith
                {"EX1_002", 1}, // The Black Knight
            };
            e.boss.board = {"NAX7_02", "NAX7_02"};
            e.human.hand = {"NAX7_05"};
            all.push_back(std::move(e));
        }
        // Gothik the Harvester
        {
            Encounter e;
            e.key = "naxx_gothik";
            e.name = "Gothik the Harvester";
            e.heroClass = "WARLOCK";
            e.heroCardId = "NAX8_01";
            e.heroHealth = 30;
            e.heroPowerId = "NAX8_02";
            e.deck = {
                {"NAX8_03", 4}, // Unrelenting Trainee
                {"NAX8_04", 4}, // Unrelenting Warrior
                {"NAX8_05", 4}, // Unrelenting Rider
                {"CS2_063", 2}, // Corruption
                {"EX1_302", 2}, // Mortal Coil
                {"CS2_061", 2}, // Drain Life
                {"CS2_057", 2}, // Shadow Bolt
                {"EX1_058", 2}, // Sunfury Protector
                {"FP1_024", 2}, // Unstable Ghoul
                {"EX1_097", 2}, // Abomination
                {"FP1_008", 2}, // Spectral Knight
                {"CS2_155", 1}, // Archmage
                {"CS2_161", 1}, // Ravenholdt Assassin
            };
            all.push_back(std::move(e));
        }
        // The Four Horsemen
        {
            Encounter e;
            e.key = "naxx_horsemen";
            e.name = "The Four Horsemen";
            e.heroClass = "WARLOCK";
            e.heroCardId = "NAX9_01";
            e.heroHealth = 7;
            e.heroPowerId = "NAX9_06";
            e.deck = {
                {"NAX9_05", 2}, // Runeblade
                {"NAX9_07", 2}, // Mark of the Horsemen
                {"CS2_024", 2}, // Frostbolt
                {"FP1_020", 2}, // Avenge
                {"EX1_136", 2}, // Redemption
                {"EX1_379", 2}, // Repentance
                {"CS2_093", 2}, // Consecration
                {"EX1_332", 1}, // Silence
                {"EX1_622", 2}, // Shadow Word: Death
                {"CS2_234", 2}, // Shadow Word: Pain
                {"EX1_626", 1}, // Mass Dispel
                {"CS2_063", 2}, // Corruption
                {"EX1_302", 2}, // Mortal Coil
                {"EX1_308", 2}, // Soulfire
                {"CS2_061", 2}, // Drain Life
                {"CS2_057", 2}, // Shadow Bolt
            };
            e.boss.board = {"NAX9_02", "NAX9_03", "NAX9_04"};
            all.push_back(std::move(e));
        }
        // Patchwerk
        {
            Encounter e;
            e.key = "naxx_patchwerk";
            e.name = "Patchwerk";
            e.heroClass = "WARLOCK";
            e.heroCardId = "NAX10_01";
            e.heroHealth = 30;
            e.heroPowerId = "NAX10_03";
            // no deck: Patchwerk skips his turn-start draw (wiki: "no deck and no card draw"); a draw forced by an
            // effect takes fatigue as usual (wiki: "still takes fatigue damage if forced to draw")
            e.boss.weapon = "NAX10_02";
            e.boss.noDraw = true;
            all.push_back(std::move(e));
        }
        // Grobbulus
        {
            Encounter e;
            e.key = "naxx_grobbulus";
            e.name = "Grobbulus";
            e.heroClass = "WARLOCK";
            e.heroCardId = "NAX11_01";
            e.heroHealth = 30;
            e.heroPowerId = "NAX11_02";
            e.deck = {
                {"NAX11_04", 4}, // Mutating Injection
                {"CS2_073", 2}, // Cold Blood
                {"CS2_105", 2}, // Heroic Strike
                {"EX1_391", 2}, // Slam
                {"EX1_029", 2}, // Leper Gnome
                {"EX1_066", 2}, // Acidic Swamp Ooze
                {"FP1_003", 4}, // Echoing Ooze
                {"EX1_096", 2}, // Loot Hoarder
                {"FP1_007", 2}, // Nerubian Egg
                {"FP1_029", 2}, // Dancing Swords
                {"EX1_595", 2}, // Cult Master
                {"EX1_097", 2}, // Abomination
                {"FP1_012", 2}, // Sludge Belcher
            };
            e.first = EncounterFirst::Human;
            all.push_back(std::move(e));
        }
        // Gluth
        {
            Encounter e;
            e.key = "naxx_gluth";
            e.name = "Gluth";
            e.heroClass = "WARLOCK";
            e.heroCardId = "NAX12_01";
            e.heroHealth = 30;
            e.heroPowerId = "NAX12_02";
            e.deck = {
                {"NAX12_03", 4}, // Jaws
                {"NAX12_04", 2}, // Enrage
                {"NAXM_002", 2}, // Skeletal Smith
                {"NAXM_001", 2}, // Necroknight
                {"EX1_578", 2}, // Savagery
                {"EX1_570", 2}, // Bite
                {"FP1_001", 6}, // Zombie Chow
                {"FP1_002", 2}, // Haunted Creeper
                {"FP1_024", 2}, // Unstable Ghoul
                {"EX1_097", 2}, // Abomination
                {"FP1_012", 2}, // Sludge Belcher
                {"CS2_221", 2}, // Spiteful Smith
            };
            all.push_back(std::move(e));
        }
        // Thaddius
        {
            Encounter e;
            e.key = "naxx_thaddius";
            e.name = "Thaddius";
            e.heroClass = "WARLOCK";
            e.heroCardId = "NAX13_01";
            e.heroHealth = 30;
            e.heroPowerId = "NAX13_02";
            e.deck = {
                {"NAX13_03", 2}, // Supercharge
                {"FP1_023", 2}, // Dark Cultist
                {"EX1_251", 2}, // Forked Lightning
                {"EX1_238", 2}, // Lightning Bolt
                {"EX1_259", 2}, // Lightning Storm
                {"EX1_258", 2}, // Unbound Elemental
                {"FP1_001", 2}, // Zombie Chow
                {"EX1_066", 2}, // Acidic Swamp Ooze
                {"FP1_002", 2}, // Haunted Creeper
                {"FP1_007", 2}, // Nerubian Egg
                {"FP1_005", 2}, // Shade of Naxxramas
                {"FP1_027", 2}, // Stoneskin Gargoyle
                {"FP1_012", 4}, // Sludge Belcher
                {"FP1_008", 2}, // Spectral Knight
            };
            all.push_back(std::move(e));
        }
        // Sapphiron
        {
            Encounter e;
            e.key = "naxx_sapphiron";
            e.name = "Sapphiron";
            e.heroClass = "WARLOCK";
            e.heroCardId = "NAX14_01";
            e.heroHealth = 30;
            e.heroPowerId = "NAX14_02";
            e.deck = {
                {"NAXM_002", 2}, // Skeletal Smith
                {"NAXM_001", 2}, // Necroknight
                {"NAX14_04", 2}, // Pure Cold
                {"CS2_031", 2}, // Ice Lance
                {"CS2_024", 2}, // Frostbolt
                {"NEW1_012", 2}, // Mana Wyrm
                {"EX1_275", 2}, // Cone of Cold
                {"CS2_028", 2}, // Blizzard
                {"EX1_096", 2}, // Loot Hoarder
                {"FP1_024", 2}, // Unstable Ghoul
                {"FP1_029", 2}, // Dancing Swords
                {"FP1_005", 2}, // Shade of Naxxramas
                {"FP1_016", 2}, // Wailing Soul
                {"FP1_012", 2}, // Sludge Belcher
                {"EX1_283", 2}, // Frost Elemental
            };
            e.human.board = {"NAX14_03"};
            all.push_back(std::move(e));
        }
        // Kel'Thuzad
        {
            Encounter e;
            e.key = "naxx_kelthuzad";
            e.name = "Kel'Thuzad";
            e.heroClass = "WARLOCK";
            e.heroCardId = "NAX15_01";
            e.heroHealth = 30;
            e.heroArmor = 10;
            e.heroPowerId = "NAX15_02";
            e.deck = {
                {"NAXM_002", 2}, // Skeletal Smith
                {"CS2_024", 2}, // Frostbolt
                {"CS2_028", 1}, // Blizzard
                {"FP1_023", 2}, // Dark Cultist
                {"EX1_334", 1}, // Shadow Madness
                {"EX1_275", 1}, // Cone of Cold
                {"CS2_037", 2}, // Frost Shock
                {"CS2_057", 2}, // Shadow Bolt
                {"EX1_312", 2}, // Twisting Nether
                {"FP1_001", 2}, // Zombie Chow
                {"EX1_007", 2}, // Acolyte of Pain
                {"FP1_005", 2}, // Shade of Naxxramas
                {"FP1_027", 1}, // Stoneskin Gargoyle
                {"EX1_097", 2}, // Abomination
                {"FP1_012", 2}, // Sludge Belcher
                {"FP1_008", 2}, // Spectral Knight
                {"EX1_283", 2}, // Frost Elemental
            };
            e.phase2.present = true;
            e.phase2.heroPowerId = "NAX15_04";
            e.phase2.summons = {"NAX15_03n", "NAX15_03n"};
            e.phase2.forcedTurn = 11;
            all.push_back(std::move(e));
        }
            return all;
        }

        // The counted list of a loaner deck, one entry per copy, sorted by id.
        std::vector<std::string> Copies(std::vector<EncounterCard> const& cards)
        {
            std::map<std::string, int> counts;
            for (EncounterCard const& c : cards)
                counts[c.id] += c.count;
            std::vector<std::string> out;
            for (auto const& entry : counts)
                for (int i = 0; i < entry.second; ++i)
                    out.push_back(entry.first);
            return out;
        }

        // The nine class challenges in adventure order (wing 1 .. 5). The decks are the main sections of the "Class-challenge
        // loaner decks" (the wiki's final lists; the 2014 variants are not used). Each
        // challenge is fought against the Normal setup of the boss it names, unchanged; the reward is its class card, which the reward card also sits in the deck (as in the original).
        std::vector<Challenge> BuildChallenges()
        {
            std::vector<Challenge> all;
            // Druid vs Grand Widow Faerlina (wing 1)
            {
                Challenge c;
                c.key = "naxx_challenge_druid";
                c.heroClass = "DRUID";
                c.deck = Copies({
                    {"EX1_169", 2}, // Innervate
                    {"CS2_005", 2}, // Claw
                    {"EX1_161", 1}, // Naturalize
                    {"CS2_009", 2}, // Mark of the Wild
                    {"EX1_160", 2}, // Power of the Wild
                    {"EX1_155", 2}, // Mark of Nature
                    {"EX1_166", 1}, // Keeper of the Grove
                    {"FP1_019", 2}, // Poison Seeds
                    {"CS2_012", 2}, // Swipe
                    {"FP1_003", 2}, // Echoing Ooze
                    {"FP1_002", 2}, // Haunted Creeper
                    {"FP1_007", 2}, // Nerubian Egg
                    {"FP1_027", 5}, // Stoneskin Gargoyle
                    {"EX1_093", 2}, // Defender of Argus
                    {"NEW1_040", 1}, // Hogger
                });
                c.encounterKey = "naxx_faerlina";
                c.rewardCardId = "FP1_019";
                all.push_back(std::move(c));
            }
            // Rogue vs Maexxna (wing 1)
            {
                Challenge c;
                c.key = "naxx_challenge_rogue";
                c.heroClass = "ROGUE";
                c.deck = Copies({
                    {"EX1_144", 1}, // Shadowstep
                    {"EX1_131", 1}, // Defias Ringleader
                    {"EX1_134", 1}, // SI:7 Agent
                    {"FP1_026", 2}, // Anub'ar Ambusher
                    {"CS2_189", 2}, // Elven Archer
                    {"EX1_011", 1}, // Voodoo Doctor
                    {"FP1_003", 5}, // Echoing Ooze
                    {"EX1_506", 1}, // Murloc Tidehunter
                    {"EX1_015", 1}, // Novice Engineer
                    {"EX1_058", 1}, // Sunfury Protector
                    {"EX1_049", 1}, // Youthful Brewmaster
                    {"CS2_117", 1}, // Earthen Ring Farseer
                    {"EX1_085", 1}, // Mind Control Tech
                    {"CS2_196", 1}, // Razorfen Hunter
                    {"EX1_083", 1}, // Tinkmaster Overspark
                    {"EX1_093", 1}, // Defender of Argus
                    {"CS2_147", 1}, // Gnomish Inventor
                    {"EX1_005", 1}, // Big Game Hunter
                    {"FP1_030", 1}, // Loatheb
                    {"NEW1_041", 2}, // Stampeding Kodo
                    {"EX1_283", 1}, // Frost Elemental
                    {"EX1_583", 1}, // Priestess of Elune
                    {"EX1_561", 1}, // Alexstrasza
                });
                c.encounterKey = "naxx_maexxna";
                c.rewardCardId = "FP1_026";
                all.push_back(std::move(c));
            }
            // Mage vs Heigan the Unclean (wing 2)
            {
                Challenge c;
                c.key = "naxx_challenge_mage";
                c.heroClass = "MAGE";
                c.deck = Copies({
                    {"EX1_287", 1}, // Counterspell
                    {"FP1_018", 2}, // Duplicate
                    {"EX1_289", 1}, // Ice Barrier
                    {"EX1_295", 1}, // Ice Block
                    {"EX1_612", 2}, // Kirin Tor Mage
                    {"EX1_294", 1}, // Mirror Entity
                    {"tt_010", 1}, // Spellbender
                    {"EX1_594", 1}, // Vaporize
                    {"EX1_274", 2}, // Ethereal Arcanist
                    {"EX1_080", 2}, // Secretkeeper
                    {"FP1_028", 2}, // Undertaker
                    {"EX1_012", 1}, // Bloodmage Thalnos
                    {"EX1_096", 2}, // Loot Hoarder
                    {"FP1_004", 4}, // Mad Scientist
                    {"FP1_007", 2}, // Nerubian Egg
                    {"FP1_031", 1}, // Baron Rivendare
                    {"FP1_012", 2}, // Sludge Belcher
                    {"EX1_110", 1}, // Cairne Bloodhoof
                    {"EX1_016", 1}, // Sylvanas Windrunner
                });
                c.encounterKey = "naxx_heigan";
                c.rewardCardId = "FP1_018";
                all.push_back(std::move(c));
            }
            // Hunter vs Loatheb (wing 2)
            {
                Challenge c;
                c.key = "naxx_challenge_hunter";
                c.heroClass = "HUNTER";
                c.deck = Copies({
                    {"FP1_011", 30}, // Webspinner
                });
                c.encounterKey = "naxx_loatheb";
                c.rewardCardId = "FP1_011";
                all.push_back(std::move(c));
            }
            // Shaman vs Gothik the Harvester (wing 3)
            {
                Challenge c;
                c.key = "naxx_challenge_shaman";
                c.heroClass = "SHAMAN";
                c.deck = Copies({
                    {"FP1_025", 2}, // Reincarnate
                    {"CS2_045", 2}, // Rockbiter Weapon
                    {"EX1_565", 3}, // Flametongue Totem
                    {"EX1_259", 2}, // Lightning Storm
                    {"EX1_246", 2}, // Hex
                    {"EX1_250", 2}, // Earth Elemental
                    {"FP1_002", 3}, // Haunted Creeper
                    {"FP1_007", 4}, // Nerubian Egg
                    {"EX1_019", 2}, // Shattered Sun Cleric
                    {"EX1_083", 1}, // Tinkmaster Overspark
                    {"EX1_595", 1}, // Cult Master
                    {"EX1_093", 2}, // Defender of Argus
                    {"FP1_015", 1}, // Feugen
                    {"FP1_014", 1}, // Stalagg
                    {"EX1_110", 1}, // Cairne Bloodhoof
                    {"EX1_016", 1}, // Sylvanas Windrunner
                });
                c.encounterKey = "naxx_gothik";
                c.rewardCardId = "FP1_025";
                all.push_back(std::move(c));
            }
            // Warlock vs The Four Horsemen (wing 3)
            {
                Challenge c;
                c.key = "naxx_challenge_warlock";
                c.heroClass = "WARLOCK";
                c.deck = Copies({
                    {"NEW1_003", 1}, // Sacrificial Pact
                    {"EX1_308", 1}, // Soulfire
                    {"CS2_065", 2}, // Voidwalker
                    {"EX1_596", 2}, // Demonfire
                    {"CS2_061", 2}, // Drain Life
                    {"EX1_301", 2}, // Felguard
                    {"EX1_317", 2}, // Sense Demons
                    {"CS2_057", 1}, // Shadow Bolt
                    {"FP1_022", 6}, // Voidcaller
                    {"EX1_320", 1}, // Bane of Doom
                    {"EX1_310", 2}, // Doomguard
                    {"CS2_064", 2}, // Dread Infernal
                    {"EX1_323", 1}, // Lord Jaraxxus
                    {"FP1_024", 2}, // Unstable Ghoul
                    {"EX1_597", 2}, // Imp Master
                    {"EX1_614", 1}, // Illidan Stormrage (Xavius)
                });
                c.encounterKey = "naxx_horsemen";
                c.rewardCardId = "FP1_022";
                all.push_back(std::move(c));
            }
            // Warrior vs Grobbulus (wing 4)
            {
                Challenge c;
                c.key = "naxx_challenge_warrior";
                c.heroClass = "WARRIOR";
                c.deck = Copies({
                    {"EX1_400", 2}, // Whirlwind
                    {"EX1_402", 2}, // Armorsmith
                    {"EX1_392", 2}, // Battle Rage
                    {"CS2_114", 1}, // Cleave
                    {"CS2_108", 1}, // Execute
                    {"CS2_104", 1}, // Rampage
                    {"CS2_106", 2}, // Fiery War Axe
                    {"EX1_604", 2}, // Frothing Berserker
                    {"FP1_021", 2}, // Death's Bite
                    {"EX1_407", 1}, // Brawl
                    {"EX1_414", 1}, // Grommash Hellscream
                    {"EX1_393", 2}, // Amani Berserker
                    {"FP1_024", 2}, // Unstable Ghoul
                    {"EX1_007", 2}, // Acolyte of Pain
                    {"EX1_412", 2}, // Raging Worgen
                    {"EX1_390", 2}, // Tauren Warrior
                    {"FP1_015", 1}, // Feugen
                    {"CS2_221", 1}, // Spiteful Smith
                    {"FP1_014", 1}, // Stalagg
                });
                c.encounterKey = "naxx_grobbulus";
                c.rewardCardId = "FP1_021";
                all.push_back(std::move(c));
            }
            // Priest vs Thaddius (wing 4)
            {
                Challenge c;
                c.key = "naxx_challenge_priest";
                c.heroClass = "PRIEST";
                c.deck = Copies({
                    {"CS2_004", 2}, // Power Word: Shield
                    {"CS1_129", 2}, // Inner Fire
                    {"CS2_236", 2}, // Divine Spirit
                    {"FP1_023", 2}, // Dark Cultist
                    {"EX1_334", 2}, // Shadow Madness
                    {"EX1_591", 2}, // Auchenai Soulpriest
                    {"EX1_335", 2}, // Lightspawn
                    {"EX1_345", 2}, // Mindgames
                    {"EX1_623", 2}, // Temple Enforcer
                    {"EX1_091", 2}, // Cabal Shadow Priest
                    {"EX1_350", 1}, // Prophet Velen
                    {"FP1_001", 2}, // Zombie Chow
                    {"EX1_059", 2}, // Crazed Alchemist
                    {"FP1_007", 2}, // Nerubian Egg
                    {"FP1_009", 2}, // Deathlord
                    {"FP1_010", 1}, // Maexxna
                });
                c.encounterKey = "naxx_thaddius";
                c.rewardCardId = "FP1_023";
                all.push_back(std::move(c));
            }
            // Paladin vs Kel'Thuzad (wing 5)
            {
                Challenge c;
                c.key = "naxx_challenge_paladin";
                c.heroClass = "PALADIN";
                c.deck = Copies({
                    {"FP1_020", 5}, // Avenge
                    {"EX1_363", 2}, // Blessing of Wisdom
                    {"EX1_382", 2}, // Aldor Peacekeeper
                    {"EX1_349", 2}, // Divine Favor
                    {"EX1_366", 2}, // Sword of Justice
                    {"CS2_092", 4}, // Blessing of Kings
                    {"CS2_093", 1}, // Consecration
                    {"CS2_088", 1}, // Guardian of Kings
                    {"FP1_003", 7}, // Echoing Ooze
                    {"EX1_020", 2}, // Scarlet Crusader
                    {"EX1_019", 1}, // Shattered Sun Cleric
                    {"EX1_093", 1}, // Defender of Argus
                });
                c.encounterKey = "naxx_kelthuzad";
                c.rewardCardId = "FP1_020";
                all.push_back(std::move(c));
            }
            return all;
        }

        // Every card id an encounter needs the engine to play, once.
        std::vector<std::string> RequiredCards(Encounter const& e)
        {
            std::set<std::string> ids;
            for (EncounterCard const& c : e.deck)
                ids.insert(c.id);
            for (Match::SideStart const* start : { &e.boss, &e.human })
            {
                ids.insert(start->board.begin(), start->board.end());
                ids.insert(start->hand.begin(), start->hand.end());
                if (!start->weapon.empty())
                    ids.insert(start->weapon);
            }
            ids.insert(e.heroPowerId);
            if (e.phase2.present)
            {
                ids.insert(e.phase2.heroPowerId);
                ids.insert(e.phase2.summons.begin(), e.phase2.summons.end());
            }
            return std::vector<std::string>(ids.begin(), ids.end());
        }

        bool DeckType(Match::CardType t)
        {
            return t == Match::CardType::Minion || t == Match::CardType::Spell || t == Match::CardType::Weapon;
        }

        // "" or the first problem with one side's start cards.
        std::string CheckStart(Match::CardDb const& db, Match::SideStart const& start, char const* side)
        {
            std::string const who = std::string(side) + " start ";
            if (start.board.size() > static_cast<std::size_t>(Match::kMaxBoard))
                return who + "board has more than " + std::to_string(Match::kMaxBoard) + " minions";
            for (std::string const& id : start.board)
            {
                Match::CardDef const* def = db.Find(id);
                if (!def)
                    return who + "board: unknown card " + id;
                if (def->type != Match::CardType::Minion)
                    return who + "board: " + NameOf(db, id) + " is not a minion";
            }
            if (!start.weapon.empty())
            {
                Match::CardDef const* def = db.Find(start.weapon);
                if (!def)
                    return who + "weapon: unknown card " + start.weapon;
                if (def->type != Match::CardType::Weapon)
                    return who + "weapon: " + NameOf(db, start.weapon) + " is not a weapon";
            }
            if (start.hand.size() + static_cast<std::size_t>(Match::kStartHandSecond) + 1 >
                static_cast<std::size_t>(Match::kMaxHand))
                return who + "hand has too many extra cards";
            for (std::string const& id : start.hand)
            {
                Match::CardDef const* def = db.Find(id);
                if (!def)
                    return who + "hand: unknown card " + id;
                if (!DeckType(def->type))
                    return who + "hand: " + NameOf(db, id) + " cannot be a hand card";
            }
            return std::string();
        }
    }

    std::vector<Encounter> const& Encounters()
    {
        static std::vector<Encounter> const all = BuildEncounters();
        return all;
    }

    Encounter const* FindEncounter(std::string const& key)
    {
        for (Encounter const& e : Encounters())
            if (e.key == key)
                return &e;
        return nullptr;
    }

    std::vector<std::string> ExpandEncounterDeck(Encounter const& e)
    {
        std::map<std::string, int> counts;
        for (EncounterCard const& c : e.deck)
            counts[c.id] += c.count;
        std::vector<std::string> out;
        for (auto const& entry : counts)
            for (int i = 0; i < entry.second; ++i)
                out.push_back(entry.first);
        return out;
    }

    std::string CheckEncounter(Match::CardDb const& db, Encounter const& e, bool requireSupported)
    {
        if (!IsKey(e.key))
            return "the key must be 1.." + std::to_string(kMaxKeyChars) + " characters of [0-9A-Za-z_]";
        if (Match::HeroCardOf(e.heroClass).empty())
            return "the hero class '" + e.heroClass + "' is not one of the nine classes";
        Match::CardDef const* hero = db.Find(e.heroCardId);
        if (!hero || hero->type != Match::CardType::Hero)
            return "hero card " + e.heroCardId + " is not a hero card";
        if (e.heroHealth < 1 || e.heroHealth > 200)
            return "hero health must be 1..200";
        if (e.heroArmor < 0 || e.heroArmor > 200)
            return "hero armor must be 0..200";
        Match::CardDef const* power = db.Find(e.heroPowerId);
        if (!power || power->type != Match::CardType::HeroPower)
            return "hero power " + e.heroPowerId + " is not a hero power";

        int total = 0;
        std::set<std::string> seen;
        for (EncounterCard const& c : e.deck)
        {
            Match::CardDef const* def = db.Find(c.id);
            if (!def)
                return "unknown card " + c.id;
            if (!DeckType(def->type))
                return NameOf(db, c.id) + " cannot be in a deck";
            if (c.count < 1 || c.count > static_cast<int>(Match::kMaxDeckCards))
                return "the count of " + NameOf(db, c.id) + " must be 1.." + std::to_string(Match::kMaxDeckCards);
            if (!seen.insert(c.id).second)
                return NameOf(db, c.id) + " is listed twice";
            total += c.count;
        }
        if (e.deck.empty())
        {
            if (!e.boss.noDraw)
                return "the deck is empty but the boss draws";
        }
        else if (total != kEncounterDeckSize)
            return "the deck has " + std::to_string(total) + " cards instead of " + std::to_string(kEncounterDeckSize);

        std::string problem = CheckStart(db, e.boss, "boss");
        if (problem.empty())
            problem = CheckStart(db, e.human, "human");
        if (!problem.empty())
            return problem;
        if (e.human.noDraw || !e.human.weapon.empty())
            return "the human side only gets a board and hand cards";

        if (e.phase2.present)
        {
            Match::CardDef const* p2 = db.Find(e.phase2.heroPowerId);
            if (!p2 || p2->type != Match::CardType::HeroPower)
                return "phase 2 hero power " + e.phase2.heroPowerId + " is not a hero power";
            for (std::string const& id : e.phase2.summons)
            {
                Match::CardDef const* def = db.Find(id);
                if (!def || def->type != Match::CardType::Minion)
                    return "phase 2 summon " + id + " is not a minion";
            }
            if (e.phase2.forcedTurn < 1)
                return "phase 2 needs a forced turn";
        }

        if (requireSupported)
        {
            for (std::string const& id : RequiredCards(e))
                if (!db.Supported(id))
                    return NameOf(db, id) + " is not supported by the match engine yet";
            if (e.phase2.present)
            {
                if (!kPhaseTwoImplemented)
                    return "the second phase is not implemented yet";
                // The engine runs the phase from the script of the boss's first hero power; it must say what this data says.
                Match::Detail::Script const* ps = Match::Detail::FindScript(e.heroPowerId);
                if (!ps || !ps->phase.present || ps->phase.powerId != e.phase2.heroPowerId ||
                    ps->phase.forcedTurn != e.phase2.forcedTurn ||
                    ps->phase.summons != static_cast<int>(e.phase2.summons.size()) ||
                    (!e.phase2.summons.empty() && ps->phase.summonId != e.phase2.summons.front()))
                    return "the hero power " + NameOf(db, e.heroPowerId) + " does not start this second phase";
            }
        }
        return std::string();
    }

    bool EncounterReady(Match::CardDb const& db, Encounter const& e)
    {
        return CheckEncounter(db, e, true).empty();
    }

    std::vector<std::string> EncounterUnsupported(Match::CardDb const& db, Encounter const& e)
    {
        std::vector<std::string> out;
        for (std::string const& id : RequiredCards(e))
            if (db.Find(id) && !db.Supported(id))
                out.push_back(id);
        return out;
    }

    void ApplyEncounter(Encounter const& e, MatchStartParams& params)
    {
        params.difficulty = "boss";
        params.profile = Match::AiProfile::Hard;
        params.opponentClass = e.heroClass;
        params.opponentDeck = ExpandEncounterDeck(e);
        params.opponentHeroHealth = e.heroHealth;
        params.opponentHeroArmor = e.heroArmor;
        params.opponentHeroCardId = e.heroCardId;
        params.opponentHeroPowerId = e.heroPowerId;
        params.opponentStart = e.boss;
        params.humanStart = e.human;
        params.firstSide = e.first == EncounterFirst::Human ? 0 : e.first == EncounterFirst::Boss ? 1 : -1;
        params.meta.opponentDeckKey = e.key;
    }

    std::vector<Challenge> const& Challenges()
    {
        static std::vector<Challenge> const all = BuildChallenges();
        return all;
    }

    Challenge const* FindChallenge(std::string const& key)
    {
        for (Challenge const& c : Challenges())
            if (c.key == key)
                return &c;
        return nullptr;
    }

    std::string CheckChallenge(Match::CardDb const& db, Challenge const& c, bool requireSupported)
    {
        if (!IsKey(c.key))
            return "the key must be 1.." + std::to_string(kMaxKeyChars) + " characters of [0-9A-Za-z_]";
        if (Match::HeroCardOf(c.heroClass).empty())
            return "the class '" + c.heroClass + "' is not one of the nine classes";
        if (c.deck.size() != static_cast<std::size_t>(kEncounterDeckSize))
            return "the loaner deck has " + std::to_string(c.deck.size()) + " cards instead of " + std::to_string(kEncounterDeckSize);
        for (std::string const& id : c.deck)
        {
            Match::CardDef const* def = db.Find(id);
            if (!def)
                return "unknown card " + id;
            if (!DeckType(def->type))
                return NameOf(db, id) + " cannot be in a deck";
            if (requireSupported && !db.Supported(id))
                return NameOf(db, id) + " is not supported by the match engine yet";
        }
        if (!FindEncounter(c.encounterKey))
            return "the encounter " + c.encounterKey + " does not exist";
        if (!db.Find(c.rewardCardId))
            return "the reward card " + c.rewardCardId + " is unknown";
        return std::string();
    }

    bool ChallengeReady(Match::CardDb const& db, Challenge const& c)
    {
        if (!CheckChallenge(db, c, true).empty())
            return false;
        Encounter const* e = FindEncounter(c.encounterKey);
        return e && EncounterReady(db, *e);
    }

    void ApplyChallenge(Challenge const& c, MatchStartParams& params)
    {
        params.humanClass = c.heroClass;
        params.humanDeck = c.deck;
    }
}
