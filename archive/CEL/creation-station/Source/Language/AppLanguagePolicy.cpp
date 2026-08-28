#include "AppLanguagePolicy.h"

#if CS_ENABLE_SCRIPTING
 #include <llvm/Config/llvm-config.h>
#endif

namespace creation_station::language
{
std::string getLanguageRuntimeSummary()
{
#if CS_ENABLE_SCRIPTING
    return "LLVM-enabled host layer ready for audio-domain scripting (" LLVM_VERSION_STRING ")";
#else
    return "LLVM scripting disabled for this build.";
#endif
}

std::string getAppDomainName()
{
    return "station";
}

bool canRunNodeDomain(const std::string& domainName)
{
    return domainName == "shared"
        || domainName == "audio"
        || domainName == "patch"
        || domainName == "tracker"
        || domainName == "mixer"
        || domainName == "performance";
}
}
