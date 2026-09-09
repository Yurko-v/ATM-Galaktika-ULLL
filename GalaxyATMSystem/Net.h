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
}
