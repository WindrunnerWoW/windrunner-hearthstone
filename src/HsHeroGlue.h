/*
 * mod-hearthstone: the `.hs hero` GM commands of the alternative hero portraits (reward track). The
 * catalog, the checks and the SQL are in src/heroes (HsHeroPortraits.h), the database access in HsHeroPortraitStore.h.
 */

#ifndef MOD_HEARTHSTONE_HS_HERO_GLUE_H
#define MOD_HEARTHSTONE_HS_HERO_GLUE_H

#include <string>
#include <vector>

class ChatHandler;

// args[0] is "hero". Showing needs security 2 (the caller checked it), everything that changes data needs 3.
void HsHeroCommand(ChatHandler* handler, int security, std::vector<std::string> const& args);
void HsHeroUsage(ChatHandler* handler);

#endif
