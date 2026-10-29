-- mod-hearthstone: booster packs (PHASE_05).
-- Additive only: every statement is CREATE TABLE IF NOT EXISTS, nothing is dropped or altered.
-- Keyed by account id in the character database, like 0001_hearthstone_collection.sql.
-- The core AutoUpdater identifies a migration by the SHA1 of its bytes: never edit this file
-- after it was applied anywhere; add 0003_... instead. It contains no backslashes.

-- Unopened packs and the Legendary bad-luck counter per account and pack type
-- (only 'classic', the combined Basic + Classic booster, exists).
CREATE TABLE IF NOT EXISTS `hearthstone_account_pack` (
  `account_id` INT UNSIGNED NOT NULL,
  `pack_type` VARCHAR(16) NOT NULL,
  `stock` INT UNSIGNED NOT NULL DEFAULT 0,
  `opened` INT UNSIGNED NOT NULL DEFAULT 0,
  `since_legendary` INT UNSIGNED NOT NULL DEFAULT 0,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`account_id`, `pack_type`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-hearthstone: unopened packs per account';

-- Every opening, kept forever: the recovery record of a lost reply and the audit trail.
-- seq numbers the openings of an account and pack type (1, 2, ...). op_key is the client's
-- idempotency key: a retry with the same key returns this row instead of opening again.
-- cards holds the five dataset card ids in slot order, comma separated; new_mask five 0/1
-- digits (1 = the account owned no copy before).
CREATE TABLE IF NOT EXISTS `hearthstone_pack_opening` (
  `account_id` INT UNSIGNED NOT NULL,
  `pack_type` VARCHAR(16) NOT NULL,
  `seq` INT UNSIGNED NOT NULL,
  `op_key` VARCHAR(16) NOT NULL,
  `cards` VARCHAR(255) NOT NULL,
  `new_mask` CHAR(5) NOT NULL,
  `collection_version` INT UNSIGNED NOT NULL,
  `stock_after` INT UNSIGNED NOT NULL,
  `since_legendary_before` INT UNSIGNED NOT NULL,
  `since_legendary_after` INT UNSIGNED NOT NULL,
  `pity_applied` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`account_id`, `pack_type`, `seq`),
  UNIQUE KEY `uk_account_key` (`account_id`, `op_key`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-hearthstone: pack openings';

-- Audit trail of stock changes: one row per grant, removal or opening (delta -1).
-- reason for example gm_pack_grant, gm_pack_remove, pack_open; source for example
-- gm:<account id>, gm:console or char:<character guid>.
CREATE TABLE IF NOT EXISTS `hearthstone_pack_ledger` (
  `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `account_id` INT UNSIGNED NOT NULL,
  `pack_type` VARCHAR(16) NOT NULL,
  `delta` INT NOT NULL,
  `stock_after` INT UNSIGNED NOT NULL,
  `reason` VARCHAR(32) NOT NULL,
  `source` VARCHAR(64) NOT NULL,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  KEY `idx_account` (`account_id`, `id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-hearthstone: pack stock changes';
