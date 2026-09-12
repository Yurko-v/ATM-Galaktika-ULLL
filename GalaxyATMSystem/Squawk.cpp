#include "pch.h"
#include "Squawk.h"
#include "Net.h"
#include "Json.h"

#include <cctype>
#include <chrono>
#include <ctime>
#include <fstream>

namespace
{
    // Callsigns, positions, codes and the server's error words are all letters,
    // digits, '_' and '-'. Anything else is dropped rather than escaped, so what
    // goes into a JSON body can never break out of its quotes, and nothing odd
    // the server might send back ever reaches the screen.
    std::string Token(const std::string& s, size_t maxLen, bool upper)
    {
        std::string out;
        for (unsigned char c : s)
        {
            if (out.size() >= maxLen)
                break;
            if (isalnum(c) || c == '_' || c == '-')
                out += upper ? (char)toupper(c) : (char)c;
        }
        return out;
    }

    std::string Token(const std::wstring& s, size_t maxLen, bool upper)
    {
        return Token(Json::WideToUtf8(s), maxLen, upper);
    }

    // Not the plugin's working state, just a bound on what a server that has
    // stopped being read from can pile up.
    const size_t kMaxQueue = 100;
    const size_t kMaxAnswers = 200;
}

SquawkClient::~SquawkClient()
{
    Stop();
}

void SquawkClient::Log(const std::string& line)
{
    std::wstring path;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        path = m_logPath;
    }
    if (path.empty())
        return;

    // The worker thread and EuroScope's own both write here.
    std::lock_guard<std::mutex> lock(m_logMutex);
    std::ofstream file(path, std::ios::app);
    if (!file)
        return;

    time_t now = time(NULL);
    tm utc = {};
    char stamp[32] = "";
    if (gmtime_s(&utc, &now) == 0)
        strftime(stamp, sizeof(stamp), "%H:%M:%S", &utc);
    file << stamp << "  " << line << "\n";
}

void SquawkClient::SetPosition(const std::string& position)
{
    std::string clean = Token(position, 20, true);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (clean == m_position)
            return;
        m_position = clean;
        m_saidNoPosition = false;
        // Logging in - or on to another position - changes what the server
        // will answer, so the list is worth asking for again straight away.
        m_pollNow = true;
    }

    Log("position: " + (clean.empty() ? std::string("(none - not logged in)") : clean));
    m_wake.notify_all();
}

void SquawkClient::Configure(const std::string& baseUrl, const std::string& apiKey, int pollSeconds,
    const std::wstring& logPath)
{
    std::string url = baseUrl;
    while (!url.empty() && (url.back() == '/' || url.back() == ' '))
        url.pop_back();

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_logPath = logPath;
        m_url = url;
        m_key = apiKey;
        m_pollSeconds = max(5, min(300, pollSeconds));
        m_errors.clear();
        m_pollNow = true;   // straight away, with whatever the settings now say
        if (url.empty())
        {
            m_queue.clear();
            m_pending.clear();
            m_assignments = std::make_shared<const std::map<std::string, std::string>>();
        }
    }

    Log("configured: url=" + (url.empty() ? std::string("(none - client off)") : url)
        + " key=" + std::to_string(apiKey.size()) + " chars poll=" + std::to_string(pollSeconds) + "s");

    if (url.empty())
    {
        Stop();
        return;
    }

    if (!m_worker.joinable())
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stop = false;
        }
        m_worker = std::thread([this]() { Run(); });
    }
    m_wake.notify_all();
}

void SquawkClient::Stop()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
    }
    m_wake.notify_all();

    // Joined rather than detached: the DLL can be unloaded straight after, and
    // a request in flight is bounded by the timeouts in Net::HttpRequest.
    if (m_worker.joinable())
        m_worker.join();
}

bool SquawkClient::Enabled() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_url.empty();
}

void SquawkClient::Assign(const std::string& callsign, const std::string& position, bool fresh, bool byUser)
{
    Request r;
    r.kind = SquawkAnswer::Kind::Assign;
    r.callsign = Token(callsign, 12, true);
    r.position = Token(position, 20, true);
    r.fresh = fresh;
    r.byUser = byUser;
    if (!r.callsign.empty() && !r.position.empty())
        Queue(std::move(r));
}

void SquawkClient::Report(const std::string& callsign, const std::string& code, const std::string& position, bool byUser)
{
    Request r;
    r.kind = SquawkAnswer::Kind::Report;
    r.callsign = Token(callsign, 12, true);
    r.code = code;
    r.position = Token(position, 20, true);
    r.byUser = byUser;
    if (!r.callsign.empty() && !r.position.empty() && IsSquawkCode(r.code))
        Queue(std::move(r));
}

void SquawkClient::Queue(Request request)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_url.empty() || m_queue.size() >= kMaxQueue)
            return;

        // A second plain "give it a code" while the first is still out would
        // only ask the server the same question twice.
        if (request.kind == SquawkAnswer::Kind::Assign && !request.fresh
            && m_pending.count(request.callsign) != 0)
            return;

        m_errors.erase(request.callsign);
        m_pending[request.callsign]++;
        m_queue.push_back(std::move(request));
    }
    m_wake.notify_all();
}

std::vector<SquawkAnswer> SquawkClient::TakeAnswers()
{
    std::vector<SquawkAnswer> out;
    std::lock_guard<std::mutex> lock(m_mutex);
    out.swap(m_answers);
    return out;
}

std::shared_ptr<const std::map<std::string, std::string>> SquawkClient::Assignments() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_assignments;
}

bool SquawkClient::IsPending(const std::string& callsign) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_pending.count(callsign) != 0;
}

std::string SquawkClient::LastError(const std::string& callsign) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_errors.find(callsign);
    return it == m_errors.end() ? std::string() : it->second;
}

// Requests go out one at a time, ahead of any poll; the list of codes is
// polled on its own clock, and again straight after every request, since a
// request is exactly what changes it.
void SquawkClient::Run()
{
    using Clock = std::chrono::steady_clock;
    Clock::time_point nextPoll = Clock::now();

    for (;;)
    {
        Request request;
        bool haveRequest = false;
        bool pollDue = false;
        std::string url;
        std::string key;
        std::string position;
        int pollSeconds = 15;

        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_wake.wait_until(lock, nextPoll,
                [this]() { return m_stop || m_pollNow || !m_queue.empty(); });
            if (m_stop)
                return;

            url = m_url;
            key = m_key;
            position = m_position;
            pollSeconds = m_pollSeconds;

            if (!m_queue.empty())
            {
                request = m_queue.front();
                m_queue.pop_front();
                haveRequest = true;
            }
            else if (m_pollNow || Clock::now() >= nextPoll)
            {
                m_pollNow = false;
                pollDue = true;
            }
        }

        if (haveRequest)
        {
            SquawkAnswer answer = Send(request, url, key);

            std::lock_guard<std::mutex> lock(m_mutex);
            auto pending = m_pending.find(request.callsign);
            if (pending != m_pending.end() && --pending->second <= 0)
                m_pending.erase(pending);
            if (answer.error.empty())
                m_errors.erase(answer.callsign);
            else
                m_errors[answer.callsign] = answer.error;
            if (m_answers.size() < kMaxAnswers)
                m_answers.push_back(std::move(answer));
            m_pollNow = true;
            continue;
        }

        if (pollDue)
        {
            Poll(url, key, position);
            nextPoll = Clock::now() + std::chrono::seconds(pollSeconds);
        }
    }
}

SquawkAnswer SquawkClient::Send(const Request& request, const std::string& url, const std::string& key)
{
    SquawkAnswer answer;
    answer.kind = request.kind;
    answer.callsign = request.callsign;
    answer.byUser = request.byUser;

    std::string endpoint;
    std::string body;
    if (request.kind == SquawkAnswer::Kind::Assign)
    {
        endpoint = "/assign.php";
        body = "{\"callsign\":\"" + request.callsign + "\",\"position\":\"" + request.position
            + "\",\"new\":" + (request.fresh ? "true" : "false") + "}";
    }
    else
    {
        endpoint = "/report.php";
        body = "{\"callsign\":\"" + request.callsign + "\",\"code\":\"" + request.code
            + "\",\"position\":\"" + request.position + "\"}";
    }

    std::string headers = "Content-Type: application/json\r\n";
    if (!key.empty())
        headers += "X-Api-Key: " + Token(key, 128, false) + "\r\n";

    Log("POST " + url + endpoint + " " + body);

    Net::HttpResponse response;
    if (!Net::HttpRequest("POST", url + endpoint, headers, body, response))
    {
        Log("  -> no answer at all (network, DNS or timeout)");
        answer.error = "network";
        return answer;
    }

    Log("  -> " + std::to_string(response.status) + " " + response.body.substr(0, 300));

    Json::Value parsed;
    bool isObject = Json::ParseUtf8(response.body, parsed) && parsed.kind == Json::Value::Kind::Object;

    if (response.status == 200 && isObject)
    {
        if (request.kind == SquawkAnswer::Kind::Assign)
        {
            if (const Json::Value* v = parsed.Find(L"code"))
                answer.code = Token(v->AsString(), 4, true);
            if (!IsSquawkCode(answer.code))
            {
                answer.code.clear();
                answer.error = "bad_answer";
            }
        }
        else
        {
            answer.code = request.code;
        }
        return answer;
    }

    answer.error = "http_" + std::to_string(response.status);
    if (isObject)
    {
        if (const Json::Value* v = parsed.Find(L"error"))
        {
            std::string word = Token(v->AsString(), 32, false);
            if (!word.empty())
                answer.error = word;
        }
        if (const Json::Value* v = parsed.Find(L"holder"))
            answer.holder = Token(v->AsString(), 12, true);
    }
    return answer;
}

// A poll that fails leaves the last list standing: the codes it listed are no
// less taken because one answer did not come back.
void SquawkClient::Poll(const std::string& url, const std::string& key, const std::string& position)
{
    // The server has nobody to check without a position, and would turn the
    // poll down every time; before the controller logs in there is nothing to
    // ask about anyway.
    if (position.empty())
    {
        // Said once, not once every poll. Log() takes the lock itself, so it
        // is called after this one has gone.
        bool tell = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            tell = !m_saidNoPosition;
            m_saidNoPosition = true;
        }
        if (tell)
            Log("state: not asking - no controller callsign yet");
        return;
    }

    std::string endpoint = "/state.php?position=" + position;

    Net::HttpResponse response;
    std::string headers;
    if (!key.empty())
        headers = "X-Api-Key: " + Token(key, 128, false) + "\r\n";
    if (!Net::HttpRequest("GET", url + endpoint, headers, std::string(), response, 1024 * 1024))
    {
        Log("GET " + url + endpoint + " -> no answer at all (network, DNS or timeout)");
        return;
    }
    if (response.status != 200)
    {
        Log("GET " + url + endpoint + " -> " + std::to_string(response.status)
            + " " + response.body.substr(0, 200));
        return;
    }

    Json::Value parsed;
    if (!Json::ParseUtf8(response.body, parsed))
        return;
    const Json::Value* list = parsed.Find(L"assignments");
    if (list == nullptr || list->kind != Json::Value::Kind::Object)
        return;

    auto held = std::make_shared<std::map<std::string, std::string>>();
    for (const auto& entry : list->obj)
    {
        std::string callsign = Token(entry.first, 12, true);
        std::string code = Token(entry.second.AsString(), 4, true);
        if (!callsign.empty() && IsSquawkCode(code))
            (*held)[callsign] = code;
    }

    Log("state: " + std::to_string(held->size()) + " codes out");

    std::lock_guard<std::mutex> lock(m_mutex);
    m_assignments = held;
}
