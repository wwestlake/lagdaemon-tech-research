#include "MachineBuilderPanel.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <optional>
#include <sstream>

namespace machine_builder
{
namespace
{
constexpr float cardWidth = 190.0f;
constexpr float cardHeaderHeight = 28.0f;
constexpr float portRowHeight = 18.0f;
constexpr float portRadius = 5.0f;

juce::String kindName(sim_panel::SubsystemKind kind)
{
    using K = sim_panel::SubsystemKind;
    switch (kind)
    {
        case K::DCMotor: return "Motor";
        case K::MotorSpeedController: return "Controller";
        case K::CoordinatedSequencer: return "Sequencer";
        case K::SchematicReference: return "Schematic";
        case K::ProgrammableComponent: return "FRust";
        case K::NestedAssembly: return "Assembly";
        case K::PlotPanel: return "Plot Panel";
    }
    return "Subsystem";
}

juce::String directionName(sim_panel::PortDirection d)
{
    using D = sim_panel::PortDirection;
    return d == D::Input ? "in" : d == D::Output ? "out" : "io";
}

juce::String domainName(sim_panel::PortDomain d)
{
    using D = sim_panel::PortDomain;
    return d == D::Electrical ? "electrical" : d == D::Mechanical ? "mechanical" : d == D::Observable ? "observable" : "signal";
}

std::string safeIdStem(juce::String text)
{
    text = text.trim().replaceCharacter(' ', '_')
               .retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-");
    if (text.isEmpty() || !juce::CharacterFunctions::isLetter(text[0]))
        text = "item_" + text;
    return text.toStdString();
}

std::string uniqueId(const std::vector<sim_panel::SubsystemInstance>& instances, const std::string& base)
{
    auto exists = [&](const std::string& id) {
        return std::any_of(instances.begin(), instances.end(),
                           [&](const auto& inst) { return inst.instanceId == id; });
    };
    auto candidate = base.empty() ? std::string("subsystem") : base;
    if (!exists(candidate))
        return candidate;
    for (int i = 2; i < 10000; ++i)
    {
        candidate = base + juce::String(i).toStdString();
        if (!exists(candidate))
            return candidate;
    }
    return base + "_copy";
}

std::string uniqueDefinitionId(const std::vector<sim_panel::SubsystemDefinition>& definitions, const std::string& base)
{
    auto exists = [&](const std::string& id) {
        return std::any_of(definitions.begin(), definitions.end(),
                           [&](const auto& def) { return def.id == id; });
    };
    auto candidate = base.empty() ? std::string("definition") : base;
    if (!exists(candidate))
        return candidate;
    for (int i = 2; i < 10000; ++i)
    {
        candidate = base + "_" + juce::String(i).toStdString();
        if (!exists(candidate))
            return candidate;
    }
    return base + "_copy";
}

bool canSourceFrom(const sim_panel::SubsystemPort& port, bool isSystem)
{
    if (isSystem)
        return port.direction == sim_panel::PortDirection::Input
            || port.direction == sim_panel::PortDirection::Bidirectional;
    return port.direction == sim_panel::PortDirection::Output
        || port.direction == sim_panel::PortDirection::Bidirectional;
}

bool canConnectTo(const sim_panel::SubsystemPort& port, bool isSystem)
{
    if (isSystem)
        return port.direction == sim_panel::PortDirection::Output
            || port.direction == sim_panel::PortDirection::Bidirectional;
    return port.direction == sim_panel::PortDirection::Input
        || port.direction == sim_panel::PortDirection::Bidirectional;
}

bool domainsCompatible(sim_panel::PortDomain a, sim_panel::PortDomain b)
{
    if (a == sim_panel::PortDomain::Observable || b == sim_panel::PortDomain::Observable)
        return true;
    if (a == b)
        return true;
    const auto electricalLike = [](sim_panel::PortDomain d) {
        return d == sim_panel::PortDomain::Electrical || d == sim_panel::PortDomain::SignalControl;
    };
    return electricalLike(a) && electricalLike(b);
}

bool isPlotDefinition(const sim_panel::SubsystemDefinition& def)
{
    return def.kind == sim_panel::SubsystemKind::PlotPanel;
}

std::string plotWidgetId(const std::string& instanceId)
{
    return "plot_" + instanceId;
}

int plotChannelIndexFromPort(const std::string& portName)
{
    const auto text = juce::String(portName).toLowerCase();
    if (!text.startsWith("ch"))
        return -1;
    const int n = text.substring(2).getIntValue();
    return n > 0 ? n - 1 : -1;
}

std::uint32_t defaultPlotColour(int index)
{
    static constexpr std::uint32_t colours[] {
        0xffffd166, 0xff4cc9f0, 0xfff72585, 0xff80ed99,
        0xffb8a1ff, 0xffff8c42, 0xffa7f3d0, 0xfffca5a5
    };
    return colours[(size_t)index % (sizeof(colours) / sizeof(colours[0]))];
}

std::string unitForSignal(const MachineDesign& machine, const std::string& signalPath)
{
    const auto dot = signalPath.find('.');
    if (dot == std::string::npos)
        return {};
    const auto instance = signalPath.substr(0, dot);
    const auto port = signalPath.substr(dot + 1);
    if (instance == "system")
    {
        for (const auto& p : machine.assembly.systemPorts)
            if (p.name == port)
                return p.unit;
        return {};
    }
    if (const auto* inst = machine.assembly.findInstance(instance))
        if (const auto* def = machine.assembly.findDefinition(inst->definitionId))
            if (const auto* p = def->findPort(port))
                return p->unit;
    return {};
}

int nextWidgetOrder(const sim_panel::ControlPanelLayout& layout, int column)
{
    int order = 0;
    for (const auto& w : layout.widgets)
        if (w.column == column)
            order = std::max(order, w.order + 1);
    return order;
}

sim_panel::WidgetType widgetTypeForSystemInput(const std::string& name)
{
    const auto n = juce::String(name).toLowerCase();
    if (n.contains("power"))
        return sim_panel::WidgetType::ToggleSwitch;
    if (n == "estop" || n.contains("emergency"))
        return sim_panel::WidgetType::EmergencyStop;
    if (n == "start" || n == "stop" || n == "reset")
        return sim_panel::WidgetType::PushButton;
    return sim_panel::WidgetType::Slider;
}

std::string widgetTitleForSystemPort(const std::string& name)
{
    const auto n = juce::String(name).toLowerCase();
    if (n.contains("power"))
        return "Master Power";
    if (n == "start")
        return "Start";
    if (n == "stop")
        return "Stop";
    if (n == "estop" || n == "e_stop" || n.contains("emergency"))
        return "E-Stop";
    if (n == "reset")
        return "Reset";
    if (n == "speed_setpoint" || n == "speed setpoint")
        return "Speed Command";
    return juce::String(name).replaceCharacter('_', ' ').toStdString();
}

class ToolbarIconLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void drawButtonBackground(juce::Graphics& g,
                              juce::Button& button,
                              const juce::Colour&,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override
    {
        const auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);
        auto bg = juce::Colour(0xff1e2c38);
        auto border = juce::Colour(0xff314556);

        if (!button.isEnabled())
        {
            bg = bg.withAlpha(0.45f);
            border = border.withAlpha(0.45f);
        }
        else if (shouldDrawButtonAsDown)
        {
            bg = juce::Colour(0xff15202a);
            border = juce::Colour(0xff78dcca);
        }
        else if (shouldDrawButtonAsHighlighted)
        {
            bg = juce::Colour(0xff293d4e);
            border = juce::Colour(0xff5bc0be);
        }

        g.setColour(bg);
        g.fillRoundedRectangle(bounds, 4.0f);
        g.setColour(border);
        g.drawRoundedRectangle(bounds, 4.0f, 1.0f);
    }

    void drawButtonText(juce::Graphics& g,
                        juce::TextButton& button,
                        bool,
                        bool) override
    {
        const auto iconId = button.getProperties()["toolbarIcon"].toString();
        const auto box = button.getLocalBounds().toFloat().reduced(5.5f, 4.5f);
        const float cx = box.getCentreX();
        const float cy = box.getCentreY();
        const float r = std::min(box.getWidth(), box.getHeight()) * 0.5f;

        auto fg = juce::Colour(0xffdce9ee);
        if (iconId == "start")
            fg = juce::Colour(0xff80ed99);
        else if (iconId == "stop" || iconId == "remove")
            fg = juce::Colour(0xffff6b6b);
        else if (iconId == "compile" || iconId == "apply")
            fg = juce::Colour(0xff7bdff2);
        else if (iconId == "validate" || iconId == "add_plot")
            fg = juce::Colour(0xffffd166);
        else if (iconId == "add_motor")
            fg = juce::Colour(0xfff4a261);

        if (!button.isEnabled())
            fg = fg.withAlpha(0.4f);

        g.setColour(fg);
        const juce::PathStrokeType stroke(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

        if (iconId == "new_blank")
        {
            juce::Path doc;
            doc.startNewSubPath(box.getX() + 2.0f, box.getY());
            doc.lineTo(box.getRight() - 4.5f, box.getY());
            doc.lineTo(box.getRight() - 1.0f, box.getY() + 3.5f);
            doc.lineTo(box.getRight() - 1.0f, box.getBottom());
            doc.lineTo(box.getX() + 2.0f, box.getBottom());
            doc.closeSubPath();
            g.strokePath(doc, stroke);
            g.drawHorizontalLine((int)std::round(cy + 1.0f), box.getX() + 4.5f, box.getRight() - 3.5f);
            g.drawVerticalLine((int)std::round(cx + 0.5f), cy - 2.0f, cy + 4.0f);
        }
        else if (iconId == "load")
        {
            juce::Path folder;
            folder.startNewSubPath(box.getX() + 1.0f, box.getY() + 2.5f);
            folder.lineTo(cx - 1.0f, box.getY() + 2.5f);
            folder.lineTo(cx + 1.0f, box.getY() + 4.5f);
            folder.lineTo(box.getRight() - 1.0f, box.getY() + 4.5f);
            folder.lineTo(box.getRight() - 1.0f, box.getBottom() - 1.0f);
            folder.lineTo(box.getX() + 1.0f, box.getBottom() - 1.0f);
            folder.closeSubPath();
            g.strokePath(folder, stroke);
        }
        else if (iconId == "save")
        {
            juce::Path disk;
            disk.startNewSubPath(box.getX() + 1.5f, box.getY() + 1.0f);
            disk.lineTo(box.getRight() - 3.5f, box.getY() + 1.0f);
            disk.lineTo(box.getRight() - 1.5f, box.getY() + 3.0f);
            disk.lineTo(box.getRight() - 1.5f, box.getBottom() - 1.0f);
            disk.lineTo(box.getX() + 1.5f, box.getBottom() - 1.0f);
            disk.closeSubPath();
            g.strokePath(disk, stroke);
            g.drawRect(juce::Rectangle<float>(box.getX() + 4.0f, box.getY() + 1.5f, box.getWidth() - 8.0f, 4.0f), 1.2f);
        }
        else if (iconId == "add_schematic")
        {
            juce::Path zig;
            const float y = cy;
            zig.startNewSubPath(box.getX(), y);
            zig.lineTo(box.getX() + 3.0f, y);
            zig.lineTo(box.getX() + 5.0f, y - 3.5f);
            zig.lineTo(cx, y + 3.5f);
            zig.lineTo(box.getRight() - 5.0f, y - 3.5f);
            zig.lineTo(box.getRight() - 3.0f, y);
            zig.lineTo(box.getRight(), y);
            g.strokePath(zig, stroke);
        }
        else if (iconId == "add_frust")
        {
            const auto chip = box.reduced(3.0f, 2.0f);
            g.drawRoundedRectangle(chip, 1.5f, 1.4f);
            for (int i = 0; i < 3; ++i)
            {
                const float py = chip.getY() + 2.2f + (float)i * 3.2f;
                g.drawLine(box.getX(), py, chip.getX(), py, 1.3f);
                g.drawLine(chip.getRight(), py, box.getRight(), py, 1.3f);
            }
        }
        else if (iconId == "add_motor")
        {
            g.drawEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f, 1.5f);
            g.setFont(juce::Font(9.5f, juce::Font::bold));
            g.drawText("M", box.toNearestInt(), juce::Justification::centred, false);
        }
        else if (iconId == "add_plot")
        {
            g.drawRoundedRectangle(box.reduced(0.5f), 2.0f, 1.2f);
            juce::Path wave;
            wave.startNewSubPath(box.getX() + 2.0f, cy + 2.0f);
            wave.lineTo(cx - 2.0f, cy - 3.0f);
            wave.lineTo(cx + 1.5f, cy + 2.5f);
            wave.lineTo(box.getRight() - 2.0f, cy - 2.5f);
            g.strokePath(wave, stroke);
        }
        else if (iconId == "add_input")
        {
            juce::Path p;
            p.startNewSubPath(box.getX() + 1.0f, cy);
            p.lineTo(cx + 2.0f, cy);
            p.startNewSubPath(cx - 1.0f, cy - 3.0f);
            p.lineTo(cx + 2.5f, cy);
            p.lineTo(cx - 1.0f, cy + 3.0f);
            g.strokePath(p, stroke);
            g.fillEllipse(box.getRight() - 4.5f, cy - 2.0f, 4.0f, 4.0f);
        }
        else if (iconId == "add_output")
        {
            g.fillEllipse(box.getX() + 0.5f, cy - 2.0f, 4.0f, 4.0f);
            juce::Path p;
            p.startNewSubPath(cx - 2.0f, cy);
            p.lineTo(box.getRight() - 1.0f, cy);
            p.startNewSubPath(box.getRight() - 4.5f, cy - 3.0f);
            p.lineTo(box.getRight() - 1.0f, cy);
            p.lineTo(box.getRight() - 4.5f, cy + 3.0f);
            g.strokePath(p, stroke);
        }
        else if (iconId == "remove")
        {
            juce::Path xPath;
            xPath.startNewSubPath(box.getX() + 2.5f, box.getY() + 2.5f);
            xPath.lineTo(box.getRight() - 2.5f, box.getBottom() - 2.5f);
            xPath.startNewSubPath(box.getRight() - 2.5f, box.getY() + 2.5f);
            xPath.lineTo(box.getX() + 2.5f, box.getBottom() - 2.5f);
            g.strokePath(xPath, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else if (iconId == "fit_all")
        {
            juce::Path corners;
            const float c = 3.5f;
            corners.startNewSubPath(box.getX(), box.getY() + c);
            corners.lineTo(box.getX(), box.getY());
            corners.lineTo(box.getX() + c, box.getY());
            corners.startNewSubPath(box.getRight() - c, box.getY());
            corners.lineTo(box.getRight(), box.getY());
            corners.lineTo(box.getRight(), box.getY() + c);
            corners.startNewSubPath(box.getX(), box.getBottom() - c);
            corners.lineTo(box.getX(), box.getBottom());
            corners.lineTo(box.getX() + c, box.getBottom());
            corners.startNewSubPath(box.getRight() - c, box.getBottom());
            corners.lineTo(box.getRight(), box.getBottom());
            corners.lineTo(box.getRight(), box.getBottom() - c);
            g.strokePath(corners, stroke);
        }
        else if (iconId == "zoom_in" || iconId == "zoom_out")
        {
            const float gr = r * 0.72f;
            const float gx = cx - 1.2f;
            const float gy = cy - 1.2f;
            g.drawEllipse(gx - gr, gy - gr, gr * 2.0f, gr * 2.0f, 1.4f);
            g.drawLine(gx + gr * 0.7f, gy + gr * 0.7f, box.getRight() - 1.0f, box.getBottom() - 1.0f, 1.6f);
            g.drawLine(gx - 2.5f, gy, gx + 2.5f, gy, 1.3f);
            if (iconId == "zoom_in")
                g.drawLine(gx, gy - 2.5f, gx, gy + 2.5f, 1.3f);
        }
        else if (iconId == "reset_view")
        {
            g.setFont(juce::Font(9.5f, juce::Font::bold));
            g.drawText("1:1", button.getLocalBounds().reduced(2), juce::Justification::centred, false);
        }
        else if (iconId == "compile")
        {
            g.drawEllipse(cx - r * 0.65f, cy - r * 0.65f, r * 1.3f, r * 1.3f, 1.6f);
            for (int i = 0; i < 6; ++i)
            {
                const float a = (float)i * juce::MathConstants<float>::pi / 3.0f;
                g.drawLine(cx + std::cos(a) * (r * 0.65f), cy + std::sin(a) * (r * 0.65f),
                           cx + std::cos(a) * r, cy + std::sin(a) * r, 1.6f);
            }
        }
        else if (iconId == "start")
        {
            juce::Path play;
            play.addTriangle(box.getX() + 3.0f, box.getY() + 1.5f,
                             box.getX() + 3.0f, box.getBottom() - 1.5f,
                             box.getRight() - 2.0f, cy);
            g.fillPath(play);
        }
        else if (iconId == "stop")
        {
            g.fillRoundedRectangle(box.reduced(2.5f), 1.5f);
        }
        else if (iconId == "reset")
        {
            juce::Path arc;
            arc.addCentredArc(cx, cy, r * 0.78f, r * 0.78f, 0.0f, 0.6f, 5.4f, true);
            g.strokePath(arc, stroke);
            juce::Path head;
            head.addTriangle(cx + 1.5f, box.getY() + 0.5f,
                             cx + 5.5f, box.getY() + 2.5f,
                             cx + 1.5f, box.getY() + 4.8f);
            g.fillPath(head);
        }
        else if (iconId == "validate")
        {
            juce::Path check;
            check.startNewSubPath(box.getX() + 2.0f, cy + 0.5f);
            check.lineTo(cx - 1.0f, box.getBottom() - 2.0f);
            check.lineTo(box.getRight() - 1.5f, box.getY() + 2.0f);
            g.strokePath(check, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else if (iconId == "open_model")
        {
            juce::Path ext;
            ext.startNewSubPath(cx, box.getY() + 2.0f);
            ext.lineTo(box.getX() + 1.5f, box.getY() + 2.0f);
            ext.lineTo(box.getX() + 1.5f, box.getBottom() - 1.5f);
            ext.lineTo(box.getRight() - 2.0f, box.getBottom() - 1.5f);
            ext.lineTo(box.getRight() - 2.0f, cy);
            ext.startNewSubPath(cx + 0.5f, cy - 0.5f);
            ext.lineTo(box.getRight() - 1.0f, box.getY() + 1.0f);
            ext.startNewSubPath(box.getRight() - 4.5f, box.getY() + 1.0f);
            ext.lineTo(box.getRight() - 1.0f, box.getY() + 1.0f);
            ext.lineTo(box.getRight() - 1.0f, box.getY() + 4.5f);
            g.strokePath(ext, stroke);
        }
        else if (iconId == "apply")
        {
            juce::Path check;
            check.startNewSubPath(box.getX() + 1.5f, cy);
            check.lineTo(cx - 1.0f, cy + 3.0f);
            check.lineTo(box.getRight() - 1.5f, box.getY() + 2.0f);
            g.strokePath(check, stroke);
            g.drawLine(box.getX() + 2.0f, box.getBottom() - 1.0f, box.getRight() - 2.0f, box.getBottom() - 1.0f, 1.4f);
        }
        else if (iconId == "overflow")
        {
            juce::Path chev;
            chev.startNewSubPath(cx - 3.5f, cy - 3.5f);
            chev.lineTo(cx - 0.5f, cy);
            chev.lineTo(cx - 3.5f, cy + 3.5f);
            chev.startNewSubPath(cx + 0.5f, cy - 3.5f);
            chev.lineTo(cx + 3.5f, cy);
            chev.lineTo(cx + 0.5f, cy + 3.5f);
            g.strokePath(chev, stroke);
        }
        else
        {
            g.setFont(juce::Font(10.0f, juce::Font::bold));
            g.drawText(button.getButtonText().substring(0, 2), button.getLocalBounds(), juce::Justification::centred, false);
        }
    }
};

} // namespace

class MachineBuilderPanel::AssemblyCanvas final : public juce::Component
{
public:
    explicit AssemblyCanvas(MachineBuilderPanel& ownerIn) : owner(ownerIn)
    {
        setWantsKeyboardFocus(true);
    }

    void resetView()
    {
        zoom = 1.0f;
        viewOffset = {};
        selectedWaypointIndex = -1;
        repaint();
    }

    void zoomBy(float factor)
    {
        const juce::Point<float> centre { std::max(1.0f, (float)getWidth() * 0.5f),
                                          std::max(1.0f, (float)getHeight() * 0.5f) };
        const auto before = screenToWorld(centre);
        zoom = std::clamp(zoom * factor, minZoom, maxZoom);
        viewOffset += centre - worldToScreen(before);
        repaint();
    }

    float getZoom() const { return zoom; }

    void fitMachine()
    {
        auto bounds = machineWorldBounds();
        if (bounds.isEmpty() || getWidth() <= 0 || getHeight() <= 0)
        {
            resetView();
            return;
        }

        bounds = bounds.expanded(80.0f);
        const auto sx = (float)getWidth() / std::max(1.0f, bounds.getWidth());
        const auto sy = (float)getHeight() / std::max(1.0f, bounds.getHeight());
        zoom = std::clamp(std::min(sx, sy), minZoom, maxZoom);
        const auto targetCentre = bounds.getCentre();
        viewOffset = { (float)getWidth() * 0.5f - targetCentre.x * zoom,
                       (float)getHeight() * 0.5f - targetCentre.y * zoom };
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        rebuildGeometry();

        g.fillAll(juce::Colour(0xff10171f));
        drawGrid(g);
        drawConnections(g);
        drawDraggedWire(g);
        drawSystemPorts(g);
        drawInstances(g);
        drawWaypointHandles(g);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        rebuildGeometry();
        mouseDownPos = e.position;
        selectedWaypointIndex = -1;

        if (const auto wp = waypointAt(e.position); wp.has_value())
        {
            owner.selectConnection(wp->connectionId);
            selectedWaypointIndex = wp->index;
            draggingWaypoint = true;
            repaint();
            return;
        }

        if (const auto endpoint = endpointAt(e.position); endpoint.has_value())
        {
            owner.selectInstance(endpoint->path());
            if (endpoint->canSource())
            {
                draggingWire = true;
                wireHasDragged = false;
                wireStart = endpoint;
                wireDragPos = e.position;
            }
            repaint();
            return;
        }

        if (const auto conn = connectionAt(e.position); conn.isNotEmpty())
        {
            owner.selectConnection(conn.toStdString());
            repaint();
            return;
        }

        for (const auto& card : cardRects)
        {
            if (card.screenBounds.contains(e.position))
            {
                owner.selectInstance(card.instanceId);
                draggingInstance = card.instanceId;
                dragStartMouseWorld = screenToWorld(e.position);
                dragStartInstanceWorld = card.worldBounds.getPosition();
                repaint();
                return;
            }
        }

        owner.selectInstance({});
        owner.selectConnection({});
        panning = true;
        panStartMouse = e.position;
        panStartOffset = viewOffset;
        repaint();
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (draggingWire)
        {
            wireDragPos = e.position;
            wireHasDragged = wireHasDragged || e.position.getDistanceFrom(mouseDownPos) > 4.0f;
            repaint();
            return;
        }

        if (draggingWaypoint)
        {
            if (auto* conn = selectedConnection())
            {
                if (selectedWaypointIndex >= 0 && selectedWaypointIndex < (int)conn->routePoints.size())
                {
                    const auto world = screenToWorld(e.position);
                    conn->routePoints[(size_t)selectedWaypointIndex] = { world.x, world.y };
                    owner.updateDiagnosticsDisplay();
                    repaint();
                }
            }
            return;
        }

        if (!draggingInstance.empty())
        {
            const auto delta = screenToWorld(e.position) - dragStartMouseWorld;
            std::string ignored;
            owner.machine_.setInstanceLayout(draggingInstance,
                                             std::max(40.0f, dragStartInstanceWorld.x + delta.x),
                                             std::max(30.0f, dragStartInstanceWorld.y + delta.y),
                                             &ignored);
            owner.updateDiagnosticsDisplay();
            repaint();
            return;
        }

        if (panning)
        {
            viewOffset = panStartOffset + (e.position - panStartMouse);
            repaint();
        }
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (draggingWire)
        {
            const auto start = wireStart;
            draggingWire = false;
            wireStart.reset();
            if (wireHasDragged && start.has_value())
                if (const auto target = endpointAt(e.position); target.has_value())
                    completeWire(*start, *target);
            repaint();
        }

        draggingWaypoint = false;
        draggingInstance.clear();
        panning = false;
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        rebuildGeometry();
        const auto connId = connectionAt(e.position);
        if (connId.isEmpty())
            return;

        if (auto* conn = findConnection(connId.toStdString()))
        {
            const auto world = screenToWorld(e.position);
            conn->routePoints.push_back({ world.x, world.y });
            owner.selectedConnectionId_ = conn->connectionId;
            owner.selectedInstanceId_.clear();
            selectedWaypointIndex = (int)conn->routePoints.size() - 1;
            owner.refreshUi();
            owner.updateDiagnosticsDisplay();
            repaint();
        }
    }

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        const auto before = screenToWorld(e.position);
        const float factor = wheel.deltaY > 0.0f ? 1.1f : (1.0f / 1.1f);
        zoom = std::clamp(zoom * factor, minZoom, maxZoom);
        viewOffset += e.position - worldToScreen(before);
        repaint();
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (isTextEditingFocused())
            return false;

        if (key == juce::KeyPress::escapeKey)
        {
            draggingWire = false;
            wireStart.reset();
            selectedWaypointIndex = -1;
            repaint();
            return true;
        }

        if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        {
            if (removeSelectedWaypoint())
                return true;

            if (!owner.selectedConnectionId_.empty() || !owner.selectedInstanceId_.empty())
            {
                owner.removeSelected();
                return true;
            }
            return false;
        }

        if (key.getModifiers().isCommandDown())
        {
            const auto ch = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());
            if (ch == 'n')
            {
                owner.resetToBlankMachine();
                return true;
            }
            if (ch == 'o')
            {
                owner.chooseMachineLoadFile();
                return true;
            }
            if (ch == 's')
            {
                owner.chooseMachineSaveFile();
                return true;
            }
        }

        if (!key.getModifiers().isAnyModifierKeyDown())
        {
            const auto ch = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());
            if (ch == 'f')
            {
                owner.fitMachineView();
                return true;
            }
            if (ch == '=' || ch == '+')
            {
                owner.zoomInView();
                return true;
            }
            if (ch == '-')
            {
                owner.zoomOutView();
                return true;
            }
            if (ch == '0')
            {
                owner.resetMachineView();
                return true;
            }
            if (key.getKeyCode() == juce::KeyPress::F7Key)
            {
                owner.compileAndRun();
                return true;
            }
            if (key.getKeyCode() == juce::KeyPress::F5Key)
            {
                owner.startSimulation();
                return true;
            }
        }
        else if (key.getModifiers().isShiftDown() && key.getKeyCode() == juce::KeyPress::F5Key)
        {
            owner.stopSimulation();
            return true;
        }

        return false;
    }

private:
    struct EndpointRect
    {
        std::string instanceId;
        std::string portName;
        sim_panel::SubsystemPort port;
        juce::Point<float> worldCentre;
        juce::Rectangle<float> screenBounds;
        bool system = false;

        std::string path() const { return instanceId + "." + portName; }
        bool canSource() const { return canSourceFrom(port, system); }
        bool canDest() const { return canConnectTo(port, system); }
    };

    struct CardRect
    {
        std::string instanceId;
        juce::Rectangle<float> worldBounds;
        juce::Rectangle<float> screenBounds;
    };

    struct WaypointHit
    {
        std::string connectionId;
        int index = -1;
    };

    MachineBuilderPanel& owner;
    std::vector<EndpointRect> endpointRects;
    std::vector<CardRect> cardRects;
    std::optional<EndpointRect> wireStart;
    juce::Point<float> wireDragPos;
    bool draggingWire = false;
    bool wireHasDragged = false;
    bool draggingWaypoint = false;
    int selectedWaypointIndex = -1;
    std::string draggingInstance;
    juce::Point<float> dragStartMouseWorld;
    juce::Point<float> dragStartInstanceWorld;
    bool panning = false;
    juce::Point<float> panStartMouse;
    juce::Point<float> panStartOffset;
    juce::Point<float> mouseDownPos;
    juce::Point<float> viewOffset;
    float zoom = 1.0f;
    static constexpr float minZoom = 0.25f;
    static constexpr float maxZoom = 2.5f;

    static bool isTextEditingFocused()
    {
        if (auto* focused = juce::Component::getCurrentlyFocusedComponent())
        {
            return dynamic_cast<juce::TextEditor*>(focused) != nullptr
                || dynamic_cast<juce::Label*>(focused) != nullptr
                || focused->findParentComponentOfClass<juce::TextEditor>() != nullptr;
        }
        return false;
    }

    juce::Point<float> worldToScreen(juce::Point<float> world) const
    {
        return { world.x * zoom + viewOffset.x, world.y * zoom + viewOffset.y };
    }

    juce::Point<float> screenToWorld(juce::Point<float> screen) const
    {
        return { (screen.x - viewOffset.x) / zoom, (screen.y - viewOffset.y) / zoom };
    }

    juce::Rectangle<float> worldToScreen(juce::Rectangle<float> world) const
    {
        const auto p = worldToScreen(world.getPosition());
        return { p.x, p.y, world.getWidth() * zoom, world.getHeight() * zoom };
    }

    juce::Rectangle<float> cardBoundsFor(const sim_panel::SubsystemInstance& inst, const sim_panel::SubsystemDefinition& def) const
    {
        const auto rows = std::max<size_t>(3, def.ports.size());
        return { (float)inst.canvasX, (float)inst.canvasY, cardWidth, cardHeaderHeight + 12.0f + (float)rows * portRowHeight };
    }

    float systemOutputX() const
    {
        float x = 760.0f;
        for (const auto& inst : owner.machine_.assembly.instances)
            if (const auto* def = owner.machine_.assembly.findDefinition(inst.definitionId))
                x = std::max(x, (float)inst.canvasX + cardBoundsFor(inst, *def).getWidth() + 140.0f);
        return x;
    }

    juce::Rectangle<float> systemPortBoundsWorld(int index, bool input) const
    {
        const float x = input ? 24.0f : systemOutputX();
        return { x, 64.0f + (float)index * 34.0f, 150.0f, 24.0f };
    }

    void rebuildGeometry()
    {
        endpointRects.clear();
        cardRects.clear();

        int inputIndex = 0;
        int outputIndex = 0;
        for (const auto& port : owner.machine_.assembly.systemPorts)
        {
            const bool input = port.direction != sim_panel::PortDirection::Output;
            const auto box = systemPortBoundsWorld(input ? inputIndex++ : outputIndex++, input);
            const auto centre = input ? juce::Point<float>(box.getRight(), box.getCentreY())
                                      : juce::Point<float>(box.getX(), box.getCentreY());
            const auto screenCentre = worldToScreen(centre);
            endpointRects.push_back({ "system", port.name, port, centre,
                                      juce::Rectangle<float>(screenCentre.x - 8.0f, screenCentre.y - 8.0f, 16.0f, 16.0f), true });
        }

        for (const auto& inst : owner.machine_.assembly.instances)
        {
            const auto* def = owner.machine_.assembly.findDefinition(inst.definitionId);
            if (def == nullptr)
                continue;

            const auto card = cardBoundsFor(inst, *def);
            cardRects.push_back({ inst.instanceId, card, worldToScreen(card) });

            int row = 0;
            for (const auto& port : def->ports)
            {
                const bool inputSide = port.direction == sim_panel::PortDirection::Input;
                const bool outputSide = port.direction == sim_panel::PortDirection::Output;
                const auto y = card.getY() + cardHeaderHeight + 8.0f + (float)row * portRowHeight;
                const auto centre = inputSide ? juce::Point<float>(card.getX(), y + 8.0f)
                                  : outputSide ? juce::Point<float>(card.getRight(), y + 8.0f)
                                               : juce::Point<float>(card.getRight(), y + 8.0f);
                const auto screenCentre = worldToScreen(centre);
                endpointRects.push_back({ inst.instanceId, port.name, port, centre,
                                          juce::Rectangle<float>(screenCentre.x - 8.0f, screenCentre.y - 8.0f, 16.0f, 16.0f), false });
                ++row;
            }
        }
    }

    juce::Rectangle<float> machineWorldBounds() const
    {
        bool any = false;
        juce::Rectangle<float> bounds;
        auto include = [&](juce::Rectangle<float> r) {
            bounds = any ? bounds.getUnion(r) : r;
            any = true;
        };
        auto includePoint = [&](juce::Point<float> p) {
            include({ p.x - 1.0f, p.y - 1.0f, 2.0f, 2.0f });
        };

        int inputIndex = 0;
        int outputIndex = 0;
        for (const auto& port : owner.machine_.assembly.systemPorts)
        {
            const bool input = port.direction != sim_panel::PortDirection::Output;
            include(systemPortBoundsWorld(input ? inputIndex++ : outputIndex++, input));
        }

        for (const auto& inst : owner.machine_.assembly.instances)
            if (const auto* def = owner.machine_.assembly.findDefinition(inst.definitionId))
                include(cardBoundsFor(inst, *def));

        for (const auto& conn : owner.machine_.assembly.connections)
            for (const auto& p : conn.routePoints)
                includePoint({ (float)p.x, (float)p.y });

        return bounds;
    }

    void drawGrid(juce::Graphics& g)
    {
        g.setColour(juce::Colour(0xff18232e));
        const auto topLeft = screenToWorld({ 0.0f, 0.0f });
        const auto bottomRight = screenToWorld({ (float)getWidth(), (float)getHeight() });
        constexpr float step = 24.0f;
        const int startX = (int)std::floor(topLeft.x / step) - 1;
        const int endX = (int)std::ceil(bottomRight.x / step) + 1;
        const int startY = (int)std::floor(topLeft.y / step) - 1;
        const int endY = (int)std::ceil(bottomRight.y / step) + 1;
        for (int x = startX; x <= endX; ++x)
        {
            const auto sx = worldToScreen(juce::Point<float> { (float)x * step, 0.0f }).x;
            g.drawVerticalLine((int)std::round(sx), 0.0f, (float)getHeight());
        }
        for (int y = startY; y <= endY; ++y)
        {
            const auto sy = worldToScreen(juce::Point<float> { 0.0f, (float)y * step }).y;
            g.drawHorizontalLine((int)std::round(sy), 0.0f, (float)getWidth());
        }
    }

    void drawSystemPorts(juce::Graphics& g)
    {
        int inputIndex = 0;
        int outputIndex = 0;
        for (const auto& port : owner.machine_.assembly.systemPorts)
        {
            const bool input = port.direction != sim_panel::PortDirection::Output;
            const auto box = worldToScreen(systemPortBoundsWorld(input ? inputIndex++ : outputIndex++, input));
            const bool selected = owner.selectedConnectionId_.empty() && owner.selectedInstanceId_ == std::string("system.") + port.name;
            g.setColour(selected ? juce::Colour(0xff315a6a) : juce::Colour(0xff1b2935));
            g.fillRoundedRectangle(box, 5.0f);
            g.setColour(juce::Colour(0xff456172));
            g.drawRoundedRectangle(box, 5.0f, 1.0f);
            g.setColour(juce::Colour(0xffdce9ee));
            g.setFont(juce::Font(std::max(10.0f, 12.0f * std::min(zoom, 1.2f)), juce::Font::bold));
            g.drawFittedText("system." + juce::String(port.name), box.reduced(8.0f, 0.0f).toNearestInt(),
                             juce::Justification::centredLeft, 1);

            if (const auto endpoint = endpointFor("system", port.name); endpoint.has_value())
                drawPort(g, endpoint->screenBounds.getCentre(), port, isCompatibleDropTarget(*endpoint));
        }
    }

    void drawInstances(juce::Graphics& g)
    {
        for (const auto& inst : owner.machine_.assembly.instances)
        {
            const auto* def = owner.machine_.assembly.findDefinition(inst.definitionId);
            if (def == nullptr)
                continue;

            const auto card = worldToScreen(cardBoundsFor(inst, *def));
            auto box = card;
            const bool selected = owner.selectedInstanceId_ == inst.instanceId;
            g.setColour(selected ? juce::Colour(0xff243f4a) : juce::Colour(0xff17212b));
            g.fillRoundedRectangle(card, 6.0f);
            g.setColour(selected ? juce::Colour(0xff7bdff2) : juce::Colour(0xff3b4d5e));
            g.drawRoundedRectangle(card, 6.0f, selected ? 2.0f : 1.0f);

            auto header = box.removeFromTop(cardHeaderHeight * zoom);
            g.setColour(juce::Colour(0xff21303c));
            g.fillRoundedRectangle(header, 6.0f);
            g.setColour(juce::Colour(0xffdce9ee));
            g.setFont(juce::Font(std::max(10.0f, 13.0f * std::min(zoom, 1.2f)), juce::Font::bold));
            g.drawFittedText(juce::String(inst.title.empty() ? inst.instanceId : inst.title), header.reduced(8.0f, 0.0f).toNearestInt(),
                             juce::Justification::centredLeft, 1);

            g.setColour(juce::Colour(0xff9eb3c4));
            g.setFont(juce::Font(std::max(9.0f, 11.0f * std::min(zoom, 1.2f))));
            g.drawText(kindName(def->kind), header.removeFromRight(72.0f * zoom).toNearestInt(), juce::Justification::centredRight);

            int row = 0;
            for (const auto& port : def->ports)
            {
                const bool inputSide = port.direction == sim_panel::PortDirection::Input;
                const auto worldY = (float)inst.canvasY + cardHeaderHeight + 8.0f + (float)row * portRowHeight;
                const auto screenY = worldToScreen(juce::Point<float> { 0.0f, worldY }).y;
                if (const auto endpoint = endpointFor(inst.instanceId, port.name); endpoint.has_value())
                    drawPort(g, endpoint->screenBounds.getCentre(), port, isCompatibleDropTarget(*endpoint));
                g.setColour(juce::Colour(0xffc7d4df));
                g.setFont(juce::Font(std::max(9.0f, 11.0f * std::min(zoom, 1.15f))));
                const auto textArea = juce::Rectangle<float>(card.getX() + 12.0f * zoom, screenY,
                                                             cardWidth * zoom - 24.0f * zoom, portRowHeight * zoom);
                g.drawFittedText(juce::String(port.name) + "  " + directionName(port.direction), textArea.toNearestInt(),
                                 inputSide ? juce::Justification::centredLeft : juce::Justification::centredRight, 1);
                ++row;
            }
        }
    }

    void drawPort(juce::Graphics& g, juce::Point<float> centre, const sim_panel::SubsystemPort& port, bool compatible)
    {
        juce::Colour colour = port.domain == sim_panel::PortDomain::Mechanical ? juce::Colour(0xfff4a261)
                            : port.domain == sim_panel::PortDomain::Electrical ? juce::Colour(0xff7bdff2)
                                                                               : juce::Colour(0xff80ed99);
        if (compatible)
            colour = juce::Colour(0xffffd166);
        const auto r = compatible ? portRadius + 2.0f : portRadius;
        g.setColour(colour);
        g.fillEllipse(centre.x - r, centre.y - r, r * 2.0f, r * 2.0f);
        g.setColour(juce::Colour(0xff0c1117));
        g.drawEllipse(centre.x - r, centre.y - r, r * 2.0f, r * 2.0f, 1.0f);
    }

    void drawConnections(juce::Graphics& g)
    {
        for (const auto& conn : owner.machine_.assembly.connections)
        {
            const auto points = connectionScreenPoints(conn);
            if (points.size() < 2)
                continue;
            const bool selected = conn.connectionId == owner.selectedConnectionId_;
            g.setColour(selected ? juce::Colour(0xffffd166) : juce::Colour(0xff6da5b8));
            juce::Path p;
            p.startNewSubPath(points.front());
            if (conn.routePoints.empty() && points.size() == 2)
            {
                const auto from = points.front();
                const auto to = points.back();
                const float mid = std::max(50.0f, std::abs(to.x - from.x) * 0.45f);
                p.cubicTo(from.x + mid, from.y, to.x - mid, to.y, to.x, to.y);
            }
            else
            {
                for (size_t i = 1; i < points.size(); ++i)
                    p.lineTo(points[i]);
            }
            g.strokePath(p, juce::PathStrokeType(selected ? 3.0f : 2.0f));
        }
    }

    void drawWaypointHandles(juce::Graphics& g)
    {
        if (owner.selectedConnectionId_.empty())
            return;
        const auto* conn = findConnection(owner.selectedConnectionId_);
        if (conn == nullptr)
            return;
        for (int i = 0; i < (int)conn->routePoints.size(); ++i)
        {
            const auto screen = worldToScreen(juce::Point<float> { (float)conn->routePoints[(size_t)i].x, (float)conn->routePoints[(size_t)i].y });
            g.setColour(i == selectedWaypointIndex ? juce::Colour(0xffffd166) : juce::Colour(0xffdce9ee));
            g.fillEllipse(screen.x - 5.0f, screen.y - 5.0f, 10.0f, 10.0f);
            g.setColour(juce::Colour(0xff0c1117));
            g.drawEllipse(screen.x - 5.0f, screen.y - 5.0f, 10.0f, 10.0f, 1.0f);
        }
    }

    void drawDraggedWire(juce::Graphics& g)
    {
        if (!draggingWire || !wireStart.has_value())
            return;
        g.setColour(juce::Colour(0xffffd166));
        juce::Path p;
        p.startNewSubPath(wireStart->screenBounds.getCentre());
        p.lineTo(wireDragPos);
        g.strokePath(p, juce::PathStrokeType(2.0f));
    }

    std::optional<EndpointRect> endpointFor(const std::string& instance, const std::string& port) const
    {
        for (const auto& e : endpointRects)
            if (e.instanceId == instance && e.portName == port)
                return e;
        return std::nullopt;
    }

    std::optional<EndpointRect> endpointAt(juce::Point<float> p) const
    {
        for (const auto& e : endpointRects)
            if (e.screenBounds.expanded(4.0f).contains(p))
                return e;
        return std::nullopt;
    }

    std::vector<juce::Point<float>> connectionScreenPoints(const sim_panel::SignalConnection& conn) const
    {
        std::vector<juce::Point<float>> points;
        const auto from = endpointFor(conn.fromInstance, conn.fromPort);
        const auto to = endpointFor(conn.toInstance, conn.toPort);
        if (!from.has_value() || !to.has_value())
            return points;
        points.push_back(from->screenBounds.getCentre());
        for (const auto& p : conn.routePoints)
            points.push_back(worldToScreen(juce::Point<float> { (float)p.x, (float)p.y }));
        points.push_back(to->screenBounds.getCentre());
        return points;
    }

    juce::String connectionAt(juce::Point<float> p) const
    {
        for (const auto& conn : owner.machine_.assembly.connections)
        {
            const auto points = connectionScreenPoints(conn);
            if (points.size() < 2)
                continue;
            for (size_t i = 1; i < points.size(); ++i)
            {
                juce::Point<float> nearest;
                if (juce::Line<float>(points[i - 1], points[i]).getDistanceFromPoint(p, nearest) < 7.0f)
                    return conn.connectionId;
            }
        }
        return {};
    }

    std::optional<WaypointHit> waypointAt(juce::Point<float> p) const
    {
        for (const auto& conn : owner.machine_.assembly.connections)
        {
            for (int i = 0; i < (int)conn.routePoints.size(); ++i)
            {
                const auto screen = worldToScreen(juce::Point<float> { (float)conn.routePoints[(size_t)i].x, (float)conn.routePoints[(size_t)i].y });
                if (screen.getDistanceFrom(p) <= 8.0f)
                    return WaypointHit { conn.connectionId, i };
            }
        }
        return std::nullopt;
    }

    bool isCompatibleDropTarget(const EndpointRect& endpoint) const
    {
        if (!draggingWire || !wireStart.has_value())
            return false;
        if (wireStart->path() == endpoint.path())
            return false;
        return wireStart->canSource() && endpoint.canDest()
            && domainsCompatible(wireStart->port.domain, endpoint.port.domain);
    }

    void completeWire(const EndpointRect& start, const EndpointRect& target)
    {
        if (start.path() == target.path() || !start.canSource() || !target.canDest())
            return;
        if (!domainsCompatible(start.port.domain, target.port.domain))
        {
            owner.statusLabel.setText("Those ports have incompatible domains.", juce::dontSendNotification);
            owner.statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffff6b6b));
            return;
        }

        std::string err;
        if (!owner.machine_.connectPorts(start.instanceId, start.portName, target.instanceId, target.portName, {}, &err))
        {
            owner.statusLabel.setText("Connect failed: " + juce::String(err), juce::dontSendNotification);
            owner.statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffff6b6b));
            return;
        }

        owner.selectedConnectionId_ = start.instanceId + "." + start.portName + "__to__" + target.instanceId + "." + target.portName;
        owner.selectedInstanceId_.clear();
        selectedWaypointIndex = -1;
        owner.syncAfterModelEdit();
    }

    sim_panel::SignalConnection* findConnection(const std::string& id)
    {
        for (auto& conn : owner.machine_.assembly.connections)
            if (conn.connectionId == id)
                return &conn;
        return nullptr;
    }

    const sim_panel::SignalConnection* findConnection(const std::string& id) const
    {
        for (const auto& conn : owner.machine_.assembly.connections)
            if (conn.connectionId == id)
                return &conn;
        return nullptr;
    }

    sim_panel::SignalConnection* selectedConnection()
    {
        return owner.selectedConnectionId_.empty() ? nullptr : findConnection(owner.selectedConnectionId_);
    }

    bool removeSelectedWaypoint()
    {
        auto* conn = selectedConnection();
        if (conn == nullptr || selectedWaypointIndex < 0 || selectedWaypointIndex >= (int)conn->routePoints.size())
            return false;
        conn->routePoints.erase(conn->routePoints.begin() + selectedWaypointIndex);
        selectedWaypointIndex = -1;
        owner.updateDiagnosticsDisplay();
        repaint();
        return true;
    }
};

MachineBuilderPanel::MachineBuilderPanel()
    : MachineBuilderPanel(nullptr, nullptr, nullptr)
{
}

MachineBuilderPanel::MachineBuilderPanel(std::shared_ptr<sim_panel::SystemSimulationSession> session,
                                           mechanical_scene::MechanicalViewComponent* mechView,
                                           sim_panel::ControlPanelComponent* ctrlPanel)
    : session_(session), mechanicalView_(mechView), controlPanel_(ctrlPanel),
      toolbarLookAndFeel_(std::make_unique<ToolbarIconLookAndFeel>())
{
    machine_ = MachineDesign::createBlankMachine("Custom Machine");

    canvas = std::make_unique<AssemblyCanvas>(*this);
    addAndMakeVisible(*canvas);

    titleLabel.setText("Machine Builder", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(13.5f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xff78dcca));
    addAndMakeVisible(titleLabel);

    auto registerToolbarButton = [this](const std::string& id,
                                        const std::string& group,
                                        juce::TextButton& b,
                                        const juce::String& tooltip) {
        b.getProperties().set("toolbarIcon", juce::String(id));
        b.getProperties().set("toolbarGroup", juce::String(group));
        b.setLookAndFeel(toolbarLookAndFeel_.get());
        b.setTooltip(tooltip);
        b.setMouseCursor(juce::MouseCursor::PointingHandCursor);
        addAndMakeVisible(b);
        toolbarItems_.push_back({ id, group, &b, 26 });
    };

    // File group
    registerToolbarButton("new_blank", "File", newBlankBtn, "New Blank Machine (Ctrl+N) - Clear diagram and reset simulation session");
    registerToolbarButton("load", "File", loadBtn, "Load Machine (Ctrl+O) - Open a saved .machine.json file");
    registerToolbarButton("save", "File", saveBtn, "Save Machine (Ctrl+S) - Save current machine assembly to .machine.json");

    // Diagram group
    registerToolbarButton("add_schematic", "Diagram", addSchematicBtn, "Add Schematic Subsystem - Import an electrical .diagram.json subsystem");
    registerToolbarButton("add_frust", "Diagram", addFrustBtn, "Add FRust Controller - Import a programmable .frcomp.json controller");
    registerToolbarButton("add_motor", "Diagram", addMotorBtn, "Add DC Motor - Place a built-in electro-mechanical DC motor subsystem");
    registerToolbarButton("add_plot", "Diagram", addPlotBtn, "Add Plot Panel - Create a passive multi-channel oscilloscope/strip-chart instrument");
    registerToolbarButton("add_input", "Diagram", addInputBtn, "Add System Input - Create an external machine input port and console control");
    registerToolbarButton("add_output", "Diagram", addOutputBtn, "Add System Output - Create an external machine output port and console readout");
    registerToolbarButton("remove", "Diagram", removeBtn, "Remove Selected (Del / Backspace) - Delete selected subsystem, port, or wire");

    // View group
    registerToolbarButton("fit_all", "View", fitAllBtn, "Fit All in View (F) - Frame the entire machine assembly in the diagram canvas");
    registerToolbarButton("zoom_in", "View", zoomInBtn, "Zoom In (+ / =) - Increase diagram canvas magnification");
    registerToolbarButton("zoom_out", "View", zoomOutBtn, "Zoom Out (-) - Decrease diagram canvas magnification");
    registerToolbarButton("reset_view", "View", resetViewBtn, "Reset View (0) - Reset diagram canvas zoom to 100% and origin");

    // Simulation group
    registerToolbarButton("compile", "Simulation", compileRunBtn, "Compile Machine (F7) - Validate and compile machine assembly into simulation session");
    registerToolbarButton("start", "Simulation", startBtn, "Start Simulation (F5) - Run compiled machine simulation");
    registerToolbarButton("stop", "Simulation", stopBtn, "Stop Simulation (Shift+F5) - Pause running machine simulation");
    registerToolbarButton("reset", "Simulation", resetBtn, "Reset Simulation - Reset simulation time and states to initial values");

    // Tools group
    registerToolbarButton("validate", "Tools", validateBtn, "Validate Machine - Run assembly topology and port compatibility checks");
    registerToolbarButton("open_model", "Tools", openReferenceBtn, "Open Referenced Model - Open selected schematic or FRust component in its editor");
    registerToolbarButton("apply", "Tools", applyInspectorBtn, "Apply Selection Edits - Commit inspector ID, title, reference, and parameter changes");

    overflowBtn.getProperties().set("toolbarIcon", "overflow");
    overflowBtn.setLookAndFeel(toolbarLookAndFeel_.get());
    overflowBtn.setTooltip("More Toolbar Commands - Show commands hidden by narrow window width");
    overflowBtn.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    addChildComponent(overflowBtn);

    newBlankBtn.onClick = [this] { resetToBlankMachine(); };
    addSchematicBtn.onClick = [this] { chooseSchematicFile(); };
    addFrustBtn.onClick = [this] { chooseFrustFile(); };
    addMotorBtn.onClick = [this] {
        std::string err;
        if (!addBuiltInMotor(&err))
            statusLabel.setText("Add motor failed: " + juce::String(err), juce::dontSendNotification);
    };
    addPlotBtn.onClick = [this] { showPlotPanelDialog(); };
    addInputBtn.onClick = [this] { showSystemPortDialog(true); };
    addOutputBtn.onClick = [this] { showSystemPortDialog(false); };
    removeBtn.onClick = [this] { removeSelected(); };
    saveBtn.onClick = [this] { chooseMachineSaveFile(); };
    loadBtn.onClick = [this] { chooseMachineLoadFile(); };
    fitAllBtn.onClick = [this] { fitMachineView(); };
    zoomInBtn.onClick = [this] { zoomInView(); };
    zoomOutBtn.onClick = [this] { zoomOutView(); };
    resetViewBtn.onClick = [this] { resetMachineView(); };
    validateBtn.onClick = [this] { updateDiagnosticsDisplay(); };
    compileRunBtn.onClick = [this] { compileAndRun(); };
    startBtn.onClick = [this] { startSimulation(); };
    stopBtn.onClick = [this] { stopSimulation(); };
    resetBtn.onClick = [this] { resetSimulation(); };
    applyInspectorBtn.onClick = [this] { applyInspectorEdits(); };
    openReferenceBtn.onClick = [this] { openSelectedReference(); };
    overflowBtn.onClick = [this] { showOverflowMenu(); };

    statusLabel.setText("Ready", juce::dontSendNotification);
    statusLabel.setJustificationType(juce::Justification::centredRight);
    statusLabel.setFont(juce::Font(11.5f, juce::Font::bold));
    statusLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(statusLabel);

    inspectorLabel.setText("Selection", juce::dontSendNotification);
    inspectorLabel.setColour(juce::Label::textColourId, juce::Colour(0xff9eb3c4));
    inspectorLabel.setFont(juce::Font(12.0f, juce::Font::bold));
    addAndMakeVisible(inspectorLabel);

    for (auto* editor : { &instanceIdEditor, &titleEditor, &referenceEditor, &paramsEditor, &liveSignalsEditor, &diagnosticsEditor })
    {
        editor->setMultiLine(editor == &paramsEditor || editor == &liveSignalsEditor || editor == &diagnosticsEditor);
        editor->setReadOnly(editor == &liveSignalsEditor || editor == &diagnosticsEditor);
        editor->setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));
        editor->setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff12171e));
        editor->setColour(juce::TextEditor::textColourId, juce::Colour(0xffdce9ee));
        editor->setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff2d3b48));
        addAndMakeVisible(*editor);
    }

    instanceIdEditor.setTextToShowWhenEmpty("instance id", juce::Colour(0xff6f8291));
    titleEditor.setTextToShowWhenEmpty("title", juce::Colour(0xff6f8291));
    referenceEditor.setTextToShowWhenEmpty("model reference", juce::Colour(0xff6f8291));
    paramsEditor.setTextToShowWhenEmpty("numeric parameters: name=value", juce::Colour(0xff6f8291));

    if (controlPanel_ != nullptr)
    {
        controlPanel_->onLayoutChanged = [this](const sim_panel::ControlPanelLayout& layout) {
            machine_.controlPanel = layout;
            for (const auto& widget : machine_.controlPanel.widgets)
            {
                if (widget.type != sim_panel::WidgetType::PlotPanel || widget.id.rfind("plot_", 0) != 0)
                    continue;
                const auto instanceId = widget.id.substr(5);
                for (auto& inst : machine_.assembly.instances)
                {
                    if (inst.instanceId != instanceId)
                        continue;
                    inst.textParams["mode"] = widget.plotMode;
                    inst.numericParams["timebase_seconds"] = widget.visibleHistorySeconds;
                    inst.numericParams["max_samples"] = (double)widget.maxSamples;
                    break;
                }
            }
            syncPlotPanelBindings();
            updateDiagnosticsDisplay();
        };
        controlPanel_->setLayout(machine_.controlPanel);
    }

    updateDiagnosticsDisplay();
    refreshUi();
    startTimerHz(10);
}

MachineBuilderPanel::~MachineBuilderPanel()
{
    for (auto& item : toolbarItems_)
        if (item.button != nullptr)
            item.button->setLookAndFeel(nullptr);
    overflowBtn.setLookAndFeel(nullptr);

    if (controlPanel_ != nullptr)
        controlPanel_->onLayoutChanged = nullptr;
    stopTimer();
}

void MachineBuilderPanel::setMachine(const MachineDesign& machine)
{
    machine_ = machine;
    selectedInstanceId_.clear();
    selectedConnectionId_.clear();
    syncPlotPanelBindings();
    updateDiagnosticsDisplay();
    refreshUi();
    if (mechanicalView_ != nullptr)
        mechanicalView_->setScene(machine_.scene);
    if (controlPanel_ != nullptr)
        controlPanel_->setLayout(machine_.controlPanel);
    if (canvas != nullptr)
        canvas->fitMachine();
}

void MachineBuilderPanel::loadTemplate(const std::string& templateName)
{
    juce::ignoreUnused(templateName);
    resetToBlankMachine();
}

bool MachineBuilderPanel::compileAndRun()
{
    std::string err;
    const auto baseFolder = referenceBaseFolder();
    if (!machine_.isValid(baseFolder))
    {
        statusLabel.setText("Validation failed. Check diagnostics.", juce::dontSendNotification);
        statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffff6b6b));
        updateDiagnosticsDisplay();
        return false;
    }

    if (session_ != nullptr)
    {
        if (!machine_.compileAndLoadIntoSession(*session_, &err, baseFolder))
        {
            statusLabel.setText("Compilation error: " + juce::String(err), juce::dontSendNotification);
            statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffff6b6b));
            return false;
        }
    }

    if (mechanicalView_ != nullptr)
        mechanicalView_->setScene(machine_.scene);
    if (controlPanel_ != nullptr)
        controlPanel_->setLayout(machine_.controlPanel);

    statusLabel.setText("Compiled. Use Start to run.", juce::dontSendNotification);
    statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xff80ed99));
    return true;
}

bool MachineBuilderPanel::startSimulation()
{
    if (session_ == nullptr)
        return false;
    const bool ok = session_->start();
    statusLabel.setText(ok ? "Simulation running" : "Simulation did not start", juce::dontSendNotification);
    statusLabel.setColour(juce::Label::textColourId, ok ? juce::Colour(0xff80ed99) : juce::Colour(0xffff6b6b));
    return ok;
}

void MachineBuilderPanel::stopSimulation()
{
    if (session_ != nullptr)
        session_->stop();
    statusLabel.setText("Simulation stopped", juce::dontSendNotification);
    statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffdce9ee));
}

void MachineBuilderPanel::resetSimulation()
{
    if (session_ != nullptr)
        session_->reset();
    statusLabel.setText("Simulation reset", juce::dontSendNotification);
    statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffdce9ee));
}

void MachineBuilderPanel::resetToBlankMachine()
{
    if (session_ != nullptr)
    {
        session_->stop();
        session_->setupCircuit(circuit_sim::Circuit{}, {}, {}, {});
        session_->reset();
    }

    currentMachineFile_ = {};
    machine_ = MachineDesign::createBlankMachine("Custom Machine");
    selectedInstanceId_.clear();
    selectedConnectionId_.clear();
    liveSignalsEditor.setText("t=0.000s stopped\n", juce::dontSendNotification);

    if (mechanicalView_ != nullptr)
        mechanicalView_->setScene(machine_.scene);
    if (controlPanel_ != nullptr)
        controlPanel_->setLayout(machine_.controlPanel);
    if (canvas != nullptr)
        canvas->resetView();

    updateDiagnosticsDisplay();
    refreshUi();
    repaint();
}

void MachineBuilderPanel::fitMachineView()
{
    if (canvas != nullptr)
        canvas->fitMachine();
}

void MachineBuilderPanel::zoomInView()
{
    if (canvas != nullptr)
        canvas->zoomBy(1.2f);
}

void MachineBuilderPanel::zoomOutView()
{
    if (canvas != nullptr)
        canvas->zoomBy(1.0f / 1.2f);
}

void MachineBuilderPanel::resetMachineView()
{
    if (canvas != nullptr)
        canvas->resetView();
}

std::vector<MachineBuilderPanel::ToolbarGroupSummary> MachineBuilderPanel::getToolbarGroupSummaries() const
{
    std::vector<ToolbarGroupSummary> groups;
    for (const auto& item : toolbarItems_)
    {
        auto it = std::find_if(groups.begin(), groups.end(),
                               [&](const auto& g) { return g.name == item.groupName; });
        if (it == groups.end())
        {
            ToolbarGroupSummary g;
            g.name = item.groupName;
            for (const auto& vis : visibleGroupSummaries_)
                if (vis.name == item.groupName)
                    g.headerBounds = vis.headerBounds;
            g.commandIds.push_back(item.id);
            groups.push_back(std::move(g));
        }
        else
        {
            it->commandIds.push_back(item.id);
        }
    }
    return groups;
}

int MachineBuilderPanel::getVisibleToolbarButtonCount() const
{
    int count = 0;
    for (const auto& item : toolbarItems_)
        if (item.button != nullptr && item.button->isVisible())
            ++count;
    return count;
}

int MachineBuilderPanel::getOverflowToolbarButtonCount() const
{
    int count = 0;
    for (const auto& item : toolbarItems_)
        if (item.button != nullptr && !item.button->isVisible())
            ++count;
    return count;
}

bool MachineBuilderPanel::isOverflowButtonVisible() const
{
    return overflowBtn.isVisible();
}

juce::Rectangle<int> MachineBuilderPanel::getCanvasBounds() const
{
    return canvas != nullptr ? canvas->getBounds() : juce::Rectangle<int>();
}

bool MachineBuilderPanel::triggerToolbarCommand(const std::string& commandId)
{
    for (const auto& item : toolbarItems_)
    {
        if (item.id == commandId && item.button != nullptr)
        {
            if (item.button->onClick != nullptr)
            {
                item.button->onClick();
                return true;
            }
            item.button->triggerClick();
            return true;
        }
    }
    return false;
}

juce::String MachineBuilderPanel::getToolbarCommandTooltip(const std::string& commandId) const
{
    for (const auto& item : toolbarItems_)
        if (item.id == commandId && item.button != nullptr)
            return item.button->getTooltip();
    return {};
}

bool MachineBuilderPanel::addSchematicSubsystemFromFile(const juce::File& file, std::string* error)
{
    sim_panel::SubsystemDefinition def;
    juce::String loadError;
    if (!sim_panel::SubsystemDefinition::loadSchematicDefinition(file, def, loadError))
    {
        if (error) *error = loadError.toStdString();
        return false;
    }

    const auto full = file.getFullPathName().toStdString();
    for (const auto& existing : machine_.assembly.definitions)
        if (existing.kind == sim_panel::SubsystemKind::SchematicReference && existing.referenceFile == full)
            def.id = existing.id;

    if (machine_.assembly.findDefinition(def.id) == nullptr)
    {
        def.id = uniqueDefinitionId(machine_.assembly.definitions, safeIdStem(file.getFileNameWithoutExtension()));
        def.referenceFile = full;
        machine_.assembly.definitions.push_back(def);
    }

    const auto instId = uniqueId(machine_.assembly.instances, def.id);
    std::string err;
    if (!machine_.addSubsystemInstance(instId, def.id, def.displayName, {}, &err))
    {
        if (error) *error = err;
        return false;
    }
    machine_.setInstanceLayout(instId, 260.0 + machine_.assembly.instances.size() * 35.0, 120.0 + machine_.assembly.instances.size() * 25.0, nullptr);
    selectInstance(instId);
    syncAfterModelEdit();
    return true;
}

bool MachineBuilderPanel::addFrustSubsystemFromFile(const juce::File& file, std::string* error)
{
    sim_panel::SubsystemDefinition def;
    juce::String loadError;
    if (!sim_panel::SubsystemDefinition::loadFrustComponentDefinition(file, def, loadError))
    {
        if (error) *error = loadError.toStdString();
        return false;
    }

    const auto full = file.getFullPathName().toStdString();
    for (const auto& existing : machine_.assembly.definitions)
        if (existing.kind == sim_panel::SubsystemKind::ProgrammableComponent && existing.referenceFile == full)
            def.id = existing.id;

    if (machine_.assembly.findDefinition(def.id) == nullptr)
    {
        def.id = uniqueDefinitionId(machine_.assembly.definitions, safeIdStem(file.getFileNameWithoutExtension()));
        def.referenceFile = full;
        machine_.assembly.definitions.push_back(def);
    }

    std::map<std::string, double> params = def.defaultNumericParams;
    const auto instId = uniqueId(machine_.assembly.instances, def.id == "SavedMotorController" ? "controller" : def.id);
    std::string err;
    if (!machine_.addSubsystemInstance(instId, def.id, def.displayName, params, &err))
    {
        if (error) *error = err;
        return false;
    }
    machine_.setInstanceLayout(instId, 500.0, 180.0 + machine_.assembly.instances.size() * 20.0, nullptr);
    selectInstance(instId);
    syncAfterModelEdit();
    return true;
}

bool MachineBuilderPanel::addBuiltInMotor(std::string* error)
{
    auto def = sim_panel::SystemAssembly::makeDCMotorDefinition();
    if (machine_.assembly.findDefinition(def.id) == nullptr)
        machine_.assembly.definitions.push_back(def);

    const auto instId = uniqueId(machine_.assembly.instances, "motor");
    std::string err;
    if (!machine_.addSubsystemInstance(instId, def.id, "DC Motor", def.defaultNumericParams, &err))
    {
        if (error) *error = err;
        return false;
    }
    machine_.setInstanceLayout(instId, 300.0, 260.0, nullptr);
    selectInstance(instId);
    syncAfterModelEdit();
    return true;
}

bool MachineBuilderPanel::addPlotPanel(int channelCount, std::string* error)
{
    channelCount = std::clamp(channelCount, 1, 16);
    auto def = sim_panel::SystemAssembly::makePlotPanelDefinition(channelCount);
    if (machine_.assembly.findDefinition(def.id) == nullptr)
        machine_.assembly.definitions.push_back(def);

    const auto instId = uniqueId(machine_.assembly.instances, "plot");
    std::string err;
    if (!machine_.addSubsystemInstance(instId, def.id, "Plot Panel", def.defaultNumericParams, &err))
    {
        if (error) *error = err;
        return false;
    }

    if (auto* inst = const_cast<sim_panel::SubsystemInstance*>(machine_.assembly.findInstance(instId)))
        inst->textParams["mode"] = "Strip Chart";

    machine_.setInstanceLayout(instId, 720.0, 120.0 + machine_.assembly.instances.size() * 22.0, nullptr);

    sim_panel::WidgetConfig widget;
    widget.id = plotWidgetId(instId);
    widget.title = "Plot Panel";
    widget.type = sim_panel::WidgetType::PlotPanel;
    widget.column = 1;
    widget.order = nextWidgetOrder(machine_.controlPanel, 1);
    widget.height = 260;
    widget.panelId = machine_.controlPanel.activePanelId.empty() ? "main" : machine_.controlPanel.activePanelId;
    widget.plotMode = "Strip Chart";
    widget.visibleHistorySeconds = 10.0;
    widget.maxSamples = 20000;
    widget.autoScroll = true;
    for (int i = 0; i < channelCount; ++i)
    {
        const auto n = juce::String(i + 1).toStdString();
        widget.plotChannels.push_back({ "ch" + n, "Channel " + n, "", "", defaultPlotColour(i), 1.0, 0.0, 0.0, 1.0, true });
    }
    machine_.controlPanel.ensureDefaultPanel();
    machine_.controlPanel.widgets.push_back(widget);

    selectInstance(instId);
    syncAfterModelEdit();
    return true;
}

bool MachineBuilderPanel::addSystemPort(const std::string& name, bool input, sim_panel::PortDomain domain,
                                        const std::string& unit, std::string* error)
{
    sim_panel::SubsystemPort port;
    port.name = name;
    port.direction = input ? sim_panel::PortDirection::Input : sim_panel::PortDirection::Output;
    port.domain = domain;
    port.unit = unit;
    port.description = input ? "External machine input" : "External machine output";

    std::string err;
    if (!machine_.addSystemPort(port, &err))
    {
        if (error) *error = err;
        return false;
    }

    sim_panel::WidgetConfig widget;
    widget.id = safeIdStem(juce::String(name).toLowerCase());
    widget.title = widgetTitleForSystemPort(name);
    widget.signalName = "system." + name;
    widget.unit = unit;
    widget.type = input ? widgetTypeForSystemInput(name) : sim_panel::WidgetType::DigitalReadout;
    widget.column = input ? 0 : 1;
    widget.order = nextWidgetOrder(machine_.controlPanel, widget.column);
    widget.height = widget.type == sim_panel::WidgetType::Slider ? 80 : 48;
    widget.minValue = 0.0;
    widget.maxValue = unit == "rad/s" ? 600.0 : 1.0;
    widget.defaultValue = 0.0;
    widget.stepSize = unit == "rad/s" ? 5.0 : 1.0;
    machine_.controlPanel.widgets.push_back(widget);

    if (input)
    {
        if (widget.type == sim_panel::WidgetType::ToggleSwitch || widget.type == sim_panel::WidgetType::PushButton
            || widget.type == sim_panel::WidgetType::EmergencyStop)
            machine_.simConfig.initialBooleanInputs[widget.signalName] = false;
        else
            machine_.simConfig.initialNumericInputs[widget.signalName] = widget.defaultValue;
    }

    selectedInstanceId_ = "system." + name;
    selectedConnectionId_.clear();
    syncAfterModelEdit();
    return true;
}

bool MachineBuilderPanel::connectPorts(const std::string& fromInstance, const std::string& fromPort,
                                       const std::string& toInstance, const std::string& toPort,
                                       std::string* error)
{
    std::string err;
    if (!machine_.connectPorts(fromInstance, fromPort, toInstance, toPort, {}, &err))
    {
        if (error) *error = err;
        return false;
    }
    selectedConnectionId_ = fromInstance + "." + fromPort + "__to__" + toInstance + "." + toPort;
    selectedInstanceId_.clear();
    syncAfterModelEdit();
    return true;
}

bool MachineBuilderPanel::saveMachineToFile(const juce::File& file, juce::String& error)
{
    syncPlotPanelBindings();
    if (!machine_.saveToFile(file, error))
        return false;
    currentMachineFile_ = file;
    updateDiagnosticsDisplay();
    return true;
}

bool MachineBuilderPanel::loadMachineFromFile(const juce::File& file, juce::String& error)
{
    MachineDesign loaded;
    if (!MachineDesign::loadFromFile(file, loaded, error))
        return false;
    currentMachineFile_ = file;
    setMachine(loaded);
    return true;
}

void MachineBuilderPanel::syncAfterModelEdit()
{
    syncPlotPanelBindings();
    updateDiagnosticsDisplay();
    refreshUi();
    if (canvas != nullptr)
        canvas->repaint();
    if (controlPanel_ != nullptr)
        controlPanel_->setLayout(machine_.controlPanel);
}

void MachineBuilderPanel::normalizePlotPanelInstance(const std::string& instanceId)
{
    auto* inst = const_cast<sim_panel::SubsystemInstance*>(machine_.assembly.findInstance(instanceId));
    if (inst == nullptr)
        return;
    const auto* def = machine_.assembly.findDefinition(inst->definitionId);
    if (def == nullptr || !isPlotDefinition(*def))
        return;

    const int channelCount = std::clamp((int)std::round(inst->getParam("channels", (double)def->ports.size())), 1, 16);
    if ((int)def->ports.size() != channelCount)
    {
        auto resizedDef = sim_panel::SystemAssembly::makePlotPanelDefinition(channelCount);
        if (machine_.assembly.findDefinition(resizedDef.id) == nullptr)
            machine_.assembly.definitions.push_back(resizedDef);
        inst->definitionId = resizedDef.id;
        machine_.assembly.connections.erase(
            std::remove_if(machine_.assembly.connections.begin(), machine_.assembly.connections.end(),
                           [&](const auto& conn) {
                               return conn.toInstance == instanceId && plotChannelIndexFromPort(conn.toPort) >= channelCount;
                           }),
            machine_.assembly.connections.end());
    }

    auto widgetIt = std::find_if(machine_.controlPanel.widgets.begin(), machine_.controlPanel.widgets.end(),
                                 [&](const auto& w) { return w.id == plotWidgetId(instanceId); });
    if (widgetIt == machine_.controlPanel.widgets.end())
    {
        sim_panel::WidgetConfig widget;
        widget.id = plotWidgetId(instanceId);
        widget.title = inst->title.empty() ? "Plot Panel" : inst->title;
        widget.type = sim_panel::WidgetType::PlotPanel;
        widget.column = 1;
        widget.order = nextWidgetOrder(machine_.controlPanel, 1);
        widget.height = 260;
        machine_.controlPanel.widgets.push_back(widget);
        widgetIt = std::prev(machine_.controlPanel.widgets.end());
    }

    widgetIt->type = sim_panel::WidgetType::PlotPanel;
    widgetIt->signalName.clear();
    widgetIt->height = std::max(widgetIt->height, 220);
    widgetIt->maxSamples = std::max(100, (int)std::round(inst->getParam("max_samples", (double)widgetIt->maxSamples)));
    widgetIt->visibleHistorySeconds = std::max(0.01, inst->getParam("timebase_seconds", widgetIt->visibleHistorySeconds));
    if (const auto mode = inst->getTextParam("mode", widgetIt->plotMode); !mode.empty())
        widgetIt->plotMode = mode;
    if ((int)widgetIt->plotChannels.size() != channelCount)
        widgetIt->plotChannels.resize((size_t)channelCount);
    for (int i = 0; i < channelCount; ++i)
    {
        auto& ch = widgetIt->plotChannels[(size_t)i];
        const auto n = juce::String(i + 1).toStdString();
        if (ch.id.empty())
            ch.id = "ch" + n;
        if (ch.title.empty())
            ch.title = "Channel " + n;
        if (ch.colourArgb == 0)
            ch.colourArgb = defaultPlotColour(i);
    }
}

void MachineBuilderPanel::syncPlotPanelBindings()
{
    machine_.controlPanel.ensureDefaultPanel();

    for (const auto& inst : machine_.assembly.instances)
        if (const auto* def = machine_.assembly.findDefinition(inst.definitionId); def != nullptr && isPlotDefinition(*def))
            normalizePlotPanelInstance(inst.instanceId);

    for (auto& widget : machine_.controlPanel.widgets)
    {
        if (widget.type != sim_panel::WidgetType::PlotPanel)
            continue;
        for (auto& ch : widget.plotChannels)
        {
            ch.signalName.clear();
            ch.unit.clear();
        }
    }

    for (const auto& conn : machine_.assembly.connections)
    {
        const auto* dstInst = machine_.assembly.findInstance(conn.toInstance);
        const auto* dstDef = dstInst != nullptr ? machine_.assembly.findDefinition(dstInst->definitionId) : nullptr;
        if (dstDef == nullptr || !isPlotDefinition(*dstDef))
            continue;

        const int chIndex = plotChannelIndexFromPort(conn.toPort);
        auto widgetIt = std::find_if(machine_.controlPanel.widgets.begin(), machine_.controlPanel.widgets.end(),
                                     [&](const auto& w) { return w.id == plotWidgetId(conn.toInstance); });
        if (widgetIt == machine_.controlPanel.widgets.end() || chIndex < 0 || chIndex >= (int)widgetIt->plotChannels.size())
            continue;

        auto& ch = widgetIt->plotChannels[(size_t)chIndex];
        ch.signalName = conn.sourcePath();
        ch.unit = unitForSignal(machine_, ch.signalName);
        if (ch.title.empty() || ch.title.rfind("Channel ", 0) == 0)
            ch.title = conn.sourcePath();
    }
}

void MachineBuilderPanel::selectInstance(const std::string& instanceId)
{
    selectedInstanceId_ = instanceId;
    selectedConnectionId_.clear();
    refreshUi();
    if (canvas != nullptr)
        canvas->repaint();
}

void MachineBuilderPanel::selectConnection(const std::string& connectionId)
{
    selectedConnectionId_ = connectionId;
    if (!connectionId.empty())
        selectedInstanceId_.clear();
    refreshUi();
    if (canvas != nullptr)
        canvas->repaint();
}

void MachineBuilderPanel::refreshUi()
{
    inspectorLabel.setText(selectedConnectionId_.empty() ? "Selection" : "Connection", juce::dontSendNotification);

    if (!selectedConnectionId_.empty())
    {
        const auto it = std::find_if(machine_.assembly.connections.begin(), machine_.assembly.connections.end(),
                                     [&](const auto& c) { return c.connectionId == selectedConnectionId_; });
        instanceIdEditor.setText(it != machine_.assembly.connections.end() ? it->connectionId : "", juce::dontSendNotification);
        titleEditor.setText(it != machine_.assembly.connections.end() ? it->sourcePath() + " -> " + it->destPath() : "", juce::dontSendNotification);
        referenceEditor.clear();
        paramsEditor.clear();
        return;
    }

    const auto* inst = machine_.assembly.findInstance(selectedInstanceId_);
    if (inst == nullptr)
    {
        instanceIdEditor.setText(selectedInstanceId_, juce::dontSendNotification);
        titleEditor.clear();
        referenceEditor.clear();
        paramsEditor.clear();
        return;
    }

    instanceIdEditor.setText(inst->instanceId, juce::dontSendNotification);
    titleEditor.setText(inst->title, juce::dontSendNotification);

    juce::String ref;
    if (const auto* def = machine_.assembly.findDefinition(inst->definitionId))
        ref = def->referenceFile;
    referenceEditor.setText(ref, juce::dontSendNotification);

    juce::String params;
    if (const auto* def = machine_.assembly.findDefinition(inst->definitionId))
        for (const auto& [k, v] : def->defaultNumericParams)
            params << k << "=" << inst->getParam(k, v) << "\n";
    if (const auto* def = machine_.assembly.findDefinition(inst->definitionId))
        for (const auto& [k, v] : def->defaultTextParams)
            params << k << "=" << inst->getTextParam(k, v) << "\n";
    for (const auto& [k, v] : inst->numericParams)
        if (params.indexOf(juce::String(k) + "=") < 0)
            params << k << "=" << v << "\n";
    for (const auto& [k, v] : inst->textParams)
        if (params.indexOf(juce::String(k) + "=") < 0)
            params << k << "=" << v << "\n";
    paramsEditor.setText(params.trimEnd(), juce::dontSendNotification);
}

void MachineBuilderPanel::applyInspectorEdits()
{
    if (selectedInstanceId_.empty() || selectedInstanceId_.rfind("system.", 0) == 0)
        return;

    sim_panel::SubsystemInstance* inst = nullptr;
    for (auto& candidate : machine_.assembly.instances)
        if (candidate.instanceId == selectedInstanceId_)
            inst = &candidate;
    if (inst == nullptr)
        return;

    const auto newId = instanceIdEditor.getText().trim().toStdString();
    const auto newTitle = titleEditor.getText().trim().toStdString();
    std::string err;
    if (newId != inst->instanceId || newTitle != inst->title)
    {
        if (!machine_.renameSubsystemInstance(inst->instanceId, newId, newTitle, &err))
        {
            statusLabel.setText("Rename failed: " + juce::String(err), juce::dontSendNotification);
            statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffff6b6b));
            return;
        }
        selectedInstanceId_ = newId;
    }

    const auto* selected = machine_.assembly.findInstance(selectedInstanceId_);
    if (selected == nullptr)
        return;

    for (auto line : juce::StringArray::fromLines(paramsEditor.getText()))
    {
        line = line.trim();
        if (line.isEmpty())
            continue;
        const auto eq = line.indexOfChar('=');
        if (eq <= 0)
            continue;
        const auto key = line.substring(0, eq).trim().toStdString();
        const auto valueText = line.substring(eq + 1).trim();
        if (key == "mode")
        {
            for (auto& candidate : machine_.assembly.instances)
                if (candidate.instanceId == selectedInstanceId_)
                    candidate.textParams[key] = valueText.toStdString();
        }
        else
        {
            const double value = valueText.getDoubleValue();
            machine_.setInstanceParameter(selectedInstanceId_, key, value, nullptr);
        }
    }

    if (const auto refText = referenceEditor.getText().trim(); refText.isNotEmpty())
        if (auto* def = const_cast<sim_panel::SubsystemDefinition*>(machine_.assembly.findDefinition(selected->definitionId)); def != nullptr)
            def->referenceFile = refText.toStdString();

    normalizePlotPanelInstance(selectedInstanceId_);
    syncAfterModelEdit();
}

void MachineBuilderPanel::openSelectedReference()
{
    const auto* inst = machine_.assembly.findInstance(selectedInstanceId_);
    if (inst == nullptr)
        return;
    const auto* def = machine_.assembly.findDefinition(inst->definitionId);
    if (def == nullptr || def->referenceFile.empty())
        return;

    const auto ref = resolveReference(def->referenceFile);
    if (def->kind == sim_panel::SubsystemKind::SchematicReference && openSchematicReference != nullptr)
        openSchematicReference(ref);
    else if (def->kind == sim_panel::SubsystemKind::ProgrammableComponent && openComponentReference != nullptr)
        openComponentReference(ref);
}

void MachineBuilderPanel::removeSelected()
{
    std::string err;
    if (!selectedConnectionId_.empty())
    {
        auto it = std::find_if(machine_.assembly.connections.begin(), machine_.assembly.connections.end(),
                               [&](const auto& c) { return c.connectionId == selectedConnectionId_; });
        if (it != machine_.assembly.connections.end())
            machine_.assembly.connections.erase(it);
        selectedConnectionId_.clear();
        syncAfterModelEdit();
        return;
    }

    if (selectedInstanceId_.rfind("system.", 0) == 0)
    {
        if (!machine_.removeSystemPort(selectedInstanceId_.substr(7), &err))
            statusLabel.setText("Remove failed: " + juce::String(err), juce::dontSendNotification);
        selectedInstanceId_.clear();
        syncAfterModelEdit();
        return;
    }

    if (!selectedInstanceId_.empty() && !machine_.removeSubsystemInstance(selectedInstanceId_, &err))
    {
        statusLabel.setText("Remove failed: " + juce::String(err), juce::dontSendNotification);
        statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffff6b6b));
        return;
    }
    selectedInstanceId_.clear();
    syncAfterModelEdit();
}

void MachineBuilderPanel::chooseSchematicFile()
{
    fileChooser = std::make_unique<juce::FileChooser>("Add schematic subsystem", referenceBaseFolder(), "*.diagram.json;*.json");
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                             [safe = juce::Component::SafePointer<MachineBuilderPanel>(this)](const juce::FileChooser& chooser) {
        if (safe == nullptr)
            return;
        const auto file = chooser.getResult();
        if (file == juce::File())
            return;
        std::string err;
        if (!safe->addSchematicSubsystemFromFile(file, &err))
            safe->statusLabel.setText("Add schematic failed: " + juce::String(err), juce::dontSendNotification);
    });
}

void MachineBuilderPanel::chooseFrustFile()
{
    fileChooser = std::make_unique<juce::FileChooser>("Add FRust component", referenceBaseFolder(), "*.frcomp.json;*.json");
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                             [safe = juce::Component::SafePointer<MachineBuilderPanel>(this)](const juce::FileChooser& chooser) {
        if (safe == nullptr)
            return;
        const auto file = chooser.getResult();
        if (file == juce::File())
            return;
        std::string err;
        if (!safe->addFrustSubsystemFromFile(file, &err))
            safe->statusLabel.setText("Add FRust failed: " + juce::String(err), juce::dontSendNotification);
    });
}

void MachineBuilderPanel::showPlotPanelDialog()
{
    auto* dialog = new juce::AlertWindow("Add Plot Panel",
                                        "Create a passive N-channel plot instrument.",
                                        juce::MessageBoxIconType::NoIcon);
    dialog->addTextEditor("channels", "4", "Channels");
    if (auto* editor = dialog->getTextEditor("channels"))
    {
        editor->setWantsKeyboardFocus(true);
        editor->setMouseClickGrabsKeyboardFocus(true);
        editor->setEscapeAndReturnKeysConsumed(false);
        editor->grabKeyboardFocus();
    }
    dialog->addButton("Add", 1, juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    dialog->enterModalState(true, juce::ModalCallbackFunction::create(
        [safe = juce::Component::SafePointer<MachineBuilderPanel>(this), dialog](int result) {
            std::unique_ptr<juce::AlertWindow> cleanup(dialog);
            if (safe == nullptr || result != 1)
                return;
            const int channels = std::clamp(dialog->getTextEditorContents("channels").getIntValue(), 1, 16);
            std::string err;
            if (!safe->addPlotPanel(channels, &err))
                safe->statusLabel.setText("Add plot failed: " + juce::String(err), juce::dontSendNotification);
        }), true);
}

void MachineBuilderPanel::chooseMachineSaveFile()
{
    const auto start = currentMachineFile_ != juce::File() ? currentMachineFile_
                                                           : referenceBaseFolder().getChildFile("machine.machine.json");
    fileChooser = std::make_unique<juce::FileChooser>("Save machine", start, "*.machine.json;*.json");
    fileChooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                             [safe = juce::Component::SafePointer<MachineBuilderPanel>(this)](const juce::FileChooser& chooser) {
        if (safe == nullptr)
            return;
        const auto file = chooser.getResult();
        if (file == juce::File())
            return;
        juce::String error;
        if (!safe->saveMachineToFile(file, error))
            safe->statusLabel.setText("Save failed: " + error, juce::dontSendNotification);
        else
            safe->statusLabel.setText("Saved " + file.getFileName(), juce::dontSendNotification);
    });
}

void MachineBuilderPanel::chooseMachineLoadFile()
{
    fileChooser = std::make_unique<juce::FileChooser>("Load machine", referenceBaseFolder(), "*.machine.json;*.json");
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                             [safe = juce::Component::SafePointer<MachineBuilderPanel>(this)](const juce::FileChooser& chooser) {
        if (safe == nullptr)
            return;
        const auto file = chooser.getResult();
        if (file == juce::File())
            return;
        juce::String error;
        if (!safe->loadMachineFromFile(file, error))
            safe->statusLabel.setText("Load failed: " + error, juce::dontSendNotification);
    });
}

void MachineBuilderPanel::showSystemPortDialog(bool input)
{
    auto* dialog = new juce::AlertWindow(input ? "Add Machine Input" : "Add Machine Output",
                                        "Create an external system port on this machine.",
                                        juce::MessageBoxIconType::NoIcon);
    dialog->addTextEditor("name", input ? "input1" : "output1", "Port name");
    dialog->addTextEditor("unit", "V", "Unit");
    if (auto* nameEditor = dialog->getTextEditor("name"))
    {
        nameEditor->setWantsKeyboardFocus(true);
        nameEditor->setMouseClickGrabsKeyboardFocus(true);
        nameEditor->setEscapeAndReturnKeysConsumed(false);
    }
    if (auto* unitEditor = dialog->getTextEditor("unit"))
    {
        unitEditor->setWantsKeyboardFocus(true);
        unitEditor->setMouseClickGrabsKeyboardFocus(true);
        unitEditor->setEscapeAndReturnKeysConsumed(false);
    }
    dialog->addComboBox("domain", { "Signal/control", "Electrical", "Mechanical" }, "Domain");
    if (auto* combo = dialog->getComboBoxComponent("domain"))
        combo->setSelectedId(1, juce::dontSendNotification);
    dialog->addButton("Add", 1, juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    if (auto* nameEditor = dialog->getTextEditor("name"))
        nameEditor->grabKeyboardFocus();
    dialog->enterModalState(true, juce::ModalCallbackFunction::create(
        [safe = juce::Component::SafePointer<MachineBuilderPanel>(this), dialog, input](int result) {
            std::unique_ptr<juce::AlertWindow> cleanup(dialog);
            if (safe == nullptr || result != 1)
                return;
            const auto name = dialog->getTextEditorContents("name").trim().toStdString();
            const auto unit = dialog->getTextEditorContents("unit").trim().toStdString();
            auto domain = sim_panel::PortDomain::SignalControl;
            if (auto* combo = dialog->getComboBoxComponent("domain"))
            {
                if (combo->getSelectedId() == 2) domain = sim_panel::PortDomain::Electrical;
                else if (combo->getSelectedId() == 3) domain = sim_panel::PortDomain::Mechanical;
            }
            std::string err;
            if (!safe->addSystemPort(name, input, domain, unit, &err))
                safe->statusLabel.setText("Add port failed: " + juce::String(err), juce::dontSendNotification);
        }), true);
}

juce::File MachineBuilderPanel::referenceBaseFolder() const
{
    if (currentMachineFile_ != juce::File())
        return currentMachineFile_.getParentDirectory();
    return juce::File::getCurrentWorkingDirectory();
}

juce::File MachineBuilderPanel::resolveReference(const std::string& referenceFile) const
{
    const juce::String ref(referenceFile);
    if (ref.isEmpty())
        return {};
    if (juce::File::isAbsolutePath(ref))
        return juce::File(ref);
    return referenceBaseFolder().getChildFile(ref);
}

void MachineBuilderPanel::updateDiagnosticsDisplay()
{
    const auto diags = machine_.validate(referenceBaseFolder());
    juce::String report;
    report << "Machine: " << machine_.name << " (v" << machine_.version << ")\n";
    if (currentMachineFile_ != juce::File())
        report << "File: " << currentMachineFile_.getFullPathName() << "\n";
    report << "Subsystem Instances: " << (int)machine_.assembly.instances.size() << "\n";
    report << "Inter-Connections: " << (int)machine_.assembly.connections.size() << "\n";
    report << "System Ports: " << (int)machine_.assembly.systemPorts.size() << "\n";
    report << "Control Widgets: " << (int)machine_.controlPanel.widgets.size() << "\n";
    report << "--------------------------------------------------------\n";

    if (diags.empty())
    {
        report << "[PASS] All validation checks passed.\n";
        statusLabel.setText("Valid", juce::dontSendNotification);
        statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xff80ed99));
    }
    else
    {
        int errCount = 0, warnCount = 0;
        for (const auto& d : diags)
        {
            if (d.level == Diagnostic::Level::Error) ++errCount; else ++warnCount;
            report << (d.level == Diagnostic::Level::Error ? "[ERROR] " : "[WARN]  ");
            report << "[" << d.component << "] ";
            if (!d.targetId.empty()) report << "(" << d.targetId << ") ";
            report << d.message << "\n";
        }
        report << "--------------------------------------------------------\n";
        report << "Summary: " << errCount << " error(s), " << warnCount << " warning(s).\n";
        statusLabel.setText(errCount > 0 ? juce::String(errCount) + " Error(s)"
                                         : "Valid (" + juce::String(warnCount) + " Warning(s))",
                            juce::dontSendNotification);
        statusLabel.setColour(juce::Label::textColourId, errCount > 0 ? juce::Colour(0xffff6b6b)
                                                                       : juce::Colour(0xffffd166));
    }

    diagnosticsEditor.setText(report, juce::dontSendNotification);
}

void MachineBuilderPanel::timerCallback()
{
    if (session_ == nullptr)
        return;

    const auto snap = session_->getSnapshot();
    juce::String text;
    text << "t=" << juce::String(snap.simTime, 3) << "s";
    text << (snap.running ? " running" : " stopped") << "\n";

    const auto prefix = selectedInstanceId_.empty() || selectedInstanceId_.rfind("system.", 0) == 0
                      ? juce::String()
                      : juce::String(selectedInstanceId_) + ".";
    int shown = 0;
    for (const auto& [name, value] : snap.numericSignals)
    {
        if (prefix.isEmpty() || juce::String(name).startsWith(prefix))
        {
            text << name << " = " << juce::String(value, 6) << "\n";
            if (++shown >= 12) break;
        }
    }
    liveSignalsEditor.setText(text, juce::dontSendNotification);
}

void MachineBuilderPanel::showOverflowMenu()
{
    juce::PopupMenu menu;
    std::string currentGroup;
    for (size_t i = 0; i < toolbarItems_.size(); ++i)
    {
        const auto& item = toolbarItems_[i];
        if (item.button == nullptr || item.button->isVisible())
            continue;
        if (item.groupName != currentGroup)
        {
            currentGroup = item.groupName;
            menu.addSectionHeader(currentGroup);
        }
        menu.addItem((int)i + 1, item.button->getButtonText(), item.button->isEnabled());
    }

    if (menu.getNumItems() == 0)
        return;

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&overflowBtn),
                       [safe = juce::Component::SafePointer<MachineBuilderPanel>(this)](int result) {
                           if (safe == nullptr || result <= 0)
                               return;
                           const size_t idx = (size_t)(result - 1);
                           if (idx < safe->toolbarItems_.size() && safe->toolbarItems_[idx].button != nullptr)
                           {
                               auto* btn = safe->toolbarItems_[idx].button;
                               if (btn->onClick != nullptr)
                                   btn->onClick();
                               else
                                   btn->triggerClick();
                           }
                       });
}

void MachineBuilderPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff18222b));

    if (!toolbarBounds_.isEmpty())
    {
        const auto tb = toolbarBounds_.toFloat();
        g.setColour(juce::Colour(0xff121a22));
        g.fillRoundedRectangle(tb, 4.0f);
        g.setColour(juce::Colour(0xff293846));
        g.drawRoundedRectangle(tb, 4.0f, 1.0f);

        g.setFont(juce::Font(9.5f, juce::Font::bold));
        g.setColour(juce::Colour(0xff7fa1b8));
        for (const auto& grp : visibleGroupSummaries_)
        {
            if (!grp.headerBounds.isEmpty())
                g.drawFittedText(grp.name, grp.headerBounds, juce::Justification::centredLeft, 1);
        }

        g.setColour(juce::Colour(0xff2c3d4d));
        for (const int sepX : toolbarSeparators_)
            g.drawVerticalLine(sepX, tb.getY() + 4.0f, tb.getBottom() - 4.0f);
    }

    g.setColour(juce::Colour(0xff2d3b48));
    g.drawRect(getLocalBounds(), 1);
}

void MachineBuilderPanel::resized()
{
    auto area = getLocalBounds().reduced(6);
    toolbarBounds_ = area.removeFromTop(38);
    visibleGroupSummaries_.clear();
    toolbarSeparators_.clear();

    constexpr int buttonHeight = 22;
    constexpr int intraGap = 3;
    constexpr int groupGap = 11;
    const int headerY = toolbarBounds_.getY() + 2;
    const int headerH = 11;
    const int buttonY = toolbarBounds_.getY() + 13;

    const int statusWidth = toolbarBounds_.getWidth() >= 540
                          ? std::clamp(toolbarBounds_.getWidth() / 6, 96, 170)
                          : 0;
    if (statusWidth > 0)
    {
        statusLabel.setVisible(true);
        statusLabel.setBounds(toolbarBounds_.getRight() - statusWidth - 8,
                              toolbarBounds_.getY() + 4,
                              statusWidth,
                              toolbarBounds_.getHeight() - 8);
    }
    else
    {
        statusLabel.setVisible(false);
        statusLabel.setBounds(0, 0, 0, 0);
    }

    const int startX = toolbarBounds_.getX() + 6;
    const int availRight = (statusWidth > 0 ? statusLabel.getX() - 6 : toolbarBounds_.getRight() - 6);

    auto measureTotalRight = [&](size_t count) {
        int x = startX;
        std::string prevGroup;
        for (size_t i = 0; i < count; ++i)
        {
            const auto& item = toolbarItems_[i];
            if (!prevGroup.empty())
                x += (item.groupName == prevGroup ? intraGap : groupGap);
            x += item.width;
            prevGroup = item.groupName;
        }
        return x;
    };

    const bool allFit = measureTotalRight(toolbarItems_.size()) <= availRight;
    const int maxButtonRight = allFit ? availRight : std::max(startX + 26, availRight - 30);

    int x = startX;
    int lastVisibleRight = startX;
    std::string prevVisibleGroup;
    bool overflowing = false;

    for (size_t i = 0; i < toolbarItems_.size(); ++i)
    {
        auto& item = toolbarItems_[i];
        if (item.button == nullptr)
            continue;

        const bool newGroup = !prevVisibleGroup.empty() && item.groupName != prevVisibleGroup;
        const int nextX = prevVisibleGroup.empty() ? x : x + (newGroup ? groupGap : intraGap);

        if (!overflowing && (nextX + item.width <= maxButtonRight || i == 0))
        {
            if (newGroup)
                toolbarSeparators_.push_back(x + groupGap / 2);

            item.button->setVisible(true);
            item.button->setBounds(nextX, buttonY, item.width, buttonHeight);
            x = nextX + item.width;
            lastVisibleRight = x;
            prevVisibleGroup = item.groupName;

            if (visibleGroupSummaries_.empty() || visibleGroupSummaries_.back().name != item.groupName)
            {
                ToolbarGroupSummary grp;
                grp.name = item.groupName;
                grp.commandIds.push_back(item.id);
                grp.headerBounds = { nextX, headerY, item.width, headerH };
                visibleGroupSummaries_.push_back(std::move(grp));
            }
            else
            {
                auto& grp = visibleGroupSummaries_.back();
                grp.commandIds.push_back(item.id);
                grp.headerBounds.setWidth(x - grp.headerBounds.getX());
            }
        }
        else
        {
            overflowing = true;
            item.button->setVisible(false);
            item.button->setBounds(0, 0, 0, 0);
        }
    }

    if (overflowing)
    {
        overflowBtn.setVisible(true);
        overflowBtn.setBounds(lastVisibleRight + 4, buttonY, 26, buttonHeight);
    }
    else
    {
        overflowBtn.setVisible(false);
        overflowBtn.setBounds(0, 0, 0, 0);
    }

    area.removeFromTop(6);
    const int sideWidth = std::clamp(area.getWidth() / 5, 192, 224);
    auto side = area.removeFromLeft(sideWidth);
    area.removeFromLeft(6);

    titleLabel.setBounds(side.removeFromTop(18));
    side.removeFromTop(4);
    inspectorLabel.setBounds(side.removeFromTop(18));
    side.removeFromTop(3);
    instanceIdEditor.setBounds(side.removeFromTop(22));
    side.removeFromTop(3);
    titleEditor.setBounds(side.removeFromTop(22));
    side.removeFromTop(3);
    referenceEditor.setBounds(side.removeFromTop(22));
    side.removeFromTop(4);
    paramsEditor.setBounds(side.removeFromTop(108));
    side.removeFromTop(6);
    liveSignalsEditor.setBounds(side.removeFromTop(120));
    side.removeFromTop(6);
    diagnosticsEditor.setBounds(side);

    if (canvas != nullptr)
        canvas->setBounds(area);
}

} // namespace machine_builder
