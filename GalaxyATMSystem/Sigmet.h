#pragma once

// EuroScopePlugIn.h expects the Windows types to be there already, and this
// header is included from more than one place - so it brings its own.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>
#include <vector>
#include "EuroScopePlugIn.h"

// -----------------------------------------------------------------------------
// One SIGMET area, as published by the international SIGMET feed.
//
// EuroScope's plug-in SDK knows nothing about SIGMETs - it delivers METARs and
// nothing else in the way of weather - so they are fetched directly, the same
// way the airport's METAR already is, and drawn by the plug-in itself.
// -----------------------------------------------------------------------------
struct Sigmet
{
    std::wstring firId;       // "ULLL"
    std::wstring firName;     // "ULLL SANKT-PETERBURG"
    std::wstring seriesId;    // "3"
    std::wstring hazard;      // "TS" / "TURB" / "ICE" / "VA" / "MTW" / "DS" ...
    std::wstring qualifier;   // "OBSC", "EMBD", a volcano's name for VA, ...
    std::wstring dir, spd;    // movement, as published ("NE", "15")
    std::wstring chng;        // "NC" / "INTSF" / "WKN"
    std::wstring raw;         // the report itself, for the info window
    int  baseFt = -1;         // 0 is the surface; -1 is "the report gave none"
    int  topFt = -1;
    long long validFrom = 0;  // unix seconds
    long long validTo = 0;
    bool closed = true;       // an area is a closed ring; a line of weather is not

    // Usually one ring, but the feed also publishes a single report covering
    // several separate areas ("AREAS"), and each of those is a ring of its own.
    std::vector<std::vector<EuroScopePlugIn::CPosition>> rings;

    // "SIGMET 3 TS - ULLL SANKT-PETERBURG" - the one-line identity, used as the
    // hover tooltip. The info window shows the raw report and nothing else, so
    // the decoded fields above it are here as the parsed record rather than to
    // be displayed.
    std::wstring Title() const;

    // Feed order is not stable between fetches, so this is what tells one
    // report from another when the list is replaced.
    std::wstring Key() const;
};

// Fetches the international SIGMET feed and parses it. Blocking, with the
// bounded timeouts of Net::HttpGet behind it - call it on a worker thread.
//
// firFilter is matched against firId (upper case); an empty filter keeps
// everything. Rings too small to be a shape are dropped, and with them any
// report left without a ring at all - the feed carries plenty whose geometry
// is a single point or nothing, and there would be nothing to draw or click.
//
// Returns false only when the feed could not be read or parsed at all; an
// empty result with no error is the normal "no active SIGMETs" answer.
bool FetchSigmets(const std::vector<std::wstring>& firFilter, std::vector<Sigmet>& out);

// The parsing half on its own, so it can be exercised against a saved copy of
// the feed without going near the network.
bool ParseSigmets(const std::string& body,
    const std::vector<std::wstring>& firFilter, std::vector<Sigmet>& out);
