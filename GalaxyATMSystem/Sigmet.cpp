#include "pch.h"
#include "Sigmet.h"
#include "Json.h"
#include "Net.h"

#include <algorithm>

namespace
{
    // aviationweather.gov's international SIGMET feed. It is the only public
    // source that publishes the areas as coordinates rather than as the raw
    // "WI N5230 E03000 - ..." text, which is what makes them drawable at all.
    const char* kFeedUrl = "https://aviationweather.gov/api/data/isigmet?format=json";

    // A few hundred reports of a couple of kilobytes each; the cap is there to
    // bound a runaway response, not to trim a normal one.
    const size_t kMaxBytes = 4 * 1024 * 1024;
    const DWORD  kTimeoutMs = 10000;

    std::wstring ToUpper(std::wstring s)
    {
        std::transform(s.begin(), s.end(), s.begin(), ::towupper);
        return s;
    }

    // Reads one list of {lat, lon} objects into a ring on the report, dropping
    // the ring if what comes out is too small to be a shape.
    //
    // A coordinate is checked rather than trusted: the feed does publish null
    // inside an otherwise good list, and a null read as zero would put a
    // vertex in the Gulf of Guinea and drag the whole area over the map.
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

        // The feed closes its rings by repeating the first point. Drawing it
        // again over the closing segment costs nothing but leaves a doubled
        // vertex in the outline hit-boxes, so it is dropped here.
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

    return ParseSigmets(body, firFilter, out);
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

        // The filter is on the FIR the report belongs to, which is what a
        // controller thinks in - not on the issuing station.
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
        // Both are published as null as often as not, and null is not the
        // surface - it is a report that gave no level, which reads as "—".
        if (const Json::Value* v = entry.Find(L"base"))
            s.baseFt = (int)v->AsInt(-1);
        if (const Json::Value* v = entry.Find(L"top"))
            s.topFt = (int)v->AsInt(-1);
        if (const Json::Value* v = entry.Find(L"validTimeFrom"))
            s.validFrom = v->AsInt(0);
        if (const Json::Value* v = entry.Find(L"validTimeTo"))
            s.validTo = v->AsInt(0);

        // "AREA" and "AREAS" are closed rings - one, and several. Anything
        // else the feed publishes with a geometry (a line of weather, say) is
        // drawn open, as it is meant to be.
        std::wstring geom = L"AREA";
        if (const Json::Value* v = entry.Find(L"geom"))
            geom = ToUpper(v->AsString(L"AREA"));
        s.closed = (geom == L"AREA" || geom == L"AREAS");

        const Json::Value* coords = entry.Find(L"coords");
        if (coords == NULL || coords->kind != Json::Value::Kind::Array)
            continue;

        // One report, one list of points - except for "AREAS", where the list
        // holds a list per area. Both shapes are read the same way by looking
        // at what the first element actually is.
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
            continue;   // nothing to draw and nothing to click on

        out.push_back(std::move(s));
    }

    return true;
}
