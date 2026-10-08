#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <mutex>
#include <set>
#include <string>
#include <thread>

// Where "who is transmitting right now" comes from.
//
// Two sources run side by side, the same pair the RDF plugin uses:
//
//   * TrackAudio, over its WebSocket SDK (127.0.0.1:49080/ws). It reports both the
//     aircraft heard (kRxBegin/kRxEnd) and our own push-to-talk (kTxBegin/kTxEnd),
//     so the control bearing works without asking the controller for a PTT key.
//   * Audio for VATSIM standalone, over the hidden window the RDF plugin has used
//     since AFV started: class RDFHiddenWindowClass, WM_COPYDATA with dwData 666 and
//     a colon separated list of callsigns, an empty string ending the transmission.
//     AFV cannot tell us about our own transmission.
//
// Whichever client is running fills its half; the view reads the union.
class RdfClient
{
public:
    ~RdfClient();

    // endpoint is "host:port"; empty means the TrackAudio default.
    void Start(const std::string& endpoint);
    void Stop();

    struct State
    {
        std::set<std::string> heard;    // callsigns transmitting on our frequencies
        bool selfTx = false;            // our own push-to-talk is down
    };

    State Snapshot() const;

    // Bumped whenever the state changes, so the screen can ask for a redraw
    // without comparing sets every 40 ms.
    unsigned Generation() const { return m_generation; }

    bool TrackAudioConnected() const { return m_wsConnected; }
    bool BridgeListening() const { return m_window != NULL; }
    bool BridgeTaken() const { return m_bridgeTaken; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void OpenBridge();
    void CloseBridge();
    void OnBridgeMessage(const char* payload, size_t length);

    void WebSocketThread();
    bool RunWebSocketSession();
    void OnTrackAudioMessage(const std::string& json);
    void CloseWebSocket();
    void DropTrackAudio();

    HWND m_window = NULL;
    bool m_classRegistered = false;
    std::atomic<bool> m_bridgeTaken{ false };

    std::thread m_ws;
    std::atomic<bool> m_stop{ false };
    std::atomic<bool> m_wsConnected{ false };
    std::wstring m_host = L"127.0.0.1";
    int m_port = 49080;

    mutable std::mutex m_mutex;
    std::set<std::string> m_fromBridge;
    std::set<std::string> m_fromTrackAudio;
    bool m_selfTx = false;
    std::atomic<unsigned> m_generation{ 0 };

    // The handle the reader thread is blocked on, kept here so Stop() can close it
    // and break the blocking receive. Whoever takes it out of this slot owns closing
    // it, so it is never closed twice. An HINTERNET, held as void* to keep winhttp.h
    // out of every file that includes the plugin.
    bool  AdoptWebSocketHandle(void* handle);
    void* TakeWebSocketHandle();
    void* m_wsHandle = NULL;
    std::mutex m_wsHandleMutex;

    void Bump() { m_generation++; }
};
