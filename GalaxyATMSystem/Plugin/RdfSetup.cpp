#include "pch.h"
#include "Core/Common.h"

using namespace Galaxy;

void CGalaxyATMSystemPlugin::ConfigureRdf()
{
    m_rdf.Stop();
    if (!m_config.RdfEnabled())
    {
        Log::Info("rdf", "switched off in the config");
        return;
    }

    if (m_config.RdfStations().empty())
    {
        Log::Warn("rdf", "no Rdf.Stations in the config - nothing to measure a bearing from");
    }
    else
    {
        std::string stations;
        for (const ::RdfStation& station : m_config.RdfStations())
        {
            stations += (stations.empty() ? "" : ", ") + Log::Utf8(station.id);
            if (station.hasPoint)
                stations += " (point)";
            if (station.controlBearing >= 0)
                stations += " control " + std::to_string(station.controlBearing);
            if (station.variation != 0.0)
                stations += " var " + std::to_string((int)lround(station.variation));
        }
        Log::Info("rdf", std::to_string(m_config.RdfStations().size()) + " stations: " + stations);
    }

    m_rdf.Start(m_config.RdfEndpoint());
}

const RdfStation* CGalaxyATMSystemPlugin::RdfStation() const
{
    if (!m_config.RdfEnabled())
        return NULL;
    return m_config.RdfStationFor(Widen(MyPosition().c_str()));
}
