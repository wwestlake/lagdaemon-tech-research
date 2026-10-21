#pragma once

#include <JuceHeader.h>
#include <functional>
#include <vector>

// Configuration only: stimulus processing remains in AudioTestRender.
class FixtureAudioInputDialog final : public juce::Component
{
public:
    struct Source { juce::String refdes, label; bool eligible = true; };

    FixtureAudioInputDialog(const juce::String& path, double peak, const juce::String& selected,
                            const std::vector<Source>& sources,
                            std::function<juce::String(const juce::var&)> applyConfiguration)
        : apply(std::move(applyConfiguration))
    {
        fileLabel.setText("WAV recording", juce::dontSendNotification);
        sourceLabel.setText("Replace schematic source", juce::dontSendNotification);
        peakLabel.setText("Peak voltage (V)", juce::dontSendNotification);
        help.setText("Select the signal source (for example VIN), then wire OUT to its positive net. "
                     "DC supplies are listed for identification and cannot be used as audio inputs. "
                     "An unwired GND lead uses circuit ground.", juce::dontSendNotification);
        help.setJustificationType(juce::Justification::topLeft);
        error.setColour(juce::Label::textColourId, juce::Colour(0xffff8a65));
        error.setJustificationType(juce::Justification::topLeft);
        file.setText(path, false);
        level.setText(juce::String(peak, 6), false);
        source.addItem("Virtual source at wired node (no replacement)", 1);
        source.setSelectedId(1, juce::dontSendNotification);
        refs.add({});
        for (const auto& s : sources)
        {
            refs.add(s.refdes);
            const auto id = refs.size();
            source.addItem(s.label, id);
            source.setItemEnabled(id, s.eligible);
            if (s.refdes == selected) source.setSelectedId(id, juce::dontSendNotification);
        }
        for (juce::Component* c : std::initializer_list<juce::Component*> {
                 &fileLabel, &sourceLabel, &peakLabel, &help, &error, &file, &level, &source, &browse, &save, &cancel })
            addAndMakeVisible(c);
        browse.onClick = [this] {
            chooser = std::make_unique<juce::FileChooser>("Select WAV recording",
                juce::File::getCurrentWorkingDirectory().getChildFile(file.getText()), "*.wav", true);
            chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [safe = juce::Component::SafePointer<FixtureAudioInputDialog>(this)](const juce::FileChooser& fc) {
                    if (safe != nullptr && fc.getResult().existsAsFile())
                        safe->file.setText(fc.getResult().getFullPathName(), false);
                });
        };
        save.onClick = [this] {
            auto* config = new juce::DynamicObject();
            config->setProperty("audioFilePath", file.getText().trim());
            config->setProperty("stimulusType", "file");
            config->setProperty("peakVolts", level.getText().getDoubleValue());
            const auto ref = refs[source.getSelectedId() - 1];
            config->setProperty("isOverride", ref.isNotEmpty());
            config->setProperty("overrideRefdes", ref);
            const auto message = apply(juce::var(config));
            error.setText(message, juce::dontSendNotification);
            if (message.isEmpty()) close();
        };
        cancel.onClick = [this] { close(); };
        setSize(660, 360);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced(16);
        fileLabel.setBounds(r.removeFromTop(22));
        auto row = r.removeFromTop(28);
        browse.setBounds(row.removeFromRight(90));
        file.setBounds(row.withTrimmedRight(8));
        r.removeFromTop(10);
        sourceLabel.setBounds(r.removeFromTop(22));
        source.setBounds(r.removeFromTop(28));
        r.removeFromTop(10);
        row = r.removeFromTop(28);
        peakLabel.setBounds(row.removeFromLeft(150));
        level.setBounds(row.removeFromLeft(130));
        r.removeFromTop(10);
        help.setBounds(r.removeFromTop(62));
        row = r.removeFromBottom(28);
        cancel.setBounds(row.removeFromRight(85));
        row.removeFromRight(8);
        save.setBounds(row.removeFromRight(85));
        error.setBounds(r);
    }

    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff171b20)); }

private:
    void close() { if (auto* d = findParentComponentOfClass<juce::DialogWindow>()) d->exitModalState(0); }
    juce::Label fileLabel, sourceLabel, peakLabel, help, error;
    juce::TextEditor file, level;
    juce::ComboBox source;
    juce::StringArray refs;
    juce::TextButton browse { "Browse..." }, save { "Apply" }, cancel { "Cancel" };
    std::unique_ptr<juce::FileChooser> chooser;
    std::function<juce::String(const juce::var&)> apply;
};
