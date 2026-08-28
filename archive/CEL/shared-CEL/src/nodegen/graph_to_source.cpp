#include "lang/nodegen/graph_to_source.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "lang/ast.h"
#include "lang/compiler.h"
#include "lang/nodegen/node_catalog.h"
#include "lang/sema.h"
#include "node_system/graph_analysis.h"

namespace ce::lang::nodegen {

using ce::node_system::Connection;
using ce::node_system::DataType;
using ce::node_system::Graph;
using ce::node_system::Node;
using ce::node_system::NodeId;
using ce::node_system::NodeTypeRegistry;
using ce::node_system::Pin;
using ce::node_system::PinId;
using ce::node_system::PinKind;

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

bool IsValidIdentifier(const std::string& s) {
    if (s.empty()) return false;
    if (!std::isalpha(static_cast<unsigned char>(s[0])) && s[0] != '_') return false;
    for (char c : s) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') return false;
    }
    return true;
}

// Reads a node's "name" config pin without needing a FunctionEmitter
// (used during GenerateSource's own SubgraphEntry-collection pass,
// which runs before any emitter exists). Returns "" if the pin is
// missing or unset -- validated by the caller, not here.
std::string ReadNamePin(const Node& node) {
    for (const Pin& p : node.Inputs()) {
        if (p.name == PinName::Name) {
            if (const auto* s = std::get_if<std::string>(&p.defaultValue)) {
                return *s;
            }
            return {};
        }
    }
    return {};
}

// GS11's source map: scans already-generated text for the `// @node <id>`
// comments FunctionEmitter prefixes every statement/block with, and
// records which node "owns" each line (the most recently seen comment's
// id) -- index 0 is a dummy (ce::lang::SourceLocation::line is 1-based),
// unmapped lines (before the first comment, or a comment-free line like
// a file-scope `var`) get NodeId 0. Deliberately a post-process text
// scan rather than threading a map through FunctionEmitter's recursive
// calls -- simpler, and correct regardless of how the emitter's own
// control flow is structured internally.
std::vector<NodeId> BuildLineToNodeMap(const std::string& source) {
    std::vector<NodeId> map;
    map.push_back(0);
    NodeId current = 0;
    std::istringstream stream(source);
    std::string line;
    constexpr std::string_view kMarker = "// @node ";
    while (std::getline(stream, line)) {
        const std::size_t start = line.find_first_not_of(" \t");
        if (start != std::string::npos && line.compare(start, kMarker.size(), kMarker) == 0) {
            std::size_t numStart = start + kMarker.size();
            std::size_t numEnd = numStart;
            while (numEnd < line.size() && std::isdigit(static_cast<unsigned char>(line[numEnd]))) {
                ++numEnd;
            }
            current = numEnd > numStart ? static_cast<NodeId>(std::stoull(line.substr(numStart, numEnd - numStart))) : 0;
        }
        map.push_back(current);
    }
    return map;
}

// Walks the exec chain reachable from OnStart/OnTick entry nodes and
// emits one CEL statement/block per exec node encountered, plus the
// literal/inline expressions Data wires resolve to. See
// graph_to_source.h's own comment for why this emits text rather than
// building an AST.
//
// GetVariable/SetVariable names are hoisted as file-scope `var`
// declarations (see `usedVariables`, threaded in from GenerateSource
// and shared across the on_start and on_tick emitters) rather than
// function-local ones: a local `var` is reinitialized every call, which
// would silently reset a node graph's "variable" back to its default
// on every single tick -- exactly the kind of state a designer wiring
// up a GetVariable/SetVariable pair expects to persist tick-to-tick.
// File-scope is CEL's only mechanism for that (see
// docs/SCRIPTING_ABI.md's "why file-scope var is module-global, not
// per-entity" note) -- correct for the single-entity-per-script graphs
// this catalog targets today, with the same already-documented
// multi-entity-sharing caveat CEL itself carries, not a new one this
// generator introduces.
class FunctionEmitter {
public:
    // `subgraphNames` is the full set of valid CallSubgraph targets in
    // this graph (every SubgraphEntry's own name, collected up front by
    // GenerateSource) -- shared read-only across every emitter (on_start,
    // on_tick, and each subgraph's own), since any of them may call any
    // subgraph, including another subgraph (GS11 doesn't forbid that).
    FunctionEmitter(const Graph& graph, std::set<std::string>& usedVariables, const std::set<std::string>& subgraphNames,
                     bool trace)
        : graph_(graph), usedVariables_(usedVariables), subgraphNames_(subgraphNames), trace_(trace) {}

    // `entryPinName` is the entry node's own exec-out pin (every entry
    // type in the catalog has exactly one, named "execOut"). Returns
    // just the function's statement body (indented one level) -- the
    // caller is responsible for the file-scope `var` declarations built
    // from `usedVariables` once every entry function has been emitted.
    std::string Emit(const Node& entryNode) { return EmitNext(entryNode, PinName::ExecOut, 1); }

    const std::vector<std::string>& Errors() const { return errors_; }

private:
    const Graph& graph_;
    std::map<std::pair<NodeId, PinId>, std::string> materialized_;
    std::set<std::string>& usedVariables_;
    const std::set<std::string>& subgraphNames_;
    bool trace_;
    std::vector<std::string> errors_;

    void Error(const Node& node, const std::string& message) {
        errors_.push_back("node " + std::to_string(node.Id()) + " ('" + node.TypeName() + "'): " + message);
    }

    // Follows the single outgoing connection from `node`'s exec-out pin
    // named `pinName`, emitting whatever node it leads to (recursively
    // continuing that node's own downstream chain). Returns "" if the
    // pin is unconnected (a legitimate chain end).
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
        // GS11: a real, runtime trace call -- see graph_to_source.h's own
        // comment on why this is a genuine execution trace, not a static
        // annotation. Gated entirely behind `trace_` so a non-trace
        // generate produces byte-identical text to before GS11 (existing
        // fixtures/tests depend on that).
        const std::string trace =
            trace_ ? Indent(indent) + "log_int(\"trace node \", " + std::to_string(node.Id()) + ");\n" : "";

        if (type == NodeType::SetPosition) {
            const std::string stmt = "set_position(" + EmitExpr(node, PinName::Entity) + ", " +
                                      EmitExpr(node, PinName::Value) + ");";
            return comment + trace + Indent(indent) + stmt + "\n" + EmitNext(node, PinName::ExecOut, indent);
        }
        if (type == NodeType::Destroy) {
            const std::string stmt = "destroy(" + EmitExpr(node, PinName::Entity) + ");";
            return comment + trace + Indent(indent) + stmt + "\n" + EmitNext(node, PinName::ExecOut, indent);
        }
        if (type == NodeType::SetVariable) {
            const std::string name = ConfigName(node, PinName::Name);
            const std::string stmt = name + " = " + EmitExpr(node, PinName::Value) + ";";
            return comment + trace + Indent(indent) + stmt + "\n" + EmitNext(node, PinName::ExecOut, indent);
        }
        if (type == NodeType::Log) {
            const std::string message = ConfigString(node, PinName::Message);
            const std::string stmt = "log(\"" + EscapeCelString(message) + "\");";
            return comment + trace + Indent(indent) + stmt + "\n" + EmitNext(node, PinName::ExecOut, indent);
        }
        if (type == NodeType::CallFunction) {
            const std::string name = ConfigName(node, PinName::Function);
            const std::string stmt = name + "();";
            return comment + trace + Indent(indent) + stmt + "\n" + EmitNext(node, PinName::ExecOut, indent);
        }
        if (type == NodeType::CallSubgraph) {
            // Validated against subgraphNames_ (unlike CallFunction, which
            // trusts sema to catch a bad name) -- see node_catalog.h's own
            // comment on why. Uses ConfigString, not ConfigName: a
            // not-found name should report "unknown subgraph", not the
            // generic "not a valid identifier" ConfigName gives an
            // actually-malformed name.
            const std::string name = ConfigString(node, PinName::Name);
            if (subgraphNames_.find(name) == subgraphNames_.end()) {
                Error(node, "references unknown subgraph '" + name + "' (no SubgraphEntry with that name in this graph)");
                return {};
            }
            const std::string stmt = name + "();";
            return comment + trace + Indent(indent) + stmt + "\n" + EmitNext(node, PinName::ExecOut, indent);
        }
        if (type == NodeType::Spawn) {
            const Pin* entityOut = FindOutput(node, PinName::Entity);
            if (entityOut == nullptr) {
                Error(node, "missing expected output pin 'entity'");
                return {};
            }
            const std::string varName = "__node" + std::to_string(node.Id()) + "_entity";
            materialized_[{ node.Id(), entityOut->id }] = varName;
            const std::string stmt =
                "var " + varName + ": entity = spawn_at(" + EmitExpr(node, PinName::Position) + ");";
            return comment + trace + Indent(indent) + stmt + "\n" + EmitNext(node, PinName::ExecOut, indent);
        }
        if (type == NodeType::Sequence) {
            // No comment/trace of its own: a Sequence is a pure codegen
            // organizational construct (concatenation), not something
            // with its own distinct execution moment -- reaching it and
            // reaching its first child happen at the same generated
            // instruction.
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
            std::string out =
                comment + trace + Indent(indent) + "if (" + cond + ") {\n" + trueBody + Indent(indent) + "}";
            if (!falseBody.empty()) {
                out += " else {\n" + falseBody + Indent(indent) + "}";
            }
            out += "\n";
            return out;
        }
        if (type == NodeType::While) {
            const std::string cond = EmitExpr(node, PinName::Condition);
            const std::string loopBody = EmitNext(node, PinName::LoopBody, indent + 1);
            std::string out = comment + trace + Indent(indent) + "while (" + cond + ") {\n" + loopBody +
                               Indent(indent) + "}\n";
            out += EmitNext(node, PinName::Completed, indent);
            return out;
        }

        Error(node, "not an exec-chain (statement/control) node type");
        return {};
    }

    // Resolves the value flowing into `node`'s Data input pin `pinName`
    // -- either the expression wired to it (recursively resolved from
    // the source node) or a literal built from the pin's own default.
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
        return EmitExprFromSource(*source, *sourcePin);
    }

    std::string LiteralFromDefault(const Node& node, const Pin& pin) {
        if (pin.type.dataType == DataType::Entity) {
            Error(node, "entity input pin '" + pin.name + "' must be connected (CEL has no entity literal)");
            return "/* missing entity */";
        }
        return std::visit(
            [&](auto&& v) -> std::string {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    switch (pin.type.dataType) {
                        case DataType::Vec3: return "vec3(0.0, 0.0, 0.0)";
                        case DataType::Bool: return "false";
                        case DataType::Int: return "0";
                        case DataType::String: return "\"\"";
                        default: return "0.0";
                    }
                } else if constexpr (std::is_same_v<T, float>) {
                    return FormatFloatLiteral(v);
                } else if constexpr (std::is_same_v<T, std::int64_t>) {
                    return std::to_string(v);
                } else if constexpr (std::is_same_v<T, bool>) {
                    return v ? "true" : "false";
                } else if constexpr (std::is_same_v<T, std::string>) {
                    return "\"" + EscapeCelString(v) + "\"";
                } else if constexpr (std::is_same_v<T, ce::node_system::Vec3Default>) {
                    return "vec3(" + FormatFloatLiteral(v.x) + ", " + FormatFloatLiteral(v.y) + ", " +
                           FormatFloatLiteral(v.z) + ")";
                }
            },
            pin.defaultValue);
    }

    // Reads a String-config pin's own default value as a raw string
    // (never a wire -- these pins are never given an output-pin
    // counterpart in the catalog, so IsConnectionCompatible can never
    // even offer to connect one).
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

    // Same as ConfigString, but validates the result is a legal CEL
    // identifier -- for pins used as a variable/function name rather
    // than free text (Log's message).
    std::string ConfigName(const Node& node, const std::string& pinName) {
        const std::string name = ConfigString(node, pinName);
        if (!IsValidIdentifier(name)) {
            Error(node, "config pin '" + pinName + "' ('" + name + "') is not a valid CEL identifier");
            return "__invalid";
        }
        if (pinName == PinName::Name) {
            usedVariables_.insert(name);
        }
        return name;
    }

    std::string EmitExprFromSource(const Node& source, const Pin& outPin) {
        const std::string& type = source.TypeName();

        if (type == NodeType::OnStart || type == NodeType::OnTick) {
            if (outPin.name == PinName::Self) return "self";
            if (outPin.name == PinName::Dt) return "dt";
            Error(source, "output pin '" + outPin.name + "' has no expression mapping");
            return "0.0";
        }
        if (type == NodeType::MakeVec3) {
            return "vec3(" + EmitExpr(source, PinName::X) + ", " + EmitExpr(source, PinName::Y) + ", " +
                   EmitExpr(source, PinName::Z) + ")";
        }
        if (type == NodeType::Add) return "(" + EmitExpr(source, PinName::A) + " + " + EmitExpr(source, PinName::B) + ")";
        if (type == NodeType::Sub) return "(" + EmitExpr(source, PinName::A) + " - " + EmitExpr(source, PinName::B) + ")";
        if (type == NodeType::Mul) return "(" + EmitExpr(source, PinName::A) + " * " + EmitExpr(source, PinName::B) + ")";
        if (type == NodeType::Div) return "(" + EmitExpr(source, PinName::A) + " / " + EmitExpr(source, PinName::B) + ")";
        if (type == NodeType::CompareLt) return "(" + EmitExpr(source, PinName::A) + " < " + EmitExpr(source, PinName::B) + ")";
        if (type == NodeType::CompareLe) return "(" + EmitExpr(source, PinName::A) + " <= " + EmitExpr(source, PinName::B) + ")";
        if (type == NodeType::CompareGt) return "(" + EmitExpr(source, PinName::A) + " > " + EmitExpr(source, PinName::B) + ")";
        if (type == NodeType::CompareGe) return "(" + EmitExpr(source, PinName::A) + " >= " + EmitExpr(source, PinName::B) + ")";
        if (type == NodeType::CompareEq) return "(" + EmitExpr(source, PinName::A) + " == " + EmitExpr(source, PinName::B) + ")";
        if (type == NodeType::CompareNeq) return "(" + EmitExpr(source, PinName::A) + " != " + EmitExpr(source, PinName::B) + ")";
        if (type == NodeType::GetPosition) return "get_position(" + EmitExpr(source, PinName::Entity) + ")";
        if (type == NodeType::GetVariable) return ConfigName(source, PinName::Name);
        if (type == NodeType::Spawn) {
            const auto it = materialized_.find({ source.Id(), outPin.id });
            if (it == materialized_.end()) {
                Error(source, "output '" + outPin.name + "' is used before its exec statement runs "
                               "(spawn must occur earlier in the exec chain than whatever consumes its result)");
                return "0";
            }
            return it->second;
        }

        Error(source, "output pin '" + outPin.name + "' has no expression mapping (not a data-producing node type)");
        return "0.0";
    }
};

} // namespace

GraphToSourceResult GenerateSource(const Graph& graph, const NodeTypeRegistry& registry, GenerateOptions options) {
    GraphToSourceResult result;

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

    const Node* onStart = nullptr;
    const Node* onTick = nullptr;
    // GS11: name -> entry node, sorted by name (std::map) for
    // deterministic output order -- iteration order over graph.Nodes()
    // (an unordered_map) isn't.
    std::map<std::string, const Node*> subgraphEntries;
    for (NodeId id : nodeIds) {
        const Node* node = graph.FindNode(id);
        if (node->TypeName() == NodeType::OnStart) {
            if (onStart != nullptr) {
                result.errors.push_back("graph may contain at most one OnStart node");
            }
            onStart = node;
        } else if (node->TypeName() == NodeType::OnTick) {
            if (onTick != nullptr) {
                result.errors.push_back("graph may contain at most one OnTick node");
            }
            onTick = node;
        } else if (node->TypeName() == NodeType::SubgraphEntry) {
            const std::string name = ReadNamePin(*node);
            if (!IsValidIdentifier(name)) {
                result.errors.push_back("node " + std::to_string(id) +
                                         " ('SubgraphEntry'): 'name' ('" + name + "') is not a valid CEL identifier");
                continue;
            }
            if (name == "on_start" || name == "on_tick") {
                result.errors.push_back("node " + std::to_string(id) + " ('SubgraphEntry'): '" + name +
                                         "' collides with a reserved lifecycle function name");
                continue;
            }
            if (!subgraphEntries.emplace(name, node).second) {
                result.errors.push_back("node " + std::to_string(id) +
                                         " ('SubgraphEntry'): duplicate subgraph name '" + name + "'");
            }
        }
    }
    if (onStart == nullptr && onTick == nullptr && subgraphEntries.empty()) {
        result.errors.push_back("graph has no OnStart, OnTick, or SubgraphEntry node -- nothing to generate");
    }
    if (!result.errors.empty()) {
        return result;
    }

    std::set<std::string> subgraphNames;
    for (const auto& [name, node] : subgraphEntries) {
        subgraphNames.insert(name);
    }

    // Shared across every emitter (not per-function) -- see
    // FunctionEmitter's own comment on why GetVariable/SetVariable
    // names become file-scope `var`s, not function-locals.
    std::set<std::string> usedVariables;
    std::string startBody, tickBody;
    std::vector<std::pair<std::string, std::string>> subgraphBodies; // (name, body), in subgraphEntries' sorted order.

    if (onStart != nullptr) {
        FunctionEmitter emitter(graph, usedVariables, subgraphNames, options.trace);
        startBody = emitter.Emit(*onStart);
        result.errors.insert(result.errors.end(), emitter.Errors().begin(), emitter.Errors().end());
    }
    if (onTick != nullptr) {
        FunctionEmitter emitter(graph, usedVariables, subgraphNames, options.trace);
        tickBody = emitter.Emit(*onTick);
        result.errors.insert(result.errors.end(), emitter.Errors().begin(), emitter.Errors().end());
    }
    for (const auto& [name, entryNode] : subgraphEntries) {
        FunctionEmitter emitter(graph, usedVariables, subgraphNames, options.trace);
        subgraphBodies.emplace_back(name, emitter.Emit(*entryNode));
        result.errors.insert(result.errors.end(), emitter.Errors().begin(), emitter.Errors().end());
    }
    if (!result.errors.empty()) {
        return result;
    }

    std::ostringstream out;
    out << "// Generated by celc --graph-to-source from graph '" << graph.Name() << "'.\n"
        << "// Do not hand-edit -- edit the .celg source graph instead.\n\n";

    for (const std::string& name : usedVariables) {
        out << "var " << name << ": float = 0.0;\n";
    }
    if (!usedVariables.empty()) {
        out << "\n";
    }

    if (onStart != nullptr) {
        out << "func on_start(self: entity) {\n" << startBody << "}\n\n";
    }
    if (onTick != nullptr) {
        out << "func on_tick(self: entity, dt: float) {\n" << tickBody << "}\n\n";
    }
    for (const auto& [name, body] : subgraphBodies) {
        out << "func " << name << "() {\n" << body << "}\n\n";
    }

    result.ok = true;
    result.source = out.str();
    result.lineToNode = BuildLineToNodeMap(result.source);
    return result;
}

CheckSourceResult CheckGeneratedSource(const GraphToSourceResult& genResult) {
    CheckSourceResult result;

    ce::lang::AstArena arena;
    std::istringstream stream(genResult.source);
    ce::lang::DiagnosticEngine diagnostics;
    ce::lang::Program* program = ce::lang::ParseProgram(stream, arena, diagnostics);
    if (program != nullptr && !diagnostics.HasErrors()) {
        ce::lang::AnalyzeProgram(*program, diagnostics);
    }

    for (const ce::lang::Diagnostic& diag : diagnostics.Diagnostics()) {
        NodeDiagnostic nd;
        nd.code = diag.code;
        nd.loc = diag.loc;
        nd.message = diag.message;
        if (diag.loc.line >= 1 && static_cast<std::size_t>(diag.loc.line) < genResult.lineToNode.size()) {
            nd.nodeId = genResult.lineToNode[static_cast<std::size_t>(diag.loc.line)];
        }
        result.diagnostics.push_back(std::move(nd));
    }
    result.ok = !diagnostics.HasErrors();
    return result;
}

} // namespace ce::lang::nodegen
