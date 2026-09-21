#include "pch.h"
#include "Apw.h"

#include <algorithm>
#include <cmath>
#include <cwctype>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using EuroScopePlugIn::CPosition;

namespace
{
    const double kNmPerDegLat = 60.0;

    double NmPerDegLon(double lat)
    {
        double c = cos(lat * M_PI / 180.0);
        if (c < 0.01)
            c = 0.01;
        return kNmPerDegLat * c;
    }

    struct Pt { double x, y; };

    bool PointInRing(const std::vector<Pt>& ring, const Pt& p)
    {
        if (ring.size() < 3)
            return false;

        bool inside = false;
        for (size_t i = 0, j = ring.size() - 1; i < ring.size(); j = i++)
        {
            if ((ring[i].y > p.y) != (ring[j].y > p.y))
            {
                const double x = (ring[j].x - ring[i].x) * (p.y - ring[i].y)
                    / (ring[j].y - ring[i].y) + ring[i].x;
                if (p.x < x)
                    inside = !inside;
            }
        }
        return inside;
    }

    double DistanceToRing(const std::vector<Pt>& ring, const Pt& p)
    {
        double best = 1e18;
        for (size_t i = 0, j = ring.size() - 1; i < ring.size(); j = i++)
        {
            const double vx = ring[j].x - ring[i].x, vy = ring[j].y - ring[i].y;
            const double wx = p.x - ring[i].x, wy = p.y - ring[i].y;
            const double len2 = vx * vx + vy * vy;
            double t = (len2 > 1e-12) ? (wx * vx + wy * vy) / len2 : 0.0;
            t = max(0.0, min(1.0, t));
            const double dx = wx - t * vx, dy = wy - t * vy;
            best = min(best, sqrt(dx * dx + dy * dy));
        }
        return best;
    }

    bool LevelsConflict(int altFt, int lowFt, int highFt, int bufferFt)
    {
        return (altFt + bufferFt) >= lowFt && (altFt - bufferFt) <= highFt;
    }

    bool IsIcao(const std::wstring& s)
    {
        if (s.size() != 4)
            return false;
        for (wchar_t c : s)
        {
            if (!iswalpha(c))
                return false;
        }
        return true;
    }

    bool TrackIsExempt(const ZoneExemption& e, const ApwTrack& track)
    {
        if (e.airports.empty())
            return false;

        for (const std::wstring& icao : e.airports)
        {
            if ((!track.origin.empty() && track.origin == icao) ||
                (!track.destination.empty() && track.destination == icao))
                return true;
        }
        return false;
    }

    std::wstring TrimUpper(const std::wstring& s)
    {
        size_t a = 0, b = s.size();
        while (a < b && iswspace(s[a])) a++;
        while (b > a && iswspace(s[b - 1])) b--;

        std::wstring out = s.substr(a, b - a);
        for (wchar_t& c : out)
            c = (wchar_t)towupper(c);
        return out;
    }

    bool ParseExemptionLine(const std::wstring& line, ZoneExemption& out)
    {
        std::vector<std::wstring> words;
        std::wstring word;
        const std::wstring u = TrimUpper(line) + L" ";
        for (wchar_t c : u)
        {
            if (iswspace(c) || c == L'/' || c == L',')
            {
                if (!word.empty())
                {
                    words.push_back(word);
                    word.clear();
                }
            }
            else
            {
                word += c;
            }
        }

        size_t i = 0;
        const bool except = (!words.empty() && words[0] == L"EXCEPT");
        if (except)
            i++;

        while (i < words.size() && IsIcao(words[i]))
            out.airports.push_back(words[i++]);
        if (out.airports.empty())
            return false;

        if (except)
            return i == words.size();

        std::wstring rest;
        for (; i < words.size(); i++)
            rest += words[i];
        if (rest.empty() || !iswdigit(rest[0]))
            return false;

        size_t k = 0;
        int value = 0;
        while (k < rest.size() && iswdigit(rest[k]))
            value = value * 10 + (rest[k++] - L'0');

        const std::wstring unit = rest.substr(k);
        if (unit.empty() || unit == L"FT" || unit == L"ФТ")
            out.ceilingFt = value;
        else if (unit == L"M" || unit == L"М")
            out.ceilingFt = (int)lround(value * 3.28084);
        else
            return false;

        return out.ceilingFt > 0;
    }
}

bool ParseZoneExemption(const std::wstring& note, ZoneExemption& out)
{
    out.airports.clear();
    out.ceilingFt = 0;
    if (note.empty())
        return false;

    size_t at = 0;
    while (at <= note.size())
    {
        size_t eol = note.find(L'\n', at);
        if (eol == std::wstring::npos)
            eol = note.size();

        ZoneExemption one;
        if (ParseExemptionLine(note.substr(at, eol - at), one))
        {
            out = one;
            return true;
        }
        at = eol + 1;
    }
    return false;
}

bool ZoneLevelFL(const std::wstring& text, int& fl)
{
    const std::wstring u = TrimUpper(text);
    if (u.empty())
        return false;

    if (u.compare(0, 3, L"GND") == 0 || u.compare(0, 3, L"SFC") == 0)
    {
        fl = 0;
        return true;
    }
    if (u.compare(0, 3, L"UNL") == 0)
    {
        fl = 999;
        return true;
    }

    size_t i = 0;
    if (u.compare(0, 2, L"FL") == 0)
        i = 2;
    else if (u[0] == L'F' && u.size() > 1 && iswdigit(u[1]))
        i = 1;

    while (i < u.size() && iswspace(u[i]))
        i++;
    if (i >= u.size() || !iswdigit(u[i]))
        return false;

    int value = 0;
    while (i < u.size() && iswdigit(u[i]))
        value = value * 10 + (u[i++] - L'0');

    while (i < u.size() && iswspace(u[i]))
        i++;
    const bool metres = (i < u.size() &&
        (u[i] == L'M' || u[i] == L'm' || u[i] == L'М' || u[i] == L'м'));
    fl = metres ? (int)lround(value * 3.28084 / 100.0) : value;
    return true;
}

void ApwBuildZones(const std::vector<Zone>& zones,
    const std::vector<char>& active,
    const std::vector<const ZoneBooking*>& bookings,
    const ApwSettings& cfg,
    std::vector<ApwZone>& out)
{
    out.assign(zones.size(), ApwZone());

    for (size_t i = 0; i < zones.size(); i++)
    {
        ApwZone& z = out[i];
        z.active = (i < active.size() && active[i] != 0);
        if (!z.active)
            continue;

        const Zone& zone = zones[i];
        z.warns = (zone.kind == ZoneKind::Prohibited) ? cfg.warnProhibited
            : (zone.kind == ZoneKind::Danger) ? cfg.warnDanger
            : cfg.warnRestricted;
        if (!z.warns)
            continue;

        if (zone.ring.size() < 3)
        {
            z.warns = false;
            continue;
        }

        z.minLat = z.maxLat = zone.ring[0].m_Latitude;
        z.minLon = z.maxLon = zone.ring[0].m_Longitude;
        for (const CPosition& p : zone.ring)
        {
            z.minLat = min(z.minLat, p.m_Latitude);
            z.maxLat = max(z.maxLat, p.m_Latitude);
            z.minLon = min(z.minLon, p.m_Longitude);
            z.maxLon = max(z.maxLon, p.m_Longitude);
        }

        const ZoneBooking* booking = (i < bookings.size()) ? bookings[i] : NULL;

        int lowFL = 0, highFL = 999;
        if (booking != NULL)
        {
            lowFL = booking->minFL;
            highFL = booking->maxFL;
        }
        else
        {
            if (!ZoneLevelFL(zone.lower, lowFL))
                lowFL = 0;
            if (!ZoneLevelFL(zone.upper, highFL))
                highFL = 999;
        }

        z.lowFt = lowFL * 100;
        z.highFt = (highFL >= 999) ? 99900 : highFL * 100;

        ParseZoneExemption(zone.note, z.exempt);
    }
}

ApwResult ApwCheck(const std::vector<Zone>& zones,
    const std::vector<ApwZone>& prepared,
    const ApwTrack& track,
    const ApwSettings& cfg)
{
    ApwResult best;
    if (!cfg.enabled || zones.empty())
        return best;

    const int lookAheadSec = max(0, min(15, cfg.lookAheadMin)) * 60;
    const double buffer = max(0.0, cfg.bufferNm);

    const double lat0 = track.pos.m_Latitude;
    const double lon0 = track.pos.m_Longitude;
    const double nmPerLon = NmPerDegLon(lat0);

    const double reach = (track.gsKt > 0 ? track.gsKt * (lookAheadSec / 3600.0) : 0.0) + buffer;

    const int kStepSec = 10;
    const double trackRad = track.trackDeg * M_PI / 180.0;
    const double nmPerSec = track.gsKt / 3600.0;

    std::vector<Pt> ring;

    for (size_t i = 0; i < zones.size() && i < prepared.size(); i++)
    {
        const ApwZone& z = prepared[i];
        if (!z.active || !z.warns)
            continue;

        int lowFt = z.lowFt, highFt = z.highFt;
        if (TrackIsExempt(z.exempt, track))
        {
            if (z.exempt.ceilingFt <= 0)
                continue;
            highFt = min(highFt, z.exempt.ceilingFt);
            if (highFt < lowFt)
                continue;
        }

        const double south = (z.minLat - lat0) * kNmPerDegLat;
        const double north = (z.maxLat - lat0) * kNmPerDegLat;
        const double west = (z.minLon - lon0) * nmPerLon;
        const double east = (z.maxLon - lon0) * nmPerLon;
        const double dx = max(0.0, max(west, -east));
        const double dy = max(0.0, max(south, -north));
        if (sqrt(dx * dx + dy * dy) > reach)
            continue;

        const Zone& zone = zones[i];
        ring.clear();
        ring.reserve(zone.ring.size());
        for (const CPosition& p : zone.ring)
        {
            Pt q;
            q.x = (p.m_Longitude - lon0) * nmPerLon;
            q.y = (p.m_Latitude - lat0) * kNmPerDegLat;
            ring.push_back(q);
        }

        for (int t = 0; t <= lookAheadSec; t += kStepSec)
        {
            const int altFt = track.altFt + (int)lround(track.vsFpm * (t / 60.0));
            if (!LevelsConflict(altFt, lowFt, highFt, cfg.verticalBufferFt))
                continue;

            Pt p;
            p.x = nmPerSec * t * sin(trackRad);
            p.y = nmPerSec * t * cos(trackRad);

            const bool inside = PointInRing(ring, p);
            if (!inside && (buffer <= 0.0 || DistanceToRing(ring, p) > buffer))
                continue;

            ApwResult found;
            found.level = (t == 0 && inside) ? ApwLevel::Inside : ApwLevel::Predicted;
            found.zoneId = zone.id.empty() ? zone.name : zone.id;
            found.secondsToEntry = (found.level == ApwLevel::Inside) ? 0 : t;

            if (found.level > best.level ||
                (found.level == best.level && found.secondsToEntry < best.secondsToEntry))
            {
                best = found;
            }
            break;
        }

        if (best.level == ApwLevel::Inside)
            break;
    }

    return best;
}
