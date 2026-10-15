#include "PredefinedLayoutCatalog.h"
#include "WorkspaceLayoutStore.h"
#include <set>
#include <algorithm>

PredefinedLayoutCatalog::PredefinedLayoutCatalog()
    : PredefinedLayoutCatalog(defaultCatalogFile(), defaultTemplateFile())
{
}

PredefinedLayoutCatalog::PredefinedLayoutCatalog(const juce::File& catalogFile, const juce::File& templateFile)
    : activeCatalogFile(catalogFile),
      shippedTemplateFile(templateFile.existsAsFile() ? templateFile : defaultTemplateFile())
{
}

juce::File PredefinedLayoutCatalog::defaultCatalogFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("DjehutiElectronicsLab")
        .getChildFile("default_layouts.json");
}

juce::File PredefinedLayoutCatalog::defaultTemplateFile()
{
    // First look for deployed template sibling to executable
    const auto exeSibling = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                                .getSiblingFile("default_layouts.json");
    if (exeSibling.existsAsFile())
        return exeSibling;

    // Second look for repository source template
    const auto repoTemplate = juce::File("D:/000 Tech Research/Electronics Design Tool/prototype/Resources/default_layouts.json");
    if (repoTemplate.existsAsFile())
        return repoTemplate;

    return exeSibling;
}

juce::String PredefinedLayoutCatalog::getDefaultCatalogTemplateJson()
{
    return R"({
  "version": 1,
  "presets": [
    {
      "id": "default",
      "name": "Default Layout",
      "order": 0,
      "leftWidth": 260,
      "rightWidth": 280,
      "bottomHeight": 220,
      "zones": {
        "left": {
          "panels": [
            "library",
            "hierarchy"
          ],
          "currentTab": 0
        },
        "center": {
          "panels": [
            "schematic",
            "analytics",
            "pcb",
            "help"
          ],
          "currentTab": 3
        },
        "right": {
          "panels": [
            "properties",
            "agent"
          ],
          "currentTab": 0
        },
        "bottom": {
          "panels": [
            "log",
            "console"
          ],
          "currentTab": 0
        }
      },
      "floating": []
    },
    {
      "id": "schematic",
      "name": "Schematic & Circuit Construction",
      "order": 1,
      "leftWidth": 260,
      "rightWidth": 280,
      "bottomHeight": 220,
      "zones": {
        "left": {
          "panels": [
            "library",
            "hierarchy"
          ],
          "currentTab": 0
        },
        "center": {
          "panels": [
            "schematic",
            "pcb"
          ],
          "currentTab": 0
        },
        "right": {
          "panels": [
            "properties",
            "agent"
          ],
          "currentTab": 0
        },
        "bottom": {
          "panels": [
            "log",
            "console"
          ],
          "currentTab": 0
        }
      },
      "floating": []
    },
    {
      "id": "simulation",
      "name": "Simulation & Analytics",
      "order": 2,
      "leftWidth": 260,
      "rightWidth": 280,
      "bottomHeight": 220,
      "zones": {
        "left": {
          "panels": [
            "library"
          ],
          "currentTab": 0
        },
        "center": {
          "panels": [
            "analytics",
            "control_panel",
            "schematic"
          ],
          "currentTab": 0
        },
        "right": {
          "panels": [
            "properties"
          ],
          "currentTab": 0
        },
        "bottom": {
          "panels": [
            "log",
            "console"
          ],
          "currentTab": 0
        }
      },
      "floating": []
    },
    {
      "id": "machine_builder",
      "name": "Machine Builder & Systems",
      "order": 3,
      "leftWidth": 260,
      "rightWidth": 280,
      "bottomHeight": 220,
      "zones": {
        "left": {
          "panels": [
            "hierarchy"
          ],
          "currentTab": 0
        },
        "center": {
          "panels": [
            "machine_builder",
            "mechanical_view",
            "demo_runner",
            "control_panel"
          ],
          "currentTab": 0
        },
        "right": {
          "panels": [
            "properties"
          ],
          "currentTab": 0
        },
        "bottom": {
          "panels": [
            "log"
          ],
          "currentTab": 0
        }
      },
      "floating": []
    },
    {
      "id": "frust",
      "name": "FRust & Node Programming",
      "order": 4,
      "leftWidth": 260,
      "rightWidth": 280,
      "bottomHeight": 220,
      "zones": {
        "left": {
          "panels": [
            "library"
          ],
          "currentTab": 0
        },
        "center": {
          "panels": [
            "nodes",
            "schematic"
          ],
          "currentTab": 0
        },
        "right": {
          "panels": [
            "debugger",
            "properties"
          ],
          "currentTab": 0
        },
        "bottom": {
          "panels": [
            "frust",
            "console",
            "log"
          ],
          "currentTab": 0
        }
      },
      "floating": []
    }
  ]
})";
}

bool PredefinedLayoutCatalog::initialize(juce::String& error)
{
    if (!activeCatalogFile.existsAsFile())
    {
        if (!seedCatalogFromTemplate(error))
        {
            // Seed failure - fallback to packaged JSON template in memory
            auto parsed = juce::JSON::parse(getDefaultCatalogTemplateJson());
            validatePresetCatalog(parsed, presets, error);
            return false;
        }
    }

    return forceReload(error);
}

bool PredefinedLayoutCatalog::seedCatalogFromTemplate(juce::String& error)
{
    activeCatalogFile.getParentDirectory().createDirectory();

    if (shippedTemplateFile.existsAsFile())
    {
        if (shippedTemplateFile.copyFileTo(activeCatalogFile))
            return true;
    }

    // Write default template JSON if template file wasn't found or copy failed
    if (activeCatalogFile.replaceWithText(getDefaultCatalogTemplateJson()))
        return true;

    error = "Failed to write default layout catalog to " + activeCatalogFile.getFullPathName();
    return false;
}

bool PredefinedLayoutCatalog::reloadIfChanged(juce::String& error)
{
    if (!activeCatalogFile.existsAsFile())
    {
        return initialize(error);
    }

    const auto modTime = activeCatalogFile.getLastModificationTime();
    if (modTime != lastModificationTime)
    {
        return forceReload(error);
    }

    return true;
}

bool PredefinedLayoutCatalog::forceReload(juce::String& error)
{
    if (!activeCatalogFile.existsAsFile())
    {
        error = "Catalog file does not exist: " + activeCatalogFile.getFullPathName();
        return false;
    }

    std::vector<PredefinedLayout> newPresets;
    if (loadFromFileInternal(activeCatalogFile, newPresets, error))
    {
        presets = std::move(newPresets);
        lastModificationTime = activeCatalogFile.getLastModificationTime();
        error.clear();
        return true;
    }

    // On invalid data, retain existing presets in memory and do not overwrite file
    return false;
}

bool PredefinedLayoutCatalog::loadFromFileInternal(const juce::File& file, std::vector<PredefinedLayout>& outPresets, juce::String& error)
{
    const auto text = file.loadFileAsString();
    if (text.trim().isEmpty())
    {
        error = "File is empty: " + file.getFullPathName();
        return false;
    }

    juce::var parsed;
    const auto parseResult = juce::JSON::parse(text, parsed);
    if (parseResult.failed())
    {
        error = "JSON parse error in " + file.getFullPathName() + ": " + parseResult.getErrorMessage();
        return false;
    }

    return validatePresetCatalog(parsed, outPresets, error);
}

bool PredefinedLayoutCatalog::validatePresetCatalog(const juce::var& root, std::vector<PredefinedLayout>& outPresets, juce::String& error)
{
    if (!root.isObject())
    {
        error = "Root JSON must be an object containing a 'presets' array.";
        return false;
    }

    const auto* presetsArr = root.getProperty("presets", {}).getArray();
    if (presetsArr == nullptr)
    {
        error = "Missing 'presets' array in catalog.";
        return false;
    }

    if (presetsArr->isEmpty())
    {
        error = "'presets' array cannot be empty.";
        return false;
    }

    std::vector<PredefinedLayout> loaded;
    std::set<juce::String> seenIds;
    std::set<juce::String> seenNames;

    int defaultIndex = 0;
    for (const auto& entryVar : *presetsArr)
    {
        if (!entryVar.isObject())
        {
            error = "Preset entry #" + juce::String(defaultIndex + 1) + " must be a JSON object.";
            return false;
        }

        const auto id = entryVar.getProperty("id", {}).toString().trim();
        if (id.isEmpty())
        {
            error = "Preset #" + juce::String(defaultIndex + 1) + " is missing a non-empty 'id'.";
            return false;
        }

        const auto idLower = id.toLowerCase();
        if (!seenIds.insert(idLower).second)
        {
            error = "Duplicate preset id found in catalog: '" + id + "'.";
            return false;
        }

        const auto name = entryVar.getProperty("name", {}).toString().trim();
        if (name.isEmpty())
        {
            error = "Preset '" + id + "' is missing a non-empty 'name'.";
            return false;
        }

        const auto nameLower = name.toLowerCase();
        if (!seenNames.insert(nameLower).second)
        {
            error = "Duplicate preset name found in catalog: '" + name + "'.";
            return false;
        }

        juce::String layoutErr;
        if (!validateSingleLayoutData(entryVar, layoutErr))
        {
            error = "Preset '" + id + "' has invalid layout structure: " + layoutErr;
            return false;
        }

        PredefinedLayout p;
        p.id = id;
        p.name = name;
        p.order = (int) entryVar.getProperty("order", defaultIndex);
        p.layoutData = entryVar.clone();
        loaded.push_back(std::move(p));

        defaultIndex++;
    }

    // Sort presets by order ascending (preserving relative order for equal priorities)
    std::stable_sort(loaded.begin(), loaded.end(), [](const PredefinedLayout& a, const PredefinedLayout& b) {
        return a.order < b.order;
    });

    outPresets = std::move(loaded);
    return true;
}

bool PredefinedLayoutCatalog::validateSingleLayoutData(const juce::var& layoutData, juce::String& error)
{
    if (!layoutData.isObject())
    {
        error = "Layout must be a JSON object.";
        return false;
    }

    const auto zonesVar = layoutData.getProperty("zones", {});
    if (!zonesVar.isObject())
    {
        error = "Missing 'zones' object in layout.";
        return false;
    }

    const char* requiredZones[] = { "left", "center", "right", "bottom" };
    std::set<juce::String> seenPanelsAcrossZones;

    for (const char* zoneName : requiredZones)
    {
        const auto zoneVar = zonesVar.getProperty(zoneName, {});
        if (!zoneVar.isObject())
        {
            error = "Missing zone '" + juce::String(zoneName) + "' in zones definition.";
            return false;
        }

        const auto* panelsArr = zoneVar.getProperty("panels", {}).getArray();
        if (panelsArr == nullptr)
        {
            error = "Zone '" + juce::String(zoneName) + "' must contain a 'panels' array.";
            return false;
        }

        const int currentTab = (int) zoneVar.getProperty("currentTab", 0);
        if (!panelsArr->isEmpty())
        {
            if (currentTab < 0 || currentTab >= panelsArr->size())
            {
                error = "Zone '" + juce::String(zoneName) + "' currentTab (" + juce::String(currentTab) +
                        ") is out of range for panels size (" + juce::String(panelsArr->size()) + ").";
                return false;
            }
        }

        for (const auto& p : *panelsArr)
        {
            const auto panelId = p.toString().trim();
            if (panelId.isEmpty())
            {
                error = "Zone '" + juce::String(zoneName) + "' contains an empty panel id.";
                return false;
            }

            const auto panelLower = panelId.toLowerCase();
            if (!seenPanelsAcrossZones.insert(panelLower).second)
            {
                error = "Duplicate panel placement: panel '" + panelId + "' appears more than once in layout.";
                return false;
            }
        }
    }

    if (auto* floatingArr = layoutData.getProperty("floating", {}).getArray())
    {
        for (const auto& f : *floatingArr)
        {
            if (!f.isObject())
            {
                error = "Floating window entry must be an object.";
                return false;
            }
            const auto floatId = f.getProperty("id", {}).toString().trim();
            if (floatId.isEmpty())
            {
                error = "Floating window is missing an 'id'.";
                return false;
            }
            if (!seenPanelsAcrossZones.insert(floatId.toLowerCase()).second)
            {
                error = "Floating panel '" + floatId + "' is already docked in a zone.";
                return false;
            }
        }
    }

    return true;
}

juce::StringArray PredefinedLayoutCatalog::getPresetIds() const
{
    juce::StringArray ids;
    for (const auto& p : presets)
        ids.add(p.id);
    return ids;
}

juce::StringArray PredefinedLayoutCatalog::getPresetNames() const
{
    juce::StringArray names;
    for (const auto& p : presets)
        names.add(p.name);
    return names;
}

bool PredefinedLayoutCatalog::hasPresetWithId(const juce::String& id) const
{
    return findPresetById(id) != nullptr;
}

bool PredefinedLayoutCatalog::hasPresetWithName(const juce::String& name) const
{
    return findPresetByName(name) != nullptr;
}

const PredefinedLayoutCatalog::PredefinedLayout* PredefinedLayoutCatalog::findPresetById(const juce::String& id) const
{
    const auto trimmed = id.trim();
    for (const auto& p : presets)
    {
        if (p.id.equalsIgnoreCase(trimmed))
            return &p;
    }
    return nullptr;
}

const PredefinedLayoutCatalog::PredefinedLayout* PredefinedLayoutCatalog::findPresetByName(const juce::String& name) const
{
    const auto trimmed = name.trim();
    for (const auto& p : presets)
    {
        if (p.name.equalsIgnoreCase(trimmed))
            return &p;
    }
    return nullptr;
}

const PredefinedLayoutCatalog::PredefinedLayout* PredefinedLayoutCatalog::findPresetByIdOrName(const juce::String& identifier) const
{
    if (const auto* byId = findPresetById(identifier))
        return byId;
    return findPresetByName(identifier);
}

bool PredefinedLayoutCatalog::isPresetNameOrId(const juce::String& nameOrId) const
{
    return findPresetByIdOrName(nameOrId) != nullptr;
}

bool PredefinedLayoutCatalog::isPredefinedName(const juce::String& name)
{
    PredefinedLayoutCatalog catalog;
    juce::String err;
    catalog.initialize(err);
    return catalog.isPresetNameOrId(name);
}
