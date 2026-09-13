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

// The name the way the Пользователь block prints it, "Фамилия.И.О", in Russian:
// "Yuriy Velbovets" -> "Велбовец.Ю", "Велбовец Юрий Владимирович" ->
// "Велбовец.Ю.В". Two words are taken as first name then surname, the order
// the network lists them in; with three, the patronymic is found by its ending
// and says which of the others is the surname. A word already in Cyrillic is
// kept as it is, a common first name is translated ("Michael" -> "Михаил"),
// anything else transliterated letter by letter. Something already written as
// "Велбовец.Ю.В" comes back unchanged. Empty when there is no name to work
// with - nothing given, or only digits (a CID standing in for a hidden name).
std::wstring RussianShortName(const std::wstring& fullName);
