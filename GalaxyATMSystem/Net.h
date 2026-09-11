#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wininet.h>
#include <string>

#pragma comment(lib, "wininet.lib")

// -----------------------------------------------------------------------------
// The one HTTP(S) fetch the plugin needs, shared by everything that reads from
// the internet: the airport's METAR and the SIGMET feed.
//
// Blocking, so it belongs on a worker thread - but bounded by a timeout on
// every stage and by a hard cap on the response, so a worker can never hang
// around waiting to be joined and a runaway response can never eat memory.
// -----------------------------------------------------------------------------
namespace Net
{
    inline bool HttpGet(const std::string& url, std::string& out,
        size_t maxBytes = 4096, DWORD timeoutMs = 5000)
    {
        HINTERNET net = InternetOpenA("GalaxyATMSystem", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
        if (net == NULL)
            return false;

        DWORD timeout = timeoutMs;
        InternetSetOptionA(net, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
        InternetSetOptionA(net, INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
        InternetSetOptionA(net, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

        HINTERNET req = InternetOpenUrlA(net, url.c_str(), NULL, 0,
            INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
        if (req == NULL)
        {
            InternetCloseHandle(net);
            return false;
        }

        char buf[4096];
        DWORD read = 0;
        while (InternetReadFile(req, buf, sizeof(buf), &read) && read > 0)
        {
            out.append(buf, read);
            if (out.size() > maxBytes)
                break;
        }

        InternetCloseHandle(req);
        InternetCloseHandle(net);
        return !out.empty();
    }

    struct HttpResponse
    {
        DWORD status = 0;
        std::string body;
    };

    // A request with a method, headers and a body, and the status code back.
    // The squawk server says no with a status and a JSON body worth reading
    // (409 "pool_empty", 401 for a wrong key), where HttpGet only knows whether
    // anything came back. Returns false only when no HTTP answer arrived at all.
    // Headers are "Name: value\r\n" lines.
    inline bool HttpRequest(const char* method, const std::string& url,
        const std::string& headers, const std::string& body, HttpResponse& out,
        size_t maxBytes = 65536, DWORD timeoutMs = 5000)
    {
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
            return false;

        const bool https = (uc.nScheme == INTERNET_SCHEME_HTTPS);
        if (!https && uc.nScheme != INTERNET_SCHEME_HTTP)
            return false;
        std::string object = std::string(path) + extra;
        if (object.empty())
            object = "/";

        HINTERNET net = InternetOpenA("GalaxyATMSystem", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
        if (net == NULL)
            return false;

        DWORD timeout = timeoutMs;
        InternetSetOptionA(net, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
        InternetSetOptionA(net, INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
        InternetSetOptionA(net, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

        bool ok = false;
        HINTERNET conn = InternetConnectA(net, host, uc.nPort, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
        if (conn != NULL)
        {
            DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE
                | INTERNET_FLAG_NO_COOKIES | INTERNET_FLAG_NO_UI;
            if (https)
                flags |= INTERNET_FLAG_SECURE;

            HINTERNET req = HttpOpenRequestA(conn, method, object.c_str(), NULL, NULL, NULL, flags, 0);
            if (req != NULL)
            {
                BOOL sent = HttpSendRequestA(req,
                    headers.empty() ? NULL : headers.c_str(), (DWORD)headers.size(),
                    body.empty() ? NULL : (LPVOID)body.data(), (DWORD)body.size());

                DWORD status = 0;
                DWORD len = sizeof(status);
                if (sent && HttpQueryInfoA(req, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                    &status, &len, NULL))
                {
                    out.status = status;
                    char buf[4096];
                    DWORD read = 0;
                    while (InternetReadFile(req, buf, sizeof(buf), &read) && read > 0)
                    {
                        out.body.append(buf, read);
                        if (out.body.size() > maxBytes)
                            break;
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
