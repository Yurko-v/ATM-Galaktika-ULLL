#include "pch.h"
#include "Screen/ScreenCommon.h"

#include <fstream>

using namespace Galaxy;

void CGalaxyATMSystemRadarScreen::OnAsrContentToBeClosed(void)
{
    delete this;
}

// Everything the screen keeps between sessions: written to the ASR by EuroScope, and to
// a file by Настройки › Файл настроек, so a setup can be carried to another profile.
std::vector<CGalaxyATMSystemRadarScreen::SavedSetting> CGalaxyATMSystemRadarScreen::CurrentSettings()
{
    std::vector<SavedSetting> out;
    auto put = [&out](const char* key, const char* description, const char* value)
    {
        out.push_back({ key, description, value });
    };
    char buf[64];
    sprintf_s(buf, "%d", m_visible ? 1 : 0);
    put("PanelVisible", "ULLL panel visible", buf);

    sprintf_s(buf, "%d", m_collapsed ? 1 : 0);
    put("PanelCollapsed", "ULLL panel collapsed", buf);

    sprintf_s(buf, "%d,%d,%d,%d,%d,%d", m_vecDistEnabled ? 1 : 0, m_vecDistKm,
        m_vecTimeEnabled ? 1 : 0, m_vecTimeMin, m_vecByPlan ? 1 : 0, m_vecShowLevel ? 1 : 0);
    put("Vectors", "distEnabled,distKm,timeEnabled,timeMin,byPlan,showLevel", buf);

    sprintf_s(buf, "%d,%d,%d,%d", (int)Plugin()->UnitAlt(), (int)Plugin()->UnitVs(),
        (int)Plugin()->UnitGs(), (int)Plugin()->UnitDist());
    put("Units", "alt,vs,gs,dist", buf);

    sprintf_s(buf, "%d,%d,%d", Plugin()->AltFilterEnabled() ? 1 : 0,
        Plugin()->AltFilterFromFL(), Plugin()->AltFilterToFL());
    put("AltFilter", "enabled,fromFL,toFL", buf);

    sprintf_s(buf, "%d,%d,%d", m_osLines, m_osSpeed ? 1 : 0, Plugin()->TagFontSize());
    put("Os", "lines,speed,fontSize", buf);

    put("FormularKind", "формуляр: auto (по позиции), ctr, app, twr",
        kFormularKindNames[(int)m_formularKindSetting]);

    sprintf_s(buf, "%d", m_rulerButton);
    put("RulerButton", "side mouse button that arms the ruler (0 = off)", buf);

    sprintf_s(buf, "%d", m_sigmetsVisible ? 1 : 0);
    put("Sigmets", "сигмет areas drawn on the radar", buf);

    sprintf_s(buf, "%d", m_zonesVisible ? 1 : 0);
    put("Zones", "запретные зоны drawn on the radar", buf);

    sprintf_s(buf, "%d", m_atisLetterOpen ? 1 : 0);
    put("AtisLetter", "АТИС letter window shown", buf);

    if (m_atisLetterPositioned)
    {
        sprintf_s(buf, "%d,%d", (int)m_atisLetterArea.left, (int)m_atisLetterArea.top);
        put("AtisLetterPos", "АТИС letter window left,top", buf);
    }

    sprintf_s(buf, "%d,%d,%d,%d", m_rcOpen ? 1 : 0, m_rcSortKey, m_rcSortAsc ? 1 : 0, m_rcScale);
    put("SectorList", "open,sortKey,sortAscending,scalePercent", buf);

    sprintf_s(buf, "%d,%d,%s", m_rcFilterBefore, m_rcFilterAfter, Narrow(m_rcFilterCallsign).c_str());
    put("SectorListFilter", "minutesBefore,minutesAfter (-1 = no limit),callsign", buf);

    sprintf_s(buf, "%d,%ld,%ld", m_rcFloating ? 1 : 0, m_rcFloatPos.x, m_rcFloatPos.y);
    put("SectorListOutside", "outside EuroScope,screenX,screenY", buf);

    sprintf_s(buf, "%d,%d,%d,%d", m_codeAll ? 1 : 0, m_codeBp ? 1 : 0,
        m_codeExtra ? 1 : 0, m_vvGain);
    put("CodeBlock", "all,bp,extra,gain", buf);

    put("CodeFilter", "ВВ1 code filter", Narrow(m_codeFilter).c_str());

    put("Language", "plugin language: rus, eng", Lang::Name(Lang::Current()));
    return out;
}

void CGalaxyATMSystemRadarScreen::OnAsrContentToBeSaved(void)
{
    for (const SavedSetting& s : CurrentSettings())
        SaveDataToAsr(s.key.c_str(), s.description.c_str(), s.value.c_str());
}

void CGalaxyATMSystemRadarScreen::OnAsrContentLoaded(bool Loaded)
{
    m_panelDirty = true;
    if (!Loaded)
        return;
    AirspaceSectors();
    ApplySettings([this](const char* key) { return GetDataFromAsr(key); });
}

// Takes what `get` has for each key of CurrentSettings; a key it returns NULL for keeps
// its value.
void CGalaxyATMSystemRadarScreen::ApplySettings(const std::function<const char*(const char*)>& get)
{
    const char* vis = get("PanelVisible");
    if (vis != NULL)
        m_visible = (atoi(vis) != 0);

    const char* collapsed = get("PanelCollapsed");
    if (collapsed != NULL)
        m_collapsed = (atoi(collapsed) != 0);

    const char* vec = get("Vectors");
    if (vec != NULL)
    {
        int de = 0, dk = 0, te = 0, tm = 0, bp = 0, sl = 0;
        if (sscanf_s(vec, "%d,%d,%d,%d,%d,%d", &de, &dk, &te, &tm, &bp, &sl) == 6)
        {
            m_vecDistEnabled = de != 0;
            if (std::find(std::begin(kDistanceSteps), std::end(kDistanceSteps), dk) != std::end(kDistanceSteps))
                m_vecDistKm = dk;
            m_vecTimeEnabled = (te != 0) && !m_vecDistEnabled;
            if (std::find(std::begin(kTimeSteps), std::end(kTimeSteps), tm) != std::end(kTimeSteps))
                m_vecTimeMin = tm;
            m_vecByPlan = bp != 0;
            m_vecShowLevel = sl != 0;
        }
    }

    const char* units = get("Units");
    if (units != NULL)
    {
        int a = 0, v = 0, g = 0, d = 0;
        if (sscanf_s(units, "%d,%d,%d,%d", &a, &v, &g, &d) == 4)
        {
            if (a >= (int)AltUnit::FL && a <= (int)AltUnit::FLM)
                Plugin()->SetUnitAlt((AltUnit)a);
            if (v == (int)VsUnit::FtMin || v == (int)VsUnit::MS)
                Plugin()->SetUnitVs((VsUnit)v);
            if (g == (int)GsUnit::Knots || g == (int)GsUnit::Kmh)
                Plugin()->SetUnitGs((GsUnit)g);
            if (d == (int)DistUnit::NM || d == (int)DistUnit::Km)
                Plugin()->SetUnitDist((DistUnit)d);
        }
    }

    const char* altFilter = get("AltFilter");
    if (altFilter != NULL)
    {
        int en = 0, from = 0, to = 0;
        if (sscanf_s(altFilter, "%d,%d,%d", &en, &from, &to) == 3)
        {
            Plugin()->SetAltFilterEnabled(en != 0);
            Plugin()->SetAltFilterFromFL(max(0, min(999, from)));
            Plugin()->SetAltFilterToFL(max(0, min(999, to)));
        }
    }

    const char* os = get("Os");
    if (os != NULL)
    {
        int lines = 0, speed = 0, size = 0;
        if (sscanf_s(os, "%d,%d,%d", &lines, &speed, &size) >= 2)
        {
            m_osLines = (lines == 3) ? 3 : 2;
            m_osSpeed = speed != 0;
            for (int step : kFontSizeSteps)
                if (step == size)
                    Plugin()->SetTagFontSize(size);
        }
    }

    const char* formularKind = get("FormularKind");
    if (formularKind != NULL)
    {
        for (int i = 0; i < (int)_countof(kFormularKindNames); i++)
            if (strcmp(formularKind, kFormularKindNames[i]) == 0)
                m_formularKindSetting = (FormularKindSetting)i;
    }

    const char* codes = get("CodeBlock");
    if (codes != NULL)
    {
        int all = 0, bp = 0, extra = 0, gain = 0;
        if (sscanf_s(codes, "%d,%d,%d,%d", &all, &bp, &extra, &gain) == 4)
        {
            m_codeAll = all != 0;
            m_codeBp = bp != 0;
            m_codeExtra = extra != 0;
            m_vvGain = max(0, min(100, gain));
        }
    }

    const char* rulerBtn = get("RulerButton");
    if (rulerBtn != NULL)
    {
        int vk = atoi(rulerBtn);
        if (vk == 0 || vk == VK_XBUTTON1 || vk == VK_XBUTTON2)
            m_rulerButton = vk;
    }

    const char* sigmets = get("Sigmets");
    if (sigmets != NULL)
        m_sigmetsVisible = (atoi(sigmets) != 0);

    const char* zones = get("Zones");
    if (zones != NULL)
        m_zonesVisible = (atoi(zones) != 0);

    const char* atisLetter = get("AtisLetter");
    if (atisLetter != NULL)
        m_atisLetterOpen = (atoi(atisLetter) != 0);

    const char* atisLetterPos = get("AtisLetterPos");
    int atisX = 0, atisY = 0;
    if (atisLetterPos != NULL && sscanf_s(atisLetterPos, "%d,%d", &atisX, &atisY) == 2)
    {
        m_atisLetterArea = { atisX, atisY, atisX, atisY };
        m_atisLetterPositioned = true;
    }

    const char* codeFilter = get("CodeFilter");
    if (codeFilter != NULL)
        m_codeFilter = Widen(codeFilter);

    const char* language = get("Language");
    if (language != NULL && strcmp(language, Lang::Name(Lang::Id::En)) == 0)
        Lang::Set(Lang::Id::En);
    else if (language != NULL && strcmp(language, Lang::Name(Lang::Id::Ru)) == 0)
        Lang::Set(Lang::Id::Ru);

    const char* rc = get("SectorList");
    if (rc != NULL)
    {
        int open = 0, key = 0, asc = 1, scale = m_rcScale;
        if (sscanf_s(rc, "%d,%d,%d,%d", &open, &key, &asc, &scale) >= 3)
        {
            m_rcOpen = (open != 0);
            m_rcSortKey = (key >= 0 && key < kRcCols) ? key : RC_CALLSIGN;
            m_rcSortAsc = (asc != 0);
            m_rcScale = max(kRcScaleMin, min(kRcScaleMax, scale));
        }
    }

    const char* rcFilter = get("SectorListFilter");
    if (rcFilter != NULL)
    {
        int before = -1, after = -1;
        char callsign[16] = { 0 };
        if (sscanf_s(rcFilter, "%d,%d,%15s", &before, &after, callsign, (unsigned)sizeof(callsign)) >= 2)
        {
            m_rcFilterBefore = max(-1, min(9999, before));
            m_rcFilterAfter = max(-1, min(9999, after));
            m_rcFilterCallsign = Upper(Widen(callsign));
        }
    }

    const char* rcOutside = get("SectorListOutside");
    if (rcOutside != NULL)
    {
        int outside = 0;
        long x = 0, y = 0;
        if (sscanf_s(rcOutside, "%d,%ld,%ld", &outside, &x, &y) == 3)
        {
            m_rcFloating = (outside != 0);
            m_rcFloatPos = { x, y };
        }
    }
}

// The file is the ASR's own key and value per line, each under its description.
bool CGalaxyATMSystemRadarScreen::ExportSettings(const std::wstring& path)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
        return false;
    file << "; Galaxy ATM System " << CGalaxyATMSystemPlugin::Version() << " settings\r\n";
    for (const SavedSetting& s : CurrentSettings())
        file << "\r\n; " << s.description << "\r\n" << s.key << "=" << s.value << "\r\n";
    return (bool)file;
}

// How many settings the file had; -1 when it cannot be read.
int CGalaxyATMSystemRadarScreen::ImportSettings(const std::wstring& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return -1;
    std::map<std::string, std::string> values;
    std::string line;
    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const size_t eq = line.find('=');
        if (line.empty() || line[0] == ';' || eq == std::string::npos || eq == 0)
            continue;
        values[line.substr(0, eq)] = line.substr(eq + 1);
    }

    int found = 0;
    for (const SavedSetting& s : CurrentSettings())
        found += values.count(s.key) ? 1 : 0;
    if (found == 0)
        return 0;
    ApplySettings([&values](const char* key) -> const char*
    {
        auto it = values.find(key);
        return it == values.end() ? NULL : it->second.c_str();
    });
    if (!m_rcFloating)
        m_rcFloat.Hide();
    m_panelDirty = true;
    return found;
}
