#pragma once

#include <windows.h>
#include <functional>

class FloatWindow
{
public:
    enum class Hit { Client, Caption };

    FloatWindow() = default;
    FloatWindow(const FloatWindow&) = delete;
    FloatWindow& operator=(const FloatWindow&) = delete;
    ~FloatWindow() { Destroy(); }

    std::function<Hit(POINT)> onHitTest;
    std::function<void(UINT msg, POINT pt)> onMouse;
    std::function<void()> onMoved;
    std::function<void()> onMoveStart;

    bool Create(HWND owner);
    bool IsCreated() const { return m_hwnd != NULL; }
    HWND Handle() const { return m_hwnd; }
    void Destroy();

    HDC  BeginFrame(int w, int h);
    void EndFrame();

    void Hide();
    bool Visible() const { return m_hwnd != NULL && IsWindowVisible(m_hwnd); }

    POINT Position() const;
    void  MoveTo(POINT screen);

    void ContinueDrag();

    void CaptureMouse();
    void ReleaseMouse();

private:
    static LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    HWND    m_hwnd = NULL;
    HDC     m_memDC = NULL;
    HBITMAP m_bitmap = NULL;
    HGDIOBJ m_oldBitmap = NULL;
    SIZE    m_size = { 0, 0 };
};
