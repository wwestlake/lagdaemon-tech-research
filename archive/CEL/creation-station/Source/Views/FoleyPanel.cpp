#include "FoleyPanel.h"

#include <lang/nodegen/foley_graph_to_source.h>

FoleyPanel::FoleyPanel()
{
    titleLabel_.setFont(juce::Font(juce::FontOptions(18.0f)).boldened());
    titleLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(titleLabel_);

    hintLabel_.setFont(juce::Font(juce::FontOptions(12.0f)));
    hintLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff8ea0b7));
    addAndMakeVisible(hintLabel_);

    generateButton_.onClick = [this] { generateSource(); };
    addAndMakeVisible(generateButton_);

    setupMenuButton_.onClick = [this] { showSetupMenu(); };
    setupMenuButton_.setTooltip("Save or load this node setup as a named project asset");
    addAndMakeVisible(setupMenuButton_);

    statusLabel_.setFont(juce::Font(juce::FontOptions(12.0f)));
    statusLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff8ea0b7));
    addAndMakeVisible(statusLabel_);

    sourceView_.setMultiLine(true);
    sourceView_.setReadOnly(true);
    sourceView_.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain)));
    sourceView_.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff0e1218));
    sourceView_.setColour(juce::TextEditor::textColourId, juce::Colour(0xffd7e3f0));
    sourceView_.setText("Generated CEL source will appear here.", juce::dontSendNotification);
    addAndMakeVisible(sourceView_);

    addAndMakeVisible(palette_);

    graphComponent_.onSelectionChanged = [this](ce::node_system::NodeId id)
    {
        inspector_.SetSelectedNode(id);
    };
    addAndMakeVisible(graphComponent_);

    addAndMakeVisible(inspector_);
}

void FoleyPanel::generateSource()
{
    auto result = ce::lang::nodegen::foley::GenerateFoleySource(graph_, registry_);
    if (result.ok)
    {
        sourceView_.setText(result.source, juce::dontSendNotification);
        statusLabel_.setText("Generated OK.", juce::dontSendNotification);
        statusLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff67e8a5));
    }
    else
    {
        juce::String combined;
        for (const auto& error : result.errors)
            combined << error << "\n";
        sourceView_.setText(combined, juce::dontSendNotification);
        statusLabel_.setText("Generation failed - see errors below.", juce::dontSendNotification);
        statusLabel_.setColour(juce::Label::textColourId, juce::Colour(0xffff6b6b));
    }
}

juce::String FoleyPanel::serializeGraph() const
{
    return ce::node_system::SerializeGraph(graph_);
}

bool FoleyPanel::loadGraph(const juce::String& celgText, juce::String& errorMessage)
{
    std::string errorOut;
    auto loaded = ce::node_system::DeserializeGraph(celgText.toStdString(), errorOut);
    if (loaded == nullptr)
    {
        errorMessage = juce::String(errorOut);
        return false;
    }

    graph_ = std::move(*loaded);
    graphComponent_.GraphReplaced();
    return true;
}

void FoleyPanel::showSetupMenu()
{
    juce::PopupMenu menu;
    menu.addItem(1, "Save Setup...");
    menu.addItem(2, "Load Setup...");

    auto area = setupMenuButton_.getScreenBounds();
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(area),
                       [this](int result)
                       {
                           if (result == 1)
                           {
                               auto* prompt = new juce::AlertWindow("Save Foley Setup",
                                                                    "Enter a name for this node setup:",
                                                                    juce::MessageBoxIconType::QuestionIcon);
                               prompt->addTextEditor("setupName", "Foley Setup");
                               prompt->addButton("Save", 1);
                               prompt->addButton("Cancel", 0);

                               auto options = juce::Component::SafePointer<FoleyPanel>(this);
                               prompt->enterModalState(true, juce::ModalCallbackFunction::create([options, prompt](int promptResult) mutable
                               {
                                   std::unique_ptr<juce::AlertWindow> dialog(prompt);
                                   if (promptResult != 1 || options == nullptr)
                                       return;

                                   auto name = dialog->getTextEditorContents("setupName").trim();
                                   if (name.isEmpty())
                                       return;

                                   if (options->onSetupSaveRequested)
                                       options->onSetupSaveRequested(name);
                               }), true);
                           }
                           else if (result == 2)
                           {
                               if (onSetupLoadRequested)
                                   onSetupLoadRequested();
                           }
                       });
}

void FoleyPanel::resized()
{
    auto area = getLocalBounds().reduced(16, 12);

    auto header = area.removeFromTop(48);
    titleLabel_.setBounds(header.removeFromTop(24));
    hintLabel_.setBounds(header);

    area.removeFromTop(8);

    auto footer = area.removeFromBottom(160);
    auto footerHeader = footer.removeFromTop(24);
    generateButton_.setBounds(footerHeader.removeFromLeft(130));
    footerHeader.removeFromLeft(8);
    setupMenuButton_.setBounds(footerHeader.removeFromLeft(90));
    footerHeader.removeFromLeft(8);
    statusLabel_.setBounds(footerHeader);
    footer.removeFromTop(4);
    sourceView_.setBounds(footer);

    area.removeFromBottom(8);

    auto paletteArea = area.removeFromLeft(180);
    palette_.setBounds(paletteArea);
    area.removeFromLeft(8);

    auto inspectorArea = area.removeFromRight(220);
    inspector_.setBounds(inspectorArea);
    area.removeFromRight(8);

    graphComponent_.setBounds(area);
}

void FoleyPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff10141a));
}
