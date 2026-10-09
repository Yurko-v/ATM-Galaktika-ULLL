#include "pch.h"
#include "Screen/ScreenCommon.h"

using namespace Galaxy;

namespace
{

    std::wstring KeyboardLanguage()
    {
        HWND fg = GetForegroundWindow();
        DWORD thread = (fg != NULL) ? GetWindowThreadProcessId(fg, NULL) : 0;
        HKL layout = GetKeyboardLayout(thread);
        if (layout == NULL)
            return std::wstring();

        const LANGID lang = LOWORD((UINT_PTR)layout);
        wchar_t name[16] = {};
        if (GetLocaleInfoW(MAKELCID(lang, SORT_DEFAULT), LOCALE_SISO639LANGNAME, name, _countof(name)) == 0)
            return std::wstring();
        std::wstring text = name;
        CharUpperBuffW(&text[0], (DWORD)text.size());
        return text;
    }
}

int CGalaxyATMSystemRadarScreen::MenuBarHeight()
{
    return max(L::MENU_BAR_H, Plugin()->GetConfig().AtisTopOffset());
}

void CGalaxyATMSystemRadarScreen::DrawMenuBar(HDC hDC)
{
    RECT ra = GetRadarArea();
    const int top = PanelTop();
    RECT bar = { ra.left, top, ra.right, top + MenuBarHeight() };
    m_menuBarArea = { 0, 0, 0, 0 };
    if (bar.right <= bar.left || bar.bottom <= bar.top)
        return;
    m_menuBarArea = bar;

    int saved = SaveDC(hDC);
    SetBkMode(hDC, TRANSPARENT);

    Theme::FlatFill(hDC, bar, Theme::MenuBarFill);

    AddScreenObject(SO_MENU_BAR, "MENU_BAR", bar, false, "");

    const int kPadX = 8;
    const int kGap  = 9;

    int contentRight = bar.right - kPadX;
    const std::wstring language = KeyboardLanguage();
    if (!language.empty())
    {
        SIZE sz = Theme::MeasureText(hDC, m_fonts.Small, language);
        RECT r = { bar.right - kPadX - sz.cx, bar.top, bar.right - kPadX, bar.bottom };
        Theme::DrawLine(hDC, r, language, m_fonts.Small, Theme::MenuText, DT_LEFT | DT_VCENTER);
        contentRight = r.left - 2 * kGap;
    }

    for (RECT& anchor : m_barAnchors)
        anchor = { 0, 0, 0, 0 };
    int x = bar.left + kPadX;
    for (int i = 0; i < kBarMenuCount; i++)
    {
        const wchar_t* text = Tr(kMenuBarItems[i]);
        HFONT font = m_fonts.Menu;
        SIZE sz = Theme::MeasureText(hDC, font, text);

        if (x + sz.cx > contentRight)
            break;

        RECT r = { x, bar.top, x + sz.cx, bar.bottom };
        // Each name opens its drop-down menu, once logged in.
        const RECT cell = { x - kGap / 2, bar.top + 2, x + sz.cx + kGap / 2, bar.bottom - 2 };
        m_barAnchors[i] = cell;
        if (Authorized())
        {
            if (Hot(cell) || m_barMenu == i)
                Theme::SmoothBox(hDC, cell, &Theme::MenuHover, NULL, 3);
            AddScreenObject(SO_MENU_BAR_ITEM, std::to_string(i).c_str(), cell, false, "");
        }
        Theme::DrawLine(hDC, r, text, font, Theme::MenuText, DT_LEFT | DT_VCENTER);

        x += sz.cx + kGap;
    }

    const int kBtnLead = 40;
    const int kBtnPastCentre = 440;
    const int kBtnPadX = 6;
    const int kBtnGap  = 6;
    const int kBtnH    = 18;
    HFONT btnFont = m_fonts.Small;
    SIZE szLogin  = Theme::MeasureText(hDC, btnFont, L"LOGIN");
    SIZE szBypass = Theme::MeasureText(hDC, btnFont, L"Bypass");
    const int loginW  = szLogin.cx + 2 * kBtnPadX;
    const int bypassW = szBypass.cx + 2 * kBtnPadX;
    int btnTop = bar.top + (bar.bottom - bar.top - kBtnH) / 2;
    const int loginLeft = max(x - kGap + kBtnLead,
        min((bar.left + bar.right) / 2 + kBtnPastCentre, contentRight - loginW - kBtnGap - bypassW));
    RECT login  = { loginLeft, btnTop, loginLeft + loginW, btnTop + kBtnH };
    RECT bypass = { login.right + kBtnGap, btnTop, login.right + kBtnGap + bypassW, btnTop + kBtnH };
    if (bypass.right <= contentRight)
    {
        const bool training = Plugin()->TrainingSession();
        const COLORREF loginInk  = training ? Theme::MenuTextDisabled : Theme::Text;
        Theme::OutlineBox(hDC, login, Theme::MenuBarFill, loginInk);
        Theme::DrawLine(hDC, login, L"LOGIN", btnFont, loginInk, DT_CENTER | DT_VCENTER);
        Theme::OutlineBox(hDC, bypass, Theme::MenuBarFill, Theme::Text);
        Theme::DrawLine(hDC, bypass, L"Bypass", btnFont, Theme::Text, DT_CENTER | DT_VCENTER);
        if (!training)
            AddButton(hDC, SO_AUTH_LOGIN, "MENU_LOGIN", login, Tr("Войти в систему"));
    }

    RestoreDC(hDC, saved);
}
