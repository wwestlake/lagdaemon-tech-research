#include "lang/nodegen/foley_graph_to_source.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <type_traits>
#include <variant>

#include "lang/nodegen/foley_node_catalog.h"
#include "node_system/graph_analysis.h"

namespace ce::lang::nodegen::foley {

using ce::node_system::Connection;
using ce::node_system::DataType;
using ce::node_system::Graph;
using ce::node_system::Node;
using ce::node_system::NodeId;
using ce::node_system::NodeTypeRegistry;
using ce::node_system::Pin;
using ce::node_system::PinId;

namespace {

std::string Indent(int level) {
    return std::string(static_cast<std::size_t>(level) * 4, ' ');
}

const Pin* FindInput(const Node& node, const std::string& name) {
    for (const Pin& p : node.Inputs()) {
        if (p.name == name) return &p;
    }
    return nullptr;
}

const Pin* FindOutput(const Node& node, const std::string& name) {
    for (const Pin& p : node.Outputs()) {
        if (p.name == name) return &p;
    }
    return nullptr;
}

std::vector<const Connection*> FindOutgoing(const Graph& graph, NodeId nodeId, PinId pinId) {
    std::vector<const Connection*> result;
    for (const Connection& c : graph.Connections()) {
        if (c.fromNode == nodeId && c.fromPin == pinId) {
            result.push_back(&c);
        }
    }
    return result;
}

std::vector<const Connection*> FindIncoming(const Graph& graph, NodeId nodeId, PinId pinId) {
    std::vector<const Connection*> result;
    for (const Connection& c : graph.Connections()) {
        if (c.toNode == nodeId && c.toPin == pinId) {
            result.push_back(&c);
        }
    }
    return result;
}

std::string FormatFloatLiteral(float v) {
    std::ostringstream os;
    os << std::setprecision(9) << v;
    std::string s = os.str();
    if (s.find('.') == std::string::npos && s.find('e') == std::string::npos &&
        s.find("nan") == std::string::npos && s.find("inf") == std::string::npos) {
        s += ".0";
    }
    return s;
}

std::string EscapeCelString(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    return out;
}

// Walks the exec chain reachable from the OnTrigger entry node and emits one CEL statement/block
// per exec node encountered, plus whatever literal/inline expressions its Data wires resolve to.
// See foley_graph_to_source.h's own comment for why this emits text rather than building an AST,
// and for why it's a separate class from graph_to_source.cpp's FunctionEmitter rather than a
// shared/extended one.
class FoleyFunctionEmitter {
public:
    explicit FoleyFunctionEmitter(const Graph& graph) : graph_(graph) {}

    std::string Emit(const Node& entryNode) { return EmitNext(entryNode, PinName::ExecOut, 1); }

    const std::vector<std::string>& Errors() const { return errors_; }

private:
    const Graph& graph_;
    std::vector<std::string> errors_;

    void Error(const Node& node, const std::string& message) {
        errors_.push_back("node " + std::to_string(node.Id()) + " ('" + node.TypeName() + "'): " + message);
    }

    std::string EmitNext(const Node& node, const std::string& pinName, int indent) {
        const Pin* pin = FindOutput(node, pinName);
        if (pin == nullptr) {
            Error(node, "missing expected exec-out pin '" + pinName + "'");
            return {};
        }
        const auto outgoing = FindOutgoing(graph_, node.Id(), pin->id);
        if (outgoing.empty()) {
            return {};
        }
        if (outgoing.size() > 1) {
            Error(node, "exec-out pin '" + pinName + "' has more than one outgoing connection (ambiguous control flow)");
            return {};
        }
        const Node* next = graph_.FindNode(outgoing.front()->toNode);
        if (next == nullptr) {
            Error(node, "exec-out pin '" + pinName + "' targets a node that no longer exists");
            return {};
        }
        return EmitNode(*next, indent);
    }

    std::string EmitNode(const Node& node, int indent) {
        const std::string& type = node.TypeName();
        const std::string comment = Indent(indent) + "// @node " + std::to_string(node.Id()) + "\n";

        if (type == NodeType::PlaySample) {
            // sampleName is a literal config pin (never wired - see foley_node_catalog.h), so
            // its value is known at generation time and embedded directly in the log call, same
            // as the core catalog's own Log node.
            const std::string name = ConfigString(node, PinName::SampleName);
            const std::string stmt = "log(\"PlaySample: " + EscapeCelString(name) + "\");";
            return comment + Indent(indent) + stmt + "\n" + EmitNext(node, PinName::ExecOut, indent);
        }
        if (type == NodeType::GainMix) {
            const std::string stmt = "log_float(\"GainMix gain: \", " + EmitExpr(node, PinName::Gain) + ");";
            return comment + Indent(indent) + stmt + "\n" + EmitNext(node, PinName::ExecOut, indent);
        }
        if (type == NodeType::Delay) {
            // Placeholder: logs the requested delay but does not actually suspend execution -
            // CEL has no coroutine/async suspension mechanism yet, so a real timed delay needs
            // new runtime scheduling support, not just codegen. Continues to ExecOut immediately.
            const std::string stmt =
                "log_float(\"Delay (not yet scheduled - runs immediately): \", " + EmitExpr(node, PinName::Seconds) + ");";
            return comment + Indent(indent) + stmt + "\n" + EmitNext(node, PinName::ExecOut, indent);
        }
        if (type == NodeType::Sequence) {
            // No comment of its own: a pure codegen organizational construct, same as the core
            // catalog's Sequence.
            std::string out;
            out += EmitNext(node, PinName::ExecOut0, indent);
            out += EmitNext(node, PinName::ExecOut1, indent);
            out += EmitNext(node, PinName::ExecOut2, indent);
            return out;
        }
        if (type == NodeType::Branch) {
            const std::string cond = EmitExpr(node, PinName::Condition);
            const std::string trueBody = EmitNext(node, PinName::ExecTrue, indent + 1);
            const std::string falseBody = EmitNext(node, PinName::ExecFalse, indent + 1);
            std::string out = comment + Indent(indent) + "if (" + cond + ") {\n" + trueBody + Indent(indent) + "}";
            if (!falseBody.empty()) {
                out += " else {\n" + falseBody + Indent(indent) + "}";
            }
            out += "\n";
            return out;
        }
        if (type == NodeType::RandomSelect) {
            // Placeholder: always takes the first branch (execA) - real randomness needs a new
            // CEL intrinsic (e.g. rand_float()), which doesn't exist yet (see intrinsics.def).
            // Deterministic-but-honest rather than silently wrong: this comment plus the
            // generated source's own "always A" behavior make the limitation visible, not hidden.
            return comment + Indent(indent) + "// RandomSelect: real randomness not implemented yet, always picks execA\n" +
                   EmitNext(node, PinName::ExecA, indent);
        }

        Error(node, "not an exec-chain (statement/control) node type");
        return {};
    }

    std::string EmitExpr(const Node& node, const std::string& pinName) {
        const Pin* pin = FindInput(node, pinName);
        if (pin == nullptr) {
            Error(node, "missing expected input pin '" + pinName + "'");
            return "0.0";
        }
        const auto incoming = FindIncoming(graph_, node.Id(), pin->id);
        if (incoming.size() > 1) {
            Error(node, "input pin '" + pinName + "' has more than one incoming connection (ambiguous value)");
            return "0.0";
        }
        if (incoming.empty()) {
            return LiteralFromDefault(node, *pin);
        }
        const Connection& conn = *incoming.front();
        const Node* source = graph_.FindNode(conn.fromNode);
        if (source == nullptr) {
            Error(node, "input pin '" + pinName + "' is wired to a node that no longer exists");
            return "0.0";
        }
        const Pin* sourcePin = source->FindPin(conn.fromPin);
        if (sourcePin == nullptr) {
            Error(node, "input pin '" + pinName + "' is wired to a pin that no longer exists");
            return "0.0";
        }
        Error(*source, "output pin '" + sourcePin->name + "' has no expression mapping (Foley's catalog has no "
                        "data-producing node types yet - every Float/Bool input must use its literal default)");
        return "0.0";
    }

    std::string LiteralFromDefault(const Node& node, const Pin& pin) {
        return std::visit(
            [&](auto&& v) -> std::string {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    switch (pin.type.dataType) {
                        case DataType::Bool: return "false";
                        case DataType::String: return "\"\"";
                        default: return "0.0";
                    }
                } else if constexpr (std::is_same_v<T, float>) {
                    return FormatFloatLiteral(v);
                } else if constexpr (std::is_same_v<T, bool>) {
                    return v ? "true" : "false";
                } else if constexpr (std::is_same_v<T, std::string>) {
                    return "\"" + EscapeCelString(v) + "\"";
                } else {
                    (void) node;
                    return "0.0";
                }
            },
            pin.defaultValue);
    }

    std::string ConfigString(const Node& node, const std::string& pinName) {
        const Pin* pin = FindInput(node, pinName);
        if (pin == nullptr) {
            Error(node, "missing expected config pin '" + pinName + "'");
            return "";
        }
        if (const auto* s = std::get_if<std::string>(&pin->defaultValue)) {
            return *s;
        }
        return "";
    }
};

} // namespace

FoleyGraphToSourceResult GenerateFoleySource(const Graph& graph, const NodeTypeRegistry& registry) {
    FoleyGraphToSourceResult result;

    const auto validation = ce::node_system::ValidateGraph(graph, &registry);
    if (!validation.ok) {
        result.errors = validation.errors;
        return result;
    }

    std::vector<NodeId> nodeIds;
    for (const auto& [id, node] : graph.Nodes()) {
        nodeIds.push_back(id);
    }
    std::sort(nodeIds.begin(), nodeIds.end());

    const Node* onTrigger = nullptr;
    for (NodeId id : nodeIds) {
        const Node* node = graph.FindNode(id);
        if (node->TypeName() == NodeType::OnTrigger) {
            if (onTrigger != nullptr) {
                result.errors.push_back("graph may contain at most one OnTrigger node");
            }
            onTrigger = node;
        }
    }
    if (onTrigger == nullptr) {
        result.errors.push_back("graph has no OnTrigger node -- nothing to generate");
    }
    if (!result.errors.empty()) {
        return result;
    }

    FoleyFunctionEmitter emitter(graph);
    const std::string body = emitter.Emit(*onTrigger);
    result.errors.insert(result.errors.end(), emitter.Errors().begin(), emitter.Errors().end());
    if (!result.errors.empty()) {
        return result;
    }

    std::ostringstream out;
    out << "// Generated by the Foley panel from graph '" << graph.Name() << "'.\n"
        << "// Do not hand-edit -- edit the node graph instead.\n\n"
        << "func on_trigger() {\n" << body << "}\n";

    result.ok = true;
    result.source = out.str();
    return result;
}

} // namespace ce::lang::nodegen::foley
