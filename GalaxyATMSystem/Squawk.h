#pragma once

#include <string>
#include <vector>
#include <map>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <condition_variable>

// -----------------------------------------------------------------------------
// The client side of the squawk server (server/ in the repository): the one
// place every position takes its codes from, so an aircraft keeps one code
// across the FIR and beyond it, and no two aircraft are given the same one.
//
// Everything that touches the network happens on a worker thread of its own;
// EuroScope's thread only queues requests and collects the answers. Setting a
// code on a flight plan is left to EuroScope's thread as well (see
// CGalaxyATMSystemPlugin::ApplySquawkAnswers) - the SDK is never called here.
// -----------------------------------------------------------------------------

// Four octal digits.
inline bool IsSquawkCode(const std::string& s)
{
    return s.size() == 4 && s.find_first_not_of("01234567") == std::string::npos;
}

struct SquawkAnswer
{
    enum class Kind { Assign, Report };

    Kind kind = Kind::Assign;
    std::string callsign;
    std::string code;       // the code the aircraft holds now; empty on a failure
    std::string error;      // empty on success, else "pool_empty", "conflict", "unauthorized", "network", ...
    std::string holder;     // on "conflict", the aircraft that holds the code
    bool byUser = false;    // asked for by a click, so a failure is worth a message
};

class SquawkClient
{
public:
    SquawkClient() = default;
    ~SquawkClient();
    SquawkClient(const SquawkClient&) = delete;
    SquawkClient& operator=(const SquawkClient&) = delete;

    // The folder the endpoints are in ("https://squawk.example.ru/api"), the
    // key the server wants, and how often to poll it. An empty URL switches
    // the client off. Called again on ".reload".
    void Configure(const std::string& baseUrl, const std::string& apiKey, int pollSeconds);
    void Stop();
    bool Enabled() const;

    // A code for the aircraft - the one it already holds, unless 'fresh' asks
    // for a different one.
    void Assign(const std::string& callsign, const std::string& position, bool fresh, bool byUser);

    // A code set on the flight plan some other way - typed in by hand, or given
    // by a controller without the plugin - so the server counts it as taken.
    void Report(const std::string& callsign, const std::string& code, const std::string& position, bool byUser);

    // Answers that have come back since the last call.
    std::vector<SquawkAnswer> TakeAnswers();

    // Every code the server has out, callsign -> code, as of the last poll.
    // Never null.
    std::shared_ptr<const std::map<std::string, std::string>> Assignments() const;

    // A request about this aircraft is still on its way.
    bool IsPending(const std::string& callsign) const;

    // What the last request about this aircraft ran into; empty if nothing.
    std::string LastError(const std::string& callsign) const;

private:
    struct Request
    {
        SquawkAnswer::Kind kind = SquawkAnswer::Kind::Assign;
        std::string callsign;
        std::string code;
        std::string position;
        bool fresh = false;
        bool byUser = false;
    };

    void Queue(Request request);
    void Run();
    SquawkAnswer Send(const Request& request, const std::string& url, const std::string& key);
    void Poll(const std::string& url, const std::string& key);

    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    std::thread m_worker;
    bool m_stop = false;
    bool m_pollNow = false;

    std::string m_url;
    std::string m_key;
    int m_pollSeconds = 15;

    std::deque<Request> m_queue;
    std::vector<SquawkAnswer> m_answers;
    std::map<std::string, int> m_pending;          // callsign -> requests still out
    std::map<std::string, std::string> m_errors;   // callsign -> last error
    std::shared_ptr<const std::map<std::string, std::string>> m_assignments =
        std::make_shared<const std::map<std::string, std::string>>();
};
