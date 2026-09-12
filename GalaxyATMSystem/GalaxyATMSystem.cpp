#include "pch.h"
#include "GalaxyATMSystem.h"

#include <string>
#include <map>
#include <cstdio>
#include <cwchar>
#include <cmath>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <algorithm>

#include "Net.h"
#include "Geometry.h"

// AlphaBlend, for the see-through backing of the сигмет info window.
#pragma comment(lib, "msimg32.lib")

// GDI+, for the target vectors: a GDI pen only comes in whole pixels.
// objidl.h first - WIN32_LEAN_AND_MEAN leaves out the COM types gdiplus.h uses.
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

    // EuroScope hands out ANSI (Windows code page) C strings; widen for GDI.
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

    // The other direction, for handing a value back to EuroScope's own popups.
    std::string Narrow(const std::wstring& w)
    {
        if (w.empty())
            return std::string();
        int n = WideCharToMultiByte(CP_ACP, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
        std::string s(n, '\0');
        WideCharToMultiByte(CP_ACP, 0, w.c_str(), (int)w.size(), &s[0], n, NULL, NULL);
        return s;
    }

    // Per-second refresh: EuroScope only repaints on radar/mouse activity, which
    // is too coarse for a live clock, so each screen drives its own 1s timer.
    std::map<UINT_PTR, CGalaxyATMSystemRadarScreen*> g_timers;

    // A second, much faster tick used only to sample the side mouse buttons -
    // see CGalaxyATMSystemRadarScreen::PollRulerButton. Kept apart from the clock
    // timer above so watching the button does not drag a repaint along with it.
    std::map<UINT_PTR, CGalaxyATMSystemRadarScreen*> g_pollTimers;

    // Hijack / radio failure / general emergency.
    bool IsDistressSquawk(const char* squawk)
    {
        return strcmp(squawk, "7500") == 0
            || strcmp(squawk, "7600") == 0
            || strcmp(squawk, "7700") == 0;
    }

    // Codes any number of aircraft may carry at once, so a repeat of one is not
    // a duplicate assignment.
    bool IsConspicuitySquawk(const char* squawk)
    {
        return strcmp(squawk, "0000") == 0
            || strcmp(squawk, "1200") == 0
            || strcmp(squawk, "2000") == 0
            || strcmp(squawk, "7000") == 0;
    }

    // ---- БЛОК 4 unit formatting -------------------------------------------
    // Shared by the tag items (OnGetTagItem, narrow strings) and the БЛОК 3
    // extrapolation vector's predicted-level label (wide, via Widen()).

    // A level in feet, in hundreds of feet. At or above the transition level
    // it is a flight level flown on standard pressure - "F247"; below it the
    // aircraft is on QNH, so it is an altitude and carries "A" - "A025".
    std::string FormatLevelFeet(int altFt, bool belowTL)
    {
        char buf[16];
        sprintf_s(buf, "%c%03d", belowTL ? 'A' : 'F', altFt / 100);
        return buf;
    }

    // A level in metres: prefix "C", value in TENS of metres and four digits
    // wide - "C0624" is 6240 m. Not the feet convention with metres substituted.
    std::string FormatLevelMetres(int altFt)
    {
        char buf[16];
        sprintf_s(buf, "C%04d", (int)lround(altFt * 0.3048 / 10.0));
        return buf;
    }

    std::string FormatAltitudeUnit(int altFt, AltUnit unit, bool belowTL)
    {
        switch (unit)
        {
        case AltUnit::M:
            return FormatLevelMetres(altFt);
        case AltUnit::FLM:
            return FormatLevelFeet(altFt, belowTL) + " " + FormatLevelMetres(altFt);
        case AltUnit::FL:
        default:
            return FormatLevelFeet(altFt, belowTL);
        }
    }

    // Prefixed with the unit and always signed, the way the reference tags
    // read it: "fm+2800" / "fm-2800" for ft/min, "ms+14.2" for m/s. Level
    // flight - the same +-100 fpm band the extrapolation-vector trend arrow
    // uses - reports nothing rather than "fm+0"; a noise-free "0" isn't a real
    // reading, and the field is simply meant to be blank until it climbs or
    // descends.
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

    // Zero-padded to three digits, so a taxiing aircraft reads "Kt007" and the
    // column keeps its width instead of jumping about as the speed changes.
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

    // Finds a "Q####" (hPa) or "A####" (inHg) group, bounded by spaces/ends so
    // it can't match digits from an unrelated part of the report. Returns the
    // pressure in hPa, or -1 when the report carries neither.
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
            // Bounded by non-alphanumerics rather than spaces specifically, so
            // a group at the very end of a fetched report - where the next
            // character is a newline - still parses.
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

    // A rectangle filled at a given opacity. GDI has no alpha of its own, so
    // this is the standard one-pixel source stretched under AlphaBlend - the
    // only thing on the panel that needs msimg32, and worth it for a window
    // the radar has to stay readable through.
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

    // Shift held, and held in EuroScope: GetAsyncKeyState is machine-wide, and
    // a Shift typed into the browser in front must not arm the areas behind it.
    bool ShiftHeldInEuroScope()
    {
        HWND fg = GetForegroundWindow();
        DWORD pid = 0;
        if (fg != NULL)
            GetWindowThreadProcessId(fg, &pid);
        return pid == GetCurrentProcessId() && (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    }

    // GDI+ surface for the target vectors, which are Theme::VectorWidth thick -
    // a fractional width no GDI pen can draw - antialiased so the half pixel
    // actually shows. Keep one in a scope of its own: plain GDI drawing on the
    // same DC should wait until it is gone.
    struct VectorCanvas
    {
        Gdiplus::Graphics g;
        Gdiplus::Pen pen;

        VectorCanvas(HDC hDC, COLORREF color)
            : g(hDC),
              pen(Gdiplus::Color(GetRValue(color), GetGValue(color), GetBValue(color)), Theme::VectorWidth)
        {
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            pen.SetLineJoin(Gdiplus::LineJoinRound);
        }

        void Line(double x0, double y0, double x1, double y1)
        {
            g.DrawLine(&pen, (Gdiplus::REAL)x0, (Gdiplus::REAL)y0, (Gdiplus::REAL)x1, (Gdiplus::REAL)y1);
        }
    };

    // Draws a polyline through pts, leaving a small gap centred on every
    // interior point (but not at the very first/last point) - this is what
    // makes a minute-vector read as a row of per-minute tick marks instead of
    // one solid line.
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

            // Half the gap off each interior end, in fractional pixels - the
            // canvas takes them as they are, so the gap comes out the same
            // whichever way the segment runs - and never more than 45% of a
            // short segment, so none collapses to nothing.
            double cut = min(gapPx / 2.0, len * 0.45) / len;
            double t0 = (i > 0) ? cut : 0.0;
            double t1 = (i + 2 < pts.size()) ? 1.0 - cut : 1.0;
            canvas.Line(ax + dx * t0, ay + dy * t0, ax + dx * t1, ay + dy * t1);
        }
    }
}

// ---- Layout metrics (kept in one place so every block's height is exact) ---
namespace L
{
    // Horizontal geometry is the reference export's own
    // ("KSA UVD GALAKTIKA.svg", a 212 px wide artboard) to the pixel. The
    // vertical numbers are that layout with its dead space squeezed out: at the
    // export's own spacing the panel is 1074 px tall, which overflows a normal
    // radar area and takes the aerodrome block off the bottom of the screen.
    // Row heights, caption bands and inter-block gaps are therefore tightened
    // (and then nudged back up a little to fit the enlarged font - see
    // Theme::FontSet) while keeping the same order, proportions and alignment.
    // Everything is expressed relative to the thing it sits in, so a block can
    // be moved without re-deriving its neighbours.
    const int CAPTION_H = 17;   // caption band; the text sits centred in it
    const int CAP_GAP   = 5;    // caption band bottom -> group box top
    const int BLOCK_GAP = 6;    // group box bottom -> next block's caption band
    const int BLOCK_GAP_WIDE = 8;   // the reference gives "Ед. изм." and the aerodrome block a little more
    const int NOCAP_GAP = 16;   // group box bottom -> next group box, no caption between

    // Header: clock over date/mode. The clock is the one oversized item on the
    // panel (Theme::FontSet::Clock) - it is what the header is read for - and
    // it, the date under it and the Таймер block below are spaced apart rather
    // than stacked tight, so the three times are never mistaken for each other.
    const int CLOCK_H   = 24;
    const int DATE_H    = 18;

    // The three spaces in the header - above the clock, clock to date, date to
    // the Таймер caption - are meant to read as one and the same gap, and the
    // only way they stay that way is to derive all three from it. What shows is
    // never the gap constant on its own: each row is taller than the text
    // centred inside it, so half of that leading falls either side of the gap
    // and has to come back out of it. Retune the spacing here, in one number.
    const int HDR_SPACE  = 16;                    // the white space actually seen
    const int CLOCK_LEAD = (CLOCK_H - 20) / 2;    // Clock font is 20 px
    const int DATE_LEAD  = (DATE_H - 14) / 2;     // Body font is 14 px
    const int CAP_LEAD   = (CAPTION_H - 14) / 2;  // the caption band under it

    const int HDR_TOP   = HDR_SPACE - CLOCK_LEAD;
    const int CLOCK_GAP = HDR_SPACE - CLOCK_LEAD - DATE_LEAD;
    const int HDR_GAP   = HDR_SPACE - DATE_LEAD - CAP_LEAD;   // header bottom -> "Таймер"
    const int HEADER_H  = HDR_TOP + CLOCK_H + CLOCK_GAP + DATE_H;       // 68

    // Таймер - one short row.
    const int T_PAD = 4, T_ROW = 20;
    const int TIMER_BOX_H = T_PAD + T_ROW + T_PAD;                      // 28

    // БЛОК 2 - Пользователь: designation/role, then the user line.
    const int U_TOP = 4, U_ROW = 20, U_GAP = 5, U_BOT = 4;
    const int USER_BOX_H = U_TOP + U_ROW + U_GAP + U_ROW + U_BOT;       // 53

    // БЛОК 3 - Векторы: Д/Э row, then two checkbox rows.
    const int V_TOP = 5, V_ROW = 20, V_GAP1 = 8, V_CHK = 16, V_GAP2 = 6, V_BOT = 6;
    const int VECTORS_BOX_H = V_TOP + V_ROW + V_GAP1 + V_CHK + V_GAP2 + V_CHK + V_BOT;  // 77

    // ОС - a "Р-р шрифта:" label over a sunken three-row list.
    const int O_TOP = 4, O_LABEL = 16, O_GAP = 4, O_LIST_H = 68, O_BOT = 4;
    const int OS_ROW = 16, OS_PITCH = 21, OS_ROW0 = 4;  // inside the list
    const int OS_BOX_H = O_TOP + O_LABEL + O_GAP + O_LIST_H + O_BOT;    // 96

    // БЛОК 4 - Ед. изм.: four rows of pill selectors.
    const int E_TOP = 4, E_ROW = 16, E_PITCH = 20, E_BOT = 6;
    const int UNITS_BOX_H = E_TOP + 3 * E_PITCH + E_ROW + E_BOT;        // 86

    // Фильтр высоты: Макс / Мин, then the enable checkbox.
    const int F_TOP = 4, F_ROW = 17, F_GAP1 = 5, F_GAP2 = 9, F_BOT = 4;
    const int ALTFILTER_BOX_H = F_TOP + F_ROW + F_GAP1 + F_ROW + F_GAP2 + F_ROW + F_BOT; // 73

    // Code block (no caption): the ВВ1 source row over the two code readouts.
    // Unlike the blocks above this one is not a stack of rows - the reference
    // hangs every part of it at its own offset below the box top, so those
    // offsets are named here rather than left as bare numbers in DrawBlockCodes.
    const int C_ROW_H    = 15;  // the ВСЕ / БП chips
    const int C_CAP_H    = 15;  // "Коды бедствия" / "Двойной код"
    const int C_FIELD_H  = 16;  // the two readouts
    const int C_VV_TOP     = 2,  C_VV_H    = 17;   // the "ВВ1" label
    const int C_ALL_TOP    = 2;                    // "ВСЕ"
    const int C_BP_TOP     = 19;                   // "БП"
    const int C_FLT_TOP    = 19, C_FLT_H   = 15;   // the inline entry field, on the БП chip's line
    const int C_EXTRA_TOP  = 20, C_EXTRA_H = 13;   // the unlabelled checkbox
    const int C_SLIDER_TOP = 32, C_SLIDER_BOT = 105;
    const int C_DISTRESS_CAP = 36, C_DISTRESS_FIELD = 53;
    const int C_DUP_CAP      = 74, C_DUP_FIELD      = 91;
    const int CODES_BOX_H = 113;

    // БЛОК 5 - Аэродром: ДАВЛ row, then Э/П + АТИС row. The rows run edge to
    // edge of the box in the reference, with only a hairline above them.
    const int A_TOP = 3, A_ROW = 21, A_GAP = 5, A_BOT = 3;
    const int AERODROME_BOX_H = A_TOP + A_ROW + A_GAP + A_ROW + A_BOT;  // 53

    const int PANEL_BOT_PAD = 8;
    const int COLLAPSED_BOT_PAD = 6;   // breathing room under the date when collapsed

    // Height of a whole block: caption band + gap + box.
    inline int Block(int boxH) { return CAPTION_H + CAP_GAP + boxH; }
}

// ---- DLL exports ------------------------------------------------------------
CGalaxyATMSystemPlugin* g_plugin = NULL;
ULONG_PTR g_gdiplusToken = 0;

void __declspec(dllexport) EuroScopePlugInInit(EuroScopePlugIn::CPlugIn** ppPlugInInstance)
{
    // Here rather than in DllMain, where GDI+ must not be started.
    Gdiplus::GdiplusStartupInput gdiplusInput;
    Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusInput, NULL);

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

// ---- Plugin -----------------------------------------------------------------
CGalaxyATMSystemPlugin::CGalaxyATMSystemPlugin() : CPlugIn(
    EuroScopePlugIn::COMPATIBILITY_CODE,
    "Galaxy ATM System",
    "0.2.0",
    "Yuriy Velbovets",
    "ULLL controller control panel")
{
    m_config.Load(g_hModule);

    // Config's placeholders until the first real METAR for our airport
    // arrives (see OnNewMetarReceived).
    m_qnhMmHg = m_config.QnhMmHg();
    m_qnhHpa = m_config.QnhHpa();

    // БЛОК 4's effect on "формуляры сопровождения": four tag items a
    // controller can drop into their tag layout, each formatted per the
    // currently selected unit for its category.
    RegisterTagItemType("ULLL Altitude", TAG_ITEM_ALTITUDE);
    RegisterTagItemType("ULLL Vertical Speed", TAG_ITEM_VERTICAL_SPEED);
    RegisterTagItemType("ULLL Ground Speed", TAG_ITEM_GROUND_SPEED);
    RegisterTagItemType("ULLL Distance to Dest", TAG_ITEM_DISTANCE);
    RegisterTagItemType("ULLL APW", TAG_ITEM_APW);

    // Squawks from the shared server: the column for the Departure list, a
    // click that takes a code, and a menu with the rest.
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

    // Joined rather than detached: the DLL can be unloaded right after this,
    // and the workers still touch this object.
    if (m_metarFetch.joinable())
        m_metarFetch.join();
    if (m_sigmetFetch.joinable())
        m_sigmetFetch.join();
    if (m_atisFetch.joinable())
        m_atisFetch.join();
    if (m_aupFetch.joinable())
        m_aupFetch.join();
    if (m_notamFetch.joinable())
        m_notamFetch.join();
}

// The ICAO code the config names, with anything that cannot be part of one
// stripped out - it goes into a URL, and it is typed by hand.
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

// The plan for the day, which is what turns the restricted areas on and off.
// A failed fetch leaves the last one standing: the areas it booked do not stop
// being booked because one poll did not come back, and blanking the list would
// take every one of them off the screen at once.
//
// ".reload" re-reads the file and starts the lot over. The зоны come back
// with it - they are read out of the config rather than fetched - so a change
// to TopSkyAreas.txt, to "Items" or to the colours is on the screen without
// leaving the session. The feeds are restarted too, since the URLs they go to
// are themselves config.
void CGalaxyATMSystemPlugin::ReloadConfig()
{
    m_config.Load(g_hModule);

    // The placeholders come back with the file, and the live values overwrite
    // them again on the first report that arrives.
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

// The NOTAMs. Same shape as the plan's fetch, and deliberately so: they
// answer the same question about the same areas, only for the ones the plan
// does not carry. A source that cannot be read leaves the last good list
// standing - a NOTAM does not stop being in force because one poll failed.
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
        m_aupFetch.join();   // the previous fetch is long finished - its stages are all timed out

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

// A failed fetch, or an aerodrome with no ATIS station on the air, leaves the
// last good report standing rather than blanking the letter - the broadcast
// does not stop being what it was because one poll did not come back.
void CGalaxyATMSystemPlugin::StartAtisFetch()
{
    if (!m_config.AtisLive())
        return;

    std::string icao = AirportIcao();
    if (icao.empty())
        return;

    if (m_atisFetch.joinable())
        m_atisFetch.join();   // the previous fetch is long finished - its stages are all timed out

    m_atisFetch = std::thread([this, icao]()
        {
            AtisReport report;
            if (!FetchVatsimAtis(icao, report))
                return;

            std::lock_guard<std::mutex> lock(m_atisMutex);
            m_atisLive = report;
        });
}

std::shared_ptr<const std::vector<Sigmet>> CGalaxyATMSystemPlugin::Sigmets() const
{
    std::lock_guard<std::mutex> lock(m_sigmetMutex);
    return m_sigmets;
}

// The whole list is replaced at once, under the lock, by a worker thread. A
// radar screen that is drawing holds its own reference to the old list for as
// long as that frame lasts, so nothing is ever pulled out from under it - and
// a failed fetch leaves the last good list on the screen rather than blanking
// the overlay until the next attempt succeeds.
void CGalaxyATMSystemPlugin::StartSigmetFetch()
{
    if (!m_config.SigmetsEnabled())
        return;

    if (m_sigmetFetch.joinable())
        m_sigmetFetch.join();   // the previous fetch is long finished - its stages are all timed out

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

    // Only our own airport matters here; ICAO codes are plain ASCII so a
    // narrow comparison against the configured (wide) airport is fine.
    char cfgAirport[16] = { 0 };
    WideCharToMultiByte(CP_ACP, 0, m_config.Airport().c_str(), -1, cfgAirport, sizeof(cfgAirport) - 1, NULL, NULL);
    if (_stricmp(sStation, cfgAirport) != 0)
        return;

    int hpa = ParseQnhHpa(sFullMetar);
    if (hpa <= 0)
        return;

    // EuroScope's own report is authoritative - from here on the fetched one
    // is ignored, so a sweatbox/simulator session can't be overwritten by live
    // weather off the internet.
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
        m_metarFetch.join();   // the previous fetch is long finished - its stages are all timed out

    m_metarFetch = std::thread([this, station]()
        {
            std::string body;
            // A METAR is a single short line; anything bigger isn't one.
            if (!Net::HttpGet("https://metar.vatsim.net/metar.php?id=" + station, body, 4096))
                return;
            int hpa = ParseQnhHpa(body);
            if (hpa > 0)
                m_fetchedQnhHpa.store(hpa);
        });
}

void CGalaxyATMSystemPlugin::OnTimer(int Counter)
{
    // The position the squawk server knows us by - it has no key to go on, only
    // whether this callsign is really controlling on the network right now, so
    // the worker thread has to be told when we log in or change position.
    m_squawk.SetPosition(MyPosition());

    // Then, and every tick: a code the server has just handed out is what a
    // controller is waiting to read out to the pilot.
    ApplySquawkAnswers();

    // SIGMETs are re-fetched whatever the METAR is doing: they come and go on
    // their own schedule, and a report that has been cancelled has to leave
    // the screen as surely as a new one has to arrive on it.
    int sigmetPeriod = max(60, m_config.SigmetRefreshMinutes() * 60);
    if (Counter > 0 && Counter % sigmetPeriod == 0)
        StartSigmetFetch();

    // The ATIS letter changes with every new report, and a stale one on the
    // strip is worse than none - it is polled on its own, shorter clock.
    int atisPeriod = max(60, m_config.AtisRefreshMinutes() * 60);
    if (Counter > 0 && Counter % atisPeriod == 0)
        StartAtisFetch();

    // The plan is republished during the day, and a booking added an hour ago
    // has to reach the screen well before it starts.
    int aupPeriod = max(60, m_config.AupRefreshMinutes() * 60);
    if (Counter > 0 && Counter % aupPeriod == 0)
        StartAupFetch();

    // Slower than the plan by default: a NOTAM is published hours before it
    // starts, where a booking can be added to the plan for the same hour.
    int notamPeriod = max(60, m_config.NotamRefreshMinutes() * 60);
    if (Counter > 0 && Counter % notamPeriod == 0)
        StartNotamFetch();

    if (m_gotLiveMetar)
        return;

    int hpa = m_fetchedQnhHpa.exchange(0);
    if (hpa > 0)
        ApplyQnhHpa(hpa);

    // Re-fetch every 15 minutes for as long as EuroScope hasn't delivered a
    // METAR of its own, so the readout can't sit frozen at its startup value
    // for a whole session.
    if (Counter > 0 && Counter % 900 == 0)
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

// ---- APW ------------------------------------------------------------------
// Which areas are up, and the band each of them takes, worked out on the clock
// - not per tag. It is the same question UpdateZoneActivity answers for the
// overlay and it is answered the same way, so what warns is exactly what is
// drawn: the plan's bookings, the NOTAMs when a source is configured, and the
// permanent areas always.
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
    // The track over the ground rather than a reported heading: it is what the
    // вектор экстраполяции is drawn along, and the warning must agree with the
    // line the controller is looking at.
    track.trackDeg = target.GetTrackHeading();
    track.gsKt = target.GetGS();
    track.vsFpm = target.GetVerticalSpeed();

    // The areas are published the way the AIP writes them - a floor on the
    // ground or on QNH, a ceiling as a flight level - so the level handed over
    // is the one the tags themselves show: QNH below the transition level and
    // the standard-pressure level above it.
    const bool belowTL = pos.GetFlightLevel() / 100 < TransitionLevelFL();
    track.altFt = belowTL ? pos.GetPressureAltitude() : pos.GetFlightLevel();

    entry.result = ApwCheck(m_config.Zones(), m_apwZones, track, cfg);
    entry.tick = now;

    // The cache is per callsign and a session sees a great many of them, so
    // whatever has not been asked about for a minute is dropped. Done here
    // rather than on a timer: this is the only thing that fills it.
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

    // Фильтр высоты applies to every item this plugin contributes: outside the
    // От/До band the tag item simply stays blank.
    //
    // Except the APW. It is a safety net, and a safety net that a display
    // filter can switch off is not one: an aircraft the controller has filtered
    // out of their own band still infringes the airspace it flies into. And
    // except the squawk, which lives in the Departure list, on aircraft still
    // on the ground and well below any band.
    if (ItemCode != TAG_ITEM_APW && ItemCode != TAG_ITEM_SQUAWK && RadarTarget.IsValid())
    {
        CRadarTargetPositionData filterPos = RadarTarget.GetPosition();
        if (filterPos.IsValid() && !AltFilterPasses(filterPos.GetPressureAltitude()))
            return;
    }

    switch (ItemCode)
    {
    case TAG_ITEM_ALTITUDE:
    {
        if (!RadarTarget.IsValid())
            return;
        CRadarTargetPositionData pos = RadarTarget.GetPosition();
        if (!pos.IsValid())
            return;
        // GetFlightLevel() is the standard-pressure level that decides which
        // side of the TL we are on; GetPressureAltitude() is the QNH altitude
        // that "A" reports once we are below it.
        bool belowTL = pos.GetFlightLevel() / 100 < TransitionLevelFL();
        int altFt = belowTL ? pos.GetPressureAltitude() : pos.GetFlightLevel();
        strcpy_s(sItemString, 16, FormatAltitudeUnit(altFt, m_unitAlt, belowTL).c_str());
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
            return;   // blank, which is what an item that is not warning must be

        // The word first and always in the same place, so that a row of tags
        // is read down the same column; the designator only when the config
        // asks for it, and only as much of it as the item can carry.
        std::wstring text = L"APW";
        if (m_config.Apw().showZone && !apw.zoneId.empty())
            text += L" " + apw.zoneId;
        strcpy_s(sItemString, 16, Narrow(text.substr(0, 15)).c_str());

        // Severity by colour, which is how every system of this kind says it:
        // red for airspace it is already in, amber for airspace it is about to
        // be in and still has time to be turned away from.
        *pColorCode = EuroScopePlugIn::TAG_COLOR_RGB_DEFINED;
        *pRGB = (apw.level == ApwLevel::Inside) ? Theme::ApwInside : Theme::ApwPredicted;
        break;
    }
    case TAG_ITEM_SQUAWK:
    {
        if (!FlightPlan.IsValid())
            return;

        std::string callsign = FlightPlan.GetCallsign();

        // While the server is being asked, and when it has said no, the column
        // says so in words rather than with a code: every colour a code can
        // take has a meaning of its own, and neither of these is one of them.
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
        // The formular's half of the column's yellow: what the transponder is
        // showing, when it is not what was assigned. A matching code, or no
        // code assigned at all, needs no second look and leaves it blank.
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

// ---- Squawks ----------------------------------------------------------------
// The column ("ULLL Squawk") goes into the Departure list with one of the two
// functions on its click: "ULLL Squawk assign" takes a code straight away,
// "ULLL Squawk menu" opens the menu with the rest.
void CGalaxyATMSystemPlugin::ConfigureSquawk()
{
    // With Squawk.Debug on, everything the client does is appended to
    // squawk-debug.log beside the plug-in - the worker thread has nowhere else
    // to say what the server answered.
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

// Everything that goes into EuroScope's own windows - popup lists, popup edits
// and the message channel - is written in ASCII: EuroScope draws them with a
// western charset, and Cyrillic handed to it comes out as "Âûäàòü êîä". The
// plugin's own panel draws its text itself and stays in Russian.
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
        // A sweatbox spends the same pool the live network does, so it is let
        // in only on purpose - Squawk.AllowSweatbox, for trying the thing out.
        int connection = GetConnectionType();
        bool live = (connection == CONNECTION_TYPE_DIRECT || connection == CONNECTION_TYPE_VIA_PROXY);
        bool sim = (connection == CONNECTION_TYPE_SWEATBOX
            || connection == CONNECTION_TYPE_SIMULATOR_SERVER
            || connection == CONNECTION_TYPE_SIMULATOR_CLIENT
            || connection == CONNECTION_TYPE_PLAYBACK);

        if (!live && !(sim && m_config.SquawkAllowSweatbox()))
        {
            why = sim
                ? "sweatbox: codes are off, set Squawk.AllowSweatbox in GalaxyATMSystem.json"
                : "not connected - codes are only handed out on the network";
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

// The order is the order of what is wrong: nothing assigned; the wrong code
// set - whatever mode the transponder is in; the right code, but no mode C.
// An aircraft with no radar target shows no code at all, which is not the
// code assigned either.
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
            // A report made on the controller's behalf fails quietly: the
            // column turns red, and nobody asked for a message.
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

        // EuroScope refuses an amendment it does not consider ours to make -
        // most often because the aircraft is not assumed. Saying so beats a
        // code that silently never appears in the column.
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

// One request for a code, with the two things that would otherwise drop it
// without a word said: nothing to ask on behalf of, and a client that quietly
// discards a request with no position on it.
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

// The column's clicks and the menu they open. Reached from both OnFunctionCall
// overrides - see the note on CGalaxyATMSystemPlugin::OnFunctionCall.
void CGalaxyATMSystemPlugin::HandleSquawkFunction(int FunctionId, const char* sItemString,
    RECT Area, const char* source)
{
    const bool mine = (FunctionId == TAG_FUNC_SQUAWK_ASSIGN || FunctionId == TAG_FUNC_SQUAWK_MENU
        || FunctionId == FN_SQUAWK_GET || FunctionId == FN_SQUAWK_NEW
        || FunctionId == FN_SQUAWK_MANUAL || FunctionId == FN_SQUAWK_MANUAL_EDIT);
    if (!mine)
        return;

    // One click can arrive down both routes; act on it once.
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
        // A click in a list row or on a tag makes that aircraft the selected
        // one before the function is called.
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

        // Set on the plan whatever the controller chose, and reported, so a
        // clash with another aircraft is said out loud.
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

// A code set on a flight plan by anything but this plugin's own answer: typed
// in by hand, or given by a controller who has no plugin. Every position that
// sees the change reports it, which the server takes as often as it comes -
// the same code for the same aircraft is simply "ok".
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

// ---- Radar screen -----------------------------------------------------------
CGalaxyATMSystemRadarScreen::CGalaxyATMSystemRadarScreen()
{
    m_panelArea = { 0, 0, 0, 0 };
    m_visible = true;
    m_collapsed = false;
    m_dragOffset = { 0, 0 };

    m_timerRunning = false;
    m_timerStartTick = 0;
    m_timerElapsedMs = 0;

    m_vecDistEnabled = false;
    m_vecDistKm = 10;
    m_vecTimeEnabled = true;
    m_vecTimeMin = 3;
    m_vecByPlan = false;
    m_vecShowLevel = false;

    m_openDropdown = DropdownKind::None;
    m_vecDistFieldRect = { 0, 0, 0, 0 };
    m_vecTimeFieldRect = { 0, 0, 0, 0 };

    m_esFont = NULL;
    m_rulerFont = NULL;
    m_rulerFontSource = NULL;

    m_osLines = 2;
    m_osSpeed = false;

    m_codeAll = false;
    m_codeBp = false;
    m_codeExtra = false;
    m_vvGain = 35;
    m_vvDragging = false;
    m_vvSliderRect = { 0, 0, 0, 0 };

    // БЛОК 4 units live on the plugin (see GalaxyATMSystem.h) - defaulted there.

    m_atisLetterOpen = true;
    m_atisLetterArea = { 0, 0, 0, 0 };

    m_rcOpen = false;
    m_rcArea = { 0, 0, 0, 0 };
    m_rcPositioned = false;
    m_rcScroll = 0;
    m_rcScrollMine = 0;
    m_rcSortKey = 0;
    m_rcSortAsc = true;

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

    // The config says whether the zones start up shown; ".zones" and the ASR
    // take it from there. Read off the plugin rather than through Plugin(),
    // which is GetPlugIn() and is not wired up until after this constructor.
    m_zonesVisible = (g_plugin != NULL) ? g_plugin->GetConfig().ZonesEnabled() : true;
    m_zoneInfoIndex = -1;
    m_zoneInfoAt = { 0, 0 };
    m_zoneInfoHeld = false;
    m_zoneInfoWait = 0;
    m_areaShiftDown = false;

    m_rulerButton = VK_XBUTTON2;   // the forward thumb button by default
    m_rulerButtonDown = false;
    m_rulerPressPending = false;
    m_rulerArmed = false;
    m_rulerPlacing = false;

    UINT_PTR id = SetTimer(NULL, 0, 1000, [](HWND, UINT, UINT_PTR idEvent, DWORD)
        {
            auto it = g_timers.find(idEvent);
            if (it != g_timers.end())
                it->second->RequestRefresh();
        });
    g_timers[id] = this;
    m_timerId = id;

    // 40 ms is fast enough that a thumb-button click is never missed and cheap
    // enough to be free - the sample is a GetAsyncKeyState and nothing else,
    // and a repaint is only asked for on an actual toggle.
    UINT_PTR pollId = SetTimer(NULL, 0, 40, [](HWND, UINT, UINT_PTR idEvent, DWORD)
        {
            auto it = g_pollTimers.find(idEvent);
            if (it != g_pollTimers.end())
                it->second->PollRulerButton();
        });
    g_pollTimers[pollId] = this;
    m_pollTimerId = pollId;
}

CGalaxyATMSystemRadarScreen::~CGalaxyATMSystemRadarScreen()
{
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
}

// ---- Derived data -----------------------------------------------------------
WorkMode CGalaxyATMSystemRadarScreen::GetWorkMode(std::wstring& labelOut, COLORREF& colorOut)
{
    int ct = GetPlugIn()->GetConnectionType();
    CController me = GetPlugIn()->ControllerMyself();
    int rating = me.IsValid() ? me.GetRating() : 0;

    // Supervisor / administrator rating maps to the РП (SUP) mode regardless of
    // how the connection was established.
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
    else
    {
        // No config entry yet: show what EuroScope knows so the panel is never
        // blank, and make the missing role obvious.
        designation = !posId.empty() ? Widen(posId.c_str()) : Widen(callsign.c_str());
        if (designation.empty())
            designation = L"—";
        role = L"—";
    }

    // The SDK does not expose the raw CID; the connected controller's full name
    // is the closest available identity.
    std::wstring full = me.IsValid() ? Widen(me.GetFullName()) : L"";
    user = full.empty() ? L"user ?" : full;
}

// Callsigns currently squawking a distress code. Both the mode-A code the
// aircraft is actually transmitting and the one assigned to it are checked, so
// a crew that has just selected 7700 shows up before the strip catches up.
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

// Mode-A codes that more than one aircraft is transmitting at the same time.
// The conspicuity codes every aircraft may legitimately share are skipped.
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

// ---- Small reusable widgets --------------------------------------------------
void CGalaxyATMSystemRadarScreen::DrawCheckbox(HDC hDC, RECT box, bool checked,
    int objType, const char* objId, const char* tooltip)
{
    // Ticked state is a plain grey square - the reference draws no tick mark.
    // Square checkboxes are the one control outlined in white rather than grey.
    Theme::OutlineBox(hDC, box, checked ? Theme::Active : Theme::ControlFill, Theme::BorderCheck);
    AddScreenObject(objType, objId, box, false, tooltip);
}

// A checkbox with its label to the right. The box is smaller than the row it
// sits on - the same square the code block's own checkbox is - so it is
// centred in the row rather than filling it, and the label keeps the full row
// height to be vertically centred in.
void CGalaxyATMSystemRadarScreen::DrawCheckRow(HDC hDC, int top, int x, const std::wstring& label,
    bool checked, int objType, const char* objId, const char* tooltip)
{
    int cy = top + (kRowH - kCheckSize) / 2;
    RECT chk = { x, cy, x + kCheckSize, cy + kCheckSize };
    DrawCheckbox(hDC, chk, checked, objType, objId, tooltip);

    RECT lbl = { chk.right + 6, top, ContentRight(), top + kRowH };
    Theme::DrawLine(hDC, lbl, label, m_fonts.Body, Theme::Text, DT_LEFT | DT_VCENTER);
}

// "Ед. изм." uses pills instead of squares, and packs its labels tight against
// them so two columns fit across 212 px - hence the explicit label right edge.
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

// For plates carrying values whose length isn't ours to control - a callsign,
// a position id, a controller's full name. An observer's "V_OBS" already
// overflows the narrow designation plate at the normal size, so pick the
// largest size that fits and only then fall back to clipping.
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
    bool active, int objType, const char* objId, const char* tooltip)
{
    if (active)
        Theme::DrawValueField(hDC, box, text, m_fonts.Body, true);
    else
        Theme::DrawControl(hDC, box, text, m_fonts.Body);
    AddScreenObject(objType, objId, box, false, tooltip);
}

// The reference's dropdowns are one plate split by a hairline: the value sits
// left-aligned in the wide part, and a small marker in the narrow part on the
// right opens the list. Only that part is clickable - the value area is inert.
void CGalaxyATMSystemRadarScreen::DrawDropdownField(HDC hDC, RECT box, const std::wstring& text,
    int objType, const char* objId, const char* tooltip)
{
    Theme::OutlineBox(hDC, box, Theme::ControlFill, Theme::BorderStrong);

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
    Ellipse(hDC, mx - 2, my - 2, mx + 2, my + 2);   // 4 px across - a marker, not a bullet
    SelectObject(hDC, oldPen);
    DeleteObject(dotPen);
    SelectObject(hDC, oldBr);
    DeleteObject(br);

    RECT textRect = { box.left + 5, box.top, chevron.left - 2, box.bottom };
    Theme::DrawLine(hDC, textRect, text, m_fonts.Body, Theme::Text, DT_LEFT | DT_VCENTER);

    AddScreenObject(objType, objId, chevron, false, tooltip);
}

// The open dropdown, drawn last so it sits over whatever is beneath it. Rows
// are the panel's own plates: near-black on an outlined list, with the current
// value carrying the same grey fill that marks any other selected control.
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
    else
    {
        return;
    }

    const int rowH = 17;   // the panel's own row height, so the list matches it
    const int listW = max(anchor.right - anchor.left, 44);
    const int listH = count * rowH + 2;

    RECT list = { anchor.left, anchor.bottom + 2, anchor.left + listW, anchor.bottom + 2 + listH };

    // Drop upwards instead if there isn't room below, and never off the side.
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

// A block's caption sits centred above its group box - not cut into the
// border line - and the box itself is a plain square-cornered outline.
RECT CGalaxyATMSystemRadarScreen::DrawBlockFrame(HDC hDC, int top, const std::wstring& caption, int boxHeight)
{
    RECT captionRect = { m_panelArea.left, top, m_panelArea.right, top + L::CAPTION_H };
    Theme::DrawLine(hDC, captionRect, caption, m_fonts.Body, Theme::Text, DT_CENTER | DT_VCENTER);

    RECT box = { GroupLeft(), captionRect.bottom + L::CAP_GAP, GroupRight(),
                 captionRect.bottom + L::CAP_GAP + boxHeight };
    Theme::OutlineBox(hDC, box, Theme::Background, Theme::Border);
    return box;
}

// The code block is the only one the reference leaves unlabelled.
RECT CGalaxyATMSystemRadarScreen::DrawBoxOnly(HDC hDC, int top, int boxHeight)
{
    RECT box = { GroupLeft(), top, GroupRight(), top + boxHeight };
    Theme::OutlineBox(hDC, box, Theme::Background, Theme::Border);
    return box;
}

// ---- Drawing ------------------------------------------------------------------
void CGalaxyATMSystemRadarScreen::OnRefresh(HDC hDC, int Phase)
{
    if (Phase != REFRESH_PHASE_BEFORE_TAGS &&
        Phase != REFRESH_PHASE_AFTER_TAGS &&
        Phase != REFRESH_PHASE_AFTER_LISTS)
        return;
    if (!m_visible)
        return;

    m_fonts.EnsureCreated();

    // Which зоны are up at this moment, before anything draws or registers a
    // hit-box off them.
    UpdateZoneActivity();

    // The list is taken once per frame and held for the whole of it, so the
    // overlay, its hit-boxes and an open info window can never disagree about
    // which report is which because a fetch landed halfway through.
    m_sigmets = Plugin()->Sigmets();

    // Зоны and сигметы sit under the tags: they are a background the traffic
    // is read against, not something to be read over a label.
    if (Phase == REFRESH_PHASE_BEFORE_TAGS)
    {
        DrawZones(hDC);
        DrawSigmets(hDC);
        return;
    }

    // Whatever font EuroScope had selected when it handed us the DC is the one
    // it draws its own tags with, and it is the only way to get at it - the SDK
    // exposes no font query. Grabbed before anything of ours touches the DC so
    // the predicted-level label on a vector can match the tag beside it. If all
    // that is on the DC is a stock font then EuroScope has not put its own
    // there, and the label falls back to the monospace that matches how it
    // renders tags rather than to GDI's default.
    HFONT dcFont = (HFONT)GetCurrentObject(hDC, OBJ_FONT);
    m_esFont = (dcFont != NULL
                && dcFont != (HFONT)GetStockObject(SYSTEM_FONT)
                && dcFont != (HFONT)GetStockObject(DEVICE_DEFAULT_FONT)
                && dcFont != (HFONT)GetStockObject(DEFAULT_GUI_FONT))
        ? dcFont : m_fonts.Mono;


    // Radar overlays belong with the traffic they annotate, so they go in the
    // tag phase. The panel and its windows go in the very last phase instead,
    // which puts them over EuroScope's lists as well as over the tags.
    if (Phase == REFRESH_PHASE_AFTER_TAGS)
    {
        // Зоны answer the mouse only while Shift is held. A hit-box of ours
        // takes the press whatever lies over it - a label included, in
        // whichever phase it is registered - and a зона's boxes cover its whole
        // inside, so with them always up no label inside an area could be
        // dragged. Kept up while a зона's window is open as well, so the
        // release still comes back to us when Shift is let go first. Сигметы
        // are boxed along their outline only and stay up all the time.
        //
        // First of ours in this pass, so everything registered after it -
        // the ruler's own boxes, then the whole panel - wins the click.
        m_areaShiftDown = ShiftHeldInEuroScope();
        if (m_areaShiftDown || m_zoneInfoIndex >= 0)
            RegisterZoneObjects();
        RegisterSigmetObjects();

        // A press of the side button arrives on the poll timer, outside any
        // refresh; it is only latched there and acted on here. The press does
        // not place anything by itself - it arms the ruler, and the two points
        // are then picked with ordinary left clicks on the radar (see
        // OnClickScreenObject). While a line is being placed its free end is
        // re-read from the cursor every frame, which is what makes it follow
        // the cursor without anything being held down.
        if (m_rulerPressPending)
        {
            m_rulerPressPending = false;
            if (m_rulerPlacing)
            {
                // Part-way through a line, the button abandons it rather than
                // arming a second one on top of it.
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

        // A small, non-draggable hit-box hugging each line, so a right-click
        // or a left double-click can delete a measurement - Moveable=false
        // means a drag started here still reaches EuroScope's own panning
        // untouched, so the radar can always be panned no matter how many
        // lines are on it; only a plain click or double-click on this exact
        // spot is ours.
        //
        // One rect per line would have to be its two endpoints' bounding box,
        // which for anything but a horizontal or vertical line is far bigger
        // than the line itself - a long diagonal easily covers most of the
        // radar, and a click anywhere inside it would then offer to delete a
        // line that is nowhere near the cursor. A chain of small boxes sampled
        // along the actual path stays tight to the line regardless of angle.
        //
        // The chain stops short of both endpoints rather than running the
        // whole 0..1 range: an endpoint snapped to a target sits right on that
        // target's own symbol, and a delete box there would sit over the tag
        // and the target the line is measuring from. Deletion still works from
        // anywhere along the line's middle stretch.
        for (size_t i = 0; i < m_rulers.size(); i++)
        {
            RulerLine& r = m_rulers[i];
            POINT a = ConvertCoordFromPositionToPixel(ResolveRulerPoint(r.startSnapped, r.startCallsign, r.startFixed));
            POINT b = ConvertCoordFromPositionToPixel(ResolveRulerPoint(r.endSnapped, r.endCallsign, r.endFixed));

            double lineLen = sqrt((double)(b.x - a.x) * (b.x - a.x) + (double)(b.y - a.y) * (b.y - a.y));
            const int kPad = 15;
            const double kStepPx = 20.0;
            // Kept clear at each end so the near edge of the first box still
            // sits outside FindNearbyTarget's own 20 px snap radius around
            // whatever the endpoint is anchored to.
            const double kEndClearPx = kPad + 20.0;

            char id[16];
            sprintf_s(id, "%zu", i);

            if (lineLen <= 2.0 * kEndClearPx)
            {
                // Too short to leave anything in the middle once both ends are
                // cleared - a single box at the midpoint is the best that fits,
                // and still keeps clear of either endpoint.
                int px = (a.x + b.x) / 2, py = (a.y + b.y) / 2;
                RECT box = { px - kPad, py - kPad, px + kPad, py + kPad };
                AddScreenObject(SO_RULER_LINE, id, box, false, "ПКМ/2ЛКМ - удалить линейку");
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
                AddScreenObject(SO_RULER_LINE, id, box, false, "ПКМ/2ЛКМ - удалить линейку");
            }
        }

        // While the ruler is armed - and for as long as a line is half-placed -
        // the whole radar answers a click. Registered last of this phase's
        // objects so it wins over the сигмет areas and the line-delete boxes
        // beneath it, and non-moveable, so a drag started on it still reaches
        // EuroScope's own panning untouched. Nothing covers the radar at any
        // other time.
        if (m_rulerArmed || m_rulerPlacing)
        {
            AddScreenObject(SO_RULER_CANVAS, "RULER_CANVAS", GetRadarArea(), false,
                m_rulerPlacing ? "ЛКМ - конец линейки, ПКМ - отмена"
                               : "ЛКМ - начало линейки, ПКМ - отмена");
        }

        // Always up - the wake category is not a setting of БЛОК 3's.
        DrawWakeArcs(hDC);

        if (m_vecDistEnabled || m_vecTimeEnabled || m_vecByPlan)
            DrawTargetVectors(hDC);

        for (size_t i = 0; i < m_rulers.size(); i++)
            DrawRulerLine(hDC, m_rulers[i], (int)i);
        if (m_rulerPlacing)
            DrawRulerLine(hDC, m_rulerPending);
        else if (m_rulerArmed)
            DrawRulerCursor(hDC);

        return;
    }

    DrawPanel(hDC);

    if (m_atisLetterOpen)
        DrawAtisLetterWindow(hDC);
    if (m_atisOpen)
        DrawAtisWindow(hDC);
    if (m_rcOpen)
        DrawSectorListWindow(hDC);

    // Last, so it paints over everything and its rows win the click.
    if (m_openDropdown != DropdownKind::None)
        DrawDropdownList(hDC);

    // The сигмет and зона windows are only up while the button is held, so they
    // go over even the dropdown - nothing else can be interacted with meanwhile.
    if (m_zoneInfoIndex >= 0)
        DrawZoneInfo(hDC);
    if (m_sigmetInfoIndex >= 0)
        DrawSigmetInfo(hDC);
}

// ---- Сигметы ----------------------------------------------------------
// One ring of one report, in this frame's pixels. Returns false when there is
// nothing worth drawing: too few points, or the whole ring off the screen.
//
// The whole ring is culled here on its bounding box; the edges that survive
// are clipped one by one where they are used. This is only the cheap first
// pass - it decides whether the ring is worth looking at at all.
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

    // Inflated by a pixel first: a ring whose points happen to be collinear
    // has a bounding box of no height, and an empty rectangle intersects
    // nothing as far as IntersectRect is concerned.
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

    // No fill: the area is an outline and nothing else, so the map and the
    // traffic inside it are as readable as they are anywhere else.
    HPEN pen = CreatePen(PS_SOLID, Theme::SigmetWidth, Theme::SigmetLine);
    HPEN oldPen = (HPEN)SelectObject(hDC, pen);

    // Drawn edge by edge rather than with Polygon/Polyline, so every edge can
    // be clipped to the radar area on the way out. The area carries no fill,
    // so a ring is nothing but its edges anyway and the two are identical -
    // except that this one survives an area far wider than the display.
    RECT clip = GetRadarArea();
    std::vector<POINT> pts;
    for (const Sigmet& sig : *m_sigmets)
    {
        for (const std::vector<EuroScopePlugIn::CPosition>& ring : sig.rings)
        {
            if (!SigmetOutline(ring, pts))
                continue;

            size_t segments = sig.closed ? pts.size() : pts.size() - 1;
            for (size_t seg = 0; seg < segments; seg++)
            {
                POINT a = pts[seg], b = pts[(seg + 1) % pts.size()];
                if (!Geom::ClipSegment(clip, a, b))
                    continue;
                MoveToEx(hDC, a.x, a.y, NULL);
                LineTo(hDC, b.x, b.y);
            }
        }
    }

    SelectObject(hDC, oldPen);
    DeleteObject(pen);
    RestoreDC(hDC, saved);
}

// The hit-boxes are registered a phase later than the areas are drawn. Drawing
// belongs under the tags; registering belongs in the same pass as everything
// else of ours, first in it, so a сигмет never takes a click away from a
// ruler line or from the panel on top of it.
//
// Boxes are sampled along the outline rather than being one box per area: an
// area's bounding box is most of the radar at a normal zoom and would swallow
// every click inside it. The area carries no fill, so its outline is what
// there is to aim at anyway.
void CGalaxyATMSystemRadarScreen::RegisterSigmetObjects()
{
    if (!m_sigmetsVisible || !m_sigmets || m_sigmets->empty())
        return;

    RECT ra = GetRadarArea();
    // Boxes 24 px across every 24 px: they meet without overlapping, and the
    // half-width matches the tolerance FindSigmetAt applies afterwards.
    const int kPad = 12;
    const double kStepPx = 24.0;

    // A ceiling on the whole overlay. Nothing normal comes close - it is there
    // so that a jagged area at a very close zoom cannot quietly hand EuroScope
    // tens of thousands of objects to hit-test on every mouse move.
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
                // Clipped first, then sampled: sampling the whole edge and
                // throwing away what falls off the screen would put every
                // sample off it once the edge is much wider than the display.
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

// Which report is under the cursor. The press is routed to us by whichever
// hit-box it landed on, but overlapping areas share their boxes, so the
// decision is made here on the real geometry: an area the cursor is inside
// wins, otherwise the one whose outline runs nearest to it.
int CGalaxyATMSystemRadarScreen::FindSigmetAt(POINT pt)
{
    if (!m_sigmets)
        return -1;

    int best = -1;
    double bestDist = 12.0;   // pixels; a miss by more than this is a miss
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

// ---- Зоны запретов и ограничений -------------------------------------------
// Static areas off the config file, drawn in the same phase and the same way as
// the сигметы: an outline with no fill so the traffic inside stays readable,
// and the designator on the area itself so it can be named without opening it.
// The colours themselves come off the config file - Theme only holds what they
// fall back to - so a position can retune the three kinds in the JSON and
// reload, without a build.

bool CGalaxyATMSystemRadarScreen::ZoneOutline(const Zone& zone, std::vector<POINT>& out)
{
    return SigmetOutline(zone.ring, out);
}

// An area that is not up at this moment is not drawn at all, so this is what
// decides what the whole overlay consists of. Worked out once per frame rather
// than per lookup: the answer changes with the clock, and an outline drawn from
// one answer with hit-boxes built from another would let a click land on a zone
// that is not on the screen.
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
    // Left null while nothing has been read: that is what tells an area
    // hanging on a NOTAM that nobody can answer for it.
    what.notams = m_notams ? m_notams.get() : NULL;
    what.showNotamWhenUnknown = cfg.ShowNotamAreas();

    const time_t now = time(NULL);   // UTC, like everything else on the panel

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

    RECT clip = GetRadarArea();
    std::vector<POINT> pts;

    // A package carries a few hundred areas, so the three pens are made once
    // for the frame and selected per area rather than created per area.
    const Config& cfg = Plugin()->GetConfig();
    const ZoneStyle& styleP = cfg.ZoneStyleFor(ZoneKind::Prohibited);
    const ZoneStyle& styleR = cfg.ZoneStyleFor(ZoneKind::Restricted);
    const ZoneStyle& styleD = cfg.ZoneStyleFor(ZoneKind::Danger);
    HPEN pens[3] = {
        CreatePen(PS_SOLID, Theme::ZoneWidth, styleP.line),
        CreatePen(PS_SOLID, Theme::ZoneWidth, styleR.line),
        CreatePen(PS_SOLID, Theme::ZoneWidth, styleD.line),
    };
    HPEN oldPen = (HPEN)SelectObject(hDC, pens[1]);

    // Whatever EuroScope had clipped the DC to, kept so that each area's wash
    // can be clipped to its own shape and the clip put straight back - the
    // outlines drawn after it must not be clipped to the inside of the area.
    HRGN baseClip = CreateRectRgn(0, 0, 1, 1);
    const bool hadClip = (GetClipRgn(hDC, baseClip) == 1);

    for (size_t i = 0; i < zones.size(); i++)
    {
        if (i >= m_zoneActive.size() || !m_zoneActive[i])
            continue;

        const Zone& zone = zones[i];
        if (!ZoneOutline(zone, pts))
            continue;

        SelectObject(hDC, pens[(zone.kind == ZoneKind::Prohibited) ? 0
            : (zone.kind == ZoneKind::Danger) ? 2 : 1]);

        // The wash inside the outline. Painted through a region of the area's
        // own shape, and only over the part of it that is on screen: an area
        // can run far past the display, and blending its whole bounding box
        // would cost a great deal of it for nothing.
        {
            RECT bbox = { LONG_MAX, LONG_MAX, LONG_MIN, LONG_MIN };
            for (const POINT& p : pts)
            {
                bbox.left = min(bbox.left, p.x);
                bbox.top = min(bbox.top, p.y);
                bbox.right = max(bbox.right, p.x);
                bbox.bottom = max(bbox.bottom, p.y);
            }

            RECT paint;
            if (IntersectRect(&paint, &bbox, &clip))
            {
                HRGN rgn = CreatePolygonRgn(pts.data(), (int)pts.size(), WINDING);
                if (rgn != NULL)
                {
                    SelectClipRgn(hDC, rgn);
                    const ZoneStyle& style = (zone.kind == ZoneKind::Prohibited) ? styleP
                        : (zone.kind == ZoneKind::Danger) ? styleD : styleR;
                    FillAlpha(hDC, paint, style.fill, style.alpha);
                    SelectClipRgn(hDC, hadClip ? baseClip : NULL);
                    DeleteObject(rgn);
                }
            }
        }

        // Edge by edge and clipped on the way out, so an area far wider than
        // the display still draws the part of it that is on screen.
        for (size_t seg = 0; seg < pts.size(); seg++)
        {
            POINT a = pts[seg], b = pts[(seg + 1) % pts.size()];
            if (!Geom::ClipSegment(clip, a, b))
                continue;
            MoveToEx(hDC, a.x, a.y, NULL);
            LineTo(hDC, b.x, b.y);
        }

        // No designator drawn on the area itself: at a normal zoom several of
        // them overlap, and their names sat on top of the traffic they are
        // there to keep clear of. The name is on the hover tooltip and in the
        // window a click opens.
    }

    SelectObject(hDC, oldPen);
    for (HPEN p : pens)
        DeleteObject(p);
    DeleteObject(baseClip);

    RestoreDC(hDC, saved);
}

// Hit-boxes, registered in the same pass as the сигмет ones - see the note on
// RegisterSigmetObjects - but only while Shift is held (see OnRefresh).
//
// The screen is ruled into squares and every square an outline runs through
// becomes one box. It is done that way because the two obvious ways are both
// wrong on the picture this position actually works with - a few hundred areas
// off the TopSky package, a hundred and sixty of them up at once:
//
//   - a box per point spends everything it has on the first areas in the file.
//     A circle arrives as seventy-two points, the package writes every
//     запретная зона before the first ограничительная one, and the R areas were
//     left without a single box: drawn, named on hover, and dead to the button.
//
//   - a share of the boxes per area keeps them all alive but spaces them by
//     what each area can afford, so on a wide picture the boxes stop touching
//     and the outline between two of them answers to nothing. That is the
//     "sometimes it opens, sometimes it doesn't" of a zoomed-out screen.
//
// Squares have neither failure: they tile, so a press anywhere on an area
// lands on a box; areas sharing a square share its box, so what the whole
// overlay costs is bounded by the screen rather than by the package; and the
// square knows which area it answers with, which is the one it names and the
// one a press on it opens.
//
// Both the outline and the inside of an area are covered. The outline on its
// own was too fine an aim for an area the size of Кронштадт: what a controller
// points at is the piece of airspace, not the line round it. The squares an
// outline runs through still win over the squares that only fall inside one,
// so a shared edge answers with the area whose line was pressed; where two
// areas overlap inside, the smaller one wins, since the bigger one can always
// be pressed somewhere the smaller is not.
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

    const int kCellPx = 24;      // a square, and so the aim it asks for
    const int kMaxCellPx = 96;
    const int kMaxBoxes = 3000;

    // The areas with something on the screen, with the outline kept as it
    // comes out: projecting a few hundred rings is the expensive part of this
    // and must not be done again for every square.
    struct Outline
    {
        size_t index;
        std::vector<POINT> pts;
        double area;        // in pixels, and only used to rank two overlapping insides
    };
    std::vector<Outline> visible;

    std::vector<POINT> pts;
    for (size_t i = 0; i < zones.size(); i++)
    {
        if (i >= m_zoneActive.size() || !m_zoneActive[i])
            continue;
        if (!ZoneOutline(zones[i], pts))
            continue;

        // On screen if any edge crosses it, and also if the display sits
        // wholly inside the area - zoomed in far enough that no edge is left
        // on the picture, the airspace underneath still has to answer.
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

        double twice = 0.0;   // the shoelace, kept as twice the area and unsigned
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

    // One square and the area it answers with. 'rank' is 0 for a square an
    // outline runs through and 1 for one that only falls inside an area, and
    // it is compared before anything else: the line wins the square it is on.
    // 'score' ranks two of the same kind - the outline nearest the square's
    // middle, or the smaller of two areas the square is inside.
    struct Square
    {
        int    index;    // -1 while the square is unclaimed
        int    rank;
        double score;
    };

    // A flat grid rather than a map: an area covering the whole display claims
    // every square on it, and with a hundred and sixty of them up that is a
    // few hundred thousand claims a frame - each one has to be an index and a
    // compare, not a tree walk.
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

        // Everything below claims through this, so the two passes cannot
        // disagree about what beats what.
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

        // The insides first, so that the outlines below take back the squares
        // the two share.
        //
        // Scanned a row of squares at a time: one horizontal line through the
        // middle of the row, crossed with every edge, and the spans between
        // its crossings are inside the area. That is the whole of the inside
        // for the cost of the ring's own points, however wide the area is -
        // testing every square against the polygon instead would be the same
        // work multiplied by the number of squares.
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
                    // Half-open in y, so a vertex sitting exactly on the line
                    // is counted once rather than twice or not at all.
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

        // Half a square between samples, so a diagonal cannot step over one.
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

    // The tooltip is built once per area rather than once per square: a
    // hundred and sixty areas can hold several thousand squares between them.
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

// The area a hit-box was registered for. Used when the press landed on a box
// but on no outline - see OnButtonDownScreenObject.
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

// Which zone was clicked. The press arrives through whichever hit-box it landed
// on, but neighbouring areas share their boxes, so the decision is made here on
// the real geometry: an area the click is inside wins, otherwise the one whose
// outline runs nearest to it.
int CGalaxyATMSystemRadarScreen::FindZoneAt(POINT pt)
{
    const std::vector<Zone>& zones = Plugin()->GetConfig().Zones();

    int best = -1;
    // Pixels; a miss by more than this is a miss. Kept in step with the squares
    // the hit-boxes are laid on, so that a press anywhere in one still reaches
    // the outline that square was laid for.
    double bestDist = 18.0;

    // The smallest area the press is inside, which beats any outline it merely
    // passed near. Smallest rather than first: зоны nest, and a press inside a
    // small опасная зона that sits within a great ограничительная one is a
    // press on the small one - the big one can be pressed anywhere else.
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

// The details of the area under the cursor, in the same half-transparent shade
// the сигмет window uses and up for exactly as long: while the button is held.
void CGalaxyATMSystemRadarScreen::DrawZoneInfo(HDC hDC)
{
    const std::vector<Zone>& zones = Plugin()->GetConfig().Zones();
    if (m_zoneInfoIndex < 0 || (size_t)m_zoneInfoIndex >= zones.size())
    {
        m_zoneInfoIndex = -1;
        return;
    }

    // An area whose booking has run out while its window was open goes off the
    // screen with the outline it belonged to.
    if ((size_t)m_zoneInfoIndex >= m_zoneActive.size() || !m_zoneActive[m_zoneInfoIndex])
    {
        m_zoneInfoIndex = -1;
        return;
    }

    // Four lines at one size, the way the real system writes them: the
    // designator, the two ends of the booking, and the band of levels. A
    // permanent area has no booking and so has neither of the middle lines.
    const Zone& zone = zones[m_zoneInfoIndex];

    std::wstring text = zone.id.empty() ? zone.name : zone.id;

    const ZoneBooking* booking = ((size_t)m_zoneInfoIndex < m_zoneBooking.size())
        ? m_zoneBooking[m_zoneInfoIndex] : NULL;

    if (booking != NULL)
    {
        // "11:00 01-01-2026", in UTC like every other time on the panel.
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

    // The booked band when there is one - a booking can take less of the area
    // than the area itself publishes - and the published limits otherwise.
    std::wstring levels = (booking != NULL)
        ? ZoneLevelText(booking->minFL) + L"-" + ZoneLevelText(booking->maxFL)
        : zone.LevelBand();
    if (!levels.empty())
        text += L"\n" + levels;

    if (!zone.note.empty())
        text += L"\n" + zone.note;

    // Sized to the text: four short lines make a small plate, and only a long
    // note stretches it. The window is the readout, not a panel to fill.
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

    // A hairline round the shade, the same as the сигмет window's, so the
    // plate has an edge to be read against the wash of the area it lands on.
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

// The window is open only while the button is held. EuroScope reports the
// release through OnButtonUpScreenObject, but only when the cursor is still on
// one of our objects - let go after dragging off the outline and no event ever
// arrives - so the button state is watched on the poll timer as well, and that
// is what actually guarantees the window closes.
//
// The button is not assumed to be down the moment the window opens: if the
// press reaches us late enough that it has already been released, the poll
// would close the window on its first tick and the report would never be
// readable. So the release only counts once the poll has seen the button
// actually held - with a short grace period, so a press that is never
// confirmed cannot leave the window up for good either.
void CGalaxyATMSystemRadarScreen::CloseSigmetInfoIfButtonReleased()
{
    if (m_sigmetInfoIndex < 0 && m_zoneInfoIndex < 0)
        return;

    const bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

    // ~0.5 s at the poll timer's 40 ms.
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

// The report itself, in white on black at half opacity, hung off the point the
// button went down at and pushed back inside the radar area if it would hang
// off the edge.
//
// Nothing else: no border, no rules, and no decoded summary above it. The
// message already says the hazard, the levels, the validity and the movement,
// in the words a controller reads them in everywhere else - repeating them in
// a second form above it only made the window taller.
//
// The width is measured from the message rather than fixed, so a report's own
// line breaks are what shows on the screen and nothing is re-wrapped that the
// issuing office did not wrap itself.
void CGalaxyATMSystemRadarScreen::DrawSigmetInfo(HDC hDC)
{
    if (!m_sigmets || m_sigmetInfoIndex < 0 || (size_t)m_sigmetInfoIndex >= m_sigmets->size())
    {
        m_sigmetInfoIndex = -1;
        return;
    }

    const Sigmet& sig = (*m_sigmets)[m_sigmetInfoIndex];

    const int kPadX = 10, kPadY = 8;
    // A narrow column rather than one wide enough for a SIGMET's own ~69
    // character lines: the report wraps into more, shorter lines, which reads
    // better beside the area it belongs to and keeps the window from lying
    // across half the radar. Measured over the live feed, this puts a typical
    // report at about 400 x 140 and the longest at 400 x 304.
    const int kMaxW = 400, kMinW = 260;

    // Every report in the feed carries its own text, but a window with nothing
    // in it would be worse than the one-line identity if one ever did not.
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

    // A hairline round the shade in the same white as the report itself, so
    // the window has an edge to be read against whatever it lands on.
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

// Where the panel's top edge sits, and with it everything docked to the panel.
// Anchored to the toolbar rather than to the radar area: EuroScope shrinks the
// radar area whenever it docks a list or the chat pane along the top, and a
// header tied to that edge slides down the screen and back as those come and
// go. The toolbar's own bottom edge stays where it is, so the clock does too.
int CGalaxyATMSystemRadarScreen::PanelTop()
{
    RECT ra = GetRadarArea();
    RECT tb = GetToolbarArea();

    // Only when the toolbar is the strip along the top, which is where it
    // normally lives: anchoring to one parked anywhere else would drop the
    // panel somewhere far worse than the edge it was tied to before.
    if (tb.bottom > 0 && tb.top <= ra.top && tb.bottom <= ra.top)
        return tb.bottom;
    return ra.top;
}

void CGalaxyATMSystemRadarScreen::DrawPanel(HDC hDC)
{
    // Collapsed, nothing is left but a small clock/date window in the corner:
    // not the sidebar with its blocks hidden, but a narrower card the height of
    // the header alone, so the radar underneath is handed back. Clicking its
    // "+" opens the panel again, without needing the ".ulll" command.
    int width = m_collapsed ? kCollapsedWidth : kPanelWidth;
    int height = m_collapsed
        ? L::HEADER_H + L::COLLAPSED_BOT_PAD
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

    // The panel is docked rather than placed: it always sits flush in the top
    // right corner of the radar area and there is no way to move it - no drag
    // handle, and no saved position to restore. A window resize therefore
    // cannot leave it half off the screen either.
    RECT ra = GetRadarArea();
    m_panelArea.right = ra.right;
    m_panelArea.left = ra.right - width;
    m_panelArea.top = PanelTop();
    m_panelArea.bottom = m_panelArea.top + height;

    int saved = SaveDC(hDC);
    SetBkMode(hDC, TRANSPARENT);

    // Expanded, the panel carries no outline - only the boxes inside it do -
    // and its background is stretched past the content to the bottom of the
    // radar area, so the card reads as a full-height sidebar rather than
    // stopping dead under the last block. m_panelArea itself (used for hit
    // testing) stays sized to the actual content. Collapsed there is no
    // sidebar to continue, so the little window is drawn to its own size and
    // given an outline of its own to read as a window against the radar.
    if (m_collapsed)
    {
        Theme::OutlineBox(hDC, m_panelArea, Theme::Background, Theme::Border);
    }
    else
    {
        RECT bgArea = m_panelArea;
        if (bgArea.bottom < ra.bottom)
            bgArea.bottom = ra.bottom;
        Theme::FillBox(hDC, bgArea, Theme::Background);
    }

    int y = DrawHeader(hDC, m_panelArea.top);
    if (!m_collapsed)
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
    // The header is not a drag handle: the panel is docked to the right edge
    // of the radar area and does not move, so the only thing registered here
    // is the collapse toggle.

    // "-" in the top-right corner, "+" once collapsed.
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
        m_collapsed ? "Развернуть панель" : "Свернуть панель");

    SYSTEMTIME st;
    GetSystemTime(&st); // already UTC

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

int CGalaxyATMSystemRadarScreen::DrawBlockTimer(HDC hDC, int y)
{
    RECT box = DrawBlockFrame(hDC, y, L"Таймер", L::TIMER_BOX_H);

    int cy = box.top + L::T_PAD;
    RECT btn = { ContentLeft(), cy, ContentLeft() + 26, cy + L::T_ROW };
    RECT field = { btn.right + 6, cy, ContentRight(), cy + L::T_ROW };

    DrawToggleChip(hDC, btn, L"C", m_timerRunning, SO_TIMER_TOGGLE, "TIMER_TOGGLE",
        "ЛКМ - пуск/стоп таймера, ПКМ - сброс");

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
    RECT box = DrawBlockFrame(hDC, y, L"Пользователь", L::USER_BOX_H);

    std::wstring designation, role, user;
    GetUserInfo(designation, role, user);

    int cy = box.top + L::U_TOP;
    RECT designR = { ContentLeft(), cy, ContentLeft() + 47, cy + L::U_ROW };
    RECT roleR = { designR.right + 6, cy, ContentRight(), cy + L::U_ROW };
    DrawFittedField(hDC, designR, designation);
    DrawFittedField(hDC, roleR, role);
    cy += L::U_ROW + L::U_GAP;

    RECT userR = { ContentLeft(), cy, ContentRight(), cy + L::U_ROW };
    DrawFittedField(hDC, userR, user);

    return box.bottom;
}

int CGalaxyATMSystemRadarScreen::DrawBlockVectors(HDC hDC, int y)
{
    RECT box = DrawBlockFrame(hDC, y, L"Векторы", L::VECTORS_BOX_H);

    int cy = box.top + L::V_TOP;
    int x = box.left + 9;   // this row is indented further than the checkboxes below it

    RECT distBtn = { x, cy, x + 25, cy + L::V_ROW };
    RECT distField = { distBtn.right + 6, cy, distBtn.right + 6 + 63, cy + L::V_ROW };
    RECT timeBtn = { distField.right + 9, cy, distField.right + 9 + 21, cy + L::V_ROW };
    RECT timeField = { timeBtn.right + 6, cy, timeBtn.right + 6 + 63, cy + L::V_ROW };

    m_vecDistFieldRect = distField;
    m_vecTimeFieldRect = timeField;

    DrawToggleChip(hDC, distBtn, L"Д", m_vecDistEnabled, SO_VEC_DIST_TOGGLE, "VEC_DIST_TOGGLE",
        "Вектор по дальности (км) - вместо вектора по времени");
    wchar_t distText[16];
    swprintf_s(distText, L"%d", m_vecDistKm);
    DrawDropdownField(hDC, distField, distText, SO_VEC_DIST_FIELD, "VEC_DIST_FIELD", "Выбрать длину вектора, км");

    DrawToggleChip(hDC, timeBtn, L"Э", m_vecTimeEnabled, SO_VEC_TIME_TOGGLE, "VEC_TIME_TOGGLE",
        "Вектор по времени (мин) - вместо вектора по дальности");
    wchar_t timeText[16];
    swprintf_s(timeText, L"%d", m_vecTimeMin);
    DrawDropdownField(hDC, timeField, timeText, SO_VEC_TIME_FIELD, "VEC_TIME_FIELD", "Выбрать время вектора, мин");

    cy += L::V_ROW + L::V_GAP1;
    DrawCheckRow(hDC, cy, box.left + 8, L"Вектор по плану", m_vecByPlan,
        SO_VEC_BY_PLAN_CHK, "VEC_BY_PLAN", "Вектор по плану");

    cy += L::V_CHK + L::V_GAP2;
    DrawCheckRow(hDC, cy, box.left + 8, L"Расчётный эшелон", m_vecShowLevel,
        SO_VEC_LEVEL_CHK, "VEC_LEVEL", "Расчётный эшелон");

    return box.bottom;
}

// ФС - "Р-р шрифта:" over a sunken list of the track-label layout options:
// a two- or three-line label (one or the other) plus an independent speed line.
int CGalaxyATMSystemRadarScreen::DrawBlockOs(HDC hDC, int y)
{
    RECT box = DrawBlockFrame(hDC, y, L"ФС", L::OS_BOX_H);

    RECT label = { box.left + 8, box.top + L::O_TOP, ContentRight(), box.top + L::O_TOP + L::O_LABEL };
    Theme::DrawLine(hDC, label, L"Р-р шрифта:", m_fonts.Body, Theme::Text, DT_LEFT | DT_VCENTER);

    RECT list = { box.left + 5, label.bottom + L::O_GAP, box.right - 4,
                  label.bottom + L::O_GAP + L::O_LIST_H };
    Theme::OutlineBox(hDC, list, Theme::InsetFill, Theme::Border);

    const int x = list.left + 5;
    int row = list.top + L::OS_ROW0;
    DrawCheckRow(hDC, row, x, L"2 строчный", m_osLines == 2,
        SO_OS_TWO_LINE, "OS_2LINE", "Двухстрочный формуляр");
    row += L::OS_PITCH;
    DrawCheckRow(hDC, row, x, L"скорость", m_osSpeed,
        SO_OS_SPEED, "OS_SPEED", "Показывать скорость в формуляре");
    row += L::OS_PITCH;
    DrawCheckRow(hDC, row, x, L"3 строчный", m_osLines == 3,
        SO_OS_THREE_LINE, "OS_3LINE", "Трёхстрочный формуляр");

    return box.bottom;
}

int CGalaxyATMSystemRadarScreen::DrawBlockAltFilter(HDC hDC, int y)
{
    RECT box = DrawBlockFrame(hDC, y, L"Фильтр высоты", L::ALTFILTER_BOX_H);

    wchar_t fromText[8], toText[8];
    swprintf_s(fromText, L"FL%03d", Plugin()->AltFilterFromFL());
    swprintf_s(toText, L"FL%03d", Plugin()->AltFilterToFL());

    // "Макс:"/"Мин :" sit hard left; their values are a pair of ordinary
    // outlined plates lined up towards the right-hand side of the box.
    const int valLeft = box.left + 106, valRight = valLeft + 60;

    int cy = box.top + L::F_TOP;
    RECT maxLbl = { box.left + 8, cy, valLeft, cy + L::F_ROW };
    RECT maxVal = { valLeft, cy, valRight, cy + L::F_ROW };
    Theme::DrawLine(hDC, maxLbl, L"Макс:", m_fonts.Body, Theme::Text, DT_LEFT | DT_VCENTER);
    DrawOutlinedField(hDC, maxVal, toText, m_fonts.Body);
    AddScreenObject(SO_ALTFILTER_TO, "ALTFILTER_TO", maxVal, false, "Верхняя граница фильтра высоты");
    cy += L::F_ROW + L::F_GAP1;

    RECT minLbl = { box.left + 8, cy, valLeft, cy + L::F_ROW };
    RECT minVal = { valLeft, cy, valRight, cy + L::F_ROW };
    Theme::DrawLine(hDC, minLbl, L"Мин :", m_fonts.Body, Theme::Text, DT_LEFT | DT_VCENTER);
    DrawOutlinedField(hDC, minVal, fromText, m_fonts.Body);
    AddScreenObject(SO_ALTFILTER_FROM, "ALTFILTER_FROM", minVal, false, "Нижняя граница фильтра высоты");
    cy += L::F_ROW + L::F_GAP2;

    DrawCheckRow(hDC, cy, box.left + 39, L"Использовать", Plugin()->AltFilterEnabled(),
        SO_ALTFILTER_USE_CHK, "ALTFILTER_USE", "Использовать фильтр высоты");

    return box.bottom;
}

// The captionless block under Фильтр высоты. Top row: the "ВВ1" secondary
// source with its ВСЕ / БП selectors, an entry field and a spare checkbox,
// plus that source's video-gain slider down the left-hand edge. Below it, two
// read-only readouts driven by the live radar picture.
int CGalaxyATMSystemRadarScreen::DrawBlockCodes(HDC hDC, int y)
{
    RECT box = DrawBoxOnly(hDC, y, L::CODES_BOX_H);

    // What the slider beside it actually does, in the units the panel is set
    // to: how far it is across the displayed area on the ground, edge to edge.
    // The zoom is what this reads - a wheel zoom or a preset moves it too,
    // because it is measured from the display area rather than from the slider.
    double widthNM = DisplayWidthNM();
    wchar_t scaleText[24];
    if (Plugin()->UnitDist() == DistUnit::NM)
        swprintf_s(scaleText, L"%d NM", (int)lround(widthNM));
    else
        swprintf_s(scaleText, L"%d км", (int)lround(widthNM * 1.852));

    RECT vv = { box.left + 7, box.top + L::C_VV_TOP, box.left + 82, box.top + L::C_VV_TOP + L::C_VV_H };
    Theme::DrawLine(hDC, vv, scaleText, m_fonts.Body, Theme::Text, DT_LEFT | DT_VCENTER);
    AddScreenObject(SO_VV_SCALE, "VV_SCALE", vv, false,
        "Масштаб: ширина отображаемой зоны от края до края");

    // "ВСЕ" straddles the top of the block, half over the ВВ1 label's row.
    RECT all = { box.left + 85, box.top + L::C_ALL_TOP, box.left + 125, box.top + L::C_ALL_TOP + L::C_ROW_H };
    Theme::OutlineBox(hDC, all, m_codeAll ? Theme::Active : Theme::ButtonLight, Theme::Border);
    Theme::DrawLine(hDC, all, L"ВСЕ", m_fonts.Small, Theme::Text, DT_CENTER | DT_VCENTER);
    AddScreenObject(SO_CODE_ALL, "CODE_ALL", all, false, "Пропускать все коды");

    RECT bp = { box.left + 32, box.top + L::C_BP_TOP, box.left + 65, box.top + L::C_BP_TOP + L::C_ROW_H };
    Theme::OutlineBox(hDC, bp, m_codeBp ? Theme::Active : Theme::ButtonMid, Theme::Border);
    Theme::DrawLine(hDC, bp, L"БП", m_fonts.Small, Theme::Text, DT_CENTER | DT_VCENTER);
    AddScreenObject(SO_CODE_BP, "CODE_BP", bp, false, "Без привязки");

    RECT filter = { box.left + 71, box.top + L::C_FLT_TOP, box.left + 183, box.top + L::C_FLT_TOP + L::C_FLT_H };
    Theme::OutlineBox(hDC, filter, Theme::InsetFill, Theme::Border);
    if (!m_codeFilter.empty())
    {
        RECT inner = { filter.left + 3, filter.top, filter.right - 3, filter.bottom };
        Theme::DrawLine(hDC, inner, m_codeFilter, m_fonts.Small, Theme::Text,
            DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
    }
    AddScreenObject(SO_CODE_FILTER, "CODE_FILTER", filter, false, "Коды источника ВВ1");

    RECT extra = { box.left + 189, box.top + L::C_EXTRA_TOP, box.left + 201, box.top + L::C_EXTRA_TOP + L::C_EXTRA_H };
    Theme::OutlineBox(hDC, extra, m_codeExtra ? Theme::Active : Theme::Background, Theme::Border);
    AddScreenObject(SO_CODE_EXTRA, "CODE_EXTRA", extra, false, "Источник ВВ1 включён");

    // Radar-scale slider: a dark track whose lower part fills cyan up to the
    // thumb. Value grows upwards - pushed up the radar closes in, pulled down
    // it opens out. Unless it is being dragged the thumb is re-derived from the
    // display area every frame, so zooming by any other means moves it too.
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
        "Масштаб радара: вверх - приблизить, вниз - отдалить");

    // Two live readouts. Both stay empty when nothing matches, exactly as the
    // reference shows them - so anything in either is an alarm, centred in its
    // plate and coloured to be caught out of the corner of an eye rather than
    // read: red for a distress squawk, amber for a code being carried twice.
    RECT distressCap = { box.left, box.top + L::C_DISTRESS_CAP, box.right, box.top + L::C_DISTRESS_CAP + L::C_CAP_H };
    Theme::DrawLine(hDC, distressCap, L"Коды бедствия", m_fonts.Body, Theme::Text, DT_CENTER | DT_VCENTER);
    RECT distress = { box.left + 24, box.top + L::C_DISTRESS_FIELD, box.left + 201, box.top + L::C_DISTRESS_FIELD + L::C_FIELD_H };
    Theme::OutlineBox(hDC, distress, Theme::Background, Theme::Border);
    Theme::DrawLine(hDC, RECT{ distress.left + 4, distress.top, distress.right - 4, distress.bottom },
        GetDistressCodes(), m_fonts.Small, Theme::DistressText,
        DT_CENTER | DT_VCENTER | DT_END_ELLIPSIS);

    RECT dupCap = { box.left, box.top + L::C_DUP_CAP, box.right, box.top + L::C_DUP_CAP + L::C_CAP_H };
    Theme::DrawLine(hDC, dupCap, L"Двойной код", m_fonts.Body, Theme::Text, DT_CENTER | DT_VCENTER);
    RECT dup = { box.left + 24, box.top + L::C_DUP_FIELD, box.left + 201, box.top + L::C_DUP_FIELD + L::C_FIELD_H };
    Theme::OutlineBox(hDC, dup, Theme::Background, Theme::Border);
    Theme::DrawLine(hDC, RECT{ dup.left + 4, dup.top, dup.right - 4, dup.bottom },
        GetDuplicateCodes(), m_fonts.Small, Theme::DuplicateText,
        DT_CENTER | DT_VCENTER | DT_END_ELLIPSIS);

    return box.bottom;
}

int CGalaxyATMSystemRadarScreen::DrawBlockUnits(HDC hDC, int y)
{
    RECT box = DrawBlockFrame(hDC, y, L"Ед. изм.", L::UNITS_BOX_H);

    // The level row is three columns of its own; the three rows under it are a
    // plain two-column grid whose right column starts further left.
    const int col1  = box.left + 5;
    const int colM  = box.left + 70;
    const int colFM = box.left + 140;
    const int col2  = box.left + 119;

    int cy = box.top + L::E_TOP;
    auto row = [&]() { int r = cy; cy += L::E_PITCH; return r; };

    int r1 = row();
    DrawRadioRow(hDC, r1, col1,  colM,        L"FL",   Plugin()->UnitAlt() == AltUnit::FL,  SO_UNIT_ALT_FL,  "U_ALT_FL",  "Эшелон");
    DrawRadioRow(hDC, r1, colM,  colFM,       L"M",    Plugin()->UnitAlt() == AltUnit::M,   SO_UNIT_ALT_M,   "U_ALT_M",   "Метры");
    DrawRadioRow(hDC, r1, colFM, box.right,   L"FL+M", Plugin()->UnitAlt() == AltUnit::FLM, SO_UNIT_ALT_FLM, "U_ALT_FLM", "Эшелон и метры");

    int r2 = row();
    DrawRadioRow(hDC, r2, col1, col2,       L"Фут / м", Plugin()->UnitVs() == VsUnit::FtMin, SO_UNIT_VS_FTM, "U_VS_FTM", "Футы в минуту");
    DrawRadioRow(hDC, r2, col2, box.right,  L"М / С",   Plugin()->UnitVs() == VsUnit::MS,    SO_UNIT_VS_MS,  "U_VS_MS",  "Метры в секунду");

    int r3 = row();
    DrawRadioRow(hDC, r3, col1, col2,       L"Узлы",  Plugin()->UnitGs() == GsUnit::Knots, SO_UNIT_GS_KT,  "U_GS_KT",  "Узлы");
    DrawRadioRow(hDC, r3, col2, box.right,  L"Км / ч", Plugin()->UnitGs() == GsUnit::Kmh,  SO_UNIT_GS_KMH, "U_GS_KMH", "Километры в час");

    int r4 = row();
    DrawRadioRow(hDC, r4, col1, col2,       L"Мили", Plugin()->UnitDist() == DistUnit::NM, SO_UNIT_DIST_NM, "U_DIST_NM", "Морские мили");
    DrawRadioRow(hDC, r4, col2, box.right,  L"Км",   Plugin()->UnitDist() == DistUnit::Km, SO_UNIT_DIST_KM, "U_DIST_KM", "Километры");

    return box.bottom;
}

int CGalaxyATMSystemRadarScreen::DrawBlockAerodrome(HDC hDC, int y)
{
    const Config& cfg = Plugin()->GetConfig();

    RECT box = DrawBlockFrame(hDC, y, cfg.Airport(), L::AERODROME_BOX_H);

    // Two rows that line up as a column pair: ДАВЛ over Э/П in tags of one
    // size, and their two readouts starting at one and the same left edge with
    // the value itself left-aligned inside the plate - so the pressure and the
    // transition level read straight down the block. The tags are bare
    // outlines with the card showing through, not the near-black plates the
    // blocks above use.
    const int kTagLeft   = 5,  kTagRight  = 57;
    const int kValLeft   = 63;

    // Row 1: ДАВЛ | 760/1013
    int cy = box.top + L::A_TOP;
    RECT davlTag = { box.left + kTagLeft, cy, box.left + kTagRight, cy + L::A_ROW };
    RECT davlVal = { box.left + kValLeft, cy, box.left + 199, cy + L::A_ROW };
    Theme::DrawGhostControl(hDC, davlTag, L"ДАВЛ", m_fonts.Body);
    Theme::DrawValueField(hDC, davlVal, Plugin()->QnhMmHg() + L"/" + Plugin()->QnhHpa(),
        m_fonts.Body, true, DT_LEFT | DT_VCENTER);
    cy += L::A_ROW + L::A_GAP;

    // Row 2: Э/П | F050 | АТИС. The button opens the report and nothing else -
    // the letter has a window of its own out on the radar and no control here.
    RECT epTag = { box.left + kTagLeft, cy, box.left + kTagRight, cy + L::A_ROW };
    RECT epVal = { box.left + kValLeft, cy, box.left + 127, cy + L::A_ROW };
    RECT atisBtn = { box.left + 146, cy, box.left + 199, cy + L::A_ROW };
    Theme::DrawGhostControl(hDC, epTag, L"Э/П", m_fonts.Body);
    Theme::DrawValueField(hDC, epVal, Plugin()->TransitionLevel(),
        m_fonts.Body, true, DT_LEFT | DT_VCENTER);

    if (m_atisOpen)
        Theme::DrawValueField(hDC, atisBtn, L"АТИС", m_fonts.Body);
    else
        Theme::DrawGhostControl(hDC, atisBtn, L"АТИС", m_fonts.Body);
    AddScreenObject(SO_ATIS_BUTTON, "ATIS_BTN", atisBtn, false,
        "Открыть текст АТИС");

    return box.bottom;
}

// The АТИС window, drawn from the photograph of the real system: an olive
// card behind a light two-pixel frame, a grey title bar shading light-to-dark
// with a white caption and an X at its right end, the index line on the card
// itself, and the message on a mid-grey panel in white monospace with a
// black-troughed scrollbar down the panel's right-hand edge.
//
// The geometry is fixed rather than proportional - the window does not resize,
// and every offset below is the reference photograph's own measurement scaled
// to this size.
void CGalaxyATMSystemRadarScreen::DrawAtisWindow(HDC hDC)
{
    const int W = 370, H = 430;
    const int kFrame   = 2;    // the light edge round the window and round the panel
    const int kTitleH  = 21;
    const int kSide    = 14;   // panel inset from the window's sides
    const int kTrackW  = 18;   // scrollbar column
    const int kEndBtn  = 16;   // the square at each end of the scrollbar
    const int kButtonH = 21;
    const int kButtonW = 60;

    RECT ra = GetRadarArea();
    if (!m_atisPositioned)
    {
        // Opened under the index strip in the top left corner - the report
        // belongs to the strip that was clicked, and the strip is what says
        // where the АТИС lives on this screen. Off the strip's own rectangle
        // when there is one; ".atis" can have hidden it, and then the corner
        // is measured the same way the strip measures it.
        const bool haveStrip = (m_atisLetterArea.bottom > m_atisLetterArea.top);
        m_atisArea.left = haveStrip ? m_atisLetterArea.left : ra.left + 8;
        m_atisArea.top = haveStrip ? m_atisLetterArea.bottom + 6
                                   : PanelTop() + Plugin()->GetConfig().AtisTopOffset();
        m_atisPositioned = true;
    }

    // Same clamp as the panel: keep the window inside the radar area.
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

    // ---- The card and its light edge ---------------------------------------
    // The window's corners are rounded, so the two things that reach them -
    // the card itself and the title bar across the top - are drawn through a
    // clip of that same shape. Everything below the title bar sits well inside
    // the corners and needs no clipping of its own; the light edge is drawn
    // last, over the lot, so it comes out clean.
    HRGN winRgn = Theme::WinRegion(m_atisArea);
    SelectClipRgn(hDC, winRgn);

    Theme::FlatFill(hDC, m_atisArea, Theme::WinBody);

    // ---- Title bar ---------------------------------------------------------
    RECT title = { m_atisArea.left + kFrame, m_atisArea.top + kFrame,
                   m_atisArea.right - kFrame, m_atisArea.top + kFrame + kTitleH };
    Theme::VGradient(hDC, title, Theme::WinTitleTop, Theme::WinTitleBot);

    SelectClipRgn(hDC, NULL);
    DeleteObject(winRgn);

    AddScreenObject(SO_ATIS_HEADER, "ATIS_HEADER", title, true, "Перетащите окно АТИС");

    // The bar sits on a light strip running the full width of the card - the
    // one edge in the reference that is not part of a box.
    RECT titleEdge = { title.left, title.bottom, title.right, title.bottom + kFrame };
    Theme::FlatFill(hDC, titleEdge, Theme::WinFrame);

    Theme::DrawLine(hDC, title, L"ATIS message", m_fonts.WinTitle, Theme::WinTitleText,
        DT_CENTER | DT_VCENTER);

    // The close mark is drawn rather than typed: at this size no font gives the
    // thick, square X the reference has.
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
    AddScreenObject(SO_ATIS_CLOSE, "ATIS_CLOSE", close, false, "Закрыть");

    // ---- Index line, on the card itself ------------------------------------
    // Plain text on the card, exactly as the reference photograph has it - the
    // letter gets a window of its own (DrawAtisLetterWindow) rather than a
    // plate here.
    RECT index = { m_atisArea.left + kSide + 4, titleEdge.bottom + 14,
                   m_atisArea.right - kSide, titleEdge.bottom + 36 };
    Theme::DrawLine(hDC, index, L"Index:   " + Plugin()->AtisIndex(),
        m_fonts.MonoBig, Theme::Text, DT_LEFT | DT_VCENTER);

    // ---- OK, bottom right --------------------------------------------------
    RECT ok = { m_atisArea.right - kSide - 9 - kButtonW,
                m_atisArea.bottom - kFrame - 13 - kButtonH,
                m_atisArea.right - kSide - 9, m_atisArea.bottom - kFrame - 13 };
    Theme::FlatFill(hDC, ok, Theme::ButtonFace);
    Theme::FlatFrame(hDC, ok, kFrame, Theme::WinFrame);
    Theme::DrawLine(hDC, ok, L"OK", m_fonts.Body, Theme::ButtonText, DT_CENTER | DT_VCENTER);
    AddScreenObject(SO_ATIS_OK, "ATIS_OK", ok, false, "Закрыть");

    // ---- The message panel: text and scrollbar share one light frame -------
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

    // The live broadcast when the network is carrying one, otherwise both
    // languages as the config composed them.
    const std::wstring atisText = Plugin()->AtisMessage();

    // Measure the wrapped text so the scroll range and the thumb size are real
    // rather than guessed.
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
    scrolled.bottom = scrolled.top + totalH + 1; // clipped to textArea anyway

    oldFont = (HFONT)SelectObject(hDC, m_fonts.Mono);
    SetTextColor(hDC, Theme::PaperInk);
    DrawTextW(hDC, atisText.c_str(), -1, &scrolled, DT_LEFT | DT_TOP | DT_WORDBREAK);
    SelectObject(hDC, oldFont);

    SelectClipRgn(hDC, NULL);
    DeleteObject(clip);

    // ---- Scrollbar: black trough, light thumb, a black square at each end --
    Theme::FlatFill(hDC, track, Theme::ScrollTrough);

    RECT btnUp = { track.left, track.top, track.right, track.top + kEndBtn };
    RECT btnDn = { track.left, track.bottom - kEndBtn, track.right, track.bottom };
    AddScreenObject(SO_ATIS_LINE_UP, "ATIS_UP", btnUp, false, "Прокрутить вверх");
    AddScreenObject(SO_ATIS_LINE_DN, "ATIS_DN", btnDn, false, "Прокрутить вниз");

    // Both squares carry the mark the reference has - a small light square with
    // a dark centre - and a light rule divides each from the trough.
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
    AddScreenObject(SO_ATIS_SCROLLBAR, "ATIS_SCROLL", bar, true, "Прокрутка текста АТИС");

    // The window's own rounded edge, over everything inside it.
    Theme::WinBorder(hDC, m_atisArea, kFrame, Theme::WinFrame);

    RestoreDC(hDC, saved);
}

// The АТИС index on its own: a single read-only strip standing on the radar
// outside the panel, carrying the line the real system shows and nothing else -
// "INDEX ATIS: <letter>" on black inside a light frame, the letter lit in lime
// against the white label. It replaces the little olive card that used to hold
// the letter alone: no title bar, no "x" and no chrome to take up radar, since
// the strip itself is the readout. It is up from the start; ".atis" hides and
// shows it, and a left click on it opens and closes the full report under it.
void CGalaxyATMSystemRadarScreen::DrawAtisLetterWindow(HDC hDC)
{
    // Drawn as two runs rather than one string - the label and the letter
    // carry different colours - so both are measured separately and the strip
    // is sized to the pair.
    const std::wstring label  = L"INDEX ATIS: ";
    const std::wstring letter = Plugin()->AtisIndex();

    // Monospaced, so the strip keeps the same width whatever letter is on the
    // air. Sized to the text rather than fixed: a config carrying a longer
    // index still fits.
    const int kFrame = 2;     // the light edge, as thick as every other window's
    const int kPadX  = 7;
    const int kPadY  = 3;

    // The message font rather than the index one: the strip stands beside the
    // panel all session and only ever carries a single letter, so it is read at
    // a glance without being drawn at the size the report's own heading uses.
    HFONT font = m_fonts.Mono;

    SIZE szLabel  = Theme::MeasureText(hDC, font, label);
    SIZE szLetter = Theme::MeasureText(hDC, font, letter);
    const int W = szLabel.cx + szLetter.cx + 2 * (kFrame + kPadX);
    const int H = max(szLabel.cy, szLetter.cy) + 2 * (kFrame + kPadY);

    // The top left corner of the radar, where the real system carries it - the
    // opposite corner to the panel, which is docked right, so the two never
    // reach for the same pixels however wide the letter makes the strip.
    // Flush against the left edge of the screen, with no gap: the strip is
    // docked to it the way the panel is docked to the right one.
    //
    // Below the toolbar by the configured offset rather than by a margin:
    // TopSky lays its own menu bar across the top of the radar and the SDK
    // reports nothing about it, so the strip would sit on the menu at any
    // gap measured from the toolbar alone. See Config::AtisTopOffset.
    // There is nothing to drag and nothing to restore from the ASR.
    RECT ra = GetRadarArea();
    m_atisLetterArea.left = ra.left;
    m_atisLetterArea.top  = PanelTop() + Plugin()->GetConfig().AtisTopOffset();

    // Only if the display is narrower than the strip, which no real one is.
    if (m_atisLetterArea.left + W > ra.right)
        m_atisLetterArea.left = max(ra.left, ra.right - W);

    m_atisLetterArea.right  = m_atisLetterArea.left + W;
    m_atisLetterArea.bottom = m_atisLetterArea.top + H;

    int saved = SaveDC(hDC);
    SetBkMode(hDC, TRANSPARENT);

    // Square corners and a flat fill, not the rounded olive card: this one is
    // a readout on the radar rather than a window over it.
    Theme::FlatFill(hDC, m_atisLetterArea, Theme::ControlFill);
    Theme::FlatFrame(hDC, m_atisLetterArea, kFrame, Theme::WinFrame);

    // Label first, then the letter hard against it: the pair is laid out from
    // the left inside the padding, which is the same thing as centring it -
    // the strip was sized to exactly this text.
    int textLeft = m_atisLetterArea.left + kFrame + kPadX;
    RECT labelR = { textLeft, m_atisLetterArea.top, textLeft + szLabel.cx,
                    m_atisLetterArea.bottom };
    RECT letterR = { labelR.right, m_atisLetterArea.top, labelR.right + szLetter.cx,
                     m_atisLetterArea.bottom };
    Theme::DrawLine(hDC, labelR, label, font, Theme::Text,
        DT_LEFT | DT_VCENTER);
    Theme::DrawLine(hDC, letterR, letter, font, Theme::AtisIndexText,
        DT_LEFT | DT_VCENTER);

    // One object for the whole strip, and it does one thing: open and close the
    // report. Registered fixed rather than moveable - the strip is docked.
    AddScreenObject(SO_ATIS_LETTER_HEADER, "ATIS_L_HEADER", m_atisLetterArea, false,
        "ЛКМ - текст АТИС, .atis - скрыть");

    RestoreDC(hDC, saved);
}

// ---- "Список РЦ" -----------------------------------------------------------
// The sector list: one line per flight this sector is concerned with, in the
// twelve columns the real system prints. Everything in it is derived from the
// live flight plans on the frame it is drawn - the window holds no copy of the
// traffic, only how it is being looked at (sort order, filter, scroll).
//
// The window is "SPISKIRC.svg" to the pixel, at two fifths of its size. Every
// number in the drawing below is that export's own coordinate in its 2085x580
// artboard, put through Sv() rather than re-derived, so the layout can be
// checked straight against the file. The export itself is drawn far wider than
// any radar screen; two fifths of it is a window that stands on the picture
// rather than covering it, and the proportions are the file's throughout -
// change the fraction in Sv() and the whole window scales with it.
namespace
{
    inline int Sv(int svgUnits) { return (svgUnits * 2) / 5; }

    struct RcColumn { const wchar_t* title; int svgLeft; UINT align; };

    // The column rules of the export, in its own coordinates, and how each
    // column sets its heading and its values. The export left-aligns the lot;
    // the two coordination columns are centred instead, because they are the
    // wide ones - "BESNO/1516/F330" hung on their left edge leaves half the
    // column empty and the two of them read as one ragged block.
    const RcColumn kRcColumns[] = {
        { L"КФ",           18, DT_LEFT   },
        { L"Рейс",        143, DT_LEFT   },
        { L"ВРЛ",         327, DT_LEFT   },
        { L"S",           483, DT_LEFT   },
        { L"Тип",         529, DT_LEFT   },
        { L"W",           628, DT_LEFT   },
        { L"CFL",         672, DT_LEFT   },
        { L"Коорд. ПОД",  778, DT_CENTER },
        { L"Коорд. ПОД", 1185, DT_CENTER },
        { L"ВыхЭш",      1628, DT_LEFT   },
        { L"Крд",        1788, DT_LEFT   },
        { L"ПВО",        1935, DT_LEFT   },
    };
    const int kRcCols = (int)(sizeof(kRcColumns) / sizeof(kRcColumns[0]));
    const int kRcTableRightSvg = 2039;   // where the last column ends

    // What the chip at the bottom left sorts by. "ПОД" sorts on the entry
    // coordination time rather than on the whole cell - the point name comes
    // first in the text, and ordering by it would scatter the times.
    struct RcSortKey { const wchar_t* label; int cell; };
    const RcSortKey kRcSortKeys[] = {
        { L"ID",  1 },
        { L"ВРЛ", 2 },
        { L"CFL", 6 },
        { L"ПОД", 7 },
    };
    const int kRcSortKeyCount = (int)(sizeof(kRcSortKeys) / sizeof(kRcSortKeys[0]));

    // The export draws two panes of fixed height: the upper one carries the
    // heading row and two lines under it, the lower one three lines. Each has
    // a scrollbar of its own, which is how a pane of two rows shows a sector's
    // worth of traffic.
    const int kRcRowsTop = 2, kRcRowsBottom = 3;

    // The text a row is ordered by. For the coordination column that is the
    // time between the two slashes; for everything else the cell itself.
    std::wstring RcSortText(const SectorListRow& r, int cell)
    {
        const std::wstring& v = r.cells[cell];
        if (cell != 7)
            return v;
        size_t a = v.find(L'/');
        if (a == std::wstring::npos)
            return v;
        size_t b = v.find(L'/', a + 1);
        return v.substr(a + 1, (b == std::wstring::npos) ? std::wstring::npos : b - a - 1);
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
    GetSystemTime(&st);   // already UTC, like everything else on the panel
    const int nowMin = st.wHour * 60 + st.wMinute;

    // A coordination time is given as minutes from now; a negative one means
    // the flight is not crossing that boundary at all, and prints as dashes
    // rather than as a time in the past.
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

    const std::wstring filter = RcUpper(m_rcFilter);

    for (CFlightPlan fp = GetPlugIn()->FlightPlanSelectFirst(); fp.IsValid();
         fp = GetPlugIn()->FlightPlanSelectNext(fp))
    {
        // The list is this sector's, not the world's: a flight nobody here has
        // been told about, and one EuroScope has already written off, are both
        // none of this position's business.
        int state = fp.GetState();
        if (state == FLIGHT_PLAN_STATE_NON_CONCERNED || state == FLIGHT_PLAN_STATE_REDUNDANT)
            continue;

        std::wstring callsign = Widen(fp.GetCallsign());
        if (!filter.empty() && RcUpper(callsign).find(filter) == std::wstring::npos)
            continue;

        CFlightPlanData fpd = fp.GetFlightPlanData();
        CFlightPlanControllerAssignedData cad = fp.GetControllerAssignedData();

        SectorListRow row;
        row.callsign = fp.GetCallsign();
        row.mine = fp.GetTrackingControllerIsMe();
        row.crdState = fp.GetCoordinatedNextControllerState();

        // КФ - who has the flight. Its own controller while someone is tracking
        // it, otherwise whoever it is on its way to; blank when it is neither.
        std::wstring who = Widen(fp.GetTrackingControllerId());
        if (who.empty())
            who = Widen(fp.GetHandoffTargetControllerId());
        row.cells[0] = who;

        row.cells[1] = callsign;

        // ВРЛ - the code the aircraft is meant to be squawking, and "S" beside
        // it as the flag for the one thing worth noticing about a code: that
        // the transponder is not actually showing it.
        std::wstring assigned = Widen(cad.GetSquawk());
        std::wstring actual;
        CRadarTarget rt = fp.GetCorrelatedRadarTarget();
        if (rt.IsValid())
            actual = Widen(rt.GetPosition().GetSquawk());
        if (assigned.empty())
            assigned = actual;
        row.cells[2] = assigned;
        row.cells[3] = (!assigned.empty() && !actual.empty() && assigned != actual) ? L"S" : L"";

        row.cells[4] = Widen(fpd.GetAircraftFPType());
        row.cells[5] = fpd.IsRvsm() ? L"R" : L"";

        int cleared = fp.GetClearedAltitude();
        row.cells[6] = level(cleared > 0 ? cleared : fp.GetFinalAltitude());

        // The two coordination cells: point / time / level, each part dashed
        // out on its own when it has not been agreed. An entry with no point
        // named still prints its leading slash, so the times stay in one
        // column however much of the coordination is missing.
        row.cells[7] = Widen(fp.GetEntryCoordinationPointName()) + L"/"
            + hhmm(fp.GetSectorEntryMinutes()) + L"/"
            + level(fp.GetEntryCoordinationAltitude());
        row.cells[8] = Widen(fp.GetExitCoordinationPointName()) + L"/"
            + hhmm(fp.GetSectorExitMinutes()) + L"/"
            + level(fp.GetExitCoordinationAltitude());

        int exitFt = fp.GetExitCoordinationAltitude();
        row.cells[9] = level(exitFt > 0 ? exitFt : fp.GetFinalAltitude());

        // Крд - whether the next controller has been coordinated with. The
        // word is the same either way; which of them it is, is the colour.
        row.cells[10] = (row.crdState != COORDINATION_STATE_NONE) ? L"ACT" : L"";

        // ПВО has no source in the network, so it is driven the way the
        // controllers themselves mark it - a note in the scratchpad.
        std::wstring pad = RcUpper(Widen(cad.GetScratchPadString()));
        row.cells[11] = (pad.find(L"ПВО") != std::wstring::npos
                      || pad.find(L"PVO") != std::wstring::npos) ? L"+" : L"";

        out.push_back(row);
    }

    const int cell = kRcSortKeys[m_rcSortKey].cell;
    const bool asc = m_rcSortAsc;
    std::stable_sort(out.begin(), out.end(),
        [cell, asc](const SectorListRow& a, const SectorListRow& b)
        {
            std::wstring ka = RcSortText(a, cell), kb = RcSortText(b, cell);
            return asc ? (ka < kb) : (kb < ka);
        });
}

void CGalaxyATMSystemRadarScreen::DrawSectorListWindow(HDC hDC)
{
    std::vector<SectorListRow> all;
    BuildSectorList(all);

    // The two panes of the export are the two halves of the sector's traffic:
    // above, the flights that are not mine to work yet; below, the ones I am
    // tracking. A row's ground says the same thing - lavender for someone
    // else's, orange for my own - so a pane is read without its heading.
    std::vector<const SectorListRow*> other, mine;
    for (const SectorListRow& r : all)
        (r.mine ? mine : other).push_back(&r);

    // The table's values are printed in EuroScope's own tag font (m_esFont,
    // taken off the DC at the top of OnRefresh), so a line in the list reads as
    // the same writing as the tag it belongs to out on the radar. Only the
    // column headings keep the window's own face. The monospace is the
    // fallback for a frame where EuroScope left a stock font on the DC.
    HFONT rowFont = m_esFont ? m_esFont : m_fonts.Mono;

    auto clampScroll = [](int& scroll, size_t count, int visible)
    {
        scroll = max(0, min(scroll, (int)count - visible));
    };
    clampScroll(m_rcScroll, other.size(), kRcRowsTop);
    clampScroll(m_rcScrollMine, mine.size(), kRcRowsBottom);

    const int W = Sv(2085), H = Sv(580);

    RECT ra = GetRadarArea();
    if (!m_rcPositioned)
    {
        // Centred over the radar rather than tucked beside the panel: at this
        // width there is no edge it would sit against comfortably.
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

    // Everything in the window is placed off these two, in the export's own
    // coordinates: X(1185) is the pixel its 1185 landed on.
    const int ox = m_rcArea.left, oy = m_rcArea.top;
    auto X = [ox](int svg) { return ox + Sv(svg); };
    auto Y = [oy](int svg) { return oy + Sv(svg); };

    // Where a column's text goes inside its rules: the export's own indent on
    // a left-aligned column, an even margin on a centred one.
    auto textCell = [&](int col, int top, int bottom) -> RECT
    {
        int left  = X(kRcColumns[col].svgLeft);
        int right = X((col + 1 < kRcCols) ? kRcColumns[col + 1].svgLeft : kRcTableRightSvg);
        int pad = (kRcColumns[col].align == DT_CENTER) ? Sv(8) : Sv(16);
        RECT r = { left + pad, top, right - Sv(8), bottom };
        return r;
    };

    int saved = SaveDC(hDC);
    SetBkMode(hDC, TRANSPARENT);

    // ---- The card: white, with only its top corners rounded ---------------
    {
        const int r = Sv(20) * 2;   // RoundRect takes the corner ellipse's size
        HRGN top = CreateRoundRectRgn(m_rcArea.left, m_rcArea.top,
            m_rcArea.right + 1, m_rcArea.top + H / 2 + r, r, r);
        HRGN bottom = CreateRectRgn(m_rcArea.left, m_rcArea.top + H / 2,
            m_rcArea.right, m_rcArea.bottom);
        CombineRgn(top, top, bottom, RGN_OR);
        HBRUSH br = CreateSolidBrush(Theme::ListCard);
        FillRgn(hDC, top, br);
        DeleteObject(br);
        DeleteObject(bottom);
        DeleteObject(top);
    }

    // The dark body inset in it, and the title strip left above.
    RECT body = { X(5), Y(53), X(2080), Y(575) };
    Theme::FlatFill(hDC, body, Theme::Background);

    RECT title = { m_rcArea.left, m_rcArea.top, m_rcArea.right, Y(53) };
    Theme::DrawLine(hDC, title, L"Список РЦ", m_fonts.WinTitleSmall, Theme::ListTitleText,
        DT_CENTER | DT_VCENTER);
    AddScreenObject(SO_RC_HEADER, "RC_HEADER", title, true, "Перетащите список РЦ");

    // The export has no close mark on the strip; the window still needs one,
    // so it is drawn in the same grey as the caption beside it.
    RECT close = { m_rcArea.right - Sv(70), title.top + Sv(12),
                   m_rcArea.right - Sv(30), title.bottom - Sv(12) };
    {
        int cx = (close.left + close.right) / 2, cy = (close.top + close.bottom) / 2;
        const int arm = 4;
        HPEN pen = CreatePen(PS_SOLID, 2, Theme::ListTitleText);
        HPEN old = (HPEN)SelectObject(hDC, pen);
        MoveToEx(hDC, cx - arm, cy - arm, NULL);
        LineTo(hDC, cx + arm + 1, cy + arm + 1);
        MoveToEx(hDC, cx + arm, cy - arm, NULL);
        LineTo(hDC, cx - arm - 1, cy + arm + 1);
        SelectObject(hDC, old);
        DeleteObject(pen);
    }
    AddScreenObject(SO_RC_CLOSE, "RC_CLOSE", close, false, "Закрыть");

    HPEN whitePen = CreatePen(PS_SOLID, 1, Theme::ListRule);
    HPEN darkPen  = CreatePen(PS_SOLID, 1, Theme::ListRowRule);
    HPEN oldPen   = (HPEN)SelectObject(hDC, whitePen);

    auto hline = [&](HPEN pen, int x1, int x2, int y)
    {
        SelectObject(hDC, pen);
        MoveToEx(hDC, x1, y, NULL);
        LineTo(hDC, x2, y);
    };
    auto vline = [&](HPEN pen, int x, int y1, int y2)
    {
        SelectObject(hDC, pen);
        MoveToEx(hDC, x, y1, NULL);
        LineTo(hDC, x, y2);
    };

    // ---- One pane: its black box, its rows, and its scrollbar -------------
    // svgTop/svgBottom are the pane box; svgRowsTop the first row's top edge -
    // the upper pane starts its rows under the heading row, the lower one at
    // the top of the box. The scrollbar's own three parts are the export's
    // too: a square button at each end and a grey thumb between them.
    auto drawPane = [&](int svgTop, int svgBottom, int svgRowsTop, int svgRowsBottom,
                        int visibleRows, const std::vector<const SectorListRow*>& rows,
                        int scroll, int svgBarUpLine, int svgBarDownLine,
                        int svgUpSquare, int svgDownSquare, const char* paneId)
    {
        RECT box = { X(11), Y(svgTop), X(2074), Y(svgBottom) };
        Theme::FlatFill(hDC, box, Theme::ListPaneFill);
        Theme::FlatFrame(hDC, box, 1, Theme::ListRule);

        const int rowsTop = Y(svgRowsTop), rowsBottom = Y(svgRowsBottom);
        const int rowsLeft = X(17), rowsRight = X(2040);
        const int span = rowsBottom - rowsTop;

        for (int i = 0; i < visibleRows; i++)
        {
            int top    = rowsTop + (span * i) / visibleRows;
            int bottom = rowsTop + (span * (i + 1)) / visibleRows;

            int index = scroll + i;
            if (index >= (int)rows.size())
                break;
            const SectorListRow& row = *rows[index];

            RECT band = { rowsLeft, top, rowsRight, bottom };
            Theme::FlatFill(hDC, band, row.mine ? Theme::ListRowMine : Theme::ListRowOther);
            if (i > 0)
                hline(darkPen, rowsLeft, rowsRight, top);

            for (int c = 0; c < kRcCols; c++)
            {
                COLORREF ink = Theme::ListText;
                if (c == 1)
                {
                    ink = row.mine ? Theme::ListCsMine : Theme::ListCsOther;
                }
                else if (c == 10 && !row.cells[c].empty())
                {
                    ink = (row.crdState == COORDINATION_STATE_ACCEPTED
                        || row.crdState == COORDINATION_STATE_MANUAL_ACCEPTED)
                        ? Theme::ListCrdOk : Theme::ListCrdReq;
                }

                Theme::DrawLine(hDC, textCell(c, top, bottom), row.cells[c], rowFont, ink,
                    kRcColumns[c].align | DT_VCENTER | DT_END_ELLIPSIS);
            }

            // The whole row answers a click by selecting the aircraft, which is
            // what makes the list a way into the traffic rather than a readout.
            AddScreenObject(SO_RC_ROW, row.callsign.c_str(), band, false, "Выбрать борт");
        }

        // ---- Scrollbar ----
        // barRight is the pane's own right edge, and the frame drawn round the
        // pane already owns that last pixel column - everything in the strip
        // therefore stops one short of it, or the grey thumb paints over the
        // white border.
        const int barLeft = X(2048), barRight = X(2074) - 1;
        vline(whitePen, barLeft, Y(svgTop), Y(svgBottom));
        hline(whitePen, barLeft + 1, barRight, Y(svgBarUpLine));
        hline(whitePen, barLeft + 1, barRight, Y(svgBarDownLine));

        // The thumb is as tall a share of the track as the pane is of the list,
        // sitting where the pane currently is; a list that fits fills it.
        RECT track = { barLeft + 1, Y(svgBarUpLine) + 1, barRight, Y(svgBarDownLine) };
        int trackH = track.bottom - track.top;
        int total = max((int)rows.size(), visibleRows);
        int thumbH = max(Sv(20), (trackH * visibleRows) / total);
        int thumbTop = track.top + (total > visibleRows
            ? ((trackH - thumbH) * scroll) / (total - visibleRows) : 0);
        RECT thumb = { track.left, thumbTop, track.right, thumbTop + thumbH };
        Theme::FlatFill(hDC, thumb, Theme::ListThumb);

        // The two end buttons: a small hollow square each, centred in the strip.
        auto square = [&](int svgY, int objType, const char* tip)
        {
            RECT s = { X(2057), Y(svgY), X(2067), Y(svgY + 10) };
            Theme::FlatFrame(hDC, s, 1, Theme::ListRule);
            RECT hit = { barLeft, s.top - Sv(20), barRight, s.bottom + Sv(20) };
            AddScreenObject(objType, paneId, hit, false, tip);
        };
        square(svgUpSquare, SO_RC_UP, "На строку вверх");
        square(svgDownSquare, SO_RC_DOWN, "На строку вниз");
    };

    // ---- Upper pane: the heading row, then two lines ----------------------
    RECT head = { X(18), Y(71), X(2039), Y(144) };
    drawPane(64, 282, 144, 277, kRcRowsTop, other, m_rcScroll, 120, 223, 87, 248, "0");

    Theme::FlatFill(hDC, head, Theme::ListHeadFill);

    // Ruled on three sides only: a white line under the titles would sit a
    // pixel above the first row and read as a second border rather than as the
    // foot of the heading. The column rules below still run its full height.
    hline(whitePen, head.left, head.right, head.top);
    vline(whitePen, head.left, head.top, head.bottom);
    vline(whitePen, head.right - 1, head.top, head.bottom);
    for (int c = 0; c < kRcCols; c++)
    {
        if (c > 0)
            vline(whitePen, X(kRcColumns[c].svgLeft), head.top, head.bottom);
        Theme::DrawLine(hDC, textCell(c, head.top, head.bottom), kRcColumns[c].title,
            m_fonts.List, Theme::ListHeadText,
            kRcColumns[c].align | DT_VCENTER | DT_END_ELLIPSIS);
    }

    // ---- Lower pane: three lines, no heading ------------------------------
    drawPane(289, 507, 295, 502, kRcRowsBottom, mine, m_rcScrollMine, 345, 448, 312, 473, "1");

    // ---- Bottom bar --------------------------------------------------------
    // Left: the sort chip - the field it is sorted by, the arrow saying which
    // way round, and the export's own solid triangle at its right end. Middle:
    // the callsign filter. Right: the plate that empties it.
    RECT sort = { X(11), Y(527), X(200), Y(569) };
    Theme::FlatFrame(hDC, sort, 1, Theme::ListRule);

    RECT sortLabel = { sort.left + Sv(30), sort.top, X(96), sort.bottom };
    Theme::DrawLine(hDC, sortLabel, kRcSortKeys[m_rcSortKey].label, m_fonts.Small,
        Theme::ListRule, DT_LEFT | DT_VCENTER);

    // The direction arrow, drawn as the export has it: a stem on a base bar,
    // with the head at whichever end the ordering runs to.
    {
        SelectObject(hDC, whitePen);
        int cx = X(105), top = Y(536), bottom = Y(562), half = Sv(8);
        int headY = m_rcSortAsc ? top : bottom;
        int baseY = m_rcSortAsc ? bottom : top;
        int chevron = m_rcSortAsc ? top + Sv(9) : bottom - Sv(9);
        MoveToEx(hDC, cx - half, baseY, NULL);
        LineTo(hDC, cx + half + 1, baseY);
        MoveToEx(hDC, cx, top, NULL);
        LineTo(hDC, cx, bottom);
        MoveToEx(hDC, cx - half, chevron, NULL);
        LineTo(hDC, cx, headY);
        LineTo(hDC, cx + half + 1, chevron);
    }
    RECT sortDir = { sort.left, sort.top, X(160), sort.bottom };
    AddScreenObject(SO_RC_SORT_DIR, "RC_SORT_DIR", sortDir, false, "Порядок сортировки");

    {
        POINT tri[3] = { { X(176), Y(551) }, { X(196), Y(551) }, { X(186), Y(562) } };
        HBRUSH br = CreateSolidBrush(Theme::ListRule);
        HBRUSH oldBr = (HBRUSH)SelectObject(hDC, br);
        SelectObject(hDC, whitePen);
        Polygon(hDC, tri, 3);
        SelectObject(hDC, oldBr);
        DeleteObject(br);
    }
    RECT sortField = { sortDir.right, sort.top, sort.right, sort.bottom };
    AddScreenObject(SO_RC_SORT, "RC_SORT", sortField, false, "Поле сортировки");

    RECT filter = { X(212), Y(527), X(2009), Y(569) };
    Theme::FlatFill(hDC, filter, Theme::ListPaneFill);
    Theme::FlatFrame(hDC, filter, 1, Theme::ListRule);
    RECT filterText = { filter.left + Sv(20), filter.top, filter.right - Sv(20), filter.bottom };
    Theme::DrawLine(hDC, filterText, m_rcFilter, rowFont, Theme::ListRule,
        DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
    AddScreenObject(SO_RC_FILTER, "RC_FILTER", filter, false, "Фильтр по позывному");

    RECT clear = { X(2021), Y(527), X(2074), Y(569) };
    Theme::FlatFrame(hDC, clear, 1, Theme::ListRule);
    vline(whitePen, (clear.left + clear.right) / 2, Y(536), Y(562));
    AddScreenObject(SO_RC_FILTER_CLR, "RC_FILTER_CLR", clear, false, "Очистить фильтр");

    SelectObject(hDC, oldPen);
    DeleteObject(whitePen);
    DeleteObject(darkPen);

    RestoreDC(hDC, saved);
}

// Grabs the thumb by its middle, so the point taken hold of stays under the
// cursor for the whole drag instead of jumping to the top of the thumb.
void CGalaxyATMSystemRadarScreen::ScrollAtisTo(POINT pt, RECT track)
{
    int usable = (track.bottom - track.top) - m_atisThumbH;
    if (usable <= 0 || m_atisScrollMax <= 0)
        return;

    int rel = pt.y - track.top - m_atisThumbH / 2;
    rel = max(0, min(usable, rel));
    m_atisScrollPx = (int)((__int64)rel * m_atisScrollMax / usable);
}

// The ВВ1 slider reads bottom-up: the cursor at the foot of the track is 0 %.
void CGalaxyATMSystemRadarScreen::SetVvGainFrom(POINT pt)
{
    int h = m_vvSliderRect.bottom - m_vvSliderRect.top;
    if (h <= 0)
        return;

    int rel = m_vvSliderRect.bottom - pt.y;
    m_vvGain = max(0, min(100, MulDiv(rel, 100, h)));
    ApplyZoomFromSlider();
}

// ---- ВВ1 slider <-> radar scale ---------------------------------------------
// The slider is the radar's zoom: pushed up the picture closes in, pulled down
// it opens out. Scale is geometric rather than linear, so a given travel on the
// slider is the same proportional zoom wherever it is on the track.
namespace
{
    const double kZoomMinSpanNM = 6.0;      // slider at 100 %
    const double kZoomMaxSpanNM = 900.0;    // slider at 0 %
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

// North-south extent of what is on screen. Latitude is used rather than the
// diagonal because a degree of it is 60 NM anywhere, with no cosine to carry.
double CGalaxyATMSystemRadarScreen::DisplaySpanNM()
{
    CPosition leftDown, rightUp;
    GetDisplayArea(&leftDown, &rightUp);
    return fabs(rightUp.m_Latitude - leftDown.m_Latitude) * 60.0;
}

// East-west, which is what "from one edge of the picture to the other" means
// on a screen wider than it is tall - so this, not the latitude span above, is
// the number the scale readout prints. A degree of longitude shortens with the
// cosine of the latitude, taken at the middle of the display area; the
// wrap-around case keeps a window straddling the antimeridian honest.
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

    // The whole rectangle is scaled about its own centre, which keeps both the
    // aspect ratio and whatever the controller had centred exactly as they were.
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

// Keeps the thumb honest when the zoom was changed by any other means - the
// mouse wheel, a preset, an ASR load. Skipped mid-drag so the slider does not
// fight the cursor.
void CGalaxyATMSystemRadarScreen::SyncSliderFromZoom()
{
    m_vvGain = SpanNMToGain(DisplaySpanNM());
}

// ---- Vectors drawn over radar targets ----------------------------------------
// This is БЛОК 3's actual effect on the radar picture, as opposed to the panel
// controls (DrawBlockVectors) that configure it.
//
// Two independent kinds of line, matching the reference screenshot of a real
// target (SYL487): a plain "vector by plan" that follows the flight plan's own
// predicted trajectory (climb/descent profile and turns included, via
// GetPositionPredictions - not just a straight bearing to the next waypoint),
// and a "track vector" that extrapolates the current track/ground speed in a
// straight line, drawn with an arrowhead and - when "Расчётный эшелон" is
// checked - a predicted-level label like "F282" with a trend arrow, exactly as
// seen in that screenshot.
COLORREF CGalaxyATMSystemRadarScreen::GetTagColorForFlightPlan(CFlightPlan fp)
{
    if (!fp.IsValid())
        return RGB(150, 150, 150); // untracked / uncorrelated

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
    const double R = 3440.065; // Earth radius, NM

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

    // Where the chevron sits. A climbing or descending aircraft gets it at the
    // point it is predicted to reach its cleared level, worked out from the
    // vertical speed against the ground speed the vector is already scaled by:
    // as the aircraft closes on that level the mark slides back down the vector
    // towards the target symbol, and reaching it puts the mark on the symbol.
    // A level not reached within the vector leaves the mark at the tip, where
    // the whole vector ends. Level flight itself draws no chevron at all (see
    // isLevel below) - there is no predicted level to point at.
    double levelFrac = 1.0;
    int verticalSpeed = rt.GetVerticalSpeed();   // ft/min
    CFlightPlan vecFp = rt.GetCorrelatedFlightPlan();
    if (vecFp.IsValid() && abs(verticalSpeed) > 100 && timeMinForLevel > 0.0)
    {
        int clearedFt = vecFp.GetClearedAltitude();
        if (clearedFt > 0)
        {
            // Compare like with like: a cleared flight level is a
            // standard-pressure altitude, a cleared altitude below the
            // transition level is a true one.
            bool clearedIsFL = clearedFt / 100 >= Plugin()->TransitionLevelFL();
            int currentFt = clearedIsFL ? pos.GetFlightLevel() : pos.GetPressureAltitude();

            // A negative time means the aircraft is moving away from the
            // level rather than towards it - nothing to mark, so the chevron
            // stays at the tip.
            double minutesToLevel = (double)(clearedFt - currentFt) / verticalSpeed;
            if (minutesToLevel > 0.0)
                levelFrac = min(1.0, minutesToLevel / timeMinForLevel);
        }
    }

    POINT pMark;
    pMark.x = p0.x + (int)lround((p1.x - p0.x) * levelFrac);
    pMark.y = p0.y + (int)lround((p1.y - p0.y) * levelFrac);

    // Open chevron ("galochka") instead of a filled arrowhead - two short
    // strokes angled back from the tip, not a solid triangle.
    const double arrowAngle = 28.0 * M_PI / 180.0;
    double headLength = min(Theme::VectorHeadLength, totalLen * levelFrac * 0.4);

    Gdiplus::PointF chevron[3] = {
        Gdiplus::PointF((Gdiplus::REAL)(pMark.x + headLength * cos(heading + M_PI - arrowAngle)),
                        (Gdiplus::REAL)(pMark.y + headLength * sin(heading + M_PI - arrowAngle))),
        Gdiplus::PointF((Gdiplus::REAL)pMark.x, (Gdiplus::REAL)pMark.y),
        Gdiplus::PointF((Gdiplus::REAL)(pMark.x + headLength * cos(heading + M_PI + arrowAngle)),
                        (Gdiplus::REAL)(pMark.y + headLength * sin(heading + M_PI + arrowAngle))),
    };

    // Level flight (the same +-100 fpm band the trend arrow below uses) has no
    // predicted-level point to mark, so the chevron is skipped rather than
    // drawn for free at the tip.
    bool isLevel = abs(verticalSpeed) <= 100;

    {
        VectorCanvas canvas(hDC, color);

        if (minuteTicks >= 2)
        {
            // One tick per minute with a gap after each of them. Every endpoint is
            // interpolated along the p0->p1 pixel line rather than converted from
            // its own geographic position: separate conversions rounded each tick
            // independently, so consecutive ticks ended up a pixel off each other
            // instead of lying on one straight line.
            double ux = (p1.x - p0.x) / totalLen, uy = (p1.y - p0.y) / totalLen;
            double segLen = totalLen / minuteTicks;
            double gap = min(Theme::VectorTickGap, segLen * 0.25);  // never swallow a short segment whole

            for (int i = 0; i < minuteTicks; i++)
            {
                double from = i * segLen;
                double to = (i + 1) * segLen - gap;
                canvas.Line(p0.x + ux * from, p0.y + uy * from, p0.x + ux * to, p0.y + uy * to);
            }
        }
        else
            canvas.Line(p0.x, p0.y, p1.x, p1.y);

        // The chevron's own weight (see Theme), never lighter than the vector
        // it caps - a lighter head under a heavier line reads as a fray at the
        // end of it rather than as an arrow. One polyline, so the tip is a join rather
        // than two line ends laid over each other.
        if (headLength > 1.0 && !isLevel)
        {
            canvas.pen.SetWidth(Theme::VectorHeadWidth);
            canvas.g.DrawLines(&canvas.pen, chevron, 3);
        }
    }

    if (m_vecShowLevel)
    {
        // The label belongs to the chevron, so it reads the level at the point
        // the chevron marks - the cleared level itself for a climb or descent,
        // and the level flown all the way along for level flight.
        double labelTimeMin = timeMinForLevel * levelFrac;
        int climbFt = (int)(verticalSpeed * labelTimeMin);

        // The F/A choice is made on the predicted level, so the label matches
        // where the aircraft will be rather than where it is now.
        bool belowTL = (pos.GetFlightLevel() + climbFt) / 100 < Plugin()->TransitionLevelFL();
        int currentAltFt = belowTL ? pos.GetPressureAltitude() : pos.GetFlightLevel();
        int predictedAltFt = currentAltFt + climbFt;
        if (predictedAltFt < 0)
            predictedAltFt = 0;

        // БЛОК 4's "на векторе-измерителе": the predicted-level label follows
        // whichever altitude unit is currently selected there.
        wchar_t trend = (verticalSpeed > 100) ? L'\x2191' : (verticalSpeed < -100) ? L'\x2193' : L' ';
        // "FL+M" prints both formats at once - far too wide to hang off a
        // vector - so the vector always shows a single one.
        AltUnit vecUnit = (Plugin()->UnitAlt() == AltUnit::M) ? AltUnit::M : AltUnit::FL;
        std::wstring label = Widen(FormatAltitudeUnit(predictedAltFt, vecUnit, belowTL).c_str()) + trend;

        // Drawn in EuroScope's own tag font (captured in OnRefresh) so it reads
        // as part of the formular beside it rather than as a second typeface.
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

    // It is fundamentally a time vector - one point per minute along the
    // flight plan's own predicted trajectory (climb/descent profile and turns
    // included, not a straight bearing) - so, like the track vector, it reads
    // as a tick per minute rather than one unbroken line.
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

    // No arrowhead/label - matches the reference screenshot, where the
    // plan-following vector is undecorated (only the track vector carries the
    // chevron and predicted-level text).
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
            continue; // airborne only, matches the ground-clutter filter used elsewhere

        // Фильтр высоты: outside the От/До band this target gets no vectors.
        if (!Plugin()->AltFilterPasses(pos.GetPressureAltitude()))
            continue;

        int groundSpeed = rt.GetGS();
        if (groundSpeed < 10)
            continue; // stationary - nothing meaningful to extrapolate

        CFlightPlan fp = rt.GetCorrelatedFlightPlan();
        COLORREF color = GetTagColorForFlightPlan(fp);

        // "Э" - fixed duration in minutes, ticked once per minute.
        if (m_vecTimeEnabled)
        {
            double lengthNM = (groundSpeed / 60.0) * m_vecTimeMin;
            DrawTrackVector(hDC, rt, lengthNM, (double)m_vecTimeMin, m_vecTimeMin, color);
        }

        // "Д" - fixed distance in km; not minute-based, so drawn as one
        // unbroken line. The level label still needs a time, so it's derived
        // from how long the aircraft takes to cover that distance at its
        // current ground speed.
        if (m_vecDistEnabled)
        {
            double lengthNM = m_vecDistKm / 1.852;
            double timeMin = (groundSpeed > 0) ? (lengthNM / (groundSpeed / 60.0)) : 0.0;
            DrawTrackVector(hDC, rt, lengthNM, timeMin, 0, color);
        }

        // "Вектор по плану" - it is itself a time vector along the flight
        // plan's own predicted trajectory, always driven by the "Э" minutes
        // value regardless of whether the Э track vector above is enabled.
        if (m_vecByPlan && fp.IsValid() && m_vecTimeMin > 0)
        {
            DrawPlanVector(hDC, fp, pos.GetPosition(), m_vecTimeMin, color);
        }
    }

    RestoreDC(hDC, saved);
}

// Wake turbulence category as arcs behind the target - one for a heavy, two
// for a super - centred on the reciprocal of its track, so they turn with the
// aircraft. The vector's colour and weight, and
// antialiased on the same GDI+ surface.
void CGalaxyATMSystemRadarScreen::DrawWakeArcs(HDC hDC)
{
    for (CRadarTarget rt = GetPlugIn()->RadarTargetSelectFirst(); rt.IsValid();
         rt = GetPlugIn()->RadarTargetSelectNext(rt))
    {
        CRadarTargetPositionData pos = rt.GetPosition();
        if (!pos.IsValid())
            continue;

        // The same targets the vectors are drawn for: airborne, and inside
        // the От/До band - a ramp full of parked heavies would otherwise carry
        // arcs round every stand.
        if (pos.GetPressureAltitude() < 700)
            continue;
        if (!Plugin()->AltFilterPasses(pos.GetPressureAltitude()))
            continue;

        // The category lives on the flight plan; an uncorrelated target has none.
        CFlightPlan fp = rt.GetCorrelatedFlightPlan();
        if (!fp.IsValid())
            continue;
        char wtc = fp.GetFlightPlanData().GetAircraftWtc();
        int arcs = (wtc == 'J') ? 2 : (wtc == 'H') ? 1 : 0;
        if (arcs == 0)
            continue;

        // Which way is "behind" on screen, and how many pixels a mile is, both
        // from one point well ahead on the track rather than from the heading
        // itself - so the arcs still sit behind the aircraft on a rotated
        // display - and far enough ahead that neither is lost to rounding to
        // whole pixels when zoomed right out.
        const double kAheadNM = 20.0;
        CPosition here = pos.GetPosition();
        POINT c = ConvertCoordFromPositionToPixel(here);
        POINT ahead = ConvertCoordFromPositionToPixel(
            CalculateDestinationPoint(here, rt.GetTrackHeading(), kAheadNM));
        double dx = ahead.x - c.x, dy = ahead.y - c.y;
        double aheadPx = sqrt(dx * dx + dy * dy);
        if (aheadPx < 1.0)
            continue;
        double pxPerNM = aheadPx / kAheadNM;

        // Only gently with the zoom, between a floor and a ceiling (see Theme):
        // clear of the symbol zoomed right out, and not ballooning at an
        // approach zoom.
        double zoom = pow(pxPerNM, Theme::WakeArcZoomPower);
        double dist = min(Theme::WakeArcDistMax, max(Theme::WakeArcDistMin, Theme::WakeArcDistScale * zoom));
        double step = min(Theme::WakeArcStepMax, max(Theme::WakeArcStepMin, Theme::WakeArcStepScale * zoom));

        // The arc's own circle is smaller than its distance, so its centre is
        // pulled back behind the target by the difference - the middle of the
        // arc still lands at `dist`. A super's second arc shares that centre.
        double arcR = dist * Theme::WakeArcSize;
        double back = (dist - arcR) / aheadPx;
        double cx = c.x - dx * back, cy = c.y - dy * back;

        // GDI+ angles run clockwise from +x, the same sense as atan2 on a
        // y-down screen, so the reciprocal is simply half a turn on.
        double behindDeg = atan2(dy, dx) * 180.0 / M_PI + 180.0;

        VectorCanvas canvas(hDC, GetTagColorForFlightPlan(fp));
        canvas.pen.SetWidth(Theme::WakeArcWidth);
        for (int i = 0; i < arcs; i++)
        {
            double r = arcR + i * step;
            canvas.g.DrawArc(&canvas.pen,
                (Gdiplus::REAL)(cx - r), (Gdiplus::REAL)(cy - r),
                (Gdiplus::REAL)(2.0 * r), (Gdiplus::REAL)(2.0 * r),
                (Gdiplus::REAL)(behindDeg - Theme::WakeArcSweep / 2.0),
                (Gdiplus::REAL)Theme::WakeArcSweep);
        }
    }
}

// ---- Ruler --------------------------------------------------------------------
// A plain distance/bearing/time measuring line, dragged out anywhere on the
// radar while ruler mode is on (".ruler" command). Matches the reference: a
// beige line with a short one-sided tick and a 3-line label stacked directly
// above it - bearing, "XX.X km(YY.Y)" distance in km with NM in brackets, and
// "MM:SS" time. An endpoint dropped near a radar target snaps to its callsign
// and is re-resolved from that target's live position every frame, so the
// ruler follows the aircraft instead of staying pinned to where it was drawn.
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
                fixed = pos.GetPosition(); // also serves as the fallback if the target later drops out
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

// ---- Side mouse button ------------------------------------------------------
// EuroScope hands a plug-in only BUTTON_LEFT / MIDDLE / RIGHT, so a thumb button
// never arrives as an event and has to be read off the keyboard state instead.
//
// A press toggles ruler mode on/off - the same thing ".ruler" does - rather
// than arming it only for as long as the button stays down. While the mode is
// on, every left-drag on the radar draws a new line and the radar cannot be
// panned by dragging (the drag capture claims the whole radar area); that is
// the deliberate trade-off of an explicit toggle, on or off by one click of
// either the button or the command, same as a light switch.
void CGalaxyATMSystemRadarScreen::PollRulerButton()
{
    // Cheap enough to share this tick: the сигмет window is open only while
    // the left button is held, and the release does not always come back as an
    // event (see CloseSigmetInfoIfButtonReleased).
    CloseSigmetInfoIfButtonReleased();

    // Pressing or letting go of Shift adds or drops the зоны' hit-boxes,
    // which can only happen in a frame (see OnRefresh).
    bool shift = ShiftHeldInEuroScope();
    if (shift != m_areaShiftDown)
    {
        m_areaShiftDown = shift;
        RequestRefresh();
    }

    if (m_rulerButton != 0)
    {
        // GetAsyncKeyState is machine-wide: a thumb-button click meant for the
        // browser in front must not reach in here and draw a line behind it.
        HWND fg = GetForegroundWindow();
        DWORD pid = 0;
        if (fg != NULL)
            GetWindowThreadProcessId(fg, &pid);
        if (pid != GetCurrentProcessId())
        {
            // Drop the latch too, so coming back to EuroScope with the button
            // still held does not fire the moment it is released elsewhere.
            m_rulerButtonDown = false;
            return;
        }

        bool down = (GetAsyncKeyState(m_rulerButton) & 0x8000) != 0;
        if (down && !m_rulerButtonDown)
            m_rulerPressPending = true;   // acted on in OnRefresh
        m_rulerButtonDown = down;
    }

    // A pending press needs a frame to be acted on; an armed ruler needs one
    // every tick to keep its crosshair under the cursor, and a line still
    // being placed to keep its free end there.
    if (m_rulerPressPending || m_rulerArmed || m_rulerPlacing)
        RequestRefresh();
}

// The side button is read straight off the keyboard state rather than
// delivered as an event, so the cursor has to be found the same way: Windows
// reports it in screen coordinates, and EuroScope talks to a plug-in in the
// radar window's client coordinates. The SDK exposes no window handle, so the
// two candidates under the cursor - the deepest child window and its top-level
// owner - are tried in turn, and whichever of them puts the cursor inside this
// screen's radar area is the right one. Returns false when the cursor is over
// another application, over EuroScope's own chrome, or over another display.
bool CGalaxyATMSystemRadarScreen::CursorRadarPoint(POINT& out)
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
            return true;
        }
    }
    return false;
}

// One left click on the armed radar places one end of a measuring line: the
// first click anchors the start on the point the line is to run from, the
// second fixes the end and leaves the finished line on the screen. In between
// the line follows the cursor by itself, so nothing is ever held down and no
// drag is captured - the radar keeps panning exactly as it does with no line
// at all. The finished line disarms the ruler, so the next one starts with a
// fresh press of the side button.
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

    // Two presses in the same spot are a fumbled click rather than a
    // measurement of no length: the line itself would be too short to draw,
    // but it would still collect delete hit-boxes and sit there invisibly.
    POINT a = ConvertCoordFromPositionToPixel(ResolveRulerPoint(
        m_rulerPending.startSnapped, m_rulerPending.startCallsign, m_rulerPending.startFixed));
    POINT b = ConvertCoordFromPositionToPixel(ResolveRulerPoint(
        m_rulerPending.endSnapped, m_rulerPending.endCallsign, m_rulerPending.endFixed));
    if (abs(a.x - b.x) > 4 || abs(a.y - b.y) > 4)
        m_rulers.push_back(m_rulerPending);
    m_rulerPlacing = false;
    m_rulerArmed = false;
}

// While the ruler is armed the cursor carries a small crosshair, so the mode
// is visible on the radar itself and not only in a tooltip. It is gone the
// moment the first point is placed - from there on the line being drawn out
// says the same thing.
void CGalaxyATMSystemRadarScreen::DrawRulerCursor(HDC hDC)
{
    POINT pt;
    if (!CursorRadarPoint(pt))
        return;

    int saved = SaveDC(hDC);
    HPEN pen = CreatePen(PS_SOLID, Theme::RulerWidth, Theme::Ruler);
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

// The free end re-evaluates the snap every time it moves, so it magnetises
// onto a target as the cursor passes over it and lets go again once it moves
// away - the same behaviour the dragged end used to have.
void CGalaxyATMSystemRadarScreen::UpdateRulerEnd(POINT pt)
{
    std::string cs;
    m_rulerPending.endSnapped = FindNearbyTarget(pt, cs);
    if (m_rulerPending.endSnapped)
        m_rulerPending.endCallsign = cs;
    else
        m_rulerPending.endFixed = ConvertCoordFromPixelToPosition(pt);
}

// A copy of baseFont (normally EuroScope's own tag font) sized a little
// smaller, so the ruler's bearing/distance/time readout does not compete
// with the tags for attention. Cached against the font it was derived from,
// so this only recreates a GDI font object when that source actually
// changes rather than once per frame.
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

    HPEN pen = CreatePen(PS_SOLID, Theme::RulerWidth, Theme::Ruler);
    HPEN oldPen = (HPEN)SelectObject(hDC, pen);
    MoveToEx(hDC, p0.x, p0.y, NULL);
    LineTo(hDC, p1.x, p1.y);

    // One-sided tick at the midpoint, always pointing towards the top of the
    // screen so the label reads above the line, exactly like the reference.
    int midX = (p0.x + p1.x) / 2, midY = (p0.y + p1.y) / 2;
    double ux = dx / len, uy = dy / len;
    double perpX = -uy, perpY = ux;
    if (perpY > 0) { perpX = -perpX; perpY = -perpY; }
    const double tickLen = 26.0;
    POINT tickTop = { midX + (int)(perpX * tickLen), midY + (int)(perpY * tickLen) };

    // Where the label actually hangs. Undragged that is the end of the tick;
    // dragged, the tick is simply stretched to follow it, so a readout moved
    // clear of the traffic underneath still points back at its own line. The
    // stroke itself is drawn further down, once the text has been measured -
    // it has to stop at the edge of the label rather than run under it.
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

    // Stacked directly above the tick's far end - time nearest the tick,
    // bearing furthest away - left-aligned starting a couple pixels left of it.
    // Drawn in EuroScope's own tag font (m_esFont, captured in OnRefresh) at
    // its full size, so it reads as part of the same radar picture as the tags
    // around it rather than a second, smaller typeface.
    HFONT rulerFont = GetRulerFont(m_esFont ? m_esFont : m_fonts.Ruler);

    // Row pitch taken from that font's own metrics rather than a guessed
    // constant, so the three lines stay snug together whatever size EuroScope
    // happens to be using.
    int rowH = 15;
    {
        HFONT oldFont = (HFONT)SelectObject(hDC, rulerFont);
        TEXTMETRICW tm;
        if (GetTextMetricsW(hDC, &tm))
            rowH = tm.tmHeight + tm.tmExternalLeading;
        SelectObject(hDC, oldFont);
    }

    // The three rows as one block, only as wide as the widest of them - a flat
    // 150 px box would reach far past the text, swallowing clicks meant for
    // whatever sits beside it and leaving the leader nothing sensible to stop
    // against. Measured before anything is drawn, because the leader below is
    // clipped to this box.
    int textW = 0;
    for (const wchar_t* t : { line1, line2, line3 })
        textW = max(textW, (int)Theme::MeasureText(hDC, rulerFont, t).cx);

    int left = labelAt.x - 4;
    RECT r3 = { left, labelAt.y - rowH, left + 150, labelAt.y };
    RECT r2 = { left, r3.top - rowH, left + 150, r3.top };
    RECT r1 = { left, r2.top - rowH, left + 150, r2.top };
    r.labelRect = { left, r1.top, left + textW + 8, r3.bottom };

    // The leader from the midpoint of the line to the readout, stopped a few
    // pixels short of the text. Undragged it lands on the bottom-left corner
    // and reads exactly as the old fixed-length tick did; dragged to the other
    // side of the line it now stops at whichever edge it reaches first instead
    // of striking through the three rows. Nothing is drawn at all when the
    // label has been pulled over the midpoint itself - there is no gap left to
    // bridge, and any stroke there would only be the text's own underlay.
    RECT leaderStop = r.labelRect;
    InflateRect(&leaderStop, 3, 3);

    POINT from = { midX, midY };
    double lx = (double)labelAt.x - from.x, ly = (double)labelAt.y - from.y;
    double tEnter = 0.0, tExit = 1.0;
    const double clipP[4] = { -lx, lx, -ly, ly };
    const double clipQ[4] = { (double)from.x - leaderStop.left, (double)leaderStop.right - from.x,
                              (double)from.y - leaderStop.top,  (double)leaderStop.bottom - from.y };
    bool drawLeader = true;
    for (int i = 0; i < 4 && drawLeader; i++)
    {
        if (fabs(clipP[i]) < 1e-9)
        {
            if (clipQ[i] < 0.0)
                drawLeader = false;   // parallel to this edge and outside it
            continue;
        }
        double t = clipQ[i] / clipP[i];
        if (clipP[i] < 0.0) { if (t > tEnter) tEnter = t; }
        else                { if (t < tExit)  tExit = t; }
    }
    if (drawLeader && tEnter > 0.0 && tEnter <= tExit)
    {
        MoveToEx(hDC, from.x, from.y, NULL);
        LineTo(hDC, from.x + (int)lround(lx * tEnter), from.y + (int)lround(ly * tEnter));
    }

    SelectObject(hDC, oldPen);
    DeleteObject(pen);

    Theme::DrawLine(hDC, r1, line1, rulerFont, Theme::Ruler, DT_LEFT | DT_VCENTER);
    Theme::DrawLine(hDC, r2, line2, rulerFont, Theme::Ruler, DT_LEFT | DT_VCENTER);
    Theme::DrawLine(hDC, r3, line3, rulerFont, Theme::Ruler, DT_LEFT | DT_VCENTER);

    // Registered last of everything on the radar, so the drag beats both the
    // line's own delete boxes and EuroScope's panning. A line still being
    // placed (index < 0) has nothing to drag yet, and while the ruler is armed
    // the whole radar is a click target for the next point - a drag handle on
    // top of that would eat the click that ends the line.
    if (index >= 0 && !m_rulerArmed && !m_rulerPlacing)
    {
        char id[16];
        sprintf_s(id, "%d", index);
        AddScreenObject(SO_RULER_LABEL, id, r.labelRect, true, "Перетащить информацию линейки");
    }

    RestoreDC(hDC, saved);
}

// ---- Popups (БЛОК 3 value pickers) ------------------------------------------
// Typed in rather than picked from a list: the flight-level range is far too
// long to make a list worth scrolling through.
void CGalaxyATMSystemRadarScreen::OpenAltFilterPicker(RECT area, bool isFrom)
{
    int current = isFrom ? Plugin()->AltFilterFromFL() : Plugin()->AltFilterToFL();
    char initial[8];
    sprintf_s(initial, "%03d", current);
    GetPlugIn()->OpenPopupEdit(area, isFrom ? FN_ALTFILTER_FROM : FN_ALTFILTER_TO, initial);
}

// ---- Interaction --------------------------------------------------------------
// Removes whichever ruler line passes closest to the click, if any is within
// range - shared by a right-click and a left double-click on the ruler canvas.
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
    if (ObjectType == SO_RULER_LINE)
    {
        if (Button == BUTTON_RIGHT)
            RemoveRulerNear(Pt);
        return;
    }

    // The armed radar surface: a left click puts down one end of the line, a
    // right click abandons the whole thing.
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

    // The press already did the work; the click that follows it is nothing.
    if (ObjectType == SO_SIGMET_AREA)
        return;

    // Any click that isn't on the open list, or on a chevron that toggles it,
    // dismisses the list.
    if (ObjectType != SO_DROPDOWN_ITEM &&
        ObjectType != SO_VEC_DIST_FIELD && ObjectType != SO_VEC_TIME_FIELD)
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
        m_openDropdown = DropdownKind::None;
        RequestRefresh();
        break;
    }

    case SO_PANEL_COLLAPSE:
        m_collapsed = !m_collapsed;
        RequestRefresh();
        break;

    case SO_TIMER_TOGGLE:
        // Пуск и стоп по левой кнопке, сброс по правой. The timer is what a
        // controller times a hold or an approach with, so it starts when it is
        // pressed and not when the session connects - the shift's own length
        // is on the clock above it anyway.
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
            // Resumed from where it stopped rather than restarted: сброс is
            // the right button's job and nothing else zeroes the count.
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
        // Д and Э are one choice rather than two switches: the extrapolation
        // vector is drawn by distance or by time, never by both at once, so
        // turning either on turns the other off. Clicking the one already on
        // is still the way to have no extrapolation vector at all.
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

    // The report is opened either from the panel's own button or from the INDEX
    // АТИС strip on the radar; the strip itself is hidden and shown by ".atis"
    // alone, since it carries no chrome of its own to close it with.
    case SO_ATIS_BUTTON:
    case SO_ATIS_LETTER_HEADER:
        m_atisOpen = !m_atisOpen;
        m_atisScrollPx = 0;
        RequestRefresh();
        break;
    // "Список РЦ". The sort chip carries two settings at once - which column
    // orders the list, and which way round - and shows both: the field name
    // steps on when the chip's triangle end is clicked, the arrow beside it
    // reverses the order when that end is.
    case SO_RC_SORT:
        m_rcSortKey = (m_rcSortKey + 1) % kRcSortKeyCount;
        RequestRefresh();
        break;
    case SO_RC_SORT_DIR:
        m_rcSortAsc = !m_rcSortAsc;
        RequestRefresh();
        break;
    case SO_RC_FILTER:
        GetPlugIn()->OpenPopupEdit(Area, FN_RC_FILTER, Narrow(m_rcFilter).c_str());
        break;
    case SO_RC_FILTER_CLR:
        m_rcFilter.clear();
        m_rcScroll = m_rcScrollMine = 0;
        RequestRefresh();
        break;

    // Each pane scrolls on its own - the object id says which one was clicked.
    // Both are clamped against the live row count as the window draws, so a
    // step past the end costs nothing.
    case SO_RC_UP:
    {
        int& scroll = (sObjectId[0] == '1') ? m_rcScrollMine : m_rcScroll;
        scroll = max(0, scroll - 1);
        RequestRefresh();
        break;
    }
    case SO_RC_DOWN:
    {
        int& scroll = (sObjectId[0] == '1') ? m_rcScrollMine : m_rcScroll;
        scroll++;
        RequestRefresh();
        break;
    }
    case SO_RC_ROW:
    {
        // The same selection the radar makes, so the tag, the lists and every
        // tag function are all pointed at the aircraft that was clicked.
        CFlightPlan picked = GetPlugIn()->FlightPlanSelect(sObjectId);
        if (picked.IsValid())
            GetPlugIn()->SetASELAircraft(picked);
        RequestRefresh();
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

    // A zone's details are not handled here at all - they come up while the
    // button is held (OnButtonDownScreenObject) and go away when it is let go.

    default:
        break;
    }
}

// Holding the left button down on a сигмет's outline - or Shift and the left
// button on a зона - opens what it has to say, and letting go closes it again.
// See OnRefresh for why a зона wants Shift. The press is routed here by
// whichever hit-box it landed on, but which area it belongs to is worked out
// from the geometry rather than from that box's id, so overlapping areas each
// answer for the part of themselves the cursor is actually on.
//
// A зона whose geometry answers for none of them falls back on the box's own
// area - the one whose outline runs nearest the middle of that square. The
// click is ours either way once it has landed on a box: EuroScope does not
// pass it on to whatever is underneath, so refusing it there would only lose
// the press rather than give it to the traffic.
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

// The matching release. It only arrives while the cursor is still on one of
// our objects, so the poll timer closes the window in every other case - see
// CloseSigmetInfoIfButtonReleased.
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

// A left double-click on a ruler line removes it - the same target search as
// the right-click, just reached through EuroScope's own double-click event
// instead of a second click registered as a plain click.
void CGalaxyATMSystemRadarScreen::OnDoubleClickScreenObject(int ObjectType, const char* sObjectId,
    POINT Pt, RECT Area, int Button)
{
    if (ObjectType == SO_RULER_LINE && Button == BUTTON_LEFT)
        RemoveRulerNear(Pt);
}

void CGalaxyATMSystemRadarScreen::OnFunctionCall(int FunctionId, const char* sItemString, POINT Pt, RECT Area)
{
    // The "ULLL Squawk" column's clicks and menu. EuroScope may deliver a TAG
    // item function here or to the plugin, depending on whether it was clicked
    // on a tag or in an AC list, so both routes lead to the same handler, which
    // ignores ids that are not its own and drops a duplicate of one click.
    Plugin()->HandleSquawkFunction(FunctionId, sItemString, Area, "screen");

    if (FunctionId == FN_RC_FILTER)
    {
        m_rcFilter = (sItemString != NULL) ? Widen(sItemString) : std::wstring();
        m_rcScroll = m_rcScrollMine = 0;   // a new filter is a new list; start at the top
        RequestRefresh();
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

    // Accept what was typed only if it is a plain flight level; anything else
    // leaves the current value alone rather than resetting the filter to zero.
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

    // A ruler's readout moves relative to its own line rather than to the
    // screen: the offset is what is stored, so the label keeps its place
    // beside the measurement when the radar is panned or zoomed.
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

    // The АТИС report and the sector list are the only things that can be
    // dragged - the panel is docked to the right edge of the radar area, and
    // the АТИС index strip is docked to the panel.
    RECT* target = NULL;
    if (ObjectType == SO_RC_HEADER)
        target = &m_rcArea;
    else if (ObjectType == SO_ATIS_HEADER)
        target = &m_atisArea;
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
    if (cmd == ".sigmet")
    {
        m_sigmetsVisible = !m_sigmetsVisible;
        if (!m_sigmetsVisible)
            m_sigmetInfoIndex = -1;
        // The count comes along with the toggle: a feed that could not be
        // reached leaves the overlay empty and otherwise says nothing at all,
        // and "показаны, 0" is the only way to tell that from clear weather.
        std::wstring what = m_sigmetsVisible ? L"показаны" : L"скрыты";
        std::shared_ptr<const std::vector<Sigmet>> list = Plugin()->Sigmets();
        what += L", загружено: " + std::to_wstring(list ? list->size() : 0);
        std::string msg = Narrow(what);
        GetPlugIn()->DisplayUserMessage("ULLL Panel", Narrow(L"Сигметы").c_str(),
            msg.c_str(), true, false, false, false, false);
        RequestRefresh();
        return true;
    }
    if (cmd == ".zones")
    {
        m_zonesVisible = !m_zonesVisible;
        if (!m_zonesVisible)
            m_zoneInfoIndex = -1;
        // Both counts, because either one alone is ambiguous: nothing on the
        // radar can mean the file was not read, or that the plan has nothing
        // booked at this hour, and those call for very different fixes.
        size_t active = 0;
        for (char on : m_zoneActive)
            active += on ? 1 : 0;
        std::wstring what = m_zonesVisible ? L"показаны" : L"скрыты";
        what += L", активно: " + std::to_wstring(active)
            + L" из " + std::to_wstring(Plugin()->GetConfig().Zones().size());

        // What the two feeds have to say, since between them they decide most
        // of that count: a plan that did not come back and a NOTAM source that
        // was never configured look identical on the radar.
        std::shared_ptr<const std::vector<ZoneBooking>> aup = Plugin()->AupBookings();
        what += L", план: " + std::to_wstring(aup ? aup->size() : 0);

        std::shared_ptr<const std::vector<ZoneBooking>> notams = Plugin()->Notams();
        what += L", нотамы: ";
        if (!notams)
            what += Plugin()->GetConfig().NotamSource().empty() ? L"источник не задан" : L"не прочитаны";
        else
            what += std::to_wstring(notams->size());
        std::string msg = Narrow(what);
        GetPlugIn()->DisplayUserMessage("ULLL Panel", Narrow(L"Зоны").c_str(),
            msg.c_str(), true, false, false, false, false);
        RequestRefresh();
        return true;
    }
    // "Список РЦ" - the sector list. No control on the panel either: the list
    // is a window of its own, and this is how it is called up and put away.
    if (cmd == ".rc")
    {
        m_rcOpen = !m_rcOpen;
        RequestRefresh();
        return true;
    }
    // The INDEX АТИС strip has no button on the panel and no "x" of its own -
    // this is the only thing that hides and brings it back.
    if (cmd == ".atis")
    {
        m_atisLetterOpen = !m_atisLetterOpen;
        RequestRefresh();
        return true;
    }
    // The side button arms a line and left clicks place it; ".ruler" is the
    // way out of one started by mistake, since abandoning it needs no cursor
    // of its own.
    if (cmd == ".ruler")
    {
        m_rulerPlacing = false;
        m_rulerArmed = false;
        m_rulerPressPending = false;
        RequestRefresh();
        return true;
    }
    // ".rulerbtn 1" / "2" pick which thumb button draws the lines, "0" turns
    // the shortcut off - and with it the only way to draw one.
    if (cmd.compare(0, 10, ".rulerbtn ") == 0)
    {
        std::string arg = cmd.substr(10);
        const wchar_t* what = NULL;
        if (arg == "0")      { m_rulerButton = 0;            what = L"боковая кнопка отключена"; }
        else if (arg == "1") { m_rulerButton = VK_XBUTTON1;  what = L"боковая кнопка 1"; }
        else if (arg == "2") { m_rulerButton = VK_XBUTTON2;  what = L"боковая кнопка 2"; }
        if (what == NULL)
            return false;

        m_rulerButtonDown = false;
        std::string msg = Narrow(what);
        GetPlugIn()->DisplayUserMessage("ULLL Panel", Narrow(L"Линейка").c_str(),
            msg.c_str(), true, false, false, false, false);
        return true;
    }
    // The config file is read at load and never again, which is no way to tune
    // зоны or their colours: every change cost a restart of EuroScope. This
    // re-reads it and starts every feed over on what it now says.
    if (cmd == ".reload")
    {
        Plugin()->ReloadConfig();

        // Whatever was open pointed into the old lists. Everything else a
        // screen holds about зоны is worked out per frame.
        m_zoneInfoIndex = -1;
        m_sigmetInfoIndex = -1;
        m_zoneActive.clear();
        m_zoneBooking.clear();
        m_aup.reset();
        m_notams.reset();

        const Config& cfg = Plugin()->GetConfig();
        std::wstring what = L"зон: " + std::to_wstring(cfg.Zones().size())
            + L", постов: " + std::to_wstring(cfg.PositionCount());
        if (!cfg.LoadError().empty())
            what += L" - " + cfg.LoadError();

        std::string msg = Narrow(what);
        GetPlugIn()->DisplayUserMessage("ULLL Panel", Narrow(L"Конфигурация").c_str(),
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

// ---- ASR persistence --------------------------------------------------------
void CGalaxyATMSystemRadarScreen::OnAsrContentToBeClosed(void)
{
    delete this;
}

void CGalaxyATMSystemRadarScreen::OnAsrContentToBeSaved(void)
{
    // No "PanelPos": the panel is docked to the right edge of the radar area
    // and has no position of its own to remember.
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

    sprintf_s(buf, "%d,%d", m_osLines, m_osSpeed ? 1 : 0);
    SaveDataToAsr("Os", "lines,speed", buf);

    sprintf_s(buf, "%d", m_rulerButton);
    SaveDataToAsr("RulerButton", "side mouse button that arms the ruler (0 = off)", buf);

    sprintf_s(buf, "%d", m_sigmetsVisible ? 1 : 0);
    SaveDataToAsr("Sigmets", "сигмет areas drawn on the radar", buf);

    sprintf_s(buf, "%d", m_zonesVisible ? 1 : 0);
    SaveDataToAsr("Zones", "запретные зоны drawn on the radar", buf);

    sprintf_s(buf, "%d", m_atisLetterOpen ? 1 : 0);
    SaveDataToAsr("AtisLetter", "АТИС letter window shown", buf);

    sprintf_s(buf, "%d,%d,%d", m_rcOpen ? 1 : 0, m_rcSortKey, m_rcSortAsc ? 1 : 0);
    SaveDataToAsr("SectorList", "open,sortKey,sortAscending", buf);

    SaveDataToAsr("SectorListFilter", "список РЦ - фильтр по позывному",
        Narrow(m_rcFilter).c_str());

    // The gain is written for the sake of the format only - it is the radar
    // scale now, which EuroScope stores in the ASR itself and which the slider
    // re-derives on the first frame.
    sprintf_s(buf, "%d,%d,%d,%d", m_codeAll ? 1 : 0, m_codeBp ? 1 : 0,
        m_codeExtra ? 1 : 0, m_vvGain);
    SaveDataToAsr("CodeBlock", "all,bp,extra,gain", buf);

    SaveDataToAsr("CodeFilter", "ВВ1 code filter", Narrow(m_codeFilter).c_str());
}

void CGalaxyATMSystemRadarScreen::OnAsrContentLoaded(bool Loaded)
{
    if (!Loaded)
        return;

    // "PanelPos" written by an older build is ignored - the panel is docked.
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
            // An ASR written before Д and Э became one choice can carry both;
            // the distance vector wins so the pair is never left in a state
            // the panel itself cannot produce.
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
        int lines = 0, speed = 0;
        if (sscanf_s(os, "%d,%d", &lines, &speed) == 2)
        {
            m_osLines = (lines == 3) ? 3 : 2;
            m_osSpeed = speed != 0;
        }
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

    // "Список РЦ": whether it is up, and how it was last sorted. The scroll
    // position is not restored - the traffic will have moved on by the next
    // session, so the list opens at the top.
    const char* rc = GetDataFromAsr("SectorList");
    if (rc != NULL)
    {
        int open = 0, key = 0, asc = 1;
        if (sscanf_s(rc, "%d,%d,%d", &open, &key, &asc) == 3)
        {
            m_rcOpen = (open != 0);
            m_rcSortKey = (key >= 0 && key < kRcSortKeyCount) ? key : 0;
            m_rcSortAsc = (asc != 0);
        }
    }

    const char* rcFilter = GetDataFromAsr("SectorListFilter");
    if (rcFilter != NULL)
        m_rcFilter = Widen(rcFilter);
}
