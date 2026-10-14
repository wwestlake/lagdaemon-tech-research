#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "HelpSystem.h"
#include <iostream>
#include <cassert>

namespace {

int totalChecks = 0;
int passedChecks = 0;

#define CHECK(condition, message) \
    do { \
        ++totalChecks; \
        if (condition) { \
            ++passedChecks; \
        } else { \
            std::cerr << "FAILED: " << message << " (" << #condition << ")" << std::endl; \
        } \
    } while (0)

#define CHECK_EQUAL(actual, expected, message) \
    do { \
        ++totalChecks; \
        if ((actual) == (expected)) { \
            ++passedChecks; \
        } else { \
            std::cerr << "FAILED: " << message << " [Expected: " << (expected) << ", Actual: " << (actual) << "]" << std::endl; \
        } \
    } while (0)

class MockRouteProvider : public djehuti_help::IRouteDiscoveryProvider
{
public:
    std::vector<djehuti_help::DiscoveredManual> mockManuals;
    juce::String mockVersion = "bundle-v1";
    bool shouldFail = false;
    int callCount = 0;

    bool discoverRoutes(std::vector<djehuti_help::DiscoveredManual>& outManuals, juce::String& outVersionOrHash) override
    {
        ++callCount;
        if (shouldFail)
            return false;
        outManuals = mockManuals;
        outVersionOrHash = mockVersion;
        return true;
    }
};

std::vector<djehuti_help::DiscoveredManual> buildInitialMockManuals()
{
    std::vector<djehuti_help::DiscoveredManual> manuals;

    // 1. product-overview
    djehuti_help::DiscoveredManual m1;
    m1.slug = "product-overview";
    m1.title = "Djehuti Electronics Lab Product Overview";
    m1.sections = {
        { "product-overview", "overview", "Product Overview", "General introduction and overview", "https://lagdaemon.com/electronics-work-bench/docs/product-overview/overview" },
        { "product-overview", "1-introduction", "Introduction", "Introduction details", "https://lagdaemon.com/electronics-work-bench/docs/product-overview/1-introduction" }
    };
    manuals.push_back(m1);

    // 2. instruments-and-data-acquisition
    djehuti_help::DiscoveredManual m2;
    m2.slug = "instruments-and-data-acquisition";
    m2.title = "Instruments and Data Acquisition";
    m2.sections = {
        { "instruments-and-data-acquisition", "3-instrumentation-overview", "Instrumentation Overview", "Scopes and meters", "https://lagdaemon.com/electronics-work-bench/docs/instruments-and-data-acquisition/3-instrumentation-overview" },
        { "instruments-and-data-acquisition", "6-signal-connections-and-naming", "Signal Connections and Naming", "Signal bus", "https://lagdaemon.com/electronics-work-bench/docs/instruments-and-data-acquisition/6-signal-connections-and-naming" },
        { "instruments-and-data-acquisition", "7-sampling-timebase-and-navigation", "Sampling Timebase and Navigation", "Strip chart recorder", "https://lagdaemon.com/electronics-work-bench/docs/instruments-and-data-acquisition/7-sampling-timebase-and-navigation" },
        { "instruments-and-data-acquisition", "8-control-panel-instruments", "Control Panel Instruments", "Analog gauge needle and digital readouts", "https://lagdaemon.com/electronics-work-bench/docs/instruments-and-data-acquisition/8-control-panel-instruments" }
    };
    manuals.push_back(m2);

    // 3. machine-and-systems-simulation
    djehuti_help::DiscoveredManual m3;
    m3.slug = "machine-and-systems-simulation";
    m3.title = "Machine and Systems Simulation";
    m3.sections = {
        { "machine-and-systems-simulation", "3-machine-builder-orientation", "Machine Builder Orientation", "Graphical assembly", "https://lagdaemon.com/electronics-work-bench/docs/machine-and-systems-simulation/3-machine-builder-orientation" },
        { "machine-and-systems-simulation", "6-motors-sensors-and-electromechanical-models", "Motors Sensors and Electromechanical Models", "DC motor and encoder", "https://lagdaemon.com/electronics-work-bench/docs/machine-and-systems-simulation/6-motors-sensors-and-electromechanical-models" },
        { "machine-and-systems-simulation", "7-control-systems", "Control Systems", "PID and speed control", "https://lagdaemon.com/electronics-work-bench/docs/machine-and-systems-simulation/7-control-systems" }
    };
    manuals.push_back(m3);

    return manuals;
}

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::cout << "=== Running Djehuti Help System Tests ===" << std::endl;

    auto testCacheFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("djehuti_test_help_cache_" + juce::String(juce::Random::getSystemRandom().nextInt()) + ".json");
    testCacheFile.deleteFile();

    auto mockProvider = std::make_shared<MockRouteProvider>();
    mockProvider->mockManuals = buildInitialMockManuals();

    auto cache = std::make_shared<djehuti_help::RouteCache>(testCacheFile);
    auto& registry = djehuti_help::TopicRegistry::instance();
    registry.setDiscoveryProvider(mockProvider);
    registry.setCache(cache);

    // -------------------------------------------------------------------------
    // TEST 1: Initial Discovery & Caching
    // -------------------------------------------------------------------------
    std::cout << "-> Test 1: Route discovery populates cache" << std::endl;
    CHECK(registry.refreshRoutes(), "refreshRoutes succeeds with mock provider");
    CHECK_EQUAL(mockProvider->callCount, 1, "Provider called once for initial discovery");
    CHECK(cache->hasRoute("control-panel.analog-gauge"), "Cache has analog gauge topic");
    CHECK(cache->isRouteValid("control-panel.analog-gauge"), "Analog gauge route is valid");

    // -------------------------------------------------------------------------
    // TEST 2: Case 1 — Cached route opens successfully without rediscovery
    // -------------------------------------------------------------------------
    std::cout << "-> Test 2: Case 1 — Cached route opens without rediscovery" << std::endl;
    const int callsBefore = mockProvider->callCount;
    auto gaugeUrl = registry.resolveTopicUrl("control-panel.analog-gauge");
    CHECK(gaugeUrl.contains("8-control-panel-instruments"), "Analog gauge URL points to 8-control-panel-instruments");
    CHECK_EQUAL(mockProvider->callCount, callsBefore, "No rediscovery performed when resolving valid cached route");

    // -------------------------------------------------------------------------
    // TEST 3: Case 2 & 3 — Route change on website, detection of obsolete route,
    //         rediscovery, and opening replacement
    // -------------------------------------------------------------------------
    std::cout << "-> Test 3: Cases 2 & 3 — Obsolete route detection and recovery" << std::endl;
    // Simulate website route change: "7-control-systems" is replaced by "7-closed-loop-control-and-pid"
    for (auto& m : mockProvider->mockManuals)
    {
        if (m.slug == "machine-and-systems-simulation")
        {
            for (auto& s : m.sections)
            {
                if (s.sectionSlug == "7-control-systems")
                {
                    s.sectionSlug = "7-closed-loop-control-and-pid";
                    s.title = "Closed Loop Control and PID Systems";
                    s.summary = "PID speed control systems";
                    s.url = "https://lagdaemon.com/electronics-work-bench/docs/machine-and-systems-simulation/7-closed-loop-control-and-pid";
                }
            }
        }
    }
    mockProvider->mockVersion = "bundle-v2";

    // Report failure for the obsolete destination
    const int callsBeforeRecovery = mockProvider->callCount;
    bool recovered = registry.reportRouteFailure("machine-builder.control", false);
    CHECK(recovered, "Route failure reported and recovered via rediscovery");
    CHECK(mockProvider->callCount > callsBeforeRecovery, "Discovery provider was called to refresh routes");

    auto updatedControlUrl = registry.resolveTopicUrl("machine-builder.control");
    CHECK(updatedControlUrl.contains("7-closed-loop-control-and-pid"),
          "Updated route resolved to new section '7-closed-loop-control-and-pid'");

    // -------------------------------------------------------------------------
    // TEST 4: Case 4 — Corrected mapping is cached and persisted
    // -------------------------------------------------------------------------
    std::cout << "-> Test 4: Case 4 — Corrected mapping is cached and persisted" << std::endl;
    CHECK(cache->isRouteValid("machine-builder.control"), "Corrected mapping is valid in cache");
    const int callsAfterRecovery = mockProvider->callCount;
    auto cachedUpdatedUrl = registry.resolveTopicUrl("machine-builder.control");
    CHECK_EQUAL(cachedUpdatedUrl, updatedControlUrl, "Resolved URL matches updated URL");
    CHECK_EQUAL(mockProvider->callCount, callsAfterRecovery, "Subsequent lookup uses cache without network discovery");

    // Verify disk persistence
    CHECK(cache->saveToDisk(), "Cache saved to disk");
    auto diskCache = std::make_shared<djehuti_help::RouteCache>(testCacheFile);
    CHECK(diskCache->loadFromDisk(), "Cache reloads from disk file");
    CHECK(diskCache->isRouteValid("machine-builder.control"), "Persisted cache has valid updated route");
    CHECK_EQUAL(diskCache->getRoute("machine-builder.control").url, updatedControlUrl, "Persisted URL matches updated URL");

    // -------------------------------------------------------------------------
    // TEST 5: Case 5 — Temporary network outage does not erase valid cached mappings
    // -------------------------------------------------------------------------
    std::cout << "-> Test 5: Case 5 — Network outage preserves valid cached mappings" << std::endl;
    mockProvider->shouldFail = true;
    auto cachedGaugeBefore = registry.resolveTopicUrl("control-panel.analog-gauge");

    // Simulate network error
    registry.reportRouteFailure("control-panel.analog-gauge", true);
    CHECK(cache->isRouteValid("control-panel.analog-gauge"), "Route remains valid in cache during network failure");
    auto cachedGaugeAfter = registry.resolveTopicUrl("control-panel.analog-gauge");
    CHECK_EQUAL(cachedGaugeAfter, cachedGaugeBefore, "Valid route returned from cache during outage");

    // Restore provider
    mockProvider->shouldFail = false;

    // -------------------------------------------------------------------------
    // TEST 6: Case 6 — F1, contextual buttons, and AI tools resolve consistently
    // -------------------------------------------------------------------------
    std::cout << "-> Test 6: Case 6 — Consistent resolution across F1, contextual buttons, and AI tools" << std::endl;
    const juce::String testTopic = "control-panel.analog-gauge";

    // 1. Contextual button / F1 resolution
    auto f1Url = registry.resolveTopicUrl(testTopic);

    // 2. AI Tool help_openTopic
    juce::String toolOpenedUrl;
    auto toolJson = djehuti_help::executeHelpTool(
        "help_openTopic",
        juce::JSON::parse("{\"topicId\":\"control-panel.analog-gauge\"}"),
        [&](const juce::String& tid) {
            toolOpenedUrl = registry.resolveTopicUrl(tid);
        },
        [](const juce::String&) {});

    auto parsedToolResult = juce::JSON::parse(toolJson);
    CHECK((bool)parsedToolResult.getProperty("ok", false), "help_openTopic tool succeeded");
    CHECK_EQUAL(parsedToolResult.getProperty("url", {}).toString(), f1Url, "AI tool resolved identical URL to F1");
    CHECK_EQUAL(toolOpenedUrl, f1Url, "Tool callback received identical URL");

    // 3. AI Tool help_search
    auto searchJson = djehuti_help::executeHelpTool(
        "help_search",
        juce::JSON::parse("{\"query\":\"analog gauge\"}"),
        [](const juce::String&) {},
        [](const juce::String&) {});
    auto parsedSearchResult = juce::JSON::parse(searchJson);
    CHECK((bool)parsedSearchResult.getProperty("ok", false), "help_search tool succeeded");
    CHECK(parsedSearchResult.getProperty("url", {}).toString().contains("control-panel-instruments"),
          "Search for 'analog gauge' points to control-panel-instruments");

    // -------------------------------------------------------------------------
    // TEST 7: React SPA awareness & URL Security
    // -------------------------------------------------------------------------
    std::cout << "-> Test 7: React SPA awareness and URL security validation" << std::endl;
    CHECK(registry.isApprovedDocumentationUrl("https://lagdaemon.com/electronics-work-bench/docs/product-overview/overview"), "Approved doc URL accepted");
    CHECK(!registry.isApprovedDocumentationUrl("https://evil.com/malicious"), "External domain rejected");
    CHECK(!registry.isApprovedDocumentationUrl("https://lagdaemon.com/admin"), "Non-docs path rejected");

    CHECK(registry.isKnownValidDestination("https://lagdaemon.com/electronics-work-bench/docs/product-overview/overview"), "Known valid section accepted");
    CHECK(!registry.isKnownValidDestination("https://lagdaemon.com/electronics-work-bench/docs/nonexistent-manual/nonexistent-section"), "Nonexistent SPA section recognized as invalid");

    // AI navigate tool rejected on external URL
    auto evilNavigateJson = djehuti_help::executeHelpTool(
        "help_navigate",
        juce::JSON::parse("{\"url\":\"https://attacker.site/exploit\"}"),
        [](const juce::String&) {},
        [](const juce::String&) {});
    auto parsedEvilResult = juce::JSON::parse(evilNavigateJson);
    CHECK(!((bool)parsedEvilResult.getProperty("ok", false)), "Unapproved URL rejected by help_navigate");

    // -------------------------------------------------------------------------
    // TEST 8: HelpPanel UI Component Smoke Test
    // -------------------------------------------------------------------------
    std::cout << "-> Test 8: HelpPanel UI Component smoke test" << std::endl;
    {
        djehuti_help::HelpPanel panel;
        panel.setBounds(0, 0, 800, 600);
        panel.openTopic("overview");
        CHECK(panel.getCurrentUrl().contains("overview"), "HelpPanel navigated to overview");

        panel.showNetworkError("Connection refused");
        panel.hideNetworkError();
        panel.goHome();
        CHECK(panel.getCurrentUrl() == djehuti_help::TopicRegistry::getDefaultOverviewUrl(), "HelpPanel goHome works");
    }

    // Clean up test cache file
    testCacheFile.deleteFile();

    std::cout << "\n==========================================" << std::endl;
    std::cout << "Results: " << passedChecks << " / " << totalChecks << " checks passed." << std::endl;

    if (passedChecks == totalChecks)
    {
        std::cout << "ALL HELP SYSTEM TESTS PASSED SUCCESSFULLY!" << std::endl;
        return 0;
    }

    std::cerr << "SOME HELP SYSTEM TESTS FAILED!" << std::endl;
    return 1;
}
