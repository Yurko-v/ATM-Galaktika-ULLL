#pragma once

#include <string>
#include <map>
#include <vector>
#include "Zones.h"
#include "Apw.h"
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
    std::map<std::wstring, PositionInfo> m_Positions;
    std::map<std::wstring, std::wstring> m_UserNames;
    std::wstring m_LoadError;
    std::wstring m_Path;
};
