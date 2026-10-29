#include "HsProtocol.h"

namespace Hs
{
    namespace
    {
        char const kHex[] = "0123456789abcdef";

        int HexVal(char c)
        {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        }

        bool ParseSmallNumber(std::string const& s, std::size_t maxDigits, uint32_t& out)
        {
            if (s.empty() || s.size() > maxDigits)
                return false;
            if (s.size() > 1 && s[0] == '0')
                return false; // no leading zeros: one canonical spelling
            uint32_t v = 0;
            for (char c : s)
            {
                if (c < '0' || c > '9')
                    return false;
                v = v * 10 + static_cast<uint32_t>(c - '0');
            }
            out = v;
            return true;
        }

        bool IsKeyChar(char c)
        {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        }

        // Characters that may appear in an escaped payload as received.
        bool IsWirePayloadChar(unsigned char c)
        {
            if (c < 0x20 || c > 0x7e)
                return false;
            return c != '|' && c != ':' && c != '_';
        }

        // Splits off up to 5 TAB separated fields; the rest goes to fields[5].
        // Returns the number of fields found (1..6); 6 means a complete header.
        int SplitHeader(std::string const& frame, std::string fields[6])
        {
            std::size_t pos = 0;
            for (int i = 0; i < 5; ++i)
            {
                std::size_t const tab = frame.find('\t', pos);
                if (tab == std::string::npos)
                {
                    fields[i] = frame.substr(pos);
                    return i + 1;
                }
                fields[i] = frame.substr(pos, tab - pos);
                pos = tab + 1;
            }
            fields[5] = frame.substr(pos);
            return 6;
        }
    }

    char const* ErrorCodeName(ErrorCode code)
    {
        switch (code)
        {
            case ErrorCode::Version: return "VERSION";
            case ErrorCode::Malformed: return "MALFORMED";
            case ErrorCode::TooLarge: return "TOO_LARGE";
            case ErrorCode::Rate: return "RATE";
            case ErrorCode::UnknownOp: return "UNKNOWN_OP";
            case ErrorCode::NotImplemented: return "NOT_IMPLEMENTED";
            case ErrorCode::Disabled: return "DISABLED";
            case ErrorCode::Timeout: return "TIMEOUT";
            case ErrorCode::Busy: return "BUSY";
            case ErrorCode::NotReady: return "NOT_READY";
            case ErrorCode::Unavailable: return "UNAVAILABLE";
            case ErrorCode::NoStock: return "NO_STOCK";
            case ErrorCode::Refused: return "REFUSED";
        }
        return "MALFORMED";
    }

    std::size_t EscapeStep(std::string const& raw, std::size_t i, std::string* out)
    {
        unsigned char const c = static_cast<unsigned char>(raw[i]);
        char const* two = nullptr;
        switch (c)
        {
            case '~': two = "~~"; break;
            case '|': two = "~p"; break;
            case '\n': two = "~n"; break;
            case '\r': two = "~r"; break;
            case '\t': two = "~t"; break;
            case ':': two = "~c"; break;
            case '_': two = "~u"; break;
            case 'L':
                if (i + 1 < raw.size() && raw[i + 1] == 'F')
                    two = "~l";
                break;
            default: break;
        }
        if (two)
        {
            if (out)
                out->append(two, 2);
            return 2;
        }
        if (c < 0x20 || c == 0x7f || c >= 0x80)
        {
            if (out)
            {
                out->push_back('~');
                out->push_back('x');
                out->push_back(kHex[c >> 4]);
                out->push_back(kHex[c & 0x0f]);
            }
            return 4;
        }
        if (out)
            out->push_back(static_cast<char>(c));
        return 1;
    }

    std::string Escape(std::string const& raw)
    {
        std::string out;
        out.reserve(raw.size() + raw.size() / 8);
        for (std::size_t i = 0; i < raw.size(); ++i)
            EscapeStep(raw, i, &out);
        return out;
    }

    bool Unescape(std::string const& in, std::string& out)
    {
        std::string result;
        result.reserve(in.size());
        for (std::size_t i = 0; i < in.size(); ++i)
        {
            char const c = in[i];
            if (c != '~')
            {
                result.push_back(c);
                continue;
            }
            if (++i >= in.size())
                return false;
            switch (in[i])
            {
                case '~': result.push_back('~'); break;
                case 'p': result.push_back('|'); break;
                case 'n': result.push_back('\n'); break;
                case 'r': result.push_back('\r'); break;
                case 't': result.push_back('\t'); break;
                case 'c': result.push_back(':'); break;
                case 'u': result.push_back('_'); break;
                case 'l': result.push_back('L'); break;
                case 'x':
                {
                    if (i + 2 >= in.size())
                        return false;
                    int const hi = HexVal(in[i + 1]);
                    int const lo = HexVal(in[i + 2]);
                    if (hi < 0 || lo < 0)
                        return false;
                    result.push_back(static_cast<char>(hi * 16 + lo));
                    i += 2;
                    break;
                }
                default: return false;
            }
        }
        out.swap(result);
        return true;
    }

    std::string EscapeValue(std::string const& raw)
    {
        std::string out;
        out.reserve(raw.size());
        for (char c : raw)
        {
            if (c == '~') out += "~~";
            else if (c == ';') out += "~s";
            else if (c == '=') out += "~q";
            else out.push_back(c);
        }
        return out;
    }

    bool UnescapeValue(std::string const& in, std::string& out)
    {
        std::string result;
        result.reserve(in.size());
        for (std::size_t i = 0; i < in.size(); ++i)
        {
            char const c = in[i];
            if (c != '~')
            {
                result.push_back(c);
                continue;
            }
            if (++i >= in.size())
                return false;
            if (in[i] == '~') result.push_back('~');
            else if (in[i] == 's') result.push_back(';');
            else if (in[i] == 'q') result.push_back('=');
            else return false;
        }
        out.swap(result);
        return true;
    }

    bool ContainsForbidden(std::string const& s)
    {
        if (s.find_first_of(std::string("|\n\r\0", 4)) != std::string::npos)
            return true;
        static char const* const kTriggers[] = { "LFT", "TW_", "TWT_", "GM_ADDON", "MR:", "TR:" };
        for (char const* t : kTriggers)
            if (s.find(t) != std::string::npos)
                return true;
        return false;
    }

    std::string KvEncode(KvList const& kv)
    {
        std::string out;
        for (std::size_t i = 0; i < kv.size(); ++i)
        {
            if (i)
                out.push_back(';');
            out += kv[i].first;
            out.push_back('=');
            out += EscapeValue(kv[i].second);
        }
        return out;
    }

    bool KvDecode(std::string const& payload, std::map<std::string, std::string>& out)
    {
        std::map<std::string, std::string> result;
        if (payload.empty())
        {
            out.swap(result);
            return true;
        }
        std::size_t pos = 0;
        while (true)
        {
            std::size_t const semi = payload.find(';', pos);
            std::string const seg = payload.substr(pos, semi == std::string::npos ? std::string::npos : semi - pos);
            std::size_t const eq = seg.find('=');
            if (seg.empty() || eq == std::string::npos || eq == 0)
                return false;
            std::string const key = seg.substr(0, eq);
            for (char c : key)
                if (!IsKeyChar(c))
                    return false;
            std::string value;
            if (!UnescapeValue(seg.substr(eq + 1), value))
                return false;
            if (!result.insert(std::make_pair(key, value)).second)
                return false;
            if (semi == std::string::npos)
                break;
            pos = semi + 1;
        }
        out.swap(result);
        return true;
    }

    bool IsValidOp(std::string const& op)
    {
        if (op.empty() || op.size() > kMaxOpLen)
            return false;
        if (op[0] < 'A' || op[0] > 'Z')
            return false;
        for (char c : op)
            if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'))
                return false;
        return true;
    }

    bool IsValidReqId(std::string const& reqId)
    {
        if (reqId.empty() || reqId.size() > kMaxReqIdLen)
            return false;
        for (char c : reqId)
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z')))
                return false;
        return true;
    }

    bool StripPrefix(std::string const& wire, std::string& frame)
    {
        if (wire.size() <= kPrefixLen + 1 || wire.compare(0, kPrefixLen, kPrefix) != 0 || wire[kPrefixLen] != '\t')
            return false;
        frame = wire.substr(kPrefixLen + 1);
        return true;
    }

    ParseResult ParseFrame(std::string const& frame, Frame& out)
    {
        ParseResult result;
        std::string f[6];
        int const fieldCount = SplitHeader(frame, f);

        // reqId first (when present), so that even a version error can echo it.
        if (fieldCount >= 3 && IsValidReqId(f[2]))
            result.reqId = f[2];

        // Version comes first: "v" + digits (up to 9). A different number is a
        // version error whatever follows, since the layout can no longer be trusted.
        if (f[0].size() < 2 || f[0][0] != 'v')
            return result;
        uint32_t version = 0;
        if (!ParseSmallNumber(f[0].substr(1), 9, version))
            return result;
        if (version != static_cast<uint32_t>(kProtocolVersion))
        {
            result.status = ParseStatus::BadVersion;
            return result;
        }
        if (fieldCount < 6)
            return result;

        if (!IsValidOp(f[1]) || !IsValidReqId(f[2]))
            return result;
        uint32_t part = 0;
        uint32_t total = 0;
        if (!ParseSmallNumber(f[3], 3, part) || !ParseSmallNumber(f[4], 3, total))
            return result;
        if (part < 1 || total < 1 || part > total)
            return result;

        for (char c : f[5])
            if (!IsWirePayloadChar(static_cast<unsigned char>(c)))
                return result;
        std::string payload;
        if (!Unescape(f[5], payload))
            return result;

        out.version = kProtocolVersion;
        out.op = f[1];
        out.reqId = f[2];
        out.part = part;
        out.total = total;
        out.payload.swap(payload);
        result.status = ParseStatus::Ok;
        return result;
    }

    bool EncodeFrame(std::string const& op, std::string const& reqId, uint32_t part, uint32_t total,
                     std::string const& payload, std::string& outFrame, std::size_t maxWire)
    {
        if (!IsValidOp(op) || !IsValidReqId(reqId))
            return false;
        if (total < 1 || total > kMaxPartsHardLimit || part < 1 || part > total)
            return false;
        std::string frame = "v" + std::to_string(kProtocolVersion) + "\t" + op + "\t" + reqId + "\t"
            + std::to_string(part) + "\t" + std::to_string(total) + "\t" + Escape(payload);
        if (kPrefixLen + 1 + frame.size() > maxWire)
            return false;
        if (ContainsForbidden(frame))
            return false;
        outFrame.swap(frame);
        return true;
    }

    bool ChunkPayload(std::string const& op, std::string const& reqId, std::string const& payload,
                      std::vector<std::string>& outFrames, std::size_t maxWire)
    {
        outFrames.clear();
        if (!IsValidOp(op) || !IsValidReqId(reqId))
            return false;
        // prefix TAB + "v1" TAB + op TAB + reqId TAB + 2-digit part TAB + 2-digit total TAB
        std::size_t const header = kPrefixLen + 1 + 3 + op.size() + 1 + reqId.size() + 1 + 3 + 3;
        if (maxWire < header + 4)
            return false;
        std::size_t const budget = maxWire - header;

        std::vector<std::string> chunks;
        std::size_t i = 0;
        do
        {
            std::size_t const start = i;
            std::size_t used = 0;
            while (i < payload.size())
            {
                std::size_t const cost = EscapeStep(payload, i, nullptr);
                if (used + cost > budget)
                    break;
                used += cost;
                ++i;
            }
            chunks.push_back(payload.substr(start, i - start));
            if (chunks.size() > kMaxPartsHardLimit)
                return false;
        } while (i < payload.size());

        std::vector<std::string> frames;
        frames.reserve(chunks.size());
        for (std::size_t k = 0; k < chunks.size(); ++k)
        {
            std::string frame;
            if (!EncodeFrame(op, reqId, static_cast<uint32_t>(k + 1), static_cast<uint32_t>(chunks.size()),
                             chunks[k], frame, maxWire))
                return false;
            frames.push_back(std::move(frame));
        }
        outFrames.swap(frames);
        return true;
    }

    uint32_t Adler32(std::string const& data)
    {
        uint32_t a = 1;
        uint32_t b = 0;
        for (char ch : data)
        {
            a = (a + static_cast<unsigned char>(ch)) % 65521u;
            b = (b + a) % 65521u;
        }
        return (b << 16) | a;
    }

    std::string HexSum(uint32_t sum)
    {
        std::string out;
        for (int shift = 28; shift >= 0; shift -= 4)
            out.push_back(kHex[(sum >> shift) & 0x0f]);
        return out;
    }

    std::string MakePattern(std::size_t len)
    {
        static char const kAlphabet[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        std::string out;
        out.reserve(len);
        for (std::size_t i = 0; i < len; ++i)
            out.push_back(kAlphabet[(i * 7 + i / 62) % 62]);
        return out;
    }
}
