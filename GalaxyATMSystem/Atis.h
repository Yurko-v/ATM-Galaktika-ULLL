#pragma once

#include <string>

// -----------------------------------------------------------------------------
// The aerodrome's live ATIS, taken from the network rather than from the config.
//
// EuroScope's plug-in SDK has no ATIS at all - it delivers METARs and nothing
// else - so the broadcast that pilots actually hear is read from the VATSIM
// datafeed, the same way the METAR and the SIGMETs already are.
// -----------------------------------------------------------------------------
struct AtisReport
{
    std::wstring letter;   // "Z" - the index the station is broadcasting
    std::wstring text;     // the report itself, the feed's lines joined by \n

    bool Empty() const { return letter.empty() && text.empty(); }
};

// Fetches the datafeed and picks out the ATIS station for one aerodrome.
// Blocking, with the bounded timeout of Net::HttpGet behind it - call it on a
// worker thread.
//
// icao is the four-letter code ("ULLI"); the station is matched on a callsign
// of "<ICAO>_ATIS", and on "<ICAO>_x_ATIS" (a split arrival/departure ATIS)
// when there is no plain one.
//
// Returns false when the feed could not be read or parsed, or carries no ATIS
// for that aerodrome - in which case 'out' is left untouched.
bool FetchVatsimAtis(const std::string& icao, AtisReport& out);

// The parsing half on its own, so it can be exercised against a saved copy of
// the feed without going near the network.
bool ParseVatsimAtis(const std::string& body, const std::string& icao, AtisReport& out);
