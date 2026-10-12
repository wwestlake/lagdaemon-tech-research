#include "MachineBuilderPanel.h"

namespace machine_builder
{

MachineBuilderPanel::MachineBuilderPanel()
    : MachineBuilderPanel(nullptr, nullptr, nullptr)
{
}

MachineBuilderPanel::MachineBuilderPanel(std::shared_ptr<sim_panel::SystemSimulationSession> session,
                                           mechanical_scene::MechanicalViewComponent* mechView,
                                           sim_panel::ControlPanelComponent* ctrlPanel)
    : session_(session), mechanicalView_(mechView), controlPanel_(ctrlPanel)
{
    machine_ = MachineDesign::createBlankMachine("Custom Machine");

    titleLabel.setText("Virtual Machine Builder", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(18.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xff78dcca));
    addAndMakeVisible(titleLabel);

    addAndMakeVisible(newBlankBtn);
    newBlankBtn.onClick = [this] {
        setMachine(MachineDesign::createBlankMachine("Custom Machine"));
    };

    addAndMakeVisible(validateBtn);
    validateBtn.onClick = [this] {
        updateDiagnosticsDisplay();
    };

    addAndMakeVisible(compileRunBtn);
    compileRunBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2d728f));
    compileRunBtn.onClick = [this] {
        compileAndRun();
    };

    statusLabel.setText("Ready", juce::dontSendNotification);
    statusLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(statusLabel);

    // Diagnostics Editor
    diagnosticsEditor.setMultiLine(true);
    diagnosticsEditor.setReadOnly(true);
    diagnosticsEditor.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));
    diagnosticsEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff12171e));
    diagnosticsEditor.setColour(juce::TextEditor::textColourId, juce::Colour(0xffdce9ee));
    addAndMakeVisible(diagnosticsEditor);

    updateDiagnosticsDisplay();
    startTimerHz(2); // periodic refresh
}

MachineBuilderPanel::~MachineBuilderPanel()
{
    stopTimer();
}

void MachineBuilderPanel::setMachine(const MachineDesign& machine)
{
    machine_ = machine;
    updateDiagnosticsDisplay();
    if (mechanicalView_ != nullptr)
        mechanicalView_->setScene(machine_.scene);
    if (controlPanel_ != nullptr)
        controlPanel_->setLayout(machine_.controlPanel);
}

void MachineBuilderPanel::loadTemplate(const std::string& templateName)
{
    juce::ignoreUnused(templateName);
    setMachine(MachineDesign::createBlankMachine("Custom Machine"));
}

bool MachineBuilderPanel::compileAndRun()
{
    std::string err;
    if (!machine_.isValid())
    {
        statusLabel.setText("Validation failed. Check diagnostics.", juce::dontSendNotification);
        statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffff6b6b));
        updateDiagnosticsDisplay();
        return false;
    }

    if (session_ != nullptr)
    {
        if (!machine_.compileAndLoadIntoSession(*session_, &err))
        {
            statusLabel.setText("Compilation error: " + juce::String(err), juce::dontSendNotification);
            statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffff6b6b));
            return false;
        }
    }

    if (mechanicalView_ != nullptr)
    {
        mechanicalView_->setScene(machine_.scene);
    }

    statusLabel.setText("Compiled successfully. Simulation active.", juce::dontSendNotification);
    statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xff80ed99));
    return true;
}

void MachineBuilderPanel::updateDiagnosticsDisplay()
{
    const auto diags = machine_.validate();
    juce::String report;
    report << "Machine: " << machine_.name << " (v" << machine_.version << ")\n";
    report << "Subsystem Instances: " << (int)machine_.assembly.instances.size() << "\n";
    report << "Inter-Connections: " << (int)machine_.assembly.connections.size() << "\n";
    report << "Rigid Bodies: " << (int)machine_.scene.bodies.size() << "\n";
    report << "Mechanical Joints: " << (int)machine_.scene.joints.size() << "\n";
    report << "Control Widgets: " << (int)machine_.controlPanel.widgets.size() << "\n";
    report << "--------------------------------------------------------\n";

    if (diags.empty())
    {
        report << "[PASS] All validation checks passed. Zero errors, zero warnings.\n";
        statusLabel.setText("Valid", juce::dontSendNotification);
        statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xff80ed99));
    }
    else
    {
        int errCount = 0, warnCount = 0;
        for (const auto& d : diags)
        {
            if (d.level == Diagnostic::Level::Error) ++errCount; else ++warnCount;
            report << (d.level == Diagnostic::Level::Error ? "[ERROR] " : "[WARN]  ");
            report << "[" << d.component << "] ";
            if (!d.targetId.empty()) report << "(" << d.targetId << ") ";
            report << d.message << "\n";
        }
        report << "--------------------------------------------------------\n";
        report << "Summary: " << errCount << " error(s), " << warnCount << " warning(s).\n";

        if (errCount > 0)
        {
            statusLabel.setText(juce::String(errCount) + " Error(s)", juce::dontSendNotification);
            statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffff6b6b));
        }
        else
        {
            statusLabel.setText("Valid (" + juce::String(warnCount) + " Warning(s))", juce::dontSendNotification);
            statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffffd166));
        }
    }

    diagnosticsEditor.setText(report);
}

void MachineBuilderPanel::timerCallback()
{
    // Sync active mechanical scene from mechanicalView if modified there
    if (mechanicalView_ != nullptr)
    {
        // Keep scene in sync
    }
}

void MachineBuilderPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff18222b));
    g.setColour(juce::Colour(0xff2d3b48));
    g.drawRect(getLocalBounds(), 1);
}

void MachineBuilderPanel::resized()
{
    auto area = getLocalBounds().reduced(8);

    auto topBar = area.removeFromTop(32);
    titleLabel.setBounds(topBar.removeFromLeft(220));
    statusLabel.setBounds(topBar.removeFromLeft(120));
    compileRunBtn.setBounds(topBar.removeFromRight(150));
    validateBtn.setBounds(topBar.removeFromRight(90));

    area.removeFromTop(6);
    auto templateBar = area.removeFromTop(28);
    newBlankBtn.setBounds(templateBar.removeFromLeft(100));
    templateBar.removeFromLeft(6);
    loadDualMotorBtn.setBounds(templateBar.removeFromLeft(160));
    templateBar.removeFromLeft(6);
    loadRobotArmBtn.setBounds(templateBar.removeFromLeft(160));

    area.removeFromTop(8);
    diagnosticsEditor.setBounds(area);
}

} // namespace machine_builder
