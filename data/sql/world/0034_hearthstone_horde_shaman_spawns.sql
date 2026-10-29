-- Owner requested recording Annie's placed spawns and four waypoints, 2026-10-08.
-- Captured read-only from tw_world:3307, with coordinates read as DOUBLE.
-- Living Annie 919552: original local GUID 12585236.
-- Spirit of Annie 919553: original local GUID 12585235.
-- GUIDs above are provenance only; let each database allocate its own GUIDs.
-- Preserve every non-GUID spawn field. No spawn addons or GUID paths are attached.
-- Both actors remain stationary. Only the personal farewell actor follows the
-- native entry route after Something I Can Give is rewarded.
-- Requires the templates from 0031. Reruns replace only these two actor entries
-- and Annie's entry route; existing trainers and unspawned credits stay untouched.
DELETE FROM `creature` WHERE `id` IN (919552,919553);
INSERT INTO `creature`
  (`id`,`id2`,`id3`,`id4`,`map`,
   `position_x`,`position_y`,`position_z`,`orientation`,
   `spawntimesecsmin`,`spawntimesecsmax`,`wander_distance`,
   `health_percent`,`mana_percent`,`movement_type`,`spawn_flags`,`visibility_mod`)
VALUES
  (919552,0,0,0,1,-1102.2900390625,117.27799987792969,152.76499938964844,0.9399710297584534,
   120,120,0,100,100,0,0,0),
  (919553,0,0,0,1,-942.6309814453125,283.875,135.74600219726562,1.082129955291748,
   120,120,0,100,100,0,0,0);

DELETE FROM `creature_movement_template` WHERE `entry`=919552;
INSERT INTO `creature_movement_template`
  (`entry`,`point`,`position_x`,`position_y`,`position_z`,`orientation`,
   `waittime`,`wander_distance`,`script_id`)
VALUES
  (919552,1,-1106.3184814453125,114.64447784423828,152.65628051757812,100,0,0,0),
  (919552,2,-1109.6396484375,105.42436981201172,151.70179748535156,100,0,0,0),
  (919552,3,-1116.23828125,106.95344543457031,151.70179748535156,100,0,0,0),
  (919552,4,-1122.35009765625,90.43563842773438,143.4742431640625,100,0,0,0);
