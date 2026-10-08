#pragma once

#include <windows.h>
#include <map>
#include <string>

namespace Lang
{
    enum class Id { Ru = 0, En = 1 };

    inline Id& State()
    {
        static Id id = Id::Ru;
        return id;
    }

    inline Id  Current()      { return State(); }
    inline void Set(Id id)    { State() = id; }
    inline bool English()     { return State() == Id::En; }

    inline const char* Name(Id id) { return id == Id::En ? "eng" : "rus"; }

    struct Pair { const wchar_t* ru; const wchar_t* en; };

    inline const Pair* Table(size_t& count)
    {
        static const Pair kTable[] =
        {
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
            { L"Введите фамилию",                L"Enter the surname" },
            { L"Введите пароль",                 L"Enter the password" },
            { L"Доступ приостановлен",           L"Access suspended" },
            { L"Уведомление",                    L"Notice" },
            { L"Закрыть",                        L"Close" },
            { L"Свернуть",                       L"Collapse" },
            { L"Развернуть",                     L"Expand" },
            { L"Очистить маршрут",               L"Clear route" },
            { L"Измерить",                       L"Measure" },
            { L"Убрать измерители",              L"Remove rulers" },
            { L"Добавить текст",                 L"Add text" },
            { L"Изменить текст",                 L"Edit text" },
            { L"Удалить текст",                  L"Delete text" },
            { L"Добавить линию",                 L"Add line" },
            { L"Удалить линию",                  L"Delete line" },
            { L"Все линии убрать",               L"Remove all lines" },
            { L"Круг",                           L"Circle" },
            { L"Привязать",                      L"Correlate" },
            { L"Отвязать",                       L"Uncorrelate" },
            { L"Сброс управления",               L"Release" },
            { L"Завершить план",                 L"Close plan" },
            { L"FPL к отметке",                  L"FPL to target" },
            { L"Редактировать FPL",              L"Edit FPL" },
            { L"Изменить код ВРЛ",               L"Change SSR code" },
            { L"Общий маркер",                   L"Shared marker" },
            { L"Мой маркер",                     L"My marker" },
            { L"км",                             L"km" },
            { L"ЛКМ - точка линии, ПКМ - закончить", L"LMB - line point, RMB - finish" },
            { L"Колесо - радиус, ЛКМ/ПКМ - готово",  L"Wheel - radius, LMB/RMB - done" },
            { L"Согласование",                   L"Coordination" },
            { L"Борт никем не взят - согласовывать не с кем",
                                                 L"Nobody tracks the aircraft - there is no one to coordinate with" },
            { L"Борт не войдёт в ваш сектор - EuroScope не даёт согласовать",
                                                 L"The aircraft never enters your sector - EuroScope does not allow the coordination" },
            { L"Не найден следующий сектор для согласования",
                                                 L"No next sector found to coordinate with" },
            { L"EuroScope не отправил согласование",
                                                 L"EuroScope did not send the coordination" },
            { L"Перетащите окно",                L"Drag the window" },

            { L"Неверные фамилия, имя, отчество или пароль",
                                                 L"Wrong surname, name, patronymic or password" },
            { L"Фамилия не та, что при регистрации",
                                                 L"The surname is not the one you registered with" },
            { L"Вы не зарегистрированы в системе КСА",
                                                 L"You are not registered in the KSA system" },
            { L"Слишком много попыток - подождите минуту",
                                                 L"Too many attempts - wait a minute" },
            { L"Сервер не принял запрос - проверьте введённое",
                                                 L"The server rejected the request - check what you entered" },
            { L"Нет связи с сервером",           L"No connection to the server" },
            { L"Сервер пока не видит вас в сети - повторите через минуту",
                                                 L"The server does not see you online yet - try again in a minute" },
            { L"Ждём данных от VATSIM...",       L"Waiting for VATSIM data..." },
            { L"Данные получены, пожалуйста, авторизуйтесь",
                                                 L"Data received, please log in" },
            { L"Ждём, пока сеть VATSIM покажет вашу позицию...",
                                                 L"Waiting for VATSIM to list your position..." },
            { L"Сеть VATSIM так и не показала вашу позицию",
                                                 L"VATSIM never listed your position" },
            { L"Сервер не получает данные VATSIM - повторите позже",
                                                 L"The server is not receiving VATSIM data - try again later" },
            { L"База пользователей недоступна",  L"The user database is unavailable" },
            { L"Сервер ещё не поддерживает вход по паролю",
                                                 L"The server does not support password login yet" },
            { L"Ошибка сервера (",               L"Server error (" },
            { L"Нет подключения к VATSIM",       L"Not connected to VATSIM" },

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

            { L"Настройки",                      L"Settings" },
            { L"Вид",                            L"View" },
            { L"Карта",                          L"Map" },
            { L"Аэродром",                       L"Aerodrome" },
            { L"Списки",                         L"Lists" },
            { L"Метео",                          L"Weather" },
            { L"Почта",                          L"Mail" },
            { L"Загрузка",                       L"Load" },
            { L"Статистика",                     L"Statistics" },
            { L"Архив",                          L"Archive" },
            { L"Справка",                        L"Help" },

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

            { L"Формуляр",                       L"Tag" },
            { L"Тянуть ЛКМ - курс, ПКМ - меню курса TopSky",
                                                 L"Drag LMB - heading, RMB - TopSky heading menu" },
            { L"РДЦ (Контроль)",                 L"ACC (Control)" },
            { L"ДПК/ДПП (Круг/Подход)",          L"APP (Approach)" },
            { L"КДП (Вышка)",                    L"TWR (Tower)" },
            { L" - по позиции",                  L" - by position" },
            { L", подключение: ",                L", connection: " },

            { L"Список РЦ",                      L"Sector list" },
            { L"Шрифт Inter не установлен",      L"The Inter font is not installed" },
            { L"Перетащите список РЦ",           L"Drag the sector list" },
            { L"Сортировать по столбцу",         L"Sort by this column" },
            { L"ЛКМ - выделить формуляр (ПКМ - следующая страница)",
                                                 L"LMB - highlight the label (RMB - next page)" },
            { L"ЛКМ - выделить формуляр",        L"LMB - highlight the label" },
            { L"Код ответчика",                  L"Squawk" },
            { L"Выходной эшелон",                L"Exit level" },
            { L"ЛКМ - текст АТИС, тянуть - переместить, .atis - скрыть",
                                                 L"LMB - ATIS text, drag - move, .atis - hide" },
            { L"Тянуть - переместить, .atis - скрыть",
                                                 L"Drag - move, .atis - hide" },
            { L"ЛКМ - текст АТИС",               L"LMB - ATIS text" },
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

            { L"Запретная зона",                 L"Prohibited area" },
            { L"Опасная зона",                   L"Danger area" },
            { L"Зона ограничения полётов",       L"Restricted area" },

            { L"Сигметы",                        L"SIGMETs" },
            { L"Зоны",                           L"Areas" },
            { L"Конфликты",                      L"Conflicts" },
            { L"м",                              L"m" },
            { L"выключен в конфиге",             L"switched off in the config" },
            { L"Щелчок - больше не предупреждать об этой паре, пока они не разойдутся",
                                                 L"Click - stop alerting for this pair until they separate" },
            { L"%s, целей %d, расчёт %.1f мс; КФ %d, SSA %d, подавлено %d; "
              L"трасса %.1f км, %d ft, прогноз %d с, зон аэродрома %d; склонение сектора %+.1f°",
                                                 L"%s, %d targets, %.1f ms; CNF %d, SSA %d, inhibited %d; "
                                                 L"en-route %.1f km, %d ft, look-ahead %d s, aerodrome areas %d; sector variation %+.1f°" },
            { L"; курс EuroScope отличается от истинного на %+.1f° (по %d бортам)",
                                                 L"; EuroScope track differs from true by %+.1f° (%d aircraft)" },
            { L"Пеленгатор",                     L"Direction finder" },
            { L"АРП ",                           L"DF " },
            { L"включён",                        L"on" },
            { L"выключен",                       L"off" },
            { L", отключён в конфиге",           L", switched off in the config" },
            { L", станции не заданы в конфиге",  L", no stations in the config" },
            { L", у этой позиции нет АРП",       L", this position has no DF station" },
            { L", станция ",                     L", station " },
            { L" (координаты не найдены)",       L" (position not found)" },
            { L", контрольный пеленг ",          L", control bearing " },
            { L", контрольный пеленг не задан",  L", no control bearing set" },
            { L", TrackAudio: ",                 L", TrackAudio: " },
            { L", AFV: ",                        L", AFV: " },
            { L"есть",                           L"connected" },
            { L"нет",                            L"none" },
            { L"окно не открылось",              L"window did not open" },
            { L"занято другим плагином",         L"taken by another plugin" },
            { L"ждёт",                           L"waiting" },
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

            { L"отладка",                        L"debug" },
            { L"сервер не настроен - Squawk.ServerUrl в GalaxyATMSystem.json",
                                                 L"server not set up - Squawk.ServerUrl in GalaxyATMSystem.json" },
            { L"тренажёр: выдача кодов отключена, включите Squawk.AllowSweatbox в GalaxyATMSystem.json",
                                                 L"sweatbox: codes are off, set Squawk.AllowSweatbox in GalaxyATMSystem.json" },
            { L"нет подключения - коды выдаются только в сети",
                                                 L"not connected - codes are only handed out on the network" },
            { L"вы не на диспетчерской позиции - наблюдатели коды не выдают",
                                                 L"not on a controller position - observers do not hand out codes" },
            { L"ответ для ",                     L"answer for " },
            { L": код=",                         L": code=" },
            { L" ошибка=",                       L" error=" },
            { L"свободных кодов не осталось",    L"no free codes left" },
            { L"код уже занят: ",                L"code already held by " },
            { L"сервер не видит ",               L"the server does not see " },
            { L" в сети VATSIM - если вы только что подключились, повторите через минуту",
                                                 L" online on VATSIM - if you have only just logged in, try again in a minute" },
            { L"сервер не может связаться с VATSIM и не знает, кто запрашивает",
                                                 L"the server cannot reach VATSIM, so it cannot tell who is asking" },
            { L"слишком много запросов с этой позиции - подождите минуту",
                                                 L"too many requests from this position - wait a minute" },
            { L"сервер не принял ключ - Squawk.ApiKeyFile",
                                                 L"server refused the key - Squawk.ApiKeyFile" },
            { L"сервер не отвечает",             L"server is not answering" },
            { L"ошибка сервера: ",               L"server error: " },
            { L": получен код ",                 L": got " },
            { L", но плана полёта уже нет",      L" but the flight plan is gone" },
            { L" уже в плане",                   L" already on the plan" },
            { L": EuroScope не дал установить ", L": EuroScope refused to set " },
            { L" - сначала возьмите борт на управление",
                                                 L" - assume the aircraft first" },
            { L": установлен код ",              L": set to " },
            { L"нет своего позывного диспетчера - сначала подключитесь диспетчером",
                                                 L"no controller callsign of your own - log in as a controller first" },
            { L"запрос кода: ",                  L"asking for a code: " },
            { L" от ",                           L" from " },
            { L" (новый)",                       L" (new one)" },
            { L" через ",                        L" via " },
            { L", борт: ",                       L", aircraft: " },
            { L"не выбран",                      L"none selected" },
            { L"борт не выбран - щёлкните по строке борта",
                                                 L"no aircraft selected - click the aircraft's row" },
            { L"меню потеряло борт - откройте его заново",
                                                 L"the menu lost track of the aircraft - open it again" },
            { L"код - это четыре цифры от 0 до 7",
                                                 L"a code is four digits, 0 to 7" },

            { L"Шрифт",                          L"Font" },
            { L"Шрифт Inter не установлен - список РЦ (.rc) рисуется шрифтом Arial."
              L" Установите Inter с https://rsms.me/inter/ и перезапустите EuroScope.",
                                                 L"Font Inter is not installed - the sector list (.rc) falls back to Arial."
                                                 L" Install Inter from https://rsms.me/inter/ and restart EuroScope." },
            { L"Замер",                          L"Timing" },
            { L"замер включён - GalaxyATMSystem.log каждые 10 с",
                                                 L"timing on - GalaxyATMSystem.log every 10 s" },
            { L"замер выключен",                 L"timing off" },
            { L"Диагностика",                    L"Diag" },
            { L"борт не выбран - сначала щёлкните по его позывному",
                                                 L"no selected aircraft - click its callsign first" },
            { L"Символы",                        L"Symbols" },
            { L"файл: ",                         L"file: " },
            { L"символы (* = из файла):",        L"symbols (* = from the file):" },
            { L"последний кадр: целей=%d вне экрана=%d фильтр высоты=%d без символа=%d нарисовано=%d панель видна=%d",
                                                 L"last frame: targets=%d offRadar=%d altFilter=%d noSymbol=%d drawn=%d visible=%d" },

            { L"конфиг не найден: ",             L"config not found: " },
            { L"ошибка JSON в файле ",           L"invalid JSON in " },
            { L"зоны: не открывается ",          L"zones: cannot open " },
            { L"зоны: не читается ",             L"zones: cannot read " },
            { L"коды: не открывается ",          L"squawk: cannot open " },
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

    inline const char* Lookup(const char* ru)
    {
        if (!English() || ru == NULL || *ru == '\0')
            return ru;

        static std::map<std::string, std::string> cache;
        std::map<std::string, std::string>::const_iterator hit = cache.find(ru);
        if (hit != cache.end())
            return hit->second.c_str();

        int wn = MultiByteToWideChar(CP_UTF8, 0, ru, -1, NULL, 0);
        std::wstring wide(wn > 1 ? wn - 1 : 0, L'\0');
        if (wn > 1)
            MultiByteToWideChar(CP_UTF8, 0, ru, -1, &wide[0], wn - 1);

        const wchar_t* en = Lookup(wide.c_str());
        if (en == wide.c_str())
            return ru;

        int an = WideCharToMultiByte(CP_UTF8, 0, en, -1, NULL, 0, NULL, NULL);
        std::string narrow(an > 1 ? an - 1 : 0, '\0');
        if (an > 1)
            WideCharToMultiByte(CP_UTF8, 0, en, -1, &narrow[0], an - 1, NULL, NULL);

        return cache.emplace(ru, narrow).first->second.c_str();
    }
}

inline const wchar_t* Tr(const wchar_t* ru)      { return Lang::Lookup(ru); }
inline const char*    Tr(const char* ru)         { return Lang::Lookup(ru); }
inline std::wstring   Tr(const std::wstring& ru) { return Lang::Lookup(ru.c_str()); }
