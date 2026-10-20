#pragma once

#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include <string>
#include "AudioTestRender.h"

namespace test_mode
{

struct TestSetup
{
    juce::String name;
    juce::String inputMode = "replace";      // "replace" (replace existing source) or "virtual" (drive node)
    juce::String inputRefdes = "VIN";        // For replace mode: source refdes
    juce::String inputNode = "V1.G";         // For virtual mode: node/pin label
    double inputPeakVolts = 0.1;             // Stimulus scaled peak voltage (e.g. 0.1 V = 100 mV)
    juce::String stimulusType = "file";      // "file" or "sine"
    juce::String audioFilePath;              // Path to WAV file
    double sineFrequencyHz = 1000.0;
    double sineDurationSec = 0.05;
    juce::String outputNode = "OUT";         // Node or pin to measure output
    juce::StringArray probeNodes;            // Extra voltage probes (e.g. ["V1.P", "V1.K"])

    // Last test result cache
    bool hasResult = false;
    double lastInputPeak = 0.0;
    double lastOutputPeak = 0.0;
    double lastOutputRms = 0.0;
    double lastOutputDc = 0.0;
    double lastGainDb = 0.0;
    double lastGainLinear = 0.0;
    juce::String lastSummary;
    std::vector<float> lastRenderedOutput;
    double lastSampleRate = 48000.0;

    juce::var toVar() const
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty("name", name);
        obj->setProperty("inputMode", inputMode);
        obj->setProperty("inputRefdes", inputRefdes);
        obj->setProperty("inputNode", inputNode);
        obj->setProperty("inputPeakVolts", inputPeakVolts);
        obj->setProperty("stimulusType", stimulusType);
        obj->setProperty("audioFilePath", audioFilePath);
        obj->setProperty("sineFrequencyHz", sineFrequencyHz);
        obj->setProperty("sineDurationSec", sineDurationSec);
        obj->setProperty("outputNode", outputNode);

        juce::Array<juce::var> probesArr;
        for (const auto& p : probeNodes)
            probesArr.add(p);
        obj->setProperty("probeNodes", probesArr);

        if (hasResult)
        {
            auto* resObj = new juce::DynamicObject();
            resObj->setProperty("inputPeak", lastInputPeak);
            resObj->setProperty("outputPeak", lastOutputPeak);
            resObj->setProperty("outputRms", lastOutputRms);
            resObj->setProperty("outputDc", lastOutputDc);
            resObj->setProperty("gainDb", lastGainDb);
            resObj->setProperty("gainLinear", lastGainLinear);
            resObj->setProperty("summary", lastSummary);
            obj->setProperty("lastResult", resObj);
        }

        return juce::var(obj);
    }

    static TestSetup fromVar(const juce::var& v)
    {
        TestSetup s;
        if (!v.isObject())
            return s;

        s.name = v.getProperty("name", "Setup").toString();
        s.inputMode = v.getProperty("inputMode", "replace").toString();
        s.inputRefdes = v.getProperty("inputRefdes", "VIN").toString();
        s.inputNode = v.getProperty("inputNode", "V1.G").toString();
        s.inputPeakVolts = (double)v.getProperty("inputPeakVolts", 0.1);
        s.stimulusType = v.getProperty("stimulusType", "file").toString();
        s.audioFilePath = v.getProperty("audioFilePath", {}).toString();
        s.sineFrequencyHz = (double)v.getProperty("sineFrequencyHz", 1000.0);
        s.sineDurationSec = (double)v.getProperty("sineDurationSec", 0.05);
        s.outputNode = v.getProperty("outputNode", "OUT").toString();

        if (const auto* arr = v.getProperty("probeNodes", {}).getArray())
        {
            for (const auto& item : *arr)
                s.probeNodes.add(item.toString());
        }

        if (const auto* res = v.getProperty("lastResult", {}).getDynamicObject())
        {
            s.hasResult = true;
            s.lastInputPeak = (double)res->getProperty("inputPeak");
            s.lastOutputPeak = (double)res->getProperty("outputPeak");
            s.lastOutputRms = (double)res->getProperty("outputRms");
            s.lastOutputDc = (double)res->getProperty("outputDc");
            s.lastGainDb = (double)res->getProperty("gainDb");
            s.lastGainLinear = (double)res->getProperty("gainLinear");
            s.lastSummary = res->getProperty("summary").toString();
        }

        return s;
    }
};

class TestModeManager
{
public:
    TestModeManager() = default;

    bool isActive() const { return active; }
    void setActive(bool shouldBeActive)
    {
        active = shouldBeActive;
        if (active && setups.empty())
        {
            // Create a default initial setup
            TestSetup defaultSetup;
            defaultSetup.name = "Guitar Preamp Test";
            defaultSetup.inputMode = "replace";
            defaultSetup.inputRefdes = "VIN";
            defaultSetup.inputNode = "V1.G";
            defaultSetup.outputNode = "OUT";
            defaultSetup.probeNodes.add("V1.P");
            defaultSetup.probeNodes.add("V1.K");
            defaultSetup.inputPeakVolts = 0.10;
            defaultSetup.stimulusType = "file";
            setups.push_back(std::move(defaultSetup));
            activeSetupIndex = 0;
        }
    }

    const std::vector<TestSetup>& getSetups() const { return setups; }
    std::vector<TestSetup>& getSetups() { return setups; }

    int getActiveSetupIndex() const { return activeSetupIndex; }
    void setActiveSetupIndex(int index)
    {
        if (index >= 0 && index < (int)setups.size())
            activeSetupIndex = index;
    }

    TestSetup* getActiveSetup()
    {
        if (activeSetupIndex >= 0 && activeSetupIndex < (int)setups.size())
            return &setups[(size_t)activeSetupIndex];
        return nullptr;
    }

    const TestSetup* getActiveSetup() const
    {
        if (activeSetupIndex >= 0 && activeSetupIndex < (int)setups.size())
            return &setups[(size_t)activeSetupIndex];
        return nullptr;
    }

    bool selectSetup(const juce::String& name)
    {
        for (size_t i = 0; i < setups.size(); ++i)
        {
            if (setups[i].name == name)
            {
                activeSetupIndex = (int)i;
                return true;
            }
        }
        return false;
    }

    bool createSetup(const juce::String& name)
    {
        if (name.trim().isEmpty())
            return false;

        for (const auto& s : setups)
            if (s.name == name)
                return false;

        TestSetup s;
        s.name = name;
        if (const auto* current = getActiveSetup())
        {
            // Duplicate current settings as convenient template
            s = *current;
            s.name = name;
            s.hasResult = false;
            s.lastRenderedOutput.clear();
        }
        else
        {
            s.inputMode = "replace";
            s.inputRefdes = "VIN";
            s.inputNode = "V1.G";
            s.outputNode = "OUT";
            s.probeNodes.add("V1.P");
            s.probeNodes.add("V1.K");
            s.inputPeakVolts = 0.10;
        }
        setups.push_back(std::move(s));
        activeSetupIndex = (int)setups.size() - 1;
        return true;
    }

    bool renameSetup(const juce::String& oldName, const juce::String& newName)
    {
        if (newName.trim().isEmpty())
            return false;

        for (const auto& s : setups)
            if (s.name == newName)
                return false;

        for (auto& s : setups)
        {
            if (s.name == oldName)
            {
                s.name = newName;
                return true;
            }
        }
        return false;
    }

    bool deleteSetup(const juce::String& name)
    {
        for (auto it = setups.begin(); it != setups.end(); ++it)
        {
            if (it->name == name)
            {
                setups.erase(it);
                if (activeSetupIndex >= (int)setups.size())
                    activeSetupIndex = (int)setups.size() - 1;
                return true;
            }
        }
        return false;
    }

    juce::var toVar() const
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty("active", active);
        if (const auto* s = getActiveSetup())
            obj->setProperty("activeSetup", s->name);

        juce::Array<juce::var> arr;
        for (const auto& s : setups)
            arr.add(s.toVar());
        obj->setProperty("setups", arr);

        return juce::var(obj);
    }

    void fromVar(const juce::var& v)
    {
        if (!v.isObject())
            return;

        setups.clear();
        if (const auto* arr = v.getProperty("setups", {}).getArray())
        {
            for (const auto& item : *arr)
                setups.push_back(TestSetup::fromVar(item));
        }

        const auto activeName = v.getProperty("activeSetup", {}).toString();
        activeSetupIndex = -1;
        for (size_t i = 0; i < setups.size(); ++i)
        {
            if (setups[i].name == activeName)
            {
                activeSetupIndex = (int)i;
                break;
            }
        }
        if (activeSetupIndex < 0 && !setups.empty())
            activeSetupIndex = 0;
    }

private:
    bool active = false;
    std::vector<TestSetup> setups;
    int activeSetupIndex = -1;
};

} // namespace test_mode
