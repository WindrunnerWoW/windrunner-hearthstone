/*
 * mod-hearthstone wire protocol v1: constants, escaping, frame codec.
 *
 * PURE C++ (standard library only, no core headers) so it can be unit-tested
 * headlessly. The Lua addon (addon/Hearthstone/HsProtocol.lua) mirrors this
 * file function for function; tests/golden_vectors.txt keeps the two honest.
 * See docs/PROTOCOL.md.
 */

#ifndef MOD_HEARTHSTONE_HS_PROTOCOL_H
#define MOD_HEARTHSTONE_HS_PROTOCOL_H

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace Hs
{
    constexpr char const* kPrefix = "WRHS";
    constexpr std::size_t kPrefixLen = 4;
    constexpr int kProtocolVersion = 1;

    // prefix + "\t" + frame must stay within this many bytes (client limit is 255).
    constexpr std::size_t kMaxWireBytes = 240;
    constexpr std::size_t kMaxReqIdLen = 8;
    constexpr std::size_t kMaxOpLen = 12;
    // Highest part / total number a frame may carry (two decimal digits).
    constexpr uint32_t kMaxPartsHardLimit = 99;
    constexpr char const* kServerPushReqId = "0";

    // Error codes carried by the ERR op.
    enum class ErrorCode
    {
        Version,
        Malformed,
        TooLarge,
        Rate,
        UnknownOp,
        NotImplemented,
        Disabled,
        Timeout,
        Busy,
        NotReady,
        Unavailable, // collection / pack storage or card data not available
        NoStock,     // PACK_OPEN without an unopened pack
        Refused      // a business rule rejected the request; nothing was changed
    };
    char const* ErrorCodeName(ErrorCode code);

    // ---- escaping ------------------------------------------------------
    // Wire escape: the encoded text never contains any of  | LF CR TAB NUL : _
    // any byte outside printable ASCII, and never the substring "LFT".
    //   ~~ '~'   ~p '|'   ~n LF   ~r CR   ~t TAB   ~c ':'   ~u '_'
    //   ~l 'L' (only emitted when the next byte is 'F', which breaks "LFT")
    //   ~xHH any other control byte, DEL and bytes >= 0x80 (lowercase hex)
    std::string Escape(std::string const& raw);
    // Encoded length of raw[i] (1..4). When out != nullptr the encoding is appended.
    std::size_t EscapeStep(std::string const& raw, std::size_t i, std::string* out);
    // False on a dangling '~' or an unknown escape sequence.
    bool Unescape(std::string const& encoded, std::string& out);

    // Value escape for key=value;key=value payloads:  ~~ '~'   ~s ';'   ~q '='
    // Applied to values BEFORE the payload goes through the wire escape.
    std::string EscapeValue(std::string const& raw);
    bool UnescapeValue(std::string const& encoded, std::string& out);

    // True if s contains | LF CR NUL or any substring the core's own addon
    // handlers react to (LFT, TW_, TWT_, GM_ADDON, MR:, TR:).
    bool ContainsForbidden(std::string const& s);

    using KvList = std::vector<std::pair<std::string, std::string>>;
    // Keys must match [A-Za-z0-9_]+ ; values are escaped with EscapeValue.
    std::string KvEncode(KvList const& kv);
    // Rejects: empty segments, missing '=', bad key, duplicate key, bad escape.
    bool KvDecode(std::string const& payload, std::map<std::string, std::string>& out);

    // ---- frames --------------------------------------------------------
    // frame = v1 TAB OP TAB reqId TAB part TAB total TAB payload
    struct Frame
    {
        int version = kProtocolVersion;
        std::string op;
        std::string reqId;
        uint32_t part = 1;
        uint32_t total = 1;
        std::string payload; // decoded (unescaped)
    };

    enum class ParseStatus { Ok, Malformed, BadVersion };
    struct ParseResult
    {
        ParseStatus status = ParseStatus::Malformed;
        std::string reqId = kServerPushReqId; // best effort, for error replies
    };

    bool IsValidOp(std::string const& op);
    bool IsValidReqId(std::string const& reqId);

    // Strips "WRHS\t" from a full chat message. False if it does not start with it.
    bool StripPrefix(std::string const& wire, std::string& frame);

    // Parse a frame (prefix already removed). The payload is everything after the
    // fifth TAB; it must consist only of characters Escape() can emit.
    ParseResult ParseFrame(std::string const& frame, Frame& out);

    // Encode one frame. False if a field is invalid or prefix+TAB+frame would
    // exceed maxWire bytes or contain a forbidden sequence.
    bool EncodeFrame(std::string const& op, std::string const& reqId, uint32_t part, uint32_t total,
                     std::string const& payload, std::string& outFrame, std::size_t maxWire = kMaxWireBytes);

    // Split a payload into as many frames as needed (each <= maxWire). False if
    // more than kMaxPartsHardLimit frames would be required or the fields are invalid.
    bool ChunkPayload(std::string const& op, std::string const& reqId, std::string const& payload,
                      std::vector<std::string>& outFrames, std::size_t maxWire = kMaxWireBytes);

    // ---- test payload helpers (ECHO) -------------------------------------
    uint32_t Adler32(std::string const& data);
    std::string HexSum(uint32_t sum); // 8 lowercase hex digits
    // Deterministic alphanumeric pattern of exactly len bytes.
    std::string MakePattern(std::size_t len);
}

#endif
