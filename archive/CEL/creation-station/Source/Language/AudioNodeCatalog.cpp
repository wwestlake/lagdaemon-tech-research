#include "AudioNodeCatalog.h"

namespace cw::audionodes
{

using ce::node_system::DataType;
using ce::node_system::Domain;
using ce::node_system::NodeTypeDescriptor;
using ce::node_system::NodeTypeRegistry;
using ce::node_system::PinKind;
using ce::node_system::PinSignature;
using ce::node_system::PinTypeDesc;

namespace
{

PinTypeDesc SignalPin()
{
    return PinTypeDesc{ PinKind::Data, DataType::AudioSignal };
}

PinTypeDesc FloatPin()
{
    return PinTypeDesc{ PinKind::Data, DataType::Float };
}

PinSignature Signal(const char* name)
{
    return PinSignature{ name, SignalPin(), {} };
}

PinSignature Float(const char* name, float defaultValue)
{
    return PinSignature{ name, FloatPin(), defaultValue };
}

} // namespace

NodeTypeRegistry BuildAudioNodeCatalog()
{
    NodeTypeRegistry registry;

    registry.Register(NodeTypeDescriptor{
        NodeType::SineOscillator, Domain::Audio,
        /*inputs=*/{ Float(PinName::Level, 0.65f) },
        /*outputs=*/{ Signal(PinName::SignalOut) } });

    registry.Register(NodeTypeDescriptor{
        NodeType::Output, Domain::Audio,
        /*inputs=*/{ Signal(PinName::SignalIn) },
        /*outputs=*/{} });

    return registry;
}

} // namespace cw::audionodes
