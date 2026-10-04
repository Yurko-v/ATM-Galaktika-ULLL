#include "pch.h"
#include "Screen/ScreenCommon.h"

using namespace Galaxy;

namespace
{
    struct CallsignMenuRow
    {
        const wchar_t* label;
        bool separatorAfter;
    };

    const CallsignMenuRow kCallsignMenuRows[] = {
        { L"Привязать", false },
        { L"Отвязать", true },
        { L"Сброс управления", false },
        { L"Завершить план", true },
        { L"FPL к отметке", false },
        { L"Редактировать FPL", false },
        { L"Изменить код ВРЛ", true },
        { L"Общий маркер", false },
        { L"Мой маркер", false },
    };
}

void CGalaxyATMSystemRadarScreen::OpenCallsignMenu(const char* callsign, const RECT& anchor)
{
    m_csMenuOpen = true;
    m_csMenuCallsign = callsign;
    m_csMenuAnchor = anchor;
    m_csMenuButtonsDown = true;
    RequestRefresh();
}

void CGalaxyATMSystemRadarScreen::CloseCallsignMenu()
{
    m_csMenuOpen = false;
    m_csMenuArea = { 0, 0, 0, 0 };
    RequestRefresh();
}

bool CGalaxyATMSystemRadarScreen::CallsignMenuItemEnabled(CallsignMenuItem item)
{
    CFlightPlan fp = GetPlugIn()->FlightPlanSelect(m_csMenuCallsign.c_str());
    CRadarTarget rt = GetPlugIn()->RadarTargetSelect(m_csMenuCallsign.c_str());
    const bool plan = fp.IsValid();
    const bool correlated = plan && fp.GetCorrelatedRadarTarget().IsValid();
    switch (item)
    {
    case CallsignMenuItem::Correlate:
        return plan && !correlated && rt.IsValid();
    case CallsignMenuItem::Uncorrelate:
        return correlated;
    case CallsignMenuItem::Release:
        return plan && fp.GetTrackingControllerIsMe();
    case CallsignMenuItem::ClosePlan:
        return plan && m_closedPlans.count(m_csMenuCallsign) == 0;
    case CallsignMenuItem::PlanToTarget:
        return rt.IsValid();
    case CallsignMenuItem::EditPlan:
    case CallsignMenuItem::ChangeCode:
    case CallsignMenuItem::SharedMarker:
        return plan;
    default:
        return true;
    }
}

void CGalaxyATMSystemRadarScreen::RunCallsignMenuItem(CallsignMenuItem item)
{
    const bool enabled = CallsignMenuItemEnabled(item);
    CloseCallsignMenu();
    if (!enabled)
        return;

    const std::string& callsign = m_csMenuCallsign;
    CFlightPlan fp = GetPlugIn()->FlightPlanSelect(callsign.c_str());
    CRadarTarget rt = GetPlugIn()->RadarTargetSelect(callsign.c_str());
    const POINT at = { (m_csMenuAnchor.left + m_csMenuAnchor.right) / 2, (m_csMenuAnchor.top + m_csMenuAnchor.bottom) / 2 };
    switch (item)
    {
    case CallsignMenuItem::Correlate:
        m_closedPlans.erase(callsign);
        if (!fp.CorrelateWithRadarTarget(rt))
            Log::Warn("formular", callsign + ": EuroScope refused to correlate the plan with the target");
        break;
    case CallsignMenuItem::Uncorrelate:
        fp.Uncorrelate();
        Log::Info("formular", callsign + ": plan uncorrelated");
        break;
    case CallsignMenuItem::Release:
        if (!fp.EndTracking())
            Log::Warn("formular", callsign + ": EuroScope refused to release");
        break;
    case CallsignMenuItem::ClosePlan:
        if (fp.GetTrackingControllerIsMe() && !fp.EndTracking())
            Log::Warn("formular", callsign + ": EuroScope refused to release before closing the plan");
        if (fp.GetCorrelatedRadarTarget().IsValid())
            fp.Uncorrelate();
        m_closedPlans.insert(callsign);
        m_rcPicked.erase(callsign);
        m_routeShown.erase(callsign);
        Log::Info("formular", callsign + ": plan closed");
        break;
    case CallsignMenuItem::PlanToTarget:
        StartTagFunction(callsign.c_str(), NULL, 0, callsign.c_str(), NULL,
            TAG_ITEM_FUNCTION_CORRELATE_POPUP, at, m_csMenuAnchor);
        break;
    case CallsignMenuItem::EditPlan:
        StartTagFunction(callsign.c_str(), NULL, 0, callsign.c_str(), NULL,
            TAG_ITEM_FUNCTION_OPEN_FP_DIALOG, at, m_csMenuAnchor);
        break;
    case CallsignMenuItem::ChangeCode:
        GetPlugIn()->SetASELAircraft(fp);
        Plugin()->HandleSquawkFunction(TAG_FUNC_SQUAWK_MENU, "", m_csMenuAnchor, "callsign menu");
        break;
    case CallsignMenuItem::SharedMarker:
        Plugin()->ToggleSharedMarker(fp);
        break;
    case CallsignMenuItem::MyMarker:
        if (m_rcPicked.erase(callsign) == 0)
            m_rcPicked.insert(callsign);
        break;
    default:
        break;
    }
    RequestRefresh();
}

void CGalaxyATMSystemRadarScreen::DrawCallsignMenu(HDC hDC)
{
    std::vector<PanelMenuRow> rows;
    for (int i = 0; i < (int)CallsignMenuItem::Count; i++)
    {
        const CallsignMenuItem item = (CallsignMenuItem)i;
        PanelMenuRow row = { kCallsignMenuRows[i].label, CallsignMenuItemEnabled(item), kCallsignMenuRows[i].separatorAfter };
        if (item == CallsignMenuItem::SharedMarker)
            row.check = Plugin()->IsSharedMarked(m_csMenuCallsign) ? MenuCheck::On : MenuCheck::Off;
        else if (item == CallsignMenuItem::MyMarker)
            row.check = m_rcPicked.count(m_csMenuCallsign) != 0 ? MenuCheck::On : MenuCheck::Off;
        rows.push_back(row);
    }
    std::wstring detail;
    CFlightPlan fp = GetPlugIn()->FlightPlanSelect(m_csMenuCallsign.c_str());
    const char* type = fp.IsValid() ? fp.GetFlightPlanData().GetAircraftFPType() : NULL;
    if (type != NULL && *type != '\0')
        detail = Widen(type);
    CRadarTarget rt = GetPlugIn()->RadarTargetSelect(m_csMenuCallsign.c_str());
    const char* squawk = rt.IsValid() && rt.GetPosition().IsValid() ? rt.GetPosition().GetSquawk() : NULL;
    if (squawk != NULL && *squawk != '\0')
        detail += (detail.empty() ? L"" : L" \x00B7 ") + Widen(squawk);

    const POINT at = { m_csMenuAnchor.right + 4, m_csMenuAnchor.top };
    m_csMenuArea = DrawPanelMenu(hDC, at, Widen(m_csMenuCallsign.c_str()).c_str(), rows, SO_CS_MENU, SO_CS_MENU_ITEM,
        detail.c_str());
}

void CGalaxyATMSystemRadarScreen::TickCallsignMenu()
{
    if (!m_csMenuOpen)
        return;
    auto label = m_formulars.find(m_csMenuCallsign);
    if (label == m_formulars.end() || label->second.items.empty())
    {
        CloseCallsignMenu();
        return;
    }
    POINT cursor;
    const bool onRadar = CursorRadarPoint(cursor);
    const bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0
        || (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    const bool inside = onRadar && (PtInRect(&m_csMenuArea, cursor) || PtInRect(&m_csMenuAnchor, cursor));
    if (down && !m_csMenuButtonsDown && !inside)
        CloseCallsignMenu();
    m_csMenuButtonsDown = down;
}
