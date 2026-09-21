#include "pch.h"
#include "Sigmet.h"
#include "Json.h"
#include "Net.h"

#include <algorithm>

namespace
{
    const char* kFeedUrl = "https://aviationweather.gov/api/data/isigmet?format=json";

    const size_t kMaxBytes = 4 * 1024 * 1024;
    const DWORD  kTimeoutMs = 10000;

    std::wstring ToUpper(std::wstring s)
    {
        std::transform(s.begin(), s.end(), s.begin(), ::towupper);
        return s;
    }

    void AppendRing(const Json::Value& list, Sigmet& s)
    {
        if (list.kind != Json::Value::Kind::Array)
            return;

        std::vector<EuroScopePlugIn::CPosition> ring;
        for (const Json::Value& c : list.arr)
        {
            if (c.kind != Json::Value::Kind::Object)
                continue;
            const Json::Value* lat = c.Find(L"lat");
            const Json::Value* lon = c.Find(L"lon");
            if (lat == NULL || lon == NULL)
                continue;

            const double kNotACoord = 1e9;
            double la = lat->AsNumber(kNotACoord);
            double lo = lon->AsNumber(kNotACoord);
            if (la < -90.0 || la > 90.0 || lo < -180.0 || lo > 180.0)
                continue;

            EuroScopePlugIn::CPosition p;
            p.m_Latitude = la;
            p.m_Longitude = lo;
            ring.push_back(p);
        }

        if (s.closed && ring.size() >= 2 &&
            ring.front().m_Latitude == ring.back().m_Latitude &&
            ring.front().m_Longitude == ring.back().m_Longitude)
            ring.pop_back();

        if (ring.size() < (s.closed ? 3u : 2u))
            return;

        s.rings.push_back(std::move(ring));
    }
}

std::wstring Sigmet::Key() const
{
    return firId + L"/" + seriesId + L"/" + hazard;
}

std::wstring Sigmet::Title() const
{
    std::wstring t = L"SIGMET";
    if (!seriesId.empty())
        t += L" " + seriesId;
    if (!hazard.empty())
        t += L" " + hazard;
    if (!qualifier.empty())
        t += L" " + qualifier;
    if (!firName.empty())
        t += L" - " + firName;
    else if (!firId.empty())
        t += L" - " + firId;
    return t;
}

bool FetchSigmets(const std::vector<std::wstring>& firFilter, std::vector<Sigmet>& out)
{
    out.clear();

    std::string body;
    if (!Net::HttpGet(kFeedUrl, body, kMaxBytes, kTimeoutMs))
        return false;

    if (!ParseSigmets(body, firFilter, out))
    {
        Log::Error("sigmet", std::string("feed ") + kFeedUrl + " is not a JSON array ("
            + std::to_string(body.size()) + " bytes): " + Log::Snippet(body, 120));
        return false;
    }
    return true;
}

bool ParseSigmets(const std::string& body,
    const std::vector<std::wstring>& firFilter, std::vector<Sigmet>& out)
{
    out.clear();

    Json::Value root;
    if (!Json::ParseUtf8(body, root) || root.kind != Json::Value::Kind::Array)
        return false;

    for (const Json::Value& entry : root.arr)
    {
        if (entry.kind != Json::Value::Kind::Object)
            continue;

        Sigmet s;
        if (const Json::Value* v = entry.Find(L"firId"))
            s.firId = ToUpper(v->AsString());

        if (!firFilter.empty() &&
            std::find(firFilter.begin(), firFilter.end(), s.firId) == firFilter.end())
            continue;

        if (const Json::Value* v = entry.Find(L"firName"))
            s.firName = v->AsString();
        if (const Json::Value* v = entry.Find(L"seriesId"))
            s.seriesId = v->AsString();
        if (const Json::Value* v = entry.Find(L"hazard"))
            s.hazard = ToUpper(v->AsString());
        if (const Json::Value* v = entry.Find(L"qualifier"))
            s.qualifier = v->AsString();
        if (const Json::Value* v = entry.Find(L"dir"))
            s.dir = v->AsString();
        if (const Json::Value* v = entry.Find(L"spd"))
            s.spd = v->AsString();
        if (const Json::Value* v = entry.Find(L"chng"))
            s.chng = v->AsString();
        if (const Json::Value* v = entry.Find(L"rawSigmet"))
            s.raw = v->AsString();
        if (const Json::Value* v = entry.Find(L"base"))
            s.baseFt = (int)v->AsInt(-1);
        if (const Json::Value* v = entry.Find(L"top"))
            s.topFt = (int)v->AsInt(-1);
        if (const Json::Value* v = entry.Find(L"validTimeFrom"))
            s.validFrom = v->AsInt(0);
        if (const Json::Value* v = entry.Find(L"validTimeTo"))
            s.validTo = v->AsInt(0);

        std::wstring geom = L"AREA";
        if (const Json::Value* v = entry.Find(L"geom"))
            geom = ToUpper(v->AsString(L"AREA"));
        s.closed = (geom == L"AREA" || geom == L"AREAS");

        const Json::Value* coords = entry.Find(L"coords");
        if (coords == NULL || coords->kind != Json::Value::Kind::Array)
            continue;

        if (!coords->arr.empty() && coords->arr.front().kind == Json::Value::Kind::Array)
        {
            for (const Json::Value& ring : coords->arr)
                AppendRing(ring, s);
        }
        else
        {
            AppendRing(*coords, s);
        }

        if (s.rings.empty())
            continue;

        out.push_back(std::move(s));
    }

    return true;
}
