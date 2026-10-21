#pragma once

#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include <map>
#include <string>
#include "AudioTestRender.h"

namespace test_mode
{

struct HierarchicalBreak
{
    juce::String instancePath; // e.g. "" for top-level, "A1", "A2", "A1/SUB"
    juce::String nodeA;        // Node label A, e.g. "A1.OUT", or "IN"
    juce::String nodeB;        // Node label B, e.g. "A2.IN", or "R1.1"
    juce::String pin;          // Optional isolated pin label, e.g. "A2.IN"
    int wireIndex = -1;        // Optional wire index if explicitly known

    juce::var toVar() const
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty("instancePath", instancePath);
        if (nodeA.isNotEmpty()) obj->setProperty("nodeA", nodeA);
        if (nodeB.isNotEmpty()) obj->setProperty("nodeB", nodeB);
        if (pin.isNotEmpty())   obj->setProperty("pin", pin);
        if (wireIndex >= 0)     obj->setProperty("wireIndex", wireIndex);
        return juce::var(obj);
    }

    static HierarchicalBreak fromVar(const juce::var& v)
    {
        HierarchicalBreak b;
        if (v.isObject())
        {
            b.instancePath = v.getProperty("instancePath", {}).toString();
            b.nodeA = v.getProperty("nodeA", {}).toString();
            b.nodeB = v.getProperty("nodeB", {}).toString();
            b.pin = v.getProperty("pin", {}).toString();
            b.wireIndex = (int)v.getProperty("wireIndex", -1);
        }
        return b;
    }
};

struct SubFixture
{
    juce::String instancePath;          // e.g. "A1", "A2"
    bool stimulusEnabled = false;       // If true, this sub-fixture injects its own stimulus
    juce::String inputNode;             // Node inside this sub-fixture, e.g. "IN" or "V1.G"
    double inputPeakVolts = 0.1;        // Peak volts for stimulus
    juce::String stimulusType = "sine"; // "sine" or "file"
    juce::String audioFilePath;
    double sineFrequencyHz = 1000.0;
    double sineDurationSec = 0.05;

    juce::String outputNode;            // Local observation node, e.g. "OUT"
    juce::StringArray probeNodes;       // Probes inside this sub-fixture (e.g. ["V1.P", "V1.K"])

    juce::var toVar() const
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty("instancePath", instancePath);
        obj->setProperty("stimulusEnabled", stimulusEnabled);
        obj->setProperty("inputNode", inputNode);
        obj->setProperty("inputPeakVolts", inputPeakVolts);
        obj->setProperty("stimulusType", stimulusType);
        obj->setProperty("audioFilePath", audioFilePath);
        obj->setProperty("sineFrequencyHz", sineFrequencyHz);
        obj->setProperty("sineDurationSec", sineDurationSec);
        obj->setProperty("outputNode", outputNode);

        juce::Array<juce::var> pArr;
        for (const auto& p : probeNodes)
            pArr.add(p);
        obj->setProperty("probeNodes", pArr);
        return juce::var(obj);
    }

    static SubFixture fromVar(const juce::var& v)
    {
        SubFixture sf;
        if (!v.isObject()) return sf;
        sf.instancePath = v.getProperty("instancePath", {}).toString();
        sf.stimulusEnabled = (bool)v.getProperty("stimulusEnabled", false);
        sf.inputNode = v.getProperty("inputNode", {}).toString();
        sf.inputPeakVolts = (double)v.getProperty("inputPeakVolts", 0.1);
        sf.stimulusType = v.getProperty("stimulusType", "sine").toString();
        sf.audioFilePath = v.getProperty("audioFilePath", {}).toString();
        sf.sineFrequencyHz = (double)v.getProperty("sineFrequencyHz", 1000.0);
        sf.sineDurationSec = (double)v.getProperty("sineDurationSec", 0.05);
        sf.outputNode = v.getProperty("outputNode", {}).toString();

        if (const auto* arr = v.getProperty("probeNodes", {}).getArray())
        {
            for (const auto& item : *arr)
                sf.probeNodes.add(item.toString());
        }
        return sf;
    }
};

struct FixtureTerminal
{
    juce::String id;
    juce::String name;
    juce::Point<float> relativePos;
    bool isOutput = false;
};

struct FixtureEquipment
{
    juce::String id;
    juce::String type = "audio_input"; // "audio_input", "audio_output", "signal_gen", "probe"
    juce::String name;
    juce::Point<float> pos { 80.0f, 180.0f };
    juce::String instancePath;

    juce::String stimulusType = "file";
    juce::String audioFilePath;
    double sineFrequencyHz = 1000.0;
    double sineDurationSec = 0.05;
    double peakVolts = 0.1;
    bool isOverride = false;
    juce::String overrideRefdes;
    std::map<juce::String, juce::String> instrumentSettings;

    double measuredPeak = 0.0;
    double measuredRms = 0.0;
    double measuredDc = 0.0;
    double gainDb = 0.0;

    std::vector<FixtureTerminal> getTerminals() const
    {
        std::vector<FixtureTerminal> terms;
        if (type == "audio_input" || type == "signal_gen" || type == "signal_source")
        {
            terms.push_back({ "out", "OUT", { 128.0f, 48.0f }, true });
            terms.push_back({ "gnd", "GND", { 18.0f, 48.0f }, true });
        }
        else if (type == "audio_output" || type == "audio_out")
        {
            terms.push_back({ "in", "IN", { 18.0f, 48.0f }, false });
            terms.push_back({ "ref", "REF", { 128.0f, 48.0f }, false });
        }
        else if (type == "probe" || type == "voltmeter" || type == "digital_multimeter")
        {
            terms.push_back({ "in", "+", { 18.0f, 48.0f }, false });
            terms.push_back({ "ref", "-", { 128.0f, 48.0f }, false });
        }
        else if (type == "scope" || type == "oscilloscope_2ch")
        {
            terms.push_back({ "in", "CH1", { 18.0f, 36.0f }, false });
            terms.push_back({ "ch2", "CH2", { 18.0f, 52.0f }, false });
            terms.push_back({ "ref", "GND", { 128.0f, 48.0f }, false });
        }
        else if (type == "bode_analyzer")
        {
            terms.push_back({ "in", "IN", { 18.0f, 36.0f }, false });
            terms.push_back({ "out", "OUT", { 18.0f, 52.0f }, true });
            terms.push_back({ "ref", "GND", { 128.0f, 48.0f }, false });
        }
        else if (type == "xyz_plotter")
        {
            terms.push_back({ "x", "X", { 18.0f, 32.0f }, false });
            terms.push_back({ "y", "Y", { 18.0f, 44.0f }, false });
            terms.push_back({ "z", "Z", { 18.0f, 56.0f }, false });
            terms.push_back({ "ref", "GND", { 128.0f, 48.0f }, false });
        }
        else if (type == "ammeter")
        {
            terms.push_back({ "in", "+", { 18.0f, 48.0f }, false });
            terms.push_back({ "out", "-", { 128.0f, 48.0f }, true });
        }
        return terms;
    }

    juce::Rectangle<float> getBounds() const
    {
        return juce::Rectangle<float>(pos.x, pos.y, 140.0f, 64.0f);
    }

    juce::var toVar() const
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty("id", id);
        obj->setProperty("type", type);
        obj->setProperty("name", name);
        obj->setProperty("x", pos.x);
        obj->setProperty("y", pos.y);
        obj->setProperty("instancePath", instancePath);
        obj->setProperty("stimulusType", stimulusType);
        obj->setProperty("audioFilePath", audioFilePath);
        obj->setProperty("sineFrequencyHz", sineFrequencyHz);
        obj->setProperty("sineDurationSec", sineDurationSec);
        obj->setProperty("peakVolts", peakVolts);
        obj->setProperty("isOverride", isOverride);
        obj->setProperty("overrideRefdes", overrideRefdes);
        auto* settings = new juce::DynamicObject();
        for (const auto& [key, value] : instrumentSettings) settings->setProperty(key, value);
        obj->setProperty("instrumentSettings", juce::var(settings));
        obj->setProperty("measuredPeak", measuredPeak);
        obj->setProperty("measuredRms", measuredRms);
        obj->setProperty("measuredDc", measuredDc);
        obj->setProperty("gainDb", gainDb);
        return juce::var(obj);
    }

    static FixtureEquipment fromVar(const juce::var& v)
    {
        FixtureEquipment eq;
        if (!v.isObject()) return eq;
        eq.id = v.getProperty("id", {}).toString();
        eq.type = v.getProperty("type", "audio_input").toString();
        eq.name = v.getProperty("name", eq.type).toString();
        eq.pos.x = (float)(double)v.getProperty("x", 80.0);
        eq.pos.y = (float)(double)v.getProperty("y", 180.0);
        eq.instancePath = v.getProperty("instancePath", {}).toString();
        eq.stimulusType = v.getProperty("stimulusType", "file").toString();
        eq.audioFilePath = v.getProperty("audioFilePath", {}).toString();
        eq.sineFrequencyHz = (double)v.getProperty("sineFrequencyHz", 1000.0);
        eq.sineDurationSec = (double)v.getProperty("sineDurationSec", 0.05);
        eq.peakVolts = (double)v.getProperty("peakVolts", 0.1);
        eq.isOverride = (bool)v.getProperty("isOverride", false);
        eq.overrideRefdes = v.getProperty("overrideRefdes", {}).toString();
        if (const auto* settings = v.getProperty("instrumentSettings", {}).getDynamicObject())
            for (const auto& p : settings->getProperties()) eq.instrumentSettings[p.name.toString()] = p.value.toString();
        eq.measuredPeak = (double)v.getProperty("measuredPeak", 0.0);
        eq.measuredRms = (double)v.getProperty("measuredRms", 0.0);
        eq.measuredDc = (double)v.getProperty("measuredDc", 0.0);
        eq.gainDb = (double)v.getProperty("gainDb", 0.0);
        return eq;
    }
};

struct FixtureConnection
{
    juce::String id;
    juce::String equipmentId;
    juce::String terminalId = "out";
    juce::String targetRefdes;
    int targetPinIndex = -1;
    juce::String targetPinLabel;
    int targetWireIndex = -1;
    juce::Point<float> targetPoint;
    juce::String instancePath;

    juce::var toVar() const
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty("id", id);
        obj->setProperty("equipmentId", equipmentId);
        obj->setProperty("terminalId", terminalId);
        obj->setProperty("targetRefdes", targetRefdes);
        obj->setProperty("targetPinIndex", targetPinIndex);
        obj->setProperty("targetPinLabel", targetPinLabel);
        obj->setProperty("targetWireIndex", targetWireIndex);
        obj->setProperty("targetX", targetPoint.x);
        obj->setProperty("targetY", targetPoint.y);
        obj->setProperty("instancePath", instancePath);
        return juce::var(obj);
    }

    static FixtureConnection fromVar(const juce::var& v)
    {
        FixtureConnection c;
        if (!v.isObject()) return c;
        c.id = v.getProperty("id", {}).toString();
        c.equipmentId = v.getProperty("equipmentId", {}).toString();
        c.terminalId = v.getProperty("terminalId", "out").toString();
        c.targetRefdes = v.getProperty("targetRefdes", {}).toString();
        c.targetPinIndex = (int)v.getProperty("targetPinIndex", -1);
        c.targetPinLabel = v.getProperty("targetPinLabel", {}).toString();
        c.targetWireIndex = (int)v.getProperty("targetWireIndex", -1);
        c.targetPoint.x = (float)(double)v.getProperty("targetX", 0.0);
        c.targetPoint.y = (float)(double)v.getProperty("targetY", 0.0);
        c.instancePath = v.getProperty("instancePath", {}).toString();
        return c;
    }
};

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

    // Hierarchical test extensions
    std::vector<HierarchicalBreak> connectionBreaks; // Virtual connection cuts
    std::vector<SubFixture> subFixtures;             // Per-instance sub-fixtures

    // Graphical test fixture equipment & wiring
    std::vector<FixtureEquipment> equipment;
    std::vector<FixtureConnection> fixtureConnections;

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
    std::vector<audio_test::Trace> lastProbeTraces; // Runtime samples, never sidecar/schematic data.
    juce::String lastError;
    juce::int64 captureRevision = 0;
    juce::String acquisitionSignature;

    void invalidateResult()
    {
        hasResult = false;
        lastRenderedOutput.clear();
        lastProbeTraces.clear();
        ++captureRevision;
    }

    const FixtureConnection* terminalConnection(const juce::String& equipmentId, const juce::String& terminal) const
    {
        for (const auto& c : fixtureConnections)
            if (c.equipmentId == equipmentId && c.terminalId.equalsIgnoreCase(terminal)) return &c;
        return nullptr;
    }

    FixtureEquipment* getEquipment(const juce::String& eqId)
    {
        for (auto& eq : equipment)
            if (eq.id.equalsIgnoreCase(eqId)) return &eq;
        return nullptr;
    }

    const FixtureEquipment* getEquipment(const juce::String& eqId) const
    {
        for (const auto& eq : equipment)
            if (eq.id.equalsIgnoreCase(eqId)) return &eq;
        return nullptr;
    }

    FixtureConnection* getConnection(const juce::String& connId)
    {
        for (auto& c : fixtureConnections)
            if (c.id.equalsIgnoreCase(connId)) return &c;
        return nullptr;
    }

    const FixtureConnection* getConnection(const juce::String& connId) const
    {
        for (const auto& c : fixtureConnections)
            if (c.id.equalsIgnoreCase(connId)) return &c;
        return nullptr;
    }

    bool addEquipment(const FixtureEquipment& eq)
    {
        for (auto& existing : equipment)
        {
            if (existing.id.equalsIgnoreCase(eq.id))
            {
                existing = eq;
                syncSetupFromEquipment();
                return true;
            }
        }
        equipment.push_back(eq);
        syncSetupFromEquipment();
        return true;
    }

    bool removeEquipment(const juce::String& eqId)
    {
        bool found = false;
        equipment.erase(std::remove_if(equipment.begin(), equipment.end(),
            [&](const FixtureEquipment& eq) {
                if (eq.id.equalsIgnoreCase(eqId)) { found = true; return true; }
                return false;
            }), equipment.end());

        fixtureConnections.erase(std::remove_if(fixtureConnections.begin(), fixtureConnections.end(),
            [&](const FixtureConnection& c) { return c.equipmentId.equalsIgnoreCase(eqId); }),
            fixtureConnections.end());

        if (found) syncSetupFromEquipment();
        return found;
    }

    bool addConnection(const FixtureConnection& conn)
    {
        for (auto& c : fixtureConnections)
        {
            if (c.id.equalsIgnoreCase(conn.id) ||
                (c.equipmentId.equalsIgnoreCase(conn.equipmentId) && c.terminalId.equalsIgnoreCase(conn.terminalId)))
            {
                c = conn;
                syncSetupFromEquipment();
                return true;
            }
        }
        fixtureConnections.push_back(conn);
        syncSetupFromEquipment();
        return true;
    }

    bool removeConnection(const juce::String& connId)
    {
        bool found = false;
        fixtureConnections.erase(std::remove_if(fixtureConnections.begin(), fixtureConnections.end(),
            [&](const FixtureConnection& c) {
                if (c.id.equalsIgnoreCase(connId) || c.equipmentId.equalsIgnoreCase(connId)) { found = true; return true; }
                return false;
            }), fixtureConnections.end());

        if (found) syncSetupFromEquipment();
        return found;
    }

    void syncSetupFromEquipment()
    {
        // Moving a body or changing display scales must not discard acquired waveforms.
        juce::String signature;
        for (const auto& eq : equipment)
        {
            signature << eq.id << ":" << eq.type << ":" << eq.instancePath << ":" << eq.audioFilePath << ":"
                      << eq.stimulusType << ":" << eq.peakVolts << ":" << eq.sineFrequencyHz << ":"
                      << eq.sineDurationSec << ":" << (int)eq.isOverride << ":" << eq.overrideRefdes << ";";
            if (eq.type == "scope" || eq.type == "oscilloscope_2ch")
                for (const auto& [key, value] : eq.instrumentSettings)
                    if (key == "ch1_impedance" || key == "ch2_impedance" || key == "ch1_impedance_enabled" || key == "ch2_impedance_enabled")
                        signature << key << "=" << value << ";";
        }
        for (const auto& c : fixtureConnections) signature << juce::JSON::toString(c.toVar(), true);
        for (const auto& b : connectionBreaks) signature << juce::JSON::toString(b.toVar(), true);
        if (signature != acquisitionSignature) { invalidateResult(); acquisitionSignature = signature; }
        if (!equipment.empty()) probeNodes.clear();
        for (const auto& eq : equipment)
        {
            if (eq.type == "audio_input" || eq.type == "signal_gen" || eq.type == "signal_source")
            {
                if (eq.instancePath.isEmpty())
                {
                    inputNode.clear();
                    inputRefdes.clear();
                    inputMode = "virtual";
                    stimulusType = eq.stimulusType;
                    audioFilePath = eq.audioFilePath;
                    sineFrequencyHz = eq.sineFrequencyHz;
                    sineDurationSec = eq.sineDurationSec;
                    inputPeakVolts = eq.peakVolts;
                    if (eq.isOverride && eq.overrideRefdes.isNotEmpty())
                    {
                        inputMode = "replace";
                        inputRefdes = eq.overrideRefdes;
                    }
                    else
                    {
                        for (const auto& c : fixtureConnections)
                        {
                            if (c.equipmentId.equalsIgnoreCase(eq.id) && c.terminalId.equalsIgnoreCase("out"))
                            {
                                inputMode = "virtual";
                                inputNode = c.targetPinLabel;
                                break;
                            }
                        }
                    }
                }
                else
                {
                    auto* sf = getSubFixture(eq.instancePath, true);
                    if (sf != nullptr)
                    {
                        sf->stimulusEnabled = true;
                        sf->stimulusType = eq.stimulusType;
                        sf->audioFilePath = eq.audioFilePath;
                        sf->sineFrequencyHz = eq.sineFrequencyHz;
                        sf->sineDurationSec = eq.sineDurationSec;
                        sf->inputPeakVolts = eq.peakVolts;
                        for (const auto& c : fixtureConnections)
                        {
                            if (c.equipmentId.equalsIgnoreCase(eq.id) && c.terminalId.equalsIgnoreCase("out"))
                            {
                                sf->inputNode = c.targetPinLabel;
                                break;
                            }
                        }
                    }
                }
            }
            else if (eq.type == "audio_output" || eq.type == "audio_out")
            {
                if (eq.instancePath.isEmpty()) outputNode.clear();
                for (const auto& c : fixtureConnections)
                {
                    if (c.equipmentId.equalsIgnoreCase(eq.id) && c.terminalId.equalsIgnoreCase("in"))
                    {
                        if (eq.instancePath.isEmpty())
                            outputNode = c.targetPinLabel;
                        else
                        {
                            auto* sf = getSubFixture(eq.instancePath, true);
                            if (sf != nullptr) sf->outputNode = c.targetPinLabel;
                        }
                        break;
                    }
                }
            }
            else if (eq.type == "probe" || eq.type == "scope" || eq.type == "oscilloscope_2ch" || eq.type == "voltmeter" || eq.type == "digital_multimeter" || eq.type == "bode_analyzer" || eq.type == "xyz_plotter")
            {
                for (const auto& c : fixtureConnections)
                {
                    if (c.equipmentId.equalsIgnoreCase(eq.id) && c.targetPinLabel.isNotEmpty())
                    {
                        if (eq.instancePath.isEmpty())
                        {
                            if (!probeNodes.contains(c.targetPinLabel))
                                probeNodes.add(c.targetPinLabel);
                        }
                        else
                        {
                            auto* sf = getSubFixture(eq.instancePath, true);
                            if (sf != nullptr && !sf->probeNodes.contains(c.targetPinLabel))
                                sf->probeNodes.add(c.targetPinLabel);
                        }
                    }
                }
            }
        }
    }

    void syncEquipmentFromSetup()
    {
        if (hasResult)
        {
            for (auto& eq : equipment)
            {
                if (eq.type == "audio_output" || eq.type == "audio_out")
                {
                    eq.measuredPeak = lastOutputPeak;
                    eq.measuredRms = lastOutputRms;
                    eq.measuredDc = lastOutputDc;
                    eq.gainDb = lastGainDb;
                }
                else if (eq.type == "audio_input" || eq.type == "signal_gen" || eq.type == "signal_source")
                {
                    eq.measuredPeak = lastInputPeak;
                }
            }
        }
    }

    SubFixture* getSubFixture(const juce::String& instancePath, bool createIfNotFound = true)
    {
        for (auto& sf : subFixtures)
            if (sf.instancePath.equalsIgnoreCase(instancePath))
                return &sf;

        if (createIfNotFound && instancePath.isNotEmpty())
        {
            SubFixture sf;
            sf.instancePath = instancePath;
            subFixtures.push_back(std::move(sf));
            return &subFixtures.back();
        }
        return nullptr;
    }

    const SubFixture* getSubFixture(const juce::String& instancePath) const
    {
        for (const auto& sf : subFixtures)
            if (sf.instancePath.equalsIgnoreCase(instancePath))
                return &sf;
        return nullptr;
    }

    bool addConnectionBreak(const HierarchicalBreak& b)
    {
        for (const auto& existing : connectionBreaks)
        {
            if (existing.instancePath == b.instancePath)
            {
                if (b.wireIndex >= 0 && existing.wireIndex == b.wireIndex)
                    return false;
                if (b.pin.isNotEmpty() && existing.pin.equalsIgnoreCase(b.pin))
                    return false;
                if (b.nodeA.isNotEmpty() && b.nodeB.isNotEmpty()
                    && ((existing.nodeA.equalsIgnoreCase(b.nodeA) && existing.nodeB.equalsIgnoreCase(b.nodeB))
                        || (existing.nodeA.equalsIgnoreCase(b.nodeB) && existing.nodeB.equalsIgnoreCase(b.nodeA))))
                    return false;
            }
        }
        connectionBreaks.push_back(b);
        return true;
    }

    bool removeConnectionBreak(const juce::String& instancePath, const juce::String& nodeA, const juce::String& nodeB)
    {
        for (auto it = connectionBreaks.begin(); it != connectionBreaks.end(); ++it)
        {
            if (instancePath.isEmpty() || it->instancePath.equalsIgnoreCase(instancePath))
            {
                if ((it->nodeA.equalsIgnoreCase(nodeA) && it->nodeB.equalsIgnoreCase(nodeB))
                    || (it->nodeA.equalsIgnoreCase(nodeB) && it->nodeB.equalsIgnoreCase(nodeA)))
                {
                    connectionBreaks.erase(it);
                    return true;
                }
            }
        }
        return false;
    }

    bool removePinBreak(const juce::String& instancePath, const juce::String& pin)
    {
        for (auto it = connectionBreaks.begin(); it != connectionBreaks.end(); ++it)
        {
            if ((instancePath.isEmpty() || it->instancePath.equalsIgnoreCase(instancePath))
                && it->pin.equalsIgnoreCase(pin))
            {
                connectionBreaks.erase(it);
                return true;
            }
        }
        return false;
    }

    void clearBreaks(const juce::String& instancePath = {})
    {
        if (instancePath.isEmpty())
        {
            connectionBreaks.clear();
        }
        else
        {
            connectionBreaks.erase(
                std::remove_if(connectionBreaks.begin(), connectionBreaks.end(),
                               [&](const HierarchicalBreak& b) { return b.instancePath.equalsIgnoreCase(instancePath); }),
                connectionBreaks.end());
        }
    }

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

        if (!connectionBreaks.empty())
        {
            juce::Array<juce::var> breaksArr;
            for (const auto& b : connectionBreaks)
                breaksArr.add(b.toVar());
            obj->setProperty("connectionBreaks", breaksArr);
        }

        if (!subFixtures.empty())
        {
            juce::Array<juce::var> sfArr;
            for (const auto& sf : subFixtures)
                sfArr.add(sf.toVar());
            obj->setProperty("subFixtures", sfArr);
        }

        if (!equipment.empty())
        {
            juce::Array<juce::var> eqArr;
            for (const auto& eq : equipment)
                eqArr.add(eq.toVar());
            obj->setProperty("equipment", eqArr);
        }

        if (!fixtureConnections.empty())
        {
            juce::Array<juce::var> connArr;
            for (const auto& c : fixtureConnections)
                connArr.add(c.toVar());
            obj->setProperty("fixtureConnections", connArr);
        }

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

        if (const auto* arr = v.getProperty("connectionBreaks", {}).getArray())
        {
            for (const auto& item : *arr)
                s.connectionBreaks.push_back(HierarchicalBreak::fromVar(item));
        }

        if (const auto* arr = v.getProperty("subFixtures", {}).getArray())
        {
            for (const auto& item : *arr)
                s.subFixtures.push_back(SubFixture::fromVar(item));
        }

        if (const auto* arr = v.getProperty("equipment", {}).getArray())
        {
            for (const auto& item : *arr)
                s.equipment.push_back(FixtureEquipment::fromVar(item));
        }

        if (const auto* arr = v.getProperty("fixtureConnections", {}).getArray())
        {
            for (const auto& item : *arr)
                s.fixtureConnections.push_back(FixtureConnection::fromVar(item));
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
    TestModeManager()
    {
        setups.reserve(64);
    }

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

    TestSetup* getSetup(const juce::String& name)
    {
        for (auto& s : setups)
            if (s.name.equalsIgnoreCase(name))
                return &s;
        return nullptr;
    }

    const TestSetup* getSetup(const juce::String& name) const
    {
        for (const auto& s : setups)
            if (s.name.equalsIgnoreCase(name))
                return &s;
        return nullptr;
    }

    bool selectSetup(const juce::String& name)
    {
        for (size_t i = 0; i < setups.size(); ++i)
        {
            if (setups[i].name.equalsIgnoreCase(name))
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
            if (s.name.equalsIgnoreCase(name))
                return false;

        TestSetup s;
        s.name = name;
        s.inputMode = "replace";
        s.inputRefdes = "VIN";
        s.inputNode = "V1.G";
        s.outputNode = "OUT";
        s.probeNodes.add("V1.P");
        s.probeNodes.add("V1.K");
        s.inputPeakVolts = 0.10;
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

    const juce::String& getCurrentInstancePath() const { return currentInstancePath; }
    void setCurrentInstancePath(const juce::String& path) { currentInstancePath = path; }

    bool saveFixtureToFile(const juce::String& setupName, const juce::File& file) const
    {
        for (const auto& s : setups)
        {
            if (s.name.equalsIgnoreCase(setupName))
            {
                auto v = s.toVar();
                auto json = juce::JSON::toString(v, true);
                return file.replaceWithText(json);
            }
        }
        return false;
    }

    bool loadFixtureFromFile(const juce::File& file, juce::String* outName = nullptr)
    {
        if (!file.existsAsFile())
            return false;
        const auto text = file.loadFileAsString();
        auto parsed = juce::JSON::parse(text);
        if (!parsed.isObject())
            return false;
        auto s = TestSetup::fromVar(parsed);
        if (s.name.isEmpty())
            s.name = file.getFileNameWithoutExtension();
        s.syncEquipmentFromSetup();
        for (auto& existing : setups)
        {
            if (existing.name.equalsIgnoreCase(s.name))
            {
                existing = s;
                if (outName != nullptr) *outName = s.name;
                selectSetup(s.name);
                return true;
            }
        }
        setups.push_back(s);
        activeSetupIndex = (int)setups.size() - 1;
        if (outName != nullptr) *outName = s.name;
        return true;
    }

private:
    bool active = false;
    std::vector<TestSetup> setups;
    int activeSetupIndex = -1;
    juce::String currentInstancePath; // "" for top level, "A1", "A2", etc.
};

} // namespace test_mode
