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
    return true;
}

bool CGalaxyATMSystemRadarScreen::OpenCopxDecisionMenu(CFlightPlan& fp, const RECT& area)
{
    const std::string callsign = fp.GetCallsign();
    FormularState& st = m_formulars[callsign];
    const bool entry = TrackedByOther(fp);
    const CoordWatch& w = entry ? st.entryPoint : st.exitPoint;
    const std::string& agreed = entry ? st.agreedEntryPoint : st.agreedCopx;
    const bool pending = w.lastState == COORDINATION_STATE_REQUESTED_BY_ME && w.decision == CoordDecision::None;
    const bool holds = !agreed.empty() && !PointPassed(fp, agreed.c_str());
    if (!pending && !holds)
        return false;

    m_copxMenuCallsign = callsign;
    GetPlugIn()->OpenPopupList(area, "COPX", 1);
    GetPlugIn()->AddPopupListElement("Cancel", "", FN_COPX_CANCEL);
    GetPlugIn()->AddPopupListElement("ManCoord", "", FN_COPX_MANCOORD, false, POPUP_ELEMENT_NO_CHECKBOX, !pending);
    return true;
}

void CGalaxyATMSystemRadarScreen::DecideCopx(bool manual, POINT pt, RECT area)
{
    CFlightPlan fp = GetPlugIn()->FlightPlanSelect(m_copxMenuCallsign.c_str());
    if (!fp.IsValid())
        return;
    FormularState& st = m_formulars[m_copxMenuCallsign];
    const bool entry = TrackedByOther(fp);
    CoordWatch& w = entry ? st.entryPoint : st.exitPoint;
    std::string& agreed = entry ? st.agreedEntryPoint : st.agreedCopx;
    const bool pending = w.lastState == COORDINATION_STATE_REQUESTED_BY_ME && w.decision == CoordDecision::None;

    if (manual && pending)
    {
        StartTagFunction(m_copxMenuCallsign.c_str(), NULL, 0, w.pointName.c_str(), NULL,
            TAG_ITEM_FUNCTION_ACCEPT_MANUAL_COORDINATION, pt, area);
        w.decision = CoordDecision::Manual;
        w.result = COORDINATION_STATE_MANUAL_ACCEPTED;
        w.resultAt = GetTickCount64();
        w.wasMine = true;
        agreed = w.pointName;
        Log::Info("formular", m_copxMenuCallsign + ": DCT " + w.pointName + " coordinated manually");
    }
    else if (!manual)
    {
        if (pending)
            w.decision = CoordDecision::Cancelled;
        w.result = 0;
        Log::Info("formular", m_copxMenuCallsign + ": coordination DCT "
            + (pending ? w.pointName : agreed) + " cancelled");
        agreed.clear();
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

    std::set<std::string> online;
    const char* myId = GetPlugIn()->ControllerMyself().GetPositionId();
    const std::string me = myId != NULL ? myId : "";
    if (!me.empty())
        online.insert(me);
    for (CController c = GetPlugIn()->ControllerSelectFirst(); c.IsValid(); c = GetPlugIn()->ControllerSelectNext(c))
    {
        const char* id = c.GetPositionId();
        if (c.IsController() && id != NULL && *id != '\0')
            online.insert(id);
    }

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

    const int kTitleH = 24, kPad = 12, kBtnW = 96, kBtnH = 22, kGap = 12, kCloseW = 26, kLabelGap = 6;
    const std::wstring caption = std::wstring(Tr(L"Согласование")) + L" " + Widen(m_coordCallsign.c_str());
    const int captionW = (int)Theme::MeasureText(hDC, m_fonts.WinTitle, caption).cx;
    const int requestW = (int)Theme::MeasureText(hDC, m_fonts.Body, request).cx;
    const int requestH = max(18, (int)Theme::MeasureText(hDC, m_fonts.Body, request).cy);
    const int W = max(2 * kBtnW + kGap, max(captionW + 2 * kCloseW, requestW)) + 2 * (kPad + 5);
    const int H = kTitleH + kPad + requestH + kGap + kBtnH + kPad + 5;

    const RECT ra = GetRadarArea();
    const RECT& box = label->second.area;
    int left = box.right + kLabelGap;
    if (left + W > ra.right)
        left = box.left - kLabelGap - W;
    left = max(ra.left, left);
    const int top = max(ra.top, min(box.top, ra.bottom - H));
    const RECT win = { left, top, left + W, top + H };
    m_coordArea = win;

    int saved = SaveDC(hDC);
    SetBkMode(hDC, TRANSPARENT);

    HRGN rgn = Theme::WinRegion(win);
    SelectClipRgn(hDC, rgn);
    Theme::FlatFill(hDC, win, Theme::MenuBarFill);
    RECT title = { win.left, win.top, win.right, win.top + kTitleH };
    Theme::DrawLine(hDC, title, caption, m_fonts.WinTitle, Theme::MenuText, DT_CENTER | DT_VCENTER | DT_NOPREFIX);
    RECT body = { win.left + 5, title.bottom, win.right - 5, win.bottom - 5 };
    Theme::OutlineBox(hDC, body, Theme::InsetFill, Theme::Border);
    SelectClipRgn(hDC, NULL);
    DeleteObject(rgn);
    Theme::WinBorder(hDC, win, 2, Theme::WinFrame);

    AddScreenObject(SO_COORD_WINDOW, m_coordCallsign.c_str(), win, false, "");

    RECT close = { win.right - kCloseW, title.top + 4, win.right - 8, title.bottom - 4 };
    AddHotButton(hDC, SO_COORD_CLOSE, m_coordCallsign.c_str(), close, Tr("Закрыть"));
    DrawCloseCross(hDC, close, Theme::MenuText);

    RECT textR = { body.left + kPad, title.bottom + kPad, body.right - kPad, title.bottom + kPad + requestH };
    Theme::DrawLine(hDC, textR, request, m_fonts.Body, Theme::DuplicateText, DT_CENTER | DT_VCENTER | DT_NOPREFIX);

    const int buttonsLeft = (win.left + win.right - (2 * kBtnW + kGap)) / 2;
    const RECT accept = { buttonsLeft, textR.bottom + kGap, buttonsLeft + kBtnW, textR.bottom + kGap + kBtnH };
    const RECT reject = { accept.right + kGap, accept.top, accept.right + kGap + kBtnW, accept.bottom };
    Theme::OutlineBox(hDC, accept, Theme::MenuBarFill, Theme::FormularGreen);
    Theme::OutlineBox(hDC, reject, Theme::MenuBarFill, Theme::DistressText);
    AddHotButton(hDC, SO_COORD_ACCEPT, m_coordCallsign.c_str(), accept, Tr("Принять согласование"));
    AddHotButton(hDC, SO_COORD_REJECT, m_coordCallsign.c_str(), reject, Tr("Отклонить согласование"));
    Theme::DrawLine(hDC, accept, L"Accept", m_fonts.Body, Theme::FormularGreen, DT_CENTER | DT_VCENTER);
    Theme::DrawLine(hDC, reject, L"Reject", m_fonts.Body, Theme::DistressText, DT_CENTER | DT_VCENTER);

    RestoreDC(hDC, saved);
}
