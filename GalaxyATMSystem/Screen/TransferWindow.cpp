#include "pch.h"
#include "Screen/ScreenCommon.h"

using namespace Galaxy;

void CGalaxyATMSystemRadarScreen::OpenTransferWindow(const char* callsign)
{
    CFlightPlan fp = GetPlugIn()->FlightPlanSelect(callsign);
    if (!fp.IsValid())
        return;

    m_xfrPositions.clear();
    const std::string me = GetPlugIn()->ControllerMyself().GetCallsign();
    const char* nextCs = fp.GetCoordinatedNextController();
    const std::string next = nextCs != NULL ? nextCs : "";
    for (CController c = GetPlugIn()->ControllerSelectFirst(); c.IsValid(); c = GetPlugIn()->ControllerSelectNext(c))
    {
        if (!c.IsController() || me == c.GetCallsign())
            continue;
        XfrPosition p = { c.GetPositionId(), c.GetCallsign() };
        if (p.positionId.empty())
            p.positionId = p.callsign;
        if (p.callsign == next)
            m_xfrPositions.insert(m_xfrPositions.begin(), p);
        else
            m_xfrPositions.push_back(p);
    }

    m_xfrOpen = true;
    m_xfrPlacement = PopupPlacement();
    m_xfrPicksRoutePoint = false;
    m_xfrCallsign = callsign;
    m_xfrTopRow = 0;
    m_xfrSelected = m_xfrPositions.empty() ? -1 : 0;
    m_xfrButtonsDown = true;
    m_xfrEntryPending = false;

    POINT cursor;
    HWND view = NULL;
    if (CursorRadarPoint(cursor, &view))
        m_popupView = view;
    UpdateWheelHook();
    RequestRefresh();
}

void CGalaxyATMSystemRadarScreen::OpenCopxWindow(const char* callsign)
{
    CFlightPlan fp = GetPlugIn()->FlightPlanSelect(callsign);
    if (!fp.IsValid())
        return;

    m_xfrPositions.clear();
    std::string current = AgreedCopx(fp);
    const char* entry = fp.GetEntryCoordinationPointName();
    if (TrackedByOther(fp) && entry != NULL && *entry != '\0')
        current = entry;
    int selected = -1;
    CFlightPlanExtractedRoute route = fp.GetExtractedRoute();
    for (int i = 0; i < route.GetPointsNumber(); i++)
    {
        const char* name = route.GetPointName(i);
        if (name == NULL || *name == '\0' || route.GetPointDistanceInMinutes(i) < 0)
            continue;
        if (!m_xfrPositions.empty() && m_xfrPositions.back().callsign == name)
            continue;
        if (current == name)
            selected = (int)m_xfrPositions.size();
        m_xfrPositions.push_back({ name, name });
    }

    m_xfrOpen = true;
    m_xfrPlacement = PopupPlacement();
    m_xfrPicksRoutePoint = true;
    m_xfrCallsign = callsign;
    m_xfrTopRow = 0;
    m_xfrSelected = selected >= 0 ? selected : (m_xfrPositions.empty() ? -1 : 0);
    m_xfrButtonsDown = true;
    m_xfrEntryPending = false;
    ScrollTransfer(m_xfrSelected - kXfrRows / 2);

    POINT cursor;
    HWND view = NULL;
    if (CursorRadarPoint(cursor, &view))
        m_popupView = view;
    UpdateWheelHook();
    RequestRefresh();
}

void CGalaxyATMSystemRadarScreen::CloseTransferWindow()
{
    m_xfrEntry.Close();
    m_xfrOpen = false;
    m_xfrEntryPending = false;
    m_xfrArea = { 0, 0, 0, 0 };
    UpdateWheelHook();
    RequestRefresh();
}

void CGalaxyATMSystemRadarScreen::ScrollTransfer(int rows)
{
    const int total = (int)m_xfrPositions.size();
    m_xfrTopRow = max(0, min(total - kXfrRows, m_xfrTopRow + rows));
    RequestRefresh();
}

void CGalaxyATMSystemRadarScreen::ApplyTransfer()
{
    std::string target;
    if (m_xfrSelected >= 0 && m_xfrSelected < (int)m_xfrPositions.size())
        target = m_xfrPositions[m_xfrSelected].callsign;

    if (m_xfrEntry.IsOpen())
    {
        std::string typed;
        for (wchar_t ch : m_xfrEntry.Text())
            if (ch > L' ' && ch < 0x80)
                typed += (char)toupper((int)ch);
        if (!typed.empty() && m_xfrPicksRoutePoint)
        {
            target = typed;
        }
        else if (!typed.empty())
        {
            target.clear();
            for (const XfrPosition& p : m_xfrPositions)
            {
                if (_stricmp(p.positionId.c_str(), typed.c_str()) == 0
                    || _stricmp(p.callsign.c_str(), typed.c_str()) == 0)
                {
                    target = p.callsign;
                    break;
                }
            }
            if (target.empty())
                Log::Warn("formular", m_xfrCallsign + ": no online position " + typed);
        }
    }

    CFlightPlan fp = GetPlugIn()->FlightPlanSelect(m_xfrCallsign.c_str());
    if (m_xfrPicksRoutePoint)
    {
        std::string owner;
        if (!fp.IsValid() || target.empty())
        {
            Log::Warn("formular", m_xfrCallsign + ": no point to coordinate DCT to");
        }
        else
        {
            PointDirectable(fp, target, &owner);
            SendCoordination(fp, target, 0, owner);
        }
    }
    else if (fp.IsValid() && !target.empty() && !fp.InitiateHandoff(target.c_str()))
        Log::Warn("formular", m_xfrCallsign + ": EuroScope refused handoff to " + target);
    CloseTransferWindow();
}

void CGalaxyATMSystemRadarScreen::DirectToTransferPoint(int index)
{
    if (index < 0 || index >= (int)m_xfrPositions.size())
        return;
    const std::string point = m_xfrPositions[index].callsign;
    CFlightPlan fp = GetPlugIn()->FlightPlanSelect(m_xfrCallsign.c_str());
    if (fp.IsValid() && fp.GetControllerAssignedData().SetDirectToPointName(point.c_str()))
        Log::Info("formular", m_xfrCallsign + ": DCT " + point);
    else
        CoordinationFailed(m_xfrCallsign, L"EuroScope \x043D\x0435 \x0432\x044B\x043F\x043E\x043B\x043D\x0438\x043B \x0441\x043F\x0440\x044F\x043C\x043B\x0435\x043D\x0438\x0435",
            "DCT " + point + " refused by EuroScope");
    CloseTransferWindow();
}

void CGalaxyATMSystemRadarScreen::ReleaseTransfer()
{
    CFlightPlan fp = GetPlugIn()->FlightPlanSelect(m_xfrCallsign.c_str());
    if (fp.IsValid() && !fp.EndTracking())
        Log::Warn("formular", m_xfrCallsign + ": EuroScope refused to release");
    CloseTransferWindow();
}

void CGalaxyATMSystemRadarScreen::TickTransferWindow()
{
    if (!m_xfrOpen)
        return;

    auto label = m_formulars.find(m_xfrCallsign);
    if (label == m_formulars.end() || label->second.items.empty())
    {
        CloseTransferWindow();
        return;
    }

    POINT cursor;
    const bool onRadar = CursorRadarPoint(cursor);
    TrackPopupActive(m_xfrPlacement, m_xfrArea, onRadar, cursor);
    const bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0
        || (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    if (down && !m_xfrButtonsDown)
    {
        const bool inside = onRadar
            && (PtInRect(&m_xfrArea, cursor) || PtInRect(&label->second.area, cursor));
        if (!inside && !m_xfrEntry.IsOpen())
        {
            m_xfrButtonsDown = down;
            CloseTransferWindow();
            return;
        }
    }
    m_xfrButtonsDown = down;

    if (m_xfrEntryPending && m_xfrDrawnTick > m_xfrPendingTick && m_popupView != NULL)
    {
        m_xfrEntryPending = false;
        m_xfrEntry.Open(m_popupView, m_xfrField, GetTransferFont(), L"", false, 10,
            [this](TextEntry::End end)
            {
                if (end == TextEntry::End::Cancel)
                {
                    m_xfrEntry.Close();
                    RequestRefresh();
                }
                else
                {
                    ApplyTransfer();
                }
            });
    }
}

HFONT CGalaxyATMSystemRadarScreen::GetTitleFont()
{
    const int size = Plugin()->TagFontSize();
    if (m_titleFont != NULL && m_titleFontSize == size)
        return m_titleFont;
    if (m_titleFont != NULL)
        DeleteObject(m_titleFont);
    LOGFONTW lf = {};
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    lf.lfHeight = -MulDiv(size, 7, 6);
    lf.lfWeight = FW_BOLD;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfOutPrecision = OUT_TT_PRECIS;
    lf.lfQuality = CLEARTYPE_QUALITY;
    m_titleFont = CreateFontIndirectW(&lf);
    m_titleFontSize = size;
    return m_titleFont;
}

HFONT CGalaxyATMSystemRadarScreen::GetTransferFont()
{
    const int size = Plugin()->TagFontSize();
    if (m_xfrFont != NULL && m_xfrFontSize == size)
        return m_xfrFont;
    if (m_xfrFont != NULL)
        DeleteObject(m_xfrFont);
    LOGFONTW lf = {};
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    lf.lfHeight = -MulDiv(size, 13, 10);
    lf.lfWeight = FW_BOLD;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfOutPrecision = OUT_TT_PRECIS;
    lf.lfQuality = CLEARTYPE_QUALITY;
    m_xfrFont = CreateFontIndirectW(&lf);
    m_xfrFontSize = size;
    return m_xfrFont;
}

void CGalaxyATMSystemRadarScreen::DrawTransferWindow(HDC hDC)
{
    auto label = m_formulars.find(m_xfrCallsign);
    if (label == m_formulars.end() || label->second.items.empty())
        return;

    int saved = SaveDC(hDC);
    SetBkMode(hDC, TRANSPARENT);
    SelectObject(hDC, GetFormularFont());
    TEXTMETRICW tm;
    GetTextMetricsW(hDC, &tm);
    const int labelLine = max(1, (int)(tm.tmHeight + tm.tmExternalLeading));

    const int R = labelLine * 37 / 20;
    const int width = R * 69 / 10;
    const int border = max(3, R / 5);
    const int line = max(2, R / 20);
    const int titleH = R * 5 / 6;
    const int pad = R / 4;
    const int fieldH = R * 5 / 6;
    const int listH = R * 8 - R / 20;
    const int listRight = R * 21 / 20;
    const int scrollW = R * 14 / 25;
    const int btnH = R * 4 / 5;
    const int height = titleH + R * 3 / 8 + fieldH + R * 3 / 10 + listH + R / 3
        + btnH + R * 3 / 10 + (m_xfrPicksRoutePoint ? 0 : btnH + R * 3 / 10) + R * 3 / 20 + border;

    const RECT& box = label->second.area;
    RECT ra = GetRadarArea();
    int left = box.right + 1;
    if (left + width > ra.right)
        left = box.left - 1 - width;
    const RECT area = PlacePopup(m_xfrPlacement, left, max(ra.top, min(box.top, ra.bottom - height)), width, height);
    m_xfrArea = area;
    AddScreenObject(SO_XFR_WINDOW, "XFR_WINDOW", area, true, "");

    auto fill = [&](const RECT& r, COLORREF color)
    {
        HBRUSH b = CreateSolidBrush(color);
        FillRect(hDC, &r, b);
        DeleteObject(b);
    };
    auto frame = [&](RECT r, COLORREF color, int thick)
    {
        HBRUSH b = CreateSolidBrush(color);
        for (int i = 0; i < thick; i++)
        {
            FrameRect(hDC, &r, b);
            InflateRect(&r, -1, -1);
        }
        DeleteObject(b);
    };
    auto text = [&](RECT r, const wchar_t* s, COLORREF color, UINT align)
    {
        SetTextColor(hDC, color);
        DrawTextW(hDC, s, -1, &r, align | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    };
    auto gradient = [&](const RECT& r, COLORREF topColor, COLORREF bottomColor)
    {
        TRIVERTEX v[2] = {
            { r.left, r.top, (COLOR16)(GetRValue(topColor) << 8), (COLOR16)(GetGValue(topColor) << 8),
              (COLOR16)(GetBValue(topColor) << 8), 0 },
            { r.right, r.bottom, (COLOR16)(GetRValue(bottomColor) << 8), (COLOR16)(GetGValue(bottomColor) << 8),
              (COLOR16)(GetBValue(bottomColor) << 8), 0 } };
        GRADIENT_RECT g = { 0, 1 };
        GradientFill(hDC, v, 2, &g, 1, GRADIENT_FILL_RECT_V);
    };

    fill(area, PopupFrame(m_xfrPlacement));
    RECT titleBar = { area.left, area.top, area.right, area.top + titleH };
    gradient(titleBar, m_xfrPlacement.active ? Theme::XfrTitleTop : Theme::XfrTitleTopInactive,
        PopupFrame(m_xfrPlacement));
    frame(area, Theme::XfrOutline, 1);
    RECT body = { area.left + border, area.top + titleH, area.right - border, area.bottom - border };
    fill(body, Theme::SpdBody);
    RECT bodyEdge = body;
    InflateRect(&bodyEdge, 1, 1);
    frame(bodyEdge, Theme::XfrOutline, 1);
    SelectObject(hDC, GetTitleFont());
    RECT title = { area.left + border, area.top, area.right - titleH, area.top + titleH };
    text(title, m_xfrPicksRoutePoint ? L"COPX" : L"\x041F\x0435\x0440\x0435\x0434\x0430\x0442\x044C", Theme::Text, DT_LEFT);
    RECT close = { area.right - titleH, area.top, area.right - border, area.top + titleH };
    text(close, L"\x00D7", Theme::Text, DT_CENTER);
    AddScreenObject(SO_XFR_CLOSE, "XFR_CLOSE", close, false, "");

    SelectObject(hDC, GetTransferFont());
    const int x0 = body.left + pad, x1 = body.right - pad;
    int y = body.top + R * 3 / 8;

    RECT field = { x0, y, x1, y + fieldH };
    fill(field, RGB(0x10, 0x10, 0x10));
    frame(field, Theme::SpdLine, line);
    m_xfrField = field;
    AddScreenObject(SO_XFR_FIELD, "XFR_FIELD", field, false, "");
    y += fieldH + R * 3 / 10;

    RECT listBox = { x0 + line, y, body.right - listRight, y + listH };
    fill(listBox, RGB(0x10, 0x10, 0x10));
    frame(listBox, Theme::SpdLine, line);
    RECT list = { listBox.left + line, listBox.top + line, listBox.right - line - scrollW, listBox.bottom - line };
    {
        int clip = SaveDC(hDC);
        IntersectClipRect(hDC, list.left, list.top, list.right, list.bottom);
        for (int k = 0; k < kXfrRows; k++)
        {
            const int idx = m_xfrTopRow + k;
            if (idx < 0 || idx >= (int)m_xfrPositions.size())
                continue;
            RECT row = { list.left, list.top + k * R, list.right, list.top + (k + 1) * R };
            const std::wstring id = Widen(m_xfrPositions[idx].positionId.c_str());
            if (idx == m_xfrSelected)
            {
                SIZE ts = {};
                GetTextExtentPoint32W(hDC, id.c_str(), (int)id.size(), &ts);
                RECT sel = { row.left, row.top + line, min(row.right, row.left + max(R * 17 / 10, (int)ts.cx + R / 2)),
                             row.bottom - line };
                fill(sel, Theme::XfrSelected);
            }
            RECT tr = { row.left + R / 6, row.top, row.right, row.bottom };
            text(tr, id.c_str(), Theme::Text, DT_LEFT);
            RECT hit;
            IntersectRect(&hit, &row, &list);
            char sid[8];
            sprintf_s(sid, "%d", idx);
            AddScreenObject(SO_XFR_ROW, sid, hit, false, "");
        }
        RestoreDC(hDC, clip);
    }
    RECT track = { list.right, list.top, listBox.right - line, list.bottom };
    frame(track, Theme::SpdLine, 1);
    RECT up = { track.left, track.top, track.right, track.top + scrollW };
    RECT down = { track.left, track.bottom - scrollW, track.right, track.bottom };
    for (const RECT* b : { &up, &down })
    {
        frame(*b, Theme::SpdLine, 1);
        const int m = max(2, scrollW / 6);
        RECT mark = { (b->left + b->right) / 2 - m, (b->top + b->bottom) / 2 - m,
                      (b->left + b->right) / 2 + m + 1, (b->top + b->bottom) / 2 + m + 1 };
        frame(mark, Theme::SpdLine, 1);
    }
    RECT inner = { track.left + 1, up.bottom, track.right - 1, down.top };
    m_xfrTrack = inner;
    const int total = (int)m_xfrPositions.size();
    const int innerH = inner.bottom - inner.top;
    if (innerH > 0)
    {
        RECT thumb = inner;
        if (total > kXfrRows)
        {
            const int thumbH = max(6, innerH * kXfrRows / total);
            thumb.top = inner.top + (innerH - thumbH) * m_xfrTopRow / (total - kXfrRows);
            thumb.bottom = thumb.top + thumbH;
        }
        fill(thumb, Theme::SpdThumb);
    }
    AddScreenObject(SO_XFR_TRACK, "XFR_TRACK", inner, false, "");
    AddScreenObject(SO_XFR_UP, "XFR_UP", up, false, "");
    AddScreenObject(SO_XFR_DOWN, "XFR_DOWN", down, false, "");
    y += listH + R / 3;

    SelectObject(hDC, GetSpeedFont());
    const wchar_t* labels[2] = { m_xfrPicksRoutePoint ? L"\x0421\x043E\x0433\x043B\x0430\x0441\x043E\x0432\x0430\x0442\x044C"
                                 : L"\x041F\x0435\x0440\x0435\x0434\x0430\x0442\x044C",
                                 L"\x041E\x0442\x0434\x0430\x0442\x044C" };
    const int shift = R / 6;
    for (int i = 0; i < (m_xfrPicksRoutePoint ? 1 : 2); i++)
    {
        RECT btn = { x0 + shift, y, x1 + shift, y + btnH };
        gradient(btn, Theme::XfrButtonTop, Theme::XfrButtonBottom);
        frame(btn, Theme::SpdLine, line);
        text(btn, labels[i], Theme::Text, DT_CENTER);
        AddScreenObject(i == 0 ? SO_XFR_HANDOFF : SO_XFR_RELEASE, i == 0 ? "XFR_HANDOFF" : "XFR_RELEASE",
            btn, false, "");
        y += btnH + R * 3 / 10;
    }

    RestoreDC(hDC, saved);
    m_xfrEntry.Move(field);
    m_xfrDrawnTick = GetTickCount64();
}
