// Proves the Foley graph -> CEL source -> JIT pipeline works end to end, per the plan's own
// verification bar for this phase: "place a handful of Foley graph nodes (trigger -> play sample
// -> branch), generate CEL source, confirm it compiles through the shared CEL JIT." Deliberately
// builds the graph programmatically (via AddRegisteredNode, the same mechanism a real node-editor
// drag-and-drop ends up calling) rather than requiring a running app/UI - this is the automated,
// CI-safe half of verification; a human clicking through the actual Foley panel once it's wired
// into the app is the other half.

#include <lang/compiler.h>
#include <lang/diagnostics.h>
#include <lang/jit/runtime.h>
#include <lang/nodegen/foley_graph_to_source.h>
#include <lang/nodegen/foley_node_catalog.h>
#include <lang/sema.h>
#include <node_system/graph.h>

#include <iostream>
#include <sstream>
#include <stdexcept>

namespace
{
void fail(const std::string& message)
{
    std::cerr << message << std::endl;
    throw std::runtime_error(message);
}

namespace PinName = ce::lang::nodegen::foley::PinName;

ce::node_system::Pin* FindInputPin(ce::node_system::Node& node, const std::string& name)
{
    for (auto& pin : node.Inputs())
        if (pin.name == name)
            return node.FindPin(pin.id);
    return nullptr;
}
}

int main()
{
    try
    {
        auto registry = ce::lang::nodegen::foley::BuildFoleyNodeCatalog();
        ce::node_system::Graph graph("FoleySmokeGraph");
        std::string error;

        auto* trigger = ce::node_system::AddRegisteredNode(graph, registry, "OnTrigger", &error);
        if (trigger == nullptr)
            fail("Failed to add OnTrigger node: " + error);

        auto* branch = ce::node_system::AddRegisteredNode(graph, registry, "Branch", &error);
        if (branch == nullptr)
            fail("Failed to add Branch node: " + error);

        auto* playGravel = ce::node_system::AddRegisteredNode(graph, registry, "PlaySample", &error);
        if (playGravel == nullptr)
            fail("Failed to add PlaySample (gravel) node: " + error);
        if (auto* sampleNamePin = FindInputPin(*playGravel, PinName::SampleName))
            sampleNamePin->defaultValue = std::string("footstep_gravel");

        auto* playGrass = ce::node_system::AddRegisteredNode(graph, registry, "PlaySample", &error);
        if (playGrass == nullptr)
            fail("Failed to add PlaySample (grass) node: " + error);
        if (auto* sampleNamePin = FindInputPin(*playGrass, PinName::SampleName))
            sampleNamePin->defaultValue = std::string("footstep_grass");

        // OnTrigger.execOut -> Branch.execIn
        const auto triggerOut = trigger->Outputs().front().id;
        const auto branchIn = branch->Inputs().front().id;
        ce::node_system::ConnectError connectError{};
        if (! graph.Connect(trigger->Id(), triggerOut, branch->Id(), branchIn, &connectError))
            fail("Failed to connect OnTrigger -> Branch");

        // Branch.execTrue -> PlaySample(gravel).execIn, Branch.execFalse -> PlaySample(grass).execIn
        ce::node_system::PinId execTrueId = 0, execFalseId = 0;
        for (const auto& pin : branch->Outputs())
        {
            if (pin.name == PinName::ExecTrue) execTrueId = pin.id;
            if (pin.name == PinName::ExecFalse) execFalseId = pin.id;
        }
        if (execTrueId == 0 || execFalseId == 0)
            fail("Branch node missing execTrue/execFalse output pins");

        const auto gravelIn = playGravel->Inputs().front().id;
        const auto grassIn = playGrass->Inputs().front().id;
        if (! graph.Connect(branch->Id(), execTrueId, playGravel->Id(), gravelIn, &connectError))
            fail("Failed to connect Branch.execTrue -> PlaySample(gravel)");
        if (! graph.Connect(branch->Id(), execFalseId, playGrass->Id(), grassIn, &connectError))
            fail("Failed to connect Branch.execFalse -> PlaySample(grass)");

        auto genResult = ce::lang::nodegen::foley::GenerateFoleySource(graph, registry);
        if (! genResult.ok)
        {
            std::string combined;
            for (const auto& e : genResult.errors)
                combined += e + "\n";
            fail("GenerateFoleySource failed:\n" + combined);
        }
        if (genResult.source.find("func on_trigger()") == std::string::npos)
            fail("Generated source missing expected on_trigger function");
        if (genResult.source.find("footstep_gravel") == std::string::npos || genResult.source.find("footstep_grass") == std::string::npos)
            fail("Generated source missing expected PlaySample literals");

        std::cout << "--- Generated CEL source ---\n" << genResult.source << "----------------------------\n";

        ce::lang::AstArena arena;
        ce::lang::DiagnosticEngine diagnostics;
        std::istringstream sourceStream(genResult.source);
        auto* program = ce::lang::ParseProgram(sourceStream, arena, diagnostics);
        if (program == nullptr || diagnostics.HasErrors())
            fail("Generated CEL source failed to parse");
        if (! ce::lang::AnalyzeProgram(*program, diagnostics) || diagnostics.HasErrors())
            fail("Generated CEL source failed semantic analysis");

        ce::lang::jit::Runtime runtime;
        const auto execResult = runtime.CompileAndRun(*program, "on_trigger", 0);
        if (execResult.kind == ce::lang::jit::ResultKind::Error)
            fail("JIT compile/run failed: " + execResult.errorMessage);
        if (execResult.faulted)
            fail("JIT run faulted: " + execResult.faultMessage);

        std::cout << "FoleyCelSmoke passed" << std::endl;
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "FoleyCelSmoke failed: " << exception.what() << std::endl;
        return 1;
    }
}
