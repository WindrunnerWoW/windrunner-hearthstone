-- Horde Rogue cast preparation. Owner manually places both NPCs; no spawns.
-- Quest chain, other contacts, gossip texts and opponent registration follow later.
-- 919522: For'sen, troll display 4070, gossip and quest giver.
-- 919523: Gora, female orc display 3128, gossip contact.
-- Copy Ezra's teacher baseline with Orgrimmar faction 85 and stationary movement.
DELETE FROM `creature_template` WHERE `entry` IN (919522, 919523);
INSERT INTO `creature_template`
  (`entry`, `display_id1`, `name`, `subname`, `level_min`, `level_max`, `health_min`, `health_max`,
   `armor`, `faction`, `npc_flags`, `speed_walk`, `speed_run`, `detection_range`, `call_for_help_range`,
   `dmg_min`, `dmg_max`, `attack_power`, `base_attack_time`, `ranged_attack_time`, `unit_class`,
   `unit_flags`, `type`, `regeneration`, `movement_type`, `gossip_menu_id`, `script_name`)
SELECT 919522, 4070, 'For''sen', 'Hearthstone enthusiast',
  `level_min`, `level_max`, `health_min`, `health_max`, `armor`, 85, 3, `speed_walk`, `speed_run`,
  `detection_range`, `call_for_help_range`, `dmg_min`, `dmg_max`, `attack_power`, `base_attack_time`,
  `ranged_attack_time`, `unit_class`, `unit_flags`, `type`, `regeneration`, 0, 0, ''
FROM `creature_template` WHERE `entry` = 919501;
INSERT INTO `creature_template`
  (`entry`, `display_id1`, `name`, `subname`, `level_min`, `level_max`, `health_min`, `health_max`,
   `armor`, `faction`, `npc_flags`, `speed_walk`, `speed_run`, `detection_range`, `call_for_help_range`,
   `dmg_min`, `dmg_max`, `attack_power`, `base_attack_time`, `ranged_attack_time`, `unit_class`,
   `unit_flags`, `type`, `regeneration`, `movement_type`, `gossip_menu_id`, `script_name`)
SELECT 919523, 3128, 'Gora', 'Card courier',
  `level_min`, `level_max`, `health_min`, `health_max`, `armor`, 85, 1, `speed_walk`, `speed_run`,
  `detection_range`, `call_for_help_range`, `dmg_min`, `dmg_max`, `attack_power`, `base_attack_time`,
  `ranged_attack_time`, `unit_class`, `unit_flags`, `type`, `regeneration`, 0, 0, ''
FROM `creature_template` WHERE `entry` = 919501;
