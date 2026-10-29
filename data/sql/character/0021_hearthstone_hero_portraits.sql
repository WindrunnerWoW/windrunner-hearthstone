-- mod-hearthstone: alternative hero portraits (reward track). Additive only: CREATE TABLE IF NOT EXISTS,
-- nothing is dropped or altered.
-- The core AutoUpdater identifies a migration by the SHA1 of its bytes: never edit this file after it was
-- applied anywhere, add 0022_... instead. It contains no backslashes and no semicolons inside comments or strings.
--
-- One credit per account and league, written when the account is the number one of Legend (INSERT IGNORE, so a
-- replay adds nothing). HERO_UNLOCK claims the oldest unclaimed credit: portrait_id and claim_key are set together
-- with the hearthstone_hero_portrait row in one transaction. claim_key is the idempotency key of the request, and
-- its unique index makes a replayed key impossible to claim twice.
CREATE TABLE IF NOT EXISTS `hearthstone_hero_credit` (
  `account_id` INT UNSIGNED NOT NULL,
  `league` INT UNSIGNED NOT NULL,
  `earned_at` INT UNSIGNED NOT NULL DEFAULT 0,
  `portrait_id` VARCHAR(32) DEFAULT NULL,
  `claim_key` VARCHAR(16) DEFAULT NULL,
  PRIMARY KEY (`account_id`, `league`),
  UNIQUE KEY `claim` (`account_id`, `claim_key`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-hearthstone: Legend number one credits per account and league';

-- The unlocked non-default portraits of an account. source is legend1 (a claimed credit) or gm.
CREATE TABLE IF NOT EXISTS `hearthstone_hero_portrait` (
  `account_id` INT UNSIGNED NOT NULL,
  `portrait_id` VARCHAR(32) NOT NULL,
  `source` VARCHAR(8) NOT NULL DEFAULT 'legend1',
  `unlocked_at` INT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`account_id`, `portrait_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-hearthstone: unlocked hero portraits per account';

-- The selected portrait per account and class. No row means the class default.
CREATE TABLE IF NOT EXISTS `hearthstone_hero_choice` (
  `account_id` INT UNSIGNED NOT NULL,
  `class` VARCHAR(16) NOT NULL,
  `portrait_id` VARCHAR(32) NOT NULL,
  PRIMARY KEY (`account_id`, `class`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-hearthstone: selected hero portrait per account and class';
