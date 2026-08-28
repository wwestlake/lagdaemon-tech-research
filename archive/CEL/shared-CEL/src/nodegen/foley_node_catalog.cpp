#include "lang/nodegen/foley_node_catalog.h"

namespace ce::lang::nodegen::foley {

using ce::node_system::DataType;
using ce::node_system::Domain;
using ce::node_system::NodeTypeDescriptor;
using ce::node_system::NodeTypeRegistry;
using ce::node_system::PinKind;
using ce::node_system::PinSignature;
using ce::node_system::PinTypeDesc;

namespace {

// Same small pin-builder helpers as node_catalog.cpp's own anonymous namespace - not shared
// between the two catalogs (each is a self-contained, independently-buildable translation unit,
// and the helpers are a handful of one-line wrappers, not worth threading a shared header just to
// avoid the duplication).
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

// String pins here are always unconnectable "configuration" values (a sample name), same
// reasoning as node_catalog.cpp's StringConfig.
PinSignature StringConfig(const char* name, std::string defaultValue = "") {
    return PinSignature{ name, DataPin(DataType::String), std::move(defaultValue) };
}

void RegisterEventNodes(NodeTypeRegistry& registry) {
    registry.Register(NodeTypeDescriptor{
        NodeType::OnTrigger, Domain::Event, /*inputs=*/{},
        /*outputs=*/{ Exec(PinName::ExecOut) } });
}

void RegisterAudioNodes(NodeTypeRegistry& registry) {
    registry.Register(NodeTypeDescriptor{
        NodeType::PlaySample, Domain::Audio,
        /*inputs=*/{ Exec(PinName::ExecIn), StringConfig(PinName::SampleName) },
        /*outputs=*/{ Exec(PinName::ExecOut) } });

    registry.Register(NodeTypeDescriptor{
        NodeType::GainMix, Domain::Audio,
        /*inputs=*/{ Exec(PinName::ExecIn), Float(PinName::Gain, 1.0f) },
        /*outputs=*/{ Exec(PinName::ExecOut) } });
}

void RegisterControlFlowNodes(NodeTypeRegistry& registry) {
    registry.Register(NodeTypeDescriptor{
        NodeType::Sequence, Domain::Core, /*inputs=*/{ Exec(PinName::ExecIn) },
        /*outputs=*/{ Exec(PinName::ExecOut0), Exec(PinName::ExecOut1), Exec(PinName::ExecOut2) } });

    registry.Register(NodeTypeDescriptor{
        NodeType::Branch, Domain::Core, /*inputs=*/{ Exec(PinName::ExecIn), Bool(PinName::Condition) },
        /*outputs=*/{ Exec(PinName::ExecTrue), Exec(PinName::ExecFalse) } });

    // Fixed 50/50 two-way pick - no weighting input yet (a natural additive follow-up, same
    // shape as Sequence's fixed 3-way arity needing an editor affordance for variable arity).
    registry.Register(NodeTypeDescriptor{
        NodeType::RandomSelect, Domain::Core, /*inputs=*/{ Exec(PinName::ExecIn) },
        /*outputs=*/{ Exec(PinName::ExecA), Exec(PinName::ExecB) } });

    registry.Register(NodeTypeDescriptor{
        NodeType::Delay, Domain::Core,
        /*inputs=*/{ Exec(PinName::ExecIn), Float(PinName::Seconds, 0.0f) },
        /*outputs=*/{ Exec(PinName::ExecOut) } });
}

} // namespace

NodeTypeRegistry BuildFoleyNodeCatalog() {
    NodeTypeRegistry registry;
    RegisterEventNodes(registry);
    RegisterAudioNodes(registry);
    RegisterControlFlowNodes(registry);
    return registry;
}

} // namespace ce::lang::nodegen::foley
