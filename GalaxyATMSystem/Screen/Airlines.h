#pragma once

#include "Core/Common.h"

namespace Galaxy
{
    inline const std::wstring& AirlineName(const char* callsign)
    {
        static std::map<std::string, std::wstring> names;
        static bool loaded = false;
        static const std::wstring none;
        if (!loaded)
        {
            loaded = true;
            HMODULE topsky = GetModuleHandleW(L"TopSky.dll");
            wchar_t path[MAX_PATH] = {};
            if (topsky != NULL && GetModuleFileNameW(topsky, path, MAX_PATH) != 0)
            {
                std::wstring dir = path;
                dir = dir.substr(0, dir.find_last_of(L"\\/") + 1);
                std::vector<std::wstring> files = { dir + L"ICAO_Airlines.txt",
                    dir + L"..\\..\\ICAO\\ICAO_Airlines.txt", dir + L"..\\..\\Data\\ICAO_Airlines.txt" };
                for (size_t i = 0; i < files.size() && i < 8; i++)
                {
                    const std::wstring file = files[i];
                    FILE* f = NULL;
                    if (_wfopen_s(&f, file.c_str(), L"rb") != 0 || f == NULL)
                        continue;
                    char line[512];
                    while (fgets(line, sizeof(line), f) != NULL)
                    {
                        if (line[0] == ';')
                            continue;
                        if (strchr(line, '\t') == NULL && names.empty())
                        {
                            std::wstring target = Widen(line);
                            while (!target.empty() && iswspace(target.back()))
                                target.pop_back();
                            if (target.size() > 4 && _wcsicmp(target.c_str() + target.size() - 4, L".txt") == 0)
                            {
                                const bool absolute = target.size() > 1 && (target[1] == L':' || target[0] == L'\\');
                                const std::wstring base = file.substr(0, file.find_last_of(L"\\/") + 1);
                                files.insert(files.begin() + i + 1, absolute ? target : base + target);
                            }
                            continue;
                        }
                        std::vector<std::string> fields;
                        std::string cur;
                        for (const char* p = line; *p != '\0' && *p != '\r' && *p != '\n'; p++)
                        {
                            if (*p == '\t') { fields.push_back(cur); cur.clear(); }
                            else cur += *p;
                        }
                        fields.push_back(cur);
                        if (fields.size() < 3 || fields[0].size() != 3 || fields[2].empty())
                            continue;
                        std::wstring name = Widen(fields[2].c_str());
                        bool wordStart = true;
                        for (wchar_t& ch : name)
                        {
                            ch = wordStart ? towupper(ch) : towlower(ch);
                            wordStart = !iswalpha(ch);
                        }
                        names.emplace(fields[0], name);
                    }
                    fclose(f);
                    Log::Info("formular", Log::Utf8(file) + ": " + std::to_string(names.size()) + " airlines");
                    if (!names.empty())
                        break;
                }
            }
        }
        if (callsign == NULL || strlen(callsign) < 4)
            return none;
        std::string code(callsign, 3);
        for (char& ch : code)
        {
            if (!isalpha((unsigned char)ch))
                return none;
            ch = (char)toupper((unsigned char)ch);
        }
        auto it = names.find(code);
        return it != names.end() ? it->second : none;
    }
}
