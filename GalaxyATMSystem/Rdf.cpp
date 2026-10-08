#include "pch.h"
#include "Rdf.h"
#include "Json.h"
#include "Log.h"

#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

extern HINSTANCE g_hModule;

namespace
{
    // The names AFV looks for. They have to match the RDF plugin exactly, because
    // AFV finds the window by class and title and will not look for anything else.
    const char* const kBridgeClass = "RDFHiddenWindowClass";
    const char* const kBridgeTitle = "RDFHiddenWindow";

    // AFV tags its payload with this, and the RDF plugin drops anything else.
    const ULONG_PTR kBridgeTag = 666;

    const DWORD kReconnectMs = 5000;

    std::string Narrow(const std::wstring& w)
    {
        if (w.empty())
            return std::string();
        const int n = WideCharToMultiByte(CP_ACP, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
        std::string s(n, '\0');
        WideCharToMultiByte(CP_ACP, 0, w.c_str(), (int)w.size(), &s[0], n, NULL, NULL);
        return s;
    }

    std::wstring Widen(const std::string& s)
    {
        if (s.empty())
            return std::wstring();
        const int n = MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.size(), NULL, 0);
        std::wstring w(n, L'\0');
        MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.size(), &w[0], n);
        return w;
    }

    std::string Upper(std::string s)
    {
        for (char& c : s)
            c = (char)toupper((unsigned char)c);
        return s;
    }

    std::string Lower(std::string s)
    {
        for (char& c : s)
            c = (char)tolower((unsigned char)c);
        return s;
    }

    // TrackAudio spells its message types kRxBegin, kTxEnd and so on; the SDK
    // documentation also shows them without the leading k. Accept either.
    std::string EventName(const std::wstring& type)
    {
        std::string s = Narrow(type);
        if (s.size() > 1 && s[0] == 'k')
            s.erase(s.begin());
        return Lower(s);
    }

    std::string CallsignOf(const Json::Value& value)
    {
        const Json::Value* cs = value.Find(L"callsign");
        return cs != NULL ? Upper(Narrow(cs->AsString())) : std::string();
    }
}

RdfClient::~RdfClient()
{
    Stop();
}

void RdfClient::Start(const std::string& endpoint)
{
    Stop();

    if (!endpoint.empty())
    {
        const size_t colon = endpoint.find_last_of(':');
        const std::string host = colon == std::string::npos ? endpoint : endpoint.substr(0, colon);
        const int port = colon == std::string::npos ? 0 : atoi(endpoint.c_str() + colon + 1);
        if (!host.empty())
            m_host = Widen(host);
        if (port > 0 && port < 65536)
            m_port = port;
    }

    m_stop = false;
    OpenBridge();
    m_ws = std::thread([this]() { WebSocketThread(); });

    Log::Info("rdf", "started - TrackAudio at " + Narrow(m_host) + ":" + std::to_string(m_port)
        + ", AFV bridge window " + (m_window != NULL ? "open" : "unavailable"));
}

void RdfClient::Stop()
{
    if (!m_ws.joinable() && m_window == NULL)
        return;

    m_stop = true;
    CloseWebSocket();
    if (m_ws.joinable())
        m_ws.join();
    CloseBridge();

    std::lock_guard<std::mutex> lock(m_mutex);
    m_fromBridge.clear();
    m_fromTrackAudio.clear();
    m_selfTx = false;
    Bump();
}

RdfClient::State RdfClient::Snapshot() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    State out;
    out.heard = m_fromBridge;
    out.heard.insert(m_fromTrackAudio.begin(), m_fromTrackAudio.end());
    out.selfTx = m_selfTx;
    return out;
}

//
//  Audio for VATSIM standalone - the hidden window
//

LRESULT CALLBACK RdfClient::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_CREATE)
    {
        CREATESTRUCTA* cs = reinterpret_cast<CREATESTRUCTA*>(lParam);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return 0;
    }

    if (msg == WM_COPYDATA)
    {
        RdfClient* self = reinterpret_cast<RdfClient*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        COPYDATASTRUCT* data = reinterpret_cast<COPYDATASTRUCT*>(lParam);
        if (self != NULL && data != NULL && data->dwData == kBridgeTag && data->lpData != NULL)
            self->OnBridgeMessage(reinterpret_cast<const char*>(data->lpData), data->cbData);
        return TRUE;
    }

    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

void RdfClient::OpenBridge()
{
    // AFV finds its target with FindWindow, which returns one window only. If the
    // RDF plugin is loaded as well, one of the two gets every message and the other
    // gets none, so say so in the log rather than leave the controller guessing.
    if (FindWindowA(kBridgeClass, kBridgeTitle) != NULL)
    {
        m_bridgeTaken = true;
        Log::Warn("rdf", "another plugin already owns the AFV bridge window - unload RDFPlugin.dll,"
            " otherwise only one of the two will see transmissions from Audio for VATSIM standalone");
    }

    WNDCLASSA wc = {};
    wc.lpfnWndProc = &RdfClient::WndProc;
    wc.hInstance = g_hModule;
    wc.lpszClassName = kBridgeClass;
    if (RegisterClassA(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        Log::Error("rdf", std::string("RegisterClass failed - ") + Log::SystemError(GetLastError()));
        return;
    }
    m_classRegistered = true;

    m_window = CreateWindowA(kBridgeClass, kBridgeTitle, 0, 0, 0, 0, 0,
        NULL, NULL, g_hModule, this);
    if (m_window == NULL)
        Log::Error("rdf", std::string("the AFV bridge window did not open - ")
            + Log::SystemError(GetLastError()));
}

void RdfClient::CloseBridge()
{
    if (m_window != NULL)
    {
        DestroyWindow(m_window);
        m_window = NULL;
    }
    if (m_classRegistered)
    {
        UnregisterClassA(kBridgeClass, g_hModule);
        m_classRegistered = false;
    }
    m_bridgeTaken = false;
}

void RdfClient::OnBridgeMessage(const char* payload, size_t length)
{
    // A colon separated list of everyone transmitting; empty ends the transmission.
    std::string body(payload, length);
    const size_t nul = body.find('\0');
    if (nul != std::string::npos)
        body.resize(nul);

    std::set<std::string> heard;
    size_t from = 0;
    while (from <= body.size())
    {
        const size_t at = body.find(':', from);
        const std::string one = body.substr(from, at == std::string::npos ? std::string::npos : at - from);
        if (!one.empty())
            heard.insert(Upper(one));
        if (at == std::string::npos)
            break;
        from = at + 1;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (heard == m_fromBridge)
        return;
    m_fromBridge = heard;
    Bump();
}

//
//  TrackAudio - the WebSocket SDK
//

void RdfClient::WebSocketThread()
{
    while (!m_stop)
    {
        const bool talked = RunWebSocketSession();
        DropTrackAudio();
        if (m_stop)
            break;

        // TrackAudio is often started after EuroScope, so a refused connection is
        // normal rather than an error. Only a session that worked and then broke is
        // worth a log line.
        if (talked)
            Log::Info("rdf", "TrackAudio connection closed - reconnecting");

        for (DWORD waited = 0; waited < kReconnectMs && !m_stop; waited += 100)
            Sleep(100);
    }
}

bool RdfClient::AdoptWebSocketHandle(void* handle)
{
    std::lock_guard<std::mutex> lock(m_wsHandleMutex);
    if (m_stop)
        return false;
    m_wsHandle = handle;
    return true;
}

void* RdfClient::TakeWebSocketHandle()
{
    std::lock_guard<std::mutex> lock(m_wsHandleMutex);
    void* handle = m_wsHandle;
    m_wsHandle = NULL;
    return handle;
}

bool RdfClient::RunWebSocketSession()
{
    bool talked = false;

    HINTERNET session = WinHttpOpen(L"GalaxyATMSystem", WINHTTP_ACCESS_TYPE_NO_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == NULL)
        return false;

    // The receive timeout bounds the handshake. Once the socket is up a timeout only
    // means TrackAudio had nothing to say, so the read loop goes round again.
    WinHttpSetTimeouts(session, 2000, 2000, 2000, 30000);

    HINTERNET connect = WinHttpConnect(session, m_host.c_str(), (INTERNET_PORT)m_port, 0);
    HINTERNET request = connect != NULL
        ? WinHttpOpenRequest(connect, L"GET", L"/ws", NULL, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES, 0)
        : NULL;

    // Hand the request over before the handshake, so a Stop() during a connect that
    // hangs can cancel it rather than wait out the timeout.
    if (request != NULL && !AdoptWebSocketHandle(request))
    {
        WinHttpCloseHandle(request);
        request = NULL;
    }

    HINTERNET socket = NULL;
    if (request != NULL
        && WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0)
        && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, NULL, 0, 0, 0)
        && WinHttpReceiveResponse(request, NULL))
    {
        socket = WinHttpWebSocketCompleteUpgrade(request, NULL);
    }

    // Whoever takes the handle out of the slot closes it, so Stop() and this thread
    // never close the same one.
    if (TakeWebSocketHandle() != NULL)
        WinHttpCloseHandle(request);

    if (socket != NULL && !AdoptWebSocketHandle(socket))
    {
        WinHttpCloseHandle(socket);
        socket = NULL;
    }

    if (socket != NULL)
    {
        m_wsConnected = true;
        talked = true;
        Log::Info("rdf", "TrackAudio connected on " + Narrow(m_host) + ":" + std::to_string(m_port));

        std::string message;
        BYTE buffer[4096];
        while (!m_stop)
        {
            DWORD read = 0;
            WINHTTP_WEB_SOCKET_BUFFER_TYPE type = WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE;
            const DWORD got = WinHttpWebSocketReceive(socket, buffer, sizeof(buffer), &read, &type);
            if (got == ERROR_WINHTTP_TIMEOUT)
                continue;   // an idle frequency, not a dropped connection
            if (got != NO_ERROR || type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE)
                break;

            message.append((const char*)buffer, read);
            if (type == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE
                || type == WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE)
                continue;

            if (message.size() < 512 * 1024)
                OnTrackAudioMessage(message);
            message.clear();
        }

        m_wsConnected = false;
        CloseWebSocket();
    }

    if (connect != NULL)
        WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return talked;
}

void RdfClient::CloseWebSocket()
{
    // Closing the handle is what breaks the blocking receive on the reader thread.
    if (void* handle = TakeWebSocketHandle())
        WinHttpCloseHandle((HINTERNET)handle);
}

void RdfClient::DropTrackAudio()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_fromTrackAudio.empty() && !m_selfTx)
        return;
    m_fromTrackAudio.clear();
    m_selfTx = false;
    Bump();
}

void RdfClient::OnTrackAudioMessage(const std::string& json)
{
    Json::Value root;
    if (!Json::ParseUtf8(json, root))
        return;

    const Json::Value* type = root.Find(L"type");
    if (type == NULL)
        return;

    const std::string event = EventName(type->AsString());
    const Json::Value* value = root.Find(L"value");

    std::lock_guard<std::mutex> lock(m_mutex);
    const std::set<std::string> was = m_fromTrackAudio;
    const bool wasSelf = m_selfTx;

    if (event == "txbegin" || event == "txend")
    {
        m_selfTx = (event == "txbegin");
    }
    else if (event == "rxbegin" && value != NULL)
    {
        const std::string callsign = CallsignOf(*value);
        if (!callsign.empty())
            m_fromTrackAudio.insert(callsign);
    }
    else if (event == "rxend" && value != NULL)
    {
        // kRxEnd carries activeTransmitters when others are still talking; trust it
        // over our own bookkeeping, which can drift if a message was missed.
        const Json::Value* active = value->Find(L"activeTransmitters");
        if (active != NULL && active->kind == Json::Value::Kind::Array)
        {
            m_fromTrackAudio.clear();
            for (const Json::Value& one : active->arr)
            {
                const std::string callsign = one.kind == Json::Value::Kind::Object
                    ? CallsignOf(one)
                    : Upper(Narrow(one.AsString()));
                if (!callsign.empty())
                    m_fromTrackAudio.insert(callsign);
            }
        }
        else
        {
            const std::string callsign = CallsignOf(*value);
            if (!callsign.empty())
                m_fromTrackAudio.erase(callsign);
        }
    }
    else if (event == "voiceconnectedstate" && value != NULL)
    {
        const Json::Value* connected = value->Find(L"connected");
        if (connected != NULL && !connected->AsBool(true))
        {
            m_fromTrackAudio.clear();
            m_selfTx = false;
        }
    }
    else
    {
        return;
    }

    if (m_fromTrackAudio != was || m_selfTx != wasSelf)
        Bump();
}
