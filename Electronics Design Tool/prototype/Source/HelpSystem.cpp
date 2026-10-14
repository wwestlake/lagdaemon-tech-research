#include "HelpSystem.h"
#include <regex>

namespace djehuti_help {

static std::function<void(const juce::String&)> g_helpOpener;
static std::function<void(const juce::String&)> g_urlOpener;

void setGlobalHelpOpener(std::function<void(const juce::String&)> opener)
{
    g_helpOpener = std::move(opener);
}

void setGlobalUrlOpener(std::function<void(const juce::String&)> opener)
{
    g_urlOpener = std::move(opener);
}

void openContextHelp(const juce::String& topicId)
{
    if (g_helpOpener)
        g_helpOpener(topicId);
}

void openDocumentationUrl(const juce::String& url)
{
    if (g_urlOpener)
        g_urlOpener(url);
}

// ==============================================================================
// LiveWebRouteDiscoveryProvider Implementation
// ==============================================================================

LiveWebRouteDiscoveryProvider::LiveWebRouteDiscoveryProvider(const juce::String& baseUrl)
    : baseUrl_(baseUrl.isNotEmpty() ? baseUrl : TopicRegistry::getBaseDocumentationUrl())
{
}

bool LiveWebRouteDiscoveryProvider::parseRoutesFromContent(const juce::String& content, std::vector<DiscoveredManual>& outManuals)
{
    outManuals.clear();
    const auto stdContent = content.toStdString();

    // 1. Try parsing JSON if content is a JSON object/array
    if (content.trimStart().startsWith("{") || content.trimStart().startsWith("["))
    {
        const auto parsed = juce::JSON::parse(content);
        if (parsed.isArray() || parsed.hasProperty("manuals"))
        {
            auto* manualsArray = parsed.isArray() ? parsed.getArray() : parsed.getProperty("manuals", {}).getArray();
            if (manualsArray != nullptr)
            {
                for (const auto& mVar : *manualsArray)
                {
                    DiscoveredManual manual;
                    manual.slug = mVar.getProperty("slug", {}).toString();
                    manual.title = mVar.getProperty("title", {}).toString();
                    auto* secArray = mVar.getProperty("sections", {}).getArray();
                    if (secArray != nullptr)
                    {
                        for (const auto& sVar : *secArray)
                        {
                            DiscoveredSection sec;
                            sec.manualSlug = manual.slug;
                            sec.sectionSlug = sVar.getProperty("slug", {}).toString();
                            sec.title = sVar.getProperty("title", {}).toString();
                            sec.summary = sVar.getProperty("summary", {}).toString();
                            sec.url = TopicRegistry::getBaseDocumentationUrl() + "/" + sec.manualSlug + "/" + sec.sectionSlug;
                            manual.sections.push_back(sec);
                        }
                    }
                    if (manual.slug.isNotEmpty())
                        outManuals.push_back(manual);
                }
                if (!outManuals.empty())
                    return true;
            }
        }
    }

    // 2. JavaScript bundle regex parser for React SPA
    // Matches {slug:`manual-slug`,title:`Manual Title`,...sections:[{slug:`sec-slug`,title:`Sec Title`...}...]}
    try
    {
        std::regex manualRegex(R"(\{slug:([`'"])([^`'"]+)\1,title:([`'"])([^`'"]+)\3,[^}]*?sections:\[(.*?)\]\})");
        auto manualsBegin = std::sregex_iterator(stdContent.begin(), stdContent.end(), manualRegex);
        auto manualsEnd = std::sregex_iterator();

        for (auto mIt = manualsBegin; mIt != manualsEnd; ++mIt)
        {
            const auto& mMatch = *mIt;
            DiscoveredManual manual;
            manual.slug = juce::String(mMatch[2].str());
            manual.title = juce::String(mMatch[4].str());
            const std::string sectionsBlob = mMatch[5].str();

            std::regex sectionRegex(R"(\{slug:([`'"])([^`'"]+)\1,title:([`'"])([^`'"]+)\3(?:,[^}]*?summary:([`'"])(.*?)\5)?)");
            auto secBegin = std::sregex_iterator(sectionsBlob.begin(), sectionsBlob.end(), sectionRegex);
            auto secEnd = std::sregex_iterator();

            for (auto sIt = secBegin; sIt != secEnd; ++sIt)
            {
                const auto& sMatch = *sIt;
                DiscoveredSection sec;
                sec.manualSlug = manual.slug;
                sec.sectionSlug = juce::String(sMatch[2].str());
                sec.title = juce::String(sMatch[4].str());
                if (sMatch.size() > 6)
                    sec.summary = juce::String(sMatch[6].str());
                sec.url = TopicRegistry::getBaseDocumentationUrl() + "/" + sec.manualSlug + "/" + sec.sectionSlug;
                manual.sections.push_back(sec);
            }

            if (manual.slug.isNotEmpty() && !manual.sections.empty())
                outManuals.push_back(manual);
        }
    }
    catch (const std::exception&)
    {
        return false;
    }

    return !outManuals.empty();
}

bool LiveWebRouteDiscoveryProvider::discoverRoutes(std::vector<DiscoveredManual>& outManuals, juce::String& outVersionOrHash)
{
    // Fetch landing documentation HTML
    juce::URL docUrl(baseUrl_);
    int statusCode = 0;
    auto htmlOptions = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                           .withConnectionTimeoutMs(5000)
                           .withStatusCode(&statusCode);

    std::unique_ptr<juce::InputStream> stream = docUrl.createInputStream(htmlOptions);
    if (stream == nullptr || statusCode < 200 || statusCode >= 400)
        return false;

    const auto html = stream->readEntireStreamAsString();
    if (html.isEmpty())
        return false;

    // Discover the JavaScript bundle path from <script type="module" crossorigin src="/assets/index-....js"></script>
    juce::String scriptPath;
    const auto stdHtml = html.toStdString();
    try
    {
        std::regex scriptRegex(R"(<script[^>]+src=([`'"])(/assets/index-[^`'"]+\.js)\1)");
        std::smatch sm;
        if (std::regex_search(stdHtml, sm, scriptRegex) && sm.size() > 2)
            scriptPath = juce::String(sm[2].str());
    }
    catch (...) {}

    if (scriptPath.isEmpty())
    {
        // Fallback: search for "/assets/index-" in html
        const auto idx = html.indexOf("/assets/index-");
        if (idx >= 0)
        {
            const auto endQuote = html.indexOfAnyOf("\"'> \r\n", idx);
            if (endQuote > idx)
                scriptPath = html.substring(idx, endQuote);
        }
    }

    if (scriptPath.isEmpty())
        return false;

    outVersionOrHash = scriptPath.fromLastOccurrenceOf("/", false, false).upToLastOccurrenceOf(".js", false, false);

    // Fetch the JS bundle
    juce::URL bundleUrl("https://lagdaemon.com" + scriptPath);
    int bundleStatusCode = 0;
    auto bundleOptions = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                             .withConnectionTimeoutMs(10000)
                             .withStatusCode(&bundleStatusCode);

    std::unique_ptr<juce::InputStream> bundleStream = bundleUrl.createInputStream(bundleOptions);
    if (bundleStream == nullptr || bundleStatusCode < 200 || bundleStatusCode >= 400)
        return false;

    const auto bundleContent = bundleStream->readEntireStreamAsString();
    return parseRoutesFromContent(bundleContent, outManuals);
}

// ==============================================================================
// RouteCache Implementation
// ==============================================================================

juce::File RouteCache::getDefaultCacheFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("DjehutiElectronicsLab")
        .getChildFile("help_routes_cache.json");
}

RouteCache::RouteCache(const juce::File& cacheFile)
    : cacheFile_(cacheFile)
{
}

bool RouteCache::loadFromDisk()
{
    if (!cacheFile_.existsAsFile())
        return false;

    const auto text = cacheFile_.loadFileAsString();
    if (text.isEmpty())
        return false;

    const auto parsed = juce::JSON::parse(text);
    if (!parsed.isObject())
        return false;

    versionOrHash_ = parsed.getProperty("bundleHash", {}).toString();

    // Load manuals
    manuals_.clear();
    knownSectionPaths_.clear();
    if (auto* mArray = parsed.getProperty("manuals", {}).getArray())
    {
        for (const auto& mVar : *mArray)
        {
            DiscoveredManual man;
            man.slug = mVar.getProperty("slug", {}).toString();
            man.title = mVar.getProperty("title", {}).toString();
            if (auto* sArray = mVar.getProperty("sections", {}).getArray())
            {
                for (const auto& sVar : *sArray)
                {
                    DiscoveredSection sec;
                    sec.manualSlug = man.slug;
                    sec.sectionSlug = sVar.getProperty("slug", {}).toString();
                    sec.title = sVar.getProperty("title", {}).toString();
                    sec.summary = sVar.getProperty("summary", {}).toString();
                    sec.url = TopicRegistry::getBaseDocumentationUrl() + "/" + sec.manualSlug + "/" + sec.sectionSlug;
                    man.sections.push_back(sec);
                    knownSectionPaths_.insert(sec.manualSlug + "/" + sec.sectionSlug);
                }
            }
            if (man.slug.isNotEmpty())
                manuals_.push_back(man);
        }
    }

    // Load mappings
    mappings_.clear();
    if (auto* mapArray = parsed.getProperty("mappings", {}).getArray())
    {
        for (const auto& item : *mapArray)
        {
            RouteMapping rm;
            rm.topicId = item.getProperty("topicId", {}).toString();
            rm.manualSlug = item.getProperty("manualSlug", {}).toString();
            rm.sectionSlug = item.getProperty("sectionSlug", {}).toString();
            rm.anchor = item.getProperty("anchor", {}).toString();
            rm.title = item.getProperty("title", {}).toString();
            rm.url = item.getProperty("url", {}).toString();
            rm.isValid = (bool)item.getProperty("isValid", true);

            if (rm.topicId.isNotEmpty() && rm.url.isNotEmpty())
                mappings_[rm.topicId] = rm;
        }
    }

    return !mappings_.empty() || !manuals_.empty();
}

bool RouteCache::saveToDisk() const
{
    auto* root = new juce::DynamicObject();
    root->setProperty("version", 1);
    root->setProperty("bundleHash", versionOrHash_);
    root->setProperty("lastUpdated", juce::Time::getCurrentTime().toMilliseconds());

    juce::Array<juce::var> mArray;
    for (const auto& m : manuals_)
    {
        auto* mObj = new juce::DynamicObject();
        mObj->setProperty("slug", m.slug);
        mObj->setProperty("title", m.title);
        juce::Array<juce::var> sArray;
        for (const auto& s : m.sections)
        {
            auto* sObj = new juce::DynamicObject();
            sObj->setProperty("slug", s.sectionSlug);
            sObj->setProperty("title", s.title);
            sObj->setProperty("summary", s.summary);
            sArray.add(juce::var(sObj));
        }
        mObj->setProperty("sections", sArray);
        mArray.add(juce::var(mObj));
    }
    root->setProperty("manuals", mArray);

    juce::Array<juce::var> mapArray;
    for (const auto& pair : mappings_)
    {
        const auto& rm = pair.second;
        auto* rmObj = new juce::DynamicObject();
        rmObj->setProperty("topicId", rm.topicId);
        rmObj->setProperty("manualSlug", rm.manualSlug);
        rmObj->setProperty("sectionSlug", rm.sectionSlug);
        rmObj->setProperty("anchor", rm.anchor);
        rmObj->setProperty("title", rm.title);
        rmObj->setProperty("url", rm.url);
        rmObj->setProperty("isValid", rm.isValid);
        mapArray.add(juce::var(rmObj));
    }
    root->setProperty("mappings", mapArray);

    cacheFile_.getParentDirectory().createDirectory();
    return cacheFile_.replaceWithText(juce::JSON::toString(juce::var(root), true));
}

bool RouteCache::hasRoute(const juce::String& topicId) const
{
    return mappings_.count(topicId) > 0;
}

bool RouteCache::isRouteValid(const juce::String& topicId) const
{
    auto it = mappings_.find(topicId);
    return it != mappings_.end() && it->second.isValid;
}

RouteMapping RouteCache::getRoute(const juce::String& topicId) const
{
    auto it = mappings_.find(topicId);
    if (it != mappings_.end())
        return it->second;
    return {};
}

void RouteCache::putRoute(const RouteMapping& mapping)
{
    mappings_[mapping.topicId] = mapping;
}

void RouteCache::invalidateRoute(const juce::String& topicId)
{
    auto it = mappings_.find(topicId);
    if (it != mappings_.end())
    {
        it->second.isValid = false;
    }
}

bool RouteCache::isSectionKnown(const juce::String& manualSlug, const juce::String& sectionSlug) const
{
    if (knownSectionPaths_.empty())
    {
        for (const auto& m : manuals_)
            for (const auto& s : m.sections)
                const_cast<std::set<juce::String>&>(knownSectionPaths_).insert(s.manualSlug + "/" + s.sectionSlug);
    }
    return knownSectionPaths_.count(manualSlug + "/" + sectionSlug) > 0;
}

void RouteCache::setDiscoveredManuals(const std::vector<DiscoveredManual>& manuals, const juce::String& versionOrHash)
{
    manuals_ = manuals;
    versionOrHash_ = versionOrHash;
    knownSectionPaths_.clear();
    for (const auto& m : manuals_)
        for (const auto& s : m.sections)
            knownSectionPaths_.insert(s.manualSlug + "/" + s.sectionSlug);
}

void RouteCache::clearMappings()
{
    mappings_.clear();
}

// ==============================================================================
// TopicRegistry Implementation
// ==============================================================================

juce::String TopicRegistry::getBaseDocumentationUrl()
{
    return "https://lagdaemon.com/electronics-work-bench/docs";
}

juce::String TopicRegistry::getDefaultOverviewUrl()
{
    return "https://lagdaemon.com/electronics-work-bench/docs/product-overview/overview";
}

TopicRegistry& TopicRegistry::instance()
{
    static TopicRegistry registry;
    return registry;
}

TopicRegistry::TopicRegistry()
{
    discoveryProvider_ = std::make_shared<LiveWebRouteDiscoveryProvider>();
    cache_ = std::make_shared<RouteCache>();

    initDefaultContextHints();

    // Try loading persistent cache from disk
    const bool loaded = cache_->loadFromDisk();
    if (!loaded || cache_->getAllMappings().empty())
    {
        // Seed initial routes from hints so application is never left empty
        associateTopicsWithDiscoveredRoutes();
    }
}

void TopicRegistry::setDiscoveryProvider(std::shared_ptr<IRouteDiscoveryProvider> provider)
{
    discoveryProvider_ = std::move(provider);
}

void TopicRegistry::setCache(std::shared_ptr<RouteCache> cache)
{
    cache_ = std::move(cache);
}

juce::String TopicRegistry::normalizeTopicId(const juce::String& topicId) const
{
    auto cleaned = topicId.trim().toLowerCase();
    cleaned = cleaned.replaceCharacter('_', '-');
    while (cleaned.startsWith("-") || cleaned.startsWith("."))
        cleaned = cleaned.substring(1);
    while (cleaned.endsWith("-") || cleaned.endsWith("."))
        cleaned = cleaned.dropLastCharacters(1);
    return cleaned;
}

void TopicRegistry::registerContextHint(const TopicContextHint& hint)
{
    auto normId = normalizeTopicId(hint.topicId);
    if (normId.isEmpty()) return;
    TopicContextHint h = hint;
    h.topicId = normId;
    contextHints_[normId] = h;
}

bool TopicRegistry::hasContextHint(const juce::String& topicId) const
{
    return contextHints_.count(normalizeTopicId(topicId)) > 0;
}

TopicContextHint TopicRegistry::getContextHint(const juce::String& topicId) const
{
    auto normId = normalizeTopicId(topicId);
    auto it = contextHints_.find(normId);
    if (it != contextHints_.end())
        return it->second;

    TopicContextHint fallback;
    fallback.topicId = normId;
    fallback.defaultManualSlug = "product-overview";
    fallback.defaultSectionSlug = "overview";
    fallback.title = "Product Overview";
    return fallback;
}

bool TopicRegistry::isApprovedDocumentationUrl(const juce::String& url) const
{
    const auto trimmed = url.trim();
    return trimmed.startsWithIgnoreCase("https://lagdaemon.com/electronics-work-bench/docs")
        || trimmed.startsWithIgnoreCase("http://lagdaemon.com/electronics-work-bench/docs")
        || trimmed.startsWithIgnoreCase("https://www.lagdaemon.com/electronics-work-bench/docs")
        || trimmed.startsWithIgnoreCase("http://www.lagdaemon.com/electronics-work-bench/docs");
}

juce::String TopicRegistry::sanitizeDocumentationUrl(const juce::String& url) const
{
    if (isApprovedDocumentationUrl(url))
        return url.trim();
    return getDefaultOverviewUrl();
}

bool TopicRegistry::isKnownValidDestination(const juce::String& url) const
{
    if (!isApprovedDocumentationUrl(url))
        return false;

    // Extract manualSlug and sectionSlug from URL
    // Format: .../docs/<manualSlug>/<sectionSlug>[#anchor]
    auto clean = url.trim();
    auto hashIdx = clean.indexOfChar('#');
    if (hashIdx >= 0)
        clean = clean.substring(0, hashIdx);

    auto base = getBaseDocumentationUrl();
    if (!clean.startsWithIgnoreCase(base))
        return false;

    auto remainder = clean.substring(base.length());
    while (remainder.startsWith("/"))
        remainder = remainder.substring(1);

    if (remainder.isEmpty())
        return true; // Overview landing

    auto slashIdx = remainder.indexOfChar('/');
    if (slashIdx <= 0)
        return false; // Incomplete path

    auto manualSlug = remainder.substring(0, slashIdx);
    auto sectionSlug = remainder.substring(slashIdx + 1);

    if (cache_ != nullptr)
    {
        // If manuals list is empty, treat as not yet checked
        if (cache_->getDiscoveredManuals().empty())
            return true;
        return cache_->isSectionKnown(manualSlug, sectionSlug);
    }
    return true;
}

DiscoveredSection TopicRegistry::findBestSectionMatch(const TopicContextHint& hint) const
{
    DiscoveredSection best;
    if (cache_ == nullptr)
        return best;

    const auto& manuals = cache_->getDiscoveredManuals();
    if (manuals.empty())
        return best;

    double bestScore = -1.0;
    auto tokens = juce::StringArray::fromTokens(hint.keywords + " " + hint.title + " " + hint.topicId, " \t-_/.", "\"");
    tokens.removeEmptyStrings();

    for (const auto& m : manuals)
    {
        for (const auto& s : m.sections)
        {
            double score = 0.0;

            // Exact match bonus
            if (s.sectionSlug == hint.defaultSectionSlug && m.slug == hint.defaultManualSlug)
                score += 1000.0;
            else if (s.sectionSlug == hint.defaultSectionSlug)
                score += 500.0;
            else if (m.slug == hint.defaultManualSlug)
                score += 100.0;

            const auto titleLower = s.title.toLowerCase();
            const auto slugLower = s.sectionSlug.toLowerCase();
            const auto summaryLower = s.summary.toLowerCase();

            for (const auto& token : tokens)
            {
                const auto tokLower = token.toLowerCase();
                if (titleLower.contains(tokLower)) score += 20.0;
                if (slugLower.contains(tokLower)) score += 15.0;
                if (summaryLower.contains(tokLower)) score += 5.0;
            }

            if (score > bestScore)
            {
                bestScore = score;
                best = s;
            }
        }
    }

    return best;
}

void TopicRegistry::associateTopicsWithDiscoveredRoutes()
{
    if (cache_ == nullptr) return;

    for (const auto& pair : contextHints_)
    {
        const auto& hint = pair.second;
        auto matched = findBestSectionMatch(hint);

        RouteMapping mapping;
        mapping.topicId = hint.topicId;
        mapping.anchor = hint.anchor;

        if (matched.manualSlug.isNotEmpty() && matched.sectionSlug.isNotEmpty())
        {
            mapping.manualSlug = matched.manualSlug;
            mapping.sectionSlug = matched.sectionSlug;
            mapping.title = matched.title;
            mapping.url = getBaseDocumentationUrl() + "/" + mapping.manualSlug + "/" + mapping.sectionSlug;
            if (mapping.anchor.isNotEmpty())
                mapping.url += (mapping.anchor.startsWith("#") ? mapping.anchor : ("#" + mapping.anchor));
            mapping.isValid = true;
        }
        else
        {
            // Seed default fallback
            mapping.manualSlug = hint.defaultManualSlug;
            mapping.sectionSlug = hint.defaultSectionSlug;
            mapping.title = hint.title;
            mapping.url = getBaseDocumentationUrl() + "/" + mapping.manualSlug + "/" + mapping.sectionSlug;
            if (mapping.anchor.isNotEmpty())
                mapping.url += (mapping.anchor.startsWith("#") ? mapping.anchor : ("#" + mapping.anchor));
            mapping.isValid = true;
        }

        cache_->putRoute(mapping);
    }
}

bool TopicRegistry::refreshRoutes()
{
    if (discoveryProvider_ == nullptr || cache_ == nullptr)
        return false;

    std::vector<DiscoveredManual> discovered;
    juce::String versionOrHash;

    const bool ok = discoveryProvider_->discoverRoutes(discovered, versionOrHash);
    if (!ok || discovered.empty())
    {
        // Network outage / discovery failure:
        // IMPORTANT per addendum: retain last known working mappings across network outages!
        return false;
    }

    ++discoveryCount_;
    cache_->setDiscoveredManuals(discovered, versionOrHash);
    associateTopicsWithDiscoveredRoutes();
    cache_->saveToDisk();
    return true;
}

juce::String TopicRegistry::resolveTopicUrl(const juce::String& topicId, bool allowRediscovery)
{
    const auto norm = normalizeTopicId(topicId);
    if (norm.isEmpty())
        return getDefaultOverviewUrl();

    // 1. Check cache for valid route
    if (cache_ != nullptr && cache_->isRouteValid(norm))
    {
        return cache_->getRoute(norm).url;
    }

    // 2. If route is missing or invalidated, attempt recovery (limited to 1 attempt to prevent loops)
    if (allowRediscovery)
    {
        if (refreshRoutes())
        {
            if (cache_->isRouteValid(norm))
                return cache_->getRoute(norm).url;
        }
    }

    // 3. Hierarchical parent fallback (e.g. "control-panel.unknown" -> "control-panel")
    auto dotIdx = norm.lastIndexOfChar('.');
    auto dashIdx = norm.lastIndexOfChar('-');
    int lastSep = std::max(dotIdx, dashIdx);
    if (lastSep > 0)
    {
        auto parentId = norm.substring(0, lastSep);
        if (cache_ != nullptr && cache_->isRouteValid(parentId))
            return cache_->getRoute(parentId).url;
    }

    // 4. If cached mapping exists even if marked invalid (e.g. offline fallback), use it
    if (cache_ != nullptr && cache_->hasRoute(norm))
    {
        return cache_->getRoute(norm).url;
    }

    // 5. Default overview page
    return getDefaultOverviewUrl();
}

bool TopicRegistry::reportRouteFailure(const juce::String& topicIdOrUrl, bool isNetworkError)
{
    if (isNetworkError)
    {
        // Per addendum: network outages do NOT erase valid cached mappings!
        return false;
    }

    // Route is missing/obsolete
    juce::String targetTopic;
    if (cache_ != nullptr)
    {
        if (cache_->hasRoute(topicIdOrUrl))
        {
            targetTopic = topicIdOrUrl;
        }
        else
        {
            // Search mapping by URL
            for (const auto& pair : cache_->getAllMappings())
            {
                if (pair.second.url.equalsIgnoreCase(topicIdOrUrl))
                {
                    targetTopic = pair.first;
                    break;
                }
            }
        }
    }

    if (targetTopic.isEmpty())
        targetTopic = normalizeTopicId(topicIdOrUrl);

    if (cache_ != nullptr && targetTopic.isNotEmpty())
        cache_->invalidateRoute(targetTopic);

    // Reinspect website and attempt recovery
    if (refreshRoutes())
    {
        auto recoveredUrl = resolveTopicUrl(targetTopic, false);
        return isKnownValidDestination(recoveredUrl);
    }

    return false;
}

std::vector<DiscoveredManual> TopicRegistry::getDiscoveredManuals() const
{
    if (cache_ != nullptr)
        return cache_->getDiscoveredManuals();
    return {};
}

SearchResult TopicRegistry::search(const juce::String& query) const
{
    SearchResult result;
    const auto q = query.trim().toLowerCase();
    if (q.isEmpty())
    {
        result.found = true;
        result.topicId = "overview";
        result.title = "Product Overview";
        result.url = getDefaultOverviewUrl();
        result.score = 1.0;
        return result;
    }

    // Check exact topic ID first
    const auto normQ = normalizeTopicId(q);
    if (cache_ != nullptr && cache_->hasRoute(normQ))
    {
        const auto r = cache_->getRoute(normQ);
        result.found = true;
        result.topicId = r.topicId;
        result.title = r.title;
        result.url = r.url;
        result.score = 1000.0;
        return result;
    }

    // Search across all discovered sections
    if (cache_ != nullptr)
    {
        double bestScore = 0.0;
        DiscoveredSection bestSec;

        auto tokens = juce::StringArray::fromTokens(q, " \t-_/.", "\"");
        tokens.removeEmptyStrings();

        for (const auto& m : cache_->getDiscoveredManuals())
        {
            for (const auto& s : m.sections)
            {
                double score = 0.0;
                const auto titleLower = s.title.toLowerCase();
                const auto slugLower = s.sectionSlug.toLowerCase();
                const auto summaryLower = s.summary.toLowerCase();

                if (titleLower == q || slugLower == q) score += 500.0;
                else if (titleLower.contains(q)) score += 100.0;
                else if (slugLower.contains(q)) score += 50.0;
                else if (summaryLower.contains(q)) score += 20.0;

                for (const auto& token : tokens)
                {
                    if (titleLower.contains(token)) score += 15.0;
                    if (slugLower.contains(token)) score += 10.0;
                    if (summaryLower.contains(token)) score += 5.0;
                }

                if (score > bestScore)
                {
                    bestScore = score;
                    bestSec = s;
                }
            }
        }

        if (bestScore > 0.0 && bestSec.url.isNotEmpty())
        {
            result.found = true;
            result.topicId = bestSec.sectionSlug;
            result.title = bestSec.title;
            result.url = bestSec.url;
            result.score = bestScore;
            return result;
        }
    }

    result.found = false;
    result.topicId = "overview";
    result.title = "Product Overview";
    result.url = getDefaultOverviewUrl();
    result.score = 0.0;
    return result;
}

void TopicRegistry::initDefaultContextHints()
{
    registerContextHint({ "overview", "product-overview", "overview", "",
                          "Product Overview", "overview intro welcome home about product" });
    registerContextHint({ "product-overview", "product-overview", "overview", "",
                          "Product Overview", "overview intro welcome home about product" });
    registerContextHint({ "getting-started", "installation-and-getting-started", "4-first-launch-orientation", "",
                          "First Launch Orientation", "getting started start tutorial installation setup first launch orientation" });
    registerContextHint({ "troubleshooting", "installation-and-getting-started", "7-troubleshooting", "",
                          "Troubleshooting Guide", "troubleshooting problems issues errors bug fix help diagnostic" });
    registerContextHint({ "user-guide", "user-guide", "2-workbench-orientation", "",
                          "Workbench Orientation", "user guide workbench orientation layout navigation" });

    registerContextHint({ "schematic", "electronics-and-circuit-simulation", "4-schematic-construction", "",
                          "Schematic Construction", "schematic diagram wire component editor place net label bus" });
    registerContextHint({ "schematic-editor", "electronics-and-circuit-simulation", "4-schematic-construction", "",
                          "Schematic Construction", "schematic diagram wire component editor place net label bus" });
    registerContextHint({ "circuit.simulation", "electronics-and-circuit-simulation", "5-simulation-engines", "",
                          "Simulation Engines", "simulation solver engines transient ac dc transient operating point" });
    registerContextHint({ "simulation", "electronics-and-circuit-simulation", "5-simulation-engines", "",
                          "Simulation Engines", "simulation solver engines transient ac dc transient operating point" });
    registerContextHint({ "analytics", "electronics-and-circuit-simulation", "6-circuit-analysis-workflow", "",
                          "Circuit Analysis Workflow", "analytics bode plot fourier fft frequency response transient curve measure" });
    registerContextHint({ "library", "technical-reference", "4-component-reference", "",
                          "Component Reference", "library palette components parts symbols resistor capacitor inductor transistor opamp" });
    registerContextHint({ "hierarchy", "user-guide", "3-projects-and-diagrams", "",
                          "Projects and Diagrams", "hierarchy sheet subcircuit multi-sheet blocks ports" });

    registerContextHint({ "control-panel", "user-guide", "9-machine-builder-and-control-panel", "",
                          "Machine Builder and Control Panel", "control panel operator dashboard widgets gauges buttons sliders controls knobs" });
    registerContextHint({ "control-panel.analog-gauge", "instruments-and-data-acquisition", "8-control-panel-instruments", "#control-panel-instruments",
                          "Control Panel Instruments", "analog gauge dial needle meter readout speed current scale range warn crit" });
    registerContextHint({ "control-panel.properties", "instruments-and-data-acquisition", "8-control-panel-instruments", "",
                          "Control Panel Instruments", "widget properties dialog configuration signal connection binding units min max" });
    registerContextHint({ "control-panel.strip-chart", "instruments-and-data-acquisition", "7-sampling-timebase-and-navigation", "",
                          "Sampling Timebase and Navigation", "strip chart recorder plot waveform timebase history lane scroll zoom" });
    registerContextHint({ "control-panel.signals", "instruments-and-data-acquisition", "6-signal-connections-and-naming", "",
                          "Signal Connections and Naming", "signals naming bus connection binding picker in out node branch" });

    registerContextHint({ "machine-builder", "machine-and-systems-simulation", "3-machine-builder-orientation", "",
                          "Machine Builder Orientation", "machine builder assembly electromechanical motor load mechanics system" });
    registerContextHint({ "machine-builder.design", "machine-and-systems-simulation", "4-machine-design-model", "",
                          "Machine Design Model", "machine design model ports wiring components connections schema" });
    registerContextHint({ "machine-builder.electromechanical", "machine-and-systems-simulation", "6-motors-sensors-and-electromechanical-models", "",
                          "Motors Sensors and Electromechanical Models", "motor dc motor shaft load torque encoder tachometer sensor inertia friction" });
    registerContextHint({ "machine-builder.control", "machine-and-systems-simulation", "7-control-systems", "",
                          "Control Systems", "control systems closed loop pid speed controller feedback setpoint drive" });
    registerContextHint({ "mechanical-view", "machine-and-systems-simulation", "3-machine-builder-orientation", "",
                          "Machine Builder Orientation", "mechanical view 3d scene animation shaft rotation physical representation" });
    registerContextHint({ "demo-runner", "machine-and-systems-simulation", "9-worked-engineering-example", "",
                          "Worked Engineering Example", "demonstration runner test scenario sequence automation acceptance" });

    registerContextHint({ "instruments", "instruments-and-data-acquisition", "3-instrumentation-overview", "",
                          "Instrumentation Overview", "instruments scope meter probe measurement daq capture" });
    registerContextHint({ "instruments.recording", "instruments-and-data-acquisition", "9-recording-export-and-data-analysis", "",
                          "Recording Export and Data Analysis", "recording export csv data logging analysis session history" });

    registerContextHint({ "frust", "frust-programming-guide", "3-frust-architecture-in-djehuti", "",
                          "FRust Architecture in Djehuti", "frust language code programmable component embedded script" });
    registerContextHint({ "frust.language", "frust-language-reference", "1-purpose-and-source-authority", "",
                          "FRust Language Reference", "frust language reference grammar types syntax expressions control flow" });
    registerContextHint({ "frust.components", "frust-programming-guide", "6-host-functions-for-programmable-components", "",
                          "Host Functions for Programmable Components", "programmable component host api pins voltage current read write evaluate" });
    registerContextHint({ "frust.debugging", "frust-programming-guide", "7-debugging-and-diagnostics", "",
                          "Debugging and Diagnostics", "debugging debugger breakpoints step inspect variables call stack diagnostics" });
    registerContextHint({ "nodes", "frust-programming-guide", "4-development-workflows", "",
                          "Development Workflows", "node designer visual programming node graph dataflow blocks" });

    registerContextHint({ "pcb", "user-guide", "8-pcb-workflow", "",
                          "PCB Workflow", "pcb layout footprint tracks traces routing board fabrication gerber" });
    registerContextHint({ "agent", "user-guide", "11-assistant-operation", "",
                          "Assistant Operation", "agent byok assistant ai copilot tools automation prompt" });
    registerContextHint({ "technical-reference", "technical-reference", "3-project-and-file-format-reference", "",
                          "Project and File Format Reference", "technical reference file formats json schema spec specification" });
}

// ==============================================================================
// Embedded Browser Wrapper
// ==============================================================================

class HelpPanel::BrowserWrapper : public juce::WebBrowserComponent
{
public:
    BrowserWrapper(HelpPanel& owner, const Options& options)
        : juce::WebBrowserComponent(options), owner_(owner) {}

    bool pageAboutToLoad(const juce::String& newURL) override
    {
        if (newURL.startsWith("about:") || newURL.startsWith("data:") || newURL.startsWith("blob:") || newURL.isEmpty())
            return true;

        if (TopicRegistry::instance().isApprovedDocumentationUrl(newURL))
            return true;

        // Block navigation to outside domains to preserve embedded doc focus
        return false;
    }

    void pageFinishedLoading(const juce::String& url) override
    {
        owner_.hideNetworkError();
        if (url.isNotEmpty() && !url.startsWith("about:"))
        {
            owner_.currentUrl_ = url;
            owner_.locationLabel_.setText(url, juce::dontSendNotification);
            if (owner_.onNavigated)
                owner_.onNavigated(url);
        }
    }

    bool pageLoadHadNetworkError(const juce::String& errorInfo) override
    {
        owner_.showNetworkError(errorInfo);
        TopicRegistry::instance().reportRouteFailure(owner_.getCurrentUrl(), true);
        return true;
    }

private:
    HelpPanel& owner_;
};

// ==============================================================================
// HelpPanel Implementation
// ==============================================================================

HelpPanel::HelpPanel()
    : currentUrl_(TopicRegistry::getDefaultOverviewUrl()),
      currentTitle_("Product Overview")
{
    backBtn_.setTooltip("Go back one page");
    backBtn_.onClick = [this] { goBack(); };
    addAndMakeVisible(backBtn_);

    forwardBtn_.setTooltip("Go forward one page");
    forwardBtn_.onClick = [this] { goForward(); };
    addAndMakeVisible(forwardBtn_);

    reloadBtn_.setTooltip("Reload current documentation page");
    reloadBtn_.onClick = [this] { reload(); };
    addAndMakeVisible(reloadBtn_);

    homeBtn_.setTooltip("Navigate to Documentation Overview");
    homeBtn_.onClick = [this] { goHome(); };
    addAndMakeVisible(homeBtn_);

    locationLabel_.setText(currentUrl_, juce::dontSendNotification);
    locationLabel_.setFont(juce::Font(12.0f));
    locationLabel_.setColour(juce::Label::backgroundColourId, juce::Colour(0xff141a22));
    locationLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff78dcca));
    locationLabel_.setColour(juce::Label::outlineColourId, juce::Colour(0xff2d3c4d));
    addAndMakeVisible(locationLabel_);

    searchEd_.setTextToShowWhenEmpty("Search docs...", juce::Colour(0xff7a8a99));
    searchEd_.onReturnKey = [this] { search(searchEd_.getText()); };
    addAndMakeVisible(searchEd_);

    searchBtn_.setTooltip("Search documentation");
    searchBtn_.onClick = [this] { search(searchEd_.getText()); };
    addAndMakeVisible(searchBtn_);

    // Error Overlay
    errorTitleLabel_.setText("Documentation Offline / Connection Error", juce::dontSendNotification);
    errorTitleLabel_.setFont(juce::Font(16.0f, juce::Font::bold));
    errorTitleLabel_.setColour(juce::Label::textColourId, juce::Colour(0xffff6b6b));
    errorTitleLabel_.setJustificationType(juce::Justification::centred);
    errorOverlay_.addAndMakeVisible(errorTitleLabel_);

    errorDescLabel_.setText("Unable to load online documentation from lagdaemon.com.\nPlease verify your internet connection and click Retry.", juce::dontSendNotification);
    errorDescLabel_.setFont(juce::Font(13.0f));
    errorDescLabel_.setColour(juce::Label::textColourId, juce::Colour(0xffc5d1de));
    errorDescLabel_.setJustificationType(juce::Justification::centred);
    errorOverlay_.addAndMakeVisible(errorDescLabel_);

    retryBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1b5e20));
    retryBtn_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    retryBtn_.onClick = [this] {
        hideNetworkError();
        reload();
    };
    errorOverlay_.addAndMakeVisible(retryBtn_);

    defaultPageBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2d4253));
    defaultPageBtn_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    defaultPageBtn_.onClick = [this] {
        hideNetworkError();
        goHome();
    };
    errorOverlay_.addAndMakeVisible(defaultPageBtn_);

    errorOverlay_.setVisible(false);
    addChildComponent(errorOverlay_);

    // Embedded Browser
    juce::WebBrowserComponent::Options options;
#if JUCE_USE_WIN_WEBVIEW2
    auto userDataDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                           .getChildFile("DjehutiElectronicsLab")
                           .getChildFile("webview2_data");
    userDataDir.createDirectory();

    auto winOptions = juce::WebBrowserComponent::Options::WinWebView2()
                          .withUserDataFolder(userDataDir);

    auto exeDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
    auto loaderDll = exeDir.getChildFile("WebView2Loader.dll");
    if (loaderDll.existsAsFile())
        winOptions = winOptions.withDLLLocation(loaderDll);

    options = options.withBackend(juce::WebBrowserComponent::Options::Backend::webview2)
                     .withWinWebView2Options(winOptions);
#else
    if (juce::WebBrowserComponent::areOptionsSupported(options.withBackend(juce::WebBrowserComponent::Options::Backend::webview2)))
        options = options.withBackend(juce::WebBrowserComponent::Options::Backend::webview2);
#endif
    options = options.withKeepPageLoadedWhenBrowserIsHidden();

    browser_ = std::make_unique<BrowserWrapper>(*this, options);
    addAndMakeVisible(browser_.get());

    browser_->goToURL(currentUrl_);
}

HelpPanel::~HelpPanel() = default;

void HelpPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff10161d));
}

void HelpPanel::resized()
{
    auto area = getLocalBounds();

    auto topBar = area.removeFromTop(36).reduced(4, 3);
    backBtn_.setBounds(topBar.removeFromLeft(28).reduced(1));
    forwardBtn_.setBounds(topBar.removeFromLeft(28).reduced(1));
    reloadBtn_.setBounds(topBar.removeFromLeft(28).reduced(1));
    topBar.removeFromLeft(4);
    homeBtn_.setBounds(topBar.removeFromLeft(54).reduced(1));
    topBar.removeFromLeft(6);

    searchBtn_.setBounds(topBar.removeFromRight(60).reduced(1));
    topBar.removeFromRight(4);
    searchEd_.setBounds(topBar.removeFromRight(150).reduced(1));
    topBar.removeFromRight(6);

    locationLabel_.setBounds(topBar);

    if (browser_ != nullptr)
        browser_->setBounds(area);

    if (hasError_)
    {
        errorOverlay_.setBounds(area);
        auto overlayBounds = errorOverlay_.getLocalBounds();
        auto centerArea = overlayBounds.withSizeKeepingCentre(420, 200);
        errorTitleLabel_.setBounds(centerArea.removeFromTop(32));
        errorDescLabel_.setBounds(centerArea.removeFromTop(60));
        centerArea.removeFromTop(16);
        auto btnRow = centerArea.removeFromTop(32);
        retryBtn_.setBounds(btnRow.removeFromLeft(160).reduced(2));
        btnRow.removeFromLeft(16);
        defaultPageBtn_.setBounds(btnRow.removeFromLeft(160).reduced(2));
    }
}

void HelpPanel::navigateTo(const juce::String& url)
{
    hideNetworkError();
    auto safeUrl = TopicRegistry::instance().sanitizeDocumentationUrl(url);

    // React SPA routing validation & automatic recovery
    if (!TopicRegistry::instance().isKnownValidDestination(safeUrl))
    {
        const auto target = pendingTopicId_.isNotEmpty() ? pendingTopicId_ : safeUrl;
        if (TopicRegistry::instance().reportRouteFailure(target, false))
        {
            safeUrl = TopicRegistry::instance().resolveTopicUrl(target, false);
        }
    }

    currentUrl_ = safeUrl;
    locationLabel_.setText(currentUrl_, juce::dontSendNotification);
    if (browser_ != nullptr)
        browser_->goToURL(safeUrl);
    if (onNavigated)
        onNavigated(safeUrl);
}

void HelpPanel::openTopic(const juce::String& topicId)
{
    pendingTopicId_ = topicId;
    const auto url = TopicRegistry::instance().resolveTopicUrl(topicId);
    navigateTo(url);
}

void HelpPanel::search(const juce::String& query)
{
    const auto result = TopicRegistry::instance().search(query);
    navigateTo(result.url);
}

void HelpPanel::goBack()
{
    hideNetworkError();
    if (browser_ != nullptr)
        browser_->goBack();
}

void HelpPanel::goForward()
{
    hideNetworkError();
    if (browser_ != nullptr)
        browser_->goForward();
}

void HelpPanel::reload()
{
    hideNetworkError();
    if (browser_ != nullptr)
    {
        if (currentUrl_.isNotEmpty())
            browser_->goToURL(currentUrl_);
        else
            browser_->refresh();
    }
}

void HelpPanel::goHome()
{
    navigateTo(TopicRegistry::getDefaultOverviewUrl());
}

void HelpPanel::showNetworkError(const juce::String&)
{
    hasError_ = true;
    errorOverlay_.setVisible(true);
    errorOverlay_.toFront(false);
    resized();
}

void HelpPanel::hideNetworkError()
{
    if (hasError_)
    {
        hasError_ = false;
        errorOverlay_.setVisible(false);
        resized();
    }
}

// ==============================================================================
// AI Tool Definitions & Execution
// ==============================================================================

const std::vector<ToolSpecText>& helpToolSpecs()
{
    static const std::vector<ToolSpecText> specs = {
        {
            "help_open",
            "Open or activate the integrated Help & Documentation panel.",
            R"({"type":"object","properties":{},"additionalProperties":false})"
        },
        {
            "help_openTopic",
            "Navigate the integrated Help panel to a registered documentation topic (e.g. 'overview', 'control-panel', 'control-panel.analog-gauge', 'machine-builder', 'schematic', 'frust', 'analytics', 'instruments').",
            R"({"type":"object","properties":{"topicId":{"type":"string","description":"Unique identifier of the documentation topic to display."}},"required":["topicId"],"additionalProperties":false})"
        },
        {
            "help_navigate",
            "Navigate the integrated Help panel to an approved documentation URL on https://lagdaemon.com/electronics-work-bench/docs.",
            R"({"type":"object","properties":{"url":{"type":"string","description":"Approved documentation URL starting with https://lagdaemon.com/electronics-work-bench/docs."}},"required":["url"],"additionalProperties":false})"
        },
        {
            "help_search",
            "Search the documentation for a query and display the best matching section in the integrated Help panel.",
            R"({"type":"object","properties":{"query":{"type":"string","description":"Search query or keywords to look up in the documentation."}},"required":["query"],"additionalProperties":false})"
        }
    };
    return specs;
}

juce::String executeHelpTool(const juce::String& toolName, const juce::var& arguments,
                             const std::function<void(const juce::String& topicId)>& openTopicFn,
                             const std::function<void(const juce::String& url)>& navigateUrlFn)
{
    const auto name = toolName.replaceCharacter('.', '_');

    if (name == "help_open")
    {
        if (openTopicFn)
            openTopicFn("overview");
        auto* root = new juce::DynamicObject();
        root->setProperty("ok", true);
        root->setProperty("tool", "help_open");
        root->setProperty("displayTool", "help.open");
        root->setProperty("url", TopicRegistry::getDefaultOverviewUrl());
        root->setProperty("panelOpen", true);
        return juce::JSON::toString(juce::var(root));
    }

    if (name == "help_openTopic")
    {
        const auto topicId = arguments.getProperty("topicId", arguments.getProperty("topic", {})).toString().trim();
        if (topicId.isEmpty())
            return R"({"ok": false, "error": "'topicId' is required."})";

        const auto resolvedUrl = TopicRegistry::instance().resolveTopicUrl(topicId);

        if (openTopicFn)
            openTopicFn(topicId);

        auto* root = new juce::DynamicObject();
        root->setProperty("ok", true);
        root->setProperty("tool", "help_openTopic");
        root->setProperty("displayTool", "help.openTopic");
        root->setProperty("topicId", topicId);
        root->setProperty("url", resolvedUrl);
        return juce::JSON::toString(juce::var(root));
    }

    if (name == "help_navigate")
    {
        const auto url = arguments.getProperty("url", arguments.getProperty("destination", {})).toString().trim();
        if (url.isEmpty())
            return R"({"ok": false, "error": "'url' is required."})";

        if (!TopicRegistry::instance().isApprovedDocumentationUrl(url))
        {
            auto* root = new juce::DynamicObject();
            root->setProperty("ok", false);
            root->setProperty("tool", "help_navigate");
            root->setProperty("error", "URL '" + url + "' is not an approved LagDaemon documentation URL. Navigation is restricted to " + TopicRegistry::getBaseDocumentationUrl());
            return juce::JSON::toString(juce::var(root));
        }

        if (navigateUrlFn)
            navigateUrlFn(url);

        auto* root = new juce::DynamicObject();
        root->setProperty("ok", true);
        root->setProperty("tool", "help_navigate");
        root->setProperty("displayTool", "help.navigate");
        root->setProperty("url", url);
        return juce::JSON::toString(juce::var(root));
    }

    if (name == "help_search")
    {
        const auto query = arguments.getProperty("query", arguments.getProperty("q", {})).toString().trim();
        if (query.isEmpty())
            return R"({"ok": false, "error": "'query' is required."})";

        const auto searchRes = TopicRegistry::instance().search(query);

        if (navigateUrlFn)
            navigateUrlFn(searchRes.url);

        auto* root = new juce::DynamicObject();
        root->setProperty("ok", true);
        root->setProperty("tool", "help_search");
        root->setProperty("displayTool", "help.search");
        root->setProperty("query", query);
        root->setProperty("matchedTopic", searchRes.topicId);
        root->setProperty("title", searchRes.title);
        root->setProperty("url", searchRes.url);
        root->setProperty("found", searchRes.found);
        return juce::JSON::toString(juce::var(root));
    }

    return "{ \"ok\": false, \"error\": \"Unknown help tool: " + toolName + "\" }";
}

} // namespace djehuti_help
