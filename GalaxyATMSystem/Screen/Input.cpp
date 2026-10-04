#include "pch.h"
#include "Screen/ScreenCommon.h"

using namespace Galaxy;

namespace
{
    HHOOK g_wheelHook = NULL;
    CGalaxyATMSystemRadarScreen* g_wheelScreen = NULL;

    LRESULT CALLBACK MouseHookProc(int code, WPARAM wp, LPARAM lp)
    {
        if (code == HC_ACTION && wp == WM_MOUSEWHEEL && g_wheelScreen != NULL)
        {
            const MOUSEHOOKSTRUCTEX* ms = (const MOUSEHOOKSTRUCTEX*)lp;
            const int delta = (short)HIWORD(ms->mouseData);
            if (g_wheelScreen->OnMouseWheel(delta))
                return 1;
        }
        if (code == HC_ACTION && (wp == WM_XBUTTONDOWN || wp == WM_XBUTTONDBLCLK) && g_wheelScreen != NULL
            && g_wheelScreen->OnSideButton())
            return 1;
        if (code == HC_ACTION && g_wheelScreen != NULL
            && (wp == WM_MOUSEMOVE || wp == WM_RBUTTONDOWN || wp == WM_RBUTTONUP)
            && g_wheelScreen->OnMouseButton(wp, ((const MOUSEHOOKSTRUCT*)lp)->pt))
            return 1;
        return CallNextHookEx(g_wheelHook, code, wp, lp);
    }
}

void CGalaxyATMSystemRadarScreen::UpdateWheelHook()
{
    if (WantsWheel())
    {
        if (g_wheelScreen == NULL || m_cflOpen || m_spdOpen || m_ahdgOpen || m_xfrOpen || m_atisOpen)
            g_wheelScreen = this;
    }
    else if (g_wheelScreen == this)
    {
        g_wheelScreen = NULL;
        for (CGalaxyATMSystemRadarScreen* other : g_screens)
        {
            if (other != this && other->WantsWheel())
            {
                g_wheelScreen = other;
                break;
            }
        }
    }

    if (g_wheelScreen != NULL && g_wheelHook == NULL)
    {
        g_wheelHook = SetWindowsHookExW(WH_MOUSE, MouseHookProc, NULL, GetCurrentThreadId());
    }
    else if (g_wheelScreen == NULL && g_wheelHook != NULL)
    {
        UnhookWindowsHookEx(g_wheelHook);
        g_wheelHook = NULL;
    }
}

void CGalaxyATMSystemRadarScreen::ReleaseWheelHook()
{
    g_wheelScreen = NULL;
    if (g_wheelHook != NULL)
    {
        UnhookWindowsHookEx(g_wheelHook);
        g_wheelHook = NULL;
    }
}

bool CGalaxyATMSystemRadarScreen::OnMouseWheel(int delta)
{
    m_panelDirty = true;
    if (delta == 0 || !Authorized())
        return false;
    const int rows = delta > 0 ? -max(1, delta / WHEEL_DELTA) : max(1, -delta / WHEEL_DELTA);
    if (WheelMapCircle(rows))
        return true;
    POINT cursor;
    if (m_cflOpen && CflCursor(cursor) && PtInRect(&m_cflArea, cursor))
    {
        ScrollCfl(rows);
        return true;
    }
    if (m_rcFloating && ScrollSectorList(rows))
        return true;
    if (!CursorRadarPoint(cursor))
        return false;
    if (m_spdOpen && PtInRect(&m_spdArea, cursor))
    {
        ScrollSpeed(rows);
        return true;
    }
    if (m_ahdgOpen && PtInRect(&m_ahdgArea, cursor))
    {
        ScrollHeading(rows);
        return true;
    }
    if (m_xfrOpen && PtInRect(&m_xfrArea, cursor))
    {
        ScrollTransfer(rows);
        return true;
    }
    if (m_atisOpen && PtInRect(&m_atisArea, cursor))
    {
        m_atisScrollPx = max(0, min(m_atisScrollMax, m_atisScrollPx + rows * kAtisLinePx));
        RequestRefresh();
        return true;
    }
    if (!m_rcFloating && ScrollSectorList(rows))
        return true;
    return WheelDropdown(cursor, rows);
}

bool CGalaxyATMSystemRadarScreen::WheelDropdown(POINT cursor, int rows)
{
    DropdownKind kind = DropdownKind::None;
    if (m_openDropdown != DropdownKind::None && PtInRect(&m_dropdownListRect, cursor))
        kind = m_openDropdown;
    else if (PtInRect(&m_vecDistFieldRect, cursor))
        kind = DropdownKind::VecDist;
    else if (PtInRect(&m_vecTimeFieldRect, cursor))
        kind = DropdownKind::VecTime;
    else if (PtInRect(&m_osFontFieldRect, cursor))
        kind = DropdownKind::OsFont;

    const int* values = NULL;
    int count = 0, current = 0;
    switch (kind)
    {
    case DropdownKind::VecDist:
        values = kDistanceSteps; count = kDistanceStepsCount; current = m_vecDistKm;
        break;
    case DropdownKind::VecTime:
        values = kTimeSteps; count = kTimeStepsCount; current = m_vecTimeMin;
        break;
    case DropdownKind::OsFont:
        values = kFontSizeSteps; count = kFontSizeStepsCount; current = Plugin()->TagFontSize();
        break;
    default:
        return false;
    }

    int at = 0;
    while (at < count - 1 && values[at] < current)
        at++;
    const int picked = values[max(0, min(count - 1, at + rows))];

    if (kind == DropdownKind::VecDist)
        m_vecDistKm = picked;
    else if (kind == DropdownKind::VecTime)
        m_vecTimeMin = picked;
    else
        Plugin()->SetTagFontSize(picked);
    RequestRefresh();
    return true;
}

void CGalaxyATMSystemRadarScreen::OnClickScreenObject(int ObjectType, const char* sObjectId,
    POINT Pt, RECT Area, int Button)
{
    m_panelDirty = true;
    m_lastObjectClickTick = GetTickCount64();
    switch (ObjectType)
    {
    case SO_MAP_MENU:
        return;
    case SO_MAP_MENU_ITEM:
    {
        const int item = atoi(sObjectId);
        if (item >= 0 && item < (int)MapMenuItem::Count)
            RunMapMenuItem((MapMenuItem)item);
        return;
    }
    case SO_MAP_CANVAS:
        MapCanvasClick(Pt, Button);
        return;
    case SO_COORD_MENU:
    case SO_CS_MENU:
        return;
    case SO_CS_MENU_ITEM:
    {
        const int item = atoi(sObjectId);
        if (item >= 0 && item < (int)CallsignMenuItem::Count)
            RunCallsignMenuItem((CallsignMenuItem)item);
        return;
    }
    case SO_COORD_MENU_ITEM:
        DecideCoordination(atoi(sObjectId) == 1);
        return;
    case SO_FT_WINDOW:
        return;
    case SO_COORD_WINDOW:
        return;
    case SO_COORD_ACCEPT:
    case SO_COORD_REJECT:
        ReplyCoordination(sObjectId, ObjectType == SO_COORD_ACCEPT);
        CloseCoordWindow();
        return;
    case SO_COORD_CLOSE:
        CloseCoordWindow();
        return;
    case SO_FT_CLOSE:
    case SO_FT_CANCEL:
        CloseFreeTextWindow();
        return;
    case SO_FT_FIELD:
        if (!m_ftEntry.IsOpen())
        {
            m_ftEntryPending = true;
            m_ftPendingTick = GetTickCount64();
            RequestRefresh();
        }
        return;
    case SO_FT_OK:
        ApplyFreeText();
        return;
    case SO_XFR_WINDOW:
        return;
    case SO_XFR_CLOSE:
        CloseTransferWindow();
        return;
    case SO_XFR_ROW:
        m_xfrSelected = atoi(sObjectId);
        m_xfrEntry.Close();
        RequestRefresh();
        return;
    case SO_XFR_UP:
        ScrollTransfer(-1);
        return;
    case SO_XFR_DOWN:
        ScrollTransfer(1);
        return;
    case SO_XFR_TRACK:
    {
        const int h = m_xfrTrack.bottom - m_xfrTrack.top;
        const int total = (int)m_xfrPositions.size() - kXfrRows;
        if (h > 0 && total > 0)
        {
            const int row = (int)lround((double)(Pt.y - m_xfrTrack.top) / h * total);
            ScrollTransfer(row - m_xfrTopRow);
        }
        return;
    }
    case SO_XFR_FIELD:
        m_xfrEntryPending = true;
        m_xfrPendingTick = GetTickCount64();
        {
            POINT cursor;
            HWND view = NULL;
            if (CursorRadarPoint(cursor, &view))
                m_popupView = view;
        }
        RequestRefresh();
        return;
    case SO_XFR_HANDOFF:
        ApplyTransfer();
        return;
    case SO_XFR_RELEASE:
        ReleaseTransfer();
        return;
    case SO_SPD_WINDOW:
        return;
    case SO_SPD_CLOSE:
    case SO_SPD_CANCEL:
        CloseSpeedWindow();
        return;
    case SO_SPD_ROW:
        m_spdSelected = atoi(sObjectId);
        m_spdEntry.Close();
        RequestRefresh();
        return;
    case SO_SPD_UP:
        ScrollSpeed(-1);
        return;
    case SO_SPD_DOWN:
        ScrollSpeed(1);
        return;
    case SO_SPD_TRACK:
    {
        const int h = m_spdTrack.bottom - m_spdTrack.top;
        if (h > 0)
        {
            const int total = (int)SpeedValues(m_spdMach).size() - kSpdRows;
            const int row = (int)lround((double)(Pt.y - m_spdTrack.top) / h * total);
            ScrollSpeed(row - m_spdTopRow);
        }
        return;
    }
    case SO_SPD_FIELD:
        m_spdEntryPending = true;
        m_spdPendingTick = GetTickCount64();
        {
            POINT cursor;
            HWND view = NULL;
            if (CursorRadarPoint(cursor, &view))
                m_popupView = view;
        }
        RequestRefresh();
        return;
    case SO_SPD_TAB:
        SelectSpeedTab(atoi(sObjectId) == 1);
        return;
    case SO_SPD_MODE:
        m_spdMode = (SpeedMode)max(0, min(2, atoi(sObjectId)));
        RequestRefresh();
        return;
    case SO_SPD_YES:
        ApplySpeed();
        return;
    case SO_RVSM_WINDOW:
        return;
    case SO_RVSM_CLOSE:
        CloseRvsmWindow();
        return;
    case SO_RVSM_ROW:
        if (Button == BUTTON_LEFT)
            ApplyRvsm(atoi(sObjectId));
        return;
    case SO_HDG_WINDOW:
        return;
    case SO_HDG_CLOSE:
    case SO_HDG_CANCEL:
        CloseHeadingWindow();
        return;
    case SO_HDG_ROW:
        m_ahdgSelected = atoi(sObjectId);
        m_ahdgEntry.Close();
        RequestRefresh();
        return;
    case SO_HDG_UP:
        ScrollHeading(-1);
        return;
    case SO_HDG_DOWN:
        ScrollHeading(1);
        return;
    case SO_HDG_TRACK:
    {
        const int h = m_ahdgTrack.bottom - m_ahdgTrack.top;
        if (h > 0)
        {
            const int total = (int)HeadingValues().size() - kHdgRows;
            const int row = (int)lround((double)(Pt.y - m_ahdgTrack.top) / h * total);
            ScrollHeading(row - m_ahdgTopRow);
        }
        return;
    }
    case SO_HDG_FIELD:
        m_ahdgEntryPending = true;
        m_ahdgPendingTick = GetTickCount64();
        {
            POINT cursor;
            HWND view = NULL;
            if (CursorRadarPoint(cursor, &view))
                m_popupView = view;
        }
        RequestRefresh();
        return;
    case SO_HDG_YES:
        ApplyHeading();
        return;
    case SO_CFL_WINDOW:
        return;
    case SO_CFL_LEVEL:
        if (Button == BUTTON_LEFT)
            ApplyCfl(atoi(sObjectId));
        return;
    case SO_CFL_UP:
        ScrollCfl(-1);
        return;
    case SO_CFL_DOWN:
        ScrollCfl(1);
        return;
    case SO_CFL_TRACK:
    {
        const int h = m_cflTrack.bottom - m_cflTrack.top;
        if (h > 0)
        {
            const int total = (int)((CflLevels(!m_cflPicksExitLevel).size() + 1) / 2) - kCflRows;
            const int row = (int)lround((double)(Pt.y - m_cflTrack.top) / h * total);
            ScrollCfl(row - m_cflTopRow);
        }
        return;
    }
    case SO_CFL_FIELD:
        m_cflEntryPending = true;
        m_cflPendingTick = GetTickCount64();
        if (m_cflInList && m_rcFloating)
            m_cflView = m_rcFloat.Handle();
        else
        {
            POINT cursor;
            HWND view = NULL;
            if (CursorRadarPoint(cursor, &view))
                m_cflView = view;
        }
        RequestRefresh();
        return;
    case SO_CFL_OK:
        if (m_cflEntry.IsOpen())
            ApplyCflText(m_cflEntry.Text());
        else
            CloseCflPicker();
        return;
    }

    if (ObjectType == SO_FORMULAR || ObjectType == SO_FORMULAR_AHDG)
    {
        FormularClick(sObjectId, Pt, Button);
        return;
    }

    if (ObjectType == SO_RULER_LINE)
    {
        if (Button == BUTTON_RIGHT)
            RemoveRulerNear(Pt);
        return;
    }

    if (ObjectType == SO_RULER_CANVAS)
    {
        if (Button == BUTTON_LEFT)
        {
            PlaceRulerPoint(Pt);
        }
        else if (Button == BUTTON_RIGHT)
        {
            m_rulerArmed = false;
            m_rulerPlacing = false;
        }
        RequestRefresh();
        return;
    }

    if (ObjectType == SO_SIGMET_AREA)
        return;

    if (ObjectType != SO_DROPDOWN_ITEM &&
        ObjectType != SO_VEC_DIST_FIELD && ObjectType != SO_VEC_TIME_FIELD &&
        ObjectType != SO_OS_FONT_FIELD)
    {
        m_openDropdown = DropdownKind::None;
    }

    switch (ObjectType)
    {
    case SO_DROPDOWN_ITEM:
    {
        int idx = atoi(sObjectId);
        if (m_openDropdown == DropdownKind::VecDist && idx >= 0 && idx < kDistanceStepsCount)
            m_vecDistKm = kDistanceSteps[idx];
        else if (m_openDropdown == DropdownKind::VecTime && idx >= 0 && idx < kTimeStepsCount)
            m_vecTimeMin = kTimeSteps[idx];
        else if (m_openDropdown == DropdownKind::OsFont && idx >= 0 && idx < kFontSizeStepsCount)
            Plugin()->SetTagFontSize(kFontSizeSteps[idx]);
        m_openDropdown = DropdownKind::None;
        RequestRefresh();
        break;
    }

    case SO_PANEL_COLLAPSE:
        m_collapsed = !m_collapsed;
        RequestRefresh();
        break;

    case SO_MENU_BAR:
        RequestRefresh();
        break;

    case SO_AUTH_LOGIN:
        if (m_authState == AuthState::LoggedOut && !m_loginWindowOpen && !Plugin()->TrainingSession())
        {
            const char* callsign = GetPlugIn()->ControllerMyself().GetCallsign();
            const std::string who = (callsign != NULL && *callsign != '\0') ? callsign : "(no callsign)";

            m_authMessage.clear();
            if (!Plugin()->LiveConnection())
            {
                m_authMessage = Tr(L"Нет подключения к VATSIM");
                Log::Error("auth", "LOGIN " + who + " failed: not controlling on the live VATSIM network"
                    " (EuroScope connection type " + std::to_string(GetPlugIn()->GetConnectionType()) + ")");
            }
            else if (Plugin()->GetConfig().SquawkServerUrl().empty())
            {
                m_authMessage = Tr(L"База пользователей недоступна");
                Log::Error("auth", "LOGIN " + who + " failed: Squawk.ServerUrl is not set in GalaxyATMSystem.json");
            }
            else
            {
                Log::Info("auth", "LOGIN " + who + ": login window opened");
                m_loginWindowOpen = true;
                m_loginProblem.clear();
                Plugin()->ResetLogin();

                const CGalaxyATMSystemPlugin::SavedLogin& saved = Plugin()->SavedIdentity();
                const std::wstring* const from[LF_COUNT] = { &saved.cid, &saved.surname };
                for (int field = 0; field < LF_COUNT; field++)
                    if (m_loginValues[field].empty())
                        m_loginValues[field] = *from[field];

                int first = LF_COUNT - 1;
                for (int field : { LF_CID, LF_SURNAME })
                {
                    if (m_loginValues[field].empty())
                    {
                        first = field;
                        break;
                    }
                }
                EditLoginField(first);
            }
        }
        RequestRefresh();
        break;

    case SO_NOTICE_WINDOW:
        break;
    case SO_NOTICE_OK:
    case SO_NOTICE_CLOSE:
        m_noticeText.clear();
        RequestRefresh();
        break;

    case SO_LOGIN_WINDOW:
        CommitEntry();
        break;
    case SO_LOGIN_CLOSE:
        CloseLoginWindow();
        break;
    case SO_LOGIN_COLLAPSE:
        ToggleLoginCollapsed();
        break;
    case SO_LOGIN_FIELD:
        EditLoginField(atoi(sObjectId));
        break;
    case SO_LOGIN_SEND:
        SendLogin();
        break;
    case SO_LOGIN_REGISTER:
    {
        CommitEntry();
        const std::string url = Plugin()->RegisterPageUrl();
        if (url.compare(0, 7, "http://") == 0 || url.compare(0, 8, "https://") == 0)
        {
            Log::Info("auth", "registration page opened in the browser: " + url);
            ShellExecuteA(NULL, "open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
        }
        break;
    }

    case SO_TIMER_TOGGLE:
        if (Button == BUTTON_RIGHT)
        {
            m_timerElapsedMs = 0;
            m_timerStartTick = GetTickCount64();
        }
        else if (m_timerRunning)
        {
            m_timerElapsedMs += GetTickCount64() - m_timerStartTick;
            m_timerRunning = false;
        }
        else
        {
            m_timerStartTick = GetTickCount64();
            m_timerRunning = true;
        }
        RequestRefresh();
        break;

    case SO_ALTFILTER_FROM:
        OpenAltFilterPicker(Area, true);
        break;
    case SO_ALTFILTER_TO:
        OpenAltFilterPicker(Area, false);
        break;
    case SO_ALTFILTER_USE_CHK:
        Plugin()->SetAltFilterEnabled(!Plugin()->AltFilterEnabled());
        RequestRefresh();
        break;

    case SO_VEC_DIST_TOGGLE:
        m_vecDistEnabled = !m_vecDistEnabled;
        if (m_vecDistEnabled)
            m_vecTimeEnabled = false;
        RequestRefresh();
        break;
    case SO_VEC_DIST_FIELD:
        m_openDropdown = (m_openDropdown == DropdownKind::VecDist)
            ? DropdownKind::None : DropdownKind::VecDist;
        RequestRefresh();
        break;
    case SO_VEC_TIME_TOGGLE:
        m_vecTimeEnabled = !m_vecTimeEnabled;
        if (m_vecTimeEnabled)
            m_vecDistEnabled = false;
        RequestRefresh();
        break;
    case SO_VEC_TIME_FIELD:
        m_openDropdown = (m_openDropdown == DropdownKind::VecTime)
            ? DropdownKind::None : DropdownKind::VecTime;
        RequestRefresh();
        break;
    case SO_VEC_BY_PLAN_CHK:
        m_vecByPlan = !m_vecByPlan;
        RequestRefresh();
        break;
    case SO_VEC_LEVEL_CHK:
        m_vecShowLevel = !m_vecShowLevel;
        RequestRefresh();
        break;

    case SO_OS_TWO_LINE:   m_osLines = 2;              RequestRefresh(); break;
    case SO_OS_THREE_LINE: m_osLines = 3;              RequestRefresh(); break;
    case SO_OS_SPEED:      m_osSpeed = !m_osSpeed;     RequestRefresh(); break;
    case SO_OS_FONT_FIELD:
        m_openDropdown = (m_openDropdown == DropdownKind::OsFont)
            ? DropdownKind::None : DropdownKind::OsFont;
        RequestRefresh();
        break;

    case SO_CODE_ALL:      m_codeAll = !m_codeAll;     RequestRefresh(); break;
    case SO_CODE_BP:       m_codeBp = !m_codeBp;       RequestRefresh(); break;
    case SO_CODE_EXTRA:    m_codeExtra = !m_codeExtra; RequestRefresh(); break;
    case SO_CODE_FILTER:
    {
        std::string initial = Narrow(m_codeFilter);
        GetPlugIn()->OpenPopupEdit(Area, FN_CODE_FILTER, initial.c_str());
        break;
    }
    case SO_VV_SLIDER:
        SetVvGainFrom(Pt);
        RequestRefresh();
        break;


    case SO_UNIT_ALT_FL:  Plugin()->SetUnitAlt(AltUnit::FL);   RequestRefresh(); break;
    case SO_UNIT_ALT_M:   Plugin()->SetUnitAlt(AltUnit::M);    RequestRefresh(); break;
    case SO_UNIT_ALT_FLM: Plugin()->SetUnitAlt(AltUnit::FLM);  RequestRefresh(); break;
    case SO_UNIT_VS_FTM:  Plugin()->SetUnitVs(VsUnit::FtMin);  RequestRefresh(); break;
    case SO_UNIT_VS_MS:   Plugin()->SetUnitVs(VsUnit::MS);     RequestRefresh(); break;
    case SO_UNIT_GS_KT:   Plugin()->SetUnitGs(GsUnit::Knots);  RequestRefresh(); break;
    case SO_UNIT_GS_KMH:  Plugin()->SetUnitGs(GsUnit::Kmh);    RequestRefresh(); break;
    case SO_UNIT_DIST_NM: Plugin()->SetUnitDist(DistUnit::NM); RequestRefresh(); break;
    case SO_UNIT_DIST_KM: Plugin()->SetUnitDist(DistUnit::Km); RequestRefresh(); break;

    case SO_ATIS_LETTER_HEADER:
        if (m_atisLetterDragged)
        {
            m_atisLetterDragged = false;
            break;
        }
    case SO_ATIS_BUTTON:
        m_atisOpen = !m_atisOpen || !m_atisIcao.empty();
        m_atisIcao.clear();
        m_atisScrollPx = 0;
        UpdateWheelHook();
        RequestRefresh();
        break;
    case SO_ATIS_LETTER_ROW:
    {
        const std::vector<std::string> onAir = Plugin()->AtisAirportsOnAir();
        const std::string icao = (!onAir.empty() && onAir.front() == sObjectId) ? std::string() : std::string(sObjectId);
        m_atisOpen = !m_atisOpen || m_atisIcao != icao;
        m_atisIcao = icao;
        m_atisScrollPx = 0;
        UpdateWheelHook();
        RequestRefresh();
        break;
    }
    case SO_RC_SORT:
    {
        int col = atoi(sObjectId);
        if (col == m_rcSortKey)
            m_rcSortAsc = !m_rcSortAsc;
        else if (col >= 0 && col < kRcCols)
        {
            m_rcSortKey = col;
            m_rcSortAsc = true;
        }
        RequestRefresh();
        break;
    }
    case SO_RC_ROW:
    {
        CFlightPlan picked = GetPlugIn()->FlightPlanSelect(sObjectId);
        if (Button == BUTTON_RIGHT)
        {
            if (picked.IsValid())
            {
                const bool mine = picked.GetTrackingControllerIsMe();
                int& scroll = mine ? m_rcScrollMine : m_rcScroll;
                scroll += kRcRows;
            }
        }
        else if (m_rcPicked.erase(sObjectId) == 0 && picked.IsValid())
        {
            m_rcPicked.insert(sObjectId);
            GetPlugIn()->SetASELAircraft(picked);
        }
        RequestRefresh();
        break;
    }
    case SO_RC_SQUAWK:
    {
        CFlightPlan picked = GetPlugIn()->FlightPlanSelect(sObjectId);
        if (!picked.IsValid() || Button != BUTTON_LEFT)
            break;
        GetPlugIn()->SetASELAircraft(picked);
        Plugin()->HandleSquawkFunction(m_rcClickFromFloat ? TAG_FUNC_SQUAWK_ASSIGN : TAG_FUNC_SQUAWK_MENU,
            "", Area, "sector list");
        RequestRefresh();
        break;
    }
    case SO_RC_XFL:
    {
        CFlightPlan picked = GetPlugIn()->FlightPlanSelect(sObjectId);
        if (!picked.IsValid() || Button != BUTTON_LEFT)
            break;
        OpenCflPicker(sObjectId, true);
        m_cflAnchor = Area;
        m_cflInList = true;
        if (m_rcClickFromFloat)
            m_cflView = m_rcFloat.Handle();
        RequestRefresh();
        break;
    }
    case SO_RC_FILTER:
    {
        const bool isCallsign = strcmp(sObjectId, "callsign") == 0;
        const bool isBefore = strcmp(sObjectId, "before") == 0;
        std::string initial;
        if (isCallsign)
            initial = Narrow(m_rcFilterCallsign);
        else
        {
            const int value = isBefore ? m_rcFilterBefore : m_rcFilterAfter;
            if (value >= 0)
                initial = std::to_string(value);
        }
        GetPlugIn()->OpenPopupEdit(Area,
            isCallsign ? FN_RC_FILTER_CALLSIGN : isBefore ? FN_RC_FILTER_BEFORE : FN_RC_FILTER_AFTER,
            initial.c_str());
        break;
    }
    case SO_RC_CLOSE:
        m_rcOpen = false;
        RequestRefresh();
        break;

    case SO_ATIS_OK:
    case SO_ATIS_CLOSE:
        m_atisOpen = false;
        UpdateWheelHook();
        RequestRefresh();
        break;
    case SO_ATIS_SCROLLBAR:
        ScrollAtisTo(Pt, Area);
        RequestRefresh();
        break;
    case SO_ATIS_LINE_UP:
        m_atisScrollPx = max(0, m_atisScrollPx - kAtisLinePx);
        RequestRefresh();
        break;
    case SO_ATIS_LINE_DN:
        m_atisScrollPx = min(m_atisScrollMax, m_atisScrollPx + kAtisLinePx);
        RequestRefresh();
        break;

    default:
        break;
    }
}

void CGalaxyATMSystemRadarScreen::OnButtonDownScreenObject(int ObjectType, const char* sObjectId,
    POINT Pt, RECT Area, int Button)
{
    m_panelDirty = true;
    if (Button != BUTTON_LEFT)
        return;

    if (ObjectType == SO_ATIS_LETTER_HEADER)
        m_atisLetterDragged = false;

    if (ObjectType == SO_SIGMET_AREA)
    {
        int idx = FindSigmetAt(Pt);
        if (idx < 0)
            return;

        m_sigmetInfoIndex = idx;
        m_sigmetInfoAt = Pt;
        m_sigmetInfoHeld = false;
        m_sigmetInfoWait = 0;
        RequestRefresh();
    }
    else if (ObjectType == SO_ZONE_AREA)
    {
        int idx = FindZoneAt(Pt);
        if (idx < 0)
            idx = ZoneFromObjectId(sObjectId);
        if (idx < 0)
            return;

        m_zoneInfoIndex = idx;
        m_zoneInfoAt = Pt;
        m_zoneInfoHeld = false;
        m_zoneInfoWait = 0;
        RequestRefresh();
    }
}

void CGalaxyATMSystemRadarScreen::OnButtonUpScreenObject(int ObjectType, const char* sObjectId,
    POINT Pt, RECT Area, int Button)
{
    m_panelDirty = true;
    if (Button != BUTTON_LEFT)
        return;

    if (m_sigmetInfoIndex >= 0 || m_zoneInfoIndex >= 0)
    {
        m_sigmetInfoIndex = -1;
        m_zoneInfoIndex = -1;
        RequestRefresh();
    }
}

void CGalaxyATMSystemRadarScreen::OnDoubleClickScreenObject(int ObjectType, const char* sObjectId,
    POINT Pt, RECT Area, int Button)
{
    m_panelDirty = true;
    if (ObjectType == SO_RULER_LINE && Button == BUTTON_LEFT)
        RemoveRulerNear(Pt);
    if (ObjectType == SO_XFR_ROW && Button == BUTTON_LEFT && m_xfrOpen && m_xfrPicksRoutePoint)
    {
        m_xfrSelected = atoi(sObjectId);
        m_xfrEntry.Close();
        DirectToTransferPoint(m_xfrSelected);
    }
}

void CGalaxyATMSystemRadarScreen::OnFunctionCall(int FunctionId, const char* sItemString, POINT Pt, RECT Area)
{
    m_panelDirty = true;
    if (FunctionId >= FN_LOGIN_FIELD && FunctionId < FN_LOGIN_FIELD + LF_COUNT)
    {
        const int field = FunctionId - FN_LOGIN_FIELD;
        const std::wstring typed = (sItemString != NULL) ? Widen(sItemString) : std::wstring();
        m_loginValues[field] = TrimSpaces(typed);
        m_loginProblem.clear();
        Plugin()->ResetLogin();
        RequestRefresh();
        return;
    }

    if (!Authorized())
        return;

    Plugin()->HandleSquawkFunction(FunctionId, sItemString, Area, "screen");

    if (FunctionId == FN_RC_FILTER_CALLSIGN || FunctionId == FN_RC_FILTER_BEFORE
        || FunctionId == FN_RC_FILTER_AFTER)
    {
        ApplyRcFilter(FunctionId, (sItemString != NULL) ? Widen(sItemString) : std::wstring());
        return;
    }

    if (FunctionId == FN_CODE_FILTER)
    {
        m_codeFilter = (sItemString != NULL) ? Widen(sItemString) : std::wstring();
        RequestRefresh();
        return;
    }

    if (FunctionId != FN_ALTFILTER_FROM && FunctionId != FN_ALTFILTER_TO)
        return;

    if (sItemString == NULL)
        return;
    std::string typed(sItemString);
    size_t start = typed.find_first_not_of(" \tFLfl");
    if (start == std::string::npos)
        return;

    int fl = 0;
    for (size_t i = start; i < typed.size(); i++)
    {
        if (!isdigit((unsigned char)typed[i]))
            return;
        fl = fl * 10 + (typed[i] - '0');
        if (fl > 999)
            return;
    }

    if (FunctionId == FN_ALTFILTER_FROM)
        Plugin()->SetAltFilterFromFL(fl);
    else
        Plugin()->SetAltFilterToFL(fl);
    RequestRefresh();
}

void CGalaxyATMSystemRadarScreen::OnMoveScreenObject(int ObjectType, const char* sObjectId,
    POINT Pt, RECT Area, bool Released)
{
    if (ObjectType == SO_VV_SLIDER)
        m_panelDirty = true;
    if (ObjectType == SO_PANEL_DRAG)
    {
        if (!m_collapsedDragging)
        {
            m_collapsedDragging = true;
            m_collapsedGrab = { Pt.x - m_panelArea.left, Pt.y - m_panelArea.top };
        }
        const RECT ra = GetRadarArea();
        m_collapsedShift = { Pt.x - m_collapsedGrab.x - (ra.right - kCollapsedWidth),
                             Pt.y - m_collapsedGrab.y - PanelTop() };
        if (Released)
            m_collapsedDragging = false;
        m_panelDirty = true;
        RequestRefresh();
        return;
    }
    if (ObjectType == SO_FORMULAR_AHDG)
    {
        const bool leftHeld = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

        if (m_hdgDragCancelled)
        {
            if (Released || !leftHeld)
                m_hdgDragCancelled = false;
            return;
        }
        if (!m_hdgDragging && (Released || !leftHeld))
            return;

        if (!m_hdgDragging)
        {
            m_hdgDragging = true;
            m_hdgDragMoved = false;
            m_hdgDragStart = Pt;
            m_hdgDragCallsign = sObjectId;
        }
        m_hdgDragPt = Pt;
        if (abs(Pt.x - m_hdgDragStart.x) > 6 || abs(Pt.y - m_hdgDragStart.y) > 6)
            m_hdgDragMoved = true;

        if (Released)
        {
            if (m_hdgDragMoved)
            {
                int hdg = DragHeading(m_hdgDragCallsign.c_str(), Pt);
                CFlightPlan fp = GetPlugIn()->FlightPlanSelect(m_hdgDragCallsign.c_str());
                if (hdg > 0 && fp.IsValid())
                    fp.GetControllerAssignedData().SetAssignedHeading(hdg);
                m_hdgDragEndTick = GetTickCount64();
            }
            m_hdgDragging = false;
            m_hdgDragMoved = false;
        }
        RequestRefresh();
        return;
    }

    if (ObjectType == SO_FORMULAR)
    {
        if (Released)
        {
            m_dragOffset = { 0, 0 };
            return;
        }

        auto it = m_formulars.find(sObjectId);
        if (it == m_formulars.end())
            return;
        FormularState& f = it->second;

        if (m_dragOffset.x == 0 && m_dragOffset.y == 0)
        {
            m_dragOffset.x = Pt.x - f.callsignAt.x;
            m_dragOffset.y = Pt.y - f.callsignAt.y;
        }
        f.offset.x = Pt.x - m_dragOffset.x - f.anchor.x;
        f.offset.y = Pt.y - m_dragOffset.y - f.anchor.y;
        f.placed = true;
        RequestRefresh();
        return;
    }

    if (ObjectType == SO_CFL_WINDOW && m_cflOpen)
    {
        DragPopup(m_cflPlacement, m_cflArea, Pt, Released);
        return;
    }

    if (ObjectType == SO_SPD_WINDOW && m_spdOpen)
    {
        DragPopup(m_spdPlacement, m_spdArea, Pt, Released);
        return;
    }

    if (ObjectType == SO_XFR_WINDOW && m_xfrOpen)
    {
        DragPopup(m_xfrPlacement, m_xfrArea, Pt, Released);
        return;
    }

    if (ObjectType == SO_HDG_WINDOW && m_ahdgOpen)
    {
        DragPopup(m_ahdgPlacement, m_ahdgArea, Pt, Released);
        return;
    }

    if (ObjectType == SO_RVSM_WINDOW && m_rvsmOpen)
    {
        DragPopup(m_rvsmPlacement, m_rvsmArea, Pt, Released);
        return;
    }

    if (ObjectType == SO_COORD_WINDOW && m_coordOpen)
    {
        DragPopup(m_coordPlacement, m_coordArea, Pt, Released);
        return;
    }

    if (ObjectType == SO_ATIS_SCROLLBAR)
    {
        ScrollAtisTo(Pt, Area);
        RequestRefresh();
        return;
    }

    if (ObjectType == SO_VV_SLIDER)
    {
        m_vvDragging = !Released;
        SetVvGainFrom(Pt);
        RequestRefresh();
        return;
    }

    if (ObjectType == SO_RULER_LABEL)
    {
        if (Released)
        {
            m_dragOffset = { 0, 0 };
            return;
        }

        size_t idx = (size_t)atoi(sObjectId);
        if (idx >= m_rulers.size())
            return;
        RulerLine& r = m_rulers[idx];

        POINT at = { r.labelAnchor.x + r.labelOffset.x, r.labelAnchor.y + r.labelOffset.y };
        if (m_dragOffset.x == 0 && m_dragOffset.y == 0)
        {
            m_dragOffset.x = Pt.x - at.x;
            m_dragOffset.y = Pt.y - at.y;
        }

        r.labelOffset.x = Pt.x - m_dragOffset.x - r.labelAnchor.x;
        r.labelOffset.y = Pt.y - m_dragOffset.y - r.labelAnchor.y;
        RequestRefresh();
        return;
    }

    if (ObjectType == SO_RC_RESIZE)
    {
        if (!m_rcResizing)
        {
            m_rcResizing = true;
            m_rcResizeGrab = m_rcArea.right - Pt.x;
        }
        const int width = Pt.x + m_rcResizeGrab - m_rcArea.left;
        m_rcScale = max(kRcScaleMin, min(kRcScaleMax, (int)lround(width * 100.0 / kRcSvgW)));
        if (Released)
            m_rcResizing = false;
        RequestRefresh();
        return;
    }

    if (ObjectType == SO_RC_HEADER)
    {
        if (m_rcFloating || (!Released && !(GetAsyncKeyState(VK_LBUTTON) & 0x8000)))
        {
            m_dragOffset = { 0, 0 };
            return;
        }
        if (!Released)
        {
            if (m_dragOffset.x == 0 && m_dragOffset.y == 0)
            {
                m_dragOffset.x = Pt.x - m_rcArea.left;
                m_dragOffset.y = Pt.y - m_rcArea.top;

                POINT cursor;
                HWND view = NULL;
                m_rcDragView = CursorRadarPoint(cursor, &view) ? view : NULL;
            }
            RECT want = { Pt.x - m_dragOffset.x, Pt.y - m_dragOffset.y, 0, 0 };
            want.right = want.left + (m_rcArea.right - m_rcArea.left);
            want.bottom = want.top + (m_rcArea.bottom - m_rcArea.top);

            const int kPull = 40;
            const RECT ra = GetRadarArea();
            if ((want.left < ra.left - kPull || want.top < ra.top - kPull
                 || want.right > ra.right + kPull || want.bottom > ra.bottom + kPull)
                && UndockSectorList(want))
                return;
        }
    }

    RECT* target = NULL;
    if (ObjectType == SO_RC_HEADER)
        target = &m_rcArea;
    else if (ObjectType == SO_ATIS_HEADER)
        target = &m_atisArea;
    else if (ObjectType == SO_ATIS_LETTER_HEADER)
        target = &m_atisLetterArea;
    else if (ObjectType == SO_LOGIN_HEADER)
        target = m_loginCollapsed ? &m_loginCollapsedArea : &m_loginArea;
    else
        return;

    if (Released)
    {
        m_dragOffset = { 0, 0 };
        return;
    }

    if (m_dragOffset.x == 0 && m_dragOffset.y == 0)
    {
        m_dragOffset.x = Pt.x - target->left;
        m_dragOffset.y = Pt.y - target->top;
    }
    else if (target == &m_atisLetterArea)
    {
        m_atisLetterDragged = true;
    }

    int w = target->right - target->left;
    int h = target->bottom - target->top;
    target->left = Pt.x - m_dragOffset.x;
    target->top = Pt.y - m_dragOffset.y;
    target->right = target->left + w;
    target->bottom = target->top + h;
    RequestRefresh();
}
