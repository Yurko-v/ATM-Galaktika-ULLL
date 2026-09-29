#include "pch.h"
#include "Core/Common.h"

using namespace Galaxy;

CGalaxyATMSystemPlugin* g_plugin = NULL;

ULONG_PTR g_gdiplusToken = 0;

void __declspec(dllexport) EuroScopePlugInInit(EuroScopePlugIn::CPlugIn** ppPlugInInstance)
{
    Gdiplus::GdiplusStartupInput gdiplusInput;
    const Gdiplus::Status gdiplus = Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusInput, NULL);
    if (gdiplus != Gdiplus::Ok)
        Log::Error("plugin", "GDI+ did not start (status " + std::to_string((int)gdiplus)
            + ") - vectors, wake arcs and the rulers will not be drawn");

    *ppPlugInInstance = g_plugin = new CGalaxyATMSystemPlugin();
}

void __declspec(dllexport) EuroScopePlugInExit(void)
{
    const std::set<CGalaxyATMSystemRadarScreen*> screens = g_screens;
    for (CGalaxyATMSystemRadarScreen* screen : screens)
        screen->Shutdown();
    CGalaxyATMSystemRadarScreen::ReleaseWheelHook();
    RadarCursor::DetachAll();
    FloatWindow::ReleaseClass();
    delete g_plugin;
    g_plugin = NULL;

    Theme::SharedCanvas().Release();
    if (g_gdiplusToken != 0)
    {
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        g_gdiplusToken = 0;
    }
}

static void LogConfigLoad(const Config& config, const char* when)
{
    const std::string path = Log::Utf8(config.ConfigPath());
    if (!config.LoadError().empty())
        Log::Error("config", std::string(when) + ": " + Log::Utf8(config.LoadError()));
    else
        Log::Info("config", std::string(when) + ": " + path + " - " + std::to_string(config.Zones().size())
            + " zones, " + std::to_string(config.PositionCount()) + " positions");

    if (config.SquawkServerUrl().empty())
        Log::Warn("config", "Squawk.ServerUrl is empty in " + path + " - no squawk codes, and LOGIN answers \""
            + Log::Utf8(L"База пользователей недоступна") + "\"");
}

CGalaxyATMSystemPlugin::CGalaxyATMSystemPlugin() : CPlugIn(
    EuroScopePlugIn::COMPATIBILITY_CODE,
    "Galaxy ATM System",
    "0.10.0",
    "ULLL Team",
    "©2024-2026")
{
    Log::Info("plugin", "Galaxy ATM System 0.10.0 loaded");
    m_config.Load(g_hModule);
    LogConfigLoad(m_config, "load");

    m_qnhMmHg = m_config.QnhMmHg();
    m_qnhHpa = m_config.QnhHpa();

    RegisterTagItemType("ULLL Altitude", TAG_ITEM_ALTITUDE);
    RegisterTagItemType("ULLL Vertical Speed", TAG_ITEM_VERTICAL_SPEED);
    RegisterTagItemType("ULLL Ground Speed", TAG_ITEM_GROUND_SPEED);
    RegisterTagItemType("ULLL Distance to Dest", TAG_ITEM_DISTANCE);
    RegisterTagItemType("ULLL APW", TAG_ITEM_APW);
    RegisterTagItemType("ULLL Callsign", TAG_ITEM_CALLSIGN);

    RegisterTagItemType("ULLL Squawk", TAG_ITEM_SQUAWK);
    RegisterTagItemType("ULLL Squawk set", TAG_ITEM_SQUAWK_SET);
    RegisterTagItemFunction("ULLL Squawk assign", TAG_FUNC_SQUAWK_ASSIGN);
    RegisterTagItemFunction("ULLL Squawk menu", TAG_FUNC_SQUAWK_MENU);

    m_sigmets = std::make_shared<const std::vector<Sigmet>>();
    m_aup = std::make_shared<const std::vector<ZoneBooking>>();

    ConfigureSquawk();
}

void CGalaxyATMSystemPlugin::StartAllFetches()
{
    StartMetarFetch();
    StartSigmetFetch();
    StartAtisFetch();
    StartAupFetch();
    StartNotamFetch();
}

CGalaxyATMSystemPlugin::~CGalaxyATMSystemPlugin()
{
    m_squawk.Stop();

    m_metarFetch.Wait();
    m_sigmetFetch.Wait();
    m_atisFetch.Wait();
    m_identityFetch.Wait();
    m_login.Wait();
    m_aupFetch.Wait();
    m_notamFetch.Wait();

    Theme::ReleaseEuroScopeFace();
}

void CGalaxyATMSystemPlugin::ReloadConfig()
{
    m_config.Load(g_hModule);
    LogConfigLoad(m_config, ".reload");

    m_qnhMmHg = m_config.QnhMmHg();
    m_qnhHpa = m_config.QnhHpa();
    m_gotLiveMetar = false;

    m_apwZones.clear();
    m_apwZonesTick = 0;
    m_apwCache.clear();

    if (Unlocked())
        StartAllFetches();
    ConfigureSquawk();
}

EuroScopePlugIn::CRadarScreen* CGalaxyATMSystemPlugin::OnRadarScreenCreated(
    const char* sDisplayName, bool NeedRadarContent, bool GeoReferenced,
    bool CanBeSaved, bool CanBeCreated)
{
    return new CGalaxyATMSystemRadarScreen();
}

void CGalaxyATMSystemPlugin::OnTimer(int Counter)
{
    if (!m_fontChecked)
    {
        m_fontChecked = true;
        if (!Theme::InterInstalled())
        {
            Log::Warn("font", "Inter is not installed - the sector list is drawn in Arial instead."
                " Install Inter (https://rsms.me/inter/) and restart EuroScope.");
            DisplayUserMessage("Galaxy ATM System", "Font",
                "Font Inter is not installed - the sector list (.rc) falls back to Arial."
                " Install Inter from https://rsms.me/inter/ and restart EuroScope.",
                true, true, true, true, false);
        }
    }

    const bool unlocked = Unlocked();
    m_squawk.SetPosition(unlocked && SquawkReady(false) ? MyPosition() : "");

    int ct = GetConnectionType();
    std::string position = MyPosition();
    if ((ct == CONNECTION_TYPE_DIRECT || ct == CONNECTION_TYPE_VIA_PROXY) && !position.empty())
    {
        const int period = ListedOnNetwork() ? kNamePollSeconds : kFeedPollSeconds;
        if (position != m_identityAskedFor || Counter % period == 0)
            StartIdentityFetch(position);
    }
    RetryLoginIfDue();

    const bool justUnlocked = unlocked && !m_wasUnlocked;
    m_wasUnlocked = unlocked;
    if (!unlocked)
        return;
    if (justUnlocked)
    {
        Log::Info("auth", "logged in - loading METAR, SIGMET, ATIS, AUP and NOTAM");
        m_gotLiveMetar = false;
        StartAllFetches();
    }

    ApplySquawkAnswers();

    int sigmetPeriod = max(60, m_config.SigmetRefreshMinutes() * 60);
    if (Counter > 0 && Counter % sigmetPeriod == 0)
        StartSigmetFetch();

    int atisPeriod = max(15, m_config.AtisRefreshSeconds());
    if (Counter > 0 && Counter % atisPeriod == 0)
        StartAtisFetch();

    int aupPeriod = max(60, m_config.AupRefreshMinutes() * 60);
    if (Counter > 0 && Counter % aupPeriod == 0)
        StartAupFetch();

    int notamPeriod = max(60, m_config.NotamRefreshMinutes() * 60);
    if (Counter > 0 && Counter % notamPeriod == 0)
        StartNotamFetch();

    if (Counter > 0 && Counter % 60 == 0)
        ForgetGone();

    if (m_gotLiveMetar)
        return;

    int hpa = m_fetchedQnhHpa.exchange(0);
    if (hpa > 0)
        ApplyQnhHpa(hpa);

    if (Counter > 0 && Counter % 60 == 0)
        StartMetarFetch();
}

void CGalaxyATMSystemPlugin::ForgetGone()
{
    std::set<std::string> live;
    for (CFlightPlan fp = FlightPlanSelectFirst(); fp.IsValid(); fp = FlightPlanSelectNext(fp))
        live.insert(fp.GetCallsign());
    for (CRadarTarget rt = RadarTargetSelectFirst(); rt.IsValid(); rt = RadarTargetSelectNext(rt))
        live.insert(rt.GetCallsign());

    KeepOnly(g_ram, live);
    KeepOnly(g_partnerGuess, live);
    KeepOnly(m_squawkSetByUs, live);
    KeepOnly(m_english, live);
    KeepOnly(m_apwCache, live);
    m_squawk.Forget(live);
    for (CGalaxyATMSystemRadarScreen* screen : g_screens)
        screen->ForgetGone(live);
}
