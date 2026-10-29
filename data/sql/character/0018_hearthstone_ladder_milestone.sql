-- mod-hearthstone: the one-time milestones of the World NPC Ladder, per account. Additive only: the
-- statement is CREATE TABLE IF NOT EXISTS, nothing is dropped or altered.
-- The core AutoUpdater identifies a migration by the SHA1 of its bytes: never edit this file after it was
-- applied anywhere, add 0019_... instead. It contains no backslashes and no semicolons inside comments or strings.
--
-- milestone is legend (the account reached Legend) or legend1 (it was the number one of Legend). A row is written once
-- with INSERT IGNORE, so dropping out of Legend and coming back never repeats it. There is no reward yet: the rows are the
-- record that a later reward claim can be built on.
CREATE TABLE IF NOT EXISTS `hearthstone_ladder_milestone` (
  `account_id` INT UNSIGNED NOT NULL,
  `milestone` VARCHAR(16) NOT NULL,
  `reached_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`account_id`, `milestone`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-hearthstone: World NPC Ladder milestones per account';
