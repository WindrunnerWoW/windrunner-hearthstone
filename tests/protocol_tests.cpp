// Standalone unit tests for src/protocol, src/collection and src/decks (no framework, no core headers).
//   protocol_tests.exe [path-to-tests-dir]      run all tests
//   protocol_tests.exe --emit-vectors           print tests/golden_vectors.txt content
//   protocol_tests.exe --emit-transcript        print tests/transcript.txt content
// Exit code 0 = all checks passed.

#include "../src/protocol/HsProtocol.h"
#include "../src/protocol/HsRateLimiter.h"
#include "../src/protocol/HsReassembler.h"
#include "../src/protocol/HsSession.h"
#include "../src/arena/HsArenaWire.h"
#include "../src/collection/HsCollectionService.h"
#include "../src/collection/HsOwnership.h"
#include "../src/collection/HsPacks.h"
#include "../src/collection/HsTraderRules.h"
#include "../src/decks/HsDeckService.h"
#include "../src/decks/HsDecks.h"
#include "../src/HsDataset.h"
#include "../src/match/HsMatchHistory.h"
#include "../src/match/HsMatchService.h"
#include "../src/match/HsMatchWire.h"
#include "../src/match/HsOpponents.h"
#include "../src/quests/HsArenaIntroRules.h"
#include "../src/quests/HsClassQuestRules.h"
#include "../src/quests/HsHunterRules.h"
#include "../src/quests/HsHunterPresentation.h"
#include "../src/quests/HsAllianceMageRules.h"
#include "../src/quests/HsHordeShamanRules.h"
#include "../src/quests/HsWarlockRules.h"
#include "../src/quests/HsHordeWarriorRules.h"
#include "../src/quests/HsHordeMageRules.h"
#include "../src/quests/HsHordeDruidRules.h"
#include "../src/quests/HsHordePriestRules.h"
#include "../src/quests/HsAlliancePriestRules.h"
#include "../src/quests/HsHordePaladinRules.h"
#include "../src/quests/HsAlliancePaladinRules.h"
#include "../src/quests/HsAllianceShamanRules.h"
#include "../src/quests/HsAllianceShamanRoutes.h"
#include "../src/quests/HsAllianceDruidRules.h"
#include "../src/quests/HsHordeRogueRules.h"
#include "../src/quests/HsAllianceRogueRules.h"
#include "../src/quests/HsHordeRogueQuestView.h"
#include "../src/quests/HsNaxxQuestRules.h"
#include "match_test_cards.h"
#include "match_all_cards.h"
#include "../src/match/HsMatchInternal.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

using namespace Hs;

static int g_checks = 0;
static int g_failed = 0;
static char const* g_test = "";

#define CHECK(cond)                                                                                   \
    do {                                                                                              \
        ++g_checks;                                                                                   \
        if (!(cond)) {                                                                                \
            ++g_failed;                                                                               \
            std::printf("FAIL [%s] %s:%d: %s\n", g_test, __FILE__, __LINE__, #cond);                  \
        }                                                                                             \
    } while (0)

#define RUN(fn)                                                                                       \
    do {                                                                                              \
        g_test = #fn;                                                                                 \
        int const before = g_failed;                                                                  \
        fn();                                                                                         \
        std::printf("%-44s %s\n", #fn, g_failed == before ? "ok" : "FAILED");                         \
    } while (0)

namespace
{
    // Deterministic pseudo random generator (no <random> dependence on platform).
    struct Lcg
    {
        uint32_t s;
        explicit Lcg(uint32_t seed) : s(seed) {}
        uint32_t Next() { s = s * 1664525u + 1013904223u; return s >> 8; }
    };

    std::string AllBytes()
    {
        std::string s;
        for (int i = 0; i < 256; ++i)
            s.push_back(static_cast<char>(i));
        return s;
    }

    std::string Hex(std::string const& s)
    {
        static char const d[] = "0123456789abcdef";
        std::string o;
        for (char c : s)
        {
            o.push_back(d[(static_cast<unsigned char>(c) >> 4) & 15]);
            o.push_back(d[static_cast<unsigned char>(c) & 15]);
        }
        return o;
    }

    bool FromHex(std::string const& h, std::string& out)
    {
        if (h.size() % 2)
            return false;
        out.clear();
        for (std::size_t i = 0; i < h.size(); i += 2)
        {
            auto v = [](char c) { return c >= '0' && c <= '9' ? c - '0' : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1); };
            int const a = v(h[i]);
            int const b = v(h[i + 1]);
            if (a < 0 || b < 0)
                return false;
            out.push_back(static_cast<char>(a * 16 + b));
        }
        return true;
    }

    // Everything the golden vector files and fuzz tests are built from.
    std::vector<std::string> VectorInputs()
    {
        std::vector<std::string> v;
        v.push_back("");
        v.push_back("hello world");
        v.push_back("a~b");
        v.push_back("|");
        v.push_back(std::string("line1\nline2\r\n"));
        v.push_back(std::string("tab\there"));
        v.push_back(std::string("nul\0byte", 8));
        v.push_back("a:b:c");
        v.push_back("snake_case_name");
        v.push_back("LFT");
        v.push_back("LF");
        v.push_back("LLFT and LFLFT");
        v.push_back("TW_GUILDBANK");
        v.push_back("TWT_UDTSv4");
        v.push_back("GM_ADDON");
        v.push_back("MR:x TR:y");
        v.push_back("protocol=1;addon=0.1.0");
        v.push_back("\x7f\x80\xff\xc3\xa4");
        v.push_back(AllBytes());
        return v;
    }

    // ---- helpers for talking to a Session ------------------------------------
    struct Client
    {
        Session session;
        Settings settings;
        ServerInfo info;
        uint64_t now = 1000;
        int security = 0;
        CollectionHandler* collections = nullptr;
        ArenaHandler* arena = nullptr;

        Client()
        {
            info.moduleVersion = "9.9.9";
            info.datasetId = "hs-test";
            info.datasetHash = "0123456789abcdef";
        }

        std::vector<std::string> SendFrame(std::string const& frame)
        {
            std::vector<std::string> out;
            session.OnFrame(now, frame, security, settings, info, out, collections, nullptr, arena);
            return out;
        }

        std::vector<std::string> Send(std::string const& op, std::string const& reqId, uint32_t part, uint32_t total,
                                      std::string const& payload)
        {
            std::string frame;
            bool const ok = EncodeFrame(op, reqId, part, total, payload, frame);
            CHECK(ok);
            return SendFrame(frame);
        }

        // Single-frame request.
        std::vector<std::string> Send1(std::string const& op, std::string const& reqId, std::string const& payload)
        {
            return Send(op, reqId, 1, 1, payload);
        }

        // A request with a match handler (GAME_* ops).
        std::vector<std::string> SendGame(MatchHandler& match, std::string const& op, std::string const& reqId,
                                          std::string const& payload)
        {
            std::string frame;
            CHECK(EncodeFrame(op, reqId, 1, 1, payload, frame));
            std::vector<std::string> out;
            session.OnFrame(now, frame, security, settings, info, out, collections, &match, arena);
            return out;
        }

        void Hello()
        {
            auto out = Send1("HELLO", "h1", "protocol=1;addon=test;dataset=-");
            CHECK(!out.empty());
            CHECK(session.Handshaken());
        }
    };

    // Reassemble server output frames into complete replies keyed in order.
    struct Reply
    {
        std::string op, reqId, payload;
    };

    std::vector<Reply> Decode(std::vector<std::string> const& frames)
    {
        std::vector<Reply> replies;
        Reassembler r;
        ReassemblyLimits lim;
        lim.maxBytes = 1 << 20;
        lim.maxParts = 99;
        lim.maxPending = 64;
        for (std::string const& f : frames)
        {
            CHECK(kPrefixLen + 1 + f.size() <= kMaxWireBytes);
            CHECK(!ContainsForbidden(std::string(kPrefix) + "\t" + f));
            Frame fr;
            ParseResult pr = ParseFrame(f, fr);
            CHECK(pr.status == ParseStatus::Ok);
            if (pr.status != ParseStatus::Ok)
                continue;
            Completed done;
            AddStatus st = r.Add(0, lim, fr.op, fr.reqId, fr.part, fr.total, fr.payload, done);
            if (st == AddStatus::Complete)
                replies.push_back({ done.op, done.reqId, done.payload });
            else
                CHECK(st == AddStatus::Pending);
        }
        return replies;
    }

    std::string ErrCode(Reply const& r)
    {
        std::map<std::string, std::string> kv;
        if (r.op != "ERR" || !KvDecode(r.payload, kv))
            return "";
        return kv["code"];
    }

    std::string FirstErr(std::vector<std::string> const& frames)
    {
        auto replies = Decode(frames);
        return replies.empty() ? "" : ErrCode(replies[0]);
    }
}

// ---------------------------------------------------------------------------
static void TestAdlerAndPattern()
{
    CHECK(HexSum(Adler32("Wikipedia")) == "11e60398");
    CHECK(Adler32("") == 1u);
    CHECK(MakePattern(0).empty());
    CHECK(MakePattern(1000).size() == 1000);
    CHECK(MakePattern(300) == MakePattern(500).substr(0, 300));
    CHECK(MakePattern(8) == "07ELSZgn");
    for (char c : MakePattern(2000))
        CHECK((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'));
}

static void TestEscapeRoundTripAllBytes()
{
    for (int i = 0; i < 256; ++i)
    {
        std::string s(1, static_cast<char>(i));
        std::string e = Escape(s);
        std::string d;
        CHECK(Unescape(e, d));
        CHECK(d == s);
        CHECK(!ContainsForbidden(e));
        for (char c : e)
        {
            unsigned char u = static_cast<unsigned char>(c);
            CHECK(u >= 0x20 && u <= 0x7e && c != '|' && c != ':' && c != '_' && c != '\t');
        }
    }
    std::string all = AllBytes();
    std::string d;
    CHECK(Unescape(Escape(all), d) && d == all);
}

static void TestEscapeTriggersNeverAppear()
{
    char const* triggers[] = { "LFT", "TW_", "TWT_", "GM_ADDON", "MR:", "TR:", "TW_GUILDBANK", "TWT_UDTSv4",
                               "TW_TRANSMOG", "TW_CHAT_MSG_WHISPER", "TW_BUFF", "TW_SHOP", "TW_BGQueue", "TW_TITLES",
                               "TW_HONOR", "TW_Merchant", "LLFT", "LFLFT", "xLFTx", "LFTLFT", "L", "F", "T", "LF" };
    for (char const* t : triggers)
    {
        std::string e = Escape(t);
        CHECK(!ContainsForbidden(e));
        std::string d;
        CHECK(Unescape(e, d) && d == t);
    }
    // Every ordered combination of a few interesting bytes, length 1..5.
    char const alpha[] = { 'L', 'F', 'T', '_', ':', 'W', 'M', 'R', '~', '|', '\n', 'G' };
    std::size_t const n = sizeof(alpha);
    for (int len = 1; len <= 5; ++len)
    {
        std::size_t combos = 1;
        for (int i = 0; i < len; ++i)
            combos *= n;
        for (std::size_t c = 0; c < combos; ++c)
        {
            std::string s;
            std::size_t x = c;
            for (int i = 0; i < len; ++i)
            {
                s.push_back(alpha[x % n]);
                x /= n;
            }
            std::string e = Escape(s);
            CHECK(!ContainsForbidden(e));
            std::string d;
            CHECK(Unescape(e, d) && d == s);
        }
    }
    // Fuzz.
    Lcg rng(12345);
    for (int it = 0; it < 3000; ++it)
    {
        std::string s;
        std::size_t len = rng.Next() % 200;
        for (std::size_t i = 0; i < len; ++i)
        {
            // bias toward trigger letters
            static char const bias[] = "LFT_:WMRGADON~|\n\t";
            s.push_back((rng.Next() & 1) ? bias[rng.Next() % (sizeof(bias) - 1)] : static_cast<char>(rng.Next() & 255));
        }
        std::string e = Escape(s);
        std::string d;
        CHECK(!ContainsForbidden(e));
        CHECK(Unescape(e, d) && d == s);
    }
}

static void TestUnescapeRejectsGarbage()
{
    std::string out;
    CHECK(!Unescape("~", out));
    CHECK(!Unescape("abc~", out));
    CHECK(!Unescape("~z", out));
    CHECK(!Unescape("~x", out));
    CHECK(!Unescape("~x4", out));
    CHECK(!Unescape("~xzz", out));
    CHECK(Unescape("~x41", out) && out == "A");
    CHECK(Unescape("~x4A", out) && out == "J");
    CHECK(Unescape("", out) && out.empty());
    CHECK(!UnescapeValue("~", out));
    CHECK(!UnescapeValue("~z", out));
}

static void TestValueEscapeAndKv()
{
    std::string v = "a;b=c~d;;==";
    std::string e = EscapeValue(v);
    CHECK(e.find(';') == std::string::npos && e.find('=') == std::string::npos);
    std::string d;
    CHECK(UnescapeValue(e, d) && d == v);

    KvList kv;
    kv.push_back({ "protocol", "1" });
    kv.push_back({ "msg", "hello; world = ~ LFT TW_x" });
    kv.push_back({ "empty", "" });
    std::string payload = KvEncode(kv);
    std::map<std::string, std::string> m;
    CHECK(KvDecode(payload, m));
    CHECK(m.size() == 3 && m["protocol"] == "1" && m["msg"] == "hello; world = ~ LFT TW_x" && m["empty"].empty());

    // Full path: value escape then wire escape and back.
    std::string wire = Escape(payload);
    CHECK(!ContainsForbidden(wire));
    std::string back;
    CHECK(Unescape(wire, back) && back == payload);

    CHECK(KvDecode("", m) && m.empty());
    CHECK(!KvDecode("a", m));
    CHECK(!KvDecode("=b", m));
    CHECK(!KvDecode("a=b;", m));
    CHECK(!KvDecode(";a=b", m));
    CHECK(!KvDecode("a=b;a=c", m));
    CHECK(!KvDecode("a b=c", m));
    CHECK(!KvDecode("a=~z", m));
}

static void TestGoldenVectors(std::string const& dir)
{
    std::ifstream in(dir + "/golden_vectors.txt", std::ios::binary);
    CHECK(in.good());
    std::string line;
    std::size_t count = 0;
    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty() || line[0] == '#')
            continue;
        std::size_t tab = line.find('\t');
        CHECK(tab != std::string::npos);
        if (tab == std::string::npos)
            continue;
        std::string raw;
        CHECK(FromHex(line.substr(0, tab), raw));
        std::string enc = line.substr(tab + 1);
        CHECK(Escape(raw) == enc);
        std::string dec;
        CHECK(Unescape(enc, dec) && dec == raw);
        ++count;
    }
    CHECK(count >= VectorInputs().size());
}

static void TestFrameCodec()
{
    std::string f;
    CHECK(EncodeFrame("PING", "a1", 1, 1, "hi", f));
    CHECK(f == "v1\tPING\ta1\t1\t1\thi");
    Frame fr;
    ParseResult pr = ParseFrame(f, fr);
    CHECK(pr.status == ParseStatus::Ok && fr.op == "PING" && fr.reqId == "a1" && fr.part == 1 && fr.total == 1 && fr.payload == "hi");

    // payload with every kind of nasty byte survives
    std::string nasty = AllBytes();
    CHECK(EncodeFrame("ECHO", "zz9", 2, 5, nasty.substr(0, 20), f));
    pr = ParseFrame(f, fr);
    CHECK(pr.status == ParseStatus::Ok && fr.part == 2 && fr.total == 5 && fr.payload == nasty.substr(0, 20));

    // invalid encode arguments
    CHECK(!EncodeFrame("ping", "a1", 1, 1, "", f));
    CHECK(!EncodeFrame("PING", "", 1, 1, "", f));
    CHECK(!EncodeFrame("PING", "A1", 1, 1, "", f));
    CHECK(!EncodeFrame("PING", "123456789", 1, 1, "", f));
    CHECK(!EncodeFrame("PING", "a", 0, 1, "", f));
    CHECK(!EncodeFrame("PING", "a", 2, 1, "", f));
    CHECK(!EncodeFrame("PING", "a", 1, 100, "", f));
    CHECK(!EncodeFrame("TW_ABC", "a", 1, 1, "", f));       // op contains a trigger
    CHECK(!EncodeFrame("VERYLONGOPNAME", "a", 1, 1, "", f)); // > 12
    CHECK(!EncodeFrame("PING", "a", 1, 1, std::string(300, 'x'), f)); // too long

    // parse rejections
    CHECK(ParseFrame("", fr).status == ParseStatus::Malformed);
    CHECK(ParseFrame("v1\tPING\ta1\t1\t1", fr).status == ParseStatus::Malformed);   // only 4 tabs
    CHECK(ParseFrame("x1\tPING\ta1\t1\t1\t", fr).status == ParseStatus::Malformed);
    CHECK(ParseFrame("v\tPING\ta1\t1\t1\t", fr).status == ParseStatus::Malformed);
    CHECK(ParseFrame("v01\tPING\ta1\t1\t1\t", fr).status == ParseStatus::Malformed);
    CHECK(ParseFrame("v1\tping\ta1\t1\t1\t", fr).status == ParseStatus::Malformed);
    CHECK(ParseFrame("v1\tPING\tA1\t1\t1\t", fr).status == ParseStatus::Malformed);
    CHECK(ParseFrame("v1\tPING\ta1\t0\t1\t", fr).status == ParseStatus::Malformed);
    CHECK(ParseFrame("v1\tPING\ta1\t2\t1\t", fr).status == ParseStatus::Malformed);
    CHECK(ParseFrame("v1\tPING\ta1\t01\t1\t", fr).status == ParseStatus::Malformed);
    CHECK(ParseFrame("v1\tPING\ta1\t1\tx\t", fr).status == ParseStatus::Malformed);
    CHECK(ParseFrame("v1\tPING\ta1\t1\t1\tbad~z", fr).status == ParseStatus::Malformed);
    CHECK(ParseFrame("v1\tPING\ta1\t1\t1\tra\tw", fr).status == ParseStatus::Malformed); // raw tab in payload
    CHECK(ParseFrame("v1\tPING\ta1\t1\t1\ta|b", fr).status == ParseStatus::Malformed);
    CHECK(ParseFrame("v1\tPING\ta1\t1\t1\ta:b", fr).status == ParseStatus::Malformed);
    CHECK(ParseFrame("v1\tPING\ta1\t1\t1\ta_b", fr).status == ParseStatus::Malformed);
    CHECK(ParseFrame("v1\tPING\ta1\t1\t1\ta\x01" "b", fr).status == ParseStatus::Malformed);
    pr = ParseFrame("v1\tPING\ta1\t1\t1\ta\x01" "b", fr);
    CHECK(pr.reqId == "a1"); // best effort reqId for the error reply

    // version handling
    // The version is looked at first: any other version is BadVersion, even for a
    // layout with fewer fields (or a long version number).
    pr = ParseFrame("v2\tWHATEVER\tq7\tweird", fr);
    CHECK(pr.status == ParseStatus::BadVersion && pr.reqId == "q7");
    pr = ParseFrame("v2\tX", fr);
    CHECK(pr.status == ParseStatus::BadVersion && pr.reqId == "0");
    pr = ParseFrame("v2", fr);
    CHECK(pr.status == ParseStatus::BadVersion);
    pr = ParseFrame("v1000\tHELLO", fr);
    CHECK(pr.status == ParseStatus::BadVersion);
    CHECK(ParseFrame("v1", fr).status == ParseStatus::Malformed);
    CHECK(ParseFrame("v1\tPING", fr).status == ParseStatus::Malformed);
    CHECK(ParseFrame("v1\tPING\ta1\t1", fr).status == ParseStatus::Malformed);
    CHECK(ParseFrame("v1234567890\tPING", fr).status == ParseStatus::Malformed); // > 9 digits
    CHECK(ParseFrame("vx\tPING", fr).status == ParseStatus::Malformed);
    pr = ParseFrame("v2\tPING\tq7\t1\t1\tx", fr);
    CHECK(pr.status == ParseStatus::BadVersion && pr.reqId == "q7");
    pr = ParseFrame("v9\tPING\tq7\t1\t1\tx", fr);
    CHECK(pr.status == ParseStatus::BadVersion);
    pr = ParseFrame("v2\tPING\tQ!\t1\t1\tx", fr);
    CHECK(pr.status == ParseStatus::BadVersion && pr.reqId == "0");

    // payload = everything after the 5th tab, bounded characters only
    std::string wire = "WRHS\tv1\tPING\ta1\t1\t1\tpay";
    std::string stripped;
    CHECK(StripPrefix(wire, stripped) && stripped == "v1\tPING\ta1\t1\t1\tpay");
    CHECK(!StripPrefix("WRHS", stripped));
    CHECK(!StripPrefix("WRHS\t", stripped));
    CHECK(!StripPrefix("WRHSX\tv1", stripped));
    CHECK(!StripPrefix("XRHS\tv1", stripped));
}

static void TestChunkingAllSizes()
{
    // Payload lengths 0..1500 built from adversarial content; limits from 60 to 240.
    std::string const nasty = AllBytes() + "LFTLFLFTTW_TWT_GM_ADDONMR:TR:" + AllBytes();
    std::size_t const limits[] = { 240, 200, 120, 80, 60 };
    for (std::size_t limit : limits)
    {
        for (std::size_t len = 0; len <= 700; len += (len < 40 ? 1 : 13))
        {
            std::string payload;
            for (std::size_t i = 0; i < len; ++i)
                payload.push_back(nasty[(i * 5 + len) % nasty.size()]);
            std::vector<std::string> frames;
            bool ok = ChunkPayload("ECHO", "abcd1234", payload, frames, limit);
            if (!ok)
            {
                // Only allowed when the 99 part cap is exceeded.
                CHECK(len > 99 * 20);
                continue;
            }
            CHECK(!frames.empty() && frames.size() <= kMaxPartsHardLimit);
            Reassembler r;
            ReassemblyLimits lim;
            lim.maxBytes = 1 << 20;
            lim.maxParts = 99;
            lim.maxPending = 4;
            Completed done;
            AddStatus last = AddStatus::Pending;
            for (std::size_t k = 0; k < frames.size(); ++k)
            {
                CHECK(kPrefixLen + 1 + frames[k].size() <= limit);
                CHECK(!ContainsForbidden(std::string(kPrefix) + "\t" + frames[k]));
                Frame fr;
                CHECK(ParseFrame(frames[k], fr).status == ParseStatus::Ok);
                CHECK(fr.part == k + 1 && fr.total == frames.size());
                last = r.Add(0, lim, fr.op, fr.reqId, fr.part, fr.total, fr.payload, done);
            }
            CHECK(last == AddStatus::Complete);
            CHECK(done.payload == payload);
        }
    }
    // A limit that cannot even hold the header must fail cleanly.
    std::vector<std::string> frames;
    CHECK(!ChunkPayload("ECHO", "abcd1234", "x", frames, 20));
    CHECK(!ChunkPayload("echo", "abcd1234", "x", frames));
    // 99 part cap.
    CHECK(!ChunkPayload("ECHO", "a", std::string(30000, 'x'), frames));
    CHECK(ChunkPayload("ECHO", "a", std::string(1, 'x'), frames) && frames.size() == 1);
    CHECK(ChunkPayload("ECHO", "a", "", frames) && frames.size() == 1);
}

static void TestChunkingAllPayloadLengthsExhaustive240()
{
    // Every single length 0..2000 of the worst-case escape content (4 bytes each).
    for (std::size_t len = 0; len <= 2000; len += 7)
    {
        std::string payload(len, '\xff');
        std::vector<std::string> frames;
        if (!ChunkPayload("NPC_START", "12345678", payload, frames))
        {
            CHECK(len * 4 > 99 * 150);
            continue;
        }
        for (auto const& f : frames)
            CHECK(kPrefixLen + 1 + f.size() <= 240);
        auto replies = Decode(frames);
        CHECK(replies.size() == 1 && replies[0].payload == payload);
    }
}


// ---------------------------------------------------------------------------
namespace
{
    ReassemblyLimits Lim(std::size_t bytes = 4096, uint32_t parts = 16, uint32_t timeout = 10000, uint32_t pending = 4)
    {
        ReassemblyLimits l;
        l.maxBytes = bytes;
        l.maxParts = parts;
        l.timeoutMs = timeout;
        l.maxPending = pending;
        return l;
    }
}

static void TestReassemblerRoundTripAllPartCounts()
{
    for (uint32_t total = 1; total <= 16; ++total)
    {
        // in order, reversed, and pairwise swapped
        for (int order = 0; order < 3; ++order)
        {
            Reassembler r;
            std::vector<uint32_t> seq;
            for (uint32_t i = 1; i <= total; ++i)
                seq.push_back(i);
            if (order == 1)
                std::reverse(seq.begin(), seq.end());
            if (order == 2)
                for (std::size_t i = 0; i + 1 < seq.size(); i += 2)
                    std::swap(seq[i], seq[i + 1]);
            Completed done;
            AddStatus st = AddStatus::Pending;
            std::string expected;
            for (uint32_t i = 1; i <= total; ++i)
                expected += "part" + std::to_string(i) + ";";
            for (std::size_t k = 0; k < seq.size(); ++k)
            {
                st = r.Add(0, Lim(), "ECHO", "r1", seq[k], total, "part" + std::to_string(seq[k]) + ";", done);
                if (k + 1 < seq.size())
                    CHECK(st == AddStatus::Pending);
            }
            CHECK(st == AddStatus::Complete);
            CHECK(done.payload == expected && done.op == "ECHO" && done.reqId == "r1");
            CHECK(r.PendingCount() == 0);
        }
    }
}

static void TestReassemblerDuplicatePart()
{
    Reassembler r;
    Completed done;
    CHECK(r.Add(0, Lim(), "ECHO", "d1", 1, 3, "A", done) == AddStatus::Pending);
    CHECK(r.Add(0, Lim(), "ECHO", "d1", 1, 3, "A", done) == AddStatus::Ignored);
    CHECK(r.Add(0, Lim(), "ECHO", "d1", 1, 3, "DIFFERENT", done) == AddStatus::Ignored); // first one wins
    CHECK(r.Add(0, Lim(), "ECHO", "d1", 3, 3, "C", done) == AddStatus::Pending);
    CHECK(r.Add(0, Lim(), "ECHO", "d1", 3, 3, "C", done) == AddStatus::Ignored);
    CHECK(r.Add(0, Lim(), "ECHO", "d1", 2, 3, "B", done) == AddStatus::Complete);
    CHECK(done.payload == "ABC");
}

static void TestReassemblerConflictsAndBadNumbers()
{
    Reassembler r;
    Completed done;
    CHECK(r.Add(0, Lim(), "ECHO", "c1", 1, 3, "A", done) == AddStatus::Pending);
    CHECK(r.Add(0, Lim(), "ECHO", "c1", 2, 4, "B", done) == AddStatus::Malformed); // conflicting total
    CHECK(r.PendingCount() == 0);                                                   // request dropped
    CHECK(r.Add(0, Lim(), "ECHO", "c1", 3, 3, "C", done) == AddStatus::Ignored);    // late part of dropped request

    CHECK(r.Add(0, Lim(), "ECHO", "c2", 1, 3, "A", done) == AddStatus::Pending);
    CHECK(r.Add(0, Lim(), "PING", "c2", 2, 3, "B", done) == AddStatus::Malformed);  // op changed

    CHECK(r.Add(0, Lim(), "ECHO", "c3", 0, 3, "A", done) == AddStatus::Malformed);
    CHECK(r.Add(0, Lim(), "ECHO", "c3", 4, 3, "A", done) == AddStatus::Malformed);  // part > total
    CHECK(r.Add(0, Lim(), "ECHO", "c3", 1, 0, "A", done) == AddStatus::Malformed);
    CHECK(r.Add(0, Lim(), "ECHO", "c4", 1, 17, "A", done) == AddStatus::TooLarge);  // total > max
    CHECK(r.Add(0, Lim(), "ECHO", "c4", 2, 17, "A", done) == AddStatus::Ignored);   // answered only once
    CHECK(r.Add(0, Lim(), "ECHO", "c4", 3, 17, "A", done) == AddStatus::Ignored);
    // an over-limit total also drops an already pending request with that id
    CHECK(r.Add(0, Lim(), "ECHO", "c6", 1, 3, "A", done) == AddStatus::Pending);
    CHECK(r.Add(0, Lim(4096, 2), "ECHO", "c6", 2, 3, "B", done) == AddStatus::TooLarge);
    CHECK(r.PendingCount() == 0);
    CHECK(r.Add(0, Lim(), "ECHO", "c6", 3, 3, "C", done) == AddStatus::Ignored);
    CHECK(r.Add(0, Lim(4096, 4), "ECHO", "c5", 1, 5, "A", done) == AddStatus::TooLarge);
    CHECK(r.PendingCount() == 0);
}

static void TestReassemblerOversize()
{
    Reassembler r;
    Completed done;
    CHECK(r.Add(0, Lim(10), "ECHO", "o1", 1, 3, "123456", done) == AddStatus::Pending);
    CHECK(r.Add(0, Lim(10), "ECHO", "o1", 2, 3, "78901", done) == AddStatus::TooLarge); // 11 > 10
    CHECK(r.PendingCount() == 0);
    CHECK(r.Add(0, Lim(10), "ECHO", "o1", 3, 3, "x", done) == AddStatus::Ignored);
    // exactly at the limit is fine
    CHECK(r.Add(0, Lim(10), "ECHO", "o2", 1, 2, "12345", done) == AddStatus::Pending);
    CHECK(r.Add(0, Lim(10), "ECHO", "o2", 2, 2, "67890", done) == AddStatus::Complete);
    // single frame requests obey the bound too
    CHECK(r.Add(0, Lim(10), "ECHO", "o3", 1, 1, "12345678901", done) == AddStatus::TooLarge);
    CHECK(r.Add(0, Lim(10), "ECHO", "o3", 1, 1, "1234567890", done) == AddStatus::Complete);
}

static void TestReassemblerTooManyPending()
{
    Reassembler r;
    Completed done;
    for (int i = 0; i < 4; ++i)
        CHECK(r.Add(0, Lim(), "ECHO", "p" + std::to_string(i), 1, 2, "A", done) == AddStatus::Pending);
    CHECK(r.PendingCount() == 4);
    CHECK(r.Add(0, Lim(), "ECHO", "p9", 1, 2, "A", done) == AddStatus::Busy);
    CHECK(r.PendingCount() == 4);
    CHECK(r.Add(0, Lim(), "ECHO", "p9", 2, 2, "B", done) == AddStatus::Ignored); // rejected request's other parts
    // existing ones still complete, and that frees a slot
    CHECK(r.Add(0, Lim(), "ECHO", "p0", 2, 2, "B", done) == AddStatus::Complete);
    CHECK(r.Add(0, Lim(), "ECHO", "pa", 1, 2, "A", done) == AddStatus::Pending);
    // single-part requests never occupy a slot
    CHECK(r.Add(0, Lim(), "PING", "s1", 1, 1, "x", done) == AddStatus::Complete);
}

static void TestReassemblerTimeoutSweep()
{
    Reassembler r;
    Completed done;
    CHECK(r.Add(1000, Lim(), "ECHO", "t1", 1, 2, "A", done) == AddStatus::Pending);
    CHECK(r.Add(5000, Lim(), "ECHO", "t2", 1, 2, "A", done) == AddStatus::Pending);
    std::vector<std::string> expired;
    CHECK(r.Sweep(10999, 10000, expired) == 0 && expired.empty());
    CHECK(r.Sweep(11000, 10000, expired) == 1 && expired.size() == 1 && expired[0] == "t1");
    CHECK(r.PendingCount() == 1);
    CHECK(r.Add(11001, Lim(), "ECHO", "t1", 2, 2, "B", done) == AddStatus::Ignored); // late part after timeout
    expired.clear();
    CHECK(r.Sweep(15000, 10000, expired) == 1 && expired[0] == "t2");
    CHECK(r.PendingCount() == 0);
    // clock going backwards must not expire or crash
    CHECK(r.Add(20000, Lim(), "ECHO", "t3", 1, 2, "A", done) == AddStatus::Pending);
    expired.clear();
    CHECK(r.Sweep(10, 10000, expired) == 0);
}

static void TestReassemblerCompletedLru()
{
    Reassembler r;
    Completed done;
    // finish 16 requests: all remembered
    for (int i = 0; i < 16; ++i)
    {
        std::string id = "k" + std::to_string(i);
        CHECK(r.Add(0, Lim(), "ECHO", id, 1, 2, "A", done) == AddStatus::Pending);
        CHECK(r.Add(0, Lim(), "ECHO", id, 2, 2, "B", done) == AddStatus::Complete);
    }
    for (int i = 0; i < 16; ++i)
    {
        std::string id = "k" + std::to_string(i);
        CHECK(r.IsRemembered(id));
        // late duplicate of the last part: ignored, not re-handled
        CHECK(r.Add(0, Lim(), "ECHO", id, 2, 2, "B", done) == AddStatus::Ignored);
        CHECK(r.PendingCount() == 0);
    }
    // the 17th evicts the oldest
    CHECK(r.Add(0, Lim(), "ECHO", "k16", 1, 2, "A", done) == AddStatus::Pending);
    CHECK(r.Add(0, Lim(), "ECHO", "k16", 2, 2, "B", done) == AddStatus::Complete);
    CHECK(!r.IsRemembered("k0") && r.IsRemembered("k1") && r.IsRemembered("k16"));
    // k0 is no longer remembered: a new request with that id starts fresh
    CHECK(r.Add(0, Lim(), "ECHO", "k0", 1, 2, "A", done) == AddStatus::Pending);
    // single frame reuse of a remembered id is allowed
    CHECK(r.Add(0, Lim(), "PING", "k1", 1, 1, "x", done) == AddStatus::Complete);
    r.Clear();
    CHECK(!r.IsRemembered("k1") && r.PendingCount() == 0);
}

static void TestRateLimiter()
{
    RateLimiter rl;
    rl.Configure(10, 30);
    int ok = 0;
    for (int i = 0; i < 100; ++i)
        ok += rl.TryConsume(5000) ? 1 : 0;
    CHECK(ok == 30); // burst at the same instant
    CHECK(!rl.TryConsume(5050)); // 0.5 token so far
    CHECK(rl.TryConsume(5100));  // 1 token refilled at 10/s
    CHECK(!rl.TryConsume(5100));
    // long pause refills to burst, not beyond
    ok = 0;
    for (int i = 0; i < 100; ++i)
        ok += rl.TryConsume(600000) ? 1 : 0;
    CHECK(ok == 30);
    // sustained rate: one call every 100ms at 10/s is always allowed
    RateLimiter steady;
    steady.Configure(10, 1);
    int allowed = 0;
    for (int i = 0; i < 50; ++i)
        allowed += steady.TryConsume(static_cast<uint64_t>(i) * 100) ? 1 : 0;
    CHECK(allowed == 50);
    // 20 calls/s at 10/s allows about half
    RateLimiter fast;
    fast.Configure(10, 1);
    allowed = 0;
    for (int i = 0; i < 100; ++i)
        allowed += fast.TryConsume(static_cast<uint64_t>(i) * 50) ? 1 : 0;
    CHECK(allowed >= 50 && allowed <= 52);
    // time going backwards never mints tokens
    RateLimiter back;
    back.Configure(10, 2);
    CHECK(back.TryConsume(10000) && back.TryConsume(10000) && !back.TryConsume(10000));
    CHECK(!back.TryConsume(5000));
    // shrinking the burst clamps stored tokens
    RateLimiter shrink;
    shrink.Configure(10, 30);
    CHECK(shrink.TryConsume(0));
    shrink.Configure(10, 2);
    int n = 0;
    for (int i = 0; i < 10; ++i)
        n += shrink.TryConsume(0) ? 1 : 0;
    CHECK(n == 2);
    // notice throttle: at most once per second
    RateLimiter nt;
    CHECK(nt.AllowNotice(1000));
    CHECK(!nt.AllowNotice(1500));
    CHECK(!nt.AllowNotice(1999));
    CHECK(nt.AllowNotice(2000));
    CHECK(!nt.AllowNotice(2100));
    // degenerate config
    RateLimiter z;
    z.Configure(0, 0);
    CHECK(z.TryConsume(0));
}

static void TestSanitize()
{
    Settings s;
    s.maxInboundBytes = 0;
    s.maxInboundParts = 0;
    s.reassemblyTimeoutMs = 0;
    s.maxPendingRequests = 0;
    s.ratePerSecond = 0;
    s.rateBurst = 0;
    Settings lo = Sanitize(s);
    CHECK(lo.maxInboundBytes == 64 && lo.maxInboundParts == 1 && lo.reassemblyTimeoutMs == 500);
    CHECK(lo.maxPendingRequests == 1 && lo.ratePerSecond == 1 && lo.rateBurst == 1);
    s.maxInboundBytes = 0xFFFFFFFFu;
    s.maxInboundParts = 1000;
    s.reassemblyTimeoutMs = 0xFFFFFFFFu;
    s.maxPendingRequests = 1000;
    s.ratePerSecond = 100000;
    s.rateBurst = 100000;
    Settings hi = Sanitize(s);
    CHECK(hi.maxInboundBytes == 65536 && hi.maxInboundParts == 99 && hi.reassemblyTimeoutMs == 600000);
    CHECK(hi.maxPendingRequests == 64 && hi.ratePerSecond == 1000 && hi.rateBurst == 1000);
    Settings def = Sanitize(Settings());
    CHECK(def.maxInboundBytes == 4096 && def.maxInboundParts == 16 && def.rateBurst == 30);
}

// ---------------------------------------------------------------------------
static std::map<std::string, std::string> Kv(Reply const& r)
{
    std::map<std::string, std::string> m;
    CHECK(KvDecode(r.payload, m));
    return m;
}

static void TestSessionHandshake()
{
    Client c;
    // Before HELLO: only HELLO / PING
    CHECK(FirstErr(c.Send1("ECHO", "e1", "n=1")) == "NOT_READY");
    CHECK(FirstErr(c.Send1("COLL_LIST", "e2", "")) == "NOT_READY");
    CHECK(FirstErr(c.Send1("BOGUS", "e3", "")) == "NOT_READY");
    {
        auto rep = Decode(c.Send1("PING", "p0", "before"));
        CHECK(rep.size() == 1 && rep[0].op == "PONG" && rep[0].payload == "before" && rep[0].reqId == "p0");
    }
    CHECK(!c.session.Handshaken());

    auto rep = Decode(c.Send1("HELLO", "h1", "protocol=1;addon=0.1.0;dataset=-"));
    CHECK(rep.size() == 1 && rep[0].op == "WELCOME" && rep[0].reqId == "h1");
    auto kv = Kv(rep[0]);
    CHECK(kv["protocol"] == "1" && kv["server"] == "9.9.9" && kv["dataset"] == "hs-test");
    CHECK(kv["hash"] == "0123456789abcdef" && kv["caps"] == "solo,chunk,packs,decks,wallet,match" && kv["maxframe"] == "240" && kv["debug"] == "0");
    CHECK(c.session.Handshaken() && c.session.AddonVersion() == "0.1.0");

    // Debug flag is reported only to accounts that may use the debug ops
    c.settings.debug = true;
    rep = Decode(c.Send1("HELLO", "h2", "protocol=1;addon=x"));
    CHECK(Kv(rep[0])["debug"] == "0"); // security 0
    c.security = 1;
    rep = Decode(c.Send1("HELLO", "h3", "protocol=1;addon=x"));
    CHECK(Kv(rep[0])["debug"] == "0");
    c.security = 2;
    rep = Decode(c.Send1("HELLO", "h4", "protocol=1;addon=x"));
    CHECK(Kv(rep[0])["debug"] == "1");
    c.settings.debug = false;
    rep = Decode(c.Send1("HELLO", "h5", "protocol=1;addon=x"));
    CHECK(Kv(rep[0])["debug"] == "0");
    c.security = 0;

    // After HELLO: unknown / reserved
    CHECK(FirstErr(c.Send1("BOGUS", "u1", "")) == "UNKNOWN_OP");
    for (char const* op : { "COLL_LIST", "PACK_BUY", "DECK_EXPORT", "GAME_ACTION", "NPC_START" })
        CHECK(FirstErr(c.Send1(op, "r1", "")) == "NOT_IMPLEMENTED");
    CHECK(FirstErr(c.Send1("DECK_SAVE", "r2", "")) == "MALFORMED"); // no longer reserved
    // near misses are unknown, not reserved
    CHECK(FirstErr(c.Send1("COLLECT", "u2", "")) == "UNKNOWN_OP");
    CHECK(FirstErr(c.Send1("NPC_STARTX", "u3", "")) == "UNKNOWN_OP");
}

static void TestSessionHelloErrors()
{
    Client c;
    CHECK(FirstErr(c.Send1("HELLO", "a1", "protocol=2;addon=x")) == "VERSION");
    CHECK(!c.session.Handshaken());
    {
        auto rep = Decode(c.Send1("HELLO", "a1", "protocol=2;addon=x"));
        auto kv = Kv(rep[0]);
        CHECK(kv["msg"].find("Update the Hearthstone addon") != std::string::npos);
    }
    CHECK(FirstErr(c.Send1("HELLO", "a2", "protocol=abc")) == "MALFORMED");
    CHECK(FirstErr(c.Send1("HELLO", "a3", "addon=x")) == "MALFORMED");
    CHECK(FirstErr(c.Send1("HELLO", "a4", "garbage")) == "MALFORMED");
    CHECK(FirstErr(c.Send1("HELLO", "a5", "protocol=1;protocol=1")) == "MALFORMED");
    CHECK(!c.session.Handshaken());
    // header version mismatch on any op
    CHECK(FirstErr(c.SendFrame("v2\tHELLO\tq1\t1\t1\tprotocol=2")) == "VERSION");
    {
        auto rep = Decode(c.SendFrame("v7\tPING\tq9\t1\t1\t"));
        CHECK(rep.size() == 1 && rep[0].reqId == "q9" && ErrCode(rep[0]) == "VERSION");
    }
    // a handshaken client who later sends an incompatible HELLO loses the handshake
    c.Hello();
    CHECK(FirstErr(c.Send1("HELLO", "b1", "protocol=9")) == "VERSION");
    CHECK(!c.session.Handshaken());
    // malformed frames
    CHECK(FirstErr(c.SendFrame("garbage")) == "MALFORMED");
    {
        auto rep = Decode(c.SendFrame("v1\tPING\tab\t3\t2\tx"));
        CHECK(rep.size() == 1 && rep[0].reqId == "ab" && ErrCode(rep[0]) == "MALFORMED");
    }
    // oversize frame
    CHECK(FirstErr(c.SendFrame("v1\tPING\tab\t1\t1\t" + std::string(300, 'x'))) == "TOO_LARGE");
    // re-HELLO resets reassembly state (reqId reuse after /reload)
    c.settings.debug = true;
    c.security = 2;
    c.Hello();
    c.Send("ECHO", "m1", 1, 2, "data=abc");
    CHECK(c.session.PendingCount() == 1);
    c.Hello();
    CHECK(c.session.PendingCount() == 0);
}

static void TestSessionDisabled()
{
    Client c;
    c.settings.enable = false;
    auto rep = Decode(c.Send1("HELLO", "h1", "protocol=1"));
    CHECK(rep.size() == 1 && ErrCode(rep[0]) == "DISABLED" && rep[0].reqId == "h1");
    CHECK(!c.session.Handshaken());
    CHECK(c.Send1("PING", "p1", "x").empty());
    CHECK(c.Send1("ECHO", "e1", "n=1").empty());
    CHECK(c.SendFrame("garbage").empty());
    CHECK(c.SendFrame("v9\tHELLO\tq\t1\t1\t").empty());
    // hammering while disabled stays silent
    for (int i = 0; i < 100; ++i)
        CHECK(c.Send1("PING", "p" + std::to_string(i), "").empty());
    // Re-enabled works again
    c.settings.enable = true;
    c.now += 60000;
    c.Hello();
}

static void TestSessionPing()
{
    Client c;
    c.Hello();
    auto rep = Decode(c.Send1("PING", "p1", "a;b=c|d:e_f\n~LFT"));
    CHECK(rep.size() == 1 && rep[0].op == "PONG" && rep[0].payload == "a;b=c|d:e_f\n~LFT");
    rep = Decode(c.Send1("PING", "p2", std::string(kMaxPingPayload, 'x')));
    CHECK(rep.size() == 1 && rep[0].op == "PONG" && rep[0].payload.size() == kMaxPingPayload);
    rep = Decode(c.Send1("PING", "p3", std::string(kMaxPingPayload + 1, 'x')));
    CHECK(rep.size() == 1 && ErrCode(rep[0]) == "TOO_LARGE");
    rep = Decode(c.Send1("PING", "p4", std::string(30, '\x01')));
    CHECK(rep.size() == 1 && rep[0].payload == std::string(30, '\x01'));
}

static void TestSessionEchoDebugGating()
{
    Client c;
    c.Hello();
    // Debug off
    c.security = 4;
    CHECK(FirstErr(c.Send1("ECHO", "e1", "n=1")) == "NOT_IMPLEMENTED");
    // Debug on but player security too low
    c.settings.debug = true;
    c.security = 0;
    CHECK(FirstErr(c.Send1("ECHO", "e2", "n=1")) == "NOT_IMPLEMENTED");
    c.security = 1;
    CHECK(FirstErr(c.Send1("ECHO", "e3", "n=1")) == "NOT_IMPLEMENTED");
    c.security = 2;
    auto rep = Decode(c.Send1("ECHO", "e4", "n=1"));
    CHECK(rep.size() == 1 && rep[0].op == "ECHO");
}

static void TestSessionEchoOutboundChunks()
{
    Client c;
    c.settings.debug = true;
    c.security = 2;
    c.Hello();
    for (uint32_t n : { 1u, 2u, 3u, 10u, 25u, 40u })
    {
        auto frames = c.Send1("ECHO", "ec" + std::to_string(n), "n=" + std::to_string(n));
        CHECK(frames.size() >= (n >= 2 ? 2u : 1u));
        auto rep = Decode(frames);
        CHECK(rep.size() == 1 && rep[0].op == "ECHO");
        auto kv = Kv(rep[0]);
        CHECK(kv["n"] == std::to_string(n));
        CHECK(kv["len"] == std::to_string(n * 100));
        CHECK(kv["data"].size() == n * 100u);
        CHECK(kv["data"] == MakePattern(n * 100));
        CHECK(kv["sum"] == HexSum(Adler32(kv["data"])));
    }
    // bad n
    CHECK(FirstErr(c.Send1("ECHO", "b0", "n=0")) == "MALFORMED");
    CHECK(FirstErr(c.Send1("ECHO", "b1", "n=41")) == "MALFORMED");
    CHECK(FirstErr(c.Send1("ECHO", "b2", "n=x")) == "MALFORMED");
    CHECK(FirstErr(c.Send1("ECHO", "b3", "foo=1")) == "MALFORMED");
    CHECK(FirstErr(c.Send1("ECHO", "b4", "zzz")) == "MALFORMED");
}

static void TestSessionEchoInboundMultiPart()
{
    Client c;
    c.settings.debug = true;
    c.security = 2;
    c.Hello();
    for (std::size_t bytes : { 10u, 250u, 1000u, 3000u })
    {
        std::string const data = MakePattern(bytes);
        std::vector<std::string> frames;
        CHECK(ChunkPayload("ECHO", "in" + std::to_string(bytes), "data=" + data, frames));
        CHECK(frames.size() >= 1 && frames.size() <= 16);
        std::vector<std::string> out;
        for (std::string const& f : frames)
        {
            auto o = c.SendFrame(f);
            out.insert(out.end(), o.begin(), o.end());
            c.now += 20;
        }
        auto rep = Decode(out);
        CHECK(rep.size() == 1 && rep[0].op == "ECHO");
        auto kv = Kv(rep[0]);
        CHECK(kv["len"] == std::to_string(bytes));
        CHECK(kv["sum"] == HexSum(Adler32(data)));
    }
    // too many parts declared
    c.settings.maxInboundParts = 4;
    CHECK(FirstErr(c.Send("ECHO", "x1", 1, 5, "data=a")) == "TOO_LARGE");
    // oversize
    c.settings.maxInboundBytes = 100;
    c.settings = Sanitize(c.settings);
    CHECK(c.Send("ECHO", "x2", 1, 3, "data=" + std::string(70, 'a')).empty());
    CHECK(FirstErr(c.Send("ECHO", "x2", 2, 3, std::string(70, 'a'))) == "TOO_LARGE");
    CHECK(c.Send("ECHO", "x2", 3, 3, "late").empty()); // silently ignored after the drop
    // conflicting total
    c.settings = Settings();
    c.settings.debug = true;
    c.now += 10000; // let the rate limiter refill
    CHECK(c.Send("ECHO", "x3", 1, 3, "data=a").empty());
    CHECK(FirstErr(c.Send("ECHO", "x3", 2, 4, "b")) == "MALFORMED");
    // too many pending
    for (int i = 0; i < 4; ++i)
        CHECK(c.Send("ECHO", "pp" + std::to_string(i), 1, 2, "data=a").empty());
    CHECK(FirstErr(c.Send("ECHO", "ppx", 1, 2, "data=a")) == "BUSY");
    CHECK(c.Send("ECHO", "ppx", 2, 2, "b").empty());
}

static void TestSessionTimeoutSweep()
{
    Client c;
    c.settings.debug = true;
    c.security = 2;
    c.Hello();
    c.Send("ECHO", "t1", 1, 2, "data=a");
    c.now += 5000;
    c.Send("ECHO", "t2", 1, 2, "data=a");
    std::vector<std::string> out;
    c.session.Sweep(c.now + 4999, c.settings, out);
    CHECK(out.empty());
    c.session.Sweep(c.now + 5000, c.settings, out); // t1 is 10s old now
    auto rep = Decode(out);
    CHECK(rep.size() == 1 && rep[0].reqId == "t1" && ErrCode(rep[0]) == "TIMEOUT");
    CHECK(c.session.PendingCount() == 1);
    // late part of the expired request is ignored, not re-handled
    CHECK(c.Send("ECHO", "t1", 2, 2, "b").empty());
    // disabled -> no error frames but the state is still freed
    c.settings.enable = false;
    out.clear();
    c.session.Sweep(c.now + 100000, c.settings, out);
    CHECK(out.empty() && c.session.PendingCount() == 0);
}

static void TestSessionRateLimit()
{
    Client c;
    c.settings.ratePerSecond = 10;
    c.settings.rateBurst = 30;
    c.Hello(); // 1 token used
    int answered = 0;
    int rateErrors = 0;
    for (int i = 0; i < 100; ++i)
    {
        auto out = c.Send1("PING", "r" + std::to_string(i % 10), "x");
        for (auto const& rep : Decode(out))
        {
            if (rep.op == "PONG")
                ++answered;
            else if (ErrCode(rep) == "RATE")
                ++rateErrors;
        }
    }
    CHECK(answered == 29);  // 30 burst - 1 for HELLO
    CHECK(rateErrors == 1); // one notice per second, all at the same instant
    // Hammering for 3 seconds produces only a few notices
    rateErrors = 0;
    for (int ms = 0; ms < 3000; ms += 10)
    {
        c.now += 10;
        for (int k = 0; k < 5; ++k)
            for (auto const& rep : Decode(c.Send1("PING", "z", "x")))
                if (ErrCode(rep) == "RATE")
                    ++rateErrors;
    }
    CHECK(rateErrors >= 2 && rateErrors <= 4);
    // A well behaved client recovers
    c.now += 5000;
    CHECK(Decode(c.Send1("PING", "ok", "x")).size() == 1);
    // config lowering the rate takes effect immediately
    c.settings.rateBurst = 1;
    c.settings.ratePerSecond = 1;
    c.now += 1000;
    CHECK(Decode(c.Send1("PING", "ok1", "x")).size() == 1);
    auto out = c.Send1("PING", "ok2", "x");
    CHECK(FirstErr(out) == "RATE");
}

static void TestSessionAllRepliesFitWire()
{
    // Feed a spread of hostile inputs; every produced frame must be a valid
    // <=240 byte frame without forbidden sequences.
    Client c;
    c.settings.debug = true;
    c.security = 4;
    c.settings.ratePerSecond = 1000;
    c.settings.rateBurst = 1000;
    c.Hello();
    Lcg rng(777);
    static char const* const ops[] = { "PING", "ECHO", "HELLO", "COLL_X", "NOPE", "LFT", "TW_X", "GAME_A" };
    for (int i = 0; i < 4000; ++i)
    {
        std::string frame;
        int const kind = static_cast<int>(rng.Next() % 5);
        if (kind == 0)
        {
            for (std::size_t k = rng.Next() % 100; k > 0; --k)
                frame.push_back(static_cast<char>(rng.Next() & 255));
        }
        else
        {
            std::string payload;
            for (std::size_t k = rng.Next() % 60; k > 0; --k)
                payload.push_back(static_cast<char>(rng.Next() & 255));
            std::string ok;
            uint32_t const total = 1 + rng.Next() % 5;
            uint32_t const part = 1 + rng.Next() % total;
            std::string const reqId = "f" + std::to_string(rng.Next() % 40);
            std::string const op = ops[rng.Next() % 8];
            if (EncodeFrame(op, reqId, part, total, payload, ok))
                frame = ok;
            else
                frame = "v1\t" + op + "\t" + reqId + "\t1\t1\t" + payload;
        }
        c.now += 5;
        auto out = c.SendFrame(frame);
        for (auto const& f : out)
        {
            CHECK(kPrefixLen + 1 + f.size() <= kMaxWireBytes);
            CHECK(!ContainsForbidden(std::string(kPrefix) + "\t" + f));
            Frame fr;
            CHECK(ParseFrame(f, fr).status == ParseStatus::Ok);
        }
        std::vector<std::string> sweep;
        c.session.Sweep(c.now, c.settings, sweep);
        for (auto const& f : sweep)
            CHECK(kPrefixLen + 1 + f.size() <= kMaxWireBytes);
        CHECK(c.session.PendingCount() <= c.settings.maxPendingRequests);
    }
}

// ---------------------------------------------------------------------------
// Collection

namespace
{
    // A slice of the real dataset plus a few synthetic rows for edge cases.
    std::shared_ptr<CardCatalog const> TestCatalog()
    {
        auto c = std::make_shared<CardCatalog>();
        CHECK(c->Add("CS2_022", true, "standard", "FREE", "MAGE"));             // Polymorph (Basic)
        CHECK(c->Add("EX1_116", true, "standard", "LEGENDARY", "NEUTRAL"));     // Leeroy Jenkins
        CHECK(c->Add("EX1_062", true, "reward_only", "LEGENDARY", "NEUTRAL"));  // Old Murk-Eye
        CHECK(c->Add("NEW1_016", true, "reward_only", "EPIC", "NEUTRAL"));      // Captain's Parrot
        CHECK(c->Add("NEW1_006", true, "unreleased", "EPIC", "ROGUE"));         // Adrenaline Rush
        CHECK(c->Add("CS1_113e", false, "none", "COMMON", "NEUTRAL"));          // enchantment
        CHECK(c->Add("TST_nc", false, "standard", "COMMON", "NEUTRAL"));        // synthetic: not collectible
        CHECK(c->Add("TST_none", true, "none", "", "NEUTRAL"));                 // synthetic: collectible but none
        return c;
    }

    struct FakeStorage : CollectionStorage
    {
        std::map<uint32_t, Collection> db;
        std::vector<std::pair<uint32_t, Mutation>> commits;
        int loads = 0;
        bool failLoad = false;
        bool failCommit = false;

        // packs
        std::map<uint32_t, PackState> packs;
        std::map<uint32_t, std::vector<PackOpening>> openings; // oldest first
        std::vector<std::pair<uint32_t, PackStockChange>> stockCommits;
        std::vector<std::pair<uint32_t, PackOpenCommit>> openCommits;
        int packLoads = 0;
        int finds = 0;
        bool failPackLoad = false;
        bool failFind = false;
        bool failPackCommit = false;
        bool commitThenFail = false; // the opening commits but the caller is told it failed

        bool LoadPacks(uint32_t accountId, PackState& out, std::vector<PackOpening>& recent) override
        {
            ++packLoads;
            if (failPackLoad)
                return false;
            out = packs.count(accountId) ? packs[accountId] : PackState();
            recent.clear();
            std::vector<PackOpening> const& all = openings[accountId];
            for (auto it = all.rbegin(); it != all.rend() && recent.size() < kRecentOpenings; ++it)
                recent.push_back(*it);
            return true;
        }

        bool FindPackOpening(uint32_t accountId, std::string const& key, bool& found, PackOpening& out) override
        {
            ++finds;
            if (failFind)
                return false;
            found = false;
            for (PackOpening const& o : openings[accountId])
                if (o.key == key)
                {
                    found = true;
                    out = o;
                }
            return true;
        }

        bool CommitPackStock(uint32_t accountId, PackStockChange const& c) override
        {
            if (failPackCommit)
                return false;
            stockCommits.push_back(std::make_pair(accountId, c));
            packs[accountId] = c.after;
            return true;
        }

        bool CommitPackOpen(uint32_t accountId, PackOpenCommit const& c) override
        {
            if (failPackCommit)
                return false;
            for (PackOpening const& o : openings[accountId])
                if (o.key == c.opening.key)
                    return false; // unique key
            openCommits.push_back(std::make_pair(accountId, c));
            packs[accountId] = c.after;
            openings[accountId].push_back(c.opening);
            Collection& coll = db[accountId];
            coll.version = c.versionAfter;
            for (Mutation const& m : c.cards)
                coll.Set(m.cardId, m.after);
            return !commitThenFail;
        }

        bool Load(uint32_t accountId, Collection& out) override
        {
            ++loads;
            if (failLoad)
                return false;
            auto const it = db.find(accountId);
            out = it == db.end() ? Collection() : it->second;
            return true;
        }

        bool Commit(uint32_t accountId, Mutation const& m) override
        {
            if (failCommit)
                return false;
            commits.push_back(std::make_pair(accountId, m));
            Collection& c = db[accountId];
            c.version = m.versionAfter;
            c.Set(m.cardId, m.after);
            return true;
        }

        std::vector<std::pair<uint32_t, std::vector<Mutation>>> batches;
        bool CommitCards(uint32_t accountId, std::vector<Mutation> const& cards) override
        {
            if (failCommit)
                return false;
            batches.push_back(std::make_pair(accountId, cards));
            Collection& c = db[accountId];
            for (Mutation const& m : cards)
            {
                c.version = m.versionAfter;
                c.Set(m.cardId, m.after);
            }
            return true;
        }

        // Economy
        std::map<uint32_t, Wallet> wallets;
        std::vector<std::pair<uint32_t, WalletChange>> walletCommits;
        std::map<uint32_t, std::vector<CraftRecord>> craftOps;
        std::vector<std::pair<uint32_t, CraftCommit>> craftCommits;
        int walletLoads = 0;
        bool failWalletLoad = false;
        bool failWalletCommit = false;
        bool failFindCraft = false;
        bool failCraftCommit = false;
        std::vector<std::pair<uint32_t, CraftExtrasCommit>> extrasCommits;

        bool LoadWallet(uint32_t accountId, Wallet& out) override
        {
            ++walletLoads;
            if (failWalletLoad)
                return false;
            out = wallets.count(accountId) ? wallets[accountId] : Wallet();
            return true;
        }

        bool CommitWallet(uint32_t accountId, WalletChange const& c) override
        {
            if (failWalletCommit)
                return false;
            walletCommits.push_back(std::make_pair(accountId, c));
            wallets[accountId] = c.after;
            return true;
        }

        bool FindCraftOp(uint32_t accountId, std::string const& key, bool& found, CraftRecord& out) override
        {
            if (failFindCraft)
                return false;
            found = false;
            for (CraftRecord const& r : craftOps[accountId])
                if (r.key == key)
                {
                    found = true;
                    out = r;
                }
            return true;
        }

        bool CommitCraft(uint32_t accountId, CraftCommit const& c) override
        {
            if (failCraftCommit)
                return false;
            for (CraftRecord const& r : craftOps[accountId])
                if (r.key == c.record.key)
                    return false; // unique key
            craftCommits.push_back(std::make_pair(accountId, c));
            craftOps[accountId].push_back(c.record);
            Collection& coll = db[accountId];
            coll.version = c.card.versionAfter;
            coll.Set(c.card.cardId, c.card.after);
            wallets[accountId] = c.wallet.after;
            return true;
        }

        bool CommitCraftExtras(uint32_t accountId, CraftExtrasCommit const& c) override
        {
            if (failCraftCommit)
                return false;
            for (CraftRecord const& r : craftOps[accountId])
                if (r.key == c.record.key)
                    return false; // unique key
            extrasCommits.push_back(std::make_pair(accountId, c));
            craftOps[accountId].push_back(c.record);
            Collection& coll = db[accountId];
            for (Mutation const& m : c.cards)
            {
                coll.version = m.versionAfter;
                coll.Set(m.cardId, m.after);
            }
            wallets[accountId] = c.wallet.after;
            return true;
        }
    };

    struct FakeCollections : CollectionHandler
    {
        Collection collection;
        bool fail = false;
        int calls = 0;

        bool GetCollection(Collection& out) override
        {
            ++calls;
            if (fail)
                return false;
            out = collection;
            return true;
        }
    };

    CardCount Count(uint32_t p, uint32_t u)
    {
        CardCount c;
        c.protectedQty = p;
        c.unprotectedQty = u;
        return c;
    }
}

static void TestCardCatalog()
{
    CardCatalog c;
    CHECK(c.Add("CS2_022", true, "standard", "FREE", "MAGE"));
    CHECK(!c.Add("CS2_022", true, "standard", "FREE", "MAGE"));   // duplicate
    CHECK(!c.Add("", true, "standard", "FREE", "NEUTRAL"));
    CHECK(!c.Add("bad id", true, "standard", "FREE", "NEUTRAL"));
    CHECK(!c.Add("a;b", true, "standard", "FREE", "NEUTRAL"));
    CHECK(!c.Add("x'y", true, "standard", "FREE", "NEUTRAL"));
    CHECK(!c.Add(std::string(33, 'A'), true, "standard", "FREE", "NEUTRAL"));
    CHECK(c.Add(std::string(32, 'A'), true, "standard", "FREE", "NEUTRAL"));
    CHECK(!c.Add("TST_1", true, "Standard", "FREE", "NEUTRAL"));  // acquisition is case-sensitive (DB enum)
    CHECK(!c.Add("TST_2", true, "", "FREE", "NEUTRAL"));
    CHECK(c.Add("CS1_113e", false, "none", "COMMON", "NEUTRAL"));
    CHECK(c.Size() == 3);

    CHECK(c.Resolve("CS2_022") == "CS2_022");
    CHECK(c.Resolve("cs2_022") == "CS2_022");
    CHECK(c.Resolve("cs1_113E") == "CS1_113e");
    CHECK(c.Resolve("CS2_02").empty());
    CHECK(c.Resolve("").empty());
    CHECK(c.Find("cs2_022") == nullptr); // Find takes the canonical id
    CHECK(c.Find("CS2_022") != nullptr && !c.Find("CS2_022")->legendary);

    // two ids that differ only by case: only exact spellings resolve
    CHECK(c.Add("AB_x", true, "standard", "LEGENDARY", "NEUTRAL"));
    CHECK(c.Add("AB_X", true, "standard", "COMMON", "NEUTRAL"));
    CHECK(c.Resolve("AB_x") == "AB_x" && c.Resolve("AB_X") == "AB_X");
    CHECK(c.Resolve("ab_x").empty());
    CHECK(c.Find("AB_x")->legendary && !c.Find("AB_X")->legendary);

    Acquisition a;
    CHECK(ParseAcquisition("reward_only", a) && a == Acquisition::RewardOnly);
    CHECK(ParseAcquisition("unreleased", a) && a == Acquisition::Unreleased);
    CHECK(!ParseAcquisition("REWARD_ONLY", a));
    ProtectionMode m;
    CHECK(ParseProtectionMode("Reward", m) && m == ProtectionMode::Reward);
    CHECK(ParseProtectionMode("PROTECTED", m) && m == ProtectionMode::Protected);
    CHECK(ParseProtectionMode("unprotected", m) && m == ProtectionMode::Unprotected);
    CHECK(!ParseProtectionMode("prot", m) && !ParseProtectionMode("", m));
    CHECK(std::string(ProtectionModeName(ProtectionMode::Reward)) == "reward");
}

static void TestValidateGrant()
{
    auto const cat = TestCatalog();
    std::string id;
    CardRule rule;
    for (GrantSource src : { GrantSource::Pack, GrantSource::Quest, GrantSource::SpecialReward, GrantSource::Gm })
    {
        CHECK(ValidateGrant(*cat, "NOPE_1", src, id, rule) == OwnershipError::UnknownCard);
        CHECK(ValidateGrant(*cat, "", src, id, rule) == OwnershipError::UnknownCard);
        CHECK(ValidateGrant(*cat, "CS1_113e", src, id, rule) == OwnershipError::NotCollectible);
        CHECK(ValidateGrant(*cat, "TST_nc", src, id, rule) == OwnershipError::NotCollectible);
        CHECK(ValidateGrant(*cat, "NEW1_006", src, id, rule) == OwnershipError::NotObtainable);
        CHECK(ValidateGrant(*cat, "TST_none", src, id, rule) == OwnershipError::NotObtainable);
        id.clear();
        CHECK(ValidateGrant(*cat, "cs2_022", src, id, rule) == OwnershipError::None);
        CHECK(id == "CS2_022" && rule.acquisition == Acquisition::Standard && !rule.legendary);
    }
    // reward_only: special rewards and GMs only
    CHECK(ValidateGrant(*cat, "EX1_062", GrantSource::Pack, id, rule) == OwnershipError::SourceNotAllowed);
    CHECK(ValidateGrant(*cat, "EX1_062", GrantSource::Quest, id, rule) == OwnershipError::SourceNotAllowed);
    CHECK(ValidateGrant(*cat, "NEW1_016", GrantSource::Pack, id, rule) == OwnershipError::SourceNotAllowed);
    CHECK(ValidateGrant(*cat, "ex1_062", GrantSource::SpecialReward, id, rule) == OwnershipError::None);
    CHECK(id == "EX1_062" && rule.acquisition == Acquisition::RewardOnly && rule.legendary);
    CHECK(ValidateGrant(*cat, "NEW1_016", GrantSource::Gm, id, rule) == OwnershipError::None);
    for (int e = 0; e <= static_cast<int>(OwnershipError::NotEnough); ++e)
        CHECK(std::string(OwnershipErrorText(static_cast<OwnershipError>(e))).size() > 1);
}

static void TestPlanGrantBounds()
{
    CardRule std1;
    std1.collectible = true;
    std1.acquisition = Acquisition::Standard;
    CardCount const before = Count(1, 2);
    CardCount after = Count(77, 77);

    CHECK(PlanGrant(std1, before, 0, ProtectionMode::Unprotected, after) == OwnershipError::BadCount);
    CHECK(PlanGrant(std1, before, 100, ProtectionMode::Unprotected, after) == OwnershipError::BadCount);
    CHECK(PlanGrant(std1, before, 0xFFFFFFFFu, ProtectionMode::Unprotected, after) == OwnershipError::BadCount);
    CHECK(after == Count(77, 77)); // untouched on error
    CHECK(PlanGrant(std1, before, 1, ProtectionMode::Unprotected, after) == OwnershipError::None);
    CHECK(after == Count(1, 3));
    CHECK(PlanGrant(std1, before, 99, ProtectionMode::Unprotected, after) == OwnershipError::None);
    CHECK(after == Count(1, 101));

    // total per card <= 999
    CHECK(PlanGrant(std1, Count(400, 500), 99, ProtectionMode::Unprotected, after) == OwnershipError::None);
    CHECK(after.Total() == 999);
    after = Count(5, 5);
    CHECK(PlanGrant(std1, Count(400, 501), 99, ProtectionMode::Unprotected, after) == OwnershipError::Overflow);
    CHECK(PlanGrant(std1, Count(999, 0), 1, ProtectionMode::Protected, after) == OwnershipError::Overflow);
    CHECK(PlanGrant(std1, Count(0, 999), 1, ProtectionMode::Reward, after) == OwnershipError::Overflow);
    // a row above the bound (edited by hand) must not wrap around
    CHECK(PlanGrant(std1, Count(60000, 60000), 1, ProtectionMode::Unprotected, after) == OwnershipError::Overflow);
    CHECK(after == Count(5, 5));
    static_assert(kMaxCountPerOp == 99 && kMaxCopiesPerCard == 999, "contract bounds");
}

static void TestPlanGrantModes()
{
    CardRule common;
    common.collectible = true;
    common.acquisition = Acquisition::Standard;
    CardRule legendary = common;
    legendary.legendary = true;
    CardRule rewardOnly = common;
    rewardOnly.acquisition = Acquisition::RewardOnly;
    CardRule rewardOnlyLegendary = rewardOnly;
    rewardOnlyLegendary.legendary = true;
    CardCount after;

    CHECK(PlanGrant(common, Count(0, 0), 3, ProtectionMode::Unprotected, after) == OwnershipError::None && after == Count(0, 3));
    CHECK(PlanGrant(common, Count(0, 0), 3, ProtectionMode::Protected, after) == OwnershipError::None && after == Count(3, 0));

    // Reward: fill missing playable copies (2, Legendary 1) as protected, counting ALL owned copies
    CHECK(PlanGrant(common, Count(0, 0), 1, ProtectionMode::Reward, after) == OwnershipError::None && after == Count(1, 0));
    CHECK(PlanGrant(common, Count(0, 0), 2, ProtectionMode::Reward, after) == OwnershipError::None && after == Count(2, 0));
    CHECK(PlanGrant(common, Count(0, 0), 3, ProtectionMode::Reward, after) == OwnershipError::None && after == Count(2, 1));
    CHECK(PlanGrant(common, Count(1, 0), 3, ProtectionMode::Reward, after) == OwnershipError::None && after == Count(2, 2));
    CHECK(PlanGrant(common, Count(0, 1), 3, ProtectionMode::Reward, after) == OwnershipError::None && after == Count(1, 3));
    CHECK(PlanGrant(common, Count(0, 2), 1, ProtectionMode::Reward, after) == OwnershipError::None && after == Count(0, 3));
    CHECK(PlanGrant(common, Count(2, 0), 2, ProtectionMode::Reward, after) == OwnershipError::None && after == Count(2, 2));
    CHECK(PlanGrant(common, Count(0, 5), 1, ProtectionMode::Reward, after) == OwnershipError::None && after == Count(0, 6));
    CHECK(PlanGrant(legendary, Count(0, 0), 1, ProtectionMode::Reward, after) == OwnershipError::None && after == Count(1, 0));
    CHECK(PlanGrant(legendary, Count(0, 0), 2, ProtectionMode::Reward, after) == OwnershipError::None && after == Count(1, 1));
    CHECK(PlanGrant(legendary, Count(0, 1), 1, ProtectionMode::Reward, after) == OwnershipError::None && after == Count(0, 2));
    CHECK(PlanGrant(legendary, Count(1, 0), 1, ProtectionMode::Reward, after) == OwnershipError::None && after == Count(1, 1));

    // reward_only copies are always protected, whatever the mode
    for (ProtectionMode m : { ProtectionMode::Unprotected, ProtectionMode::Protected, ProtectionMode::Reward })
    {
        CHECK(PlanGrant(rewardOnly, Count(0, 0), 3, m, after) == OwnershipError::None && after == Count(3, 0));
        CHECK(PlanGrant(rewardOnlyLegendary, Count(1, 0), 2, m, after) == OwnershipError::None && after == Count(3, 0));
    }
}

static void TestPlanRemove()
{
    CardCount after = Count(42, 42);
    CHECK(PlanRemove(Count(2, 3), 1, false, after) == OwnershipError::None && after == Count(2, 2));
    CHECK(PlanRemove(Count(2, 3), 3, false, after) == OwnershipError::None && after == Count(2, 0));
    CHECK(PlanRemove(Count(2, 3), 4, false, after) == OwnershipError::None && after == Count(1, 0));
    CHECK(PlanRemove(Count(2, 3), 5, false, after) == OwnershipError::None && after == Count(0, 0));
    CHECK(PlanRemove(Count(2, 0), 1, false, after) == OwnershipError::None && after == Count(1, 0));
    after = Count(42, 42);
    CHECK(PlanRemove(Count(2, 3), 6, false, after) == OwnershipError::NotEnough);
    CHECK(PlanRemove(Count(0, 0), 1, false, after) == OwnershipError::NotEnough);
    CHECK(PlanRemove(Count(2, 3), 0, false, after) == OwnershipError::BadCount);
    CHECK(PlanRemove(Count(500, 500), 100, false, after) == OwnershipError::BadCount);
    CHECK(after == Count(42, 42)); // untouched on error
    CHECK(PlanRemove(Count(500, 499), 0, true, after) == OwnershipError::None && after == Count(0, 0));
    CHECK(PlanRemove(Count(0, 0), 0, true, after) == OwnershipError::NotEnough);
}

static void TestCollectionModelAndPayload()
{
    Collection c;
    CHECK(EncodeCollPayload(c) == "ver=0;n=0;cards=");
    CHECK(c.Summarize().distinct == 0 && c.Summarize().copies == 0);

    c.version = 7;
    c.Set("EX1_116", Count(0, 1));
    c.Set("CS2_022", Count(2, 1));
    c.Set("NEW1_016", Count(1, 0));
    c.Set("EX1_062", Count(1, 0));
    c.Set("NEW1_016", Count(0, 0)); // zero erases
    CHECK(c.cards.size() == 3 && c.Get("NEW1_016") == Count(0, 0));
    CHECK(c.Get("CS2_022") == Count(2, 1));
    std::string const payload = EncodeCollPayload(c);
    CHECK(payload == "ver=7;n=3;cards=CS2_022.3.2,EX1_062.1.1,EX1_116.1.0");
    Collection::Summary const s = c.Summarize();
    CHECK(s.distinct == 3 && s.copies == 5 && s.protectedCopies == 3);

    // byte order: upper case before lower case, digits before letters
    Collection o;
    o.Set("b", Count(0, 1));
    o.Set("B", Count(0, 1));
    o.Set("A1", Count(0, 1));
    o.Set("A_", Count(0, 1));
    CHECK(EncodeCollPayload(o) == "ver=0;n=4;cards=A1.1.0,A_.1.0,B.1.0,b.1.0");

    CHECK(EncodeCollChangedPayload(0) == "ver=0");
    CHECK(EncodeCollChangedPayload(4294967295u) == "ver=4294967295");

    // COLL_CHANGED: one frame, reqId 0
    std::vector<std::string> out;
    AppendCollChanged(out, 42);
    CHECK(out.size() == 1);
    CHECK(out.size() == 1 && out[0] == "v1\tCOLL_CHANGED\t0\t1\t1\tver=42");
    auto rep = Decode(out);
    CHECK(rep.size() == 1 && rep[0].op == "COLL_CHANGED" && rep[0].reqId == "0" && rep[0].payload == "ver=42");
    CHECK(std::string(ErrorCodeName(ErrorCode::Unavailable)) == "UNAVAILABLE");
}

static void TestSessionCollGet()
{
    FakeCollections fake;
    fake.collection.version = 3;
    fake.collection.Set("CS2_022", Count(1, 1));
    fake.collection.Set("EX1_062", Count(1, 0));

    Client c;
    c.collections = &fake;
    CHECK(FirstErr(c.Send1("COLL_GET", "c0", "")) == "NOT_READY");
    CHECK(fake.calls == 0);
    c.Hello();

    auto rep = Decode(c.Send1("COLL_GET", "c1", ""));
    CHECK(rep.size() == 1 && rep[0].op == "COLL" && rep[0].reqId == "c1");
    CHECK(rep[0].payload == "ver=3;n=2;cards=CS2_022.2.1,EX1_062.1.1");
    auto kv = Kv(rep[0]);
    CHECK(kv["ver"] == "3" && kv["n"] == "2" && kv["cards"] == "CS2_022.2.1,EX1_062.1.1");
    CHECK(fake.calls == 1);

    // The addon cannot assert ownership: a payload is ignored, the reply is the server's.
    rep = Decode(c.Send1("COLL_GET", "c2", "cards=CS2_022.99.99;n=1"));
    CHECK(rep.size() == 1 && rep[0].payload == "ver=3;n=2;cards=CS2_022.2.1,EX1_062.1.1");
    for (char const* op : { "COLL_SET", "COLL_GRANT", "COLL_ADD", "COLL" })
        CHECK(FirstErr(c.Send1(op, "c3", "cards=CS2_022.99.99")) == (std::string(op) == "COLL" ? "UNKNOWN_OP" : "NOT_IMPLEMENTED"));

    // empty collection
    fake.collection = Collection();
    rep = Decode(c.Send1("COLL_GET", "c4", ""));
    CHECK(rep.size() == 1 && rep[0].payload == "ver=0;n=0;cards=");

    // storage failure / no handler -> ERR UNAVAILABLE with the request id
    fake.fail = true;
    rep = Decode(c.Send1("COLL_GET", "c5", ""));
    CHECK(rep.size() == 1 && rep[0].reqId == "c5" && ErrCode(rep[0]) == "UNAVAILABLE");
    fake.fail = false;
    c.collections = nullptr;
    rep = Decode(c.Send1("COLL_GET", "c6", ""));
    CHECK(rep.size() == 1 && rep[0].reqId == "c6" && ErrCode(rep[0]) == "UNAVAILABLE");

    // disabled: silent, handler not consulted
    c.collections = &fake;
    int const calls = fake.calls;
    c.settings.enable = false;
    CHECK(c.Send1("COLL_GET", "c7", "").empty());
    CHECK(fake.calls == calls);
}

static void TestSessionCollLargeCollection()
{
    // Worst case: every card of a 615-card dataset at 999 copies (999 protected).
    FakeCollections fake;
    fake.collection.version = 4000000000u;
    for (int i = 0; i < 615; ++i)
    {
        char id[16];
        std::snprintf(id, sizeof(id), "NEW1_%03dt", i);
        fake.collection.Set(id, Count(999, 0));
    }
    Client c;
    c.collections = &fake;
    c.Hello();
    auto frames = c.Send1("COLL_GET", "big", "");
    CHECK(frames.size() > 1 && frames.size() <= kMaxPartsHardLimit);
    auto rep = Decode(frames);
    CHECK(rep.size() == 1 && rep[0].op == "COLL");
    CHECK(rep.size() == 1 && rep[0].payload == EncodeCollPayload(fake.collection));
    CHECK(rep.size() == 1 && rep[0].payload.size() < 16384); // the addon's reply bound
    auto kv = Kv(rep[0]);
    CHECK(kv["n"] == "615" && kv["ver"] == "4000000000");
}

static void TestCollectionService()
{
    FakeStorage storage;
    CollectionService svc(storage);
    Collection out;

    // no card data: everything is unavailable
    CHECK(!svc.Get(1, out));
    CHECK(svc.Grant(1, "CS2_022", 1, GrantSource::Gm, ProtectionMode::Unprotected, "r", "s").status == MutationStatus::Unavailable);
    CHECK(svc.Remove(1, "CS2_022", 1, false, "r", "s").status == MutationStatus::Unavailable);
    CHECK(storage.loads == 0 && storage.commits.empty());
    svc.SetCatalog(TestCatalog());
    CHECK(svc.Catalog() != nullptr);

    // online account: loaded once, then cached
    storage.db[1].version = 5;
    storage.db[1].Set("CS2_022", Count(1, 0));
    svc.SetOnline(1);
    CHECK(svc.IsOnline(1) && !svc.IsCached(1));
    CHECK(svc.Get(1, out) && out.version == 5 && out.Get("CS2_022") == Count(1, 0));
    CHECK(svc.Get(1, out) && storage.loads == 1 && svc.IsCached(1));

    // grant: commit carries the full mutation, cache and version follow
    MutationResult r = svc.Grant(1, "cs2_022", 3, GrantSource::Gm, ProtectionMode::Reward, "gm_grant", "gm:9");
    CHECK(r.status == MutationStatus::Ok && r.error == OwnershipError::None);
    CHECK(r.mutation.cardId == "CS2_022" && r.mutation.before == Count(1, 0) && r.mutation.after == Count(2, 2));
    CHECK(r.mutation.versionBefore == 5 && r.mutation.versionAfter == 6);
    CHECK(storage.commits.size() == 1);
    if (storage.commits.size() == 1)
    {
        Mutation const& m = storage.commits[0].second;
        CHECK(storage.commits[0].first == 1 && m.cardId == "CS2_022" && m.versionAfter == 6);
        CHECK(m.DeltaProtected() == 1 && m.DeltaUnprotected() == 2 && m.reason == "gm_grant" && m.source == "gm:9");
    }
    CHECK(svc.Get(1, out) && out.version == 6 && out.Get("CS2_022") == Count(2, 2) && storage.loads == 1);

    // rejections write nothing and keep the cache
    r = svc.Grant(1, "EX1_062", 1, GrantSource::Pack, ProtectionMode::Unprotected, "pack", "x");
    CHECK(r.status == MutationStatus::Rejected && r.error == OwnershipError::SourceNotAllowed);
    r = svc.Grant(1, "NOPE", 1, GrantSource::Gm, ProtectionMode::Unprotected, "gm_grant", "x");
    CHECK(r.status == MutationStatus::Rejected && r.error == OwnershipError::UnknownCard);
    r = svc.Grant(1, "CS2_022", 100, GrantSource::Gm, ProtectionMode::Unprotected, "gm_grant", "x");
    CHECK(r.status == MutationStatus::Rejected && r.error == OwnershipError::BadCount);
    r = svc.Grant(1, "NEW1_006", 1, GrantSource::Gm, ProtectionMode::Unprotected, "gm_grant", "x");
    CHECK(r.status == MutationStatus::Rejected && r.error == OwnershipError::NotObtainable);
    r = svc.Remove(1, "CS2_022", 5, false, "gm_remove", "x");
    CHECK(r.status == MutationStatus::Rejected && r.error == OwnershipError::NotEnough);
    r = svc.Remove(1, "NOPE", 1, false, "gm_remove", "x");
    CHECK(r.status == MutationStatus::Rejected && r.error == OwnershipError::UnknownCard);
    r = svc.Remove(1, "CS2_022", 0, false, "gm_remove", "x");
    CHECK(r.status == MutationStatus::Rejected && r.error == OwnershipError::BadCount);
    CHECK(storage.commits.size() == 1 && svc.IsCached(1));

    // overflow: 999 max per card
    for (int i = 0; i < 10; ++i)
        svc.Grant(1, "CS2_022", 99, GrantSource::Gm, ProtectionMode::Unprotected, "gm_grant", "x");
    CHECK(svc.Get(1, out) && out.Get("CS2_022").Total() == 994 && out.version == 16);
    r = svc.Grant(1, "CS2_022", 6, GrantSource::Gm, ProtectionMode::Unprotected, "gm_grant", "x");
    CHECK(r.status == MutationStatus::Rejected && r.error == OwnershipError::Overflow);
    r = svc.Grant(1, "CS2_022", 5, GrantSource::Gm, ProtectionMode::Unprotected, "gm_grant", "x");
    CHECK(r.status == MutationStatus::Ok && r.mutation.after.Total() == 999);

    // remove: unprotected first, then all
    r = svc.Remove(1, "CS2_022", 99, false, "gm_remove", "x");
    CHECK(r.status == MutationStatus::Ok && r.mutation.after == Count(2, 898));
    r = svc.Remove(1, "cs2_022", 0, true, "gm_remove", "x");
    CHECK(r.status == MutationStatus::Ok && r.mutation.after == Count(0, 0));
    CHECK(r.mutation.DeltaProtected() == -2 && r.mutation.DeltaUnprotected() == -898);
    CHECK(svc.Get(1, out) && out.cards.empty() && out.version == 19);
    CHECK(storage.db[1].cards.empty() && storage.db[1].version == 19);

    // reward_only through GM is protected
    r = svc.Grant(1, "EX1_062", 2, GrantSource::Gm, ProtectionMode::Unprotected, "gm_grant", "x");
    CHECK(r.status == MutationStatus::Ok && r.mutation.after == Count(2, 0));

    // failed commit: nothing changes, the cache entry is dropped and re-read next time
    std::size_t const commits = storage.commits.size();
    int const loads = storage.loads;
    storage.failCommit = true;
    r = svc.Grant(1, "CS2_022", 1, GrantSource::Gm, ProtectionMode::Unprotected, "gm_grant", "x");
    CHECK(r.status == MutationStatus::CommitFailed && r.mutation.versionAfter == 21);
    CHECK(!svc.IsCached(1) && storage.commits.size() == commits);
    storage.failCommit = false;
    CHECK(svc.Get(1, out) && storage.loads == loads + 1 && out.version == 20 && out.Get("CS2_022") == Count(0, 0));
    CHECK(out.Get("EX1_062") == Count(2, 0));

    // load failure: unavailable, nothing cached, nothing written
    svc.Evict(1);
    storage.failLoad = true;
    CHECK(!svc.Get(1, out));
    r = svc.Grant(1, "CS2_022", 1, GrantSource::Gm, ProtectionMode::Unprotected, "gm_grant", "x");
    CHECK(r.status == MutationStatus::Unavailable && storage.commits.size() == commits);
    CHECK(!svc.IsCached(1));
    storage.failLoad = false;

    // offline account: GM operations and reads leave no cache entry behind
    CHECK(!svc.IsOnline(2));
    r = svc.Grant(2, "EX1_116", 2, GrantSource::Gm, ProtectionMode::Reward, "gm_grant", "gm:console");
    CHECK(r.status == MutationStatus::Ok && r.mutation.after == Count(1, 1) && r.mutation.versionAfter == 1);
    CHECK(!svc.IsCached(2) && svc.CachedCount() == 0);
    CHECK(svc.Get(2, out) && out.Get("EX1_116") == Count(1, 1) && !svc.IsCached(2));
    r = svc.Remove(2, "EX1_116", 3, false, "gm_remove", "x");
    CHECK(r.status == MutationStatus::Rejected && !svc.IsCached(2));
    r = svc.Remove(2, "EX1_116", 1, false, "gm_remove", "x");
    CHECK(r.status == MutationStatus::Ok && r.mutation.after == Count(1, 0) && !svc.IsCached(2));

    // lifecycle: accounts are separate; logout evicts; login drops stale entries
    svc.SetOnline(2);
    CHECK(svc.Get(2, out) && svc.IsCached(2) && out.Get("EX1_062") == Count(0, 0));
    CHECK(svc.Get(1, out) && svc.IsCached(1));
    svc.SetOffline(2);
    CHECK(!svc.IsOnline(2) && !svc.IsCached(2) && svc.IsCached(1));
    storage.db[1].Set("CS2_022", Count(0, 7)); // changed behind the cache
    svc.SetOnline(1);
    CHECK(!svc.IsCached(1));
    CHECK(svc.Get(1, out) && out.Get("CS2_022") == Count(0, 7));
    svc.Evict(1);
    CHECK(svc.CachedCount() == 0 && svc.IsOnline(1));

    // catalog removed at runtime -> unavailable again
    svc.SetCatalog(nullptr);
    CHECK(!svc.Get(1, out));
}

// ---------------------------------------------------------------- packs
namespace
{
    // Every rarity bucket plus cards a pack must never contain.
    std::shared_ptr<CardCatalog const> PackCatalog()
    {
        auto c = std::make_shared<CardCatalog>();
        CHECK(c->Add("CS2_022", true, "standard", "FREE", "MAGE"));             // Basic: Common bucket
        CHECK(c->Add("CS1_042", true, "standard", "COMMON", "NEUTRAL"));
        CHECK(c->Add("EX1_015", true, "standard", "COMMON", "NEUTRAL"));
        CHECK(c->Add("EX1_012", true, "standard", "RARE", "NEUTRAL"));
        CHECK(c->Add("EX1_008", true, "standard", "RARE", "NEUTRAL"));
        CHECK(c->Add("EX1_005", true, "standard", "EPIC", "NEUTRAL"));
        CHECK(c->Add("EX1_116", true, "standard", "LEGENDARY", "NEUTRAL"));
        CHECK(c->Add("EX1_002", true, "standard", "LEGENDARY", "NEUTRAL"));
        CHECK(c->Add("EX1_062", true, "reward_only", "LEGENDARY", "NEUTRAL"));  // promo: never in packs
        CHECK(c->Add("NEW1_006", true, "unreleased", "EPIC", "ROGUE"));         // never in packs
        CHECK(c->Add("CS1_113e", false, "none", "COMMON", "NEUTRAL"));          // not collectible
        CHECK(c->Add("TST_nc", false, "standard", "COMMON", "NEUTRAL"));        // not collectible
        CHECK(c->Add("TST_norar", true, "standard", "", "NEUTRAL"));            // no rarity
        return c;
    }

    // Returns the scripted values (modulo bound), then 0 forever.
    struct ScriptRng
    {
        std::vector<uint32_t> values;
        std::size_t next = 0;
        PackRng Fn()
        {
            return [this](uint32_t bound)
            {
                uint32_t const v = next < values.size() ? values[next] : 0;
                ++next;
                return v % bound;
            };
        }
    };

    PackRng LcgRng(Lcg& lcg)
    {
        return [&lcg](uint32_t bound) { return lcg.Next() % bound; };
    }

    PackRng ZeroRng()
    {
        return [](uint32_t) { return 0u; };
    }

    struct FakePacks : FakeCollections
    {
        PackView view;
        bool failPacks = false;
        PackOpenResult open;
        std::vector<std::string> keys;

        bool GetPacks(PackView& out) override
        {
            if (failPacks)
                return false;
            out = view;
            return true;
        }

        PackOpenResult OpenPack(std::string const& key) override
        {
            keys.push_back(key);
            return open;
        }
    };

    PackOpening MakeOpening(uint32_t seq, std::string const& key, std::string const& cards, std::string const& mask,
                            uint32_t version)
    {
        PackOpening o;
        o.seq = seq;
        o.key = key;
        CHECK(o.SetCardList(cards, ','));
        CHECK(o.SetNewMask(mask));
        o.collectionVersion = version;
        return o;
    }
}

static void TestPackPool()
{
    PackPool const pool = PackPool::FromCatalog(*PackCatalog());
    CHECK(pool.Complete() && pool.Size() == 8);
    CHECK((pool.Cards(PackRarity::Common) == std::vector<std::string>{ "CS1_042", "CS2_022", "EX1_015" }));
    CHECK((pool.Cards(PackRarity::Rare) == std::vector<std::string>{ "EX1_008", "EX1_012" }));
    CHECK((pool.Cards(PackRarity::Epic) == std::vector<std::string>{ "EX1_005" }));
    CHECK((pool.Cards(PackRarity::Legendary) == std::vector<std::string>{ "EX1_002", "EX1_116" }));

    // The test catalog has no standard Rare or Epic: incomplete, cannot roll.
    PackPool const partial = PackPool::FromCatalog(*TestCatalog());
    CHECK(!partial.Complete());
    PackRoll roll;
    roll.cards[0] = "untouched";
    CHECK(!RollPack(partial, PackOdds(), 0, ZeroRng(), roll) && roll.cards[0] == "untouched");

    Rarity r = ParseRarity("FREE");
    CHECK(r == Rarity::Free && ParseRarity("LEGENDARY") == Rarity::Legendary && ParseRarity("legendary") == Rarity::None);
    PackRarity b;
    CHECK(PackRarityOf(Rarity::Free, b) && b == PackRarity::Common);
    CHECK(PackRarityOf(Rarity::Epic, b) && b == PackRarity::Epic);
    CHECK(!PackRarityOf(Rarity::None, b));
    CHECK(std::string(PackRarityName(PackRarity::Legendary)) == "Legendary");

    PackOdds odds;
    odds.weight = { { 0, 0, 0, 0 } };
    odds.legendaryPity = 5000;
    PackOdds const s = SanitizePackOdds(odds);
    CHECK(s.weight == PackOdds().weight && s.legendaryPity == 1000);
    odds.weight = { { 2000000, 0, 1, 0 } };
    CHECK(SanitizePackOdds(odds).weight[0] == 1000000 && SanitizePackOdds(odds).weight[2] == 1);
}

static void TestRollPackGuarantees()
{
    PackPool const pool = PackPool::FromCatalog(*PackCatalog());
    PackOdds const odds; // 716 / 228 / 44 / 11 of 999, pity 40
    PackRoll roll;

    // All five Common: the last slot becomes a Rare.
    CHECK(RollPack(pool, odds, 0, ZeroRng(), roll));
    CHECK(roll.cards[0] == "CS1_042" && roll.cards[3] == "CS1_042" && roll.cards[4] == "EX1_008");
    CHECK(roll.rarities[4] == PackRarity::Rare && roll.rareUpgraded && !roll.pityApplied && !roll.HasLegendary());
    CHECK(SinceLegendaryAfter(roll, 7) == 8);

    // Rarity thresholds and uniform picks: rarity value, then the card index.
    ScriptRng script;
    script.values = { 988, 1, 715, 2, 716, 1, 943, 0, 944, 0 };
    CHECK(RollPack(pool, odds, 0, script.Fn(), roll));
    CHECK(roll.cards[0] == "EX1_116" && roll.rarities[0] == PackRarity::Legendary);
    CHECK(roll.cards[1] == "EX1_015" && roll.rarities[1] == PackRarity::Common);
    CHECK(roll.cards[2] == "EX1_012" && roll.rarities[2] == PackRarity::Rare);
    CHECK(roll.cards[3] == "EX1_008" && roll.rarities[3] == PackRarity::Rare);
    CHECK(roll.cards[4] == "EX1_005" && roll.rarities[4] == PackRarity::Epic);
    CHECK(!roll.rareUpgraded && !roll.pityApplied && roll.HasLegendary());
    CHECK(SinceLegendaryAfter(roll, 39) == 0);

    // Bad-luck protection: the 40th pack in a row without a Legendary gets one in the
    // first lowest-rarity slot, which also satisfies the Rare guarantee.
    CHECK(RollPack(pool, odds, 39, ZeroRng(), roll));
    CHECK(roll.pityApplied && !roll.rareUpgraded && roll.cards[0] == "EX1_002" && roll.rarities[0] == PackRarity::Legendary);
    CHECK(roll.cards[1] == "CS1_042" && roll.cards[4] == "CS1_042");
    CHECK(SinceLegendaryAfter(roll, 39) == 0);
    CHECK(RollPack(pool, odds, 38, ZeroRng(), roll) && !roll.pityApplied && roll.rareUpgraded);
    CHECK(SinceLegendaryAfter(roll, 38) == 39);

    // The lowest slot is replaced, not an Epic or a Rare.
    script = ScriptRng();
    script.values = { 944, 0, 716, 0, 0, 2, 716, 1, 944, 0 };
    CHECK(RollPack(pool, odds, 100, script.Fn(), roll));
    CHECK(roll.pityApplied && roll.rarities[2] == PackRarity::Legendary && roll.rarities[0] == PackRarity::Epic);
    CHECK(roll.rarities[1] == PackRarity::Rare && roll.rarities[3] == PackRarity::Rare && roll.rarities[4] == PackRarity::Epic);

    // A rolled Legendary needs no protection.
    script = ScriptRng();
    script.values = { 0, 0, 998, 0 };
    CHECK(RollPack(pool, odds, 39, script.Fn(), roll) && !roll.pityApplied && roll.rarities[1] == PackRarity::Legendary);

    // pity 0 is off, pity 1 makes every pack contain a Legendary.
    PackOdds off = odds;
    off.legendaryPity = 0;
    CHECK(RollPack(pool, off, 100000, ZeroRng(), roll) && !roll.pityApplied && !roll.HasLegendary());
    PackOdds every = odds;
    every.legendaryPity = 1;
    CHECK(RollPack(pool, every, 0, ZeroRng(), roll) && roll.pityApplied && roll.HasLegendary());

    // Zero weights: only the remaining buckets roll.
    PackOdds epicOnly;
    epicOnly.weight = { { 0, 0, 5, 0 } };
    CHECK(RollPack(pool, epicOnly, 0, ZeroRng(), roll));
    for (std::size_t i = 0; i < kPackSize; ++i)
        CHECK(roll.rarities[i] == PackRarity::Epic && roll.cards[i] == "EX1_005");
}

static void TestPackStatistics()
{
    // A pool shaped like the real one: 227 common (incl. basic), 81 rare, 37 epic, 33 legendary.
    auto catalog = std::make_shared<CardCatalog>();
    char id[16];
    struct Bucket { char const* rarity; int n; };
    Bucket const buckets[] = { { "COMMON", 168 }, { "FREE", 59 }, { "RARE", 81 }, { "EPIC", 37 }, { "LEGENDARY", 33 } };
    int serial = 0;
    for (Bucket const& b : buckets)
        for (int i = 0; i < b.n; ++i)
        {
            std::snprintf(id, sizeof(id), "C%04d", serial++);
            catalog->Add(id, true, "standard", b.rarity, "NEUTRAL");
        }
    PackPool const pool = PackPool::FromCatalog(*catalog);
    CHECK(pool.Size() == 378 && pool.Cards(PackRarity::Common).size() == 227);

    Lcg lcg(12345);
    PackRng const rng = LcgRng(lcg);
    PackOdds const odds;
    uint32_t const packs = 200000;
    PackSimStats const s = SimulatePacks(pool, odds, packs, rng);
    CHECK(s.packs == packs && s.invalidPacks == 0);
    CHECK(s.longestWithoutLegendary <= 39);
    double const cards = packs * 5.0;
    double const common = s.cards[0] / cards, rare = s.cards[1] / cards, epic = s.cards[2] / cards,
        legendary = s.cards[3] / cards;
    // Rare guarantee on top of the estimates: P(5 commons) = 0.7167^5 = 18.9 % of packs upgrade a slot.
    CHECK(common > 0.672 && common < 0.686);
    CHECK(rare > 0.259 && rare < 0.273);
    CHECK(epic > 0.041 && epic < 0.047);
    CHECK(legendary > 0.0105 && legendary < 0.0135);
    CHECK(s.rareUpgraded > packs * 0.18 && s.rareUpgraded < packs * 0.20);
    CHECK(s.pityApplied > 0);

    // Uniform within a bucket: each Legendary within 25 % of the mean.
    Lcg lcg2(777);
    PackRng const rng2 = LcgRng(lcg2);
    std::map<std::string, uint32_t> seen;
    PackOdds legendaryOnly;
    legendaryOnly.weight = { { 0, 0, 0, 1 } };
    for (int p = 0; p < 20000; ++p)
    {
        PackRoll roll;
        CHECK(RollPack(pool, legendaryOnly, 0, rng2, roll));
        for (std::string const& c : roll.cards)
            ++seen[c];
    }
    CHECK(seen.size() == 33);
    double const mean = 20000.0 * 5 / 33;
    for (auto const& e : seen)
        CHECK(e.second > mean * 0.75 && e.second < mean * 1.25);
}

static void TestPackOpeningCodec()
{
    CHECK(IsValidPackKey("abcdef123456") && IsValidPackKey("0123456789abcdef"));
    CHECK(!IsValidPackKey("abcdef12345") && !IsValidPackKey("0123456789abcdefg") && !IsValidPackKey(""));
    CHECK(!IsValidPackKey("ABCDEF123456") && !IsValidPackKey("abcdef-23456") && !IsValidPackKey("abcdef 23456"));

    PackOpening o;
    CHECK(o.SetCardList("CS2_022,EX1_116,CS2_022,CS2_189,EX1_011", ','));
    CHECK(o.CardList(',') == "CS2_022,EX1_116,CS2_022,CS2_189,EX1_011" && o.CardList('.') == "CS2_022.EX1_116.CS2_022.CS2_189.EX1_011");
    CHECK(!o.SetCardList("A,B,C,D", ',') && !o.SetCardList("A,B,C,D,E,F", ',') && !o.SetCardList("A,B,,D,E", ','));
    CHECK(!o.SetCardList("A,B,C,D,E-1", ',') && !o.SetCardList("", ','));
    CHECK(o.cards[0] == "CS2_022"); // failed parses leave it untouched
    CHECK(o.SetNewMask("10010") && o.isNew[0] && !o.isNew[1] && o.isNew[3] && o.NewMask() == "10010");
    CHECK(!o.SetNewMask("1001") && !o.SetNewMask("100100") && !o.SetNewMask("10a10") && o.NewMask() == "10010");

    Collection before;
    before.Set("EX1_116", Count(0, 1));
    std::array<std::string, kPackSize> const cards = { { "CS2_022", "EX1_116", "CS2_022", "CS2_189", "CS2_189" } };
    std::array<bool, kPackSize> const flags = NewFlags(before, cards);
    CHECK(flags[0] && !flags[1] && !flags[2] && flags[3] && !flags[4]);

    PackState state;
    state.stock = 2;
    state.opened = 7;
    std::vector<PackOpening> recent;
    CHECK(EncodePacksPayload(state, recent) == "type=classic;stock=2;opened=7;recent=");
    recent.push_back(MakeOpening(7, "k7aaaaaaaaaa", "EX1_116,CS2_022,CS2_022,EX1_011,CS2_189", "10000", 9));
    recent.push_back(MakeOpening(6, "k6aaaaaaaaaa", "CS2_022,CS2_022,CS2_022,CS2_022,EX1_011", "10001", 8));
    CHECK(EncodePacksPayload(state, recent) == "type=classic;stock=2;opened=7;"
        "recent=7.10000.EX1_116.CS2_022.CS2_022.EX1_011.CS2_189,6.10001.CS2_022.CS2_022.CS2_022.CS2_022.EX1_011");
    for (uint32_t i = 5; i >= 1; --i)
        recent.push_back(MakeOpening(i, "kaaaaaaaaaa" + std::to_string(i), "A,B,C,D,E", "00000", i));
    std::map<std::string, std::string> kv;
    CHECK(KvDecode(EncodePacksPayload(state, recent), kv));
    CHECK(std::count(kv["recent"].begin(), kv["recent"].end(), ',') == 4); // at most five listed

    CHECK(EncodePackOpenedPayload(recent[0], false, 1) ==
        "id=7;key=k7aaaaaaaaaa;replay=0;stock=1;ver=9;new=10000;cards=EX1_116,CS2_022,CS2_022,EX1_011,CS2_189");
    CHECK(EncodePackOpenedPayload(recent[0], true, 0).find("replay=1;stock=0;") != std::string::npos);
    CHECK(EncodePackChangedPayload(5) == "stock=5");
}

static void TestPackService()
{
    FakeStorage storage;
    CollectionService svc(storage);
    PackView view;

    // no card data: everything is unavailable
    CHECK(!svc.GetPacks(1, view));
    CHECK(svc.GrantPacks(1, 1, "r", "s").status == PackStatus::Unavailable);
    CHECK(svc.OpenPack(1, "aaaaaaaaaaa1", "char:1").status == PackStatus::Unavailable);
    CHECK(svc.Pool() == nullptr);
    // a catalog without Rare/Epic cards cannot fill a pack
    svc.SetCatalog(TestCatalog());
    CHECK(svc.OpenPack(1, "aaaaaaaaaaa1", "char:1").status == PackStatus::Unavailable);
    CHECK(storage.openCommits.empty() && storage.packLoads == 0);

    svc.SetCatalog(PackCatalog());
    svc.SetRng(ZeroRng()); // every pack: CS1_042 x4 + EX1_008 (Rare guarantee)
    CHECK(svc.Pool() && svc.Pool()->Complete());
    svc.SetPackOdds(PackOdds());
    CHECK(svc.GetPackOdds().legendaryPity == 40);

    storage.db[1].version = 3;
    storage.db[1].Set("CS1_042", Count(1, 0));
    svc.SetOnline(1);

    // no stock
    PackOpenResult r = svc.OpenPack(1, "aaaaaaaaaaa1", "char:42");
    CHECK(r.status == PackStatus::NoStock && r.stockNow == 0 && storage.openCommits.empty());
    CHECK(svc.GetPacks(1, view) && view.state == PackState() && view.recent.empty());
    CHECK(svc.IsPackCached(1) && storage.packLoads == 1);

    // GM stock changes
    CHECK(svc.GrantPacks(1, 0, "gm_pack_grant", "gm:9").status == PackStatus::BadCount);
    CHECK(svc.GrantPacks(1, 101, "gm_pack_grant", "gm:9").status == PackStatus::BadCount);
    CHECK(svc.RemovePacks(1, 1, false, "gm_pack_remove", "gm:9").status == PackStatus::NotEnough);
    CHECK(svc.RemovePacks(1, 0, true, "gm_pack_remove", "gm:9").status == PackStatus::NotEnough);
    PackStockResult g = svc.GrantPacks(1, 10, "gm_pack_grant", "gm:9");
    CHECK(g.status == PackStatus::Ok && g.before.stock == 0 && g.after.stock == 10);
    CHECK(storage.stockCommits.size() == 1 && storage.stockCommits[0].second.reason == "gm_pack_grant" &&
          storage.stockCommits[0].second.source == "gm:9" && storage.stockCommits[0].second.after.stock == 10);
    CHECK(svc.GetPacks(1, view) && view.state.stock == 10 && storage.packLoads == 1);

    // a bad key changes nothing
    CHECK(svc.OpenPack(1, "SHORT", "char:42").status == PackStatus::BadKey);
    CHECK(svc.OpenPack(1, "AAAAAAAAAAA1", "char:42").status == PackStatus::BadKey);

    // open: one commit with stock, counter, opening and the card rows
    r = svc.OpenPack(1, "aaaaaaaaaaa1", "char:42");
    CHECK(r.status == PackStatus::Ok && r.stockNow == 9 && r.roll.rareUpgraded);
    CHECK(r.opening.seq == 1 && r.opening.key == "aaaaaaaaaaa1" && r.opening.collectionVersion == 4);
    CHECK(r.opening.CardList(',') == "CS1_042,CS1_042,CS1_042,CS1_042,EX1_008" && r.opening.NewMask() == "00001");
    CHECK(r.opening.stockAfter == 9 && r.opening.sinceLegendaryBefore == 0 && r.opening.sinceLegendaryAfter == 1);
    CHECK(storage.openCommits.size() == 1);
    if (storage.openCommits.size() == 1)
    {
        PackOpenCommit const& c = storage.openCommits[0].second;
        CHECK(c.before.stock == 10 && c.after.stock == 9 && c.after.opened == 1 && c.after.sinceLegendary == 1);
        CHECK(c.versionBefore == 3 && c.versionAfter == 4 && c.source == "char:42");
        CHECK(c.cards.size() == 2); // duplicates of one card are one mutation
        if (c.cards.size() == 2)
        {
            CHECK(c.cards[0].cardId == "CS1_042" && c.cards[0].before == Count(1, 0) && c.cards[0].after == Count(1, 4));
            CHECK(c.cards[1].cardId == "EX1_008" && c.cards[1].before == Count(0, 0) && c.cards[1].after == Count(0, 1));
            CHECK(c.cards[0].versionAfter == 4 && c.cards[1].versionAfter == 4 && c.cards[0].reason == "pack_open" &&
                  c.cards[0].source == "pack:classic:1");
        }
    }
    Collection coll;
    CHECK(svc.Get(1, coll) && coll.version == 4 && coll.Get("CS1_042") == Count(1, 4) && coll.Get("EX1_008") == Count(0, 1));
    CHECK(svc.GetPacks(1, view) && view.state.stock == 9 && view.state.opened == 1 && view.recent.size() == 1);
    CHECK(storage.loads == 1 && storage.packLoads == 1); // served from the cache

    // retry with the same key: the stored opening, nothing consumed or granted
    int const finds = storage.finds;
    r = svc.OpenPack(1, "aaaaaaaaaaa1", "char:42");
    CHECK(r.status == PackStatus::Replay && r.opening.seq == 1 && r.stockNow == 9 && storage.openCommits.size() == 1);
    CHECK(storage.finds == finds); // found among the cached recent openings
    CHECK(svc.Get(1, coll) && coll.version == 4);

    // six more packs push the first one out of the recent list
    for (int i = 2; i <= 7; ++i)
        CHECK(svc.OpenPack(1, "aaaaaaaaaaa" + std::to_string(i), "char:42").status == PackStatus::Ok);
    CHECK(svc.GetPacks(1, view) && view.state.stock == 3 && view.state.opened == 7 && view.recent.size() == 5);
    CHECK(view.recent[0].seq == 7 && view.recent[4].seq == 3);
    CHECK(svc.Get(1, coll) && coll.version == 10 && coll.Get("CS1_042") == Count(1, 28) && coll.Get("EX1_008") == Count(0, 7));

    // "restart": the caches are gone, a retry of the first key is still a replay (database lookup)
    svc.SetOffline(1);
    svc.SetOnline(1);
    CHECK(!svc.IsPackCached(1) && !svc.IsCached(1));
    int const findsBeforeRestart = storage.finds;
    r = svc.OpenPack(1, "aaaaaaaaaaa1", "char:42");
    CHECK(r.status == PackStatus::Replay && r.opening.seq == 1 && r.opening.CardList(',') == "CS1_042,CS1_042,CS1_042,CS1_042,EX1_008");
    CHECK(r.stockNow == 3 && storage.finds == findsBeforeRestart + 1 && storage.openCommits.size() == 7);

    // failed commit: nothing changed, caches dropped
    storage.failPackCommit = true;
    r = svc.OpenPack(1, "bbbbbbbbbbb1", "char:42");
    CHECK(r.status == PackStatus::CommitFailed && !svc.IsPackCached(1) && !svc.IsCached(1));
    CHECK(svc.GrantPacks(1, 1, "gm_pack_grant", "gm:9").status == PackStatus::CommitFailed);
    storage.failPackCommit = false;
    CHECK(svc.GetPacks(1, view) && view.state.stock == 3 && view.state.opened == 7);
    CHECK(svc.Get(1, coll) && coll.version == 10);

    // lost outcome: the opening committed but the store reported a failure. The
    // retry with the same key returns it instead of opening a second pack.
    storage.commitThenFail = true;
    r = svc.OpenPack(1, "ccccccccccc1", "char:42");
    CHECK(r.status == PackStatus::CommitFailed);
    storage.commitThenFail = false;
    r = svc.OpenPack(1, "ccccccccccc1", "char:42");
    CHECK(r.status == PackStatus::Replay && r.opening.seq == 8 && r.stockNow == 2 && storage.openCommits.size() == 8);
    CHECK(svc.Get(1, coll) && coll.version == 11);

    // a rolled card at the 999 copy limit refuses the whole opening
    storage.db[1].Set("CS1_042", Count(0, 999));
    svc.Evict(1);
    r = svc.OpenPack(1, "ddddddddddd1", "char:42");
    CHECK(r.status == PackStatus::Refused && r.error == OwnershipError::Overflow && r.card == "CS1_042");
    CHECK(svc.GetPacks(1, view) && view.state.stock == 2 && storage.openCommits.size() == 8);
    storage.db[1].Set("CS1_042", Count(1, 0));
    svc.Evict(1);

    // bad-luck protection through the service: counter persisted with the opening
    storage.packs[1].sinceLegendary = 39;
    svc.Evict(1);
    r = svc.OpenPack(1, "eeeeeeeeeee1", "char:42");
    CHECK(r.status == PackStatus::Ok && r.roll.pityApplied && r.opening.pityApplied && r.opening.cards[0] == "EX1_002");
    CHECK(r.opening.sinceLegendaryBefore == 39 && r.opening.sinceLegendaryAfter == 0 && storage.packs[1].sinceLegendary == 0);
    CHECK(r.opening.NewMask() == "10000");

    // remove
    PackStockResult rm = svc.RemovePacks(1, 0, true, "gm_pack_remove", "gm:9");
    CHECK(rm.status == PackStatus::Ok && rm.before.stock == 1 && rm.after.stock == 0 && storage.packs[1].stock == 0);
    CHECK(svc.OpenPack(1, "fffffffffff1", "char:42").status == PackStatus::NoStock);

    // overflow of the stock
    storage.packs[1].stock = kMaxPackStock - 5;
    svc.Evict(1);
    CHECK(svc.GrantPacks(1, 6, "gm_pack_grant", "gm:9").status == PackStatus::Overflow);
    CHECK(svc.GrantPacks(1, 5, "gm_pack_grant", "gm:9").status == PackStatus::Ok);

    // offline account: GM grants leave no cache entry behind
    CHECK(svc.GrantPacks(2, 3, "gm_pack_grant", "gm:9").status == PackStatus::Ok && !svc.IsPackCached(2));
    CHECK(svc.GetPacks(2, view) && view.state.stock == 3 && !svc.IsPackCached(2));

    // storage failures
    storage.failPackLoad = true;
    svc.Evict(1);
    CHECK(!svc.GetPacks(1, view));
    CHECK(svc.OpenPack(1, "ggggggggggg1", "char:42").status == PackStatus::Unavailable);
    storage.failPackLoad = false;
    storage.failFind = true;
    storage.packs[1].stock = 1;
    svc.Evict(1);
    CHECK(svc.OpenPack(1, "ggggggggggg1", "char:42").status == PackStatus::Unavailable);
    storage.failFind = false;
    CHECK(svc.OpenPack(1, "ggggggggggg1", "char:42").status == PackStatus::Ok);
}

static void TestSessionPacks()
{
    FakePacks fake;
    fake.view.state.stock = 2;
    fake.view.state.opened = 7;
    fake.view.recent.push_back(MakeOpening(7, "k7aaaaaaaaaa", "EX1_116,CS2_022,CS2_022,EX1_011,CS2_189", "10000", 9));

    Client c;
    c.collections = &fake;
    CHECK(FirstErr(c.Send1("PACK_GET", "k0", "")) == "NOT_READY");
    CHECK(FirstErr(c.Send1("PACK_OPEN", "o0", "key=abcdef123456")) == "NOT_READY");
    CHECK(fake.keys.empty());
    c.Hello();

    auto rep = Decode(c.Send1("PACK_GET", "k1", "stock=99"));
    CHECK(rep.size() == 1 && rep[0].op == "PACKS" && rep[0].reqId == "k1");
    CHECK(rep[0].payload == "type=classic;stock=2;opened=7;recent=7.10000.EX1_116.CS2_022.CS2_022.EX1_011.CS2_189");
    fake.failPacks = true;
    rep = Decode(c.Send1("PACK_GET", "k2", ""));
    CHECK(rep.size() == 1 && rep[0].reqId == "k2" && ErrCode(rep[0]) == "UNAVAILABLE");
    fake.failPacks = false;

    // key checks happen before the handler is asked
    for (char const* payload : { "", "key=", "key=short", "key=ABCDEF123456", "key=abcdef1234567890a", "cards=CS2_022" })
        CHECK(FirstErr(c.Send1("PACK_OPEN", "o1", payload)) == "MALFORMED");
    CHECK(fake.keys.empty());

    // opened: PACK_OPENED, then the COLL_CHANGED push
    fake.open.status = PackStatus::Ok;
    fake.open.opening = MakeOpening(8, "abcdef123456", "CS2_022,EX1_116,CS2_022,CS2_189,EX1_011", "11000", 9);
    fake.open.stockNow = 1;
    rep = Decode(c.Send1("PACK_OPEN", "o2", "key=abcdef123456"));
    CHECK(rep.size() == 2 && fake.keys.size() == 1 && fake.keys[0] == "abcdef123456");
    if (rep.size() == 2)
    {
        CHECK(rep[0].op == "PACK_OPENED" && rep[0].reqId == "o2");
        CHECK(rep[0].payload == "id=8;key=abcdef123456;replay=0;stock=1;ver=9;new=11000;cards=CS2_022,EX1_116,CS2_022,CS2_189,EX1_011");
        CHECK(rep[1].op == "COLL_CHANGED" && rep[1].reqId == "0" && rep[1].payload == "ver=9");
    }

    // replay: the same reply with replay=1, no push
    fake.open.status = PackStatus::Replay;
    rep = Decode(c.Send1("PACK_OPEN", "o3", "key=abcdef123456"));
    CHECK(rep.size() == 1 && rep[0].op == "PACK_OPENED" && Kv(rep[0])["replay"] == "1" && Kv(rep[0])["id"] == "8");

    struct Case { PackStatus status; char const* code; };
    Case const cases[] = {
        { PackStatus::NoStock, "NO_STOCK" }, { PackStatus::Refused, "REFUSED" },
        { PackStatus::Unavailable, "UNAVAILABLE" }, { PackStatus::CommitFailed, "UNAVAILABLE" },
        { PackStatus::BadKey, "MALFORMED" },
    };
    for (Case const& k : cases)
    {
        fake.open.status = k.status;
        rep = Decode(c.Send1("PACK_OPEN", "o4", "key=abcdef123456"));
        CHECK(rep.size() == 1 && rep[0].reqId == "o4" && ErrCode(rep[0]) == k.code);
    }

    // no handler: unavailable; other PACK_* stay reserved
    c.collections = nullptr;
    CHECK(FirstErr(c.Send1("PACK_GET", "k3", "")) == "UNAVAILABLE");
    CHECK(FirstErr(c.Send1("PACK_OPEN", "o5", "key=abcdef123456")) == "UNAVAILABLE");
    CHECK(FirstErr(c.Send1("PACK_BUY", "o6", "")) == "NOT_IMPLEMENTED");

    // push
    std::vector<std::string> push;
    AppendPackChanged(push, 5);
    rep = Decode(push);
    CHECK(push.size() == 1 && rep.size() == 1 && rep[0].op == "PACK_CHANGED" && rep[0].reqId == "0" && rep[0].payload == "stock=5");

    // the largest replies still fit the wire: 32 character ids everywhere
    std::string const longId(32, 'A');
    std::string const five = longId + "," + longId + "," + longId + "," + longId + "," + longId;
    c.collections = &fake;
    fake.view.recent.clear();
    for (uint32_t i = 5; i >= 1; --i)
        fake.view.recent.push_back(MakeOpening(i, "abcdefabcdef" + std::to_string(i), five, "11111", i));
    fake.view.state.stock = kMaxPackStock;
    fake.view.state.opened = 4000000000u;
    rep = Decode(c.Send1("PACK_GET", "k4", ""));
    CHECK(rep.size() == 1 && rep[0].op == "PACKS");
    fake.open.status = PackStatus::Ok;
    fake.open.opening = MakeOpening(4000000000u, "abcdef1234567890", five, "11111", 4000000000u);
    rep = Decode(c.Send1("PACK_OPEN", "o7", "key=abcdef1234567890"));
    CHECK(rep.size() == 2 && rep[0].op == "PACK_OPENED");
}

// ---------------------------------------------------------------- wallet and crafting

static void TestWalletRules()
{
    MatchRewards const r; // defaults: a coin item stacks to 250
    CHECK(MatchCoins(r, "win", "hero", "normal") == 3);
    CHECK(MatchCoins(r, "win", "hero", "hard") == 4);
    CHECK(MatchCoins(r, "win", "hero", "boss") == 8);
    CHECK(MatchCoins(r, "draw", "hero", "normal") == 3);   // a draw pays the win amount
    CHECK(MatchCoins(r, "draw", "turns", "hard") == 4);
    CHECK(MatchCoins(r, "loss", "hero", "boss") == 1);     // a loss pays the same against anyone
    CHECK(MatchCoins(r, "loss", "turns", "normal") == 1);
    CHECK(MatchCoins(r, "loss", "concede", "normal") == 0); // surrender
    CHECK(MatchCoins(r, "int", "stuck", "normal") == 0);    // interrupted
    CHECK(MatchCoins(r, "int", "logout", "hard") == 0);
    CHECK(MatchCoins(r, "-", "", "normal") == 0);
    MatchRewards custom;
    custom.loss = 1;
    custom.winNormal = 2;
    custom.winHard = 3;
    custom.winBoss = 4;
    CHECK(MatchCoins(custom, "win", "hero", "unknown") == 2);
    CHECK(MatchCoins(custom, "loss", "hero", "hard") == 1);
    custom.loss = 999999;
    CHECK(SanitizeMatchRewards(custom).loss == 1000);

    std::size_t bucket = 9;
    CHECK(DustBucket(Rarity::Free, bucket) && bucket == 0);
    CHECK(DustBucket(Rarity::Common, bucket) && bucket == 0);
    CHECK(DustBucket(Rarity::Rare, bucket) && bucket == 1);
    CHECK(DustBucket(Rarity::Epic, bucket) && bucket == 2);
    CHECK(DustBucket(Rarity::Legendary, bucket) && bucket == 3);
    CHECK(!DustBucket(Rarity::None, bucket));
    CHECK(std::string(DustBucketName(3)) == "LEGENDARY" && std::string(DustBucketName(0)) == "COMMON");

    std::shared_ptr<CardCatalog const> const cat = TestCatalog();
    CHECK(IsCraftable(*cat->Find("CS2_022")));      // Basic cards craft at Common values
    CHECK(IsCraftable(*cat->Find("EX1_116")));
    CHECK(!IsCraftable(*cat->Find("EX1_062")));     // reward_only promo
    CHECK(!IsCraftable(*cat->Find("NEW1_016")));
    CHECK(!IsCraftable(*cat->Find("NEW1_006")));    // unreleased
    CHECK(!IsCraftable(*cat->Find("CS1_113e")));    // not collectible
    CHECK(!IsCraftable(*cat->Find("TST_nc")));
    CHECK(!IsCraftable(*cat->Find("TST_none")));    // no rarity
    CHECK(CraftLimit(*cat->Find("EX1_116")) == 1 && CraftLimit(*cat->Find("CS2_022")) == 2);

    DustValues v;
    CHECK(v.scrap[0] == 5 && v.scrap[3] == 400 && v.craft[0] == 40 && v.craft[3] == 1600);
    v.scrap[1] = 0;
    v.craft[2] = 5000000;
    DustValues const s = SanitizeDustValues(v);
    CHECK(s.scrap[1] == 1 && s.craft[2] == 1000000 && s.scrap[0] == 5);
    v = DustValues();
    v.scrap[3] = 2000;   // more than crafting costs: lowered below the craft price
    v.craft[0] = 0;
    DustValues const t = SanitizeDustValues(v);
    CHECK(t.scrap[3] == 1599 && t.craft[0] == 2 && t.scrap[0] == 1);

    CHECK(IsValidCraftKey("abcdef123456") && IsValidCraftKey("abcdef1234567890"));
    CHECK(!IsValidCraftKey("abcdef12345") && !IsValidCraftKey("abcdef1234567890a") && !IsValidCraftKey("ABCDEF123456"));
    CHECK(!IsValidCraftKey("abcdef-23456") && !IsValidCraftKey(""));

    WalletView view;
    view.coins = 120;
    view.wallet.dust = 45;
    view.packPriceCoins = 10;
    CHECK(EncodeWalletPayload(view) ==
          "coins=120;dust=45;craft=0;scrap=5.20.100.400;make=40.100.400.1600;packcoins=10");
    view.craftOpen = true;
    view.craftNpc = "Ezra Chatterton";
    CHECK(EncodeWalletPayload(view) ==
          "coins=120;dust=45;craft=1;npc=Ezra Chatterton;scrap=5.20.100.400;make=40.100.400.1600;packcoins=10");
    Wallet w;
    w.dust = 45;
    CHECK(EncodeWalletChangedPayload(135, w, "match", 3) == "coins=135;dust=45;why=match;gain=3");
    CHECK(EncodeWalletChangedPayload(135, w, "gm", 0) == "coins=135;dust=45;why=gm");
    CraftRecord rec;
    rec.key = "abcdef123456";
    rec.kind = CraftKind::Make;
    rec.cardId = "EX1_116";
    rec.count = 1;
    rec.dustDelta = -1600;
    rec.dustAfter = 400;
    rec.collectionVersion = 5;
    rec.cardAfter = Count(0, 1);
    CHECK(EncodeCraftedPayload(rec, false) ==
          "key=abcdef123456;replay=0;kind=craft;card=EX1_116;n=1;delta=-1600;dust=400;ver=5;total=1;prot=0");
    rec.kind = CraftKind::Scrap;
    rec.count = 2;
    rec.dustDelta = 10;
    rec.cardAfter = Count(2, 0);
    CHECK(EncodeCraftedPayload(rec, true) ==
          "key=abcdef123456;replay=1;kind=scrap;card=EX1_116;n=2;delta=10;dust=400;ver=5;total=2;prot=2");
    CHECK(EncodeCraftAccessPayload(true, "Ibelin Redmoore") == "open=1;npc=Ibelin Redmoore");
    CHECK(EncodeCraftAccessPayload(false, "Ibelin Redmoore") == "open=0");
    for (CraftWhy why : { CraftWhy::Card, CraftWhy::Dust, CraftWhy::Limit, CraftWhy::Copies, CraftWhy::Count, CraftWhy::Npc,
                          CraftWhy::Nothing })
        CHECK(std::string(CraftWhyCode(why)) != "ok" && std::string(CraftWhyText(why)).size() > 10);
    CHECK(std::string(CraftWhyCode(CraftWhy::Nothing)) == "nothing" && std::string(CraftKindCode(CraftKind::Extras)) == "extras");
    rec.kind = CraftKind::Extras;
    rec.cardId = kExtrasCardId;
    rec.count = 12;
    rec.dustDelta = 240;
    rec.cardAfter = Count(0, 0);
    CHECK(EncodeCraftedPayload(rec, false) ==
          "key=abcdef123456;replay=0;kind=extras;card=all;n=12;delta=240;dust=400;ver=5;total=0;prot=0");
}

// CRAFT_EXTRAS: every extra copy of the collection in one commit.
static void TestScrapExtras()
{
    FakeStorage storage;
    CollectionService svc(storage);
    CHECK(svc.ScrapExtras(1, "aaaaaaaaaaa1", "s").status == CraftStatus::Unavailable); // no card data
    svc.SetCatalog(TestCatalog());
    svc.SetOnline(1);
    CHECK(svc.ScrapExtras(1, "SHORT", "s").status == CraftStatus::BadKey);

    // nothing above the playable copies: refused, nothing written
    storage.db[1].version = 3;
    storage.db[1].Set("CS2_022", Count(2, 0));   // 2 protected: within the limit
    storage.db[1].Set("EX1_116", Count(1, 0));   // the one Legendary copy
    storage.db[1].Set("EX1_062", Count(1, 3));   // reward_only: cannot be scrapped at all
    CraftResult r = svc.ScrapExtras(1, "aaaaaaaaaaa1", "char:42");
    CHECK(r.status == CraftStatus::Refused && r.why == CraftWhy::Nothing);
    CHECK(storage.extrasCommits.empty() && storage.craftOps[1].empty());

    // protected copies count as owned but are never scrapped: 3 protected + 0 unprotected has no extra
    storage.db[1].Set("CS2_022", Count(3, 0));
    svc.SetOffline(1);
    svc.SetOnline(1);
    r = svc.ScrapExtras(1, "aaaaaaaaaaa1", "char:42");
    CHECK(r.status == CraftStatus::Refused && r.why == CraftWhy::Nothing);

    storage.db[1].Set("CS2_022", Count(2, 4));   // 6 owned, 2 playable: 4 extra (5 dust each)
    storage.db[1].Set("EX1_116", Count(0, 3));   // 3 owned, 1 playable: 2 extra (400 dust each)
    svc.SetOffline(1);
    svc.SetOnline(1);
    CHECK(svc.ChangeDust(1, 100, "gm_dust", "gm:9").status == WalletStatus::Ok);
    r = svc.ScrapExtras(1, "bbbbbbbbbbb1", "char:42");
    CHECK(r.status == CraftStatus::Ok && r.record.kind == CraftKind::Extras && r.record.cardId == kExtrasCardId);
    CHECK(r.record.count == 6 && r.record.dustDelta == 4 * 5 + 2 * 400 && r.record.dustAfter == 100 + 820);
    CHECK(r.record.collectionVersion == 4 && r.wallet.dust == 920);
    CHECK(storage.extrasCommits.size() == 1);
    if (storage.extrasCommits.size() == 1)
    {
        CraftExtrasCommit const& c = storage.extrasCommits[0].second;
        CHECK(c.cards.size() == 2 && c.cards[0].cardId == "CS2_022" && c.cards[0].after == Count(2, 0));
        CHECK(c.cards.size() == 2 && c.cards[1].cardId == "EX1_116" && c.cards[1].after == Count(0, 1));
        CHECK(c.cards.size() == 2 && c.cards[0].reason == "scrap" && c.cards[0].source == "char:42");
        for (Mutation const& m : c.cards)
            CHECK(m.versionBefore == 3 && m.versionAfter == 4);
        CHECK(c.wallet.before.dust == 100 && c.wallet.after.dust == 920 && c.wallet.reason == "scrap");
    }
    Collection coll;
    CHECK(svc.Get(1, coll) && coll.version == 4 && coll.Get("CS2_022") == Count(2, 0) && coll.Get("EX1_116") == Count(0, 1));
    CHECK(coll.Get("EX1_062") == Count(1, 3));   // reward_only: untouched
    Wallet wallet;
    CHECK(svc.GetWallet(1, wallet) && wallet.dust == 920);

    // the same key again: the stored outcome, nothing changes
    r = svc.ScrapExtras(1, "bbbbbbbbbbb1", "char:42");
    CHECK(r.status == CraftStatus::Replay && r.record.count == 6 && r.record.dustAfter == 920 && r.record.kind == CraftKind::Extras);
    CHECK(storage.extrasCommits.size() == 1);
    // a new key finds nothing left to scrap
    r = svc.ScrapExtras(1, "bbbbbbbbbbb2", "char:42");
    CHECK(r.status == CraftStatus::Refused && r.why == CraftWhy::Nothing);

    // a failed commit drops the caches; the same key works afterwards
    storage.db[1].Set("CS2_022", Count(2, 1));
    svc.SetOffline(1);
    svc.SetOnline(1);
    storage.failCraftCommit = true;
    r = svc.ScrapExtras(1, "ccccccccccc1", "char:42");
    CHECK(r.status == CraftStatus::CommitFailed && !svc.IsCached(1) && !svc.IsWalletCached(1));
    storage.failCraftCommit = false;
    r = svc.ScrapExtras(1, "ccccccccccc1", "char:42");
    CHECK(r.status == CraftStatus::Ok && r.record.count == 1 && r.record.dustAfter == 925);

    // dust that would pass the maximum: refused with limit
    storage.db[1].Set("EX1_116", Count(0, 2));
    storage.wallets[1].dust = kMaxBalance - 10;
    svc.SetOffline(1);
    svc.SetOnline(1);
    r = svc.ScrapExtras(1, "ddddddddddd1", "char:42");
    CHECK(r.status == CraftStatus::Refused && r.why == CraftWhy::Limit);
}

static void TestWalletService()
{
    FakeStorage storage;
    CollectionService svc(storage);
    Wallet wallet;

    // no card data: everything is unavailable
    CHECK(!svc.GetWallet(1, wallet));
    CHECK(svc.ChangeDust(1, 5, "r", "s").status == WalletStatus::Unavailable);
    CHECK(svc.Craft(1, "aaaaaaaaaaa1", "EX1_116", "s").status == CraftStatus::Unavailable);
    CHECK(svc.Scrap(1, "aaaaaaaaaaa1", "EX1_116", 1, "s").status == CraftStatus::Unavailable);
    CHECK(storage.walletLoads == 0);

    svc.SetCatalog(TestCatalog());
    CHECK(svc.GetDustValues().craft[3] == 1600);
    DustValues zero;
    zero.scrap[0] = 0;
    svc.SetDustValues(zero);
    CHECK(svc.GetDustValues().scrap[0] == 1); // sanitized
    svc.SetDustValues(DustValues());

    storage.db[1].version = 3;
    storage.db[1].Set("CS2_022", Count(2, 1));
    svc.SetOnline(1);

    CHECK(svc.GetWallet(1, wallet) && wallet == Wallet() && svc.IsWalletCached(1) && storage.walletLoads == 1);

    // dust changes
    CHECK(svc.ChangeDust(1, 0, "gm_dust", "gm:9").status == WalletStatus::BadCount);
    WalletResult wr = svc.ChangeDust(1, -1, "gm_dust", "gm:9");
    CHECK(wr.status == WalletStatus::NotEnough && storage.walletCommits.empty());
    wr = svc.ChangeDust(1, 30, "gm_dust", "gm:9");
    CHECK(wr.status == WalletStatus::Ok && wr.before.dust == 0 && wr.after.dust == 30);
    CHECK(storage.walletCommits.size() == 1 && storage.walletCommits[0].second.reason == "gm_dust" &&
          storage.walletCommits[0].second.source == "gm:9" && storage.walletCommits[0].second.after.dust == 30);
    CHECK(svc.ChangeDust(1, kMaxBalance, "gm_dust", "gm:9").status == WalletStatus::Overflow);
    CHECK(svc.GetWallet(1, wallet) && wallet.dust == 30 && storage.walletLoads == 1);
    CHECK(svc.ChangeDust(1, -30, "gm_dust", "gm:9").status == WalletStatus::Ok);

    // crafting: refusals change nothing
    CraftResult cr = svc.Craft(1, "aaaaaaaaaaa1", "EX1_116", "char:42");
    CHECK(cr.status == CraftStatus::Refused && cr.why == CraftWhy::Dust && cr.needed == 1600);
    CHECK(svc.Craft(1, "SHORT", "EX1_116", "char:42").status == CraftStatus::BadKey);
    for (char const* id : { "EX1_062", "NEW1_016", "NEW1_006", "CS1_113e", "TST_none", "NOPE" })
    {
        cr = svc.Craft(1, "aaaaaaaaaaa1", id, "char:42");
        CHECK(cr.status == CraftStatus::Refused && cr.why == CraftWhy::Card);
    }
    cr = svc.Craft(1, "aaaaaaaaaaa1", "CS2_022", "char:42"); // already 3 copies, 2 playable
    CHECK(cr.status == CraftStatus::Refused && cr.why == CraftWhy::Limit);
    CHECK(storage.craftCommits.empty());

    // craft: one commit with the card, the version, the dust and the record
    CHECK(svc.ChangeDust(1, 2000, "gm_dust", "gm:9").status == WalletStatus::Ok);
    cr = svc.Craft(1, "aaaaaaaaaaa1", "ex1_116", "char:42"); // case-insensitive id
    CHECK(cr.status == CraftStatus::Ok && cr.record.cardId == "EX1_116" && cr.record.kind == CraftKind::Make);
    CHECK(cr.record.count == 1 && cr.record.dustDelta == -1600 && cr.record.dustAfter == 400 && cr.wallet.dust == 400);
    CHECK(cr.record.collectionVersion == 4 && cr.record.cardAfter == Count(0, 1) && cr.before == Count(0, 0));
    CHECK(storage.craftCommits.size() == 1);
    if (storage.craftCommits.size() == 1)
    {
        CraftCommit const& c = storage.craftCommits[0].second;
        CHECK(c.card.reason == "craft" && c.card.source == "char:42" && c.card.versionBefore == 3 && c.card.versionAfter == 4);
        CHECK(c.wallet.reason == "craft" && c.wallet.before.dust == 2000 && c.wallet.after.dust == 400);
    }
    Collection coll;
    CHECK(svc.Get(1, coll) && coll.version == 4 && coll.Get("EX1_116") == Count(0, 1));
    CHECK(svc.GetWallet(1, wallet) && wallet.dust == 400);

    // replay: the stored outcome, nothing new
    cr = svc.Craft(1, "aaaaaaaaaaa1", "EX1_116", "char:42");
    CHECK(cr.status == CraftStatus::Replay && cr.record.dustAfter == 400 && cr.record.collectionVersion == 4);
    CHECK(storage.craftCommits.size() == 1);
    // a replayed key wins over another card or a scrap
    CHECK(svc.Scrap(1, "aaaaaaaaaaa1", "CS2_022", 1, "char:42").status == CraftStatus::Replay);
    // the Legendary is at its limit now
    cr = svc.Craft(1, "aaaaaaaaaaa2", "EX1_116", "char:42");
    CHECK(cr.status == CraftStatus::Refused && cr.why == CraftWhy::Limit);

    // scrap: only unprotected copies
    cr = svc.Scrap(1, "bbbbbbbbbbb1", "CS2_022", 2, "char:42");
    CHECK(cr.status == CraftStatus::Refused && cr.why == CraftWhy::Copies);
    CHECK(svc.Scrap(1, "bbbbbbbbbbb1", "CS2_022", 0, "char:42").why == CraftWhy::Count);
    CHECK(svc.Scrap(1, "bbbbbbbbbbb1", "CS2_022", 100, "char:42").why == CraftWhy::Count);
    CHECK(svc.Scrap(1, "bbbbbbbbbbb1", "EX1_062", 1, "char:42").why == CraftWhy::Card);
    cr = svc.Scrap(1, "bbbbbbbbbbb1", "CS2_022", 1, "char:42");
    CHECK(cr.status == CraftStatus::Ok && cr.record.kind == CraftKind::Scrap && cr.record.dustDelta == 5 &&
          cr.record.dustAfter == 405 && cr.record.cardAfter == Count(2, 0) && cr.record.collectionVersion == 5);
    cr = svc.Scrap(1, "bbbbbbbbbbb2", "CS2_022", 1, "char:42"); // the protected starter copies stay
    CHECK(cr.status == CraftStatus::Refused && cr.why == CraftWhy::Copies);
    cr = svc.Scrap(1, "bbbbbbbbbbb3", "EX1_116", 1, "char:42");
    CHECK(cr.status == CraftStatus::Ok && cr.record.dustAfter == 805 && cr.record.cardAfter.Total() == 0);
    CHECK(svc.Get(1, coll) && coll.cards.count("EX1_116") == 0 && coll.version == 6);
    CHECK(storage.craftCommits.size() == 3 && storage.craftCommits[1].second.card.reason == "scrap");

    // a failed commit drops the caches; the same key works afterwards
    CHECK(svc.ChangeDust(1, 1000, "gm_dust", "gm:9").status == WalletStatus::Ok);
    storage.failCraftCommit = true;
    cr = svc.Craft(1, "ccccccccccc1", "EX1_116", "char:42");
    CHECK(cr.status == CraftStatus::CommitFailed && !svc.IsCached(1) && !svc.IsWalletCached(1));
    storage.failCraftCommit = false;
    cr = svc.Craft(1, "ccccccccccc1", "EX1_116", "char:42");
    CHECK(cr.status == CraftStatus::Ok && cr.record.dustAfter == 205);
    storage.failFindCraft = true;
    CHECK(svc.Craft(1, "ccccccccccc2", "CS2_022", "char:42").status == CraftStatus::Unavailable);
    storage.failFindCraft = false;
    storage.failWalletCommit = true;
    CHECK(svc.ChangeDust(1, 1, "r", "s").status == WalletStatus::CommitFailed && !svc.IsWalletCached(1));
    storage.failWalletCommit = false;
    storage.failWalletLoad = true;
    CHECK(!svc.GetWallet(1, wallet));
    CHECK(svc.Craft(1, "ccccccccccc3", "CS2_022", "char:42").status == CraftStatus::Unavailable);
    storage.failWalletLoad = false;

    // an offline account is never cached
    CHECK(svc.ChangeDust(2, 10, "gm", "gm:9").status == WalletStatus::Ok && !svc.IsWalletCached(2));
    CHECK(svc.GetWallet(2, wallet) && wallet.dust == 10 && !svc.IsWalletCached(2));
    svc.SetOffline(1);
    CHECK(!svc.IsWalletCached(1));
}

namespace
{
    struct FakeWallet : FakeCollections
    {
        WalletView view;
        bool failWallet = false;
        CraftResult craft;
        std::vector<std::string> calls;

        bool GetWallet(WalletView& out) override
        {
            if (failWallet)
                return false;
            out = view;
            return true;
        }

        CraftResult CraftCard(std::string const& key, std::string const& card) override
        {
            calls.push_back("make:" + key + ":" + card);
            return craft;
        }

        CraftResult ScrapCard(std::string const& key, std::string const& card, uint32_t count) override
        {
            calls.push_back("scrap:" + key + ":" + card + ":" + std::to_string(count));
            return craft;
        }

        CraftResult ScrapExtras(std::string const& key) override
        {
            calls.push_back("extras:" + key);
            return craft;
        }
    };
}

// WELCOME intro= (the addon's micro menu button) and the INTRO_DONE push.
static void TestSessionIntro()
{
    {
        Client c;
        auto rep = Decode(c.Send1("HELLO", "h1", "protocol=1;addon=test;dataset=-"));
        CHECK(rep.size() == 1 && rep[0].op == "WELCOME" && rep[0].payload.find("intro=") == std::string::npos);
    }
    for (char const* v : { "0", "1" })
    {
        Client c;
        c.info.intro = v;
        auto rep = Decode(c.Send1("HELLO", "h1", "protocol=1;addon=test;dataset=-"));
        CHECK(rep.size() == 1 && rep[0].op == "WELCOME");
        if (!rep.empty())
        {
            std::string const want = std::string(";intro=") + v;
            CHECK(rep[0].payload.size() > want.size() &&
                  rep[0].payload.compare(rep[0].payload.size() - want.size(), want.size(), want) == 0);
        }
    }
    std::vector<std::string> push;
    AppendIntroDone(push);
    auto rep = Decode(push);
    CHECK(push.size() == 1 && rep.size() == 1 && rep[0].op == "INTRO_DONE" && rep[0].reqId == "0" && rep[0].payload.empty());
}

// ---- the draft arena protocol --------------------------------------------------------------------------
namespace
{
    struct FakeArena : ArenaHandler
    {
        ArenaReply get;
        ArenaReply mutate;
        std::vector<ArenaRequest> requests;
        int gets = 0;
        std::string getReqId;
        ArenaReply Get(std::string const& reqId) override
        {
            ++gets;
            getReqId = reqId;
            return get;
        }
        ArenaReply Mutate(ArenaRequest const& request) override
        {
            requests.push_back(request);
            return mutate;
        }
    };

    struct FakeOrdinaryMatch : MatchHandler
    {
        std::string GameGet() override { return "ph=none"; }
        GameReply GameAct(GameActRequest const&) override { return GameReply(); }
        GameReply GameLeave(uint32_t) override { return GameReply(); }
    };

    struct FakePauseMatch : MatchHandler
    {
        GameReply pause;
        std::vector<uint32_t> pauses;
        std::string GameGet() override { return "ph=none"; }
        GameReply GameAct(GameActRequest const&) override { return GameReply(); }
        GameReply GameLeave(uint32_t) override { return GameReply(); }
        GameReply GamePause(uint32_t matchId) override
        {
            pauses.push_back(matchId);
            return pause;
        }
    };

    ArenaView SampleArenaView()
    {
        ArenaView v;
        v.state = "draft";
        v.runId = 12;
        v.enabled = true;
        v.pay = "free";
        v.deckClass = "MAGE";
        v.wins = 0;
        v.offerNo = 3;
        v.offer = { "CS2_022", "EX1_116", "CS2_168" };
        v.picked = 2;
        v.deck = { { "CS2_168", 1 }, { "EX1_116", 1 } };
        v.prizePacks = 0;
        v.prizeCoins = 0;
        v.basePacks = 3;
        v.bonusCoins = 10;
        return v;
    }

    char const* const kSampleArena =
        "st=draft;run=12;en=1;pay=free;cls=MAGE;w=0;d=0;l=0;duel=0;no=3;of=CS2_022,EX1_116,CS2_168;pk=2;"
        "dk=CS2_168.1,EX1_116.1;pz=0.0.0;tb=3.10;spc=0;sp=;sc=;live=0;mine=0;rp=0";
}

// Hero portraits: the service over a fake store, and HERO_GET / HERO_SELECT / HERO_UNLOCK through the session.
namespace
{
    struct FakeHeroStore : Heroes::HeroStore
    {
        Heroes::HeroState state;
        std::map<std::string, std::string> claims; // key -> portrait
        bool failLoad = false, failClaim = false, failSave = false;
        int claimCalls = 0;

        bool Load(uint32_t, Heroes::HeroState& out) override
        {
            if (failLoad)
                return false;
            out = state;
            return true;
        }
        bool FindClaim(uint32_t, std::string const& key, bool& found, std::string& portraitId) override
        {
            auto it = claims.find(key);
            found = it != claims.end();
            portraitId = found ? it->second : std::string();
            return true;
        }
        bool Claim(uint32_t, std::string const& key, Heroes::Portrait const& portrait) override
        {
            ++claimCalls;
            if (failClaim)
                return false;
            claims[key] = portrait.id;
            state.owned.insert(portrait.id);
            --state.credits;
            return true;
        }
        bool SaveChoice(uint32_t, std::string const& heroClass, Heroes::Portrait const* portrait) override
        {
            if (failSave)
                return false;
            if (portrait)
                state.selected[heroClass] = portrait->id;
            else
                state.selected.erase(heroClass);
            return true;
        }
    };

    struct FakeHeroes : FakeCollections
    {
        FakeHeroStore store;
        Heroes::HeroResult GetHeroes() override { return Heroes::GetHeroes(store, 1); }
        Heroes::HeroResult SelectHero(std::string const& c, std::string const& id) override
        {
            return Heroes::SelectPortrait(store, 1, c, id);
        }
        Heroes::HeroResult UnlockHero(std::string const& key, std::string const& id) override
        {
            return Heroes::UnlockPortrait(store, 1, key, id);
        }
    };
}

static void TestSessionHeroes()
{
    std::string mage, mage2, priest;
    for (Heroes::Portrait const& p : Heroes::kPortraits)
    {
        if (p.isDefault)
            continue;
        if (std::string(p.heroClass) == "MAGE")
            (mage.empty() ? mage : mage2) = p.id;
        if (std::string(p.heroClass) == "PRIEST" && priest.empty())
            priest = p.id;
    }

    FakeHeroes fake;
    fake.store.state.credits = 1;
    Client c;
    c.settings.ratePerSecond = 1000;
    c.settings.rateBurst = 1000;
    c.collections = &fake;
    CHECK(FirstErr(c.Send1("HERO_GET", "g0", "")) == "NOT_READY");
    CHECK(FirstErr(c.Send1("HERO_UNLOCK", "g0", "key=abcdef123456;id=" + mage)) == "NOT_READY");
    c.Hello();

    auto rep = Decode(c.Send1("HERO_GET", "g1", ""));
    CHECK(rep.size() == 1 && rep[0].op == "HEROES" && rep[0].reqId == "g1" && rep[0].payload == "credits=1;own=;sel=");

    // Selecting a locked portrait is refused, the default always works.
    auto err = Decode(c.Send1("HERO_SELECT", "s1", "class=MAGE;id=" + mage));
    CHECK(err.size() == 1 && err[0].op == "ERR" && err[0].payload.find("why=locked") != std::string::npos);
    rep = Decode(c.Send1("HERO_SELECT", "s2", "class=MAGE;id=default"));
    CHECK(rep.size() == 1 && rep[0].op == "HEROES" && rep[0].payload == "credits=1;own=;sel=");

    // Unlock: one credit pays one portrait, the same key again is a replay, a second key finds no credit.
    rep = Decode(c.Send1("HERO_UNLOCK", "u1", "key=abcdef123456;id=" + mage));
    CHECK(rep.size() == 1 && rep[0].op == "HEROES" && rep[0].payload == "credits=0;own=" + mage + ";sel=");
    rep = Decode(c.Send1("HERO_UNLOCK", "u2", "key=abcdef123456;id=" + mage));
    CHECK(rep.size() == 1 && rep[0].op == "HEROES" && rep[0].payload == "credits=0;own=" + mage + ";sel=");
    CHECK(fake.store.claimCalls == 1);
    err = Decode(c.Send1("HERO_UNLOCK", "u3", "key=abcdef654321;id=" + mage2));
    CHECK(err.size() == 1 && err[0].payload.find("why=nocredit") != std::string::npos);
    err = Decode(c.Send1("HERO_UNLOCK", "u4", "key=abcdef654322;id=" + mage));
    CHECK(err.size() == 1 && err[0].payload.find("why=owned") != std::string::npos);
    err = Decode(c.Send1("HERO_UNLOCK", "u5", "key=abcdef654323;id=NOPE"));
    CHECK(err.size() == 1 && err[0].payload.find("why=unknown") != std::string::npos);
    CHECK(fake.store.claimCalls == 1);

    // Select the unlocked one, then another class's portrait is refused.
    rep = Decode(c.Send1("HERO_SELECT", "s3", "class=MAGE;id=" + mage));
    CHECK(rep.size() == 1 && rep[0].payload == "credits=0;own=" + mage + ";sel=MAGE." + mage);
    err = Decode(c.Send1("HERO_SELECT", "s4", "class=MAGE;id=" + priest));
    CHECK(err.size() == 1 && err[0].payload.find("why=class") != std::string::npos);
    rep = Decode(c.Send1("HERO_SELECT", "s5", "class=MAGE;id=default"));
    CHECK(rep.size() == 1 && rep[0].payload == "credits=0;own=" + mage + ";sel=");

    // Malformed requests never reach the handler.
    for (char const* payload : { "", "class=MAGE", "id=default", "class=MAGE;id=" })
        CHECK(FirstErr(c.Send1("HERO_SELECT", "m1", payload)) == "MALFORMED");
    for (char const* payload : { "", "id=x", "key=abcdef123456", "key=SHORT;id=x", "key=ABCDEF123456;id=x", "key=abcdef123456;id=" })
        CHECK(FirstErr(c.Send1("HERO_UNLOCK", "m2", payload)) == "MALFORMED");

    // Failures answer UNAVAILABLE and change nothing.
    fake.store.failLoad = true;
    CHECK(FirstErr(c.Send1("HERO_GET", "f1", "")) == "UNAVAILABLE");
    fake.store.failLoad = false;
    fake.store.state.credits = 1;
    fake.store.failClaim = true;
    CHECK(FirstErr(c.Send1("HERO_UNLOCK", "f2", "key=abcdef999999;id=" + mage2)) == "UNAVAILABLE");
    CHECK(fake.store.state.credits == 1 && !fake.store.state.owned.count(mage2) && fake.store.claims.count("abcdef999999") == 0);
    fake.store.failClaim = false;
    fake.store.failSave = true;
    CHECK(FirstErr(c.Send1("HERO_SELECT", "f3", "class=MAGE;id=" + mage)) == "UNAVAILABLE");
    Client none;
    none.settings.ratePerSecond = 1000;
    none.settings.rateBurst = 1000;
    none.Hello();
    CHECK(FirstErr(none.Send1("HERO_GET", "n1", "")) == "UNAVAILABLE");

    // The capability follows ServerInfo::heroes.
    {
        Client w;
        auto hello = Decode(w.Send1("HELLO", "h1", "protocol=1;addon=test;dataset=-"));
        CHECK(hello.size() == 1 && hello[0].payload.find("heroes") == std::string::npos);
        Client h;
        h.info.heroes = true;
        hello = Decode(h.Send1("HELLO", "h1", "protocol=1;addon=test;dataset=-"));
        CHECK(hello.size() == 1 && hello[0].payload.find("caps=solo,chunk,packs,decks,wallet,match,heroes;") != std::string::npos);
    }

    // The push.
    std::vector<std::string> frames;
    AppendHeroChanged(frames, "legend1");
    auto push = Decode(frames);
    CHECK(push.size() == 1 && push[0].op == "HERO_CHANGED" && push[0].reqId == "0" && push[0].payload == "why=legend1");
}

static void TestArenaWire()
{
    CHECK(IsValidArenaKey("abcdef123456"));
    CHECK(IsValidArenaKey("abcdef1234567890"));
    CHECK(!IsValidArenaKey("abcdef12345"));
    CHECK(!IsValidArenaKey("abcdef12345678901"));
    CHECK(!IsValidArenaKey("ABCDEF123456"));
    CHECK(!IsValidArenaKey("abcdef12345_"));

    std::string const payload = EncodeArenaPayload(SampleArenaView(), false);
    CHECK(payload == kSampleArena);
    if (payload != kSampleArena)
        std::printf("    got: %s\n", payload.c_str());
    CHECK(EncodeArenaPayload(SampleArenaView(), true).size() == payload.size() &&
          EncodeArenaPayload(SampleArenaView(), true).find("rp=1") != std::string::npos);
    // No run: state none, empty offer (no= is empty, not 0: pick 0 is the class offer).
    std::string const none = EncodeArenaPayload(ArenaView(), false);
    CHECK(none.find("st=none;run=0;en=0;pay=;cls=;") == 0 && none.find(";no=;of=;") != std::string::npos);
    // The class offer is no=0.
    ArenaView cls;
    cls.state = "class";
    cls.offerNo = 0;
    cls.offer = { "MAGE", "DRUID", "ROGUE" };
    CHECK(EncodeArenaPayload(cls, false).find(";no=0;of=MAGE,DRUID,ROGUE;") != std::string::npos);

    ArenaRequest r;
    CHECK(DecodeArenaRequest(kOpArenaClass, "key=abcdef123456;class=MAGE", r));
    CHECK(r.op == "ARENA_CLASS" && r.key == "abcdef123456" && r.deckClass == "MAGE" && r.runId == 0);
    CHECK(DecodeArenaRequest(kOpArenaClass, "key=abcdef123456;class=MAGE;run=12", r) && r.runId == 12);
    CHECK(DecodeArenaRequest(kOpArenaPick, "key=abcdef123456;no=30;id=CS2_022", r));
    CHECK(r.pickNo == 30 && r.cardId == "CS2_022");
    CHECK(DecodeArenaRequest(kOpArenaClaim, "key=abcdef123456;id=EX1_062", r) && r.cardId == "EX1_062");
    // Malformed: missing or bad pieces, unknown and duplicate keys, wrong op.
    CHECK(!DecodeArenaRequest(kOpArenaClass, "class=MAGE", r));
    CHECK(!DecodeArenaRequest(kOpArenaClass, "key=short;class=MAGE", r));
    CHECK(!DecodeArenaRequest(kOpArenaClass, "key=abcdef123456", r));
    CHECK(!DecodeArenaRequest(kOpArenaClass, "key=abcdef123456;class=MA GE", r));
    CHECK(!DecodeArenaRequest(kOpArenaClass, "key=abcdef123456;class=MAGE;x=1", r));
    CHECK(!DecodeArenaRequest(kOpArenaClass, "key=abcdef123456;class=MAGE;run=x", r));
    CHECK(!DecodeArenaRequest(kOpArenaPick, "key=abcdef123456;no=0;id=CS2_022", r));
    CHECK(!DecodeArenaRequest(kOpArenaPick, "key=abcdef123456;no=31;id=CS2_022", r));
    CHECK(!DecodeArenaRequest(kOpArenaPick, "key=abcdef123456;id=CS2_022", r));
    CHECK(!DecodeArenaRequest(kOpArenaPick, "key=abcdef123456;no=1", r));
    CHECK(!DecodeArenaRequest(kOpArenaClaim, "key=abcdef123456;id=EX1_062;no=1", r));
    CHECK(!DecodeArenaRequest(kOpArenaClaim, "key=abcdef123456;key=abcdef123456;id=EX1_062", r));
    CHECK(!DecodeArenaRequest("ARENA_GET", "key=abcdef123456", r));
    // ARENA_DUEL / ARENA_RETIRE: the key and an optional run, nothing else.
    CHECK(DecodeArenaRequest(kOpArenaDuel, "key=abcdef123456", r) && r.op == "ARENA_DUEL" && r.runId == 0);
    CHECK(DecodeArenaRequest(kOpArenaRetire, "key=abcdef123456;run=7", r) && r.op == "ARENA_RETIRE" && r.runId == 7);
    CHECK(!DecodeArenaRequest(kOpArenaDuel, "", r));
    CHECK(!DecodeArenaRequest(kOpArenaDuel, "key=abcdef123456;id=EX1_062", r));
    CHECK(!DecodeArenaRequest(kOpArenaRetire, "key=short", r));
}

static void TestArenaSession()
{
    FakeArena fake;
    fake.get.status = ArenaReply::Status::Ok;
    fake.get.view = SampleArenaView();
    fake.mutate.status = ArenaReply::Status::Ok;
    fake.mutate.view = SampleArenaView();

    Client c;
    c.settings.ratePerSecond = 1000;
    c.settings.rateBurst = 1000;
    c.arena = &fake;
    CHECK(FirstErr(c.Send1("ARENA_GET", "a0", "")) == "NOT_READY");
    CHECK(FirstErr(c.Send1("GAME_PAUSE", "a0", "mid=1")) == "NOT_READY");

    // The capability follows ServerInfo::arena (and needs the match capability).
    {
        Client w;
        auto rep = Decode(w.Send1("HELLO", "h1", "protocol=1;addon=test;dataset=-"));
        CHECK(rep.size() == 1 && rep[0].payload.find("caps=solo,chunk,packs,decks,wallet,match;") != std::string::npos);
        Client a;
        a.info.arena = true;
        rep = Decode(a.Send1("HELLO", "h1", "protocol=1;addon=test;dataset=-"));
        CHECK(rep.size() == 1 && rep[0].payload.find("caps=solo,chunk,packs,decks,wallet,match,arena;") != std::string::npos);
        Client off;
        off.info.arena = true;
        off.settings.matchEnable = false;
        rep = Decode(off.Send1("HELLO", "h1", "protocol=1;addon=test;dataset=-"));
        CHECK(rep.size() == 1 && rep[0].payload.find("arena") == std::string::npos);
    }

    c.Hello();
    // ARENA_GET: the payload is ignored.
    auto rep = Decode(c.Send1("ARENA_GET", "g1", ""));
    CHECK(rep.size() == 1 && rep[0].op == "ARENA" && rep[0].reqId == "g1" && rep[0].payload == kSampleArena);
    rep = Decode(c.Send1("ARENA_GET", "g2", "anything=1"));
    CHECK(rep.size() == 1 && rep[0].op == "ARENA" && fake.gets == 2 && fake.getReqId == "g2");
    // Deferred (the game glue): nothing now, and the handler has the reqId its later answer must carry.
    fake.get.status = ArenaReply::Status::Deferred;
    rep = Decode(c.Send1("ARENA_GET", "g3", ""));
    CHECK(rep.empty() && fake.gets == 3 && fake.getReqId == "g3");
    fake.get.status = ArenaReply::Status::Ok;

    // ARENA_PICK: applied, replayed, refused with why=.
    rep = Decode(c.Send1("ARENA_PICK", "p1", "key=abcdef123456;no=3;id=CS2_022"));
    CHECK(rep.size() == 1 && rep[0].op == "ARENA" && rep[0].payload.find(";rp=0") != std::string::npos);
    CHECK(fake.requests.size() == 1 && fake.requests[0].op == "ARENA_PICK" && fake.requests[0].reqId == "p1" &&
          fake.requests[0].pickNo == 3 && fake.requests[0].cardId == "CS2_022");
    fake.mutate.status = ArenaReply::Status::Replay;
    rep = Decode(c.Send1("ARENA_PICK", "p2", "key=abcdef123456;no=3;id=CS2_022"));
    CHECK(rep.size() == 1 && rep[0].op == "ARENA" && rep[0].payload.find(";rp=1") != std::string::npos);
    fake.mutate.status = ArenaReply::Status::Refused;
    fake.mutate.why = "picked";
    fake.mutate.msg = "That pick was made already.";
    rep = Decode(c.Send1("ARENA_PICK", "p3", "key=abcdef123457;no=3;id=EX1_116"));
    CHECK(rep.size() == 1 && ErrCode(rep[0]) == "REFUSED");
    {
        std::map<std::string, std::string> kv;
        CHECK(KvDecode(rep[0].payload, kv) && kv["why"] == "picked" && kv["msg"] == "That pick was made already.");
    }
    fake.mutate.status = ArenaReply::Status::Unavailable;
    fake.mutate.msg = "The arena could not save right now. Try again.";
    CHECK(FirstErr(c.Send1("ARENA_CLASS", "c1", "key=abcdef123458;class=MAGE")) == "UNAVAILABLE");
    // Deferred: the session appends nothing, the caller answers later with AppendArenaReply.
    fake.mutate.status = ArenaReply::Status::Deferred;
    CHECK(c.Send1("ARENA_CLAIM", "k1", "key=abcdef123459;id=EX1_062").empty());
    CHECK(fake.requests.back().op == "ARENA_CLAIM" && fake.requests.back().cardId == "EX1_062");
    {
        std::vector<std::string> out;
        ArenaReply later;
        later.status = ArenaReply::Status::Ok;
        later.view = SampleArenaView();
        AppendArenaReply(out, "k1", later);
        auto d = Decode(out);
        CHECK(d.size() == 1 && d[0].op == "ARENA" && d[0].reqId == "k1");
        out.clear();
        later.status = ArenaReply::Status::Deferred;
        AppendArenaReply(out, "k1", later);
        CHECK(out.empty());
    }

    // Malformed requests never reach the handler.
    size_t const calls = fake.requests.size();
    CHECK(FirstErr(c.Send1("ARENA_PICK", "m1", "key=abcdef123456;no=0;id=CS2_022")) == "MALFORMED");
    CHECK(FirstErr(c.Send1("ARENA_CLASS", "m2", "class=MAGE")) == "MALFORMED");
    CHECK(FirstErr(c.Send1("ARENA_CLAIM", "m3", "key=BADKEY;id=EX1_062")) == "MALFORMED");
    CHECK(FirstErr(c.Send1("ARENA_DUEL", "m5", "")) == "MALFORMED");
    CHECK(FirstErr(c.Send1("ARENA_RETIRE", "m6", "key=abcdef123456;x=1")) == "MALFORMED");
    CHECK(fake.requests.size() == calls);
    // ARENA_DUEL / ARENA_RETIRE reach the handler like the other steps.
    CHECK(c.Send1("ARENA_DUEL", "d1", "key=abcdef123460").empty());
    CHECK(fake.requests.back().op == "ARENA_DUEL" && fake.requests.back().reqId == "d1");
    CHECK(c.Send1("ARENA_RETIRE", "r1", "key=abcdef123461;run=4").empty());
    CHECK(fake.requests.back().op == "ARENA_RETIRE" && fake.requests.back().runId == 4);
    // Unknown ARENA_ ops are reserved.
    CHECK(FirstErr(c.Send1("ARENA_FOO", "m4", "")) == "NOT_IMPLEMENTED");

    // Without a handler the arena is unavailable.
    {
        Client n;
        n.Hello();
        CHECK(FirstErr(n.Send1("ARENA_GET", "n1", "")) == "UNAVAILABLE");
        CHECK(FirstErr(n.Send1("ARENA_PICK", "n2", "key=abcdef123456;no=1;id=CS2_022")) == "UNAVAILABLE");
    }

    // The pushes: reqId 0, a single ARENA / ARENA_UPD / ARENA_OPEN payload.
    std::vector<std::string> push;
    AppendArenaUpdate(push, SampleArenaView());
    auto up = Decode(push);
    CHECK(up.size() == 1 && up[0].op == "ARENA_UPD" && up[0].reqId == "0" && up[0].payload == kSampleArena);
    push.clear();
    AppendArenaOpen(push, SampleArenaView());
    up = Decode(push);
    CHECK(up.size() == 1 && up[0].op == "ARENA_OPEN" && up[0].reqId == "0" && up[0].payload == kSampleArena);
    push.clear();
    AppendArenaClose(push);
    up = Decode(push);
    CHECK(up.size() == 1 && up[0].op == "ARENA_CLOSE" && up[0].reqId == "0" && up[0].payload.empty());
}

static void TestArenaWorstCase()
{
    // Thirty distinct drafted cards with long ids, a full offer, four special options and a long class name.
    ArenaView v = SampleArenaView();
    v.state = "settled";
    v.specialPending = true;
    v.claimed = "NEW1_016";
    v.live = v.mine = true;
    v.picked = 30;
    v.deck.clear();
    for (int i = 0; i < 30; ++i)
        v.deck.push_back({ "EX1_" + std::to_string(100 + i) + "_ABCDEFGHIJ", 2 });
    v.specials = { { "EX1_062", 1 }, { "NEW1_016", 2 }, { "EX1_112", 1 }, { "PRO_001", 1 } };
    std::vector<std::string> frames;
    AppendArenaUpdate(frames, v);
    CHECK(!frames.empty() && frames.size() < 20);
    auto rep = Decode(frames);
    CHECK(rep.size() == 1 && rep[0].op == "ARENA_UPD");
    std::map<std::string, std::string> kv;
    CHECK(KvDecode(rep[0].payload, kv) && kv["sp"] == "EX1_062.1,NEW1_016.2,EX1_112.1,PRO_001.1" && kv["spc"] == "1");
}

static void TestSessionLadder()
{
    struct DeferredLadder : LadderHandler
    {
        std::vector<Completed> requests;
        void Defer(Completed const& req) override { requests.push_back(req); }
    } ladder;
    Client c;
    auto send = [&](std::string const& op, std::string const& id, std::string const& payload, bool available = true)
    {
        std::string frame;
        CHECK(EncodeFrame(op, id, 1, 1, payload, frame));
        std::vector<std::string> out;
        c.session.OnFrame(c.now, frame, c.security, c.settings, c.info, out, nullptr, nullptr, nullptr,
            available ? &ladder : nullptr);
        return Decode(out);
    };
    auto rep = send("LADDER_GET", "l1", "");
    CHECK(rep.size() == 1 && rep[0].op == "ERR" && ladder.requests.empty());
    c.Hello();
    CHECK(!c.session.LadderUi());
    rep = Decode(c.Send1("HELLO", "h2", "protocol=1;addon=test;ladderui=1"));
    CHECK(rep.size() == 1 && c.session.LadderUi());
    for (auto const* op : {"LADDER_GET", "LADDER_PLAY", "LADDER_RES", "LADDER_GIVE"})
    {
        std::string const id = "l" + std::to_string(ladder.requests.size() + 2);
        CHECK(send(op, id, "npc=123;deck=4").empty());
        CHECK(ladder.requests.back().op == op && ladder.requests.back().reqId == id &&
            ladder.requests.back().payload == "npc=123;deck=4");
    }
    rep = send("LADDER_PLAY", "l6", "npc=123;deck=4", false);
    CHECK(rep.size() == 1 && rep[0].op == "ERR" && ladder.requests.size() == 4);
    c.settings.enable = false;
    CHECK(send("LADDER_PLAY", "l7", "npc=123;deck=4").empty() && ladder.requests.size() == 4);
}

static void TestSessionGamePause()
{
    FakePauseMatch pm;
    Client c;
    c.settings.ratePerSecond = 1000;
    c.settings.rateBurst = 1000;
    c.Hello();
    // An ordinary match handler (the default GamePause) refuses.
    FakeOrdinaryMatch ordinary;
    auto rep = Decode(c.SendGame(ordinary, "GAME_PAUSE", "z1", "mid=7"));
    CHECK(rep.size() == 1 && ErrCode(rep[0]) == "REFUSED");
    {
        std::map<std::string, std::string> kv;
        CHECK(KvDecode(rep[0].payload, kv) && kv["why"] == "ordinary");
    }
    // An arena duel is paused: GAME ph=none, same reqId.
    pm.pause.ok = true;
    pm.pause.payload = "ph=none";
    rep = Decode(c.SendGame(pm, "GAME_PAUSE", "z2", "mid=7"));
    CHECK(rep.size() == 1 && rep[0].op == "GAME" && rep[0].reqId == "z2" && rep[0].payload == "ph=none");
    CHECK(pm.pauses.size() == 1 && pm.pauses[0] == 7);
    pm.pause = GameReply();
    pm.pause.why = "match";
    rep = Decode(c.SendGame(pm, "GAME_PAUSE", "z3", "mid=8"));
    CHECK(rep.size() == 1 && ErrCode(rep[0]) == "REFUSED");
    // Malformed.
    CHECK(FirstErr(c.SendGame(pm, "GAME_PAUSE", "z4", "")) == "MALFORMED");
    CHECK(FirstErr(c.SendGame(pm, "GAME_PAUSE", "z5", "mid=x")) == "MALFORMED");
    CHECK(FirstErr(c.SendGame(pm, "GAME_PAUSE", "z6", "mid=7;x=1")) == "MALFORMED");
    CHECK(pm.pauses.size() == 2);
    // Matches switched off: no handler.
    CHECK(FirstErr(c.Send1("GAME_PAUSE", "z7", "mid=7")) == "UNAVAILABLE");
}

static void TestArenaIntroRules()
{
    // The gate: all nine classes AND at least 120 different cards.
    static_assert(kArenaIntroMinCards == 120 && kArenaIntroClasses == 9, "owner decision 2026-10-10");
    CHECK(ArenaIntroGate(9, 120));
    CHECK(ArenaIntroGate(9, 382));
    CHECK(!ArenaIntroGate(9, 119));
    CHECK(!ArenaIntroGate(8, 382));
    CHECK(!ArenaIntroGate(0, 0));
    // The hosts and their quests.
    CHECK(ArenaIntroQuestOfHost(919501) == kArenaIntroQuestHorde && kArenaIntroQuestHorde == 919921);
    CHECK(ArenaIntroQuestOfHost(919502) == kArenaIntroQuestAlliance && kArenaIntroQuestAlliance == 919931);
    CHECK(ArenaIntroQuestOfHost(919505) == 0 && ArenaIntroQuestOfHost(0) == 0);
    CHECK(IsArenaIntroQuest(919921) && IsArenaIntroQuest(919931) && !IsArenaIntroQuest(919704));

    ArenaIntroRunFacts none;
    ArenaIntroRunFacts freeOpen;
    freeOpen.exists = freeOpen.free = freeOpen.open = true;
    ArenaIntroRunFacts freeEnded;
    freeEnded.exists = freeEnded.free = freeEnded.ended = true;
    ArenaIntroRunFacts freeEndedSpecial = freeEnded;
    freeEndedSpecial.open = true; // settled with a special card choice pending: the run is over for the quest
    ArenaIntroRunFacts paidOpen;
    paidOpen.exists = paidOpen.open = true;
    ArenaIntroRunFacts paidEnded;
    paidEnded.exists = paidEnded.ended = true;
    // One free run: allowed without a run, never while a run is open, never after a free run existed.
    CHECK(ArenaIntroFreeRunAllowed(none));
    CHECK(!ArenaIntroFreeRunAllowed(freeOpen));
    CHECK(!ArenaIntroFreeRunAllowed(freeEnded));
    CHECK(!ArenaIntroFreeRunAllowed(paidOpen));
    CHECK(ArenaIntroFreeRunAllowed(paidEnded));
    // The quest completes when the free run ended, whatever its result; a paid run never completes it.
    CHECK(!ArenaIntroShouldComplete(none));
    CHECK(!ArenaIntroShouldComplete(freeOpen));
    CHECK(ArenaIntroShouldComplete(freeEnded));
    CHECK(ArenaIntroShouldComplete(freeEndedSpecial));
    CHECK(!ArenaIntroShouldComplete(paidEnded));
    CHECK(!ArenaIntroShouldComplete(paidOpen));
}

static void TestNaxxQuestRules()
{
    static_assert(sizeof(kNaxxQuests) / sizeof(kNaxxQuests[0]) == 29 && sizeof(kNaxxBosses) / sizeof(kNaxxBosses[0]) == 15,
        "the adventure has 15 bosses and 29 quests");
    // Quest lookups by id: every quest is found, the breadcrumb and neighbours are not.
    for (NaxxQuest const& q : kNaxxQuests)
    {
        NaxxQuest const* found = NaxxQuestById(q.questId);
        CHECK(found == &q);
    }
    CHECK(NaxxQuestById(0) == nullptr);
    CHECK(NaxxQuestById(kNaxxBreadcrumbQuest) == nullptr);
    CHECK(NaxxQuestById(919962) == nullptr);
    CHECK(NaxxQuestById(919933) && NaxxQuestById(919933)->kind == NaxxKind::Boss);
    CHECK(NaxxQuestById(919936) && NaxxQuestById(919936)->kind == NaxxKind::Wing);
    CHECK(NaxxQuestById(919937) && NaxxQuestById(919937)->kind == NaxxKind::Challenge);

    // All 15 bosses: entry <-> encounter key, and the Boss quest of the stand-in carries the same key.
    for (NaxxBoss const& boss : kNaxxBosses)
    {
        CHECK(NaxxBossByEntry(boss.entry) == &boss);
        CHECK(NaxxBossByEncounter(boss.encounterKey) == &boss);
        NaxxQuest const* byKey = NaxxQuestByKey(boss.encounterKey);
        CHECK(byKey != nullptr && byKey->kind == NaxxKind::Boss);
        CHECK(NaxxBossQuestOfEntry(boss.entry) == byKey);
        CHECK(byKey && std::string(byKey->key) == boss.encounterKey);
    }
    CHECK(NaxxBossByEntry(920000) && std::string(NaxxBossByEntry(920000)->encounterKey) == "naxx_anubrekhan");
    CHECK(NaxxBossByEntry(920014) && std::string(NaxxBossByEntry(920014)->encounterKey) == "naxx_kelthuzad");
    CHECK(NaxxBossQuestOfEntry(920000) && NaxxBossQuestOfEntry(920000)->questId == 919933);
    CHECK(NaxxBossQuestOfEntry(920014) && NaxxBossQuestOfEntry(920014)->questId == 919959);
    CHECK(NaxxBossQuestOfEntry(920008) && NaxxBossQuestOfEntry(920008)->questId == 919947); // the Horsemen
    // Nothing else is a stand-in.
    CHECK(NaxxBossByEntry(0) == nullptr && NaxxBossByEntry(919999) == nullptr && NaxxBossByEntry(920015) == nullptr);
    CHECK(NaxxBossByEntry(16112) == nullptr && NaxxBossQuestOfEntry(16112) == nullptr);
    CHECK(NaxxBossByEncounter("") == nullptr && NaxxBossByEncounter("naxx_challenge_druid") == nullptr);
    CHECK(NaxxBossByEncounter("naxx_nobody") == nullptr);

    // Challenge keys: nine quests, keys found, wing quests (key "") and the empty key are not.
    char const* const challengeKeys[9] = { "naxx_challenge_druid", "naxx_challenge_rogue", "naxx_challenge_mage",
        "naxx_challenge_hunter", "naxx_challenge_shaman", "naxx_challenge_warlock", "naxx_challenge_warrior",
        "naxx_challenge_priest", "naxx_challenge_paladin" };
    for (char const* key : challengeKeys)
    {
        NaxxQuest const* q = NaxxQuestByKey(key);
        CHECK(q != nullptr && q->kind == NaxxKind::Challenge);
    }
    CHECK(NaxxQuestByKey("naxx_challenge_druid") && NaxxQuestByKey("naxx_challenge_druid")->questId == 919937);
    CHECK(NaxxQuestByKey("naxx_challenge_paladin") && NaxxQuestByKey("naxx_challenge_paladin")->questId == 919961);
    CHECK(NaxxQuestByKey("") == nullptr);
    CHECK(NaxxQuestByKey("naxx_nobody") == nullptr);
    CHECK(NaxxQuestByKey("NAXX_ANUBREKHAN") == nullptr);
    int bosses = 0, wings = 0, challenges = 0;
    for (NaxxQuest const& q : kNaxxQuests)
    {
        bosses += q.kind == NaxxKind::Boss;
        wings += q.kind == NaxxKind::Wing;
        challenges += q.kind == NaxxKind::Challenge;
    }
    CHECK(bosses == 15 && wings == 5 && challenges == 9);

    // Challenges at a stand-in: the mapping is passed in (it lives in HsEncounters).
    std::map<std::string, std::string> const encounterOf = {
        { "naxx_challenge_druid", "naxx_faerlina" }, { "naxx_challenge_rogue", "naxx_maexxna" },
        { "naxx_challenge_mage", "naxx_heigan" }, { "naxx_challenge_hunter", "naxx_loatheb" },
        { "naxx_challenge_shaman", "naxx_gothik" }, { "naxx_challenge_warlock", "naxx_horsemen" },
        { "naxx_challenge_warrior", "naxx_grobbulus" }, { "naxx_challenge_priest", "naxx_thaddius" },
        { "naxx_challenge_paladin", "naxx_kelthuzad" } };
    auto resolve = [&encounterOf](std::string const& key) {
        auto const it = encounterOf.find(key);
        return it == encounterOf.end() ? std::string() : it->second;
    };
    CHECK(NaxxChallengesAtEntry(920001, resolve).size() == 1); // Faerlina
    CHECK(NaxxChallengesAtEntry(920001, resolve)[0]->questId == 919937);
    CHECK(NaxxChallengesAtEntry(920014, resolve).size() == 1 && NaxxChallengesAtEntry(920014, resolve)[0]->questId == 919961);
    CHECK(NaxxChallengesAtEntry(920000, resolve).empty());     // Anub'Rekhan has none
    // A stand-in is seen only while its Boss quest or one of its challenges is open.
    CHECK(NaxxStandInVisible(true, false) && NaxxStandInVisible(false, true) && NaxxStandInVisible(true, true));
    CHECK(!NaxxStandInVisible(false, false));
    CHECK(NaxxChallengesAtEntry(16112, resolve).empty());      // not a stand-in
    CHECK(NaxxChallengesAtEntry(920001, nullptr).empty());     // no mapping
    std::size_t total = 0;
    for (NaxxBoss const& boss : kNaxxBosses)
        total += NaxxChallengesAtEntry(boss.entry, resolve).size();
    CHECK(total == 9);
    // Two challenges at one boss come back in adventure order.
    std::map<std::string, std::string> const sameBoss = { { "naxx_challenge_mage", "naxx_noth" }, { "naxx_challenge_druid", "naxx_noth" } };
    auto resolveSame = [&sameBoss](std::string const& key) {
        auto const it = sameBoss.find(key);
        return it == sameBoss.end() ? std::string() : it->second;
    };
    std::vector<NaxxQuest const*> const two = NaxxChallengesAtEntry(920003, resolveSame);
    CHECK(two.size() == 2 && two[0]->questId == 919937 && two[1]->questId == 919943);

    CHECK(NaxxClassLabel("DRUID") == "Druid" && NaxxClassLabel("paladin") == "Paladin" && NaxxClassLabel("") == "");

    // Decisions.
    CHECK(NaxxBossDuelAllowed(true));
    CHECK(!NaxxBossDuelAllowed(false));
    CHECK(NaxxShouldCredit(true, "win", "hero"));
    CHECK(!NaxxShouldCredit(false, "win", "hero"));
    CHECK(!NaxxShouldCredit(true, "loss", "hero"));
    CHECK(!NaxxShouldCredit(true, "win", "concede"));
    CHECK(!NaxxShouldCredit(true, "draw", "hero"));
    CHECK(!NaxxShouldCredit(true, "win", ""));
    CHECK(!NaxxShouldCredit(true, "", ""));
}

static void TestSessionWallet()
{
    FakeWallet fake;
    fake.view.coins = 7;
    fake.view.wallet.dust = 30;
    fake.view.packPriceCoins = 10;

    Client c;
    c.settings.ratePerSecond = 1000; // many requests in a row
    c.settings.rateBurst = 1000;
    c.collections = &fake;
    CHECK(FirstErr(c.Send1("WALLET_GET", "w0", "")) == "NOT_READY");
    CHECK(FirstErr(c.Send1("CRAFT_MAKE", "w0", "key=abcdef123456;card=EX1_116")) == "NOT_READY");
    c.Hello();

    auto rep = Decode(c.Send1("WALLET_GET", "w1", "coins=999"));
    CHECK(rep.size() == 1 && rep[0].op == "WALLET" && rep[0].reqId == "w1");
    CHECK(rep[0].payload == "coins=7;dust=30;craft=0;scrap=5.20.100.400;make=40.100.400.1600;packcoins=10");
    fake.failWallet = true;
    CHECK(FirstErr(c.Send1("WALLET_GET", "w2", "")) == "UNAVAILABLE");
    fake.failWallet = false;

    // malformed requests never reach the handler
    for (char const* payload : { "", "key=abcdef123456", "card=EX1_116", "key=SHORT;card=EX1_116", "key=abcdef123456;card=",
                                 "key=ABCDEF123456;card=EX1_116" })
        CHECK(FirstErr(c.Send1("CRAFT_MAKE", "m1", payload)) == "MALFORMED");
    for (char const* payload : { "key=abcdef123456;card=CS2_022", "key=abcdef123456;card=CS2_022;n=0",
                                 "key=abcdef123456;card=CS2_022;n=100", "key=abcdef123456;card=CS2_022;n=x" })
        CHECK(FirstErr(c.Send1("CRAFT_SCRAP", "s1", payload)) == "MALFORMED");
    CHECK(fake.calls.empty());

    // crafted: CRAFTED, then the COLL_CHANGED push
    fake.craft.status = CraftStatus::Ok;
    fake.craft.record.key = "abcdef123456";
    fake.craft.record.cardId = "EX1_116";
    fake.craft.record.count = 1;
    fake.craft.record.dustDelta = -1600;
    fake.craft.record.dustAfter = 400;
    fake.craft.record.collectionVersion = 12;
    fake.craft.record.cardAfter = Count(0, 1);
    rep = Decode(c.Send1("CRAFT_MAKE", "m2", "key=abcdef123456;card=EX1_116"));
    CHECK(rep.size() == 2 && fake.calls.size() == 1 && fake.calls[0] == "make:abcdef123456:EX1_116");
    if (rep.size() == 2)
    {
        CHECK(rep[0].op == "CRAFTED" && rep[0].reqId == "m2");
        CHECK(rep[0].payload == "key=abcdef123456;replay=0;kind=craft;card=EX1_116;n=1;delta=-1600;dust=400;ver=12;total=1;prot=0");
        CHECK(rep[1].op == "COLL_CHANGED" && rep[1].reqId == "0" && rep[1].payload == "ver=12");
    }
    fake.craft.status = CraftStatus::Replay;
    rep = Decode(c.Send1("CRAFT_MAKE", "m3", "key=abcdef123456;card=EX1_116"));
    CHECK(rep.size() == 1 && rep[0].op == "CRAFTED" && Kv(rep[0])["replay"] == "1");

    fake.craft.status = CraftStatus::Ok;
    fake.craft.record.kind = CraftKind::Scrap;
    rep = Decode(c.Send1("CRAFT_SCRAP", "s2", "key=abcdef654321;card=CS2_022;n=2"));
    CHECK(rep.size() == 2 && fake.calls.size() == 3 && fake.calls[2] == "scrap:abcdef654321:CS2_022:2");
    CHECK(rep.size() == 2 && Kv(rep[0])["kind"] == "scrap");

    // CRAFT_EXTRAS: only a key is needed
    size_t const callsBefore = fake.calls.size();
    for (char const* payload : { "", "key=SHORT", "key=ABCDEF123456", "card=all" })
        CHECK(FirstErr(c.Send1("CRAFT_EXTRAS", "x1", payload)) == "MALFORMED");
    CHECK(fake.calls.size() == callsBefore);
    fake.craft.status = CraftStatus::Ok;
    fake.craft.record.key = "abcdef777777";
    fake.craft.record.kind = CraftKind::Extras;
    fake.craft.record.cardId = kExtrasCardId;
    fake.craft.record.count = 12;
    fake.craft.record.dustDelta = 240;
    fake.craft.record.dustAfter = 640;
    fake.craft.record.collectionVersion = 13;
    fake.craft.record.cardAfter = Count(0, 0);
    rep = Decode(c.Send1("CRAFT_EXTRAS", "x2", "key=abcdef777777"));
    CHECK(rep.size() == 2 && fake.calls.size() == callsBefore + 1 && fake.calls.back() == "extras:abcdef777777");
    if (rep.size() == 2)
    {
        CHECK(rep[0].op == "CRAFTED" && rep[0].reqId == "x2");
        CHECK(rep[0].payload == "key=abcdef777777;replay=0;kind=extras;card=all;n=12;delta=240;dust=640;ver=13;total=0;prot=0");
        CHECK(rep[1].op == "COLL_CHANGED" && rep[1].payload == "ver=13");
    }
    fake.craft.status = CraftStatus::Refused;
    fake.craft.why = CraftWhy::Nothing;
    rep = Decode(c.Send1("CRAFT_EXTRAS", "x3", "key=abcdef777777"));
    CHECK(rep.size() == 1 && ErrCode(rep[0]) == "REFUSED" && Kv(rep[0])["why"] == "nothing");
    fake.craft.status = CraftStatus::Replay;
    rep = Decode(c.Send1("CRAFT_EXTRAS", "x4", "key=abcdef777777"));
    CHECK(rep.size() == 1 && rep[0].op == "CRAFTED" && Kv(rep[0])["replay"] == "1");
    fake.craft.status = CraftStatus::Ok;
    fake.craft.record.kind = CraftKind::Scrap;

    // refusals carry why=<code>
    struct Case { CraftStatus status; CraftWhy why; char const* code; char const* whyCode; };
    Case const cases[] = {
        { CraftStatus::Refused, CraftWhy::Npc, "REFUSED", "npc" },
        { CraftStatus::Refused, CraftWhy::Dust, "REFUSED", "dust" },
        { CraftStatus::Refused, CraftWhy::Limit, "REFUSED", "limit" },
        { CraftStatus::Refused, CraftWhy::Copies, "REFUSED", "copies" },
        { CraftStatus::Refused, CraftWhy::Card, "REFUSED", "card" },
        { CraftStatus::Unavailable, CraftWhy::None, "UNAVAILABLE", "" },
        { CraftStatus::CommitFailed, CraftWhy::None, "UNAVAILABLE", "" },
        { CraftStatus::BadKey, CraftWhy::None, "MALFORMED", "" },
    };
    for (Case const& k : cases)
    {
        fake.craft.status = k.status;
        fake.craft.why = k.why;
        for (char const* op : { "CRAFT_MAKE", "CRAFT_SCRAP" })
        {
            rep = Decode(c.Send1(op, "e1", "key=abcdef123456;card=CS2_022;n=1"));
            CHECK(rep.size() == 1 && rep[0].reqId == "e1" && ErrCode(rep[0]) == k.code);
            CHECK(rep.size() == 1 && Kv(rep[0])["why"] == k.whyCode);
        }
    }

    // no handler: unavailable; other CRAFT_* / WALLET_* stay reserved
    c.collections = nullptr;
    CHECK(FirstErr(c.Send1("WALLET_GET", "w3", "")) == "UNAVAILABLE");
    CHECK(FirstErr(c.Send1("CRAFT_MAKE", "w4", "key=abcdef123456;card=EX1_116")) == "UNAVAILABLE");
    CHECK(FirstErr(c.Send1("CRAFT_SCRAP", "w5", "key=abcdef123456;card=EX1_116;n=1")) == "UNAVAILABLE");
    CHECK(FirstErr(c.Send1("CRAFT_EXTRAS", "w5", "key=abcdef123456")) == "UNAVAILABLE");
    CHECK(FirstErr(c.Send1("CRAFT_OTHER", "w6", "")) == "NOT_IMPLEMENTED");
    CHECK(FirstErr(c.Send1("WALLET_SET", "w7", "")) == "NOT_IMPLEMENTED");

    // pushes
    std::vector<std::string> push;
    Wallet w;
    w.dust = 400;
    AppendWalletChanged(push, 35, w, "match", 25);
    AppendCraftAccess(push, true, "Ezra Chatterton");
    AppendCraftAccess(push, false, "");
    rep = Decode(push);
    CHECK(push.size() == 3 && rep.size() == 3);
    if (rep.size() == 3)
    {
        CHECK(rep[0].op == "WALLET_UPD" && rep[0].reqId == "0" && rep[0].payload == "coins=35;dust=400;why=match;gain=25");
        if (rep[0].payload != "coins=35;dust=400;why=match;gain=25")
            std::printf("    WALLET_UPD was %s %s %s\n", rep[0].op.c_str(), rep[0].reqId.c_str(), rep[0].payload.c_str());
        CHECK(rep[1].op == "CRAFT_ACCESS" && rep[1].payload == "open=1;npc=Ezra Chatterton");
        CHECK(rep[2].op == "CRAFT_ACCESS" && rep[2].payload == "open=0");
    }

    // the largest WALLET still fits: everything at its maximum
    c.collections = &fake;
    fake.view.coins = kMaxBalance;
    fake.view.wallet.dust = kMaxBalance;
    fake.view.craftOpen = true;
    fake.view.craftNpc = std::string(100, 'N');
    for (std::size_t i = 0; i < 4; ++i)
        fake.view.values.scrap[i] = fake.view.values.craft[i] = 1000000;
    fake.view.packPriceCoins = kMaxBalance;
    rep = Decode(c.Send1("WALLET_GET", "w8", ""));
    CHECK(rep.size() == 1 && rep[0].op == "WALLET");
}

// ---------------------------------------------------------------- decks
namespace
{
    // Fifteen neutral commons TST_n01..TST_n15 (30 cards at two copies each),
    // class cards, and cards no deck may hold.
    std::shared_ptr<CardCatalog const> DeckCatalog()
    {
        auto c = std::make_shared<CardCatalog>();
        CHECK(c->Add("CS2_022", true, "standard", "FREE", "MAGE"));          // Polymorph
        CHECK(c->Add("CS2_029", true, "standard", "FREE", "MAGE"));          // Fireball
        CHECK(c->Add("CS2_108", true, "standard", "FREE", "WARRIOR"));       // Execute
        CHECK(c->Add("EX1_116", true, "standard", "LEGENDARY", "NEUTRAL"));  // Leeroy Jenkins
        CHECK(c->Add("EX1_062", true, "reward_only", "LEGENDARY", "NEUTRAL")); // Old Murk-Eye (promo)
        CHECK(c->Add("NEW1_016", true, "reward_only", "EPIC", "NEUTRAL"));   // Captain's Parrot (promo)
        CHECK(c->Add("NEW1_006", true, "unreleased", "EPIC", "ROGUE"));      // Adrenaline Rush
        CHECK(c->Add("CS1_113e", false, "none", "COMMON", "NEUTRAL"));       // enchantment
        CHECK(c->Add("TST_nc", false, "standard", "COMMON", "NEUTRAL"));     // synthetic: not collectible
        CHECK(c->Add("TST_none", true, "none", "COMMON", "NEUTRAL"));        // synthetic: acquisition none
        CHECK(c->Add("TST_dream", true, "standard", "COMMON", "DREAM"));     // synthetic: a class no deck has
        // Naxxramas (acquisition quest): Undertaker, Maexxna (Legendary), Duplicate (Mage), Death's Bite (Warrior)
        CHECK(c->Add("FP1_028", true, "quest", "COMMON", "NEUTRAL"));
        CHECK(c->Add("FP1_010", true, "quest", "LEGENDARY", "NEUTRAL"));
        CHECK(c->Add("FP1_018", true, "quest", "COMMON", "MAGE"));
        CHECK(c->Add("FP1_021", true, "quest", "COMMON", "WARRIOR"));
        for (int i = 1; i <= 15; ++i)
        {
            char id[16];
            std::snprintf(id, sizeof(id), "TST_n%02d", i);
            CHECK(c->Add(id, true, "standard", "COMMON", "NEUTRAL"));
        }
        return c;
    }

    // count copies of TST_n01.. (two each, the last one possibly one).
    DeckCards Neutrals(uint32_t count)
    {
        DeckCards cards;
        for (uint32_t i = 0; i < count; ++i)
        {
            char id[16];
            std::snprintf(id, sizeof(id), "TST_n%02u", i / 2 + 1);
            ++cards[id];
        }
        return cards;
    }

    // The fixture recipes' cards with their real classes.
    std::shared_ptr<CardCatalog const> FixtureCatalog()
    {
        auto c = std::make_shared<CardCatalog>();
        for (char const* id : { "EX1_277", "CS2_025", "CS2_023", "CS2_029", "CS2_022" })
            CHECK(c->Add(id, true, "standard", "FREE", "MAGE"));
        for (char const* id : { "CS2_108", "EX1_400", "CS2_106", "CS2_105", "CS2_114", "CS2_103", "EX1_606", "EX1_084",
                                "NEW1_011", "CS2_112" })
            CHECK(c->Add(id, true, "standard", "COMMON", "WARRIOR"));
        for (char const* id : { "CS2_168", "CS2_172", "EX1_015", "CS2_120", "CS2_122", "CS2_124", "CS2_119", "CS2_179",
                                "EX1_593", "CS2_200", "CS2_182" })
            CHECK(c->Add(id, true, "standard", "FREE", "NEUTRAL"));
        return c;
    }

    struct FakeDeckStorage : DeckStorage
    {
        struct Row
        {
            Deck deck;
            bool deleted = false;
        };
        std::map<uint32_t, std::map<uint32_t, Row>> rows;
        std::map<uint32_t, std::map<std::string, DeckOp>> ops;
        std::map<uint32_t, std::map<std::string, std::string>> unlocks;
        std::vector<std::pair<uint32_t, DeckCommit>> commits;
        std::vector<std::pair<uint32_t, ClassCommit>> classCommits;
        int loads = 0;
        int finds = 0;
        bool failLoad = false;
        bool failFind = false;
        bool failCommit = false;
        bool commitThenFail = false; // the change commits but the caller is told it failed

        bool LoadBook(uint32_t accountId, DeckBook& out) override
        {
            ++loads;
            if (failLoad)
                return false;
            DeckBook book;
            for (auto const& entry : rows[accountId])
            {
                book.maxDeckId = std::max(book.maxDeckId, entry.first);
                if (!entry.second.deleted)
                    book.decks[entry.first] = entry.second.deck;
            }
            book.unlocks = unlocks[accountId];
            book.starters = starters[accountId];
            out = book;
            return true;
        }

        std::map<uint32_t, std::map<std::string, uint32_t>> starters;
        std::vector<std::pair<uint32_t, StarterCommit>> starterCommits;
        bool CommitStarter(uint32_t accountId, StarterCommit const& c) override
        {
            if (failCommit)
                return false;
            if (starters[accountId].count(c.cls))
                return false; // primary key
            if (c.withDeck && rows[accountId].count(c.deck.deck.id))
                return false;
            starterCommits.push_back(std::make_pair(accountId, c));
            if (c.withDeck)
                rows[accountId][c.deck.deck.id].deck = c.deck.deck;
            starters[accountId][c.cls] = c.withDeck ? c.deck.deck.id : 0;
            return !commitThenFail;
        }

        bool FindDeckOp(uint32_t accountId, std::string const& key, bool& found, DeckOp& out) override
        {
            ++finds;
            if (failFind)
                return false;
            auto const it = ops[accountId].find(key);
            found = it != ops[accountId].end();
            if (found)
                out = it->second;
            return true;
        }

        bool CommitDeck(uint32_t accountId, DeckCommit const& c) override
        {
            if (failCommit)
                return false;
            if (!c.op.key.empty() && ops[accountId].count(c.op.key))
                return false; // primary key
            std::map<uint32_t, Row>& account = rows[accountId];
            if (c.action == DeckAction::Create && account.count(c.deck.id))
                return false; // primary key: ids are never reused
            if (c.action != DeckAction::Create && (!account.count(c.deck.id) || account[c.deck.id].deleted))
                return false;
            commits.push_back(std::make_pair(accountId, c));
            Row& row = account[c.deck.id];
            row.deck = c.deck;
            row.deleted = c.action == DeckAction::Delete;
            if (!c.op.key.empty())
                ops[accountId][c.op.key] = c.op;
            return !commitThenFail;
        }

        bool CommitClass(uint32_t accountId, ClassCommit const& c) override
        {
            if (failCommit)
                return false;
            classCommits.push_back(std::make_pair(accountId, c));
            if (c.unlock)
                unlocks[accountId][c.cls] = c.source;
            else
                unlocks[accountId].erase(c.cls);
            return !commitThenFail;
        }
    };

    DeckSaveRequest SaveReq(std::string const& key, uint32_t id, uint32_t rev, std::string const& cls,
                            std::string const& name, DeckCards const& cards)
    {
        DeckSaveRequest r;
        r.key = key;
        r.id = id;
        r.baseRev = rev;
        r.deckClass = cls;
        r.name = name;
        r.cards = cards;
        r.source = "char:42";
        return r;
    }

    DeckDeleteRequest DeleteReq(std::string const& key, uint32_t id, uint32_t rev)
    {
        DeckDeleteRequest r;
        r.key = key;
        r.id = id;
        r.baseRev = rev;
        r.source = "char:42";
        return r;
    }

    // Collection, deck stores and services wired like the game glue.
    struct DeckWorld
    {
        FakeStorage collStorage;
        FakeDeckStorage deckStorage;
        CollectionService collections { collStorage };
        DeckService decks { deckStorage, collections };

        void Own(uint32_t account, DeckCards const& cards)
        {
            for (auto const& entry : cards)
                CHECK(collections.Grant(account, entry.first, entry.second, GrantSource::Gm, ProtectionMode::Unprotected,
                                        "gm_grant", "gm:9").status == MutationStatus::Ok);
        }
    };

    struct FakeDecks : FakeCollections
    {
        DeckList list;
        bool failList = false;
        DeckSaveResult save;
        DeckDeleteResult del;
        std::vector<DeckSaveRequest> saves;
        std::vector<DeckDeleteRequest> deletes;

        bool ListDecks(DeckList& out) override
        {
            if (failList)
                return false;
            out = list;
            return true;
        }

        DeckSaveResult SaveDeck(DeckSaveRequest const& req) override
        {
            saves.push_back(req);
            return save;
        }

        DeckDeleteResult DeleteDeck(DeckDeleteRequest const& req) override
        {
            deletes.push_back(req);
            return del;
        }
    };

    Deck MakeDeck(uint32_t id, uint32_t rev, std::string const& cls, std::string const& name, DeckCards const& cards,
                  std::string const& origin = kDeckOriginPlayer)
    {
        Deck d;
        d.id = id;
        d.rev = rev;
        d.deckClass = cls;
        d.name = name;
        d.cards = cards;
        d.origin = origin;
        return d;
    }
}

static void TestDeckClasses()
{
    std::vector<std::string> const& classes = DeckClasses();
    CHECK(classes.size() == 9);
    CHECK(std::is_sorted(classes.begin(), classes.end()));
    for (std::string const& c : classes)
        CHECK(IsDeckClass(c) && ParseDeckClass(c) == c);
    CHECK(!IsDeckClass("mage") && !IsDeckClass("NEUTRAL") && !IsDeckClass("DREAM") && !IsDeckClass(""));
    CHECK(ParseDeckClass("mage") == "MAGE" && ParseDeckClass("Warrior") == "WARRIOR");
    CHECK(ParseDeckClass("neutral").empty() && ParseDeckClass("DEATHKNIGHT").empty() && ParseDeckClass("").empty());

    // core Player::getClass(): 1 WARRIOR ... 11 DRUID; 6 and 10 do not exist in 1.12
    char const* const expected[] = { "", "WARRIOR", "PALADIN", "HUNTER", "ROGUE", "PRIEST", "", "SHAMAN", "MAGE",
                                     "WARLOCK", "", "DRUID", "" };
    for (uint32_t i = 0; i <= 12; ++i)
        CHECK(DeckClassOfWowClass(i) == expected[i]);
    CHECK(DeckClassOfWowClass(255).empty());

    // the catalog keeps the card class
    auto const catalog = DeckCatalog();
    CHECK(catalog->Find("CS2_022")->cardClass == "MAGE" && catalog->Find("TST_dream")->cardClass == "DREAM");
}

static void TestDeckNames()
{
    std::string out;
    CHECK(NormalizeDeckName("My Deck", out) && out == "My Deck");
    CHECK(NormalizeDeckName("   trimmed  ", out) && out == "trimmed");
    CHECK(NormalizeDeckName("a  b", out) && out == "a  b"); // inner spaces stay
    CHECK(NormalizeDeckName("x;y=z~w", out) && out == "x;y=z~w");
    CHECK(NormalizeDeckName("-", out) && out == "-");
    CHECK(NormalizeDeckName(std::string(24, 'a'), out));
    CHECK(NormalizeDeckName("  " + std::string(24, 'a') + "  ", out) && out == std::string(24, 'a'));
    CHECK(!NormalizeDeckName(std::string(25, 'a'), out));
    CHECK(!NormalizeDeckName("", out) && !NormalizeDeckName("    ", out));
    CHECK(!NormalizeDeckName("a|b", out) && !NormalizeDeckName("|cff00ff00x", out));
    for (int b = 1; b < 0x20; ++b)
        CHECK(!NormalizeDeckName(std::string("a") + static_cast<char>(b) + "b", out));
    CHECK(!NormalizeDeckName(std::string("a\0b", 3), out));
    CHECK(!NormalizeDeckName("a\x7f" "b", out));
    CHECK(!NormalizeDeckName("\tname", out)); // only spaces are trimmed

    // UTF-8: characters, not bytes, count up to 24; at most 64 bytes
    std::string ae;
    for (int i = 0; i < 24; ++i)
        ae += "\xc3\xa4"; // a-umlaut, 2 bytes
    CHECK(NormalizeDeckName(ae, out) && out.size() == 48);
    CHECK(!NormalizeDeckName(ae + "\xc3\xa4", out)); // 25 characters
    std::string euro;
    for (int i = 0; i < 21; ++i)
        euro += "\xe2\x82\xac"; // euro sign, 3 bytes
    CHECK(NormalizeDeckName(euro, out) && out.size() == 63);
    CHECK(!NormalizeDeckName(euro + "\xe2\x82\xac", out)); // 22 characters but 66 bytes
    std::string mixed;
    for (int i = 0; i < 16; ++i)
        mixed += "\xe2\x82\xac";
    for (int i = 0; i < 8; ++i)
        mixed += "\xc3\xa4";
    CHECK(NormalizeDeckName(mixed, out) && out.size() == 64); // 24 characters, 64 bytes
    CHECK(!NormalizeDeckName(mixed + "a", out));                // 65 bytes
    CHECK(NormalizeDeckName("\xef\xbf\xbd", out));              // U+FFFD, the largest 3-byte sequence family

    // invalid UTF-8
    char const* const bad[] = {
        "\xc3", "a\xc3", "\x80", "a\xbf" "b", "\xc0\xaf", "\xc1\xbf", "\xe0\x80\xaf", "\xe0\x9f\xbf",
        "\xed\xa0\x80", "\xed\xbf\xbf", "\xe2\x82", "\xe2\x28\xac", "\xc3\x28",
        "\xf0\x9f\x98\x80", // U+1F600: valid UTF-8, but above U+FFFF (the core's utf8mb3 connection)
        "\xf4\x8f\xbf\xbf", "\xf5\x80\x80\x80", "\xff", "\xfe",
    };
    for (char const* b : bad)
        CHECK(!NormalizeDeckName(b, out));
}

static void TestDeckKeysAndCardCodec()
{
    CHECK(IsValidDeckKey("abcdef123456") && IsValidDeckKey("abcdef1234567890"));
    CHECK(!IsValidDeckKey("abcdef12345") && !IsValidDeckKey("abcdef1234567890a") && !IsValidDeckKey(""));
    CHECK(!IsValidDeckKey("ABCDEF123456") && !IsValidDeckKey("abcdef-23456") && !IsValidDeckKey("abcdef 23456"));

    DeckCards cards;
    cards["b"] = 1;
    cards["A_1"] = 2;
    cards["A1"] = 1;
    CHECK(EncodeDeckCards(cards) == "A1.1,A_1.2,b.1"); // byte order
    CHECK(EncodeDeckCards(DeckCards()).empty());

    DeckCards out;
    CHECK(DecodeDeckCards("", out) && out.empty());
    CHECK(DecodeDeckCards("b.1,A_1.2,A1.1", out) && out == cards); // any order in, sorted out
    CHECK(DecodeDeckCards("CS2_022.2", out) && out.size() == 1 && out["CS2_022"] == 2);
    CHECK(DecodeDeckCards("A.0", out) && out["A"] == 0);       // a rule question (copies), not syntax
    CHECK(DecodeDeckCards("A.999", out) && out["A"] == 999);
    CHECK(DecodeDeckCards(std::string(32, 'A') + ".1", out));
    out.clear();
    out["keep"] = 1;
    char const* const bad[] = { ",", "A.1,", ",A.1", "A", "A.", ".1", "A.1.1", "A.-1", "A.+1", "A.1000", "A.x",
                                "A b.1", "A;b.1", "A.1,A.1", "A.1,,B.1", "A.1 ", " A.1", "A~.1", "A-1.1" };
    for (char const* b : bad)
        CHECK(!DecodeDeckCards(b, out));
    CHECK(!DecodeDeckCards(std::string(33, 'A') + ".1", out));
    CHECK(out.size() == 1 && out["keep"] == 1); // untouched on failure
}

static void TestDeckRules()
{
    auto const catalog = DeckCatalog();
    CardCatalog const& c = *catalog;
    auto with = [](DeckCards cards, std::string const& id, uint32_t n)
    {
        cards[id] = n;
        return cards;
    };

    CHECK(ValidateDeckCards(c, "MAGE", DeckCards()) == DeckWhy::None);
    CHECK(ValidateDeckCards(c, "MAGE", Neutrals(30)) == DeckWhy::None);
    CHECK(ValidateDeckCards(c, "MAGE", with(Neutrals(28), "CS2_022", 2)) == DeckWhy::None);
    CHECK(ValidateDeckCards(c, "MAGE", with(Neutrals(29), "EX1_116", 1)) == DeckWhy::None);
    CHECK(ValidateDeckCards(c, "MAGE", with(Neutrals(28), "EX1_062", 1)) == DeckWhy::None);  // reward_only is deckable
    CHECK(ValidateDeckCards(c, "WARRIOR", with(Neutrals(10), "CS2_108", 2)) == DeckWhy::None);

    // card: unknown (ids are exact), not collectible, acquisition none / unreleased
    for (char const* id : { "CS2_999", "cs2_022", "CS1_113e", "TST_nc", "TST_none", "NEW1_006" })
        CHECK(ValidateDeckCards(c, "ROGUE", with(Neutrals(2), id, 1)) == DeckWhy::Card);
    // cardclass: another class, or a class no deck has
    CHECK(ValidateDeckCards(c, "MAGE", with(Neutrals(2), "CS2_108", 1)) == DeckWhy::CardClass);
    CHECK(ValidateDeckCards(c, "WARRIOR", with(Neutrals(2), "CS2_022", 1)) == DeckWhy::CardClass);
    CHECK(ValidateDeckCards(c, "MAGE", with(Neutrals(2), "TST_dream", 1)) == DeckWhy::CardClass);
    // copies: 0, 3, a second Legendary
    CHECK(ValidateDeckCards(c, "MAGE", with(Neutrals(2), "CS2_022", 0)) == DeckWhy::Copies);
    CHECK(ValidateDeckCards(c, "MAGE", with(Neutrals(2), "CS2_022", 3)) == DeckWhy::Copies);
    CHECK(ValidateDeckCards(c, "MAGE", with(Neutrals(2), "EX1_116", 2)) == DeckWhy::Copies);
    CHECK(ValidateDeckCards(c, "MAGE", with(Neutrals(2), "EX1_062", 2)) == DeckWhy::Copies);
    // size: 31
    CHECK(ValidateDeckCards(c, "MAGE", with(Neutrals(30), "CS2_022", 1)) == DeckWhy::Size);
    CHECK(ValidateDeckCards(c, "MAGE", with(Neutrals(29), "CS2_022", 1)) == DeckWhy::None);

    // order: card before cardclass before copies before size, whatever the id order
    CHECK(ValidateDeckCards(c, "MAGE", with(with(Neutrals(2), "CS2_108", 1), "ZZZ", 1)) == DeckWhy::Card);
    CHECK(ValidateDeckCards(c, "MAGE", with(with(Neutrals(2), "TST_n01", 3), "TST_dream", 1)) == DeckWhy::CardClass);
    CHECK(ValidateDeckCards(c, "MAGE", with(Neutrals(30), "EX1_116", 2)) == DeckWhy::Copies);
    CHECK(ValidateDeckCards(c, "MAGE", with(with(Neutrals(30), "CS2_022", 2), "AAA", 1)) == DeckWhy::Card);

    struct Code { DeckWhy why; char const* code; };
    Code const codes[] = {
        { DeckWhy::None, "" }, { DeckWhy::Class, "class" }, { DeckWhy::Name, "name" }, { DeckWhy::Card, "card" },
        { DeckWhy::CardClass, "cardclass" }, { DeckWhy::Copies, "copies" }, { DeckWhy::Size, "size" },
        { DeckWhy::Cap, "cap" }, { DeckWhy::Missing, "missing" }, { DeckWhy::Conflict, "conflict" }, { DeckWhy::Key, "key" },
    };
    for (Code const& k : codes)
    {
        CHECK(std::string(DeckWhyCode(k.why)) == k.code);
        CHECK(std::string(DeckWhyText(k.why)).size() > 1);
    }
}

static void TestDeckStates()
{
    auto const catalog = DeckCatalog();
    CardCatalog const& c = *catalog;
    Collection owned;
    for (auto const& entry : Neutrals(30))
        owned.Set(entry.first, Count(1, 1)); // protected and unprotected copies both count
    owned.Set("CS2_022", Count(0, 2));
    std::set<std::string> const mage = PlayableClasses("MAGE", std::set<std::string>());
    std::set<std::string> const warrior = PlayableClasses("WARRIOR", std::set<std::string>());

    Deck deck = MakeDeck(1, 1, "MAGE", "D", Neutrals(30));
    CHECK(EvaluateDeck(c, owned, deck, &mage).Text() == "ok");
    CHECK(EvaluateDeck(c, owned, deck, &mage).Playable());
    CHECK(EvaluateDeck(c, owned, deck, &warrior).Text() == "locked");
    CHECK(EvaluateDeck(c, owned, deck, &warrior).Complete() && !EvaluateDeck(c, owned, deck, &warrior).Playable());
    CHECK(EvaluateDeck(c, owned, deck, nullptr).Text() == "ok"); // no character: never locked

    // one owned copy serves every deck: a second deck with the same cards is ok too
    Deck second = MakeDeck(2, 1, "MAGE", "E", Neutrals(30));
    CHECK(EvaluateDeck(c, owned, second, &mage).Text() == "ok");

    CHECK(EvaluateDeck(c, owned, MakeDeck(1, 1, "MAGE", "D", Neutrals(29)), &mage).Text() == "size");
    CHECK(EvaluateDeck(c, Collection(), deck, &mage).Text() == "owned");
    Collection oneShort = owned;
    oneShort.Set("TST_n15", Count(0, 1));
    CHECK(EvaluateDeck(c, oneShort, deck, &mage).Text() == "owned");
    CHECK(EvaluateDeck(c, Collection(), MakeDeck(1, 1, "MAGE", "D", DeckCards()), &mage).Text() == "size");
    CHECK(EvaluateDeck(c, Collection(), MakeDeck(1, 1, "MAGE", "D", Neutrals(1)), &mage).Text() == "size,owned");

    // invalid: a stored deck that breaks a hard rule now
    DeckCards broken = Neutrals(28);
    broken["CS2_108"] = 2; // a Warrior card in a Mage deck (card data changed)
    owned.Set("CS2_108", Count(2, 0));
    CHECK(EvaluateDeck(c, owned, MakeDeck(1, 1, "MAGE", "D", broken), &mage).Text() == "invalid");
    DeckCards gone = Neutrals(28);
    gone["OLD_card"] = 2;
    CHECK(EvaluateDeck(c, owned, MakeDeck(1, 1, "MAGE", "D", gone), &mage).Text() == "owned,invalid");
    Deck damaged = deck;
    damaged.damaged = true;
    CHECK(EvaluateDeck(c, owned, damaged, &mage).Text() == "invalid");
    CHECK(EvaluateDeck(c, owned, MakeDeck(1, 1, "NEUTRAL", "D", Neutrals(30)), &mage).Text() == "invalid,locked");
    // every code, in order
    DeckCards wrong = Neutrals(20);
    wrong["CS2_022"] = 2; // a Mage card in a Warrior deck
    CHECK(EvaluateDeck(c, Collection(), MakeDeck(1, 1, "WARRIOR", "D", wrong), &mage).Text() == "size,owned,invalid,locked");

    DeckState s;
    CHECK(s.Text() == "ok");
    s.locked = true;
    s.owned = true;
    CHECK(s.Text() == "owned,locked" && !s.Complete());

    std::set<std::string> unlocks;
    unlocks.insert("WARRIOR");
    CHECK((PlayableClasses("MAGE", unlocks) == std::set<std::string>{ "MAGE", "WARRIOR" }));
    CHECK((PlayableClasses("", unlocks) == std::set<std::string>{ "WARRIOR" }));
    CHECK((PlayableClasses("WARRIOR", unlocks) == std::set<std::string>{ "WARRIOR" }));
}

static void TestDeckPayloads()
{
    DeckList list;
    list.cap = 29;
    list.start = "MAGE";
    list.unlocked = { "PRIEST", "WARRIOR" };
    DeckListEntry a;
    a.deck = MakeDeck(1, 3, "MAGE", "Fire;Ice=1", Neutrals(3));
    a.state = "size,owned";
    DeckListEntry b;
    b.deck = MakeDeck(4, 1, "WARRIOR", "Dev Warrior", DeckCards(), kDeckOriginGmFixture);
    b.state = "size,locked";
    list.decks = { a, b };
    CHECK(EncodeDecksPayload(list) == "cap=29;n=2;start=MAGE;unlocked=PRIEST,WARRIOR;"
          "d1=1.3.MAGE.player;m1=Fire~sIce~q1;c1=TST_n01.2,TST_n02.1;s1=size,owned;"
          "d2=4.1.WARRIOR.gm_fixture;m2=Dev Warrior;c2=;s2=size,locked");
    std::map<std::string, std::string> kv;
    CHECK(KvDecode(EncodeDecksPayload(list), kv) && kv["m1"] == "Fire;Ice=1" && kv["c2"].empty());

    DeckList empty;
    empty.cap = 1;
    CHECK(EncodeDecksPayload(empty) == "cap=1;n=0;start=-;unlocked=");

    CHECK(EncodeDeckSavedPayload("abcdef123456", 7, 2, false, "ok") == "key=abcdef123456;id=7;rev=2;replay=0;state=ok");
    CHECK(EncodeDeckSavedPayload("abcdef123456", 7, 2, true, "gone") == "key=abcdef123456;id=7;rev=2;replay=1;state=gone");
    CHECK(EncodeDeckDeletedPayload("abcdef654321", 7, false) == "key=abcdef654321;id=7;replay=0");
    CHECK(EncodeDeckDeletedPayload("abcdef654321", 7, true) == "key=abcdef654321;id=7;replay=1");
    CHECK(EncodeDeckChangedPayload("gm") == "why=gm" && EncodeDeckChangedPayload("class") == "why=class");
}

static void TestDeckFixtures()
{
    auto const catalog = FixtureCatalog();
    for (char const* which : { "mage", "warrior", "MAGE", "Warrior" })
    {
        DeckFixture f;
        CHECK(GetDeckFixture(which, f));
        Deck d = MakeDeck(1, 1, f.deckClass, f.name, f.cards, kDeckOriginGmFixture);
        CHECK(d.CardCount() == kDeckSize && f.cards.size() == 15);
        for (auto const& entry : f.cards)
            CHECK(entry.second == 2);
        CHECK(ValidateDeckCards(*catalog, f.deckClass, f.cards) == DeckWhy::None);
        std::string name;
        CHECK(NormalizeDeckName(f.name, name) && name == f.name);
    }
    DeckFixture f;
    CHECK(GetDeckFixture("mage", f) && f.deckClass == "MAGE" && f.name == "Dev Mage" && f.cards.count("CS2_029"));
    CHECK(GetDeckFixture("warrior", f) && f.deckClass == "WARRIOR" && f.name == "Dev Warrior" && f.cards.count("NEW1_011"));
    CHECK(!GetDeckFixture("rogue", f) && !GetDeckFixture("", f));
    // the Warrior recipe does not fit a Mage deck
    CHECK(GetDeckFixture("warrior", f) && ValidateDeckCards(*catalog, "MAGE", f.cards) == DeckWhy::CardClass);
}

static void TestDeckService()
{
    DeckWorld w;
    DeckService& svc = w.decks;
    FakeDeckStorage& ds = w.deckStorage;
    DeckList list;
    DeckBook book;

    // no card data: every deck operation is unavailable, nothing is read
    CHECK(!svc.List(1, "MAGE", list));
    CHECK(svc.SaveDeck(1, SaveReq("aaaaaaaaaaa1", 0, 0, "MAGE", "First", Neutrals(2)), "MAGE").status ==
          DeckStatus::Unavailable);
    CHECK(svc.DeleteDeck(1, DeleteReq("aaaaaaaaaaa1", 1, 1)).status == DeckStatus::Unavailable);
    CHECK(svc.CheckForMatch(1, 1, "MAGE").status == DeckMatchCheck::Status::Unavailable);
    CHECK(ds.loads == 0 && ds.finds == 0 && ds.commits.empty());

    w.collections.SetCatalog(DeckCatalog());
    w.collections.SetOnline(1);
    svc.SetOnline(1);
    CHECK(svc.IsOnline(1) && svc.MaxSaved() == kDefaultMaxSavedDecks);
    svc.SetMaxSaved(0);
    CHECK(svc.MaxSaved() == 1);
    svc.SetMaxSaved(1000);
    CHECK(svc.MaxSaved() == kMaxSavedDecksLimit);
    svc.SetMaxSaved(3);

    // empty book
    CHECK(svc.List(1, "MAGE", list) && list.cap == 3 && list.start == "MAGE" && list.unlocked.empty() && list.decks.empty());
    CHECK(svc.IsCached(1) && ds.loads == 1);

    // create: id 1, rev 1, trimmed name, one commit with op and ledger data
    DeckCards cards = Neutrals(4);
    cards["CS2_022"] = 2;
    DeckSaveResult r = svc.SaveDeck(1, SaveReq("aaaaaaaaaaa1", 0, 0, "MAGE", "  First  ", cards), "MAGE");
    CHECK(r.status == DeckStatus::Ok && r.created && r.id == 1 && r.rev == 1 && r.state == "size,owned");
    CHECK(r.deck.name == "First" && r.deck.origin == "player" && r.deck.cards == cards);
    CHECK(ds.commits.size() == 1);
    if (ds.commits.size() == 1)
    {
        DeckCommit const& c = ds.commits[0].second;
        CHECK(c.action == DeckAction::Create && c.revBefore == 0 && c.deck.id == 1 && c.deck.rev == 1);
        CHECK(c.op.key == "aaaaaaaaaaa1" && c.op.op == "save" && c.op.deckId == 1 && c.op.rev == 1);
        CHECK(c.reason == "deck_save" && c.source == "char:42" && c.deck.name == "First");
    }
    CHECK(ds.loads == 1); // the book came from the cache

    // ownership changes the state (shared collection), not the deck
    w.Own(1, cards);
    CHECK(svc.List(1, "MAGE", list) && list.decks.size() == 1 && list.decks[0].state == "size");
    CHECK(svc.List(1, "WARRIOR", list) && list.decks[0].state == "size,locked");
    CHECK(svc.List(1, "", list) && list.start.empty() && list.decks[0].state == "size");

    // update: rev 2, origin kept
    cards = Neutrals(30);
    r = svc.SaveDeck(1, SaveReq("aaaaaaaaaaa2", 1, 1, "MAGE", "Renamed", cards), "MAGE");
    CHECK(r.status == DeckStatus::Ok && !r.created && r.id == 1 && r.rev == 2 && r.state == "owned");
    CHECK(ds.commits.size() == 2 && ds.commits.back().second.action == DeckAction::Update &&
          ds.commits.back().second.revBefore == 1 && ds.commits.back().second.op.rev == 2);
    w.Own(1, Neutrals(30));
    CHECK(svc.List(1, "MAGE", list) && list.decks[0].state == "ok" && list.decks[0].deck.name == "Renamed");

    // refusals: nothing written
    auto refused = [&](DeckSaveRequest const& req, DeckWhy why)
    {
        std::size_t const before = ds.commits.size();
        DeckSaveResult const x = svc.SaveDeck(1, req, "MAGE");
        CHECK(x.status == DeckStatus::Refused && x.why == why);
        CHECK(ds.commits.size() == before);
    };
    refused(SaveReq("bbbbbbbbbbb1", 1, 1, "MAGE", "X", cards), DeckWhy::Conflict);   // stale rev
    refused(SaveReq("bbbbbbbbbbb1", 1, 3, "MAGE", "X", cards), DeckWhy::Conflict);   // future rev
    refused(SaveReq("bbbbbbbbbbb1", 9, 1, "MAGE", "X", cards), DeckWhy::Missing);
    refused(SaveReq("bbbbbbbbbbb1", 1, 2, "PRIEST", "X", cards), DeckWhy::Class);    // class fixed at creation
    refused(SaveReq("bbbbbbbbbbb1", 0, 0, "mage", "X", cards), DeckWhy::Class);
    refused(SaveReq("bbbbbbbbbbb1", 0, 0, "NEUTRAL", "X", cards), DeckWhy::Class);
    refused(SaveReq("bbbbbbbbbbb1", 0, 0, "MAGE", "  ", cards), DeckWhy::Name);
    refused(SaveReq("bbbbbbbbbbb1", 0, 0, "MAGE", "a|b", cards), DeckWhy::Name);
    DeckCards big = Neutrals(30);
    big["CS2_022"] = 1;
    refused(SaveReq("bbbbbbbbbbb1", 0, 0, "MAGE", "X", big), DeckWhy::Size);
    DeckCards bad = Neutrals(2);
    bad["CS2_108"] = 1;
    refused(SaveReq("bbbbbbbbbbb1", 0, 0, "MAGE", "X", bad), DeckWhy::CardClass);
    bad["NOPE"] = 1;
    refused(SaveReq("bbbbbbbbbbb1", 0, 0, "MAGE", "X", bad), DeckWhy::Card);
    bad = Neutrals(2);
    bad["EX1_116"] = 2;
    refused(SaveReq("bbbbbbbbbbb1", 0, 0, "MAGE", "X", bad), DeckWhy::Copies);
    CHECK(svc.SaveDeck(1, SaveReq("SHORT", 0, 0, "MAGE", "X", cards), "MAGE").status == DeckStatus::BadKey);
    CHECK(svc.SaveDeck(1, SaveReq("BBBBBBBBBBB1", 0, 0, "MAGE", "X", cards), "MAGE").status == DeckStatus::BadKey);
    // the payload is checked before the key is looked up: a refused request leaves no op behind
    CHECK(svc.SaveDeck(1, SaveReq("bbbbbbbbbbb1", 0, 0, "MAGE", "Second", cards), "MAGE").status == DeckStatus::Ok);

    // replay of the update and of the create: the stored id/rev, the state now
    std::size_t const commits = ds.commits.size();
    int finds = ds.finds;
    r = svc.SaveDeck(1, SaveReq("aaaaaaaaaaa2", 1, 1, "MAGE", "Renamed", cards), "MAGE");
    CHECK(r.status == DeckStatus::Replay && r.id == 1 && r.rev == 2 && r.state == "ok" && ds.finds == finds + 1);
    r = svc.SaveDeck(1, SaveReq("aaaaaaaaaaa1", 0, 0, "MAGE", "  First  ", Neutrals(4)), "MAGE");
    CHECK(r.status == DeckStatus::Replay && r.id == 1 && r.rev == 1 && r.state == "ok");
    r = svc.SaveDeck(1, SaveReq("aaaaaaaaaaa1", 1, 0, "MAGE", "First", Neutrals(4)), "WARRIOR"); // id named
    CHECK(r.status == DeckStatus::Replay && r.id == 1 && r.rev == 1 && r.state == "locked");
    CHECK(ds.commits.size() == commits);

    // a key reused for another operation or deck
    refused(SaveReq("aaaaaaaaaaa2", 0, 0, "MAGE", "X", cards), DeckWhy::Key);       // an update key for a create
    refused(SaveReq("aaaaaaaaaaa1", 2, 1, "MAGE", "X", cards), DeckWhy::Key);       // deck 1's create for deck 2
    DeckDeleteResult d = svc.DeleteDeck(1, DeleteReq("aaaaaaaaaaa1", 1, 2));       // a save key for a delete
    CHECK(d.status == DeckStatus::Refused && d.why == DeckWhy::Key);

    // cap: 2 live decks of 3, one more is fine, then refused (bypassCap still creates)
    CHECK(svc.SaveDeck(1, SaveReq("ccccccccccc1", 0, 0, "WARRIOR", "Third", Neutrals(2)), "MAGE").id == 3);
    r = svc.SaveDeck(1, SaveReq("ccccccccccc2", 0, 0, "MAGE", "Fourth", Neutrals(2)), "MAGE");
    CHECK(r.status == DeckStatus::Refused && r.why == DeckWhy::Cap);
    DeckSaveRequest starter = SaveReq("", 0, 0, "PRIEST", "Starter", Neutrals(30));
    starter.bypassCap = true;
    starter.origin = "starter";
    starter.reason = "starter";
    r = svc.SaveDeck(1, starter, "MAGE");
    CHECK(r.status == DeckStatus::Ok && r.id == 4 && r.state == "locked" && r.deck.origin == "starter");
    CHECK(ds.commits.back().second.op.key.empty() && ds.ops[1].size() == 4); // no op row without a key
    CHECK(svc.List(1, "MAGE", list) && list.decks.size() == 4 && list.cap == 3);

    // soft delete of the newest deck: the row stays, its id is never used again
    d = svc.DeleteDeck(1, DeleteReq("ddddddddddd1", 4, 1));
    CHECK(d.status == DeckStatus::Ok && d.deck.rev == 2 && ds.rows[1][4].deleted && ds.rows[1][4].deck.rev == 2);
    CHECK(ds.commits.back().second.action == DeckAction::Delete && ds.commits.back().second.op.op == "delete" &&
          ds.commits.back().second.op.rev == 2 && ds.commits.back().second.revBefore == 1);
    CHECK(svc.List(1, "MAGE", list) && list.decks.size() == 3 && list.decks.back().deck.id == 3);
    CHECK(svc.GetBook(1, book) && book.maxDeckId == 4 && book.decks.count(4) == 0);
    CHECK(svc.DeleteDeck(1, DeleteReq("ddddddddddd2", 3, 2)).why == DeckWhy::Conflict);
    CHECK(svc.DeleteDeck(1, DeleteReq("ddddddddddd2", 3, 1)).status == DeckStatus::Ok);
    r = svc.SaveDeck(1, SaveReq("eeeeeeeeeee1", 0, 0, "MAGE", "Fifth", Neutrals(2)), "MAGE");
    CHECK(r.status == DeckStatus::Ok && r.id == 5);
    // deleted decks are missing for everything else
    CHECK(svc.DeleteDeck(1, DeleteReq("ddddddddddd3", 4, 2)).why == DeckWhy::Missing);
    refused(SaveReq("ddddddddddd3", 4, 2, "PRIEST", "X", Neutrals(2)), DeckWhy::Missing);
    // delete replay; the create key of a deleted deck replays as gone
    finds = ds.finds;
    d = svc.DeleteDeck(1, DeleteReq("ddddddddddd1", 4, 1));
    CHECK(d.status == DeckStatus::Replay && ds.finds == finds + 1);
    r = svc.SaveDeck(1, SaveReq("ccccccccccc1", 0, 0, "WARRIOR", "Third", Neutrals(2)), "MAGE");
    CHECK(r.status == DeckStatus::Replay && r.id == 3 && r.rev == 1 && r.state == "gone");
    d = svc.DeleteDeck(1, DeleteReq("ddddddddddd1", 3, 1)); // the key belongs to deck 4's delete
    CHECK(d.status == DeckStatus::Refused && d.why == DeckWhy::Key);
    refused(SaveReq("ddddddddddd1", 4, 1, "PRIEST", "X", Neutrals(2)), DeckWhy::Key); // a delete key for a save

    // "restart": everything is read again; replays and the id sequence survive
    svc.SetOffline(1);
    svc.SetOnline(1);
    CHECK(!svc.IsCached(1));
    int const loads = ds.loads;
    CHECK(svc.DeleteDeck(1, DeleteReq("ddddddddddd1", 4, 1)).status == DeckStatus::Replay);
    r = svc.SaveDeck(1, SaveReq("aaaaaaaaaaa2", 1, 1, "MAGE", "Renamed", cards), "MAGE");
    CHECK(r.status == DeckStatus::Replay && r.id == 1 && r.rev == 2 && r.state == "ok");
    CHECK(ds.loads == loads + 1);
    r = svc.SaveDeck(1, SaveReq("eeeeeeeeeee2", 0, 0, "MAGE", "Sixth", Neutrals(2)), "MAGE");
    CHECK(r.status == DeckStatus::Refused && r.why == DeckWhy::Cap); // decks 1, 2, 5 are live
    svc.SetMaxSaved(10);
    r = svc.SaveDeck(1, SaveReq("eeeeeeeeeee2", 0, 0, "MAGE", "Sixth", Neutrals(2)), "MAGE");
    CHECK(r.status == DeckStatus::Ok && r.id == 6); // 1 + the highest id ever used (5)

    // a failed commit leaves storage and the next answer unchanged, and drops the cache
    std::size_t const before = ds.commits.size();
    auto const rowsBefore = ds.rows[1].size();
    ds.failCommit = true;
    r = svc.SaveDeck(1, SaveReq("fffffffffff1", 1, 2, "MAGE", "Broken", Neutrals(2)), "MAGE");
    CHECK(r.status == DeckStatus::CommitFailed && !svc.IsCached(1));
    CHECK(svc.DeleteDeck(1, DeleteReq("fffffffffff2", 1, 2)).status == DeckStatus::CommitFailed);
    CHECK(svc.SaveDeck(1, SaveReq("fffffffffff3", 0, 0, "MAGE", "New", Neutrals(2)), "MAGE").status ==
          DeckStatus::CommitFailed);
    ds.failCommit = false;
    CHECK(ds.commits.size() == before && ds.rows[1].size() == rowsBefore && ds.ops[1].count("fffffffffff1") == 0);
    CHECK(svc.List(1, "MAGE", list) && list.decks.size() == 4 && list.decks[0].deck.name == "Renamed" &&
          list.decks[0].deck.rev == 2);
    // ... and the same key works afterwards
    r = svc.SaveDeck(1, SaveReq("fffffffffff1", 1, 2, "MAGE", "Fixed", cards), "MAGE");
    CHECK(r.status == DeckStatus::Ok && r.rev == 3);

    // a commit whose outcome was lost: the retry with the same key replays it
    ds.commitThenFail = true;
    r = svc.SaveDeck(1, SaveReq("ggggggggggg1", 0, 0, "HUNTER", "Lost", Neutrals(2)), "MAGE");
    CHECK(r.status == DeckStatus::CommitFailed && !svc.IsCached(1));
    CHECK(svc.DeleteDeck(1, DeleteReq("ggggggggggg2", 2, 1)).status == DeckStatus::CommitFailed);
    ds.commitThenFail = false;
    std::size_t const committed = ds.commits.size();
    r = svc.SaveDeck(1, SaveReq("ggggggggggg1", 0, 0, "HUNTER", "Lost", Neutrals(2)), "MAGE");
    CHECK(r.status == DeckStatus::Replay && r.id == 7 && r.rev == 1 && r.state == "size,locked");
    CHECK(svc.DeleteDeck(1, DeleteReq("ggggggggggg2", 2, 1)).status == DeckStatus::Replay);
    CHECK(ds.commits.size() == committed);
    CHECK(svc.List(1, "MAGE", list) && list.decks.size() == 4 && list.decks.back().deck.name == "Lost");

    // offline account: works, leaves no cache entry
    w.collections.SetOffline(2);
    r = svc.SaveDeck(2, SaveReq("hhhhhhhhhhh1", 0, 0, "ROGUE", "Offline", Neutrals(2)), "");
    CHECK(r.status == DeckStatus::Ok && r.id == 1 && r.state == "size,owned" && !svc.IsCached(2));
    CHECK(svc.List(2, "", list) && list.decks.size() == 1 && !svc.IsCached(2) && svc.CachedCount() == 1);
    svc.Evict(1);
    CHECK(svc.CachedCount() == 0 && svc.IsOnline(1));

    // storage failures: unavailable, nothing written
    std::size_t const n = ds.commits.size();
    ds.failLoad = true;
    CHECK(!svc.List(1, "MAGE", list) && !svc.GetBook(1, book));
    CHECK(svc.SaveDeck(1, SaveReq("iiiiiiiiiii1", 0, 0, "MAGE", "X", Neutrals(2)), "MAGE").status == DeckStatus::Unavailable);
    CHECK(svc.DeleteDeck(1, DeleteReq("iiiiiiiiiii2", 1, 3)).status == DeckStatus::Unavailable);
    CHECK(svc.UnlockClass(1, "MAGE", "gm_unlock", "gm:9").status == ClassStatus::Unavailable);
    CHECK(svc.CheckForMatch(1, 1, "MAGE").status == DeckMatchCheck::Status::Unavailable);
    ds.failLoad = false;
    ds.failFind = true;
    CHECK(svc.SaveDeck(1, SaveReq("iiiiiiiiiii1", 0, 0, "MAGE", "X", Neutrals(2)), "MAGE").status == DeckStatus::Unavailable);
    CHECK(svc.DeleteDeck(1, DeleteReq("iiiiiiiiiii2", 1, 3)).status == DeckStatus::Unavailable);
    DeckSaveRequest gm = SaveReq("", 0, 0, "MAGE", "No key", Neutrals(2)); // GM saves have no op to look up
    gm.origin = kDeckOriginGmFixture;
    CHECK(svc.SaveDeck(1, gm, "").status == DeckStatus::Ok);
    ds.failFind = false;
    CHECK(ds.commits.size() == n + 1 && ds.commits.back().second.deck.origin == "gm_fixture");
    // the collection cannot be read: unavailable as well
    w.collStorage.failLoad = true;
    w.collections.Evict(1);
    CHECK(!svc.List(1, "MAGE", list));
    CHECK(svc.SaveDeck(1, SaveReq("iiiiiiiiiii1", 0, 0, "MAGE", "X", Neutrals(2)), "MAGE").status == DeckStatus::Unavailable);
    w.collStorage.failLoad = false;
    CHECK(ds.commits.size() == n + 1);

    // GM delete: no key, any revision
    DeckDeleteRequest gmDel = DeleteReq("", 1, 0);
    gmDel.anyRev = true;
    gmDel.reason = "gm_delete";
    d = svc.DeleteDeck(1, gmDel);
    CHECK(d.status == DeckStatus::Ok && d.deck.id == 1 && ds.commits.back().second.op.key.empty() &&
          ds.commits.back().second.reason == "gm_delete");
}

// The protected top-up of a starter recipe and the once-per-account starter deck award.
static void TestStarterAwards()
{
    DeckWorld w;
    DeckService& svc = w.decks;
    FakeDeckStorage& ds = w.deckStorage;
    FakeStorage& cs = w.collStorage;
    w.collections.SetCatalog(DeckCatalog());
    w.collections.SetOnline(5);
    svc.SetOnline(5);

    DeckCards recipe = Neutrals(28);
    recipe["CS2_022"] = 2;
    std::map<std::string, uint32_t> const need(recipe.begin(), recipe.end());

    // Unprotected copies already owned stay as they are; protected copies are added up to the recipe.
    CHECK(w.collections.Grant(5, "TST_n01", 3, GrantSource::Gm, ProtectionMode::Unprotected, "gm_grant", "gm:9").status ==
          MutationStatus::Ok);
    CHECK(w.collections.Grant(5, "TST_n02", 1, GrantSource::Gm, ProtectionMode::Protected, "gm_grant", "gm:9").status ==
          MutationStatus::Ok);
    Collection before;
    CHECK(w.collections.Get(5, before));
    TopUpResult t = w.collections.TopUpProtected(5, need, "starter", "gm:9");
    CHECK(t.status == MutationStatus::Ok && t.added == 29 && t.mutations.size() == 15);
    CHECK(cs.batches.size() == 1 && cs.batches[0].second.size() == 15); // one transaction
    for (Mutation const& m : t.mutations)
        CHECK(m.versionBefore == before.version && m.versionAfter == before.version + 1 && m.reason == "starter" &&
              m.source == "gm:9");
    Collection after;
    CHECK(w.collections.Get(5, after) && after.version == before.version + 1);
    CHECK(after.Get("TST_n01").protectedQty == 2 && after.Get("TST_n01").unprotectedQty == 3);
    CHECK(after.Get("TST_n02").protectedQty == 2 && after.Get("TST_n02").unprotectedQty == 0);
    CHECK(after.Get("CS2_022").protectedQty == 2);
    // Idempotent: nothing missing, nothing written.
    t = w.collections.TopUpProtected(5, need, "starter", "gm:9");
    CHECK(t.status == MutationStatus::Ok && t.added == 0 && t.mutations.empty() && cs.batches.size() == 1);
    // A card that cannot be granted rejects the whole top-up.
    std::map<std::string, uint32_t> bad = need;
    bad["TST_n01"] = 3;
    bad["NOPE_1"] = 1;
    t = w.collections.TopUpProtected(5, bad, "starter", "gm:9");
    CHECK(t.status == MutationStatus::Rejected && t.card == "NOPE_1" && cs.batches.size() == 1);
    Collection same;
    CHECK(w.collections.Get(5, same) && same.version == after.version && same.Get("TST_n01").protectedQty == 2);
    cs.failCommit = true;
    bad.erase("NOPE_1");
    t = w.collections.TopUpProtected(5, bad, "starter", "gm:9");
    CHECK(t.status == MutationStatus::CommitFailed && !w.collections.IsCached(5));
    cs.failCommit = false;

    // The deck award: once per account, origin starter, the full recipe.
    StarterAwardRequest req;
    req.cls = "MAGE";
    req.name = "Basic Mage";
    req.cards = recipe;
    req.source = "gm:9";
    StarterAwardResult a = svc.AwardStarter(5, req);
    CHECK(a.status == StarterStatus::Ok && a.deckCreated && !a.listFull && a.deckId == 1 && a.deck.origin == "starter" &&
          a.deck.name == "Basic Mage" && a.deck.cards == recipe && a.deck.rev == 1);
    CHECK(ds.starterCommits.size() == 1 && ds.starterCommits[0].second.withDeck && ds.starters[5]["MAGE"] == 1);
    DeckBook book;
    CHECK(svc.GetBook(5, book) && book.decks.count(1) && book.starters["MAGE"] == 1);
    // The award never unlocks the class.
    CHECK(book.unlocks.empty());
    a = svc.AwardStarter(5, req);
    CHECK(a.status == StarterStatus::Already && a.deckId == 1 && ds.starterCommits.size() == 1);
    // Deleting the deck does not make the award repeatable.
    DeckDeleteRequest del;
    del.id = 1;
    del.anyRev = true;
    del.source = "gm:9";
    CHECK(svc.DeleteDeck(5, del).status == DeckStatus::Ok);
    CHECK(svc.AwardStarter(5, req).status == StarterStatus::Already);

    // A full deck list (the configured cap): the award is recorded as cards only.
    svc.SetMaxSaved(2);
    DeckCards plain = Neutrals(30);
    w.Own(5, plain);
    CHECK(svc.SaveDeck(5, SaveReq("aaaaaaaaaaa1", 0, 0, "WARRIOR", "One", plain), "WARRIOR").status == DeckStatus::Ok);
    CHECK(svc.SaveDeck(5, SaveReq("aaaaaaaaaaa2", 0, 0, "WARRIOR", "Two", plain), "WARRIOR").status == DeckStatus::Ok);
    StarterAwardRequest war = req;
    war.cls = "WARRIOR";
    war.name = "Basic Warrior";
    war.cards = plain;
    a = svc.AwardStarter(5, war);
    CHECK(a.status == StarterStatus::Ok && !a.deckCreated && a.listFull && a.deckId == 0);
    CHECK(ds.starterCommits.size() == 2 && !ds.starterCommits[1].second.withDeck && ds.starters[5]["WARRIOR"] == 0);
    CHECK(svc.GetBook(5, book) && book.decks.size() == 2 && book.starters["WARRIOR"] == 0);
    CHECK(svc.AwardStarter(5, war).status == StarterStatus::Already);
    svc.SetMaxSaved(20);

    // Refusals change nothing: a bad class, a bad name, a recipe that is not 30 cards, a class card of another class.
    StarterAwardRequest r2 = req;
    r2.cls = "mage";
    CHECK(svc.AwardStarter(6, r2).status == StarterStatus::Refused);
    r2 = req;
    r2.name = "";
    CHECK(svc.AwardStarter(6, r2).status == StarterStatus::Refused && svc.AwardStarter(6, r2).why == DeckWhy::Name);
    r2 = req;
    r2.cards = Neutrals(29);
    StarterAwardResult refused = svc.AwardStarter(6, r2);
    CHECK(refused.status == StarterStatus::Refused && refused.why == DeckWhy::Size);
    r2 = req;
    r2.cls = "WARRIOR";
    CHECK(svc.AwardStarter(6, r2).status == StarterStatus::Refused); // CS2_022 is a Mage card
    CHECK(ds.starters[6].empty());

    // A failed commit: nothing recorded, the retry succeeds once.
    ds.failCommit = true;
    CHECK(svc.AwardStarter(6, req).status == StarterStatus::CommitFailed && ds.starters[6].empty());
    ds.failCommit = false;
    CHECK(svc.AwardStarter(6, req).status == StarterStatus::Ok && ds.starters[6]["MAGE"] == 1);
    // A row written elsewhere (another server thread, an earlier crash) is seen at load: Already, never two decks.
    ds.starters[7]["MAGE"] = 0;
    CHECK(svc.AwardStarter(7, req).status == StarterStatus::Already);
    ds.failLoad = true;
    CHECK(svc.AwardStarter(8, req).status == StarterStatus::Unavailable);
    ds.failLoad = false;
}

static void TestDeckClassAccessAndMatch()
{
    DeckWorld w;
    DeckService& svc = w.decks;
    FakeDeckStorage& ds = w.deckStorage;
    w.collections.SetCatalog(DeckCatalog());
    w.collections.SetOnline(5);
    svc.SetOnline(5);

    // a complete, owned Mage deck made on the account's Mage
    DeckCards cards = Neutrals(28);
    cards["CS2_022"] = 2;
    w.Own(5, cards);
    DeckSaveResult r = svc.SaveDeck(5, SaveReq("aaaaaaaaaaa1", 0, 0, "MAGE", "Shared", cards), "MAGE");
    CHECK(r.status == DeckStatus::Ok && r.state == "ok");
    // owning cards or a deck of a class never unlocks it: the Warrior alt cannot play it
    DeckList list;
    CHECK(svc.List(5, "WARRIOR", list) && list.start == "WARRIOR" && list.decks[0].state == "locked");
    DeckMatchCheck m = svc.CheckForMatch(5, 1, "WARRIOR");
    CHECK(m.status == DeckMatchCheck::Status::Ok && m.state.locked && !m.Ready() && m.Text() == "locked,engine");

    // development unlock (account-wide): now both characters may use it
    ClassChangeResult u = svc.UnlockClass(5, "MAGE", "gm_unlock", "gm:9");
    CHECK(u.status == ClassStatus::Ok && u.cls == "MAGE");
    CHECK(ds.classCommits.size() == 1 && ds.classCommits[0].second.unlock && ds.classCommits[0].second.reason == "gm_unlock" &&
          ds.classCommits[0].second.source == "gm:9" && ds.unlocks[5]["MAGE"] == "gm:9");
    CHECK(svc.List(5, "WARRIOR", list) && list.unlocked == std::vector<std::string>{ "MAGE" } && list.decks[0].state == "ok");
    CHECK(svc.List(5, "MAGE", list) && list.decks[0].state == "ok");
    CHECK(svc.UnlockClass(5, "MAGE", "gm_unlock", "gm:9").status == ClassStatus::Already);
    CHECK(svc.UnlockClass(5, "mage", "gm_unlock", "gm:9").status == ClassStatus::BadClass);
    CHECK(svc.UnlockClass(5, "NEUTRAL", "gm_unlock", "gm:9").status == ClassStatus::BadClass);
    CHECK(svc.UnlockClass(5, "WARRIOR", "gm_unlock", "gm:9").status == ClassStatus::Ok);
    CHECK(svc.List(5, "", list) && (list.unlocked == std::vector<std::string>{ "MAGE", "WARRIOR" }));
    CHECK(ds.classCommits.size() == 2);

    // nothing is match-ready until the engine registers its support predicate
    m = svc.CheckForMatch(5, 1, "WARRIOR");
    CHECK(m.status == DeckMatchCheck::Status::Ok && m.state.Playable() && m.engine && !m.Ready() && m.Text() == "engine");
    CHECK(m.unsupportedCard.empty() && !svc.HasEngineSupport());
    svc.SetEngineSupport([](std::string const&) { return true; });
    CHECK(svc.HasEngineSupport());
    m = svc.CheckForMatch(5, 1, "WARRIOR");
    CHECK(m.Ready() && m.Text() == "ready");
    svc.SetEngineSupport([](std::string const& id) { return id != "CS2_022"; });
    m = svc.CheckForMatch(5, 1, "MAGE");
    CHECK(!m.Ready() && m.engine && m.unsupportedCard == "CS2_022" && m.Text() == "engine");
    svc.SetEngineSupport(nullptr);
    CHECK(!svc.HasEngineSupport() && svc.CheckForMatch(5, 1, "MAGE").Text() == "engine");
    svc.SetEngineSupport([](std::string const&) { return true; });

    // lock again: the Warrior loses access, the Mage keeps its starting class
    CHECK(svc.LockClass(5, "MAGE", "gm_lock", "gm:9").status == ClassStatus::Ok);
    CHECK(svc.LockClass(5, "MAGE", "gm_lock", "gm:9").status == ClassStatus::NotUnlocked);
    CHECK(svc.LockClass(5, "DRUID", "gm_lock", "gm:9").status == ClassStatus::NotUnlocked);
    CHECK(svc.LockClass(5, "x", "gm_lock", "gm:9").status == ClassStatus::BadClass);
    CHECK(!ds.classCommits.back().second.unlock && ds.classCommits.back().second.reason == "gm_lock" &&
          ds.unlocks[5].count("MAGE") == 0);
    CHECK(svc.CheckForMatch(5, 1, "WARRIOR").Text() == "locked");
    CHECK(svc.CheckForMatch(5, 1, "MAGE").Ready());
    CHECK(svc.CheckForMatch(5, 1, "").Text() == "locked"); // no starting class: only unlocks count

    // revalidation at match start, not only on save: ownership lost since the save
    CHECK(w.collections.Remove(5, "CS2_022", 1, false, "gm_remove", "gm:9").status == MutationStatus::Ok);
    m = svc.CheckForMatch(5, 1, "MAGE");
    CHECK(!m.Ready() && m.state.owned && m.Text() == "owned");
    // an incomplete draft is never match-ready
    CHECK(svc.SaveDeck(5, SaveReq("aaaaaaaaaaa2", 0, 0, "MAGE", "Draft", Neutrals(10)), "MAGE").status == DeckStatus::Ok);
    CHECK(svc.CheckForMatch(5, 2, "MAGE").Text() == "size");
    CHECK(svc.CheckForMatch(5, 2, "WARRIOR").Text() == "size,locked");
    // card data changed under a stored deck
    ds.rows[5][2].deck.cards["CS2_108"] = 2;
    svc.Evict(5);
    CHECK(svc.CheckForMatch(5, 2, "MAGE").Text() == "size,owned,invalid");
    CHECK(svc.CheckForMatch(5, 9, "MAGE").status == DeckMatchCheck::Status::Missing);
    CHECK(svc.CheckForMatch(5, 9, "MAGE").Text() == "missing");

    // failed / lost class commits
    ds.failCommit = true;
    CHECK(svc.UnlockClass(5, "PRIEST", "gm_unlock", "gm:9").status == ClassStatus::CommitFailed && !svc.IsCached(5));
    ds.failCommit = false;
    CHECK(ds.unlocks[5].count("PRIEST") == 0);
    ds.commitThenFail = true;
    CHECK(svc.UnlockClass(5, "PRIEST", "gm_unlock", "gm:9").status == ClassStatus::CommitFailed);
    ds.commitThenFail = false;
    CHECK(svc.UnlockClass(5, "PRIEST", "gm_unlock", "gm:9").status == ClassStatus::Already); // read again
    CHECK(std::string(ClassStatusText(ClassStatus::Already)).size() > 1);
    CHECK(std::string(DeckStatusText(DeckStatus::Replay)).size() > 1);
    CHECK(std::string(DeckActionName(DeckAction::Delete)) == "delete");
}

static void TestSessionDecks()
{
    FakeDecks fake;
    fake.list.cap = 29;
    fake.list.start = "MAGE";
    fake.list.unlocked = { "WARRIOR" };
    DeckListEntry e;
    e.deck = MakeDeck(1, 2, "MAGE", "Fire", Neutrals(3));
    e.state = "size,owned";
    fake.list.decks.push_back(e);

    Client c;
    c.collections = &fake;
    c.settings.ratePerSecond = 1000;
    c.settings.rateBurst = 1000;
    CHECK(FirstErr(c.Send1("DECK_LIST", "l0", "")) == "NOT_READY");
    CHECK(FirstErr(c.Send1("DECK_SAVE", "s0", "key=abcdef123456;id=0;rev=0;class=MAGE;name=x;cards=")) == "NOT_READY");
    CHECK(fake.saves.empty());
    c.Hello();

    // DECK_LIST -> DECKS (payload ignored)
    auto rep = Decode(c.Send1("DECK_LIST", "l1", "cap=99"));
    CHECK(rep.size() == 1 && rep[0].op == "DECKS" && rep[0].reqId == "l1");
    CHECK(rep[0].payload == "cap=29;n=1;start=MAGE;unlocked=WARRIOR;d1=1.2.MAGE.player;m1=Fire;c1=TST_n01.2,TST_n02.1;s1=size,owned");
    fake.failList = true;
    rep = Decode(c.Send1("DECK_LIST", "l2", ""));
    CHECK(rep.size() == 1 && rep[0].reqId == "l2" && ErrCode(rep[0]) == "UNAVAILABLE");
    fake.failList = false;

    // malformed DECK_SAVE: checked before the handler is asked
    char const* const badSaves[] = {
        "",
        "id=0;rev=0;class=MAGE;name=x;cards=",                               // no key
        "key=short;id=0;rev=0;class=MAGE;name=x;cards=",                     // bad key
        "key=ABCDEF123456;id=0;rev=0;class=MAGE;name=x;cards=",
        "key=abcdef123456;rev=0;class=MAGE;name=x;cards=",                   // no id
        "key=abcdef123456;id=x;rev=0;class=MAGE;name=x;cards=",
        "key=abcdef123456;id=-1;rev=0;class=MAGE;name=x;cards=",
        "key=abcdef123456;id=1234567890;rev=0;class=MAGE;name=x;cards=",     // more than 9 digits
        "key=abcdef123456;id=0;class=MAGE;name=x;cards=",                    // no rev
        "key=abcdef123456;id=0;rev=1;class=MAGE;name=x;cards=",              // a new deck with a base rev
        "key=abcdef123456;id=0;rev=0;name=x;cards=",                         // no class
        "key=abcdef123456;id=0;rev=0;class=MAGE;cards=",                     // no name
        "key=abcdef123456;id=0;rev=0;class=MAGE;name=x",                     // no cards
        "key=abcdef123456;id=0;rev=0;class=MAGE;name=x;cards=CS2_022",       // unparsable cards
        "key=abcdef123456;id=0;rev=0;class=MAGE;name=x;cards=A.1,A.2",
        "key=abcdef123456;id=0;rev=0;class=MAGE;name=x;cards=A.1;key=abcdef123456", // duplicate key
        "key=abcdef123456;id=0;rev=0;class=MAGE;name=x~;cards=",             // bad value escape
    };
    for (char const* p : badSaves)
        CHECK(FirstErr(c.Send1("DECK_SAVE", "s1", p)) == "MALFORMED");
    CHECK(fake.saves.empty());

    // saved: the request reaches the handler decoded
    fake.save.status = DeckStatus::Ok;
    fake.save.id = 3;
    fake.save.rev = 1;
    fake.save.state = "size,owned";
    rep = Decode(c.Send1("DECK_SAVE", "s2",
        "key=abcdef123456;id=0;rev=0;class=MAGE;name= My~sDeck ;cards=TST_n02.1,CS2_022.2;extra=1"));
    CHECK(rep.size() == 1 && rep[0].op == "DECK_SAVED" && rep[0].reqId == "s2");
    CHECK(rep[0].payload == "key=abcdef123456;id=3;rev=1;replay=0;state=size,owned");
    CHECK(fake.saves.size() == 1);
    if (fake.saves.size() == 1)
    {
        DeckSaveRequest const& s = fake.saves[0];
        CHECK(s.key == "abcdef123456" && s.id == 0 && s.baseRev == 0 && s.deckClass == "MAGE" && s.name == " My;Deck ");
        CHECK(s.cards.size() == 2 && s.cards.at("CS2_022") == 2 && s.cards.at("TST_n02") == 1);
        CHECK(s.origin == "player" && !s.bypassCap);
    }
    // a rule refusal is the handler's (and the service's) business: class, name, counts pass through
    CHECK(Decode(c.Send1("DECK_SAVE", "s3", "key=abcdef123456;id=7;rev=4;class=;name=;cards=X.0")).size() == 1);
    CHECK(fake.saves.size() == 2 && fake.saves[1].id == 7 && fake.saves[1].baseRev == 4 && fake.saves[1].cards.at("X") == 0);

    fake.save.status = DeckStatus::Replay;
    fake.save.state = "gone";
    rep = Decode(c.Send1("DECK_SAVE", "s4", "key=abcdef123456;id=0;rev=0;class=MAGE;name=x;cards="));
    CHECK(rep.size() == 1 && rep[0].payload == "key=abcdef123456;id=3;rev=1;replay=1;state=gone");

    // refused: ERR REFUSED with why=
    DeckWhy const whys[] = { DeckWhy::Class, DeckWhy::Name, DeckWhy::Card, DeckWhy::CardClass, DeckWhy::Copies,
                             DeckWhy::Size, DeckWhy::Cap, DeckWhy::Missing, DeckWhy::Conflict, DeckWhy::Key };
    for (DeckWhy why : whys)
    {
        fake.save = DeckSaveResult();
        fake.save.status = DeckStatus::Refused;
        fake.save.why = why;
        rep = Decode(c.Send1("DECK_SAVE", "s5", "key=abcdef123456;id=1;rev=1;class=MAGE;name=x;cards="));
        CHECK(rep.size() == 1 && rep[0].reqId == "s5" && ErrCode(rep[0]) == "REFUSED");
        CHECK(rep.size() == 1 && Kv(rep[0])["why"] == DeckWhyCode(why) && !Kv(rep[0])["msg"].empty());
    }
    struct Case { DeckStatus status; char const* code; };
    Case const cases[] = {
        { DeckStatus::Unavailable, "UNAVAILABLE" }, { DeckStatus::CommitFailed, "UNAVAILABLE" },
        { DeckStatus::BadKey, "MALFORMED" },
    };
    for (Case const& k : cases)
    {
        fake.save.status = k.status;
        rep = Decode(c.Send1("DECK_SAVE", "s6", "key=abcdef123456;id=1;rev=1;class=MAGE;name=x;cards="));
        CHECK(rep.size() == 1 && rep[0].reqId == "s6" && ErrCode(rep[0]) == k.code && Kv(rep[0]).count("why") == 0);
    }

    // DECK_DELETE
    char const* const badDeletes[] = {
        "", "id=1;rev=1", "key=abcdef123456;rev=1", "key=abcdef123456;id=0;rev=0", "key=abcdef123456;id=1",
        "key=abcdef123456;id=1;rev=x", "key=short;id=1;rev=1",
    };
    for (char const* p : badDeletes)
        CHECK(FirstErr(c.Send1("DECK_DELETE", "x1", p)) == "MALFORMED");
    CHECK(fake.deletes.empty());
    fake.del.status = DeckStatus::Ok;
    rep = Decode(c.Send1("DECK_DELETE", "x2", "key=abcdef654321;id=3;rev=2"));
    CHECK(rep.size() == 1 && rep[0].op == "DECK_DELETED" && rep[0].reqId == "x2" &&
          rep[0].payload == "key=abcdef654321;id=3;replay=0");
    CHECK(fake.deletes.size() == 1 && fake.deletes[0].key == "abcdef654321" && fake.deletes[0].id == 3 &&
          fake.deletes[0].baseRev == 2 && !fake.deletes[0].anyRev);
    fake.del.status = DeckStatus::Replay;
    rep = Decode(c.Send1("DECK_DELETE", "x3", "key=abcdef654321;id=3;rev=2"));
    CHECK(rep.size() == 1 && rep[0].payload == "key=abcdef654321;id=3;replay=1");
    fake.del.status = DeckStatus::Refused;
    fake.del.why = DeckWhy::Missing;
    rep = Decode(c.Send1("DECK_DELETE", "x4", "key=abcdef654321;id=3;rev=2"));
    CHECK(rep.size() == 1 && ErrCode(rep[0]) == "REFUSED" && Kv(rep[0])["why"] == "missing");
    for (Case const& k : cases)
    {
        fake.del.status = k.status;
        rep = Decode(c.Send1("DECK_DELETE", "x5", "key=abcdef654321;id=3;rev=2"));
        CHECK(rep.size() == 1 && rep[0].reqId == "x5" && ErrCode(rep[0]) == k.code);
    }

    // a handler without deck support (the CollectionHandler defaults) and no handler: unavailable
    FakeCollections plain;
    c.collections = &plain;
    CHECK(FirstErr(c.Send1("DECK_LIST", "y1", "")) == "UNAVAILABLE");
    CHECK(FirstErr(c.Send1("DECK_SAVE", "y2", "key=abcdef123456;id=0;rev=0;class=MAGE;name=x;cards=")) == "UNAVAILABLE");
    CHECK(FirstErr(c.Send1("DECK_DELETE", "y3", "key=abcdef123456;id=1;rev=1")) == "UNAVAILABLE");
    c.collections = nullptr;
    CHECK(FirstErr(c.Send1("DECK_LIST", "y4", "")) == "UNAVAILABLE");
    CHECK(FirstErr(c.Send1("DECK_SAVE", "y5", "key=abcdef123456;id=0;rev=0;class=MAGE;name=x;cards=")) == "UNAVAILABLE");
    CHECK(FirstErr(c.Send1("DECK_DELETE", "y6", "key=abcdef123456;id=1;rev=1")) == "UNAVAILABLE");
    // other DECK_* stay reserved
    CHECK(FirstErr(c.Send1("DECK_RENAME", "y7", "")) == "NOT_IMPLEMENTED");
    CHECK(FirstErr(c.Send1("DECK_GET", "y8", "")) == "NOT_IMPLEMENTED");

    // push
    std::vector<std::string> push;
    AppendDeckChanged(push, "class");
    rep = Decode(push);
    CHECK(push.size() == 1 && rep.size() == 1 && rep[0].op == "DECK_CHANGED" && rep[0].reqId == "0" &&
          rep[0].payload == "why=class");

    // the error builder: why only when given
    std::vector<std::string> err;
    AppendError(err, "e1", ErrorCode::Refused, "m", "cap");
    AppendError(err, "e2", ErrorCode::Refused, "m");
    rep = Decode(err);
    CHECK(rep.size() == 2 && rep[0].payload == "code=REFUSED;msg=m;why=cap" && rep[1].payload == "code=REFUSED;msg=m");

    // a long save request (30 distinct cards, a 64-byte name) arrives in several parts
    DeckCards many;
    for (int i = 0; i < 30; ++i)
    {
        char id[16];
        std::snprintf(id, sizeof(id), "NEW1_%03d", i);
        many[id] = 1;
    }
    std::string name;
    for (int i = 0; i < 16; ++i)
        name += "\xe2\x82\xac";
    for (int i = 0; i < 8; ++i)
        name += "\xc3\xa4";
    KvList kv;
    kv.push_back(std::make_pair("key", std::string("abcdef1234567890")));
    kv.push_back(std::make_pair("id", std::string("999999999")));
    kv.push_back(std::make_pair("rev", std::string("999999999")));
    kv.push_back(std::make_pair("class", std::string("WARLOCK")));
    kv.push_back(std::make_pair("name", name));
    kv.push_back(std::make_pair("cards", EncodeDeckCards(many)));
    std::vector<std::string> frames;
    CHECK(ChunkPayload("DECK_SAVE", "big", KvEncode(kv), frames));
    CHECK(frames.size() > 1 && frames.size() <= c.settings.maxInboundParts);
    fake.save.status = DeckStatus::Ok;
    c.collections = &fake;
    std::vector<std::string> out;
    for (std::string const& f : frames)
        for (std::string const& o : c.SendFrame(f))
            out.push_back(o);
    rep = Decode(out);
    CHECK(rep.size() == 1 && rep[0].op == "DECK_SAVED");
    CHECK(!fake.saves.empty() && fake.saves.back().name == name && fake.saves.back().cards == many &&
          fake.saves.back().id == 999999999u);
}

static void TestDeckListedCeiling()
{
    // bypassCap (starter awards) passes the configured cap but never
    // kMaxListedDecks, the bound of the single DECKS reply.
    static_assert(kMaxSavedDecksLimit == 20 && kDefaultMaxSavedDecks == 20 && kMaxListedDecks == 29, "deck caps");
    DeckWorld w;
    DeckService& svc = w.decks;
    w.collections.SetCatalog(DeckCatalog());
    w.collections.SetOnline(1);
    svc.SetOnline(1);
    CHECK(svc.MaxSaved() == 20);
    for (uint32_t i = 0; i < kMaxListedDecks; ++i)
    {
        DeckSaveRequest starter = SaveReq("", 0, 0, "MAGE", "Starter", Neutrals(2));
        starter.bypassCap = i >= kMaxSavedDecksLimit;
        CHECK(svc.SaveDeck(1, starter, "MAGE").status == DeckStatus::Ok);
    }
    DeckSaveRequest one = SaveReq("", 0, 0, "MAGE", "One more", Neutrals(2));
    one.bypassCap = true;
    DeckSaveResult const r = svc.SaveDeck(1, one, "MAGE");
    CHECK(r.status == DeckStatus::Refused && r.why == DeckWhy::Cap);
    DeckList list;
    CHECK(svc.List(1, "MAGE", list) && list.decks.size() == kMaxListedDecks);
}

static void TestDecksWorstCaseFits()
{
    // The largest DECKS reply: kMaxListedDecks decks of 30 distinct 8-character
    // ids, 24-character names of 64 non-ASCII bytes (16 x 3 + 8 x 2, each byte
    // ~xHH on the wire), 10-digit ids and revisions, the longest class, origin
    // and state, every class unlocked.
    std::string name;
    for (int i = 0; i < 16; ++i)
        name += "\xe2\x82\xac";
    for (int i = 0; i < 8; ++i)
        name += "\xc3\xa4";
    std::string normalized;
    CHECK(NormalizeDeckName(name, normalized) && normalized.size() == kMaxDeckNameBytes);

    DeckList list;
    list.cap = kMaxListedDecks;
    list.start = "WARLOCK";
    list.unlocked = DeckClasses();
    for (uint32_t d = 0; d < kMaxListedDecks; ++d)
    {
        DeckListEntry e;
        e.deck.id = 4000000000u + d;
        e.deck.rev = 4000000000u + d;
        e.deck.deckClass = "WARLOCK";
        e.deck.origin = kDeckOriginGmFixture;
        e.deck.name = name;
        for (int i = 0; i < 30; ++i)
        {
            char id[16];
            std::snprintf(id, sizeof(id), "NEW1_%03d", i);
            e.deck.cards[id] = 1;
        }
        e.state = "size,owned,invalid,locked";
        list.decks.push_back(e);
    }
    std::string const payload = EncodeDecksPayload(list);
    std::vector<std::string> frames;
    bool const fits = ChunkPayload("DECKS", "zzzzzzzz", payload, frames);
    std::printf("    worst-case DECKS (%u decks): %u decoded bytes, %u frames\n", kMaxListedDecks,
                static_cast<unsigned>(payload.size()), static_cast<unsigned>(frames.size()));
    CHECK(fits && frames.size() <= kMaxPartsHardLimit);
    CHECK(payload.size() <= 32768); // the addon's reply bound
    // the bound is tight: one deck more no longer fits
    DeckList more = list;
    more.decks.push_back(list.decks.back());
    more.cap = kMaxListedDecks + 1;
    std::vector<std::string> tooMany;
    CHECK(!ChunkPayload("DECKS", "zzzzzzzz", EncodeDecksPayload(more), tooMany));
    // and through the session: one DECKS reply, no ERR TOO_LARGE
    FakeDecks fake;
    fake.list = list;
    Client c;
    c.collections = &fake;
    c.Hello();
    auto rep = Decode(c.Send1("DECK_LIST", "zzzzzzzz", ""));
    CHECK(rep.size() == 1 && rep[0].op == "DECKS" && rep[0].payload == payload);
}

// Writes a client<->server transcript used by tests/lua_tests.lua: the exact
// frames the C++ chunker produces for requests, and the server's replies.
static void EmitTranscript()
{
    Client c;
    c.settings.debug = true;
    c.security = 2;
    c.settings.ratePerSecond = 1000;
    c.settings.rateBurst = 1000;
    std::printf("# CHUNK <op> <reqId> <hex payload> <frame>  : request frames built by the C++ chunker\n");
    std::printf("# SERVER <frame>                            : reply frames produced by the C++ session\n");
    auto request = [&](std::string const& op, std::string const& reqId, std::string const& payload)
    {
        std::vector<std::string> frames;
        bool const ok = ChunkPayload(op, reqId, payload, frames);
        CHECK(ok);
        for (std::string const& f : frames)
        {
            std::printf("CHUNK\t%s\t%s\t%s\t%s\n", op.c_str(), reqId.c_str(), Hex(payload).c_str(), f.c_str());
            c.now += 150;
            for (std::string const& r : c.SendFrame(f))
                std::printf("SERVER\t%s\n", r.c_str());
        }
    };
    request("HELLO", "h1", "protocol=1;addon=0.1.0;dataset=-");
    request("PING", "p1", "a;b=c|d:e_f\nLFT~");
    request("ECHO", "e3", "n=3");
    request("ECHO", "e12", "n=12");
    request("ECHO", "in1", "data=" + MakePattern(700));
    request("ECHO", "bad1", "n=99");
    request("BOGUS", "u1", "");
    request("GAME_START", "g1", "x");
    // Collection (real card ids), a COLL_CHANGED push and an ERR UNAVAILABLE
    FakeCollections fake;
    fake.collection.version = 7;
    fake.collection.Set("EX1_116", Count(0, 1));
    fake.collection.Set("CS2_022", Count(1, 1));
    fake.collection.Set("EX1_062", Count(1, 0));
    c.collections = &fake;
    request("COLL_GET", "c1", "");
    std::vector<std::string> changed;
    AppendCollChanged(changed, 8);
    for (std::string const& r : changed)
        std::printf("SERVER\t%s\n", r.c_str());
    fake.collection = Collection();
    fake.collection.version = 8;
    request("COLL_GET", "c2", "");
    fake.fail = true;
    request("COLL_GET", "c3", "");
    // Packs (PACKS, PACK_OPENED + COLL_CHANGED, a PACK_CHANGED push, ERR NO_STOCK)
    FakePacks packs;
    packs.view.state.stock = 2;
    packs.view.state.opened = 7;
    packs.view.recent.push_back(MakeOpening(7, "k7aaaaaaaaaa", "EX1_116,CS2_022,CS2_022,EX1_011,CS2_189", "10000", 9));
    c.collections = &packs;
    request("PACK_GET", "k1", "");
    packs.open.status = PackStatus::Ok;
    packs.open.opening = MakeOpening(8, "abcdef123456", "CS2_022,EX1_116,CS2_022,CS2_189,EX1_011", "11000", 9);
    packs.open.stockNow = 1;
    request("PACK_OPEN", "o1", "key=abcdef123456");
    std::vector<std::string> stock;
    AppendPackChanged(stock, 5);
    for (std::string const& r : stock)
        std::printf("SERVER\t%s\n", r.c_str());
    packs.open = PackOpenResult();
    packs.open.status = PackStatus::NoStock;
    request("PACK_OPEN", "o2", "key=abcdef654321");
    // Decks (DECKS, DECK_SAVED, ERR REFUSED why=, DECK_DELETED, DECK_CHANGED pushes)
    FakeDecks decks;
    decks.list.cap = kDefaultMaxSavedDecks;
    decks.list.start = "MAGE";
    decks.list.unlocked = { "WARRIOR" };
    DeckFixture fixture;
    CHECK(GetDeckFixture("mage", fixture));
    DeckListEntry first;
    first.deck = MakeDeck(1, 1, "MAGE", fixture.name, fixture.cards, kDeckOriginGmFixture);
    first.state = "ok";
    DeckListEntry second;
    DeckCards priest;
    priest["CS2_168"] = 2;
    priest["CS2_172"] = 1;
    second.deck = MakeDeck(4, 3, "PRIEST", "Kr\xc3\xb6te; =1", priest);
    second.state = "size,owned,locked";
    decks.list.decks = { first, second };
    c.collections = &decks;
    request("DECK_LIST", "d1", "");
    KvList save;
    save.push_back(std::make_pair("key", std::string("abcdef123456")));
    save.push_back(std::make_pair("id", std::string("0")));
    save.push_back(std::make_pair("rev", std::string("0")));
    save.push_back(std::make_pair("class", std::string("MAGE")));
    save.push_back(std::make_pair("name", std::string("Frost; Fire")));
    save.push_back(std::make_pair("cards", std::string("CS2_029.2,EX1_277.1")));
    decks.save.status = DeckStatus::Ok;
    decks.save.id = 5;
    decks.save.rev = 1;
    decks.save.state = "size,owned";
    request("DECK_SAVE", "s1", KvEncode(save));
    decks.save = DeckSaveResult();
    decks.save.status = DeckStatus::Refused;
    decks.save.why = DeckWhy::Conflict;
    request("DECK_SAVE", "s2", "key=abcdef654321;id=1;rev=0;class=MAGE;name=Dev Mage;cards=");
    decks.del.status = DeckStatus::Ok;
    request("DECK_DELETE", "x1", "key=abcdef777777;id=4;rev=3");
    std::vector<std::string> deckPushes;
    AppendDeckChanged(deckPushes, "gm");
    AppendDeckChanged(deckPushes, "class");
    for (std::string const& r : deckPushes)
        std::printf("SERVER\t%s\n", r.c_str());
    c.collections = nullptr;
    request("HELLO", "v2", "protocol=2;addon=9");
    std::vector<std::string> versionErr = c.SendFrame("v2\tPING\tq7\t1\t1\tx");
    CHECK(versionErr.size() == 1);
    for (std::string const& r : versionErr)
        std::printf("SERVER\t%s\n", r.c_str());
}

// ---------------------------------------------------------------------------
// The GAME wire codec, the match service, the session ops.

namespace
{
    namespace HM = Hs::Match;

    // The canonical GAME example (the base values plus the later keys; shared with tests/lua_tests.lua
    // and the addon). New in dif, a frozen stealthy minion of the viewer (flags sf), an enraged
    // Windfury enemy minion (we), a Choose One card in hand (mo and the p24:1 / p24:2 entries), a secret of
    // the viewer (ms) and two of the opponent (os), the cards played this turn (mx) and the opponent's
    // overload (ox), no pending pick (pk empty, opk 0).
    char const* const kCanonicalGame =
        "mid=7;seq=12;ph=play;turn=3;act=m;res=-;why=-;mn=Jonas;on=Innkeeper Allison;dif=normal;"
        "mh=1.HERO_08.27.30.0.0.-;mp=2.CS2_034.2.0;mw=;mm=2.2;mk=5.23.0;"
        "mb=40.CS2_172.3.2.2.a,41.CS2_179.3.5.5.t,42.EX1_010.2.1.1.sf;"
        "mc=12.CS2_029.4.0.0,15.CS2_168.1.2.1,18.CS2_025.2.0.0,21.GAME_005.0.0.0,24.EX1_154.2.0.0;"
        "mo=24.EX1_154a/EX1_154b;ms=26.EX1_289;mx=1.0.0;"
        "oh=3.HERO_01.30.30.2.0.-;op=4.CS2_102.2.1;ow=33.CS2_106.3.1;om=0.2;ok=5.23.0;"
        "ob=44.CS2_124.3.1.1.c,45.EX1_412.4.2.3.we;os=36,37;ox=0.1.2;mul=;pk=;opk=0;tmr=0;"
        "lg=p15.0,p18.0,p21.0,p24:1.40/41/42/44/45,p24:2.40/41/42/44/45,a40.3/44/45,h2.1/3/40/41/42/44/45;"
        "evs=31;ev=turn.m.0.0.3.-,draw.m.0.18.0.CS2_025";

    HM::MinionView MakeMinionView(HM::EntityId id, char const* card, int atk, int hp, int maxHp)
    {
        HM::MinionView m;
        m.id = id;
        m.cardId = card;
        m.attack = atk;
        m.health = hp;
        m.maxHealth = maxHp;
        return m;
    }

    HM::HandCardView MakeHandView(HM::EntityId id, char const* card, int cost, int atk, int hp)
    {
        HM::HandCardView h;
        h.id = id;
        h.cardId = card;
        h.cost = cost;
        h.attack = atk;
        h.health = hp;
        return h;
    }

    HM::Action LegalEntry(HM::ActionType t, HM::EntityId s, HM::EntityId target, int choice = 0)
    {
        HM::Action a;
        a.type = t;
        a.source = s;
        a.target = target;
        a.choice = choice;
        return a;
    }

    HM::Event MakeEvent(uint32_t seq, HM::EventType t, int side, HM::EntityId s, HM::EntityId target, int amount,
                        char const* card)
    {
        HM::Event e;
        e.seq = seq;
        e.type = t;
        e.side = side;
        e.source = s;
        e.target = target;
        e.amount = amount;
        e.cardId = card;
        return e;
    }

    HM::MatchView CanonicalView()
    {
        HM::MatchView v;
        v.viewer = 0;
        v.seq = 12;
        v.phase = HM::Phase::Play;
        v.turn = 3;
        v.activeSide = 0;
        HM::SideView& me = v.self;
        me.name = "Jonas";
        me.hero.id = 1;
        me.hero.cardId = "HERO_08";
        me.hero.health = 27;
        me.hero.powerId = 2;
        me.hero.powerCardId = "CS2_034";
        me.hero.powerCost = 2;
        me.hero.mana = 2;
        me.hero.maxMana = 2;
        me.handCount = 5;
        me.deckCount = 23;
        me.cardsPlayed = 1;
        me.board.push_back(MakeMinionView(40, "CS2_172", 3, 2, 2));
        me.board.back().canAttack = true;
        me.board.push_back(MakeMinionView(41, "CS2_179", 3, 5, 5));
        me.board.back().taunt = true;
        me.board.push_back(MakeMinionView(42, "EX1_010", 2, 1, 1));
        me.board.back().stealth = true;
        me.board.back().frozen = true;
        me.hand.push_back(MakeHandView(12, "CS2_029", 4, 0, 0));
        me.hand.push_back(MakeHandView(15, "CS2_168", 1, 2, 1));
        me.hand.push_back(MakeHandView(18, "CS2_025", 2, 0, 0));
        me.hand.push_back(MakeHandView(21, "GAME_005", 0, 0, 0));
        me.hand.push_back(MakeHandView(24, "EX1_154", 2, 0, 0));
        me.hand.back().options = { "EX1_154a", "EX1_154b" };
        HM::SecretView ownSecret;
        ownSecret.id = 26;
        ownSecret.cardId = "EX1_289";
        me.secrets.push_back(ownSecret);
        HM::SideView& op = v.other;
        op.hero.id = 3;
        op.hero.cardId = "HERO_01";
        op.hero.armor = 2;
        op.hero.powerId = 4;
        op.hero.powerCardId = "CS2_102";
        op.hero.powerUsed = true;
        op.hero.weaponId = 33;
        op.hero.weaponCardId = "CS2_106";
        op.hero.weaponAttack = 3;
        op.hero.weaponDurability = 1;
        op.hero.mana = 0;
        op.hero.maxMana = 2;
        op.handCount = 5;
        op.deckCount = 23;
        op.overloadLocked = 1;
        op.overloadOwed = 2;
        op.board.push_back(MakeMinionView(44, "CS2_124", 3, 1, 1));
        op.board.back().charge = true;
        op.board.push_back(MakeMinionView(45, "EX1_412", 4, 2, 3));
        op.board.back().windfury = true;
        op.board.back().enraged = true;
        for (HM::EntityId id : { 36u, 37u })
        {
            HM::SecretView hidden;
            hidden.id = id; // the other side's secrets carry no card id
            op.secrets.push_back(hidden);
        }
        // Scrambled on purpose: the encoder orders the entries and the targets.
        v.legal.push_back(LegalEntry(HM::ActionType::HeroPower, 2, 44));
        v.legal.push_back(LegalEntry(HM::ActionType::Attack, 40, 44));
        v.legal.push_back(LegalEntry(HM::ActionType::Play, 21, 0));
        v.legal.push_back(LegalEntry(HM::ActionType::HeroPower, 2, 1));
        v.legal.push_back(LegalEntry(HM::ActionType::Play, 15, 0));
        v.legal.push_back(LegalEntry(HM::ActionType::Attack, 40, 3));
        v.legal.push_back(LegalEntry(HM::ActionType::HeroPower, 2, 40));
        for (HM::EntityId t : { 45u, 40u, 44u, 41u, 42u })
        {
            v.legal.push_back(LegalEntry(HM::ActionType::Play, 24, t, 2));
            v.legal.push_back(LegalEntry(HM::ActionType::Play, 24, t, 1));
        }
        v.legal.push_back(LegalEntry(HM::ActionType::Attack, 40, 45));
        v.legal.push_back(LegalEntry(HM::ActionType::Play, 18, 0));
        v.legal.push_back(LegalEntry(HM::ActionType::HeroPower, 2, 3));
        v.legal.push_back(LegalEntry(HM::ActionType::HeroPower, 2, 41));
        v.legal.push_back(LegalEntry(HM::ActionType::HeroPower, 2, 42));
        v.legal.push_back(LegalEntry(HM::ActionType::HeroPower, 2, 45));
        v.events.push_back(MakeEvent(31, HM::EventType::TurnStart, 0, 0, 0, 3, ""));
        v.events.push_back(MakeEvent(32, HM::EventType::Draw, 0, 0, 18, 0, "CS2_025"));
        return v;
    }

    GameHeader CanonicalHeader()
    {
        GameHeader h;
        h.matchId = 7;
        h.myName = "Jonas";
        h.opponentName = "Innkeeper Allison";
        h.difficulty = "normal";
        return h;
    }

    std::map<std::string, std::string> KvOf(std::string const& payload)
    {
        std::map<std::string, std::string> m;
        CHECK(KvDecode(payload, m));
        return m;
    }

    std::shared_ptr<HM::CardDb const> TestCardDb()
    {
        static std::shared_ptr<HM::CardDb const> const db = std::make_shared<HM::CardDb>(MatchTestCards::BuildDb());
        return db;
    }

    MatchStartParams FixtureParams(uint32_t guid, uint32_t seed)
    {
        MatchStartParams p;
        p.guid = guid;
        p.seed = seed;
        p.humanName = "Jonas";
        p.opponentName = "Innkeeper Allison";
        DeckFixture mage;
        DeckFixture warrior;
        CHECK(GetDeckFixture("mage", mage) && GetDeckFixture("warrior", warrior));
        p.humanClass = mage.deckClass;
        p.opponentClass = warrior.deckClass;
        CHECK(ExpandDeck(mage.cards, p.humanDeck) && p.humanDeck.size() == 30);
        CHECK(ExpandDeck(warrior.cards, p.opponentDeck) && p.opponentDeck.size() == 30);
        return p;
    }

    GameActRequest Req(std::string const& payload)
    {
        GameActRequest r;
        CHECK(DecodeGameAct(payload, r));
        return r;
    }

    uint32_t U(std::map<std::string, std::string> const& kv, char const* key)
    {
        auto const it = kv.find(key);
        return it == kv.end() ? 0u : static_cast<uint32_t>(std::strtoul(it->second.c_str(), nullptr, 10));
    }

    // The first "."-separated field of a value ("3.HERO_01.30..." -> 3).
    uint32_t FirstId(std::string const& v)
    {
        return static_cast<uint32_t>(std::strtoul(v.c_str(), nullptr, 10));
    }

    struct FakeMatch : MatchHandler
    {
        std::string getPayload = "ph=none";
        GameReply act;
        GameReply leave;
        std::vector<GameActRequest> acts;
        std::vector<uint32_t> leaves;
        int gets = 0;

        std::string GameGet() override
        {
            ++gets;
            return getPayload;
        }
        GameReply GameAct(GameActRequest const& request) override
        {
            acts.push_back(request);
            return act;
        }
        GameReply GameLeave(uint32_t matchId) override
        {
            leaves.push_back(matchId);
            return leave;
        }
    };
}

static void TestGameCanonicalEncoding()
{
    std::string const payload = EncodeGame(CanonicalHeader(), CanonicalView());
    CHECK(payload == kCanonicalGame);
    if (payload != kCanonicalGame)
        std::printf("    got: %s\n", payload.c_str());
    // Keys in the contract's order.
    std::vector<std::string> keys;
    for (std::size_t start = 0; start < payload.size();)
    {
        std::size_t const semi = payload.find(';', start);
        std::string const part = payload.substr(start, semi == std::string::npos ? std::string::npos : semi - start);
        keys.push_back(part.substr(0, part.find('=')));
        start = semi == std::string::npos ? payload.size() : semi + 1;
    }
    std::vector<std::string> const expected = { "mid", "seq", "ph", "turn", "act", "res", "why", "mn", "on", "dif", "mh", "mp",
        "mw", "mm", "mk", "mb", "mc", "mo", "ms", "mx", "oh", "op", "ow", "om", "ok", "ob", "os", "ox", "mul", "pk", "opk", "tmr",
        "lg", "evs", "ev" };
    CHECK(keys == expected);
    CHECK(EncodeGameNone() == "ph=none");

    // Names with the separator characters are escaped; the decoded values are unchanged.
    GameHeader h = CanonicalHeader();
    h.opponentName = "A;B=C";
    std::map<std::string, std::string> const kv = KvOf(EncodeGame(h, CanonicalView()));
    CHECK(kv.at("on") == "A;B=C");
}

// dur=1 (a ranked ladder duel) comes right after dif and only for a ladder duel: every other payload is unchanged.
static void TestGameDurableKey()
{
    GameHeader h = CanonicalHeader();
    std::string p = EncodeGame(h, CanonicalView());
    CHECK(KvOf(p).count("dur") == 0 && p.find(";dur=") == std::string::npos);
    h.durable = true;
    p = EncodeGame(h, CanonicalView());
    CHECK(KvOf(p).at("dur") == "1" && p.find(";dif=normal;dur=1;mh=") != std::string::npos);
}

// tmr= (the Nozdormu turn timer), right after opk and before lg.
static void TestGameTimerKey()
{
    GameHeader h = CanonicalHeader();
    std::string p = EncodeGame(h, CanonicalView());
    CHECK(KvOf(p).at("tmr") == "0" && p.find(";opk=0;tmr=0;lg=") != std::string::npos);
    h.timerSec = 15;
    p = EncodeGame(h, CanonicalView());
    CHECK(KvOf(p).at("tmr") == "15" && p.find(";opk=0;tmr=15;lg=") != std::string::npos);
    h.timerSec = 1;
    CHECK(KvOf(EncodeGame(h, CanonicalView())).at("tmr") == "1");
    h.timerSec = -4; // never negative
    CHECK(KvOf(EncodeGame(h, CanonicalView())).at("tmr") == "0");
}

static void TestGameResultAndPhases()
{
    HM::MatchView v = CanonicalView();
    CHECK(GameResultCode(v) == "-" && GameWhyCode(v, "stuck") == "-");
    v.phase = HM::Phase::Over;
    v.result.over = true;
    v.result.reason = HM::EndReason::HeroDied;
    v.result.winner = 0;
    CHECK(GameResultCode(v) == "win" && GameWhyCode(v, "") == "hero");
    v.result.winner = 1;
    CHECK(GameResultCode(v) == "loss");
    v.result.winner = -1;
    CHECK(GameResultCode(v) == "draw");
    v.result.reason = HM::EndReason::Concede;
    v.result.winner = 1;
    CHECK(GameResultCode(v) == "loss" && GameWhyCode(v, "") == "concede");
    v.result.reason = HM::EndReason::TurnLimit;
    v.result.winner = -1;
    CHECK(GameResultCode(v) == "draw" && GameWhyCode(v, "") == "turns");
    v.result.reason = HM::EndReason::Aborted;
    CHECK(GameResultCode(v) == "int" && GameWhyCode(v, "teleport") == "teleport" && GameWhyCode(v, "") == "error");

    std::map<std::string, std::string> kv = KvOf(EncodeGame(CanonicalHeader(), v));
    CHECK(kv.at("ph") == "over" && kv.at("act") == "-" && kv.at("res") == "int" && kv.at("why") == "error");

    HM::MatchView m = CanonicalView();
    m.phase = HM::Phase::Mulligan;
    m.turn = 0;
    m.legal.clear();
    m.mulliganPending = true;
    m.mulligan = { 12, 15, 18 };
    kv = KvOf(EncodeGame(CanonicalHeader(), m));
    CHECK(kv.at("ph") == "mull" && kv.at("act") == "-" && kv.at("mul") == "12,15,18" && kv.at("lg").empty());
    m.activeSide = 1;
    m.phase = HM::Phase::Play;
    m.mulliganPending = false;
    m.mulligan.clear();
    kv = KvOf(EncodeGame(CanonicalHeader(), m));
    CHECK(kv.at("act") == "o" && kv.at("mul").empty());

    // The viewer is side 1: m/o follow the viewer.
    HM::MatchView w = CanonicalView();
    w.viewer = 1;
    w.activeSide = 1;
    kv = KvOf(EncodeGame(CanonicalHeader(), w));
    CHECK(kv.at("act") == "m" && kv.at("ev") == "turn.o.0.0.3.-,draw.o.0.18.0.CS2_025");
}

static void TestGameEventWindow()
{
    HM::MatchView v = CanonicalView();
    v.events.clear();
    std::map<std::string, std::string> kv = KvOf(EncodeGame(CanonicalHeader(), v));
    CHECK(kv.at("evs") == "0" && kv.at("ev").empty());
    for (uint32_t i = 0; i < 55; ++i)
        v.events.push_back(MakeEvent(100 + i, HM::EventType::Damage, i % 3 == 2 ? -1 : static_cast<int>(i % 2), 1, 2, 3, ""));
    kv = KvOf(EncodeGame(CanonicalHeader(), v));
    CHECK(kv.at("evs") == "115");
    std::string const& ev = kv.at("ev");
    CHECK(static_cast<std::size_t>(std::count(ev.begin(), ev.end(), ',')) + 1 == kMaxGameEvents);
    CHECK(ev.compare(0, 14, "dmg.o.1.2.3.-,") == 0); // seq 115: side 1
    CHECK(ev.find("dmg.-.1.2.3.-") != std::string::npos);
}

static void TestDecodeGameAct()
{
    GameActRequest r;
    CHECK(DecodeGameAct("mid=7;seq=12;a=play;s=15", r));
    CHECK(r.matchId == 7 && r.seq == 12 && r.action.type == HM::ActionType::Play && r.action.source == 15 &&
          r.action.target == 0 && r.action.position == -1);
    CHECK(DecodeGameAct("mid=7;seq=12;a=play;s=15;t=3;p=0", r) && r.action.target == 3 && r.action.position == 0);
    CHECK(DecodeGameAct("mid=7;seq=12;a=play;s=15;p=7", r) && r.action.position == 7);
    CHECK(DecodeGameAct("mid=7;seq=12;a=atk;s=40;t=44", r) && r.action.type == HM::ActionType::Attack &&
          r.action.source == 40 && r.action.target == 44);
    CHECK(DecodeGameAct("mid=7;seq=12;a=hp", r) && r.action.type == HM::ActionType::HeroPower && r.action.source == 0 &&
          r.action.target == 0);
    CHECK(DecodeGameAct("mid=7;seq=12;a=hp;t=3;s=99", r) && r.action.source == 0 && r.action.target == 3);
    CHECK(DecodeGameAct("mid=7;seq=12;a=end", r) && r.action.type == HM::ActionType::EndTurn);
    CHECK(DecodeGameAct("mid=7;a=concede", r) && r.action.type == HM::ActionType::Concede && r.seq == 0);
    CHECK(DecodeGameAct("mid=7;seq=3;a=concede", r) && r.seq == 3);
    CHECK(DecodeGameAct("mid=7;seq=0;a=mull;r=", r) && r.action.type == HM::ActionType::Mulligan && r.action.replace.empty());
    CHECK(DecodeGameAct("mid=7;seq=0;a=mull;r=12,18", r) && r.action.replace.size() == 2 && r.action.replace[1] == 18);
    CHECK(DecodeGameAct("mid=4294967295;seq=4294967295;a=end", r) && r.matchId == 4294967295u);

    char const* const bad[] = {
        "",
        "seq=1;a=end",                    // no mid
        "mid=7;seq=1",                    // no a
        "mid=7;a=end",                    // no seq
        "mid=7;seq=1;a=dance",
        "mid=x;seq=1;a=end",
        "mid=-1;seq=1;a=end",
        "mid=4294967296;seq=1;a=end",
        "mid=7;seq=1;a=play",             // play needs s
        "mid=7;seq=1;a=atk;s=4",          // atk needs t
        "mid=7;seq=1;a=atk;t=4",
        "mid=7;seq=1;a=mull",             // mull needs r
        "mid=7;seq=1;a=mull;r=1,,2",
        "mid=7;seq=1;a=mull;r=1,2,3,4,5,6,7,8,9,10,11",
        "mid=7;seq=1;a=mull;r=a",
        "mid=7;seq=1;a=play;s=1;p=8",     // position 0..7
        "mid=7;seq=1;a=play;s=1;p=-1",
        "mid=7;seq=1;a=play;s=",
        "mid=7;seq=1;a=play;s=1;x=2",     // unknown key
        "mid=7;seq=1;a=end;a=end",        // duplicate key
        "mid=7;seq=1;a=end;",
    };
    for (char const* p : bad)
    {
        bool const ok = DecodeGameAct(p, r);
        CHECK(!ok);
        if (ok)
            std::printf("    accepted: %s\n", p);
    }

    uint32_t mid = 0;
    CHECK(DecodeGameLeave("mid=9", mid) && mid == 9);
    CHECK(!DecodeGameLeave("", mid) && !DecodeGameLeave("mid=", mid) && !DecodeGameLeave("mid=9;x=1", mid) &&
          !DecodeGameLeave("id=9", mid));
    CHECK(std::string(GameWhyText("stale")).find("out of date") != std::string::npos);
    CHECK(std::string(GameWhyText("taunt")).find("Taunt") != std::string::npos);
}

static void TestMatchOpponentsAndExpand()
{
    std::vector<MatchOpponent> ops;
    std::vector<std::string> errors;
    ParseMatchOpponents(" 6740:mage , 6741:WARRIOR,,6740:warrior, x:mage, 12:priest, 13, 0:mage, 99999999999:mage, 14:paladin2 ",
                        ops, errors);
    // Any of the nine classes (the opponent plays normal_<class> / hard_<class> of the library).
    CHECK(ops.size() == 3 && ops[0].entry == 6740 && ops[0].deckClass == "MAGE" && ops[1].entry == 6741 &&
          ops[1].deckClass == "WARRIOR" && ops[2].entry == 12 && ops[2].deckClass == "PRIEST");
    CHECK(errors.size() == 6);
    errors.clear();
    ParseMatchOpponents("1:druid,2:hunter,3:mage,4:paladin,5:priest,6:rogue,7:shaman,8:warlock,9:warrior,10:neutral", ops, errors);
    CHECK(ops.size() == 9 && errors.size() == 1 && ops[3].deckClass == "PALADIN");
    errors.clear();
    ParseMatchOpponents("", ops, errors);
    CHECK(ops.empty() && errors.empty());

    std::vector<std::string> out;
    DeckCards cards;
    CHECK(!ExpandDeck(cards, out));
    cards["B"] = 2;
    cards["A"] = 1;
    CHECK(ExpandDeck(cards, out) && out == std::vector<std::string>({ "A", "B", "B" }));
    cards["C"] = 0;
    CHECK(!ExpandDeck(cards, out));
    cards.erase("C");
    cards["D"] = 60;
    CHECK(!ExpandDeck(cards, out));
}

static void TestMatchServiceLifecycle()
{
    MatchService svc(500);
    MatchStartParams const params = FixtureParams(42, 1234);
    MatchStartResult start = svc.Start(params, 1000);
    CHECK(!start.ok && start.error == "engine");
    CHECK(svc.Get(42) == "ph=none" && svc.RunningCount() == 0);

    svc.SetCardDb(TestCardDb());
    svc.SetAiDelayMs(1);
    CHECK(svc.AiDelayMs() == kMinAiDelayMs);
    svc.SetAiDelayMs(1000000);
    CHECK(svc.AiDelayMs() == kMaxAiDelayMs);
    svc.SetAiDelayMs(1000);

    // A bad deck: nothing is stored.
    MatchStartParams badDeck = params;
    badDeck.humanDeck[0] = "EX1_323"; // Lord Jaraxxus: not supported
    start = svc.Start(badDeck, 1000);
    CHECK(!start.ok && start.error.find("EX1_323") != std::string::npos && !svc.Has(42));

    start = svc.Start(params, 1000);
    CHECK(start.ok && start.matchId == 500 && svc.IsRunning(42) && svc.RunningCount() == 1 && svc.Count() == 1);
    std::map<std::string, std::string> kv = KvOf(start.payload);
    CHECK(kv.at("mid") == "500" && kv.at("ph") == "mull" && kv.at("mn") == "Jonas" && kv.at("on") == "Innkeeper Allison");
    CHECK(kv.at("mh").find(".HERO_08.30.30.0.0.-") != std::string::npos && kv.at("oh").find(".HERO_01.") != std::string::npos);
    CHECK(!kv.at("mul").empty() && kv.at("lg").empty() && kv.at("mc").size() > 10);
    // The AI mulliganed at creation: its Mulligan event is in the first payload.
    CHECK(kv.at("ev").find("mull.o.") != std::string::npos);
    uint32_t const seq0 = U(kv, "seq");
    CHECK(seq0 == 1);

    // A second start while running is refused.
    MatchStartResult again = svc.Start(params, 1000);
    CHECK(!again.ok && again.error == "running");

    // Refusals change nothing.
    GameActOutcome o = svc.Act(42, Req("mid=501;seq=1;a=mull;r="), 1100);
    CHECK(!o.ok && o.why == kWhyMatch);
    o = svc.Act(43, Req("mid=500;seq=1;a=mull;r="), 1100);
    CHECK(!o.ok && o.why == kWhyMatch);
    o = svc.Act(42, Req("mid=500;seq=0;a=mull;r="), 1100);
    CHECK(!o.ok && o.why == kWhyStale);
    o = svc.Act(42, Req("mid=500;seq=1;a=mull;r=1"), 1100);
    CHECK(!o.ok && o.why == "mulligan");
    o = svc.Act(42, Req("mid=500;seq=1;a=end"), 1100);
    CHECK(!o.ok && o.why == "phase");
    CHECK(U(KvOf(svc.Get(42)), "seq") == 1);

    // GAME_GET: a resync without events.
    kv = KvOf(svc.Get(42));
    CHECK(kv.at("evs") == "0" && kv.at("ev").empty() && kv.at("ph") == "mull");

    o = svc.Act(42, Req("mid=500;seq=1;a=mull;r="), 1100);
    CHECK(o.ok && !o.ended);
    kv = KvOf(o.payload);
    CHECK(kv.at("ph") == "play" && kv.at("turn") == "1" && U(kv, "seq") == 2 && kv.at("mul").empty());
    CHECK(kv.at("ev").find("mull.m.0.0.0.-") != std::string::npos && kv.at("ev").find("turn.") != std::string::npos);
    CHECK(kv.at("ev").find("mull.o.") == std::string::npos); // already sent with the start payload

    // Hidden information: the opponent's draws carry no card id; its hand is a count.
    std::string const& ev = kv.at("ev");
    for (std::size_t p = ev.find("draw.o."); p != std::string::npos; p = ev.find("draw.o.", p + 1))
    {
        std::size_t const end = ev.find(',', p);
        std::string const e = ev.substr(p, end == std::string::npos ? std::string::npos : end - p);
        CHECK(e.size() > 2 && e.compare(e.size() - 2, 2, ".-") == 0);
    }
    CHECK(kv.at("ok").compare(0, 1, "4") == 0 || kv.at("ok").compare(0, 1, "5") == 0);

    // Play the match out: the human ends its turns (and uses the hero power once), the AI acts on Tick.
    uint64_t now = 1100;
    bool ended = false;
    bool usedPower = false;
    MatchEnd end;
    int guard = 0;
    while (!ended && ++guard < 3000)
    {
        kv = KvOf(svc.Get(42));
        if (kv.at("act") == "m")
        {
            uint32_t const seq = U(kv, "seq");
            std::string const mana = kv.at("mm");
            if (!usedPower && FirstId(mana) >= 2)
            {
                // a=hp without s: the service fills in the human's hero power.
                std::string const enemy = std::to_string(FirstId(kv.at("oh")));
                o = svc.Act(42, Req("mid=500;seq=" + std::to_string(seq) + ";a=hp;t=" + enemy), now);
                CHECK(o.ok);
                CHECK(KvOf(o.payload).at("mp").back() == '1');
                usedPower = true;
                continue;
            }
            o = svc.Act(42, Req("mid=500;seq=" + std::to_string(seq) + ";a=end"), now);
            CHECK(o.ok);
            if (o.ended)
            {
                ended = true;
                end = o.end;
            }
            continue;
        }
        // The AI's turn: nothing before the delay, then exactly one action per Tick.
        CHECK(svc.Tick(now + 10).empty());
        now += 1000;
        std::vector<MatchPush> const pushes = svc.Tick(now);
        CHECK(pushes.size() == 1);
        if (pushes.size() != 1)
            break;
        CHECK(pushes[0].guid == 42 && pushes[0].note.empty() && !pushes[0].payload.empty());
        CHECK(KvOf(pushes[0].payload).at("evs") != "0");
        if (pushes[0].ended)
        {
            ended = true;
            end = pushes[0].end;
        }
    }
    CHECK(ended && usedPower);
    CHECK(end.guid == 42 && end.matchId == 500 && end.humanName == "Jonas" && end.opponentName == "Innkeeper Allison");
    CHECK((end.res == "loss" && end.why == "hero") || (end.res == "draw" && end.why == "turns"));
    CHECK(!svc.IsRunning(42) && svc.Has(42) && svc.RunningCount() == 0 && svc.AiRefusals() == 0);
    kv = KvOf(svc.Get(42));
    CHECK(kv.at("ph") == "over" && kv.at("res") == end.res && kv.at("lg").empty());
    o = svc.Act(42, Req("mid=500;a=concede"), now);
    CHECK(!o.ok && o.why == kWhyOver);
    CHECK(svc.Tick(now + 100000).empty());

    MatchInfo info;
    CHECK(svc.Info(42, info) && !info.running && info.matchId == 500 && info.res == end.res && info.seed == 1234);
    CHECK(svc.List().size() == 1);

    // GAME_LEAVE: another id answers ph=none and keeps the match; the right one drops it.
    GameLeaveOutcome l = svc.Leave(42, 77);
    CHECK(l.ok && l.payload == "ph=none" && svc.Has(42));
    l = svc.Leave(42, 500);
    CHECK(l.ok && !svc.Has(42) && svc.Get(42) == "ph=none");
    l = svc.Leave(42, 500);
    CHECK(l.ok);
}

static void TestMatchServiceEndings()
{
    MatchService svc(10);
    svc.SetCardDb(TestCardDb());

    // Concede (no seq needed), also during the mulligan; the finished match is replaced by a new start.
    MatchStartResult s = svc.Start(FixtureParams(1, 5), 0);
    CHECK(s.ok && s.matchId == 10);
    GameLeaveOutcome l = svc.Leave(1, 10);
    CHECK(!l.ok && l.why == kWhyActive);
    GameActOutcome o = svc.Act(1, Req("mid=10;a=concede"), 0);
    CHECK(o.ok && o.ended && o.end.res == "loss" && o.end.why == "concede");
    std::map<std::string, std::string> kv = KvOf(o.payload);
    CHECK(kv.at("ph") == "over" && kv.at("res") == "loss" && kv.at("why") == "concede");
    s = svc.Start(FixtureParams(1, 6), 0);
    CHECK(s.ok && s.matchId == 11 && svc.Count() == 1);

    // Abort from outside: no winner, the reason code; a second abort does nothing.
    MatchAbortOutcome a = svc.Abort(1, Interrupt::Stuck);
    CHECK(a.ended && a.end.res == "int" && a.end.why == "stuck");
    kv = KvOf(a.payload);
    CHECK(kv.at("res") == "int" && kv.at("why") == "stuck" && kv.at("ev").find("end.") != std::string::npos);
    CHECK(!svc.Abort(1, Interrupt::Gm).ended && !svc.Abort(99, Interrupt::Gm).ended);
    CHECK(KvOf(svc.Get(1)).at("why") == "stuck");
    o = svc.Act(1, Req("mid=11;a=concede"), 0);
    CHECK(!o.ok && o.why == kWhyOver);

    for (Interrupt why : { Interrupt::Teleport, Interrupt::Death, Interrupt::Npc, Interrupt::Gm, Interrupt::Error })
    {
        CHECK(svc.Start(FixtureParams(1, 7), 0).ok);
        a = svc.Abort(1, why);
        CHECK(a.ended && a.end.why == InterruptCode(why));
    }

    // Logout: the match is dropped, running or finished.
    CHECK(svc.Start(FixtureParams(2, 8), 0).ok && svc.RunningCount() == 1);
    MatchEnd end;
    CHECK(svc.Drop(2, &end) && end.why == "logout" && end.res == "int");
    CHECK(!svc.Has(2) && svc.RunningCount() == 0);
    CHECK(!svc.Drop(2));
    CHECK(!svc.Drop(1) && !svc.Has(1)); // finished: dropped, but it was not running

    // Two matches tick independently, one AI action each per Tick.
    CHECK(svc.Start(FixtureParams(3, 101), 0).ok && svc.Start(FixtureParams(4, 202), 0).ok);
    for (uint32_t g : { 3u, 4u })
    {
        uint32_t const seq = U(KvOf(svc.Get(g)), "seq");
        CHECK(svc.Act(g, Req("mid=" + KvOf(svc.Get(g)).at("mid") + ";seq=" + std::to_string(seq) + ";a=mull;r="), 0).ok);
        if (KvOf(svc.Get(g)).at("act") == "m")
            CHECK(svc.Act(g, Req("mid=" + KvOf(svc.Get(g)).at("mid") + ";seq=" + std::to_string(seq + 1) + ";a=end"), 0).ok);
    }
    std::vector<MatchPush> const pushes = svc.Tick(5000);
    CHECK(pushes.size() == 2);
    CHECK(svc.List().size() == 2 && svc.List()[0].matchId < svc.List()[1].matchId);
}

static void TestMatchServiceEventsMark()
{
    MatchService svc(1);
    svc.SetCardDb(TestCardDb());
    MatchStartParams p = FixtureParams(9, 77);
    MatchStartResult s = svc.Start(p, 0);
    CHECK(s.ok);
    uint32_t seq = U(KvOf(s.payload), "seq");
    GameActOutcome o = svc.Act(9, Req("mid=1;seq=" + std::to_string(seq) + ";a=mull;r="), 0);
    CHECK(o.ok);
    std::map<std::string, std::string> kv = KvOf(o.payload);
    uint32_t lastSeen = U(kv, "evs") + static_cast<uint32_t>(std::count(kv.at("ev").begin(), kv.at("ev").end(), ',')) +
        (kv.at("ev").empty() ? 0u : 1u) - 1u;
    // Every following payload continues right after the last event sent.
    uint64_t now = 0;
    for (int i = 0; i < 40 && kv.at("ph") == "play"; ++i)
    {
        std::string payload;
        std::vector<Match::Event> events;
        if (kv.at("act") == "m")
        {
            o = svc.Act(9, Req("mid=1;seq=" + kv.at("seq") + ";a=end"), now);
            CHECK(o.ok);
            payload = o.payload;
            events = o.events;
        }
        else
        {
            now += 2000;
            std::vector<MatchPush> const pushes = svc.Tick(now);
            CHECK(pushes.size() == 1);
            if (pushes.empty())
                break;
            payload = pushes[0].payload;
            CHECK(pushes[0].matchId == s.matchId);
            events = pushes[0].events;
        }
        kv = KvOf(payload);
        CHECK(events.size() == (kv.at("ev").empty() ? 0u :
            1u + static_cast<unsigned>(std::count(kv.at("ev").begin(), kv.at("ev").end(), ','))));
        if (!kv.at("ev").empty())
        {
            CHECK(U(kv, "evs") == lastSeen + 1);
            CHECK(!events.empty() && events.front().seq == lastSeen + 1);
            lastSeen = U(kv, "evs") + static_cast<uint32_t>(std::count(kv.at("ev").begin(), kv.at("ev").end(), ','));
        }
    }
}

static void TestSessionGame()
{
    FakeMatch fake;
    Client c;
    c.settings.ratePerSecond = 1000;
    c.settings.rateBurst = 1000;
    // Before HELLO.
    CHECK(FirstErr(c.SendGame(fake, "GAME_GET", "g0", "")) == "NOT_READY");
    CHECK(fake.gets == 0);
    // WELCOME lists the match capability (Hearthstone.Match.Enable).
    auto rep = Decode(c.SendGame(fake, "HELLO", "h1", "protocol=1;addon=t;dataset=-"));
    CHECK(rep.size() == 1 && Kv(rep[0])["caps"] == "solo,chunk,packs,decks,wallet,match");
    c.settings.matchEnable = false;
    rep = Decode(c.SendGame(fake, "HELLO", "h2", "protocol=1;addon=t;dataset=-"));
    CHECK(rep.size() == 1 && Kv(rep[0])["caps"] == "solo,chunk,packs,decks,wallet");
    c.settings.matchEnable = true;

    // GAME_GET: the handler's payload with the same reqId (any payload).
    fake.getPayload = kCanonicalGame;
    rep = Decode(c.SendGame(fake, "GAME_GET", "g1", "whatever"));
    CHECK(rep.size() == 1 && rep[0].op == "GAME" && rep[0].reqId == "g1" && rep[0].payload == kCanonicalGame && fake.gets == 1);

    // GAME_ACT: malformed requests never reach the handler.
    CHECK(FirstErr(c.SendGame(fake, "GAME_ACT", "a0", "mid=1;a=play")) == "MALFORMED");
    CHECK(FirstErr(c.SendGame(fake, "GAME_ACT", "a0", "")) == "MALFORMED");
    CHECK(fake.acts.empty());
    fake.act.ok = true;
    fake.act.payload = "mid=1;ph=play";
    rep = Decode(c.SendGame(fake, "GAME_ACT", "a1", "mid=1;seq=4;a=atk;s=40;t=44"));
    CHECK(rep.size() == 1 && rep[0].op == "GAME" && rep[0].reqId == "a1" && rep[0].payload == "mid=1;ph=play");
    CHECK(fake.acts.size() == 1 && fake.acts[0].matchId == 1 && fake.acts[0].seq == 4 &&
          fake.acts[0].action.type == HM::ActionType::Attack && fake.acts[0].action.target == 44);
    fake.act.ok = false;
    fake.act.why = "stale";
    rep = Decode(c.SendGame(fake, "GAME_ACT", "a2", "mid=1;seq=4;a=end"));
    CHECK(rep.size() == 1 && ErrCode(rep[0]) == "REFUSED" && Kv(rep[0])["why"] == "stale" && rep[0].reqId == "a2");

    // GAME_LEAVE.
    CHECK(FirstErr(c.SendGame(fake, "GAME_LEAVE", "l0", "")) == "MALFORMED");
    fake.leave.ok = true;
    fake.leave.payload = "ph=none";
    rep = Decode(c.SendGame(fake, "GAME_LEAVE", "l1", "mid=3"));
    CHECK(rep.size() == 1 && rep[0].op == "GAME" && rep[0].payload == "ph=none" && fake.leaves.back() == 3);
    fake.leave.ok = false;
    fake.leave.why = "active";
    rep = Decode(c.SendGame(fake, "GAME_LEAVE", "l2", "mid=3"));
    CHECK(rep.size() == 1 && ErrCode(rep[0]) == "REFUSED" && Kv(rep[0])["why"] == "active");

    // Without a handler (matches off): UNAVAILABLE.
    CHECK(FirstErr(c.Send1("GAME_GET", "u1", "")) == "UNAVAILABLE");
    CHECK(FirstErr(c.Send1("GAME_ACT", "u2", "mid=1;seq=1;a=end")) == "UNAVAILABLE");
    CHECK(FirstErr(c.Send1("GAME_LEAVE", "u3", "mid=1")) == "UNAVAILABLE");

    // The GAME push.
    std::vector<std::string> frames;
    AppendGame(frames, kCanonicalGame);
    rep = Decode(frames);
    CHECK(rep.size() == 1 && rep[0].op == "GAME" && rep[0].reqId == kServerPushReqId && rep[0].payload == kCanonicalGame);
}

static void TestDeckNotReadyText()
{
    DeckMatchCheck c;
    CHECK(DeckNotReadyText(c, "MAGE", "") == "your decks cannot be read right now");
    c.status = DeckMatchCheck::Status::Missing;
    CHECK(DeckNotReadyText(c, "MAGE", "") == "the deck does not exist any more");
    c.status = DeckMatchCheck::Status::Ok;
    CHECK(DeckNotReadyText(c, "MAGE", "").empty()); // ready
    c.state.locked = true;
    CHECK(DeckNotReadyText(c, "MAGE", "") == "this character cannot play Mage (it is not your class and not unlocked)");
    c.state.locked = false;
    c.engine = true;
    c.unsupportedCard = "EX1_011";
    CHECK(DeckNotReadyText(c, "MAGE", "Voodoo Doctor") == "Voodoo Doctor does not work in matches yet");
    CHECK(DeckNotReadyText(c, "MAGE", "") == "EX1_011 does not work in matches yet");
    c.state.size = true;
    c.state.owned = true;
    CHECK(DeckNotReadyText(c, "WARRIOR", "X") ==
          "fewer than 30 cards; uses more copies of a card than you own; X does not work in matches yet");
}

static void TestGameFlagsAndNewKeys()
{
    // Minion flags, one at a time, in the contract order t c d w s f x p e i n a.
    struct MinionFlag
    {
        bool HM::MinionView::*field;
        char letter;
    };
    MinionFlag const minionFlags[] = {
        { &HM::MinionView::taunt, 't' }, { &HM::MinionView::charge, 'c' }, { &HM::MinionView::divineShield, 'd' },
        { &HM::MinionView::windfury, 'w' }, { &HM::MinionView::stealth, 's' }, { &HM::MinionView::frozen, 'f' },
        { &HM::MinionView::silenced, 'x' }, { &HM::MinionView::poisonous, 'p' }, { &HM::MinionView::enraged, 'e' },
        { &HM::MinionView::immune, 'i' }, { &HM::MinionView::cantAttack, 'n' }, { &HM::MinionView::canAttack, 'a' } };
    std::string order;
    for (MinionFlag const& f : minionFlags)
        order.push_back(f.letter);
    CHECK(order == "tcdwsfxpeina");

    HM::MatchView v = CanonicalView();
    v.self.board.clear();
    v.other.board.clear();
    v.legal.clear();
    HM::MinionView plain = MakeMinionView(50, "CS2_172", 1, 1, 1);
    v.self.board.push_back(plain);
    CHECK(KvOf(EncodeGame(CanonicalHeader(), v)).at("mb") == "50.CS2_172.1.1.1.-");
    for (MinionFlag const& f : minionFlags)
    {
        HM::MinionView m = plain;
        m.*(f.field) = true;
        v.self.board = { m };
        v.other.board = { m };
        std::string const expected = std::string("50.CS2_172.1.1.1.") + f.letter;
        std::map<std::string, std::string> const kv = KvOf(EncodeGame(CanonicalHeader(), v));
        CHECK(kv.at("mb") == expected && kv.at("ob") == expected);
    }
    HM::MinionView every = plain;
    for (MinionFlag const& f : minionFlags)
        every.*(f.field) = true;
    v.self.board = { every };
    CHECK(KvOf(EncodeGame(CanonicalHeader(), v)).at("mb") == "50.CS2_172.1.1.1.tcdwsfxpeina");
    HM::MinionView combo = plain; // a frozen Taunt that can not attack, silenced and poisonous
    combo.taunt = combo.frozen = combo.silenced = combo.poisonous = combo.cantAttack = true;
    v.self.board = { combo, plain };
    CHECK(KvOf(EncodeGame(CanonicalHeader(), v)).at("mb") == "50.CS2_172.1.1.1.tfxpn,50.CS2_172.1.1.1.-");

    // Hero flags: f i a, "-" when none (both heroes).
    struct HeroFlag
    {
        bool HM::HeroView::*field;
        char letter;
    };
    HeroFlag const heroFlags[] = { { &HM::HeroView::frozen, 'f' }, { &HM::HeroView::immune, 'i' }, { &HM::HeroView::canAttack, 'a' } };
    v = CanonicalView();
    CHECK(KvOf(EncodeGame(CanonicalHeader(), v)).at("mh").back() == '-');
    for (HeroFlag const& f : heroFlags)
    {
        v = CanonicalView();
        v.self.hero.*(f.field) = true;
        v.other.hero.*(f.field) = true;
        std::map<std::string, std::string> const kv = KvOf(EncodeGame(CanonicalHeader(), v));
        CHECK(kv.at("mh") == std::string("1.HERO_08.27.30.0.0.") + f.letter);
        CHECK(kv.at("oh") == std::string("3.HERO_01.30.30.2.0.") + f.letter);
    }
    v = CanonicalView();
    v.self.hero.frozen = v.self.hero.immune = v.self.hero.canAttack = true;
    v.other.hero.frozen = v.other.hero.immune = true;
    std::map<std::string, std::string> kv = KvOf(EncodeGame(CanonicalHeader(), v));
    CHECK(kv.at("mh") == "1.HERO_08.27.30.0.0.fia" && kv.at("oh") == "3.HERO_01.30.30.2.0.fi");

    // dif: normal, hard, boss; an empty header value falls back to normal.
    for (char const* dif : { "normal", "hard", "boss" })
    {
        GameHeader h = CanonicalHeader();
        h.difficulty = dif;
        CHECK(KvOf(EncodeGame(h, CanonicalView())).at("dif") == dif);
    }
    GameHeader noDif = CanonicalHeader();
    noDif.difficulty.clear();
    CHECK(KvOf(EncodeGame(noDif, CanonicalView())).at("dif") == "normal");

    // mo: Choose One options of the hand cards, in hand order; none without such a card.
    v = CanonicalView();
    kv = KvOf(EncodeGame(CanonicalHeader(), v));
    CHECK(kv.at("mo") == "24.EX1_154a/EX1_154b" && kv.at("ms") == "26.EX1_289" && kv.at("os") == "36,37");
    v.self.hand.insert(v.self.hand.begin(), MakeHandView(70, "EX1_165", 5, 4, 4));
    v.self.hand.front().options = { "EX1_165a", "EX1_165b" };
    CHECK(KvOf(EncodeGame(CanonicalHeader(), v)).at("mo") == "70.EX1_165a/EX1_165b,24.EX1_154a/EX1_154b");
    v.self.hand.clear();
    CHECK(KvOf(EncodeGame(CanonicalHeader(), v)).at("mo").empty());

    // ms / os: empty without secrets; the other side's card ids are never written, even when set.
    v = CanonicalView();
    v.self.secrets.clear();
    v.other.secrets.clear();
    kv = KvOf(EncodeGame(CanonicalHeader(), v));
    CHECK(kv.at("ms").empty() && kv.at("os").empty());
    for (HM::EntityId id = 80; id < 85; ++id)
    {
        HM::SecretView x;
        x.id = id;
        x.cardId = "EX1_289";
        v.self.secrets.push_back(x);
        x.cardId = "LEAK_ME";
        v.other.secrets.push_back(x);
    }
    std::string const text = EncodeGame(CanonicalHeader(), v);
    kv = KvOf(text);
    CHECK(kv.at("ms") == "80.EX1_289,81.EX1_289,82.EX1_289,83.EX1_289,84.EX1_289" && kv.at("os") == "80,81,82,83,84");
    CHECK(text.find("LEAK_ME") == std::string::npos);

    // mx / ox.
    v = CanonicalView();
    v.self.cardsPlayed = 3;
    v.self.overloadLocked = 2;
    v.self.overloadOwed = 1;
    kv = KvOf(EncodeGame(CanonicalHeader(), v));
    CHECK(kv.at("mx") == "3.2.1" && kv.at("ox") == "0.1.2");

    // pk / opk: the viewer's pending pick options; the other side only sees opk=1.
    v = CanonicalView();
    CHECK(kv.at("pk").empty() && kv.at("opk") == "0");
    v.legal.clear();
    v.pickPending = true;
    for (HM::EntityId id : { 60u, 61u, 62u })
    {
        HM::PickView p;
        p.id = id;
        p.cardId = id == 60 ? "CS2_029" : (id == 61 ? "CS2_023" : "EX1_011");
        v.pick.push_back(p);
    }
    kv = KvOf(EncodeGame(CanonicalHeader(), v));
    CHECK(kv.at("pk") == "60.CS2_029,61.CS2_023,62.EX1_011" && kv.at("opk") == "0" && kv.at("lg").empty());
    v.pickPending = false; // options only travel while the pick is pending
    kv = KvOf(EncodeGame(CanonicalHeader(), v));
    CHECK(kv.at("pk").empty());
    v.pick.clear();
    v.opponentPicking = true;
    kv = KvOf(EncodeGame(CanonicalHeader(), v));
    CHECK(kv.at("pk").empty() && kv.at("opk") == "1");

    // lg: one entry per Choose One option in option order after the hand position; the choice is
    // only written for plays; an attack with a stray choice stays "a<source>.".
    v = CanonicalView();
    v.legal.clear();
    v.legal.push_back(LegalEntry(HM::ActionType::Play, 24, 45, 2));
    v.legal.push_back(LegalEntry(HM::ActionType::Play, 24, 40, 1));
    v.legal.push_back(LegalEntry(HM::ActionType::Play, 24, 45, 1));
    v.legal.push_back(LegalEntry(HM::ActionType::Play, 24, 0, 2));
    v.legal.push_back(LegalEntry(HM::ActionType::Play, 12, 0, 0));
    v.legal.push_back(LegalEntry(HM::ActionType::Attack, 40, 3, 2));
    v.legal.push_back(LegalEntry(HM::ActionType::Play, 15, 0, 0));
    CHECK(KvOf(EncodeGame(CanonicalHeader(), v)).at("lg") == "p12.0,p15.0,p24:1.40/45,p24:2.0/45,a40.3");
}

static void TestDecodeGameActPhase08()
{
    GameActRequest r;
    // Choose One: c=1|2 with a=play.
    CHECK(DecodeGameAct("mid=7;seq=12;a=play;s=24;c=1", r) && r.action.type == HM::ActionType::Play && r.action.source == 24 &&
          r.action.choice == 1 && r.action.target == 0 && r.action.position == -1);
    CHECK(DecodeGameAct("mid=7;seq=12;a=play;s=24;t=40;p=2;c=2", r) && r.action.choice == 2 && r.action.target == 40 &&
          r.action.position == 2);
    CHECK(DecodeGameAct("mid=7;seq=12;a=play;s=24", r) && r.action.choice == 0);
    char const* const badChoice[] = {
        "mid=7;seq=12;a=play;s=24;c=0",      // only 1 or 2
        "mid=7;seq=12;a=play;s=24;c=3",
        "mid=7;seq=12;a=play;s=24;c=",
        "mid=7;seq=12;a=play;s=24;c=x",
        "mid=7;seq=12;a=play;s=24;c=-1",
        "mid=7;seq=12;a=play;s=24;c=01x",
        "mid=7;seq=12;a=play;s=24;c=1;c=1",  // duplicate
        "mid=7;seq=12;a=atk;s=40;t=44;c=1",  // c only with play
        "mid=7;seq=12;a=hp;c=1",
        "mid=7;seq=12;a=end;c=1",
        "mid=7;a=concede;c=1",
        "mid=7;seq=12;a=mull;r=;c=1",
        "mid=7;seq=12;a=pick;s=60;c=1",
    };
    for (char const* p : badChoice)
    {
        bool const ok = DecodeGameAct(p, r);
        CHECK(!ok);
        if (ok)
            std::printf("    accepted: %s\n", p);
    }
    // pick answers a pending pick: ActionType::Choose, source = the option, needs seq and s.
    CHECK(DecodeGameAct("mid=7;seq=12;a=pick;s=60", r) && r.action.type == HM::ActionType::Choose && r.action.source == 60 &&
          r.action.target == 0 && r.action.position == -1 && r.action.choice == 0 && r.action.replace.empty() && r.seq == 12);
    CHECK(DecodeGameAct("mid=7;seq=12;a=pick;s=61;t=5;p=3;r=1,2", r) && r.action.source == 61 && r.action.target == 0 &&
          r.action.position == -1 && r.action.replace.empty());
    char const* const badPick[] = {
        "mid=7;a=pick;s=60",                 // pick needs seq (not like concede)
        "mid=7;seq=12;a=pick",               // needs s
        "mid=7;seq=12;a=pick;s=",
        "mid=7;seq=12;a=pick;s=x",
        "mid=7;seq=12;a=pick;s=1;p=8",       // other values are still validated
        "mid=7;seq=12;a=pick;t=60",
        "mid=7;seq=12;a=Pick;s=60",
    };
    for (char const* p : badPick)
    {
        bool const ok = DecodeGameAct(p, r);
        CHECK(!ok);
        if (ok)
            std::printf("    accepted: %s\n", p);
    }
    for (char const* why : { "choice", "pending", "secret" })
    {
        std::string const text = GameWhyText(why);
        CHECK(text != GameWhyText("nonsense") && !text.empty());
    }
    CHECK(std::string(GameWhyText("secret")).find("secret") != std::string::npos);
}

namespace
{
    // Plays a started match of the character to its end: the human only ends its turns, the AI is
    // ticked. Returns every payload (pushes and replies) in order.
    std::vector<std::string> PlayOut(MatchService& svc, uint32_t guid, uint32_t mid, MatchEnd& end, bool& ended)
    {
        std::vector<std::string> payloads;
        ended = false;
        uint64_t now = 1000;
        for (int guard = 0; guard < 3000 && !ended; ++guard)
        {
            std::map<std::string, std::string> kv = KvOf(svc.Get(guid));
            if (kv.at("ph") == "mull")
            {
                GameActOutcome o = svc.Act(guid, Req("mid=" + std::to_string(mid) + ";seq=" + kv.at("seq") + ";a=mull;r="), now);
                CHECK(o.ok);
                payloads.push_back(o.payload);
            }
            else if (kv.at("act") == "m")
            {
                GameActOutcome o = svc.Act(guid, Req("mid=" + std::to_string(mid) + ";seq=" + kv.at("seq") + ";a=end"), now);
                CHECK(o.ok);
                payloads.push_back(o.payload);
                if (o.ended)
                {
                    ended = true;
                    end = o.end;
                }
            }
            else
            {
                now += 5000;
                for (MatchPush const& push : svc.Tick(now))
                {
                    CHECK(push.note.empty());
                    payloads.push_back(push.payload);
                    if (push.ended)
                    {
                        ended = true;
                        end = push.end;
                    }
                }
            }
        }
        return payloads;
    }
}

static void TestMatchServicePhase08()
{
    // The seed of an AI decision: deterministic, distinct per seq and per match seed.
    CHECK(AiSeedFor(77, 5) == AiSeedFor(77, 5) && AiSeedFor(77, 5) != AiSeedFor(77, 6) && AiSeedFor(77, 5) != AiSeedFor(78, 5));
    std::set<uint32_t> seeds;
    for (uint32_t seq = 0; seq < 20000; ++seq)
        seeds.insert(AiSeedFor(12345, seq));
    CHECK(seeds.size() == 20000);

    // dif= of every payload, the profile and the overrides travel through Start.
    MatchService svc(900);
    svc.SetCardDb(TestCardDb());
    svc.SetAiDelayMs(1000);
    MatchStartParams p = FixtureParams(1, 4242);
    CHECK(p.difficulty == "normal" && p.profile == HM::AiProfile::Normal && p.opponentHeroHealth == 0 &&
          p.opponentHeroArmor == 0 && p.opponentHeroCardId.empty() && p.opponentHeroPowerId.empty());
    MatchStartResult s = svc.Start(p, 0);
    CHECK(s.ok && KvOf(s.payload).at("dif") == "normal");
    MatchInfo info;
    CHECK(svc.Info(1, info) && info.difficulty == "normal");
    bool firstDifficulty = true;
    for (char const* dif : { "hard", "boss" })
    {
        p = FixtureParams(1, 4243);
        p.difficulty = dif;
        p.profile = HM::AiProfile::Hard;
        if (firstDifficulty)
        {
            s = svc.Start(p, 0); // the first match is still running
            CHECK(!s.ok && s.error == "running");
            CHECK(svc.Abort(1, Interrupt::Gm).ended);
            firstDifficulty = false;
        }
        s = svc.Start(p, 0);
        CHECK(s.ok && KvOf(s.payload).at("dif") == dif);
        CHECK(svc.Info(1, info) && info.difficulty == dif);
        MatchAbortOutcome const a = svc.Abort(1, Interrupt::Gm);
        CHECK(a.ended && a.end.difficulty == dif && KvOf(a.payload).at("dif") == dif);
        CHECK(KvOf(svc.Get(1)).at("dif") == dif); // also after the end
    }
    p = FixtureParams(1, 4244);
    p.difficulty = "easy";
    s = svc.Start(p, 0);
    CHECK(!s.ok && s.error.find("difficulty") != std::string::npos);
    p.difficulty = "";
    CHECK(!svc.Start(p, 0).ok);

    // A Hard match with the encounter overrides starts (the engine applies them or, in the simple
    // shim, ignores them) and plays to its end without a refused AI action.
    p = FixtureParams(2, 99);
    p.difficulty = "boss";
    p.profile = HM::AiProfile::Hard;
    p.opponentHeroHealth = 40;
    p.opponentHeroArmor = 3;
    s = svc.Start(p, 0);
    CHECK(s.ok);
    MatchEnd end;
    bool ended = false;
    std::vector<std::string> payloads = PlayOut(svc, 2, s.matchId, end, ended);
    CHECK(ended && end.difficulty == "boss" && svc.AiRefusals() == 0);
    CHECK(!payloads.empty() && KvOf(payloads.back()).at("dif") == "boss");

    // Determinism: the same setup, profile and human actions give the same payloads, push for push.
    for (HM::AiProfile profile : { HM::AiProfile::Normal, HM::AiProfile::Hard })
    {
        std::vector<std::string> runs[2];
        for (int run = 0; run < 2; ++run)
        {
            MatchService det(300);
            det.SetCardDb(TestCardDb());
            det.SetAiDelayMs(1000);
            MatchStartParams q = FixtureParams(5, 31337);
            q.profile = profile;
            q.difficulty = profile == HM::AiProfile::Hard ? "hard" : "normal";
            MatchStartResult const started = det.Start(q, 0);
            CHECK(started.ok);
            runs[run].push_back(started.payload);
            MatchEnd e;
            bool done = false;
            std::vector<std::string> more = PlayOut(det, 5, started.matchId, e, done);
            runs[run].insert(runs[run].end(), more.begin(), more.end());
            CHECK(done);
        }
        CHECK(runs[0] == runs[1] && runs[0].size() > 10);
    }
}


// The encounter start state travels through MatchService::Start (MatchStartParams -> MatchSetup): boards, the
// weapon, the extra hand card, the forced first side and a deck-less side; the match plays out.
static void TestMatchServiceEncounterStart()
{
    MatchService svc(950);
    svc.SetCardDb(TestCardDb());
    svc.SetAiDelayMs(1000);
    for (int first : { 0, 1, -1 })
    {
        MatchStartParams p = FixtureParams(11, 60000 + static_cast<uint32_t>(first + 1));
        p.difficulty = "boss";
        p.profile = HM::AiProfile::Hard;
        p.opponentStart.board = { "CS2_168", "CS2_168" };
        p.opponentStart.weapon = "CS2_106";
        p.humanStart.board = { "CS2_168" };
        p.humanStart.hand = { "CS2_029" };
        p.firstSide = first;
        MatchStartResult const s = svc.Start(p, 0);
        CHECK(s.ok);
        std::map<std::string, std::string> const kv = KvOf(s.payload);
        // The boards and the weapon are in the first payload (mb / ob: boards, ow: the opponent weapon).
        CHECK(kv.at("ph") == "mull" && kv.count("mb") && kv.count("ob"));
        CHECK(kv.at("ob").find("CS2_168") != std::string::npos && kv.at("mb").find("CS2_168") != std::string::npos);
        CHECK(kv.at("ow").find("CS2_106") != std::string::npos);
        MatchEnd end;
        bool ended = false;
        PlayOut(svc, 11, s.matchId, end, ended);
        CHECK(ended && svc.AiRefusals() == 0);
    }
    // A side without a deck that draws is refused (the engine's text), and so is an unsupported start card.
    MatchStartParams q = FixtureParams(12, 5);
    q.opponentDeck.clear();
    CHECK(!svc.Start(q, 0).ok);
    q.opponentStart.noDraw = true;
    q.difficulty = "boss";
    q.profile = HM::AiProfile::Hard;
    MatchStartResult s = svc.Start(q, 0);
    CHECK(s.ok);
    MatchEnd end;
    bool ended = false;
    PlayOut(svc, 12, s.matchId, end, ended);
    CHECK(ended && svc.AiRefusals() == 0);
    MatchStartParams r = FixtureParams(13, 6);
    r.humanStart.hand = { "EX1_323" }; // Lord Jaraxxus: not supported
    s = svc.Start(r, 0);
    CHECK(!s.ok && s.error.find("EX1_323") != std::string::npos && !svc.Has(13));
}

// Nozdormu (EX1_560). The human's turn ends 15 s after the later of the turn's start and the moment the
// limit appeared; the AI's turns are not timed (its actions are paced). The injected clock is the nowMs of Start /
// Act / Get / Tick.
static void TestMatchServiceNozdormuTimer()
{
    using TA = HM::TestAccess;
    std::shared_ptr<HM::CardDb const> db(&MatchAllCards::Db(), [](HM::CardDb const*) {});
    CHECK(db->Supported("EX1_560"));
    MatchService svc(1200);
    svc.SetCardDb(db);
    svc.SetAiDelayMs(10000);
    uint32_t const guid = 9;
    MatchStartResult const started = svc.Start(FixtureParams(guid, 777), 0);
    CHECK(started.ok);
    if (!started.ok)
        return;
    std::string const mid = "mid=" + std::to_string(started.matchId) + ";seq=";
    uint64_t now = 0;
    auto act = [&](std::string const& rest, uint64_t t) {
        return svc.Act(guid, Req(mid + std::to_string(U(KvOf(svc.Get(guid, t)), "seq")) + ";" + rest), t);
    };
    auto field = [&](char const* key, uint64_t t) { return KvOf(svc.Get(guid, t)).at(key); };
    // Steps the clock until it is the human's turn again (the AI delay is 10 s, a Nozdormu turn lasts 15 s).
    auto toHuman = [&]() {
        for (int guard = 0; guard < 60 && field("act", now) != "m"; ++guard)
        {
            now += 5000;
            svc.Tick(now);
        }
        CHECK(field("act", now) == "m");
    };

    CHECK(KvOf(started.payload).at("tmr") == "0"); // the mulligan has no timer
    GameActOutcome o = act("a=mull;r=", now);
    CHECK(o.ok);
    toHuman();

    // No Nozdormu: no timer, however long the human thinks.
    CHECK(field("tmr", now) == "0" && field("ph", now) == "play");
    uint32_t const turn0 = U(KvOf(svc.Get(guid, now)), "turn");
    now += 1000000;
    CHECK(svc.Tick(now).empty() && U(KvOf(svc.Get(guid, now)), "turn") == turn0);

    HM::Match* m = svc.MatchForTests(guid);
    CHECK(m && m->TurnTimeLimitSec() == 0);
    if (!m)
        return;

    // A Nozdormu played 4 s into the turn starts a fresh 15 s from that moment (not from the turn's start).
    std::vector<HM::EntityId> const hand = TA::SetHand(*m, 0, { "EX1_560" });
    TA::SetMana(*m, 0, 9, 9);
    uint64_t const tPlay = now + 4000;
    o = act("a=play;s=" + std::to_string(hand[0]), tPlay);
    CHECK(o.ok && m->TurnTimeLimitSec() == 15);
    CHECK(KvOf(o.payload).at("tmr") == "15");
    CHECK(field("tmr", tPlay + 4200) == "11"); // 10.8 s left, rounded up
    CHECK(field("tmr", tPlay + 14999) == "1");
    CHECK(svc.Tick(tPlay + 14999).empty()); // the 15 s of the turn's start would be over, the new ones are not
    std::vector<MatchPush> pushes = svc.Tick(tPlay + 15000);
    CHECK(pushes.size() == 1);
    if (pushes.size() != 1)
        return;
    std::map<std::string, std::string> kv = KvOf(pushes[0].payload);
    CHECK(pushes[0].guid == guid && pushes[0].note.empty() && !pushes[0].ended);
    CHECK(kv.at("act") == "o" && U(kv, "turn") == turn0 + 1 && kv.at("tmr") == "0"); // the AI's turn is untimed
    CHECK(kv.at("ev").find("turn.o.") != std::string::npos);

    // The AI's turn is not timed: with 10 s between its actions it plays on past 15 s.
    uint64_t const tAi = tPlay + 15000;
    TA::SetHand(*m, 1, std::vector<std::string>(7, "CS2_231"));
    TA::SetMana(*m, 1, 10, 10);
    CHECK(svc.Tick(tAi + 9999).empty());
    pushes = svc.Tick(tAi + 10000);
    CHECK(pushes.size() == 1 && KvOf(pushes[0].payload).at("act") == "o");
    CHECK(svc.Tick(tAi + 19999).empty()); // past 15 s: still the AI's turn, nothing forced
    CHECK(field("act", tAi + 19999) == "o" && field("tmr", tAi + 19999) == "0");
    pushes = svc.Tick(tAi + 20000);
    CHECK(pushes.size() == 1 && KvOf(pushes[0].payload).at("act") == "o"); // its second action, still its turn
    now = tAi + 20000;
    toHuman();
    CHECK(field("tmr", now) == "15" && U(KvOf(svc.Get(guid, now)), "turn") == turn0 + 2);

    // A pending pick (Tracking) of the human is answered with its first option, then the turn ends.
    HM::EntityId const tracking = TA::SetHand(*m, 0, { "DS1_184" })[0];
    TA::SetMana(*m, 0, 9, 9);
    o = act("a=play;s=" + std::to_string(tracking), now + 1000);
    CHECK(o.ok);
    kv = KvOf(o.payload);
    CHECK(!kv.at("pk").empty() && kv.at("tmr") == "14"); // the turn started 1 s earlier
    pushes = svc.Tick(now + 14999);
    CHECK(pushes.empty());
    pushes = svc.Tick(now + 15000);
    CHECK(pushes.size() == 1);
    if (pushes.size() != 1)
        return;
    kv = KvOf(pushes[0].payload);
    CHECK(kv.at("pk").empty() && kv.at("act") == "o" && U(kv, "turn") == turn0 + 3);
    CHECK(kv.at("ev").find("pick.m.") != std::string::npos && kv.at("ev").find("turn.o.") != std::string::npos);
    now += 15000;
    toHuman();

    // A silenced Nozdormu gives no timer: silencing it mid-turn removes the deadline.
    CHECK(field("tmr", now) == "15");
    std::vector<std::string> const board = TA::BoardCardIds(*m, 0);
    std::size_t nozdormu = board.size();
    for (std::size_t i = 0; i < board.size(); ++i)
        if (board[i] == "EX1_560")
            nozdormu = i;
    CHECK(nozdormu < board.size());
    if (nozdormu == board.size())
        return;
    HM::EntityId const silence = TA::SetHand(*m, 0, { "EX1_332" })[0];
    o = act("a=play;s=" + std::to_string(silence) + ";t=" + std::to_string(TA::BoardIds(*m, 0)[nozdormu]), now + 2000);
    CHECK(o.ok && m->TurnTimeLimitSec() == 0 && KvOf(o.payload).at("tmr") == "0");
    uint32_t const turn1 = U(KvOf(svc.Get(guid, now + 2000)), "turn");
    now += 500000;
    CHECK(svc.Tick(now).empty() && U(KvOf(svc.Get(guid, now)), "turn") == turn1);

    // A Nozdormu on the AI's board limits the human's turns too (the AI's own turns stay untimed).
    TA::SetBoard(*m, 1, { "EX1_560" });
    CHECK(m->TurnTimeLimitSec() == 15);
    o = act("a=end", now);
    CHECK(o.ok && KvOf(o.payload).at("tmr") == "0" && KvOf(o.payload).at("act") == "o");
    toHuman();
    CHECK(field("tmr", now) == "15" && U(KvOf(svc.Get(guid, now)), "turn") > turn1 + 1);
}

namespace
{
    HM::CardDef OppCard(std::string const& id, char const* cls, HM::CardType type, char const* rarity)
    {
        HM::CardDef d;
        d.id = id;
        d.name = "Card " + id;
        d.cardClass = cls;
        d.type = type;
        d.rarity = rarity;
        d.cost = 1;
        if (type == HM::CardType::Minion)
        {
            d.attack = 1;
            d.health = 1;
        }
        if (type == HM::CardType::Weapon)
        {
            d.attack = 1;
            d.durability = 2;
        }
        return d;
    }

    // N01..N20 neutral, M01..M10 Mage, W01..W10 Warrior (plain minions, so the engine supports them),
    // TLEG a neutral Legendary, TSPELL a Mage spell without a script (not supported), the Mage hero and
    // hero power.
    HM::CardDb const& OppDb()
    {
        static HM::CardDb const db = []
        {
            HM::CardDb d;
            char buf[16];
            for (int i = 1; i <= 20; ++i)
            {
                std::snprintf(buf, sizeof buf, "T_N%02d", i);
                d.Add(OppCard(buf, "NEUTRAL", HM::CardType::Minion, "COMMON"));
            }
            for (int i = 1; i <= 10; ++i)
            {
                std::snprintf(buf, sizeof buf, "T_M%02d", i);
                d.Add(OppCard(buf, "MAGE", HM::CardType::Minion, "COMMON"));
                std::snprintf(buf, sizeof buf, "T_W%02d", i);
                d.Add(OppCard(buf, "WARRIOR", HM::CardType::Minion, "COMMON"));
            }
            d.Add(OppCard("T_LEG", "NEUTRAL", HM::CardType::Minion, "LEGENDARY"));
            d.Add(OppCard("T_SPELL", "MAGE", HM::CardType::Spell, "COMMON"));
            d.Add(OppCard("T_AXE", "WARRIOR", HM::CardType::Weapon, "COMMON"));
            d.Add(OppCard("HERO_08", "MAGE", HM::CardType::Hero, ""));
            d.Add(OppCard("CS2_034", "MAGE", HM::CardType::HeroPower, ""));
            return d;
        }();
        return db;
    }

    // "T_<prefix>01:2,...": count cards of one prefix with two copies each.
    std::string Cards(char const* prefix, int from, int to, int copies = 2)
    {
        std::string out;
        char buf[32];
        for (int i = from; i <= to; ++i)
        {
            std::snprintf(buf, sizeof buf, "%s%02d:%d", prefix, i, copies);
            out += (out.empty() ? "" : ",") + std::string(buf);
        }
        return out;
    }

    LibraryDeckRow Row(char const* key, char const* cls, std::string const& cards)
    {
        LibraryDeckRow r;
        r.key = key;
        r.name = std::string("Deck ") + key;
        r.deckClass = cls;
        r.cards = cards;
        return r;
    }

    std::string MageDeck() { return Cards("T_M", 1, 6) + "," + Cards("T_N", 1, 9); }
    std::string WarriorDeck() { return Cards("T_W", 1, 6) + "," + Cards("T_N", 1, 9); }
    // 12 + 16 + 1 + 1 = 30 cards with one Legendary.
    std::string HardMageDeck() { return Cards("T_M", 1, 6) + "," + Cards("T_N", 1, 8) + ",T_N09:1,T_LEG:1"; }

    OpponentRow ORow(uint32_t entry, char const* difficulty, char const* deck)
    {
        OpponentRow r;
        r.entry = entry;
        r.difficulty = difficulty;
        r.deckKey = deck;
        return r;
    }

    bool HasError(std::vector<std::string> const& errors, std::string const& part)
    {
        for (std::string const& e : errors)
            if (e.find(part) != std::string::npos)
                return true;
        return false;
    }
}

static void TestLibraryDeckParsing()
{
    DeckCards cards;
    std::string error;
    CHECK(ParseLibraryCards("A:2,B:1", cards, error) && cards.size() == 2 && cards["A"] == 2 && cards["B"] == 1);
    CHECK(ParseLibraryCards(" A : 2 , B:1 ", cards, error) && cards.size() == 2);
    CHECK(ParseLibraryCards("EX1_011:2", cards, error) && cards["EX1_011"] == 2);
    CHECK(ParseLibraryCards("A:30", cards, error) && cards["A"] == 30);
    char const* const bad[] = { "", " ", "A", "A:", ":2", "A:0", "A:x", "A:1000", "A:-1", "A:2,", ",A:2", "A:2,,B:1", "A:2,A:1",
                                "A.2", "A:2;B:1", "A B:2", "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA:1", "A:1:2" };
    for (char const* text : bad)
    {
        bool const ok = ParseLibraryCards(text, cards, error);
        CHECK(!ok && cards.empty() && !error.empty());
        if (ok)
            std::printf("    accepted: '%s'\n", text);
    }
}

static void TestLibraryDeckChecks()
{
    HM::CardDb const& db = OppDb();
    auto check = [&](char const* cls, std::string const& cards, bool supported = true)
    {
        DeckCards parsed;
        std::string error;
        CHECK(ParseLibraryCards(cards, parsed, error));
        return CheckLibraryDeck(db, cls, parsed, supported);
    };
    CHECK(check("MAGE", MageDeck()).empty());
    CHECK(check("WARRIOR", WarriorDeck()).empty());
    CHECK(check("MAGE", Cards("T_N", 1, 15)).empty());                         // neutral cards fit every class
    CHECK(HasError({ check("MAGE", Cards("T_M", 1, 6) + "," + Cards("T_N", 1, 8) + ",T_LEG:1") }, "29 cards"));
    CHECK(check("MAGE", HardMageDeck()).empty());
    CHECK(HasError({ check("MAGE", Cards("T_N", 1, 14) + ",T_N15:1") }, "29 cards"));
    CHECK(HasError({ check("MAGE", Cards("T_N", 1, 16)) }, "32 cards"));
    CHECK(HasError({ check("PIRATE", MageDeck()) }, "not one of the nine classes"));
    CHECK(HasError({ check("", MageDeck()) }, "not one of the nine classes"));
    CHECK(HasError({ check("mage", MageDeck()) }, "not one of the nine classes")); // exact upper case
    CHECK(HasError({ check("MAGE", Cards("T_M", 1, 6) + "," + Cards("T_N", 1, 8) + ",NOPE:2") }, "unknown card NOPE"));
    CHECK(HasError({ check("MAGE", Cards("T_M", 1, 5) + "," + Cards("T_N", 1, 9) + ",HERO_08:2") }, "cannot be in a deck"));
    CHECK(HasError({ check("MAGE", Cards("T_M", 1, 5) + "," + Cards("T_N", 1, 9) + ",T_W01:2") }, "belongs to WARRIOR"));
    CHECK(HasError({ check("WARRIOR", MageDeck()) }, "belongs to MAGE"));
    CHECK(HasError({ check("MAGE", Cards("T_M", 1, 5) + ",T_M06:3," + Cards("T_N", 1, 8) + ",T_N09:1") }, "3 copies of Card T_M06"));
    CHECK(HasError({ check("MAGE", Cards("T_M", 1, 5) + "," + Cards("T_N", 1, 9) + ",T_LEG:2") }, "2 copies of Card T_LEG (T_LEG) (the limit is 1)"));
    // Supported: T_SPELL has no script. requireSupported = false skips only that rule.
    std::string const withSpell = Cards("T_M", 1, 5) + "," + Cards("T_N", 1, 9) + ",T_SPELL:2";
    CHECK(HasError({ check("MAGE", withSpell) }, "not supported by the match engine"));
    CHECK(check("MAGE", withSpell, false).empty());
    // A weapon without text is supported.
    CHECK(check("WARRIOR", Cards("T_W", 1, 5) + "," + Cards("T_N", 1, 9) + ",T_AXE:2").empty());
}

static void TestDeckLibraryBuild()
{
    HM::CardDb const& db = OppDb();
    std::vector<LibraryDeckRow> rows;
    rows.push_back(Row("normal_mage", "MAGE", MageDeck()));
    rows.push_back(Row("broken_size", "MAGE", Cards("T_N", 1, 14)));
    rows.push_back(Row("hard_mage", "MAGE", HardMageDeck()));
    rows.push_back(Row("broken_parse", "MAGE", "T_M01=2"));
    rows.push_back(Row("bad key!", "MAGE", MageDeck()));
    rows.push_back(Row("normal_mage", "WARRIOR", WarriorDeck())); // the key is listed twice
    rows.push_back(Row("broken_spell", "MAGE", Cards("T_M", 1, 5) + "," + Cards("T_N", 1, 9) + ",T_SPELL:2"));
    rows.push_back(Row("", "MAGE", MageDeck()));
    rows.push_back(Row("a_long_key_a_long_key_a_long_key_a_", "MAGE", MageDeck()));
    std::vector<std::string> errors;
    DeckLibrary const lib = DeckLibrary::Build(db, rows, errors);
    CHECK(lib.Decks().size() == 5 && lib.ReadyCount() == 2);
    std::vector<std::string> keys;
    for (LibraryDeck const& d : lib.Decks())
        keys.push_back(d.key);
    CHECK(keys == std::vector<std::string>({ "broken_parse", "broken_size", "broken_spell", "hard_mage", "normal_mage" }));
    LibraryDeck const* mage = lib.Find("normal_mage");
    CHECK(mage && mage->ready && mage->problem.empty() && mage->deckClass == "MAGE" && mage->ids.size() == 30 &&
          mage->name == "Deck normal_mage" && std::is_sorted(mage->ids.begin(), mage->ids.end()));
    CHECK(lib.Find("hard_mage") && lib.Find("hard_mage")->ready);
    CHECK(lib.Find("broken_size") && !lib.Find("broken_size")->ready && lib.Find("broken_size")->ids.empty() &&
          lib.Find("broken_size")->problem.find("28 cards") != std::string::npos);
    CHECK(lib.Find("broken_parse") && lib.Find("broken_parse")->problem.find("cannot be read") != std::string::npos);
    CHECK(lib.Find("broken_spell") && lib.Find("broken_spell")->problem.find("not supported") != std::string::npos);
    CHECK(!lib.Find("bad key!") && !lib.Find("") && !lib.Find("nope"));
    // Every deck that was skipped or dropped is described (once).
    CHECK(errors.size() == 7);
    CHECK(HasError(errors, "'broken_size' skipped") && HasError(errors, "'broken_parse' skipped") &&
          HasError(errors, "'broken_spell' skipped") && HasError(errors, "'bad key!'") && HasError(errors, "listed twice"));
    CHECK(!HasError(errors, "'normal_mage' skipped") && !HasError(errors, "'hard_mage' skipped"));
}

static void TestOpponentRegistry()
{
    HM::CardDb const& db = OppDb();
    std::vector<LibraryDeckRow> rows;
    rows.push_back(Row("normal_mage", "MAGE", MageDeck()));
    rows.push_back(Row("hard_mage", "MAGE", HardMageDeck()));
    rows.push_back(Row("normal_warrior", "WARRIOR", WarriorDeck()));
    rows.push_back(Row("alt_mage", "MAGE", Cards("T_M", 1, 5) + "," + Cards("T_N", 11, 20)));
    rows.push_back(Row("broken", "MAGE", Cards("T_N", 1, 14)));
    std::vector<std::string> libErrors;
    DeckLibrary const lib = DeckLibrary::Build(db, rows, libErrors);
    CHECK(lib.ReadyCount() == 4);

    std::vector<OpponentRow> dbRows;
    // 100: a Normal pool of three decks and a Hard deck with the encounter overrides.
    dbRows.push_back(ORow(100, "normal", "normal_mage"));
    dbRows.push_back(ORow(100, "normal", "normal_warrior"));
    dbRows.push_back(ORow(100, "normal", "alt_mage"));
    dbRows.push_back(ORow(100, "hard", "hard_mage"));
    dbRows.back().heroHealth = 40;
    dbRows.back().heroArmor = 5;
    dbRows.back().heroPower = "CS2_034";
    dbRows.back().heroCard = "HERO_08";
    // 200: a boss (its Normal row is ignored).
    dbRows.push_back(ORow(200, "boss", "hard_mage"));
    dbRows.back().heroHealth = 120;
    dbRows.push_back(ORow(200, "normal", "normal_mage"));
    // 300: only Normal. 301: only Hard.
    dbRows.push_back(ORow(300, "normal", "normal_mage"));
    dbRows.push_back(ORow(301, "hard", "hard_mage"));
    // Rows that are skipped.
    dbRows.push_back(ORow(400, "normal", "nope"));          // unknown deck: the entry is dropped
    dbRows.push_back(ORow(401, "normal", "broken"));        // not ready: dropped
    dbRows.push_back(ORow(402, "easy", "normal_mage"));     // bad difficulty
    dbRows.push_back(ORow(403, "normal", "normal_mage"));   // health out of range ...
    dbRows.back().heroHealth = 500;
    dbRows.push_back(ORow(403, "hard", "hard_mage"));       // ... the other row stays
    dbRows.push_back(ORow(404, "normal", "normal_mage"));
    dbRows.back().heroCard = "T_N01";                       // not a hero
    dbRows.push_back(ORow(405, "normal", "normal_mage"));
    dbRows.back().heroPower = "T_N01";                      // not a hero power
    dbRows.push_back(ORow(406, "normal", "normal_mage"));
    dbRows.back().heroArmor = 201;
    dbRows.push_back(ORow(407, "normal", "normal_mage"));
    dbRows.back().heroHealth = -1;

    std::vector<MatchOpponent> config;
    auto cfg = [&config](uint32_t entry, char const* cls)
    {
        MatchOpponent o;
        o.entry = entry;
        o.deckClass = cls;
        config.push_back(o);
    };
    cfg(800, "MAGE");     // normal_mage and hard_mage
    cfg(801, "WARRIOR");  // only normal_warrior exists
    cfg(802, "ROGUE");    // no deck at all: dropped
    cfg(100, "WARRIOR");  // the database rows win
    cfg(400, "MAGE");     // the rows of 400 were all unusable: the config does not replace them

    std::vector<std::string> errors;
    OpponentRegistry const reg = OpponentRegistry::Build(db, lib, dbRows, config, errors);
    CHECK(reg.Entries() == std::vector<uint32_t>({ 100, 200, 300, 301, 403, 800, 801 }));
    CHECK(reg.Count() == 7 && reg.Has(100) && !reg.Has(400) && !reg.Has(802) && !reg.Has(999));

    using D = Difficulty;
    CHECK(reg.Offered(100) == std::vector<D>({ D::Normal, D::Hard }));
    CHECK(reg.Offered(200) == std::vector<D>({ D::Boss }) && reg.IsBoss(200) && !reg.IsBoss(100));
    CHECK(reg.Offered(300) == std::vector<D>({ D::Normal }));
    CHECK(reg.Offered(301) == std::vector<D>({ D::Hard }));
    CHECK(reg.Offered(403) == std::vector<D>({ D::Hard }));
    CHECK(reg.Offered(800) == std::vector<D>({ D::Normal, D::Hard }));
    CHECK(reg.Offered(801) == std::vector<D>({ D::Normal }));
    CHECK(reg.Offered(999).empty());
    CHECK(reg.PoolSize(100, D::Normal) == 3 && reg.PoolSize(100, D::Hard) == 1 && reg.PoolSize(100, D::Boss) == 0 &&
          reg.PoolSize(200, D::Boss) == 1 && reg.PoolSize(200, D::Normal) == 0 && reg.PoolSize(999, D::Normal) == 0);

    // Errors: one line per skipped row / ignored config / dropped entry.
    CHECK(HasError(errors, "opponent 400") && HasError(errors, "no library deck with this key"));
    CHECK(HasError(errors, "opponent 401") && HasError(errors, "the deck is not ready"));
    CHECK(HasError(errors, "opponent 402") && HasError(errors, "normal, hard or boss"));
    CHECK(HasError(errors, "hero_health must be 1..200") && HasError(errors, "hero_armor must be 0..200"));
    CHECK(HasError(errors, "hero_card T_N01 is not a hero card") && HasError(errors, "hero_power T_N01"));
    CHECK(HasError(errors, "has boss rows"));
    CHECK(HasError(errors, "opponent 100: the config entry is ignored"));
    CHECK(HasError(errors, "opponent 400: the config entry is ignored"));
    CHECK(HasError(errors, "opponent 801 (config): no library deck 'hard_warrior'"));
    CHECK(HasError(errors, "opponent 802 (config) dropped"));

    // Pick: uniform over the pool with the given rng, deterministic per seed.
    std::mt19937 rng(7);
    OpponentPick pick;
    std::map<std::string, int> seen;
    for (int i = 0; i < 300; ++i)
    {
        CHECK(reg.Pick(100, D::Normal, rng, pick));
        ++seen[pick.deckKey];
        CHECK(pick.difficulty == D::Normal && pick.profile == HM::AiProfile::Normal && pick.deck.size() == 30 &&
              pick.overrides.heroHealth == 0 && pick.overrides.heroArmor == 0 && pick.overrides.heroCardId.empty() &&
              pick.overrides.heroPowerId.empty());
        CHECK((pick.deckKey == "normal_mage" && pick.deckClass == "MAGE") || (pick.deckKey == "normal_warrior" && pick.deckClass == "WARRIOR") ||
              (pick.deckKey == "alt_mage" && pick.deckClass == "MAGE"));
    }
    CHECK(seen.size() == 3);
    for (auto const& entry : seen)
        CHECK(entry.second > 60); // about 100 each
    std::mt19937 a(99);
    std::mt19937 b(99);
    for (int i = 0; i < 50; ++i)
    {
        OpponentPick x;
        OpponentPick y;
        CHECK(reg.Pick(100, D::Normal, a, x) && reg.Pick(100, D::Normal, b, y) && x.deckKey == y.deckKey && x.deck == y.deck);
    }
    // A one deck pool does not consume the rng; Hard picks carry the overrides and the Hard AI.
    std::mt19937 untouched(5);
    std::mt19937 reference(5);
    CHECK(reg.Pick(100, D::Hard, untouched, pick));
    CHECK(pick.deckKey == "hard_mage" && pick.profile == HM::AiProfile::Hard && pick.difficulty == D::Hard &&
          pick.overrides.heroHealth == 40 && pick.overrides.heroArmor == 5 && pick.overrides.heroPowerId == "CS2_034" &&
          pick.overrides.heroCardId == "HERO_08" && untouched == reference);
    CHECK(reg.Pick(200, D::Boss, rng, pick) && pick.profile == HM::AiProfile::Hard && pick.difficulty == D::Boss &&
          pick.overrides.heroHealth == 120 && pick.deckKey == "hard_mage");
    CHECK(!reg.Pick(200, D::Normal, rng, pick) && !reg.Pick(100, D::Boss, rng, pick) && !reg.Pick(300, D::Hard, rng, pick) &&
          !reg.Pick(999, D::Normal, rng, pick) && !reg.Pick(400, D::Normal, rng, pick));
    // The config syntax: normal_<class> / hard_<class>, no overrides.
    CHECK(reg.Pick(800, D::Hard, rng, pick) && pick.deckKey == "hard_mage" && pick.overrides.heroHealth == 0);
    CHECK(reg.Pick(801, D::Normal, rng, pick) && pick.deckKey == "normal_warrior" && !reg.Pick(801, D::Hard, rng, pick));
    // 403 kept its valid row only.
    CHECK(reg.Pick(403, D::Hard, rng, pick) && pick.deckKey == "hard_mage" && !reg.Pick(403, D::Normal, rng, pick));

    // Difficulty helpers.
    Difficulty d = D::Boss;
    CHECK(ParseDifficulty("normal", d) && d == D::Normal && ParseDifficulty("hard", d) && d == D::Hard &&
          ParseDifficulty("boss", d) && d == D::Boss);
    CHECK(!ParseDifficulty("Normal", d) && !ParseDifficulty("", d) && !ParseDifficulty("easy", d));
    CHECK(std::string(DifficultyCode(D::Hard)) == "hard" && std::string(DifficultyName(D::Boss)) == "Boss");
    CHECK(AiProfileFor(D::Normal) == HM::AiProfile::Normal && AiProfileFor(D::Hard) == HM::AiProfile::Hard &&
          AiProfileFor(D::Boss) == HM::AiProfile::Hard);
}

// The match-finder entries and their opponent names.

// A boss row whose deck_key names an encounter (HsEncounters.h) plays the encounter: deck, hero, start state.
// The table is the test's own (the real one has no ready encounter before its cards are scripted).
static void TestOpponentRegistryEncounters()
{
    HM::CardDb const& db = OppDb();
    std::vector<LibraryDeckRow> rows;
    rows.push_back(Row("normal_mage", "MAGE", MageDeck()));
    std::vector<std::string> libErrors;
    DeckLibrary const lib = DeckLibrary::Build(db, rows, libErrors);

    Encounter good;
    good.key = "test_boss";
    good.name = "Test Boss";
    good.heroClass = "MAGE";
    good.heroCardId = "HERO_08";
    good.heroHealth = 60;
    good.heroArmor = 4;
    good.heroPowerId = "CS2_034";
    for (int i = 1; i <= 15; ++i)
    {
        char buf[16];
        std::snprintf(buf, sizeof buf, "T_N%02d", i);
        good.deck.push_back({ buf, 2 });
    }
    good.boss.board = { "T_N01", "T_N02" };
    good.human.hand = { "T_N03" };
    good.first = EncounterFirst::Human;
    Encounter unready = good;
    unready.key = "test_unready";
    unready.deck[0].id = "T_SPELL"; // a Mage spell without a script
    Encounter broken = good;
    broken.key = "test_broken";
    broken.deck[0].count = 5; // 33 cards
    Encounter empty = good;
    empty.key = "test_empty";
    empty.deck.clear();
    empty.boss.noDraw = true;
    CHECK(CheckEncounter(db, good, true).empty() && CheckEncounter(db, empty, true).empty());
    CHECK(CheckEncounter(db, unready, false).empty() && CheckEncounter(db, unready, true).find("not supported") != std::string::npos);
    CHECK(CheckEncounter(db, broken, false).find("33 cards") != std::string::npos);
    std::vector<Encounter> const table = { good, unready, broken, empty };

    std::vector<OpponentRow> dbRows;
    dbRows.push_back(ORow(900, "boss", "test_boss"));
    dbRows.push_back(ORow(901, "boss", "test_unready"));
    dbRows.push_back(ORow(902, "boss", "test_boss"));
    dbRows.back().heroHealth = 10;                          // the encounter owns the hero columns
    dbRows.push_back(ORow(903, "normal", "test_boss"));     // only a boss row may name an encounter
    dbRows.push_back(ORow(904, "boss", "test_empty"));
    dbRows.push_back(ORow(905, "boss", "test_broken"));
    dbRows.push_back(ORow(906, "boss", "normal_mage"));     // a boss with a library deck, as before
    dbRows.back().heroHealth = 120;
    dbRows.push_back(ORow(907, "boss", "test_boss"));       // two rows of one entry: the encounter and a deck
    dbRows.push_back(ORow(907, "boss", "normal_mage"));
    std::vector<std::string> errors;
    OpponentRegistry const reg = OpponentRegistry::Build(db, lib, dbRows, std::vector<MatchOpponent>(), std::vector<uint32_t>(),
                                                          std::vector<NpcUnlockRow>(), table, errors);
    CHECK(reg.Entries() == std::vector<uint32_t>({ 900, 904, 906, 907 }));
    CHECK(reg.IsBoss(900) && reg.Offered(900) == std::vector<Difficulty>({ Difficulty::Boss }));
    CHECK(HasError(errors, "opponent 901") && HasError(errors, "the encounter is not ready") && HasError(errors, "T_SPELL"));
    CHECK(HasError(errors, "opponent 902") && HasError(errors, "leaves hero_health"));
    CHECK(HasError(errors, "opponent 903") && HasError(errors, "no library deck with this key"));
    CHECK(HasError(errors, "opponent 905") && HasError(errors, "33 cards"));

    std::mt19937 rng(5);
    OpponentPick pick;
    CHECK(reg.Pick(900, Difficulty::Boss, rng, pick));
    CHECK(pick.encounterKey == "test_boss" && pick.deckKey == "test_boss" && pick.deckName == "Test Boss" &&
          pick.deckClass == "MAGE" && pick.difficulty == Difficulty::Boss && pick.profile == HM::AiProfile::Hard);
    CHECK(pick.deck.size() == 30 && std::is_sorted(pick.deck.begin(), pick.deck.end()));
    CHECK(pick.overrides.heroHealth == 60 && pick.overrides.heroArmor == 4 && pick.overrides.heroCardId == "HERO_08" &&
          pick.overrides.heroPowerId == "CS2_034");
    CHECK(reg.Pick(904, Difficulty::Boss, rng, pick) && pick.encounterKey == "test_empty" && pick.deck.empty());
    // A boss with a library deck has no encounter; the entry with both rows picks either.
    CHECK(reg.Pick(906, Difficulty::Boss, rng, pick) && pick.encounterKey.empty() && pick.deckKey == "normal_mage" &&
          pick.overrides.heroHealth == 120);
    int encounters = 0;
    for (int i = 0; i < 100; ++i)
    {
        CHECK(reg.Pick(907, Difficulty::Boss, rng, pick));
        encounters += pick.encounterKey.empty() ? 0 : 1;
    }
    CHECK(encounters > 20 && encounters < 80);

    // ApplyEncounter on the pick's encounter fills the start parameters the match service reads.
    MatchStartParams params;
    ApplyEncounter(good, params);
    CHECK(params.difficulty == "boss" && params.profile == HM::AiProfile::Hard && params.firstSide == 0 &&
          params.opponentStart.board == std::vector<std::string>({ "T_N01", "T_N02" }) &&
          params.humanStart.hand == std::vector<std::string>({ "T_N03" }) && params.opponentDeck.size() == 30);

    // The default Build uses the real table, whose cards OppDb does not have: every row naming one is refused.
    std::vector<OpponentRow> realRows;
    for (Encounter const& e : Encounters())
        realRows.push_back(ORow(7000 + static_cast<uint32_t>(realRows.size()), "boss", e.key.c_str()));
    std::vector<std::string> realErrors;
    OpponentRegistry const real = OpponentRegistry::Build(db, lib, realRows, std::vector<MatchOpponent>(), realErrors);
    CHECK(real.Count() == 0 && realErrors.size() >= Encounters().size());
    CHECK(HasError(realErrors, "naxx_anubrekhan") && HasError(realErrors, "the encounter is not ready"));
}

static void TestFinderRegistry()
{
    HM::CardDb const& db = OppDb();
    std::vector<LibraryDeckRow> rows;
    auto add = [&rows](char const* key, char const* cls, std::string const& cards, char const* kind, char const* finder)
    {
        rows.push_back(Row(key, cls, cards));
        rows.back().kind = kind;
        rows.back().finder = finder;
    };
    add("normal_mage", "MAGE", MageDeck(), "opponent", "none");
    add("starter_mage", "MAGE", MageDeck(), "starter", "normal");
    add("lib_mage_c01", "MAGE", Cards("T_M", 1, 5) + "," + Cards("T_N", 11, 20), "library", "normal");
    add("lib_warrior_c01", "WARRIOR", WarriorDeck(), "library", "normal");
    add("lib_mage_a01", "MAGE", HardMageDeck(), "library", "hard");
    add("lib_mage_a02", "MAGE", Cards("T_N", 1, 14), "library", "hard"); // not ready: never picked
    add("npc_someone", "WARRIOR", WarriorDeck(), "npc", "none");
    std::vector<std::string> libErrors;
    DeckLibrary const lib = DeckLibrary::Build(db, rows, libErrors);
    CHECK(lib.Find("starter_mage") && lib.Find("starter_mage")->kind == "starter" && lib.Find("starter_mage")->finder == "normal");
    CHECK(StarterDeckKey("MAGE") == "starter_mage" && StarterDeckKey("WARRIOR") == "starter_warrior");

    std::vector<OpponentRow> dbRows;
    dbRows.push_back(ORow(100, "normal", "normal_mage"));
    dbRows.push_back(ORow(500, "normal", "normal_mage")); // a finder: its rows are ignored
    std::vector<MatchOpponent> config;
    MatchOpponent o;
    o.entry = 501;
    o.deckClass = "MAGE";
    config.push_back(o); // a finder: the config entry is ignored too
    std::vector<std::string> errors;
    OpponentRegistry const reg = OpponentRegistry::Build(db, lib, dbRows, config, { 500, 501 }, errors);
    using D = Difficulty;
    CHECK(reg.Entries() == std::vector<uint32_t>({ 100, 500, 501 }));
    CHECK(reg.IsFinder(500) && reg.IsFinder(501) && !reg.IsFinder(100) && !reg.IsFinder(999) && !reg.IsBoss(500));
    CHECK(reg.Offered(500) == std::vector<D>({ D::Normal, D::Hard }));
    CHECK(reg.PoolSize(500, D::Normal) == 3 && reg.PoolSize(500, D::Hard) == 1 && reg.PoolSize(501, D::Normal) == 3);
    CHECK(HasError(errors, "opponent 500: its hearthstone_opponent rows are ignored"));
    CHECK(HasError(errors, "opponent 501: the config entry is ignored, the creature is a match-finder"));

    std::mt19937 rng(11);
    OpponentPick pick;
    std::set<std::string> normal;
    for (int i = 0; i < 200; ++i)
    {
        CHECK(reg.Pick(500, D::Normal, rng, pick) && pick.profile == HM::AiProfile::Normal && pick.deck.size() == 30);
        normal.insert(pick.deckKey);
    }
    CHECK(normal == std::set<std::string>({ "lib_mage_c01", "lib_warrior_c01", "starter_mage" }));
    CHECK(reg.Pick(500, D::Hard, rng, pick) && pick.deckKey == "lib_mage_a01" && pick.profile == HM::AiProfile::Hard);

    // A finder without any finder deck is dropped.
    std::vector<LibraryDeckRow> none;
    none.push_back(Row("normal_mage", "MAGE", MageDeck()));
    DeckLibrary const plain = DeckLibrary::Build(db, none, libErrors);
    errors.clear();
    OpponentRegistry const empty = OpponentRegistry::Build(db, plain, {}, {}, { 600 }, errors);
    CHECK(!empty.Has(600) && HasError(errors, "match-finder 600 dropped"));
    // The old Build has no finders.
    errors.clear();
    CHECK(!OpponentRegistry::Build(db, lib, dbRows, config, errors).IsFinder(500));

    // Names: alliance rows + any rows for an Alliance character, horde rows + any for a Horde one.
    std::vector<FinderName> names;
    auto name = [&names](uint32_t entry, char const* n, char const* faction)
    {
        FinderName f;
        f.entry = entry;
        f.name = n;
        f.faction = faction;
        names.push_back(f);
    };
    name(1, "Innkeeper Allison", "alliance");
    name(2, "Innkeeper Gryshka", "horde");
    name(3, "Baron Revilgaz", "any");
    name(4, "", "alliance"); // no name: never picked
    std::set<std::string> alliance;
    std::set<std::string> horde;
    for (int i = 0; i < 200; ++i)
    {
        alliance.insert(PickFinderName(names, true, rng));
        horde.insert(PickFinderName(names, false, rng));
    }
    CHECK(alliance == std::set<std::string>({ "Innkeeper Allison", "Baron Revilgaz" }));
    CHECK(horde == std::set<std::string>({ "Innkeeper Gryshka", "Baron Revilgaz" }));
    CHECK(PickFinderName({}, true, rng).empty());
    names.erase(names.begin() + 2);
    names.erase(names.begin());
    CHECK(PickFinderName(names, true, rng).empty()); // only the empty alliance name is left
}

// The quest NPCs in the registry, the finder picks that include unlocked ones, the account unlocks.
namespace
{
    struct FakeNpcUnlockStorage : NpcUnlockStorage
    {
        std::map<uint32_t, std::map<uint32_t, uint32_t>> rows;
        std::vector<std::string> sources;
        int loads = 0;
        bool failLoad = false;
        bool failCommit = false;

        bool Load(uint32_t accountId, std::map<uint32_t, uint32_t>& out) override
        {
            ++loads;
            if (failLoad)
                return false;
            out = rows[accountId];
            return true;
        }

        bool Commit(uint32_t accountId, uint32_t entry, uint32_t questId, std::string const& source) override
        {
            if (failCommit)
                return false;
            rows[accountId].insert(std::make_pair(entry, questId)); // INSERT IGNORE
            sources.push_back(source);
            return true;
        }
    };
}

static void TestQuestNpcs()
{
    HM::CardDb const& db = OppDb();
    std::vector<LibraryDeckRow> rows;
    auto add = [&rows](char const* key, char const* cls, std::string const& cards, char const* kind, char const* finder)
    {
        rows.push_back(Row(key, cls, cards));
        rows.back().kind = kind;
        rows.back().finder = finder;
    };
    add("lib_mage_c01", "MAGE", MageDeck(), "library", "normal");
    add("lib_mage_a01", "MAGE", HardMageDeck(), "library", "hard");
    add("npc_gryan", "WARRIOR", WarriorDeck(), "npc", "none");
    add("npc_mankrik", "WARRIOR", WarriorDeck(), "npc", "none");
    add("npc_broken", "MAGE", Cards("T_N", 1, 14), "npc", "none");
    std::vector<std::string> libErrors;
    DeckLibrary const lib = DeckLibrary::Build(db, rows, libErrors);

    auto unlock = [](uint32_t entry, uint32_t quest, char const* deck, char const* faction)
    {
        NpcUnlockRow u;
        u.entry = entry;
        u.questId = quest;
        u.deckKey = deck;
        u.faction = faction;
        return u;
    };
    std::vector<NpcUnlockRow> unlocks = {
        unlock(234, 166, "npc_gryan", "alliance"),
        unlock(3432, 4921, "npc_mankrik", "horde"),
        unlock(900, 1, "npc_broken", "horde"),     // not ready: skipped
        unlock(901, 0, "npc_gryan", "horde"),      // no quest: skipped
        unlock(902, 2, "npc_gryan", "neutral"),    // bad faction: skipped
        unlock(903, 3, "npc_nope", "horde"),       // unknown deck: skipped
    };
    std::vector<OpponentRow> dbRows = { ORow(234, "normal", "lib_mage_c01") }; // ignored: a quest NPC
    std::vector<MatchOpponent> config;
    MatchOpponent o;
    o.entry = 3432;
    o.deckClass = "MAGE";
    config.push_back(o); // ignored: a quest NPC
    std::vector<std::string> errors;
    // 3432 is also listed as a finder: the quest NPC wins.
    OpponentRegistry const reg = OpponentRegistry::Build(db, lib, dbRows, config, { 500, 3432 }, unlocks, errors);
    using D = Difficulty;
    CHECK(reg.Entries() == std::vector<uint32_t>({ 234, 500, 3432 }));
    CHECK(reg.UnlockQuest(234) == 166 && reg.UnlockQuest(3432) == 4921 && reg.UnlockQuest(500) == 0 && reg.UnlockQuest(9) == 0);
    CHECK(!reg.IsFinder(3432) && reg.IsFinder(500));
    CHECK(reg.Offered(234) == std::vector<D>({ D::Normal, D::Hard }));
    std::mt19937 rng(3);
    OpponentPick pick;
    CHECK(reg.Pick(234, D::Normal, rng, pick) && pick.deckKey == "npc_gryan" && pick.profile == HM::AiProfile::Normal);
    CHECK(reg.Pick(234, D::Hard, rng, pick) && pick.deckKey == "npc_gryan" && pick.profile == HM::AiProfile::Hard);
    std::vector<OpponentRegistry::UnlockNpc> const npcs = reg.UnlockNpcs();
    CHECK(npcs.size() == 2 && npcs[0].entry == 234 && npcs[0].questId == 166 && npcs[0].faction == "alliance" &&
          npcs[1].entry == 3432 && npcs[1].faction == "horde");
    CHECK(HasError(errors, "match-finder 3432 ignored: the creature is a quest NPC"));
    CHECK(HasError(errors, "opponent 234: its hearthstone_opponent rows are ignored, the creature is a quest NPC"));
    CHECK(HasError(errors, "opponent 3432: the config entry is ignored, the creature is a quest NPC"));
    CHECK(HasError(errors, "quest NPC 900 skipped") && HasError(errors, "quest NPC 901 skipped") &&
          HasError(errors, "quest NPC 902 skipped") && HasError(errors, "quest NPC 903 skipped"));

    // The finder: without unlocked NPCs only its own pool; with one, that NPC sometimes, under its entry.
    uint32_t npcEntry = 7;
    CHECK(reg.PickFinder(500, D::Normal, {}, rng, pick, npcEntry) && npcEntry == 0 && pick.deckKey == "lib_mage_c01");
    std::map<uint32_t, int> seen;
    for (int i = 0; i < 400; ++i)
    {
        CHECK(reg.PickFinder(500, D::Normal, { 234, 234, 77, 500 }, rng, pick, npcEntry)); // repeats and non-NPCs ignored
        ++seen[npcEntry];
        CHECK((npcEntry == 0 && pick.deckKey == "lib_mage_c01") || (npcEntry == 234 && pick.deckKey == "npc_gryan"));
    }
    CHECK(seen.size() == 2 && seen[0] > 120 && seen[234] > 120); // about half each
    CHECK(reg.PickFinder(500, D::Hard, { 234 }, rng, pick, npcEntry) && pick.profile == HM::AiProfile::Hard);
    CHECK(!reg.PickFinder(234, D::Normal, {}, rng, pick, npcEntry)); // not a finder

    // The account unlocks: reconcile is idempotent, only completed quests count, the cache follows online accounts.
    FakeNpcUnlockStorage storage;
    NpcUnlockService svc(storage);
    svc.SetOnline(5);
    std::vector<uint32_t> added;
    CHECK(!svc.IsUnlocked(5, 234));
    CHECK(svc.Reconcile(5, npcs, { 4921, 999 }, "login:1", added) && added == std::vector<uint32_t>({ 3432 }));
    CHECK(svc.IsUnlocked(5, 3432) && !svc.IsUnlocked(5, 234) && storage.sources == std::vector<std::string>({ "login:1" }));
    CHECK(svc.Reconcile(5, npcs, { 4921, 166 }, "reward:1", added) && added == std::vector<uint32_t>({ 234 }));
    CHECK(svc.Reconcile(5, npcs, { 4921, 166 }, "talk:1", added) && added.empty() && storage.sources.size() == 2);
    std::map<uint32_t, uint32_t> got;
    CHECK(svc.Get(5, got) && got.size() == 2 && got[234] == 166);
    int const loads = storage.loads;
    CHECK(svc.IsUnlocked(5, 234) && storage.loads == loads); // cached while online
    // Another account is separate; an offline account is read every time and never cached.
    CHECK(!svc.IsUnlocked(6, 234) && storage.loads == loads + 1 && !svc.IsUnlocked(6, 234) && storage.loads == loads + 2);
    // A failed commit: false, nothing added, the next call retries.
    storage.failCommit = true;
    CHECK(!svc.Reconcile(6, npcs, { 166 }, "login:2", added) && added.empty());
    storage.failCommit = false;
    CHECK(svc.Reconcile(6, npcs, { 166 }, "login:2", added) && added == std::vector<uint32_t>({ 234 }));
    storage.failLoad = true;
    svc.SetOffline(5);
    CHECK(!svc.IsUnlocked(5, 234) && !svc.Reconcile(5, npcs, { 166 }, "x", added) && !svc.Get(5, got));
    storage.failLoad = false;
    CHECK(svc.IsUnlocked(5, 234)); // the rows are still there
}

static void CheckClassQuestRoster(uint32_t entry)
{
    Hs::ClassQuestOpponent const* steeg = Hs::ClassQuestOpponentOf(entry);
    CHECK(steeg && steeg->questId == (entry == kHsWarriorVeteran ? kHsWarriorWarGames :
        entry == kHsHordeMage ? kHsHordeMageDuel :
        entry == kHsMitchellJones ? kHsAllianceMageDuel :
        entry == kHsHordeDruid ? kHsHordeDruidDuel :
        entry == kHsAllianceDruid ? kHsAllianceDruidDuel :
        entry == kHsHordeRogue ? kHsHordeRogueDuel :
        entry == kHsAllianceRogue ? kHsAllianceRogueDuel :
        entry == kHsHordePriest ? kHsHordePriestDuel :
        entry == kHsAlenya ? kHsAlliancePriestDuel :
        entry == kHsAllianceShaman ? kHsAllianceShamanDuel :
        entry == kHsAnnieSpirit ? kHsAnnieDuel :
        entry == kHsSirRowan ? kHsAlliancePaladinDuel :
        entry == kHsHordePaladin ? kHsHordePaladinDuel : kHsHordeWarriorDuel));
    if (!steeg)
        return;
    CHECK(HsClassQuestDirectAllowed(*steeg, steeg->alliance, true));
    CHECK(!HsClassQuestDirectAllowed(*steeg, steeg->alliance, false)); // complete, rewarded or absent quest
    CHECK(!HsClassQuestDirectAllowed(*steeg, !steeg->alliance, true)); // wrong faction
    CHECK(!Hs::ClassQuestOpponentOf(234)); // an archive NPC does not use the class-quest rule
    Hs::ClassQuestOpponent const futureHorde = { 919507, 919733, false };
    CHECK(HsClassQuestDirectAllowed(futureHorde, false, true));
    CHECK(!HsClassQuestDirectAllowed(futureHorde, true, true));
    CHECK(!HsClassQuestDirectAllowed(futureHorde, false, false));

    std::vector<LibraryDeckRow> rows = {
        Row("lib_mage_c01", "MAGE", MageDeck()),
        Row("lib_mage_a01", "MAGE", HardMageDeck()),
        Row("normal_warrior", "WARRIOR", WarriorDeck()),
        Row("normal_mage", "MAGE", MageDeck()),
        Row("normal_druid", "DRUID", Cards("T_N", 1, 15)),
        Row("normal_rogue", "ROGUE", Cards("T_N", 1, 15)),
        Row("normal_priest", "PRIEST", Cards("T_N", 1, 15)),
        Row("normal_paladin", "PALADIN", Cards("T_N", 1, 15)),
        Row("normal_shaman", "SHAMAN", Cards("T_N", 1, 15)),
    };
    rows[0].kind = rows[1].kind = "library";
    rows[0].finder = "normal";
    rows[1].finder = "hard";
    std::vector<std::string> errors;
    DeckLibrary const library = DeckLibrary::Build(OppDb(), rows, errors);
    CHECK(errors.empty());
    NpcUnlockRow unlock;
    unlock.entry = steeg->entry;
    unlock.questId = steeg->questId;
    unlock.deckKey = (entry == kHsHordeMage || entry == kHsMitchellJones) ? "normal_mage" :
        (entry == kHsHordeDruid || entry == kHsAllianceDruid) ? "normal_druid" :
        (entry == kHsHordeRogue || entry == kHsAllianceRogue) ? "normal_rogue" :
        (entry == kHsHordePriest || entry == kHsAlenya) ? "normal_priest" :
        (entry == kHsAllianceShaman || entry == kHsAnnieSpirit) ? "normal_shaman" :
        (entry == kHsHordePaladin || entry == kHsSirRowan) ? "normal_paladin" : "normal_warrior";
    unlock.faction = steeg->alliance ? "alliance" : "horde";
    OpponentRegistry const registry = OpponentRegistry::Build(OppDb(), library, {}, {}, { 500 }, { unlock }, errors);
    CHECK(errors.empty() && registry.UnlockQuest(steeg->entry) == steeg->questId);

    FakeNpcUnlockStorage storage;
    NpcUnlockService service(storage);
    std::vector<uint32_t> added;
    CHECK(service.Reconcile(5, registry.UnlockNpcs(), {}, "login", added) && added.empty());
    CHECK(!service.IsUnlocked(5, steeg->entry));
    std::mt19937 rng(42);
    OpponentPick pick;
    uint32_t npcEntry = 0;
    for (Difficulty difficulty : { Difficulty::Normal, Difficulty::Hard })
        for (int i = 0; i < 30; ++i)
            CHECK(registry.PickFinder(500, difficulty, {}, rng, pick, npcEntry) && npcEntry == 0);

    // The turn-in unlocks the roster account-wide and is idempotent; another account remains locked.
    CHECK(service.Reconcile(5, registry.UnlockNpcs(), { steeg->questId }, "reward", added) &&
        added == std::vector<uint32_t>({ steeg->entry }));
    CHECK(service.IsUnlocked(5, steeg->entry) && !service.IsUnlocked(6, steeg->entry));
    CHECK(service.Reconcile(5, registry.UnlockNpcs(), { steeg->questId }, "login", added) && added.empty());
    std::map<uint32_t, uint32_t> unlocked;
    CHECK(service.Get(5, unlocked) && unlocked.count(steeg->entry));
    std::vector<uint32_t> roster;
    for (OpponentRegistry::UnlockNpc const& npc : registry.UnlockNpcs())
        if (npc.faction == unlock.faction && unlocked.count(npc.entry))
            roster.push_back(npc.entry);
    for (Difficulty difficulty : { Difficulty::Normal, Difficulty::Hard })
    {
        bool sawSteeg = false;
        bool sawLibrary = false;
        for (int i = 0; i < 100; ++i)
        {
            CHECK(registry.PickFinder(500, difficulty, roster, rng, pick, npcEntry));
            if (npcEntry == steeg->entry)
            {
                sawSteeg = true;
                CHECK(pick.deckKey == unlock.deckKey && pick.profile == AiProfileFor(difficulty));
            }
            else
            {
                sawLibrary = true;
                CHECK(npcEntry == 0);
            }
        }
        CHECK(sawSteeg && sawLibrary);
    }
    CHECK(!HsClassQuestDirectAllowed(*steeg, steeg->alliance, false)); // roster access never opens direct rematches
}

static void TestAllianceShamanWalkingScenes()
{
    // Cover the native floor projection case: a smooth path can finish well
    // above the player-authored Z. Arrival belongs to that real endpoint.
    CHECK(!HsAllianceShamanArrived(true, 1.5f));
    CHECK(HsAllianceShamanPathEndpointAllowed(0.0f, 1.5f));
    CHECK(HsAllianceShamanArrived(true, 0.0f));
    HsAllianceShamanWalkState walk;
    walk.stop=5; walk.moving=true; walk.elapsed=2500;
    CHECK(!walk.Arrive(1, false, 0.0f));
    CHECK(!walk.Arrive(1, true, 1.5f));
    CHECK(walk.next==0 && walk.moving && !walk.paused);
    CHECK(walk.Arrive(1, true, 0.0f)); // actual native endpoint, not recorded Z
    CHECK(walk.next==1 && walk.last==1 && !walk.moving && !walk.paused && walk.elapsed==0);
    CHECK(!walk.Arrive(1, true, 0.0f)); // one arrival cannot skip a point
    for (uint32_t label : {2u,3u})
    {
        walk.moving=true;
        CHECK(walk.Arrive(label, true, 0.2f));
        CHECK(!walk.paused && !walk.moving && walk.next==label);
    }
    walk.moving=true;
    CHECK(walk.Arrive(4, true, 0.0f));
    CHECK(walk.next==4 && walk.last==4 && !walk.paused);
    walk.moving=true;
    CHECK(walk.Arrive(5, true, 0.0f));
    CHECK(walk.next==5 && walk.last==5 && walk.paused);
    walk.stop=6; walk.paused=false; walk.moving=true;
    CHECK(walk.Arrive(6, true, 0.0f));
    CHECK(walk.next==6 && walk.last==6 && walk.paused); // Krip's exchange, not the keg
    // Consecutive route labels and the final home stop remain ordered.
    walk.stop=0; walk.paused=false;
    for (uint32_t label : {7u,8u,9u,0u})
    {
        walk.moving=true;
        CHECK(walk.Arrive(label, true, 0.0f));
        CHECK(walk.paused==(label==0));
    }
    CHECK(walk.next==10 && walk.last==0 && !walk.moving);
    CHECK(HsAllianceShamanPathEndpointAllowed(1.0f, 3.0f));
    CHECK(!HsAllianceShamanPathEndpointAllowed(1.01f, 0.0f));
    CHECK(!HsAllianceShamanPathEndpointAllowed(0.0f, 3.01f)); // wrong floor
    CHECK(!HsAllianceShamanPathEndpointAllowed(-1.0f, 0.0f));
    CHECK(!HsAllianceShamanPathEndpointAllowed(0.0f, -1.0f));
    CHECK(HsAllianceShamanActorAllowed(kHsAllianceShaman, false));
    CHECK(HsAllianceShamanActorAllowed(5111, false));
    for (uint32_t visitor : {kHsAllianceShamanWildhammer, kHsAllianceShamanTauren})
    {
        CHECK(!HsAllianceShamanActorAllowed(visitor, false)); // setup copy
        CHECK(HsAllianceShamanActorAllowed(visitor, true)); // quest summon
    }
    CHECK(HsAllianceShamanActionAllowed(0, kHsAllianceShamanActionFirst, true, false, false));
    CHECK(!HsAllianceShamanActionAllowed(0, kHsAllianceShamanActionFirst+1, true, false, false));
    CHECK(!HsAllianceShamanActionAllowed(0, kHsAllianceShamanActionFirst+2, true, false, false));
    CHECK(!HsAllianceShamanArrived(false, 0.0f)); // interrupted spline at a stop
    CHECK(!HsAllianceShamanArrived(true, 10.0f)); // truncated movement far away
    CHECK(!HsAllianceShamanArrived(true, -1.0f));
    CHECK(HsAllianceShamanArrived(true, 0.8f));
    CHECK(!HsAllianceShamanArrived(true, 0.81f));
    for (uint32_t step = 0; step <= 4; ++step)
        for (uint32_t flags = 0; flags < 8; ++flags)
            CHECK(HsAllianceShamanSpeechAllowed(step, flags&1, flags&2, flags&4) ==
                (step < 4 && (flags&1) && (!(step&1) || (flags&6)==6)));
    CHECK(kHsAllianceShamanStages[0].johnStop==5);
    CHECK(kHsAllianceShamanStages[1].johnStop==5);
    CHECK(kHsAllianceShamanStages[2].johnStop==6);
    CHECK(kHsAllianceShamanStages[3].johnStop==6);
    for (uint32_t step=0; step<4; ++step)
    {
        auto const& stage=kHsAllianceShamanStages[step];
        CHECK(stage.visitorStop==6u);
        CHECK(stage.visitorExit==(step<2?9u:10u));
        CHECK(HsAllianceShamanSpawnAllowed(step,true));
        CHECK(HsAllianceShamanSpawnAllowed(step,false));
        CHECK(!stage.spawnAfterJohnArrival);
        CHECK(stage.spawnDelay==(step<2?2000u:1500u));
        CHECK(HsAllianceShamanActionAllowed(step,kHsAllianceShamanActionFirst+step*4,true,false,false));
        CHECK(!HsAllianceShamanActionAllowed(step,kHsAllianceShamanActionFirst+step*4+1,true,false,false));
        CHECK(!HsAllianceShamanActionAllowed(step,kHsAllianceShamanActionFirst+step*4+2,true,false,false));
    }
    CHECK(!HsAllianceShamanSpawnAllowed(4,true));
    CHECK(!HsAllianceShamanSpawnAllowed(UINT32_MAX,true));
    CHECK(HsAllianceShamanReservedInteractionAllowed(true, true, false, false));
    CHECK(HsAllianceShamanReservedInteractionAllowed(true, false, true, true));
    CHECK(!HsAllianceShamanReservedInteractionAllowed(true, false, true, false));
    CHECK(!HsAllianceShamanReservedInteractionAllowed(true, false, false, true));
    for (bool gossip : {false, true})
        for (bool idle : {false, true})
            for (bool ready : {false, true})
                CHECK(!HsAllianceShamanReservedInteractionAllowed(false, idle, ready, gossip));
    // Runtime route labels are consecutive even when source DB paths were sparse.
    std::vector<uint32_t> john;
    for (auto const& p : kHsShamanJohnRoute) john.push_back(p.label);
    CHECK(john == std::vector<uint32_t>({1,2,3,4,5,6,7,8,9}));
    bool kiblerStop=false, kiblerExit=false, kripStop=false, kripExit=false;
    for (auto const& p : kHsShamanKiblerRoute) {kiblerStop |= p.label==6; kiblerExit |= p.label==9;}
    for (auto const& p : kHsShamanKripRoute) {kripStop |= p.label==6; kripExit |= p.label==10;}
    CHECK(kiblerStop && kiblerExit && kripStop && kripExit);
    Hs::MatchEnd end;
    end.meta.opponentEntry=kHsAllianceShaman; end.res="win"; end.why="hero";
    CHECK(HsClassQuestVictory(end));
    for (char const* reason : {"concede","disconnect","timeout","death"})
    { end.why=reason; CHECK(!HsClassQuestVictory(end)); }
    end.why="hero";
    for (char const* result : {"loss","draw","interrupted"})
    { end.res=result; CHECK(!HsClassQuestVictory(end)); }
    end.res="win"; end.meta.opponentEntry=kHsAllianceShamanWildhammer;
    CHECK(!HsClassQuestVictory(end));
}

static void TestAllianceShamanProgress()
{
    for (uint32_t counter = 0; counter < 2; ++counter)
    {
        uint32_t const expected = counter ? 4 : 0;
        CHECK(HsAllianceShamanNextStep(counter) == expected);
        for (uint32_t action = kHsAllianceShamanActionFirst - 1;
             action <= kHsAllianceShamanActionLast + 1; ++action)
            for (uint32_t flags = 0; flags < 8; ++flags)
            {
                bool const offered = (flags & 1) != 0;
                bool const active = (flags & 2) != 0;
                bool const ready = (flags & 4) != 0;
                bool const inRange = action >= kHsAllianceShamanActionFirst &&
                    action <= kHsAllianceShamanActionLast;
                uint32_t const offset = inRange ? action - kHsAllianceShamanActionFirst : 0;
                bool const allowed = offered && expected < 4 && inRange && offset / 4 == expected &&
                    (offset % 4 == 3 ? active && ready : !active && offset % 4 == 0);
                CHECK(HsAllianceShamanActionAllowed(expected, action, offered, active, ready) == allowed);
            }
    }
    for (uint32_t flags = 0; flags < 64; ++flags)
        for (uint32_t level : { 0u, 9u, 10u, 60u })
        {
            CHECK(HsAllianceShamanReviewAllowed((flags & 1) != 0, level,
                (flags & 2) != 0, (flags & 4) != 0, (flags & 8) != 0, (flags & 16) != 0) ==
                ((flags & 31) == 15 && level >= 10));
        }
    CHECK(!HsAllianceShamanActionAllowed(UINT32_MAX, kHsAllianceShamanActionFirst, true, false, false));
    CHECK(!HsAllianceShamanActionAllowed(0, UINT32_MAX, true, false, false));
}

static void TestAllianceShamanSceneClock()
{
    HsAllianceShamanSceneClock clock;
    CHECK(!clock.Ready(4));
    CHECK(!clock.Advance(4999, 5000, 4));
    CHECK(clock.line == 0);
    CHECK(clock.Advance(1, 5000, 4));
    CHECK(clock.line == 1 && clock.elapsed == 0);
    // Even a very late update emits at most one following line.
    CHECK(clock.Advance(UINT32_MAX, 6000, 4));
    CHECK(clock.line == 2 && clock.elapsed == 0);
    CHECK(!clock.Advance(5999, 6000, 4));
    CHECK(clock.Advance(1, 6000, 4));
    CHECK(!clock.Ready(4));
    // The final line still needs its full pause before acknowledgement opens.
    CHECK(!clock.Advance(4999, 5000, 4));
    CHECK(clock.Advance(1, 5000, 4));
    CHECK(clock.Ready(4));
    CHECK(!clock.Advance(UINT32_MAX, 5000, 4));
    CHECK(clock.line == 4 && clock.elapsed == 0);
    // A cancelled unfinished scene starts fresh; completed native steps live
    // outside this clock and are not cleared by playback cleanup.
    clock = {};
    CHECK(clock.line == 0 && clock.elapsed == 0 && !clock.Ready(4));
    CHECK(!clock.Advance(UINT32_MAX, 5000, 0));
    CHECK(!clock.Ready(0));
}

static void TestHordePaladinScene()
{
    HsHordePaladinSceneClock clock;
    CHECK(!clock.Ready());
    CHECK(!clock.Advance(0)); // Queue time/world tick before the actual shout is not elapsed scene time.
    CHECK(!clock.Advance(4999));
    CHECK(clock.Advance(1));
    CHECK(clock.Ready());
    CHECK(clock.Advance(UINT32_MAX)); // Saturates; long world ticks cannot overflow.
    // The protest begins a separate pause after "Silence."; the first wait must not count twice.
    clock = {};
    CHECK(!clock.Ready());
    CHECK(!clock.Advance(0));
    CHECK(!clock.Advance(4999));
    CHECK(clock.Advance(1));
    CHECK(clock.Ready());
    CHECK(clock.Advance(UINT32_MAX));
    for (uint32_t page = 0; page < 4; ++page)
        for (bool active : {false, true})
            for (bool ready : {false, true})
                CHECK(HsHordePaladinDemonstrationActionAllowed(page, active, ready) ==
                    ((page < 2 && !active) || (page == 2 && active && ready)));
    // No acknowledgement before the five-second barrier; starts cannot reset a running/ready scene.
    CHECK(!HsHordePaladinDemonstrationActionAllowed(2, true, false));
    CHECK(!HsHordePaladinDemonstrationActionAllowed(1, true, false));
    CHECK(!HsHordePaladinDemonstrationActionAllowed(1, true, true));
    // Native quest accept/turn-in packets cannot bypass either timer or the protest.
    // Between timers only gossip is allowed; another player/NPC is independent.
    for (bool watching : {false, true})
        for (bool ready : {false, true})
            for (bool gossip : {false, true})
                CHECK(HsHordePaladinInteractionAllowed(watching, ready, gossip) ==
                    (!watching || (ready && gossip)));
    CHECK(!HsHordePaladinInteractionAllowed(true, false, true));
    CHECK(!HsHordePaladinInteractionAllowed(true, false, false));
    CHECK(!HsHordePaladinInteractionAllowed(true, true, false));
    CHECK(HsHordePaladinInteractionAllowed(true, true, true));
    HsHordePaladinSceneClock retry;
    CHECK(!retry.Ready()); // Death/leaving/relog/abandon recreates an unfinished scene.
    CHECK(retry.Advance(UINT32_MAX));
}

static void TestHordePaladinReadings()
{
    CHECK(HsHordePaladinSceneQuest(0) == kHsHordePaladinResearch);
    CHECK(HsHordePaladinSceneQuest(1) == kHsHordePaladinResearch);
    CHECK(HsHordePaladinSceneQuest(3) == kHsHordePaladinRehearsal);
    CHECK(!HsHordePaladinSceneQuest(2));
    CHECK(!HsHordePaladinSceneQuest(UINT32_MAX));
    for (uint32_t page = 0; page < 4; ++page)
    {
        CHECK(HsHordePaladinReadingActionAllowed(page, false) == (page < 2));
        CHECK(!HsHordePaladinReadingActionAllowed(page, true)); // Duplicate starts and old acknowledgements.
    }
    HsHordePaladinReadingClock clock;
    CHECK(!clock.Advance(0, 5500, 3)); // Exclude the tick before the first spoken passage.
    CHECK(!clock.Advance(5499, 5500, 3));
    CHECK(clock.line == 0);
    CHECK(clock.Advance(1, 5500, 3));
    CHECK(clock.line == 1 && clock.elapsed == 0);
    CHECK(!clock.Done(3));
    CHECK(clock.Advance(UINT32_MAX, 8000, 3)); // One passage per stalled tick; no rapid catch-up speech.
    CHECK(clock.line == 2 && clock.elapsed == 0);
    CHECK(!clock.Done(3));
    CHECK(!clock.Advance(0, 5000, 3));
    CHECK(!clock.Advance(4999, 5000, 3)); // Credit waits for the final spoken passage's full pause.
    CHECK(!clock.Done(3));
    CHECK(clock.Advance(1, 5000, 3));
    CHECK(clock.Done(3));
    CHECK(!clock.Advance(UINT32_MAX, 5000, 3)); // Finished clock cannot repeat completion.
    clock = {}; // Interrupted scene is recreated; native counters retain earlier completed objectives.
    CHECK(clock.line == 0 && !clock.Done(3));
    CHECK(!clock.Advance(5499, 5500, 3));
    CHECK(clock.Advance(1, 5500, 3));
}

static void TestHordePaladinRules()
{
    // Saved native states: closing/death/relog preserve the next missing step;
    // abandon/reaccept resets both. Later progress cannot skip a missing first step.
    for (uint32_t mask = 0; mask < 4; ++mask)
    {
        std::array<uint32_t, 2> const counters = { mask & 1, (mask >> 1) & 1 };
        uint32_t const next = !(mask & 1) ? 0 : !(mask & 2) ? 1 : 2;
        CHECK(HsHordePaladinNextStep(counters) == next);
        for (uint32_t offset : { 0u, 2u })
            for (uint32_t action = kHsHordePaladinActionFirst - 1; action <= kHsHordePaladinActionLast + 1; ++action)
            {
                bool const expected = next < 2 && action >= kHsHordePaladinActionFirst &&
                    action <= kHsHordePaladinActionLast &&
                    (action - kHsHordePaladinActionFirst) / 3 == next + offset;
                CHECK(HsHordePaladinActionAllowed(next == 2 ? 4 : next + offset, action, true) == expected);
                CHECK(!HsHordePaladinActionAllowed(next + offset, action, false));
            }
    }
    for (uint32_t flags = 0; flags < 32; ++flags)
        for (uint32_t level : { 1u, 9u, 10u, 60u })
            CHECK(HsHordePaladinLessonAllowed((flags & 1) != 0, level, (flags & 2) != 0,
                (flags & 4) != 0, (flags & 8) != 0, (flags & 16) != 0) ==
                ((flags & 23) == 23 && !(flags & 8) && level >= 10));
    Hs::MatchEnd end;
    end.meta.opponentEntry = kHsHordePaladin;
    end.res = "win";
    end.why = "hero";
    CHECK(HsClassQuestVictory(end));
    for (char const* reason : { "concede", "disconnect", "timeout", "death" })
    {
        end.why = reason;
        CHECK(!HsClassQuestVictory(end));
    }
    end.why = "hero";
    for (char const* result : { "loss", "draw", "interrupted" })
    {
        end.res = result;
        CHECK(!HsClassQuestVictory(end));
    }
    end.res = "win";
    end.meta.opponentEntry = kHsHordePaladinCredits[0];
    CHECK(!HsClassQuestVictory(end));
}

static void TestAlliancePaladinQuestFlow()
{
    // Native counters survive closing/relog. A later saved credit cannot skip a hole,
    // and the unused third slot cannot prevent completion of Tess's two-step lesson.
    for (uint32_t mask = 0; mask < 8; ++mask)
    {
        std::array<uint32_t, 3> const counters = {mask & 1, (mask >> 1) & 1, (mask >> 2) & 1};
        uint32_t const first = !(mask & 1) ? 0 : !(mask & 2) ? 1 : !(mask & 4) ? 2 : 3;
        CHECK(HsAlliancePaladinNextStep(counters, 2) == std::min(first, 2u));
    }
    CHECK(HsAlliancePaladinNextStep({2, 7, 4}, 2) == 2);
    CHECK(HsAlliancePaladinNextStep({0, 0, 0}, 2) == 0); // Abandon/reaccept starts afresh.
    CHECK(HsAlliancePaladinNextStep({1, 1, 1}, 4) == 3); // Invalid objective count is bounded.
    for (uint32_t flags = 0; flags < 32; ++flags)
        for (uint32_t level : {0u, 9u, 10u, 60u})
            CHECK(HsAlliancePaladinLessonAllowed((flags & 1) != 0, level, (flags & 2) != 0,
                (flags & 4) != 0, (flags & 8) != 0, (flags & 16) != 0) ==
                (flags == 23 && level >= 10));

    // Item delivery has no gossip credit. Only Tess's invitation and Rowan's
    // report offer replies; all old card-study actions remain invalid.
    std::array<uint32_t, 2> const allowedPages = {0xe, 0x2};
    std::array<uint32_t, 2> const pageCounts = {4, 2};
    std::array<uint32_t, 2> const speakers = {kHsSquireTess, kHsSirRowan};
    for (uint32_t exchange = 0; exchange < 4; ++exchange)
    {
        CHECK(HsAlliancePaladinPageCount(exchange) == (exchange < 2 ? pageCounts[exchange] : 0));
        for (uint32_t entry : {kHsSirRowan, kHsSquireTess, kHsAdaSparkplugz, kHsMaren, 0u})
            CHECK(HsAlliancePaladinExchangeCreature(exchange, entry) ==
                (exchange < 2 && entry == speakers[exchange]));
        for (uint32_t action = kHsAlliancePaladinRetiredActionFirst - 1;
            action <= kHsAlliancePaladinActionLast + 1; ++action)
        {
            bool expected = false;
            if (exchange < 2 && action >= kHsAlliancePaladinActionFirst && action <= kHsAlliancePaladinActionLast)
            {
                uint32_t const offset = action - kHsAlliancePaladinActionFirst;
                expected = offset / 4 == exchange && (allowedPages[exchange] & (1u << (offset % 4))) != 0;
            }
            CHECK(HsAlliancePaladinActionAllowed(exchange, action, true) == expected);
            CHECK(!HsAlliancePaladinActionAllowed(exchange, action, false)); // Stale or unoffered reply.
        }
    }
    CHECK(!HsAlliancePaladinActionAllowed(0, UINT32_MAX, true));
    CHECK(!HsAlliancePaladinExchangeCreature(UINT32_MAX, kHsSirRowan));
    CHECK(HsAlliancePaladinPageCount(UINT32_MAX) == 0);

    std::array<uint32_t, 3> invitation = {0, 0, 0};
    for (uint32_t page = 1; page <= 3; ++page)
    {
        CHECK(HsAlliancePaladinNextStep(invitation, 2) == 0);
        CHECK(HsAlliancePaladinActionAllowed(0, kHsAlliancePaladinActionFirst + page, true));
        if (page == 3) invitation[0] = 1; // Only "Come and learn with us" saves Tess's agreement.
    }
    CHECK(HsAlliancePaladinNextStep(invitation, 2) == 1);
    CHECK(!HsAlliancePaladinExchangeCreature(1, kHsSquireTess)); // Only Rowan hears the report.
    CHECK(!HsAlliancePaladinActionAllowed(1, kHsAlliancePaladinActionFirst + 3, true)); // Old Tess acknowledgement.
    CHECK(HsAlliancePaladinActionAllowed(1, kHsAlliancePaladinActionFirst + 5, true));
    invitation[1] = 1;
    CHECK(HsAlliancePaladinNextStep(invitation, 2) == 2);
    CHECK(!HsAlliancePaladinActionAllowed(2, kHsAlliancePaladinActionFirst + 5, true));

    auto const* teacher = Hs::ClassQuestOpponentOf(kHsSirRowan);
    CHECK(teacher && teacher->questId == kHsAlliancePaladinDuel && teacher->alliance);
    CHECK(Hs::ClassQuestOpponentOf(kHsSquireTess) == nullptr);
    CHECK(Hs::ClassQuestOpponentOf(kHsAdaSparkplugz) == nullptr);
    CHECK(kHsAlliancePaladinCredits.size() == 2);
    for (uint32_t credit : kHsAlliancePaladinCredits)
        CHECK(Hs::ClassQuestOpponentOf(credit) == nullptr);
}

static void TestHordeShamanRules()
{
    // Native incomplete and completed-but-unrewarded farewell both keep Annie
    // visible. Only rewarded flips forms; an unrelated NPC is never filtered.
    CHECK(HsAnnieVisible(kHsAnnie, false));
    CHECK(!HsAnnieVisible(kHsAnnieSpirit, false));
    CHECK(!HsAnnieVisible(kHsAnnie, true));
    CHECK(HsAnnieVisible(kHsAnnieSpirit, true));
    // A personal farewell is visible only to its owner. Shared Annie remains
    // available to unfinished viewers, and the spirit waits for scene cleanup.
    CHECK(HsAnnieDepartureVisible(kHsAnnie, false, false, false, false));
    CHECK(HsAnnieDepartureVisible(kHsAnnie, false, false, false, true));
    CHECK(!HsAnnieDepartureVisible(kHsAnnie, true, false, false, true));
    CHECK(HsAnnieDepartureVisible(kHsAnnie, true, true, true, true));
    CHECK(!HsAnnieDepartureVisible(kHsAnnie, false, true, true, true));
    CHECK(!HsAnnieDepartureVisible(kHsAnnie, true, true, false, true));
    CHECK(!HsAnnieDepartureVisible(kHsAnnie, true, true, true, false));
    CHECK(!HsAnnieDepartureVisible(kHsAnnieSpirit, true, false, false, true));
    CHECK(HsAnnieDepartureVisible(kHsAnnieSpirit, true, false, false, false));
    CHECK(!HsAnnieDepartureVisible(kHsAnnieSpirit, false, false, false, false));
    CHECK(HsAnnieDepartureVisible(3030, true, true, false, true));
    CHECK(HsAnnieDepartureContinues(true, true, true, true, true, true, false, 0));
    CHECK(HsAnnieDepartureContinues(true, true, true, true, true, true, false, kHsAnnieDepartureLimit - 1));
    CHECK(!HsAnnieDepartureContinues(true, true, true, true, true, true, false, kHsAnnieDepartureLimit));
    CHECK(!HsAnnieDepartureContinues(true, true, true, true, true, true, false, UINT32_MAX));
    CHECK(!HsAnnieDepartureContinues(false, true, true, true, true, true, false, 1));
    CHECK(!HsAnnieDepartureContinues(true, false, true, true, true, true, false, 1)); // Logout/map change.
    CHECK(!HsAnnieDepartureContinues(true, true, false, true, true, true, false, 1)); // Player death.
    CHECK(!HsAnnieDepartureContinues(true, true, true, false, true, true, false, 1));
    CHECK(!HsAnnieDepartureContinues(true, true, true, true, false, true, false, 1)); // Left the scene.
    CHECK(!HsAnnieDepartureContinues(true, true, true, true, true, false, false, 1)); // Stale actor.
    CHECK(!HsAnnieDepartureContinues(true, true, true, true, true, true, true, 1)); // Final arrival.
    CHECK(!HsAnnieRewardTransition(false, false)); // Failed/full-bag turn-in.
    CHECK(HsAnnieRewardTransition(false, true));
    CHECK(!HsAnnieRewardTransition(true, true)); // Repeated packet/relog.
    CHECK(!HsAnnieRewardTransition(true, false));
    for (bool rewarded : {false, true})
        for (uint32_t entry : {3030u, 3031u, 3032u, kHsHordePaladin, 60863u})
            CHECK(HsAnnieVisible(entry, rewarded));
    for (uint32_t mask = 0; mask < 8; ++mask)
    {
        std::array<uint32_t, 3> const saved = {mask & 1, (mask >> 1) & 1, (mask >> 2) & 1};
        // Every unfinished mentor is available, regardless of the other two.
        for (uint32_t i = 0; i < 3; ++i)
        {
            CHECK(HsAnnieMentorIndex(kHsAnnieMentors[i]) == i);
            CHECK(HsAnnieMentorExchange(kHsAnnieMentors[i], saved) == (saved[i] ? 5u : i));
            CHECK(HsAnnieEmptyMentorGossipAllowed(kHsAnnieMentors[i], true, saved) == !saved[i]);
            CHECK(!HsAnnieEmptyMentorGossipAllowed(kHsAnnieMentors[i], false, saved));
        }
    }
    // All six visit orders finish the same three native counters. Completed
    // mentors cannot replay credit, including after reopening or relogging.
    std::array<uint32_t, 3> order = {0, 1, 2};
    do
    {
        std::array<uint32_t, 3> saved = {0, 0, 0};
        for (uint32_t index : order)
        {
            CHECK(HsAnnieMentorExchange(kHsAnnieMentors[index], saved) == index);
            saved[index] = 1;
            CHECK(HsAnnieMentorExchange(kHsAnnieMentors[index], saved) == 5);
            CHECK(!HsAnnieEmptyMentorGossipAllowed(kHsAnnieMentors[index], true, saved));
            CHECK(!HsAnnieActionAllowed(5, kHsAnnieActionFirst + index * 4 + 2, true));
        }
        CHECK(saved[0] && saved[1] && saved[2]);
    } while (std::next_permutation(order.begin(), order.end()));
    for (uint32_t flags = 0; flags < 32; ++flags)
        for (uint32_t level : {1u, 9u, 10u, 60u})
            CHECK(HsAnnieLessonAllowed(flags & 1, level, flags & 2, flags & 4, flags & 8, flags & 16) ==
                ((flags & 23) == 23 && !(flags & 8) && level >= 10));
    for (uint32_t exchange = 0; exchange <= 5; ++exchange)
        for (uint32_t action = kHsAnnieActionFirst - 1; action <= kHsAnnieActionLast + 1; ++action)
        {
            bool const valid = exchange < 5 && action >= kHsAnnieActionFirst &&
                action < kHsAnnieActionFirst + exchange * 4 + (exchange < 3 ? 3 : 4) &&
                action >= kHsAnnieActionFirst + exchange * 4;
            CHECK(HsAnnieActionAllowed(exchange, action, true) == valid);
            CHECK(!HsAnnieActionAllowed(exchange, action, false)); // Stale/unoffered pages.
        }
    CHECK(!HsAnnieActionAllowed(UINT32_MAX, UINT32_MAX, true));
    CHECK(HsAnnieExchangeCreature(3, kHsAnnie));
    CHECK(HsAnnieExchangeCreature(4, kHsAnnie));
    CHECK(!HsAnnieExchangeCreature(3, kHsAnnieSpirit));
    CHECK(!HsAnnieExchangeCreature(4, kHsAnnieMentors[0]));
    // Unrelated services never get the empty-menu exception.
    for (uint32_t entry : {kHsAnnie, kHsAnnieSpirit, 3027u, kHsHordeMage, 60864u})
    {
        CHECK(HsAnnieMentorIndex(entry) == 3);
        CHECK(HsAnnieMentorExchange(entry, {0, 0, 0}) == 5);
        CHECK(!HsAnnieEmptyMentorGossipAllowed(entry, true, {0, 0, 0}));
    }
    Hs::MatchEnd end;
    end.meta.opponentEntry = kHsAnnieSpirit;
    end.res = "win"; end.why = "hero";
    CHECK(HsClassQuestVictory(end));
    for (char const* reason : {"concede", "disconnect", "timeout", "death"})
    {
        end.why = reason;
        CHECK(!HsClassQuestVictory(end));
    }
    end.why = "hero";
    for (char const* result : {"loss", "draw", "interrupted"})
    {
        end.res = result;
        CHECK(!HsClassQuestVictory(end));
    }
    end.res = "win"; end.meta.opponentEntry = kHsAnnie;
    CHECK(!HsClassQuestVictory(end));
}

static void TestClassQuestRoster()
{
    CheckClassQuestRoster(kHsWarriorVeteran);
    CheckClassQuestRoster(kHsHordeWarriorVeteran);
    CheckClassQuestRoster(kHsHordeMage);
    CheckClassQuestRoster(kHsHordeDruid);
    CheckClassQuestRoster(kHsAllianceDruid);
    CheckClassQuestRoster(kHsHordeRogue);
    CheckClassQuestRoster(kHsAllianceRogue);
    CheckClassQuestRoster(kHsHordePriest);
    CheckClassQuestRoster(kHsAlenya);
    CheckClassQuestRoster(kHsHordePaladin);
    CheckClassQuestRoster(kHsSirRowan);
    CheckClassQuestRoster(kHsAllianceShaman);
    CheckClassQuestRoster(kHsAnnieSpirit);
    CHECK(HsAllianceDruidStep({ 0, 0 }) == 0);
    CHECK(HsAllianceDruidStep({ 1, 0 }) == 1);
    CHECK(HsAllianceDruidStep({ 1, 1 }) == 2);
    CHECK(HsAllianceDruidStep({ 0, 1 }) == 0); // A report never bypasses its observation.
    HsAllianceDruidClock wait;
    CHECK(!wait.Advance(0));
    CHECK(!wait.Advance(9999));
    CHECK(wait.Advance(1));
    CHECK(wait.Advance(UINT32_MAX)); // Saturates without overflow or repeated elapsed time.
    HsAllianceDruidClock retry;
    CHECK(!retry.Advance(4000));
    retry = {}; // Death/relog restarts an unfinished wait; ordinary movement needs no action.
    CHECK(!retry.Advance(6000));
    CHECK(retry.Advance(4000));
    float const x = kHsAllianceDruidViewX, y = kHsAllianceDruidViewY, z = kHsAllianceDruidViewZ;
    CHECK(HsAllianceDruidAtView(1, 0, x, y, z));
    CHECK(HsAllianceDruidAtView(1, 0, x + 5, y, z));
    CHECK(!HsAllianceDruidAtView(1, 0, x + 5.1f, y, z));
    CHECK(!HsAllianceDruidAtView(1, 0, x, y, z - 10)); // Ground below the tower never counts.
    CHECK(!HsAllianceDruidAtView(1, 0, x + 4, y, z + 4)); // 3D radius, not separate axis gates.
    CHECK(!HsAllianceDruidAtView(0, 0, x, y, z));
    CHECK(!HsAllianceDruidAtView(1, 1, x, y, z));
}

// ---------------------------------------------------------------------------
// The seeded library of data/sql/world/0002_hearthstone_opponents.sql against the card data of
// 0001_hearthstone_card_template.sql: counts, classes, rarities (the engine support is not required).

namespace
{
    // One cell of an INSERT row: strings unquoted ('' -> '), NULL as "<NULL>", other expressions
    // (CONCAT of the multi line card texts) as "<expr>", numbers as written.
    typedef std::vector<std::string> SqlRow;

    bool ReadFile(std::string const& path, std::string& out)
    {
        std::ifstream in(path.c_str(), std::ios::binary);
        if (!in)
            return false;
        std::ostringstream ss;
        ss << in.rdbuf();
        out = ss.str();
        return true;
    }

    // The rows of one "INSERT INTO `table` (...) VALUES (...),(...);" starting at pos (on return: after it).
    bool ParseInsertStatement(std::string const& sql, std::size_t& pos, std::vector<SqlRow>& rows)
    {
        pos = sql.find("VALUES", pos);
        if (pos == std::string::npos)
            return false;
        pos += 6;
        auto skipWs = [&]() { while (pos < sql.size() && std::isspace(static_cast<unsigned char>(sql[pos]))) ++pos; };
        while (true)
        {
            skipWs();
            if (pos >= sql.size() || sql[pos] != '(')
                return false;
            ++pos;
            SqlRow row;
            while (true)
            {
                skipWs();
                if (pos >= sql.size())
                    return false;
                if (sql[pos] == '\'')
                {
                    std::string value;
                    ++pos;
                    while (true)
                    {
                        if (pos >= sql.size())
                            return false;
                        if (sql[pos] == '\'')
                        {
                            if (pos + 1 < sql.size() && sql[pos + 1] == '\'')
                            {
                                value.push_back('\'');
                                pos += 2;
                                continue;
                            }
                            ++pos;
                            break;
                        }
                        value.push_back(sql[pos++]);
                    }
                    row.push_back(value);
                }
                else
                {
                    std::size_t const start = pos;
                    int depth = 0;
                    while (pos < sql.size())
                    {
                        char const c = sql[pos];
                        if (c == '\'')
                        {
                            ++pos;
                            while (pos < sql.size() && sql[pos] != '\'')
                                ++pos;
                        }
                        else if (c == '(')
                            ++depth;
                        else if (c == ')' && depth > 0)
                            --depth;
                        else if (depth == 0 && (c == ',' || c == ')'))
                            break;
                        ++pos;
                    }
                    std::string token = sql.substr(start, pos - start);
                    while (!token.empty() && std::isspace(static_cast<unsigned char>(token.back())))
                        token.pop_back();
                    row.push_back(token == "NULL" ? std::string("<NULL>") : (token.find('(') != std::string::npos ? std::string("<expr>") : token));
                }
                skipWs();
                if (pos >= sql.size())
                    return false;
                if (sql[pos] == ',')
                {
                    ++pos;
                    continue;
                }
                if (sql[pos] == ')')
                {
                    ++pos;
                    break;
                }
                return false;
            }
            rows.push_back(row);
            skipWs();
            if (pos < sql.size() && sql[pos] == ',')
            {
                ++pos;
                continue;
            }
            return true;
        }
    }

    // The rows of every INSERT statement into `table`.
    bool ParseInsertRows(std::string const& sql, std::string const& table, std::vector<SqlRow>& rows)
    {
        std::string const marker = "INSERT INTO `" + table + "`";
        std::size_t pos = sql.find(marker);
        if (pos == std::string::npos)
            return false;
        while (pos != std::string::npos)
        {
            if (!ParseInsertStatement(sql, pos, rows))
                return false;
            pos = sql.find(marker, pos);
        }
        return true;
    }

}

static void TestSeededLibraryDecks(std::string const& dir)
{
    std::string cardSql;
    std::string deckSql;
    if (!ReadFile(dir + "/../data/sql/world/0001_hearthstone_card_template.sql", cardSql) ||
        !ReadFile(dir + "/../data/sql/world/0002_hearthstone_opponents.sql", deckSql))
    {
        std::printf("    cannot read the world SQL files next to %s\n", dir.c_str());
        CHECK(false);
        return;
    }

    // The card data as the engine's CardDb sees it, plus the CORE / collectible / acquisition columns.
    std::vector<SqlRow> cardRows;
    CHECK(ParseInsertRows(cardSql, "hearthstone_card_template", cardRows) && cardRows.size() > 600);
    HM::CardDb db;
    std::map<std::string, SqlRow> byId;
    for (SqlRow const& r : cardRows)
    {
        CHECK(r.size() == 22);
        if (r.size() != 22)
            return;
        HM::CardDef def;
        def.id = r[1];
        def.name = r[3];
        def.cardClass = r[6];
        def.rarity = r[7] == "<NULL>" ? std::string() : r[7];
        def.type = HM::ParseCardType(r[8]);
        db.Add(def);
        byId[r[1]] = r;
    }

    // No backslashes, no semicolon outside the statement ends, the tables of the contract.
    CHECK(deckSql.find('\\') == std::string::npos);
    CHECK(deckSql.find("DROP TABLE IF EXISTS `hearthstone_deck_template`;") != std::string::npos);
    CHECK(deckSql.find("CREATE TABLE IF NOT EXISTS `hearthstone_opponent`") != std::string::npos);
    CHECK(deckSql.find("DROP TABLE IF EXISTS `hearthstone_opponent`") == std::string::npos);     // owner data: never dropped
    CHECK(deckSql.find("INSERT INTO `hearthstone_opponent`") == std::string::npos);              // no seed rows
    CHECK(deckSql.find("ENUM('normal','hard','boss')") != std::string::npos);
    for (char const* column : { "`creature_entry`", "`difficulty`", "`deck_key`", "`hero_health`", "`hero_power`", "`hero_card`" })
        CHECK(deckSql.find(column) != std::string::npos);

    // The generated library (tools/decks/gen_deck_library.py): the module decks, the nine starters, the 16
    // quest NPC decks and the 2014 class library, with kind and finder columns.
    CHECK(deckSql.find("ENUM('opponent','starter','npc','library')") != std::string::npos);
    CHECK(deckSql.find("ENUM('none','normal','hard','ladder_easy','ladder_hard')") != std::string::npos);
    std::vector<SqlRow> deckRows;
    CHECK(ParseInsertRows(deckSql, "hearthstone_deck_template", deckRows));
    CHECK(deckRows.size() == 281);
    std::set<std::string> keys;
    std::map<std::string, int> kinds;
    std::map<std::string, int> finders;
    std::vector<std::string> const classes = DeckClasses();
    for (SqlRow const& r : deckRows)
    {
        CHECK(r.size() == 6);
        if (r.size() != 6)
            return;
        std::string const& key = r[0];
        std::string const& kind = r[3];
        std::string const& finder = r[4];
        CHECK(keys.insert(key).second && key.size() <= kMaxDeckKeyChars && !r[1].empty() && r[1].size() <= 64);
        CHECK(IsDeckClass(r[2]));
        ++kinds[kind];
        ++finders[kind + "/" + finder];
        bool const normal = key.compare(0, 7, "normal_") == 0;
        bool const hard = key.compare(0, 5, "hard_") == 0;
        std::string lowerClass = r[2];
        std::transform(lowerClass.begin(), lowerClass.end(), lowerClass.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (kind == "opponent")
        {
            CHECK((normal || hard) && finder == "none" && key == (normal ? "normal_" : "hard_") + lowerClass);
        }
        else if (kind == "starter")
            CHECK(key == StarterDeckKey(r[2]) && finder == "normal");
        else if (kind == "npc")
            CHECK(key.compare(0, 4, "npc_") == 0 && finder == "none");
        else if (kind == "library")
            CHECK(key.compare(0, 5 + lowerClass.size(), "lib_" + lowerClass + "_") == 0 &&
                (finder == "normal" || finder == "hard" || finder == "ladder_easy" || finder == "ladder_hard"));
        else
            CHECK(false);

        DeckCards cards;
        std::string error;
        CHECK(ParseLibraryCards(r[5], cards, error));
        std::string const problem = CheckLibraryDeck(db, r[2], cards, false);
        CHECK(problem.empty());
        if (!problem.empty())
            std::printf("    deck %s: %s\n", key.c_str(), problem.c_str());

        uint32_t total = 0;
        uint32_t classicCards = 0;
        for (auto const& entry : cards)
        {
            total += entry.second;
            SqlRow const& card = byId[entry.first];
            CHECK(!card.empty());
            if (card.empty())
                continue;
            CHECK(card[19] == "1"); // collectible
            // Player decks (starters) and the module decks use standard cards only; the AI decks of the quest NPCs and
            // the library may hold a promo card (Gelbin Mekkatorque in two lists), which no player can craft.
            if (kind == "opponent" || kind == "starter")
                CHECK(card[20] == "standard");
            else if (kind == "library" && (finder == "ladder_easy" || finder == "ladder_hard"))
                // The Naxx-era ladder decks (25356b8) may hold Naxxramas cards (acquisition quest): NPC-only decks.
                CHECK(card[20] == "standard" || card[20] == "reward_only" || card[20] == "quest");
            else
                CHECK(card[20] == "standard" || card[20] == "reward_only");
            bool const core = card[5] == "CORE";
            if (!core)
                classicCards += entry.second;
        }
        CHECK(total == kDeckSize);
        if (kind == "starter" || normal)
            CHECK(classicCards == 0); // Basic cards only
        if (hard)
            CHECK(classicCards >= 8); // the Hard deck really uses the Classic cards
    }
    CHECK(kinds["opponent"] == 18 && kinds["starter"] == 9 && kinds["npc"] == 16 && kinds["library"] == 238);
    // The 60 Naxx-era decks are ladder only: 20 Easy and 40 Hard, not in the match-finder pools.
    CHECK(finders["library/ladder_easy"] == 20 && finders["library/ladder_hard"] == 40);
    // The archive's ratings: 79 Easy (70 library + the 9 starters) and 108 Hard.
    CHECK(finders["library/normal"] == 70 && finders["library/hard"] == 108 && finders["starter/normal"] == 9);
    for (std::string const& cls : classes)
    {
        std::string lower = cls;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        CHECK(keys.count("normal_" + lower) == 1 && keys.count("hard_" + lower) == 1 && keys.count("starter_" + lower) == 1);
    }

    // The library builds from the seeded rows: every deck parses; the CardDb here has no scripts for
    // most cards, so only the structure is checked (readiness depends on the engine).
    std::vector<LibraryDeckRow> libraryRows;
    for (SqlRow const& r : deckRows)
    {
        libraryRows.push_back(Row(r[0].c_str(), r[2].c_str(), r[5]));
        libraryRows.back().kind = r[3];
        libraryRows.back().finder = r[4];
    }
    std::vector<std::string> errors;
    DeckLibrary const library = DeckLibrary::Build(db, libraryRows, errors);
    CHECK(library.Decks().size() == 281);   // 221 + the 60 Naxx-era ladder decks (25356b8)
    for (LibraryDeck const& d : library.Decks())
        CHECK(d.problem.empty() || d.problem.find("not supported by the match engine") != std::string::npos);
    CHECK(library.Find("starter_mage") && library.Find("starter_mage")->kind == "starter" &&
          library.Find("starter_mage")->finder == "normal");

    // The archive NPCs plus class-quest NPCs: distinct entries, every referenced deck exists.
    CHECK(deckSql.find("DROP TABLE IF EXISTS `hearthstone_npc_unlock`;") != std::string::npos);
    std::vector<SqlRow> unlockRows;
    CHECK(ParseInsertRows(deckSql, "hearthstone_npc_unlock", unlockRows) && unlockRows.size() == 34);
    std::set<std::string> npcEntries;
    std::set<std::string> npcDecks;
    std::map<std::string, int> sides;
    for (SqlRow const& r : unlockRows)
    {
        CHECK(r.size() == 4);
        if (r.size() != 4)
            return;
        CHECK(npcEntries.insert(r[0]).second && std::stoul(r[0]) > 0 && std::stoul(r[1]) > 0);
        LibraryDeck const* deck = library.Find(r[2]);
        CHECK(deck);
        if (Hs::ClassQuestOpponent const* opponent = Hs::ClassQuestOpponentOf(static_cast<uint32_t>(std::stoul(r[0]))))
        {
            CHECK(std::stoul(r[1]) == opponent->questId && (r[3] == "alliance") == opponent->alliance);
            CHECK(deck && deck->kind == "opponent" && deck->finder == "none");
        }
        else
            CHECK(npcDecks.insert(r[2]).second && deck && deck->kind == "npc");
        ++sides[r[3]];
    }
    CHECK(sides["alliance"] == 17 && sides["horde"] == 17 && npcDecks.size() == static_cast<std::size_t>(kinds["npc"]));
    for (auto const& chain : kHsHunterChains)
    {
        auto const hunter = std::find_if(unlockRows.begin(),unlockRows.end(),[&chain](SqlRow const& row) {
            return row.size()==4 && row[0]==std::to_string(chain.teacher);
        });
        CHECK(hunter!=unlockRows.end());
        if (hunter!=unlockRows.end()) CHECK((*hunter)[1]==std::to_string(chain.firstQuest+2) &&
            (*hunter)[2]=="normal_hunter" && (*hunter)[3]==(chain.alliance ? "alliance" : "horde"));
        CHECK(npcEntries.count(std::to_string(chain.helper))==0);
        for (uint32_t i=0;i<5;++i) CHECK(npcEntries.count(std::to_string(chain.firstCredit+i))==0);
        for (uint32_t i=0;i<9;++i) CHECK(npcEntries.count(std::to_string(chain.firstProjection+i))==0);
    }
    auto const priest = std::find_if(unlockRows.begin(), unlockRows.end(), [](SqlRow const& row) {
        return row.size() == 4 && row[0] == std::to_string(kHsAlenya);
    });
    CHECK(priest != unlockRows.end());
    if (priest != unlockRows.end())
        CHECK((*priest)[1] == std::to_string(kHsAlliancePriestDuel) &&
            (*priest)[2] == "normal_priest" && (*priest)[3] == "alliance");
    CHECK(npcEntries.count(std::to_string(kHsSyrelle)) == 0);
    for (uint32_t credit : kHsAlliancePriestCredits)
        CHECK(npcEntries.count(std::to_string(credit)) == 0);
    // The owner-chosen quests: Rexxar 6602, Dorn Plainstalker 1394, Nathanos 6148, Althea 228.
    std::map<std::string, std::string> questOf;
    for (SqlRow const& r : unlockRows)
        questOf[r[2]] = r[1];
    CHECK(questOf["npc_rexxar"] == "6602" && questOf["npc_dorn_plainstalker"] == "1394" &&
          questOf["npc_nathanos_blightcaller"] == "6148" && questOf["npc_commander_althea_ebonlocke"] == "228");
}

static void TestGameWorstCaseFits()
{
    // The largest GAME payload: 10 hand cards, 7 minions a side with every flag and big numbers, the longest
    // card ids, a weapon each, every hand card and attacker with every target, 40 long events, long names.
    // Adds, at their worst: every minion and hero flag, a Choose One option pair on every hand card
    // with one lg entry per option and target, 5 secrets a side, a 3 option pick, big mx / ox numbers.
    for (int phase = 7; phase <= 8; ++phase)
    {
        bool const p8 = phase == 8;
        HM::MatchView v = CanonicalView();
        v.self.hand.clear();
        v.self.board.clear();
        v.other.board.clear();
        v.self.secrets.clear();
        v.other.secrets.clear();
        v.legal.clear();
        v.events.clear();
        std::vector<HM::EntityId> all = { 1000001, 1000002 };
        for (int i = 0; i < 7; ++i)
            for (int side = 0; side < 2; ++side)
            {
                HM::MinionView m = MakeMinionView(1000100 + static_cast<HM::EntityId>(i * 2 + side), "ds1_whelptoken", 999, 999, 999);
                m.taunt = m.charge = m.divineShield = m.windfury = m.canAttack = true;
                if (p8)
                    m.stealth = m.frozen = m.silenced = m.poisonous = m.enraged = m.immune = m.cantAttack = true;
                (side ? v.other : v.self).board.push_back(m);
                all.push_back(m.id);
            }
        v.self.hero.weaponId = 1000300;
        v.self.hero.weaponCardId = "EX1_383t";
        v.other.hero.weaponId = 1000301;
        v.other.hero.weaponCardId = "EX1_383t";
        if (p8)
        {
            v.self.hero.frozen = v.self.hero.immune = v.self.hero.canAttack = true;
            v.other.hero.frozen = v.other.hero.immune = v.other.hero.canAttack = true;
            v.self.cardsPlayed = 99;
            v.self.overloadLocked = 10;
            v.self.overloadOwed = 10;
            v.other.cardsPlayed = 99;
            v.other.overloadLocked = 10;
            v.other.overloadOwed = 10;
            for (HM::EntityId i = 0; i < 5; ++i)
            {
                HM::SecretView mine;
                mine.id = 1000400 + i;
                mine.cardId = "ds1_whelptoken";
                v.self.secrets.push_back(mine);
                HM::SecretView theirs;
                theirs.id = 1000410 + i;
                v.other.secrets.push_back(theirs);
            }
            v.pickPending = true;
            v.opponentPicking = true;
            for (HM::EntityId i = 0; i < 3; ++i)
            {
                HM::PickView pick;
                pick.id = 1000500 + i;
                pick.cardId = "ds1_whelptoken";
                v.pick.push_back(pick);
            }
        }
        for (int i = 0; i < 10; ++i)
        {
            v.self.hand.push_back(MakeHandView(1000200 + static_cast<HM::EntityId>(i), "ds1_whelptoken", 99, 999, 999));
            if (p8)
                v.self.hand.back().options = { "ds1_whelptoken", "ds1_whelptoken" };
            for (HM::EntityId t : all)
            {
                if (p8)
                {
                    v.legal.push_back(LegalEntry(HM::ActionType::Play, v.self.hand.back().id, t, 1));
                    v.legal.push_back(LegalEntry(HM::ActionType::Play, v.self.hand.back().id, t, 2));
                }
                else
                    v.legal.push_back(LegalEntry(HM::ActionType::Play, v.self.hand.back().id, t));
            }
        }
        for (HM::MinionView const& m : v.self.board)
            for (HM::EntityId t : all)
                v.legal.push_back(LegalEntry(HM::ActionType::Attack, m.id, t));
        for (HM::EntityId t : all)
            v.legal.push_back(LegalEntry(HM::ActionType::HeroPower, 2, t));
        for (uint32_t i = 0; i < 60; ++i)
            v.events.push_back(MakeEvent(1000000 + i, HM::EventType::WeaponBreak, 1, 1000000, 1000001, 999, "ds1_whelptoken"));
        GameHeader h = CanonicalHeader();
        h.matchId = 4294967295u;
        h.myName = "Abcdefghijkl";
        h.opponentName = std::string(64, ';'); // escaped: twice as long
        h.difficulty = "normal";
        std::string const payload = EncodeGame(h, v);
        std::vector<std::string> frames;
        bool const fits = ChunkPayload("GAME", "zzzzzzzz", payload, frames);
        std::printf("    worst-case GAME (phase %d keys): %u bytes, %u frames\n", phase, static_cast<unsigned>(payload.size()),
                    static_cast<unsigned>(frames.size()));
        CHECK(fits && frames.size() <= kMaxPartsHardLimit);
        // The same payload decodes back (every key present once).
        std::map<std::string, std::string> kv;
        CHECK(KvDecode(payload, kv) && kv.count("dif") == 1 && kv.count("opk") == 1);
    }
}

// ---------------------------------------------------------------------------
// Match history: the row built from a MatchEnd.

static MatchStartParams HistoryParams(uint32_t guid, uint32_t seed)
{
    MatchStartParams p = FixtureParams(guid, seed);
    p.humanName = "Eqwe";
    p.opponentName = "Innkeeper Gryshka";
    p.difficulty = "hard";
    p.meta.accountId = 7;
    p.meta.opponentEntry = 6740;
    p.meta.deckId = 3;
    p.meta.deckName = "Mage \"Tempo\" 1";
    p.meta.opponentDeckKey = "hard_warrior";
    p.meta.startedAt = 1790000000;
    return p;
}

static void TestHistoryEncodeCardList()
{
    CHECK(EncodeCardList({}).empty());
    CHECK(EncodeCardList({"EX1_116"}) == "EX1_116.1");
    CHECK(EncodeCardList({"CS2_168", "EX1_116", "CS2_168", "CS2_022", "CS2_168"}) == "CS2_022.1,CS2_168.3,EX1_116.1");
    CHECK(EncodeCardList({"", "A", ""}) == "A.1"); // an empty entry is no card
    // The fixture deck of a real match: 30 copies in at most 30 entries.
    MatchStartParams const p = FixtureParams(1, 1);
    std::string const text = EncodeCardList(p.humanDeck);
    uint32_t total = 0;
    std::stringstream in(text);
    std::string part;
    while (std::getline(in, part, ','))
        total += static_cast<uint32_t>(std::strtoul(part.substr(part.find('.') + 1).c_str(), nullptr, 10));
    CHECK(total == 30 && text.size() <= kHistoryCardsBytes);
}

static void TestHistoryResultWords()
{
    CHECK(HistoryResultOf("win", "hero") == "win");
    CHECK(HistoryResultOf("loss", "hero") == "loss");
    CHECK(HistoryResultOf("loss", "concede") == "surrender");
    CHECK(HistoryResultOf("draw", "turns") == "draw");
    CHECK(HistoryResultOf("int", "stuck") == "interrupted");
    CHECK(HistoryResultOf("int", "logout") == "interrupted");
    CHECK(HistoryResultOf("-", "-") == "unknown");
    CHECK(HistoryResultOf("", "") == "unknown");
    // Only a loss can be a surrender.
    CHECK(HistoryResultOf("win", "concede") == "win");
}

static void TestHistoryClipText()
{
    CHECK(ClipHistoryText("Eqwe", 24) == "Eqwe");
    CHECK(ClipHistoryText("abcdef", 3) == "abc");
    CHECK(ClipHistoryText("", 5).empty());
    CHECK(ClipHistoryText("a\tb\nc", 10) == "a b c");                  // control characters
    CHECK(ClipHistoryText("caf\xC3\xA9", 10) == "caf\xC3\xA9");          // 2-byte character kept
    CHECK(ClipHistoryText("caf\xC3\xA9", 4) == "caf");                  // never half a character
    CHECK(ClipHistoryText("\xE2\x82\xAC" "uro", 3) == "\xE2\x82\xAC");   // 3-byte character fits exactly
    CHECK(ClipHistoryText("\xE2\x82\xAC" "uro", 2).empty());
    CHECK(ClipHistoryText("a\xF0\x9F\x98\x80" "b", 10) == "a?b");        // 4-byte character replaced
    CHECK(ClipHistoryText("\xC3", 10) == "?");                          // a truncated sequence
    CHECK(ClipHistoryText("\x80x", 10) == "?x");                        // a stray continuation byte
    CHECK(ClipHistoryText("a\xE2\x82", 10) == "a?");                    // a truncated 3-byte sequence
    CHECK(ClipHistoryText("\xC3(x", 10) == "?(x");                      // a lead byte without continuation
    CHECK(ClipHistoryText("\xE0\x80\x80z", 10) == "?z");                // an overlong form
    CHECK(ClipHistoryText("\xED\xA0\x80z", 10) == "?z");                // a UTF-16 surrogate
    CHECK(ClipHistoryText("\xC0\xAFz", 10) == "?z");                    // C0/C1 are never valid lead bytes
    CHECK(ClipHistoryText(std::string(2000, 'x'), kHistoryCardsBytes).size() == kHistoryCardsBytes);
}

static void TestHistoryDurationAndLine()
{
    CHECK(FormatHistoryDuration(0) == "0s" && FormatHistoryDuration(45) == "45s");
    CHECK(FormatHistoryDuration(60) == "1m00s" && FormatHistoryDuration(312) == "5m12s");
    CHECK(FormatHistoryDuration(3599) == "59m59s" && FormatHistoryDuration(3600) == "1h00m");
    CHECK(FormatHistoryDuration(3600 + 5 * 60 + 9) == "1h05m");

    MatchHistoryListing entry;
    entry.id = 12;
    entry.endedText = "2026-10-02 14:03";
    entry.row.characterName = "Eqwe";
    entry.row.opponentName = "Innkeeper Gryshka";
    entry.row.difficulty = "normal";
    entry.row.deckName = "Mage 1";
    entry.row.deckClass = "MAGE";
    entry.row.opponentDeckKey = "normal_mage";
    entry.row.result = "win";
    entry.row.endReason = "hero";
    entry.row.turns = 8;
    entry.row.durationSec = 312;
    entry.row.seed = 123456;
    CHECK(FormatMatchHistoryLine(entry) == "  #12 2026-10-02 14:03 Eqwe vs Innkeeper Gryshka [normal], deck \"Mage 1\" (MAGE) vs "
        "normal_mage: win (hero), 8 turns, 5m12s, seed 123456");
    entry.row.turns = 1;
    entry.row.opponentDeckKey.clear();
    entry.row.endReason.clear();
    CHECK(FormatMatchHistoryLine(entry).find("vs ?: win, 1 turn, 5m12s") != std::string::npos);
}

static void TestMatchHistoryRows()
{
    MatchService svc(500);
    svc.SetCardDb(TestCardDb());
    svc.SetAiDelayMs(2500);

    // A concede: result surrender, reason concede; every identity field of the start comes back.
    MatchStartParams const p = HistoryParams(1, 4242);
    MatchStartResult const s = svc.Start(p, 0);
    CHECK(s.ok);
    GameActOutcome const o = svc.Act(1, Req("mid=" + std::to_string(s.matchId) + ";a=concede"), 0);
    CHECK(o.ok && o.ended);
    MatchHistoryRow r = BuildMatchHistoryRow(o.end, p.meta.startedAt + 312);
    CHECK(r.accountId == 7 && r.characterGuid == 1 && r.characterName == "Eqwe" && r.matchId == s.matchId);
    CHECK(r.opponentEntry == 6740 && r.opponentName == "Innkeeper Gryshka" && r.difficulty == "hard");
    CHECK(r.deckId == 3 && r.deckName == "Mage \"Tempo\" 1" && r.deckClass == "MAGE");
    CHECK(r.opponentDeckKey == "hard_warrior" && r.opponentClass == "WARRIOR");
    CHECK(r.result == "surrender" && r.endReason == "concede");
    CHECK(static_cast<int>(r.turns) == o.end.turn); // 0: conceded during the mulligan
    CHECK(r.startedAt == 1790000000 && r.endedAt == 1790000312 && r.durationSec == 312);
    CHECK(r.seed == 4242 && r.aiDelayMs == 2500);
    CHECK(r.datasetId == kDatasetId && r.datasetHash == kDatasetHash);
    CHECK(r.rulesVersion == 11 && r.rulesVersion == static_cast<uint32_t>(HM::kRulesVersion));
    CHECK(r.humanCards == EncodeCardList(p.humanDeck) && r.opponentCards == EncodeCardList(p.opponentDeck));
    CHECK(r.currencyPaid == 0);

    // Every interruption: result interrupted, the reason code of the interruption.
    for (Interrupt why : { Interrupt::Stuck, Interrupt::Teleport, Interrupt::Death, Interrupt::Npc, Interrupt::Gm, Interrupt::Error })
    {
        CHECK(svc.Start(HistoryParams(2, 77), 0).ok);
        MatchAbortOutcome const a = svc.Abort(2, why);
        CHECK(a.ended);
        MatchHistoryRow const x = BuildMatchHistoryRow(a.end, p.meta.startedAt + 10);
        CHECK(x.result == "interrupted" && x.endReason == InterruptCode(why) && x.durationSec == 10);
        CHECK(x.seed == 77 && x.difficulty == "hard" && x.deckId == 3);
    }

    // Logout: interrupted, reason logout.
    CHECK(svc.Start(HistoryParams(3, 88), 0).ok);
    MatchEnd logout;
    CHECK(svc.Drop(3, &logout));
    MatchHistoryRow const l = BuildMatchHistoryRow(logout, p.meta.startedAt + 1);
    CHECK(l.result == "interrupted" && l.endReason == "logout" && l.characterGuid == 3 && l.seed == 88);

    // Played to the end: the human only mulligans and ends its turns while the AI plays, so the match ends by a
    // hero death (loss) or the turn limit (draw), never as an interruption.
    MatchStartParams q = HistoryParams(4, 31337);
    q.difficulty = "normal";
    MatchStartResult const st = svc.Start(q, 0);
    CHECK(st.ok);
    std::string const mid = std::to_string(st.matchId);
    uint64_t now = 0;
    MatchEnd finished;
    bool ended = false;
    for (int guard = 0; guard < 3000 && !ended; ++guard)
    {
        std::map<std::string, std::string> const kv = KvOf(svc.Get(4, now));
        if (kv.at("ph") == "mull")
        {
            GameActOutcome const act = svc.Act(4, Req("mid=" + mid + ";seq=" + kv.at("seq") + ";a=mull;r="), now);
            CHECK(act.ok);
        }
        else if (kv.at("ph") == "play" && kv.at("act") == "m")
        {
            GameActOutcome const act = svc.Act(4, Req("mid=" + mid + ";seq=" + kv.at("seq") + ";a=end"), now);
            CHECK(act.ok);
            if (act.ended)
            {
                finished = act.end;
                ended = true;
            }
        }
        else if (kv.at("ph") == "play")
        {
            now += 3000;
            for (MatchPush const& push : svc.Tick(now))
                if (push.ended)
                {
                    finished = push.end;
                    ended = true;
                }
        }
        else
            break;
    }
    CHECK(ended);
    MatchHistoryRow const f = BuildMatchHistoryRow(finished, q.meta.startedAt + 600);
    CHECK(f.result == "win" || f.result == "loss" || f.result == "draw");
    CHECK(f.endReason == "hero" || f.endReason == "turns");
    CHECK(f.difficulty == "normal" && f.seed == 31337 && f.turns >= 1 && f.durationSec == 600);
    CHECK((f.result == "draw") == (f.endReason == "turns"));
}

static void TestMatchHistoryRowEdges()
{
    MatchEnd end;
    end.guid = 9;
    end.matchId = 5;
    end.turn = 3;
    end.res = "win";
    end.why = "hero";
    end.humanName = "Eqwe";
    end.opponentName = "Gryshka";
    end.difficulty = "normal";

    // A start that is unknown (a match from before the identity existed) or after the end: no negative or
    // absurd duration, the start time falls back to the end time; a missing deck name stays empty.
    MatchHistoryRow r = BuildMatchHistoryRow(end, 1790000500);
    CHECK(r.startedAt == 1790000500 && r.endedAt == 1790000500 && r.durationSec == 0);
    CHECK(r.deckName.empty() && r.deckId == 0 && r.opponentDeckKey.empty() && r.opponentEntry == 0 && r.accountId == 0);
    CHECK(r.result == "win" && r.rulesVersion == 11 && r.datasetId == kDatasetId);
    end.meta.startedAt = 1790000900; // the clock moved back
    r = BuildMatchHistoryRow(end, 1790000500);
    CHECK(r.durationSec == 0 && r.startedAt == 1790000900);
    end.meta.startedAt = 1;
    r = BuildMatchHistoryRow(end, 1790000500);
    CHECK(r.durationSec == 1790000499u);
    end.meta.startedAt = -5;
    r = BuildMatchHistoryRow(end, 1790000500);
    CHECK(r.durationSec == 0 && r.startedAt == 1790000500);

    // A turn counter that is somehow negative stays 0.
    end.turn = -1;
    CHECK(BuildMatchHistoryRow(end, 5).turns == 0);

    // Over-long and odd text is clipped to the columns of the migration.
    end.humanName = std::string(100, 'n');
    end.opponentName = std::string(200, 'o');
    end.meta.deckName = "Deck \xF0\x9F\x98\x80 name";
    end.meta.opponentDeckKey = std::string(100, 'k');
    r = BuildMatchHistoryRow(end, 5);
    CHECK(r.characterName.size() == kHistoryCharNameBytes && r.opponentName.size() == kHistoryNameBytes);
    CHECK(r.deckName == "Deck ? name" && r.opponentDeckKey.size() == kHistoryKeyBytes);

    // A res the service never produces becomes "unknown" rather than a wrong result.
    end.res = "-";
    end.why = "-";
    CHECK(BuildMatchHistoryRow(end, 5).result == "unknown");
}

static void TestHordeWarriorQuestRules()
{
    std::vector<uint32_t> spoken;
    HsHordeSceneClock clock;
    auto speak = [&](uint32_t line) { spoken.push_back(line); };
    CHECK(!clock.Advance(0, speak));
    CHECK(spoken == std::vector<uint32_t>({ 0 }));
    // Irregular updates still take the full minute; no early quest credit at the penultimate line.
    CHECK(!clock.Advance(17000, speak));
    CHECK(spoken == std::vector<uint32_t>({ 0, 1, 2 }));
    CHECK(!clock.Advance(42999, speak));
    CHECK(clock.elapsedMs == 59999 && spoken.size() == 7);
    CHECK(clock.Advance(1, speak));
    CHECK(spoken == std::vector<uint32_t>({ 0, 1, 2, 3, 4, 5, 6, 7 }));
    CHECK(clock.Advance(0xffffffffu, speak) && spoken.size() == 8); // no duplicate lines or timer overflow
    // A restarted attempt has to watch the entire minute again.
    clock = HsHordeSceneClock();
    spoken.clear();
    CHECK(!clock.Advance(59000, speak));
    CHECK(clock.Advance(1000, speak) && spoken.size() == 8);
    for (int flags = 0; flags < 8; ++flags)
        CHECK(HsHordePickupAllowed((flags & 1) != 0, (flags & 2) != 0,
            (flags & 4) != 0) == (flags == 3));

    // Two viewers of the same permanent box get different interaction states without changing shared flags.
    uint32_t const sharedFlags = 0x20;
    CHECK(HsHordeBoxViewFlags(sharedFlags, true) == 0x20);
    CHECK(HsHordeBoxViewFlags(sharedFlags, false) == 0x30);
    CHECK(HsHordeBoxViewFlags(0x30, true) == 0x20); // accept/restart enables only this viewer
    CHECK(HsHordeBoxViewFlags(0x20, false) == 0x30); // collection/abandonment disables only this viewer

    MatchEnd end;
    end.meta.opponentEntry = kHsHordeWarriorVeteran;
    end.res = "win";
    end.why = "hero";
    CHECK(HsClassQuestVictory(end));
    CHECK(!HsWarriorQuestVictory(end)); // no Alliance credit or dwarf commentary
    for (char const* result : { "loss", "draw", "int", "surrender" })
    {
        end.res = result;
        CHECK(!HsClassQuestVictory(end));
    }
    end.res = "win";
    for (char const* reason : { "concede", "turns", "logout", "stuck", "gm", "error" })
    {
        end.why = reason;
        CHECK(!HsClassQuestVictory(end));
    }
    end.why = "hero";
    end.meta.opponentEntry = kHsHordeThinkingGrunt;
    CHECK(!HsClassQuestVictory(end));
    end.meta.opponentEntry = 919501;
    CHECK(!HsClassQuestVictory(end));
}

static void TestHordeMageQuestRules()
{
    CHECK(HsHordeMageNextExperiment({ 0, 0, 0 }) == 0);
    CHECK(HsHordeMageNextExperiment({ 1, 0, 0 }) == 1);
    CHECK(HsHordeMageNextExperiment({ 1, 1, 0 }) == 2);
    CHECK(HsHordeMageNextExperiment({ 1, 1, 1 }) == 3);
    CHECK(HsHordeMageNextExperiment({ 0, 1, 1 }) == 0); // No skipping missing observations.
    std::vector<uint32_t> lines;
    auto emit = [&](uint32_t line) { lines.push_back(line); };
    HsHordeMageClock clock;
    CHECK(!clock.Advance(0, emit) && lines == std::vector<uint32_t>({ 0 }));
    CHECK(!clock.Advance(1999, emit));
    CHECK(clock.Advance(1, emit) && lines.size() == 1);
    CHECK(clock.Advance(UINT32_MAX, emit) && lines.size() == 1);
    clock = {};
    lines.clear();
    CHECK(clock.Advance(UINT32_MAX, emit) && lines == std::vector<uint32_t>({ 0 }));
    Hs::MatchEnd end;
    end.meta.opponentEntry = kHsHordeMage;
    end.res = "win";
    end.why = "hero";
    CHECK(HsClassQuestVictory(end));
    for (char const* why : { "surrender", "disconnect", "draw", "timeout" })
    {
        end.why = why;
        CHECK(!HsClassQuestVictory(end));
    }
    end.why = "hero";
    end.res = "loss";
    CHECK(!HsClassQuestVictory(end));
}

static void TestTraderRules()
{
    uint32_t swap = 14400;
    CHECK(ParseTraderSwapTime("00:00", swap) && swap == 0);
    CHECK(ParseTraderSwapTime("23:59", swap) && swap == 86340);
    for (std::string const& value : {"24:00", "04:60", "4:00", "04:0", "04:00 UTC", "-1:00", "xx:yy", ""})
        CHECK(!ParseTraderSwapTime(value, swap) && swap == 86340);
    CHECK(ParseTraderSwapTime("04:00", swap) && swap == 14400);
    uint64_t const boundary = 20000ULL * 86400 + swap;
    CHECK(TraderPeriod(boundary - 1, swap) == boundary - 86400);
    CHECK(TraderPeriod(boundary, swap) == boundary);
    CHECK(TraderPeriod(boundary + 86399, swap) == boundary);
    CHECK(TraderPeriod(boundary + 86400, swap) == boundary + 86400);
    for (uint32_t offset = 0; offset < 86400; offset += 60)
    {
        uint64_t const start = TraderPeriod(boundary + offset, swap);
        CHECK(start <= boundary + offset && boundary + offset < start + 86400);
    }
    CardCatalog catalog;
    CHECK(catalog.Add("CS2_001", true, "standard", "FREE", "MAGE"));
    CHECK(catalog.Add("EX1_298", true, "standard", "LEGENDARY", "NEUTRAL"));
    CHECK(catalog.Add("PRO_001", true, "reward_only", "LEGENDARY", "NEUTRAL"));
    auto const excluded = TraderExclusions("ex1_298, EX1_298 FUTURE_CARD", catalog);
    CHECK(excluded.size() == 2 && excluded.count("EX1_298") && excluded.count("FUTURE_CARD"));
    CHECK(TraderEligible("CS2_001", *catalog.Find("CS2_001"), excluded));
    CHECK(!TraderEligible("EX1_298", *catalog.Find("EX1_298"), excluded));
    CHECK(!TraderEligible("PRO_001", *catalog.Find("PRO_001"), {}));
    CardRule token = *catalog.Find("CS2_001");
    token.collectible = false;
    CHECK(!TraderEligible("TOKEN", token, {}));
    token.collectible = true;
    token.acquisition = Acquisition::Unreleased;
    CHECK(!TraderEligible("UNRELEASED", token, {}));
    CHECK(TraderPrice(Rarity::Free) == 50000 && TraderPrice(Rarity::Common) == 50000);
    CHECK(TraderPrice(Rarity::Rare) == 250000 && TraderPrice(Rarity::Epic) == 500000);
    CHECK(TraderPrice(Rarity::Legendary) == 2000000 && TraderPrice(Rarity::None) == 0);
    // The daily pick is stable per period, always inside the pool and spreads over it.
    std::set<std::size_t> picks;
    for (uint64_t day = 0; day < 400; ++day)
    {
        std::size_t const pick = TraderPick(378, boundary + day * 86400);
        CHECK(pick < 378 && pick == TraderPick(378, boundary + day * 86400));
        picks.insert(pick);
    }
    CHECK(picks.size() > 200 && TraderPick(1, boundary) == 0);
}


// Collectible Naxxramas cards (acquisition quest). Rules over a small catalog; the full dataset is checked
// in arena_tests (pack pool, trader, arena pools).
static void TestQuestAcquisition()
{
    Acquisition a;
    CHECK(ParseAcquisition("quest", a) && a == Acquisition::Quest);
    CHECK(!ParseAcquisition("Quest", a) && !ParseAcquisition("quests", a));

    auto const catalog = DeckCatalog();
    CardRule const undertaker = *catalog->Find("FP1_028");
    CardRule const maexxna = *catalog->Find("FP1_010");
    CHECK(undertaker.collectible && undertaker.acquisition == Acquisition::Quest && !undertaker.legendary);
    CHECK(maexxna.acquisition == Acquisition::Quest && maexxna.legendary);

    // who may grant them: quest rewards and GMs, never a pack and not the arena's special-card rewards
    std::string id;
    CardRule rule;
    CHECK(ValidateGrant(*catalog, "fp1_028", GrantSource::Quest, id, rule) == OwnershipError::None && id == "FP1_028");
    CHECK(rule.acquisition == Acquisition::Quest);
    CHECK(ValidateGrant(*catalog, "FP1_028", GrantSource::Gm, id, rule) == OwnershipError::None);
    CHECK(ValidateGrant(*catalog, "FP1_028", GrantSource::Pack, id, rule) == OwnershipError::SourceNotAllowed);
    CHECK(ValidateGrant(*catalog, "FP1_028", GrantSource::SpecialReward, id, rule) == OwnershipError::SourceNotAllowed);

    // Reward mode: the missing playable copies (2, Legendary 1) are protected, extras are not; Unprotected stays so
    CardCount after;
    CHECK(PlanGrant(undertaker, Count(0, 0), 2, ProtectionMode::Reward, after) == OwnershipError::None && after == Count(2, 0));
    CHECK(PlanGrant(undertaker, Count(0, 0), 3, ProtectionMode::Reward, after) == OwnershipError::None && after == Count(2, 1));
    CHECK(PlanGrant(undertaker, Count(1, 0), 2, ProtectionMode::Reward, after) == OwnershipError::None && after == Count(2, 1));
    CHECK(PlanGrant(maexxna, Count(0, 0), 1, ProtectionMode::Reward, after) == OwnershipError::None && after == Count(1, 0));
    CHECK(PlanGrant(maexxna, Count(1, 0), 1, ProtectionMode::Reward, after) == OwnershipError::None && after == Count(1, 1));
    CHECK(PlanGrant(undertaker, Count(0, 0), 1, ProtectionMode::Unprotected, after) == OwnershipError::None && after == Count(0, 1));

    // never crafted, never scrapped, never offered by the daily trader
    CHECK(!IsCraftable(undertaker) && !IsCraftable(maexxna));
    CHECK(!TraderEligible("FP1_028", undertaker, {}) && !TraderEligible("FP1_010", maexxna, {}));
    CardRule standardCommon = *catalog->Find("TST_n01");
    CHECK(TraderEligible("TST_n01", standardCommon, {}) && IsCraftable(standardCommon));

    // never rolled in a pack
    PackPool const pool = PackPool::FromCatalog(*catalog);
    std::size_t questInPool = 0;
    for (std::size_t b = 0; b < kPackRarityCount; ++b)
        for (std::string const& card : pool.Cards(static_cast<PackRarity>(b)))
            questInPool += card.compare(0, 4, "FP1_") == 0 ? 1 : 0;
    CHECK(questInPool == 0 && pool.Size() > 15);

    // deck rules: legal like any collectible card (class and copy limits apply; the Legendary once)
    DeckCards deck = Neutrals(28);
    deck["FP1_028"] = 2;
    CHECK(ValidateDeckCards(*catalog, "MAGE", deck) == DeckWhy::None);
    DeckCards big = Neutrals(2);
    big["FP1_028"] = 3;
    CHECK(ValidateDeckCards(*catalog, "MAGE", big) == DeckWhy::Copies);
    DeckCards legendary = Neutrals(2);
    legendary["FP1_010"] = 1;
    CHECK(ValidateDeckCards(*catalog, "MAGE", legendary) == DeckWhy::None);
    legendary["FP1_010"] = 2;
    CHECK(ValidateDeckCards(*catalog, "MAGE", legendary) == DeckWhy::Copies);
    DeckCards classCard = Neutrals(2);
    classCard["FP1_018"] = 2; // Duplicate (Mage)
    CHECK(ValidateDeckCards(*catalog, "MAGE", classCard) == DeckWhy::None);
    CHECK(ValidateDeckCards(*catalog, "WARRIOR", classCard) == DeckWhy::CardClass);
    classCard.erase("FP1_018");
    classCard["FP1_021"] = 2; // Death's Bite (Warrior)
    CHECK(ValidateDeckCards(*catalog, "WARRIOR", classCard) == DeckWhy::None);
    CHECK(ValidateDeckCards(*catalog, "MAGE", classCard) == DeckWhy::CardClass);

    // service level: a quest reward fills protected copies, packs refuse, crafting and scrapping refuse
    DeckWorld w;
    w.collections.SetCatalog(catalog);
    w.collections.SetOnline(5);
    w.decks.SetOnline(5);
    MutationResult g = w.collections.Grant(5, "FP1_028", 2, GrantSource::Quest, ProtectionMode::Reward, "quest_reward", "quest:1");
    CHECK(g.status == MutationStatus::Ok && g.mutation.after == Count(2, 0) && g.mutation.DeltaProtected() == 2);
    g = w.collections.Grant(5, "FP1_028", 1, GrantSource::Quest, ProtectionMode::Reward, "quest_reward", "quest:2");
    CHECK(g.status == MutationStatus::Ok && g.mutation.after == Count(2, 1)); // the replay copy is an extra
    g = w.collections.Grant(5, "FP1_010", 1, GrantSource::Pack, ProtectionMode::Unprotected, "pack", "x");
    CHECK(g.status == MutationStatus::Rejected && g.error == OwnershipError::SourceNotAllowed);
    g = w.collections.Grant(5, "FP1_010", 1, GrantSource::SpecialReward, ProtectionMode::Reward, "arena_special", "x");
    CHECK(g.status == MutationStatus::Rejected && g.error == OwnershipError::SourceNotAllowed);
    CraftResult cr = w.collections.Craft(5, "aaaaaaaaaaa1", "FP1_028", "char:42");
    CHECK(cr.status == CraftStatus::Refused && cr.why == CraftWhy::Card);
    cr = w.collections.Scrap(5, "bbbbbbbbbbb1", "FP1_028", 1, "char:42"); // even the unprotected extra
    CHECK(cr.status == CraftStatus::Refused && cr.why == CraftWhy::Card);
    Collection owned;
    CHECK(w.collections.Get(5, owned) && owned.Get("FP1_028") == Count(2, 1) && owned.Get("FP1_010") == Count(0, 0));

    // a deck with owned Naxxramas cards is saved, owned and (with engine support) match-ready; without the cards it is
    // "owned"; a card the engine cannot play keeps the deck out of a match with a named reason
    DeckCards cards = Neutrals(28);
    cards["FP1_028"] = 2;
    w.Own(5, Neutrals(28));
    DeckSaveResult saved = w.decks.SaveDeck(5, SaveReq("aaaaaaaaaaa1", 0, 0, "MAGE", "Naxx", cards), "MAGE");
    CHECK(saved.status == DeckStatus::Ok && saved.state == "ok");
    DeckCards unowned = Neutrals(28);
    unowned["FP1_018"] = 2; // Duplicate: not owned
    saved = w.decks.SaveDeck(5, SaveReq("aaaaaaaaaaa2", 0, 0, "MAGE", "Unowned", unowned), "MAGE");
    CHECK(saved.status == DeckStatus::Ok && saved.state == "owned");
    w.decks.SetEngineSupport([](std::string const&) { return true; });
    CHECK(w.decks.CheckForMatch(5, 1, "MAGE").Ready());
    CHECK(!w.decks.CheckForMatch(5, 2, "MAGE").Ready() && w.decks.CheckForMatch(5, 2, "MAGE").Text() == "owned");
    w.decks.SetEngineSupport([](std::string const& card) { return card.compare(0, 4, "FP1_") != 0; });
    DeckMatchCheck const m = w.decks.CheckForMatch(5, 1, "MAGE");
    CHECK(!m.Ready() && m.state.Playable() && m.engine && m.unsupportedCard == "FP1_028" && m.Text() == "engine");
    CHECK(DeckNotReadyText(m, "MAGE", "Undertaker") == "Undertaker does not work in matches yet");
    // owning Naxxramas cards never unlocks a class: a Warrior character still cannot play the Mage deck
    w.decks.SetEngineSupport([](std::string const&) { return true; });
    CHECK(w.decks.CheckForMatch(5, 1, "WARRIOR").Text() == "locked");
}

static void TestHordeRogueQuestView()
{
    // Fixture matches the core encoder: fixed fields, four strings, interleaved
    // NPC/item requirements, four labels. Counters must retain their original slots.
    std::vector<uint8_t> native;
    auto word = [&native](uint32_t n)
    {
        for (unsigned b = 0; b < 4; ++b) native.push_back(uint8_t(n >> (8 * b)));
    };
    word(kHsHordeRogueInvestigation);
    for (uint32_t i = 1; i < 39; ++i) word(i);
    auto str = [&native](std::string const& s)
    {
        native.insert(native.end(), s.begin(), s.end()); native.push_back(0);
    };
    str("A Deck Goes Missing"); str("Old summary"); str("Full story"); str("End");
    for (uint32_t i = 0; i < 4; ++i)
    {
        word(kHsHordeRogueCredits[i]); word(1);
        word(i == 0 ? kHsHordeRogueInvestigationReport : 0); word(i == 0 ? 1 : 0);
    }
    char const* const names[] = { "Rikka", "Gora", "Zikko", "Sable" };
    for (auto name : names) str(std::string("Talk to ") + name);
    auto const original = native;
    for (uint32_t mask = 0; mask < 16; ++mask)
    {
        std::array<uint32_t, 4> counters{};
        for (unsigned i = 0; i < 4; ++i) counters[i] = (mask >> i) & 1;
        uint32_t const stage = HsHordeRogueStep(counters);
        std::vector<uint8_t> view;
        CHECK(HsHordeRogueQuestView(native, stage, view));
        CHECK(std::equal(native.begin(), native.begin() + 156, view.begin()));
        std::size_t pos = 156;
        auto readStr = [&view, &pos]()
        {
            std::string s;
            while (pos < view.size() && view[pos]) s += char(view[pos++]);
            ++pos; return s;
        };
        auto readWord = [&view, &pos]()
        {
            uint32_t n = 0;
            for (unsigned b = 0; b < 4; ++b) n |= uint32_t(view[pos++]) << (8 * b);
            return n;
        };
        CHECK(readStr() == "A Deck Goes Missing");
        auto const summary = readStr();
        CHECK(summary.find(stage < 4 ? names[stage] : "For'sen") != std::string::npos);
        CHECK(readStr() == "Full story"); CHECK(readStr() == "End");
        unsigned visible = 0;
        for (unsigned i = 0; i < 4; ++i)
        {
            auto const npc = readWord(), count = readWord(), item = readWord(), itemCount = readWord();
            CHECK(npc == (stage == i ? kHsHordeRogueCredits[i] : 0));
            CHECK(count == (stage == i ? 1u : 0u));
            CHECK(item == (stage == 4 && i == 0 ? kHsHordeRogueInvestigationReport : 0));
            CHECK(itemCount == (stage == 4 && i == 0 ? 1u : 0u));
            visible += unsigned(npc != 0) + unsigned(item != 0);
        }
        CHECK(visible == 1);
        for (unsigned i = 0; i < 4; ++i)
            CHECK(readStr() == (stage == i ? std::string("Talk to ") + names[i] : ""));
        CHECK(pos == view.size()); CHECK(native == original);
    }
    std::vector<uint8_t> unchanged{1, 2, 3};
    for (std::size_t length = 0; length < native.size(); ++length)
    {
        std::vector<uint8_t> truncated(native.begin(), native.begin() + length);
        CHECK(!HsHordeRogueQuestView(truncated, 0, unchanged));
        CHECK((unchanged == std::vector<uint8_t>{1, 2, 3}));
    }
    CHECK(!HsHordeRogueQuestView(native, 5, unchanged));
    auto other = native; other[0] ^= 1;
    CHECK(!HsHordeRogueQuestView(other, 0, unchanged));
    other = native; other.push_back(0);
    CHECK(!HsHordeRogueQuestView(other, 0, unchanged));
}

static void TestHordeRogueQuestRules()
{
    // Every saved counter combination resumes at the first missing contact, even malformed later progress.
    for (uint32_t mask = 0; mask < 16; ++mask)
    {
        std::array<uint32_t, 4> counters{};
        uint32_t expected = 4;
        for (uint32_t i = 0; i < 4; ++i)
        {
            counters[i] = (mask >> i) & 1;
            if (!counters[i] && expected == 4)
                expected = i;
        }
        CHECK(HsHordeRogueStep(counters) == expected);
        for (uint32_t i = 0; i < 4; ++i)
        {
            CHECK(HsHordeRogueContactAllowed(true, true, counters, kHsHordeRogueContacts[i]) == (i == expected));
            CHECK(!HsHordeRogueContactAllowed(false, true, counters, kHsHordeRogueContacts[i]));
            CHECK(!HsHordeRogueContactAllowed(true, false, counters, kHsHordeRogueContacts[i]));
            CHECK(HsHordeRogueDirectionsAllowed(true, true, counters, i) == (expected == i + 1));
            CHECK(!HsHordeRogueDirectionsAllowed(false, true, counters, i));
            CHECK(!HsHordeRogueDirectionsAllowed(true, false, counters, i));
        }
        CHECK(!HsHordeRogueDirectionsAllowed(true, true, counters, 4));
        CHECK(!HsHordeRogueContactAllowed(true, true, counters, kHsHordeRogue));
        // The fifth step cannot happen early, off-faction, after turn-in, or once its native item exists.
        for (uint32_t flags = 0; flags < 8; ++flags)
            CHECK(HsHordeRogueReturnAllowed((flags & 1) != 0, (flags & 2) != 0,
                counters, (flags & 4) != 0) == (expected == 4 && flags == 3));
    }
    // Losing/destroying/banking evidence suspends the report; saved report credit never offers it again.
    for (uint32_t flags = 0; flags < 16; ++flags)
        CHECK(HsHordeRogueReportAllowed((flags & 1) != 0, (flags & 2) != 0,
            (flags & 4) != 0, (flags & 8) != 0) == (flags == 7));
    // The quest-deck vendor requires the investigation turn-in and the open recovery quest.
    for (uint32_t flags = 0; flags < 8; ++flags)
        CHECK(HsHordeRogueTradeAllowed((flags & 1) != 0, (flags & 2) != 0, (flags & 4) != 0) == (flags == 7));
    Hs::MatchEnd end;
    end.meta.opponentEntry = kHsHordeRogue;
    end.res = "win";
    end.why = "hero";
    CHECK(HsClassQuestVictory(end));
    for (char const* why : { "concede", "logout", "disconnect", "timeout", "draw", "gm", "error" })
    {
        end.why = why;
        CHECK(!HsClassQuestVictory(end));
    }
    end.why = "hero";
    end.res = "loss";
    CHECK(!HsClassQuestVictory(end));
    end.res = "win";
    end.meta.opponentEntry = kHsHordeRogueSable;
    CHECK(!HsClassQuestVictory(end));
}

static void TestAllianceRogueQuestRules()
{
    // Reconstructed from native saved progress: every combination, including
    // later counters without earlier ones, must reveal only the first missing clue.
    for (uint32_t mask = 0; mask < 8; ++mask)
    {
        std::array<uint32_t, 3> clues = { mask & 1, mask & 2, mask & 4 };
        uint32_t expected = 0;
        while (expected < 3 && clues[expected]) ++expected;
        CHECK(HsAllianceRogueClueStep(clues) == expected);
        for (uint32_t entry : { uint32_t(1478), uint32_t(1323), kHsAllianceRogue, kHsAllianceRogueOperative })
        {
            CHECK(HsAllianceRogueContactAllowed(true, true, clues, entry) ==
                (expected < 3 && kHsAllianceRogueContacts[expected] == entry));
            CHECK(!HsAllianceRogueContactAllowed(false, true, clues, entry));
            CHECK(!HsAllianceRogueContactAllowed(true, false, clues, entry));
        }
    }
    // Regression: the upstairs card and Osric must work even if the wager was
    // won but its card was not claimed. Other cards' possession cannot hide them.
    for (uint32_t held = 0; held < 8; ++held)
        for (uint32_t flags = 0; flags < 4; ++flags)
        {
            std::array<bool, 3> cards = { (held & 1) != 0, (held & 2) != 0, (held & 4) != 0 };
            for (uint32_t i = 0; i < 3; ++i)
                CHECK(HsAllianceRogueCardAllowed((flags & 1) != 0, (flags & 2) != 0, cards[i]) ==
                    (flags == 3 && !cards[i]));
        }
    for (uint32_t flags = 0; flags < 8; ++flags)
        CHECK(HsAllianceRogueDiceAllowed((flags & 1) != 0, (flags & 2) != 0,
            (flags & 4) != 0) == (flags == 3));
    // Missing or banked dice cannot win; the ready, concealed switch wins on
    // the very first attempt. There is no random detection/failure roll.
    CHECK(!HsAllianceRogueRiggedWin(false, false));
    CHECK(!HsAllianceRogueRiggedWin(false, true));
    CHECK(!HsAllianceRogueRiggedWin(true, false));
    CHECK(HsAllianceRogueRiggedWin(true, true));
    for (uint32_t mask = 0; mask < 32; ++mask)
        for (uint32_t level : { 9u, 10u, 60u })
            CHECK(HsAllianceRogueInvitationAllowed((mask & 1) != 0, level, (mask & 2) != 0,
                (mask & 4) != 0, (mask & 8) != 0, (mask & 16) != 0) == (mask == 7 && level >= 10));
    Hs::MatchEnd end;
    end.meta.opponentEntry = kHsAllianceRogue;
    end.res = "win";
    end.why = "hero";
    CHECK(HsClassQuestVictory(end));
    for (char const* why : { "concede", "logout", "disconnect", "timeout", "draw", "gm", "error" })
    {
        end.why = why;
        CHECK(!HsClassQuestVictory(end));
    }
    end.why = "hero";
    for (char const* res : { "loss", "draw", "int" })
    {
        end.res = res;
        CHECK(!HsClassQuestVictory(end));
    }
    end.res = "win";
    end.meta.opponentEntry = kHsAllianceRogueOperative;
    CHECK(!HsClassQuestVictory(end));
}

static void TestHordeDruidQuestRules()
{
    // These counters are read again after relog/closing gossip: Nan alone never unlocks turn-in.
    CHECK(HsHordeDruidFeedStep({ 0, 0 }) == 0);
    CHECK(HsHordeDruidFeedStep({ 1, 0 }) == 1);
    CHECK(HsHordeDruidFeedStep({ 1, 1 }) == 2);
    CHECK(HsHordeDruidFeedStep({ 0, 1 }) == 0);
    // A card the character already holds (including bank storage) cannot be farmed for other objectives.
    CHECK(HsHordeDruidCardAllowed(true, true, false));
    CHECK(!HsHordeDruidCardAllowed(false, true, false));
    CHECK(!HsHordeDruidCardAllowed(true, false, false));
    CHECK(!HsHordeDruidCardAllowed(true, true, true));
    CHECK(HsHordeDruidCardViewFlags(true) == 1); // Native quest activation; sparkle is provided by the type-3 client path.
    CHECK(HsHordeDruidCardViewFlags(false) == 4);
    CHECK((HsHordeDruidCardViewFlags(false) & 0x01u) == 0); // Ineligible viewers never get quest activation.
    CHECK((HsHordeDruidCardViewFlags(true) & 0x04u) == 0); // Eligible pickups retain interaction.
    Hs::MatchEnd end;
    end.meta.opponentEntry = kHsHordeDruid;
    end.res = "win";
    end.why = "hero";
    CHECK(HsClassQuestVictory(end));
    for (char const* why : { "concede", "logout", "timeout", "draw", "gm", "error" })
    {
        end.why = why;
        CHECK(!HsClassQuestVictory(end));
    }
    end.why = "hero";
    end.res = "loss";
    CHECK(!HsClassQuestVictory(end));
}

static void TestWarlockQuestRules()
{
    CHECK(HsWarlockNextStep({0, 0, 0}, 3) == 0);
    CHECK(HsWarlockNextStep({1, 0, 0}, 3) == 1);
    CHECK(HsWarlockNextStep({1, 1, 0}, 3) == 2);
    CHECK(HsWarlockNextStep({1, 1, 1}, 3) == 3);
    CHECK(HsWarlockNextStep({0, 1, 1}, 3) == 0); // holes never skip an earlier lesson
    CHECK(HsWarlockNextStep({1, 1, 0}, 2) == 2); // witness needs only two counters
    CHECK(HsWarlockNextStep({1, 0, 0}, 1) == 1); // fee settled; await native item turn-in
    CHECK(HsWarlockNextStep({0, 0, 0}, 4) == 3);
    for (uint32_t flags = 0; flags < 32; ++flags)
        for (uint32_t level : {9u, 10u, 60u})
            CHECK(HsWarlockLessonAllowed((flags & 1) != 0, level, (flags & 2) != 0,
                (flags & 4) != 0, (flags & 8) != 0, (flags & 16) != 0) ==
                (flags == 23 && level >= 10));
    for (uint32_t flags = 0; flags < 4; ++flags)
        CHECK(HsWarlockCardAllowed((flags & 1) != 0, (flags & 2) != 0) == (flags == 1));
    for (uint32_t flags = 0; flags < 8; ++flags)
        CHECK(HsWarlockTradeAllowed((flags & 1) != 0, (flags & 2) != 0, (flags & 4) != 0) == (flags == 5));
    CHECK(!HsWarlockTradeAllowed(true, false, false)); // ashes in the bank cannot pay
    CHECK(!HsWarlockTradeAllowed(true, true, true)); // an existing card prevents a second purchase

    std::array<uint32_t, 7> const entries = {kHsVarka, kHsVarka, kHsVarka, kHsSkrit, kHsOwen, kHsMaren, kHsNib};
    for (uint32_t exchange = 0; exchange < 9; ++exchange)
    {
        uint32_t const pages = exchange < 3 || exchange == 5 ? 2 : exchange < 7 ? 4 : 0;
        CHECK(HsWarlockPageCount(exchange) == pages);
        for (uint32_t entry : {kHsVarka, kHsSkrit, kHsMaren, kHsNib, kHsOwen, 0u, 919501u})
            CHECK(HsWarlockExchangeCreature(exchange, entry) == (exchange < 7 && entry == entries[exchange]));
        for (uint32_t action = kHsWarlockActionFirst - 1; action <= kHsWarlockActionLast + 1; ++action)
        {
            CHECK(!HsWarlockActionAllowed(exchange, action, false));
            CHECK(HsWarlockActionAllowed(exchange, action, true) ==
                (exchange < 7 && action >= kHsWarlockActionFirst && action <= kHsWarlockActionLast &&
                 (action - kHsWarlockActionFirst) / 4 == exchange &&
                 (action - kHsWarlockActionFirst) % 4 < pages));
        }
    }
    CHECK(!HsWarlockActionAllowed(0, 0xffffffffu, true));
    for (uint32_t entry : {kHsVarka, kHsMaren})
    {
        auto const* opponent = Hs::ClassQuestOpponentOf(entry);
        CHECK(opponent != nullptr);
        if (!opponent) continue;
        CHECK(opponent->questId == (entry == kHsVarka ? kHsVarkaDuel : kHsMarenDuel));
        bool const alliance = entry == kHsMaren;
        CHECK(opponent->alliance == alliance);
        CHECK(HsClassQuestDirectAllowed(*opponent, alliance, true));
        CHECK(!HsClassQuestDirectAllowed(*opponent, !alliance, true));
        CHECK(!HsClassQuestDirectAllowed(*opponent, alliance, false));
        MatchEnd end;
        end.meta.opponentEntry = entry;
        end.res = "win";
        end.why = "hero";
        CHECK(HsClassQuestVictory(end));
        for (char const* result : {"loss", "draw", "int", "surrender"})
        {
            end.res = result;
            CHECK(!HsClassQuestVictory(end));
        }
        end.res = "win";
        for (char const* reason : {"concede", "turns", "logout", "stuck", "gm", "error"})
        {
            end.why = reason;
            CHECK(!HsClassQuestVictory(end));
        }
    }
    for (uint32_t entry : {kHsSkrit, kHsNib, kHsOwen})
        CHECK(Hs::ClassQuestOpponentOf(entry) == nullptr);
    CHECK(kHsWarlockCredits[3] == 0); // the card is the objective; no duplicate conversation credit
}

static void TestHordePriestQuestRules()
{
    for (bool horde : { false, true })
        for (bool incomplete : { false, true })
            for (bool rewarded : { false, true })
                for (uint32_t level : { 1u, 9u, 10u, 60u })
                    for (bool introduction : { false, true })
                        for (uint32_t count = 0; count <= 6; ++count)
                            CHECK(HsHordePriestPickupAllowed(horde, incomplete, rewarded, level, introduction, count) ==
                                (horde && incomplete && !rewarded && level >= 10 && introduction && count < 4));
    // A consumed chest is unavailable even before its native update begins respawn.
    // A delayed respawn stays unavailable to GMs as well as ordinary players.
    for (bool spawned : { false, true })
        for (bool deleted : { false, true })
            for (bool ready : { false, true })
                CHECK(HsQuestPickupAvailable(spawned, deleted, ready) ==
                    (spawned && !deleted && ready));
    CHECK(HsHordePriestPickupViewFlags(true) == 1);
    CHECK(HsHordePriestPickupViewFlags(false) == 4);
    auto const* teacher = Hs::ClassQuestOpponentOf(kHsHordePriest);
    CHECK(teacher && teacher->questId == kHsHordePriestDuel && !teacher->alliance);
    if (!teacher) return;
    CHECK(HsClassQuestDirectAllowed(*teacher, false, true));
    CHECK(!HsClassQuestDirectAllowed(*teacher, true, true));
    CHECK(!HsClassQuestDirectAllowed(*teacher, false, false));
    Hs::MatchEnd end;
    end.meta.opponentEntry = kHsHordePriest;
    end.res = "win";
    end.why = "hero";
    CHECK(HsClassQuestVictory(end));
    for (char const* why : { "concede", "logout", "timeout", "draw", "turns", "gm", "error" })
    {
        end.why = why;
        CHECK(!HsClassQuestVictory(end));
    }
    end.why = "hero";
    for (char const* result : { "loss", "draw", "int" })
    {
        end.res = result;
        CHECK(!HsClassQuestVictory(end));
    }
    end.res = "win";
    end.meta.opponentEntry = 919501;
    CHECK(!HsClassQuestVictory(end));
}

static void TestHunterQuestRules()
{
    uint32_t const spots[2][3][3] = {{{0,1,2},{2,0,1},{1,0,2}},{{0,1,2},{2,1,0},{0,1,2}}};
    for (uint32_t faction=0; faction<2; ++faction)
    for (uint32_t round=0; round<3; ++round)
    {
        HsHunterShow show;
        uint32_t reveals=0;
        for (uint32_t animal=0; animal<3; ++animal)
        {
            CHECK(HsHunterSpot(faction==1,round,animal)==spots[faction][round][animal]);
            CHECK(show.phase==HsHunterShowPhase::Walk && show.animal==animal);
            CHECK(!show.Advance(UINT32_MAX)); // Time alone cannot replace reaching the point.
            CHECK(!show.Arrive(false,0));
            CHECK(!show.Arrive(true,0.36f));
            CHECK(show.Arrive(true,0.1f));
            CHECK(!show.Arrive(true,0)); // Repeated arrival cannot emit another name.
            CHECK(show.phase==HsHunterShowPhase::Name && show.animal==animal);
            CHECK(!show.Advance(kHsHunterNamePause-1));
            CHECK(show.Advance(UINT32_MAX)); // Late update reveals only this animal.
            ++reveals;
            CHECK(show.phase==HsHunterShowPhase::Describe && show.animal==animal && show.elapsed==0);
            CHECK(!show.Advance(0));
            CHECK(!show.Advance(kHsHunterDescriptionPause-1));
            CHECK(show.Advance(1));
            CHECK(show.animal==animal+1);
            CHECK(show.phase==(animal==2 ? HsHunterShowPhase::Done : HsHunterShowPhase::Walk));
            auto const& introduction = faction==1 ? kHsFennIntroductions[round][animal] : kHsBoroIntroductions[round][animal];
            CHECK(introduction.name && introduction.description);
        }
        CHECK(reveals==3 && show.phase==HsHunterShowPhase::Done);
        CHECK(!show.Advance(UINT32_MAX) && !show.Arrive(true,0));
    }
    for (bool alliance : {false,true})
    {
        CHECK(HsHunterSpot(alliance,3,0)==3 && HsHunterSpot(alliance,0,3)==3 &&
            HsHunterSpot(alliance,UINT32_MAX,UINT32_MAX)==3);
        for (uint32_t round=0; round<3; ++round)
        {
            uint32_t mask=0;
            for (uint32_t animal=0; animal<3; ++animal) mask |= 1u << HsHunterSpot(alliance,round,animal);
            CHECK(mask==7); // Each authored route visits all three standpoints exactly once.
            if (round) CHECK(HsHunterSpot(alliance,round-1,2)==HsHunterSpot(alliance,round,0));
        }
    }
    CHECK(HsHunterPathEndpointAllowed(0.8f,2.0f));
    CHECK(!HsHunterPathEndpointAllowed(0.81f,0) && !HsHunterPathEndpointAllowed(0,2.01f));
    CHECK(!HsHunterPathEndpointAllowed(-1,0) && !HsHunterPathEndpointAllowed(0,-1));
    CHECK(!HsHunterPathEndpointAllowed(std::numeric_limits<float>::quiet_NaN(),0));
    CHECK(HsHunterAtPoint(true,0.35f) && !HsHunterAtPoint(true,0.351f));
    CHECK(!HsHunterAtPoint(false,0) && !HsHunterAtPoint(true,-1));
    CHECK(!HsHunterAtPoint(true,std::numeric_limits<float>::infinity()));
    for (auto const& chain : kHsHunterChains)
    {
        CHECK(HsHunterChainFor(chain.teacher) == &chain);
        CHECK(HsHunterChainFor(chain.helper) == &chain);
        CHECK(!HsHunterChainFor(chain.firstCredit));
        for (uint32_t mask=0; mask<8; ++mask)
        {
            std::array<uint32_t,3> counters={mask&1,(mask>>1)&1,(mask>>2)&1};
            uint32_t expected=0;
            while (expected<3 && counters[expected]) ++expected;
            CHECK(HsHunterNextStep(counters,3)==expected);
            CHECK(HsHunterNextStep(counters,2)==std::min(expected,2u));
        }
        CHECK(HsHunterNextStep({0,8,9},3)==0);
        CHECK(HsHunterNextStep({5,0,9},3)==1);
        CHECK(HsHunterNextStep({1,1,1},4)==3);
        for (uint32_t flags=0; flags<32; ++flags)
            for (uint32_t level : {0u,9u,10u,60u})
                CHECK(HsHunterLessonAllowed((flags&1)!=0,chain,level,(flags&2)!=0,
                    (flags&4)!=0,(flags&8)!=0,(flags&16)!=0)==
                    (((flags&1)!=0)==chain.alliance && level>=10 && (flags&2) && (flags&4) && !(flags&8) && (flags&16)));
        std::array<uint32_t,3> saved={0,0,0};
        for (uint32_t round=0; round<3; ++round)
        {
            CHECK(HsHunterNextStep(saved,3)==round);
            auto launch=HsHunterDecode(chain,chain.firstAction+round);
            CHECK(HsHunterReplyAllowed(launch,true,true,true,round,false,HsHunterPhase::Queued));
            CHECK(!HsHunterReplyAllowed(launch,true,true,true,round,true,HsHunterPhase::Choosing));
            for (uint32_t choice=0; choice<3; ++choice)
            {
                auto answer=HsHunterDecode(chain,chain.firstAction+8+round*3+choice);
                CHECK(answer.kind==HsHunterAction::Choose && answer.step==round && answer.page==choice);
                CHECK(HsHunterReplyAllowed(answer,true,true,true,round,true,HsHunterPhase::Choosing));
                CHECK(!HsHunterReplyAllowed(answer,false,true,true,round,true,HsHunterPhase::Choosing));
                CHECK(!HsHunterReplyAllowed(answer,true,true,true,round,false,HsHunterPhase::Choosing));
                CHECK(!HsHunterReplyAllowed(answer,true,true,true,round,true,HsHunterPhase::AnswerQueued));
                CHECK(!HsHunterReplyAllowed(answer,true,true,true,round,true,HsHunterPhase::Expired));
                CHECK(!HsHunterReplyAllowed(answer,true,true,true,round,true,HsHunterPhase::Presenting));
                CHECK(!HsHunterReplyAllowed(answer,true,true,true,round,true,HsHunterPhase::Cancelling));
                CHECK(!HsHunterReplyAllowed(answer,true,false,true,round,true,HsHunterPhase::Choosing));
                auto ack=HsHunterDecode(chain,chain.firstAction+20+round);
                CHECK(!HsHunterReplyAllowed(ack,true,true,true,round,true,HsHunterPhase::Choosing));
                CHECK(HsHunterReplyAllowed(ack,true,true,true,round,true,HsHunterPhase::Reaction));
                CHECK(!HsHunterReplyAllowed(ack,true,true,true,round,false,HsHunterPhase::Reaction));
                CHECK(!HsHunterReplyAllowed(ack,true,true,true,round+1,true,HsHunterPhase::Reaction));
            }
            saved[round]=1; // Only acknowledgement saves, whichever animal was selected.
            CHECK(HsHunterNextStep(saved,3)==round+1);
        }
        CHECK(HsHunterNextStep({0,0,0},3)==0); // Abandon/reaccept.
        for (uint32_t step=0; step<2; ++step)
            for (uint32_t page=0; page<(step ? 2u : 3u); ++page)
            {
                auto talk=HsHunterDecode(chain,chain.firstAction+32+step*4+page);
                CHECK(HsHunterReplyAllowed(talk,true,step==1,false,step,false,HsHunterPhase::Queued));
                CHECK(!HsHunterReplyAllowed(talk,false,step==1,false,step,false,HsHunterPhase::Queued));
                CHECK(!HsHunterReplyAllowed(talk,true,step!=1,false,step,false,HsHunterPhase::Queued));
                CHECK(!HsHunterReplyAllowed(talk,true,step==1,false,1-step,false,HsHunterPhase::Queued));
            }
        for (uint32_t offset=0; offset<64; ++offset)
        {
            bool const defined=offset<3 || (offset>=8 && offset<17) || (offset>=20 && offset<23) ||
                (offset>=32 && offset<35) || (offset>=36 && offset<38);
            CHECK((HsHunterDecode(chain,chain.firstAction+offset).kind!=HsHunterAction::Invalid)==defined);
        }
        CHECK(HsHunterDecode(chain,chain.firstAction-1).kind==HsHunterAction::Invalid);
        CHECK(HsHunterDecode(chain,chain.firstAction+64).kind==HsHunterAction::Invalid);
        CHECK(HsHunterDecode(chain,UINT32_MAX).kind==HsHunterAction::Invalid);
        for (uint32_t i=0;i<9;++i)
        { CHECK(HsHunterProjection(chain.firstProjection+i)); CHECK(!Hs::ClassQuestOpponentOf(chain.firstProjection+i)); }
        CHECK(!Hs::ClassQuestOpponentOf(chain.helper));
        for (uint32_t i=0;i<5;++i) CHECK(!Hs::ClassQuestOpponentOf(chain.firstCredit+i));
        auto const* opponent=Hs::ClassQuestOpponentOf(chain.teacher);
        CHECK(opponent && opponent->questId==chain.firstQuest+2 && opponent->alliance==chain.alliance);
        if (!opponent) continue;
        CHECK(HsClassQuestDirectAllowed(*opponent,chain.alliance,true));
        CHECK(!HsClassQuestDirectAllowed(*opponent,!chain.alliance,true));
        CHECK(!HsClassQuestDirectAllowed(*opponent,chain.alliance,false)); // Complete/roster states don't open a duel.
        Hs::MatchEnd end;
        end.meta.opponentEntry=chain.teacher; end.res="win"; end.why="hero";
        CHECK(HsClassQuestVictory(end));
        for (char const* result : {"loss","draw","int","surrender"})
        { end.res=result; CHECK(!HsClassQuestVictory(end)); }
        end.res="win";
        for (char const* reason : {"concede","turns","logout","stuck","gm","error"})
        { end.why=reason; CHECK(!HsClassQuestVictory(end)); }
        end.why="hero"; end.meta.opponentEntry=chain.helper; CHECK(!HsClassQuestVictory(end));
    }
    for (uint32_t flags=0;flags<16;++flags)
        for (uint32_t step=0;step<4;++step)
            CHECK(HsHunterSceneContinues((flags&1)!=0,(flags&2)!=0,(flags&4)!=0,(flags&8)!=0,step,1)==
                (flags==15 && step==1)); // Token, identity, death/leave and abandon each cancel.
    CHECK(!HsHunterVisualExpired(44999));
    CHECK(HsHunterVisualExpired(45000));
    CHECK(HsHunterVisualExpired(UINT32_MAX));
}

static void TestAlliancePriestClaspScene()
{
    // Only a successful native reward transition starts the disposable scene.
    // Failed turn-in, an already rewarded quest, and a duplicate callback do not.
    for (uint32_t flags = 0; flags < 64; ++flags)
        for (uint32_t map : {0u, 1u, 530u})
            for (uint32_t instance : {0u, 1u, UINT32_MAX})
                CHECK(HsAlliancePriestSceneStartAllowed((flags & 1) != 0, (flags & 2) != 0,
                    (flags & 4) != 0, (flags & 8) != 0, (flags & 16) != 0, (flags & 32) != 0,
                    map, instance) == (flags == 29 && map == 1 && instance == 0));
    CHECK(!HsAlliancePriestRewardTransition(false, false));
    CHECK(HsAlliancePriestRewardTransition(false, true));
    CHECK(!HsAlliancePriestRewardTransition(true, true));
    CHECK(!HsAlliancePriestRewardTransition(true, false));

    // Scene actor identity is stricter than merely sharing a creature entry.
    // Another summon or a real actor keeps its ordinary visibility/services.
    for (uint32_t entry : {kHsAlenya, kHsSyrelle, kHsAlliancePriestCredits[0], 919582u, 0u})
        for (bool sceneActor : {false, true})
            for (bool ownActor : {false, true})
                CHECK(HsAlliancePriestSceneVisible(entry, sceneActor, ownActor) ==
                    (entry != kHsSyrelle || !sceneActor || ownActor));
    for (uint32_t flags = 0; flags < 32; ++flags)
        for (uint32_t map : {0u, 1u, 530u})
            for (uint32_t instance : {0u, 1u})
                for (uint32_t elapsed : {0u, kHsAlliancePriestSceneLifetime - 1,
                    kHsAlliancePriestSceneLifetime, UINT32_MAX})
                    CHECK(HsAlliancePriestSceneContinues((flags & 1) != 0, (flags & 2) != 0,
                        (flags & 4) != 0, (flags & 8) != 0, (flags & 16) != 0, map, instance, elapsed) ==
                        (flags == 31 && map == 1 && instance == 0 && elapsed < kHsAlliancePriestSceneLifetime));

    // A final spline far from its requested endpoint, an interrupted spline,
    // and a wrong-floor/nonfinite distance cannot begin the apology.
    CHECK(HsAlliancePriestSceneEndpointAllowed(0.8f, 2.0f));
    CHECK(!HsAlliancePriestSceneEndpointAllowed(0.801f, 0));
    CHECK(!HsAlliancePriestSceneEndpointAllowed(0, 2.001f));
    for (float bad : {-1.0f, std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity()})
    {
        CHECK(!HsAlliancePriestSceneEndpointAllowed(bad, 0));
        CHECK(!HsAlliancePriestSceneEndpointAllowed(0, bad));
        CHECK(!HsAlliancePriestSceneAtPoint(true, bad));
    }
    CHECK(HsAlliancePriestSceneAtPoint(true, 0.35f));
    CHECK(!HsAlliancePriestSceneAtPoint(true, 0.351f));
    CHECK(!HsAlliancePriestSceneAtPoint(false, 0));

    HsAlliancePriestSceneClock clock;
    CHECK(clock.phase == HsAlliancePriestScenePhase::Walk);
    CHECK(!clock.Advance(UINT32_MAX)); // Time alone does not prove native arrival.
    CHECK(!clock.Arrive(false, 0));
    CHECK(!clock.Arrive(true, 0.351f));
    CHECK(clock.phase == HsAlliancePriestScenePhase::Walk);
    CHECK(clock.Arrive(true, 0.35f));
    CHECK(clock.phase == HsAlliancePriestScenePhase::Apology && clock.elapsed == 0);
    CHECK(!clock.Arrive(true, 0)); // Duplicate movement notifications cannot skip speech.
    std::array<uint32_t, 4> const delays = {1250, 5000, 5000, 4000};
    std::array<HsAlliancePriestScenePhase, 4> const next = {
        HsAlliancePriestScenePhase::Promise, HsAlliancePriestScenePhase::Reply,
        HsAlliancePriestScenePhase::Pause, HsAlliancePriestScenePhase::Done};
    for (uint32_t i = 0; i < delays.size(); ++i)
    {
        CHECK(!clock.Advance(delays[i] - 1));
        CHECK(clock.Advance(1));
        CHECK(clock.phase == next[i] && clock.elapsed == 0);
        CHECK(!clock.Advance(0));
    }
    CHECK(!clock.Advance(UINT32_MAX));
    CHECK(!clock.Arrive(true, 0));
    HsAlliancePriestSceneClock late;
    CHECK(late.Arrive(true, 0));
    for (auto phase : next)
    {
        CHECK(late.Advance(UINT32_MAX));
        CHECK(late.phase == phase && late.elapsed == 0); // One line per update, no time carried over.
        CHECK(!late.Advance(0));
    }
    CHECK(!late.Advance(UINT32_MAX));
}

static void TestAlliancePriestQuestFlow()
{
    // Native saved progress resumes the first missing conversation, even if a
    // later counter is already present. Abandon/reaccept leaves both missing.
    for (uint32_t mask = 0; mask < 4; ++mask)
        CHECK(HsAlliancePriestNextStep({mask & 1, (mask >> 1) & 1}) ==
            (!(mask & 1) ? 0u : !(mask & 2) ? 1u : 2u));
    CHECK(HsAlliancePriestNextStep({0, 7}) == 0);
    CHECK(HsAlliancePriestNextStep({3, 0}) == 1);
    CHECK(HsAlliancePriestNextStep({3, 7}) == 2);
    for (uint32_t flags = 0; flags < 32; ++flags)
        for (uint32_t level : {0u, 9u, 10u, 60u})
            CHECK(HsAlliancePriestLessonAllowed((flags & 1) != 0, level, (flags & 2) != 0,
                (flags & 4) != 0, (flags & 8) != 0, (flags & 16) != 0) ==
                (flags == 23 && level >= 10));
    // Active includes completed-but-unrewarded: a lost clasp may be replaced
    // until turn-in, but a rewarded or absent quest cannot grant another.
    CHECK(HsAlliancePriestLessonAllowed(true, 10, true, true, false, true));
    CHECK(!HsAlliancePriestLessonAllowed(true, 10, true, true, true, true));
    CHECK(!HsAlliancePriestLessonAllowed(true, 10, true, false, false, true));

    std::array<uint32_t, 3> const pageCounts = {4, 2, 4};
    std::array<uint32_t, 3> const speakers = {kHsSyrelle, kHsAlenya, kHsSyrelle};
    for (uint32_t exchange = 0; exchange < 5; ++exchange)
    {
        CHECK(HsAlliancePriestPageCount(exchange) == (exchange < 3 ? pageCounts[exchange] : 0));
        for (uint32_t entry : {kHsAlenya, kHsSyrelle, kHsAlliancePriestCredits[0],
            kHsAlliancePriestCredits[1], 0u})
            CHECK(HsAlliancePriestExchangeCreature(exchange, entry) ==
                (exchange < 3 && entry == speakers[exchange]));
        for (uint32_t action = kHsAlliancePriestActionFirst - 1;
            action <= kHsAlliancePriestActionLast + 1; ++action)
        {
            bool const ordinary = exchange < 3 && action > kHsAlliancePriestActionFirst + exchange * 4 &&
                action < kHsAlliancePriestActionFirst + exchange * 4 + pageCounts[exchange];
            bool const retry = exchange == 2 && action == kHsAlliancePriestRetry;
            CHECK(HsAlliancePriestActionAllowed(exchange, action, true) == (ordinary || retry));
            CHECK(!HsAlliancePriestActionAllowed(exchange, action, false)); // Unoffered or stale page.
        }
    }
    CHECK(!HsAlliancePriestActionAllowed(UINT32_MAX, kHsAlliancePriestActionFirst + 1, true));
    CHECK(!HsAlliancePriestActionAllowed(0, UINT32_MAX, true));
    CHECK(!HsAlliancePriestExchangeCreature(UINT32_MAX, kHsAlenya));
    CHECK(HsAlliancePriestPageCount(UINT32_MAX) == 0);

    std::array<uint32_t, 2> account = {0, 0};
    for (uint32_t page = 1; page <= 3; ++page)
    {
        CHECK(HsAlliancePriestNextStep(account) == 0);
        CHECK(HsAlliancePriestActionAllowed(0, kHsAlliancePriestActionFirst + page, true));
        if (page == 3) account[0] = 1; // The final promise to report saves the account.
    }
    CHECK(HsAlliancePriestNextStep(account) == 1);
    CHECK(!HsAlliancePriestActionAllowed(1, kHsAlliancePriestActionFirst + 3, true));
    CHECK(!HsAlliancePriestExchangeCreature(1, kHsSyrelle));
    CHECK(HsAlliancePriestActionAllowed(1, kHsAlliancePriestActionFirst + 5, true));
    account[1] = 1;
    CHECK(HsAlliancePriestNextStep(account) == 2);
    CHECK(!HsAlliancePriestActionAllowed(2, kHsAlliancePriestActionFirst + 5, true));

    // Bags and bank both prevent a duplicate. Item grants require the final
    // acknowledgement (or its offered full-bag retry) and available bag space.
    for (bool open : {false, true})
        for (uint32_t bags : {0u, 1u})
            for (uint32_t bank : {0u, 1u})
            {
                bool const owned = bags + bank != 0;
                CHECK(HsAlliancePriestClaspAllowed(open, owned) == (open && !owned));
                for (bool finalReply : {false, true})
                    for (bool room : {false, true})
                        CHECK(HsAlliancePriestClaspGrantAllowed(open, owned, finalReply, room) ==
                            (open && !owned && finalReply && room));
            }
    CHECK(HsAlliancePriestActionAllowed(2, kHsAlliancePriestActionFirst + 11, true));
    CHECK(!HsAlliancePriestClaspGrantAllowed(true, false, true, false));
    CHECK(HsAlliancePriestActionAllowed(2, kHsAlliancePriestRetry, true));
    CHECK(HsAlliancePriestClaspGrantAllowed(true, false, true, true));
    CHECK(!HsAlliancePriestClaspGrantAllowed(true, true, true, true)); // Repeated successful reply.
    CHECK(!HsAlliancePriestActionAllowed(2, kHsAlliancePriestRetry, false));

    auto const* teacher = Hs::ClassQuestOpponentOf(kHsAlenya);
    CHECK(teacher && teacher->questId == kHsAlliancePriestDuel && teacher->alliance);
    CHECK(Hs::ClassQuestOpponentOf(kHsSyrelle) == nullptr);
    for (uint32_t credit : kHsAlliancePriestCredits)
        CHECK(Hs::ClassQuestOpponentOf(credit) == nullptr);
    if (!teacher) return;
    CHECK(HsClassQuestDirectAllowed(*teacher, true, true));
    CHECK(!HsClassQuestDirectAllowed(*teacher, false, true));
    CHECK(!HsClassQuestDirectAllowed(*teacher, true, false)); // Includes complete, rewarded and absent.
    Hs::MatchEnd end;
    end.meta.opponentEntry = kHsAlenya;
    end.res = "win";
    end.why = "hero";
    CHECK(HsClassQuestVictory(end));
    for (char const* why : {"concede", "disconnect", "logout", "timeout", "death", "turns", "gm", "error"})
    {
        end.why = why;
        CHECK(!HsClassQuestVictory(end));
    }
    end.why = "hero";
    for (char const* result : {"loss", "draw", "int", "interrupted", "surrender"})
    {
        end.res = result;
        CHECK(!HsClassQuestVictory(end));
    }
    end.res = "win";
    for (uint32_t entry : {kHsSyrelle, kHsAlliancePriestCredits[0], kHsAlliancePriestCredits[1], 919502u, 0u})
    {
        end.meta.opponentEntry = entry;
        CHECK(!HsClassQuestVictory(end));
    }
}

static void TestWarriorQuestRules()
{
    MatchEnd end;
    end.meta.opponentEntry = kHsWarriorVeteran;
    end.res = "win";
    end.why = "hero";
    CHECK(HsWarriorQuestVictory(end));
    for (char const* result : { "loss", "draw", "int" })
    {
        end.res = result;
        CHECK(!HsWarriorQuestVictory(end));
    }
    end.res = "win";
    for (char const* why : { "concede", "turns", "logout", "stuck", "gm", "error" })
    {
        end.why = why;
        CHECK(!HsWarriorQuestVictory(end));
    }
    end.why = "hero";
    end.meta.opponentEntry = 919502;
    CHECK(!HsWarriorQuestVictory(end));

    // End-to-end metadata: a surrender / interruption at this NPC still cannot complete War Games.
    MatchService service(1);
    service.SetCardDb(TestCardDb());
    MatchStartParams params = FixtureParams(1, 77);
    params.meta.opponentEntry = kHsWarriorVeteran;
    CHECK(service.Start(params, 0).ok);
    GameActOutcome const surrendered = service.Act(1, Req("mid=1;a=concede"), 0);
    CHECK(surrendered.ended && !HsWarriorQuestVictory(surrendered.end));
    CHECK(service.Start(params, 0).ok);
    MatchAbortOutcome const interrupted = service.Abort(1, Interrupt::Stuck);
    CHECK(interrupted.ended && !HsWarriorQuestVictory(interrupted.end));
}

// ---------------------------------------------------------------------------
static void TestAllianceMageQuestRules()
{
    // Eligibility is shared by every WoW class. "Active" includes native
    // completed-but-unrewarded, preserving turn-in/reminders/visibility.
    for (uint32_t flags = 0; flags < 32; ++flags)
        for (uint32_t level : {0u, 9u, 10u, 60u, UINT32_MAX})
        {
            bool const alliance = (flags & 1) != 0;
            bool const intro = (flags & 2) != 0;
            bool const active = (flags & 4) != 0;
            bool const rewarded = (flags & 8) != 0;
            bool const previous = (flags & 16) != 0;
            bool const eligible = alliance && level >= 10 && intro && active && !rewarded && previous;
            CHECK(HsAllianceMageLessonAllowed(alliance, level, intro, active, rewarded, previous) == eligible);
            CHECK(HsAllianceMageImageVisible(alliance, level, intro, active, rewarded, previous) == eligible);
        }
    CHECK(HsAllianceMageImageVisible(true, 10, true, true, false, true)); // Incomplete/complete but unrewarded.
    CHECK(!HsAllianceMageImageVisible(true, 10, true, false, false, true)); // Absent/abandoned viewer.
    CHECK(!HsAllianceMageImageVisible(true, 10, true, true, true, true)); // Rewarded viewer.
    // Two characters' native states are independent; hiding for the latter
    // must leave the former's predicate eligible.
    bool const firstViewer = HsAllianceMageImageVisible(true, 10, true, true, false, true);
    bool const secondViewer = HsAllianceMageImageVisible(true, 10, true, false, false, true);
    CHECK(firstViewer && !secondViewer);

    static_assert(kHsAllianceMageQuestionCount == 10, "The approved quiz has ten questions");
    for (uint32_t saved = 0; saved <= 10; ++saved)
        CHECK(HsAllianceMageQuizNextQuestion(saved) == saved);
    CHECK(HsAllianceMageQuizNextQuestion(11) == 10);
    CHECK(HsAllianceMageQuizNextQuestion(UINT32_MAX) == 10); // Malformed data cannot index text/answer arrays.

    for (uint32_t question = 0; question < 10; ++question)
    {
        auto const launch = HsAllianceMageDecode(kHsAllianceMageActionFirst + question);
        auto const ack = HsAllianceMageDecode(kHsAllianceMageActionFirst + 48 + question);
        CHECK(launch.kind == HsAllianceMageAction::Launch && launch.step == question);
        CHECK(ack.kind == HsAllianceMageAction::Acknowledge && ack.step == question);
        CHECK(HsAllianceMageQuizReplyAllowed(launch, true, true, true, question));
        CHECK(HsAllianceMageQuizReplyAllowed(ack, true, true, true, question));
        CHECK(!HsAllianceMageQuizReplyAllowed(launch, false, true, true, question));
        CHECK(!HsAllianceMageQuizReplyAllowed(ack, false, true, true, question)); // No offered feedback acknowledgement.
        for (uint32_t choice = 0; choice < 3; ++choice)
        {
            auto const answer = HsAllianceMageDecode(kHsAllianceMageActionFirst + 16 + question * 3 + choice);
            CHECK(answer.kind == HsAllianceMageAction::Answer && answer.step == question && answer.page == choice);
            CHECK(HsAllianceMageQuizReplyAllowed(answer, true, true, true, question));
            CHECK(!HsAllianceMageQuizReplyAllowed(answer, false, true, true, question));
            CHECK(!HsAllianceMageQuizReplyAllowed(answer, true, false, true, question)); // Image/unrelated speaker.
            CHECK(!HsAllianceMageQuizReplyAllowed(answer, true, true, false, question)); // Complete/rewarded/absent quest.
            CHECK(!HsAllianceMageQuizReplyAllowed(answer, true, true, true, (question + 1) % 10));
            // Every answer leads to the same offered acknowledgement. Correctness
            // affects the explanation only; it cannot impose a retry/score gate.
            CHECK(HsAllianceMageQuizReplyAllowed(ack, true, true, true, question));
        }
        for (uint32_t saved : {question + 1, 10u, 11u, UINT32_MAX})
        {
            CHECK(!HsAllianceMageQuizReplyAllowed(ack, true, true, true, saved)); // Replayed or out-of-order native step.
            CHECK(!HsAllianceMageQuizReplyAllowed(launch, true, true, true, saved));
        }
        CHECK(!HsAllianceMageQuizReplyAllowed(ack, true, false, true, question));
        CHECK(!HsAllianceMageQuizReplyAllowed(ack, true, true, false, question));
    }
    CHECK(!HsAllianceMageQuizReplyAllowed({HsAllianceMageAction::Answer, 0, 3}, true, true, true, 0));
    CHECK(!HsAllianceMageQuizReplyAllowed({HsAllianceMageAction::Answer, 0, UINT32_MAX}, true, true, true, 0));
    CHECK(!HsAllianceMageQuizReplyAllowed({HsAllianceMageAction::Acknowledge, 0, 1}, true, true, true, 0));
    CHECK(!HsAllianceMageQuizReplyAllowed({HsAllianceMageAction::Tour, 0, 0}, true, true, true, 0));
    CHECK(!HsAllianceMageQuizReplyAllowed({}, true, true, true, 0));

    for (uint32_t stop = 0; stop < 3; ++stop)
    {
        CHECK(HsAllianceMageImageIndex(kHsAllianceMageImages[stop]) == stop);
        for (uint32_t page = 0; page < 3; ++page)
        {
            auto const reply = HsAllianceMageDecode(kHsAllianceMageActionFirst + 64 + stop * 4 + page);
            CHECK(reply.kind == HsAllianceMageAction::Tour && reply.step == stop && reply.page == page);
            CHECK(HsAllianceMageTourReplyAllowed(reply, true, stop, true, false));
            CHECK(!HsAllianceMageTourReplyAllowed(reply, false, stop, true, false));
            CHECK(!HsAllianceMageTourReplyAllowed(reply, true, stop, false, false));
            CHECK(!HsAllianceMageTourReplyAllowed(reply, true, stop, true, true)); // Completed stop cannot repeat.
            CHECK(!HsAllianceMageTourReplyAllowed(reply, true, (stop + 1) % 3, true, false));
            CHECK(!HsAllianceMageTourReplyAllowed(reply, true, 3, true, false)); // Teacher/unrelated creature.
        }
    }
    for (uint32_t entry : {kHsMitchellJones, kHsAllianceMageQuizCredit, 0u, UINT32_MAX})
        CHECK(HsAllianceMageImageIndex(entry) == 3);
    CHECK(!HsAllianceMageTourReplyAllowed({HsAllianceMageAction::Tour, 0, 3}, true, 0, true, false));
    CHECK(!HsAllianceMageTourReplyAllowed({HsAllianceMageAction::Tour, UINT32_MAX, 0}, true, 0, true, false));
    CHECK(!HsAllianceMageTourReplyAllowed({HsAllianceMageAction::Acknowledge, 0, 0}, true, 0, true, false));

    // All combinations of saved native holes keep each missing stop available.
    // A saved later visit never blocks an earlier one, unlike an ordered quiz.
    for (uint32_t mask = 0; mask < 8; ++mask)
        for (uint32_t stop = 0; stop < 3; ++stop)
        {
            auto const ack = HsAllianceMageDecode(kHsAllianceMageActionFirst + 66 + stop * 4);
            CHECK(HsAllianceMageTourReplyAllowed(ack, true, stop, true, (mask & (1u << stop)) != 0) ==
                ((mask & (1u << stop)) == 0));
        }
    std::array<uint32_t, 3> order = {0, 1, 2};
    do
    {
        std::array<uint32_t, 3> saved = {0, 0, 0};
        for (uint32_t stop : order)
        {
            auto const ack = HsAllianceMageDecode(kHsAllianceMageActionFirst + 66 + stop * 4);
            CHECK(HsAllianceMageTourReplyAllowed(ack, true, stop, true, saved[stop] != 0));
            saved[stop] = 1;
            CHECK(!HsAllianceMageTourReplyAllowed(ack, true, stop, true, saved[stop] != 0));
        }
        CHECK(saved[0] && saved[1] && saved[2]);
    } while (std::next_permutation(order.begin(), order.end()));

    // Sparse action offsets are intentional. Undefined offsets, boundary values
    // and other chains' actions must never turn into a valid story reply.
    for (uint32_t offset = 0; offset < 128; ++offset)
    {
        bool const defined = offset < 10 || (offset >= 16 && offset < 46) ||
            (offset >= 48 && offset < 58) || offset == 80 ||
            (offset >= 64 && offset < 76 && ((offset - 64) % 4) < 3);
        CHECK((HsAllianceMageDecode(kHsAllianceMageActionFirst + offset).kind != HsAllianceMageAction::Invalid) == defined);
    }
    CHECK(HsAllianceMageDecode(kHsAllianceMageActionFirst - 1).kind == HsAllianceMageAction::Invalid);
    CHECK(HsAllianceMageDecode(kHsAllianceMageActionLast + 1).kind == HsAllianceMageAction::Invalid);
    CHECK(HsAllianceMageDecode(UINT32_MAX).kind == HsAllianceMageAction::Invalid);

    auto const* teacher = Hs::ClassQuestOpponentOf(kHsMitchellJones);
    CHECK(teacher && teacher->questId == kHsAllianceMageDuel && teacher->alliance);
    for (uint32_t entry : kHsAllianceMageImages) CHECK(Hs::ClassQuestOpponentOf(entry) == nullptr);
    CHECK(Hs::ClassQuestOpponentOf(kHsAllianceMageQuizCredit) == nullptr);
    for (uint32_t entry : kHsAllianceMageTourCredits) CHECK(Hs::ClassQuestOpponentOf(entry) == nullptr);
    if (!teacher) return;
    CHECK(HsClassQuestDirectAllowed(*teacher, true, true));
    CHECK(!HsClassQuestDirectAllowed(*teacher, false, true));
    CHECK(!HsClassQuestDirectAllowed(*teacher, true, false)); // Complete/rewarded/absent or finder access alone.
    Hs::MatchEnd end;
    end.meta.opponentEntry = kHsMitchellJones;
    end.res = "win"; end.why = "hero";
    CHECK(HsClassQuestVictory(end));
    for (char const* reason : {"concede", "disconnect", "logout", "timeout", "death", "turns", "gm", "error"})
    { end.why = reason; CHECK(!HsClassQuestVictory(end)); }
    end.why = "hero";
    for (char const* result : {"loss", "draw", "int", "interrupted", "surrender"})
    { end.res = result; CHECK(!HsClassQuestVictory(end)); }
    end.res = "win";
    for (uint32_t entry : kHsAllianceMageImages)
    { end.meta.opponentEntry = entry; CHECK(!HsClassQuestVictory(end)); }
    CheckClassQuestRoster(kHsMitchellJones); // Basic Mage, Native turn-in roster boundary and Normal/Hard finder AI.
}

int main(int argc, char** argv)
{
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY); // emitted files use LF line endings
#endif
    if (argc > 1 && std::string(argv[1]) == "--emit-vectors")
    {
        std::printf("# golden vectors: <hex of raw bytes> TAB <Escape(raw)>\n");
        std::printf("# Shared by tests/protocol_tests.cpp and tests/lua_tests.lua. Regenerate with:\n");
        std::printf("#   protocol_tests.exe --emit-vectors > tests/golden_vectors.txt\n");
        for (auto const& v : VectorInputs())
            std::printf("%s\t%s\n", Hex(v).c_str(), Escape(v).c_str());
        return 0;
    }
    if (argc > 1 && std::string(argv[1]) == "--emit-transcript")
    {
        EmitTranscript();
        return g_failed == 0 ? 0 : 1;
    }
    std::string const dir = argc > 1 ? argv[1] : ".";

    RUN(TestAdlerAndPattern);
    RUN(TestEscapeRoundTripAllBytes);
    RUN(TestEscapeTriggersNeverAppear);
    RUN(TestUnescapeRejectsGarbage);
    RUN(TestValueEscapeAndKv);
    g_test = "TestGoldenVectors";
    {
        int const before = g_failed;
        TestGoldenVectors(dir);
        std::printf("%-44s %s\n", "TestGoldenVectors", g_failed == before ? "ok" : "FAILED");
    }
    RUN(TestFrameCodec);
    RUN(TestChunkingAllSizes);
    RUN(TestChunkingAllPayloadLengthsExhaustive240);
    RUN(TestReassemblerRoundTripAllPartCounts);
    RUN(TestReassemblerDuplicatePart);
    RUN(TestReassemblerConflictsAndBadNumbers);
    RUN(TestReassemblerOversize);
    RUN(TestReassemblerTooManyPending);
    RUN(TestReassemblerTimeoutSweep);
    RUN(TestReassemblerCompletedLru);
    RUN(TestRateLimiter);
    RUN(TestSanitize);
    RUN(TestSessionHandshake);
    RUN(TestSessionHelloErrors);
    RUN(TestSessionDisabled);
    RUN(TestSessionPing);
    RUN(TestSessionEchoDebugGating);
    RUN(TestSessionEchoOutboundChunks);
    RUN(TestSessionEchoInboundMultiPart);
    RUN(TestSessionTimeoutSweep);
    RUN(TestSessionRateLimit);
    RUN(TestSessionAllRepliesFitWire);
    RUN(TestCardCatalog);
    RUN(TestValidateGrant);
    RUN(TestPlanGrantBounds);
    RUN(TestPlanGrantModes);
    RUN(TestPlanRemove);
    RUN(TestCollectionModelAndPayload);
    RUN(TestSessionCollGet);
    RUN(TestSessionCollLargeCollection);
    RUN(TestCollectionService);
    RUN(TestPackPool);
    RUN(TestRollPackGuarantees);
    RUN(TestPackStatistics);
    RUN(TestPackOpeningCodec);
    RUN(TestPackService);
    RUN(TestSessionPacks);
    RUN(TestWalletRules);
    RUN(TestWalletService);
    RUN(TestSessionHeroes);
    RUN(TestScrapExtras);
    RUN(TestSessionWallet);
    RUN(TestSessionIntro);
    RUN(TestArenaWire);
    RUN(TestArenaSession);
    RUN(TestArenaWorstCase);
    RUN(TestSessionGamePause);
    RUN(TestSessionLadder);
    RUN(TestArenaIntroRules);
    RUN(TestNaxxQuestRules);
    RUN(TestDeckClasses);
    RUN(TestDeckNames);
    RUN(TestDeckKeysAndCardCodec);
    RUN(TestDeckRules);
    RUN(TestDeckStates);
    RUN(TestDeckPayloads);
    RUN(TestDeckFixtures);
    RUN(TestDeckService);
    RUN(TestStarterAwards);
    RUN(TestDeckClassAccessAndMatch);
    RUN(TestSessionDecks);
    RUN(TestDeckListedCeiling);
    RUN(TestDecksWorstCaseFits);
    RUN(TestGameCanonicalEncoding);
    RUN(TestGameResultAndPhases);
    RUN(TestGameEventWindow);
    RUN(TestDecodeGameAct);
    RUN(TestGameFlagsAndNewKeys);
    RUN(TestDecodeGameActPhase08);
    RUN(TestMatchOpponentsAndExpand);
    RUN(TestMatchServiceLifecycle);
    RUN(TestMatchServiceEndings);
    RUN(TestMatchServiceEventsMark);
    RUN(TestWarriorQuestRules);
    RUN(TestHordeMageQuestRules);
    RUN(TestHordeDruidQuestRules);
    RUN(TestHordePriestQuestRules);
    RUN(TestAlliancePriestQuestFlow);
    RUN(TestAlliancePriestClaspScene);
    RUN(TestHunterQuestRules);
    RUN(TestAllianceMageQuestRules);
    RUN(TestWarlockQuestRules);
    RUN(TestHordeRogueQuestRules);
    RUN(TestAllianceRogueQuestRules);
    RUN(TestHordeRogueQuestView);
    RUN(TestTraderRules);
    RUN(TestQuestAcquisition);
    RUN(TestHordeWarriorQuestRules);
    RUN(TestMatchServicePhase08);
    RUN(TestMatchServiceEncounterStart);
    RUN(TestGameDurableKey);
    RUN(TestGameTimerKey);
    RUN(TestMatchServiceNozdormuTimer);
    RUN(TestLibraryDeckParsing);
    RUN(TestLibraryDeckChecks);
    RUN(TestDeckLibraryBuild);
    RUN(TestOpponentRegistry);
    RUN(TestOpponentRegistryEncounters);
    RUN(TestFinderRegistry);
    RUN(TestQuestNpcs);
    RUN(TestClassQuestRoster);
    RUN(TestHordeShamanRules);
    RUN(TestHordePaladinRules);
    RUN(TestAlliancePaladinQuestFlow);
    RUN(TestHordePaladinScene);
    RUN(TestAllianceShamanProgress);
    RUN(TestAllianceShamanWalkingScenes);
    RUN(TestAllianceShamanSceneClock);
    RUN(TestHordePaladinReadings);
    g_test = "TestSeededLibraryDecks";
    {
        int const before = g_failed;
        TestSeededLibraryDecks(dir);
        std::printf("%-44s %s\n", "TestSeededLibraryDecks", g_failed == before ? "ok" : "FAILED");
    }
    RUN(TestSessionGame);
    RUN(TestDeckNotReadyText);
    RUN(TestGameWorstCaseFits);
    RUN(TestHistoryEncodeCardList);
    RUN(TestHistoryResultWords);
    RUN(TestHistoryClipText);
    RUN(TestHistoryDurationAndLine);
    RUN(TestMatchHistoryRows);
    RUN(TestMatchHistoryRowEdges);

    std::printf("\n%d checks, %d failed\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
