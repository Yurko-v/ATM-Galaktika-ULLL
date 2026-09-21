#include "pch.h"
#include "GalaxyATMSystem.h"

#include <shellapi.h>
#pragma comment(lib, "shell32.lib")

#include <string>
#include <map>
#include <set>
#include <cstdio>
#include <cwchar>
#include <cmath>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <algorithm>

#include "Net.h"
#include "Log.h"
#include "Lang.h"
#include "Geometry.h"

#pragma comment(lib, "msimg32.lib")

#include <objidl.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace EuroScopePlugIn;

namespace
{
    const int kDistanceSteps[] = { 5, 10, 15, 20, 25, 30, 35, 40, 45, 50 };
    const int kDistanceStepsCount = sizeof(kDistanceSteps) / sizeof(kDistanceSteps[0]);
    const int kTimeSteps[] = { 1, 2, 3, 4, 5, 10, 15 };
    const int kTimeStepsCount = sizeof(kTimeSteps) / sizeof(kTimeSteps[0]);
    const int kFontSizeSteps[] = { 8, 9, 10, 11, 12, 13, 14, 16 };
    const int kFontSizeStepsCount = sizeof(kFontSizeSteps) / sizeof(kFontSizeSteps[0]);

    std::wstring Widen(const char* s)
    {
        if (s == NULL || *s == '\0')
            return std::wstring();
        int n = MultiByteToWideChar(CP_ACP, 0, s, -1, NULL, 0);
        std::wstring w(n ? n - 1 : 0, L'\0');
        if (n > 1)
            MultiByteToWideChar(CP_ACP, 0, s, -1, &w[0], n - 1);
        return w;
    }

    std::string Narrow(const std::wstring& w)
    {
        if (w.empty())
            return std::string();
        int n = WideCharToMultiByte(CP_ACP, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
        std::string s(n, '\0');
        WideCharToMultiByte(CP_ACP, 0, w.c_str(), (int)w.size(), &s[0], n, NULL, NULL);
        return s;
    }

    std::wstring Upper(std::wstring v)
    {
        std::transform(v.begin(), v.end(), v.begin(), ::towupper);
        return v;
    }

    std::map<UINT_PTR, CGalaxyATMSystemRadarScreen*> g_timers;

    std::map<UINT_PTR, CGalaxyATMSystemRadarScreen*> g_pollTimers;

    bool IsDistressSquawk(const char* squawk)
    {
        return strcmp(squawk, "7500") == 0
            || strcmp(squawk, "7600") == 0
            || strcmp(squawk, "7700") == 0;
    }

    bool IsConspicuitySquawk(const char* squawk)
    {
        return strcmp(squawk, "0000") == 0
            || strcmp(squawk, "1200") == 0
            || strcmp(squawk, "2000") == 0
            || strcmp(squawk, "7000") == 0;
    }

    std::string FormatLevelFeet(int altFt)
    {
        char buf[16];
        sprintf_s(buf, "F%03d", altFt / 100);
        return buf;
    }

    std::string FormatLevelMetres(int altFt)
    {
        char buf[16];
        sprintf_s(buf, "C%04d", (int)lround(altFt * 0.3048 / 10.0));
        return buf;
    }

    std::string FormatAltitudeUnit(int altFt, AltUnit unit)
    {
        switch (unit)
        {
        case AltUnit::M:
            return FormatLevelMetres(altFt);
        case AltUnit::FLM:
            return FormatLevelFeet(altFt) + " " + FormatLevelMetres(altFt);
        case AltUnit::FL:
        default:
            return FormatLevelFeet(altFt);
        }
    }

    std::string FormatVerticalSpeedUnit(int fpm, VsUnit unit)
    {
        if (fpm > -100 && fpm < 100)
            return std::string();

        char buf[16];
        if (unit == VsUnit::MS)
            sprintf_s(buf, "ms%+.1f", fpm * 0.00508);
        else
            sprintf_s(buf, "fm%+d", fpm);
        return buf;
    }

    std::string FormatGroundSpeedUnit(int kt, GsUnit unit)
    {
        char buf[16];
        if (unit == GsUnit::Kmh)
            sprintf_s(buf, "Km%03d", (int)lround(kt * 1.852));
        else
            sprintf_s(buf, "Kt%03d", kt);
        return buf;
    }

    std::string FormatDistanceUnit(double nm, DistUnit unit)
    {
        char buf[16];
        if (unit == DistUnit::Km)
            sprintf_s(buf, "%.0f", nm * 1.852);
        else
            sprintf_s(buf, "%.0f", nm);
        return buf;
    }

    int ParseQnhHpa(const std::string& metar)
    {
        for (size_t i = 0; i + 4 < metar.size(); i++)
        {
            char c = metar[i];
            if (c != 'Q' && c != 'A')
                continue;
            if (!isdigit((unsigned char)metar[i + 1]) || !isdigit((unsigned char)metar[i + 2]) ||
                !isdigit((unsigned char)metar[i + 3]) || !isdigit((unsigned char)metar[i + 4]))
                continue;
            bool boundaryOk = (i == 0) || !isalnum((unsigned char)metar[i - 1]);
            bool endOk = (i + 5 >= metar.size()) || !isdigit((unsigned char)metar[i + 5]);
            if (!boundaryOk || !endOk)
                continue;

            int val = (metar[i + 1] - '0') * 1000 + (metar[i + 2] - '0') * 100 +
                (metar[i + 3] - '0') * 10 + (metar[i + 4] - '0');
            return (c == 'Q') ? val : (int)lround((val / 100.0) * 33.8639);
        }
        return -1;
    }

    void FillAlpha(HDC hDC, const RECT& r, COLORREF color, BYTE alpha)
    {
        HDC mem = CreateCompatibleDC(hDC);
        if (mem == NULL)
            return;
        HBITMAP bmp = CreateCompatibleBitmap(hDC, 1, 1);
        if (bmp == NULL)
        {
            DeleteDC(mem);
            return;
        }
        HGDIOBJ oldBmp = SelectObject(mem, bmp);

        RECT one = { 0, 0, 1, 1 };
        HBRUSH br = CreateSolidBrush(color);
        FillRect(mem, &one, br);
        DeleteObject(br);

        BLENDFUNCTION bf = { AC_SRC_OVER, 0, alpha, 0 };
        AlphaBlend(hDC, r.left, r.top, r.right - r.left, r.bottom - r.top,
            mem, 0, 0, 1, 1, bf);

        SelectObject(mem, oldBmp);
        DeleteObject(bmp);
        DeleteDC(mem);
    }

    bool ShiftHeldInEuroScope()
    {
        HWND fg = GetForegroundWindow();
        DWORD pid = 0;
        if (fg != NULL)
            GetWindowThreadProcessId(fg, &pid);
        return pid == GetCurrentProcessId() && (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    }

    struct VectorCanvas
    {
        Gdiplus::Graphics g;
        Gdiplus::Pen pen;

        VectorCanvas(HDC hDC, COLORREF color, float width = Theme::VectorWidth)
            : g(hDC),
              pen(Gdiplus::Color(GetRValue(color), GetGValue(color), GetBValue(color)), width)
        {
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            pen.SetLineJoin(Gdiplus::LineJoinRound);
        }

        void Line(double x0, double y0, double x1, double y1)
        {
            g.DrawLine(&pen, (Gdiplus::REAL)x0, (Gdiplus::REAL)y0, (Gdiplus::REAL)x1, (Gdiplus::REAL)y1);
        }

        void Circle(double cx, double cy, double r)
        {
            g.DrawEllipse(&pen, (Gdiplus::REAL)(cx - r), (Gdiplus::REAL)(cy - r),
                (Gdiplus::REAL)(r * 2), (Gdiplus::REAL)(r * 2));
        }

        void Polyline(const std::vector<POINT>& pts)
        {
            if (pts.size() < 2)
                return;
            std::vector<Gdiplus::PointF> shape;
            shape.reserve(pts.size());
            for (const POINT& p : pts)
                shape.push_back(Gdiplus::PointF((Gdiplus::REAL)p.x, (Gdiplus::REAL)p.y));
            g.DrawLines(&pen, shape.data(), (INT)shape.size());
        }
    };

    struct AreaCanvas
    {
        Gdiplus::Graphics g;
        Gdiplus::Pen pen;
        RECT clip;

        AreaCanvas(HDC hDC, const RECT& area, COLORREF color, float width)
            : g(hDC),
              pen(Gdiplus::Color(GetRValue(color), GetGValue(color), GetBValue(color)),
                  width),
              clip(area)
        {
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
            pen.SetLineJoin(Gdiplus::LineJoinRound);
            g.SetClip(Gdiplus::Rect(area.left, area.top,
                area.right - area.left, area.bottom - area.top),
                Gdiplus::CombineModeIntersect);
        }

        void SetColor(COLORREF color)
        {
            pen.SetColor(Gdiplus::Color(GetRValue(color), GetGValue(color), GetBValue(color)));
        }

        void Ring(const std::vector<POINT>& pts, bool closed)
        {
            if (pts.size() < 2)
                return;

            Gdiplus::GraphicsPath path;
            const size_t segments = closed ? pts.size() : pts.size() - 1;
            POINT last = { 0, 0 };
            bool running = false;

            for (size_t i = 0; i < segments; i++)
            {
                POINT a = pts[i], b = pts[(i + 1) % pts.size()];
                if (!Geom::ClipSegment(clip, a, b))
                {
                    running = false;
                    continue;
                }

                if (!running || last.x != a.x || last.y != a.y)
                {
                    path.StartFigure();
                    running = true;
                }
                path.AddLine((Gdiplus::REAL)a.x, (Gdiplus::REAL)a.y,
                    (Gdiplus::REAL)b.x, (Gdiplus::REAL)b.y);
                last = b;
            }

            g.DrawPath(&pen, &path);
        }

        void Wash(const std::vector<POINT>& pts, COLORREF color, BYTE alpha)
        {
            if (alpha == 0)
                return;

            RECT box = clip;
            InflateRect(&box, 8, 8);

            std::vector<POINT> visible;
            if (!Geom::ClipPolygon(box, pts, visible))
                return;

            std::vector<Gdiplus::PointF> shape;
            shape.reserve(visible.size());
            for (const POINT& p : visible)
                shape.push_back(Gdiplus::PointF((Gdiplus::REAL)p.x, (Gdiplus::REAL)p.y));

            Gdiplus::SolidBrush brush(Gdiplus::Color(alpha,
                GetRValue(color), GetGValue(color), GetBValue(color)));
            g.FillPolygon(&brush, shape.data(), (int)shape.size(), Gdiplus::FillModeWinding);
        }
    };

    bool ClipLeaderToText(POINT from, POINT aim, const std::vector<RECT>& rows, double gapPx,
        POINT& start, POINT& end)
    {
        const double dx = (double)aim.x - from.x, dy = (double)aim.y - from.y;

        double tHit = 2.0;
        for (const RECT& row : rows)
        {
            if (row.right <= row.left || row.bottom <= row.top)
                continue;

            double tEnter = 0.0, tExit = 1.0;
            bool hit = true;
            const double clipP[4] = { -dx, dx, -dy, dy };
            const double clipQ[4] = { (double)from.x - row.left, (double)row.right - from.x,
                                      (double)from.y - row.top,  (double)row.bottom - from.y };
            for (int i = 0; i < 4 && hit; i++)
            {
                if (fabs(clipP[i]) < 1e-9)
                {
                    if (clipQ[i] < 0.0)
                        hit = false;
                    continue;
                }
                const double t = clipQ[i] / clipP[i];
                if (clipP[i] < 0.0) { if (t > tEnter) tEnter = t; }
                else                { if (t < tExit)  tExit = t; }
            }
            if (hit && tEnter <= tExit && tEnter < tHit)
                tHit = tEnter;
        }

        const double len = sqrt(dx * dx + dy * dy);
        const double tGap = (len > 1e-9) ? gapPx / len : 1.0;
        if (tHit > 1.0 || (tHit - tGap) * len <= 2.0)
            return false;

        start.x = (LONG)lround(from.x + dx * tGap);
        start.y = (LONG)lround(from.y + dy * tGap);
        end.x = (LONG)lround(from.x + dx * tHit);
        end.y = (LONG)lround(from.y + dy * tHit);
        return true;
    }

    double GeoBearingDeg(const EuroScopePlugIn::CPosition& from,
        const EuroScopePlugIn::CPosition& to)
    {
        const double lat1 = from.m_Latitude * M_PI / 180.0;
        const double lat2 = to.m_Latitude * M_PI / 180.0;
        const double dLon = (to.m_Longitude - from.m_Longitude) * M_PI / 180.0;
        const double y = sin(dLon) * cos(lat2);
        const double x = cos(lat1) * sin(lat2) - sin(lat1) * cos(lat2) * cos(dLon);
        const double deg = atan2(y, x) * 180.0 / M_PI;
        return (deg < 0.0) ? deg + 360.0 : deg;
    }

    const double kRamThresholdNm = 5.0;
    const int kRamMinGsKt = 50;

    double CrossTrackNm(const EuroScopePlugIn::CPosition& a,
        const EuroScopePlugIn::CPosition& b, const EuroScopePlugIn::CPosition& p)
    {
        const double R = 3440.065;
        EuroScopePlugIn::CPosition from = a, to = b, at = p;

        const double legNm = from.DistanceTo(to);
        const double d13 = from.DistanceTo(at);
        if (legNm < 0.1)
            return d13;

        const double turn = fmod(GeoBearingDeg(from, at) - GeoBearingDeg(from, to) + 540.0,
            360.0) - 180.0;
        if (fabs(turn) > 90.0)
            return d13;

        const double across = fabs(asin(sin(d13 / R) * sin(turn * M_PI / 180.0)) * R);
        double ratio = cos(d13 / R) / cos(across / R);
        ratio = max(-1.0, min(1.0, ratio));
        if (acos(ratio) * R > legNm)
            return to.DistanceTo(at);

        return across;
    }

    bool RouteAdherenceAlert(EuroScopePlugIn::CFlightPlan fp, EuroScopePlugIn::CRadarTarget rt)
    {
        if (!fp.IsValid() || !rt.IsValid())
            return false;
        EuroScopePlugIn::CRadarTargetPositionData pos = rt.GetPosition();
        if (!pos.IsValid() || rt.GetGS() < kRamMinGsKt)
            return false;

        EuroScopePlugIn::CFlightPlanControllerAssignedData cad = fp.GetControllerAssignedData();
        if (cad.GetAssignedHeading() > 0)
            return false;
        const int cfl = cad.GetClearedAltitude();
        if (cfl == 1 || cfl == 2)
            return false;

        EuroScopePlugIn::CFlightPlanExtractedRoute route = fp.GetExtractedRoute();
        const int points = route.GetPointsNumber();
        const int leg = route.GetPointsCalculatedIndex();
        if (points < 2 || leg < 0 || leg + 1 >= points)
            return false;

        return CrossTrackNm(route.GetPointPosition(leg), route.GetPointPosition(leg + 1),
            pos.GetPosition()) > kRamThresholdNm;
    }

    void DrawGappedPolyline(HDC hDC, const std::vector<POINT>& pts, COLORREF color, double gapPx)
    {
        if (pts.size() < 2)
            return;
        VectorCanvas canvas(hDC, color);
        for (size_t i = 0; i + 1 < pts.size(); i++)
        {
            double ax = pts[i].x, ay = pts[i].y;
            double dx = pts[i + 1].x - ax, dy = pts[i + 1].y - ay;
            double len = sqrt(dx * dx + dy * dy);
            if (len < 1e-6)
                continue;

            double cut = min(gapPx / 2.0, len * 0.45) / len;
            double t0 = (i > 0) ? cut : 0.0;
            double t1 = (i + 2 < pts.size()) ? 1.0 - cut : 1.0;
            canvas.Line(ax + dx * t0, ay + dy * t0, ax + dx * t1, ay + dy * t1);
        }
    }
}

namespace L
{
    const int CAPTION_H = 17;
    const int CAP_GAP   = 5;
    const int BLOCK_GAP = 6;
    const int BLOCK_GAP_WIDE = 8;
    const int NOCAP_GAP = 16;

    const int CLOCK_H   = 24;
    const int DATE_H    = 18;

    const int HDR_SPACE  = 16;
    const int CLOCK_LEAD = (CLOCK_H - 20) / 2;
    const int DATE_LEAD  = (DATE_H - 14) / 2;
    const int CAP_LEAD   = (CAPTION_H - 14) / 2;

    const int HDR_TOP   = HDR_SPACE - CLOCK_LEAD;
    const int CLOCK_GAP = HDR_SPACE - CLOCK_LEAD - DATE_LEAD;
    const int HDR_GAP   = HDR_SPACE - DATE_LEAD - CAP_LEAD;
    const int HEADER_H  = HDR_TOP + CLOCK_H + CLOCK_GAP + DATE_H;

    const int T_PAD = 4, T_ROW = 20;
    const int TIMER_BOX_H = T_PAD + T_ROW + T_PAD;

    const int U_TOP = 4, U_ROW = 20, U_GAP = 5, U_BOT = 4;
    const int USER_BOX_H = U_TOP + U_ROW + U_GAP + U_ROW + U_BOT;

    const int V_TOP = 5, V_ROW = 20, V_GAP1 = 8, V_CHK = 16, V_GAP2 = 6, V_BOT = 6;
    const int VECTORS_BOX_H = V_TOP + V_ROW + V_GAP1 + V_CHK + V_GAP2 + V_CHK + V_BOT;

    const int O_TOP = 4, O_LABEL = V_ROW, O_GAP = 4, O_LIST_H = 130, O_BOT = 4;
    const int OS_ROW = 16, OS_PITCH = 21, OS_ROW0 = 4;
    const int OS_BOX_H = O_TOP + O_LABEL + O_GAP + O_LIST_H + O_BOT;

    const int E_TOP = 4, E_ROW = 16, E_PITCH = 20, E_BOT = 6;
    const int UNITS_BOX_H = E_TOP + 3 * E_PITCH + E_ROW + E_BOT;

    const int F_TOP = 4, F_ROW = 17, F_GAP1 = 5, F_GAP2 = 9, F_BOT = 4;
    const int ALTFILTER_BOX_H = F_TOP + F_ROW + F_GAP1 + F_ROW + F_GAP2 + F_ROW + F_BOT;

    const int C_ROW_H    = 15;
    const int C_CAP_H    = 15;
    const int C_FIELD_H  = 16;
    const int C_VV_TOP     = 2,  C_VV_H    = 17;
    const int C_ALL_TOP    = 2;
    const int C_BP_TOP     = 19;
    const int C_FLT_TOP    = 19, C_FLT_H   = 15;
    const int C_EXTRA_TOP  = 20, C_EXTRA_H = 13;
    const int C_SLIDER_TOP = 32, C_SLIDER_BOT = 105;
    const int C_DISTRESS_CAP = 36, C_DISTRESS_FIELD = 53;
    const int C_DUP_CAP      = 74, C_DUP_FIELD      = 91;
    const int CODES_BOX_H = 113;

    const int A_TOP = 3, A_ROW = 21, A_GAP = 5, A_BOT = 3;
    const int AERODROME_BOX_H = A_TOP + A_ROW + A_GAP + A_ROW + A_BOT;

    const int AU_TOP = 4, AU_ROW = 20, AU_GAP = 5, AU_GAP2 = 8, AU_STATUS = 20, AU_BOT = 6;
    const int AUTH_BOX_H      = AU_TOP + AU_ROW + AU_GAP + AU_ROW + AU_GAP2 + AU_STATUS + AU_BOT;
    const int AUTH_BOX_H_IDLE = AU_TOP + AU_ROW + AU_GAP + AU_ROW + AU_BOT;

    const int MENU_BAR_H = 28;

    const int PANEL_BOT_PAD = 8;
    const int COLLAPSED_BOT_PAD = 6;

    inline int Block(int boxH) { return CAPTION_H + CAP_GAP + boxH; }
}

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
    delete g_plugin;
    g_plugin = NULL;

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
    "0.2.0",
    "Yuriy Velbovets",
    "ULLL controller control panel")
{
    Log::Info("plugin", "Galaxy ATM System 0.2.0 loaded");
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

    StartMetarFetch();
    StartSigmetFetch();
    StartAtisFetch();
    StartAupFetch();
    StartNotamFetch();
    ConfigureSquawk();
}

CGalaxyATMSystemPlugin::~CGalaxyATMSystemPlugin()
{
    m_squawk.Stop();

    if (m_metarFetch.joinable())
        m_metarFetch.join();
    if (m_sigmetFetch.joinable())
        m_sigmetFetch.join();
    if (m_atisFetch.joinable())
        m_atisFetch.join();
    if (m_identityFetch.joinable())
        m_identityFetch.join();
    if (m_login.joinable())
        m_login.join();
    if (m_aupFetch.joinable())
        m_aupFetch.join();
    if (m_notamFetch.joinable())
        m_notamFetch.join();

    Theme::ReleaseEuroScopeFace();
}

std::string CGalaxyATMSystemPlugin::AirportIcao() const
{
    std::string icao;
    for (wchar_t c : m_config.Airport())
    {
        if (icao.size() >= 4)
            break;
        if (iswalnum(c))
            icao += (char)towupper(c);
    }
    return (icao.size() == 4) ? icao : std::string();
}

std::wstring CGalaxyATMSystemPlugin::AtisIndex() const
{
    std::lock_guard<std::mutex> lock(m_atisMutex);
    return m_atisLive.letter.empty() ? m_config.AtisIndex() : m_atisLive.letter;
}

std::wstring CGalaxyATMSystemPlugin::AtisMessage() const
{
    std::lock_guard<std::mutex> lock(m_atisMutex);
    return m_atisLive.text.empty() ? m_config.AtisMessage() : m_atisLive.text;
}

std::shared_ptr<const std::vector<ZoneBooking>> CGalaxyATMSystemPlugin::AupBookings() const
{
    std::lock_guard<std::mutex> lock(m_aupMutex);
    return m_aup;
}

void CGalaxyATMSystemPlugin::ReloadConfig()
{
    m_config.Load(g_hModule);
    LogConfigLoad(m_config, ".reload");

    m_qnhMmHg = m_config.QnhMmHg();
    m_qnhHpa = m_config.QnhHpa();
    m_gotLiveMetar = false;

    StartMetarFetch();
    StartSigmetFetch();
    StartAtisFetch();
    StartAupFetch();
    StartNotamFetch();
    ConfigureSquawk();
}

void CGalaxyATMSystemPlugin::StartNotamFetch()
{
    std::string source = m_config.NotamSource();
    if (source.empty())
        return;

    if (m_notamFetch.joinable())
        m_notamFetch.join();

    m_notamFetch = std::thread([this, source]()
        {
            std::vector<ZoneBooking> fetched;
            if (!FetchNotams(source, fetched))
                return;

            auto list = std::make_shared<const std::vector<ZoneBooking>>(std::move(fetched));
            std::lock_guard<std::mutex> lock(m_notamMutex);
            m_notams = list;
        });
}

std::shared_ptr<const std::vector<ZoneBooking>> CGalaxyATMSystemPlugin::Notams() const
{
    std::lock_guard<std::mutex> lock(m_notamMutex);
    return m_notams;
}

void CGalaxyATMSystemPlugin::StartAupFetch()
{
    std::string url = m_config.AupUrl();
    if (url.empty())
        return;

    if (m_aupFetch.joinable())
        m_aupFetch.join();

    m_aupFetch = std::thread([this, url]()
        {
            std::vector<ZoneBooking> fetched;
            if (!FetchAup(url, fetched))
                return;

            auto list = std::make_shared<const std::vector<ZoneBooking>>(std::move(fetched));
            std::lock_guard<std::mutex> lock(m_aupMutex);
            m_aup = list;
        });
}

void CGalaxyATMSystemPlugin::StartAtisFetch()
{
    if (!m_config.AtisLive())
        return;

    std::string icao = AirportIcao();
    if (icao.empty())
        return;

    if (m_atisBusy)
        return;
    if (m_atisFetch.joinable())
        m_atisFetch.join();

    m_atisBusy = true;
    m_atisFetch = std::thread([this, icao]()
        {
            AtisReport report;
            if (FetchVatsimAtis(icao, report))
            {
                std::lock_guard<std::mutex> lock(m_atisMutex);
                m_atisLive = report;
            }
            m_atisBusy = false;
        });
}

void CGalaxyATMSystemPlugin::StartIdentityFetch(const std::string& callsign)
{
    if (m_identityFetch.joinable())
        m_identityFetch.join();

    VatsimIdentity known;
    {
        std::lock_guard<std::mutex> lock(m_identityMutex);
        if (_stricmp(m_identity.callsign.c_str(), callsign.c_str()) == 0)
            known = m_identity;
    }
    std::string url = m_config.SquawkServerUrl();
    std::string key = m_config.SquawkApiKey();

    m_identityAskedFor = callsign;
    m_identityFetch = std::thread([this, callsign, known, url, key]()
        {
            VatsimIdentity found = known;
            if (found.Empty() && !FetchVatsimIdentity(callsign, found))
                return;

            std::wstring name = found.registeredName;
            bool table = found.registeredTable;
            const bool answered = url.empty() || FetchRegisteredName(url, key, callsign, name, table);

            std::lock_guard<std::mutex> lock(m_identityMutex);
            if (answered)
            {
                if (table && name.empty() && !m_identity.registeredName.empty() && m_identity.cid == found.cid)
                {
                    if (!m_accessSuspended)
                        Log::Warn("auth", "user base: the name for CID " + Log::Utf8(found.cid) + " ("
                            + Log::Utf8(m_identity.registeredName) + ") has been removed - access suspended");
                    m_accessSuspended = true;
                }
                else if (!name.empty())
                {
                    m_accessSuspended = false;
                }
                found.registeredName = name;
                found.registeredTable = table;
            }
            else if (_stricmp(m_identity.callsign.c_str(), callsign.c_str()) == 0)
            {
                found.registeredName = m_identity.registeredName;
                found.registeredTable = m_identity.registeredTable;
            }
            m_identity = found;
        });
}

std::wstring CGalaxyATMSystemPlugin::MyUserName() const
{
    VatsimIdentity id;
    {
        std::lock_guard<std::mutex> lock(m_identityMutex);
        id = m_identity;
    }

    if (!id.Empty() && _stricmp(id.callsign.c_str(), MyPosition().c_str()) == 0)
    {
        for (const std::wstring& chosen : { m_config.UserName(id.cid), id.registeredName })
        {
            if (chosen.empty())
                continue;
            std::wstring shortened = RussianShortName(chosen);
            return shortened.empty() ? chosen : shortened;
        }
        std::wstring name = RussianShortName(id.name);
        if (!name.empty())
            return name;
    }
    else
    {
        id = VatsimIdentity();
    }

    CController me = ControllerMyself();
    std::wstring name = me.IsValid() ? RussianShortName(Widen(me.GetFullName())) : L"";
    return !name.empty() ? name : id.cid;
}

bool CGalaxyATMSystemPlugin::LiveConnection() const
{
    const int ct = GetConnectionType();
    return (ct == CONNECTION_TYPE_DIRECT || ct == CONNECTION_TYPE_VIA_PROXY) && !MyPosition().empty();
}

bool CGalaxyATMSystemPlugin::AccessSuspended() const
{
    std::lock_guard<std::mutex> lock(m_identityMutex);
    return m_accessSuspended;
}

bool CGalaxyATMSystemPlugin::TrainingSession() const
{
    const int ct = GetConnectionType();
    return ct == CONNECTION_TYPE_SWEATBOX
        || ct == CONNECTION_TYPE_SIMULATOR_SERVER
        || ct == CONNECTION_TYPE_SIMULATOR_CLIENT
        || ct == CONNECTION_TYPE_PLAYBACK;
}

namespace
{
    std::wstring LoginMessage(const std::string& error)
    {
        if (error == "wrong_cid")
            return Tr(L"CID не тот, под которым вы в сети VATSIM");
        if (error == "wrong_credentials")
            return Tr(L"Фамилия, имя или отчество не те, что при регистрации");
        if (error == "not_registered")
            return Tr(L"Вы не зарегистрированы в системе КСА");
        if (error == "rate_limited")
            return Tr(L"Слишком много попыток - подождите минуту");
        if (error == "bad_request" || error == "bad_json")
            return Tr(L"Сервер не принял запрос - проверьте введённое");

        if (error == "network")
            return Tr(L"Нет связи с сервером");
        if (error == "not_online")
            return Tr(L"Сервер пока не видит вас в сети - повторите через минуту");
        if (error == "network_stale")
            return Tr(L"Сервер не получает данные VATSIM - повторите позже");
        if (error == "no_server")
            return Tr(L"База пользователей недоступна");
        if (error == "http_404" || error == "method_not_allowed")
            return Tr(L"Сервер ещё не поддерживает вход");
        return Tr(L"Ошибка сервера (") + Widen(error.c_str()) + L")";
    }
}

void CGalaxyATMSystemPlugin::StartLogin(const std::wstring& cid, const std::wstring& surname,
    const std::wstring& firstName, const std::wstring& patronymic)
{
    const std::string position = MyPosition();
    {
        std::lock_guard<std::mutex> lock(m_identityMutex);
        if (m_loginState == LoginState::Sending)
            return;
        m_loginState = LoginState::Sending;
        m_loginMessage.clear();
    }
    if (m_login.joinable())
        m_login.join();

    std::string url = m_config.SquawkServerUrl();
    std::string key = m_config.SquawkApiKey();
    m_login = std::thread([this, cid, surname, firstName, patronymic, position, url, key]()
        {
            std::wstring name;
            std::string error;
            const bool ok = SubmitLogin(url, key, position, cid, surname, firstName, patronymic, name, error);

            std::lock_guard<std::mutex> lock(m_identityMutex);
            if (ok)
            {
                if (!name.empty() && _stricmp(m_identity.callsign.c_str(), position.c_str()) == 0)
                    m_identity.registeredName = name;
                m_accessSuspended = false;
                m_loginState = LoginState::Done;
                Log::Info("auth", "LOGIN " + position + ": let in by the user base as \"" + Log::Utf8(name) + "\"");
            }
            else
            {
                m_loginState = LoginState::Failed;
                m_loginMessage = LoginMessage(error);
                Log::Error("auth", "LOGIN " + position + " refused: " + error);
            }
        });
}

CGalaxyATMSystemPlugin::LoginState CGalaxyATMSystemPlugin::MyLogin(std::wstring* message) const
{
    std::lock_guard<std::mutex> lock(m_identityMutex);
    if (message != NULL)
        *message = m_loginMessage;
    return m_loginState;
}

namespace
{
    const char* const kLoginSetting = "GalaxyLogin";

    std::string SettingUtf8(const std::wstring& text)
    {
        if (text.empty())
            return std::string();
        const int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(),
            NULL, 0, NULL, NULL);
        std::string out(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), &out[0], n, NULL, NULL);
        return out;
    }

    std::wstring SettingWide(const std::string& utf8)
    {
        if (utf8.empty())
            return std::wstring();
        const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), NULL, 0);
        std::wstring out(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), &out[0], n);
        return out;
    }

    std::string EncodeSetting(const std::wstring& text)
    {
        static const char kHex[] = "0123456789ABCDEF";
        std::string out;
        for (char c : SettingUtf8(text))
        {
            const unsigned char u = (unsigned char)c;
            if (u <= 0x20 || u >= 0x7F || c == '%' || c == '|' || c == ':')
            {
                out += '%';
                out += kHex[u >> 4];
                out += kHex[u & 0xF];
            }
            else
                out += c;
        }
        return out;
    }

    std::wstring DecodeSetting(const std::string& text)
    {
        std::string utf8;
        for (size_t i = 0; i < text.size(); i++)
        {
            if (text[i] == '%' && i + 2 < text.size())
            {
                const std::string hex = text.substr(i + 1, 2);
                utf8 += (char)strtoul(hex.c_str(), NULL, 16);
                i += 2;
            }
            else
                utf8 += text[i];
        }
        return SettingWide(utf8);
    }
}

const CGalaxyATMSystemPlugin::SavedLogin& CGalaxyATMSystemPlugin::SavedIdentity()
{
    if (!m_savedLoginRead)
    {
        m_savedLoginRead = true;
        const char* stored = GetDataFromSettings(kLoginSetting);
        if (stored != NULL && *stored != '\0')
        {
            std::vector<std::string> parts(1);
            for (const char* p = stored; *p != '\0'; p++)
            {
                if (*p == '|')
                    parts.push_back(std::string());
                else
                    parts.back() += *p;
            }
            parts.resize(4);
            m_savedLogin.cid = DecodeSetting(parts[0]);
            m_savedLogin.surname = DecodeSetting(parts[1]);
            m_savedLogin.firstName = DecodeSetting(parts[2]);
            m_savedLogin.patronymic = DecodeSetting(parts[3]);
            Log::Info("auth", "saved login read from the settings: CID " + Log::Utf8(m_savedLogin.cid)
                + ", \"" + Log::Utf8(m_savedLogin.surname + L" " + m_savedLogin.firstName
                    + L" " + m_savedLogin.patronymic) + "\"");
        }
    }
    return m_savedLogin;
}

void CGalaxyATMSystemPlugin::SaveIdentity(const SavedLogin& id)
{
    m_savedLoginRead = true;
    m_savedLogin = id;
    SaveDataToSettings(kLoginSetting, "вход в КСА: CID и ФИО, введённые один раз",
        (EncodeSetting(id.cid) + "|" + EncodeSetting(id.surname) + "|"
            + EncodeSetting(id.firstName) + "|" + EncodeSetting(id.patronymic)).c_str());
    Log::Info("auth", "login saved to the settings - it will not be asked for again");
}

void CGalaxyATMSystemPlugin::ResetLogin()
{
    std::lock_guard<std::mutex> lock(m_identityMutex);
    if (m_loginState != LoginState::Sending)
    {
        m_loginState = LoginState::Idle;
        m_loginMessage.clear();
    }
}

std::string CGalaxyATMSystemPlugin::RegisterPageUrl() const
{
    std::string url = m_config.SquawkServerUrl();
    while (!url.empty() && (url.back() == '/' || url.back() == ' '))
        url.pop_back();
    if (url.empty())
        return url;

    const size_t api = 4;
    if (url.size() > api && _stricmp(url.c_str() + url.size() - api, "/api") == 0)
        url.resize(url.size() - api);
    return url + "/register/";
}

std::shared_ptr<const std::vector<Sigmet>> CGalaxyATMSystemPlugin::Sigmets() const
{
    std::lock_guard<std::mutex> lock(m_sigmetMutex);
    return m_sigmets;
}

void CGalaxyATMSystemPlugin::StartSigmetFetch()
{
    if (!m_config.SigmetsEnabled())
        return;

    if (m_sigmetFetch.joinable())
        m_sigmetFetch.join();

    std::vector<std::wstring> firs = m_config.SigmetFirs();
    m_sigmetFetch = std::thread([this, firs]()
        {
            std::vector<Sigmet> fetched;
            if (!FetchSigmets(firs, fetched))
                return;

            auto list = std::make_shared<const std::vector<Sigmet>>(std::move(fetched));
            std::lock_guard<std::mutex> lock(m_sigmetMutex);
            m_sigmets = list;
        });
}

EuroScopePlugIn::CRadarScreen* CGalaxyATMSystemPlugin::OnRadarScreenCreated(
    const char* sDisplayName, bool NeedRadarContent, bool GeoReferenced,
    bool CanBeSaved, bool CanBeCreated)
{
    return new CGalaxyATMSystemRadarScreen();
}

void CGalaxyATMSystemPlugin::OnNewMetarReceived(const char* sStation, const char* sFullMetar)
{
    if (sStation == NULL || sFullMetar == NULL)
        return;

    char cfgAirport[16] = { 0 };
    WideCharToMultiByte(CP_ACP, 0, m_config.Airport().c_str(), -1, cfgAirport, sizeof(cfgAirport) - 1, NULL, NULL);
    if (_stricmp(sStation, cfgAirport) != 0)
        return;

    int hpa = ParseQnhHpa(sFullMetar);
    if (hpa <= 0)
        return;

    m_gotLiveMetar = true;
    ApplyQnhHpa(hpa);
}

void CGalaxyATMSystemPlugin::ApplyQnhHpa(int hpa)
{
    wchar_t buf[16];
    swprintf_s(buf, L"%d", hpa);
    m_qnhHpa = buf;
    swprintf_s(buf, L"%d", (int)lround(hpa * 0.750062));
    m_qnhMmHg = buf;
}

void CGalaxyATMSystemPlugin::StartMetarFetch()
{
    if (m_gotLiveMetar)
        return;

    std::string station = AirportIcao();
    if (station.empty())
        return;

    if (m_metarFetch.joinable())
        m_metarFetch.join();

    m_metarFetch = std::thread([this, station]()
        {
            std::string body;
            if (!Net::HttpGet("https://metar.vatsim.net/metar.php?id=" + station, body, 4096))
                return;
            int hpa = ParseQnhHpa(body);
            if (hpa > 0)
                m_fetchedQnhHpa.store(hpa);
            else
                Log::Error("metar", "no QNH in the METAR for " + station + ": " + Log::Snippet(body, 120));
        });
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

    m_squawk.SetPosition(SquawkReady(false) ? MyPosition() : "");

    int ct = GetConnectionType();
    std::string position = MyPosition();
    if ((ct == CONNECTION_TYPE_DIRECT || ct == CONNECTION_TYPE_VIA_PROXY) && !position.empty())
    {
        if (position != m_identityAskedFor || Counter % 60 == 0)
            StartIdentityFetch(position);
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

    if (m_gotLiveMetar)
        return;

    int hpa = m_fetchedQnhHpa.exchange(0);
    if (hpa > 0)
        ApplyQnhHpa(hpa);

    if (Counter > 0 && Counter % 60 == 0)
        StartMetarFetch();
}

bool CGalaxyATMSystemPlugin::AltFilterPasses(int altFt) const
{
    if (!m_altFilterEnabled)
        return true;

    int fl = altFt / 100;
    int lo = min(m_altFilterFromFL, m_altFilterToFL);
    int hi = max(m_altFilterFromFL, m_altFilterToFL);
    return fl >= lo && fl <= hi;
}

std::wstring CGalaxyATMSystemPlugin::TransitionLevel() const
{
    wchar_t buf[8];
    swprintf_s(buf, L"F%03d", TransitionLevelFL());
    return buf;
}

int CGalaxyATMSystemPlugin::TransitionLevelFL() const
{
    int hpa = _wtoi(m_qnhHpa.c_str());
    return (hpa < 960) ? 70 : (hpa < 996) ? 60 : 50;
}

void CGalaxyATMSystemPlugin::RefreshApwZones()
{
    const ULONGLONG now = GetTickCount64();
    if (m_apwZonesTick != 0 && now - m_apwZonesTick < 2000)
        return;
    m_apwZonesTick = now;

    const std::vector<Zone>& zones = m_config.Zones();
    if (zones.empty() || !m_config.Apw().enabled)
    {
        m_apwZones.clear();
        return;
    }

    static const std::vector<ZoneBooking> kNoBookings;

    std::shared_ptr<const std::vector<ZoneBooking>> aup = AupBookings();
    std::shared_ptr<const std::vector<ZoneBooking>> notams = Notams();

    ZoneActivation what;
    what.aup = aup ? aup.get() : &kNoBookings;
    what.notams = notams ? notams.get() : NULL;
    what.showNotamWhenUnknown = m_config.ShowNotamAreas();

    const time_t nowUtc = time(NULL);

    std::vector<char> active(zones.size(), 0);
    std::vector<const ZoneBooking*> bookings(zones.size(), NULL);
    for (size_t i = 0; i < zones.size(); i++)
    {
        const ZoneBooking* hit = NULL;
        active[i] = ZoneActiveNow(zones[i], what, nowUtc, &hit) ? 1 : 0;
        bookings[i] = hit;
    }

    ApwBuildZones(zones, active, bookings, m_config.Apw(), m_apwZones);
}

const ApwResult& CGalaxyATMSystemPlugin::ApwFor(CRadarTarget& target)
{
    static const ApwResult kNone;

    const ApwSettings& cfg = m_config.Apw();
    if (!cfg.enabled || !target.IsValid())
        return kNone;

    RefreshApwZones();
    if (m_apwZones.empty())
        return kNone;

    const std::string callsign = target.GetCallsign();
    const ULONGLONG now = GetTickCount64();

    ApwCacheEntry& entry = m_apwCache[callsign];
    if (entry.tick != 0 && now - entry.tick < 1000)
        return entry.result;

    CRadarTargetPositionData pos = target.GetPosition();
    if (!pos.IsValid())
    {
        entry.tick = now;
        entry.result = ApwResult();
        return entry.result;
    }

    ApwTrack track;
    track.pos = pos.GetPosition();
    track.trackDeg = target.GetTrackHeading();
    track.gsKt = target.GetGS();
    track.vsFpm = target.GetVerticalSpeed();

    const bool belowTL = pos.GetFlightLevel() / 100 < TransitionLevelFL();
    track.altFt = belowTL ? pos.GetPressureAltitude() : pos.GetFlightLevel();

    CFlightPlan fp = target.GetCorrelatedFlightPlan();
    if (fp.IsValid())
    {
        track.origin = Upper(Widen(fp.GetFlightPlanData().GetOrigin()));
        track.destination = Upper(Widen(fp.GetFlightPlanData().GetDestination()));
    }

    entry.result = ApwCheck(m_config.Zones(), m_apwZones, track, cfg);
    entry.tick = now;

    if (m_apwCache.size() > 256)
    {
        for (auto it = m_apwCache.begin(); it != m_apwCache.end(); )
        {
            if (it->first != callsign && now - it->second.tick > 60000)
                it = m_apwCache.erase(it);
            else
                ++it;
        }
    }

    return m_apwCache[callsign].result;
}

void CGalaxyATMSystemPlugin::OnGetTagItem(
    CFlightPlan FlightPlan, CRadarTarget RadarTarget,
    int ItemCode, int TagData, char sItemString[16],
    int* pColorCode, COLORREF* pRGB, double* pFontSize)
{
    *pColorCode = EuroScopePlugIn::TAG_COLOR_DEFAULT;
    sItemString[0] = '\0';

    if (pFontSize != NULL && *pFontSize > 0.0
        && ItemCode != TAG_ITEM_SQUAWK && ItemCode != TAG_ITEM_SQUAWK_SET)
        *pFontSize *= m_tagFontSize / 12.0;

    if (ItemCode != TAG_ITEM_APW && ItemCode != TAG_ITEM_SQUAWK && RadarTarget.IsValid())
    {
        CRadarTargetPositionData filterPos = RadarTarget.GetPosition();
        if (filterPos.IsValid() && !AltFilterPasses(filterPos.GetPressureAltitude()))
            return;
    }

    switch (ItemCode)
    {
    case TAG_ITEM_CALLSIGN:
    {
        const char* callsign = FlightPlan.IsValid() ? FlightPlan.GetCallsign()
            : RadarTarget.IsValid() ? RadarTarget.GetCallsign() : NULL;
        if (callsign == NULL)
            return;
        strncpy_s(sItemString, 16, callsign, _TRUNCATE);
        break;
    }
    case TAG_ITEM_ALTITUDE:
    {
        if (!RadarTarget.IsValid())
            return;
        CRadarTargetPositionData pos = RadarTarget.GetPosition();
        if (!pos.IsValid())
            return;
        bool belowTL = pos.GetFlightLevel() / 100 < TransitionLevelFL();
        int altFt = belowTL ? pos.GetPressureAltitude() : pos.GetFlightLevel();
        strcpy_s(sItemString, 16, FormatAltitudeUnit(altFt, m_unitAlt).c_str());
        break;
    }
    case TAG_ITEM_VERTICAL_SPEED:
    {
        if (!RadarTarget.IsValid())
            return;
        strcpy_s(sItemString, 16, FormatVerticalSpeedUnit(RadarTarget.GetVerticalSpeed(), m_unitVs).c_str());
        break;
    }
    case TAG_ITEM_GROUND_SPEED:
    {
        if (!RadarTarget.IsValid())
            return;
        strcpy_s(sItemString, 16, FormatGroundSpeedUnit(RadarTarget.GetGS(), m_unitGs).c_str());
        break;
    }
    case TAG_ITEM_DISTANCE:
    {
        if (!FlightPlan.IsValid())
            return;
        strcpy_s(sItemString, 16, FormatDistanceUnit(FlightPlan.GetDistanceToDestination(), m_unitDist).c_str());
        break;
    }
    case TAG_ITEM_APW:
    {
        if (!RadarTarget.IsValid())
            return;

        const ApwResult& apw = ApwFor(RadarTarget);
        if (apw.level == ApwLevel::None)
            return;

        std::wstring text = L"APW";
        if (m_config.Apw().showZone && !apw.zoneId.empty())
            text += L" " + apw.zoneId;
        strcpy_s(sItemString, 16, Narrow(text.substr(0, 15)).c_str());

        *pColorCode = EuroScopePlugIn::TAG_COLOR_RGB_DEFINED;
        *pRGB = (apw.level == ApwLevel::Inside) ? Theme::ApwInside : Theme::ApwPredicted;
        break;
    }
    case TAG_ITEM_SQUAWK:
    {
        if (!FlightPlan.IsValid())
            return;

        std::string callsign = FlightPlan.GetCallsign();

        if (m_squawk.Enabled() && m_squawk.IsPending(callsign))
        {
            strcpy_s(sItemString, 16, "....");
            *pColorCode = EuroScopePlugIn::TAG_COLOR_RGB_DEFINED;
            *pRGB = Theme::SquawkPending;
            break;
        }
        if (m_squawk.Enabled() && !m_squawk.LastError(callsign).empty())
        {
            strcpy_s(sItemString, 16, "ERR");
            *pColorCode = EuroScopePlugIn::TAG_COLOR_RGB_DEFINED;
            *pRGB = Theme::SquawkError;
            break;
        }

        std::string code = AssignedSquawk(FlightPlan);
        strcpy_s(sItemString, 16, code.empty() ? "----" : code.substr(0, 15).c_str());
        *pColorCode = EuroScopePlugIn::TAG_COLOR_RGB_DEFINED;
        *pRGB = SquawkColor(FlightPlan, RadarTarget, code);
        break;
    }
    case TAG_ITEM_SQUAWK_SET:
    {
        if (!FlightPlan.IsValid() || !RadarTarget.IsValid())
            return;

        std::string assigned = AssignedSquawk(FlightPlan);
        const char* set = RadarTarget.GetPosition().GetSquawk();
        if (assigned.empty() || set == NULL || *set == '\0' || assigned == set)
            return;

        strcpy_s(sItemString, 16, std::string(set).substr(0, 15).c_str());
        *pColorCode = EuroScopePlugIn::TAG_COLOR_RGB_DEFINED;
        *pRGB = Theme::SquawkMismatch;
        break;
    }
    default:
        break;
    }
}

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

static bool OnControllerPosition(const CController& me)
{
    if (!me.IsValid() || !me.IsController() || me.GetFacility() < 1)
        return false;
    const char* callsign = me.GetCallsign();
    if (callsign == NULL || *callsign == '\0')
        return false;
    size_t len = strlen(callsign);
    return !(len >= 4 && _stricmp(callsign + len - 4, "_OBS") == 0);
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
    if (m_squawk.Enabled())
    {
        auto held = m_squawk.Assignments();
        auto it = held->find(fp.GetCallsign());
        if (it != held->end())
            return it->second;
    }
    const char* assigned = fp.GetControllerAssignedData().GetSquawk();
    return assigned != NULL ? assigned : "";
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
        fp.GetControllerAssignedData().SetSquawk(code.c_str());
        if (SquawkReady(false))
            m_squawk.Report(fp.GetCallsign(), code, MyPosition(), true);
        return;
    }

    default:
        return;
    }
}

void CGalaxyATMSystemPlugin::OnFunctionCall(int FunctionId, const char* sItemString, POINT Pt, RECT Area)
{
    HandleSquawkFunction(FunctionId, sItemString, Area, "plugin");
}

void CGalaxyATMSystemPlugin::OnFlightPlanControllerAssignedDataUpdate(CFlightPlan FlightPlan, int DataType)
{
    if (DataType != CTR_DATA_TYPE_SQUAWK || !FlightPlan.IsValid())
        return;

    std::string callsign = FlightPlan.GetCallsign();
    std::string code = FlightPlan.GetControllerAssignedData().GetSquawk();

    auto ours = m_squawkSetByUs.find(callsign);
    if (ours != m_squawkSetByUs.end())
    {
        const bool same = (ours->second == code);
        m_squawkSetByUs.erase(ours);
        if (same)
            return;
    }

    if (!IsSquawkCode(code) || !SquawkReady(false))
        return;

    auto held = m_squawk.Assignments();
    auto it = held->find(callsign);
    if (it != held->end() && it->second == code)
        return;

    m_squawk.Report(callsign, code, MyPosition(), false);
}

CGalaxyATMSystemRadarScreen::CGalaxyATMSystemRadarScreen()
{
    m_panelArea = { 0, 0, 0, 0 };
    m_visible = true;
    m_loginWindowOpen = false;
    m_loginArea = { 0, 0, 0, 0 };
    m_loginPositioned = false;
    m_loginDrawnTick = 0;
    for (RECT& field : m_loginFields)
        field = { 0, 0, 0, 0 };
    m_entryField = -1;
    m_entryPending = -1;
    m_entryPendingTick = 0;
    m_entryView = NULL;
    m_collapsed = false;
    m_dragOffset = { 0, 0 };

    m_authState = AuthState::LoggedOut;
    m_authStartTick = 0;
    m_autoLoginTried = false;
    m_authFailed = false;

    m_timerRunning = false;
    m_timerStartTick = 0;
    m_timerElapsedMs = 0;

    m_vecDistEnabled = false;
    m_vecDistKm = 10;
    m_vecTimeEnabled = true;
    m_vecTimeMin = 2;
    m_vecByPlan = false;
    m_vecShowLevel = false;

    m_openDropdown = DropdownKind::None;
    m_vecDistFieldRect = { 0, 0, 0, 0 };
    m_vecTimeFieldRect = { 0, 0, 0, 0 };

    m_esFont = NULL;
    m_rulerFont = NULL;
    m_rulerFontSource = NULL;

    m_osLines = 3;
    m_osSpeed = true;
    m_osFontFieldRect = { 0, 0, 0, 0 };

    m_formularsVisible = true;
    m_formularKindSetting = FormularKindSetting::Auto;
    m_formularFont = NULL;
    m_formularFontSize = 0;
    m_hdgDragging = false;
    m_hdgDragMoved = false;
    m_hdgDragStart = { 0, 0 };
    m_hdgDragPt = { 0, 0 };
    m_hdgDragEndTick = 0;
    m_hdgDragCancelled = false;
    m_hdgReleaseTicks = 0;

    m_codeAll = false;
    m_codeBp = false;
    m_codeExtra = false;
    m_vvGain = 35;
    m_vvDragging = false;
    m_vvSliderRect = { 0, 0, 0, 0 };

    m_atisLetterOpen = true;
    m_atisLetterArea = { 0, 0, 0, 0 };

    m_rcOpen = false;
    m_rcArea = { 0, 0, 0, 0 };
    m_rcPositioned = false;
    m_rcScroll = 0;
    m_rcScrollMine = 0;
    m_rcPageRows[0] = m_rcPageRows[1] = 6;
    m_rcSortKey = 1;
    m_rcSortAsc = true;
    m_rcScale = 40;
    m_rcResizing = false;
    m_rcResizeGrab = 0;
    m_rcFont = NULL;
    m_rcHeadFont = NULL;
    m_rcRowFont = NULL;
    m_rcKfFont = NULL;
    m_rcFontScale = 0;
    m_rcFilterBefore = -1;
    m_rcFilterAfter = -1;
    m_rcFloating = false;
    m_rcFloatPos = { 0, 0 };
    m_rcDragView = NULL;
    m_rcFloatDrawn = 0;
    m_rcDrawingFloat = false;
    m_rcFloatResizing = false;
    m_rcFloatGrab = 0;

    m_atisOpen = false;
    m_atisScrollPx = 0;
    m_atisScrollMax = 0;
    m_atisThumbH = 0;
    m_atisArea = { 0, 0, 0, 0 };
    m_atisPositioned = false;

    m_sigmetsVisible = true;
    m_sigmetInfoIndex = -1;
    m_sigmetInfoAt = { 0, 0 };
    m_sigmetInfoHeld = false;
    m_sigmetInfoWait = 0;

    m_zonesVisible = (g_plugin != NULL) ? g_plugin->GetConfig().ZonesEnabled() : true;
    m_zoneInfoIndex = -1;
    m_zoneInfoAt = { 0, 0 };
    m_zoneInfoHeld = false;
    m_zoneInfoWait = 0;
    m_areaShiftDown = false;

    m_rulerButton = VK_XBUTTON2;
    m_rulerButtonDown = false;
    m_rulerPressPending = false;
    m_rulerArmed = false;
    m_rulerPlacing = false;

    UINT_PTR id = SetTimer(NULL, 0, 1000, [](HWND, UINT, UINT_PTR idEvent, DWORD)
        {
            auto it = g_timers.find(idEvent);
            if (it != g_timers.end())
            {
                it->second->RequestRefresh();
                it->second->TickRcFloat();
            }
        });
    g_timers[id] = this;
    m_timerId = id;

    UINT_PTR pollId = SetTimer(NULL, 0, 40, [](HWND, UINT, UINT_PTR idEvent, DWORD)
        {
            auto it = g_pollTimers.find(idEvent);
            if (it != g_pollTimers.end())
            {
                it->second->PollRulerButton();
                it->second->TickAuth();
                it->second->TickEntry();
            }
        });
    g_pollTimers[pollId] = this;
    m_pollTimerId = pollId;
}

CGalaxyATMSystemRadarScreen::~CGalaxyATMSystemRadarScreen()
{
    m_rcEntry.Close();
    m_rcFloat.Destroy();
    if (m_timerId != 0)
    {
        KillTimer(NULL, m_timerId);
        g_timers.erase(m_timerId);
    }
    if (m_pollTimerId != 0)
    {
        KillTimer(NULL, m_pollTimerId);
        g_pollTimers.erase(m_pollTimerId);
    }
    m_fonts.Destroy();
    if (m_rulerFont != NULL)
        DeleteObject(m_rulerFont);
    if (m_formularFont != NULL)
        DeleteObject(m_formularFont);
    for (HFONT f : { m_rcFont, m_rcHeadFont, m_rcRowFont, m_rcKfFont })
        if (f != NULL)
            DeleteObject(f);
}

WorkMode CGalaxyATMSystemRadarScreen::GetWorkMode(std::wstring& labelOut, COLORREF& colorOut)
{
    int ct = GetPlugIn()->GetConnectionType();
    CController me = GetPlugIn()->ControllerMyself();
    int rating = me.IsValid() ? me.GetRating() : 0;

    if (rating >= 11)
    {
        labelOut = L"SUP";
        colorOut = Theme::ModeSup;
        return WorkMode::Sup;
    }

    switch (ct)
    {
    case CONNECTION_TYPE_DIRECT:
    case CONNECTION_TYPE_VIA_PROXY:
        labelOut = L"OPS";
        colorOut = Theme::ModeOps;
        return WorkMode::Ops;
    case CONNECTION_TYPE_SIMULATOR_SERVER:
    case CONNECTION_TYPE_PLAYBACK:
    case CONNECTION_TYPE_SIMULATOR_CLIENT:
    case CONNECTION_TYPE_SWEATBOX:
        labelOut = L"SIM";
        colorOut = Theme::ModeSim;
        return WorkMode::Sim;
    default:
        labelOut = L"OFFLINE";
        colorOut = Theme::ModeOffline;
        return WorkMode::Offline;
    }
}

void CGalaxyATMSystemRadarScreen::GetUserInfo(std::wstring& designation,
    std::wstring& role, std::wstring& user)
{
    CController me = GetPlugIn()->ControllerMyself();
    std::string callsign = me.IsValid() ? me.GetCallsign() : "";
    std::string posId = me.IsValid() ? me.GetPositionId() : "";

    PositionInfo pi;
    if (Plugin()->GetConfig().FindPosition(callsign, posId, pi))
    {
        designation = pi.Designation;
        role = pi.Role;
    }
    else if (OnControllerPosition(me) && !posId.empty())
    {
        designation = Widen(posId.c_str());
    }

    if (designation.empty())
        designation = L"—";
    if (role.empty())
        role = L"—";

    if (Plugin()->TrainingSession())
    {
        user = L"user";
        return;
    }

    user =(m_authState == AuthState::LoggedOut) ? std::wstring() : Plugin()->MyUserName();
    if (user.empty())
        user = L"user ?";
}

std::wstring CGalaxyATMSystemRadarScreen::GetDistressCodes()
{
    std::wstring out;
    for (CRadarTarget rt = GetPlugIn()->RadarTargetSelectFirst(); rt.IsValid();
        rt = GetPlugIn()->RadarTargetSelectNext(rt))
    {
        const char* squawk = rt.GetPosition().GetSquawk();
        if (squawk == NULL || !IsDistressSquawk(squawk))
            continue;
        if (!out.empty())
            out += L" ";
        out += Widen(rt.GetCallsign()) + L"/" + Widen(squawk);
    }
    return out;
}

std::wstring CGalaxyATMSystemRadarScreen::GetDuplicateCodes()
{
    std::map<std::string, int> seen;
    for (CRadarTarget rt = GetPlugIn()->RadarTargetSelectFirst(); rt.IsValid();
        rt = GetPlugIn()->RadarTargetSelectNext(rt))
    {
        const char* squawk = rt.GetPosition().GetSquawk();
        if (squawk == NULL || strlen(squawk) != 4 || IsConspicuitySquawk(squawk))
            continue;
        seen[squawk]++;
    }

    std::wstring out;
    for (const auto& kv : seen)
    {
        if (kv.second < 2)
            continue;
        if (!out.empty())
            out += L" ";
        out += Widen(kv.first.c_str());
    }
    return out;
}

void CGalaxyATMSystemRadarScreen::DrawCheckbox(HDC hDC, RECT box, bool checked,
    int objType, const char* objId, const char* tooltip)
{
    Theme::OutlineBox(hDC, box, checked ? Theme::Active : Theme::ControlFill, Theme::BorderCheck);
    AddScreenObject(objType, objId, box, false, tooltip);
}

void CGalaxyATMSystemRadarScreen::DrawCheckRow(HDC hDC, int top, int x, const std::wstring& label,
    bool checked, int objType, const char* objId, const char* tooltip)
{
    int cy = top + (kRowH - kCheckSize) / 2;
    RECT chk = { x, cy, x + kCheckSize, cy + kCheckSize };
    DrawCheckbox(hDC, chk, checked, objType, objId, tooltip);

    RECT lbl = { chk.right + 6, top, ContentRight(), top + kRowH };
    Theme::DrawLine(hDC, lbl, label, m_fonts.Body, Theme::Text, DT_LEFT | DT_VCENTER);
}

void CGalaxyATMSystemRadarScreen::DrawRadioRow(HDC hDC, int top, int x, int labelRight,
    const std::wstring& label, bool selected, int objType, const char* objId, const char* tooltip)
{
    int cy = top + (kRowH - kRadioSize) / 2;
    RECT pill = { x, cy, x + kRadioSize, cy + kRadioSize };
    Theme::DrawRadio(hDC, pill, selected);
    AddScreenObject(objType, objId, pill, false, tooltip);

    RECT lbl = { pill.right + 2, top, labelRight, top + kRowH };
    Theme::DrawLine(hDC, lbl, label, m_fonts.Body, Theme::Text, DT_LEFT | DT_VCENTER);
}

void CGalaxyATMSystemRadarScreen::DrawOutlinedField(HDC hDC, RECT box, const std::wstring& text, HFONT font)
{
    Theme::DrawControl(hDC, box, text, font);
}

void CGalaxyATMSystemRadarScreen::DrawFittedField(HDC hDC, RECT box, const std::wstring& text)
{
    const int avail = (box.right - box.left) - 6;

    HFONT font = m_fonts.Body;
    for (HFONT candidate : { m_fonts.Body, m_fonts.Small, m_fonts.Tiny })
    {
        font = candidate;
        if (Theme::MeasureText(hDC, candidate, text).cx <= avail)
            break;
    }

    Theme::OutlineBox(hDC, box, Theme::ControlFill, Theme::BorderStrong);
    RECT inner = { box.left + 3, box.top, box.right - 3, box.bottom };
    Theme::DrawLine(hDC, inner, text, font, Theme::Text, DT_CENTER | DT_VCENTER | DT_END_ELLIPSIS);
}

void CGalaxyATMSystemRadarScreen::DrawToggleChip(HDC hDC, RECT box, const std::wstring& text,
    bool active, int objType, const char* objId, const char* tooltip, COLORREF idleFill)
{
    if (active)
    {
        Theme::DrawValueField(hDC, box, text, m_fonts.Body, true);
    }
    else
    {
        Theme::OutlineBox(hDC, box, idleFill, Theme::BorderStrong);
        Theme::DrawLine(hDC, box, text, m_fonts.Body, Theme::Text, DT_CENTER | DT_VCENTER);
    }
    AddScreenObject(objType, objId, box, false, tooltip);
}

void CGalaxyATMSystemRadarScreen::DrawDropdownField(HDC hDC, RECT box, const std::wstring& text,
    int objType, const char* objId, const char* tooltip)
{
    Theme::OutlineBox(hDC, box, Theme::ButtonMid, Theme::BorderStrong);

    RECT chevron = { box.right - 19, box.top, box.right, box.bottom };

    HPEN pen = CreatePen(PS_SOLID, 1, Theme::BorderStrong);
    HPEN oldPen = (HPEN)SelectObject(hDC, pen);
    MoveToEx(hDC, chevron.left, box.top, NULL);
    LineTo(hDC, chevron.left, box.bottom);
    SelectObject(hDC, oldPen);
    DeleteObject(pen);

    int mx = (chevron.left + chevron.right) / 2, my = (box.top + box.bottom) / 2;
    HBRUSH br = CreateSolidBrush(Theme::Text);
    HBRUSH oldBr = (HBRUSH)SelectObject(hDC, br);
    HPEN dotPen = CreatePen(PS_SOLID, 1, Theme::Text);
    oldPen = (HPEN)SelectObject(hDC, dotPen);
    Ellipse(hDC, mx - 2, my - 2, mx + 2, my + 2);
    SelectObject(hDC, oldPen);
    DeleteObject(dotPen);
    SelectObject(hDC, oldBr);
    DeleteObject(br);

    RECT textRect = { box.left + 5, box.top, chevron.left - 2, box.bottom };
    Theme::DrawLine(hDC, textRect, text, m_fonts.Body, Theme::Text, DT_LEFT | DT_VCENTER);

    AddScreenObject(objType, objId, chevron, false, tooltip);
}

void CGalaxyATMSystemRadarScreen::DrawDropdownList(HDC hDC)
{
    const int* values = NULL;
    int count = 0, current = 0;
    RECT anchor = { 0, 0, 0, 0 };

    if (m_openDropdown == DropdownKind::VecDist)
    {
        values = kDistanceSteps; count = kDistanceStepsCount;
        current = m_vecDistKm;   anchor = m_vecDistFieldRect;
    }
    else if (m_openDropdown == DropdownKind::VecTime)
    {
        values = kTimeSteps; count = kTimeStepsCount;
        current = m_vecTimeMin; anchor = m_vecTimeFieldRect;
    }
    else if (m_openDropdown == DropdownKind::OsFont)
    {
        values = kFontSizeSteps; count = kFontSizeStepsCount;
        current = Plugin()->TagFontSize(); anchor = m_osFontFieldRect;
    }
    else
    {
        return;
    }

    const int rowH = 17;
    const int listW = max(anchor.right - anchor.left, 44);
    const int listH = count * rowH + 2;

    RECT list = { anchor.left, anchor.bottom + 2, anchor.left + listW, anchor.bottom + 2 + listH };

    RECT ra = GetRadarArea();
    if (list.bottom > ra.bottom)
        OffsetRect(&list, 0, -(listH + (anchor.bottom - anchor.top) + 4));
    if (list.right > ra.right)
        OffsetRect(&list, ra.right - list.right, 0);
    if (list.left < ra.left)
        OffsetRect(&list, ra.left - list.left, 0);

    int saved = SaveDC(hDC);
    SetBkMode(hDC, TRANSPARENT);
    Theme::OutlineBox(hDC, list, Theme::ControlFill, Theme::BorderStrong);

    for (int i = 0; i < count; i++)
    {
        RECT row = { list.left + 1, list.top + 1 + i * rowH, list.right - 1, list.top + 1 + (i + 1) * rowH };

        wchar_t label[16];
        swprintf_s(label, L"%d", values[i]);

        bool selected = (values[i] == current);
        if (selected)
            Theme::FillBox(hDC, row, Theme::Active);
        Theme::DrawLine(hDC, row, label, m_fonts.Body,
            selected ? Theme::ActiveText : Theme::Text, DT_CENTER | DT_VCENTER);

        char id[8];
        sprintf_s(id, "%d", i);
        AddScreenObject(SO_DROPDOWN_ITEM, id, row, false, "");
    }

    RestoreDC(hDC, saved);
}

RECT CGalaxyATMSystemRadarScreen::DrawBlockFrame(HDC hDC, int top, const std::wstring& caption, int boxHeight)
{
    RECT captionRect = { m_panelArea.left, top, m_panelArea.right, top + L::CAPTION_H };
    Theme::DrawLine(hDC, captionRect, caption, m_fonts.Body, Theme::Text, DT_CENTER | DT_VCENTER);

    RECT box = { GroupLeft(), captionRect.bottom + L::CAP_GAP, GroupRight(),
                 captionRect.bottom + L::CAP_GAP + boxHeight };
    Theme::OutlineBox(hDC, box, Theme::Background, Theme::Border);
    return box;
}

RECT CGalaxyATMSystemRadarScreen::DrawBoxOnly(HDC hDC, int top, int boxHeight)
{
    RECT box = { GroupLeft(), top, GroupRight(), top + boxHeight };
    Theme::OutlineBox(hDC, box, Theme::Background, Theme::Border);
    return box;
}

void CGalaxyATMSystemRadarScreen::OnRefresh(HDC hDC, int Phase)
{
    if (Phase != REFRESH_PHASE_BEFORE_TAGS &&
        Phase != REFRESH_PHASE_AFTER_TAGS &&
        Phase != REFRESH_PHASE_AFTER_LISTS)
        return;
    if (!m_visible)
    {
        m_rcEntry.Close();
        m_rcFloat.Hide();
        return;
    }

    m_fonts.EnsureCreated();

    SyncAuth();

    UpdateZoneActivity();

    m_sigmets = Plugin()->Sigmets();

    if (Phase == REFRESH_PHASE_BEFORE_TAGS)
    {
        DrawZones(hDC);
        DrawSigmets(hDC);
        return;
    }

    HFONT dcFont = (HFONT)GetCurrentObject(hDC, OBJ_FONT);
    m_esFont = (dcFont != NULL
                && dcFont != (HFONT)GetStockObject(SYSTEM_FONT)
                && dcFont != (HFONT)GetStockObject(DEVICE_DEFAULT_FONT)
                && dcFont != (HFONT)GetStockObject(DEFAULT_GUI_FONT))
        ? dcFont : m_fonts.Mono;

    if (Phase == REFRESH_PHASE_AFTER_TAGS)
    {
        m_areaShiftDown = ShiftHeldInEuroScope();
        if (m_areaShiftDown || m_zoneInfoIndex >= 0)
            RegisterZoneObjects();
        RegisterSigmetObjects();

        if (!Authorized())
        {
            m_rulerPressPending = false;
            m_rulerArmed = false;
            m_rulerPlacing = false;
            DrawWakeArcs(hDC);
            DrawTargetSymbols(hDC);
            DrawFormulars(hDC, true);
            return;
        }

        if (m_rulerPressPending)
        {
            m_rulerPressPending = false;
            if (m_rulerPlacing)
            {
                m_rulerPlacing = false;
                m_rulerArmed = false;
            }
            else
            {
                m_rulerArmed = !m_rulerArmed;
            }
        }
        if (m_rulerPlacing)
        {
            POINT cursor;
            if (CursorRadarPoint(cursor))
                UpdateRulerEnd(cursor);
        }

        for (size_t i = 0; i < m_rulers.size(); i++)
        {
            RulerLine& r = m_rulers[i];
            POINT a = ConvertCoordFromPositionToPixel(ResolveRulerPoint(r.startSnapped, r.startCallsign, r.startFixed));
            POINT b = ConvertCoordFromPositionToPixel(ResolveRulerPoint(r.endSnapped, r.endCallsign, r.endFixed));

            double lineLen = sqrt((double)(b.x - a.x) * (b.x - a.x) + (double)(b.y - a.y) * (b.y - a.y));
            const int kPad = 15;
            const double kStepPx = 20.0;
            const double kEndClearPx = kPad + 20.0;

            char id[16];
            sprintf_s(id, "%zu", i);

            if (lineLen <= 2.0 * kEndClearPx)
            {
                int px = (a.x + b.x) / 2, py = (a.y + b.y) / 2;
                RECT box = { px - kPad, py - kPad, px + kPad, py + kPad };
                AddScreenObject(SO_RULER_LINE, id, box, false, Tr("ПКМ/2ЛКМ - удалить линейку"));
                continue;
            }

            double tMin = kEndClearPx / lineLen, tMax = 1.0 - tMin;
            int steps = max(1, (int)lround((lineLen - 2.0 * kEndClearPx) / kStepPx));
            for (int s = 0; s <= steps; s++)
            {
                double t = tMin + (tMax - tMin) * s / steps;
                int px = a.x + (int)lround((b.x - a.x) * t);
                int py = a.y + (int)lround((b.y - a.y) * t);
                RECT box = { px - kPad, py - kPad, px + kPad, py + kPad };
                AddScreenObject(SO_RULER_LINE, id, box, false, Tr("ПКМ/2ЛКМ - удалить линейку"));
            }
        }

        if (m_rulerArmed || m_rulerPlacing)
        {
            AddScreenObject(SO_RULER_CANVAS, "RULER_CANVAS", GetRadarArea(), false,
                m_rulerPlacing ? Tr("ЛКМ - конец линейки, ПКМ - отмена")
                               : Tr("ЛКМ - начало линейки, ПКМ - отмена"));
        }

        DrawWakeArcs(hDC);

        if (m_vecDistEnabled || m_vecTimeEnabled || m_vecByPlan)
            DrawTargetVectors(hDC);

        DrawTargetSymbols(hDC);
        DrawFormulars(hDC, !(m_rulerArmed || m_rulerPlacing));

        for (size_t i = 0; i < m_rulers.size(); i++)
            DrawRulerLine(hDC, m_rulers[i], (int)i);
        if (m_rulerPlacing)
            DrawRulerLine(hDC, m_rulerPending);
        else if (m_rulerArmed)
            DrawRulerCursor(hDC);

        return;
    }

    if (m_authState != AuthState::LoggedOut && !Plugin()->TrainingSession() && Plugin()->AccessSuspended())
    {
        Plugin()->SetSessionAuthorized(false);
        m_authState = AuthState::LoggedOut;
        m_openDropdown = DropdownKind::None;
        m_rulerArmed = false;
        m_rulerPlacing = false;
        CloseLoginWindow();
        m_authMessage = Tr(L"Доступ приостановлен");
        ShowNotice(m_authMessage);
        Log::Warn("auth", "panel closed: access suspended - the name was removed from the user base");
    }

    DrawPanel(hDC);

    if (!m_collapsed)
        DrawMenuBar(hDC);

    if (m_loginWindowOpen && !Authorized())
    {
        if (Plugin()->MyLogin() == CGalaxyATMSystemPlugin::LoginState::Done)
        {
            CGalaxyATMSystemPlugin::SavedLogin id;
            id.cid = m_loginValues[LF_CID];
            id.surname = m_loginValues[LF_SURNAME];
            id.firstName = m_loginValues[LF_FIRST_NAME];
            id.patronymic = m_loginValues[LF_PATRONYMIC];
            Plugin()->SaveIdentity(id);

            CloseLoginWindow();
            StartAuthCheck();
        }
        else
        {
            // any failed attempt unlocks Bypass
            if (Plugin()->MyLogin() == CGalaxyATMSystemPlugin::LoginState::Failed)
                m_authFailed = true;
            DrawLoginWindow(hDC);
        }
    }
    else if (m_loginWindowOpen)
    {
        CloseLoginWindow();
    }

    if (Authorized())
    {
        if (m_atisLetterOpen)
            DrawAtisLetterWindow(hDC);
        if (m_atisOpen)
            DrawAtisWindow(hDC);
        if (m_rcOpen && m_rcFloating)
            RenderRcFloat();
        else if (m_rcOpen)
            DrawSectorListWindow(hDC);

        if (m_openDropdown != DropdownKind::None)
            DrawDropdownList(hDC);
    }

    if (!(m_rcOpen && m_rcFloating && Authorized()))
    {
        m_rcEntry.Close();
        m_rcFloat.Hide();
    }

    if (!m_noticeText.empty())
        DrawNoticeWindow(hDC);

    if (m_zoneInfoIndex >= 0)
        DrawZoneInfo(hDC);
    if (m_sigmetInfoIndex >= 0)
        DrawSigmetInfo(hDC);
}

namespace
{
    struct FormularRun
    {
        std::wstring text;
        COLORREF color;
        const FormularFn* fn;
    };

    const char* const kTopSky = "TopSky plugin";
    const char* const kUlll   = "ULLLPlugin";
    const char* const kVch    = "VCH";

    const FormularFn kFnSquawkWarning = { kTopSky, 138, kTopSky, 62,  NULL,    0    };
    const FormularFn kFnCommunication = { kTopSky, 201, NULL,    32,  NULL,    0    };
    const FormularFn kFnRemark        = { kTopSky, 212, kTopSky, 2,   NULL,    0    };
    const FormularFn kFnCallsign      = { kTopSky, 22,  kTopSky, 6,   kTopSky, 6    };
    const FormularFn kFnSector        = { kTopSky, 66,  NULL,    20,  kTopSky, 100  };
    const FormularFn kFnTssr          = { kTopSky, 106, kTopSky, 62,  NULL,    0    };
    const FormularFn kFnAfl           = { kUlll,   509, NULL,    1,   kTopSky, 99   };
    const FormularFn kFnCfl           = { kUlll,   510, kTopSky, 12,  kTopSky, 139  };
    const FormularFn kFnGs            = { kTopSky, 40,  kUlll,   507, NULL,    0    };
    const FormularFn kFnXfl           = { kTopSky, 53,  NULL,    26,  NULL,    0    };
    const FormularFn kFnCopx          = { kTopSky, 44,  NULL,    22,  kTopSky, 45   };
    const FormularFn kFnAhdg          = { NULL,    25,  NULL,    0,   kTopSky, 14   };
    const FormularFn kFnAsp           = { kTopSky, 47,  kTopSky, 15,  NULL,    TAG_ITEM_FUNCTION_ASSIGNED_SPEED_POPUP };
    const FormularFn kFnArc           = { kTopSky, 56,  kTopSky, 16,  kTopSky, 134  };
    const FormularFn kFnAtyp          = { kTopSky, 70,  kVch,    650, kTopSky, 2    };
    const FormularFn kFnAdes          = { kTopSky, 79,  NULL,    7,   kTopSky, 1001 };
    const FormularFn kFnRfl           = { kTopSky, 120, kTopSky, 59,  NULL,    0    };

    const FormularFn kFnAppSquawkWarning = { kTopSky, 138,   kTopSky, 62,  kTopSky, 62   };
    const FormularFn kFnAppRemark        = { kTopSky, 212,   kTopSky, 2,   kTopSky, 2    };
    const FormularFn kFnAppAfl           = { kUlll,   509,   NULL,    1,   kTopSky, 143  };
    const FormularFn kFnAppCfl           = { kUlll,   510,   kTopSky, 12,  kTopSky, 157  };
    const FormularFn kFnAppAtyp          = { kTopSky, 69,    kVch,    650, kTopSky, 2    };
    const FormularFn kFnArwy             = { kTopSky, 261,   NULL,    19,  NULL,    0    };
    const FormularFn kFnAppAhdg          = { NULL,    25,    NULL,    0,   kTopSky, 14   };

    const FormularFn kFnTwrSector        = { kTopSky, 10014, NULL,    20,  kTopSky, 100  };
    const FormularFn kFnTwrGs            = { kTopSky, 40,    NULL,    0,   NULL,    0    };

    bool IsAhdgFn(const FormularFn* fn) { return fn == &kFnAhdg || fn == &kFnAppAhdg; }
    bool IsCflFn(const FormularFn* fn)  { return fn == &kFnCfl || fn == &kFnAppCfl; }
    bool IsGsFn(const FormularFn* fn)   { return fn == &kFnGs || fn == &kFnTwrGs; }

    const double kProtectionZoneKm = 10.0;

    const FormularFn kFnSimulation = { NULL, TAG_ITEM_TYPE_SIMULATION_INDICATOR,
        NULL, TAG_ITEM_FUNCTION_SIMULATION_POPUP, NULL, TAG_ITEM_FUNCTION_SIMULATION_POPUP };

    bool InSimulatorSession(CPlugIn* plugin)
    {
        const int connection = plugin->GetConnectionType();
        return connection != CONNECTION_TYPE_DIRECT
            && connection != CONNECTION_TYPE_VIA_PROXY;
    }

    FormularFn SimulatorFn(const FormularFn& fn, bool right)
    {
        int item = 0, function = 0;
        if (right && IsCflFn(&fn))
        {
            item = TAG_ITEM_TYPE_TEMP_ALTITUDE;
            function = TAG_ITEM_FUNCTION_TEMP_ALTITUDE_POPUP;
        }
        else if (!right && &fn == &kFnArc)
        {
            item = TAG_ITEM_TYPE_ASSIGNED_RATE;
            function = TAG_ITEM_FUNCTION_ASSIGNED_RATE_POPUP;
        }
        else
        {
            return fn;
        }

        FormularFn sim = fn;
        sim.itemPlugin = NULL;
        sim.itemCode = item;
        if (right)
        {
            sim.rightPlugin = NULL;
            sim.rightFn = function;
        }
        else
        {
            sim.leftPlugin = NULL;
            sim.leftFn = function;
        }
        return sim;
    }

    const char* const kFormularKindNames[] = { "auto", "ctr", "app", "twr" };

    struct SymbolStep
    {
        enum Kind { Move, Line, Pixel, Arc } kind;
        int v[6];
    };
    typedef std::vector<SymbolStep> TrackSymbol;
    typedef std::map<std::string, TrackSymbol> TrackSymbolSet;

#define GALAXY_SSR_SYMBOL \
    "MOVETO:-1:0\nLINETO:0:1\nLINETO:1:0\nLINETO:0:-1\nLINETO:-1:0\n" \
    "MOVETO:-2:0\nLINETO:0:2\nLINETO:2:0\nLINETO:0:-2\nLINETO:-2:0\n" \
    "MOVETO:-3:0\nLINETO:0:3\nLINETO:3:0\nLINETO:0:-3\nLINETO:-3:0\n" \
    "MOVETO:-5:-5\nLINETO:5:-5\nLINETO:5:5\nLINETO:-5:5\nLINETO:-5:-5\n"
#define GALAXY_ADSB_SYMBOL \
    "MOVETO:-6:0\nLINETO:0:6\nLINETO:6:0\nLINETO:0:-6\nLINETO:-6:0\n"
    const char* const kDefaultTrackSymbols =
        "SYMBOL:PRIMARY\nMOVETO:0:-5\nLINETO:0:5\nMOVETO:-5:0\nLINETO:5:0\n"
        "SYMBOL:PRIMARY_DIV\nMOVETO:0:-4\nLINETO:0:5\nMOVETO:-4:0\nLINETO:5:0\n"
        "SYMBOL:DAPS\n" GALAXY_SSR_SYMBOL
        "SYMBOL:DAPS_DIV\n" GALAXY_SSR_SYMBOL
        "SYMBOL:NODAPS\n" GALAXY_SSR_SYMBOL
        "SYMBOL:NODAPS_DIV\n" GALAXY_SSR_SYMBOL
        "SYMBOL:ADSB\n" GALAXY_ADSB_SYMBOL
        "SYMBOL:ADSB_DIV\n" GALAXY_ADSB_SYMBOL
        "SYMBOL:UNCONTROLLED\nMOVETO:0:-5\nLINETO:0:5\nMOVETO:-5:0\nLINETO:5:0\n"
        "MOVETO:-5:-5\nLINETO:5:-5\nLINETO:5:5\nLINETO:-5:5\nLINETO:-5:-5\n"
        "SYMBOL:HISTORY\nMOVETO:-1:-1\nLINETO:-1:0\nLINETO:0:0\nLINETO:0:-1\nLINETO:-1:-1\n"
        "SYMBOL:ASSUMED\nMOVETO:0:-3\nLINETO:0:4\nMOVETO:-3:0\nLINETO:4:0\n"
        "MOVETO:-2:-2\nLINETO:3:3\nMOVETO:-2:2\nLINETO:3:-3\n";
#undef GALAXY_SSR_SYMBOL
#undef GALAXY_ADSB_SYMBOL

    const double kAssumedHoleRadius = 4.0;

    void ParseTrackSymbols(const std::string& text, TrackSymbolSet& out)
    {
        TrackSymbol* current = NULL;
        size_t start = 0;
        while (start < text.size())
        {
            size_t end = text.find('\n', start);
            if (end == std::string::npos)
                end = text.size();
            std::string line = text.substr(start, end - start);
            start = end + 1;

            size_t comment = line.find("//");
            if (comment != std::string::npos)
                line.erase(comment);

            std::vector<std::string> fields(1);
            for (char c : line)
            {
                if (c == ':')
                    fields.push_back(std::string());
                else if (c != ' ' && c != '\t' && c != '\r')
                    fields.back() += c;
            }
            if (fields[0].empty())
                continue;
            for (char& c : fields[0])
                if (c >= 'a' && c <= 'z')
                    c = (char)(c - 'a' + 'A');

            if (fields[0] == "SYMBOL")
            {
                current = NULL;
                if (fields.size() >= 2 && !fields[1].empty())
                {
                    std::string name = fields[1];
                    for (char& c : name)
                        if (c >= 'a' && c <= 'z')
                            c = (char)(c - 'a' + 'A');
                    current = &out[name];
                    current->clear();
                }
                continue;
            }
            if (current == NULL)
                continue;

            std::vector<int> n;
            for (size_t i = 1; i < fields.size(); i++)
                n.push_back(atoi(fields[i].c_str()));

            SymbolStep step = {};
            if ((fields[0] == "MOVETO" || fields[0] == "LINETO" || fields[0] == "SETPIXEL") && n.size() >= 2)
            {
                step.kind = fields[0] == "MOVETO" ? SymbolStep::Move
                          : fields[0] == "LINETO" ? SymbolStep::Line : SymbolStep::Pixel;
                step.v[0] = n[0];
                step.v[1] = n[1];
            }
            else if (fields[0] == "ARC" && n.size() == 5)
            {
                step.kind = SymbolStep::Arc;
                const int v[6] = { n[0], n[1], n[2], n[2], n[3], n[4] };
                memcpy(step.v, v, sizeof(v));
            }
            else if (fields[0] == "ARC" && n.size() >= 6)
            {
                step.kind = SymbolStep::Arc;
                for (int i = 0; i < 6; i++)
                    step.v[i] = n[i];
            }
            else
            {
                continue;
            }
            current->push_back(step);
        }
    }

    bool DrawsSomething(const TrackSymbol& symbol)
    {
        int x = 0, y = 0;
        for (const SymbolStep& s : symbol)
        {
            switch (s.kind)
            {
            case SymbolStep::Pixel:
                return true;
            case SymbolStep::Arc:
                if (s.v[2] > 0 && s.v[3] > 0)
                    return true;
                break;
            case SymbolStep::Line:
                if (s.v[0] != x || s.v[1] != y)
                    return true;
            case SymbolStep::Move:
                x = s.v[0];
                y = s.v[1];
                break;
            }
        }
        return false;
    }

    bool g_trackSymbolsLoaded = false;
    TrackSymbolSet g_trackSymbols;
    std::string g_trackSymbolsSource;
    std::set<std::string> g_trackSymbolsFromFile;

    void ResetTrackSymbols()
    {
        g_trackSymbolsLoaded = false;
        g_trackSymbols.clear();
        g_trackSymbolsSource.clear();
        g_trackSymbolsFromFile.clear();
    }

    const TrackSymbolSet& TrackSymbols()
    {
        if (g_trackSymbolsLoaded)
            return g_trackSymbols;
        g_trackSymbolsLoaded = true;
        ParseTrackSymbols(kDefaultTrackSymbols, g_trackSymbols);
        g_trackSymbolsSource = "TopSky.dll is not loaded - built-in symbols";

        HMODULE topsky = GetModuleHandleW(L"TopSky.dll");
        wchar_t path[MAX_PATH] = {};
        if (topsky == NULL || GetModuleFileNameW(topsky, path, MAX_PATH) == 0)
            return g_trackSymbols;
        std::wstring file = path;
        size_t slash = file.find_last_of(L"\\/");
        file = file.substr(0, slash == std::wstring::npos ? 0 : slash + 1) + L"TopSkySymbols.txt";

        FILE* f = NULL;
        if (_wfopen_s(&f, file.c_str(), L"rb") != 0 || f == NULL)
        {
            g_trackSymbolsSource = Narrow(file) + " cannot be read - built-in symbols";
            Log::Warn("symbols", Log::Utf8(file) + " cannot be read - the built-in track symbols are drawn");
            return g_trackSymbols;
        }
        std::string text;
        char buf[4096];
        size_t got;
        while ((got = fread(buf, 1, sizeof(buf), f)) > 0)
            text.append(buf, got);
        fclose(f);
        g_trackSymbolsSource = Narrow(file);

        TrackSymbolSet fromFile;
        ParseTrackSymbols(text, fromFile);
        for (const auto& s : fromFile)
        {
            if (!DrawsSomething(s.second))
                continue;
            g_trackSymbols[s.first] = s.second;
            g_trackSymbolsFromFile.insert(s.first);
        }
        return g_trackSymbols;
    }

    void DrawSymbolLineOutsideHole(HDC hDC, POINT at, POINT from, POINT to, double holeR)
    {
        const double dx = (double)to.x - from.x, dy = (double)to.y - from.y;
        const double a = dx * dx + dy * dy;

        double t0 = 0.0, t1 = 0.0;
        if (a < 1e-9)
        {
            if ((double)from.x * from.x + (double)from.y * from.y < holeR * holeR)
                return;
        }
        else
        {
            const double b = 2.0 * (from.x * dx + from.y * dy);
            const double c = (double)from.x * from.x + (double)from.y * from.y - holeR * holeR;
            const double disc = b * b - 4.0 * a * c;
            if (disc > 0.0)
            {
                const double root = sqrt(disc);
                t0 = max(0.0, (-b - root) / (2.0 * a));
                t1 = min(1.0, (-b + root) / (2.0 * a));
            }
        }

        if (t0 > 0.0)
        {
            MoveToEx(hDC, at.x + from.x, at.y + from.y, NULL);
            LineTo(hDC, at.x + from.x + (int)lround(dx * t0), at.y + from.y + (int)lround(dy * t0));
        }
        if (t1 < 1.0)
        {
            MoveToEx(hDC, at.x + from.x + (int)lround(dx * t1), at.y + from.y + (int)lround(dy * t1), NULL);
            LineTo(hDC, at.x + to.x, at.y + to.y);
        }
        MoveToEx(hDC, at.x + to.x, at.y + to.y, NULL);
    }

    void DrawTrackSymbol(HDC hDC, const TrackSymbol& symbol, POINT at, COLORREF color,
        double holeRadius = 0.0)
    {
        HPEN pen = CreatePen(PS_SOLID, 1, color);
        HGDIOBJ oldPen = SelectObject(hDC, pen);
        HGDIOBJ oldBrush = SelectObject(hDC, GetStockObject(NULL_BRUSH));
        MoveToEx(hDC, at.x, at.y, NULL);

        POINT cur = { 0, 0 };
        for (const SymbolStep& s : symbol)
        {
            const POINT here = { s.v[0], s.v[1] };
            const int x = at.x + here.x, y = at.y + here.y;
            switch (s.kind)
            {
            case SymbolStep::Move:
                MoveToEx(hDC, x, y, NULL);
                cur = here;
                break;
            case SymbolStep::Line:
                if (holeRadius > 0.0)
                    DrawSymbolLineOutsideHole(hDC, at, cur, here, holeRadius);
                else
                    LineTo(hDC, x, y);
                cur = here;
                break;
            case SymbolStep::Pixel:
                if (holeRadius <= 0.0
                    || (double)here.x * here.x + (double)here.y * here.y >= holeRadius * holeRadius)
                    SetPixel(hDC, x, y, color);
                break;
            case SymbolStep::Arc:
            {
                const int rx = s.v[2], ry = s.v[3];
                if (rx <= 0 || ry <= 0)
                    break;
                if (holeRadius > 0.0
                    && sqrt((double)here.x * here.x + (double)here.y * here.y) + max(rx, ry) <= holeRadius)
                    break;
                const double a0 = s.v[4] * M_PI / 180.0, a1 = s.v[5] * M_PI / 180.0;
                const int oldDir = SetArcDirection(hDC, AD_COUNTERCLOCKWISE);
                ::Arc(hDC, x - rx, y - ry, x + rx + 1, y + ry + 1,
                    x + (int)lround(rx * cos(a0)), y - (int)lround(ry * sin(a0)),
                    x + (int)lround(rx * cos(a1)), y - (int)lround(ry * sin(a1)));
                SetArcDirection(hDC, oldDir);
                break;
            }
            }
        }

        SelectObject(hDC, oldBrush);
        SelectObject(hDC, oldPen);
        DeleteObject(pen);
    }

    char TopSkySpeedModifier(const CFlightPlanControllerAssignedData& assigned)
    {
        const char* annotation = assigned.GetFlightStripAnnotation(7);
        if (annotation == NULL)
            return 0;

        const std::string text(annotation);
        size_t pos = 0;
        while (pos < text.size())
        {
            size_t end = text.find('/', pos);
            const std::string field = text.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
            if (field.size() >= 2 && field[0] == 's' && (field[1] == '+' || field[1] == '-'))
                return field[1];
            if (end == std::string::npos)
                break;
            pos = end + 1;
        }
        return 0;
    }

    bool CalculatedIasMach(int gsKt, int pressureAltFt, int& iasKt, int& machX100)
    {
        if (gsKt < 40)
            return false;

        const double h = (double)max(0, pressureAltFt);
        double T, delta;
        if (h <= 36089.0)
        {
            T = 288.15 - 0.0019812 * h;
            delta = pow(T / 288.15, 5.25588);
        }
        else
        {
            T = 216.65;
            delta = 0.223361 * exp(-(h - 36089.0) / 20805.8);
        }

        const double mach = gsKt / (38.967854 * sqrt(T));
        const double qcOverP0 = delta * (pow(1.0 + 0.2 * mach * mach, 3.5) - 1.0);
        const double cas = 661.4786 * sqrt(5.0 * (pow(qcOverP0 + 1.0, 2.0 / 7.0) - 1.0));

        iasKt = (int)lround(cas);
        machX100 = (int)lround(mach * 100.0);
        return true;
    }
}

HFONT CGalaxyATMSystemRadarScreen::GetFormularFont()
{
    int size = Plugin()->TagFontSize();
    if (m_formularFont != NULL && m_formularFontSize == size)
        return m_formularFont;
    if (m_formularFont != NULL)
        DeleteObject(m_formularFont);

    LOGFONTW lf = {};
    const wchar_t* face = Theme::EuroScopeFace();
    if (face != NULL)
        wcscpy_s(lf.lfFaceName, face);
    else if (m_esFont == NULL || GetObjectW(m_esFont, sizeof(lf), &lf) == 0)
        wcscpy_s(lf.lfFaceName, L"Consolas");
    lf.lfHeight = -MulDiv(size, 7, 6);
    lf.lfWidth = 0;
    lf.lfEscapement = lf.lfOrientation = 0;
    lf.lfWeight = FW_NORMAL;
    lf.lfItalic = lf.lfUnderline = lf.lfStrikeOut = FALSE;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfOutPrecision = OUT_TT_PRECIS;
    lf.lfQuality = CLEARTYPE_QUALITY;

    m_formularFont = CreateFontIndirectW(&lf);
    m_formularFontSize = size;
    return m_formularFont;
}

void CGalaxyATMSystemRadarScreen::DrawFormulars(HDC hDC, bool registerObjects)
{
    for (auto& entry : m_formulars)
        entry.second.items.clear();

    if (!m_formularsVisible)
        return;

    CGalaxyATMSystemPlugin* plugin = Plugin();
    RECT ra = GetRadarArea();

    int saved = SaveDC(hDC);
    SetBkMode(hDC, TRANSPARENT);
    SetTextAlign(hDC, TA_LEFT | TA_TOP);
    SelectObject(hDC, GetFormularFont());

    TEXTMETRICW tm;
    GetTextMetricsW(hDC, &tm);
    const int lineH = max(1, (int)(tm.tmHeight + tm.tmExternalLeading));
    SIZE space = { 0, 0 };
    GetTextExtentPoint32W(hDC, L" ", 1, &space);

    const int tl = plugin->TransitionLevelFL();
    const AltUnit altUnit = plugin->UnitAlt();

    const FormularKind kind = CurrentFormularKind();
    const bool ctrLabel = (kind == FormularKind::Ctr);
    const bool simulator = InSimulatorSession(plugin);
    const FormularFn* const remarkFn = (kind == FormularKind::App) ? &kFnAppRemark : &kFnRemark;

    std::map<std::string, int> codeCount;
    for (CRadarTarget t = plugin->RadarTargetSelectFirst(); t.IsValid();
         t = plugin->RadarTargetSelectNext(t))
    {
        CRadarTargetPositionData p = t.GetPosition();
        const char* c = p.IsValid() ? p.GetSquawk() : NULL;
        if (c == NULL || strlen(c) != 4 || strcmp(c, "0000") == 0 || strcmp(c, "1200") == 0
            || strcmp(c, "2000") == 0 || strcmp(c, "7000") == 0)
            continue;
        codeCount[c]++;
    }

    for (CRadarTarget rt = plugin->RadarTargetSelectFirst(); rt.IsValid();
         rt = plugin->RadarTargetSelectNext(rt))
    {
        CRadarTargetPositionData pos = rt.GetPosition();
        if (!pos.IsValid())
            continue;

        POINT tp = ConvertCoordFromPositionToPixel(pos.GetPosition());
        if (!PtInRect(&ra, tp))
            continue;

        if (!plugin->AltFilterPasses(pos.GetPressureAltitude()))
            continue;
        const ApwResult& apw = plugin->ApwForTarget(rt);

        CFlightPlan fp = rt.GetCorrelatedFlightPlan();
        const char* cs = fp.IsValid() ? fp.GetCallsign() : rt.GetCallsign();
        if (cs == NULL || *cs == '\0')
            continue;
        const std::string callsign = cs;
        const COLORREF base = GetTagColorForFlightPlan(fp);
        const char* sq = pos.GetSquawk();

        const bool correlated = fp.IsValid();
        const bool expanded = !m_formularHover.empty() && m_formularHover == callsign;

        std::vector<FormularRun> warnings;
        if (simulator && correlated)
            warnings.push_back({ fp.GetSimulated() ? L"{*}" : L"{}", base, &kFnSimulation });
        if (correlated && !fp.GetFlightPlanData().IsRvsm())
            warnings.push_back({ L"W", Theme::DistressText, NULL });
        if (sq != NULL)
        {
            auto dup = codeCount.find(sq);
            if (dup != codeCount.end() && dup->second > 1)
                warnings.push_back({ L"D", Theme::DuplicateText, NULL });
        }
        if (correlated && sq != NULL && *sq != '\0')
        {
            std::string assigned = plugin->AssignedSquawkFor(fp);
            if (!assigned.empty() && assigned != sq)
                warnings.push_back({ L"A" + Widen(sq), Theme::SquawkMismatch,
                    ctrLabel ? &kFnSquawkWarning : &kFnAppSquawkWarning });
        }
        if (correlated)
        {
            char com = fp.GetControllerAssignedData().GetCommunicationType();
            if (com == 0 || com == ' ' || com == '?')
                com = fp.GetFlightPlanData().GetCommunicationType();
            com = (char)tolower((unsigned char)com);
            if (com == 't' || com == 'r')
                warnings.push_back({ std::wstring(1, (wchar_t)com), base, &kFnCommunication });
            if (fp.GetRAMFlag() || RouteAdherenceAlert(fp, rt))
                warnings.push_back({ L"RAM", Theme::DuplicateText, NULL });
            if (fp.GetCLAMFlag())
                warnings.push_back({ L"CLAM", Theme::DuplicateText, NULL });
        }
        if (apw.level != ApwLevel::None)
        {
            std::wstring text = L"APW";
            if (plugin->GetConfig().Apw().showZone && !apw.zoneId.empty())
                text += L" " + apw.zoneId;
            warnings.push_back({ text,
                apw.level == ApwLevel::Inside ? Theme::ApwInside : Theme::ApwPredicted, NULL });
        }
        if (sq != NULL && strcmp(sq, "7700") == 0)
            warnings.push_back({ L"EM", Theme::DistressText, NULL });
        else if (sq != NULL && strcmp(sq, "7600") == 0)
            warnings.push_back({ L"RDO", Theme::DistressText, NULL });
        else if (sq != NULL && strcmp(sq, "7500") == 0)
            warnings.push_back({ L"HIJ", Theme::DistressText, NULL });
        if (correlated)
        {
            const char* remark = fp.GetControllerAssignedData().GetScratchPadString();
            if (remark != NULL && *remark != '\0')
            {
                std::wstring text = Widen(remark);
                std::wstring upper = text;
                CharUpperBuffW(&upper[0], (DWORD)upper.size());
                bool missedApproach = false;
                size_t at;
                while ((at = upper.find(L"MISAP")) != std::wstring::npos)
                {
                    text.erase(at, 5);
                    upper.erase(at, 5);
                    missedApproach = true;
                }
                if (missedApproach)
                {
                    std::wstring rest;
                    for (wchar_t c : text)
                        if (c != L'_')
                            rest += c;
                    size_t first = rest.find_first_not_of(L" \t");
                    size_t last = rest.find_last_not_of(L" \t");
                    text = (first == std::wstring::npos) ? std::wstring() : rest.substr(first, last - first + 1);
                    warnings.push_back({ L"MAPP", Theme::FormularMapp, remarkFn });
                }
                if (!text.empty())
                    warnings.push_back({ text, base, remarkFn });
            }
        }

        const char* planType = correlated ? fp.GetFlightPlanData().GetPlanType() : NULL;
        const bool vfr = planType != NULL && (planType[0] == 'V' || planType[0] == 'v');
        if (kind == FormularKind::Twr && vfr)
            warnings.push_back({ L"V", Theme::FormularVfr, NULL });

        std::vector<FormularRun> ident;
        auto highlight = m_formulars.find(callsign);
        const bool highlighted = highlight != m_formulars.end() && highlight->second.highlighted;
        ident.push_back({ Widen(callsign.c_str()), highlighted ? Theme::FormularHighlight : base,
            correlated ? &kFnCallsign : NULL });
        if (correlated)
        {
            if (!ctrLabel)
            {
                const char wtc = fp.GetFlightPlanData().GetAircraftWtc();
                if (wtc == 'H' || wtc == 'J')
                    ident.push_back({ std::wstring(1, (wchar_t)wtc), Theme::FormularWtc, NULL });
            }

            std::string si;
            const char* current = fp.GetTrackingControllerId();
            if (current != NULL)
                si = current;
            const char* next = fp.GetCoordinatedNextController();
            if (next != NULL && *next != '\0')
            {
                CController nextController = plugin->ControllerSelect(next);
                const char* nextId = nextController.IsValid() ? nextController.GetPositionId()
                    : (strlen(next) <= 3 ? next : NULL);
                if (nextId != NULL && si != nextId)
                    si += nextId;
            }
            if (!si.empty())
                ident.push_back({ Widen(si.c_str()),
                    kind == FormularKind::Twr ? base : Theme::FormularSector,
                    kind == FormularKind::Twr ? &kFnTwrSector : &kFnSector });

            if (vfr && ctrLabel)
                ident.push_back({ L"V", base, NULL });
            else if (vfr && kind == FormularKind::App && expanded)
                ident.push_back({ L"V", Theme::FormularVfr, NULL });
        }
        if (ctrLabel && expanded && sq != NULL && *sq != '\0')
            ident.push_back({ Widen(sq), base, correlated ? &kFnTssr : NULL });

        std::vector<FormularRun> levels;
        const bool belowTL = pos.GetFlightLevel() / 100 < tl;
        const int altFt = belowTL ? pos.GetPressureAltitude() : pos.GetFlightLevel();
        levels.push_back({ Widen(FormatAltitudeUnit(altFt, altUnit).c_str()),
            base, correlated ? (ctrLabel ? &kFnAfl : &kFnAppAfl) : NULL });
        const int vs = rt.GetVerticalSpeed();
        std::wstring cflText;
        COLORREF cflColor = base;
        int clearedFt = 0;
        bool clearedApproach = false;
        if (correlated)
        {
            int cfl = fp.GetControllerAssignedData().GetClearedAltitude();
            if (cfl == 1)
            {
                cflText = L"CA";
                clearedApproach = true;
            }
            else if (cfl == 2)
            {
                cflText = L"VA";
                clearedApproach = true;
            }
            else if (cfl > 2)
            {
                cflText = Widen(FormatAltitudeUnit(cfl, altUnit).c_str());
                clearedFt = cfl;
                const bool level = vs > -100 && vs < 100;
                if ((level && abs(altFt - cfl) > 200)
                    || (vs >= 100 && altFt > cfl + 200) || (vs <= -100 && altFt < cfl - 200))
                    cflColor = Theme::DuplicateText;
            }
            if (cflText.empty())
            {
                const int rfl = fp.GetFlightPlanData().GetFinalAltitude();
                cflText = Widen(FormatAltitudeUnit(rfl > 0 ? rfl : altFt, altUnit).c_str());
            }
        }

        const int kClearedBandFt = 200;
        const wchar_t* trend = NULL;
        if (clearedApproach)
            trend = L"\x2193";
        else if (clearedFt > 0)
            trend = (clearedFt > altFt + kClearedBandFt) ? L"\x2191"
                  : (clearedFt < altFt - kClearedBandFt) ? L"\x2193" : NULL;
        else
            trend = (vs >= 100) ? L"\x2191" : (vs <= -100) ? L"\x2193" : NULL;
        if (trend != NULL)
            levels.push_back({ trend, base, NULL });

        if (correlated)
            levels.push_back({ cflText, cflColor, ctrLabel ? &kFnCfl : &kFnAppCfl });
        if (m_osSpeed && ctrLabel)
            levels.push_back({ Widen(FormatGroundSpeedUnit(rt.GetGS(), plugin->UnitGs()).c_str()),
                base, correlated ? &kFnGs : NULL });

        std::wstring ahdgText, aspText, arcText;
        if (correlated)
        {
            CFlightPlanControllerAssignedData assigned = fp.GetControllerAssignedData();
            wchar_t t[16];
            if (assigned.GetAssignedHeading() > 0)
            {
                swprintf_s(t, L"H%03d", assigned.GetAssignedHeading());
                ahdgText = t;
            }
            else
            {
                const char* direct = assigned.GetDirectToPointName();
                if (direct != NULL && *direct != '\0')
                    ahdgText = Widen(direct);
            }
            if (assigned.GetAssignedMach() > 0)
            {
                swprintf_s(t, L"M%03d", assigned.GetAssignedMach());
                aspText = t;
            }
            else if (assigned.GetAssignedSpeed() > 0)
            {
                swprintf_s(t, L"N%03d", assigned.GetAssignedSpeed());
                aspText = t;
            }
            if (!aspText.empty())
            {
                char modifier = TopSkySpeedModifier(assigned);
                if (modifier != 0)
                    aspText += (wchar_t)modifier;
            }
            if (assigned.GetAssignedRate() != 0)
            {
                swprintf_s(t, L"R%d", assigned.GetAssignedRate());
                arcText = t;
            }
        }

        std::vector<std::vector<FormularRun>> extra;
        if (expanded && correlated && ctrLabel)
        {
            CFlightPlanControllerAssignedData cad = fp.GetControllerAssignedData();
            CFlightPlanData fpd = fp.GetFlightPlanData();
            wchar_t buf[32];

            std::vector<FormularRun> exitLine;
            int xfl = fp.GetExitCoordinationAltitude();
            exitLine.push_back({ xfl > 0 ? Widen(FormatAltitudeUnit(xfl, altUnit).c_str())
                                         : std::wstring(L"XFL"), base, &kFnXfl });
            const char* copx = fp.GetExitCoordinationPointName();
            exitLine.push_back({ (copx != NULL && *copx != '\0') ? Widen(copx) : std::wstring(L"COPX"),
                base, &kFnCopx });
            extra.push_back(exitLine);

            std::vector<FormularRun> assignedLine;
            assignedLine.push_back({ ahdgText.empty() ? std::wstring(L"AHDG") : ahdgText, base, &kFnAhdg });
            assignedLine.push_back({ aspText.empty() ? std::wstring(L"ASP") : aspText, base, &kFnAsp });
            assignedLine.push_back({ arcText.empty() ? std::wstring(L"ARC") : arcText, base, &kFnArc });
            const char* firExit = fp.GetNextFirCopxPointName();
            if (firExit != NULL && *firExit != '\0')
                assignedLine.push_back({ Widen(firExit), base, NULL });
            extra.push_back(assignedLine);

            extra.push_back(std::vector<FormularRun>());

            std::vector<FormularRun> planLine;
            const char* atyp = fpd.GetAircraftFPType();
            std::wstring typeText = (atyp != NULL && *atyp != '\0') ? Widen(atyp) : std::wstring(L"ATYP");
            char wtc = fpd.GetAircraftWtc();
            if (wtc != 0 && wtc != ' ' && wtc != '?')
                typeText += L"/" + std::wstring(1, (wchar_t)wtc);
            planLine.push_back({ typeText, base, &kFnAtyp });
            const char* ades = fpd.GetDestination();
            if (ades != NULL && *ades != '\0')
                planLine.push_back({ Widen(ades), base, &kFnAdes });
            int rfl = fpd.GetFinalAltitude();
            if (rfl > 0)
            {
                swprintf_s(buf, L"%03d", rfl / 100);
                planLine.push_back({ buf, base, &kFnRfl });
            }
            extra.push_back(planLine);

            int ias = 0, machX100 = 0;
            if (CalculatedIasMach(rt.GetGS(), pos.GetFlightLevel(), ias, machX100))
            {
                std::vector<FormularRun> speedLine;
                swprintf_s(buf, L"N%03d", ias);
                speedLine.push_back({ buf, base, NULL });
                swprintf_s(buf, L"M%02d", machX100);
                speedLine.push_back({ buf, base, NULL });
                extra.push_back(speedLine);
            }
        }
        else if (correlated && ctrLabel)
        {
            std::vector<FormularRun> assignedLine;
            if (!ahdgText.empty())
                assignedLine.push_back({ ahdgText, base, &kFnAhdg });
            if (!aspText.empty())
                assignedLine.push_back({ aspText, base, &kFnAsp });
            if (!arcText.empty())
                assignedLine.push_back({ arcText, base, &kFnArc });
            if (!assignedLine.empty())
                extra.push_back(assignedLine);
        }
        else if (correlated)
        {
            CFlightPlanControllerAssignedData cad = fp.GetControllerAssignedData();
            CFlightPlanData fpd = fp.GetFlightPlanData();
            const bool app = (kind == FormularKind::App);
            wchar_t buf[32];

            std::wstring speed;
            if (cad.GetAssignedMach() > 0)
            {
                swprintf_s(buf, L"M%02d", cad.GetAssignedMach());
                speed = buf;
            }
            else if (cad.GetAssignedSpeed() > 0)
            {
                if (app)
                    swprintf_s(buf, L"%02d", cad.GetAssignedSpeed() / 10);
                else
                    swprintf_s(buf, L"%03d", cad.GetAssignedSpeed());
                speed = buf;
            }
            if (!speed.empty())
            {
                char modifier = TopSkySpeedModifier(cad);
                if (modifier != 0)
                    speed += (wchar_t)modifier;
            }

            const FormularRun gsRun = { Widen(FormatGroundSpeedUnit(rt.GetGS(), plugin->UnitGs()).c_str()),
                base, app ? &kFnGs : &kFnTwrGs };
            const char* atyp = fpd.GetAircraftFPType();
            const FormularRun typeRun = { (atyp != NULL && *atyp != '\0') ? Widen(atyp) : std::wstring(L"ATYP"),
                base, &kFnAppAtyp };
            const FormularFn* const ahdgFn = app ? &kFnAppAhdg : &kFnAhdg;

            if (!expanded)
            {
                std::vector<FormularRun> speedLine;
                if (m_osSpeed)
                    speedLine.push_back(gsRun);
                if (!speed.empty())
                    speedLine.push_back({ speed, base, &kFnAsp });
                speedLine.push_back(typeRun);
                extra.push_back(speedLine);

                if (!ahdgText.empty())
                    extra.push_back(std::vector<FormularRun>(1, FormularRun{ ahdgText, base, ahdgFn }));
            }
            else
            {
                std::vector<FormularRun> speedLine;
                if (m_osSpeed)
                    speedLine.push_back(gsRun);
                speedLine.push_back({ speed.empty() ? std::wstring(L"ASP") : speed, base, &kFnAsp });
                extra.push_back(speedLine);

                if (app)
                {
                    std::vector<FormularRun> exitLine;
                    int xfl = fp.GetExitCoordinationAltitude();
                    exitLine.push_back({ xfl > 0 ? Widen(FormatAltitudeUnit(xfl, altUnit).c_str())
                                                 : std::wstring(L"XFL"), base, &kFnXfl });
                    const char* copx = fp.GetExitCoordinationPointName();
                    exitLine.push_back({ (copx != NULL && *copx != '\0') ? Widen(copx) : std::wstring(L"COPX"),
                        base, &kFnCopx });
                    int ias = 0, machX100 = 0;
                    if (CalculatedIasMach(rt.GetGS(), pos.GetFlightLevel(), ias, machX100))
                    {
                        swprintf_s(buf, L"N%03d", ias);
                        exitLine.push_back({ buf, base, NULL });
                    }
                    extra.push_back(exitLine);
                }

                std::vector<FormularRun> planLine;
                planLine.push_back(typeRun);
                const char* ades = fpd.GetDestination();
                if (ades != NULL && *ades != '\0')
                    planLine.push_back({ Widen(ades), base, &kFnAdes });
                const char* arwy = fpd.GetArrivalRwy();
                if (arwy != NULL && *arwy != '\0')
                    planLine.push_back({ Widen(arwy), Theme::FormularGreen, &kFnArwy });
                else
                    planLine.push_back({ L"ARWY", base, &kFnArwy });
                extra.push_back(planLine);

                extra.push_back(std::vector<FormularRun>(1,
                    FormularRun{ ahdgText.empty() ? std::wstring(L"AHDG") : ahdgText, base, ahdgFn }));
            }

            const char* ctl = cad.GetFlightStripAnnotation(3);
            if (ctl != NULL && strcmp(ctl, "CTL") == 0 && fp.GetTrackingControllerIsMe())
                extra.push_back(std::vector<FormularRun>(1, FormularRun{ L"CTL", Theme::FormularGreen, NULL }));
        }

        std::vector<std::vector<FormularRun>> lines;
        if (m_osLines == 3)
        {
            if (!warnings.empty())
                lines.push_back(warnings);
            lines.push_back(ident);
        }
        else
        {
            ident.insert(ident.end(), warnings.begin(), warnings.end());
            lines.push_back(ident);
        }
        lines.push_back(levels);
        lines.insert(lines.end(), extra.begin(), extra.end());

        int width = 0;
        std::vector<std::vector<int>> runWidths(lines.size());
        std::vector<int> lineWidths(lines.size(), 0);
        for (size_t l = 0; l < lines.size(); l++)
        {
            int w = 0;
            for (size_t r = 0; r < lines[l].size(); r++)
            {
                SIZE sz = { 0, 0 };
                GetTextExtentPoint32W(hDC, lines[l][r].text.c_str(), (int)lines[l][r].text.size(), &sz);
                runWidths[l].push_back(sz.cx);
                w += sz.cx + (r > 0 ? space.cx : 0);
            }
            lineWidths[l] = w;
            width = max(width, w);
        }
        const int height = (int)lines.size() * lineH;

        const size_t identLine = (m_osLines == 3 && !warnings.empty()) ? 1 : 0;

        FormularState& state = m_formulars[callsign];
        if (!state.placed)
        {
            state.offset = { 16, -14 };
            state.placed = true;
        }
        state.anchor = tp;
        POINT callsignAt = { tp.x + state.offset.x, tp.y + state.offset.y };
        state.callsignAt = callsignAt;

        RECT area;
        area.left = callsignAt.x;
        area.top = callsignAt.y - (int)identLine * lineH - lineH / 2;
        area.right = area.left + width;
        area.bottom = area.top + height;
        state.area = area;

        std::vector<RECT> rows;
        for (size_t l = 0; l < lines.size(); l++)
        {
            RECT row = { area.left, area.top + (int)l * lineH,
                         area.left + lineWidths[l], area.top + (int)(l + 1) * lineH };
            InflateRect(&row, 3, 1);
            rows.push_back(row);
        }

        const POINT aim = { area.left + lineWidths[identLine] / 2, callsignAt.y };
        POINT leaderFrom, leaderTo;
        if (ClipLeaderToText(tp, aim, rows, 6.0, leaderFrom, leaderTo))
        {
            VectorCanvas canvas(hDC, base, 1.0f);
            canvas.Line(leaderFrom.x, leaderFrom.y, leaderTo.x, leaderTo.y);
        }

        RECT ahdgRect = { 0, 0, 0, 0 };
        bool haveAhdg = false;
        for (size_t l = 0; l < lines.size(); l++)
        {
            int x = area.left;
            int y = area.top + (int)l * lineH;
            for (size_t r = 0; r < lines[l].size(); r++)
            {
                const FormularRun& run = lines[l][r];
                SetTextColor(hDC, run.color);
                TextOutW(hDC, x, y, run.text.c_str(), (int)run.text.size());

                FormularItem item;
                item.rect = { x, y, x + runWidths[l][r], y + lineH };
                item.fn = run.fn;
                item.text = Narrow(run.text);
                state.items.push_back(item);
                if (IsAhdgFn(run.fn))
                {
                    ahdgRect = item.rect;
                    haveAhdg = true;
                }

                x += runWidths[l][r] + space.cx;
            }
        }

        if (registerObjects)
        {
            RECT hit = area;
            InflateRect(&hit, 2, 1);
            AddScreenObject(SO_FORMULAR, callsign.c_str(), hit, true, "");

            if (haveAhdg)
                AddScreenObject(SO_FORMULAR_AHDG, callsign.c_str(), ahdgRect, true,
                    Tr("Тянуть ЛКМ - курс, ПКМ - меню курса TopSky"));
        }
    }

    if (m_hdgDragging && m_hdgDragMoved)
    {
        POINT from = { 0, 0 };
        double distNm = 0.0, drawnHdg = 0.0;
        int hdg = DragHeading(m_hdgDragCallsign.c_str(), m_hdgDragPt, &from, &distNm, &drawnHdg);
        if (hdg > 0)
        {
            std::vector<POINT> path;
            HeadingTurnPath(plugin->RadarTargetSelect(m_hdgDragCallsign.c_str()),
                drawnHdg, distNm, path);
            {
                VectorCanvas canvas(hDC, Theme::HeadingDragLine);
                const Gdiplus::REAL w = max((Gdiplus::REAL)0.5f, canvas.pen.GetWidth());
                const Gdiplus::REAL dashes[] = { 14.0f / w, 6.0f / w };
                canvas.pen.SetDashPattern(dashes, 2);
                if (path.size() >= 2)
                    canvas.Polyline(path);
                else
                    canvas.Line(from.x, from.y, m_hdgDragPt.x, m_hdgDragPt.y);
            }

            const double dist = (plugin->UnitDist() == DistUnit::Km) ? distNm * 1.852 : distNm;
            wchar_t text[32];
            swprintf_s(text, L"%.1f/%03d", dist, hdg);

            const POINT tip = path.size() >= 2 ? path.back() : m_hdgDragPt;

            SIZE sz = { 0, 0 };
            GetTextExtentPoint32W(hDC, text, (int)wcslen(text), &sz);
            SetTextColor(hDC, Theme::HeadingDragText);
            TextOutW(hDC, tip.x - 2, tip.y - sz.cy - 2, text, (int)wcslen(text));
        }
    }

    RestoreDC(hDC, saved);
}

CGalaxyATMSystemRadarScreen::FormularKind CGalaxyATMSystemRadarScreen::CurrentFormularKind()
{
    switch (m_formularKindSetting)
    {
    case FormularKindSetting::Ctr: return FormularKind::Ctr;
    case FormularKindSetting::App: return FormularKind::App;
    case FormularKindSetting::Twr: return FormularKind::Twr;
    default: break;
    }

    CController me = GetPlugIn()->ControllerMyself();
    if (me.IsValid() && me.IsController())
    {
        const int facility = me.GetFacility();
        if (facility == 5)
            return FormularKind::App;
        if (facility >= 2 && facility <= 4)
            return FormularKind::Twr;
    }
    return FormularKind::Ctr;
}

void CGalaxyATMSystemRadarScreen::HeadingTurnPath(CRadarTarget rt, double headingDeg,
    double totalNm, std::vector<POINT>& out)
{
    out.clear();
    if (!rt.IsValid() || !rt.GetPosition().IsValid())
        return;

    const CPosition start = rt.GetPosition().GetPosition();
    out.push_back(ConvertCoordFromPositionToPixel(start));

    if (rt.GetGS() < 30)
    {
        out.push_back(ConvertCoordFromPositionToPixel(
            CalculateDestinationPoint(start, headingDeg, max(0.1, totalNm))));
        return;
    }

    const double kRoundPx = 15.0;
    const POINT atStart = out.front();
    const POINT aMileOff = ConvertCoordFromPositionToPixel(
        CalculateDestinationPoint(start, 90.0, 1.0));
    const double dxPx = (double)aMileOff.x - atStart.x, dyPx = (double)aMileOff.y - atStart.y;
    const double pxPerNm = sqrt(dxPx * dxPx + dyPx * dyPx);
    const double radiusNm = max(0.02, min(pxPerNm > 0.01 ? kRoundPx / pxPerNm : 0.5,
        totalNm / 4.0));
    const double track = rt.GetTrackHeading();
    const double delta = fmod(headingDeg - track + 540.0, 360.0) - 180.0;
    const double dir = (delta >= 0.0) ? 1.0 : -1.0;
    const double turn = fabs(delta);

    const CPosition centre = CalculateDestinationPoint(start, track + dir * 90.0, radiusNm);
    const double fromCentre = track - dir * 90.0;

    const double kStepDeg = 5.0;
    for (double a = kStepDeg; a < turn; a += kStepDeg)
        out.push_back(ConvertCoordFromPositionToPixel(
            CalculateDestinationPoint(centre, fromCentre + dir * a, radiusNm)));

    const CPosition rollOut = CalculateDestinationPoint(centre, fromCentre + dir * turn, radiusNm);
    out.push_back(ConvertCoordFromPositionToPixel(rollOut));

    const double straightNm = totalNm - radiusNm * turn * M_PI / 180.0;
    if (straightNm > 0.05)
        out.push_back(ConvertCoordFromPositionToPixel(
            CalculateDestinationPoint(rollOut, headingDeg, straightNm)));
}

void CGalaxyATMSystemRadarScreen::DrawTargetSymbols(HDC hDC)
{
    const TrackSymbolSet& symbols = TrackSymbols();
    m_symbolStats = SymbolStats();
    if (symbols.empty())
        return;

    CGalaxyATMSystemPlugin* plugin = Plugin();
    RECT ra = GetRadarArea();

    int saved = SaveDC(hDC);
    for (CRadarTarget rt = plugin->RadarTargetSelectFirst(); rt.IsValid();
         rt = plugin->RadarTargetSelectNext(rt))
    {
        m_symbolStats.targets++;
        CRadarTargetPositionData pos = rt.GetPosition();
        if (!pos.IsValid())
            continue;
        POINT tp = ConvertCoordFromPositionToPixel(pos.GetPosition());
        if (!PtInRect(&ra, tp))
        {
            m_symbolStats.offRadar++;
            continue;
        }
        if (!plugin->AltFilterPasses(pos.GetPressureAltitude()))
        {
            m_symbolStats.filtered++;
            continue;
        }

        CFlightPlan fp = rt.GetCorrelatedFlightPlan();
        const char* plan = fp.IsValid() ? fp.GetFlightPlanData().GetPlanType() : NULL;
        const bool vfr = plan != NULL && (plan[0] == 'V' || plan[0] == 'v');

        std::string name;
        if (vfr && fp.GetState() != FLIGHT_PLAN_STATE_ASSUMED)
        {
            name = "UNCONTROLLED";
        }
        else
        {
            const int flags = pos.GetRadarFlags();
            if (flags & RADAR_POSITION_SECONDARY_S)
                name = "DAPS";
            else if (flags & RADAR_POSITION_SECONDARY_C)
                name = "NODAPS";
            else if (flags & RADAR_POSITION_PRIMARY)
                name = "PRIMARY";
            else
                name = "NODAPS";

            const bool diverging = fp.IsValid()
                && (fp.GetRAMFlag() || fp.GetCLAMFlag() || RouteAdherenceAlert(fp, rt));
            if (pos.GetTransponderI() && symbols.count(name + "_SPI"))
                name += "_SPI";
            else if (diverging && symbols.count(name + "_DIV"))
                name += "_DIV";
        }
        if (pos.GetReceivedTime() > 30 && symbols.count("COASTED"))
            name = "COASTED";

        auto symbol = symbols.find(name);
        if (symbol == symbols.end())
        {
            m_symbolStats.noSymbol++;
            continue;
        }

        const COLORREF color = GetTagColorForFlightPlan(fp);

        const char* cs = fp.IsValid() ? fp.GetCallsign() : rt.GetCallsign();
        auto state = (cs != NULL) ? m_formulars.find(cs) : m_formulars.end();
        if (state != m_formulars.end() && state->second.zone)
            DrawProtectionZone(hDC, pos.GetPosition(), tp, color);

        auto history = symbols.find("HISTORY");
        if (history != symbols.end())
        {
            CRadarTargetPositionData earlier = pos;
            for (int i = 0; i < Theme::TrackHistoryDots; i++)
            {
                earlier = rt.GetPreviousPosition(earlier);
                if (!earlier.IsValid())
                    break;
                POINT hp = ConvertCoordFromPositionToPixel(earlier.GetPosition());
                if (PtInRect(&ra, hp))
                    DrawTrackSymbol(hDC, history->second, hp, color);
            }
        }

        auto star = (fp.IsValid() && fp.GetState() == FLIGHT_PLAN_STATE_ASSUMED)
            ? symbols.find("ASSUMED") : symbols.end();
        const bool assumed = star != symbols.end();

        DrawTrackSymbol(hDC, symbol->second, tp, color, assumed ? kAssumedHoleRadius : 0.0);
        if (assumed)
            DrawTrackSymbol(hDC, star->second, tp, color);
        m_symbolStats.drawn++;
    }
    RestoreDC(hDC, saved);
}

void CGalaxyATMSystemRadarScreen::DrawProtectionZone(HDC hDC, CPosition center, POINT tp,
    COLORREF color)
{
    const double nm = kProtectionZoneKm / 1.852;
    POINT edge = ConvertCoordFromPositionToPixel(CalculateDestinationPoint(center, 90.0, nm));
    const double dx = edge.x - tp.x, dy = edge.y - tp.y;
    const double r = sqrt(dx * dx + dy * dy);
    if (r < 2.0)
        return;

    VectorCanvas canvas(hDC, color);
    canvas.Circle(tp.x, tp.y, r);
}

int CGalaxyATMSystemRadarScreen::DragHeading(const char* sCallsign, POINT cursor,
    POINT* from, double* distNm, double* drawnHdg)
{
    CRadarTarget rt = GetPlugIn()->RadarTargetSelect(sCallsign);
    if (!rt.IsValid() || !rt.GetPosition().IsValid())
        return 0;

    const CPosition start = rt.GetPosition().GetPosition();
    POINT tp = ConvertCoordFromPositionToPixel(start);
    if (abs(cursor.x - tp.x) < 3 && abs(cursor.y - tp.y) < 3)
        return 0;
    const CPosition target = ConvertCoordFromPixelToPosition(cursor);

    const double raw = start.DirectionTo(target);
    int hdg = (int)lround(raw / 5.0) * 5 % 360;
    if (hdg <= 0)
        hdg += 360;

    if (from != NULL)
        *from = tp;
    if (distNm != NULL)
        *distNm = start.DistanceTo(target);
    if (drawnHdg != NULL)
        *drawnHdg = GeoBearingDeg(start, target) + (hdg - raw);
    return hdg;
}

void CGalaxyATMSystemRadarScreen::FormularClick(const char* sCallsign, POINT pt, int button)
{
    if (GetTickCount64() - m_hdgDragEndTick < 500)
        return;

    auto it = m_formulars.find(sCallsign);
    if (it == m_formulars.end())
        return;

    if (button == BUTTON_MIDDLE)
    {
        it->second.highlighted = !it->second.highlighted;
        RequestRefresh();
        return;
    }

    CFlightPlan fp = GetPlugIn()->FlightPlanSelect(sCallsign);
    if (fp.IsValid())
        GetPlugIn()->SetASELAircraft(fp);

    const FormularItem* hit = NULL;
    for (const FormularItem& item : it->second.items)
    {
        if (PtInRect(&item.rect, pt))
        {
            hit = &item;
            break;
        }
    }

    if (hit != NULL && IsGsFn(hit->fn) && button == BUTTON_LEFT)
    {
        it->second.zone = !it->second.zone;
        RequestRefresh();
        return;
    }

    if (hit != NULL && fp.IsValid() && IsCflFn(hit->fn) && button == BUTTON_RIGHT)
    {
        int cfl = fp.GetControllerAssignedData().GetClearedAltitude();
        if (cfl == 1 || cfl == 2)
        {
            fp.GetControllerAssignedData().SetClearedAltitude(0);
            RequestRefresh();
            return;
        }
    }

    if (hit != NULL && hit->fn != NULL && fp.IsValid())
    {
        const bool right = (button == BUTTON_RIGHT);
        const FormularFn f = InSimulatorSession(GetPlugIn()) ? SimulatorFn(*hit->fn, right) : *hit->fn;
        const int id = right ? f.rightFn : f.leftFn;
        if (id != 0)
            StartTagFunction(sCallsign, f.itemPlugin, f.itemCode, hit->text.c_str(),
                right ? f.rightPlugin : f.leftPlugin, id, pt, hit->rect);
    }
    RequestRefresh();
}

bool CGalaxyATMSystemRadarScreen::SigmetOutline(
    const std::vector<EuroScopePlugIn::CPosition>& ring, std::vector<POINT>& out)
{
    out.clear();
    if (ring.size() < 2)
        return false;

    out.reserve(ring.size());
    RECT bbox = { LONG_MAX, LONG_MAX, LONG_MIN, LONG_MIN };
    for (const EuroScopePlugIn::CPosition& p : ring)
    {
        POINT px = ConvertCoordFromPositionToPixel(p);
        out.push_back(px);
        bbox.left   = min(bbox.left, px.x);
        bbox.top    = min(bbox.top, px.y);
        bbox.right  = max(bbox.right, px.x);
        bbox.bottom = max(bbox.bottom, px.y);
    }

    InflateRect(&bbox, 1, 1);

    RECT ra = GetRadarArea();
    RECT unused;
    if (!IntersectRect(&unused, &bbox, &ra))
    {
        out.clear();
        return false;
    }
    return true;
}

void CGalaxyATMSystemRadarScreen::DrawSigmets(HDC hDC)
{
    if (!m_sigmetsVisible || !m_sigmets || m_sigmets->empty())
        return;

    int saved = SaveDC(hDC);

    {
        AreaCanvas canvas(hDC, GetRadarArea(), Theme::SigmetLine,
            (float)Theme::SigmetWidth);
        for (const Sigmet& sig : *m_sigmets)
        {
            std::vector<POINT> pts;
            for (const std::vector<EuroScopePlugIn::CPosition>& ring : sig.rings)
            {
                if (SigmetOutline(ring, pts))
                    canvas.Ring(pts, sig.closed);
            }
        }
    }

    RestoreDC(hDC, saved);
}

void CGalaxyATMSystemRadarScreen::RegisterSigmetObjects()
{
    if (!m_sigmetsVisible || !m_sigmets || m_sigmets->empty())
        return;

    RECT ra = GetRadarArea();
    const int kPad = 12;
    const double kStepPx = 24.0;

    const int kMaxBoxes = 3000;
    int boxes = 0;

    std::vector<POINT> pts;
    for (size_t i = 0; i < m_sigmets->size() && boxes < kMaxBoxes; i++)
    {
        const Sigmet& sig = (*m_sigmets)[i];

        char id[16];
        sprintf_s(id, "%zu", i);
        std::string tip = Narrow(sig.Title().substr(0, 120));

        for (const std::vector<EuroScopePlugIn::CPosition>& ring : sig.rings)
        {
            if (!SigmetOutline(ring, pts))
                continue;

            size_t segments = sig.closed ? pts.size() : pts.size() - 1;
            for (size_t seg = 0; seg < segments && boxes < kMaxBoxes; seg++)
            {
                POINT a = pts[seg], b = pts[(seg + 1) % pts.size()];
                if (!Geom::ClipSegment(ra, a, b))
                    continue;

                double len = sqrt((double)(b.x - a.x) * (b.x - a.x) + (double)(b.y - a.y) * (b.y - a.y));
                int steps = max(1, (int)lround(len / kStepPx));

                for (int st = 0; st <= steps && boxes < kMaxBoxes; st++)
                {
                    double t = (double)st / steps;
                    POINT p;
                    p.x = a.x + (LONG)lround((b.x - a.x) * t);
                    p.y = a.y + (LONG)lround((b.y - a.y) * t);
                    RECT box = { p.x - kPad, p.y - kPad, p.x + kPad, p.y + kPad };
                    AddScreenObject(SO_SIGMET_AREA, id, box, false, tip.c_str());
                    boxes++;
                }
            }
        }
    }
}

int CGalaxyATMSystemRadarScreen::FindSigmetAt(POINT pt)
{
    if (!m_sigmets)
        return -1;

    int best = -1;
    double bestDist = 12.0;
    std::vector<POINT> pts;

    for (size_t i = 0; i < m_sigmets->size(); i++)
    {
        const Sigmet& sig = (*m_sigmets)[i];
        for (const std::vector<EuroScopePlugIn::CPosition>& ring : sig.rings)
        {
            if (!SigmetOutline(ring, pts))
                continue;

            if (sig.closed && Geom::PointInPolygon(pts, pt))
                return (int)i;

            size_t segments = sig.closed ? pts.size() : pts.size() - 1;
            for (size_t seg = 0; seg < segments; seg++)
            {
                double d = Geom::DistanceToSegment(pts[seg], pts[(seg + 1) % pts.size()], pt);
                if (d < bestDist)
                {
                    bestDist = d;
                    best = (int)i;
                }
            }
        }
    }
    return best;
}

bool CGalaxyATMSystemRadarScreen::ZoneOutline(const Zone& zone, std::vector<POINT>& out)
{
    return SigmetOutline(zone.ring, out);
}

void CGalaxyATMSystemRadarScreen::UpdateZoneActivity()
{
    const Config& cfg = Plugin()->GetConfig();
    const std::vector<Zone>& zones = cfg.Zones();

    m_aup = Plugin()->AupBookings();
    m_notams = Plugin()->Notams();
    m_zoneActive.assign(zones.size(), 0);
    m_zoneBooking.assign(zones.size(), NULL);

    if (zones.empty())
        return;

    static const std::vector<ZoneBooking> kNoBookings;

    ZoneActivation what;
    what.aup = m_aup ? m_aup.get() : &kNoBookings;
    what.notams = m_notams ? m_notams.get() : NULL;
    what.showNotamWhenUnknown = cfg.ShowNotamAreas();

    const time_t now = time(NULL);

    for (size_t i = 0; i < zones.size(); i++)
    {
        const ZoneBooking* hit = NULL;
        m_zoneActive[i] = ZoneActiveNow(zones[i], what, now, &hit) ? 1 : 0;
        m_zoneBooking[i] = hit;
    }
}

void CGalaxyATMSystemRadarScreen::DrawZones(HDC hDC)
{
    if (!m_zonesVisible)
        return;

    const std::vector<Zone>& zones = Plugin()->GetConfig().Zones();
    if (zones.empty())
        return;

    int saved = SaveDC(hDC);
    SetBkMode(hDC, TRANSPARENT);

    const Config& cfg = Plugin()->GetConfig();
    const ZoneStyle& styleP = cfg.ZoneStyleFor(ZoneKind::Prohibited);
    const ZoneStyle& styleR = cfg.ZoneStyleFor(ZoneKind::Restricted);
    const ZoneStyle& styleD = cfg.ZoneStyleFor(ZoneKind::Danger);

    {
        AreaCanvas canvas(hDC, GetRadarArea(), styleR.line, (float)Theme::ZoneWidth);
        std::vector<POINT> pts;

        for (size_t i = 0; i < zones.size(); i++)
        {
            if (i >= m_zoneActive.size() || !m_zoneActive[i])
                continue;

            const Zone& zone = zones[i];
            if (!ZoneOutline(zone, pts))
                continue;

            const ZoneStyle& style = (zone.kind == ZoneKind::Prohibited) ? styleP
                : (zone.kind == ZoneKind::Danger) ? styleD : styleR;

            canvas.Wash(pts, style.fill, style.alpha);
            canvas.SetColor(style.line);
            canvas.Ring(pts, true);
        }
    }

    RestoreDC(hDC, saved);
}

void CGalaxyATMSystemRadarScreen::RegisterZoneObjects()
{
    if (!m_zonesVisible)
        return;

    const std::vector<Zone>& zones = Plugin()->GetConfig().Zones();
    if (zones.empty())
        return;

    RECT ra = GetRadarArea();
    if (ra.right <= ra.left || ra.bottom <= ra.top)
        return;

    const int kCellPx = 24;
    const int kMaxCellPx = 96;
    const int kMaxBoxes = 3000;

    struct Outline
    {
        size_t index;
        std::vector<POINT> pts;
        double area;
    };
    std::vector<Outline> visible;

    std::vector<POINT> pts;
    for (size_t i = 0; i < zones.size(); i++)
    {
        if (i >= m_zoneActive.size() || !m_zoneActive[i])
            continue;
        if (!ZoneOutline(zones[i], pts))
            continue;

        bool onScreen = false;
        for (size_t seg = 0; seg < pts.size() && !onScreen; seg++)
        {
            POINT a = pts[seg], b = pts[(seg + 1) % pts.size()];
            onScreen = Geom::ClipSegment(ra, a, b);
        }
        if (!onScreen)
        {
            POINT mid = { (ra.left + ra.right) / 2, (ra.top + ra.bottom) / 2 };
            onScreen = Geom::PointInPolygon(pts, mid);
        }
        if (!onScreen)
            continue;

        Outline o;
        o.index = i;
        o.pts = pts;

        double twice = 0.0;
        for (size_t seg = 0; seg < pts.size(); seg++)
        {
            const POINT& a = pts[seg];
            const POINT& b = pts[(seg + 1) % pts.size()];
            twice += (double)a.x * b.y - (double)b.x * a.y;
        }
        o.area = fabs(twice) * 0.5;

        visible.push_back(std::move(o));
    }

    if (visible.empty())
        return;

    struct Square
    {
        int    index;
        int    rank;
        double score;
    };

    std::vector<Square> grid;

    int cell = kCellPx;
    int cols = 1, rows = 1, baseCX = 0, baseCY = 0;
    int claimed = 0;

    for (;;)
    {
        baseCX = (int)floor((double)ra.left / cell);
        baseCY = (int)floor((double)ra.top / cell);
        cols = max(1, (int)floor((double)(ra.right - 1) / cell) - baseCX + 1);
        rows = max(1, (int)floor((double)(ra.bottom - 1) / cell) - baseCY + 1);

        Square unclaimed = { -1, 0, 0.0 };
        grid.assign((size_t)cols * rows, unclaimed);
        claimed = 0;

        auto claim = [&](int cx, int cy, int index, int rank, double score)
            {
                cx -= baseCX;
                cy -= baseCY;
                if (cx < 0 || cy < 0 || cx >= cols || cy >= rows)
                    return;
                Square& sq = grid[(size_t)cy * cols + cx];
                if (sq.index < 0)
                {
                    claimed++;
                }
                else if (!(rank < sq.rank || (rank == sq.rank && score < sq.score)))
                {
                    return;
                }
                sq.index = index;
                sq.rank = rank;
                sq.score = score;
            };

        std::vector<double> xs;
        for (const Outline& o : visible)
        {
            RECT bbox = { LONG_MAX, LONG_MAX, LONG_MIN, LONG_MIN };
            for (const POINT& p : o.pts)
            {
                bbox.left = min(bbox.left, p.x);
                bbox.top = min(bbox.top, p.y);
                bbox.right = max(bbox.right, p.x);
                bbox.bottom = max(bbox.bottom, p.y);
            }

            const int cy0 = max(baseCY, (int)floor((double)max(bbox.top, ra.top) / cell));
            const int cy1 = min(baseCY + rows - 1,
                (int)floor((double)min(bbox.bottom, ra.bottom - 1) / cell));

            for (int cy = cy0; cy <= cy1; cy++)
            {
                const double y = cy * (double)cell + cell / 2.0;

                xs.clear();
                for (size_t seg = 0; seg < o.pts.size(); seg++)
                {
                    const POINT& a = o.pts[seg];
                    const POINT& b = o.pts[(seg + 1) % o.pts.size()];
                    if ((a.y <= y) == (b.y <= y))
                        continue;
                    const double t = (y - a.y) / (double)(b.y - a.y);
                    xs.push_back(a.x + t * (b.x - a.x));
                }
                if (xs.size() < 2)
                    continue;
                std::sort(xs.begin(), xs.end());

                for (size_t k = 0; k + 1 < xs.size(); k += 2)
                {
                    const double x0 = max(xs[k], (double)ra.left);
                    const double x1 = min(xs[k + 1], (double)(ra.right - 1));
                    if (x1 < x0)
                        continue;

                    const int cx0 = (int)floor(x0 / cell);
                    const int cx1 = (int)floor(x1 / cell);
                    for (int cx = cx0; cx <= cx1; cx++)
                        claim(cx, cy, (int)o.index, 1, o.area);
                }
            }
        }

        const double step = cell / 2.0;

        for (const Outline& o : visible)
        {
            for (size_t seg = 0; seg < o.pts.size(); seg++)
            {
                POINT a = o.pts[seg], b = o.pts[(seg + 1) % o.pts.size()];
                if (!Geom::ClipSegment(ra, a, b))
                    continue;

                const double len = sqrt((double)(b.x - a.x) * (b.x - a.x) + (double)(b.y - a.y) * (b.y - a.y));
                const int steps = (int)floor(len / step);

                for (int st = 0; st <= steps; st++)
                {
                    const double f = (len > 0.0) ? (st * step) / len : 0.0;
                    const double px = a.x + (b.x - a.x) * f;
                    const double py = a.y + (b.y - a.y) * f;

                    const int cx = (int)floor(px / cell);
                    const int cy = (int)floor(py / cell);
                    const double mx = cx * (double)cell + cell / 2.0;
                    const double my = cy * (double)cell + cell / 2.0;
                    const double d2 = (px - mx) * (px - mx) + (py - my) * (py - my);

                    claim(cx, cy, (int)o.index, 0, d2);
                }
            }
        }

        if (claimed <= kMaxBoxes || cell >= kMaxCellPx)
            break;
        cell *= 2;
    }

    std::map<size_t, std::string> tips;
    for (const Outline& o : visible)
        tips[o.index] = Narrow(zones[o.index].Title().substr(0, 120));

    for (int cy = 0; cy < rows; cy++)
    {
        for (int cx = 0; cx < cols; cx++)
        {
            const Square& sq = grid[(size_t)cy * cols + cx];
            if (sq.index < 0)
                continue;

            const int gx = (baseCX + cx) * cell;
            const int gy = (baseCY + cy) * cell;
            RECT box = { gx, gy, gx + cell, gy + cell };

            char id[16];
            sprintf_s(id, "%d", sq.index);
            AddScreenObject(SO_ZONE_AREA, id, box, false, tips[(size_t)sq.index].c_str());
        }
    }
}

int CGalaxyATMSystemRadarScreen::ZoneFromObjectId(const char* sObjectId)
{
    if (sObjectId == NULL || *sObjectId == '\0')
        return -1;

    char* end = NULL;
    long idx = strtol(sObjectId, &end, 10);
    if (end == sObjectId || idx < 0)
        return -1;

    if ((size_t)idx >= Plugin()->GetConfig().Zones().size())
        return -1;
    if ((size_t)idx >= m_zoneActive.size() || !m_zoneActive[idx])
        return -1;

    return (int)idx;
}

int CGalaxyATMSystemRadarScreen::FindZoneAt(POINT pt)
{
    const std::vector<Zone>& zones = Plugin()->GetConfig().Zones();

    int best = -1;
    double bestDist = 18.0;

    int bestInside = -1;
    double bestInsideArea = 0.0;
    std::vector<POINT> pts;

    for (size_t i = 0; i < zones.size(); i++)
    {
        if (i >= m_zoneActive.size() || !m_zoneActive[i])
            continue;
        if (!ZoneOutline(zones[i], pts))
            continue;

        if (Geom::PointInPolygon(pts, pt))
        {
            double twice = 0.0;
            for (size_t seg = 0; seg < pts.size(); seg++)
            {
                const POINT& a = pts[seg];
                const POINT& b = pts[(seg + 1) % pts.size()];
                twice += (double)a.x * b.y - (double)b.x * a.y;
            }
            const double area = fabs(twice) * 0.5;
            if (bestInside < 0 || area < bestInsideArea)
            {
                bestInside = (int)i;
                bestInsideArea = area;
            }
            continue;
        }

        for (size_t seg = 0; seg < pts.size(); seg++)
        {
            double d = Geom::DistanceToSegment(pts[seg], pts[(seg + 1) % pts.size()], pt);
            if (d < bestDist)
            {
                bestDist = d;
                best = (int)i;
            }
        }
    }
    return (bestInside >= 0) ? bestInside : best;
}

void CGalaxyATMSystemRadarScreen::DrawZoneInfo(HDC hDC)
{
    const std::vector<Zone>& zones = Plugin()->GetConfig().Zones();
    if (m_zoneInfoIndex < 0 || (size_t)m_zoneInfoIndex >= zones.size())
    {
        m_zoneInfoIndex = -1;
        return;
    }

    if ((size_t)m_zoneInfoIndex >= m_zoneActive.size() || !m_zoneActive[m_zoneInfoIndex])
    {
        m_zoneInfoIndex = -1;
        return;
    }

    const Zone& zone = zones[m_zoneInfoIndex];

    std::wstring text = zone.id.empty() ? zone.name : zone.id;

    const ZoneBooking* booking = ((size_t)m_zoneInfoIndex < m_zoneBooking.size())
        ? m_zoneBooking[m_zoneInfoIndex] : NULL;

    if (booking != NULL)
    {
        auto stamp = [](time_t t) -> std::wstring
        {
            tm utc = {};
            if (gmtime_s(&utc, &t) != 0)
                return L"--:-- ----------";
            wchar_t buf[24];
            swprintf_s(buf, L"%02d:%02d %02d-%02d-%04d", utc.tm_hour, utc.tm_min,
                utc.tm_mday, utc.tm_mon + 1, utc.tm_year + 1900);
            return buf;
        };
        text += L"\n" + stamp(booking->start);
        text += L"\n" + stamp(booking->end);
    }

    std::wstring levels = (booking != NULL)
        ? ZoneLevelText(booking->minFL) + L"-" + ZoneLevelText(booking->maxFL)
        : zone.LevelBand();
    if (!levels.empty())
        text += L"\n" + levels;

    if (!zone.note.empty())
        text += L"\n" + zone.note;

    const int kPadX = 10, kPadY = 8;
    const int kMaxW = 360, kMinW = 130;

    int saved = SaveDC(hDC);
    SetBkMode(hDC, TRANSPARENT);

    HFONT oldFont = (HFONT)SelectObject(hDC, m_fonts.Mono);
    RECT calc = { 0, 0, kMaxW - 2 * kPadX, 0 };
    DrawTextW(hDC, text.c_str(), -1, &calc, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_CALCRECT);
    SelectObject(hDC, oldFont);

    int w = max(kMinW, min(kMaxW, (int)(calc.right - calc.left) + 2 * kPadX));
    int h = (int)(calc.bottom - calc.top) + 2 * kPadY;

    RECT ra = GetRadarArea();
    RECT box;
    box.left = m_zoneInfoAt.x + 14;
    box.top = m_zoneInfoAt.y + 14;
    if (box.left + w > ra.right)
        box.left = m_zoneInfoAt.x - 14 - w;
    if (box.top + h > ra.bottom)
        box.top = ra.bottom - h;
    box.left = max(ra.left, box.left);
    box.top = max(ra.top, box.top);
    box.right = box.left + w;
    box.bottom = box.top + h;

    FillAlpha(hDC, box, Theme::SigmetInfoBg, Theme::SigmetInfoAlpha);

    {
        HPEN pen = CreatePen(PS_INSIDEFRAME, 1, Theme::SigmetInfoEdge);
        HPEN oldPen = (HPEN)SelectObject(hDC, pen);
        HBRUSH oldBr = (HBRUSH)SelectObject(hDC, GetStockObject(NULL_BRUSH));
        Rectangle(hDC, box.left, box.top, box.right, box.bottom);
        SelectObject(hDC, oldBr);
        SelectObject(hDC, oldPen);
        DeleteObject(pen);
    }

    RECT r = { box.left + kPadX, box.top + kPadY, box.right - kPadX, box.bottom - kPadY };
    oldFont = (HFONT)SelectObject(hDC, m_fonts.Mono);
    SetTextColor(hDC, Theme::SigmetInfoText);
    DrawTextW(hDC, text.c_str(), -1, &r, DT_LEFT | DT_TOP | DT_WORDBREAK);
    SelectObject(hDC, oldFont);

    RestoreDC(hDC, saved);
}

void CGalaxyATMSystemRadarScreen::CloseSigmetInfoIfButtonReleased()
{
    if (m_sigmetInfoIndex < 0 && m_zoneInfoIndex < 0)
        return;

    const bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

    const int kGraceTicks = 12;

    if (m_sigmetInfoIndex >= 0)
    {
        if (down)
        {
            m_sigmetInfoHeld = true;
        }
        else if (m_sigmetInfoHeld || ++m_sigmetInfoWait >= kGraceTicks)
        {
            m_sigmetInfoIndex = -1;
            RequestRefresh();
        }
    }

    if (m_zoneInfoIndex >= 0)
    {
        if (down)
        {
            m_zoneInfoHeld = true;
        }
        else if (m_zoneInfoHeld || ++m_zoneInfoWait >= kGraceTicks)
        {
            m_zoneInfoIndex = -1;
            RequestRefresh();
        }
    }
}

void CGalaxyATMSystemRadarScreen::DrawSigmetInfo(HDC hDC)
{
    if (!m_sigmets || m_sigmetInfoIndex < 0 || (size_t)m_sigmetInfoIndex >= m_sigmets->size())
    {
        m_sigmetInfoIndex = -1;
        return;
    }

    const Sigmet& sig = (*m_sigmets)[m_sigmetInfoIndex];

    const int kPadX = 10, kPadY = 8;
    const int kMaxW = 400, kMinW = 260;

    const std::wstring& text = sig.raw.empty() ? sig.Title() : sig.raw;

    int saved = SaveDC(hDC);
    SetBkMode(hDC, TRANSPARENT);

    HFONT oldFont = (HFONT)SelectObject(hDC, m_fonts.Mono);
    RECT calc = { 0, 0, kMaxW - 2 * kPadX, 0 };
    DrawTextW(hDC, text.c_str(), -1, &calc, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_CALCRECT);
    SelectObject(hDC, oldFont);

    int w = max(kMinW, min(kMaxW, (int)(calc.right - calc.left) + 2 * kPadX));
    int h = (int)(calc.bottom - calc.top) + 2 * kPadY;

    RECT ra = GetRadarArea();
    RECT box;
    box.left = m_sigmetInfoAt.x + 14;
    box.top = m_sigmetInfoAt.y + 14;
    if (box.left + w > ra.right)
        box.left = m_sigmetInfoAt.x - 14 - w;
    if (box.top + h > ra.bottom)
        box.top = ra.bottom - h;
    box.left = max(ra.left, box.left);
    box.top = max(ra.top, box.top);
    box.right = box.left + w;
    box.bottom = box.top + h;

    FillAlpha(hDC, box, Theme::SigmetInfoBg, Theme::SigmetInfoAlpha);

    {
        HPEN pen = CreatePen(PS_INSIDEFRAME, 1, Theme::SigmetInfoEdge);
        HPEN oldPen = (HPEN)SelectObject(hDC, pen);
        HBRUSH oldBr = (HBRUSH)SelectObject(hDC, GetStockObject(NULL_BRUSH));
        Rectangle(hDC, box.left, box.top, box.right, box.bottom);
        SelectObject(hDC, oldBr);
        SelectObject(hDC, oldPen);
        DeleteObject(pen);
    }

    RECT r = { box.left + kPadX, box.top + kPadY, box.right - kPadX, box.bottom - kPadY };
    oldFont = (HFONT)SelectObject(hDC, m_fonts.Mono);
    SetTextColor(hDC, Theme::SigmetInfoText);
    DrawTextW(hDC, text.c_str(), -1, &r, DT_LEFT | DT_TOP | DT_WORDBREAK);
    SelectObject(hDC, oldFont);

    RestoreDC(hDC, saved);
}

int CGalaxyATMSystemRadarScreen::PanelTop()
{
    RECT ra = GetRadarArea();
    RECT tb = GetToolbarArea();

    if (tb.bottom > 0 && tb.top <= ra.top && tb.bottom <= ra.top)
        return tb.bottom;
    return ra.top;
}

void CGalaxyATMSystemRadarScreen::DrawPanel(HDC hDC)
{
    int width = m_collapsed ? kCollapsedWidth : kPanelWidth;
    int height = m_collapsed
        ? L::HEADER_H + L::COLLAPSED_BOT_PAD
        : !Authorized()
        ? L::HEADER_H + L::HDR_GAP
        + L::Block((m_authState == AuthState::Checking || !m_authMessage.empty()) ? L::AUTH_BOX_H : L::AUTH_BOX_H_IDLE)
        + L::PANEL_BOT_PAD
        : L::HEADER_H
        + L::HDR_GAP + L::Block(L::TIMER_BOX_H)
        + L::BLOCK_GAP + L::Block(L::USER_BOX_H)
        + L::BLOCK_GAP + L::Block(L::VECTORS_BOX_H)
        + L::BLOCK_GAP + L::Block(L::OS_BOX_H)
        + L::BLOCK_GAP_WIDE + L::Block(L::UNITS_BOX_H)
        + L::BLOCK_GAP + L::Block(L::ALTFILTER_BOX_H)
        + L::NOCAP_GAP + L::CODES_BOX_H
        + L::BLOCK_GAP_WIDE + L::Block(L::AERODROME_BOX_H)
        + L::PANEL_BOT_PAD;

    RECT ra = GetRadarArea();
    m_panelArea.right = ra.right;
    m_panelArea.left = ra.right - width;
    m_panelArea.top = m_collapsed ? PanelTop() : PanelTop() + MenuBarHeight();
    m_panelArea.bottom = m_panelArea.top + height;

    int saved = SaveDC(hDC);
    SetBkMode(hDC, TRANSPARENT);

    RECT bgArea = m_panelArea;
    if (!m_collapsed && Authorized() && bgArea.bottom < ra.bottom)
        bgArea.bottom = ra.bottom;
    Theme::FlatFill(hDC, bgArea, Theme::Background);

    int y = DrawHeader(hDC, m_panelArea.top);
    if (!m_collapsed && !Authorized())
    {
        DrawBlockAuth(hDC, y + L::HDR_GAP);
    }
    else if (!m_collapsed)
    {
        y = DrawBlockTimer(hDC, y + L::HDR_GAP);
        y = DrawBlockUser(hDC, y + L::BLOCK_GAP);
        y = DrawBlockVectors(hDC, y + L::BLOCK_GAP);
        y = DrawBlockOs(hDC, y + L::BLOCK_GAP);
        y = DrawBlockUnits(hDC, y + L::BLOCK_GAP_WIDE);
        y = DrawBlockAltFilter(hDC, y + L::BLOCK_GAP);
        y = DrawBlockCodes(hDC, y + L::NOCAP_GAP);
        y = DrawBlockAerodrome(hDC, y + L::BLOCK_GAP_WIDE);
    }

    RestoreDC(hDC, saved);
}

int CGalaxyATMSystemRadarScreen::DrawHeader(HDC hDC, int y)
{
    RECT toggle = { m_panelArea.right - 16, y + 2, m_panelArea.right - 2, y + 16 };
    int tx = (toggle.left + toggle.right) / 2, ty = (toggle.top + toggle.bottom) / 2;
    HPEN pen = CreatePen(PS_SOLID, 1, Theme::Text);
    HPEN oldPen = (HPEN)SelectObject(hDC, pen);
    MoveToEx(hDC, tx - 4, ty, NULL);
    LineTo(hDC, tx + 5, ty);
    if (m_collapsed)
    {
        MoveToEx(hDC, tx, ty - 4, NULL);
        LineTo(hDC, tx, ty + 5);
    }
    SelectObject(hDC, oldPen);
    DeleteObject(pen);
    AddScreenObject(SO_PANEL_COLLAPSE, "PANEL_COLLAPSE", toggle, false,
        m_collapsed ? Tr("Развернуть панель") : Tr("Свернуть панель"));

    SYSTEMTIME st;
    GetSystemTime(&st);

    wchar_t clock[16];
    swprintf_s(clock, L"%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
    wchar_t date[16];
    swprintf_s(date, L"%02d.%02d.%04d", st.wDay, st.wMonth, st.wYear);

    RECT clockR = { m_panelArea.left, y + L::HDR_TOP, m_panelArea.right, y + L::HDR_TOP + L::CLOCK_H };
    Theme::DrawLine(hDC, clockR, clock, m_fonts.Clock, Theme::Text, DT_CENTER | DT_VCENTER);

    std::wstring mode; COLORREF modeColor;
    GetWorkMode(mode, modeColor);
    std::wstring dateLine = std::wstring(date) + L" " + mode;

    RECT dateR = { m_panelArea.left, clockR.bottom + L::CLOCK_GAP, m_panelArea.right,
                   clockR.bottom + L::CLOCK_GAP + L::DATE_H };
    Theme::DrawLine(hDC, dateR, dateLine, m_fonts.Body, modeColor, DT_CENTER | DT_VCENTER);

    return y + L::HEADER_H;
}

namespace
{
    struct AuthStage { ULONGLONG endMs; const wchar_t* text; };
    const AuthStage kAuthStages[] = {
        {  700, L"Подключение к КСА..." },
        { 1500, L"Проверка полномочий..." },
        { 2200, L"Загрузка профиля..." },
        { 2700, L"Доступ разрешён" },
    };
    const size_t    kAuthStageCount = _countof(kAuthStages);
    const ULONGLONG kAuthFillMs     = kAuthStages[kAuthStageCount - 2].endMs;
    const ULONGLONG kAuthTotalMs    = kAuthStages[kAuthStageCount - 1].endMs;
}

void CGalaxyATMSystemRadarScreen::AutoLogin()
{
    if (m_authState != AuthState::LoggedOut || m_loginWindowOpen || Plugin()->TrainingSession())
        return;

    const CGalaxyATMSystemPlugin::LoginState state = Plugin()->MyLogin();
    if (m_autoLoginTried)
    {
        if (state == CGalaxyATMSystemPlugin::LoginState::Done)
            StartAuthCheck();
        else if (state == CGalaxyATMSystemPlugin::LoginState::Failed && m_authMessage.empty())
        {
            m_authFailed = true;
            Plugin()->MyLogin(&m_authMessage);
            RequestRefresh();
        }
        return;
    }

    if (state != CGalaxyATMSystemPlugin::LoginState::Idle
        || Plugin()->AccessSuspended() || !Plugin()->LiveConnection()
        || Plugin()->GetConfig().SquawkServerUrl().empty())
        return;

    const CGalaxyATMSystemPlugin::SavedLogin& saved = Plugin()->SavedIdentity();
    if (!saved.Complete())
        return;

    m_autoLoginTried = true;
    Log::Info("auth", "LOGIN sent with the saved CID and name, without asking");
    Plugin()->StartLogin(saved.cid, saved.surname, saved.firstName, saved.patronymic);
}

void CGalaxyATMSystemRadarScreen::TickAuth()
{
    AutoLogin();
    if (m_authState != AuthState::Checking)
        return;
    if (GetTickCount64() - m_authStartTick >= kAuthTotalMs)
    {
        m_authState = AuthState::LoggedIn;
        Plugin()->SetSessionAuthorized(true);
    }
    RequestRefresh();
}

void CGalaxyATMSystemRadarScreen::SyncAuth()
{
    const bool session = Plugin()->SessionAuthorized();
    if (session && m_authState != AuthState::LoggedIn)
    {
        m_authState = AuthState::LoggedIn;
        m_authMessage.clear();
        m_authFailed = false;
        CloseLoginWindow();
    }
    else if (!session && m_authState == AuthState::LoggedIn)
    {
        m_authState = AuthState::LoggedOut;
        m_authMessage.clear();
        m_openDropdown = DropdownKind::None;
        m_rulerArmed = false;
        m_rulerPlacing = false;
        CloseLoginWindow();
    }
}

void CGalaxyATMSystemRadarScreen::StartAuthCheck()
{
    if (m_authState != AuthState::LoggedOut)
        return;
    m_authState = AuthState::Checking;
    m_authStartTick = GetTickCount64();
    RequestRefresh();
}

bool CGalaxyATMSystemRadarScreen::BypassAvailable()
{
    return m_authState == AuthState::LoggedOut && m_authFailed
        && !Plugin()->AccessSuspended() && !Plugin()->TrainingSession();
}

void CGalaxyATMSystemRadarScreen::ShowNotice(const std::wstring& text)
{
    m_noticeText = text;
    RequestRefresh();
}

namespace
{
    void DrawCloseCross(HDC hDC, const RECT& close, COLORREF ink)
    {
        const int cx = (close.left + close.right) / 2, cy = (close.top + close.bottom) / 2;
        const int arm = 4;
        HPEN pen = CreatePen(PS_SOLID, 2, ink);
        HPEN old = (HPEN)SelectObject(hDC, pen);
        MoveToEx(hDC, cx - arm, cy - arm, NULL);
        LineTo(hDC, cx + arm + 1, cy + arm + 1);
        MoveToEx(hDC, cx + arm, cy - arm, NULL);
        LineTo(hDC, cx - arm - 1, cy + arm + 1);
        SelectObject(hDC, old);
        DeleteObject(pen);
    }
}

void CGalaxyATMSystemRadarScreen::DrawNoticeWindow(HDC hDC)
{
    const int kTitleH = 24, kPad = 14, kBtnW = 96, kBtnH = 22, kGap = 12;
    const int W = 420;
    const int textW = W - 2 * (kPad + 5);

    RECT measure = { 0, 0, textW, 0 };
    {
        HFONT old = (HFONT)SelectObject(hDC, m_fonts.Body);
        DrawTextW(hDC, m_noticeText.c_str(), -1, &measure, DT_CALCRECT | DT_WORDBREAK | DT_CENTER);
        SelectObject(hDC, old);
    }
    const int textH = max(18, (int)(measure.bottom - measure.top));
    const int H = kTitleH + kPad + textH + kGap + kBtnH + kPad;

    RECT ra = GetRadarArea();
    RECT win;
    win.left = ra.left + max(0, ((ra.right - ra.left) - W) / 2);
    win.top = ra.top + max(0, ((ra.bottom - ra.top) - H) / 3);
    win.right = win.left + W;
    win.bottom = win.top + H;

    int saved = SaveDC(hDC);
    SetBkMode(hDC, TRANSPARENT);

    HRGN rgn = Theme::WinRegion(win);
    SelectClipRgn(hDC, rgn);
    Theme::FlatFill(hDC, win, Theme::MenuBarFill);
    RECT title = { win.left, win.top, win.right, win.top + kTitleH };
    Theme::DrawLine(hDC, title, Tr(L"Уведомление"), m_fonts.WinTitle, Theme::MenuText, DT_CENTER | DT_VCENTER);
    RECT body = { win.left + 5, title.bottom, win.right - 5, win.bottom - 5 };
    Theme::OutlineBox(hDC, body, Theme::InsetFill, Theme::Border);
    SelectClipRgn(hDC, NULL);
    DeleteObject(rgn);
    Theme::WinBorder(hDC, win, 2, Theme::WinFrame);

    RECT close = { win.right - 26, title.top + 4, win.right - 8, title.bottom - 4 };
    DrawCloseCross(hDC, close, Theme::MenuText);

    AddScreenObject(SO_NOTICE_WINDOW, "NOTICE_WINDOW", win, false, "");
    AddScreenObject(SO_NOTICE_CLOSE, "NOTICE_CLOSE", close, false, Tr("Закрыть"));

    RECT textR = { win.left + kPad + 5, title.bottom + kPad, win.right - kPad - 5, title.bottom + kPad + textH };
    HFONT oldFont = (HFONT)SelectObject(hDC, m_fonts.Body);
    SetTextColor(hDC, Theme::Text);
    DrawTextW(hDC, m_noticeText.c_str(), -1, &textR, DT_WORDBREAK | DT_CENTER);
    SelectObject(hDC, oldFont);

    const int okLeft = (win.left + win.right - kBtnW) / 2;
    RECT ok = { okLeft, textR.bottom + kGap, okLeft + kBtnW, textR.bottom + kGap + kBtnH };
    Theme::OutlineBox(hDC, ok, Theme::MenuBarFill, Theme::MenuText);
    Theme::DrawLine(hDC, ok, L"OK", m_fonts.Body, Theme::MenuText, DT_CENTER | DT_VCENTER);
    AddScreenObject(SO_NOTICE_OK, "NOTICE_OK", ok, false, Tr("Закрыть"));

    RestoreDC(hDC, saved);
}

void CGalaxyATMSystemRadarScreen::DrawLoginWindow(HDC hDC)
{
    const int kTitleH = 24, kPad = 12, kLine = 18, kRowH = 24, kRowGap = 6, kLabelW = 84;
    const int kBtnW = 96, kBtnH = 22, kGap = 8;
    const int W = 420;
    const int H = kTitleH + kPad + kLine + kRowGap + LF_COUNT * (kRowH + kRowGap) + 2 * kLine + kGap + kBtnH + kPad;

    RECT ra = GetRadarArea();
    if (!m_loginPositioned)
    {
        m_loginArea.left = ra.left + max(0, ((ra.right - ra.left) - W) / 2);
        m_loginArea.top  = ra.top + max(0, ((ra.bottom - ra.top) - H) / 3);
        m_loginPositioned = true;
    }
    m_loginArea.left   = max(ra.left, min(m_loginArea.left, ra.right - W));
    m_loginArea.top    = max(ra.top, min(m_loginArea.top, ra.bottom - H));
    m_loginArea.right  = m_loginArea.left + W;
    m_loginArea.bottom = m_loginArea.top + H;
    const RECT win = m_loginArea;

    int saved = SaveDC(hDC);
    SetBkMode(hDC, TRANSPARENT);

    HRGN rgn = Theme::WinRegion(win);
    SelectClipRgn(hDC, rgn);
    Theme::FlatFill(hDC, win, Theme::MenuBarFill);
    RECT title = { win.left, win.top, win.right, win.top + kTitleH };
    Theme::DrawLine(hDC, title, Tr(L"Вход в систему КСА"), m_fonts.WinTitle, Theme::MenuText,
        DT_CENTER | DT_VCENTER);
    RECT body = { win.left + 5, title.bottom, win.right - 5, win.bottom - 5 };
    Theme::OutlineBox(hDC, body, Theme::InsetFill, Theme::Border);
    SelectClipRgn(hDC, NULL);
    DeleteObject(rgn);
    Theme::WinBorder(hDC, win, 2, Theme::WinFrame);

    RECT close = { win.right - 26, title.top + 4, win.right - 8, title.bottom - 4 };
    DrawCloseCross(hDC, close, Theme::MenuText);

    AddScreenObject(SO_LOGIN_WINDOW, "LOGIN_WINDOW", win, false, "");
    AddScreenObject(SO_LOGIN_HEADER, "LOGIN_HEADER", title, true, Tr("Перетащите окно"));
    AddScreenObject(SO_LOGIN_CLOSE, "LOGIN_CLOSE", close, false, Tr("Закрыть"));

    std::wstring message;
    const CGalaxyATMSystemPlugin::LoginState state = Plugin()->MyLogin(&message);
    const bool sending = (state == CGalaxyATMSystemPlugin::LoginState::Sending);

    const int left = win.left + kPad, right = win.right - kPad;
    int y = title.bottom + kPad;
    RECT intro = { left, y, right, y + kLine };
    Theme::DrawLine(hDC, intro, Tr(L"Введите данные, указанные при регистрации - один раз:"),
        m_fonts.Body, Theme::Text, DT_LEFT | DT_VCENTER);
    y += kLine + kRowGap;

    static const wchar_t* const kLabels[LF_COUNT] = { L"CID", L"Фамилия", L"Имя", L"Отчество" };
    static const wchar_t* const kHints[LF_COUNT]  = { L"1234567", L"Иванов", L"Иван", L"если есть" };
    for (int i = 0; i < LF_COUNT; i++)
    {
        RECT label = { left, y, left + kLabelW, y + kRowH };
        Theme::DrawLine(hDC, label, Tr(kLabels[i]), m_fonts.Body, Theme::Text, DT_LEFT | DT_VCENTER);

        RECT field = { left + kLabelW, y, right, y + kRowH };
        m_loginFields[i] = field;
        Theme::OutlineBox(hDC, field, Theme::ControlFill, m_entryField == i ? Theme::Text : Theme::BorderStrong);

        RECT text = { field.left + 6, field.top, field.right - 6, field.bottom };
        const std::wstring& value = m_loginValues[i];
        if (m_entryField != i)
        {
            if (value.empty())
                Theme::DrawLine(hDC, text, Tr(kHints[i]), m_fonts.Body, Theme::MenuTextDisabled, DT_LEFT | DT_VCENTER);
            else
                Theme::DrawLine(hDC, text, value, m_fonts.Body, Theme::Text,
                    DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
        }
        if (!sending)
            AddScreenObject(SO_LOGIN_FIELD, std::to_string(i).c_str(), field, false,
                Tr("Нажмите, чтобы ввести"));
        y += kRowH + kRowGap;
    }

    std::wstring status = Tr(L"Enter - следующее поле, Esc - отмена");
    COLORREF statusColor = Theme::TextDim;
    if (sending)
    {
        status = Tr(L"Проверка...");
        statusColor = Theme::DuplicateText;
    }
    else if (!m_loginProblem.empty())
    {
        status = m_loginProblem;
        statusColor = Theme::DistressText;
    }
    else if (state == CGalaxyATMSystemPlugin::LoginState::Failed)
    {
        status = message;
        statusColor = Theme::DistressText;
    }
    RECT statusR = { left, y, right, y + kLine };
    Theme::DrawLine(hDC, statusR, status, m_fonts.Small, statusColor, DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
    y += kLine;

    const std::string registerUrl = Plugin()->RegisterPageUrl();
    if (!registerUrl.empty())
    {
        std::wstring shown = Widen(registerUrl.c_str());
        for (const wchar_t* scheme : { L"http://", L"https://" })
            if (shown.compare(0, wcslen(scheme), scheme) == 0)
                shown.erase(0, wcslen(scheme));
        RECT linkR = { left, y, right, y + kLine };
        Theme::DrawLine(hDC, linkR, Tr(L"Регистрация: ") + shown, m_fonts.Small, Theme::Text,
            DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
        AddScreenObject(SO_LOGIN_REGISTER, "LOGIN_REGISTER", linkR, false, Tr("Открыть страницу регистрации в браузере"));
    }
    y += kLine + kGap;

    RECT send = { right - kBtnW, y, right, y + kBtnH };
    const COLORREF ink = sending ? Theme::MenuTextDisabled : Theme::MenuText;
    Theme::OutlineBox(hDC, send, Theme::MenuBarFill, ink);
    Theme::DrawLine(hDC, send, Tr(L"Войти"), m_fonts.Body, ink, DT_CENTER | DT_VCENTER);
    if (!sending)
        AddScreenObject(SO_LOGIN_SEND, "LOGIN_SEND", send, false, Tr("Войти в систему"));

    RestoreDC(hDC, saved);

    if (m_entryField >= 0)
        m_entry.Move(m_loginFields[m_entryField]);
    m_loginDrawnTick = GetTickCount64();
}

namespace
{
    std::wstring TrimSpaces(const std::wstring& s)
    {
        const size_t from = s.find_first_not_of(L" \t");
        if (from == std::wstring::npos)
            return std::wstring();
        return s.substr(from, s.find_last_not_of(L" \t") - from + 1);
    }
}

void CGalaxyATMSystemRadarScreen::EditLoginField(int field)
{
    CommitEntry();
    if (field < 0 || field >= LF_COUNT)
        return;

    POINT cursor;
    HWND view = NULL;
    if (CursorRadarPoint(cursor, &view))
        m_entryView = view;

    m_entryPending = field;
    m_entryPendingTick = GetTickCount64();
    RequestRefresh();
}

void CGalaxyATMSystemRadarScreen::TickEntry()
{
    if (m_entry.IsOpen() && (!m_loginWindowOpen || GetTickCount64() - m_loginDrawnTick > 2500))
        CommitEntry();

    if (m_entryPending < 0 || !m_loginWindowOpen || m_loginDrawnTick < m_entryPendingTick)
        return;
    const int field = m_entryPending;
    m_entryPending = -1;
    if (Plugin()->MyLogin() == CGalaxyATMSystemPlugin::LoginState::Sending)
        return;

    const bool opened = m_entryView != NULL && m_entry.Open(m_entryView, m_loginFields[field], m_fonts.Body,
        m_loginValues[field], false, field == LF_CID ? 10 : 40,
        [this, field](TextEntry::End end)
        {
            if (end == TextEntry::End::Cancel)
            {
                m_entry.Close();
                m_entryField = -1;
                RequestRefresh();
            }
            else if (end == TextEntry::End::Submit && field == LF_COUNT - 1)
            {
                SendLogin();
            }
            else
            {
                EditLoginField((field + 1) % LF_COUNT);
            }
        });

    if (opened)
    {
        m_entryField = field;
    }
    else
    {
        Log::Warn("entry", "no edit box of our own over login field " + std::to_string(field)
            + " - EuroScope's popup edit used instead");
        GetPlugIn()->OpenPopupEdit(m_loginFields[field], FN_LOGIN_FIELD + field, Narrow(m_loginValues[field]).c_str());
    }
    RequestRefresh();
}

void CGalaxyATMSystemRadarScreen::CommitEntry()
{
    m_entryPending = -1;
    if (m_entry.IsOpen() && m_entryField >= 0 && m_entryField < LF_COUNT)
    {
        const std::wstring text = TrimSpaces(m_entry.Text());
        std::wstring& value = m_loginValues[m_entryField];
        if (text != value)
        {
            value = text;
            m_loginProblem.clear();
            Plugin()->ResetLogin();
        }
    }
    m_entry.Close();
    m_entryField = -1;
    RequestRefresh();
}

void CGalaxyATMSystemRadarScreen::SendLogin()
{
    CommitEntry();
    if (Plugin()->MyLogin() == CGalaxyATMSystemPlugin::LoginState::Sending)
        return;

    const std::wstring& cid = m_loginValues[LF_CID];
    const bool digits = !cid.empty()
        && cid.find_first_not_of(L"0123456789") == std::wstring::npos;
    if (cid.empty())
    {
        m_loginProblem = Tr(L"Введите свой CID");
    }
    else if (!digits)
    {
        m_loginProblem = Tr(L"CID - это только цифры");
    }
    else if (m_loginValues[LF_SURNAME].empty() || m_loginValues[LF_FIRST_NAME].empty())
    {
        m_loginProblem = Tr(L"Введите фамилию и имя");
    }
    else
    {
        m_loginProblem.clear();
        Plugin()->StartLogin(cid, m_loginValues[LF_SURNAME], m_loginValues[LF_FIRST_NAME],
            m_loginValues[LF_PATRONYMIC]);
    }
    RequestRefresh();
}

void CGalaxyATMSystemRadarScreen::CloseLoginWindow()
{
    m_entryPending = -1;
    m_entry.Close();
    m_entryField = -1;
    m_loginProblem.clear();
    m_loginWindowOpen = false;
    RequestRefresh();
}

int CGalaxyATMSystemRadarScreen::DrawBlockAuth(HDC hDC, int y)
{
    const bool checking = (m_authState == AuthState::Checking);
    const bool refused = !checking && !m_authMessage.empty();
    RECT box = DrawBlockFrame(hDC, y, Tr(L"Авторизация"),
        (checking || refused) ? L::AUTH_BOX_H : L::AUTH_BOX_H_IDLE);

    std::wstring designation, role, user;
    GetUserInfo(designation, role, user);

    int cy = box.top + L::AU_TOP;
    RECT designR = { ContentLeft(), cy, ContentLeft() + 47, cy + L::AU_ROW };
    RECT userR = { designR.right + 6, cy, ContentRight(), cy + L::AU_ROW };
    DrawFittedField(hDC, designR, designation);
    DrawFittedField(hDC, userR, user);
    cy += L::AU_ROW + L::AU_GAP;

    RECT roleR = { ContentLeft(), cy, ContentRight(), cy + L::AU_ROW };
    DrawFittedField(hDC, roleR, role);
    cy += L::AU_ROW + L::AU_GAP2;

    if (!checking)
    {
        if (refused)
        {
            RECT reasonR = { ContentLeft(), cy, ContentRight(), cy + L::AU_STATUS };
            Theme::DrawLine(hDC, reasonR, m_authMessage, m_fonts.Small, Theme::DistressText,
                DT_CENTER | DT_VCENTER | DT_END_ELLIPSIS);
        }
        return box.bottom;
    }

    RECT statusR = { ContentLeft(), cy, ContentRight(), cy + L::AU_STATUS };
    ULONGLONG elapsed = GetTickCount64() - m_authStartTick;
    const wchar_t* status = kAuthStages[kAuthStageCount - 1].text;
    for (const AuthStage& s : kAuthStages)
    {
        if (elapsed < s.endMs)
        {
            status = s.text;
            break;
        }
    }

    ULONGLONG done = min(elapsed, kAuthFillMs);
    Theme::OutlineBox(hDC, statusR, Theme::ControlFill, Theme::BorderStrong);
    int w = (int)((statusR.right - statusR.left - 4) * done / kAuthFillMs);
    if (w > 0)
    {
        RECT bar = { statusR.left + 2, statusR.top + 2, statusR.left + 2 + w, statusR.bottom - 2 };
        Theme::FlatFill(hDC, bar, Theme::Active);
    }
    Theme::DrawLine(hDC, statusR, Tr(status), m_fonts.Body,
        (elapsed >= kAuthFillMs) ? Theme::AuthGranted : Theme::Text,
        DT_CENTER | DT_VCENTER | DT_END_ELLIPSIS);

    return box.bottom;
}

int CGalaxyATMSystemRadarScreen::DrawBlockTimer(HDC hDC, int y)
{
    RECT box = DrawBlockFrame(hDC, y, Tr(L"Таймер"), L::TIMER_BOX_H);

    int cy = box.top + L::T_PAD;
    RECT btn = { ContentLeft(), cy, ContentLeft() + 26, cy + L::T_ROW };
    RECT field = { btn.right + 6, cy, ContentRight(), cy + L::T_ROW };

    DrawToggleChip(hDC, btn, L"C", m_timerRunning, SO_TIMER_TOGGLE, "TIMER_TOGGLE",
        Tr("ЛКМ - пуск/стоп таймера, ПКМ - сброс"));

    std::wstring text = L"---";
    if (m_timerRunning || m_timerElapsedMs > 0)
    {
        ULONGLONG elapsed = m_timerElapsedMs;
        if (m_timerRunning)
            elapsed += GetTickCount64() - m_timerStartTick;
        ULONGLONG totalSec = elapsed / 1000;
        wchar_t buf[16];
        swprintf_s(buf, L"%02llu:%02llu:%02llu", totalSec / 3600, (totalSec / 60) % 60, totalSec % 60);
        text = buf;
    }
    DrawOutlinedField(hDC, field, text, m_fonts.Body);

    return box.bottom;
}

int CGalaxyATMSystemRadarScreen::DrawBlockUser(HDC hDC, int y)
{
    RECT box = DrawBlockFrame(hDC, y, Tr(L"Пользователь"), L::USER_BOX_H);

    std::wstring designation, role, user;
    GetUserInfo(designation, role, user);

    int cy = box.top + L::U_TOP;
    RECT designR = { ContentLeft(), cy, ContentLeft() + 47, cy + L::U_ROW };
    RECT userR = { designR.right + 6, cy, ContentRight(), cy + L::U_ROW };
    DrawFittedField(hDC, designR, designation);
    DrawFittedField(hDC, userR, user);
    cy += L::U_ROW + L::U_GAP;

    RECT roleR = { ContentLeft(), cy, ContentRight(), cy + L::U_ROW };
    DrawFittedField(hDC, roleR, role);

    return box.bottom;
}

int CGalaxyATMSystemRadarScreen::DrawBlockVectors(HDC hDC, int y)
{
    RECT box = DrawBlockFrame(hDC, y, Tr(L"Векторы"), L::VECTORS_BOX_H);

    int cy = box.top + L::V_TOP;
    int x = box.left + 9;

    RECT distBtn = { x, cy, x + 25, cy + L::V_ROW };
    RECT distField = { distBtn.right + 6, cy, distBtn.right + 6 + 63, cy + L::V_ROW };
    RECT timeBtn = { distField.right + 9, cy, distField.right + 9 + 21, cy + L::V_ROW };
    RECT timeField = { timeBtn.right + 6, cy, timeBtn.right + 6 + 63, cy + L::V_ROW };

    m_vecDistFieldRect = distField;
    m_vecTimeFieldRect = timeField;

    DrawToggleChip(hDC, distBtn, Tr(L"Д"), m_vecDistEnabled, SO_VEC_DIST_TOGGLE, "VEC_DIST_TOGGLE",
        Tr("Вектор по дальности (км) - вместо вектора по времени"), Theme::ButtonMid);
    wchar_t distText[16];
    swprintf_s(distText, L"%d", m_vecDistKm);
    DrawDropdownField(hDC, distField, distText, SO_VEC_DIST_FIELD, "VEC_DIST_FIELD", Tr("Выбрать длину вектора, км"));

    DrawToggleChip(hDC, timeBtn, Tr(L"Э"), m_vecTimeEnabled, SO_VEC_TIME_TOGGLE, "VEC_TIME_TOGGLE",
        Tr("Вектор по времени (мин) - вместо вектора по дальности"), Theme::ButtonMid);
    wchar_t timeText[16];
    swprintf_s(timeText, L"%d", m_vecTimeMin);
    DrawDropdownField(hDC, timeField, timeText, SO_VEC_TIME_FIELD, "VEC_TIME_FIELD", Tr("Выбрать время вектора, мин"));

    cy += L::V_ROW + L::V_GAP1;
    DrawCheckRow(hDC, cy, box.left + 8, Tr(L"Вектор по плану"), m_vecByPlan,
        SO_VEC_BY_PLAN_CHK, "VEC_BY_PLAN", Tr("Вектор по плану"));

    cy += L::V_CHK + L::V_GAP2;
    DrawCheckRow(hDC, cy, box.left + 8, Tr(L"Расчётный эшелон"), m_vecShowLevel,
        SO_VEC_LEVEL_CHK, "VEC_LEVEL", Tr("Расчётный эшелон"));

    return box.bottom;
}

int CGalaxyATMSystemRadarScreen::DrawBlockOs(HDC hDC, int y)
{
    RECT box = DrawBlockFrame(hDC, y, Tr(L"ФС"), L::OS_BOX_H);

    RECT fontField = { box.right - 4 - 63, box.top + L::O_TOP, box.right - 4, box.top + L::O_TOP + L::O_LABEL };
    m_osFontFieldRect = fontField;

    RECT label = { box.left + 8, box.top + L::O_TOP, fontField.left - 6, box.top + L::O_TOP + L::O_LABEL };
    Theme::DrawLine(hDC, label, Tr(L"Р-р шрифта:"), m_fonts.Body, Theme::Text, DT_LEFT | DT_VCENTER);

    wchar_t sizeText[8];
    swprintf_s(sizeText, L"%d", Plugin()->TagFontSize());
    DrawDropdownField(hDC, fontField, sizeText, SO_OS_FONT_FIELD, "OS_FONT_FIELD", Tr("Выбрать размер шрифта формуляра"));

    RECT list = { box.left + 5, label.bottom + L::O_GAP, box.right - 4,
                  label.bottom + L::O_GAP + L::O_LIST_H };
    Theme::OutlineBox(hDC, list, Theme::InsetFill, Theme::Border);

    const int x = list.left + 5;
    int row = list.top + L::OS_ROW0;
    DrawCheckRow(hDC, row, x, Tr(L"2 строчный"), m_osLines == 2,
        SO_OS_TWO_LINE, "OS_2LINE", Tr("Двухстрочный формуляр"));
    row += L::OS_PITCH;
    DrawCheckRow(hDC, row, x, Tr(L"скорость"), m_osSpeed,
        SO_OS_SPEED, "OS_SPEED", Tr("Показывать скорость в формуляре"));
    row += L::OS_PITCH;
    DrawCheckRow(hDC, row, x, Tr(L"3 строчный"), m_osLines == 3,
        SO_OS_THREE_LINE, "OS_3LINE", Tr("Трёхстрочный формуляр"));

    return box.bottom;
}

int CGalaxyATMSystemRadarScreen::DrawBlockAltFilter(HDC hDC, int y)
{
    RECT box = DrawBlockFrame(hDC, y, Tr(L"Фильтр высоты"), L::ALTFILTER_BOX_H);

    wchar_t fromText[8], toText[8];
    swprintf_s(fromText, L"FL%03d", Plugin()->AltFilterFromFL());
    swprintf_s(toText, L"FL%03d", Plugin()->AltFilterToFL());

    const int valLeft = box.left + 106, valRight = valLeft + 60;

    int cy = box.top + L::F_TOP;
    RECT maxLbl = { box.left + 8, cy, valLeft, cy + L::F_ROW };
    RECT maxVal = { valLeft, cy, valRight, cy + L::F_ROW };
    Theme::DrawLine(hDC, maxLbl, Tr(L"Макс:"), m_fonts.Body, Theme::Text, DT_LEFT | DT_VCENTER);
    DrawOutlinedField(hDC, maxVal, toText, m_fonts.Body);
    AddScreenObject(SO_ALTFILTER_TO, "ALTFILTER_TO", maxVal, false, Tr("Верхняя граница фильтра высоты"));
    cy += L::F_ROW + L::F_GAP1;

    RECT minLbl = { box.left + 8, cy, valLeft, cy + L::F_ROW };
    RECT minVal = { valLeft, cy, valRight, cy + L::F_ROW };
    Theme::DrawLine(hDC, minLbl, Tr(L"Мин :"), m_fonts.Body, Theme::Text, DT_LEFT | DT_VCENTER);
    DrawOutlinedField(hDC, minVal, fromText, m_fonts.Body);
    AddScreenObject(SO_ALTFILTER_FROM, "ALTFILTER_FROM", minVal, false, Tr("Нижняя граница фильтра высоты"));
    cy += L::F_ROW + L::F_GAP2;

    DrawCheckRow(hDC, cy, box.left + 39, Tr(L"Использовать"), Plugin()->AltFilterEnabled(),
        SO_ALTFILTER_USE_CHK, "ALTFILTER_USE", Tr("Использовать фильтр высоты"));

    return box.bottom;
}

int CGalaxyATMSystemRadarScreen::DrawBlockCodes(HDC hDC, int y)
{
    RECT box = DrawBoxOnly(hDC, y, L::CODES_BOX_H);

    double widthNM = DisplayWidthNM();
    wchar_t scaleText[24];
    if (Plugin()->UnitDist() == DistUnit::NM)
        swprintf_s(scaleText, L"%d", (int)lround(widthNM));
    else
        swprintf_s(scaleText, L"%d", (int)lround(widthNM * 1.852));

    RECT vv = { box.left + 7, box.top + L::C_VV_TOP, box.left + 82, box.top + L::C_VV_TOP + L::C_VV_H };
    Theme::DrawLine(hDC, vv, scaleText, m_fonts.Body, Theme::Text, DT_LEFT | DT_VCENTER);
    AddScreenObject(SO_VV_SCALE, "VV_SCALE", vv, false,
        Tr("Масштаб: ширина отображаемой зоны от края до края"));

    RECT all = { box.left + 85, box.top + L::C_ALL_TOP, box.left + 125, box.top + L::C_ALL_TOP + L::C_ROW_H };
    Theme::OutlineBox(hDC, all, m_codeAll ? Theme::Active : Theme::ButtonMid, Theme::Border);
    Theme::DrawLine(hDC, all, Tr(L"ВСЕ"), m_fonts.Small, Theme::Text, DT_CENTER | DT_VCENTER);
    AddScreenObject(SO_CODE_ALL, "CODE_ALL", all, false, Tr("Пропускать все коды"));

    RECT bp = { box.left + 32, box.top + L::C_BP_TOP, box.left + 65, box.top + L::C_BP_TOP + L::C_ROW_H };
    Theme::OutlineBox(hDC, bp, m_codeBp ? Theme::Active : Theme::ButtonMid, Theme::Border);
    Theme::DrawLine(hDC, bp, Tr(L"БП"), m_fonts.Small, Theme::Text, DT_CENTER | DT_VCENTER);
    AddScreenObject(SO_CODE_BP, "CODE_BP", bp, false, Tr("Без привязки"));

    RECT filter = { box.left + 71, box.top + L::C_FLT_TOP, box.left + 183, box.top + L::C_FLT_TOP + L::C_FLT_H };
    Theme::OutlineBox(hDC, filter, Theme::InsetFill, Theme::Border);
    if (!m_codeFilter.empty())
    {
        RECT inner = { filter.left + 3, filter.top, filter.right - 3, filter.bottom };
        Theme::DrawLine(hDC, inner, m_codeFilter, m_fonts.Small, Theme::Text,
            DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
    }
    AddScreenObject(SO_CODE_FILTER, "CODE_FILTER", filter, false, Tr("Коды источника ВВ1"));

    RECT extra = { box.left + 189, box.top + L::C_EXTRA_TOP, box.left + 201, box.top + L::C_EXTRA_TOP + L::C_EXTRA_H };
    Theme::OutlineBox(hDC, extra, m_codeExtra ? Theme::Active : Theme::Background, Theme::Border);
    AddScreenObject(SO_CODE_EXTRA, "CODE_EXTRA", extra, false, Tr("Источник ВВ1 включён"));

    if (!m_vvDragging)
        SyncSliderFromZoom();

    RECT track = { box.left + 7, box.top + L::C_SLIDER_TOP, box.left + 13, box.top + L::C_SLIDER_BOT };
    m_vvSliderRect = track;
    Theme::FillBox(hDC, track, Theme::SliderTrack);
    int thumbY = track.bottom - MulDiv(track.bottom - track.top, m_vvGain, 100);
    RECT lit = { track.left, thumbY, track.right, track.bottom };
    if (lit.bottom > lit.top)
        Theme::FillBox(hDC, lit, Theme::SliderFill);
    HBRUSH thumbBr = CreateSolidBrush(Theme::SliderThumb);
    HBRUSH oldBr = (HBRUSH)SelectObject(hDC, thumbBr);
    HPEN thumbPen = CreatePen(PS_SOLID, 1, Theme::SliderThumb);
    HPEN oldPen = (HPEN)SelectObject(hDC, thumbPen);
    Ellipse(hDC, track.left - 2, thumbY - 5, track.right + 2, thumbY + 5);
    SelectObject(hDC, oldPen);
    DeleteObject(thumbPen);
    SelectObject(hDC, oldBr);
    DeleteObject(thumbBr);
    AddScreenObject(SO_VV_SLIDER, "VV_SLIDER",
        RECT{ track.left - 4, track.top, track.right + 4, track.bottom }, true,
        Tr("Масштаб радара: вверх - приблизить, вниз - отдалить"));

    RECT distressCap = { box.left, box.top + L::C_DISTRESS_CAP, box.right, box.top + L::C_DISTRESS_CAP + L::C_CAP_H };
    Theme::DrawLine(hDC, distressCap, Tr(L"Коды бедствия"), m_fonts.Body, Theme::Text, DT_CENTER | DT_VCENTER);
    RECT distress = { box.left + 24, box.top + L::C_DISTRESS_FIELD, box.left + 201, box.top + L::C_DISTRESS_FIELD + L::C_FIELD_H };
    Theme::OutlineBox(hDC, distress, Theme::Background, Theme::Border);
    Theme::DrawLine(hDC, RECT{ distress.left + 4, distress.top, distress.right - 4, distress.bottom },
        GetDistressCodes(), m_fonts.Small, Theme::DistressText,
        DT_CENTER | DT_VCENTER | DT_END_ELLIPSIS);

    RECT dupCap = { box.left, box.top + L::C_DUP_CAP, box.right, box.top + L::C_DUP_CAP + L::C_CAP_H };
    Theme::DrawLine(hDC, dupCap, Tr(L"Двойной код"), m_fonts.Body, Theme::Text, DT_CENTER | DT_VCENTER);
    RECT dup = { box.left + 24, box.top + L::C_DUP_FIELD, box.left + 201, box.top + L::C_DUP_FIELD + L::C_FIELD_H };
    Theme::OutlineBox(hDC, dup, Theme::Background, Theme::Border);
    Theme::DrawLine(hDC, RECT{ dup.left + 4, dup.top, dup.right - 4, dup.bottom },
        GetDuplicateCodes(), m_fonts.Small, Theme::DuplicateText,
        DT_CENTER | DT_VCENTER | DT_END_ELLIPSIS);

    return box.bottom;
}

int CGalaxyATMSystemRadarScreen::DrawBlockUnits(HDC hDC, int y)
{
    RECT box = DrawBlockFrame(hDC, y, Tr(L"Ед. изм."), L::UNITS_BOX_H);

    const int col1  = box.left + 5;
    const int colM  = box.left + 70;
    const int colFM = box.left + 140;
    const int col2  = box.left + 119;

    int cy = box.top + L::E_TOP;
    auto row = [&]() { int r = cy; cy += L::E_PITCH; return r; };

    int r1 = row();
    DrawRadioRow(hDC, r1, col1,  colM,        L"FL",   Plugin()->UnitAlt() == AltUnit::FL,  SO_UNIT_ALT_FL,  "U_ALT_FL",  Tr("Эшелон"));
    DrawRadioRow(hDC, r1, colM,  colFM,       L"M",    Plugin()->UnitAlt() == AltUnit::M,   SO_UNIT_ALT_M,   "U_ALT_M",   Tr("Метры"));
    DrawRadioRow(hDC, r1, colFM, box.right,   L"FL+M", Plugin()->UnitAlt() == AltUnit::FLM, SO_UNIT_ALT_FLM, "U_ALT_FLM", Tr("Эшелон и метры"));

    int r2 = row();
    DrawRadioRow(hDC, r2, col1, col2,       Tr(L"Фут / м"), Plugin()->UnitVs() == VsUnit::FtMin, SO_UNIT_VS_FTM, "U_VS_FTM", Tr("Футы в минуту"));
    DrawRadioRow(hDC, r2, col2, box.right,  Tr(L"М / С"),   Plugin()->UnitVs() == VsUnit::MS,    SO_UNIT_VS_MS,  "U_VS_MS",  Tr("Метры в секунду"));

    int r3 = row();
    DrawRadioRow(hDC, r3, col1, col2,       Tr(L"Узлы"),  Plugin()->UnitGs() == GsUnit::Knots, SO_UNIT_GS_KT,  "U_GS_KT",  Tr("Узлы"));
    DrawRadioRow(hDC, r3, col2, box.right,  Tr(L"Км / ч"), Plugin()->UnitGs() == GsUnit::Kmh,  SO_UNIT_GS_KMH, "U_GS_KMH", Tr("Километры в час"));

    int r4 = row();
    DrawRadioRow(hDC, r4, col1, col2,       Tr(L"Мили"), Plugin()->UnitDist() == DistUnit::NM, SO_UNIT_DIST_NM, "U_DIST_NM", Tr("Морские мили"));
    DrawRadioRow(hDC, r4, col2, box.right,  Tr(L"Км"),   Plugin()->UnitDist() == DistUnit::Km, SO_UNIT_DIST_KM, "U_DIST_KM", Tr("Километры"));

    return box.bottom;
}

int CGalaxyATMSystemRadarScreen::DrawBlockAerodrome(HDC hDC, int y)
{
    const Config& cfg = Plugin()->GetConfig();

    RECT box = DrawBlockFrame(hDC, y, cfg.Airport(), L::AERODROME_BOX_H);

    const int kTagLeft   = 5,  kTagRight  = 57;
    const int kValLeft   = 63;

    int cy = box.top + L::A_TOP;
    RECT davlTag = { box.left + kTagLeft, cy, box.left + kTagRight, cy + L::A_ROW };
    RECT davlVal = { box.left + kValLeft, cy, box.left + 199, cy + L::A_ROW };
    Theme::DrawGhostControl(hDC, davlTag, Tr(L"ДАВЛ"), m_fonts.Body);
    Theme::DrawValueField(hDC, davlVal, Plugin()->QnhMmHg() + L"/" + Plugin()->QnhHpa(),
        m_fonts.Body, true, DT_LEFT | DT_VCENTER);
    cy += L::A_ROW + L::A_GAP;

    RECT epTag = { box.left + kTagLeft, cy, box.left + kTagRight, cy + L::A_ROW };
    RECT epVal = { box.left + kValLeft, cy, box.left + 127, cy + L::A_ROW };
    RECT atisBtn = { box.left + 146, cy, box.left + 199, cy + L::A_ROW };
    Theme::DrawGhostControl(hDC, epTag, Tr(L"Э/П"), m_fonts.Body);
    Theme::DrawValueField(hDC, epVal, Plugin()->TransitionLevel(),
        m_fonts.Body, true, DT_LEFT | DT_VCENTER);

    if (m_atisOpen)
        Theme::DrawValueField(hDC, atisBtn, Tr(L"АТИС"), m_fonts.Body);
    else
        Theme::DrawGhostControl(hDC, atisBtn, Tr(L"АТИС"), m_fonts.Body);
    AddScreenObject(SO_ATIS_BUTTON, "ATIS_BTN", atisBtn, false,
        Tr("Открыть текст АТИС"));

    return box.bottom;
}

void CGalaxyATMSystemRadarScreen::DrawAtisWindow(HDC hDC)
{
    const int W = 370, H = 430;
    const int kFrame   = 2;
    const int kTitleH  = 21;
    const int kSide    = 14;
    const int kTrackW  = 18;
    const int kEndBtn  = 16;
    const int kButtonH = 21;
    const int kButtonW = 60;

    RECT ra = GetRadarArea();
    if (!m_atisPositioned)
    {
        const bool haveStrip = (m_atisLetterArea.bottom > m_atisLetterArea.top);
        m_atisArea.left = haveStrip ? m_atisLetterArea.left : ra.left + 8;
        m_atisArea.top = haveStrip ? m_atisLetterArea.bottom + 6
                                   : PanelTop() + MenuBarHeight();
        m_atisPositioned = true;
    }

    if (m_atisArea.left + W > ra.right)
        m_atisArea.left = ra.right - W;
    if (m_atisArea.top + H > ra.bottom)
        m_atisArea.top = ra.bottom - H;
    if (m_atisArea.left < ra.left)
        m_atisArea.left = ra.left;
    if (m_atisArea.top < ra.top)
        m_atisArea.top = ra.top;

    m_atisArea.right = m_atisArea.left + W;
    m_atisArea.bottom = m_atisArea.top + H;

    int saved = SaveDC(hDC);
    SetBkMode(hDC, TRANSPARENT);

    HRGN winRgn = Theme::WinRegion(m_atisArea);
    SelectClipRgn(hDC, winRgn);

    Theme::FlatFill(hDC, m_atisArea, Theme::WinBody);

    RECT title = { m_atisArea.left + kFrame, m_atisArea.top + kFrame,
                   m_atisArea.right - kFrame, m_atisArea.top + kFrame + kTitleH };
    Theme::VGradient(hDC, title, Theme::WinTitleTop, Theme::WinTitleBot);

    SelectClipRgn(hDC, NULL);
    DeleteObject(winRgn);

    AddScreenObject(SO_ATIS_HEADER, "ATIS_HEADER", title, true, Tr("Перетащите окно АТИС"));

    RECT titleEdge = { title.left, title.bottom, title.right, title.bottom + kFrame };
    Theme::FlatFill(hDC, titleEdge, Theme::WinFrame);

    Theme::DrawLine(hDC, title, L"ATIS message", m_fonts.WinTitle, Theme::WinTitleText,
        DT_CENTER | DT_VCENTER);

    RECT close = { m_atisArea.right - kFrame - 24, title.top + 1,
                   m_atisArea.right - kFrame - 4, title.bottom - 1 };
    {
        int cx = (close.left + close.right) / 2;
        int cy = (close.top + close.bottom) / 2;
        const int arm = 5;
        HPEN pen = CreatePen(PS_SOLID, 2, Theme::WinTitleText);
        HPEN old = (HPEN)SelectObject(hDC, pen);
        MoveToEx(hDC, cx - arm, cy - arm, NULL);
        LineTo(hDC, cx + arm + 1, cy + arm + 1);
        MoveToEx(hDC, cx + arm, cy - arm, NULL);
        LineTo(hDC, cx - arm - 1, cy + arm + 1);
        SelectObject(hDC, old);
        DeleteObject(pen);
    }
    AddScreenObject(SO_ATIS_CLOSE, "ATIS_CLOSE", close, false, Tr("Закрыть"));

    RECT index = { m_atisArea.left + kSide + 4, titleEdge.bottom + 14,
                   m_atisArea.right - kSide, titleEdge.bottom + 36 };
    Theme::DrawLine(hDC, index, L"Index:   " + Plugin()->AtisIndex(),
        m_fonts.MonoBig, Theme::Text, DT_LEFT | DT_VCENTER);

    RECT ok = { m_atisArea.right - kSide - 9 - kButtonW,
                m_atisArea.bottom - kFrame - 13 - kButtonH,
                m_atisArea.right - kSide - 9, m_atisArea.bottom - kFrame - 13 };
    Theme::FlatFill(hDC, ok, Theme::ButtonFace);
    Theme::FlatFrame(hDC, ok, kFrame, Theme::WinFrame);
    Theme::DrawLine(hDC, ok, L"OK", m_fonts.Body, Theme::ButtonText, DT_CENTER | DT_VCENTER);
    AddScreenObject(SO_ATIS_OK, "ATIS_OK", ok, false, Tr("Закрыть"));

    RECT panel = { m_atisArea.left + kSide, index.bottom + 17,
                   m_atisArea.right - kSide, ok.top - 6 };
    Theme::FlatFrame(hDC, panel, kFrame, Theme::WinFrame);

    RECT inner = { panel.left + kFrame, panel.top + kFrame,
                   panel.right - kFrame, panel.bottom - kFrame };
    RECT track = { inner.right - kTrackW, inner.top, inner.right, inner.bottom };
    RECT paper = { inner.left, inner.top, track.left - kFrame, inner.bottom };
    RECT gutter = { paper.right, inner.top, track.left, inner.bottom };

    Theme::FlatFill(hDC, paper, Theme::Paper);
    Theme::FlatFill(hDC, gutter, Theme::WinFrame);

    RECT textArea = { paper.left + 8, paper.top + 6, paper.right - 6, paper.bottom - 6 };

    const std::wstring atisText = Plugin()->AtisMessage();

    HFONT oldFont = (HFONT)SelectObject(hDC, m_fonts.Mono);
    RECT calc = { 0, 0, textArea.right - textArea.left, 0 };
    DrawTextW(hDC, atisText.c_str(), -1, &calc, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_CALCRECT);
    SelectObject(hDC, oldFont);

    int viewH = textArea.bottom - textArea.top;
    int totalH = calc.bottom - calc.top;
    m_atisScrollMax = max(0, totalH - viewH);
    m_atisScrollPx = max(0, min(m_atisScrollMax, m_atisScrollPx));

    HRGN clip = CreateRectRgn(textArea.left, textArea.top, textArea.right, textArea.bottom);
    SelectClipRgn(hDC, clip);

    RECT scrolled = textArea;
    OffsetRect(&scrolled, 0, -m_atisScrollPx);
    scrolled.bottom = scrolled.top + totalH + 1;

    oldFont = (HFONT)SelectObject(hDC, m_fonts.Mono);
    SetTextColor(hDC, Theme::PaperInk);
    DrawTextW(hDC, atisText.c_str(), -1, &scrolled, DT_LEFT | DT_TOP | DT_WORDBREAK);
    SelectObject(hDC, oldFont);

    SelectClipRgn(hDC, NULL);
    DeleteObject(clip);

    Theme::FlatFill(hDC, track, Theme::ScrollTrough);

    RECT btnUp = { track.left, track.top, track.right, track.top + kEndBtn };
    RECT btnDn = { track.left, track.bottom - kEndBtn, track.right, track.bottom };
    AddScreenObject(SO_ATIS_LINE_UP, "ATIS_UP", btnUp, false, Tr("Прокрутить вверх"));
    AddScreenObject(SO_ATIS_LINE_DN, "ATIS_DN", btnDn, false, Tr("Прокрутить вниз"));

    for (const RECT* btn : { &btnUp, &btnDn })
    {
        int cx = (btn->left + btn->right) / 2;
        int cy = (btn->top + btn->bottom) / 2;
        RECT mark = { cx - 4, cy - 4, cx + 4, cy + 4 };
        RECT hole = { cx - 1, cy - 1, cx + 2, cy + 2 };
        Theme::FlatFill(hDC, mark, Theme::WinFrame);
        Theme::FlatFill(hDC, hole, Theme::ScrollTrough);
    }
    RECT ruleUp = { track.left, btnUp.bottom, track.right, btnUp.bottom + kFrame };
    RECT ruleDn = { track.left, btnDn.top - kFrame, track.right, btnDn.top };
    Theme::FlatFill(hDC, ruleUp, Theme::WinFrame);
    Theme::FlatFill(hDC, ruleDn, Theme::WinFrame);

    RECT bar = { track.left, ruleUp.bottom, track.right, ruleDn.top };
    int trackH = max(1, (int)(bar.bottom - bar.top));
    m_atisThumbH = (totalH > viewH) ? max(18, (int)((__int64)trackH * viewH / totalH)) : trackH;
    int thumbTop = bar.top;
    if (m_atisScrollMax > 0)
        thumbTop += (int)((__int64)(trackH - m_atisThumbH) * m_atisScrollPx / m_atisScrollMax);

    RECT thumb = { bar.left, thumbTop, bar.right, thumbTop + m_atisThumbH };
    Theme::FlatFill(hDC, thumb, Theme::ScrollThumb);
    Theme::FlatFrame(hDC, thumb, 1, Theme::ScrollEdge);
    AddScreenObject(SO_ATIS_SCROLLBAR, "ATIS_SCROLL", bar, true, Tr("Прокрутка текста АТИС"));

    Theme::WinBorder(hDC, m_atisArea, kFrame, Theme::WinFrame);

    RestoreDC(hDC, saved);
}

namespace
{
    const wchar_t* const kMenuItems[] = {
        L"Настройки", L"Вид", L"Сенсоры", L"Карта", L"Аэродром", L"Списки",
        L"Метео", L"Почта", L"Загрузка", L"Статистика", L"Архив", L"Справка",
    };
    const int kMenuItemCount = (int)_countof(kMenuItems);

    std::wstring KeyboardLanguage()
    {
        HWND fg = GetForegroundWindow();
        DWORD thread = (fg != NULL) ? GetWindowThreadProcessId(fg, NULL) : 0;
        HKL layout = GetKeyboardLayout(thread);
        if (layout == NULL)
            return std::wstring();

        const LANGID lang = LOWORD((UINT_PTR)layout);
        wchar_t name[16] = {};
        if (GetLocaleInfoW(MAKELCID(lang, SORT_DEFAULT), LOCALE_SISO639LANGNAME, name, _countof(name)) == 0)
            return std::wstring();
        std::wstring text = name;
        CharUpperBuffW(&text[0], (DWORD)text.size());
        return text;
    }
}

int CGalaxyATMSystemRadarScreen::MenuBarHeight()
{
    return max(L::MENU_BAR_H, Plugin()->GetConfig().AtisTopOffset());
}

void CGalaxyATMSystemRadarScreen::DrawMenuBar(HDC hDC)
{
    RECT ra = GetRadarArea();
    const int top = PanelTop();
    RECT bar = { ra.left, top, ra.right, top + MenuBarHeight() };
    if (bar.right <= bar.left || bar.bottom <= bar.top)
        return;

    int saved = SaveDC(hDC);
    SetBkMode(hDC, TRANSPARENT);

    Theme::FlatFill(hDC, bar, Theme::MenuBarFill);

    AddScreenObject(SO_MENU_BAR, "MENU_BAR", bar, false, "");

    const int kPadX = 8;
    const int kGap  = 9;

    int contentRight = bar.right - kPadX;
    const std::wstring language = KeyboardLanguage();
    if (!language.empty())
    {
        SIZE sz = Theme::MeasureText(hDC, m_fonts.Small, language);
        RECT r = { bar.right - kPadX - sz.cx, bar.top, bar.right - kPadX, bar.bottom };
        Theme::DrawLine(hDC, r, language, m_fonts.Small, Theme::MenuText, DT_LEFT | DT_VCENTER);
        contentRight = r.left - 2 * kGap;
    }

    int x = bar.left + kPadX;
    for (int i = 0; i < kMenuItemCount; i++)
    {
        const wchar_t* text = Tr(kMenuItems[i]);
        HFONT font = m_fonts.Menu;
        SIZE sz = Theme::MeasureText(hDC, font, text);

        if (x + sz.cx > contentRight)
            break;

        RECT r = { x, bar.top, x + sz.cx, bar.bottom };
        Theme::DrawLine(hDC, r, text, font, Theme::MenuTextDisabled, DT_LEFT | DT_VCENTER);

        x += sz.cx + kGap;
    }

    const int kBtnLead = 40;
    const int kBtnPastCentre = 440;
    const int kBtnPadX = 6;
    const int kBtnGap  = 6;
    const int kBtnH    = 18;
    HFONT btnFont = m_fonts.Small;
    SIZE szLogin  = Theme::MeasureText(hDC, btnFont, L"LOGIN");
    SIZE szBypass = Theme::MeasureText(hDC, btnFont, L"Bypass");
    const int loginW  = szLogin.cx + 2 * kBtnPadX;
    const int bypassW = szBypass.cx + 2 * kBtnPadX;
    int btnTop = bar.top + (bar.bottom - bar.top - kBtnH) / 2;
    const int loginLeft = max(x - kGap + kBtnLead,
        min((bar.left + bar.right) / 2 + kBtnPastCentre, contentRight - loginW - kBtnGap - bypassW));
    RECT login  = { loginLeft, btnTop, loginLeft + loginW, btnTop + kBtnH };
    RECT bypass = { login.right + kBtnGap, btnTop, login.right + kBtnGap + bypassW, btnTop + kBtnH };
    if (bypass.right <= contentRight)
    {
        const bool training = Plugin()->TrainingSession();
        const bool bypassLive = BypassAvailable();
        const COLORREF loginInk  = training ? Theme::MenuTextDisabled : Theme::Text;
        const COLORREF bypassInk = bypassLive ? Theme::Text : Theme::MenuTextDisabled;
        Theme::OutlineBox(hDC, login, Theme::MenuBarFill, loginInk);
        Theme::DrawLine(hDC, login, L"LOGIN", btnFont, loginInk, DT_CENTER | DT_VCENTER);
        Theme::OutlineBox(hDC, bypass, Theme::MenuBarFill, bypassInk);
        Theme::DrawLine(hDC, bypass, L"Bypass", btnFont, bypassInk, DT_CENTER | DT_VCENTER);
        if (!training)
        {
            AddScreenObject(SO_AUTH_LOGIN, "MENU_LOGIN", login, false, Tr("Войти в систему"));
            if (m_authState == AuthState::LoggedOut)
                AddScreenObject(SO_AUTH_BYPASS, "MENU_BYPASS", bypass, false,
                    bypassLive ? Tr("Войти без проверки в базе") : "");
        }
    }

    RestoreDC(hDC, saved);
}

void CGalaxyATMSystemRadarScreen::DrawAtisLetterWindow(HDC hDC)
{
    const std::wstring label  = L"INDEX ATIS: ";
    const std::wstring letter = Plugin()->AtisIndex();

    const int kFrame = 2;
    const int kPadX  = 7;
    const int kPadY  = 3;

    HFONT font = m_fonts.Mono;

    SIZE szLabel  = Theme::MeasureText(hDC, font, label);
    SIZE szLetter = Theme::MeasureText(hDC, font, letter);
    const int W = szLabel.cx + szLetter.cx + 2 * (kFrame + kPadX);
    const int H = max(szLabel.cy, szLetter.cy) + 2 * (kFrame + kPadY);

    RECT ra = GetRadarArea();
    m_atisLetterArea.left = ra.left;
    m_atisLetterArea.top  = PanelTop() + MenuBarHeight();

    if (m_atisLetterArea.left + W > ra.right)
        m_atisLetterArea.left = max(ra.left, ra.right - W);

    m_atisLetterArea.right  = m_atisLetterArea.left + W;
    m_atisLetterArea.bottom = m_atisLetterArea.top + H;

    int saved = SaveDC(hDC);
    SetBkMode(hDC, TRANSPARENT);

    Theme::FlatFill(hDC, m_atisLetterArea, Theme::ControlFill);
    Theme::FlatFrame(hDC, m_atisLetterArea, kFrame, Theme::WinFrame);

    int textLeft = m_atisLetterArea.left + kFrame + kPadX;
    RECT labelR = { textLeft, m_atisLetterArea.top, textLeft + szLabel.cx,
                    m_atisLetterArea.bottom };
    RECT letterR = { labelR.right, m_atisLetterArea.top, labelR.right + szLetter.cx,
                     m_atisLetterArea.bottom };
    Theme::DrawLine(hDC, labelR, label, font, Theme::Text,
        DT_LEFT | DT_VCENTER);
    Theme::DrawLine(hDC, letterR, letter, font, Theme::AtisIndexText,
        DT_LEFT | DT_VCENTER);

    AddScreenObject(SO_ATIS_LETTER_HEADER, "ATIS_L_HEADER", m_atisLetterArea, false,
        Tr("ЛКМ - текст АТИС, .atis - скрыть"));

    RestoreDC(hDC, saved);
}

namespace
{
    const int kRcSvgW = 2068, kRcSvgH = 904;
    const int kRcScaleMin = 25, kRcScaleMax = 100;

    struct RcColumn { const wchar_t* title; int svgLeft, svgRight; };

    const RcColumn kRcColumns[] = {
        { L"КФ",       13,  134 },
        { L"Рейс",    140,  358 },
        { L"ВРЛ",     364,  485 },
        { L"S",       491,  537 },
        { L"Тип",     543,  663 },
        { L"W",       669,  717 },
        { L"CFL",     723,  842 },
        { L"Точка",   848, 1030 },
        { L"Вход",   1036, 1254 },
        { L"Точка",  1260, 1440 },
        { L"Выход",  1446, 1664 },
        { L"ВыхЭш",  1670, 1851 },
        { L"ПВО",    1857, 1953 },
        { L"Крд",    1959, 2055 },
    };
    const int kRcCols = (int)(sizeof(kRcColumns) / sizeof(kRcColumns[0]));

    enum
    {
        RC_KF, RC_CALLSIGN, RC_SQUAWK, RC_S, RC_TYPE, RC_W, RC_CFL,
        RC_ENTRY_POINT, RC_ENTRY, RC_EXIT_POINT, RC_EXIT, RC_EXIT_LEVEL,
        RC_PVO, RC_CRD
    };

    const int kRcPaneTopSvg[2]    = { 102, 462 };
    const int kRcPaneBottomSvg[2] = { 445, 802 };
    const int kRcHeadSvg = 46, kRcPlateInsetSvg = 3;
    const int kRcRowSvg = 46, kRcRowPitchSvg = 49;
    const int kRcRows = 6;

    int RcGrowSvg(int rows) { return (rows - kRcRows) * kRcRowPitchSvg; }

    const float kRcDividerSvg[] = { 844.0f, 1256.0f, 1852.5f };
    const float kRcDividerWSvg = 2.5f;

    const double kKfLateralNm    = 5.0;
    const double kKfVerticalFt   = 800.0;
    const int    kKfLookaheadSec = 120;
    const int    kKfStepSec      = 10;
    const int    kKfMinGsKt      = 50;
    const double kKfScanNm       = 40.0;
    const double kKfScanFt       = 10000.0;

    const float kRcCrossSvg[12][2] = {
        { 2011.27f, 64.4168f }, { 2008.58f, 61.7335f }, { 2019.32f, 51.0002f },
        { 2008.58f, 40.2668f }, { 2011.27f, 37.5835f }, { 2022.00f, 48.3168f },
        { 2032.73f, 37.5835f }, { 2035.42f, 40.2668f }, { 2024.68f, 51.0002f },
        { 2035.42f, 61.7335f }, { 2032.73f, 64.4168f }, { 2022.00f, 53.6835f },
    };

    std::wstring RcSortText(const SectorListRow& r, int cell)
    {
        const std::wstring& v = r.cells[cell];
        if (cell != RC_ENTRY && cell != RC_EXIT)
            return v;
        size_t a = v.find(L'/');
        return (a == std::wstring::npos) ? v : v.substr(0, a);
    }

    std::wstring RcUpper(std::wstring v)
    {
        std::transform(v.begin(), v.end(), v.begin(), ::towupper);
        return v;
    }
}

void CGalaxyATMSystemRadarScreen::BuildSectorList(std::vector<SectorListRow>& out)
{
    out.clear();

    SYSTEMTIME st;
    GetSystemTime(&st);
    const int nowMin = st.wHour * 60 + st.wMinute;

    auto hhmm = [nowMin](int minutesAhead) -> std::wstring
    {
        if (minutesAhead < 0)
            return L"----";
        int t = (nowMin + minutesAhead) % (24 * 60);
        wchar_t buf[8];
        swprintf_s(buf, L"%02d%02d", t / 60, t % 60);
        return buf;
    };
    auto level = [](int ft) -> std::wstring
    {
        if (ft <= 0)
            return L"------";
        wchar_t buf[8];
        swprintf_s(buf, L"F%03d", ft / 100);
        return buf;
    };

    struct KfTrack
    {
        std::string callsign;
        CPosition pos;
        double trackDeg, nmPerSec, ft, ftPerSec;
    };
    std::vector<KfTrack> airborne;
    for (CRadarTarget t = GetPlugIn()->RadarTargetSelectFirst(); t.IsValid();
         t = GetPlugIn()->RadarTargetSelectNext(t))
    {
        CRadarTargetPositionData p = t.GetPosition();
        if (!p.IsValid() || t.GetGS() < kKfMinGsKt)
            continue;
        airborne.push_back({ t.GetCallsign(), p.GetPosition(), t.GetTrackHeading(),
            t.GetGS() / 3600.0, (double)p.GetFlightLevel(), t.GetVerticalSpeed() / 60.0 });
    }

    auto inConflict = [&](const std::string& callsign) -> bool
    {
        auto self = std::find_if(airborne.begin(), airborne.end(),
            [&](const KfTrack& k) { return k.callsign == callsign; });
        if (self == airborne.end())
            return false;

        for (const KfTrack& other : airborne)
        {
            if (&other == &*self
                || self->pos.DistanceTo(other.pos) > kKfScanNm
                || fabs(self->ft - other.ft) > kKfScanFt)
                continue;

            for (int s = 0; s <= kKfLookaheadSec; s += kKfStepSec)
            {
                CPosition a = s ? CalculateDestinationPoint(self->pos, self->trackDeg, self->nmPerSec * s) : self->pos;
                CPosition b = s ? CalculateDestinationPoint(other.pos, other.trackDeg, other.nmPerSec * s) : other.pos;
                double dv = (self->ft + self->ftPerSec * s) - (other.ft + other.ftPerSec * s);
                if (a.DistanceTo(b) < kKfLateralNm && fabs(dv) < kKfVerticalFt)
                    return true;
            }
        }
        return false;
    };

    for (CFlightPlan fp = GetPlugIn()->FlightPlanSelectFirst(); fp.IsValid();
         fp = GetPlugIn()->FlightPlanSelectNext(fp))
    {
        int state = fp.GetState();
        if (state == FLIGHT_PLAN_STATE_NON_CONCERNED || state == FLIGHT_PLAN_STATE_REDUNDANT)
            continue;

        const std::string callsign = fp.GetCallsign();
        const int entryMin = fp.GetSectorEntryMinutes();
        const ULONGLONG nowTick = GetTickCount64();
        if (entryMin >= 0)
            m_rcLastInSector[callsign] = nowTick;

        if (!m_rcFilterCallsign.empty()
            && RcUpper(Widen(callsign.c_str())).find(m_rcFilterCallsign) == std::wstring::npos)
            continue;
        if (m_rcFilterBefore >= 0 && entryMin > m_rcFilterBefore)
            continue;
        if (m_rcFilterAfter >= 0 && entryMin < 0)
        {
            auto seen = m_rcLastInSector.find(callsign);
            if (seen == m_rcLastInSector.end()
                || nowTick - seen->second > (ULONGLONG)m_rcFilterAfter * 60000)
                continue;
        }

        CFlightPlanData fpd = fp.GetFlightPlanData();
        CFlightPlanControllerAssignedData cad = fp.GetControllerAssignedData();

        SectorListRow row;
        row.callsign = fp.GetCallsign();
        row.mine = fp.GetTrackingControllerIsMe();
        row.crdState = fp.GetCoordinatedNextControllerState();

        CRadarTarget track = fp.GetCorrelatedRadarTarget();
        double course = -1.0;
        CFlightPlanExtractedRoute route = fp.GetExtractedRoute();
        const int points = route.GetPointsNumber();
        if (points >= 2)
        {
            CPosition from = route.GetPointPosition(0), to = route.GetPointPosition(points - 1);
            if (from.DistanceTo(to) > 10.0)
                course = from.DirectionTo(to);
        }
        if (course < 0.0 && track.IsValid() && track.GetGS() >= kKfMinGsKt)
            course = track.GetTrackHeading();
        row.east = course < 0.0 || fmod(course + 360.0, 360.0) < 180.0;

        row.cells[RC_CALLSIGN] = Widen(fp.GetCallsign());

        std::wstring assigned = Widen(cad.GetSquawk());
        std::wstring actual;
        CRadarTarget rt = fp.GetCorrelatedRadarTarget();
        if (rt.IsValid())
            actual = Widen(rt.GetPosition().GetSquawk());
        if (assigned.empty())
            assigned = actual;
        row.cells[RC_SQUAWK] = assigned;
        row.cells[RC_S] = (!assigned.empty() && !actual.empty() && assigned != actual) ? L"S" : L"";

        row.cells[RC_KF] = (rt.IsValid() && inConflict(rt.GetCallsign())) ? Tr(L"КФ") : L"";

        row.cells[RC_TYPE] = Widen(fpd.GetAircraftFPType());
        row.cells[RC_W] = fpd.IsRvsm() ? L"R" : L"";

        int cleared = fp.GetClearedAltitude();
        row.cells[RC_CFL] = level(cleared > 0 ? cleared : fp.GetFinalAltitude());

        row.cells[RC_ENTRY_POINT] = Widen(fp.GetEntryCoordinationPointName());
        row.cells[RC_ENTRY] = hhmm(fp.GetSectorEntryMinutes()) + L"/"
            + level(fp.GetEntryCoordinationAltitude());
        row.cells[RC_EXIT_POINT] = Widen(fp.GetExitCoordinationPointName());
        row.cells[RC_EXIT] = hhmm(fp.GetSectorExitMinutes()) + L"/"
            + level(fp.GetExitCoordinationAltitude());

        int exitFt = fp.GetExitCoordinationAltitude();
        row.cells[RC_EXIT_LEVEL] = level(exitFt > 0 ? exitFt : fp.GetFinalAltitude());

        std::wstring pad = RcUpper(Widen(cad.GetScratchPadString()));
        row.cells[RC_PVO] = (pad.find(L"ПВО") != std::wstring::npos
                          || pad.find(L"PVO") != std::wstring::npos) ? L"+" : L"";

        row.cells[RC_CRD] = (row.crdState != COORDINATION_STATE_NONE) ? L"ACT" : L"";

        out.push_back(row);
    }

    const int cell = (m_rcSortKey >= 0 && m_rcSortKey < kRcCols) ? m_rcSortKey : RC_CALLSIGN;
    const bool asc = m_rcSortAsc;
    std::stable_sort(out.begin(), out.end(),
        [cell, asc](const SectorListRow& a, const SectorListRow& b)
        {
            std::wstring ka = RcSortText(a, cell), kb = RcSortText(b, cell);
            return asc ? (ka < kb) : (kb < ka);
        });
}

int CGalaxyATMSystemRadarScreen::RcScale(int availW, int availH)
{
    const int fit = min(availW * 100 / kRcSvgW, availH * 100 / kRcSvgH);
    return max(kRcScaleMin, min(m_rcScale, min(kRcScaleMax, fit)));
}

void CGalaxyATMSystemRadarScreen::RcObject(int type, const char* id, RECT r, bool moveable, const char* tip)
{
    if (m_rcDrawingFloat)
        m_rcFloatHits.push_back({ type, id, r });
    else
        AddScreenObject(type, id, r, moveable, tip);
}

int CGalaxyATMSystemRadarScreen::PlanSectorList(const std::vector<SectorListRow>& all, int maxSvgH)
{
    int count[2] = { 0, 0 };
    for (const SectorListRow& r : all)
        count[r.mine ? 0 : 1]++;

    int need[2] = { max(0, count[0] - kRcRows), max(0, count[1] - kRcRows) };
    int room = max(0, (maxSvgH - kRcSvgH) / kRcRowPitchSvg);
    int give[2] = { 0, 0 };
    if (need[0] + need[1] <= room)
    {
        give[0] = need[0];
        give[1] = need[1];
    }
    else
    {
        give[0] = min(need[0], room / 2);
        give[1] = min(need[1], room - give[0]);
        give[0] = min(need[0], room - give[1]);
    }

    m_rcPageRows[0] = kRcRows + give[0];
    m_rcPageRows[1] = kRcRows + give[1];
    return kRcSvgH + RcGrowSvg(m_rcPageRows[0]) + RcGrowSvg(m_rcPageRows[1]);
}

void CGalaxyATMSystemRadarScreen::DrawSectorListWindow(HDC hDC)
{
    std::vector<SectorListRow> all;
    BuildSectorList(all);

    RECT ra = GetRadarArea();
    const int scale = RcScale(ra.right - ra.left, ra.bottom - ra.top);
    const int svgH = PlanSectorList(all, (ra.bottom - ra.top) * 100 / scale);
    const int W = (kRcSvgW * scale + 50) / 100, H = (svgH * scale + 50) / 100;

    if (!m_rcPositioned)
    {
        m_rcArea.left = ra.left + max(0, ((ra.right - ra.left) - W) / 2);
        m_rcArea.top  = ra.top + 60;
        m_rcPositioned = true;
    }

    if (m_rcArea.left + W > ra.right)
        m_rcArea.left = ra.right - W;
    if (m_rcArea.top + H > ra.bottom)
        m_rcArea.top = ra.bottom - H;
    if (m_rcArea.left < ra.left)
        m_rcArea.left = ra.left;
    if (m_rcArea.top < ra.top)
        m_rcArea.top = ra.top;

    m_rcArea.right  = m_rcArea.left + W;
    m_rcArea.bottom = m_rcArea.top + H;

    DrawSectorList(hDC, m_rcArea, scale, false, all);
}

void CGalaxyATMSystemRadarScreen::DrawSectorList(HDC hDC, RECT area, int scale, bool floating,
    const std::vector<SectorListRow>& all)
{
    std::vector<const SectorListRow*> other, mine;
    for (const SectorListRow& r : all)
        (r.mine ? mine : other).push_back(&r);

    auto S  = [scale](int svg) { return (svg * scale + 50) / 100; };
    auto SF = [scale](double svg) { return (float)(svg * scale / 100.0); };

    if (m_rcFontScale != scale)
    {
        for (HFONT f : { m_rcFont, m_rcHeadFont, m_rcRowFont, m_rcKfFont })
            if (f != NULL)
                DeleteObject(f);
        m_rcFont     = Theme::ListFont(max(6, S(32)), Theme::ListMedium);
        m_rcHeadFont = Theme::ListFont(max(6, S(32)), Theme::ListRegular);
        m_rcRowFont  = Theme::ListFont(max(6, S(24)), Theme::ListMedium);
        m_rcKfFont   = Theme::ListFont(max(6, S(24)), Theme::ListBold);
        m_rcFontScale = scale;
    }

    m_rcDrawingFloat = floating;
    if (floating)
        m_rcFloatHits.clear();

    const int ox = area.left, oy = area.top;
    auto X = [ox, &S](int svg) { return ox + S(svg); };
    auto Y = [oy, &S](int svg) { return oy + S(svg); };

    int saved = SaveDC(hDC);
    SetBkMode(hDC, TRANSPARENT);

    if (floating)
        Theme::FlatFill(hDC, area, Theme::ListGround);
    else
        FillAlpha(hDC, area, Theme::ListGround, Theme::ListGroundAlpha);

    RECT bar = { X(10), Y(20), X(2058), Y(82) };
    {
        Gdiplus::Graphics g(hDC);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

        const Gdiplus::REAL l = (Gdiplus::REAL)bar.left, r = (Gdiplus::REAL)bar.right;
        const Gdiplus::REAL t = (Gdiplus::REAL)bar.top, b = (Gdiplus::REAL)bar.bottom;
        const Gdiplus::REAL d = SF(20) * 2;

        Gdiplus::GraphicsPath shape;
        shape.AddArc(l, t, d, d, 180.0f, 90.0f);
        shape.AddArc(r - d, t, d, d, 270.0f, 90.0f);
        shape.AddLine(r, b, l, b);
        shape.CloseFigure();

        COLORREF fill = Theme::ListTitleFill, ink = Theme::ListTitleText;
        Gdiplus::SolidBrush fillBrush(Gdiplus::Color(GetRValue(fill), GetGValue(fill), GetBValue(fill)));
        g.FillPath(&fillBrush, &shape);

        Gdiplus::PointF cross[12];
        for (int i = 0; i < 12; i++)
            cross[i] = Gdiplus::PointF(ox + SF(kRcCrossSvg[i][0]), oy + SF(kRcCrossSvg[i][1]));
        Gdiplus::SolidBrush inkBrush(Gdiplus::Color(GetRValue(ink), GetGValue(ink), GetBValue(ink)));
        g.FillPolygon(&inkBrush, cross, 12);
    }

    const std::wstring caption = Tr(L"Список РЦ");
    RECT captionR = { X(52), bar.top, X(1990), bar.bottom };
    Theme::DrawLine(hDC, captionR, caption, m_rcFont, Theme::ListTitleText,
        DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);

    RECT close = { X(1998), bar.top, X(2046), bar.bottom };

    if (!Theme::InterInstalled())
    {
        const int captionRight = captionR.left + Theme::MeasureText(hDC, m_rcFont, caption).cx;
        RECT note = { captionRight + S(60), bar.top, close.left - S(30), bar.bottom };
        Theme::DrawLine(hDC, note, Tr(L"Шрифт Inter не установлен"), m_rcRowFont, Theme::SquawkMismatch,
            DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
    }
    RECT drag = { area.left, area.top, close.left, bar.bottom };
    RcObject(SO_RC_HEADER, "RC_HEADER", drag, true, Tr("Перетащите список РЦ"));
    RcObject(SO_RC_CLOSE, "RC_CLOSE", close, false, Tr("Закрыть"));

    const int grow0 = RcGrowSvg(m_rcPageRows[0]);
    const int growAll = grow0 + RcGrowSvg(m_rcPageRows[1]);
    for (int p = 0; p < 2; p++)
    {
        const std::vector<const SectorListRow*>& rows = p ? other : mine;
        const int pageRows = m_rcPageRows[p];
        const bool paged = (int)rows.size() > pageRows;

        int& scroll = p ? m_rcScroll : m_rcScrollMine;
        if (!paged || scroll < 0 || scroll >= (int)rows.size())
            scroll = 0;

        const int top = kRcPaneTopSvg[p] + (p ? grow0 : 0);
        const int bottom = kRcPaneBottomSvg[p] + (p ? growAll : grow0);
        RECT pane = { X(10), Y(top), X(2058), Y(bottom) };
        Theme::FlatFill(hDC, pane, Theme::ListPaneFill);

        RECT band = { X(10), Y(top), X(2058), Y(top + kRcHeadSvg) };
        Theme::FlatFill(hDC, band, Theme::ListHeadRule);
        for (int c = 0; c < kRcCols; c++)
        {
            RECT plate = { X(kRcColumns[c].svgLeft), Y(top + kRcPlateInsetSvg),
                           X(kRcColumns[c].svgRight), Y(top + kRcHeadSvg - kRcPlateInsetSvg) };
            Theme::FlatFill(hDC, plate, Theme::ListHeadFill);
            Theme::DrawLine(hDC, plate, Tr(kRcColumns[c].title), p ? m_rcFont : m_rcHeadFont, Theme::ListHeadText,
                DT_CENTER | DT_VCENTER | DT_END_ELLIPSIS);

            char id[8];
            sprintf_s(id, "%d", c);
            RcObject(SO_RC_SORT, id, plate, false, Tr("Сортировать по столбцу"));
        }

        for (int i = 0; i < pageRows; i++)
        {
            int index = scroll + i;
            if (index >= (int)rows.size())
                break;
            const SectorListRow& row = *rows[index];

            const int rowTop = top + kRcHeadSvg + kRcPlateInsetSvg + kRcRowPitchSvg * i;
            RECT line = { X(10), Y(rowTop), X(2058), Y(rowTop + kRcRowSvg) };
            Theme::FlatFill(hDC, line, row.east ? Theme::ListRowEast : Theme::ListRowWest);

            for (float d : kRcDividerSvg)
            {
                const int l = ox + (int)(SF(d) + 0.5f);
                RECT rule = { l, line.top, l + max(1, (int)(SF(kRcDividerWSvg) + 0.5f)), line.bottom };
                Theme::FlatFill(hDC, rule, Theme::ListRowRule);
            }

            for (int c = 0; c < kRcCols; c++)
            {
                COLORREF ink = Theme::ListText;
                HFONT font = m_rcRowFont;
                if (c == RC_KF && !row.cells[c].empty())
                {
                    ink = Theme::ListConflict;
                    font = m_rcKfFont;
                }
                else if (c == RC_CRD && !row.cells[c].empty())
                {
                    ink = (row.crdState == COORDINATION_STATE_ACCEPTED
                        || row.crdState == COORDINATION_STATE_MANUAL_ACCEPTED)
                        ? Theme::ListCrdOk : Theme::ListCrdReq;
                }

                RECT cell = { X(kRcColumns[c].svgLeft) + 1, line.top,
                              X(kRcColumns[c].svgRight) - 1, line.bottom };
                Theme::DrawLine(hDC, cell, row.cells[c], font, ink,
                    DT_CENTER | DT_VCENTER | DT_END_ELLIPSIS);
            }

            RcObject(SO_RC_ROW, row.callsign.c_str(), line, false,
                paged ? Tr("Выбрать борт (ПКМ - следующая страница)") : Tr("Выбрать борт"));
        }
    }

    {
        auto field = [&](int svgX, int svgY, const std::wstring& text, const char* id, const char* tip) -> RECT
        {
            const int fw = max(1, S(2));
            RECT r = { X(svgX) - fw / 2, Y(svgY) - fw / 2, X(svgX + 182) + (fw + 1) / 2, Y(svgY + 54) + (fw + 1) / 2 };
            Theme::FlatFill(hDC, r, Theme::ListPaneFill);
            Theme::FlatFrame(hDC, r, fw, Theme::ListFieldFrame);
            RECT inner = { r.left + S(12), r.top, r.right - S(8), r.bottom };
            Theme::DrawLine(hDC, inner, text, m_rcRowFont, Theme::ListTitleText,
                DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
            RcObject(SO_RC_FILTER, id, r, false, tip);
            return r;
        };
        auto label = [&](const RECT& f, int svgEdge, bool rightAligned, const std::wstring& text)
        {
            RECT r = rightAligned ? RECT{ area.left, f.top, X(svgEdge), f.bottom }
                                  : RECT{ X(svgEdge), f.top, f.left, f.bottom };
            Theme::DrawLine(hDC, r, text, m_rcFont, Theme::ListTitleText,
                (rightAligned ? DT_RIGHT : DT_LEFT) | DT_VCENTER);
        };
        auto minutes = [](int value) { return value >= 0 ? std::to_wstring(value) : std::wstring(); };

        RECT callsign = field(120, 825 + growAll, m_rcFilterCallsign, "callsign", Tr("Фильтр по рейсу"));
        label(callsign, 13, false, Tr(L"Рейс:"));

        RECT before = field(1467, 829 + growAll, minutes(m_rcFilterBefore), "before",
            Tr("За сколько минут до входа в сектор показывать рейс"));
        label(before, 1453, true, Tr(L"До (мин)"));

        RECT after = field(1865, 829 + growAll, minutes(m_rcFilterAfter), "after",
            Tr("Сколько минут держать рейс после выхода из сектора"));
        label(after, 1856, true, Tr(L"После (мин)"));
    }

    const int g = max(8, S(20));
    RECT grip = { area.right - g, area.bottom - g, area.right, area.bottom };
    {
        HPEN pen = CreatePen(PS_SOLID, 1, Theme::ListHeadRule);
        HPEN old = (HPEN)SelectObject(hDC, pen);
        for (int i = 1; i <= 3; i++)
        {
            const int d = (g - 2) * i / 3;
            MoveToEx(hDC, grip.right - 2 - d, grip.bottom - 2, NULL);
            LineTo(hDC, grip.right - 2, grip.bottom - 2 - d);
        }
        SelectObject(hDC, old);
        DeleteObject(pen);
    }
    RcObject(SO_RC_RESIZE, "RC_RESIZE", grip, true, Tr("Потяните, чтобы изменить размер"));

    m_rcDrawingFloat = false;
    RestoreDC(hDC, saved);
}

namespace
{
    HWND EuroScopeMainWindow()
    {
        struct Found { HWND hwnd; LONG area; } found = { NULL, 0 };
        EnumWindows([](HWND h, LPARAM lp) -> BOOL
            {
                DWORD pid = 0;
                GetWindowThreadProcessId(h, &pid);
                if (pid != GetCurrentProcessId() || !IsWindowVisible(h) || GetWindow(h, GW_OWNER) != NULL)
                    return TRUE;
                RECT r;
                GetWindowRect(h, &r);
                Found* f = (Found*)lp;
                const LONG area = (r.right - r.left) * (r.bottom - r.top);
                if (area > f->area)
                    *f = { h, area };
                return TRUE;
            }, (LPARAM)&found);
        return found.hwnd;
    }
}

bool CGalaxyATMSystemRadarScreen::CreateRcFloat(HWND owner)
{
    if (m_rcFloat.IsCreated())
        return true;
    if (owner == NULL || !m_rcFloat.Create(owner))
        return false;
    m_rcFloat.onHitTest = [this](POINT pt)
    {
        for (const RcHit& h : m_rcFloatHits)
            if (h.type == SO_RC_HEADER && PtInRect(&h.rect, pt))
                return FloatWindow::Hit::Caption;
        return FloatWindow::Hit::Client;
    };
    m_rcFloat.onMouse = [this](UINT msg, POINT pt) { RcFloatMouse(msg, pt); };
    m_rcFloat.onMoveStart = [this]() { m_rcEntry.Close(); };
    m_rcFloat.onMoved = [this]() { RcFloatMoved(); };
    return true;
}

bool CGalaxyATMSystemRadarScreen::UndockSectorList(RECT requested)
{
    HWND view = m_rcDragView;
    if (view == NULL || !IsWindow(view))
        return false;
    if (!CreateRcFloat(GetAncestor(view, GA_ROOT)))
        return false;

    const RECT ra = GetRadarArea();
    m_rcScale = RcScale(ra.right - ra.left, ra.bottom - ra.top);

    POINT at = { requested.left, requested.top };
    ClientToScreen(view, &at);
    m_rcFloatPos = at;
    m_rcFloating = true;
    m_rcFloat.MoveTo(at);
    RenderRcFloat();
    m_rcFloat.ContinueDrag();

    m_dragOffset = { 0, 0 };
    RequestRefresh();
    Log::Info("rc", "sector list taken off the radar");
    return true;
}

void CGalaxyATMSystemRadarScreen::RenderRcFloat()
{
    if (!m_rcFloat.IsCreated())
    {
        if (!CreateRcFloat(EuroScopeMainWindow()))
            return;
        if (MonitorFromPoint(m_rcFloatPos, MONITOR_DEFAULTTONULL) == NULL)
            m_rcFloatPos = { 100, 100 };
        m_rcFloat.MoveTo(m_rcFloatPos);
    }

    std::vector<SectorListRow> all;
    BuildSectorList(all);

    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(MonitorFromWindow(m_rcFloat.Handle(), MONITOR_DEFAULTTONEAREST), &mi);
    const int scale = RcScale(mi.rcWork.right - mi.rcWork.left, mi.rcWork.bottom - mi.rcWork.top);
    const int svgH = PlanSectorList(all, (mi.rcWork.bottom - m_rcFloat.Position().y) * 100 / scale);
    const int W = (kRcSvgW * scale + 50) / 100, H = (svgH * scale + 50) / 100;

    HDC dc = m_rcFloat.BeginFrame(W, H);
    if (dc == NULL)
        return;
    RECT area = { 0, 0, W, H };
    DrawSectorList(dc, area, scale, true, all);
    m_rcFloat.EndFrame();
    m_rcFloatDrawn = GetTickCount64();
}

void CGalaxyATMSystemRadarScreen::RcFloatMouse(UINT msg, POINT pt)
{
    if (m_rcFloatResizing)
    {
        const int width = pt.x + m_rcFloatGrab;
        m_rcScale = max(kRcScaleMin, min(kRcScaleMax, (int)lround(width * 100.0 / kRcSvgW)));
        if (msg == WM_LBUTTONUP)
        {
            m_rcFloatResizing = false;
            m_rcFloat.ReleaseMouse();
        }
        RenderRcFloat();
        return;
    }
    if (msg == WM_MOUSEMOVE)
        return;

    const RcHit* hit = NULL;
    for (auto it = m_rcFloatHits.rbegin(); it != m_rcFloatHits.rend(); ++it)
    {
        if (PtInRect(&it->rect, pt))
        {
            hit = &*it;
            break;
        }
    }
    if (hit == NULL)
        return;

    if (msg == WM_LBUTTONDOWN && hit->type == SO_RC_RESIZE)
    {
        RECT r;
        GetClientRect(m_rcFloat.Handle(), &r);
        m_rcFloatResizing = true;
        m_rcFloatGrab = r.right - pt.x;
        m_rcEntry.Close();
        m_rcFloat.CaptureMouse();
        return;
    }
    if (msg != WM_LBUTTONUP && msg != WM_RBUTTONUP)
        return;
    const int button = (msg == WM_LBUTTONUP) ? BUTTON_LEFT : BUTTON_RIGHT;

    const RcHit h = *hit;
    m_rcEntry.Close();
    if (h.type == SO_RC_FILTER)
    {
        if (button != BUTTON_LEFT)
            return;
        const bool isCallsign = h.id == "callsign";
        const bool isBefore = h.id == "before";
        const int fn = isCallsign ? FN_RC_FILTER_CALLSIGN : isBefore ? FN_RC_FILTER_BEFORE : FN_RC_FILTER_AFTER;
        std::wstring initial = m_rcFilterCallsign;
        if (!isCallsign)
        {
            const int value = isBefore ? m_rcFilterBefore : m_rcFilterAfter;
            initial = value >= 0 ? std::to_wstring(value) : std::wstring();
        }
        m_rcEntry.Open(m_rcFloat.Handle(), h.rect, m_rcRowFont, initial, false, isCallsign ? 10 : 4,
            [this, fn](TextEntry::End end)
            {
                if (end != TextEntry::End::Cancel)
                    ApplyRcFilter(fn, m_rcEntry.Text());
                m_rcEntry.Close();
                RenderRcFloat();
            });
        return;
    }
    if (h.type == SO_RC_CLOSE)
    {
        m_rcOpen = false;
        m_rcFloat.Hide();
        RequestRefresh();
        return;
    }

    RECT screenArea = h.rect;
    OnClickScreenObject(h.type, h.id.c_str(), pt, screenArea, button);
    RenderRcFloat();
}

void CGalaxyATMSystemRadarScreen::RcFloatMoved()
{
    RECT wr;
    GetWindowRect(m_rcFloat.Handle(), &wr);
    m_rcFloatPos = { wr.left, wr.top };

    HWND owner = GetWindow(m_rcFloat.Handle(), GW_OWNER);
    if (owner == NULL || IsIconic(owner))
        return;

    const POINT mid = { (wr.left + wr.right) / 2, (wr.top + wr.bottom) / 2 };
    std::vector<HWND> chain;
    for (HWND h = owner; h != NULL && chain.size() < 16; )
    {
        chain.push_back(h);
        POINT p = mid;
        ScreenToClient(h, &p);
        HWND child = ChildWindowFromPointEx(h, p, CWP_SKIPINVISIBLE | CWP_SKIPTRANSPARENT);
        h = (child == h) ? NULL : child;
    }

    const RECT ra = GetRadarArea();
    for (auto it = chain.rbegin(); it != chain.rend(); ++it)
    {
        POINT tl = { wr.left, wr.top }, br = { wr.right, wr.bottom };
        ScreenToClient(*it, &tl);
        ScreenToClient(*it, &br);
        if (tl.x >= ra.left && tl.y >= ra.top && br.x <= ra.right && br.y <= ra.bottom)
        {
            m_rcArea = { tl.x, tl.y, br.x, br.y };
            m_rcPositioned = true;
            m_rcFloating = false;
            m_rcEntry.Close();
            m_rcFloat.Hide();
            RequestRefresh();
            Log::Info("rc", "sector list put back on the radar");
            return;
        }
    }
}

void CGalaxyATMSystemRadarScreen::TickRcFloat()
{
    if (m_rcFloat.Visible() && !m_rcFloatResizing && GetTickCount64() - m_rcFloatDrawn > 3000)
    {
        m_rcEntry.Close();
        m_rcFloat.Hide();
    }
}

void CGalaxyATMSystemRadarScreen::ScrollAtisTo(POINT pt, RECT track)
{
    int usable = (track.bottom - track.top) - m_atisThumbH;
    if (usable <= 0 || m_atisScrollMax <= 0)
        return;

    int rel = pt.y - track.top - m_atisThumbH / 2;
    rel = max(0, min(usable, rel));
    m_atisScrollPx = (int)((__int64)rel * m_atisScrollMax / usable);
}

void CGalaxyATMSystemRadarScreen::SetVvGainFrom(POINT pt)
{
    int h = m_vvSliderRect.bottom - m_vvSliderRect.top;
    if (h <= 0)
        return;

    int rel = m_vvSliderRect.bottom - pt.y;
    m_vvGain = max(0, min(100, MulDiv(rel, 100, h)));
    ApplyZoomFromSlider();
}

namespace
{
    const double kZoomMinSpanNM = 6.0;
    const double kZoomMaxSpanNM = 900.0;
}

double CGalaxyATMSystemRadarScreen::GainToSpanNM(int gain)
{
    double t = max(0, min(100, gain)) / 100.0;
    return kZoomMaxSpanNM * pow(kZoomMinSpanNM / kZoomMaxSpanNM, t);
}

int CGalaxyATMSystemRadarScreen::SpanNMToGain(double spanNM)
{
    if (spanNM <= 0.0)
        return 100;
    double t = log(spanNM / kZoomMaxSpanNM) / log(kZoomMinSpanNM / kZoomMaxSpanNM);
    return max(0, min(100, (int)lround(t * 100.0)));
}

double CGalaxyATMSystemRadarScreen::DisplaySpanNM()
{
    CPosition leftDown, rightUp;
    GetDisplayArea(&leftDown, &rightUp);
    return fabs(rightUp.m_Latitude - leftDown.m_Latitude) * 60.0;
}

double CGalaxyATMSystemRadarScreen::DisplayWidthNM()
{
    CPosition leftDown, rightUp;
    GetDisplayArea(&leftDown, &rightUp);

    double dLon = fabs(rightUp.m_Longitude - leftDown.m_Longitude);
    if (dLon > 180.0)
        dLon = 360.0 - dLon;

    double midLat = (leftDown.m_Latitude + rightUp.m_Latitude) / 2.0;
    return dLon * 60.0 * cos(midLat * M_PI / 180.0);
}

void CGalaxyATMSystemRadarScreen::ApplyZoomFromSlider()
{
    CPosition leftDown, rightUp;
    GetDisplayArea(&leftDown, &rightUp);

    double current = fabs(rightUp.m_Latitude - leftDown.m_Latitude) * 60.0;
    if (current <= 0.0)
        return;

    double k = GainToSpanNM(m_vvGain) / current;
    if (k <= 0.0 || fabs(k - 1.0) < 0.005)
        return;

    double cLat = (leftDown.m_Latitude + rightUp.m_Latitude) / 2.0;
    double cLon = (leftDown.m_Longitude + rightUp.m_Longitude) / 2.0;

    CPosition newLeftDown, newRightUp;
    newLeftDown.m_Latitude  = cLat + (leftDown.m_Latitude  - cLat) * k;
    newLeftDown.m_Longitude = cLon + (leftDown.m_Longitude - cLon) * k;
    newRightUp.m_Latitude   = cLat + (rightUp.m_Latitude   - cLat) * k;
    newRightUp.m_Longitude  = cLon + (rightUp.m_Longitude  - cLon) * k;

    SetDisplayArea(newLeftDown, newRightUp);
}

void CGalaxyATMSystemRadarScreen::SyncSliderFromZoom()
{
    m_vvGain = SpanNMToGain(DisplaySpanNM());
}

COLORREF CGalaxyATMSystemRadarScreen::GetTagColorForFlightPlan(CFlightPlan fp)
{
    if (!fp.IsValid())
        return RGB(150, 150, 150);

    int state = fp.GetState();
    if (state == FLIGHT_PLAN_STATE_ASSUMED)
        return RGB(255, 255, 255);
    if (state == FLIGHT_PLAN_STATE_TRANSFER_TO_ME_INITIATED)
        return RGB(0, 255, 255);
    if (state == FLIGHT_PLAN_STATE_TRANSFER_FROM_ME_INITIATED)
        return RGB(255, 128, 0);
    return RGB(150, 150, 150);
}

CPosition CGalaxyATMSystemRadarScreen::CalculateDestinationPoint(CPosition start, double bearingDeg, double distanceNM)
{
    const double R = 3440.065;

    double lat1 = start.m_Latitude * M_PI / 180.0;
    double lon1 = start.m_Longitude * M_PI / 180.0;
    double bearing = bearingDeg * M_PI / 180.0;
    double angularDistance = distanceNM / R;

    double lat2 = asin(sin(lat1) * cos(angularDistance) + cos(lat1) * sin(angularDistance) * cos(bearing));
    double lon2 = lon1 + atan2(sin(bearing) * sin(angularDistance) * cos(lat1),
        cos(angularDistance) - sin(lat1) * sin(lat2));

    CPosition dest;
    dest.m_Latitude = lat2 * 180.0 / M_PI;
    dest.m_Longitude = lon2 * 180.0 / M_PI;
    return dest;
}

void CGalaxyATMSystemRadarScreen::DrawTrackVector(HDC hDC, CRadarTarget rt, double lengthNM,
    double timeMinForLevel, int minuteTicks, COLORREF color)
{
    CRadarTargetPositionData pos = rt.GetPosition();
    double trackHeading = rt.GetTrackHeading();
    CPosition currentPos = pos.GetPosition();

    CPosition endPos = CalculateDestinationPoint(currentPos, trackHeading, lengthNM);
    POINT p0 = ConvertCoordFromPositionToPixel(currentPos);
    POINT p1 = ConvertCoordFromPositionToPixel(endPos);

    double dx = p1.x - p0.x, dy = p1.y - p0.y;
    double totalLen = sqrt(dx * dx + dy * dy);
    if (totalLen < 1.0)
        return;

    double heading = atan2(dy, dx);

    double levelFrac = 1.0;
    int verticalSpeed = rt.GetVerticalSpeed();
    CFlightPlan vecFp = rt.GetCorrelatedFlightPlan();
    if (vecFp.IsValid() && abs(verticalSpeed) > 100 && timeMinForLevel > 0.0)
    {
        int clearedFt = vecFp.GetClearedAltitude();
        if (clearedFt > 0)
        {
            bool clearedIsFL = clearedFt / 100 >= Plugin()->TransitionLevelFL();
            int currentFt = clearedIsFL ? pos.GetFlightLevel() : pos.GetPressureAltitude();

            double minutesToLevel = (double)(clearedFt - currentFt) / verticalSpeed;
            if (minutesToLevel > 0.0)
                levelFrac = min(1.0, minutesToLevel / timeMinForLevel);
        }
    }

    POINT pMark;
    pMark.x = p0.x + (int)lround((p1.x - p0.x) * levelFrac);
    pMark.y = p0.y + (int)lround((p1.y - p0.y) * levelFrac);

    const double arrowAngle = 28.0 * M_PI / 180.0;
    double headLength = min(Theme::VectorHeadLength, totalLen * levelFrac * 0.4);

    Gdiplus::PointF chevron[3] = {
        Gdiplus::PointF((Gdiplus::REAL)(pMark.x + headLength * cos(heading + M_PI - arrowAngle)),
                        (Gdiplus::REAL)(pMark.y + headLength * sin(heading + M_PI - arrowAngle))),
        Gdiplus::PointF((Gdiplus::REAL)pMark.x, (Gdiplus::REAL)pMark.y),
        Gdiplus::PointF((Gdiplus::REAL)(pMark.x + headLength * cos(heading + M_PI + arrowAngle)),
                        (Gdiplus::REAL)(pMark.y + headLength * sin(heading + M_PI + arrowAngle))),
    };

    bool isLevel = abs(verticalSpeed) <= 100;

    {
        VectorCanvas canvas(hDC, color);

        if (minuteTicks >= 2)
        {
            double ux = (p1.x - p0.x) / totalLen, uy = (p1.y - p0.y) / totalLen;
            double segLen = totalLen / minuteTicks;
            double gap = min(Theme::VectorTickGap, segLen * 0.25);

            for (int i = 0; i < minuteTicks; i++)
            {
                double from = i * segLen;
                double to = (i + 1) * segLen - gap;
                canvas.Line(p0.x + ux * from, p0.y + uy * from, p0.x + ux * to, p0.y + uy * to);
            }
        }
        else
            canvas.Line(p0.x, p0.y, p1.x, p1.y);

        if (headLength > 1.0 && !isLevel)
        {
            canvas.pen.SetWidth(Theme::VectorHeadWidth);
            canvas.g.DrawLines(&canvas.pen, chevron, 3);
        }
    }

    if (m_vecShowLevel && !isLevel)
    {
        double labelTimeMin = timeMinForLevel * levelFrac;
        int climbFt = (int)(verticalSpeed * labelTimeMin);

        bool belowTL = (pos.GetFlightLevel() + climbFt) / 100 < Plugin()->TransitionLevelFL();
        int currentAltFt = belowTL ? pos.GetPressureAltitude() : pos.GetFlightLevel();
        int predictedAltFt = currentAltFt + climbFt;
        if (predictedAltFt < 0)
            predictedAltFt = 0;

        wchar_t trend = (verticalSpeed > 100) ? L'\x2191' : (verticalSpeed < -100) ? L'\x2193' : L' ';
        AltUnit vecUnit = (Plugin()->UnitAlt() == AltUnit::M) ? AltUnit::M : AltUnit::FL;
        std::wstring label = Widen(FormatAltitudeUnit(predictedAltFt, vecUnit).c_str()) + trend;

        RECT r = { pMark.x + 6, pMark.y - 16, pMark.x + 110, pMark.y };
        SetBkMode(hDC, TRANSPARENT);
        Theme::DrawLine(hDC, r, label, m_esFont ? m_esFont : m_fonts.Small, color,
            DT_LEFT | DT_VCENTER);
    }
}

void CGalaxyATMSystemRadarScreen::DrawPlanVector(HDC hDC, CFlightPlan fp, const CPosition& currentPos,
    int minutes, COLORREF color)
{
    if (!fp.IsValid() || minutes <= 0)
        return;

    CFlightPlanPositionPredictions pred = fp.GetPositionPredictions();
    int count = pred.GetPointsNumber();
    if (count <= 1)
        return;
    if (minutes >= count)
        minutes = count - 1;

    std::vector<POINT> pts;
    pts.push_back(ConvertCoordFromPositionToPixel(currentPos));
    for (int i = 1; i <= minutes; i++)
        pts.push_back(ConvertCoordFromPositionToPixel(pred.GetPosition(i)));

    if (pts.size() < 2)
        return;
    const POINT& p0 = pts.front();
    const POINT& p1 = pts.back();
    if (abs(p1.x - p0.x) < 1 && abs(p1.y - p0.y) < 1)
        return;

    if (minutes >= 2)
        DrawGappedPolyline(hDC, pts, color, Theme::VectorTickGap);
    else
        VectorCanvas(hDC, color).Line(p0.x, p0.y, p1.x, p1.y);
}

void CGalaxyATMSystemRadarScreen::DrawTargetVectors(HDC hDC)
{
    int saved = SaveDC(hDC);
    SetBkMode(hDC, TRANSPARENT);

    for (CRadarTarget rt = GetPlugIn()->RadarTargetSelectFirst(); rt.IsValid();
         rt = GetPlugIn()->RadarTargetSelectNext(rt))
    {
        CRadarTargetPositionData pos = rt.GetPosition();
        if (!pos.IsValid())
            continue;
        if (pos.GetPressureAltitude() < 700)
            continue;

        if (!Plugin()->AltFilterPasses(pos.GetPressureAltitude()))
            continue;

        int groundSpeed = rt.GetGS();
        if (groundSpeed < 10)
            continue;

        CFlightPlan fp = rt.GetCorrelatedFlightPlan();
        COLORREF color = GetTagColorForFlightPlan(fp);

        if (m_vecTimeEnabled)
        {
            double lengthNM = (groundSpeed / 60.0) * m_vecTimeMin;
            DrawTrackVector(hDC, rt, lengthNM, (double)m_vecTimeMin, m_vecTimeMin, color);
        }

        if (m_vecDistEnabled)
        {
            double lengthNM = m_vecDistKm / 1.852;
            double timeMin = (groundSpeed > 0) ? (lengthNM / (groundSpeed / 60.0)) : 0.0;
            DrawTrackVector(hDC, rt, lengthNM, timeMin, 0, color);
        }

        if (m_vecByPlan && fp.IsValid() && m_vecTimeMin > 0)
        {
            DrawPlanVector(hDC, fp, pos.GetPosition(), m_vecTimeMin, color);
        }
    }

    RestoreDC(hDC, saved);
}

void CGalaxyATMSystemRadarScreen::DrawWakeArcs(HDC hDC)
{
    for (CRadarTarget rt = GetPlugIn()->RadarTargetSelectFirst(); rt.IsValid();
         rt = GetPlugIn()->RadarTargetSelectNext(rt))
    {
        CRadarTargetPositionData pos = rt.GetPosition();
        if (!pos.IsValid())
            continue;

        if (pos.GetPressureAltitude() < 700)
            continue;
        if (!Plugin()->AltFilterPasses(pos.GetPressureAltitude()))
            continue;

        CFlightPlan fp = rt.GetCorrelatedFlightPlan();
        if (!fp.IsValid())
            continue;
        char wtc = fp.GetFlightPlanData().GetAircraftWtc();
        int arcs = (wtc == 'J') ? 2 : (wtc == 'H') ? 1 : 0;
        if (arcs == 0)
            continue;

        const double kAheadNM = 20.0;
        CPosition here = pos.GetPosition();
        POINT c = ConvertCoordFromPositionToPixel(here);
        POINT ahead = ConvertCoordFromPositionToPixel(
            CalculateDestinationPoint(here, rt.GetTrackHeading(), kAheadNM));
        double dx = ahead.x - c.x, dy = ahead.y - c.y;
        if (dx * dx + dy * dy < 1.0)
            continue;

        // short arcs centred on the target, fixed size
        double behindDeg = atan2(dy, dx) * 180.0 / M_PI + 180.0;

        VectorCanvas canvas(hDC, GetTagColorForFlightPlan(fp));
        canvas.pen.SetWidth(Theme::WakeArcWidth);
        canvas.pen.SetStartCap(Gdiplus::LineCapRound);
        canvas.pen.SetEndCap(Gdiplus::LineCapRound);
        for (int i = 0; i < arcs; i++)
        {
            double r = Theme::WakeArcRadius + i * Theme::WakeArcStep;
            canvas.g.DrawArc(&canvas.pen,
                (Gdiplus::REAL)(c.x - r), (Gdiplus::REAL)(c.y - r),
                (Gdiplus::REAL)(2.0 * r), (Gdiplus::REAL)(2.0 * r),
                (Gdiplus::REAL)(behindDeg - Theme::WakeArcSweep / 2.0),
                (Gdiplus::REAL)Theme::WakeArcSweep);
        }
    }
}

bool CGalaxyATMSystemRadarScreen::FindNearbyTarget(POINT pt, std::string& callsignOut)
{
    const double thresholdPx = 20.0;
    double bestDist = thresholdPx;
    bool found = false;

    for (CRadarTarget rt = GetPlugIn()->RadarTargetSelectFirst(); rt.IsValid();
         rt = GetPlugIn()->RadarTargetSelectNext(rt))
    {
        CRadarTargetPositionData pos = rt.GetPosition();
        if (!pos.IsValid())
            continue;

        POINT p = ConvertCoordFromPositionToPixel(pos.GetPosition());
        double dx = p.x - pt.x, dy = p.y - pt.y;
        double d = sqrt(dx * dx + dy * dy);
        if (d < bestDist)
        {
            bestDist = d;
            callsignOut = rt.GetCallsign();
            found = true;
        }
    }
    return found;
}

CPosition CGalaxyATMSystemRadarScreen::ResolveRulerPoint(bool snapped, const std::string& callsign, CPosition& fixed)
{
    if (snapped)
    {
        CRadarTarget rt = GetPlugIn()->RadarTargetSelect(callsign.c_str());
        if (rt.IsValid())
        {
            CRadarTargetPositionData pos = rt.GetPosition();
            if (pos.IsValid())
                fixed = pos.GetPosition();
        }
    }
    return fixed;
}

double CGalaxyATMSystemRadarScreen::GetRulerSpeedKt(const RulerLine& r)
{
    if (!r.startSnapped)
        return -1.0;
    CRadarTarget rt = GetPlugIn()->RadarTargetSelect(r.startCallsign.c_str());
    return rt.IsValid() ? rt.GetGS() : -1.0;
}

int CGalaxyATMSystemRadarScreen::FindNearestRulerIndex(POINT pt, double thresholdPx)
{
    double bestDist = thresholdPx;
    int bestIndex = -1;

    for (size_t i = 0; i < m_rulers.size(); i++)
    {
        RulerLine& r = m_rulers[i];
        POINT a = ConvertCoordFromPositionToPixel(ResolveRulerPoint(r.startSnapped, r.startCallsign, r.startFixed));
        POINT b = ConvertCoordFromPositionToPixel(ResolveRulerPoint(r.endSnapped, r.endCallsign, r.endFixed));

        double dx = b.x - a.x, dy = b.y - a.y;
        double lenSq = dx * dx + dy * dy;
        double t = (lenSq > 1e-6) ? ((pt.x - a.x) * dx + (pt.y - a.y) * dy) / lenSq : 0.0;
        t = max(0.0, min(1.0, t));
        double px = a.x + t * dx, py = a.y + t * dy;
        double dist = sqrt((pt.x - px) * (pt.x - px) + (pt.y - py) * (pt.y - py));

        if (dist < bestDist)
        {
            bestDist = dist;
            bestIndex = (int)i;
        }
    }
    return bestIndex;
}

void CGalaxyATMSystemRadarScreen::PollRulerButton()
{
    CloseSigmetInfoIfButtonReleased();

    bool shift = ShiftHeldInEuroScope();
    if (shift != m_areaShiftDown)
    {
        m_areaShiftDown = shift;
        RequestRefresh();
    }

    {
        std::string hover;
        POINT cursor;
        if (m_formularsVisible && CursorRadarPoint(cursor))
        {
            for (const auto& entry : m_formulars)
            {
                if (!entry.second.items.empty() && PtInRect(&entry.second.area, cursor))
                {
                    hover = entry.first;
                    break;
                }
            }
        }
        if (hover != m_formularHover)
        {
            m_formularHover = hover;
            RequestRefresh();
        }
    }

    if (m_hdgDragging)
    {
        const bool left = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        const bool right = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
        m_hdgReleaseTicks = left ? 0 : m_hdgReleaseTicks + 1;
        if (right || m_hdgReleaseTicks >= 3)
        {
            m_hdgDragging = false;
            m_hdgDragMoved = false;
            m_hdgDragCancelled = left;
            m_hdgDragEndTick = GetTickCount64();
            m_hdgReleaseTicks = 0;
            RequestRefresh();
        }
    }
    else
    {
        m_hdgReleaseTicks = 0;
        if (m_hdgDragCancelled && (GetAsyncKeyState(VK_LBUTTON) & 0x8000) == 0)
            m_hdgDragCancelled = false;
    }

    if (m_rulerButton != 0)
    {
        HWND fg = GetForegroundWindow();
        DWORD pid = 0;
        if (fg != NULL)
            GetWindowThreadProcessId(fg, &pid);
        if (pid != GetCurrentProcessId())
        {
            m_rulerButtonDown = false;
            return;
        }

        bool down = (GetAsyncKeyState(m_rulerButton) & 0x8000) != 0;
        if (down && !m_rulerButtonDown)
            m_rulerPressPending = true;
        m_rulerButtonDown = down;
    }

    if (m_rulerPressPending || m_rulerArmed || m_rulerPlacing)
        RequestRefresh();
}

bool CGalaxyATMSystemRadarScreen::CursorRadarPoint(POINT& out, HWND* view)
{
    POINT scr;
    if (!GetCursorPos(&scr))
        return false;

    HWND under = WindowFromPoint(scr);
    if (under == NULL)
        return false;

    DWORD pid = 0;
    GetWindowThreadProcessId(under, &pid);
    if (pid != GetCurrentProcessId())
        return false;

    RECT ra = GetRadarArea();
    for (HWND h : { under, GetAncestor(under, GA_ROOT) })
    {
        if (h == NULL)
            continue;
        POINT p = scr;
        if (!ScreenToClient(h, &p))
            continue;
        if (PtInRect(&ra, p))
        {
            out = p;
            if (view != NULL)
                *view = h;
            return true;
        }
    }
    return false;
}

void CGalaxyATMSystemRadarScreen::PlaceRulerPoint(POINT pt)
{
    if (!m_rulerPlacing)
    {
        m_rulerPending = RulerLine();
        std::string cs;
        m_rulerPending.startSnapped = FindNearbyTarget(pt, cs);
        if (m_rulerPending.startSnapped)
            m_rulerPending.startCallsign = cs;
        else
            m_rulerPending.startFixed = ConvertCoordFromPixelToPosition(pt);
        m_rulerPlacing = true;
        UpdateRulerEnd(pt);
        return;
    }

    UpdateRulerEnd(pt);

    POINT a = ConvertCoordFromPositionToPixel(ResolveRulerPoint(
        m_rulerPending.startSnapped, m_rulerPending.startCallsign, m_rulerPending.startFixed));
    POINT b = ConvertCoordFromPositionToPixel(ResolveRulerPoint(
        m_rulerPending.endSnapped, m_rulerPending.endCallsign, m_rulerPending.endFixed));
    if (abs(a.x - b.x) > 4 || abs(a.y - b.y) > 4)
        m_rulers.push_back(m_rulerPending);
    m_rulerPlacing = false;
    m_rulerArmed = false;
}

void CGalaxyATMSystemRadarScreen::DrawRulerCursor(HDC hDC)
{
    POINT pt;
    if (!CursorRadarPoint(pt))
        return;

    int saved = SaveDC(hDC);
    HPEN pen = CreatePen(PS_SOLID, (int)Theme::RulerWidth, Theme::Ruler);
    HPEN oldPen = (HPEN)SelectObject(hDC, pen);

    const int arm = 10, gap = 3;
    MoveToEx(hDC, pt.x - arm, pt.y, NULL);      LineTo(hDC, pt.x - gap, pt.y);
    MoveToEx(hDC, pt.x + gap + 1, pt.y, NULL);  LineTo(hDC, pt.x + arm + 1, pt.y);
    MoveToEx(hDC, pt.x, pt.y - arm, NULL);      LineTo(hDC, pt.x, pt.y - gap);
    MoveToEx(hDC, pt.x, pt.y + gap + 1, NULL);  LineTo(hDC, pt.x, pt.y + arm + 1);

    SelectObject(hDC, oldPen);
    DeleteObject(pen);
    RestoreDC(hDC, saved);
}

void CGalaxyATMSystemRadarScreen::UpdateRulerEnd(POINT pt)
{
    std::string cs;
    m_rulerPending.endSnapped = FindNearbyTarget(pt, cs);
    if (m_rulerPending.endSnapped)
        m_rulerPending.endCallsign = cs;
    else
        m_rulerPending.endFixed = ConvertCoordFromPixelToPosition(pt);
}

HFONT CGalaxyATMSystemRadarScreen::GetRulerFont(HFONT baseFont)
{
    if (baseFont == NULL)
        return NULL;
    if (m_rulerFont != NULL && m_rulerFontSource == baseFont)
        return m_rulerFont;

    LOGFONTW lf;
    if (GetObjectW(baseFont, sizeof(lf), &lf) == 0)
        return baseFont;

    lf.lfHeight = (LONG)lround(lf.lfHeight * 0.85);

    HFONT scaled = CreateFontIndirectW(&lf);
    if (scaled == NULL)
        return baseFont;

    if (m_rulerFont != NULL)
        DeleteObject(m_rulerFont);
    m_rulerFont = scaled;
    m_rulerFontSource = baseFont;
    return m_rulerFont;
}

void CGalaxyATMSystemRadarScreen::DrawRulerLine(HDC hDC, RulerLine& r, int index)
{
    CPosition startPos = ResolveRulerPoint(r.startSnapped, r.startCallsign, r.startFixed);
    CPosition endPos = ResolveRulerPoint(r.endSnapped, r.endCallsign, r.endFixed);

    POINT p0 = ConvertCoordFromPositionToPixel(startPos);
    POINT p1 = ConvertCoordFromPositionToPixel(endPos);

    double dx = p1.x - p0.x, dy = p1.y - p0.y;
    double len = sqrt(dx * dx + dy * dy);
    if (len < 2.0)
        return;

    int saved = SaveDC(hDC);
    SetBkMode(hDC, TRANSPARENT);

    int midX = (p0.x + p1.x) / 2, midY = (p0.y + p1.y) / 2;
    double ux = dx / len, uy = dy / len;
    double perpX = -uy, perpY = ux;
    if (perpY > 0) { perpX = -perpX; perpY = -perpY; }
    const double tickLen = 26.0;
    POINT tickTop = { midX + (int)(perpX * tickLen), midY + (int)(perpY * tickLen) };

    r.labelAnchor = tickTop;
    POINT labelAt = { tickTop.x + r.labelOffset.x, tickTop.y + r.labelOffset.y };

    double bearing = startPos.DirectionTo(endPos);
    bearing = fmod(bearing + 360.0, 360.0);
    double nm = startPos.DistanceTo(endPos);
    double km = nm * 1.852;
    double speedKt = GetRulerSpeedKt(r);

    wchar_t line1[16], line2[32], line3[16];
    swprintf_s(line1, L"%03d", ((int)(bearing + 0.5)) % 360);
    swprintf_s(line2, L"%.1f km(%.1f)", km, nm);
    if (speedKt > 0.1)
    {
        int totalSec = (int)((nm / speedKt) * 3600.0 + 0.5);
        swprintf_s(line3, L"%02d:%02d", totalSec / 60, totalSec % 60);
    }
    else
    {
        swprintf_s(line3, L"--:--");
    }

    HFONT rulerFont = GetRulerFont(m_esFont ? m_esFont : m_fonts.Ruler);

    int rowH = 15;
    {
        HFONT oldFont = (HFONT)SelectObject(hDC, rulerFont);
        TEXTMETRICW tm;
        if (GetTextMetricsW(hDC, &tm))
            rowH = tm.tmHeight + tm.tmExternalLeading;
        SelectObject(hDC, oldFont);
    }

    const wchar_t* const rowText[3] = { line1, line2, line3 };
    int rowW[3] = { 0, 0, 0 };
    int textW = 0;
    for (int i = 0; i < 3; i++)
    {
        rowW[i] = (int)Theme::MeasureText(hDC, rulerFont, rowText[i]).cx;
        textW = max(textW, rowW[i]);
    }

    int left = labelAt.x - 4;
    RECT r3 = { left, labelAt.y - rowH, left + 150, labelAt.y };
    RECT r2 = { left, r3.top - rowH, left + 150, r3.top };
    RECT r1 = { left, r2.top - rowH, left + 150, r2.top };
    r.labelRect = { left, r1.top, left + textW + 8, r3.bottom };

    std::vector<RECT> rows;
    const RECT rowRects[3] = { r1, r2, r3 };
    for (int i = 0; i < 3; i++)
    {
        RECT row = { rowRects[i].left, rowRects[i].top, rowRects[i].left + rowW[i],
                     rowRects[i].bottom };
        InflateRect(&row, 3, 3);
        rows.push_back(row);
    }

    POINT from = { midX, midY };
    POINT leaderFrom, leaderTo;
    const bool drawLeader = ClipLeaderToText(from, labelAt, rows, 0.0, leaderFrom, leaderTo);
    {
        VectorCanvas canvas(hDC, Theme::Ruler, Theme::RulerWidth);
        canvas.Line(p0.x, p0.y, p1.x, p1.y);
        if (drawLeader)
            canvas.Line(leaderFrom.x, leaderFrom.y, leaderTo.x, leaderTo.y);
    }

    Theme::DrawLine(hDC, r1, line1, rulerFont, Theme::Ruler, DT_LEFT | DT_VCENTER);
    Theme::DrawLine(hDC, r2, line2, rulerFont, Theme::Ruler, DT_LEFT | DT_VCENTER);
    Theme::DrawLine(hDC, r3, line3, rulerFont, Theme::Ruler, DT_LEFT | DT_VCENTER);

    if (index >= 0 && !m_rulerArmed && !m_rulerPlacing)
    {
        char id[16];
        sprintf_s(id, "%d", index);
        AddScreenObject(SO_RULER_LABEL, id, r.labelRect, true, Tr("Перетащить информацию линейки"));
    }

    RestoreDC(hDC, saved);
}

void CGalaxyATMSystemRadarScreen::OpenAltFilterPicker(RECT area, bool isFrom)
{
    int current = isFrom ? Plugin()->AltFilterFromFL() : Plugin()->AltFilterToFL();
    char initial[8];
    sprintf_s(initial, "%03d", current);
    GetPlugIn()->OpenPopupEdit(area, isFrom ? FN_ALTFILTER_FROM : FN_ALTFILTER_TO, initial);
}

void CGalaxyATMSystemRadarScreen::RemoveRulerNear(POINT pt)
{
    int idx = FindNearestRulerIndex(pt, 15.0);
    if (idx >= 0)
        m_rulers.erase(m_rulers.begin() + idx);
    RequestRefresh();
}

void CGalaxyATMSystemRadarScreen::OnClickScreenObject(int ObjectType, const char* sObjectId,
    POINT Pt, RECT Area, int Button)
{
    if (ObjectType == SO_FORMULAR || ObjectType == SO_FORMULAR_AHDG)
    {
        FormularClick(sObjectId, Pt, Button);
        return;
    }

    if (ObjectType == SO_RULER_LINE)
    {
        if (Button == BUTTON_RIGHT)
            RemoveRulerNear(Pt);
        return;
    }

    if (ObjectType == SO_RULER_CANVAS)
    {
        if (Button == BUTTON_LEFT)
        {
            PlaceRulerPoint(Pt);
        }
        else if (Button == BUTTON_RIGHT)
        {
            m_rulerArmed = false;
            m_rulerPlacing = false;
        }
        RequestRefresh();
        return;
    }

    if (ObjectType == SO_SIGMET_AREA)
        return;

    if (ObjectType != SO_DROPDOWN_ITEM &&
        ObjectType != SO_VEC_DIST_FIELD && ObjectType != SO_VEC_TIME_FIELD &&
        ObjectType != SO_OS_FONT_FIELD)
    {
        m_openDropdown = DropdownKind::None;
    }

    switch (ObjectType)
    {
    case SO_DROPDOWN_ITEM:
    {
        int idx = atoi(sObjectId);
        if (m_openDropdown == DropdownKind::VecDist && idx >= 0 && idx < kDistanceStepsCount)
            m_vecDistKm = kDistanceSteps[idx];
        else if (m_openDropdown == DropdownKind::VecTime && idx >= 0 && idx < kTimeStepsCount)
            m_vecTimeMin = kTimeSteps[idx];
        else if (m_openDropdown == DropdownKind::OsFont && idx >= 0 && idx < kFontSizeStepsCount)
            Plugin()->SetTagFontSize(kFontSizeSteps[idx]);
        m_openDropdown = DropdownKind::None;
        RequestRefresh();
        break;
    }

    case SO_PANEL_COLLAPSE:
        m_collapsed = !m_collapsed;
        RequestRefresh();
        break;

    case SO_MENU_BAR:
        RequestRefresh();
        break;

    case SO_AUTH_LOGIN:
        if (m_authState == AuthState::LoggedOut && !m_loginWindowOpen && !Plugin()->TrainingSession())
        {
            const char* callsign = GetPlugIn()->ControllerMyself().GetCallsign();
            const std::string who = (callsign != NULL && *callsign != '\0') ? callsign : "(no callsign)";

            m_authMessage.clear();
            if (Plugin()->AccessSuspended())
            {
                m_authMessage = Tr(L"Доступ приостановлен");
                ShowNotice(m_authMessage);
                Log::Warn("auth", "LOGIN " + who + " refused: access suspended - the name was removed from the user base");
            }
            else if (!Plugin()->LiveConnection())
            {
                m_authMessage = Tr(L"Нет подключения к VATSIM");
                m_authFailed = true;
                Log::Error("auth", "LOGIN " + who + " failed: not controlling on the live VATSIM network"
                    " (EuroScope connection type " + std::to_string(GetPlugIn()->GetConnectionType()) + ")");
            }
            else if (Plugin()->GetConfig().SquawkServerUrl().empty())
            {
                m_authMessage = Tr(L"База пользователей недоступна");
                m_authFailed = true;
                Log::Error("auth", "LOGIN " + who + " failed: Squawk.ServerUrl is not set in GalaxyATMSystem.json");
            }
            else
            {
                Log::Info("auth", "LOGIN " + who + ": login window opened");
                m_loginWindowOpen = true;
                m_loginProblem.clear();
                Plugin()->ResetLogin();

                const CGalaxyATMSystemPlugin::SavedLogin& saved = Plugin()->SavedIdentity();
                const std::wstring* const from[LF_COUNT] = { &saved.cid, &saved.surname,
                    &saved.firstName, &saved.patronymic };
                for (int field = 0; field < LF_COUNT; field++)
                    if (m_loginValues[field].empty())
                        m_loginValues[field] = *from[field];

                int first = LF_COUNT - 1;
                for (int field : { LF_CID, LF_SURNAME, LF_FIRST_NAME })
                {
                    if (m_loginValues[field].empty())
                    {
                        first = field;
                        break;
                    }
                }
                EditLoginField(first);
            }
        }
        RequestRefresh();
        break;

    case SO_AUTH_BYPASS:
        if (m_authState == AuthState::LoggedOut && !Plugin()->TrainingSession())
        {
            if (BypassAvailable())
            {
                Log::Warn("auth", "Bypass: panel opened past the user base after a failed attempt");
                CloseLoginWindow();
                m_authMessage.clear();
                m_authFailed = false;
                StartAuthCheck();
            }
            else if (Plugin()->AccessSuspended())
            {
                Log::Warn("auth", "Bypass refused: access suspended");
                ShowNotice(Tr(L"Доступ приостановлен"));
            }
            else
            {
                Log::Info("auth", "Bypass refused: no LOGIN has failed - told to register");
                ShowNotice(Tr(L"Пожалуйста, зарегистрируйтесь в системе в установленном порядке"));
            }
        }
        RequestRefresh();
        break;

    case SO_NOTICE_WINDOW:
        break;
    case SO_NOTICE_OK:
    case SO_NOTICE_CLOSE:
        m_noticeText.clear();
        RequestRefresh();
        break;

    case SO_LOGIN_WINDOW:
        CommitEntry();
        break;
    case SO_LOGIN_CLOSE:
        CloseLoginWindow();
        break;
    case SO_LOGIN_FIELD:
        EditLoginField(atoi(sObjectId));
        break;
    case SO_LOGIN_SEND:
        SendLogin();
        break;
    case SO_LOGIN_REGISTER:
    {
        CommitEntry();
        const std::string url = Plugin()->RegisterPageUrl();
        if (url.compare(0, 7, "http://") == 0 || url.compare(0, 8, "https://") == 0)
        {
            Log::Info("auth", "registration page opened in the browser: " + url);
            ShellExecuteA(NULL, "open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
        }
        break;
    }

    case SO_TIMER_TOGGLE:
        if (Button == BUTTON_RIGHT)
        {
            m_timerElapsedMs = 0;
            m_timerStartTick = GetTickCount64();
        }
        else if (m_timerRunning)
        {
            m_timerElapsedMs += GetTickCount64() - m_timerStartTick;
            m_timerRunning = false;
        }
        else
        {
            m_timerStartTick = GetTickCount64();
            m_timerRunning = true;
        }
        RequestRefresh();
        break;

    case SO_ALTFILTER_FROM:
        OpenAltFilterPicker(Area, true);
        break;
    case SO_ALTFILTER_TO:
        OpenAltFilterPicker(Area, false);
        break;
    case SO_ALTFILTER_USE_CHK:
        Plugin()->SetAltFilterEnabled(!Plugin()->AltFilterEnabled());
        RequestRefresh();
        break;

    case SO_VEC_DIST_TOGGLE:
        m_vecDistEnabled = !m_vecDistEnabled;
        if (m_vecDistEnabled)
            m_vecTimeEnabled = false;
        RequestRefresh();
        break;
    case SO_VEC_DIST_FIELD:
        m_openDropdown = (m_openDropdown == DropdownKind::VecDist)
            ? DropdownKind::None : DropdownKind::VecDist;
        RequestRefresh();
        break;
    case SO_VEC_TIME_TOGGLE:
        m_vecTimeEnabled = !m_vecTimeEnabled;
        if (m_vecTimeEnabled)
            m_vecDistEnabled = false;
        RequestRefresh();
        break;
    case SO_VEC_TIME_FIELD:
        m_openDropdown = (m_openDropdown == DropdownKind::VecTime)
            ? DropdownKind::None : DropdownKind::VecTime;
        RequestRefresh();
        break;
    case SO_VEC_BY_PLAN_CHK:
        m_vecByPlan = !m_vecByPlan;
        RequestRefresh();
        break;
    case SO_VEC_LEVEL_CHK:
        m_vecShowLevel = !m_vecShowLevel;
        RequestRefresh();
        break;

    case SO_OS_TWO_LINE:   m_osLines = 2;              RequestRefresh(); break;
    case SO_OS_THREE_LINE: m_osLines = 3;              RequestRefresh(); break;
    case SO_OS_SPEED:      m_osSpeed = !m_osSpeed;     RequestRefresh(); break;
    case SO_OS_FONT_FIELD:
        m_openDropdown = (m_openDropdown == DropdownKind::OsFont)
            ? DropdownKind::None : DropdownKind::OsFont;
        RequestRefresh();
        break;

    case SO_CODE_ALL:      m_codeAll = !m_codeAll;     RequestRefresh(); break;
    case SO_CODE_BP:       m_codeBp = !m_codeBp;       RequestRefresh(); break;
    case SO_CODE_EXTRA:    m_codeExtra = !m_codeExtra; RequestRefresh(); break;
    case SO_CODE_FILTER:
    {
        std::string initial = Narrow(m_codeFilter);
        GetPlugIn()->OpenPopupEdit(Area, FN_CODE_FILTER, initial.c_str());
        break;
    }
    case SO_VV_SLIDER:
        SetVvGainFrom(Pt);
        RequestRefresh();
        break;


    case SO_UNIT_ALT_FL:  Plugin()->SetUnitAlt(AltUnit::FL);   RequestRefresh(); break;
    case SO_UNIT_ALT_M:   Plugin()->SetUnitAlt(AltUnit::M);    RequestRefresh(); break;
    case SO_UNIT_ALT_FLM: Plugin()->SetUnitAlt(AltUnit::FLM);  RequestRefresh(); break;
    case SO_UNIT_VS_FTM:  Plugin()->SetUnitVs(VsUnit::FtMin);  RequestRefresh(); break;
    case SO_UNIT_VS_MS:   Plugin()->SetUnitVs(VsUnit::MS);     RequestRefresh(); break;
    case SO_UNIT_GS_KT:   Plugin()->SetUnitGs(GsUnit::Knots);  RequestRefresh(); break;
    case SO_UNIT_GS_KMH:  Plugin()->SetUnitGs(GsUnit::Kmh);    RequestRefresh(); break;
    case SO_UNIT_DIST_NM: Plugin()->SetUnitDist(DistUnit::NM); RequestRefresh(); break;
    case SO_UNIT_DIST_KM: Plugin()->SetUnitDist(DistUnit::Km); RequestRefresh(); break;

    case SO_ATIS_BUTTON:
    case SO_ATIS_LETTER_HEADER:
        m_atisOpen = !m_atisOpen;
        m_atisScrollPx = 0;
        RequestRefresh();
        break;
    case SO_RC_SORT:
    {
        int col = atoi(sObjectId);
        if (col == m_rcSortKey)
            m_rcSortAsc = !m_rcSortAsc;
        else if (col >= 0 && col < kRcCols)
        {
            m_rcSortKey = col;
            m_rcSortAsc = true;
        }
        RequestRefresh();
        break;
    }
    case SO_RC_ROW:
    {
        CFlightPlan picked = GetPlugIn()->FlightPlanSelect(sObjectId);
        if (Button == BUTTON_RIGHT)
        {
            if (picked.IsValid())
            {
                const bool mine = picked.GetTrackingControllerIsMe();
                int& scroll = mine ? m_rcScrollMine : m_rcScroll;
                scroll += m_rcPageRows[mine ? 0 : 1];
            }
        }
        else if (picked.IsValid())
        {
            GetPlugIn()->SetASELAircraft(picked);
        }
        RequestRefresh();
        break;
    }
    case SO_RC_FILTER:
    {
        const bool isCallsign = strcmp(sObjectId, "callsign") == 0;
        const bool isBefore = strcmp(sObjectId, "before") == 0;
        std::string initial;
        if (isCallsign)
            initial = Narrow(m_rcFilterCallsign);
        else
        {
            const int value = isBefore ? m_rcFilterBefore : m_rcFilterAfter;
            if (value >= 0)
                initial = std::to_string(value);
        }
        GetPlugIn()->OpenPopupEdit(Area,
            isCallsign ? FN_RC_FILTER_CALLSIGN : isBefore ? FN_RC_FILTER_BEFORE : FN_RC_FILTER_AFTER,
            initial.c_str());
        break;
    }
    case SO_RC_CLOSE:
        m_rcOpen = false;
        RequestRefresh();
        break;

    case SO_ATIS_OK:
    case SO_ATIS_CLOSE:
        m_atisOpen = false;
        RequestRefresh();
        break;
    case SO_ATIS_SCROLLBAR:
        ScrollAtisTo(Pt, Area);
        RequestRefresh();
        break;
    case SO_ATIS_LINE_UP:
        m_atisScrollPx = max(0, m_atisScrollPx - 18);
        RequestRefresh();
        break;
    case SO_ATIS_LINE_DN:
        m_atisScrollPx = min(m_atisScrollMax, m_atisScrollPx + 18);
        RequestRefresh();
        break;

    default:
        break;
    }
}

void CGalaxyATMSystemRadarScreen::OnButtonDownScreenObject(int ObjectType, const char* sObjectId,
    POINT Pt, RECT Area, int Button)
{
    if (Button != BUTTON_LEFT)
        return;

    if (ObjectType == SO_SIGMET_AREA)
    {
        int idx = FindSigmetAt(Pt);
        if (idx < 0)
            return;

        m_sigmetInfoIndex = idx;
        m_sigmetInfoAt = Pt;
        m_sigmetInfoHeld = false;
        m_sigmetInfoWait = 0;
        RequestRefresh();
    }
    else if (ObjectType == SO_ZONE_AREA)
    {
        int idx = FindZoneAt(Pt);
        if (idx < 0)
            idx = ZoneFromObjectId(sObjectId);
        if (idx < 0)
            return;

        m_zoneInfoIndex = idx;
        m_zoneInfoAt = Pt;
        m_zoneInfoHeld = false;
        m_zoneInfoWait = 0;
        RequestRefresh();
    }
}

void CGalaxyATMSystemRadarScreen::OnButtonUpScreenObject(int ObjectType, const char* sObjectId,
    POINT Pt, RECT Area, int Button)
{
    if (Button != BUTTON_LEFT)
        return;

    if (m_sigmetInfoIndex >= 0 || m_zoneInfoIndex >= 0)
    {
        m_sigmetInfoIndex = -1;
        m_zoneInfoIndex = -1;
        RequestRefresh();
    }
}

void CGalaxyATMSystemRadarScreen::OnDoubleClickScreenObject(int ObjectType, const char* sObjectId,
    POINT Pt, RECT Area, int Button)
{
    if (ObjectType == SO_RULER_LINE && Button == BUTTON_LEFT)
        RemoveRulerNear(Pt);
}

void CGalaxyATMSystemRadarScreen::ApplyRcFilter(int functionId, const std::wstring& typed)
{
    if (functionId == FN_RC_FILTER_CALLSIGN)
    {
        std::wstring callsign;
        for (wchar_t c : typed)
            if (!iswspace(c) && callsign.size() < 10)
                callsign += c;
        m_rcFilterCallsign = RcUpper(callsign);
    }
    else
    {
        std::wstring digits = typed;
        digits.erase(0, digits.find_first_not_of(L" \t"));
        digits.erase(digits.find_last_not_of(L" \t") + 1);

        int value = -1;
        if (!digits.empty())
        {
            if (digits.size() > 4 || digits.find_first_not_of(L"0123456789") != std::wstring::npos)
                return;
            value = _wtoi(digits.c_str());
        }
        (functionId == FN_RC_FILTER_BEFORE ? m_rcFilterBefore : m_rcFilterAfter) = value;
    }
    m_rcScroll = m_rcScrollMine = 0;
    RequestRefresh();
}

void CGalaxyATMSystemRadarScreen::OnFunctionCall(int FunctionId, const char* sItemString, POINT Pt, RECT Area)
{
    Plugin()->HandleSquawkFunction(FunctionId, sItemString, Area, "screen");

    if (FunctionId >= FN_LOGIN_FIELD && FunctionId < FN_LOGIN_FIELD + LF_COUNT)
    {
        const int field = FunctionId - FN_LOGIN_FIELD;
        const std::wstring typed = (sItemString != NULL) ? Widen(sItemString) : std::wstring();
        m_loginValues[field] = TrimSpaces(typed);
        m_loginProblem.clear();
        Plugin()->ResetLogin();
        RequestRefresh();
        return;
    }

    if (FunctionId == FN_RC_FILTER_CALLSIGN || FunctionId == FN_RC_FILTER_BEFORE
        || FunctionId == FN_RC_FILTER_AFTER)
    {
        ApplyRcFilter(FunctionId, (sItemString != NULL) ? Widen(sItemString) : std::wstring());
        return;
    }

    if (FunctionId == FN_CODE_FILTER)
    {
        m_codeFilter = (sItemString != NULL) ? Widen(sItemString) : std::wstring();
        RequestRefresh();
        return;
    }

    if (FunctionId != FN_ALTFILTER_FROM && FunctionId != FN_ALTFILTER_TO)
        return;

    if (sItemString == NULL)
        return;
    std::string typed(sItemString);
    size_t start = typed.find_first_not_of(" \tFLfl");
    if (start == std::string::npos)
        return;

    int fl = 0;
    for (size_t i = start; i < typed.size(); i++)
    {
        if (!isdigit((unsigned char)typed[i]))
            return;
        fl = fl * 10 + (typed[i] - '0');
        if (fl > 999)
            return;
    }

    if (FunctionId == FN_ALTFILTER_FROM)
        Plugin()->SetAltFilterFromFL(fl);
    else
        Plugin()->SetAltFilterToFL(fl);
    RequestRefresh();
}

void CGalaxyATMSystemRadarScreen::OnMoveScreenObject(int ObjectType, const char* sObjectId,
    POINT Pt, RECT Area, bool Released)
{
    if (ObjectType == SO_FORMULAR_AHDG)
    {
        const bool leftHeld = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

        if (m_hdgDragCancelled)
        {
            if (Released || !leftHeld)
                m_hdgDragCancelled = false;
            return;
        }
        if (!m_hdgDragging && (Released || !leftHeld))
            return;

        if (!m_hdgDragging)
        {
            m_hdgDragging = true;
            m_hdgDragMoved = false;
            m_hdgDragStart = Pt;
            m_hdgDragCallsign = sObjectId;
        }
        m_hdgDragPt = Pt;
        if (abs(Pt.x - m_hdgDragStart.x) > 6 || abs(Pt.y - m_hdgDragStart.y) > 6)
            m_hdgDragMoved = true;

        if (Released)
        {
            if (m_hdgDragMoved)
            {
                int hdg = DragHeading(m_hdgDragCallsign.c_str(), Pt);
                CFlightPlan fp = GetPlugIn()->FlightPlanSelect(m_hdgDragCallsign.c_str());
                if (hdg > 0 && fp.IsValid())
                    fp.GetControllerAssignedData().SetAssignedHeading(hdg);
                m_hdgDragEndTick = GetTickCount64();
            }
            m_hdgDragging = false;
            m_hdgDragMoved = false;
        }
        RequestRefresh();
        return;
    }

    if (ObjectType == SO_FORMULAR)
    {
        if (Released)
        {
            m_dragOffset = { 0, 0 };
            return;
        }

        auto it = m_formulars.find(sObjectId);
        if (it == m_formulars.end())
            return;
        FormularState& f = it->second;

        if (m_dragOffset.x == 0 && m_dragOffset.y == 0)
        {
            m_dragOffset.x = Pt.x - f.callsignAt.x;
            m_dragOffset.y = Pt.y - f.callsignAt.y;
        }
        f.offset.x = Pt.x - m_dragOffset.x - f.anchor.x;
        f.offset.y = Pt.y - m_dragOffset.y - f.anchor.y;
        f.placed = true;
        RequestRefresh();
        return;
    }

    if (ObjectType == SO_ATIS_SCROLLBAR)
    {
        ScrollAtisTo(Pt, Area);
        RequestRefresh();
        return;
    }

    if (ObjectType == SO_VV_SLIDER)
    {
        m_vvDragging = !Released;
        SetVvGainFrom(Pt);
        RequestRefresh();
        return;
    }

    if (ObjectType == SO_RULER_LABEL)
    {
        if (Released)
        {
            m_dragOffset = { 0, 0 };
            return;
        }

        size_t idx = (size_t)atoi(sObjectId);
        if (idx >= m_rulers.size())
            return;
        RulerLine& r = m_rulers[idx];

        POINT at = { r.labelAnchor.x + r.labelOffset.x, r.labelAnchor.y + r.labelOffset.y };
        if (m_dragOffset.x == 0 && m_dragOffset.y == 0)
        {
            m_dragOffset.x = Pt.x - at.x;
            m_dragOffset.y = Pt.y - at.y;
        }

        r.labelOffset.x = Pt.x - m_dragOffset.x - r.labelAnchor.x;
        r.labelOffset.y = Pt.y - m_dragOffset.y - r.labelAnchor.y;
        RequestRefresh();
        return;
    }

    if (ObjectType == SO_RC_RESIZE)
    {
        if (!m_rcResizing)
        {
            m_rcResizing = true;
            m_rcResizeGrab = m_rcArea.right - Pt.x;
        }
        const int width = Pt.x + m_rcResizeGrab - m_rcArea.left;
        m_rcScale = max(kRcScaleMin, min(kRcScaleMax, (int)lround(width * 100.0 / kRcSvgW)));
        if (Released)
            m_rcResizing = false;
        RequestRefresh();
        return;
    }

    if (ObjectType == SO_RC_HEADER)
    {
        if (m_rcFloating || (!Released && !(GetAsyncKeyState(VK_LBUTTON) & 0x8000)))
        {
            m_dragOffset = { 0, 0 };
            return;
        }
        if (!Released)
        {
            if (m_dragOffset.x == 0 && m_dragOffset.y == 0)
            {
                m_dragOffset.x = Pt.x - m_rcArea.left;
                m_dragOffset.y = Pt.y - m_rcArea.top;

                POINT cursor;
                HWND view = NULL;
                m_rcDragView = CursorRadarPoint(cursor, &view) ? view : NULL;
            }
            RECT want = { Pt.x - m_dragOffset.x, Pt.y - m_dragOffset.y, 0, 0 };
            want.right = want.left + (m_rcArea.right - m_rcArea.left);
            want.bottom = want.top + (m_rcArea.bottom - m_rcArea.top);

            const int kPull = 40;
            const RECT ra = GetRadarArea();
            if ((want.left < ra.left - kPull || want.top < ra.top - kPull
                 || want.right > ra.right + kPull || want.bottom > ra.bottom + kPull)
                && UndockSectorList(want))
                return;
        }
    }

    RECT* target = NULL;
    if (ObjectType == SO_RC_HEADER)
        target = &m_rcArea;
    else if (ObjectType == SO_ATIS_HEADER)
        target = &m_atisArea;
    else if (ObjectType == SO_LOGIN_HEADER)
        target = &m_loginArea;
    else
        return;

    if (Released)
    {
        m_dragOffset = { 0, 0 };
        return;
    }

    if (m_dragOffset.x == 0 && m_dragOffset.y == 0)
    {
        m_dragOffset.x = Pt.x - target->left;
        m_dragOffset.y = Pt.y - target->top;
    }

    int w = target->right - target->left;
    int h = target->bottom - target->top;
    target->left = Pt.x - m_dragOffset.x;
    target->top = Pt.y - m_dragOffset.y;
    target->right = target->left + w;
    target->bottom = target->top + h;
    RequestRefresh();
}

bool CGalaxyATMSystemRadarScreen::OnCompileCommand(const char* sCommandLine)
{
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
            const char* a7 = cad.GetFlightStripAnnotation(7);
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
        m_authFailed = false;
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

void CGalaxyATMSystemRadarScreen::OnAsrContentToBeClosed(void)
{
    delete this;
}

void CGalaxyATMSystemRadarScreen::OnAsrContentToBeSaved(void)
{
    char buf[64];
    sprintf_s(buf, "%d", m_visible ? 1 : 0);
    SaveDataToAsr("PanelVisible", "ULLL panel visible", buf);

    sprintf_s(buf, "%d", m_collapsed ? 1 : 0);
    SaveDataToAsr("PanelCollapsed", "ULLL panel collapsed", buf);

    sprintf_s(buf, "%d,%d,%d,%d,%d,%d", m_vecDistEnabled ? 1 : 0, m_vecDistKm,
        m_vecTimeEnabled ? 1 : 0, m_vecTimeMin, m_vecByPlan ? 1 : 0, m_vecShowLevel ? 1 : 0);
    SaveDataToAsr("Vectors", "distEnabled,distKm,timeEnabled,timeMin,byPlan,showLevel", buf);

    sprintf_s(buf, "%d,%d,%d,%d", (int)Plugin()->UnitAlt(), (int)Plugin()->UnitVs(),
        (int)Plugin()->UnitGs(), (int)Plugin()->UnitDist());
    SaveDataToAsr("Units", "alt,vs,gs,dist", buf);

    sprintf_s(buf, "%d,%d,%d", Plugin()->AltFilterEnabled() ? 1 : 0,
        Plugin()->AltFilterFromFL(), Plugin()->AltFilterToFL());
    SaveDataToAsr("AltFilter", "enabled,fromFL,toFL", buf);

    sprintf_s(buf, "%d,%d,%d", m_osLines, m_osSpeed ? 1 : 0, Plugin()->TagFontSize());
    SaveDataToAsr("Os", "lines,speed,fontSize", buf);

    SaveDataToAsr("FormularKind", "формуляр: auto (по позиции), ctr, app, twr",
        kFormularKindNames[(int)m_formularKindSetting]);

    sprintf_s(buf, "%d", m_rulerButton);
    SaveDataToAsr("RulerButton", "side mouse button that arms the ruler (0 = off)", buf);

    sprintf_s(buf, "%d", m_sigmetsVisible ? 1 : 0);
    SaveDataToAsr("Sigmets", "сигмет areas drawn on the radar", buf);

    sprintf_s(buf, "%d", m_zonesVisible ? 1 : 0);
    SaveDataToAsr("Zones", "запретные зоны drawn on the radar", buf);

    sprintf_s(buf, "%d", m_atisLetterOpen ? 1 : 0);
    SaveDataToAsr("AtisLetter", "АТИС letter window shown", buf);

    sprintf_s(buf, "%d,%d,%d,%d", m_rcOpen ? 1 : 0, m_rcSortKey, m_rcSortAsc ? 1 : 0, m_rcScale);
    SaveDataToAsr("SectorList", "open,sortKey,sortAscending,scalePercent", buf);

    sprintf_s(buf, "%d,%d,%s", m_rcFilterBefore, m_rcFilterAfter, Narrow(m_rcFilterCallsign).c_str());
    SaveDataToAsr("SectorListFilter", "minutesBefore,minutesAfter (-1 = no limit),callsign", buf);

    sprintf_s(buf, "%d,%ld,%ld", m_rcFloating ? 1 : 0, m_rcFloatPos.x, m_rcFloatPos.y);
    SaveDataToAsr("SectorListOutside", "outside EuroScope,screenX,screenY", buf);

    sprintf_s(buf, "%d,%d,%d,%d", m_codeAll ? 1 : 0, m_codeBp ? 1 : 0,
        m_codeExtra ? 1 : 0, m_vvGain);
    SaveDataToAsr("CodeBlock", "all,bp,extra,gain", buf);

    SaveDataToAsr("CodeFilter", "ВВ1 code filter", Narrow(m_codeFilter).c_str());

    SaveDataToAsr("Language", "plugin language: rus, eng", Lang::Name(Lang::Current()));
}

void CGalaxyATMSystemRadarScreen::OnAsrContentLoaded(bool Loaded)
{
    if (!Loaded)
        return;

    const char* vis = GetDataFromAsr("PanelVisible");
    if (vis != NULL)
        m_visible = (atoi(vis) != 0);

    const char* collapsed = GetDataFromAsr("PanelCollapsed");
    if (collapsed != NULL)
        m_collapsed = (atoi(collapsed) != 0);

    const char* vec = GetDataFromAsr("Vectors");
    if (vec != NULL)
    {
        int de = 0, dk = 0, te = 0, tm = 0, bp = 0, sl = 0;
        if (sscanf_s(vec, "%d,%d,%d,%d,%d,%d", &de, &dk, &te, &tm, &bp, &sl) == 6)
        {
            m_vecDistEnabled = de != 0;
            m_vecDistKm = dk;
            m_vecTimeEnabled = (te != 0) && !m_vecDistEnabled;
            m_vecTimeMin = tm;
            m_vecByPlan = bp != 0;
            m_vecShowLevel = sl != 0;
        }
    }

    const char* units = GetDataFromAsr("Units");
    if (units != NULL)
    {
        int a = 0, v = 0, g = 0, d = 0;
        if (sscanf_s(units, "%d,%d,%d,%d", &a, &v, &g, &d) == 4)
        {
            Plugin()->SetUnitAlt((AltUnit)a);
            Plugin()->SetUnitVs((VsUnit)v);
            Plugin()->SetUnitGs((GsUnit)g);
            Plugin()->SetUnitDist((DistUnit)d);
        }
    }

    const char* altFilter = GetDataFromAsr("AltFilter");
    if (altFilter != NULL)
    {
        int en = 0, from = 0, to = 0;
        if (sscanf_s(altFilter, "%d,%d,%d", &en, &from, &to) == 3)
        {
            Plugin()->SetAltFilterEnabled(en != 0);
            Plugin()->SetAltFilterFromFL(from);
            Plugin()->SetAltFilterToFL(to);
        }
    }

    const char* os = GetDataFromAsr("Os");
    if (os != NULL)
    {
        int lines = 0, speed = 0, size = 0;
        if (sscanf_s(os, "%d,%d,%d", &lines, &speed, &size) >= 2)
        {
            m_osLines = (lines == 3) ? 3 : 2;
            m_osSpeed = speed != 0;
            for (int step : kFontSizeSteps)
                if (step == size)
                    Plugin()->SetTagFontSize(size);
        }
    }

    const char* formularKind = GetDataFromAsr("FormularKind");
    if (formularKind != NULL)
    {
        for (int i = 0; i < (int)_countof(kFormularKindNames); i++)
            if (strcmp(formularKind, kFormularKindNames[i]) == 0)
                m_formularKindSetting = (FormularKindSetting)i;
    }

    const char* codes = GetDataFromAsr("CodeBlock");
    if (codes != NULL)
    {
        int all = 0, bp = 0, extra = 0, gain = 0;
        if (sscanf_s(codes, "%d,%d,%d,%d", &all, &bp, &extra, &gain) == 4)
        {
            m_codeAll = all != 0;
            m_codeBp = bp != 0;
            m_codeExtra = extra != 0;
            m_vvGain = max(0, min(100, gain));
        }
    }

    const char* rulerBtn = GetDataFromAsr("RulerButton");
    if (rulerBtn != NULL)
    {
        int vk = atoi(rulerBtn);
        if (vk == 0 || vk == VK_XBUTTON1 || vk == VK_XBUTTON2)
            m_rulerButton = vk;
    }

    const char* sigmets = GetDataFromAsr("Sigmets");
    if (sigmets != NULL)
        m_sigmetsVisible = (atoi(sigmets) != 0);

    const char* zones = GetDataFromAsr("Zones");
    if (zones != NULL)
        m_zonesVisible = (atoi(zones) != 0);

    const char* atisLetter = GetDataFromAsr("AtisLetter");
    if (atisLetter != NULL)
        m_atisLetterOpen = (atoi(atisLetter) != 0);

    const char* codeFilter = GetDataFromAsr("CodeFilter");
    if (codeFilter != NULL)
        m_codeFilter = Widen(codeFilter);

    const char* language = GetDataFromAsr("Language");
    if (language != NULL && strcmp(language, Lang::Name(Lang::Id::En)) == 0)
        Lang::Set(Lang::Id::En);
    else if (language != NULL && strcmp(language, Lang::Name(Lang::Id::Ru)) == 0)
        Lang::Set(Lang::Id::Ru);

    const char* rc = GetDataFromAsr("SectorList");
    if (rc != NULL)
    {
        int open = 0, key = 0, asc = 1, scale = m_rcScale;
        if (sscanf_s(rc, "%d,%d,%d,%d", &open, &key, &asc, &scale) >= 3)
        {
            m_rcOpen = (open != 0);
            m_rcSortKey = (key >= 0 && key < kRcCols) ? key : RC_CALLSIGN;
            m_rcSortAsc = (asc != 0);
            m_rcScale = max(kRcScaleMin, min(kRcScaleMax, scale));
        }
    }

    const char* rcFilter = GetDataFromAsr("SectorListFilter");
    if (rcFilter != NULL)
    {
        int before = -1, after = -1;
        char callsign[16] = { 0 };
        if (sscanf_s(rcFilter, "%d,%d,%15s", &before, &after, callsign, (unsigned)sizeof(callsign)) >= 2)
        {
            m_rcFilterBefore = max(-1, min(9999, before));
            m_rcFilterAfter = max(-1, min(9999, after));
            m_rcFilterCallsign = RcUpper(Widen(callsign));
        }
    }

    const char* rcOutside = GetDataFromAsr("SectorListOutside");
    if (rcOutside != NULL)
    {
        int outside = 0;
        long x = 0, y = 0;
        if (sscanf_s(rcOutside, "%d,%ld,%ld", &outside, &x, &y) == 3)
        {
            m_rcFloating = (outside != 0);
            m_rcFloatPos = { x, y };
        }
    }
}
