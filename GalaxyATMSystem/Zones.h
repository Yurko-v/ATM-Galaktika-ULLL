#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>
#include <vector>
#include "EuroScopePlugIn.h"

#include <ctime>

namespace Json { struct Value; }

// -----------------------------------------------------------------------------
// Запретные зоны, зоны ограничения полётов и опасные зоны, as described in the
// config file. EuroScope's sector file carries geometry of its own, but nothing
// a plug-in can read back and nothing a controller can annotate - so the areas
// the position actually works with are listed in GalaxyATMSystem.json, drawn by
// the plug-in and clickable for their details.
// -----------------------------------------------------------------------------
enum class ZoneKind { Prohibited, Restricted, Danger };

struct Zone
{
    std::wstring id;      // "ULR100" - the designator, drawn on the area itself
    std::wstring name;    // "Кронштадт"
    ZoneKind kind = ZoneKind::Restricted;
    std::wstring lower;   // written as the AIP writes it: "GND", "FL095", "500 м"
    std::wstring upper;
    std::wstring note;    // activity, working hours, who to call - free text

    // How the area is switched on: empty or "1" for one that is always there,
    // "AUP:<id>" for one that only exists when the day's airspace use plan
    // says so. Kept as written - what matters on the screen is the difference
    // between a permanent area and one that has to be checked.
    std::wstring activation;

    std::vector<EuroScopePlugIn::CPosition> ring;

    // Where the designator goes. TopSky's own files carry a label position per
    // area, which beats the ring's centre of gravity on a horseshoe-shaped one;
    // without it the centre is used.
    bool hasLabelPos = false;
    EuroScopePlugIn::CPosition labelPos;

    std::wstring KindLabel() const;   // "Запретная зона" / "Зона ограничений" / "Опасная зона"
    std::wstring Title() const;       // one line, for the hover tooltip

    // "GND-FL095" - the published limits as one line, the way the info window
    // and the real system both write them.
    std::wstring LevelBand() const;
};

// A level as the areas write it: the ground and the top of the sky by name,
// everything between them as a flight level.
std::wstring ZoneLevelText(int fl);

// Reads the "Zones" node of the config file. Accepts either the array of areas
// on its own or an object of { "Enabled": true, "Items": [ ... ] }; 'enabled'
// is left untouched when the node does not say.
//
// Every area needs a ring of at least three points, so anything that parses to
// less is dropped rather than half-drawn. Never throws; a malformed entry costs
// that entry and nothing else.
bool ParseZones(const Json::Value& node, std::vector<Zone>& out, bool& enabled);

// Reads TopSky's own TopSkyAreas.txt - the file the TopSky plug-in draws its
// prohibited, danger and restricted areas from. It is the same airspace this
// panel wants to show and it is already maintained by whoever keeps the sector
// package up to date, so the areas are read straight out of it rather than
// copied into our own config by hand.
//
// Understood: AREA / CATEGORY (P, D, R) / LIMITS / LABEL / USERTEXT / CIRCLE
// / ACTIVE, and the plain "N059.48.59.000 E030.17.00.000" coordinate lines.
// Anything else in the file is skipped.
//
// Appends to 'out'. Returns false only when the file cannot be opened at all.
bool LoadTopSkyAreas(const std::wstring& path, std::vector<Zone>& out);

// -----------------------------------------------------------------------------
// Which of those areas is switched on at this moment.
//
// Most of the restricted ones exist only while the day's airspace use plan says
// so, and the plan is published as a list of bookings - one area, one band of
// levels, one window of time. It is the same feed TopSky reads (its
// HTTP_AUP_URL), so the two show the same airspace.
// -----------------------------------------------------------------------------
struct ZoneBooking
{
    std::wstring name;      // the area's designator, "ULR100"
    int minFL = 0;
    int maxFL = 999;
    time_t start = 0;       // UTC, seconds since the epoch
    time_t end = 0;
};

bool ParseAup(const std::string& body, std::vector<ZoneBooking>& out);

// The other half of what switches an area on: the NOTAM. There is no feed for
// them the way there is for the plan - no VATSIM service publishes Russian
// NOTAMs - so the source is whatever the config points at, and two shapes are
// understood:
//
//   - the plan's own JSON, { "areas": [ { "name", "start_datetime", ... } ] }.
//     Anything that can be made to emit that works with no more code here.
//   - plain ICAO NOTAM text, as every source in the world will hand it over:
//     the designators are read out of the message, the window out of its B)
//     and C) fields and the levels out of F) and G).
//
// A designator is a token like ULD3, ULR100 or ULP12 - letters then digits.
// Every one found in a message books that area for the message's window, and a
// name no area carries costs nothing.
bool ParseNotams(const std::string& body, std::vector<ZoneBooking>& out);

// Blocking, like FetchAup. "http://..." / "https://..." is fetched; anything
// else is read as a file, so a position with no source at all can keep the
// day's NOTAM in a text file beside the plug-in and have the areas follow it.
bool FetchNotams(const std::string& source, std::vector<ZoneBooking>& out);

// Blocking - call it on a worker thread. Returns false when the plan could not
// be read or parsed, leaving 'out' untouched.
bool FetchAup(const std::string& url, std::vector<ZoneBooking>& out);

// Whether the area is up at nowUtc, and the booking that puts it there.
//
//   ACTIVE:1        - permanent, always up.
//   ACTIVE:AUP:<id> - up only while a booking for <id> covers nowUtc.
//   ACTIVE:NOTAM:<fir>:<id> - up only while a NOTAM for <id> covers nowUtc,
//                     when a NOTAM source is configured. With none, there is
//                     nothing to answer with and these follow showNotam,
//                     since "unknown" is not "active".
//
// 'notams' is NULL when no source is configured, which is not the same as a
// source that came back with nothing to report.
//
// 'booking' is filled with the booking that made it active, when there was one.
struct ZoneActivation
{
    const std::vector<ZoneBooking>* aup = NULL;
    const std::vector<ZoneBooking>* notams = NULL;
    bool showNotamWhenUnknown = false;
};

bool ZoneActiveNow(const Zone& zone, const ZoneActivation& what,
    time_t nowUtc, const ZoneBooking** booking);
