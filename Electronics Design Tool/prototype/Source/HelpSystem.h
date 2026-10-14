#pragma once

#include <JuceHeader.h>
#include <map>
#include <set>
#include <vector>
#include <memory>
#include <functional>

namespace djehuti_help {

// ==============================================================================
// Discovered Documentation Data Structures
// ==============================================================================

struct DiscoveredSection
{
    juce::String manualSlug;
    juce::String sectionSlug;
    juce::String title;
    juce::String summary;
    juce::String url;
};

struct DiscoveredManual
{
    juce::String slug;
    juce::String title;
    std::vector<DiscoveredSection> sections;
};

struct RouteMapping
{
    juce::String topicId;
    juce::String manualSlug;
    juce::String sectionSlug;
    juce::String anchor;
    juce::String title;
    juce::String url;
    bool isValid = true;
};

struct TopicContextHint
{
    juce::String topicId;
    juce::String defaultManualSlug;
    juce::String defaultSectionSlug;
    juce::String anchor;
    juce::String title;
    juce::String keywords;
};

struct SearchResult
{
    bool found = false;
    juce::String topicId;
    juce::String title;
    juce::String url;
    double score = 0.0;
};

// ==============================================================================
// Route Discovery Provider Interface
// ==============================================================================

class IRouteDiscoveryProvider
{
public:
    virtual ~IRouteDiscoveryProvider() = default;

    // Discovers manuals and sections from live website, bundle, or mock data.
    // Returns true on success, false on failure (e.g. network outage).
    virtual bool discoverRoutes(std::vector<DiscoveredManual>& outManuals, juce::String& outVersionOrHash) = 0;
};

// Live web discovery implementation that inspects the LagDaemon website / JS bundle
class LiveWebRouteDiscoveryProvider : public IRouteDiscoveryProvider
{
public:
    LiveWebRouteDiscoveryProvider(const juce::String& baseUrl = "https://lagdaemon.com/electronics-work-bench/docs");

    bool discoverRoutes(std::vector<DiscoveredManual>& outManuals, juce::String& outVersionOrHash) override;

    // Parses manuals and sections from raw JavaScript bundle content or JSON metadata
    static bool parseRoutesFromContent(const juce::String& content, std::vector<DiscoveredManual>& outManuals);

private:
    juce::String baseUrl_;
};

// ==============================================================================
// Route Cache & Persistence
// ==============================================================================

class RouteCache
{
public:
    explicit RouteCache(const juce::File& cacheFile = getDefaultCacheFile());

    static juce::File getDefaultCacheFile();

    bool loadFromDisk();
    bool saveToDisk() const;

    bool hasRoute(const juce::String& topicId) const;
    bool isRouteValid(const juce::String& topicId) const;
    RouteMapping getRoute(const juce::String& topicId) const;

    void putRoute(const RouteMapping& mapping);
    void invalidateRoute(const juce::String& topicId);

    bool isSectionKnown(const juce::String& manualSlug, const juce::String& sectionSlug) const;
    const std::vector<DiscoveredManual>& getDiscoveredManuals() const { return manuals_; }
    void setDiscoveredManuals(const std::vector<DiscoveredManual>& manuals, const juce::String& versionOrHash);

    juce::String getVersionOrHash() const { return versionOrHash_; }
    const std::map<juce::String, RouteMapping>& getAllMappings() const { return mappings_; }

    void clearMappings();

private:
    juce::File cacheFile_;
    juce::String versionOrHash_;
    std::map<juce::String, RouteMapping> mappings_;
    std::vector<DiscoveredManual> manuals_;
    std::set<juce::String> knownSectionPaths_;
};

// ==============================================================================
// Centralized Help Topic Registry
// ==============================================================================

class TopicRegistry
{
public:
    TopicRegistry();

    static TopicRegistry& instance();

    static juce::String getBaseDocumentationUrl();
    static juce::String getDefaultOverviewUrl();

    void setDiscoveryProvider(std::shared_ptr<IRouteDiscoveryProvider> provider);
    void setCache(std::shared_ptr<RouteCache> cache);

    std::shared_ptr<RouteCache> getCache() const { return cache_; }
    std::shared_ptr<IRouteDiscoveryProvider> getDiscoveryProvider() const { return discoveryProvider_; }

    // Register an application help context hint
    void registerContextHint(const TopicContextHint& hint);
    bool hasContextHint(const juce::String& topicId) const;
    TopicContextHint getContextHint(const juce::String& topicId) const;

    // Dynamic resolution of a topic to its approved documentation URL
    juce::String resolveTopicUrl(const juce::String& topicId, bool allowRediscovery = true);

    // Invalidation and recovery reporting
    // Returns true if a new route was discovered and resolved
    bool reportRouteFailure(const juce::String& topicIdOrUrl, bool isNetworkError);

    // React SPA routing validation
    bool isKnownValidDestination(const juce::String& url) const;

    // Search across discovered documentation
    SearchResult search(const juce::String& query) const;

    // URL security and validation
    bool isApprovedDocumentationUrl(const juce::String& url) const;
    juce::String sanitizeDocumentationUrl(const juce::String& url) const;

    juce::String normalizeTopicId(const juce::String& topicId) const;

    // Route refresh / discovery
    bool refreshRoutes();
    int getDiscoveryCount() const { return discoveryCount_; }

    std::vector<DiscoveredManual> getDiscoveredManuals() const;

private:
    void initDefaultContextHints();
    void associateTopicsWithDiscoveredRoutes();
    DiscoveredSection findBestSectionMatch(const TopicContextHint& hint) const;

    std::shared_ptr<IRouteDiscoveryProvider> discoveryProvider_;
    std::shared_ptr<RouteCache> cache_;
    std::map<juce::String, TopicContextHint> contextHints_;
    int discoveryCount_ = 0;
};

// ==============================================================================
// Embedded Help Browser & Dockable Panel
// ==============================================================================

class HelpPanel : public juce::Component
{
public:
    HelpPanel();
    ~HelpPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void navigateTo(const juce::String& url);
    void openTopic(const juce::String& topicId);
    void search(const juce::String& query);

    void goBack();
    void goForward();
    void reload();
    void goHome();

    juce::String getCurrentUrl() const { return currentUrl_; }
    juce::String getCurrentTitle() const { return currentTitle_; }

    void showNetworkError(const juce::String& errorInfo);
    void hideNetworkError();

    std::function<void(const juce::String& url)> onNavigated;

private:
    class BrowserWrapper;
    std::unique_ptr<BrowserWrapper> browser_;

    juce::TextButton backBtn_ { "<" };
    juce::TextButton forwardBtn_ { ">" };
    juce::TextButton reloadBtn_ { "↻" };
    juce::TextButton homeBtn_ { "Home" };
    juce::Label locationLabel_;
    juce::TextEditor searchEd_;
    juce::TextButton searchBtn_ { "Search" };

    // Offline / Error Overlay
    juce::Component errorOverlay_;
    juce::Label errorTitleLabel_;
    juce::Label errorDescLabel_;
    juce::TextButton retryBtn_ { "Retry Connection" };
    juce::TextButton defaultPageBtn_ { "Load Overview" };
    bool hasError_ = false;

    juce::String currentUrl_;
    juce::String currentTitle_;
    juce::String pendingTopicId_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HelpPanel)
};

// ==============================================================================
// Global Context-Sensitive Help Dispatch
// ==============================================================================

void setGlobalHelpOpener(std::function<void(const juce::String& topicId)> opener);
void setGlobalUrlOpener(std::function<void(const juce::String& url)> opener);
void openContextHelp(const juce::String& topicId = {});
void openDocumentationUrl(const juce::String& url);

// ==============================================================================
// AI Tool Definitions & Execution
// ==============================================================================

struct ToolSpecText
{
    juce::String name;
    juce::String description;
    juce::String schema;
};

const std::vector<ToolSpecText>& helpToolSpecs();

juce::String executeHelpTool(const juce::String& toolName, const juce::var& arguments,
                             const std::function<void(const juce::String& topicId)>& openTopicFn,
                             const std::function<void(const juce::String& url)>& navigateUrlFn);

} // namespace djehuti_help
