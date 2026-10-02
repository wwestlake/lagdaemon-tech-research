#include "ElectronicsWorkbench.h"

#include <map>
#include <set>

namespace
{
constexpr int toolbarHeight = 36;
constexpr int menuHeight = 24;

void styleTextEditor(juce::TextEditor& editor, bool mono = false)
{
    editor.setMultiLine(true);
    editor.setReturnKeyStartsNewLine(true);
    editor.setScrollbarsShown(true);
    editor.setFont(juce::Font(mono ? "Consolas" : "Segoe UI", mono ? 14.0f : 15.0f, juce::Font::plain));
    editor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff10161d));
    editor.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff33424d));
    editor.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour(0xff5aa7c8));
    editor.setColour(juce::TextEditor::textColourId, juce::Colour(0xffdce9ee));
}

void showCursorForEvent(const juce::MouseEvent& event, juce::MouseCursor cursor)
{
    auto source = event.source;
    if (source.hasMouseCursor())
        source.showMouseCursor(cursor);
}

class NotesPanel : public juce::Component
{
public:
    NotesPanel(const juce::String& heading, const juce::String& body)
    {
        title.setText(heading, juce::dontSendNotification);
        title.setFont(juce::Font(16.0f, juce::Font::bold));
        title.setColour(juce::Label::textColourId, juce::Colour(0xff78dcca));
        addAndMakeVisible(title);

        styleTextEditor(text);
        text.setText(body);
        addAndMakeVisible(text);
    }

    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff151a20)); }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8);
        title.setBounds(area.removeFromTop(24));
        area.removeFromTop(6);
        text.setBounds(area);
    }

private:
    juce::Label title;
    juce::TextEditor text;
};

class ComponentLibraryPanel final : public juce::Component
{
public:
    struct SymbolInfo
    {
        juce::String id;
        juce::String name;
        juce::String category;
    };

    explicit ComponentLibraryPanel(std::function<void(juce::String)> onSelection)
        : onSymbolSelected(std::move(onSelection))
    {
        filter.setTextToShowWhenEmpty("Search components, MPNs, aliases...", juce::Colour(0xff71808c));
        styleTextEditor(filter);
        filter.setMultiLine(false);
        filter.onTextChange = [this] { refreshFilter(); };
        addAndMakeVisible(filter);

        if (!loadSeedLibrary())
            addFallbackLibrary();
        refreshFilter();

        if (onSymbolSelected != nullptr)
            onSymbolSelected("resistor");
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff151a20));
        g.setColour(juce::Colour(0xffdce9ee));
        g.setFont(juce::Font(14.0f, juce::Font::bold));
        g.drawText("Component Library", getLocalBounds().removeFromTop(24).reduced(8, 0), juce::Justification::centredLeft);

        rowBounds.clear();
        auto listArea = getLocalBounds().reduced(8);
        listArea.removeFromTop(24);
        listArea.removeFromTop(28);
        listArea.removeFromTop(8);
        g.setColour(juce::Colour(0xff10161d));
        g.fillRect(listArea);

        int y = listArea.getY() + 4;
        for (int row = 0; row < (int)listModel.items.size(); ++row)
        {
            auto rowArea = juce::Rectangle<int>(listArea.getX() + 4, y, listArea.getWidth() - 8, 42);
            rowBounds.push_back(rowArea);
            paintSymbolRow(g, row, rowArea);
            y += 44;
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8);
        area.removeFromTop(24);
        filter.setBounds(area.removeFromTop(28));
    }

    void mouseMove(const juce::MouseEvent& event) override
    {
        const auto row = rowAt(event.getPosition());
        if (row != hoverRow)
        {
            hoverRow = row;
            repaint();
        }
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        hoverRow = -1;
        repaint();
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        const auto row = rowAt(event.getPosition());
        if (row < 0 || row >= (int)listModel.items.size())
            return;

        selectedRow = row;
        const auto& symbol = listModel.items[(size_t)row];
        if (onSymbolSelected != nullptr)
            onSymbolSelected(symbol.id);

        repaint();
        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this))
        {
            showCursorForEvent(event, juce::MouseCursor::DraggingHandCursor);
            container->startDragging("symbol:" + symbol.id, this);
        }
    }

    void mouseDoubleClick(const juce::MouseEvent& event) override
    {
        const auto row = rowAt(event.getPosition());
        if (row < 0 || row >= (int)listModel.items.size())
            return;

        selectedRow = row;
        const auto& symbol = listModel.items[(size_t)row];
        if (onSymbolSelected != nullptr)
            onSymbolSelected(symbol.id);
        repaint();
    }

private:
    class ComponentList final : public juce::ListBoxModel
    {
    public:
        std::vector<SymbolInfo> items;
        std::function<void(juce::String)> onSelected;

        int getNumRows() override { return (int)items.size(); }

        void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override
        {
            if (selected) g.fillAll(juce::Colour(0xff23394a));
            if (row < 0 || row >= (int)items.size()) return;
            const auto& item = items[(size_t)row];
            g.setColour(item.id == "opamp_741" ? juce::Colour(0xffffc857) : juce::Colour(0xffdce9ee));
            g.setFont(juce::Font(14.0f, juce::Font::bold));
            g.drawText(item.name, 8, 2, width - 16, height / 2, juce::Justification::centredLeft);
            g.setColour(juce::Colour(0xff93a7b0));
            g.setFont(juce::Font(12.0f));
            g.drawText(item.category, 8, height / 2 - 1, width - 16, height / 2, juce::Justification::centredLeft);
        }

        int getRowHeight() const { return 42; }

        void selectedRowsChanged(int lastRowSelected) override
        {
            if (lastRowSelected >= 0 && lastRowSelected < (int)items.size() && onSelected != nullptr)
                onSelected(items[(size_t)lastRowSelected].id);
        }
    };

    class SymbolListBox final : public juce::ListBox
    {
    public:
        explicit SymbolListBox(ComponentList& model)
            : juce::ListBox("components", &model), listModel(model)
        {
        }

        void mouseDown(const juce::MouseEvent& event) override
        {
            juce::ListBox::mouseDown(event);
            dragStartRow = getRowContainingPosition(event.x, event.y);
            if (dragStartRow < 0 || dragStartRow >= (int)listModel.items.size())
                return;

            selectRow(dragStartRow);
            if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this))
            {
                const auto& symbol = listModel.items[(size_t)dragStartRow];
                showCursorForEvent(event, juce::MouseCursor::DraggingHandCursor);
                container->startDragging("symbol:" + symbol.id, this);
            }
        }

        void mouseDrag(const juce::MouseEvent& event) override
        {
            juce::ListBox::mouseDrag(event);
        }

        void mouseUp(const juce::MouseEvent& event) override
        {
            juce::ListBox::mouseUp(event);
            showCursorForEvent(event, juce::MouseCursor::NormalCursor);
            dragStarted = false;
            dragStartRow = -1;
        }

    private:
        ComponentList& listModel;
        int dragStartRow = -1;
        bool dragStarted = false;
    };

    juce::TextEditor filter;
    ComponentList listModel;
    SymbolListBox components { listModel };
    std::vector<SymbolInfo> allSymbols;
    std::vector<juce::Rectangle<int>> rowBounds;
    std::function<void(juce::String)> onSymbolSelected;
    int hoverRow = -1;
    int selectedRow = 0;

    void add(SymbolInfo item) { allSymbols.push_back(std::move(item)); }

    bool loadSeedLibrary()
    {
        const auto seedFile = juce::File(ELECTRONICS_RESEARCH_ROOT)
            .getChildFile("prototype")
            .getChildFile("data")
            .getChildFile("component_seed.json");
        if (!seedFile.existsAsFile())
            return false;

        const auto parsed = juce::JSON::parse(seedFile);
        const auto* root = parsed.getDynamicObject();
        if (root == nullptr || !root->hasProperty("librarySymbols"))
            return false;

        const auto* symbols = root->getProperty("librarySymbols").getArray();
        if (symbols == nullptr)
            return false;

        for (const auto& entry : *symbols)
        {
            const auto* object = entry.getDynamicObject();
            if (object == nullptr)
                continue;

            const auto id = object->getProperty("id").toString();
            const auto name = object->getProperty("name").toString();
            const auto category = object->getProperty("category").toString();
            if (id.isNotEmpty() && name.isNotEmpty())
                add({ id, name, category });
        }
        return !allSymbols.empty();
    }

    void addFallbackLibrary()
    {
        add({ "resistor", "Resistor", "Passive" });
        add({ "capacitor", "Capacitor", "Passive" });
        add({ "inductor", "Inductor", "Passive" });
        add({ "diode", "Diode", "Discrete" });
        add({ "power_bus", "Power Bus", "Bus" });
        add({ "ground_bus", "Ground Bus", "Bus" });
        add({ "battery", "Battery", "Source" });
        add({ "voltage_source", "DC Voltage Source", "Source" });
        add({ "ac_voltage_source", "AC Voltage Source", "Source" });
        add({ "signal_source", "Signal Source", "Source" });
        add({ "ground", "Ground", "Reference" });
        add({ "opamp_741", "741 Op Amp - provisional", "Analog IC" });
        add({ "npn", "NPN Transistor - generic", "Discrete" });
        add({ "logic_not", "Logic Inverter - behavioral", "Digital" });
    }

    void refreshFilter()
    {
        const auto needle = filter.getText().trim().toLowerCase();
        listModel.items.clear();
        for (const auto& symbol : allSymbols)
        {
            const auto haystack = (symbol.name + " " + symbol.id + " " + symbol.category).toLowerCase();
            if (needle.isEmpty() || haystack.contains(needle))
                listModel.items.push_back(symbol);
        }
        listModel.onSelected = onSymbolSelected;
        selectedRow = listModel.items.empty() ? -1 : std::clamp(selectedRow, 0, (int)listModel.items.size() - 1);
        repaint();
    }

    int rowAt(juce::Point<int> position) const
    {
        for (int i = 0; i < (int)rowBounds.size(); ++i)
            if (rowBounds[(size_t)i].contains(position))
                return i;
        return -1;
    }

    void paintSymbolRow(juce::Graphics& g, int row, juce::Rectangle<int> area)
    {
        if (row < 0 || row >= (int)listModel.items.size())
            return;

        const auto& item = listModel.items[(size_t)row];
        if (row == selectedRow)
        {
            g.setColour(juce::Colour(0xff23394a));
            g.fillRoundedRectangle(area.toFloat(), 4.0f);
        }
        else if (row == hoverRow)
        {
            g.setColour(juce::Colour(0xff202b35));
            g.fillRoundedRectangle(area.toFloat(), 4.0f);
        }

        const auto swatch = area.withWidth(10).withHeight(10).withCentre({ area.getX() + 13, area.getCentreY() });
        g.setColour(item.category == "Source" ? juce::Colour(0xfff4d35e)
                    : item.category == "Bus" ? juce::Colour(0xff78dcca)
                    : item.category == "Analog IC" ? juce::Colour(0xffffc857)
                    : juce::Colour(0xff5aa7c8));
        g.fillRoundedRectangle(swatch.toFloat(), 3.0f);

        g.setColour(juce::Colour(0xffdce9ee));
        g.setFont(juce::Font(14.0f, juce::Font::bold));
        g.drawText(item.name, area.withTrimmedLeft(26).withTrimmedBottom(18), juce::Justification::centredLeft, true);
        g.setColour(juce::Colour(0xff93a7b0));
        g.setFont(juce::Font(12.0f));
        g.drawText(item.category + "  " + item.id, area.withTrimmedLeft(26).withTrimmedTop(20), juce::Justification::centredLeft, true);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ComponentLibraryPanel)
};

class SchematicCanvasPanel final : public juce::Component,
                                   public juce::DragAndDropTarget
{
public:
    SchematicCanvasPanel(std::function<juce::String()> getSelectedSymbol,
                         std::function<bool()> getStampMode,
                         std::function<void(juce::String)> onMessage)
        : getSelectedSymbolId(std::move(getSelectedSymbol)),
          getStampPlacementEnabled(std::move(getStampMode)),
          onStatus(std::move(onMessage))
    {
        setWantsKeyboardFocus(true);
    }

    void setSelectionListener(std::function<void(int, juce::String, juce::String, juce::String, juce::String, juce::String, juce::String, juce::String)> listener)
    {
        onSelectionChanged = std::move(listener);
        notifySelection();
    }

    void setProbeListener(std::function<void(juce::String, juce::String, juce::String)> listener)
    {
        onProbeChanged = std::move(listener);
    }

    void updateSelectedProperties(const juce::String& value,
                                  const juce::String& frequency,
                                  const juce::String& busName,
                                  const juce::String& family,
                                  const juce::String& manufacturerPart)
    {
        if (selectedInstance < 0 || selectedInstance >= (int)instances.size())
            return;

        auto& instance = instances[(size_t)selectedInstance];
        instance.value = value.trim();
        instance.frequency = frequency.trim();
        instance.busName = busName.trim();
        instance.family = family.trim();
        instance.manufacturerPart = manufacturerPart.trim();
        if (onStatus) onStatus("Updated " + instance.refdes + " properties.");
        notifySelection();
        repaint();
    }

    void rotateSelected()
    {
        if (selectedInstance < 0 || selectedInstance >= (int)instances.size())
            return;

        auto& instance = instances[(size_t)selectedInstance];
        const auto symbol = symbolFor(instance.symbolId);
        const auto step = symbol.rotationStepDegrees > 0 ? symbol.rotationStepDegrees : 90;
        instance.rotation = (instance.rotation + step) % 360;
        if (onStatus) onStatus("Rotated " + instance.refdes + " to " + juce::String(instance.rotation) + " degrees.");
        notifySelection();
        repaint();
    }

    void clearCircuit()
    {
        clearModel();
        if (onStatus) onStatus("New electronics research project initialized.");
    }

    bool loadCircuitJson(const juce::String& json, juce::String& error)
    {
        const auto parsed = juce::JSON::parse(json);
        const auto* root = parsed.getDynamicObject();
        if (root == nullptr)
        {
            error = "Project file is not valid JSON.";
            return false;
        }

        const auto* componentArray = root->getProperty("components").getArray();
        if (componentArray == nullptr)
        {
            error = "Project file has no components array.";
            return false;
        }

        std::vector<Instance> loadedInstances;
        std::vector<juce::Point<float>> loadedJunctions;
        std::vector<Wire> loadedWires;
        std::vector<Probe> loadedProbes;
        int highestRefNumber = 0;

        for (const auto& entry : *componentArray)
        {
            const auto* object = entry.getDynamicObject();
            if (object == nullptr)
                continue;

            Instance instance;
            instance.symbolId = stringProperty(*object, "symbol", "resistor");
            instance.refdes = stringProperty(*object, "id", "U" + juce::String((int)loadedInstances.size() + 1));
            instance.value = stringProperty(*object, "value", defaultValueFor(instance.symbolId));
            instance.frequency = stringProperty(*object, "frequency", defaultFrequencyFor(instance.symbolId));
            instance.busName = stringProperty(*object, "busName", defaultBusNameFor(instance.symbolId));
            instance.position = { floatProperty(*object, "x", 120.0f), floatProperty(*object, "y", 120.0f) };
            instance.rotation = (int)floatProperty(*object, "rotation", 0.0f);
            instance.busLength = floatProperty(*object, "length", isRailBus(instance.symbolId) ? 420.0f : 0.0f);

            if (const auto* component = object->getProperty("component").getDynamicObject())
            {
                instance.family = stringProperty(*component, "family", familyFor(instance.symbolId));
                instance.manufacturerPart = stringProperty(*component, "manufacturerPart", {});
            }
            else
            {
                instance.family = familyFor(instance.symbolId);
            }

            highestRefNumber = std::max(highestRefNumber, trailingNumber(instance.refdes));
            loadedInstances.push_back(std::move(instance));
        }

        if (const auto* junctionArray = root->getProperty("junctions").getArray())
        {
            for (const auto& entry : *junctionArray)
            {
                const auto* object = entry.getDynamicObject();
                if (object == nullptr)
                    continue;
                loadedJunctions.push_back({ floatProperty(*object, "x", 0.0f),
                                            floatProperty(*object, "y", 0.0f) });
            }
        }

        auto nodeForLabel = [&](const juce::String& label, WireNode& result) -> bool {
            if (label.startsWith("J"))
            {
                const auto index = label.substring(1).getIntValue() - 1;
                if (index >= 0 && index < (int)loadedJunctions.size())
                {
                    result = WireNode::forJunction(index);
                    return true;
                }
                return false;
            }

            const auto dot = label.lastIndexOfChar('.');
            if (dot <= 0 || dot >= label.length() - 1)
                return false;

            const auto refdes = label.substring(0, dot);
            const auto pinName = label.substring(dot + 1);
            for (int i = 0; i < (int)loadedInstances.size(); ++i)
            {
                if (loadedInstances[(size_t)i].refdes != refdes)
                    continue;

                const auto symbol = symbolFor(loadedInstances[(size_t)i].symbolId);
                for (int p = 0; p < (int)symbol.pins.size(); ++p)
                {
                    if (symbol.pins[(size_t)p].name == pinName)
                    {
                        result = WireNode::forPin({ i, p });
                        return true;
                    }
                }
                return false;
            }
            return false;
        };

        if (const auto* wireArray = root->getProperty("wires").getArray())
        {
            for (const auto& entry : *wireArray)
            {
                const auto* object = entry.getDynamicObject();
                if (object == nullptr)
                    continue;

                Wire wire;
                const auto a = stringProperty(*object, "a", {});
                const auto b = stringProperty(*object, "b", {});
                if (!nodeForLabel(a, wire.a) || !nodeForLabel(b, wire.b))
                {
                    error = "Project wire references an unknown node: " + a + " -> " + b;
                    return false;
                }
                loadedWires.push_back(wire);
            }
        }

        if (const auto* probeArray = root->getProperty("probes").getArray())
        {
            for (const auto& entry : *probeArray)
            {
                const auto* object = entry.getDynamicObject();
                if (object == nullptr)
                    continue;

                const auto id = stringProperty(*object, "id", {});
                const auto label = probeLabel(id);
                if (id.isEmpty() || label.isEmpty())
                    continue;

                WireNode target;
                const auto targetLabel = stringProperty(*object, "target", {});
                if (!nodeForLabel(targetLabel, target))
                {
                    error = "Project probe references an unknown node: " + targetLabel;
                    return false;
                }

                loadedProbes.push_back({ id, label, probeRole(id), target, probeColour(id) });
            }
        }

        const auto previousProbes = probes;
        instances = std::move(loadedInstances);
        junctions = std::move(loadedJunctions);
        wires = std::move(loadedWires);
        probes = std::move(loadedProbes);
        selectedInstance = instances.empty() ? -1 : 0;
        nextRef = std::max(1, highestRefNumber + 1);
        wireDragging = false;
        draggingInstance = false;
        resizingRail = false;
        notifySelection();

        if (onProbeChanged)
        {
            for (const auto& probe : previousProbes)
                onProbeChanged(probe.id, probe.label, {});
            for (const auto& probe : probes)
                onProbeChanged(probe.id, probe.label, nodeLabel(probe.node));
        }

        repaint();
        return true;
    }

    juce::String buildCircuitJson() const
    {
        const auto netNames = computeNetNames();
        juce::String text;
        text << "{\n";
        text << "  \"schemaVersion\": 2,\n";
        text << "  \"kind\": \"electronics_circuit\",\n";
        text << "  \"components\": [\n";
        for (size_t i = 0; i < instances.size(); ++i)
        {
            const auto& instance = instances[i];
            const auto symbol = symbolFor(instance.symbolId);
            if (i != 0) text << ",\n";
            text << "    {\n";
            text << "      \"id\": " << quote(instance.refdes) << ",\n";
            text << "      \"symbol\": " << quote(instance.symbolId) << ",\n";
            text << "      \"component\": {\n";
            text << "        \"archetype\": " << quote(archetypeFor(instance.symbolId)) << ",\n";
            text << "        \"family\": " << nullableQuote(instance.family) << ",\n";
            text << "        \"manufacturerPart\": " << nullableQuote(instance.manufacturerPart) << ",\n";
            text << "        \"datasheetStatus\": \"generic\"\n";
            text << "      },\n";
            text << "      \"x\": " << instance.position.x << ",\n";
            text << "      \"y\": " << instance.position.y << ",\n";
            text << "      \"rotation\": " << instance.rotation << ",\n";
            if (isRailBus(instance.symbolId))
                text << "      \"length\": " << instance.busLength << ",\n";
            text << "      \"value\": " << quote(instance.value) << ",\n";
            text << "      \"frequency\": " << quote(instance.frequency) << ",\n";
            text << "      \"busName\": " << quote(instance.busName) << ",\n";
            text << "      \"parameters\": " << parametersJsonFor(instance) << ",\n";
            text << "      \"pins\": {\n";
            for (size_t p = 0; p < symbol.pins.size(); ++p)
            {
                if (p != 0) text << ",\n";
                text << "        " << quote(symbol.pins[p].name) << ": "
                     << quote(netFor({ (int)i, (int)p }, netNames));
            }
            text << "\n      }\n";
            text << "    }";
        }
        text << "\n  ],\n";
        text << "  \"wires\": [\n";
        for (size_t i = 0; i < wires.size(); ++i)
        {
            const auto& wire = wires[i];
            if (i != 0) text << ",\n";
            text << "    { \"a\": " << quote(nodeLabel(wire.a))
                 << ", \"b\": " << quote(nodeLabel(wire.b)) << " }";
        }
        text << "\n  ],\n";
        text << "  \"junctions\": [\n";
        for (size_t i = 0; i < junctions.size(); ++i)
        {
            const auto& junction = junctions[i];
            if (i != 0) text << ",\n";
            text << "    { \"id\": " << quote("J" + juce::String((int)i + 1))
                 << ", \"x\": " << junction.x
                 << ", \"y\": " << junction.y << " }";
        }
        text << "\n  ],\n";
        text << "  \"probes\": [\n";
        for (size_t i = 0; i < probes.size(); ++i)
        {
            const auto& probe = probes[i];
            if (i != 0) text << ",\n";
            text << "    { \"id\": " << quote(probe.id)
                 << ", \"label\": " << quote(probe.label)
                 << ", \"role\": " << quote(probe.role)
                 << ", \"target\": " << quote(nodeLabel(probe.node))
                 << ", \"net\": " << quote(netForNode(probe.node, netNames))
                 << " }";
        }
        text << "\n  ]\n";
        text << "}\n";
        return text;
    }

    juce::String buildXyceNetlist() const
    {
        const auto netNames = computeNetNames();
        juce::String netlist;
        netlist << "* Djehuti Electronics Lab generated Xyce netlist\n";
        netlist << "* Research prototype output. Circuit JSON remains authoritative.\n\n";

        bool hasGround = false;
        bool hasProbe = false;

        for (size_t i = 0; i < instances.size(); ++i)
        {
            const auto& instance = instances[i];
            const auto symbol = symbolFor(instance.symbolId);
            auto pinNet = [&](const juce::String& pinName) {
                for (size_t p = 0; p < symbol.pins.size(); ++p)
                    if (symbol.pins[p].name == pinName)
                        return netFor({ (int)i, (int)p }, netNames);
                return juce::String("floating");
            };

            if (instance.symbolId == "ground" || instance.symbolId == "ground_bus")
            {
                hasGround = true;
                continue;
            }
            if (instance.symbolId == "power_bus")
            {
                netlist << "* " << instance.refdes << " " << instance.busName << " power bus on net " << pinNet("VBUS") << "\n";
                continue;
            }
            if (instance.symbolId == "resistor")
            {
                netlist << instance.refdes << " " << pinNet("1") << " " << pinNet("2") << " " << instance.value << "\n";
                hasProbe = true;
            }
            else if (instance.symbolId == "capacitor")
            {
                netlist << instance.refdes << " " << pinNet("1") << " " << pinNet("2") << " " << instance.value << "\n";
            }
            else if (instance.symbolId == "inductor")
            {
                netlist << instance.refdes << " " << pinNet("1") << " " << pinNet("2") << " " << instance.value << "\n";
            }
            else if (instance.symbolId == "diode")
            {
                netlist << instance.refdes << " " << pinNet("A") << " " << pinNet("K") << " " << instance.value << "\n";
                netlist << ".MODEL " << instance.value << " D\n";
                hasProbe = true;
            }
            else if (instance.symbolId == "voltage_source")
            {
                netlist << instance.refdes << " " << pinNet("+") << " " << pinNet("-") << " DC " << instance.value << "\n";
                hasProbe = true;
            }
            else if (instance.symbolId == "battery")
            {
                netlist << instance.refdes << " " << pinNet("+") << " " << pinNet("-") << " DC " << instance.value << "\n";
                hasProbe = true;
            }
            else if (instance.symbolId == "ac_voltage_source")
            {
                netlist << instance.refdes << " " << pinNet("+") << " " << pinNet("-") << " AC " << instance.value
                        << " SIN(0 " << instance.value << " " << instance.frequency << ")\n";
                hasProbe = true;
            }
            else if (instance.symbolId == "signal_source")
            {
                netlist << instance.refdes << " " << pinNet("OUT") << " " << pinNet("REF") << " AC " << instance.value
                        << " SIN(0 " << instance.value << " " << instance.frequency << ")\n";
                hasProbe = true;
            }
            else
            {
                netlist << "* " << instance.refdes << " (" << instance.symbolId << ") not lowered to Xyce yet\n";
            }
        }

        netlist << "\n.OP\n";
        netlist << ".PRINT DC";
        const auto printableNets = printableNetNames(netNames);
        if (printableNets.empty())
        {
            netlist << " V(0)";
        }
        else
        {
            for (const auto& net : printableNets)
                netlist << " V(" << net << ")";
        }
        netlist << "\n.END\n";

        if (!hasGround)
            netlist = "* WARNING: no ground symbol found; generated netlist may not solve.\n" + netlist;
        if (!hasProbe)
            netlist = "* WARNING: no lowered source/resistor found; this netlist is mostly structural.\n" + netlist;

        return netlist;
    }

    juce::String buildErcReport() const
    {
        const auto netNames = computeNetNames();
        const auto totalPins = pinCount();
        const auto totalNodes = totalPins + (int)junctions.size();
        std::vector<int> nodeDegree((size_t)std::max(0, totalNodes), 0);

        for (const auto& wire : wires)
        {
            const auto a = nodeOrdinal(wire.a);
            const auto b = nodeOrdinal(wire.b);
            if (a >= 0 && a < totalNodes) ++nodeDegree[(size_t)a];
            if (b >= 0 && b < totalNodes) ++nodeDegree[(size_t)b];
        }

        int errors = 0;
        int warnings = 0;
        int infos = 0;
        juce::String findings;

        auto addFinding = [&](const juce::String& severity, const juce::String& message) {
            if (severity == "ERROR") ++errors;
            else if (severity == "WARN") ++warnings;
            else ++infos;

            findings << "- [" << severity << "] " << message << "\n";
        };

        auto pinNet = [&](int instanceIndex, const juce::String& pinName) {
            const auto symbol = symbolFor(instances[(size_t)instanceIndex].symbolId);
            for (int p = 0; p < (int)symbol.pins.size(); ++p)
                if (symbol.pins[(size_t)p].name == pinName)
                    return netFor({ instanceIndex, p }, netNames);
            return juce::String("floating");
        };

        auto unsupportedForXyce = [](const juce::String& symbolId) {
            return symbolId == "opamp_741" || symbolId == "npn" || symbolId == "logic_not";
        };

        if (instances.empty())
            addFinding("ERROR", "No components are placed on the schematic.");

        bool hasGround = false;
        bool hasLoweredPrimitive = false;
        for (const auto& instance : instances)
        {
            if (instance.symbolId == "ground" || instance.symbolId == "ground_bus")
                hasGround = true;
            if (instance.symbolId == "resistor"
                || instance.symbolId == "capacitor"
                || instance.symbolId == "inductor"
                || instance.symbolId == "diode"
                || instance.symbolId == "voltage_source"
                || instance.symbolId == "battery"
                || instance.symbolId == "ac_voltage_source"
                || instance.symbolId == "signal_source")
                hasLoweredPrimitive = true;
        }

        if (!hasGround)
            addFinding("ERROR", "No ground reference is present. Add a ground or ground bus before running solver-backed analysis.");
        if (!hasLoweredPrimitive && !instances.empty())
            addFinding("WARN", "No currently lowered Xyce primitive is present. The generated netlist will be mostly structural.");

        std::set<juce::String> refdesSeen;
        for (int i = 0; i < (int)instances.size(); ++i)
        {
            const auto& instance = instances[(size_t)i];
            const auto symbol = symbolFor(instance.symbolId);

            if (refdesSeen.count(instance.refdes) != 0)
                addFinding("ERROR", "Duplicate reference designator found: " + instance.refdes + ".");
            refdesSeen.insert(instance.refdes);

            if (instance.value.trim().isEmpty()
                && instance.symbolId != "ground"
                && instance.symbolId != "ground_bus"
                && instance.symbolId != "power_bus")
                addFinding("WARN", instance.refdes + " has no value or model text.");

            if (unsupportedForXyce(instance.symbolId))
                addFinding("INFO", instance.refdes + " (" + instance.symbolId + ") is captured in the model but not lowered to Xyce yet.");

            for (int p = 0; p < (int)symbol.pins.size(); ++p)
            {
                const auto ordinal = pinOrdinal({ i, p });
                if (ordinal >= 0 && ordinal < (int)nodeDegree.size() && nodeDegree[(size_t)ordinal] == 0)
                    addFinding("WARN", instance.refdes + "." + symbol.pins[(size_t)p].name + " is not wired.");
            }

            if (symbol.pins.size() == 2
                && (instance.symbolId == "resistor"
                    || instance.symbolId == "capacitor"
                    || instance.symbolId == "inductor"
                    || instance.symbolId == "diode"))
            {
                const auto a = netFor({ i, 0 }, netNames);
                const auto b = netFor({ i, 1 }, netNames);
                if (a == b)
                    addFinding("WARN", instance.refdes + " has both pins on " + a + ".");
            }

            if ((instance.symbolId == "voltage_source" || instance.symbolId == "battery" || instance.symbolId == "ac_voltage_source")
                && pinNet(i, "+") == pinNet(i, "-"))
                addFinding("ERROR", instance.refdes + " has positive and negative terminals on the same net.");
            if (instance.symbolId == "signal_source" && pinNet(i, "OUT") == pinNet(i, "REF"))
                addFinding("ERROR", instance.refdes + " has OUT and REF on the same net.");
        }

        for (const auto& wire : wires)
        {
            if (sameNode(wire.a, wire.b))
                addFinding("WARN", "A wire loops back to " + nodeLabel(wire.a) + ".");
        }

        if (probes.empty() && !instances.empty())
            addFinding("INFO", "No lab probes are assigned yet, so instruments do not have schematic targets.");

        juce::String report;
        report << "# Electrical Rule Check\n\n";
        report << "- Components: " << (int)instances.size() << "\n";
        report << "- Wires: " << (int)wires.size() << "\n";
        report << "- Junctions: " << (int)junctions.size() << "\n";
        report << "- Probes: " << (int)probes.size() << "\n";
        report << "- Errors: " << errors << "\n";
        report << "- Warnings: " << warnings << "\n";
        report << "- Info: " << infos << "\n\n";

        if (findings.isEmpty())
            report << "No ERC findings.\n";
        else
            report << "## Findings\n\n" << findings;

        return report;
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff0e141a));
        drawGrid(g);
        drawWires(g);
        drawInstances(g);
        drawProbes(g);
        drawPendingWire(g);

        g.setColour(juce::Colour(0xff93a7b0));
        g.setFont(juce::Font(13.0f));
        const auto stampOn = getStampPlacementEnabled != nullptr && getStampPlacementEnabled();
        g.drawText(stampOn ? "Stamp mode: click empty canvas to place selected symbols, drag selected parts to move, R rotates."
                           : "Drag pins/wires/rails to connect. Select a rail and drag its end handles to resize. Delete removes selected parts.",
                   getLocalBounds().reduced(12).removeFromBottom(24),
                   juce::Justification::centredLeft);

        if (dragHover)
            g.drawText(dragMessage, getLocalBounds().reduced(12).removeFromBottom(24), juce::Justification::centredRight);
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        grabKeyboardFocus();
        const auto p = snap(event.position);
        if (event.mods.isRightButtonDown())
        {
            if (releaseProbeAt(event.position))
            {
                repaint();
                return;
            }
            disconnectAt(event.position);
            repaint();
            return;
        }

        if (beginRailResize(event.position))
        {
            repaint();
            return;
        }

        if (auto rail = hitTestRailBus(event.position); rail >= 0)
        {
            beginWireDrag(createRailTap(rail, p), event.position);
            repaint();
            return;
        }

        if (auto pin = hitTestPin(event.position); pin.instanceIndex >= 0)
        {
            beginWireDrag(WireNode::forPin(pin), event.position);
            repaint();
            return;
        }

        if (auto junction = hitTestJunction(event.position); junction >= 0)
        {
            beginWireDrag(WireNode::forJunction(junction), event.position);
            repaint();
            return;
        }

        if (auto wireIndex = hitTestWire(event.position); wireIndex >= 0)
        {
            beginWireDrag(createJunctionOnWire(wireIndex, p), event.position);
            repaint();
            return;
        }

        if (const auto instanceIndex = hitTestInstance(event.position); instanceIndex >= 0)
        {
            selectedInstance = instanceIndex;
            draggingInstance = true;
            dragStartMouse = event.position;
            dragStartPosition = instances[(size_t)selectedInstance].position;
            notifySelection();
            repaint();
            return;
        }

        if (getStampPlacementEnabled != nullptr && getStampPlacementEnabled())
        {
            const auto selected = getSelectedSymbolId != nullptr ? getSelectedSymbolId() : juce::String("resistor");
            placeSymbol(selected, p);
            repaint();
        }
    }

    void resized() override {}

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (resizingRail)
        {
            resizeRail(event.position);
            repaint();
            return;
        }

        if (wireDragging)
        {
            wireDragPosition = event.position;
            repaint();
            return;
        }

        if (!draggingInstance || selectedInstance < 0 || selectedInstance >= (int)instances.size())
            return;

        auto& instance = instances[(size_t)selectedInstance];
        instance.position = snap(dragStartPosition + (event.position - dragStartMouse));
        notifySelection();
        repaint();
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        if (wireDragging)
        {
            finishWireDrag(event.position);
            repaint();
        }

        draggingInstance = false;
        resizingRail = false;
        resizingRailInstance = -1;
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey)
        {
            wireDragging = false;
            repaint();
            return true;
        }
        if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        {
            deleteSelected();
            return true;
        }
        if (key.getTextCharacter() == 'r' || key.getTextCharacter() == 'R')
        {
            rotateSelected();
            return true;
        }
        return false;
    }

    bool isInterestedInDragSource(const SourceDetails& details) override
    {
        const auto description = details.description.toString();
        return description.startsWith("symbol:") || description.startsWith("probe:");
    }

    void itemDragEnter(const SourceDetails& details) override
    {
        dragHover = true;
        dragMessage = details.description.toString().startsWith("probe:")
            ? "Drop probe on a pin or wire"
            : "Drop symbol on schematic";
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        repaint();
    }

    void itemDragExit(const SourceDetails&) override
    {
        dragHover = false;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        repaint();
    }

    void itemDropped(const SourceDetails& details) override
    {
        dragHover = false;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        const auto description = details.description.toString();
        if (description.startsWith("probe:"))
        {
            const auto node = nodeAt(details.localPosition.toFloat(), snap(details.localPosition.toFloat()));
            if (!node.isValid())
            {
                if (onStatus) onStatus("Probe drop needs a pin, junction, or wire.");
                repaint();
                return;
            }

            assignProbe(description.fromFirstOccurrenceOf("probe:", false, false), node);
            repaint();
            return;
        }

        if (!description.startsWith("symbol:"))
            return;

        const auto symbolId = description.fromFirstOccurrenceOf("symbol:", false, false);
        placeSymbol(symbolId, snap(details.localPosition.toFloat()));
        repaint();
    }

private:
    struct PinDef
    {
        juce::String name;
        juce::Point<float> offset;
    };

    struct SymbolDef
    {
        juce::String id;
        juce::String title;
        juce::Rectangle<float> bounds;
        std::vector<PinDef> pins;
        int rotationStepDegrees = 90;
    };

    struct Instance
    {
        juce::String symbolId;
        juce::String refdes;
        juce::String value;
        juce::String frequency;
        juce::String busName;
        juce::String family;
        juce::String manufacturerPart;
        juce::Point<float> position;
        int rotation = 0;
        float busLength = 420.0f;
    };

    struct PinRef
    {
        int instanceIndex = -1;
        int pinIndex = -1;
    };

    struct WireNode
    {
        PinRef pin;
        int junctionIndex = -1;

        static WireNode forPin(PinRef pinRef)
        {
            WireNode node;
            node.pin = pinRef;
            return node;
        }

        static WireNode forJunction(int index)
        {
            WireNode node;
            node.junctionIndex = index;
            return node;
        }

        bool isPin() const { return pin.instanceIndex >= 0 && pin.pinIndex >= 0; }
        bool isJunction() const { return junctionIndex >= 0; }
        bool isValid() const { return isPin() || isJunction(); }
    };

    struct Wire
    {
        WireNode a;
        WireNode b;
    };

    struct Probe
    {
        juce::String id;
        juce::String label;
        juce::String role;
        WireNode node;
        juce::Colour colour;
    };

    struct DisjointSet
    {
        std::vector<int> parent;

        explicit DisjointSet(int count)
        {
            parent.resize((size_t)count);
            for (int i = 0; i < count; ++i) parent[(size_t)i] = i;
        }

        int find(int x)
        {
            auto& p = parent[(size_t)x];
            if (p == x) return x;
            p = find(p);
            return p;
        }

        void unite(int a, int b)
        {
            const auto ra = find(a);
            const auto rb = find(b);
            if (ra != rb) parent[(size_t)rb] = ra;
        }
    };

    std::vector<Instance> instances;
    std::vector<Wire> wires;
    std::vector<juce::Point<float>> junctions;
    std::vector<Probe> probes;
    WireNode wireDragStart;
    int selectedInstance = -1;
    int nextRef = 1;
    bool dragHover = false;
    juce::String dragMessage = "Drop symbol on schematic";
    bool wireDragging = false;
    bool draggingInstance = false;
    bool resizingRail = false;
    bool resizingLeftRailEnd = false;
    int resizingRailInstance = -1;
    float fixedRailEndX = 0.0f;
    juce::Point<float> dragStartMouse;
    juce::Point<float> dragStartPosition;
    juce::Point<float> wireDragPosition;
    std::function<juce::String()> getSelectedSymbolId;
    std::function<bool()> getStampPlacementEnabled;
    std::function<void(juce::String)> onStatus;
    std::function<void(juce::String, juce::String, juce::String)> onProbeChanged;
    std::function<void(int, juce::String, juce::String, juce::String, juce::String, juce::String, juce::String, juce::String)> onSelectionChanged;

    static juce::String quote(const juce::String& text)
    {
        return "\"" + text.replace("\\", "\\\\").replace("\"", "\\\"") + "\"";
    }

    static juce::String nullableQuote(const juce::String& text)
    {
        return text.isEmpty() ? juce::String("null") : quote(text);
    }

    static juce::String stringProperty(const juce::DynamicObject& object,
                                       const char* name,
                                       const juce::String& fallback)
    {
        const auto property = juce::Identifier(name);
        if (!object.hasProperty(property))
            return fallback;

        const auto value = object.getProperty(property);
        if (value.isVoid() || value.isUndefined())
            return fallback;

        return value.toString();
    }

    static float floatProperty(const juce::DynamicObject& object, const char* name, float fallback)
    {
        const auto property = juce::Identifier(name);
        if (!object.hasProperty(property))
            return fallback;

        const auto value = object.getProperty(property);
        if (value.isVoid() || value.isUndefined())
            return fallback;

        return (float)(double)value;
    }

    static int trailingNumber(const juce::String& text)
    {
        auto start = text.length();
        const auto end = start;
        while (start > 0)
        {
            const auto c = text[start - 1];
            if (c < '0' || c > '9')
                break;
            --start;
        }

        return start == end ? 0 : text.substring(start, end).getIntValue();
    }

    void clearModel()
    {
        const auto previousProbes = probes;
        instances.clear();
        wires.clear();
        junctions.clear();
        probes.clear();
        selectedInstance = -1;
        nextRef = 1;
        wireDragging = false;
        draggingInstance = false;
        resizingRail = false;
        resizingRailInstance = -1;
        notifySelection();

        if (onProbeChanged)
            for (const auto& probe : previousProbes)
                onProbeChanged(probe.id, probe.label, {});

        repaint();
    }

    void drawGrid(juce::Graphics& g)
    {
        const auto b = getLocalBounds();
        g.setColour(juce::Colour(0xff18222b));
        for (int x = 0; x < b.getWidth(); x += 24) g.drawVerticalLine(x, 0.0f, (float)b.getHeight());
        for (int y = 0; y < b.getHeight(); y += 24) g.drawHorizontalLine(y, 0.0f, (float)b.getWidth());
        g.setColour(juce::Colour(0xff26323d));
        for (int x = 0; x < b.getWidth(); x += 120) g.drawVerticalLine(x, 0.0f, (float)b.getHeight());
        for (int y = 0; y < b.getHeight(); y += 120) g.drawHorizontalLine(y, 0.0f, (float)b.getWidth());
    }

    static juce::Point<float> snap(juce::Point<float> p)
    {
        return { std::round(p.x / 24.0f) * 24.0f, std::round(p.y / 24.0f) * 24.0f };
    }

    static bool isRailBus(const juce::String& symbolId)
    {
        return symbolId == "power_bus" || symbolId == "ground_bus";
    }

    SymbolDef symbolFor(const juce::String& id) const
    {
        if (id == "capacitor")
            return { id, "C", { -30, -18, 60, 36 }, { { "1", { -42, 0 } }, { "2", { 42, 0 } } } };
        if (id == "inductor")
            return { id, "L", { -34, -18, 68, 36 }, { { "1", { -54, 0 } }, { "2", { 54, 0 } } }, 45 };
        if (id == "diode")
            return { id, "D", { -28, -24, 56, 48 }, { { "A", { -54, 0 } }, { "K", { 54, 0 } } }, 45 };
        if (id == "power_bus")
            return { id, "PWR", { -210, -8, 420, 16 }, { { "VBUS", { 0, 0 } } } };
        if (id == "ground_bus")
            return { id, "GND BUS", { -210, -8, 420, 16 }, { { "0", { 0, 0 } } } };
        if (id == "battery")
            return { id, "BAT", { -24, -34, 48, 68 }, { { "+", { 0, -52 } }, { "-", { 0, 52 } } } };
        if (id == "voltage_source")
            return { id, "V", { -24, -24, 48, 48 }, { { "+", { 0, -42 } }, { "-", { 0, 42 } } } };
        if (id == "ac_voltage_source")
            return { id, "AC", { -28, -28, 56, 56 }, { { "+", { 0, -46 } }, { "-", { 0, 46 } } } };
        if (id == "signal_source")
            return { id, "SIG", { -40, -24, 80, 48 }, { { "OUT", { 58, 0 } }, { "REF", { -58, 0 } } } };
        if (id == "ground")
            return { id, "GND", { -24, -12, 48, 32 }, { { "0", { 0, -24 } } } };
        if (id == "opamp_741")
            return { id, "uA741", { -50, -42, 100, 84 }, { { "IN+", { -72, -20 } }, { "IN-", { -72, 20 } }, { "OUT", { 72, 0 } }, { "V+", { 0, -62 } }, { "V-", { 0, 62 } } } };
        if (id == "npn")
            return { id, "NPN", { -30, -36, 60, 72 }, { { "B", { -54, 0 } }, { "C", { 28, -48 } }, { "E", { 28, 48 } } } };
        if (id == "logic_not")
            return { id, "NOT", { -42, -30, 84, 60 }, { { "A", { -66, 0 } }, { "Y", { 66, 0 } } } };
        return { "resistor", "R", { -36, -14, 72, 28 }, { { "1", { -54, 0 } }, { "2", { 54, 0 } } } };
    }

    juce::String defaultValueFor(const juce::String& symbolId) const
    {
        if (symbolId == "resistor") return "10k";
        if (symbolId == "capacitor") return "1u";
        if (symbolId == "inductor") return "10m";
        if (symbolId == "diode") return "1N4148";
        if (symbolId == "power_bus") return "+V";
        if (symbolId == "ground_bus") return "0";
        if (symbolId == "battery") return "9";
        if (symbolId == "voltage_source") return "10";
        if (symbolId == "ac_voltage_source") return "1";
        if (symbolId == "signal_source") return "1";
        if (symbolId == "opamp_741") return "uA741";
        if (symbolId == "npn") return "generic_npn";
        return "";
    }

    juce::String archetypeFor(const juce::String& symbolId) const
    {
        if (symbolId == "resistor") return "passive.resistor";
        if (symbolId == "capacitor") return "passive.capacitor";
        if (symbolId == "inductor") return "passive.inductor";
        if (symbolId == "diode") return "discrete.diode";
        if (symbolId == "power_bus") return "net.power_bus";
        if (symbolId == "ground" || symbolId == "ground_bus") return "net.ground_reference";
        if (symbolId == "battery") return "source.battery";
        if (symbolId == "voltage_source") return "source.dc_voltage";
        if (symbolId == "ac_voltage_source") return "source.ac_voltage";
        if (symbolId == "signal_source") return "source.signal";
        if (symbolId == "opamp_741") return "analog.op_amp";
        if (symbolId == "npn") return "discrete.bjt.npn";
        if (symbolId == "logic_not") return "digital.logic.not";
        return "unknown";
    }

    juce::String familyFor(const juce::String& symbolId) const
    {
        if (symbolId == "opamp_741") return "741";
        if (symbolId == "npn") return "generic_npn";
        return "";
    }

    juce::String defaultFrequencyFor(const juce::String& symbolId) const
    {
        if (symbolId == "ac_voltage_source" || symbolId == "signal_source") return "1k";
        return "";
    }

    juce::String defaultBusNameFor(const juce::String& symbolId) const
    {
        if (symbolId == "power_bus") return "+V";
        if (symbolId == "ground" || symbolId == "ground_bus") return "0";
        return "";
    }

    juce::String parametersJsonFor(const Instance& instance) const
    {
        const auto& symbolId = instance.symbolId;
        if (symbolId == "resistor")
            return "{ \"resistance\": { \"value\": " + quote(instance.value) + ", \"unit\": \"ohm\" } }";
        if (symbolId == "capacitor")
            return "{ \"capacitance\": { \"value\": " + quote(instance.value) + ", \"unit\": \"F\" } }";
        if (symbolId == "inductor")
            return "{ \"inductance\": { \"value\": " + quote(instance.value) + ", \"unit\": \"H\" } }";
        if (symbolId == "diode")
            return "{ \"model\": " + quote(instance.value) + " }";
        if (symbolId == "power_bus")
            return "{ \"name\": " + quote(instance.busName) + ", \"voltageHint\": null }";
        if (symbolId == "ground" || symbolId == "ground_bus")
            return "{ \"name\": " + quote(instance.busName) + ", \"voltageHint\": \"0V\" }";
        if (symbolId == "battery")
            return "{ \"voltage\": { \"value\": " + quote(instance.value) + ", \"unit\": \"V\" } }";
        if (symbolId == "voltage_source")
            return "{ \"dcVoltage\": { \"value\": " + quote(instance.value) + ", \"unit\": \"V\" } }";
        if (symbolId == "ac_voltage_source")
            return "{ \"amplitude\": { \"value\": " + quote(instance.value) + ", \"unit\": \"V\" }, \"frequency\": { \"value\": " + quote(instance.frequency) + ", \"unit\": \"Hz\" }, \"offset\": { \"value\": \"0\", \"unit\": \"V\" } }";
        if (symbolId == "signal_source")
            return "{ \"waveform\": \"sine\", \"amplitude\": { \"value\": " + quote(instance.value) + ", \"unit\": \"V\" }, \"frequency\": { \"value\": " + quote(instance.frequency) + ", \"unit\": \"Hz\" } }";
        return "{}";
    }

    int pinOrdinal(const PinRef& pin) const
    {
        int ordinal = 0;
        for (int i = 0; i < pin.instanceIndex; ++i)
            ordinal += (int)symbolFor(instances[(size_t)i].symbolId).pins.size();
        return ordinal + pin.pinIndex;
    }

    int pinCount() const
    {
        int count = 0;
        for (const auto& instance : instances)
            count += (int)symbolFor(instance.symbolId).pins.size();
        return count;
    }

    int nodeOrdinal(const WireNode& node) const
    {
        if (node.isPin())
            return pinOrdinal(node.pin);
        if (node.isJunction())
            return pinCount() + node.junctionIndex;
        return -1;
    }

    std::map<int, juce::String> computeNetNames() const
    {
        const auto totalPins = pinCount();
        const auto totalNodes = totalPins + (int)junctions.size();

        std::map<int, juce::String> result;
        if (totalNodes <= 0)
            return result;

        DisjointSet sets(totalNodes);
        for (const auto& wire : wires)
        {
            const auto a = nodeOrdinal(wire.a);
            const auto b = nodeOrdinal(wire.b);
            if (a < 0 || b < 0)
                continue;
            sets.unite(a, b);
        }

        std::set<int> groundRoots;
        for (size_t i = 0; i < instances.size(); ++i)
        {
            if (instances[i].symbolId != "ground" && instances[i].symbolId != "ground_bus")
                continue;
            const auto symbol = symbolFor(instances[i].symbolId);
            for (size_t p = 0; p < symbol.pins.size(); ++p)
                groundRoots.insert(sets.find(pinOrdinal({ (int)i, (int)p })));
        }

        std::map<int, int> assigned;
        int nextNet = 1;
        for (int pin = 0; pin < totalNodes; ++pin)
        {
            const auto root = sets.find(pin);
            if (groundRoots.count(root) != 0)
            {
                result[pin] = "0";
                continue;
            }
            if (assigned.count(root) == 0)
                assigned[root] = nextNet++;
            result[pin] = "n" + juce::String(assigned[root]);
        }
        return result;
    }

    juce::String netFor(const PinRef& pin, const std::map<int, juce::String>& netNames) const
    {
        if (pin.instanceIndex < 0 || pin.instanceIndex >= (int)instances.size())
            return "floating";
        const auto ordinal = pinOrdinal(pin);
        const auto found = netNames.find(ordinal);
        return found != netNames.end() ? found->second : "floating";
    }

    juce::String netForNode(const WireNode& node, const std::map<int, juce::String>& netNames) const
    {
        const auto ordinal = nodeOrdinal(node);
        if (ordinal < 0)
            return "floating";
        const auto found = netNames.find(ordinal);
        return found != netNames.end() ? found->second : "floating";
    }

    std::vector<juce::String> printableNetNames(const std::map<int, juce::String>& netNames) const
    {
        std::set<juce::String> unique;
        for (const auto& entry : netNames)
            if (entry.second != "0")
                unique.insert(entry.second);

        std::vector<juce::String> result;
        result.reserve(unique.size());
        for (const auto& net : unique)
            result.push_back(net);
        return result;
    }

    juce::Point<float> pinPosition(const PinRef& pin) const
    {
        if (pin.instanceIndex < 0 || pin.instanceIndex >= (int)instances.size()) return {};
        const auto& instance = instances[(size_t)pin.instanceIndex];
        const auto symbol = symbolFor(instance.symbolId);
        if (pin.pinIndex < 0 || pin.pinIndex >= (int)symbol.pins.size()) return instance.position;
        return instance.position + rotateOffset(symbol.pins[(size_t)pin.pinIndex].offset, instance.rotation);
    }

    juce::String pinLabel(const PinRef& pin) const
    {
        if (pin.instanceIndex < 0 || pin.instanceIndex >= (int)instances.size()) return {};
        const auto& instance = instances[(size_t)pin.instanceIndex];
        const auto symbol = symbolFor(instance.symbolId);
        if (pin.pinIndex < 0 || pin.pinIndex >= (int)symbol.pins.size()) return instance.refdes;
        return instance.refdes + "." + symbol.pins[(size_t)pin.pinIndex].name;
    }

    juce::Point<float> nodePosition(const WireNode& node) const
    {
        if (node.isPin())
            return pinPosition(node.pin);
        if (node.isJunction() && node.junctionIndex < (int)junctions.size())
            return junctions[(size_t)node.junctionIndex];
        return {};
    }

    juce::String nodeLabel(const WireNode& node) const
    {
        if (node.isPin())
            return pinLabel(node.pin);
        if (node.isJunction())
            return "J" + juce::String(node.junctionIndex + 1);
        return {};
    }

    static bool sameNode(const WireNode& a, const WireNode& b)
    {
        if (a.isPin() && b.isPin())
            return a.pin.instanceIndex == b.pin.instanceIndex && a.pin.pinIndex == b.pin.pinIndex;
        if (a.isJunction() && b.isJunction())
            return a.junctionIndex == b.junctionIndex;
        return false;
    }

    bool isRailAnchorNode(const WireNode& node) const
    {
        return node.isPin()
            && node.pin.instanceIndex >= 0
            && node.pin.instanceIndex < (int)instances.size()
            && isRailBus(instances[(size_t)node.pin.instanceIndex].symbolId);
    }

    bool isInternalRailTapWire(const Wire& wire) const
    {
        return (isRailAnchorNode(wire.a) && wire.b.isJunction())
            || (isRailAnchorNode(wire.b) && wire.a.isJunction());
    }

    juce::Point<float> nodeLeadDirection(const WireNode& node) const
    {
        if (!node.isPin() || node.pin.instanceIndex < 0 || node.pin.instanceIndex >= (int)instances.size())
            return {};

        const auto& instance = instances[(size_t)node.pin.instanceIndex];
        const auto symbol = symbolFor(instance.symbolId);
        if (node.pin.pinIndex < 0 || node.pin.pinIndex >= (int)symbol.pins.size())
            return {};

        const auto offset = rotateOffset(symbol.pins[(size_t)node.pin.pinIndex].offset, instance.rotation);
        const auto length = std::sqrt(offset.x * offset.x + offset.y * offset.y);
        if (length <= 0.001f)
            return {};

        return { offset.x / length, offset.y / length };
    }

    std::vector<juce::Point<float>> routedWirePoints(const WireNode& a, const WireNode& b) const
    {
        return routedWirePoints(a, nodePosition(b), nodeLeadDirection(b));
    }

    std::vector<juce::Point<float>> routedWirePoints(const WireNode& a,
                                                     juce::Point<float> b,
                                                     juce::Point<float> bLead) const
    {
        constexpr auto leadLength = 24.0f;
        const auto start = nodePosition(a);
        const auto aLead = nodeLeadDirection(a);
        const auto startRun = aLead == juce::Point<float>() ? start : start + aLead * leadLength;
        const auto endRun = bLead == juce::Point<float>() ? b : b + bLead * leadLength;

        std::vector<juce::Point<float>> points;
        points.push_back(start);
        if (startRun.getDistanceFrom(start) > 0.1f)
            points.push_back(startRun);

        if (std::abs(startRun.x - endRun.x) <= 0.1f || std::abs(startRun.y - endRun.y) <= 0.1f)
        {
            points.push_back(endRun);
        }
        else
        {
            const auto midX = std::round((startRun.x + endRun.x) * 0.5f / 24.0f) * 24.0f;
            points.push_back({ midX, startRun.y });
            points.push_back({ midX, endRun.y });
            points.push_back(endRun);
        }

        if (b.getDistanceFrom(endRun) > 0.1f)
            points.push_back(b);

        return points;
    }

    PinRef hitTestPin(juce::Point<float> p) const
    {
        for (int i = (int)instances.size() - 1; i >= 0; --i)
        {
            if (isRailBus(instances[(size_t)i].symbolId))
                continue;

            const auto symbol = symbolFor(instances[(size_t)i].symbolId);
            for (int j = 0; j < (int)symbol.pins.size(); ++j)
            {
                const auto pin = pinPosition({ i, j });
                if (pin.getDistanceFrom(p) <= 14.0f)
                    return { i, j };
            }
        }
        return {};
    }

    int hitTestJunction(juce::Point<float> p) const
    {
        for (int i = (int)junctions.size() - 1; i >= 0; --i)
            if (junctions[(size_t)i].getDistanceFrom(p) <= 12.0f)
                return i;
        return -1;
    }

    static float distanceToSegment(juce::Point<float> p, juce::Point<float> a, juce::Point<float> b)
    {
        const auto ab = b - a;
        const auto ap = p - a;
        const auto lengthSquared = ab.x * ab.x + ab.y * ab.y;
        if (lengthSquared <= 0.001f)
            return p.getDistanceFrom(a);

        const auto t = std::clamp((ap.x * ab.x + ap.y * ab.y) / lengthSquared, 0.0f, 1.0f);
        return p.getDistanceFrom({ a.x + ab.x * t, a.y + ab.y * t });
    }

    float distanceToWire(juce::Point<float> p, const Wire& wire) const
    {
        if (isInternalRailTapWire(wire))
            return std::numeric_limits<float>::max();

        const auto points = routedWirePoints(wire.a, wire.b);
        auto best = std::numeric_limits<float>::max();
        for (size_t i = 1; i < points.size(); ++i)
            best = std::min(best, distanceToSegment(p, points[i - 1], points[i]));
        return best;
    }

    int hitTestWire(juce::Point<float> p) const
    {
        for (int i = (int)wires.size() - 1; i >= 0; --i)
            if (distanceToWire(p, wires[(size_t)i]) <= 8.0f)
                return i;
        return -1;
    }

    int hitTestInstance(juce::Point<float> p) const
    {
        for (int i = (int)instances.size() - 1; i >= 0; --i)
        {
            const auto& instance = instances[(size_t)i];
            const auto symbol = symbolFor(instance.symbolId);
            if (orientedBounds(instance, symbol).expanded(4.0f).contains(p))
                return i;
        }
        return -1;
    }

    WireNode nodeAt(juce::Point<float> rawPosition, juce::Point<float> snappedPosition)
    {
        if (auto pin = hitTestPin(rawPosition); pin.instanceIndex >= 0)
            return WireNode::forPin(pin);

        if (auto junction = hitTestJunction(rawPosition); junction >= 0)
            return WireNode::forJunction(junction);

        if (auto rail = hitTestRailBus(rawPosition); rail >= 0)
            return createRailTap(rail, snappedPosition);

        if (auto wireIndex = hitTestWire(rawPosition); wireIndex >= 0)
            return createJunctionOnWire(wireIndex, snappedPosition);

        return {};
    }

    static juce::Point<float> rotateOffset(juce::Point<float> offset, int rotation)
    {
        switch (((rotation % 360) + 360) % 360)
        {
            case 90: return { -offset.y, offset.x };
            case 180: return { -offset.x, -offset.y };
            case 270: return { offset.y, -offset.x };
            default: return offset;
        }
    }

    juce::Rectangle<float> orientedBounds(const Instance& instance, const SymbolDef& symbol) const
    {
        if (isRailBus(instance.symbolId))
            return railBounds(instance).expanded(0.0f, 10.0f);

        const auto r = symbol.bounds;
        const auto rotation = ((instance.rotation % 360) + 360) % 360;
        if (rotation == 90 || rotation == 270)
            return { instance.position.x - r.getHeight() * 0.5f,
                     instance.position.y - r.getWidth() * 0.5f,
                     r.getHeight(),
                     r.getWidth() };
        return r.translated(instance.position.x, instance.position.y);
    }

    juce::Rectangle<float> railBounds(const Instance& instance) const
    {
        const auto length = std::max(120.0f, instance.busLength);
        return { instance.position.x - length * 0.5f, instance.position.y - 1.5f, length, 3.0f };
    }

    int hitTestRailBus(juce::Point<float> p) const
    {
        for (int i = (int)instances.size() - 1; i >= 0; --i)
        {
            const auto& instance = instances[(size_t)i];
            if (!isRailBus(instance.symbolId))
                continue;

            if (railBounds(instance).expanded(0.0f, 12.0f).contains(p))
                return i;
        }
        return -1;
    }

    juce::Rectangle<float> railHandleBounds(const Instance& instance, bool left) const
    {
        const auto rail = railBounds(instance);
        const auto x = left ? rail.getX() : rail.getRight();
        return { x - 7.0f, instance.position.y - 12.0f, 14.0f, 24.0f };
    }

    bool beginRailResize(juce::Point<float> p)
    {
        if (selectedInstance < 0 || selectedInstance >= (int)instances.size())
            return false;

        const auto& instance = instances[(size_t)selectedInstance];
        if (!isRailBus(instance.symbolId))
            return false;

        if (railHandleBounds(instance, true).contains(p))
        {
            resizingRail = true;
            resizingLeftRailEnd = true;
            resizingRailInstance = selectedInstance;
            fixedRailEndX = railBounds(instance).getRight();
            return true;
        }

        if (railHandleBounds(instance, false).contains(p))
        {
            resizingRail = true;
            resizingLeftRailEnd = false;
            resizingRailInstance = selectedInstance;
            fixedRailEndX = railBounds(instance).getX();
            return true;
        }

        return false;
    }

    void resizeRail(juce::Point<float> p)
    {
        if (resizingRailInstance < 0 || resizingRailInstance >= (int)instances.size())
            return;

        auto& instance = instances[(size_t)resizingRailInstance];
        if (!isRailBus(instance.symbolId))
            return;

        constexpr auto minLength = 120.0f;
        auto movingX = snap(p).x;
        if (std::abs(movingX - fixedRailEndX) < minLength)
            movingX = fixedRailEndX + (movingX < fixedRailEndX ? -minLength : minLength);

        const auto left = std::min(movingX, fixedRailEndX);
        const auto right = std::max(movingX, fixedRailEndX);
        instance.busLength = right - left;
        instance.position.x = (left + right) * 0.5f;
        notifySelection();
    }

    void beginWireDrag(WireNode node, juce::Point<float> position)
    {
        if (!node.isValid())
            return;

        wireDragStart = node;
        wireDragPosition = position;
        wireDragging = true;
        draggingInstance = false;
        if (onStatus) onStatus("Wire drag: " + nodeLabel(node) + ". Release on another pin or wire.");
    }

    void finishWireDrag(juce::Point<float> position)
    {
        wireDragging = false;
        if (!wireDragStart.isValid())
            return;

        const auto target = nodeAt(position, snap(position));
        if (!target.isValid())
        {
            if (onStatus) onStatus("Wire cancelled.");
            wireDragStart = {};
            return;
        }

        if (sameNode(wireDragStart, target))
        {
            if (onStatus) onStatus("Wire cancelled: start and end are the same connector.");
            wireDragStart = {};
            return;
        }

        wires.push_back({ wireDragStart, target });
        if (onStatus) onStatus("Connected " + nodeLabel(wireDragStart) + " to " + nodeLabel(target) + ".");
        wireDragStart = {};
    }

    void disconnectAt(juce::Point<float> position)
    {
        if (auto wireIndex = hitTestWire(position); wireIndex >= 0)
        {
            const auto removed = wires[(size_t)wireIndex];
            wires.erase(wires.begin() + wireIndex);
            if (onStatus) onStatus("Disconnected wire " + nodeLabel(removed.a) + " to " + nodeLabel(removed.b) + ".");
            return;
        }

        if (auto pin = hitTestPin(position); pin.instanceIndex >= 0)
        {
            disconnectNode(WireNode::forPin(pin));
            return;
        }

        if (auto junction = hitTestJunction(position); junction >= 0)
        {
            disconnectNode(WireNode::forJunction(junction));
            return;
        }

        if (onStatus) onStatus("Nothing to disconnect here.");
    }

    void disconnectNode(WireNode node)
    {
        const auto before = wires.size();
        wires.erase(std::remove_if(wires.begin(), wires.end(), [&](const Wire& wire) {
            return sameNode(wire.a, node) || sameNode(wire.b, node);
        }), wires.end());

        const auto removed = before - wires.size();
        if (onStatus)
            onStatus(removed > 0 ? "Disconnected " + juce::String((int)removed) + " wire(s) from " + nodeLabel(node) + "."
                                 : "No wires connected to " + nodeLabel(node) + ".");
    }

    void assignProbe(const juce::String& id, WireNode node)
    {
        const auto label = probeLabel(id);
        if (label.isEmpty())
            return;

        const auto role = probeRole(id);
        const auto colour = probeColour(id);

        auto found = std::find_if(probes.begin(), probes.end(), [&](const Probe& probe) { return probe.id == id; });
        if (found != probes.end())
        {
            found->node = node;
            found->colour = colour;
        }
        else
        {
            probes.push_back({ id, label, role, node, colour });
        }

        if (onStatus) onStatus("Assigned " + label + " to " + nodeLabel(node) + ".");
        if (onProbeChanged) onProbeChanged(id, label, nodeLabel(node));
    }

    bool releaseProbeAt(juce::Point<float> position)
    {
        for (int i = (int)probes.size() - 1; i >= 0; --i)
        {
            const auto p = nodePosition(probes[(size_t)i].node);
            if (p.getDistanceFrom(position) > 16.0f)
                continue;

            const auto id = probes[(size_t)i].id;
            const auto label = probes[(size_t)i].label;
            probes.erase(probes.begin() + i);
            if (onStatus) onStatus("Removed " + label + " from schematic.");
            if (onProbeChanged) onProbeChanged(id, label, {});
            return true;
        }
        return false;
    }

    static juce::String probeLabel(const juce::String& id)
    {
        if (id == "DMM_HI") return "DMM+";
        if (id == "DMM_LO") return "DMM-";
        if (id == "SCOPE_CH1") return "CH1";
        if (id == "SCOPE_CH2") return "CH2";
        return {};
    }

    static juce::String probeRole(const juce::String& id)
    {
        if (id == "DMM_HI") return "dmm.high";
        if (id == "DMM_LO") return "dmm.low";
        if (id == "SCOPE_CH1") return "scope.channel1";
        if (id == "SCOPE_CH2") return "scope.channel2";
        return {};
    }

    static juce::Colour probeColour(const juce::String& id)
    {
        if (id == "DMM_HI") return juce::Colour(0xffffc857);
        if (id == "DMM_LO") return juce::Colour(0xff93a7b0);
        if (id == "SCOPE_CH1") return juce::Colour(0xff78dcca);
        if (id == "SCOPE_CH2") return juce::Colour(0xffff6b6b);
        return juce::Colour(0xffdce9ee);
    }

    WireNode createJunctionOnWire(int wireIndex, juce::Point<float> position)
    {
        const auto junctionIndex = (int)junctions.size();
        junctions.push_back(position);
        const auto junction = WireNode::forJunction(junctionIndex);

        if (wireIndex >= 0 && wireIndex < (int)wires.size())
        {
            const auto existing = wires[(size_t)wireIndex];
            wires.erase(wires.begin() + wireIndex);
            wires.push_back({ existing.a, junction });
            wires.push_back({ junction, existing.b });
        }

        if (onStatus) onStatus("Added junction " + nodeLabel(junction) + ".");
        return junction;
    }

    WireNode createRailTap(int instanceIndex, juce::Point<float> position)
    {
        if (instanceIndex < 0 || instanceIndex >= (int)instances.size())
            return {};

        auto& instance = instances[(size_t)instanceIndex];
        const auto rail = railBounds(instance);
        position.x = std::clamp(position.x, rail.getX(), rail.getRight());
        position.y = instance.position.y;

        const auto junctionIndex = (int)junctions.size();
        junctions.push_back(position);
        const auto junction = WireNode::forJunction(junctionIndex);
        wires.push_back({ WireNode::forPin({ instanceIndex, 0 }), junction });

        if (onStatus) onStatus("Added tap on " + instance.refdes + " " + instance.busName + ".");
        return junction;
    }

    static bool nodeTouchesInstance(const WireNode& node, int instanceIndex)
    {
        return node.isPin() && node.pin.instanceIndex == instanceIndex;
    }

    static void adjustNodeAfterDeletingInstance(WireNode& node, int instanceIndex)
    {
        if (node.isPin() && node.pin.instanceIndex > instanceIndex)
            --node.pin.instanceIndex;
    }

    void deleteSelected()
    {
        if (selectedInstance < 0 || selectedInstance >= (int)instances.size())
        {
            if (onStatus) onStatus("Nothing selected to delete.");
            return;
        }

        const auto removedName = instances[(size_t)selectedInstance].refdes;
        wires.erase(std::remove_if(wires.begin(), wires.end(), [&](const Wire& wire) {
            return nodeTouchesInstance(wire.a, selectedInstance) || nodeTouchesInstance(wire.b, selectedInstance);
        }), wires.end());

        juce::StringArray returnedProbes;
        for (const auto& probe : probes)
            if (nodeTouchesInstance(probe.node, selectedInstance))
                returnedProbes.add(probe.id);
        probes.erase(std::remove_if(probes.begin(), probes.end(), [&](const Probe& probe) {
            return nodeTouchesInstance(probe.node, selectedInstance);
        }), probes.end());

        instances.erase(instances.begin() + selectedInstance);
        for (auto& wire : wires)
        {
            adjustNodeAfterDeletingInstance(wire.a, selectedInstance);
            adjustNodeAfterDeletingInstance(wire.b, selectedInstance);
        }
        for (auto& probe : probes)
            adjustNodeAfterDeletingInstance(probe.node, selectedInstance);

        selectedInstance = -1;
        notifySelection();
        for (const auto& probeId : returnedProbes)
            if (onProbeChanged) onProbeChanged(probeId, {}, {});
        if (onStatus) onStatus("Deleted " + removedName + ".");
        repaint();
    }

    void notifySelection()
    {
        if (onSelectionChanged == nullptr)
            return;

        if (selectedInstance < 0 || selectedInstance >= (int)instances.size())
        {
            onSelectionChanged(-1, {}, {}, {}, {}, {}, {}, {});
            return;
        }

        const auto& instance = instances[(size_t)selectedInstance];
        onSelectionChanged(selectedInstance, instance.refdes, instance.symbolId, instance.value,
                           instance.frequency, instance.busName, instance.family, instance.manufacturerPart);
    }

    void placeSymbol(const juce::String& symbolId, juce::Point<float> p)
    {
        const auto symbol = symbolFor(symbolId);
        const auto prefix = symbolId == "ground" ? juce::String("GND") :
                            symbolId == "ground_bus" ? juce::String("GBUS") :
                            symbolId == "power_bus" ? juce::String("PBUS") :
                            symbolId == "battery" ? juce::String("BAT") :
                            symbolId == "voltage_source" ? juce::String("V") :
                            symbolId == "ac_voltage_source" ? juce::String("VAC") :
                            symbolId == "signal_source" ? juce::String("SIG") :
                            symbolId == "capacitor" ? juce::String("C") :
                            symbolId == "inductor" ? juce::String("L") :
                            symbolId == "diode" ? juce::String("D") :
                            symbolId == "resistor" ? juce::String("R") :
                            symbolId == "opamp_741" ? juce::String("U") :
                            symbolId == "npn" ? juce::String("Q") :
                            juce::String("U");
        instances.push_back({ symbol.id,
                              prefix + juce::String(nextRef++),
                              defaultValueFor(symbol.id),
                              defaultFrequencyFor(symbol.id),
                              defaultBusNameFor(symbol.id),
                              familyFor(symbol.id),
                              {},
                              p,
                              0,
                              isRailBus(symbol.id) ? 420.0f : 0.0f });
        selectedInstance = (int)instances.size() - 1;
        notifySelection();
        if (onStatus) onStatus("Placed " + symbol.title + " at schematic grid.");
    }

    juce::String displayValueFor(const Instance& instance) const
    {
        if (instance.symbolId == "ground" || instance.symbolId == "ground_bus")
            return instance.busName;
        if (instance.symbolId == "power_bus")
            return instance.busName;
        if (instance.symbolId == "ac_voltage_source" || instance.symbolId == "signal_source")
            return instance.value + " @ " + instance.frequency;
        return instance.value;
    }

    void drawSymbolBody(juce::Graphics& g, const Instance& instance, const SymbolDef& symbol)
    {
        const auto selectionBounds = orientedBounds(instance, symbol).expanded(8.0f);
        if (selectedInstance >= 0 && selectedInstance < (int)instances.size()
            && &instance == &instances[(size_t)selectedInstance])
        {
            g.setColour(juce::Colour(0xffffc857));
            const auto r = selectionBounds;
            const auto s = 8.0f;
            g.drawLine(r.getX(), r.getY(), r.getX() + s, r.getY(), 1.5f);
            g.drawLine(r.getX(), r.getY(), r.getX(), r.getY() + s, 1.5f);
            g.drawLine(r.getRight(), r.getY(), r.getRight() - s, r.getY(), 1.5f);
            g.drawLine(r.getRight(), r.getY(), r.getRight(), r.getY() + s, 1.5f);
            g.drawLine(r.getX(), r.getBottom(), r.getX() + s, r.getBottom(), 1.5f);
            g.drawLine(r.getX(), r.getBottom(), r.getX(), r.getBottom() - s, 1.5f);
            g.drawLine(r.getRight(), r.getBottom(), r.getRight() - s, r.getBottom(), 1.5f);
            g.drawLine(r.getRight(), r.getBottom(), r.getRight(), r.getBottom() - s, 1.5f);

            if (isRailBus(instance.symbolId))
            {
                g.setColour(juce::Colour(0xff0e141a));
                g.fillRoundedRectangle(railHandleBounds(instance, true), 3.0f);
                g.fillRoundedRectangle(railHandleBounds(instance, false), 3.0f);
                g.setColour(juce::Colour(0xffffc857));
                g.drawRoundedRectangle(railHandleBounds(instance, true), 3.0f, 1.5f);
                g.drawRoundedRectangle(railHandleBounds(instance, false), 3.0f, 1.5f);
            }
        }

        const auto body = symbol.bounds;
        g.saveState();
        g.addTransform(juce::AffineTransform::rotation(juce::degreesToRadians((float)instance.rotation))
                           .translated(instance.position.x, instance.position.y));

        if (symbol.id == "opamp_741")
        {
            juce::Path tri;
            tri.startNewSubPath(body.getX(), body.getY());
            tri.lineTo(body.getX(), body.getBottom());
            tri.lineTo(body.getRight(), body.getCentreY());
            tri.closeSubPath();
            g.setColour(juce::Colour(0xff78dcca));
            g.strokePath(tri, juce::PathStrokeType(1.8f));
            g.setColour(juce::Colour(0xffe8f1f2));
            g.drawLine(-72.0f, -20.0f, body.getX(), -20.0f, 1.8f);
            g.drawLine(-72.0f, 20.0f, body.getX(), 20.0f, 1.8f);
            g.drawLine(body.getRight(), 0.0f, 72.0f, 0.0f, 1.8f);
            g.drawLine(0.0f, -62.0f, 0.0f, body.getY(), 1.8f);
            g.drawLine(0.0f, body.getBottom(), 0.0f, 62.0f, 1.8f);
            g.setFont(juce::Font(14.0f, juce::Font::bold));
            g.drawText("+", -48, -30, 18, 18, juce::Justification::centred);
            g.drawText("-", -48, 12, 18, 18, juce::Justification::centred);
            g.setColour(juce::Colour(0xffdce9ee));
            g.setFont(juce::Font(13.0f, juce::Font::bold));
            g.drawText("uA741", body.toNearestInt(), juce::Justification::centred);
        }
        else if (symbol.id == "resistor")
        {
            g.setColour(juce::Colour(0xffe8f1f2));
            juce::Path z;
            const auto cy = body.getCentreY();
            g.drawLine(-54.0f, 0.0f, body.getX(), 0.0f, 1.8f);
            z.startNewSubPath(body.getX(), cy);
            for (int i = 0; i < 6; ++i)
            {
                const auto x = body.getX() + (float)(i + 1) * body.getWidth() / 7.0f;
                z.lineTo(x, cy + (i % 2 == 0 ? -10.0f : 10.0f));
            }
            z.lineTo(body.getRight(), cy);
            g.strokePath(z, juce::PathStrokeType(1.8f));
            g.drawLine(body.getRight(), 0.0f, 54.0f, 0.0f, 1.8f);
        }
        else if (symbol.id == "capacitor")
        {
            g.setColour(juce::Colour(0xffe8f1f2));
            g.drawLine(-42.0f, 0.0f, -8.0f, 0.0f, 1.8f);
            g.drawLine(8.0f, 0.0f, 42.0f, 0.0f, 1.8f);
            g.drawVerticalLine((int)(body.getCentreX() - 6), body.getY(), body.getBottom());
            g.drawVerticalLine((int)(body.getCentreX() + 6), body.getY(), body.getBottom());
        }
        else if (symbol.id == "inductor")
        {
            g.setColour(juce::Colour(0xffe8f1f2));
            g.drawLine(-54.0f, 0.0f, -34.0f, 0.0f, 1.8f);
            g.drawLine(34.0f, 0.0f, 54.0f, 0.0f, 1.8f);
            juce::Path coils;
            coils.startNewSubPath(-34.0f, 0.0f);
            for (int i = 0; i < 4; ++i)
            {
                const auto x = -34.0f + (float)i * 17.0f;
                coils.cubicTo(x + 4.0f, -18.0f, x + 13.0f, -18.0f, x + 17.0f, 0.0f);
            }
            g.strokePath(coils, juce::PathStrokeType(1.8f));
        }
        else if (symbol.id == "diode")
        {
            g.setColour(juce::Colour(0xffe8f1f2));
            g.drawLine(-54.0f, 0.0f, -28.0f, 0.0f, 1.8f);
            g.drawLine(28.0f, 0.0f, 54.0f, 0.0f, 1.8f);
            juce::Path tri;
            tri.startNewSubPath(-28.0f, -20.0f);
            tri.lineTo(-28.0f, 20.0f);
            tri.lineTo(18.0f, 0.0f);
            tri.closeSubPath();
            g.strokePath(tri, juce::PathStrokeType(1.8f));
            g.drawLine(22.0f, -22.0f, 22.0f, 22.0f, 1.8f);
        }
        else if (symbol.id == "power_bus")
        {
            g.setColour(juce::Colour(0xffffc857));
            const auto length = std::max(120.0f, instance.busLength);
            g.drawLine(-length * 0.5f, 0.0f, length * 0.5f, 0.0f, 3.0f);
            for (float x = -length * 0.5f; x <= length * 0.5f + 0.1f; x += 48.0f)
            {
                g.drawLine(x, -7.0f, x, 7.0f, 1.2f);
            }
            g.setFont(juce::Font(12.0f, juce::Font::bold));
            g.drawText(instance.busName.isNotEmpty() ? instance.busName : "PWR",
                       juce::Rectangle<float>(-length * 0.5f, -24.0f, length, 18.0f).toNearestInt(),
                       juce::Justification::centredLeft);
        }
        else if (symbol.id == "ground_bus")
        {
            g.setColour(juce::Colour(0xff78dcca));
            const auto length = std::max(120.0f, instance.busLength);
            g.drawLine(-length * 0.5f, 0.0f, length * 0.5f, 0.0f, 3.0f);
            for (float x = -length * 0.5f; x <= length * 0.5f + 0.1f; x += 48.0f)
            {
                g.drawLine(x, -7.0f, x, 7.0f, 1.2f);
            }
            g.setFont(juce::Font(12.0f, juce::Font::bold));
            g.drawText(instance.busName.isNotEmpty() ? instance.busName : "0",
                       juce::Rectangle<float>(-length * 0.5f, 6.0f, length, 18.0f).toNearestInt(),
                       juce::Justification::centredLeft);
        }
        else if (symbol.id == "ground")
        {
            g.setColour(juce::Colour(0xffe8f1f2));
            const auto cx = body.getCentreX();
            const auto leadTop = -24.0f;
            g.drawLine(cx, leadTop, cx, body.getY() + 10, 2.0f);
            g.drawLine(cx - 22, body.getY() + 10, cx + 22, body.getY() + 10, 2.0f);
            g.drawLine(cx - 14, body.getY() + 19, cx + 14, body.getY() + 19, 2.0f);
            g.drawLine(cx - 6, body.getY() + 28, cx + 6, body.getY() + 28, 2.0f);
        }
        else if (symbol.id == "voltage_source" || symbol.id == "battery" || symbol.id == "ac_voltage_source" || symbol.id == "signal_source")
        {
            g.setColour(juce::Colour(0xffe8f1f2));
            if (symbol.id == "signal_source")
            {
                g.drawLine(-58.0f, 0.0f, body.getX(), 0.0f, 1.8f);
                g.drawLine(body.getRight(), 0.0f, 58.0f, 0.0f, 1.8f);
            }
            else
            {
                const auto topPin = symbol.id == "battery" ? -52.0f : symbol.id == "ac_voltage_source" ? -46.0f : -42.0f;
                const auto bottomPin = -topPin;
                g.drawLine(0.0f, topPin, 0.0f, body.getY(), 1.8f);
                g.drawLine(0.0f, body.getBottom(), 0.0f, bottomPin, 1.8f);
            }
            if (symbol.id == "battery")
            {
                g.drawLine(-14.0f, -10.0f, 14.0f, -10.0f, 2.0f);
                g.drawLine(-8.0f, 10.0f, 8.0f, 10.0f, 2.0f);
                g.setFont(juce::Font(12.0f, juce::Font::bold));
                g.drawText("+", 10, -30, 18, 18, juce::Justification::centred);
            }
            else if (symbol.id == "ac_voltage_source" || symbol.id == "signal_source")
            {
                g.drawEllipse(body, 2.0f);
                juce::Path wave;
                const auto cy = body.getCentreY();
                for (int i = 0; i <= 24; ++i)
                {
                    const auto t = (float)i / 24.0f;
                    const auto x = body.getX() + 8.0f + t * (body.getWidth() - 16.0f);
                    const auto y = cy + std::sin(t * juce::MathConstants<float>::twoPi) * 8.0f;
                    if (i == 0) wave.startNewSubPath(x, y); else wave.lineTo(x, y);
                }
                g.strokePath(wave, juce::PathStrokeType(1.6f));
            }
            else
            {
                g.drawEllipse(body, 2.0f);
                g.drawText("+", body.withHeight(22.0f).toNearestInt(), juce::Justification::centred);
            }
            g.setFont(juce::Font(12.0f, juce::Font::bold));
        }
        else if (symbol.id == "npn")
        {
            g.setColour(juce::Colour(0xffe8f1f2));
            g.drawLine(-54.0f, 0.0f, -10.0f, 0.0f, 1.8f);
            g.drawLine(-10.0f, -24.0f, -10.0f, 24.0f, 1.8f);
            g.drawLine(-10.0f, -12.0f, 28.0f, -48.0f, 1.8f);
            g.drawLine(-10.0f, 12.0f, 28.0f, 48.0f, 1.8f);
            juce::Path arrow;
            arrow.startNewSubPath(20.0f, 38.0f);
            arrow.lineTo(28.0f, 48.0f);
            arrow.lineTo(15.0f, 46.0f);
            g.strokePath(arrow, juce::PathStrokeType(1.8f));
        }
        else if (symbol.id == "logic_not")
        {
            g.setColour(juce::Colour(0xffe8f1f2));
            g.drawLine(-66.0f, 0.0f, body.getX(), 0.0f, 1.8f);
            juce::Path tri;
            tri.startNewSubPath(body.getX(), body.getY());
            tri.lineTo(body.getX(), body.getBottom());
            tri.lineTo(body.getRight() - 12.0f, 0.0f);
            tri.closeSubPath();
            g.strokePath(tri, juce::PathStrokeType(1.8f));
            g.drawEllipse(body.getRight() - 12.0f, -6.0f, 12.0f, 12.0f, 1.8f);
            g.drawLine(body.getRight(), 0.0f, 66.0f, 0.0f, 1.8f);
        }

        g.restoreState();

        g.setColour(juce::Colour(0xff93a7b0));
        g.setFont(juce::Font(12.0f));
        g.drawText(instance.refdes,
                   (int)selectionBounds.getX(),
                   (int)selectionBounds.getY() - 18,
                   (int)selectionBounds.getWidth(),
                   16,
                   juce::Justification::centred);
        const auto valueText = displayValueFor(instance);
        if (valueText.isNotEmpty())
        {
            g.setColour(juce::Colour(0xffdce9ee));
            g.drawText(valueText,
                       (int)selectionBounds.getX(),
                       (int)selectionBounds.getBottom() + 2,
                       (int)selectionBounds.getWidth(),
                       16,
                       juce::Justification::centred);
        }
    }

    void drawInstances(juce::Graphics& g)
    {
        for (int instanceIndex = 0; instanceIndex < (int)instances.size(); ++instanceIndex)
        {
            const auto& instance = instances[(size_t)instanceIndex];
            const auto symbol = symbolFor(instance.symbolId);
            drawSymbolBody(g, instance, symbol);

            if (isRailBus(instance.symbolId))
                continue;

            for (size_t i = 0; i < symbol.pins.size(); ++i)
            {
                const auto pin = pinPosition({ instanceIndex, (int)i });
                g.setColour(juce::Colour(0xffe8f1f2));
                g.fillEllipse(pin.x - 4, pin.y - 4, 8, 8);
                g.setColour(juce::Colour(0xff93a7b0));
                g.setFont(juce::Font(11.0f));
                g.drawText(symbol.pins[i].name, (int)pin.x - 24, (int)pin.y - 18, 48, 14, juce::Justification::centred);
            }
        }
    }

    void drawRightAngleWire(juce::Graphics& g, juce::Point<float> a, juce::Point<float> b, juce::Colour colour, float width)
    {
        const auto midX = std::round((a.x + b.x) * 0.5f / 24.0f) * 24.0f;
        juce::Path path;
        path.startNewSubPath(a);
        path.lineTo(midX, a.y);
        path.lineTo(midX, b.y);
        path.lineTo(b);
        g.setColour(colour);
        g.strokePath(path, juce::PathStrokeType(width));
    }

    void drawRoutedWire(juce::Graphics& g, const std::vector<juce::Point<float>>& points, juce::Colour colour, float width)
    {
        if (points.size() < 2)
            return;

        juce::Path path;
        path.startNewSubPath(points.front());
        for (size_t i = 1; i < points.size(); ++i)
            path.lineTo(points[i]);

        g.setColour(colour);
        g.strokePath(path, juce::PathStrokeType(width));
    }

    void drawWires(juce::Graphics& g)
    {
        for (const auto& wire : wires)
        {
            if (isInternalRailTapWire(wire))
                continue;

            drawRoutedWire(g, routedWirePoints(wire.a, wire.b), juce::Colour(0xfff4d35e), 2.0f);
        }

        g.setColour(juce::Colour(0xffffc857));
        for (const auto& junction : junctions)
            g.fillEllipse(junction.x - 4.0f, junction.y - 4.0f, 8.0f, 8.0f);
    }

    void drawProbes(juce::Graphics& g)
    {
        for (const auto& probe : probes)
        {
            const auto p = nodePosition(probe.node);
            if (p == juce::Point<float>())
                continue;

            g.setColour(probe.colour.withAlpha(0.24f));
            g.fillEllipse(p.x - 13.0f, p.y - 13.0f, 26.0f, 26.0f);
            g.setColour(probe.colour);
            g.drawEllipse(p.x - 10.0f, p.y - 10.0f, 20.0f, 20.0f, 2.0f);
            g.fillEllipse(p.x - 3.0f, p.y - 3.0f, 6.0f, 6.0f);

            auto label = juce::Rectangle<int>((int)p.x + 10, (int)p.y - 20, 64, 18);
            g.setColour(juce::Colour(0xdd0e141a));
            g.fillRoundedRectangle(label.toFloat(), 3.0f);
            g.setColour(probe.colour);
            g.setFont(juce::Font(11.0f, juce::Font::bold));
            g.drawText(probe.label, label.reduced(4, 0), juce::Justification::centredLeft);
        }
    }

    void drawPendingWire(juce::Graphics& g)
    {
        if (!wireDragging || !wireDragStart.isValid()) return;
        drawRoutedWire(g,
                       routedWirePoints(wireDragStart, wireDragPosition, {}),
                       juce::Colour(0x99f4d35e),
                       1.5f);
    }

    /*
    void drawOpAmp(juce::Graphics& g, juce::Rectangle<float> r)
    {
        juce::Path tri;
        tri.startNewSubPath(r.getX(), r.getY());
        tri.lineTo(r.getX(), r.getBottom());
        tri.lineTo(r.getRight(), r.getCentreY());
        tri.closeSubPath();
        g.setColour(juce::Colour(0xff17212b));
        g.fillPath(tri);
        g.setColour(juce::Colour(0xff78dcca));
        g.strokePath(tri, juce::PathStrokeType(2.0f));
        g.setFont(juce::Font(13.0f, juce::Font::bold));
        g.drawText("uA741", r.toNearestInt(), juce::Justification::centred);
        g.drawText("+", (int)r.getX() - 22, (int)r.getY() + 18, 18, 18, juce::Justification::centred);
        g.drawText("-", (int)r.getX() - 22, (int)r.getBottom() - 38, 18, 18, juce::Justification::centred);
    }

    void drawSampleCircuit(juce::Graphics& g)
    {
        const auto c = getLocalBounds().getCentre();
        juce::Point<float> op { (float)c.x - 30.0f, (float)c.y - 55.0f };
        drawOpAmp(g, { op.x, op.y, 130.0f, 110.0f });

        g.setColour(juce::Colour(0xffe8f1f2));
        g.drawLine(op.x - 120.0f, op.y + 28.0f, op.x, op.y + 28.0f, 2.0f);
        g.drawLine(op.x - 120.0f, op.y + 82.0f, op.x, op.y + 82.0f, 2.0f);
        g.drawLine(op.x + 130.0f, op.y + 55.0f, op.x + 245.0f, op.y + 55.0f, 2.0f);

        g.setFont(juce::Font(14.0f, juce::Font::bold));
        g.drawText("IN+", (int)op.x - 170, (int)op.y + 16, 48, 22, juce::Justification::centredRight);
        g.drawText("IN-", (int)op.x - 170, (int)op.y + 70, 48, 22, juce::Justification::centredRight);
        g.drawText("OUT", (int)op.x + 250, (int)op.y + 44, 56, 22, juce::Justification::centredLeft);
    }
    */
};

class InstrumentPanel final : public juce::Component
{
public:
    InstrumentPanel()
    {
        title.setText("Lab Bench", juce::dontSendNotification);
        title.setFont(juce::Font(16.0f, juce::Font::bold));
        title.setColour(juce::Label::textColourId, juce::Colour(0xff78dcca));
        addAndMakeVisible(title);

        psuTitle.setText("Programmable Power Supply", juce::dontSendNotification);
        psuTitle.setFont(juce::Font(14.0f, juce::Font::bold));
        psuTitle.setColour(juce::Label::textColourId, juce::Colour(0xffdce9ee));
        addAndMakeVisible(psuTitle);

        mode.addItem("DC", 1);
        mode.addItem("AC", 2);
        mode.setSelectedId(1, juce::dontSendNotification);
        mode.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff253341));
        mode.setColour(juce::ComboBox::textColourId, juce::Colour(0xffdce9ee));
        addAndMakeVisible(mode);

        for (auto* editor : { &positiveNet, &negativeNet, &voltage, &frequency, &currentLimit, &internalResistance })
        {
            styleTextEditor(*editor);
            editor->setMultiLine(false);
            addAndMakeVisible(*editor);
        }

        positiveNet.setText("+9V", juce::dontSendNotification);
        negativeNet.setText("0", juce::dontSendNotification);
        voltage.setText("9", juce::dontSendNotification);
        frequency.setText("60", juce::dontSendNotification);
        currentLimit.setText("100m", juce::dontSendNotification);
        internalResistance.setText("0.2", juce::dontSendNotification);

        positiveNet.setTextToShowWhenEmpty("+9V, +12V, +18V", juce::Colour(0xff71808c));
        negativeNet.setTextToShowWhenEmpty("0", juce::Colour(0xff71808c));
        voltage.setTextToShowWhenEmpty("9", juce::Colour(0xff71808c));
        frequency.setTextToShowWhenEmpty("60", juce::Colour(0xff71808c));
        currentLimit.setTextToShowWhenEmpty("100m", juce::Colour(0xff71808c));
        internalResistance.setTextToShowWhenEmpty("0.2", juce::Colour(0xff71808c));

        outputEnabled.setButtonText("Output enabled");
        outputEnabled.setToggleState(true, juce::dontSendNotification);
        outputEnabled.setColour(juce::ToggleButton::textColourId, juce::Colour(0xffdce9ee));
        addAndMakeVisible(outputEnabled);

        currentLimited.setButtonText("Current limit active");
        currentLimited.setToggleState(true, juce::dontSendNotification);
        currentLimited.setColour(juce::ToggleButton::textColourId, juce::Colour(0xffdce9ee));
        addAndMakeVisible(currentLimited);

        psuPreset.addItem("9V pedal supply", 1);
        psuPreset.addItem("12V pedal supply", 2);
        psuPreset.addItem("18V pedal supply", 3);
        psuPreset.addItem("Dying 9V battery", 4);
        psuPreset.setSelectedId(1, juce::dontSendNotification);
        psuPreset.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff253341));
        psuPreset.setColour(juce::ComboBox::textColourId, juce::Colour(0xffdce9ee));
        psuPreset.onChange = [this] { applySelectedPreset(); };
        addAndMakeVisible(psuPreset);

        dmmTitle.setText("Precision Digital Multimeter", juce::dontSendNotification);
        dmmTitle.setFont(juce::Font(14.0f, juce::Font::bold));
        dmmTitle.setColour(juce::Label::textColourId, juce::Colour(0xffdce9ee));
        addAndMakeVisible(dmmTitle);

        for (const auto& item : { "DC Voltage", "AC Voltage", "DC Current", "AC Current", "Resistance",
                                  "4-Wire Resistance", "Continuity", "Diode", "Capacitance", "Frequency",
                                  "Period", "Duty Cycle", "Temperature", "AC+DC Voltage", "AC+DC Current",
                                  "Ratio", "dB", "dBm", "Digitizer Voltage", "Digitizer Current" })
            dmmFunction.addItem(item, dmmFunction.getNumItems() + 1);
        dmmFunction.setSelectedId(1, juce::dontSendNotification);
        dmmFunction.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff253341));
        dmmFunction.setColour(juce::ComboBox::textColourId, juce::Colour(0xffdce9ee));
        addAndMakeVisible(dmmFunction);

        for (const auto& item : { "Auto", "100 mV", "1 V", "10 V", "100 V", "1000 V",
                                  "1 uA", "100 uA", "1 mA", "10 mA", "100 mA", "1 A", "10 A",
                                  "100 Ohm", "1 kOhm", "10 kOhm", "100 kOhm", "1 MOhm", "100 MOhm",
                                  "1 nF", "10 nF", "100 nF", "1 uF", "100 uF", "Hz" })
            dmmRange.addItem(item, dmmRange.getNumItems() + 1);
        dmmRange.setSelectedId(1, juce::dontSendNotification);
        dmmRange.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff253341));
        dmmRange.setColour(juce::ComboBox::textColourId, juce::Colour(0xffdce9ee));
        addAndMakeVisible(dmmRange);

        for (auto* editor : { &dmmHighNet, &dmmLowNet, &dmmNplc, &dmmSampleRate })
        {
            styleTextEditor(*editor);
            editor->setMultiLine(false);
            addAndMakeVisible(*editor);
        }
        dmmHighNet.setText("probe", juce::dontSendNotification);
        dmmLowNet.setText("bench", juce::dontSendNotification);
        dmmNplc.setText("10", juce::dontSendNotification);
        dmmSampleRate.setText("1000", juce::dontSendNotification);
        dmmHighNet.setReadOnly(true);
        dmmLowNet.setReadOnly(true);

        dmmDisplay.setText("+0.000000 V", juce::dontSendNotification);
        dmmDisplay.setFont(juce::Font(20.0f, juce::Font::bold));
        dmmDisplay.setJustificationType(juce::Justification::centredRight);
        dmmDisplay.setColour(juce::Label::textColourId, juce::Colour(0xff78dcca));
        dmmDisplay.setColour(juce::Label::backgroundColourId, juce::Colour(0xff0e141a));
        addAndMakeVisible(dmmDisplay);

        for (auto* toggle : { &dmmTrueRms, &dmmAutoRange, &dmmHold, &dmmRelative, &dmmMinMax,
                              &dmmPeakMinMax, &dmmLowPass, &dmmLoZ, &dmmContinuityBeep })
        {
            toggle->setColour(juce::ToggleButton::textColourId, juce::Colour(0xffdce9ee));
            addAndMakeVisible(*toggle);
        }
        dmmTrueRms.setToggleState(true, juce::dontSendNotification);
        dmmAutoRange.setToggleState(true, juce::dontSendNotification);
        dmmContinuityBeep.setToggleState(true, juce::dontSendNotification);
    }

    void setProbeTarget(const juce::String& id, const juce::String& target)
    {
        if (id == "DMM_HI")
            dmmHighNet.setText(target.isEmpty() ? "bench" : target, juce::dontSendNotification);
        else if (id == "DMM_LO")
            dmmLowNet.setText(target.isEmpty() ? "bench" : target, juce::dontSendNotification);
        else if (id == "SCOPE_CH1")
            scopeCh1Target = target;
        else if (id == "SCOPE_CH2")
            scopeCh2Target = target;

        repaint();
    }

    juce::String buildInstrumentJson() const
    {
        juce::String text;
        text << "{\n";
        text << "  \"schemaVersion\": 1,\n";
        text << "  \"kind\": \"djehuti_lab_instruments\",\n";
        text << "  \"instruments\": [\n";
        text << "    {\n";
        text << "      \"id\": \"PSU1\",\n";
        text << "      \"type\": \"programmable_power_supply\",\n";
        text << "      \"mode\": " << quote(mode.getText().toLowerCase()) << ",\n";
        text << "      \"channels\": [\n";
        text << "        {\n";
        text << "          \"name\": \"CH1\",\n";
        text << "          \"positiveNet\": " << quote(positiveNet.getText().trim()) << ",\n";
        text << "          \"negativeNet\": " << quote(negativeNet.getText().trim()) << ",\n";
        text << "          \"voltage\": " << quote(voltage.getText().trim() + "V") << ",\n";
        text << "          \"frequency\": " << quote(frequency.getText().trim() + "Hz") << ",\n";
        text << "          \"currentLimit\": " << quote(currentLimit.getText().trim() + "A") << ",\n";
        text << "          \"currentLimitEnabled\": " << (currentLimited.getToggleState() ? "true" : "false") << ",\n";
        text << "          \"internalResistance\": " << quote(internalResistance.getText().trim() + "ohm") << ",\n";
        text << "          \"enabled\": " << (outputEnabled.getToggleState() ? "true" : "false") << "\n";
        text << "        }\n";
        text << "      ]\n";
        text << "    },\n";
        text << "    {\n";
        text << "      \"id\": \"DMM1\",\n";
        text << "      \"type\": \"precision_digital_multimeter\",\n";
        text << "      \"function\": " << quote(dmmFunction.getText()) << ",\n";
        text << "      \"range\": " << quote(dmmRange.getText()) << ",\n";
        text << "      \"connections\": {\n";
        text << "        \"high\": " << quote(dmmHighNet.getText().trim()) << ",\n";
        text << "        \"low\": " << quote(dmmLowNet.getText().trim()) << "\n";
        text << "      },\n";
        text << "      \"features\": {\n";
        text << "        \"trueRms\": " << (dmmTrueRms.getToggleState() ? "true" : "false") << ",\n";
        text << "        \"autoRange\": " << (dmmAutoRange.getToggleState() ? "true" : "false") << ",\n";
        text << "        \"hold\": " << (dmmHold.getToggleState() ? "true" : "false") << ",\n";
        text << "        \"relative\": " << (dmmRelative.getToggleState() ? "true" : "false") << ",\n";
        text << "        \"minMaxRecording\": " << (dmmMinMax.getToggleState() ? "true" : "false") << ",\n";
        text << "        \"peakMinMax\": " << (dmmPeakMinMax.getToggleState() ? "true" : "false") << ",\n";
        text << "        \"lowPassFilter\": " << (dmmLowPass.getToggleState() ? "true" : "false") << ",\n";
        text << "        \"lowImpedanceMode\": " << (dmmLoZ.getToggleState() ? "true" : "false") << ",\n";
        text << "        \"continuityBeep\": " << (dmmContinuityBeep.getToggleState() ? "true" : "false") << "\n";
        text << "      },\n";
        text << "      \"acquisition\": {\n";
        text << "        \"nplc\": " << quote(dmmNplc.getText().trim()) << ",\n";
        text << "        \"sampleRate\": " << quote(dmmSampleRate.getText().trim() + "Sa/s") << ",\n";
        text << "        \"digitizer\": { \"enabled\": true, \"resolutionBits\": 16 },\n";
        text << "        \"statistics\": [\"min\", \"max\", \"average\", \"peakToPeak\", \"standardDeviation\"],\n";
        text << "        \"logging\": true,\n";
        text << "        \"graphing\": true,\n";
        text << "        \"displayViews\": [\"numeric\", \"trend\", \"histogram\", \"bar\", \"waveform\"]\n";
        text << "      }\n";
        text << "    },\n";
        text << "    {\n";
        text << "      \"id\": \"SCOPE1\",\n";
        text << "      \"type\": \"digital_oscilloscope\",\n";
        text << "      \"channels\": [\n";
        text << "        { \"name\": \"CH1\", \"target\": " << quote(scopeCh1Target.isEmpty() ? "bench" : scopeCh1Target) << " },\n";
        text << "        { \"name\": \"CH2\", \"target\": " << quote(scopeCh2Target.isEmpty() ? "bench" : scopeCh2Target) << " }\n";
        text << "      ],\n";
        text << "      \"display\": { \"view\": \"waveform\", \"grid\": true }\n";
        text << "    }\n";
        text << "  ]\n";
        text << "}\n";
        return text;
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff10161d));
        auto area = getLocalBounds().reduced(12);
        drawZone(g, psuZone, "Power Source");
        drawZone(g, dmmZone, "Meter Setup");
        drawZone(g, meterOptionsZone, "Meter Options");
        drawZone(g, scopeZone, "Scope Preview");

        g.setColour(juce::Colour(0xff93a7b0));
        g.setFont(juce::Font(11.5f, juce::Font::bold));
        g.drawText("Mode", 12, 64, 78, 16, juce::Justification::centredLeft);
        g.drawText("Preset", 380, 64, 150, 16, juce::Justification::centredLeft);
        g.drawText("+ net", 12, 104, 110, 16, juce::Justification::centredLeft);
        g.drawText("- net", 130, 104, 110, 16, juce::Justification::centredLeft);
        g.drawText("Volts", 12, 160, 92, 16, juce::Justification::centredLeft);
        g.drawText("Freq", 112, 160, 92, 16, juce::Justification::centredLeft);
        g.drawText("Limit", 212, 160, 92, 16, juce::Justification::centredLeft);
        g.drawText("Internal R", 312, 160, 92, 16, juce::Justification::centredLeft);
        g.drawText("Function", 12, 240, 150, 16, juce::Justification::centredLeft);
        g.drawText("Range", 170, 240, 86, 16, juce::Justification::centredLeft);
        g.drawText("Leads", 12, 280, 228, 16, juce::Justification::centredLeft);
        g.drawText("NPLC", 248, 280, 70, 16, juce::Justification::centredLeft);
        g.drawText("Sa/s", 326, 280, 90, 16, juce::Justification::centredLeft);

        drawProbeLead(g, dmmHiLead, "DMM+", juce::Colour(0xffffc857), dmmHighNet.getText().trim());
        drawProbeLead(g, dmmLoLead, "DMM-", juce::Colour(0xff93a7b0), dmmLowNet.getText().trim());
        drawProbeLead(g, scopeCh1Lead, "CH1", juce::Colour(0xff78dcca), scopeCh1Target);
        drawProbeLead(g, scopeCh2Lead, "CH2", juce::Colour(0xffff6b6b), scopeCh2Target);

        area.removeFromTop(scopeZone.getY() - 12);
        g.setColour(juce::Colour(0xffdce9ee));
        g.setFont(juce::Font(15.0f, juce::Font::bold));
        g.drawText("Oscilloscope / Dataset Viewer", area.removeFromTop(24), juce::Justification::centredLeft);

        auto graph = area.reduced(0, 10).toFloat();
        g.setColour(juce::Colour(0xff26323d));
        g.drawRect(graph, 1.0f);
        for (int i = 1; i < 10; ++i)
        {
            const auto x = graph.getX() + graph.getWidth() * (float)i / 10.0f;
            const auto y = graph.getY() + graph.getHeight() * (float)i / 10.0f;
            g.drawVerticalLine((int)x, graph.getY(), graph.getBottom());
            g.drawHorizontalLine((int)y, graph.getX(), graph.getRight());
        }

        juce::Path wave;
        for (int i = 0; i < 360; ++i)
        {
            const auto t = (float)i / 359.0f;
            const auto x = graph.getX() + t * graph.getWidth();
            const auto y = graph.getCentreY() - std::sin(t * juce::MathConstants<float>::twoPi * 3.0f) * graph.getHeight() * 0.28f;
            if (i == 0) wave.startNewSubPath(x, y); else wave.lineTo(x, y);
        }
        g.setColour(juce::Colour(0xff78dcca));
        g.strokePath(wave, juce::PathStrokeType(2.0f));
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        const auto p = event.getPosition();
        const auto probeId = probeAt(p);
        if (probeId.isEmpty())
            return;

        if (probeIsInUse(probeId))
            return;

        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this))
        {
            showCursorForEvent(event, juce::MouseCursor::DraggingHandCursor);
            container->startDragging("probe:" + probeId, this);
        }
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        showCursorForEvent(event, juce::MouseCursor::NormalCursor);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(12);
        title.setBounds(area.removeFromTop(24));
        area.removeFromTop(6);
        psuTitle.setBounds(area.removeFromTop(22));
        psuZone = juce::Rectangle<int>(8, 44, getWidth() - 16, 166);

        auto topRow = area.removeFromTop(28);
        addField(topRow, mode, 78);
        topRow.removeFromLeft(8);
        outputEnabled.setBounds(topRow.removeFromLeft(140));
        currentLimited.setBounds(topRow.removeFromLeft(150));
        topRow.removeFromLeft(8);
        psuPreset.setBounds(topRow.removeFromLeft(170));

        area.removeFromTop(8);
        auto nets = area.removeFromTop(48);
        layoutEditor(nets, "Positive net", positiveNet);
        nets.removeFromLeft(8);
        layoutEditor(nets, "Negative net", negativeNet);

        area.removeFromTop(8);
        auto electrical = area.removeFromTop(48);
        layoutEditor(electrical, "Voltage", voltage);
        electrical.removeFromLeft(8);
        layoutEditor(electrical, "Frequency", frequency);
        electrical.removeFromLeft(8);
        layoutEditor(electrical, "Current limit", currentLimit);
        electrical.removeFromLeft(8);
        layoutEditor(electrical, "Internal R", internalResistance);

        area.removeFromTop(8);
        area.removeFromTop(18);
        dmmZone = juce::Rectangle<int>(8, area.getY() - 8, getWidth() - 16, 112);
        dmmTitle.setBounds(area.removeFromTop(22));
        auto dmmTop = area.removeFromTop(32);
        dmmFunction.setBounds(dmmTop.removeFromLeft(150));
        dmmTop.removeFromLeft(8);
        dmmRange.setBounds(dmmTop.removeFromLeft(86));
        dmmTop.removeFromLeft(8);
        dmmDisplay.setBounds(dmmTop.removeFromLeft(190));

        area.removeFromTop(8);
        auto dmmNets = area.removeFromTop(32);
        dmmHiLead = dmmNets.removeFromLeft(52);
        dmmNets.removeFromLeft(6);
        dmmLoLead = dmmNets.removeFromLeft(52);
        dmmNets.removeFromLeft(8);
        dmmHighNet.setBounds(dmmNets.removeFromLeft(110));
        dmmNets.removeFromLeft(8);
        dmmLowNet.setBounds(dmmNets.removeFromLeft(110));
        dmmNets.removeFromLeft(8);
        dmmNplc.setBounds(dmmNets.removeFromLeft(70));
        dmmNets.removeFromLeft(8);
        dmmSampleRate.setBounds(dmmNets.removeFromLeft(90));

        area.removeFromTop(12);
        meterOptionsZone = juce::Rectangle<int>(8, area.getY() - 6, getWidth() - 16, 48);
        auto toggles = area.removeFromTop(58);
        dmmTrueRms.setBounds(toggles.removeFromLeft(96));
        dmmAutoRange.setBounds(toggles.removeFromLeft(104));
        dmmHold.setBounds(toggles.removeFromLeft(72));
        dmmRelative.setBounds(toggles.removeFromLeft(82));
        dmmMinMax.setBounds(toggles.removeFromLeft(88));
        dmmPeakMinMax.setBounds(toggles.removeFromLeft(92));
        dmmLowPass.setBounds(toggles.removeFromLeft(92));
        dmmLoZ.setBounds(toggles.removeFromLeft(72));
        dmmContinuityBeep.setBounds(toggles.removeFromLeft(80));
        area.removeFromTop(10);
        scopeZone = juce::Rectangle<int>(8, area.getY(), getWidth() - 16, getHeight() - area.getY() - 8);
        auto scopeLeads = scopeZone.reduced(10).removeFromTop(34).removeFromRight(126);
        scopeCh1Lead = scopeLeads.removeFromLeft(58);
        scopeLeads.removeFromLeft(10);
        scopeCh2Lead = scopeLeads.removeFromLeft(58);
    }

private:
    static juce::String quote(const juce::String& text)
    {
        return "\"" + text.replace("\\", "\\\\").replace("\"", "\\\"") + "\"";
    }

    static void addField(juce::Rectangle<int>& area, juce::Component& component, int width)
    {
        component.setBounds(area.removeFromLeft(width));
    }

    static void layoutEditor(juce::Rectangle<int>& area, const juce::String& label, juce::TextEditor& editor)
    {
        auto column = area.removeFromLeft(std::max(92, area.getWidth() / 4));
        juce::ignoreUnused(label);
        editor.setBounds(column.removeFromBottom(28));
    }

    static void drawZone(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& label)
    {
        if (area.isEmpty())
            return;

        auto r = area.toFloat();
        g.setColour(juce::Colour(0xff121a22));
        g.fillRoundedRectangle(r, 5.0f);
        g.setColour(juce::Colour(0xff31404b));
        g.drawRoundedRectangle(r, 5.0f, 1.0f);
        g.setColour(juce::Colour(0xff78dcca));
        g.setFont(juce::Font(11.5f, juce::Font::bold));
        g.drawText(label, area.reduced(8, 2).removeFromTop(16), juce::Justification::centredLeft);
    }

    static juce::Colour disabledProbeColour(juce::Colour colour)
    {
        return colour.withAlpha(0.22f);
    }

    void drawProbeLead(juce::Graphics& g,
                       juce::Rectangle<int> area,
                       const juce::String& label,
                       juce::Colour colour,
                       const juce::String& target)
    {
        if (area.isEmpty())
            return;

        const auto inUse = target.isNotEmpty() && target != "bench" && target != "probe";
        const auto activeColour = inUse ? disabledProbeColour(colour) : colour;
        auto body = area.toFloat().reduced(2.0f);
        g.setColour(juce::Colour(0xff0e141a));
        g.fillRoundedRectangle(body, 7.0f);
        g.setColour(activeColour);
        g.drawRoundedRectangle(body, 7.0f, 1.5f);

        const auto jack = juce::Rectangle<float>(body.getX() + 7.0f, body.getCentreY() - 5.0f, 10.0f, 10.0f);
        g.setColour(activeColour);
        g.drawEllipse(jack, 1.6f);
        if (!inUse)
            g.fillEllipse(jack.reduced(3.0f));

        g.setFont(juce::Font(11.0f, juce::Font::bold));
        g.drawText(inUse ? "out" : label, area.withTrimmedLeft(20), juce::Justification::centredLeft, true);
    }

    juce::String probeAt(juce::Point<int> p) const
    {
        if (dmmHiLead.contains(p)) return "DMM_HI";
        if (dmmLoLead.contains(p)) return "DMM_LO";
        if (scopeCh1Lead.contains(p)) return "SCOPE_CH1";
        if (scopeCh2Lead.contains(p)) return "SCOPE_CH2";
        return {};
    }

    bool probeIsInUse(const juce::String& id) const
    {
        if (id == "DMM_HI") return !isProbeHome(dmmHighNet.getText().trim());
        if (id == "DMM_LO") return !isProbeHome(dmmLowNet.getText().trim());
        if (id == "SCOPE_CH1") return scopeCh1Target.isNotEmpty();
        if (id == "SCOPE_CH2") return scopeCh2Target.isNotEmpty();
        return true;
    }

    static bool isProbeHome(const juce::String& target)
    {
        return target.isEmpty() || target == "bench" || target == "probe";
    }

    void applySelectedPreset()
    {
        switch (psuPreset.getSelectedId())
        {
            case 2: applyPreset("12", "100m", "0.15"); break;
            case 3: applyPreset("18", "100m", "0.15"); break;
            case 4: applyPreset("6.8", "35m", "25"); break;
            default: applyPreset("9", "100m", "0.2"); break;
        }
    }

    void applyPreset(const juce::String& volts, const juce::String& amps, const juce::String& resistance)
    {
        mode.setSelectedId(1, juce::dontSendNotification);
        positiveNet.setText("+" + volts + "V", juce::dontSendNotification);
        negativeNet.setText("0", juce::dontSendNotification);
        voltage.setText(volts, juce::dontSendNotification);
        currentLimit.setText(amps, juce::dontSendNotification);
        internalResistance.setText(resistance, juce::dontSendNotification);
    }

    juce::Label title;
    juce::Label psuTitle;
    juce::ComboBox mode;
    juce::TextEditor positiveNet;
    juce::TextEditor negativeNet;
    juce::TextEditor voltage;
    juce::TextEditor frequency;
    juce::TextEditor currentLimit;
    juce::TextEditor internalResistance;
    juce::ToggleButton outputEnabled;
    juce::ToggleButton currentLimited;
    juce::ComboBox psuPreset;
    juce::Label dmmTitle;
    juce::ComboBox dmmFunction;
    juce::ComboBox dmmRange;
    juce::Label dmmDisplay;
    juce::TextEditor dmmHighNet;
    juce::TextEditor dmmLowNet;
    juce::TextEditor dmmNplc;
    juce::TextEditor dmmSampleRate;
    juce::ToggleButton dmmTrueRms { "True RMS" };
    juce::ToggleButton dmmAutoRange { "Auto" };
    juce::ToggleButton dmmHold { "Hold" };
    juce::ToggleButton dmmRelative { "Rel" };
    juce::ToggleButton dmmMinMax { "Min/Max" };
    juce::ToggleButton dmmPeakMinMax { "Peak" };
    juce::ToggleButton dmmLowPass { "LPF" };
    juce::ToggleButton dmmLoZ { "LoZ" };
    juce::ToggleButton dmmContinuityBeep { "Beep" };
    juce::String scopeCh1Target;
    juce::String scopeCh2Target;
    juce::Rectangle<int> dmmHiLead;
    juce::Rectangle<int> dmmLoLead;
    juce::Rectangle<int> scopeCh1Lead;
    juce::Rectangle<int> scopeCh2Lead;
    juce::Rectangle<int> psuZone;
    juce::Rectangle<int> dmmZone;
    juce::Rectangle<int> meterOptionsZone;
    juce::Rectangle<int> scopeZone;
};

class ConsolePanel final : public juce::Component
{
public:
    ConsolePanel(juce::TextEditor*& externalLog)
    {
        styleTextEditor(console, true);
        console.setReadOnly(false);
        console.setText("// Embedded Frust math console research stub\n"
                        "// Future: circuit API, datasets, FFT, solvers, plots.\n\n"
                        "> ");
        externalLog = &console;
        addAndMakeVisible(console);
    }

    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff10161d)); }
    void resized() override { console.setBounds(getLocalBounds().reduced(6)); }

private:
    juce::TextEditor console;
};

class PropertiesPanel final : public juce::Component
{
public:
    PropertiesPanel()
    {
        title.setText("Properties", juce::dontSendNotification);
        title.setFont(juce::Font(16.0f, juce::Font::bold));
        title.setColour(juce::Label::textColourId, juce::Colour(0xff78dcca));
        addAndMakeVisible(title);

        for (auto* label : { &selectedLabel, &valueLabel, &frequencyLabel, &busLabel, &familyLabel, &manufacturerLabel })
        {
            label->setFont(juce::Font(12.5f, juce::Font::bold));
            label->setColour(juce::Label::textColourId, juce::Colour(0xff93a7b0));
            addAndMakeVisible(*label);
        }

        selectedLabel.setText("Selected", juce::dontSendNotification);
        valueLabel.setText("Value / amplitude", juce::dontSendNotification);
        frequencyLabel.setText("Frequency", juce::dontSendNotification);
        busLabel.setText("Bus / net name", juce::dontSendNotification);
        familyLabel.setText("Part family", juce::dontSendNotification);
        manufacturerLabel.setText("Manufacturer part", juce::dontSendNotification);

        for (auto* editor : { &selected, &value, &frequency, &busName, &family, &manufacturerPart })
        {
            styleTextEditor(*editor);
            editor->setMultiLine(false);
            addAndMakeVisible(*editor);
        }

        selected.setReadOnly(true);
        selected.setTextToShowWhenEmpty("Select a placed component", juce::Colour(0xff71808c));
        value.setTextToShowWhenEmpty("10k, 1u, 9, 1...", juce::Colour(0xff71808c));
        frequency.setTextToShowWhenEmpty("1k", juce::Colour(0xff71808c));
        busName.setTextToShowWhenEmpty("+5V, +12V, VREF, 0", juce::Colour(0xff71808c));
        family.setTextToShowWhenEmpty("2N2222, LM741, NE555...", juce::Colour(0xff71808c));
        manufacturerPart.setTextToShowWhenEmpty("vendor-specific MPN", juce::Colour(0xff71808c));

        apply.setButtonText("Apply");
        apply.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff253341));
        apply.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffdce9ee));
        apply.onClick = [this] {
            if (onApply != nullptr && selectedIndex >= 0)
                onApply(value.getText(), frequency.getText(), busName.getText(), family.getText(), manufacturerPart.getText());
        };
        addAndMakeVisible(apply);

        rotate.setButtonText("Rotate 90");
        rotate.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff253341));
        rotate.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffdce9ee));
        rotate.onClick = [this] {
            if (onRotate != nullptr && selectedIndex >= 0)
                onRotate();
        };
        addAndMakeVisible(rotate);

        setSelection(-1, {}, {}, {}, {}, {}, {}, {});
    }

    void setSelection(int index,
                      const juce::String& refdes,
                      const juce::String& symbol,
                      const juce::String& newValue,
                      const juce::String& newFrequency,
                      const juce::String& newBusName,
                      const juce::String& newFamily,
                      const juce::String& newManufacturerPart)
    {
        selectedIndex = index;
        const auto hasSelection = selectedIndex >= 0;
        selected.setText(hasSelection ? refdes + "  (" + symbol + ")" : juce::String(), juce::dontSendNotification);
        value.setText(newValue, juce::dontSendNotification);
        frequency.setText(newFrequency, juce::dontSendNotification);
        busName.setText(newBusName, juce::dontSendNotification);
        family.setText(newFamily, juce::dontSendNotification);
        manufacturerPart.setText(newManufacturerPart, juce::dontSendNotification);

        for (auto* editor : { &value, &frequency, &busName, &family, &manufacturerPart })
            editor->setEnabled(hasSelection);
        apply.setEnabled(hasSelection);
        rotate.setEnabled(hasSelection);
    }

    std::function<void(juce::String, juce::String, juce::String, juce::String, juce::String)> onApply;
    std::function<void()> onRotate;

    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff151a20)); }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8);
        title.setBounds(area.removeFromTop(24));
        area.removeFromTop(8);
        layoutRow(area, selectedLabel, selected);
        layoutRow(area, valueLabel, value);
        layoutRow(area, frequencyLabel, frequency);
        layoutRow(area, busLabel, busName);
        layoutRow(area, familyLabel, family);
        layoutRow(area, manufacturerLabel, manufacturerPart);
        area.removeFromTop(8);
        auto buttons = area.removeFromTop(30);
        apply.setBounds(buttons.removeFromLeft(90));
        buttons.removeFromLeft(8);
        rotate.setBounds(buttons.removeFromLeft(110));
    }

private:
    static void layoutRow(juce::Rectangle<int>& area, juce::Label& label, juce::TextEditor& editor)
    {
        label.setBounds(area.removeFromTop(18));
        editor.setBounds(area.removeFromTop(28));
        area.removeFromTop(8);
    }

    int selectedIndex = -1;
    juce::Label title;
    juce::Label selectedLabel;
    juce::Label valueLabel;
    juce::Label frequencyLabel;
    juce::Label busLabel;
    juce::Label familyLabel;
    juce::Label manufacturerLabel;
    juce::TextEditor selected;
    juce::TextEditor value;
    juce::TextEditor frequency;
    juce::TextEditor busName;
    juce::TextEditor family;
    juce::TextEditor manufacturerPart;
    juce::TextButton apply;
    juce::TextButton rotate;
};

class AgentPanel final : public NotesPanel
{
public:
    AgentPanel()
        : NotesPanel("BYOK Electronics Agent",
                     "Agent shell placeholder.\n\n"
                     "Planned responsibilities:\n"
                     "- component acquisition\n"
                     "- datasheet extraction\n"
                     "- circuit inspection\n"
                     "- simulation setup\n"
                     "- Frust console/script execution with approval\n"
                     "- LiteSemRAG cards and process memory")
    {
    }
};

class SimulationPanel final : public NotesPanel
{
public:
    SimulationPanel()
        : NotesPanel("Simulation Setup",
                     "Analysis modes:\n"
                     "- DC operating point\n"
                     "- DC sweep\n"
                     "- transient\n"
                     "- AC small signal\n"
                     "- compiled Frust realtime preview\n\n"
                     "Simulation output should become datasets consumed by instruments and the Frust console.")
    {
    }
};

class SpecIngestionPanel final : public NotesPanel
{
public:
    SpecIngestionPanel()
        : NotesPanel("Component Spec Ingestion",
                     "Workflow:\n"
                     "1. Search local component DB.\n"
                     "2. Query approved providers/manufacturer sources.\n"
                     "3. Fetch datasheet.\n"
                     "4. Extract pins/spec claims/packages/models.\n"
                     "5. Store provenance and confidence.\n"
                     "6. Ask for approval when ambiguous.")
    {
    }
};

class PartsSourcingPanel final : public NotesPanel
{
public:
    PartsSourcingPanel()
        : NotesPanel("Parts Sourcing",
                     "Find buyable parts without confusing marketplace listings with verified specifications.\n\n"
                     "Research targets:\n"
                     "- distributor listings for exact MPNs\n"
                     "- hobby suppliers and breadboard-friendly packages\n"
                     "- Amazon/eBay/AliExpress style consumer listings\n"
                     "- assortment kits and substitutes\n"
                     "- local user inventory\n\n"
                     "Every sourcing result should carry match confidence, source URL, timestamp, package notes, and warnings.")
    {
    }
};

} // namespace

ElectronicsWorkbench::ElectronicsWorkbench()
{
    menuBar = std::make_unique<juce::MenuBarComponent>(this);
    addAndMakeVisible(menuBar.get());

    titleLabel.setText("Djehuti Electronics Lab", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(17.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xffdce9ee));
    addAndMakeVisible(titleLabel);

    statusLabel.setText("Research shell ready", juce::dontSendNotification);
    statusLabel.setFont(juce::Font(13.0f));
    statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xff93a7b0));
    statusLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(statusLabel);

    for (auto* b : { &newButton, &ercButton, &transientButton, &compileButton })
    {
        b->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff253341));
        b->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffdce9ee));
        addAndMakeVisible(*b);
    }
    stampModeButton.setToggleState(false, juce::dontSendNotification);
    stampModeButton.setColour(juce::ToggleButton::textColourId, juce::Colour(0xffdce9ee));
    addAndMakeVisible(stampModeButton);

    newButton.onClick = [this] { resetResearchState(); };
    ercButton.onClick = [this] { runElectricalRuleCheck(); };
    transientButton.onClick = [this] { exportCircuitArtifacts(); };
    compileButton.onClick = [this] { appendLog("Compiled Frust preview stub: circuit IR -> Frust lowering pending."); };

    dockManager = std::make_unique<CreationDock::DockManager>(*this);
    addAndMakeVisible(*dockManager);

    dockManager->registerPanel("library", "Component Library",
                               std::make_unique<ComponentLibraryPanel>(
                                   [this](juce::String id) {
                                       selectedSymbolId = id;
                                       appendLog("Selected symbol: " + id + ". Click the schematic to place it, or drag it from the library.");
                                   }),
                               CreationDock::DockTargetZone::Left);
    auto schematic = std::make_unique<SchematicCanvasPanel>(
        [this] { return selectedSymbolId; },
        [this] { return stampModeButton.getToggleState(); },
        [this](juce::String message) { appendLog(message); });
    auto* schematicPanel = schematic.get();
    auto properties = std::make_unique<PropertiesPanel>();
    auto* propertiesPanel = properties.get();
    schematicPanel->setSelectionListener([propertiesPanel](int index,
                                                           juce::String refdes,
                                                           juce::String symbol,
                                                           juce::String value,
                                                           juce::String frequency,
                                                           juce::String busName,
                                                           juce::String family,
                                                           juce::String manufacturerPart) {
        propertiesPanel->setSelection(index, refdes, symbol, value, frequency, busName, family, manufacturerPart);
    });
    propertiesPanel->onApply = [schematicPanel](juce::String value,
                                                juce::String frequency,
                                                juce::String busName,
                                                juce::String family,
                                                juce::String manufacturerPart) {
        schematicPanel->updateSelectedProperties(value, frequency, busName, family, manufacturerPart);
    };
    propertiesPanel->onRotate = [schematicPanel] {
        schematicPanel->rotateSelected();
    };
    auto instruments = std::make_unique<InstrumentPanel>();
    auto* instrumentPanel = instruments.get();
    schematicPanel->setProbeListener([instrumentPanel](juce::String id, juce::String, juce::String target) {
        instrumentPanel->setProbeTarget(id, target);
    });
    resetCircuit = [panel = schematic.get()] { panel->clearCircuit(); };
    getCircuitJson = [panel = schematic.get()] { return panel->buildCircuitJson(); };
    getXyceNetlist = [panel = schematic.get()] { return panel->buildXyceNetlist(); };
    getLabInstrumentsJson = [instrumentPanel] { return instrumentPanel->buildInstrumentJson(); };
    getErcReport = [panel = schematic.get()] { return panel->buildErcReport(); };
    loadCircuitJson = [panel = schematic.get()](const juce::String& json, juce::String& error) {
        return panel->loadCircuitJson(json, error);
    };
    dockManager->registerPanel("schematic", "Schematic", std::move(schematic), CreationDock::DockTargetZone::CenterTab);
    dockManager->registerPanel("simulation", "Simulation", std::make_unique<SimulationPanel>(), CreationDock::DockTargetZone::CenterTab);
    dockManager->registerPanel("console", "Frust Math Console", std::make_unique<ConsolePanel>(logConsole), CreationDock::DockTargetZone::Bottom);
    dockManager->registerPanel("agent", "BYOK Agent", std::make_unique<AgentPanel>(), CreationDock::DockTargetZone::Right);
    dockManager->registerPanel("properties", "Properties", std::move(properties), CreationDock::DockTargetZone::Right);
    dockManager->registerPanel("ingestion", "Spec Ingestion", std::make_unique<SpecIngestionPanel>(), CreationDock::DockTargetZone::Right);
    dockManager->registerPanel("sourcing", "Parts Sourcing", std::make_unique<PartsSourcingPanel>(), CreationDock::DockTargetZone::Right);
    dockManager->registerPanel("lab_bench", "Lab Bench", std::move(instruments), CreationDock::DockTargetZone::Right);

    dockManager->loadLayoutFromFile(layoutFile());
    appendLog("Electronics research shell initialized.");
}

ElectronicsWorkbench::~ElectronicsWorkbench()
{
    if (dockManager != nullptr)
        dockManager->saveLayoutToFile(layoutFile());
    menuBar = nullptr;
}

void ElectronicsWorkbench::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff171b20));
    g.setColour(juce::Colour(0xff26323d));
    g.fillRect(0, menuHeight, getWidth(), toolbarHeight);
}

void ElectronicsWorkbench::resized()
{
    auto area = getLocalBounds();
    menuBar->setBounds(area.removeFromTop(menuHeight));

    auto toolbar = area.removeFromTop(toolbarHeight).reduced(8, 4);
    titleLabel.setBounds(toolbar.removeFromLeft(230));
    newButton.setBounds(toolbar.removeFromLeft(70));
    toolbar.removeFromLeft(6);
    ercButton.setBounds(toolbar.removeFromLeft(70));
    toolbar.removeFromLeft(6);
    transientButton.setBounds(toolbar.removeFromLeft(100));
    toolbar.removeFromLeft(6);
    compileButton.setBounds(toolbar.removeFromLeft(140));
    toolbar.removeFromLeft(10);
    stampModeButton.setBounds(toolbar.removeFromLeft(90));
    statusLabel.setBounds(toolbar);

    if (dockManager != nullptr)
        dockManager->setBounds(area);
}

juce::StringArray ElectronicsWorkbench::getMenuBarNames()
{
    return { "File", "Circuit", "Simulation", "Agent", "View", "Help" };
}

juce::PopupMenu ElectronicsWorkbench::getMenuForIndex(int, const juce::String& menuName)
{
    juce::PopupMenu menu;
    if (menuName == "File")
    {
        menu.addItem(newProject, "New Research Project");
        menu.addItem(openProject, "Open Project...");
        menu.addItem(saveProject, "Save Project");
    }
    else if (menuName == "Circuit")
    {
        menu.addItem(importComponent, "Import / Fetch Component Spec...");
        menu.addSeparator();
        menu.addItem(runErc, "Run ERC");
    }
    else if (menuName == "Simulation")
    {
        menu.addItem(runOperatingPoint, "Run Operating Point");
        menu.addItem(runTransient, "Run Transient");
        menu.addItem(runCompiledPreview, "Compile Realtime Preview");
    }
    else if (menuName == "Agent")
    {
        menu.addItem(openAgentSettings, "BYOK Agent Settings...");
    }
    else if (menuName == "View")
    {
        menu.addItem(resetLayout, "Reset Dock Layout");
    }
    else if (menuName == "Help")
    {
        menu.addItem(openResearchSpec, "Open Research Spec");
    }
    return menu;
}

void ElectronicsWorkbench::menuItemSelected(int menuItemID, int)
{
    switch (menuItemID)
    {
        case newProject: resetResearchState(); break;
        case saveProject: saveProjectFile(); break;
        case openProject: openProjectFile(); break;
        case resetLayout:
            if (dockManager != nullptr) dockManager->resetLayout();
            appendLog("Dock layout reset.");
            break;
        case importComponent: appendLog("Component ingestion stub: BYOK agent/provider workflow pending."); break;
        case runErc: runElectricalRuleCheck(); break;
        case runOperatingPoint: exportCircuitArtifacts(); break;
        case runTransient: exportCircuitArtifacts(); break;
        case runCompiledPreview: appendLog("Compiled preview stub: circuit IR -> Frust backend pending."); break;
        case openAgentSettings: appendLog("BYOK agent settings stub: provider/key/model UI pending."); break;
        case openResearchSpec: showSpecDocument(); break;
        default: break;
    }
}

juce::File ElectronicsWorkbench::layoutFile() const
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("DjehutiElectronicsLab")
        .getChildFile("layout.json");
}

juce::File ElectronicsWorkbench::savedProjectFile() const
{
    return juce::File::getSpecialLocation(juce::File::currentExecutableFile)
        .getParentDirectory()
        .getParentDirectory()
        .getParentDirectory()
        .getChildFile("projects")
        .getChildFile("current")
        .getChildFile("circuit.json");
}

juce::File ElectronicsWorkbench::generatedRunDirectory() const
{
    return juce::File::getSpecialLocation(juce::File::currentExecutableFile)
        .getParentDirectory()
        .getParentDirectory()
        .getParentDirectory()
        .getChildFile("sim")
        .getChildFile("xyce")
        .getChildFile("runs")
        .getChildFile("generated");
}

void ElectronicsWorkbench::appendLog(const juce::String& text)
{
    statusLabel.setText(text, juce::dontSendNotification);
    if (logConsole != nullptr)
        logConsole->insertTextAtCaret("\n// " + text + "\n> ");
}

void ElectronicsWorkbench::resetResearchState()
{
    if (resetCircuit != nullptr)
        resetCircuit();
    else
        appendLog("New electronics research project initialized.");
}

void ElectronicsWorkbench::saveProjectFile()
{
    if (getCircuitJson == nullptr)
    {
        appendLog("No schematic exporter is available.");
        return;
    }

    const auto file = savedProjectFile();
    if (!file.getParentDirectory().createDirectory())
    {
        appendLog("Could not create project directory: " + file.getParentDirectory().getFullPathName());
        return;
    }

    if (!file.replaceWithText(getCircuitJson()))
    {
        appendLog("Could not save project file: " + file.getFullPathName());
        return;
    }

    appendLog("Saved project circuit to " + file.getFullPathName());
}

void ElectronicsWorkbench::openProjectFile()
{
    if (loadCircuitJson == nullptr)
    {
        appendLog("No project loader is available.");
        return;
    }

    const auto file = savedProjectFile();
    if (!file.existsAsFile())
    {
        appendLog("No saved project found yet: " + file.getFullPathName());
        return;
    }

    juce::String error;
    if (!loadCircuitJson(file.loadFileAsString(), error))
    {
        appendLog("Could not open project: " + error);
        return;
    }

    appendLog("Opened project circuit from " + file.getFullPathName());
}

void ElectronicsWorkbench::runElectricalRuleCheck()
{
    if (getErcReport == nullptr)
    {
        appendLog("No ERC engine is available.");
        return;
    }

    const auto runDir = generatedRunDirectory();
    if (!runDir.createDirectory())
    {
        appendLog("Could not create run directory: " + runDir.getFullPathName());
        return;
    }

    const auto report = getErcReport();
    const auto reportFile = runDir.getChildFile("erc_report.md");
    if (!reportFile.replaceWithText(report))
    {
        appendLog("Could not write ERC report: " + reportFile.getFullPathName());
        return;
    }

    const auto errors = report.fromFirstOccurrenceOf("- Errors: ", false, false)
                             .upToFirstOccurrenceOf("\n", false, false)
                             .trim();
    const auto warnings = report.fromFirstOccurrenceOf("- Warnings: ", false, false)
                               .upToFirstOccurrenceOf("\n", false, false)
                               .trim();

    appendLog("ERC complete: " + errors + " error(s), " + warnings + " warning(s). Report: " + reportFile.getFullPathName());
}

void ElectronicsWorkbench::exportCircuitArtifacts()
{
    if (getCircuitJson == nullptr || getXyceNetlist == nullptr || getLabInstrumentsJson == nullptr)
    {
        appendLog("No schematic exporter is available.");
        return;
    }

    const auto runDir = generatedRunDirectory();
    if (!runDir.createDirectory())
    {
        appendLog("Could not create run directory: " + runDir.getFullPathName());
        return;
    }

    const auto circuitFile = runDir.getChildFile("circuit.json");
    const auto netlistFile = runDir.getChildFile("generated.cir");
    const auto instrumentsFile = runDir.getChildFile("lab_instruments.json");
    const auto circuitJson = getCircuitJson();
    const auto netlist = getXyceNetlist();
    const auto instrumentsJson = getLabInstrumentsJson();

    if (!circuitFile.replaceWithText(circuitJson))
    {
        appendLog("Could not write circuit JSON: " + circuitFile.getFullPathName());
        return;
    }
    if (!netlistFile.replaceWithText(netlist))
    {
        appendLog("Could not write Xyce netlist: " + netlistFile.getFullPathName());
        return;
    }
    if (!instrumentsFile.replaceWithText(instrumentsJson))
    {
        appendLog("Could not write lab instruments JSON: " + instrumentsFile.getFullPathName());
        return;
    }

    appendLog("Exported circuit JSON, Xyce netlist, and lab instruments to " + runDir.getFullPathName());
}

void ElectronicsWorkbench::showSpecDocument()
{
    const auto spec = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
        .getParentDirectory()
        .getParentDirectory()
        .getParentDirectory()
        .getParentDirectory()
        .getChildFile("ELECTRONICS_DESIGN_TOOL_SPEC.md");
    appendLog("Research spec path: " + spec.getFullPathName());
}
