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

    // Directory containing the loaded module, with trailing backslash.
    std::wstring ModuleDir(HINSTANCE hModule)
    {
        wchar_t path[MAX_PATH] = { 0 };
        GetModuleFileNameW(hModule, path, MAX_PATH);
        std::wstring p(path);
        size_t slash = p.find_last_of(L"\\/");
        return (slash == std::wstring::npos) ? std::wstring() : p.substr(0, slash + 1);
    }

    // The JSON reader itself lives in Json.h - the SIGMET feed needs one too,
    // and needs arrays, which the config file never did.
}

void Config::Load(HINSTANCE hModule)
{
    m_Positions.clear();
    m_LoadError.clear();
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

    // AsText, not AsString: these three read naturally as bare numbers too,
    // and a config file that leaves the quotes off a QNH should still work.
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

    if (const Json::Value* atis = root.Find(L"Atis"))
    {
        if (atis->kind == Json::Value::Kind::Object)
        {
            if (const Json::Value* v = atis->Find(L"Index"))
                m_AtisIndex = v->AsString(m_AtisIndex);
            if (const Json::Value* v = atis->Find(L"TextRu"))
                m_AtisTextRu = v->AsString();
            if (const Json::Value* v = atis->Find(L"TextEn"))
                m_AtisTextEn = v->AsString();
            // "Text" is what the English half used to be called.
            if (m_AtisTextEn.empty())
            {
                if (const Json::Value* v = atis->Find(L"Text"))
                    m_AtisTextEn = v->AsString();
            }
        }
    }

    // Сигметы: which of them to draw, and how often to go and look.
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
                    // Replaces the default rather than adding to it, so that
                    // "Firs": [] is a way of asking for no filter at all and
                    // not just a list that failed to override anything.
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

    // The broadcast carries both languages one after the other, and so does
    // the window. With only one of them configured it stands alone - a lone
    // heading over a single block would say nothing - and with neither, the
    // default placeholder is left in place to point at the config file.
    if (!m_AtisTextRu.empty() && !m_AtisTextEn.empty())
        m_AtisMessage = m_AtisTextRu + L"\n\n" + m_AtisTextEn;
    else if (!m_AtisTextRu.empty())
        m_AtisMessage = m_AtisTextRu;
    else if (!m_AtisTextEn.empty())
        m_AtisMessage = m_AtisTextEn;

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
