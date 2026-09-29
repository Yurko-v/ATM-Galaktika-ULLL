#include "pch.h"
#include "Core/Common.h"

using namespace Galaxy;

bool CGalaxyATMSystemPlugin::AltFilterPasses(int altFt) const
{
    if (!m_altFilterEnabled)
        return true;

    int fl = altFt / 100;
    int lo = min(m_altFilterFromFL, m_altFilterToFL);
    int hi = max(m_altFilterFromFL, m_altFilterToFL);
    return fl >= lo && fl <= hi;
}

std::wstring CGalaxyATMSystemPlugin::TransitionLevel() const
{
    wchar_t buf[8];
    swprintf_s(buf, L"F%03d", TransitionLevelFL());
    return buf;
}

int CGalaxyATMSystemPlugin::TransitionLevelFL() const
{
    int hpa = _wtoi(m_qnhHpa.c_str());
    return (hpa < 960) ? 70 : (hpa < 996) ? 60 : 50;
}

void CGalaxyATMSystemPlugin::RefreshApwZones()
{
    const ULONGLONG now = GetTickCount64();
    if (m_apwZonesTick != 0 && now - m_apwZonesTick < 2000)
        return;
    m_apwZonesTick = now;

    const std::vector<Zone>& zones = m_config.Zones();
    if (zones.empty() || !m_config.Apw().enabled)
    {
        m_apwZones.clear();
        return;
    }

    static const std::vector<ZoneBooking> kNoBookings;

    std::shared_ptr<const std::vector<ZoneBooking>> aup = AupBookings();
    std::shared_ptr<const std::vector<ZoneBooking>> notams = Notams();

    ZoneActivation what;
    what.aup = aup ? aup.get() : &kNoBookings;
    what.notams = notams ? notams.get() : NULL;
    what.showNotamWhenUnknown = m_config.ShowNotamAreas();

    const time_t nowUtc = time(NULL);

    std::vector<char> active(zones.size(), 0);
    std::vector<const ZoneBooking*> bookings(zones.size(), NULL);
    for (size_t i = 0; i < zones.size(); i++)
    {
        const ZoneBooking* hit = NULL;
        active[i] = ZoneActiveNow(zones[i], what, nowUtc, &hit) ? 1 : 0;
        bookings[i] = hit;
    }

    ApwBuildZones(zones, active, bookings, m_config.Apw(), m_apwZones);
}

const ApwResult& CGalaxyATMSystemPlugin::ApwFor(CRadarTarget& target)
{
    static const ApwResult kNone;

    const ApwSettings& cfg = m_config.Apw();
    if (!cfg.enabled || !target.IsValid())
        return kNone;

    RefreshApwZones();
    if (m_apwZones.empty())
        return kNone;

    const std::string callsign = target.GetCallsign();
    const ULONGLONG now = GetTickCount64();

    ApwCacheEntry& entry = m_apwCache[callsign];
    if (Fresh(entry.tick, callsign, kApwRecheckMs))
        return entry.result;

    CRadarTargetPositionData pos = target.GetPosition();
    if (!pos.IsValid())
    {
        entry.tick = now;
        entry.result = ApwResult();
        return entry.result;
    }

    ApwTrack track;
    track.pos = pos.GetPosition();
    track.trackDeg = target.GetTrackHeading();
    track.gsKt = target.GetGS();
    track.vsFpm = target.GetVerticalSpeed();

    const bool belowTL = pos.GetFlightLevel() / 100 < TransitionLevelFL();
    track.altFt = belowTL ? pos.GetPressureAltitude() : pos.GetFlightLevel();

    CFlightPlan fp = target.GetCorrelatedFlightPlan();
    if (fp.IsValid())
    {
        track.origin = Upper(Widen(fp.GetFlightPlanData().GetOrigin()));
        track.destination = Upper(Widen(fp.GetFlightPlanData().GetDestination()));
    }

    entry.result = ApwCheck(m_config.Zones(), m_apwZones, track, cfg);

    if (m_apwCache.size() > 256)
    {
        for (auto it = m_apwCache.begin(); it != m_apwCache.end(); )
        {
            if (it->first != callsign && now - it->second.tick > 60000)
                it = m_apwCache.erase(it);
            else
                ++it;
        }
    }

    return m_apwCache[callsign].result;
}

void CGalaxyATMSystemPlugin::OnGetTagItem(
    CFlightPlan FlightPlan, CRadarTarget RadarTarget,
    int ItemCode, int TagData, char sItemString[16],
    int* pColorCode, COLORREF* pRGB, double* pFontSize)
{
    *pColorCode = EuroScopePlugIn::TAG_COLOR_DEFAULT;
    sItemString[0] = '\0';
    if (!Unlocked())
        return;

    if (pFontSize != NULL && *pFontSize > 0.0
        && ItemCode != TAG_ITEM_SQUAWK && ItemCode != TAG_ITEM_SQUAWK_SET)
        *pFontSize *= m_tagFontSize / 12.0;

    if (ItemCode != TAG_ITEM_APW && ItemCode != TAG_ITEM_SQUAWK && RadarTarget.IsValid())
    {
        CRadarTargetPositionData filterPos = RadarTarget.GetPosition();
        if (filterPos.IsValid() && !AltFilterPasses(filterPos.GetPressureAltitude()))
            return;
    }

    switch (ItemCode)
    {
    case TAG_ITEM_CALLSIGN:
    {
        const char* callsign = FlightPlan.IsValid() ? FlightPlan.GetCallsign()
            : RadarTarget.IsValid() ? RadarTarget.GetCallsign() : NULL;
        if (callsign == NULL)
            return;
        strncpy_s(sItemString, 16, callsign, _TRUNCATE);
        break;
    }
    case TAG_ITEM_ALTITUDE:
    {
        if (!RadarTarget.IsValid())
            return;
        CRadarTargetPositionData pos = RadarTarget.GetPosition();
        if (!pos.IsValid())
            return;
        bool belowTL = pos.GetFlightLevel() / 100 < TransitionLevelFL();
        int altFt = belowTL ? pos.GetPressureAltitude() : pos.GetFlightLevel();
        strncpy_s(sItemString, 16, FormatAltitudeUnit(altFt, m_unitAlt).c_str(), _TRUNCATE);
        break;
    }
    case TAG_ITEM_VERTICAL_SPEED:
    {
        if (!RadarTarget.IsValid())
            return;
        strncpy_s(sItemString, 16, FormatVerticalSpeedUnit(RadarTarget.GetVerticalSpeed(), m_unitVs).c_str(), _TRUNCATE);
        break;
    }
    case TAG_ITEM_GROUND_SPEED:
    {
        if (!RadarTarget.IsValid())
            return;
        strncpy_s(sItemString, 16, FormatGroundSpeedUnit(RadarTarget.GetGS(), m_unitGs).c_str(), _TRUNCATE);
        break;
    }
    case TAG_ITEM_DISTANCE:
    {
        if (!FlightPlan.IsValid())
            return;
        strncpy_s(sItemString, 16, FormatDistanceUnit(FlightPlan.GetDistanceToDestination(), m_unitDist).c_str(), _TRUNCATE);
        break;
    }
    case TAG_ITEM_APW:
    {
        if (!RadarTarget.IsValid())
            return;

        const ApwResult& apw = ApwFor(RadarTarget);
        if (apw.level == ApwLevel::None)
            return;

        std::wstring text = L"APW";
        if (m_config.Apw().showZone && !apw.zoneId.empty())
            text += L" " + apw.zoneId;
        strncpy_s(sItemString, 16, Narrow(text).c_str(), _TRUNCATE);

        *pColorCode = EuroScopePlugIn::TAG_COLOR_RGB_DEFINED;
        *pRGB = (apw.level == ApwLevel::Inside) ? Theme::ApwInside : Theme::ApwPredicted;
        break;
    }
    case TAG_ITEM_SQUAWK:
    {
        if (!FlightPlan.IsValid())
            return;

        std::string callsign = FlightPlan.GetCallsign();

        if (m_squawk.Enabled() && m_squawk.IsPending(callsign))
        {
            strncpy_s(sItemString, 16, "....", _TRUNCATE);
            *pColorCode = EuroScopePlugIn::TAG_COLOR_RGB_DEFINED;
            *pRGB = Theme::SquawkPending;
            break;
        }
        if (m_squawk.Enabled() && !m_squawk.LastError(callsign).empty())
        {
            strncpy_s(sItemString, 16, "ERR", _TRUNCATE);
            *pColorCode = EuroScopePlugIn::TAG_COLOR_RGB_DEFINED;
            *pRGB = Theme::SquawkError;
            break;
        }

        std::string code = AssignedSquawk(FlightPlan);
        strncpy_s(sItemString, 16, code.empty() ? "----" : code.c_str(), _TRUNCATE);
        *pColorCode = EuroScopePlugIn::TAG_COLOR_RGB_DEFINED;
        *pRGB = SquawkColor(FlightPlan, RadarTarget, code);
        break;
    }
    case TAG_ITEM_SQUAWK_SET:
    {
        if (!FlightPlan.IsValid() || !RadarTarget.IsValid())
            return;

        std::string assigned = AssignedSquawk(FlightPlan);
        const char* set = RadarTarget.GetPosition().GetSquawk();
        if (assigned.empty() || set == NULL || *set == '\0' || assigned == set)
            return;

        strncpy_s(sItemString, 16, set, _TRUNCATE);
        *pColorCode = EuroScopePlugIn::TAG_COLOR_RGB_DEFINED;
        *pRGB = Theme::SquawkMismatch;
        break;
    }
    default:
        break;
    }
}
