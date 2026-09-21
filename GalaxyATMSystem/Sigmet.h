#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>
#include <vector>
#include "EuroScopePlugIn.h"

struct Sigmet
{
    std::wstring firId;
    std::wstring firName;
    std::wstring seriesId;
    std::wstring hazard;
    std::wstring qualifier;
    std::wstring dir, spd;
    std::wstring chng;
    std::wstring raw;
    int  baseFt = -1;
    int  topFt = -1;
    long long validFrom = 0;
    long long validTo = 0;
    bool closed = true;

    std::vector<std::vector<EuroScopePlugIn::CPosition>> rings;

    std::wstring Title() const;

    std::wstring Key() const;
};

bool FetchSigmets(const std::vector<std::wstring>& firFilter, std::vector<Sigmet>& out);

bool ParseSigmets(const std::string& body,
    const std::vector<std::wstring>& firFilter, std::vector<Sigmet>& out);
