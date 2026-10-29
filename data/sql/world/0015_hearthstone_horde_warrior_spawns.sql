-- Horde Warrior teacher and standing players (templates in world migration 0013).
-- Owner-approved Orgrimmar placements captured from tw_world on port 3307 on 2026-10-04.
-- 919507: teacher, original local guid 12585206.
-- 919508: thinking player, original local guid 12585204.
-- 919509: waiting player, original local guid 12585205.
-- Preserve positions, orientations and stationary movement.
-- Allocate guids locally to avoid collisions. Reruns leave one spawn per entry.

DELETE FROM `creature` WHERE `id` IN (919507, 919508, 919509);
INSERT INTO `creature`
  (`id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`,
   `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`, `movement_type`)
VALUES
  (919507, 1, 1704.729980469, -4270.220214844, 33.154201508, 0.595587015, 120, 120, 0, 0),
  (919508, 1, 1706.520019531, -4266.419921875, 33.101299286, 4.911349773, 120, 120, 0, 0),
  (919509, 1, 1707.540039063, -4270.520019531, 33.130599976, 1.832589984, 120, 120, 0, 0);
