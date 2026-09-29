#pragma once

#include "Core/Base.h"

namespace Galaxy
{
    inline bool IsDistressSquawk(const char* squawk)
    {
        return strcmp(squawk, "7500") == 0
            || strcmp(squawk, "7600") == 0
            || strcmp(squawk, "7700") == 0;
    }

    inline bool IsConspicuitySquawk(const char* squawk)
    {
        return strcmp(squawk, "0000") == 0
            || strcmp(squawk, "1200") == 0
            || strcmp(squawk, "2000") == 0
            || strcmp(squawk, "7000") == 0;
    }

    inline std::string FormatLevelFeet(int altFt)
    {
        char buf[16];
        sprintf_s(buf, "F%03d", altFt / 100);
        return buf;
    }

    inline std::string FormatLevelMetres(int altFt)
    {
        char buf[16];
        sprintf_s(buf, "S%04d", (int)lround(altFt * 0.3048 / 10.0));
        return buf;
    }

    inline std::string FormatAltitudeUnit(int altFt, AltUnit unit)
    {
        switch (unit)
        {
        case AltUnit::M:
            return FormatLevelMetres(altFt);
        case AltUnit::FLM:
            return FormatLevelFeet(altFt) + " " + FormatLevelMetres(altFt);
        case AltUnit::FL:
        default:
            return FormatLevelFeet(altFt);
        }
    }

    inline std::string FormatVerticalSpeedUnit(int fpm, VsUnit unit)
    {
        if (fpm > -100 && fpm < 100)
            return std::string();

        char buf[16];
        if (unit == VsUnit::MS)
            sprintf_s(buf, "ms%+.1f", fpm * 0.00508);
        else
            sprintf_s(buf, "fm%+d", fpm);
        return buf;
    }

    inline std::string FormatGroundSpeedUnit(int kt, GsUnit unit)
    {
        char buf[16];
        if (unit == GsUnit::Kmh)
            sprintf_s(buf, "Km%03d", (int)lround(kt * 1.852));
        else
            sprintf_s(buf, "Kt%03d", kt);
        return buf;
    }

    inline std::string FormatDistanceUnit(double nm, DistUnit unit)
    {
        char buf[16];
        if (unit == DistUnit::Km)
            sprintf_s(buf, "%.0f", nm * 1.852);
        else
            sprintf_s(buf, "%.0f", nm);
        return buf;
    }

    inline int ParseQnhHpa(const std::string& metar)
    {
        for (size_t i = 0; i + 4 < metar.size(); i++)
        {
            char c = metar[i];
            if (c != 'Q' && c != 'A')
                continue;
            if (!isdigit((unsigned char)metar[i + 1]) || !isdigit((unsigned char)metar[i + 2]) ||
                !isdigit((unsigned char)metar[i + 3]) || !isdigit((unsigned char)metar[i + 4]))
                continue;
            bool boundaryOk = (i == 0) || !isalnum((unsigned char)metar[i - 1]);
            bool endOk = (i + 5 >= metar.size()) || !isdigit((unsigned char)metar[i + 5]);
            if (!boundaryOk || !endOk)
                continue;

            int val = (metar[i + 1] - '0') * 1000 + (metar[i + 2] - '0') * 100 +
                (metar[i + 3] - '0') * 10 + (metar[i + 4] - '0');
            return (c == 'Q') ? val : (int)lround((val / 100.0) * 33.8639);
        }
        return -1;
    }
}
