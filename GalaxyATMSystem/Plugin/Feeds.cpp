#include "pch.h"
#include "Core/Common.h"

using namespace Galaxy;

std::string CGalaxyATMSystemPlugin::AirportIcao() const
{
    std::string icao;
    for (wchar_t c : m_config.Airport())
    {
        if (icao.size() >= 4)
            break;
        if (iswalnum(c))
            icao += (char)towupper(c);
    }
    return (icao.size() == 4) ? icao : std::string();
}

std::wstring CGalaxyATMSystemPlugin::AtisIndex(const std::string& icao) const
{
    const std::string airport = icao.empty() ? AirportIcao() : icao;
    const bool home = airport == AirportIcao();
    std::lock_guard<std::mutex> lock(m_atisMutex);
    auto live = m_atisLive.find(airport);
    if (live != m_atisLive.end() && !live->second.letter.empty())
        return live->second.letter;
    return home ? m_config.AtisIndex() : std::wstring();
}

std::wstring CGalaxyATMSystemPlugin::AtisMessage(const std::string& icao) const
{
    const std::string airport = icao.empty() ? AirportIcao() : icao;
    const bool home = airport == AirportIcao();
    std::lock_guard<std::mutex> lock(m_atisMutex);
    auto live = m_atisLive.find(airport);
    if (live != m_atisLive.end() && !live->second.text.empty())
        return live->second.text;
    return home ? m_config.AtisMessage() : std::wstring();
}

std::vector<std::string> CGalaxyATMSystemPlugin::AtisAirportsOnAir() const
{
    const std::string home = AirportIcao();
    std::vector<std::string> out;
    if (!home.empty())
        out.push_back(home);
    std::lock_guard<std::mutex> lock(m_atisMutex);
    for (const std::wstring& airport : m_config.AtisAirports())
    {
        const std::string icao = Narrow(airport);
        if (icao != home && m_atisLive.count(icao) != 0
            && std::find(out.begin(), out.end(), icao) == out.end())
            out.push_back(icao);
    }
    for (const auto& live : m_atisLive)
        if (live.first != home && std::find(out.begin(), out.end(), live.first) == out.end())
            out.push_back(live.first);
    return out;
}

std::shared_ptr<const std::vector<ZoneBooking>> CGalaxyATMSystemPlugin::AupBookings() const
{
    std::lock_guard<std::mutex> lock(m_aupMutex);
    return m_aup;
}

void CGalaxyATMSystemPlugin::StartNotamFetch()
{
    std::string source = m_config.NotamSource();
    if (source.empty())
        return;

    m_notamFetch.Start([this, source]()
        {
            std::vector<ZoneBooking> fetched;
            if (!FetchNotams(source, fetched))
                return;

            auto list = std::make_shared<const std::vector<ZoneBooking>>(std::move(fetched));
            std::lock_guard<std::mutex> lock(m_notamMutex);
            m_notams = list;
        });
}

std::shared_ptr<const std::vector<ZoneBooking>> CGalaxyATMSystemPlugin::Notams() const
{
    std::lock_guard<std::mutex> lock(m_notamMutex);
    return m_notams;
}

void CGalaxyATMSystemPlugin::StartAupFetch()
{
    std::string url = m_config.AupUrl();
    if (url.empty())
        return;

    m_aupFetch.Start([this, url]()
        {
            std::vector<ZoneBooking> fetched;
            if (!FetchAup(url, fetched))
                return;

            auto list = std::make_shared<const std::vector<ZoneBooking>>(std::move(fetched));
            std::lock_guard<std::mutex> lock(m_aupMutex);
            m_aup = list;
        });
}

void CGalaxyATMSystemPlugin::StartAtisFetch()
{
    if (!m_config.AtisLive())
        return;

    std::vector<std::string> airports;
    std::set<std::string> listed;
    auto add = [&](const std::string& icao)
    {
        if (icao.size() == 4 && listed.insert(icao).second)
            airports.push_back(icao);
    };
    add(AirportIcao());
    for (const std::wstring& airport : m_config.AtisAirports())
        add(Narrow(airport));
    for (CSectorElement e = SectorFileElementSelectFirst(SECTOR_ELEMENT_AIRPORT); e.IsValid();
         e = SectorFileElementSelectNext(e, SECTOR_ELEMENT_AIRPORT))
    {
        const char* name = e.GetName();
        std::string icao;
        for (const char* p = name; p != NULL && *p != '\0' && icao.size() < 5; p++)
            icao += (char)toupper((unsigned char)*p);
        add(icao);
    }
    if (airports.empty())
        return;

    m_atisFetch.Start([this, airports]()
        {
            std::map<std::string, AtisReport> reports;
            if (FetchVatsimAtis(airports, reports))
            {
                std::lock_guard<std::mutex> lock(m_atisMutex);
                m_atisLive = reports;
            }
        });
}

std::shared_ptr<const std::vector<Sigmet>> CGalaxyATMSystemPlugin::Sigmets() const
{
    std::lock_guard<std::mutex> lock(m_sigmetMutex);
    return m_sigmets;
}

void CGalaxyATMSystemPlugin::StartSigmetFetch()
{
    if (!m_config.SigmetsEnabled())
        return;

    std::vector<std::wstring> firs = m_config.SigmetFirs();
    m_sigmetFetch.Start([this, firs]()
        {
            std::vector<Sigmet> fetched;
            if (!FetchSigmets(firs, fetched))
                return;

            auto list = std::make_shared<const std::vector<Sigmet>>(std::move(fetched));
            std::lock_guard<std::mutex> lock(m_sigmetMutex);
            m_sigmets = list;
        });
}

void CGalaxyATMSystemPlugin::OnNewMetarReceived(const char* sStation, const char* sFullMetar)
{
    if (sStation == NULL || sFullMetar == NULL)
        return;

    char cfgAirport[16] = { 0 };
    WideCharToMultiByte(CP_ACP, 0, m_config.Airport().c_str(), -1, cfgAirport, sizeof(cfgAirport) - 1, NULL, NULL);
    if (_stricmp(sStation, cfgAirport) != 0)
        return;

    int hpa = ParseQnhHpa(sFullMetar);
    if (hpa <= 0)
        return;

    m_gotLiveMetar = true;
    ApplyQnhHpa(hpa);
}

void CGalaxyATMSystemPlugin::ApplyQnhHpa(int hpa)
{
    wchar_t buf[16];
    swprintf_s(buf, L"%d", hpa);
    m_qnhHpa = buf;
    swprintf_s(buf, L"%d", (int)lround(hpa * 0.750062));
    m_qnhMmHg = buf;
}

void CGalaxyATMSystemPlugin::StartMetarFetch()
{
    if (m_gotLiveMetar)
        return;

    std::string station = AirportIcao();
    if (station.empty())
        return;

    m_metarFetch.Start([this, station]()
        {
            std::string body;
            if (!Net::HttpGet("https://metar.vatsim.net/metar.php?id=" + station, body, 4096))
                return;
            int hpa = ParseQnhHpa(body);
            if (hpa > 0)
                m_fetchedQnhHpa.store(hpa);
            else
                Log::Error("metar", "no QNH in the METAR for " + station + ": " + Log::Snippet(body, 120));
        });
}
