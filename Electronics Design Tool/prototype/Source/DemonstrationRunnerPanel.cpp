#include "DemonstrationRunnerPanel.h"

namespace demo_system
{

DemonstrationRunnerPanel::DemonstrationRunnerPanel()
    : DemonstrationRunnerPanel(std::make_shared<sim_panel::SystemSimulationSession>(), nullptr, nullptr)
{
}

DemonstrationRunnerPanel::DemonstrationRunnerPanel(std::shared_ptr<sim_panel::SystemSimulationSession> session,
                                                   mechanical_scene::MechanicalViewComponent* mechView,
                                                   sim_panel::ControlPanelComponent* ctrlPanel)
    : session_(session), mechanicalView_(mechView), controlPanel_(ctrlPanel)
{
    runner_.attachSession(session_);

    titleLabel.setText("Agent Demonstration Runner & Automated Laboratory", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(16.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xff5bc0be));
    addAndMakeVisible(titleLabel);

    machineSelector.addItem("No machine loaded", 1);
    machineSelector.setSelectedId(1, juce::dontSendNotification);
    addAndMakeVisible(machineSelector);

    demoSelector.addItem("No demonstration loaded", 1);
    demoSelector.setSelectedId(1, juce::dontSendNotification);
    addAndMakeVisible(demoSelector);

    startBtn.onClick = [this]() { startDemo(); };
    pauseBtn.onClick = [this]() { pauseDemo(); };
    resumeBtn.onClick = [this]() { resumeDemo(); };
    stopBtn.onClick = [this]() { stopDemo(); };
    resetBtn.onClick = [this]() { resetDemo(); };
    stepBtn.onClick = [this]() { stepDemo(); };
    runExperimentBtn.onClick = [this]() { runFullExperiment(); };

    addAndMakeVisible(startBtn);
    addAndMakeVisible(pauseBtn);
    addAndMakeVisible(resumeBtn);
    addAndMakeVisible(stopBtn);
    addAndMakeVisible(resetBtn);
    addAndMakeVisible(stepBtn);
    addAndMakeVisible(runExperimentBtn);

    actionListBox.setModel(this);
    actionListBox.setRowHeight(24);
    addAndMakeVisible(actionListBox);

    reportViewer.setMultiLine(true);
    reportViewer.setReadOnly(true);
    reportViewer.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));
    reportViewer.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff161b22));
    reportViewer.setColour(juce::TextEditor::textColourId, juce::Colour(0xffe6edf3));
    addAndMakeVisible(reportViewer);

    statusLabel.setText("Status: Ready", juce::dontSendNotification);
    statusLabel.setFont(juce::Font(13.0f, juce::Font::bold));
    statusLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(statusLabel);

    startTimerHz(15);
}

DemonstrationRunnerPanel::~DemonstrationRunnerPanel()
{
    stopTimer();
}

void DemonstrationRunnerPanel::setMachine(const machine_builder::MachineDesign& machine)
{
    runner_.setMachine(machine);
    if (mechanicalView_)
        mechanicalView_->setScene(machine.scene);
    if (controlPanel_)
        controlPanel_->setLayout(machine.controlPanel);
    refreshUi();
}

void DemonstrationRunnerPanel::setDemonstration(const DemonstrationDefinition& def)
{
    runner_.setDefinition(def);
    actionListBox.updateContent();
    refreshUi();
}

void DemonstrationRunnerPanel::startDemo()
{
    runner_.start();
    refreshUi();
}

void DemonstrationRunnerPanel::pauseDemo()
{
    runner_.pause();
    refreshUi();
}

void DemonstrationRunnerPanel::resumeDemo()
{
    runner_.resume();
    refreshUi();
}

void DemonstrationRunnerPanel::stopDemo()
{
    runner_.stop();
    refreshUi();
}

void DemonstrationRunnerPanel::resetDemo()
{
    runner_.reset();
    refreshUi();
}

void DemonstrationRunnerPanel::stepDemo()
{
    runner_.executeNextAction();
    refreshUi();
}

void DemonstrationRunnerPanel::runFullExperiment()
{
    const auto run = runner_.runSynchronous();
    updateReportView();
    refreshUi();
}

int DemonstrationRunnerPanel::getNumRows()
{
    return (int)runner_.getDefinition().actions.size();
}

void DemonstrationRunnerPanel::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    if (rowNumber < 0 || rowNumber >= (int)runner_.getDefinition().actions.size())
        return;

    const auto& act = runner_.getDefinition().actions[rowNumber];
    const int curIdx = runner_.getCurrentActionIndex();
    const auto status = runner_.getStatus();

    if (rowIsSelected)
        g.fillAll(juce::Colour(0xff2d3748));
    else if (rowNumber % 2 == 1)
        g.fillAll(juce::Colour(0xff1a202c));
    else
        g.fillAll(juce::Colour(0xff141923));

    // Status Indicator
    juce::Colour indicatorCol = juce::Colour(0xff718096);
    juce::String stateTag = "[WAIT]";

    if (rowNumber < curIdx)
    {
        indicatorCol = juce::Colour(0xff48bb78); // Green (Passed)
        stateTag = "[DONE]";
    }
    else if (rowNumber == curIdx && status == RunStatus::Running)
    {
        indicatorCol = juce::Colour(0xffecc94b); // Yellow (Executing)
        stateTag = "[RUN]";
    }
    else if (rowNumber == curIdx && status == RunStatus::Failed)
    {
        indicatorCol = juce::Colour(0xfff56565); // Red (Failed)
        stateTag = "[FAIL]";
    }

    g.setColour(indicatorCol);
    g.setFont(juce::Font(12.0f, juce::Font::bold));
    g.drawText(stateTag, 8, 0, 50, height, juce::Justification::centredLeft);

    g.setColour(juce::Colour(0xffe2e8f0));
    g.setFont(juce::Font(12.0f, juce::Font::plain));
    juce::String desc = juce::String(rowNumber + 1) + ". " + juce::String(actionTypeToString(act.type)) + " - " + juce::String(act.description);
    g.drawText(desc, 65, 0, width - 75, height, juce::Justification::centredLeft, true);
}

void DemonstrationRunnerPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff0d1117));
}

void DemonstrationRunnerPanel::resized()
{
    auto area = getLocalBounds().reduced(8);

    titleLabel.setBounds(area.removeFromTop(24));
    area.removeFromTop(6);

    auto selArea = area.removeFromTop(28);
    machineSelector.setBounds(selArea.removeFromLeft(selArea.getWidth() / 2 - 4));
    selArea.removeFromLeft(8);
    demoSelector.setBounds(selArea);
    area.removeFromTop(8);

    auto btnArea = area.removeFromTop(28);
    const int btnW = (btnArea.getWidth() - 36) / 7;
    startBtn.setBounds(btnArea.removeFromLeft(btnW));
    btnArea.removeFromLeft(6);
    pauseBtn.setBounds(btnArea.removeFromLeft(btnW));
    btnArea.removeFromLeft(6);
    resumeBtn.setBounds(btnArea.removeFromLeft(btnW));
    btnArea.removeFromLeft(6);
    stopBtn.setBounds(btnArea.removeFromLeft(btnW));
    btnArea.removeFromLeft(6);
    resetBtn.setBounds(btnArea.removeFromLeft(btnW));
    btnArea.removeFromLeft(6);
    stepBtn.setBounds(btnArea.removeFromLeft(btnW));
    btnArea.removeFromLeft(6);
    runExperimentBtn.setBounds(btnArea);
    area.removeFromTop(8);

    statusLabel.setBounds(area.removeFromTop(22));
    area.removeFromTop(6);

    auto split = area.withHeight(area.getHeight());
    actionListBox.setBounds(split.removeFromTop(split.getHeight() / 2));
    split.removeFromTop(6);
    reportViewer.setBounds(split);
}

void DemonstrationRunnerPanel::timerCallback()
{
    runner_.tick(0.066);

    const auto status = runner_.getStatus();
    juce::String statText = "Status: " + juce::String(runStatusToString(status))
                          + " | Sim Time: " + juce::String(runner_.getCurrentSimTime(), 3) + " s"
                          + " | Step: " + juce::String(runner_.getCurrentActionIndex()) + "/" + juce::String(runner_.getDefinition().actions.size());
    statusLabel.setText(statText, juce::dontSendNotification);

    actionListBox.repaint();

    if (status == RunStatus::Completed || status == RunStatus::Failed || status == RunStatus::TimedOut)
    {
        updateReportView();
    }
}

void DemonstrationRunnerPanel::refreshUi()
{
    actionListBox.updateContent();
    actionListBox.repaint();
    updateReportView();
}

void DemonstrationRunnerPanel::updateReportView()
{
    const auto& run = runner_.getCurrentRun();
    if (!run.runId.empty())
    {
        reportViewer.setText(juce::String(run.generateMarkdownReport()));
    }
}

} // namespace demo_system
