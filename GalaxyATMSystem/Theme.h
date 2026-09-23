#pragma once

#include <windows.h>
#include <string>
#include <objidl.h>
#include <gdiplus.h>

#include "resource.h"

extern HINSTANCE g_hModule;

namespace Theme
{
    const COLORREF Background   = RGB(0x21, 0x27, 0x1C);
    const COLORREF Border       = RGB(0xC4, 0xC4, 0xC4);

    const COLORREF BorderStrong = RGB(0xC4, 0xC4, 0xC4);
    const COLORREF BorderCheck  = RGB(0xFF, 0xFF, 0xFF);

    const COLORREF ControlFill  = RGB(0x15, 0x15, 0x15);
    const COLORREF InsetFill    = RGB(0x1E, 0x1E, 0x1E);

    const COLORREF Text         = RGB(0xFF, 0xFF, 0xFF);
    const COLORREF TextDim      = RGB(0xCF, 0xDA, 0xCF);

    const COLORREF Active       = RGB(0x86, 0x8E, 0x96);
    const COLORREF ActiveText   = RGB(0xFF, 0xFF, 0xFF);
    const COLORREF Disabled     = RGB(0x3A, 0x55, 0x35);

    const COLORREF ButtonLight  = RGB(0x69, 0x6F, 0x76);
    const COLORREF ButtonMid    = RGB(0x49, 0x4D, 0x52);

    const COLORREF SliderTrack  = RGB(0x16, 0x1A, 0x13);
    const COLORREF SliderFill   = RGB(0x71, 0xFF, 0xF3);
    const COLORREF SliderThumb  = RGB(0xD9, 0xD9, 0xD9);

    const COLORREF ModeSim      = Text;
    const COLORREF ModeOps      = Text;
    const COLORREF ModeSup      = Text;
    const COLORREF ModeOffline  = TextDim;

    const COLORREF DistressText  = RGB(0xFF, 0x3B, 0x30);
    const COLORREF DuplicateText = RGB(0xFF, 0xD6, 0x00);
    const COLORREF ReadyText     = RGB(0x00, 0xDC, 0x00);

    const COLORREF FormularSector = RGB(0x3E, 0x7F, 0xE0);
    const COLORREF FormularInbound = FormularSector;

    const COLORREF FormularMapp = RGB(0xFF, 0x8C, 0x00);

    const COLORREF FormularHighlight = RGB(0xD0, 0x60, 0x00);

    const COLORREF FormularWtc   = RGB(0xFF, 0x3B, 0x30);
    const COLORREF FormularVfr   = RGB(0xE8, 0x8E, 0x2C);
    const COLORREF FormularGreen = RGB(0x00, 0xDC, 0x00);

    const int TrackHistoryDots = 5;

    const COLORREF HeadingDragLine = RGB(0xE8, 0x8E, 0x2C);
    const COLORREF CflFrame      = Background;
    const COLORREF CflCellLine   = RGB(0xB4, 0xB4, 0xB4);
    const COLORREF CflHover      = RGB(0x8C, 0x8C, 0x8C);
    const COLORREF CflField      = RGB(0x8A, 0x8F, 0x8A);
    const COLORREF CflFieldText  = RGB(0xCC, 0xCC, 0xCC);
    const COLORREF CflButton     = RGB(0x8C, 0x94, 0x8C);
    const COLORREF CflThumb      = RGB(0xA0, 0xA0, 0xA0);

    const COLORREF SpdTitle     = RGB(0x6C, 0xC0, 0xE8);
    const COLORREF SpdBody      = Background;
    const COLORREF SpdSelected  = RGB(0xD2, 0xCE, 0xC4);
    const COLORREF SpdTabOn     = RGB(0x9C, 0x9C, 0x98);
    const COLORREF SpdButton    = RGB(0x8C, 0x8C, 0x88);
    const COLORREF SpdLine      = RGB(0xE6, 0xE6, 0xE6);
    const COLORREF SpdThumb     = RGB(0x9C, 0x9C, 0x9C);
    const COLORREF XfrSelected  = RGB(0x3A, 0x3A, 0x3A);
    const COLORREF XfrTitleTop  = RGB(0x9C, 0xD8, 0xF2);
    const COLORREF XfrOutline   = RGB(0x3C, 0x84, 0xAC);
    const COLORREF XfrButtonTop    = RGB(0xA8, 0xA8, 0xA4);
    const COLORREF XfrButtonBottom = RGB(0x7C, 0x7C, 0x78);

    const COLORREF HoverFill = RGB(0xE8, 0x8E, 0x2C);
    const BYTE     HoverAlpha = 150;

    const COLORREF FormularHoverTarget = RGB(0xE8, 0x8E, 0x2C);
    const COLORREF HeadingDragText = RGB(0xFF, 0xFF, 0xFF);

    const COLORREF AtisIndexText = RGB(0x9E, 0xFF, 0x3D);
    const COLORREF AtisStripFill = RGB(0x00, 0x00, 0x00);

    const COLORREF MenuBarFill      = Background;
    const COLORREF MenuText         = RGB(0xFF, 0xFF, 0xFF);
    const COLORREF MenuTextDisabled = RGB(0x9A, 0x9A, 0x9A);

    const COLORREF Link      = RGB(0x8C, 0xC8, 0xFF);
    const COLORREF LinkHover = RGB(0xC8, 0xE6, 0xFF);

    const COLORREF AuthGranted   = AtisIndexText;

    const COLORREF ListGround      = Background;
    const BYTE     ListGroundAlpha = 204;
    const COLORREF ListTitleFill = RGB(0x3C, 0x3C, 0x3C);
    const COLORREF ListTitleText = RGB(0xFF, 0xFF, 0xFF);
    const COLORREF ListPaneFill  = RGB(0x00, 0x00, 0x00);
    const COLORREF ListHeadRule  = RGB(0xD9, 0xD9, 0xD9);
    const COLORREF ListHeadFill  = RGB(0x3C, 0x3C, 0x3C);
    const COLORREF ListHeadText  = RGB(0xFF, 0xFF, 0xFF);
    const COLORREF ListRowEast   = RGB(0xF5, 0xE0, 0x87);
    const COLORREF ListRowWest   = RGB(0xC5, 0xE0, 0xF3);
    const COLORREF ListRowRule   = RGB(0x00, 0x00, 0x00);
    const COLORREF ListFieldFrame = RGB(0xFF, 0xFF, 0xFF);
    const COLORREF ListText      = RGB(0x00, 0x00, 0x00);
    const COLORREF ListCrdReq    = RGB(0xD8, 0x1B, 0x14);
    const COLORREF ListCrdOk     = RGB(0x0C, 0x9E, 0x2E);
    const COLORREF ListConflict  = RGB(0xD8, 0x1B, 0x14);

    const COLORREF Ruler        = RGB(0xE0, 0xC9, 0x9A);
    const float    RulerWidth   = 1.0f;

    const float    VectorWidth       = 1.7f;
    const double   VectorTickGap     = 3.0;

    const float    VectorHeadWidth   = VectorWidth;
    const double   VectorHeadLength  = 7.0;

    const float    WakeArcWidth     = 2.0f;
    const double   WakeArcRadiusPx    = 20.0;
    const double   WakeArcStepPx      = 10.0;
    const double   WakeArcSweepDeg     = 60.0;

    const COLORREF SigmetLine   = RGB(0x1C, 0x3C, 0xA0);
    const int      SigmetWidth  = 2;

    const COLORREF SigmetInfoBg   = RGB(0x00, 0x00, 0x00);
    const BYTE     SigmetInfoAlpha = 128;
    const COLORREF SigmetInfoText = RGB(0xFF, 0xFF, 0xFF);
    const COLORREF SigmetInfoEdge = RGB(0xFF, 0xFF, 0xFF);

    const COLORREF ZoneFillProhibited = RGB(0xD2, 0x46, 0x3C);
    const COLORREF ZoneLineProhibited = RGB(0x96, 0x2E, 0x28);
    const COLORREF ZoneFillDanger     = RGB(0xCE, 0x7C, 0x3C);
    const COLORREF ZoneLineDanger     = RGB(0x92, 0x56, 0x28);
    const COLORREF ZoneFillRestricted = RGB(0xD2, 0x46, 0x3C);
    const COLORREF ZoneLineRestricted = RGB(0x96, 0x2E, 0x28);
    const int      ZoneWidth = 1;

    const COLORREF SquawkSet      = RGB(0xFF, 0xFF, 0xFF);
    const COLORREF SquawkMismatch = RGB(0xFF, 0xD6, 0x00);
    const COLORREF SquawkNoModeC  = RGB(0xFF, 0x3B, 0x30);
    const COLORREF SquawkNone     = RGB(0x8E, 0x8E, 0x93);

    const COLORREF SquawkPending = RGB(0xFF, 0xD6, 0x00);
    const COLORREF SquawkError   = RGB(0xFF, 0x3B, 0x30);

    const COLORREF ApwInside    = RGB(0xFF, 0x3C, 0x3C);
    const COLORREF ApwPredicted = RGB(0xFF, 0xB4, 0x32);

    const BYTE ZoneAlphaProhibited = 77;
    const BYTE ZoneAlphaDanger     = 44;
    const BYTE ZoneAlphaRestricted = 26;

    const COLORREF WinBody      = RGB(0x33, 0x3D, 0x2E);
    const COLORREF WinFrame     = RGB(0xD4, 0xD8, 0xCE);
    const COLORREF WinTitleTop  = RGB(0x71, 0x71, 0x6D);
    const COLORREF WinTitleBot  = RGB(0x49, 0x49, 0x45);
    const COLORREF WinTitleText = RGB(0xFF, 0xFF, 0xFF);
    const COLORREF Paper        = RGB(0x9C, 0x9C, 0x98);
    const COLORREF PaperInk     = RGB(0xFF, 0xFF, 0xFF);
    const COLORREF ButtonFace   = RGB(0x8B, 0x8B, 0x87);
    const COLORREF ButtonText   = RGB(0xFF, 0xFF, 0xFF);
    const COLORREF ScrollTrough = RGB(0x08, 0x08, 0x08);
    const COLORREF ScrollThumb  = RGB(0x9C, 0x9C, 0x98);
    const COLORREF ScrollEdge   = RGB(0x5C, 0x5C, 0x58);

    inline bool FaceInstalled(const wchar_t* face)
    {
        HDC dc = GetDC(NULL);
        if (dc == NULL)
            return false;

        LOGFONTW lf = {};
        lf.lfCharSet = DEFAULT_CHARSET;
        wcscpy_s(lf.lfFaceName, face);
        bool found = false;
        EnumFontFamiliesExW(dc, &lf,
            [](const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM p) -> int
            {
                *(bool*)p = true;
                return 0;
            }, (LPARAM)&found, 0);
        ReleaseDC(NULL, dc);
        return found;
    }

    inline const wchar_t* FirstInstalled(const wchar_t* const* names, int count, int& state)
    {
        if (state < 0)
        {
            state = 0;
            for (int i = 0; i < count && state == 0; i++)
                if (FaceInstalled(names[i]))
                    state = i + 1;
        }
        return state > 0 ? names[state - 1] : NULL;
    }

    inline HANDLE& EuroScopeFontHandle()
    {
        static HANDLE h = NULL;
        return h;
    }

    inline const wchar_t* EuroScopeFace()
    {
        static const wchar_t* const kFace = L"EuroScope";
        static int state = -1;
        if (state < 0)
        {
            state = 0;
            HRSRC res = FindResourceW(g_hModule,
                MAKEINTRESOURCEW(IDR_FONT_EUROSCOPE), RT_RCDATA);
            if (res != NULL)
            {
                HGLOBAL blob = LoadResource(g_hModule, res);
                void* data = (blob != NULL) ? LockResource(blob) : NULL;
                DWORD size = SizeofResource(g_hModule, res);
                DWORD faces = 0;
                if (data != NULL && size > 0)
                    EuroScopeFontHandle() = AddFontMemResourceEx(data, size, NULL, &faces);
                if (EuroScopeFontHandle() != NULL && faces > 0)
                    state = 1;
            }
            if (state == 0 && FaceInstalled(kFace))
                state = 1;
        }
        return state > 0 ? kFace : NULL;
    }

    inline void ReleaseEuroScopeFace()
    {
        if (EuroScopeFontHandle() != NULL)
        {
            RemoveFontMemResourceEx(EuroScopeFontHandle());
            EuroScopeFontHandle() = NULL;
        }
    }

    inline const wchar_t* InterFace()
    {
        static const wchar_t* const kNames[] = { L"Inter", L"Inter Variable" };
        static int state = -1;
        return FirstInstalled(kNames, (int)(sizeof(kNames) / sizeof(kNames[0])), state);
    }

    inline const wchar_t* InterMediumFace()
    {
        static const wchar_t* const kNames[] = { L"Inter Medium" };
        static int state = -1;
        return FirstInstalled(kNames, (int)(sizeof(kNames) / sizeof(kNames[0])), state);
    }

    inline const wchar_t* InterSemiBoldFace()
    {
        static const wchar_t* const kNames[] = { L"Inter SemiBold", L"Inter Semi Bold" };
        static int state = -1;
        return FirstInstalled(kNames, (int)(sizeof(kNames) / sizeof(kNames[0])), state);
    }

    inline const wchar_t* InterBoldFace()
    {
        static const wchar_t* const kNames[] = { L"Inter Bold" };
        static int state = -1;
        return FirstInstalled(kNames, (int)(sizeof(kNames) / sizeof(kNames[0])), state);
    }

    inline bool InterInstalled()
    {
        return InterFace() != NULL || InterMediumFace() != NULL
            || InterSemiBoldFace() != NULL || InterBoldFace() != NULL;
    }

    inline HFONT ListFontIn(int px, const wchar_t* face, int weight)
    {
        return CreateFontW(-px, 0, 0, 0, weight, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, face != NULL ? face : L"Arial");
    }

    enum ListWeight { ListRegular, ListMedium, ListBold };

    inline HFONT ListFont(int px, ListWeight weight)
    {
        switch (weight)
        {
        case ListMedium:
            if (InterMediumFace() != NULL)
                return ListFontIn(px, InterMediumFace(), FW_NORMAL);
            if (InterFace() != NULL)
                return ListFontIn(px, InterFace(), FW_MEDIUM);
            return ListFontIn(px, InterSemiBoldFace(), FW_NORMAL);

        case ListBold:
            if (InterBoldFace() != NULL)
                return ListFontIn(px, InterBoldFace(), FW_NORMAL);
            if (InterSemiBoldFace() != NULL)
                return ListFontIn(px, InterSemiBoldFace(), FW_BOLD);
            return ListFontIn(px, InterFace(), FW_BOLD);

        default:
            return ListFontIn(px, InterFace(), FW_NORMAL);
        }
    }

    struct FontSet
    {
        HFONT Body   = NULL;
        HFONT Clock  = NULL;
        HFONT Small  = NULL;
        HFONT Tiny   = NULL;
        HFONT Large  = NULL;
        HFONT Ruler  = NULL;
        HFONT Mono   = NULL;
        HFONT MonoBig= NULL;
        HFONT MonoHuge = NULL;
        HFONT WinTitle = NULL;
        HFONT WinTitleSmall = NULL;
        HFONT Menu    = NULL;

        void EnsureCreated()
        {
            if (Body != NULL)
                return;
            const wchar_t* face = L"Arial";
            auto mk = [&](int h, int weight)
            {
                return CreateFontW(h, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, face);
            };
            Body  = mk(-14, FW_NORMAL);
            Clock = mk(-20, FW_BOLD);
            Small = mk(-12, FW_NORMAL);
            Tiny  = mk(-11, FW_NORMAL);
            Large = mk(-19, FW_SEMIBOLD);
            Ruler = mk(-13, FW_SEMIBOLD);
            WinTitle = mk(-15, FW_BOLD);
            WinTitleSmall = mk(-12, FW_BOLD);
            Menu    = mk(-15, FW_NORMAL);

            auto mkMono = [](int h)
            {
                return CreateFontW(h, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
            };
            Mono     = mkMono(-15);
            MonoBig  = mkMono(-19);
            MonoHuge = mkMono(-30);
        }

        void Destroy()
        {
            for (HFONT* f : { &Body, &Clock, &Small, &Tiny, &Large, &Ruler,
                              &Mono, &MonoBig, &MonoHuge, &WinTitle, &WinTitleSmall,
                              &Menu })
            {
                if (*f) { DeleteObject(*f); *f = NULL; }
            }
        }
    };

    const int CornerRadius = 4;
    const int PanelCornerRadius = 10;

    enum Corners { CornersAll = 0xF, CornersNone = 0x0 };
    enum Corner { CornerTopLeft = 0x1, CornerTopRight = 0x2, CornerBottomRight = 0x4, CornerBottomLeft = 0x8 };

    inline Gdiplus::Color GdiColor(COLORREF c)
    {
        return Gdiplus::Color(GetRValue(c), GetGValue(c), GetBValue(c));
    }

    class SmoothCanvas
    {
    public:
        ~SmoothCanvas() { Release(); }

        HDC Fit(int width, int height)
        {
            if (m_dc != NULL && width <= m_width && height <= m_height)
                return m_dc;
            const int w = max(width, m_width), h = max(height, m_height);
            Release();
            if (w <= 0 || h <= 0 || w > MaxSide || h > MaxSide)
                return NULL;

            BITMAPINFO bi = {};
            bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
            bi.bmiHeader.biWidth = w;
            bi.bmiHeader.biHeight = -h;
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            void* bits = NULL;
            HDC screen = GetDC(NULL);
            m_dc = CreateCompatibleDC(screen);
            m_bitmap = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
            ReleaseDC(NULL, screen);
            if (m_dc == NULL || m_bitmap == NULL)
            {
                Release();
                return NULL;
            }
            m_old = SelectObject(m_dc, m_bitmap);
            m_graphics = new Gdiplus::Graphics(m_dc);
            m_graphics->SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            m_width = w;
            m_height = h;
            return m_dc;
        }

        Gdiplus::Graphics* Graphics() const { return m_graphics; }

        void Release()
        {
            delete m_graphics;
            m_graphics = NULL;
            if (m_dc != NULL && m_old != NULL)
                SelectObject(m_dc, m_old);
            if (m_bitmap != NULL)
                DeleteObject(m_bitmap);
            if (m_dc != NULL)
                DeleteDC(m_dc);
            m_dc = NULL;
            m_bitmap = NULL;
            m_old = NULL;
            m_width = m_height = 0;
        }

    private:
        static const int MaxSide = 8192;

        HDC m_dc = NULL;
        HBITMAP m_bitmap = NULL;
        HGDIOBJ m_old = NULL;
        Gdiplus::Graphics* m_graphics = NULL;
        int m_width = 0, m_height = 0;
    };

    inline SmoothCanvas& SharedCanvas()
    {
        static SmoothCanvas canvas;
        return canvas;
    }

    inline void DrawRoundedCorners(Gdiplus::Graphics& g, const RECT& r, const COLORREF* fill,
        const COLORREF* stroke, int rad, int bw, const bool round[4])
    {
        const Gdiplus::REAL fd = (Gdiplus::REAL)(2 * rad);
        const Gdiplus::REAL fl = r.left - 0.5f, ft = r.top - 0.5f;
        const Gdiplus::REAL fr = r.right - 0.5f - fd, fb = r.bottom - 0.5f - fd;
        const Gdiplus::REAL inset = (bw - 1) / 2.0f;
        const Gdiplus::REAL sd = (Gdiplus::REAL)(2 * rad - bw);
        const Gdiplus::REAL sl = r.left + inset, st = r.top + inset;
        const Gdiplus::REAL sr = r.right - 1 - inset - sd, sb = r.bottom - 1 - inset - sd;
        const Gdiplus::PointF fillAt[4] = { { fl, ft }, { fr, ft }, { fr, fb }, { fl, fb } };
        const Gdiplus::PointF strokeAt[4] = { { sl, st }, { sr, st }, { sr, sb }, { sl, sb } };
        const Gdiplus::REAL startAngle[4] = { 180, 270, 0, 90 };

        if (fill != NULL)
        {
            Gdiplus::GraphicsPath pies;
            for (int i = 0; i < 4; i++)
                if (round[i])
                    pies.AddPie(fillAt[i].X, fillAt[i].Y, fd, fd, startAngle[i], 90);
            Gdiplus::SolidBrush brush(GdiColor(*fill));
            g.FillPath(&brush, &pies);
        }
        if (bw > 0 && sd > 0)
        {
            Gdiplus::GraphicsPath arcs;
            for (int i = 0; i < 4; i++)
            {
                if (!round[i])
                    continue;
                arcs.StartFigure();
                arcs.AddArc(strokeAt[i].X, strokeAt[i].Y, sd, sd, startAngle[i], 90);
            }
            Gdiplus::Pen pen(GdiColor(*stroke), (Gdiplus::REAL)bw);
            g.DrawPath(&pen, &arcs);
        }
    }

    inline void DrawRoundedBox(HDC hDC, Gdiplus::Graphics* shared, const RECT& r, const COLORREF* fill,
        const COLORREF* stroke, int rad, int bw, const bool round[4])
    {
        const int tl = round[0] ? rad : 0, tr = round[1] ? rad : 0;
        const int br = round[2] ? rad : 0, bl = round[3] ? rad : 0;

        auto band = [hDC](HBRUSH brush, int left, int top, int right, int bottom)
        {
            RECT part = { left, top, right, bottom };
            if (right > left && bottom > top)
                FillRect(hDC, &part, brush);
        };

        if (fill != NULL)
        {
            HBRUSH brush = CreateSolidBrush(*fill);
            const int topH = max(tl, tr), bottomH = max(bl, br);
            band(brush, r.left + tl, r.top, r.right - tr, r.top + topH);
            band(brush, r.left, r.top + topH, r.right, r.bottom - bottomH);
            band(brush, r.left + bl, r.bottom - bottomH, r.right - br, r.bottom);
            DeleteObject(brush);
        }
        if (bw > 0)
        {
            HBRUSH brush = CreateSolidBrush(*stroke);
            band(brush, r.left + tl, r.top, r.right - tr, r.top + bw);
            band(brush, r.left + bl, r.bottom - bw, r.right - br, r.bottom);
            band(brush, r.left, r.top + tl, r.left + bw, r.bottom - bl);
            band(brush, r.right - bw, r.top + tr, r.right, r.bottom - br);
            DeleteObject(brush);
        }

        if (!round[0] && !round[1] && !round[2] && !round[3])
            return;

        if (shared != NULL)
        {
            DrawRoundedCorners(*shared, r, fill, stroke, rad, bw, round);
            shared->Flush(Gdiplus::FlushIntentionSync);
            return;
        }
        Gdiplus::Graphics g(hDC);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        DrawRoundedCorners(g, r, fill, stroke, rad, bw, round);
    }

    inline void SmoothBox(HDC hDC, const RECT& r, const COLORREF* fill, const COLORREF* stroke,
        int radius, int width = 1, int corners = CornersAll)
    {
        const int w = r.right - r.left, h = r.bottom - r.top;
        if (w <= 0 || h <= 0)
            return;

        const int rad = max(0, min(radius, min(w, h) / 2));
        const int bw = stroke != NULL ? max(0, width) : 0;
        const bool round[4] = {
            rad > 1 && (corners & CornerTopLeft) != 0,
            rad > 1 && (corners & CornerTopRight) != 0,
            rad > 1 && (corners & CornerBottomRight) != 0,
            rad > 1 && (corners & CornerBottomLeft) != 0,
        };

        HDC canvas = (round[0] || round[1] || round[2] || round[3]) && r.left >= 0 && r.top >= 0
            ? SharedCanvas().Fit(r.right, r.bottom) : NULL;
        if (canvas == NULL)
        {
            DrawRoundedBox(hDC, NULL, r, fill, stroke, rad, bw, round);
            return;
        }

        BitBlt(canvas, r.left, r.top, w, h, hDC, r.left, r.top, SRCCOPY);
        DrawRoundedBox(canvas, SharedCanvas().Graphics(), r, fill, stroke, rad, bw, round);
        BitBlt(hDC, r.left, r.top, w, h, canvas, r.left, r.top, SRCCOPY);
    }

    inline void OutlineBox(HDC hDC, const RECT& r, COLORREF fill, COLORREF stroke)
    {
        SmoothBox(hDC, r, &fill, &stroke, CornerRadius);
    }

    inline void FillBox(HDC hDC, const RECT& r, COLORREF fill)
    {
        OutlineBox(hDC, r, fill, fill);
    }

    inline void DrawLine(HDC hDC, const RECT& r, const std::wstring& text,
        HFONT font, COLORREF color, UINT format)
    {
        HFONT oldFont = (HFONT)SelectObject(hDC, font);
        SetTextColor(hDC, color);
        RECT rc = r;
        DrawTextW(hDC, text.c_str(), -1, &rc, format | DT_SINGLELINE);
        SelectObject(hDC, oldFont);
    }

    inline SIZE MeasureText(HDC hDC, HFONT font, const std::wstring& text)
    {
        HFONT old = (HFONT)SelectObject(hDC, font);
        SIZE sz{ 0, 0 };
        GetTextExtentPoint32W(hDC, text.c_str(), (int)text.size(), &sz);
        SelectObject(hDC, old);
        return sz;
    }

    inline void DrawControl(HDC hDC, const RECT& r, const std::wstring& text,
        HFONT font, UINT format = DT_CENTER | DT_VCENTER)
    {
        OutlineBox(hDC, r, ControlFill, BorderStrong);
        DrawLine(hDC, r, text, font, Text, format);
    }

    inline void DrawValueField(HDC hDC, const RECT& r, const std::wstring& text, HFONT font,
        bool outlined = true, UINT format = DT_CENTER | DT_VCENTER)
    {
        OutlineBox(hDC, r, Active, outlined ? BorderStrong : Active);
        RECT inner = r;
        if ((format & DT_CENTER) == 0)
            inner.left += 5;
        DrawLine(hDC, inner, text, font, ActiveText, format);
    }

    inline void DrawGhostControl(HDC hDC, const RECT& r, const std::wstring& text, HFONT font)
    {
        OutlineBox(hDC, r, Background, BorderStrong);
        DrawLine(hDC, r, text, font, Text, DT_CENTER | DT_VCENTER);
    }

    inline void DrawRadio(HDC hDC, const RECT& r, bool selected)
    {
        const COLORREF fill = selected ? Active : ControlFill;
        SmoothBox(hDC, r, &fill, &BorderStrong, (r.right - r.left) * 7 / 20);
    }

    const int WinCornerRadius = 8;

    inline HRGN WinRegion(const RECT& r)
    {
        return CreateRoundRectRgn(r.left, r.top, r.right, r.bottom,
            WinCornerRadius * 2, WinCornerRadius * 2);
    }

    inline void WinBorder(HDC hDC, const RECT& r, int width, COLORREF color)
    {
        SmoothBox(hDC, r, NULL, &color, WinCornerRadius, width);
    }

    inline void WinFill(HDC hDC, const RECT& r, COLORREF fill)
    {
        SmoothBox(hDC, r, &fill, NULL, WinCornerRadius);
    }

    inline void FlatFill(HDC hDC, const RECT& r, COLORREF fill)
    {
        HBRUSH br = CreateSolidBrush(fill);
        RECT rc = r;
        FillRect(hDC, &rc, br);
        DeleteObject(br);
    }

    inline void FlatFrame(HDC hDC, const RECT& r, int width, COLORREF color)
    {
        HBRUSH br = CreateSolidBrush(color);
        RECT top    = { r.left, r.top, r.right, r.top + width };
        RECT bottom = { r.left, r.bottom - width, r.right, r.bottom };
        RECT left   = { r.left, r.top, r.left + width, r.bottom };
        RECT right  = { r.right - width, r.top, r.right, r.bottom };
        for (RECT* side : { &top, &bottom, &left, &right })
            FillRect(hDC, side, br);
        DeleteObject(br);
    }

    inline void VGradient(HDC hDC, const RECT& r, COLORREF top, COLORREF bottom)
    {
        int h = r.bottom - r.top;
        if (h <= 0)
            return;

        for (int y = 0; y < h; y++)
        {
            int R = GetRValue(top) + (GetRValue(bottom) - GetRValue(top)) * y / h;
            int G = GetGValue(top) + (GetGValue(bottom) - GetGValue(top)) * y / h;
            int B = GetBValue(top) + (GetBValue(bottom) - GetBValue(top)) * y / h;
            RECT band = { r.left, r.top + y, r.right, r.top + y + 1 };
            FlatFill(hDC, band, RGB(R, G, B));
        }
    }
}
