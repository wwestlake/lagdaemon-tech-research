// Projects and diagrams for the workbench: File menu actions, dialogs, and
// the agent's project_* / diagram_* tools. Storage lives in ProjectStore.

#include "ElectronicsWorkbench.h"
#include "ElectronicsKnowledge.h"
#include "Preferences.h"

namespace
{
juce::String quoteJson(const juce::String& text)
{
    return juce::JSON::toString(juce::var(text));
}

// Name + storage location, with a folder picker.
class NewProjectDialog final : public juce::Component
{
public:
    std::function<void(const juce::String&, const juce::File&)> onCreate;

    NewProjectDialog()
    {
        for (auto* label : { &nameLabel, &locationLabel })
        {
            label->setColour(juce::Label::textColourId, juce::Colour(0xffdce9ee));
            addAndMakeVisible(*label);
        }
        for (auto* editor : { &name, &location })
        {
            editor->setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff10161d));
            editor->setColour(juce::TextEditor::textColourId, juce::Colour(0xffdce9ee));
            editor->setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff33424d));
            addAndMakeVisible(*editor);
        }
        name.setTextToShowWhenEmpty("Project name", juce::Colour(0xff71808c));
        location.setText(project_store::defaultProjectsRoot().getFullPathName(), false);
        hint.setColour(juce::Label::textColourId, juce::Colour(0xff93a7b0));
        hint.setFont(juce::Font(12.0f));
        addAndMakeVisible(hint);
        for (auto* button : { &browse, &create, &cancel })
            addAndMakeVisible(*button);

        browse.onClick = [this] {
            chooser = std::make_unique<juce::FileChooser>("Choose where to store the project", juce::File(location.getText()));
            chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                 [this](const juce::FileChooser& fc) {
                                     if (fc.getResult() != juce::File())
                                         location.setText(fc.getResult().getFullPathName(), false);
                                     updateHint();
                                 });
        };
        create.onClick = [this] {
            if (name.getText().trim().isEmpty())
            {
                name.grabKeyboardFocus();
                return;
            }
            const auto chosenName = name.getText().trim();
            const auto chosenLocation = juce::File(location.getText().trim());
            if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
                window->exitModalState(1);
            if (onCreate)
                onCreate(chosenName, chosenLocation);
        };
        cancel.onClick = [this] {
            if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
                window->exitModalState(0);
        };
        name.onTextChange = [this] { updateHint(); };
        location.onTextChange = [this] { updateHint(); };
        updateHint();
        setSize(560, 196);
    }

    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff171b20)); }

    void resized() override
    {
        auto area = getLocalBounds().reduced(16);
        auto row = [&](juce::Label& label, juce::TextEditor& editor, juce::Button* extra) {
            auto line = area.removeFromTop(28);
            label.setBounds(line.removeFromLeft(80));
            if (extra != nullptr)
                extra->setBounds(line.removeFromRight(90).reduced(4, 0));
            editor.setBounds(line);
            area.removeFromTop(8);
        };
        row(nameLabel, name, nullptr);
        row(locationLabel, location, &browse);
        hint.setBounds(area.removeFromTop(36));
        auto buttons = area.removeFromBottom(30);
        cancel.setBounds(buttons.removeFromRight(100));
        buttons.removeFromRight(8);
        create.setBounds(buttons.removeFromRight(100));
    }

private:
    void updateHint()
    {
        const auto folder = juce::File(location.getText().trim()).getChildFile(juce::File::createLegalFileName(name.getText().trim()));
        hint.setText("Creates " + folder.getFullPathName(), juce::dontSendNotification);
    }

    juce::Label nameLabel { {}, "Name" };
    juce::Label locationLabel { {}, "Location" };
    juce::Label hint;
    juce::TextEditor name, location;
    juce::TextButton browse { "Browse..." }, create { "Create" }, cancel { "Cancel" };
    std::unique_ptr<juce::FileChooser> chooser;
};
}

// ---- state ----------------------------------------------------------------

bool ElectronicsWorkbench::hasUnsavedChanges() const
{
    return getCircuitJson != nullptr && project.isOpen() && currentDiagram.isNotEmpty()
        && getCircuitJson() != lastSavedJson;
}

void ElectronicsWorkbench::updateProjectTitle()
{
    juce::String title = "Djehuti Electronics Lab";
    if (project.isOpen())
        title << " - " << project.name << (currentDiagram.isNotEmpty() ? " > " + currentDiagram : juce::String());
    if (auto* top = getTopLevelComponent())
        top->setName(title);
    titleLabel.setText(project.isOpen() ? project.name + (currentDiagram.isNotEmpty() ? " > " + currentDiagram : juce::String())
                                        : juce::String("Djehuti Electronics Lab"),
                       juce::dontSendNotification);
    menuItemsChanged();
}

void ElectronicsWorkbench::useProjectMemory()
{
    electronics_knowledge::setProjectMemoryDirectory(project.isOpen() ? project_store::memoryDirectory(project) : juce::File());
}

// ---- operations (shared by menu and agent tools) ---------------------------

void ElectronicsWorkbench::autoSaveCurrentDiagram()
{
    juce::String error;
    if (hasUnsavedChanges() && !saveDiagram(error))
        appendLog("Could not save " + currentDiagram + ": " + error);
}

bool ElectronicsWorkbench::openProjectFolder(const juce::File& folder, juce::String& error)
{
    project_store::Project loaded;
    if (!project_store::loadProject(folder, loaded, error))
        return false;
    autoSaveCurrentDiagram();
    closeFloatingInstrumentWindows();
    project = loaded;
    project_store::rememberProject(project.folder);
    currentDiagram = {};
    useProjectMemory();
    if (resetCircuit != nullptr) resetCircuit();
    lastSavedJson = getCircuitJson != nullptr ? getCircuitJson() : juce::String();

    auto diagramToOpen = project.lastDiagram;
    if (project.find(diagramToOpen) == nullptr && !project.diagrams.empty())
        diagramToOpen = project.diagrams.front().name;
    if (diagramToOpen.isNotEmpty())
    {
        juce::String diagramError;
        if (!openDiagram(diagramToOpen, diagramError))
            appendLog("Could not open diagram " + diagramToOpen + ": " + diagramError);
    }
    updateProjectTitle();
    appendLog("Opened project " + project.name + " (" + project.folder.getFullPathName() + ").");
    return true;
}

bool ElectronicsWorkbench::createNewProject(const juce::File& location, const juce::String& name, juce::String& error)
{
    project_store::Project created;
    if (!project_store::createProject(location, name, created, error))
        return false;
    autoSaveCurrentDiagram();
    closeFloatingInstrumentWindows();
    project = created;
    currentDiagram = {};
    useProjectMemory();
    if (resetCircuit != nullptr) resetCircuit();
    lastSavedJson = getCircuitJson != nullptr ? getCircuitJson() : juce::String();
    updateProjectTitle();
    appendLog("Created project " + project.name + " at " + project.folder.getFullPathName() + ".");
    return true;
}

bool ElectronicsWorkbench::openDiagram(const juce::String& name, juce::String& error)
{
    if (!project.isOpen())
    {
        error = "Open or create a project first.";
        return false;
    }
    juce::String content;
    if (!project_store::readDiagram(project, name, content, error))
        return false;
    if (!currentDiagram.equalsIgnoreCase(name))
        autoSaveCurrentDiagram();
    closeFloatingInstrumentWindows();
    if (loadCircuitJson == nullptr || !loadCircuitJson(content, error))
        return false;
    currentDiagram = project.find(name)->name;
    lastSavedJson = getCircuitJson();
    project.lastDiagram = currentDiagram;
    juce::String manifestError;
    project_store::saveManifest(project, manifestError);
    updateProjectTitle();
    appendLog("Opened diagram " + currentDiagram + ".");
    return true;
}

bool ElectronicsWorkbench::createNewDiagram(const juce::String& name, juce::String& error)
{
    if (!project.isOpen())
    {
        error = "Open or create a project first.";
        return false;
    }
    autoSaveCurrentDiagram();
    closeFloatingInstrumentWindows();
    if (resetCircuit != nullptr) resetCircuit();
    const auto blank = getCircuitJson != nullptr ? getCircuitJson() : juce::String("{}");
    if (!project_store::createDiagram(project, name, blank, error))
        return false;
    currentDiagram = project.diagrams.back().name;
    lastSavedJson = blank;
    updateProjectTitle();
    appendLog("Created diagram " + currentDiagram + " in " + project.name + ".");
    return true;
}

bool ElectronicsWorkbench::saveDiagram(juce::String& error)
{
    if (!project.isOpen() || currentDiagram.isEmpty())
    {
        error = "No diagram is open in a project.";
        return false;
    }
    const auto content = getCircuitJson();
    if (!project_store::writeDiagram(project, currentDiagram, content, error))
        return false;
    lastSavedJson = content;
    appendLog("Saved " + project.name + " > " + currentDiagram + ".");
    return true;
}

bool ElectronicsWorkbench::saveDiagramAs(const juce::String& name, juce::String& error)
{
    if (!project.isOpen())
    {
        error = "Open or create a project first.";
        return false;
    }
    const auto content = getCircuitJson();
    if (!project_store::createDiagram(project, name, content, error))
        return false;
    currentDiagram = project.diagrams.back().name;
    lastSavedJson = content;
    updateProjectTitle();
    appendLog("Saved as " + project.name + " > " + currentDiagram + ".");
    return true;
}

bool ElectronicsWorkbench::renameDiagram(const juce::String& name, const juce::String& newName, juce::String& error)
{
    const auto renamingCurrent = name.equalsIgnoreCase(currentDiagram);
    if (!project_store::renameDiagram(project, name, newName, error))
        return false;
    if (renamingCurrent)
        currentDiagram = newName.trim();
    updateProjectTitle();
    return true;
}

bool ElectronicsWorkbench::deleteDiagram(const juce::String& name, juce::String& error)
{
    const auto deletingCurrent = name.equalsIgnoreCase(currentDiagram);
    if (!project_store::deleteDiagram(project, name, error))
        return false;
    if (deletingCurrent)
    {
        currentDiagram = {};
        if (resetCircuit != nullptr) resetCircuit();
        lastSavedJson = getCircuitJson != nullptr ? getCircuitJson() : juce::String();
        if (!project.diagrams.empty())
        {
            juce::String openError;
            openDiagram(project.diagrams.front().name, openError);
        }
    }
    updateProjectTitle();
    return true;
}

void ElectronicsWorkbench::openMostRecentProject()
{
    const auto recent = project_store::recentProjects();
    juce::String error;
    if (prefs::isOn("projects.reopen_last") && !recent.isEmpty() && !openProjectFolder(juce::File(recent[0]), error))
        appendLog("Could not reopen the last project: " + error);
    if (!project.isOpen())
        appendLog("No project open. Use File > New Project... to create one.");
    useProjectMemory();
    updateProjectTitle();
}

// ---- dialogs ---------------------------------------------------------------

void ElectronicsWorkbench::promptForName(const juce::String& title, const juce::String& message,
                                         const juce::String& initial, std::function<void(const juce::String&)> onName)
{
    auto* dialog = new juce::AlertWindow(title, message, juce::AlertWindow::NoIcon);
    dialog->addTextEditor("name", initial, "Name");
    dialog->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    dialog->enterModalState(true, juce::ModalCallbackFunction::create([dialog, onName](int result) {
        std::unique_ptr<juce::AlertWindow> owner(dialog);
        const auto name = owner->getTextEditor("name")->getText().trim();
        if (result == 1 && name.isNotEmpty())
            onName(name);
    }), true);
}

void ElectronicsWorkbench::showNewProjectDialog()
{
    auto* dialog = new NewProjectDialog();
    auto safeThis = juce::Component::SafePointer<ElectronicsWorkbench>(this);
    dialog->onCreate = [safeThis](const juce::String& name, const juce::File& location) {
        if (safeThis == nullptr)
            return;
        juce::String error;
        if (!safeThis->createNewProject(location, name, error))
        {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "New Project", error);
            return;
        }
        safeThis->showNewDiagramDialog();
    };
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(dialog);
    options.dialogTitle = "New Project";
    options.dialogBackgroundColour = juce::Colour(0xff171b20);
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

void ElectronicsWorkbench::showOpenProjectDialog()
{
    projectChooser = std::make_unique<juce::FileChooser>("Open project (choose the project folder or its project.json)",
                                                         project_store::defaultProjectsRoot(), "project.json");
    auto safeThis = juce::Component::SafePointer<ElectronicsWorkbench>(this);
    projectChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                    | juce::FileBrowserComponent::canSelectDirectories,
                                [safeThis](const juce::FileChooser& fc) {
                                    if (safeThis == nullptr || fc.getResult() == juce::File())
                                        return;
                                    juce::String error;
                                    if (!safeThis->openProjectFolder(fc.getResult(), error))
                                        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Open Project", error);
                                });
}

void ElectronicsWorkbench::showNewDiagramDialog()
{
    if (!project.isOpen())
    {
        showNewProjectDialog();
        return;
    }
    promptForName("New Diagram", "Name the new diagram in " + project.name + ". It is saved straight away.",
                  project.diagrams.empty() ? juce::String("Main") : juce::String(),
                  [this](const juce::String& name) {
                      juce::String error;
                      if (!createNewDiagram(name, error))
                          juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "New Diagram", error);
                  });
}

void ElectronicsWorkbench::confirmCloseThen(std::function<void()> proceed)
{
    if (!hasUnsavedChanges())
    {
        proceed();
        return;
    }
    auto safeThis = juce::Component::SafePointer<ElectronicsWorkbench>(this);
    juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                     .withIconType(juce::MessageBoxIconType::QuestionIcon)
                                     .withTitle("Unsaved changes")
                                     .withMessage("Save changes to " + project.name + " > " + currentDiagram + " before closing?")
                                     .withButton("Save")
                                     .withButton("Don't Save")
                                     .withButton("Cancel"),
                                 [safeThis, proceed](int result) {
                                     if (safeThis == nullptr || result == 0)
                                         return;
                                     if (result == 1)
                                     {
                                         juce::String error;
                                         if (!safeThis->saveDiagram(error))
                                         {
                                             juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Save", error);
                                             return;
                                         }
                                     }
                                     proceed();
                                 });
}

void ElectronicsWorkbench::showOpenDiagramMenu()
{
    if (!project.isOpen())
    {
        appendLog("Open or create a project before opening a diagram.");
        return;
    }
    if (project.diagrams.empty())
    {
        appendLog("Project " + project.name + " has no saved diagrams yet.");
        return;
    }

    juce::PopupMenu menu;
    for (size_t i = 0; i < project.diagrams.size(); ++i)
        menu.addItem(diagramMenuBase + (int)i,
                     project.diagrams[i].name,
                     true,
                     project.diagrams[i].name.equalsIgnoreCase(currentDiagram));

    menu.showMenuAsync(juce::PopupMenu::Options(), [this](int result) {
        if (result < diagramMenuBase || result >= diagramMenuBase + 1000)
            return;
        const auto index = (size_t)(result - diagramMenuBase);
        if (index >= project.diagrams.size())
            return;
        juce::String error;
        if (!openDiagram(project.diagrams[index].name, error))
            appendLog("Open Diagram failed: " + error);
    });
}

void ElectronicsWorkbench::showOpenDiagramFileDialog()
{
    if (!project.isOpen())
    {
        appendLog("Open or create a project before opening a diagram.");
        return;
    }
    
    diagramChooser = std::make_unique<juce::FileChooser>("Select a Diagram to Open", project.folder.getChildFile("diagrams"), "*.diagram.json");
    diagramChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc) {
            auto result = fc.getResult();
            if (result.existsAsFile())
            {
                juce::String name = result.getFileNameWithoutExtension().upToLastOccurrenceOf(".diagram", false, false);
                if (project.find(name) == nullptr) {
                    project.diagrams.push_back({name, "diagrams/" + result.getFileName()});
                    juce::String manifestError;
                    project_store::saveManifest(project, manifestError);
                }
                juce::String error;
                if (!openDiagram(name, error))
                    appendLog("Could not open diagram: " + error);
            }
        });
}

void ElectronicsWorkbench::handleProjectMenu(int menuItemID)
{
    juce::String error;
    auto report = [this](bool ok, const juce::String& title, const juce::String& message) {
        if (!ok)
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, title, message);
    };

    if (menuItemID >= recentProjectBase && menuItemID < recentProjectBase + 100)
    {
        const auto recent = project_store::recentProjects();
        const auto index = menuItemID - recentProjectBase;
        if (index < recent.size())
            report(openProjectFolder(juce::File(recent[index]), error), "Open Project", error);
        return;
    }
    if (menuItemID >= diagramMenuBase && menuItemID < diagramMenuBase + 1000)
    {
        const auto index = (size_t)(menuItemID - diagramMenuBase);
        if (index < project.diagrams.size())
            report(openDiagram(project.diagrams[index].name, error), "Open Diagram", error);
        return;
    }

    switch (menuItemID)
    {
        case newProject: showNewProjectDialog(); break;
        case openProject: showOpenProjectDialog(); break;
        case newDiagramItem: showNewDiagramDialog(); break;
        case openDiagramFileItem: showOpenDiagramFileDialog(); break;
        case saveProject:
            if (!project.isOpen() || currentDiagram.isEmpty())
                showNewDiagramDialog();
            else
                report(saveDiagram(error), "Save", error);
            break;
        case saveDiagramAsItem:
            if (!project.isOpen()) { showNewProjectDialog(); break; }
            promptForName("Save Diagram As", "Save the current diagram under a new name in " + project.name + ".", currentDiagram + " copy",
                          [this](const juce::String& name) {
                              juce::String e;
                              if (!saveDiagramAs(name, e))
                                  juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Save As", e);
                          });
            break;
        case renameProjectItem:
            if (!project.isOpen()) break;
            promptForName("Rename Project", "The project folder is renamed too.", project.name, [this](const juce::String& name) {
                juce::String e;
                if (!project_store::renameProject(project, name, e))
                    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Rename Project", e);
                useProjectMemory();
                updateProjectTitle();
            });
            break;
        case renameDiagramItem:
            if (currentDiagram.isEmpty()) break;
            promptForName("Rename Diagram", {}, currentDiagram, [this](const juce::String& name) {
                juce::String e;
                if (!renameDiagram(currentDiagram, name, e))
                    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Rename Diagram", e);
            });
            break;
        case duplicateDiagramItem:
            if (currentDiagram.isEmpty()) break;
            promptForName("Duplicate Diagram", "Copies the saved diagram " + currentDiagram + ".", currentDiagram + " copy", [this](const juce::String& name) {
                juce::String e;
                autoSaveCurrentDiagram();
                if (!project_store::duplicateDiagram(project, currentDiagram, name, e))
                    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Duplicate Diagram", e);
                menuItemsChanged();
            });
            break;
        case deleteDiagramItem:
        {
            if (currentDiagram.isEmpty()) break;
            const auto target = currentDiagram;
            auto safeThis = juce::Component::SafePointer<ElectronicsWorkbench>(this);
            juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                             .withIconType(juce::MessageBoxIconType::WarningIcon)
                                             .withTitle("Delete Diagram")
                                             .withMessage("Remove " + target + " from " + project.name + "? Its file is moved to the project's deleted folder.")
                                             .withButton("Delete")
                                             .withButton("Cancel"),
                                         [safeThis, target](int result) {
                                             juce::String e;
                                             if (safeThis != nullptr && result == 1 && !safeThis->deleteDiagram(target, e))
                                                 juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Delete Diagram", e);
                                         });
            break;
        }
        default: break;
    }
}

void ElectronicsWorkbench::addProjectMenuItems(juce::PopupMenu& menu)
{
    menu.addItem(newProject, "New Project...");
    menu.addItem(openProject, "Open Project...");
    juce::PopupMenu recentMenu;
    const auto recent = project_store::recentProjects();
    for (int i = 0; i < recent.size(); ++i)
        recentMenu.addItem(recentProjectBase + i, juce::File(recent[i]).getFileName() + "   (" + recent[i] + ")");
    menu.addSubMenu("Recent Projects", recentMenu, !recent.isEmpty());
    menu.addItem(renameProjectItem, "Rename Project...", project.isOpen());
    menu.addSeparator();
    menu.addItem(newDiagramItem, "New Diagram...", project.isOpen());
    menu.addItem(openDiagramFileItem, "Open Diagram File...", project.isOpen());
    juce::PopupMenu diagramMenu;
    for (size_t i = 0; i < project.diagrams.size(); ++i)
        diagramMenu.addItem(diagramMenuBase + (int)i, project.diagrams[i].name, true, project.diagrams[i].name.equalsIgnoreCase(currentDiagram));
    menu.addSubMenu("Diagrams", diagramMenu, project.isOpen() && !project.diagrams.empty());
    menu.addItem(renameDiagramItem, "Rename Diagram...", currentDiagram.isNotEmpty());
    menu.addItem(duplicateDiagramItem, "Duplicate Diagram...", currentDiagram.isNotEmpty());
    menu.addItem(deleteDiagramItem, "Delete Diagram...", currentDiagram.isNotEmpty());
    menu.addSeparator();
    menu.addItem(saveProject, "Save Diagram");
    menu.addItem(saveDiagramAsItem, "Save Diagram As...", project.isOpen());
}

// ---- agent tools -------------------------------------------------------------

juce::String ElectronicsWorkbench::projectInfoJson() const
{
    juce::String text;
    text << "{ \"open\": " << (project.isOpen() ? "true" : "false");
    if (project.isOpen())
    {
        text << ", \"name\": " << quoteJson(project.name)
             << ", \"folder\": " << quoteJson(project.folder.getFullPathName())
             << ", \"currentDiagram\": " << quoteJson(currentDiagram)
             << ", \"unsavedChanges\": " << (hasUnsavedChanges() ? "true" : "false")
             << ", \"diagrams\": [";
        for (size_t i = 0; i < project.diagrams.size(); ++i)
            text << (i == 0 ? "" : ", ") << quoteJson(project.diagrams[i].name);
        text << "]";
    }
    text << " }";
    return text;
}

juce::String ElectronicsWorkbench::projectTool(const juce::String& name, const juce::var& args)
{
    auto text = [&](const char* key) { return args.getProperty(key, {}).toString().trim(); };
    auto fail = [&](const juce::String& error) {
        return "{ \"ok\": false, \"tool\": " + quoteJson(name) + ", \"error\": " + quoteJson(error) + " }";
    };
    auto ok = [&] {
        return "{ \"ok\": true, \"tool\": " + quoteJson(name) + ", \"project\": " + projectInfoJson() + " }";
    };
    juce::String error;

    if (name == "project_create")
    {
        const auto location = text("location").isNotEmpty() ? juce::File(text("location")) : project_store::defaultProjectsRoot();
        return createNewProject(location, text("name"), error) ? ok() : fail(error);
    }
    if (name == "project_open")
    {
        const auto key = text("project");
        juce::File folder(juce::File::isAbsolutePath(key) ? key : juce::String());
        if (folder == juce::File())
        {
            for (const auto& path : project_store::recentProjects())
                if (juce::File(path).getFileName().equalsIgnoreCase(key))
                    folder = juce::File(path);
            if (folder == juce::File() && project_store::defaultProjectsRoot().getChildFile(key).isDirectory())
                folder = project_store::defaultProjectsRoot().getChildFile(key);
        }
        if (folder == juce::File())
            return fail("No project named " + key + " in recent projects or " + project_store::defaultProjectsRoot().getFullPathName() + ". Pass the project folder path.");
        return openProjectFolder(folder, error) ? ok() : fail(error);
    }
    if (name == "project_list")
    {
        juce::StringArray seen;
        juce::String list = "[";
        auto add = [&](const juce::File& folder) {
            project_store::Project p;
            juce::String e;
            if (seen.contains(folder.getFullPathName()) || !project_store::loadProject(folder, p, e))
                return;
            list << (seen.isEmpty() ? "" : ", ") << "{ \"name\": " << quoteJson(p.name) << ", \"folder\": " << quoteJson(p.folder.getFullPathName())
                 << ", \"diagrams\": " << (int)p.diagrams.size() << " }";
            seen.add(folder.getFullPathName());
        };
        for (const auto& path : project_store::recentProjects())
            add(juce::File(path));
        for (const auto& child : project_store::defaultProjectsRoot().findChildFiles(juce::File::findDirectories, false))
            add(child);
        list << "]";
        return "{ \"ok\": true, \"tool\": \"project_list\", \"projects\": " + list + ", \"current\": " + projectInfoJson() + " }";
    }
    if (name == "project_info")
        return ok();
    if (name == "project_rename")
    {
        if (!project.isOpen()) return fail("No project is open.");
        if (!project_store::renameProject(project, text("name"), error)) return fail(error);
        useProjectMemory();
        updateProjectTitle();
        return ok();
    }
    if (name == "diagram_create")
        return createNewDiagram(text("name"), error) ? ok() : fail(error);
    if (name == "diagram_open")
        return openDiagram(text("name"), error) ? ok() : fail(error);
    if (name == "diagram_list")
        return project.isOpen() ? ok() : fail("No project is open.");
    if (name == "diagram_save")
        return saveDiagram(error) ? ok() : fail(error);
    if (name == "diagram_save_as")
        return saveDiagramAs(text("name"), error) ? ok() : fail(error);
    if (name == "diagram_rename")
    {
        const auto target = text("name").isNotEmpty() ? text("name") : currentDiagram;
        return renameDiagram(target, text("newName"), error) ? ok() : fail(error);
    }
    if (name == "diagram_duplicate")
    {
        const auto source = text("name").isNotEmpty() ? text("name") : currentDiagram;
        if (source.equalsIgnoreCase(currentDiagram))
            autoSaveCurrentDiagram();
        if (!project_store::duplicateDiagram(project, source, text("newName"), error))
            return fail(error);
        menuItemsChanged();
        return ok();
    }
    if (name == "diagram_delete")
        return deleteDiagram(text("name"), error) ? ok() : fail(error);

    return fail("Unknown project tool.");
}
