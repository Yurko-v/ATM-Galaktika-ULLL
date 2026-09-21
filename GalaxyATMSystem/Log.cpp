#include "pch.h"
#include "Log.h"

#include <cstdio>
#include <fstream>
#include <map>
#include <mutex>

extern HINSTANCE g_hModule;

namespace
{
    const ULONGLONG kRepeatWindowMs = 10 * 60 * 1000;
    const size_t    kMaxRepeatKeys  = 500;
    const ULONGLONG kRotateBytes    = 2 * 1024 * 1024;

    struct Repeat
    {
        ULONGLONG writtenTick;
        int suppressed;
    };

    std::mutex& Mutex()
    {
        static std::mutex m;
        return m;
    }

    std::map<std::string, Repeat>& Repeats()
    {
        static std::map<std::string, Repeat> r;
        return r;
    }

    std::wstring& Path()
    {
        static std::wstring p;
        return p;
    }

    bool g_opened = false;

    std::wstring LogPathBesideDll()
    {
        wchar_t path[MAX_PATH] = { 0 };
        if (g_hModule == NULL || GetModuleFileNameW(g_hModule, path, MAX_PATH) == 0)
            return std::wstring();
        std::wstring p(path);
        size_t slash = p.find_last_of(L"\\/");
        if (slash == std::wstring::npos)
            return std::wstring();
        return p.substr(0, slash + 1) + L"GalaxyATMSystem.log";
    }

    void Write(const char* level, const char* area, const std::string& text)
    {
        std::lock_guard<std::mutex> lock(Mutex());

        std::wstring& path = Path();
        if (!g_opened)
        {
            g_opened = true;
            path = LogPathBesideDll();

            WIN32_FILE_ATTRIBUTE_DATA info;
            if (!path.empty() && GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info))
            {
                ULONGLONG size = ((ULONGLONG)info.nFileSizeHigh << 32) | info.nFileSizeLow;
                if (size > kRotateBytes)
                {
                    std::wstring old = path.substr(0, path.size() - 4) + L".old.log";
                    MoveFileExW(path.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING);
                }
            }
        }
        if (path.empty())
            return;

        const std::string key = std::string(level) + "|" + area + "|" + text;
        const ULONGLONG now = GetTickCount64();
        std::map<std::string, Repeat>& repeats = Repeats();
        int suppressed = 0;
        auto it = repeats.find(key);
        if (it != repeats.end())
        {
            if (now - it->second.writtenTick < kRepeatWindowMs)
            {
                it->second.suppressed++;
                return;
            }
            suppressed = it->second.suppressed;
            it->second = { now, 0 };
        }
        else
        {
            if (repeats.size() >= kMaxRepeatKeys)
                repeats.clear();
            repeats[key] = { now, 0 };
        }

        std::ofstream file(path, std::ios::app | std::ios::binary);
        if (!file)
            return;

        SYSTEMTIME st;
        GetSystemTime(&st);
        char head[96];
        sprintf_s(head, "%04u-%02u-%02u %02u:%02u:%02u.%03uZ  %s  [%s]  t%lu  ",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
            level, area, GetCurrentThreadId());

        file << head << text;
        if (suppressed > 0)
            file << "  (repeated " << suppressed << " more times before this)";
        file << "\r\n";
    }
}

namespace Log
{
    void Info(const char* area, const std::string& text)  { Write("INFO ", area, text); }
    void Warn(const char* area, const std::string& text)  { Write("WARN ", area, text); }
    void Error(const char* area, const std::string& text) { Write("ERROR", area, text); }

    std::string Utf8(const std::wstring& text)
    {
        if (text.empty())
            return std::string();
        int n = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), NULL, 0, NULL, NULL);
        std::string out(n > 0 ? n : 0, '\0');
        if (n > 0)
            WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), &out[0], n, NULL, NULL);
        return out;
    }

    std::string SystemError(DWORD code)
    {
        static const struct { DWORD code; const char* name; } kNames[] = {
            { 12002, "ERROR_INTERNET_TIMEOUT" },
            { 12005, "ERROR_INTERNET_INVALID_URL" },
            { 12006, "ERROR_INTERNET_UNRECOGNIZED_SCHEME" },
            { 12007, "ERROR_INTERNET_NAME_NOT_RESOLVED" },
            { 12029, "ERROR_INTERNET_CANNOT_CONNECT" },
            { 12030, "ERROR_INTERNET_CONNECTION_ABORTED" },
            { 12031, "ERROR_INTERNET_CONNECTION_RESET" },
            { 12037, "ERROR_INTERNET_SEC_CERT_DATE_INVALID" },
            { 12038, "ERROR_INTERNET_SEC_CERT_CN_INVALID" },
            { 12045, "ERROR_INTERNET_INVALID_CA" },
            { 12152, "ERROR_HTTP_INVALID_SERVER_RESPONSE" },
            { 12157, "ERROR_INTERNET_SECURITY_CHANNEL_ERROR" },
            { 12175, "ERROR_INTERNET_DECODING_FAILED" },
        };

        std::string out = std::to_string(code);
        for (const auto& n : kNames)
        {
            if (n.code == code)
            {
                out += " ";
                out += n.name;
                break;
            }
        }

        DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS;
        HMODULE source = NULL;
        if (code >= 12000 && code < 13000)
            source = GetModuleHandleW(L"wininet.dll");
        flags |= (source != NULL) ? FORMAT_MESSAGE_FROM_HMODULE : FORMAT_MESSAGE_FROM_SYSTEM;

        wchar_t* text = NULL;
        DWORD len = FormatMessageW(flags, source, code, 0, (LPWSTR)&text, 0, NULL);
        if (len > 0 && text != NULL)
        {
            std::wstring w(text, len);
            while (!w.empty() && (w.back() == L'\r' || w.back() == L'\n' || w.back() == L' ' || w.back() == L'.'))
                w.pop_back();
            out += ": " + Utf8(w);
        }
        if (text != NULL)
            LocalFree(text);
        return out;
    }

    std::string Snippet(const std::string& body, size_t maxLen)
    {
        std::string out;
        for (char c : body)
        {
            if (out.size() >= maxLen)
            {
                out += "...";
                break;
            }
            out += ((unsigned char)c < 0x20) ? ' ' : c;
        }
        return out.empty() ? "(empty body)" : out;
    }
}
