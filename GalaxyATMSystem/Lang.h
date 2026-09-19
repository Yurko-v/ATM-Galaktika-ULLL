#pragma once

#include <windows.h>
#include <map>
#include <string>

// -----------------------------------------------------------------------------
// The plugin's two languages.
//
// Every user-visible string is still written in Russian at the place it is
// drawn - that is the wording the display is specified in - and passed through
// Tr() on the way out. Tr() hands back the English wording while ".eng" is in
// force and the Russian one after ".rus", so nothing but the table below has to
// know that a second language exists.
//
// A string the table does not carry comes back unchanged. That is on purpose:
// callsigns, position names, ATIS text and everything else that arrives from
// the network or the config is not ours to translate.
// -----------------------------------------------------------------------------
namespace Lang
{
    enum class Id { Ru = 0, En = 1 };

    // Process-wide, not per-screen: one command switches every display at once.
    inline Id& State()
    {
        static Id id = Id::Ru;
        return id;
    }

    inline Id  Current()      { return State(); }
    inline void Set(Id id)    { State() = id; }
    inline bool English()     { return State() == Id::En; }

    // ".eng" / ".rus" and the ASR write this spelling.
    inline const char* Name(Id id) { return id == Id::En ? "eng" : "rus"; }

    struct Pair { const wchar_t* ru; const wchar_t* en; };

    // Russian -> English. Only what the plugin itself puts on the screen: panel
    // captions and labels, tooltips, window titles, the sector list's headings
    // and the messages the dot commands print.
    inline const Pair* Table(size_t& count)
    {
        static const Pair kTable[] =
        {
            // ---- Авторизация / вход -----------------------------------------
            { L"Авторизация",                    L"Authorisation" },
            { L"Вход в систему КСА",             L"KSA system login" },
            { L"Введите данные, указанные при регистрации:",
                                                 L"Enter the details you registered with:" },
            { L"Фамилия",                        L"Surname" },
            { L"Имя",                            L"First name" },
            { L"Отчество",                       L"Patronymic" },
            { L"Пароль",                         L"Password" },
            { L"Иванов",                         L"Smith" },
            { L"Иван",                           L"John" },
            { L"если есть",                      L"if any" },
            { L"Нажмите, чтобы ввести пароль",   L"Click to enter the password" },
            { L"Нажмите, чтобы ввести",          L"Click to enter" },
            { L"Enter - следующее поле, Esc - отмена",
                                                 L"Enter - next field, Esc - cancel" },
            { L"Проверка...",                    L"Checking..." },
            { L"Регистрация: ",                  L"Registration: " },
            { L"Открыть страницу регистрации в браузере",
                                                 L"Open the registration page in a browser" },
            { L"Войти",                          L"Log in" },
            { L"Войти в систему",                L"Log in to the system" },
            { L"Войти без проверки в базе",      L"Log in without a database check" },
            { L"Введите фамилию и имя",          L"Enter the surname and first name" },
            { L"Введите пароль",                 L"Enter the password" },
            { L"Подключение к КСА...",           L"Connecting to KSA..." },
            { L"Проверка полномочий...",         L"Checking credentials..." },
            { L"Загрузка профиля...",            L"Loading profile..." },
            { L"Доступ разрешён",                L"Access granted" },
            { L"Доступ приостановлен",           L"Access suspended" },
            { L"Пожалуйста, зарегистрируйтесь в системе в установленном порядке",
                                                 L"Please register in the system in the established manner" },
            { L"Уведомление",                    L"Notice" },
            { L"Закрыть",                        L"Close" },
            { L"Перетащите окно",                L"Drag the window" },

            // Why a LOGIN did not go through.
            { L"Неверные фамилия, имя, отчество или пароль",
                                                 L"Wrong surname, name, patronymic or password" },
            { L"Вы не зарегистрированы в системе КСА",
                                                 L"You are not registered in the KSA system" },
            { L"Слишком много попыток - подождите минуту",
                                                 L"Too many attempts - wait a minute" },
            { L"Сервер не принял запрос - проверьте введённое",
                                                 L"The server rejected the request - check what you entered" },
            { L"Нет связи с сервером",           L"No connection to the server" },
            { L"Сервер пока не видит вас в сети - повторите через минуту",
                                                 L"The server does not see you online yet - try again in a minute" },
            { L"Сервер не получает данные VATSIM - повторите позже",
                                                 L"The server is not receiving VATSIM data - try again later" },
            { L"База пользователей недоступна",  L"The user database is unavailable" },
            { L"Сервер ещё не поддерживает вход по паролю",
                                                 L"The server does not support password login yet" },
            { L"Ошибка сервера (",               L"Server error (" },
            { L"Нет подключения к VATSIM",       L"Not connected to VATSIM" },

            // ---- Панель ------------------------------------------------------
            { L"Развернуть панель",              L"Expand the panel" },
            { L"Свернуть панель",                L"Collapse the panel" },
            { L"Таймер",                         L"Timer" },
            { L"ЛКМ - пуск/стоп таймера, ПКМ - сброс",
                                                 L"LMB - start/stop the timer, RMB - reset" },
            { L"Пользователь",                   L"User" },

            { L"Векторы",                        L"Vectors" },
            { L"Д",                              L"D" },
            { L"Э",                              L"T" },
            { L"Вектор по дальности (км) - вместо вектора по времени",
                                                 L"Distance vector (km) - instead of the time vector" },
            { L"Выбрать длину вектора, км",      L"Choose the vector length, km" },
            { L"Вектор по времени (мин) - вместо вектора по дальности",
                                                 L"Time vector (min) - instead of the distance vector" },
            { L"Выбрать время вектора, мин",     L"Choose the vector time, min" },
            { L"Вектор по плану",                L"Vector along the flight plan" },
            { L"Расчётный эшелон",               L"Predicted level" },

            { L"ФС",                             L"TAG" },
            { L"Р-р шрифта:",                    L"Font size:" },
            { L"Выбрать размер шрифта формуляра",
                                                 L"Choose the tag font size" },
            { L"2 строчный",                     L"2 lines" },
            { L"3 строчный",                     L"3 lines" },
            { L"скорость",                       L"speed" },
            { L"Двухстрочный формуляр",          L"Two-line tag" },
            { L"Трёхстрочный формуляр",          L"Three-line tag" },
            { L"Показывать скорость в формуляре",
                                                 L"Show the speed in the tag" },

            { L"Фильтр высоты",                  L"Altitude filter" },
            { L"Макс:",                          L"Max:" },
            { L"Мин :",                          L"Min :" },
            { L"Верхняя граница фильтра высоты", L"Upper limit of the altitude filter" },
            { L"Нижняя граница фильтра высоты",  L"Lower limit of the altitude filter" },
            { L"Использовать",                   L"Enable" },
            { L"Использовать фильтр высоты",     L"Enable the altitude filter" },

            { L"Масштаб: ширина отображаемой зоны от края до края",
                                                 L"Scale: width of the displayed area, edge to edge" },
            { L"Масштаб радара: вверх - приблизить, вниз - отдалить",
                                                 L"Radar scale: up - zoom in, down - zoom out" },
            { L"ВСЕ",                            L"ALL" },
            { L"БП",                             L"UNC" },
            { L"Пропускать все коды",            L"Let every code through" },
            { L"Без привязки",                   L"Uncorrelated" },
            { L"ВВ1",                            L"VV1" },
            { L"Коды источника ВВ1",             L"VV1 source codes" },
            { L"Источник ВВ1 включён",           L"VV1 source is on" },
            { L"Коды бедствия",                  L"Emergency codes" },
            { L"Двойной код",                    L"Duplicate code" },

            { L"Ед. изм.",                       L"Units" },
            { L"Эшелон",                         L"Flight level" },
            { L"Метры",                          L"Metres" },
            { L"Эшелон и метры",                 L"Flight level and metres" },
            { L"Фут / м",                        L"ft / min" },
            { L"Футы в минуту",                  L"Feet per minute" },
            { L"М / С",                          L"m / s" },
            { L"Метры в секунду",                L"Metres per second" },
            { L"Узлы",                           L"Knots" },
            { L"Км / ч",                         L"km / h" },
            { L"Километры в час",                L"Kilometres per hour" },
            { L"Мили",                           L"NM" },
            { L"Морские мили",                   L"Nautical miles" },
            { L"Км",                             L"km" },
            { L"Километры",                      L"Kilometres" },

            { L"ДАВЛ",                           L"QNH" },
            { L"Э/П",                            L"TL" },
            { L"АТИС",                           L"ATIS" },
            { L"Открыть текст АТИС",             L"Open the ATIS text" },
            { L"Перетащите окно АТИС",           L"Drag the ATIS window" },
            { L"Прокрутить вверх",               L"Scroll up" },
            { L"Прокрутить вниз",                L"Scroll down" },
            { L"Прокрутка текста АТИС",          L"ATIS text scrollbar" },
            { L"ЛКМ - текст АТИС, .atis - скрыть",
                                                 L"LMB - ATIS text, .atis - hide" },

            // ---- Меню --------------------------------------------------------
            { L"Настройки",                      L"Settings" },
            { L"Вид",                            L"View" },
            { L"Сенсоры",                        L"Sensors" },
            { L"Карта",                          L"Map" },
            { L"Аэродром",                       L"Aerodrome" },
            { L"Списки",                         L"Lists" },
            { L"Метео",                          L"Weather" },
            { L"Почта",                          L"Mail" },
            { L"Загрузка",                       L"Load" },
            { L"Статистика",                     L"Statistics" },
            { L"Архив",                          L"Archive" },
            { L"Справка",                        L"Help" },

            // ---- Линейка -----------------------------------------------------
            { L"Линейка",                        L"Ruler" },
            { L"ПКМ/2ЛКМ - удалить линейку",     L"RMB / double LMB - delete the ruler" },
            { L"ЛКМ - конец линейки, ПКМ - отмена",
                                                 L"LMB - end of the ruler, RMB - cancel" },
            { L"ЛКМ - начало линейки, ПКМ - отмена",
                                                 L"LMB - start of the ruler, RMB - cancel" },
            { L"Перетащить информацию линейки",  L"Drag the ruler readout" },
            { L"боковая кнопка отключена",       L"side button off" },
            { L"боковая кнопка 1",               L"side button 1" },
            { L"боковая кнопка 2",               L"side button 2" },

            // ---- Формуляр ----------------------------------------------------
            { L"Формуляр",                       L"Tag" },
            { L"Тянуть ЛКМ - курс, ПКМ - меню курса TopSky",
                                                 L"Drag LMB - heading, RMB - TopSky heading menu" },
            { L"РДЦ (Контроль)",                 L"ACC (Control)" },
            { L"ДПК/ДПП (Круг/Подход)",          L"APP (Approach)" },
            { L"КДП (Вышка)",                    L"TWR (Tower)" },
            { L" - по позиции",                  L" - by position" },
            { L", подключение: ",                L", connection: " },

            // ---- Список РЦ ---------------------------------------------------
            { L"Список РЦ",                      L"Sector list" },
            { L"Шрифт Inter не установлен",      L"The Inter font is not installed" },
            { L"Перетащите список РЦ",           L"Drag the sector list" },
            { L"Сортировать по столбцу",         L"Sort by this column" },
            { L"Выбрать борт (ПКМ - следующая страница)",
                                                 L"Select the aircraft (RMB - next page)" },
            { L"Выбрать борт",                   L"Select the aircraft" },
            { L"Потяните, чтобы изменить размер",
                                                 L"Drag to resize" },
            { L"Рейс:",                          L"Flight:" },
            { L"Фильтр по рейсу",                L"Filter by flight" },
            { L"До (мин)",                       L"Before (min)" },
            { L"После (мин)",                    L"After (min)" },
            { L"За сколько минут до входа в сектор показывать рейс",
                                                 L"How many minutes before it enters the sector a flight is shown" },
            { L"Сколько минут держать рейс после выхода из сектора",
                                                 L"How many minutes a flight is kept after it leaves the sector" },
            // Its column headings, and the two cells that carry a word of their own.
            { L"КФ",                             L"CNF" },
            { L"Рейс",                           L"Flight" },
            { L"ВРЛ",                            L"SSR" },
            { L"Тип",                            L"Type" },
            { L"Точка",                          L"Point" },
            { L"Вход",                           L"Entry" },
            { L"Выход",                          L"Exit" },
            { L"ВыхЭш",                          L"ExitFL" },
            { L"ПВО",                            L"AD" },
            { L"Крд",                            L"Coord" },

            // ---- Зоны и сигметы ----------------------------------------------
            { L"Запретная зона",                 L"Prohibited area" },
            { L"Опасная зона",                   L"Danger area" },
            { L"Зона ограничения полётов",       L"Restricted area" },

            // ---- Что печатают точечные команды --------------------------------
            { L"Сигметы",                        L"SIGMETs" },
            { L"Зоны",                           L"Areas" },
            { L"Конфигурация",                   L"Configuration" },
            { L"Язык",                           L"Language" },
            { L"показаны",                       L"shown" },
            { L"скрыты",                         L"hidden" },
            { L", загружено: ",                  L", loaded: " },
            { L", активно: ",                    L", active: " },
            { L" из ",                           L" of " },
            { L", план: ",                       L", plan: " },
            { L", нотамы: ",                     L", NOTAMs: " },
            { L"источник не задан",              L"source not set" },
            { L"не прочитаны",                   L"not read" },
            { L"зон: ",                          L"areas: " },
            { L", постов: ",                     L", positions: " },
            { L"русский",                        L"Russian" },
            { L"английский",                     L"English" },
        };
        count = sizeof(kTable) / sizeof(kTable[0]);
        return kTable;
    }

    inline const std::map<std::wstring, const wchar_t*>& Index()
    {
        static const std::map<std::wstring, const wchar_t*> index = []
        {
            std::map<std::wstring, const wchar_t*> m;
            size_t n = 0;
            const Pair* table = Table(n);
            for (size_t i = 0; i < n; i++)
                m[table[i].ru] = table[i].en;
            return m;
        }();
        return index;
    }

    inline const wchar_t* Lookup(const wchar_t* ru)
    {
        if (!English() || ru == NULL || *ru == L'\0')
            return ru;
        const std::map<std::wstring, const wchar_t*>& index = Index();
        std::map<std::wstring, const wchar_t*>::const_iterator it = index.find(ru);
        return (it == index.end()) ? ru : it->second;
    }

    // The narrow overload is for the tooltips, which EuroScope takes as C
    // strings. The table is kept in one place, so the text is widened, looked
    // up and narrowed again - once per string, then remembered. std::map keeps
    // its nodes where they are, so the pointer handed out stays good.
    inline const char* Lookup(const char* ru)
    {
        if (!English() || ru == NULL || *ru == '\0')
            return ru;

        static std::map<std::string, std::string> cache;
        std::map<std::string, std::string>::const_iterator hit = cache.find(ru);
        if (hit != cache.end())
            return hit->second.c_str();

        // The source is compiled with /utf-8, so a narrow literal is UTF-8.
        int wn = MultiByteToWideChar(CP_UTF8, 0, ru, -1, NULL, 0);
        std::wstring wide(wn > 1 ? wn - 1 : 0, L'\0');
        if (wn > 1)
            MultiByteToWideChar(CP_UTF8, 0, ru, -1, &wide[0], wn - 1);

        const wchar_t* en = Lookup(wide.c_str());
        if (en == wide.c_str())
            return ru;   // not in the table - leave it exactly as it was written

        int an = WideCharToMultiByte(CP_UTF8, 0, en, -1, NULL, 0, NULL, NULL);
        std::string narrow(an > 1 ? an - 1 : 0, '\0');
        if (an > 1)
            WideCharToMultiByte(CP_UTF8, 0, en, -1, &narrow[0], an - 1, NULL, NULL);

        return cache.emplace(ru, narrow).first->second.c_str();
    }
}

// What every call site uses. Russian in, the current language out.
inline const wchar_t* Tr(const wchar_t* ru)      { return Lang::Lookup(ru); }
inline const char*    Tr(const char* ru)         { return Lang::Lookup(ru); }
inline std::wstring   Tr(const std::wstring& ru) { return Lang::Lookup(ru.c_str()); }
