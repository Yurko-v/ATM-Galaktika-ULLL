#pragma once

#include <windows.h>
#include <string>

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

    const COLORREF FormularSector = RGB(0x3E, 0x7F, 0xE0);

    const COLORREF FormularMapp = RGB(0xFF, 0x8C, 0x00);

    const COLORREF FormularHighlight = RGB(0xD0, 0x60, 0x00);

    const COLORREF FormularWtc   = RGB(0xFF, 0x3B, 0x30);
    const COLORREF FormularVfr   = RGB(0xE8, 0x8E, 0x2C);
    const COLORREF FormularGreen = RGB(0x00, 0xDC, 0x00);

    const int TrackHistoryDots = 5;

    const COLORREF HeadingDragLine = RGB(0xE8, 0x8E, 0x2C);
    // эшелонатор (CFL picker)
    const COLORREF CflFrame      = RGB(0x5C, 0x6A, 0x58);
    const COLORREF CflCellLine   = RGB(0xB4, 0xB4, 0xB4);
    const COLORREF CflHover      = RGB(0x8C, 0x8C, 0x8C);
    const COLORREF CflField      = RGB(0x8A, 0x8F, 0x8A);
    const COLORREF CflFieldText  = RGB(0xCC, 0xCC, 0xCC);
    const COLORREF CflButton     = RGB(0x8C, 0x94, 0x8C);
    const COLORREF CflThumb      = RGB(0xA0, 0xA0, 0xA0);

    // target symbol and vector while the РЦ label is hovered
    const COLORREF FormularHoverTarget = RGB(0xE8, 0x8E, 0x2C);
    const COLORREF HeadingDragText = RGB(0xFF, 0xFF, 0xFF);

    const COLORREF AtisIndexText = RGB(0x9E, 0xFF, 0x3D);

    const COLORREF MenuBarFill      = Background;
    const COLORREF MenuText         = RGB(0xFF, 0xFF, 0xFF);
    const COLORREF MenuTextDisabled = RGB(0x9A, 0x9A, 0x9A);

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
    const double   WakeArcRadius    = 20.0;   // px, target -> first arc
    const double   WakeArcStep      = 10.0;   // px, first -> second arc (J)
    const double   WakeArcSweep     = 60.0;   // degrees, centred behind

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

    const int CornerRadius = 2;

    inline void OutlineBox(HDC hDC, const RECT& r, COLORREF fill, COLORREF stroke)
    {
        HBRUSH br = CreateSolidBrush(fill);
        HBRUSH oldBr = (HBRUSH)SelectObject(hDC, br);
        HPEN pen = CreatePen(PS_SOLID, 1, stroke);
        HPEN oldPen = (HPEN)SelectObject(hDC, pen);
        RoundRect(hDC, r.left, r.top, r.right, r.bottom, CornerRadius * 2, CornerRadius * 2);
        SelectObject(hDC, oldPen);
        DeleteObject(pen);
        SelectObject(hDC, oldBr);
        DeleteObject(br);
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
        HBRUSH br = CreateSolidBrush(selected ? Active : ControlFill);
        HBRUSH oldBr = (HBRUSH)SelectObject(hDC, br);
        HPEN pen = CreatePen(PS_SOLID, 1, BorderStrong);
        HPEN oldPen = (HPEN)SelectObject(hDC, pen);
        int d = (r.right - r.left) * 7 / 10;
        RoundRect(hDC, r.left, r.top, r.right, r.bottom, d, d);
        SelectObject(hDC, oldPen);
        DeleteObject(pen);
        SelectObject(hDC, oldBr);
        DeleteObject(br);
    }

    const int WinCornerRadius = 8;

    inline HRGN WinRegion(const RECT& r)
    {
        return CreateRoundRectRgn(r.left, r.top, r.right, r.bottom,
            WinCornerRadius * 2, WinCornerRadius * 2);
    }

    inline void WinBorder(HDC hDC, const RECT& r, int width, COLORREF color)
    {
        HPEN pen = CreatePen(PS_INSIDEFRAME, width, color);
        HPEN oldPen = (HPEN)SelectObject(hDC, pen);
        HBRUSH oldBr = (HBRUSH)SelectObject(hDC, GetStockObject(NULL_BRUSH));
        RoundRect(hDC, r.left, r.top, r.right, r.bottom,
            WinCornerRadius * 2, WinCornerRadius * 2);
        SelectObject(hDC, oldBr);
        SelectObject(hDC, oldPen);
        DeleteObject(pen);
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
