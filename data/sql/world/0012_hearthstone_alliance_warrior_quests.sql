-- Alliance Warrior unlock: The Strategic Mind (Ironforge).
-- Steeg Haskell copies the Hearthstone Master template values, with dwarf display 10694,
-- Ironforge faction 55 and only gossip / quest giver flags. Owner placement is recorded in world migration 0014.
-- 919721: five tactical questions, three answers each, no wrong answers (SpecialFlags 2).
-- Question me opens the first question only while quest 1 is active. The fifth answer completes the objective.
-- 919722: buy one Skin of Dwarven Stout and five Tough Jerky from Innkeeper Firebrew (5111).
-- Both items are already stocked by Firebrew. The normal quest turn-in consumes them.
-- 919723: win a Hearthstone duel against this creature while the quest is active. Losses,
-- draws, surrender, interruptions and wins against other opponents do not give credit.
-- Reward: item 919620 Warrior Deck, right-click to unlock Warrior account-wide and award its starter.
-- Level 10, 500 XP each, no WoW money or reputation. Quest 1 follows the Alliance Hearthstone lesson.
-- No creature script or core changes. Rebuilt idempotently by the module updater.

DELETE FROM `creature_template` WHERE `entry` = 919506;
INSERT INTO `creature_template`
  (`entry`, `display_id1`, `name`, `subname`, `level_min`, `level_max`, `health_min`, `health_max`,
   `armor`, `faction`, `npc_flags`, `speed_walk`, `speed_run`, `detection_range`, `call_for_help_range`,
   `dmg_min`, `dmg_max`, `attack_power`, `base_attack_time`, `ranged_attack_time`, `unit_class`,
   `unit_flags`, `type`, `regeneration`, `gossip_menu_id`, `script_name`)
VALUES
  (919506, 10694, 'Steeg Haskell', 'Hearthstone enthusiast', 60, 60, 4500, 4500,
   3500, 55, 3, 1, 1.14286, 18, 5,
   100, 130, 250, 2000, 2000, 1,
   4608, 7, 3, 65006, '');

DELETE FROM `gossip_menu` WHERE `entry` = 65006;
-- After War Games is turned in, the highest satisfied condition wins: Steeg greets the victor instead.
INSERT IGNORE INTO `conditions` (`type`, `value1`, `value2`, `value3`, `value4`, `flags`)
VALUES (8, 919723, 0, 0, 0, 0);
INSERT INTO `gossip_menu` (`entry`, `text_id`, `condition_id`)
SELECT 65006, 9195122, 0
UNION ALL
SELECT 65006, 9195127, `condition_entry` FROM `conditions`
WHERE `type` = 8 AND `value1` = 919723 AND `value2` = 0 AND `value3` = 0 AND `value4` = 0 AND `flags` = 0;
DELETE FROM `npc_text` WHERE `ID` BETWEEN 9195120 AND 9195127;
DELETE FROM `broadcast_text` WHERE `entry` BETWEEN 9195120 AND 9195127;
INSERT INTO `broadcast_text` (`entry`, `male_text`, `female_text`) VALUES
  (9195120, 'Enemy''s got three minions. Ye''ve got one.$B$BWhat''s yer next move, commander?',
            'Enemy''s got three minions. Ye''ve got one.$B$BWhat''s yer next move, commander?'),
  (9195121, 'Interesting.$B$BFive situations. Five orders. Every answer tells me something about the commander giving it.$B$BThere''s no right answer. The battlefield doesn''t give ye a list to choose from. Neither did mine.$B$BNow, about our supply line...',
            'Interesting.$B$BFive situations. Five orders. Every answer tells me something about the commander giving it.$B$BThere''s no right answer. The battlefield doesn''t give ye a list to choose from. Neither did mine.$B$BNow, about our supply line...'),
  (9195122, 'A good commander keeps his cards close and his ale closer.$B$BWhat brings ye to me table?',
            'A good commander keeps his cards close and his ale closer.$B$BWhat brings ye to me table?'),
  (9195123, 'Interesting.$B$BNext situation. Enemy archers have yer troops pinned down. Arrows everywhere.$B$BWhat''s yer order?',
            'Interesting.$B$BNext situation. Enemy archers have yer troops pinned down. Arrows everywhere.$B$BWhat''s yer order?'),
  (9195124, 'Interesting.$B$BThe enemy''s marching a whole army through one narrow pass. They''re packed shoulder to shoulder.$B$BHow do ye greet them?',
            'Interesting.$B$BThe enemy''s marching a whole army through one narrow pass. They''re packed shoulder to shoulder.$B$BHow do ye greet them?'),
  (9195125, 'Interesting.$B$BYer supply wagons are lost. The troops are exhausted, and ye''ve one last chance to turn the battle.$B$BWhat''s the plan?',
            'Interesting.$B$BYer supply wagons are lost. The troops are exhausted, and ye''ve one last chance to turn the battle.$B$BWhat''s the plan?'),
  (9195127, 'Ah. The commander who sacked Ironforge.$B$BI''ve reorganized me forces since. Keep that in mind.',
            'Ah. The commander who sacked Ironforge.$B$BI''ve reorganized me forces since. Keep that in mind.'),
  (9195126, 'Interesting.$B$BLast situation. Both armies are tangled together. Ye can barely tell ally from enemy.$B$BHow do ye settle this mess?',
            'Interesting.$B$BLast situation. Both armies are tangled together. Ye can barely tell ally from enemy.$B$BHow do ye settle this mess?');
INSERT INTO `npc_text` (`ID`, `BroadcastTextID0`, `Probability0`) VALUES
  (9195120, 9195120, 1), (9195121, 9195121, 1), (9195122, 9195122, 1),
  (9195123, 9195123, 1), (9195124, 9195124, 1), (9195125, 9195125, 1), (9195126, 9195126, 1),
  (9195127, 9195127, 1);

DELETE FROM `quest_template` WHERE `entry` IN (919721, 919722, 919723);
DELETE FROM `creature_questrelation` WHERE `quest` IN (919721, 919722, 919723);
DELETE FROM `creature_involvedrelation` WHERE `quest` IN (919721, 919722, 919723);
INSERT INTO `quest_template`
  (`entry`, `Method`, `ZoneOrSort`, `MinLevel`, `QuestLevel`, `RequiredRaces`, `QuestFlags`, `SpecialFlags`,
   `PrevQuestId`, `NextQuestInChain`, `ReqItemId1`, `ReqItemCount1`, `ReqItemId2`, `ReqItemCount2`,
   `RewXP`, `RewOrReqMoney`, `RewMoneyMaxLevel`, `RewItemId1`, `RewItemCount1`,
   `Title`, `Details`, `Objectives`, `ObjectiveText1`, `RequestItemsText`, `OfferRewardText`)
VALUES
  (919721, 2, -9100, 10, 0, 589, 0, 2,
   919714, 919722, 0, 0, 0, 0,
   500, 0, 0, 0, 0,
   'Know Your Forces',
   'Ibelin taught ye the rules, did he? Good. Rules are the easy part.$B$BI gave orders for thirty years, $N. Most of them kept men alive. One of them didn''t, and I still go over it in my head.$B$BThis game is the first battlefield where I can give a bad order and nobody gets buried for it. So I take it seriously. I want to know how ye command.$B$BFive situations, answered as ye would on the field. First one: the enemy has three minions. Ye''ve got one. What''s yer next move?',
   'Answer Steeg Haskell''s five tactical questions in Ironforge.',
   'Discuss tactics with Steeg Haskell',
   'Five situations, commander. I need to hear yer orders for all of them.',
   'Interesting.$B$BNot what I would have done. But then, what I would have done has occasionally required a new regiment.$B$BWe''ll put yer strategic mind to the test. First, we need supplies.'),
  (919722, 2, -9100, 10, 0, 589, 0, 0,
   919721, 919723, 2596, 1, 117, 5,
   500, 0, 0, 0, 0,
   'An Army Marches on Its Stomach',
   'Every battle I ever lost, I lost on the supply line. Never on the field. Remember that.$B$BInnkeeper Firebrew is downstairs. Have him fill this order: one Skin of Dwarven Stout, five pieces of Tough Jerky. Bring them straight back. Our forces are depending on ye.$B$BThe stout is for morale. The jerky is for... the army.',
   'Buy one Skin of Dwarven Stout and five Tough Jerky from Innkeeper Firebrew downstairs, then bring them to Steeg Haskell.',
   '',
   'The provisions, commander. A thirsty army is a mutinous army.',
   'Excellent. Put the ale beside me cards. The jerky too.$B$BFor the soldiers? No, no. They''re pictures, $N.$B$BI''m the one commanding.$B$BCan''t command on an empty stomach.'),
  (919723, 2, -9100, 10, 0, 589, 0, 2,
   919722, 0, 0, 0, 0, 0,
   500, 0, 0, 919620, 1,
   'War Games',
   'Supplies secured. Forces assembled. Table reasonably level.$B$BHere is why I do this at a card table. Out there, a lost battle costs names on a wall. Here it costs a deck and some pride, and I can set up the board and try again. That mercy never came to me in the field.$B$BSo play like it matters anyway. Challenge me. If ye win, ye''ve earned a Warrior Deck of yer own. If ye lose, we''ll call it reconnaissance.',
   'Defeat Steeg Haskell in a Hearthstone duel, then return to him for your Warrior Deck. Right-click the deck to unlock Warrior for your account.',
   'Defeat Steeg Haskell at Hearthstone',
   'The exercise isn''t over until one commander wins. Ready for another attempt?',
   '...Ironforge has suffered a catastrophic defeat.$B$BAgain?$B$BOh. Ye want yer reward first.$B$BTake the deck. A good commander has to be willing to lose a few battles learning, and ye can afford to lose them here. Do it often.');

INSERT INTO `creature_questrelation` (`id`, `quest`) VALUES
  (919506, 919721), (919506, 919722), (919506, 919723);
INSERT INTO `creature_involvedrelation` (`id`, `quest`) VALUES
  (919506, 919721), (919506, 919722), (919506, 919723);

-- Direct duels are only for Alliance characters with War Games incomplete (Normal difficulty).
-- Turning in War Games adds Steeg to the account's Alliance finder roster (Normal and Hard AI).
-- Also generated in migration 0002 from data/quests/class_unlocks.json, so rebuilding retains the roster entry.
DELETE FROM `hearthstone_opponent` WHERE `creature_entry` = 919506;
DELETE FROM `hearthstone_npc_unlock` WHERE `creature_entry` = 919506;
INSERT INTO `hearthstone_npc_unlock` (`creature_entry`, `quest_id`, `deck_key`, `faction`) VALUES
  (919506, 919723, 'normal_warrior', 'alliance');
