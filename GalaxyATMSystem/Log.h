#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>

// -----------------------------------------------------------------------------
// GalaxyATMSystem.log, beside the DLL: every error the plugin runs into, with
// what it was doing at the time - which URL, which stage of the request, the
// Windows error code and its text, the HTTP status and the start of the body,
// the file that could not be read. EuroScope's message window is ASCII only
// and gone with the session; this is what is left to read afterwards.
//
// Lines are UTC, "2026-09-14 15:22:37.081Z  ERROR  [net]  t1234  ...". Callable
// from any thread. A line identical to one written in the last ten minutes is
// counted rather than written again - a feed that is down fails once a minute
// - and the count goes on the next copy that is written. Past 2 MB at the
// first write of a session the file is moved to GalaxyATMSystem.old.log.
// -----------------------------------------------------------------------------
namespace Log
{
    void Info(const char* area, const std::string& text);
    void Warn(const char* area, const std::string& text);
    void Error(const char* area, const std::string& text);

    // For a wide string on its way into a line - the log is UTF-8.
    std::string Utf8(const std::wstring& text);

    // "12029 ERROR_INTERNET_CANNOT_CONNECT: <what Windows says about it>".
    // WinINet's codes are looked up in wininet.dll, everything else in the system.
    std::string SystemError(DWORD code);

    // The start of a response body on one line - control characters flattened,
    // cut at 'maxLen' with "..." after it.
    std::string Snippet(const std::string& body, size_t maxLen = 200);
}
