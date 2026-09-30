#include "pch.h"
#include "Screen/ScreenCommon.h"

using namespace Galaxy;

void CGalaxyATMSystemRadarScreen::OnRefresh(HDC hDC, int Phase)
{
    if (Phase != REFRESH_PHASE_BEFORE_TAGS &&
        Phase != REFRESH_PHASE_AFTER_TAGS &&
        Phase != REFRESH_PHASE_AFTER_LISTS)
        return;

    if (m_perfOn && Phase == REFRESH_PHASE_BEFORE_TAGS)
    {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        if (m_perfLastFrame != 0)
        {
            const LONGLONG gap = now.QuadPart - m_perfLastFrame;
            m_perfGap.sum += gap;
            m_perfGap.worst = max(m_perfGap.worst, gap);
            m_perfGap.calls++;
        }
        m_perfLastFrame = now.QuadPart;
    }

    const PerfSection section = Phase == REFRESH_PHASE_BEFORE_TAGS ? PerfSection::BeforeTags
        : Phase == REFRESH_PHASE_AFTER_TAGS ? PerfSection::AfterTags : PerfSection::AfterLists;
    Timed(section, [&] { RefreshPhase(hDC, Phase); });

    if (m_perfOn && Phase == REFRESH_PHASE_AFTER_LISTS)
        PerfReport();
}

void CGalaxyATMSystemRadarScreen::PerfReset()
{
    for (PerfTotals& t : m_perf)
        t = PerfTotals();
    m_perfGap = PerfTotals();
    m_perfLastFrame = 0;
    m_perfSince = GetTickCount64();
}

void CGalaxyATMSystemRadarScreen::PerfReport()
{
    const ULONGLONG kReportMs = 10000;
    const ULONGLONG now = GetTickCount64();
    if (now - m_perfSince < kReportMs)
        return;

    static const char* const kNames[] = {
        "BeforeTags", "AfterTags", "AfterLists",
        "Zones", "Sigmets", "WakeArcs", "TargetVectors", "TargetSymbols", "Formulars", "CoordWindow",
        "Panel", "Windows" };
    static_assert(_countof(kNames) == (size_t)PerfSection::Count, "perf section names");

    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    const double msPerTick = 1000.0 / (double)freq.QuadPart;
    const double seconds = (now - m_perfSince) / 1000.0;

    int targets = 0;
    for (CRadarTarget rt = GetPlugIn()->RadarTargetSelectFirst(); rt.IsValid();
         rt = GetPlugIn()->RadarTargetSelectNext(rt))
        targets++;

    char head[256];
    sprintf_s(head, "%.1f frames/s, %d targets, %d symbols drawn, %d formulars known; frame gap avg %.1f max %.1f ms",
        m_perf[(int)PerfSection::AfterTags].calls / seconds, targets, m_symbolStats.drawn, (int)m_formulars.size(),
        m_perfGap.calls > 0 ? m_perfGap.sum * msPerTick / m_perfGap.calls : 0.0, m_perfGap.worst * msPerTick);
    std::string line = head;
    for (int i = 0; i < (int)PerfSection::Count; i++)
    {
        const PerfTotals& t = m_perf[i];
        if (t.calls == 0)
            continue;
        char part[96];
        sprintf_s(part, "; %s avg %.2f max %.2f ms", kNames[i], t.sum * msPerTick / t.calls, t.worst * msPerTick);
        line += part;
    }
    Log::Info("perf", line);
    PerfReset();
}

void CGalaxyATMSystemRadarScreen::RefreshPhase(HDC hDC, int Phase)
{
    if (!m_visible)
    {
        m_rcEntry.Close();
        m_rcFloat.Hide();
        return;
    }

    m_fonts.EnsureCreated();
    UpdateWheelHook();

    SyncAuth();

    if (!Authorized())
    {
        m_rulerPressPending = false;
        m_rulerArmed = false;
        m_rulerPlacing = false;
        m_zoneInfoIndex = -1;
        m_sigmetInfoIndex = -1;
        m_hotRects.clear();
    }

    if (Phase != REFRESH_PHASE_AFTER_LISTS && !Authorized())
        return;

    UpdateZoneActivity();

    m_sigmets = Plugin()->Sigmets();

    if (Phase == REFRESH_PHASE_BEFORE_TAGS)
    {
        Timed(PerfSection::Zones, [&] { DrawZones(hDC); });
        Timed(PerfSection::Sigmets, [&] { DrawSigmets(hDC); });
        return;
    }

    HFONT dcFont = (HFONT)GetCurrentObject(hDC, OBJ_FONT);
    m_esFont = (dcFont != NULL
                && dcFont != (HFONT)GetStockObject(SYSTEM_FONT)
                && dcFont != (HFONT)GetStockObject(DEVICE_DEFAULT_FONT)
                && dcFont != (HFONT)GetStockObject(DEFAULT_GUI_FONT))
        ? dcFont : m_fonts.Mono;

    if (Phase == REFRESH_PHASE_AFTER_TAGS)
    {
        m_hotRects.clear();
        m_areaShiftDown = ShiftHeldInEuroScope();
        if (m_areaShiftDown || m_zoneInfoIndex >= 0)
            RegisterZoneObjects();
        RegisterSigmetObjects();

        if (m_rulerPressPending)
        {
            m_rulerPressPending = false;
            if (m_rulerPlacing)
            {
                m_rulerPlacing = false;
                m_rulerArmed = false;
            }
            else
            {
                m_rulerArmed = !m_rulerArmed;
            }
        }
        if (m_rulerPlacing)
        {
            POINT cursor;
            if (CursorRadarPoint(cursor))
                UpdateRulerEnd(cursor);
        }

        for (size_t i = 0; i < m_rulers.size(); i++)
        {
            RulerLine& r = m_rulers[i];
            POINT a = ConvertCoordFromPositionToPixel(ResolveRulerPoint(r.startSnapped, r.startCallsign, r.startFixed));
            POINT b = ConvertCoordFromPositionToPixel(ResolveRulerPoint(r.endSnapped, r.endCallsign, r.endFixed));

            double lineLen = sqrt((double)(b.x - a.x) * (b.x - a.x) + (double)(b.y - a.y) * (b.y - a.y));
            const int kPad = 15;
            const double kStepPx = 20.0;
            const double kEndClearPx = kPad + 20.0;

            char id[16];
            sprintf_s(id, "%zu", i);

            if (lineLen <= 2.0 * kEndClearPx)
            {
                int px = (a.x + b.x) / 2, py = (a.y + b.y) / 2;
                RECT box = { px - kPad, py - kPad, px + kPad, py + kPad };
                AddScreenObject(SO_RULER_LINE, id, box, false, Tr("ПКМ/2ЛКМ - удалить линейку"));
                continue;
            }

            double tMin = kEndClearPx / lineLen, tMax = 1.0 - tMin;
            int steps = max(1, (int)lround((lineLen - 2.0 * kEndClearPx) / kStepPx));
            for (int s = 0; s <= steps; s++)
            {
                double t = tMin + (tMax - tMin) * s / steps;
                int px = a.x + (int)lround((b.x - a.x) * t);
                int py = a.y + (int)lround((b.y - a.y) * t);
                RECT box = { px - kPad, py - kPad, px + kPad, py + kPad };
                AddScreenObject(SO_RULER_LINE, id, box, false, Tr("ПКМ/2ЛКМ - удалить линейку"));
            }
        }

        if (m_rulerArmed || m_rulerPlacing)
        {
            AddScreenObject(SO_RULER_CANVAS, "RULER_CANVAS", GetRadarArea(), false,
                m_rulerPlacing ? Tr("ЛКМ - конец линейки, ПКМ - отмена")
                               : Tr("ЛКМ - начало линейки, ПКМ - отмена"));
        }
        else if (m_mapTool != MapTool::None)
        {
            AddScreenObject(SO_MAP_CANVAS, "MAP_CANVAS", GetRadarArea(), false,
                m_mapTool == MapTool::Line ? Tr("ЛКМ - точка линии, ПКМ - закончить")
                                           : Tr("Колесо - радиус, ЛКМ/ПКМ - готово"));
        }
        const bool sketching = m_rulerArmed || m_rulerPlacing || m_mapTool != MapTool::None;

        {
            Theme::AntiAliased smoothVectors;
            Timed(PerfSection::WakeArcs, [&] { DrawWakeArcs(hDC); });
            if (!m_routeShown.empty())
                DrawRoutes(hDC);
            DrawMapSketches(hDC);

            if (m_vecDistEnabled || m_vecTimeEnabled || m_vecByPlan)
                Timed(PerfSection::TargetVectors, [&] { DrawTargetVectors(hDC); });
        }

        Timed(PerfSection::TargetSymbols, [&] { DrawTargetSymbols(hDC); });
        {
            Theme::AntiAliased smoothFormulars;
            Timed(PerfSection::Formulars, [&] { DrawFormulars(hDC, !sketching); });
        }
        if (m_coordOpen)
            Timed(PerfSection::CoordWindow, [&] { DrawCoordWindow(hDC); });
        if (m_cflOpen && !m_cflInList)
            DrawCflPicker(hDC, GetRadarArea());
        if (m_spdOpen)
            DrawSpeedWindow(hDC);
        if (m_ahdgOpen)
            DrawHeadingWindow(hDC);
        if (m_rvsmOpen)
            DrawRvsmWindow(hDC);
        if (m_xfrOpen)
            DrawTransferWindow(hDC);
        if (m_ftOpen)
            DrawFreeTextWindow(hDC);

        Theme::AntiAliased smoothRulers;
        for (size_t i = 0; i < m_rulers.size(); i++)
            DrawRulerLine(hDC, m_rulers[i], (int)i);
        if (m_rulerPlacing)
            DrawRulerLine(hDC, m_rulerPending);
        else if (m_rulerArmed)
            DrawRulerCursor(hDC);

        return;
    }

    if (m_authState != AuthState::LoggedOut && !Plugin()->TrainingSession() && Plugin()->AccessSuspended())
    {
        Plugin()->SetSessionAuthorized(false);
        m_authState = AuthState::LoggedOut;
        m_openDropdown = DropdownKind::None;
        m_rulerArmed = false;
        m_rulerPlacing = false;
        CloseLoginWindow();
        m_authMessage = Tr(L"Доступ приостановлен");
        ShowNotice(m_authMessage);
        Log::Warn("auth", "panel closed: access suspended - the name was removed from the user base");
    }

    Timed(PerfSection::Panel, [&] { DrawPanelAndMenu(hDC); });

    if (m_loginWindowOpen && !Authorized())
    {
        if (Plugin()->MyLogin() == CGalaxyATMSystemPlugin::LoginState::Done)
        {
            CGalaxyATMSystemPlugin::SavedLogin id;
            id.cid = m_loginValues[LF_CID];
            id.surname = m_loginValues[LF_SURNAME];
            Plugin()->SaveIdentity(id);

            CloseLoginWindow();
            GrantAccess();
        }
        else
        {
            DrawLoginWindow(hDC);
        }
    }
    else if (m_loginWindowOpen)
    {
        CloseLoginWindow();
    }

    if (Authorized())
    {
        Timed(PerfSection::Windows, [&]
        {
            if (m_atisLetterOpen)
                DrawAtisLetterWindow(hDC);
            if (m_atisOpen)
                DrawAtisWindow(hDC);
            if (m_rcOpen && m_rcFloating)
                RenderRcFloat();
            else if (m_rcOpen)
                DrawSectorListWindow(hDC);

            if (m_openDropdown != DropdownKind::None)
                DrawDropdownList(hDC);
            if (m_mapMenuOpen)
                DrawMapMenu(hDC);
            if (m_coordMenuOpen)
                DrawCoordDecisionMenu(hDC);
            if (m_csMenuOpen)
                DrawCallsignMenu(hDC);
        });
    }

    if (!(m_rcOpen && m_rcFloating && Authorized()))
    {
        m_rcEntry.Close();
        m_rcFloat.Hide();
    }

    if (!m_noticeText.empty())
        DrawNoticeWindow(hDC);

    if (m_zoneInfoIndex >= 0)
        DrawZoneInfo(hDC);
    if (m_sigmetInfoIndex >= 0)
        DrawSigmetInfo(hDC);
}
