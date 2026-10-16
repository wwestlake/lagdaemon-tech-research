#include "CircuitVstPlugin.h"
#include "GeneratedPluginIdentity.h"

#include <algorithm>
#include <cmath>

namespace djehuti_circuit_vst
{

namespace
{

dsp_artifact::LoadArtifactResult resolveDefaultArtifact()
{
    const auto bundleFile = findBundleResourceArtifactFile();
    if (bundleFile.existsAsFile())
        return dsp_artifact::loadAndValidateArtifactFromFile(bundleFile);

    const char* embedded = getEmbeddedArtifactJson();
    if (embedded != nullptr && embedded[0] != '\0')
        return dsp_artifact::loadAndValidateArtifactFromJson(juce::String::fromUTF8(embedded));

    dsp_artifact::LoadArtifactResult r;
    r.error = "No bundle model.frust-il or embedded executable artifact found.";
    return r;
}

class CircuitPluginEditor final : public juce::AudioProcessorEditor,
                                  private juce::Timer
{
public:
    explicit CircuitPluginEditor(Processor& p)
        : juce::AudioProcessorEditor(p),
          processor(p)
    {
        const auto& art = processor.artifact();
        const juce::String prodName = art.productName.empty() ? "Djehuti Circuit VST3" : juce::String(art.productName);
        const juce::String mfrName = art.manufacturerName.empty() ? "Djehuti Audio" : juce::String(art.manufacturerName);
        const juce::String verStr = art.productVersion.empty() ? "1.0.0" : juce::String(art.productVersion);

        titleLabel.setText(prodName, juce::dontSendNotification);
        titleLabel.setFont(juce::Font(20.0f, juce::Font::bold));
        titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xffe6f2f5));
        addAndMakeVisible(titleLabel);

        subtitleLabel.setText(mfrName + "  |  v" + verStr + "  |  Pkg: " + juce::String(art.packageId),
                              juce::dontSendNotification);
        subtitleLabel.setFont(juce::Font(12.5f));
        subtitleLabel.setColour(juce::Label::textColourId, juce::Colour(0xff8fa6b2));
        addAndMakeVisible(subtitleLabel);

        statusBadge.setText(processor.isValid() ? "EXECUTABLE COFF-x64 READY" : ("ERROR: " + processor.runtimeError()),
                            juce::dontSendNotification);
        statusBadge.setJustificationType(juce::Justification::centredRight);
        statusBadge.setFont(juce::Font(12.0f, juce::Font::bold));
        statusBadge.setColour(juce::Label::textColourId,
                              processor.isValid() ? juce::Colour(0xff67e8b5) : juce::Colour(0xffff6b6b));
        addAndMakeVisible(statusBadge);

        bypassButton.setButtonText("Bypass");
        bypassButton.setColour(juce::ToggleButton::textColourId, juce::Colour(0xffdce9ee));
        addAndMakeVisible(bypassButton);
        if (processor.state().getParameter("bypass") != nullptr)
            bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
                processor.state(), "bypass", bypassButton);

        if (art.effects.size() > 1 && processor.state().getParameter("effect_select") != nullptr)
        {
            effectSelectorLabel.setText("Circuit Effect Mode:", juce::dontSendNotification);
            effectSelectorLabel.setFont(juce::Font(13.0f, juce::Font::bold));
            effectSelectorLabel.setColour(juce::Label::textColourId, juce::Colour(0xffdce9ee));
            addAndMakeVisible(effectSelectorLabel);

            for (std::size_t i = 0; i < art.effects.size(); ++i)
                effectSelector.addItem(juce::String(art.effects[i].name), static_cast<int>(i) + 1);

            addAndMakeVisible(effectSelector);
            effectAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
                processor.state(), "effect_select", effectSelector);
        }

        // Studio framing rotary controls
        addRotaryControl("input_gain_db", "Input Gain", "dB", juce::Colour(0xff5ec2e6));
        for (const auto& param : art.parameters)
        {
            if (param.hiddenInModeSelector)
                continue;
            if (param.kind == "switch")
                addToggleControl(param.id, param.name);
            else
                addRotaryControl(param.id, param.name, param.unit, juce::Colour(0xff78dcca));
        }
        addRotaryControl("mix", "Dry/Wet Mix", "", juce::Colour(0xffe6b85e));
        addRotaryControl("output_level_db", "Output Level", "dB", juce::Colour(0xff5ec2e6));

        footerLabel.setText(processor.runtimeStatus(), juce::dontSendNotification);
        footerLabel.setFont(juce::Font(11.5f));
        footerLabel.setColour(juce::Label::textColourId, juce::Colour(0xff788c99));
        addAndMakeVisible(footerLabel);

        const int numKnobs = std::max(4, static_cast<int>(knobs.size()));
        const int cols = std::min(5, numKnobs);
        const int rows = (numKnobs + cols - 1) / cols;
        const int width = std::max(540, cols * 130 + 48);
        const int height = 170 + rows * 145 + (switches.empty() ? 0 : 48);
        setSize(width, height);
        startTimerHz(10);
    }

    ~CircuitPluginEditor() override
    {
        stopTimer();
    }

    void paint(juce::Graphics& g) override
    {
        juce::ColourGradient bg(juce::Colour(0xff10171f), 0.0f, 0.0f,
                                juce::Colour(0xff17222e), 0.0f, static_cast<float>(getHeight()), false);
        g.setGradientFill(bg);
        g.fillAll();

        auto headerRect = getLocalBounds().removeFromTop(66).reduced(12, 8);
        g.setColour(juce::Colour(0xff1c2a38));
        g.fillRoundedRectangle(headerRect.toFloat(), 6.0f);
        g.setColour(juce::Colour(0xff31475c));
        g.drawRoundedRectangle(headerRect.toFloat(), 6.0f, 1.2f);

        g.setColour(juce::Colour(0xff2d4052));
        g.drawRect(getLocalBounds(), 1);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(18, 12);
        auto header = area.removeFromTop(54);
        auto rightHeader = header.removeFromRight(220);
        statusBadge.setBounds(rightHeader.removeFromTop(24));
        bypassButton.setBounds(rightHeader.removeFromTop(26).removeFromRight(100));

        titleLabel.setBounds(header.removeFromTop(28));
        subtitleLabel.setBounds(header.removeFromTop(22));

        area.removeFromTop(8);
        if (effectSelector.isVisible())
        {
            auto modeRow = area.removeFromTop(32);
            effectSelectorLabel.setBounds(modeRow.removeFromLeft(145));
            effectSelector.setBounds(modeRow.removeFromLeft(280).reduced(0, 2));
            area.removeFromTop(8);
        }

        if (!switches.empty())
        {
            auto swRow = area.removeFromTop(36);
            const int swWidth = std::max(110, swRow.getWidth() / std::max(1, static_cast<int>(switches.size())));
            for (auto& sw : switches)
                sw->button.setBounds(swRow.removeFromLeft(swWidth).reduced(6, 2));
            area.removeFromTop(6);
        }

        footerLabel.setBounds(area.removeFromBottom(22));

        if (!knobs.empty())
        {
            const int count = static_cast<int>(knobs.size());
            const int cols = std::min(5, std::max(1, count));
            const int rows = (count + cols - 1) / cols;
            const int cellW = area.getWidth() / cols;
            const int cellH = std::max(120, area.getHeight() / std::max(1, rows));

            for (int i = 0; i < count; ++i)
            {
                const int r = i / cols;
                const int c = i % cols;
                juce::Rectangle<int> cell(area.getX() + c * cellW, area.getY() + r * cellH, cellW, cellH);
                auto cellInner = cell.reduced(8, 4);
                knobs[(std::size_t)i]->label.setBounds(cellInner.removeFromTop(20));
                knobs[(std::size_t)i]->slider.setBounds(cellInner.withSizeKeepingCentre(100, 100));
            }
        }
    }

private:
    struct KnobControl
    {
        juce::Label label;
        juce::Slider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    struct SwitchControl
    {
        juce::ToggleButton button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
    };

    void addRotaryControl(const std::string& paramId,
                          const std::string& titleText,
                          const std::string& unitText,
                          juce::Colour fillCol)
    {
        if (processor.state().getParameter(paramId) == nullptr)
            return;

        auto kc = std::make_unique<KnobControl>();
        kc->label.setText(juce::String(titleText), juce::dontSendNotification);
        kc->label.setJustificationType(juce::Justification::centred);
        kc->label.setFont(juce::Font(12.5f, juce::Font::bold));
        kc->label.setColour(juce::Label::textColourId, juce::Colour(0xffdce9ee));
        addAndMakeVisible(kc->label);

        kc->slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        kc->slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 84, 20);
        kc->slider.setColour(juce::Slider::rotarySliderFillColourId, fillCol);
        kc->slider.setColour(juce::Slider::thumbColourId, juce::Colour(0xffffd24a));
        if (!unitText.empty())
            kc->slider.setTextValueSuffix(" " + juce::String(unitText));
        addAndMakeVisible(kc->slider);

        kc->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.state(), paramId, kc->slider);
        knobs.push_back(std::move(kc));
    }

    void addToggleControl(const std::string& paramId, const std::string& titleText)
    {
        if (processor.state().getParameter(paramId) == nullptr)
            return;

        auto sc = std::make_unique<SwitchControl>();
        sc->button.setButtonText(juce::String(titleText));
        sc->button.setColour(juce::ToggleButton::textColourId, juce::Colour(0xffdce9ee));
        addAndMakeVisible(sc->button);
        sc->attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            processor.state(), paramId, sc->button);
        switches.push_back(std::move(sc));
    }

    void timerCallback() override
    {
        footerLabel.setText(processor.runtimeStatus() +
                            "  |  SR: " + juce::String(processor.activeSampleRate(), 0) + " Hz" +
                            (processor.divergenceResets() > 0
                                 ? ("  |  Safety Resets: " + juce::String(processor.divergenceResets()))
                                 : ""),
                            juce::dontSendNotification);
    }

    Processor& processor;
    juce::Label titleLabel, subtitleLabel, statusBadge, effectSelectorLabel, footerLabel;
    juce::ToggleButton bypassButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    juce::ComboBox effectSelector;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> effectAttachment;
    std::vector<std::unique_ptr<KnobControl>> knobs;
    std::vector<std::unique_ptr<SwitchControl>> switches;
};

} // namespace

juce::File findBundleResourceArtifactFile()
{
    const auto moduleFile = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    // VST3 bundle structure: <Name>.vst3/Contents/x86_64-win/<Name>.vst3
    // Resources live in:     <Name>.vst3/Contents/Resources/model.frust-il
    const auto contentsDir = moduleFile.getParentDirectory().getParentDirectory();
    const auto resourceFile = contentsDir.getChildFile("Resources").getChildFile("model.frust-il");
    if (resourceFile.existsAsFile())
        return resourceFile;

    const auto siblingFile = moduleFile.getSiblingFile("model.frust-il");
    if (siblingFile.existsAsFile())
        return siblingFile;

    return {};
}

Processor::Processor()
    : Processor(resolveDefaultArtifact().artifact)
{
    const auto loadRes = resolveDefaultArtifact();
    if (!loadRes.ok)
    {
        validArtifactAndObject = false;
        errorMessage = loadRes.error;
        statusMessage = "Artifact validation failed: " + juce::String(loadRes.error);
    }
}

Processor::Processor(const juce::String& artifactJsonText)
    : Processor(dsp_artifact::loadAndValidateArtifactFromJson(artifactJsonText).artifact)
{
    const auto loadRes = dsp_artifact::loadAndValidateArtifactFromJson(artifactJsonText);
    if (!loadRes.ok)
    {
        validArtifactAndObject = false;
        errorMessage = loadRes.error;
        statusMessage = "Artifact validation failed: " + juce::String(loadRes.error);
        runtimeSlots.clear();
    }
}

Processor::Processor(const dsp_artifact::ExecutableArtifact& inArtifact)
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      loadedArtifact(inArtifact),
      parameters(*this,
                 nullptr,
                 juce::Identifier(inArtifact.packageId.empty() ? "DJEHUTI_CIRCUIT_VST" : inArtifact.packageId),
                 createParameterLayout(inArtifact, !inArtifact.slots.empty()))
{
    if (!inArtifact.slots.empty())
    {
        const auto roundTrip = dsp_artifact::loadAndValidateArtifactFromJson(
            dsp_artifact::serializeArtifactToJson(inArtifact));
        initializeFromLoadResult(roundTrip);
    }
    else
    {
        validArtifactAndObject = false;
        errorMessage = "Empty or invalid executable artifact.";
        statusMessage = "Artifact load failed: empty slot list.";
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout Processor::createParameterLayout(
    const dsp_artifact::ExecutableArtifact& artifact,
    bool /*isValidArtifact*/)
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("bypass", 1),
        "Bypass",
        false));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("input_gain_db", 1),
        "Input Gain",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.01f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("output_level_db", 1),
        "Output Level",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.01f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("mix", 1),
        "Dry/Wet Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
        1.0f));

    juce::StringArray effectChoices;
    if (artifact.effects.size() >= 2)
    {
        for (const auto& eff : artifact.effects)
            effectChoices.add(eff.name.empty() ? "Effect" : juce::String(eff.name));
    }
    else
    {
        effectChoices.add(artifact.productName.empty() ? "Primary Effect" : juce::String(artifact.productName));
    }

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("effect_select", 1),
        "Effect Selector",
        effectChoices,
        0));

    for (const auto& p : artifact.parameters)
    {
        if (p.id.empty()) continue;
        if (p.kind == "switch")
        {
            params.push_back(std::make_unique<juce::AudioParameterBool>(
                juce::ParameterID(p.id, 1),
                p.name.empty() ? p.id : p.name,
                p.defaultValue > 0.5));
        }
        else
        {
            const float minV = static_cast<float>(p.minValue);
            const float maxV = static_cast<float>(p.maxValue > p.minValue ? p.maxValue : p.minValue + 1.0);
            const float defV = juce::jlimit(minV, maxV, static_cast<float>(p.defaultValue));
            const float stepV = p.step > 0.0 ? static_cast<float>(p.step) : 0.001f;
            params.push_back(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID(p.id, 1),
                p.name.empty() ? p.id : p.name,
                juce::NormalisableRange<float>(minV, maxV, stepV),
                defV,
                juce::AudioParameterFloatAttributes().withLabel(p.unit)));
        }
    }

    return { params.begin(), params.end() };
}

bool Processor::initializeFromLoadResult(const dsp_artifact::LoadArtifactResult& loadRes)
{
    runtimeSlots.clear();
    validArtifactAndObject = false;

    if (!loadRes.ok)
    {
        errorMessage = loadRes.error;
        statusMessage = "Artifact validation failed: " + juce::String(loadRes.error);
        return false;
    }

    loadedArtifact = loadRes.artifact;

    // Cache atomic pointers to APVTS raw parameters
    bypassParam = parameters.getRawParameterValue("bypass");
    inputGainDbParam = parameters.getRawParameterValue("input_gain_db");
    outputLevelDbParam = parameters.getRawParameterValue("output_level_db");
    mixParam = parameters.getRawParameterValue("mix");
    effectSelectParam = parameters.getRawParameterValue("effect_select");

    boundCircuitParams.clear();
    for (const auto& p : loadedArtifact.parameters)
    {
        BoundCircuitParam bp;
        bp.id = p.id;
        bp.livePortId = p.livePortId.empty() ? p.id : p.livePortId;
        bp.rawValue = parameters.getRawParameterValue(p.id);
        boundCircuitParams.push_back(std::move(bp));
    }

    // Link precompiled object code for each slot via frust_plugin_load_object (NO source compilation!)
    for (std::size_t idx = 0; idx < loadedArtifact.slots.size(); ++idx)
    {
        const auto& artSlot = loadedArtifact.slots[idx];
        const std::string unitKey = "slot_" + std::to_string(idx) + "_" + artSlot.slotId;

        const auto objLoad = engine.loadObject(unitKey, artSlot.runtimeManifestJson, artSlot.objectBytes);
        if (!objLoad.ok)
        {
            errorMessage = "Object link failed for slot '" + artSlot.slotName + "': " + objLoad.report();
            statusMessage = juce::String(errorMessage);
            runtimeSlots.clear();
            return false;
        }

        auto fn = reinterpret_cast<ProcessFn>(engine.function(unitKey, "process_sample"));
        if (fn == nullptr)
        {
            errorMessage = "Entry point 'process_sample' missing in linked object for slot '" + artSlot.slotName + "'.";
            statusMessage = juce::String(errorMessage);
            runtimeSlots.clear();
            return false;
        }

        RuntimeSlot rtSlot;
        rtSlot.slotId = artSlot.slotId;
        rtSlot.circuit = artSlot.circuit;
        rtSlot.baseConfig = artSlot.dspConfig;
        rtSlot.runtimeModel = artSlot.model;
        rtSlot.coeffScratch = rtSlot.runtimeModel.makeLiveCoeffScratch();
        rtSlot.activeCoeffs.assign(std::max(1, rtSlot.runtimeModel.coeffSize()), 0.0);
        rtSlot.orderedLiveValues.assign(std::max(1, rtSlot.runtimeModel.livePortCount), 0.5);
        for (const auto& lp : rtSlot.runtimeModel.livePorts)
            rtSlot.livePortParamIds.push_back(lp.paramId);

        const int wsSize = std::max(1, rtSlot.runtimeModel.workspaceSize);
        rtSlot.channelWorkspaces.assign(kMaxChannels, std::vector<double>(wsSize, 0.0));
        for (int ch = 0; ch < kMaxChannels; ++ch)
        {
            if (rtSlot.runtimeModel.initialWorkspace.size() == static_cast<std::size_t>(wsSize))
                rtSlot.channelWorkspaces[(std::size_t)ch] = rtSlot.runtimeModel.initialWorkspace;
        }
        rtSlot.processSample = fn;
        runtimeSlots.push_back(std::move(rtSlot));
    }

    for (std::size_t i = 0; i < runtimeSlots.size(); ++i)
        updateSlotCoefficientsNoAlloc(i, 0);

    validArtifactAndObject = true;
    errorMessage.clear();
    statusMessage = "Loaded precompiled COFF-x64 artifact '" + juce::String(loadedArtifact.packageId) +
                    "' (" + juce::String(static_cast<int>(runtimeSlots.size())) + " DSP slot(s), " +
                    juce::String(static_cast<int>(loadedArtifact.effects.size())) + " effect mode(s)).";
    return true;
}

const juce::String Processor::getName() const
{
    if (!loadedArtifact.productName.empty())
        return juce::String(loadedArtifact.productName);
    return JucePlugin_Name;
}

bool Processor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    return !in.isDisabled() && in == out &&
           (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

void Processor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    if (sampleRate <= 0.0 || !std::isfinite(sampleRate))
        sampleRate = 48000.0;

    currentSampleRate = sampleRate;
    effectCrossfadeStep = 1.0 / std::max(16.0, 0.005 * sampleRate); // 5 ms smooth mode transition
    bypassSmoothStep = 1.0 / std::max(16.0, 0.003 * sampleRate);    // 3 ms smooth bypass transition
    effectCrossfadeAlpha = 1.0;

    if (!validArtifactAndObject)
        return;

    // Re-discretize every slot's circuit model at the host's exact sample rate (zero source compilation!)
    for (std::size_t i = 0; i < runtimeSlots.size(); ++i)
    {
        auto& slot = runtimeSlots[i];
        audio_dsp::Config cfg = slot.baseConfig;
        cfg.sampleRate = sampleRate;
        audio_dsp::Model rebuilt = audio_dsp::build(slot.circuit, cfg);
        if (rebuilt.ok &&
            rebuilt.workspaceSize == slot.runtimeModel.workspaceSize &&
            rebuilt.coeffSize() == slot.runtimeModel.coeffSize())
        {
            slot.runtimeModel = std::move(rebuilt);
        }
        slot.coeffScratch = slot.runtimeModel.makeLiveCoeffScratch();
        slot.activeCoeffs.assign(std::max(1, slot.runtimeModel.coeffSize()), 0.0);
        slot.orderedLiveValues.assign(std::max(1, slot.runtimeModel.livePortCount), 0.5);

        const int wsSize = std::max(1, slot.runtimeModel.workspaceSize);
        slot.channelWorkspaces.assign(kMaxChannels, std::vector<double>(wsSize, 0.0));
        for (int ch = 0; ch < kMaxChannels; ++ch)
        {
            if (slot.runtimeModel.initialWorkspace.size() == static_cast<std::size_t>(wsSize))
                slot.channelWorkspaces[(std::size_t)ch] = slot.runtimeModel.initialWorkspace;
        }
    }

    const int effIdx = getSelectedEffectIndex();
    lastEffectIndex = effIdx;
    previousEffectIndex = effIdx;
    for (std::size_t i = 0; i < runtimeSlots.size(); ++i)
        updateSlotCoefficientsNoAlloc(i, effIdx);
}

void Processor::updateSlotCoefficientsNoAlloc(std::size_t slotIdx, int activeEffectIdx) noexcept
{
    if (slotIdx >= runtimeSlots.size())
        return;

    auto& slot = runtimeSlots[slotIdx];
    const int L = slot.runtimeModel.livePortCount;

    for (int j = 0; j < L; ++j)
    {
        const std::string& portParamId = slot.livePortParamIds[(std::size_t)j];
        double val = 0.5;

        // 1. Check user/host parameter value
        for (const auto& bp : boundCircuitParams)
        {
            if (bp.livePortId == portParamId && bp.rawValue != nullptr)
            {
                val = static_cast<double>(bp.rawValue->load(std::memory_order_relaxed));
                break;
            }
        }

        // 2. Apply active effect preset livePortOverrides (for coupled 3-effect mode switching)
        if (activeEffectIdx >= 0 && activeEffectIdx < static_cast<int>(loadedArtifact.effects.size()))
        {
            const auto& eff = loadedArtifact.effects[(std::size_t)activeEffectIdx];
            auto it = eff.livePortOverrides.find(portParamId);
            if (it != eff.livePortOverrides.end())
                val = it->second;
        }

        slot.orderedLiveValues[(std::size_t)j] = val;
    }

    slot.runtimeModel.computeLiveCoefficientsNoAlloc(
        slot.orderedLiveValues.data(),
        L,
        slot.coeffScratch,
        slot.activeCoeffs.data());
}

double Processor::stepSlotChannelNoAlloc(RuntimeSlot& slot, int channel, double inputVolts) noexcept
{
    if (slot.processSample == nullptr)
        return 0.0;

    const int chIdx = std::max(0, std::min(kMaxChannels - 1, channel));
    auto& ws = slot.channelWorkspaces[(std::size_t)chIdx];
    const double rawOutVolts = slot.processSample(inputVolts, ws.data(), slot.activeCoeffs.data());

    // Non-finite / runaway Newton divergence guard
    if (!std::isfinite(rawOutVolts) || std::abs(rawOutVolts) > 1.0e6)
    {
        if (slot.runtimeModel.initialWorkspace.size() == ws.size())
            std::copy(slot.runtimeModel.initialWorkspace.begin(), slot.runtimeModel.initialWorkspace.end(), ws.begin());
        else
            std::fill(ws.begin(), ws.end(), 0.0);
        divergenceResetCount.fetch_add(1, std::memory_order_relaxed);
        return 0.0;
    }

    for (double v : ws)
    {
        if (!std::isfinite(v) || std::abs(v) > 1.0e6)
        {
            if (slot.runtimeModel.initialWorkspace.size() == ws.size())
                std::copy(slot.runtimeModel.initialWorkspace.begin(), slot.runtimeModel.initialWorkspace.end(), ws.begin());
            else
                std::fill(ws.begin(), ws.end(), 0.0);
            divergenceResetCount.fetch_add(1, std::memory_order_relaxed);
            return 0.0;
        }
    }

    const double outScale = slot.runtimeModel.outputFullScaleVolts > 1e-9
                                ? slot.runtimeModel.outputFullScaleVolts
                                : 1.0;
    return rawOutVolts / outScale;
}

void Processor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Never substitute pass-through or a demo gain processor when the artifact is invalid!
    if (!validArtifactAndObject || runtimeSlots.empty())
    {
        buffer.clear();
        return;
    }

    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();
    if (numChannels <= 0 || numSamples <= 0)
        return;

    const bool bypassed = (bypassParam != nullptr) && (bypassParam->load(std::memory_order_relaxed) > 0.5f);
    const double targetBypassWet = bypassed ? 0.0 : 1.0;

    const float inDb = (inputGainDbParam != nullptr) ? inputGainDbParam->load(std::memory_order_relaxed) : 0.0f;
    const float outDb = (outputLevelDbParam != nullptr) ? outputLevelDbParam->load(std::memory_order_relaxed) : 0.0f;
    const double inGain = std::pow(10.0, static_cast<double>(inDb) / 20.0);
    const double outGain = std::pow(10.0, static_cast<double>(outDb) / 20.0);
    const double wetMix = (mixParam != nullptr)
                              ? juce::jlimit(0.0, 1.0, static_cast<double>(mixParam->load(std::memory_order_relaxed)))
                              : 1.0;

    const int effectCount = std::max(1, static_cast<int>(loadedArtifact.effects.size()));
    const int requestedEffect = (effectSelectParam != nullptr)
                                    ? juce::jlimit(0, effectCount - 1, static_cast<int>( std::lround(effectSelectParam->load(std::memory_order_relaxed))))
                                    : 0;

    if (requestedEffect != lastEffectIndex)
    {
        previousEffectIndex = lastEffectIndex;
        lastEffectIndex = requestedEffect;
        effectCrossfadeAlpha = 0.0;
    }

    const int currSlotIdx = (lastEffectIndex < static_cast<int>(loadedArtifact.effects.size()))
                                ? juce::jlimit(0, static_cast<int>(runtimeSlots.size()) - 1,
                                               loadedArtifact.effects[(std::size_t)lastEffectIndex].modelSlotIndex)
                                : 0;
    const int prevSlotIdx = (previousEffectIndex < static_cast<int>(loadedArtifact.effects.size()))
                                ? juce::jlimit(0, static_cast<int>(runtimeSlots.size()) - 1,
                                               loadedArtifact.effects[(std::size_t)previousEffectIndex].modelSlotIndex)
                                : currSlotIdx;

    // Update live reduced coefficients on active slot(s) with zero heap allocation
    updateSlotCoefficientsNoAlloc(static_cast<std::size_t>(currSlotIdx), lastEffectIndex);
    if (prevSlotIdx != currSlotIdx && effectCrossfadeAlpha < 1.0)
        updateSlotCoefficientsNoAlloc(static_cast<std::size_t>(prevSlotIdx), previousEffectIndex);

    for (int i = 0; i < numSamples; ++i)
    {
        if (smoothedBypassWet < targetBypassWet)
            smoothedBypassWet = std::min(targetBypassWet, smoothedBypassWet + bypassSmoothStep);
        else if (smoothedBypassWet > targetBypassWet)
            smoothedBypassWet = std::max(targetBypassWet, smoothedBypassWet - bypassSmoothStep);

        if (effectCrossfadeAlpha < 1.0)
            effectCrossfadeAlpha = std::min(1.0, effectCrossfadeAlpha + effectCrossfadeStep);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* channelData = buffer.getWritePointer(ch);
            const double drySample = static_cast<double>(channelData[i]);
            if (!std::isfinite(drySample))
            {
                channelData[i] = 0.0f;
                continue;
            }

            auto& currSlot = runtimeSlots[(std::size_t)currSlotIdx];
            const double inVoltsCurr = drySample * inGain * currSlot.runtimeModel.inputVolts;
            double wetSample = stepSlotChannelNoAlloc(currSlot, ch, inVoltsCurr);

            if (prevSlotIdx != currSlotIdx && effectCrossfadeAlpha < 1.0)
            {
                auto& prevSlot = runtimeSlots[(std::size_t)prevSlotIdx];
                const double inVoltsPrev = drySample * inGain * prevSlot.runtimeModel.inputVolts;
                const double wetPrev = stepSlotChannelNoAlloc(prevSlot, ch, inVoltsPrev);
                wetSample = (1.0 - effectCrossfadeAlpha) * wetPrev + effectCrossfadeAlpha * wetSample;
            }

            const double mixedEffect = (1.0 - wetMix) * drySample + wetMix * (wetSample * outGain);
            double finalOut = (1.0 - smoothedBypassWet) * drySample + smoothedBypassWet * mixedEffect;
            if (!std::isfinite(finalOut))
                finalOut = 0.0;
            finalOut = juce::jlimit(-8.0, 8.0, finalOut);
            channelData[i] = static_cast<float>(finalOut);
        }
    }
}

juce::AudioProcessorEditor* Processor::createEditor()
{
    return new CircuitPluginEditor(*this);
}

int Processor::getNumPrograms()
{
    return std::max(1, static_cast<int>(loadedArtifact.effects.size()));
}

int Processor::getCurrentProgram()
{
    return getSelectedEffectIndex();
}

void Processor::setCurrentProgram(int index)
{
    setSelectedEffectIndex(index);
}

const juce::String Processor::getProgramName(int index)
{
    if (index >= 0 && index < static_cast<int>(loadedArtifact.effects.size()))
        return juce::String(loadedArtifact.effects[(std::size_t)index].name);
    return getName();
}

void Processor::getStateInformation(juce::MemoryBlock& destData)
{
    auto* root = new juce::DynamicObject();
    root->setProperty("packageId", juce::String(loadedArtifact.packageId));
    root->setProperty("productName", juce::String(loadedArtifact.productName));
    root->setProperty("productVersion", juce::String(loadedArtifact.productVersion));
    root->setProperty("schemaVersion", loadedArtifact.schemaVersion);
    root->setProperty("selectedEffect", getSelectedEffectIndex());
    if (auto xml = parameters.copyState().createXml())
        root->setProperty("parametersXml", xml->toString());

    juce::MemoryOutputStream stream(destData, false);
    stream.writeString(juce::JSON::toString(juce::var(root), false));
}

void Processor::setStateInformation(const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;

    const auto text = juce::String::fromUTF8(static_cast<const char*>(data), sizeInBytes);
    const auto parsed = juce::JSON::parse(text);
    if (const auto* root = parsed.getDynamicObject())
    {
        if (const auto xmlText = root->getProperty("parametersXml").toString(); xmlText.isNotEmpty())
        {
            if (auto xml = juce::parseXML(xmlText))
                parameters.replaceState(juce::ValueTree::fromXml(*xml));
        }
        if (root->hasProperty("selectedEffect"))
            setSelectedEffectIndex(static_cast<int>(root->getProperty("selectedEffect")));
    }
}

bool Processor::setParameterValueNormalized(const std::string& paramId, float normalizedValue)
{
    if (auto* p = parameters.getParameter(paramId))
    {
        p->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, normalizedValue));
        return true;
    }
    return false;
}

bool Processor::setParameterValuePlain(const std::string& paramId, float plainValue)
{
    if (auto* p = parameters.getParameter(paramId))
    {
        p->setValueNotifyingHost(p->convertTo0to1(plainValue));
        return true;
    }
    return false;
}

float Processor::getParameterValuePlain(const std::string& paramId) const
{
    if (const auto* raw = parameters.getRawParameterValue(paramId))
        return raw->load(std::memory_order_relaxed);
    return 0.0f;
}

void Processor::setSelectedEffectIndex(int effectIndex)
{
    const int count = std::max(1, static_cast<int>(loadedArtifact.effects.size()));
    const int clamped = juce::jlimit(0, count - 1, effectIndex);
    setParameterValuePlain("effect_select", static_cast<float>(clamped));
}

int Processor::getSelectedEffectIndex() const
{
    if (effectSelectParam == nullptr)
        return 0;
    const int count = std::max(1, static_cast<int>(loadedArtifact.effects.size()));
    return juce::jlimit(0, count - 1, static_cast<int>(std::lround(effectSelectParam->load(std::memory_order_relaxed))));
}

void Processor::resetAllWorkspaces()
{
    for (auto& slot : runtimeSlots)
    {
        const int wsSize = std::max(1, slot.runtimeModel.workspaceSize);
        for (int ch = 0; ch < kMaxChannels; ++ch)
        {
            if (slot.runtimeModel.initialWorkspace.size() == static_cast<std::size_t>(wsSize))
                slot.channelWorkspaces[(std::size_t)ch] = slot.runtimeModel.initialWorkspace;
            else
                slot.channelWorkspaces[(std::size_t)ch].assign(wsSize, 0.0);
        }
    }
}

} // namespace djehuti_circuit_vst

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new djehuti_circuit_vst::Processor();
}
