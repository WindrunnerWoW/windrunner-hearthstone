-- Alliance Rogue: owner manually places Matt Dixon 919532 and Alex Horley 919533.
-- Rigged dice and dialogue are defined here; modified migrations rerun automatically.
-- A Light Touch has exactly three objectives: the three cards. No hidden counters.
-- No creature/gameobject spawns. 919534-919536 are objective-only, never placed.
-- Copy Ibelin's teacher baseline. Preserve existing Aedis/Osric services and quests.
DELETE FROM `creature_template` WHERE `entry` BETWEEN 919532 AND 919538;
INSERT INTO `creature_template`
  (`entry`, `display_id1`, `name`, `subname`, `level_min`, `level_max`, `health_min`, `health_max`,
   `armor`, `faction`, `npc_flags`, `speed_walk`, `speed_run`, `detection_range`, `call_for_help_range`,
   `dmg_min`, `dmg_max`, `attack_power`, `base_attack_time`, `ranged_attack_time`, `unit_class`,
   `unit_flags`, `type`, `regeneration`, `movement_type`, `gossip_menu_id`, `script_name`)
SELECT c.entry,c.display_id,c.name,c.subname,
  b.level_min,b.level_max,b.health_min,b.health_max,b.armor,12,c.flags,b.speed_walk,b.speed_run,
  b.detection_range,b.call_for_help_range,b.dmg_min,b.dmg_max,b.attack_power,b.base_attack_time,
  b.ranged_attack_time,b.unit_class,b.unit_flags,b.type,b.regeneration,0,c.menu,''
FROM `creature_template` b CROSS JOIN (
  SELECT 919532 AS entry,4338 AS display_id,'Operative Matt Dixon' AS name,'SI:7' AS subname,1 AS flags,65017 AS menu
  UNION ALL SELECT 919533,5551,'Alex Horley','Card trader',3,65018
) c WHERE b.entry=919502;
INSERT INTO `creature_template`
  (`entry`,`display_id1`,`name`,`level_min`,`level_max`,`health_min`,`health_max`,
   `faction`,`npc_flags`,`unit_flags`,`type`,`regeneration`,`movement_type`,`script_name`)
VALUES
  (919534,5551,'Question Aedis about the marked card',1,1,1,1,12,0,33554434,7,3,0,''),
  (919535,5551,'Ask Osric about the silver dagger',1,1,1,1,12,0,33554434,7,3,0,''),
  (919536,5551,'Find Alex Horley',1,1,1,1,12,0,33554434,7,3,0,'');
UPDATE `creature_template` SET `npc_flags`=`npc_flags` | 1 WHERE `entry` IN (1323,1478);

DELETE FROM `gossip_menu` WHERE `entry` IN (65017,65018);
-- After A Light Touch is turned in, Alex mentions his trading (highest satisfied condition wins).
INSERT IGNORE INTO `conditions` (`type`,`value1`,`value2`,`value3`,`value4`,`flags`) VALUES (8,919782,0,0,0,0);
INSERT INTO `gossip_menu` (`entry`,`text_id`,`condition_id`) VALUES (65017,9195220,0),(65018,9195221,0);
INSERT INTO `gossip_menu` (`entry`,`text_id`,`condition_id`)
SELECT 65018,9195244,`condition_entry` FROM `conditions`
WHERE `type`=8 AND `value1`=919782 AND `value2`=0 AND `value3`=0 AND `value4`=0 AND `flags`=0;
DELETE FROM `npc_text` WHERE `ID` BETWEEN 9195220 AND 9195244;
DELETE FROM `broadcast_text` WHERE `entry` BETWEEN 9195220 AND 9195244;
INSERT INTO `broadcast_text` (`entry`,`male_text`) VALUES
  (9195220,'Official business? Keep it brief.'),
  (9195221,'A good card changes hands. A good player knows when to let it go.'),
  (9195222,'Don''t know what you''re talking about. I am not interested in card games.'),
  (9195223,'Silver dagger on the corner? Where did you get it?'),
  (9195224,'Funny. People usually slip things out of bags around here. Osric Strang might know more about this. Ask him about it.'),
  (9195225,'Ask Osric Strang, the heavy armor merchant in Old Town. Show him the silver dagger mark.'),
  (9195226,'That mark belongs to Alex Horley. He knows cards almost as well as he knows how to avoid a straight answer.'),
  (9195227,'Then you are looking for the right man. Find Alex Horley here in Old Town. Can''t tell you exactly where to look.'),
  (9195228,'Alex Horley is here in Old Town.'),
  (9195229,'Took you long enough.'),
  (9195230,'Yes. Matt slipped this into your pocket, you noticed the card, followed the clues, and asked the right questions. That is a start.'),
  (9195231,'You found me. Now let us see whether you can acquire a few cards without drawing a weapon.'),
  (9195232,'Highest throw wins. You win, you get a card. I win, you owe me 10 gold coins. You sure? All right, I will start...$B$BA ten. It''s your turn.'),
  (9195233,'Twelve! You win. The card is yours; make room and collect it when you can.'),
  (9195235,'Twelve! Fine, you win. Here is the card.'),
  (9195236,'Spare a card? I sell armor. People already expect the armor to be free once they hear the price. Why should my cards be any different?'),
  (9195237,'Help someone learn, eh? Better reason than "because I want it." Who is teaching you?'),
  (9195238,'Alex? Then you may actually learn something. All right. Take this and put it to good use.'),
  (9195239,'There you go.'),
  (9195240,'Check upstairs in Gerik Koen''s Heavy-Handed Weapons shop. He loves to play and keeps his cards upstairs. He will get it back as soon as I play him again; take that one without him noticing, then bring all three cards back to me.'),
  (9195241,'These dice are weighted to land on six. Aedis likes talking about his wins. Ask him about the last one, and while he is busy boasting, slip these into the cup. Roll before he looks back. Win his card without letting him notice the switch.'),
  (9195242,'Aedis turns away and launches into the story of his last winning game. His eyes are off the cup. This is your moment to switch the dice.'),
  (9195244,'A good card changes hands. A good player knows when to let it go.$B$BI keep a few special cards in limited stock, and the selection changes every day. Take a look.'),
  (9195243,'Bring the rigged dice Alex gave you. If you lost them, ask him for another set. If they are in the bank, collect them before we play.');
UPDATE `broadcast_text` SET `female_text`=`male_text` WHERE `entry` BETWEEN 9195220 AND 9195244;
INSERT INTO `npc_text` (`ID`,`BroadcastTextID0`,`Probability0`)
SELECT `entry`,`entry`,1 FROM `broadcast_text` WHERE `entry` BETWEEN 9195220 AND 9195244;

DELETE FROM `item_template` WHERE `entry` BETWEEN 919638 AND 919642;
INSERT INTO `item_template`
  (`entry`,`class`,`subclass`,`name`,`description`,`display_id`,`quality`,`buy_count`,
   `buy_price`,`sell_price`,`max_count`,`stackable`,`bonding`,`material`,`start_quest`,`other_team_entry`,`script_name`)
VALUES
  (919638,12,0,'Someone Else''s Card','A silver dagger marks the corner.',69179,1,1,0,0,1,1,4,1,919781,0,''),
  (919639,12,0,'Aedis''s Wagered Card','Won without drawing a weapon. Alex is expecting this card.',69179,1,1,0,0,1,1,4,1,0,0,''),
  (919640,12,0,'Osric''s Spare Card','A duplicate, freely given after a little persuasion.',69179,1,1,0,0,1,1,4,1,0,0,''),
  (919641,12,0,'Gerik''s Tucked-Away Card','A spare from the upstairs collection in Heavy-Handed Weapons.',69179,1,1,0,0,1,1,4,1,0,0,''),
  (919642,12,0,'Alex''s Rigged Dice','Weighted to land on six. Keep Aedis''s eyes off the cup when you use them.',
  69179,1,1,0,0,1,1,4,1,0,0,'');
DELETE FROM `gameobject_template` WHERE `entry`=919055;
-- Type 3 enables LootSparkle; data8 is the native quest link, not the lootId.
INSERT INTO `gameobject_template` (`entry`,`type`,`displayId`,`name`,`size`,`data1`,`data3`,`data8`,`script_name`)
VALUES (919055,3,220,'Tucked-Away Hearthstone Card',0.35,0,0,919782,'');

DELETE FROM `quest_template` WHERE `entry` IN (919781,919782,919783);
DELETE FROM `creature_questrelation` WHERE `quest` IN (919781,919782,919783);
DELETE FROM `creature_involvedrelation` WHERE `quest` IN (919781,919782,919783);
INSERT INTO `quest_template`
  (`entry`,`Method`,`ZoneOrSort`,`MinLevel`,`QuestLevel`,`RequiredRaces`,`QuestFlags`,`SpecialFlags`,
   `PrevQuestId`,`NextQuestInChain`,`SrcItemId`,`SrcItemCount`,
   `ReqCreatureOrGOId1`,`ReqCreatureOrGOCount1`,`ReqCreatureOrGOId2`,`ReqCreatureOrGOCount2`,
   `ReqCreatureOrGOId3`,`ReqCreatureOrGOCount3`,
   `ReqItemId1`,`ReqItemCount1`,`ReqItemId2`,`ReqItemCount2`,`ReqItemId3`,`ReqItemCount3`,
   `RewXP`,`RewOrReqMoney`,`RewMoneyMaxLevel`,`RewItemId1`,`RewItemCount1`,
   `Title`,`Details`,`Objectives`,`ObjectiveText1`,`ObjectiveText2`,`ObjectiveText3`,
   `RequestItemsText`,`OfferRewardText`)
VALUES
  (919781,2,-9100,10,0,589,0,0,919714,919782,0,0,
   919534,1,919535,1,919536,1,0,0,0,0,0,0,500,0,0,0,0,
   'Someone Else''s Card',
   'Matt Dixon knows nothing about Rogue Hearthstone players. At least he said so.$B$BBut a card does not slip into a pocket by accident. A silver dagger marks its corner, and under it someone has written: "Drinking dwarves tell the best stories in Old Town."$B$BWhoever left it wants to be found, but only by someone who pays attention.',
   'Follow the marked card''s clues.',
   'Follow the clue','Talk to Osric','Find Alex Horley',
   'Follow the silver dagger.',
   'No formal invitation. No sign over the door. You found your way here anyway.$B$BI do not take students who need to be told where I am. A Rogue notices what others miss, and you noticed. Next, let us see what those hands can do.'),
  (919782,2,-9100,10,0,589,0,0,919781,919783,919642,1,
   0,0,0,0,0,0,919639,1,919640,1,919641,1,500,0,0,0,0,
   'A Light Touch',
   'Three cards, three approaches. No fighting.$B$BHere are my rigged dice. They always land on six. Aedis Brom will wager a card on a throw, but you must use these without being detected. Get him talking about his last win, slip them into the cup while he looks away, and roll.$B$BPersuade Osric Strang to give you one of his spare cards.$B$BThe third is upstairs in Gerik Koen''s Heavy-Handed Weapons shop. He loves to play and keeps his cards up there. Take one without him noticing. He will get it back, I promise.$B$BCollect the three cards in any order and bring them back.',
   'Win Aedis''s card with Alex''s rigged dice without being detected, persuade Osric, and collect Gerik''s card.',
   '','','',
   'A wager, a little persuasion, and an upstairs spare. Bring all three cards, and keep your weapons sheathed.',
   'Luck, a little conversation, and a careful eye. Three cards without a single fight.$B$BYou have good hands. Come back from time to time. Now, one more thing. Sit down and play.'),
  (919783,2,-9100,10,0,589,0,2,919782,0,0,0,
   0,0,0,0,0,0,0,0,0,0,0,0,500,0,0,919623,1,
   'The Last Trick',
   'You found the player and found the cards. Now read the table.$B$BEvery trick you used so far was borrowed: my dice, a stranger''s patience, Gerik''s spare. The last one has to be yours. Beat me in a Hearthstone duel.$B$BThere is no need for a trick die this time. Knowing when to act is trick enough.',
   'Defeat Alex Horley in a Hearthstone duel.',
   'Defeat Alex at Hearthstone','','',
   'Ready for a game? We can try again whenever you like.',
   'The last trick was yours, and I did not see it coming. Take the deck.$B$BIf anyone asks about Matt, you do not know what they are talking about.');
-- Quest 1 starts only from the card: no creature start relation, no marker on Matt.
INSERT INTO `creature_questrelation` (`id`,`quest`) VALUES (919533,919782),(919533,919783);
INSERT INTO `creature_involvedrelation` (`id`,`quest`) VALUES (919533,919781),(919533,919782),(919533,919783);

DELETE FROM `hearthstone_opponent` WHERE `creature_entry`=919533;
DELETE FROM `hearthstone_npc_unlock` WHERE `creature_entry`=919533;
INSERT INTO `hearthstone_npc_unlock` (`creature_entry`,`quest_id`,`deck_key`,`faction`)
VALUES (919533,919783,'normal_rogue','alliance');
