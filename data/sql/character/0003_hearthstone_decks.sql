-- mod-hearthstone: decks and account-wide class access (PHASE_06).
-- Additive only: every statement is CREATE TABLE IF NOT EXISTS, nothing is dropped or altered.
-- Keyed by account id in the character database, like 0001 and 0002: decks are shared by all
-- characters of an account. The core AutoUpdater identifies a migration by the SHA1 of its
-- bytes: never edit this file after it was applied anywhere; add 0004_... instead. It contains
-- no backslashes.

-- Saved decks, drafts included. deck_id numbers the decks of an account and is never reused:
-- a new deck gets 1 + the highest id ever used, deleted rows included. A delete is soft
-- (deleted = 1): the row stays for audit and recovery and is invisible everywhere else.
-- name is the trimmed deck name (1..24 characters, at most 64 bytes of UTF-8). deck_class is
-- one of DRUID HUNTER MAGE PALADIN PRIEST ROGUE SHAMAN WARLOCK WARRIOR and never changes.
-- cards is <card id>.<copies>,... sorted by id (for example CS2_022.2,EX1_116.1); card_count
-- the sum of the copies (at most 30). rev is 1 after the create and goes up by one with every
-- committed change, the delete included. origin is player or gm_fixture.
CREATE TABLE IF NOT EXISTS `hearthstone_deck` (
  `account_id` INT UNSIGNED NOT NULL,
  `deck_id` INT UNSIGNED NOT NULL,
  `name` VARCHAR(64) CHARACTER SET utf8mb4 NOT NULL,
  `deck_class` VARCHAR(16) NOT NULL,
  `cards` VARCHAR(1024) NOT NULL,
  `card_count` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `rev` INT UNSIGNED NOT NULL,
  `origin` VARCHAR(16) NOT NULL,
  `deleted` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`account_id`, `deck_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-hearthstone: decks per account';

-- The idempotency record of client deck operations, kept forever. op_key is the key chosen by the client
-- (DECK_SAVE / DECK_DELETE key=): a retry with the same key returns this row instead of
-- applying again. op is save or delete; rev the revision of the deck after the operation (1 for the
-- save that created the deck). Written in the same transaction as the deck row; the primary key
-- makes a racing duplicate fail the whole transaction. GM changes carry no key and no row here.
CREATE TABLE IF NOT EXISTS `hearthstone_deck_op` (
  `account_id` INT UNSIGNED NOT NULL,
  `op_key` VARCHAR(16) NOT NULL,
  `op` VARCHAR(8) NOT NULL,
  `deck_id` INT UNSIGNED NOT NULL,
  `rev` INT UNSIGNED NOT NULL,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`account_id`, `op_key`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-hearthstone: deck operation keys';

-- Audit trail: one row per committed deck change. action is create, update or delete;
-- reason for example deck_save, deck_delete, gm_fixture, gm_delete; source for example
-- char:<character guid>, gm:<account id> or gm:console.
CREATE TABLE IF NOT EXISTS `hearthstone_deck_ledger` (
  `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `account_id` INT UNSIGNED NOT NULL,
  `deck_id` INT UNSIGNED NOT NULL,
  `action` VARCHAR(8) NOT NULL,
  `rev_after` INT UNSIGNED NOT NULL,
  `card_count` TINYINT UNSIGNED NOT NULL,
  `reason` VARCHAR(32) NOT NULL,
  `source` VARCHAR(64) NOT NULL,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  KEY `idx_account` (`account_id`, `id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-hearthstone: deck changes';

-- Account-wide class unlocks (D01/D02). A character may play its own starting class (derived
-- from the character, never stored) plus the classes listed here. Owning cards or a deck of a
-- class never unlocks it. source names what granted the unlock (gm:<account id> or gm:console
-- for the phase 06 development unlocks; phase 11 questlines use the same table).
CREATE TABLE IF NOT EXISTS `hearthstone_account_class` (
  `account_id` INT UNSIGNED NOT NULL,
  `class` VARCHAR(16) NOT NULL,
  `source` VARCHAR(64) NOT NULL,
  `granted_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`account_id`, `class`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-hearthstone: class unlocks per account';

-- Audit trail of class access: one row per unlock or lock (reason for example gm_unlock, gm_lock).
CREATE TABLE IF NOT EXISTS `hearthstone_class_ledger` (
  `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `account_id` INT UNSIGNED NOT NULL,
  `class` VARCHAR(16) NOT NULL,
  `action` VARCHAR(8) NOT NULL,
  `reason` VARCHAR(32) NOT NULL,
  `source` VARCHAR(64) NOT NULL,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  KEY `idx_account` (`account_id`, `id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-hearthstone: class access changes';
