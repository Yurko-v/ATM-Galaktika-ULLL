#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>

namespace Log
{
    void Info(const char* area, const std::string& text);
    void Warn(const char* area, const std::string& text);
    void Error(const char* area, const std::string& text);

    std::string Utf8(const std::wstring& text);

    std::string SystemError(DWORD code);

    std::string Snippet(const std::string& body, size_t maxLen = 200);
}
