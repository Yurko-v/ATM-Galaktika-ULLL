#include "pch.h"
#include "Screen/ScreenCommon.h"

using namespace Galaxy;

bool CGalaxyATMSystemRadarScreen::SigmetOutline(
    const std::vector<EuroScopePlugIn::CPosition>& ring, std::vector<POINT>& out)
{
    out.clear();
    if (ring.size() < 2)
        return false;

    out.reserve(ring.size());
    RECT bbox = { LONG_MAX, LONG_MAX, LONG_MIN, LONG_MIN };
    for (const EuroScopePlugIn::CPosition& p : ring)
    {
        POINT px = ConvertCoordFromPositionToPixel(p);
        out.push_back(px);
        bbox.left   = min(bbox.left, px.x);
        bbox.top    = min(bbox.top, px.y);
        bbox.right  = max(bbox.right, px.x);
        bbox.bottom = max(bbox.bottom, px.y);
    }

    InflateRect(&bbox, 1, 1);

    RECT ra = GetRadarArea();
    RECT unused;
    if (!IntersectRect(&unused, &bbox, &ra))
    {
        out.clear();
        return false;
    }
    return true;
}

void CGalaxyATMSystemRadarScreen::DrawSigmets(HDC hDC)
{
    if (!m_sigmetsVisible || !m_sigmets || m_sigmets->empty())
        return;

    int saved = SaveDC(hDC);

    {
        AreaCanvas canvas(hDC, GetRadarArea(), Theme::SigmetLine,
            (float)Theme::SigmetWidth);
        for (const Sigmet& sig : *m_sigmets)
        {
            std::vector<POINT> pts;
            for (const std::vector<EuroScopePlugIn::CPosition>& ring : sig.rings)
            {
                if (SigmetOutline(ring, pts))
                    canvas.Ring(pts, sig.closed);
            }
        }
    }

    RestoreDC(hDC, saved);
}

void CGalaxyATMSystemRadarScreen::RegisterSigmetObjects()
{
    if (!m_sigmetsVisible || !m_sigmets || m_sigmets->empty())
        return;

    RECT ra = GetRadarArea();
    const int kPad = 12;
    const double kStepPx = 24.0;

    const int kMaxBoxes = 3000;
    int boxes = 0;

    std::vector<POINT> pts;
    for (size_t i = 0; i < m_sigmets->size() && boxes < kMaxBoxes; i++)
    {
        const Sigmet& sig = (*m_sigmets)[i];

        char id[16];
        sprintf_s(id, "%zu", i);
        std::string tip = Narrow(sig.Title().substr(0, 120));

        for (const std::vector<EuroScopePlugIn::CPosition>& ring : sig.rings)
        {
            if (!SigmetOutline(ring, pts))
                continue;

            size_t segments = sig.closed ? pts.size() : pts.size() - 1;
            for (size_t seg = 0; seg < segments && boxes < kMaxBoxes; seg++)
            {
                POINT a = pts[seg], b = pts[(seg + 1) % pts.size()];
                if (!Geom::ClipSegment(ra, a, b))
                    continue;

                double len = sqrt((double)(b.x - a.x) * (b.x - a.x) + (double)(b.y - a.y) * (b.y - a.y));
                int steps = max(1, (int)lround(len / kStepPx));

                for (int st = 0; st <= steps && boxes < kMaxBoxes; st++)
                {
                    double t = (double)st / steps;
                    POINT p;
                    p.x = a.x + (LONG)lround((b.x - a.x) * t);
                    p.y = a.y + (LONG)lround((b.y - a.y) * t);
                    RECT box = { p.x - kPad, p.y - kPad, p.x + kPad, p.y + kPad };
                    AddScreenObject(SO_SIGMET_AREA, id, box, false, tip.c_str());
                    boxes++;
                }
            }
        }
    }
}

int CGalaxyATMSystemRadarScreen::FindSigmetAt(POINT pt)
{
    if (!m_sigmets)
        return -1;

    int best = -1;
    double bestDist = 12.0;
    std::vector<POINT> pts;

    for (size_t i = 0; i < m_sigmets->size(); i++)
    {
        const Sigmet& sig = (*m_sigmets)[i];
        for (const std::vector<EuroScopePlugIn::CPosition>& ring : sig.rings)
        {
            if (!SigmetOutline(ring, pts))
                continue;

            if (sig.closed && Geom::PointInPolygon(pts, pt))
                return (int)i;

            size_t segments = sig.closed ? pts.size() : pts.size() - 1;
            for (size_t seg = 0; seg < segments; seg++)
            {
                double d = Geom::DistanceToSegment(pts[seg], pts[(seg + 1) % pts.size()], pt);
                if (d < bestDist)
                {
                    bestDist = d;
                    best = (int)i;
                }
            }
        }
    }
    return best;
}

void CGalaxyATMSystemRadarScreen::CloseSigmetInfoIfButtonReleased()
{
    if (m_sigmetInfoIndex < 0 && m_zoneInfoIndex < 0)
        return;

    const bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

    const int kGraceTicks = 12;

    if (m_sigmetInfoIndex >= 0)
    {
        if (down)
        {
            m_sigmetInfoHeld = true;
        }
        else if (m_sigmetInfoHeld || ++m_sigmetInfoWait >= kGraceTicks)
        {
            m_sigmetInfoIndex = -1;
            RequestRefresh();
        }
    }

    if (m_zoneInfoIndex >= 0)
    {
        if (down)
        {
            m_zoneInfoHeld = true;
        }
        else if (m_zoneInfoHeld || ++m_zoneInfoWait >= kGraceTicks)
        {
            m_zoneInfoIndex = -1;
            RequestRefresh();
        }
    }
}

void CGalaxyATMSystemRadarScreen::DrawSigmetInfo(HDC hDC)
{
    if (!m_sigmets || m_sigmetInfoIndex < 0 || (size_t)m_sigmetInfoIndex >= m_sigmets->size())
    {
        m_sigmetInfoIndex = -1;
        return;
    }

    const Sigmet& sig = (*m_sigmets)[m_sigmetInfoIndex];

    const int kPadX = 10, kPadY = 8;
    const int kMaxW = 400, kMinW = 260;

    const std::wstring& text = sig.raw.empty() ? sig.Title() : sig.raw;

    int saved = SaveDC(hDC);
    SetBkMode(hDC, TRANSPARENT);

    HFONT oldFont = (HFONT)SelectObject(hDC, m_fonts.Mono);
    RECT calc = { 0, 0, kMaxW - 2 * kPadX, 0 };
    DrawTextW(hDC, text.c_str(), -1, &calc, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_CALCRECT);
    SelectObject(hDC, oldFont);

    int w = max(kMinW, min(kMaxW, (int)(calc.right - calc.left) + 2 * kPadX));
    int h = (int)(calc.bottom - calc.top) + 2 * kPadY;

    RECT ra = GetRadarArea();
    RECT box;
    box.left = m_sigmetInfoAt.x + 14;
    box.top = m_sigmetInfoAt.y + 14;
    if (box.left + w > ra.right)
        box.left = m_sigmetInfoAt.x - 14 - w;
    if (box.top + h > ra.bottom)
        box.top = ra.bottom - h;
    box.left = max(ra.left, box.left);
    box.top = max(ra.top, box.top);
    box.right = box.left + w;
    box.bottom = box.top + h;

    FillAlpha(hDC, box, Theme::SigmetInfoBg, Theme::SigmetInfoAlpha);

    {
        HPEN pen = CreatePen(PS_INSIDEFRAME, 1, Theme::SigmetInfoEdge);
        HPEN oldPen = (HPEN)SelectObject(hDC, pen);
        HBRUSH oldBr = (HBRUSH)SelectObject(hDC, GetStockObject(NULL_BRUSH));
        Rectangle(hDC, box.left, box.top, box.right, box.bottom);
        SelectObject(hDC, oldBr);
        SelectObject(hDC, oldPen);
        DeleteObject(pen);
    }

    RECT r = { box.left + kPadX, box.top + kPadY, box.right - kPadX, box.bottom - kPadY };
    oldFont = (HFONT)SelectObject(hDC, m_fonts.Mono);
    SetTextColor(hDC, Theme::SigmetInfoText);
    DrawTextW(hDC, text.c_str(), -1, &r, DT_LEFT | DT_TOP | DT_WORDBREAK);
    SelectObject(hDC, oldFont);

    RestoreDC(hDC, saved);
}
