-- Horde Warrior unlock: Victory Above All.
-- Teacher 919507 and standing actors 919508 / 919509 are defined in 0013 and placed in 0015.
-- Native quests 919731-919733, level 10, Horde races, 500 XP each, after the Horde lesson 919704.
-- Gossip starts a 60-second scene. Quest 2 requires the quest-gated deck pickup at Sarlek.
-- Quest 3 uses the existing Basic Warrior deck and Normal AI. No deck or battle AI changes.
-- Permanent box placement is in 0017. Opening reward 919620 unlocks Warrior account-wide.

UPDATE `creature_template` SET `gossip_menu_id` = 65007 WHERE `entry` = 919507;
DELETE FROM `gossip_menu` WHERE `entry` = 65007;
INSERT INTO `gossip_menu` (`entry`, `text_id`) VALUES (65007, 9195130);
DELETE FROM `npc_text` WHERE `ID` = 9195130;
DELETE FROM `broadcast_text` WHERE `entry` = 9195130;
INSERT INTO `broadcast_text` (`entry`, `male_text`, `female_text`) VALUES
  (9195130, 'A battlefield does not wait while you think. Neither should a card table.$B$BWhat do you want?',
            'A battlefield does not wait while you think. Neither should a card table.$B$BWhat do you want?');
INSERT INTO `npc_text` (`ID`, `BroadcastTextID0`, `Probability0`) VALUES (9195130, 9195130, 1);

DELETE FROM `item_template` WHERE `entry` = 919630;
INSERT INTO `item_template`
  (`entry`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`,
   `buy_price`, `sell_price`, `max_count`, `stackable`, `bonding`, `material`, `other_team_entry`, `script_name`)
VALUES
  (919630, 12, 0, 'Veteran''s Hearthstone Deck',
   'Smells of ale. Several cards have circular stains.', 69179, 1,
   0, 0, 1, 1, 4, 1, 0, '');

-- Small chest model, custom click handler supplies one deck only while the retrieval quest is incomplete.
DELETE FROM `gameobject_template` WHERE `entry` = 919050;
-- Type 3 enables the installed LootSparkle effect. Native quest activation is data8.
-- Scripted per-character delivery keeps this permanent box ready for other viewers.
INSERT INTO `gameobject_template` (`entry`, `type`, `displayId`, `name`, `size`, `data1`, `data3`, `data8`, `script_name`)
VALUES (919050, 3, 10, 'Veteran''s Deck Box', 0.4, 0, 0, 919732, '');

DELETE FROM `quest_template` WHERE `entry` IN (919731, 919732, 919733);
DELETE FROM `creature_questrelation` WHERE `quest` IN (919731, 919732, 919733);
DELETE FROM `creature_involvedrelation` WHERE `quest` IN (919731, 919732, 919733);
INSERT INTO `quest_template`
  (`entry`, `Method`, `ZoneOrSort`, `MinLevel`, `QuestLevel`, `RequiredRaces`, `QuestFlags`, `SpecialFlags`,
   `PrevQuestId`, `NextQuestInChain`, `ReqItemId1`, `ReqItemCount1`,
   `RewXP`, `RewOrReqMoney`, `RewMoneyMaxLevel`, `RewItemId1`, `RewItemCount1`,
   `Title`, `Details`, `Objectives`, `ObjectiveText1`, `RequestItemsText`, `OfferRewardText`)
VALUES
  (919731, 2, -9100, 10, 0, 434, 0, 2,
   919704, 919732, 0, 0,
   500, 0, 0, 0, 0,
   'A Moment''s Thought',
   'You know the rules? Good. Rules never won a war.$B$BI have fought in three of them. In none of them did anyone give me a minute to think before I swung. The enemy did not give me a minute.$B$BThis game is getting soft. Too much thinking, not enough hitting. Watch those two at the table behind me. Stay close and see the whole thing. Then you will understand what is wrong.',
   'Talk to Zerok and stay nearby until the demonstration ends.',
   'Watch the veteran''s demonstration',
   'You have to watch this.',
   'One minute. One whole minute and nobody hit anything.$B$BThat is what I''m talking about.$B$BI''ll show you how a real orc plays.'),
  (919732, 2, -9100, 10, 0, 434, 0, 0,
   919731, 919733, 919630, 1,
   500, 0, 0, 0, 0,
   'A Veteran of Three Wars',
   'Last opponent I played was Sarlek. He is across from Jandi''s Arboretum, the herbalist''s place here in the city. I left my deck there. Look for my little deck box.$B$BI learned this game in my first war, from a grunt who taught me with his own deck. He did not come home. His deck did, and it has been in my pack through every war since.$B$BThen Sarlek made coasters of it. Go fetch it, and tell him those are CARDS. Not coasters.',
   'Retrieve the Veteran''s Hearthstone Deck, and return to the veteran.',
   'Retrieve the Veteran''s Hearthstone Deck',
   'My deck. Sarlek. Across from Jandi''s Arboretum. Stop thinking about it and go get it.',
   'Ale stains. Again.$B$BThree wars, and the greatest threat to this deck is a mug.$B$BGood. It is back where it belongs.'),
  (919733, 2, -9100, 10, 0, 434, 0, 2,
   919732, 0, 0, 0,
   500, 0, 0, 919620, 1,
   'Victory Above All',
   'Enough watching. Enough fetching. Sit down and play.$B$BChallenge me to a game of Hearthstone and win. I will be playing the old grunt''s cards, so do not expect mercy. He never gave me any either.$B$BDo not make me watch you think for another minute.',
   'Defeat the Veteran Zerok at Hearthstone.',
   'Defeat the veteran at Hearthstone',
   'We are not done until you win.',
   'Good. You wasted less time thinking than I did.$B$BHe would have liked you. He would also have told you to hit harder.$B$BTake the deck. Lok''tar.');

INSERT INTO `creature_questrelation` (`id`, `quest`) VALUES
  (919507, 919731), (919507, 919732), (919507, 919733);
INSERT INTO `creature_involvedrelation` (`id`, `quest`) VALUES
  (919507, 919731), (919507, 919732), (919507, 919733);

-- Direct challenges are quest-only. Final turn-in unlocks the account Horde random roster.
-- Also generated in 0002 from data/quests/class_unlocks.json.
DELETE FROM `hearthstone_opponent` WHERE `creature_entry` = 919507;
DELETE FROM `hearthstone_npc_unlock` WHERE `creature_entry` = 919507;
INSERT INTO `hearthstone_npc_unlock` (`creature_entry`, `quest_id`, `deck_key`, `faction`) VALUES
  (919507, 919733, 'normal_warrior', 'horde');
