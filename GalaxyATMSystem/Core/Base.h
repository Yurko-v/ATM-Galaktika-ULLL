#pragma once

#include "GalaxyATMSystem.h"

#include <shellapi.h>
#pragma comment(lib, "shell32.lib")

#include <string>
#include <map>
#include <set>
#include <cstdio>
#include <cwchar>
#include <cmath>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <functional>
#include <memory>

#include "Net.h"
#include "Log.h"
#include "Lang.h"
#include "Geometry.h"

#pragma comment(lib, "msimg32.lib")

#include <objidl.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace EuroScopePlugIn;

extern CGalaxyATMSystemPlugin* g_plugin;

namespace Galaxy
{
    inline const int kDistanceSteps[] = { 5, 10, 15, 20, 25, 30, 35, 40, 45, 50 };
    inline const int kDistanceStepsCount = sizeof(kDistanceSteps) / sizeof(kDistanceSteps[0]);
    inline const int kTimeSteps[] = { 1, 2, 3, 4, 5, 10, 15 };
    inline const int kTimeStepsCount = sizeof(kTimeSteps) / sizeof(kTimeSteps[0]);

    inline const int kAtisLinePx = 18;
    inline const int kFontSizeSteps[] = { 8, 9, 10, 11, 12, 13, 14, 16 };
    inline const int kFontSizeStepsCount = sizeof(kFontSizeSteps) / sizeof(kFontSizeSteps[0]);

    inline std::map<UINT_PTR, CGalaxyATMSystemRadarScreen*> g_timers;

    inline std::map<UINT_PTR, CGalaxyATMSystemRadarScreen*> g_pollTimers;

    inline std::set<CGalaxyATMSystemRadarScreen*> g_screens;

    inline const int kVchCtlAnnotation      = 3;
    inline const int kTopSkySpeedAnnotation = 7;
    inline const int kEnglishAnnotation     = 8;

    inline bool ShiftHeldInEuroScope()
    {
        HWND fg = GetForegroundWindow();
        DWORD pid = 0;
        if (fg != NULL)
            GetWindowThreadProcessId(fg, &pid);
        return pid == GetCurrentProcessId() && (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    }

    inline ULONGLONG SpreadMs(const std::string& key, ULONGLONG periodMs)
    {
        return std::hash<std::string>()(key) % periodMs;
    }

    inline bool Fresh(ULONGLONG& tick, const std::string& key, ULONGLONG periodMs)
    {
        const ULONGLONG now = GetTickCount64();
        if (tick != 0 && now - tick < periodMs)
            return true;
        tick = (tick == 0) ? now - SpreadMs(key, periodMs) : now;
        return false;
    }

    inline const ULONGLONG kApwRecheckMs = 1000;

    inline const int       kFeedPollSeconds  = 15;
    inline const int       kNamePollSeconds  = 60;
    inline const ULONGLONG kLoginRetryMs     = kFeedPollSeconds * 1000;
    inline const ULONGLONG kLoginWaitMs      = 3 * 60 * 1000;

    template <class T>
    void KeepOnly(std::map<std::string, T>& m, const std::set<std::string>& live)
    {
        for (auto it = m.begin(); it != m.end(); )
            it = live.count(it->first) ? std::next(it) : m.erase(it);
    }

    inline void KeepOnly(std::set<std::string>& s, const std::set<std::string>& live)
    {
        for (auto it = s.begin(); it != s.end(); )
            it = live.count(*it) ? std::next(it) : s.erase(it);
    }

    inline bool OnControllerPosition(const CController& me)
    {
        if (!me.IsValid() || !me.IsController() || me.GetFacility() < 1)
            return false;
        const char* callsign = me.GetCallsign();
        if (callsign == NULL || *callsign == '\0')
            return false;
        size_t len = strlen(callsign);
        return !(len >= 4 && _stricmp(callsign + len - 4, "_OBS") == 0);
    }
}
