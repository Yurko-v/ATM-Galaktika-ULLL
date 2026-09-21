#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vector>
#include <cmath>

namespace Geom
{
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
                    return false;
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

    inline bool ClipPolygon(const RECT& r, const std::vector<POINT>& poly,
        std::vector<POINT>& out)
    {
        out.clear();
        if (poly.size() < 3)
            return false;

        struct P { double x, y; };
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
