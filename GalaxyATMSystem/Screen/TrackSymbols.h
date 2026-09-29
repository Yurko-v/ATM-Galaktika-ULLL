#pragma once

#include "Core/Common.h"

namespace Galaxy
{
    struct SymbolStep
    {
        enum Kind { Move, Line, Pixel, Arc } kind;
        int v[6];
    };
    typedef std::vector<SymbolStep> TrackSymbol;
    typedef std::map<std::string, TrackSymbol> TrackSymbolSet;

#define GALAXY_SSR_SYMBOL \
    "MOVETO:-1:0\nLINETO:0:1\nLINETO:1:0\nLINETO:0:-1\nLINETO:-1:0\n" \
    "MOVETO:-2:0\nLINETO:0:2\nLINETO:2:0\nLINETO:0:-2\nLINETO:-2:0\n" \
    "MOVETO:-3:0\nLINETO:0:3\nLINETO:3:0\nLINETO:0:-3\nLINETO:-3:0\n" \
    "MOVETO:-5:-5\nLINETO:5:-5\nLINETO:5:5\nLINETO:-5:5\nLINETO:-5:-5\n"
#define GALAXY_ADSB_SYMBOL \
    "MOVETO:-6:0\nLINETO:0:6\nLINETO:6:0\nLINETO:0:-6\nLINETO:-6:0\n"
    inline const char* const kDefaultTrackSymbols =
        "SYMBOL:PRIMARY\nMOVETO:0:-5\nLINETO:0:5\nMOVETO:-5:0\nLINETO:5:0\n"
        "SYMBOL:PRIMARY_DIV\nMOVETO:0:-4\nLINETO:0:5\nMOVETO:-4:0\nLINETO:5:0\n"
        "SYMBOL:DAPS\n" GALAXY_SSR_SYMBOL
        "SYMBOL:DAPS_DIV\n" GALAXY_SSR_SYMBOL
        "SYMBOL:NODAPS\n" GALAXY_SSR_SYMBOL
        "SYMBOL:NODAPS_DIV\n" GALAXY_SSR_SYMBOL
        "SYMBOL:ADSB\n" GALAXY_ADSB_SYMBOL
        "SYMBOL:ADSB_DIV\n" GALAXY_ADSB_SYMBOL
        "SYMBOL:UNCONTROLLED\nMOVETO:0:-5\nLINETO:0:5\nMOVETO:-5:0\nLINETO:5:0\n"
        "MOVETO:-5:-5\nLINETO:5:-5\nLINETO:5:5\nLINETO:-5:5\nLINETO:-5:-5\n"
        "SYMBOL:HISTORY\nMOVETO:-1:-1\nLINETO:-1:0\nLINETO:0:0\nLINETO:0:-1\nLINETO:-1:-1\n"
        "SYMBOL:ASSUMED\nMOVETO:0:-3\nLINETO:0:4\nMOVETO:-3:0\nLINETO:4:0\n"
        "MOVETO:-2:-2\nLINETO:3:3\nMOVETO:-2:2\nLINETO:3:-3\n";
#undef GALAXY_SSR_SYMBOL
#undef GALAXY_ADSB_SYMBOL

    inline const double kAssumedHoleRadius = 4.0;

    inline void ParseTrackSymbols(const std::string& text, TrackSymbolSet& out)
    {
        TrackSymbol* current = NULL;
        size_t start = 0;
        while (start < text.size())
        {
            size_t end = text.find('\n', start);
            if (end == std::string::npos)
                end = text.size();
            std::string line = text.substr(start, end - start);
            start = end + 1;

            size_t comment = line.find("//");
            if (comment != std::string::npos)
                line.erase(comment);

            std::vector<std::string> fields(1);
            for (char c : line)
            {
                if (c == ':')
                    fields.push_back(std::string());
                else if (c != ' ' && c != '\t' && c != '\r')
                    fields.back() += c;
            }
            if (fields[0].empty())
                continue;
            for (char& c : fields[0])
                if (c >= 'a' && c <= 'z')
                    c = (char)(c - 'a' + 'A');

            if (fields[0] == "SYMBOL")
            {
                current = NULL;
                if (fields.size() >= 2 && !fields[1].empty())
                {
                    std::string name = fields[1];
                    for (char& c : name)
                        if (c >= 'a' && c <= 'z')
                            c = (char)(c - 'a' + 'A');
                    current = &out[name];
                    current->clear();
                }
                continue;
            }
            if (current == NULL)
                continue;

            std::vector<int> n;
            for (size_t i = 1; i < fields.size(); i++)
                n.push_back(atoi(fields[i].c_str()));

            SymbolStep step = {};
            if ((fields[0] == "MOVETO" || fields[0] == "LINETO" || fields[0] == "SETPIXEL") && n.size() >= 2)
            {
                step.kind = fields[0] == "MOVETO" ? SymbolStep::Move
                          : fields[0] == "LINETO" ? SymbolStep::Line : SymbolStep::Pixel;
                step.v[0] = n[0];
                step.v[1] = n[1];
            }
            else if (fields[0] == "ARC" && n.size() == 5)
            {
                step.kind = SymbolStep::Arc;
                const int v[6] = { n[0], n[1], n[2], n[2], n[3], n[4] };
                memcpy(step.v, v, sizeof(v));
            }
            else if (fields[0] == "ARC" && n.size() >= 6)
            {
                step.kind = SymbolStep::Arc;
                for (int i = 0; i < 6; i++)
                    step.v[i] = n[i];
            }
            else
            {
                continue;
            }
            current->push_back(step);
        }
    }

    inline bool DrawsSomething(const TrackSymbol& symbol)
    {
        int x = 0, y = 0;
        for (const SymbolStep& s : symbol)
        {
            switch (s.kind)
            {
            case SymbolStep::Pixel:
                return true;
            case SymbolStep::Arc:
                if (s.v[2] > 0 && s.v[3] > 0)
                    return true;
                break;
            case SymbolStep::Line:
                if (s.v[0] != x || s.v[1] != y)
                    return true;
            case SymbolStep::Move:
                x = s.v[0];
                y = s.v[1];
                break;
            }
        }
        return false;
    }

    inline bool g_trackSymbolsLoaded = false;
    inline TrackSymbolSet g_trackSymbols;
    inline std::string g_trackSymbolsSource;
    inline std::set<std::string> g_trackSymbolsFromFile;

    inline void ResetTrackSymbols()
    {
        g_trackSymbolsLoaded = false;
        g_trackSymbols.clear();
        g_trackSymbolsSource.clear();
        g_trackSymbolsFromFile.clear();
    }

    inline const TrackSymbolSet& TrackSymbols()
    {
        if (g_trackSymbolsLoaded)
            return g_trackSymbols;
        g_trackSymbolsLoaded = true;
        ParseTrackSymbols(kDefaultTrackSymbols, g_trackSymbols);
        g_trackSymbolsSource = "TopSky.dll is not loaded - built-in symbols";

        HMODULE topsky = GetModuleHandleW(L"TopSky.dll");
        wchar_t path[MAX_PATH] = {};
        if (topsky == NULL || GetModuleFileNameW(topsky, path, MAX_PATH) == 0)
            return g_trackSymbols;
        std::wstring file = path;
        size_t slash = file.find_last_of(L"\\/");
        file = file.substr(0, slash == std::wstring::npos ? 0 : slash + 1) + L"TopSkySymbols.txt";

        FILE* f = NULL;
        if (_wfopen_s(&f, file.c_str(), L"rb") != 0 || f == NULL)
        {
            g_trackSymbolsSource = Narrow(file) + " cannot be read - built-in symbols";
            Log::Warn("symbols", Log::Utf8(file) + " cannot be read - the built-in track symbols are drawn");
            return g_trackSymbols;
        }
        std::string text;
        char buf[4096];
        size_t got;
        while ((got = fread(buf, 1, sizeof(buf), f)) > 0)
            text.append(buf, got);
        fclose(f);
        g_trackSymbolsSource = Narrow(file);

        TrackSymbolSet fromFile;
        ParseTrackSymbols(text, fromFile);
        for (const auto& s : fromFile)
        {
            if (!DrawsSomething(s.second))
                continue;
            g_trackSymbols[s.first] = s.second;
            g_trackSymbolsFromFile.insert(s.first);
        }
        return g_trackSymbols;
    }

    inline void DrawSymbolLineOutsideHole(HDC hDC, POINT at, POINT from, POINT to, double holeR)
    {
        const double dx = (double)to.x - from.x, dy = (double)to.y - from.y;
        const double a = dx * dx + dy * dy;

        double t0 = 0.0, t1 = 0.0;
        if (a < 1e-9)
        {
            if ((double)from.x * from.x + (double)from.y * from.y < holeR * holeR)
                return;
        }
        else
        {
            const double b = 2.0 * (from.x * dx + from.y * dy);
            const double c = (double)from.x * from.x + (double)from.y * from.y - holeR * holeR;
            const double disc = b * b - 4.0 * a * c;
            if (disc > 0.0)
            {
                const double root = sqrt(disc);
                t0 = max(0.0, (-b - root) / (2.0 * a));
                t1 = min(1.0, (-b + root) / (2.0 * a));
            }
        }

        if (t0 > 0.0)
        {
            MoveToEx(hDC, at.x + from.x, at.y + from.y, NULL);
            LineTo(hDC, at.x + from.x + (int)lround(dx * t0), at.y + from.y + (int)lround(dy * t0));
        }
        if (t1 < 1.0)
        {
            MoveToEx(hDC, at.x + from.x + (int)lround(dx * t1), at.y + from.y + (int)lround(dy * t1), NULL);
            LineTo(hDC, at.x + to.x, at.y + to.y);
        }
        MoveToEx(hDC, at.x + to.x, at.y + to.y, NULL);
    }

    inline void DrawTrackSymbol(HDC hDC, const TrackSymbol& symbol, POINT at, COLORREF color,
        double holeRadius = 0.0)
    {
        HPEN pen = CreatePen(PS_SOLID, 1, color);
        HGDIOBJ oldPen = SelectObject(hDC, pen);
        HGDIOBJ oldBrush = SelectObject(hDC, GetStockObject(NULL_BRUSH));
        MoveToEx(hDC, at.x, at.y, NULL);

        POINT cur = { 0, 0 };
        for (const SymbolStep& s : symbol)
        {
            const POINT here = { s.v[0], s.v[1] };
            const int x = at.x + here.x, y = at.y + here.y;
            switch (s.kind)
            {
            case SymbolStep::Move:
                MoveToEx(hDC, x, y, NULL);
                cur = here;
                break;
            case SymbolStep::Line:
                if (holeRadius > 0.0)
                    DrawSymbolLineOutsideHole(hDC, at, cur, here, holeRadius);
                else
                    LineTo(hDC, x, y);
                cur = here;
                break;
            case SymbolStep::Pixel:
                if (holeRadius <= 0.0
                    || (double)here.x * here.x + (double)here.y * here.y >= holeRadius * holeRadius)
                    SetPixel(hDC, x, y, color);
                break;
            case SymbolStep::Arc:
            {
                const int rx = s.v[2], ry = s.v[3];
                if (rx <= 0 || ry <= 0)
                    break;
                if (holeRadius > 0.0
                    && sqrt((double)here.x * here.x + (double)here.y * here.y) + max(rx, ry) <= holeRadius)
                    break;
                const double a0 = s.v[4] * M_PI / 180.0, a1 = s.v[5] * M_PI / 180.0;
                const int oldDir = SetArcDirection(hDC, AD_COUNTERCLOCKWISE);
                ::Arc(hDC, x - rx, y - ry, x + rx + 1, y + ry + 1,
                    x + (int)lround(rx * cos(a0)), y - (int)lround(ry * sin(a0)),
                    x + (int)lround(rx * cos(a1)), y - (int)lround(ry * sin(a1)));
                SetArcDirection(hDC, oldDir);
                break;
            }
            }
        }

        SelectObject(hDC, oldBrush);
        SelectObject(hDC, oldPen);
        DeleteObject(pen);
    }
}
