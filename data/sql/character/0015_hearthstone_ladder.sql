-- mod-hearthstone: the World NPC Ladder. One realm-wide ladder of NPCs (the finder names) and the
-- accounts that play against them: ranks, stars, hidden ratings, the player records and a log of the NPC games.
-- Additive only: every statement is CREATE TABLE IF NOT EXISTS, nothing is dropped or altered.
-- The core AutoUpdater identifies a migration by the SHA1 of its bytes: never edit this file after it was
-- applied anywhere, add 0016_... instead. It contains no backslashes and no semicolons inside comments or strings.
--
-- The server (src/HsLadderStore.cpp, src/ladder/HsLadderRows.cpp) reads all of it at startup and writes a whole NPC
-- round, or a whole player result, in ONE transaction. State loaded from these tables has the same StateHash as the
-- state that was saved, so a restart continues exactly where the server stopped. Delete the rows (or use
-- .hs ladder reset confirm) to start a new ladder.

-- The ladder itself: always one row with ladder_id 1.
--   seed           the stored ladder seed. Round seeds and game seeds are derived from it and the round number.
--   rounds_played  rounds applied so far, the rating-only rounds of the ladder start included.
--   started        0 until the ladder start finished and the NPCs were placed on the star ladder.
--   rules_version  Hs::Match::kRulesVersion of the engine at the last save. A different version is only logged.
CREATE TABLE IF NOT EXISTS `hearthstone_ladder` (
  `ladder_id` TINYINT UNSIGNED NOT NULL,
  `seed` INT UNSIGNED NOT NULL,
  `rounds_played` INT UNSIGNED NOT NULL DEFAULT 0,
  `started` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `rules_version` INT UNSIGNED NOT NULL DEFAULT 0,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`ladder_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-hearthstone: World NPC Ladder';

-- One row per ladder entry: an NPC or a player. Every field of Hs::Ladder::Entrant, nothing derived.
--   entrant_id     an NPC is its creature entry. A player is 4294967296 plus the account id (one entry per account).
--   name           the NPC name (creature_template, refreshed from the finder list at every start) or the name of
--                  the character that last played for the account. Only for reading the table.
--   faction        the finder row's faction for an NPC, any for a player.
--   hero_class     deck_key  profile  assigned once and for good when an NPC joins (empty and normal for a player).
--   enabled        0 when an NPC left the finder list. It keeps its record and is out of rounds and the order.
--   joined_round   rounds_played when the entry joined.
--   rank_no        15 (bottom) to 1, 0 is Legend. stars 0 to 3 on ranks 15 to 11, 0 to 5 on ranks 10 to 1.
--   rating         the hidden Elo rating in centi-points, 100000 is 1000.00.
--   win_streak     wins in a row (used for the bonus star of a player only).
--   best_rank, best_stars   the best position the entry ever held.
--   byes           NPC rounds sat out because of an odd count.
--   last_opponent  the entrant id of its opponent in its last NPC game (0 for none).
CREATE TABLE IF NOT EXISTS `hearthstone_ladder_entrant` (
  `entrant_id` BIGINT UNSIGNED NOT NULL,
  `name` VARCHAR(100) NOT NULL DEFAULT '',
  `faction` ENUM('alliance','horde','any') NOT NULL DEFAULT 'any',
  `hero_class` VARCHAR(12) NOT NULL DEFAULT '',
  `deck_key` VARCHAR(32) NOT NULL DEFAULT '',
  `profile` ENUM('normal','hard') NOT NULL DEFAULT 'normal',
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `joined_round` INT UNSIGNED NOT NULL DEFAULT 0,
  `rank_no` TINYINT UNSIGNED NOT NULL DEFAULT 15,
  `stars` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `rating` INT NOT NULL DEFAULT 100000,
  `games` INT UNSIGNED NOT NULL DEFAULT 0,
  `wins` INT UNSIGNED NOT NULL DEFAULT 0,
  `losses` INT UNSIGNED NOT NULL DEFAULT 0,
  `draws` INT UNSIGNED NOT NULL DEFAULT 0,
  `win_streak` INT UNSIGNED NOT NULL DEFAULT 0,
  `best_rank` TINYINT UNSIGNED NOT NULL DEFAULT 15,
  `best_stars` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `byes` INT UNSIGNED NOT NULL DEFAULT 0,
  `last_opponent` BIGINT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`entrant_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-hearthstone: World NPC Ladder entries';

-- A player's record against one NPC (the player's wins, losses and draws), written with the result it counts.
CREATE TABLE IF NOT EXISTS `hearthstone_ladder_vs` (
  `player_id` BIGINT UNSIGNED NOT NULL,
  `npc_id` BIGINT UNSIGNED NOT NULL,
  `wins` INT UNSIGNED NOT NULL DEFAULT 0,
  `losses` INT UNSIGNED NOT NULL DEFAULT 0,
  `draws` INT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`player_id`, `npc_id`),
  KEY `idx_npc` (`npc_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-hearthstone: World NPC Ladder player records against NPCs';

-- The log of the simulated NPC games, one row per planned game of a round, written in the transaction of the round.
-- For checks and later league news only, it is never shown as a fixture list. Round numbers are unique over the
-- whole ladder, so (round_no, slot) is the key.
--   rating_only  1 for the rounds of the ladder start (only hidden ratings change there).
--   npc_a, npc_b entrant ids, npc_a is the engine's side 0.
--   seed         the game seed, with the two decks and the engine rules it replays the game.
--   outcome      win0 (npc_a won), win1 (npc_b won), draw or failed (no result, nothing changed for either NPC).
--   first_side   the side that took the first turn, -1 when the game could not be set up.
--   error        why a failed game failed, empty otherwise.
CREATE TABLE IF NOT EXISTS `hearthstone_ladder_game` (
  `round_no` INT UNSIGNED NOT NULL,
  `slot` INT UNSIGNED NOT NULL,
  `rating_only` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `npc_a` BIGINT UNSIGNED NOT NULL,
  `npc_b` BIGINT UNSIGNED NOT NULL,
  `seed` INT UNSIGNED NOT NULL,
  `outcome` VARCHAR(8) NOT NULL,
  `first_side` TINYINT NOT NULL DEFAULT -1,
  `turns` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `error` VARCHAR(160) NOT NULL DEFAULT '',
  PRIMARY KEY (`round_no`, `slot`),
  KEY `idx_a` (`npc_a`, `round_no`),
  KEY `idx_b` (`npc_b`, `round_no`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-hearthstone: World NPC Ladder NPC game log';
