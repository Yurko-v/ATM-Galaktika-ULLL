#include "pch.h"
#include "Screen/ScreenCommon.h"

using namespace Galaxy;

void CGalaxyATMSystemRadarScreen::DrawCheckbox(HDC hDC, RECT box, bool checked,
    int objType, const char* objId, const char* tooltip)
{
    Theme::OutlineBox(hDC, box, checked ? Theme::Active : Theme::ControlFill, Theme::BorderCheck);
    AddButton(hDC, objType, objId, box, tooltip);
}

void CGalaxyATMSystemRadarScreen::DrawCheckRow(HDC hDC, int top, int x, const std::wstring& label,
    bool checked, int objType, const char* objId, const char* tooltip)
{
    int cy = top + (kRowH - kCheckSize) / 2;
    RECT chk = { x, cy, x + kCheckSize, cy + kCheckSize };
    DrawCheckbox(hDC, chk, checked, objType, objId, tooltip);

    RECT lbl = { chk.right + 6, top, ContentRight(), top + kRowH };
    Theme::DrawLine(hDC, lbl, label, m_fonts.Body, Theme::Text, DT_LEFT | DT_VCENTER);
}

void CGalaxyATMSystemRadarScreen::DrawRadioRow(HDC hDC, int top, int x, int labelRight,
    const std::wstring& label, bool selected, int objType, const char* objId, const char* tooltip)
{
    int cy = top + (kRowH - kRadioSize) / 2;
    RECT pill = { x, cy, x + kRadioSize, cy + kRadioSize };
    Theme::DrawRadio(hDC, pill, selected);
    AddButton(hDC, objType, objId, pill, tooltip);

    RECT lbl = { pill.right + 2, top, labelRight, top + kRowH };
    Theme::DrawLine(hDC, lbl, label, m_fonts.Body, Theme::Text, DT_LEFT | DT_VCENTER);
}

void CGalaxyATMSystemRadarScreen::DrawOutlinedField(HDC hDC, RECT box, const std::wstring& text, HFONT font)
{
    Theme::DrawControl(hDC, box, text, font);
}

void CGalaxyATMSystemRadarScreen::DrawFittedField(HDC hDC, RECT box, const std::wstring& text)
{
    const int avail = (box.right - box.left) - 6;

    HFONT font = m_fonts.Body;
    for (HFONT candidate : { m_fonts.Body, m_fonts.Small, m_fonts.Tiny })
    {
        font = candidate;
        if (Theme::MeasureText(hDC, candidate, text).cx <= avail)
            break;
    }

    Theme::OutlineBox(hDC, box, Theme::ControlFill, Theme::BorderStrong);
    RECT inner = { box.left + 3, box.top, box.right - 3, box.bottom };
    Theme::DrawLine(hDC, inner, text, font, Theme::Text, DT_CENTER | DT_VCENTER | DT_END_ELLIPSIS);
}

void CGalaxyATMSystemRadarScreen::DrawToggleChip(HDC hDC, RECT box, const std::wstring& text,
    bool active, int objType, const char* objId, const char* tooltip, COLORREF idleFill)
{
    if (active)
    {
        Theme::DrawValueField(hDC, box, text, m_fonts.Body, true);
    }
    else
    {
        Theme::OutlineBox(hDC, box, idleFill, Theme::BorderStrong);
        Theme::DrawLine(hDC, box, text, m_fonts.Body, Theme::Text, DT_CENTER | DT_VCENTER);
    }
    AddButton(hDC, objType, objId, box, tooltip);
}

void CGalaxyATMSystemRadarScreen::DrawDropdownField(HDC hDC, RECT box, const std::wstring& text,
    int objType, const char* objId, const char* tooltip)
{
    Theme::OutlineBox(hDC, box, Theme::ButtonMid, Theme::BorderStrong);

    RECT chevron = { box.right - 19, box.top, box.right, box.bottom };

    HPEN pen = CreatePen(PS_SOLID, 1, Theme::BorderStrong);
    HPEN oldPen = (HPEN)SelectObject(hDC, pen);
    MoveToEx(hDC, chevron.left, box.top, NULL);
    LineTo(hDC, chevron.left, box.bottom);
    SelectObject(hDC, oldPen);
    DeleteObject(pen);

    int mx = (chevron.left + chevron.right) / 2, my = (box.top + box.bottom) / 2;
    HBRUSH br = CreateSolidBrush(Theme::Text);
    HBRUSH oldBr = (HBRUSH)SelectObject(hDC, br);
    HPEN dotPen = CreatePen(PS_SOLID, 1, Theme::Text);
    oldPen = (HPEN)SelectObject(hDC, dotPen);
    Ellipse(hDC, mx - 2, my - 2, mx + 2, my + 2);
    SelectObject(hDC, oldPen);
    DeleteObject(dotPen);
    SelectObject(hDC, oldBr);
    DeleteObject(br);

    RECT textRect = { box.left + 5, box.top, chevron.left - 2, box.bottom };
    Theme::DrawLine(hDC, textRect, text, m_fonts.Body, Theme::Text, DT_LEFT | DT_VCENTER);

    AddButton(hDC, objType, objId, chevron, tooltip);
}

void CGalaxyATMSystemRadarScreen::DrawDropdownList(HDC hDC)
{
    const int* values = NULL;
    int count = 0, current = 0;
    RECT anchor = { 0, 0, 0, 0 };

    if (m_openDropdown == DropdownKind::VecDist)
    {
        values = kDistanceSteps; count = kDistanceStepsCount;
        current = m_vecDistKm;   anchor = m_vecDistFieldRect;
    }
    else if (m_openDropdown == DropdownKind::VecTime)
    {
        values = kTimeSteps; count = kTimeStepsCount;
        current = m_vecTimeMin; anchor = m_vecTimeFieldRect;
    }
    else if (m_openDropdown == DropdownKind::OsFont)
    {
        values = kFontSizeSteps; count = kFontSizeStepsCount;
        current = Plugin()->TagFontSize(); anchor = m_osFontFieldRect;
    }
    else
    {
        return;
    }

    const int rowH = 17;
    const int listW = max(anchor.right - anchor.left, 44);
    const int listH = count * rowH + 2;

    RECT list = { anchor.left, anchor.bottom + 2, anchor.left + listW, anchor.bottom + 2 + listH };

    RECT ra = GetRadarArea();
    if (list.bottom > ra.bottom)
        OffsetRect(&list, 0, -(listH + (anchor.bottom - anchor.top) + 4));
    if (list.right > ra.right)
        OffsetRect(&list, ra.right - list.right, 0);
    if (list.left < ra.left)
        OffsetRect(&list, ra.left - list.left, 0);
    m_dropdownListRect = list;

    int saved = SaveDC(hDC);
    SetBkMode(hDC, TRANSPARENT);
    Theme::OutlineBox(hDC, list, Theme::ControlFill, Theme::BorderStrong);

    for (int i = 0; i < count; i++)
    {
        RECT row = { list.left + 1, list.top + 1 + i * rowH, list.right - 1, list.top + 1 + (i + 1) * rowH };

        wchar_t label[16];
        swprintf_s(label, L"%d", values[i]);

        bool selected = (values[i] == current);
        if (selected)
            Theme::FillBox(hDC, row, Theme::Active);
        Theme::DrawLine(hDC, row, label, m_fonts.Body,
            selected ? Theme::ActiveText : Theme::Text, DT_CENTER | DT_VCENTER);

        char id[8];
        sprintf_s(id, "%d", i);
        AddButton(hDC, SO_DROPDOWN_ITEM, id, row, "");
    }

    RestoreDC(hDC, saved);
}

RECT CGalaxyATMSystemRadarScreen::DrawBlockFrame(HDC hDC, int top, const std::wstring& caption, int boxHeight)
{
    RECT captionRect = { m_panelArea.left, top, m_panelArea.right, top + L::CAPTION_H };
    Theme::DrawLine(hDC, captionRect, caption, m_fonts.Body, Theme::Text, DT_CENTER | DT_VCENTER);

    RECT box = { GroupLeft(), captionRect.bottom + L::CAP_GAP, GroupRight(),
                 captionRect.bottom + L::CAP_GAP + boxHeight };
    Theme::OutlineBox(hDC, box, Theme::Background, Theme::Border);
    return box;
}

RECT CGalaxyATMSystemRadarScreen::DrawBoxOnly(HDC hDC, int top, int boxHeight)
{
    RECT box = { GroupLeft(), top, GroupRight(), top + boxHeight };
    Theme::OutlineBox(hDC, box, Theme::Background, Theme::Border);
    return box;
}

bool CGalaxyATMSystemRadarScreen::Hot(const RECT& r)
{
    m_hotRects.push_back(r);
    return m_hotValid && PtInRect(&r, m_hotCursor);
}

void CGalaxyATMSystemRadarScreen::AddButton(HDC, int type, const char* id, RECT r, const char* tip)
{
    AddScreenObject(type, id, r, false, tip);
}

void CGalaxyATMSystemRadarScreen::AddHotButton(HDC hDC, int type, const char* id, RECT r, const char* tip)
{
    AddScreenObject(type, id, r, false, tip);
    if (Hot(r))
        FillAlpha(hDC, r, Theme::HoverFill, Theme::HoverAlpha);
}

void CGalaxyATMSystemRadarScreen::DragPopup(PopupPlacement& popup, const RECT& area, POINT pt, bool released)
{
    if (!popup.dragging)
    {
        popup.dragging = true;
        popup.grab = { pt.x - area.left, pt.y - area.top };
    }
    popup.placed = true;
    popup.active = true;
    popup.topLeft = { pt.x - popup.grab.x, pt.y - popup.grab.y };
    if (released)
        popup.dragging = false;
    RequestRefresh();
}

RECT CGalaxyATMSystemRadarScreen::PlacePopup(const PopupPlacement& popup, int left, int top, int width, int height)
{
    if (popup.placed)
    {
        const RECT ra = GetRadarArea();
        left = max(ra.left, min((int)popup.topLeft.x, (int)ra.right - width));
        top = max(ra.top, min((int)popup.topLeft.y, (int)ra.bottom - height));
    }
    RECT area = { left, top, left + width, top + height };
    return area;
}

void CGalaxyATMSystemRadarScreen::TrackPopupActive(PopupPlacement& popup, const RECT& area, bool onRadar, POINT cursor)
{
    const bool active = popup.dragging || (onRadar && PtInRect(&area, cursor));
    if (active != popup.active)
    {
        popup.active = active;
        RequestRefresh();
    }
}

COLORREF CGalaxyATMSystemRadarScreen::PopupFrame(const PopupPlacement& popup) const
{
    return popup.active ? Theme::PopupFrameActive : Theme::PopupFrameInactive;
}

void CGalaxyATMSystemRadarScreen::TickHot()
{
    POINT cursor;
    HWND view = NULL;
    m_hotValid = CursorRadarPoint(cursor, &view);
    if (m_hotValid)
    {
        m_hotCursor = cursor;
        RadarCursor::Attach(view, GetRadarArea());
    }
    int index = -1;
    if (m_hotValid)
        for (int i = (int)m_hotRects.size() - 1; i >= 0 && index < 0; i--)
            if (PtInRect(&m_hotRects[i], cursor))
                index = i;
    if (index != m_hotIndex)
    {
        m_hotIndex = index;
        m_panelDirty = true;
        RequestRefresh();
    }
}
