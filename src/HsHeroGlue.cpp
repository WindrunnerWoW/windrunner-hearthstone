/*
 * mod-hearthstone: the `.hs hero` GM commands, see HsHeroGlue.h.
 */

#include "Chat.h"
#include "Player.h"
#include "Log.h"
#include "SharedDefines.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include "HsEconomyGlue.h"
#include "HsHeroGlue.h"
#include "HsHeroPortraitStore.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace
{
    std::string Lower(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    }

    void Show(ChatHandler* handler, uint32_t accountId, std::string const& label)
    {
        HsHeroDbStore store;
        Hs::Heroes::HeroState state;
        if (!store.Load(accountId, state))
        {
            handler->SendSysMessage("Hearthstone: the hero portraits of " + label + " could not be read (see the server log; "
                "is character migration 0021 applied?).");
            return;
        }
        handler->SendSysMessage("Hearthstone: hero portraits of " + label + ": " + std::to_string(state.credits) +
            " unspent credit(s), " + std::to_string(state.owned.size()) + " unlocked.");
        for (std::string const& id : state.owned)
        {
            Hs::Heroes::Portrait const* p = Hs::Heroes::FindPortrait(id);
            std::string line = "  " + id + (p ? std::string(" (") + p->heroClass + ", " + p->name + ")" : std::string(" (not in the catalog)"));
            handler->SendSysMessage(line);
        }
        for (auto const& entry : state.selected)
            handler->SendSysMessage("  chosen for " + entry.first + ": " + entry.second);
    }
}

void HsHeroUsage(ChatHandler* handler)
{
    handler->SendSysMessage("Usage: .hs hero <player|#accountId>");
    handler->SendSysMessage("       .hs hero credit <player|#accountId> [n]  (adds 1 to 20 Legend #1 credits)");
    handler->SendSysMessage("       .hs hero grant <player|#accountId> <portraitId>  (unlocks without a credit)");
    handler->SendSysMessage("       .hs hero revoke <player|#accountId> <portraitId> confirm");
    handler->SendSysMessage("       .hs hero reset <player|#accountId> confirm  (forgets all credits, portraits and choices)");
}

void HsHeroCommand(ChatHandler* handler, int security, std::vector<std::string> const& args)
{
    std::string const first = args.size() > 1 ? Lower(args[1]) : std::string();
    bool const mutating = first == "credit" || first == "grant" || first == "revoke" || first == "reset";
    // Counts of the whole line: show 2, credit 3 or 4, grant 4, revoke 5, reset 4.
    bool const shapeOk = !mutating ? args.size() == 2
        : first == "credit" ? (args.size() == 3 || args.size() == 4)
        : first == "grant" ? args.size() == 4
        : first == "revoke" ? args.size() == 5
        : args.size() == 4;
    if (args.size() < 2 || !shapeOk)
    {
        HsHeroUsage(handler);
        return;
    }
    if (mutating && security < static_cast<int>(SEC_DEVELOPER))
    {
        handler->SendSysMessage("Hearthstone: .hs hero " + first + " needs security level 3 (developer) or higher.");
        return;
    }
    if ((first == "revoke" && Lower(args[4]) != "confirm") || (first == "reset" && Lower(args[3]) != "confirm"))
    {
        handler->SendSysMessage("Hearthstone: .hs hero " + first + " removes data: add the word confirm at the end.");
        return;
    }

    uint32_t accountId = 0;
    std::string label;
    if (!HsResolveAccountTarget(handler, mutating ? args[2] : args[1], accountId, label))
        return;
    if (!mutating)
    {
        Show(handler, accountId, label);
        return;
    }
    if (!HsHeroReady())
    {
        handler->SendSysMessage("Hearthstone: the hero portrait tables are missing (character migration "
            "0021_hearthstone_hero_portraits.sql is not applied). Nothing was changed.");
        return;
    }

    if (first == "credit")
    {
        uint32_t n = 1;
        if (args.size() == 4)
        {
            bool ok = !args[3].empty() && args[3].size() <= 2;
            for (char c : args[3])
                ok = ok && c >= '0' && c <= '9';
            n = ok ? static_cast<uint32_t>(std::stoul(args[3])) : 0;
            if (n < 1 || n > 20)
            {
                handler->SendSysMessage("Hearthstone: '" + args[3] + "' is not a count (1 to 20).");
                return;
            }
        }
        std::vector<std::string> sql;
        for (uint32_t i = 0; i < n; ++i)
            sql.push_back(Hs::Heroes::GmCreditSql(accountId));
        if (!HsHeroCommit(sql, "gm credit"))
        {
            handler->SendSysMessage("Hearthstone: the credits could not be written (see the server log). Nothing was changed.");
            return;
        }
        sLog.outString("Hearthstone: %s added %u hero portrait credit(s) to account %u", HsGmSource(handler).c_str(), n, accountId);
    }
    else if (first == "reset")
    {
        if (!HsHeroCommit(Hs::Heroes::GmResetSql(accountId), "gm reset"))
        {
            handler->SendSysMessage("Hearthstone: the reset failed (see the server log). Nothing was changed.");
            return;
        }
        sLog.outString("Hearthstone: %s reset the hero portraits of account %u", HsGmSource(handler).c_str(), accountId);
    }
    else
    {
        Hs::Heroes::Portrait const* portrait = Hs::Heroes::FindPortrait(args[3]);
        if (!portrait || portrait->isDefault)
        {
            handler->SendSysMessage("Hearthstone: '" + args[3] + "' is not an alternative portrait of the catalog "
                "(data/heroes/portraits.json).");
            return;
        }
        bool const grant = first == "grant";
        std::vector<std::string> const sql = grant
            ? std::vector<std::string>{ Hs::Heroes::PortraitUnlockSql(accountId, *portrait, "gm") }
            : Hs::Heroes::GmRevokeSql(accountId, *portrait);
        if (!HsHeroCommit(sql, grant ? "gm grant" : "gm revoke"))
        {
            handler->SendSysMessage("Hearthstone: the change failed (see the server log). Nothing was changed.");
            return;
        }
        sLog.outString("Hearthstone: %s %s the portrait %s %s account %u", HsGmSource(handler).c_str(),
            grant ? "granted" : "revoked", portrait->id, grant ? "to" : "from", accountId);
    }
    HsPushHeroChanged(accountId, "gm");
    handler->SendSysMessage("Hearthstone: done.");
    Show(handler, accountId, label);
}
