-- mod-hearthstone: ranked ladder duels that pause and resume, and the NPC rounds that wait for the
-- server to play them. Additive only: every statement is CREATE TABLE IF NOT EXISTS, nothing is dropped or altered.
-- The core AutoUpdater identifies a migration by the SHA1 of its bytes: never edit this file after it was
-- applied anywhere, add 0018_... instead. It contains no backslashes and no semicolons inside comments or strings.
--
-- A ladder duel is an ordinary match against a summoned ladder NPC whose every action is stored before the player sees it
-- (src/HsLadderDuelStore.cpp, the same way as the draft arena). An interruption (logout, teleport, death, the NPC gone,
-- closing the window) only pauses it: the account resumes it at a ladder chair, from any of its characters. Only an
-- explicit surrender is a loss. A duel is deleted in the SAME transaction that applies its result to the ladder (the
-- result, the stars, the ratings and the pending NPC round), so a result is applied exactly once.

-- One row per account that has a ladder duel (the unique key on account_id is the "one ladder duel per account" rule).
--   duel_id        the run id of the duel (the server counts up from the highest stored id).
--   guid           the character that plays or last played the duel.
--   state          active (a match runs, or it did when the server stopped) or paused (stored, nobody plays it).
--   npc_entry      the creature entry (the ladder entrant id) of the opponent.
--   human_deck     the card ids of the player's deck as played, one entry per copy, comma separated.
--   opponent_deck  the same for the opponent, so a change of the library deck does not change a paused duel.
--   rules_version  Match::kRulesVersion and dataset_hash the card dataset hash the duel was played with: a resume
--                  with other ones is refused and the duel stays for an administrator.
--   timer_ms       the milliseconds left on the turn timer when it was paused (0 for none).
CREATE TABLE IF NOT EXISTS `hearthstone_ladder_duel` (
  `duel_id` BIGINT UNSIGNED NOT NULL,
  `account_id` INT UNSIGNED NOT NULL,
  `guid` INT UNSIGNED NOT NULL,
  `state` VARCHAR(8) NOT NULL,
  `npc_entry` INT UNSIGNED NOT NULL,
  `opponent_name` VARCHAR(100) NOT NULL,
  `opponent_class` VARCHAR(12) NOT NULL,
  `opponent_deck_key` VARCHAR(32) NOT NULL,
  `opponent_deck` TEXT NOT NULL,
  `profile` VARCHAR(8) NOT NULL,
  `deck_id` INT UNSIGNED NOT NULL,
  `deck_name` VARCHAR(100) NOT NULL,
  `human_class` VARCHAR(12) NOT NULL,
  `human_deck` TEXT NOT NULL,
  `seed` INT UNSIGNED NOT NULL,
  `started_at` BIGINT NOT NULL,
  `rules_version` INT UNSIGNED NOT NULL,
  `dataset_hash` VARCHAR(64) NOT NULL,
  `timer_ms` INT UNSIGNED NOT NULL DEFAULT 0,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`duel_id`),
  UNIQUE KEY `uq_hearthstone_ladder_duel_account` (`account_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-hearthstone: World NPC Ladder duels that can pause and resume';

-- The replay log of a duel: every applied action in order (Hs::EncodeAction text), stored before the player sees it.
CREATE TABLE IF NOT EXISTS `hearthstone_ladder_duel_action` (
  `duel_id` BIGINT UNSIGNED NOT NULL,
  `idx` INT UNSIGNED NOT NULL,
  `action` VARCHAR(160) NOT NULL,
  PRIMARY KEY (`duel_id`, `idx`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-hearthstone: replay log of a ladder duel';

-- NPC rounds that follow a player result and were not played yet. One row is written in the transaction of the player
-- result, the oldest row is deleted in the transaction of the round that plays it. After a server stop the rows that are
-- left are played first (the NPC that sat out is excluded_npc).
CREATE TABLE IF NOT EXISTS `hearthstone_ladder_pending` (
  `seq` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `excluded_npc` BIGINT UNSIGNED NOT NULL,
  PRIMARY KEY (`seq`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-hearthstone: World NPC Ladder rounds waiting to be played';
