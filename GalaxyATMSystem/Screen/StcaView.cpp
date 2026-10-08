#include "pch.h"
#include "Screen/ScreenCommon.h"

using namespace Galaxy;

namespace
{
    const ULONGLONG kStcaRecheckMs = 1000;

    // A predicted pair has to be found this many cycles running before it shows, and
    // is kept this many cycles after it stops being found.
    const int kRaiseHits = 2;
    const int kDropMisses = 3;

    const int kHistoryPoints = 8;

    std::string PairKey(const std::string& a, const std::string& b)
    {
        return a < b ? a + "|" + b : b + "|" + a;
    }

    double Wrap180(double deg)
    {
        double v = fmod(deg, 360.0);
        if (v < 0.0)
            v += 360.0;
        return v > 180.0 ? v - 360.0 : v;
    }

    std::wstring Distance(double nm, DistUnit unit)
    {
        wchar_t buf[32];
        if (unit == DistUnit::Km)
            swprintf_s(buf, L"%.1f %s", nm * 1.852, Tr(L"км"));
        else
            swprintf_s(buf, L"%.1f NM", nm);
        return buf;
    }

    std::wstring Vertical(double ft, AltUnit unit)
    {
        wchar_t buf[32];
        if (unit == AltUnit::M)
            swprintf_s(buf, L"%d %s", (int)lround(ft * 0.3048 / 10.0) * 10, Tr(L"м"));
        else
            swprintf_s(buf, L"%d ft", (int)lround(ft / 10.0) * 10);
        return buf;
    }
}

std::vector<Stca::Area> CGalaxyATMSystemRadarScreen::StcaAreas()
{
    std::vector<Stca::Area> out;
    for (const StcaAreaConfig& cfg : Plugin()->GetConfig().StcaAreas())
    {
        CPosition centre;
        bool found = false;
        if (cfg.hasPoint)
        {
            centre = cfg.point;
            found = true;
        }
        else
        {
            auto cached = m_stcaAreaCentres.find(cfg.id);
            if (cached != m_stcaAreaCentres.end())
            {
                centre = cached->second;
                found = true;
            }
            else
            {
                const std::string icao = Narrow(cfg.id);
                for (CSectorElement airport = GetPlugIn()->SectorFileElementSelectFirst(SECTOR_ELEMENT_AIRPORT);
                     airport.IsValid();
                     airport = GetPlugIn()->SectorFileElementSelectNext(airport, SECTOR_ELEMENT_AIRPORT))
                {
                    const char* name = airport.GetName();
                    if (name != NULL && _stricmp(name, icao.c_str()) == 0 && airport.GetPosition(&centre, 0))
                    {
                        found = true;
                        break;
                    }
                }
                // Not cached when missing: the sector file may not be loaded yet.
                if (found)
                    m_stcaAreaCentres[cfg.id] = centre;
                else if (m_stcaWarnedAreas.insert(cfg.id).second)
                    Log::Warn("stca", "aerodrome area " + icao + " has no Point and no airport of that name"
                        " in the sector file - the en-route minimum applies there");
            }
        }
        if (!found)
            continue;

        Stca::Area area;
        area.name = Narrow(cfg.id);
        area.lat = centre.m_Latitude;
        area.lon = centre.m_Longitude;
        area.radiusNm = cfg.radiusNm;
        area.ceilingFt = cfg.ceilingFt;
        area.lateralNm = cfg.lateralNm;
        out.push_back(area);
    }
    return out;
}

void CGalaxyATMSystemRadarScreen::RunStca()
{
    const Config& config = Plugin()->GetConfig();
    if (!m_stcaOn || !config.StcaEnabled())
    {
        m_stcaWatch.clear();
        m_kfConflicts.clear();
        m_ssaViolations.clear();
        return;
    }

    LARGE_INTEGER from, to, freq;
    QueryPerformanceCounter(&from);

    Stca::Settings settings = config.StcaSettings();
    settings.areas = StcaAreas();
    const int transitionFt = Plugin()->TransitionLevelFL() * 100;

    std::vector<Stca::TrackState> states;
    bool varianceKnown = false;
    double offsetSum = 0.0;
    int offsetCount = 0;

    for (CRadarTarget rt = GetPlugIn()->RadarTargetSelectFirst(); rt.IsValid();
         rt = GetPlugIn()->RadarTargetSelectNext(rt))
    {
        CRadarTargetPositionData pos = rt.GetPosition();
        if (!pos.IsValid())
            continue;

        // EuroScope's DirectionTo is magnetic with the sector file deviation; the
        // difference from a true bearing over the same two points is that deviation.
        // Assigned headings are magnetic too, so this turns them into true ones.
        if (!varianceKnown)
        {
            CPosition here = pos.GetPosition(), north = here;
            north.m_Latitude += 10.0 / 60.0;
            m_stcaVariation = Wrap180(Stca::TrueBearing(here.m_Latitude, here.m_Longitude,
                north.m_Latitude, north.m_Longitude) - here.DirectionTo(north));
            varianceKnown = true;
        }

        Stca::TrackInput in;
        in.callsign = rt.GetCallsign();
        in.gsKt = rt.GetGS();
        in.fallbackTrackDeg = rt.GetTrackHeading();
        in.fallbackVsFpm = rt.GetVerticalSpeed();

        CRadarTargetPositionData p = pos;
        for (int i = 0; i < kHistoryPoints && p.IsValid(); i++)
        {
            Stca::HistoryPoint h;
            h.lat = p.GetPosition().m_Latitude;
            h.lon = p.GetPosition().m_Longitude;
            h.ageSec = p.GetReceivedTime();
            h.altFt = p.GetFlightLevel();
            in.history.push_back(h);
            p = rt.GetPreviousPosition(p);
        }

        CFlightPlan fp = rt.GetCorrelatedFlightPlan();
        if (fp.IsValid())
        {
            // EuroScope falls back to the final level when no CFL is set, which is a
            // fair limit too: nobody climbs past their requested level. 1 and 2 mean
            // cleared for an ILS or a visual approach, with no level to stop at.
            int cfl = fp.GetClearedAltitude();
            if (cfl > 2)
            {
                // Below the transition level the CFL is an altitude on QNH; put it on
                // the standard pressure the levels are compared on.
                if (cfl < transitionFt)
                    cfl += pos.GetFlightLevel() - pos.GetPressureAltitude();
                in.cflFt = cfl;
            }
            const int heading = fp.GetControllerAssignedData().GetAssignedHeading();
            if (heading > 0)
                in.assignedHeadingDeg = fmod(heading + m_stcaVariation + 360.0, 360.0);
        }

        const Stca::TrackState state = Stca::Estimate(in, settings);
        if (!state.valid)
            continue;
        states.push_back(state);

        // How far EuroScope's own track heading is from the true one, on straight
        // fast tracks, so .stca can tell whether it is magnetic.
        if (state.turnDegSec == 0.0 && state.gsKt > 150.0)
        {
            offsetSum += Wrap180(rt.GetTrackHeading() - state.trackDeg);
            offsetCount++;
        }
    }
    if (offsetCount > 0)
    {
        m_stcaTrackHeadingOffset = offsetSum / offsetCount;
        m_stcaOffsetSamples = offsetCount;
    }
    m_stcaTracks = (int)states.size();

    const std::vector<Stca::Conflict> found = Stca::Detect(states, settings);

    std::set<std::string> seen;
    for (const Stca::Conflict& c : found)
    {
        const std::string key = PairKey(c.a, c.b);
        seen.insert(key);
        StcaWatch& watch = m_stcaWatch[key];
        // Keep the pair in a stable order for drawing, whichever way Detect put it.
        watch.last = c;
        if (c.a > c.b)
        {
            std::swap(watch.last.a, watch.last.b);
            std::swap(watch.last.aLat, watch.last.bLat);
            std::swap(watch.last.aLon, watch.last.bLon);
            std::swap(watch.last.aAltFt, watch.last.bAltFt);
        }
        watch.hits++;
        watch.misses = 0;
        if (c.lossNow || watch.hits >= kRaiseHits)
        {
            if (!watch.raised)
            {
                char line[192];
                sprintf_s(line, "%s %s / %s: in %d s, %.1f NM %.0f ft at %d s", c.lossNow ? "loss" : "predicted",
                    watch.last.a.c_str(), watch.last.b.c_str(), c.timeToLossSec, c.cpaNm, c.cpaVerticalFt,
                    c.timeToCpaSec);
                Log::Info("stca", line);
            }
            watch.raised = true;
        }
    }
    for (auto it = m_stcaWatch.begin(); it != m_stcaWatch.end(); )
    {
        if (seen.count(it->first))
        {
            ++it;
            continue;
        }
        it->second.hits = 0;
        it->second.misses++;
        // Dropped for good only now - and with it any inhibit, so a pair that comes
        // back together later alerts again.
        it = it->second.misses >= kDropMisses ? m_stcaWatch.erase(it) : std::next(it);
    }

    m_kfConflicts.clear();
    m_ssaViolations.clear();
    for (const auto& entry : m_stcaWatch)
    {
        const StcaWatch& watch = entry.second;
        if (!watch.raised)
            continue;
        // A loss shows only while it is really there; a prediction rides out a miss or two.
        // A predicted conflict is flagged as SSA too, with no prediction geometry on the scope.
        if ((watch.last.lossNow && watch.misses == 0) || StcaShown(watch))
        {
            m_ssaViolations.insert(watch.last.a);
            m_ssaViolations.insert(watch.last.b);
        }
    }
    m_kfConflicts = m_ssaViolations;

    QueryPerformanceCounter(&to);
    QueryPerformanceFrequency(&freq);
    m_stcaLastMs = (to.QuadPart - from.QuadPart) * 1000.0 / freq.QuadPart;
}

bool CGalaxyATMSystemRadarScreen::StcaShown(const StcaWatch& watch) const
{
    return watch.raised && !watch.inhibited && !(watch.last.lossNow && watch.misses == 0);
}

const std::set<std::string>& CGalaxyATMSystemRadarScreen::KfConflicts()
{
    const ULONGLONG now = GetTickCount64();
    if (m_kfTick == 0 || now - m_kfTick >= kStcaRecheckMs)
    {
        m_kfTick = now;
        RunStca();
    }
    return m_kfConflicts;
}

bool CGalaxyATMSystemRadarScreen::SeparationLost(const char* callsign)
{
    KfConflicts();
    return callsign != NULL && m_ssaViolations.count(callsign) != 0;
}

void CGalaxyATMSystemRadarScreen::InhibitStca(const char* pairKey)
{
    auto it = m_stcaWatch.find(pairKey != NULL ? pairKey : "");
    if (it == m_stcaWatch.end() || it->second.last.lossNow)
        return;
    it->second.inhibited = true;
    Log::Info("stca", "inhibited by the controller: " + it->first);
    m_kfTick = 0;   // recompute the formular and list marks straight away
    RequestRefresh();
}

void CGalaxyATMSystemRadarScreen::DrawStca(HDC hDC)
{
    KfConflicts();
    if (m_stcaWatch.empty())
        return;

    HFONT font = GetRulerFont(m_esFont ? m_esFont : m_fonts.Ruler);
    const DistUnit distUnit = Plugin()->UnitDist();
    const AltUnit altUnit = Plugin()->UnitAlt();

    int saved = SaveDC(hDC);
    SetBkMode(hDC, TRANSPARENT);

    for (const auto& entry : m_stcaWatch)
    {
        const StcaWatch& watch = entry.second;
        const Stca::Conflict& c = watch.last;
        // Only an actual loss of separation is drawn; predicted conflicts are not shown.
        if (!(watch.raised && c.lossNow && watch.misses == 0))
            continue;

        CRadarTarget ra = GetPlugIn()->RadarTargetSelect(c.a.c_str());
        CRadarTarget rb = GetPlugIn()->RadarTargetSelect(c.b.c_str());
        if (!ra.IsValid() || !rb.IsValid() || !ra.GetPosition().IsValid() || !rb.GetPosition().IsValid())
            continue;
        const POINT pa = ConvertCoordFromPositionToPixel(ra.GetPosition().GetPosition());
        const POINT pb = ConvertCoordFromPositionToPixel(rb.GetPosition().GetPosition());

        const COLORREF color = Theme::SeparationLoss;
        {
            // Already inside the minima: tie the two aircraft together.
            VectorCanvas canvas(hDC, color, Theme::StcaWidth);
            canvas.Line(pa.x, pa.y, pb.x, pb.y);
        }
        const POINT labelAt = { (pa.x + pb.x) / 2, (pa.y + pb.y) / 2 };
        const std::wstring text = L"SSA  " + Distance(c.nowNm, distUnit) + L"  " + Vertical(c.nowVerticalFt, altUnit);

        const SIZE size = Theme::MeasureText(hDC, font, text);
        RECT box = { labelAt.x - size.cx / 2 - 5, labelAt.y - size.cy - 10,
                     labelAt.x + size.cx / 2 + 5, labelAt.y - 8 };
        Theme::SmoothBox(hDC, box, &Theme::RdfBoxFill, &color, 4, 1);
        Theme::DrawLine(hDC, box, text, font, color, DT_CENTER | DT_VCENTER);
    }

    RestoreDC(hDC, saved);
}

std::wstring CGalaxyATMSystemRadarScreen::StcaStatusLine()
{
    const Config& config = Plugin()->GetConfig();
    if (!config.StcaEnabled())
        return Tr(L"выключен в конфиге");

    KfConflicts();
    int predicted = 0, lost = 0, inhibited = 0;
    for (const auto& entry : m_stcaWatch)
    {
        const StcaWatch& w = entry.second;
        if (!w.raised)
            continue;
        if (w.last.lossNow && w.misses == 0)
            lost++;
        else if (w.inhibited)
            inhibited++;
        else
            predicted++;
    }

    const Stca::Settings& s = config.StcaSettings();
    wchar_t line[512];
    swprintf_s(line, Tr(L"%s, целей %d, расчёт %.1f мс; КФ %d, SSA %d, подавлено %d; "
        L"трасса %.1f км, %d ft, прогноз %d с, зон аэродрома %d; склонение сектора %+.1f°"),
        m_stcaOn ? Tr(L"включён") : Tr(L"выключен"), m_stcaTracks, m_stcaLastMs, predicted, lost, inhibited,
        s.lateralNm * 1.852, (int)s.verticalFt, s.lookaheadSec, (int)StcaAreas().size(), m_stcaVariation);
    std::wstring out = line;
    if (m_stcaOffsetSamples > 0)
    {
        swprintf_s(line, Tr(L"; курс EuroScope отличается от истинного на %+.1f° (по %d бортам)"),
            m_stcaTrackHeadingOffset, m_stcaOffsetSamples);
        out += line;
    }
    return out;
}
