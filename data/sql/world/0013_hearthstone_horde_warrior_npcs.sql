-- Horde Warrior unlock: Victory Above All, creature templates (quests in 0016).
-- Teacher name supplied by the owner. Scene actor names describe their roles.
-- 919507: teacher, display 9268, gossip and quest giver flags.
-- 919508 and 919509: standing scene actors, Orgrimmar Grunt display 4259.
-- All three copy the Hearthstone Master level 60 baseline with Orgrimmar faction 85.
-- No creature scripts. Quest relations and deck bindings are in 0016.
-- Owner placement is recorded in world migration 0015.

DELETE FROM `creature_template` WHERE `entry` IN (919507, 919508, 919509);
INSERT INTO `creature_template`
  (`entry`, `display_id1`, `name`, `subname`, `level_min`, `level_max`, `health_min`, `health_max`,
   `armor`, `faction`, `npc_flags`, `speed_walk`, `speed_run`, `detection_range`, `call_for_help_range`,
   `dmg_min`, `dmg_max`, `attack_power`, `base_attack_time`, `ranged_attack_time`, `unit_class`,
   `unit_flags`, `type`, `regeneration`, `movement_type`, `script_name`)
VALUES
  (919507, 9268, 'Veteran Zerok', 'Hearthstone enthusiast', 60, 60, 4500, 4500,
   3500, 85, 3, 1, 1.14286, 18, 5,
   100, 130, 250, 2000, 2000, 1,
   4608, 7, 3, 0, ''),
  (919508, 4259, 'Thoughtful Grunt', 'Hearthstone player', 60, 60, 4500, 4500,
   3500, 85, 0, 1, 1.14286, 18, 5,
   100, 130, 250, 2000, 2000, 1,
   4608, 7, 3, 0, ''),
  (919509, 4259, 'Impatient Grunt', 'Hearthstone player', 60, 60, 4500, 4500,
   3500, 85, 0, 1, 1.14286, 18, 5,
   100, 130, 250, 2000, 2000, 1,
   4608, 7, 3, 0, '');
