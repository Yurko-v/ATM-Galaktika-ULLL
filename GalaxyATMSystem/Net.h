#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wininet.h>
#include <string>

#include "Log.h"

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "advapi32.lib")

namespace Net
{
    namespace Detail
    {
        inline bool SystemProxyConfigured()
        {
            const wchar_t* key = L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings";

            DWORD enabled = 0;
            DWORD size = sizeof(enabled);
            if (RegGetValueW(HKEY_CURRENT_USER, key, L"ProxyEnable", RRF_RT_REG_DWORD,
                    NULL, &enabled, &size) == ERROR_SUCCESS && enabled != 0)
                return true;

            wchar_t pac[8] = { 0 };
            DWORD pacSize = sizeof(pac);
            const LSTATUS s = RegGetValueW(HKEY_CURRENT_USER, key, L"AutoConfigURL", RRF_RT_REG_SZ,
                NULL, pac, &pacSize);
            return s == ERROR_MORE_DATA || (s == ERROR_SUCCESS && pac[0] != L'\0');
        }

        inline const char* Via(DWORD accessType)
        {
            return accessType == INTERNET_OPEN_TYPE_DIRECT ? "direct" : "system proxy settings";
        }

        inline HINTERNET Open(DWORD accessType, DWORD timeoutMs)
        {
            HINTERNET net = InternetOpenA("GalaxyATMSystem", accessType, NULL, NULL, 0);
            if (net == NULL)
                return NULL;

            DWORD timeout = timeoutMs;
            InternetSetOptionA(net, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
            InternetSetOptionA(net, INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
            InternetSetOptionA(net, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
            return net;
        }

        inline bool HttpGetVia(DWORD accessType, const std::string& url, std::string& out,
            size_t maxBytes, DWORD timeoutMs)
        {
            const std::string what = "GET " + url + " (" + Via(accessType) + ")";

            HINTERNET net = Open(accessType, timeoutMs);
            if (net == NULL)
            {
                Log::Error("net", what + ": InternetOpen failed - " + Log::SystemError(GetLastError()));
                return false;
            }

            HINTERNET req = InternetOpenUrlA(net, url.c_str(), NULL, 0,
                INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
            if (req == NULL)
            {
                const DWORD error = GetLastError();
                InternetCloseHandle(net);
                Log::Error("net", what + ": no connection - " + Log::SystemError(error));
                return false;
            }

            DWORD status = 0;
            DWORD statusLen = sizeof(status);
            const bool haveStatus = HttpQueryInfoA(req, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                &status, &statusLen, NULL) != FALSE;

            bool readFailed = false;
            DWORD readError = 0;
            char buf[4096];
            DWORD read = 0;
            for (;;)
            {
                if (!InternetReadFile(req, buf, sizeof(buf), &read))
                {
                    readFailed = true;
                    readError = GetLastError();
                    break;
                }
                if (read == 0)
                    break;
                out.append(buf, read);
                if (out.size() > maxBytes)
                    break;
            }

            InternetCloseHandle(req);
            InternetCloseHandle(net);

            if (haveStatus && status >= 400)
            {
                Log::Error("net", what + ": HTTP " + std::to_string(status) + " - " + Log::Snippet(out));
                out.clear();
                return false;
            }
            if (readFailed)
                Log::Error("net", what + ": reading the answer failed after " + std::to_string(out.size())
                    + " bytes - " + Log::SystemError(readError));
            if (out.size() > maxBytes)
                Log::Warn("net", what + ": answer cut off at " + std::to_string(out.size())
                    + " bytes (limit " + std::to_string(maxBytes) + ")");
            if (out.empty() && !readFailed)
                Log::Error("net", what + ": empty answer"
                    + (haveStatus ? " (HTTP " + std::to_string(status) + ")" : std::string()));
            return !out.empty();
        }
    }

    inline bool HttpGet(const std::string& url, std::string& out,
        size_t maxBytes = 4096, DWORD timeoutMs = 5000)
    {
        std::string got;
        if (!Detail::HttpGetVia(INTERNET_OPEN_TYPE_PRECONFIG, url, got, maxBytes, timeoutMs))
        {
            got.clear();
            if (!Detail::SystemProxyConfigured()
                || !Detail::HttpGetVia(INTERNET_OPEN_TYPE_DIRECT, url, got, maxBytes, timeoutMs))
                return false;
            Log::Warn("net", "GET " + url + ": failed through the system proxy, went through directly");
        }
        out.append(got);
        return true;
    }

    struct HttpResponse
    {
        DWORD status = 0;
        std::string body;
    };

    namespace Detail
    {
        inline bool HttpRequestVia(DWORD accessType, const char* method, const std::string& url,
            const std::string& headers, const std::string& body, HttpResponse& out,
            size_t maxBytes, DWORD timeoutMs)
        {
            const std::string what = std::string(method) + " " + url + " (" + Via(accessType) + ")";

            char host[256] = { 0 };
            char path[2048] = { 0 };
            char extra[1024] = { 0 };
            URL_COMPONENTSA uc = { 0 };
            uc.dwStructSize = sizeof(uc);
            uc.lpszHostName = host;
            uc.dwHostNameLength = sizeof(host);
            uc.lpszUrlPath = path;
            uc.dwUrlPathLength = sizeof(path);
            uc.lpszExtraInfo = extra;
            uc.dwExtraInfoLength = sizeof(extra);
            if (!InternetCrackUrlA(url.c_str(), 0, 0, &uc))
            {
                Log::Error("net", what + ": not a URL - " + Log::SystemError(GetLastError()));
                return false;
            }

            const bool https = (uc.nScheme == INTERNET_SCHEME_HTTPS);
            if (!https && uc.nScheme != INTERNET_SCHEME_HTTP)
            {
                Log::Error("net", what + ": only http:// and https:// are supported");
                return false;
            }
            std::string object = std::string(path) + extra;
            if (object.empty())
                object = "/";

            HINTERNET net = Open(accessType, timeoutMs);
            if (net == NULL)
            {
                Log::Error("net", what + ": InternetOpen failed - " + Log::SystemError(GetLastError()));
                return false;
            }

            bool ok = false;
            HINTERNET conn = InternetConnectA(net, host, uc.nPort, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
            if (conn == NULL)
            {
                Log::Error("net", what + ": InternetConnect failed - " + Log::SystemError(GetLastError()));
            }
            else
            {
                DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE
                    | INTERNET_FLAG_NO_COOKIES | INTERNET_FLAG_NO_UI;
                if (https)
                    flags |= INTERNET_FLAG_SECURE;

                HINTERNET req = HttpOpenRequestA(conn, method, object.c_str(), NULL, NULL, NULL, flags, 0);
                if (req == NULL)
                {
                    Log::Error("net", what + ": HttpOpenRequest failed - " + Log::SystemError(GetLastError()));
                }
                else
                {
                    BOOL sent = HttpSendRequestA(req,
                        headers.empty() ? NULL : headers.c_str(), (DWORD)headers.size(),
                        body.empty() ? NULL : (LPVOID)body.data(), (DWORD)body.size());

                    DWORD status = 0;
                    DWORD len = sizeof(status);
                    if (!sent)
                    {
                        Log::Error("net", what + ": no answer - " + Log::SystemError(GetLastError()));
                    }
                    else if (!HttpQueryInfoA(req, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                        &status, &len, NULL))
                    {
                        Log::Error("net", what + ": answer without a status - " + Log::SystemError(GetLastError()));
                    }
                    else
                    {
                        out.status = status;
                        char buf[4096];
                        DWORD read = 0;
                        while (InternetReadFile(req, buf, sizeof(buf), &read) && read > 0)
                        {
                            out.body.append(buf, read);
                            if (out.body.size() > maxBytes)
                            {
                                Log::Warn("net", what + ": answer cut off at " + std::to_string(out.body.size())
                                    + " bytes (limit " + std::to_string(maxBytes) + ")");
                                break;
                            }
                        }
                        ok = true;
                    }
                    InternetCloseHandle(req);
                }
                InternetCloseHandle(conn);
            }
            InternetCloseHandle(net);
            return ok;
        }
    }

    inline bool HttpRequest(const char* method, const std::string& url,
        const std::string& headers, const std::string& body, HttpResponse& out,
        size_t maxBytes = 65536, DWORD timeoutMs = 5000)
    {
        HttpResponse got;
        if (!Detail::HttpRequestVia(INTERNET_OPEN_TYPE_PRECONFIG, method, url, headers, body, got,
                maxBytes, timeoutMs))
        {
            got = HttpResponse();
            if (!Detail::SystemProxyConfigured()
                || !Detail::HttpRequestVia(INTERNET_OPEN_TYPE_DIRECT, method, url, headers, body, got,
                    maxBytes, timeoutMs))
                return false;
            Log::Warn("net", std::string(method) + " " + url + ": failed through the system proxy, went through directly");
        }
        out = got;
        return true;
    }
}
