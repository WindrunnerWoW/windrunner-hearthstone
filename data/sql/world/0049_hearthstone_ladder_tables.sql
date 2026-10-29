-- mod-hearthstone: the owner data and world objects of the World NPC Ladder chair
--.
--
--   Gameobject template 919100 (Ladder Chair): a copy of Wooden Chair 101766 (type 7, same model, size, sitting height
--   and every data field) with script_name hearthstone_ladder_chair. The name is Ladder Chair on purpose: the
--   client shows it as the mouse-over name, which tells the player what the chair is for. The ordinary Wooden
--   Chairs keep the name Wooden Chair. Change the name in this file if the owner prefers the plain name.
--   Spawns: the two player chairs, guid 7003005 (Darnassus) and 7003007 (Thunder Bluff), change from entry 101766 to
--   919100 and keep guid, position, rotation and every other spawn field. The values were read from tw_world on
--   2026-10-14 (TCP port 3307, SELECT only) at full stored float precision. The opponent chairs 7003006 and 7003008 stay
--   Wooden Chairs: they are only inserted with INSERT IGNORE, so a database that holds them is not touched.
--
--   hearthstone_ladder_table / hearthstone_ladder_path: per table the chairs and the opponent's walk (owner .gps, map
--   1). Rebuilt by this file for table guids 7003001 and 7003002 only (other rows are kept). The opponent appears at
--   spawn, walks through the path points in point_no order to stop and sits on the opponent chair.
--   hearthstone_ladder_line: what an NPC says after the match, kind from the NPC point of view. DRAFTS FOR OWNER
--   REVIEW: written in the tone of the ladder background (a peace that does not work). Rows are inserted with INSERT
--   IGNORE by id, so edits in the table stay. To replace a draft, edit the row or delete it and add your own.
--   hearthstone_ladder_block: roster NPCs (hearthstone_finder_npc) that are never offered. Seeded with INSERT IGNORE for
--   the roster NPCs with a boss rank or a script AI of their own. They still take part in the simulated ladder.
--   Delete a row to allow the NPC, add rows for more.
--
-- Rebuilt each run, only the ids named below are touched. The file has no backslashes and no semicolons inside
-- comments or strings.

-- The ladder chair template, copied from Wooden Chair 101766.
DELETE FROM `gameobject_template` WHERE `entry` = 919100;
INSERT INTO `gameobject_template`
  (`entry`, `type`, `displayId`, `name`, `faction`, `flags`, `size`,
   `data0`, `data1`, `data2`, `data3`, `data4`, `data5`, `data6`, `data7`, `data8`, `data9`, `data10`, `data11`,
   `data12`, `data13`, `data14`, `data15`, `data16`, `data17`, `data18`, `data19`, `data20`, `data21`, `data22`, `data23`,
   `mingold`, `maxgold`, `phase_quest_id`, `script_name`)
SELECT
  919100, `type`, `displayId`, 'Ladder Chair', `faction`, `flags`, `size`,
  `data0`, `data1`, `data2`, `data3`, `data4`, `data5`, `data6`, `data7`, `data8`, `data9`, `data10`, `data11`,
  `data12`, `data13`, `data14`, `data15`, `data16`, `data17`, `data18`, `data19`, `data20`, `data21`, `data22`, `data23`,
  `mingold`, `maxgold`, `phase_quest_id`, 'hearthstone_ladder_chair'
FROM `gameobject_template` WHERE `entry` = 101766;

-- The player chairs (west of each table). Only the entry changes.
DELETE FROM `gameobject` WHERE `guid` IN (7003005, 7003007);
INSERT INTO `gameobject`
  (`guid`, `id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`,
   `rotation0`, `rotation1`, `rotation2`, `rotation3`,
   `spawntimesecsmin`, `spawntimesecsmax`, `animprogress`, `state`, `spawn_flags`, `visibility_mod`)
VALUES
  (7003005, 919100, 1, 9817.51953125, 2349.889892578125, 1321.6800537109375, 0.4203599989414215,
   0, 0, 0.20863600075244904, 0.9779930114746094,
   300, 300, 100, 1, 0, 0),
  (7003007, 919100, 1, -1226.030029296875, 152.63600158691406, 133.27000427246094, 6.217050075531006,
   0, 0, 0.033059898763895035, -0.9994530081748962,
   300, 300, 100, 1, 0, 0);

-- The opponent chairs (east of each table): ordinary Wooden Chairs, left alone when they exist.
INSERT IGNORE INTO `gameobject`
  (`guid`, `id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`,
   `rotation0`, `rotation1`, `rotation2`, `rotation3`,
   `spawntimesecsmin`, `spawntimesecsmax`, `animprogress`, `state`, `spawn_flags`, `visibility_mod`)
VALUES
  (7003006, 101766, 1, 9820.7900390625, 2351.219970703125, 1321.6800537109375, 3.5148301124572754,
   0, 0, 0.9826369881629944, -0.18553699553012848,
   300, 300, 100, 1, 0, 0),
  (7003008, 101766, 1, -1222.510009765625, 152.44500732421875, 133.11599731445312, 3.0558300018310547,
   0, 0, 0.999081015586853, 0.042868100106716156,
   300, 300, 100, 1, 0, 0);

-- Per table: the gameobjects and the opponent's route (coordinates as DOUBLE: the owner values keep all their digits).
--   table_guid, chair_guid, opponent_chair_guid  gameobject guids. The player sits on chair_guid.
--   spawn_o       facing of the opponent at the spawn point. stop_x, stop_y, stop_z: where it ends up, beside its chair.
CREATE TABLE IF NOT EXISTS `hearthstone_ladder_table` (
  `table_guid` INT UNSIGNED NOT NULL,
  `city` VARCHAR(32) NOT NULL,
  `map_id` SMALLINT UNSIGNED NOT NULL,
  `chair_guid` INT UNSIGNED NOT NULL,
  `opponent_chair_guid` INT UNSIGNED NOT NULL,
  `spawn_x` DOUBLE NOT NULL DEFAULT 0,
  `spawn_y` DOUBLE NOT NULL DEFAULT 0,
  `spawn_z` DOUBLE NOT NULL DEFAULT 0,
  `spawn_o` DOUBLE NOT NULL DEFAULT 0,
  `stop_x` DOUBLE NOT NULL DEFAULT 0,
  `stop_y` DOUBLE NOT NULL DEFAULT 0,
  `stop_z` DOUBLE NOT NULL DEFAULT 0,
  PRIMARY KEY (`table_guid`),
  UNIQUE KEY `idx_chair` (`chair_guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='World NPC Ladder tables of mod-hearthstone (owner data)';

-- The points between spawn and stop of a table, in point_no order (none: the opponent walks straight).
CREATE TABLE IF NOT EXISTS `hearthstone_ladder_path` (
  `table_guid` INT UNSIGNED NOT NULL,
  `point_no` TINYINT UNSIGNED NOT NULL,
  `x` DOUBLE NOT NULL DEFAULT 0,
  `y` DOUBLE NOT NULL DEFAULT 0,
  `z` DOUBLE NOT NULL DEFAULT 0,
  PRIMARY KEY (`table_guid`, `point_no`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='World NPC Ladder opponent path points of mod-hearthstone (owner data)';

-- Thunder Bluff 7003001 (chair 7003007, opponent chair 7003008) and Darnassus 7003002 (chair 7003005, opponent chair
-- 7003006). Owner .gps 2026-10-14, map 1.
DELETE FROM `hearthstone_ladder_path` WHERE `table_guid` IN (7003001, 7003002);
DELETE FROM `hearthstone_ladder_table` WHERE `table_guid` IN (7003001, 7003002);
INSERT INTO `hearthstone_ladder_table`
  (`table_guid`, `city`, `map_id`, `chair_guid`, `opponent_chair_guid`,
   `spawn_x`, `spawn_y`, `spawn_z`, `spawn_o`, `stop_x`, `stop_y`, `stop_z`)
VALUES
  (7003001, 'Thunder Bluff', 1, 7003007, 7003008,
   -1224.166748, 144.446808, 133.451202, 1.465406, -1222.630859, 151.279099, 133.160339),
  (7003002, 'Darnassus', 1, 7003005, 7003006,
   9821.464844, 2341.206787, 1321.681885, 1.997904, 9821.241211, 2350.254639, 1321.681396);
INSERT INTO `hearthstone_ladder_path` (`table_guid`, `point_no`, `x`, `y`, `z`)
VALUES
  (7003002, 1, 9819.243164, 2346.087158, 1321.681885);

-- What an NPC says after the match. kind is from the NPC point of view: win = the NPC won, loss = the NPC lost.
CREATE TABLE IF NOT EXISTS `hearthstone_ladder_line` (
  `id` INT UNSIGNED NOT NULL,
  `kind` ENUM('win','loss','draw') NOT NULL,
  `text` VARCHAR(255) NOT NULL,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='World NPC Ladder lines of mod-hearthstone (owner data)';

INSERT IGNORE INTO `hearthstone_ladder_line` (`id`, `kind`, `text`)
VALUES
  (1, 'win', "Better luck next time. Mind your mana curve, friend."),
  (2, 'win', "A fine game, and I was a little finer. Do come back."),
  (3, 'win', "Hah! That is how a deck should be played."),
  (4, 'win', "A close one. You made me work for it, I will give you that."),
  (5, 'win', "No hard feelings. The peace holds for another day."),
  (6, 'win', "Was that your best? I would hate to see your worst."),
  (7, 'loss', "Luck. Nothing but luck. Sit down again and we will see who is lucky next time."),
  (8, 'loss', "I want that deck inspected. Nobody draws like that honestly."),
  (9, 'loss', "Hmph. One game proves nothing about my people."),
  (10, 'loss', "Well played. I mean it, though it pains me to say so."),
  (11, 'loss', "Enjoy it while it lasts. My people never forget a defeat."),
  (12, 'loss', "The cards were against me today, and I suspect the committee had a hand in it."),
  (13, 'draw', "A draw. Neither of us can claim the table today."),
  (14, 'draw', "Nobody wins, nobody loses. The committee will be so disappointed."),
  (15, 'draw', "A stalemate. We will have to settle this another day.");

-- Roster NPCs that are never offered (they stay on the ladder and play their simulated games).
CREATE TABLE IF NOT EXISTS `hearthstone_ladder_block` (
  `creature_entry` INT UNSIGNED NOT NULL,
  `reason` VARCHAR(120) NOT NULL DEFAULT '',
  PRIMARY KEY (`creature_entry`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='World NPC Ladder NPCs kept out of the offers of mod-hearthstone (owner data)';

INSERT IGNORE INTO `hearthstone_ladder_block` (`creature_entry`, `reason`)
VALUES
  (6177, 'Narm Faulk: script AI lays the NPC down as a corpse until healed'),
  (2784, 'King Magni Bronzebeard: boss rank, boss script with a combat spell rotation'),
  (3057, 'Cairne Bloodhoof: boss rank, own gossip script'),
  (7937, 'High Tinker Mekkatorque: boss rank, own gossip script'),
  (1284, 'Archbishop Benedictus: boss rank, EventAI with combat spells'),
  (2425, 'Varimathras: boss rank, EventAI casts a spell when it spawns'),
  (4949, 'Thrall: boss rank, EventAI with a spell list'),
  (10540, "Vol'jin: boss rank, EventAI and an own script");
