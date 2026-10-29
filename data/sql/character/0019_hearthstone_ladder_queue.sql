-- mod-hearthstone: the ladder work that waits for the running round is stored, not only
-- kept in memory. Replaces hearthstone_ladder_pending of 0017 (its rows are copied in order, then the table is dropped).
-- The core AutoUpdater identifies a migration by the SHA1 of its bytes: never edit this file after it was applied
-- anywhere, add 0020_... instead. It contains no backslashes and no semicolons inside comments or strings.
--
-- The queue holds every item the ladder has not applied or played yet, oldest first (seq). It is rewritten in the
-- transaction of every change that matters (a save of a round or a player result, a queued result, a GM round), so after
-- a server stop the rows that are left are played or applied first, in the same order.
--   kind           0 = an NPC round (excluded_npc sits out), 1 = a player result against npc_id.
--   account_id,    the account, the character name and the NPC of a player result. result is 0 win, 1 draw, 2 loss.
--   player_name
--   duel_id        the ladder duel the result settles (deleted in the same transaction), 0 for a GM result.

CREATE TABLE IF NOT EXISTS `hearthstone_ladder_queue` (
  `seq` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `kind` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `excluded_npc` BIGINT UNSIGNED NOT NULL DEFAULT 0,
  `account_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `player_name` VARCHAR(48) NOT NULL DEFAULT '',
  `npc_id` BIGINT UNSIGNED NOT NULL DEFAULT 0,
  `result` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `duel_id` BIGINT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`seq`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-hearthstone: World NPC Ladder work waiting for the running round';

-- The rounds of 0017 move over unchanged, in their order.
INSERT INTO `hearthstone_ladder_queue` (`kind`, `excluded_npc`) SELECT 0, `excluded_npc` FROM `hearthstone_ladder_pending` ORDER BY `seq`;

DROP TABLE IF EXISTS `hearthstone_ladder_pending`;
