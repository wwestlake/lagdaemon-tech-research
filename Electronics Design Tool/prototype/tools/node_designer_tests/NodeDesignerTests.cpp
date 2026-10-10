// Node Designer (ported FRust node-programming editor) without a window:
// node creation and deletion, wiring rules, save/open round trips,
// parameters, unknown node types and malformed documents.

#include <JuceHeader.h>

#include "../../Source/NodeDesignerPanel.h"

#include <cstdio>

namespace
{
int failures = 0;
int checks = 0;

void check(bool ok, const juce::String& name, const juce::String& detail = {})
{
    ++checks;
    if (!ok) ++failures;
    std::printf("%s  %s%s\n", ok ? "PASS" : "FAIL", name.toRawUTF8(), ok || detail.isEmpty() ? "" : ("  -- " + detail).toRawUTF8());
}

juce::File tempFile(const juce::String& name)
{
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("node_designer_tests");
    dir.createDirectory();
    return dir.getChildFile(name);
}

juce::File writeTemp(const juce::String& name, const juce::String& text)
{
    auto f = tempFile(name);
    f.replaceWithText(text);
    return f;
}

const juce::var* findById(const juce::var& list, const juce::String& id)
{
    if (auto* a = list.getArray())
        for (auto& v : *a)
            if (v.getProperty("id", {}).toString() == id)
                return &v;
    return nullptr;
}

juce::StringArray wiring(const juce::var& graph)
{
    juce::StringArray s;
    if (auto* a = graph.getProperty("connections", {}).getArray())
        for (auto& c : *a)
            s.add(c.getProperty("from", {}).toString() + "." + c.getProperty("fromPin", {}).toString() + "->"
                  + c.getProperty("to", {}).toString() + "." + c.getProperty("toPin", {}).toString());
    s.sort(false);
    return s;
}

juce::String param(const juce::var& graph, const juce::String& node, const juce::String& name)
{
    const auto* n = findById(graph.getProperty("nodes", {}), node);
    return n != nullptr ? n->getProperty("parameters", {}).getProperty(name, {}).toString() : juce::String("<no node>");
}

// Saves, opens the file in a second editor, and returns that editor's graph.
juce::var roundTrip(NodeDesignerPanel& panel, const juce::String& name, juce::String& openProblems)
{
    juce::String error;
    const auto file = tempFile(name);
    if (!panel.saveToFile(file, error))
        return {};
    NodeDesignerPanel reopened;
    reopened.openFile(file, openProblems);
    return reopened.describeGraph();
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const juce::File fixtures(NODE_DESIGNER_FIXTURES);
    juce::String error;

    std::printf("-- starter graph --\n");
    {
        NodeDesignerPanel panel;
        const auto g = panel.describeGraph();
        check(g.getProperty("nodes", {}).size() == 4 && g.getProperty("connections", {}).size() == 3,
              "the Hello Nodes starter graph has its 4 nodes and 3 wires", wiring(g).joinIntoString(", "));
        check(panel.validateGraph().isEmpty(), "the starter graph validates", panel.validateGraph().joinIntoString(" "));
    }

    std::printf("-- node creation and deletion --\n");
    {
        NodeDesignerPanel panel;
        panel.newGraph("node_graph", false);
        check(panel.describeGraph().getProperty("nodes", {}).size() == 0, "a new empty graph has no nodes");
        const auto a = panel.addNodeOfType("literal_i64", 0, 0, "ten", error);
        const auto b = panel.addNodeOfType("add", 200, 0, {}, error);
        check(a == "ten" && b.isNotEmpty(), "nodes are added with a requested or generated id", a + " " + b);
        check(panel.addNodeOfType("warp_drive", 0, 0, {}, error).isEmpty() && error.contains("Unknown node type"), "an unknown type is refused", error);
        check(panel.addNodeOfType("add", 0, 0, "ten", error).isEmpty() && error.contains("already exists"), "a duplicate id is refused", error);
        panel.connect("ten", "value", b, "a", error);
        check(panel.deleteNode("ten", error) && panel.describeGraph().getProperty("nodes", {}).size() == 1
                  && panel.describeGraph().getProperty("connections", {}).size() == 0,
              "deleting a node removes it and its wires");
        check(!panel.deleteNode("ten", error), "deleting a missing node is refused");
    }

    std::printf("-- wiring rules --\n");
    {
        NodeDesignerPanel panel;
        panel.newGraph("node_graph", false);
        panel.addNodeOfType("event_start", 0, 0, "start", error);
        panel.addNodeOfType("literal_i64", 0, 100, "ten", error);
        panel.addNodeOfType("add", 200, 100, "sum", error);
        panel.addNodeOfType("print", 200, 0, "say", error);
        const auto id = panel.connect("ten", "value", "sum", "a", error);
        check(id == "ten.value->sum.a", "a valid wire is made with a stable id", id + " " + error);
        check(panel.connect("start", "start", "sum", "b", error).isEmpty() && error.contains("Incompatible"), "exec into data is refused", error);
        check(panel.connect("sum", "a", "say", "value", error).isEmpty() && error.contains("is an input"), "a wire out of an input is refused", error);
        check(panel.connect("ten", "value", "sum", "sum", error).isEmpty() && error.contains("is an output"), "a wire into an output is refused", error);
        check(panel.connect("ten", "nope", "sum", "b", error).isEmpty() && error.contains("no output"), "a missing pin is refused", error);
        check(panel.connect("ten", "value", "sum", "a", error).isEmpty() && error.contains("already wired"), "an input takes one wire", error);
        check(panel.connect("ghost", "value", "sum", "b", error).isEmpty() && error.contains("No node"), "a missing node is refused");
        check(panel.disconnect(id, error) && panel.connect("ten", "value", "sum", "a", error) == id, "disconnect frees the input");
    }

    std::printf("-- save and open --\n");
    {
        NodeDesignerPanel panel;
        panel.newGraph("node_graph", false);
        panel.addNodeOfType("event_start", 0, 0, "start", error);
        panel.addNodeOfType("const_bool", 0, 120, "flag", error);
        panel.addNodeOfType("branch", 220, 0, "choose", error);
        panel.addNodeOfType("literal_string", 220, 200, "msg", error);
        panel.addNodeOfType("print", 440, 0, "say", error);
        panel.addNodeOfType("for_loop", 440, 200, "loop", error);
        panel.addNodeOfType("literal_i64", 220, 320, "ten", error);
        panel.addNodeOfType("mul", 660, 200, "twice", error);
        panel.connect("start", "start", "choose", "in", error);
        panel.connect("flag", "value", "choose", "condition", error);
        panel.connect("choose", "false", "say", "in", error);        // second exec output
        panel.connect("msg", "value", "say", "value", error);
        panel.connect("choose", "true", "loop", "in", error);
        panel.connect("loop", "index", "twice", "a", error);         // third output
        panel.connect("ten", "value", "twice", "b", error);
        panel.setNodeParameter("flag", "value", true, error);
        panel.setNodeParameter("ten", "value", 42, error);
        panel.setNodeParameter("msg", "text", "hello", error);
        panel.setNodeParameter("loop", "input.last", "9", error);
        panel.setNodeParameter("say", "breakpoint", true, error);
        panel.moveNode("twice", 777, 333, error);
        const auto before = panel.describeGraph();

        juce::String problems;
        const auto after = roundTrip(panel, "save_open.frnode.json", problems);
        check(problems.isEmpty(), "the saved program opens without problems", problems);
        check(wiring(after) == wiring(before), "every wire keeps both of its pins",
              wiring(before).joinIntoString(", ") + "\n   got " + wiring(after).joinIntoString(", "));
        bool same = after.getProperty("nodes", {}).size() == before.getProperty("nodes", {}).size();
        if (auto* list = before.getProperty("nodes", {}).getArray())
            for (auto& n : *list)
            {
                const auto* m = findById(after.getProperty("nodes", {}), n.getProperty("id", {}).toString());
                same = same && m != nullptr && m->getProperty("type", {}) == n.getProperty("type", {})
                    && (double)m->getProperty("x", 0) == (double)n.getProperty("x", 0) && (double)m->getProperty("y", 0) == (double)n.getProperty("y", 0)
                    && juce::JSON::toString(m->getProperty("parameters", {})) == juce::JSON::toString(n.getProperty("parameters", {}));
            }
        check(same, "node ids, types, positions and parameters survive");
        check(param(after, "flag", "value") == "1" || param(after, "flag", "value") == "true", "a Boolean value survives", param(after, "flag", "value"));
        check(param(after, "ten", "value") == "42" && param(after, "msg", "text") == "hello" && param(after, "loop", "input.last") == "9"
                  && param(after, "say", "breakpoint") == "1",
              "integer, text, input default and breakpoint survive");
        bool ids = true;
        if (auto* list = before.getProperty("connections", {}).getArray())
            for (auto& c : *list)
                ids = ids && findById(after.getProperty("connections", {}), c.getProperty("id", {}).toString()) != nullptr;
        check(ids, "connection ids survive");
        juce::String again;
        NodeDesignerPanel reopened;
        reopened.openFile(tempFile("save_open.frnode.json"), again);
        check(reopened.buildSavedDocument() == panel.buildSavedDocument(), "save -> open -> save writes the same document");
    }

    std::printf("-- state machine --\n");
    {
        NodeDesignerPanel panel;
        panel.newGraph("state_machine", false);
        for (auto id : { "idle", "busy", "fault" })
        {
            panel.addNodeOfType("sm_state", 0, 0, id, error);
            panel.setNodeParameter(id, "text", juce::String(id).toUpperCase(), error);
        }
        panel.setNodeParameter("idle", "initial", true, error);
        const auto t1 = panel.connect("idle", "leave", "busy", "enter", error);
        const auto t2 = panel.connect("busy", "leave", "idle", "enter", error);
        const auto t3 = panel.connect("fault", "leave", "idle", "enter", error);
        check(t1.isNotEmpty() && t2.isNotEmpty() && t3.isNotEmpty(), "wiring two states inserts a Transition node", t1 + " " + t2 + " " + t3);
        panel.setNodeParameter(t2, "guard", "done", error);
        juce::String problems;
        const auto after = roundTrip(panel, "states.frnode.json", problems);
        int intoIdle = 0;
        if (auto* list = after.getProperty("connections", {}).getArray())
            for (auto& c : *list)
                intoIdle += c.getProperty("to", {}).toString() == "idle" ? 1 : 0;
        check(intoIdle == 2, "both transitions into 'idle' survive (the reference kept one)", wiring(after).joinIntoString(", "));
        check(param(after, t2, "guard") == "done" && param(after, t2, "event").isNotEmpty(), "transition event and guard survive");
        check(after.getProperty("diagramType", {}).toString() == "state_machine" && problems.isEmpty(), "it opens as a valid state machine", problems);
    }

    std::printf("-- existing files and unknown content --\n");
    {
        juce::String problems;
        NodeDesignerPanel panel;
        check(panel.openFile(fixtures.getChildFile("ide_hello_nodes_v2.json"), problems) && problems.isEmpty(),
              "a schematic saved by the FrustIDE node designer opens", problems);
        juce::StringArray expected { "event_start.start->print.in", "print.then->end.in", "text.value->print.value" };
        check(wiring(panel.describeGraph()) == expected, "its wires are restored", wiring(panel.describeGraph()).joinIntoString(", "));
        const auto saved = juce::JSON::parse(panel.buildSavedDocument());
        const auto original = juce::JSON::parse(fixtures.getChildFile("ide_hello_nodes_v2.json").loadFileAsString());
        check(juce::JSON::toString(saved.getProperty("types", {})) == juce::JSON::toString(original.getProperty("types", {})),
              "its types are saved back unchanged");

        const auto doc = R"({"schemaVersion": 2, "name": "Unknown", "namespace": "djehuti.test",
            "nodes": [{"id": "src", "type": "literal_i64", "value": 3, "x": 0, "y": 0, "context": "audio"},
                      {"id": "w", "type": "warp_drive", "x": 200, "y": 0, "power": 11, "inputs": [{"ref": "src", "pin": "value"}]}]})";
        NodeDesignerPanel unknown;
        check(unknown.openFile(writeTemp("unknown.frnode.json", doc), problems) && problems.contains("warp_drive"),
              "an unknown node type is reported, not converted", problems);
        const auto out = juce::JSON::parse(unknown.buildSavedDocument());
        const auto* w = findById(out.getProperty("nodes", {}), "w");
        check(w != nullptr && w->getProperty("type", {}).toString() == "warp_drive" && (int)w->getProperty("power", 0) == 11,
              "it is saved back with its type and data");
        check(wiring(unknown.describeGraph()).contains("src.value->w.in 1"), "its wire is kept", wiring(unknown.describeGraph()).joinIntoString(", "));
        check(out.getProperty("namespace", {}).toString() == "djehuti.test" && findById(out.getProperty("nodes", {}), "src")->getProperty("context", {}).toString() == "audio",
              "sections and node keys the editor does not show are saved back");
    }

    std::printf("-- malformed documents --\n");
    {
        NodeDesignerPanel panel;
        const auto before = panel.buildSavedDocument();
        juce::String problems;
        check(!panel.openFile(writeTemp("bad1.json", "{ not json"), problems) && problems.contains("not valid JSON"), "invalid JSON is refused", problems);
        check(!panel.openFile(writeTemp("bad2.json", R"({"schemaVersion": 9, "nodes": []})"), problems) && problems.contains("newer"),
              "a newer schema version is refused", problems);
        check(!panel.openFile(writeTemp("bad3.json", R"({"nodes": 5})"), problems) && problems.contains("not a list"), "a bad nodes section is refused", problems);
        check(panel.buildSavedDocument() == before, "a refused document leaves the open graph untouched");

        const auto doc = R"({"schemaVersion": 3, "nodes": [
                {"id": "a", "type": "literal_i64", "value": 1}, {"type": "add"}, {"id": "b", "type": "add"}, {"id": "a", "type": "literal_i64", "value": 2}],
            "connections": [{"id": "c1", "from": {"node": "a", "pin": "value"}, "to": {"node": "b", "pin": "a"}},
                            {"id": "c2", "from": {"node": "ghost", "pin": "value"}, "to": {"node": "b", "pin": "b"}},
                            {"id": "c3", "from": {"node": "a", "pin": "value"}, "to": {"node": "b", "pin": "sum"}},
                            {"id": "c4", "from": {"node": "a"}}]})";
        check(panel.openFile(writeTemp("partial.json", doc), problems), "a document with bad parts still opens");
        check(problems.contains("has no id") && problems.contains("more than one node") && problems.contains("ghost")
                  && problems.contains("(that is an output)") && problems.contains("does not name both"),
              "each bad part is reported", problems);
    }

    std::printf("%d checks, %s\n", checks, failures == 0 ? "ALL PASSED" : (juce::String(failures) + " FAILED").toRawUTF8());
    return failures == 0 ? 0 : 1;
}
