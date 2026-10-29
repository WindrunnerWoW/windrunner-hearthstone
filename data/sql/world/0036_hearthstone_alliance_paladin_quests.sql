-- Alliance Paladin: Rowan 919570 and Tess 919571 in Cathedral Square; Ada Sparkplugz 919577 at Stormwind Keep.
-- Existing locations and game models. No new locations or spawn SQL.
-- Native item delivery replaces the retired three-card lesson. Never place 919575-919576.
DELETE FROM `creature_template` WHERE `entry` BETWEEN 919570 AND 919577;
INSERT INTO `creature_template`
  (`entry`,`display_id1`,`name`,`subname`,`level_min`,`level_max`,`health_min`,`health_max`,
   `mana_min`,`mana_max`,`armor`,`faction`,`npc_flags`,`speed_walk`,`speed_run`,
   `detection_range`,`call_for_help_range`,`dmg_min`,`dmg_max`,`attack_power`,
   `base_attack_time`,`ranged_attack_time`,`unit_class`,`unit_flags`,`type`,`regeneration`,
   `movement_type`,`gossip_menu_id`,`auras`,`phase_quest_id`,`script_name`)
SELECT c.`entry`,c.`display`,c.`name`,c.`subname`,b.`level_min`,b.`level_max`,b.`health_min`,b.`health_max`,
  b.`mana_min`,b.`mana_max`,b.`armor`,12,c.`flags`,b.`speed_walk`,b.`speed_run`,
  b.`detection_range`,b.`call_for_help_range`,b.`dmg_min`,b.`dmg_max`,b.`attack_power`,
  b.`base_attack_time`,b.`ranged_attack_time`,b.`unit_class`,
  IF(c.`flags`=0,33554432,b.`unit_flags`),b.`type`,b.`regeneration`,0,c.`menu`,'',0,''
FROM `creature_template` b CROSS JOIN (
  SELECT 919570 AS `entry`,3284 AS `display`,'Sir Rowan Vale' AS `name`,'Hearthstone teacher' AS `subname`,3 AS `flags`,65065 AS `menu`
  UNION ALL SELECT 919571,3289,'Squire Tess','Hearthstone student',1,65066
  UNION ALL SELECT 919575,3284,'HS Paladin Tess Invited','',0,0
  UNION ALL SELECT 919576,3284,'HS Paladin Invitation Reported','',0,0
  UNION ALL SELECT 919577,6975,'Ada Sparkplugz','Courier',3,65078
) c WHERE b.`entry`=919502;

DELETE FROM `item_template` WHERE `entry`=919650;
INSERT INTO `item_template`
  (`entry`,`class`,`subclass`,`name`,`display_id`,`quality`,`flags`,`buy_price`,`sell_price`,
   `inventory_type`,`allowable_class`,`allowable_race`,`item_level`,`required_level`,
   `max_count`,`stackable`,`bonding`,`description`,`start_quest`,`script_name`)
VALUES (919650,12,0,'Ada''s Card Case',7404,1,0,0,0,0,-1,-1,1,0,1,1,4,
  'A worn card case with "Ada Sparkplugz" written beneath the lid.',0,'');

DELETE FROM `gossip_menu` WHERE `entry` BETWEEN 65065 AND 65079;
DELETE FROM `npc_text` WHERE `ID` BETWEEN 9195370 AND 9195390;
DELETE FROM `broadcast_text` WHERE `entry` BETWEEN 9195370 AND 9195390;
INSERT INTO `broadcast_text` (`entry`,`male_text`,`female_text`) VALUES
  (9195370,'Ada came through here earlier. If I heard her correctly, she was on her way to Stormwind Keep. The name inside the lid is hers; I''d rather those cards went home than became part of my collection.','Ada came through here earlier. If I heard her correctly, she was on her way to Stormwind Keep. The name inside the lid is hers; I''d rather those cards went home than became part of my collection.'),
  (9195376,'Rowan wants me at the table? I thought he needed someone to sort the cards afterward. I''m quite good at that.','Rowan wants me at the table? I thought he needed someone to sort the cards afterward. I''m quite good at that.'),
  (9195377,'I know what the cards say. Choosing when to play them is another matter. What if I make a complete fool of myself?','I know what the cards say. Choosing when to play them is another matter. What if I make a complete fool of myself?'),
  (9195378,'He makes it look so easy. Then again, I''ve seen him forget where he put his own shield. Perhaps I''m allowed to forget a card or two.','He makes it look so easy. Then again, I''ve seen him forget where he put his own shield. Perhaps I''m allowed to forget a card or two.'),
  (9195379,'All right. I''ll join the lesson. I reserve the right to look nervous.','All right. I''ll join the lesson. I reserve the right to look nervous.'),
  (9195380,'Did Tess agree, or has she discovered another pile of cards that urgently needs sorting?','Did Tess agree, or has she discovered another pile of cards that urgently needs sorting?'),
  (9195381,'Then she will be in excellent company. I have made enough mistakes for a respectable little collection.','Then she will be in excellent company. I have made enough mistakes for a respectable little collection.'),
  (9195382,'Come to learn how a Paladin plays Hearthstone? There is room for you here. Tess has kept the practice cards in order, which is more than I can say for my notes.','Come to learn how a Paladin plays Hearthstone? There is room for you here. Tess has kept the practice cards in order, which is more than I can say for my notes.'),
  (9195383,'Ada has her cards again. Thank you. There is someone else I''d like you to meet.','Ada has her cards again. Thank you. There is someone else I''d like you to meet.'),
  (9195384,'Have a word with Tess. She is very good at finding work when someone offers her a seat.','Have a word with Tess. She is very good at finding work when someone offers her a seat.'),
  (9195385,'There is a place for Tess at our next lesson. First, let us give her a game worth watching.','There is a place for Tess at our next lesson. First, let us give her a game worth watching.'),
  (9195386,'Ready? I promise to keep the sermon shorter than the game. Tess has asked me to make that promise in front of a witness.','Ready? I promise to keep the sermon shorter than the game. Tess has asked me to make that promise in front of a witness.'),
  (9195387,'Good to see you, $n. Tess has started correcting my card arrangements. I believe that counts as progress.','Good to see you, $n. Tess has started correcting my card arrangements. I believe that counts as progress.'),
  (9195388,'I still like keeping the cards in order. I would also like a turn playing them.','I still like keeping the cards in order. I would also like a turn playing them.'),
  (9195389,'If you spot a battered card case, please let me know. My name is inside the lid. I''ve checked every pocket twice; I was about to start numbering them.','If you spot a battered card case, please let me know. My name is inside the lid. I''ve checked every pocket twice; I was about to start numbering them.'),
  (9195390,'Thank you again. Next time, my own cards are going on the delivery list.','Thank you again. Next time, my own cards are going on the delivery list.');
INSERT INTO `npc_text` (`ID`,`BroadcastTextID0`,`Probability0`)
SELECT `entry`,`entry`,1 FROM `broadcast_text` WHERE `entry` BETWEEN 9195370 AND 9195390;
INSERT INTO `gossip_menu` (`entry`,`text_id`) VALUES
  (65065,9195382),(65066,9195376),(65067,9195370),(65070,9195383),
  (65071,9195384),(65072,9195380),
  (65073,9195385),(65074,9195386),(65075,9195387),(65076,9195388),
  (65077,9195379),(65078,9195389),(65079,9195390);

DELETE FROM `quest_template` WHERE `entry` BETWEEN 919851 AND 919853;
DELETE FROM `creature_questrelation` WHERE `quest` BETWEEN 919851 AND 919853;
DELETE FROM `creature_involvedrelation` WHERE `quest` BETWEEN 919851 AND 919853;
INSERT INTO `quest_template`
  (`entry`,`Method`,`ZoneOrSort`,`MinLevel`,`QuestLevel`,`RequiredRaces`,`RequiredClasses`,`QuestFlags`,`SpecialFlags`,
   `PrevQuestId`,`NextQuestInChain`,`ReqCreatureOrGOId1`,`ReqCreatureOrGOCount1`,
   `ReqCreatureOrGOId2`,`ReqCreatureOrGOCount2`,`ReqCreatureOrGOId3`,`ReqCreatureOrGOCount3`,
   `SrcItemId`,`SrcItemCount`,`ReqItemId1`,`ReqItemCount1`,
   `RewXP`,`RewOrReqMoney`,`RewMoneyMaxLevel`,`RewItemId1`,`RewItemCount1`,
   `Title`,`Details`,`Objectives`,`ObjectiveText1`,`ObjectiveText2`,`ObjectiveText3`,`ObjectiveText4`,
   `RequestItemsText`,`OfferRewardText`)
VALUES
  (919851,2,-9100,10,0,589,0,0,0,919714,919852,0,0,0,0,0,0,919650,1,919650,1,500,0,0,0,0,
   'A Name on the Case',
   'I found this card case beside my chair. It belongs to Ada Sparkplugz, a gnome courier who came through here earlier. If I heard her correctly, she was on her way to Stormwind Keep.$B$BWould you take it back to her? She''s spent months saving for those cards. Losing a game is one thing. Losing the only deck you own is rather harder.',
   'Return Ada Sparkplugz''s card case.',
   '','','','',
   'Is that my case? I''ve checked every pocket, even the little one I keep spare washers in.',
   'Oh, thank the Light, everything''s here! I was already calculating how many deliveries it would take to replace them.$B$BThank you. And thank Sir Rowan for me. If he offers you a game, don''t be fooled by his manners. I have never seen anyone apologize so sincerely while winning.'),
  (919852,2,-9100,10,0,589,0,0,0,919851,919853,919575,1,919576,1,0,0,0,0,0,0,500,0,0,0,0,
   'An Empty Chair',
   'Thank you for helping Ada. There is someone else I''d like you to meet.$B$BTess prepares every lesson, then finds an excuse to stand aside when we play. Apparently, being a squire means everyone else ought to take the chair first.$B$BI have saved one for her. Ask whether she will join our next lesson. She might find that easier to hear from another learner.',
   'Invite Squire Tess to the lesson, then report to Sir Rowan Vale.',
   'Invite Squire Tess','Report to Sir Rowan Vale','','',
   'Have a word with Tess. She is very good at finding work when someone offers her a seat.',
   'Good. She deserves a chance to learn, mistakes and all.$B$BNow, let us give her a game worth watching. Try to keep me honest.'),
  (919853,2,-9100,10,0,589,0,0,2,919852,0,0,0,0,0,0,0,0,0,0,0,500,0,0,919621,1,
   'Stand Together',
   'Ada has her cards again, and Tess is willing to join us. I''d say you have earned a game.$B$BPlay me, and watch how I strengthen my allies and keep them standing. Defeat me, and this Paladin deck is yours.',
   'Defeat Sir Rowan Vale in a game of Hearthstone.',
   'Defeat Sir Rowan at Hearthstone','','','',
   'There is still a chair for you. Shall we try another game?',
   'A fine game, $n. I will be glad to have you at the table again.$B$BTake this Paladin Deck. You have earned it.$B$BAnd Tess, you needn''t sort it first.');
INSERT INTO `creature_questrelation` (`id`,`quest`) VALUES (919570,919851),(919570,919852),(919570,919853);
INSERT INTO `creature_involvedrelation` (`id`,`quest`) VALUES (919577,919851),(919570,919852),(919570,919853);

-- Shared generated policy: quest-only Basic Paladin / Normal, account Alliance
-- finder roster at final turn-in. Tess is a supporting actor, never an opponent.
DELETE FROM `hearthstone_opponent` WHERE `creature_entry`=919570;
DELETE FROM `hearthstone_npc_unlock` WHERE `creature_entry`=919570;
INSERT INTO `hearthstone_npc_unlock` (`creature_entry`,`quest_id`,`deck_key`,`faction`)
VALUES (919570,919853,'normal_paladin','alliance');
