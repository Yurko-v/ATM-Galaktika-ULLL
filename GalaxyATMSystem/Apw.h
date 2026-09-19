#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>
#include <vector>

#include "EuroScopePlugIn.h"
#include "Zones.h"

// -----------------------------------------------------------------------------
// APW - Area Proximity Warning.
//
// The ground half of the airspace-infringement alerts: the system watches every
// track against the airspace it must not be in - запретные, опасные and the
// ограничительные зоны that the day's plan has switched on - and tells the
// controller before the aircraft is in it rather than after.
//
// What it is, in the words of the definition it is built to:
//
//   "APW is a ground-based safety net that warns the controller when an
//    aircraft is, or is predicted to be, in an area of airspace it should not
//    enter" - see https://skybrary.aero/articles/area-proximity-warning
//
// Two things follow from that and both are in here. It is *predicted*, so the
// track is flown forward and the warning is raised on where the aircraft is
// going, not only on where it is; and it is *an area*, so the vertical limits
// of that area count as much as its outline - an aircraft over the top of a
// зона ограничений is not infringing it.
//
// Deliberately not on the radar screen: EuroScope asks the plug-in for a tag
// item with no screen behind it, and the warning has to be the same for every
// display the position has open anyway.
// -----------------------------------------------------------------------------

// What the config file says about the alert. Everything here is a controller's
// judgement of how much warning is wanted and how much noise is bearable, which
// is why none of it is baked in.
struct ApwSettings
{
    bool   enabled = true;

    // How far ahead the track is flown. The usual working value: long enough
    // to turn or stop a climb, short enough that a turn away from the area
    // clears the warning at once.
    int    lookAheadMin = 2;

    // A margin round the outline, in nautical miles. A track that will pass
    // this close to an area counts as entering it: the areas are published to
    // the nautical mile and the radar's own position is not exact either.
    double bufferNm = 1.0;

    // The same idea vertically, in feet, applied to both the floor and the
    // ceiling of the area.
    int    verticalBufferFt = 0;

    // Whether the designator is written into the tag item beside the word.
    // Off by default: "APW" is what has to be seen from across the room, and
    // which area it is is a click on the area away.
    bool   showZone = false;

    // Which kinds warn at all. All three normally; a position that works with
    // a hundred and eighty ограничительные зоны booked over it can turn that
    // one off and keep the alerts that matter.
    bool   warnProhibited = true;
    bool   warnRestricted = true;
    bool   warnDanger = true;
};

// How bad it is. The order is the severity order and is compared as such.
enum class ApwLevel
{
    None = 0,
    Predicted,   // will be inside it within the look-ahead
    Inside       // is inside it now
};

struct ApwResult
{
    ApwLevel     level = ApwLevel::None;
    std::wstring zoneId;          // the designator of the area that raised it
    int          secondsToEntry = 0;   // 0 when already inside
};

// One track, in the terms the areas are written in. The caller decides which
// altitude to hand over - the areas mix QNH altitudes with flight levels the
// way the AIP publishes them, and only the plug-in knows the transition level.
struct ApwTrack
{
    EuroScopePlugIn::CPosition pos;
    double trackDeg = 0.0;   // true
    int    gsKt = 0;
    int    altFt = 0;
    int    vsFpm = 0;

    // Where it is coming from and going to, as the flight plan writes them.
    // Half the areas over Пулково are published with an exception for exactly
    // that traffic, so without this the alert would fire on every departure
    // and every arrival the position works. Empty on an uncorrelated track,
    // which is then warned about the way anyone else is.
    std::wstring origin;
    std::wstring destination;
};

// -----------------------------------------------------------------------------
// The exception the area is published with.
//
// Most of the areas over Пулково would be unflyable as written: the departure
// and arrival procedures of the aerodrome they surround go straight through
// them. The AIP resolves it the way it always does - by naming the traffic the
// area does not apply to - and the sector package carries that in the area's
// note, in two forms and no others:
//
//   "Except ULLI"        - it does not apply to ULLI traffic at all
//   "Except ULLI/ULLP"   - nor to ULLP's
//   "ULLI 3000ft"        - it applies to ULLI traffic only below 3000 ft,
//                          which is the height their procedures cross it at
//
// An aircraft on neither end of that list is warned about as usual.
// -----------------------------------------------------------------------------
struct ZoneExemption
{
    std::vector<std::wstring> airports;   // ICAO, upper case; empty - no exception
    int ceilingFt = 0;   // 0: the area is not there for them at all. Otherwise
                         // the ceiling it has for them, in feet.
};

// Reads one out of an area's note. False when the note says nothing of the
// kind, which is what most of them do - a note is free text and anything that
// is not one of the two forms above is left to the controller to read.
bool ParseZoneExemption(const std::wstring& note, ZoneExemption& out);

// An area reduced to what the alert needs: the box its outline fits in and the
// band it occupies, both worked out once when the config is read rather than
// per aircraft per second.
//
// 'lowFt' and 'highFt' come from the area's published limits unless a booking
// says otherwise - the plan books a band inside the area, and it is the booked
// band the aircraft has to stay out of.
struct ApwZone
{
    bool   active = false;    // switched on at this moment; nothing else warns
    bool   warns = false;     // its kind is one the config asks to be warned about
    double minLat = 0.0, maxLat = 0.0, minLon = 0.0, maxLon = 0.0;
    int    lowFt = 0, highFt = 0;

    // Read off the note once here rather than per aircraft per second.
    ZoneExemption exempt;
};

// The area's limit as a flight level: "GND"/"SFC" -> 0, "UNL" -> 999, "FL095"
// -> 95, a bare number as that number, and "500 м"/"500 M" converted from
// metres. False when there is nothing to read, which the caller must treat as
// "no limit published" rather than as ground level.
bool ZoneLevelFL(const std::wstring& text, int& fl);

// Builds the per-area half of the alert. 'zones' and 'out' are index-parallel;
// 'active' and 'booking' come from the same ZoneActiveNow the overlay is drawn
// from, so what warns is what the controller can see.
void ApwBuildZones(const std::vector<Zone>& zones,
    const std::vector<char>& active,
    const std::vector<const ZoneBooking*>& bookings,
    const ApwSettings& cfg,
    std::vector<ApwZone>& out);

// The warning for one track, or ApwLevel::None. 'zones' and 'prepared' are
// index-parallel - the first carries the outlines, the second what
// ApwBuildZones worked out about them.
ApwResult ApwCheck(const std::vector<Zone>& zones,
    const std::vector<ApwZone>& prepared,
    const ApwTrack& track,
    const ApwSettings& cfg);
