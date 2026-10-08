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

    const double kStandardRateDegPerSec = 3.0;
    const double kBankLimitDeg = 25.0;
    const double kGravityMs2 = 9.80665;
    const double kMsPerKnot = 0.514444;
    const double kMetresPerNm = 1852.0;
    const double speedMs = rt.GetGS() * kMsPerKnot;
    const double rateRadiusM = speedMs / (kStandardRateDegPerSec * M_PI / 180.0);
    const double bankRadiusM = speedMs * speedMs / (kGravityMs2 * tan(kBankLimitDeg * M_PI / 180.0));
    const double radiusNm = max(rateRadiusM, bankRadiusM) / kMetresPerNm;
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

void CGalaxyATMSystemRadarScreen::CollectFrameTargets()
{
    m_frameTargets.clear();
    CGalaxyATMSystemPlugin* plugin = Plugin();
    const RECT ra = GetRadarArea();

    const double widthNm = DisplayWidthNM();
    m_framePxPerNm = widthNm > 0.0 ? (ra.right - ra.left) / widthNm : 0.0;

    for (CRadarTarget rt = plugin->RadarTargetSelectFirst(); rt.IsValid();
         rt = plugin->RadarTargetSelectNext(rt))
    {
        CRadarTargetPositionData pos = rt.GetPosition();
        if (!pos.IsValid())
            continue;
        m_frameTargets.emplace_back();
        FrameTarget& t = m_frameTargets.back();
        t.rt = rt;
        t.pos = pos;
        t.fp = rt.GetCorrelatedFlightPlan();
        const char* cs = t.fp.IsValid() ? t.fp.GetCallsign() : rt.GetCallsign();
        if (cs != NULL)
            t.callsign = cs;
        t.tp = ConvertCoordFromPositionToPixel(pos.GetPosition());
        t.pressureAltFt = pos.GetPressureAltitude();
        t.onScreen = PtInRect(&ra, t.tp) != FALSE;
        t.shown = plugin->AltFilterPasses(t.pressureAltFt);
    }
}

// Whether anything drawn from tp out to reachNm can land on the radar area. Without a
// scale yet, everything counts as near.
bool CGalaxyATMSystemRadarScreen::NearScreen(POINT tp, double reachNm)
{
    if (m_framePxPerNm <= 0.0)
        return true;
    const RECT ra = GetRadarArea();
    const double margin = reachNm * m_framePxPerNm * 1.5 + 50.0;
    return tp.x >= ra.left - margin && tp.x <= ra.right + margin
        && tp.y >= ra.top - margin && tp.y <= ra.bottom + margin;
}

void CGalaxyATMSystemRadarScreen::DrawTargetSymbols(HDC hDC)
{
    const TrackSymbolSet& symbols = TrackSymbols();
    m_symbolStats = SymbolStats();
    if (symbols.empty())
        return;

    RECT ra = GetRadarArea();
    PenCache pens;

    auto history = symbols.find("HISTORY");
    auto coasted = symbols.find("COASTED");
    auto assumedStar = symbols.find("ASSUMED");

    int saved = SaveDC(hDC);
    for (const FrameTarget& t : m_frameTargets)
    {
        m_symbolStats.targets++;
        if (!t.onScreen)
        {
            m_symbolStats.offRadar++;
            continue;
        }
        if (!t.shown)
        {
            m_symbolStats.filtered++;
            continue;
        }

        CRadarTarget rt = t.rt;
        const CRadarTargetPositionData& pos = t.pos;
        CFlightPlan fp = t.fp;
        const POINT tp = t.tp;
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
        auto symbol = symbols.find(name);
        if (pos.GetReceivedTime() > 30 && coasted != symbols.end())
            symbol = coasted;
        if (symbol == symbols.end())
        {
            m_symbolStats.noSymbol++;
            continue;
        }

        const COLORREF color = GetTagColorForFlightPlan(fp);

        const COLORREF symbolColor = SeparationLost(rt.GetCallsign()) ? Theme::SeparationLoss
            : HoveredCtrLabel(t.callsign.c_str()) ? Theme::FormularHoverTarget : color;
        auto state = !t.callsign.empty() ? m_formulars.find(t.callsign) : m_formulars.end();
        if (state != m_formulars.end() && state->second.zone)
            DrawProtectionZone(hDC, pos.GetPosition(), tp, symbolColor);

        if (history != symbols.end())
        {
            const HPEN historyPen = pens.Get(color);
            CRadarTargetPositionData earlier = pos;
            for (int i = 0; i < Theme::TrackHistoryDots; i++)
            {
                earlier = rt.GetPreviousPosition(earlier);
                if (!earlier.IsValid())
                    break;
                POINT hp = ConvertCoordFromPositionToPixel(earlier.GetPosition());
                if (PtInRect(&ra, hp))
                    DrawTrackSymbol(hDC, history->second, hp, historyPen, color);
            }
        }

        const bool assumed = assumedStar != symbols.end()
            && fp.IsValid() && fp.GetState() == FLIGHT_PLAN_STATE_ASSUMED;

        const HPEN symbolPen = pens.Get(symbolColor);
        DrawTrackSymbol(hDC, symbol->second, tp, symbolPen, symbolColor, assumed ? kAssumedHoleRadius : 0.0);
        if (assumed)
            DrawTrackSymbol(hDC, assumedStar->second, tp, symbolPen, symbolColor);
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
