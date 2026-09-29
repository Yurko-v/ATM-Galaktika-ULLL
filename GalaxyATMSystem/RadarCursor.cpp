#include "pch.h"
#include "RadarCursor.h"
#include "Log.h"

#include <map>

namespace
{
    const char* const kShape[] = {
        "o",
        "oo",
        "oXo",
        "oXXo",
        "oXXXo",
        "oXXXXo",
        "oXXXXXo",
        "oXXXXXXo",
        "oXXXXXXXo",
        "oXXXXXooo",
        "oXXoXXo",
        "oXo oXXo",
        "oo  oXXo",
        "     oXXo",
        "     oXXo",
        "      oo",
    };

    const int kCursorSide = 32;
    const int kRowBytes = kCursorSide / 8;

    std::map<HWND, RECT> g_views;
    HHOOK g_hook = NULL;
    HCURSOR g_cursor = NULL;
    bool g_released = false;

    HCURSOR SmallBlackArrow()
    {
        if (g_cursor != NULL)
            return g_cursor;

        BYTE andMask[kCursorSide * kRowBytes];
        BYTE xorMask[kCursorSide * kRowBytes];
        memset(andMask, 0xFF, sizeof(andMask));
        memset(xorMask, 0x00, sizeof(xorMask));

        for (int y = 0; y < (int)_countof(kShape); y++)
            for (int x = 0; kShape[y][x] != '\0' && x < kCursorSide; x++)
            {
                const char pixel = kShape[y][x];
                if (pixel == ' ')
                    continue;
                const int index = y * kRowBytes + x / 8;
                const BYTE bit = (BYTE)(0x80 >> (x % 8));
                andMask[index] &= (BYTE)~bit;
                if (pixel == 'o')
                    xorMask[index] |= bit;
            }

        g_cursor = CreateCursor(GetModuleHandleW(NULL), 0, 0, kCursorSide, kCursorSide, andMask, xorMask);
        if (g_cursor == NULL)
            Log::Error("cursor", "the radar cursor could not be made - " + Log::SystemError(GetLastError()));
        return g_cursor;
    }

    LRESULT CALLBACK AfterMessage(int code, WPARAM wp, LPARAM lp)
    {
        if (code == HC_ACTION)
        {
            const CWPRETSTRUCT* msg = (const CWPRETSTRUCT*)lp;
            if (msg->message == WM_SETCURSOR && LOWORD(msg->lParam) == HTCLIENT
                && (HWND)msg->wParam == msg->hwnd)
            {
                auto view = g_views.find(msg->hwnd);
                POINT p;
                if (view != g_views.end() && GetCursorPos(&p) && ScreenToClient(msg->hwnd, &p)
                    && PtInRect(&view->second, p))
                {
                    HCURSOR cursor = SmallBlackArrow();
                    if (cursor != NULL)
                        SetCursor(cursor);
                }
            }
        }
        return CallNextHookEx(g_hook, code, wp, lp);
    }
}

void RadarCursor::Attach(HWND view, const RECT& area)
{
    if (view == NULL || g_released)
        return;

    for (auto it = g_views.begin(); it != g_views.end();)
        it = IsWindow(it->first) ? std::next(it) : g_views.erase(it);

    if (g_hook == NULL)
    {
        g_hook = SetWindowsHookExW(WH_CALLWNDPROCRET, AfterMessage, NULL, GetCurrentThreadId());
        if (g_hook == NULL)
        {
            Log::Error("cursor", "the radar cursor hook could not be set - " + Log::SystemError(GetLastError()));
            g_released = true;
            return;
        }
    }
    g_views[view] = area;
}

void RadarCursor::DetachAll()
{
    g_released = true;
    if (g_hook != NULL)
    {
        UnhookWindowsHookEx(g_hook);
        g_hook = NULL;
    }
    g_views.clear();

    if (g_cursor != NULL)
    {
        if (GetCursor() == g_cursor)
            SetCursor(LoadCursor(NULL, IDC_ARROW));
        DestroyCursor(g_cursor);
        g_cursor = NULL;
    }
}
