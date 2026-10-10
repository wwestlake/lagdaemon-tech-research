#include "ProjectStore.h"

#include "Preferences.h"

namespace project_store
{
namespace
{
juce::File appDataDirectory()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("DjehutiElectronicsLab");
}

juce::File recentFile()
{
    return appDataDirectory().getChildFile("recent_projects.json");
}

juce::String safeFileName(const juce::String& name)
{
    return juce::File::createLegalFileName(name.trim()).trim();
}

bool validName(const juce::String& name, const juce::String& what, juce::String& error)
{
    if (name.trim().isEmpty() || safeFileName(name).isEmpty())
    {
        error = what + " needs a name.";
        return false;
    }
    return true;
}

juce::String relativeDiagramPath(const juce::String& name)
{
    return "diagrams/" + safeFileName(name) + ".diagram.json";
}
}

const DiagramEntry* Project::find(const juce::String& diagramName) const
{
    for (const auto& entry : diagrams)
        if (entry.name.equalsIgnoreCase(diagramName.trim()))
            return &entry;
    return nullptr;
}

juce::File defaultProjectsRoot()
{
    return juce::File(prefs::get("projects.default_folder"));
}

juce::File manifestFile(const Project& project)
{
    return project.folder.getChildFile("project.json");
}

juce::File diagramFile(const Project& project, const juce::String& diagramName)
{
    if (const auto* entry = project.find(diagramName))
        return project.folder.getChildFile(entry->file);
    return project.folder.getChildFile(relativeDiagramPath(diagramName));
}

juce::File outputsDirectory(const Project& project, const juce::String& diagramName)
{
    return project.folder.getChildFile("outputs").getChildFile(safeFileName(diagramName));
}

juce::File memoryDirectory(const Project& project)
{
    return project.folder.getChildFile("memory");
}

juce::File programsDirectory(const Project& project)
{
    return project.folder.getChildFile("programs");
}

bool saveManifest(const Project& project, juce::String& error)
{
    auto* root = new juce::DynamicObject();
    root->setProperty("schema", "djehuti-electronics-project");
    root->setProperty("version", 1);
    root->setProperty("name", project.name);
    root->setProperty("description", project.description);
    root->setProperty("created", project.created);
    root->setProperty("lastDiagram", project.lastDiagram);
    juce::Array<juce::var> diagrams;
    for (const auto& entry : project.diagrams)
    {
        auto* object = new juce::DynamicObject();
        object->setProperty("name", entry.name);
        object->setProperty("file", entry.file);
        diagrams.add(juce::var(object));
    }
    root->setProperty("diagrams", diagrams);

    if (!manifestFile(project).replaceWithText(juce::JSON::toString(juce::var(root), false)))
    {
        error = "Could not write " + manifestFile(project).getFullPathName();
        return false;
    }
    return true;
}

bool createProject(const juce::File& location, const juce::String& name, Project& out, juce::String& error)
{
    if (!validName(name, "A project", error))
        return false;
    const auto folder = location.getChildFile(safeFileName(name));
    if (folder.getChildFile("project.json").existsAsFile())
    {
        error = "A project already exists at " + folder.getFullPathName();
        return false;
    }
    for (const auto* sub : { "diagrams", "outputs", "memory" })
        if (!folder.getChildFile(sub).createDirectory())
        {
            error = "Could not create " + folder.getChildFile(sub).getFullPathName();
            return false;
        }

    Project project;
    project.folder = folder;
    project.name = name.trim();
    project.created = juce::Time::getCurrentTime().toISO8601(true);
    if (!saveManifest(project, error))
        return false;
    out = project;
    rememberProject(folder);
    return true;
}

bool loadProject(const juce::File& folderOrManifest, Project& out, juce::String& error)
{
    const auto manifest = folderOrManifest.isDirectory() ? folderOrManifest.getChildFile("project.json") : folderOrManifest;
    if (!manifest.existsAsFile())
    {
        error = "No project.json in " + folderOrManifest.getFullPathName();
        return false;
    }
    const auto parsed = juce::JSON::parse(manifest.loadFileAsString());
    if (!parsed.isObject() || parsed.getProperty("schema", {}).toString() != "djehuti-electronics-project")
    {
        error = manifest.getFullPathName() + " is not a Djehuti Electronics project.";
        return false;
    }

    Project project;
    project.folder = manifest.getParentDirectory();
    project.name = parsed.getProperty("name", project.folder.getFileName()).toString();
    project.description = parsed.getProperty("description", {}).toString();
    project.created = parsed.getProperty("created", {}).toString();
    project.lastDiagram = parsed.getProperty("lastDiagram", {}).toString();
    if (const auto* diagrams = parsed.getProperty("diagrams", {}).getArray())
        for (const auto& entry : *diagrams)
            project.diagrams.push_back({ entry.getProperty("name", {}).toString(), entry.getProperty("file", {}).toString() });
            
    juce::File diagramsFolder = project.folder.getChildFile("diagrams");
    if (diagramsFolder.isDirectory()) {
        for (const auto& f : diagramsFolder.findChildFiles(juce::File::findFiles, false, "*.diagram.json")) {
            bool found = false;
            for (const auto& d : project.diagrams) {
                if (project.folder.getChildFile(d.file) == f) { found = true; break; }
            }
            if (!found) {
                juce::String name = f.getFileNameWithoutExtension().upToLastOccurrenceOf(".diagram", false, false);
                project.diagrams.push_back({name, "diagrams/" + f.getFileName()});
            }
        }
    }
            
    out = project;
    return true;
}

bool renameProject(Project& project, const juce::String& newName, juce::String& error)
{
    if (!validName(newName, "A project", error))
        return false;
    const auto target = project.folder.getParentDirectory().getChildFile(safeFileName(newName));
    if (target != project.folder)
    {
        if (target.exists())
        {
            error = "There is already a folder at " + target.getFullPathName();
            return false;
        }
        if (!project.folder.moveFileTo(target))
        {
            error = "Could not rename the project folder (is a file in it open elsewhere?).";
            return false;
        }
        project.folder = target;
    }
    project.name = newName.trim();
    rememberProject(project.folder);
    return saveManifest(project, error);
}

bool writeDiagram(const Project& project, const juce::String& name, const juce::String& contentJson, juce::String& error)
{
    const auto file = diagramFile(project, name);
    file.getParentDirectory().createDirectory();
    if (!file.replaceWithText(contentJson))
    {
        error = "Could not write " + file.getFullPathName();
        return false;
    }
    return true;
}

bool readDiagram(const Project& project, const juce::String& name, juce::String& contentJson, juce::String& error)
{
    if (project.find(name) == nullptr)
    {
        error = "Project " + project.name + " has no diagram named " + name + ".";
        return false;
    }
    const auto file = diagramFile(project, name);
    if (!file.existsAsFile())
    {
        error = "Diagram file is missing: " + file.getFullPathName();
        return false;
    }
    contentJson = file.loadFileAsString();
    return true;
}

bool createDiagram(Project& project, const juce::String& name, const juce::String& contentJson, juce::String& error)
{
    if (!validName(name, "A diagram", error))
        return false;
    if (project.find(name) != nullptr || project.folder.getChildFile(relativeDiagramPath(name)).existsAsFile())
    {
        error = "Project " + project.name + " already has a diagram named " + name.trim() + ".";
        return false;
    }
    project.diagrams.push_back({ name.trim(), relativeDiagramPath(name) });
    if (!writeDiagram(project, name, contentJson, error))
    {
        project.diagrams.pop_back();
        return false;
    }
    project.lastDiagram = name.trim();
    return saveManifest(project, error);
}

bool renameDiagram(Project& project, const juce::String& oldName, const juce::String& newName, juce::String& error)
{
    if (!validName(newName, "A diagram", error))
        return false;
    auto it = std::find_if(project.diagrams.begin(), project.diagrams.end(),
                           [&](const DiagramEntry& e) { return e.name.equalsIgnoreCase(oldName.trim()); });
    if (it == project.diagrams.end())
    {
        error = "Project " + project.name + " has no diagram named " + oldName + ".";
        return false;
    }
    if (!oldName.trim().equalsIgnoreCase(newName.trim()) && project.find(newName) != nullptr)
    {
        error = "Project " + project.name + " already has a diagram named " + newName.trim() + ".";
        return false;
    }

    const auto from = project.folder.getChildFile(it->file);
    const auto toRelative = relativeDiagramPath(newName);
    const auto to = project.folder.getChildFile(toRelative);
    if (from != to && from.existsAsFile() && !from.moveFileTo(to))
    {
        error = "Could not rename " + from.getFullPathName();
        return false;
    }
    const auto outputsFrom = outputsDirectory(project, oldName);
    const auto outputsTo = outputsDirectory(project, newName);
    if (outputsFrom.isDirectory() && outputsFrom != outputsTo && !outputsTo.exists())
        outputsFrom.moveFileTo(outputsTo);

    if (project.lastDiagram.equalsIgnoreCase(it->name))
        project.lastDiagram = newName.trim();
    it->name = newName.trim();
    it->file = toRelative;
    return saveManifest(project, error);
}

bool duplicateDiagram(Project& project, const juce::String& name, const juce::String& newName, juce::String& error)
{
    juce::String content;
    if (!readDiagram(project, name, content, error))
        return false;
    return createDiagram(project, newName, content, error);
}

bool deleteDiagram(Project& project, const juce::String& name, juce::String& error)
{
    auto it = std::find_if(project.diagrams.begin(), project.diagrams.end(),
                           [&](const DiagramEntry& e) { return e.name.equalsIgnoreCase(name.trim()); });
    if (it == project.diagrams.end())
    {
        error = "Project " + project.name + " has no diagram named " + name + ".";
        return false;
    }
    const auto file = project.folder.getChildFile(it->file);
    if (file.existsAsFile())
    {
        const auto trash = project.folder.getChildFile("deleted");
        trash.createDirectory();
        const auto stamp = juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S");
        if (!file.moveFileTo(trash.getChildFile(file.getFileNameWithoutExtension() + "." + stamp + file.getFileExtension())))
        {
            error = "Could not move " + file.getFullPathName() + " to deleted\\.";
            return false;
        }
    }
    if (project.lastDiagram.equalsIgnoreCase(it->name))
        project.lastDiagram = {};
    project.diagrams.erase(it);
    return saveManifest(project, error);
}

juce::StringArray recentProjects()
{
    juce::StringArray result;
    const auto parsed = juce::JSON::parse(recentFile().loadFileAsString());
    if (const auto* list = parsed.getProperty("projects", {}).getArray())
        for (const auto& entry : *list)
            if (juce::File(entry.toString()).getChildFile("project.json").existsAsFile())
                result.addIfNotAlreadyThere(entry.toString());
    return result;
}

void rememberProject(const juce::File& folder)
{
    auto list = recentProjects();
    list.removeString(folder.getFullPathName());
    list.insert(0, folder.getFullPathName());
    while (list.size() > 12)
        list.remove(list.size() - 1);
    auto* root = new juce::DynamicObject();
    juce::Array<juce::var> projects;
    for (const auto& path : list)
        projects.add(path);
    root->setProperty("projects", projects);
    appDataDirectory().createDirectory();
    recentFile().replaceWithText(juce::JSON::toString(juce::var(root), false));
}
}
