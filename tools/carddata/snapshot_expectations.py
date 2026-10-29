"""Historic spot-check expectations for the pinned 6024 snapshot (Naxxramas launch build, 2014-07-22).

Each entry names a card whose value changed around the snapshot date (before it, with
the Naxxramas patch itself, or after it), so a wrong build, a contemporary data source
or a silent override shows up immediately.

Fields:
  id            card id
  name          expected name
  expect        {field: value} that must match exactly
  text_has      substrings that the plain text (markup stripped) must contain
  text_lacks    substrings that the plain text must NOT contain
  why           where the historic value comes from
"""

SPOT_CHECKS = [
    {"id": "EX1_538", "name": "Unleash the Hounds", "expect": {"cost": 3, "set": "EXPERT1"},
     "why": "cost 2 -> 3 in patch 1.0.0.5314 (2014-05-08), before the snapshot"},
    {"id": "EX1_116", "name": "Leeroy Jenkins", "expect": {"cost": 4, "attack": 6, "health": 2},
     "why": "cost 4 -> 5 only in patch 1.2.0.6485 (2014-09-22), after the snapshot"},
    {"id": "CS2_237", "name": "Starving Buzzard", "expect": {"cost": 2, "attack": 2, "health": 1},
     "why": "became 5 mana 3/2 only in patch 1.2.0.6485 (2014-09-22)"},
    {"id": "EX1_095", "name": "Gadgetzan Auctioneer", "expect": {"cost": 5},
     "why": "cost 5 -> 6 only in patch 2.0.0.7234 (2014-12-04)"},
    {"id": "EX1_544", "name": "Flare", "expect": {"cost": 1},
     "why": "cost 1 -> 2 only in patch 2.0.0.7234 (2014-12-04)"},
    {"id": "EX1_308", "name": "Soulfire", "expect": {"cost": 0},
     "why": "cost 0 -> 1 only in patch 2.0.0.7234 (2014-12-04)"},
    {"id": "EX1_536", "name": "Eaglehorn Bow", "expect": {"cost": 3, "attack": 3, "durability": 2},
     "text_has": ["friendly Secret is revealed"],
     "why": "'friendly Secret' wording (rule change) arrived with the Naxxramas patch (build 6024, 2014-07-22)"},
    {"id": "CS2_038", "name": "Ancestral Spirit", "expect": {"cost": 2},
     "text_has": ['Give a minion "Deathrattle: Resummon this minion."'],
     "text_lacks": ["return it to the battlefield"],
     "why": "reimplemented as a Deathrattle enchantment (rule change) in build 6024"},
    {"id": "EX1_323", "name": "Lord Jaraxxus", "expect": {"type": "MINION", "cost": 9},
     "text_has": ["replace it with Lord Jaraxxus"], "text_lacks": ["replace him"],
     "why": "'replace it' wording (no rule change) arrived in build 6024"},
    {"id": "EX1_289", "name": "Ice Barrier", "expect": {"cost": 3},
     "text_has": ["When your hero is attacked, gain 8 Armor"], "text_lacks": ["As soon as"],
     "why": "wording only: 'When' instead of 'As soon as' in build 6024"},
    {"id": "EX1_559", "name": "Archmage Antonidas", "expect": {"cost": 7},
     "text_has": ["add a 'Fireball' spell to your hand"], "text_lacks": ["put a"],
     "why": "wording only: 'add ... to your hand' instead of 'put ... into your hand' in build 6024"},
    {"id": "NEW1_023", "name": "Faerie Dragon", "expect": {"cost": 2},
     "text_has": ["spells or Hero Powers"], "text_lacks": ["Spells"],
     "why": "wording only: lower-case 'spells' in build 6024"},
    {"id": "EX1_323h", "name": "Lord Jaraxxus", "expect": {"type": "HERO", "health": 15, "hero_power": "EX1_tk33"},
     "why": "hero form and its hero power link (unchanged between 5834 and 6024)"},
    {"id": "FP1_028", "name": "Undertaker", "expect": {"cost": 1, "attack": 1, "health": 2, "set": "NAXX", "rarity": "COMMON",
                                                       "acquisition": "quest", "collectible": True},
     "text_has": ["gain +1/+1"],
     "why": "Naxxramas launch values: +1/+1 per Deathrattle minion summoned (changed to +1 Attack only in a later patch)"},
    {"id": "FP1_001", "name": "Zombie Chow", "expect": {"cost": 1, "attack": 2, "health": 3, "set": "NAXX",
                                                        "acquisition": "quest", "collectible": True},
     "text_has": ["Restore 5 Health to the enemy hero"],
     "why": "Naxxramas launch values (unchanged through build 6898)"},
    {"id": "CS2_029", "name": "Fireball", "expect": {"cost": 4, "set": "CORE", "rarity": "FREE", "class": "MAGE"},
     "why": "Basic card; FREE rarity kept as in the source"},
]

# Ids that must NOT exist: they belong to later sets (proves no post-snapshot data leaked in).
MUST_BE_ABSENT = [
    "NEW1_006",  # Adrenaline Rush (never obtainable, removed from the dataset)
    "EX1_016t",  # Skeleton token no card creates (removed from the dataset)
    "GVG_001",   # Flamecannon (Goblins vs Gnomes, 2014-12): proves no later-set card leaked in
]

# Standard collectible catalog totals (acquisition == "standard"), by set.
# The Classic set had 245 collectible cards: build 6024 also flags NEW1_006 Adrenaline Rush,
# which was removed in the alpha; it is dropped from the dataset (cardlib.REMOVED_IDS).
EXPECTED_CATALOG_TOTALS = {"CORE": 133, "EXPERT1": 245}
# Acquisition "unreleased" is not used by any card at the moment (the value stays in the schema).
EXPECTED_UNRELEASED = {}
EXPECTED_COLLECTIBLE_HEROES = 9

# Imported collectibles held back from packs/crafting: id -> (name, original set).
EXPECTED_REWARD_ONLY = {
    "EX1_062": ("Old Murk-Eye", "REWARD"),
    "NEW1_016": ("Captain's Parrot", "REWARD"),
    "EX1_112": ("Gelbin Mekkatorque", "PROMO"),
    "PRO_001": ("Elite Tauren Chieftain", "PROMO"),
}
# The collectible Naxxramas cards (acquisition "quest"): id -> name. 15 boss rewards + 6 Legendaries + 9 class cards.
EXPECTED_QUEST = {"FP1_001": "Zombie Chow", "FP1_002": "Haunted Creeper", "FP1_003": "Echoing Ooze",
                  "FP1_004": "Mad Scientist", "FP1_005": "Shade of Naxxramas", "FP1_007": "Nerubian Egg",
                  "FP1_008": "Spectral Knight", "FP1_009": "Deathlord", "FP1_010": "Maexxna", "FP1_011": "Webspinner",
                  "FP1_012": "Sludge Belcher", "FP1_013": "Kel'Thuzad", "FP1_014": "Stalagg", "FP1_015": "Feugen",
                  "FP1_016": "Wailing Soul", "FP1_017": "Nerub'ar Weblord", "FP1_018": "Duplicate",
                  "FP1_019": "Poison Seeds", "FP1_020": "Avenge", "FP1_021": "Death's Bite", "FP1_022": "Voidcaller",
                  "FP1_023": "Dark Cultist", "FP1_024": "Unstable Ghoul", "FP1_025": "Reincarnate",
                  "FP1_026": "Anub'ar Ambusher", "FP1_027": "Stoneskin Gargoyle", "FP1_028": "Undertaker",
                  "FP1_029": "Dancing Swords", "FP1_030": "Loatheb", "FP1_031": "Baron Rivendare"}
# Entities of the NAXX set in the pinned build (all kept): 30 collectible + 131 encounter-only runtime entities.
EXPECTED_NAXX_ENTITIES = 161
# Encounter entities the Naxxramas boss decks use that are not collectible (all NAXX set).
EXPECTED_BOSS_DECK_ONLY = ["FP1_006", "NAXM_001", "NAXM_002"]

# Non-collectible entities pulled in only because a reward-only card references them.
EXPECTED_REWARD_DEPENDENCIES = ["Mekka1", "Mekka2", "Mekka3", "Mekka4", "Mekka4t", "PRO_001a", "PRO_001at",
                                "PRO_001b", "PRO_001c"]
# 604 CORE/EXPERT1 - 2 removed (NEW1_006, EX1_016t) + 4 reward-only + 9 dependencies + 161 NAXX
EXPECTED_ENTITY_COUNT = 776
