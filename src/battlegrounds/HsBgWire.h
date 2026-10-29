/*
 * Battlegrounds wire format: the WRBG payloads on top of the HsProtocol codec (same frame,
 * escaping and chunking as WRHS; only the chat prefix differs, and the glue adds it). PURE C++ like HsArenaWire.
 *
 * CLIENT -> SERVER
 *   BG_GET                      no payload: resend the state (and the end result when the game is over)
 *   BG_ACT  act=<name>;...      one action, keys by act:
 *             buy      i=<offer>
 *             sell     z=b|h  i=<index>              (board or hand)
 *             play     i=<hand slot>  s=<board slot>  [u=<target uid>]
 *             cast     i=<hand slot>  [u=<target uid>]   (a spell)
 *             move     f=<from>  t=<to>
 *             refresh | upgrade | end
 *             freeze   on=0|1
 *             discover i=<option>
 *             peek     to=<ally seat>            (a read-only view of an ally's field)
 * SERVER -> CLIENT
 *   BG_STATE   the human's view after every action (reply, same reqId) and after the start (push, reqId 0)
 *   BG_ALLY    the answer to a peek: the ally's seat, board, hand, tier, gold and hero power state
 *   BG_COMBAT  push before the BG_STATE of the next round: the human's fight with its event log
 *   BG_END     push after the last BG_STATE: the result
 *   BG_CLOSE   push, empty: the game was ended on the server (leave, logout)
 *   ERR        a refused action: code=Refused, why=<ActionResult name>, nothing changed
 *
 * Minions are written "id.g.atk.hp.kw.uid" (g = golden 0/1, kw = Keyword bit set including auras), lists join them with
 * ','. Card ids are the stable ids of the card table, never table indexes. A hero-power choice is the
 * same list of options as power ids in "opw" instead of minions in "op"; a chosen one is sent with the normal
 * BG_ACT act=discover;i=<option>.
 *
 * "oft" (one digit per tavern offer, 1 = a Darkmoon Ticket), "pws" (this turn is skipped), "pwh"
 * (Maiev's held offer, "id.turns"), "pwe" (Faelin's held minions, "id.tier" joined by ',') and the second quest pair
 * ("q2", "qp2", "qr2", "rw2", "qa2", "qb2", "rt2"); each lobby seat carries two more fields (its second quest and
 * reward id). A held Discover (Faelin) puts the chosen minion in the power's held slot, not in the hand.
 *
 * Adds "sec": the human's Secrets in play order (card ids joined by ',').
 *
 * Adds "bc": what the next tavern minion costs (Friends and Family Discount, also Millhouse), and the
 * Prize Wall's "N turns left" as "pwx" (the power counter). A prize Discover has no field of its own: its options are
 * spell cards in "op", so the spell UI shows them.
 *
 * Adds "nd": where the human's hand cards, board minions and affordable tavern offers would help an ally, as
 * "<uid>.<seat><reasons>,..." best first, at most three allies per card (T triple, R tribe, U upgrade). Empty outside
 * the shop phase and once the send of the turn is used.
 *
 * Adds "rk": every seat's live overall placing, comma separated (1 best).
 *
 * Adds "svh" / "svb": what each hand card / board minion sells for, one number per entry (0 for a spell).
 */

#ifndef MOD_HEARTHSTONE_HS_BG_WIRE_H
#define MOD_HEARTHSTONE_HS_BG_WIRE_H

#include "HsBgSession.h"

#include <string>
#include <vector>

namespace Hs
{
    namespace Bg
    {
        constexpr char const* kBgPrefix = "WRBG";
        constexpr char const* kOpBgGet = "BG_GET";
        constexpr char const* kOpBgAct = "BG_ACT";
        constexpr char const* kOpBgState = "BG_STATE";
        constexpr char const* kOpBgCombat = "BG_COMBAT";
        constexpr char const* kOpBgEnd = "BG_END";
        constexpr char const* kOpBgClose = "BG_CLOSE";
        constexpr char const* kOpBgAlly = "BG_ALLY";

        // false = malformed (ERR MALFORMED): unknown, duplicate or missing keys, a number out of range.
        bool DecodeBgAction(std::string const& payload, Action& out);
        // The inverse, for the tests and the Lua mirror.
        std::string EncodeBgAction(Action const& action);

        // withGifts: fill "gift" from the last Apply. Only the answer to that action sets it, so a BG_GET (a refetch, a
        // /reload) does not announce the same gift again.
        std::string EncodeBgState(Session const& session, bool withGifts = false);
        // The human's fight of the last Apply. A log too long for the frame limit is left out ("tr=1"): the start boards
        // and the survivors are always there, so the addon can jump to the result.
        std::string EncodeBgCombat(Session const& session);
        std::string EncodeBgEnd(Session const& session);
        // An ally's field for a peek: the seat, board, hand, tier, gold and hero power state. Empty when the seat is not
        // a living ally of the human's faction.
        std::string EncodeBgAlly(Session const& session, int seat);

        // Appends the frames of the answer to an action: the ERR of a refusal, the ally's field for a peek, or the fight
        // (push), the state (reqId) and, when the game ended, the result.
        void AppendBgActionReply(std::vector<std::string>& out, std::string const& reqId, Session const& session,
                                 ActionResult result, Action const* action = nullptr);
        // BG_STATE with the given reqId, then BG_END when the game is over (the answer to BG_GET and the first push).
        void AppendBgSnapshot(std::vector<std::string>& out, std::string const& reqId, Session const& session);
        void AppendBgClose(std::vector<std::string>& out);
        // ERR for a malformed request.
        void AppendBgMalformed(std::vector<std::string>& out, std::string const& reqId);
    }
}

#endif
