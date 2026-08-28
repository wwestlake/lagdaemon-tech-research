#pragma once

#include <JuceHeader.h>

#include "Views/CelCodeTokeniser.h"

namespace ce {

// GS10: a read-only "View Generated Code" pane -- the literal payoff of
// GS9's design decision to make the graph emit real, readable .cel text
// rather than a hidden intermediate representation (see
// Language/include/lang/nodegen/graph_to_source.h's own comment).
// Reuses CelCodeTokeniser exactly as ScriptPanel does, just with the
// editor set read-only.
class GeneratedCodeView final : public juce::Component {
public:
    GeneratedCodeView();

    void SetText(const juce::String& text);

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    juce::Label titleLabel_{ {}, "Generated Code" };
    juce::CodeDocument document_;
    CelCodeTokeniser tokeniser_;
    juce::CodeEditorComponent editor_{ document_, &tokeniser_ };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GeneratedCodeView)
};

} // namespace ce
