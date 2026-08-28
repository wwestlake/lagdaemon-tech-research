#include "AudioGraphCodegen.h"

#include <variant>

#include "AudioNodeCatalog.h"

namespace cw::audionodes
{

using namespace ce::node_system;

Graph BuildSineToOutputDemoGraph(const NodeTypeRegistry& registry, float levelValue)
{
    Graph graph("SignalLabDemo");

    Node* sine = AddRegisteredNode(graph, registry, NodeType::SineOscillator);
    Node* output = AddRegisteredNode(graph, registry, NodeType::Output);
    if (sine == nullptr || output == nullptr)
        return graph;

    sine->SetEditorPosition(0.0f, 0.0f);
    output->SetEditorPosition(220.0f, 0.0f);

    // Override the level pin's default with the caller-supplied value --
    // the registry only supplies the STARTING default (see
    // NodeTypeDescriptor's own comment); a real instance is free to carry
    // a different one, same as any other node in this system.
    if (Pin* levelPin = sine->FindPin(sine->Inputs()[0].id))
        levelPin->defaultValue = levelValue;

    graph.Connect(sine->Id(), sine->Outputs()[0].id, output->Id(), output->Inputs()[0].id);

    return graph;
}

namespace
{

// v1: the level pin is always a literal (unconnected) -- reading it
// straight off the pin's default value. A level fed by another node's
// output (a Value node, an envelope, ...) is real, specified work for a
// later slice once codegen actually walks connections into other nodes'
// outputs rather than just literals.
float ReadFloatDefault(const Pin& pin, float fallback)
{
    if (const float* value = std::get_if<float>(&pin.defaultValue))
        return *value;
    return fallback;
}

} // namespace

GraphToSourceResult GenerateAudioSource(const Graph& graph, const NodeTypeRegistry& registry)
{
    GraphToSourceResult result;

    std::vector<std::string> validationErrors;
    if (!ValidateAgainstRegistry(graph, registry, &validationErrors))
    {
        result.errors = std::move(validationErrors);
        return result;
    }

    const Node* outputNode = nullptr;
    for (const auto& [id, node] : graph.Nodes())
    {
        if (node->TypeName() == NodeType::Output)
        {
            outputNode = node.get();
            break;
        }
    }
    if (outputNode == nullptr)
    {
        result.errors.push_back("graph has no Output node");
        return result;
    }

    const Pin& signalInPin = outputNode->Inputs()[0];
    const Connection* feedingConnection = nullptr;
    for (const auto& connection : graph.Connections())
    {
        if (connection.toNode == outputNode->Id() && connection.toPin == signalInPin.id)
        {
            feedingConnection = &connection;
            break;
        }
    }
    if (feedingConnection == nullptr)
    {
        result.errors.push_back("node " + std::to_string(outputNode->Id()) + " ('Output'): signalIn is not connected");
        return result;
    }

    const Node* source = graph.FindNode(feedingConnection->fromNode);
    if (source == nullptr || source->TypeName() != NodeType::SineOscillator)
    {
        result.errors.push_back("node " + std::to_string(outputNode->Id())
                                 + " ('Output'): only a SineOscillator source is supported in this v1 slice");
        return result;
    }

    const float level = ReadFloatDefault(source->Inputs()[0], 0.0f);

    // Fixed test phase -- proving graph -> CEL -> JIT -> real numeric
    // result end to end on ONE sample, not a real buffer render yet (see
    // this file's header comment for why).
    constexpr float kTestPhase = 0.5f;

    result.source = "func compute_sample() -> float {\n"
                     "    return sin(" + std::to_string(kTestPhase) + ") * " + std::to_string(level) + ";\n"
                     "}\n";
    result.ok = true;
    return result;
}

} // namespace cw::audionodes
