#pragma once

#include <string>
#include <vector>

// Short term conflict alert: predicts, from radar data, which pairs of aircraft will
// lose separation within the next couple of minutes.
//
// Kept free of EuroScope types so the whole calculation can be run on made up
// traffic outside the simulator. The radar screen turns EuroScope targets into
// TrackInput, and Conflict back into lines and formular warnings.
//
// Compared with a plain straight line extrapolation it:
//   * works out track, turn rate and vertical rate from the position history itself,
//     as true bearings, rather than trusting EuroScope's calculated values;
//   * stops a climb or descent at the cleared level, so an aircraft climbing to
//     FL300 under one at FL310 is not reported as a conflict;
//   * follows a turn that is in progress for a limited time, and stops it at the
//     assigned heading when the aircraft is turning onto one;
//   * finds the time of the first loss and the closest point of approach rather
//     than only saying "conflict";
//   * takes the lateral minimum from the zone each aircraft is in (a sector of the
//     .ese, by the position that owns it), with a wake minimum behind a heavy or a
//     super where that zone has one, and the en-route minimum outside every zone.
namespace Stca
{
    struct HistoryPoint
    {
        double lat = 0.0, lon = 0.0;
        double ageSec = 0.0;    // how long ago this position was received
        double altFt = 0.0;     // standard pressure level, the same reference for every aircraft
    };

    struct TrackInput
    {
        std::string callsign;
        std::vector<HistoryPoint> history;      // newest first
        double gsKt = 0.0;                      // reported ground speed
        double fallbackTrackDeg = -1.0;         // true track for a target with no usable history
        double fallbackVsFpm = 0.0;
        double cflFt = -1.0;                    // cleared level on the altFt reference; < 0 when none
        double assignedHeadingDeg = -1.0;       // true heading the aircraft was told to fly; < 0 when none
        char   wtc = '?';                       // wake category from the flight plan: L, M, H, J
        bool   modeC = true;
    };

    struct TrackState
    {
        std::string callsign;
        bool   valid = false;
        double lat = 0.0, lon = 0.0, altFt = 0.0;
        double ageSec = 0.0;                    // age of the position the state starts from
        double trackDeg = 0.0;                  // true
        double gsKt = 0.0;
        double turnDegSec = 0.0;                // + right, - left
        double vsFpm = 0.0;
        double cflFt = -1.0;
        double assignedHeadingDeg = -1.0;
        char   wtc = '?';
    };

    struct LatLon
    {
        double lat = 0.0, lon = 0.0;
    };

    // A piece of airspace with its own lateral minimum. Where zones overlap the
    // smallest one counts, the way a tower sector sits inside an approach one.
    struct Zone
    {
        std::string name;                   // the position whose minima apply, for the log
        std::vector<LatLon> ring;
        double floorFt = 0.0, ceilingFt = 0.0;
        double lateralNm = 5.4;
        double behindHeavyNm = 0.0;         // following an H; 0 when the zone has no wake minimum
        double behindSuperNm = 0.0;         // following a J
        // Filled by Bound.
        double minLat = 0.0, maxLat = 0.0, minLon = 0.0, maxLon = 0.0;
        double area = 0.0;
    };

    // Works out the bounding box and the area a zone is looked up by.
    void Bound(Zone& zone);

    struct Settings
    {
        int    lookaheadSec = 120;
        double lateralNm = 5.4;             // 10 km
        double verticalFt = 1000.0;
        double verticalHighFt = 2000.0;     // above highFromFt
        double highFromFt = 41000.0;
        double verticalToleranceFt = 100.0; // altimetry noise allowed below the minimum
        double minAltitudeFt = 1500.0;      // below this nobody is checked
        double minGsKt = 50.0;
        double levelVsFpm = 300.0;          // slower than this counts as level
        bool   turns = true;
        double maxTurnSec = 30.0;           // how long a turn in progress is followed
        double maxTurnDeg = 90.0;
        std::vector<Zone> zones;
    };

    struct Conflict
    {
        std::string a, b;
        bool   lossNow = false;             // the minima are already broken
        int    timeToLossSec = 0;           // first predicted second inside the minima
        int    timeToCpaSec = 0;            // second of the smallest horizontal distance while inside
        double cpaNm = 0.0;
        double cpaVerticalFt = 0.0;
        double lateralMinNm = 0.0;          // the minimum that applied at the CPA
        double verticalMinFt = 0.0;
        double aLat = 0.0, aLon = 0.0, aAltFt = 0.0;   // where each aircraft is at the CPA
        double bLat = 0.0, bLon = 0.0, bAltFt = 0.0;
        double nowNm = 0.0, nowVerticalFt = 0.0;       // separation right now
    };

    // True great circle bearing, the way a track is measured on the ground.
    double TrueBearing(double lat1, double lon1, double lat2, double lon2);
    double DistanceNm(double lat1, double lon1, double lat2, double lon2);

    TrackState Estimate(const TrackInput& input, const Settings& settings);

    std::vector<Conflict> Detect(const std::vector<TrackState>& tracks, const Settings& settings);
}
