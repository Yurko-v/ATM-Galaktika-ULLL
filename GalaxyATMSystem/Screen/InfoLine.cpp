#include "pch.h"
#include "Screen/ScreenCommon.h"
#include "Screen/Airlines.h"

using namespace Galaxy;

namespace
{
    const int kEsInfoLineId = 1004;
    const int kEsCommandLineId = 1003;

    HWND FindEsInfoLine(HWND view)
    {
        static HWND cached = NULL;
        if (cached != NULL && IsWindow(cached))
            return cached;
        cached = NULL;
        HWND root = view != NULL ? GetAncestor(view, GA_ROOT) : NULL;
        if (root == NULL)
            return NULL;
        EnumChildWindows(root, [](HWND h, LPARAM) -> BOOL
            {
                wchar_t cls[16] = L"";
                GetClassNameW(h, cls, 16);
                if (GetDlgCtrlID(h) == kEsInfoLineId && _wcsicmp(cls, L"Static") == 0
                    && GetDlgItem(GetParent(h), kEsCommandLineId) != NULL)
                {
                    cached = h;
                    return FALSE;
                }
                return TRUE;
            }, 0);
        return cached;
    }

    std::wstring UpperCase(std::wstring s)
    {
        if (!s.empty())
            CharUpperBuffW(&s[0], (DWORD)s.size());
        return s;
    }
}

void CGalaxyATMSystemRadarScreen::ShowEsInfoLine(CFlightPlan& fp, HWND view)
{
    HWND line = FindEsInfoLine(view);
    if (line == NULL || !fp.IsValid())
        return;

    CFlightPlanData fpd = fp.GetFlightPlanData();
    auto field = [](const char* s) { return s != NULL ? std::string(s) : std::string(); };

    std::wstring text = Widen(fp.GetCallsign());
    const std::wstring telephony = UpperCase(AirlineName(fp.GetCallsign()));
    if (!telephony.empty())
        text += L" (" + telephony + L")";
    const std::string pilot = field(fp.GetPilotName());
    if (!pilot.empty())
        text += L" (" + Widen(pilot.c_str()) + L")";

    text += L": " + Widen(field(fpd.GetAircraftFPType()).c_str());
    const std::string rules = field(fpd.GetPlanType());
    text += L" " + Widen(rules.empty() ? "I" : rules.substr(0, 1).c_str()) + L":";

    const std::string assigned = field(fp.GetControllerAssignedData().GetSquawk());
    CRadarTarget rt = fp.GetCorrelatedRadarTarget();
    const std::string set = rt.IsValid() && rt.GetPosition().IsValid() ? field(rt.GetPosition().GetSquawk()) : "";
    text += Widen((assigned.empty() ? set : assigned).c_str());
    if (!assigned.empty() && !set.empty() && set != assigned)
        text += L" (" + Widen(set.c_str()) + L")";

    text += L" " + Widen(field(fpd.GetOrigin()).c_str()) + L"==>" + Widen(field(fpd.GetDestination()).c_str());
    const std::string alternate = field(fpd.GetAlternate());
    if (!alternate.empty())
        text += L" (" + Widen(alternate.c_str()) + L")";

    const int rfl = fpd.GetFinalAltitude();
    if (rfl > 0)
    {
        wchar_t alt[32];
        if (rfl / 100 >= Plugin()->TransitionLevelFL())
            swprintf_s(alt, L" at FL%d", rfl / 100);
        else
            swprintf_s(alt, L" at %d ft", rfl);
        text += alt;
    }

    text += L" Route:";
    std::string word;
    for (const char* p = fpd.GetRoute(); p != NULL; p++)
    {
        if (*p == '\0' || *p == ' ')
        {
            if (!word.empty())
                text += L" " + Widen(word.c_str());
            word.clear();
            if (*p == '\0')
                break;
        }
        else
        {
            word += *p;
        }
    }

    SetWindowTextW(line, text.c_str());
}
