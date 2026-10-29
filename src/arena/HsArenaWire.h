/*
 * Draft arena wire format: the ARENA / ARENA_UPD payload, the decoded ARENA_CLASS / ARENA_PICK /
 * ARENA_CLAIM / ARENA_DUEL / ARENA_RETIRE requests, the ARENA_CLOSE push and the key rule. PURE C++ (the protocol codec only), so the protocol unit tests can run it
 * without the arena rules. docs/PROTOCOL.md "Draft arena" is the contract.
 */

#ifndef MOD_HEARTHSTONE_HS_ARENA_WIRE_H
#define MOD_HEARTHSTONE_HS_ARENA_WIRE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace Hs
{
    // Op names (<= 12 characters).
    constexpr char const* kOpArenaGet = "ARENA_GET";
    constexpr char const* kOpArenaClass = "ARENA_CLASS";
    constexpr char const* kOpArenaPick = "ARENA_PICK";
    constexpr char const* kOpArenaClaim = "ARENA_CLAIM";
    // The buttons of the arena window, only at a host (Ezra / Ibelin): the next duel (or resume the paused one), retire.
    constexpr char const* kOpArenaDuel = "ARENA_DUEL";
    constexpr char const* kOpArenaRetire = "ARENA_RETIRE";

    // Idempotency key of the mutating arena ops: 12..16 characters [0-9a-z], the rule of PACK_OPEN.
    constexpr std::size_t kMinArenaKeyLen = 12;
    constexpr std::size_t kMaxArenaKeyLen = 16;
    bool IsValidArenaKey(std::string const& key);

    // What the addon sees of the account's run. Built by the glue from the stored run; nothing hidden is in it.
    struct ArenaView
    {
        // "none" (no run), class, draft, ready, duel, paused, finished, retired, settled or repair.
        std::string state = "none";
        uint32_t runId = 0;
        bool enabled = false;             // new entries are accepted (existing runs play on)
        std::string pay;                  // free, gold or coins ("" without a run)
        std::string deckClass;            // the drafted class ("" before the choice)
        uint32_t wins = 0;                // draws count as wins
        uint32_t draws = 0;               // how many of the wins were draws
        uint32_t losses = 0;
        uint32_t duels = 0;               // duels started
        // The open offer: pick number 0 is the class offer, 1..30 the card offers; offerNo < 0 when none is open.
        int offerNo = -1;
        std::vector<std::string> offer;
        uint32_t picked = 0;              // card picks made so far (0..30)
        std::vector<std::pair<std::string, uint32_t>> deck; // drafted cards, card id and copies, sorted by id
        // The prize the run pays if it ended now (a run that ended: the fixed prize).
        uint32_t prizePacks = 0;
        uint32_t prizeCoins = 0;
        bool prizeSpecial = false;
        uint32_t basePacks = 0;           // the run's snapshotted terms, for the prize table of the screen
        uint32_t bonusCoins = 0;
        bool specialPending = false;      // a special card can be chosen (ARENA_CLAIM)
        std::vector<std::pair<std::string, uint32_t>> specials; // the missing sets: card id, copies the claim grants
        std::string claimed;              // the claimed special card ("" none)
        bool live = false;                // the duel runs in this process now
        bool mine = false;                // ... and the asking character plays it
    };

    // The payload of ARENA (reply) and ARENA_UPD (push). replay = the request was applied before.
    std::string EncodeArenaPayload(ArenaView const& view, bool replay);

    // A decoded request of the mutating arena ops.
    struct ArenaRequest
    {
        std::string op;
        std::string reqId;
        std::string key;
        uint32_t runId = 0;    // 0 = the current run
        std::string deckClass; // ARENA_CLASS
        uint32_t pickNo = 0;   // ARENA_PICK, 1..30
        std::string cardId;    // ARENA_PICK, ARENA_CLAIM
    };
    // false = malformed (ERR MALFORMED). op decides which keys are required; unknown or duplicate keys are malformed.
    bool DecodeArenaRequest(std::string const& op, std::string const& payload, ArenaRequest& out);

    struct ArenaReply
    {
        enum class Status { Ok, Replay, Refused, Unavailable, Deferred };
        Status status = Status::Unavailable;
        std::string why;   // Refused: the refusal code
        std::string msg;   // Refused / Unavailable: the sentence for the player
        ArenaView view;    // Ok / Replay
    };

    // Appends the frames of the answer to `out` (ARENA, or an ERR). Deferred appends nothing.
    void AppendArenaReply(std::vector<std::string>& out, std::string const& reqId, ArenaReply const& reply);
    // ARENA_UPD push (reqId 0): the state changed outside this client's own request.
    void AppendArenaUpdate(std::vector<std::string>& out, ArenaView const& view);
    // ARENA_OPEN push (reqId 0): the player chose "continue drafting" at the host; the addon opens its arena screen
    // with this state.
    void AppendArenaOpen(std::vector<std::string>& out, ArenaView const& view);
    // ARENA_CLOSE push (reqId 0, empty payload): the player left the host that opened the arena screen; the addon
    // closes it.
    void AppendArenaClose(std::vector<std::string>& out);
}

#endif
