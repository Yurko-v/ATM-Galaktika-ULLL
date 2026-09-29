#include "pch.h"
#include "Screen/ScreenCommon.h"

using namespace Galaxy;

bool CGalaxyATMSystemRadarScreen::FindNearbyTarget(POINT pt, std::string& callsignOut)
{
    const double thresholdPx = 20.0;
    double bestDist = thresholdPx;
    bool found = false;

    for (CRadarTarget rt = GetPlugIn()->RadarTargetSelectFirst(); rt.IsValid();
         rt = GetPlugIn()->RadarTargetSelectNext(rt))
    {
        CRadarTargetPositionData pos = rt.GetPosition();
        if (!pos.IsValid())
            continue;

        POINT p = ConvertCoordFromPositionToPixel(pos.GetPosition());
        double dx = p.x - pt.x, dy = p.y - pt.y;
        double d = sqrt(dx * dx + dy * dy);
        if (d < bestDist)
        {
            bestDist = d;
            callsignOut = rt.GetCallsign();
            found = true;
        }
    }
    return found;
}

CPosition CGalaxyATMSystemRadarScreen::ResolveRulerPoint(bool snapped, const std::string& callsign, CPosition& fixed)
{
    if (snapped)
    {
        CRadarTarget rt = GetPlugIn()->RadarTargetSelect(callsign.c_str());
        if (rt.IsValid())
        {
            CRadarTargetPositionData pos = rt.GetPosition();
            if (pos.IsValid())
                fixed = pos.GetPosition();
        }
    }
    return fixed;
}

double CGalaxyATMSystemRadarScreen::GetRulerSpeedKt(const RulerLine& r)
{
    if (!r.startSnapped)
        return -1.0;
    CRadarTarget rt = GetPlugIn()->RadarTargetSelect(r.startCallsign.c_str());
    return rt.IsValid() ? rt.GetGS() : -1.0;
}

int CGalaxyATMSystemRadarScreen::FindNearestRulerIndex(POINT pt, double thresholdPx)
{
    double bestDist = thresholdPx;
    int bestIndex = -1;

    for (size_t i = 0; i < m_rulers.size(); i++)
    {
        RulerLine& r = m_rulers[i];
        POINT a = ConvertCoordFromPositionToPixel(ResolveRulerPoint(r.startSnapped, r.startCallsign, r.startFixed));
        POINT b = ConvertCoordFromPositionToPixel(ResolveRulerPoint(r.endSnapped, r.endCallsign, r.endFixed));

        double dx = b.x - a.x, dy = b.y - a.y;
        double lenSq = dx * dx + dy * dy;
        double t = (lenSq > 1e-6) ? ((pt.x - a.x) * dx + (pt.y - a.y) * dy) / lenSq : 0.0;
        t = max(0.0, min(1.0, t));
        double px = a.x + t * dx, py = a.y + t * dy;
        double dist = sqrt((pt.x - px) * (pt.x - px) + (pt.y - py) * (pt.y - py));

        if (dist < bestDist)
        {
            bestDist = dist;
            bestIndex = (int)i;
        }
    }
    return bestIndex;
}

void CGalaxyATMSystemRadarScreen::StartTimerOnConnect()
{
    const LinkSeen now = GetPlugIn()->GetConnectionType() != CONNECTION_TYPE_NO ? LinkSeen::Online : LinkSeen::Offline;
    if (m_linkSeen == LinkSeen::Offline && now == LinkSeen::Online)
    {
        m_timerElapsedMs = 0;
        m_timerStartTick = GetTickCount64();
        m_timerRunning = true;
        m_panelDirty = true;
        Log::Info("timer", "connected - timer started");
        RequestRefresh();
    }
    m_linkSeen = now;
}

void CGalaxyATMSystemRadarScreen::PollRulerButton()
{
    CloseSigmetInfoIfButtonReleased();

    bool shift = ShiftHeldInEuroScope();
    if (shift != m_areaShiftDown)
    {
        m_areaShiftDown = shift;
        RequestRefresh();
    }

    {
        std::string hover;
        POINT cursor;
        if (m_cflOpen || m_spdOpen || m_ahdgOpen || m_rvsmOpen || m_xfrOpen || m_ftOpen || m_coordOpen)
        {
            hover = m_cflOpen ? m_cflCallsign : m_spdOpen ? m_spdCallsign : m_ahdgOpen ? m_ahdgCallsign
                  : m_rvsmOpen ? m_rvsmCallsign
                  : m_xfrOpen ? m_xfrCallsign : m_ftOpen ? m_ftCallsign : m_coordCallsign;
        }
        else if (m_formularsVisible && CursorRadarPoint(cursor))
        {
            for (const auto& entry : m_formulars)
            {
                if (!entry.second.items.empty() && PtInRect(&entry.second.area, cursor))
                {
                    hover = entry.first;
                    break;
                }
            }
        }
        if (hover != m_formularHover)
        {
            m_formularHover = hover;
            RequestRefresh();
        }
    }

    {
        const ULONGLONG now = GetTickCount64();
        for (auto& entry : m_formulars)
        {
            bool redraw = false;
            for (CoordWatch* w : { &entry.second.exitCoord, &entry.second.entryCoord,
                                   &entry.second.exitPoint, &entry.second.entryPoint })
            {
                if (w->result != 0 && w->resultAt != 0 && now - w->resultAt >= kCoordResultMs)
                {
                    w->resultAt = 0;
                    redraw = true;
                }
            }
            const bool asked = entry.second.exitCoord.lastState == COORDINATION_STATE_REQUESTED_BY_ME
                || entry.second.entryCoord.lastState == COORDINATION_STATE_REQUESTED_BY_ME
                || entry.second.exitPoint.lastState == COORDINATION_STATE_REQUESTED_BY_ME
                || entry.second.entryPoint.lastState == COORDINATION_STATE_REQUESTED_BY_ME;
            if (asked)
            {
                CFlightPlan fp = GetPlugIn()->FlightPlanSelect(entry.first.c_str());
                if (fp.IsValid()
                    && (fp.GetExitCoordinationAltitudeState() != entry.second.exitCoord.lastState
                        || fp.GetEntryCoordinationAltitudeState() != entry.second.entryCoord.lastState
                        || fp.GetExitCoordinationNameState() != entry.second.exitPoint.lastState
                        || fp.GetEntryCoordinationPointState() != entry.second.entryPoint.lastState))
                    redraw = true;
            }
            if (redraw)
                RequestRefresh();
        }
    }

    StartTimerOnConnect();
    TickCflPicker();
    TickSpeedWindow();
    TickHeadingWindow();
    TickRvsmWindow();
    TickTransferWindow();
    TickFreeTextWindow();
    TickCoordWindow();
    TickHot();

    if (m_hdgDragging)
    {
        const bool left = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        const bool right = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
        m_hdgReleaseTicks = left ? 0 : m_hdgReleaseTicks + 1;
        if (right || m_hdgReleaseTicks >= 3)
        {
            m_hdgDragging = false;
            m_hdgDragMoved = false;
            m_hdgDragCancelled = left;
            m_hdgDragEndTick = GetTickCount64();
            m_hdgReleaseTicks = 0;
            RequestRefresh();
        }
    }
    else
    {
        m_hdgReleaseTicks = 0;
        if (m_hdgDragCancelled && (GetAsyncKeyState(VK_LBUTTON) & 0x8000) == 0)
            m_hdgDragCancelled = false;
    }

    if (m_rulerButton != 0)
    {
        HWND fg = GetForegroundWindow();
        DWORD pid = 0;
        if (fg != NULL)
            GetWindowThreadProcessId(fg, &pid);
        if (pid != GetCurrentProcessId())
        {
            m_rulerButtonDown = false;
            return;
        }

        bool down = (GetAsyncKeyState(m_rulerButton) & 0x8000) != 0;
        if (down && !m_rulerButtonDown)
            m_rulerPressPending = true;
        m_rulerButtonDown = down;
    }

    if (m_rulerPressPending || m_rulerArmed || m_rulerPlacing)
        RequestRefresh();
}

bool CGalaxyATMSystemRadarScreen::CursorRadarPoint(POINT& out, HWND* view)
{
    POINT scr;
    if (!GetCursorPos(&scr))
        return false;

    HWND under = WindowFromPoint(scr);
    if (under == NULL)
        return false;

    DWORD pid = 0;
    GetWindowThreadProcessId(under, &pid);
    if (pid != GetCurrentProcessId())
        return false;

    RECT ra = GetRadarArea();
    for (HWND h : { under, GetAncestor(under, GA_ROOT) })
    {
        if (h == NULL)
            continue;
        POINT p = scr;
        if (!ScreenToClient(h, &p))
            continue;
        if (PtInRect(&ra, p))
        {
            out = p;
            if (view != NULL)
                *view = h;
            return true;
        }
    }
    return false;
}

void CGalaxyATMSystemRadarScreen::PlaceRulerPoint(POINT pt)
{
    if (!m_rulerPlacing)
    {
        m_rulerPending = RulerLine();
        std::string cs;
        m_rulerPending.startSnapped = FindNearbyTarget(pt, cs);
        if (m_rulerPending.startSnapped)
            m_rulerPending.startCallsign = cs;
        else
            m_rulerPending.startFixed = ConvertCoordFromPixelToPosition(pt);
        m_rulerPlacing = true;
        UpdateRulerEnd(pt);
        return;
    }

    UpdateRulerEnd(pt);

    POINT a = ConvertCoordFromPositionToPixel(ResolveRulerPoint(
        m_rulerPending.startSnapped, m_rulerPending.startCallsign, m_rulerPending.startFixed));
    POINT b = ConvertCoordFromPositionToPixel(ResolveRulerPoint(
        m_rulerPending.endSnapped, m_rulerPending.endCallsign, m_rulerPending.endFixed));
    if (abs(a.x - b.x) > 4 || abs(a.y - b.y) > 4)
        m_rulers.push_back(m_rulerPending);
    m_rulerPlacing = false;
    m_rulerArmed = false;
}

void CGalaxyATMSystemRadarScreen::DrawRulerCursor(HDC hDC)
{
    POINT pt;
    if (!CursorRadarPoint(pt))
        return;

    int saved = SaveDC(hDC);
    HPEN pen = CreatePen(PS_SOLID, (int)Theme::RulerWidth, Theme::Ruler);
    HPEN oldPen = (HPEN)SelectObject(hDC, pen);

    const int arm = 10, gap = 3;
    MoveToEx(hDC, pt.x - arm, pt.y, NULL);      LineTo(hDC, pt.x - gap, pt.y);
    MoveToEx(hDC, pt.x + gap + 1, pt.y, NULL);  LineTo(hDC, pt.x + arm + 1, pt.y);
    MoveToEx(hDC, pt.x, pt.y - arm, NULL);      LineTo(hDC, pt.x, pt.y - gap);
    MoveToEx(hDC, pt.x, pt.y + gap + 1, NULL);  LineTo(hDC, pt.x, pt.y + arm + 1);

    SelectObject(hDC, oldPen);
    DeleteObject(pen);
    RestoreDC(hDC, saved);
}

void CGalaxyATMSystemRadarScreen::UpdateRulerEnd(POINT pt)
{
    std::string cs;
    m_rulerPending.endSnapped = FindNearbyTarget(pt, cs);
    if (m_rulerPending.endSnapped)
        m_rulerPending.endCallsign = cs;
    else
        m_rulerPending.endFixed = ConvertCoordFromPixelToPosition(pt);
}

HFONT CGalaxyATMSystemRadarScreen::GetRulerFont(HFONT baseFont)
{
    if (baseFont == NULL)
        return NULL;
    if (m_rulerFont != NULL && m_rulerFontSource == baseFont)
        return m_rulerFont;

    LOGFONTW lf;
    if (GetObjectW(baseFont, sizeof(lf), &lf) == 0)
        return baseFont;

    lf.lfHeight = (LONG)lround(lf.lfHeight * 0.85);

    HFONT scaled = CreateFontIndirectW(&lf);
    if (scaled == NULL)
        return baseFont;

    if (m_rulerFont != NULL)
        DeleteObject(m_rulerFont);
    m_rulerFont = scaled;
    m_rulerFontSource = baseFont;
    return m_rulerFont;
}

void CGalaxyATMSystemRadarScreen::DrawRulerLine(HDC hDC, RulerLine& r, int index)
{
    CPosition startPos = ResolveRulerPoint(r.startSnapped, r.startCallsign, r.startFixed);
    CPosition endPos = ResolveRulerPoint(r.endSnapped, r.endCallsign, r.endFixed);

    POINT p0 = ConvertCoordFromPositionToPixel(startPos);
    POINT p1 = ConvertCoordFromPositionToPixel(endPos);

    double dx = p1.x - p0.x, dy = p1.y - p0.y;
    double len = sqrt(dx * dx + dy * dy);
    if (len < 2.0)
        return;

    int saved = SaveDC(hDC);
    SetBkMode(hDC, TRANSPARENT);

    int midX = (p0.x + p1.x) / 2, midY = (p0.y + p1.y) / 2;
    double ux = dx / len, uy = dy / len;
    double perpX = -uy, perpY = ux;
    if (perpY > 0) { perpX = -perpX; perpY = -perpY; }
    const double tickLen = 26.0;
    POINT tickTop = { midX + (int)(perpX * tickLen), midY + (int)(perpY * tickLen) };

    r.labelAnchor = tickTop;
    POINT labelAt = { tickTop.x + r.labelOffset.x, tickTop.y + r.labelOffset.y };

    double bearing = startPos.DirectionTo(endPos);
    bearing = fmod(bearing + 360.0, 360.0);
    double nm = startPos.DistanceTo(endPos);
    double km = nm * 1.852;
    double speedKt = GetRulerSpeedKt(r);

    wchar_t line1[16], line2[32], line3[16];
    swprintf_s(line1, L"%03d", ((int)(bearing + 0.5)) % 360);
    swprintf_s(line2, L"%.1f km(%.1f)", km, nm);
    if (speedKt > 0.1)
    {
        int totalSec = (int)((nm / speedKt) * 3600.0 + 0.5);
        swprintf_s(line3, L"%02d:%02d", totalSec / 60, totalSec % 60);
    }
    else
    {
        swprintf_s(line3, L"--:--");
    }

    HFONT rulerFont = GetRulerFont(m_esFont ? m_esFont : m_fonts.Ruler);

    int rowH = 15;
    {
        HFONT oldFont = (HFONT)SelectObject(hDC, rulerFont);
        TEXTMETRICW tm;
        if (GetTextMetricsW(hDC, &tm))
            rowH = tm.tmHeight + tm.tmExternalLeading;
        SelectObject(hDC, oldFont);
    }

    const wchar_t* const rowText[3] = { line1, line2, line3 };
    int rowW[3] = { 0, 0, 0 };
    int textW = 0;
    for (int i = 0; i < 3; i++)
    {
        rowW[i] = (int)Theme::MeasureText(hDC, rulerFont, rowText[i]).cx;
        textW = max(textW, rowW[i]);
    }

    int left = labelAt.x - 4;
    RECT r3 = { left, labelAt.y - rowH, left + 150, labelAt.y };
    RECT r2 = { left, r3.top - rowH, left + 150, r3.top };
    RECT r1 = { left, r2.top - rowH, left + 150, r2.top };
    r.labelRect = { left, r1.top, left + textW + 8, r3.bottom };

    POINT from = { midX, midY };
    RECT block = r.labelRect;
    InflateRect(&block, 2, 3);
    const LONG blockMidY = (block.top + block.bottom) / 2;
    const POINT aim = { (block.left + block.right) / 2, blockMidY };
    POINT leaderFrom, leaderTo;
    const bool drawLeader = ClipLeaderToText(from, aim, std::vector<RECT>(1, block), 0.0, leaderFrom, leaderTo);
    {
        VectorCanvas canvas(hDC, Theme::Ruler, Theme::RulerWidth);
        canvas.Line(p0.x, p0.y, p1.x, p1.y);
        if (drawLeader)
            canvas.Line(leaderFrom.x, leaderFrom.y, leaderTo.x, leaderTo.y);
    }

    Theme::DrawLine(hDC, r1, line1, rulerFont, Theme::Ruler, DT_LEFT | DT_VCENTER);
    Theme::DrawLine(hDC, r2, line2, rulerFont, Theme::Ruler, DT_LEFT | DT_VCENTER);
    Theme::DrawLine(hDC, r3, line3, rulerFont, Theme::Ruler, DT_LEFT | DT_VCENTER);

    if (index >= 0 && !m_rulerArmed && !m_rulerPlacing)
    {
        char id[16];
        sprintf_s(id, "%d", index);
        AddScreenObject(SO_RULER_LABEL, id, r.labelRect, true, Tr("Перетащить информацию линейки"));
    }

    RestoreDC(hDC, saved);
}

void CGalaxyATMSystemRadarScreen::RemoveRulerNear(POINT pt)
{
    int idx = FindNearestRulerIndex(pt, 15.0);
    if (idx >= 0)
        m_rulers.erase(m_rulers.begin() + idx);
    RequestRefresh();
}
