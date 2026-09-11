#pragma once

#include <string>
#include <map>
#include <vector>
#include "Zones.h"
#include "Apw.h"
#include "Theme.h"

// Static, controller-editable data that is not available from EuroScope:
// aerodrome identity / transition level and the designation+role for each
// controller position. Read once from GalaxyATMSystem.json located next to the DLL.
//
// File format (UTF-8 JSON):
//
//   {
//     "Airport": "ULLI",
//     "QnhMmHg": "760",
//     "QnhHpa": "1013",
//     "Positions": {
//       "ULLL_CTR": { "Designation": "R1", "Role": "R1 ДРУ + R1" }
//     },
//     "Atis": {
//       "Index": "Z",
//       "TopOffset": 30,
//       "TextRu": "ПУЛКОВО АТИС. ИНФОРМАЦИЯ ЗУЛУ, 20:00. ...",
//       "TextEn": "ST PETERSBURG PULKOVO ATIS. INFORMATION ZULU, 20:00. ..."
//     }
//   }
//
// The АТИС window shows both languages, one after the other, the way the
// broadcast itself carries them. "Text" is still accepted as a name for the
// English one, so a config written for an earlier version keeps working.
//
// A "Sigmets" object tunes the сигмет overlay; every key is optional:
//
//   "Sigmets": {
//     "Enabled": true,
//     "RefreshMinutes": 10,
//     "Firs": [ "ULLL", "UUWV" ]
//   }
//
// "Firs" lists the FIRs whose reports are drawn, and defaults to УЛЛЛ alone -
// the feed is worldwide, and a controller working УЛЛЛ has no use for a SIGMET
// over Indonesia. Add neighbouring FIRs to see across the boundary. An
// explicitly empty list, "Firs": [], turns the filter off and draws them all.
//
// "Atis" also decides where the broadcast comes from. With "Live": true (the
// default) the aerodrome's ATIS station is read off the VATSIM datafeed every
// "RefreshMinutes" and its letter and text are what the windows show; "Index"
// and the two texts above are the fallback for when nothing is on the air.
//
// "TopOffset" is how far below the toolbar the index strip stands, in pixels -
// far enough to clear TopSky's menu bar, which the SDK cannot measure.
//
// A "Zones" section lists the запретные зоны, зоны ограничения полётов and
// опасные зоны drawn on the radar. Normally they are read straight out of the
// TopSky package the sector file ships with, which already has them and is
// already maintained - "TopSkyAreas" is the path to its TopSkyAreas.txt, taken
// from the config's own folder when it is relative:
//
//   "Zones": {
//     "Enabled": true,
//     "ItemsFile": "Zones.ULLL.json",
//     "TopSkyAreas": "C:/.../Plugins/TopSky Peterburg/TopSkyAreas.txt",
//     "AupUrl": "https://aup.vatsim-petersburg.com",
//     "AupRefreshMinutes": 10,
//     "NotamSource": "",
//     "NotamRefreshMinutes": 15,
//     "ShowNotamAreas": false
//   }
//
// "ItemsFile" is the library of areas that ships with the plug-in - the same
// TopSky package converted once into JSON by tools/topsky_to_zones.py, so a
// position without TopSky installed has the airspace all the same. It is read
// before "Items" and adds to whatever "TopSkyAreas" carried, so name only one
// of the two unless you want every area twice.
//
// "NotamSource" is where the NOTAMs come from, and there is no default: no
// service publishes Russian NOTAMs to VATSIM. It takes a URL, or the name of a
// file beside the plug-in, and reads either the plan's own JSON or plain ICAO
// NOTAM text - the designators out of the message, the window out of B) and
// C), the levels out of F) and G). With nothing configured, an area that hangs
// on a NOTAM has nothing to answer with and follows "ShowNotamAreas".
//
// Only the areas that are up at this moment are drawn. The permanent ones
// (TopSky's "ACTIVE:1") always are; the restricted ones exist only while the
// day's airspace use plan books them, and "AupUrl" is where that plan is read
// from - the same feed TopSky uses, its HTTP_AUP_URL. A handful of danger areas
// are published by NOTAM instead, which no feed here carries: they stay hidden
// unless "ShowNotamAreas" turns them on, since "unknown" is not "active".
//
// "Items" lists areas of your own, and is added to whatever that file carried:
//
//   "Zones": {
//     "Enabled": true,
//     "Items": [
//       {
//         "Id": "R-1", "Name": "Кронштадт", "Type": "Ограничение",
//         "Lower": "GND", "Upper": "FL095",
//         "Note": "Работает по заявкам, ...",
//         "Circle": { "Center": [59.9386, 29.7658], "RadiusKm": 8 }
//       },
//       { "Id": "P-1", "Type": "Запрет", "Points": [ [59.98, 30.20], ... ] }
//     ]
//   }
//
// "Activation" says when the area is up, written the way TopSky writes it:
// "1" or nothing for one that is always there, "AUP:<id>" for one the day's
// plan books, "NOTAM:<fir>:<id>" for one a NOTAM puts up. "Label" is where its
// designator belongs, when the centre of the ring is the wrong place for it.
//
// "Type" is matched loosely - "Запрет"/"Prohibited"/"P", "Ограничение"/
// "Restricted"/"R", "Опасная"/"Danger"/"D" - and decides the outline's colour.
// An area is either a "Circle" (centre and RadiusKm or RadiusNM, turned into a
// ring for you) or a list of "Points"; a point is either [lat, lon] in decimal
// degrees or a sector-file coordinate string, "N059.48.01.080:E030.15.45.000".
// "Lower"/"Upper"/"Note" are free text and are shown when the area is clicked.
//
// "Enabled" only decides whether they start up shown - ".zones" toggles them
// from then on, and that is what the ASR remembers.
//
// "Colors" retunes the three kinds without a rebuild:
//
//   "Zones": {
//     "Colors": {
//       "Prohibited": { "Fill": "#D2463C", "Line": "#962E28", "Opacity": 30 },
//       "Restricted": { "Fill": "#D2463C", "Line": "#962E28", "Opacity": 10 },
//       "Danger":     { "Fill": "#CE7C3C", "Line": "#92562A", "Opacity": 17 }
//     }
//   }
//
// "Fill" is the wash inside the area and "Line" its outline, either as
// "#RRGGBB" (the leading "#" is optional, "0x" is accepted too) or as
// [ 210, 70, 60 ]. "Opacity" is how densely the wash is laid on, in per cent
// of it - the outline is always drawn solid. "Alpha", 0-255, says the same
// thing in the units the code works in and wins if both are given. Anything
// left out keeps the built-in colour, so a "Colors" node naming one kind is
// enough to retune that one kind.
//
// An "Apw" object tunes the area proximity warning - the tag item that says a
// track is in, or is about to be in, one of those areas. Every key is
// optional and these are the defaults:
//
//   "Apw": {
//     "Enabled": true,
//     "LookAheadMinutes": 2,
//     "BufferNm": 1.0,
//     "VerticalBufferFt": 0,
//     "ShowZone": false,
//     "Kinds": [ "P", "R", "D" ]
//   }
//
// "LookAheadMinutes" is how far the track is flown forward, "BufferNm" the
// margin round the outline and "VerticalBufferFt" the one on its published
// floor and ceiling. "Kinds" is which of запретные / ограничительные / опасные
// warn at all - a position with two hundred ограничительные зоны booked over
// it can drop the "R" and keep the rest. "ShowZone" writes the designator into
// the tag item beside the word.
//
// The alert is the areas' own: it warns about exactly what the overlay draws,
// at the levels the plan booked, and says nothing about an area that is not up.
//
// The key under "Positions" (callsign or position id) is matched against the
// controller's callsign first, then their position id.
// How one kind of area is painted. The defaults are Theme's, so a config that
// says nothing about colours looks exactly as the build does.
struct ZoneStyle
{
    COLORREF fill;
    COLORREF line;
    BYTE alpha;      // out of 255
};

struct PositionInfo
{
    std::wstring Designation;   // e.g. "R1"
    std::wstring Role;          // e.g. "R1 ДРУ + R1"
};

class Config
{
public:
    // Loads the config file located next to the given module (the plugin DLL).
    // Missing file / keys fall back to the defaults below; never throws.
    void Load(HINSTANCE hModule);

    const std::wstring& Airport() const { return m_Airport; }

    // QNH placeholder shown in БЛОК 5 until real METAR parsing lands (Stage 2).
    const std::wstring& QnhMmHg() const { return m_QnhMmHg; }
    const std::wstring& QnhHpa() const { return m_QnhHpa; }

    // АТИС window content. AtisMessage() is the two languages already
    // laid out as one body of text - composed once at load rather than on
    // every frame the window is drawn; the two halves are also readable on
    // their own.
    const std::wstring& AtisIndex() const { return m_AtisIndex; }
    const std::wstring& AtisMessage() const { return m_AtisMessage; }
    const std::wstring& AtisTextRu() const { return m_AtisTextRu; }
    const std::wstring& AtisTextEn() const { return m_AtisTextEn; }

    // Whether to read the live broadcast off the network at all. With it off,
    // or with nothing on the air, the text above is what the windows show.
    bool AtisLive() const { return m_AtisLive; }
    int  AtisRefreshMinutes() const { return m_AtisRefreshMin; }

    // How far below EuroScope's own toolbar the АТИС index strip stands, in
    // pixels. It is a setting rather than a constant because what has to be
    // cleared is TopSky's menu bar, and the SDK cannot see it: TopSky draws
    // its own row of buttons across the top of the radar and nothing in
    // EuroScope reports where it ends. The default clears the menu at its
    // normal size; a screen scaled differently retunes it here.
    int  AtisTopOffset() const { return m_AtisTopOffset; }

    // Looks up the position by callsign first, then by position id. Returns
    // true and fills 'out' on a hit; false if neither key is configured.
    bool FindPosition(const std::string& callsign,
        const std::string& positionId, PositionInfo& out) const;

    // Сигметы. An empty FIR list means "no filter" - see the note above.
    // It is empty only if the config says so; left out, it is УЛЛЛ alone.
    bool SigmetsEnabled() const { return m_SigmetsEnabled; }
    int  SigmetRefreshMinutes() const { return m_SigmetRefreshMin; }
    const std::vector<std::wstring>& SigmetFirs() const { return m_SigmetFirs; }

    // Запретные зоны / зоны ограничений / опасные зоны, as listed in the file.
    // "Enabled" only says whether they start up shown - ".zones" toggles them
    // from then on, and that choice is what the ASR remembers.
    // APW - what the area proximity warning is allowed to warn about and how
    // far ahead it looks. See Apw.h; the tag item is "ULLL APW".
    const ApwSettings& Apw() const { return m_Apw; }

    bool ZonesEnabled() const { return m_ZonesEnabled; }
    const std::vector<Zone>& Zones() const { return m_Zones; }

    // How each kind of area is painted, off the config's "Colors" node.
    const ZoneStyle& ZoneStyleFor(ZoneKind kind) const;

    // Where the day's airspace use plan comes from, how often to go back for
    // it, and what to do with the areas no feed here can answer for.
    const std::string& AupUrl() const { return m_AupUrl; }
    int  AupRefreshMinutes() const { return m_AupRefreshMin; }
    bool ShowNotamAreas() const { return m_ShowNotamAreas; }

    // Where the NOTAMs come from - a URL, or a file beside the plug-in. Empty
    // when nothing is configured, and then the areas that hang on a NOTAM have
    // nothing to answer with and follow ShowNotamAreas instead.
    const std::string& NotamSource() const { return m_NotamSource; }
    int  NotamRefreshMinutes() const { return m_NotamRefreshMin; }

    // The squawk server (see Squawk.h): the folder its endpoints are in, the
    // key it wants and how often to poll it. An empty URL - or "Enabled":
    // false - leaves squawks to EuroScope alone.
    const std::string& SquawkServerUrl() const { return m_SquawkServerUrl; }
    const std::string& SquawkApiKey() const { return m_SquawkApiKey; }
    int  SquawkPollSeconds() const { return m_SquawkPollSeconds; }

    // Let a sweatbox / simulator session take codes out of the same pool the
    // live network uses. Off by default - a training session must not spend
    // the real pool - but the only way to try the whole thing out before a
    // real session is to turn it on for a while.
    bool SquawkAllowSweatbox() const { return m_SquawkAllowSweatbox; }

    // Says out loud, in the "ULLL Squawk" message channel, what every click on
    // the column did - which function id arrived, from where, and whether an
    // aircraft was selected. For working out why a click does nothing.
    bool SquawkDebug() const { return m_SquawkDebug; }

    // How many positions the file named - what ".reload" reports, so a config
    // that failed to parse says so by the count rather than only by its error.
    size_t PositionCount() const { return m_Positions.size(); }

    const std::wstring& LoadError() const { return m_LoadError; }
    const std::wstring& ConfigPath() const { return m_Path; }

private:
    std::wstring m_Airport = L"ULLI";
    std::wstring m_QnhMmHg = L"760";
    std::wstring m_QnhHpa = L"1013";
    std::wstring m_AtisIndex = L"Z";
    std::wstring m_AtisTextRu;
    std::wstring m_AtisTextEn;
    std::wstring m_AtisMessage = L"ATIS TEXT NOT CONFIGURED - edit GalaxyATMSystem.json";
    bool m_AtisLive = true;
    int  m_AtisRefreshMin = 2;
    int  m_AtisTopOffset = 22;
    bool m_SigmetsEnabled = true;
    int  m_SigmetRefreshMin = 10;
    std::vector<std::wstring> m_SigmetFirs = { L"ULLL" };   // upper-cased; empty = all
    ApwSettings m_Apw;

    bool m_ZonesEnabled = true;
    std::vector<Zone> m_Zones;
    ZoneStyle m_ZoneProhibited = { Theme::ZoneFillProhibited, Theme::ZoneLineProhibited, Theme::ZoneAlphaProhibited };
    ZoneStyle m_ZoneRestricted = { Theme::ZoneFillRestricted, Theme::ZoneLineRestricted, Theme::ZoneAlphaRestricted };
    ZoneStyle m_ZoneDanger     = { Theme::ZoneFillDanger,     Theme::ZoneLineDanger,     Theme::ZoneAlphaDanger };
    std::string m_AupUrl;
    int  m_AupRefreshMin = 10;
    bool m_ShowNotamAreas = false;
    std::string m_NotamSource;
    int  m_NotamRefreshMin = 15;
    std::string m_SquawkServerUrl;
    std::string m_SquawkApiKey;
    int  m_SquawkPollSeconds = 15;
    bool m_SquawkAllowSweatbox = false;
    bool m_SquawkDebug = false;
    std::map<std::wstring, PositionInfo> m_Positions;   // key -> info (key upper-cased)
    std::wstring m_LoadError;
    std::wstring m_Path;
};
