#include "pch.h"
#include "Screen/ScreenCommon.h"

using namespace Galaxy;

bool CGalaxyATMSystemRadarScreen::OnCompileCommand(const char* sCommandLine)
{
    m_panelDirty = true;
    std::string cmd(sCommandLine);
    if (cmd == ".ulll")
    {
        m_visible = !m_visible;
        RequestRefresh();
        return true;
    }
    if (cmd == ".eng" || cmd == ".rus")
    {
        Lang::Set(cmd == ".eng" ? Lang::Id::En : Lang::Id::Ru);
        std::string msg = Narrow(Tr(cmd == ".eng" ? L"английский" : L"русский"));
        GetPlugIn()->DisplayUserMessage("ULLL Panel", Narrow(Tr(L"Язык")).c_str(),
            msg.c_str(), true, false, false, false, false);
        RequestRefresh();
        return true;
    }
    if (!Authorized() && cmd.compare(0, 9, ".formular") == 0)
        return false;
    if (cmd == ".formular")
    {
        m_formularsVisible = !m_formularsVisible;
        RequestRefresh();
        return true;
    }
    if (cmd.compare(0, 10, ".formular ") == 0)
    {
        const std::string arg = cmd.substr(10);
        int kind = -1;
        for (int i = 0; i < (int)_countof(kFormularKindNames); i++)
            if (arg == kFormularKindNames[i])
                kind = i;
        if (kind < 0)
            return false;
        m_formularKindSetting = (FormularKindSetting)kind;

        static const wchar_t* const kLabelNames[] = {
            L"РДЦ (Контроль)", L"ДПК/ДПП (Круг/Подход)", L"КДП (Вышка)" };
        std::wstring what = Tr(kLabelNames[(int)CurrentFormularKind()]);
        if (m_formularKindSetting == FormularKindSetting::Auto)
            what += Tr(L" - по позиции");
        what += Tr(L", подключение: ") + std::to_wstring(GetPlugIn()->GetConnectionType());
        std::string msg = Narrow(what);
        GetPlugIn()->DisplayUserMessage("ULLL Panel", Narrow(Tr(L"Формуляр")).c_str(),
            msg.c_str(), true, false, false, false, false);
        RequestRefresh();
        return true;
    }
    if (cmd == ".galaxyperf")
    {
        m_perfOn = !m_perfOn;
        PerfReset();
        Log::Info("perf", m_perfOn ? "timing on" : "timing off");
        GetPlugIn()->DisplayUserMessage("ULLL Panel", "perf",
            m_perfOn ? "timing on - GalaxyATMSystem.log every 10 s" : "timing off",
            true, false, false, false, false);
        return true;
    }
    if (cmd == ".galaxydiag")
    {
        CPlugIn* p = GetPlugIn();
        CController me = p->ControllerMyself();
        char line[512];
        sprintf_s(line, "connection=%d me=%s position=%s facility=%d controller=%d",
            p->GetConnectionType(),
            me.IsValid() && me.GetCallsign() != NULL ? me.GetCallsign() : "-",
            me.IsValid() && me.GetPositionId() != NULL ? me.GetPositionId() : "-",
            me.IsValid() ? me.GetFacility() : -1,
            (me.IsValid() && me.IsController()) ? 1 : 0);
        p->DisplayUserMessage("ULLL Panel", "Diag", line, true, true, false, false, false);

        CFlightPlan fp = p->FlightPlanSelectASEL();
        if (fp.IsValid())
        {
            CFlightPlanControllerAssignedData cad = fp.GetControllerAssignedData();
            const char* tracking = fp.GetTrackingControllerId();
            const char* next = fp.GetCoordinatedNextController();
            const char* a7 = cad.GetFlightStripAnnotation(kTopSkySpeedAnnotation);
            const char* direct = cad.GetDirectToPointName();
            sprintf_s(line, "%s state=%d simulated=%d fpstate=%d tracking='%s' trackedByMe=%d next='%s' cfl=%d ahdg=%d direct='%s' asp=%d ann7='%s'",
                fp.GetCallsign(), fp.GetState(), fp.GetSimulated() ? 1 : 0, fp.GetFPState(),
                tracking != NULL ? tracking : "", fp.GetTrackingControllerIsMe() ? 1 : 0,
                next != NULL ? next : "",
                cad.GetClearedAltitude(), cad.GetAssignedHeading(),
                direct != NULL ? direct : "", cad.GetAssignedSpeed(),
                a7 != NULL ? a7 : "");
        }
        else
        {
            strcpy_s(line, "no selected aircraft - click its callsign first");
        }
        p->DisplayUserMessage("ULLL Panel", "Diag", line, true, true, false, false, false);
        return true;
    }

    if (cmd == ".symbols")
    {
        CPlugIn* p = GetPlugIn();
        const TrackSymbolSet& symbols = TrackSymbols();
        std::string line = "file: " + g_trackSymbolsSource;
        p->DisplayUserMessage("ULLL Panel", "Symbols", line.c_str(), true, true, false, false, false);

        line = "symbols (* = from the file):";
        for (const auto& s : symbols)
            line += " " + s.first + (g_trackSymbolsFromFile.count(s.first) ? "*" : "");
        p->DisplayUserMessage("ULLL Panel", "Symbols", line.c_str(), true, true, false, false, false);

        char buf[256];
        sprintf_s(buf, "last frame: targets=%d offRadar=%d altFilter=%d noSymbol=%d drawn=%d visible=%d",
            m_symbolStats.targets, m_symbolStats.offRadar, m_symbolStats.filtered,
            m_symbolStats.noSymbol, m_symbolStats.drawn, m_visible ? 1 : 0);
        p->DisplayUserMessage("ULLL Panel", "Symbols", buf, true, true, false, false, false);
        return true;
    }

    if (cmd == ".logout")
    {
        Plugin()->SetSessionAuthorized(false);
        m_authState = AuthState::LoggedOut;
        m_authMessage.clear();
        m_autoLoginTried = true;
        CloseLoginWindow();
        m_openDropdown = DropdownKind::None;
        RequestRefresh();
        return true;
    }
    if (cmd == ".sigmet")
    {
        m_sigmetsVisible = !m_sigmetsVisible;
        if (!m_sigmetsVisible)
            m_sigmetInfoIndex = -1;
        std::wstring what = m_sigmetsVisible ? Tr(L"показаны") : Tr(L"скрыты");
        std::shared_ptr<const std::vector<Sigmet>> list = Plugin()->Sigmets();
        what += Tr(L", загружено: ") + std::to_wstring(list ? list->size() : 0);
        std::string msg = Narrow(what);
        GetPlugIn()->DisplayUserMessage("ULLL Panel", Narrow(Tr(L"Сигметы")).c_str(),
            msg.c_str(), true, false, false, false, false);
        RequestRefresh();
        return true;
    }
    if (cmd == ".zones")
    {
        m_zonesVisible = !m_zonesVisible;
        if (!m_zonesVisible)
            m_zoneInfoIndex = -1;
        size_t active = 0;
        for (char on : m_zoneActive)
            active += on ? 1 : 0;
        std::wstring what = m_zonesVisible ? Tr(L"показаны") : Tr(L"скрыты");
        what += Tr(L", активно: ") + std::to_wstring(active)
            + Tr(L" из ") + std::to_wstring(Plugin()->GetConfig().Zones().size());

        std::shared_ptr<const std::vector<ZoneBooking>> aup = Plugin()->AupBookings();
        what += Tr(L", план: ") + std::to_wstring(aup ? aup->size() : 0);

        std::shared_ptr<const std::vector<ZoneBooking>> notams = Plugin()->Notams();
        what += Tr(L", нотамы: ");
        if (!notams)
            what += Plugin()->GetConfig().NotamSource().empty() ? Tr(L"источник не задан") : Tr(L"не прочитаны");
        else
            what += std::to_wstring(notams->size());
        std::string msg = Narrow(what);
        GetPlugIn()->DisplayUserMessage("ULLL Panel", Narrow(Tr(L"Зоны")).c_str(),
            msg.c_str(), true, false, false, false, false);
        RequestRefresh();
        return true;
    }
    if (cmd == ".rc")
    {
        m_rcOpen = !m_rcOpen;
        RequestRefresh();
        return true;
    }
    if (cmd.compare(0, 4, ".rc ") == 0)
    {
        const int pct = atoi(cmd.c_str() + 4);
        if (pct < kRcScaleMin || pct > kRcScaleMax)
            return false;
        m_rcScale = pct;
        m_rcOpen = true;
        RequestRefresh();
        return true;
    }
    if (cmd == ".atis")
    {
        m_atisLetterOpen = !m_atisLetterOpen;
        RequestRefresh();
        return true;
    }
    if (cmd == ".ruler")
    {
        m_rulerPlacing = false;
        m_rulerArmed = false;
        m_rulerPressPending = false;
        RequestRefresh();
        return true;
    }
    if (cmd.compare(0, 10, ".rulerbtn ") == 0)
    {
        std::string arg = cmd.substr(10);
        const wchar_t* what = NULL;
        if (arg == "0")      { m_rulerButton = 0;            what = Tr(L"боковая кнопка отключена"); }
        else if (arg == "1") { m_rulerButton = VK_XBUTTON1;  what = Tr(L"боковая кнопка 1"); }
        else if (arg == "2") { m_rulerButton = VK_XBUTTON2;  what = Tr(L"боковая кнопка 2"); }
        if (what == NULL)
            return false;

        m_rulerButtonDown = false;
        std::string msg = Narrow(what);
        GetPlugIn()->DisplayUserMessage("ULLL Panel", Narrow(Tr(L"Линейка")).c_str(),
            msg.c_str(), true, false, false, false, false);
        return true;
    }
    if (cmd == ".reload")
    {
        Plugin()->ReloadConfig();
        ResetTrackSymbols();

        m_zoneInfoIndex = -1;
        m_sigmetInfoIndex = -1;
        m_zoneActive.clear();
        m_zoneBooking.clear();
        m_aup.reset();
        m_notams.reset();

        const Config& cfg = Plugin()->GetConfig();
        std::wstring what = Tr(L"зон: ") + std::to_wstring(cfg.Zones().size())
            + Tr(L", постов: ") + std::to_wstring(cfg.PositionCount());
        if (!cfg.LoadError().empty())
            what += L" - " + cfg.LoadError();

        std::string msg = Narrow(what);
        GetPlugIn()->DisplayUserMessage("ULLL Panel", Narrow(Tr(L"Конфигурация")).c_str(),
            msg.c_str(), true, false, false, false, false);
        RequestRefresh();
        return true;
    }
    if (cmd == ".rulerclear")
    {
        m_rulers.clear();
        m_rulerPlacing = false;
        m_rulerArmed = false;
        m_rulerPressPending = false;
        RequestRefresh();
        return true;
    }
    return false;
}
