#include "pch.h"
#include "FloatWindow.h"
#include "Log.h"

#include <windowsx.h>

namespace
{
    const wchar_t kClassName[] = L"GalaxyATMSystemFloat";

    const UINT kContinueDrag = WM_APP + 0x48;

    HINSTANCE ThisModule()
    {
        HMODULE module = NULL;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCWSTR)&ThisModule, &module);
        return module;
    }
}

bool FloatWindow::Create(HWND owner)
{
    if (m_hwnd != NULL)
        return true;

    HINSTANCE module = ThisModule();
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = Proc;
    wc.hInstance = module;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc) && GetLastError() == ERROR_CLASS_ALREADY_EXISTS)
    {
        if (UnregisterClassW(kClassName, module))
            RegisterClassExW(&wc);
    }

    m_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, kClassName, L"Список РЦ", WS_POPUP,
        0, 0, 1, 1, owner, NULL, module, this);
    if (m_hwnd == NULL)
    {
        Log::Error("float", "the window could not be made - " + Log::SystemError(GetLastError()));
        return false;
    }
    return true;
}

void FloatWindow::Destroy()
{
    if (m_hwnd != NULL)
    {
        SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, 0);
        DestroyWindow(m_hwnd);
        m_hwnd = NULL;
    }
    if (m_memDC != NULL)
    {
        SelectObject(m_memDC, m_oldBitmap);
        DeleteDC(m_memDC);
        m_memDC = NULL;
    }
    if (m_bitmap != NULL)
    {
        DeleteObject(m_bitmap);
        m_bitmap = NULL;
    }
    m_size = { 0, 0 };
}

HDC FloatWindow::BeginFrame(int w, int h)
{
    if (m_hwnd == NULL || w <= 0 || h <= 0)
        return NULL;

    if (m_memDC == NULL || w != m_size.cx || h != m_size.cy)
    {
        HDC screen = GetDC(NULL);
        HBITMAP bitmap = CreateCompatibleBitmap(screen, w, h);
        if (m_memDC == NULL)
        {
            m_memDC = CreateCompatibleDC(screen);
            m_oldBitmap = SelectObject(m_memDC, bitmap);
        }
        else
        {
            SelectObject(m_memDC, bitmap);
        }
        ReleaseDC(NULL, screen);

        if (m_bitmap != NULL)
            DeleteObject(m_bitmap);
        m_bitmap = bitmap;
        m_size = { w, h };
    }
    return m_memDC;
}

void FloatWindow::EndFrame()
{
    if (m_hwnd == NULL)
        return;

    RECT r;
    GetWindowRect(m_hwnd, &r);
    if (r.right - r.left != m_size.cx || r.bottom - r.top != m_size.cy)
        SetWindowPos(m_hwnd, NULL, 0, 0, m_size.cx, m_size.cy, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    if (!IsWindowVisible(m_hwnd))
        ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    InvalidateRect(m_hwnd, NULL, FALSE);
    UpdateWindow(m_hwnd);
}

void FloatWindow::Hide()
{
    if (m_hwnd != NULL && IsWindowVisible(m_hwnd))
        ShowWindow(m_hwnd, SW_HIDE);
}

POINT FloatWindow::Position() const
{
    RECT r = { 0, 0, 0, 0 };
    if (m_hwnd != NULL)
        GetWindowRect(m_hwnd, &r);
    POINT p = { r.left, r.top };
    return p;
}

void FloatWindow::MoveTo(POINT screen)
{
    if (m_hwnd != NULL)
        SetWindowPos(m_hwnd, NULL, screen.x, screen.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void FloatWindow::ContinueDrag()
{
    if (m_hwnd != NULL)
        PostMessageW(m_hwnd, kContinueDrag, 0, 0);
}

void FloatWindow::CaptureMouse()
{
    if (m_hwnd != NULL)
        SetCapture(m_hwnd);
}

void FloatWindow::ReleaseMouse()
{
    if (m_hwnd != NULL && GetCapture() == m_hwnd)
        ReleaseCapture();
}

LRESULT CALLBACK FloatWindow::Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_NCCREATE)
    {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
    }
    FloatWindow* self = (FloatWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (self == NULL)
        return DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg)
    {
    case WM_NCHITTEST:
    {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(hwnd, &pt);
        if (self->onHitTest && self->onHitTest(pt) == Hit::Caption)
            return HTCAPTION;
        return HTCLIENT;
    }

    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MOUSEMOVE:
        if (self->onMouse)
        {
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            self->onMouse(msg, pt);
        }
        return 0;

    case WM_ENTERSIZEMOVE:
        if (self->onMoveStart)
            self->onMoveStart();
        return 0;

    case WM_EXITSIZEMOVE:
        if (self->onMoved)
            self->onMoved();
        return 0;

    case kContinueDrag:
        if (GetAsyncKeyState(VK_LBUTTON) & 0x8000)
        {
            ReleaseCapture();
            SendMessageW(hwnd, WM_SYSCOMMAND, SC_MOVE | HTCAPTION, 0);
        }
        else if (self->onMoved)
        {
            self->onMoved();
        }
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        if (self->m_memDC != NULL)
            BitBlt(dc, 0, 0, self->m_size.cx, self->m_size.cy, self->m_memDC, 0, 0, SRCCOPY);
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
