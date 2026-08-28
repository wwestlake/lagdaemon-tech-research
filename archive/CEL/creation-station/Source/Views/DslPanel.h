#pragma once

#include <JuceHeader.h>

class DslPanel final : public juce::Component
{
public:
    DslPanel();

    void setSourceText(const juce::String& text);
    juce::String getSourceText() const;

    // Reads a previously exported/saved .cel source file back into the
    // editor, recompiles it, and shows a short load summary. CEL source is
    // re-parsed on load exactly like it is on every edit -- there's no
    // separate compiled-artifact format to summarize.
    void loadSourceFromFile(const juce::File& sourceFile);

    std::function<void(const juce::String& sourceText, const juce::String& suggestedName)> onSourceExportRequested;
    std::function<void(const juce::String& sourceText, const juce::String& suggestedName)> onSourceSaveToLibraryRequested;
    std::function<void()> onSourceLoadRequested;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void compileSource();
    juce::String makeSuggestedFileName() const;

    juce::Label headerLabel;
    juce::TextEditor sourceEditor;
    juce::TextEditor outputEditor;
    juce::TextButton compileButton { "Compile CEL" };
    juce::TextButton exportButton { "Export Source" };
    juce::TextButton saveButton { "Save To Library" };
    juce::TextButton loadButton { "Load Source" };
    bool lastCompileSucceeded = false;
};
