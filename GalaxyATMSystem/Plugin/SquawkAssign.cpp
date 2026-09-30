#include "pch.h"
#include "Core/Common.h"

using namespace Galaxy;

void CGalaxyATMSystemPlugin::ConfigureSquawk()
{
    std::wstring log;
    if (m_config.SquawkDebug())
    {
        wchar_t path[MAX_PATH] = { 0 };
        GetModuleFileNameW(g_hModule, path, MAX_PATH);
        std::wstring p(path);
        size_t slash = p.find_last_of(L"\\/");
        if (slash != std::wstring::npos)
            log = p.substr(0, slash + 1) + L"squawk-debug.log";
    }

    m_squawk.Configure(m_config.SquawkServerUrl(), m_config.SquawkApiKey(),
        m_config.SquawkPollSeconds(), log);
}

void CGalaxyATMSystemPlugin::SquawkDebugLine(const std::string& text)
{
    if (!m_config.SquawkDebug())
        return;
    DisplayUserMessage("ULLL Squawk", "debug", text.c_str(), true, true, true, true, false);
    m_squawk.Log(text);
}

void CGalaxyATMSystemPlugin::SquawkMessage(const std::string& text)
{
    DisplayUserMessage("ULLL Squawk", "squawk", text.c_str(), true, true, false, false, false);
    m_squawk.Log("message: " + text);
    Log::Warn("squawk", text);
}

bool CGalaxyATMSystemPlugin::SquawkReady(bool tell)
{
    const char* why = NULL;
    if (!m_squawk.Enabled())
    {
        why = "server not set up - Squawk.ServerUrl in GalaxyATMSystem.json";
    }
    else
    {
        int connection = GetConnectionType();
        bool live = (connection == CONNECTION_TYPE_DIRECT || connection == CONNECTION_TYPE_VIA_PROXY);
        bool sim = TrainingSession();

        if (!live && !(sim && m_config.SquawkAllowSweatbox()))
        {
            why = sim
                ? "sweatbox: codes are off, set Squawk.AllowSweatbox in GalaxyATMSystem.json"
                : "not connected - codes are only handed out on the network";
        }
        else if (!OnControllerPosition(ControllerMyself()))
        {
            why = "not on a controller position - observers do not hand out codes";
        }
    }

    if (why == NULL)
        return true;
    if (tell)
        SquawkMessage(why);
    return false;
}

std::string CGalaxyATMSystemPlugin::MyPosition() const
{
    const char* callsign = ControllerMyself().GetCallsign();
    return callsign != NULL ? callsign : "";
}

std::string CGalaxyATMSystemPlugin::AssignedSquawk(const CFlightPlan& fp) const
{
    const char* assigned = fp.GetControllerAssignedData().GetSquawk();
    if (assigned != NULL && IsSquawkCode(assigned))
        return assigned;
    if (m_squawk.Enabled())
    {
        auto held = m_squawk.Assignments();
        auto it = held->find(fp.GetCallsign());
        if (it != held->end())
            return it->second;
    }
    return "";
}

COLORREF CGalaxyATMSystemPlugin::SquawkColor(const CFlightPlan& fp, CRadarTarget rt,
    const std::string& assigned) const
{
    if (assigned.empty())
        return Theme::SquawkNone;

    if (!rt.IsValid())
        rt = fp.GetCorrelatedRadarTarget();
    if (!rt.IsValid())
        return Theme::SquawkMismatch;

    CRadarTargetPositionData pos = rt.GetPosition();
    const char* set = pos.IsValid() ? pos.GetSquawk() : NULL;
    if (set == NULL || assigned != set)
        return Theme::SquawkMismatch;
    if (!pos.GetTransponderC())
        return Theme::SquawkNoModeC;
    return Theme::SquawkSet;
}

void CGalaxyATMSystemPlugin::ApplySquawkAnswers()
{
    for (const SquawkAnswer& answer : m_squawk.TakeAnswers())
    {
        SquawkDebugLine("answer for " + answer.callsign
            + ": code=" + (answer.code.empty() ? "-" : answer.code)
            + " error=" + (answer.error.empty() ? "-" : answer.error));

        if (!answer.error.empty())
        {
            Log::Error("squawk", answer.callsign + ": "
                + (answer.kind == SquawkAnswer::Kind::Assign ? "code request" : "code report")
                + (answer.byUser ? "" : " (automatic)") + " failed - " + answer.error
                + (answer.holder.empty() ? "" : ", held by " + answer.holder));

            if (!answer.byUser)
                continue;

            std::string text;
            if (answer.error == "pool_empty")
                text = "no free codes left";
            else if (answer.error == "conflict")
                text = "code already held by " + answer.holder;
            else if (answer.error == "not_online")
                text = "the server does not see " + MyPosition()
                    + " online on VATSIM - if you have only just logged in, try again in a minute";
            else if (answer.error == "network_stale")
                text = "the server cannot reach VATSIM, so it cannot tell who is asking";
            else if (answer.error == "rate_limited")
                text = "too many requests from this position - wait a minute";
            else if (answer.error == "unauthorized")
                text = "server refused the key - Squawk.ApiKeyFile";
            else if (answer.error == "network")
                text = "server is not answering";
            else
                text = "server error: " + answer.error;

            DisplayUserMessage("ULLL Squawk", answer.callsign.c_str(), text.c_str(),
                true, true, false, false, false);
            continue;
        }

        if (answer.kind != SquawkAnswer::Kind::Assign)
            continue;

        CFlightPlan fp = FlightPlanSelect(answer.callsign.c_str());
        if (!fp.IsValid())
        {
            SquawkMessage(answer.callsign + ": got " + answer.code
                + " but the flight plan is gone");
            continue;
        }
        if (answer.code == fp.GetControllerAssignedData().GetSquawk())
        {
            SquawkDebugLine(answer.callsign + ": " + answer.code + " already on the plan");
            continue;
        }

        m_squawkSetByUs[answer.callsign] = answer.code;
        if (!fp.GetControllerAssignedData().SetSquawk(answer.code.c_str()))
        {
            m_squawkSetByUs.erase(answer.callsign);
            SquawkMessage(answer.callsign + ": EuroScope refused to set " + answer.code
                + " - assume the aircraft first");
            continue;
        }
        SquawkDebugLine(answer.callsign + ": set to " + answer.code);
    }
}

void CGalaxyATMSystemPlugin::RequestSquawk(const std::string& callsign, bool fresh)
{
    if (!SquawkReady(true))
        return;

    std::string position = MyPosition();
    if (position.empty())
    {
        SquawkMessage("no controller callsign of your own - log in as a controller first");
        return;
    }

    SquawkDebugLine("asking for a code: " + callsign + " from " + position
        + (fresh ? " (new one)" : ""));
    m_squawk.Assign(callsign, position, fresh, true);
}

void CGalaxyATMSystemPlugin::HandleSquawkFunction(int FunctionId, const char* sItemString,
    RECT Area, const char* source)
{
    const bool mine = (FunctionId == TAG_FUNC_SQUAWK_ASSIGN || FunctionId == TAG_FUNC_SQUAWK_MENU
        || FunctionId == FN_SQUAWK_GET || FunctionId == FN_SQUAWK_NEW
        || FunctionId == FN_SQUAWK_MANUAL || FunctionId == FN_SQUAWK_MANUAL_EDIT);
    if (!mine)
        return;

    ULONGLONG now = GetTickCount64();
    if (FunctionId == m_lastSquawkFn && now - m_lastSquawkTick < 300)
        return;
    m_lastSquawkFn = FunctionId;
    m_lastSquawkTick = now;

    if (m_config.SquawkDebug())
    {
        CFlightPlan asel = FlightPlanSelectASEL();
        SquawkDebugLine("fn=" + std::to_string(FunctionId) + " via " + source
            + ", aircraft: " + (asel.IsValid() ? asel.GetCallsign() : "none selected"));
    }

    switch (FunctionId)
    {
    case TAG_FUNC_SQUAWK_ASSIGN:
    case TAG_FUNC_SQUAWK_MENU:
    {
        CFlightPlan fp = FlightPlanSelectASEL();
        if (!fp.IsValid())
        {
            SquawkMessage("no aircraft selected - click the aircraft's row");
            return;
        }
        if (!SquawkReady(true))
            return;

        if (FunctionId == TAG_FUNC_SQUAWK_ASSIGN)
        {
            RequestSquawk(fp.GetCallsign(), false);
            return;
        }

        m_squawkMenuCallsign = fp.GetCallsign();
        m_squawkMenuArea = Area;
        OpenPopupList(Area, "Squawk", 1);
        AddPopupListElement("Get code", "", FN_SQUAWK_GET);
        AddPopupListElement("New code", "", FN_SQUAWK_NEW);
        AddPopupListElement("Type in", "", FN_SQUAWK_MANUAL);
        return;
    }

    case FN_SQUAWK_GET:
    case FN_SQUAWK_NEW:
        if (m_squawkMenuCallsign.empty())
        {
            SquawkMessage("the menu lost track of the aircraft - open it again");
            return;
        }
        RequestSquawk(m_squawkMenuCallsign, FunctionId == FN_SQUAWK_NEW);
        return;

    case FN_SQUAWK_MANUAL:
    {
        CFlightPlan fp = FlightPlanSelect(m_squawkMenuCallsign.c_str());
        if (!fp.IsValid())
            return;
        OpenPopupEdit(m_squawkMenuArea, FN_SQUAWK_MANUAL_EDIT, fp.GetControllerAssignedData().GetSquawk());
        return;
    }

    case FN_SQUAWK_MANUAL_EDIT:
    {
        std::string code;
        for (const char* p = sItemString; p != NULL && *p != '\0'; p++)
        {
            if (*p != ' ')
                code += *p;
        }
        if (!IsSquawkCode(code))
        {
            SquawkMessage("a code is four digits, 0 to 7");
            return;
        }

        CFlightPlan fp = FlightPlanSelect(m_squawkMenuCallsign.c_str());
        if (!fp.IsValid())
            return;

        m_squawkSetByUs[fp.GetCallsign()] = code;
        if (!fp.GetControllerAssignedData().SetSquawk(code.c_str()))
        {
            m_squawkSetByUs.erase(fp.GetCallsign());
            SquawkMessage(std::string(fp.GetCallsign()) + ": EuroScope refused to set " + code
                + " - assume the aircraft first");
            return;
        }
        if (SquawkReady(false))
            m_squawk.Report(fp.GetCallsign(), code, MyPosition(), true);
        return;
    }

    default:
        return;
    }
}
