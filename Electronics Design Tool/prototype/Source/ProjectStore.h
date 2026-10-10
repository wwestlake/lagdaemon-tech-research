#pragma once

#include <JuceHeader.h>

#include <vector>

// Named projects that contain named diagrams.
//
//   <location>\<Project Name>\
//     project.json                     name, description, created, diagram list
//     diagrams\<Diagram Name>.diagram.json
//     outputs\<Diagram Name>\          netlists, ERC, simulations, images
//     memory\                          the agent's notes for this project
//     programs\<Name>.frnode.json      node programs (Node Designer)
//     deleted\                         diagrams removed from the project
//
// File operations only; the workbench owns the UI and the open diagram.
namespace project_store
{
struct DiagramEntry
{
    juce::String name;
    juce::String file; // relative to the project folder
};

struct Project
{
    juce::File folder;
    juce::String name;
    juce::String description;
    juce::String created; // ISO 8601
    juce::String lastDiagram;
    std::vector<DiagramEntry> diagrams;

    bool isOpen() const { return folder != juce::File(); }
    const DiagramEntry* find(const juce::String& diagramName) const;
};

juce::File defaultProjectsRoot();
juce::File manifestFile(const Project& project);
juce::File diagramFile(const Project& project, const juce::String& diagramName);
juce::File outputsDirectory(const Project& project, const juce::String& diagramName);
juce::File memoryDirectory(const Project& project);
// Node programs (FRust node schematics) of the project: programs\<Name>.frnode.json.
juce::File programsDirectory(const Project& project);

// Creates <location>\<name>\ with an empty diagram list. Fails if that
// folder already holds a project.
bool createProject(const juce::File& location, const juce::String& name, Project& out, juce::String& error);
// Accepts the project folder or its project.json.
bool loadProject(const juce::File& folderOrManifest, Project& out, juce::String& error);
bool saveManifest(const Project& project, juce::String& error);
bool renameProject(Project& project, const juce::String& newName, juce::String& error);

bool createDiagram(Project& project, const juce::String& name, const juce::String& contentJson, juce::String& error);
bool writeDiagram(const Project& project, const juce::String& name, const juce::String& contentJson, juce::String& error);
bool readDiagram(const Project& project, const juce::String& name, juce::String& contentJson, juce::String& error);
bool renameDiagram(Project& project, const juce::String& oldName, const juce::String& newName, juce::String& error);
bool duplicateDiagram(Project& project, const juce::String& name, const juce::String& newName, juce::String& error);
// Moves the diagram file into the project's deleted\ folder; nothing is erased.
bool deleteDiagram(Project& project, const juce::String& name, juce::String& error);

// Most recently opened project folders, newest first.
juce::StringArray recentProjects();
void rememberProject(const juce::File& folder);
}
