#include "pch.h"
#include "Screen/ScreenCommon.h"

using namespace Galaxy;

COLORREF CGalaxyATMSystemRadarScreen::GetTagColorForFlightPlan(CFlightPlan fp)
{
    if (!fp.IsValid())
        return RGB(150, 150, 150);

    int state = fp.GetState();
    if (state == FLIGHT_PLAN_STATE_ASSUMED || state == FLIGHT_PLAN_STATE_TRANSFER_FROM_ME_INITIATED)
        return RGB(255, 255, 255);
    if (state == FLIGHT_PLAN_STATE_TRANSFER_TO_ME_INITIATED)
        return RGB(0, 255, 255);
    return RGB(150, 150, 150);
}

CPosition CGalaxyATMSystemRadarScreen::CalculateDestinationPoint(CPosition start, double bearingDeg, double distanceNM)
{
    const double R = 3440.065;

    double lat1 = start.m_Latitude * M_PI / 180.0;
    double lon1 = start.m_Longitude * M_PI / 180.0;
    double bearing = bearingDeg * M_PI / 180.0;
    double angularDistance = distanceNM / R;

    double lat2 = asin(sin(lat1) * cos(angularDistance) + cos(lat1) * sin(angularDistance) * cos(bearing));
    double lon2 = lon1 + atan2(sin(bearing) * sin(angularDistance) * cos(lat1),
        cos(angularDistance) - sin(lat1) * sin(lat2));

    CPosition dest;
    dest.m_Latitude = lat2 * 180.0 / M_PI;
    dest.m_Longitude = lon2 * 180.0 / M_PI;
    return dest;
}

void CGalaxyATMSystemRadarScreen::DrawTrackVector(VectorCanvas& canvas, std::vector<VectorLabel>& labels,
    CRadarTarget rt, double lengthNM, double timeMinForLevel, int minuteTicks, COLORREF color)
{
    CRadarTargetPositionData pos = rt.GetPosition();
    double trackHeading = rt.GetTrackHeading();
    CPosition currentPos = pos.GetPosition();

    CPosition endPos = CalculateDestinationPoint(currentPos, trackHeading, lengthNM);
    POINT p0 = ConvertCoordFromPositionToPixel(currentPos);
    POINT p1 = ConvertCoordFromPositionToPixel(endPos);

    double dx = p1.x - p0.x, dy = p1.y - p0.y;
    double totalLen = sqrt(dx * dx + dy * dy);
    if (totalLen < 1.0)
        return;

    double heading = atan2(dy, dx);

    double levelFrac = 1.0;
    int verticalSpeed = rt.GetVerticalSpeed();
    CFlightPlan vecFp = rt.GetCorrelatedFlightPlan();
    if (vecFp.IsValid() && abs(verticalSpeed) > 100 && timeMinForLevel > 0.0)
    {
        int clearedFt = vecFp.GetClearedAltitude();
        if (clearedFt > 0)
        {
            bool clearedIsFL = clearedFt / 100 >= Plugin()->TransitionLevelFL();
            int currentFt = clearedIsFL ? pos.GetFlightLevel() : pos.GetPressureAltitude();

            double minutesToLevel = (double)(clearedFt - currentFt) / verticalSpeed;
            if (minutesToLevel > 0.0)
                levelFrac = min(1.0, minutesToLevel / timeMinForLevel);
        }
    }

    POINT pMark;
    pMark.x = p0.x + (int)lround((p1.x - p0.x) * levelFrac);
    pMark.y = p0.y + (int)lround((p1.y - p0.y) * levelFrac);

    const double arrowAngle = 28.0 * M_PI / 180.0;
    double headLength = min(Theme::VectorHeadLength, totalLen * levelFrac * 0.4);

    Gdiplus::PointF chevron[3] = {
        Gdiplus::PointF((Gdiplus::REAL)(pMark.x + headLength * cos(heading + M_PI - arrowAngle)),
                        (Gdiplus::REAL)(pMark.y + headLength * sin(heading + M_PI - arrowAngle))),
        Gdiplus::PointF((Gdiplus::REAL)pMark.x, (Gdiplus::REAL)pMark.y),
        Gdiplus::PointF((Gdiplus::REAL)(pMark.x + headLength * cos(heading + M_PI + arrowAngle)),
                        (Gdiplus::REAL)(pMark.y + headLength * sin(heading + M_PI + arrowAngle))),
    };

    bool isLevel = abs(verticalSpeed) <= 100;

    canvas.SetColor(color);
    if (minuteTicks >= 2)
    {
        double ux = (p1.x - p0.x) / totalLen, uy = (p1.y - p0.y) / totalLen;
        double segLen = totalLen / minuteTicks;
        double gap = min(Theme::VectorTickGap, segLen * 0.25);

        for (int i = 0; i < minuteTicks; i++)
        {
            double from = i * segLen;
            double to = (i + 1) * segLen - gap;
            canvas.Line(p0.x + ux * from, p0.y + uy * from, p0.x + ux * to, p0.y + uy * to);
        }
    }
    else
        canvas.Line(p0.x, p0.y, p1.x, p1.y);

    if (m_vecShowLevel && headLength > 1.0 && !isLevel)
    {
        canvas.pen.SetWidth(Theme::VectorHeadWidth);
        canvas.g.DrawLines(&canvas.pen, chevron, 3);
        canvas.pen.SetWidth(Theme::VectorWidth);
    }

    if (m_vecShowLevel && !isLevel)
    {
        double labelTimeMin = timeMinForLevel * levelFrac;
        int climbFt = (int)(verticalSpeed * labelTimeMin);

        bool belowTL = (pos.GetFlightLevel() + climbFt) / 100 < Plugin()->TransitionLevelFL();
        int currentAltFt = belowTL ? pos.GetPressureAltitude() : pos.GetFlightLevel();
        int predictedAltFt = currentAltFt + climbFt;
        if (predictedAltFt < 0)
            predictedAltFt = 0;

        AltUnit vecUnit = (Plugin()->UnitAlt() == AltUnit::M) ? AltUnit::M : AltUnit::FL;
        std::wstring label = Widen(FormatAltitudeUnit(predictedAltFt, vecUnit).c_str());

        labels.push_back({ pMark, -sin(heading), cos(heading), label, color });
    }
}

void CGalaxyATMSystemRadarScreen::DrawPlanVector(VectorCanvas& canvas, CFlightPlan fp, const CPosition& currentPos,
    int minutes, COLORREF color)
{
    if (!fp.IsValid() || minutes <= 0)
        return;

    CFlightPlanPositionPredictions pred = fp.GetPositionPredictions();
    int count = pred.GetPointsNumber();
    if (count <= 1)
        return;
    if (minutes >= count)
        minutes = count - 1;

    std::vector<POINT> pts;
    pts.push_back(ConvertCoordFromPositionToPixel(currentPos));
    for (int i = 1; i <= minutes; i++)
        pts.push_back(ConvertCoordFromPositionToPixel(pred.GetPosition(i)));

    if (pts.size() < 2)
        return;
    const POINT& p0 = pts.front();
    const POINT& p1 = pts.back();
    if (abs(p1.x - p0.x) < 1 && abs(p1.y - p0.y) < 1)
        return;

    canvas.SetColor(color);
    if (minutes >= 2)
        DrawGappedPolyline(canvas, pts, Theme::VectorTickGap);
    else
        canvas.Line(p0.x, p0.y, p1.x, p1.y);
}

void CGalaxyATMSystemRadarScreen::DrawTargetVectors(HDC hDC)
{
    int saved = SaveDC(hDC);
    SetBkMode(hDC, TRANSPARENT);
    std::vector<VectorLabel> labels;
    std::unique_ptr<VectorCanvas> canvas;

    for (CRadarTarget rt = GetPlugIn()->RadarTargetSelectFirst(); rt.IsValid();
         rt = GetPlugIn()->RadarTargetSelectNext(rt))
    {
        CRadarTargetPositionData pos = rt.GetPosition();
        if (!pos.IsValid())
            continue;
        if (pos.GetPressureAltitude() < 700)
            continue;

        if (!Plugin()->AltFilterPasses(pos.GetPressureAltitude()))
            continue;

        int groundSpeed = rt.GetGS();
        if (groundSpeed < 10)
            continue;

        CFlightPlan fp = rt.GetCorrelatedFlightPlan();
        COLORREF color = GetTagColorForFlightPlan(fp);
        if (SeparationLost(rt.GetCallsign()))
            color = Theme::SeparationLoss;
        else if (HoveredCtrLabel(fp.IsValid() ? fp.GetCallsign() : rt.GetCallsign()))
            color = Theme::FormularHoverTarget;

        if (!canvas)
            canvas.reset(new VectorCanvas(hDC, color));

        if (m_vecTimeEnabled)
        {
            double lengthNM = (groundSpeed / 60.0) * m_vecTimeMin;
            DrawTrackVector(*canvas, labels, rt, lengthNM, (double)m_vecTimeMin, m_vecTimeMin, color);
        }

        if (m_vecDistEnabled)
        {
            double lengthNM = m_vecDistKm / 1.852;
            double timeMin = (groundSpeed > 0) ? (lengthNM / (groundSpeed / 60.0)) : 0.0;
            DrawTrackVector(*canvas, labels, rt, lengthNM, timeMin, 0, color);
        }

        if (m_vecByPlan && fp.IsValid() && m_vecTimeMin > 0)
        {
            DrawPlanVector(*canvas, fp, pos.GetPosition(), m_vecTimeMin, color);
        }
    }
    canvas.reset();

    const HFONT labelFont = m_esFont ? m_esFont : m_fonts.Small;
    const double kLabelGapPx = 8.0;
    for (const VectorLabel& label : labels)
    {
        const SIZE size = Theme::MeasureText(hDC, labelFont, label.text);
        const RECT r = PlaceBeside(label.mark, label.sideX, label.sideY, size, kLabelGapPx);
        Theme::DrawLine(hDC, r, label.text, labelFont, label.color, DT_LEFT | DT_VCENTER | DT_NOCLIP);
    }

    RestoreDC(hDC, saved);
}

bool CGalaxyATMSystemRadarScreen::AnyEntryOpen() const
{
    for (const TextEntry* entry : { &m_spdEntry, &m_ahdgEntry, &m_xfrEntry, &m_ftEntry, &m_cflEntry, &m_entry, &m_rcEntry })
        if (entry->IsOpen())
            return true;
    return false;
}

void CGalaxyATMSystemRadarScreen::PollRouteClearKey()
{
    HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    if (fg != NULL)
        GetWindowThreadProcessId(fg, &pid);
    const bool down = pid == GetCurrentProcessId() && (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    const bool entryOpen = AnyEntryOpen();
    if (down && !m_routeClearKeyDown && !entryOpen && !m_entryOpenBeforeKey && !m_routeShown.empty())
    {
        Log::Info("formular", "ESC: routes hidden for " + std::to_string(m_routeShown.size()) + " aircraft");
        m_routeShown.clear();
        RequestRefresh();
    }
    m_routeClearKeyDown = down;
    m_entryOpenBeforeKey = entryOpen;
}

void CGalaxyATMSystemRadarScreen::DrawRoutes(HDC hDC)
{
    struct PointName
    {
        POINT at;
        double sideX, sideY;
        std::wstring name;
        COLORREF color;
    };
    std::vector<PointName> names;
    const RECT ra = GetRadarArea();
    SYSTEMTIME utc;
    GetSystemTime(&utc);
    const int nowMin = utc.wHour * 60 + utc.wMinute;
    {
        std::unique_ptr<AreaCanvas> canvas;
        for (const std::string& callsign : m_routeShown)
        {
            CFlightPlan fp = GetPlugIn()->FlightPlanSelect(callsign.c_str());
            if (!fp.IsValid())
                continue;
            CFlightPlanExtractedRoute route = fp.GetExtractedRoute();
            const int count = route.GetPointsNumber();
            int from = route.GetPointsCalculatedIndex();
            const int direct = route.GetPointsAssignedIndex();
            if (direct >= 0 && direct < count && route.GetPointDistanceInMinutes(direct) >= 0)
                from = direct;
            if (from < 0)
                from = 0;
            if (from >= count)
                continue;

            const COLORREF color = Theme::RouteColor;
            std::vector<POINT> path;
            std::vector<std::pair<size_t, std::wstring>> pointNames;
            CRadarTarget rt = fp.GetCorrelatedRadarTarget();
            if (rt.IsValid() && rt.GetPosition().IsValid())
                path.push_back(ConvertCoordFromPositionToPixel(rt.GetPosition().GetPosition()));
            for (int i = from; i < count; i++)
            {
                const POINT p = ConvertCoordFromPositionToPixel(route.GetPointPosition(i));
                path.push_back(p);
                const char* name = route.GetPointName(i);
                if (name == NULL || *name == '\0' || !PtInRect(&ra, p))
                    continue;
                std::wstring text = Widen(name);
                const int minutes = route.GetPointDistanceInMinutes(i);
                if (minutes >= 0)
                {
                    const int at = (nowMin + minutes) % (24 * 60);
                    wchar_t eta[8];
                    swprintf_s(eta, L" %02d%02d", at / 60, at % 60);
                    text += eta;
                }
                pointNames.push_back({ path.size() - 1, text });
            }
            for (const auto& named : pointNames)
            {
                double sideX = 0.0, sideY = 0.0;
                FreeSideOf(path, named.first, sideX, sideY);
                names.push_back({ path[named.first], sideX, sideY, named.second, color });
            }
            if (path.size() < 2)
                continue;

            if (!canvas)
            {
                canvas.reset(new AreaCanvas(hDC, ra, color, Theme::RouteWidth));
                canvas->pen.SetStartCap(Gdiplus::LineCapRound);
                canvas->pen.SetEndCap(Gdiplus::LineCapRound);
            }
            canvas->SetColor(color);
            canvas->Ring(path, false);

            Gdiplus::SolidBrush dot(Theme::GdiColor(color));
            const Gdiplus::REAL r = (Gdiplus::REAL)Theme::RoutePointRadiusPx;
            for (size_t i = 1; i < path.size(); i++)
                if (PtInRect(&ra, path[i]))
                    canvas->g.FillEllipse(&dot, path[i].x - r, path[i].y - r, 2 * r, 2 * r);
        }
    }

    if (names.empty())
        return;
    const HFONT font = m_esFont ? m_esFont : m_fonts.Small;
    const double kNameGapPx = 6.0;
    int saved = SaveDC(hDC);
    SetBkMode(hDC, TRANSPARENT);
    for (const PointName& n : names)
    {
        const SIZE size = Theme::MeasureText(hDC, font, n.name);
        const RECT r = PlaceBeside(n.at, n.sideX, n.sideY, size, kNameGapPx);
        Theme::DrawLine(hDC, r, n.name, font, n.color, DT_LEFT | DT_VCENTER | DT_NOCLIP);
    }
    RestoreDC(hDC, saved);
}

void CGalaxyATMSystemRadarScreen::DrawWakeArcs(HDC hDC)
{
    const RECT ra = GetRadarArea();
    std::unique_ptr<VectorCanvas> canvas;
    for (CRadarTarget rt = GetPlugIn()->RadarTargetSelectFirst(); rt.IsValid();
         rt = GetPlugIn()->RadarTargetSelectNext(rt))
    {
        CRadarTargetPositionData pos = rt.GetPosition();
        if (!pos.IsValid())
            continue;

        if (pos.GetPressureAltitude() < 700)
            continue;
        if (!Plugin()->AltFilterPasses(pos.GetPressureAltitude()))
            continue;

        CFlightPlan fp = rt.GetCorrelatedFlightPlan();
        if (!fp.IsValid())
            continue;
        char wtc = fp.GetFlightPlanData().GetAircraftWtc();
        int arcs = (wtc == 'J') ? 2 : (wtc == 'H') ? 1 : 0;
        if (arcs == 0)
            continue;

        const double kAheadNM = 20.0;
        CPosition here = pos.GetPosition();
        POINT c = ConvertCoordFromPositionToPixel(here);
        if (!PtInRect(&ra, c))
            continue;
        POINT ahead = ConvertCoordFromPositionToPixel(
            CalculateDestinationPoint(here, rt.GetTrackHeading(), kAheadNM));
        double dx = ahead.x - c.x, dy = ahead.y - c.y;
        if (dx * dx + dy * dy < 1.0)
            continue;

        double behindDeg = atan2(dy, dx) * 180.0 / M_PI + 180.0;

        const COLORREF color = SeparationLost(rt.GetCallsign()) ? Theme::SeparationLoss
            : GetTagColorForFlightPlan(fp);
        if (!canvas)
        {
            canvas.reset(new VectorCanvas(hDC, color, Theme::WakeArcWidth));
            canvas->pen.SetStartCap(Gdiplus::LineCapRound);
            canvas->pen.SetEndCap(Gdiplus::LineCapRound);
        }
        canvas->pen.SetColor(Gdiplus::Color(GetRValue(color), GetGValue(color), GetBValue(color)));
        for (int i = 0; i < arcs; i++)
        {
            double r = Theme::WakeArcRadiusPx + i * Theme::WakeArcStepPx;
            canvas->g.DrawArc(&canvas->pen,
                (Gdiplus::REAL)(c.x - r), (Gdiplus::REAL)(c.y - r),
                (Gdiplus::REAL)(2.0 * r), (Gdiplus::REAL)(2.0 * r),
                (Gdiplus::REAL)(behindDeg - Theme::WakeArcSweepDeg / 2.0),
                (Gdiplus::REAL)Theme::WakeArcSweepDeg);
        }
    }
}
