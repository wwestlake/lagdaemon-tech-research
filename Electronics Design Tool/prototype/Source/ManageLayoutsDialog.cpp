#include "ManageLayoutsDialog.h"

ManageLayoutsDialog::ManageLayoutsDialog(WorkspaceLayoutStore& store, std::function<void()> onLayoutsChanged)
    : layoutStore(store), onLayoutsChangedCallback(std::move(onLayoutsChanged))
{
    headerLabel.setText("Saved User Layouts:", juce::dontSendNotification);
    headerLabel.setColour(juce::Label::textColourId, juce::Colour(0xffdce9ee));
    headerLabel.setFont(juce::Font(14.0f).boldened());
    addAndMakeVisible(headerLabel);

    listBox.setModel(this);
    listBox.setRowHeight(26);
    listBox.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff10161d));
    listBox.setColour(juce::ListBox::outlineColourId, juce::Colour(0xff33424d));
    addAndMakeVisible(listBox);

    emptyHintLabel.setText("No saved layouts", juce::dontSendNotification);
    emptyHintLabel.setColour(juce::Label::textColourId, juce::Colour(0xff71808c));
    emptyHintLabel.setJustificationType(juce::Justification::centred);
    emptyHintLabel.setFont(juce::Font(13.0f).italicised());
    addChildComponent(emptyHintLabel);

    for (auto* btn : { &renameButton, &deleteButton, &closeButton })
    {
        btn->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff222a33));
        btn->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff2a4153));
        btn->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffdce9ee));
        btn->setColour(juce::TextButton::textColourOnId, juce::Colour(0xffffffff));
        addAndMakeVisible(*btn);
    }

    renameButton.onClick = [this] { handleRename(); };
    deleteButton.onClick = [this] { handleDelete(); };
    closeButton.onClick = [this] {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState(0);
    };

    refreshList();
    setSize(440, 320);
}

void ManageLayoutsDialog::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff171b20));
}

void ManageLayoutsDialog::resized()
{
    auto area = getLocalBounds().reduced(16);
    headerLabel.setBounds(area.removeFromTop(24));
    area.removeFromTop(8);

    auto buttonBar = area.removeFromBottom(30);
    closeButton.setBounds(buttonBar.removeFromRight(80));
    buttonBar.removeFromRight(10);
    deleteButton.setBounds(buttonBar.removeFromRight(80));
    buttonBar.removeFromRight(10);
    renameButton.setBounds(buttonBar.removeFromRight(90));

    area.removeFromBottom(12);
    listBox.setBounds(area);
    emptyHintLabel.setBounds(area);
}

void ManageLayoutsDialog::refreshList()
{
    currentNames = layoutStore.getLayoutNames();
    listBox.updateContent();

    emptyHintLabel.setVisible(currentNames.isEmpty());
    updateButtons();
}

int ManageLayoutsDialog::getNumRows()
{
    return currentNames.size();
}

void ManageLayoutsDialog::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    if (rowIsSelected)
    {
        g.fillAll(juce::Colour(0xff2a4153));
    }
    else if (rowNumber % 2 == 1)
    {
        g.fillAll(juce::Colour(0xff141a22));
    }

    if (juce::isPositiveAndBelow(rowNumber, currentNames.size()))
    {
        g.setColour(juce::Colour(0xffdce9ee));
        g.setFont(14.0f);
        g.drawText(currentNames[rowNumber], 10, 0, width - 20, height, juce::Justification::centredLeft, true);
    }
}

void ManageLayoutsDialog::selectedRowsChanged(int)
{
    updateButtons();
}

void ManageLayoutsDialog::listBoxItemDoubleClicked(int row, const juce::MouseEvent&)
{
    if (juce::isPositiveAndBelow(row, currentNames.size()))
    {
        handleRename();
    }
}

void ManageLayoutsDialog::updateButtons()
{
    const int row = listBox.getSelectedRow();
    const bool hasSelection = (row >= 0 && row < currentNames.size());
    renameButton.setEnabled(hasSelection);
    deleteButton.setEnabled(hasSelection);
}

void ManageLayoutsDialog::handleRename()
{
    const int row = listBox.getSelectedRow();
    if (!juce::isPositiveAndBelow(row, currentNames.size()))
        return;

    const auto oldName = currentNames[row];

    auto* dialog = new juce::AlertWindow("Rename Layout", "Enter a new name for '" + oldName + "':", juce::AlertWindow::NoIcon);
    dialog->addTextEditor("name", oldName, "Name");
    dialog->addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    auto safeThis = juce::Component::SafePointer<ManageLayoutsDialog>(this);

    dialog->enterModalState(true, juce::ModalCallbackFunction::create([safeThis, oldName, dialog](int result) {
        std::unique_ptr<juce::AlertWindow> owner(dialog);
        if (safeThis == nullptr || result != 1)
            return;

        const auto newName = owner->getTextEditor("name")->getText().trim();
        if (newName.isEmpty())
        {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Rename Layout", "Layout name cannot be empty.");
            return;
        }

        if (WorkspaceLayoutStore::isBuiltInPresetName(newName))
        {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Rename Layout", "Cannot use built-in layout preset name '" + newName + "'.");
            return;
        }

        if (newName.equalsIgnoreCase(oldName))
        {
            if (newName != oldName)
            {
                juce::String err;
                safeThis->layoutStore.renameLayout(oldName, newName, err);
                safeThis->refreshList();
                if (safeThis->onLayoutsChangedCallback)
                    safeThis->onLayoutsChangedCallback();
            }
            return;
        }

        if (safeThis->layoutStore.hasLayout(newName))
        {
            juce::AlertWindow::showAsync(
                juce::MessageBoxOptions()
                    .withIconType(juce::MessageBoxIconType::QuestionIcon)
                    .withTitle("Replace Layout")
                    .withMessage("A saved layout named '" + newName + "' already exists. Do you want to replace it?")
                    .withButton("Replace")
                    .withButton("Cancel"),
                [safeThis, oldName, newName](int replaceResult) {
                    if (safeThis == nullptr || replaceResult != 1)
                        return;

                    const auto oldData = safeThis->layoutStore.getLayoutData(oldName);
                    juce::String err;
                    safeThis->layoutStore.deleteLayout(oldName, err);
                    safeThis->layoutStore.saveLayout(newName, oldData, err);
                    safeThis->refreshList();
                    if (safeThis->onLayoutsChangedCallback)
                        safeThis->onLayoutsChangedCallback();
                });
            return;
        }

        juce::String err;
        if (safeThis->layoutStore.renameLayout(oldName, newName, err))
        {
            safeThis->refreshList();
            for (int i = 0; i < safeThis->currentNames.size(); ++i)
            {
                if (safeThis->currentNames[i].equalsIgnoreCase(newName))
                {
                    safeThis->listBox.selectRow(i);
                    break;
                }
            }
            if (safeThis->onLayoutsChangedCallback)
                safeThis->onLayoutsChangedCallback();
        }
        else
        {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Rename Layout", "Failed to rename layout: " + err);
        }
    }), true);
}

void ManageLayoutsDialog::handleDelete()
{
    const int row = listBox.getSelectedRow();
    if (!juce::isPositiveAndBelow(row, currentNames.size()))
        return;

    const auto name = currentNames[row];
    auto safeThis = juce::Component::SafePointer<ManageLayoutsDialog>(this);

    juce::AlertWindow::showAsync(
        juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::QuestionIcon)
            .withTitle("Delete Layout")
            .withMessage("Are you sure you want to delete the layout '" + name + "'?")
            .withButton("Delete")
            .withButton("Cancel"),
        [safeThis, name, row](int result) {
            if (safeThis == nullptr || result != 1)
                return;

            juce::String err;
            if (safeThis->layoutStore.deleteLayout(name, err))
            {
                safeThis->refreshList();
                const int newCount = safeThis->currentNames.size();
                if (newCount > 0)
                    safeThis->listBox.selectRow(juce::jmin(row, newCount - 1));

                if (safeThis->onLayoutsChangedCallback)
                    safeThis->onLayoutsChangedCallback();
            }
            else
            {
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Delete Layout", "Failed to delete layout: " + err);
            }
        });
}
