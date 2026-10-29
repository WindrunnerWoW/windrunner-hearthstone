/*
 * Wire codec of the match ops: the GAME payload built from a
 * Match::MatchView and the GAME_ACT / GAME_LEAVE request decoders. See
 * docs/PROTOCOL.md.
 *
 * PURE C++ (standard library, the protocol codec and the match engine header),
 * tested headless in tests/protocol_tests.cpp. The addon mirrors the format in
 * addon/Hearthstone/Match.lua.
 *
 * GAME payload, decoded, keys in this order (values are escaped by KvEncode; the later keys are
 * dif, mo, ms, mx, os, ox, pk and opk):
 *   mid;seq;ph;turn;act;res;why;mn;on;dif;mh;mp;mw;mm;mk;mb;mc;mo;ms;mx;oh;op;ow;om;ok;ob;os;ox;mul;pk;opk;lg;evs;ev
 * Without a match the payload is just "ph=none".
 */

#ifndef MOD_HEARTHSTONE_HS_MATCH_WIRE_H
#define MOD_HEARTHSTONE_HS_MATCH_WIRE_H

#include "HsMatch.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace Hs
{
    // At most this many (the newest) events travel in one GAME payload.
    constexpr std::size_t kMaxGameEvents = 40;
    // The mulligan offers at most four cards; a request naming more is malformed.
    constexpr std::size_t kMaxMulliganIds = 10;
    // GAME_ACT position (p=): a board slot 0..7.
    constexpr int kMaxWirePosition = 7;

    // Everything of a GAME payload that is not in the MatchView.
    struct GameHeader
    {
        uint32_t matchId = 0;
        std::string myName;       // mn
        std::string opponentName; // on
        std::string difficulty = "normal"; // dif: normal, hard or boss
        // The reason code of a match that was aborted from outside (stuck, teleport, death,
        // npc, gm, error); used for why= when the engine reports EndReason::Aborted.
        std::string abortWhy;
        // Seconds left of the Nozdormu turn timer, rounded up (tmr=; 0 = no timer).
        int timerSec = 0;
        // A ranked ladder duel (dur=1, sent only then): the window's close button pauses it, never a surrender.
        bool durable = false;
    };

    // "ph=none"
    std::string EncodeGameNone();
    // The GAME payload of view (a view of the human side: viewer = "m", the other side = "o").
    // view.events are the events to send (the newest kMaxGameEvents of them are encoded).
    std::string EncodeGame(GameHeader const& header, Match::MatchView const& view);

    // res= and why= of a view: "-" while the match runs; win / loss / draw / int and
    // hero / concede / turns / <abortWhy> when it is over.
    std::string GameResultCode(Match::MatchView const& view);
    std::string GameWhyCode(Match::MatchView const& view, std::string const& abortWhy);

    // A decoded GAME_ACT request. For a hero power action action.source is 0: the
    // service fills in the hero power entity of the acting side.
    struct GameActRequest
    {
        uint32_t matchId = 0;
        uint32_t seq = 0;
        Match::Action action;
    };

    // mid=<id>;seq=<n>;a=<mull|play|atk|hp|end|concede|pick>[;s=<id>][;t=<id>][;p=<0..7>][;c=<1|2>][;r=<id>,<id>,...]
    //  - mid and a are required; seq is required except for a=concede (0 then);
    //  - mull needs r (an empty value is allowed), play needs s (t defaults to 0, p to -1, c to 0 = no
    //    choice), atk needs s and t, hp takes an optional t, pick (answers a pending pick,
    //    ActionType::Choose) needs s = the picked option, end and concede take nothing more;
    //  - c (the Choose One option, Action::choice) is only allowed with a=play and must be 1 or 2;
    //  - s, t, p and r are validated whenever present (decimal digits only, p at most 7, r at
    //    most kMaxMulliganIds ids without empty entries) but only used by the actions above;
    //  - any other key, a duplicate key or a bad value is malformed.
    // False when the payload is malformed (nothing in out is meaningful then).
    bool DecodeGameAct(std::string const& payload, GameActRequest& out);

    // mid=<id>
    bool DecodeGameLeave(std::string const& payload, uint32_t& matchId);

    // Why codes of ERR REFUSED for GAME_ACT / GAME_LEAVE and their texts for players.
    constexpr char const* kWhyStale = "stale";
    constexpr char const* kWhyMatch = "match";
    constexpr char const* kWhyOver = "over";
    constexpr char const* kWhyActive = "active";
    // The engine's refusal codes choice, pending and secret have texts too.
    char const* GameWhyText(std::string const& why);
}

#endif
