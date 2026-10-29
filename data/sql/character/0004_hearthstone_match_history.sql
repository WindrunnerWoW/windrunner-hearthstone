-- mod-hearthstone: match history and diagnostics (PHASE_10).
-- Additive only: every statement is CREATE TABLE IF NOT EXISTS, nothing is dropped or altered.
-- Keyed by account id in the character database, like 0001 to 0003. The core AutoUpdater identifies a
-- migration by the SHA1 of its bytes: never edit this file after it was applied anywhere; add 0005_...
-- instead. It contains no backslashes.

-- One row per ended match, written by the module when the match ends (win, loss, draw, surrender,
-- interrupted, logout) if Hearthstone.Match.History.Enable is 1. The module only inserts; nothing reads
-- the table except the GM command .hs match history. Rows are never updated or deleted by the module.
--   id               auto number; newest = highest. match_id is the number the match had in the running
--                    server (it can repeat after a restart or about every 11 days).
--   character_*      the human's character when the match ended, name as of then.
--   opponent_*       the opponent creature entry (0 = unknown) and its name; difficulty is normal, hard or
--                    boss; opponent_deck_key is the hearthstone_deck_template key it played (empty for a
--                    deck not from the library), opponent_class its class (MAGE ...).
--   deck_*           the player's saved deck: id (0 = unknown), name and class (MAGE ...).
--   result           win, loss, draw, surrender (the player conceded: a loss), interrupted (no winner:
--                    stuck, teleport, death, npc, gm, error, logout) or unknown (never expected).
--   end_reason       hero (a hero died), concede, turns (the turn limit), or the interruption: stuck,
--                    teleport, death, npc, gm, error, logout.
--   turns            the turn the match ended in; duration_sec the seconds between start and end (0 if
--                    the start time was not known); times are the server's local time.
--   seed             the engine seed (the unsigned 32 bit value passed to Hs::Match::MatchSetup::seed):
--                    it decides the shuffles, who starts and every random effect, and every AI decision
--                    derives from it (Hs::AiSeedFor(seed, event seq)).
--   dataset_id/hash  the card dataset of the binary (Hs::kDatasetId and the first 16 hex characters of its
--                    content hash); rules_version is Hs::Match::kRulesVersion, the version of the rules
--                    and AI. A match can only be reproduced under the same dataset and rules version
--                    (and, for a boss, its hearthstone_opponent overrides, which are not stored here).
--   ai_delay_ms      Hearthstone.Match.AiActionDelayMs when the match started.
--   human_cards,     both decks as <card id>.<copies>,... sorted by card id (like hearthstone_deck.cards).
--   opponent_cards
--   currency_paid    always 0 until phase 11 pays rewards.
CREATE TABLE IF NOT EXISTS `hearthstone_match_history` (
  `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `account_id` INT UNSIGNED NOT NULL,
  `character_guid` INT UNSIGNED NOT NULL,
  `character_name` VARCHAR(24) CHARACTER SET utf8mb4 NOT NULL,
  `match_id` INT UNSIGNED NOT NULL,
  `opponent_entry` INT UNSIGNED NOT NULL DEFAULT 0,
  `opponent_name` VARCHAR(64) CHARACTER SET utf8mb4 NOT NULL,
  `difficulty` VARCHAR(12) NOT NULL,
  `deck_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `deck_name` VARCHAR(64) CHARACTER SET utf8mb4 NOT NULL,
  `deck_class` VARCHAR(16) NOT NULL,
  `opponent_deck_key` VARCHAR(32) NOT NULL,
  `opponent_class` VARCHAR(16) NOT NULL,
  `result` VARCHAR(12) NOT NULL,
  `end_reason` VARCHAR(12) NOT NULL,
  `turns` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `started_at` DATETIME NOT NULL,
  `ended_at` DATETIME NOT NULL,
  `duration_sec` INT UNSIGNED NOT NULL DEFAULT 0,
  `seed` INT UNSIGNED NOT NULL,
  `dataset_id` VARCHAR(32) NOT NULL,
  `dataset_hash` VARCHAR(16) NOT NULL,
  `rules_version` SMALLINT UNSIGNED NOT NULL,
  `ai_delay_ms` INT UNSIGNED NOT NULL DEFAULT 0,
  `human_cards` VARCHAR(1024) NOT NULL,
  `opponent_cards` VARCHAR(1024) NOT NULL,
  `currency_paid` INT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`id`),
  KEY `idx_account` (`account_id`, `id`),
  KEY `idx_character` (`character_guid`, `id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-hearthstone: match history';
