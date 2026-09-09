#pragma once

#include <string>
#include <map>
#include <vector>

// Static, controller-editable data that is not available from EuroScope:
// aerodrome identity / transition level and the designation+role for each
// controller position. Read once from GalaxyATMSystem.json located next to the DLL.
//
// File format (UTF-8 JSON):
//
//   {
//     "Airport": "ULLI",
//     "QnhMmHg": "760",
//     "QnhHpa": "1013",
//     "Positions": {
//       "ULLL_CTR": { "Designation": "R1", "Role": "R1 ДРУ + R1" }
//     },
//     "Atis": {
//       "Index": "Z",
//       "TextRu": "ПУЛКОВО АТИС. ИНФОРМАЦИЯ ЗУЛУ, 20:00. ...",
//       "TextEn": "ST PETERSBURG PULKOVO ATIS. INFORMATION ZULU, 20:00. ..."
//     }
//   }
//
// The АТИС window shows both languages, one after the other, the way the
// broadcast itself carries them. "Text" is still accepted as a name for the
// English one, so a config written for an earlier version keeps working.
//
// A "Sigmets" object tunes the сигмет overlay; every key is optional:
//
//   "Sigmets": {
//     "Enabled": true,
//     "RefreshMinutes": 10,
//     "Firs": [ "ULLL", "UUWV" ]
//   }
//
// "Firs" lists the FIRs whose reports are drawn, and defaults to УЛЛЛ alone -
// the feed is worldwide, and a controller working УЛЛЛ has no use for a SIGMET
// over Indonesia. Add neighbouring FIRs to see across the boundary. An
// explicitly empty list, "Firs": [], turns the filter off and draws them all.
//
// The key under "Positions" (callsign or position id) is matched against the
// controller's callsign first, then their position id.
struct PositionInfo
{
    std::wstring Designation;   // e.g. "R1"
    std::wstring Role;          // e.g. "R1 ДРУ + R1"
};

class Config
{
public:
    // Loads the config file located next to the given module (the plugin DLL).
    // Missing file / keys fall back to the defaults below; never throws.
    void Load(HINSTANCE hModule);

    const std::wstring& Airport() const { return m_Airport; }

    // QNH placeholder shown in БЛОК 5 until real METAR parsing lands (Stage 2).
    const std::wstring& QnhMmHg() const { return m_QnhMmHg; }
    const std::wstring& QnhHpa() const { return m_QnhHpa; }

    // АТИС window content. AtisMessage() is the two languages already
    // laid out as one body of text - composed once at load rather than on
    // every frame the window is drawn; the two halves are also readable on
    // their own.
    const std::wstring& AtisIndex() const { return m_AtisIndex; }
    const std::wstring& AtisMessage() const { return m_AtisMessage; }
    const std::wstring& AtisTextRu() const { return m_AtisTextRu; }
    const std::wstring& AtisTextEn() const { return m_AtisTextEn; }

    // Looks up the position by callsign first, then by position id. Returns
    // true and fills 'out' on a hit; false if neither key is configured.
    bool FindPosition(const std::string& callsign,
        const std::string& positionId, PositionInfo& out) const;

    // Сигметы. An empty FIR list means "no filter" - see the note above.
    // It is empty only if the config says so; left out, it is УЛЛЛ alone.
    bool SigmetsEnabled() const { return m_SigmetsEnabled; }
    int  SigmetRefreshMinutes() const { return m_SigmetRefreshMin; }
    const std::vector<std::wstring>& SigmetFirs() const { return m_SigmetFirs; }

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
    bool m_SigmetsEnabled = true;
    int  m_SigmetRefreshMin = 10;
    std::vector<std::wstring> m_SigmetFirs = { L"ULLL" };   // upper-cased; empty = all
    std::map<std::wstring, PositionInfo> m_Positions;   // key -> info (key upper-cased)
    std::wstring m_LoadError;
    std::wstring m_Path;
};
