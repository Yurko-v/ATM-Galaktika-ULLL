#include "pch.h"
#include "Stca.h"

#include <algorithm>
#include <cmath>

namespace Stca
{
    namespace
    {
        const double kPi = 3.14159265358979323846;
        const double kRad = kPi / 180.0;
        const double kEarthNm = 3440.065;

        // A turn rate below this is radar noise on a straight track; above the upper
        // one it is a jump in the data, as no airliner turns that hard.
        const double kTurnNoiseDegSec = 0.5;
        const double kTurnMaxDegSec = 4.0;

        // Positions closer together than this cannot give a reliable direction.
        const double kMinSegmentNm = 0.05;

        // How far back the history is read, and the shortest gap that makes a segment.
        const double kHistorySec = 30.0;
        const double kSegmentSec = 4.0;

        double Wrap360(double deg)
        {
            const double v = fmod(deg, 360.0);
            return v < 0.0 ? v + 360.0 : v;
        }

        double Wrap180(double deg)
        {
            const double v = Wrap360(deg);
            return v > 180.0 ? v - 360.0 : v;
        }

        struct Sample
        {
            double x = 0.0, y = 0.0;    // NM east and north of the position the state starts from
            double alt = 0.0;
            double hdg = 0.0;           // true track at this second
        };

        // The path one aircraft is expected to fly, one sample a second from now.
        struct Path
        {
            const TrackState* track = NULL;
            std::vector<Sample> at;
            double minAlt = 0.0, maxAlt = 0.0;
        };

        double TurnSeconds(const TrackState& t, const Settings& s)
        {
            if (!s.turns || t.turnDegSec == 0.0)
                return 0.0;
            const double rate = fabs(t.turnDegSec);

            // Turning onto an assigned heading: the turn ends there. More than half a
            // circle to go means it is turning away from it - wind drift or noise on a
            // track already near the heading - and is not followed round the circle.
            if (t.assignedHeadingDeg >= 0.0)
            {
                const double togo = t.turnDegSec > 0.0
                    ? Wrap360(t.assignedHeadingDeg - t.trackDeg)
                    : Wrap360(t.trackDeg - t.assignedHeadingDeg);
                if (togo <= 180.0)
                    return togo / rate;
            }

            // Otherwise nobody knows where it stops; follow it for a while, then straight.
            return (std::min)(s.maxTurnSec, s.maxTurnDeg / rate);
        }

        Path Fly(const TrackState& t, const Settings& s)
        {
            Path path;
            path.track = &t;

            const int lead = (std::max)(0, (int)lround(t.ageSec));
            const int steps = lead + s.lookaheadSec;
            const double nmPerSec = t.gsKt / 3600.0;
            double turnLeft = TurnSeconds(t, s);

            const bool level = fabs(t.vsFpm) < s.levelVsFpm;
            const double ftPerSec = level ? 0.0 : t.vsFpm / 60.0;
            // The cleared level caps the climb or descent only when it lies ahead.
            const bool capUp = !level && t.vsFpm > 0.0 && t.cflFt > t.altFt;
            const bool capDown = !level && t.vsFpm < 0.0 && t.cflFt >= 0.0 && t.cflFt < t.altFt;

            Sample now;
            now.alt = t.altFt;
            double heading = t.trackDeg;

            path.at.reserve(s.lookaheadSec + 1);
            for (int step = 0; step <= steps; step++)
            {
                now.hdg = heading;
                if (step >= lead)
                    path.at.push_back(now);

                double turnStep = 0.0;
                if (turnLeft > 0.0)
                {
                    const double dt = (std::min)(1.0, turnLeft);
                    turnStep = t.turnDegSec * dt;
                    turnLeft -= dt;
                }
                const double mid = (heading + turnStep / 2.0) * kRad;
                now.x += nmPerSec * sin(mid);
                now.y += nmPerSec * cos(mid);
                heading += turnStep;

                now.alt += ftPerSec;
                if (capUp && now.alt > t.cflFt)
                    now.alt = t.cflFt;
                if (capDown && now.alt < t.cflFt)
                    now.alt = t.cflFt;
                if (now.alt < 0.0)
                    now.alt = 0.0;
            }

            path.minAlt = path.maxAlt = path.at.front().alt;
            for (const Sample& p : path.at)
            {
                path.minAlt = (std::min)(path.minAlt, p.alt);
                path.maxAlt = (std::max)(path.maxAlt, p.alt);
            }
            return path;
        }

        void ToLatLon(const TrackState& t, const Sample& p, double& lat, double& lon)
        {
            lat = t.lat + p.y / 60.0;
            lon = t.lon + p.x / (60.0 * cos(t.lat * kRad));
        }

        bool InsideRing(const std::vector<LatLon>& ring, double lat, double lon)
        {
            bool inside = false;
            for (size_t i = 0, j = ring.size() - 1; i < ring.size(); j = i++)
            {
                const LatLon& a = ring[i];
                const LatLon& b = ring[j];
                if ((a.lat > lat) != (b.lat > lat)
                    && lon < (b.lon - a.lon) * (lat - a.lat) / (b.lat - a.lat) + a.lon)
                    inside = !inside;
            }
            return inside;
        }

        // The smallest zone around a point, or NULL outside them all.
        const Zone* ZoneAt(double lat, double lon, double alt, const Settings& s)
        {
            const Zone* best = NULL;
            for (const Zone& zone : s.zones)
            {
                if (alt < zone.floorFt || alt >= zone.ceilingFt
                    || lat < zone.minLat || lat > zone.maxLat || lon < zone.minLon || lon > zone.maxLon)
                    continue;
                if ((best == NULL || zone.area < best->area) && InsideRing(zone.ring, lat, lon))
                    best = &zone;
            }
            return best;
        }

        // Whether the follower flies in the leader's wake: in the half behind it, not
        // above it, and not heading the other way.
        bool Behind(const Sample& leader, double dx, double dy, const Sample& follower, const Settings& s)
        {
            if (follower.alt > leader.alt + s.verticalToleranceFt)
                return false;
            if (fabs(Wrap180(follower.hdg - leader.hdg)) > 90.0)
                return false;
            const double h = leader.hdg * kRad;
            return dx * sin(h) + dy * cos(h) < 0.0;
        }

        double WakeMinimum(const Zone* zone, char leaderWtc)
        {
            if (zone == NULL)
                return 0.0;
            if (leaderWtc == 'J')
                return zone->behindSuperNm;
            if (leaderWtc == 'H')
                return zone->behindHeavyNm;
            return 0.0;
        }

        // The lateral minimum for two aircraft: the larger of the minima of the zones
        // they are in, the en-route one outside the zones, and a wake minimum when one
        // follows a heavy or a super. dx, dy is where b is from a, in NM.
        double LateralMinimum(const TrackState& ta, const Sample& sa, double latA, double lonA,
            const TrackState& tb, const Sample& sb, double latB, double lonB,
            double dx, double dy, const Settings& s)
        {
            const Zone* za = ZoneAt(latA, lonA, sa.alt, s);
            const Zone* zb = ZoneAt(latB, lonB, sb.alt, s);
            double minimum = (std::max)(za != NULL ? za->lateralNm : s.lateralNm,
                zb != NULL ? zb->lateralNm : s.lateralNm);
            if (Behind(sa, dx, dy, sb, s))
                minimum = (std::max)(minimum, (std::max)(WakeMinimum(za, ta.wtc), WakeMinimum(zb, ta.wtc)));
            if (Behind(sb, -dx, -dy, sa, s))
                minimum = (std::max)(minimum, (std::max)(WakeMinimum(za, tb.wtc), WakeMinimum(zb, tb.wtc)));
            return minimum;
        }

        // A straight line fit of altitude against age; the slope is the vertical rate.
        bool FitVerticalRate(const std::vector<HistoryPoint>& points, double& fpm)
        {
            if (points.size() < 2 || points.back().ageSec - points.front().ageSec < 3.0)
                return false;
            double n = 0, st = 0, sa = 0, stt = 0, sta = 0;
            for (const HistoryPoint& p : points)
            {
                const double t = -p.ageSec;     // older points are further in the past
                n += 1;
                st += t;
                sa += p.altFt;
                stt += t * t;
                sta += t * p.altFt;
            }
            const double den = n * stt - st * st;
            if (fabs(den) < 1e-9)
                return false;
            fpm = (n * sta - st * sa) / den * 60.0;
            return true;
        }
    }

    void Bound(Zone& zone)
    {
        zone.minLat = zone.minLon = 1e9;
        zone.maxLat = zone.maxLon = -1e9;
        double twice = 0.0;
        for (size_t i = 0, j = zone.ring.size() - 1; i < zone.ring.size(); j = i++)
        {
            const LatLon& a = zone.ring[i];
            const LatLon& b = zone.ring[j];
            zone.minLat = (std::min)(zone.minLat, a.lat);
            zone.maxLat = (std::max)(zone.maxLat, a.lat);
            zone.minLon = (std::min)(zone.minLon, a.lon);
            zone.maxLon = (std::max)(zone.maxLon, a.lon);
            twice += (b.lon - a.lon) * cos((a.lat + b.lat) / 2.0 * kRad) * (a.lat + b.lat);
        }
        zone.area = fabs(twice) / 2.0;
    }

    double TrueBearing(double lat1, double lon1, double lat2, double lon2)
    {
        const double p1 = lat1 * kRad, p2 = lat2 * kRad;
        const double dl = (lon2 - lon1) * kRad;
        const double y = sin(dl) * cos(p2);
        const double x = cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl);
        return Wrap360(atan2(y, x) / kRad);
    }

    double DistanceNm(double lat1, double lon1, double lat2, double lon2)
    {
        const double p1 = lat1 * kRad, p2 = lat2 * kRad;
        const double dp = p2 - p1, dl = (lon2 - lon1) * kRad;
        const double h = sin(dp / 2) * sin(dp / 2) + cos(p1) * cos(p2) * sin(dl / 2) * sin(dl / 2);
        return 2.0 * kEarthNm * asin((std::min)(1.0, sqrt(h)));
    }

    TrackState Estimate(const TrackInput& input, const Settings& settings)
    {
        TrackState out;
        out.callsign = input.callsign;
        out.cflFt = input.cflFt;
        out.assignedHeadingDeg = input.assignedHeadingDeg;
        out.wtc = input.wtc;
        if (input.history.empty() || !input.modeC)
            return out;

        // Distinct positions only, newest first, from the last half minute. EuroScope
        // repeats a position until the next update arrives.
        std::vector<HistoryPoint> points;
        for (const HistoryPoint& p : input.history)
        {
            if (p.ageSec - input.history.front().ageSec > kHistorySec)
                break;
            if (!points.empty() && p.lat == points.back().lat && p.lon == points.back().lon)
                continue;
            if (!points.empty() && p.ageSec <= points.back().ageSec)
                continue;
            points.push_back(p);
        }

        const HistoryPoint& newest = points.front();
        out.lat = newest.lat;
        out.lon = newest.lon;
        out.altFt = newest.altFt;
        out.ageSec = newest.ageSec;
        out.gsKt = input.gsKt;
        out.trackDeg = input.fallbackTrackDeg;
        out.vsFpm = input.fallbackVsFpm;

        // The newest segment gives the current direction; the one before it, the turn.
        size_t a = 0;
        while (a + 1 < points.size() && points[a].ageSec - newest.ageSec < kSegmentSec)
            a++;
        const bool haveSegment = a > 0
            && points[a].ageSec - newest.ageSec >= kSegmentSec / 2.0
            && DistanceNm(points[a].lat, points[a].lon, newest.lat, newest.lon) >= kMinSegmentNm;

        if (haveSegment)
        {
            const double track1 = TrueBearing(points[a].lat, points[a].lon, newest.lat, newest.lon);
            const double mid1 = (points[a].ageSec + newest.ageSec) / 2.0;
            const double span = points[a].ageSec - newest.ageSec;
            out.trackDeg = track1;
            if (out.gsKt <= 0.0)
                out.gsKt = DistanceNm(points[a].lat, points[a].lon, newest.lat, newest.lon) / span * 3600.0;

            size_t b = a;
            while (b + 1 < points.size() && points[b].ageSec - points[a].ageSec < kSegmentSec)
                b++;
            if (b > a && points[b].ageSec - points[a].ageSec >= kSegmentSec / 2.0
                && DistanceNm(points[b].lat, points[b].lon, points[a].lat, points[a].lon) >= kMinSegmentNm)
            {
                const double track2 = TrueBearing(points[b].lat, points[b].lon, points[a].lat, points[a].lon);
                const double mid2 = (points[b].ageSec + points[a].ageSec) / 2.0;
                double rate = Wrap180(track1 - track2) / (mid2 - mid1);
                if (fabs(rate) < kTurnNoiseDegSec || fabs(rate) > kTurnMaxDegSec)
                    rate = 0.0;
                out.turnDegSec = rate;
                // track1 is the direction half way along the newest segment; carry the
                // turn on to the newest position itself.
                out.trackDeg = Wrap360(track1 + rate * (mid1 - newest.ageSec));
            }
        }

        double fpm = 0.0;
        if (FitVerticalRate(points, fpm))
            out.vsFpm = fpm;

        out.valid = out.trackDeg >= 0.0 && out.gsKt > 0.0;
        return out;
    }

    std::vector<Conflict> Detect(const std::vector<TrackState>& tracks, const Settings& s)
    {
        std::vector<Path> paths;
        paths.reserve(tracks.size());
        for (const TrackState& t : tracks)
        {
            if (!t.valid || t.gsKt < s.minGsKt)
                continue;
            paths.push_back(Fly(t, s));
            if (paths.back().maxAlt < s.minAltitudeFt)
                paths.pop_back();
        }

        double widestLateral = s.lateralNm;
        for (const Zone& zone : s.zones)
            widestLateral = (std::max)({ widestLateral, zone.lateralNm, zone.behindHeavyNm, zone.behindSuperNm });
        const double widestVertical = (std::max)(s.verticalFt, s.verticalHighFt);

        std::vector<Conflict> out;
        for (size_t i = 0; i < paths.size(); i++)
        {
            const Path& pa = paths[i];
            const TrackState& ta = *pa.track;
            for (size_t j = i + 1; j < paths.size(); j++)
            {
                const Path& pb = paths[j];
                const TrackState& tb = *pb.track;

                // Cheap rejections first: too far apart to meet in the time, or never
                // within the vertical minimum of each other.
                const double reach = widestLateral + (ta.gsKt + tb.gsKt) * s.lookaheadSec / 3600.0;
                if (fabs(ta.lat - tb.lat) * 60.0 > reach)
                    continue;
                if (pa.minAlt - pb.maxAlt >= widestVertical || pb.minAlt - pa.maxAlt >= widestVertical)
                    continue;
                if (DistanceNm(ta.lat, ta.lon, tb.lat, tb.lon) > reach)
                    continue;

                // Both paths in one flat frame around the pair.
                const double midLat = (ta.lat + tb.lat) / 2.0 * kRad;
                const double east0 = Wrap180(tb.lon - ta.lon) * 60.0 * cos(midLat);
                const double north0 = (tb.lat - ta.lat) * 60.0;

                int firstLoss = -1, cpa = -1;
                double cpaNm = 0.0, cpaLat = 0.0, cpaVert = 0.0;
                bool lossNow = false;
                const size_t count = (std::min)(pa.at.size(), pb.at.size());
                for (size_t k = 0; k < count; k++)
                {
                    const Sample& sa = pa.at[k];
                    const Sample& sb = pb.at[k];
                    if (sa.alt < s.minAltitudeFt || sb.alt < s.minAltitudeFt)
                        continue;

                    const double vertical = fabs(sa.alt - sb.alt);
                    const double verticalMin = (sa.alt > s.highFromFt || sb.alt > s.highFromFt)
                        ? s.verticalHighFt : s.verticalFt;
                    if (vertical >= verticalMin - s.verticalToleranceFt)
                        continue;

                    const double dx = east0 + sb.x - sa.x, dy = north0 + sb.y - sa.y;
                    const double horizontal = sqrt(dx * dx + dy * dy);
                    if (horizontal >= widestLateral)
                        continue;

                    double latA, lonA, latB, lonB;
                    ToLatLon(ta, sa, latA, lonA);
                    ToLatLon(tb, sb, latB, lonB);
                    const double lateralMin = LateralMinimum(ta, sa, latA, lonA, tb, sb, latB, lonB, dx, dy, s);
                    if (horizontal >= lateralMin)
                        continue;

                    if (firstLoss < 0)
                        firstLoss = (int)k;
                    if (k == 0)
                        lossNow = true;
                    if (cpa < 0 || horizontal < cpaNm)
                    {
                        cpa = (int)k;
                        cpaNm = horizontal;
                        cpaLat = lateralMin;
                        cpaVert = vertical;
                    }
                }
                if (firstLoss < 0)
                    continue;

                Conflict c;
                c.a = ta.callsign;
                c.b = tb.callsign;
                c.lossNow = lossNow;
                c.timeToLossSec = firstLoss;
                c.timeToCpaSec = cpa;
                c.cpaNm = cpaNm;
                c.cpaVerticalFt = cpaVert;
                c.lateralMinNm = cpaLat;
                const Sample& ca = pa.at[cpa];
                const Sample& cb = pb.at[cpa];
                c.verticalMinFt = (ca.alt > s.highFromFt || cb.alt > s.highFromFt) ? s.verticalHighFt : s.verticalFt;
                ToLatLon(ta, ca, c.aLat, c.aLon);
                ToLatLon(tb, cb, c.bLat, c.bLon);
                c.aAltFt = ca.alt;
                c.bAltFt = cb.alt;
                const double nx = east0 + pb.at[0].x - pa.at[0].x, ny = north0 + pb.at[0].y - pa.at[0].y;
                c.nowNm = sqrt(nx * nx + ny * ny);
                c.nowVerticalFt = fabs(pa.at[0].alt - pb.at[0].alt);
                out.push_back(c);
            }
        }
        return out;
    }
}
