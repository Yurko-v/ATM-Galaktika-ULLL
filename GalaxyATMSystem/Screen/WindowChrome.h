#pragma once

#include "Core/Common.h"

namespace Galaxy
{
    inline void DrawScrollArrow(HDC hDC, const RECT& r, bool up, COLORREF ink)
    {
        const Gdiplus::REAL cx = (r.left + r.right) / 2.0f - 0.5f;
        const Gdiplus::REAL cy = (r.top + r.bottom) / 2.0f - 0.5f;
        const Gdiplus::REAL half = 4.0f, rise = up ? -2.5f : 2.5f;
        const Gdiplus::PointF tip[] = {
            Gdiplus::PointF(cx - half, cy - rise),
            Gdiplus::PointF(cx + half, cy - rise),
            Gdiplus::PointF(cx, cy + rise),
        };
        Gdiplus::Graphics g(hDC);
        g.SetSmoothingMode(Theme::Smoothing());
        Gdiplus::SolidBrush brush(Theme::GdiColor(ink));
        g.FillPolygon(&brush, tip, 3);
    }

    inline void DrawCloseCross(HDC hDC, const RECT& close, COLORREF ink)
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
