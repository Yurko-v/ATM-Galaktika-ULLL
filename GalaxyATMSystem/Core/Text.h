#pragma once

#include "Core/Base.h"

namespace Galaxy
{
    inline std::wstring Widen(const char* s)
    {
        if (s == NULL || *s == '\0')
            return std::wstring();
        int n = MultiByteToWideChar(CP_ACP, 0, s, -1, NULL, 0);
        std::wstring w(n ? n - 1 : 0, L'\0');
        if (n > 1)
            MultiByteToWideChar(CP_ACP, 0, s, -1, &w[0], n - 1);
        return w;
    }

    inline std::string Narrow(const std::wstring& w)
    {
        if (w.empty())
            return std::string();
        int n = WideCharToMultiByte(CP_ACP, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
        std::string s(n, '\0');
        WideCharToMultiByte(CP_ACP, 0, w.c_str(), (int)w.size(), &s[0], n, NULL, NULL);
        return s;
    }

    inline std::wstring Upper(std::wstring v)
    {
        std::transform(v.begin(), v.end(), v.begin(), ::towupper);
        return v;
    }

    inline std::wstring TrimSpaces(const std::wstring& s)
    {
        const size_t from = s.find_first_not_of(L" \t");
        if (from == std::wstring::npos)
            return std::wstring();
        return s.substr(from, s.find_last_not_of(L" \t") - from + 1);
    }
}
