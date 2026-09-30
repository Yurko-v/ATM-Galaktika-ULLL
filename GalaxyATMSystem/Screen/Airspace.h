#pragma once

#include "Core/Common.h"

namespace Galaxy
{
    struct AirspaceSector
    {
        std::string name;
        int bottomFt = 0;
        int topFt = 0;
        std::vector<std::string> owners;
        std::vector<CPosition> ring;
        double area = 0.0;
    };

    enum class PointZone { Mine, Junction, Unowned, Other, Unknown };

    struct PointVerdict
    {
        PointZone zone = PointZone::Unknown;
        std::string ownerId;
    };

    inline const double kJunctionNm = 1.5;
    inline const int kJunctionFt = 100;
    inline const double kSameCoordDeg = 1e-6;

    inline bool ParseEseCoord(const std::string& text, double& out)
    {
        if (text.size() < 2)
            return false;
        const char hemi = (char)toupper((unsigned char)text[0]);
        double parts[3] = { 0, 0, 0 };
        int part = 0;
        std::string cur;
        for (size_t i = 1; i <= text.size(); i++)
        {
            const char ch = i < text.size() ? text[i] : '\0';
            if ((ch == '.' && part < 2) || ch == '\0')
            {
                if (cur.empty())
                    return false;
                parts[part++] = atof(cur.c_str());
                cur.clear();
                if (ch == '\0')
                    break;
            }
            else
                cur += ch;
        }
        if (part < 3)
            return false;
        out = parts[0] + parts[1] / 60.0 + parts[2] / 3600.0;
        if (hemi == 'S' || hemi == 'W')
            out = -out;
        return hemi == 'N' || hemi == 'S' || hemi == 'E' || hemi == 'W';
    }

    inline std::vector<std::string> SplitColons(const std::string& line)
    {
        std::vector<std::string> fields;
        std::string cur;
        for (char ch : line)
        {
            if (ch == ':')
            {
                fields.push_back(cur);
                cur.clear();
            }
            else if (ch != '\r' && ch != '\n')
                cur += ch;
        }
        fields.push_back(cur);
        return fields;
    }

    inline bool SameCoord(const CPosition& a, const CPosition& b)
    {
        return fabs(a.m_Latitude - b.m_Latitude) < kSameCoordDeg && fabs(a.m_Longitude - b.m_Longitude) < kSameCoordDeg;
    }

    inline std::vector<CPosition> ChainBorder(const std::vector<std::vector<CPosition>>& lines)
    {
        std::vector<CPosition> ring;
        std::vector<bool> used(lines.size(), false);
        for (size_t first = 0; first < lines.size() && ring.empty(); first++)
        {
            if (lines[first].empty())
                continue;
            ring = lines[first];
            used[first] = true;
        }
        for (bool grown = true; grown;)
        {
            grown = false;
            for (size_t i = 0; i < lines.size(); i++)
            {
                if (used[i] || lines[i].empty())
                    continue;
                const std::vector<CPosition>& line = lines[i];
                if (SameCoord(line.front(), ring.back()))
                    ring.insert(ring.end(), line.begin() + 1, line.end());
                else if (SameCoord(line.back(), ring.back()))
                    ring.insert(ring.end(), line.rbegin() + 1, line.rend());
                else if (SameCoord(line.back(), ring.front()))
                    ring.insert(ring.begin(), line.begin(), line.end() - 1);
                else if (SameCoord(line.front(), ring.front()))
                    ring.insert(ring.begin(), line.rbegin(), line.rend() - 1);
                else
                    continue;
                used[i] = grown = true;
            }
        }
        for (size_t i = 0; i < lines.size(); i++)
            if (!used[i])
                ring.insert(ring.end(), lines[i].begin(), lines[i].end());
        return ring;
    }

    inline std::wstring FindSectorExtensionFile()
    {
        std::vector<std::wstring> dirs;
        HMODULE topsky = GetModuleHandleW(L"TopSky.dll");
        wchar_t path[MAX_PATH] = {};
        if (topsky != NULL && GetModuleFileNameW(topsky, path, MAX_PATH) != 0)
        {
            std::wstring dir = path;
            for (int up = 0; up < 5; up++)
            {
                const size_t cut = dir.find_last_of(L"\\/");
                if (cut == std::wstring::npos)
                    break;
                dir = dir.substr(0, cut);
                dirs.push_back(dir + L"\\");
            }
        }
        if (GetCurrentDirectoryW(MAX_PATH, path) != 0)
            dirs.push_back(std::wstring(path) + L"\\");

        std::wstring best;
        FILETIME newest = { 0, 0 };
        for (const std::wstring& dir : dirs)
        {
            WIN32_FIND_DATAW found;
            HANDLE h = FindFirstFileW((dir + L"*.ese").c_str(), &found);
            if (h == INVALID_HANDLE_VALUE)
                continue;
            do
            {
                if (CompareFileTime(&found.ftLastWriteTime, &newest) > 0)
                {
                    newest = found.ftLastWriteTime;
                    best = dir + found.cFileName;
                }
            } while (FindNextFileW(h, &found));
            FindClose(h);
            if (!best.empty())
                break;
        }
        return best;
    }

    inline double RingArea(const std::vector<CPosition>& ring)
    {
        double twice = 0.0;
        for (size_t i = 0, j = ring.size() - 1; i < ring.size(); j = i++)
        {
            const double k = cos((ring[i].m_Latitude + ring[j].m_Latitude) * M_PI / 360.0);
            twice += (ring[j].m_Longitude - ring[i].m_Longitude) * k * (ring[j].m_Latitude + ring[i].m_Latitude);
        }
        return fabs(twice) / 2.0;
    }

    inline const std::vector<AirspaceSector>& AirspaceSectors()
    {
        static std::vector<AirspaceSector> sectors;
        static bool loaded = false;
        if (loaded)
            return sectors;
        loaded = true;

        const std::wstring file = FindSectorExtensionFile();
        FILE* f = NULL;
        if (file.empty() || _wfopen_s(&f, file.c_str(), L"rb") != 0 || f == NULL)
        {
            Log::Warn("airspace", "no .ese sector file found near TopSky - exit point DCT/coordination falls back to coordination");
            return sectors;
        }

        std::map<std::string, std::vector<CPosition>> lines;
        std::vector<std::pair<AirspaceSector, std::vector<std::string>>> pending;
        std::vector<CPosition>* line = NULL;
        bool inAirspace = false;
        char buf[2048];
        while (fgets(buf, sizeof(buf), f) != NULL)
        {
            std::string text = buf;
            while (!text.empty() && (text.back() == '\r' || text.back() == '\n' || text.back() == ' '))
                text.pop_back();
            if (text.empty() || text[0] == ';')
                continue;
            if (text[0] == '[')
            {
                inAirspace = _stricmp(text.c_str(), "[AIRSPACE]") == 0;
                line = NULL;
                continue;
            }
            if (!inAirspace)
                continue;

            const std::vector<std::string> fields = SplitColons(text);
            const std::string& kind = fields[0];
            if (kind == "SECTORLINE" && fields.size() >= 2)
            {
                line = &lines[fields[1]];
                line->clear();
            }
            else if (kind == "COORD" && fields.size() >= 3 && line != NULL)
            {
                CPosition p;
                if (ParseEseCoord(fields[1], p.m_Latitude) && ParseEseCoord(fields[2], p.m_Longitude))
                    line->push_back(p);
            }
            else if (kind == "SECTOR" && fields.size() >= 4)
            {
                line = NULL;
                AirspaceSector s;
                s.name = fields[1];
                s.bottomFt = atoi(fields[fields.size() - 2].c_str());
                s.topFt = atoi(fields[fields.size() - 1].c_str());
                pending.push_back({ s, {} });
            }
            else if (kind == "OWNER" && !pending.empty())
            {
                pending.back().first.owners.assign(fields.begin() + 1, fields.end());
            }
            else if (kind == "BORDER" && !pending.empty())
            {
                pending.back().second.assign(fields.begin() + 1, fields.end());
            }
            else if (kind != "COORD")
            {
                line = NULL;
            }
        }
        fclose(f);

        for (auto& entry : pending)
        {
            std::vector<std::vector<CPosition>> border;
            for (const std::string& id : entry.second)
            {
                auto found = lines.find(id);
                if (found != lines.end() && !found->second.empty())
                    border.push_back(found->second);
            }
            entry.first.ring = ChainBorder(border);
            entry.first.area = RingArea(entry.first.ring);
            if (entry.first.ring.size() >= 3)
                sectors.push_back(entry.first);
        }
        Log::Info("airspace", Log::Utf8(file) + ": " + std::to_string(sectors.size()) + " sectors");
        return sectors;
    }

    inline bool InsideRing(const std::vector<CPosition>& ring, const CPosition& p)
    {
        bool inside = false;
        for (size_t i = 0, j = ring.size() - 1; i < ring.size(); j = i++)
        {
            const CPosition& a = ring[i];
            const CPosition& b = ring[j];
            if ((a.m_Latitude > p.m_Latitude) != (b.m_Latitude > p.m_Latitude))
            {
                const double x = (b.m_Longitude - a.m_Longitude) * (p.m_Latitude - a.m_Latitude)
                    / (b.m_Latitude - a.m_Latitude) + a.m_Longitude;
                if (p.m_Longitude < x)
                    inside = !inside;
            }
        }
        return inside;
    }

    inline double DistanceToRingNm(const std::vector<CPosition>& ring, const CPosition& p)
    {
        const double kx = 60.0 * cos(p.m_Latitude * M_PI / 180.0), ky = 60.0;
        double best = 1e9;
        for (size_t i = 0, j = ring.size() - 1; i < ring.size(); j = i++)
        {
            const double ax = (ring[j].m_Longitude - p.m_Longitude) * kx, ay = (ring[j].m_Latitude - p.m_Latitude) * ky;
            const double bx = (ring[i].m_Longitude - p.m_Longitude) * kx, by = (ring[i].m_Latitude - p.m_Latitude) * ky;
            const double dx = bx - ax, dy = by - ay;
            const double len = dx * dx + dy * dy;
            double t = len > 1e-12 ? -(ax * dx + ay * dy) / len : 0.0;
            t = max(0.0, min(1.0, t));
            const double cx = ax + t * dx, cy = ay + t * dy;
            best = min(best, sqrt(cx * cx + cy * cy));
        }
        return best;
    }

    inline const AirspaceSector* SectorAt(const CPosition& p, int altFt)
    {
        const AirspaceSector* best = NULL;
        for (const AirspaceSector& s : AirspaceSectors())
        {
            if (s.owners.empty() || altFt < s.bottomFt || altFt >= s.topFt || !InsideRing(s.ring, p))
                continue;
            if (best == NULL || s.area < best->area)
                best = &s;
        }
        return best;
    }

    inline bool InsideZoneOf(const std::string& owner, const CPosition& p, int altFt)
    {
        for (const AirspaceSector& s : AirspaceSectors())
        {
            if (s.owners.empty() || s.owners[0] != owner || altFt < s.bottomFt || altFt >= s.topFt)
                continue;
            if (InsideRing(s.ring, p) || DistanceToRingNm(s.ring, p) <= kJunctionNm)
                return true;
        }
        return false;
    }

    inline const ULONGLONG kZoneExitRecheckMs = 2000;
    inline const size_t kZoneExitMemoLimit = 1024;

    inline std::string ZoneExitPoint(CFlightPlan& fp)
    {
        struct Memo
        {
            std::string point;
            ULONGLONG tick;
        };
        static std::map<std::string, Memo> memo;
        const ULONGLONG now = GetTickCount64();
        const std::string callsign = fp.GetCallsign();
        auto known = memo.find(callsign);
        if (known != memo.end() && now - known->second.tick < kZoneExitRecheckMs)
            return known->second.point;
        if (memo.size() > kZoneExitMemoLimit)
            memo.clear();

        std::string point;
        CRadarTarget rt = fp.GetCorrelatedRadarTarget();
        CRadarTargetPositionData pos = rt.IsValid() ? rt.GetPosition() : fp.GetFPTrackPosition();
        const int altFt = pos.IsValid() ? pos.GetFlightLevel() : 0;
        const AirspaceSector* here = pos.IsValid() ? SectorAt(pos.GetPosition(), altFt) : NULL;
        if (here != NULL)
        {
            CFlightPlanExtractedRoute route = fp.GetExtractedRoute();
            std::string lastInside;
            for (int i = max(0, route.GetPointsCalculatedIndex()); i < route.GetPointsNumber(); i++)
            {
                if (!InsideZoneOf(here->owners[0], route.GetPointPosition(i), altFt))
                {
                    point = lastInside;
                    break;
                }
                const char* name = route.GetPointName(i);
                if (name != NULL && *name != '\0')
                    lastInside = name;
            }
        }

        if (known == memo.end() || known->second.point != point)
            Log::Info("formular", callsign + ": exit from the zone of " + (here != NULL ? here->owners[0] : std::string("-"))
                + " is " + (point.empty() ? std::string("unknown") : point));
        memo[callsign] = { point, now };
        return point;
    }

    inline std::string SectorOwner(const AirspaceSector& s, const std::set<std::string>& online)
    {
        for (const std::string& id : s.owners)
            if (online.count(id) != 0)
                return id;
        return "";
    }

    inline PointVerdict ClassifyPoint(const CPosition& p, int altFt, const std::set<std::string>& online,
        const std::string& me)
    {
        PointVerdict verdict;
        const std::vector<AirspaceSector>& sectors = AirspaceSectors();
        bool anySector = false;
        bool junction = false;
        std::string other;
        for (const AirspaceSector& s : sectors)
        {
            if (altFt < s.bottomFt - kJunctionFt || altFt > s.topFt + kJunctionFt)
                continue;
            const std::string owner = SectorOwner(s, online);
            const bool inside = InsideRing(s.ring, p);
            const bool inBand = altFt >= s.bottomFt && altFt < s.topFt;
            if (owner == me && !me.empty())
            {
                if (inside && inBand)
                {
                    verdict.zone = PointZone::Mine;
                    verdict.ownerId = me;
                    return verdict;
                }
                if (inside || DistanceToRingNm(s.ring, p) <= kJunctionNm)
                    junction = true;
                continue;
            }
            if (!inside || !inBand)
                continue;
            anySector = true;
            if (!owner.empty() && other.empty())
                other = owner;
        }
        if (junction)
            verdict.zone = PointZone::Junction;
        else if (!other.empty())
            verdict.zone = PointZone::Other;
        else if (anySector)
            verdict.zone = PointZone::Unowned;
        verdict.ownerId = junction ? me : other;
        return verdict;
    }
}
