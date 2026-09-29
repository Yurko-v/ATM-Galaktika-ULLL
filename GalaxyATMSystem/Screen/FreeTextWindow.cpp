#include "pch.h"
#include "Screen/ScreenCommon.h"

using namespace Galaxy;

void CGalaxyATMSystemRadarScreen::OpenFreeTextWindow(const char* callsign)
{
    CFlightPlan fp = GetPlugIn()->FlightPlanSelect(callsign);
    if (!fp.IsValid())
        return;
    m_ftOpen = true;
    m_ftCallsign = callsign;
    m_ftButtonsDown = true;
    m_ftEntryPending = true;
    m_ftPendingTick = GetTickCount64();

    POINT cursor;
    HWND view = NULL;
    if (CursorRadarPoint(cursor, &view))
        m_popupView = view;
    RequestRefresh();
}

void CGalaxyATMSystemRadarScreen::CloseFreeTextWindow()
{
    m_ftEntry.Close();
    m_ftOpen = false;
    m_ftEntryPending = false;
    m_ftArea = { 0, 0, 0, 0 };
    RequestRefresh();
}

void CGalaxyATMSystemRadarScreen::ApplyFreeText()
{
    CFlightPlan fp = GetPlugIn()->FlightPlanSelect(m_ftCallsign.c_str());
    if (fp.IsValid() && m_ftEntry.IsOpen())
    {
        const std::string text = Narrow(m_ftEntry.Text());
        CFlightPlanControllerAssignedData cad = fp.GetControllerAssignedData();
        const bool ok = cad.SetScratchPadString(text.c_str());
        const char* back = fp.GetControllerAssignedData().GetScratchPadString();
        const char* tracking = fp.GetTrackingControllerId();
        Log::Info("formular", m_ftCallsign + ": free text '" + text + "' set=" + (ok ? "1" : "0")
            + " now='" + (back != NULL ? back : "") + "' tracking='" + (tracking != NULL ? tracking : "") + "'");
        if (text.empty())
            m_localFreeText.erase(m_ftCallsign);
        else
            m_localFreeText[m_ftCallsign] = text;
    }
    else
    {
        Log::Warn("formular", m_ftCallsign + ": free text not applied, the entry was not open");
    }
    CloseFreeTextWindow();
}

void CGalaxyATMSystemRadarScreen::TickFreeTextWindow()
{
    if (!m_ftOpen)
        return;

    auto label = m_formulars.find(m_ftCallsign);
    if (label == m_formulars.end() || label->second.items.empty())
    {
        CloseFreeTextWindow();
        return;
    }

    POINT cursor;
    const bool onRadar = CursorRadarPoint(cursor);
    const bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0
        || (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    if (down && !m_ftButtonsDown && onRadar
        && !PtInRect(&m_ftArea, cursor) && !PtInRect(&label->second.area, cursor))
    {
        m_ftButtonsDown = down;
        CloseFreeTextWindow();
        return;
    }
    m_ftButtonsDown = down;

    if (m_ftEntryPending && m_ftDrawnTick > m_ftPendingTick && m_popupView != NULL)
    {
        m_ftEntryPending = false;
        CFlightPlan fp = GetPlugIn()->FlightPlanSelect(m_ftCallsign.c_str());
        const char* pad = fp.IsValid() ? fp.GetControllerAssignedData().GetScratchPadString() : NULL;
        std::string initial = pad != NULL ? pad : "";
        auto local = m_localFreeText.find(m_ftCallsign);
        if (initial.empty() && local != m_localFreeText.end())
            initial = local->second;
        if (!m_ftEntry.Open(m_popupView, m_ftField, GetFormularFont(), Widen(initial.c_str()), false, 60,
            [this](TextEntry::End end)
            {
                if (end == TextEntry::End::Cancel)
                    CloseFreeTextWindow();
                else
                    ApplyFreeText();
            }))
            Log::Warn("formular", m_ftCallsign + ": free text entry did not open");
    }
}

void CGalaxyATMSystemRadarScreen::DrawFreeTextWindow(HDC hDC)
{
    auto label = m_formulars.find(m_ftCallsign);
    if (label == m_formulars.end() || label->second.items.empty())
        return;

    int saved = SaveDC(hDC);
    SetBkMode(hDC, TRANSPARENT);
    SelectObject(hDC, GetFormularFont());
    TEXTMETRICW tm;
    GetTextMetricsW(hDC, &tm);
    const int L = max(1, (int)(tm.tmHeight + tm.tmExternalLeading));

    const int border = max(2, L / 8);
    const int line = max(1, L / 12);
    const int width = L * 9;
    const int titleH = L * 6 / 5;
    const int pad = L / 4;
    const int rowH = L * 6 / 5;
    const int height = titleH + pad + rowH + pad + rowH + pad + border;

    const RECT& box = label->second.area;
    RECT ra = GetRadarArea();
    int left = box.right + 1;
    if (left + width > ra.right)
        left = box.left - 1 - width;
    const int top = max(ra.top, min(box.top, ra.bottom - height));
    RECT area = { left, top, left + width, top + height };
    m_ftArea = area;
    AddScreenObject(SO_FT_WINDOW, "FT_WINDOW", area, false, "");

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
    auto text = [&](RECT r, const wchar_t* s, UINT align)
    {
        SetTextColor(hDC, Theme::Text);
        DrawTextW(hDC, s, -1, &r, align | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
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

    fill(area, Theme::SpdTitle);
    RECT body = { area.left + border, area.top + titleH, area.right - border, area.bottom - border };
    fill(body, Theme::SpdBody);
    SelectObject(hDC, GetSpeedFont());
    RECT close = { area.right - titleH, area.top, area.right - border, area.top + titleH };
    RECT title = { area.left + border * 2, area.top, close.left, area.top + titleH };
    const std::wstring caption = L"Edit Free Text - " + Widen(m_ftCallsign.c_str());
    text(title, caption.c_str(), DT_LEFT);
    text(close, L"\x00D7", DT_CENTER);
    AddScreenObject(SO_FT_CLOSE, "FT_CLOSE", close, false, "");

    const int x0 = body.left + pad, x1 = body.right - pad;
    int y = body.top + pad;

    RECT field = { x0, y, x1, y + rowH };
    fill(field, RGB(0, 0, 0));
    frame(field, Theme::SpdLine);
    m_ftField = field;
    AddScreenObject(SO_FT_FIELD, "FT_FIELD", field, false, "");
    y += rowH + pad;

    const int okW = (x1 - x0) * 3 / 10;
    RECT cancel = { x0 + (x1 - x0) / 10, y, x1 - okW - pad, y + rowH };
    RECT ok = { x1 - okW, y, x1, y + rowH };
    for (const RECT* b : { &cancel, &ok })
    {
        gradient(*b, Theme::XfrButtonTop, Theme::XfrButtonBottom);
        frame(*b, Theme::SpdLine);
    }
    text(cancel, L"\x041E\x0442\x043C\x0435\x043D\x0430", DT_CENTER);
    text(ok, L"\x041E\x043A", DT_CENTER);
    AddScreenObject(SO_FT_CANCEL, "FT_CANCEL", cancel, false, "");
    AddScreenObject(SO_FT_OK, "FT_OK", ok, false, "");

    RestoreDC(hDC, saved);
    m_ftEntry.Move(field);
    m_ftDrawnTick = GetTickCount64();
}
