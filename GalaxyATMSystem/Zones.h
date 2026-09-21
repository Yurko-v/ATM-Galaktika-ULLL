#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>
#include <vector>
#include "EuroScopePlugIn.h"

#include <ctime>

namespace Json { struct Value; }

enum class ZoneKind { Prohibited, Restricted, Danger };

struct Zone
{
    std::wstring id;
    std::wstring name;
    ZoneKind kind = ZoneKind::Restricted;
    std::wstring lower;
    std::wstring upper;
    std::wstring note;

    std::wstring activation;

    std::vector<EuroScopePlugIn::CPosition> ring;

    bool hasLabelPos = false;
    EuroScopePlugIn::CPosition labelPos;

    std::wstring KindLabel() const;
    std::wstring Title() const;

    std::wstring LevelBand() const;
};

std::wstring ZoneLevelText(int fl);

bool ParseZones(const Json::Value& node, std::vector<Zone>& out, bool& enabled);

bool LoadTopSkyAreas(const std::wstring& path, std::vector<Zone>& out);

struct ZoneBooking
{
    std::wstring name;
    int minFL = 0;
    int maxFL = 999;
    time_t start = 0;
    time_t end = 0;
};

bool ParseAup(const std::string& body, std::vector<ZoneBooking>& out);

bool ParseNotams(const std::string& body, std::vector<ZoneBooking>& out);

bool FetchNotams(const std::string& source, std::vector<ZoneBooking>& out);

bool FetchAup(const std::string& url, std::vector<ZoneBooking>& out);

struct ZoneActivation
{
    const std::vector<ZoneBooking>* aup = NULL;
    const std::vector<ZoneBooking>* notams = NULL;
    bool showNotamWhenUnknown = false;
};

bool ZoneActiveNow(const Zone& zone, const ZoneActivation& what,
    time_t nowUtc, const ZoneBooking** booking);
