#include "pch.h"
#include "Screen/ScreenCommon.h"

using namespace Galaxy;

const std::set<std::string>& CGalaxyATMSystemRadarScreen::KfConflicts()
{
    const ULONGLONG now = GetTickCount64();
    if (m_kfTick != 0 && now - m_kfTick < kKfRecheckMs)
        return m_kfConflicts;
    m_kfTick = now;
    m_kfConflicts.clear();
    m_ssaViolations.clear();

    struct KfTrack
    {
        std::string callsign;
        CPosition pos;
        double trackDeg, nmPerSec, ft, ftPerSec;
    };
    std::vector<KfTrack> airborne;
    for (CRadarTarget t = GetPlugIn()->RadarTargetSelectFirst(); t.IsValid();
         t = GetPlugIn()->RadarTargetSelectNext(t))
    {
        CRadarTargetPositionData p = t.GetPosition();
        if (!p.IsValid() || t.GetGS() < kKfMinGsKt)
            continue;
        airborne.push_back({ t.GetCallsign(), p.GetPosition(), t.GetTrackHeading(),
            t.GetGS() / 3600.0, (double)p.GetFlightLevel(), t.GetVerticalSpeed() / 60.0 });
    }

    for (size_t i = 0; i < airborne.size(); i++)
    {
        const KfTrack& self = airborne[i];
        for (size_t j = i + 1; j < airborne.size(); j++)
        {
            const KfTrack& other = airborne[j];
            if (fabs(self.pos.m_Latitude - other.pos.m_Latitude) * 60.0 > kKfScanNm
                || fabs(self.ft - other.ft) > kKfScanFt
                || self.pos.DistanceTo(other.pos) > kKfScanNm)
                continue;
            if (m_ssaViolations.count(self.callsign) && m_ssaViolations.count(other.callsign))
                continue;
            if (self.pos.DistanceTo(other.pos) < kKfLateralNm && fabs(self.ft - other.ft) < kKfVerticalFt)
            {
                for (const std::string* callsign : { &self.callsign, &other.callsign })
                {
                    m_ssaViolations.insert(*callsign);
                    m_kfConflicts.insert(*callsign);
                }
                continue;
            }
            if (m_kfConflicts.count(self.callsign) && m_kfConflicts.count(other.callsign))
                continue;

            for (int s = kKfStepSec; s <= kKfLookaheadSec; s += kKfStepSec)
            {
                CPosition a = CalculateDestinationPoint(self.pos, self.trackDeg, self.nmPerSec * s);
                CPosition b = CalculateDestinationPoint(other.pos, other.trackDeg, other.nmPerSec * s);
                double dv = (self.ft + self.ftPerSec * s) - (other.ft + other.ftPerSec * s);
                if (a.DistanceTo(b) < kKfLateralNm && fabs(dv) < kKfVerticalFt)
                {
                    m_kfConflicts.insert(self.callsign);
                    m_kfConflicts.insert(other.callsign);
                    break;
                }
            }
        }
    }
    return m_kfConflicts;
}

bool CGalaxyATMSystemRadarScreen::SeparationLost(const char* callsign)
{
    KfConflicts();
    return callsign != NULL && m_ssaViolations.count(callsign) != 0;
}

void CGalaxyATMSystemRadarScreen::BuildSectorList(std::vector<SectorListRow>& out)
{
    out.clear();

    SYSTEMTIME st;
    GetSystemTime(&st);
    const int nowMin = st.wHour * 60 + st.wMinute;

    auto hhmm = [nowMin](int minutesAhead) -> std::wstring
    {
        if (minutesAhead < 0)
            return L"----";
        int t = (nowMin + minutesAhead) % (24 * 60);
        wchar_t buf[8];
        swprintf_s(buf, L"%02d%02d", t / 60, t % 60);
        return buf;
    };
    auto level = [](int ft) -> std::wstring
    {
        if (ft <= 0)
            return L"------";
        wchar_t buf[8];
        swprintf_s(buf, L"F%03d", ft / 100);
        return buf;
    };

    const std::set<std::string>& conflicts = KfConflicts();

    for (CFlightPlan fp = GetPlugIn()->FlightPlanSelectFirst(); fp.IsValid();
         fp = GetPlugIn()->FlightPlanSelectNext(fp))
    {
        int state = fp.GetState();
        if (state == FLIGHT_PLAN_STATE_NON_CONCERNED || state == FLIGHT_PLAN_STATE_REDUNDANT)
            continue;

        const std::string callsign = fp.GetCallsign();
        const int entryMin = fp.GetSectorEntryMinutes();
        const ULONGLONG nowTick = GetTickCount64();
        if (entryMin >= 0)
            m_rcLastInSector[callsign] = nowTick;

        if (!m_rcFilterCallsign.empty()
            && Upper(Widen(callsign.c_str())).find(m_rcFilterCallsign) == std::wstring::npos)
            continue;
        if (m_rcFilterBefore >= 0 && entryMin > m_rcFilterBefore)
            continue;
        if (m_rcFilterAfter >= 0 && entryMin < 0)
        {
            auto seen = m_rcLastInSector.find(callsign);
            if (seen == m_rcLastInSector.end()
                || nowTick - seen->second > (ULONGLONG)m_rcFilterAfter * 60000)
                continue;
        }

        CFlightPlanData fpd = fp.GetFlightPlanData();
        CFlightPlanControllerAssignedData cad = fp.GetControllerAssignedData();

        SectorListRow row;
        row.callsign = fp.GetCallsign();
        row.mine = fp.GetTrackingControllerIsMe();
        row.crdState = fp.GetCoordinatedNextControllerState();

        CRadarTarget track = fp.GetCorrelatedRadarTarget();
        double course = -1.0;
        CFlightPlanExtractedRoute route = fp.GetExtractedRoute();
        const int points = route.GetPointsNumber();
        if (points >= 2)
        {
            CPosition from = route.GetPointPosition(0), to = route.GetPointPosition(points - 1);
            if (from.DistanceTo(to) > 10.0)
                course = from.DirectionTo(to);
        }
        if (course < 0.0 && track.IsValid() && track.GetGS() >= kKfMinGsKt)
            course = track.GetTrackHeading();
        row.east = course < 0.0 || fmod(course + 360.0, 360.0) < 180.0;

        row.cells[RC_CALLSIGN] = Widen(fp.GetCallsign());

        std::wstring assigned = Widen(cad.GetSquawk());
        std::wstring actual;
        CRadarTarget rt = fp.GetCorrelatedRadarTarget();
        if (rt.IsValid())
            actual = Widen(rt.GetPosition().GetSquawk());
        if (assigned.empty())
            assigned = actual;
        row.cells[RC_SQUAWK] = assigned;
        row.cells[RC_S] = (!assigned.empty() && !actual.empty() && assigned != actual) ? L"S" : L"";

        row.cells[RC_KF] = (rt.IsValid() && conflicts.count(rt.GetCallsign()) != 0) ? Tr(L"КФ") : L"";

        row.cells[RC_TYPE] = Widen(fpd.GetAircraftFPType());
        row.cells[RC_W] = fpd.IsRvsm() ? L"R" : L"";

        int cleared = fp.GetClearedAltitude();
        row.cells[RC_CFL] = level(cleared > 0 ? cleared : fp.GetFinalAltitude());

        row.cells[RC_ENTRY_POINT] = Widen(fp.GetEntryCoordinationPointName());
        row.cells[RC_ENTRY] = hhmm(fp.GetSectorEntryMinutes()) + L"/"
            + level(fp.GetEntryCoordinationAltitude());
        row.cells[RC_EXIT_POINT] = Widen(AgreedCopx(fp).c_str());
        row.cells[RC_EXIT] = hhmm(fp.GetSectorExitMinutes()) + L"/"
            + level(fp.GetExitCoordinationAltitude());

        int exitFt = fp.GetExitCoordinationAltitude();
        row.cells[RC_EXIT_LEVEL] = level(exitFt > 0 ? exitFt : fp.GetFinalAltitude());

        std::wstring pad = Upper(Widen(cad.GetScratchPadString()));
        row.cells[RC_PVO] = (pad.find(L"ПВО") != std::wstring::npos
                          || pad.find(L"PVO") != std::wstring::npos) ? L"+" : L"";

        row.cells[RC_CRD] = (row.crdState != COORDINATION_STATE_NONE) ? L"ACT" : L"";

        out.push_back(row);
    }

    const int cell = (m_rcSortKey >= 0 && m_rcSortKey < kRcCols) ? m_rcSortKey : RC_CALLSIGN;
    const bool asc = m_rcSortAsc;
    std::stable_sort(out.begin(), out.end(),
        [cell, asc](const SectorListRow& a, const SectorListRow& b)
        {
            std::wstring ka = RcSortText(a, cell), kb = RcSortText(b, cell);
            return asc ? (ka < kb) : (kb < ka);
        });
}

bool CGalaxyATMSystemRadarScreen::ScrollSectorList(int rows)
{
    if (!m_rcOpen)
        return false;

    POINT at;
    if (m_rcFloating)
    {
        if (!m_rcFloat.IsCreated() || !GetCursorPos(&at) || WindowFromPoint(at) != m_rcFloat.Handle())
            return false;
        ScreenToClient(m_rcFloat.Handle(), &at);
    }
    else if (!CursorRadarPoint(at))
        return false;

    for (int p = 0; p < 2; p++)
    {
        if (!PtInRect(&m_rcPane[p], at))
            continue;
        int& scroll = p ? m_rcScroll : m_rcScrollMine;
        scroll = max(0, min(m_rcPaneRows[p] - kRcRows, scroll + rows));
        if (m_rcFloating)
            RenderRcFloat();
        else
            RequestRefresh();
        return true;
    }
    return false;
}

int CGalaxyATMSystemRadarScreen::RcScale(int availW, int availH)
{
    const int fit = min(availW * 100 / kRcSvgW, availH * 100 / kRcSvgH);
    return max(kRcScaleMin, min(m_rcScale, min(kRcScaleMax, fit)));
}

void CGalaxyATMSystemRadarScreen::RcObject(int type, const char* id, RECT r, bool moveable, const char* tip)
{
    if (m_rcDrawingFloat)
        m_rcFloatHits.push_back({ type, id, r });
    else
        AddScreenObject(type, id, r, moveable, tip);
}

void CGalaxyATMSystemRadarScreen::DrawSectorListWindow(HDC hDC)
{
    std::vector<SectorListRow> all;
    BuildSectorList(all);

    RECT ra = GetRadarArea();
    const int scale = RcScale(ra.right - ra.left, ra.bottom - ra.top);
    const int W = (kRcSvgW * scale + 50) / 100, H = (kRcSvgH * scale + 50) / 100;

    if (!m_rcPositioned)
    {
        m_rcArea.left = ra.left + max(0, ((ra.right - ra.left) - W) / 2);
        m_rcArea.top  = ra.top + 60;
        m_rcPositioned = true;
    }

    if (m_rcArea.left + W > ra.right)
        m_rcArea.left = ra.right - W;
    if (m_rcArea.top + H > ra.bottom)
        m_rcArea.top = ra.bottom - H;
    if (m_rcArea.left < ra.left)
        m_rcArea.left = ra.left;
    if (m_rcArea.top < ra.top)
        m_rcArea.top = ra.top;

    m_rcArea.right  = m_rcArea.left + W;
    m_rcArea.bottom = m_rcArea.top + H;

    DrawSectorList(hDC, m_rcArea, scale, false, all);
}

void CGalaxyATMSystemRadarScreen::DrawSectorList(HDC hDC, RECT area, int scale, bool floating,
    const std::vector<SectorListRow>& all)
{
    std::vector<const SectorListRow*> other, mine;
    for (const SectorListRow& r : all)
        (r.mine ? mine : other).push_back(&r);

    auto S  = [scale](int svg) { return (svg * scale + 50) / 100; };
    auto SF = [scale](double svg) { return (float)(svg * scale / 100.0); };

    if (m_rcFontScale != scale)
    {
        for (HFONT f : { m_rcFont, m_rcHeadFont, m_rcRowFont, m_rcCellFont })
            if (f != NULL)
                DeleteObject(f);
        m_rcFont     = Theme::ListFont(max(6, S(32)), Theme::ListMedium);
        m_rcHeadFont = Theme::ListFont(max(6, S(32)), Theme::ListRegular);
        m_rcRowFont  = Theme::ListFont(max(6, S(24)), Theme::ListMedium);
        m_rcCellFont = Theme::ListFont(max(6, S(24)), Theme::ListBold);
        m_rcFontScale = scale;
    }

    m_rcDrawingFloat = floating;
    if (floating)
        m_rcFloatHits.clear();

    const int ox = area.left, oy = area.top;
    auto X = [ox, &S](int svg) { return ox + S(svg); };
    auto Y = [oy, &S](int svg) { return oy + S(svg); };

    int saved = SaveDC(hDC);
    SetBkMode(hDC, TRANSPARENT);

    if (floating)
        Theme::FlatFill(hDC, area, Theme::ListGround);
    else
        FillAlpha(hDC, area, Theme::ListGround, Theme::ListGroundAlpha);

    RECT bar = { X(10), Y(20), X(2058), Y(82) };
    {
        Gdiplus::Graphics g(hDC);
        g.SetSmoothingMode(Theme::Smoothing());
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

        const Gdiplus::REAL l = (Gdiplus::REAL)bar.left, r = (Gdiplus::REAL)bar.right;
        const Gdiplus::REAL t = (Gdiplus::REAL)bar.top, b = (Gdiplus::REAL)bar.bottom;
        const Gdiplus::REAL d = SF(20) * 2;

        Gdiplus::GraphicsPath shape;
        shape.AddArc(l, t, d, d, 180.0f, 90.0f);
        shape.AddArc(r - d, t, d, d, 270.0f, 90.0f);
        shape.AddLine(r, b, l, b);
        shape.CloseFigure();

        COLORREF fill = Theme::ListTitleFill, ink = Theme::ListTitleText;
        Gdiplus::SolidBrush fillBrush(Gdiplus::Color(GetRValue(fill), GetGValue(fill), GetBValue(fill)));
        g.FillPath(&fillBrush, &shape);

        Gdiplus::PointF cross[12];
        for (int i = 0; i < 12; i++)
            cross[i] = Gdiplus::PointF(ox + SF(kRcCrossSvg[i][0]), oy + SF(kRcCrossSvg[i][1]));
        Gdiplus::SolidBrush inkBrush(Gdiplus::Color(GetRValue(ink), GetGValue(ink), GetBValue(ink)));
        g.FillPolygon(&inkBrush, cross, 12);
    }

    const std::wstring caption = Tr(L"Список РЦ");
    RECT captionR = { X(52), bar.top, X(1990), bar.bottom };
    Theme::DrawLine(hDC, captionR, caption, m_rcFont, Theme::ListTitleText,
        DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);

    RECT close = { X(1998), bar.top, X(2046), bar.bottom };

    if (!Theme::InterInstalled())
    {
        const int captionRight = captionR.left + Theme::MeasureText(hDC, m_rcFont, caption).cx;
        RECT note = { captionRight + S(60), bar.top, close.left - S(30), bar.bottom };
        Theme::DrawLine(hDC, note, Tr(L"Шрифт Inter не установлен"), m_rcRowFont, Theme::SquawkMismatch,
            DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
    }
    RECT drag = { area.left, area.top, close.left, bar.bottom };
    RcObject(SO_RC_HEADER, "RC_HEADER", drag, true, Tr("Перетащите список РЦ"));
    RcObject(SO_RC_CLOSE, "RC_CLOSE", close, false, Tr("Закрыть"));

    for (int p = 0; p < 2; p++)
    {
        const std::vector<const SectorListRow*>& rows = p ? other : mine;
        const int count = (int)rows.size();
        const bool paged = count > kRcRows;

        int& scroll = p ? m_rcScroll : m_rcScrollMine;
        if (scroll >= count)
            scroll = 0;
        scroll = max(0, min(scroll, count - kRcRows));

        const int top = kRcPaneTopSvg[p];
        RECT pane = { X(10), Y(top), X(2058), Y(kRcPaneBottomSvg[p]) };
        Theme::FlatFill(hDC, pane, Theme::ListPaneFill);
        m_rcPane[p] = pane;
        m_rcPaneRows[p] = count;

        RECT band = { X(10), Y(top), X(2058), Y(top + kRcHeadSvg) };
        Theme::FlatFill(hDC, band, Theme::ListHeadRule);
        for (int c = 0; c < kRcCols; c++)
        {
            RECT plate = { X(kRcColumns[c].svgLeft), Y(top + kRcPlateInsetSvg),
                           X(kRcColumns[c].svgRight), Y(top + kRcHeadSvg - kRcPlateInsetSvg) };
            Theme::FlatFill(hDC, plate, Theme::ListHeadFill);
            Theme::DrawLine(hDC, plate, Tr(kRcColumns[c].title), p ? m_rcFont : m_rcHeadFont, Theme::ListHeadText,
                DT_CENTER | DT_VCENTER | DT_END_ELLIPSIS);

            char id[8];
            sprintf_s(id, "%d", c);
            RcObject(SO_RC_SORT, id, plate, false, Tr("Сортировать по столбцу"));
        }

        for (int i = 0; i < kRcRows; i++)
        {
            int index = scroll + i;
            if (index >= (int)rows.size())
                break;
            const SectorListRow& row = *rows[index];

            const int rowTop = top + kRcHeadSvg + kRcPlateInsetSvg + kRcRowPitchSvg * i;
            RECT line = { X(10), Y(rowTop), X(2058), Y(rowTop + kRcRowSvg) };
            Theme::FlatFill(hDC, line, m_rcPicked.count(row.callsign) ? Theme::FormularPicked
                : row.east ? Theme::ListRowEast : Theme::ListRowWest);

            for (float d : kRcDividerSvg)
            {
                const int l = ox + (int)(SF(d) + 0.5f);
                RECT rule = { l, line.top, l + max(1, (int)(SF(kRcDividerWSvg) + 0.5f)), line.bottom };
                Theme::FlatFill(hDC, rule, Theme::ListRowRule);
            }

            for (int c = 0; c < kRcCols; c++)
            {
                COLORREF ink = Theme::ListText;
                if (c == RC_KF && !row.cells[c].empty())
                {
                    ink = Theme::ListConflict;
                }
                else if (c == RC_CRD && !row.cells[c].empty())
                {
                    ink = (row.crdState == COORDINATION_STATE_ACCEPTED
                        || row.crdState == COORDINATION_STATE_MANUAL_ACCEPTED)
                        ? Theme::ListCrdOk : Theme::ListCrdReq;
                }

                RECT cell = { X(kRcColumns[c].svgLeft) + 1, line.top,
                              X(kRcColumns[c].svgRight) - 1, line.bottom };
                Theme::DrawLine(hDC, cell, row.cells[c], m_rcCellFont, ink,
                    DT_CENTER | DT_VCENTER | DT_END_ELLIPSIS);

                if (c == RC_CALLSIGN)
                    RcObject(SO_RC_ROW, row.callsign.c_str(), cell, false,
                        paged ? Tr("ЛКМ - выделить формуляр (ПКМ - следующая страница)")
                              : Tr("ЛКМ - выделить формуляр"));
                else if (c == RC_SQUAWK)
                    RcObject(SO_RC_SQUAWK, row.callsign.c_str(), cell, false, Tr("Код ответчика"));
                else if (c == RC_EXIT_LEVEL)
                    RcObject(SO_RC_XFL, row.callsign.c_str(), cell, false, Tr("Выходной эшелон"));
            }
        }
    }

    {
        auto field = [&](int svgX, int svgY, const std::wstring& text, const char* id, const char* tip) -> RECT
        {
            const int fw = max(1, S(2));
            RECT r = { X(svgX) - fw / 2, Y(svgY) - fw / 2, X(svgX + 182) + (fw + 1) / 2, Y(svgY + 54) + (fw + 1) / 2 };
            Theme::FlatFill(hDC, r, Theme::ListPaneFill);
            Theme::FlatFrame(hDC, r, fw, Theme::ListFieldFrame);
            RECT inner = { r.left + S(12), r.top, r.right - S(8), r.bottom };
            Theme::DrawLine(hDC, inner, text, m_rcRowFont, Theme::ListTitleText,
                DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
            RcObject(SO_RC_FILTER, id, r, false, tip);
            return r;
        };
        auto label = [&](const RECT& f, int svgEdge, bool rightAligned, const std::wstring& text)
        {
            RECT r = rightAligned ? RECT{ area.left, f.top, X(svgEdge), f.bottom }
                                  : RECT{ X(svgEdge), f.top, f.left, f.bottom };
            Theme::DrawLine(hDC, r, text, m_rcFont, Theme::ListTitleText,
                (rightAligned ? DT_RIGHT : DT_LEFT) | DT_VCENTER);
        };
        auto minutes = [](int value) { return value >= 0 ? std::to_wstring(value) : std::wstring(); };

        RECT callsign = field(120, 825, m_rcFilterCallsign, "callsign", Tr("Фильтр по рейсу"));
        label(callsign, 13, false, Tr(L"Рейс:"));

        RECT before = field(1467, 829, minutes(m_rcFilterBefore), "before",
            Tr("За сколько минут до входа в сектор показывать рейс"));
        label(before, 1453, true, Tr(L"До (мин)"));

        RECT after = field(1865, 829, minutes(m_rcFilterAfter), "after",
            Tr("Сколько минут держать рейс после выхода из сектора"));
        label(after, 1856, true, Tr(L"После (мин)"));
    }

    const int g = max(8, S(20));
    RECT grip = { area.right - g, area.bottom - g, area.right, area.bottom };
    {
        HPEN pen = CreatePen(PS_SOLID, 1, Theme::ListHeadRule);
        HPEN old = (HPEN)SelectObject(hDC, pen);
        for (int i = 1; i <= 3; i++)
        {
            const int d = (g - 2) * i / 3;
            MoveToEx(hDC, grip.right - 2 - d, grip.bottom - 2, NULL);
            LineTo(hDC, grip.right - 2, grip.bottom - 2 - d);
        }
        SelectObject(hDC, old);
        DeleteObject(pen);
    }
    RcObject(SO_RC_RESIZE, "RC_RESIZE", grip, true, Tr("Потяните, чтобы изменить размер"));

    m_rcDrawingFloat = false;
    RestoreDC(hDC, saved);
}

namespace
{
    HWND EuroScopeMainWindow()
    {
        struct Found { HWND hwnd; LONG area; } found = { NULL, 0 };
        EnumWindows([](HWND h, LPARAM lp) -> BOOL
            {
                DWORD pid = 0;
                GetWindowThreadProcessId(h, &pid);
                if (pid != GetCurrentProcessId() || !IsWindowVisible(h) || GetWindow(h, GW_OWNER) != NULL)
                    return TRUE;
                RECT r;
                GetWindowRect(h, &r);
                Found* f = (Found*)lp;
                const LONG area = (r.right - r.left) * (r.bottom - r.top);
                if (area > f->area)
                    *f = { h, area };
                return TRUE;
            }, (LPARAM)&found);
        return found.hwnd;
    }
}

bool CGalaxyATMSystemRadarScreen::CreateRcFloat(HWND owner)
{
    if (m_rcFloat.IsCreated())
        return true;
    if (owner == NULL || !m_rcFloat.Create(owner))
        return false;
    m_rcFloat.onHitTest = [this](POINT pt)
    {
        for (const RcHit& h : m_rcFloatHits)
            if (h.type == SO_RC_HEADER && PtInRect(&h.rect, pt))
                return FloatWindow::Hit::Caption;
        return FloatWindow::Hit::Client;
    };
    m_rcFloat.onMouse = [this](UINT msg, POINT pt) { RcFloatMouse(msg, pt); };
    m_rcFloat.onMoveStart = [this]() { m_rcEntry.Close(); };
    m_rcFloat.onMoved = [this]() { RcFloatMoved(); };
    return true;
}

bool CGalaxyATMSystemRadarScreen::UndockSectorList(RECT requested)
{
    HWND view = m_rcDragView;
    if (view == NULL || !IsWindow(view))
        return false;
    if (!CreateRcFloat(GetAncestor(view, GA_ROOT)))
        return false;

    const RECT ra = GetRadarArea();
    m_rcScale = RcScale(ra.right - ra.left, ra.bottom - ra.top);

    POINT at = { requested.left, requested.top };
    ClientToScreen(view, &at);
    m_rcFloatPos = at;
    m_rcFloating = true;
    m_rcFloat.MoveTo(at);
    RenderRcFloat();
    m_rcFloat.ContinueDrag();

    m_dragOffset = { 0, 0 };
    RequestRefresh();
    Log::Info("rc", "sector list taken off the radar");
    return true;
}

void CGalaxyATMSystemRadarScreen::RenderRcFloat()
{
    if (!m_rcFloat.IsCreated())
    {
        if (!CreateRcFloat(EuroScopeMainWindow()))
            return;
        if (MonitorFromPoint(m_rcFloatPos, MONITOR_DEFAULTTONULL) == NULL)
            m_rcFloatPos = { 100, 100 };
        m_rcFloat.MoveTo(m_rcFloatPos);
    }

    std::vector<SectorListRow> all;
    BuildSectorList(all);

    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(MonitorFromWindow(m_rcFloat.Handle(), MONITOR_DEFAULTTONEAREST), &mi);
    const int scale = RcScale(mi.rcWork.right - mi.rcWork.left, mi.rcWork.bottom - mi.rcWork.top);
    const int W = (kRcSvgW * scale + 50) / 100, H = (kRcSvgH * scale + 50) / 100;

    HDC dc = m_rcFloat.BeginFrame(W, H);
    if (dc == NULL)
        return;
    RECT area = { 0, 0, W, H };
    DrawSectorList(dc, area, scale, true, all);
    m_rcFloat.EndFrame();
    m_rcFloatDrawn = GetTickCount64();
}

void CGalaxyATMSystemRadarScreen::RcFloatMouse(UINT msg, POINT pt)
{
    if (m_rcFloatResizing)
    {
        const int width = pt.x + m_rcFloatGrab;
        m_rcScale = max(kRcScaleMin, min(kRcScaleMax, (int)lround(width * 100.0 / kRcSvgW)));
        if (msg == WM_LBUTTONUP)
        {
            m_rcFloatResizing = false;
            m_rcFloat.ReleaseMouse();
        }
        RenderRcFloat();
        return;
    }
    if (msg == WM_MOUSEMOVE)
        return;

    const RcHit* hit = NULL;
    for (auto it = m_rcFloatHits.rbegin(); it != m_rcFloatHits.rend(); ++it)
    {
        if (PtInRect(&it->rect, pt))
        {
            hit = &*it;
            break;
        }
    }
    if (hit == NULL)
        return;

    if (msg == WM_LBUTTONDOWN && hit->type == SO_RC_RESIZE)
    {
        RECT r;
        GetClientRect(m_rcFloat.Handle(), &r);
        m_rcFloatResizing = true;
        m_rcFloatGrab = r.right - pt.x;
        m_rcEntry.Close();
        m_rcFloat.CaptureMouse();
        return;
    }
    if (msg != WM_LBUTTONUP && msg != WM_RBUTTONUP)
        return;
    const int button = (msg == WM_LBUTTONUP) ? BUTTON_LEFT : BUTTON_RIGHT;

    const RcHit h = *hit;
    m_rcEntry.Close();
    if (h.type == SO_RC_FILTER)
    {
        if (button != BUTTON_LEFT)
            return;
        const bool isCallsign = h.id == "callsign";
        const bool isBefore = h.id == "before";
        const int fn = isCallsign ? FN_RC_FILTER_CALLSIGN : isBefore ? FN_RC_FILTER_BEFORE : FN_RC_FILTER_AFTER;
        std::wstring initial = m_rcFilterCallsign;
        if (!isCallsign)
        {
            const int value = isBefore ? m_rcFilterBefore : m_rcFilterAfter;
            initial = value >= 0 ? std::to_wstring(value) : std::wstring();
        }
        m_rcEntry.Open(m_rcFloat.Handle(), h.rect, m_rcRowFont, initial, false, isCallsign ? 10 : 4,
            [this, fn](TextEntry::End end)
            {
                if (end != TextEntry::End::Cancel)
                    ApplyRcFilter(fn, m_rcEntry.Text());
                m_rcEntry.Close();
                RenderRcFloat();
            });
        return;
    }
    if (h.type == SO_RC_CLOSE)
    {
        m_rcOpen = false;
        m_rcFloat.Hide();
        RequestRefresh();
        return;
    }

    RECT screenArea = h.rect;
    m_rcClickFromFloat = true;
    OnClickScreenObject(h.type, h.id.c_str(), pt, screenArea, button);
    m_rcClickFromFloat = false;
    RenderRcFloat();
}

void CGalaxyATMSystemRadarScreen::RcFloatMoved()
{
    RECT wr;
    GetWindowRect(m_rcFloat.Handle(), &wr);
    m_rcFloatPos = { wr.left, wr.top };

    HWND owner = GetWindow(m_rcFloat.Handle(), GW_OWNER);
    if (owner == NULL || IsIconic(owner))
        return;

    const POINT mid = { (wr.left + wr.right) / 2, (wr.top + wr.bottom) / 2 };
    std::vector<HWND> chain;
    for (HWND h = owner; h != NULL && chain.size() < 16; )
    {
        chain.push_back(h);
        POINT p = mid;
        ScreenToClient(h, &p);
        HWND child = ChildWindowFromPointEx(h, p, CWP_SKIPINVISIBLE | CWP_SKIPTRANSPARENT);
        h = (child == h) ? NULL : child;
    }

    const RECT ra = GetRadarArea();
    for (auto it = chain.rbegin(); it != chain.rend(); ++it)
    {
        POINT tl = { wr.left, wr.top }, br = { wr.right, wr.bottom };
        ScreenToClient(*it, &tl);
        ScreenToClient(*it, &br);
        if (tl.x >= ra.left && tl.y >= ra.top && br.x <= ra.right && br.y <= ra.bottom)
        {
            m_rcArea = { tl.x, tl.y, br.x, br.y };
            m_rcPositioned = true;
            m_rcFloating = false;
            m_rcEntry.Close();
            m_rcFloat.Hide();
            RequestRefresh();
            Log::Info("rc", "sector list put back on the radar");
            return;
        }
    }
}

void CGalaxyATMSystemRadarScreen::TickRcFloat()
{
    if (m_rcFloat.Visible() && !m_rcFloatResizing && GetTickCount64() - m_rcFloatDrawn > 3000)
    {
        m_rcEntry.Close();
        m_rcFloat.Hide();
    }
}

void CGalaxyATMSystemRadarScreen::ApplyRcFilter(int functionId, const std::wstring& typed)
{
    if (functionId == FN_RC_FILTER_CALLSIGN)
    {
        std::wstring callsign;
        for (wchar_t c : typed)
            if (!iswspace(c) && callsign.size() < 10)
                callsign += c;
        m_rcFilterCallsign = Upper(callsign);
    }
    else
    {
        std::wstring digits = typed;
        digits.erase(0, digits.find_first_not_of(L" \t"));
        digits.erase(digits.find_last_not_of(L" \t") + 1);

        int value = -1;
        if (!digits.empty())
        {
            if (digits.size() > 4 || digits.find_first_not_of(L"0123456789") != std::wstring::npos)
                return;
            value = _wtoi(digits.c_str());
        }
        (functionId == FN_RC_FILTER_BEFORE ? m_rcFilterBefore : m_rcFilterAfter) = value;
    }
    m_rcScroll = m_rcScrollMine = 0;
    RequestRefresh();
}
