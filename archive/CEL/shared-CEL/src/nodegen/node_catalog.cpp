#include "lang/nodegen/node_catalog.h"

namespace ce::lang::nodegen {

using ce::node_system::DataType;
using ce::node_system::Domain;
using ce::node_system::NodeTypeDescriptor;
using ce::node_system::NodeTypeRegistry;
using ce::node_system::PinKind;
using ce::node_system::PinSignature;
using ce::node_system::PinTypeDesc;
using ce::node_system::Vec3Default;

namespace {

PinTypeDesc ExecPin() {
    return PinTypeDesc{ PinKind::Exec, DataType::Float };
}

PinTypeDesc DataPin(DataType type) {
    return PinTypeDesc{ PinKind::Data, type };
}

PinSignature Exec(const char* name) {
    return PinSignature{ name, ExecPin(), {} };
}

PinSignature Float(const char* name, float defaultValue = 0.0f) {
    return PinSignature{ name, DataPin(DataType::Float), defaultValue };
}

PinSignature Bool(const char* name, bool defaultValue = false) {
    return PinSignature{ name, DataPin(DataType::Bool), defaultValue };
}

PinSignature Vec3(const char* name, Vec3Default defaultValue = {}) {
    return PinSignature{ name, DataPin(DataType::Vec3), defaultValue };
}

// Entity has no CEL literal syntax (see docs/SCRIPTING_ABI.md), so an
// Entity-typed pin never gets a meaningful default -- monostate here
// means "must be connected," enforced by graph_to_source.cpp at
// codegen time, not by NodeSystem itself.
PinSignature Entity(const char* name) {
    return PinSignature{ name, DataPin(DataType::Entity), {} };
}

// String pins in this catalog are always unconnectable "configuration"
// values (a variable name, a log message, a target function name) --
// see node_catalog.h's own comment on why. Giving them a default of ""
// rather than monostate keeps codegen's literal formatting uniform.
PinSignature StringConfig(const char* name, std::string defaultValue = "") {
    return PinSignature{ name, DataPin(DataType::String), std::move(defaultValue) };
}

void RegisterEventNodes(NodeTypeRegistry& registry) {
    registry.Register(NodeTypeDescriptor{
        NodeType::OnStart, Domain::Event, /*inputs=*/{},
        /*outputs=*/{ Exec(PinName::ExecOut), Entity(PinName::Self) } });

    registry.Register(NodeTypeDescriptor{
        NodeType::OnTick, Domain::Event, /*inputs=*/{},
        /*outputs=*/{ Exec(PinName::ExecOut), Entity(PinName::Self), Float(PinName::Dt) } });
}

void RegisterControlFlowNodes(NodeTypeRegistry& registry) {
    registry.Register(NodeTypeDescriptor{
        NodeType::Sequence, Domain::Core, /*inputs=*/{ Exec(PinName::ExecIn) },
        /*outputs=*/{ Exec(PinName::ExecOut0), Exec(PinName::ExecOut1), Exec(PinName::ExecOut2) } });

    registry.Register(NodeTypeDescriptor{
        NodeType::Branch, Domain::Core, /*inputs=*/{ Exec(PinName::ExecIn), Bool(PinName::Condition) },
        /*outputs=*/{ Exec(PinName::ExecTrue), Exec(PinName::ExecFalse) } });

    registry.Register(NodeTypeDescriptor{
        NodeType::While, Domain::Core, /*inputs=*/{ Exec(PinName::ExecIn), Bool(PinName::Condition) },
        /*outputs=*/{ Exec(PinName::LoopBody), Exec(PinName::Completed) } });
}

void RegisterCompareNode(NodeTypeRegistry& registry, const char* typeName) {
    registry.Register(NodeTypeDescriptor{
        typeName, Domain::Core, /*inputs=*/{ Float(PinName::A), Float(PinName::B) },
        /*outputs=*/{ Bool(PinName::Result) } });
}

void RegisterArithmeticNode(NodeTypeRegistry& registry, const char* typeName) {
    registry.Register(NodeTypeDescriptor{
        typeName, Domain::Core, /*inputs=*/{ Float(PinName::A), Float(PinName::B) },
        /*outputs=*/{ Float(PinName::Result) } });
}

void RegisterMathNodes(NodeTypeRegistry& registry) {
    RegisterCompareNode(registry, NodeType::CompareLt);
    RegisterCompareNode(registry, NodeType::CompareLe);
    RegisterCompareNode(registry, NodeType::CompareGt);
    RegisterCompareNode(registry, NodeType::CompareGe);
    RegisterCompareNode(registry, NodeType::CompareEq);
    RegisterCompareNode(registry, NodeType::CompareNeq);

    RegisterArithmeticNode(registry, NodeType::Add);
    RegisterArithmeticNode(registry, NodeType::Sub);
    RegisterArithmeticNode(registry, NodeType::Mul);
    RegisterArithmeticNode(registry, NodeType::Div);

    registry.Register(NodeTypeDescriptor{
        NodeType::MakeVec3, Domain::Core, /*inputs=*/{ Float(PinName::X), Float(PinName::Y), Float(PinName::Z) },
        /*outputs=*/{ Vec3(PinName::Value) } });
}

void RegisterWorldNodes(NodeTypeRegistry& registry) {
    registry.Register(NodeTypeDescriptor{
        NodeType::GetPosition, Domain::Core, /*inputs=*/{ Entity(PinName::Entity) },
        /*outputs=*/{ Vec3(PinName::Value) } });

    registry.Register(NodeTypeDescriptor{
        NodeType::SetPosition, Domain::Core,
        /*inputs=*/{ Exec(PinName::ExecIn), Entity(PinName::Entity), Vec3(PinName::Value) },
        /*outputs=*/{ Exec(PinName::ExecOut) } });

    registry.Register(NodeTypeDescriptor{
        NodeType::Spawn, Domain::Core, /*inputs=*/{ Exec(PinName::ExecIn), Vec3(PinName::Position) },
        /*outputs=*/{ Exec(PinName::ExecOut), Entity(PinName::Entity) } });

    registry.Register(NodeTypeDescriptor{
        NodeType::Destroy, Domain::Core, /*inputs=*/{ Exec(PinName::ExecIn), Entity(PinName::Entity) },
        /*outputs=*/{ Exec(PinName::ExecOut) } });
}

void RegisterUtilityNodes(NodeTypeRegistry& registry) {
    registry.Register(NodeTypeDescriptor{
        NodeType::Log, Domain::Core, /*inputs=*/{ Exec(PinName::ExecIn), StringConfig(PinName::Message) },
        /*outputs=*/{ Exec(PinName::ExecOut) } });

    registry.Register(NodeTypeDescriptor{
        NodeType::GetVariable, Domain::Core, /*inputs=*/{ StringConfig(PinName::Name) },
        /*outputs=*/{ Float(PinName::Value) } });

    registry.Register(NodeTypeDescriptor{
        NodeType::SetVariable, Domain::Core,
        /*inputs=*/{ Exec(PinName::ExecIn), StringConfig(PinName::Name), Float(PinName::Value) },
        /*outputs=*/{ Exec(PinName::ExecOut) } });

    registry.Register(NodeTypeDescriptor{
        NodeType::CallFunction, Domain::Core, /*inputs=*/{ Exec(PinName::ExecIn), StringConfig(PinName::Function) },
        /*outputs=*/{ Exec(PinName::ExecOut) } });

    // GS11: SubgraphEntry has no execIn (it's a root, like OnStart/OnTick)
    // -- its "name" config pin has no wire counterpart to accept either,
    // same reasoning as every other config-only String pin.
    registry.Register(NodeTypeDescriptor{
        NodeType::SubgraphEntry, Domain::Core, /*inputs=*/{ StringConfig(PinName::Name) },
        /*outputs=*/{ Exec(PinName::ExecOut) } });

    registry.Register(NodeTypeDescriptor{
        NodeType::CallSubgraph, Domain::Core, /*inputs=*/{ Exec(PinName::ExecIn), StringConfig(PinName::Name) },
        /*outputs=*/{ Exec(PinName::ExecOut) } });
}

} // namespace

NodeTypeRegistry BuildCoreNodeCatalog() {
    NodeTypeRegistry registry;
    RegisterEventNodes(registry);
    RegisterControlFlowNodes(registry);
    RegisterMathNodes(registry);
    RegisterWorldNodes(registry);
    RegisterUtilityNodes(registry);
    return registry;
}

} // namespace ce::lang::nodegen
