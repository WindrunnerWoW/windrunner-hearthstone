-- Horde Warlock: owner places Varka 919559 and Skrit 919560. Never place the credits.
-- Placeholder models; no creature/gameobject spawns, paths or existing-NPC changes.
DELETE FROM `creature_template` WHERE `entry` IN (919559,919560,919564,919565,919566);
INSERT INTO `creature_template`
  (`entry`,`display_id1`,`name`,`subname`,`level_min`,`level_max`,`health_min`,`health_max`,
   `mana_min`,`mana_max`,`armor`,`faction`,`npc_flags`,`speed_walk`,`speed_run`,
   `detection_range`,`call_for_help_range`,`dmg_min`,`dmg_max`,`attack_power`,
   `base_attack_time`,`ranged_attack_time`,`unit_class`,`unit_flags`,`type`,`regeneration`,
   `movement_type`,`gossip_menu_id`,`auras`,`phase_quest_id`,`script_name`)
SELECT c.`entry`,c.`display`,c.`name`,c.`subname`,b.`level_min`,b.`level_max`,b.`health_min`,b.`health_max`,
  b.`mana_min`,b.`mana_max`,b.`armor`,85,c.`flags`,b.`speed_walk`,b.`speed_run`,
  b.`detection_range`,b.`call_for_help_range`,b.`dmg_min`,b.`dmg_max`,b.`attack_power`,
  b.`base_attack_time`,b.`ranged_attack_time`,b.`unit_class`,
  IF(c.`flags`=0,33554432,b.`unit_flags`),IF(c.`entry`=919560,3,b.`type`),b.`regeneration`,0,c.`menu`,'',0,''
FROM `creature_template` b CROSS JOIN (
  SELECT 919559 AS `entry`,1325 AS `display`,'Varka Ashhand' AS `name`,'Hearthstone teacher' AS `subname`,3 AS `flags`,65046 AS `menu`
  UNION ALL SELECT 919560,4449,'Skrit','Varka''s imp',1,65047
  UNION ALL SELECT 919564,1325,'HS Warlock Scorched Card','',0,0
  UNION ALL SELECT 919565,1325,'HS Warlock Stained Card','',0,0
  UNION ALL SELECT 919566,1325,'HS Warlock Torn Card','',0,0
) c WHERE b.`entry`=919501;

DELETE FROM `item_template` WHERE `entry`=919646;
INSERT INTO `item_template`
  (`entry`,`class`,`subclass`,`name`,`description`,`display_id`,`quality`,`buy_count`,
   `buy_price`,`sell_price`,`max_count`,`stackable`,`bonding`,`material`,`other_team_entry`,`script_name`)
SELECT 919646,12,0,'Claw-Marked Practice Card','One small claw mark. No additional terms.',
  `display_id`,1,1,0,0,1,1,4,1,0,'' FROM `item_template` WHERE `entry`=919640;

DELETE FROM `item_template` WHERE `entry`=919647;
INSERT INTO `item_template`
  (`entry`,`class`,`subclass`,`name`,`description`,`display_id`,`quality`,`buy_count`,
   `buy_price`,`sell_price`,`max_count`,`stackable`,`bonding`,`material`,`other_team_entry`,`script_name`)
SELECT 919647,12,0,'Burnt Bone Ashes','One handful. Enough to settle an imp''s price.',
  `display_id`,1,1,300,0,1,1,4,1,0,'' FROM `item_template` WHERE `entry`=10940;

-- Hagrus retains his native reagent service and all existing stock.
-- Native vendor pricing: 3 silver before any normal reputation discount.
INSERT IGNORE INTO `conditions` (`type`,`value1`,`value2`,`value3`,`value4`,`flags`)
VALUES (9,919832,1,0,0,0);
DELETE FROM `npc_vendor` WHERE `entry`=3335 AND `item`=919647;
INSERT INTO `npc_vendor` (`entry`,`item`,`maxcount`,`incrtime`,`condition_id`)
SELECT 3335,919647,0,0,`condition_entry` FROM `conditions`
WHERE `type`=9 AND `value1`=919832 AND `value2`=1 AND `value3`=0 AND `value4`=0 AND `flags`=0;

DELETE FROM `gossip_menu` WHERE `entry` IN (65046,65047,65051,65052,65053,65058);
DELETE FROM `npc_text` WHERE `ID` IN (9195330,9195331,9195335,9195336,9195337,9195342,9195344,9195345,9195346,9195347,9195348,9195349,9195350,9195351,9195352,9195353);
DELETE FROM `broadcast_text` WHERE `entry` IN (9195330,9195331,9195335,9195336,9195337,9195342,9195344,9195345,9195346,9195347,9195348,9195349,9195350,9195351,9195352,9195353);
INSERT INTO `broadcast_text` (`entry`,`male_text`,`female_text`) VALUES
  (9195330,'Pull up a chair. Keep your hands where the imp can see them.','Pull up a chair. Keep your hands where the imp can see them.'),
  (9195331,'Looking for power? You have come to the right imp.','Looking for power? You have come to the right imp.'),
  (9195335,'You have examined all three. Now you know where the bill arrives.','You have examined all three. Now you know where the bill arrives.'),
  (9195336,'Skrit is here in the Cleft of Shadow. No promises. Offer him something he wants and settle the price now.','Skrit is here in the Cleft of Shadow. No promises. Offer him something he wants and settle the price now.'),
  (9195337,'You already have the marked card. Bring it to Varka. If you left it in the bank, fetch it first.','You already have the marked card. Bring it to Varka. If you left it in the bank, fetch it first.'),
  (9195342,'Health is something I can spend. Losing is something I cannot afford.','Health is something I can spend. Losing is something I cannot afford.'),
  (9195344,'Hellfire. Cleared the table. Nearly cleared my chair as well. What does that tell you?','Hellfire. Cleared the table. Nearly cleared my chair as well. What does that tell you?'),
  (9195345,'And your own throat. Count everyone it burns.','And your own throat. Count everyone it burns.'),
  (9195346,'Life Tap bought me another card. Those stains are mine. Would you do it?','Life Tap bought me another card. Those stains are mine. Would you do it?'),
  (9195347,'Good. A full hand is little use to a corpse.','Good. A full hand is little use to a corpse.'),
  (9195348,'Soulfire finished the fight. It also cost a card I was saving. What would you check before playing it?','Soulfire finished the fight. It also cost a card I was saving. What would you check before playing it?'),
  (9195349,'Exactly. Cheap magic can be expensive.','Exactly. Cheap magic can be expensive.'),
  (9195350,'One little mark? Certainly. Just a favor in return. Later. I''ll decide what kind when I need it.','One little mark? Certainly. Just a favor in return. Later. I''ll decide what kind when I need it.'),
  (9195351,'Then I won''t mark it. No favor, no card. You may tell Varka I was perfectly reasonable.','Then I won''t mark it. No favor, no card. You may tell Varka I was perfectly reasonable.'),
  (9195352,'Better than nothing. Hagrus, the reagent seller, has them. I never have the coin. Always want the ashes, though. Bring me one handful, and I''ll mark your card. Nothing owed afterward.','Better than nothing. Hagrus, the reagent seller, has them. I never have the coin. Always want the ashes, though. Bring me one handful, and I''ll mark your card. Nothing owed afterward.'),
  (9195353,'Ah, that smell! Here''s Varka''s card. We''re even. You may tell her I was perfectly reasonable.','Ah, that smell! Here''s Varka''s card. We''re even. You may tell her I was perfectly reasonable.');
INSERT INTO `npc_text` (`ID`,`BroadcastTextID0`,`Probability0`)
SELECT `entry`,`entry`,1 FROM `broadcast_text` WHERE `entry` IN (9195330,9195331,9195335,9195336,9195337,9195342,9195344,9195345,9195346,9195347,9195348,9195349,9195350,9195351,9195352,9195353);
INSERT INTO `gossip_menu` (`entry`,`text_id`) VALUES
  (65046,9195330),(65047,9195331),(65051,9195335),(65052,9195336),(65053,9195337),(65058,9195342);

DELETE FROM `quest_template` WHERE `entry` BETWEEN 919831 AND 919833;
DELETE FROM `creature_questrelation` WHERE `quest` BETWEEN 919831 AND 919833;
DELETE FROM `creature_involvedrelation` WHERE `quest` BETWEEN 919831 AND 919833;
INSERT INTO `quest_template`
  (`entry`,`Method`,`ZoneOrSort`,`MinLevel`,`QuestLevel`,`RequiredRaces`,`RequiredClasses`,`QuestFlags`,`SpecialFlags`,
   `PrevQuestId`,`NextQuestInChain`,`ReqCreatureOrGOId1`,`ReqCreatureOrGOCount1`,
   `ReqCreatureOrGOId2`,`ReqCreatureOrGOCount2`,`ReqCreatureOrGOId3`,`ReqCreatureOrGOCount3`,
   `ReqItemId1`,`ReqItemCount1`,`RewXP`,`RewOrReqMoney`,`RewMoneyMaxLevel`,`RewItemId1`,`RewItemCount1`,
   `Title`,`Details`,`Objectives`,`ObjectiveText1`,`ObjectiveText2`,`ObjectiveText3`,`ObjectiveText4`,
   `RequestItemsText`,`OfferRewardText`)
VALUES
  (919831,2,-9100,10,0,434,0,0,0,919704,919832,919564,1,919565,1,919566,1,0,0,500,0,0,0,0,
   'Read the Scars',
   'New cards. Straight edges. No stains. You can tell very little about a player from a deck like that.$B$BThese three have earned their marks. My people once took a demon''s offer without reading the price. I read mine. Sit down and look closely. Before you borrow a warlock''s power, learn where the bill arrives.',
   'Examine Varka''s three practice cards in the Cleft of Shadow.',
   'Examine the scorched card','Examine the stained card','Examine the torn card','',
   'Look at all three. The scorch marks are the easy part.',
   'Now you know what to count. Skrit owes me a marked practice card. Go collect it, and listen carefully when he says "free."'),
  (919832,2,-9100,10,0,434,0,0,0,919831,919833,0,0,0,0,0,0,919646,1,500,0,0,0,0,
   'The Imp''s Cut',
   'Skrit puts his claw mark on my practice cards. He says it makes them authentic. Mostly it makes him feel important.$B$BHe always wants a favor in return. Don''t agree to that. Bring him Burnt Bone Ashes instead; Hagrus, the reagent seller, keeps them in stock. Get the card marked, with nothing left owing.',
   'Get the practice card marked by Skrit and return it to Varka.',
   '','','','',
   'One card. If he has convinced you to bring a contract, we need to start again.',
   'Ashes for a card. A price you could count, and nothing left owing. Good. You kept your choices, and the imp kept his dignity. That is rarer than either.'),
  (919833,2,-9100,10,0,434,0,0,2,919832,0,0,0,0,0,0,0,0,0,500,0,0,919627,1,
   'The Price of Power',
   'You''ve seen what the cards can cost. You''ve heard what a demon calls a favor. Now take the other chair.$B$BI''ll spend blood when it buys me something worth having. Your task is to make me regret the purchase. Beat me at Hearthstone, and this Warlock deck is yours.',
   'Defeat Varka Ashhand in a game of Hearthstone.',
   'Defeat Varka at Hearthstone','','','',
   'Still standing? Then we can play again.',
   'You made me pay for every advantage. Keep that habit. The deck is yours. Skrit is not included.');
INSERT INTO `creature_questrelation` (`id`,`quest`) VALUES (919559,919831),(919559,919832),(919559,919833);
INSERT INTO `creature_involvedrelation` (`id`,`quest`) VALUES (919559,919831),(919559,919832),(919559,919833);

DELETE FROM `hearthstone_opponent` WHERE `creature_entry`=919559;
DELETE FROM `hearthstone_npc_unlock` WHERE `creature_entry`=919559;
INSERT INTO `hearthstone_npc_unlock` (`creature_entry`,`quest_id`,`deck_key`,`faction`)
VALUES (919559,919833,'normal_warlock','horde');
