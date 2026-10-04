#include "pch.h"
#include "Screen/ScreenCommon.h"

using namespace Galaxy;

void CGalaxyATMSystemRadarScreen::OpenSpeedWindow(const char* callsign)
{
    CFlightPlan fp = GetPlugIn()->FlightPlanSelect(callsign);
    if (!fp.IsValid())
        return;
    CFlightPlanControllerAssignedData cad = fp.GetControllerAssignedData();

    m_spdOpen = true;
    m_spdPlacement = PopupPlacement();
    m_spdCallsign = callsign;
    m_spdButtonsDown = true;
    m_spdEntryPending = false;
    const char modifier = SpeedModifierOf(Plugin(), fp);
    m_spdMode = modifier == '+' ? SpeedOrGreater : modifier == '-' ? SpeedOrLess : SpeedExact;

    int mach = cad.GetAssignedMach();
    if (mach > 100)
        mach /= 10;
    if (mach > 0)
    {
        m_spdMach = true;
        m_spdSelected = mach;
    }
    else
    {
        m_spdMach = false;
        m_spdSelected = cad.GetAssignedSpeed();
        if (m_spdSelected <= 0)
        {
            int ias = 0, machX100 = 0;
            CRadarTarget rt = fp.GetCorrelatedRadarTarget();
            if (rt.IsValid() && CalculatedIasMach(rt.GetGS(), rt.GetPosition().GetFlightLevel(), ias, machX100))
                m_spdSelected = (ias + 5) / 10 * 10;
            else
                m_spdSelected = 250;
        }
    }
    SelectSpeedTab(m_spdMach);

    POINT cursor;
    HWND view = NULL;
    if (CursorRadarPoint(cursor, &view))
        m_popupView = view;
    UpdateWheelHook();
    RequestRefresh();
}

void CGalaxyATMSystemRadarScreen::CloseSpeedWindow()
{
    m_spdEntry.Close();
    m_spdOpen = false;
    m_spdEntryPending = false;
    m_spdArea = { 0, 0, 0, 0 };
    UpdateWheelHook();
    RequestRefresh();
}

void CGalaxyATMSystemRadarScreen::SelectSpeedTab(bool mach)
{
    const std::vector<int>& values = SpeedValues(mach);
    if (mach != m_spdMach)
        m_spdSelected = mach ? 78 : 250;
    m_spdMach = mach;
    size_t best = 0;
    for (size_t i = 0; i < values.size(); i++)
        if (abs(values[i] - m_spdSelected) < abs(values[best] - m_spdSelected))
            best = i;
    m_spdSelected = values[best];
    m_spdTopRow = 0;
    ScrollSpeed((int)best);
    RequestRefresh();
}

void CGalaxyATMSystemRadarScreen::ScrollSpeed(int rows)
{
    const int total = (int)SpeedValues(m_spdMach).size();
    m_spdTopRow = max(0, min(total - kSpdRows, m_spdTopRow + rows));
    RequestRefresh();
}

void CGalaxyATMSystemRadarScreen::ApplySpeed()
{
    bool mach = m_spdMach;
    int value = m_spdSelected;

    if (m_spdEntry.IsOpen())
    {
        const std::wstring text = m_spdEntry.Text();
        int v = 0, digits = 0;
        bool m = mach, typed = false;
        for (wchar_t ch : text)
        {
            if (ch == L'M' || ch == L'm' || ch == L'.')
                m = true;
            else if (ch == L'N' || ch == L'n' || ch == L'K' || ch == L'k')
                m = false;
            else if (ch >= L'0' && ch <= L'9')
            {
                v = v * 10 + (ch - L'0');
                digits++;
                typed = true;
            }
        }
        if (typed && digits <= 3)
        {
            mach = m;
            value = v;
        }
    }

    CFlightPlan fp = GetPlugIn()->FlightPlanSelect(m_spdCallsign.c_str());
    if (fp.IsValid() && value > 0)
    {
        CFlightPlanControllerAssignedData cad = fp.GetControllerAssignedData();
        bool ok;
        if (mach)
        {
            if (cad.GetAssignedSpeed() > 0)
                cad.SetAssignedSpeed(0);
            ok = cad.SetAssignedMach(value);
        }
        else
        {
            if (cad.GetAssignedMach() > 0)
                cad.SetAssignedMach(0);
            ok = cad.SetAssignedSpeed(value);
        }
        if (!ok)
            Log::Warn("formular", m_spdCallsign + ": EuroScope refused speed " + std::to_string(value));

        const char modifier = m_spdMode == SpeedOrGreater ? '+' : m_spdMode == SpeedOrLess ? '-' : 0;
        const char* annotation = cad.GetFlightStripAnnotation(kTopSkySpeedAnnotation);
        const std::string updated = WithSpeedModifier(annotation, modifier);
        if (updated != (annotation != NULL ? annotation : ""))
            cad.SetFlightStripAnnotation(kTopSkySpeedAnnotation, updated.c_str());
        if (ok)
            Plugin()->ShareSpeedModifier(fp, modifier);
    }
    CloseSpeedWindow();
}

void CGalaxyATMSystemRadarScreen::TickSpeedWindow()
{
    if (!m_spdOpen)
        return;

    auto label = m_formulars.find(m_spdCallsign);
    if (label == m_formulars.end() || label->second.items.empty())
    {
        CloseSpeedWindow();
        return;
    }

    POINT cursor;
    const bool onRadar = CursorRadarPoint(cursor);
    TrackPopupActive(m_spdPlacement, m_spdArea, onRadar, cursor);
    const bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0
        || (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    if (down && !m_spdButtonsDown)
    {
        const bool inside = onRadar
            && (PtInRect(&m_spdArea, cursor) || PtInRect(&label->second.area, cursor));
        if (!inside && !m_spdEntry.IsOpen())
        {
            m_spdButtonsDown = down;
            CloseSpeedWindow();
            return;
        }
    }
    m_spdButtonsDown = down;

    if (m_spdEntryPending && m_spdDrawnTick > m_spdPendingTick && m_popupView != NULL)
    {
        m_spdEntryPending = false;
        m_spdEntry.Open(m_popupView, m_spdField, GetSpeedFont(), L"", false, 5,
            [this](TextEntry::End end)
            {
                if (end == TextEntry::End::Cancel)
                {
                    m_spdEntry.Close();
                    RequestRefresh();
                }
                else
                {
                    ApplySpeed();
                }
            });
    }
}

HFONT CGalaxyATMSystemRadarScreen::GetSpeedFont()
{
    const int size = Plugin()->TagFontSize();
    if (m_spdFont != NULL && m_spdFontSize == size)
        return m_spdFont;
    if (m_spdFont != NULL)
        DeleteObject(m_spdFont);
    LOGFONTW lf = {};
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    lf.lfHeight = -MulDiv(size, 7, 6);
    lf.lfWeight = FW_SEMIBOLD;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfOutPrecision = OUT_TT_PRECIS;
    lf.lfQuality = CLEARTYPE_QUALITY;
    m_spdFont = CreateFontIndirectW(&lf);
    m_spdFontSize = size;
    return m_spdFont;
}

void CGalaxyATMSystemRadarScreen::DrawSpeedWindow(HDC hDC)
{
    auto label = m_formulars.find(m_spdCallsign);
    if (label == m_formulars.end() || label->second.items.empty())
        return;

    int saved = SaveDC(hDC);
    SetBkMode(hDC, TRANSPARENT);
    SelectObject(hDC, GetSpeedFont());
    TEXTMETRICW tm;
    GetTextMetricsW(hDC, &tm);
    const int lineH = max(1, (int)(tm.tmHeight + tm.tmExternalLeading));

    const int rowH = lineH * 7 / 5;
    const int border = max(3, rowH / 9);
    const int line = max(1, rowH / 25);
    const int width = rowH * 13 / 2;
    const int pad = rowH * 2 / 5;
    const int titleH = rowH * 9 / 8;
    const int gap = rowH / 5;
    const int listH = kSpdRows * rowH + rowH / 2;
    const int height = titleH + gap + rowH + gap + rowH + gap + listH + gap + rowH + gap
        + 3 * rowH + gap + rowH + gap + rowH + gap + border;

    const RECT& box = label->second.area;
    RECT ra = GetRadarArea();
    int left = box.right + 1;
    if (left + width > ra.right)
        left = box.left - 1 - width;
    const RECT area = PlacePopup(m_spdPlacement, left, max(ra.top, min(box.top, ra.bottom - height)), width, height);
    m_spdArea = area;
    AddScreenObject(SO_SPD_WINDOW, "SPD_WINDOW", area, true, "");

    auto fill = [&](const RECT& r, COLORREF color)
    {
        HBRUSH b = CreateSolidBrush(color);
        FillRect(hDC, &r, b);
        DeleteObject(b);
    };
    auto frame = [&](RECT r, COLORREF color)
    {
        HBRUSH b = CreateSolidBrush(color);
        for (int i = 0; i < line; i++)
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
    HPEN linePen = CreatePen(PS_SOLID, line, Theme::SpdLine);

    fill(area, PopupFrame(m_spdPlacement));
    RECT body = { area.left + border, area.top + titleH, area.right - border, area.bottom - border };
    fill(body, Theme::SpdBody);
    RECT title = { area.left + border * 2, area.top, area.right - titleH, area.top + titleH };
    text(title, L"Speed", Theme::Text, DT_LEFT);
    RECT close = { area.right - titleH, area.top, area.right - border, area.top + titleH };
    text(close, L"\x00D7", Theme::Text, DT_CENTER);
    AddScreenObject(SO_SPD_CLOSE, "SPD_CLOSE", close, false, "");

    const int x0 = body.left + pad, x1 = body.right - pad;
    const int mid = (x0 + x1) / 2;
    int y = body.top + gap;

    RECT cs = { x0, y, x1, y + rowH };
    const std::wstring callsign = Widen(m_spdCallsign.c_str());
    text(cs, callsign.c_str(), Theme::Text, DT_CENTER);
    y += rowH + gap;

    RECT field = { x0, y, x1, y + rowH };
    fill(field, RGB(0, 0, 0));
    frame(field, Theme::SpdLine);
    m_spdField = field;
    AddScreenObject(SO_SPD_FIELD, "SPD_FIELD", field, false, "");
    y += rowH + gap;

    const int scrollW = max(12, rowH * 11 / 20);
    RECT listBox = { x0, y, x1, y + listH };
    fill(listBox, RGB(0, 0, 0));
    frame(listBox, Theme::SpdLine);
    RECT list = { listBox.left + line, listBox.top + line, listBox.right - line - scrollW, listBox.bottom - line };
    m_spdList = list;
    const std::vector<int>& values = SpeedValues(m_spdMach);
    {
        int clip = SaveDC(hDC);
        IntersectClipRect(hDC, list.left, list.top, list.right, list.bottom);
        for (int k = 0; k <= kSpdRows; k++)
        {
            const int idx = m_spdTopRow + k;
            if (idx < 0 || idx >= (int)values.size())
                continue;
            RECT row = { list.left, list.top + k * rowH, list.right, list.top + (k + 1) * rowH };
            RECT hit;
            IntersectRect(&hit, &row, &list);
            if (values[idx] == m_spdSelected)
                fill(row, Theme::SpdSelected);
            wchar_t s[8];
            if (m_spdMach)
                swprintf_s(s, L"M%d.%02d", values[idx] / 100, values[idx] % 100);
            else
                swprintf_s(s, L"N%03d", values[idx]);
            RECT tr = { row.left + rowH / 5, row.top, row.right, row.bottom };
            text(tr, s, Theme::Text, DT_LEFT);
            char id[8];
            sprintf_s(id, "%d", values[idx]);
            AddScreenObject(SO_SPD_ROW, id, hit, false, "");
        }
        RestoreDC(hDC, clip);
    }
    RECT track = { list.right, listBox.top, listBox.right, listBox.bottom };
    frame(track, Theme::SpdLine);
    RECT up = { track.left, track.top, track.right, track.top + scrollW };
    RECT down = { track.left, track.bottom - scrollW, track.right, track.bottom };
    for (const RECT* b : { &up, &down })
    {
        frame(*b, Theme::SpdLine);
        const int m = max(2, scrollW / 6);
        RECT mark = { (b->left + b->right) / 2 - m, (b->top + b->bottom) / 2 - m,
                      (b->left + b->right) / 2 + m, (b->top + b->bottom) / 2 + m };
        frame(mark, Theme::SpdLine);
    }
    RECT inner = { track.left + line, up.bottom, track.right - line, down.top };
    m_spdTrack = inner;
    const int total = (int)values.size();
    const int innerH = inner.bottom - inner.top;
    if (innerH > 0 && total > kSpdRows)
    {
        const int thumbH = max(6, innerH * kSpdRows / total);
        const int thumbTop = inner.top + (innerH - thumbH) * m_spdTopRow / (total - kSpdRows);
        RECT thumb = { inner.left, thumbTop, inner.right, thumbTop + thumbH };
        fill(thumb, Theme::SpdThumb);
    }
    AddScreenObject(SO_SPD_TRACK, "SPD_TRACK", inner, false, "");
    AddScreenObject(SO_SPD_UP, "SPD_UP", up, false, "");
    AddScreenObject(SO_SPD_DOWN, "SPD_DOWN", down, false, "");
    y += listH + gap;

    RECT tabs[2] = { { x0, y, mid, y + rowH }, { mid, y, x1, y + rowH } };
    const wchar_t* tabText[2] = { L"Kt", L"M" };
    const int radius = rowH / 3;
    for (int i = 0; i < 2; i++)
    {
        const bool on = (i == 1) == m_spdMach;
        int clip = SaveDC(hDC);
        IntersectClipRect(hDC, tabs[i].left, tabs[i].top, tabs[i].right + line, tabs[i].bottom);
        HBRUSH b = CreateSolidBrush(on ? Theme::SpdTabOn : Theme::SpdBody);
        SelectObject(hDC, b);
        SelectObject(hDC, linePen);
        RoundRect(hDC, tabs[i].left + line / 2, tabs[i].top + line / 2, tabs[i].right, tabs[i].bottom + radius,
            radius, radius);
        RestoreDC(hDC, clip);
        DeleteObject(b);
        text(tabs[i], tabText[i], Theme::Text, DT_CENTER);
        AddScreenObject(SO_SPD_TAB, i == 1 ? "1" : "0", tabs[i], false, "");
    }
    {
        HGDIOBJ op = SelectObject(hDC, linePen);
        MoveToEx(hDC, x0, y + rowH - line, NULL);
        LineTo(hDC, x1, y + rowH - line);
        SelectObject(hDC, op);
    }
    y += rowH + gap;

    const wchar_t* modes[3] = { L"equal", L"or greater", L"or less" };
    const int dot = rowH * 3 / 5;
    for (int i = 0; i < 3; i++)
    {
        RECT row = { x0, y, x1, y + rowH };
        {
            const Gdiplus::REAL left = x0 + 0.5f, top = (row.top + row.bottom - dot) / 2.0f + 0.5f;
            const Gdiplus::REAL side = (Gdiplus::REAL)dot, round = dot / 2.5f;
            Gdiplus::GraphicsPath box;
            box.AddArc(left, top, round, round, 180.0f, 90.0f);
            box.AddArc(left + side - round, top, round, round, 270.0f, 90.0f);
            box.AddArc(left + side - round, top + side - round, round, round, 0.0f, 90.0f);
            box.AddArc(left, top + side - round, round, round, 90.0f, 90.0f);
            box.CloseFigure();
            Gdiplus::Graphics g(hDC);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            Gdiplus::SolidBrush face(Theme::GdiColor(i == m_spdMode ? Theme::Text : Theme::SpdBody));
            Gdiplus::Pen edge(Theme::GdiColor(Theme::SpdLine), (Gdiplus::REAL)line);
            g.FillPath(&face, &box);
            g.DrawPath(&edge, &box);
        }
        RECT tr = { x0 + dot + rowH / 5, row.top, x1, row.bottom };
        text(tr, modes[i], Theme::Text, DT_LEFT);
        char id[4];
        sprintf_s(id, "%d", i);
        AddScreenObject(SO_SPD_MODE, id, row, false, "");
        y += rowH;
    }
    y += gap;

    const int btnW = (x1 - x0) * 3 / 5;
    for (int i = 0; i < 2; i++)
    {
        RECT btn = { mid - btnW / 2, y, mid + btnW / 2, y + rowH };
        fill(btn, Theme::SpdButton);
        frame(btn, Theme::SpdLine);
        text(btn, i == 0 ? L"\x0414\x0430" : L"\x041E\x0442\x043C\x0435\x043D\x0430", Theme::Text, DT_CENTER);
        AddScreenObject(i == 0 ? SO_SPD_YES : SO_SPD_CANCEL, i == 0 ? "SPD_YES" : "SPD_CANCEL", btn, false, "");
        y += rowH + gap;
    }

    RestoreDC(hDC, saved);
    DeleteObject(linePen);
    m_spdEntry.Move(field);
    m_spdDrawnTick = GetTickCount64();
}
