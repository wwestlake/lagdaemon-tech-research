#include "WorkspaceLayoutStore.h"
#include "PredefinedLayoutCatalog.h"
#include <set>

WorkspaceLayoutStore::WorkspaceLayoutStore()
    : WorkspaceLayoutStore(defaultStorageFile())
{
}

WorkspaceLayoutStore::WorkspaceLayoutStore(const juce::File& file)
    : storageFile(file)
{
    reload();
}

juce::File WorkspaceLayoutStore::defaultStorageFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("DjehutiElectronicsLab")
        .getChildFile("user_layouts.json");
}

bool WorkspaceLayoutStore::isBuiltInPresetName(const juce::String& name)
{
    return PredefinedLayoutCatalog::isPredefinedName(name);
}

juce::StringArray WorkspaceLayoutStore::getLayoutNames() const
{
    juce::StringArray names;
    for (const auto& l : layouts)
        names.add(l.name);
    names.sort(true); // Case-insensitive alphabetical sort
    return names;
}

int WorkspaceLayoutStore::findIndex(const juce::String& name) const
{
    const auto trimmed = name.trim();
    for (size_t i = 0; i < layouts.size(); ++i)
    {
        if (layouts[i].name.equalsIgnoreCase(trimmed))
            return (int) i;
    }
    return -1;
}

bool WorkspaceLayoutStore::hasLayout(const juce::String& name) const
{
    return findIndex(name) >= 0;
}

juce::var WorkspaceLayoutStore::getLayoutData(const juce::String& name) const
{
    const int idx = findIndex(name);
    if (idx >= 0 && idx < (int) layouts.size())
        return layouts[(size_t) idx].layoutData;
    return {};
}

bool WorkspaceLayoutStore::saveLayout(const juce::String& name, const juce::var& layoutData, juce::String& error)
{
    const auto trimmed = name.trim();
    if (trimmed.isEmpty())
    {
        error = "Layout name cannot be empty.";
        return false;
    }

    if (isBuiltInPresetName(trimmed))
    {
        error = "Cannot overwrite built-in layout preset '" + trimmed + "'.";
        return false;
    }

    if (!validateLayoutData(layoutData, error))
        return false;

    const auto backup = layouts;
    const int existingIdx = findIndex(trimmed);

    if (existingIdx >= 0)
    {
        layouts[(size_t) existingIdx].name = trimmed;
        layouts[(size_t) existingIdx].layoutData = layoutData;
    }
    else
    {
        layouts.push_back({ trimmed, layoutData });
    }

    if (!saveToFile(storageFile, error))
    {
        layouts = backup;
        return false;
    }

    return true;
}

bool WorkspaceLayoutStore::renameLayout(const juce::String& oldName, const juce::String& newName, juce::String& error)
{
    const auto trimmedOld = oldName.trim();
    const auto trimmedNew = newName.trim();

    if (trimmedNew.isEmpty())
    {
        error = "New layout name cannot be empty.";
        return false;
    }

    if (isBuiltInPresetName(trimmedNew))
    {
        error = "Cannot use built-in layout preset name '" + trimmedNew + "'.";
        return false;
    }

    const int oldIdx = findIndex(trimmedOld);
    if (oldIdx < 0)
    {
        error = "Layout '" + trimmedOld + "' was not found.";
        return false;
    }

    const int newIdx = findIndex(trimmedNew);
    if (newIdx >= 0 && newIdx != oldIdx)
    {
        error = "A layout named '" + trimmedNew + "' already exists.";
        return false;
    }

    const auto backup = layouts;
    layouts[(size_t) oldIdx].name = trimmedNew;

    if (!saveToFile(storageFile, error))
    {
        layouts = backup;
        return false;
    }

    return true;
}

bool WorkspaceLayoutStore::deleteLayout(const juce::String& name, juce::String& error)
{
    const int idx = findIndex(name);
    if (idx < 0)
    {
        error = "Layout '" + name.trim() + "' was not found.";
        return false;
    }

    const auto backup = layouts;
    layouts.erase(layouts.begin() + idx);

    if (!saveToFile(storageFile, error))
    {
        layouts = backup;
        return false;
    }

    return true;
}

void WorkspaceLayoutStore::reload()
{
    juce::String error;
    loadFromFile(storageFile, error);
}

bool WorkspaceLayoutStore::loadFromFile(const juce::File& file, juce::String& error)
{
    if (!file.existsAsFile())
    {
        layouts.clear();
        return true;
    }

    const auto content = file.loadFileAsString();
    if (content.trim().isEmpty())
    {
        layouts.clear();
        return true;
    }

    const auto parsed = juce::JSON::parse(content);
    if (!parsed.isObject())
    {
        error = "Layout storage file does not contain a valid JSON object.";
        return false;
    }

    std::vector<NamedLayout> parsedList;
    if (!deserialize(parsed, parsedList, error))
        return false;

    layouts = std::move(parsedList);
    return true;
}

bool WorkspaceLayoutStore::saveToFile(const juce::File& file, juce::String& error) const
{
    const auto dir = file.getParentDirectory();
    if (!dir.exists() && !dir.createDirectory())
    {
        error = "Could not create application data directory: " + dir.getFullPathName();
        return false;
    }

    const auto jsonVar = serialize(layouts);
    const auto jsonString = juce::JSON::toString(jsonVar, false);

    // Safe atomic write: write to temp file first, then replace target file
    const auto tempFile = dir.getChildFile(file.getFileNameWithoutExtension() + "_tmp.json");
    if (!tempFile.replaceWithText(jsonString))
    {
        error = "Could not write temporary layout file: " + tempFile.getFullPathName();
        return false;
    }

    if (!tempFile.moveFileTo(file))
    {
        // Fallback: direct write if move failed
        if (!file.replaceWithText(jsonString))
        {
            error = "Could not write layout file: " + file.getFullPathName();
            tempFile.deleteFile();
            return false;
        }
    }

    tempFile.deleteFile();
    return true;
}

juce::var WorkspaceLayoutStore::serialize(const std::vector<NamedLayout>& list)
{
    auto* root = new juce::DynamicObject();
    root->setProperty("version", 1);

    juce::Array<juce::var> arr;
    for (const auto& item : list)
    {
        auto* entry = new juce::DynamicObject();
        entry->setProperty("name", item.name);
        entry->setProperty("layout", item.layoutData);
        arr.add(juce::var(entry));
    }

    root->setProperty("layouts", arr);
    return juce::var(root);
}

bool WorkspaceLayoutStore::deserialize(const juce::var& root, std::vector<NamedLayout>& outList, juce::String& error)
{
    outList.clear();

    if (!root.isObject())
    {
        error = "Root JSON is not an object.";
        return false;
    }

    const int version = (int) root.getProperty("version", 0);
    if (version < 1)
    {
        error = "Unsupported layout storage version: " + juce::String(version);
        return false;
    }

    auto* arr = root.getProperty("layouts", {}).getArray();
    if (arr == nullptr)
    {
        error = "Missing 'layouts' array in storage file.";
        return false;
    }

    for (const auto& itemVar : *arr)
    {
        if (!itemVar.isObject())
            continue;

        const auto name = itemVar.getProperty("name", {}).toString().trim();
        const auto layoutData = itemVar.getProperty("layout", {});

        if (name.isNotEmpty() && layoutData.isObject())
        {
            outList.push_back({ name, layoutData });
        }
    }

    return true;
}

bool WorkspaceLayoutStore::validateLayoutData(const juce::var& layoutData, juce::String& error)
{
    if (!layoutData.isObject())
    {
        error = "Layout data is not a valid JSON object.";
        return false;
    }

    const auto zones = layoutData.getProperty("zones", {});
    if (!zones.isObject())
    {
        error = "Layout data missing 'zones' definition.";
        return false;
    }

    return true;
}

void WorkspaceLayoutStore::sanitizeFloatingBounds(juce::var& layoutData)
{
    if (!layoutData.isObject())
        return;

    auto* floatingArr = layoutData.getProperty("floating", {}).getArray();
    if (floatingArr == nullptr || floatingArr->isEmpty())
        return;

    const auto& displays = juce::Desktop::getInstance().getDisplays();
    auto* primary = displays.getPrimaryDisplay();
    const auto fallbackArea = (primary != nullptr ? primary->userArea : juce::Rectangle<int>(100, 100, 800, 600));

    for (auto& entryVar : *floatingArr)
    {
        if (!entryVar.isObject())
            continue;

        auto* obj = entryVar.getDynamicObject();
        int x = (int) obj->getProperty("x");
        int y = (int) obj->getProperty("y");
        int w = juce::jmax(200, (int) obj->getProperty("w"));
        int h = juce::jmax(150, (int) obj->getProperty("h"));

        juce::Rectangle<int> rect(x, y, w, h);
        bool intersectsAnyDisplay = false;

        for (const auto& d : displays.displays)
        {
            if (d.userArea.intersects(rect))
            {
                intersectsAnyDisplay = true;
                break;
            }
        }

        if (!intersectsAnyDisplay)
        {
            // Center within the primary display user area
            rect.setSize(juce::jmin(w, fallbackArea.getWidth() - 40),
                         juce::jmin(h, fallbackArea.getHeight() - 40));
            rect.setCentre(fallbackArea.getCentre());
            obj->setProperty("x", rect.getX());
            obj->setProperty("y", rect.getY());
            obj->setProperty("w", rect.getWidth());
            obj->setProperty("h", rect.getHeight());
        }
    }
}

bool WorkspaceLayoutStore::applyLayoutSafely(CreationDock::DockManager& dockManager, const juce::var& layoutData, juce::String& error)
{
    if (!validateLayoutData(layoutData, error))
        return false;

    // Collect all panels that are specified to be open in the new layout
    std::set<juce::String> panelsInLayout;

    auto zones = layoutData.getProperty("zones", {});
    auto collectZonePanels = [&](const juce::String& zoneName) {
        auto z = zones.getProperty(zoneName, {});
        if (z.isObject())
        {
            if (auto* panelIds = z.getProperty("panels", {}).getArray())
            {
                for (const auto& idVar : *panelIds)
                    panelsInLayout.insert(idVar.toString().trim());
            }
        }
    };

    collectZonePanels("left");
    collectZonePanels("center");
    collectZonePanels("right");
    collectZonePanels("bottom");

    if (auto* floatingArr = layoutData.getProperty("floating", {}).getArray())
    {
        for (const auto& f : *floatingArr)
        {
            if (f.isObject())
            {
                const auto id = f.getProperty("id", {}).toString().trim();
                if (id.isNotEmpty())
                    panelsInLayout.insert(id);
            }
        }
    }

    // Hide any currently open panels that are NOT present in the layout
    const auto registeredIds = dockManager.getRegisteredPanelIds();
    for (const auto& regId : registeredIds)
    {
        if (panelsInLayout.find(regId) == panelsInLayout.end())
        {
            if (dockManager.isPanelOpen(regId))
                dockManager.hidePanel(regId);
        }
    }

    // Sanitize floating window coordinates against current monitor configuration
    juce::var sanitizedLayout = layoutData.clone();
    sanitizeFloatingBounds(sanitizedLayout);

    // Apply the layout to the dock manager
    dockManager.applyLayout(sanitizedLayout);

    // Explicitly activate the active tab specified for each zone
    for (const char* zoneName : { "left", "center", "right", "bottom" })
    {
        auto z = zones.getProperty(zoneName, {});
        if (z.isObject())
        {
            if (auto* panelIds = z.getProperty("panels", {}).getArray())
            {
                int currentTab = (int) z.getProperty("currentTab", 0);
                if (juce::isPositiveAndBelow(currentTab, panelIds->size()))
                {
                    const auto activeId = (*panelIds)[currentTab].toString().trim();
                    if (activeId.isNotEmpty())
                        dockManager.activatePanel(activeId);
                }
            }
        }
    }

    return true;
}
