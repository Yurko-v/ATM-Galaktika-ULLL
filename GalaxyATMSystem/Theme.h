#pragma once

#include <windows.h>
#include <string>

#include "resource.h"

extern HINSTANCE g_hModule;   // this DLL, captured in dllmain.cpp

// -----------------------------------------------------------------------------
// Visual theme + small GDI helpers shared by every block of the control panel.
//
// Matches the "КСА УВД ГАЛАКТИКА" reference export (KSA UVD GALAKTIKA.svg):
// a dark olive card, white text and thin light-grey outlines throughout, with
// every plate barely rounded. An inactive control is a near-black plate inside
// that outline; anything "selected" - a ticked checkbox, a pressed button, a
// live readout such as QNH or the transition level - is a flat #868E96 block
// with white text on it instead.
//
// Colours are centralised here so the whole panel can be retuned in one place.
// -----------------------------------------------------------------------------
namespace Theme
{
    // Card
    const COLORREF Background   = RGB(0x21, 0x27, 0x1C);  // #21271C - the card itself
    const COLORREF Border       = RGB(0xC4, 0xC4, 0xC4);  // #C4C4C4 - group frames

    // Controls take the same outline as the frames in the reference; only the
    // square checkboxes of "Векторы" and "ОС" are drawn a step brighter.
    const COLORREF BorderStrong = RGB(0xC4, 0xC4, 0xC4);  // #C4C4C4 - buttons and fields
    const COLORREF BorderCheck  = RGB(0xFF, 0xFF, 0xFF);  // #FFFFFF - square checkboxes

    // Control interiors. Plates (buttons, entry fields, unticked checkboxes)
    // are near-black; a sunken list or a narrow inline input is a shade lighter.
    const COLORREF ControlFill  = RGB(0x15, 0x15, 0x15);  // #151515
    const COLORREF InsetFill    = RGB(0x1E, 0x1E, 0x1E);  // #1E1E1E

    // Text
    const COLORREF Text         = RGB(0xFF, 0xFF, 0xFF);
    const COLORREF TextDim      = RGB(0xCF, 0xDA, 0xCF);

    // Selected / live-value blocks: #868E96 with white text on top.
    const COLORREF Active       = RGB(0x86, 0x8E, 0x96);
    const COLORREF ActiveText   = RGB(0xFF, 0xFF, 0xFF);
    const COLORREF Disabled     = RGB(0x3A, 0x55, 0x35);

    // The two lighter greys the code block's "ВСЕ" and "БП" buttons carry.
    const COLORREF ButtonLight  = RGB(0x69, 0x6F, 0x76);  // #696F76
    const COLORREF ButtonMid    = RGB(0x49, 0x4D, 0x52);  // #494D52

    // Vertical brightness slider of the code block.
    const COLORREF SliderTrack  = RGB(0x16, 0x1A, 0x13);  // #161A13
    const COLORREF SliderFill   = RGB(0x71, 0xFF, 0xF3);  // #71FFF3
    const COLORREF SliderThumb  = RGB(0xD9, 0xD9, 0xD9);  // #D9D9D9

    // Mode label (БЛОК 1) - the reference prints it in the same white as the
    // date it sits next to, so only the offline state is dimmed.
    const COLORREF ModeSim      = Text;
    const COLORREF ModeOps      = Text;
    const COLORREF ModeSup      = Text;
    const COLORREF ModeOffline  = TextDim;

    // The code block's two live readouts. Both are alarms rather than
    // readings - they are empty whenever nothing is wrong - so they are read
    // by their colour before they are read as text: a distress squawk in
    // bright red, a code two aircraft are carrying at once in amber.
    const COLORREF DistressText  = RGB(0xFF, 0x3B, 0x30);
    const COLORREF DuplicateText = RGB(0xFF, 0xD6, 0x00);

    // The формуляр's sector indicator ("MC" beside the callsign), blue as the
    // ULLL wiki's picture of the РДЦ label has it.
    const COLORREF FormularSector = RGB(0x3E, 0x7F, 0xE0);

    // MAPP over the callsign - a missed approach, set from TopSky's menu.
    const COLORREF FormularMapp = RGB(0xFF, 0x8C, 0x00);

    // A callsign the controller has highlighted (middle click on the label).
    const COLORREF FormularHighlight = RGB(0xD0, 0x60, 0x00);   // dark orange

    // The approach and tower labels' own colours, as the ULLL wiki's pictures
    // of them have it: the wake category of a heavy or a super in red, V for a
    // VFR flight in orange, and in green the arrival runway and VCH's CTL flag -
    // which is VCH's own TAG_GREEN, the colour its tag item was drawn in.
    const COLORREF FormularWtc   = RGB(0xFF, 0x3B, 0x30);
    const COLORREF FormularVfr   = RGB(0xE8, 0x8E, 0x2C);
    const COLORREF FormularGreen = RGB(0x00, 0xDC, 0x00);

    // Traffic history behind a target: how many of its earlier positions are
    // drawn, in TopSky's HISTORY symbol and the target's own colour.
    const int TrackHistoryDots = 5;

    // A heading being pulled off the формуляр's AHDG: a dashed line in TopSky's
    // orange, and its readout in plain white.
    const COLORREF HeadingDragLine = RGB(0xE8, 0x8E, 0x2C);
    const COLORREF HeadingDragText = RGB(0xFF, 0xFF, 0xFF);

    // The index letter on the INDEX АТИС strip. The one lit thing on it: the
    // label beside it is ordinary white, the letter is the value the strip
    // exists to show, so it is picked out in lime the way the real system
    // lights it up.
    const COLORREF AtisIndexText = RGB(0x9E, 0xFF, 0x3D);

    // The menu bar across the top of the radar: the panel's own card, so the
    // bar and the panel read as one frame. What works on it is white, and
    // what does not yet - every menu item, and Bypass until a LOGIN has
    // failed - is grey.
    const COLORREF MenuBarFill      = Background;
    const COLORREF MenuText         = RGB(0xFF, 0xFF, 0xFF);
    const COLORREF MenuTextDisabled = RGB(0x9A, 0x9A, 0x9A);

    // Авторизация: the "Доступ разрешён" line at the end of the check, in the
    // same lime - the one thing on the block that says it went through.
    const COLORREF AuthGranted   = AtisIndexText;

    // "Список РЦ" - the sector list. Every colour here is "rc.svg"'s own: the
    // panel's olive ground at 80 % so the radar shows through round the
    // window, a grey title bar, two black panes each headed by a row of grey
    // plates standing on a light band, and rows whose ground says which way
    // the flight is going - yellow eastbound, blue westbound.
    // Values are black; only the coordination flag is coloured.
    const COLORREF ListGround      = Background;                // #21271C
    const BYTE     ListGroundAlpha = 204;                       // 80%
    const COLORREF ListTitleFill = RGB(0x3C, 0x3C, 0x3C);  // #3C3C3C - the title bar
    const COLORREF ListTitleText = RGB(0xFF, 0xFF, 0xFF);  // its caption and close mark
    const COLORREF ListPaneFill  = RGB(0x00, 0x00, 0x00);  // a pane, and the rule between two rows
    const COLORREF ListHeadRule  = RGB(0xD9, 0xD9, 0xD9);  // #D9D9D9 - the band the plates stand on
    const COLORREF ListHeadFill  = RGB(0x3C, 0x3C, 0x3C);  // #3C3C3C - a heading plate
    const COLORREF ListHeadText  = RGB(0xFF, 0xFF, 0xFF);
    const COLORREF ListRowEast   = RGB(0xF5, 0xE0, 0x87);  // #F5E087 - flying east
    const COLORREF ListRowWest   = RGB(0xC5, 0xE0, 0xF3);  // #C5E0F3 - flying west
    const COLORREF ListRowRule   = RGB(0x00, 0x00, 0x00);  // the rules across a row: CFL|Точка, Вход|Точка, ВыхЭш|ПВО
    const COLORREF ListFieldFrame = RGB(0xFF, 0xFF, 0xFF); // the frame of a filter field
    const COLORREF ListText      = RGB(0x00, 0x00, 0x00);
    const COLORREF ListCrdReq    = RGB(0xD8, 0x1B, 0x14);  // coordination asked for, not answered
    const COLORREF ListCrdOk     = RGB(0x0C, 0x9E, 0x2E);  // coordination agreed
    const COLORREF ListConflict  = RGB(0xD8, 0x1B, 0x14);  // КФ - a конфликтная ситуация

    // Ruler (distance/bearing/time measuring line drawn on the radar).
    const COLORREF Ruler        = RGB(0xE0, 0xC9, 0x9A);  // beige
    const float    RulerWidth   = 1.0f; // the line, its midpoint tick and the cursor (antialiased)

    // Вектор экстраполяции - the track vector and the plan-following line.
    // Drawn with GDI+ (antialiased) rather than a GDI pen, so a fractional
    // weight is available here too.
    const float    VectorWidth       = 1.7f;
    const double   VectorTickGap     = 3.0;    // px between one minute's tick and the next

    // Its chevron: the same weight as the line - they are one mark - and
    // short-winged so it stays compact.
    const float    VectorHeadWidth   = VectorWidth;
    const double   VectorHeadLength  = 7.0;    // wing length at most, px

    // Категория турбулентности - arcs behind the target, one for a heavy and
    // two for a super, opening across the reciprocal of the track so they
    // read as the wake the aircraft leaves. The vector's colour, a touch
    // heavier than it.
    //
    // Every distance grows only gently with the zoom (pixels per nautical
    // mile) and stops growing at a ceiling. The size that works is the one at
    // an area-control zoom (~10 px/NM); an approach controller zooms in a long
    // way further, and arcs that kept growing with the picture - even with a
    // square root of it - came out huge there. So a low power keeps a little
    // life in the wheel, the ceiling caps it, and the floor keeps them clear
    // of the symbol when zoomed right out.
    //
    // How far an arc stands off and how big it is are separate: the arc is a
    // piece of a smaller circle whose centre sits behind the target, so it can
    // stand well clear and still be short. Its size is a share of its
    // distance, so it keeps the same shape at every zoom.
    const float    WakeArcWidth     = 2.0f;
    const double   WakeArcZoomPower = 0.25;   // how much the wheel still grows them; 0 = fixed size
    const double   WakeArcDistScale = 12.4;   // target to the middle of the inner arc, x (px/NM)^power
    const double   WakeArcDistMin   = 10.0;   // ...but never less, px
    const double   WakeArcDistMax   = 22.0;   // ...and never more, px - no bigger than at area control
    const double   WakeArcSize      = 0.8;    // the arc's own radius, as a share of that
    const double   WakeArcStepScale = 3.1;    // out to a super's second arc, x (px/NM)^power
    const double   WakeArcStepMin   = 4.0;    // ...but never less, px
    const double   WakeArcStepMax   = 5.5;    // ...and never more, px
    const double   WakeArcSweep     = 90.0;   // degrees, centred behind

    // Сигметы. The area is an outline in dark blue with nothing behind it,
    // so the traffic and the map inside it stay fully readable. Pure #00008B
    // disappears against the radar's black, so the blue is lifted just enough
    // to read while staying unmistakably dark - retune here, in one place.
    const COLORREF SigmetLine   = RGB(0x1C, 0x3C, 0xA0);
    const int      SigmetWidth  = 2;    // pen width of that outline

    // Its info window - and the зона one, which shares the look: black at half
    // opacity with white text on it, so the radar underneath stays visible
    // through the report, and a white hairline round the edge.
    const COLORREF SigmetInfoBg   = RGB(0x00, 0x00, 0x00);
    const BYTE     SigmetInfoAlpha = 128;   // 50%
    const COLORREF SigmetInfoText = RGB(0xFF, 0xFF, 0xFF);
    const COLORREF SigmetInfoEdge = RGB(0xFF, 0xFF, 0xFF);   // same white as the text

    // Зоны запретов и ограничений. A wash inside a darker outline of the same
    // hue, so an area reads as a piece of airspace rather than as a boundary.
    //
    // Запретные и ограничительные зоны are the same red, and how densely each
    // is washed in is what tells them apart: a запретная зона is airspace to
    // stay out of and carries the heavy wash, an ограничительная one is
    // airspace with conditions on it and is barely tinted. Опасные зоны keep
    // their orange - they are neither, and hue is what says so.
    const COLORREF ZoneFillProhibited = RGB(0xD2, 0x46, 0x3C);
    const COLORREF ZoneLineProhibited = RGB(0x96, 0x2E, 0x28);
    const COLORREF ZoneFillDanger     = RGB(0xCE, 0x7C, 0x3C);
    const COLORREF ZoneLineDanger     = RGB(0x92, 0x56, 0x28);
    const COLORREF ZoneFillRestricted = RGB(0xD2, 0x46, 0x3C);
    const COLORREF ZoneLineRestricted = RGB(0x96, 0x2E, 0x28);
    const int      ZoneWidth = 1;    // a hairline: the wash says where the area is

    // The squawk column. Whether the transponder is really set up can only be
    // told from the colour of the code in the Departure list, so each of the
    // four colours a code takes means exactly one thing:
    //   white  - the assigned code is set, and the transponder is in mode C;
    //   yellow - the code set is not the one assigned (the formular shows the
    //            code that is set, in the same yellow - TAG_ITEM_SQUAWK_SET);
    //   red    - the assigned code is set, but the transponder is not in mode C;
    //   grey   - no code has been assigned.
    const COLORREF SquawkSet      = RGB(0xFF, 0xFF, 0xFF);
    const COLORREF SquawkMismatch = RGB(0xFF, 0xD6, 0x00);
    const COLORREF SquawkNoModeC  = RGB(0xFF, 0x3B, 0x30);
    const COLORREF SquawkNone     = RGB(0x8E, 0x8E, 0x93);

    // While the server is being asked, and when it said no. The column shows
    // words then ("...." / "ERR"), never a code, so these colours cannot be
    // read as one of the four above.
    const COLORREF SquawkPending = RGB(0xFF, 0xD6, 0x00);
    const COLORREF SquawkError   = RGB(0xFF, 0x3B, 0x30);

    // APW - the area proximity warning in the tag. Severity is carried by the
    // colour, which is the convention every system of this kind follows: red
    // for airspace the aircraft is already inside, amber for airspace it is
    // predicted to enter and can still be turned away from. Both are lifted
    // well clear of the зона outlines they are warning about, so the word in
    // the tag is never mistaken for part of the overlay.
    const COLORREF ApwInside    = RGB(0xFF, 0x3C, 0x3C);
    const COLORREF ApwPredicted = RGB(0xFF, 0xB4, 0x32);

    // Out of 255, and the map and the traffic inside an area have to stay as
    // readable as they are outside it - which is what holds the ограничительные
    // зоны down at a tenth: there are a hundred and eighty-eight of them in the
    // package and they overlap.
    const BYTE ZoneAlphaProhibited = 77;   // 30%
    const BYTE ZoneAlphaDanger     = 44;   // ~17%
    const BYTE ZoneAlphaRestricted = 26;   // 10%

    // АТИС window. Taken from the photograph of the real system: an
    // olive card inside a light two-pixel frame, a grey title bar shading from
    // light to dark with a white caption on it, and the message itself on a
    // mid-grey panel in *white* monospace - the panel is a shade the text sits
    // lightly on, not paper with dark ink. The scrollbar beside it runs in a
    // black trough with a light thumb and a black square at either end.
    const COLORREF WinBody      = RGB(0x33, 0x3D, 0x2E);  // the window's card
    const COLORREF WinFrame     = RGB(0xD4, 0xD8, 0xCE);  // every light edge in the window
    const COLORREF WinTitleTop  = RGB(0x71, 0x71, 0x6D);  // title bar, top of the shade
    const COLORREF WinTitleBot  = RGB(0x49, 0x49, 0x45);  // title bar, bottom of the shade
    const COLORREF WinTitleText = RGB(0xFF, 0xFF, 0xFF);
    const COLORREF Paper        = RGB(0x9C, 0x9C, 0x98);  // the message panel
    const COLORREF PaperInk     = RGB(0xFF, 0xFF, 0xFF);
    const COLORREF ButtonFace   = RGB(0x8B, 0x8B, 0x87);  // OK
    const COLORREF ButtonText   = RGB(0xFF, 0xFF, 0xFF);
    const COLORREF ScrollTrough = RGB(0x08, 0x08, 0x08);
    const COLORREF ScrollThumb  = RGB(0x9C, 0x9C, 0x98);
    const COLORREF ScrollEdge   = RGB(0x5C, 0x5C, 0x58);

    // Is that exact face name installed?
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

    // The first of 'names' the system has, NULL if it has none. Looked up once
    // and remembered per call site - font enumeration is not free.
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

    // EuroScope's own label face, carried inside the DLL (see resource.h) so
    // the tags read the same on every machine - the .ttf ships with EuroScope
    // but is not installed by it, and a controller who never put it in Windows
    // would otherwise get a different picture from everyone else.
    //
    // Handed to GDI once, on the first call: AddFontMemResourceEx makes the
    // face private to this process, invisible to font enumeration but found by
    // name like any other. NULL when the resource is missing or GDI refuses it,
    // and then the caller keeps whatever face it was going to use.
    inline HANDLE& EuroScopeFontHandle()
    {
        static HANDLE h = NULL;
        return h;
    }

    inline const wchar_t* EuroScopeFace()
    {
        static const wchar_t* const kFace = L"EuroScope";
        static int state = -1;   // -1 not tried yet, 0 unavailable, 1 usable
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
            // Failing that, the face may still be installed system-wide.
            if (state == 0 && FaceInstalled(kFace))
                state = 1;
        }
        return state > 0 ? kFace : NULL;
    }

    // Given back before the DLL goes away - the fonts it was handed live in
    // the resource image, which unloads with us.
    inline void ReleaseEuroScopeFace()
    {
        if (EuroScopeFontHandle() != NULL)
        {
            RemoveFontMemResourceEx(EuroScopeFontHandle());
            EuroScopeFontHandle() = NULL;
        }
    }

    // "Список РЦ" is set in Inter, the face "rc.svg" was drawn in. It does not
    // come with Windows, so the face is looked for once: "Inter" as the static
    // fonts install it, "Inter Variable" as the variable one does. NULL when
    // neither is there - the window falls back to Arial and says so on its
    // title bar, and the plugin says so in the message window.
    inline const wchar_t* InterFace()
    {
        static const wchar_t* const kNames[] = { L"Inter", L"Inter Variable" };
        static int state = -1;
        return FirstInstalled(kNames, (int)(sizeof(kNames) / sizeof(kNames[0])), state);
    }

    // GDI knows only Regular and Bold inside a family, so the static fonts
    // install every other weight of Inter as a family of its own - these are
    // where they are looked for. NULL when that weight is not installed.
    inline const wchar_t* InterMediumFace()
    {
        static const wchar_t* const kNames[] = { L"Inter Medium" };
        static int state = -1;
        return FirstInstalled(kNames, (int)(sizeof(kNames) / sizeof(kNames[0])), state);
    }

    // "Inter SemiBold" is how the static fonts install it and "Inter Semi Bold"
    // how GDI names the same family.
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

    // Whether the list is drawn in Inter at all - any weight of it counts.
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

    // The three weights "rc.svg" uses, read off its outlines against Inter's
    // own: Regular for the upper pane's headings; Medium for the caption, the
    // lower pane's headings, the values and the filter strip; Bold for the
    // КФ mark alone.
    enum ListWeight { ListRegular, ListMedium, ListBold };

    // A "Список РЦ" font 'px' tall, in Inter when it is installed. A weight
    // installed as a family of its own is used as it is; one that is not is
    // asked of the nearest family there is, which GDI answers with the nearest
    // face it has - emboldened, for Bold.
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

    // Fonts. One plain sans-serif size carries the whole panel; the header
    // clock is the single exception, set a couple of steps larger so the time
    // can be read from across the desk rather than only up close. "Список РЦ"
    // makes its own, at the size it is scaled to - see ListFont.
    struct FontSet
    {
        HFONT Body   = NULL;   // everything on the panel
        HFONT Clock  = NULL;   // the header clock, the one oversized item
        HFONT Small  = NULL;   // ATIS body text; fallback for over-long field values
        HFONT Tiny   = NULL;   // last-resort size for an unusually long callsign/name
        HFONT Large  = NULL;   // ATIS index letter
        HFONT Ruler  = NULL;   // ruler bearing/distance/time label
        HFONT Mono   = NULL;   // ATIS message body - the real system prints it monospaced
        HFONT MonoBig= NULL;   // ATIS index line, a size up from the message
        HFONT MonoHuge = NULL; // the АТИС literal in its own little window
        HFONT WinTitle = NULL; // ATIS window caption - bold, unlike the panel's labels
        HFONT WinTitleSmall = NULL; // the same, for the small letter window's bar
        HFONT Menu    = NULL;  // the menu bar's items

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
            // A step down from the reference export's own sizes: the panel is
            // read at a glance beside the radar picture, and at the export's
            // size it dominated the screen. Every vertical metric in
            // namespace L is scaled to match (see GalaxyATMSystem.cpp).
            Body  = mk(-14, FW_NORMAL);
            Clock = mk(-20, FW_BOLD);   // the reference sets the time in bold
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

    // Every plate on the panel - the panel edge, the group boxes and each
    // control alike - is a rectangle with a 2 px corner radius. GDI's RoundRect
    // takes the corner ellipse's diameter, hence the doubling.
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

    // Same shape, filled flat with no outline.
    inline void FillBox(HDC hDC, const RECT& r, COLORREF fill)
    {
        OutlineBox(hDC, r, fill, fill);
    }

    // Draw a single line of (wide) text inside a rect with the given font/colour.
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

    // A plain control: near-black interior, bright outline, white text.
    inline void DrawControl(HDC hDC, const RECT& r, const std::wstring& text,
        HFONT font, UINT format = DT_CENTER | DT_VCENTER)
    {
        OutlineBox(hDC, r, ControlFill, BorderStrong);
        DrawLine(hDC, r, text, font, Text, format);
    }

    // A selected / live-value block: flat #868E96 with white text. Both the
    // read-only readouts (QNH, transition level) and the pressed buttons carry
    // the frame outline in the reference, so it is on by default here.
    inline void DrawValueField(HDC hDC, const RECT& r, const std::wstring& text, HFONT font,
        bool outlined = true, UINT format = DT_CENTER | DT_VCENTER)
    {
        OutlineBox(hDC, r, Active, outlined ? BorderStrong : Active);
        // Left-aligned values are inset from the plate's edge by the same
        // margin every other left-aligned readout on the panel carries.
        RECT inner = r;
        if ((format & DT_CENTER) == 0)
            inner.left += 5;
        DrawLine(hDC, inner, text, font, ActiveText, format);
    }

    // The аэродром block's buttons (ДАВЛ / Э/П / АТИС) are outlines with no
    // plate behind them - the card shows straight through.
    inline void DrawGhostControl(HDC hDC, const RECT& r, const std::wstring& text, HFONT font)
    {
        OutlineBox(hDC, r, Background, BorderStrong);
        DrawLine(hDC, r, text, font, Text, DT_CENTER | DT_VCENTER);
    }

    // "Ед. изм." uses pill-shaped selectors rather than the square checkboxes
    // the other blocks use. The corner ellipse is taken from the box's own side
    // rather than fixed, so the pill still reads as a circle at any size.
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

    // The АТИС window's own corner radius. Everything inside it is still
    // square - only the card and the light edge round it are rounded, and the
    // parts that reach the corners (the title bar) are clipped to this shape.
    const int WinCornerRadius = 8;

    inline HRGN WinRegion(const RECT& r)
    {
        return CreateRoundRectRgn(r.left, r.top, r.right, r.bottom,
            WinCornerRadius * 2, WinCornerRadius * 2);
    }

    // The light edge round that card, drawn last and on top of everything the
    // clip region let through, so the rounded corners come out clean.
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

    // Everything inside the window is square-cornered chrome rather than one
    // of the panel's rounded plates, so it draws through its own flat helpers.
    inline void FlatFill(HDC hDC, const RECT& r, COLORREF fill)
    {
        HBRUSH br = CreateSolidBrush(fill);
        RECT rc = r;
        FillRect(hDC, &rc, br);
        DeleteObject(br);
    }

    // A border of the given width drawn *inside* the rect, as the window's own
    // light edge and the frame round the message panel both are.
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

    // The title bar's shade, a row of 1 px bands - too few rows to be worth a
    // GradientFill and its msimg32 dependency.
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
