#include "juce_docking/DockPanel.h"

namespace juce_docking {

DockPanel::DockPanel(const juce::String& panelID, const juce::String& title, std::unique_ptr<juce::Component> contentComponent)
    : id(panelID), panelTitle(title), content(std::move(contentComponent))
{
    if (content) addAndMakeVisible(*content);
}

void DockPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff181818));
}

void DockPanel::resized()
{
    if (content) content->setBounds(getLocalBounds());
}

void DockPanel::setTitle(const juce::String& newTitle)
{
    panelTitle = newTitle;
    if (onTitleChanged) onTitleChanged();
}

} // namespace juce_docking
