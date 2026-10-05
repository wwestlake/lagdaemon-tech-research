#pragma once

#include <JuceHeader.h>

#include "FrustEngine.h"

#include <atomic>
#include <memory>

// Write Frust and run it inside the app. The source is compiled in memory by
// the embedded Frust compiler; `run()` is called and what it returns (after
// any print_line output) is shown, or the compile diagnostics with their lines.
// The agent's frust_check / frust_run tools go through the same panel.
class FrustPanel final : public juce::Component
{
public:
    FrustPanel();
    ~FrustPanel() override;

    void setSource(const juce::String& source);
    juce::String getSource() const;

    // Compiles and runs on the calling thread and shows the result (agent tools).
    frust_engine::Result runNow(const juce::String& script);
    frust_engine::Result checkNow(const juce::String& script);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void runAsync();
    void show(const frust_engine::Result& result, bool ran);
    void saveSource() const;

    juce::CodeDocument document;
    juce::CPlusPlusCodeTokeniser tokeniser;
    std::unique_ptr<juce::CodeEditorComponent> editor;
    juce::TextButton runButton { "Run" }, checkButton { "Check" };
    juce::Label status;
    juce::TextEditor output;
    juce::ThreadPool pool { 1 };
    std::atomic<bool> running { false };
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};
