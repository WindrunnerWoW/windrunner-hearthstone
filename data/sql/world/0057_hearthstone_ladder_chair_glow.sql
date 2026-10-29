-- mod-hearthstone: the trigger creature that carries the glow spell of the ladder chair
-- (Hearthstone.Ladder.ChairGlowSpellId, src/HsLadderTable.cpp).
--
--   Creature template 919101 (Ladder Chair Glow): a copy of Invisible Stalker 15214 with the flag
--   CREATURE_FLAG_EXTRA_INVISIBLE (flags_extra bit 128) cleared. The core hides a creature with that flag from every
--   player who is not a game master, so the stalker itself shows its spell to game masters only. The model of the
--   stalker is invisible already, so players see nothing but the spell. unit_flags stay as copied (not attackable,
--   not selectable).
--
-- Rebuilt each run, only entry 919101 is touched. The file has no backslashes and no semicolons inside comments or
-- strings.

DELETE FROM `creature_template` WHERE `entry` = 919101;

DROP TEMPORARY TABLE IF EXISTS `hs_chair_glow_template`;
CREATE TEMPORARY TABLE `hs_chair_glow_template` AS SELECT * FROM `creature_template` WHERE `entry` = 15214;
UPDATE `hs_chair_glow_template` SET `entry` = 919101, `name` = 'Ladder Chair Glow', `script_name` = '', `flags_extra` = `flags_extra` & ~128;
INSERT INTO `creature_template` SELECT * FROM `hs_chair_glow_template`;
DROP TEMPORARY TABLE `hs_chair_glow_template`;
