#pragma once

#include <JuceHeader.h>

#include "Analytics.h"

#include <atomic>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <vector>

// The Analytics window: pick an analysis, fill in its settings (the form is
// built from analytics::fieldsFor), run it on the open diagram, and read the
// result as plots (cursors, zoom, legend), tables and measurements. Every run
// is kept in the history and written to CSV. The agent's analytics_* tools
// drive the same panel through runNow(), so both see the same results.
class AnalyticsPanel final : public juce::Component
{
public:
    AnalyticsPanel();
    ~AnalyticsPanel() override;

    // Supplied by the host.
    std::function<analytics::Netlist()> getNetlist;
    std::function<juce::File()> outputFolder;
    std::function<void()> bringToFront;

    struct Run
    {
        analytics::Result result;
        juce::StringArray files;
        analytics::Table measurements;
    };

    void selectAnalysis(analytics::Analysis analysis);
    void setSettings(analytics::Analysis analysis, const analytics::Settings& settings);
    analytics::Settings settingsFor(analytics::Analysis analysis) const;
    juce::var settingsState() const;
    void restoreSettingsState(const juce::var& state);

    // Runs on the calling thread, shows the result, returns the stored run.
    const Run& runNow(analytics::Analysis analysis, const analytics::Settings& settings);
    // Measures a trace of the latest run and lists it in the window.
    signal_measure::Result measureLatest(const juce::String& trace, const signal_measure::Request& request, juce::String& label);

    const std::deque<Run>& history() const { return runs; }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void visibilityChanged() override;

private:
    class PlotView;
    class ReportView;
    class FormView;
    class AnalysisList;

    void runSelected();
    void deliver(analytics::Result result);
    void storeRun(analytics::Result result);
    void showRun(int index);
    void showPlot(int index);
    void refreshChoices();
    void addMeasurement();
    void updateMeasureControls();
    void saveSettings() const;
    int leftWidth() const { return juce::jlimit(230, 320, getWidth() * 30 / 100); }
    void loadSettings();
    juce::StringArray writeFiles(const analytics::Result& result) const;

    analytics::Analysis current = analytics::Analysis::OperatingPoint;
    std::map<analytics::Analysis, analytics::Settings> settings;
    analytics::Netlist choicesNetlist;

    std::unique_ptr<AnalysisList> list;
    std::unique_ptr<FormView> form;
    juce::Viewport formViewport;
    juce::Label titleLabel, descriptionLabel, statusLabel;
    juce::TextButton runButton { "Run" }, filesButton { "Open CSV folder" }, refreshButton { "Refresh nets" };
    juce::ComboBox historyBox;
    juce::OwnedArray<juce::TextButton> plotButtons;
    std::unique_ptr<PlotView> plot;
    juce::ComboBox measureTrace, measureKind;
    juce::TextEditor measureFrom, measureTo, measureValue;
    juce::Label measureValueLabel;
    juce::TextButton measureButton { "Measure" };
    std::unique_ptr<ReportView> report;
    juce::Viewport reportViewport;

    std::deque<Run> runs; // deque: plots and tables keep their addresses as runs are added
    int shownRun = -1;
    int shownPlot = 0;
    float splitRatio = 0.58f;
    juce::Rectangle<int> dividerArea;
    bool draggingDivider = false;

    std::atomic<bool> running { false };
    juce::ThreadPool pool { 1 };
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
};
