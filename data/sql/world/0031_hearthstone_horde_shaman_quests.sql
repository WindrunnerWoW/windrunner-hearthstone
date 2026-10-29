-- Annie Foxstalker: native Horde Shaman quests. Owner places both Annie templates.
-- No creature/gameobject spawns, paths, trainer changes, vendor changes or new items.
DELETE FROM `creature_template` WHERE `entry` BETWEEN 919552 AND 919558;
INSERT INTO `creature_template`
  (`entry`,`display_id1`,`name`,`subname`,`level_min`,`level_max`,`health_min`,`health_max`,
   `mana_min`,`mana_max`,`armor`,`faction`,`npc_flags`,`speed_walk`,`speed_run`,
   `detection_range`,`call_for_help_range`,`dmg_min`,`dmg_max`,`attack_power`,
   `base_attack_time`,`ranged_attack_time`,`unit_class`,`unit_flags`,`type`,`regeneration`,
   `movement_type`,`gossip_menu_id`,`auras`,`phase_quest_id`,`script_name`)
SELECT c.`entry`,3780,c.`name`,c.`subname`,b.`level_min`,b.`level_max`,b.`health_min`,b.`health_max`,
  b.`mana_min`,b.`mana_max`,b.`armor`,104,c.`flags`,b.`speed_walk`,b.`speed_run`,
  b.`detection_range`,b.`call_for_help_range`,b.`dmg_min`,b.`dmg_max`,b.`attack_power`,
  b.`base_attack_time`,b.`ranged_attack_time`,b.`unit_class`,
  IF(c.`flags`=0,33554432,b.`unit_flags`),b.`type`,b.`regeneration`,0,c.`menu`,
  IF(c.`entry`=919553,'9617',''),IF(c.`entry`=919553,919824,0),
  ''
FROM `creature_template` b CROSS JOIN (
  SELECT 919552 AS `entry`,'Annie Foxstalker' AS `name`,'Aspiring shaman' AS `subname`,3 AS `flags`,65038 AS `menu`
  UNION ALL SELECT 919553,'Spirit of Annie Foxstalker','Hearthstone teacher',3,65039
  UNION ALL SELECT 919554,'HS Annie Siln Heard','',0,0
  UNION ALL SELECT 919555,'HS Annie Beram Heard','',0,0
  UNION ALL SELECT 919556,'HS Annie Tigor Heard','',0,0
  UNION ALL SELECT 919557,'HS Annie Introduction Practiced','',0,0
  UNION ALL SELECT 919558,'HS Annie Silence Shared','',0,0
) c WHERE b.`entry`=919501;

DELETE FROM `gossip_menu` WHERE `entry` BETWEEN 65038 AND 65045;
DELETE FROM `npc_text` WHERE `ID` BETWEEN 9195300 AND 9195327;
DELETE FROM `broadcast_text` WHERE `entry` BETWEEN 9195300 AND 9195327;
INSERT INTO `broadcast_text` (`entry`,`male_text`,`female_text`) VALUES
  (9195300,'Annie watches the teachers on Spirit Rise. When she notices you, she quickly looks away.','Annie watches the teachers on Spirit Rise. When she notices you, she quickly looks away.'),
  (9195301,'Good to see you again. I''ve been working on a Shaman deck. Care for a game?','Good to see you again. I''ve been working on a Shaman deck. Care for a game?'),
  (9195302,'Did you get a chance to speak with them?','Did you get a chance to speak with them?'),
  (9195303,'That sounds better. I think I''ll keep it short when I speak to Siln.','That sounds better. I think I''ll keep it short when I speak to Siln.'),
  (9195304,'Just the wind today. It was pleasant up here, though. Thank you for joining me.','Just the wind today. It was pleasant up here, though. Thank you for joining me.'),
  (9195305,'Greetings. What brings you to Spirit Rise?','Greetings. What brings you to Spirit Rise?'),
  (9195306,'Welcome. Have you come to learn?','Welcome. Have you come to learn?'),
  (9195307,'Come closer. What can I do for you?','Come closer. What can I do for you?'),
  (9195310,'Annie? Yes. She always waits until my students have gone. Then she leaves before I can greet her.','Annie? Yes. She always waits until my students have gone. Then she leaves before I can greet her.'),
  (9195311,'I have seen her stop to help people who would not have noticed her passing by. There is goodness in that girl. Ask what she would say if she came to me for lessons. Let her try it with you first.','I have seen her stop to help people who would not have noticed her passing by. There is goodness in that girl. Ask what she would say if she came to me for lessons. Let her try it with you first.'),
  (9195312,'Tell her I would be glad to hear it.','Tell her I would be glad to hear it.'),
  (9195313,'I know Annie. She listens from the edge of the rise, long after the others have gone. That patience will serve her well.','I know Annie. She listens from the edge of the rise, long after the others have gone. That patience will serve her well.'),
  (9195314,'Sit with her and listen. The wind, the birds, whatever is there. She needn''t find an answer today; learning to listen is a beginning.','Sit with her and listen. The wind, the birds, whatever is there. She needn''t find an answer today; learning to listen is a beginning.'),
  (9195315,'A little time spent listening will do her good.','A little time spent listening will do her good.'),
  (9195316,'Annie notices when someone needs a hand. I have seen her stop while others carried on. There is more to being a shaman than calling on the elements.','Annie notices when someone needs a hand. I have seen her stop while others carried on. There is more to being a shaman than calling on the elements.'),
  (9195317,'Let her choose someone to help. The gesture matters, whether it is small or great. She already has something to offer.','Let her choose someone to help. The gesture matters, whether it is small or great. She already has something to offer.'),
  (9195318,'I see promise in her. She already has the right instincts.','I see promise in her. She already has the right instincts.'),
  (9195320,'My name is Annie Foxstalker. I''d like to...$B$BNo, he knows my name. Let me start again.','My name is Annie Foxstalker. I''d like to...$B$BNo, he knows my name. Let me start again.'),
  (9195321,'I''ve listened to the lessons on the rise for a long time. I''d like to understand the elements, and how to help people with what I learn.','I''ve listened to the lessons on the rise for a long time. I''d like to understand the elements, and how to help people with what I learn.'),
  (9195322,'Siln, I''d like to learn from you. When could we begin?','Siln, I''d like to learn from you. When could we begin?'),
  (9195323,'Annie mouths the sentence once more, then nods.','Annie mouths the sentence once more, then nods.'),
  (9195324,'Annie settles beside you and closes her eyes. The wind moves over Spirit Rise.','Annie settles beside you and closes her eyes. The wind moves over Spirit Rise.'),
  (9195325,'The wind sounds different up here. Do you hear anything else?','The wind sounds different up here. Do you hear anything else?'),
  (9195326,'I hear only the wind too. Shall we stay a little longer?','I hear only the wind too. Shall we stay a little longer?'),
  (9195327,'Then let us listen a little longer.','Then let us listen a little longer.');
INSERT INTO `npc_text` (`ID`,`BroadcastTextID0`,`Probability0`)
SELECT `entry`,`entry`,1 FROM `broadcast_text` WHERE `entry` BETWEEN 9195300 AND 9195327;
INSERT INTO `gossip_menu` (`entry`,`text_id`) VALUES
  (65038,9195300),(65039,9195301),(65040,9195302),(65041,9195303),
  (65042,9195304),(65043,9195305),(65044,9195306),(65045,9195307);

DELETE FROM `quest_template` WHERE `entry` BETWEEN 919821 AND 919825;
DELETE FROM `creature_questrelation` WHERE `quest` BETWEEN 919821 AND 919825;
DELETE FROM `creature_involvedrelation` WHERE `quest` BETWEEN 919821 AND 919825;
INSERT INTO `quest_template`
  (`entry`,`Method`,`ZoneOrSort`,`MinLevel`,`QuestLevel`,`RequiredRaces`,`RequiredClasses`,`QuestFlags`,`SpecialFlags`,
   `PrevQuestId`,`NextQuestInChain`,`ReqCreatureOrGOId1`,`ReqCreatureOrGOCount1`,
   `ReqCreatureOrGOId2`,`ReqCreatureOrGOCount2`,`ReqCreatureOrGOId3`,`ReqCreatureOrGOCount3`,
   `ReqItemId1`,`ReqItemCount1`,`RewXP`,`RewOrReqMoney`,`RewMoneyMaxLevel`,`RewItemId1`,`RewItemCount1`,
   `Title`,`Details`,`Objectives`,`ObjectiveText1`,`ObjectiveText2`,`ObjectiveText3`,`ObjectiveText4`,
   `RequestItemsText`,`OfferRewardText`)
VALUES
  (919821,2,-9100,10,0,434,0,0,0,919704,919822,919554,1,919555,1,919556,1,0,0,500,0,0,0,0,
   'Words She Cannot Say',
   'Siln is often teaching when I pass. I don''t like to interrupt. Beram and Tigor are up there too; I''ve listened to them a few times.$B$BI''d like to learn from them. Would you speak to them about me? My name is Annie Foxstalker.',
   'Speak to Siln Skychaser, Beram Skychaser, and Tigor Skychaser on Spirit Rise, then return to Annie Foxstalker.',
   'Ask Siln about Annie','Ask Beram about Annie','Ask Tigor about Annie','',
   'Did you get a chance to speak with them?',
   'They''ve seen me, then. I hadn''t realized. Siln''s suggestion sounds simple enough.'),
  (919822,2,-9100,10,0,434,0,0,0,919821,919823,919557,1,0,0,0,0,0,0,500,0,0,0,0,
   'A Voice That Trembles',
   'Siln asked what I''d say if I came to him for lessons. I''ve been turning it over since you left.$B$BWould you hear it first? It probably doesn''t need to be this long.',
   'Help Annie practice her introduction.','Practice the introduction','','','',
   'Give me a moment. I had it a minute ago.',
   'I''ll use that. Before I go up, shall we try what Beram suggested?'),
  (919823,2,-9100,10,0,434,0,0,0,919822,919824,919558,1,0,0,0,0,0,0,500,0,0,0,0,
   'Room for Silence',
   'Beram suggested we listen to what''s around us. I usually come up here hoping to hear something in particular.$B$BThere''s a good breeze today. Will you join me for a while?',
   'Listen beside Annie and hear what she has to say afterward.','Listen beside Annie','','','',
   'Have you a moment to sit with me?',
   'Just the wind today. It was pleasant up here, though. Thank you for joining me.'),
  (919824,2,-9100,10,0,434,0,0,0,919823,919825,0,0,0,0,0,0,159,1,500,0,0,0,0,
   'Something I Can Give',
   'There''s a trader resting by the road below the rise. He''s had a long journey. I think I''ll see whether he could use some company, or a hand with his things.$B$BWould you help me get something to take along? Naal Mistrunner''s shop should have what I need.',
   'Bring one Refreshing Spring Water to Annie Foxstalker.','','','','',
   'I''ll head down once I''ve got something for him.',
   'Thank you. I''d best be going; he may be getting ready to leave. I''ll tell you how it went when I get back.'),
  (919825,2,-9100,10,0,434,0,0,2,919824,0,0,0,0,0,0,0,0,0,500,0,0,919625,1,
   'A Seat for Annie',
   'The trader told me about his family while we packed his things. I keep thinking they must still be waiting for him.$B$BI never got to take those lessons. Siln brought me a Shaman deck, though, and I''ve been learning to play. Would you join me? I''d like to teach someone, for a change.',
   'Defeat the Spirit of Annie Foxstalker in a Hearthstone duel.','Defeat Annie at Hearthstone','','','',
   'Stay for another hand? I am still learning too.',
   'A good game. Take this Shaman Deck; I have my own set. If anyone asks, you can show them what you''ve learned.');
INSERT INTO `creature_questrelation` (`id`,`quest`) VALUES
  (919552,919821),(919552,919822),(919552,919823),(919552,919824),(919553,919825);
INSERT INTO `creature_involvedrelation` (`id`,`quest`) VALUES
  (919552,919821),(919552,919822),(919552,919823),(919552,919824),(919553,919825);

-- Only the spirit joins the account Horde roster. Direct Normal play is quest-only.
DELETE FROM `hearthstone_opponent` WHERE `creature_entry` IN (919552,919553);
DELETE FROM `hearthstone_npc_unlock` WHERE `creature_entry` IN (919552,919553);
INSERT INTO `hearthstone_npc_unlock` (`creature_entry`,`quest_id`,`deck_key`,`faction`)
VALUES (919553,919825,'normal_shaman','horde');
