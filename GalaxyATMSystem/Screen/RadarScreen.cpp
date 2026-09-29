#include "pch.h"
#include "Screen/ScreenCommon.h"

using namespace Galaxy;

void CGalaxyATMSystemRadarScreen::ForgetGone(const std::set<std::string>& live)
{
    KeepOnly(m_formulars, live);
    KeepOnly(m_localFreeText, live);
    for (auto it = m_routeShown.begin(); it != m_routeShown.end();)
        it = live.count(*it) != 0 ? std::next(it) : m_routeShown.erase(it);
    KeepOnly(m_rvsmStatus, live);
    KeepOnly(m_rcLastInSector, live);
    KeepOnly(m_rcPicked, live);
}

CGalaxyATMSystemRadarScreen::CGalaxyATMSystemRadarScreen()
{
    g_screens.insert(this);
    m_panelArea = { 0, 0, 0, 0 };
    m_visible = true;
    m_loginWindowOpen = false;
    m_loginArea = { 0, 0, 0, 0 };
    m_loginPositioned = false;
    m_loginDrawnTick = 0;
    for (RECT& field : m_loginFields)
        field = { 0, 0, 0, 0 };
    m_entryField = -1;
    m_entryPending = -1;
    m_entryPendingTick = 0;
    m_entryView = NULL;
    m_collapsed = false;
    m_dragOffset = { 0, 0 };

    m_authState = AuthState::LoggedOut;
    m_autoLoginTried = false;

    m_timerRunning = false;
    m_timerStartTick = 0;
    m_timerElapsedMs = 0;

    m_vecDistEnabled = false;
    m_vecDistKm = 10;
    m_vecTimeEnabled = true;
    m_vecTimeMin = 2;
    m_vecByPlan = false;
    m_vecShowLevel = false;

    m_openDropdown = DropdownKind::None;
    m_dropdownListRect = { 0, 0, 0, 0 };
    m_vecDistFieldRect = { 0, 0, 0, 0 };
    m_vecTimeFieldRect = { 0, 0, 0, 0 };

    m_esFont = NULL;
    m_rulerFont = NULL;
    m_rulerFontSource = NULL;

    m_osLines = 3;
    m_osSpeed = true;
    m_osFontFieldRect = { 0, 0, 0, 0 };

    m_formularsVisible = true;
    m_formularKindSetting = FormularKindSetting::Auto;
    m_formularFont = NULL;
    m_formularFontSize = 0;
    m_hdgDragging = false;
    m_hdgDragMoved = false;
    m_hdgDragStart = { 0, 0 };
    m_hdgDragPt = { 0, 0 };
    m_hdgDragEndTick = 0;
    m_hdgDragCancelled = false;
    m_hdgReleaseTicks = 0;

    m_codeAll = false;
    m_codeBp = false;
    m_codeExtra = false;
    m_vvGain = 35;
    m_vvDragging = false;
    m_vvSliderRect = { 0, 0, 0, 0 };

    m_atisLetterOpen = true;
    m_atisLetterArea = { 0, 0, 0, 0 };

    m_rcOpen = false;
    m_rcArea = { 0, 0, 0, 0 };
    m_rcPositioned = false;
    m_rcScroll = 0;
    m_rcScrollMine = 0;
    m_rcPane[0] = m_rcPane[1] = { 0, 0, 0, 0 };
    m_rcPaneRows[0] = m_rcPaneRows[1] = 0;
    m_rcSortKey = 1;
    m_rcSortAsc = true;
    m_rcScale = 40;
    m_rcResizing = false;
    m_rcResizeGrab = 0;
    m_rcFont = NULL;
    m_rcHeadFont = NULL;
    m_rcRowFont = NULL;
    m_rcCellFont = NULL;
    m_rcFontScale = 0;
    m_rcFilterBefore = -1;
    m_rcFilterAfter = -1;
    m_rcFloating = false;
    m_rcFloatPos = { 0, 0 };
    m_rcDragView = NULL;
    m_rcFloatDrawn = 0;
    m_rcDrawingFloat = false;
    m_rcFloatResizing = false;
    m_rcFloatGrab = 0;

    m_atisOpen = false;
    m_atisScrollPx = 0;
    m_atisScrollMax = 0;
    m_atisThumbH = 0;
    m_atisArea = { 0, 0, 0, 0 };
    m_atisPositioned = false;

    m_sigmetsVisible = true;
    m_sigmetInfoIndex = -1;
    m_sigmetInfoAt = { 0, 0 };
    m_sigmetInfoHeld = false;
    m_sigmetInfoWait = 0;

    m_zonesVisible = (g_plugin != NULL) ? g_plugin->GetConfig().ZonesEnabled() : true;
    m_zoneInfoIndex = -1;
    m_zoneInfoAt = { 0, 0 };
    m_zoneInfoHeld = false;
    m_zoneInfoWait = 0;
    m_areaShiftDown = false;

    m_rulerButton = VK_XBUTTON2;
    m_rulerButtonDown = false;
    m_rulerPressPending = false;
    m_rulerArmed = false;
    m_rulerPlacing = false;

    UINT_PTR id = SetTimer(NULL, 0, 1000, [](HWND, UINT, UINT_PTR idEvent, DWORD)
        {
            auto it = g_timers.find(idEvent);
            if (it != g_timers.end())
            {
                it->second->RequestRefresh();
                it->second->TickRcFloat();
            }
        });
    g_timers[id] = this;
    m_timerId = id;

    UINT_PTR pollId = SetTimer(NULL, 0, 40, [](HWND, UINT, UINT_PTR idEvent, DWORD)
        {
            auto it = g_pollTimers.find(idEvent);
            if (it != g_pollTimers.end())
            {
                it->second->PollRulerButton();
                it->second->AutoLogin();
                it->second->TickEntry();
            }
        });
    g_pollTimers[pollId] = this;
    m_pollTimerId = pollId;
}

void CGalaxyATMSystemRadarScreen::Shutdown()
{
    m_cflOpen = m_spdOpen = m_ahdgOpen = m_rvsmOpen = m_xfrOpen = m_ftOpen = m_coordOpen = m_atisOpen = false;
    m_visible = false;
    for (TextEntry* entry : { &m_spdEntry, &m_ahdgEntry, &m_xfrEntry, &m_ftEntry, &m_cflEntry, &m_entry, &m_rcEntry })
        entry->Close();
    UpdateWheelHook();
    m_rcFloat.Destroy();
    if (m_timerId != 0)
    {
        KillTimer(NULL, m_timerId);
        g_timers.erase(m_timerId);
        m_timerId = 0;
    }
    if (m_pollTimerId != 0)
    {
        KillTimer(NULL, m_pollTimerId);
        g_pollTimers.erase(m_pollTimerId);
        m_pollTimerId = 0;
    }
}

CGalaxyATMSystemRadarScreen::~CGalaxyATMSystemRadarScreen()
{
    g_screens.erase(this);
    Shutdown();
    m_panelSnapshot.Release();
    m_zoneLayer.Release();
    m_fonts.Destroy();
    if (m_rulerFont != NULL)
        DeleteObject(m_rulerFont);
    if (m_formularFont != NULL)
        DeleteObject(m_formularFont);
    if (m_spdFont != NULL)
        DeleteObject(m_spdFont);
    if (m_xfrFont != NULL)
        DeleteObject(m_xfrFont);
    if (m_titleFont != NULL)
        DeleteObject(m_titleFont);
    for (HFONT f : { m_rcFont, m_rcHeadFont, m_rcRowFont, m_rcCellFont })
        if (f != NULL)
            DeleteObject(f);
}

WorkMode CGalaxyATMSystemRadarScreen::GetWorkMode(std::wstring& labelOut, COLORREF& colorOut)
{
    int ct = GetPlugIn()->GetConnectionType();
    CController me = GetPlugIn()->ControllerMyself();
    int rating = me.IsValid() ? me.GetRating() : 0;

    if (rating >= 11)
    {
        labelOut = L"SUP";
        colorOut = Theme::ModeSup;
        return WorkMode::Sup;
    }

    switch (ct)
    {
    case CONNECTION_TYPE_DIRECT:
    case CONNECTION_TYPE_VIA_PROXY:
        labelOut = L"OPS";
        colorOut = Theme::ModeOps;
        return WorkMode::Ops;
    case CONNECTION_TYPE_SIMULATOR_SERVER:
    case CONNECTION_TYPE_PLAYBACK:
    case CONNECTION_TYPE_SIMULATOR_CLIENT:
    case CONNECTION_TYPE_SWEATBOX:
        labelOut = L"SIM";
        colorOut = Theme::ModeSim;
        return WorkMode::Sim;
    default:
        labelOut = L"OFFLINE";
        colorOut = Theme::ModeOffline;
        return WorkMode::Offline;
    }
}

void CGalaxyATMSystemRadarScreen::GetUserInfo(std::wstring& designation,
    std::wstring& role, std::wstring& user)
{
    CController me = GetPlugIn()->ControllerMyself();
    std::string callsign = me.IsValid() ? me.GetCallsign() : "";
    std::string posId = me.IsValid() ? me.GetPositionId() : "";

    PositionInfo pi;
    if (Plugin()->GetConfig().FindPosition(callsign, posId, pi))
    {
        designation = pi.Designation;
        role = pi.Role;
    }
    else if (OnControllerPosition(me) && !posId.empty())
    {
        designation = Widen(posId.c_str());
    }

    if (designation.empty())
        designation = L"—";
    if (role.empty())
        role = L"—";

    if (Plugin()->TrainingSession())
    {
        user = L"user";
        return;
    }

    user =(m_authState == AuthState::LoggedOut) ? std::wstring() : Plugin()->MyUserName();
    if (user.empty())
        user = L"user ?";
}

std::wstring CGalaxyATMSystemRadarScreen::GetDistressCodes()
{
    std::wstring out;
    for (CRadarTarget rt = GetPlugIn()->RadarTargetSelectFirst(); rt.IsValid();
        rt = GetPlugIn()->RadarTargetSelectNext(rt))
    {
        const char* squawk = rt.GetPosition().GetSquawk();
        if (squawk == NULL || !IsDistressSquawk(squawk))
            continue;
        if (!out.empty())
            out += L" ";
        out += Widen(rt.GetCallsign()) + L"/" + Widen(squawk);
    }
    return out;
}

std::wstring CGalaxyATMSystemRadarScreen::GetDuplicateCodes()
{
    std::map<std::string, int> seen;
    for (CRadarTarget rt = GetPlugIn()->RadarTargetSelectFirst(); rt.IsValid();
        rt = GetPlugIn()->RadarTargetSelectNext(rt))
    {
        const char* squawk = rt.GetPosition().GetSquawk();
        if (squawk == NULL || strlen(squawk) != 4 || IsConspicuitySquawk(squawk))
            continue;
        seen[squawk]++;
    }

    std::wstring out;
    for (const auto& kv : seen)
    {
        if (kv.second < 2)
            continue;
        if (!out.empty())
            out += L" ";
        out += Widen(kv.first.c_str());
    }
    return out;
}
