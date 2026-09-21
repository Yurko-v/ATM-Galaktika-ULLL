#pragma once

#include <string>
#include <vector>
#include <map>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <condition_variable>

inline bool IsSquawkCode(const std::string& s)
{
    return s.size() == 4 && s.find_first_not_of("01234567") == std::string::npos;
}

struct SquawkAnswer
{
    enum class Kind { Assign, Report };

    Kind kind = Kind::Assign;
    std::string callsign;
    std::string code;
    std::string error;
    std::string holder;
    bool byUser = false;
};

class SquawkClient
{
public:
    SquawkClient() = default;
    ~SquawkClient();
    SquawkClient(const SquawkClient&) = delete;
    SquawkClient& operator=(const SquawkClient&) = delete;

    void Configure(const std::string& baseUrl, const std::string& apiKey, int pollSeconds,
        const std::wstring& logPath = std::wstring());

    void SetPosition(const std::string& position);

    void Log(const std::string& line);
    void Stop();
    bool Enabled() const;

    void Assign(const std::string& callsign, const std::string& position, bool fresh, bool byUser);

    void Report(const std::string& callsign, const std::string& code, const std::string& position, bool byUser);

    std::vector<SquawkAnswer> TakeAnswers();

    std::shared_ptr<const std::map<std::string, std::string>> Assignments() const;

    bool IsPending(const std::string& callsign) const;

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
    void Poll(const std::string& url, const std::string& key, const std::string& position);

    mutable std::mutex m_mutex;
    mutable std::mutex m_logMutex;
    std::wstring m_logPath;
    std::condition_variable m_wake;
    std::thread m_worker;
    bool m_stop = false;
    bool m_pollNow = false;

    std::string m_url;
    std::string m_key;
    std::string m_position;
    bool m_saidNoPosition = false;
    int m_pollSeconds = 15;

    std::deque<Request> m_queue;
    std::vector<SquawkAnswer> m_answers;
    std::map<std::string, int> m_pending;
    std::map<std::string, std::string> m_errors;
    std::shared_ptr<const std::map<std::string, std::string>> m_assignments =
        std::make_shared<const std::map<std::string, std::string>>();
};
