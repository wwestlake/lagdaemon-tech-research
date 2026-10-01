#pragma once

#include <JuceHeader.h>
#include <CreationDock/DockManager.h>

class ElectronicsWorkbench final : public juce::Component,
                                   public juce::DragAndDropContainer,
                                   public juce::MenuBarModel
{
public:
    ElectronicsWorkbench();
    ~ElectronicsWorkbench() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int menuIndex, const juce::String& menuName) override;
    void menuItemSelected(int menuItemID, int topLevelMenuIndex) override;

private:
    enum MenuIds
    {
        newProject = 100,
        openProject,
        saveProject,
        resetLayout,
        importComponent,
        runErc,
        runOperatingPoint,
        runTransient,
        runCompiledPreview,
        openAgentSettings,
        openResearchSpec
    };

    juce::File layoutFile() const;
    void appendLog(const juce::String& text);
    void resetResearchState();
    void showSpecDocument();

    std::unique_ptr<juce::MenuBarComponent> menuBar;
    std::unique_ptr<CreationDock::DockManager> dockManager;

    juce::Label titleLabel;
    juce::Label statusLabel;
    juce::TextButton newButton { "New" };
    juce::TextButton ercButton { "ERC" };
    juce::TextButton transientButton { "Transient" };
    juce::TextButton compileButton { "Compile Preview" };

    juce::TextEditor* logConsole = nullptr;
    juce::String selectedSymbolId = "resistor";

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ElectronicsWorkbench)
};
