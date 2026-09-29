#include "pch.h"
#include "Screen/ScreenCommon.h"

using namespace Galaxy;

void CGalaxyATMSystemRadarScreen::DrawAtisWindow(HDC hDC)
{
    const int W = 370, H = 430;
    const int kTitleH    = 24;
    const int kSide      = 10;
    const int kTrackW    = 10;
    const int kScrollGap = 4;
    const int kEndBtn    = 12;
    const int kButtonH   = 22;
    const int kButtonW   = 60;

    RECT ra = GetRadarArea();
    if (!m_atisPositioned)
    {
        const bool haveStrip = (m_atisLetterArea.bottom > m_atisLetterArea.top);
        m_atisArea.left = haveStrip ? m_atisLetterArea.left : ra.left + 8;
        m_atisArea.top = haveStrip ? m_atisLetterArea.bottom + 6
                                   : PanelTop() + MenuBarHeight();
        m_atisPositioned = true;
    }

    if (m_atisArea.left + W > ra.right)
        m_atisArea.left = ra.right - W;
    if (m_atisArea.top + H > ra.bottom)
        m_atisArea.top = ra.bottom - H;
    if (m_atisArea.left < ra.left)
        m_atisArea.left = ra.left;
    if (m_atisArea.top < ra.top)
        m_atisArea.top = ra.top;

    m_atisArea.right = m_atisArea.left + W;
    m_atisArea.bottom = m_atisArea.top + H;

    int saved = SaveDC(hDC);
    SetBkMode(hDC, TRANSPARENT);

    Theme::WinFill(hDC, m_atisArea, Theme::MenuBarFill);

    RECT title = { m_atisArea.left, m_atisArea.top, m_atisArea.right, m_atisArea.top + kTitleH };
    const std::wstring caption = m_atisIcao.empty() ? std::wstring(L"ATIS message")
                                                    : L"ATIS " + Widen(m_atisIcao.c_str());
    Theme::DrawLine(hDC, title, caption, m_fonts.WinTitle, Theme::MenuText, DT_CENTER | DT_VCENTER);
    AddScreenObject(SO_ATIS_HEADER, "ATIS_HEADER", title, true, Tr("Перетащите окно АТИС"));

    RECT body = { m_atisArea.left + 5, title.bottom, m_atisArea.right - 5, m_atisArea.bottom - 5 };
    Theme::SmoothBox(hDC, body, &Theme::InsetFill, &Theme::Border, Theme::WinCornerRadius - 2);

    RECT close = { m_atisArea.right - 26, title.top + 4, m_atisArea.right - 8, title.bottom - 4 };
    DrawCloseCross(hDC, close, Theme::MenuText);
    AddButton(hDC, SO_ATIS_CLOSE, "ATIS_CLOSE", close, Tr("Закрыть"));

    RECT indexLabel = { body.left + kSide, body.top + 8, body.right - kSide, body.top + 30 };
    const std::wstring indexCaption = L"Index:   ";
    Theme::DrawLine(hDC, indexLabel, indexCaption, m_fonts.MonoBig, Theme::Text, DT_LEFT | DT_VCENTER);
    RECT indexLetter = indexLabel;
    indexLetter.left += Theme::MeasureText(hDC, m_fonts.MonoBig, indexCaption).cx;
    Theme::DrawLine(hDC, indexLetter, Plugin()->AtisIndex(m_atisIcao), m_fonts.MonoBig, Theme::AtisIndexText,
        DT_LEFT | DT_VCENTER);

    RECT ok = { body.right - kSide - kButtonW, body.bottom - 10 - kButtonH, body.right - kSide, body.bottom - 10 };
    Theme::OutlineBox(hDC, ok, Theme::MenuBarFill, Theme::MenuText);
    Theme::DrawLine(hDC, ok, L"OK", m_fonts.Body, Theme::MenuText, DT_CENTER | DT_VCENTER);
    AddButton(hDC, SO_ATIS_OK, "ATIS_OK", ok, Tr("Закрыть"));

    RECT panel = { body.left + kSide, indexLabel.bottom + 10, body.right - kSide, ok.top - 8 };
    Theme::OutlineBox(hDC, panel, Theme::ControlFill, Theme::Border);

    RECT track = { panel.right - kScrollGap - kTrackW, panel.top + kScrollGap,
                   panel.right - kScrollGap, panel.bottom - kScrollGap };
    RECT textArea = { panel.left + 8, panel.top + 6, track.left - 6, panel.bottom - 6 };

    const std::wstring atisText = Plugin()->AtisMessage(m_atisIcao);

    HFONT oldFont = (HFONT)SelectObject(hDC, m_fonts.Mono);
    RECT calc = { 0, 0, textArea.right - textArea.left, 0 };
    DrawTextW(hDC, atisText.c_str(), -1, &calc, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_CALCRECT);
    SelectObject(hDC, oldFont);

    int viewH = textArea.bottom - textArea.top;
    int totalH = calc.bottom - calc.top;
    m_atisScrollMax = max(0, totalH - viewH);
    m_atisScrollPx = max(0, min(m_atisScrollMax, m_atisScrollPx));

    HRGN clip = CreateRectRgn(textArea.left, textArea.top, textArea.right, textArea.bottom);
    SelectClipRgn(hDC, clip);

    RECT scrolled = textArea;
    OffsetRect(&scrolled, 0, -m_atisScrollPx);
    scrolled.bottom = scrolled.top + totalH + 1;

    oldFont = (HFONT)SelectObject(hDC, m_fonts.Mono);
    SetTextColor(hDC, Theme::Text);
    DrawTextW(hDC, atisText.c_str(), -1, &scrolled, DT_LEFT | DT_TOP | DT_WORDBREAK);
    SelectObject(hDC, oldFont);

    SelectClipRgn(hDC, NULL);
    DeleteObject(clip);

    RECT btnUp = { track.left, track.top, track.right, track.top + kEndBtn };
    RECT btnDn = { track.left, track.bottom - kEndBtn, track.right, track.bottom };
    AddButton(hDC, SO_ATIS_LINE_UP, "ATIS_UP", btnUp, Tr("Прокрутить вверх"));
    AddButton(hDC, SO_ATIS_LINE_DN, "ATIS_DN", btnDn, Tr("Прокрутить вниз"));
    DrawScrollArrow(hDC, btnUp, true, Theme::TextDim);
    DrawScrollArrow(hDC, btnDn, false, Theme::TextDim);

    RECT bar = { track.left, btnUp.bottom + 2, track.right, btnDn.top - 2 };
    Theme::SmoothBox(hDC, bar, &Theme::InsetFill, NULL, (kTrackW + 1) / 2);
    int trackH = max(1, (int)(bar.bottom - bar.top));
    m_atisThumbH = (totalH > viewH) ? max(18, (int)((__int64)trackH * viewH / totalH)) : trackH;
    int thumbTop = bar.top;
    if (m_atisScrollMax > 0)
        thumbTop += (int)((__int64)(trackH - m_atisThumbH) * m_atisScrollPx / m_atisScrollMax);

    RECT thumb = { bar.left, thumbTop, bar.right, thumbTop + m_atisThumbH };
    Theme::SmoothBox(hDC, thumb, &Theme::Active, NULL, (kTrackW + 1) / 2);
    AddScreenObject(SO_ATIS_SCROLLBAR, "ATIS_SCROLL", bar, true, Tr("Прокрутка текста АТИС"));

    Theme::WinBorder(hDC, m_atisArea, 2, Theme::WinFrame);

    RestoreDC(hDC, saved);
}

void CGalaxyATMSystemRadarScreen::DrawAtisLetterWindow(HDC hDC)
{
    struct Line
    {
        std::wstring label;
        std::wstring letter;
        std::string icao;
    };

    const std::vector<std::string> airports = Plugin()->AtisAirportsOnAir();
    std::vector<Line> lines;
    if (airports.size() <= 1)
    {
        lines.push_back({ L"INDEX ATIS: ", Plugin()->AtisIndex(), std::string() });
    }
    else
    {
        lines.push_back({ L"INDEX ATIS", std::wstring(), std::string() });
        for (const std::string& icao : airports)
            lines.push_back({ Widen(icao.c_str()) + L": ", Plugin()->AtisIndex(icao), icao });
    }

    const int kFrame = 2;
    const int kPadX  = 7;
    const int kPadY  = 3;

    HFONT font = m_fonts.Mono;

    int labelW = 0, letterW = 0, lineH = 0;
    for (const Line& line : lines)
    {
        const SIZE label = Theme::MeasureText(hDC, font, line.label);
        const SIZE letter = Theme::MeasureText(hDC, font, line.letter);
        labelW = max(labelW, (int)label.cx);
        letterW = max(letterW, (int)letter.cx);
        lineH = max(lineH, (int)max(label.cy, letter.cy));
    }
    const int W = labelW + letterW + 2 * (kFrame + kPadX);
    const int H = (int)lines.size() * lineH + 2 * (kFrame + kPadY);

    RECT ra = GetRadarArea();
    if (!m_atisLetterPositioned)
    {
        m_atisLetterArea.left = ra.left;
        m_atisLetterArea.top  = PanelTop() + MenuBarHeight();
        m_atisLetterPositioned = true;
    }
    if (m_atisLetterArea.left + W > ra.right)
        m_atisLetterArea.left = ra.right - W;
    if (m_atisLetterArea.top + H > ra.bottom)
        m_atisLetterArea.top = ra.bottom - H;
    if (m_atisLetterArea.left < ra.left)
        m_atisLetterArea.left = ra.left;
    if (m_atisLetterArea.top < ra.top)
        m_atisLetterArea.top = ra.top;

    m_atisLetterArea.right  = m_atisLetterArea.left + W;
    m_atisLetterArea.bottom = m_atisLetterArea.top + H;

    int saved = SaveDC(hDC);
    SetBkMode(hDC, TRANSPARENT);

    Theme::OutlineBox(hDC, m_atisLetterArea, Theme::AtisStripFill, Theme::Border);

    const int textLeft = m_atisLetterArea.left + kFrame + kPadX;
    for (size_t i = 0; i < lines.size(); i++)
    {
        const int top = m_atisLetterArea.top + kFrame + kPadY + (int)i * lineH;
        const Line& line = lines[i];
        const int labelRight = textLeft + labelW;
        RECT labelR = { textLeft, top, labelRight, top + lineH };
        RECT letterR = { labelRight, top, labelRight + letterW, top + lineH };
        Theme::DrawLine(hDC, labelR, line.label, font, Theme::Text, DT_LEFT | DT_VCENTER);
        Theme::DrawLine(hDC, letterR, line.letter, font, Theme::AtisIndexText, DT_LEFT | DT_VCENTER);

        RECT row = { m_atisLetterArea.left, i == 0 ? m_atisLetterArea.top : top,
                     m_atisLetterArea.right, i + 1 == lines.size() ? m_atisLetterArea.bottom : top + lineH };
        if (i == 0)
            AddScreenObject(SO_ATIS_LETTER_HEADER, "ATIS_L_HEADER", row, true,
                lines.size() == 1 ? Tr("ЛКМ - текст АТИС, тянуть - переместить, .atis - скрыть")
                                  : Tr("Тянуть - переместить, .atis - скрыть"));
        else
            AddScreenObject(SO_ATIS_LETTER_ROW, line.icao.c_str(), row, false, Tr("ЛКМ - текст АТИС"));
    }

    RestoreDC(hDC, saved);
}

void CGalaxyATMSystemRadarScreen::ScrollAtisTo(POINT pt, RECT track)
{
    int usable = (track.bottom - track.top) - m_atisThumbH;
    if (usable <= 0 || m_atisScrollMax <= 0)
        return;

    int rel = pt.y - track.top - m_atisThumbH / 2;
    rel = max(0, min(usable, rel));
    m_atisScrollPx = (int)((__int64)rel * m_atisScrollMax / usable);
}
