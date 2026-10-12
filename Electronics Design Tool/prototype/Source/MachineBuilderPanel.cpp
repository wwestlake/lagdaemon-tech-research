#include "MachineBuilderPanel.h"

#include <algorithm>
#include <cmath>
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
    return d == D::Electrical ? "electrical" : d == D::Mechanical ? "mechanical" : "signal";
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
    if (a == b)
        return true;
    const auto electricalLike = [](sim_panel::PortDomain d) {
        return d == sim_panel::PortDomain::Electrical || d == sim_panel::PortDomain::SignalControl;
    };
    return electricalLike(a) && electricalLike(b);
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

        if (key != juce::KeyPress::deleteKey && key != juce::KeyPress::backspaceKey)
            return false;

        if (removeSelectedWaypoint())
            return true;

        if (!owner.selectedConnectionId_.empty() || !owner.selectedInstanceId_.empty())
        {
            owner.removeSelected();
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
    : session_(session), mechanicalView_(mechView), controlPanel_(ctrlPanel)
{
    machine_ = MachineDesign::createBlankMachine("Custom Machine");

    canvas = std::make_unique<AssemblyCanvas>(*this);
    addAndMakeVisible(*canvas);

    titleLabel.setText("Virtual Machine Builder", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(18.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xff78dcca));
    addAndMakeVisible(titleLabel);

    auto addButton = [this](juce::TextButton& b) {
        b.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff22313d));
        b.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffdce9ee));
        addAndMakeVisible(b);
    };

    for (auto* b : { &newBlankBtn, &addSchematicBtn, &addFrustBtn, &addMotorBtn, &addInputBtn, &addOutputBtn,
                    &removeBtn, &saveBtn, &loadBtn, &fitAllBtn, &validateBtn, &compileRunBtn, &startBtn, &stopBtn,
                    &resetBtn, &applyInspectorBtn, &openReferenceBtn })
        addButton(*b);

    newBlankBtn.onClick = [this] { resetToBlankMachine(); };
    addSchematicBtn.onClick = [this] { chooseSchematicFile(); };
    addFrustBtn.onClick = [this] { chooseFrustFile(); };
    addMotorBtn.onClick = [this] {
        std::string err;
        if (!addBuiltInMotor(&err))
            statusLabel.setText("Add motor failed: " + juce::String(err), juce::dontSendNotification);
    };
    addInputBtn.onClick = [this] { showSystemPortDialog(true); };
    addOutputBtn.onClick = [this] { showSystemPortDialog(false); };
    removeBtn.onClick = [this] { removeSelected(); };
    saveBtn.onClick = [this] { chooseMachineSaveFile(); };
    loadBtn.onClick = [this] { chooseMachineLoadFile(); };
    fitAllBtn.onClick = [this] { fitMachineView(); };
    validateBtn.onClick = [this] { updateDiagnosticsDisplay(); };
    compileRunBtn.onClick = [this] { compileAndRun(); };
    startBtn.onClick = [this] { startSimulation(); };
    stopBtn.onClick = [this] { stopSimulation(); };
    resetBtn.onClick = [this] { resetSimulation(); };
    applyInspectorBtn.onClick = [this] { applyInspectorEdits(); };
    openReferenceBtn.onClick = [this] { openSelectedReference(); };

    statusLabel.setText("Ready", juce::dontSendNotification);
    statusLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(statusLabel);

    inspectorLabel.setText("Selection", juce::dontSendNotification);
    inspectorLabel.setColour(juce::Label::textColourId, juce::Colour(0xff9eb3c4));
    inspectorLabel.setFont(juce::Font(13.0f, juce::Font::bold));
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

    updateDiagnosticsDisplay();
    refreshUi();
    startTimerHz(10);
}

MachineBuilderPanel::~MachineBuilderPanel()
{
    stopTimer();
}

void MachineBuilderPanel::setMachine(const MachineDesign& machine)
{
    machine_ = machine;
    selectedInstanceId_.clear();
    selectedConnectionId_.clear();
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
    updateDiagnosticsDisplay();
    refreshUi();
    if (canvas != nullptr)
        canvas->repaint();
    if (controlPanel_ != nullptr)
        controlPanel_->setLayout(machine_.controlPanel);
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
    for (const auto& [k, v] : inst->numericParams)
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
        const double value = line.substring(eq + 1).trim().getDoubleValue();
        machine_.setInstanceParameter(selectedInstanceId_, key, value, nullptr);
    }

    if (const auto refText = referenceEditor.getText().trim(); refText.isNotEmpty())
        if (auto* def = const_cast<sim_panel::SubsystemDefinition*>(machine_.assembly.findDefinition(selected->definitionId)); def != nullptr)
            def->referenceFile = refText.toStdString();

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

void MachineBuilderPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff18222b));
    g.setColour(juce::Colour(0xff2d3b48));
    g.drawRect(getLocalBounds(), 1);
}

void MachineBuilderPanel::resized()
{
    auto area = getLocalBounds().reduced(8);
    auto topBar = area.removeFromTop(32);
    titleLabel.setBounds(topBar.removeFromLeft(250));
    statusLabel.setBounds(topBar.removeFromLeft(220));
    resetBtn.setBounds(topBar.removeFromRight(70));
    topBar.removeFromRight(6);
    stopBtn.setBounds(topBar.removeFromRight(70));
    topBar.removeFromRight(6);
    startBtn.setBounds(topBar.removeFromRight(70));
    topBar.removeFromRight(6);
    compileRunBtn.setBounds(topBar.removeFromRight(90));
    topBar.removeFromRight(6);
    fitAllBtn.setBounds(topBar.removeFromRight(80));
    topBar.removeFromRight(6);
    validateBtn.setBounds(topBar.removeFromRight(80));

    area.removeFromTop(6);
    auto side = area.removeFromLeft(286);
    area.removeFromLeft(8);

    auto row = side.removeFromTop(28);
    newBlankBtn.setBounds(row.removeFromLeft(90));
    row.removeFromLeft(6);
    loadBtn.setBounds(row.removeFromLeft(70));
    row.removeFromLeft(6);
    saveBtn.setBounds(row.removeFromLeft(70));

    side.removeFromTop(8);
    row = side.removeFromTop(28);
    addSchematicBtn.setBounds(row.removeFromLeft(132));
    row.removeFromLeft(6);
    addFrustBtn.setBounds(row.removeFromLeft(104));

    side.removeFromTop(6);
    row = side.removeFromTop(28);
    addMotorBtn.setBounds(row.removeFromLeft(90));
    row.removeFromLeft(6);
    addInputBtn.setBounds(row.removeFromLeft(86));
    row.removeFromLeft(6);
    addOutputBtn.setBounds(row.removeFromLeft(90));

    side.removeFromTop(6);
    row = side.removeFromTop(28);
    removeBtn.setBounds(row.removeFromLeft(90));
    row.removeFromLeft(6);
    openReferenceBtn.setBounds(row.removeFromLeft(112));
    row.removeFromLeft(6);
    applyInspectorBtn.setBounds(row.removeFromLeft(70));

    side.removeFromTop(10);
    inspectorLabel.setBounds(side.removeFromTop(20));
    side.removeFromTop(4);
    instanceIdEditor.setBounds(side.removeFromTop(24));
    side.removeFromTop(4);
    titleEditor.setBounds(side.removeFromTop(24));
    side.removeFromTop(4);
    referenceEditor.setBounds(side.removeFromTop(24));
    side.removeFromTop(4);
    paramsEditor.setBounds(side.removeFromTop(112));
    side.removeFromTop(8);
    liveSignalsEditor.setBounds(side.removeFromTop(130));
    side.removeFromTop(8);
    diagnosticsEditor.setBounds(side);

    if (canvas != nullptr)
        canvas->setBounds(area);
}

} // namespace machine_builder
