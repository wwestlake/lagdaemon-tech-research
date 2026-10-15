#pragma once

#include <JuceHeader.h>
#include <CreationDock/DockManager.h>
#include <vector>

class WorkspaceLayoutStore
{
public:
    struct NamedLayout
    {
        juce::String name;
        juce::var layoutData;
    };

    WorkspaceLayoutStore();
    explicit WorkspaceLayoutStore(const juce::File& storageFile);

    static juce::File defaultStorageFile();
    static bool isBuiltInPresetName(const juce::String& name);

    // Queries
    juce::StringArray getLayoutNames() const; // Sorted alphabetically, case-insensitive
    bool hasLayout(const juce::String& name) const;
    juce::var getLayoutData(const juce::String& name) const;
    const std::vector<NamedLayout>& getLayouts() const { return layouts; }

    // Mutators
    bool saveLayout(const juce::String& name, const juce::var& layoutData, juce::String& error);
    bool renameLayout(const juce::String& oldName, const juce::String& newName, juce::String& error);
    bool deleteLayout(const juce::String& name, juce::String& error);

    // Persistence
    bool loadFromFile(const juce::File& file, juce::String& error);
    bool saveToFile(const juce::File& file, juce::String& error) const;
    void reload();

    // Validation & Safe Application
    static bool validateLayoutData(const juce::var& layoutData, juce::String& error);
    static void sanitizeFloatingBounds(juce::var& layoutData);
    static bool applyLayoutSafely(CreationDock::DockManager& dockManager, const juce::var& layoutData, juce::String& error);

private:
    juce::File storageFile;
    std::vector<NamedLayout> layouts;

    int findIndex(const juce::String& name) const;
    static juce::var serialize(const std::vector<NamedLayout>& list);
    static bool deserialize(const juce::var& root, std::vector<NamedLayout>& outList, juce::String& error);
};
