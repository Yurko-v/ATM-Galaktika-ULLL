#pragma once

#include "Core/Base.h"

namespace Galaxy
{
    inline void FillAlpha(HDC hDC, const RECT& r, COLORREF color, BYTE alpha)
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

    struct VectorCanvas
    {
        Gdiplus::Graphics g;
        Gdiplus::Pen pen;

        VectorCanvas(HDC hDC, COLORREF color, float width = Theme::VectorWidth)
            : g(hDC),
              pen(Gdiplus::Color(GetRValue(color), GetGValue(color), GetBValue(color)), width)
        {
            g.SetSmoothingMode(Theme::Smoothing());
            pen.SetLineJoin(Gdiplus::LineJoinRound);
        }

        void SetColor(COLORREF color)
        {
            pen.SetColor(Gdiplus::Color(GetRValue(color), GetGValue(color), GetBValue(color)));
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
            Prepare(area);
        }

        AreaCanvas(Gdiplus::Image* image, const RECT& area, COLORREF color, float width)
            : g(image),
              pen(Gdiplus::Color(GetRValue(color), GetGValue(color), GetBValue(color)),
                  width),
              clip(area)
        {
            Prepare(area);
        }

        void Prepare(const RECT& area)
        {
            g.SetSmoothingMode(Theme::Smoothing());
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

    inline RECT PlaceBeside(POINT mark, double sideX, double sideY, SIZE size, double gapPx)
    {
        const double kCentred = 0.35;
        const int ax = mark.x + (int)lround(sideX * gapPx);
        const int ay = mark.y + (int)lround(sideY * gapPx);
        const int left = sideX > kCentred ? ax : sideX < -kCentred ? ax - size.cx : ax - size.cx / 2;
        const int top = sideY > kCentred ? ay : sideY < -kCentred ? ay - size.cy : ay - size.cy / 2;
        RECT r = { left, top, left + size.cx, top + size.cy };
        return r;
    }

    inline bool UnitToward(POINT from, POINT to, double& ux, double& uy)
    {
        const double dx = (double)to.x - from.x, dy = (double)to.y - from.y;
        const double len = sqrt(dx * dx + dy * dy);
        if (len < 1.0)
            return false;
        ux = dx / len;
        uy = dy / len;
        return true;
    }

    inline void FreeSideOf(const std::vector<POINT>& path, size_t k, double& sideX, double& sideY)
    {
        double sx = 0.0, sy = 0.0, ux = 0.0, uy = 0.0, dirX = 1.0, dirY = 0.0;
        if (k > 0 && UnitToward(path[k], path[k - 1], ux, uy))
        {
            sx += ux;
            sy += uy;
            dirX = -ux;
            dirY = -uy;
        }
        if (k + 1 < path.size() && UnitToward(path[k], path[k + 1], ux, uy))
        {
            sx += ux;
            sy += uy;
            dirX = ux;
            dirY = uy;
        }
        const double len = sqrt(sx * sx + sy * sy);
        const bool straight = k > 0 && k + 1 < path.size() && len < 0.3;
        if (straight || len < 1e-6)
        {
            sideX = -dirY;
            sideY = dirX;
            return;
        }
        sideX = -sx / len;
        sideY = -sy / len;
    }

    inline bool ClipLeaderToText(POINT from, POINT aim, const std::vector<RECT>& rows, double gapPx,
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

    inline void DrawGappedPolyline(VectorCanvas& canvas, const std::vector<POINT>& pts, double gapPx)
    {
        if (pts.size() < 2)
            return;
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
