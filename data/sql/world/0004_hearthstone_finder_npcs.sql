-- mod-hearthstone: the module own match-finder NPCs.
--   919501 Ezra Chatterton (Tauren, Horde, display 1624)
--   919502 Ibelin Redmoore (Human, Alliance, display 3338)
-- Plain level 60 humanoids with the gossip, quest giver and vendor flags (7): no inn, trainer, gossip menu or AI.
-- The module adds the Find me a Hearthstone opponent option because both are hearthstone_finder rows. The vendor
-- window sells the booster pack items of migration 0006 (for gold and for Hearthstone Coins).
-- They give and end the introduction quests (world migration 0008). The module awards the starter deck and the
-- free booster pack at the turn-ins from the quest packet (HsIntroGlue.cpp). No script_name: a module creature
-- script would break the vendor option and the quest marker.
-- Columns not listed keep their defaults. The templates are rebuilt each time this file runs.
-- The spawns are in world migration 0007.
--
-- The file contains no backslashes, no semicolons and no apostrophes inside comments or strings.

DELETE FROM `creature_template` WHERE `entry` IN (919501, 919502);

INSERT INTO `creature_template`
  (`entry`, `display_id1`, `name`, `subname`, `level_min`, `level_max`, `health_min`, `health_max`,
   `armor`, `faction`, `npc_flags`, `speed_walk`, `speed_run`, `detection_range`, `call_for_help_range`,
   `dmg_min`, `dmg_max`, `attack_power`, `base_attack_time`, `ranged_attack_time`, `unit_class`,
   `unit_flags`, `type`, `regeneration`, `script_name`)
VALUES
  (919501, 1624, 'Ezra Chatterton', 'Hearthstone Master', 60, 60, 4500, 4500,
   3500, 29, 7, 1, 1.14286, 18, 5,
   100, 130, 250, 2000, 2000, 1,
   4608, 7, 3, ''),
  (919502, 3338, 'Ibelin Redmoore', 'Hearthstone Master', 60, 60, 4500, 4500,
   3500, 12, 7, 1, 1.14286, 18, 5,
   100, 130, 250, 2000, 2000, 1,
   4608, 7, 3, '');

INSERT IGNORE INTO `hearthstone_finder` (`creature_entry`) VALUES (919501), (919502);
