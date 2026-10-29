/*
 * Match replay log: the ordered list of every action
 * that was applied to a match, and its compact, versioned text form.
 *
 * A match is reproducible from its setup (seed, sides, decks) and the actions applied to it, in order
 * (HsMatch.h "Determinism"). The log therefore holds every successful Apply: the human's, the AI's
 * (including its opening Mulligan) and the actions the Nozdormu turn timer forces (a pick answer and the
 * EndTurn). Refused actions are not part of it (a refusal changes nothing). Replaying the log with
 * Match::Apply on a fresh Match::Create gives the same match, event for event, for the same
 * Match::kRulesVersion and card dataset.
 *
 * PURE C++ (standard library and the match engine header), tested in tests/match_replay_tests.cpp.
 *
 * Text form (one action per string; the storage keeps one string per row):
 *   v1,<side>,<type>,<source>,<target>,<position>,<choice>[,<replaced id>...]
 *   - v1: the format version (kActionLogVersion); a decoder refuses another version;
 *   - side: 0 or 1;
 *   - type: M Mulligan, P Play, A Attack, H HeroPower, E EndTurn, C Concede, K Choose;
 *   - source, target: entity ids (decimal, 0 = none); position: board slot or -1; choice: 0, 1 or 2;
 *   - the replaced ids follow only for a Mulligan (the offered cards shuffled back, in the order given).
 *   Example: "v1,0,P,12,0,-1,0", "v1,1,M,0,0,-1,0,5,7", "v1,0,E,0,0,-1,0".
 * Decoding is strict (decimal digits only, no spaces, no leading zeros, no extra fields), so
 * EncodeAction(DecodeAction(text)) == text for every accepted text.
 */

#ifndef MOD_HEARTHSTONE_HS_MATCH_REPLAY_H
#define MOD_HEARTHSTONE_HS_MATCH_REPLAY_H

#include "HsMatch.h"

#include <cstddef>
#include <string>

namespace Hs
{
    // The version prefix of the text form. Raise it (and keep a decoder for the old one) if the form changes.
    constexpr int kActionLogVersion = 1;
    // A Mulligan names at most this many ids (the offer is at most four cards; the wire allows ten).
    constexpr std::size_t kMaxLoggedReplace = 10;

    // One applied action and the side that applied it.
    struct LoggedAction
    {
        int side = 0;
        Match::Action action;
    };

    bool SameLoggedAction(LoggedAction const& a, LoggedAction const& b);

    // "v1,0,P,12,0,-1,0"; "" for a side outside 0..1 (nothing sensible to store).
    std::string EncodeAction(LoggedAction const& logged);

    // False with a short reason in error ("version", "malformed", "side", "type", "number", "replace") when text
    // is not a form this decoder reads; out is not meaningful then.
    bool DecodeAction(std::string const& text, LoggedAction& out, std::string& error);
}

#endif
