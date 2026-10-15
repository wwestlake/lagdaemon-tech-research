#pragma once

#include <JuceHeader.h>
#include "WorkspaceLayoutStore.h"
#include <functional>

class ManageLayoutsDialog : public juce::Component,
                            public juce::ListBoxModel
{
public:
    ManageLayoutsDialog(WorkspaceLayoutStore& store, std::function<void()> onLayoutsChanged = nullptr);
    ~ManageLayoutsDialog() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // ListBoxModel overrides
    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    void selectedRowsChanged(int lastRowSelected) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;

    void refreshList();

private:
    WorkspaceLayoutStore& layoutStore;
    std::function<void()> onLayoutsChangedCallback;

    juce::Label headerLabel;
    juce::ListBox listBox;
    juce::TextButton renameButton { "Rename..." };
    juce::TextButton deleteButton { "Delete" };
    juce::TextButton closeButton { "Close" };
    juce::Label emptyHintLabel;

    juce::StringArray currentNames;

    void handleRename();
    void handleDelete();
    void updateButtons();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ManageLayoutsDialog)
};
