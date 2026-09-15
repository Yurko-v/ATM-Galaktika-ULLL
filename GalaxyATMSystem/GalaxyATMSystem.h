#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>
#include <map>
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
#include "Apw.h"
#include "Squawk.h"

// Handle to this DLL (set in dllmain.cpp) - used to locate the config file.
extern HINSTANCE g_hModule;

// Working mode shown in БЛОК 1.
enum class WorkMode { Offline, Sim, Ops, Sup };

// Selected unit per БЛОК 4 category. One value chosen per category, exactly
// like the mockup's per-row radio behaviour (drawn as checkboxes).
enum class AltUnit  { FL, M, FLM };
enum class VsUnit   { FtMin, MS };
enum class GsUnit   { Knots, Kmh };
enum class DistUnit { NM, Km };

// -----------------------------------------------------------------------------
// Plugin object. Owns the config and the БЛОК 4 unit choices; creates the
// radar-screen object that draws the panel.
//
// The unit settings live here rather than on CRadarScreen because
// OnGetTagItem - the thing that actually makes them affect "формуляры
// сопровождения" - is a CPlugIn method with no per-screen context. The panel
// UI (drawn per radar screen) just reads/writes through the accessors below;
// ASR persistence still happens from CRadarScreen, it just round-trips
// through the plugin instead of local fields.
// -----------------------------------------------------------------------------
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

    // Pushed by EuroScope whenever a new METAR arrives for a station it's
    // tracking; picks out our own airport's QNH (БЛОК 5's ДАВЛ) and ignores
    // everything else.
    virtual void OnNewMetarReceived(const char* sStation, const char* sFullMetar);

    // Ticks once a second; used to pick up a METAR fetched on the worker
    // thread and to re-fetch periodically.
    virtual void OnTimer(int Counter);

    // Squawks from the shared server (see Squawk.h): the "ULLL Squawk" column's
    // clicks and menu, and a code changed some other way reported back to it.
    //
    // Which of the two OnFunctionCall overrides EuroScope actually calls for a
    // TAG item function depends on where it was clicked - a tag on a radar
    // screen or a row in an AC list - so both are wired to the same handler,
    // and the handler itself drops a repeat of the same function within a few
    // hundred milliseconds in case both ever fire for one click.
    virtual void OnFunctionCall(int FunctionId, const char* sItemString, POINT Pt, RECT Area);
    virtual void OnFlightPlanControllerAssignedDataUpdate(
        EuroScopePlugIn::CFlightPlan FlightPlan, int DataType);

    // 'source' is only for the debug line ("plugin" / "screen") - see
    // Config::SquawkDebug.
    void HandleSquawkFunction(int FunctionId, const char* sItemString, RECT Area, const char* source);

    const Config& GetConfig() const { return m_config; }

    // Reads GalaxyATMSystem.json again and starts every feed over on what it
    // now says - ".reload". Everything a radar screen keeps that points into
    // the config is worked out per frame, so the only thing a reload asks of a
    // screen is to drop an open window: see OnCompileCommand.
    void ReloadConfig();

    // Сигметы, fetched on a worker thread and handed over as a whole list
    // at a time. A radar screen takes the current one for the frame it is
    // drawing and holds it for as long as it needs it, so the fetch can
    // replace the list underneath without ever pulling it out from under a
    // half-drawn overlay. Never null - an empty list before the first fetch.
    std::shared_ptr<const std::vector<Sigmet>> Sigmets() const;

    // The АТИС the panel and its two windows show: the live broadcast while the
    // network is carrying one for this aerodrome, and the config file's own
    // letter and text whenever it is not - a sweatbox session, a fetch that
    // failed, or simply no ATIS station logged on.
    std::wstring AtisIndex() const;
    std::wstring AtisMessage() const;

    // The Пользователь block's user line: the controller as "Фамилия И.О.", in
    // Russian (see RussianShortName). Found by the CID the VATSIM datafeed
    // lists for our callsign - "UserNames" in the config first, then the name
    // entered on the squawk server, then the network's own name - and, off the
    // network or before the feed has us, the
    // name EuroScope was given. The CID itself when there is no name to be had;
    // empty with no CID either.
    std::wstring MyUserName() const;

    // Controlling on the live network - the only connection with a CID for the
    // server to check a LOGIN against.
    bool LiveConnection() const;

    // The server had a name for our CID and has answered since that it has
    // none - the admin has taken us out of the base. For the rest of the
    // session LOGIN answers "Доступ приостановлен" instead of opening the Вход
    // window, and a panel that is open is shut. Cleared again once the server
    // has a name for us.
    bool AccessSuspended() const;

    // A sweatbox, a simulator or a playback - the trainer, where the panel is
    // open to whoever sits at it and the Пользователь block says "user".
    bool TrainingSession() const;

    // The Вход в систему КСА window's "Войти": the name and password, checked
    // by the squawk server on a worker thread (see SubmitLogin in UserName.h).
    // Done once the server has let us in, and the Пользователь block shows the
    // name it has for us from then on. Failed with a line for the window saying
    // why - and 'serverFault' when it is the service that failed rather than
    // what was typed, the kind of failure that opens Bypass.
    enum class LoginState { Idle, Sending, Done, Failed };
    void StartLogin(const std::wstring& surname, const std::wstring& firstName,
        const std::wstring& patronymic, const std::wstring& password);
    LoginState MyLogin(std::wstring* message = NULL, bool* serverFault = NULL) const;
    void ResetLogin();

    // The site's registration page, beside the squawk API: "http://host/api" ->
    // "http://host/register/". Empty with no server configured.
    std::string RegisterPageUrl() const;

    // The bookings that decide which restricted areas are up right now. Handed
    // over whole, like the сигметы, so a frame that starts reading them cannot
    // have them replaced underneath it. Never null.
    std::shared_ptr<const std::vector<ZoneBooking>> AupBookings() const;

    // The NOTAMs, read the same way and handed over the same way. Null - not
    // merely empty - while no source is configured or none has been read yet,
    // because an area that hangs on a NOTAM has to tell "nothing booked" from
    // "nobody asked".
    std::shared_ptr<const std::vector<ZoneBooking>> Notams() const;

    // БЛОК 5's ДАВЛ - the config's placeholder values until the first METAR
    // for the configured airport arrives, then the parsed QNH from then on.
    const std::wstring& QnhMmHg() const { return m_qnhMmHg; }
    const std::wstring& QnhHpa() const { return m_qnhHpa; }

    // БЛОК 5's Э/П - transition level derived from the current QNH (hPa):
    // QNH < 960 -> F070, QNH < 996 -> F060, QNH >= 996 -> F050.
    std::wstring TransitionLevel() const;

    // The same value as a bare flight level (50 / 60 / 70). Levels strictly
    // below it are flown on QNH, so tags there read "A025" instead of "F050".
    int TransitionLevelFL() const;

    // Фильтр высоты. Lives here rather than on the radar screen for the same
    // reason the units do: OnGetTagItem has no per-screen context and needs to
    // read it. The panel UI reads/writes through these accessors.
    bool AltFilterEnabled() const { return m_altFilterEnabled; }
    int  AltFilterFromFL()  const { return m_altFilterFromFL; }
    int  AltFilterToFL()    const { return m_altFilterToFL; }
    void SetAltFilterEnabled(bool on) { m_altFilterEnabled = on; }
    void SetAltFilterFromFL(int fl)   { m_altFilterFromFL = fl; }
    void SetAltFilterToFL(int fl)     { m_altFilterToFL = fl; }

    // True when the filter is off, or the given pressure altitude (feet) falls
    // inside the От/До band. От above До is treated as the same band either
    // way round rather than as "nothing passes".
    bool AltFilterPasses(int altFt) const;

    AltUnit  UnitAlt()  const { return m_unitAlt; }
    VsUnit   UnitVs()   const { return m_unitVs; }
    GsUnit   UnitGs()   const { return m_unitGs; }
    DistUnit UnitDist() const { return m_unitDist; }
    void SetUnitAlt(AltUnit u)   { m_unitAlt = u; }
    void SetUnitVs(VsUnit u)     { m_unitVs = u; }
    void SetUnitGs(GsUnit u)     { m_unitGs = u; }
    void SetUnitDist(DistUnit u) { m_unitDist = u; }

    // ФС "Р-р шрифта" - lives here for the same reason: OnGetTagItem scales
    // the font of this plugin's tag items by it. 12 is EuroScope's own size.
    int  TagFontSize() const  { return m_tagFontSize; }
    void SetTagFontSize(int s) { m_tagFontSize = s; }

    // For the plugin's own формуляр (CGalaxyATMSystemRadarScreen::DrawFormulars),
    // which shows what the tag items do without going through EuroScope's tag.
    const ApwResult& ApwForTarget(EuroScopePlugIn::CRadarTarget& target) { return ApwFor(target); }
    std::string AssignedSquawkFor(const EuroScopePlugIn::CFlightPlan& fp) const { return AssignedSquawk(fp); }

private:
    // EuroScope only pushes a METAR when the server sends one, so a plugin
    // loaded mid-session can wait a long time for the first QNH. To have a
    // value straight away the airport's METAR is also fetched directly, on a
    // worker thread so nothing blocks EuroScope's own thread. A real METAR
    // from EuroScope always wins once it arrives.
    void StartMetarFetch();
    void ApplyQnhHpa(int hpa);
    std::string AirportIcao() const;   // the config's airport, sanitised for a URL

    // Same shape as the METAR fetch above: a worker thread, joined before the
    // next one starts and again on shutdown, since the DLL can be unloaded
    // straight after the destructor returns.
    void StartSigmetFetch();

    Config m_config;

    std::wstring m_qnhMmHg;
    std::wstring m_qnhHpa;

    std::thread      m_metarFetch;
    std::atomic<int> m_fetchedQnhHpa{ 0 };   // handed over by the worker thread, 0 = nothing new
    bool             m_gotLiveMetar = false; // EuroScope delivered one, stop using the fetch

    bool m_altFilterEnabled = false;
    int  m_altFilterFromFL  = 100;   // e.g. 100 -> "FL100"
    int  m_altFilterToFL    = 600;

    int  m_tagFontSize      = 10;   // ФС "Р-р шрифта" until the ASR says otherwise

    std::thread m_sigmetFetch;
    mutable std::mutex m_sigmetMutex;
    std::shared_ptr<const std::vector<Sigmet>> m_sigmets;

    // Same shape again: a worker fetches the aerodrome's ATIS off the network
    // and swaps the whole report in under the lock. Empty until the first one
    // lands, which is what makes the config file the fallback.
    void StartAtisFetch();
    std::thread m_atisFetch;
    mutable std::mutex m_atisMutex;
    AtisReport m_atisLive;

    // And for our own CID and name. Asked for whenever the callsign changes,
    // then again every minute until the feed lists us - a new logon takes a
    // little while to reach it.
    void StartIdentityFetch(const std::string& callsign);
    std::thread m_identityFetch;
    mutable std::mutex m_identityMutex;
    VatsimIdentity m_identity;
    bool m_accessSuspended = false;   // under m_identityMutex - see AccessSuspended
    std::string m_identityAskedFor;   // main thread only

    // Inter looked for and, if it is missing, said so - once a session.
    bool m_fontChecked = false;

    // LOGIN on its way to the server - one at a time, its state under
    // m_identityMutex, since the name that comes back is written into
    // m_identity too.
    std::thread m_login;
    LoginState m_loginState = LoginState::Idle;
    std::wstring m_loginMessage;
    bool m_loginServerFault = false;

    // And once more for the day's airspace use plan, which is what says whether
    // a restricted area exists at this moment.
    void StartAupFetch();
    void StartNotamFetch();
    std::thread m_aupFetch;
    mutable std::mutex m_aupMutex;
    std::shared_ptr<const std::vector<ZoneBooking>> m_aup;

    std::thread m_notamFetch;
    mutable std::mutex m_notamMutex;
    std::shared_ptr<const std::vector<ZoneBooking>> m_notams;   // null until one is read

    // ---- Squawks ---------------------------------------------------------
    SquawkClient m_squawk;

    // Codes this plugin has just set on a flight plan itself, so the update
    // EuroScope raises for them is not reported back as one typed by hand.
    std::map<std::string, std::string> m_squawkSetByUs;

    // The aircraft and the cell the menu was opened on, for the item picked in
    // it and for the edit box "Ввести вручную" opens in the same place.
    std::string m_squawkMenuCallsign;
    RECT m_squawkMenuArea = { 0, 0, 0, 0 };

    // Last function handled, so one click delivered down both routes is acted
    // on once.
    int       m_lastSquawkFn = 0;
    ULONGLONG m_lastSquawkTick = 0;

    void ConfigureSquawk();
    // Configured, and connected to a network codes may be taken for - the live
    // one, or a sweatbox when the config allows it. With 'tell', says why not.
    bool SquawkReady(bool tell);
    std::string MyPosition() const;
    // The code the aircraft is meant to squawk: the server's when it holds one,
    // the flight plan's otherwise.
    std::string AssignedSquawk(const EuroScopePlugIn::CFlightPlan& fp) const;
    // Which of the column's four colours the code takes - see Theme::SquawkSet.
    COLORREF SquawkColor(const EuroScopePlugIn::CFlightPlan& fp, EuroScopePlugIn::CRadarTarget rt, const std::string& assigned) const;
    // Ask the server for a code for this aircraft, saying why if it cannot.
    void RequestSquawk(const std::string& callsign, bool fresh);
    // Into the "ULLL Squawk" message channel. ASCII only - see the note on
    // SquawkDebugLine's definition.
    void SquawkMessage(const std::string& text);
    // One line per click, when Config::SquawkDebug is on.
    void SquawkDebugLine(const std::string& text);
    // The answers that have come back: codes set on their flight plans, and
    // what went wrong with a request a controller clicked for.
    void ApplySquawkAnswers();

    // ---- APW -------------------------------------------------------------
    // The areas reduced to what the warning needs, rebuilt on the clock rather
    // than per aircraft: which of them are up changes with the day's plan, not
    // with the tag being drawn.
    void RefreshApwZones();
    std::vector<ApwZone> m_apwZones;
    ULONGLONG m_apwZonesTick = 0;

    // The last answer for each callsign. EuroScope asks for a tag item on
    // every repaint of every tag, and flying three hundred areas forward for
    // each of those is work worth doing once a second and no oftener.
    struct ApwCacheEntry
    {
        ApwResult result;
        ULONGLONG tick = 0;    // when it was worked out
    };
    std::map<std::string, ApwCacheEntry> m_apwCache;

    // The warning for one track, from the cache when it is fresh enough.
    const ApwResult& ApwFor(EuroScopePlugIn::CRadarTarget& target);

    AltUnit  m_unitAlt  = AltUnit::FL;
    VsUnit   m_unitVs   = VsUnit::FtMin;
    GsUnit   m_unitGs   = GsUnit::Knots;
    DistUnit m_unitDist = DistUnit::Km;
};

// ---- Tag item type ids (БЛОК 4's effect on формуляры сопровождения) --------
const int TAG_ITEM_ALTITUDE       = 1;
const int TAG_ITEM_VERTICAL_SPEED = 2;
const int TAG_ITEM_GROUND_SPEED   = 3;
const int TAG_ITEM_DISTANCE       = 4;

// APW - Area Proximity Warning: the track is in, or is about to be in, an area
// it must stay out of. See Apw.h for what raises it.
const int TAG_ITEM_APW            = 5;

// The squawk the shared server holds for the aircraft - the Departure list
// column. See Squawk.h.
const int TAG_ITEM_SQUAWK         = 6;

// For the formular: the code the transponder is actually showing, in the
// column's yellow, and only when it is not the code assigned. Blank otherwise.
const int TAG_ITEM_SQUAWK_SET     = 7;

// The callsign, only so a формуляр can be built entirely out of this plugin's
// items and every line of it follows ФС "Р-р шрифта" - EuroScope's own
// callsign item keeps whatever size EuroScope gives it.
const int TAG_ITEM_CALLSIGN       = 8;

// The column's clicks and the menu items they lead to. Kept clear of the radar
// screen's own FN_ ids (300 and up).
const int TAG_FUNC_SQUAWK_ASSIGN  = 400;   // one click: take a code from the server
const int TAG_FUNC_SQUAWK_MENU    = 401;   // the menu below
const int FN_SQUAWK_GET           = 410;   // "Выдать код"
const int FN_SQUAWK_NEW           = 411;   // "Новый код"
const int FN_SQUAWK_MANUAL        = 412;   // "Ввести вручную" - opens the edit box
const int FN_SQUAWK_MANUAL_EDIT   = 413;   // what was typed into it

// What a click on one item of the plugin's формуляр starts, through
// CRadarScreen::StartTagFunction: the tag item it stands for and the function a
// left and a right click call, each named by the plug-in that provides it -
// NULL for EuroScope's own, and a function id of 0 for a button that does
// nothing there. The values are the sector tag's own (see DrawFormulars).
struct FormularFn
{
    const char* itemPlugin;
    int         itemCode;
    const char* leftPlugin;
    int         leftFn;
    const char* rightPlugin;
    int         rightFn;
};

// One measuring line: each endpoint either tracks a radar target live (by
// callsign) or sits at a fixed geo position picked by the click.
struct RulerLine
{
    bool startSnapped = false;
    bool endSnapped = false;
    std::string startCallsign;
    std::string endCallsign;
    EuroScopePlugIn::CPosition startFixed;  // used when not snapped, and as the
    EuroScopePlugIn::CPosition endFixed;    // last-known fallback if a snapped target disappears

    // The bearing/distance/time readout is draggable, so it can be pulled off
    // whatever it happens to be covering. It stays attached to the line: the
    // offset is measured in pixels from the default position at the end of the
    // midpoint tick, and the tick is redrawn to reach wherever it was dragged.
    // Left at (0,0) the label sits exactly where an undragged one always did.
    POINT labelOffset = { 0, 0 };
    POINT labelAnchor = { 0, 0 };   // that default position, refreshed every frame
    RECT  labelRect = { 0, 0, 0, 0 };  // where it was actually drawn, for hit testing
};

// One line of the "Список РЦ" window: the fourteen cells as they will be
// printed, plus the two things that are read by colour rather than by text.
// Built fresh every frame from the live flight plans - nothing here is state.
struct SectorListRow
{
    std::wstring cells[14];
    bool mine = false;      // I am the tracking controller: yellow ground, lower pane
    int  crdState = 0;      // COORDINATION_STATE_... - colours the "Крд" cell
    std::string callsign;   // for the click that selects the aircraft
};

// -----------------------------------------------------------------------------
// Radar-screen object: draws the control panel and handles interaction. One
// instance per display (ASR). The panel is docked to the right edge of the
// radar area and does not move. Layout mirrors "KSA UVD GALAKTIKA.svg": a
// single 212 px card with no title bar, carrying a clock/date header and then
// Таймер, Пользователь, Векторы, ОС, Ед. изм., Фильтр высоты, the captionless
// code block and the aerodrome block, in that order.
// -----------------------------------------------------------------------------
class CGalaxyATMSystemRadarScreen : public EuroScopePlugIn::CRadarScreen
{
public:
    CGalaxyATMSystemRadarScreen();
    virtual ~CGalaxyATMSystemRadarScreen();

    virtual void OnRefresh(HDC hDC, int Phase);
    virtual void OnClickScreenObject(int ObjectType, const char* sObjectId, POINT Pt, RECT Area, int Button);
    virtual void OnButtonDownScreenObject(int ObjectType, const char* sObjectId, POINT Pt, RECT Area, int Button);
    virtual void OnButtonUpScreenObject(int ObjectType, const char* sObjectId, POINT Pt, RECT Area, int Button);
    virtual void OnDoubleClickScreenObject(int ObjectType, const char* sObjectId, POINT Pt, RECT Area, int Button);
    virtual void OnMoveScreenObject(int ObjectType, const char* sObjectId, POINT Pt, RECT Area, bool Released);
    virtual void OnFunctionCall(int FunctionId, const char* sItemString, POINT Pt, RECT Area);
    virtual bool OnCompileCommand(const char* sCommandLine);
    virtual void OnAsrContentToBeClosed(void);
    virtual void OnAsrContentToBeSaved(void);
    virtual void OnAsrContentLoaded(bool Loaded);

private:
    CGalaxyATMSystemPlugin* Plugin() { return (CGalaxyATMSystemPlugin*)GetPlugIn(); }

    // ---- Drawing (each returns the Y just below what it drew) ----
    int  PanelTop();                            // the docked top edge, toolbar-anchored
    void DrawPanel(HDC hDC);
    int  DrawHeader(HDC hDC, int y);            // БЛОК 1 - Время / Дата / Режим (also the drag handle)
    int  DrawBlockTimer(HDC hDC, int y);        // Таймер
    int  DrawBlockUser(HDC hDC, int y);         // БЛОК 2 - Данные пользователя
    int  DrawBlockVectors(HDC hDC, int y);      // БЛОК 3 - Вектор экстраполяции
    int  DrawBlockOs(HDC hDC, int y);           // ОС - размер формуляра сопровождения
    int  DrawBlockUnits(HDC hDC, int y);        // БЛОК 4 - Единицы измерения
    int  DrawBlockAltFilter(HDC hDC, int y);    // Фильтр высоты
    int  DrawBlockCodes(HDC hDC, int y);        // ВВ1 / Коды бедствия / Двойной код (no caption)
    int  DrawBlockAerodrome(HDC hDC, int y);    // БЛОК 5 - Аэродром
    int  DrawBlockAuth(HDC hDC, int y);         // Авторизация - the only block until "Вход" goes through
    void DrawAtisWindow(HDC hDC);
    void DrawAtisLetterWindow(HDC hDC);

    // The system's menu bar - Настройки, Вид, ... Справка - laid over the top
    // of the radar edge to edge, with the panel hanging under it, so the two
    // read as one frame. It stands exactly where TopSky draws its own menu, and never
    // shorter than Config::AtisTopOffset says that menu is, so it covers it.
    void DrawMenuBar(HDC hDC);
    int  MenuBarHeight();       // the INDEX АТИС strip stands right under it

    // "Список РЦ" - the sector list: every flight this sector is concerned
    // with, one row each, in the twelve columns the real system prints. Opened
    // and closed by ".rc"; dragged by its title bar, closed by its "x", and
    // its sort order and filter are remembered in the ASR.
    void DrawSectorListWindow(HDC hDC);
    void BuildSectorList(std::vector<SectorListRow>& out);
    void ScrollAtisTo(POINT pt, RECT track);   // maps a click/drag on the scrollbar to a scroll offset
    void SetVvGainFrom(POINT pt);              // maps a click/drag on the ВВ1 slider to 0..100

    // The ВВ1 slider drives the radar scale: up zooms in, down zooms out. The
    // two run in both directions - the slider sets the display area, and a zoom
    // made any other way (mouse wheel, a preset) puts the slider back in step.
    void   ApplyZoomFromSlider();
    void   SyncSliderFromZoom();
    double DisplaySpanNM();                    // current north-south extent of the display area
    double DisplayWidthNM();                   // and its east-west extent, edge to edge
    static double GainToSpanNM(int gain);
    static int    SpanNMToGain(double spanNM);

    // Сигметы, drawn under the tags: each area is a dark blue outline with no
    // fill, so the traffic and the map inside it stay readable. Holding the
    // left button down on an outline opens a half-transparent black window
    // with the report in it; releasing the button closes it again.
    void DrawSigmets(HDC hDC);
    void RegisterSigmetObjects();   // hit-boxes, a phase later than the drawing
    void DrawSigmetInfo(HDC hDC);
    // The pixel outline of one of a report's rings for this frame, empty when
    // it is off screen. Shared by the drawing, the hit-boxes and the hit test,
    // so the shape that is tested is exactly the shape that was drawn.
    bool SigmetOutline(const std::vector<EuroScopePlugIn::CPosition>& ring,
        std::vector<POINT>& out);
    int  FindSigmetAt(POINT pt);   // index into the frame's snapshot, -1 if none
    void CloseSigmetInfoIfButtonReleased();

    // Зоны запретов и ограничений, drawn the same way and in the same phase as
    // the сигметы above: an outline coloured by what kind of zone it is, its
    // designator on the area itself, and a window of details on a click.
    void DrawZones(HDC hDC);
    void RegisterZoneObjects();
    void DrawZoneInfo(HDC hDC);
    bool ZoneOutline(const Zone& zone, std::vector<POINT>& out);
    int  FindZoneAt(POINT pt);
    // The area a hit-box belongs to, off the box's own id - what a press falls
    // back on when it landed on a box but on no outline.
    int  ZoneFromObjectId(const char* sObjectId);
    // Which areas are up at this moment, worked out once at the top of the
    // frame so the outlines, the hit-boxes and the open window can never
    // disagree - a zone that is not drawn must not be clickable either.
    void UpdateZoneActivity();

    // Формуляр сопровождения, drawn by the plugin rather than by EuroScope's
    // tag - EuroScope spaces a tag's lines by its own symbology size and gives a
    // plug-in no way to change that, so a larger ФС "Р-р шрифта" ran each line
    // into the next. Rebuilt every frame from the live picture; all that is
    // kept per aircraft is where its label was dragged to, and where on it
    // each item landed this frame, for the click.
    struct FormularItem
    {
        RECT rect;
        const FormularFn* fn;   // NULL: a click only selects the aircraft
        std::string text;       // as drawn, for StartTagFunction
    };
    struct FormularState
    {
        POINT offset = { 0, 0 };   // the callsign line's left end (mid-height), from the target, px
        POINT callsignAt = { 0, 0 };   // that point on the screen this frame - the leader's end, the drag's grip
        bool  placed = false;      // offset set - by the default or by a drag
        bool  highlighted = false; // callsign lit up orange by a middle click
        POINT anchor = { 0, 0 };   // the target this frame
        RECT  area = { 0, 0, 0, 0 };
        std::vector<FormularItem> items;
    };
    std::map<std::string, FormularState> m_formulars;   // by callsign
    bool  m_formularsVisible;   // ".formular"
    std::string m_formularHover;   // callsign of the label under the cursor - the expanded one
    HFONT m_formularFont;       // re-created only when TagFontSize changes
    int   m_formularFontSize;
    HFONT GetFormularFont();
    // Which of the ULLL wiki's labels is drawn: "Формуляр РДЦ (Контроль)",
    // "Формуляр ДПК/ДПП (Круг/Подход)" or "Формуляр КДП (Вышка)". Picked by the
    // position I am logged in on - APP gets the approach label, TWR, GND and
    // DEL the tower one, everything else the РДЦ one - unless ".formular ctr",
    // "app" or "twr" has fixed it (".formular auto" hands it back).
    enum class FormularKind { Ctr, App, Twr };
    enum class FormularKindSetting { Auto, Ctr, App, Twr };
    FormularKindSetting m_formularKindSetting;   // persisted in the ASR
    FormularKind CurrentFormularKind();

    // With registerObjects false the labels are drawn but take no clicks -
    // while the ruler is armed, so its canvas gets them.
    void  DrawFormulars(HDC hDC, bool registerObjects);

    // Метки - the aircraft position symbols, drawn by the plugin as the
    // формуляр is and in the формуляр's colour: TopSky's own track symbols out
    // of the TopSkySymbols.txt next to the TopSky.dll that is loaded, picked
    // the way TopSky picks them (primary / without DAPs / with DAPs, their
    // diverging variant on RAM or CLAM, the uncontrolled one for a VFR flight
    // nobody has assumed).
    void  DrawTargetSymbols(HDC hDC);
    // What the last DrawTargetSymbols did with the targets, for ".symbols".
    struct SymbolStats
    {
        int targets = 0, offRadar = 0, filtered = 0, noSymbol = 0, drawn = 0;
    };
    SymbolStats m_symbolStats;
    void  FormularClick(const char* sCallsign, POINT pt, int button);

    // AHDG pulled with the button held: a line from the aircraft to the cursor
    // and the heading it points along, assigned when the button is let go.
    bool  m_hdgDragging;        // a press on AHDG is being moved
    bool  m_hdgDragMoved;       // ...far enough to be a pull rather than a click
    POINT m_hdgDragStart;
    POINT m_hdgDragPt;          // the cursor, radar pixels
    std::string m_hdgDragCallsign;
    ULONGLONG m_hdgDragEndTick; // a pull just ended - the click that may follow it is not one
    bool  m_hdgDragCancelled;   // dropped mid-press - EuroScope's moves ignored until the button is let go
    int   m_hdgReleaseTicks;    // polls the left button has been seen up during a pull
    // The heading a pull to 'cursor' assigns: from the aircraft to the cursor,
    // magnetic, to the nearest 5 degrees; 0 when there is no aircraft or the cursor is on
    // it. Optionally hands back the aircraft in radar pixels and the distance.
    int   DragHeading(const char* sCallsign, POINT cursor, POINT* from = NULL, double* distNm = NULL);

    // Vectors drawn over radar targets (БЛОК 3's actual effect on the radar,
    // as opposed to the panel controls that configure it).
    void DrawTargetVectors(HDC hDC);
    void DrawWakeArcs(HDC hDC);
    void DrawTrackVector(HDC hDC, EuroScopePlugIn::CRadarTarget rt, double lengthNM,
        double timeMinForLevel, int minuteTicks, COLORREF color);
    void DrawPlanVector(HDC hDC, EuroScopePlugIn::CFlightPlan fp,
        const EuroScopePlugIn::CPosition& currentPos, int minutes, COLORREF color);
    EuroScopePlugIn::CPosition CalculateDestinationPoint(
        EuroScopePlugIn::CPosition start, double bearingDeg, double distanceNM);
    COLORREF GetTagColorForFlightPlan(EuroScopePlugIn::CFlightPlan fp);

    // Ruler: any number of plain beige distance/bearing/time lines. A press of
    // the side mouse button arms one - the cursor picks up a crosshair - and
    // two ordinary left clicks then draw it: the first on the point the line
    // is to run from, the second on the point it runs to, with the line
    // following the cursor in between. Nothing is held down and no drag is
    // captured, so the radar pans as usual throughout. An endpoint placed near a radar target
    // snaps to it by callsign and keeps tracking that target's live position
    // (and, for the start point, ground speed) every frame rather than
    // freezing where it was placed. ".rulerclear" removes all of them;
    // right-click removes whichever one is nearest the cursor, and ".ruler"
    // abandons a line still being placed.
    void   DrawRulerLine(HDC hDC, RulerLine& r, int index = -1);
    void   PollRulerButton();     // fast-timer sample of the side mouse button
    void   PlaceRulerPoint(POINT pt);   // one click: anchor the start, or fix the end
    void   DrawRulerCursor(HDC hDC);    // armed crosshair, shown until the start is placed
    void   UpdateRulerEnd(POINT pt);
    bool   CursorRadarPoint(POINT& out, HWND* view = NULL);   // cursor in this screen's radar pixels, and the window they belong to
    bool   FindNearbyTarget(POINT pt, std::string& callsignOut);
    EuroScopePlugIn::CPosition ResolveRulerPoint(bool snapped, const std::string& callsign,
        EuroScopePlugIn::CPosition& fixed);
    double GetRulerSpeedKt(const RulerLine& r);   // -1 if the start point isn't snapped to a target
    int    FindNearestRulerIndex(POINT pt, double thresholdPx);
    void   RemoveRulerNear(POINT pt);   // shared by right-click and left double-click removal

    // Small reusable widgets. Each registers its own screen object for hit
    // testing and advances nothing itself - callers manage the layout cursor.
    RECT DrawBlockFrame(HDC hDC, int top, const std::wstring& caption, int boxHeight);
    RECT DrawBoxOnly(HDC hDC, int top, int boxHeight);   // captionless group box
    void DrawCheckbox(HDC hDC, RECT box, bool checked, int objType, const char* objId, const char* tooltip);
    void DrawCheckRow(HDC hDC, int top, int x, const std::wstring& label, bool checked, int objType, const char* objId, const char* tooltip);
    void DrawRadioRow(HDC hDC, int top, int x, int labelRight, const std::wstring& label, bool selected, int objType, const char* objId, const char* tooltip);
    void DrawOutlinedField(HDC hDC, RECT box, const std::wstring& text, HFONT font);
    void DrawFittedField(HDC hDC, RECT box, const std::wstring& text);   // shrinks the text to fit the plate
    void DrawToggleChip(HDC hDC, RECT box, const std::wstring& text, bool active, int objType, const char* objId, const char* tooltip,
        COLORREF idleFill = Theme::ControlFill);
    void DrawDropdownField(HDC hDC, RECT box, const std::wstring& text, int objType, const char* objId, const char* tooltip);

    // ---- Derived / external data ----
    WorkMode GetWorkMode(std::wstring& labelOut, COLORREF& colorOut);
    void     GetUserInfo(std::wstring& designation, std::wstring& role, std::wstring& user);

    // The two read-only lines of the code block, recomputed from the current
    // radar picture each frame: callsigns squawking 7500/7600/7700, and the
    // codes that more than one aircraft is currently transmitting.
    std::wstring GetDistressCodes();
    std::wstring GetDuplicateCodes();

    // ---- Popup helpers ----
    void OpenAltFilterPicker(RECT area, bool isFrom);

    // The Д/Э value pickers are drawn by the plugin itself instead of using
    // EuroScope's default popup list, so they match the panel's own look.
    // Opened by clicking a field's chevron, closed by picking a value, by
    // clicking the same chevron again, or by any other click on the panel.
    enum class DropdownKind { None, VecDist, VecTime, OsFont };
    void DrawDropdownList(HDC hDC);

    // ---- Geometry ----
    int GroupLeft()    const { return m_panelArea.left + kOuterPad; }
    int GroupRight()   const { return m_panelArea.right - kOuterPad; }
    int ContentLeft()  const { return GroupLeft() + kInnerPad; }
    int ContentRight() const { return GroupRight() - kInnerPad; }
    int ContentWidth() const { return ContentRight() - ContentLeft(); }

    Theme::FontSet m_fonts;

    // EuroScope's own tag font, taken off the DC at the top of OnRefresh - the
    // SDK offers no way to ask for it. Used at full size for the predicted-
    // level label on an extrapolation vector and for the ruler's own
    // bearing/distance/time annotation, so both read as part of the same
    // radar picture as the tags around them rather than a second typeface.
    HFONT m_esFont;

    // A slightly smaller derivative of m_esFont, used only for the ruler's
    // bearing/distance/time readout (see GetRulerFont) - re-derived only when
    // the tag font it is copied from actually changes, not every frame.
    HFONT m_rulerFont;
    HFONT m_rulerFontSource;
    HFONT GetRulerFont(HFONT baseFont);

    RECT  m_panelArea;          // current on-screen rectangle; derived every
                                 // frame from the radar area, never stored
    bool  m_visible;            // panel shown at all (".ulll")
    bool  m_collapsed;          // collapsed to the small clock/date window

    // Авторизация. LOGIN opens the Вход window, and the panel opens once the
    // squawk server has taken the name and password typed there, after a short
    // staged "проверка". Until it has, the
    // panel is the header and the Авторизация block alone, and the things the
    // panel drives (its windows, the ruler, the vectors) stay off. In the
    // trainer nobody is asked: the panel is simply open. Deliberately not saved
    // to the ASR, so every session starts at the login.
    enum class AuthState { LoggedOut, Checking, LoggedIn };
    AuthState m_authState;
    ULONGLONG m_authStartTick;  // when "Вход" was pressed
    std::wstring m_authMessage; // why LOGIN did not open the panel - on the Авторизация card until the next LOGIN
    bool Authorized() { return m_authState == AuthState::LoggedIn || Plugin()->TrainingSession(); }
    void TickAuth();            // fast timer: animates the check and ends it
    void StartAuthCheck();      // the server let the controller in - or Bypass

    // Bypass - past the base, for when the base is what is broken. Only once
    // the last LOGIN was turned away by an error of the service's (no network,
    // no server, no answer) - never for a wrong name or password: pressed
    // before that, it tells the controller to register instead. Never past a
    // suspension.
    bool m_authFailed;          // the last LOGIN ended in an error of the service's
    bool m_authBypassed;        // this panel was opened by Bypass
    bool BypassAvailable();

    // A notice over the middle of the radar - "Пожалуйста, зарегистрируйтесь...",
    // "Доступ приостановлен" - until its "OK" or "x". Empty text: none up.
    std::wstring m_noticeText;
    void ShowNotice(const std::wstring& text);
    void DrawNoticeWindow(HDC hDC);

    // Вход в систему КСА: the window LOGIN opens. Фамилия, Имя, Отчество and
    // Пароль, as registered on the site, typed into edit boxes laid over its
    // fields (TextEntry), and "Войти", which sends them to the server
    // (CGalaxyATMSystemPlugin::StartLogin). There is no other way past it but
    // Bypass after a failure of the service's. Its "x" closes it and leaves
    // the panel shut, until LOGIN opens it again.
    enum LoginField { LF_SURNAME, LF_FIRST_NAME, LF_PATRONYMIC, LF_PASSWORD, LF_COUNT };
    bool m_loginWindowOpen;
    std::wstring m_loginValues[LF_COUNT];   // as typed; the password is wiped once sent, and with the window
    std::wstring m_loginProblem;            // why "Войти" did not send, until the next edit
    RECT m_loginFields[LF_COUNT];           // where each field was drawn last, in radar pixels
    RECT m_loginArea;                       // where the window stands - dragged by its title bar
    bool m_loginPositioned;                 // put in the middle of the radar once, then left where it is dragged
    ULONGLONG m_loginDrawnTick;             // when it was last drawn - a box is not left over a window no longer on screen
    void DrawLoginWindow(HDC hDC);
    void CloseLoginWindow();
    void SendLogin();

    // The edit box over one of the Вход window's fields. Opened on the fast
    // tick after the click rather than in it, so EuroScope, finishing with the
    // click, cannot take the focus straight back from it.
    TextEntry m_entry;
    int  m_entryField;          // the field the box is over, or -1
    int  m_entryPending;        // the field to open it over on the next tick, or -1
    ULONGLONG m_entryPendingTick;   // when that was asked for - the box waits for a frame drawn since
    HWND m_entryView;           // the radar view, found under the cursor at the click
    void EditLoginField(int field);
    void TickEntry();           // fast timer: opens the box that is pending
    void CommitEntry();         // the open box's text into its field, and the box closed
    POINT m_dragOffset;         // cursor->window offset captured on an АТИС drag
    UINT_PTR m_timerId;         // 1s tick that keeps the clock live
    UINT_PTR m_pollTimerId;     // fast tick that watches the side mouse buttons

    // Таймер - run by hand from the "С" chip: left click starts and stops it,
    // right click zeroes it. Elapsed time is derived each frame from
    // GetTickCount64() rather than stored, so it cannot drift.
    bool      m_timerRunning;
    ULONGLONG m_timerStartTick;
    ULONGLONG m_timerElapsedMs;   // accumulated while stopped; frozen display value

    // БЛОК 3 - Вектор экстраполяции
    bool m_vecDistEnabled;
    int  m_vecDistKm;           // 5..50 step 5
    bool m_vecTimeEnabled;
    int  m_vecTimeMin;          // 1/2/3/4/5/10/15
    bool m_vecByPlan;           // "Вектор по плану"
    bool m_vecShowLevel;        // "Расчётный эшелон"

    DropdownKind m_openDropdown;
    RECT m_vecDistFieldRect;    // the list hangs off the whole field, not just its chevron
    RECT m_vecTimeFieldRect;

    // ОС - how the track label ("формуляр сопровождения") is laid out. The
    // reference offers a two- or three-line label plus an independent speed
    // line. Both are stored and persisted, and lay out the plugin's own
    // формуляр (DrawFormulars).
    int  m_osLines;             // 2 or 3
    bool m_osSpeed;
    RECT m_osFontFieldRect;     // "Р-р шрифта" - the size itself is the plugin's TagFontSize()

    // Code block. "ВВ1" names the secondary-radar source; ВСЕ / БП and the
    // small right-hand checkbox pick what it passes through, and the slider is
    // that source's video gain. EuroScope exposes no radar-source controls, so
    // these keep their state (and persist) without acting on the picture; the
    // two code readouts below them are live.
    bool m_codeAll;             // "ВСЕ"
    bool m_codeBp;              // "БП"
    bool m_codeExtra;           // the unlabelled checkbox at the end of the row
    std::wstring m_codeFilter;  // the inline entry field next to "БП"
    int  m_vvGain;              // 0..100, the vertical slider - the radar zoom
    bool m_vvDragging;          // slider held; stops the zoom sync fighting the cursor
    RECT m_vvSliderRect;        // recomputed each frame; reused by the drag handler

    // Фильтр высоты state lives on CGalaxyATMSystemPlugin (see GalaxyATMSystem.h) so that
    // OnGetTagItem can read it too.

    // БЛОК 4's unit choices live on CGalaxyATMSystemPlugin (Plugin()->UnitAlt() etc.)
    // so OnGetTagItem can read them too - see the comment there.

    // БЛОК 5 - окно АТИС. The scroll range and thumb height are recomputed
    // from the measured text every frame and reused by the drag handler.
    //
    // Two windows, not one. The little one carries nothing but the current
    // index letter and lives on the radar outside the panel entirely, with no
    // control of its own in it: it stands there by default, is moved by its
    // title bar, closed by its "x" and brought back with ".atis", and its open
    // state is remembered in the ASR. The message window is the report itself,
    // opened either by the panel's "АТИС" button or by clicking the letter.
    bool m_atisLetterOpen;
    RECT m_atisLetterArea;      // docked to the panel, recomputed every frame

    // "Список РЦ". The rows themselves are never stored - they are rebuilt from
    // the live flight plans on every frame - so all that lives here is how the
    // window is arranged: where it stands, which page each pane is turned to
    // and what it is sorted by.
    bool m_rcOpen;
    RECT m_rcArea;
    bool m_rcPositioned;
    int  m_rcScroll;        // first visible row of the upper pane, a page at a time
    int  m_rcScrollMine;    // and of the lower one
    int  m_rcSortKey;       // the column it is sorted by, index into kRcColumns
    bool m_rcSortAsc;

    // How big it is drawn: percent of "New Window.svg"'s own size, 40 being
    // the two fifths it started at. Set by pulling the grip in its bottom
    // right corner, or by ".rc <percent>"; remembered in the ASR. Its fonts
    // are made for the size and made again when it changes.
    int   m_rcScale;
    bool  m_rcResizing;     // the grip is being pulled
    int   m_rcResizeGrab;   // the window's right edge minus the cursor when the pull began
    HFONT m_rcFont;         // caption and headings
    HFONT m_rcRowFont;      // the values in the rows
    int   m_rcFontScale;    // the scale those two were made for, 0 for none

    // The filter strip under the two panes, each field set through EuroScope's
    // edit box and remembered in the ASR. Рейс: only callsigns with that in
    // them. До (мин): a flight still outside is listed only once it enters
    // within that many minutes. После (мин): a flight that has left stays that
    // many minutes. Empty text / -1: no limit, as before there was a strip.
    std::wstring m_rcFilterCallsign;
    int  m_rcFilterBefore;
    int  m_rcFilterAfter;
    // When each flight was last inside the sector or on its way in - EuroScope
    // says when a flight will enter and leave, never how long ago it left.
    std::map<std::string, ULONGLONG> m_rcLastInSector;

    bool m_atisOpen;
    int  m_atisScrollPx;
    int  m_atisScrollMax;
    int  m_atisThumbH;
    RECT m_atisArea;
    bool m_atisPositioned;

    // Сигметы. The list itself belongs to the plugin (one fetch feeds every
    // display); the screen takes a reference to it at the top of each frame so
    // the overlay it draws, the hit test that follows and the info window it
    // opens are all looking at the same list even if a fetch lands mid-frame.
    std::shared_ptr<const std::vector<Sigmet>> m_sigmets;
    bool m_sigmetsVisible;      // ".sigmet" toggle, persisted in the ASR
    int  m_sigmetInfoIndex;     // report whose window is open, -1 for none
    POINT m_sigmetInfoAt;       // where the button went down, the window hangs off it
    bool m_sigmetInfoHeld;      // the poll has confirmed the button really is down
    int  m_sigmetInfoWait;      // polls spent waiting for that confirmation

    // Зоны запретов и ограничений. Static geometry off the config file rather
    // than a feed, so there is nothing to hold a reference to - the screen
    // reads the plugin's list straight. The details window behaves exactly as
    // a сигмет's does: up while the button is held, gone when it is released,
    // with the same two fields guarding against a press that arrives late.
    bool m_zonesVisible;        // ".zones" toggle, persisted in the ASR
    int  m_zoneInfoIndex;       // zone whose window is open, -1 for none
    POINT m_zoneInfoAt;         // where the button went down, the window hangs off it
    bool m_zoneInfoHeld;

    // Shift, as last seen. Зоны take the mouse only while it is held -
    // without it their hit-boxes are not registered at all, so a label lying
    // over one can still be dragged (see OnRefresh).
    bool m_areaShiftDown;
    int  m_zoneInfoWait;

    // Filled by UpdateZoneActivity, one entry per configured area. The plan is
    // held for the whole frame because m_zoneBooking points into it.
    std::shared_ptr<const std::vector<ZoneBooking>> m_aup;
    std::shared_ptr<const std::vector<ZoneBooking>> m_notams;   // null while none has been read
    std::vector<char> m_zoneActive;
    std::vector<const ZoneBooking*> m_zoneBooking;   // the booking that put it up, or null

    // Ruler. A side ("thumb") mouse button draws the lines: EuroScope only ever
    // reports LEFT/MIDDLE/RIGHT to a plug-in, so the button is read directly
    // off the keyboard state on a fast timer instead of arriving as an event.
    int  m_rulerButton;         // VK_XBUTTON1 / VK_XBUTTON2, or 0 to disable
    bool m_rulerButtonDown;     // previous sample, so only the press edge counts

    // The timer only latches the press; it is acted on in OnRefresh, where the
    // SDK's coordinate and radar-target calls belong.
    bool m_rulerPressPending;
    bool m_rulerArmed;          // side button pressed - the next left click anchors the start
    bool m_rulerPlacing;        // a line is being placed - start anchored, end on the cursor
    RulerLine m_rulerPending;   // that line; it joins m_rulers once its end is fixed
    std::vector<RulerLine> m_rulers;

    // The card is the reference export's 212 px of content plus the wider
    // outer padding below, so every block's own horizontal geometry - all of
    // it measured from the group box, never from the panel - is left exactly
    // where it was by the bigger margin.
    static const int kPanelWidth = 218;
    // Collapsed the card shrinks to fit the date line ("08.09.2026 OFFLINE")
    // and the "+" beside it, and nothing else.
    static const int kCollapsedWidth = 154;
    static const int kOuterPad = 7;    // panel edge -> group box edge
    static const int kInnerPad = 6;    // group box edge -> content

    // Checkboxes are the same small square the code block's own checkbox is
    // rather than a size of their own, so the panel reads as one control set.
    // The pills of "Ед. изм." are a hair larger so they still read as circles.
    static const int kCheckSize = 13;  // checkbox side
    static const int kRadioSize = 14;  // "Ед. изм." pill diameter
    static const int kRowH      = 16;  // the row either of them is centred in
};

// ---- Screen object type ids -------------------------------------------------
// 1 was the header drag handle - the panel is docked to the right edge of the
// radar area now and cannot be moved, so nothing claims the header any more.

const int SO_PANEL_COLLAPSE = 2;   // "-"/"+" in the top-right corner - collapses the panel to a small clock window

const int SO_TIMER_TOGGLE = 5;     // "C" chip - start/stop the Таймер stopwatch

const int SO_AUTH_LOGIN   = 90;    // "LOGIN" on the menu bar - starts the Авторизация check
const int SO_AUTH_BYPASS  = 96;    // "Bypass" beside it - past the base, after a LOGIN that failed

// The notice window - see CGalaxyATMSystemRadarScreen::ShowNotice.
const int SO_NOTICE_WINDOW = 84;   // the whole card, so a click on it never reaches the radar
const int SO_NOTICE_OK     = 85;
const int SO_NOTICE_CLOSE  = 86;

const int SO_MENU_BAR     = 91;    // the whole menu bar, so a click on it never reaches TopSky's menu below

// Вход в систему КСА - the window LOGIN opens.
const int SO_LOGIN_WINDOW   = 93;  // the whole card, so a click on it never reaches the radar
const int SO_LOGIN_FIELD    = 94;  // one of its fields - sObjectId is its LoginField
const int SO_LOGIN_SEND     = 95;  // "Войти"
const int SO_LOGIN_HEADER   = 97;  // its title bar - drag handle
const int SO_LOGIN_CLOSE    = 98;  // the "x" on that bar - closes it, the panel stays shut
const int SO_LOGIN_REGISTER = 99;  // the registration page's address - opens it in the browser

const int SO_ALTFILTER_FROM     = 6;
const int SO_ALTFILTER_TO       = 7;
const int SO_ALTFILTER_USE_CHK  = 8;

const int SO_VEC_DIST_TOGGLE = 10;
const int SO_VEC_DIST_FIELD  = 11;
const int SO_VEC_TIME_TOGGLE = 12;
const int SO_VEC_TIME_FIELD  = 13;
const int SO_VEC_BY_PLAN_CHK = 14;
const int SO_VEC_LEVEL_CHK   = 15;

const int SO_UNIT_ALT_FL   = 20;
const int SO_UNIT_ALT_M    = 21;
const int SO_UNIT_ALT_FLM  = 22;
const int SO_UNIT_VS_FTM   = 23;
const int SO_UNIT_VS_MS    = 24;
const int SO_UNIT_GS_KT    = 25;
const int SO_UNIT_GS_KMH   = 26;
const int SO_UNIT_DIST_NM  = 27;
const int SO_UNIT_DIST_KM  = 28;

const int SO_OS_TWO_LINE   = 16;
const int SO_OS_SPEED      = 17;
const int SO_OS_THREE_LINE = 18;
const int SO_OS_FONT_FIELD = 19;   // "Р-р шрифта" dropdown

const int SO_CODE_ALL      = 60;   // "ВСЕ"
const int SO_CODE_BP       = 61;   // "БП"
const int SO_CODE_EXTRA    = 62;   // the unlabelled checkbox at the end of the ВВ1 row
const int SO_CODE_FILTER   = 63;   // the inline entry field next to "БП"
const int SO_VV_SLIDER     = 64;   // vertical zoom slider
const int SO_VV_SCALE      = 65;   // the scale readout beside it - a tooltip only

const int SO_ATIS_LETTER_HEADER = 37;   // the whole INDEX АТИС strip: drag to move, click to open the report

// 29 was a letter chip on the panel; the letter window lives on the radar on
// its own now and has no control of its own inside the panel.

const int SO_ATIS_BUTTON   = 30;
const int SO_ATIS_HEADER   = 31;   // drag handle of the ATIS window
const int SO_ATIS_OK       = 32;
const int SO_ATIS_SCROLLBAR   = 33;   // draggable scrollbar track of the ATIS window
const int SO_ATIS_CLOSE    = 34;   // the "x" in the ATIS window's title bar
const int SO_ATIS_LINE_UP  = 35;   // scrollbar end buttons
const int SO_ATIS_LINE_DN  = 36;

// Covers the whole radar, but only while the ruler is armed or a line is
// half-placed, and never moveable - so a drag still reaches EuroScope's own
// panning and only a plain click is ours. Nothing covers the radar otherwise.
// "Список РЦ" window.
const int SO_RC_HEADER     = 80;   // title bar - drag handle
const int SO_RC_CLOSE      = 81;   // its "x"
const int SO_RC_RESIZE     = 83;   // the grip in its bottom right corner - pulled to scale the window
const int SO_RC_FILTER     = 88;   // a field of its filter strip - sObjectId "callsign", "before" or "after"
const int SO_RC_SORT       = 82;   // a column heading - sorts by it; sObjectId is the column index
const int SO_RC_ROW        = 87;   // one row - left selects it, right turns its pane a page; sObjectId is its callsign

const int SO_FORMULAR      = 89;   // one aircraft's формуляр - drag to move, click an item; sObjectId is its callsign
const int SO_FORMULAR_AHDG = 92;   // its AHDG item - pull for a heading, click for the list; sObjectId is the callsign

const int SO_RULER_CANVAS  = 40;
const int SO_RULER_LINE    = 41;   // small hit-box around one existing line, for delete only - sObjectId is its index
const int SO_RULER_LABEL   = 42;   // the draggable bearing/distance/time readout; sObjectId is its line's index

const int SO_DROPDOWN_ITEM = 50;   // one row of the plugin's own dropdown; sObjectId is the row index

const int SO_SIGMET_AREA   = 70;   // small hit-box on a сигмет's outline; sObjectId is its index
const int SO_ZONE_AREA     = 71;   // same, for a запретная/ограничительная зона; sObjectId is its index

// ---- Function ids for the Фильтр высоты edit boxes -------------------------
const int FN_ALTFILTER_FROM = 300;
const int FN_ALTFILTER_TO   = 301;
const int FN_CODE_FILTER    = 302;   // the code block's inline entry field
const int FN_LOGIN_FIELD    = 310;   // + LoginField: a Вход window field, typed into EuroScope's own edit box when ours cannot be opened
const int FN_RC_FILTER_CALLSIGN = 305;   // "Список РЦ" filter strip: Рейс
const int FN_RC_FILTER_BEFORE   = 306;   // До (мин)
const int FN_RC_FILTER_AFTER    = 307;   // После (мин)