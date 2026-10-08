#include "pch.h"
#include "Screen/ScreenCommon.h"

using namespace Galaxy;

namespace
{
    // How long the line stays on after the carrier drops, so a short press is still
    // visible. The reading in the box is not cleared at all - it is the крайний пеленг.
    const ULONGLONG kHoldMs = 2000;

    // The step used to read off the screen direction of the control bearing. Long
    // enough that pixel rounding does not swing the angle, short enough that the
    // great circle has not curved away yet.
    const double kRadialStepShare = 0.1;
    const double kRadialStepMinNM = 5.0;

    double Wrap360(double deg)
    {
        double v = fmod(deg, 360.0);
        return v < 0.0 ? v + 360.0 : v;
    }

    int Round360(double deg)
    {
        return (int)lround(Wrap360(deg)) % 360;
    }

    // True great circle bearing from the station, the way a direction finder measures
    // it. Not CPosition::DirectionTo: that one is magnetic, using the sector file
    // deviation, and mixing it with true bearings put the line beside the aircraft.
    double TrueBearing(const CPosition& from, const CPosition& to)
    {
        const double k = M_PI / 180.0;
        const double lat1 = from.m_Latitude * k, lat2 = to.m_Latitude * k;
        const double dLon = (to.m_Longitude - from.m_Longitude) * k;
        const double y = sin(dLon) * cos(lat2);
        const double x = cos(lat1) * sin(lat2) - sin(lat1) * cos(lat2) * cos(dLon);
        return Wrap360(atan2(y, x) / k);
    }
}

bool CGalaxyATMSystemRadarScreen::RdfStationPosition(const RdfStation& station, CPosition& out)
{
    if (station.hasPoint)
    {
        out = station.point;
        return true;
    }

    // No coordinates given, so take the aerodrome reference point out of the sector
    // file - the АРП normally stands there anyway.
    auto cached = m_rdfAirports.find(station.id);
    if (cached != m_rdfAirports.end())
    {
        out = cached->second;
        return true;
    }

    const std::string icao = Narrow(station.id);
    CPosition found;
    found.m_Latitude = found.m_Longitude = 0.0;
    for (CSectorElement airport = GetPlugIn()->SectorFileElementSelectFirst(SECTOR_ELEMENT_AIRPORT);
         airport.IsValid();
         airport = GetPlugIn()->SectorFileElementSelectNext(airport, SECTOR_ELEMENT_AIRPORT))
    {
        const char* name = airport.GetName();
        if (name == NULL || _stricmp(name, icao.c_str()) != 0)
            continue;
        CPosition pos;
        if (airport.GetPosition(&pos, 0))
            found = pos;
        break;
    }

    if (found.m_Latitude == 0.0 && found.m_Longitude == 0.0)
    {
        // Not cached: the sector file may simply not be loaded yet, and caching the
        // miss would leave the пеленгатор dead until EuroScope restarts.
        if (m_rdfWarnedAirports.insert(station.id).second)
            Log::Warn("rdf", "АРП " + icao + " has no Point in the config and no airport of that name"
                " in the sector file - no bearing can be drawn");
        return false;
    }

    m_rdfAirports[station.id] = found;
    out = found;
    return true;
}

void CGalaxyATMSystemRadarScreen::CollectRdfFixes(const CPosition& arp, const RdfStation& station)
{
    m_rdfLive.clear();

    const RdfClient::State state = Plugin()->Rdf().Snapshot();

    for (const std::string& callsign : state.heard)
    {
        CRadarTarget rt = GetPlugIn()->RadarTargetSelect(callsign.c_str());
        if (!rt.IsValid())
            continue;   // a controller, or an aircraft we have no radar data for
        CRadarTargetPositionData pos = rt.GetPosition();
        if (!pos.IsValid())
            continue;

        RdfFix fix;
        fix.valid = true;
        fix.target = pos.GetPosition();
        fix.toTarget = true;
        fix.bearing = TrueBearing(arp, fix.target);
        fix.reading = Wrap360(fix.bearing - station.variation);
        fix.callsign = callsign;
        m_rdfLive.push_back(fix);
    }

    // Our own transmission does not come from anywhere on the map: the station shows
    // its control bearing instead, the one the controller knows by heart. That one is
    // quoted the way the station reads out, so it turns back into a true bearing here.
    if (state.selfTx && station.controlBearing >= 0)
    {
        RdfFix fix;
        fix.valid = true;
        fix.reading = station.controlBearing;
        fix.bearing = Wrap360(fix.reading + station.variation);
        fix.self = true;
        m_rdfLive.push_back(fix);
    }

    if (m_rdfLive.empty())
        return;

    // The reading follows an aircraft when there is one; the control bearing is what
    // the station falls back to while we are the only one on the air.
    m_rdfLast = m_rdfLive.front();
    for (const RdfFix& fix : m_rdfLive)
    {
        if (!fix.self)
        {
            m_rdfLast = fix;
            break;
        }
    }
    m_rdfHoldUntil = GetTickCount64() + kHoldMs;
}

void CGalaxyATMSystemRadarScreen::DrawRdf(HDC hDC)
{
    const RdfStation* station = m_rdfVisible ? Plugin()->RdfStation() : NULL;

    CPosition arp;
    if (station == NULL || !RdfStationPosition(*station, arp))
    {
        m_rdfLive.clear();
        return;
    }

    CollectRdfFixes(arp, *station);

    std::vector<RdfFix> draw = m_rdfLive;
    if (draw.empty() && m_rdfLast.valid && GetTickCount64() < m_rdfHoldUntil)
        draw.push_back(m_rdfLast);
    if (draw.empty())
        return;

    // Two aircraft keying at once is exactly the case the АРП cannot resolve, so
    // both bearings are marked rather than quietly drawn as if they were good.
    int aircraft = 0;
    for (const RdfFix& fix : draw)
        aircraft += fix.self ? 0 : 1;
    const bool concurrent = aircraft > 1;

    const RECT ra = GetRadarArea();
    const double stepNM = max(kRadialStepMinNM, DisplayWidthNM() * kRadialStepShare);
    const POINT from = ConvertCoordFromPositionToPixel(arp);

    // The АРП is often off the picture (Kotlas, seen from the western sectors), so
    // the radial has to reach the far corner of the radar area from wherever it is.
    double reach = 0.0;
    for (const POINT corner : { POINT{ ra.left, ra.top }, POINT{ ra.right, ra.top },
                                POINT{ ra.left, ra.bottom }, POINT{ ra.right, ra.bottom } })
    {
        const double dx = (double)corner.x - from.x, dy = (double)corner.y - from.y;
        reach = max(reach, sqrt(dx * dx + dy * dy));
    }

    Theme::AntiAliased smooth;
    VectorCanvas canvas(hDC, Theme::RdfLine, Theme::RdfWidth);
    for (const RdfFix& fix : draw)
    {
        double toX = 0.0, toY = 0.0;
        if (fix.toTarget)
        {
            // Straight onto the aircraft itself, not rebuilt from a bearing and a
            // distance, so the line cannot miss the target whatever the projection.
            const POINT to = ConvertCoordFromPositionToPixel(fix.target);
            toX = to.x;
            toY = to.y;
        }
        else
        {
            // The control bearing has no aircraft to stop at. Take the direction from a
            // short step along the bearing, so the line leaves the АРП at the angle the
            // station reads out whatever the projection does further away, then run it
            // past the edge of the picture.
            const POINT step = ConvertCoordFromPositionToPixel(
                CalculateDestinationPoint(arp, fix.bearing, stepNM));
            const double dx = (double)step.x - from.x, dy = (double)step.y - from.y;
            const double len = sqrt(dx * dx + dy * dy);
            if (len < 1e-6)
                continue;
            toX = from.x + dx / len * reach;
            toY = from.y + dy / len * reach;
        }

        canvas.SetColor(concurrent ? Theme::RdfConcurrent
            : fix.self ? Theme::RdfControl : Theme::RdfLine);
        canvas.Line(from.x, from.y, toX, toY);
    }
}

void CGalaxyATMSystemRadarScreen::DrawRdfBox(HDC hDC)
{
    if (!m_rdfVisible)
        return;

    const RdfStation* station = Plugin()->RdfStation();
    if (station == NULL)
        return;

    const bool live = !m_rdfLive.empty();
    std::wstring caption = Tr(L"АРП ") + station->id;
    std::wstring reading = L"---/---";
    if (m_rdfLast.valid)
    {
        // The forward bearing and its reciprocal, the pair the АРП shows.
        const int direct = Round360(m_rdfLast.reading);
        wchar_t text[16];
        swprintf_s(text, L"%03d/%03d", direct, (direct + 180) % 360);
        reading = text;
    }

    const COLORREF ink = !m_rdfLast.valid ? Theme::RdfBoxIdle
        : m_rdfLast.self ? Theme::RdfControl : Theme::RdfLine;

    int saved = SaveDC(hDC);
    SetBkMode(hDC, TRANSPARENT);

    const SIZE capSize = Theme::MeasureText(hDC, m_fonts.Small, caption);
    const SIZE readSize = Theme::MeasureText(hDC, m_fonts.Clock, reading);

    const int padX = 10, padY = 6, gap = 1;
    const int width = max(capSize.cx, readSize.cx) + 2 * padX;
    const int height = capSize.cy + gap + readSize.cy + 2 * padY;

    const RECT ra = GetRadarArea();
    const bool panelShown = m_visible && m_panelArea.right > m_panelArea.left;
    const int right = (panelShown ? m_panelArea.left : ra.right) - 8;
    const int top = PanelTop() + MenuBarHeight() + 8;
    const RECT box = { right - width, top, right, top + height };
    if (box.left > ra.left && box.bottom < ra.bottom)
    {
        Theme::SmoothBox(hDC, box, &Theme::RdfBoxFill,
            live ? &ink : &Theme::RdfBoxEdge, 6, 1);

        RECT capRect = { box.left, box.top + padY, box.right, box.top + padY + capSize.cy };
        Theme::DrawLine(hDC, capRect, caption, m_fonts.Small, Theme::TextDim, DT_CENTER | DT_VCENTER);

        RECT readRect = { box.left, capRect.bottom + gap, box.right, capRect.bottom + gap + readSize.cy };
        Theme::DrawLine(hDC, readRect, reading, m_fonts.Clock, ink, DT_CENTER | DT_VCENTER);
    }

    RestoreDC(hDC, saved);
}

void CGalaxyATMSystemRadarScreen::PollRdf()
{
    if (!m_rdfVisible)
        return;

    // Transmissions are a second or two long, so waiting for the one second tick
    // would lose half of them. Redraw as soon as the client reports a change.
    const unsigned generation = Plugin()->Rdf().Generation();
    const bool fading = m_rdfLast.valid && m_rdfLive.empty() && GetTickCount64() < m_rdfHoldUntil;
    if (generation == m_rdfGeneration && !fading)
        return;

    m_rdfGeneration = generation;
    RequestRefresh();
}

std::wstring CGalaxyATMSystemRadarScreen::RdfStatusLine()
{
    const Config& config = Plugin()->GetConfig();
    std::wstring what = m_rdfVisible ? Tr(L"включён") : Tr(L"выключен");

    if (!config.RdfEnabled())
        return what + Tr(L", отключён в конфиге");

    const RdfStation* station = Plugin()->RdfStation();
    if (station == NULL)
        return what + (config.RdfStations().empty() ? Tr(L", станции не заданы в конфиге")
            : Tr(L", у этой позиции нет АРП"));

    what += Tr(L", станция ") + station->id;

    CPosition arp;
    if (!RdfStationPosition(*station, arp))
        what += Tr(L" (координаты не найдены)");

    if (station->controlBearing >= 0)
    {
        wchar_t text[16];
        swprintf_s(text, L"%03d/%03d", station->controlBearing, (station->controlBearing + 180) % 360);
        what += Tr(L", контрольный пеленг ") + std::wstring(text);
    }
    else
    {
        what += Tr(L", контрольный пеленг не задан");
    }

    const RdfClient& rdf = Plugin()->Rdf();
    what += Tr(L", TrackAudio: ") + std::wstring(rdf.TrackAudioConnected() ? Tr(L"есть") : Tr(L"нет"));
    what += Tr(L", AFV: ") + std::wstring(!rdf.BridgeListening() ? Tr(L"окно не открылось")
        : rdf.BridgeTaken() ? Tr(L"занято другим плагином") : Tr(L"ждёт"));

    return what;
}
