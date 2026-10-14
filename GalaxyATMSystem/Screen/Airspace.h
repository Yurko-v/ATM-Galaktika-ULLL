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
        bool home = false;
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
    inline const size_t kFirPrefixLength = 4;

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

    // Position id (the OWNER entries) to callsign, from [POSITIONS] of the same file.
    inline std::map<std::string, std::string>& EsePositionCallsigns()
    {
        static std::map<std::string, std::string> callsigns;
        return callsigns;
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
        bool inPositions = false;
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
                inPositions = _stricmp(text.c_str(), "[POSITIONS]") == 0;
                line = NULL;
                continue;
            }
            if (inPositions)
            {
                // callsign:name:frequency:id:...; the first position with an id keeps it.
                const std::vector<std::string> fields = SplitColons(text);
                if (fields.size() >= 4 && !fields[3].empty())
                    EsePositionCallsigns().emplace(fields[3], fields[0]);
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

        std::map<std::string, int> sectorsPerFir;
        std::string homeFir;
        for (const AirspaceSector& s : sectors)
        {
            const std::string fir = s.name.substr(0, kFirPrefixLength);
            if (++sectorsPerFir[fir] > sectorsPerFir[homeFir] || homeFir.empty())
                homeFir = fir;
        }
        for (AirspaceSector& s : sectors)
            s.home = s.name.compare(0, kFirPrefixLength, homeFir) == 0;
        Log::Info("airspace", Log::Utf8(file) + ": " + std::to_string(sectors.size()) + " sectors");
        return sectors;
    }

    // The callsign of an OWNER id, or empty when [POSITIONS] has no such id.
    inline std::string EsePositionCallsign(const std::string& id)
    {
        AirspaceSectors();
        auto found = EsePositionCallsigns().find(id);
        return found != EsePositionCallsigns().end() ? found->second : std::string();
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

    inline std::set<std::string> OnlinePositions(CPlugIn* plugin)
    {
        std::set<std::string> online;
        const char* myId = plugin->ControllerMyself().GetPositionId();
        if (myId != NULL && *myId != '\0')
            online.insert(myId);
        for (CController c = plugin->ControllerSelectFirst(); c.IsValid(); c = plugin->ControllerSelectNext(c))
        {
            const char* id = c.GetPositionId();
            if (c.IsController() && id != NULL && *id != '\0')
                online.insert(id);
        }
        return online;
    }

    inline std::string ZoneHolder(const AirspaceSector& s, const std::set<std::string>& online)
    {
        for (const std::string& id : s.owners)
            if (online.count(id) != 0)
                return id;
        return s.owners.empty() ? std::string() : s.owners[0];
    }

    inline bool InsideZoneOf(const std::string& holder, const std::set<std::string>& online,
        const CPosition& p, int altFt)
    {
        for (const AirspaceSector& s : AirspaceSectors())
        {
            if (s.owners.empty() || altFt < s.bottomFt || altFt >= s.topFt || ZoneHolder(s, online) != holder)
                continue;
            if (InsideRing(s.ring, p) || DistanceToRingNm(s.ring, p) <= kJunctionNm)
                return true;
        }
        return false;
    }

    inline const size_t kZoneExitMemoLimit = 1024;
    // The airspace part of a sector name: "ULLL·ULLI CTA 2·030·060" gives "ULLI CTA 2".
    inline std::string SectorTitle(const AirspaceSector& s)
    {
        const size_t from = s.name.find('\xB7');
        if (from == std::string::npos)
            return s.name;
        const size_t to = s.name.find('\xB7', from + 1);
        return s.name.substr(from + 1, to == std::string::npos ? std::string::npos : to - from - 1);
    }

    // Area control and oceanic sectors; the rest (CTR, CTA, TMA, GND ...) belong to an aerodrome.
    inline bool EnRouteSector(const AirspaceSector& s)
    {
        const std::string title = SectorTitle(s);
        return title.find("ACC") != std::string::npos || title.find("OCA") != std::string::npos;
    }

    // The home aerodrome sectors around a destination at `at`: those over it, and the others
    // of the same aerodrome by the first word of their name ("ULLI CTA 2 EAST" with "ULLI CTR").
    inline const std::vector<const AirspaceSector*>& AerodromeArea(const std::string& ades, const CPosition& at)
    {
        static std::map<std::string, std::vector<const AirspaceSector*>> known;
        auto found = known.find(ades);
        if (found != known.end())
            return found->second;

        const auto key = [](const AirspaceSector& s)
        {
            const std::string title = SectorTitle(s);
            return title.substr(0, title.find(' '));
        };
        std::vector<const AirspaceSector*>& area = known[ades];
        std::set<std::string> keys;
        for (const AirspaceSector& s : AirspaceSectors())
            if (s.home && !s.owners.empty() && !EnRouteSector(s) && InsideRing(s.ring, at))
            {
                area.push_back(&s);
                keys.insert(key(s));
            }
        for (const AirspaceSector& s : AirspaceSectors())
            if (s.home && !s.owners.empty() && !EnRouteSector(s) && keys.count(key(s)) != 0
                && std::find(area.begin(), area.end(), &s) == area.end())
                area.push_back(&s);

        std::string names;
        for (const AirspaceSector* s : area)
            names += (names.empty() ? "" : ", ") + SectorTitle(*s);
        Log::Info("airspace", "aerodrome area of " + ades + ": " + (names.empty() ? std::string("none") : names));
        return area;
    }

    inline const ULONGLONG kAerodromeAreaRecheckMs = 2000;

    // The flight is inside the aerodrome area of the aerodrome it lands at, within the levels
    // of one of its sectors. It is handed to the aerodrome there and has no exit point left.
    inline bool InDestinationAerodromeArea(CFlightPlan& fp)
    {
        struct Memo
        {
            bool inside;
            ULONGLONG tick;
        };
        static std::map<std::string, Memo> memo;
        const ULONGLONG now = GetTickCount64();
        const std::string callsign = fp.GetCallsign();
        auto known = memo.find(callsign);
        if (known != memo.end() && now - known->second.tick < kAerodromeAreaRecheckMs)
            return known->second.inside;
        if (memo.size() > kZoneExitMemoLimit)
            memo.clear();

        bool inside = false;
        const char* adesName = fp.GetFlightPlanData().GetDestination();
        const std::string ades = adesName != NULL ? adesName : "";
        CFlightPlanExtractedRoute route = fp.GetExtractedRoute();
        const int last = route.GetPointsNumber() - 1;
        const char* lastName = last >= 0 ? route.GetPointName(last) : NULL;
        CRadarTarget rt = fp.GetCorrelatedRadarTarget();
        CRadarTargetPositionData pos = rt.IsValid() ? rt.GetPosition() : fp.GetFPTrackPosition();
        if (!ades.empty() && lastName != NULL && _stricmp(lastName, ades.c_str()) == 0 && pos.IsValid())
        {
            const CPosition p = pos.GetPosition();
            const int altFt = pos.GetFlightLevel();
            for (const AirspaceSector* s : AerodromeArea(ades, route.GetPointPosition(last)))
                if (altFt >= s->bottomFt && altFt < s->topFt && InsideRing(s->ring, p))
                {
                    inside = true;
                    break;
                }
        }
        if (known != memo.end() && known->second.inside != inside)
            Log::Info("formular", callsign + (inside ? ": in the aerodrome area of " + ades + " - no exit point"
                : ": out of the aerodrome area of " + ades));
        memo[callsign] = { inside, now };
        return inside;
    }

    inline const ULONGLONG kZoneExitRecheckMs = 2000;

    inline std::string ZoneExitPoint(CFlightPlan& fp, CPlugIn* plugin, bool& neverInHomeZone)
    {
        struct Memo
        {
            std::string point;
            bool neverInHomeZone;
            ULONGLONG tick;
        };
        static std::map<std::string, Memo> memo;
        const ULONGLONG now = GetTickCount64();
        const std::string callsign = fp.GetCallsign();
        auto known = memo.find(callsign);
        if (known != memo.end() && now - known->second.tick < kZoneExitRecheckMs)
        {
            neverInHomeZone = known->second.neverInHomeZone;
            return known->second.point;
        }
        if (memo.size() > kZoneExitMemoLimit)
            memo.clear();

        std::string point;
        CRadarTarget rt = fp.GetCorrelatedRadarTarget();
        CRadarTargetPositionData pos = rt.IsValid() ? rt.GetPosition() : fp.GetFPTrackPosition();
        const int altFt = pos.IsValid() ? pos.GetFlightLevel() : 0;
        const AirspaceSector* here = pos.IsValid() ? SectorAt(pos.GetPosition(), altFt) : NULL;
        std::string holder;
        neverInHomeZone = false;
        if (here != NULL)
        {
            const std::set<std::string> online = OnlinePositions(plugin);
            CFlightPlanExtractedRoute route = fp.GetExtractedRoute();
            const auto levelOver = [&](int i)
            {
                const int profileFt = route.GetPointCalculatedProfileAltitude(i);
                return profileFt > 0 ? profileFt : altFt;
            };

            const auto staffed = [&](const AirspaceSector& s) { return online.count(ZoneHolder(s, online)) != 0; };

            int from = max(0, route.GetPointsCalculatedIndex());
            const AirspaceSector* zone = here;
            if (!here->home || !staffed(*here))
            {
                // A foreign or unstaffed zone has no exit of its own: take the next staffed home zone
                // ahead, or the first home zone when none along the route is staffed.
                const AirspaceSector* firstHome = here->home ? here : NULL;
                int firstHomeAt = from;
                zone = NULL;
                for (int i = from; i < route.GetPointsNumber(); i++)
                {
                    const AirspaceSector* ahead = SectorAt(route.GetPointPosition(i), levelOver(i));
                    if (ahead == NULL || !ahead->home)
                        continue;
                    if (firstHome == NULL)
                    {
                        firstHome = ahead;
                        firstHomeAt = i;
                    }
                    if (staffed(*ahead))
                    {
                        zone = ahead;
                        from = i;
                        break;
                    }
                }
                if (zone == NULL)
                {
                    zone = firstHome;
                    from = firstHomeAt;
                }
                neverInHomeZone = zone == NULL;
            }
            const auto exitOf = [&](const std::string& who, const std::set<std::string>& on)
            {
                std::string lastInside;
                for (int i = from; i < route.GetPointsNumber(); i++)
                {
                    if (!InsideZoneOf(who, on, route.GetPointPosition(i), levelOver(i)))
                        return lastInside;
                    const char* name = route.GetPointName(i);
                    if (name != NULL && *name != '\0')
                        lastInside = name;
                }
                return std::string();
            };

            if (zone != NULL)
            {
                holder = ZoneHolder(*zone, online);
                if (staffed(*zone))
                    point = exitOf(holder, online);
                else
                {
                    // Nobody along the route is on: the zone goes to the first of its owners whose
                    // zone, with them logged in, still has a point ahead. In ACC-SE (R2:R:...) with
                    // the flight about to leave it, R2's zone has none; R's takes in ACC-N and R5's
                    // Petrozavodsk too, and ends at UMDEL.
                    for (const std::string& id : zone->owners)
                    {
                        std::set<std::string> assumed = online;
                        assumed.insert(id);
                        point = exitOf(id, assumed);
                        if (!point.empty())
                        {
                            holder = id;
                            break;
                        }
                    }
                }
            }
        }

        if (known == memo.end() || known->second.point != point)
            Log::Info("formular", callsign + ": exit from the zone of " + (!holder.empty() ? holder : std::string("-"))
                + " is " + (neverInHomeZone ? std::string("none - the route stays outside the home sectors")
                    : point.empty() ? std::string("unknown") : point));
        memo[callsign] = { point, neverInHomeZone, now };
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
