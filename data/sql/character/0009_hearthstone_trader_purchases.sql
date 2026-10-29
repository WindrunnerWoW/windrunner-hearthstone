-- One daily card purchase per account and rotation (period_start = UTC swap boundary),
-- shared by every character and trader.
-- The table only holds the daily limit, so it is rebuilt: an earlier build created it
-- without card_id, and CREATE TABLE IF NOT EXISTS would have kept that old shape.
DROP TABLE IF EXISTS `hearthstone_trader_purchase`;
CREATE TABLE `hearthstone_trader_purchase` (
  `period_start` BIGINT UNSIGNED NOT NULL,
  `account_id` INT UNSIGNED NOT NULL,
  `card_id` VARCHAR(32) NOT NULL,
  PRIMARY KEY (`period_start`, `account_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
