#pragma once

#include <JuceHeader.h>
#include <CreationDock/DockManager.h>
#include <vector>

class PredefinedLayoutCatalog
{
public:
    struct PredefinedLayout
    {
        juce::String id;
        juce::String name;
        int order = 0;
        juce::var layoutData;
    };

    PredefinedLayoutCatalog();
    explicit PredefinedLayoutCatalog(const juce::File& catalogFile, const juce::File& templateFile = {});

    static juce::File defaultCatalogFile();
    static juce::File defaultTemplateFile();
    static juce::String getDefaultCatalogTemplateJson();

    // Query active file locations
    juce::File getActiveCatalogFile() const { return activeCatalogFile; }
    juce::File getTemplateFile() const { return shippedTemplateFile; }

    // Initialization and dynamic reloading
    bool initialize(juce::String& error);
    bool reloadIfChanged(juce::String& error);
    bool forceReload(juce::String& error);

    // Queries
    const std::vector<PredefinedLayout>& getPresets() const { return presets; }
    juce::StringArray getPresetIds() const;
    juce::StringArray getPresetNames() const;

    bool hasPresetWithId(const juce::String& id) const;
    bool hasPresetWithName(const juce::String& name) const;

    const PredefinedLayout* findPresetById(const juce::String& id) const;
    const PredefinedLayout* findPresetByName(const juce::String& name) const;
    const PredefinedLayout* findPresetByIdOrName(const juce::String& identifier) const;

    // Collision checking against predefined preset names/ids
    bool isPresetNameOrId(const juce::String& nameOrId) const;
    static bool isPredefinedName(const juce::String& name);

    // Validation
    static bool validatePresetCatalog(const juce::var& root, std::vector<PredefinedLayout>& outPresets, juce::String& error);
    static bool validateSingleLayoutData(const juce::var& layoutData, juce::String& error);

private:
    juce::File activeCatalogFile;
    juce::File shippedTemplateFile;
    juce::Time lastModificationTime;
    std::vector<PredefinedLayout> presets;

    bool loadFromFileInternal(const juce::File& file, std::vector<PredefinedLayout>& outPresets, juce::String& error);
    bool seedCatalogFromTemplate(juce::String& error);
};
