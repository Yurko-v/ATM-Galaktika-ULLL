// AFV simulator for the Galaxy ATM System direction finder.
//
// Pretends to be Audio for VATSIM standalone: while the button is held it tells the
// plugin which callsigns are transmitting, exactly the way AFV does - WM_COPYDATA to
// the hidden window RDFHiddenWindowClass / RDFHiddenWindow, dwData 666, the callsigns
// joined with colons, and an empty string when the transmission ends.
//
// So the bearing line can be checked alone, with no pilots on the frequency: type the
// callsign of any target on the radar screen and hold the button.
//
// What it cannot do: our own transmission. AFV never reports it, so the control
// bearing only shows with TrackAudio.

#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>

#include <string>
#include <vector>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' "  \
    "version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace
{
    const char* const kBridgeClass = "RDFHiddenWindowClass";
    const char* const kBridgeTitle = "RDFHiddenWindow";
    const ULONG_PTR   kBridgeTag   = 666;

    enum { ID_CALLSIGNS = 100, ID_PTT, ID_LATCH, ID_STATUS, ID_TIMER };

    HWND    g_main = NULL;
    HWND    g_edit = NULL;
    HWND    g_ptt = NULL;
    HWND    g_latch = NULL;
    HWND    g_status = NULL;
    HFONT   g_font = NULL;
    HFONT   g_bigFont = NULL;
    WNDPROC g_buttonProc = NULL;
    HBRUSH  g_background = NULL;

    bool         g_onAir = false;
    std::string  g_sent;
    std::wstring g_statusText;
    COLORREF     g_statusColor = RGB(0x60, 0x60, 0x60);

    const COLORREF kGood = RGB(0x1A, 0x8F, 0x2E);
    const COLORREF kBad  = RGB(0xC8, 0x28, 0x1E);
    const COLORREF kIdle = RGB(0x60, 0x60, 0x60);

    void SetStatus(const std::wstring& text, COLORREF color)
    {
        g_statusText = text;
        g_statusColor = color;
        SetWindowTextW(g_status, text.c_str());
        InvalidateRect(g_status, NULL, TRUE);
    }

    // Every window the plugin opened. Normally one; two when the RDF plugin is
    // loaded as well, and then both get the message so neither is left out.
    std::vector<HWND> BridgeWindows()
    {
        std::vector<HWND> found;
        HWND after = NULL;
        while ((after = FindWindowExA(NULL, after, kBridgeClass, kBridgeTitle)) != NULL)
            found.push_back(after);
        return found;
    }

    // The callsigns from the box, upper cased and joined with colons the way AFV
    // joins them. Spaces, commas and semicolons all separate.
    std::string Callsigns()
    {
        wchar_t text[512] = {};
        GetWindowTextW(g_edit, text, _countof(text));

        std::string out, one;
        auto flush = [&]()
        {
            if (one.empty())
                return;
            if (!out.empty())
                out += ':';
            out += one;
            one.clear();
        };
        for (const wchar_t* p = text; *p != L'\0'; p++)
        {
            const wchar_t c = *p;
            if (c == L' ' || c == L',' || c == L';' || c == L':' || c == L'\t' || c == L'\r' || c == L'\n')
                flush();
            else if (c < 128)
                one += (char)toupper((unsigned char)c);
        }
        flush();
        return out;
    }

    std::wstring Widen(const std::string& s)
    {
        return std::wstring(s.begin(), s.end());
    }

    // Returns how many plugin windows took the message.
    int Send(const std::string& payload)
    {
        int delivered = 0;
        for (HWND target : BridgeWindows())
        {
            COPYDATASTRUCT data = {};
            data.dwData = kBridgeTag;
            data.cbData = (DWORD)payload.size() + 1;   // AFV counts the closing NUL
            data.lpData = (PVOID)payload.c_str();
            DWORD_PTR result = 0;
            if (SendMessageTimeoutA(target, WM_COPYDATA, (WPARAM)g_main, (LPARAM)&data,
                    SMTO_ABORTIFHUNG, 1000, &result) != 0)
                delivered++;
        }
        return delivered;
    }

    void ShowButton()
    {
        SetWindowTextW(g_ptt, g_onAir ? L"\x25CF  В ЭФИРЕ" : L"ПЕРЕДАЧА  (держать)");
    }

    void StartTransmission()
    {
        if (g_onAir)
            return;

        const std::string callsigns = Callsigns();
        if (callsigns.empty())
        {
            SetStatus(L"Впишите позывной цели, которая есть на радаре.", kBad);
            return;
        }

        const int delivered = Send(callsigns);
        if (delivered == 0)
        {
            SetStatus(L"Плагин не найден: EuroScope с Galaxy ATM System не запущен.", kBad);
            return;
        }

        g_onAir = true;
        g_sent = callsigns;
        ShowButton();
        std::wstring text = L"В эфире: " + Widen(callsigns);
        if (callsigns.find(':') != std::string::npos)
            text += L"  (одновременно - черты красные)";
        SetStatus(text, kGood);
    }

    void StopTransmission()
    {
        if (!g_onAir)
            return;

        Send("");
        g_onAir = false;
        ShowButton();
        SetStatus(L"Передача окончена: " + Widen(g_sent) + L". Пеленг остаётся в рамке.", kIdle);
        g_sent.clear();
    }

    bool Latched()
    {
        return SendMessageW(g_latch, BM_GETCHECK, 0, 0) == BST_CHECKED;
    }

    // Push to talk on the button itself: down starts, up ends. With the latch on,
    // a click toggles instead, for a long transmission without holding the mouse.
    LRESULT CALLBACK ButtonProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
            if (Latched())
            {
                if (g_onAir)
                    StopTransmission();
                else
                    StartTransmission();
            }
            else
            {
                StartTransmission();
            }
            break;

        case WM_LBUTTONUP:
            if (!Latched())
                StopTransmission();
            break;

        case WM_CAPTURECHANGED:
            // Dragged off the window or lost focus mid press: the key came up.
            if (!Latched())
                StopTransmission();
            break;

        case WM_KEYDOWN:
            if (wParam == VK_SPACE && (lParam & (1 << 30)) == 0)
            {
                if (Latched() && g_onAir)
                    StopTransmission();
                else
                    StartTransmission();
            }
            if (wParam == VK_SPACE)
                return 0;
            break;

        case WM_KEYUP:
            if (wParam == VK_SPACE)
            {
                if (!Latched())
                    StopTransmission();
                return 0;
            }
            break;
        }
        return CallWindowProcW(g_buttonProc, hwnd, msg, wParam, lParam);
    }

    void CheckPlugin()
    {
        if (g_onAir)
            return;
        const size_t windows = BridgeWindows().size();
        if (windows == 0)
            SetStatus(L"Плагин не найден: запустите EuroScope с Galaxy ATM System.", kBad);
        else if (windows == 1)
            SetStatus(L"Плагин найден. Впишите позывной и держите кнопку.", kGood);
        else
            SetStatus(L"Найдено окон: " + std::to_wstring(windows)
                + L" - загружен и плагин RDF. Сообщение уйдёт во все.", kGood);
    }

    HWND Child(const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id,
        HFONT font, DWORD exStyle = 0)
    {
        HWND hwnd = CreateWindowExW(exStyle, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h,
            g_main, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
        SendMessageW(hwnd, WM_SETFONT, (WPARAM)font, TRUE);
        return hwnd;
    }

    LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
        case WM_CREATE:
        {
            g_main = hwnd;
            NONCLIENTMETRICSW ncm = { sizeof(ncm) };
            SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
            g_font = CreateFontIndirectW(&ncm.lfMessageFont);
            LOGFONTW big = ncm.lfMessageFont;
            big.lfHeight = big.lfHeight * 3 / 2;
            big.lfWeight = FW_BOLD;
            g_bigFont = CreateFontIndirectW(&big);
            g_background = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));

            const int pad = 12, width = 380 - 2 * pad;
            int y = pad;
            Child(L"STATIC", L"Позывные целей на радаре (несколько через пробел - одновременная передача):",
                0, pad, y, width, 34, 0, g_font);
            y += 38;
            g_edit = Child(L"EDIT", L"", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL | ES_UPPERCASE,
                pad, y, width, 26, ID_CALLSIGNS, g_bigFont);
            y += 36;
            g_ptt = Child(L"BUTTON", L"", WS_TABSTOP | BS_PUSHBUTTON, pad, y, width, 58, ID_PTT, g_bigFont);
            g_buttonProc = (WNDPROC)SetWindowLongPtrW(g_ptt, GWLP_WNDPROC, (LONG_PTR)ButtonProc);
            ShowButton();
            y += 66;
            g_latch = Child(L"BUTTON", L"Фиксация: щелчок - начать, ещё щелчок - закончить",
                WS_TABSTOP | BS_AUTOCHECKBOX, pad, y, width, 22, ID_LATCH, g_font);
            y += 30;
            g_status = Child(L"STATIC", L"", SS_LEFT, pad, y, width, 38, ID_STATUS, g_font);

            SetFocus(g_edit);
            CheckPlugin();
            SetTimer(hwnd, ID_TIMER, 1000, NULL);
            return 0;
        }

        case WM_TIMER:
            if (wParam == ID_TIMER)
                CheckPlugin();
            return 0;

        case WM_CTLCOLORSTATIC:
            if ((HWND)lParam == g_status)
            {
                HDC dc = (HDC)wParam;
                SetTextColor(dc, g_statusColor);
                SetBkColor(dc, GetSysColor(COLOR_BTNFACE));
                return (LRESULT)g_background;
            }
            break;

        case WM_COMMAND:
            // Enter in the callsign box behaves like a short press of the button.
            if (LOWORD(wParam) == IDOK)
            {
                if (g_onAir)
                    StopTransmission();
                else
                    StartTransmission();
                return 0;
            }
            if (LOWORD(wParam) == ID_LATCH && !Latched())
                StopTransmission();
            break;

        case WM_ACTIVATE:
            // Switching to EuroScope while holding the key would otherwise leave the
            // plugin thinking the aircraft never stopped talking.
            if (LOWORD(wParam) == WA_INACTIVE && !Latched())
                StopTransmission();
            break;

        case WM_CLOSE:
            StopTransmission();
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            KillTimer(hwnd, ID_TIMER);
            if (g_font != NULL)
                DeleteObject(g_font);
            if (g_bigFont != NULL)
                DeleteObject(g_bigFont);
            if (g_background != NULL)
                DeleteObject(g_background);
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show)
{
    WNDCLASSW wc = {};
    wc.lpfnWndProc = MainProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"GalaxyAfvSimulator";
    RegisterClassW(&wc);

    // Stays on top, so it can sit over a full screen EuroScope.
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT frame = { 0, 0, 380, 236 };
    AdjustWindowRectEx(&frame, style, FALSE, WS_EX_TOPMOST);
    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST, wc.lpszClassName, L"Имитатор AFV - пеленгатор", style,
        CW_USEDEFAULT, CW_USEDEFAULT, frame.right - frame.left, frame.bottom - frame.top,
        NULL, NULL, instance, NULL);
    if (hwnd == NULL)
        return 1;
    ShowWindow(hwnd, show);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0)
    {
        if (!IsDialogMessageW(hwnd, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return 0;
}
