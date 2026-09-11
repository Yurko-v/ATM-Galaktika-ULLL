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
    // A degree of latitude is sixty miles wherever you are; a degree of
    // longitude is sixty miles times the cosine of the latitude. That is the
    // whole of the projection this needs: an area is at most a few dozen miles
    // across and the alert is worked in a flat frame centred on the aircraft,
    // where the error over that distance is far below the mile of buffer the
    // config puts round the outline anyway.
    const double kNmPerDegLat = 60.0;

    double NmPerDegLon(double lat)
    {
        double c = cos(lat * M_PI / 180.0);
        if (c < 0.01)
            c = 0.01;     // the poles, where nothing this plug-in works with flies
        return kNmPerDegLat * c;
    }

    struct Pt { double x, y; };   // nautical miles, east and north of the aircraft

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

    // The band the aircraft occupies at that moment, widened by the vertical
    // buffer, against the band the area occupies.
    bool LevelsConflict(int altFt, int lowFt, int highFt, int bufferFt)
    {
        return (altFt + bufferFt) >= lowFt && (altFt - bufferFt) <= highFt;
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

    // "FL095", "FL 95", "F095" - the prefix is skipped and what follows read
    // as the level itself.
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

    // A bare number is a level. A number written in metres - which is how the
    // Russian AIP publishes the low ones - is converted to one.
    while (i < u.size() && iswspace(u[i]))
        i++;
    // Both alphabets and both cases: the Cyrillic М of "500 м" is not folded
    // by towupper outside a Russian locale, so it is matched as it is written.
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

        // The booked band beats the published one: the plan takes a slice of
        // the area for the day, and the rest of it is not airspace anyone has
        // to be warned about.
        const ZoneBooking* booking = (i < bookings.size()) ? bookings[i] : NULL;

        int lowFL = 0, highFL = 999;
        if (booking != NULL)
        {
            lowFL = booking->minFL;
            highFL = booking->maxFL;
        }
        else
        {
            // An area with no floor published starts at the ground, and one
            // with no ceiling published has none: both are the safe reading -
            // the alert is raised, and the controller reads the real limits
            // off the area itself.
            if (!ZoneLevelFL(zone.lower, lowFL))
                lowFL = 0;
            if (!ZoneLevelFL(zone.upper, highFL))
                highFL = 999;
        }

        z.lowFt = lowFL * 100;
        z.highFt = (highFL >= 999) ? 99900 : highFL * 100;
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

    // The frame everything below is worked in: miles east and north of where
    // the aircraft is now.
    const double lat0 = track.pos.m_Latitude;
    const double lon0 = track.pos.m_Longitude;
    const double nmPerLon = NmPerDegLon(lat0);

    // How far it can get in the look-ahead, plus the buffer. Nothing outside
    // that circle can be reached and so nothing outside it is examined.
    const double reach = (track.gsKt > 0 ? track.gsKt * (lookAheadSec / 3600.0) : 0.0) + buffer;

    // Where the track goes, in that frame. A sample every ten seconds: an
    // area is miles across and a jet covers under a mile and a half in ten
    // seconds, so nothing can be crossed between two samples.
    const int kStepSec = 10;
    const double trackRad = track.trackDeg * M_PI / 180.0;
    const double nmPerSec = track.gsKt / 3600.0;

    std::vector<Pt> ring;

    for (size_t i = 0; i < zones.size() && i < prepared.size(); i++)
    {
        const ApwZone& z = prepared[i];
        if (!z.active || !z.warns)
            continue;

        // The box first, in miles: an aircraft over Пулково must not be made
        // to walk the outlines of three hundred areas across the whole FIR.
        const double south = (z.minLat - lat0) * kNmPerDegLat;
        const double north = (z.maxLat - lat0) * kNmPerDegLat;
        const double west = (z.minLon - lon0) * nmPerLon;
        const double east = (z.maxLon - lon0) * nmPerLon;
        const double dx = max(0.0, max(west, -east));      // 0 when the box spans x=0
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
            // The level it will be at then, climbing or descending at what it
            // is doing now. An aircraft levelling off short of the area's
            // floor is not warned about, and one climbing into it is.
            const int altFt = track.altFt + (int)lround(track.vsFpm * (t / 60.0));
            if (!LevelsConflict(altFt, z.lowFt, z.highFt, cfg.verticalBufferFt))
                continue;

            Pt p;
            p.x = nmPerSec * t * sin(trackRad);
            p.y = nmPerSec * t * cos(trackRad);

            const bool inside = PointInRing(ring, p);
            if (!inside && (buffer <= 0.0 || DistanceToRing(ring, p) > buffer))
                continue;

            // Inside at the first sample is inside now, which is the severe
            // one; anything later is the predicted one.
            ApwResult found;
            found.level = (t == 0 && inside) ? ApwLevel::Inside : ApwLevel::Predicted;
            found.zoneId = zone.id.empty() ? zone.name : zone.id;
            found.secondsToEntry = (found.level == ApwLevel::Inside) ? 0 : t;

            // The worst one on the screen wins, and between two of the same
            // kind the one that happens first.
            if (found.level > best.level ||
                (found.level == best.level && found.secondsToEntry < best.secondsToEntry))
            {
                best = found;
            }
            break;   // this area has answered; the next sample of it adds nothing
        }

        // Nothing left to look for once it is already in one.
        if (best.level == ApwLevel::Inside)
            break;
    }

    return best;
}
