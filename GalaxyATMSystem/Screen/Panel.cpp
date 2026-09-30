#include "pch.h"
#include "Screen/ScreenCommon.h"

using namespace Galaxy;

int CGalaxyATMSystemRadarScreen::PanelTop()
{
    RECT ra = GetRadarArea();
    RECT tb = GetToolbarArea();

    if (tb.bottom > 0 && tb.top <= ra.top && tb.bottom <= ra.top)
        return tb.bottom;
    return ra.top;
}

void CGalaxyATMSystemRadarScreen::AddScreenObject(int type, const char* id, RECT area, bool moveable, const char* tip)
{
    if (m_recordingPanel)
        m_panelSnapshot.objects.push_back({ type, id != NULL ? id : "", area, moveable,
            tip != NULL ? tip : "", tip != NULL });
    CRadarScreen::AddScreenObject(type, id, area, moveable, tip);
}

void CGalaxyATMSystemRadarScreen::PanelSnapshot::Release()
{
    if (dc != NULL && oldBitmap != NULL)
        SelectObject(dc, oldBitmap);
    if (bitmap != NULL)
        DeleteObject(bitmap);
    if (dc != NULL)
        DeleteDC(dc);
    dc = NULL;
    bitmap = NULL;
    oldBitmap = NULL;
    width = height = 0;
    valid = false;
}

std::string CGalaxyATMSystemRadarScreen::PanelKey()
{
    const RECT ra = GetRadarArea();
    const RECT tb = GetToolbarArea();
    HWND fg = GetForegroundWindow();
    const HKL layout = GetKeyboardLayout(fg != NULL ? GetWindowThreadProcessId(fg, NULL) : 0);
    char key[256];
    sprintf_s(key, "%ld,%ld,%ld,%ld|%ld,%ld|%d%d|%lld|%ld|%p",
        ra.left, ra.top, ra.right, ra.bottom, tb.top, tb.bottom, m_collapsed ? 1 : 0, Authorized() ? 1 : 0,
        (long long)time(NULL), lround(DisplayWidthNM() * 10.0), (void*)layout);
    return key;
}

void CGalaxyATMSystemRadarScreen::DrawPanelAndMenu(HDC hDC)
{
    PanelSnapshot& snap = m_panelSnapshot;
    const std::string key = PanelKey();
    if (!m_panelDirty && snap.valid && snap.key == key)
    {
        int y = 0;
        for (const RECT& r : snap.regions)
        {
            BitBlt(hDC, r.left, r.top, r.right - r.left, r.bottom - r.top, snap.dc, 0, y, SRCCOPY);
            y += r.bottom - r.top;
        }
        for (const RecordedObject& o : snap.objects)
            CRadarScreen::AddScreenObject(o.type, o.id.c_str(), o.area, o.moveable, o.hasTip ? o.tip.c_str() : NULL);
        m_hotRects.insert(m_hotRects.end(), snap.hotRects.begin(), snap.hotRects.end());
        return;
    }

    m_panelDirty = false;
    snap.valid = false;
    snap.objects.clear();
    const size_t hotFrom = m_hotRects.size();
    m_recordingPanel = true;
    DrawPanel(hDC);
    m_menuBarArea = { 0, 0, 0, 0 };
    if (!m_collapsed)
        DrawMenuBar(hDC);
    m_recordingPanel = false;
    snap.hotRects.assign(m_hotRects.begin() + hotFrom, m_hotRects.end());

    if (m_panelBackRounded || m_collapsed || !Authorized())
        return;

    std::vector<RECT> regions;
    int width = 0, height = 0;
    for (const RECT& r : { m_panelBackArea, m_menuBarArea })
    {
        if (r.right <= r.left || r.bottom <= r.top)
            continue;
        regions.push_back(r);
        width = max(width, (int)(r.right - r.left));
        height += r.bottom - r.top;
    }
    if (regions.empty())
        return;

    if (snap.dc == NULL || width > snap.width || height > snap.height)
    {
        snap.Release();
        snap.dc = CreateCompatibleDC(hDC);
        snap.bitmap = snap.dc != NULL ? CreateCompatibleBitmap(hDC, width, height) : NULL;
        if (snap.bitmap == NULL)
        {
            snap.Release();
            return;
        }
        snap.oldBitmap = SelectObject(snap.dc, snap.bitmap);
        snap.width = width;
        snap.height = height;
    }

    int y = 0;
    for (const RECT& r : regions)
    {
        BitBlt(snap.dc, 0, y, r.right - r.left, r.bottom - r.top, hDC, r.left, r.top, SRCCOPY);
        y += r.bottom - r.top;
    }
    snap.regions = regions;
    snap.key = key;
    snap.valid = true;
}

void CGalaxyATMSystemRadarScreen::DrawPanel(HDC hDC)
{
    int width = m_collapsed ? kCollapsedWidth : kPanelWidth;
    int height = m_collapsed
        ? L::HEADER_H + L::COLLAPSED_BOT_PAD
        : !Authorized()
        ? L::HEADER_H + L::HDR_GAP
        + L::Block(!m_authMessage.empty() ? L::AUTH_BOX_H : L::AUTH_BOX_H_IDLE)
        + L::PANEL_BOT_PAD
        : L::HEADER_H
        + L::HDR_GAP + L::Block(L::TIMER_BOX_H)
        + L::BLOCK_GAP + L::Block(L::USER_BOX_H)
        + L::BLOCK_GAP + L::Block(L::VECTORS_BOX_H)
        + L::BLOCK_GAP + L::Block(L::OS_BOX_H)
        + L::BLOCK_GAP_WIDE + L::Block(L::UNITS_BOX_H)
        + L::BLOCK_GAP + L::Block(L::ALTFILTER_BOX_H)
        + L::NOCAP_GAP + L::CODES_BOX_H
        + L::BLOCK_GAP_WIDE + L::Block(L::AERODROME_BOX_H)
        + L::PANEL_BOT_PAD;

    m_vecDistFieldRect = m_vecTimeFieldRect = m_osFontFieldRect = m_dropdownListRect = { 0, 0, 0, 0 };

    RECT ra = GetRadarArea();
    m_panelArea.right = ra.right;
    m_panelArea.left = ra.right - width;
    m_panelArea.top = m_collapsed ? PanelTop() : PanelTop() + MenuBarHeight();
    m_panelArea.bottom = m_panelArea.top + height;
    if (m_collapsed)
    {
        const int dx = max((int)(ra.left - m_panelArea.left), min((int)m_collapsedShift.x, 0));
        const int dy = max(0, min((int)m_collapsedShift.y, (int)(ra.bottom - m_panelArea.bottom)));
        m_collapsedShift = { dx, dy };
        OffsetRect(&m_panelArea, dx, dy);
    }

    int saved = SaveDC(hDC);
    SetBkMode(hDC, TRANSPARENT);

    RECT bgArea = m_panelArea;
    if (!m_collapsed && Authorized() && bgArea.bottom < ra.bottom)
        bgArea.bottom = ra.bottom;
    const int bgCorners = bgArea.bottom < ra.bottom ? Theme::CornerBottomLeft : Theme::CornersNone;
    m_panelBackArea = bgArea;
    m_panelBackRounded = bgCorners != Theme::CornersNone;
    Theme::SmoothBox(hDC, bgArea, &Theme::Background, NULL, Theme::PanelCornerRadius, 1, bgCorners);

    int y = DrawHeader(hDC, m_panelArea.top);
    if (!m_collapsed && !Authorized())
    {
        DrawBlockAuth(hDC, y + L::HDR_GAP);
    }
    else if (!m_collapsed)
    {
        y = DrawBlockTimer(hDC, y + L::HDR_GAP);
        y = DrawBlockUser(hDC, y + L::BLOCK_GAP);
        y = DrawBlockVectors(hDC, y + L::BLOCK_GAP);
        y = DrawBlockOs(hDC, y + L::BLOCK_GAP);
        y = DrawBlockUnits(hDC, y + L::BLOCK_GAP_WIDE);
        y = DrawBlockAltFilter(hDC, y + L::BLOCK_GAP);
        y = DrawBlockCodes(hDC, y + L::NOCAP_GAP);
        y = DrawBlockAerodrome(hDC, y + L::BLOCK_GAP_WIDE);
    }

    RestoreDC(hDC, saved);
}

int CGalaxyATMSystemRadarScreen::DrawHeader(HDC hDC, int y)
{
    RECT toggle = { m_panelArea.right - 16, y + 2, m_panelArea.right - 2, y + 16 };
    int tx = (toggle.left + toggle.right) / 2, ty = (toggle.top + toggle.bottom) / 2;
    HPEN pen = CreatePen(PS_SOLID, 1, Theme::Text);
    HPEN oldPen = (HPEN)SelectObject(hDC, pen);
    MoveToEx(hDC, tx - 4, ty, NULL);
    LineTo(hDC, tx + 5, ty);
    if (m_collapsed)
    {
        MoveToEx(hDC, tx, ty - 4, NULL);
        LineTo(hDC, tx, ty + 5);
    }
    SelectObject(hDC, oldPen);
    DeleteObject(pen);
    AddButton(hDC, SO_PANEL_COLLAPSE, "PANEL_COLLAPSE", toggle,
        m_collapsed ? Tr("Развернуть панель") : Tr("Свернуть панель"));
    if (m_collapsed)
    {
        const RECT beside = { m_panelArea.left, m_panelArea.top, toggle.left, m_panelArea.bottom };
        const RECT below = { toggle.left, toggle.bottom, m_panelArea.right, m_panelArea.bottom };
        AddScreenObject(SO_PANEL_DRAG, "PANEL_DRAG", beside, true, Tr("Перетащите окно"));
        AddScreenObject(SO_PANEL_DRAG, "PANEL_DRAG", below, true, Tr("Перетащите окно"));
    }

    SYSTEMTIME st;
    GetSystemTime(&st);

    wchar_t clock[16];
    swprintf_s(clock, L"%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
    wchar_t date[16];
    swprintf_s(date, L"%02d.%02d.%04d", st.wDay, st.wMonth, st.wYear);

    RECT clockR = { m_panelArea.left, y + L::HDR_TOP, m_panelArea.right, y + L::HDR_TOP + L::CLOCK_H };
    Theme::DrawLine(hDC, clockR, clock, m_fonts.Clock, Theme::Text, DT_CENTER | DT_VCENTER);

    std::wstring mode; COLORREF modeColor;
    GetWorkMode(mode, modeColor);
    std::wstring dateLine = std::wstring(date) + L" " + mode;

    RECT dateR = { m_panelArea.left, clockR.bottom + L::CLOCK_GAP, m_panelArea.right,
                   clockR.bottom + L::CLOCK_GAP + L::DATE_H };
    Theme::DrawLine(hDC, dateR, dateLine, m_fonts.Body, modeColor, DT_CENTER | DT_VCENTER);

    return y + L::HEADER_H;
}

int CGalaxyATMSystemRadarScreen::DrawBlockAuth(HDC hDC, int y)
{
    const bool refused = !m_authMessage.empty();
    RECT box = DrawBlockFrame(hDC, y, Tr(L"Авторизация"),
        refused ? L::AUTH_BOX_H : L::AUTH_BOX_H_IDLE);

    std::wstring designation, role, user;
    GetUserInfo(designation, role, user);

    int cy = box.top + L::AU_TOP;
    RECT designR = { ContentLeft(), cy, ContentLeft() + 47, cy + L::AU_ROW };
    RECT userR = { designR.right + 6, cy, ContentRight(), cy + L::AU_ROW };
    DrawFittedField(hDC, designR, designation);
    DrawFittedField(hDC, userR, user);
    cy += L::AU_ROW + L::AU_GAP;

    RECT roleR = { ContentLeft(), cy, ContentRight(), cy + L::AU_ROW };
    DrawFittedField(hDC, roleR, role);
    cy += L::AU_ROW + L::AU_GAP2;

    if (refused)
    {
        RECT reasonR = { ContentLeft(), cy, ContentRight(), cy + L::AU_STATUS };
        Theme::DrawLine(hDC, reasonR, m_authMessage, m_fonts.Small, Theme::DistressText,
            DT_CENTER | DT_VCENTER | DT_END_ELLIPSIS);
    }
    return box.bottom;
}

int CGalaxyATMSystemRadarScreen::DrawBlockTimer(HDC hDC, int y)
{
    RECT box = DrawBlockFrame(hDC, y, Tr(L"Таймер"), L::TIMER_BOX_H);

    int cy = box.top + L::T_PAD;
    RECT btn = { ContentLeft(), cy, ContentLeft() + 26, cy + L::T_ROW };
    RECT field = { btn.right + 6, cy, ContentRight(), cy + L::T_ROW };

    DrawToggleChip(hDC, btn, L"C", m_timerRunning, SO_TIMER_TOGGLE, "TIMER_TOGGLE",
        Tr("ЛКМ - пуск/стоп таймера, ПКМ - сброс"));

    std::wstring text = L"---";
    if (m_timerRunning || m_timerElapsedMs > 0)
    {
        ULONGLONG elapsed = m_timerElapsedMs;
        if (m_timerRunning)
            elapsed += GetTickCount64() - m_timerStartTick;
        ULONGLONG totalSec = elapsed / 1000;
        wchar_t buf[16];
        swprintf_s(buf, L"%02llu:%02llu:%02llu", totalSec / 3600, (totalSec / 60) % 60, totalSec % 60);
        text = buf;
    }
    DrawOutlinedField(hDC, field, text, m_fonts.Body);

    return box.bottom;
}

int CGalaxyATMSystemRadarScreen::DrawBlockUser(HDC hDC, int y)
{
    RECT box = DrawBlockFrame(hDC, y, Tr(L"Пользователь"), L::USER_BOX_H);

    std::wstring designation, role, user;
    GetUserInfo(designation, role, user);

    int cy = box.top + L::U_TOP;
    RECT designR = { ContentLeft(), cy, ContentLeft() + 47, cy + L::U_ROW };
    RECT userR = { designR.right + 6, cy, ContentRight(), cy + L::U_ROW };
    DrawFittedField(hDC, designR, designation);
    DrawFittedField(hDC, userR, user);
    cy += L::U_ROW + L::U_GAP;

    RECT roleR = { ContentLeft(), cy, ContentRight(), cy + L::U_ROW };
    DrawFittedField(hDC, roleR, role);

    return box.bottom;
}

int CGalaxyATMSystemRadarScreen::DrawBlockVectors(HDC hDC, int y)
{
    RECT box = DrawBlockFrame(hDC, y, Tr(L"Векторы"), L::VECTORS_BOX_H);

    int cy = box.top + L::V_TOP;
    int x = box.left + 9;

    RECT distBtn = { x, cy, x + 25, cy + L::V_ROW };
    RECT distField = { distBtn.right + 6, cy, distBtn.right + 6 + 63, cy + L::V_ROW };
    RECT timeBtn = { distField.right + 9, cy, distField.right + 9 + 21, cy + L::V_ROW };
    RECT timeField = { timeBtn.right + 6, cy, timeBtn.right + 6 + 63, cy + L::V_ROW };

    m_vecDistFieldRect = distField;
    m_vecTimeFieldRect = timeField;

    DrawToggleChip(hDC, distBtn, Tr(L"Д"), m_vecDistEnabled, SO_VEC_DIST_TOGGLE, "VEC_DIST_TOGGLE",
        Tr("Вектор по дальности (км) - вместо вектора по времени"), Theme::ButtonMid);
    wchar_t distText[16];
    swprintf_s(distText, L"%d", m_vecDistKm);
    DrawDropdownField(hDC, distField, distText, SO_VEC_DIST_FIELD, "VEC_DIST_FIELD", Tr("Выбрать длину вектора, км"));

    DrawToggleChip(hDC, timeBtn, Tr(L"Э"), m_vecTimeEnabled, SO_VEC_TIME_TOGGLE, "VEC_TIME_TOGGLE",
        Tr("Вектор по времени (мин) - вместо вектора по дальности"), Theme::ButtonMid);
    wchar_t timeText[16];
    swprintf_s(timeText, L"%d", m_vecTimeMin);
    DrawDropdownField(hDC, timeField, timeText, SO_VEC_TIME_FIELD, "VEC_TIME_FIELD", Tr("Выбрать время вектора, мин"));

    cy += L::V_ROW + L::V_GAP1;
    DrawCheckRow(hDC, cy, box.left + 8, Tr(L"Вектор по плану"), m_vecByPlan,
        SO_VEC_BY_PLAN_CHK, "VEC_BY_PLAN", Tr("Вектор по плану"));

    cy += L::V_CHK + L::V_GAP2;
    DrawCheckRow(hDC, cy, box.left + 8, Tr(L"Расчётный эшелон"), m_vecShowLevel,
        SO_VEC_LEVEL_CHK, "VEC_LEVEL", Tr("Расчётный эшелон"));

    return box.bottom;
}

int CGalaxyATMSystemRadarScreen::DrawBlockOs(HDC hDC, int y)
{
    RECT box = DrawBlockFrame(hDC, y, Tr(L"ФС"), L::OS_BOX_H);

    RECT fontField = { box.right - 4 - 63, box.top + L::O_TOP, box.right - 4, box.top + L::O_TOP + L::O_LABEL };
    m_osFontFieldRect = fontField;

    RECT label = { box.left + 8, box.top + L::O_TOP, fontField.left - 6, box.top + L::O_TOP + L::O_LABEL };
    Theme::DrawLine(hDC, label, Tr(L"Р-р шрифта:"), m_fonts.Body, Theme::Text, DT_LEFT | DT_VCENTER);

    wchar_t sizeText[8];
    swprintf_s(sizeText, L"%d", Plugin()->TagFontSize());
    DrawDropdownField(hDC, fontField, sizeText, SO_OS_FONT_FIELD, "OS_FONT_FIELD", Tr("Выбрать размер шрифта формуляра"));

    RECT list = { box.left + 5, label.bottom + L::O_GAP, box.right - 4,
                  label.bottom + L::O_GAP + L::O_LIST_H };
    Theme::OutlineBox(hDC, list, Theme::InsetFill, Theme::Border);

    const int x = list.left + 5;
    int row = list.top + L::OS_ROW0;
    DrawCheckRow(hDC, row, x, Tr(L"2 строчный"), m_osLines == 2,
        SO_OS_TWO_LINE, "OS_2LINE", Tr("Двухстрочный формуляр"));
    row += L::OS_PITCH;
    DrawCheckRow(hDC, row, x, Tr(L"скорость"), m_osSpeed,
        SO_OS_SPEED, "OS_SPEED", Tr("Показывать скорость в формуляре"));
    row += L::OS_PITCH;
    DrawCheckRow(hDC, row, x, Tr(L"3 строчный"), m_osLines == 3,
        SO_OS_THREE_LINE, "OS_3LINE", Tr("Трёхстрочный формуляр"));

    return box.bottom;
}

int CGalaxyATMSystemRadarScreen::DrawBlockAltFilter(HDC hDC, int y)
{
    RECT box = DrawBlockFrame(hDC, y, Tr(L"Фильтр высоты"), L::ALTFILTER_BOX_H);

    wchar_t fromText[8], toText[8];
    swprintf_s(fromText, L"FL%03d", Plugin()->AltFilterFromFL());
    swprintf_s(toText, L"FL%03d", Plugin()->AltFilterToFL());

    const int valLeft = box.left + 106, valRight = valLeft + 60;

    int cy = box.top + L::F_TOP;
    RECT maxLbl = { box.left + 8, cy, valLeft, cy + L::F_ROW };
    RECT maxVal = { valLeft, cy, valRight, cy + L::F_ROW };
    Theme::DrawLine(hDC, maxLbl, Tr(L"Макс:"), m_fonts.Body, Theme::Text, DT_LEFT | DT_VCENTER);
    DrawOutlinedField(hDC, maxVal, toText, m_fonts.Body);
    AddButton(hDC, SO_ALTFILTER_TO, "ALTFILTER_TO", maxVal, Tr("Верхняя граница фильтра высоты"));
    cy += L::F_ROW + L::F_GAP1;

    RECT minLbl = { box.left + 8, cy, valLeft, cy + L::F_ROW };
    RECT minVal = { valLeft, cy, valRight, cy + L::F_ROW };
    Theme::DrawLine(hDC, minLbl, Tr(L"Мин :"), m_fonts.Body, Theme::Text, DT_LEFT | DT_VCENTER);
    DrawOutlinedField(hDC, minVal, fromText, m_fonts.Body);
    AddButton(hDC, SO_ALTFILTER_FROM, "ALTFILTER_FROM", minVal, Tr("Нижняя граница фильтра высоты"));
    cy += L::F_ROW + L::F_GAP2;

    DrawCheckRow(hDC, cy, box.left + 39, Tr(L"Использовать"), Plugin()->AltFilterEnabled(),
        SO_ALTFILTER_USE_CHK, "ALTFILTER_USE", Tr("Использовать фильтр высоты"));

    return box.bottom;
}

int CGalaxyATMSystemRadarScreen::DrawBlockCodes(HDC hDC, int y)
{
    RECT box = DrawBoxOnly(hDC, y, L::CODES_BOX_H);

    double widthNM = DisplayWidthNM();
    wchar_t scaleText[24];
    if (Plugin()->UnitDist() == DistUnit::NM)
        swprintf_s(scaleText, L"%d", (int)lround(widthNM));
    else
        swprintf_s(scaleText, L"%d", (int)lround(widthNM * 1.852));

    RECT vv = { box.left + 7, box.top + L::C_VV_TOP, box.left + 82, box.top + L::C_VV_TOP + L::C_VV_H };
    Theme::DrawLine(hDC, vv, scaleText, m_fonts.Body, Theme::Text, DT_LEFT | DT_VCENTER);
    AddScreenObject(SO_VV_SCALE, "VV_SCALE", vv, false,
        Tr("Масштаб: ширина отображаемой зоны от края до края"));

    RECT all = { box.left + 85, box.top + L::C_ALL_TOP, box.left + 125, box.top + L::C_ALL_TOP + L::C_ROW_H };
    Theme::OutlineBox(hDC, all, m_codeAll ? Theme::Active : Theme::ButtonMid, Theme::Border);
    Theme::DrawLine(hDC, all, Tr(L"ВСЕ"), m_fonts.Small, Theme::Text, DT_CENTER | DT_VCENTER);
    AddButton(hDC, SO_CODE_ALL, "CODE_ALL", all, Tr("Пропускать все коды"));

    RECT bp = { box.left + 32, box.top + L::C_BP_TOP, box.left + 65, box.top + L::C_BP_TOP + L::C_ROW_H };
    Theme::OutlineBox(hDC, bp, m_codeBp ? Theme::Active : Theme::ButtonMid, Theme::Border);
    Theme::DrawLine(hDC, bp, Tr(L"БП"), m_fonts.Small, Theme::Text, DT_CENTER | DT_VCENTER);
    AddButton(hDC, SO_CODE_BP, "CODE_BP", bp, Tr("Без привязки"));

    RECT filter = { box.left + 71, box.top + L::C_FLT_TOP, box.left + 183, box.top + L::C_FLT_TOP + L::C_FLT_H };
    Theme::OutlineBox(hDC, filter, Theme::InsetFill, Theme::Border);
    if (!m_codeFilter.empty())
    {
        RECT inner = { filter.left + 3, filter.top, filter.right - 3, filter.bottom };
        Theme::DrawLine(hDC, inner, m_codeFilter, m_fonts.Small, Theme::Text,
            DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
    }
    AddButton(hDC, SO_CODE_FILTER, "CODE_FILTER", filter, Tr("Коды источника ВВ1"));

    RECT extra = { box.left + 189, box.top + L::C_EXTRA_TOP, box.left + 201, box.top + L::C_EXTRA_TOP + L::C_EXTRA_H };
    Theme::OutlineBox(hDC, extra, m_codeExtra ? Theme::Active : Theme::Background, Theme::Border);
    AddButton(hDC, SO_CODE_EXTRA, "CODE_EXTRA", extra, Tr("Источник ВВ1 включён"));

    if (!m_vvDragging)
        SyncSliderFromZoom();

    RECT track = { box.left + 7, box.top + L::C_SLIDER_TOP, box.left + 13, box.top + L::C_SLIDER_BOT };
    m_vvSliderRect = track;
    Theme::FillBox(hDC, track, Theme::SliderTrack);
    int thumbY = track.bottom - MulDiv(track.bottom - track.top, m_vvGain, 100);
    RECT lit = { track.left, thumbY, track.right, track.bottom };
    if (lit.bottom > lit.top)
        Theme::FillBox(hDC, lit, Theme::SliderFill);
    HBRUSH thumbBr = CreateSolidBrush(Theme::SliderThumb);
    HBRUSH oldBr = (HBRUSH)SelectObject(hDC, thumbBr);
    HPEN thumbPen = CreatePen(PS_SOLID, 1, Theme::SliderThumb);
    HPEN oldPen = (HPEN)SelectObject(hDC, thumbPen);
    Ellipse(hDC, track.left - 2, thumbY - 5, track.right + 2, thumbY + 5);
    SelectObject(hDC, oldPen);
    DeleteObject(thumbPen);
    SelectObject(hDC, oldBr);
    DeleteObject(thumbBr);
    AddScreenObject(SO_VV_SLIDER, "VV_SLIDER",
        RECT{ track.left - 4, track.top, track.right + 4, track.bottom }, true,
        Tr("Масштаб радара: вверх - приблизить, вниз - отдалить"));

    RECT distressCap = { box.left, box.top + L::C_DISTRESS_CAP, box.right, box.top + L::C_DISTRESS_CAP + L::C_CAP_H };
    Theme::DrawLine(hDC, distressCap, Tr(L"Коды бедствия"), m_fonts.Body, Theme::Text, DT_CENTER | DT_VCENTER);
    RECT distress = { box.left + 24, box.top + L::C_DISTRESS_FIELD, box.left + 201, box.top + L::C_DISTRESS_FIELD + L::C_FIELD_H };
    Theme::OutlineBox(hDC, distress, Theme::Background, Theme::Border);
    Theme::DrawLine(hDC, RECT{ distress.left + 4, distress.top, distress.right - 4, distress.bottom },
        GetDistressCodes(), m_fonts.Small, Theme::DistressText,
        DT_CENTER | DT_VCENTER | DT_END_ELLIPSIS);

    RECT dupCap = { box.left, box.top + L::C_DUP_CAP, box.right, box.top + L::C_DUP_CAP + L::C_CAP_H };
    Theme::DrawLine(hDC, dupCap, Tr(L"Двойной код"), m_fonts.Body, Theme::Text, DT_CENTER | DT_VCENTER);
    RECT dup = { box.left + 24, box.top + L::C_DUP_FIELD, box.left + 201, box.top + L::C_DUP_FIELD + L::C_FIELD_H };
    Theme::OutlineBox(hDC, dup, Theme::Background, Theme::Border);
    Theme::DrawLine(hDC, RECT{ dup.left + 4, dup.top, dup.right - 4, dup.bottom },
        GetDuplicateCodes(), m_fonts.Small, Theme::DuplicateText,
        DT_CENTER | DT_VCENTER | DT_END_ELLIPSIS);

    return box.bottom;
}

int CGalaxyATMSystemRadarScreen::DrawBlockUnits(HDC hDC, int y)
{
    RECT box = DrawBlockFrame(hDC, y, Tr(L"Ед. изм."), L::UNITS_BOX_H);

    const int col1  = box.left + 5;
    const int colM  = box.left + 70;
    const int colFM = box.left + 140;
    const int col2  = box.left + 119;

    int cy = box.top + L::E_TOP;
    auto row = [&]() { int r = cy; cy += L::E_PITCH; return r; };

    int r1 = row();
    DrawRadioRow(hDC, r1, col1,  colM,        L"FL",   Plugin()->UnitAlt() == AltUnit::FL,  SO_UNIT_ALT_FL,  "U_ALT_FL",  Tr("Эшелон"));
    DrawRadioRow(hDC, r1, colM,  colFM,       L"M",    Plugin()->UnitAlt() == AltUnit::M,   SO_UNIT_ALT_M,   "U_ALT_M",   Tr("Метры"));
    DrawRadioRow(hDC, r1, colFM, box.right,   L"FL+M", Plugin()->UnitAlt() == AltUnit::FLM, SO_UNIT_ALT_FLM, "U_ALT_FLM", Tr("Эшелон и метры"));

    int r2 = row();
    DrawRadioRow(hDC, r2, col1, col2,       Tr(L"Фут / м"), Plugin()->UnitVs() == VsUnit::FtMin, SO_UNIT_VS_FTM, "U_VS_FTM", Tr("Футы в минуту"));
    DrawRadioRow(hDC, r2, col2, box.right,  Tr(L"М / С"),   Plugin()->UnitVs() == VsUnit::MS,    SO_UNIT_VS_MS,  "U_VS_MS",  Tr("Метры в секунду"));

    int r3 = row();
    DrawRadioRow(hDC, r3, col1, col2,       Tr(L"Узлы"),  Plugin()->UnitGs() == GsUnit::Knots, SO_UNIT_GS_KT,  "U_GS_KT",  Tr("Узлы"));
    DrawRadioRow(hDC, r3, col2, box.right,  Tr(L"Км / ч"), Plugin()->UnitGs() == GsUnit::Kmh,  SO_UNIT_GS_KMH, "U_GS_KMH", Tr("Километры в час"));

    int r4 = row();
    DrawRadioRow(hDC, r4, col1, col2,       Tr(L"Мили"), Plugin()->UnitDist() == DistUnit::NM, SO_UNIT_DIST_NM, "U_DIST_NM", Tr("Морские мили"));
    DrawRadioRow(hDC, r4, col2, box.right,  Tr(L"Км"),   Plugin()->UnitDist() == DistUnit::Km, SO_UNIT_DIST_KM, "U_DIST_KM", Tr("Километры"));

    return box.bottom;
}

int CGalaxyATMSystemRadarScreen::DrawBlockAerodrome(HDC hDC, int y)
{
    const Config& cfg = Plugin()->GetConfig();

    RECT box = DrawBlockFrame(hDC, y, cfg.Airport(), L::AERODROME_BOX_H);

    const int kTagLeft   = 5,  kTagRight  = 57;
    const int kValLeft   = 63;

    int cy = box.top + L::A_TOP;
    RECT davlTag = { box.left + kTagLeft, cy, box.left + kTagRight, cy + L::A_ROW };
    RECT davlVal = { box.left + kValLeft, cy, box.left + 199, cy + L::A_ROW };
    Theme::DrawGhostControl(hDC, davlTag, Tr(L"ДАВЛ"), m_fonts.Body);
    Theme::DrawValueField(hDC, davlVal, Plugin()->QnhMmHg() + L"/" + Plugin()->QnhHpa(),
        m_fonts.Body, true, DT_LEFT | DT_VCENTER);
    cy += L::A_ROW + L::A_GAP;

    RECT epTag = { box.left + kTagLeft, cy, box.left + kTagRight, cy + L::A_ROW };
    RECT epVal = { box.left + kValLeft, cy, box.left + 127, cy + L::A_ROW };
    RECT atisBtn = { box.left + 146, cy, box.left + 199, cy + L::A_ROW };
    Theme::DrawGhostControl(hDC, epTag, Tr(L"Э/П"), m_fonts.Body);
    Theme::DrawValueField(hDC, epVal, Plugin()->TransitionLevel(),
        m_fonts.Body, true, DT_LEFT | DT_VCENTER);

    if (m_atisOpen)
        Theme::DrawValueField(hDC, atisBtn, Tr(L"АТИС"), m_fonts.Body);
    else
        Theme::DrawGhostControl(hDC, atisBtn, Tr(L"АТИС"), m_fonts.Body);
    AddButton(hDC, SO_ATIS_BUTTON, "ATIS_BTN", atisBtn,
        Tr("Открыть текст АТИС"));

    return box.bottom;
}

void CGalaxyATMSystemRadarScreen::SetVvGainFrom(POINT pt)
{
    int h = m_vvSliderRect.bottom - m_vvSliderRect.top;
    if (h <= 0)
        return;

    int rel = m_vvSliderRect.bottom - pt.y;
    m_vvGain = max(0, min(100, MulDiv(rel, 100, h)));
    ApplyZoomFromSlider();
}

namespace
{
    const double kZoomMinSpanNM = 6.0;
    const double kZoomMaxSpanNM = 900.0;
}

double CGalaxyATMSystemRadarScreen::GainToSpanNM(int gain)
{
    double t = max(0, min(100, gain)) / 100.0;
    return kZoomMaxSpanNM * pow(kZoomMinSpanNM / kZoomMaxSpanNM, t);
}

int CGalaxyATMSystemRadarScreen::SpanNMToGain(double spanNM)
{
    if (spanNM <= 0.0)
        return 100;
    double t = log(spanNM / kZoomMaxSpanNM) / log(kZoomMinSpanNM / kZoomMaxSpanNM);
    return max(0, min(100, (int)lround(t * 100.0)));
}

double CGalaxyATMSystemRadarScreen::DisplaySpanNM()
{
    CPosition leftDown, rightUp;
    GetDisplayArea(&leftDown, &rightUp);
    return fabs(rightUp.m_Latitude - leftDown.m_Latitude) * 60.0;
}

double CGalaxyATMSystemRadarScreen::DisplayWidthNM()
{
    CPosition leftDown, rightUp;
    GetDisplayArea(&leftDown, &rightUp);

    double dLon = fabs(rightUp.m_Longitude - leftDown.m_Longitude);
    if (dLon > 180.0)
        dLon = 360.0 - dLon;

    double midLat = (leftDown.m_Latitude + rightUp.m_Latitude) / 2.0;
    return dLon * 60.0 * cos(midLat * M_PI / 180.0);
}

void CGalaxyATMSystemRadarScreen::ApplyZoomFromSlider()
{
    CPosition leftDown, rightUp;
    GetDisplayArea(&leftDown, &rightUp);

    double current = fabs(rightUp.m_Latitude - leftDown.m_Latitude) * 60.0;
    if (current <= 0.0)
        return;

    double k = GainToSpanNM(m_vvGain) / current;
    if (k <= 0.0 || fabs(k - 1.0) < 0.005)
        return;

    double cLat = (leftDown.m_Latitude + rightUp.m_Latitude) / 2.0;
    double cLon = (leftDown.m_Longitude + rightUp.m_Longitude) / 2.0;

    CPosition newLeftDown, newRightUp;
    newLeftDown.m_Latitude  = cLat + (leftDown.m_Latitude  - cLat) * k;
    newLeftDown.m_Longitude = cLon + (leftDown.m_Longitude - cLon) * k;
    newRightUp.m_Latitude   = cLat + (rightUp.m_Latitude   - cLat) * k;
    newRightUp.m_Longitude  = cLon + (rightUp.m_Longitude  - cLon) * k;

    SetDisplayArea(newLeftDown, newRightUp);
}

void CGalaxyATMSystemRadarScreen::SyncSliderFromZoom()
{
    m_vvGain = SpanNMToGain(DisplaySpanNM());
}

void CGalaxyATMSystemRadarScreen::OpenAltFilterPicker(RECT area, bool isFrom)
{
    int current = isFrom ? Plugin()->AltFilterFromFL() : Plugin()->AltFilterToFL();
    char initial[8];
    sprintf_s(initial, "%03d", current);
    GetPlugIn()->OpenPopupEdit(area, isFrom ? FN_ALTFILTER_FROM : FN_ALTFILTER_TO, initial);
}
