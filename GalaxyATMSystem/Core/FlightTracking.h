#pragma once

#include "Core/Base.h"

namespace Galaxy
{
    inline double GeoBearingDeg(const EuroScopePlugIn::CPosition& from,
        const EuroScopePlugIn::CPosition& to)
    {
        const double lat1 = from.m_Latitude * M_PI / 180.0;
        const double lat2 = to.m_Latitude * M_PI / 180.0;
        const double dLon = (to.m_Longitude - from.m_Longitude) * M_PI / 180.0;
        const double y = sin(dLon) * cos(lat2);
        const double x = cos(lat1) * sin(lat2) - sin(lat1) * cos(lat2) * cos(dLon);
        const double deg = atan2(y, x) * 180.0 / M_PI;
        return (deg < 0.0) ? deg + 360.0 : deg;
    }

    inline const double kRamThresholdNm = 5.0;
    inline const int kRamMinGsKt = 50;

    inline double CrossTrackNm(const EuroScopePlugIn::CPosition& a,
        const EuroScopePlugIn::CPosition& b, const EuroScopePlugIn::CPosition& p)
    {
        const double R = 3440.065;
        EuroScopePlugIn::CPosition from = a, to = b, at = p;

        const double legNm = from.DistanceTo(to);
        const double d13 = from.DistanceTo(at);
        if (legNm < 0.1)
            return d13;

        const double turn = fmod(GeoBearingDeg(from, at) - GeoBearingDeg(from, to) + 540.0,
            360.0) - 180.0;
        if (fabs(turn) > 90.0)
            return d13;

        const double across = fabs(asin(sin(d13 / R) * sin(turn * M_PI / 180.0)) * R);
        double ratio = cos(d13 / R) / cos(across / R);
        ratio = max(-1.0, min(1.0, ratio));
        if (acos(ratio) * R > legNm)
            return to.DistanceTo(at);

        return across;
    }

    inline const double kRamClearNm = 4.0;

    inline const ULONGLONG kRamRecheckMs = 1000;
    inline const ULONGLONG kPartnerRecheckMs = 3000;

    struct RamTrack
    {
        bool on = false;
        ULONGLONG tick = 0;
        std::string directPoint;
        EuroScopePlugIn::CPosition directStart;
    };
    inline std::map<std::string, RamTrack> g_ram;

    struct PartnerGuess
    {
        std::string callsign;
        ULONGLONG tick = 0;
    };
    inline std::map<std::string, PartnerGuess> g_partnerGuess;

    inline bool RouteAdherenceAlert(EuroScopePlugIn::CFlightPlan fp, EuroScopePlugIn::CRadarTarget rt)
    {
        if (!fp.IsValid() || !rt.IsValid())
            return false;
        const std::string callsign = fp.GetCallsign();
        RamTrack& ram = g_ram[callsign];
        if (Fresh(ram.tick, callsign, kRamRecheckMs))
            return ram.on;
        EuroScopePlugIn::CRadarTargetPositionData pos = rt.GetPosition();
        EuroScopePlugIn::CFlightPlanControllerAssignedData cad = fp.GetControllerAssignedData();
        const int cfl = cad.GetClearedAltitude();
        EuroScopePlugIn::CFlightPlanExtractedRoute route = fp.GetExtractedRoute();
        const int points = route.GetPointsNumber();
        const int leg = route.GetPointsCalculatedIndex();
        if (!pos.IsValid() || rt.GetGS() < kRamMinGsKt || cad.GetAssignedHeading() > 0
            || cfl == 1 || cfl == 2 || points < 2 || leg < 0 || leg + 1 >= points)
        {
            ram.on = false;
            ram.directPoint.clear();
            return false;
        }

        double offNm;
        const int directIdx = route.GetPointsAssignedIndex();
        if (directIdx >= 0 && directIdx < points && route.GetPointDistanceInMinutes(directIdx) >= 0)
        {
            const std::string name = route.GetPointName(directIdx);
            if (ram.directPoint != name)
            {
                ram.directPoint = name;
                ram.directStart = pos.GetPosition();
            }
            offNm = CrossTrackNm(ram.directStart, route.GetPointPosition(directIdx), pos.GetPosition());
        }
        else
        {
            ram.directPoint.clear();
            offNm = CrossTrackNm(route.GetPointPosition(leg), route.GetPointPosition(leg + 1),
                pos.GetPosition());
        }

        ram.on = ram.on ? offNm > kRamClearNm : offNm > kRamThresholdNm;
        return ram.on;
    }
}
