#include "pch.h"
#include "Config.h"
#include "Json.h"

#include <fstream>
#include <algorithm>

namespace
{
    std::wstring ToUpper(std::wstring s)
    {
        std::transform(s.begin(), s.end(), s.begin(), ::towupper);
        return s;
    }

    std::wstring ModuleDir(HINSTANCE hModule)
    {
        wchar_t path[MAX_PATH] = { 0 };
        GetModuleFileNameW(hModule, path, MAX_PATH);
        std::wstring p(path);
        size_t slash = p.find_last_of(L"\\/");
        return (slash == std::wstring::npos) ? std::wstring() : p.substr(0, slash + 1);
    }

    std::wstring ResolvePath(HINSTANCE hModule, const std::wstring& path)
    {
        bool absolute = (path.size() > 1 && path[1] == L':')
            || (!path.empty() && (path[0] == L'\\' || path[0] == L'/'));
        return absolute ? path : ModuleDir(hModule) + path;
    }

    bool ParseColor(const Json::Value& v, COLORREF& out)
    {
        if (v.kind == Json::Value::Kind::Array)
        {
            if (v.arr.size() < 3)
                return false;
            long long r = v.arr[0].AsInt(-1);
            long long g = v.arr[1].AsInt(-1);
            long long b = v.arr[2].AsInt(-1);
            if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255)
                return false;
            out = RGB((BYTE)r, (BYTE)g, (BYTE)b);
            return true;
        }

        if (v.kind != Json::Value::Kind::String)
            return false;

        std::wstring s = v.AsString();
        size_t at = s.find_first_not_of(L" 	");
        if (at == std::wstring::npos)
            return false;
        s = s.substr(at);
        if (s[0] == L'#')
            s.erase(s.begin());
        else if (s.size() > 2 && s[0] == L'0' && (s[1] == L'x' || s[1] == L'X'))
            s.erase(0, 2);

        if (s.size() < 6)
            return false;
        s = s.substr(0, 6);

        unsigned int rgb = 0;
        for (wchar_t c : s)
        {
            int d;
            if (c >= L'0' && c <= L'9')      d = c - L'0';
            else if (c >= L'a' && c <= L'f') d = 10 + (c - L'a');
            else if (c >= L'A' && c <= L'F') d = 10 + (c - L'A');
            else return false;
            rgb = (rgb << 4) | (unsigned int)d;
        }

        out = RGB((BYTE)(rgb >> 16), (BYTE)(rgb >> 8), (BYTE)rgb);
        return true;
    }

    void ParseZoneStyle(const Json::Value& node, ZoneStyle& out)
    {
        if (node.kind != Json::Value::Kind::Object)
            return;

        if (const Json::Value* v = node.Find(L"Fill"))
            ParseColor(*v, out.fill);
        if (const Json::Value* v = node.Find(L"Line"))
            ParseColor(*v, out.line);

        if (const Json::Value* v = node.Find(L"Opacity"))
        {
            long long pct = v->AsInt(-1);
            if (pct >= 0 && pct <= 100)
                out.alpha = (BYTE)((pct * 255 + 50) / 100);
        }
        if (const Json::Value* v = node.Find(L"Alpha"))
        {
            long long a = v->AsInt(-1);
            if (a >= 0 && a <= 255)
                out.alpha = (BYTE)a;
        }
    }
}

void Config::Load(HINSTANCE hModule)
{
    *this = Config();
    m_Path = ModuleDir(hModule) + L"GalaxyATMSystem.json";

    std::ifstream file(m_Path, std::ios::binary);
    if (!file)
    {
        m_LoadError = L"config not found: " + m_Path;
        return;
    }

    std::string raw((std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());

    Json::Value root;
    if (!Json::ParseUtf8(raw, root) || root.kind != Json::Value::Kind::Object)
    {
        m_LoadError = L"invalid JSON in " + m_Path;
        return;
    }

    if (const Json::Value* v = root.Find(L"Airport"))
        m_Airport = v->AsText(m_Airport);
    if (const Json::Value* v = root.Find(L"QnhMmHg"))
        m_QnhMmHg = v->AsText(m_QnhMmHg);
    if (const Json::Value* v = root.Find(L"QnhHpa"))
        m_QnhHpa = v->AsText(m_QnhHpa);

    if (const Json::Value* positions = root.Find(L"Positions"))
    {
        if (positions->kind == Json::Value::Kind::Object)
        {
            for (const auto& [key, entry] : positions->obj)
            {
                PositionInfo info;
                if (entry.kind == Json::Value::Kind::Object)
                {
                    if (const Json::Value* d = entry.Find(L"Designation"))
                        info.Designation = d->AsString();
                    if (const Json::Value* r = entry.Find(L"Role"))
                        info.Role = r->AsString();
                }
                m_Positions[ToUpper(key)] = info;
            }
        }
    }

    if (const Json::Value* names = root.Find(L"UserNames"))
    {
        if (names->kind == Json::Value::Kind::Object)
        {
            for (const auto& [cid, name] : names->obj)
            {
                if (!name.AsString().empty())
                    m_UserNames[cid] = name.AsString();
            }
        }
    }

    if (const Json::Value* atis = root.Find(L"Atis"))
    {
        if (atis->kind == Json::Value::Kind::Object)
        {
            if (const Json::Value* v = atis->Find(L"Index"))
                m_AtisIndex = v->AsString(m_AtisIndex);
            if (const Json::Value* v = atis->Find(L"Live"))
                m_AtisLive = v->AsBool(m_AtisLive);
            if (const Json::Value* v = atis->Find(L"RefreshMinutes"))
                m_AtisRefreshMin = (int)max(1LL, min(60LL, v->AsInt(m_AtisRefreshMin)));
            if (const Json::Value* v = atis->Find(L"RefreshSeconds"))
                m_AtisRefreshSec = (int)max(15LL, min(3600LL, v->AsInt(m_AtisRefreshSec)));
            if (const Json::Value* v = atis->Find(L"TopOffset"))
                m_AtisTopOffset = (int)max(0LL, min(400LL, v->AsInt(m_AtisTopOffset)));
            if (const Json::Value* v = atis->Find(L"TextRu"))
                m_AtisTextRu = v->AsString();
            if (const Json::Value* v = atis->Find(L"TextEn"))
                m_AtisTextEn = v->AsString();
            if (m_AtisTextEn.empty())
            {
                if (const Json::Value* v = atis->Find(L"Text"))
                    m_AtisTextEn = v->AsString();
            }
        }
    }

    if (const Json::Value* apw = root.Find(L"Apw"))
    {
        if (apw->kind == Json::Value::Kind::Object)
        {
            if (const Json::Value* v = apw->Find(L"Enabled"))
                m_Apw.enabled = v->AsBool(m_Apw.enabled);
            if (const Json::Value* v = apw->Find(L"LookAheadMinutes"))
                m_Apw.lookAheadMin = (int)max(0LL, min(15LL, v->AsInt(m_Apw.lookAheadMin)));
            if (const Json::Value* v = apw->Find(L"BufferNm"))
                m_Apw.bufferNm = max(0.0, min(20.0, v->AsNumber(m_Apw.bufferNm)));
            if (const Json::Value* v = apw->Find(L"VerticalBufferFt"))
                m_Apw.verticalBufferFt = (int)max(0LL, min(5000LL, v->AsInt(m_Apw.verticalBufferFt)));
            if (const Json::Value* v = apw->Find(L"ShowZone"))
                m_Apw.showZone = v->AsBool(m_Apw.showZone);

            if (const Json::Value* v = apw->Find(L"Kinds"))
            {
                if (v->kind == Json::Value::Kind::Array)
                {
                    m_Apw.warnProhibited = m_Apw.warnRestricted = m_Apw.warnDanger = false;
                    for (const Json::Value& k : v->arr)
                    {
                        const std::wstring t = ToUpper(k.AsString());
                        if (t.empty())
                            continue;
                        if (t[0] == L'P')
                            m_Apw.warnProhibited = true;
                        else if (t[0] == L'R')
                            m_Apw.warnRestricted = true;
                        else if (t[0] == L'D')
                            m_Apw.warnDanger = true;
                    }
                }
            }
        }
    }

    if (const Json::Value* sig = root.Find(L"Sigmets"))
    {
        if (sig->kind == Json::Value::Kind::Object)
        {
            if (const Json::Value* v = sig->Find(L"Enabled"))
                m_SigmetsEnabled = v->AsBool(m_SigmetsEnabled);
            if (const Json::Value* v = sig->Find(L"RefreshMinutes"))
                m_SigmetRefreshMin = (int)max(1LL, min(180LL, v->AsInt(m_SigmetRefreshMin)));
            if (const Json::Value* v = sig->Find(L"Firs"))
            {
                if (v->kind == Json::Value::Kind::Array)
                {
                    m_SigmetFirs.clear();
                    for (const Json::Value& e : v->arr)
                    {
                        std::wstring fir = ToUpper(e.AsString());
                        if (!fir.empty())
                            m_SigmetFirs.push_back(fir);
                    }
                }
            }
        }
    }

    if (const Json::Value* zones = root.Find(L"Zones"))
    {
        std::wstring areasPath;
        if (zones->kind == Json::Value::Kind::Object)
        {
            if (const Json::Value* v = zones->Find(L"TopSkyAreas"))
                areasPath = v->AsString();
        }
        if (zones->kind == Json::Value::Kind::Object)
        {
            if (const Json::Value* v = zones->Find(L"AupUrl"))
                m_AupUrl = Json::WideToUtf8(v->AsString());
            if (const Json::Value* v = zones->Find(L"AupRefreshMinutes"))
                m_AupRefreshMin = (int)max(1LL, min(180LL, v->AsInt(m_AupRefreshMin)));
            if (const Json::Value* v = zones->Find(L"ShowNotamAreas"))
                m_ShowNotamAreas = v->AsBool(m_ShowNotamAreas);

            if (const Json::Value* v = zones->Find(L"NotamSource"))
            {
                std::wstring src = v->AsString();
                if (!src.empty() && src.compare(0, 4, L"http") != 0)
                    src = ResolvePath(hModule, src);
                m_NotamSource = Json::WideToUtf8(src);
            }
            if (const Json::Value* v = zones->Find(L"NotamRefreshMinutes"))
                m_NotamRefreshMin = (int)max(1LL, min(180LL, v->AsInt(m_NotamRefreshMin)));

            if (const Json::Value* colors = zones->Find(L"Colors"))
            {
                if (const Json::Value* v = colors->Find(L"Prohibited"))
                    ParseZoneStyle(*v, m_ZoneProhibited);
                if (const Json::Value* v = colors->Find(L"Restricted"))
                    ParseZoneStyle(*v, m_ZoneRestricted);
                if (const Json::Value* v = colors->Find(L"Danger"))
                    ParseZoneStyle(*v, m_ZoneDanger);
            }
        }

        if (!areasPath.empty())
        {
            std::wstring path = ResolvePath(hModule, areasPath);
            if (!LoadTopSkyAreas(path, m_Zones))
                m_LoadError = L"zones: cannot open " + path;
        }

        if (zones->kind == Json::Value::Kind::Object)
        {
            if (const Json::Value* v = zones->Find(L"ItemsFile"))
            {
                std::wstring name = v->AsString();
                if (!name.empty())
                {
                    std::wstring path = ResolvePath(hModule, name);
                    std::ifstream lib(path, std::ios::binary);
                    if (!lib)
                    {
                        m_LoadError = L"zones: cannot open " + path;
                    }
                    else
                    {
                        std::string body((std::istreambuf_iterator<char>(lib)),
                            std::istreambuf_iterator<char>());
                        Json::Value parsed;
                        bool ignored = true;
                        if (!Json::ParseUtf8(body, parsed)
                            || !ParseZones(parsed, m_Zones, ignored))
                            m_LoadError = L"zones: cannot read " + path;
                    }
                }
            }
        }

        ParseZones(*zones, m_Zones, m_ZonesEnabled);
    }

    m_SquawkServerUrl.clear();
    m_SquawkApiKey.clear();
    m_SquawkPollSeconds = 15;
    m_SquawkAllowSweatbox = false;
    m_SquawkDebug = false;
    if (const Json::Value* sq = root.Find(L"Squawk"))
    {
        if (sq->kind == Json::Value::Kind::Object)
        {
            bool enabled = true;
            if (const Json::Value* v = sq->Find(L"Enabled"))
                enabled = v->AsBool(true);
            if (const Json::Value* v = sq->Find(L"ServerUrl"))
                m_SquawkServerUrl = Json::WideToUtf8(v->AsString());
            if (const Json::Value* v = sq->Find(L"ApiKey"))
                m_SquawkApiKey = Json::WideToUtf8(v->AsString());

            if (const Json::Value* v = sq->Find(L"ApiKeyFile"))
            {
                std::wstring name = v->AsString();
                if (!name.empty())
                {
                    std::wstring path = ResolvePath(hModule, name);
                    std::ifstream keyFile(path, std::ios::binary);
                    if (!keyFile)
                    {
                        m_LoadError = L"squawk: cannot open " + path;
                    }
                    else
                    {
                        std::string keyText((std::istreambuf_iterator<char>(keyFile)),
                            std::istreambuf_iterator<char>());

                        size_t line = keyText.find_first_of("\r\n");
                        if (line != std::string::npos)
                            keyText.erase(line);
                        size_t from = keyText.find_first_not_of(" \t");
                        size_t to = keyText.find_last_not_of(" \t");
                        m_SquawkApiKey = (from == std::string::npos)
                            ? std::string() : keyText.substr(from, to - from + 1);
                    }
                }
            }
            if (const Json::Value* v = sq->Find(L"PollSeconds"))
                m_SquawkPollSeconds = (int)max(5LL, min(300LL, v->AsInt(m_SquawkPollSeconds)));
            if (const Json::Value* v = sq->Find(L"AllowSweatbox"))
                m_SquawkAllowSweatbox = v->AsBool(m_SquawkAllowSweatbox);
            if (const Json::Value* v = sq->Find(L"Debug"))
                m_SquawkDebug = v->AsBool(m_SquawkDebug);
            if (!enabled)
                m_SquawkServerUrl.clear();
        }
    }

    if (!m_AtisTextRu.empty() && !m_AtisTextEn.empty())
        m_AtisMessage = m_AtisTextRu + L"\n\n" + m_AtisTextEn;
    else if (!m_AtisTextRu.empty())
        m_AtisMessage = m_AtisTextRu;
    else if (!m_AtisTextEn.empty())
        m_AtisMessage = m_AtisTextEn;

}

const ZoneStyle& Config::ZoneStyleFor(ZoneKind kind) const
{
    switch (kind)
    {
    case ZoneKind::Prohibited: return m_ZoneProhibited;
    case ZoneKind::Danger:     return m_ZoneDanger;
    default:                   return m_ZoneRestricted;
    }
}

bool Config::FindPosition(const std::string& callsign,
    const std::string& positionId, PositionInfo& out) const
{
    auto tryKey = [&](const std::string& k) -> bool
    {
        if (k.empty())
            return false;
        auto it = m_Positions.find(ToUpper(Json::Utf8ToWide(k)));
        if (it == m_Positions.end())
            return false;
        out = it->second;
        return true;
    };

    if (tryKey(callsign))
        return true;
    if (tryKey(positionId))
        return true;
    return false;
}
