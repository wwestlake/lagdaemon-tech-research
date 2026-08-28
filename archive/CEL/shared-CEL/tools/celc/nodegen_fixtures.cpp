#include "nodegen_fixtures.h"

#include <stdexcept>

#include "lang/nodegen/node_catalog.h"
#include "node_system/type_registry.h"

namespace ce::lang::tools {

namespace {

using ce::node_system::Graph;
using ce::node_system::Node;
using ce::node_system::NodeTypeRegistry;
using ce::node_system::PinId;
using ce::node_system::Vec3Default;
namespace NodeType = ce::lang::nodegen::NodeType;
namespace PinName = ce::lang::nodegen::PinName;

PinId InputId(Node& node, const char* name) {
    for (const auto& pin : node.Inputs()) {
        if (pin.name == name) return pin.id;
    }
    throw std::runtime_error(std::string("node '") + node.TypeName() + "' has no input pin '" + name + "'");
}

PinId OutputId(Node& node, const char* name) {
    for (const auto& pin : node.Outputs()) {
        if (pin.name == name) return pin.id;
    }
    throw std::runtime_error(std::string("node '") + node.TypeName() + "' has no output pin '" + name + "'");
}

Node& AddNode(Graph& graph, const NodeTypeRegistry& registry, const char* typeName) {
    std::string error;
    Node* node = ce::node_system::AddRegisteredNode(graph, registry, typeName, &error);
    if (node == nullptr) {
        throw std::runtime_error("AddRegisteredNode('" + std::string(typeName) + "') failed: " + error);
    }
    return *node;
}

void Wire(Graph& graph, Node& from, const char* fromPin, Node& to, const char* toPin) {
    ce::node_system::ConnectError error{};
    const auto result = graph.Connect(from.Id(), OutputId(from, fromPin), to.Id(), InputId(to, toPin), &error);
    if (!result) {
        throw std::runtime_error(std::string("Connect ") + from.TypeName() + "." + fromPin + " -> " + to.TypeName() +
                                  "." + toPin + " failed (ConnectError " + std::to_string(static_cast<int>(error)) +
                                  ")");
    }
}

void SetDefault(Node& node, const char* pinName, ce::node_system::PinDefaultValue value) {
    for (auto& pin : const_cast<std::vector<ce::node_system::Pin>&>(node.Inputs())) {
        if (pin.name == pinName) {
            pin.defaultValue = std::move(value);
            return;
        }
    }
    throw std::runtime_error(std::string("node '") + node.TypeName() + "' has no input pin '" + pinName + "'");
}

} // namespace

std::unique_ptr<Graph> BuildBounceGraph() {
    const NodeTypeRegistry registry = ce::lang::nodegen::BuildCoreNodeCatalog();
    auto graphPtr = std::make_unique<Graph>("BounceDemo");
    Graph& graph = *graphPtr;

    Node& onStart = AddNode(graph, registry, NodeType::OnStart);
    Node& onTick = AddNode(graph, registry, NodeType::OnTick);

    // --- on_start: reset position, init variables, prove spawn/destroy/
    // log/while all compile and run via a harmless scratch entity. ---
    Node& seq0 = AddNode(graph, registry, NodeType::Sequence);
    Wire(graph, onStart, PinName::ExecOut, seq0, PinName::ExecIn);

    Node& setPos0 = AddNode(graph, registry, NodeType::SetPosition);
    Node& makeVec0 = AddNode(graph, registry, NodeType::MakeVec3); // x=y=z=0 (registry default).
    Wire(graph, seq0, PinName::ExecOut0, setPos0, PinName::ExecIn);
    Wire(graph, onStart, PinName::Self, setPos0, PinName::Entity);
    Wire(graph, makeVec0, PinName::Value, setPos0, PinName::Value);

    Node& setX0 = AddNode(graph, registry, NodeType::SetVariable);
    SetDefault(setX0, PinName::Name, std::string("x"));
    Node& setLoopCounter0 = AddNode(graph, registry, NodeType::SetVariable);
    SetDefault(setLoopCounter0, PinName::Name, std::string("loopCounter"));
    Wire(graph, seq0, PinName::ExecOut1, setX0, PinName::ExecIn);
    Wire(graph, setX0, PinName::ExecOut, setLoopCounter0, PinName::ExecIn);

    Node& spawn0 = AddNode(graph, registry, NodeType::Spawn);
    SetDefault(spawn0, PinName::Position, Vec3Default{ 10.0f, 0.0f, 0.0f });
    Node& log1 = AddNode(graph, registry, NodeType::Log);
    SetDefault(log1, PinName::Message, std::string("spawned scratch entity"));
    Node& destroy0 = AddNode(graph, registry, NodeType::Destroy);
    Node& log2 = AddNode(graph, registry, NodeType::Log);
    SetDefault(log2, PinName::Message, std::string("destroyed scratch entity"));
    Node& while0 = AddNode(graph, registry, NodeType::While);
    Wire(graph, seq0, PinName::ExecOut2, spawn0, PinName::ExecIn);
    Wire(graph, spawn0, PinName::ExecOut, log1, PinName::ExecIn);
    Wire(graph, log1, PinName::ExecOut, destroy0, PinName::ExecIn);
    Wire(graph, spawn0, PinName::Entity, destroy0, PinName::Entity);
    Wire(graph, destroy0, PinName::ExecOut, log2, PinName::ExecIn);
    Wire(graph, log2, PinName::ExecOut, while0, PinName::ExecIn);

    Node& getLoopCounterA = AddNode(graph, registry, NodeType::GetVariable);
    SetDefault(getLoopCounterA, PinName::Name, std::string("loopCounter"));
    Node& cmpLt0 = AddNode(graph, registry, NodeType::CompareLt);
    SetDefault(cmpLt0, PinName::B, 3.0f);
    Wire(graph, getLoopCounterA, PinName::Value, cmpLt0, PinName::A);
    Wire(graph, cmpLt0, PinName::Result, while0, PinName::Condition);

    Node& setLoopCounter1 = AddNode(graph, registry, NodeType::SetVariable);
    SetDefault(setLoopCounter1, PinName::Name, std::string("loopCounter"));
    Node& getLoopCounterB = AddNode(graph, registry, NodeType::GetVariable);
    SetDefault(getLoopCounterB, PinName::Name, std::string("loopCounter"));
    Node& add0 = AddNode(graph, registry, NodeType::Add);
    SetDefault(add0, PinName::B, 1.0f);
    Node& log3 = AddNode(graph, registry, NodeType::Log);
    SetDefault(log3, PinName::Message, std::string("loop tick"));
    Wire(graph, while0, PinName::LoopBody, setLoopCounter1, PinName::ExecIn);
    Wire(graph, getLoopCounterB, PinName::Value, add0, PinName::A);
    Wire(graph, add0, PinName::Result, setLoopCounter1, PinName::Value);
    Wire(graph, setLoopCounter1, PinName::ExecOut, log3, PinName::ExecIn);

    Node& log4 = AddNode(graph, registry, NodeType::Log);
    SetDefault(log4, PinName::Message, std::string("loop complete"));
    Wire(graph, while0, PinName::Completed, log4, PinName::ExecIn);

    // --- on_tick: constant-velocity motion along X, plus a side-branch
    // that logs once x has passed the midpoint (state-free, so it can't
    // desync the position math -- see BuildBounceGraph's own header
    // comment on why Branch reconvergence was deliberately avoided). ---
    Node& seq1 = AddNode(graph, registry, NodeType::Sequence);
    Wire(graph, onTick, PinName::ExecOut, seq1, PinName::ExecIn);

    Node& setX1 = AddNode(graph, registry, NodeType::SetVariable);
    SetDefault(setX1, PinName::Name, std::string("x"));
    Node& getX1 = AddNode(graph, registry, NodeType::GetVariable);
    SetDefault(getX1, PinName::Name, std::string("x"));
    Node& mul0 = AddNode(graph, registry, NodeType::Mul);
    SetDefault(mul0, PinName::A, 2.0f);
    Node& add1 = AddNode(graph, registry, NodeType::Add);
    Wire(graph, seq1, PinName::ExecOut0, setX1, PinName::ExecIn);
    Wire(graph, onTick, PinName::Dt, mul0, PinName::B);
    Wire(graph, getX1, PinName::Value, add1, PinName::A);
    Wire(graph, mul0, PinName::Result, add1, PinName::B);
    Wire(graph, add1, PinName::Result, setX1, PinName::Value);

    Node& setPos1 = AddNode(graph, registry, NodeType::SetPosition);
    Node& getX2 = AddNode(graph, registry, NodeType::GetVariable);
    SetDefault(getX2, PinName::Name, std::string("x"));
    Node& makeVec1 = AddNode(graph, registry, NodeType::MakeVec3);
    Wire(graph, setX1, PinName::ExecOut, setPos1, PinName::ExecIn);
    Wire(graph, onTick, PinName::Self, setPos1, PinName::Entity);
    Wire(graph, getX2, PinName::Value, makeVec1, PinName::X);
    Wire(graph, makeVec1, PinName::Value, setPos1, PinName::Value);

    Node& branch0 = AddNode(graph, registry, NodeType::Branch);
    Node& getX3 = AddNode(graph, registry, NodeType::GetVariable);
    SetDefault(getX3, PinName::Name, std::string("x"));
    Node& cmpGe0 = AddNode(graph, registry, NodeType::CompareGe);
    SetDefault(cmpGe0, PinName::B, 5.0f);
    Node& log5 = AddNode(graph, registry, NodeType::Log);
    SetDefault(log5, PinName::Message, std::string("passed midpoint"));
    Wire(graph, seq1, PinName::ExecOut1, branch0, PinName::ExecIn);
    Wire(graph, getX3, PinName::Value, cmpGe0, PinName::A);
    Wire(graph, cmpGe0, PinName::Result, branch0, PinName::Condition);
    Wire(graph, branch0, PinName::ExecTrue, log5, PinName::ExecIn);

    return graphPtr;
}

std::unique_ptr<Graph> BuildOperatorCoverageGraph() {
    const NodeTypeRegistry registry = ce::lang::nodegen::BuildCoreNodeCatalog();
    auto graphPtr = std::make_unique<Graph>("OperatorCoverage");
    Graph& graph = *graphPtr;

    Node& onTick = AddNode(graph, registry, NodeType::OnTick);

    // Every node below is wired into the exec-reachable chain (nested
    // inside the branches' execTrue paths) so its generated expression
    // text actually appears in the output -- codegen's exec-walk emits
    // BOTH sides of every Branch it statically reaches, regardless of
    // what a real run would evaluate, so nesting everything under
    // execTrue is enough; nothing here needs to be runnable (this graph
    // is only ever fed to --graph-to-source, never --run-graph -- see
    // nodegen_fixtures.h's own comment on why).
    Node& getPos = AddNode(graph, registry, NodeType::GetPosition);
    Wire(graph, onTick, PinName::Self, getPos, PinName::Entity);
    Node& setPos = AddNode(graph, registry, NodeType::SetPosition);
    Wire(graph, onTick, PinName::ExecOut, setPos, PinName::ExecIn);
    Wire(graph, onTick, PinName::Self, setPos, PinName::Entity);
    Wire(graph, getPos, PinName::Value, setPos, PinName::Value);

    Node& sub0 = AddNode(graph, registry, NodeType::Sub);
    SetDefault(sub0, PinName::A, 10.0f);
    SetDefault(sub0, PinName::B, 3.0f);
    Node& setX = AddNode(graph, registry, NodeType::SetVariable);
    SetDefault(setX, PinName::Name, std::string("x"));
    Wire(graph, setPos, PinName::ExecOut, setX, PinName::ExecIn);
    Wire(graph, sub0, PinName::Result, setX, PinName::Value);

    Node& div0 = AddNode(graph, registry, NodeType::Div);
    SetDefault(div0, PinName::A, 10.0f);
    SetDefault(div0, PinName::B, 3.0f);
    Node& setY = AddNode(graph, registry, NodeType::SetVariable);
    SetDefault(setY, PinName::Name, std::string("y"));
    Wire(graph, setX, PinName::ExecOut, setY, PinName::ExecIn);
    Wire(graph, div0, PinName::Result, setY, PinName::Value);

    Node& branchLe = AddNode(graph, registry, NodeType::Branch);
    Node& cmpLe = AddNode(graph, registry, NodeType::CompareLe);
    SetDefault(cmpLe, PinName::A, 1.0f);
    SetDefault(cmpLe, PinName::B, 2.0f);
    Wire(graph, setY, PinName::ExecOut, branchLe, PinName::ExecIn);
    Wire(graph, cmpLe, PinName::Result, branchLe, PinName::Condition);

    Node& branchGt = AddNode(graph, registry, NodeType::Branch);
    Node& cmpGt = AddNode(graph, registry, NodeType::CompareGt);
    SetDefault(cmpGt, PinName::A, 1.0f);
    SetDefault(cmpGt, PinName::B, 2.0f);
    Wire(graph, branchLe, PinName::ExecTrue, branchGt, PinName::ExecIn);
    Wire(graph, cmpGt, PinName::Result, branchGt, PinName::Condition);

    Node& branchEq = AddNode(graph, registry, NodeType::Branch);
    Node& cmpEq = AddNode(graph, registry, NodeType::CompareEq);
    SetDefault(cmpEq, PinName::A, 1.0f);
    SetDefault(cmpEq, PinName::B, 2.0f);
    Wire(graph, branchGt, PinName::ExecTrue, branchEq, PinName::ExecIn);
    Wire(graph, cmpEq, PinName::Result, branchEq, PinName::Condition);

    Node& branchNeq = AddNode(graph, registry, NodeType::Branch);
    Node& cmpNeq = AddNode(graph, registry, NodeType::CompareNeq);
    SetDefault(cmpNeq, PinName::A, 1.0f);
    SetDefault(cmpNeq, PinName::B, 2.0f);
    Wire(graph, branchEq, PinName::ExecTrue, branchNeq, PinName::ExecIn);
    Wire(graph, cmpNeq, PinName::Result, branchNeq, PinName::Condition);

    Node& call = AddNode(graph, registry, NodeType::CallFunction);
    SetDefault(call, PinName::Function, std::string("SomeHelper"));
    Wire(graph, branchNeq, PinName::ExecTrue, call, PinName::ExecIn);

    return graphPtr;
}

std::unique_ptr<Graph> BuildSubgraphGraph() {
    const NodeTypeRegistry registry = ce::lang::nodegen::BuildCoreNodeCatalog();
    auto graphPtr = std::make_unique<Graph>("SubgraphDemo");
    Graph& graph = *graphPtr;

    Node& onStart = AddNode(graph, registry, NodeType::OnStart);
    Node& helperEntry = AddNode(graph, registry, NodeType::SubgraphEntry);
    SetDefault(helperEntry, PinName::Name, std::string("Helper"));

    // Helper's own body: counter = counter + 1.0.
    Node& getCounter = AddNode(graph, registry, NodeType::GetVariable);
    SetDefault(getCounter, PinName::Name, std::string("counter"));
    Node& add0 = AddNode(graph, registry, NodeType::Add);
    SetDefault(add0, PinName::B, 1.0f);
    Node& setCounter = AddNode(graph, registry, NodeType::SetVariable);
    SetDefault(setCounter, PinName::Name, std::string("counter"));
    Wire(graph, helperEntry, PinName::ExecOut, setCounter, PinName::ExecIn);
    Wire(graph, getCounter, PinName::Value, add0, PinName::A);
    Wire(graph, add0, PinName::Result, setCounter, PinName::Value);

    // on_start: counter = 0.0; Helper(); Helper(); set_position(self, vec3(counter,0,0)).
    Node& setCounter0 = AddNode(graph, registry, NodeType::SetVariable);
    SetDefault(setCounter0, PinName::Name, std::string("counter"));
    Node& call1 = AddNode(graph, registry, NodeType::CallSubgraph);
    SetDefault(call1, PinName::Name, std::string("Helper"));
    Node& call2 = AddNode(graph, registry, NodeType::CallSubgraph);
    SetDefault(call2, PinName::Name, std::string("Helper"));
    Node& setPos = AddNode(graph, registry, NodeType::SetPosition);
    Node& getCounter2 = AddNode(graph, registry, NodeType::GetVariable);
    SetDefault(getCounter2, PinName::Name, std::string("counter"));
    Node& makeVec = AddNode(graph, registry, NodeType::MakeVec3);

    Wire(graph, onStart, PinName::ExecOut, setCounter0, PinName::ExecIn);
    Wire(graph, setCounter0, PinName::ExecOut, call1, PinName::ExecIn);
    Wire(graph, call1, PinName::ExecOut, call2, PinName::ExecIn);
    Wire(graph, call2, PinName::ExecOut, setPos, PinName::ExecIn);
    Wire(graph, onStart, PinName::Self, setPos, PinName::Entity);
    Wire(graph, getCounter2, PinName::Value, makeVec, PinName::X);
    Wire(graph, makeVec, PinName::Value, setPos, PinName::Value);

    return graphPtr;
}

std::unique_ptr<Graph> BuildDiagnosticMappingGraph() {
    const NodeTypeRegistry registry = ce::lang::nodegen::BuildCoreNodeCatalog();
    auto graphPtr = std::make_unique<Graph>("DiagnosticMappingDemo");
    Graph& graph = *graphPtr;

    Node& onStart = AddNode(graph, registry, NodeType::OnStart);
    Node& call = AddNode(graph, registry, NodeType::CallFunction);
    SetDefault(call, PinName::Function, std::string("NonexistentHelper"));
    Wire(graph, onStart, PinName::ExecOut, call, PinName::ExecIn);

    return graphPtr;
}

std::unique_ptr<Graph> BuildTraceDemoGraph() {
    const NodeTypeRegistry registry = ce::lang::nodegen::BuildCoreNodeCatalog();
    auto graphPtr = std::make_unique<Graph>("TraceDemo");
    Graph& graph = *graphPtr;

    Node& onStart = AddNode(graph, registry, NodeType::OnStart);
    Node& log = AddNode(graph, registry, NodeType::Log);
    SetDefault(log, PinName::Message, std::string("trace demo"));
    Wire(graph, onStart, PinName::ExecOut, log, PinName::ExecIn);

    return graphPtr;
}

} // namespace ce::lang::tools
