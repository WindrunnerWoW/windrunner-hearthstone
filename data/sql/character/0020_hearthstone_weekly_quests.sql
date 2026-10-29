-- Acceptance snapshots for durable arena / ladder duels. A replay cannot gain newly accepted quests.
-- The high bit of durable_id distinguishes ladder duels from arena runs, like MatchStartParams.
CREATE TABLE IF NOT EXISTS `hearthstone_weekly_match` (
  `durable_id` BIGINT UNSIGNED NOT NULL,
  `match_no` INT UNSIGNED NOT NULL,
  `guid` INT UNSIGNED NOT NULL,
  `quest_mask` INT UNSIGNED NOT NULL DEFAULT 0,
  `consumed` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`durable_id`, `match_no`),
  KEY `guid` (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

-- Scoped to the current acceptance of 919991, removed on abandon or reward.
CREATE TABLE IF NOT EXISTS `hearthstone_weekly_streak` (
  `guid` INT UNSIGNED NOT NULL,
  `wins` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

-- Progress below one objective unit (damage under ten), carried to the next match. Scoped to the current
-- acceptance of the quest, removed on abandon or reward.
CREATE TABLE IF NOT EXISTS `hearthstone_weekly_carry` (
  `guid` INT UNSIGNED NOT NULL,
  `quest` INT UNSIGNED NOT NULL,
  `amount` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`guid`, `quest`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
