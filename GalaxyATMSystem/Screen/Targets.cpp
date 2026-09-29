#include "pch.h"
#include "Screen/ScreenCommon.h"

using namespace Galaxy;

void CGalaxyATMSystemRadarScreen::HeadingTurnPath(CRadarTarget rt, double headingDeg,
    double totalNm, std::vector<POINT>& out)
{
    out.clear();
    if (!rt.IsValid() || !rt.GetPosition().IsValid())
        return;

    const CPosition start = rt.GetPosition().GetPosition();
    out.push_back(ConvertCoordFromPositionToPixel(start));

    if (rt.GetGS() < 30)
    {
        out.push_back(ConvertCoordFromPositionToPixel(
            CalculateDestinationPoint(start, headingDeg, max(0.1, totalNm))));
        return;
    }

    const double kRoundPx = 15.0;
    const POINT atStart = out.front();
    const POINT aMileOff = ConvertCoordFromPositionToPixel(
        CalculateDestinationPoint(start, 90.0, 1.0));
    const double dxPx = (double)aMileOff.x - atStart.x, dyPx = (double)aMileOff.y - atStart.y;
    const double pxPerNm = sqrt(dxPx * dxPx + dyPx * dyPx);
    const double radiusNm = max(0.02, min(pxPerNm > 0.01 ? kRoundPx / pxPerNm : 0.5,
        totalNm / 4.0));
    const double track = rt.GetTrackHeading();
    const double delta = fmod(headingDeg - track + 540.0, 360.0) - 180.0;
    const double dir = (delta >= 0.0) ? 1.0 : -1.0;
    const double turn = fabs(delta);

    const CPosition centre = CalculateDestinationPoint(start, track + dir * 90.0, radiusNm);
    const double fromCentre = track - dir * 90.0;

    const double kStepDeg = 5.0;
    for (double a = kStepDeg; a < turn; a += kStepDeg)
        out.push_back(ConvertCoordFromPositionToPixel(
            CalculateDestinationPoint(centre, fromCentre + dir * a, radiusNm)));

    const CPosition rollOut = CalculateDestinationPoint(centre, fromCentre + dir * turn, radiusNm);
    out.push_back(ConvertCoordFromPositionToPixel(rollOut));

    const double straightNm = totalNm - radiusNm * turn * M_PI / 180.0;
    if (straightNm > 0.05)
        out.push_back(ConvertCoordFromPositionToPixel(
            CalculateDestinationPoint(rollOut, headingDeg, straightNm)));
}

void CGalaxyATMSystemRadarScreen::DrawTargetSymbols(HDC hDC)
{
    const TrackSymbolSet& symbols = TrackSymbols();
    m_symbolStats = SymbolStats();
    if (symbols.empty())
        return;

    CGalaxyATMSystemPlugin* plugin = Plugin();
    RECT ra = GetRadarArea();

    int saved = SaveDC(hDC);
    for (CRadarTarget rt = plugin->RadarTargetSelectFirst(); rt.IsValid();
         rt = plugin->RadarTargetSelectNext(rt))
    {
        m_symbolStats.targets++;
        CRadarTargetPositionData pos = rt.GetPosition();
        if (!pos.IsValid())
            continue;
        POINT tp = ConvertCoordFromPositionToPixel(pos.GetPosition());
        if (!PtInRect(&ra, tp))
        {
            m_symbolStats.offRadar++;
            continue;
        }
        if (!plugin->AltFilterPasses(pos.GetPressureAltitude()))
        {
            m_symbolStats.filtered++;
            continue;
        }

        CFlightPlan fp = rt.GetCorrelatedFlightPlan();
        const char* plan = fp.IsValid() ? fp.GetFlightPlanData().GetPlanType() : NULL;
        const bool vfr = plan != NULL && (plan[0] == 'V' || plan[0] == 'v');

        std::string name;
        if (vfr && fp.GetState() != FLIGHT_PLAN_STATE_ASSUMED)
        {
            name = "UNCONTROLLED";
        }
        else
        {
            const int flags = pos.GetRadarFlags();
            if (flags & RADAR_POSITION_SECONDARY_S)
                name = "DAPS";
            else if (flags & RADAR_POSITION_SECONDARY_C)
                name = "NODAPS";
            else if (flags & RADAR_POSITION_PRIMARY)
                name = "PRIMARY";
            else
                name = "NODAPS";

            const bool diverging = fp.IsValid()
                && (fp.GetCLAMFlag()
                    || (fp.GetTrackingControllerIsMe() && (fp.GetRAMFlag() || RouteAdherenceAlert(fp, rt))));
            if (pos.GetTransponderI() && symbols.count(name + "_SPI"))
                name += "_SPI";
            else if (diverging && symbols.count(name + "_DIV"))
                name += "_DIV";
        }
        if (pos.GetReceivedTime() > 30 && symbols.count("COASTED"))
            name = "COASTED";

        auto symbol = symbols.find(name);
        if (symbol == symbols.end())
        {
            m_symbolStats.noSymbol++;
            continue;
        }

        const COLORREF color = GetTagColorForFlightPlan(fp);

        const char* cs = fp.IsValid() ? fp.GetCallsign() : rt.GetCallsign();
        const COLORREF symbolColor = SeparationLost(rt.GetCallsign()) ? Theme::SeparationLoss
            : HoveredCtrLabel(cs) ? Theme::FormularHoverTarget : color;
        auto state = (cs != NULL) ? m_formulars.find(cs) : m_formulars.end();
        if (state != m_formulars.end() && state->second.zone)
            DrawProtectionZone(hDC, pos.GetPosition(), tp, symbolColor);

        auto history = symbols.find("HISTORY");
        if (history != symbols.end())
        {
            CRadarTargetPositionData earlier = pos;
            for (int i = 0; i < Theme::TrackHistoryDots; i++)
            {
                earlier = rt.GetPreviousPosition(earlier);
                if (!earlier.IsValid())
                    break;
                POINT hp = ConvertCoordFromPositionToPixel(earlier.GetPosition());
                if (PtInRect(&ra, hp))
                    DrawTrackSymbol(hDC, history->second, hp, color);
            }
        }

        auto star = (fp.IsValid() && fp.GetState() == FLIGHT_PLAN_STATE_ASSUMED)
            ? symbols.find("ASSUMED") : symbols.end();
        const bool assumed = star != symbols.end();

        DrawTrackSymbol(hDC, symbol->second, tp, symbolColor, assumed ? kAssumedHoleRadius : 0.0);
        if (assumed)
            DrawTrackSymbol(hDC, star->second, tp, symbolColor);
        m_symbolStats.drawn++;
    }
    RestoreDC(hDC, saved);
}

void CGalaxyATMSystemRadarScreen::DrawProtectionZone(HDC hDC, CPosition center, POINT tp,
    COLORREF color)
{
    const double nm = kProtectionZoneKm / 1.852;
    POINT edge = ConvertCoordFromPositionToPixel(CalculateDestinationPoint(center, 90.0, nm));
    const double dx = edge.x - tp.x, dy = edge.y - tp.y;
    const double r = sqrt(dx * dx + dy * dy);
    if (r < 2.0)
        return;

    Theme::AntiAliased smoothCircle;
    VectorCanvas canvas(hDC, color, Theme::ProtectionZoneWidth);
    canvas.Circle(tp.x, tp.y, r);
}

int CGalaxyATMSystemRadarScreen::DragHeading(const char* sCallsign, POINT cursor,
    POINT* from, double* distNm, double* drawnHdg)
{
    CRadarTarget rt = GetPlugIn()->RadarTargetSelect(sCallsign);
    if (!rt.IsValid() || !rt.GetPosition().IsValid())
        return 0;

    const CPosition start = rt.GetPosition().GetPosition();
    POINT tp = ConvertCoordFromPositionToPixel(start);
    if (abs(cursor.x - tp.x) < 3 && abs(cursor.y - tp.y) < 3)
        return 0;
    const CPosition target = ConvertCoordFromPixelToPosition(cursor);

    const double raw = start.DirectionTo(target);
    int hdg = (int)lround(raw / 5.0) * 5 % 360;
    if (hdg <= 0)
        hdg += 360;

    if (from != NULL)
        *from = tp;
    if (distNm != NULL)
        *distNm = start.DistanceTo(target);
    if (drawnHdg != NULL)
        *drawnHdg = GeoBearingDeg(start, target) + (hdg - raw);
    return hdg;
}
