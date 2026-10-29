-- Owner requested recording the placed Warlock actors, 2026-10-08.
-- Captured read-only from tw_world:3307, with coordinates read as DOUBLE.
-- Varka Ashhand 919559: original local GUID 12585241.
-- Skrit 919560: original local GUID 12585243.
-- Maren Blackquill 919561: original local GUID 12585244.
-- Nib 919562: original local GUID 12585245.
-- Owen Wellwick 919563: original local GUID 12585246.
-- GUIDs above are provenance only; each database allocates its own GUIDs.
-- Preserve every non-GUID spawn field and the full stored coordinate precision.
-- No addons, GUID/entry waypoints, groups or links are attached to these spawns.
-- Requires templates from 0032/0033. Reruns replace only these five actor entries;
-- existing city NPCs and unspawned objective credits remain untouched.
DELETE FROM `creature` WHERE `id` IN (919559,919560,919561,919562,919563);
INSERT INTO `creature`
  (`id`,`id2`,`id3`,`id4`,`map`,
   `position_x`,`position_y`,`position_z`,`orientation`,
   `spawntimesecsmin`,`spawntimesecsmax`,`wander_distance`,
   `health_percent`,`mana_percent`,`movement_type`,`spawn_flags`,`visibility_mod`)
VALUES
  (919559,0,0,0,1,1855.1800537109375,-4354.330078125,-14.996199607849121,3.275049924850464,
   120,120,0,100,100,0,0,0),
  (919560,0,0,0,1,1800.280029296875,-4376.64013671875,-17.64069938659668,1.4914100170135498,
   120,120,0,100,100,0,0,0),
  (919561,0,0,0,0,-8969.919921875,1037.489990234375,101.40399932861328,2.215169906616211,
   120,120,0,100,100,0,0,0),
  (919562,0,0,0,0,-8971.58984375,1040,101.40399932861328,5.364620208740234,
   120,120,0,100,100,0,0,0),
  (919563,0,0,0,0,-8950.9599609375,994.7780151367188,122.0260009765625,3.7663400173187256,
   120,120,0,100,100,0,0,0);
