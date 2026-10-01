#include "pch.h"
#include "Screen/ScreenCommon.h"

using namespace Galaxy;

void CGalaxyATMSystemRadarScreen::OpenCflPicker(const char* callsign, bool xfl)
{
    CFlightPlan fp = GetPlugIn()->FlightPlanSelect(callsign);
    if (!fp.IsValid())
        return;

    int fl = (xfl ? AgreedXfl(fp) : fp.GetControllerAssignedData().GetClearedAltitude()) / 100;
    if (xfl && fl <= 2)
        fl = fp.GetControllerAssignedData().GetClearedAltitude() / 100;
    if (fl <= 2)
        fl = fp.GetCorrelatedRadarTarget().GetPosition().GetFlightLevel() / 100;
    const std::vector<int>& levels = CflLevels(!xfl);
    size_t best = 0;
    for (size_t i = 0; i < levels.size(); i++)
        if (abs(levels[i] - fl) < abs(levels[best] - fl))
            best = i;

    m_cflOpen = true;
    m_cflPlacement = PopupPlacement();
    m_cflAnchor = { 0, 0, 0, 0 };
    m_cflInList = false;
    m_cflPicksExitLevel = xfl;
    m_cflCallsign = callsign;
    m_cflTopRow = 0;
    m_cflHoverLevel = -1;
    m_cflButtonsDown = true;
    m_cflEntryPending = false;
    m_cflCells.clear();
    ScrollCfl((int)best / 2 - kCflRows / 2);

    POINT cursor;
    HWND view = NULL;
    if (CursorRadarPoint(cursor, &view))
        m_cflView = view;
    UpdateWheelHook();
    RequestRefresh();
}

void CGalaxyATMSystemRadarScreen::CloseCflPicker()
{
    m_cflEntry.Close();
    m_cflOpen = false;
    m_cflEntryPending = false;
    m_cflCells.clear();
    m_cflArea = { 0, 0, 0, 0 };
    UpdateWheelHook();
    RequestRefresh();
}

void CGalaxyATMSystemRadarScreen::ScrollCfl(int rows)
{
    const int total = (int)((CflLevels(!m_cflPicksExitLevel).size() + 1) / 2);
    m_cflTopRow = max(0, min(total - kCflRows, m_cflTopRow + rows));
    RequestRefresh();
}

int CGalaxyATMSystemRadarScreen::AgreedXfl(CFlightPlan& fp)
{
    auto it = m_formulars.find(fp.GetCallsign());
    if (it == m_formulars.end())
        return XflOf(fp);
    return XflOf(fp, TrackedByOther(fp) ? it->second.agreedEntryFt : it->second.agreedXflFt);
}

std::string CGalaxyATMSystemRadarScreen::AgreedCopx(CFlightPlan& fp)
{
    auto it = m_formulars.find(fp.GetCallsign());
    std::string agreed;
    if (it != m_formulars.end())
        agreed = TrackedByOther(fp) ? it->second.agreedEntryPoint : it->second.agreedCopx;
    return ExitPointFor(fp, agreed, CurrentFormularKind() == FormularKind::Ctr ? GetPlugIn() : NULL);
}

void CGalaxyATMSystemRadarScreen::ApplyCfl(int fl)
{
    CFlightPlan fp = GetPlugIn()->FlightPlanSelect(m_cflCallsign.c_str());
    if (fp.IsValid() && fl > 0 && m_cflPicksExitLevel)
        SendCoordination(fp, "", fl * 100);
    else if (fp.IsValid() && fl > 0)
    {
        if (!fp.GetControllerAssignedData().SetClearedAltitude(IsApproachClearance(fl) ? fl : fl * 100))
            Log::Warn("formular", m_cflCallsign + ": EuroScope refused CFL " + std::to_string(fl));
    }
    CloseCflPicker();
}

void CGalaxyATMSystemRadarScreen::ApplyCflText(const std::wstring& text)
{
    int fl = 0, digits = 0;
    for (wchar_t ch : text)
    {
        if (ch >= L'0' && ch <= L'9')
        {
            fl = fl * 10 + (ch - L'0');
            digits++;
        }
    }
    if (digits >= 1 && digits <= 3 && !IsApproachClearance(fl))
        ApplyCfl(fl);
    else
        CloseCflPicker();
}

void CGalaxyATMSystemRadarScreen::TickCflPicker()
{
    if (!m_cflOpen)
        return;

    auto label = m_formulars.find(m_cflCallsign);
    const bool labelShown = label != m_formulars.end() && !label->second.items.empty();
    if (m_cflInList ? !m_rcOpen : !labelShown)
    {
        CloseCflPicker();
        return;
    }

    POINT cursor;
    const bool onRadar = CflCursor(cursor);
    TrackPopupActive(m_cflPlacement, m_cflArea, onRadar, cursor);

    int hover = -1;
    if (onRadar)
        for (const auto& cell : m_cflCells)
            if (PtInRect(&cell.first, cursor))
                hover = cell.second;
    if (hover != m_cflHoverLevel)
    {
        m_cflHoverLevel = hover;
        RequestRefresh();
    }

    const bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0
        || (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    if (down && !m_cflButtonsDown)
    {
        const bool inside = onRadar
            && (PtInRect(&m_cflArea, cursor) || PtInRect(m_cflInList ? &m_cflAnchor : &label->second.area, cursor));
        if (!inside && !m_cflEntry.IsOpen())
        {
            m_cflButtonsDown = down;
            CloseCflPicker();
            return;
        }
    }
    m_cflButtonsDown = down;

    if (m_cflEntryPending && m_cflDrawnTick > m_cflPendingTick && m_cflView != NULL)
    {
        m_cflEntryPending = false;
        m_cflEntry.Open(m_cflView, m_cflField, GetFormularFont(), L"", false, 3,
            [this](TextEntry::End end)
            {
                if (end == TextEntry::End::Cancel)
                {
                    m_cflEntry.Close();
                    RequestRefresh();
                }
                else
                {
                    ApplyCflText(m_cflEntry.Text());
                }
            });
    }
}

bool CGalaxyATMSystemRadarScreen::CflCursor(POINT& out)
{
    if (!(m_cflInList && m_rcFloating))
        return CursorRadarPoint(out);
    if (!m_rcFloat.Visible() || !GetCursorPos(&out) || WindowFromPoint(out) != m_rcFloat.Handle())
        return false;
    return ScreenToClient(m_rcFloat.Handle(), &out) != FALSE;
}

void CGalaxyATMSystemRadarScreen::DrawCflPicker(HDC hDC, const RECT& bounds)
{
    auto label = m_formulars.find(m_cflCallsign);
    if (!m_cflInList && (label == m_formulars.end() || label->second.items.empty()))
        return;
    const bool toFloat = m_cflInList && m_rcDrawingFloat;
    auto object = [&](int type, const char* id, const RECT& r)
    {
        if (m_cflInList)
            RcObject(type, id, r, false, "");
        else
            AddScreenObject(type, id, r, false, "");
    };
    auto button = [&](int type, const char* id, const RECT& r)
    {
        if (toFloat)
            RcObject(type, id, r, false, "");
        else
            AddHotButton(hDC, type, id, r, "");
    };
    CFlightPlan fp = GetPlugIn()->FlightPlanSelect(m_cflCallsign.c_str());
    if (!fp.IsValid())
        return;

    int saved = SaveDC(hDC);
    SetBkMode(hDC, TRANSPARENT);
    HFONT font = GetFormularFont();
    SelectObject(hDC, font);
    TEXTMETRICW tm;
    GetTextMetricsW(hDC, &tm);
    const int lineH = max(1, (int)(tm.tmHeight + tm.tmExternalLeading));
    SIZE digits = { 0, 0 };
    GetTextExtentPoint32W(hDC, L"VAPP", 4, &digits);
    SIZE okSize = { 0, 0 };
    GetTextExtentPoint32W(hDC, L"Ok", 2, &okSize);

    const int pad = 3;
    const int cellH = lineH * 3 / 2 + 2;
    const int cellW = digits.cx + lineH;
    const int scrollW = max(12, lineH * 2 / 3);
    const int listW = 2 * cellW;
    const int listH = kCflRows * cellH + cellH / 2;
    const int width = pad + listW + 2 + scrollW + pad;
    const int height = pad + listH + pad + cellH + pad;

    const RECT& box = m_cflInList ? m_cflAnchor : label->second.area;
    int left = box.right + 1;
    if (left + width > bounds.right)
        left = box.left - 1 - width;
    const int wantTop = max(bounds.top, min(box.top, bounds.bottom - height));
    const RECT area = m_cflInList ? RECT{ left, wantTop, left + width, wantTop + height }
                                  : PlacePopup(m_cflPlacement, left, wantTop, width, height);
    left = area.left;
    const int top = area.top;
    m_cflArea = area;
    if (m_cflInList)
        object(SO_CFL_WINDOW, "CFL_WINDOW", area);
    else
        AddScreenObject(SO_CFL_WINDOW, "CFL_WINDOW", area, true, "");

    HBRUSH frame = CreateSolidBrush(PopupFrame(m_cflPlacement));
    FillRect(hDC, &area, frame);
    DeleteObject(frame);

    RECT list = { left + pad, top + pad, left + pad + listW, top + pad + listH };
    HBRUSH black = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(hDC, &list, black);

    const std::vector<int>& levels = CflLevels(!m_cflPicksExitLevel);
    HBRUSH hoverFill = CreateSolidBrush(Theme::HoverFill);
    HPEN cellPen = CreatePen(PS_SOLID, 1, Theme::CflCellLine);
    m_cflCells.clear();

    int clipSaved = SaveDC(hDC);
    IntersectClipRect(hDC, list.left, list.top, list.right, list.bottom);
    SelectObject(hDC, cellPen);
    SelectObject(hDC, GetStockObject(NULL_BRUSH));
    for (int col = 0; col < 2; col++)
    {
        for (int k = (col == 0 ? 0 : -1); k < kCflRows; k++)
        {
            const int idx = 2 * (m_cflTopRow + k) + col;
            if (idx < 0 || idx >= (int)levels.size())
                continue;
            const int y = list.top + k * cellH + (col == 1 ? cellH / 2 : 0);
            RECT cell = { list.left + col * cellW, y, list.left + (col + 1) * cellW, y + cellH };
            if (levels[idx] == m_cflHoverLevel)
                FillRect(hDC, &cell, hoverFill);
            Rectangle(hDC, cell.left, cell.top, cell.right + (col == 0 ? 1 : 0), cell.bottom + 1);

            wchar_t text[8];
            if (levels[idx] == kCflClearedApproach)
                wcscpy_s(text, L"CA");
            else if (levels[idx] == kCflVisualApproach)
                wcscpy_s(text, L"VAPP");
            else
                swprintf_s(text, L"%03d", levels[idx]);
            RECT textR = { cell.left + lineH / 2, cell.top, cell.right, cell.bottom };
            SetTextColor(hDC, Theme::Text);
            DrawTextW(hDC, text, -1, &textR, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            RECT hit;
            if (IntersectRect(&hit, &cell, &list))
            {
                char id[8];
                sprintf_s(id, "%d", levels[idx]);
                object(SO_CFL_LEVEL, id, hit);
                m_cflCells.push_back(std::make_pair(hit, levels[idx]));
            }
        }
    }
    RestoreDC(hDC, clipSaved);
    DeleteObject(hoverFill);
    DeleteObject(cellPen);

    RECT track = { list.right + 2, list.top, list.right + 2 + scrollW, list.bottom };
    FillRect(hDC, &track, black);
    HBRUSH line = CreateSolidBrush(Theme::CflCellLine);
    FrameRect(hDC, &track, line);
    RECT up = { track.left, track.top, track.right, track.top + scrollW };
    RECT down = { track.left, track.bottom - scrollW, track.right, track.bottom };
    for (const RECT* b : { &up, &down })
    {
        FrameRect(hDC, b, line);
        const int m = scrollW / 2;
        RECT mark = { (b->left + b->right) / 2 - m / 3 - 1, (b->top + b->bottom) / 2 - m / 3 - 1,
                      (b->left + b->right) / 2 + m / 3 + 1, (b->top + b->bottom) / 2 + m / 3 + 1 };
        FrameRect(hDC, &mark, line);
    }
    RECT inner = { track.left + 1, up.bottom, track.right - 1, down.top };
    m_cflTrack = inner;
    const int totalRows = (int)((levels.size() + 1) / 2);
    const int innerH = inner.bottom - inner.top;
    if (innerH > 0 && totalRows > kCflRows)
    {
        const int thumbH = max(8, innerH * kCflRows / totalRows);
        const int thumbTop = inner.top + (innerH - thumbH) * m_cflTopRow / (totalRows - kCflRows);
        RECT thumb = { inner.left, thumbTop, inner.right, thumbTop + thumbH };
        HBRUSH tb = CreateSolidBrush(Theme::CflThumb);
        FillRect(hDC, &thumb, tb);
        DeleteObject(tb);
    }
    object(SO_CFL_TRACK, "CFL_TRACK", inner);
    button(SO_CFL_UP, "CFL_UP", up);
    button(SO_CFL_DOWN, "CFL_DOWN", down);
    DeleteObject(line);
    DeleteObject(black);

    const int rowTop = list.bottom + pad;
    const int okW = okSize.cx + lineH;
    RECT ok = { area.right - pad - okW, rowTop, area.right - pad, rowTop + cellH };
    RECT field = { list.left, rowTop, ok.left - pad, rowTop + cellH };
    m_cflField = field;

    HBRUSH fieldFill = CreateSolidBrush(Theme::CflField);
    FillRect(hDC, &field, fieldFill);
    DeleteObject(fieldFill);
    int cfl = (m_cflPicksExitLevel ? AgreedXfl(fp) : fp.GetControllerAssignedData().GetClearedAltitude()) / 100;
    if (cfl > 0)
    {
        wchar_t text[8];
        swprintf_s(text, L"%03d", cfl);
        RECT textR = { field.left + 4, field.top, field.right, field.bottom };
        SetTextColor(hDC, Theme::CflFieldText);
        DrawTextW(hDC, text, -1, &textR, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    button(SO_CFL_FIELD, "CFL_FIELD", field);

    HBRUSH okFill = CreateSolidBrush(!toFloat && Hot(ok) ? Theme::HoverFill : Theme::CflButton);
    HPEN okPen = CreatePen(PS_SOLID, 1, Theme::CflCellLine);
    SelectObject(hDC, okFill);
    SelectObject(hDC, okPen);
    RoundRect(hDC, ok.left, ok.top, ok.right, ok.bottom, 6, 6);
    SetTextColor(hDC, Theme::Text);
    DrawTextW(hDC, L"Ok", -1, &ok, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    object(SO_CFL_OK, "CFL_OK", ok);

    RestoreDC(hDC, saved);
    DeleteObject(okFill);
    DeleteObject(okPen);

    m_cflEntry.Move(field);
    m_cflDrawnTick = GetTickCount64();
}
