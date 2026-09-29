#include "pch.h"
#include "Core/Common.h"

using namespace Galaxy;

void CGalaxyATMSystemPlugin::OnFunctionCall(int FunctionId, const char* sItemString, POINT Pt, RECT Area)
{
    if (!Unlocked())
        return;
    HandleSquawkFunction(FunctionId, sItemString, Area, "plugin");
}

namespace
{
    const char* const kEnglishMark = "GAL/EN";
}

void CGalaxyATMSystemPlugin::ToggleEnglish(CFlightPlan fp)
{
    if (!fp.IsValid())
        return;
    const std::string callsign = fp.GetCallsign();
    const bool on = !IsEnglish(callsign);
    if (on)
        m_english.insert(callsign);
    else
        m_english.erase(callsign);

    const char* tracking = fp.GetTrackingControllerId();
    if (!fp.GetTrackingControllerIsMe() && tracking != NULL && *tracking != '\0')
    {
        Log::Info("formular", callsign + ": English mark set only locally, tracked by " + tracking);
        return;
    }

    CFlightPlanControllerAssignedData cad = fp.GetControllerAssignedData();
    cad.SetFlightStripAnnotation(kEnglishAnnotation, on ? kEnglishMark : "");

    const std::string scratch = cad.GetScratchPadString();
    const std::string msg = std::string(kEnglishMark) + (on ? "/1" : "/0");
    if (!cad.SetScratchPadString(msg.c_str()))
        Log::Error("formular", callsign + ": English mark broadcast refused by EuroScope");
    cad.SetScratchPadString(scratch.c_str());
}

void CGalaxyATMSystemPlugin::OnFlightPlanFlightStripPushed(CFlightPlan FlightPlan,
    const char* sSenderController, const char* sTargetController)
{
    const std::string me = MyPosition();
    if (!FlightPlan.IsValid() || sTargetController == NULL || me.empty() || me != sTargetController)
        return;
    const char* mark = FlightPlan.GetControllerAssignedData().GetFlightStripAnnotation(kEnglishAnnotation);
    if (mark != NULL && strcmp(mark, kEnglishMark) == 0)
        m_english.insert(FlightPlan.GetCallsign());
    else
        m_english.erase(FlightPlan.GetCallsign());
}

namespace
{
    const double kLandedDistanceNm = 5.0;

    std::string UpperWord(const char* text)
    {
        std::string word;
        for (const char* c = text; c != NULL && *c != '\0' && *c != ' ' && *c != '\t'; c++)
            word += (char)toupper((unsigned char)*c);
        return word;
    }

    std::string StarEntryFix(const std::string& star)
    {
        return star.substr(0, star.find_first_of("0123456789"));
    }

    char RunwaySide(const std::string& runway)
    {
        return runway.empty() || isdigit((unsigned char)runway.back()) ? '\0' : runway.back();
    }

    std::vector<std::string> RouteWords(const std::string& route)
    {
        std::vector<std::string> words;
        std::string word;
        for (char c : route + " ")
        {
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            {
                if (!word.empty())
                    words.push_back(word);
                word.clear();
            }
            else
                word += c;
        }
        return words;
    }
}

void CGalaxyATMSystemPlugin::OnAirportRunwayActivityChanged()
{
    if (!Unlocked())
        return;

    SelectActiveSectorfile();

    std::map<std::string, std::vector<std::string>> arrivalRunways;
    for (CSectorElement rwy = SectorFileElementSelectFirst(SECTOR_ELEMENT_RUNWAY); rwy.IsValid();
         rwy = SectorFileElementSelectNext(rwy, SECTOR_ELEMENT_RUNWAY))
        for (int end = 0; end < 2; end++)
            if (rwy.IsElementActive(false, end))
                arrivalRunways[UpperWord(rwy.GetAirportName())].push_back(UpperWord(rwy.GetRunwayName(end)));

    std::map<std::pair<std::string, std::string>, std::vector<std::string>> stars;
    for (CSectorElement star = SectorFileElementSelectFirst(SECTOR_ELEMENT_STAR); star.IsValid();
         star = SectorFileElementSelectNext(star, SECTOR_ELEMENT_STAR))
        stars[{ UpperWord(star.GetAirportName()), UpperWord(star.GetRunwayName(0)) }]
            .push_back(UpperWord(star.GetName()));

    int moved = 0;
    for (CFlightPlan fp = FlightPlanSelectFirst(); fp.IsValid(); fp = FlightPlanSelectNext(fp))
    {
        CFlightPlanData fpd = fp.GetFlightPlanData();
        const std::string dest = UpperWord(fpd.GetDestination());
        auto active = arrivalRunways.find(dest);
        if (active == arrivalRunways.end())
            continue;

        const std::string oldRunway = UpperWord(fpd.GetArrivalRwy());
        if (oldRunway.empty()
            || std::find(active->second.begin(), active->second.end(), oldRunway) != active->second.end())
            continue;

        const char* tracking = fp.GetTrackingControllerId();
        if (!fp.GetTrackingControllerIsMe() && tracking != NULL && *tracking != '\0')
            continue;
        if (fp.GetDistanceToDestination() < kLandedDistanceNm)
            continue;

        const std::string oldStar = UpperWord(fpd.GetStarName());
        std::vector<std::string> candidates = active->second;
        std::stable_partition(candidates.begin(), candidates.end(),
            [&](const std::string& r) { return RunwaySide(r) == RunwaySide(oldRunway); });

        std::string newRunway, newStar;
        for (const std::string& runway : candidates)
        {
            if (oldStar.empty())
            {
                newRunway = runway;
                break;
            }
            auto procedures = stars.find({ dest, runway });
            if (procedures == stars.end())
                continue;
            for (const std::string& name : procedures->second)
                if (StarEntryFix(name) == StarEntryFix(oldStar))
                {
                    newStar = name;
                    break;
                }
            if (!newStar.empty())
            {
                newRunway = runway;
                break;
            }
        }
        if (newRunway.empty())
            continue;

        const bool roundTrip = UpperWord(fpd.GetOrigin()) == dest;
        std::vector<std::string> words = RouteWords(fpd.GetRoute());
        size_t insertAt = words.size();
        for (size_t i = words.size(); i-- > (roundTrip ? 1u : 0u);)
        {
            const std::string word = UpperWord(words[i].c_str());
            const size_t slash = word.find('/');
            const std::string head = word.substr(0, slash);
            if ((!oldStar.empty() && head == oldStar) || (slash != std::string::npos && head == dest))
            {
                words.erase(words.begin() + i);
                insertAt = i;
            }
        }
        words.insert(words.begin() + min(insertAt, words.size()),
            (newStar.empty() ? dest : newStar) + "/" + newRunway);

        std::string route;
        for (const std::string& word : words)
            route += (route.empty() ? "" : " ") + word;

        if (fpd.SetRoute(route.c_str()) && fpd.AmendFlightPlan())
        {
            moved++;
            Log::Info("runways", std::string(fp.GetCallsign()) + ": " + oldStar + "/" + oldRunway
                + " -> " + newStar + "/" + newRunway);
        }
        else
            Log::Error("runways", std::string(fp.GetCallsign()) + ": EuroScope refused the new STAR");
    }
    Log::Info("runways", "arrival runways changed, STAR reassigned for " + std::to_string(moved));
}

void CGalaxyATMSystemPlugin::OnFlightPlanControllerAssignedDataUpdate(CFlightPlan FlightPlan, int DataType)
{
    if (DataType == CTR_DATA_TYPE_SCRATCH_PAD_STRING && FlightPlan.IsValid())
    {
        const char* scratch = FlightPlan.GetControllerAssignedData().GetScratchPadString();
        const size_t n = strlen(kEnglishMark);
        if (scratch != NULL && strncmp(scratch, kEnglishMark, n) == 0 && scratch[n] == '/')
        {
            if (scratch[n + 1] == '1')
                m_english.insert(FlightPlan.GetCallsign());
            else
                m_english.erase(FlightPlan.GetCallsign());
        }
        return;
    }

    if (DataType != CTR_DATA_TYPE_SQUAWK || !FlightPlan.IsValid())
        return;

    std::string callsign = FlightPlan.GetCallsign();
    std::string code = FlightPlan.GetControllerAssignedData().GetSquawk();

    auto ours = m_squawkSetByUs.find(callsign);
    if (ours != m_squawkSetByUs.end())
    {
        const bool same = (ours->second == code);
        m_squawkSetByUs.erase(ours);
        if (same)
            return;
    }

    if (!IsSquawkCode(code) || !Unlocked() || !SquawkReady(false))
        return;

    auto held = m_squawk.Assignments();
    auto it = held->find(callsign);
    if (it != held->end() && it->second == code)
        return;

    m_squawk.Report(callsign, code, MyPosition(), false);
}
