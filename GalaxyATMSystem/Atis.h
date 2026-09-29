#pragma once

#include <map>
#include <string>
#include <vector>

struct AtisReport
{
    std::wstring letter;
    std::wstring text;

    bool Empty() const { return letter.empty() && text.empty(); }
};

bool FetchVatsimAtis(const std::vector<std::string>& airports, std::map<std::string, AtisReport>& out);

bool ParseVatsimAtis(const std::string& body, const std::vector<std::string>& airports,
    std::map<std::string, AtisReport>& out);
