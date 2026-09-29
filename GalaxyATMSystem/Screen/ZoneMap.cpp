#include "pch.h"
#include "Screen/ScreenCommon.h"

using namespace Galaxy;

bool CGalaxyATMSystemRadarScreen::ZoneOutline(const Zone& zone, std::vector<POINT>& out)
{
    return SigmetOutline(zone.ring, out);
}

void CGalaxyATMSystemRadarScreen::UpdateZoneActivity()
{
    const Config& cfg = Plugin()->GetConfig();
    const std::vector<Zone>& zones = cfg.Zones();

    const time_t now = time(NULL);
    auto aup = Plugin()->AupBookings();
    auto notams = Plugin()->Notams();
    if (now == m_zoneActivityAt && aup == m_aup && notams == m_notams && m_zoneActive.size() == zones.size())
        return;
    m_zoneActivityAt = now;

    m_aup = aup;
    m_notams = notams;
    m_zoneActive.assign(zones.size(), 0);
    m_zoneBooking.assign(zones.size(), NULL);

    if (zones.empty())
        return;

    static const std::vector<ZoneBooking> kNoBookings;

    ZoneActivation what;
    what.aup = m_aup ? m_aup.get() : &kNoBookings;
    what.notams = m_notams ? m_notams.get() : NULL;
    what.showNotamWhenUnknown = cfg.ShowNotamAreas();

    for (size_t i = 0; i < zones.size(); i++)
    {
        const ZoneBooking* hit = NULL;
        m_zoneActive[i] = ZoneActiveNow(zones[i], what, now, &hit) ? 1 : 0;
        m_zoneBooking[i] = hit;
    }
}

void CGalaxyATMSystemRadarScreen::ZoneLayer::Release()
{
    if (dc != NULL && oldBitmap != NULL)
        SelectObject(dc, oldBitmap);
    if (bitmap != NULL)
        DeleteObject(bitmap);
    if (dc != NULL)
        DeleteDC(dc);
    dc = NULL;
    bitmap = NULL;
    oldBitmap = NULL;
    bits = NULL;
    width = height = 0;
    key.clear();
    drawn = { 0, 0, 0, 0 };
}

std::string CGalaxyATMSystemRadarScreen::ZoneLayerKey()
{
    const RECT ra = GetRadarArea();
    CPosition leftDown, rightUp;
    GetDisplayArea(&leftDown, &rightUp);
    const Config& cfg = Plugin()->GetConfig();
    const std::vector<Zone>& zones = cfg.Zones();
    char head[256];
    sprintf_s(head, "%ld,%ld,%ld,%ld|%.7f,%.7f,%.7f,%.7f|%d|%p,%zu|",
        ra.left, ra.top, ra.right, ra.bottom, leftDown.m_Latitude, leftDown.m_Longitude,
        rightUp.m_Latitude, rightUp.m_Longitude, Theme::AntiAliasOn() ? 1 : 0,
        (const void*)zones.data(), zones.size());
    std::string key = head;
    for (ZoneKind kind : { ZoneKind::Prohibited, ZoneKind::Restricted, ZoneKind::Danger })
    {
        const ZoneStyle& s = cfg.ZoneStyleFor(kind);
        char style[48];
        sprintf_s(style, "%lu,%lu,%d;", (unsigned long)s.line, (unsigned long)s.fill, (int)s.alpha);
        key += style;
    }
    key += '|';
    for (char active : m_zoneActive)
        key += active ? '1' : '0';
    return key;
}

void CGalaxyATMSystemRadarScreen::RenderZoneLayer(HDC hDC)
{
    ZoneLayer& layer = m_zoneLayer;
    const RECT ra = GetRadarArea();
    const int width = ra.right, height = ra.bottom;
    layer.drawn = { 0, 0, 0, 0 };
    if (width <= 0 || height <= 0)
        return;

    if (layer.dc == NULL || layer.width != width || layer.height != height)
    {
        layer.Release();
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = width;
        bi.bmiHeader.biHeight = -height;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        layer.dc = CreateCompatibleDC(hDC);
        layer.bitmap = layer.dc != NULL
            ? CreateDIBSection(hDC, &bi, DIB_RGB_COLORS, &layer.bits, NULL, 0) : NULL;
        if (layer.bitmap == NULL || layer.bits == NULL)
        {
            layer.Release();
            return;
        }
        layer.oldBitmap = SelectObject(layer.dc, layer.bitmap);
        layer.width = width;
        layer.height = height;
    }
    memset(layer.bits, 0, (size_t)width * height * 4);

    const std::vector<Zone>& zones = Plugin()->GetConfig().Zones();
    const Config& cfg = Plugin()->GetConfig();
    const ZoneStyle& styleP = cfg.ZoneStyleFor(ZoneKind::Prohibited);
    const ZoneStyle& styleR = cfg.ZoneStyleFor(ZoneKind::Restricted);
    const ZoneStyle& styleD = cfg.ZoneStyleFor(ZoneKind::Danger);

    RECT drawn = { LONG_MAX, LONG_MAX, LONG_MIN, LONG_MIN };
    {
        Gdiplus::Bitmap surface(width, height, width * 4, PixelFormat32bppPARGB, (BYTE*)layer.bits);
        AreaCanvas canvas(&surface, ra, styleR.line, (float)Theme::ZoneWidth);
        std::vector<POINT> pts;

        for (size_t i = 0; i < zones.size(); i++)
        {
            if (i >= m_zoneActive.size() || !m_zoneActive[i])
                continue;

            const Zone& zone = zones[i];
            if (!ZoneOutline(zone, pts))
                continue;

            const ZoneStyle& style = (zone.kind == ZoneKind::Prohibited) ? styleP
                : (zone.kind == ZoneKind::Danger) ? styleD : styleR;

            canvas.Wash(pts, style.fill, style.alpha);
            canvas.SetColor(style.line);
            canvas.Ring(pts, true);

            for (const POINT& p : pts)
            {
                drawn.left = min(drawn.left, p.x);
                drawn.top = min(drawn.top, p.y);
                drawn.right = max(drawn.right, p.x);
                drawn.bottom = max(drawn.bottom, p.y);
            }
        }
    }
    if (drawn.right < drawn.left)
        return;
    InflateRect(&drawn, (int)Theme::ZoneWidth + 2, (int)Theme::ZoneWidth + 2);
    IntersectRect(&layer.drawn, &drawn, &ra);
}

void CGalaxyATMSystemRadarScreen::DrawZones(HDC hDC)
{
    if (!m_zonesVisible)
        return;

    const std::vector<Zone>& zones = Plugin()->GetConfig().Zones();
    if (zones.empty())
        return;

    const std::string key = ZoneLayerKey();
    if (key != m_zoneLayer.key || m_zoneLayer.dc == NULL)
    {
        RenderZoneLayer(hDC);
        m_zoneLayer.key = m_zoneLayer.dc != NULL ? key : std::string();
    }

    const RECT& r = m_zoneLayer.drawn;
    if (m_zoneLayer.dc == NULL || r.right <= r.left || r.bottom <= r.top)
        return;
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    AlphaBlend(hDC, r.left, r.top, r.right - r.left, r.bottom - r.top,
        m_zoneLayer.dc, r.left, r.top, r.right - r.left, r.bottom - r.top, blend);
}

void CGalaxyATMSystemRadarScreen::RegisterZoneObjects()
{
    if (!m_zonesVisible)
        return;

    const std::vector<Zone>& zones = Plugin()->GetConfig().Zones();
    if (zones.empty())
        return;

    RECT ra = GetRadarArea();
    if (ra.right <= ra.left || ra.bottom <= ra.top)
        return;

    const int kCellPx = 24;
    const int kMaxCellPx = 96;
    const int kMaxBoxes = 3000;

    struct Outline
    {
        size_t index;
        std::vector<POINT> pts;
        double area;
    };
    std::vector<Outline> visible;

    std::vector<POINT> pts;
    for (size_t i = 0; i < zones.size(); i++)
    {
        if (i >= m_zoneActive.size() || !m_zoneActive[i])
            continue;
        if (!ZoneOutline(zones[i], pts))
            continue;

        bool onScreen = false;
        for (size_t seg = 0; seg < pts.size() && !onScreen; seg++)
        {
            POINT a = pts[seg], b = pts[(seg + 1) % pts.size()];
            onScreen = Geom::ClipSegment(ra, a, b);
        }
        if (!onScreen)
        {
            POINT mid = { (ra.left + ra.right) / 2, (ra.top + ra.bottom) / 2 };
            onScreen = Geom::PointInPolygon(pts, mid);
        }
        if (!onScreen)
            continue;

        Outline o;
        o.index = i;
        o.pts = pts;

        double twice = 0.0;
        for (size_t seg = 0; seg < pts.size(); seg++)
        {
            const POINT& a = pts[seg];
            const POINT& b = pts[(seg + 1) % pts.size()];
            twice += (double)a.x * b.y - (double)b.x * a.y;
        }
        o.area = fabs(twice) * 0.5;

        visible.push_back(std::move(o));
    }

    if (visible.empty())
        return;

    struct Square
    {
        int    index;
        int    rank;
        double score;
    };

    std::vector<Square> grid;

    int cell = kCellPx;
    int cols = 1, rows = 1, baseCX = 0, baseCY = 0;
    int claimed = 0;

    for (;;)
    {
        baseCX = (int)floor((double)ra.left / cell);
        baseCY = (int)floor((double)ra.top / cell);
        cols = max(1, (int)floor((double)(ra.right - 1) / cell) - baseCX + 1);
        rows = max(1, (int)floor((double)(ra.bottom - 1) / cell) - baseCY + 1);

        Square unclaimed = { -1, 0, 0.0 };
        grid.assign((size_t)cols * rows, unclaimed);
        claimed = 0;

        auto claim = [&](int cx, int cy, int index, int rank, double score)
            {
                cx -= baseCX;
                cy -= baseCY;
                if (cx < 0 || cy < 0 || cx >= cols || cy >= rows)
                    return;
                Square& sq = grid[(size_t)cy * cols + cx];
                if (sq.index < 0)
                {
                    claimed++;
                }
                else if (!(rank < sq.rank || (rank == sq.rank && score < sq.score)))
                {
                    return;
                }
                sq.index = index;
                sq.rank = rank;
                sq.score = score;
            };

        std::vector<double> xs;
        for (const Outline& o : visible)
        {
            RECT bbox = { LONG_MAX, LONG_MAX, LONG_MIN, LONG_MIN };
            for (const POINT& p : o.pts)
            {
                bbox.left = min(bbox.left, p.x);
                bbox.top = min(bbox.top, p.y);
                bbox.right = max(bbox.right, p.x);
                bbox.bottom = max(bbox.bottom, p.y);
            }

            const int cy0 = max(baseCY, (int)floor((double)max(bbox.top, ra.top) / cell));
            const int cy1 = min(baseCY + rows - 1,
                (int)floor((double)min(bbox.bottom, ra.bottom - 1) / cell));

            for (int cy = cy0; cy <= cy1; cy++)
            {
                const double y = cy * (double)cell + cell / 2.0;

                xs.clear();
                for (size_t seg = 0; seg < o.pts.size(); seg++)
                {
                    const POINT& a = o.pts[seg];
                    const POINT& b = o.pts[(seg + 1) % o.pts.size()];
                    if ((a.y <= y) == (b.y <= y))
                        continue;
                    const double t = (y - a.y) / (double)(b.y - a.y);
                    xs.push_back(a.x + t * (b.x - a.x));
                }
                if (xs.size() < 2)
                    continue;
                std::sort(xs.begin(), xs.end());

                for (size_t k = 0; k + 1 < xs.size(); k += 2)
                {
                    const double x0 = max(xs[k], (double)ra.left);
                    const double x1 = min(xs[k + 1], (double)(ra.right - 1));
                    if (x1 < x0)
                        continue;

                    const int cx0 = (int)floor(x0 / cell);
                    const int cx1 = (int)floor(x1 / cell);
                    for (int cx = cx0; cx <= cx1; cx++)
                        claim(cx, cy, (int)o.index, 1, o.area);
                }
            }
        }

        const double step = cell / 2.0;

        for (const Outline& o : visible)
        {
            for (size_t seg = 0; seg < o.pts.size(); seg++)
            {
                POINT a = o.pts[seg], b = o.pts[(seg + 1) % o.pts.size()];
                if (!Geom::ClipSegment(ra, a, b))
                    continue;

                const double len = sqrt((double)(b.x - a.x) * (b.x - a.x) + (double)(b.y - a.y) * (b.y - a.y));
                const int steps = (int)floor(len / step);

                for (int st = 0; st <= steps; st++)
                {
                    const double f = (len > 0.0) ? (st * step) / len : 0.0;
                    const double px = a.x + (b.x - a.x) * f;
                    const double py = a.y + (b.y - a.y) * f;

                    const int cx = (int)floor(px / cell);
                    const int cy = (int)floor(py / cell);
                    const double mx = cx * (double)cell + cell / 2.0;
                    const double my = cy * (double)cell + cell / 2.0;
                    const double d2 = (px - mx) * (px - mx) + (py - my) * (py - my);

                    claim(cx, cy, (int)o.index, 0, d2);
                }
            }
        }

        if (claimed <= kMaxBoxes || cell >= kMaxCellPx)
            break;
        cell *= 2;
    }

    std::map<size_t, std::string> tips;
    for (const Outline& o : visible)
        tips[o.index] = Narrow(zones[o.index].Title().substr(0, 120));

    for (int cy = 0; cy < rows; cy++)
    {
        for (int cx = 0; cx < cols; cx++)
        {
            const Square& sq = grid[(size_t)cy * cols + cx];
            if (sq.index < 0)
                continue;

            const int gx = (baseCX + cx) * cell;
            const int gy = (baseCY + cy) * cell;
            RECT box = { gx, gy, gx + cell, gy + cell };

            char id[16];
            sprintf_s(id, "%d", sq.index);
            AddScreenObject(SO_ZONE_AREA, id, box, false, tips[(size_t)sq.index].c_str());
        }
    }
}

int CGalaxyATMSystemRadarScreen::ZoneFromObjectId(const char* sObjectId)
{
    if (sObjectId == NULL || *sObjectId == '\0')
        return -1;

    char* end = NULL;
    long idx = strtol(sObjectId, &end, 10);
    if (end == sObjectId || idx < 0)
        return -1;

    if ((size_t)idx >= Plugin()->GetConfig().Zones().size())
        return -1;
    if ((size_t)idx >= m_zoneActive.size() || !m_zoneActive[idx])
        return -1;

    return (int)idx;
}

int CGalaxyATMSystemRadarScreen::FindZoneAt(POINT pt)
{
    const std::vector<Zone>& zones = Plugin()->GetConfig().Zones();

    int best = -1;
    double bestDist = 18.0;

    int bestInside = -1;
    double bestInsideArea = 0.0;
    std::vector<POINT> pts;

    for (size_t i = 0; i < zones.size(); i++)
    {
        if (i >= m_zoneActive.size() || !m_zoneActive[i])
            continue;
        if (!ZoneOutline(zones[i], pts))
            continue;

        if (Geom::PointInPolygon(pts, pt))
        {
            double twice = 0.0;
            for (size_t seg = 0; seg < pts.size(); seg++)
            {
                const POINT& a = pts[seg];
                const POINT& b = pts[(seg + 1) % pts.size()];
                twice += (double)a.x * b.y - (double)b.x * a.y;
            }
            const double area = fabs(twice) * 0.5;
            if (bestInside < 0 || area < bestInsideArea)
            {
                bestInside = (int)i;
                bestInsideArea = area;
            }
            continue;
        }

        for (size_t seg = 0; seg < pts.size(); seg++)
        {
            double d = Geom::DistanceToSegment(pts[seg], pts[(seg + 1) % pts.size()], pt);
            if (d < bestDist)
            {
                bestDist = d;
                best = (int)i;
            }
        }
    }
    return (bestInside >= 0) ? bestInside : best;
}

void CGalaxyATMSystemRadarScreen::DrawZoneInfo(HDC hDC)
{
    const std::vector<Zone>& zones = Plugin()->GetConfig().Zones();
    if (m_zoneInfoIndex < 0 || (size_t)m_zoneInfoIndex >= zones.size())
    {
        m_zoneInfoIndex = -1;
        return;
    }

    if ((size_t)m_zoneInfoIndex >= m_zoneActive.size() || !m_zoneActive[m_zoneInfoIndex])
    {
        m_zoneInfoIndex = -1;
        return;
    }

    const Zone& zone = zones[m_zoneInfoIndex];

    std::wstring text = zone.id.empty() ? zone.name : zone.id;

    const ZoneBooking* booking = ((size_t)m_zoneInfoIndex < m_zoneBooking.size())
        ? m_zoneBooking[m_zoneInfoIndex] : NULL;

    if (booking != NULL)
    {
        auto stamp = [](time_t t) -> std::wstring
        {
            tm utc = {};
            if (gmtime_s(&utc, &t) != 0)
                return L"--:-- ----------";
            wchar_t buf[24];
            swprintf_s(buf, L"%02d:%02d %02d-%02d-%04d", utc.tm_hour, utc.tm_min,
                utc.tm_mday, utc.tm_mon + 1, utc.tm_year + 1900);
            return buf;
        };
        text += L"\n" + stamp(booking->start);
        text += L"\n" + stamp(booking->end);
    }

    std::wstring levels = (booking != NULL)
        ? ZoneLevelText(booking->minFL) + L"-" + ZoneLevelText(booking->maxFL)
        : zone.LevelBand();
    if (!levels.empty())
        text += L"\n" + levels;

    if (!zone.note.empty())
        text += L"\n" + zone.note;

    const int kPadX = 10, kPadY = 8;
    const int kMaxW = 360, kMinW = 130;

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
    box.left = m_zoneInfoAt.x + 14;
    box.top = m_zoneInfoAt.y + 14;
    if (box.left + w > ra.right)
        box.left = m_zoneInfoAt.x - 14 - w;
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
