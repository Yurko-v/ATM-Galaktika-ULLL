#include "pch.h"
#include "Screen/ScreenCommon.h"

using namespace Galaxy;

void CGalaxyATMSystemRadarScreen::ReplyCoordination(const char* callsign, bool accept)
{
    CFlightPlan fp = GetPlugIn()->FlightPlanSelect(callsign);
    if (!fp.IsValid())
        return;
    if (accept)
        fp.AcceptCoordination();
    else
        fp.RefuseCoordination();
    const int reply = accept ? COORDINATION_STATE_ACCEPTED : COORDINATION_STATE_REFUSED;
    FormularState& st = m_formulars[callsign];
    for (CoordWatch* w : { &st.exitCoord, &st.entryCoord, &st.exitPoint, &st.entryPoint })
        if (w->lastState == COORDINATION_STATE_REQUESTED_BY_OTHER)
            w->myReply = reply;
    RequestRefresh();
}

bool CGalaxyATMSystemRadarScreen::SendCoordination(CFlightPlan& fp, const std::string& point, int altitudeFt,
    const std::string& partnerId)
{
    const std::string callsign = fp.GetCallsign();
    const std::string request = point.empty() ? "level " + std::to_string(altitudeFt) : "DCT " + point;
    if (!fp.GetTrackingControllerIsMe())
    {
        if (!TrackedByOther(fp))
        {
            CoordinationFailed(callsign, L"Борт никем не взят - согласовывать не с кем",
                request + " not sent: nobody tracks it");
            return false;
        }
        if (fp.GetSectorEntryMinutes() < 0)
        {
            CoordinationFailed(callsign, L"Борт не войдёт в ваш сектор - EuroScope не даёт согласовать",
                request + " not sent: it never enters my sectors");
            return false;
        }
    }
    std::string partner;
    if (!partnerId.empty() && fp.GetTrackingControllerIsMe())
    {
        CController owner = GetPlugIn()->ControllerSelectByPositionId(partnerId.c_str());
        if (owner.IsValid())
            partner = owner.GetCallsign();
    }
    if (partner.empty())
        partner = CoordPartner(GetPlugIn(), fp);
    if (partner.empty())
    {
        CoordinationFailed(callsign, L"Не найден следующий сектор для согласования",
            request + " not sent: no sector to coordinate with");
        return false;
    }
    bool sent = fp.InitiateCoordination(partner.c_str(), point.c_str(), altitudeFt);
    if (!sent && !partnerId.empty())
    {
        const std::string usual = CoordPartner(GetPlugIn(), fp);
        if (!usual.empty() && usual != partner)
        {
            Log::Info("formular", callsign + ": " + partner + " refused " + request + ", trying " + usual);
            partner = usual;
            sent = fp.InitiateCoordination(partner.c_str(), point.c_str(), altitudeFt);
        }
    }
    if (!sent)
    {
        CoordinationFailed(callsign, L"EuroScope не отправил согласование",
            request + " not sent: EuroScope refused it for " + partner);
        return false;
    }
    Log::Info("formular", callsign + ": coordination " + request + " sent to " + partner);
    FormularState& st = m_formulars[callsign];
    for (CoordWatch* w : { point.empty() ? &st.exitCoord : &st.exitPoint, point.empty() ? &st.entryCoord : &st.entryPoint })
        w->decision = CoordDecision::None;
    Plugin()->ShareCoordOutcome(fp, !point.empty(), point.empty() ? std::to_string(altitudeFt) : point, 'R');
    return true;
}

CGalaxyATMSystemRadarScreen::CoordWatch& CGalaxyATMSystemRadarScreen::WatchOf(FormularState& st, CoordTarget target)
{
    switch (target)
    {
    case CoordTarget::ExitLevel:  return st.exitCoord;
    case CoordTarget::EntryLevel: return st.entryCoord;
    case CoordTarget::ExitPoint:  return st.exitPoint;
    default:                      return st.entryPoint;
    }
}

bool CGalaxyATMSystemRadarScreen::CoordDecisionAllowed(bool manual)
{
    auto label = m_formulars.find(m_coordMenuCallsign);
    if (label == m_formulars.end())
        return false;
    FormularState& st = label->second;
    const CoordWatch& w = WatchOf(st, m_coordMenuTarget);
    const bool pending = w.lastState == COORDINATION_STATE_REQUESTED_BY_ME && w.decision == CoordDecision::None;
    if (manual)
        return pending;
    switch (m_coordMenuTarget)
    {
    case CoordTarget::ExitLevel:  return pending || st.agreedXflFt > 0;
    case CoordTarget::EntryLevel: return pending || st.agreedEntryFt > 0;
    case CoordTarget::ExitPoint:  return pending || !st.agreedCopx.empty();
    default:                      return pending || !st.agreedEntryPoint.empty();
    }
}

void CGalaxyATMSystemRadarScreen::OpenCoordDecisionMenu(const char* callsign, CoordTarget target, const RECT& anchor)
{
    m_coordMenuOpen = true;
    m_coordMenuCallsign = callsign;
    m_coordMenuTarget = target;
    m_coordMenuAnchor = anchor;
    m_coordMenuButtonsDown = true;
    RequestRefresh();
}

void CGalaxyATMSystemRadarScreen::CloseCoordDecisionMenu()
{
    m_coordMenuOpen = false;
    m_coordMenuArea = { 0, 0, 0, 0 };
    RequestRefresh();
}

void CGalaxyATMSystemRadarScreen::DecideCoordination(bool manual)
{
    const bool allowed = CoordDecisionAllowed(manual);
    CloseCoordDecisionMenu();
    CFlightPlan fp = GetPlugIn()->FlightPlanSelect(m_coordMenuCallsign.c_str());
    if (!allowed || !fp.IsValid())
        return;

    FormularState& st = m_formulars[m_coordMenuCallsign];
    CoordWatch& w = WatchOf(st, m_coordMenuTarget);
    const bool point = m_coordMenuTarget == CoordTarget::ExitPoint || m_coordMenuTarget == CoordTarget::EntryPoint;
    const bool exit = m_coordMenuTarget == CoordTarget::ExitPoint || m_coordMenuTarget == CoordTarget::ExitLevel;
    const std::string value = point ? w.pointName : std::to_string(w.levelFt);
    const std::string request = (point ? "DCT " : "level ") + value;

    if (manual)
    {
        const POINT at = { (m_coordMenuAnchor.left + m_coordMenuAnchor.right) / 2, m_coordMenuAnchor.top };
        StartTagFunction(m_coordMenuCallsign.c_str(), NULL, 0, point ? w.pointName.c_str() : "", NULL,
            TAG_ITEM_FUNCTION_ACCEPT_MANUAL_COORDINATION, at, m_coordMenuAnchor);
        w.decision = CoordDecision::Manual;
        w.result = COORDINATION_STATE_MANUAL_ACCEPTED;
        w.resultAt = GetTickCount64();
        w.wasMine = true;
        if (point)
            (exit ? st.agreedCopx : st.agreedEntryPoint) = w.pointName;
        else
            (exit ? st.agreedXflFt : st.agreedEntryFt) = w.levelFt;
        Log::Info("formular", m_coordMenuCallsign + ": " + request + " coordinated manually");
        Plugin()->ShareCoordOutcome(fp, point, value, 'M');
    }
    else
    {
        if (w.lastState == COORDINATION_STATE_REQUESTED_BY_ME && w.decision == CoordDecision::None)
            w.decision = CoordDecision::Cancelled;
        w.result = 0;
        w.resultAt = 0;
        if (point)
            (exit ? st.agreedCopx : st.agreedEntryPoint).clear();
        else
            (exit ? st.agreedXflFt : st.agreedEntryFt) = 0;
        Log::Info("formular", m_coordMenuCallsign + ": coordination " + request + " cancelled");
        Plugin()->ShareCoordOutcome(fp, point, value, 'C');
    }
    RequestRefresh();
}

bool CGalaxyATMSystemRadarScreen::PointDirectable(CFlightPlan& fp, const std::string& point, std::string* ownerId)
{
    if (ownerId != NULL)
        ownerId->clear();
    if (!fp.IsValid() || point.empty())
        return false;

    CPosition where;
    int altFt = 0;
    bool found = false;
    CFlightPlanExtractedRoute route = fp.GetExtractedRoute();
    for (int i = 0; i < route.GetPointsNumber() && !found; i++)
    {
        const char* name = route.GetPointName(i);
        if (name == NULL || _stricmp(name, point.c_str()) != 0 || route.GetPointDistanceInMinutes(i) < 0)
            continue;
        where = route.GetPointPosition(i);
        altFt = route.GetPointCalculatedProfileAltitude(i);
        found = true;
    }
    for (int type : { SECTOR_ELEMENT_FIX, SECTOR_ELEMENT_VOR, SECTOR_ELEMENT_NDB, SECTOR_ELEMENT_AIRPORT })
    {
        for (CSectorElement e = GetPlugIn()->SectorFileElementSelectFirst(type); e.IsValid() && !found;
             e = GetPlugIn()->SectorFileElementSelectNext(e, type))
        {
            const char* name = e.GetName();
            if (name != NULL && _stricmp(name, point.c_str()) == 0 && e.GetPosition(&where, 0))
                found = true;
        }
    }
    if (!found)
        return false;

    if (altFt <= 0)
    {
        const int cfl = fp.GetControllerAssignedData().GetClearedAltitude();
        CRadarTarget rt = fp.GetCorrelatedRadarTarget();
        if (cfl > 2)
            altFt = cfl;
        else if (rt.IsValid())
        {
            CRadarTargetPositionData pos = rt.GetPosition();
            altFt = pos.GetFlightLevel() / 100 >= Plugin()->TransitionLevelFL()
                ? pos.GetFlightLevel() : pos.GetPressureAltitude();
        }
        else
            altFt = fp.GetFinalAltitude();
    }

    const char* myId = GetPlugIn()->ControllerMyself().GetPositionId();
    const std::string me = myId != NULL ? myId : "";
    const std::set<std::string> online = OnlinePositions(GetPlugIn());

    const PointVerdict verdict = ClassifyPoint(where, altFt, online, me);
    if (ownerId != NULL && verdict.zone == PointZone::Other)
        *ownerId = verdict.ownerId;
    const bool mine = verdict.zone == PointZone::Mine || verdict.zone == PointZone::Junction
        || verdict.zone == PointZone::Unowned;
    return mine && fp.GetTrackingControllerIsMe();
}

void CGalaxyATMSystemRadarScreen::CoordinationFailed(const std::string& callsign, const wchar_t* reason,
    const std::string& detail)
{
    Log::Warn("formular", callsign + ": " + detail);
    GetPlugIn()->DisplayUserMessage("Galaxy ATM System", Narrow(Tr(L"Согласование")).c_str(),
        (callsign + ": " + Narrow(Tr(reason))).c_str(), true, true, true, false, false);
}

std::wstring CGalaxyATMSystemRadarScreen::PendingCoordRequest(const std::string& callsign) const
{
    std::wstring request;
    auto label = m_formulars.find(callsign);
    if (label == m_formulars.end())
        return request;
    for (const FormularItem& item : label->second.items)
    {
        if (item.fn != &kFnCoordReply)
            continue;
        if (!request.empty())
            request += L' ';
        request += Widen(item.text.c_str());
    }
    return request;
}

void CGalaxyATMSystemRadarScreen::OpenCoordWindow(const char* callsign)
{
    m_coordOpen = true;
    m_coordPlacement = PopupPlacement();
    m_coordCallsign = callsign;
    m_coordButtonsDown = true;
    RequestRefresh();
}

void CGalaxyATMSystemRadarScreen::CloseCoordWindow()
{
    m_coordOpen = false;
    m_coordArea = { 0, 0, 0, 0 };
    RequestRefresh();
}

void CGalaxyATMSystemRadarScreen::TickCoordWindow()
{
    if (!m_coordOpen)
        return;

    auto label = m_formulars.find(m_coordCallsign);
    if (label == m_formulars.end() || PendingCoordRequest(m_coordCallsign).empty())
    {
        CloseCoordWindow();
        return;
    }

    POINT cursor;
    const bool onRadar = CursorRadarPoint(cursor);
    TrackPopupActive(m_coordPlacement, m_coordArea, onRadar, cursor);
    const bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0
        || (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    if (down && !m_coordButtonsDown && onRadar
        && !PtInRect(&m_coordArea, cursor) && !PtInRect(&label->second.area, cursor))
    {
        m_coordButtonsDown = down;
        CloseCoordWindow();
        return;
    }
    m_coordButtonsDown = down;
}

void CGalaxyATMSystemRadarScreen::DrawCoordWindow(HDC hDC)
{
    auto label = m_formulars.find(m_coordCallsign);
    const std::wstring request = PendingCoordRequest(m_coordCallsign);
    if (label == m_formulars.end() || request.empty())
        return;

    int saved = SaveDC(hDC);
    SetBkMode(hDC, TRANSPARENT);
    SelectObject(hDC, GetSpeedFont());
    TEXTMETRICW tm;
    GetTextMetricsW(hDC, &tm);
    const int lineH = max(1, (int)(tm.tmHeight + tm.tmExternalLeading));

    const int rowH = lineH * 7 / 5;
    const int border = max(3, rowH / 9);
    const int line = max(1, rowH / 25);
    const int titleH = rowH * 9 / 8;
    const int gap = rowH / 3;
    const int pad = rowH * 2 / 5;
    const int accentH = max(2, rowH / 8);

    const std::wstring acceptText = Tr(L"Принять"), rejectText = Tr(L"Отклонить");
    const wchar_t* caption = Tr(L"Согласование");
    auto measure = [&](HFONT font, const std::wstring& s)
    {
        SelectObject(hDC, font);
        SIZE sz = { 0, 0 };
        GetTextExtentPoint32W(hDC, s.c_str(), (int)s.size(), &sz);
        return (int)sz.cx;
    };
    const int btnW = max(measure(GetSpeedFont(), acceptText), measure(GetSpeedFont(), rejectText)) + rowH;
    const int fieldW = measure(GetSpeedFont(), request) + rowH;
    const int titleW = measure(GetTitleFont(), caption) + titleH + border;
    const int inner = max(max(2 * btnW + gap, fieldW), max(titleW - 2 * border, rowH * 5));
    const int width = inner + 2 * (border + pad);
    const int height = titleH + gap + rowH + gap / 2 + rowH + gap + rowH + pad + border;

    const RECT& box = label->second.area;
    const RECT ra = GetRadarArea();
    int left = box.right + 1;
    if (left + width > ra.right)
        left = box.left - 1 - width;
    const RECT area = PlacePopup(m_coordPlacement, left, max(ra.top, min(box.top, ra.bottom - height)), width, height);
    m_coordArea = area;
    AddScreenObject(SO_COORD_WINDOW, m_coordCallsign.c_str(), area, true, "");

    auto fill = [&](const RECT& r, COLORREF color)
    {
        HBRUSH b = CreateSolidBrush(color);
        FillRect(hDC, &r, b);
        DeleteObject(b);
    };
    auto frame = [&](RECT r, COLORREF color, int thick)
    {
        HBRUSH b = CreateSolidBrush(color);
        for (int i = 0; i < thick; i++)
        {
            FrameRect(hDC, &r, b);
            InflateRect(&r, -1, -1);
        }
        DeleteObject(b);
    };
    auto text = [&](RECT r, const wchar_t* s, COLORREF color, UINT align)
    {
        SetTextColor(hDC, color);
        DrawTextW(hDC, s, -1, &r, align | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    };
    auto gradient = [&](const RECT& r, COLORREF topColor, COLORREF bottomColor)
    {
        TRIVERTEX v[2] = {
            { r.left, r.top, (COLOR16)(GetRValue(topColor) << 8), (COLOR16)(GetGValue(topColor) << 8),
              (COLOR16)(GetBValue(topColor) << 8), 0 },
            { r.right, r.bottom, (COLOR16)(GetRValue(bottomColor) << 8), (COLOR16)(GetGValue(bottomColor) << 8),
              (COLOR16)(GetBValue(bottomColor) << 8), 0 } };
        GRADIENT_RECT g = { 0, 1 };
        GradientFill(hDC, v, 2, &g, 1, GRADIENT_FILL_RECT_V);
    };

    fill(area, PopupFrame(m_coordPlacement));
    const RECT titleBar = { area.left, area.top, area.right, area.top + titleH };
    gradient(titleBar, m_coordPlacement.active ? Theme::XfrTitleTop : Theme::XfrTitleTopInactive,
        PopupFrame(m_coordPlacement));
    frame(area, Theme::XfrOutline, 1);
    const RECT body = { area.left + border, area.top + titleH, area.right - border, area.bottom - border };
    fill(body, Theme::SpdBody);
    RECT bodyEdge = body;
    InflateRect(&bodyEdge, 1, 1);
    frame(bodyEdge, Theme::XfrOutline, 1);

    SelectObject(hDC, GetTitleFont());
    const RECT title = { area.left + border * 2, area.top, area.right - titleH, area.top + titleH };
    text(title, caption, Theme::Text, DT_LEFT);
    const RECT close = { area.right - titleH, area.top, area.right - border, area.top + titleH };
    text(close, L"\x00D7", Theme::Text, DT_CENTER);
    AddScreenObject(SO_COORD_CLOSE, m_coordCallsign.c_str(), close, false, Tr("Закрыть"));

    SelectObject(hDC, GetSpeedFont());
    const int x0 = body.left + pad, x1 = body.right - pad;
    int y = body.top + gap;
    const RECT cs = { x0, y, x1, y + rowH };
    text(cs, Widen(m_coordCallsign.c_str()).c_str(), Theme::Text, DT_CENTER);
    y += rowH + gap / 2;

    const RECT field = { x0, y, x1, y + rowH };
    fill(field, RGB(0x10, 0x10, 0x10));
    frame(field, Theme::SpdLine, line);
    text(field, request.c_str(), Theme::DuplicateText, DT_CENTER);
    y += rowH + gap;

    const int mid = (x0 + x1) / 2;
    const RECT buttons[2] = { { x0, y, mid - gap / 2, y + rowH }, { mid + gap / 2, y, x1, y + rowH } };
    const wchar_t* labels[2] = { acceptText.c_str(), rejectText.c_str() };
    const COLORREF accents[2] = { Theme::FormularGreen, Theme::DistressText };
    const int types[2] = { SO_COORD_ACCEPT, SO_COORD_REJECT };
    const char* tips[2] = { Tr("Принять согласование"), Tr("Отклонить согласование") };
    for (int i = 0; i < 2; i++)
    {
        const RECT& btn = buttons[i];
        if (Hot(btn))
            gradient(btn, Theme::XfrButtonHotTop, Theme::XfrButtonHotBottom);
        else
            gradient(btn, Theme::XfrButtonTop, Theme::XfrButtonBottom);
        const RECT accent = { btn.left + line, btn.bottom - line - accentH, btn.right - line, btn.bottom - line };
        fill(accent, accents[i]);
        frame(btn, Theme::SpdLine, line);
        const RECT face = { btn.left, btn.top, btn.right, btn.bottom - accentH };
        text(face, labels[i], Theme::Text, DT_CENTER);
        AddScreenObject(types[i], m_coordCallsign.c_str(), btn, false, tips[i]);
    }

    RestoreDC(hDC, saved);
}
