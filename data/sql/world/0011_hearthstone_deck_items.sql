-- mod-hearthstone: the deck items, quest rewards with the deck icon (display id 69175 for the starter deck, 69178 for the class decks, owner,
-- client patch). Right-click (CMSG_OPEN_ITEM, flags 4 like the booster pack) is answered by the module (HsIntroGlue.cpp,
-- HsIntroHandleOpenItem); the item is used up. All are soulbound (bonding 1).
--
--   919610 Hearthstone Starter Deck  the visible reward of the intro quest "Too Many Rules" (RewItemId1, world migration
--                                    0008). Using it gives the starter cards and the starter deck of the CLASS OF THE
--                                    CHARACTER using it (once per account and class, protected top-up as before). It does
--                                    not unlock another class and it is for this character only.
--   919620 to 919628  <Class> Deck   the reward of the final quest of a class-unlock questline (the questlines are not
--                                    built yet; a GM can hand one out with .additem). Using it unlocks that class for the
--                                    WHOLE ACCOUNT (D12) and awards its starter deck and cards (the same as
--                                    .hs class unlock). Order: Warrior, Paladin, Hunter, Rogue, Priest, Shaman, Mage,
--                                    Warlock, Druid (HsIntroGlue.cpp kClassDecks). The names are placeholders.
--
-- The templates are rebuilt each time this file runs. The file contains no backslashes, no semicolons and no
-- apostrophes inside comments or strings.

DELETE FROM `item_template` WHERE `entry` = 919610 OR `entry` BETWEEN 919620 AND 919628;

INSERT INTO `item_template`
  (`entry`, `class`, `subclass`, `name`, `description`, `display_id`, `quality`, `flags`, `buy_count`, `buy_price`,
   `sell_price`, `inventory_type`, `max_count`, `stackable`, `bonding`, `material`, `other_team_entry`, `script_name`)
VALUES
  (919610, 15, 0, 'Hearthstone Starter Deck', 'Right-click to receive the Hearthstone starter deck of your class.',
   69175, 2, 4, 1, 0, 0, 0, 1, 1, 1, 1, 0, ''),
  (919620, 15, 0, 'Warrior Deck', 'Right-click to unlock the Warrior class for your whole account and receive its starter deck.',
   69178, 3, 4, 1, 0, 0, 0, 1, 1, 1, 1, 0, ''),
  (919621, 15, 0, 'Paladin Deck', 'Right-click to unlock the Paladin class for your whole account and receive its starter deck.',
   69178, 3, 4, 1, 0, 0, 0, 1, 1, 1, 1, 0, ''),
  (919622, 15, 0, 'Hunter Deck', 'Right-click to unlock the Hunter class for your whole account and receive its starter deck.',
   69178, 3, 4, 1, 0, 0, 0, 1, 1, 1, 1, 0, ''),
  (919623, 15, 0, 'Rogue Deck', 'Right-click to unlock the Rogue class for your whole account and receive its starter deck.',
   69178, 3, 4, 1, 0, 0, 0, 1, 1, 1, 1, 0, ''),
  (919624, 15, 0, 'Priest Deck', 'Right-click to unlock the Priest class for your whole account and receive its starter deck.',
   69178, 3, 4, 1, 0, 0, 0, 1, 1, 1, 1, 0, ''),
  (919625, 15, 0, 'Shaman Deck', 'Right-click to unlock the Shaman class for your whole account and receive its starter deck.',
   69178, 3, 4, 1, 0, 0, 0, 1, 1, 1, 1, 0, ''),
  (919626, 15, 0, 'Mage Deck', 'Right-click to unlock the Mage class for your whole account and receive its starter deck.',
   69178, 3, 4, 1, 0, 0, 0, 1, 1, 1, 1, 0, ''),
  (919627, 15, 0, 'Warlock Deck', 'Right-click to unlock the Warlock class for your whole account and receive its starter deck.',
   69178, 3, 4, 1, 0, 0, 0, 1, 1, 1, 1, 0, ''),
  (919628, 15, 0, 'Druid Deck', 'Right-click to unlock the Druid class for your whole account and receive its starter deck.',
   69178, 3, 4, 1, 0, 0, 0, 1, 1, 1, 1, 0, '');
