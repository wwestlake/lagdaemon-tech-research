#pragma once
#include <juce_core/juce_core.h>
#include <algorithm>
#include <functional>
#include <vector>

// One disk-backed collection for the palette, canvas shortcut and agent tools.
// Definitions remain independent copy templates; management never edits diagrams.
namespace user_blocks
{
struct Entry
{
    juce::String name, category, description;
    juce::File file;
};

class Library
{
public:
    explicit Library(juce::File directory = defaultFolder()) : folder(std::move(directory)) {}
    static juce::File defaultFolder()
    {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("DjehutiElectronicsLab/user_library/blocks");
    }
    std::function<void()> onChanged;

    static bool valid(const juce::var& value)
    {
        const auto* root = value.getDynamicObject();
        if (root == nullptr || root->getProperty("schema").toString() != "djehuti_electronics_user_block"
            || (int)root->getProperty("version") != 1 || root->getProperty("name").toString().trim().isEmpty()) return false;
        const auto sheet = root->getProperty("sourceSheet").toString();
        const auto source = root->getProperty("sourceBlock").toString();
        const auto* circuit = root->getProperty("circuit").getDynamicObject();
        const auto* components = circuit != nullptr ? circuit->getProperty("components").getArray() : nullptr;
        if (sheet.isEmpty() || source.isEmpty() || components == nullptr) return false;
        for (const auto& c : *components)
            if (c.getProperty("id", {}).toString() == source && c.getProperty("symbol", {}).toString() == "sub_block"
                && c.getProperty("childSheet", {}).toString() == sheet) return true;
        return false;
    }

    std::vector<Entry> entries() const
    {
        std::vector<Entry> result;
        auto files = folder.findChildFiles(juce::File::findFiles, false, "*.block.json");
        std::sort(files.begin(), files.end(), [](const juce::File& a, const juce::File& b) { return a.getFullPathName() < b.getFullPathName(); });
        juce::StringArray names;
        for (const auto& file : files)
        {
            const auto v = juce::JSON::parse(file);
            if (!valid(v)) continue;
            const auto name = v.getProperty("name", {}).toString().trim();
            if (names.contains(name, true)) continue;
            names.add(name);
            result.push_back({name, v.getProperty("category", {}).toString().trim(), v.getProperty("description", {}).toString(), file});
        }
        std::sort(result.begin(), result.end(), [](const Entry& a, const Entry& b) { return a.name.compareIgnoreCase(b.name) < 0; });
        return result;
    }
    juce::StringArray categories() const
    {
        juce::StringArray result;
        const auto manifest = juce::JSON::parse(categoryFile());
        if (const auto* items = manifest.getArray())
            for (const auto& item : *items)
                if (item.toString().trim().isNotEmpty()) result.addIfNotAlreadyThere(item.toString().trim(), true);
        for (const auto& entry : entries())
            if (entry.category.isNotEmpty()) result.addIfNotAlreadyThere(entry.category, true);
        result.sort(true);
        return result;
    }
    juce::File find(const juce::String& name) const
    {
        for (const auto& entry : entries()) if (entry.name.equalsIgnoreCase(name.trim())) return entry.file;
        return {};
    }
    bool save(juce::var definition, juce::File& saved, juce::String& error)
    {
        if (!valid(definition)) return fail(error, "Invalid reusable block: name, source block and child sheet are required.");
        auto* root = definition.getDynamicObject();
        const auto name = root->getProperty("name").toString().trim();
        root->setProperty("name", name);
        root->setProperty("category", root->getProperty("category").toString().trim());
        const auto category = root->getProperty("category").toString();
        if (category.isNotEmpty())
        {
            auto cats = categories();
            cats.addIfNotAlreadyThere(category, true);
            juce::Array<juce::var> values;
            for (const auto& cat : cats) values.add(cat);
            if (!write(categoryFile(), juce::var(values), error)) return false;
        }
        saved = find(name);
        if (saved == juce::File())
        {
            juce::String stem;
            for (int i = 0; i < name.length(); ++i)
                stem += juce::CharacterFunctions::isLetterOrDigit(name[i]) ? juce::String::charToString(name[i]) : "_";
            saved = folder.getNonexistentChildFile(stem.isEmpty() ? "block" : stem, ".block.json", false);
        }
        if (!write(saved, definition, error)) return false;
        changed();
        return true;
    }

    // Category changes update metadata only. Roll back all touched files if any write fails.
    bool manage(const juce::String& action, juce::String name, juce::String value, juce::String& error)
    {
        name = name.trim(); value = value.trim();
        if (action == "refresh") { changed(); return true; }
        auto cats = categories();
        if (action == "create_category" || action == "rename_category" || action == "delete_category")
        {
            if (name.isEmpty()) return fail(error, "A category name is required.");
            if (action == "create_category")
            {
                if (cats.contains(name, true)) return fail(error, "Category already exists.");
                cats.add(name);
            }
            else
            {
                const auto index = cats.indexOf(name, true);
                if (index < 0) return fail(error, "Category does not exist.");
                name = cats[index];
                if (action == "rename_category" && (value.isEmpty() || (cats.contains(value, true) && !name.equalsIgnoreCase(value))))
                    return fail(error, "Choose a nonempty, unused category name.");
                for (const auto& entry : entries())
                    if (entry.category.equalsIgnoreCase(name) && action == "delete_category")
                        return fail(error, "Only empty categories can be deleted. Move their components first.");
                cats.remove(index);
                if (action == "rename_category") cats.add(value);
            }
            std::vector<std::pair<juce::File, juce::String>> backups;
            if (action == "rename_category")
                for (const auto& entry : entries())
                    if (entry.category.equalsIgnoreCase(name))
                    {
                        backups.push_back({entry.file, entry.file.loadFileAsString()});
                        auto v = juce::JSON::parse(entry.file);
                        v.getDynamicObject()->setProperty("category", value);
                        if (!write(entry.file, v, error)) { rollback(backups, error); return false; }
                    }
            juce::Array<juce::var> values;
            for (const auto& cat : cats) values.add(cat);
            if (!write(categoryFile(), juce::var(values), error)) { rollback(backups, error); return false; }
        }
        else
        {
            const auto file = find(name);
            if (!file.existsAsFile()) return fail(error, "No saved user component named " + name + ".");
            if (action == "delete_component")
            {
                if (!file.deleteFile()) return fail(error, "Could not delete " + file.getFullPathName());
            }
            else
            {
                auto v = juce::JSON::parse(file);
                if (action == "rename_component")
                {
                    if (value.isEmpty() || (find(value).existsAsFile() && find(value) != file)) return fail(error, "Choose a nonempty, unused component name.");
                    v.getDynamicObject()->setProperty("name", value);
                }
                else if (action == "move_component")
                {
                    if (value.isNotEmpty() && !cats.contains(value, true)) return fail(error, "Destination category does not exist.");
                    if (value.isNotEmpty()) value = cats[cats.indexOf(value, true)];
                    // Retain even legacy categories after their last template moves out.
                    juce::Array<juce::var> values;
                    for (const auto& cat : cats) values.add(cat);
                    if (!write(categoryFile(), juce::var(values), error)) return false;
                    v.getDynamicObject()->setProperty("category", value);
                }
                else return fail(error, "Unsupported user library operation.");
                if (!write(file, v, error)) return false;
            }
        }
        changed();
        return true;
    }
private:
    juce::File folder;
    juce::File categoryFile() const { return folder.getChildFile("categories.json"); }
    static bool fail(juce::String& error, const juce::String& message) { error = message; return false; }
    static bool write(const juce::File& file, const juce::var& value, juce::String& error)
    {
        if (!file.getParentDirectory().createDirectory()) return fail(error, "Could not create library folder: " + file.getParentDirectory().getFullPathName());
        const auto text = juce::JSON::toString(value, true);
        juce::TemporaryFile temporary(file);
        if (!temporary.getFile().replaceWithText(text) || temporary.getFile().loadFileAsString() != text
            || !temporary.overwriteTargetFileWithTemporary()) return fail(error, "Could not store " + file.getFullPathName());
        if (file.loadFileAsString() != text) return fail(error, "Library file could not be verified: " + file.getFullPathName());
        return true;
    }
    static void rollback(const std::vector<std::pair<juce::File, juce::String>>& backups, juce::String& error)
    {
        for (const auto& [file, text] : backups)
            if (!file.replaceWithText(text)) error += " Rollback failed for " + file.getFullPathName();
    }
    void changed() { if (onChanged) onChanged(); }
};
inline Library& collection() { static Library library; return library; }
}
