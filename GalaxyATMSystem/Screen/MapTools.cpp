#include "pch.h"
#include "Screen/ScreenCommon.h"

using namespace Galaxy;

namespace
{
    const int kClickSlopPx = 4;
    const ULONGLONG kRightClickSettleMs = 80;
    const double kMapPickPx = 12.0;
    const double kMapTextPickPx = 30.0;
    const int kMapCircleStartKm = 10;
    const int kMapCircleMaxKm = 999;
    const int kMapCircleSegments = 72;
    const double kKmPerNm = 1.852;
    const int kMapTextMaxChars = 40;

    struct MapMenuRow
    {
        const wchar_t* label;
        bool separatorAfter;
    };

    const MapMenuRow kMapMenuRows[] = {
        { L"Очистить маршрут", false },
        { L"Измерить", false },
        { L"Убрать измерители", true },
        { L"Добавить текст", false },
        { L"Изменить текст", false },
        { L"Удалить текст", true },
        { L"Добавить линию", false },
        { L"Удалить линию", false },
        { L"Все линии убрать", false },
        { L"Круг", false },
    };

    double SegmentDistance(POINT p, POINT a, POINT b)
    {
        const double vx = b.x - a.x, vy = b.y - a.y;
        const double wx = p.x - a.x, wy = p.y - a.y;
        const double len2 = vx * vx + vy * vy;
        const double t = len2 > 0.0 ? max(0.0, min(1.0, (wx * vx + wy * vy) / len2)) : 0.0;
        const double dx = wx - t * vx, dy = wy - t * vy;
        return sqrt(dx * dx + dy * dy);
    }

    double PathDistance(POINT p, const std::vector<POINT>& path, bool closed)
    {
        double best = 1e9;
        for (size_t i = 1; i < path.size(); i++)
            best = min(best, SegmentDistance(p, path[i - 1], path[i]));
        if (closed && path.size() > 2)
            best = min(best, SegmentDistance(p, path.back(), path.front()));
        if (path.size() == 1)
            best = hypot((double)(p.x - path[0].x), (double)(p.y - path[0].y));
        return best;
    }

    std::vector<Gdiplus::PointF> GdiPath(const std::vector<POINT>& path)
    {
        std::vector<Gdiplus::PointF> out;
        out.reserve(path.size());
        for (const POINT& p : path)
            out.push_back(Gdiplus::PointF((Gdiplus::REAL)p.x, (Gdiplus::REAL)p.y));
        return out;
    }
}

bool CGalaxyATMSystemRadarScreen::OnMouseButton(WPARAM message, POINT screenPt)
{
    if (!Authorized())
        return false;

    switch (message)
    {
    case WM_MOUSEMOVE:
        if (m_rightDown && (abs(screenPt.x - m_rightDownScreen.x) > kClickSlopPx
                            || abs(screenPt.y - m_rightDownScreen.y) > kClickSlopPx))
            m_rightMoved = true;
        return false;
    case WM_RBUTTONDOWN:
    {
        POINT client = { 0, 0 };
        HWND view = NULL;
        m_rightDown = CursorRadarPoint(client, &view);
        m_rightMoved = false;
        m_rightDownScreen = screenPt;
        m_rightDownClient = client;
        m_rightDownView = view;
        m_rightDownTick = GetTickCount64();
        return false;
    }
    case WM_RBUTTONUP:
        if (m_rightDown && !m_rightMoved)
        {
            m_rightClickPending = true;
            m_rightUpTick = GetTickCount64();
        }
        m_rightDown = false;
        return false;
    default:
        return false;
    }
}

bool CGalaxyATMSystemRadarScreen::RightClickOnEmptyRadar(POINT pt)
{
    if (m_rulerArmed || m_rulerPlacing || m_mapTool != MapTool::None)
        return false;
    if ((m_visible && PtInRect(&m_panelArea, pt)) || PtInRect(&m_menuBarArea, pt))
        return false;
    for (const auto& entry : m_formulars)
        if (!entry.second.items.empty() && PtInRect(&entry.second.area, pt))
            return false;
    std::string callsign;
    if (FindNearbyTarget(pt, callsign))
        return false;
    return FindNearestRulerIndex(pt, kMapPickPx) < 0;
}

void CGalaxyATMSystemRadarScreen::TickMapTools()
{
    const ULONGLONG now = GetTickCount64();

    if (m_rightClickPending && now - m_rightUpTick >= kRightClickSettleMs)
    {
        m_rightClickPending = false;
        if (m_lastObjectClickTick < m_rightDownTick && Authorized() && RightClickOnEmptyRadar(m_rightDownClient))
            OpenMapMenu(m_rightDownClient, m_rightDownView);
    }

    TickCoordDecisionMenu();

    if (m_mapMenuOpen)
    {
        POINT cursor;
        const bool onRadar = CursorRadarPoint(cursor);
        const bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0
            || (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
        if (down && !m_mapMenuButtonsDown && !(onRadar && PtInRect(&m_mapMenuArea, cursor)))
            CloseMapMenu();
        m_mapMenuButtonsDown = down;
    }

    if (m_mapEntryPending && m_mapDrawnTick > m_mapEntryPendingTick && m_mapView != NULL)
    {
        m_mapEntryPending = false;
        m_mapEntry.Open(m_mapView, m_mapEntryRect, m_fonts.Body, m_mapEntryText, false, kMapTextMaxChars,
            [this](TextEntry::End end)
            {
                if (end == TextEntry::End::Cancel)
                {
                    m_mapEntry.Close();
                    RequestRefresh();
                }
                else
                {
                    CommitMapText();
                }
            });
    }

    if (m_mapTool != MapTool::None)
        RequestRefresh();
}

void CGalaxyATMSystemRadarScreen::OpenMapMenu(POINT at, HWND view)
{
    m_mapMenuOpen = true;
    m_mapMenuAt = at;
    m_mapMenuPos = ConvertCoordFromPixelToPosition(at);
    m_mapMenuButtonsDown = true;
    m_mapView = view;
    RequestRefresh();
}

void CGalaxyATMSystemRadarScreen::CloseMapMenu()
{
    m_mapMenuOpen = false;
    m_mapMenuArea = { 0, 0, 0, 0 };
    RequestRefresh();
}

bool CGalaxyATMSystemRadarScreen::MapMenuItemEnabled(MapMenuItem item)
{
    int line = -1, circle = -1;
    switch (item)
    {
    case MapMenuItem::RemoveMeasures:
        return !m_rulers.empty();
    case MapMenuItem::EditText:
    case MapMenuItem::DeleteText:
        return NearestMapText(m_mapMenuAt, kMapTextPickPx) >= 0;
    case MapMenuItem::DeleteLine:
        return NearestMapShape(m_mapMenuAt, kMapPickPx, line, circle);
    case MapMenuItem::ClearLines:
        return !m_mapLines.empty() || !m_mapCircles.empty();
    default:
        return true;
    }
}

void CGalaxyATMSystemRadarScreen::RunMapMenuItem(MapMenuItem item)
{
    if (!MapMenuItemEnabled(item))
        return;
    CloseMapMenu();
    int line = -1, circle = -1;
    switch (item)
    {
    case MapMenuItem::ClearRoutes:
        m_routeShown.clear();
        break;
    case MapMenuItem::Measure:
        if (!m_rulerArmed && !m_rulerPlacing)
            m_rulerPressPending = true;
        break;
    case MapMenuItem::RemoveMeasures:
        m_rulers.clear();
        m_rulerPlacing = false;
        m_rulerArmed = false;
        break;
    case MapMenuItem::AddText:
        OpenMapTextEntry(-1);
        break;
    case MapMenuItem::EditText:
        OpenMapTextEntry(NearestMapText(m_mapMenuAt, kMapTextPickPx));
        break;
    case MapMenuItem::DeleteText:
    {
        const int index = NearestMapText(m_mapMenuAt, kMapTextPickPx);
        if (index >= 0)
            m_mapTexts.erase(m_mapTexts.begin() + index);
        break;
    }
    case MapMenuItem::AddLine:
        m_mapLineDraft = MapLine();
        m_mapLineDraft.points.push_back(m_mapMenuPos);
        m_mapTool = MapTool::Line;
        break;
    case MapMenuItem::DeleteLine:
        if (NearestMapShape(m_mapMenuAt, kMapPickPx, line, circle))
        {
            if (line >= 0)
                m_mapLines.erase(m_mapLines.begin() + line);
            else if (circle >= 0)
                m_mapCircles.erase(m_mapCircles.begin() + circle);
        }
        break;
    case MapMenuItem::ClearLines:
        m_mapLines.clear();
        m_mapCircles.clear();
        break;
    case MapMenuItem::Circle:
    {
        MapCircle c;
        c.center = m_mapMenuPos;
        c.radiusKm = kMapCircleStartKm;
        m_mapCircles.push_back(c);
        m_mapCircleEditing = (int)m_mapCircles.size() - 1;
        m_mapTool = MapTool::Circle;
        break;
    }
    default:
        break;
    }
    RequestRefresh();
}

int CGalaxyATMSystemRadarScreen::NearestMapText(POINT pt, double thresholdPx)
{
    int best = -1;
    double bestDist = thresholdPx;
    for (size_t i = 0; i < m_mapTexts.size(); i++)
    {
        const POINT at = ConvertCoordFromPositionToPixel(m_mapTexts[i].at);
        const int width = (int)m_mapTexts[i].text.size() * 8;
        const POINT a = at, b = { at.x + width, at.y };
        const double d = SegmentDistance(pt, a, b);
        if (d <= bestDist)
        {
            bestDist = d;
            best = (int)i;
        }
    }
    return best;
}

bool CGalaxyATMSystemRadarScreen::NearestMapShape(POINT pt, double thresholdPx, int& line, int& circle)
{
    line = -1;
    circle = -1;
    double bestDist = thresholdPx;
    for (size_t i = 0; i < m_mapLines.size(); i++)
    {
        std::vector<POINT> path;
        for (const CPosition& p : m_mapLines[i].points)
            path.push_back(ConvertCoordFromPositionToPixel(p));
        const double d = PathDistance(pt, path, false);
        if (d <= bestDist)
        {
            bestDist = d;
            line = (int)i;
        }
    }
    for (size_t i = 0; i < m_mapCircles.size(); i++)
    {
        const double d = PathDistance(pt, MapCirclePath(m_mapCircles[i]), true);
        if (d <= bestDist)
        {
            bestDist = d;
            circle = (int)i;
            line = -1;
        }
    }
    return line >= 0 || circle >= 0;
}

std::vector<POINT> CGalaxyATMSystemRadarScreen::MapCirclePath(const MapCircle& c)
{
    std::vector<POINT> path;
    path.reserve(kMapCircleSegments);
    for (int i = 0; i < kMapCircleSegments; i++)
        path.push_back(ConvertCoordFromPositionToPixel(
            CalculateDestinationPoint(c.center, 360.0 * i / kMapCircleSegments, c.radiusKm / kKmPerNm)));
    return path;
}

void CGalaxyATMSystemRadarScreen::OpenMapTextEntry(int index)
{
    m_mapTextEditing = index;
    const bool editing = index >= 0 && index < (int)m_mapTexts.size();
    m_mapTextPos = editing ? m_mapTexts[index].at : m_mapMenuPos;
    m_mapEntryText = editing ? m_mapTexts[index].text : std::wstring();
    const POINT at = ConvertCoordFromPositionToPixel(m_mapTextPos);
    const int height = 22, width = 220;
    m_mapEntryRect = { at.x, at.y - height / 2, at.x + width, at.y + height / 2 };
    m_mapEntryPending = true;
    m_mapEntryPendingTick = GetTickCount64();
    RequestRefresh();
}

void CGalaxyATMSystemRadarScreen::CommitMapText()
{
    const std::wstring text = TrimSpaces(m_mapEntry.Text());
    m_mapEntry.Close();
    const bool editing = m_mapTextEditing >= 0 && m_mapTextEditing < (int)m_mapTexts.size();
    if (editing && text.empty())
        m_mapTexts.erase(m_mapTexts.begin() + m_mapTextEditing);
    else if (editing)
        m_mapTexts[m_mapTextEditing].text = text;
    else if (!text.empty())
        m_mapTexts.push_back({ m_mapTextPos, text });
    m_mapTextEditing = -1;
    RequestRefresh();
}

void CGalaxyATMSystemRadarScreen::FinishMapTool()
{
    if (m_mapTool == MapTool::Line && m_mapLineDraft.points.size() >= 2)
        m_mapLines.push_back(m_mapLineDraft);
    m_mapLineDraft = MapLine();
    m_mapCircleEditing = -1;
    m_mapTool = MapTool::None;
    RequestRefresh();
}

bool CGalaxyATMSystemRadarScreen::WheelMapCircle(int rows)
{
    if (m_mapTool != MapTool::Circle || m_mapCircleEditing < 0 || m_mapCircleEditing >= (int)m_mapCircles.size())
        return false;
    int& radius = m_mapCircles[m_mapCircleEditing].radiusKm;
    const int reference = rows > 0 ? radius - 1 : radius;
    const int step = reference < 10 ? 1 : reference < 50 ? 5 : 10;
    radius = max(1, min(kMapCircleMaxKm, radius - rows * step));
    RequestRefresh();
    return true;
}

void CGalaxyATMSystemRadarScreen::MapCanvasClick(POINT pt, int button)
{
    if (m_mapTool == MapTool::Line && button == BUTTON_LEFT)
    {
        m_mapLineDraft.points.push_back(ConvertCoordFromPixelToPosition(pt));
        RequestRefresh();
        return;
    }
    FinishMapTool();
}

void CGalaxyATMSystemRadarScreen::DrawMapSketches(HDC hDC)
{
    m_mapDrawnTick = GetTickCount64();
    if (m_mapLines.empty() && m_mapCircles.empty() && m_mapTexts.empty() && m_mapTool == MapTool::None)
        return;

    struct RadiusLabel
    {
        POINT at;
        std::wstring text;
    };
    std::vector<RadiusLabel> radiusLabels;
    {
        Gdiplus::Graphics g(hDC);
        g.SetSmoothingMode(Theme::Smoothing());
        Gdiplus::Pen pen(Theme::GdiColor(Theme::MapSketch), Theme::MapSketchWidth);
        for (const MapLine& line : m_mapLines)
        {
            std::vector<POINT> path;
            for (const CPosition& p : line.points)
                path.push_back(ConvertCoordFromPositionToPixel(p));
            const std::vector<Gdiplus::PointF> points = GdiPath(path);
            if (points.size() >= 2)
                g.DrawLines(&pen, points.data(), (INT)points.size());
        }
        if (m_mapTool == MapTool::Line)
        {
            std::vector<POINT> path;
            for (const CPosition& p : m_mapLineDraft.points)
                path.push_back(ConvertCoordFromPositionToPixel(p));
            POINT cursor;
            if (CursorRadarPoint(cursor))
                path.push_back(cursor);
            const std::vector<Gdiplus::PointF> points = GdiPath(path);
            if (points.size() >= 2)
                g.DrawLines(&pen, points.data(), (INT)points.size());
        }
        for (const MapCircle& c : m_mapCircles)
        {
            const std::vector<POINT> path = MapCirclePath(c);
            const std::vector<Gdiplus::PointF> points = GdiPath(path);
            if (points.size() >= 3)
                g.DrawPolygon(&pen, points.data(), (INT)points.size());
            if (!path.empty())
                radiusLabels.push_back({ path.front(), std::to_wstring(c.radiusKm) + L" " + Tr(L"км") });
        }
    }

    const HFONT font = m_esFont ? m_esFont : m_fonts.Mono;
    int saved = SaveDC(hDC);
    SetBkMode(hDC, TRANSPARENT);
    for (const RadiusLabel& label : radiusLabels)
    {
        const SIZE size = Theme::MeasureText(hDC, font, label.text);
        RECT r = { label.at.x - size.cx / 2, label.at.y - size.cy - 2, label.at.x + size.cx / 2 + 1, label.at.y - 2 };
        Theme::DrawLine(hDC, r, label.text, font, Theme::MapSketch, DT_CENTER | DT_VCENTER | DT_NOCLIP);
    }
    for (size_t i = 0; i < m_mapTexts.size(); i++)
    {
        if (m_mapEntry.IsOpen() && (int)i == m_mapTextEditing)
            continue;
        const POINT at = ConvertCoordFromPositionToPixel(m_mapTexts[i].at);
        const SIZE size = Theme::MeasureText(hDC, font, m_mapTexts[i].text);
        RECT r = { at.x, at.y - size.cy / 2, at.x + size.cx + 1, at.y + size.cy / 2 + 1 };
        Theme::DrawLine(hDC, r, m_mapTexts[i].text, font, Theme::MapText, DT_LEFT | DT_VCENTER | DT_NOCLIP);
    }
    RestoreDC(hDC, saved);
}

RECT CGalaxyATMSystemRadarScreen::DrawPanelMenu(HDC hDC, POINT at, const wchar_t* title,
    const std::vector<PanelMenuRow>& rows, int windowType, int itemType)
{
    const HFONT font = GetSpeedFont();
    const int lineH = max(12, (int)Theme::MeasureText(hDC, font, L"Ag").cy);
    const int border = max(3, lineH / 5);
    const int titleH = title != NULL ? lineH * 5 / 4 : 0;
    const int rowH = lineH * 3 / 2;
    const int separatorH = max(5, lineH / 2);
    const int padX = lineH * 2 / 3;
    const int pad = max(2, lineH / 6);

    int width = lineH * 6;
    if (title != NULL)
        width = max(width, (int)Theme::MeasureText(hDC, GetTitleFont(), Tr(title)).cx + titleH + 2 * border + padX);
    int height = titleH + 2 * border + 2 * pad;
    for (const PanelMenuRow& row : rows)
    {
        width = max(width, (int)Theme::MeasureText(hDC, font, Tr(row.label)).cx + 2 * padX + 2 * border + 2 * pad);
        height += rowH + (row.separatorAfter ? separatorH : 0);
    }

    const RECT ra = GetRadarArea();
    int left = at.x, top = at.y;
    if (left + width > ra.right)
        left = max((int)ra.left, (int)at.x - width);
    if (top + height > ra.bottom)
        top = max((int)ra.top, (int)ra.bottom - height);
    const RECT win = { left, top, left + width, top + height };

    auto fill = [&](const RECT& r, COLORREF color)
    {
        HBRUSH b = CreateSolidBrush(color);
        FillRect(hDC, &r, b);
        DeleteObject(b);
    };
    auto frame = [&](const RECT& r, COLORREF color)
    {
        HBRUSH b = CreateSolidBrush(color);
        FrameRect(hDC, &r, b);
        DeleteObject(b);
    };

    int saved = SaveDC(hDC);
    SetBkMode(hDC, TRANSPARENT);
    fill(win, Theme::PopupFrameActive);
    if (title != NULL)
    {
        const RECT bar = { win.left, win.top, win.right, win.top + titleH };
        TRIVERTEX v[2] = {
            { bar.left, bar.top, (COLOR16)(GetRValue(Theme::XfrTitleTop) << 8), (COLOR16)(GetGValue(Theme::XfrTitleTop) << 8),
              (COLOR16)(GetBValue(Theme::XfrTitleTop) << 8), 0 },
            { bar.right, bar.bottom, (COLOR16)(GetRValue(Theme::PopupFrameActive) << 8),
              (COLOR16)(GetGValue(Theme::PopupFrameActive) << 8), (COLOR16)(GetBValue(Theme::PopupFrameActive) << 8), 0 } };
        GRADIENT_RECT g = { 0, 1 };
        GradientFill(hDC, v, 2, &g, 1, GRADIENT_FILL_RECT_V);
        const RECT caption = { bar.left + border, bar.top, bar.right - border, bar.bottom };
        Theme::DrawLine(hDC, caption, Tr(title), GetTitleFont(), Theme::Text, DT_LEFT | DT_VCENTER);
    }
    frame(win, Theme::XfrOutline);
    const RECT body = { win.left + border, win.top + titleH + (title != NULL ? 0 : border),
                        win.right - border, win.bottom - border };
    fill(body, Theme::SpdBody);
    RECT bodyEdge = body;
    InflateRect(&bodyEdge, 1, 1);
    frame(bodyEdge, Theme::XfrOutline);
    AddScreenObject(windowType, "MENU", win, false, "");

    int y = body.top + pad;
    for (size_t i = 0; i < rows.size(); i++)
    {
        const RECT row = { body.left + pad, y, body.right - pad, y + rowH };
        if (rows[i].enabled)
        {
            if (Hot(row))
                fill(row, Theme::XfrSelected);
            AddScreenObject(itemType, std::to_string(i).c_str(), row, false, "");
        }
        const RECT text = { row.left + padX, row.top, row.right - padX, row.bottom };
        Theme::DrawLine(hDC, text, Tr(rows[i].label), font,
            rows[i].enabled ? Theme::Text : Theme::MenuTextDisabled, DT_LEFT | DT_VCENTER);
        y += rowH;
        if (rows[i].separatorAfter)
        {
            const RECT line = { row.left + padX / 2, y + separatorH / 2, row.right - padX / 2, y + separatorH / 2 + 1 };
            fill(line, Theme::SpdLine);
            y += separatorH;
        }
    }
    RestoreDC(hDC, saved);
    return win;
}

void CGalaxyATMSystemRadarScreen::DrawMapMenu(HDC hDC)
{
    std::vector<PanelMenuRow> rows;
    for (int i = 0; i < (int)MapMenuItem::Count; i++)
        rows.push_back({ kMapMenuRows[i].label, MapMenuItemEnabled((MapMenuItem)i), kMapMenuRows[i].separatorAfter });
    m_mapMenuArea = DrawPanelMenu(hDC, m_mapMenuAt, NULL, rows, SO_MAP_MENU, SO_MAP_MENU_ITEM);
}

void CGalaxyATMSystemRadarScreen::DrawCoordDecisionMenu(HDC hDC)
{
    const std::vector<PanelMenuRow> rows = {
        { L"Cancel", CoordDecisionAllowed(false), false },
        { L"ManCoord", CoordDecisionAllowed(true), false },
    };
    const POINT at = { m_coordMenuAnchor.right + 2, m_coordMenuAnchor.top };
    m_coordMenuArea = DrawPanelMenu(hDC, at, L"Согласование", rows, SO_COORD_MENU, SO_COORD_MENU_ITEM);
}

void CGalaxyATMSystemRadarScreen::TickCoordDecisionMenu()
{
    if (!m_coordMenuOpen)
        return;
    auto label = m_formulars.find(m_coordMenuCallsign);
    if (label == m_formulars.end() || label->second.items.empty())
    {
        CloseCoordDecisionMenu();
        return;
    }
    POINT cursor;
    const bool onRadar = CursorRadarPoint(cursor);
    const bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0
        || (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    const bool inside = onRadar && (PtInRect(&m_coordMenuArea, cursor) || PtInRect(&m_coordMenuAnchor, cursor));
    if (down && !m_coordMenuButtonsDown && !inside)
        CloseCoordDecisionMenu();
    m_coordMenuButtonsDown = down;
}
