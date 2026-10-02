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

        addAndMakeVisible(components);
        components.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff10161d));
        components.setModel(&listModel);
        components.setRowHeight(42);
        components.selectRow(0);
        if (onSymbolSelected != nullptr)
            onSymbolSelected("resistor");
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff151a20));
        g.setColour(juce::Colour(0xffdce9ee));
        g.setFont(juce::Font(14.0f, juce::Font::bold));
        g.drawText("Component Library", getLocalBounds().removeFromTop(24).reduced(8, 0), juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8);
        area.removeFromTop(24);
        filter.setBounds(area.removeFromTop(28));
        area.removeFromTop(8);
        components.setBounds(area);
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
        }

        void mouseDrag(const juce::MouseEvent& event) override
        {
            juce::ListBox::mouseDrag(event);
            if (dragStarted || dragStartRow < 0 || dragStartRow >= (int)listModel.items.size())
                return;
            if (event.getDistanceFromDragStart() < 6)
                return;

            if (auto* container = findParentComponentOfClass<juce::DragAndDropContainer>())
            {
                dragStarted = true;
                const auto& symbol = listModel.items[(size_t)dragStartRow];
                juce::Image image(juce::Image::ARGB, 150, 38, true);
                juce::Graphics g(image);
                g.fillAll(juce::Colour(0xdd23394a));
                g.setColour(juce::Colour(0xffdce9ee));
                g.setFont(juce::Font(14.0f, juce::Font::bold));
                g.drawText(symbol.name, image.getBounds().reduced(8), juce::Justification::centredLeft);
                container->startDragging("symbol:" + symbol.id, this, image, true);
            }
        }

        void mouseUp(const juce::MouseEvent& event) override
        {
            juce::ListBox::mouseUp(event);
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
    std::function<void(juce::String)> onSymbolSelected;

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
        components.updateContent();
        if (!listModel.items.empty() && components.getSelectedRow() < 0)
            components.selectRow(0);
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

    juce::String buildCircuitJson() const
    {
        const auto netNames = computeNetNames();
        juce::String text;
        text << "{\n";
        text << "  \"schemaVersion\": 1,\n";
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
            text << "      \"value\": " << quote(instance.value) << ",\n";
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
            text << "    { \"a\": " << quote(pinLabel(wire.a))
                 << ", \"b\": " << quote(pinLabel(wire.b)) << " }";
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

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff0e141a));
        drawGrid(g);
        drawWires(g);
        drawInstances(g);
        drawPendingWire(g);

        g.setColour(juce::Colour(0xff93a7b0));
        g.setFont(juce::Font(13.0f));
        const auto stampOn = getStampPlacementEnabled != nullptr && getStampPlacementEnabled();
        g.drawText(stampOn ? "Stamp mode: click empty canvas to place selected symbols. Click pins to wire."
                           : "Drag components from the library. Click pins to start/end right-angle wires.",
                   getLocalBounds().reduced(12).removeFromBottom(24),
                   juce::Justification::centredLeft);

        if (dragHover)
        {
            g.setColour(juce::Colour(0x335aa7c8));
            g.fillRect(getLocalBounds());
            g.setColour(juce::Colour(0xff78dcca));
            g.drawRect(getLocalBounds().reduced(4), 2);
            g.setFont(juce::Font(15.0f, juce::Font::bold));
            g.drawText("Drop symbol on schematic", getLocalBounds().reduced(18).removeFromTop(28), juce::Justification::centredRight);
        }
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        const auto p = snap(event.position);
        if (auto pin = hitTestPin(event.position); pin.instanceIndex >= 0)
        {
            if (pendingPin.instanceIndex < 0)
            {
                pendingPin = pin;
                if (onStatus) onStatus("Wire start: " + pinLabel(pin) + ". Click another pin to connect.");
            }
            else if (!(pendingPin.instanceIndex == pin.instanceIndex && pendingPin.pinIndex == pin.pinIndex))
            {
                wires.push_back({ pendingPin, pin });
                if (onStatus) onStatus("Connected " + pinLabel(pendingPin) + " to " + pinLabel(pin) + ".");
                pendingPin = {};
            }
            repaint();
            return;
        }

        if (const auto instanceIndex = hitTestInstance(event.position); instanceIndex >= 0)
        {
            selectedInstance = instanceIndex;
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

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey)
        {
            pendingPin = {};
            repaint();
            return true;
        }
        return false;
    }

    bool isInterestedInDragSource(const SourceDetails& details) override
    {
        return details.description.toString().startsWith("symbol:");
    }

    void itemDragEnter(const SourceDetails&) override
    {
        dragHover = true;
        repaint();
    }

    void itemDragExit(const SourceDetails&) override
    {
        dragHover = false;
        repaint();
    }

    void itemDropped(const SourceDetails& details) override
    {
        dragHover = false;
        const auto description = details.description.toString();
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
    };

    struct PinRef
    {
        int instanceIndex = -1;
        int pinIndex = -1;
    };

    struct Wire
    {
        PinRef a;
        PinRef b;
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
    PinRef pendingPin;
    int selectedInstance = -1;
    int nextRef = 1;
    bool dragHover = false;
    std::function<juce::String()> getSelectedSymbolId;
    std::function<bool()> getStampPlacementEnabled;
    std::function<void(juce::String)> onStatus;
    std::function<void(int, juce::String, juce::String, juce::String, juce::String, juce::String, juce::String, juce::String)> onSelectionChanged;

    static juce::String quote(const juce::String& text)
    {
        return "\"" + text.replace("\\", "\\\\").replace("\"", "\\\"") + "\"";
    }

    static juce::String nullableQuote(const juce::String& text)
    {
        return text.isEmpty() ? juce::String("null") : quote(text);
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

    SymbolDef symbolFor(const juce::String& id) const
    {
        if (id == "capacitor")
            return { id, "C", { -30, -18, 60, 36 }, { { "1", { -42, 0 } }, { "2", { 42, 0 } } } };
        if (id == "power_bus")
            return { id, "PWR", { -42, -14, 84, 28 }, { { "VBUS", { 0, 28 } } } };
        if (id == "ground_bus")
            return { id, "GND BUS", { -48, -14, 96, 28 }, { { "0", { 0, -28 } } } };
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

    std::map<int, juce::String> computeNetNames() const
    {
        int pinCount = 0;
        for (const auto& instance : instances)
            pinCount += (int)symbolFor(instance.symbolId).pins.size();

        std::map<int, juce::String> result;
        if (pinCount <= 0)
            return result;

        DisjointSet sets(pinCount);
        for (const auto& wire : wires)
        {
            if (wire.a.instanceIndex < 0 || wire.b.instanceIndex < 0)
                continue;
            sets.unite(pinOrdinal(wire.a), pinOrdinal(wire.b));
        }

        std::set<int> groundRoots;
        for (size_t i = 0; i < instances.size(); ++i)
        {
            if (instances[i].symbolId != "ground")
                continue;
            const auto symbol = symbolFor(instances[i].symbolId);
            for (size_t p = 0; p < symbol.pins.size(); ++p)
                groundRoots.insert(sets.find(pinOrdinal({ (int)i, (int)p })));
        }

        std::map<int, int> assigned;
        int nextNet = 1;
        for (int pin = 0; pin < pinCount; ++pin)
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
        return instance.position + symbol.pins[(size_t)pin.pinIndex].offset;
    }

    juce::String pinLabel(const PinRef& pin) const
    {
        if (pin.instanceIndex < 0 || pin.instanceIndex >= (int)instances.size()) return {};
        const auto& instance = instances[(size_t)pin.instanceIndex];
        const auto symbol = symbolFor(instance.symbolId);
        if (pin.pinIndex < 0 || pin.pinIndex >= (int)symbol.pins.size()) return instance.refdes;
        return instance.refdes + "." + symbol.pins[(size_t)pin.pinIndex].name;
    }

    PinRef hitTestPin(juce::Point<float> p) const
    {
        for (int i = (int)instances.size() - 1; i >= 0; --i)
        {
            const auto symbol = symbolFor(instances[(size_t)i].symbolId);
            for (int j = 0; j < (int)symbol.pins.size(); ++j)
            {
                const auto pin = instances[(size_t)i].position + symbol.pins[(size_t)j].offset;
                if (pin.getDistanceFrom(p) <= 9.0f)
                    return { i, j };
            }
        }
        return {};
    }

    int hitTestInstance(juce::Point<float> p) const
    {
        for (int i = (int)instances.size() - 1; i >= 0; --i)
        {
            const auto& instance = instances[(size_t)i];
            const auto symbol = symbolFor(instance.symbolId);
            if (symbol.bounds.translated(instance.position.x, instance.position.y).expanded(4.0f).contains(p))
                return i;
        }
        return -1;
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
                              p });
        selectedInstance = (int)instances.size() - 1;
        notifySelection();
        if (onStatus) onStatus("Placed " + symbol.title + " at schematic grid.");
    }

    void drawSymbolBody(juce::Graphics& g, const Instance& instance, const SymbolDef& symbol)
    {
        const auto body = symbol.bounds.translated(instance.position.x, instance.position.y);
        g.setColour(juce::Colour(0xff17212b));
        g.fillRoundedRectangle(body, 4.0f);
        g.setColour(juce::Colour(0xff78dcca));
        g.drawRoundedRectangle(body, 4.0f, 1.6f);
        if (selectedInstance >= 0 && selectedInstance < (int)instances.size()
            && &instance == &instances[(size_t)selectedInstance])
        {
            g.setColour(juce::Colour(0xffffc857));
            g.drawRoundedRectangle(body.expanded(4.0f), 6.0f, 2.0f);
        }

        if (symbol.id == "opamp_741")
        {
            juce::Path tri;
            tri.startNewSubPath(body.getX(), body.getY());
            tri.lineTo(body.getX(), body.getBottom());
            tri.lineTo(body.getRight(), body.getCentreY());
            tri.closeSubPath();
            g.setColour(juce::Colour(0xff17212b));
            g.fillPath(tri);
            g.setColour(juce::Colour(0xff78dcca));
            g.strokePath(tri, juce::PathStrokeType(1.8f));
        }
        else if (symbol.id == "resistor")
        {
            g.setColour(juce::Colour(0xffe8f1f2));
            juce::Path z;
            const auto cy = body.getCentreY();
            z.startNewSubPath(body.getX(), cy);
            for (int i = 0; i < 6; ++i)
            {
                const auto x = body.getX() + (float)(i + 1) * body.getWidth() / 7.0f;
                z.lineTo(x, cy + (i % 2 == 0 ? -10.0f : 10.0f));
            }
            z.lineTo(body.getRight(), cy);
            g.strokePath(z, juce::PathStrokeType(1.8f));
        }
        else if (symbol.id == "capacitor")
        {
            g.setColour(juce::Colour(0xffe8f1f2));
            g.drawVerticalLine((int)(body.getCentreX() - 6), body.getY(), body.getBottom());
            g.drawVerticalLine((int)(body.getCentreX() + 6), body.getY(), body.getBottom());
        }
        else if (symbol.id == "power_bus")
        {
            g.setColour(juce::Colour(0xffffc857));
            g.drawLine(body.getX() + 8.0f, body.getCentreY(), body.getRight() - 8.0f, body.getCentreY(), 3.0f);
            g.drawLine(body.getCentreX(), body.getCentreY(), body.getCentreX(), body.getBottom() + 14.0f, 2.0f);
        }
        else if (symbol.id == "ground" || symbol.id == "ground_bus")
        {
            g.setColour(juce::Colour(0xffe8f1f2));
            const auto cx = body.getCentreX();
            g.drawLine(cx, body.getY(), cx, body.getY() + 10, 2.0f);
            g.drawLine(cx - 22, body.getY() + 10, cx + 22, body.getY() + 10, 2.0f);
            g.drawLine(cx - 14, body.getY() + 19, cx + 14, body.getY() + 19, 2.0f);
            g.drawLine(cx - 6, body.getY() + 28, cx + 6, body.getY() + 28, 2.0f);
        }
        else if (symbol.id == "voltage_source" || symbol.id == "battery" || symbol.id == "ac_voltage_source" || symbol.id == "signal_source")
        {
            g.setColour(juce::Colour(0xffe8f1f2));
            g.drawEllipse(body, 2.0f);
            if (symbol.id == "ac_voltage_source" || symbol.id == "signal_source")
            {
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
                g.drawText("+", body.withHeight(22.0f).toNearestInt(), juce::Justification::centred);
            }
        }

        g.setColour(juce::Colour(0xffdce9ee));
        g.setFont(juce::Font(13.0f, juce::Font::bold));
        g.drawText(symbol.title, body.toNearestInt(), juce::Justification::centred);
        g.setColour(juce::Colour(0xff93a7b0));
        g.setFont(juce::Font(12.0f));
        g.drawText(instance.refdes, (int)body.getX(), (int)body.getY() - 18, (int)body.getWidth(), 16, juce::Justification::centred);
    }

    void drawInstances(juce::Graphics& g)
    {
        for (const auto& instance : instances)
        {
            const auto symbol = symbolFor(instance.symbolId);
            drawSymbolBody(g, instance, symbol);

            for (size_t i = 0; i < symbol.pins.size(); ++i)
            {
                const auto pin = instance.position + symbol.pins[i].offset;
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

    void drawWires(juce::Graphics& g)
    {
        for (const auto& wire : wires)
            drawRightAngleWire(g, pinPosition(wire.a), pinPosition(wire.b), juce::Colour(0xfff4d35e), 2.0f);
    }

    void drawPendingWire(juce::Graphics& g)
    {
        if (pendingPin.instanceIndex < 0) return;
        const auto start = pinPosition(pendingPin);
        const auto end = getMouseXYRelative().toFloat();
        drawRightAngleWire(g, start, end, juce::Colour(0x99f4d35e), 1.5f);
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
    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff10161d));
        auto area = getLocalBounds().reduced(12);
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
    }

    std::function<void(juce::String, juce::String, juce::String, juce::String, juce::String)> onApply;

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
        apply.setBounds(area.removeFromTop(30).removeFromLeft(90));
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
    ercButton.onClick = [this] { appendLog("ERC research stub: no electrical rule engine wired yet."); };
    transientButton.onClick = [this] { exportCircuitArtifacts(); };
    compileButton.onClick = [this] { appendLog("Compiled Frust preview stub: circuit IR -> Frust lowering pending."); };

    dockManager = std::make_unique<CreationDock::DockManager>(*this);
    addAndMakeVisible(*dockManager);

    dockManager->registerPanel("library", "Component Library",
                               std::make_unique<ComponentLibraryPanel>(
                                   [this](juce::String id) {
                                       selectedSymbolId = id;
                                       appendLog("Selected symbol: " + id + ". Drag it to the schematic.");
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
    getCircuitJson = [panel = schematic.get()] { return panel->buildCircuitJson(); };
    getXyceNetlist = [panel = schematic.get()] { return panel->buildXyceNetlist(); };
    dockManager->registerPanel("schematic", "Schematic", std::move(schematic), CreationDock::DockTargetZone::CenterTab);
    dockManager->registerPanel("simulation", "Simulation", std::make_unique<SimulationPanel>(), CreationDock::DockTargetZone::CenterTab);
    dockManager->registerPanel("scope", "Instruments", std::make_unique<InstrumentPanel>(), CreationDock::DockTargetZone::Bottom);
    dockManager->registerPanel("console", "Frust Math Console", std::make_unique<ConsolePanel>(logConsole), CreationDock::DockTargetZone::Bottom);
    dockManager->registerPanel("agent", "BYOK Agent", std::make_unique<AgentPanel>(), CreationDock::DockTargetZone::Right);
    dockManager->registerPanel("properties", "Properties", std::move(properties), CreationDock::DockTargetZone::Right);
    dockManager->registerPanel("ingestion", "Spec Ingestion", std::make_unique<SpecIngestionPanel>(), CreationDock::DockTargetZone::Right);
    dockManager->registerPanel("sourcing", "Parts Sourcing", std::make_unique<PartsSourcingPanel>(), CreationDock::DockTargetZone::Right);

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
        case saveProject: exportCircuitArtifacts(); break;
        case openProject: appendLog("Open project stub: project loader pending."); break;
        case resetLayout:
            if (dockManager != nullptr) dockManager->resetLayout();
            appendLog("Dock layout reset.");
            break;
        case importComponent: appendLog("Component ingestion stub: BYOK agent/provider workflow pending."); break;
        case runErc: appendLog("ERC research stub: rules pending."); break;
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
    appendLog("New electronics research project initialized.");
}

void ElectronicsWorkbench::exportCircuitArtifacts()
{
    if (getCircuitJson == nullptr || getXyceNetlist == nullptr)
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
    const auto circuitJson = getCircuitJson();
    const auto netlist = getXyceNetlist();

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

    appendLog("Exported circuit JSON and Xyce netlist to " + runDir.getFullPathName());
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
