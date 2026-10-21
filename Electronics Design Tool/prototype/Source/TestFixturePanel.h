#pragma once

#include <JuceHeader.h>
#include "TestMode.h"
#include "CircuitHierarchy.h"
#include <functional>
#include <vector>

inline juce::String sanitizeId(const juce::String& text)
{
    juce::String out;
    for (int i = 0; i < text.length(); ++i)
    {
        auto c = text[i];
        if (juce::CharacterFunctions::isLetterOrDigit(c))
            out += c;
        else
            out += '_';
    }
    return out;
}

class TestFixturePanel final : public juce::Component
{
public:
    struct Callbacks
    {
        std::function<std::vector<circuit_hierarchy::Sheet>()> getHierarchySheets;
        std::function<juce::String(test_mode::TestSetup* setup, bool startAudition, juce::String* outError)> runSimulation;
        std::function<void()> stopSimulation;
        std::function<void(const std::vector<float>& buffer, double sampleRate)> auditionBuffer;
        std::function<juce::String(test_mode::TestSetup* setup, const juce::String& path, juce::String* outError)> exportAudio;
        std::function<void(const juce::String& msg)> onStatusMessage;
        std::function<void(TestFixturePanel*)> onClose;
        std::function<bool(const juce::String& setupName, const juce::File& file)> onSaveFixture;
        std::function<juce::File()> getAuthoritativeDiagramFile;
        std::function<test_mode::TestSetup*(const juce::String& setupName)> getSetupByName;
    };

    TestFixturePanel(const juce::String& fixtureName, test_mode::TestSetup* setup, Callbacks callbacks);
    ~TestFixturePanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    juce::String getFixtureName() const { return fixtureName; }
    test_mode::TestSetup* getSetup();
    const test_mode::TestSetup* getSetup() const;
    void updateFromSetup();

    // Direct actions for GUI and Agent API
    bool placeEquipment(const juce::String& type, const juce::String& name, float x, float y, const juce::String& instancePath = {});
    bool moveEquipment(const juce::String& eqId, float x, float y);
    bool removeEquipment(const juce::String& eqId);
    bool connectEquipment(const juce::String& eqId, const juce::String& terminalId, const juce::String& targetPinLabel, const juce::String& targetRefdes = {}, int wireIndex = -1, const juce::String& instancePath = {});
    bool disconnectEquipment(const juce::String& eqIdOrConnId);
    bool configureEquipment(const juce::String& eqId, const juce::var& config);
    bool breakConnection(const juce::String& nodeA, const juce::String& nodeB, const juce::String& pin = {}, int wireIndex = -1, const juce::String& instancePath = {});
    bool restoreConnection(bool restoreAll, const juce::String& nodeA = {}, const juce::String& nodeB = {}, const juce::String& pin = {}, const juce::String& instancePath = {});
    void navigateToSheet(const juce::String& sheetName, const juce::String& instanceRefdes = {});
    bool saveFixture();

    // Simulation, Audition, and Break tool actions (invoked from Test Fixture menu and API)
    void runSimulation(bool startAudition);
    void stopSimulation();
    void exportWav();
    void toggleBreakTool();
    bool isBreakToolActive() const;
    void setDiagnostic(const juce::String& message);
    juce::String diagnosticAction(const juce::String& action);
    juce::String instrumentAction(const juce::String& eqId, const juce::String& action, const juce::var& settings);

    juce::Component* getCanvas() { return schematicCanvas.get(); }

private:
    juce::String fixtureName;
    test_mode::TestSetup* setup = nullptr;
    Callbacks callbacks;

    // Status display
    juce::Label titleLabel;
    juce::Label resultsLabel;
    juce::TextButton diagnosticsButton { "Diagnostics" }, clearDiagnosticButton { "Clear" };
    juce::TextEditor diagnostics;
    bool diagnosticsExpanded = false;

    std::unique_ptr<juce::Component> schematicCanvas;
    std::unique_ptr<juce::FileChooser> fileChooser;

    void updateResultsDisplay();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TestFixturePanel)
};
