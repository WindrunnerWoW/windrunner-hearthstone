-- mod-hearthstone: Ezra Chatterton (919501, Tauren, display 1624) is drawn too small at the default scale 1. The other Tauren
-- teacher of the module (Boro Softstep, migration 0040) uses 1.35, so Ezra gets the same. Presentation only, safe to repeat.
-- The file contains no backslashes and no semicolons inside comments or strings.
UPDATE `creature_template` SET `scale` = 1.35 WHERE `entry` = 919501;
