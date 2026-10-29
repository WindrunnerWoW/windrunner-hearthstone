-- Alliance Shaman cast and the permanent John placement, captured 2026-10-07.
-- Existing John spawns and owner paths are preserved. No permanent visitors.
-- The scripted scene uses the route snapshot in HsAllianceShamanRoutes.h.
DELETE FROM `creature_template` WHERE `entry` IN (919545,919546,919547);
INSERT INTO `creature_template`
  (`entry`,`display_id1`,`name`,`subname`,`level_min`,`level_max`,`health_min`,`health_max`,
   `armor`,`faction`,`npc_flags`,`speed_walk`,`speed_run`,`detection_range`,`call_for_help_range`,
   `dmg_min`,`dmg_max`,`attack_power`,`base_attack_time`,`ranged_attack_time`,`unit_class`,
   `unit_flags`,`type`,`regeneration`,`movement_type`,`gossip_menu_id`,`script_name`)
SELECT c.`entry`,c.`display`,c.`name`,c.`subname`,
  b.`level_min`,b.`level_max`,b.`health_min`,b.`health_max`,b.`armor`,55,c.`flags`,
  b.`speed_walk`,b.`speed_run`,b.`detection_range`,b.`call_for_help_range`,
  b.`dmg_min`,b.`dmg_max`,b.`attack_power`,b.`base_attack_time`,b.`ranged_attack_time`,
  b.`unit_class`,b.`unit_flags` | c.`safe_flags`,b.`type`,b.`regeneration`,0,IF(c.`entry`=919545,65027,0),''
FROM `creature_template` b
CROSS JOIN (
  SELECT 919545 AS `entry`,10184 AS `display`,'John Maloneybrew' AS `name`,
    'Hearthstone researcher' AS `subname`,3 AS `flags`,0 AS `safe_flags`
  UNION ALL SELECT 919546,8929,'Kibler McGriff','Wildhammer visitor',0,2
  UNION ALL SELECT 919547,3898,'Ambassador Krip Parrianhoof','Tauren ambassador',0,2
) c
WHERE b.`entry`=919502;

-- Source GUID 12585232 is provenance only. Allocate the destination GUID locally.
-- Preserve the full stored position precision and every non-GUID spawn field.
-- Only fill a missing placement so reruns keep existing GUIDs and owner edits.
INSERT INTO `creature`
  (`id`, `id2`, `id3`, `id4`, `map`,
   `position_x`, `position_y`, `position_z`, `orientation`,
   `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`,
   `health_percent`, `mana_percent`, `movement_type`, `spawn_flags`, `visibility_mod`)
SELECT
  919545,0,0,0,0,
  -4651.2001953125,-1180.8699951171875,501.64300537109375,4.253230094909668,
  120,120,0,100,100,0,0,0
WHERE NOT EXISTS (SELECT 1 FROM `creature` WHERE `id`=919545);
