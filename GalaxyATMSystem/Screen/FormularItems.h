#pragma once

#include "Core/Common.h"

namespace Galaxy
{
    inline std::string WithSpeedModifier(const char* annotation, char modifier)
    {
        std::vector<std::string> fields;
        const std::string text = annotation != NULL ? annotation : "";
        size_t pos = 0;
        while (pos <= text.size())
        {
            size_t end = text.find('/', pos);
            std::string field = text.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
            if (!field.empty() && !(field.size() >= 2 && field[0] == 's' && (field[1] == '+' || field[1] == '-')))
                fields.push_back(field);
            if (end == std::string::npos)
                break;
            pos = end + 1;
        }
        if (modifier != 0)
            fields.push_back(std::string("s") + modifier);
        std::string out;
        for (const std::string& f : fields)
            out += f + "/";
        return out;
    }

    struct FormularRun
    {
        std::wstring text;
        COLORREF color;
        const FormularFn* fn;
        COLORREF back = CLR_INVALID;
    };

    inline const char* const kTopSky = "TopSky plugin";
    inline const char* const kUlll   = "ULLLPlugin";
    inline const char* const kVch    = "VCH";

    inline const char* const kGalaxy = "Galaxy ATM System";

    inline const FormularFn kFnSquawkWarning = { kGalaxy, TAG_ITEM_SQUAWK, kGalaxy, TAG_FUNC_SQUAWK_MENU, NULL, 0 };
    inline const FormularFn kFnCommunication = { kTopSky, 201, NULL,    32,  NULL,    0    };
    inline const FormularFn kFnRemark        = { kTopSky, 212, kTopSky, 2,   NULL,    0    };
    inline const FormularFn kFnCallsign      = { kTopSky, 22,  kTopSky, 6,   kTopSky, 6    };
    inline const FormularFn kFnSector        = { kTopSky, 66,  NULL,    20,  kTopSky, 100  };
    inline const FormularFn kFnTssr          = { kGalaxy, TAG_ITEM_SQUAWK, kGalaxy, TAG_FUNC_SQUAWK_MENU, NULL, 0 };
    inline const FormularFn kFnAfl           = { kUlll,   509, NULL,    1,   kTopSky, 99   };
    inline const FormularFn kFnCfl           = { kUlll,   510, kTopSky, 12,  kTopSky, 139  };
    inline const FormularFn kFnGs            = { kTopSky, 40,  kUlll,   507, NULL,    0    };
    inline const FormularFn kFnXfl           = { kTopSky, 53,  NULL,    26,  NULL,    0    };
    inline const FormularFn kFnCopx          = { kTopSky, 44,  NULL,    0,   kTopSky, 45   };
    inline const FormularFn kFnAhdg          = { NULL,    25,  NULL,    0,   kTopSky, 14   };
    inline const FormularFn kFnAsp           = { kTopSky, 47,  kTopSky, 15,  NULL,    TAG_ITEM_FUNCTION_ASSIGNED_SPEED_POPUP };
    inline const FormularFn kFnArc           = { kTopSky, 56,  kTopSky, 16,  kTopSky, 134  };
    inline const FormularFn kFnAtyp          = { kTopSky, 70,  kVch,    650, kTopSky, 2    };
    inline const FormularFn kFnAdes          = { kTopSky, 79,  NULL,    7,   kTopSky, 1001 };
    inline const FormularFn kFnRfl           = { kTopSky, 120, kTopSky, 59,  NULL,    0    };
    inline const FormularFn kFnFlightRule    = { NULL,    TAG_ITEM_TYPE_FLIGHT_RULE, NULL, TAG_ITEM_FUNCTION_OPEN_FP_DIALOG, NULL, 0 };

    inline const FormularFn kFnAppSquawkWarning = { kGalaxy, TAG_ITEM_SQUAWK, kGalaxy, TAG_FUNC_SQUAWK_MENU, kGalaxy, TAG_FUNC_SQUAWK_MENU };
    inline const FormularFn kFnAppRemark        = { kTopSky, 212,   kTopSky, 2,   kTopSky, 2    };
    inline const FormularFn kFnAppAfl           = { kUlll,   509,   NULL,    1,   kTopSky, 143  };
    inline const FormularFn kFnAppCfl           = { kUlll,   510,   kTopSky, 12,  kTopSky, 157  };
    inline const FormularFn kFnAppAtyp          = { kTopSky, 69,    kVch,    650, kTopSky, 2    };
    inline const FormularFn kFnArwy             = { kTopSky, 261,   NULL,    19,  NULL,    0    };
    inline const FormularFn kFnAppAhdg          = { NULL,    25,    NULL,    0,   kTopSky, 14   };

    inline const FormularFn kFnTwrSector        = { kTopSky, 10014, NULL,    20,  kTopSky, 100  };
    inline const FormularFn kFnTwrGs            = { kTopSky, 40,    NULL,    0,   NULL,    0    };

    inline bool IsAhdgFn(const FormularFn* fn) { return fn == &kFnAhdg || fn == &kFnAppAhdg; }
    inline bool IsCflFn(const FormularFn* fn)  { return fn == &kFnCfl || fn == &kFnAppCfl; }
    inline bool IsGsFn(const FormularFn* fn)   { return fn == &kFnGs || fn == &kFnTwrGs; }

    const int kSideButton = BUTTON_RIGHT + 1;

    inline const FormularFn kFnCoordReply    = { NULL,    0,   NULL,    0,   NULL,    0    };
    inline const FormularFn kFnModeSTab      = { NULL,    0,   NULL,    0,   NULL,    0    };
    inline const FormularFn kFnCoordExitLevel  = { NULL,  0,   NULL,    0,   NULL,    0    };
    inline const FormularFn kFnCoordEntryLevel = { NULL,  0,   NULL,    0,   NULL,    0    };
    inline const FormularFn kFnCoordExitPoint  = { NULL,  0,   NULL,    0,   NULL,    0    };
    inline const FormularFn kFnCoordEntryPoint = { NULL,  0,   NULL,    0,   NULL,    0    };

    inline bool IsMyCoordFn(const FormularFn* fn)
    {
        return fn == &kFnCoordExitLevel || fn == &kFnCoordEntryLevel
            || fn == &kFnCoordExitPoint || fn == &kFnCoordEntryPoint;
    }

    inline const double kProtectionZoneKm = 10.0;

    inline const FormularFn kFnSimulation = { NULL, TAG_ITEM_TYPE_SIMULATION_INDICATOR,
        NULL, TAG_ITEM_FUNCTION_SIMULATION_POPUP, NULL, TAG_ITEM_FUNCTION_SIMULATION_POPUP };

    inline bool InSimulatorSession(CPlugIn* plugin)
    {
        const int connection = plugin->GetConnectionType();
        return connection != CONNECTION_TYPE_DIRECT
            && connection != CONNECTION_TYPE_VIA_PROXY;
    }

    inline FormularFn SimulatorFn(const FormularFn& fn, bool right)
    {
        int item = 0, function = 0;
        if (right && IsCflFn(&fn))
        {
            item = TAG_ITEM_TYPE_TEMP_ALTITUDE;
            function = TAG_ITEM_FUNCTION_TEMP_ALTITUDE_POPUP;
        }
        else if (!right && &fn == &kFnArc)
        {
            item = TAG_ITEM_TYPE_ASSIGNED_RATE;
            function = TAG_ITEM_FUNCTION_ASSIGNED_RATE_POPUP;
        }
        else
        {
            return fn;
        }

        FormularFn sim = fn;
        sim.itemPlugin = NULL;
        sim.itemCode = item;
        if (right)
        {
            sim.rightPlugin = NULL;
            sim.rightFn = function;
        }
        else
        {
            sim.leftPlugin = NULL;
            sim.leftFn = function;
        }
        return sim;
    }

    inline const char* const kFormularKindNames[] = { "auto", "ctr", "app", "twr" };

    inline void DrawTrendArrow(Gdiplus::Graphics& g, const RECT& slot, bool up, COLORREF ink)
    {
        const Gdiplus::REAL h = (Gdiplus::REAL)(slot.bottom - slot.top);
        const Gdiplus::REAL cx = (slot.left + slot.right) / 2.0f;
        const Gdiplus::REAL top = slot.top + h * 0.16f, bottom = slot.top + h * 0.88f;
        const Gdiplus::REAL headH = h * 0.36f;
        const Gdiplus::REAL halfW = min((slot.right - slot.left) * 0.48f, h * 0.24f);
        const Gdiplus::REAL tip = up ? top : bottom;
        const Gdiplus::REAL wing = up ? top + headH : bottom - headH;
        const Gdiplus::REAL notch = up ? top + headH * 0.72f : bottom - headH * 0.72f;
        const Gdiplus::REAL tail = up ? bottom : top;

        Gdiplus::Pen shaft(Theme::GdiColor(ink), max(1.5f, h / 9.0f));
        g.DrawLine(&shaft, cx, tail, cx, notch);
        const Gdiplus::PointF head[] = {
            Gdiplus::PointF(cx, tip),
            Gdiplus::PointF(cx + halfW, wing),
            Gdiplus::PointF(cx, notch),
            Gdiplus::PointF(cx - halfW, wing),
        };
        Gdiplus::SolidBrush brush(Theme::GdiColor(ink));
        g.FillPolygon(&brush, head, 4);
    }

    inline void DrawTick(Gdiplus::Graphics& g, const RECT& slot, COLORREF ink)
    {
        const Gdiplus::REAL w = (Gdiplus::REAL)(slot.right - slot.left);
        const Gdiplus::REAL h = (Gdiplus::REAL)(slot.bottom - slot.top);
        const Gdiplus::REAL top = slot.top + h * 0.3f, bottom = slot.top + h * 0.8f;
        const Gdiplus::PointF tick[] = {
            Gdiplus::PointF(slot.left + w * 0.1f, (top + bottom) / 2.0f + h * 0.05f),
            Gdiplus::PointF(slot.left + w * 0.4f, bottom),
            Gdiplus::PointF(slot.right - w * 0.1f, top),
        };

        Gdiplus::Pen pen(Theme::GdiColor(ink), max(2.0f, h / 7.0f));
        pen.SetLineJoin(Gdiplus::LineJoinRound);
        pen.SetStartCap(Gdiplus::LineCapRound);
        pen.SetEndCap(Gdiplus::LineCapRound);
        const Gdiplus::SmoothingMode mode = g.GetSmoothingMode();
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.DrawLines(&pen, tick, 3);
        g.SetSmoothingMode(mode);
    }

    const wchar_t kHandoffArrow = L'\x2192';

    inline void DrawHandoffArrow(Gdiplus::Graphics& g, const RECT& slot, COLORREF ink)
    {
        const Gdiplus::REAL h = (Gdiplus::REAL)(slot.bottom - slot.top);
        const Gdiplus::REAL cy = slot.top + h * 0.55f;
        const Gdiplus::REAL left = slot.left + h * 0.08f, right = slot.right - h * 0.08f;
        const Gdiplus::REAL headW = min((right - left) * 0.5f, h * 0.34f);
        const Gdiplus::REAL halfH = h * 0.2f;

        Gdiplus::Pen shaft(Theme::GdiColor(ink), max(1.5f, h / 9.0f));
        g.DrawLine(&shaft, left, cy, right - headW * 0.72f, cy);
        const Gdiplus::PointF head[] = {
            Gdiplus::PointF(right, cy),
            Gdiplus::PointF(right - headW, cy - halfH),
            Gdiplus::PointF(right - headW * 0.72f, cy),
            Gdiplus::PointF(right - headW, cy + halfH),
        };
        Gdiplus::SolidBrush brush(Theme::GdiColor(ink));
        g.FillPolygon(&brush, head, 4);
    }

    inline char TopSkySpeedModifier(const CFlightPlanControllerAssignedData& assigned)
    {
        const char* annotation = assigned.GetFlightStripAnnotation(kTopSkySpeedAnnotation);
        if (annotation == NULL)
            return 0;

        const std::string text(annotation);
        size_t pos = 0;
        while (pos < text.size())
        {
            size_t end = text.find('/', pos);
            const std::string field = text.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
            if (field.size() >= 2 && field[0] == 's' && (field[1] == '+' || field[1] == '-'))
                return field[1];
            if (end == std::string::npos)
                break;
            pos = end + 1;
        }
        return 0;
    }

    inline char SpeedModifierOf(CGalaxyATMSystemPlugin* plugin, CFlightPlan& fp)
    {
        char shared = 0;
        if (plugin != NULL && plugin->SharedSpeedModifier(fp, shared))
            return shared;
        return TopSkySpeedModifier(fp.GetControllerAssignedData());
    }

    inline bool CalculatedIasMach(int gsKt, int pressureAltFt, int& iasKt, int& machX100)
    {
        if (gsKt < 40)
            return false;

        const double h = (double)max(0, pressureAltFt);
        double T, delta;
        if (h <= 36089.0)
        {
            T = 288.15 - 0.0019812 * h;
            delta = pow(T / 288.15, 5.25588);
        }
        else
        {
            T = 216.65;
            delta = 0.223361 * exp(-(h - 36089.0) / 20805.8);
        }

        const double mach = gsKt / (38.967854 * sqrt(T));
        const double qcOverP0 = delta * (pow(1.0 + 0.2 * mach * mach, 3.5) - 1.0);
        const double cas = 661.4786 * sqrt(5.0 * (pow(qcOverP0 + 1.0, 2.0 / 7.0) - 1.0));

        iasKt = (int)lround(cas);
        machX100 = (int)lround(mach * 100.0);
        return true;
    }
}
