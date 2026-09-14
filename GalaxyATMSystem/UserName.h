#pragma once

#include <string>

// -----------------------------------------------------------------------------
// Who is sitting at the scope, for the Пользователь block.
//
// EuroScope's plug-in SDK hands out a controller's callsign and the name typed
// into the connect dialog, but never the CID. The VATSIM datafeed does carry
// it: every controller on the network is listed there with their CID and the
// name the network knows them by, so our own entry is found by callsign.
// -----------------------------------------------------------------------------
struct VatsimIdentity
{
    std::string  callsign;   // the callsign the lookup was made for, as asked
    std::wstring cid;        // "1234567"
    std::wstring name;       // "Yuriy Velbovets" - or the CID again, for a hidden name

    // The name entered for this CID on the squawk server, "Велбовец Юрий
    // Владимирович" - empty when none is. 'registeredAsked' is whether that is
    // an answer: the server has replied, or there is no server to ask.
    std::wstring registeredName;
    bool registeredAsked = false;

    // The server has the name table at all, so a name can be sent to it - a
    // server set up before the table was added answers the question too, but
    // with nowhere to put one.
    bool registeredTable = false;

    bool Empty() const { return cid.empty(); }
};

// Fetches the datafeed and finds the controller logged on as 'callsign'.
// Blocking, with the bounded timeout of Net::HttpGet behind it - call it on a
// worker thread. Returns false when the feed could not be read or parsed, or
// does not list that callsign (yet) - 'out' is left untouched then.
bool FetchVatsimIdentity(const std::string& callsign, VatsimIdentity& out);

// The parsing half on its own, so it can be exercised against a saved copy of
// the feed without going near the network.
bool ParseVatsimIdentity(const std::string& body, const std::string& callsign, VatsimIdentity& out);

// The name the admin has entered for us in the squawk server's user_names
// table (server/public/api/name.php), found by the CID the network lists for
// 'position' - the server only ever tells a controller their own. 'baseUrl' and
// 'apiKey' are the squawk client's. Blocking - a worker thread's job.
//
// True when the server has answered, and 'name' is then what it said - empty
// when no name is entered, or when the server predates the table (404, and
// 'hasTable' false). False when there is no answer to go on yet - no network,
// or the server not seeing us online in the first minute after logging in - so
// it is worth asking again.
bool FetchRegisteredName(const std::string& baseUrl, const std::string& apiKey,
    const std::string& position, std::wstring& name, bool& hasTable);

// Enters our own name in that table, for a controller it has none for - what
// the Регистрация window sends. 'name' as NormalizeEnteredName made it.
// Blocking - a worker thread's job.
//
// True when the table holds a name for us afterwards, and 'stored' is then that
// name: the one sent, or the one that was already there, which the server keeps.
// False otherwise, with the server's reason in 'error' ("not_online",
// "bad_name", "network_stale", ...), "network" when no answer came at all, or
// "http_<status>" when the answer carried no reason.
bool SubmitRegisteredName(const std::string& baseUrl, const std::string& apiKey,
    const std::string& position, const std::wstring& name, std::wstring& stored, std::string& error);

// What was typed into the Регистрация window, put the way it is sent: surname,
// first name and patronymic in full, capitalised - "Велбовец Юрий
// Владимирович" - or shortened to "Велбовец Ю." when there is no patronymic to
// tell the words apart by. "Имя Отчество Фамилия" is turned round, and initials
// typed with or without full stops are accepted. Empty when it cannot be a
// name, with the reason, in Russian, for the window in 'problem'.
std::wstring NormalizeEnteredName(const std::wstring& typed, std::wstring& problem);

// The name the way the Пользователь block prints it, "Фамилия И.О.", in Russian:
// "Yuriy Velbovets" -> "Велбовец Ю.", "Велбовец Юрий Владимирович" ->
// "Велбовец Ю.В.". Two words are taken as first name then surname, the order
// the network lists them in; with three, the patronymic is found by its ending
// and says which of the others is the surname. A word already in Cyrillic is
// kept as it is, a common first name is translated ("Michael" -> "Михаил"),
// anything else transliterated letter by letter. Something already written as
// "Велбовец Ю.В." comes back unchanged, and one written "Велбовец.Ю.В" or
// without the last full stop is put into that form. Empty when there is no name to work
// with - nothing given, or only digits (a CID standing in for a hidden name).
std::wstring RussianShortName(const std::wstring& fullName);
