-- mod-hearthstone: account-wide card collection (PHASE_04).
-- Additive only: every statement is CREATE TABLE IF NOT EXISTS, nothing is dropped.
-- Keyed by account id in the character database (the mod-crafting-orders precedent), so
-- ownership is shared by all characters of an account and survives character deletion.
-- The core AutoUpdater identifies a migration by the SHA1 of its bytes: never edit this file
-- after it was applied anywhere; add 0002_... instead. It contains no backslashes (the
-- updater guesses string state from the previous character).

-- One row per owned card. Rows with both quantities at 0 are deleted.
-- card_id is the dataset id (for example CS2_022), not the dbf id.
CREATE TABLE IF NOT EXISTS `hearthstone_account_card` (
  `account_id` INT UNSIGNED NOT NULL,
  `card_id` VARCHAR(32) NOT NULL,
  `qty_protected` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `qty_unprotected` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`account_id`, `card_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-hearthstone: cards owned per account';

-- Collection version, bumped in the same transaction as every collection change.
CREATE TABLE IF NOT EXISTS `hearthstone_account` (
  `account_id` INT UNSIGNED NOT NULL,
  `collection_version` INT UNSIGNED NOT NULL DEFAULT 0,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`account_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-hearthstone: per-account state';

-- Audit trail: one row per committed change (reason for example gm_grant, gm_remove;
-- source for example gm:<account id> or gm:console).
CREATE TABLE IF NOT EXISTS `hearthstone_card_ledger` (
  `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `account_id` INT UNSIGNED NOT NULL,
  `card_id` VARCHAR(32) NOT NULL,
  `delta_protected` INT NOT NULL,
  `delta_unprotected` INT NOT NULL,
  `version_after` INT UNSIGNED NOT NULL,
  `reason` VARCHAR(32) NOT NULL,
  `source` VARCHAR(64) NOT NULL,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  KEY `idx_account` (`account_id`, `id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-hearthstone: card ownership changes';
