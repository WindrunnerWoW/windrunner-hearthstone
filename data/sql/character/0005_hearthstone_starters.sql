-- mod-hearthstone: class starter deck awards (PHASE_11).
-- Additive only: every statement is CREATE TABLE IF NOT EXISTS, nothing is dropped or altered.
-- Keyed by account id in the character database like the decks (0003). The core AutoUpdater
-- identifies a migration by the SHA1 of its bytes: never edit this file after it was applied
-- anywhere; add 0006_... instead. It contains no backslashes.

-- One row per account and class whose starter deck was awarded (once per account, owner decision
-- 2026-10-03). deck_id is the deck created by the award (origin starter), or 0 when the deck list
-- was full and only the cards were granted; the deck may have been deleted since. The protected
-- starter cards are granted before this row is written (an idempotent top-up in the card tables,
-- with ledger reason starter), so a retry after a failure between the two steps only writes this
-- row. source names what triggered the award: gm:<account id>, gm:console, unlock:<source of the
-- class unlock> or char:<character guid>. The primary key makes a racing second award fail.
CREATE TABLE IF NOT EXISTS `hearthstone_account_starter` (
  `account_id` INT UNSIGNED NOT NULL,
  `class` VARCHAR(16) NOT NULL,
  `deck_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `source` VARCHAR(64) NOT NULL,
  `granted_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`account_id`, `class`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-hearthstone: class starter deck awards per account';
