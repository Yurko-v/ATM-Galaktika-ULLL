#pragma once

#include "Core/Common.h"

namespace Galaxy
{
    inline const int kRcSvgW = 2068, kRcSvgH = 904;
    inline const int kRcScaleMin = 25, kRcScaleMax = 100;

    struct RcColumn { const wchar_t* title; int svgLeft, svgRight; };

    inline const RcColumn kRcColumns[] = {
        { L"КФ",       13,  134 },
        { L"Рейс",    140,  358 },
        { L"ВРЛ",     364,  485 },
        { L"S",       491,  537 },
        { L"Тип",     543,  663 },
        { L"W",       669,  717 },
        { L"CFL",     723,  842 },
        { L"Точка",   848, 1030 },
        { L"Вход",   1036, 1254 },
        { L"Точка",  1260, 1440 },
        { L"Выход",  1446, 1664 },
        { L"ВыхЭш",  1670, 1851 },
        { L"ПВО",    1857, 1953 },
        { L"Крд",    1959, 2055 },
    };
    inline const int kRcCols = (int)(sizeof(kRcColumns) / sizeof(kRcColumns[0]));

    enum
    {
        RC_KF, RC_CALLSIGN, RC_SQUAWK, RC_S, RC_TYPE, RC_W, RC_CFL,
        RC_ENTRY_POINT, RC_ENTRY, RC_EXIT_POINT, RC_EXIT, RC_EXIT_LEVEL,
        RC_PVO, RC_CRD
    };

    inline const int kRcPaneTopSvg[2]    = { 102, 462 };
    inline const int kRcPaneBottomSvg[2] = { 445, 802 };
    inline const int kRcHeadSvg = 46, kRcPlateInsetSvg = 3;
    inline const int kRcRowSvg = 46, kRcRowPitchSvg = 49;
    inline const int kRcRows = 6;

    inline const float kRcDividerSvg[] = { 844.0f, 1256.0f, 1852.5f };
    inline const float kRcDividerWSvg = 2.5f;

    inline const int    kKfMinGsKt      = 50;

    inline const float kRcCrossSvg[12][2] = {
        { 2011.27f, 64.4168f }, { 2008.58f, 61.7335f }, { 2019.32f, 51.0002f },
        { 2008.58f, 40.2668f }, { 2011.27f, 37.5835f }, { 2022.00f, 48.3168f },
        { 2032.73f, 37.5835f }, { 2035.42f, 40.2668f }, { 2024.68f, 51.0002f },
        { 2035.42f, 61.7335f }, { 2032.73f, 64.4168f }, { 2022.00f, 53.6835f },
    };

    inline std::wstring RcSortText(const SectorListRow& r, int cell)
    {
        const std::wstring& v = r.cells[cell];
        if (cell != RC_ENTRY && cell != RC_EXIT)
            return v;
        size_t a = v.find(L'/');
        return (a == std::wstring::npos) ? v : v.substr(0, a);
    }
}
