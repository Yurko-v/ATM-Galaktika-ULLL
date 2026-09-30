#pragma once

#include "Plugin/Plugin.h"

namespace Galaxy
{
    struct VectorCanvas;
}

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

    void ForgetGone(const std::set<std::string>& live);

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

public:
    void AddScreenObject(int type, const char* id, RECT area, bool moveable, const char* tip);

private:
    struct RecordedObject
    {
        int type;
        std::string id;
        RECT area;
        bool moveable;
        std::string tip;
        bool hasTip;
    };
    struct PanelSnapshot
    {
        HDC dc = NULL;
        HBITMAP bitmap = NULL;
        HGDIOBJ oldBitmap = NULL;
        int width = 0, height = 0;
        std::vector<RECT> regions;
        std::string key;
        std::vector<RecordedObject> objects;
        std::vector<RECT> hotRects;
        bool valid = false;
        void Release();
    };
    PanelSnapshot m_panelSnapshot;
    bool m_panelDirty = true;
    bool m_recordingPanel = false;
    RECT m_panelBackArea = { 0, 0, 0, 0 };
    bool m_panelBackRounded = false;
    RECT m_menuBarArea = { 0, 0, 0, 0 };
    std::string PanelKey();
    void DrawPanelAndMenu(HDC hDC);

    struct ZoneLayer
    {
        HDC dc = NULL;
        HBITMAP bitmap = NULL;
        HGDIOBJ oldBitmap = NULL;
        void* bits = NULL;
        int width = 0, height = 0;
        std::string key;
        RECT drawn = { 0, 0, 0, 0 };
        void Release();
    };
    ZoneLayer m_zoneLayer;

    static const size_t kRunWidthCacheLimit = 4096;
    std::map<std::wstring, int> m_runWidths;
    HFONT m_runWidthFont = NULL;
    std::string ZoneLayerKey();
    void RenderZoneLayer(HDC hDC);

    void DrawSectorListWindow(HDC hDC);
    void BuildSectorList(std::vector<SectorListRow>& out);
    const std::set<std::string>& KfConflicts();
    bool SeparationLost(const char* callsign);
    void DrawSectorList(HDC hDC, RECT area, int scale, bool floating, const std::vector<SectorListRow>& all);
    bool ScrollSectorList(int rows);
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
    enum class CoordDecision { None, Cancelled, Manual };
    struct CoordWatch
    {
        int lastState = 0;
        int levelFt = 0;
        std::string pointName;
        int result = 0;
        ULONGLONG resultAt = 0;
        bool wasMine = false;
        int myReply = 0;
        CoordDecision decision = CoordDecision::None;
    };
    struct FormularState
    {
        POINT offset = { 0, 0 };
        POINT callsignAt = { 0, 0 };
        bool  placed = false;
        bool  zone = false;
        POINT anchor = { 0, 0 };
        RECT  area = { 0, 0, 0, 0 };
        std::vector<FormularItem> items;
        CoordWatch exitCoord;
        CoordWatch entryCoord;
        CoordWatch exitPoint;
        CoordWatch entryPoint;
        int agreedXflFt = 0;
        std::string agreedCopx;
        int agreedEntryFt = 0;
        std::string agreedEntryPoint;
    };
    std::map<std::string, FormularState> m_formulars;
    int AgreedXfl(EuroScopePlugIn::CFlightPlan& fp);
    std::string AgreedCopx(EuroScopePlugIn::CFlightPlan& fp);
    std::string m_copxMenuCallsign;
    bool OpenCopxDecisionMenu(EuroScopePlugIn::CFlightPlan& fp, const RECT& area);
    void DecideCopx(bool manual, POINT pt, RECT area);

    POINT m_hotCursor = { 0, 0 };
    bool  m_hotValid = false;
    int   m_hotIndex = -1;
    std::vector<RECT> m_hotRects;
    bool Hot(const RECT& r);
    void AddButton(HDC hDC, int type, const char* id, RECT r, const char* tip);
    void AddHotButton(HDC hDC, int type, const char* id, RECT r, const char* tip);
    void TickHot();

    struct PopupPlacement
    {
        bool  placed = false;
        bool  dragging = false;
        bool  active = false;
        POINT topLeft = { 0, 0 };
        POINT grab = { 0, 0 };
    };
    void DragPopup(PopupPlacement& popup, const RECT& area, POINT pt, bool released);
    RECT PlacePopup(const PopupPlacement& popup, int left, int top, int width, int height);
    void TrackPopupActive(PopupPlacement& popup, const RECT& area, bool onRadar, POINT cursor);
    COLORREF PopupFrame(const PopupPlacement& popup) const;

    bool m_spdOpen = false;
    PopupPlacement m_spdPlacement;
    std::string m_spdCallsign;
    bool m_spdMach = false;
    int  m_spdTopRow = 0;
    int  m_spdSelected = 0;
    enum SpeedMode { SpeedExact, SpeedOrGreater, SpeedOrLess };
    SpeedMode m_spdMode = SpeedExact;
    bool m_spdButtonsDown = true;
    bool m_spdEntryPending = false;
    ULONGLONG m_spdPendingTick = 0;
    ULONGLONG m_spdDrawnTick = 0;
    RECT m_spdArea = { 0, 0, 0, 0 };
    RECT m_spdField = { 0, 0, 0, 0 };
    RECT m_spdList = { 0, 0, 0, 0 };
    RECT m_spdTrack = { 0, 0, 0, 0 };
    TextEntry m_spdEntry;
    HFONT m_spdFont = NULL;
    int   m_spdFontSize = 0;
    HFONT GetSpeedFont();
    void OpenSpeedWindow(const char* callsign);
    void CloseSpeedWindow();
    void DrawSpeedWindow(HDC hDC);
    void SelectSpeedTab(bool mach);
    void ScrollSpeed(int rows);
    void ApplySpeed();
    void TickSpeedWindow();

    bool m_ahdgOpen = false;
    PopupPlacement m_ahdgPlacement;
    std::string m_ahdgCallsign;
    int  m_ahdgTopRow = 0;
    int  m_ahdgSelected = 0;
    bool m_ahdgButtonsDown = true;
    bool m_ahdgEntryPending = false;
    ULONGLONG m_ahdgPendingTick = 0;
    ULONGLONG m_ahdgDrawnTick = 0;
    RECT m_ahdgArea = { 0, 0, 0, 0 };
    RECT m_ahdgField = { 0, 0, 0, 0 };
    RECT m_ahdgTrack = { 0, 0, 0, 0 };
    TextEntry m_ahdgEntry;
    void OpenHeadingWindow(const char* callsign);
    void CloseHeadingWindow();
    void DrawHeadingWindow(HDC hDC);
    void ScrollHeading(int rows);
    void ApplyHeading();
    void TickHeadingWindow();

    enum class RvsmStatus { Approved, Exempt, NotApproved, Turbulent };
    std::map<std::string, RvsmStatus> m_rvsmStatus;
    RvsmStatus RvsmStatusOf(EuroScopePlugIn::CFlightPlan& fp);
    bool m_rvsmOpen = false;
    PopupPlacement m_rvsmPlacement;
    std::string m_rvsmCallsign;
    bool m_rvsmButtonsDown = true;
    RECT m_rvsmArea = { 0, 0, 0, 0 };
    void OpenRvsmWindow(const char* callsign);
    void CloseRvsmWindow();
    void DrawRvsmWindow(HDC hDC);
    void ApplyRvsm(int status);
    void TickRvsmWindow();

    struct XfrPosition
    {
        std::string positionId;
        std::string callsign;
    };
    bool m_xfrOpen = false;
    PopupPlacement m_xfrPlacement;
    bool m_xfrPicksRoutePoint = false;
    std::string m_xfrCallsign;
    std::vector<XfrPosition> m_xfrPositions;
    int  m_xfrTopRow = 0;
    int  m_xfrSelected = -1;
    bool m_xfrButtonsDown = true;
    bool m_xfrEntryPending = false;
    ULONGLONG m_xfrPendingTick = 0;
    ULONGLONG m_xfrDrawnTick = 0;
    RECT m_xfrArea = { 0, 0, 0, 0 };
    RECT m_xfrField = { 0, 0, 0, 0 };
    RECT m_xfrTrack = { 0, 0, 0, 0 };
    TextEntry m_xfrEntry;
    HFONT m_xfrFont = NULL;
    int   m_xfrFontSize = 0;
    HFONT GetTransferFont();
    HFONT m_titleFont = NULL;
    int   m_titleFontSize = 0;
    HFONT GetTitleFont();
    void OpenTransferWindow(const char* callsign);
    void OpenCopxWindow(const char* callsign);
    void CloseTransferWindow();
    void DrawTransferWindow(HDC hDC);
    void ScrollTransfer(int rows);
    void ApplyTransfer();
    void DirectToTransferPoint(int index);
    void ReleaseTransfer();
    void TickTransferWindow();

    bool m_ftOpen = false;
    std::string m_ftCallsign;
    bool m_ftButtonsDown = true;
    bool m_ftEntryPending = false;
    ULONGLONG m_ftPendingTick = 0;
    ULONGLONG m_ftDrawnTick = 0;
    RECT m_ftArea = { 0, 0, 0, 0 };
    RECT m_ftField = { 0, 0, 0, 0 };
    TextEntry m_ftEntry;
    std::map<std::string, std::string> m_localFreeText;
    void OpenFreeTextWindow(const char* callsign);
    void CloseFreeTextWindow();
    void DrawFreeTextWindow(HDC hDC);
    void ApplyFreeText();
    void TickFreeTextWindow();

    bool m_coordOpen = false;
    std::string m_coordCallsign;
    bool m_coordButtonsDown = true;
    RECT m_coordArea = { 0, 0, 0, 0 };
    void OpenCoordWindow(const char* callsign);
    void CloseCoordWindow();
    void DrawCoordWindow(HDC hDC);
    void TickCoordWindow();
    std::wstring PendingCoordRequest(const std::string& callsign) const;
    void ReplyCoordination(const char* callsign, bool accept);
    bool SendCoordination(EuroScopePlugIn::CFlightPlan& fp, const std::string& point, int altitudeFt,
        const std::string& partnerId = "");
    bool PointDirectable(EuroScopePlugIn::CFlightPlan& fp, const std::string& point, std::string* ownerId = NULL);
    void CoordinationFailed(const std::string& callsign, const wchar_t* reason, const std::string& detail);

    bool m_cflOpen = false;
    PopupPlacement m_cflPlacement;
    bool m_cflPicksExitLevel = false;
    RECT m_cflAnchor = { 0, 0, 0, 0 };
    bool m_cflInList = false;
    std::string m_cflCallsign;
    int  m_cflTopRow = 0;
    int  m_cflHoverLevel = -1;
    bool m_cflButtonsDown = true;
    bool m_cflEntryPending = false;
    ULONGLONG m_cflPendingTick = 0;
    ULONGLONG m_cflDrawnTick = 0;
    RECT m_cflArea = { 0, 0, 0, 0 };
    RECT m_cflField = { 0, 0, 0, 0 };
    RECT m_cflTrack = { 0, 0, 0, 0 };
    std::vector<std::pair<RECT, int>> m_cflCells;
    HWND m_cflView = NULL;
    TextEntry m_cflEntry;
    void OpenCflPicker(const char* callsign, bool xfl = false);
    void CloseCflPicker();
    void DrawCflPicker(HDC hDC, const RECT& bounds);
    bool CflCursor(POINT& out);
    void ApplyCfl(int fl);
    void ApplyCflText(const std::wstring& text);
    void ScrollCfl(int rows);
    void TickCflPicker();
    HWND m_popupView = NULL;
    bool WantsWheel() const { return m_cflOpen || m_spdOpen || m_ahdgOpen || m_xfrOpen || m_atisOpen || m_visible
        || m_authState == AuthState::LoggedIn; }
    void UpdateWheelHook();
    bool WheelDropdown(POINT cursor, int rows);
public:
    bool OnMouseWheel(int delta);
    bool OnSideButton();
    bool OnMouseButton(WPARAM message, POINT screenPt);
    static void ReleaseWheelHook();
    void Shutdown();
private:
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
    std::set<std::string> m_routeShown;
    bool m_routeClearKeyDown = false;
    bool m_entryOpenBeforeKey = false;
    void DrawRoutes(HDC hDC);
    bool AnyEntryOpen() const;
    void PollRouteClearKey();

    struct MapText
    {
        EuroScopePlugIn::CPosition at;
        std::wstring text;
    };
    struct MapLine
    {
        std::vector<EuroScopePlugIn::CPosition> points;
    };
    struct MapCircle
    {
        EuroScopePlugIn::CPosition center;
        int radiusKm = 0;
    };
    enum class MapTool { None, Line, Circle };
    enum class MapMenuItem { ClearRoutes, Measure, RemoveMeasures, AddText, EditText, DeleteText,
                             AddLine, DeleteLine, ClearLines, Circle, Count };
    std::vector<MapText> m_mapTexts;
    std::vector<MapLine> m_mapLines;
    std::vector<MapCircle> m_mapCircles;
    MapTool m_mapTool = MapTool::None;
    MapLine m_mapLineDraft;
    int m_mapCircleEditing = -1;
    bool m_mapMenuOpen = false;
    POINT m_mapMenuAt = { 0, 0 };
    EuroScopePlugIn::CPosition m_mapMenuPos;
    RECT m_mapMenuArea = { 0, 0, 0, 0 };
    bool m_mapMenuButtonsDown = true;
    HWND m_mapView = NULL;
    int m_mapTextEditing = -1;
    EuroScopePlugIn::CPosition m_mapTextPos;
    TextEntry m_mapEntry;
    bool m_mapEntryPending = false;
    ULONGLONG m_mapEntryPendingTick = 0;
    ULONGLONG m_mapDrawnTick = 0;
    RECT m_mapEntryRect = { 0, 0, 0, 0 };
    std::wstring m_mapEntryText;
    bool m_rightDown = false;
    bool m_rightMoved = false;
    POINT m_rightDownScreen = { 0, 0 };
    POINT m_rightDownClient = { 0, 0 };
    HWND m_rightDownView = NULL;
    ULONGLONG m_rightDownTick = 0;
    bool m_rightClickPending = false;
    ULONGLONG m_rightUpTick = 0;
    ULONGLONG m_lastObjectClickTick = 0;
    void TickMapTools();
    void DrawMapSketches(HDC hDC);
    void DrawMapMenu(HDC hDC);
    void OpenMapMenu(POINT at, HWND view);
    void CloseMapMenu();
    bool MapMenuItemEnabled(MapMenuItem item);
    void RunMapMenuItem(MapMenuItem item);
    int  NearestMapText(POINT pt, double thresholdPx);
    bool NearestMapShape(POINT pt, double thresholdPx, int& line, int& circle);
    void OpenMapTextEntry(int index);
    void CommitMapText();
    void FinishMapTool();
    void MapCanvasClick(POINT pt, int button);
    bool WheelMapCircle(int rows);
    bool RightClickOnEmptyRadar(POINT pt);
    std::vector<POINT> MapCirclePath(const MapCircle& c);
    struct VectorLabel
    {
        POINT mark;
        double sideX, sideY;
        std::wstring text;
        COLORREF color;
    };
    void DrawTrackVector(Galaxy::VectorCanvas& canvas, std::vector<VectorLabel>& labels,
        EuroScopePlugIn::CRadarTarget rt, double lengthNM, double timeMinForLevel, int minuteTicks, COLORREF color);
    void DrawPlanVector(Galaxy::VectorCanvas& canvas, EuroScopePlugIn::CFlightPlan fp,
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
    POINT m_collapsedShift = { 0, 0 };
    bool  m_collapsedDragging = false;
    POINT m_collapsedGrab = { 0, 0 };

    enum class AuthState { LoggedOut, LoggedIn };
    AuthState m_authState;
    std::wstring m_authMessage;
    bool Authorized() { return m_authState == AuthState::LoggedIn || Plugin()->TrainingSession(); }
    void SyncAuth();
    void GrantAccess();
    void AutoLogin();
    bool m_autoLoginTried;


    std::wstring m_noticeText;
    void ShowNotice(const std::wstring& text);
    void DrawNoticeWindow(HDC hDC);

    enum LoginField { LF_CID, LF_SURNAME, LF_COUNT };
    bool m_loginWindowOpen;
    std::wstring m_loginValues[LF_COUNT];
    std::wstring m_loginProblem;
    RECT m_loginFields[LF_COUNT];
    RECT m_loginArea;
    bool m_loginPositioned;
    bool m_loginCollapsed = false;
    RECT m_loginCollapsedArea = { 0, 0, 0, 0 };
    bool m_loginCollapsedPlaced = false;
    ULONGLONG m_loginDrawnTick;
    void DrawLoginWindow(HDC hDC);
    void DrawCollapsedLogin(HDC hDC);
    void ToggleLoginCollapsed();
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
    enum class LinkSeen { Unknown, Offline, Online };
    LinkSeen  m_linkSeen = LinkSeen::Unknown;
    void StartTimerOnConnect();

    bool m_vecDistEnabled;
    int  m_vecDistKm;
    bool m_vecTimeEnabled;
    int  m_vecTimeMin;
    bool m_vecByPlan;
    bool m_vecShowLevel;

    DropdownKind m_openDropdown;
    RECT m_dropdownListRect;
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
    bool m_atisLetterPositioned = false;
    bool m_atisLetterDragged = false;
    std::string m_atisIcao;

    bool m_rcOpen;
    RECT m_rcArea;
    bool m_rcPositioned;
    int  m_rcScroll;
    int  m_rcScrollMine;
    RECT m_rcPane[2];
    int  m_rcPaneRows[2];
    int  m_rcSortKey;
    bool m_rcSortAsc;

    int   m_rcScale;
    bool  m_rcResizing;
    int   m_rcResizeGrab;
    HFONT m_rcFont;
    HFONT m_rcHeadFont;
    HFONT m_rcRowFont;
    HFONT m_rcCellFont;
    int   m_rcFontScale;

    std::wstring m_rcFilterCallsign;
    int  m_rcFilterBefore;
    int  m_rcFilterAfter;
    std::map<std::string, ULONGLONG> m_rcLastInSector;
    std::set<std::string> m_kfConflicts;
    std::set<std::string> m_ssaViolations;
    ULONGLONG m_kfTick = 0;

    enum class PerfSection
    {
        BeforeTags, AfterTags, AfterLists,
        Zones, Sigmets, WakeArcs, TargetVectors, TargetSymbols, Formulars, CoordWindow,
        Panel, Windows, Count
    };
    struct PerfTotals
    {
        LONGLONG sum = 0;
        LONGLONG worst = 0;
        int calls = 0;
    };
    bool m_perfOn = false;
    PerfTotals m_perf[(int)PerfSection::Count];
    ULONGLONG m_perfSince = 0;
    LONGLONG m_perfLastFrame = 0;
    PerfTotals m_perfGap;
    void PerfReset();
    void PerfReport();
    template <class Draw> void Timed(PerfSection section, Draw draw)
    {
        if (!m_perfOn)
        {
            draw();
            return;
        }
        LARGE_INTEGER from, to;
        QueryPerformanceCounter(&from);
        draw();
        QueryPerformanceCounter(&to);
        PerfTotals& t = m_perf[(int)section];
        const LONGLONG spent = to.QuadPart - from.QuadPart;
        t.sum += spent;
        t.worst = max(t.worst, spent);
        t.calls++;
    }
    void RefreshPhase(HDC hDC, int Phase);

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
    bool  m_rcClickFromFloat = false;
    std::set<std::string> m_rcPicked;
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
    time_t m_zoneActivityAt = 0;
    std::vector<const ZoneBooking*> m_zoneBooking;

    int  m_rulerButton;
    bool m_rulerButtonDown;
    bool m_rulerHotkeyDown = false;

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

const int SO_CFL_WINDOW = 100;
const int SO_CFL_LEVEL  = 101;
const int SO_CFL_UP     = 102;
const int SO_CFL_DOWN   = 103;
const int SO_CFL_TRACK  = 104;
const int SO_CFL_FIELD  = 105;
const int SO_CFL_OK     = 106;

const int SO_SPD_WINDOW = 110;
const int SO_SPD_CLOSE  = 111;
const int SO_SPD_ROW    = 112;
const int SO_SPD_UP     = 113;
const int SO_SPD_DOWN   = 114;
const int SO_SPD_TRACK  = 115;
const int SO_SPD_FIELD  = 116;
const int SO_SPD_TAB    = 117;
const int SO_SPD_MODE   = 118;
const int SO_SPD_YES    = 119;
const int SO_SPD_CANCEL = 120;

const int SO_HDG_WINDOW = 150;
const int SO_HDG_CLOSE  = 151;
const int SO_HDG_ROW    = 152;
const int SO_HDG_UP     = 153;
const int SO_HDG_DOWN   = 154;
const int SO_HDG_TRACK  = 155;
const int SO_HDG_FIELD  = 156;
const int SO_HDG_YES    = 157;
const int SO_HDG_CANCEL = 158;

const int SO_RVSM_WINDOW = 160;
const int SO_RVSM_CLOSE  = 161;
const int SO_RVSM_ROW    = 162;

const int SO_XFR_WINDOW  = 121;
const int SO_XFR_CLOSE   = 122;
const int SO_XFR_ROW     = 123;
const int SO_XFR_UP      = 124;
const int SO_XFR_DOWN    = 125;
const int SO_XFR_TRACK   = 126;
const int SO_XFR_FIELD   = 127;
const int SO_XFR_HANDOFF = 128;
const int SO_XFR_RELEASE = 129;
const int SO_FT_WINDOW   = 130;
const int SO_FT_CLOSE    = 131;
const int SO_FT_FIELD    = 132;
const int SO_FT_OK       = 133;
const int SO_FT_CANCEL   = 134;
const int SO_COORD_ACCEPT = 135;
const int SO_COORD_REJECT = 136;
const int SO_RC_SQUAWK     = 137;
const int SO_RC_XFL        = 138;
const int SO_COORD_WINDOW  = 139;
const int SO_COORD_CLOSE   = 140;

const int SO_NOTICE_WINDOW = 84;
const int SO_NOTICE_OK     = 85;
const int SO_NOTICE_CLOSE  = 86;

const int SO_MENU_BAR     = 91;

const int SO_LOGIN_WINDOW   = 93;
const int SO_LOGIN_FIELD    = 94;
const int SO_LOGIN_SEND     = 95;
const int SO_LOGIN_COLLAPSE = 96;
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
const int SO_ATIS_LETTER_ROW    = 38;

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
const int SO_MAP_MENU      = 170;
const int SO_MAP_MENU_ITEM = 171;
const int SO_MAP_CANVAS    = 172;
const int SO_PANEL_DRAG    = 173;
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
const int FN_COPX_CANCEL        = 320;
const int FN_COPX_MANCOORD      = 321;
