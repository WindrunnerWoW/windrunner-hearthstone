-- mod-hearthstone: the match-finder NPCs and the names of their random opponents.
-- Hand maintained. Both tables are owner data: CREATE TABLE IF NOT EXISTS, never dropped.
--
-- hearthstone_finder: one row per creature entry that acts as a match-finder. Talking to it offers
-- Random opponent (Normal) and Random opponent (Hard). Normal plays a random library deck rated Easy
-- with the Normal AI, Hard a random deck rated Hard with the Hard AI (hearthstone_deck_template column
-- finder). Not seeded: Innkeeper Gryshka (6929, Orgrimmar) was the owner test NPC and is removed. Add the entry of any
-- NPC you placed, for example
--   INSERT INTO hearthstone_finder (creature_entry) VALUES (6740)
--
-- hearthstone_finder_npc: the real NPCs whose names the random opponent takes. faction alliance is used
-- for Alliance characters, horde for Horde characters, any for both. The name comes from creature_template
-- at server startup. Rows are seeded with INSERT IGNORE, so your changes stay: set enabled = 0 to take
-- a name out (a deleted seed row comes back when this file changes and runs again), add rows for more.
--
-- The file contains no backslashes, no semicolons and no apostrophes inside comments or strings.

CREATE TABLE IF NOT EXISTS `hearthstone_finder` (
  `creature_entry` INT UNSIGNED NOT NULL,
  PRIMARY KEY (`creature_entry`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Match-finder NPCs of mod-hearthstone (owner data)';

DELETE FROM `hearthstone_finder` WHERE `creature_entry` = 6929;

CREATE TABLE IF NOT EXISTS `hearthstone_finder_npc` (
  `creature_entry` INT UNSIGNED NOT NULL,
  `faction` ENUM('alliance','horde','any') NOT NULL,
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  PRIMARY KEY (`creature_entry`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Names of the random match-finder opponents of mod-hearthstone (owner data)';

INSERT IGNORE INTO `hearthstone_finder_npc` (`creature_entry`, `faction`) VALUES
(1284, 'alliance'), -- Archbishop Benedictus
(1444, 'alliance'), -- Brother Kristoff
(392, 'alliance'), -- Captain Grayson
(823, 'alliance'), -- Deputy Willem
(352, 'alliance'), -- Dungar Longdrink
(482, 'alliance'), -- Elling Trias
(341, 'alliance'), -- Foreman Oslow
(6122, 'alliance'), -- Gakin the Darkbinder
(1416, 'alliance'), -- Grimand Elmore
(1573, 'alliance'), -- Gryth Thurden
(1427, 'alliance'), -- Harlan Bagley
(7937, 'alliance'), -- High Tinker Mekkatorque
(2916, 'alliance'), -- Historian Karnik
(5170, 'alliance'), -- Hjoldir Stoneblade
(11866, 'alliance'), -- Ilyenia Moonfire
(6740, 'alliance'), -- Innkeeper Allison
(1247, 'alliance'), -- Innkeeper Belm
(295, 'alliance'), -- Innkeeper Farley
(5111, 'alliance'), -- Innkeeper Firebrew
(6734, 'alliance'), -- Innkeeper Hearthstove
(8931, 'alliance'), -- Innkeeper Heather
(1464, 'alliance'), -- Innkeeper Helbrek
(6738, 'alliance'), -- Innkeeper Kimlya
(6735, 'alliance'), -- Innkeeper Saelienne
(6737, 'alliance'), -- Innkeeper Shaussiy
(6790, 'alliance'), -- Innkeeper Trelayne
(4753, 'alliance'), -- Jartsam
(2784, 'alliance'), -- King Magni Bronzebeard
(1299, 'alliance'), -- Lisbeth Schneider
(928, 'alliance'), -- Lord Grayson Shadowbreaker
(1139, 'alliance'), -- Magistrate Bluntnose
(240, 'alliance'), -- Marshal Dughan
(197, 'alliance'), -- Marshal McBride
(332, 'alliance'), -- Master Mathias Shaw
(6142, 'alliance'), -- Mathiel
(279, 'alliance'), -- Morgan Pestle
(1340, 'alliance'), -- Mountaineer Kadrell
(5109, 'alliance'), -- Myra Tyrngaarde
(241, 'alliance'), -- Remy Two Times
(235, 'alliance'), -- Salma Saldean
(1274, 'alliance'), -- Senator Barin Redstone
(514, 'alliance'), -- Smith Argus
(3518, 'alliance'), -- Thomas Miller
(4259, 'alliance'), -- Thurgrum Deepforge
(1402, 'alliance'), -- Topper McNabb
(8507, 'alliance'), -- Tymor
(415, 'alliance'), -- Verner Osgood
(5479, 'alliance'), -- Wu Shen
(1518, 'horde'), -- Apothecary Johaan
(5769, 'horde'), -- Arch Druid Hamuul Runetotem
(2993, 'horde'), -- Baine Bloodhoof
(1498, 'horde'), -- Bethor Iceshard
(3057, 'horde'), -- Cairne Bloodhoof
(2981, 'horde'), -- Chief Hawkwind
(1519, 'horde'), -- Deathguard Simmer
(1515, 'horde'), -- Executor Zygand
(3143, 'horde'), -- Gornek
(3353, 'horde'), -- Grezz Ragefist
(2947, 'horde'), -- Harken Windtotem
(6739, 'horde'), -- Innkeeper Bates
(3934, 'horde'), -- Innkeeper Boorand Plainswind
(6928, 'horde'), -- Innkeeper Grosk
(6929, 'horde'), -- Innkeeper Gryshka
(6747, 'horde'), -- Innkeeper Kauth
(6741, 'horde'), -- Innkeeper Norman
(6746, 'horde'), -- Innkeeper Pala
(5688, 'horde'), -- Innkeeper Renee
(2388, 'horde'), -- Innkeeper Shay
(3344, 'horde'), -- Kardris Dreamseeker
(2492, 'horde'), -- Lexington Mortaim
(4046, 'horde'), -- Magatha Grimtotem
(1499, 'horde'), -- Magistrate Sevren
(3430, 'horde'), -- Mangletooth
(2055, 'horde'), -- Master Apothecary Faranell
(3304, 'horde'), -- Master Vornal
(4551, 'horde'), -- Michael Garrett
(3230, 'horde'), -- Nazgrel
(3142, 'horde'), -- Orgnil Soulscar
(3328, 'horde'), -- Ormok
(2985, 'horde'), -- Ruul Eagletalon
(11868, 'horde'), -- Sayoc
(3433, 'horde'), -- Tatternack Steelforge
(3429, 'horde'), -- Thork
(4949, 'horde'), -- Thrall
(2425, 'horde'), -- Varimathras
(10540, 'horde'), -- Voljin
(3489, 'horde'), -- Zargh
(4047, 'horde'), -- Zor Lonetree
(2496, 'any'), -- Baron Revilgaz
(2486, 'any'), -- Fin Fizracket
(3391, 'any'), -- Gazlowe
(7733, 'any'), -- Innkeeper Fizzgrimble
(6807, 'any'), -- Innkeeper Skindle
(11118, 'any'), -- Innkeeper Vizzie
(6791, 'any'), -- Innkeeper Wiley
(737, 'any'), -- Kebok
(773, 'any'), -- Krazek
(7564, 'any'), -- Marin Noggenfogger
(6177, 'any'), -- Narm Faulk
(2832, 'any'), -- Nixxrax Fillamug
(3666, 'any') -- Wizbang Cranktoggle
;
