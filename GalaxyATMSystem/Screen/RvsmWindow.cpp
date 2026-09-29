#include "pch.h"
#include "Screen/ScreenCommon.h"

using namespace Galaxy;

namespace
{
    const wchar_t* const kRvsmStatusText[] = {
        L"RVSM Approved", L"Exempt", L"Not Approved", L"RVSM Turbulent",
    };
    const int kRvsmStatusCount = (int)_countof(kRvsmStatusText);
    const int kRvsmVisibleHalfRows = 9;
}

CGalaxyATMSystemRadarScreen::RvsmStatus CGalaxyATMSystemRadarScreen::RvsmStatusOf(CFlightPlan& fp)
{
    auto set = m_rvsmStatus.find(fp.GetCallsign());
    if (set != m_rvsmStatus.end())
        return set->second;
    return fp.GetFlightPlanData().IsRvsm() ? RvsmStatus::Approved : RvsmStatus::NotApproved;
}

void CGalaxyATMSystemRadarScreen::OpenRvsmWindow(const char* callsign)
{
    CFlightPlan fp = GetPlugIn()->FlightPlanSelect(callsign);
    if (!fp.IsValid())
        return;

    m_rvsmOpen = true;
    m_rvsmPlacement = PopupPlacement();
    m_rvsmCallsign = callsign;
    m_rvsmButtonsDown = true;
    RequestRefresh();
}

void CGalaxyATMSystemRadarScreen::CloseRvsmWindow()
{
    m_rvsmOpen = false;
    m_rvsmArea = { 0, 0, 0, 0 };
    RequestRefresh();
}

void CGalaxyATMSystemRadarScreen::ApplyRvsm(int status)
{
    if (status >= 0 && status < kRvsmStatusCount)
    {
        m_rvsmStatus[m_rvsmCallsign] = (RvsmStatus)status;
        Log::Info("formular", m_rvsmCallsign + ": RVSM " + Narrow(kRvsmStatusText[status]));
    }
    CloseRvsmWindow();
}

void CGalaxyATMSystemRadarScreen::TickRvsmWindow()
{
    if (!m_rvsmOpen)
        return;

    auto label = m_formulars.find(m_rvsmCallsign);
    if (label == m_formulars.end() || label->second.items.empty())
    {
        CloseRvsmWindow();
        return;
    }

    POINT cursor;
    const bool onRadar = CursorRadarPoint(cursor);
    TrackPopupActive(m_rvsmPlacement, m_rvsmArea, onRadar, cursor);
    const bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0
        || (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    if (down && !m_rvsmButtonsDown)
    {
        const bool inside = onRadar
            && (PtInRect(&m_rvsmArea, cursor) || PtInRect(&label->second.area, cursor));
        if (!inside)
        {
            m_rvsmButtonsDown = down;
            CloseRvsmWindow();
            return;
        }
    }
    m_rvsmButtonsDown = down;
}

void CGalaxyATMSystemRadarScreen::DrawRvsmWindow(HDC hDC)
{
    auto label = m_formulars.find(m_rvsmCallsign);
    if (label == m_formulars.end() || label->second.items.empty())
        return;
    CFlightPlan fp = GetPlugIn()->FlightPlanSelect(m_rvsmCallsign.c_str());
    if (!fp.IsValid())
        return;
    const int selected = (int)RvsmStatusOf(fp);

    int saved = SaveDC(hDC);
    SetBkMode(hDC, TRANSPARENT);
    SelectObject(hDC, GetSpeedFont());
    TEXTMETRICW tm;
    GetTextMetricsW(hDC, &tm);
    const int lineH = max(1, (int)(tm.tmHeight + tm.tmExternalLeading));
    SIZE widest = { 0, 0 };
    GetTextExtentPoint32W(hDC, L"RVSM Turbulent", 14, &widest);

    const int rowH = lineH + 2;
    const int border = max(3, rowH / 6);
    const int line = max(1, rowH / 25);
    const int titleH = rowH + rowH / 8;
    const int gap = rowH / 5;
    const int pad = rowH / 2;
    const int scrollW = max(10, rowH * 3 / 5);
    const int listH = kRvsmVisibleHalfRows * rowH / 2 + 2 * line;
    const int listW = rowH / 3 + widest.cx + rowH + scrollW + 2 * line;
    const int width = 2 * border + 2 * pad + listW;
    const int height = titleH + gap + rowH + gap + listH + pad + border;

    const RECT& box = label->second.area;
    RECT ra = GetRadarArea();
    int left = box.right + 1;
    if (left + width > ra.right)
        left = box.left - 1 - width;
    const RECT area = PlacePopup(m_rvsmPlacement, left, max(ra.top, min(box.top, ra.bottom - height)), width, height);
    m_rvsmArea = area;
    AddScreenObject(SO_RVSM_WINDOW, "RVSM_WINDOW", area, true, "");

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

    fill(area, PopupFrame(m_rvsmPlacement));
    RECT body = { area.left + border, area.top + titleH, area.right - border, area.bottom - border };
    fill(body, Theme::RvsmBody);

    SelectObject(hDC, GetTitleFont());
    RECT title = { area.left + border * 2, area.top, area.right - titleH, area.top + titleH };
    text(title, L"RVSM", Theme::Text, DT_LEFT);
    RECT close = { area.right - titleH, area.top, area.right - border, area.top + titleH };
    text(close, L"\x00D7", Theme::Text, DT_CENTER);
    AddHotButton(hDC, SO_RVSM_CLOSE, "RVSM_CLOSE", close, "");

    SelectObject(hDC, GetSpeedFont());
    const int x0 = body.left + pad, x1 = body.right - pad;
    int y = body.top + gap;
    RECT cs = { x0, y, x1, y + rowH };
    const std::wstring callsign = Widen(m_rvsmCallsign.c_str());
    text(cs, callsign.c_str(), Theme::Text, DT_CENTER);
    y += rowH + gap;

    RECT listBox = { x0, y, x1, y + listH };
    fill(listBox, Theme::RvsmList);
    frame(listBox, Theme::RvsmListEdge);
    RECT list = { listBox.left + line, listBox.top + line, listBox.right - line - scrollW, listBox.bottom - line };
    for (int i = 0; i < kRvsmStatusCount; i++)
    {
        RECT row = { list.left, list.top + i * rowH, list.right, list.top + (i + 1) * rowH };
        if (row.bottom > list.bottom)
            break;
        if (i == selected)
            fill(row, Theme::SpdSelected);
        RECT tr = { row.left + rowH / 3, row.top, row.right, row.bottom };
        text(tr, kRvsmStatusText[i], Theme::Text, DT_LEFT);
        char id[4];
        sprintf_s(id, "%d", i);
        AddHotButton(hDC, SO_RVSM_ROW, id, row, "");
    }

    RECT track = { list.right, list.top, listBox.right - line, list.bottom };
    fill(track, Theme::RvsmTrack);
    RECT up = { track.left, track.top, track.right, track.top + scrollW };
    RECT down = { track.left, track.bottom - scrollW, track.right, track.bottom };
    RECT thumb = { track.left, up.bottom, track.right, up.bottom + scrollW };
    fill(thumb, Theme::RvsmList);
    for (const RECT* b : { &up, &down })
    {
        fill(*b, Theme::RvsmList);
        frame(*b, Theme::RvsmTrack);
        const int m = max(1, scrollW / 6);
        RECT mark = { (b->left + b->right) / 2 - m, (b->top + b->bottom) / 2 - m,
                      (b->left + b->right) / 2 + m, (b->top + b->bottom) / 2 + m };
        frame(mark, Theme::RvsmListEdge);
    }

    RestoreDC(hDC, saved);
}
