#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <thread>
#include <atomic>
#include <memory>
#include <mutex>
#include "EuroScopePlugIn.h"
#include "Theme.h"
#include "Config.h"
#include "Sigmet.h"
#include "Atis.h"
#include "UserName.h"
#include "TextEntry.h"
#include "FloatWindow.h"
#include "RadarCursor.h"
#include "Apw.h"
#include "Squawk.h"

extern HINSTANCE g_hModule;

enum class WorkMode { Offline, Sim, Ops, Sup };

enum class AltUnit  { FL, M, FLM };
enum class VsUnit   { FtMin, MS };
enum class GsUnit   { Knots, Kmh };
enum class DistUnit { NM, Km };

class BackgroundJob
{
public:
    ~BackgroundJob() { Wait(); }

    template <class Work>
    bool Start(Work work)
    {
        if (m_busy)
            return false;
        Wait();
        m_busy = true;
        m_thread = std::thread([this, work]() { work(); m_busy = false; });
        return true;
    }

    bool Busy() const { return m_busy; }
    void Wait() { if (m_thread.joinable()) m_thread.join(); }

private:
    std::thread m_thread;
    std::atomic<bool> m_busy{ false };
};

class CGalaxyATMSystemPlugin : public EuroScopePlugIn::CPlugIn
{
public:
    CGalaxyATMSystemPlugin();
    virtual ~CGalaxyATMSystemPlugin();

    virtual EuroScopePlugIn::CRadarScreen* OnRadarScreenCreated(
        const char* sDisplayName, bool NeedRadarContent, bool GeoReferenced,
        bool CanBeSaved, bool CanBeCreated);

    virtual void OnGetTagItem(
        EuroScopePlugIn::CFlightPlan FlightPlan, EuroScopePlugIn::CRadarTarget RadarTarget,
        int ItemCode, int TagData, char sItemString[16],
        int* pColorCode, COLORREF* pRGB, double* pFontSize);

    virtual void OnNewMetarReceived(const char* sStation, const char* sFullMetar);

    virtual void OnTimer(int Counter);

    virtual void OnAirportRunwayActivityChanged();

    virtual void OnFunctionCall(int FunctionId, const char* sItemString, POINT Pt, RECT Area);
    virtual void OnFlightPlanControllerAssignedDataUpdate(
        EuroScopePlugIn::CFlightPlan FlightPlan, int DataType);
    virtual void OnFlightPlanFlightStripPushed(EuroScopePlugIn::CFlightPlan FlightPlan,
        const char* sSenderController, const char* sTargetController);

    bool IsEnglish(const std::string& callsign) const { return m_english.count(callsign) != 0; }
    void ToggleEnglish(EuroScopePlugIn::CFlightPlan fp);
    bool IsSharedMarked(const std::string& callsign) const { return m_sharedMarked.count(callsign) != 0; }
    void ToggleSharedMarker(EuroScopePlugIn::CFlightPlan fp);

    void HandleSquawkFunction(int FunctionId, const char* sItemString, RECT Area, const char* source);

    const Config& GetConfig() const { return m_config; }

    void ReloadConfig();

    std::shared_ptr<const std::vector<Sigmet>> Sigmets() const;

    std::wstring AtisIndex(const std::string& icao = "") const;
    std::wstring AtisMessage(const std::string& icao = "") const;
    std::vector<std::string> AtisAirportsOnAir() const;

    std::wstring MyUserName() const;

    bool LiveConnection() const;

    bool ListedOnNetwork() const;

    bool AccessSuspended() const;

    bool TrainingSession() const;

    enum class LoginState { Idle, Sending, Done, Failed };
    void StartLogin(const std::wstring& cid, const std::wstring& surname);
    LoginState MyLogin(std::wstring* message = NULL) const;
    void ResetLogin();

    struct SavedLogin
    {
        std::wstring cid, surname;
        bool Complete() const { return !cid.empty() && !surname.empty(); }
    };
    const SavedLogin& SavedIdentity();
    void SaveIdentity(const SavedLogin& id);

    bool SessionAuthorized() const { return m_sessionAuthorized; }
    void SetSessionAuthorized(bool on) { m_sessionAuthorized = on; }
    bool Unlocked() const { return m_sessionAuthorized || TrainingSession(); }

    std::string RegisterPageUrl() const;

    std::shared_ptr<const std::vector<ZoneBooking>> AupBookings() const;

    std::shared_ptr<const std::vector<ZoneBooking>> Notams() const;

    const std::wstring& QnhMmHg() const { return m_qnhMmHg; }
    const std::wstring& QnhHpa() const { return m_qnhHpa; }

    std::wstring TransitionLevel() const;

    int TransitionLevelFL() const;

    bool AltFilterEnabled() const { return m_altFilterEnabled; }
    int  AltFilterFromFL()  const { return m_altFilterFromFL; }
    int  AltFilterToFL()    const { return m_altFilterToFL; }
    void SetAltFilterEnabled(bool on) { m_altFilterEnabled = on; }
    void SetAltFilterFromFL(int fl)   { m_altFilterFromFL = fl; }
    void SetAltFilterToFL(int fl)     { m_altFilterToFL = fl; }

    bool AltFilterPasses(int altFt) const;

    AltUnit  UnitAlt()  const { return m_unitAlt; }
    VsUnit   UnitVs()   const { return m_unitVs; }
    GsUnit   UnitGs()   const { return m_unitGs; }
    DistUnit UnitDist() const { return m_unitDist; }
    void SetUnitAlt(AltUnit u)   { m_unitAlt = u; }
    void SetUnitVs(VsUnit u)     { m_unitVs = u; }
    void SetUnitGs(GsUnit u)     { m_unitGs = u; }
    void SetUnitDist(DistUnit u) { m_unitDist = u; }

    int  TagFontSize() const  { return m_tagFontSize; }
    void SetTagFontSize(int s) { m_tagFontSize = s; }

    const ApwResult& ApwForTarget(EuroScopePlugIn::CRadarTarget& target) { return ApwFor(target); }
    std::string AssignedSquawkFor(const EuroScopePlugIn::CFlightPlan& fp) const { return AssignedSquawk(fp); }

private:
    void StartMetarFetch();
    void ApplyQnhHpa(int hpa);
    std::string AirportIcao() const;

    void StartSigmetFetch();

    Config m_config;

    std::wstring m_qnhMmHg;
    std::wstring m_qnhHpa;

    BackgroundJob      m_metarFetch;
    std::atomic<int> m_fetchedQnhHpa{ 0 };
    bool             m_gotLiveMetar = false;

    bool m_altFilterEnabled = false;
    int  m_altFilterFromFL  = 100;
    int  m_altFilterToFL    = 600;

    int  m_tagFontSize      = 10;

    BackgroundJob m_sigmetFetch;
    mutable std::mutex m_sigmetMutex;
    std::shared_ptr<const std::vector<Sigmet>> m_sigmets;

    void StartAtisFetch();
    BackgroundJob m_atisFetch;
    mutable std::mutex m_atisMutex;
    std::map<std::string, AtisReport> m_atisLive;

    void StartIdentityFetch(const std::string& callsign);
    BackgroundJob m_identityFetch;
    mutable std::mutex m_identityMutex;
    VatsimIdentity m_identity;
    bool m_accessSuspended = false;
    std::string m_identityAskedFor;

    bool m_fontChecked = false;

    void SendLoginJob();
    void RetryLoginIfDue();
    BackgroundJob m_login;
    LoginState m_loginState = LoginState::Idle;
    SavedLogin m_pendingLogin;
    std::string m_loginPosition;
    ULONGLONG m_loginFirstTick = 0;
    ULONGLONG m_loginRetryTick = 0;
    SavedLogin m_savedLogin;
    bool m_savedLoginRead = false;
    std::wstring m_loginMessage;

    bool m_sessionAuthorized = false;
    bool m_wasUnlocked = false;
    void StartAllFetches();

    void StartAupFetch();
    void StartNotamFetch();
    BackgroundJob m_aupFetch;
    mutable std::mutex m_aupMutex;
    std::shared_ptr<const std::vector<ZoneBooking>> m_aup;

    BackgroundJob m_notamFetch;
    mutable std::mutex m_notamMutex;
    std::shared_ptr<const std::vector<ZoneBooking>> m_notams;

    SquawkClient m_squawk;

    std::map<std::string, std::string> m_squawkSetByUs;

    std::set<std::string> m_english;
    std::set<std::string> m_sharedMarked;
    bool BroadcastScratchMark(EuroScopePlugIn::CFlightPlan fp, const char* mark, bool on);

    std::string m_squawkMenuCallsign;
    RECT m_squawkMenuArea = { 0, 0, 0, 0 };

    int       m_lastSquawkFn = 0;
    ULONGLONG m_lastSquawkTick = 0;

    void ConfigureSquawk();
    bool SquawkReady(bool tell);
    std::string MyPosition() const;
    std::string AssignedSquawk(const EuroScopePlugIn::CFlightPlan& fp) const;
    COLORREF SquawkColor(const EuroScopePlugIn::CFlightPlan& fp, EuroScopePlugIn::CRadarTarget rt, const std::string& assigned) const;
    void RequestSquawk(const std::string& callsign, bool fresh);
    void SquawkMessage(const std::wstring& text);
    void SquawkDebugLine(const std::wstring& text);
    void ApplySquawkAnswers();

    void RefreshApwZones();
    std::vector<ApwZone> m_apwZones;
    ULONGLONG m_apwZonesTick = 0;

    struct ApwCacheEntry
    {
        ApwResult result;
        ULONGLONG tick = 0;
    };
    std::map<std::string, ApwCacheEntry> m_apwCache;

    const ApwResult& ApwFor(EuroScopePlugIn::CRadarTarget& target);

    void ForgetGone();

    AltUnit  m_unitAlt  = AltUnit::FL;
    VsUnit   m_unitVs   = VsUnit::FtMin;
    GsUnit   m_unitGs   = GsUnit::Knots;
    DistUnit m_unitDist = DistUnit::Km;
};

const int TAG_ITEM_ALTITUDE       = 1;
const int TAG_ITEM_VERTICAL_SPEED = 2;
const int TAG_ITEM_GROUND_SPEED   = 3;
const int TAG_ITEM_DISTANCE       = 4;

const int TAG_ITEM_APW            = 5;

const int TAG_ITEM_SQUAWK         = 6;

const int TAG_ITEM_SQUAWK_SET     = 7;

const int TAG_ITEM_CALLSIGN       = 8;

const int TAG_FUNC_SQUAWK_ASSIGN  = 400;
const int TAG_FUNC_SQUAWK_MENU    = 401;
const int FN_SQUAWK_GET           = 410;
const int FN_SQUAWK_NEW           = 411;
const int FN_SQUAWK_MANUAL        = 412;
const int FN_SQUAWK_MANUAL_EDIT   = 413;
