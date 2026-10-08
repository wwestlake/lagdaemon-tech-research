#include "GainPluginProcessor.h"

namespace djehuti_generated_gain
{
namespace
{
constexpr const char* modelPackageId = "com.djehuti.generated.gain";
constexpr const char* modelName = "Djehuti Generated Gain";
constexpr const char* parameterId = "gain";

const char* modelFrustSource()
{
    return R"FRUST(
pub fn process_sample(audio_in: f64, ws: Array<f64, 1>, coeffs: Array<f64, 1>) -> f64 = {
    audio_in * coeffs[0]
}
)FRUST";
}

class Editor final : public juce::AudioProcessorEditor
{
public:
    explicit Editor(Processor& p)
        : juce::AudioProcessorEditor(p),
          processor(p),
          gainAttachment(processor.state(), parameterId, gainSlider)
    {
        title.setText(modelName, juce::dontSendNotification);
        title.setFont(juce::Font(18.0f, juce::Font::bold));
        title.setColour(juce::Label::textColourId, juce::Colour(0xffdce9ee));
        addAndMakeVisible(title);

        subtitle.setText("Generated VST3 wrapper hosting exported Djehuti/Frust model code.", juce::dontSendNotification);
        subtitle.setColour(juce::Label::textColourId, juce::Colour(0xff93a7b0));
        addAndMakeVisible(subtitle);

        gainSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        gainSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 88, 22);
        gainSlider.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff78dcca));
        gainSlider.setColour(juce::Slider::thumbColourId, juce::Colour(0xffffd24a));
        addAndMakeVisible(gainSlider);

        gainLabel.setText("Gain", juce::dontSendNotification);
        gainLabel.setJustificationType(juce::Justification::centred);
        gainLabel.setColour(juce::Label::textColourId, juce::Colour(0xffdce9ee));
        addAndMakeVisible(gainLabel);

        status.setText(processor.runtimeStatus(), juce::dontSendNotification);
        status.setColour(juce::Label::textColourId, juce::Colour(0xff71808c));
        addAndMakeVisible(status);

        setSize(360, 260);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff10161d));
        g.setColour(juce::Colour(0xff33424d));
        g.drawRect(getLocalBounds(), 1);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(18);
        title.setBounds(area.removeFromTop(28));
        subtitle.setBounds(area.removeFromTop(34));
        gainSlider.setBounds(area.removeFromTop(140).withSizeKeepingCentre(128, 128));
        gainLabel.setBounds(area.removeFromTop(24));
        status.setBounds(area.removeFromBottom(26));
    }

private:
    Processor& processor;
    juce::Label title, subtitle, gainLabel, status;
    juce::Slider gainSlider;
    juce::AudioProcessorValueTreeState::SliderAttachment gainAttachment;
};
}

Processor::Processor()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "DJEHUTI_GENERATED_GAIN", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout Processor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(parameterId, 1),
        "Gain",
        juce::NormalisableRange<float>(0.0f, 2.0f, 0.001f),
        1.0f,
        juce::AudioParameterFloatAttributes().withLabel("x")));
    return { params.begin(), params.end() };
}

bool Processor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    return !in.isDisabled() && in == out && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

bool Processor::compileModel()
{
    const auto source = frust_engine::manifestLine(modelPackageId, "Generated wrapper model: gain", { "djehuti_dsp_exp", "djehuti_dsp_log" })
                      + modelFrustSource();
    const auto result = engine.load("embedded_model", source);
    if (!result.ok)
    {
        processSample = nullptr;
        status = "Frust model load failed: " + juce::String(result.report());
        return false;
    }
    processSample = reinterpret_cast<ProcessFn>(engine.function("embedded_model", "process_sample"));
    if (processSample == nullptr)
    {
        status = "Frust model loaded, but process_sample was not found.";
        return false;
    }
    status = "Loaded embedded model " + juce::String(modelPackageId) + ".";
    return true;
}

void Processor::prepareToPlay(double, int)
{
    compileModel();
    std::fill(workspace.begin(), workspace.end(), 0.0);
}

void Processor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    coeffs[0] = parameters.getRawParameterValue(parameterId)->load();

    if (processSample == nullptr)
        return;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* data = buffer.getWritePointer(ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            data[i] = static_cast<float>(processSample(static_cast<double>(data[i]), workspace.data(), coeffs.data()));
    }
}

juce::AudioProcessorEditor* Processor::createEditor()
{
    return new Editor(*this);
}

void Processor::getStateInformation(juce::MemoryBlock& destData)
{
    auto* root = new juce::DynamicObject();
    root->setProperty("packageId", modelPackageId);
    root->setProperty("packageName", modelName);
    if (auto xml = parameters.copyState().createXml())
        root->setProperty("parametersXml", xml->toString());
    juce::MemoryOutputStream stream(destData, false);
    stream.writeString(juce::JSON::toString(juce::var(root), true));
}

void Processor::setStateInformation(const void* data, int sizeInBytes)
{
    const auto text = juce::String::fromUTF8(static_cast<const char*>(data), sizeInBytes);
    const auto parsed = juce::JSON::parse(text);
    if (const auto* root = parsed.getDynamicObject())
        if (const auto xmlText = root->getProperty("parametersXml").toString(); xmlText.isNotEmpty())
            if (auto xml = juce::parseXML(xmlText))
                parameters.replaceState(juce::ValueTree::fromXml(*xml));
}
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new djehuti_generated_gain::Processor();
}
