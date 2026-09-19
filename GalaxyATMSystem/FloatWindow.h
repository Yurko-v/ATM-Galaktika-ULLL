#pragma once

#include <windows.h>
#include <functional>

// -----------------------------------------------------------------------------
// A window of the plug-in's own that stands outside EuroScope - for "Список
// РЦ" once it has been pulled off the radar, onto another monitor or simply
// past the edge of EuroScope's window, where nothing EuroScope draws can
// reach.
//
// It draws nothing itself: the radar screen paints a frame into the bitmap
// BeginFrame hands out, the same drawing it does on the radar, and EndFrame
// puts that on the screen. The mouse goes back the same way, as callbacks in
// the window's own client coordinates. A top-level popup owned by EuroScope's
// main window, so it stays in front of EuroScope, goes when EuroScope is
// minimised and never has a taskbar button of its own; and it never takes the
// activation from EuroScope on a click, so the keyboard stays where the
// controller was typing.
// -----------------------------------------------------------------------------
class FloatWindow
{
public:
    // Where on the window the press is: the title bar is dragged by Windows
    // itself, everything else is a click for the owner.
    enum class Hit { Client, Caption };

    FloatWindow() = default;
    FloatWindow(const FloatWindow&) = delete;
    FloatWindow& operator=(const FloatWindow&) = delete;
    ~FloatWindow() { Destroy(); }

    std::function<Hit(POINT)> onHitTest;
    // A button pressed and let go, WM_LBUTTONDOWN.. WM_RBUTTONUP, and the cursor
    // moving - while the owner has captured the mouse, anywhere on the screen.
    std::function<void(UINT msg, POINT pt)> onMouse;
    // Let go after being dragged by its title bar - the owner may take it back.
    std::function<void()> onMoved;
    // A drag by the title bar is starting.
    std::function<void()> onMoveStart;

    // Made on first use, owned by 'owner' - EuroScope's main window.
    bool Create(HWND owner);
    bool IsCreated() const { return m_hwnd != NULL; }
    HWND Handle() const { return m_hwnd; }
    void Destroy();

    // A DC to draw the next frame into, 'w' x 'h' pixels; NULL when there is no
    // window. EndFrame resizes the window to it and shows it.
    HDC  BeginFrame(int w, int h);
    void EndFrame();

    void Hide();
    bool Visible() const { return m_hwnd != NULL && IsWindowVisible(m_hwnd); }

    // Its top left corner on the screen, and moving it there.
    POINT Position() const;
    void  MoveTo(POINT screen);

    // Takes over a drag already in progress with the left button held: the
    // window follows the cursor, holding it where it is now, until the
    // button is let go. Posted, so it starts after whatever handler asked.
    void ContinueDrag();

    // The mouse for this window until ReleaseMouse, for a pull on a grip.
    void CaptureMouse();
    void ReleaseMouse();

private:
    static LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    HWND    m_hwnd = NULL;
    HDC     m_memDC = NULL;
    HBITMAP m_bitmap = NULL;
    HGDIOBJ m_oldBitmap = NULL;
    SIZE    m_size = { 0, 0 };     // of the bitmap
};
