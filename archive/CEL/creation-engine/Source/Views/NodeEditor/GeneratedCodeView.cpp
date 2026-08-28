#include "Views/NodeEditor/GeneratedCodeView.h"

namespace ce {

GeneratedCodeView::GeneratedCodeView() {
    titleLabel_.setFont(juce::Font(juce::FontOptions(15.0f)).boldened());
    titleLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(titleLabel_);

    editor_.setColourScheme(tokeniser_.getDefaultColourScheme());
    editor_.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 13.0f, 0)));
    editor_.setReadOnly(true);
    addAndMakeVisible(editor_);
}

void GeneratedCodeView::SetText(const juce::String& text) {
    document_.replaceAllContent(text);
    document_.clearUndoHistory();
    // Otherwise a stale horizontal/vertical scroll position from the
    // PREVIOUS (longer) content can leave a few leftover glyph pixels
    // unrepainted at the old scroll offset when the new content is
    // shorter or empty.
    editor_.moveCaretToTop(false);
}

void GeneratedCodeView::resized() {
    auto bounds = getLocalBounds();
    titleLabel_.setBounds(bounds.removeFromTop(24));
    editor_.setBounds(bounds);
}

void GeneratedCodeView::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff15181d));
}

} // namespace ce
