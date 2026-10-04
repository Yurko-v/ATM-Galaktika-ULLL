#include "pch.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <vector>
#include "Core/Common.h"
#include "Json.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

using namespace Galaxy;

namespace
{
    const char* const kSweatboxServersUrl = "https://data.vatsim.net/v3/sweatbox-servers.json";
    const u_short kFsdPort = 6809;

    std::string Ipv4Text(const in_addr& addr)
    {
        char text[INET_ADDRSTRLEN] = { 0 };
        return inet_ntop(AF_INET, &addr, text, sizeof(text)) != NULL ? std::string(text) : std::string();
    }

    std::string FsdServerAddress()
    {
        std::vector<BYTE> buffer;
        ULONG size = 0;
        DWORD result = ERROR_INSUFFICIENT_BUFFER;
        for (int attempt = 0; attempt < 3 && result == ERROR_INSUFFICIENT_BUFFER; attempt++)
        {
            buffer.resize(size + 4096);
            size = (ULONG)buffer.size();
            result = GetExtendedTcpTable(buffer.data(), &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_CONNECTIONS, 0);
        }
        if (result != NO_ERROR)
            return std::string();

        const MIB_TCPTABLE_OWNER_PID* table = (const MIB_TCPTABLE_OWNER_PID*)buffer.data();
        const DWORD me = GetCurrentProcessId();
        for (DWORD i = 0; i < table->dwNumEntries; i++)
        {
            const MIB_TCPROW_OWNER_PID& row = table->table[i];
            if (row.dwOwningPid != me || row.dwState != MIB_TCP_STATE_ESTAB
                || ntohs((u_short)row.dwRemotePort) != kFsdPort)
                continue;
            in_addr remote;
            remote.S_un.S_addr = row.dwRemoteAddr;
            return Ipv4Text(remote);
        }
        return std::string();
    }

    void AddAddresses(const std::string& host, std::set<std::string>& out)
    {
        in_addr numeric;
        if (inet_pton(AF_INET, host.c_str(), &numeric) == 1)
        {
            out.insert(Ipv4Text(numeric));
            return;
        }

        addrinfo hints = {};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* found = NULL;
        if (getaddrinfo(host.c_str(), NULL, &hints, &found) != 0)
        {
            Log::Warn("auth", "Sweatbox server " + host + " does not resolve");
            return;
        }
        for (const addrinfo* a = found; a != NULL; a = a->ai_next)
            out.insert(Ipv4Text(((const sockaddr_in*)a->ai_addr)->sin_addr));
        freeaddrinfo(found);
    }
}

void CGalaxyATMSystemPlugin::StartSweatboxFetch()
{
    m_sweatboxFetch.Start([this]()
        {
            std::string body;
            std::set<std::string> found;
            Json::Value root;
            if (Net::HttpGet(kSweatboxServersUrl, body, 256 * 1024, 15000)
                && Json::ParseUtf8(body, root) && root.kind == Json::Value::Kind::Array)
            {
                WSADATA wsa;
                const bool winsock = WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
                for (const Json::Value& server : root.arr)
                    if (const Json::Value* host = server.Find(L"hostname_or_ip"))
                        AddAddresses(Narrow(host->AsString()), found);
                if (winsock)
                    WSACleanup();
            }
            if (found.empty())
            {
                Log::Warn("auth", std::string("no Sweatbox servers from ") + kSweatboxServersUrl
                    + " - only the built-in addresses are known");
                return;
            }

            std::string list;
            for (const std::string& address : found)
                list += (list.empty() ? "" : ", ") + address;
            Log::Info("auth", "Sweatbox servers: " + list);

            std::lock_guard<std::mutex> lock(m_sweatboxMutex);
            m_sweatboxAddresses.insert(found.begin(), found.end());
        });
}

bool CGalaxyATMSystemPlugin::SweatboxAddress(const std::string& address) const
{
    std::lock_guard<std::mutex> lock(m_sweatboxMutex);
    return m_sweatboxAddresses.count(address) > 0;
}

void CGalaxyATMSystemPlugin::DetectSweatbox()
{
    if (GetConnectionType() != CONNECTION_TYPE_DIRECT)
    {
        m_fsdServer.clear();
        return;
    }

    if (!m_sweatboxFetchTried)
    {
        m_sweatboxFetchTried = true;
        StartSweatboxFetch();
    }

    if (m_fsdServer.empty())
    {
        m_fsdServer = FsdServerAddress();
        if (m_fsdServer.empty())
            return;
        Log::Info("auth", "EuroScope is connected to the FSD server " + m_fsdServer);
    }

    if (!TrainingSession() && SweatboxAddress(m_fsdServer))
    {
        m_trainingConnection = true;
        Log::Info("auth", m_fsdServer + " is a VATSIM Sweatbox server - training session, no LOGIN");
    }
}
