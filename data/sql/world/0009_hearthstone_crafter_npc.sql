-- mod-hearthstone: the crafting NPC.
--   919505 Byron "Reckful" Bernstein, subtitle Card Master (Human, neutral, custom display 21698 with Rogue T2)
-- A plain level 60 humanoid with the gossip and quest giver flags (npc_flags 3): no vendor window, no match-finder
-- row. He ends the introduction quest 4 and gives and ends quest 5 (world migration 0008). The Craft and scrap
-- cards option comes from the hearthstone_vendor row below (crafting = 1, packs = 0) and is only shown to accounts
-- that turned in quest 5 (HsEconomyGlue.cpp).
-- Faction 35 (Friendly) so that both factions can talk to him. No script_name: a module creature script would
-- break the core gossip handling (see world migration 0004).
-- Ezra and Ibelin lose their crafting option (UPDATE at the end).
-- The template is rebuilt each time this file runs. The owner places the spawn (.npc add 919505), the display_id
-- can be changed here later.
--
-- The file contains no backslashes and no semicolons inside comments or strings.

DELETE FROM `creature_template` WHERE `entry` = 919505;

INSERT INTO `creature_template`
  (`entry`, `display_id1`, `name`, `subname`, `level_min`, `level_max`, `health_min`, `health_max`,
   `armor`, `faction`, `npc_flags`, `speed_walk`, `speed_run`, `detection_range`, `call_for_help_range`,
   `dmg_min`, `dmg_max`, `attack_power`, `base_attack_time`, `ranged_attack_time`, `unit_class`,
   `unit_flags`, `type`, `regeneration`, `script_name`)
VALUES
  (919505, 21698, 'Byron "Reckful" Bernstein', 'Card Master', 60, 60, 4500, 4500,
   3500, 35, 3, 1, 1.14286, 18, 5,
   100, 130, 250, 2000, 2000, 1,
   4608, 7, 3, '');

-- Custom display 21698 (CreatureDisplayInfoExtra 15992, Bloodfang). The server refuses to use a display that has no
-- creature_display_info_addon row and falls back to display 13, so the row is required.
INSERT INTO `creature_display_info_addon` (`display_id`, `bounding_radius`, `combat_reach`, `gender`, `display_id_other_gender`)
VALUES (21698, 0.306, 1.5, 0, 0)
ON DUPLICATE KEY UPDATE `bounding_radius` = VALUES(`bounding_radius`), `combat_reach` = VALUES(`combat_reach`),
  `gender` = VALUES(`gender`), `display_id_other_gender` = VALUES(`display_id_other_gender`);

INSERT IGNORE INTO `hearthstone_vendor` (`creature_entry`, `packs`, `crafting`) VALUES (919505, 0, 1);

-- Only Byron crafts: the Hearthstone Masters were seeded as crafting NPCs for testing (migration 0005).
UPDATE `hearthstone_vendor` SET `crafting` = 0 WHERE `creature_entry` IN (919501, 919502);
