#pragma once

#include <string>

struct AtisReport
{
    std::wstring letter;
    std::wstring text;

    bool Empty() const { return letter.empty() && text.empty(); }
};

bool FetchVatsimAtis(const std::string& icao, AtisReport& out);

bool ParseVatsimAtis(const std::string& body, const std::string& icao, AtisReport& out);
