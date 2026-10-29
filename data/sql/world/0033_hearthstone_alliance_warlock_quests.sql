-- Alliance Warlock: owner places Maren 919561, Nib 919562 and Owen 919563.
-- Placeholder models. No spawns, new locations, paths or existing vendor/trainer changes.
DELETE FROM `creature_template` WHERE `entry` IN (919561,919562,919563,919567,919568,919569);
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
  IF(c.`flags`=0,33554432,b.`unit_flags`),IF(c.`entry`=919562,3,b.`type`),b.`regeneration`,0,c.`menu`,'',0,''
FROM `creature_template` b CROSS JOIN (
  SELECT 919561 AS `entry`,5092 AS `display`,'Maren Blackquill' AS `name`,'Hearthstone teacher' AS `subname`,3 AS `flags`,65048 AS `menu`
  UNION ALL SELECT 919562,4449,'Nib','Maren''s imp',3,65063
  UNION ALL SELECT 919563,1490,'Owen Wellwick','Reluctant witness',1,65050
  UNION ALL SELECT 919567,1469,'HS Warlock Witness Signed','',0,0
  UNION ALL SELECT 919568,1469,'HS Warlock Witness Reported','',0,0
  UNION ALL SELECT 919569,1469,'HS Warlock Fee Agreed','',0,0
) c WHERE b.`entry`=919502;

DELETE FROM `gossip_menu` WHERE `entry` IN (65048,65049,65050,65054,65055,65056,65057,65059,65060,65061,65062,65063,65064);
DELETE FROM `npc_text` WHERE `ID` IN (9195332,9195333,9195334,9195338,9195339,9195340,9195341,9195343,9195354,9195355,9195356,9195357,9195358,9195359,9195360,9195361,9195362,9195363,9195364,9195365,9195366);
DELETE FROM `broadcast_text` WHERE `entry` IN (9195332,9195333,9195334,9195338,9195339,9195340,9195341,9195343,9195354,9195355,9195356,9195357,9195358,9195359,9195360,9195361,9195362,9195363,9195364,9195365,9195366);
INSERT INTO `broadcast_text` (`entry`,`male_text`,`female_text`) VALUES
  (9195332,'Come to learn how a Warlock plays Hearthstone? Good. Before we begin, there''s a small matter involving my imp.$B$BHe''s decided that helping with a lesson makes him a paid professional.','Come to learn how a Warlock plays Hearthstone? Good. Before we begin, there''s a small matter involving my imp.$B$BHe''s decided that helping with a lesson makes him a paid professional.'),
  (9195333,'We have an agreement! A terrible, insulting agreement!$B$BBut an agreement nonetheless.$B$BNow where is my flask?','We have an agreement! A terrible, insulting agreement!$B$BBut an agreement nonetheless.$B$BNow where is my flask?'),
  (9195334,'Maren sent you, didn''t she?$B$BI knew that imp was trouble the moment he started counting on his fingers. He has more fingers than he ought to.','Maren sent you, didn''t she?$B$BI knew that imp was trouble the moment he started counting on his fingers. He has more fingers than he ought to.'),
  (9195338,'Come to learn how a Warlock plays Hearthstone? Good. Before we begin, there''s a small matter involving my imp.$B$BHe''s decided that helping with a lesson makes him a paid professional.','Come to learn how a Warlock plays Hearthstone? Good. Before we begin, there''s a small matter involving my imp.$B$BHe''s decided that helping with a lesson makes him a paid professional.'),
  (9195339,'Well? Did Owen agree, or has he decided to leave Stormwind altogether?','Well? Did Owen agree, or has he decided to leave Stormwind altogether?'),
  (9195340,'Ah, yes. Nib was terribly disappointed when I made him return it.$B$BGood. Now we have someone who remembers the original price. That should make the next part considerably easier.','Ah, yes. Nib was terribly disappointed when I made him return it.$B$BGood. Now we have someone who remembers the original price. That should make the next part considerably easier.'),
  (9195341,'Now comes the difficult part.$B$BNib knows perfectly well that we agreed on one flask of port. He simply believes that complaining loudly enough will earn him a second.$B$BJarel Moor sells port upstairs. Fetch a flask, then speak to Nib down here and make him honor the original agreement.$B$BDon''t give him anything until he agrees to the price. And whatever you do, don''t let him introduce any new terms.$B$BConsider this your first practical lesson in dealing with demons.','Now comes the difficult part.$B$BNib knows perfectly well that we agreed on one flask of port. He simply believes that complaining loudly enough will earn him a second.$B$BJarel Moor sells port upstairs. Fetch a flask, then speak to Nib down here and make him honor the original agreement.$B$BDon''t give him anything until he agrees to the price. And whatever you do, don''t let him introduce any new terms.$B$BConsider this your first practical lesson in dealing with demons.'),
  (9195343,'The terms are settled, Nib has been paid, and we can finally begin.$B$BReady to put what you''ve learned into practice?$B$BNib: "IF SHE LOSES, I DEMAND ANOTHER FLASK!"$B$BMaren: "You were paid to assist with one lesson, Nib. Not to set the stakes."$B$BNib: "THEN I SHALL WATCH DISAPPROVINGLY!"$B$BMaren: "That was already expected."','The terms are settled, Nib has been paid, and we can finally begin.$B$BReady to put what you''ve learned into practice?$B$BNib: "IF SHE LOSES, I DEMAND ANOTHER FLASK!"$B$BMaren: "You were paid to assist with one lesson, Nib. Not to set the stakes."$B$BNib: "THEN I SHALL WATCH DISAPPROVINGLY!"$B$BMaren: "That was already expected."'),
  (9195354,'Maren sent you, didn''t she?$B$BI knew that imp was trouble the moment he started counting on his fingers. He has more fingers than he ought to.','Maren sent you, didn''t she?$B$BI knew that imp was trouble the moment he started counting on his fingers. He has more fingers than he ought to.'),
  (9195355,'I heard the agreement. One flask of port, nothing more.$B$BBut the last time I signed something for that creature, I spent three days arguing that my hat was not part of the payment.','I heard the agreement. One flask of port, nothing more.$B$BBut the last time I signed something for that creature, I spent three days arguing that my hat was not part of the payment.'),
  (9195356,'Just a witness, then? No obligations? No mysterious clauses about my belongings?$B$B...Very well. I''ll sign.$B$BAnd tell Maren that if he tries to renegotiate, I''m going to pretend I never learned to write.','Just a witness, then? No obligations? No mysterious clauses about my belongings?$B$B...Very well. I''ll sign.$B$BAnd tell Maren that if he tries to renegotiate, I''m going to pretend I never learned to write.'),
  (9195357,'And tell Maren that if he tries to renegotiate, I''m going to pretend I never learned to write.','And tell Maren that if he tries to renegotiate, I''m going to pretend I never learned to write.'),
  (9195358,'Well? Did Owen agree, or has he decided to leave Stormwind altogether?','Well? Did Owen agree, or has he decided to leave Stormwind altogether?'),
  (9195359,'Ah, yes. Nib was terribly disappointed when I made him return it.$B$BGood. Now we have someone who remembers the original price. That should make the next part considerably easier.','Ah, yes. Nib was terribly disappointed when I made him return it.$B$BGood. Now we have someone who remembers the original price. That should make the next part considerably easier.'),
  (9195360,'AH! THE APPRENTICE!$B$BI''ve been expecting you. I''ve also been reconsidering my fee.$B$BOne flask of port is hardly adequate compensation for a demon of my considerable talents!','AH! THE APPRENTICE!$B$BI''ve been expecting you. I''ve also been reconsidering my fee.$B$BOne flask of port is hardly adequate compensation for a demon of my considerable talents!'),
  (9195361,'That was BEFORE I considered the risks!$B$BI demand TWO flasks! And a gold coin! And a proper title!$B$B''Nib the Magnificent, Senior Advisor on All Matters Infernal.'' That has a lovely sound to it.','That was BEFORE I considered the risks!$B$BI demand TWO flasks! And a gold coin! And a proper title!$B$B''Nib the Magnificent, Senior Advisor on All Matters Infernal.'' That has a lovely sound to it.'),
  (9195362,'YOU WOULDN''T DARE!$B$BShe''ll make me sit through another lecture about honoring agreements! Do you have any idea how LONG those are?$B$B...Fine! FINE! One flask of port! No coin! No title!$B$BBut I want it known that I negotiated under extreme emotional distress!','YOU WOULDN''T DARE!$B$BShe''ll make me sit through another lecture about honoring agreements! Do you have any idea how LONG those are?$B$B...Fine! FINE! One flask of port! No coin! No title!$B$BBut I want it known that I negotiated under extreme emotional distress!'),
  (9195363,'AGREED!$B$BNow where''s my port?','AGREED!$B$BNow where''s my port?'),
  (9195364,'Back for another game? Nib has been forbidden from negotiating the stakes.','Back for another game? Nib has been forbidden from negotiating the stakes.'),
  (9195365,'I WAS UNDERPAID! I''m considering a formal complaint. Once I find someone willing to write it.','I WAS UNDERPAID! I''m considering a formal complaint. Once I find someone willing to write it.'),
  (9195366,'You settled the business with that imp? Good. I''d rather not witness anything ever again.','You settled the business with that imp? Good. I''d rather not witness anything ever again.');
INSERT INTO `npc_text` (`ID`,`BroadcastTextID0`,`Probability0`)
SELECT `entry`,`entry`,1 FROM `broadcast_text` WHERE `entry` IN (9195332,9195333,9195334,9195338,9195339,9195340,9195341,9195343,9195354,9195355,9195356,9195357,9195358,9195359,9195360,9195361,9195362,9195363,9195364,9195365,9195366);
INSERT INTO `gossip_menu` (`entry`,`text_id`) VALUES
  (65048,9195332),
  (65049,9195333),
  (65050,9195334),
  (65054,9195338),
  (65055,9195339),
  (65056,9195340),
  (65057,9195341),
  (65059,9195343),
  (65060,9195364),
  (65061,9195365),
  (65062,9195366),
  (65063,9195360),
  (65064,9195357);

DELETE FROM `quest_template` WHERE `entry` BETWEEN 919841 AND 919843;
DELETE FROM `creature_questrelation` WHERE `quest` BETWEEN 919841 AND 919843;
DELETE FROM `creature_involvedrelation` WHERE `quest` BETWEEN 919841 AND 919843;
INSERT INTO `quest_template`
  (`entry`,`Method`,`ZoneOrSort`,`MinLevel`,`QuestLevel`,`RequiredRaces`,`RequiredClasses`,`QuestFlags`,`SpecialFlags`,
   `PrevQuestId`,`NextQuestInChain`,`ReqCreatureOrGOId1`,`ReqCreatureOrGOCount1`,
   `ReqCreatureOrGOId2`,`ReqCreatureOrGOCount2`,`ReqCreatureOrGOId3`,`ReqCreatureOrGOCount3`,
   `ReqItemId1`,`ReqItemCount1`,`RewXP`,`RewOrReqMoney`,`RewMoneyMaxLevel`,`RewItemId1`,`RewItemCount1`,
   `Title`,`Details`,`Objectives`,`ObjectiveText1`,`ObjectiveText2`,`ObjectiveText3`,`ObjectiveText4`,
   `RequestItemsText`,`OfferRewardText`)
VALUES
  (919841,2,-9100,10,0,589,0,0,0,919714,919842,919567,1,919568,1,0,0,0,0,500,0,0,0,0,
   'A Suitable Witness',
   'A good Warlock knows the value of a bargain. More importantly, they know when someone is trying to change it.$B$BMy imp, Nib, agreed to help with your lesson in exchange for one flask of port. A generous offer, considering all he has to do is behave himself.$B$BUnfortunately, he''s already begun demanding additional payment.$B$BOwen Wellwick was here when we agreed on the price. You''ll find him upstairs, probably wishing he''d never come downstairs in the first place.$B$BAsk him to confirm the original terms and sign as our witness. Nib is much less creative when someone remembers what he actually said.',
   'Obtain Owen Wellwick''s signature.$B$BReport back to Maren Blackquill.',
   'Obtain Owen Wellwick''s signature','Report back to Maren Blackquill','','',
   'Owen is upstairs. He''s a sensible man, which is precisely why he''s reluctant to be involved.',
   'Excellent. One witness, one agreed price.$B$BNow we need to convince Nib that an agreement means something after he''s made it.'),
  (919842,2,-9100,10,0,589,0,0,0,919841,919843,919569,1,0,0,0,0,2593,1,500,0,0,0,0,
   'A Small Consideration',
   'Now comes the difficult part.$B$BNib knows perfectly well that we agreed on one flask of port. He simply believes that complaining loudly enough will earn him a second.$B$BJarel Moor sells port upstairs. Fetch a flask, then speak to Nib down here and make him honor the original agreement.$B$BDon''t give him anything until he agrees to the price. And whatever you do, don''t let him introduce any new terms.$B$BConsider this your first practical lesson in dealing with demons.',
   'Convince Nib to accept the original fee.$B$BBring Nib one Flask of Port.',
   'Convince Nib to accept the original fee','','','',
   'We have an agreement! A terrible, insulting agreement!$B$BBut an agreement nonetheless.$B$BNow where is my flask?',
   'Ahhh. Finally! A proper payment!$B$BThere. My mark, right beside the others. Everything signed and settled.$B$BTell Maren I''m ready for the lesson.$B$BAnd tell her I was exceptionally reasonable!'),
  (919843,2,-9100,10,0,589,0,0,2,919842,0,0,0,0,0,0,0,0,0,500,0,0,919627,1,
   'Terms and Conditions Apply',
   'Well done. You managed to strike a bargain with an imp and, more remarkably, convince him to keep it.$B$BThat is the whole lesson. Power always comes at a price. The trick is knowing whether that price is worth paying. Hearthstone is no different.$B$BFace me in a game of Hearthstone. Watch what I''m willing to sacrifice, and decide when the cost becomes too great.$B$BDefeat me, and I''ll give you a Warlock deck of your own.',
   'Defeat Maren Blackquill in a game of Hearthstone.',
   'Defeat Maren at Hearthstone','','','',
   'A Warlock can gain a great deal by giving something up. Just remember that not every price is worth paying.$B$BReady for another game?',
   'Well played.$B$BYou''ve learned something Nib still struggles to understand: just because you can demand more doesn''t mean you should.$B$BYou''ve earned this deck. And don''t worry. There are no additional fees.');
INSERT INTO `creature_questrelation` (`id`,`quest`) VALUES (919561,919841),(919561,919842),(919561,919843);
INSERT INTO `creature_involvedrelation` (`id`,`quest`) VALUES (919561,919841),(919562,919842),(919561,919843);

-- Jarel Moor 1305 already sells port through vendor template 1500048. Leave all stock untouched.
DELETE FROM `hearthstone_opponent` WHERE `creature_entry`=919561;
DELETE FROM `hearthstone_npc_unlock` WHERE `creature_entry`=919561;
INSERT INTO `hearthstone_npc_unlock` (`creature_entry`,`quest_id`,`deck_key`,`faction`)
VALUES (919561,919843,'normal_warlock','alliance');
