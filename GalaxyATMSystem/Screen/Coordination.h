#pragma once

#include "Core/Common.h"

namespace Galaxy
{
    inline const ULONGLONG kCoordResultMs = 5000;

    inline bool CoordinationHolds(int state)
    {
        return state == COORDINATION_STATE_ACCEPTED || state == COORDINATION_STATE_MANUAL_ACCEPTED;
    }

    inline std::string DirectPointAhead(CFlightPlan& fp)
    {
        const char* direct = fp.GetControllerAssignedData().GetDirectToPointName();
        if (direct == NULL || *direct == '\0')
            return "";
        CFlightPlanExtractedRoute route = fp.GetExtractedRoute();
        const int index = route.GetPointsAssignedIndex();
        if (index < 0 || index >= route.GetPointsNumber() || route.GetPointDistanceInMinutes(index) < 0)
            return "";
        return direct;
    }

    inline bool RoutePointPassed(CFlightPlan& fp, const char* name)
    {
        CFlightPlanExtractedRoute route = fp.GetExtractedRoute();
        const int calculated = route.GetPointsCalculatedIndex();
        bool passed = false;
        for (int i = 0; i < route.GetPointsNumber(); i++)
        {
            const char* point = route.GetPointName(i);
            if (point == NULL || _stricmp(point, name) != 0)
                continue;
            if (i >= calculated)
                return false;
            passed = true;
        }
        return passed;
    }

    inline const ULONGLONG kPointPassedRecheckMs = 1000;
    inline const size_t kPointPassedMemoLimit = 4096;

    inline bool PointPassed(CFlightPlan& fp, const char* name)
    {
        struct Memo
        {
            bool passed;
            ULONGLONG tick;
        };
        static std::map<std::string, Memo> memo;
        const ULONGLONG now = GetTickCount64();
        const std::string key = std::string(fp.GetCallsign()) + ' ' + name;
        auto known = memo.find(key);
        if (known != memo.end() && now - known->second.tick < kPointPassedRecheckMs)
            return known->second.passed;
        if (memo.size() > kPointPassedMemoLimit)
            memo.clear();
        const bool passed = RoutePointPassed(fp, name);
        memo[key] = { passed, now };
        return passed;
    }

    inline std::string ExitPointFor(CFlightPlan& fp, const std::string& agreed = "", bool firExit = false)
    {
        static std::set<std::string> reported;
        const char* coordinated = fp.GetExitCoordinationPointName();
        const struct { const char* name; const char* source; } candidates[] = {
            { agreed.c_str(), "agreed" },
            { CoordinationHolds(fp.GetExitCoordinationNameState()) ? coordinated : NULL, "exit coordination" },
            { firExit ? fp.GetNextFirCopxPointName() : NULL, "next FIR COPX" },
            { fp.GetNextCopxPointName(), "next COPX" },
            { coordinated, "exit point" },
        };
        for (const auto& c : candidates)
        {
            if (c.name == NULL || *c.name == '\0')
                continue;
            if (!PointPassed(fp, c.name))
                return c.name;
            const std::string key = std::string(fp.GetCallsign()) + " " + c.name;
            if (reported.insert(key).second)
                Log::Info("formular", std::string(fp.GetCallsign()) + ": " + c.source + " " + c.name
                    + " is already behind on the route - skipped");
        }
        return "";
    }

    inline bool TrackedByOther(CFlightPlan& fp)
    {
        const char* tracking = fp.GetTrackingControllerCallsign();
        return !fp.GetTrackingControllerIsMe() && tracking != NULL && *tracking != '\0';
    }

    inline int XflOf(CFlightPlan& fp, int agreedFt = 0)
    {
        if (agreedFt > 0)
            return agreedFt;
        if (TrackedByOther(fp) && CoordinationHolds(fp.GetEntryCoordinationAltitudeState())
            && fp.GetEntryCoordinationAltitude() > 0)
            return fp.GetEntryCoordinationAltitude();
        if (CoordinationHolds(fp.GetExitCoordinationAltitudeState()) && fp.GetExitCoordinationAltitude() > 0)
            return fp.GetExitCoordinationAltitude();
        if (!fp.GetTrackingControllerIsMe() && CoordinationHolds(fp.GetEntryCoordinationAltitudeState())
            && fp.GetEntryCoordinationAltitude() > 0)
            return fp.GetEntryCoordinationAltitude();
        return fp.GetFinalAltitude();
    }

    inline std::string CoordPartner(CPlugIn* plugin, CFlightPlan& fp)
    {
        if (TrackedByOther(fp))
            return fp.GetTrackingControllerCallsign();
        const char* next = fp.GetCoordinatedNextController();
        if (next != NULL && *next != '\0')
            return next;
        const std::string callsign = fp.GetCallsign();
        PartnerGuess& guess = g_partnerGuess[callsign];
        if (Fresh(guess.tick, callsign, kPartnerRecheckMs))
            return guess.callsign;
        guess.callsign.clear();
        const char* myId = plugin->ControllerMyself().GetPositionId();
        CFlightPlanPositionPredictions pred = fp.GetPositionPredictions();
        std::string lastId;
        for (int i = 0; i < pred.GetPointsNumber(); i++)
        {
            const char* id = pred.GetControllerId(i);
            if (id == NULL || *id == '\0' || (myId != NULL && _stricmp(id, myId) == 0) || lastId == id)
                continue;
            lastId = id;
            CController c = plugin->ControllerSelectByPositionId(id);
            if (c.IsValid())
            {
                guess.callsign = c.GetCallsign();
                break;
            }
        }
        return guess.callsign;
    }

    inline std::string PositionIdOf(CPlugIn* plugin, const char* callsign)
    {
        if (callsign == NULL || *callsign == '\0')
            return "";
        CController c = plugin->ControllerMyself();
        if (!c.IsValid() || _stricmp(c.GetCallsign(), callsign) != 0)
            c = plugin->ControllerSelect(callsign);
        if (c.IsValid() && c.GetPositionId() != NULL && *c.GetPositionId() != '\0')
            return c.GetPositionId();
        return callsign;
    }
}
