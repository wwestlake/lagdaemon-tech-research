#include "ElectronicsWorkbench.h"

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

        add({ "resistor", "Resistor", "Passive" });
        add({ "capacitor", "Capacitor", "Passive" });
        add({ "voltage_source", "DC Voltage Source", "Source" });
        add({ "ground", "Ground", "Reference" });
        add({ "opamp_741", "741 Op Amp - provisional", "Analog IC" });
        add({ "npn", "NPN Transistor - generic", "Discrete" });
        add({ "logic_not", "Logic Inverter - behavioral", "Digital" });
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
                         std::function<void(juce::String)> onMessage)
        : getSelectedSymbolId(std::move(getSelectedSymbol)),
          onStatus(std::move(onMessage))
    {
        setWantsKeyboardFocus(true);
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
        g.drawText("Select a symbol, click canvas to place it. Click pins to start/end right-angle wires.",
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

        const auto selected = getSelectedSymbolId != nullptr ? getSelectedSymbolId() : juce::String("resistor");
        placeSymbol(selected, p);
        repaint();
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

    std::vector<Instance> instances;
    std::vector<Wire> wires;
    PinRef pendingPin;
    int nextRef = 1;
    bool dragHover = false;
    std::function<juce::String()> getSelectedSymbolId;
    std::function<void(juce::String)> onStatus;

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
        if (id == "voltage_source")
            return { id, "V", { -24, -24, 48, 48 }, { { "+", { 0, -42 } }, { "-", { 0, 42 } } } };
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

    void placeSymbol(const juce::String& symbolId, juce::Point<float> p)
    {
        const auto symbol = symbolFor(symbolId);
        const auto prefix = symbolId == "ground" ? juce::String("GND") :
                            symbolId == "voltage_source" ? juce::String("V") :
                            symbolId == "capacitor" ? juce::String("C") :
                            symbolId == "resistor" ? juce::String("R") :
                            symbolId == "opamp_741" ? juce::String("U") :
                            symbolId == "npn" ? juce::String("Q") :
                            juce::String("U");
        instances.push_back({ symbol.id, prefix + juce::String(nextRef++), p });
        if (onStatus) onStatus("Placed " + symbol.title + " at schematic grid.");
    }

    void drawSymbolBody(juce::Graphics& g, const Instance& instance, const SymbolDef& symbol)
    {
        const auto body = symbol.bounds.translated(instance.position.x, instance.position.y);
        g.setColour(juce::Colour(0xff17212b));
        g.fillRoundedRectangle(body, 4.0f);
        g.setColour(juce::Colour(0xff78dcca));
        g.drawRoundedRectangle(body, 4.0f, 1.6f);

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
        else if (symbol.id == "ground")
        {
            g.setColour(juce::Colour(0xffe8f1f2));
            const auto cx = body.getCentreX();
            g.drawLine(cx, body.getY(), cx, body.getY() + 10, 2.0f);
            g.drawLine(cx - 22, body.getY() + 10, cx + 22, body.getY() + 10, 2.0f);
            g.drawLine(cx - 14, body.getY() + 19, cx + 14, body.getY() + 19, 2.0f);
            g.drawLine(cx - 6, body.getY() + 28, cx + 6, body.getY() + 28, 2.0f);
        }
        else if (symbol.id == "voltage_source")
        {
            g.setColour(juce::Colour(0xffe8f1f2));
            g.drawEllipse(body, 2.0f);
            g.drawText("+", body.withHeight(22.0f).toNearestInt(), juce::Justification::centred);
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

class PropertiesPanel final : public NotesPanel
{
public:
    PropertiesPanel()
        : NotesPanel("Properties",
                     "Selected object properties will appear here.\n\n"
                     "Early targets:\n"
                     "- component values\n"
                     "- pin mappings\n"
                     "- datasheet provenance\n"
                     "- simulation model choice\n"
                     "- verification status")
    {
    }
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

    newButton.onClick = [this] { resetResearchState(); };
    ercButton.onClick = [this] { appendLog("ERC research stub: no electrical rule engine wired yet."); };
    transientButton.onClick = [this] { appendLog("Transient analysis research stub: dataset engine pending."); };
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
    dockManager->registerPanel("schematic", "Schematic",
                               std::make_unique<SchematicCanvasPanel>(
                                   [this] { return selectedSymbolId; },
                                   [this](juce::String message) { appendLog(message); }),
                               CreationDock::DockTargetZone::CenterTab);
    dockManager->registerPanel("simulation", "Simulation", std::make_unique<SimulationPanel>(), CreationDock::DockTargetZone::CenterTab);
    dockManager->registerPanel("scope", "Instruments", std::make_unique<InstrumentPanel>(), CreationDock::DockTargetZone::Bottom);
    dockManager->registerPanel("console", "Frust Math Console", std::make_unique<ConsolePanel>(logConsole), CreationDock::DockTargetZone::Bottom);
    dockManager->registerPanel("agent", "BYOK Agent", std::make_unique<AgentPanel>(), CreationDock::DockTargetZone::Right);
    dockManager->registerPanel("properties", "Properties", std::make_unique<PropertiesPanel>(), CreationDock::DockTargetZone::Right);
    dockManager->registerPanel("ingestion", "Spec Ingestion", std::make_unique<SpecIngestionPanel>(), CreationDock::DockTargetZone::Right);

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
        case saveProject: appendLog("Save project stub: circuit JSON persistence pending."); break;
        case openProject: appendLog("Open project stub: project loader pending."); break;
        case resetLayout:
            if (dockManager != nullptr) dockManager->resetLayout();
            appendLog("Dock layout reset.");
            break;
        case importComponent: appendLog("Component ingestion stub: BYOK agent/provider workflow pending."); break;
        case runErc: appendLog("ERC research stub: rules pending."); break;
        case runOperatingPoint: appendLog("Operating point stub: solver backend pending."); break;
        case runTransient: appendLog("Transient stub: solver backend pending."); break;
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
