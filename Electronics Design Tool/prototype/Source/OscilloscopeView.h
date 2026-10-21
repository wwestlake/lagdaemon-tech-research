#pragma once
#include <JuceHeader.h>
#include "Oscilloscope.h"

// The scope's single UI, shared by schematic and fixture instrument hosts.
class OscilloscopeView : public juce::Component, private juce::Timer
{
public:
    struct Callbacks
    {
        std::function<oscilloscope::Display()> read;
        std::function<juce::String()> revision;
        std::function<juce::String(const juce::String&)> get;
        std::function<juce::String(const juce::String&, const juce::String&)> set;
        std::function<juce::String(const juce::String&, const juce::String&)> action;
    };
    explicit OscilloscopeView(Callbacks);
    void refresh();
    juce::Rectangle<float> waveformBounds() const { return plotArea(); }
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    struct Row { juce::String key; juce::Label* label; juce::Component* control; int group = -1; };
    Callbacks callbacks;
    oscilloscope::Display display;
    juce::String lastRevision, actionMessage;
    juce::Viewport controls;
    juce::Component controlPanel;
    juce::OwnedArray<juce::Label> labels;
    juce::OwnedArray<juce::Component> fields;
    juce::OwnedArray<juce::TextButton> buttons;
    std::vector<Row> rows;
    juce::TextEditor measurements;
    std::unique_ptr<juce::FileChooser> fileChooser;
    bool synchronising = false;
    int dragCursor = -1;
    juce::Point<float> dragStart;
    oscilloscope::Settings dragSettings;
    void timerCallback() override;
    void group(const juce::String&);
    void field(const juce::String& key, const juce::String& label, const juce::String& help = {});
    void choice(const juce::String& key, const juce::String& label, juce::StringArray options, const juce::String& help = {});
    void action(const juce::String&);
    void commit(const juce::String& key, const juce::String& value);
    juce::Rectangle<float> plotArea() const;
    float timeX(double) const;
    float voltsY(double, int) const;
    double timeAt(float) const;
    double voltsAt(float, int) const;
    static juce::String number(double, const juce::String& unit);
};
