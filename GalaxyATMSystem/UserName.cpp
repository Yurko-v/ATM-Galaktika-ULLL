#include "pch.h"
#include "UserName.h"
#include "Json.h"
#include "Net.h"

#include <algorithm>
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

    return ParseVatsimIdentity(body, callsign, out);
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
    // Already in the block's own form - "Велбовец.Ю.В" put in the config as it
    // is to be shown.
    size_t start = fullName.find_first_not_of(L" \t");
    if (start == std::wstring::npos)
        return L"";
    size_t end = fullName.find_last_not_of(L" \t");
    std::wstring trimmed = fullName.substr(start, end - start + 1);
    if (trimmed.find(L'.') != std::wstring::npos && trimmed.find_first_of(L" \t_") == std::wstring::npos
        && std::any_of(trimmed.begin(), trimmed.end(), IsCyrillic))
        return trimmed;

    // Words, however they are separated: "Yuriy Velbovets", "Yuriy_Velbovets"
    // and "Yuriy.V" all turn up. Anything with no letters in it is dropped.
    std::vector<std::wstring> words;
    size_t pos = 0;
    while (pos < trimmed.size())
    {
        size_t from = trimmed.find_first_not_of(L" \t_.", pos);
        if (from == std::wstring::npos)
            break;
        size_t to = trimmed.find_first_of(L" \t_.", from);
        std::wstring word = trimmed.substr(from, to == std::wstring::npos ? std::wstring::npos : to - from);
        if (!RussianWord(word).empty())
            words.push_back(word);
        pos = (to == std::wstring::npos) ? trimmed.size() : to;
    }
    if (words.empty())
        return L"";
    if (words.size() == 1)
        return RussianWord(words[0]);

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

    std::wstring out = RussianWord(words[last]);
    std::wstring name = RussianWord(words[first]);
    if (!name.empty())
        out += L"." + name.substr(0, 1);
    if (patronymic != std::wstring::npos)
    {
        std::wstring p = RussianWord(words[patronymic]);
        if (!p.empty())
            out += L"." + p.substr(0, 1);
    }
    return out;
}
