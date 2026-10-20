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

namespace test_fixture {

struct CircuitPin
{
    juce::String label; // e.g. "V1.G", "OUT", "C_IN.1"
    juce::String name;  // e.g. "G", "1", "2"
    juce::Point<float> pos;
};

struct CircuitComponent
{
    juce::String refdes;
    juce::String symbolId;
    juce::String value;
    juce::Point<float> pos;
    float rotation = 0.0f;
    juce::Rectangle<float> bounds;
    std::vector<CircuitPin> pins;
    bool isSubBlock = false;
    juce::String childSheet;
};

struct CircuitWire
{
    int wireIndex = -1;
    juce::Point<float> a;
    juce::Point<float> b;
    juce::String nodeA;
    juce::String nodeB;
};

struct FixtureCircuitData
{
    juce::String sheet;
    std::vector<CircuitComponent> components;
    std::vector<CircuitWire> wires;
};

} // namespace test_fixture

class TestFixturePanel final : public juce::Component
{
public:
    struct Callbacks
    {
        std::function<test_fixture::FixtureCircuitData(const juce::String& sheet)> getCircuitData;
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
    test_mode::TestSetup* getSetup()
    {
        if (callbacks.getSetupByName != nullptr)
        {
            if (auto* s = callbacks.getSetupByName(fixtureName))
                return (setup = s);
        }
        return setup;
    }
    const test_mode::TestSetup* getSetup() const
    {
        if (callbacks.getSetupByName != nullptr)
        {
            if (auto* s = callbacks.getSetupByName(fixtureName))
                return s;
        }
        return setup;
    }
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

private:
    class CanvasView;
    friend class CanvasView;

    juce::String fixtureName;
    test_mode::TestSetup* setup = nullptr;
    Callbacks callbacks;

    // Controls
    juce::Label titleLabel;
    juce::TextButton runButton { "▶ Run" };
    juce::TextButton auditionButton { "🔊 Audition" };
    juce::TextButton stopButton { "⏹ Stop" };
    juce::TextButton exportButton { "💾 Export WAV" };
    juce::TextButton saveButton { "💾 Save Fixture" };

    juce::TextButton addAudioInButton { "+ Audio In" };
    juce::TextButton addAudioSinkButton { "+ Audio Sink" };
    juce::TextButton addSigGenButton { "+ Sig Gen" };
    juce::TextButton addProbeButton { "+ Probe" };
    juce::ToggleButton breakToolToggle { "✂ Cut Wire" };

    juce::Label resultsLabel;

    std::unique_ptr<CanvasView> canvas;
    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::String currentSheet;
    juce::String currentInstancePath;

    std::vector<std::pair<juce::Rectangle<int>, juce::String>> breadcrumbAreas;

    void updateResultsDisplay();
    void showEquipmentDialog(const juce::String& eqId);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TestFixturePanel)
};
