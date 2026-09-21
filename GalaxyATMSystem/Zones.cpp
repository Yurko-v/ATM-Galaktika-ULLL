#include "pch.h"
#include "Zones.h"
#include "Json.h"
#include "Lang.h"
#include "Net.h"

#include <algorithm>
#include <cmath>
#include <fstream>

namespace
{
    const double kPi = 3.14159265358979323846;

    std::wstring ToUpper(std::wstring s)
    {
        if (!s.empty())
            CharUpperBuffW(&s[0], (DWORD)s.size());
        return s;
    }

    ZoneKind KindFromText(const std::wstring& raw, ZoneKind fallback)
    {
        std::wstring t = ToUpper(raw);
        if (t.find(L"ЗАПРЕТ") != std::wstring::npos || t.find(L"PROHIB") != std::wstring::npos
            || t == L"P")
            return ZoneKind::Prohibited;
        if (t.find(L"ОПАСН") != std::wstring::npos || t.find(L"DANGER") != std::wstring::npos
            || t == L"D")
            return ZoneKind::Danger;
        if (t.find(L"ОГРАНИЧ") != std::wstring::npos || t.find(L"RESTRICT") != std::wstring::npos
            || t == L"R")
            return ZoneKind::Restricted;
        return fallback;
    }

    bool ParseDegreesHalf(const std::wstring& raw, bool& isLat, double& out)
    {
        std::wstring s;
        for (wchar_t c : raw)
        {
            if (!iswspace(c))
                s += c;
        }
        if (s.empty())
            return false;

        double sign = 1.0;
        wchar_t hemi = towupper(s[0]);
        if (hemi == L'N' || hemi == L'S' || hemi == L'E' || hemi == L'W')
        {
            isLat = (hemi == L'N' || hemi == L'S');
            sign = (hemi == L'S' || hemi == L'W') ? -1.0 : 1.0;
            s.erase(s.begin());
        }
        else if (hemi == L'-' || hemi == L'+')
        {
            sign = (hemi == L'-') ? -1.0 : 1.0;
            s.erase(s.begin());
        }
        if (s.empty())
            return false;

        std::vector<std::wstring> parts;
        std::wstring cur;
        for (wchar_t c : s)
        {
            if (c == L'.')
            {
                parts.push_back(cur);
                cur.clear();
            }
            else if (iswdigit(c))
            {
                cur += c;
            }
            else
            {
                return false;
            }
        }
        parts.push_back(cur);

        if (parts.size() <= 2)
        {
            std::wstring joined = parts[0];
            if (parts.size() == 2)
                joined += L"." + parts[1];
            if (joined.empty())
                return false;
            out = sign * _wtof(joined.c_str());
            return true;
        }

        double deg = _wtof(parts[0].c_str());
        double min = _wtof(parts[1].c_str());
        double sec = _wtof(parts[2].c_str());
        if (parts.size() >= 4 && !parts[3].empty())
        {
            double frac = _wtof(parts[3].c_str());
            for (size_t i = 0; i < parts[3].size(); i++)
                frac /= 10.0;
            sec += frac;
        }
        out = sign * (deg + min / 60.0 + sec / 3600.0);
        return true;
    }

    bool ParsePointString(const std::wstring& raw, EuroScopePlugIn::CPosition& out)
    {
        size_t split = raw.find_first_of(L":,;");
        if (split == std::wstring::npos)
        {
            size_t scan = raw.find_first_not_of(L" \t");
            if (scan != std::wstring::npos)
                split = raw.find_first_of(L" \t", scan);
        }
        if (split == std::wstring::npos)
            return false;

        std::wstring first = raw.substr(0, split);
        std::wstring second = raw.substr(split + 1);

        bool firstIsLat = true, secondIsLat = false;
        double a = 0.0, b = 0.0;
        if (!ParseDegreesHalf(first, firstIsLat, a))
            return false;
        if (!ParseDegreesHalf(second, secondIsLat, b))
            return false;

        double lat = firstIsLat ? a : b;
        double lon = firstIsLat ? b : a;
        if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0)
            return false;

        out.m_Latitude = lat;
        out.m_Longitude = lon;
        return true;
    }

    bool ParsePoint(const Json::Value& v, EuroScopePlugIn::CPosition& out)
    {
        if (v.kind == Json::Value::Kind::Array)
        {
            if (v.arr.size() < 2)
                return false;
            const double kNotACoord = 1e9;
            double lat = v.arr[0].AsNumber(kNotACoord);
            double lon = v.arr[1].AsNumber(kNotACoord);
            if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0)
                return false;
            out.m_Latitude = lat;
            out.m_Longitude = lon;
            return true;
        }
        if (v.kind == Json::Value::Kind::String)
            return ParsePointString(v.AsString(), out);
        return false;
    }

    void RingFromCircle(const EuroScopePlugIn::CPosition& centre, double radiusNM,
        std::vector<EuroScopePlugIn::CPosition>& out)
    {
        const int kSteps = 72;
        const double kNmPerDegLat = 60.0;
        double cosLat = cos(centre.m_Latitude * kPi / 180.0);
        if (cosLat < 0.01)
            cosLat = 0.01;

        for (int i = 0; i < kSteps; i++)
        {
            double a = (2.0 * kPi * i) / kSteps;
            EuroScopePlugIn::CPosition p;
            p.m_Latitude = centre.m_Latitude + (radiusNM * cos(a)) / kNmPerDegLat;
            p.m_Longitude = centre.m_Longitude + (radiusNM * sin(a)) / (kNmPerDegLat * cosLat);
            out.push_back(p);
        }
    }

    bool ParseOne(const Json::Value& item, Zone& out)
    {
        if (item.kind != Json::Value::Kind::Object)
            return false;

        if (const Json::Value* v = item.Find(L"Id"))
            out.id = v->AsText();
        if (const Json::Value* v = item.Find(L"Name"))
            out.name = v->AsString();
        if (const Json::Value* v = item.Find(L"Type"))
            out.kind = KindFromText(v->AsString(), out.kind);
        if (const Json::Value* v = item.Find(L"Lower"))
            out.lower = v->AsText();
        if (const Json::Value* v = item.Find(L"Upper"))
            out.upper = v->AsText();
        if (const Json::Value* v = item.Find(L"Note"))
            out.note = v->AsString();

        if (const Json::Value* v = item.Find(L"Activation"))
            out.activation = v->AsText();

        if (const Json::Value* v = item.Find(L"Label"))
        {
            EuroScopePlugIn::CPosition p;
            if (ParsePoint(*v, p))
            {
                out.labelPos = p;
                out.hasLabelPos = true;
            }
        }

        if (const Json::Value* circle = item.Find(L"Circle"))
        {
            if (circle->kind == Json::Value::Kind::Object)
            {
                EuroScopePlugIn::CPosition centre;
                const Json::Value* c = circle->Find(L"Center");
                if (c == NULL)
                    c = circle->Find(L"Centre");
                if (c != NULL && ParsePoint(*c, centre))
                {
                    double radiusNM = 0.0;
                    if (const Json::Value* r = circle->Find(L"RadiusNM"))
                        radiusNM = r->AsNumber(0.0);
                    else if (const Json::Value* r = circle->Find(L"RadiusKm"))
                        radiusNM = r->AsNumber(0.0) / 1.852;
                    if (radiusNM > 0.0)
                        RingFromCircle(centre, radiusNM, out.ring);
                }
            }
        }

        if (out.ring.empty())
        {
            const Json::Value* points = item.Find(L"Points");
            if (points != NULL && points->kind == Json::Value::Kind::Array)
            {
                for (const Json::Value& p : points->arr)
                {
                    EuroScopePlugIn::CPosition pos;
                    if (ParsePoint(p, pos))
                        out.ring.push_back(pos);
                }
            }
        }

        if (out.ring.size() >= 2
            && out.ring.front().m_Latitude == out.ring.back().m_Latitude
            && out.ring.front().m_Longitude == out.ring.back().m_Longitude)
            out.ring.pop_back();

        if (out.ring.size() < 3)
            return false;

        if (out.id.empty() && out.name.empty())
            out.id = L"ZONE";

        return true;
    }
}

namespace
{
    std::vector<std::wstring> SplitFields(const std::wstring& line, size_t maxFields)
    {
        std::vector<std::wstring> out;
        size_t start = 0;
        while (out.size() + 1 < maxFields)
        {
            size_t sep = line.find(L':', start);
            if (sep == std::wstring::npos)
                break;
            out.push_back(line.substr(start, sep - start));
            start = sep + 1;
        }
        out.push_back(line.substr(start));
        return out;
    }

    std::wstring Trim(const std::wstring& s)
    {
        size_t a = s.find_first_not_of(L" \t\r\n");
        if (a == std::wstring::npos)
            return std::wstring();
        size_t b = s.find_last_not_of(L" \t\r\n");
        return s.substr(a, b - a + 1);
    }
}

bool LoadTopSkyAreas(const std::wstring& path, std::vector<Zone>& out)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return false;

    std::string raw((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::wstring text = Json::Utf8ToWide(raw);

    Zone current;
    bool have = false;

    auto flush = [&]()
    {
        if (have && current.ring.size() >= 3)
        {
            if (current.ring.front().m_Latitude == current.ring.back().m_Latitude
                && current.ring.front().m_Longitude == current.ring.back().m_Longitude)
                current.ring.pop_back();
            if (current.ring.size() >= 3)
                out.push_back(current);
        }
        current = Zone();
        have = false;
    };

    size_t pos = 0;
    while (pos <= text.size())
    {
        size_t eol = text.find(L'\n', pos);
        std::wstring line = Trim(text.substr(pos, (eol == std::wstring::npos) ? eol : eol - pos));
        if (eol == std::wstring::npos)
            pos = text.size() + 1;
        else
            pos = eol + 1;

        if (line.empty() || line[0] == L';')
            continue;

        if (line.compare(0, 5, L"AREA:") == 0)
        {
            flush();
            std::vector<std::wstring> f = SplitFields(line, 3);
            current.id = (f.size() >= 3) ? Trim(f[2]) : std::wstring();
            have = true;
            continue;
        }

        if (!have)
            continue;

        if (line.compare(0, 9, L"CATEGORY:") == 0)
        {
            std::wstring c = ToUpper(Trim(line.substr(9)));
            current.kind = (c == L"P") ? ZoneKind::Prohibited
                : (c == L"D") ? ZoneKind::Danger
                : ZoneKind::Restricted;
        }
        else if (line.compare(0, 7, L"ACTIVE:") == 0)
        {
            current.activation = Trim(line.substr(7));
        }
        else if (line.compare(0, 7, L"LIMITS:") == 0)
        {
            std::vector<std::wstring> f = SplitFields(line, 3);
            if (f.size() >= 3)
            {
                current.lower = ZoneLevelText(_wtoi(Trim(f[1]).c_str()));
                current.upper = ZoneLevelText(_wtoi(Trim(f[2]).c_str()));
            }
        }
        else if (line.compare(0, 6, L"LABEL:") == 0)
        {
            std::vector<std::wstring> f = SplitFields(line, 4);
            if (f.size() >= 4)
            {
                EuroScopePlugIn::CPosition p;
                if (ParsePointString(Trim(f[1]) + L":" + Trim(f[2]), p))
                {
                    current.labelPos = p;
                    current.hasLabelPos = true;
                }
                std::wstring label = Trim(f[3]);
                if (!label.empty() && label != current.id)
                    current.name = label;
            }
        }
        else if (line.compare(0, 9, L"USERTEXT:") == 0)
        {
            std::wstring note = Trim(line.substr(9));
            if (!note.empty())
                current.note = current.note.empty() ? note : current.note + L"\n" + note;
        }
        else if (line.compare(0, 7, L"CIRCLE:") == 0)
        {
            std::vector<std::wstring> f = SplitFields(line, 5);
            if (f.size() >= 4)
            {
                EuroScopePlugIn::CPosition centre;
                double radiusNM = _wtof(f[3].c_str());
                if (radiusNM > 0.0 && ParsePointString(Trim(f[1]) + L":" + Trim(f[2]), centre))
                    RingFromCircle(centre, radiusNM, current.ring);
            }
        }
        else
        {
            EuroScopePlugIn::CPosition p;
            if (ParsePointString(line, p))
                current.ring.push_back(p);
        }
    }

    flush();
    return true;
}

namespace
{
    const size_t kAupMaxBytes = 4 * 1024 * 1024;
    const DWORD  kAupTimeoutMs = 10000;

    bool ParseIsoUtc(const std::wstring& s, time_t& out)
    {
        int y = 0, mo = 0, d = 0, h = 0, mi = 0, sec = 0;
        if (swscanf_s(s.c_str(), L"%4d-%2d-%2dT%2d:%2d:%2d", &y, &mo, &d, &h, &mi, &sec) < 5)
            return false;
        if (y < 1970 || mo < 1 || mo > 12 || d < 1 || d > 31)
            return false;

        tm t = {};
        t.tm_year = y - 1900;
        t.tm_mon = mo - 1;
        t.tm_mday = d;
        t.tm_hour = h;
        t.tm_min = mi;
        t.tm_sec = sec;
        time_t v = _mkgmtime(&t);
        if (v == (time_t)-1)
            return false;
        out = v;
        return true;
    }
}

bool ParseAup(const std::string& body, std::vector<ZoneBooking>& out)
{
    Json::Value root;
    if (!Json::ParseUtf8(body, root) || root.kind != Json::Value::Kind::Object)
        return false;

    const Json::Value* areas = root.Find(L"areas");
    if (areas == NULL || areas->kind != Json::Value::Kind::Array)
        return false;

    for (const Json::Value& e : areas->arr)
    {
        if (e.kind != Json::Value::Kind::Object)
            continue;

        ZoneBooking b;
        if (const Json::Value* v = e.Find(L"name"))
            b.name = ToUpper(v->AsString());
        if (b.name.empty())
            continue;

        if (const Json::Value* v = e.Find(L"minimum_fl"))
            b.minFL = (int)v->AsInt(0);
        if (const Json::Value* v = e.Find(L"maximum_fl"))
            b.maxFL = (int)v->AsInt(999);

        const Json::Value* from = e.Find(L"start_datetime");
        const Json::Value* to = e.Find(L"end_datetime");
        if (from == NULL || to == NULL)
            continue;
        if (!ParseIsoUtc(from->AsString(), b.start) || !ParseIsoUtc(to->AsString(), b.end))
            continue;
        if (b.end < b.start)
            continue;

        out.push_back(std::move(b));
    }
    return true;
}

bool FetchAup(const std::string& url, std::vector<ZoneBooking>& out)
{
    if (url.empty())
        return false;

    std::string body;
    if (!Net::HttpGet(url, body, kAupMaxBytes, kAupTimeoutMs))
        return false;

    std::vector<ZoneBooking> parsed;
    if (!ParseAup(body, parsed))
    {
        Log::Error("aup", "plan " + url + " could not be read (" + std::to_string(body.size())
            + " bytes): " + Log::Snippet(body, 120));
        return false;
    }

    out = std::move(parsed);
    return true;
}

namespace
{
    bool ParseNotamStamp(const std::wstring& s, time_t& out)
    {
        std::wstring d;
        for (wchar_t c : s)
        {
            if (iswdigit(c))
                d += c;
            else if (!d.empty())
                break;
        }
        if (d.size() < 10)
            return false;

        tm t = {};
        t.tm_year = 100 + _wtoi(d.substr(0, 2).c_str());
        t.tm_mon = _wtoi(d.substr(2, 2).c_str()) - 1;
        t.tm_mday = _wtoi(d.substr(4, 2).c_str());
        t.tm_hour = _wtoi(d.substr(6, 2).c_str());
        t.tm_min = _wtoi(d.substr(8, 2).c_str());
        if (t.tm_mon < 0 || t.tm_mon > 11 || t.tm_mday < 1 || t.tm_mday > 31)
            return false;

        time_t v = _mkgmtime(&t);
        if (v == (time_t)-1)
            return false;
        out = v;
        return true;
    }

    std::wstring NotamField(const std::wstring& msg, const wchar_t* key)
    {
        size_t at = msg.find(key);
        if (at == std::wstring::npos)
            return std::wstring();
        at += wcslen(key);

        static const wchar_t* kMarkers[] = { L"A)", L"B)", L"C)", L"D)", L"E)", L"F)", L"G)" };
        size_t end = msg.size();
        for (const wchar_t* m : kMarkers)
        {
            size_t next = msg.find(m, at);
            if (next != std::wstring::npos && next < end)
                end = next;
        }
        return Trim(msg.substr(at, end - at));
    }

    bool ParseNotamLevel(const std::wstring& s, int& out)
    {
        std::wstring u = ToUpper(Trim(s));
        if (u.empty())
            return false;
        if (u.compare(0, 3, L"SFC") == 0 || u.compare(0, 3, L"GND") == 0)
        {
            out = 0;
            return true;
        }
        if (u.compare(0, 3, L"UNL") == 0)
        {
            out = 999;
            return true;
        }
        if (u.compare(0, 2, L"FL") == 0)
        {
            out = _wtoi(u.substr(2).c_str());
            return true;
        }
        return false;
    }

    void CollectDesignators(const std::wstring& msg, std::vector<std::wstring>& out)
    {
        size_t i = 0;
        while (i < msg.size())
        {
            const bool joined = (i > 0 && (iswalnum(msg[i - 1]) || msg[i - 1] == L'/'));
            if (!iswalpha(msg[i]) || joined)
            {
                i++;
                continue;
            }

            size_t j = i, letters = 0;
            while (j < msg.size() && iswalpha(msg[j])) { j++; letters++; }
            size_t k = j, digits = 0;
            while (k < msg.size() && iswdigit(msg[k])) { k++; digits++; }

            const bool trails = (k < msg.size() && (iswalnum(msg[k]) || msg[k] == L'/'));
            if (!trails && letters >= 2 && letters <= 4 && digits >= 1 && digits <= 4)
            {
                std::wstring id = ToUpper(msg.substr(i, k - i));
                if (std::find(out.begin(), out.end(), id) == out.end())
                    out.push_back(id);
            }
            i = (k > i) ? k : i + 1;
        }
    }
}

bool ParseNotams(const std::string& body, std::vector<ZoneBooking>& out)
{
    {
        Json::Value root;
        if (Json::ParseUtf8(body, root) && root.kind == Json::Value::Kind::Object)
            return ParseAup(body, out);
    }

    std::wstring text = Json::Utf8ToWide(body);
    if (text.empty())
        return false;

    std::vector<std::wstring> messages;
    std::wstring current;
    size_t pos = 0;
    while (pos <= text.size())
    {
        size_t eol = text.find(L'\n', pos);
        std::wstring line = Trim(text.substr(pos, (eol == std::wstring::npos) ? eol : eol - pos));
        pos = (eol == std::wstring::npos) ? text.size() + 1 : eol + 1;

        if (line.empty())
        {
            if (!current.empty())
                messages.push_back(current);
            current.clear();
            continue;
        }
        current += current.empty() ? line : (L" " + line);
    }
    if (!current.empty())
        messages.push_back(current);

    const time_t kPermanent = (time_t)2147483647;

    bool any = false;
    for (const std::wstring& msg : messages)
    {
        time_t start = 0, end = 0;
        if (!ParseNotamStamp(NotamField(msg, L"B)"), start))
            continue;

        std::wstring c = ToUpper(NotamField(msg, L"C)"));
        if (c.find(L"PERM") != std::wstring::npos)
            end = kPermanent;
        else if (!ParseNotamStamp(c, end))
            continue;
        if (end < start)
            continue;

        int lower = 0, upper = 999;
        ParseNotamLevel(NotamField(msg, L"F)"), lower);
        ParseNotamLevel(NotamField(msg, L"G)"), upper);

        std::wstring subject = NotamField(msg, L"E)");
        if (subject.empty())
            subject = msg;

        std::vector<std::wstring> ids;
        CollectDesignators(subject, ids);

        for (const std::wstring& id : ids)
        {
            ZoneBooking b;
            b.name = id;
            b.minFL = lower;
            b.maxFL = upper;
            b.start = start;
            b.end = end;
            out.push_back(std::move(b));
            any = true;
        }
    }

    return any;
}

bool FetchNotams(const std::string& source, std::vector<ZoneBooking>& out)
{
    if (source.empty())
        return false;

    std::string body;
    if (source.compare(0, 4, "http") == 0)
    {
        if (!Net::HttpGet(source, body, kAupMaxBytes, kAupTimeoutMs))
            return false;
    }
    else
    {
        std::ifstream f(Json::Utf8ToWide(source), std::ios::binary);
        if (!f)
        {
            Log::Error("notam", "cannot open " + source + " - " + Log::SystemError(GetLastError()));
            return false;
        }
        body.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    }

    std::vector<ZoneBooking> parsed;
    if (!ParseNotams(body, parsed))
    {
        Log::Error("notam", "NOTAMs from " + source + " could not be read (" + std::to_string(body.size())
            + " bytes): " + Log::Snippet(body, 120));
        return false;
    }

    out = std::move(parsed);
    return true;
}


namespace
{
    const ZoneBooking* BookingFor(const std::vector<ZoneBooking>* list,
        const std::wstring& id, time_t nowUtc)
    {
        if (list == NULL || id.empty())
            return NULL;
        for (const ZoneBooking& b : *list)
        {
            if (b.name != id)
                continue;
            if (nowUtc < b.start || nowUtc > b.end)
                continue;
            return &b;
        }
        return NULL;
    }
}

bool ZoneActiveNow(const Zone& zone, const ZoneActivation& what,
    time_t nowUtc, const ZoneBooking** booking)
{
    if (booking != NULL)
        *booking = NULL;

    if (zone.activation.compare(0, 6, L"NOTAM:") == 0)
    {
        size_t colon = zone.activation.find_last_of(L':');
        std::wstring id = ToUpper(zone.activation.substr(colon + 1));

        if (what.notams == NULL)
            return what.showNotamWhenUnknown;

        const ZoneBooking* hit = BookingFor(what.notams, id, nowUtc);
        if (hit == NULL)
            return false;
        if (booking != NULL)
            *booking = hit;
        return true;
    }

    const bool named = (zone.activation.compare(0, 4, L"AUP:") == 0);

    const bool permanent = (zone.activation.empty() || zone.activation == L"1");
    if (!named && !(permanent && zone.kind == ZoneKind::Restricted))
        return true;

    std::wstring id = named ? ToUpper(zone.activation.substr(4)) : std::wstring();
    if (id.empty())
        id = ToUpper(zone.id);
    if (id.empty())
        return true;

    const ZoneBooking* hit = BookingFor(what.aup, id, nowUtc);
    if (hit == NULL)
        return false;
    if (booking != NULL)
        *booking = hit;
    return true;
}

std::wstring Zone::KindLabel() const
{
    switch (kind)
    {
    case ZoneKind::Prohibited: return Tr(L"Запретная зона");
    case ZoneKind::Danger:     return Tr(L"Опасная зона");
    default:                   return Tr(L"Зона ограничения полётов");
    }
}

std::wstring Zone::Title() const
{
    std::wstring t = KindLabel();
    if (!id.empty())
        t += L" " + id;
    if (!name.empty())
        t += L" - " + name;
    return t;
}

std::wstring ZoneLevelText(int fl)
{
    if (fl <= 0)
        return L"GND";
    if (fl >= 999)
        return L"UNL";
    wchar_t buf[8];
    swprintf_s(buf, L"FL%03d", fl);
    return buf;
}

std::wstring Zone::LevelBand() const
{
    if (lower.empty() && upper.empty())
        return std::wstring();
    return (lower.empty() ? L"---" : lower) + L"-" + (upper.empty() ? L"---" : upper);
}

bool ParseZones(const Json::Value& node, std::vector<Zone>& out, bool& enabled)
{
    const Json::Value* items = NULL;

    if (node.kind == Json::Value::Kind::Array)
    {
        items = &node;
    }
    else if (node.kind == Json::Value::Kind::Object)
    {
        if (const Json::Value* v = node.Find(L"Enabled"))
            enabled = v->AsBool(enabled);
        items = node.Find(L"Items");
    }

    if (items == NULL || items->kind != Json::Value::Kind::Array)
        return false;

    for (const Json::Value& item : items->arr)
    {
        Zone zone;
        if (ParseOne(item, zone))
            out.push_back(std::move(zone));
    }
    return true;
}
