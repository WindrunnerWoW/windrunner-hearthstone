-- Alliance Priest: Alenya 919578 at Temple of the Moon; Syrelle 919579 in Temple Gardens.
-- Owner-placed actor spawns recorded below; no new locations or existing service changes.
-- Credits 919580-919581 are bookkeeping only and must never be placed.
DELETE FROM `creature_template` WHERE `entry` IN (919578,919579,919580,919581);
INSERT INTO `creature_template`
  (`entry`,`display_id1`,`name`,`subname`,`level_min`,`level_max`,`health_min`,`health_max`,
   `mana_min`,`mana_max`,`armor`,`faction`,`npc_flags`,`speed_walk`,`speed_run`,
   `detection_range`,`call_for_help_range`,`dmg_min`,`dmg_max`,`attack_power`,
   `base_attack_time`,`ranged_attack_time`,`unit_class`,`unit_flags`,`type`,`regeneration`,
   `movement_type`,`gossip_menu_id`,`auras`,`phase_quest_id`,`script_name`)
SELECT c.`entry`,c.`display`,c.`name`,c.`subname`,b.`level_min`,b.`level_max`,b.`health_min`,b.`health_max`,
  b.`mana_min`,b.`mana_max`,b.`armor`,79,c.`flags`,b.`speed_walk`,b.`speed_run`,
  b.`detection_range`,b.`call_for_help_range`,b.`dmg_min`,b.`dmg_max`,b.`attack_power`,
  b.`base_attack_time`,b.`ranged_attack_time`,b.`unit_class`,
  IF(c.`flags`=0,33554432,b.`unit_flags`),b.`type`,b.`regeneration`,0,c.`menu`,'',0,''
FROM `creature_template` b CROSS JOIN (
  SELECT 919578 AS `entry`,2200 AS `display`,'Priestess Alenya Moonbrook' AS `name`,'Hearthstone teacher' AS `subname`,3 AS `flags`,65080 AS `menu`
  UNION ALL SELECT 919579,2306,'Captain Syrelle Starbough','Sentinel captain',1,65081
  UNION ALL SELECT 919580,2306,'HS Priest Syrelle Account Heard','',0,0
  UNION ALL SELECT 919581,2200,'HS Priest Account Reported','',0,0
) c WHERE b.`entry`=919502;

DELETE FROM `item_template` WHERE `entry`=919651;
INSERT INTO `item_template`
  (`entry`,`class`,`subclass`,`name`,`display_id`,`quality`,`flags`,`buy_price`,`sell_price`,
   `inventory_type`,`allowable_class`,`allowable_race`,`item_level`,`required_level`,
   `max_count`,`stackable`,`bonding`,`description`,`start_quest`,`script_name`)
VALUES (919651,12,0,'Maerith''s Cloak Clasp',7366,1,0,0,0,0,-1,-1,1,0,1,1,4,
  'A worn cloak clasp with a small notch beside the hinge.',0,'');

DELETE FROM `gossip_menu` WHERE `entry` IN (65080,65081,65082,65083,65084,65085,65086,65087,65088,65089,65090,65091,65092,65093,65094,65095,65096);
-- 9195409 is deliberately unused; do not delete or bind another owner's row there.
DELETE FROM `npc_text` WHERE `ID` IN (9195400,9195401,9195402,9195403,9195404,9195405,9195406,9195407,9195408,9195410,9195411,9195412,9195413,9195414,9195415,9195416,9195417,9195418,9195419,9195420,9195421);
DELETE FROM `broadcast_text` WHERE `entry` IN (9195400,9195401,9195402,9195403,9195404,9195405,9195406,9195407,9195408,9195410,9195411,9195412,9195413,9195414,9195415,9195416,9195417,9195418,9195419,9195420,9195421);
INSERT INTO `broadcast_text` (`entry`,`male_text`,`female_text`) VALUES
  (9195400,'Come to learn Hearthstone? Forgive me. My thoughts are elsewhere today.','Come to learn Hearthstone? Forgive me. My thoughts are elsewhere today.'),
  (9195401,'If you have business with the Temple, the priestesses can help you.','If you have business with the Temple, the priestesses can help you.'),
  (9195402,'Alenya sent you? Does she want another report?','Alenya sent you? Does she want another report?'),
  (9195403,'I ordered Maerith to hold the path while we brought the wounded through. I told her I would relieve her. Then the fighting cut us off.','I ordered Maerith to hold the path while we brought the wounded through. I told her I would relieve her. Then the fighting cut us off.'),
  (9195404,'Afterward. Too late. I brought back her cloak clasp. Every time I try to take it to Alenya, all I can think is that I gave the order.','Afterward. Too late. I brought back her cloak clasp. Every time I try to take it to Alenya, all I can think is that I gave the order.'),
  (9195405,'Tell her I meant to come back. Don''t tell her it was worth it.','Tell her I meant to come back. Don''t tell her it was worth it.'),
  (9195406,'Syrelle is in the Temple Gardens. Please ask her what happened.','Syrelle is in the Temple Gardens. Please ask her what happened.'),
  (9195407,'What did she tell you?','What did she tell you?'),
  (9195408,'Her clasp? I wondered what became of it.','Her clasp? I wondered what became of it.'),
  (9195410,'She brought something of Maerith''s home. I would like to have it.','She brought something of Maerith''s home. I would like to have it.'),
  (9195411,'Syrelle has Maerith''s clasp. She needn''t find an apology before giving it back.','Syrelle has Maerith''s clasp. She needn''t find an apology before giving it back.'),
  (9195412,'She wants it? Of course she does. I should have taken it to her myself.','She wants it? Of course she does. I should have taken it to her myself.'),
  (9195413,'Every time I reach for it, I hear myself promising Maerith I would relieve her. Then I put it away again.','Every time I reach for it, I hear myself promising Maerith I would relieve her. Then I put it away again.'),
  (9195414,'I know. I haven''t cleaned it. I couldn''t decide whether that would be kindness or another way of hiding what happened.','I know. I haven''t cleaned it. I couldn''t decide whether that would be kindness or another way of hiding what happened.'),
  (9195415,'It belongs with Alenya. I should never have made her wait for it.','It belongs with Alenya. I should never have made her wait for it.'),
  (9195416,'You''ll need room to carry it. I''ll keep it wrapped.','You''ll need room to carry it. I''ll keep it wrapped.'),
  (9195417,'I had forgotten how often I repaired that little hinge.','I had forgotten how often I repaired that little hinge.'),
  (9195418,'I owed her that clasp. I owe her an honest answer, too.','I owed her that clasp. I owe her an honest answer, too.'),
  (9195419,'I have my Priest deck ready. Take your time choosing yours. Apparently, it runs in the family.','I have my Priest deck ready. Take your time choosing yours. Apparently, it runs in the family.'),
  (9195420,'You won fairly. This Priest Deck is yours.','You won fairly. This Priest Deck is yours.'),
  (9195421,'I am still angry. I am also glad we played.','I am still angry. I am also glad we played.');
INSERT INTO `npc_text` (`ID`,`BroadcastTextID0`,`Probability0`)
SELECT `entry`,`entry`,1 FROM `broadcast_text` WHERE `entry` IN (9195400,9195401,9195402,9195403,9195404,9195405,9195406,9195407,9195408,9195410,9195411,9195412,9195413,9195414,9195415,9195416,9195417,9195418,9195419,9195420,9195421);
INSERT INTO `gossip_menu` (`entry`,`text_id`) VALUES
  (65080,9195400),(65081,9195401),(65082,9195402),(65083,9195405),
  (65084,9195406),(65085,9195407),(65086,9195408),(65087,9195410),
  (65088,9195411),(65089,9195412),(65090,9195415),(65091,9195417),
  (65092,9195418),(65093,9195419),(65094,9195420),(65095,9195421),
  (65096,9195418);

DELETE FROM `quest_template` WHERE `entry` IN (919861,919862,919863);
DELETE FROM `creature_questrelation` WHERE `quest` IN (919861,919862,919863);
DELETE FROM `creature_involvedrelation` WHERE `quest` IN (919861,919862,919863);
INSERT INTO `quest_template`
  (`entry`,`Method`,`ZoneOrSort`,`MinLevel`,`QuestLevel`,`RequiredRaces`,`RequiredClasses`,`QuestFlags`,`SpecialFlags`,
   `PrevQuestId`,`NextQuestInChain`,`ReqCreatureOrGOId1`,`ReqCreatureOrGOCount1`,
   `ReqCreatureOrGOId2`,`ReqCreatureOrGOCount2`,`ReqItemId1`,`ReqItemCount1`,
   `RewXP`,`RewOrReqMoney`,`RewMoneyMaxLevel`,`RewItemId1`,`RewItemCount1`,
   `Title`,`Details`,`Objectives`,`ObjectiveText1`,`ObjectiveText2`,`ObjectiveText3`,`ObjectiveText4`,
   `RequestItemsText`,`OfferRewardText`)
VALUES
  (919861,2,-9100,10,0,589,0,0,0,919714,919862,919580,1,919581,1,0,0,500,0,0,0,0,
   'The Last Order',
   'I have Syrelle''s report. Maerith held the path while the wounded withdrew. That is all it says about my sister''s last night.$B$BSyrelle used to come here with her after every patrol. Now she won''t meet my eyes.$B$BWould you speak with her in the Temple Gardens? I want to know what happened. I don''t need her to make it sound noble.',
   'Hear Captain Syrelle Starbough''s account, then report to Priestess Alenya Moonbrook.',
   'Hear Syrelle''s account','Report to Alenya','','',
   'Syrelle is in the Temple Gardens. I would rather hear something painful than keep guessing.',
   'She meant to return. I wish she had told me that herself.$B$BThank you for telling me plainly.'),
  (919862,2,-9100,10,0,589,0,0,0,919861,919863,0,0,0,0,919651,1,500,0,0,0,0,
   'What She Brought Back',
   'My sister''s cloak clasp should be here, with her things. Ask Syrelle for it. She doesn''t have to clean it or find the right words first.$B$BI gave it to Maerith before her first patrol. I would like it back.',
   'Bring Maerith''s Cloak Clasp to Priestess Alenya Moonbrook.',
   '','','','',
   'Has Syrelle given you the clasp? It has a small notch beside the hinge. Maerith always caught it on her bowstring.',
   'There''s the notch. I repaired that hinge twice.$B$BMaerith always said she would bring it back before it broke again.$B$BI think I''ll keep it as it is.'),
  (919863,2,-9100,10,0,589,0,0,2,919862,0,0,0,0,0,0,0,500,0,0,919624,1,
   'After the Watch',
   'Maerith and I used to play Hearthstone after her watch. She said I took too long over every turn. I told her patience was a virtue. She called it losing slowly.$B$BI have been missing those evenings. Would you play a game with me? Defeat me, and this Priest Deck is yours.',
   'Defeat Priestess Alenya Moonbrook in a game of Hearthstone.',
   'Defeat Alenya at Hearthstone','','','',
   'We can try another game when you''re ready.',
   'Thank you for the game, $n. I enjoyed it.$B$BThat feels strange to say today.$B$BTake this Priest Deck. You''ve earned it.');
INSERT INTO `creature_questrelation` (`id`,`quest`) VALUES (919578,919861),(919578,919862),(919578,919863);
INSERT INTO `creature_involvedrelation` (`id`,`quest`) VALUES (919578,919861),(919578,919862),(919578,919863);

-- Quest-only Basic Priest / Normal. Final reward unlocks this teacher in the
-- account's Alliance finder roster; opening the deck item unlocks the class.
-- Syrelle and credit templates never become ordinary opponents or roster members.
DELETE FROM `hearthstone_opponent` WHERE `creature_entry`=919578;
DELETE FROM `hearthstone_npc_unlock` WHERE `creature_entry`=919578;
INSERT INTO `hearthstone_npc_unlock` (`creature_entry`,`quest_id`,`deck_key`,`faction`)
VALUES (919578,919863,'normal_priest','alliance');

-- Owner-recorded Alliance Priest placements, 2026-10-10.
-- Source GUIDs 12585269/12585270 are provenance only; native GUID allocation is retained.
-- No attached addons, GUID/entry paths, groups, links, events or pools in the capture.
-- Replace only these two permanent actors; credit templates and the personal RP actor stay unspawned.
DELETE FROM `creature` WHERE `id` IN (919578,919579);
INSERT INTO `creature`
  (`id`,`id2`,`id3`,`id4`,`map`,`position_x`,`position_y`,`position_z`,`orientation`,`spawntimesecsmin`,`spawntimesecsmax`,`wander_distance`,`health_percent`,`mana_percent`,`movement_type`,`spawn_flags`,`visibility_mod`)
VALUES
  (919578,0,0,0,1,9628.580078125,2569.909912109375,1335.5999755859375,4.8599700927734375,120,120,0,100,100,0,0,0),
  (919579,0,0,0,1,9983.330078125,2548.860107421875,1314.8399658203125,1.0900599956512451,120,120,0,100,100,0,0,0);
