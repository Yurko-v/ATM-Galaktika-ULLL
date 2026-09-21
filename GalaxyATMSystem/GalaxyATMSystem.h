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
#include "Apw.h"
#include "Squawk.h"

extern HINSTANCE g_hModule;

enum class WorkMode { Offline, Sim, Ops, Sup };

enum class AltUnit  { FL, M, FLM };
enum class VsUnit   { FtMin, MS };
enum class GsUnit   { Knots, Kmh };
enum class DistUnit { NM, Km };

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

    virtual void OnFunctionCall(int FunctionId, const char* sItemString, POINT Pt, RECT Area);
    virtual void OnFlightPlanControllerAssignedDataUpdate(
        EuroScopePlugIn::CFlightPlan FlightPlan, int DataType);
    virtual void OnFlightPlanFlightStripPushed(EuroScopePlugIn::CFlightPlan FlightPlan,
        const char* sSenderController, const char* sTargetController);

    // "√" on the label: the crew speaks English. Shared with the other controllers
    // through a scratch pad broadcast (like IASsure does) and strip annotation 8.
    bool IsEnglish(const std::string& callsign) const { return m_english.count(callsign) != 0; }
    void ToggleEnglish(EuroScopePlugIn::CFlightPlan fp);

    void HandleSquawkFunction(int FunctionId, const char* sItemString, RECT Area, const char* source);

    const Config& GetConfig() const { return m_config; }

    void ReloadConfig();

    std::shared_ptr<const std::vector<Sigmet>> Sigmets() const;

    std::wstring AtisIndex() const;
    std::wstring AtisMessage() const;

    std::wstring MyUserName() const;

    bool LiveConnection() const;

    bool AccessSuspended() const;

    bool TrainingSession() const;

    enum class LoginState { Idle, Sending, Done, Failed };
    void StartLogin(const std::wstring& cid, const std::wstring& surname,
        const std::wstring& firstName, const std::wstring& patronymic);
    LoginState MyLogin(std::wstring* message = NULL) const;
    void ResetLogin();

    struct SavedLogin
    {
        std::wstring cid, surname, firstName, patronymic;
        bool Complete() const { return !cid.empty() && !surname.empty() && !firstName.empty(); }
    };
    const SavedLogin& SavedIdentity();
    void SaveIdentity(const SavedLogin& id);

    bool SessionAuthorized() const { return m_sessionAuthorized; }
    void SetSessionAuthorized(bool on) { m_sessionAuthorized = on; }

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

    std::thread      m_metarFetch;
    std::atomic<int> m_fetchedQnhHpa{ 0 };
    bool             m_gotLiveMetar = false;

    bool m_altFilterEnabled = false;
    int  m_altFilterFromFL  = 100;
    int  m_altFilterToFL    = 600;

    int  m_tagFontSize      = 10;

    std::thread m_sigmetFetch;
    mutable std::mutex m_sigmetMutex;
    std::shared_ptr<const std::vector<Sigmet>> m_sigmets;

    void StartAtisFetch();
    std::thread m_atisFetch;
    std::atomic<bool> m_atisBusy{ false };
    mutable std::mutex m_atisMutex;
    AtisReport m_atisLive;

    void StartIdentityFetch(const std::string& callsign);
    std::thread m_identityFetch;
    mutable std::mutex m_identityMutex;
    VatsimIdentity m_identity;
    bool m_accessSuspended = false;
    std::string m_identityAskedFor;

    bool m_fontChecked = false;

    std::thread m_login;
    LoginState m_loginState = LoginState::Idle;
    SavedLogin m_savedLogin;
    bool m_savedLoginRead = false;
    std::wstring m_loginMessage;

    bool m_sessionAuthorized = false;

    void StartAupFetch();
    void StartNotamFetch();
    std::thread m_aupFetch;
    mutable std::mutex m_aupMutex;
    std::shared_ptr<const std::vector<ZoneBooking>> m_aup;

    std::thread m_notamFetch;
    mutable std::mutex m_notamMutex;
    std::shared_ptr<const std::vector<ZoneBooking>> m_notams;

    SquawkClient m_squawk;

    std::map<std::string, std::string> m_squawkSetByUs;

    std::set<std::string> m_english;

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
    void SquawkMessage(const std::string& text);
    void SquawkDebugLine(const std::string& text);
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

struct FormularFn
{
    const char* itemPlugin;
    int         itemCode;
    const char* leftPlugin;
    int         leftFn;
    const char* rightPlugin;
    int         rightFn;
};

struct RulerLine
{
    bool startSnapped = false;
    bool endSnapped = false;
    std::string startCallsign;
    std::string endCallsign;
    EuroScopePlugIn::CPosition startFixed;
    EuroScopePlugIn::CPosition endFixed;

    POINT labelOffset = { 0, 0 };
    POINT labelAnchor = { 0, 0 };
    RECT  labelRect = { 0, 0, 0, 0 };
};

struct SectorListRow
{
    std::wstring cells[14];
    bool mine = false;
    bool east = true;
    int  crdState = 0;
    std::string callsign;
};

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

    int  PanelTop();
    void DrawPanel(HDC hDC);
    int  DrawHeader(HDC hDC, int y);
    int  DrawBlockTimer(HDC hDC, int y);
    int  DrawBlockUser(HDC hDC, int y);
    int  DrawBlockVectors(HDC hDC, int y);
    int  DrawBlockOs(HDC hDC, int y);
    int  DrawBlockUnits(HDC hDC, int y);
    int  DrawBlockAltFilter(HDC hDC, int y);
    int  DrawBlockCodes(HDC hDC, int y);
    int  DrawBlockAerodrome(HDC hDC, int y);
    int  DrawBlockAuth(HDC hDC, int y);
    void DrawAtisWindow(HDC hDC);
    void DrawAtisLetterWindow(HDC hDC);

    void DrawMenuBar(HDC hDC);
    int  MenuBarHeight();

    void DrawSectorListWindow(HDC hDC);
    void BuildSectorList(std::vector<SectorListRow>& out);
    void DrawSectorList(HDC hDC, RECT area, int scale, bool floating, const std::vector<SectorListRow>& all);
    int  PlanSectorList(const std::vector<SectorListRow>& all, int maxSvgH);
    int  RcScale(int availW, int availH);
    void RcObject(int type, const char* id, RECT r, bool moveable, const char* tip);

    bool CreateRcFloat(HWND owner);
    bool UndockSectorList(RECT requested);
    void RenderRcFloat();
    void RcFloatMouse(UINT msg, POINT pt);
    void RcFloatMoved();
    void TickRcFloat();
    void ApplyRcFilter(int functionId, const std::wstring& typed);
    void ScrollAtisTo(POINT pt, RECT track);
    void SetVvGainFrom(POINT pt);

    void   ApplyZoomFromSlider();
    void   SyncSliderFromZoom();
    double DisplaySpanNM();
    double DisplayWidthNM();
    static double GainToSpanNM(int gain);
    static int    SpanNMToGain(double spanNM);

    void DrawSigmets(HDC hDC);
    void RegisterSigmetObjects();
    void DrawSigmetInfo(HDC hDC);
    bool SigmetOutline(const std::vector<EuroScopePlugIn::CPosition>& ring,
        std::vector<POINT>& out);
    int  FindSigmetAt(POINT pt);
    void CloseSigmetInfoIfButtonReleased();

    void DrawZones(HDC hDC);
    void RegisterZoneObjects();
    void DrawZoneInfo(HDC hDC);
    bool ZoneOutline(const Zone& zone, std::vector<POINT>& out);
    int  FindZoneAt(POINT pt);
    int  ZoneFromObjectId(const char* sObjectId);
    void UpdateZoneActivity();

    struct FormularItem
    {
        RECT rect;
        const FormularFn* fn;
        std::string text;
    };
    struct FormularState
    {
        POINT offset = { 0, 0 };
        POINT callsignAt = { 0, 0 };
        bool  placed = false;
        bool  highlighted = false;
        bool  zone = false;
        POINT anchor = { 0, 0 };
        RECT  area = { 0, 0, 0, 0 };
        std::vector<FormularItem> items;
    };
    std::map<std::string, FormularState> m_formulars;
    bool  m_formularsVisible;
    std::string m_formularHover;
    HFONT m_formularFont;
    int   m_formularFontSize;
    HFONT GetFormularFont();
    enum class FormularKind { Ctr, App, Twr };
    enum class FormularKindSetting { Auto, Ctr, App, Twr };
    FormularKindSetting m_formularKindSetting;
    FormularKind CurrentFormularKind();
    bool HoveredCtrLabel(const char* callsign);

    void  DrawFormulars(HDC hDC, bool registerObjects);

    void  DrawTargetSymbols(HDC hDC);
    void  DrawProtectionZone(HDC hDC, EuroScopePlugIn::CPosition center, POINT tp, COLORREF color);
    struct SymbolStats
    {
        int targets = 0, offRadar = 0, filtered = 0, noSymbol = 0, drawn = 0;
    };
    SymbolStats m_symbolStats;
    void  FormularClick(const char* sCallsign, POINT pt, int button);

    bool  m_hdgDragging;
    bool  m_hdgDragMoved;
    POINT m_hdgDragStart;
    POINT m_hdgDragPt;
    std::string m_hdgDragCallsign;
    ULONGLONG m_hdgDragEndTick;
    bool  m_hdgDragCancelled;
    int   m_hdgReleaseTicks;
    int   DragHeading(const char* sCallsign, POINT cursor, POINT* from = NULL, double* distNm = NULL,
        double* drawnHdg = NULL);
    void  HeadingTurnPath(EuroScopePlugIn::CRadarTarget rt, double headingDeg, double totalNm,
        std::vector<POINT>& out);

    void DrawTargetVectors(HDC hDC);
    void DrawWakeArcs(HDC hDC);
    void DrawTrackVector(HDC hDC, EuroScopePlugIn::CRadarTarget rt, double lengthNM,
        double timeMinForLevel, int minuteTicks, COLORREF color);
    void DrawPlanVector(HDC hDC, EuroScopePlugIn::CFlightPlan fp,
        const EuroScopePlugIn::CPosition& currentPos, int minutes, COLORREF color);
    EuroScopePlugIn::CPosition CalculateDestinationPoint(
        EuroScopePlugIn::CPosition start, double bearingDeg, double distanceNM);
    COLORREF GetTagColorForFlightPlan(EuroScopePlugIn::CFlightPlan fp);

    void   DrawRulerLine(HDC hDC, RulerLine& r, int index = -1);
    void   PollRulerButton();
    void   PlaceRulerPoint(POINT pt);
    void   DrawRulerCursor(HDC hDC);
    void   UpdateRulerEnd(POINT pt);
    bool   CursorRadarPoint(POINT& out, HWND* view = NULL);
    bool   FindNearbyTarget(POINT pt, std::string& callsignOut);
    EuroScopePlugIn::CPosition ResolveRulerPoint(bool snapped, const std::string& callsign,
        EuroScopePlugIn::CPosition& fixed);
    double GetRulerSpeedKt(const RulerLine& r);
    int    FindNearestRulerIndex(POINT pt, double thresholdPx);
    void   RemoveRulerNear(POINT pt);

    RECT DrawBlockFrame(HDC hDC, int top, const std::wstring& caption, int boxHeight);
    RECT DrawBoxOnly(HDC hDC, int top, int boxHeight);
    void DrawCheckbox(HDC hDC, RECT box, bool checked, int objType, const char* objId, const char* tooltip);
    void DrawCheckRow(HDC hDC, int top, int x, const std::wstring& label, bool checked, int objType, const char* objId, const char* tooltip);
    void DrawRadioRow(HDC hDC, int top, int x, int labelRight, const std::wstring& label, bool selected, int objType, const char* objId, const char* tooltip);
    void DrawOutlinedField(HDC hDC, RECT box, const std::wstring& text, HFONT font);
    void DrawFittedField(HDC hDC, RECT box, const std::wstring& text);
    void DrawToggleChip(HDC hDC, RECT box, const std::wstring& text, bool active, int objType, const char* objId, const char* tooltip,
        COLORREF idleFill = Theme::ControlFill);
    void DrawDropdownField(HDC hDC, RECT box, const std::wstring& text, int objType, const char* objId, const char* tooltip);

    WorkMode GetWorkMode(std::wstring& labelOut, COLORREF& colorOut);
    void     GetUserInfo(std::wstring& designation, std::wstring& role, std::wstring& user);

    std::wstring GetDistressCodes();
    std::wstring GetDuplicateCodes();

    void OpenAltFilterPicker(RECT area, bool isFrom);

    enum class DropdownKind { None, VecDist, VecTime, OsFont };
    void DrawDropdownList(HDC hDC);

    int GroupLeft()    const { return m_panelArea.left + kOuterPad; }
    int GroupRight()   const { return m_panelArea.right - kOuterPad; }
    int ContentLeft()  const { return GroupLeft() + kInnerPad; }
    int ContentRight() const { return GroupRight() - kInnerPad; }
    int ContentWidth() const { return ContentRight() - ContentLeft(); }

    Theme::FontSet m_fonts;

    HFONT m_esFont;

    HFONT m_rulerFont;
    HFONT m_rulerFontSource;
    HFONT GetRulerFont(HFONT baseFont);

    RECT  m_panelArea;
    bool  m_visible;
    bool  m_collapsed;

    enum class AuthState { LoggedOut, Checking, LoggedIn };
    AuthState m_authState;
    ULONGLONG m_authStartTick;
    std::wstring m_authMessage;
    bool Authorized() { return m_authState == AuthState::LoggedIn || Plugin()->TrainingSession(); }
    void TickAuth();
    void SyncAuth();
    void StartAuthCheck();
    void AutoLogin();
    bool m_autoLoginTried;

    // Bypass: only after a failed LOGIN attempt (any error), never past a suspension.
    // Pressed before that it asks to register.
    bool m_authFailed;
    bool BypassAvailable();

    std::wstring m_noticeText;
    void ShowNotice(const std::wstring& text);
    void DrawNoticeWindow(HDC hDC);

    enum LoginField { LF_CID, LF_SURNAME, LF_FIRST_NAME, LF_PATRONYMIC, LF_COUNT };
    bool m_loginWindowOpen;
    std::wstring m_loginValues[LF_COUNT];
    std::wstring m_loginProblem;
    RECT m_loginFields[LF_COUNT];
    RECT m_loginArea;
    bool m_loginPositioned;
    ULONGLONG m_loginDrawnTick;
    void DrawLoginWindow(HDC hDC);
    void CloseLoginWindow();
    void SendLogin();

    TextEntry m_entry;
    int  m_entryField;
    int  m_entryPending;
    ULONGLONG m_entryPendingTick;
    HWND m_entryView;
    void EditLoginField(int field);
    void TickEntry();
    void CommitEntry();
    POINT m_dragOffset;
    UINT_PTR m_timerId;
    UINT_PTR m_pollTimerId;

    bool      m_timerRunning;
    ULONGLONG m_timerStartTick;
    ULONGLONG m_timerElapsedMs;

    bool m_vecDistEnabled;
    int  m_vecDistKm;
    bool m_vecTimeEnabled;
    int  m_vecTimeMin;
    bool m_vecByPlan;
    bool m_vecShowLevel;

    DropdownKind m_openDropdown;
    RECT m_vecDistFieldRect;
    RECT m_vecTimeFieldRect;

    int  m_osLines;
    bool m_osSpeed;
    RECT m_osFontFieldRect;

    bool m_codeAll;
    bool m_codeBp;
    bool m_codeExtra;
    std::wstring m_codeFilter;
    int  m_vvGain;
    bool m_vvDragging;
    RECT m_vvSliderRect;

    bool m_atisLetterOpen;
    RECT m_atisLetterArea;

    bool m_rcOpen;
    RECT m_rcArea;
    bool m_rcPositioned;
    int  m_rcScroll;
    int  m_rcScrollMine;
    int  m_rcPageRows[2];
    int  m_rcSortKey;
    bool m_rcSortAsc;

    int   m_rcScale;
    bool  m_rcResizing;
    int   m_rcResizeGrab;
    HFONT m_rcFont;
    HFONT m_rcHeadFont;
    HFONT m_rcRowFont;
    HFONT m_rcKfFont;
    int   m_rcFontScale;

    std::wstring m_rcFilterCallsign;
    int  m_rcFilterBefore;
    int  m_rcFilterAfter;
    std::map<std::string, ULONGLONG> m_rcLastInSector;

    bool  m_rcFloating;
    POINT m_rcFloatPos;
    HWND  m_rcDragView;
    FloatWindow m_rcFloat;
    ULONGLONG m_rcFloatDrawn;
    struct RcHit
    {
        int type;
        std::string id;
        RECT rect;
    };
    std::vector<RcHit> m_rcFloatHits;
    bool  m_rcDrawingFloat;
    bool  m_rcFloatResizing;
    int   m_rcFloatGrab;
    TextEntry m_rcEntry;

    bool m_atisOpen;
    int  m_atisScrollPx;
    int  m_atisScrollMax;
    int  m_atisThumbH;
    RECT m_atisArea;
    bool m_atisPositioned;

    std::shared_ptr<const std::vector<Sigmet>> m_sigmets;
    bool m_sigmetsVisible;
    int  m_sigmetInfoIndex;
    POINT m_sigmetInfoAt;
    bool m_sigmetInfoHeld;
    int  m_sigmetInfoWait;

    bool m_zonesVisible;
    int  m_zoneInfoIndex;
    POINT m_zoneInfoAt;
    bool m_zoneInfoHeld;

    bool m_areaShiftDown;
    int  m_zoneInfoWait;

    std::shared_ptr<const std::vector<ZoneBooking>> m_aup;
    std::shared_ptr<const std::vector<ZoneBooking>> m_notams;
    std::vector<char> m_zoneActive;
    std::vector<const ZoneBooking*> m_zoneBooking;

    int  m_rulerButton;
    bool m_rulerButtonDown;

    bool m_rulerPressPending;
    bool m_rulerArmed;
    bool m_rulerPlacing;
    RulerLine m_rulerPending;
    std::vector<RulerLine> m_rulers;

    static const int kPanelWidth = 218;
    static const int kCollapsedWidth = 154;
    static const int kOuterPad = 7;
    static const int kInnerPad = 6;

    static const int kCheckSize = 13;
    static const int kRadioSize = 14;
    static const int kRowH      = 16;
};

const int SO_PANEL_COLLAPSE = 2;

const int SO_TIMER_TOGGLE = 5;

const int SO_AUTH_LOGIN   = 90;
const int SO_AUTH_BYPASS  = 96;

const int SO_NOTICE_WINDOW = 84;
const int SO_NOTICE_OK     = 85;
const int SO_NOTICE_CLOSE  = 86;

const int SO_MENU_BAR     = 91;

const int SO_LOGIN_WINDOW   = 93;
const int SO_LOGIN_FIELD    = 94;
const int SO_LOGIN_SEND     = 95;
const int SO_LOGIN_HEADER   = 97;
const int SO_LOGIN_CLOSE    = 98;
const int SO_LOGIN_REGISTER = 99;

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
const int SO_OS_FONT_FIELD = 19;

const int SO_CODE_ALL      = 60;
const int SO_CODE_BP       = 61;
const int SO_CODE_EXTRA    = 62;
const int SO_CODE_FILTER   = 63;
const int SO_VV_SLIDER     = 64;
const int SO_VV_SCALE      = 65;

const int SO_ATIS_LETTER_HEADER = 37;

const int SO_ATIS_BUTTON   = 30;
const int SO_ATIS_HEADER   = 31;
const int SO_ATIS_OK       = 32;
const int SO_ATIS_SCROLLBAR   = 33;
const int SO_ATIS_CLOSE    = 34;
const int SO_ATIS_LINE_UP  = 35;
const int SO_ATIS_LINE_DN  = 36;

const int SO_RC_HEADER     = 80;
const int SO_RC_CLOSE      = 81;
const int SO_RC_RESIZE     = 83;
const int SO_RC_FILTER     = 88;
const int SO_RC_SORT       = 82;
const int SO_RC_ROW        = 87;

const int SO_FORMULAR      = 89;
const int SO_FORMULAR_AHDG = 92;

const int SO_RULER_CANVAS  = 40;
const int SO_RULER_LINE    = 41;
const int SO_RULER_LABEL   = 42;

const int SO_DROPDOWN_ITEM = 50;

const int SO_SIGMET_AREA   = 70;
const int SO_ZONE_AREA     = 71;

const int FN_ALTFILTER_FROM = 300;
const int FN_ALTFILTER_TO   = 301;
const int FN_CODE_FILTER    = 302;
const int FN_LOGIN_FIELD    = 310;
const int FN_RC_FILTER_CALLSIGN = 305;
const int FN_RC_FILTER_BEFORE   = 306;
const int FN_RC_FILTER_AFTER    = 307;