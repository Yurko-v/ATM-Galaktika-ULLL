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

    // The same trim for a whole ring rather than one edge: Sutherland-Hodgman
    // against the four sides of 'r', so what comes out is the part of the area
    // that is actually on the screen, with the same shape it had there.
    //
    // A filled area needs this and not ClipSegment: an edge trimmed on its own
    // says nothing about which side of it the inside was, and the fill has to
    // be handed a closed shape. Bounded coordinates are the other half of it -
    // an area can project hundreds of screens wide, and a rasteriser asked to
    // scan-convert that spends everything it has outside the display.
    //
    // Returns false when nothing of the ring is left.
    inline bool ClipPolygon(const RECT& r, const std::vector<POINT>& poly,
        std::vector<POINT>& out)
    {
        out.clear();
        if (poly.size() < 3)
            return false;

        struct P { double x, y; };
        // side 0 the left edge, 1 the right, 2 the top, 3 the bottom.
        struct Side
        {
            static bool In(const P& p, const RECT& r, int side)
            {
                switch (side)
                {
                case 0:  return p.x >= (double)r.left;
                case 1:  return p.x <= (double)r.right;
                case 2:  return p.y >= (double)r.top;
                default: return p.y <= (double)r.bottom;
                }
            }

            static P Cross(const P& a, const P& b, const RECT& r, int side)
            {
                // Only ever called with a and b on opposite sides of it, so
                // the difference below cannot be zero.
                if (side < 2)
                {
                    const double x = (side == 0) ? (double)r.left : (double)r.right;
                    const double t = (x - a.x) / (b.x - a.x);
                    return P{ x, a.y + t * (b.y - a.y) };
                }
                const double y = (side == 2) ? (double)r.top : (double)r.bottom;
                const double t = (y - a.y) / (b.y - a.y);
                return P{ a.x + t * (b.x - a.x), y };
            }
        };

        std::vector<P> in, work;
        in.reserve(poly.size() + 8);
        for (const POINT& p : poly)
            in.push_back(P{ (double)p.x, (double)p.y });

        for (int side = 0; side < 4 && !in.empty(); side++)
        {
            work.clear();
            for (size_t i = 0; i < in.size(); i++)
            {
                const P& cur = in[i];
                const P& prev = in[(i + in.size() - 1) % in.size()];
                const bool curIn = Side::In(cur, r, side);
                const bool prevIn = Side::In(prev, r, side);

                if (curIn)
                {
                    if (!prevIn)
                        work.push_back(Side::Cross(prev, cur, r, side));
                    work.push_back(cur);
                }
                else if (prevIn)
                {
                    work.push_back(Side::Cross(prev, cur, r, side));
                }
            }
            in.swap(work);
        }

        if (in.size() < 3)
            return false;

        out.reserve(in.size());
        for (const P& p : in)
        {
            POINT q = { (LONG)lround(p.x), (LONG)lround(p.y) };
            out.push_back(q);
        }
        return true;
    }
}
