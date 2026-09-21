#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>
#include <vector>

#include "EuroScopePlugIn.h"
#include "Zones.h"

struct ApwSettings
{
    bool   enabled = true;

    int    lookAheadMin = 2;

    double bufferNm = 1.0;

    int    verticalBufferFt = 0;

    bool   showZone = false;

    bool   warnProhibited = true;
    bool   warnRestricted = true;
    bool   warnDanger = true;
};

enum class ApwLevel
{
    None = 0,
    Predicted,
    Inside
};

struct ApwResult
{
    ApwLevel     level = ApwLevel::None;
    std::wstring zoneId;
    int          secondsToEntry = 0;
};

struct ApwTrack
{
    EuroScopePlugIn::CPosition pos;
    double trackDeg = 0.0;
    int    gsKt = 0;
    int    altFt = 0;
    int    vsFpm = 0;

    std::wstring origin;
    std::wstring destination;
};

struct ZoneExemption
{
    std::vector<std::wstring> airports;
    int ceilingFt = 0;
};

bool ParseZoneExemption(const std::wstring& note, ZoneExemption& out);

struct ApwZone
{
    bool   active = false;
    bool   warns = false;
    double minLat = 0.0, maxLat = 0.0, minLon = 0.0, maxLon = 0.0;
    int    lowFt = 0, highFt = 0;

    ZoneExemption exempt;
};

bool ZoneLevelFL(const std::wstring& text, int& fl);

void ApwBuildZones(const std::vector<Zone>& zones,
    const std::vector<char>& active,
    const std::vector<const ZoneBooking*>& bookings,
    const ApwSettings& cfg,
    std::vector<ApwZone>& out);

ApwResult ApwCheck(const std::vector<Zone>& zones,
    const std::vector<ApwZone>& prepared,
    const ApwTrack& track,
    const ApwSettings& cfg);
