#include "pch.h"
#include "Atis.h"
#include "Json.h"
#include "Net.h"

#include <algorithm>
#include <vector>

namespace
{
    // VATSIM's own status feed. Its "atis" array is the only published source
    // for the letter and the text together - the METAR endpoint carries the
    // weather but knows nothing about which index is on the air.
    const char* kFeedUrl = "https://data.vatsim.net/v3/vatsim-data.json";

    // The whole feed is a couple of megabytes of traffic, and only the "atis"
    // array of it is wanted; the cap is there to bound a runaway response, not
    // to trim a normal one. Net::HttpGet stops reading at the cap and hands
    // back what it has, so a cap set near the real size would quietly deliver
    // a truncated body that then fails to parse.
    const size_t kMaxBytes = 16 * 1024 * 1024;
    const DWORD  kTimeoutMs = 15000;

    std::wstring ToUpper(std::wstring s)
    {
        std::transform(s.begin(), s.end(), s.begin(), ::towupper);
        return s;
    }

    bool EndsWith(const std::wstring& s, const std::wstring& tail)
    {
        return s.size() >= tail.size()
            && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
    }

    // The index the report itself announces - "INFORMATION E" or "INFORMATION
    // ECHO" - or empty when it names none. The feed's own "atis_code" is set
    // apart from the text and has been seen a report behind it, so the
    // window read "D" over a text that said "E"; the text is what the pilots
    // hear, and it wins.
    std::wstring LetterFromText(const std::wstring& text)
    {
        static const wchar_t* kPhonetic[] = {
            L"ALFA", L"BRAVO", L"CHARLIE", L"DELTA", L"ECHO", L"FOXTROT", L"GOLF",
            L"HOTEL", L"INDIA", L"JULIETT", L"KILO", L"LIMA", L"MIKE", L"NOVEMBER",
            L"OSCAR", L"PAPA", L"QUEBEC", L"ROMEO", L"SIERRA", L"TANGO", L"UNIFORM",
            L"VICTOR", L"WHISKEY", L"XRAY", L"YANKEE", L"ZULU",
        };

        std::vector<std::wstring> words(1);
        for (wchar_t c : ToUpper(text))
        {
            if (iswalnum(c))
                words.back() += c;
            else if (!words.back().empty())
                words.emplace_back();
        }

        for (size_t i = 0; i + 1 < words.size(); i++)
        {
            if (words[i] != L"INFORMATION")
                continue;
            const std::wstring& w = words[i + 1];
            if (w.size() == 1 && w[0] >= L'A' && w[0] <= L'Z')
                return w;
            for (const wchar_t* p : kPhonetic)
                if (w == p || (w == L"ALPHA" && p[0] == L'A') || (w == L"JULIET" && p[0] == L'J')
                    || (w == L"WHISKY" && p[0] == L'W'))
                    return std::wstring(1, p[0]);
        }
        return std::wstring();
    }

    // "ULLI_ATIS" is the station; "ULLI_A_ATIS" / "ULLI_D_ATIS" are the split
    // arrival and departure ones some aerodromes run instead. Rank 0 is the
    // plain station and wins outright, 1 is a split one, -1 is somebody else's.
    int StationRank(const std::wstring& callsign, const std::wstring& icao)
    {
        std::wstring cs = ToUpper(callsign);
        if (cs == icao + L"_ATIS")
            return 0;
        if (cs.size() > icao.size() + 1
            && cs.compare(0, icao.size() + 1, icao + L"_") == 0
            && EndsWith(cs, L"_ATIS"))
            return 1;
        return -1;
    }
}

bool ParseVatsimAtis(const std::string& body, const std::string& icao, AtisReport& out)
{
    Json::Value root;
    if (!Json::ParseUtf8(body, root) || root.kind != Json::Value::Kind::Object)
        return false;

    const Json::Value* list = root.Find(L"atis");
    if (list == NULL || list->kind != Json::Value::Kind::Array)
        return false;

    const std::wstring station = ToUpper(Json::Utf8ToWide(icao));

    int bestRank = -1;
    AtisReport best;

    for (const Json::Value& entry : list->arr)
    {
        if (entry.kind != Json::Value::Kind::Object)
            continue;

        const Json::Value* callsign = entry.Find(L"callsign");
        if (callsign == NULL)
            continue;

        int rank = StationRank(callsign->AsString(), station);
        if (rank < 0 || (bestRank >= 0 && rank >= bestRank))
            continue;

        AtisReport report;
        if (const Json::Value* code = entry.Find(L"atis_code"))
            report.letter = code->AsString();

        // The feed breaks the report into the lines the station transmits;
        // they are kept as they came rather than run together, so the window
        // shows the broadcast the way it is written.
        if (const Json::Value* text = entry.Find(L"text_atis"))
        {
            if (text->kind == Json::Value::Kind::Array)
            {
                for (const Json::Value& line : text->arr)
                {
                    if (!report.text.empty())
                        report.text += L"\n";
                    report.text += line.AsString();
                }
            }
            else
            {
                report.text = text->AsString();
            }
        }

        const std::wstring spoken = LetterFromText(report.text);
        if (!spoken.empty() && spoken != report.letter)
        {
            Log::Info("atis", "feed's atis_code \"" + Log::Utf8(report.letter) + "\" differs from the text's \""
                + Log::Utf8(spoken) + "\" - the text's is shown");
            report.letter = spoken;
        }

        if (report.Empty())
            continue;   // a station logged on with nothing on the air yet

        best = report;
        bestRank = rank;
        if (rank == 0)
            break;      // the plain station: nothing can beat it
    }

    if (bestRank < 0)
        return false;

    out = best;
    return true;
}

bool FetchVatsimAtis(const std::string& icao, AtisReport& out)
{
    if (icao.size() != 4)
        return false;

    std::string body;
    if (!Net::HttpGet(kFeedUrl, body, kMaxBytes, kTimeoutMs))
        return false;

    if (ParseVatsimAtis(body, icao, out))
        return true;

    // Either the feed could not be read as one, or there is simply no ATIS on
    // the air for the aerodrome - only the first is an error.
    Json::Value root;
    if (!Json::ParseUtf8(body, root) || root.kind != Json::Value::Kind::Object || root.Find(L"atis") == NULL)
        Log::Error("atis", std::string("feed ") + kFeedUrl + " has no \"atis\" list ("
            + std::to_string(body.size()) + " bytes): " + Log::Snippet(body, 120));
    else
        Log::Info("atis", "no ATIS on the air for " + icao + " - the config's letter stands");
    return false;
}
