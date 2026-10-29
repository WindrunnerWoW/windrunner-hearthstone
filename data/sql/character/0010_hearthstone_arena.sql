-- mod-hearthstone: the Draft Arena runs, the daily entry slot, the drafted picks, the duels and
-- their replay logs, the special-card claims and the idempotency keys of the client ops.
-- Additive only: every statement is CREATE TABLE IF NOT EXISTS, nothing is dropped or altered.
-- Keyed by account id like the collection: a run belongs to the account, whichever character plays it.
-- The core AutoUpdater identifies a migration by the SHA1 of its bytes: never edit this file after it was
-- applied anywhere; add 0011_... instead. It contains no backslashes.

-- One row per run. The newest row of an account is its current run.
--   state            class, draft, ready, duel, paused, finished, retired, settled or repair (Hs::ArenaStateCode).
--   repair_from      the state a run in repair came from.
--   open_account     the account id while the run is open (not settled, or settled with a special-card choice
--                    pending), NULL afterwards. The unique key on it is the rule "one open run per account",
--                    enforced here and not only in the service.
--   pay_method       free (arena introduction quest, GM start), gold (pay_amount copper) or coins (pay_amount
--                    Hearthstone Coin items). The method and the amounts below are the terms the run snapshotted at
--                    entry: a config reload never changes a run that exists.
--   rules_version    Hs::Match::kRulesVersion and dataset_hash the first 16 hex characters of the card dataset hash
--                    at entry. A run of another version is moved to repair, never played on.
--   paying_guid      the character that paid (or started a free run); slot_period the daily period a paid entry
--                    reserved in hearthstone_arena_entry (0 for a free run).
--   entry_confirmed  0 until the entry debit committed. The debit and this flag commit in ONE character
--                    database transaction (HsArenaGlue.cpp); a run with 0 here is only a reservation.
--   match_no         duels started, result_no the newest duel whose result is recorded.
--   controller_guid  the character that started or resumed the current duel; ended_guid the one that ended the
--                    run (it receives the coins).
--   prize_*          the payout fixed when the run ended; special_* the special-card choice and its claim.
CREATE TABLE IF NOT EXISTS `hearthstone_arena_run` (
  `run_id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `account_id` INT UNSIGNED NOT NULL,
  `open_account` INT UNSIGNED DEFAULT NULL,
  `state` VARCHAR(12) NOT NULL,
  `repair_from` VARCHAR(12) NOT NULL DEFAULT 'class',
  `deck_class` VARCHAR(12) NOT NULL DEFAULT '',
  `wins` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `draws` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `losses` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `match_no` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `result_no` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `pay_method` ENUM('free','gold','coins') NOT NULL,
  `pay_amount` INT UNSIGNED NOT NULL DEFAULT 0,
  `base_packs` INT UNSIGNED NOT NULL,
  `bonus_coins` INT UNSIGNED NOT NULL,
  `rarity_weights` VARCHAR(40) NOT NULL,
  `rules_version` INT UNSIGNED NOT NULL,
  `dataset_hash` VARCHAR(32) NOT NULL,
  `paying_guid` INT UNSIGNED NOT NULL DEFAULT 0,
  `slot_period` BIGINT UNSIGNED NOT NULL DEFAULT 0,
  `entry_confirmed` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `controller_guid` INT UNSIGNED NOT NULL DEFAULT 0,
  `ended_guid` INT UNSIGNED NOT NULL DEFAULT 0,
  `retired` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `prize_packs` INT UNSIGNED NOT NULL DEFAULT 0,
  `prize_coins` INT UNSIGNED NOT NULL DEFAULT 0,
  `prize_special` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `special_pending` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `special_card` VARCHAR(32) NOT NULL DEFAULT '',
  `special_copies` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `created` BIGINT UNSIGNED NOT NULL DEFAULT 0,
  `ended` BIGINT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`run_id`),
  UNIQUE KEY `uq_open_account` (`open_account`),
  KEY `idx_account` (`account_id`, `run_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-hearthstone: draft arena runs';

-- The daily entry slot: one PAID entry per account and trader period (period_start = the UTC boundary of
-- Hearthstone.Trader.SwapTime, like hearthstone_trader_purchase). The primary key is the limit. Written in the
-- same transaction as the run row; deleted only with an unconfirmed run (a failed entry gives the slot back).
-- A free run never has a row here.
CREATE TABLE IF NOT EXISTS `hearthstone_arena_entry` (
  `period_start` BIGINT UNSIGNED NOT NULL,
  `account_id` INT UNSIGNED NOT NULL,
  `run_id` INT UNSIGNED NOT NULL,
  PRIMARY KEY (`period_start`, `account_id`),
  KEY `idx_run` (`run_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-hearthstone: draft arena daily entry slot';

-- Every offer of a run, written before it is shown. pick_no 0 is the class offer (the ids are class names),
-- 1..30 the card offers. offer is the comma separated ids in the order shown, chosen the answer ('' while open).
CREATE TABLE IF NOT EXISTS `hearthstone_arena_pick` (
  `run_id` INT UNSIGNED NOT NULL,
  `pick_no` TINYINT UNSIGNED NOT NULL,
  `offer` VARCHAR(112) NOT NULL,
  `chosen` VARCHAR(32) NOT NULL DEFAULT '',
  PRIMARY KEY (`run_id`, `pick_no`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-hearthstone: draft arena offers and picks';

-- One row per duel of a run: the AI opponent, persisted BEFORE the duel starts (a relog or a crash never draws a
-- new opponent), and what a paused duel needs besides its action log.
--   opponent_deck    the 30 drafted card ids in their exact drafted order (the shuffle depends on the order).
--   seed             the engine seed. With the two decks, the seed and the action log the duel is rebuilt
--                    move for move.
--   timer_ms         the remaining Nozdormu turn timer of a paused duel (0 = none was running).
--   rules_version/dataset_hash  what the log was recorded with; a restore of another version is refused.
--   result           '' while the duel is not decided, then win, draw or loss (a draw counts as a win).
CREATE TABLE IF NOT EXISTS `hearthstone_arena_match` (
  `run_id` INT UNSIGNED NOT NULL,
  `match_no` SMALLINT UNSIGNED NOT NULL,
  `opponent_class` VARCHAR(12) NOT NULL,
  `opponent_name` VARCHAR(64) NOT NULL,
  `opponent_deck` VARCHAR(1100) NOT NULL,
  `seed` INT UNSIGNED NOT NULL,
  `timer_ms` INT UNSIGNED NOT NULL DEFAULT 0,
  `rules_version` INT UNSIGNED NOT NULL,
  `dataset_hash` VARCHAR(32) NOT NULL,
  `started_at` BIGINT UNSIGNED NOT NULL DEFAULT 0,
  `result` VARCHAR(8) NOT NULL DEFAULT '',
  PRIMARY KEY (`run_id`, `match_no`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-hearthstone: draft arena duels';

-- The replay log: every action applied to a duel, in order (Hs::EncodeAction, for example
-- v1,0,P,12,0,-1,0). idx starts at 0 for the AI's opening mulligan. A row is written before the action is shown
-- to the player.
CREATE TABLE IF NOT EXISTS `hearthstone_arena_action` (
  `run_id` INT UNSIGNED NOT NULL,
  `match_no` SMALLINT UNSIGNED NOT NULL,
  `idx` INT UNSIGNED NOT NULL,
  `action` VARCHAR(160) NOT NULL,
  PRIMARY KEY (`run_id`, `match_no`, `idx`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-hearthstone: draft arena duel replay log';

-- The special-card claims of ten-win runs: one row per account and card.
CREATE TABLE IF NOT EXISTS `hearthstone_arena_milestone` (
  `account_id` INT UNSIGNED NOT NULL,
  `card_id` VARCHAR(32) NOT NULL,
  `run_id` INT UNSIGNED NOT NULL,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`account_id`, `card_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-hearthstone: draft arena special-card claims';

-- Idempotency of the client ops, as in the protocol: a repeated key returns the stored result.
CREATE TABLE IF NOT EXISTS `hearthstone_arena_op` (
  `account_id` INT UNSIGNED NOT NULL,
  `op_key` VARCHAR(16) NOT NULL,
  `result` VARCHAR(255) NOT NULL,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`account_id`, `op_key`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-hearthstone: draft arena client op keys';
