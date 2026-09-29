#include "pch.h"
#include "Core/Common.h"

using namespace Galaxy;

namespace
{
    bool NotListedYet(const std::string& error)
    {
        return error == "not_online" || error == "network_stale" || error == "network";
    }

    std::wstring LoginMessage(const std::string& error)
    {
        if (error == "wrong_cid")
            return Tr(L"CID не тот, под которым вы в сети VATSIM");
        if (error == "wrong_credentials")
            return Tr(L"Фамилия не та, что при регистрации");
        if (error == "not_registered")
            return Tr(L"Вы не зарегистрированы в системе КСА");
        if (error == "rate_limited")
            return Tr(L"Слишком много попыток - подождите минуту");
        if (error == "bad_request" || error == "bad_json")
            return Tr(L"Сервер не принял запрос - проверьте введённое");

        if (error == "network")
            return Tr(L"Нет связи с сервером");
        if (error == "not_online")
            return Tr(L"Сервер пока не видит вас в сети - повторите через минуту");
        if (error == "network_stale")
            return Tr(L"Сервер не получает данные VATSIM - повторите позже");
        if (error == "no_server")
            return Tr(L"База пользователей недоступна");
        if (error == "http_404" || error == "method_not_allowed")
            return Tr(L"Сервер ещё не поддерживает вход");
        return Tr(L"Ошибка сервера (") + Widen(error.c_str()) + L")";
    }
}

void CGalaxyATMSystemPlugin::StartIdentityFetch(const std::string& callsign)
{
    if (m_identityFetch.Busy())
        return;

    VatsimIdentity known;
    {
        std::lock_guard<std::mutex> lock(m_identityMutex);
        if (_stricmp(m_identity.callsign.c_str(), callsign.c_str()) == 0)
            known = m_identity;
    }
    std::string url = m_config.SquawkServerUrl();
    std::string key = m_config.SquawkApiKey();

    m_identityAskedFor = callsign;
    m_identityFetch.Start([this, callsign, known, url, key]()
        {
            VatsimIdentity found = known;
            if (found.Empty() && !FetchVatsimIdentity(callsign, found))
                return;

            std::wstring name = found.registeredName;
            bool table = found.registeredTable;
            const bool answered = url.empty() || FetchRegisteredName(url, key, callsign, name, table);

            std::lock_guard<std::mutex> lock(m_identityMutex);
            if (answered)
            {
                if (table && name.empty() && !m_identity.registeredName.empty() && m_identity.cid == found.cid)
                {
                    if (!m_accessSuspended)
                        Log::Warn("auth", "user base: the name for CID " + Log::Utf8(found.cid) + " ("
                            + Log::Utf8(m_identity.registeredName) + ") has been removed - access suspended");
                    m_accessSuspended = true;
                }
                else if (!name.empty())
                {
                    m_accessSuspended = false;
                }
                found.registeredName = name;
                found.registeredTable = table;
            }
            else if (_stricmp(m_identity.callsign.c_str(), callsign.c_str()) == 0)
            {
                found.registeredName = m_identity.registeredName;
                found.registeredTable = m_identity.registeredTable;
            }
            m_identity = found;
        });
}

std::wstring CGalaxyATMSystemPlugin::MyUserName() const
{
    VatsimIdentity id;
    {
        std::lock_guard<std::mutex> lock(m_identityMutex);
        id = m_identity;
    }

    if (!id.Empty() && _stricmp(id.callsign.c_str(), MyPosition().c_str()) == 0)
    {
        for (const std::wstring& chosen : { m_config.UserName(id.cid), id.registeredName })
        {
            if (chosen.empty())
                continue;
            std::wstring shortened = RussianShortName(chosen);
            return shortened.empty() ? chosen : shortened;
        }
        std::wstring name = RussianShortName(id.name);
        if (!name.empty())
            return name;
    }
    else
    {
        id = VatsimIdentity();
    }

    CController me = ControllerMyself();
    std::wstring name = me.IsValid() ? RussianShortName(Widen(me.GetFullName())) : L"";
    return !name.empty() ? name : id.cid;
}

bool CGalaxyATMSystemPlugin::LiveConnection() const
{
    const int ct = GetConnectionType();
    return (ct == CONNECTION_TYPE_DIRECT || ct == CONNECTION_TYPE_VIA_PROXY) && !MyPosition().empty();
}

bool CGalaxyATMSystemPlugin::ListedOnNetwork() const
{
    const std::string position = MyPosition();
    std::lock_guard<std::mutex> lock(m_identityMutex);
    return !position.empty() && !m_identity.Empty()
        && _stricmp(m_identity.callsign.c_str(), position.c_str()) == 0;
}

bool CGalaxyATMSystemPlugin::AccessSuspended() const
{
    std::lock_guard<std::mutex> lock(m_identityMutex);
    return m_accessSuspended;
}

bool CGalaxyATMSystemPlugin::TrainingSession() const
{
    const int ct = GetConnectionType();
    return ct == CONNECTION_TYPE_SWEATBOX
        || ct == CONNECTION_TYPE_SIMULATOR_SERVER
        || ct == CONNECTION_TYPE_SIMULATOR_CLIENT
        || ct == CONNECTION_TYPE_PLAYBACK;
}

void CGalaxyATMSystemPlugin::StartLogin(const std::wstring& cid, const std::wstring& surname)
{
    const std::string position = MyPosition();
    {
        std::lock_guard<std::mutex> lock(m_identityMutex);
        const bool waitingForFeed = m_loginState == LoginState::Sending && m_loginRetryTick != 0;
        if ((m_loginState == LoginState::Sending && !waitingForFeed) || m_login.Busy())
            return;
        m_loginState = LoginState::Sending;
        m_loginMessage.clear();
        m_pendingLogin.cid = cid;
        m_pendingLogin.surname = surname;
        m_loginPosition = position;
        m_loginFirstTick = GetTickCount64();
        m_loginRetryTick = 0;
    }
    SendLoginJob();
}

void CGalaxyATMSystemPlugin::SendLoginJob()
{
    SavedLogin login;
    std::string position;
    {
        std::lock_guard<std::mutex> lock(m_identityMutex);
        login = m_pendingLogin;
        position = m_loginPosition;
    }

    std::string url = m_config.SquawkServerUrl();
    std::string key = m_config.SquawkApiKey();
    m_login.Start([this, login, position, url, key]()
        {
            std::wstring name;
            std::string error;
            const bool ok = SubmitLogin(url, key, position, login.cid, login.surname, name, error);

            std::lock_guard<std::mutex> lock(m_identityMutex);
            const ULONGLONG now = GetTickCount64();
            if (ok)
            {
                if (!name.empty() && _stricmp(m_identity.callsign.c_str(), position.c_str()) == 0)
                    m_identity.registeredName = name;
                m_accessSuspended = false;
                m_loginState = LoginState::Done;
                m_loginRetryTick = 0;
                Log::Info("auth", "LOGIN " + position + ": let in by the user base as \"" + Log::Utf8(name)
                    + "\" after " + std::to_string((now - m_loginFirstTick) / 1000) + " s");
            }
            else if (NotListedYet(error) && now - m_loginFirstTick + kLoginRetryMs < kLoginWaitMs)
            {
                m_loginRetryTick = now + kLoginRetryMs;
                m_loginMessage = Tr(L"Ждём, пока сеть VATSIM покажет вашу позицию...");
                Log::Info("auth", "LOGIN " + position + ": " + error + ", asking again in "
                    + std::to_string(kFeedPollSeconds) + " s");
            }
            else
            {
                m_loginState = LoginState::Failed;
                m_loginRetryTick = 0;
                m_loginMessage = NotListedYet(error) ? Tr(L"Сеть VATSIM так и не показала вашу позицию")
                                                     : LoginMessage(error);
                Log::Error("auth", "LOGIN " + position + " refused: " + error);
            }
        });
}

void CGalaxyATMSystemPlugin::RetryLoginIfDue()
{
    const bool live = LiveConnection();
    const std::string position = MyPosition();
    {
        std::lock_guard<std::mutex> lock(m_identityMutex);
        if (m_loginState != LoginState::Sending || m_loginRetryTick == 0 || m_login.Busy()
            || GetTickCount64() < m_loginRetryTick)
            return;
        m_loginRetryTick = 0;
        if (!live || _stricmp(position.c_str(), m_loginPosition.c_str()) != 0)
        {
            m_loginState = LoginState::Failed;
            m_loginMessage = Tr(L"Нет подключения к VATSIM");
            Log::Warn("auth", "LOGIN " + m_loginPosition + ": gave up waiting - the connection is gone or the position changed");
            return;
        }
    }
    SendLoginJob();
}

CGalaxyATMSystemPlugin::LoginState CGalaxyATMSystemPlugin::MyLogin(std::wstring* message) const
{
    std::lock_guard<std::mutex> lock(m_identityMutex);
    if (message != NULL)
        *message = m_loginMessage;
    return m_loginState;
}

namespace
{
    const char* const kLoginSetting = "GalaxyLogin";

    std::string SettingUtf8(const std::wstring& text)
    {
        if (text.empty())
            return std::string();
        const int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(),
            NULL, 0, NULL, NULL);
        std::string out(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), &out[0], n, NULL, NULL);
        return out;
    }

    std::wstring SettingWide(const std::string& utf8)
    {
        if (utf8.empty())
            return std::wstring();
        const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), NULL, 0);
        std::wstring out(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), &out[0], n);
        return out;
    }

    std::string EncodeSetting(const std::wstring& text)
    {
        static const char kHex[] = "0123456789ABCDEF";
        std::string out;
        for (char c : SettingUtf8(text))
        {
            const unsigned char u = (unsigned char)c;
            if (u <= 0x20 || u >= 0x7F || c == '%' || c == '|' || c == ':')
            {
                out += '%';
                out += kHex[u >> 4];
                out += kHex[u & 0xF];
            }
            else
                out += c;
        }
        return out;
    }

    std::wstring DecodeSetting(const std::string& text)
    {
        std::string utf8;
        for (size_t i = 0; i < text.size(); i++)
        {
            if (text[i] == '%' && i + 2 < text.size())
            {
                const std::string hex = text.substr(i + 1, 2);
                utf8 += (char)strtoul(hex.c_str(), NULL, 16);
                i += 2;
            }
            else
                utf8 += text[i];
        }
        return SettingWide(utf8);
    }
}

const CGalaxyATMSystemPlugin::SavedLogin& CGalaxyATMSystemPlugin::SavedIdentity()
{
    if (!m_savedLoginRead)
    {
        m_savedLoginRead = true;
        const char* stored = GetDataFromSettings(kLoginSetting);
        if (stored != NULL && *stored != '\0')
        {
            std::vector<std::string> parts(1);
            for (const char* p = stored; *p != '\0'; p++)
            {
                if (*p == '|')
                    parts.push_back(std::string());
                else
                    parts.back() += *p;
            }
            parts.resize(2);
            m_savedLogin.cid = DecodeSetting(parts[0]);
            m_savedLogin.surname = DecodeSetting(parts[1]);
            Log::Info("auth", "saved login read from the settings: CID " + Log::Utf8(m_savedLogin.cid));
        }
    }
    return m_savedLogin;
}

void CGalaxyATMSystemPlugin::SaveIdentity(const SavedLogin& id)
{
    m_savedLoginRead = true;
    m_savedLogin = id;
    SaveDataToSettings(kLoginSetting, "вход в КСА: CID и фамилия, введённые один раз",
        (EncodeSetting(id.cid) + "|" + EncodeSetting(id.surname)).c_str());
    Log::Info("auth", "login saved to the settings - it will not be asked for again");
}

void CGalaxyATMSystemPlugin::ResetLogin()
{
    std::lock_guard<std::mutex> lock(m_identityMutex);
    if (m_loginState != LoginState::Sending)
    {
        m_loginState = LoginState::Idle;
        m_loginMessage.clear();
    }
}

std::string CGalaxyATMSystemPlugin::RegisterPageUrl() const
{
    std::string url = m_config.SquawkServerUrl();
    while (!url.empty() && (url.back() == '/' || url.back() == ' '))
        url.pop_back();
    if (url.empty())
        return url;

    const size_t api = 4;
    if (url.size() > api && _stricmp(url.c_str() + url.size() - api, "/api") == 0)
        url.resize(url.size() - api);
    return url + "/register/";
}
