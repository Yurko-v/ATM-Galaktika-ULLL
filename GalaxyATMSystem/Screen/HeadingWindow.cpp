#include "pch.h"
#include "Screen/ScreenCommon.h"

using namespace Galaxy;

void CGalaxyATMSystemRadarScreen::OpenHeadingWindow(const char* callsign)
{
    CFlightPlan fp = GetPlugIn()->FlightPlanSelect(callsign);
    if (!fp.IsValid())
        return;

    m_ahdgOpen = true;
    m_ahdgPlacement = PopupPlacement();
    m_ahdgCallsign = callsign;
    m_ahdgButtonsDown = true;
    m_ahdgEntryPending = false;

    int heading = fp.GetControllerAssignedData().GetAssignedHeading();
    if (heading <= 0)
    {
        CRadarTarget rt = fp.GetCorrelatedRadarTarget();
        heading = rt.IsValid() ? (int)lround(rt.GetTrackHeading()) : kHeadingStep;
    }
    const std::vector<int>& values = HeadingValues();
    size_t best = 0;
    for (size_t i = 0; i < values.size(); i++)
        if (HeadingGap(values[i], heading) < HeadingGap(values[best], heading))
            best = i;
    m_ahdgSelected = values[best];
    m_ahdgTopRow = 0;
    ScrollHeading((int)best - kHdgRows / 2);

    POINT cursor;
    HWND view = NULL;
    if (CursorRadarPoint(cursor, &view))
        m_popupView = view;
    UpdateWheelHook();
    RequestRefresh();
}

void CGalaxyATMSystemRadarScreen::CloseHeadingWindow()
{
    m_ahdgEntry.Close();
    m_ahdgOpen = false;
    m_ahdgEntryPending = false;
    m_ahdgArea = { 0, 0, 0, 0 };
    UpdateWheelHook();
    RequestRefresh();
}

void CGalaxyATMSystemRadarScreen::ScrollHeading(int rows)
{
    const int total = (int)HeadingValues().size();
    m_ahdgTopRow = max(0, min(total - kHdgRows, m_ahdgTopRow + rows));
    RequestRefresh();
}

void CGalaxyATMSystemRadarScreen::ApplyHeading()
{
    int value = m_ahdgSelected;

    if (m_ahdgEntry.IsOpen())
    {
        int typed = 0, digits = 0;
        for (wchar_t ch : m_ahdgEntry.Text())
        {
            if (ch >= L'0' && ch <= L'9')
            {
                typed = typed * 10 + (ch - L'0');
                digits++;
            }
        }
        if (digits >= 1 && digits <= 3 && typed <= 360)
            value = typed == 0 ? 360 : typed;
    }

    CFlightPlan fp = GetPlugIn()->FlightPlanSelect(m_ahdgCallsign.c_str());
    if (fp.IsValid() && value > 0)
    {
        if (!fp.GetControllerAssignedData().SetAssignedHeading(value))
            Log::Warn("formular", m_ahdgCallsign + ": EuroScope refused heading " + std::to_string(value));
    }
    CloseHeadingWindow();
}

void CGalaxyATMSystemRadarScreen::TickHeadingWindow()
{
    if (!m_ahdgOpen)
        return;

    auto label = m_formulars.find(m_ahdgCallsign);
    if (label == m_formulars.end() || label->second.items.empty())
    {
        CloseHeadingWindow();
        return;
    }

    POINT cursor;
    const bool onRadar = CursorRadarPoint(cursor);
    TrackPopupActive(m_ahdgPlacement, m_ahdgArea, onRadar, cursor);
    const bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0
        || (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    if (down && !m_ahdgButtonsDown)
    {
        const bool inside = onRadar
            && (PtInRect(&m_ahdgArea, cursor) || PtInRect(&label->second.area, cursor));
        if (!inside && !m_ahdgEntry.IsOpen())
        {
            m_ahdgButtonsDown = down;
            CloseHeadingWindow();
            return;
        }
    }
    m_ahdgButtonsDown = down;

    if (m_ahdgEntryPending && m_ahdgDrawnTick > m_ahdgPendingTick && m_popupView != NULL)
    {
        m_ahdgEntryPending = false;
        m_ahdgEntry.Open(m_popupView, m_ahdgField, GetSpeedFont(), L"", false, 3,
            [this](TextEntry::End end)
            {
                if (end == TextEntry::End::Cancel)
                {
                    m_ahdgEntry.Close();
                    RequestRefresh();
                }
                else
                {
                    ApplyHeading();
                }
            });
    }
}

void CGalaxyATMSystemRadarScreen::DrawHeadingWindow(HDC hDC)
{
    auto label = m_formulars.find(m_ahdgCallsign);
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
    const int width = rowH * 11 / 2;
    const int pad = rowH * 2 / 5;
    const int titleH = rowH * 9 / 8;
    const int gap = rowH / 5;
    const int listH = kHdgRows * rowH;
    const int height = titleH + gap + rowH + gap + rowH + gap + listH + gap
        + 2 * (rowH + gap) + border;

    const RECT& box = label->second.area;
    RECT ra = GetRadarArea();
    int left = box.right + 1;
    if (left + width > ra.right)
        left = box.left - 1 - width;
    const RECT area = PlacePopup(m_ahdgPlacement, left, max(ra.top, min(box.top, ra.bottom - height)), width, height);
    m_ahdgArea = area;
    AddScreenObject(SO_HDG_WINDOW, "HDG_WINDOW", area, true, "");

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

    fill(area, PopupFrame(m_ahdgPlacement));
    RECT body = { area.left + border, area.top + titleH, area.right - border, area.bottom - border };
    fill(body, Theme::SpdBody);
    RECT title = { area.left + border * 2, area.top, area.right - titleH, area.top + titleH };
    text(title, L"Heading", Theme::Text, DT_LEFT);
    RECT close = { area.right - titleH, area.top, area.right - border, area.top + titleH };
    text(close, L"\x00D7", Theme::Text, DT_CENTER);
    AddScreenObject(SO_HDG_CLOSE, "HDG_CLOSE", close, false, "");

    const int x0 = body.left + pad, x1 = body.right - pad;
    const int mid = (x0 + x1) / 2;
    int y = body.top + gap;

    RECT cs = { x0, y, x1, y + rowH };
    const std::wstring callsign = Widen(m_ahdgCallsign.c_str());
    text(cs, callsign.c_str(), Theme::Text, DT_CENTER);
    y += rowH + gap;

    RECT field = { x0, y, x1, y + rowH };
    fill(field, RGB(0, 0, 0));
    frame(field, Theme::SpdLine);
    m_ahdgField = field;
    AddScreenObject(SO_HDG_FIELD, "HDG_FIELD", field, false, "");
    y += rowH + gap;

    const int scrollW = max(12, rowH * 11 / 20);
    RECT listBox = { x0, y, x1, y + listH };
    fill(listBox, RGB(0, 0, 0));
    frame(listBox, Theme::SpdLine);
    RECT list = { listBox.left + line, listBox.top + line, listBox.right - line - scrollW, listBox.bottom - line };
    const std::vector<int>& values = HeadingValues();
    {
        int clip = SaveDC(hDC);
        IntersectClipRect(hDC, list.left, list.top, list.right, list.bottom);
        for (int k = 0; k <= kHdgRows; k++)
        {
            const int idx = m_ahdgTopRow + k;
            if (idx < 0 || idx >= (int)values.size())
                continue;
            RECT row = { list.left, list.top + k * rowH, list.right, list.top + (k + 1) * rowH };
            RECT hit;
            IntersectRect(&hit, &row, &list);
            if (values[idx] == m_ahdgSelected)
                fill(row, Theme::SpdSelected);
            wchar_t s[8];
            swprintf_s(s, L"%03d", values[idx]);
            text(row, s, Theme::Text, DT_CENTER);
            char id[8];
            sprintf_s(id, "%d", values[idx]);
            AddScreenObject(SO_HDG_ROW, id, hit, false, "");
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
    m_ahdgTrack = inner;
    const int total = (int)values.size();
    const int innerH = inner.bottom - inner.top;
    if (innerH > 0 && total > kHdgRows)
    {
        const int thumbH = max(6, innerH * kHdgRows / total);
        const int thumbTop = inner.top + (innerH - thumbH) * m_ahdgTopRow / (total - kHdgRows);
        RECT thumb = { inner.left, thumbTop, inner.right, thumbTop + thumbH };
        fill(thumb, Theme::SpdThumb);
    }
    AddScreenObject(SO_HDG_TRACK, "HDG_TRACK", inner, false, "");
    AddScreenObject(SO_HDG_UP, "HDG_UP", up, false, "");
    AddScreenObject(SO_HDG_DOWN, "HDG_DOWN", down, false, "");
    y += listH + gap;

    const int btnW = (x1 - x0) * 3 / 4;
    for (int i = 0; i < 2; i++)
    {
        RECT btn = { mid - btnW / 2, y, mid + btnW / 2, y + rowH };
        fill(btn, Theme::SpdButton);
        frame(btn, Theme::SpdLine);
        text(btn, i == 0 ? L"\x0414\x0430" : L"\x041E\x0442\x043C\x0435\x043D\x0430", Theme::Text, DT_CENTER);
        AddScreenObject(i == 0 ? SO_HDG_YES : SO_HDG_CANCEL, i == 0 ? "HDG_YES" : "HDG_CANCEL", btn, false, "");
        y += rowH + gap;
    }

    RestoreDC(hDC, saved);
    m_ahdgEntry.Move(field);
    m_ahdgDrawnTick = GetTickCount64();
}
