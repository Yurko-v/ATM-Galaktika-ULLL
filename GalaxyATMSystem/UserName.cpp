#include "pch.h"
#include "UserName.h"
#include "Json.h"
#include "Net.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <vector>

namespace
{
    // The same feed the ATIS is read from; see Atis.cpp for why the cap sits
    // so far above the feed's real size.
    const char* kFeedUrl = "https://data.vatsim.net/v3/vatsim-data.json";
    const size_t kMaxBytes = 16 * 1024 * 1024;
    const DWORD  kTimeoutMs = 15000;

    std::wstring ToUpper(std::wstring s)
    {
        std::transform(s.begin(), s.end(), s.begin(), ::towupper);
        return s;
    }

    bool IsCyrillic(wchar_t c) { return c >= 0x0400 && c <= 0x04FF; }
    bool IsLatin(wchar_t c)    { return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z'); }

    // The spellings a Russian name turns up in on the network, and the English
    // names that have a Russian form of their own. Keys are lower case.
    const std::map<std::wstring, std::wstring>& KnownNames()
    {
        static const std::map<std::wstring, std::wstring> names = {
            { L"aleksandr", L"Александр" }, { L"alexandr", L"Александр" }, { L"alexander", L"Александр" },
            { L"aleksander", L"Александр" }, { L"alexsandr", L"Александр" }, { L"sasha", L"Саша" },
            { L"aleksey", L"Алексей" }, { L"alexey", L"Алексей" }, { L"alexei", L"Алексей" },
            { L"aleksei", L"Алексей" }, { L"alexej", L"Алексей" }, { L"aleksej", L"Алексей" },
            { L"alex", L"Алекс" },
            { L"anatoly", L"Анатолий" }, { L"anatoliy", L"Анатолий" }, { L"anatolii", L"Анатолий" },
            { L"andrey", L"Андрей" }, { L"andrei", L"Андрей" }, { L"andrej", L"Андрей" },
            { L"andrii", L"Андрей" }, { L"andriy", L"Андрей" }, { L"andrew", L"Андрей" },
            { L"anton", L"Антон" },
            { L"arseny", L"Арсений" }, { L"arseniy", L"Арсений" }, { L"arsenii", L"Арсений" },
            { L"artem", L"Артём" }, { L"artyom", L"Артём" }, { L"artiom", L"Артём" },
            { L"boris", L"Борис" },
            { L"daniil", L"Даниил" }, { L"danil", L"Данил" }, { L"daniel", L"Даниил" },
            { L"denis", L"Денис" },
            { L"dmitry", L"Дмитрий" }, { L"dmitriy", L"Дмитрий" }, { L"dmitrii", L"Дмитрий" },
            { L"dmitri", L"Дмитрий" }, { L"dmitrij", L"Дмитрий" }, { L"dima", L"Дима" },
            { L"egor", L"Егор" }, { L"yegor", L"Егор" },
            { L"evgeny", L"Евгений" }, { L"evgeniy", L"Евгений" }, { L"evgenii", L"Евгений" },
            { L"evgenij", L"Евгений" }, { L"yevgeny", L"Евгений" }, { L"yevgeniy", L"Евгений" },
            { L"eugene", L"Евгений" },
            { L"fedor", L"Фёдор" }, { L"fyodor", L"Фёдор" }, { L"feodor", L"Фёдор" },
            { L"georgy", L"Георгий" }, { L"georgiy", L"Георгий" }, { L"georgii", L"Георгий" },
            { L"george", L"Георгий" },
            { L"gleb", L"Глеб" },
            { L"grigory", L"Григорий" }, { L"grigoriy", L"Григорий" }, { L"grigorii", L"Григорий" },
            { L"igor", L"Игорь" },
            { L"ilya", L"Илья" }, { L"ilia", L"Илья" }, { L"ilja", L"Илья" },
            { L"ivan", L"Иван" },
            { L"kirill", L"Кирилл" }, { L"kiril", L"Кирилл" }, { L"cyril", L"Кирилл" },
            { L"konstantin", L"Константин" },
            { L"leonid", L"Леонид" }, { L"lev", L"Лев" },
            { L"maksim", L"Максим" }, { L"maxim", L"Максим" }, { L"maksym", L"Максим" }, { L"max", L"Макс" },
            { L"mark", L"Марк" },
            { L"matvey", L"Матвей" }, { L"matvei", L"Матвей" },
            { L"mikhail", L"Михаил" }, { L"mihail", L"Михаил" }, { L"michail", L"Михаил" },
            { L"michael", L"Михаил" },
            { L"nikita", L"Никита" },
            { L"nikolay", L"Николай" }, { L"nikolai", L"Николай" }, { L"nicolai", L"Николай" },
            { L"nikolaj", L"Николай" }, { L"nicholas", L"Николай" },
            { L"oleg", L"Олег" },
            { L"pavel", L"Павел" }, { L"paul", L"Павел" },
            { L"petr", L"Пётр" }, { L"pyotr", L"Пётр" }, { L"peter", L"Пётр" },
            { L"roman", L"Роман" }, { L"ruslan", L"Руслан" },
            { L"semyon", L"Семён" }, { L"semen", L"Семён" }, { L"semion", L"Семён" },
            { L"sergey", L"Сергей" }, { L"sergei", L"Сергей" }, { L"sergej", L"Сергей" },
            { L"serguei", L"Сергей" }, { L"sergii", L"Сергей" }, { L"serhiy", L"Сергей" },
            { L"stanislav", L"Станислав" },
            { L"stepan", L"Степан" }, { L"stephan", L"Степан" },
            { L"timofey", L"Тимофей" }, { L"timofei", L"Тимофей" },
            { L"timur", L"Тимур" }, { L"vadim", L"Вадим" }, { L"valentin", L"Валентин" },
            { L"valery", L"Валерий" }, { L"valeriy", L"Валерий" }, { L"valerii", L"Валерий" },
            { L"vasily", L"Василий" }, { L"vasiliy", L"Василий" }, { L"vasilii", L"Василий" },
            { L"viktor", L"Виктор" }, { L"victor", L"Виктор" },
            { L"vitaly", L"Виталий" }, { L"vitaliy", L"Виталий" }, { L"vitalii", L"Виталий" },
            { L"vladimir", L"Владимир" },
            { L"vladislav", L"Владислав" }, { L"vlad", L"Влад" },
            { L"vyacheslav", L"Вячеслав" }, { L"viacheslav", L"Вячеслав" },
            { L"yaroslav", L"Ярослав" }, { L"iaroslav", L"Ярослав" },
            { L"yury", L"Юрий" }, { L"yuriy", L"Юрий" }, { L"yuri", L"Юрий" }, { L"yurii", L"Юрий" },
            { L"iurii", L"Юрий" }, { L"jurij", L"Юрий" }, { L"yura", L"Юра" },
            { L"zakhar", L"Захар" },

            { L"alena", L"Алёна" }, { L"alyona", L"Алёна" }, { L"alina", L"Алина" },
            { L"anastasia", L"Анастасия" }, { L"anastasiya", L"Анастасия" },
            { L"anna", L"Анна" },
            { L"daria", L"Дарья" }, { L"darya", L"Дарья" },
            { L"ekaterina", L"Екатерина" }, { L"yekaterina", L"Екатерина" }, { L"katerina", L"Катерина" },
            { L"elena", L"Елена" }, { L"yelena", L"Елена" },
            { L"irina", L"Ирина" },
            { L"julia", L"Юлия" }, { L"yulia", L"Юлия" }, { L"yuliya", L"Юлия" }, { L"iuliia", L"Юлия" },
            { L"kristina", L"Кристина" }, { L"christina", L"Кристина" },
            { L"ksenia", L"Ксения" }, { L"kseniya", L"Ксения" },
            { L"maria", L"Мария" }, { L"mariya", L"Мария" },
            { L"natalia", L"Наталья" }, { L"natalya", L"Наталья" },
            { L"olga", L"Ольга" }, { L"polina", L"Полина" }, { L"svetlana", L"Светлана" },
            { L"tatiana", L"Татьяна" }, { L"tatyana", L"Татьяна" },
            { L"victoria", L"Виктория" }, { L"viktoriya", L"Виктория" },
            { L"sofia", L"София" }, { L"sofiya", L"София" }, { L"sophia", L"София" },
        };
        return names;
    }

    bool IsVowel(wchar_t c) { return wcschr(L"aeiouy", c) != NULL; }

    // Letter-by-letter, for a name the table does not know. Longest spelling
    // first, so "shch" is one letter and not "ш" + "ч".
    std::wstring Transliterate(const std::wstring& lower)
    {
        static const std::pair<const wchar_t*, const wchar_t*> kGroups[] = {
            { L"shch", L"щ" }, { L"sch", L"ш" }, { L"zh", L"ж" }, { L"kh", L"х" },
            { L"ts", L"ц" }, { L"ch", L"ч" }, { L"sh", L"ш" }, { L"ph", L"ф" },
            { L"th", L"т" }, { L"ck", L"к" }, { L"yu", L"ю" }, { L"ya", L"я" },
            { L"yo", L"ё" }, { L"ye", L"е" }, { L"ee", L"и" }, { L"oo", L"у" },
        };
        static const wchar_t* kLetters[26] = {
            L"а", L"б", L"к", L"д", L"е", L"ф", L"г", L"х", L"и", L"й", L"к", L"л", L"м",
            L"н", L"о", L"п", L"к", L"р", L"с", L"т", L"у", L"в", L"в", L"кс", L"и", L"з",
        };

        std::wstring out;
        size_t i = 0;
        while (i < lower.size())
        {
            bool matched = false;
            for (const auto& g : kGroups)
            {
                size_t n = wcslen(g.first);
                if (lower.compare(i, n, g.first) == 0)
                {
                    out += g.second;
                    i += n;
                    matched = true;
                    break;
                }
            }
            if (matched)
                continue;

            wchar_t c = lower[i];
            bool first = (i == 0);
            bool afterVowel = !first && IsVowel(lower[i - 1]);
            if (c == L'e' && first)
                out += L"э";                       // Eduard, Edward
            else if (c == L'j' && first)
                out += L"дж";                      // John, James
            else if (c == L'y' && (first || afterVowel))
                out += L"й";                       // Sergey, Nikolay
            else if (c == L'i' && !first && wcschr(L"aeo", lower[i - 1]) != NULL
                     && (i + 1 == lower.size() || !IsVowel(lower[i + 1])))
                out += L"й";                       // Aidan, Eilon
            else if (c == L'h' && afterVowel && (i + 1 == lower.size() || !IsVowel(lower[i + 1])))
                ;                                  // silent: John, Sarah
            else if (c >= L'a' && c <= L'z')
                out += kLetters[c - L'a'];
            i++;
        }
        return out;
    }
}

bool ParseVatsimIdentity(const std::string& body, const std::string& callsign, VatsimIdentity& out)
{
    Json::Value root;
    if (!Json::ParseUtf8(body, root) || root.kind != Json::Value::Kind::Object)
        return false;

    const Json::Value* list = root.Find(L"controllers");
    if (list == NULL || list->kind != Json::Value::Kind::Array)
        return false;

    const std::wstring wanted = ToUpper(Json::Utf8ToWide(callsign));
    for (const Json::Value& entry : list->arr)
    {
        if (entry.kind != Json::Value::Kind::Object)
            continue;
        const Json::Value* cs = entry.Find(L"callsign");
        if (cs == NULL || ToUpper(cs->AsString()) != wanted)
            continue;

        const Json::Value* cid = entry.Find(L"cid");
        if (cid == NULL || cid->AsText().empty())
            continue;

        out.callsign = callsign;
        out.cid = cid->AsText();
        const Json::Value* name = entry.Find(L"name");
        out.name = name != NULL ? name->AsString() : L"";
        return true;
    }
    return false;
}

bool FetchVatsimIdentity(const std::string& callsign, VatsimIdentity& out)
{
    if (callsign.empty())
        return false;

    std::string body;
    if (!Net::HttpGet(kFeedUrl, body, kMaxBytes, kTimeoutMs))
        return false;

    if (ParseVatsimIdentity(body, callsign, out))
        return true;

    Json::Value root;
    if (!Json::ParseUtf8(body, root) || root.kind != Json::Value::Kind::Object || root.Find(L"controllers") == NULL)
        Log::Error("identity", std::string("feed ") + kFeedUrl + " has no \"controllers\" list ("
            + std::to_string(body.size()) + " bytes): " + Log::Snippet(body, 120));
    else
        Log::Warn("identity", "the VATSIM feed does not list " + callsign
            + " yet - no CID to look the name up by (a new logon takes a minute or two to appear)");
    return false;
}

namespace
{
    // The endpoint folder without its trailing slash, and the position and the
    // key cut down to the characters they are made of - the position goes into
    // a query string or a JSON body, the key into a header. False when there is
    // no server or no position to ask about.
    bool ServerRequestParts(const std::string& baseUrl, const std::string& apiKey,
        const std::string& position, std::string& url, std::string& pos, std::string& headers)
    {
        url = baseUrl;
        while (!url.empty() && (url.back() == '/' || url.back() == ' '))
            url.pop_back();

        pos.clear();
        std::string key;
        for (unsigned char c : position)
            if (isalnum(c) || c == '_' || c == '-')
                pos += (char)toupper(c);
        for (unsigned char c : apiKey)
            if (isalnum(c) || c == '_' || c == '-')
                key += (char)c;

        headers.clear();
        if (!key.empty())
            headers = "X-Api-Key: " + key + "\r\n";
        return !url.empty() && !pos.empty();
    }
}

bool FetchRegisteredName(const std::string& baseUrl, const std::string& apiKey,
    const std::string& position, std::wstring& name, bool& hasTable)
{
    std::string url, pos, headers;
    if (!ServerRequestParts(baseUrl, apiKey, position, url, pos, headers))
        return false;

    const std::string endpoint = url + "/name.php?position=" + pos;
    Net::HttpResponse response;
    if (!Net::HttpRequest("GET", endpoint, headers, std::string(), response))
    {
        Log::Error("auth", "user base: no answer from " + endpoint + " - see the [net] line before this");
        return false;
    }

    // A server set up before the table existed: there is no name to be had
    // from it, and asking again every minute would not change that.
    if (response.status == 404)
    {
        Log::Error("auth", "user base: " + endpoint + " answered 404 - the server has no name table (name.php)");
        name.clear();
        hasTable = false;
        return true;
    }
    if (response.status != 200)
    {
        Log::Error("auth", "user base: " + endpoint + " answered HTTP " + std::to_string(response.status)
            + " - " + Log::Snippet(response.body));
        return false;
    }

    Json::Value root;
    if (!Json::ParseUtf8(response.body, root) || root.kind != Json::Value::Kind::Object)
    {
        Log::Error("auth", "user base: " + endpoint + " answered something that is not a JSON object - "
            + Log::Snippet(response.body));
        return false;
    }

    const Json::Value* v = root.Find(L"name");
    name = (v != NULL) ? v->AsString() : L"";
    if (name.size() > 100)
        name.resize(100);
    hasTable = true;
    return true;
}

bool SubmitRegisteredName(const std::string& baseUrl, const std::string& apiKey,
    const std::string& position, const std::wstring& name, std::wstring& stored, std::string& error)
{
    std::string url, pos, headers;
    if (!ServerRequestParts(baseUrl, apiKey, position, url, pos, headers))
    {
        error = "no_server";
        return false;
    }

    // Letters, spaces, hyphens and full stops are all NormalizeEnteredName lets
    // through, but the value is escaped all the same.
    std::string body = "{\"position\":\"" + pos + "\",\"name\":\"";
    for (char c : Json::WideToUtf8(name))
    {
        if ((unsigned char)c < 0x20)
            continue;
        if (c == '"' || c == '\\')
            body += '\\';
        body += c;
    }
    body += "\"}";
    headers += "Content-Type: application/json\r\n";

    Net::HttpResponse response;
    if (!Net::HttpRequest("POST", url + "/name.php", headers, body, response))
    {
        Log::Error("auth", "registration: no answer from " + url + "/name.php - see the [net] line before this");
        error = "network";
        return false;
    }

    Json::Value root;
    const bool isObject = Json::ParseUtf8(response.body, root) && root.kind == Json::Value::Kind::Object;
    const Json::Value* v = isObject ? root.Find(L"name") : NULL;
    stored = (v != NULL) ? v->AsString() : L"";
    if (stored.size() > 100)
        stored.resize(100);

    // 409 is a name that was already there - the admin's, or one sent from
    // another machine. The server keeps it, so it is the one we have now.
    if ((response.status == 200 || response.status == 409) && !stored.empty())
        return true;

    const Json::Value* e = isObject ? root.Find(L"error") : NULL;
    error = (e != NULL) ? Json::WideToUtf8(e->AsString()) : std::string();
    if (error.empty())
        error = "http_" + std::to_string(response.status);
    Log::Error("auth", "registration: " + url + "/name.php refused the name for " + pos + " - HTTP "
        + std::to_string(response.status) + ", " + error + ", body: " + Log::Snippet(response.body));
    return false;
}

namespace
{
    // One word of a name, in Russian and capitalised. Empty for a word with no
    // letters in it - digits, the network showing a CID for a hidden name.
    std::wstring RussianWord(std::wstring word)
    {
        bool cyrillic = false, latin = false;
        for (wchar_t c : word)
        {
            if (IsCyrillic(c)) cyrillic = true;
            else if (IsLatin(c)) latin = true;
        }

        // CharLower/CharUpper rather than towlower/towupper: those go by the C
        // locale, which leaves Cyrillic exactly as it was.
        if (cyrillic)
        {
            CharLowerBuffW(&word[0], (DWORD)word.size());
            CharUpperBuffW(&word[0], 1);
            return word;
        }
        if (!latin)
            return L"";

        std::wstring lower;
        for (wchar_t c : word)
            if (IsLatin(c))
                lower += (wchar_t)towlower(c);

        auto it = KnownNames().find(lower);
        std::wstring ru = (it != KnownNames().end()) ? it->second : Transliterate(lower);
        if (ru.empty())
            return L"";
        CharUpperBuffW(&ru[0], 1);
        return ru;
    }

    // "Владимирович", "Сергеевна", "Ильич", "Кузьминична" - or one written in
    // Latin letters.
    bool IsPatronymic(std::wstring word)
    {
        if (word.empty())
            return false;
        CharLowerBuffW(&word[0], (DWORD)word.size());
        for (const wchar_t* ending : { L"вич", L"вна", L"чна", L"ич",
                                       L"vich", L"vna", L"chna", L"ich" })
        {
            size_t n = wcslen(ending);
            if (word.size() > n + 1 && word.compare(word.size() - n, n, ending) == 0)
                return true;
        }
        return false;
    }
}

std::wstring RussianShortName(const std::wstring& fullName)
{
    // Words, however they are separated: "Yuriy Velbovets", "Yuriy_Velbovets"
    // and "Yuriy.V" all turn up. Anything with no letters in it is dropped.
    std::vector<std::wstring> words;
    size_t pos = 0;
    while (pos < fullName.size())
    {
        size_t from = fullName.find_first_not_of(L" \t_.", pos);
        if (from == std::wstring::npos)
            break;
        size_t to = fullName.find_first_of(L" \t_.", from);
        std::wstring word = fullName.substr(from, to == std::wstring::npos ? std::wstring::npos : to - from);
        if (!RussianWord(word).empty())
            words.push_back(word);
        pos = (to == std::wstring::npos) ? fullName.size() : to;
    }
    if (words.empty())
        return L"";
    if (words.size() == 1)
        return RussianWord(words[0]);

    // Already shortened, the way it is put in the config or the server's
    // table: a surname and its initials - "Велбовец Ю.В.", "Велбовец.Ю.В",
    // "Ю.В. Велбовец". Told from a full name by every other word being a
    // single letter, and only in Cyrillic, where such a word is nothing but an
    // initial. The surname is kept as it was written - "Римская-Корсакова".
    if (words.size() <= 3 && std::any_of(fullName.begin(), fullName.end(), IsCyrillic))
    {
        size_t longWords = 0, surname = 0;
        for (size_t i = 0; i < words.size(); i++)
        {
            if (words[i].size() > 1)
            {
                longWords++;
                surname = i;
            }
        }
        if (longWords == 1)
        {
            std::wstring out = std::any_of(words[surname].begin(), words[surname].end(), IsCyrillic)
                ? words[surname] : RussianWord(words[surname]);
            out += L" ";
            for (size_t i = 0; i < words.size(); i++)
                if (i != surname)
                    out += RussianWord(words[i]).substr(0, 1) + L".";
            return out;
        }
    }

    // First name then surname, as the network has it - unless a patronymic
    // says otherwise: "Фамилия Имя Отчество" or "Имя Отчество Фамилия".
    size_t first = 0, last = words.size() - 1, patronymic = std::wstring::npos;
    if (words.size() >= 3)
    {
        if (IsPatronymic(words[2]))
        {
            last = 0;
            first = 1;
            patronymic = 2;
        }
        else if (IsPatronymic(words[1]))
        {
            patronymic = 1;
        }
    }

    // The surname, a space, and each initial with its own full stop:
    // "Велбовец Ю.В.".
    std::wstring out = RussianWord(words[last]) + L" ";
    std::wstring name = RussianWord(words[first]);
    if (!name.empty())
        out += name.substr(0, 1) + L".";
    if (patronymic != std::wstring::npos)
    {
        std::wstring p = RussianWord(words[patronymic]);
        if (!p.empty())
            out += p.substr(0, 1) + L".";
    }
    return out;
}

namespace
{
    // "римская-КОРСАКОВА" -> "Римская-Корсакова".
    std::wstring CapitalizeName(std::wstring word)
    {
        if (word.empty())
            return word;
        CharLowerBuffW(&word[0], (DWORD)word.size());
        for (size_t i = 0; i < word.size(); i++)
            if (i == 0 || word[i - 1] == L'-')
                CharUpperBuffW(&word[i], 1);
        return word;
    }
}

std::wstring NormalizeEnteredName(const std::wstring& typed, std::wstring& problem)
{
    problem.clear();

    // Words, split on spaces and on the full stops of initials.
    std::vector<std::wstring> words;
    size_t pos = 0;
    while (pos < typed.size())
    {
        size_t from = typed.find_first_not_of(L" \t.", pos);
        if (from == std::wstring::npos)
            break;
        size_t to = typed.find_first_of(L" \t.", from);
        words.push_back(typed.substr(from, to == std::wstring::npos ? std::wstring::npos : to - from));
        pos = (to == std::wstring::npos) ? typed.size() : to;
    }
    if (words.empty())
    {
        problem = L"Введите фамилию, имя и отчество";
        return L"";
    }

    for (const std::wstring& w : words)
    {
        for (wchar_t c : w)
        {
            // A "?" is a Cyrillic letter EuroScope's edit box could not pass
            // through the Windows code page.
            if (IsLatin(c) || c == L'?')
            {
                problem = L"Пишите русскими буквами";
                return L"";
            }
            if (!IsCyrillic(c) && c != L'-')
            {
                problem = L"Только буквы, дефис и точки";
                return L"";
            }
        }
        if (w.front() == L'-' || w.back() == L'-')
        {
            problem = L"Дефис - только внутри двойной фамилии";
            return L"";
        }
    }
    if (words.size() < 2)
    {
        problem = L"Нужны фамилия и имя, и отчество, если есть";
        return L"";
    }
    if (words.size() > 3)
    {
        problem = L"Не больше трёх слов: фамилия, имя, отчество";
        return L"";
    }

    for (std::wstring& w : words)
        w = CapitalizeName(w);

    // "Имя Отчество Фамилия", told by where the patronymic is.
    if (words.size() == 3 && words[1].size() > 1 && IsPatronymic(words[1]) && !IsPatronymic(words[2]))
        std::rotate(words.begin(), words.begin() + 2, words.end());

    if (words[0].size() < 2)
    {
        problem = L"Сначала фамилия, полностью";
        return L"";
    }

    // In full only when RussianShortName can read it back the same way: a
    // patronymic in last place says which word is the surname, and a plain
    // surname is not lowered to "Римская-корсакова" on the way. Otherwise it is
    // shortened here, while the order is still known.
    bool full = words.size() == 3 && IsPatronymic(words[2])
        && words[0].find(L'-') == std::wstring::npos;
    for (size_t i = 1; i < words.size(); i++)
        if (words[i].size() < 2)
            full = false;
    if (full)
        return words[0] + L" " + words[1] + L" " + words[2];

    std::wstring out = words[0] + L" ";
    for (size_t i = 1; i < words.size(); i++)
        out += words[i].substr(0, 1) + L".";
    return out;
}
