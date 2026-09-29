#pragma once

#include "Core/Common.h"

namespace Galaxy
{
    inline const int kCflClearedApproach = 1;
    inline const int kCflVisualApproach = 2;

    inline bool IsApproachClearance(int value)
    {
        return value == kCflClearedApproach || value == kCflVisualApproach;
    }

    inline const std::vector<int>& CflLevels(bool withApproaches)
    {
        static std::vector<int> levels, withApp;
        if (levels.empty())
        {
            for (int fl = 510; fl > 410; fl -= 20)
                levels.push_back(fl);
            for (int fl = 410; fl >= 10; fl -= 10)
                levels.push_back(fl);
            withApp = levels;
            withApp.push_back(kCflClearedApproach);
            withApp.push_back(kCflVisualApproach);
        }
        return withApproaches ? withApp : levels;
    }
    inline const int kCflRows = 9;

    inline const std::vector<int>& SpeedValues(bool mach)
    {
        static std::vector<int> kt, m;
        if (kt.empty())
        {
            for (int v = 400; v >= 100; v -= 10)
                kt.push_back(v);
            for (int v = 95; v >= 60; v--)
                m.push_back(v);
        }
        return mach ? m : kt;
    }
    inline const int kSpdRows = 3;

    inline const int kHeadingStep = 5;
    inline const int kHdgRows = 5;

    inline const std::vector<int>& HeadingValues()
    {
        static std::vector<int> values;
        if (values.empty())
            for (int hdg = 360; hdg >= kHeadingStep; hdg -= kHeadingStep)
                values.push_back(hdg);
        return values;
    }

    inline int HeadingGap(int a, int b)
    {
        const int d = abs(a - b) % 360;
        return min(d, 360 - d);
    }
    inline const int kXfrRows = 8;
}
