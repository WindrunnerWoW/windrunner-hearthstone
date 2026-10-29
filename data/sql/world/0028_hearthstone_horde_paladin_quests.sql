-- Horde Paladin: Drin Lia, a Forsaken scholar studying an Alliance card archetype.
-- Temporary female Forsaken display 2663 from Hannah Akeley (4575).
-- Owner placed Drin Lia manually; her requested spawn capture is recorded below.
-- No gameobject spawns or runtime objects.
DELETE FROM `creature_template` WHERE `entry` BETWEEN 919540 AND 919544;
INSERT INTO `creature_template`
  (`entry`,`display_id1`,`name`,`subname`,`level_min`,`level_max`,`health_min`,`health_max`,
   `armor`,`faction`,`npc_flags`,`speed_walk`,`speed_run`,`detection_range`,`call_for_help_range`,
   `dmg_min`,`dmg_max`,`attack_power`,`base_attack_time`,`ranged_attack_time`,`unit_class`,
   `unit_flags`,`type`,`regeneration`,`movement_type`,`gossip_menu_id`,`script_name`)
SELECT 919540,2663,'Drin Lia','Hearthstone researcher',
  `level_min`,`level_max`,`health_min`,`health_max`,`armor`,68,3,`speed_walk`,`speed_run`,
  `detection_range`,`call_for_help_range`,`dmg_min`,`dmg_max`,`attack_power`,`base_attack_time`,
  `ranged_attack_time`,`unit_class`,`unit_flags`,`type`,`regeneration`,0,65020,''
FROM `creature_template` WHERE `entry`=919501;
-- Native objective credit templates: bookkeeping only; never place these.
INSERT INTO `creature_template` (`entry`,`display_id1`,`name`,`level_min`,`level_max`,
  `health_min`,`health_max`,`faction`,`npc_flags`,`unit_flags`,`type`,`regeneration`,`movement_type`,`script_name`)
VALUES
  (919541,2663,'HS Paladin Libram Examined',1,1,1,1,68,0,33554432,7,3,0,''),
  (919542,2663,'HS Paladin Alliance Accounts Read',1,1,1,1,68,0,33554432,7,3,0,''),
  (919543,2663,'HS Paladin Practice Deck Arranged',1,1,1,1,68,0,33554432,7,3,0,''),
  (919544,2663,'HS Paladin Demonstration Reviewed',1,1,1,1,68,0,33554432,7,3,0,'');

DELETE FROM `gossip_menu` WHERE `entry` BETWEEN 65020 AND 65026;
INSERT INTO `gossip_menu` (`entry`,`text_id`) VALUES
  (65020,9195260),(65021,9195266),(65022,9195272),(65023,9195263),(65024,9195269),(65025,9195271),(65026,9195273);
DELETE FROM `npc_text` WHERE `ID` BETWEEN 9195260 AND 9195273;
DELETE FROM `broadcast_text` WHERE `entry` BETWEEN 9195260 AND 9195273;
INSERT INTO `broadcast_text` (`entry`,`male_text`,`female_text`) VALUES
  (9195260,'My notes on Paladins: extremely shiny, speak frequently of justice, difficult to kill, and apparently incapable of opening a conversation without mentioning the Light. A promising Hearthstone archetype.','My notes on Paladins: extremely shiny, speak frequently of justice, difficult to kill, and apparently incapable of opening a conversation without mentioning the Light. A promising Hearthstone archetype.'),
  (9195261,'This libram belonged to a Paladin. The clasp is polished enough to signal ships. I shall read the passage on blessings aloud when you are ready.','This libram belonged to a Paladin. The clasp is polished enough to signal ships. I shall read the passage on blessings aloud when you are ready.'),
  (9195262,'A blessing strengthens an ally. Sensible. I had assumed the shine distracted the enemy while somebody else did the fighting.','A blessing strengthens an ally. Sensible. I had assumed the shine distracted the enemy while somebody else did the fighting.'),
  (9195263,'Blessings improve the recipient, rather than the book. Recorded. Let us see whether the Alliance accounts support this alarming claim.','Blessings improve the recipient, rather than the book. Recorded. Let us see whether the Alliance accounts support this alarming claim.'),
  (9195264,'This Alliance account describes a Paladin shielding a companion and rallying soldiers. The author mentions justice six times before anyone draws a sword. Shall I read it aloud?','This Alliance account describes a Paladin shielding a companion and rallying soldiers. The author mentions justice six times before anyone draws a sword. Shall I read it aloud?'),
  (9195265,'The soldiers hold the line while their allies strengthen and protect them. Apparently the speeches are supplementary; the author seemed quite attached to them.','The soldiers hold the line while their allies strengthen and protect them. Apparently the speeches are supplementary; the author seemed quite attached to them.'),
  (9195266,'Allies, blessings, protection. I have enough evidence to arrange a practice deck. Bring me your conclusions and we shall begin.','Allies, blessings, protection. I have enough evidence to arrange a practice deck. Bring me your conclusions and we shall begin.'),
  (9195267,'Here are my practice cards. Before we begin, are you ready to help arrange the recruits, blessings, and protection?','Here are my practice cards. Before we begin, are you ready to help arrange the recruits, blessings, and protection?'),
  (9195268,'Silver Hand Recruits give us allies. Blessing of Might strengthens one; Hand of Protection helps one survive. Arrange them together, so even my notes can follow the plan.','Silver Hand Recruits give us allies. Blessing of Might strengthens one; Hand of Protection helps one survive. Arrange them together, so even my notes can follow the plan.'),
  (9195269,'The cards are arranged. Small allies, useful blessings, protection. Now I must establish the proper manner of playing them.','The cards are arranged. Small allies, useful blessings, protection. Now I must establish the proper manner of playing them.'),
  (9195270,'I shall demonstrate playing a blessing on an ally. Are you ready? I have rehearsed the opening declaration and checked that all my limbs are attached.','I shall demonstrate playing a blessing on an ally. Are you ready? I have rehearsed the opening declaration and checked that all my limbs are attached.'),
  (9195271,'Drin Lia waits for your assessment of her technique.','Drin Lia waits for your assessment of her technique.'),
  (9195272,'Drin Lia looks entirely satisfied with her demonstration.','Drin Lia looks entirely satisfied with her demonstration.'),
  (9195273,'Drin Lia is absorbed in her research. Give her a moment.','Drin Lia is absorbed in her research. Give her a moment.');
INSERT INTO `npc_text` (`ID`,`BroadcastTextID0`,`Probability0`) VALUES
  (9195260,9195260,1),
  (9195261,9195261,1),
  (9195262,9195262,1),
  (9195263,9195263,1),
  (9195264,9195264,1),
  (9195265,9195265,1),
  (9195266,9195266,1),
  (9195267,9195267,1),
  (9195268,9195268,1),
  (9195269,9195269,1),
  (9195270,9195270,1),
  (9195271,9195271,1),
  (9195272,9195272,1),
  (9195273,9195273,1);

DELETE FROM `quest_template` WHERE `entry` BETWEEN 919801 AND 919803;
DELETE FROM `creature_questrelation` WHERE `quest` BETWEEN 919801 AND 919803;
DELETE FROM `creature_involvedrelation` WHERE `quest` BETWEEN 919801 AND 919803;
INSERT INTO `quest_template`
  (`entry`,`Method`,`ZoneOrSort`,`MinLevel`,`QuestLevel`,`RequiredRaces`,`RequiredClasses`,`QuestFlags`,`SpecialFlags`,
   `PrevQuestId`,`NextQuestInChain`,`ReqCreatureOrGOId1`,`ReqCreatureOrGOCount1`,`ReqCreatureOrGOId2`,`ReqCreatureOrGOCount2`,
   `RewXP`,`RewOrReqMoney`,`RewMoneyMaxLevel`,`RewItemId1`,`RewItemCount1`,
   `Title`,`Details`,`Objectives`,`ObjectiveText1`,`ObjectiveText2`,`ObjectiveText3`,`ObjectiveText4`,
   `RequestItemsText`,`OfferRewardText`)
VALUES
  (919801,2,-9100,10,0,434,0,0,0,919704,919802,919541,1,919542,1,500,0,0,0,0,'Peer-Reviewed Radiance','There are no Paladins among us, but their Hearthstone cards keep turning up. My research suggests they are shiny, stubborn, and deeply concerned with justice.$B$BIn life I catalogued the archives of Lordaeron. The Silver Hand filled an entire shelf, and I never understood a word of it. I have a "borrowed" libram and several Alliance accounts here. Help me examine them. I would prefer to understand the cards before asking a Paladin. That could take all afternoon.','Examine the libram and read the Alliance accounts with Drin Lia.','Examine the libram','Read the Alliance accounts','','','The libram and accounts are here. Try to separate the tactics from the adjectives.','So they protect allies and make small soldiers useful. That is rather more practical than my first hypothesis.$B$BLet us try the cards.'),
  (919802,2,-9100,10,0,434,0,0,0,919801,919803,919543,1,919544,1,500,0,0,0,0,'A Reasonable Approximation','Our evidence suggests that a Paladin deck needs allies, blessings, and a way to keep those allies standing. I have gathered practice cards for exactly that purpose.$B$BHelp me arrange them. I have also prepared a suitably authoritative voice. Scholarship demands thoroughness.','Arrange the practice cards and review Drin Lia''s demonstration.','Arrange the practice deck','Review the demonstration','','','The practice cards are ready. I am waiting for an independent review of my technique.','You have raised concerns about my interpretation. I have written them in the margin.$B$BThe cards themselves appear sound. We shall settle the remaining questions at the table.'),
  (919803,2,-9100,10,0,434,0,0,2,919802,0,0,0,0,0,500,0,0,919621,1,'Silence. I Am a Paladin','The deck is assembled. The declarations are memorized. My evidence is ready for its most rigorous test: losing to someone who has actually read it.$B$BPlay Hearthstone with me and win. Try not to confuse volume with divine guidance.','Defeat Drin Lia in a Hearthstone duel.','Defeat Drin Lia at Hearthstone','','','','I remain difficult to kill. Difficult to beat is still under investigation. Shall we try again?','Your objections have survived testing. I shall lower my voice.$B$BTake this Paladin Deck.');
INSERT INTO `creature_questrelation` (`id`,`quest`) VALUES (919540,919801),(919540,919802),(919540,919803);
INSERT INTO `creature_involvedrelation` (`id`,`quest`) VALUES (919540,919801),(919540,919802),(919540,919803);

-- Shared generated policy: quest-only Basic Paladin / Normal; account Horde roster at final turn-in.
DELETE FROM `hearthstone_opponent` WHERE `creature_entry`=919540;
DELETE FROM `hearthstone_npc_unlock` WHERE `creature_entry`=919540;
INSERT INTO `hearthstone_npc_unlock` (`creature_entry`,`quest_id`,`deck_key`,`faction`)
VALUES (919540,919803,'normal_paladin','horde');

-- Owner requested capturing Drin Lia's manually placed spawn on 2026-10-06.
-- Source GUID 12585231 is provenance only; coordinates were read as DOUBLE.
-- Reuse a local Drin Lia GUID; allocate one only if no placement exists.
-- Stationary, no spawn addon, individual/special path or attached movement.
SET @hs_paladin_spawn_guid = (SELECT MIN(`guid`) FROM `creature` WHERE `id`=919540);
INSERT INTO `creature` (`id`,`id2`,`id3`,`id4`,`map`,`position_x`,`position_y`,`position_z`,`orientation`,`spawntimesecsmin`,`spawntimesecsmax`,`wander_distance`,`health_percent`,`mana_percent`,`movement_type`,`spawn_flags`,`visibility_mod`)
SELECT 919540,0,0,0,0,1507.699951171875,417.0929870605469,-62.185001373291016,3.6074700355529785,120,120,0,100,100,0,0,0 WHERE @hs_paladin_spawn_guid IS NULL;
SET @hs_paladin_spawn_guid = COALESCE(@hs_paladin_spawn_guid, LAST_INSERT_ID());
UPDATE `creature` SET `id`=919540, `id2`=0, `id3`=0, `id4`=0, `map`=0, `position_x`=1507.699951171875, `position_y`=417.0929870605469, `position_z`=-62.185001373291016, `orientation`=3.6074700355529785, `spawntimesecsmin`=120, `spawntimesecsmax`=120, `wander_distance`=0, `health_percent`=100, `mana_percent`=100, `movement_type`=0, `spawn_flags`=0, `visibility_mod`=0 WHERE `guid`=@hs_paladin_spawn_guid;
