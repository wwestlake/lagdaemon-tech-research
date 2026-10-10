#pragma once

// The FRust graphical node-programming editor, ported from FrustIDE's
// NodeDesignerPanel (commit 431543f). The node set, editor, state machines,
// variables, types and schematic format are the reference implementation's;
// this copy is adapted to the Workbench: Compile, Export and Compile & Run
// use the Workbench's copy of the node compiler (NodeCompiler.cpp) and its
// embedded FRust compiler; programs are saved in the open project; every
// wire keeps its output pin and nodes it cannot show are kept; and the
// Workbench agent drives it through the public operations below.

#include <JuceHeader.h>

#include "NodeCompiler.h"

#include <functional>
#include <memory>
#include <vector>

class NodeDesignerPanel : public juce::Component,
                          public juce::DragAndDropContainer
{
public:
    NodeDesignerPanel();
    explicit NodeDesignerPanel(const juce::File& fileToOpen);
    ~NodeDesignerPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void loadGraphFile(const juce::File& file);

    enum class PinFlow { Data, Exec, Stream, Resource };

    struct Pin
    {
        juce::String name;
        juce::String type;
        PinFlow flow = PinFlow::Data;
        bool isInput = true;
    };

    // Where Save and Open start: the open project's programs folder.
    std::function<juce::File()> defaultFolder;

    // Operations for the Workbench agent and tests. Nodes and pins are named
    // by id and pin name; each returns false with `error` set when refused.
    void newGraph(const juce::String& diagramType, bool withStarterNodes);
    bool openFile(const juce::File& file, juce::String& error);
    bool saveToFile(const juce::File& file, juce::String& error);
    juce::File currentFile() const { return currentGraphFile; }
    juce::String addNodeOfType(const juce::String& type, float x, float y, const juce::String& requestedId, juce::String& error);
    bool deleteNode(const juce::String& nodeId, juce::String& error);
    bool moveNode(const juce::String& nodeId, float x, float y, juce::String& error);
    juce::String connect(const juce::String& fromNode, const juce::String& fromPin,
                         const juce::String& toNode, const juce::String& toPin, juce::String& error);
    bool disconnect(const juce::String& connectionId, juce::String& error);
    bool setNodeParameter(const juce::String& nodeId, const juce::String& name, const juce::var& value, juce::String& error);
    // The graph as JSON: name, diagram type, nodes with pins and parameters,
    // connections with ids and pin names.
    juce::var describeGraph() const;
    // The node types this editor offers, with their pins.
    juce::var describeNodeTypes() const;
    // Structural problems: duplicate ids, missing or wrong-direction pins,
    // incompatible wires, inputs driven twice, unknown node types, and the
    // state-machine rules. Empty when the graph is sound. Never changes it.
    juce::StringArray validateGraph() const;
    // The document Save writes.
    juce::String buildSavedDocument() const;

    // Compile: the open graph through the node compiler. On success the
    // generated FRust program is kept for Export and Run.
    bool compileProgram(juce::String& message);
    juce::String generatedProgramSource() const { return generatedSource; }
    // Export: writes the generated package (source files and frate.json)
    // next to the program, under .frust/generated/nodes/<name>/.
    bool exportProgram(juce::File& packageRoot, juce::String& message);
    // Run: the compiled program as a Workbench Frust script (its output is
    // what `pub fn run() -> String` returns plus what the program prints).
    bool buildRunScript(juce::String& script, juce::String& error) const;
    // Compile & Run hands the script here (the Workbench runs it in the
    // Frust panel).
    std::function<void(const juce::String& script, const juce::String& label)> onRunRequested;
    juce::String statusText() const { return statusView.getText(); }

private:
    struct GraphNode
    {
        int uid = 0;
        juce::String id;
        juce::String type;
        juce::String title;
        juce::String category;
        juce::Colour colour;
        float x = 0.0f;
        float y = 0.0f;
        int literalValue = 0;
        juce::String textValue;
        juce::String accessibility = "package";
        juce::String entryAction;
        juce::String updateAction;
        juce::String exitAction;
        juce::String eventName;
        juce::String payloadType;
        juce::String guardExpression;
        juce::String transitionAction;
        juce::String machineRef;
        bool initial = false;
        bool terminal = false;
        bool breakpoint = false;
        bool watched = false;
        std::vector<Pin> inputs;
        std::vector<Pin> outputs;
        std::vector<juce::String> inputDefaults;
        juce::String functionRef;
        // A node whose type this editor has no template for: shown as a
        // placeholder and saved back exactly as it was loaded.
        juce::var unsupportedSource;
    };

    struct Connection
    {
        int fromNode = 0;
        int fromPin = 0;
        int toNode = 0;
        int toPin = 0;
        juce::String id;
    };

    struct NodeTemplate
    {
        juce::String type;
        juce::String title;
        juce::String category;
        juce::Colour colour;
        int defaultLiteral = 0;
        std::vector<Pin> inputs;
        std::vector<Pin> outputs;
        juce::String functionRef;
    };

    struct VariableDef
    {
        juce::String name;
        juce::String type;
        juce::String access;
        juce::String defaultValue;
    };

    struct TypeField
    {
        juce::String name;
        juce::String type;
    };

    struct TypeVariant
    {
        juce::String name;
        std::vector<TypeField> fields;
        int intValue = 0;
    };

    struct TypeDef
    {
        juce::String name;
        juce::String kind;
        juce::String targetFallback;
        std::vector<TypeField> fields;
        std::vector<TypeVariant> variants;
    };

    struct FunctionPort
    {
        juce::String name;
        juce::String type;
    };

    struct FunctionDef
    {
        juce::String id;
        juce::String name;
        juce::String namespaceName;
        juce::String accessibility;
        juce::String kind;
        juce::String owningPod;
        juce::String owningVersion;
        std::vector<FunctionPort> inputs;
        std::vector<FunctionPort> outputs;
    };

    struct ResourceDef
    {
        juce::String id;
        juce::String name;
        juce::String kind;
        juce::String accessibility;
    };

    struct StateDef
    {
        juce::String id;
        juce::String name;
        juce::String accessibility;
        juce::String entryAction;
        juce::String updateAction;
        juce::String exitAction;
        float x = 0.0f;
        float y = 0.0f;
        bool initial = false;
        bool terminal = false;
    };

    struct EventDef
    {
        juce::String id;
        juce::String name;
        juce::String payloadType;
    };

    struct TransitionDef
    {
        juce::String id;
        juce::String from;
        juce::String to;
        juce::String event;
        juce::String guard;
        juce::String action;
    };

    class PalettePanel;
    class GraphCanvas;
    class InspectorPanel;
    class VariablesPanel;
    class TypesPanel;

    static std::vector<NodeTemplate> buildTemplates();
    static juce::String quoted(const juce::String& text);

    GraphNode* findNode(int uid);
    const GraphNode* findNode(int uid) const;
    const GraphNode* findNodeById(const juce::String& id) const;
    const NodeTemplate* findTemplate(const juce::String& type) const;
    const FunctionDef* findFunction(const juce::String& id) const;
    void refreshTemplatesWithFunctions();
    GraphNode& addNode(const juce::String& type, juce::Point<float> world);
    GraphNode& addRerouteNode(const Connection& connection, juce::Point<float> world);
    void removeNode(int uid);
    void connectPins(int fromNode, int fromPin, int toNode, int toPin);
    juce::String connectionIdFor(int fromNode, int fromPin, int toNode, int toPin) const;
    const Connection* connectionToInput(int nodeUid, int inputPin) const;
    bool pinsCompatible(const Pin& from, const Pin& to) const;
    const GraphNode* resolveRerouteUpstream(int nodeUid, int inputPin) const;
    const GraphNode* resolveRerouteDownstream(int nodeUid, int outputPin) const;
    juce::String validateStateMachine() const;
    int nextUid();
    juce::String uniqueNodeId(const juce::String& base) const;

    void initializeUntitled();
    bool loadFromJson(const juce::String& text, juce::String& problems);
    juce::String buildSchematicJson(bool includeRoutingNodes = true) const;
    juce::String buildCompilerJson(bool includeRoutingNodes) const;
    juce::String selectedDiagramType() const;
    juce::String selectedTarget() const;
    juce::String selectedFrustProjectType() const;
    juce::String outputNodeId() const;
    void initializeNodeGraph();
    void initializeStateMachine();
    void clearGraph();
    void refreshProperties();
    void saveGraph();
    void openGraph();
    void compileGraph();
    void saveGeneratedSource();
    void compileAndRun();
    juce::String generatedSourcePathLabel() const;
    juce::File generatedSourceCacheFile() const;
    void showValidation();
    void setStatus(const juce::String& text, bool isError = false);

    std::vector<NodeTemplate> templates;
    std::vector<GraphNode> nodes;
    std::vector<Connection> connections;
    std::vector<VariableDef> variables;
    std::vector<TypeDef> types;
    std::vector<FunctionDef> functions;
    std::vector<ResourceDef> resources;
    std::vector<StateDef> states;
    std::vector<EventDef> events;
    std::vector<TransitionDef> transitions;
    std::vector<node_compiler::GeneratedFile> generatedFiles;
    std::vector<node_compiler::SourceMapEntry> currentSourceMap;
    juce::String generatedSource;
    node_compiler::ArtifactKind generatedKind = node_compiler::ArtifactKind::FunctionSource;
    int nextNodeUid = 1;
    int selectedNodeUid = 0;
    int selectedConnectionIndex = -1;
    juce::String diagramName = "Untitled Node Schematic";
    juce::String diagramType = "node_graph";
    juce::String frustProjectType = "bin";
    // The document as last opened: sections and node keys this editor does
    // not manage are saved back from it.
    juce::var loadedDocument;

    std::unique_ptr<PalettePanel> palette;
    std::unique_ptr<GraphCanvas> canvas;
    std::unique_ptr<InspectorPanel> inspector;
    std::unique_ptr<VariablesPanel> variablesPanel;
    std::unique_ptr<TypesPanel> typesPanel;

    juce::TextButton newButton { "New" };
    juce::TextButton openButton { "Open" };
    juce::TextButton saveButton { "Save" };
    juce::TextButton validateButton { "Validate" };
    juce::TextButton compileButton { "Compile" };
    juce::TextButton saveSourceButton { "Export .fr" };
    juce::Label diagramTypeLabel { "DiagramTypeLabel", "Diagram" };
    juce::ComboBox diagramTypeSelector;
    juce::Label targetLabel { "TargetLabel", "Target" };
    juce::ComboBox targetSelector;
    juce::Label frustProjectTypeLabel { "FrustProjectTypeLabel", "Frate" };
    juce::ComboBox frustProjectTypeSelector;
    juce::Label titleLabel { "Title", "Untitled Node Schematic" };
    juce::Label propertiesTitle { "PropertiesTitle", "Properties" };
    juce::TextEditor statusView;

    juce::File currentGraphFile;
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NodeDesignerPanel)
};
