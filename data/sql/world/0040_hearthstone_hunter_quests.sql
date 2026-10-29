-- Hunter: owner-placed Boro/Nali in Hunter Rise; Fenn/Eris in Cenarion Enclave.
-- The owner's recorded permanent placements are exported at the end of this migration.
-- Animal images are private temporary actors. Never place image or bookkeeping templates.
DELETE FROM `creature_template` WHERE `entry` IN (919582,919583,919586,919587,919588,919589,919590,919900,919901,919902,919903,919904,919905,919906,919907,919908,919584,919585,919591,919592,919593,919594,919595,919909,919910,919911,919912,919913,919914,919915,919916,919917);
INSERT INTO `creature_template`
 (`entry`,`display_id1`,`name`,`subname`,`level_min`,`level_max`,`health_min`,`health_max`,
  `mana_min`,`mana_max`,`armor`,`faction`,`npc_flags`,`speed_walk`,`speed_run`,`scale`,
  `detection_range`,`call_for_help_range`,`dmg_min`,`dmg_max`,`attack_power`,`base_attack_time`,
  `ranged_attack_time`,`unit_class`,`unit_flags`,`type`,`regeneration`,`movement_type`,
  `gossip_menu_id`,`auras`,`phase_quest_id`,`script_name`)
SELECT c.`entry`,c.`display`,c.`name`,c.`subname`,b.`level_min`,b.`level_max`,b.`health_min`,b.`health_max`,
 b.`mana_min`,b.`mana_max`,b.`armor`,104,c.`flags`,b.`speed_walk`,b.`speed_run`,c.`scale`,
 b.`detection_range`,b.`call_for_help_range`,IF(c.`flags`=0,0,b.`dmg_min`),IF(c.`flags`=0,0,b.`dmg_max`),
 IF(c.`flags`=0,0,b.`attack_power`),b.`base_attack_time`,b.`ranged_attack_time`,b.`unit_class`,
 IF(c.`flags`=0,33554432,b.`unit_flags`),b.`type`,b.`regeneration`,0,c.`menu`,c.`auras`,0,''
FROM `creature_template` b CROSS JOIN (
  SELECT 919582 AS `entry`,4511 AS `display`,'Boro Softstep' AS `name`,'Hearthstone teacher' AS `subname`,3 AS `flags`,65100 AS `menu`,'' AS `auras`,1.35 AS `scale`
  UNION ALL SELECT 919583 AS `entry`,9269 AS `display`,'Nali Softstep' AS `name`,'Boro''s niece' AS `subname`,1 AS `flags`,65101 AS `menu`,'' AS `auras`,1.25 AS `scale`
  UNION ALL SELECT 919586 AS `entry`,4511 AS `display`,'HS Hunter Boro Softstep step 1' AS `name`,'' AS `subname`,0 AS `flags`,0 AS `menu`,'' AS `auras`,1 AS `scale`
  UNION ALL SELECT 919587 AS `entry`,4511 AS `display`,'HS Hunter Boro Softstep step 2' AS `name`,'' AS `subname`,0 AS `flags`,0 AS `menu`,'' AS `auras`,1 AS `scale`
  UNION ALL SELECT 919588 AS `entry`,4511 AS `display`,'HS Hunter Boro Softstep step 3' AS `name`,'' AS `subname`,0 AS `flags`,0 AS `menu`,'' AS `auras`,1 AS `scale`
  UNION ALL SELECT 919589 AS `entry`,4511 AS `display`,'HS Hunter Boro Softstep step 4' AS `name`,'' AS `subname`,0 AS `flags`,0 AS `menu`,'' AS `auras`,1 AS `scale`
  UNION ALL SELECT 919590 AS `entry`,4511 AS `display`,'HS Hunter Boro Softstep step 5' AS `name`,'' AS `subname`,0 AS `flags`,0 AS `menu`,'' AS `auras`,1 AS `scale`
  UNION ALL SELECT 919900 AS `entry`,8843 AS `display`,'Bear' AS `name`,'' AS `subname`,0 AS `flags`,0 AS `menu`,'9617' AS `auras`,0.75 AS `scale`
  UNION ALL SELECT 919901 AS `entry`,1105 AS `display`,'Buzzard' AS `name`,'' AS `subname`,0 AS `flags`,0 AS `menu`,'9617' AS `auras`,0.75 AS `scale`
  UNION ALL SELECT 919902 AS `entry`,320 AS `display`,'Tiger' AS `name`,'' AS `subname`,0 AS `flags`,0 AS `menu`,'9617' AS `auras`,0.75 AS `scale`
  UNION ALL SELECT 919903 AS `entry`,161 AS `display`,'Wolf' AS `name`,'' AS `subname`,0 AS `flags`,0 AS `menu`,'9617' AS `auras`,0.75 AS `scale`
  UNION ALL SELECT 919904 AS `entry`,381 AS `display`,'Boar' AS `name`,'' AS `subname`,0 AS `flags`,0 AS `menu`,'9617' AS `auras`,0.75 AS `scale`
  UNION ALL SELECT 919905 AS `entry`,1244 AS `display`,'Turtle' AS `name`,'' AS `subname`,0 AS `flags`,0 AS `menu`,'9617' AS `auras`,0.75 AS `scale`
  UNION ALL SELECT 919906 AS `entry`,901 AS `display`,'Frog' AS `name`,'' AS `subname`,0 AS `flags`,0 AS `menu`,'9617' AS `auras`,0.75 AS `scale`
  UNION ALL SELECT 919907 AS `entry`,304 AS `display`,'Chicken' AS `name`,'' AS `subname`,0 AS `flags`,0 AS `menu`,'9617' AS `auras`,0.75 AS `scale`
  UNION ALL SELECT 919908 AS `entry`,4511 AS `display`,'Tauren Accountant' AS `name`,'' AS `subname`,0 AS `flags`,0 AS `menu`,'9617' AS `auras`,1.35 AS `scale`
) c WHERE b.`entry`=919501;
INSERT INTO `creature_template`
 (`entry`,`display_id1`,`name`,`subname`,`level_min`,`level_max`,`health_min`,`health_max`,
  `mana_min`,`mana_max`,`armor`,`faction`,`npc_flags`,`speed_walk`,`speed_run`,`scale`,
  `detection_range`,`call_for_help_range`,`dmg_min`,`dmg_max`,`attack_power`,`base_attack_time`,
  `ranged_attack_time`,`unit_class`,`unit_flags`,`type`,`regeneration`,`movement_type`,
  `gossip_menu_id`,`auras`,`phase_quest_id`,`script_name`)
SELECT c.`entry`,c.`display`,c.`name`,c.`subname`,b.`level_min`,b.`level_max`,b.`health_min`,b.`health_max`,
 b.`mana_min`,b.`mana_max`,b.`armor`,79,c.`flags`,b.`speed_walk`,b.`speed_run`,c.`scale`,
 b.`detection_range`,b.`call_for_help_range`,IF(c.`flags`=0,0,b.`dmg_min`),IF(c.`flags`=0,0,b.`dmg_max`),
 IF(c.`flags`=0,0,b.`attack_power`),b.`base_attack_time`,b.`ranged_attack_time`,b.`unit_class`,
 IF(c.`flags`=0,33554432,b.`unit_flags`),b.`type`,b.`regeneration`,0,c.`menu`,c.`auras`,0,''
FROM `creature_template` b CROSS JOIN (
  SELECT 919584 AS `entry`,7041 AS `display`,'Fenn Coilwhistle' AS `name`,'Hearthstone teacher' AS `subname`,3 AS `flags`,65160 AS `menu`,'' AS `auras`,1 AS `scale`
  UNION ALL SELECT 919585 AS `entry`,2306 AS `display`,'Eris Leafshade' AS `name`,'Sentinel' AS `subname`,1 AS `flags`,65161 AS `menu`,'' AS `auras`,1 AS `scale`
  UNION ALL SELECT 919591 AS `entry`,7041 AS `display`,'HS Hunter Fenn Coilwhistle step 1' AS `name`,'' AS `subname`,0 AS `flags`,0 AS `menu`,'' AS `auras`,1 AS `scale`
  UNION ALL SELECT 919592 AS `entry`,7041 AS `display`,'HS Hunter Fenn Coilwhistle step 2' AS `name`,'' AS `subname`,0 AS `flags`,0 AS `menu`,'' AS `auras`,1 AS `scale`
  UNION ALL SELECT 919593 AS `entry`,7041 AS `display`,'HS Hunter Fenn Coilwhistle step 3' AS `name`,'' AS `subname`,0 AS `flags`,0 AS `menu`,'' AS `auras`,1 AS `scale`
  UNION ALL SELECT 919594 AS `entry`,7041 AS `display`,'HS Hunter Fenn Coilwhistle step 4' AS `name`,'' AS `subname`,0 AS `flags`,0 AS `menu`,'' AS `auras`,1 AS `scale`
  UNION ALL SELECT 919595 AS `entry`,7041 AS `display`,'HS Hunter Fenn Coilwhistle step 5' AS `name`,'' AS `subname`,0 AS `flags`,0 AS `menu`,'' AS `auras`,1 AS `scale`
  UNION ALL SELECT 919909 AS `entry`,5554 AS `display`,'Cat' AS `name`,'' AS `subname`,0 AS `flags`,0 AS `menu`,'9617' AS `auras`,0.75 AS `scale`
  UNION ALL SELECT 919910 AS `entry`,4615 AS `display`,'Owl' AS `name`,'' AS `subname`,0 AS `flags`,0 AS `menu`,'9617' AS `auras`,0.75 AS `scale`
  UNION ALL SELECT 919911 AS `entry`,709 AS `display`,'Spider' AS `name`,'' AS `subname`,0 AS `flags`,0 AS `menu`,'9617' AS `auras`,0.75 AS `scale`
  UNION ALL SELECT 919912 AS `entry`,999 AS `display`,'Crab' AS `name`,'' AS `subname`,0 AS `flags`,0 AS `menu`,'9617' AS `auras`,0.75 AS `scale`
  UNION ALL SELECT 919913 AS `entry`,807 AS `display`,'Crocolisk' AS `name`,'' AS `subname`,0 AS `flags`,0 AS `menu`,'9617' AS `auras`,0.75 AS `scale`
  UNION ALL SELECT 919914 AS `entry`,328 AS `display`,'Rabbit' AS `name`,'' AS `subname`,0 AS `flags`,0 AS `menu`,'9617' AS `auras`,0.75 AS `scale`
  UNION ALL SELECT 919915 AS `entry`,1141 AS `display`,'Rat' AS `name`,'' AS `subname`,0 AS `flags`,0 AS `menu`,'9617' AS `auras`,0.75 AS `scale`
  UNION ALL SELECT 919916 AS `entry`,856 AS `display`,'Sheep' AS `name`,'' AS `subname`,0 AS `flags`,0 AS `menu`,'9617' AS `auras`,0.75 AS `scale`
  UNION ALL SELECT 919917 AS `entry`,7041 AS `display`,'Gnome Accountant' AS `name`,'' AS `subname`,0 AS `flags`,0 AS `menu`,'9617' AS `auras`,1 AS `scale`
) c WHERE b.`entry`=919502;
DELETE FROM `gossip_menu` WHERE `entry` IN (65100,65101,65102,65103,65105,65106,65107,65108,65109,65110,65111,65112,65113,65114,65115,65116,65117,65118,65119,65120,65121,65122,65123,65124,65125,65126,65127,65128,65129,65130,65131,65132,65104,65160,65161,65162,65163,65165,65166,65167,65168,65169,65170,65171,65172,65173,65174,65175,65176,65177,65178,65179,65180,65181,65182,65183,65184,65185,65186,65187,65188,65189,65190,65191,65192,65164);
DELETE FROM `npc_text` WHERE `ID` IN (9195430,9195431,9195432,9195433,9195435,9195436,9195437,9195438,9195439,9195440,9195441,9195442,9195443,9195444,9195445,9195446,9195447,9195448,9195449,9195450,9195451,9195452,9195453,9195454,9195455,9195456,9195457,9195458,9195459,9195460,9195461,9195462,9195434,9195490,9195491,9195492,9195493,9195495,9195496,9195497,9195498,9195499,9195500,9195501,9195502,9195503,9195504,9195505,9195506,9195507,9195508,9195509,9195510,9195511,9195512,9195513,9195514,9195515,9195516,9195517,9195518,9195519,9195520,9195521,9195522,9195494);
DELETE FROM `broadcast_text` WHERE `entry` IN (9195430,9195431,9195432,9195433,9195435,9195436,9195437,9195438,9195439,9195440,9195441,9195442,9195443,9195444,9195445,9195446,9195447,9195448,9195449,9195450,9195451,9195452,9195453,9195454,9195455,9195456,9195457,9195458,9195459,9195460,9195461,9195462,9195434,9195490,9195491,9195492,9195493,9195495,9195496,9195497,9195498,9195499,9195500,9195501,9195502,9195503,9195504,9195505,9195506,9195507,9195508,9195509,9195510,9195511,9195512,9195513,9195514,9195515,9195516,9195517,9195518,9195519,9195520,9195521,9195522,9195494);
INSERT INTO `broadcast_text` (`entry`,`male_text`,`female_text`) VALUES
 (9195430,'Come to learn how a Hunter plays Hearthstone? I begin with companions. Nali says I take a long time to begin.','Come to learn how a Hunter plays Hearthstone? I begin with companions. Nali says I take a long time to begin.'),
 (9195431,'Uncle Boro can describe a companion for an hour. He can describe why he would not choose it for two.','Uncle Boro can describe a companion for an hour. He can describe why he would not choose it for two.'),
 (9195432,'We have not finished comparing companions. Shall I go on?','We have not finished comparing companions. Shall I go on?'),
 (9195433,'Nali is nearby. Let us find out what all this was supposed to accomplish.','Nali is nearby. Let us find out what all this was supposed to accomplish.'),
 (9195435,'What did Nali actually ask for? I would rather hear her words than another of my splendid ideas.','What did Nali actually ask for? I would rather hear her words than another of my splendid ideas.'),
 (9195436,'No more speeches about companions. I have brought the cards.','No more speeches about companions. I have brought the cards.'),
 (9195437,'Nali says I am allowed to explain my choices after I have made them. This seems fair. Shall we play?','Nali says I am allowed to explain my choices after I have made them. This seems fair. Shall we play?'),
 (9195438,'You won fairly. Your Hunter Deck is ready. I have no further questions about animals.','You won fairly. Your Hunter Deck is ready. I have no further questions about animals.'),
 (9195439,'Good to see you, $n. Nali has started asking me what my lessons are for. I may have taught her rather too well.','Good to see you, $n. Nali has started asking me what my lessons are for. I may have taught her rather too well.'),
 (9195440,'He finally played. I still like the boar. I like Uncle Boro better when he stops examining everyone.','He finally played. I still like the boar. I like Uncle Boro better when he stops examining everyone.'),
 (9195441,'He has stopped comparing companions. Now I might finally see the cards.','He has stopped comparing companions. Now I might finally see the cards.'),
 (9195442,'I have lost my place. Let us consider those three again.','I have lost my place. Let us consider those three again.'),
 (9195443,'It is easy to get carried away. Shall we start that part again?','It is easy to get carried away. Shall we start that part again?'),
 (9195444,'Has he reached the accountant? Then you have been here a while.','Has he reached the accountant? Then you have been here a while.'),
 (9195445,'I asked to see how he plays his Hunter deck. One game. He said we had to choose the proper companion first.','I asked to see how he plays his Hunter deck. One game. He said we had to choose the proper companion first.'),
 (9195446,'I chose the boar. He said it was wrong. Then he put a boar in his own deck and said that was different. I would like him to show me the difference by playing.','I chose the boar. He said it was wrong. Then he put a boar in his own deck and said that was different. I would like him to show me the difference by playing.'),
 (9195447,'Thank you. If he starts another examination, tell him the boar has already passed mine.','Thank you. If he starts another examination, tell him the boar has already passed mine.'),
 (9195448,'Well? A grand hunt? A tiger?','Well? A grand hunt? A tiger?'),
 (9195449,'One game. I could have managed that without the accountant.','One game. I could have managed that without the accountant.'),
 (9195450,'Yes. I have been telling her what a Hunter should notice while missing the person standing beside me. No more auditions.','Yes. I have been telling her what a Hunter should notice while missing the person standing beside me. No more auditions.'),
 (9195451,'A bear, a buzzard and a tiger. Which would you choose?','A bear, a buzzard and a tiger. Which would you choose?'),
 (9195452,'Wrong. The bear would take you. It would decide you were carrying lunch.','Wrong. The bear would take you. It would decide you were carrying lunch.'),
 (9195453,'Wrong. It waits for you to die before becoming helpful. I prefer a companion with a little more urgency.','Wrong. It waits for you to die before becoming helpful. I prefer a companion with a little more urgency.'),
 (9195454,'Wrong. A magnificent hunter, but it expects the finest cut of everything. Including your blanket.','Wrong. A magnificent hunter, but it expects the finest cut of everything. Including your blanket.'),
 (9195455,'A wolf, a boar and a turtle. More modest appetites. Mostly. Which one?','A wolf, a boar and a turtle. More modest appetites. Mostly. Which one?'),
 (9195456,'Wrong. It howls when it finds something. By the time it has finished announcing breakfast, breakfast has left.','Wrong. It howls when it finds something. By the time it has finished announcing breakfast, breakfast has left.'),
 (9195457,'Wrong. It is very good at finding things underground. I was hoping to keep most of the ground where it is.','Wrong. It is very good at finding things underground. I was hoping to keep most of the ground where it is.'),
 (9195458,'Wrong. An excellent companion for yesterday''s hunt. We would arrive next week.','Wrong. An excellent companion for yesterday''s hunt. We would arrive next week.'),
 (9195459,'A frog, a chicken and a tauren accountant. Keep an open mind. The accountant is very good at following people.','A frog, a chicken and a tauren accountant. Keep an open mind. The accountant is very good at following people.'),
 (9195460,'Wrong. It could catch a fly, but I doubt it would share.','Wrong. It could catch a fly, but I doubt it would share.'),
 (9195461,'Wrong. The chicken would be the bait. I do not like its chances.','Wrong. The chicken would be the bait. I do not like its chances.'),
 (9195462,'Wrong. He finds every copper you have ever earned, then charges you for the discovery.','Wrong. He finds every copper you have ever earned, then charges you for the discovery.'),
 (9195434,'That is a fair question. Nali asked me for a lesson. Somewhere between the bear and the accountant, I stopped asking what she wanted to learn.','That is a fair question. Nali asked me for a lesson. Somewhere between the bear and the accountant, I stopped asking what she wanted to learn.'),
 (9195490,'I teach Hunter play by describing animals and asking which one a student would take. Eris, who reads her reports over there, says I do it too loudly. I think she needs a companion of her own, something quiet. I am choosing one.','I teach Hunter play by describing animals and asking which one a student would take. Eris, who reads her reports over there, says I do it too loudly. I think she needs a companion of her own, something quiet. I am choosing one.'),
 (9195491,'Fenn is planning another lesson. If he starts one while you are nearby, find somewhere else to sit.','Fenn is planning another lesson. If he starts one while you are nearby, find somewhere else to sit.'),
 (9195492,'We have not finished choosing Eris''s companion. Shall I go on?','We have not finished choosing Eris''s companion. Shall I go on?'),
 (9195493,'Eris is nearby in the Cenarion Enclave. Go and ask her what she needs. She is usually very precise about problems.','Eris is nearby in the Cenarion Enclave. Go and ask her what she needs. She is usually very precise about problems.'),
 (9195495,'What does Eris need? Keep it short. I have done enough talking this week.','What does Eris need? Keep it short. I have done enough talking this week.'),
 (9195496,'Cards make no noise at all. Eris has approved of them in principle.','Cards make no noise at all. Eris has approved of them in principle.'),
 (9195497,'Ready? I have an ordinary Hunter deck. If I lose, that will be my own fault. Eris made me promise to say so out loud.','Ready? I have an ordinary Hunter deck. If I lose, that will be my own fault. Eris made me promise to say so out loud.'),
 (9195498,'You won. Your Hunter Deck is ready. Eris says it was a fair game, which surprised us both.','You won. Your Hunter Deck is ready. Eris says it was a fair game, which surprised us both.'),
 (9195499,'Good to see you, $n. The lessons are shorter now, and Eris has stopped complaining. Mostly.','Good to see you, $n. The lessons are shorter now, and Eris has stopped complaining. Mostly.'),
 (9195500,'Cards. No long speeches. I may even finish this report before he offers me a game.','Cards. No long speeches. I may even finish this report before he offers me a game.'),
 (9195501,'He listened. That is worth encouraging. Preferably with something that fits in his pocket.','He listened. That is worth encouraging. Preferably with something that fits in his pocket.'),
 (9195502,'One moment. I have lost my place.','One moment. I have lost my place.'),
 (9195503,'Give me a moment. I have lost my train of thought.','Give me a moment. I have lost my train of thought.'),
 (9195504,'Please tell me he has finished talking.','Please tell me he has finished talking.'),
 (9195505,'Quiet. He describes things so well that I stop reading and listen. I asked him for less talking. He has spent all week choosing me a companion instead.','Quiet. He describes things so well that I stop reading and listen. I asked him for less talking. He has spent all week choosing me a companion instead.'),
 (9195506,'No companion. I like Fenn. I would just like to finish one report without a lesson going on beside me.','No companion. I like Fenn. I would just like to finish one report without a lesson going on beside me.'),
 (9195507,'Thank you. He is a good friend. He is also a great deal of noise for one gnome.','Thank you. He is a good friend. He is also a great deal of noise for one gnome.'),
 (9195508,'Well? What does she need? Did she pick a companion?','Well? What does she need? Did she pick a companion?'),
 (9195509,'No companion at all? Then what if I keep my voice very low?','No companion at all? Then what if I keep my voice very low?'),
 (9195510,'All right. No more lessons at her desk. I spent a week looking for a clever answer to a question she never asked.','All right. No more lessons at her desk. I spent a week looking for a clever answer to a question she never asked.'),
 (9195511,'First set: a cat, an owl and a spider. All quiet. Which would you bring her?','First set: a cat, an owl and a spider. All quiet. Which would you bring her?'),
 (9195512,'Wrong. Quiet on its paws, but it would sit on Eris''s reports. Cats always find the page that matters.','Wrong. Quiet on its paws, but it would sit on Eris''s reports. Cats always find the page that matters.'),
 (9195513,'Wrong. Silent wings, but it would watch Eris read all evening. Nobody finishes a report under that stare.','Wrong. Silent wings, but it would watch Eris read all evening. Nobody finishes a report under that stare.'),
 (9195514,'Wrong. Quiet, yes, but Eris would find it in her papers by morning, and I would hear about it.','Wrong. Quiet, yes, but Eris would find it in her papers by morning, and I would hear about it.'),
 (9195515,'Second set: a crab, a crocolisk and a rabbit. Which would you bring her?','Second set: a crab, a crocolisk and a rabbit. Which would you bring her?'),
 (9195516,'Wrong. Those claws click on every stone step. Eris would hear it coming from the far end of the Enclave.','Wrong. Those claws click on every stone step. Eris would hear it coming from the far end of the Enclave.'),
 (9195517,'Wrong. Patient, until it is not, and then it thrashes. The Enclave is meant to be a quiet place.','Wrong. Patient, until it is not, and then it thrashes. The Enclave is meant to be a quiet place.'),
 (9195518,'Wrong. It bolts at the smallest noise, even a page turning, and knocks something over on the way out.','Wrong. It bolts at the smallest noise, even a page turning, and knocks something over on the way out.'),
 (9195519,'Last set: a rat, a sheep and a gnome accountant. Which would you bring her?','Last set: a rat, a sheep and a gnome accountant. Which would you bring her?'),
 (9195520,'Wrong. It would be inside Eris''s report cabinet within the hour.','Wrong. It would be inside Eris''s report cabinet within the hour.'),
 (9195521,'Wrong. Calm, until it is left alone. Then it bleats, and Eris would be the one who went to see why.','Wrong. Calm, until it is left alone. Then it bleats, and Eris would be the one who went to see why.'),
 (9195522,'Wrong. I have costed that one. Quiet and tidy, but it would audit Eris''s reports, and she would never forgive me.','Wrong. I have costed that one. Quiet and tidy, but it would audit Eris''s reports, and she would never forgive me.'),
 (9195494,'...No. She asked for quiet, and I decided she needed company. I never asked what she actually wants. Perhaps you should.','...No. She asked for quiet, and I decided she needed company. I never asked what she actually wants. Perhaps you should.');
INSERT INTO `npc_text` (`ID`,`BroadcastTextID0`,`Probability0`) SELECT `entry`,`entry`,1 FROM `broadcast_text` WHERE `entry` IN (9195430,9195431,9195432,9195433,9195435,9195436,9195437,9195438,9195439,9195440,9195441,9195442,9195443,9195444,9195445,9195446,9195447,9195448,9195449,9195450,9195451,9195452,9195453,9195454,9195455,9195456,9195457,9195458,9195459,9195460,9195461,9195462,9195434,9195490,9195491,9195492,9195493,9195495,9195496,9195497,9195498,9195499,9195500,9195501,9195502,9195503,9195504,9195505,9195506,9195507,9195508,9195509,9195510,9195511,9195512,9195513,9195514,9195515,9195516,9195517,9195518,9195519,9195520,9195521,9195522,9195494);
INSERT INTO `gossip_menu` (`entry`,`text_id`) VALUES
 (65100,9195430),
 (65101,9195431),
 (65102,9195432),
 (65103,9195433),
 (65105,9195435),
 (65106,9195436),
 (65107,9195437),
 (65108,9195438),
 (65109,9195439),
 (65110,9195440),
 (65111,9195441),
 (65112,9195442),
 (65113,9195443),
 (65114,9195444),
 (65115,9195445),
 (65116,9195446),
 (65117,9195447),
 (65118,9195448),
 (65119,9195449),
 (65120,9195450),
 (65121,9195451),
 (65122,9195452),
 (65123,9195453),
 (65124,9195454),
 (65125,9195455),
 (65126,9195456),
 (65127,9195457),
 (65128,9195458),
 (65129,9195459),
 (65130,9195460),
 (65131,9195461),
 (65132,9195462),
 (65104,9195434),
 (65160,9195490),
 (65161,9195491),
 (65162,9195492),
 (65163,9195493),
 (65165,9195495),
 (65166,9195496),
 (65167,9195497),
 (65168,9195498),
 (65169,9195499),
 (65170,9195500),
 (65171,9195501),
 (65172,9195502),
 (65173,9195503),
 (65174,9195504),
 (65175,9195505),
 (65176,9195506),
 (65177,9195507),
 (65178,9195508),
 (65179,9195509),
 (65180,9195510),
 (65181,9195511),
 (65182,9195512),
 (65183,9195513),
 (65184,9195514),
 (65185,9195515),
 (65186,9195516),
 (65187,9195517),
 (65188,9195518),
 (65189,9195519),
 (65190,9195520),
 (65191,9195521),
 (65192,9195522),
 (65164,9195494);
DELETE FROM `quest_template` WHERE `entry` IN (919871,919872,919873,919881,919882,919883);
DELETE FROM `creature_questrelation` WHERE `quest` IN (919871,919872,919873,919881,919882,919883);
DELETE FROM `creature_involvedrelation` WHERE `quest` IN (919871,919872,919873,919881,919882,919883);
INSERT INTO `quest_template` (`entry`,`Method`,`ZoneOrSort`,`MinLevel`,`QuestLevel`,`RequiredRaces`,`RequiredClasses`,`QuestFlags`,`SpecialFlags`,`PrevQuestId`,`NextQuestInChain`,`ReqCreatureOrGOId1`,`ReqCreatureOrGOCount1`,`ReqItemId1`,`ReqItemCount1`,`ReqCreatureOrGOId2`,`ReqCreatureOrGOCount2`,`ReqItemId2`,`ReqItemCount2`,`ReqCreatureOrGOId3`,`ReqCreatureOrGOCount3`,`ReqItemId3`,`ReqItemCount3`,`ReqCreatureOrGOId4`,`ReqCreatureOrGOCount4`,`ReqItemId4`,`ReqItemCount4`,`RewXP`,`RewOrReqMoney`,`RewMoneyMaxLevel`,`RewItemId1`,`RewItemCount1`,`Title`,`Details`,`Objectives`,`ObjectiveText1`,`ObjectiveText2`,`ObjectiveText3`,`ObjectiveText4`,`RequestItemsText`,`OfferRewardText`) VALUES
 (919871,2,-9100,10,0,434,0,0,0,919704,919872,919586,1,0,0,919587,1,0,0,919588,1,0,0,0,0,0,0,500,0,0,0,0,'A Fine Selection','My niece Nali asked for a Hunter lesson. A good lesson begins with a useful companion. I have given the matter considerable thought. $B$BI will describe a few candidates. Tell me which one you would choose. There is more to a companion than teeth, though I find teeth are a reliable place to begin.','Discuss Boro Softstep''s possible companions.','Compare Boro''s first set','Compare Boro''s second set','Compare Boro''s last set','','We still have a set to consider. I assure you, the next one is entirely reasonable.','Hm. We have learned a great deal about me and rather less about what Nali needs.$B$BPerhaps we should correct that.'),
 (919872,2,-9100,10,0,434,0,0,0,919871,919873,919589,1,0,0,919590,1,0,0,0,0,0,0,0,0,0,0,500,0,0,0,0,'What She Asked For','Nali is here on Hunter Rise, probably listening to the hunter trainers. She has been remarkably patient with my lesson. She may have been waiting for me to stop speaking.$B$BAsk her what she wanted when she came to me. I have had enough of listening to myself for one afternoon.','Ask Nali what she wanted from Boro''s lesson, then tell Boro.','Hear Nali''s request','Tell Boro what Nali asked','','','What did Nali actually ask for? I would rather hear her words than another of my splendid ideas.','I was choosing her companion before I had even heard her question. No wonder every answer seemed wrong.$B$BShe wanted a game. Let us give her one worth watching.'),
 (919873,2,-9100,10,0,434,0,0,2,919872,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,500,0,0,919622,1,'No More Auditions','Nali wanted to see how I play my Hunter deck. You have endured enough of my opinions to deserve the other side of the table.$B$BDefeat me, and this Hunter Deck is yours. I shall try to make my companions more useful than my questions.','Defeat Boro Softstep in a game of Hearthstone.','Defeat Boro at Hearthstone','','','','Another game? At last, a question with a sensible answer.','A fine game, $n. Nali was right to ask for the game first.$B$BTake this Hunter Deck. I will not ask which animal you intend to put in it.'),
 (919881,2,-9100,10,0,589,0,0,0,919714,919882,919591,1,0,0,919592,1,0,0,919593,1,0,0,0,0,0,0,500,0,0,0,0,'A Quieter Companion','I give Hunter lessons out here, and my friend Eris, a Sentinel, reads her reports nearby. She says my lessons are too loud. I have decided that what she needs is a companion of her own, something quiet to keep her company, so that she will be in a better mood for my teaching.$B$BI will go through three sets of candidates. Tell me which one you would bring her from each, and remember that she needs quiet to read.','Help Fenn Coilwhistle choose a quiet companion for Eris from his three sets.','Compare Fenn''s first set','Compare Fenn''s second set','Compare Fenn''s last set','','Another set is ready. Surely one of them is quiet enough for Eris.','I have rejected every one. You chose carefully, I will say that. The trouble is that I never checked what Eris actually wants.$B$BI should have started there.'),
 (919882,2,-9100,10,0,589,0,0,0,919881,919883,919594,1,0,0,919595,1,0,0,0,0,0,0,0,0,0,0,500,0,0,0,0,'Less Wildlife, Please','Eris is on duty in the Cenarion Enclave, though I suspect she is also trying to read her reports. I would like to know exactly what she needs.$B$BAsk her, then tell me what she said. Please do not soften it.','Ask Eris what she needs, then tell Fenn.','Hear Eris''s request','Tell Fenn what Eris needs','','','What does Eris need? Keep it short. I have done enough talking this week.','No lessons at her desk. Plain enough, isn''t it?$B$BVery well. If I want Eris to take my lessons seriously, I can begin by noticing when she is trying to read.'),
 (919883,2,-9100,10,0,589,0,0,2,919882,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,500,0,0,919622,1,'Keep It on the Cards','Eris can read in peace now, and I have had time to think. My lessons are best when I stop trying to impress people.$B$BLet us play Hearthstone. Defeat me, and this Hunter Deck is yours. Cards are much quieter than my lessons.','Defeat Fenn Coilwhistle in a game of Hearthstone.','Defeat Fenn at Hearthstone','','','','Shall we try again? I have resisted making another lesson of it.','A fine game, $n. My lessons improve whenever I stop trying to win people over.$B$BTake this Hunter Deck. If you pass Eris, tell her I lost. Quietly.');
INSERT INTO `creature_questrelation` (`id`,`quest`) VALUES (919582,919871),(919582,919872),(919582,919873),(919584,919881),(919584,919882),(919584,919883);
INSERT INTO `creature_involvedrelation` (`id`,`quest`) VALUES (919582,919871),(919582,919872),(919582,919873),(919584,919881),(919584,919882),(919584,919883);
-- Only teachers are quest opponents; the generated library owns ordinary finder bindings.
DELETE FROM `hearthstone_opponent` WHERE `creature_entry` IN (919582,919584);
DELETE FROM `hearthstone_npc_unlock` WHERE `creature_entry` IN (919582,919584);
INSERT INTO `hearthstone_npc_unlock` (`creature_entry`,`quest_id`,`deck_key`,`faction`) VALUES (919582,919873,'normal_hunter','horde'),(919584,919883,'normal_hunter','alliance');

-- Fenn's owner-authored walking demonstration. Timing/route/speech live in the handler;
-- these are the waiting pages, never additional objectives or permanent spawns.
DELETE FROM `gossip_menu` WHERE `entry` IN (65193,65194);
DELETE FROM `npc_text` WHERE `ID` IN (9195523,9195524);
DELETE FROM `broadcast_text` WHERE `entry` IN (9195523,9195524);
INSERT INTO `broadcast_text` (`entry`,`male_text`,`female_text`) VALUES
 (9195523,'Pay attention. This part takes imagination.','Pay attention. This part takes imagination.'),
 (9195524,'One lesson at a time. I have only the one pair of pointing hands.','One lesson at a time. I have only the one pair of pointing hands.');
INSERT INTO `npc_text` (`ID`,`BroadcastTextID0`,`Probability0`) VALUES (9195523,9195523,1),(9195524,9195524,1);
INSERT INTO `gossip_menu` (`entry`,`text_id`) VALUES (65193,9195523),(65194,9195524);

-- Boro uses the same staged presentation, with his own route and imagined-companion dialogue.
DELETE FROM `gossip_menu` WHERE `entry` IN (65133,65134);
DELETE FROM `npc_text` WHERE `ID` IN (9195463,9195464);
DELETE FROM `broadcast_text` WHERE `entry` IN (9195463,9195464);
INSERT INTO `broadcast_text` (`entry`,`male_text`,`female_text`) VALUES
 (9195463,'Listen carefully. We shall consider them one at a time.','Listen carefully. We shall consider them one at a time.'),
 (9195464,'One lesson at a time. Even I must finish a sentence eventually.','One lesson at a time. Even I must finish a sentence eventually.');
INSERT INTO `npc_text` (`ID`,`BroadcastTextID0`,`Probability0`) VALUES (9195463,9195463,1),(9195464,9195464,1);
INSERT INTO `gossip_menu` (`entry`,`text_id`) VALUES (65133,9195463),(65134,9195464);

-- Owner-recorded Hunter placements, 2026-10-09.
-- Read-only snapshot from tw_world:3307, with FLOAT coordinates read as DOUBLE.
-- Boro Softstep 919582: original local GUID 12585263.
-- Nali Softstep 919583: original local GUID 12585264.
-- Fenn Coilwhistle 919584: original local GUID 12585261.
-- Eris Leafshade 919585: original local GUID 12585262.
-- Source GUIDs are provenance only; each destination allocates its own GUIDs.
-- Preserve all non-GUID fields and the full stored position/facing precision.
-- No addons, GUID/entry paths, groups, links, events or pools are attached.
-- Reruns replace only these four permanent actors. Never place credits
-- 919586-919595 or temporary actors 919900-919917. No live SQL applied by this export.
DELETE FROM `creature` WHERE `id` IN (919582,919583,919584,919585);
INSERT INTO `creature`
 (`id`,`id2`,`id3`,`id4`,`map`,
  `position_x`,`position_y`,`position_z`,`orientation`,
  `spawntimesecsmin`,`spawntimesecsmax`,`wander_distance`,
  `health_percent`,`mana_percent`,`movement_type`,`spawn_flags`,`visibility_mod`)
VALUES
 (919582,0,0,0,1,-1386.43994140625,-136.95199584960938,157.48899841308594,2.132970094680786,
  120,120,0,100,100,0,0,0),
 (919583,0,0,0,1,-1461.47998046875,-91.95220184326172,159.01800537109375,3.622730016708374,
  120,120,0,100,100,0,0,0),
 (919584,0,0,0,1,10077.900390625,2565.360107421875,1319.510009765625,2.3577001094818115,
  120,120,0,100,100,0,0,0),
 (919585,0,0,0,1,10156.2001953125,2550.389892578125,1322.27001953125,3.4847500324249268,
  120,120,0,100,100,0,0,0);
