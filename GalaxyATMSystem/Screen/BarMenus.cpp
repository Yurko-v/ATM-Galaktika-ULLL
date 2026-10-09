#include "pch.h"
#include "Screen/ScreenCommon.h"

using namespace Galaxy;

// The drop-down menus behind the names in the menu bar. A switch that also has a dot
// command runs that command, so the menu and the command line always do the same
// thing; one that mirrors a panel control changes the same setting the panel does.
// Rows with a tick keep the menu open, so several can be flipped in a row; any other
// row closes it. A row with an arrow opens a submenu beside it, on hover or click.
namespace
{
    enum BarMenu { MenuSettings, MenuView, MenuMap, MenuAerodrome, MenuLists, MenuWeather, MenuMail,
                   MenuDownload, MenuStatistics, MenuArchive, MenuHelp };

    // Submenus, numbered within their menu.
    enum SettingsSub { SubLanguage, SubAltUnit, SubVsUnit, SubGsUnit, SubDistUnit, SubRulerButton };
    enum ViewSub { SubLabelKind, SubFontSize, SubLines, SubVecTime, SubVecDist };
    enum AerodromeSub { SubAtisAirport };
    enum ListsSub { SubRcScale, SubRcSort };
    enum WeatherSub { SubSigmetList };

    const wchar_t* const kRcSortNames[] = {
        L"КФ", L"Рейс", L"ВРЛ", L"S", L"Тип", L"W", L"CFL", L"Точка входа", L"Вход",
        L"Точка выхода", L"Выход", L"ВыхЭш", L"ПВО", L"Крд",
    };
    static_assert(_countof(kRcSortNames) == kRcCols, "sort names");

    const wchar_t* const kHelpLines[] = {
        L".ulll - показать или скрыть панель",
        L".formular - метки; .formular auto|ctr|app|twr - вид метки",
        L".rc - список РЦ; .rc 25..100 - масштаб списка",
        L".atis - окно буквы ATIS",
        L".sigmet, .zones, .stca, .rdf - SIGMET, зоны, конфликты, пеленгатор",
        L".ruler, .rulerclear, .rulerbtn 0|1|2 - линейка",
        L".rus, .eng - язык; .reload - перечитать конфигурацию; .logout - выйти",
        L".galaxyperf - замер быстродействия; .galaxydiag, .symbols, .stcainfo - диагностика",
    };

    void Say(CPlugIn* plugin, const wchar_t* sender, const std::wstring& text)
    {
        plugin->DisplayUserMessage("ULLL Panel", Narrow(Tr(sender)).c_str(), Narrow(text).c_str(),
            true, false, false, false, false);
    }

    std::wstring PluginFolder()
    {
        wchar_t path[MAX_PATH] = { 0 };
        if (g_hModule == NULL || GetModuleFileNameW(g_hModule, path, MAX_PATH) == 0)
            return std::wstring();
        std::wstring p(path);
        const size_t slash = p.find_last_of(L"\\/");
        return slash == std::wstring::npos ? std::wstring() : p.substr(0, slash + 1);
    }

    std::wstring FlText(int fl)
    {
        wchar_t buf[8];
        swprintf_s(buf, L"F%03d", fl);
        return buf;
    }
}

void CGalaxyATMSystemRadarScreen::OpenBarMenu(int menu)
{
    if (menu < 0 || menu >= kBarMenuCount || !Authorized())
        return;
    m_barMenu = menu;
    m_barSub = -1;
    m_barMenuButtonsDown = true;
    m_barMenuArea = m_barSubArea = { 0, 0, 0, 0 };
    m_barRowRects.clear();
    m_barRowSubs.clear();
    m_panelDirty = true;
    RequestRefresh();
}

void CGalaxyATMSystemRadarScreen::CloseBarMenu()
{
    m_barMenu = -1;
    m_barSub = -1;
    m_barMenuArea = m_barSubArea = { 0, 0, 0, 0 };
    m_barRowRects.clear();
    m_barRowSubs.clear();
    m_panelDirty = true;
    RequestRefresh();
}

void CGalaxyATMSystemRadarScreen::OpenBarSub(int row)
{
    if (row == m_barSub)
        return;
    m_barSub = row;
    m_barSubArea = { 0, 0, 0, 0 };
    RequestRefresh();
}

std::vector<CGalaxyATMSystemRadarScreen::BarMenuRow> CGalaxyATMSystemRadarScreen::BarMenuRows(int menu, int sub)
{
    std::vector<BarMenuRow> rows;
    auto mark = [](bool on) { return on ? MenuCheck::On : MenuCheck::Off; };
    auto add = [&rows](const std::wstring& label, BarAction what, MenuCheck check = MenuCheck::None,
        bool enabled = true, bool separator = false) -> BarMenuRow&
    {
        BarMenuRow row;
        row.label = label;
        row.action = what;
        row.check = check;
        row.enabled = enabled;
        row.separatorAfter = separator;
        rows.push_back(row);
        return rows.back();
    };
    auto command = [&add](const wchar_t* label, const char* cmd, MenuCheck check = MenuCheck::None,
        bool separator = false) -> BarMenuRow&
    {
        BarMenuRow& row = add(label, BarAction::Command, check, true, separator);
        row.command = cmd;
        return row;
    };
    auto submenu = [&add](const std::wstring& label, int id, bool separator = false) -> BarMenuRow&
    {
        BarMenuRow& row = add(label, BarAction::None, MenuCheck::None, true, separator);
        row.sub = id;
        return row;
    };
    // A line that only tells something; drawn greyed and does nothing.
    auto info = [&add](const std::wstring& text, bool separator = false) -> BarMenuRow&
    {
        return add(text, BarAction::None, MenuCheck::None, false, separator);
    };
    auto valued = [&add](const std::wstring& label, BarAction what, int value, bool on) -> BarMenuRow&
    {
        BarMenuRow& row = add(label, what, on ? MenuCheck::On : MenuCheck::Off);
        row.value = value;
        return row;
    };

    CGalaxyATMSystemPlugin* plugin = Plugin();
    const Config& cfg = plugin->GetConfig();
    switch (menu)
    {
    case MenuSettings:
        switch (sub)
        {
        case SubLanguage:
            command(L"Русский", ".rus", mark(!Lang::English()));
            command(L"English", ".eng", mark(Lang::English()));
            return rows;
        case SubAltUnit:
            valued(L"FL", BarAction::AltUnit, (int)AltUnit::FL, plugin->UnitAlt() == AltUnit::FL);
            valued(Tr(L"Метры"), BarAction::AltUnit, (int)AltUnit::M, plugin->UnitAlt() == AltUnit::M);
            valued(Tr(L"Эшелон и метры"), BarAction::AltUnit, (int)AltUnit::FLM, plugin->UnitAlt() == AltUnit::FLM);
            return rows;
        case SubVsUnit:
            valued(Tr(L"Футы в минуту"), BarAction::VsUnit, (int)VsUnit::FtMin, plugin->UnitVs() == VsUnit::FtMin);
            valued(Tr(L"Метры в секунду"), BarAction::VsUnit, (int)VsUnit::MS, plugin->UnitVs() == VsUnit::MS);
            return rows;
        case SubGsUnit:
            valued(Tr(L"Узлы"), BarAction::GsUnit, (int)GsUnit::Knots, plugin->UnitGs() == GsUnit::Knots);
            valued(Tr(L"Километры в час"), BarAction::GsUnit, (int)GsUnit::Kmh, plugin->UnitGs() == GsUnit::Kmh);
            return rows;
        case SubDistUnit:
            valued(Tr(L"Морские мили"), BarAction::DistUnit, (int)DistUnit::NM, plugin->UnitDist() == DistUnit::NM);
            valued(Tr(L"Километры"), BarAction::DistUnit, (int)DistUnit::Km, plugin->UnitDist() == DistUnit::Km);
            return rows;
        case SubRulerButton:
            command(L"Без боковой кнопки", ".rulerbtn 0", mark(m_rulerButton == 0));
            command(L"Боковая кнопка 1", ".rulerbtn 1", mark(m_rulerButton == VK_XBUTTON1));
            command(L"Боковая кнопка 2", ".rulerbtn 2", mark(m_rulerButton == VK_XBUTTON2));
            return rows;
        default:
            break;
        }
        submenu(Tr(L"Язык"), SubLanguage, true);
        submenu(Tr(L"Высота"), SubAltUnit);
        submenu(Tr(L"Вертикальная скорость"), SubVsUnit);
        submenu(Tr(L"Путевая скорость"), SubGsUnit);
        submenu(Tr(L"Расстояние"), SubDistUnit, true);
        add(Tr(L"Фильтр высот ") + FlText(min(plugin->AltFilterFromFL(), plugin->AltFilterToFL())) + L"-"
            + FlText(max(plugin->AltFilterFromFL(), plugin->AltFilterToFL())),
            BarAction::AltFilter, mark(plugin->AltFilterEnabled()));
        submenu(Tr(L"Боковая кнопка линейки"), SubRulerButton, true);
        command(L"Замер быстродействия", ".galaxyperf", mark(m_perfOn));
        command(L"Перечитать конфигурацию", ".reload", MenuCheck::None, true);
        command(L"Выйти из системы", ".logout");
        break;

    case MenuView:
        switch (sub)
        {
        case SubLabelKind:
            command(L"По позиции", ".formular auto", mark(m_formularKindSetting == FormularKindSetting::Auto));
            command(L"РДЦ (Контроль)", ".formular ctr", mark(m_formularKindSetting == FormularKindSetting::Ctr));
            command(L"ДПК/ДПП (Круг/Подход)", ".formular app", mark(m_formularKindSetting == FormularKindSetting::App));
            command(L"КДП (Вышка)", ".formular twr", mark(m_formularKindSetting == FormularKindSetting::Twr));
            return rows;
        case SubFontSize:
            for (int i = 0; i < kFontSizeStepsCount; i++)
                valued(std::to_wstring(kFontSizeSteps[i]), BarAction::FontSize, kFontSizeSteps[i],
                    plugin->TagFontSize() == kFontSizeSteps[i]);
            return rows;
        case SubLines:
            valued(Tr(L"2 строчный"), BarAction::OsLines, 2, m_osLines == 2);
            valued(Tr(L"3 строчный"), BarAction::OsLines, 3, m_osLines == 3);
            return rows;
        case SubVecTime:
            for (int i = 0; i < kTimeStepsCount; i++)
                valued(std::to_wstring(kTimeSteps[i]) + Tr(L" мин"), BarAction::VecTimeMin, kTimeSteps[i],
                    m_vecTimeMin == kTimeSteps[i]);
            return rows;
        case SubVecDist:
            for (int i = 0; i < kDistanceStepsCount; i++)
                valued(std::to_wstring(kDistanceSteps[i]) + Tr(L" км"), BarAction::VecDistKm, kDistanceSteps[i],
                    m_vecDistKm == kDistanceSteps[i]);
            return rows;
        default:
            break;
        }
        command(L"Показывать метки", ".formular", mark(m_formularsVisible));
        submenu(Tr(L"Вид метки"), SubLabelKind);
        submenu(Tr(L"Шрифт метки"), SubFontSize);
        submenu(Tr(L"Строк в метке"), SubLines);
        add(Tr(L"Скорость на метке"), BarAction::OsSpeed, mark(m_osSpeed), true, true);
        add(Tr(L"Вектор по времени"), BarAction::VecTime, mark(m_vecTimeEnabled));
        submenu(Tr(L"Время вектора: ") + std::to_wstring(m_vecTimeMin) + Tr(L" мин"), SubVecTime);
        add(Tr(L"Вектор по расстоянию"), BarAction::VecDist, mark(m_vecDistEnabled));
        submenu(Tr(L"Длина вектора: ") + std::to_wstring(m_vecDistKm) + Tr(L" км"), SubVecDist);
        add(Tr(L"Вектор по плану"), BarAction::VecPlan, mark(m_vecByPlan));
        add(Tr(L"Высота на векторе"), BarAction::VecLevel, mark(m_vecShowLevel));
        break;

    case MenuMap:
        command(L"SIGMET", ".sigmet", mark(m_sigmetsVisible));
        command(L"Зоны", ".zones", mark(m_zonesVisible));
        command(L"Конфликты (STCA)", ".stca", mark(m_stcaOn && cfg.StcaEnabled())).enabled = cfg.StcaEnabled();
        command(L"Пеленгатор", ".rdf", mark(m_rdfVisible), true);
        add(Tr(L"Измерить"), BarAction::Measure, MenuCheck::None, !m_rulerArmed && !m_rulerPlacing, true);
        add(Tr(L"Убрать маршруты"), BarAction::ClearRoutes, MenuCheck::None, !m_routeShown.empty());
        command(L"Убрать линейки", ".rulerclear").enabled = !m_rulers.empty();
        add(Tr(L"Убрать линии и окружности"), BarAction::ClearSketches, MenuCheck::None,
            !m_mapLines.empty() || !m_mapCircles.empty());
        add(Tr(L"Убрать надписи"), BarAction::ClearTexts, MenuCheck::None, !m_mapTexts.empty());
        break;

    case MenuAerodrome:
    {
        if (sub == SubAtisAirport)
        {
            const std::vector<std::string> onAir = plugin->AtisAirportsOnAir();
            const std::string home = plugin->AirportIcao();
            if (onAir.empty())
                info(Tr(L"Нет ATIS в эфире"));
            for (const std::string& airport : onAir)
            {
                // The home aerodrome is kept with an empty ICAO, as the panel does.
                const std::string icao = airport == home ? std::string() : airport;
                BarMenuRow& row = add(Widen(airport.c_str()) + L"  " + plugin->AtisIndex(airport),
                    BarAction::AtisAirport, mark(m_atisOpen && m_atisIcao == icao));
                row.text = icao;
            }
            return rows;
        }
        command(L"Окно буквы ATIS", ".atis", mark(m_atisLetterOpen));
        add(Tr(L"Текст ATIS"), BarAction::AtisText, mark(m_atisOpen && m_atisIcao.empty()));
        submenu(Tr(L"ATIS аэродрома"), SubAtisAirport, true);
        const std::wstring& hpa = plugin->QnhHpa();
        const std::wstring& mmHg = plugin->QnhMmHg();
        info(L"QNH " + (hpa.empty() ? std::wstring(L"----") : hpa) + Tr(L" гПа / ")
            + (mmHg.empty() ? std::wstring(L"---") : mmHg) + Tr(L" мм"));
        info(Tr(L"Эшелон перехода ") + plugin->TransitionLevel());
        break;
    }

    case MenuLists:
        switch (sub)
        {
        case SubRcScale:
        {
            static const char* const kScaleCommands[] = { ".rc 25", ".rc 50", ".rc 75", ".rc 100" };
            for (int i = 0; i < 4; i++)
            {
                const int pct = 25 * (i + 1);
                command(L"", kScaleCommands[i], mark(m_rcScale == pct)).label = std::to_wstring(pct) + L" %";
            }
            return rows;
        }
        case SubRcSort:
            for (int i = 0; i < kRcCols; i++)
                valued(Tr(kRcSortNames[i]), BarAction::RcSort, i, m_rcSortKey == i).separatorAfter = i == kRcCols - 1;
            add(Tr(L"По возрастанию"), BarAction::RcSortAsc, mark(m_rcSortAsc));
            return rows;
        default:
            break;
        }
        command(L"Список РЦ", ".rc", mark(m_rcOpen));
        submenu(Tr(L"Масштаб списка: ") + std::to_wstring(m_rcScale) + L" %", SubRcScale);
        submenu(Tr(L"Сортировка"), SubRcSort);
        add(Tr(L"Сбросить фильтры списка"), BarAction::ResetRcFilters, MenuCheck::None,
            !m_rcFilterCallsign.empty() || m_rcFilterBefore >= 0 || m_rcFilterAfter >= 0);
        break;

    case MenuWeather:
    {
        const auto sigmets = plugin->Sigmets();
        if (sub == SubSigmetList)
        {
            if (!sigmets || sigmets->empty())
                info(Tr(L"SIGMET нет"));
            else
                for (size_t i = 0; i < sigmets->size() && i < 15; i++)
                    info((*sigmets)[i].Title().substr(0, 80));
            return rows;
        }
        add(Tr(L"Обновить METAR"), BarAction::FetchMetar, MenuCheck::None, !plugin->MetarFromEuroScope());
        add(Tr(L"Обновить SIGMET"), BarAction::FetchSigmet, MenuCheck::None, cfg.SigmetsEnabled(), true);
        submenu(Tr(L"SIGMET загружено: ") + std::to_wstring(sigmets ? sigmets->size() : 0), SubSigmetList);
        break;
    }

    case MenuMail:
        info(Tr(L"Сообщений нет"));
        break;

    case MenuDownload:
    {
        add(Tr(L"Обновить всё"), BarAction::FetchAll, MenuCheck::None, true, true);
        add(Tr(L"Обновить план зон (AUP)"), BarAction::FetchAup, MenuCheck::None, !cfg.AupUrl().empty());
        add(Tr(L"Обновить NOTAM"), BarAction::FetchNotam, MenuCheck::None, !cfg.NotamSource().empty());
        add(Tr(L"Обновить ATIS"), BarAction::FetchAtis, MenuCheck::None, cfg.AtisLive(), true);
        const auto aup = plugin->AupBookings();
        const auto notams = plugin->Notams();
        info(Tr(L"План зон: ") + std::to_wstring(aup ? aup->size() : 0));
        info(Tr(L"NOTAM: ") + (notams ? std::to_wstring(notams->size())
            : std::wstring(Tr(cfg.NotamSource().empty() ? L"источник не задан" : L"не прочитаны"))));
        break;
    }

    case MenuStatistics:
    {
        int targets = 0, mine = 0;
        for (CRadarTarget rt = plugin->RadarTargetSelectFirst(); rt.IsValid(); rt = plugin->RadarTargetSelectNext(rt))
        {
            targets++;
            CFlightPlan fp = rt.GetCorrelatedFlightPlan();
            if (fp.IsValid() && fp.GetTrackingControllerIsMe())
                mine++;
        }
        size_t active = 0;
        for (char on : m_zoneActive)
            active += on ? 1 : 0;
        KfConflicts();
        wchar_t stcaMs[32];
        swprintf_s(stcaMs, L"%.1f", m_stcaLastMs);
        info(Tr(L"Целей: ") + std::to_wstring(targets));
        info(Tr(L"Под моим управлением: ") + std::to_wstring(mine));
        info(Tr(L"Потеря разделения (SSA): ") + std::to_wstring(m_ssaViolations.size()), true);
        info(Tr(L"Зон активно: ") + std::to_wstring(active) + Tr(L" из ") + std::to_wstring(cfg.Zones().size()));
        info(std::wstring(Tr(L"Расчёт STCA: ")) + stcaMs + Tr(L" мс"), true);
        command(L"Замер быстродействия", ".galaxyperf", mark(m_perfOn));
        break;
    }

    case MenuArchive:
        info(Tr(L"Архив не ведётся"));
        break;

    case MenuHelp:
        add(Tr(L"Команды плагина"), BarAction::ShowCommands);
        add(Tr(L"О плагине"), BarAction::ShowAbout, MenuCheck::None, true, true);
        add(Tr(L"Открыть журнал"), BarAction::OpenLog);
        add(Tr(L"Открыть папку плагина"), BarAction::OpenFolder);
        break;

    default:
        break;
    }
    return rows;
}

void CGalaxyATMSystemRadarScreen::RunBarMenuItem(int index, bool inSub)
{
    if (m_barMenu < 0 || (inSub && m_barSub < 0))
        return;
    const int subId = inSub && m_barSub < (int)m_barRowSubs.size() ? m_barRowSubs[m_barSub] : -1;
    if (inSub && subId < 0)
        return;
    const std::vector<BarMenuRow> rows = BarMenuRows(m_barMenu, subId);
    if (index < 0 || index >= (int)rows.size() || !rows[index].enabled)
        return;
    const BarMenuRow row = rows[index];

    if (row.sub >= 0)
    {
        OpenBarSub(index);
        return;
    }

    Log::Info("menu", Narrow(kMenuBarItems[m_barMenu]) + ": " + Narrow(row.label.c_str()));
    if (row.check == MenuCheck::None)
        CloseBarMenu();

    CGalaxyATMSystemPlugin* plugin = Plugin();
    switch (row.action)
    {
    case BarAction::Command:
        if (row.command != NULL)
            OnCompileCommand(row.command);
        break;
    // The two vectors are one or the other, as on the panel.
    case BarAction::VecTime:
        m_vecTimeEnabled = !m_vecTimeEnabled;
        if (m_vecTimeEnabled)
            m_vecDistEnabled = false;
        break;
    case BarAction::VecDist:
        m_vecDistEnabled = !m_vecDistEnabled;
        if (m_vecDistEnabled)
            m_vecTimeEnabled = false;
        break;
    case BarAction::VecPlan:      m_vecByPlan = !m_vecByPlan;           break;
    case BarAction::VecLevel:     m_vecShowLevel = !m_vecShowLevel;     break;
    case BarAction::VecTimeMin:   m_vecTimeMin = row.value;             break;
    case BarAction::VecDistKm:    m_vecDistKm = row.value;              break;
    case BarAction::FontSize:     plugin->SetTagFontSize(row.value);    break;
    case BarAction::OsLines:      m_osLines = row.value;                break;
    case BarAction::OsSpeed:      m_osSpeed = !m_osSpeed;               break;
    case BarAction::AltUnit:      plugin->SetUnitAlt((AltUnit)row.value);   break;
    case BarAction::VsUnit:       plugin->SetUnitVs((VsUnit)row.value);     break;
    case BarAction::GsUnit:       plugin->SetUnitGs((GsUnit)row.value);     break;
    case BarAction::DistUnit:     plugin->SetUnitDist((DistUnit)row.value); break;
    case BarAction::AltFilter:    plugin->SetAltFilterEnabled(!plugin->AltFilterEnabled()); break;
    case BarAction::Measure:
        if (!m_rulerArmed && !m_rulerPlacing)
            m_rulerPressPending = true;
        break;
    case BarAction::ClearRoutes:
        m_routeShown.clear();
        break;
    case BarAction::ClearSketches:
        m_mapLines.clear();
        m_mapCircles.clear();
        break;
    case BarAction::ClearTexts:
        m_mapTexts.clear();
        break;
    case BarAction::ResetRcFilters:
        m_rcFilterCallsign.clear();
        m_rcFilterBefore = m_rcFilterAfter = -1;
        m_rcScroll = m_rcScrollMine = 0;
        break;
    case BarAction::RcSort:
        m_rcSortKey = row.value;
        break;
    case BarAction::RcSortAsc:
        m_rcSortAsc = !m_rcSortAsc;
        break;
    case BarAction::AtisText:
        // As the panel's ATIS button: another aerodrome's ATIS switches to the home one.
        m_atisOpen = !m_atisOpen || !m_atisIcao.empty();
        m_atisIcao.clear();
        m_atisScrollPx = 0;
        UpdateWheelHook();
        break;
    case BarAction::AtisAirport:
        m_atisOpen = !(m_atisOpen && m_atisIcao == row.text);
        m_atisIcao = row.text;
        m_atisScrollPx = 0;
        UpdateWheelHook();
        break;
    case BarAction::FetchAll:
    case BarAction::FetchMetar:
    case BarAction::FetchSigmet:
    case BarAction::FetchAtis:
    case BarAction::FetchAup:
    case BarAction::FetchNotam:
    {
        const CGalaxyATMSystemPlugin::Feed feed =
              row.action == BarAction::FetchMetar ? CGalaxyATMSystemPlugin::Feed::Metar
            : row.action == BarAction::FetchSigmet ? CGalaxyATMSystemPlugin::Feed::Sigmet
            : row.action == BarAction::FetchAtis ? CGalaxyATMSystemPlugin::Feed::Atis
            : row.action == BarAction::FetchAup ? CGalaxyATMSystemPlugin::Feed::Aup
            : row.action == BarAction::FetchNotam ? CGalaxyATMSystemPlugin::Feed::Notam
            : CGalaxyATMSystemPlugin::Feed::All;
        plugin->RefreshFeed(feed);
        Say(plugin, L"Загрузка", row.label + Tr(L": запрошено"));
        break;
    }
    case BarAction::ShowCommands:
        for (const wchar_t* line : kHelpLines)
            Say(plugin, L"Справка", Tr(line));
        break;
    case BarAction::ShowAbout:
        Say(plugin, L"Справка", std::wstring(L"Galaxy ATM System ") + Widen(CGalaxyATMSystemPlugin::Version())
            + L", ULLL Team");
        break;
    case BarAction::OpenLog:
    case BarAction::OpenFolder:
    {
        const std::wstring folder = PluginFolder();
        if (folder.empty())
            break;
        const std::wstring target = row.action == BarAction::OpenLog ? folder + L"GalaxyATMSystem.log" : folder;
        ShellExecuteW(NULL, L"open", target.c_str(), NULL, NULL, SW_SHOWNORMAL);
        break;
    }
    default:
        break;
    }
    m_panelDirty = true;
    RequestRefresh();
}

void CGalaxyATMSystemRadarScreen::DrawBarMenu(HDC hDC)
{
    static_assert(_countof(kMenuBarItems) == kBarMenuCount, "menu bar items");
    if (m_barMenu < 0 || m_barMenu >= kBarMenuCount)
        return;
    const RECT& anchor = m_barAnchors[m_barMenu];
    if (anchor.right <= anchor.left)
    {
        CloseBarMenu();
        return;
    }

    auto toPanel = [](const std::vector<BarMenuRow>& rows, int selected)
    {
        std::vector<PanelMenuRow> drawn;
        for (size_t i = 0; i < rows.size(); i++)
        {
            const BarMenuRow& row = rows[i];
            PanelMenuRow r = { row.label.c_str(), row.enabled, row.separatorAfter };
            r.check = row.check;
            r.submenu = row.sub >= 0;
            r.selected = (int)i == selected;
            drawn.push_back(r);
        }
        return drawn;
    };

    const std::vector<BarMenuRow> rows = BarMenuRows(m_barMenu);
    if (m_barSub >= (int)rows.size() || (m_barSub >= 0 && rows[m_barSub].sub < 0))
        m_barSub = -1;
    const POINT at = { anchor.left, anchor.bottom + 2 };
    m_barMenuArea = DrawPanelMenu(hDC, at, kMenuBarItems[m_barMenu], toPanel(rows, m_barSub), SO_BAR_MENU,
        SO_BAR_MENU_ITEM, NULL, &m_barRowRects);
    m_barRowSubs.clear();
    for (const BarMenuRow& row : rows)
        m_barRowSubs.push_back(row.sub);

    m_barSubArea = { 0, 0, 0, 0 };
    if (m_barSub < 0 || m_barSub >= (int)m_barRowRects.size())
        return;
    const std::vector<BarMenuRow> subRows = BarMenuRows(m_barMenu, rows[m_barSub].sub);
    if (subRows.empty())
        return;
    // Beside the row that opened it, its first row level with that row.
    const RECT& from = m_barRowRects[m_barSub];
    const POINT subAt = { m_barMenuArea.right - 2, from.top - kOuterPad - 3 };
    m_barSubArea = DrawPanelMenu(hDC, subAt, NULL, toPanel(subRows, -1), SO_BAR_SUB_MENU, SO_BAR_SUB_ITEM,
        NULL, NULL, m_barMenuArea.left + 2);
}

void CGalaxyATMSystemRadarScreen::TickBarMenu()
{
    if (m_barMenu < 0)
        return;
    if (!Authorized() || !m_visible || m_collapsed)
    {
        CloseBarMenu();
        return;
    }
    POINT cursor;
    const bool onRadar = CursorRadarPoint(cursor);
    if (onRadar)
    {
        // Moving along the menu bar with a menu open opens the menu under the cursor.
        for (int i = 0; i < kBarMenuCount; i++)
            if (i != m_barMenu && PtInRect(&m_barAnchors[i], cursor))
            {
                OpenBarMenu(i);
                return;
            }
        // Resting on a row of the open menu opens its submenu, or shuts the one open.
        if (!PtInRect(&m_barSubArea, cursor))
            for (size_t i = 0; i < m_barRowRects.size() && i < m_barRowSubs.size(); i++)
                if (PtInRect(&m_barRowRects[i], cursor))
                {
                    const int want = m_barRowSubs[i] >= 0 ? (int)i : -1;
                    if (want != m_barSub)
                    {
                        m_barSub = want;
                        m_barSubArea = { 0, 0, 0, 0 };
                        RequestRefresh();
                    }
                    break;
                }
    }
    const bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0
        || (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    const bool inside = onRadar && (PtInRect(&m_barMenuArea, cursor) || PtInRect(&m_barSubArea, cursor)
        || PtInRect(&m_barAnchors[m_barMenu], cursor));
    if (down && !m_barMenuButtonsDown && !inside)
        CloseBarMenu();
    m_barMenuButtonsDown = down;
}
