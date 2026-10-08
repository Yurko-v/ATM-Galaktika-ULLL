#pragma once

#include "Screen/FormularItems.h"

namespace Galaxy
{
    // The 24-bit address as filed in item 18 (CODE/155BE9). VATSIM has no transponder
    // address of its own, so this is the only place a real one can come from.
    inline std::string ModeSAddress(const char* remarks)
    {
        if (remarks == NULL)
            return std::string();
        for (const char* at = strstr(remarks, "CODE/"); at != NULL; at = strstr(at + 1, "CODE/"))
        {
            if (at != remarks && isalnum((unsigned char)at[-1]))
                continue;
            const char* hex = at + 5;
            int n = 0;
            while (n < 7 && isxdigit((unsigned char)hex[n]))
                n++;
            if (n != 6)
                continue;
            std::string out(hex, 6);
            for (char& c : out)
                c = (char)toupper((unsigned char)c);
            return out;
        }
        return std::string();
    }

    // State of registry by the ICAO block the address falls in (Annex 10, vol. III).
    inline const char* ModeSCountry(unsigned address)
    {
        struct Block { unsigned from, to; const char* code; };
        static const Block kBlocks[] = {
            { 0x010000, 0x017FFF, "EGY" }, { 0x06A000, 0x06A3FF, "QAT" },
            { 0x100000, 0x1FFFFF, "RUS" },
            { 0x300000, 0x33FFFF, "ITA" }, { 0x340000, 0x37FFFF, "ESP" }, { 0x380000, 0x3BFFFF, "FRA" },
            { 0x3C0000, 0x3FFFFF, "DEU" }, { 0x400000, 0x43FFFF, "GBR" }, { 0x440000, 0x447FFF, "AUT" },
            { 0x448000, 0x44FFFF, "BEL" }, { 0x450000, 0x457FFF, "BGR" }, { 0x458000, 0x45FFFF, "DNK" },
            { 0x460000, 0x467FFF, "FIN" }, { 0x468000, 0x46FFFF, "GRC" }, { 0x470000, 0x477FFF, "HUN" },
            { 0x478000, 0x47FFFF, "NOR" }, { 0x480000, 0x487FFF, "NLD" }, { 0x488000, 0x48FFFF, "POL" },
            { 0x490000, 0x497FFF, "PRT" }, { 0x498000, 0x49FFFF, "CZE" }, { 0x4A0000, 0x4A7FFF, "ROU" },
            { 0x4A8000, 0x4AFFFF, "SWE" }, { 0x4B0000, 0x4B7FFF, "CHE" }, { 0x4B8000, 0x4BFFFF, "TUR" },
            { 0x4C0000, 0x4C7FFF, "SRB" }, { 0x4CA000, 0x4CAFFF, "IRL" },
            { 0x501C00, 0x501FFF, "HRV" }, { 0x502C00, 0x502FFF, "LVA" }, { 0x503C00, 0x503FFF, "LTU" },
            { 0x504C00, 0x504FFF, "MDA" }, { 0x505C00, 0x505FFF, "SVK" }, { 0x506C00, 0x506FFF, "SVN" },
            { 0x507C00, 0x507FFF, "UZB" }, { 0x508000, 0x50FFFF, "UKR" }, { 0x510000, 0x5103FF, "BLR" },
            { 0x511000, 0x5113FF, "EST" }, { 0x514000, 0x5143FF, "GEO" }, { 0x515000, 0x5153FF, "TJK" },
            { 0x600000, 0x6003FF, "ARM" }, { 0x600800, 0x600BFF, "AZE" }, { 0x601000, 0x6013FF, "KGZ" },
            { 0x601800, 0x601BFF, "TKM" }, { 0x682000, 0x6823FF, "MNG" }, { 0x683000, 0x6833FF, "KAZ" },
            { 0x710000, 0x717FFF, "SAU" }, { 0x718000, 0x71FFFF, "KOR" }, { 0x730000, 0x737FFF, "IRN" },
            { 0x738000, 0x73FFFF, "ISR" }, { 0x780000, 0x7BFFFF, "CHN" }, { 0x800000, 0x83FFFF, "IND" },
            { 0x840000, 0x87FFFF, "JPN" }, { 0x880000, 0x887FFF, "THA" }, { 0x888000, 0x88FFFF, "VNM" },
            { 0x896000, 0x896FFF, "ARE" }, { 0xA00000, 0xAFFFFF, "USA" }, { 0xC00000, 0xC3FFFF, "CAN" },
        };
        for (const Block& b : kBlocks)
            if (address >= b.from && address <= b.to)
                return b.code;
        return NULL;
    }

    // Outside air temperature of the standard atmosphere at a pressure altitude: the
    // network sends no weather from the aircraft, and the IAS and Mach beside it are
    // worked out on the same ISA, still-air assumption.
    inline double IsaTemperatureC(int pressureAltFt)
    {
        return max(-56.5, 15.0 - 0.0019812 * max(0, pressureAltFt));
    }

    // The Mode-S page of the expanded label, laid out like the downlinked data block:
    //   155BE9(RUS) SDM6407
    //   F330 F--- fm0000 1013.3     level, selected level, vertical rate, baro setting
    //   062° Kt448 ---              magnetic heading, true airspeed, roll
    //   059° Kt273 M0.77            track, IAS, Mach
    //   ---° Kt--- -50.4°C          wind, temperature
    inline std::vector<std::wstring> ModeSPage(CFlightPlan& fp, CRadarTarget& rt, int transitionLevelFL,
        const std::wstring& qnhHpa)
    {
        std::vector<std::wstring> out;
        CRadarTargetPositionData pos = rt.GetPosition();
        wchar_t buf[64];

        const std::string address = ModeSAddress(fp.GetFlightPlanData().GetRemarks());
        std::wstring line = address.empty() ? std::wstring(L"------") : Widen(address.c_str());
        const char* country = address.empty() ? NULL : ModeSCountry(strtoul(address.c_str(), NULL, 16));
        if (country != NULL)
            line += L"(" + Widen(country) + L")";
        line += L" " + Widen(fp.GetCallsign());
        out.push_back(line);

        const int fl = pos.GetFlightLevel();
        const int vs = rt.GetVerticalSpeed();
        wchar_t rate[16];
        if (vs > -100 && vs < 100)
            wcscpy_s(rate, L"fm0000");
        else
            swprintf_s(rate, L"fm%+05d", (int)lround(vs / 50.0) * 50);
        // Above the transition level the aircraft is on standard pressure; below it the
        // only setting known is the QNH the panel shows.
        const std::wstring baro = fl / 100 >= transitionLevelFL || qnhHpa.empty()
            ? std::wstring(L"1013.3") : qnhHpa + L".0";
        swprintf_s(buf, L"F%03d F--- %s %s", fl / 100, rate, baro.c_str());
        out.push_back(buf);

        const int gs = rt.GetGS();
        const int bank = pos.GetReportedBank();
        wchar_t roll[16];
        if (abs(bank) < 2 || abs(bank) > 90)
            wcscpy_s(roll, L"---");
        else
            swprintf_s(roll, L"%c%02d\x00B0", bank < 0 ? L'R' : L'L', abs(bank));
        swprintf_s(buf, L"%03d\x00B0 Kt%03d %s", (pos.GetReportedHeading() + 359) % 360 + 1, gs, roll);
        out.push_back(buf);

        int ias = 0, machX100 = 0;
        const int track = ((int)lround(rt.GetTrackHeading()) + 359) % 360 + 1;
        if (CalculatedIasMach(gs, fl, ias, machX100))
            swprintf_s(buf, L"%03d\x00B0 Kt%03d M%d.%02d", track, ias, machX100 / 100, machX100 % 100);
        else
            swprintf_s(buf, L"%03d\x00B0 Kt--- M-.--", track);
        out.push_back(buf);

        swprintf_s(buf, L"---\x00B0 Kt--- %.1f\x00B0" L"C", IsaTemperatureC(fl));
        out.push_back(buf);
        return out;
    }
}
