#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vector>
#include <cmath>

// -----------------------------------------------------------------------------
// Plain screen-pixel geometry, kept apart from the drawing code so it can be
// exercised on its own - the clipping in particular only shows itself on a
// display zoomed far enough in that an area is many screens wide.
// -----------------------------------------------------------------------------
namespace Geom
{
    // Ray casting. On screen pixels rather than on coordinates: the projection
    // is close enough to affine over one polygon that testing the drawn shape
    // and testing the real one give the same answer, and the drawn shape is
    // the one the controller is aiming at.
    inline bool PointInPolygon(const std::vector<POINT>& poly, POINT pt)
    {
        if (poly.size() < 3)
            return false;

        bool inside = false;
        for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
        {
            if ((poly[i].y > pt.y) != (poly[j].y > pt.y))
            {
                double x = (double)(poly[j].x - poly[i].x) * (pt.y - poly[i].y)
                    / (double)(poly[j].y - poly[i].y) + poly[i].x;
                if (pt.x < x)
                    inside = !inside;
            }
        }
        return inside;
    }

    // Distance from a point to the segment a-b, in pixels.
    inline double DistanceToSegment(POINT a, POINT b, POINT pt)
    {
        double vx = (double)(b.x - a.x), vy = (double)(b.y - a.y);
        double wx = (double)(pt.x - a.x), wy = (double)(pt.y - a.y);
        double len2 = vx * vx + vy * vy;
        double t = (len2 > 1e-9) ? (wx * vx + wy * vy) / len2 : 0.0;
        t = max(0.0, min(1.0, t));
        double dx = wx - t * vx, dy = wy - t * vy;
        return sqrt(dx * dx + dy * dy);
    }

    // Trims a segment to a rectangle, returning false when none of it is
    // inside (Liang-Barsky). a and b are only written when it returns true.
    //
    // Every сигмет edge goes through this before it is drawn or sampled: an
    // area can be hundreds of times wider than a zoomed-in display, and a
    // vertex a thousand screens away projects to a coordinate GDI cannot draw
    // and that no sampling along the whole edge would ever land inside.
    inline bool ClipSegment(const RECT& r, POINT& a, POINT& b)
    {
        double x0 = a.x, y0 = a.y;
        double dx = (double)b.x - x0, dy = (double)b.y - y0;
        double t0 = 0.0, t1 = 1.0;

        const double p[4] = { -dx, dx, -dy, dy };
        const double q[4] = { x0 - r.left, r.right - x0, y0 - r.top, r.bottom - y0 };

        for (int i = 0; i < 4; i++)
        {
            if (fabs(p[i]) < 1e-9)
            {
                if (q[i] < 0.0)
                    return false;   // parallel to this edge, and outside it
                continue;
            }
            double t = q[i] / p[i];
            if (p[i] < 0.0)
                t0 = max(t0, t);
            else
                t1 = min(t1, t);
            if (t0 > t1)
                return false;
        }

        a.x = (LONG)lround(x0 + t0 * dx);
        a.y = (LONG)lround(y0 + t0 * dy);
        b.x = (LONG)lround(x0 + t1 * dx);
        b.y = (LONG)lround(y0 + t1 * dy);
        return true;
    }
}
