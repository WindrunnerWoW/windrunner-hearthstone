-- mod-hearthstone: the League Committee of the World NPC Ladder.
--   920100 Amaz Chan (Human, Alliance, League Committee, Darnassus)
--   920101 The Firebat (Goblin, Horde, League Committee, Thunder Bluff)
-- Later the turn-in of the ladder breadcrumb quest from Ezra Chatterton / Ibelin Redmoore (not written yet: its unlock
-- condition is decided with ranked play, 14D). For now plain level 60 humanoids with a gossip menu only (npc_flags 1):
-- a greeting and one question each.
-- Looks chosen by the owner (2026-10-14): Amaz Chan display 1436, The Firebat display 7192.
-- Gossip lives entirely in the database (gossip_menu, gossip_menu_option, npc_text, broadcast_text), no module code.
-- Spawns: world migration 0048.
--
-- Menus: 65270 / 65271 greeting of Amaz Chan / The Firebat, 65272 / 65273 the answers.
-- Texts: npc_text and broadcast_text 9195920 to 9195923. Rebuilt each time this file runs, only these ids are touched.
-- The file contains no backslashes and no semicolons inside comments or strings.

DELETE FROM `creature_template` WHERE `entry` IN (920100, 920101);

INSERT INTO `creature_template`
  (`entry`, `display_id1`, `name`, `subname`, `level_min`, `level_max`, `health_min`, `health_max`,
   `armor`, `faction`, `npc_flags`, `speed_walk`, `speed_run`, `detection_range`, `call_for_help_range`,
   `dmg_min`, `dmg_max`, `attack_power`, `base_attack_time`, `ranged_attack_time`, `unit_class`,
   `unit_flags`, `type`, `regeneration`, `gossip_menu_id`, `script_name`)
VALUES
  (920100, 1436, 'Amaz Chan', 'League Committee', 60, 60, 4500, 4500,
   3500, 12, 1, 1, 1.14286, 18, 5,
   100, 130, 250, 2000, 2000, 1,
   4608, 7, 3, 65270, '');

INSERT INTO `creature_template`
  (`entry`, `display_id1`, `name`, `subname`, `level_min`, `level_max`, `health_min`, `health_max`,
   `armor`, `faction`, `npc_flags`, `speed_walk`, `speed_run`, `detection_range`, `call_for_help_range`,
   `dmg_min`, `dmg_max`, `attack_power`, `base_attack_time`, `ranged_attack_time`, `unit_class`,
   `unit_flags`, `type`, `regeneration`, `gossip_menu_id`, `script_name`)
VALUES
  (920101, 7192, '"The Firebat"', 'League Committee', 60, 60, 4500, 4500,
   3500, 29, 1, 1, 1.14286, 18, 5,
   100, 130, 250, 2000, 2000, 1,
   4608, 7, 3, 65271, '');

DELETE FROM `broadcast_text` WHERE `entry` IN (9195920, 9195921, 9195922, 9195923);
INSERT INTO `broadcast_text` (`entry`, `male_text`, `female_text`) VALUES
  (9195920,
   "Welcome, $N. The League Committee keeps the tables honest and the standings fair. Mostly honest. Fairly fair.$B$BOut here the cards do the talking.",
   "Welcome, $N. The League Committee keeps the tables honest and the standings fair. Mostly honest. Fairly fair.$B$BOut here the cards do the talking."),
  (9195921,
   "Before the Committee I founded my own order, the Archons. We played the finest tables from Stormwind to Booty Bay, and I sat at the head of every one of them. Some say I was the greatest player of my day. I say that too.$B$BAnd Amaz? Short for amazing, of course. Why, what did you think it stood for?",
   "Before the Committee I founded my own order, the Archons. We played the finest tables from Stormwind to Booty Bay, and I sat at the head of every one of them. Some say I was the greatest player of my day. I say that too.$B$BAnd Amaz? Short for amazing, of course. Why, what did you think it stood for?"),
  (9195922,
   "The Firebat from the League Committee. I watch the tables, I count the stars, and I make sure nobody palms an extra card.$B$BWhat do you want?",
   "The Firebat from the League Committee. I watch the tables, I count the stars, and I make sure nobody palms an extra card.$B$BWhat do you want?"),
  (9195923,
   "No comment.$B$BBut believe me, it had to do with fire and a lot of bats. And a lot of fire.",
   "No comment.$B$BBut believe me, it had to do with fire and a lot of bats. And a lot of fire.");

DELETE FROM `npc_text` WHERE `ID` IN (9195920, 9195921, 9195922, 9195923);
INSERT INTO `npc_text` (`ID`, `BroadcastTextID0`, `Probability0`) VALUES
  (9195920, 9195920, 1), (9195921, 9195921, 1), (9195922, 9195922, 1), (9195923, 9195923, 1);

DELETE FROM `gossip_menu` WHERE `entry` IN (65270, 65271, 65272, 65273);
INSERT INTO `gossip_menu` (`entry`, `text_id`) VALUES
  (65270, 9195920), (65271, 9195922), (65272, 9195921), (65273, 9195923);

-- option_id 1 / npc_option_npcflag 1: a plain gossip option that opens action_menu_id.
DELETE FROM `gossip_menu_option` WHERE `menu_id` IN (65270, 65271);
INSERT INTO `gossip_menu_option`
  (`menu_id`, `id`, `option_icon`, `option_text`, `option_id`, `npc_option_npcflag`, `action_menu_id`)
VALUES
  (65270, 0, 0, 'Who are you? Is your name really Amaz?', 1, 1, 65272),
  (65271, 0, 0, 'Why are you called The Firebat?', 1, 1, 65273);
