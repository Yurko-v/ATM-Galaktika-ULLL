#include "pch.h"
#include "Screen/ScreenCommon.h"

using namespace Galaxy;

void CGalaxyATMSystemRadarScreen::AutoLogin()
{
    if (m_authState != AuthState::LoggedOut || m_loginWindowOpen || Plugin()->TrainingSession())
        return;

    const CGalaxyATMSystemPlugin::LoginState state = Plugin()->MyLogin();
    if (m_autoLoginTried)
    {
        if (state == CGalaxyATMSystemPlugin::LoginState::Done)
            GrantAccess();
        else if (state == CGalaxyATMSystemPlugin::LoginState::Failed && m_authMessage.empty())
        {
            Plugin()->MyLogin(&m_authMessage);
            RequestRefresh();
        }
        return;
    }

    if (state != CGalaxyATMSystemPlugin::LoginState::Idle
        || Plugin()->AccessSuspended() || !Plugin()->LiveConnection() || !Plugin()->ListedOnNetwork()
        || Plugin()->GetConfig().SquawkServerUrl().empty())
        return;

    const CGalaxyATMSystemPlugin::SavedLogin& saved = Plugin()->SavedIdentity();
    if (!saved.Complete())
        return;

    m_autoLoginTried = true;
    Log::Info("auth", "LOGIN sent with the saved CID and name, without asking");
    Plugin()->StartLogin(saved.cid, saved.surname);
}

void CGalaxyATMSystemRadarScreen::SyncAuth()
{
    const bool session = Plugin()->SessionAuthorized();
    if (session && m_authState != AuthState::LoggedIn)
    {
        m_authState = AuthState::LoggedIn;
        m_authMessage.clear();
        CloseLoginWindow();
    }
    else if (!session && m_authState == AuthState::LoggedIn)
    {
        m_authState = AuthState::LoggedOut;
        m_authMessage.clear();
        m_openDropdown = DropdownKind::None;
        m_rulerArmed = false;
        m_rulerPlacing = false;
        CloseLoginWindow();
    }
}

void CGalaxyATMSystemRadarScreen::GrantAccess()
{
    if (m_authState != AuthState::LoggedOut)
        return;
    m_authState = AuthState::LoggedIn;
    m_authMessage.clear();
    Plugin()->SetSessionAuthorized(true);
    RequestRefresh();
}

void CGalaxyATMSystemRadarScreen::ShowNotice(const std::wstring& text)
{
    m_noticeText = text;
    RequestRefresh();
}

void CGalaxyATMSystemRadarScreen::DrawNoticeWindow(HDC hDC)
{
    const int kTitleH = 24, kPad = 14, kBtnW = 96, kBtnH = 22, kGap = 12;
    const int W = 420;
    const int textW = W - 2 * (kPad + 5);

    RECT measure = { 0, 0, textW, 0 };
    {
        HFONT old = (HFONT)SelectObject(hDC, m_fonts.Body);
        DrawTextW(hDC, m_noticeText.c_str(), -1, &measure, DT_CALCRECT | DT_WORDBREAK | DT_CENTER);
        SelectObject(hDC, old);
    }
    const int textH = max(18, (int)(measure.bottom - measure.top));
    const int H = kTitleH + kPad + textH + kGap + kBtnH + kPad;

    RECT ra = GetRadarArea();
    RECT win;
    win.left = ra.left + max(0, ((ra.right - ra.left) - W) / 2);
    win.top = ra.top + max(0, ((ra.bottom - ra.top) - H) / 3);
    win.right = win.left + W;
    win.bottom = win.top + H;

    int saved = SaveDC(hDC);
    SetBkMode(hDC, TRANSPARENT);

    HRGN rgn = Theme::WinRegion(win);
    SelectClipRgn(hDC, rgn);
    Theme::FlatFill(hDC, win, Theme::MenuBarFill);
    RECT title = { win.left, win.top, win.right, win.top + kTitleH };
    Theme::DrawLine(hDC, title, Tr(L"Уведомление"), m_fonts.WinTitle, Theme::MenuText, DT_CENTER | DT_VCENTER);
    RECT body = { win.left + 5, title.bottom, win.right - 5, win.bottom - 5 };
    Theme::OutlineBox(hDC, body, Theme::InsetFill, Theme::Border);
    SelectClipRgn(hDC, NULL);
    DeleteObject(rgn);
    Theme::WinBorder(hDC, win, 2, Theme::WinFrame);

    RECT close = { win.right - 26, title.top + 4, win.right - 8, title.bottom - 4 };
    DrawCloseCross(hDC, close, Theme::MenuText);

    AddScreenObject(SO_NOTICE_WINDOW, "NOTICE_WINDOW", win, false, "");
    AddButton(hDC, SO_NOTICE_CLOSE, "NOTICE_CLOSE", close, Tr("Закрыть"));

    RECT textR = { win.left + kPad + 5, title.bottom + kPad, win.right - kPad - 5, title.bottom + kPad + textH };
    HFONT oldFont = (HFONT)SelectObject(hDC, m_fonts.Body);
    SetTextColor(hDC, Theme::Text);
    DrawTextW(hDC, m_noticeText.c_str(), -1, &textR, DT_WORDBREAK | DT_CENTER);
    SelectObject(hDC, oldFont);

    const int okLeft = (win.left + win.right - kBtnW) / 2;
    RECT ok = { okLeft, textR.bottom + kGap, okLeft + kBtnW, textR.bottom + kGap + kBtnH };
    Theme::OutlineBox(hDC, ok, Theme::MenuBarFill, Theme::MenuText);
    Theme::DrawLine(hDC, ok, L"OK", m_fonts.Body, Theme::MenuText, DT_CENTER | DT_VCENTER);
    AddButton(hDC, SO_NOTICE_OK, "NOTICE_OK", ok, Tr("Закрыть"));

    RestoreDC(hDC, saved);
}

void CGalaxyATMSystemRadarScreen::DrawLoginWindow(HDC hDC)
{
    const int kTitleH = 24, kPad = 12, kLine = 18, kFieldH = 24, kRowGap = 6, kLabelW = 84;
    const int kBtnW = 96, kBtnH = 22, kGap = 8;
    const int W = 420;
    const int H = kTitleH + kPad + kLine + kRowGap + LF_COUNT * (kFieldH + kRowGap) + 2 * kLine + kGap + kBtnH + kPad;

    if (m_loginCollapsed)
    {
        DrawCollapsedLogin(hDC);
        return;
    }

    RECT ra = GetRadarArea();
    if (!m_loginPositioned)
    {
        m_loginArea.left = ra.left + max(0, ((ra.right - ra.left) - W) / 2);
        m_loginArea.top  = ra.top + max(0, ((ra.bottom - ra.top) - H) / 3);
        m_loginPositioned = true;
    }
    m_loginArea.left   = max(ra.left, min(m_loginArea.left, ra.right - W));
    m_loginArea.top    = max(ra.top, min(m_loginArea.top, ra.bottom - H));
    m_loginArea.right  = m_loginArea.left + W;
    m_loginArea.bottom = m_loginArea.top + H;
    const RECT win = m_loginArea;

    int saved = SaveDC(hDC);
    SetBkMode(hDC, TRANSPARENT);

    Theme::WinFill(hDC, win, Theme::MenuBarFill);
    RECT title = { win.left, win.top, win.right, win.top + kTitleH };
    Theme::DrawLine(hDC, title, Tr(L"Вход в систему КСА"), m_fonts.WinTitle, Theme::MenuText,
        DT_CENTER | DT_VCENTER);
    RECT body = { win.left + 5, title.bottom, win.right - 5, win.bottom - 5 };
    Theme::SmoothBox(hDC, body, &Theme::InsetFill, &Theme::Border, Theme::WinCornerRadius - 2);
    Theme::WinBorder(hDC, win, 2, Theme::WinFrame);

    RECT close = { win.right - 26, title.top + 4, win.right - 8, title.bottom - 4 };
    DrawCloseCross(hDC, close, Theme::MenuText);
    RECT collapse = { close.left - 22, close.top, close.left - 4, close.bottom };
    DrawCollapseBar(hDC, collapse, Theme::MenuText);

    AddScreenObject(SO_LOGIN_WINDOW, "LOGIN_WINDOW", win, false, "");
    AddScreenObject(SO_LOGIN_HEADER, "LOGIN_HEADER", title, true, Tr("Перетащите окно"));
    AddButton(hDC, SO_LOGIN_CLOSE, "LOGIN_CLOSE", close, Tr("Закрыть"));
    AddButton(hDC, SO_LOGIN_COLLAPSE, "LOGIN_COLLAPSE", collapse, Tr("Свернуть"));

    std::wstring message;
    const CGalaxyATMSystemPlugin::LoginState state = Plugin()->MyLogin(&message);
    const bool sending = (state == CGalaxyATMSystemPlugin::LoginState::Sending);

    const int left = win.left + kPad, right = win.right - kPad;
    int y = title.bottom + kPad;
    RECT intro = { left, y, right, y + kLine };
    Theme::DrawLine(hDC, intro, Tr(L"Введите данные, указанные при регистрации - один раз:"),
        m_fonts.Body, Theme::Text, DT_LEFT | DT_VCENTER);
    y += kLine + kRowGap;

    static const wchar_t* const kLabels[LF_COUNT] = { L"CID", L"Фамилия" };
    static const wchar_t* const kHints[LF_COUNT]  = { L"1234567", L"Иванов" };
    for (int i = 0; i < LF_COUNT; i++)
    {
        RECT label = { left, y, left + kLabelW, y + kFieldH };
        Theme::DrawLine(hDC, label, Tr(kLabels[i]), m_fonts.Body, Theme::Text, DT_LEFT | DT_VCENTER);

        RECT field = { left + kLabelW, y, right, y + kFieldH };
        m_loginFields[i] = field;
        Theme::OutlineBox(hDC, field, Theme::ControlFill, m_entryField == i ? Theme::Text : Theme::BorderStrong);

        RECT text = { field.left + 6, field.top, field.right - 6, field.bottom };
        const std::wstring& value = m_loginValues[i];
        if (m_entryField != i)
        {
            if (value.empty())
                Theme::DrawLine(hDC, text, Tr(kHints[i]), m_fonts.Body, Theme::MenuTextDisabled, DT_LEFT | DT_VCENTER);
            else
                Theme::DrawLine(hDC, text, value, m_fonts.Body, Theme::Text,
                    DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
        }
        if (!sending)
            AddButton(hDC, SO_LOGIN_FIELD, std::to_string(i).c_str(), field,
                Tr("Нажмите, чтобы ввести"));
        y += kFieldH + kRowGap;
    }

    std::wstring status = Tr(L"Enter - следующее поле, Esc - отмена");
    COLORREF statusColor = Theme::TextDim;
    if (sending)
    {
        status = message.empty() ? Tr(L"Проверка...") : message;
        statusColor = Theme::DuplicateText;
    }
    else if (!m_loginProblem.empty())
    {
        status = m_loginProblem;
        statusColor = Theme::DistressText;
    }
    else if (!Plugin()->ListedOnNetwork())
    {
        status = Tr(L"Ждём данных от VATSIM...");
        statusColor = Theme::DuplicateText;
    }
    else if (state == CGalaxyATMSystemPlugin::LoginState::Failed)
    {
        status = message;
        statusColor = Theme::DistressText;
    }
    else
    {
        status = Tr(L"Данные получены, пожалуйста, авторизуйтесь");
        statusColor = Theme::ReadyText;
    }
    RECT statusR = { left, y, right, y + kLine };
    Theme::DrawLine(hDC, statusR, status, m_fonts.Small, statusColor, DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
    y += kLine;

    const std::string registerUrl = Plugin()->RegisterPageUrl();
    if (!registerUrl.empty())
    {
        std::wstring shown = Widen(registerUrl.c_str());
        for (const wchar_t* scheme : { L"http://", L"https://" })
            if (shown.compare(0, wcslen(scheme), scheme) == 0)
                shown.erase(0, wcslen(scheme));
        const std::wstring caption = Tr(L"Регистрация: ");
        RECT captionR = { left, y, right, y + kLine };
        Theme::DrawLine(hDC, captionR, caption, m_fonts.Small, Theme::Text, DT_LEFT | DT_VCENTER);

        const SIZE captionSize = Theme::MeasureText(hDC, m_fonts.Small, caption);
        const SIZE linkSize = Theme::MeasureText(hDC, m_fonts.Small, shown);
        RECT linkR = { left + captionSize.cx, y, min(right, left + captionSize.cx + linkSize.cx), y + kLine };
        const bool hovered = Hot(linkR);
        const COLORREF linkInk = hovered ? Theme::LinkHover : Theme::Link;
        Theme::DrawLine(hDC, linkR, shown, m_fonts.Small, linkInk, DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
        if (hovered)
        {
            const int baseline = (linkR.top + linkR.bottom + linkSize.cy) / 2;
            RECT underline = { linkR.left, baseline - 1, linkR.right, baseline };
            Theme::FlatFill(hDC, underline, linkInk);
        }
        AddButton(hDC, SO_LOGIN_REGISTER, "LOGIN_REGISTER", linkR, Tr("Открыть страницу регистрации в браузере"));
    }
    y += kLine + kGap;

    RECT send = { right - kBtnW, y, right, y + kBtnH };
    const COLORREF ink = sending ? Theme::MenuTextDisabled : Theme::MenuText;
    Theme::OutlineBox(hDC, send, Theme::MenuBarFill, ink);
    Theme::DrawLine(hDC, send, Tr(L"Войти"), m_fonts.Body, ink, DT_CENTER | DT_VCENTER);
    if (!sending)
        AddButton(hDC, SO_LOGIN_SEND, "LOGIN_SEND", send, Tr("Войти в систему"));

    RestoreDC(hDC, saved);

    if (m_entryField >= 0)
        m_entry.Move(m_loginFields[m_entryField]);
    m_loginDrawnTick = GetTickCount64();
}

void CGalaxyATMSystemRadarScreen::DrawCollapsedLogin(HDC hDC)
{
    const int kTitleH = 24, kButtonsW = 52, kPad = 10;
    const std::wstring caption = Tr(L"Вход в систему КСА");
    const int W = (int)Theme::MeasureText(hDC, m_fonts.WinTitle, caption).cx + 2 * kPad + kButtonsW;
    const int H = kTitleH + 4;

    RECT ra = GetRadarArea();
    if (!m_loginCollapsedPlaced)
    {
        m_loginCollapsedArea.left = m_loginArea.left;
        m_loginCollapsedArea.top = m_loginArea.top;
        m_loginCollapsedPlaced = true;
    }
    m_loginCollapsedArea.left   = max(ra.left, min(m_loginCollapsedArea.left, ra.right - W));
    m_loginCollapsedArea.top    = max(ra.top, min(m_loginCollapsedArea.top, ra.bottom - H));
    m_loginCollapsedArea.right  = m_loginCollapsedArea.left + W;
    m_loginCollapsedArea.bottom = m_loginCollapsedArea.top + H;
    const RECT win = m_loginCollapsedArea;

    int saved = SaveDC(hDC);
    SetBkMode(hDC, TRANSPARENT);
    Theme::WinFill(hDC, win, Theme::MenuBarFill);
    Theme::WinBorder(hDC, win, 2, Theme::WinFrame);

    RECT close = { win.right - 26, win.top + 6, win.right - 8, win.bottom - 6 };
    DrawCloseCross(hDC, close, Theme::MenuText);
    RECT expand = { close.left - 22, close.top, close.left - 4, close.bottom };
    DrawExpandBox(hDC, expand, Theme::MenuText);
    RECT title = { win.left + kPad, win.top, expand.left, win.bottom };
    Theme::DrawLine(hDC, title, caption, m_fonts.WinTitle, Theme::MenuText, DT_LEFT | DT_VCENTER);

    RECT header = { win.left, win.top, expand.left, win.bottom };
    AddScreenObject(SO_LOGIN_HEADER, "LOGIN_HEADER", header, true, Tr("Перетащите окно"));
    AddButton(hDC, SO_LOGIN_COLLAPSE, "LOGIN_EXPAND", expand, Tr("Развернуть"));
    AddButton(hDC, SO_LOGIN_CLOSE, "LOGIN_CLOSE", close, Tr("Закрыть"));
    RestoreDC(hDC, saved);
    m_loginDrawnTick = GetTickCount64();
}

void CGalaxyATMSystemRadarScreen::ToggleLoginCollapsed()
{
    CommitEntry();
    m_loginCollapsed = !m_loginCollapsed;
    RequestRefresh();
}

void CGalaxyATMSystemRadarScreen::EditLoginField(int field)
{
    CommitEntry();
    if (field < 0 || field >= LF_COUNT)
        return;

    POINT cursor;
    HWND view = NULL;
    if (CursorRadarPoint(cursor, &view))
        m_entryView = view;

    m_entryPending = field;
    m_entryPendingTick = GetTickCount64();
    RequestRefresh();
}

void CGalaxyATMSystemRadarScreen::TickEntry()
{
    if (m_entry.IsOpen() && (!m_loginWindowOpen || GetTickCount64() - m_loginDrawnTick > 2500))
        CommitEntry();

    if (m_entryPending < 0 || !m_loginWindowOpen || m_loginDrawnTick < m_entryPendingTick)
        return;
    const int field = m_entryPending;
    m_entryPending = -1;
    if (Plugin()->MyLogin() == CGalaxyATMSystemPlugin::LoginState::Sending)
        return;

    const bool opened = m_entryView != NULL && m_entry.Open(m_entryView, m_loginFields[field], m_fonts.Body,
        m_loginValues[field], false, field == LF_CID ? 10 : 40,
        [this, field](TextEntry::End end)
        {
            if (end == TextEntry::End::Cancel)
            {
                m_entry.Close();
                m_entryField = -1;
                RequestRefresh();
            }
            else if (end == TextEntry::End::Submit && field == LF_COUNT - 1)
            {
                SendLogin();
            }
            else
            {
                EditLoginField((field + 1) % LF_COUNT);
            }
        });

    if (opened)
    {
        m_entryField = field;
    }
    else
    {
        Log::Warn("entry", "no edit box of our own over login field " + std::to_string(field)
            + " - EuroScope's popup edit used instead");
        GetPlugIn()->OpenPopupEdit(m_loginFields[field], FN_LOGIN_FIELD + field, Narrow(m_loginValues[field]).c_str());
    }
    RequestRefresh();
}

void CGalaxyATMSystemRadarScreen::CommitEntry()
{
    m_entryPending = -1;
    if (m_entry.IsOpen() && m_entryField >= 0 && m_entryField < LF_COUNT)
    {
        const std::wstring text = TrimSpaces(m_entry.Text());
        std::wstring& value = m_loginValues[m_entryField];
        if (text != value)
        {
            value = text;
            m_loginProblem.clear();
            Plugin()->ResetLogin();
        }
    }
    m_entry.Close();
    m_entryField = -1;
    RequestRefresh();
}

void CGalaxyATMSystemRadarScreen::SendLogin()
{
    CommitEntry();
    if (Plugin()->MyLogin() == CGalaxyATMSystemPlugin::LoginState::Sending)
        return;

    const std::wstring& cid = m_loginValues[LF_CID];
    const bool digits = !cid.empty()
        && cid.find_first_not_of(L"0123456789") == std::wstring::npos;
    if (cid.empty())
    {
        m_loginProblem = Tr(L"Введите свой CID");
    }
    else if (!digits)
    {
        m_loginProblem = Tr(L"CID - это только цифры");
    }
    else if (m_loginValues[LF_SURNAME].empty())
    {
        m_loginProblem = Tr(L"Введите фамилию");
    }
    else if (!Plugin()->ListedOnNetwork())
    {
        m_loginProblem.clear();
    }
    else
    {
        m_loginProblem.clear();
        Plugin()->StartLogin(cid, m_loginValues[LF_SURNAME]);
    }
    RequestRefresh();
}

void CGalaxyATMSystemRadarScreen::CloseLoginWindow()
{
    m_entryPending = -1;
    m_entry.Close();
    m_entryField = -1;
    m_loginProblem.clear();
    m_loginWindowOpen = false;
    m_loginCollapsed = false;
    RequestRefresh();
}
