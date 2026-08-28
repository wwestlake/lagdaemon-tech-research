#pragma once

#include <string>

namespace creation_station::language
{
std::string getLanguageRuntimeSummary();
std::string getAppDomainName();
bool canRunNodeDomain(const std::string& domainName);
}
