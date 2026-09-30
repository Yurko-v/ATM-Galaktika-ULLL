#include "pch.h"
#include "Screen/ScreenCommon.h"

using namespace Galaxy;

namespace
{
    const int kRvsmBottomFt = 29000;
    const int kRvsmTopFt = 41000;
    const double kRvsmOccupiedBandM = 30.0;
    const double kOccupiedBandM = 60.0;
    const double kMetresPerFoot = 0.3048;
    const int kOnGroundMaxGsKt = 50;

    int OccupiedBandFt(int flightLevelFt)
    {
        const bool rvsm = flightLevelFt >= kRvsmBottomFt && flightLevelFt <= kRvsmTopFt;
        return (int)lround((rvsm ? kRvsmOccupiedBandM : kOccupiedBandM) / kMetresPerFoot);
    }
}

HFONT CGalaxyATMSystemRadarScreen::GetFormularFont()
{
    int size = Plugin()->TagFontSize();
    if (m_formularFont != NULL && m_formularFontSize == size)
        return m_formularFont;
    if (m_formularFont != NULL)
        DeleteObject(m_formularFont);

    LOGFONTW lf = {};
    const wchar_t* face = Theme::EuroScopeFace();
    if (face != NULL)
        wcscpy_s(lf.lfFaceName, face);
    else if (m_esFont == NULL || GetObjectW(m_esFont, sizeof(lf), &lf) == 0)
        wcscpy_s(lf.lfFaceName, L"Consolas");
    lf.lfHeight = -MulDiv(size, 7, 6);
    lf.lfWidth = 0;
    lf.lfEscapement = lf.lfOrientation = 0;
    lf.lfWeight = FW_NORMAL;
    lf.lfItalic = lf.lfUnderline = lf.lfStrikeOut = FALSE;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfOutPrecision = OUT_TT_PRECIS;
    lf.lfQuality = CLEARTYPE_QUALITY;

    m_formularFont = CreateFontIndirectW(&lf);
    m_formularFontSize = size;
    return m_formularFont;
}

void CGalaxyATMSystemRadarScreen::DrawFormulars(HDC hDC, bool registerObjects)
{
    for (auto& entry : m_formulars)
        entry.second.items.clear();

    if (!m_formularsVisible)
        return;

    CGalaxyATMSystemPlugin* plugin = Plugin();
    RECT ra = GetRadarArea();

    int saved = SaveDC(hDC);
    SetBkMode(hDC, TRANSPARENT);
    SetTextAlign(hDC, TA_LEFT | TA_TOP);
    SelectObject(hDC, GetFormularFont());

    TEXTMETRICW tm;
    GetTextMetricsW(hDC, &tm);
    const int lineH = max(1, (int)(tm.tmHeight + tm.tmExternalLeading));
    SIZE space = { 0, 0 };
    GetTextExtentPoint32W(hDC, L" ", 1, &space);

    const HFONT labelFont = GetFormularFont();
    if (m_runWidthFont != labelFont || m_runWidths.size() > kRunWidthCacheLimit)
    {
        m_runWidths.clear();
        m_runWidthFont = labelFont;
    }
    auto runWidth = [&](const std::wstring& text)
    {
        auto known = m_runWidths.find(text);
        if (known != m_runWidths.end())
            return known->second;
        SIZE sz = { 0, 0 };
        const size_t arrowAt = text.find(kHandoffArrow);
        if (arrowAt == std::wstring::npos)
        {
            GetTextExtentPoint32W(hDC, text.c_str(), (int)text.size(), &sz);
        }
        else
        {
            SIZE part = { 0, 0 };
            GetTextExtentPoint32W(hDC, text.c_str(), (int)arrowAt, &part);
            sz.cx = part.cx + lineH;
            GetTextExtentPoint32W(hDC, text.c_str() + arrowAt + 1, (int)(text.size() - arrowAt - 1), &part);
            sz.cx += part.cx;
        }
        m_runWidths.emplace(text, (int)sz.cx);
        return (int)sz.cx;
    };

    const int tl = plugin->TransitionLevelFL();
    const AltUnit altUnit = plugin->UnitAlt();

    const FormularKind kind = CurrentFormularKind();
    const bool ctrLabel = (kind == FormularKind::Ctr);
    const bool simulator = InSimulatorSession(plugin);
    const FormularFn* const remarkFn = (kind == FormularKind::App) ? &kFnAppRemark : &kFnRemark;
    const std::string myPositionId = PositionIdOf(plugin, plugin->ControllerMyself().GetCallsign());

    std::map<std::string, int> codeCount;
    for (CRadarTarget t = plugin->RadarTargetSelectFirst(); t.IsValid();
         t = plugin->RadarTargetSelectNext(t))
    {
        CRadarTargetPositionData p = t.GetPosition();
        const char* c = p.IsValid() ? p.GetSquawk() : NULL;
        if (c == NULL || strlen(c) != 4 || strcmp(c, "0000") == 0 || strcmp(c, "1200") == 0
            || strcmp(c, "2000") == 0 || strcmp(c, "7000") == 0)
            continue;
        codeCount[c]++;
    }

    struct PendingLeader
    {
        POINT from, to;
        COLORREF color;
    };
    enum class ArrowKind { Up, Down, Handoff };
    struct PendingArrow
    {
        RECT slot;
        ArrowKind kind;
        COLORREF ink;
    };
    std::vector<PendingLeader> leaders;
    std::vector<PendingArrow> arrows;
    auto drawPending = [&]()
    {
        if (leaders.empty() && arrows.empty())
            return;
        Gdiplus::Graphics g(hDC);
        g.SetSmoothingMode(Theme::Smoothing());
        Gdiplus::Pen pen(Theme::GdiColor(RGB(0, 0, 0)), 1.0f);
        for (const PendingLeader& l : leaders)
        {
            pen.SetColor(Theme::GdiColor(l.color));
            g.DrawLine(&pen, (Gdiplus::REAL)l.from.x, (Gdiplus::REAL)l.from.y,
                (Gdiplus::REAL)l.to.x, (Gdiplus::REAL)l.to.y);
        }
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        for (const PendingArrow& a : arrows)
        {
            if (a.kind == ArrowKind::Handoff)
                DrawHandoffArrow(g, a.slot, a.ink);
            else
                DrawTrendArrow(g, a.slot, a.kind == ArrowKind::Up, a.ink);
        }
        leaders.clear();
        arrows.clear();
    };

    for (int pass = 0; pass < 2; pass++, drawPending())
    for (CRadarTarget rt = plugin->RadarTargetSelectFirst(); rt.IsValid();
         rt = plugin->RadarTargetSelectNext(rt))
    {
        CRadarTargetPositionData pos = rt.GetPosition();
        if (!pos.IsValid())
            continue;

        POINT tp = ConvertCoordFromPositionToPixel(pos.GetPosition());
        if (!PtInRect(&ra, tp))
            continue;

        if (!plugin->AltFilterPasses(pos.GetPressureAltitude()))
            continue;

        CFlightPlan fp = rt.GetCorrelatedFlightPlan();
        const char* cs = fp.IsValid() ? fp.GetCallsign() : rt.GetCallsign();
        if (cs == NULL || *cs == '\0')
            continue;
        const std::string callsign = cs;
        const bool expanded = !m_formularHover.empty() && m_formularHover == callsign;
        const bool rcPicked = !expanded && m_rcPicked.count(callsign) != 0;
        const bool sharedPicked = !expanded && !rcPicked && plugin->IsSharedMarked(callsign);
        if ((expanded || rcPicked || sharedPicked) != (pass == 1))
            continue;

        const ApwResult& apw = plugin->ApwForTarget(rt);
        const COLORREF tagColor = GetTagColorForFlightPlan(fp);
        const int fpState = fp.IsValid() ? fp.GetState() : FLIGHT_PLAN_STATE_NON_CONCERNED;
        const COLORREF base = (fpState == FLIGHT_PLAN_STATE_NOTIFIED || fpState == FLIGHT_PLAN_STATE_COORDINATED)
            ? Theme::FormularInbound : tagColor;
        const char* sq = pos.GetSquawk();

        const bool correlated = fp.IsValid();

        std::vector<FormularRun> warnings;
        if (simulator && correlated)
            warnings.push_back({ fp.GetSimulated() ? L"{*}" : L"{}", base, &kFnSimulation });
        if (sq != NULL)
        {
            auto dup = codeCount.find(sq);
            if (dup != codeCount.end() && dup->second > 1)
                warnings.push_back({ L"SSR", Theme::DuplicateText, NULL });
        }
        if (correlated && sq != NULL && *sq != '\0')
        {
            std::string assigned = plugin->AssignedSquawkFor(fp);
            if (!assigned.empty() && assigned != sq)
                warnings.push_back({ L"A" + Widen(sq), Theme::SquawkMismatch,
                    ctrLabel ? &kFnSquawkWarning : &kFnAppSquawkWarning });
        }
        if (correlated)
        {
            char com = fp.GetControllerAssignedData().GetCommunicationType();
            if (com == 0 || com == ' ' || com == '?')
                com = fp.GetFlightPlanData().GetCommunicationType();
            com = (char)tolower((unsigned char)com);
            if (com == 't' || com == 'r')
                warnings.push_back({ std::wstring(1, (wchar_t)com), base, &kFnCommunication });
            if (fp.GetTrackingControllerIsMe() && (fp.GetRAMFlag() || RouteAdherenceAlert(fp, rt)))
                warnings.push_back({ L"RAM", Theme::DuplicateText, NULL });
            if (fp.GetCLAMFlag())
                warnings.push_back({ L"CLAM", Theme::DuplicateText, NULL });
        }
        if (apw.level != ApwLevel::None)
        {
            std::wstring text = L"APW";
            if (plugin->GetConfig().Apw().showZone && !apw.zoneId.empty())
                text += L" " + apw.zoneId;
            warnings.push_back({ text,
                apw.level == ApwLevel::Inside ? Theme::ApwInside : Theme::ApwPredicted, NULL });
        }
        if (SeparationLost(rt.GetCallsign()))
            warnings.push_back({ L"SSA", Theme::SeparationLoss, NULL });
        if (sq != NULL && strcmp(sq, "7700") == 0)
            warnings.push_back({ L"EM", Theme::DistressText, NULL });
        else if (sq != NULL && strcmp(sq, "7600") == 0)
            warnings.push_back({ L"RDO", Theme::DistressText, NULL });
        else if (sq != NULL && strcmp(sq, "7500") == 0)
            warnings.push_back({ L"HIJ", Theme::DistressText, NULL });
        std::vector<FormularRun> freeText;
        if (correlated)
        {
            const char* remark = fp.GetControllerAssignedData().GetScratchPadString();
            auto local = m_localFreeText.find(callsign);
            if ((remark == NULL || *remark == '\0') && local != m_localFreeText.end())
                remark = local->second.c_str();
            if (remark != NULL && *remark != '\0')
            {
                std::wstring text = Widen(remark);
                std::wstring upper = text;
                CharUpperBuffW(&upper[0], (DWORD)upper.size());
                bool missedApproach = false;
                size_t at;
                while ((at = upper.find(L"MISAP")) != std::wstring::npos)
                {
                    text.erase(at, 5);
                    upper.erase(at, 5);
                    missedApproach = true;
                }
                if (missedApproach)
                {
                    std::wstring rest;
                    for (wchar_t c : text)
                        if (c != L'_')
                            rest += c;
                    size_t first = rest.find_first_not_of(L" \t");
                    size_t last = rest.find_last_not_of(L" \t");
                    text = (first == std::wstring::npos) ? std::wstring() : rest.substr(first, last - first + 1);
                    warnings.push_back({ L"MAPP", Theme::FormularMapp, remarkFn });
                }
                if (!text.empty())
                    (ctrLabel ? freeText : warnings).push_back({ text, base, remarkFn });
            }
        }

        const char* planType = correlated ? fp.GetFlightPlanData().GetPlanType() : NULL;
        const bool vfr = planType != NULL && (planType[0] == 'V' || planType[0] == 'v');
        if (kind == FormularKind::Twr && vfr)
            warnings.push_back({ L"V", Theme::FormularVfr, NULL });

        for (FormularRun& run : warnings)
            run.color = RGB(GetRValue(run.color) * Theme::FormularWarningBrightnessPct / 100,
                GetGValue(run.color) * Theme::FormularWarningBrightnessPct / 100,
                GetBValue(run.color) * Theme::FormularWarningBrightnessPct / 100);

        std::vector<FormularRun> ident;
        ident.push_back({ Widen(callsign.c_str()), base, &kFnCallsign });
        if (correlated)
        {
            if (!ctrLabel)
            {
                const char wtc = fp.GetFlightPlanData().GetAircraftWtc();
                if (wtc == 'H' || wtc == 'J')
                    ident.push_back({ std::wstring(1, (wchar_t)wtc), Theme::FormularWtc, NULL });
            }

            std::string si;
            const char* current = fp.GetTrackingControllerId();
            if (current != NULL)
                si = current;
            const char* next = fp.GetCoordinatedNextController();
            if (next != NULL && *next != '\0')
            {
                CController nextController = plugin->ControllerSelect(next);
                const char* nextId = nextController.IsValid() ? nextController.GetPositionId()
                    : (strlen(next) <= 3 ? next : NULL);
                if (nextId != NULL && *nextId != '\0')
                    si = nextId;
            }
            const char* handoffTarget = fp.GetHandoffTargetControllerId();
            if (fpState == FLIGHT_PLAN_STATE_TRANSFER_FROM_ME_INITIATED && handoffTarget != NULL && *handoffTarget != '\0')
                ident.push_back({ Widen(current != NULL ? current : "") + kHandoffArrow + Widen(handoffTarget),
                    Theme::FormularHandoff, kind == FormularKind::Twr ? &kFnTwrSector : &kFnSector });
            else if (!si.empty())
                ident.push_back({ Widen(si.c_str()), base,
                    kind == FormularKind::Twr ? &kFnTwrSector : &kFnSector });
            if (ctrLabel && !expanded)
                ident.push_back({ L"#", base, NULL });

            if (vfr && ctrLabel && !expanded)
                ident.push_back({ L"V", base, &kFnFlightRule });
            else if (vfr && kind == FormularKind::App && expanded)
                ident.push_back({ L"V", Theme::FormularVfr, NULL });
        }
        if (ctrLabel && expanded && sq != NULL && *sq != '\0')
        {
            ident.push_back({ L"#", base, NULL });
            ident.push_back({ Widen(sq), base, correlated ? &kFnTssr : NULL });
        }
        if (ctrLabel && expanded && planType != NULL && isalpha((unsigned char)planType[0]))
            ident.push_back({ std::wstring(1, (wchar_t)toupper((unsigned char)planType[0])), base, &kFnFlightRule });
        if (ctrLabel && expanded && plugin->IsEnglish(callsign))
            ident.push_back({ L"\x221A", base, NULL });

        std::vector<FormularRun> levels;
        const bool belowTL = pos.GetFlightLevel() / 100 < tl;
        const int altFt = belowTL ? pos.GetPressureAltitude() : pos.GetFlightLevel();
        const bool english = ctrLabel && plugin->IsEnglish(callsign);
        levels.push_back({ Widen(FormatAltitudeUnit(altFt, altUnit).c_str()),
            base, correlated ? (ctrLabel ? &kFnAfl : &kFnAppAfl) : NULL });
        const int vs = rt.GetVerticalSpeed();
        const int occupiedBandFt = OccupiedBandFt(pos.GetFlightLevel());
        const bool onGround = rt.GetGS() < kOnGroundMaxGsKt;
        std::wstring cflText;
        COLORREF cflColor = base;
        int clearedFt = 0;
        bool clearedApproach = false;
        if (correlated)
        {
            int cfl = fp.GetControllerAssignedData().GetClearedAltitude();
            if (cfl == 1)
            {
                cflText = L"CA";
                clearedApproach = true;
            }
            else if (cfl == 2)
            {
                cflText = L"VA";
                clearedApproach = true;
            }
            else if (cfl > 2)
            {
                cflText = Widen(FormatAltitudeUnit(cfl, altUnit).c_str());
                clearedFt = cfl;
                const bool level = vs > -100 && vs < 100;
                if ((level && abs(altFt - cfl) > occupiedBandFt)
                    || (vs >= 100 && altFt > cfl + occupiedBandFt) || (vs <= -100 && altFt < cfl - occupiedBandFt))
                    cflColor = Theme::DuplicateText;
            }
            if (cflText.empty())
            {
                const int rfl = fp.GetFlightPlanData().GetFinalAltitude();
                cflText = Widen(FormatAltitudeUnit(rfl > 0 ? rfl : altFt, altUnit).c_str());
            }
        }

        const wchar_t* trend = NULL;
        if (onGround)
            trend = NULL;
        else if (clearedApproach)
            trend = L"\x2193";
        else if (clearedFt > 0)
            trend = (clearedFt > altFt + occupiedBandFt) ? L"\x2191"
                  : (clearedFt < altFt - occupiedBandFt) ? L"\x2193" : NULL;
        else
            trend = (vs >= 100) ? L"\x2191" : (vs <= -100) ? L"\x2193" : NULL;
        if (trend != NULL)
            levels.push_back({ trend, base, NULL });

        if (correlated)
        {
            const bool picking = m_cflOpen && !m_cflPicksExitLevel && m_cflCallsign == callsign;
            levels.push_back({ cflText, picking ? Theme::Text : cflColor, ctrLabel ? &kFnCfl : &kFnAppCfl,
                picking ? Theme::FormularHoverTarget : CLR_INVALID });
        }
        if (m_osSpeed && ctrLabel)
            levels.push_back({ Widen(FormatGroundSpeedUnit(rt.GetGS(), plugin->UnitGs()).c_str()),
                base, correlated ? &kFnGs : NULL });
        if (english && !expanded)
            levels.push_back({ L"\x221A", base, NULL });
        if (ctrLabel && correlated)
        {
            const RvsmStatus rvsm = RvsmStatusOf(fp);
            if (rvsm == RvsmStatus::Approved && expanded)
                levels.push_back({ L"R", base, NULL });
            else if (rvsm == RvsmStatus::NotApproved)
                levels.push_back({ L"N", Theme::RvsmMark, NULL });
            else if (rvsm == RvsmStatus::Exempt)
                levels.push_back({ L"E", Theme::RvsmMark, NULL });
        }

        std::wstring ahdgText, aspText, arcText;
        if (correlated)
        {
            CFlightPlanControllerAssignedData assigned = fp.GetControllerAssignedData();
            wchar_t t[16];
            if (assigned.GetAssignedHeading() > 0)
            {
                swprintf_s(t, L"H%03d", assigned.GetAssignedHeading());
                ahdgText = t;
            }
            else if (!ctrLabel)
            {
                const char* direct = assigned.GetDirectToPointName();
                if (direct != NULL && *direct != '\0')
                    ahdgText = Widen(direct);
            }
            if (assigned.GetAssignedMach() > 0)
            {
                swprintf_s(t, L"M%d.%02d", assigned.GetAssignedMach() / 100, assigned.GetAssignedMach() % 100);
                aspText = t;
            }
            else if (assigned.GetAssignedSpeed() > 0)
            {
                swprintf_s(t, L"N%03d", assigned.GetAssignedSpeed());
                aspText = t;
            }
            if (!aspText.empty())
            {
                char modifier = TopSkySpeedModifier(assigned);
                if (modifier != 0)
                    aspText += (wchar_t)modifier;
            }
            if (assigned.GetAssignedRate() != 0)
            {
                swprintf_s(t, L"R%d", assigned.GetAssignedRate());
                arcText = t;
            }
        }

        std::vector<std::vector<FormularRun>> coordLines;
        if (correlated)
        {
            FormularState& st = m_formulars[callsign];
            const ULONGLONG now = GetTickCount64();
            const int exitState = fp.GetExitCoordinationAltitudeState();
            const int entryState = fp.GetEntryCoordinationAltitudeState();
            auto track = [now, &st, &callsign](CoordWatch& w, bool exit, int s, int fl, const char* point = NULL)
            {
                if (s == COORDINATION_STATE_REQUESTED_BY_ME && w.lastState != COORDINATION_STATE_REQUESTED_BY_ME)
                    w.decision = CoordDecision::None;
                if ((s == COORDINATION_STATE_REQUESTED_BY_ME || s == COORDINATION_STATE_REQUESTED_BY_OTHER)
                    && point != NULL)
                    w.pointName = point;
                if (w.lastState != 0 && w.lastState != s
                    && (w.lastState == COORDINATION_STATE_REQUESTED_BY_ME || w.lastState == COORDINATION_STATE_REQUESTED_BY_OTHER)
                    && s != COORDINATION_STATE_REQUESTED_BY_ME && s != COORDINATION_STATE_REQUESTED_BY_OTHER)
                {
                    const std::string nowPoint = point != NULL ? point : "";
                    const bool valueKept = point != NULL ? _stricmp(nowPoint.c_str(), w.pointName.c_str()) == 0
                                                         : fl == w.levelFt;
                    if (w.decision == CoordDecision::Cancelled)
                        w.result = COORDINATION_STATE_REFUSED;
                    else if (w.decision == CoordDecision::Manual)
                        w.result = COORDINATION_STATE_MANUAL_ACCEPTED;
                    else if (w.lastState == COORDINATION_STATE_REQUESTED_BY_OTHER && w.myReply != 0)
                        w.result = w.myReply;
                    else if (s == COORDINATION_STATE_ACCEPTED || s == COORDINATION_STATE_MANUAL_ACCEPTED
                        || s == COORDINATION_STATE_REFUSED)
                        w.result = s;
                    else
                        w.result = valueKept ? COORDINATION_STATE_ACCEPTED : COORDINATION_STATE_REFUSED;
                    const bool cancelled = w.decision == CoordDecision::Cancelled;
                    if (w.decision == CoordDecision::None)
                        w.resultAt = now;
                    if (cancelled)
                        w.result = 0;
                    w.decision = CoordDecision::None;
                    w.wasMine = (w.lastState == COORDINATION_STATE_REQUESTED_BY_ME);
                    const bool accepted = !cancelled && w.result != COORDINATION_STATE_REFUSED;
                    if (accepted && point != NULL && !w.pointName.empty())
                        (exit ? st.agreedCopx : st.agreedEntryPoint) = w.pointName;
                    else if (accepted && point == NULL && w.levelFt > 0)
                        (exit ? st.agreedXflFt : st.agreedEntryFt) = w.levelFt;
                    Log::Info("formular", callsign + ": coordination " + (point != NULL ? "DCT " + w.pointName
                        : "level " + std::to_string(w.levelFt)) + " state " + std::to_string(w.lastState)
                        + " -> " + std::to_string(s) + ", now " + (point != NULL ? nowPoint : std::to_string(fl))
                        + (cancelled ? ", cancelled here" : accepted ? ", agreed" : ", refused"));
                }
                if (s != COORDINATION_STATE_REQUESTED_BY_OTHER)
                    w.myReply = 0;
                w.lastState = s;
                if (s == COORDINATION_STATE_REQUESTED_BY_ME || s == COORDINATION_STATE_REQUESTED_BY_OTHER)
                    w.levelFt = fl;
            };
            track(st.exitCoord, true, exitState, fp.GetExitCoordinationAltitude());
            track(st.entryCoord, false, entryState, fp.GetEntryCoordinationAltitude());
            track(st.exitPoint, true, fp.GetExitCoordinationNameState(), 0, fp.GetExitCoordinationPointName());
            track(st.entryPoint, false, fp.GetEntryCoordinationPointState(), 0, fp.GetEntryCoordinationPointName());

            const std::string& me = myPositionId;
            std::string next = PositionIdOf(plugin, fp.GetCoordinatedNextController());
            std::string prev = PositionIdOf(plugin, fp.GetTrackingControllerCallsign());
            if (next.empty() || prev.empty() || prev == me)
            {
                const std::string partner = PositionIdOf(plugin, CoordPartner(plugin, fp).c_str());
                if (next.empty())
                    next = partner;
                if (prev.empty() || prev == me)
                    prev = partner;
            }

            auto show = [&](const CoordWatch& w, bool exit, bool point)
            {
                if (!point && w.levelFt <= 0)
                    return false;
                COLORREF ink;
                bool mine;
                if (w.lastState == COORDINATION_STATE_REQUESTED_BY_ME && w.decision == CoordDecision::Cancelled)
                {
                    return false;
                }
                else if (w.lastState == COORDINATION_STATE_REQUESTED_BY_ME && w.decision == CoordDecision::Manual)
                {
                    if (now - w.resultAt >= kCoordResultMs)
                        return false;
                    ink = Theme::FormularGreen;
                    mine = true;
                }
                else if (w.lastState == COORDINATION_STATE_REQUESTED_BY_ME || w.lastState == COORDINATION_STATE_REQUESTED_BY_OTHER)
                {
                    ink = Theme::DuplicateText;
                    mine = (w.lastState == COORDINATION_STATE_REQUESTED_BY_ME);
                }
                else if (w.result != 0 && now - w.resultAt < kCoordResultMs)
                {
                    const bool accepted = w.result != COORDINATION_STATE_REFUSED;
                    ink = accepted ? Theme::FormularGreen : Theme::DistressText;
                    mine = w.wasMine;
                }
                else
                {
                    return false;
                }
                std::string from = exit ? me : prev, to = exit ? next : me;
                if (mine != exit)
                    std::swap(from, to);
                const bool incoming = (w.lastState == COORDINATION_STATE_REQUESTED_BY_OTHER);
                std::vector<FormularRun> coordLine;
                coordLine.push_back({ Widen((from + ">" + to + (point ? " DCT:" : " HFL:")).c_str()), base,
                    incoming ? &kFnCoordReply : NULL });
                std::wstring value;
                if (point)
                    value = w.pointName.empty() ? std::wstring(L"---") : Widen(w.pointName.c_str());
                else
                    value = w.levelFt > 0 ? Widen(FormatAltitudeUnit(w.levelFt, altUnit).c_str()) : std::wstring(L"---");
                const FormularFn* mineFn = point ? (exit ? &kFnCoordExitPoint : &kFnCoordEntryPoint)
                                                 : (exit ? &kFnCoordExitLevel : &kFnCoordEntryLevel);
                coordLine.push_back({ value, ink, incoming ? &kFnCoordReply : mine ? mineFn : NULL });
                coordLines.push_back(coordLine);
                return true;
            };
            if (!show(st.exitCoord, true, false))
                show(st.entryCoord, false, false);
            if (!show(st.exitPoint, true, true))
                show(st.entryPoint, false, true);
        }

        std::vector<std::vector<FormularRun>> extra;
        if (expanded && correlated && ctrLabel)
        {
            CFlightPlanData fpd = fp.GetFlightPlanData();

            std::vector<FormularRun> exitLine;
            int xfl = AgreedXfl(fp);
            const bool pickingXfl = m_cflOpen && m_cflPicksExitLevel && m_cflCallsign == callsign;
            exitLine.push_back({ xfl > 0 ? Widen(FormatAltitudeUnit(xfl, altUnit).c_str())
                                         : std::wstring(L"XFL"),
                pickingXfl ? Theme::Text : base, &kFnXfl,
                pickingXfl ? Theme::FormularHoverTarget : CLR_INVALID });
            std::string copx = DirectPointAhead(fp);
            if (copx.empty())
                copx = AgreedCopx(fp);
            exitLine.push_back({ !copx.empty() ? Widen(copx.c_str()) : std::wstring(L"COPX"),
                base, &kFnCopx });
            extra.push_back(exitLine);
            extra.insert(extra.end(), coordLines.begin(), coordLines.end());

            std::vector<FormularRun> assignedLine;
            assignedLine.push_back({ ahdgText.empty() ? std::wstring(L"ahdg") : ahdgText, base, &kFnAhdg });
            assignedLine.push_back({ aspText.empty() ? std::wstring(L"spd") : aspText, base, &kFnAsp });
            extra.push_back(assignedLine);

            std::vector<FormularRun> planLine;
            const char* atyp = fpd.GetAircraftFPType();
            planLine.push_back({ (atyp != NULL && *atyp != '\0') ? Widen(atyp) : std::wstring(L"ATYP"),
                base, &kFnAtyp });
            const std::wstring& airline = AirlineName(callsign.c_str());
            if (!airline.empty())
                planLine.push_back({ airline, base, NULL });
            extra.push_back(planLine);
        }
        else if (correlated && ctrLabel)
        {
            extra.insert(extra.end(), coordLines.begin(), coordLines.end());
            std::vector<FormularRun> assignedLine;
            std::string point = DirectPointAhead(fp);
            if (point.empty())
            {
                const bool entry = TrackedByOther(fp);
                auto known = m_formulars.find(callsign);
                if (known != m_formulars.end())
                    point = entry ? known->second.agreedEntryPoint : known->second.agreedCopx;
                const char* coordinated = entry ? fp.GetEntryCoordinationPointName() : fp.GetExitCoordinationPointName();
                const int coordination = entry ? fp.GetEntryCoordinationPointState() : fp.GetExitCoordinationNameState();
                if (point.empty() && CoordinationHolds(coordination) && coordinated != NULL)
                    point = coordinated;
                if (!point.empty() && PointPassed(fp, point.c_str()))
                    point.clear();
            }
            if (!point.empty())
                assignedLine.push_back({ Widen(point.c_str()), base, &kFnCopx });
            if (!ahdgText.empty())
                assignedLine.push_back({ ahdgText, base, &kFnAhdg });
            if (!aspText.empty())
                assignedLine.push_back({ aspText, base, &kFnAsp });
            if (!arcText.empty())
                assignedLine.push_back({ arcText, base, &kFnArc });
            if (!assignedLine.empty())
                extra.push_back(assignedLine);
        }
        else if (correlated)
        {
            extra.insert(extra.end(), coordLines.begin(), coordLines.end());
            CFlightPlanControllerAssignedData cad = fp.GetControllerAssignedData();
            CFlightPlanData fpd = fp.GetFlightPlanData();
            const bool app = (kind == FormularKind::App);
            wchar_t buf[32];

            std::wstring speed;
            if (cad.GetAssignedMach() > 0)
            {
                swprintf_s(buf, L"M%02d", cad.GetAssignedMach());
                speed = buf;
            }
            else if (cad.GetAssignedSpeed() > 0)
            {
                if (app)
                    swprintf_s(buf, L"%02d", cad.GetAssignedSpeed() / 10);
                else
                    swprintf_s(buf, L"%03d", cad.GetAssignedSpeed());
                speed = buf;
            }
            if (!speed.empty())
            {
                char modifier = TopSkySpeedModifier(cad);
                if (modifier != 0)
                    speed += (wchar_t)modifier;
            }

            const FormularRun gsRun = { Widen(FormatGroundSpeedUnit(rt.GetGS(), plugin->UnitGs()).c_str()),
                base, app ? &kFnGs : &kFnTwrGs };
            const char* atyp = fpd.GetAircraftFPType();
            const FormularRun typeRun = { (atyp != NULL && *atyp != '\0') ? Widen(atyp) : std::wstring(L"ATYP"),
                base, &kFnAppAtyp };
            const FormularFn* const ahdgFn = app ? &kFnAppAhdg : &kFnAhdg;

            if (!expanded)
            {
                std::vector<FormularRun> speedLine;
                if (m_osSpeed)
                    speedLine.push_back(gsRun);
                if (!speed.empty())
                    speedLine.push_back({ speed, base, &kFnAsp });
                speedLine.push_back(typeRun);
                extra.push_back(speedLine);

                if (!ahdgText.empty())
                    extra.push_back(std::vector<FormularRun>(1, FormularRun{ ahdgText, base, ahdgFn }));
            }
            else
            {
                std::vector<FormularRun> speedLine;
                if (m_osSpeed)
                    speedLine.push_back(gsRun);
                speedLine.push_back({ speed.empty() ? std::wstring(L"ASP") : speed, base, &kFnAsp });
                extra.push_back(speedLine);

                {
                    std::vector<FormularRun> exitLine;
                    int xfl = AgreedXfl(fp);
                    const bool pickingXfl = m_cflOpen && m_cflPicksExitLevel && m_cflCallsign == callsign;
                    exitLine.push_back({ xfl > 0 ? Widen(FormatAltitudeUnit(xfl, altUnit).c_str())
                                                 : std::wstring(L"XFL"),
                        pickingXfl ? Theme::Text : base, &kFnXfl,
                        pickingXfl ? Theme::FormularHoverTarget : CLR_INVALID });
                    std::string copx = DirectPointAhead(fp);
                    if (copx.empty())
                        copx = AgreedCopx(fp);
                    exitLine.push_back({ !copx.empty() ? Widen(copx.c_str()) : std::wstring(L"COPX"),
                        base, &kFnCopx });
                    int ias = 0, machX100 = 0;
                    if (app && CalculatedIasMach(rt.GetGS(), pos.GetFlightLevel(), ias, machX100))
                    {
                        swprintf_s(buf, L"N%03d", ias);
                        exitLine.push_back({ buf, base, NULL });
                    }
                    extra.push_back(exitLine);
                }

                std::vector<FormularRun> planLine;
                planLine.push_back(typeRun);
                const char* ades = fpd.GetDestination();
                if (ades != NULL && *ades != '\0')
                    planLine.push_back({ Widen(ades), base, &kFnAdes });
                const char* arwy = fpd.GetArrivalRwy();
                if (arwy != NULL && *arwy != '\0')
                    planLine.push_back({ Widen(arwy), Theme::FormularGreen, &kFnArwy });
                else
                    planLine.push_back({ L"ARWY", base, &kFnArwy });
                extra.push_back(planLine);

                extra.push_back(std::vector<FormularRun>(1,
                    FormularRun{ ahdgText.empty() ? std::wstring(L"AHDG") : ahdgText, base, ahdgFn }));
            }

            const char* ctl = cad.GetFlightStripAnnotation(kVchCtlAnnotation);
            if (ctl != NULL && strcmp(ctl, "CTL") == 0 && fp.GetTrackingControllerIsMe())
                extra.push_back(std::vector<FormularRun>(1, FormularRun{ L"CTL", Theme::FormularGreen, NULL }));
        }

        std::vector<std::vector<FormularRun>> lines;
        if (m_osLines == 3)
        {
            if (!warnings.empty())
                lines.push_back(warnings);
            lines.push_back(ident);
        }
        else
        {
            ident.insert(ident.end(), warnings.begin(), warnings.end());
            lines.push_back(ident);
        }
        lines.push_back(levels);
        lines.insert(lines.end(), extra.begin(), extra.end());
        if (!freeText.empty())
            lines.push_back(freeText);

        int width = 0;
        std::vector<std::vector<int>> runWidths(lines.size());
        std::vector<int> lineWidths(lines.size(), 0);
        for (size_t l = 0; l < lines.size(); l++)
        {
            int w = 0;
            for (size_t r = 0; r < lines[l].size(); r++)
            {
                const int runW = runWidth(lines[l][r].text);
                runWidths[l].push_back(runW);
                w += runW + (r > 0 ? space.cx : 0);
            }
            lineWidths[l] = w;
            width = max(width, w);
        }
        const int height = (int)lines.size() * lineH;

        const size_t identLine = (m_osLines == 3 && !warnings.empty()) ? 1 : 0;

        FormularState& state = m_formulars[callsign];
        if (!state.placed)
        {
            state.offset = { 16, -14 };
            state.placed = true;
        }
        state.anchor = tp;
        POINT callsignAt = { tp.x + state.offset.x, tp.y + state.offset.y };
        state.callsignAt = callsignAt;

        RECT area;
        area.left = callsignAt.x;
        area.top = callsignAt.y - (int)identLine * lineH - lineH / 2;
        area.right = area.left + width;
        area.bottom = area.top + height;
        state.area = area;

        const bool boxed = expanded && ctrLabel;
        const int kBoxPad = 3;
        RECT box = area;
        InflateRect(&box, kBoxPad, kBoxPad);
        if (boxed)
            state.area = box;

        std::vector<RECT> rows;
        if (boxed)
        {
            rows.push_back(box);
        }
        else
        {
            for (size_t l = 0; l < lines.size(); l++)
            {
                RECT row = { area.left, area.top + (int)l * lineH,
                             area.left + lineWidths[l], area.top + (int)(l + 1) * lineH };
                InflateRect(&row, 3, 1);
                rows.push_back(row);
            }
        }

        const POINT aim = boxed
            ? POINT{ (box.left + box.right) / 2, (box.top + box.bottom) / 2 }
            : POINT{ area.left + lineWidths[identLine] / 2, callsignAt.y };
        POINT leaderFrom, leaderTo;
        if (ClipLeaderToText(tp, aim, rows, boxed ? 4.0 : 6.0, leaderFrom, leaderTo))
            leaders.push_back({ leaderFrom, leaderTo,
                rcPicked ? Theme::FormularPicked : sharedPicked ? Theme::FormularShared
                : boxed ? Theme::Text : tagColor });

        if (boxed)
        {
            HBRUSH fill = CreateSolidBrush(RGB(0, 0, 0));
            FillRect(hDC, &box, fill);
            DeleteObject(fill);
            HBRUSH edge = CreateSolidBrush(Theme::Text);
            FrameRect(hDC, &box, edge);
            DeleteObject(edge);
        }

        RECT ahdgRect = { 0, 0, 0, 0 };
        bool haveAhdg = false;
        for (size_t l = 0; l < lines.size(); l++)
        {
            int x = area.left;
            int y = area.top + (int)l * lineH;
            std::wstring group;
            int groupX = x;
            COLORREF groupInk = CLR_INVALID;
            auto flushGroup = [&]()
            {
                if (group.empty())
                    return;
                SetTextColor(hDC, groupInk);
                TextOutW(hDC, groupX, y, group.c_str(), (int)group.size());
                group.clear();
            };
            for (size_t r = 0; r < lines[l].size(); r++)
            {
                const FormularRun& run = lines[l][r];
                RECT bg = { x - 1, y, x + runWidths[l][r] + 1, y + lineH };
                const bool hot = registerObjects && run.fn != NULL && (ctrLabel || run.fn == &kFnCoordReply || IsMyCoordFn(run.fn)) && Hot(bg);
                const COLORREF back = hot ? Theme::HoverFill : run.back;
                if (back != CLR_INVALID)
                {
                    flushGroup();
                    HBRUSH b = CreateSolidBrush(back);
                    FillRect(hDC, &bg, b);
                    DeleteObject(b);
                }
                const COLORREF ink = back != CLR_INVALID ? Theme::Text
                    : rcPicked ? Theme::FormularPicked
                    : sharedPicked ? Theme::FormularShared
                    : (boxed && run.color == base) ? Theme::Text : run.color;
                const size_t handoffArrowAt = run.text.find(kHandoffArrow);
                const bool special = run.text == L"\x221A" || run.text == L"\x2191" || run.text == L"\x2193"
                    || handoffArrowAt != std::wstring::npos;
                const bool plain = back == CLR_INVALID && !special;
                if (!plain || (!group.empty() && groupInk != ink))
                    flushGroup();
                if (plain)
                {
                    if (group.empty())
                    {
                        groupX = x;
                        groupInk = ink;
                    }
                    else
                        group += L' ';
                    group += run.text;
                }
                else if (!special)
                {
                    SetTextColor(hDC, ink);
                    TextOutW(hDC, x, y, run.text.c_str(), (int)run.text.size());
                }

                if (run.text == L"\x221A")
                {
                    const int w = runWidths[l][r];
                    const int top = y + lineH * 3 / 10, bottom = y + lineH * 4 / 5;
                    const POINT tick[3] = { { x + w / 10, (top + bottom) / 2 + lineH / 20 },
                                            { x + w * 2 / 5, bottom },
                                            { x + w - w / 10, top } };
                    HPEN pen = CreatePen(PS_SOLID, max(2, lineH / 7), ink);
                    HGDIOBJ old = SelectObject(hDC, pen);
                    Polyline(hDC, tick, 3);
                    SelectObject(hDC, old);
                    DeleteObject(pen);
                }
                else if (run.text == L"\x2191" || run.text == L"\x2193")
                {
                    const RECT slot = { x, y, x + runWidths[l][r], y + lineH };
                    arrows.push_back({ slot, run.text == L"\x2191" ? ArrowKind::Up : ArrowKind::Down, ink });
                }
                else if (handoffArrowAt != std::wstring::npos)
                {
                    SIZE before = { 0, 0 };
                    GetTextExtentPoint32W(hDC, run.text.c_str(), (int)handoffArrowAt, &before);
                    SetTextColor(hDC, ink);
                    TextOutW(hDC, x, y, run.text.c_str(), (int)handoffArrowAt);
                    const RECT slot = { x + before.cx, y, x + before.cx + lineH, y + lineH };
                    arrows.push_back({ slot, ArrowKind::Handoff, ink });
                    TextOutW(hDC, slot.right, y, run.text.c_str() + handoffArrowAt + 1,
                        (int)(run.text.size() - handoffArrowAt - 1));
                }

                FormularItem item;
                item.rect = { x, y, x + runWidths[l][r], y + lineH };
                item.fn = run.fn;
                item.text = Narrow(run.text);
                state.items.push_back(item);
                if (IsAhdgFn(run.fn))
                {
                    ahdgRect = item.rect;
                    haveAhdg = true;
                }

                x += runWidths[l][r] + space.cx;
            }
            flushGroup();
        }

        if (registerObjects)
        {
            RECT hit = area;
            InflateRect(&hit, 2, 1);
            AddScreenObject(SO_FORMULAR, callsign.c_str(), hit, true, "");

            if (haveAhdg)
                AddScreenObject(SO_FORMULAR_AHDG, callsign.c_str(), ahdgRect, true,
                    Tr("ЛКМ - снять курс, тянуть ЛКМ - назначить курс, ПКМ - меню курса"));
        }
    }

    if (m_hdgDragging && m_hdgDragMoved)
    {
        POINT from = { 0, 0 };
        double distNm = 0.0, drawnHdg = 0.0;
        int hdg = DragHeading(m_hdgDragCallsign.c_str(), m_hdgDragPt, &from, &distNm, &drawnHdg);
        if (hdg > 0)
        {
            std::vector<POINT> path;
            HeadingTurnPath(plugin->RadarTargetSelect(m_hdgDragCallsign.c_str()),
                drawnHdg, distNm, path);
            {
                VectorCanvas canvas(hDC, Theme::HeadingDragLine);
                const Gdiplus::REAL w = max((Gdiplus::REAL)0.5f, canvas.pen.GetWidth());
                const Gdiplus::REAL dashes[] = { 14.0f / w, 6.0f / w };
                canvas.pen.SetDashPattern(dashes, 2);
                if (path.size() >= 2)
                    canvas.Polyline(path);
                else
                    canvas.Line(from.x, from.y, m_hdgDragPt.x, m_hdgDragPt.y);
            }

            const double dist = (plugin->UnitDist() == DistUnit::Km) ? distNm * 1.852 : distNm;
            wchar_t text[32];
            swprintf_s(text, L"%.1f/%03d", dist, hdg);

            const POINT tip = path.size() >= 2 ? path.back() : m_hdgDragPt;

            SIZE sz = { 0, 0 };
            GetTextExtentPoint32W(hDC, text, (int)wcslen(text), &sz);
            SetTextColor(hDC, Theme::HeadingDragText);
            TextOutW(hDC, tip.x - 2, tip.y - sz.cy - 2, text, (int)wcslen(text));
        }
    }

    RestoreDC(hDC, saved);
}

bool CGalaxyATMSystemRadarScreen::HoveredCtrLabel(const char* callsign)
{
    return m_formularsVisible && callsign != NULL && !m_formularHover.empty()
        && m_formularHover == callsign && CurrentFormularKind() == FormularKind::Ctr;
}

CGalaxyATMSystemRadarScreen::FormularKind CGalaxyATMSystemRadarScreen::CurrentFormularKind()
{
    switch (m_formularKindSetting)
    {
    case FormularKindSetting::Ctr: return FormularKind::Ctr;
    case FormularKindSetting::App: return FormularKind::App;
    case FormularKindSetting::Twr: return FormularKind::Twr;
    default: break;
    }

    CController me = GetPlugIn()->ControllerMyself();
    if (me.IsValid() && me.IsController())
    {
        const int facility = me.GetFacility();
        if (facility == 5)
            return FormularKind::App;
        if (facility >= 2 && facility <= 4)
            return FormularKind::Twr;
    }
    return FormularKind::Ctr;
}

bool CGalaxyATMSystemRadarScreen::OnSideButton()
{
    if (!Authorized() || CurrentFormularKind() != FormularKind::Ctr)
        return false;
    POINT cursor;
    if (!CursorRadarPoint(cursor))
        return false;
    for (const auto& label : m_formulars)
        for (const FormularItem& item : label.second.items)
            if (item.fn == &kFnCallsign && PtInRect(&item.rect, cursor))
            {
                FormularClick(label.first.c_str(), cursor, kSideButton);
                return true;
            }
    return false;
}

void CGalaxyATMSystemRadarScreen::FormularClick(const char* sCallsign, POINT pt, int button)
{
    if (GetTickCount64() - m_hdgDragEndTick < 500)
        return;

    auto it = m_formulars.find(sCallsign);
    if (it == m_formulars.end())
        return;

    if (button == BUTTON_MIDDLE)
        return;

    const bool freeTextRequested = button == kSideButton && CurrentFormularKind() == FormularKind::Ctr
        && std::any_of(it->second.items.begin(), it->second.items.end(), [&pt](const FormularItem& item)
            { return item.fn == &kFnCallsign && PtInRect(&item.rect, pt); });
    if (button == kSideButton && !freeTextRequested)
        return;

    const bool pickerWasOpen = m_cflOpen && !m_cflPicksExitLevel && m_cflCallsign == sCallsign;
    const bool xflPickerWasOpen = m_cflOpen && m_cflPicksExitLevel && m_cflCallsign == sCallsign;
    const bool speedWasOpen = m_spdOpen && m_spdCallsign == sCallsign;
    const bool headingWasOpen = m_ahdgOpen && m_ahdgCallsign == sCallsign;
    const bool rvsmWasOpen = m_rvsmOpen && m_rvsmCallsign == sCallsign;
    const bool transferWasOpen = m_xfrOpen && !m_xfrPicksRoutePoint && m_xfrCallsign == sCallsign;
    const bool copxWasOpen = m_xfrOpen && m_xfrPicksRoutePoint && m_xfrCallsign == sCallsign;
    const bool coordWasOpen = m_coordOpen && m_coordCallsign == sCallsign;
    const bool freeTextWasOpen = m_ftOpen && m_ftCallsign == sCallsign;
    if (m_coordOpen)
        CloseCoordWindow();
    if (m_cflOpen)
        CloseCflPicker();
    if (m_spdOpen)
        CloseSpeedWindow();
    if (m_ahdgOpen)
        CloseHeadingWindow();
    if (m_rvsmOpen)
        CloseRvsmWindow();
    if (m_xfrOpen)
        CloseTransferWindow();
    if (m_ftOpen)
        CloseFreeTextWindow();
    if (m_coordMenuOpen)
        CloseCoordDecisionMenu();
    if (m_csMenuOpen)
        CloseCallsignMenu();

    CFlightPlan fp = GetPlugIn()->FlightPlanSelect(sCallsign);
    if (fp.IsValid())
        GetPlugIn()->SetASELAircraft(fp);

    if (freeTextRequested)
    {
        OpenFreeTextWindow(sCallsign);
        return;
    }

    const FormularItem* hit = NULL;
    for (const FormularItem& item : it->second.items)
    {
        if (PtInRect(&item.rect, pt))
        {
            hit = &item;
            break;
        }
    }

    if (hit != NULL && hit->fn == &kFnCallsign && button == BUTTON_RIGHT)
    {
        OpenCallsignMenu(sCallsign, hit->rect);
        return;
    }
    if (hit != NULL && hit->fn == &kFnCallsign && !(fp.IsValid() && fp.GetCorrelatedRadarTarget().IsValid()))
        return;

    if (hit != NULL && fp.IsValid() && hit->fn == &kFnCfl && button == BUTTON_LEFT)
    {
        if (!pickerWasOpen)
            OpenCflPicker(sCallsign);
        RequestRefresh();
        return;
    }

    if (hit != NULL && fp.IsValid() && hit->fn == &kFnXfl && button == BUTTON_RIGHT)
    {
        if (!xflPickerWasOpen)
            OpenCflPicker(sCallsign, true);
        RequestRefresh();
        return;
    }

    if (hit != NULL && fp.IsValid() && hit->fn == &kFnCopx && button == BUTTON_RIGHT)
    {
        if (!copxWasOpen)
            OpenCopxWindow(sCallsign);
        RequestRefresh();
        return;
    }

    if (hit != NULL && fp.IsValid() && (hit->fn == &kFnAtyp || hit->fn == &kFnAppAtyp) && button == BUTTON_RIGHT)
    {
        if (!freeTextWasOpen)
            OpenFreeTextWindow(sCallsign);
        RequestRefresh();
        return;
    }

    if (hit != NULL && fp.IsValid() && IsMyCoordFn(hit->fn) && (button == BUTTON_LEFT || button == BUTTON_RIGHT))
    {
        const CoordTarget target = hit->fn == &kFnCoordExitLevel ? CoordTarget::ExitLevel
            : hit->fn == &kFnCoordEntryLevel ? CoordTarget::EntryLevel
            : hit->fn == &kFnCoordExitPoint ? CoordTarget::ExitPoint : CoordTarget::EntryPoint;
        OpenCoordDecisionMenu(sCallsign, target, hit->rect);
        return;
    }

    if (hit != NULL && fp.IsValid() && hit->fn == &kFnCoordReply)
    {
        if (button == BUTTON_LEFT && !coordWasOpen)
            OpenCoordWindow(sCallsign);
        return;
    }

    if (hit != NULL && fp.IsValid() && hit->fn == &kFnCopx && button == BUTTON_LEFT)
    {
        if (m_routeShown.erase(sCallsign) == 0)
            m_routeShown.insert(sCallsign);
        RequestRefresh();
        return;
    }

    if (hit != NULL && fp.IsValid() && (hit->fn == &kFnAfl || hit->fn == &kFnAppAfl) && button == BUTTON_LEFT)
    {
        if (!rvsmWasOpen)
            OpenRvsmWindow(sCallsign);
        RequestRefresh();
        return;
    }

    if (hit != NULL && fp.IsValid() && IsAhdgFn(hit->fn) && button == BUTTON_LEFT)
    {
        if (fp.GetControllerAssignedData().GetAssignedHeading() > 0
            && !fp.GetControllerAssignedData().SetAssignedHeading(0))
            Log::Warn("formular", std::string(sCallsign) + ": EuroScope refused to clear the heading");
        RequestRefresh();
        return;
    }

    if (hit != NULL && fp.IsValid() && IsAhdgFn(hit->fn) && button == BUTTON_RIGHT)
    {
        if (!headingWasOpen)
            OpenHeadingWindow(sCallsign);
        RequestRefresh();
        return;
    }

    if (hit != NULL && fp.IsValid() && hit->fn == &kFnAsp && button == BUTTON_LEFT
        && CurrentFormularKind() == FormularKind::Ctr)
    {
        if (!speedWasOpen)
            OpenSpeedWindow(sCallsign);
        RequestRefresh();
        return;
    }

    if (hit != NULL && fp.IsValid() && hit->fn == &kFnSector
        && button == BUTTON_LEFT && CurrentFormularKind() == FormularKind::Ctr)
    {
        if (!transferWasOpen)
            OpenTransferWindow(sCallsign);
        RequestRefresh();
        return;
    }

    if (hit != NULL && fp.IsValid() && (hit->fn == &kFnRemark || hit->fn == &kFnAppRemark)
        && button == BUTTON_RIGHT)
    {
        const bool ok = fp.GetControllerAssignedData().SetScratchPadString("");
        m_localFreeText.erase(sCallsign);
        Log::Info("formular", std::string(sCallsign) + ": free text cleared set=" + (ok ? "1" : "0"));
        RequestRefresh();
        return;
    }

    if (hit != NULL && hit->fn == &kFnAfl && button == BUTTON_RIGHT)
    {
        Plugin()->ToggleEnglish(GetPlugIn()->FlightPlanSelect(sCallsign));
        RequestRefresh();
        return;
    }

    if (hit != NULL && IsGsFn(hit->fn) && button == BUTTON_LEFT)
    {
        it->second.zone = !it->second.zone;
        RequestRefresh();
        return;
    }

    if (hit != NULL && fp.IsValid() && IsCflFn(hit->fn) && button == BUTTON_RIGHT)
    {
        int cfl = fp.GetControllerAssignedData().GetClearedAltitude();
        if (cfl == 1 || cfl == 2)
        {
            fp.GetControllerAssignedData().SetClearedAltitude(0);
            RequestRefresh();
            return;
        }
    }

    if (hit != NULL && hit->fn != NULL && fp.IsValid())
    {
        const bool right = (button == BUTTON_RIGHT);
        const FormularFn f = InSimulatorSession(GetPlugIn()) ? SimulatorFn(*hit->fn, right) : *hit->fn;
        const int id = right ? f.rightFn : f.leftFn;
        if (id != 0)
            StartTagFunction(sCallsign, f.itemPlugin, f.itemCode, hit->text.c_str(),
                right ? f.rightPlugin : f.leftPlugin, id, pt, hit->rect);
    }
    RequestRefresh();
}
