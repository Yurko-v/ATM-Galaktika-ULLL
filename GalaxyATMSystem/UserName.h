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
    // Владимирович" - empty when none is.
    std::wstring registeredName;

    // The server has the name table at all - a server set up before the table
    // was added answers the question too, but with nothing to go on.
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

// The name the squawk server's user_names table has for us
// (server/public/api/name.php), found by the CID the network lists for
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

// LOGIN: what the Вход в систему КСА window holds, sent to api/login.php, which
// checks it against the registration of the CID the network lists for
// 'position'. Blocking - a worker thread's job.
//
// True when the server lets us in, and 'name' is then the name it has for us.
// False otherwise, with the server's reason in 'error' ("wrong_credentials",
// "not_registered", "not_online", "network_stale", "rate_limited", ...),
// "network" when no answer came at all, or "http_<status>" when the answer
// carried no reason. The password never goes into the log.
bool SubmitLogin(const std::string& baseUrl, const std::string& apiKey, const std::string& position,
    const std::wstring& surname, const std::wstring& firstName, const std::wstring& patronymic,
    const std::wstring& password, std::wstring& name, std::string& error);

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
