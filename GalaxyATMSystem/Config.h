#pragma once

#include <string>
#include <map>
#include <vector>
#include "Zones.h"
#include "Apw.h"
#include "Stca.h"
#include "Theme.h"

struct ZoneStyle
{
    COLORREF fill;
    COLORREF line;
    BYTE alpha;
};

struct PositionInfo
{
    std::wstring Designation;
    std::wstring Role;
};

// An АРП: the direction finder a sector works with. It normally sits on the
// aerodrome reference point, so when no Point is given the position is taken from
// the sector file airport whose ICAO matches the station id.
struct RdfStation
{
    std::wstring id;
    std::wstring name;
    bool hasPoint = false;
    EuroScopePlugIn::CPosition point;

    // The "контрольный пеленг" the station shows while the controller transmits.
    // Different at every station, so -1 until the local one is known.
    int controlBearing = -1;

    // Degrees east, when the station reads out magnetic bearings. Left at zero the
    // readout is true, which is what EuroScope measures.
    double variation = 0.0;
};

// Conflict alert minima for the zone of a position: the .ese sectors whose first
// owner is that position. The pattern is a position callsign, '*' standing for any
// run of characters, so ULLL_*CTR covers ULLL_CTR and ULLL_R2_CTR alike.
struct StcaZoneRule
{
    std::wstring pattern;
    double lateralNm = 5.4;
    double behindHeavyNm = 0.0;     // wake minimum behind an H; 0 for none
    double behindSuperNm = 0.0;     // behind a J
};

class Config
{
public:
    void Load(HINSTANCE hModule);

    const std::wstring& Airport() const { return m_Airport; }

    const std::wstring& QnhMmHg() const { return m_QnhMmHg; }
    const std::wstring& QnhHpa() const { return m_QnhHpa; }

    const std::wstring& AtisIndex() const { return m_AtisIndex; }
    const std::wstring& AtisMessage() const { return m_AtisMessage; }
    const std::wstring& AtisTextRu() const { return m_AtisTextRu; }
    const std::wstring& AtisTextEn() const { return m_AtisTextEn; }

    bool AtisLive() const { return m_AtisLive; }
    const std::vector<std::wstring>& AtisAirports() const { return m_AtisAirports; }
    int  AtisRefreshSeconds() const { return min(m_AtisRefreshSec, m_AtisRefreshMin * 60); }

    int  AtisTopOffset() const { return m_AtisTopOffset; }

    bool FindPosition(const std::string& callsign,
        const std::string& positionId, PositionInfo& out) const;

    std::wstring UserName(const std::wstring& cid) const
    {
        auto it = m_UserNames.find(cid);
        return it == m_UserNames.end() ? std::wstring() : it->second;
    }

    bool SigmetsEnabled() const { return m_SigmetsEnabled; }
    int  SigmetRefreshMinutes() const { return m_SigmetRefreshMin; }
    const std::vector<std::wstring>& SigmetFirs() const { return m_SigmetFirs; }

    const ApwSettings& Apw() const { return m_Apw; }

    bool ZonesEnabled() const { return m_ZonesEnabled; }
    const std::vector<Zone>& Zones() const { return m_Zones; }

    const ZoneStyle& ZoneStyleFor(ZoneKind kind) const;

    const std::string& AupUrl() const { return m_AupUrl; }
    int  AupRefreshMinutes() const { return m_AupRefreshMin; }
    bool ShowNotamAreas() const { return m_ShowNotamAreas; }

    const std::string& NotamSource() const { return m_NotamSource; }
    int  NotamRefreshMinutes() const { return m_NotamRefreshMin; }

    const std::string& SquawkServerUrl() const { return m_SquawkServerUrl; }
    const std::string& SquawkApiKey() const { return m_SquawkApiKey; }
    int  SquawkPollSeconds() const { return m_SquawkPollSeconds; }

    bool SquawkAllowSweatbox() const { return m_SquawkAllowSweatbox; }

    bool SquawkDebug() const { return m_SquawkDebug; }

    bool RdfEnabled() const { return m_RdfEnabled; }
    const std::string& RdfEndpoint() const { return m_RdfEndpoint; }
    const std::vector<RdfStation>& RdfStations() const { return m_RdfStations; }

    // The АРП that belongs to the position we are logged in as, or the default one.
    const RdfStation* RdfStationFor(const std::wstring& positionCallsign) const;

    bool StcaEnabled() const { return m_StcaEnabled; }
    // The settings without zones; the screen builds those from the .ese sectors.
    const Stca::Settings& StcaSettings() const { return m_StcaSettings; }
    const std::vector<StcaZoneRule>& StcaZoneRules() const { return m_StcaZoneRules; }
    // The rule for a position callsign: an exact one first, otherwise the longest
    // matching pattern. NULL when none matches - the en-route minimum applies there.
    const StcaZoneRule* StcaZoneRuleFor(const std::wstring& positionCallsign) const;

    size_t PositionCount() const { return m_Positions.size(); }

    const std::wstring& LoadError() const { return m_LoadError; }
    // Different after every Load, for whoever caches something built from the config.
    unsigned Generation() const { return m_Generation; }
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
    std::vector<std::wstring> m_AtisAirports = { L"ULOO", L"ULOL", L"ULPB", L"ULWW", L"ULWC" };
    int  m_AtisRefreshMin = 60;
    int  m_AtisRefreshSec = 20;
    int  m_AtisTopOffset = 22;
    bool m_SigmetsEnabled = true;
    int  m_SigmetRefreshMin = 1;
    std::vector<std::wstring> m_SigmetFirs = { L"ULLL" };
    ApwSettings m_Apw;

    bool m_ZonesEnabled = true;
    std::vector<Zone> m_Zones;
    ZoneStyle m_ZoneProhibited = { Theme::ZoneFillProhibited, Theme::ZoneLineProhibited, Theme::ZoneAlphaProhibited };
    ZoneStyle m_ZoneRestricted = { Theme::ZoneFillRestricted, Theme::ZoneLineRestricted, Theme::ZoneAlphaRestricted };
    ZoneStyle m_ZoneDanger     = { Theme::ZoneFillDanger,     Theme::ZoneLineDanger,     Theme::ZoneAlphaDanger };
    std::string m_AupUrl;
    int  m_AupRefreshMin = 1;
    bool m_ShowNotamAreas = false;
    std::string m_NotamSource;
    int  m_NotamRefreshMin = 1;
    std::string m_SquawkServerUrl;
    std::string m_SquawkApiKey;
    int  m_SquawkPollSeconds = 15;
    bool m_SquawkAllowSweatbox = false;
    bool m_SquawkDebug = false;
    bool m_RdfEnabled = true;
    std::string m_RdfEndpoint = "127.0.0.1:49080";
    std::vector<RdfStation> m_RdfStations;
    std::wstring m_RdfDefault;
    std::map<std::wstring, std::wstring> m_RdfByPosition;
    bool m_StcaEnabled = true;
    Stca::Settings m_StcaSettings;
    // As agreed for Pulkovo: 5 km in the tower zone; 5 km in the radar approach zone,
    // 10 km behind a heavy and 15 km behind a super; 10 km at approach and control.
    std::vector<StcaZoneRule> m_StcaZoneRules = {
        StcaZoneRule{ L"ULLI_*TWR", 5.0 / 1.852 },
        StcaZoneRule{ L"ULLI_R_APP", 5.0 / 1.852, 10.0 / 1.852, 15.0 / 1.852 },
        StcaZoneRule{ L"ULLL_*APP", 10.0 / 1.852 },
        StcaZoneRule{ L"ULLL_*CTR", 10.0 / 1.852 },
    };
    std::map<std::wstring, PositionInfo> m_Positions;
    std::map<std::wstring, std::wstring> m_UserNames;
    std::wstring m_LoadError;
    std::wstring m_Path;
    unsigned m_Generation = 0;
};
