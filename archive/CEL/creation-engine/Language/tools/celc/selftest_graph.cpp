#include "selftest_graph.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

#include "node_system/celg_serialization.h"
#include "node_system/graph.h"
#include "node_system/graph_analysis.h"
#include "node_system/type_registry.h"

namespace ce::lang::tools {

namespace {

using namespace ce::node_system;

bool GraphsStructurallyEqual(const Graph& a, const Graph& b, std::string& mismatchOut) {
    if (a.Name() != b.Name()) {
        mismatchOut = "graph name differs";
        return false;
    }
    if (a.Nodes().size() != b.Nodes().size()) {
        mismatchOut = "node count differs";
        return false;
    }
    for (const auto& [id, nodeA] : a.Nodes()) {
        const Node* nodeB = b.FindNode(id);
        if (nodeB == nullptr) {
            mismatchOut = "node " + std::to_string(id) + " missing in reloaded graph";
            return false;
        }
        if (nodeA->TypeName() != nodeB->TypeName() || nodeA->NodeDomain() != nodeB->NodeDomain()) {
            mismatchOut = "node " + std::to_string(id) + " type/domain differs";
            return false;
        }
        if (nodeA->EditorX() != nodeB->EditorX() || nodeA->EditorY() != nodeB->EditorY()) {
            mismatchOut = "node " + std::to_string(id) + " editor position differs";
            return false;
        }
        if (!(nodeA->Inputs() == nodeB->Inputs())) {
            mismatchOut = "node " + std::to_string(id) + " inputs differ";
            return false;
        }
        if (!(nodeA->Outputs() == nodeB->Outputs())) {
            mismatchOut = "node " + std::to_string(id) + " outputs differ";
            return false;
        }
    }

    if (a.Connections().size() != b.Connections().size()) {
        mismatchOut = "connection count differs";
        return false;
    }
    std::vector<Connection> ca = a.Connections();
    std::vector<Connection> cb = b.Connections();
    std::sort(ca.begin(), ca.end(), [](const Connection& x, const Connection& y) { return x.id < y.id; });
    std::sort(cb.begin(), cb.end(), [](const Connection& x, const Connection& y) { return x.id < y.id; });
    if (!(ca == cb)) {
        mismatchOut = "connections differ";
        return false;
    }
    return true;
}

// A minimal, test-only type catalog -- NOT the real v1 Core/Event
// catalog (that's GS9 scope). Just enough to exercise
// AddRegisteredNode/ValidateAgainstRegistry and give the round-trip
// test real Exec + Data pins (with a default value on one input) to
// serialize.
NodeTypeRegistry BuildTestRegistry() {
    NodeTypeRegistry registry;

    NodeTypeDescriptor source;
    source.typeName = "TestSource";
    source.domain = Domain::Core;
    source.outputs.push_back({ "execOut", PinTypeDesc{ PinKind::Exec, DataType::Float }, {} });
    source.outputs.push_back({ "value", PinTypeDesc{ PinKind::Data, DataType::Float }, {} });
    registry.Register(source);

    NodeTypeDescriptor sink;
    sink.typeName = "TestSink";
    sink.domain = Domain::Core;
    sink.inputs.push_back({ "execIn", PinTypeDesc{ PinKind::Exec, DataType::Float }, {} });
    sink.inputs.push_back({ "value", PinTypeDesc{ PinKind::Data, DataType::Float }, 3.5f });
    registry.Register(sink);

    return registry;
}

bool TestRoundTrip() {
    NodeTypeRegistry registry = BuildTestRegistry();
    Graph original("SelfTestGraph");

    Node* source = AddRegisteredNode(original, registry, "TestSource");
    Node* sink = AddRegisteredNode(original, registry, "TestSink");
    if (source == nullptr || sink == nullptr) {
        std::cout << "  [round-trip] AddRegisteredNode failed" << std::endl;
        return false;
    }
    source->SetEditorPosition(100.0f, 200.0f);
    sink->SetEditorPosition(400.0f, 200.0f);

    if (!ValidateAgainstRegistry(original, registry)) {
        std::cout << "  [round-trip] freshly-built graph unexpectedly failed registry validation" << std::endl;
        return false;
    }

    ConnectError connectError;
    const auto execConn =
        original.Connect(source->Id(), source->Outputs()[0].id, sink->Id(), sink->Inputs()[0].id, &connectError);
    const auto dataConn =
        original.Connect(source->Id(), source->Outputs()[1].id, sink->Id(), sink->Inputs()[1].id, &connectError);
    if (!execConn || !dataConn) {
        std::cout << "  [round-trip] Connect failed" << std::endl;
        return false;
    }

    const std::string serialized = SerializeGraph(original);

    std::string parseError;
    auto reloaded = DeserializeGraph(serialized, parseError);
    if (!reloaded) {
        std::cout << "  [round-trip] DeserializeGraph failed: " << parseError << std::endl;
        return false;
    }

    std::string mismatch;
    if (!GraphsStructurallyEqual(original, *reloaded, mismatch)) {
        std::cout << "  [round-trip] structural mismatch: " << mismatch << std::endl;
        return false;
    }

    // A second round trip must reproduce IDENTICAL text -- the actual
    // format-stability property GS8's committed .celg fixture regression
    // test depends on.
    const std::string reserialized = SerializeGraph(*reloaded);
    if (serialized != reserialized) {
        std::cout << "  [round-trip] re-serialization is not byte-identical" << std::endl;
        return false;
    }

    std::cout << "  [round-trip] OK (" << serialized.size() << " bytes)" << std::endl;
    return true;
}

bool TestExecCycleDetection() {
    Graph graph("CycleTest");
    Node& a = graph.AddNode("A", Domain::Core);
    Node& b = graph.AddNode("B", Domain::Core);
    const PinId aOut = a.AddOutput("out", PinTypeDesc{ PinKind::Exec, DataType::Float });
    const PinId aIn = a.AddInput("in", PinTypeDesc{ PinKind::Exec, DataType::Float });
    const PinId bOut = b.AddOutput("out", PinTypeDesc{ PinKind::Exec, DataType::Float });
    const PinId bIn = b.AddInput("in", PinTypeDesc{ PinKind::Exec, DataType::Float });

    graph.Connect(a.Id(), aOut, b.Id(), bIn);
    graph.Connect(b.Id(), bOut, a.Id(), aIn); // closes the loop.

    const auto cycle = DetectExecCycle(graph);
    if (!cycle) {
        std::cout << "  [exec-cycle] expected a cycle to be detected, found none" << std::endl;
        return false;
    }
    std::cout << "  [exec-cycle] OK (cycle length " << cycle->size() << ")" << std::endl;

    // An acyclic graph must NOT report one -- guards against a
    // detector that's just permanently "yes."
    Graph acyclic("AcyclicTest");
    Node& c = acyclic.AddNode("C", Domain::Core);
    Node& d = acyclic.AddNode("D", Domain::Core);
    const PinId cOut = c.AddOutput("out", PinTypeDesc{ PinKind::Exec, DataType::Float });
    const PinId dIn = d.AddInput("in", PinTypeDesc{ PinKind::Exec, DataType::Float });
    acyclic.Connect(c.Id(), cOut, d.Id(), dIn);
    if (DetectExecCycle(acyclic)) {
        std::cout << "  [exec-cycle] false positive on an acyclic graph" << std::endl;
        return false;
    }

    return true;
}

bool TestIncompatibleTypeRejection() {
    Graph graph("IncompatibleTest");
    Node& a = graph.AddNode("A", Domain::Core);
    Node& b = graph.AddNode("B", Domain::Core);
    const PinId floatOut = a.AddOutput("out", PinTypeDesc{ PinKind::Data, DataType::Float });
    const PinId boolIn = b.AddInput("in", PinTypeDesc{ PinKind::Data, DataType::Bool });

    ConnectError connectError;
    const auto result = graph.Connect(a.Id(), floatOut, b.Id(), boolIn, &connectError);
    if (result) {
        std::cout << "  [incompatible-types] expected rejection, connection succeeded" << std::endl;
        return false;
    }
    if (connectError != ConnectError::IncompatibleTypes) {
        std::cout << "  [incompatible-types] expected ConnectError::IncompatibleTypes, got a different reason"
                   << std::endl;
        return false;
    }
    std::cout << "  [incompatible-types] OK" << std::endl;
    return true;
}

} // namespace

int RunSelfTestGraph() {
    bool ok = true;
    ok = TestRoundTrip() && ok;
    ok = TestExecCycleDetection() && ok;
    ok = TestIncompatibleTypeRejection() && ok;

    std::cout << (ok ? "[celc] selftest-graph: PASS" : "[celc] selftest-graph: FAIL") << std::endl;
    return ok ? 0 : 1;
}

} // namespace ce::lang::tools
