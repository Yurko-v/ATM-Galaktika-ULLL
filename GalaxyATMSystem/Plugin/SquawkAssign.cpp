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

void CGalaxyATMSystemPlugin::SquawkDebugLine(const std::wstring& text)
{
    if (!m_config.SquawkDebug())
        return;
    DisplayUserMessage("ULLL Squawk", Narrow(Tr(L"отладка")).c_str(), Narrow(text).c_str(),
        true, true, true, true, false);
    m_squawk.Log(Log::Utf8(text));
}

void CGalaxyATMSystemPlugin::SquawkMessage(const std::wstring& text)
{
    DisplayUserMessage("ULLL Squawk", Narrow(Tr(L"Код ответчика")).c_str(), Narrow(text).c_str(),
        true, true, false, false, false);
    m_squawk.Log("message: " + Log::Utf8(text));
    Log::Warn("squawk", Log::Utf8(text));
}

bool CGalaxyATMSystemPlugin::SquawkReady(bool tell)
{
    const wchar_t* why = NULL;
    if (!m_squawk.Enabled())
    {
        why = Tr(L"сервер не настроен - Squawk.ServerUrl в GalaxyATMSystem.json");
    }
    else
    {
        int connection = GetConnectionType();
        bool live = (connection == CONNECTION_TYPE_DIRECT || connection == CONNECTION_TYPE_VIA_PROXY);
        bool sim = TrainingSession();

        if (!live && !(sim && m_config.SquawkAllowSweatbox()))
        {
            why = sim
                ? Tr(L"тренажёр: выдача кодов отключена, включите Squawk.AllowSweatbox в GalaxyATMSystem.json")
                : Tr(L"нет подключения - коды выдаются только в сети");
        }
        else if (!OnControllerPosition(ControllerMyself()))
        {
            why = Tr(L"вы не на диспетчерской позиции - наблюдатели коды не выдают");
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
        SquawkDebugLine(Tr(L"ответ для ") + Widen(answer.callsign.c_str())
            + Tr(L": код=") + Widen(answer.code.empty() ? "-" : answer.code.c_str())
            + Tr(L" ошибка=") + Widen(answer.error.empty() ? "-" : answer.error.c_str()));

        if (!answer.error.empty())
        {
            Log::Error("squawk", answer.callsign + ": "
                + (answer.kind == SquawkAnswer::Kind::Assign ? "code request" : "code report")
                + (answer.byUser ? "" : " (automatic)") + " failed - " + answer.error
                + (answer.holder.empty() ? "" : ", held by " + answer.holder));

            if (!answer.byUser)
                continue;

            std::wstring text;
            if (answer.error == "pool_empty")
                text = Tr(L"свободных кодов не осталось");
            else if (answer.error == "conflict")
                text = Tr(L"код уже занят: ") + Widen(answer.holder.c_str());
            else if (answer.error == "not_online")
                text = Tr(L"сервер не видит ") + Widen(MyPosition().c_str())
                    + Tr(L" в сети VATSIM - если вы только что подключились, повторите через минуту");
            else if (answer.error == "network_stale")
                text = Tr(L"сервер не может связаться с VATSIM и не знает, кто запрашивает");
            else if (answer.error == "rate_limited")
                text = Tr(L"слишком много запросов с этой позиции - подождите минуту");
            else if (answer.error == "unauthorized")
                text = Tr(L"сервер не принял ключ - Squawk.ApiKeyFile");
            else if (answer.error == "network")
                text = Tr(L"сервер не отвечает");
            else
                text = Tr(L"ошибка сервера: ") + Widen(answer.error.c_str());

            DisplayUserMessage("ULLL Squawk", answer.callsign.c_str(), Narrow(text).c_str(),
                true, true, false, false, false);
            continue;
        }

        if (answer.kind != SquawkAnswer::Kind::Assign)
            continue;

        CFlightPlan fp = FlightPlanSelect(answer.callsign.c_str());
        if (!fp.IsValid())
        {
            SquawkMessage(Widen(answer.callsign.c_str()) + Tr(L": получен код ") + Widen(answer.code.c_str())
                + Tr(L", но плана полёта уже нет"));
            continue;
        }
        if (answer.code == fp.GetControllerAssignedData().GetSquawk())
        {
            SquawkDebugLine(Widen(answer.callsign.c_str()) + L": " + Widen(answer.code.c_str()) + Tr(L" уже в плане"));
            continue;
        }

        m_squawkSetByUs[answer.callsign] = answer.code;
        if (!fp.GetControllerAssignedData().SetSquawk(answer.code.c_str()))
        {
            m_squawkSetByUs.erase(answer.callsign);
            SquawkMessage(Widen(answer.callsign.c_str()) + Tr(L": EuroScope не дал установить ")
                + Widen(answer.code.c_str()) + Tr(L" - сначала возьмите борт на управление"));
            continue;
        }
        SquawkDebugLine(Widen(answer.callsign.c_str()) + Tr(L": установлен код ") + Widen(answer.code.c_str()));
    }
}

void CGalaxyATMSystemPlugin::RequestSquawk(const std::string& callsign, bool fresh)
{
    if (!SquawkReady(true))
        return;

    std::string position = MyPosition();
    if (position.empty())
    {
        SquawkMessage(Tr(L"нет своего позывного диспетчера - сначала подключитесь диспетчером"));
        return;
    }

    SquawkDebugLine(Tr(L"запрос кода: ") + Widen(callsign.c_str()) + Tr(L" от ") + Widen(position.c_str())
        + (fresh ? Tr(L" (новый)") : L""));
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
        SquawkDebugLine(L"fn=" + std::to_wstring(FunctionId) + Tr(L" через ") + Widen(source)
            + Tr(L", борт: ") + (asel.IsValid() ? Widen(asel.GetCallsign()) : std::wstring(Tr(L"не выбран"))));
    }

    switch (FunctionId)
    {
    case TAG_FUNC_SQUAWK_ASSIGN:
    case TAG_FUNC_SQUAWK_MENU:
    {
        CFlightPlan fp = FlightPlanSelectASEL();
        if (!fp.IsValid())
        {
            SquawkMessage(Tr(L"борт не выбран - щёлкните по строке борта"));
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
        OpenPopupList(Area, Narrow(Tr(L"Код ответчика")).c_str(), 1);
        AddPopupListElement(Narrow(Tr(L"Получить код")).c_str(), "", FN_SQUAWK_GET);
        AddPopupListElement(Narrow(Tr(L"Новый код")).c_str(), "", FN_SQUAWK_NEW);
        AddPopupListElement(Narrow(Tr(L"Ввести вручную")).c_str(), "", FN_SQUAWK_MANUAL);
        return;
    }

    case FN_SQUAWK_GET:
    case FN_SQUAWK_NEW:
        if (m_squawkMenuCallsign.empty())
        {
            SquawkMessage(Tr(L"меню потеряло борт - откройте его заново"));
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
            SquawkMessage(Tr(L"код - это четыре цифры от 0 до 7"));
            return;
        }

        CFlightPlan fp = FlightPlanSelect(m_squawkMenuCallsign.c_str());
        if (!fp.IsValid())
            return;

        m_squawkSetByUs[fp.GetCallsign()] = code;
        if (!fp.GetControllerAssignedData().SetSquawk(code.c_str()))
        {
            m_squawkSetByUs.erase(fp.GetCallsign());
            SquawkMessage(Widen(fp.GetCallsign()) + Tr(L": EuroScope не дал установить ")
                + Widen(code.c_str()) + Tr(L" - сначала возьмите борт на управление"));
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
