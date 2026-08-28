#pragma once

#include <JuceHeader.h>
#include <creation/node_editor_ui/NodeGraphComponent.h>
#include <creation/node_editor_ui/NodeInspector.h>
#include <creation/node_editor_ui/NodePalette.h>
#include <lang/nodegen/foley_node_catalog.h>
#include <node_system/graph.h>
#include <node_system/celg_serialization.h>

// The Foley workspace tab: an execution-style (UE4-Blueprint-like) node graph editor for
// sequencing/choice logic, expressed in CEL. Reuses the generic, domain-agnostic node-editor UI
// (shared/NodeEditorUI, itself ported from CreationEngine's own node editor) with Foley's own
// node catalog (shared/CEL's foley_node_catalog.h) - per the suite's explicit stance, share the
// editing machinery across domains, never merge the domain-specific node catalogs.
//
// PUBLICLY inherits juce::DragAndDropContainer (must be public, not private - JUCE's
// DragAndDropContainer::findParentDragContainerFor walks the component tree using
// dynamic_cast<DragAndDropContainer*>, which only succeeds through a public base) so dragging a
// node type from the palette actually lands on the graph canvas - see NodePalette's own header
// comment, and CreationEngine's LogicPanel, which this mirrors exactly for the same reason.
class FoleyPanel final : public juce::Component, public juce::DragAndDropContainer
{
public:
    FoleyPanel();

    void resized() override;
    void paint(juce::Graphics& g) override;

    // Save/load this graph as one named project asset, via the shared
    // ProjectAssetService::saveGeneratedAsset mechanism -- see the Setup toolbar button.
    // Uses node_system's own .celg text format (celg_serialization.h), the same byte-stable
    // round-trip format graphs already use elsewhere in the suite.
    juce::String serializeGraph() const;
    bool loadGraph(const juce::String& celgText, juce::String& errorMessage);

    std::function<void(const juce::String& name)> onSetupSaveRequested;
    std::function<void()> onSetupLoadRequested;

private:
    void generateSource();
    void showSetupMenu();

    ce::node_system::NodeTypeRegistry registry_ = ce::lang::nodegen::foley::BuildFoleyNodeCatalog();
    ce::node_system::Graph graph_ { "Foley" };

    juce::Label titleLabel_ { {}, "Foley" };
    juce::Label hintLabel_ { {}, "Drag a node from the palette onto the canvas. Right-drag to pan, wheel to zoom." };
    juce::TextButton generateButton_ { "Generate CEL" };
    juce::TextButton setupMenuButton_ { "Setup" };
    juce::Label statusLabel_;
    juce::TextEditor sourceView_;

    creation::node_editor_ui::NodePalette palette_ { registry_ };
    creation::node_editor_ui::NodeGraphComponent graphComponent_ { graph_, registry_ };
    creation::node_editor_ui::NodeInspector inspector_ { graph_ };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FoleyPanel)
};
