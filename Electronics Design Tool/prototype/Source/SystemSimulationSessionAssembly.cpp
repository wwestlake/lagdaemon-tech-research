#include "SystemSimulationSession.h"
#include "SystemAssembly.h"

namespace sim_panel
{

bool SystemSimulationSession::setupAssembly(const SystemAssembly& assembly, const juce::File& projectFolder)
{
    stop();
    auto compiled = AssemblyCompiler::compile(assembly, projectFolder);
    if (!compiled.success)
    {
        addEventLog("Assembly compilation failed: " + compiled.error, "FAULT");
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    baseCircuit_ = std::move(compiled.circuit);
    inputElementBindings_ = std::move(compiled.inputElements);
    outputNodeBindings_ = std::move(compiled.signalNodes);
    outputBranchBindings_ = std::move(compiled.signalBranches);

    signalDefs_.clear();
    for (const auto& meta : compiled.metadata)
    {
        SignalDef def;
        def.name = meta.path;
        def.type = DataType::Numeric;
        def.isOutput = !meta.isInput;
        def.unit = meta.unit;
        def.minValue = meta.minVal;
        def.maxValue = meta.maxVal;
        def.description = meta.description.empty() ? meta.path : meta.description;
        def.quantity = meta.quantity;
        def.kind = meta.kind;
        def.companionSignal = meta.companionSignal;
        signalDefs_.push_back(def);
    }

    currentSimTime_ = 0.0;
    hasStarted_ = false;
    isFaulted_ = false;
    currentNumericInputs_.clear();
    currentBooleanInputs_.clear();
    pendingInputs_.clear();

    {
        std::lock_guard<std::mutex> snapshotLock(snapshotMutex_);
        ++latestSnapshot_.timelineRevision;
        latestSnapshot_.simTime = 0.0;
        latestSnapshot_.running = false;
        latestSnapshot_.paused = false;
        latestSnapshot_.faulted = false;
        latestSnapshot_.numericSignals.clear();
        latestSnapshot_.booleanSignals.clear();
        latestSnapshot_.textSignals.clear();
        for (const auto& [name, node] : outputNodeBindings_)
            latestSnapshot_.numericSignals[name] = 0.0;
        for (const auto& [name, elem] : outputBranchBindings_)
            latestSnapshot_.numericSignals[name] = 0.0;
        for (const auto& [name, val] : currentNumericInputs_)
            latestSnapshot_.numericSignals[name] = val;
        for (const auto& [name, val] : currentBooleanInputs_)
            latestSnapshot_.booleanSignals[name] = val;
    }

    addEventLog("System Assembly Loaded: " + assembly.name, "INFO");
    return true;
}

} // namespace sim_panel
