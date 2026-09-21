#pragma once

#include <string>

struct VatsimIdentity
{
    std::string  callsign;
    std::wstring cid;
    std::wstring name;

    std::wstring registeredName;

    bool registeredTable = false;

    bool Empty() const { return cid.empty(); }
};

bool FetchVatsimIdentity(const std::string& callsign, VatsimIdentity& out);

bool ParseVatsimIdentity(const std::string& body, const std::string& callsign, VatsimIdentity& out);

bool FetchRegisteredName(const std::string& baseUrl, const std::string& apiKey,
    const std::string& position, std::wstring& name, bool& hasTable);

bool SubmitLogin(const std::string& baseUrl, const std::string& apiKey, const std::string& position,
    const std::wstring& cid, const std::wstring& surname, const std::wstring& firstName,
    const std::wstring& patronymic, std::wstring& name, std::string& error);

std::wstring RussianShortName(const std::wstring& fullName);
