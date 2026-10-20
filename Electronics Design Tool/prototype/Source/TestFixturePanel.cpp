#include "TestFixturePanel.h"
#include "SchematicSymbols.h"
#include <cmath>

class TestFixturePanel::CanvasView final : public juce::Component
{
public:
    CanvasView(TestFixturePanel& p) : owner(p)
    {
        setWantsKeyboardFocus(true);
    }

    void loadCircuitData()
    {
        if (owner.callbacks.getCircuitData != nullptr)
            circuitData = owner.callbacks.getCircuitData(owner.currentSheet);
        repaint();
    }

    juce::Point<float> canvasToView(juce::Point<float> pt) const
    {
        return pt * zoom + viewOffset;
    }

    juce::Point<float> viewToCanvas(juce::Point<float> pt) const
    {
        return (pt - viewOffset) / zoom;
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff0d131a));

        // Draw grid
        const float gridSize = 24.0f * zoom;
        if (gridSize >= 8.0f)
        {
            g.setColour(juce::Colour(0xff18222d));
            const float startX = std::fmod(viewOffset.x, gridSize);
            const float startY = std::fmod(viewOffset.y, gridSize);
            for (float x = startX; x < (float)getWidth(); x += gridSize)
                g.drawVerticalLine((int)x, 0.0f, (float)getHeight());
            for (float y = startY; y < (float)getHeight(); y += gridSize)
                g.drawHorizontalLine((int)y, 0.0f, (float)getWidth());
        }

        g.saveState();
        g.addTransform(juce::AffineTransform::scale(zoom).translated(viewOffset.x, viewOffset.y));

        drawCircuit(g);
        drawFixtureConnections(g);
        drawEquipment(g);
        drawPendingWiring(g);

        g.restoreState();

        // Canvas overlay status
        g.setColour(juce::Colour(0xffe056fd).withAlpha(0.35f));
        g.drawRect(getLocalBounds(), 2);

        g.setColour(juce::Colour(0xff8a9ba8));
        g.setFont(juce::Font(12.0f));
        juce::String hint = "Test Fixture: " + owner.fixtureName;
        if (owner.currentInstancePath.isNotEmpty())
            hint += " [Sub-Fixture: " + owner.currentInstancePath + "]";
        hint += " — Drag equipment to move; drag terminals to wire; double-click equipment or sub-blocks to open; Delete removes selected.";
        g.drawText(hint, getLocalBounds().reduced(10).removeFromBottom(22), juce::Justification::centredLeft);
    }

    void drawCircuit(juce::Graphics& g)
    {
        const auto* s = owner.getSetup();
        const float dashPattern[] = { 6.0f, 4.0f };

        // 1. Draw Wires
        for (const auto& w : circuitData.wires)
        {
            bool isBroken = false;
            if (s != nullptr)
            {
                for (const auto& b : s->connectionBreaks)
                {
                    if (b.instancePath.equalsIgnoreCase(owner.currentInstancePath))
                    {
                        if (b.wireIndex >= 0 && b.wireIndex == w.wireIndex) { isBroken = true; break; }
                        if (b.nodeA.isNotEmpty() && b.nodeB.isNotEmpty())
                        {
                            if ((b.nodeA.equalsIgnoreCase(w.nodeA) && b.nodeB.equalsIgnoreCase(w.nodeB)) ||
                                (b.nodeA.equalsIgnoreCase(w.nodeB) && b.nodeB.equalsIgnoreCase(w.nodeA)))
                            {
                                isBroken = true;
                                break;
                            }
                        }
                    }
                }
            }

            if (isBroken)
            {
                g.setColour(juce::Colour(0xffe74c3c));
                g.drawDashedLine(juce::Line<float>(w.a, w.b), dashPattern, 2, 2.5f);
                const auto mid = (w.a + w.b) * 0.5f;
                g.fillEllipse(mid.x - 9.0f, mid.y - 9.0f, 18.0f, 18.0f);
                g.setColour(juce::Colours::white);
                g.setFont(juce::Font(11.0f, juce::Font::bold));
                g.drawText("✕", juce::Rectangle<float>(mid.x - 9.0f, mid.y - 9.0f, 18.0f, 18.0f).toNearestInt(), juce::Justification::centred);
                g.setColour(juce::Colour(0xffff7675));
                g.setFont(juce::Font(9.0f, juce::Font::bold));
                g.drawText("INTERRUPTED", juce::Rectangle<float>(mid.x - 40.0f, mid.y - 22.0f, 80.0f, 12.0f).toNearestInt(), juce::Justification::centred);
            }
            else
            {
                g.setColour(juce::Colour(0xff576574));
                g.drawLine(w.a.x, w.a.y, w.b.x, w.b.y, 2.0f);
            }
        }

        // 2. Draw Components
        for (const auto& comp : circuitData.components)
        {
            const bool isOverridden = (s != nullptr && s->inputMode == "replace" &&
                                       s->inputRefdes.equalsIgnoreCase(comp.refdes) &&
                                       owner.currentSheet.isEmpty());

            g.saveState();
            if (isOverridden)
                g.setOpacity(0.38f);

            if (comp.isSubBlock)
            {
                // Subdiagram block representation
                auto b = comp.bounds;
                g.setColour(juce::Colour(0xff1e2b37));
                g.fillRoundedRectangle(b, 5.0f);
                g.setColour(juce::Colour(0xff4b6584));
                g.drawRoundedRectangle(b, 5.0f, 1.5f);

                // Header band
                auto header = b.removeFromTop(24.0f);
                g.setColour(juce::Colour(0xff2d3e50));
                g.fillRoundedRectangle(header, 5.0f);
                g.setColour(juce::Colours::white);
                g.setFont(juce::Font(12.0f, juce::Font::bold));
                g.drawText(comp.refdes + ": " + comp.value, header.reduced(6, 2), juce::Justification::centredLeft);

                g.setColour(juce::Colour(0xffa5b1c2));
                g.setFont(juce::Font(10.0f, juce::Font::italic));
                g.drawText("[Double-Click to enter Sub-Fixture]", b.reduced(4), juce::Justification::centred);
            }
            else
            {
                auto sym = schematic::symbolFor(comp.symbolId);
                g.saveState();
                g.addTransform(juce::AffineTransform::rotation(juce::degreesToRadians(comp.rotation))
                                   .translated(comp.pos.x, comp.pos.y));
                schematic::drawSymbolArt(g, sym, comp.value);
                g.restoreState();

                // Labels
                g.setFont(juce::Font(11.0f, juce::Font::bold));
                g.setColour(juce::Colour(0xffd1d8e0));
                g.drawText(comp.refdes, (int)comp.pos.x - 30, (int)comp.pos.y - 28, 60, 14, juce::Justification::centred);
                g.setFont(juce::Font(10.0f));
                g.setColour(juce::Colour(0xff778ca3));
                g.drawText(comp.value, (int)comp.pos.x - 35, (int)comp.pos.y + 16, 70, 14, juce::Justification::centred);
            }

            // Pins
            for (const auto& pin : comp.pins)
            {
                bool isIsolated = false;
                if (s != nullptr)
                {
                    for (const auto& b : s->connectionBreaks)
                    {
                        if (b.instancePath.equalsIgnoreCase(owner.currentInstancePath) &&
                            b.pin.equalsIgnoreCase(pin.label))
                        {
                            isIsolated = true;
                            break;
                        }
                    }
                }

                if (isIsolated)
                {
                    g.setColour(juce::Colour(0xffe74c3c));
                    g.fillEllipse(pin.pos.x - 7.0f, pin.pos.y - 7.0f, 14.0f, 14.0f);
                    g.setColour(juce::Colours::white);
                    g.setFont(juce::Font(9.0f, juce::Font::bold));
                    g.drawText("✕", (int)pin.pos.x - 7, (int)pin.pos.y - 7, 14, 14, juce::Justification::centred);
                    g.setColour(juce::Colour(0xffff7675));
                    g.drawText("ISOLATED", (int)pin.pos.x - 25, (int)pin.pos.y - 18, 50, 10, juce::Justification::centred);
                }
                else
                {
                    g.setColour(juce::Colour(0xffff8a65));
                    g.drawEllipse(pin.pos.x - 3.0f, pin.pos.y - 3.0f, 6.0f, 6.0f, 1.2f);
                }
            }

            g.restoreState();

            // Source override badge
            if (isOverridden)
            {
                const juce::Rectangle<float> badgeBox(comp.pos.x - 80.0f, comp.pos.y - 42.0f, 160.0f, 22.0f);
                g.setColour(juce::Colour(0xff2d0d3d));
                g.fillRoundedRectangle(badgeBox, 4.0f);
                g.setColour(juce::Colour(0xffe056fd));
                g.drawRoundedRectangle(badgeBox, 4.0f, 1.5f);
                g.setFont(juce::Font(10.0f, juce::Font::bold));
                g.drawText("⚡ OVERRIDDEN BY FIXTURE", badgeBox, juce::Justification::centred);
            }
        }
    }

    void drawEquipment(juce::Graphics& g)
    {
        auto* s = owner.getSetup();
        if (s == nullptr) return;

        for (const auto& eq : s->equipment)
        {
            if (!eq.instancePath.equalsIgnoreCase(owner.currentInstancePath))
                continue;

            const auto bounds = eq.getBounds();
            const bool isSelected = (eq.id == selectedEquipmentId);

            // Shadow
            g.setColour(juce::Colours::black.withAlpha(0.4f));
            g.fillRoundedRectangle(bounds.translated(3.0f, 3.0f), 6.0f);

            // Body
            g.setColour(juce::Colour(0xff151d26));
            g.fillRoundedRectangle(bounds, 6.0f);
            g.setColour(isSelected ? juce::Colour(0xff00d2d3) : juce::Colour(0xff2c3a4a));
            g.drawRoundedRectangle(bounds, 6.0f, isSelected ? 2.0f : 1.2f);

            // Header band
            juce::Colour headerCol(0xff4b6584);
            juce::String titleText = eq.name;
            if (eq.type == "audio_input")
            {
                headerCol = juce::Colour(0xff8e44ad);
                titleText = "⚡ AUDIO INPUT";
            }
            else if (eq.type == "audio_output")
            {
                headerCol = juce::Colour(0xff0984e3);
                titleText = "AUDIO SINK";
            }
            else if (eq.type == "signal_gen")
            {
                headerCol = juce::Colour(0xffd35400);
                titleText = "SIGNAL GENERATOR";
            }
            else if (eq.type == "probe")
            {
                headerCol = juce::Colour(0xff27ae60);
                titleText = "VOLTAGE PROBE";
            }

            auto headerRect = bounds;
            headerRect = headerRect.removeFromTop(20.0f);
            g.setColour(headerCol);
            g.fillRoundedRectangle(headerRect, 6.0f);
            // squared bottom of header
            g.fillRect(headerRect.removeFromBottom(4.0f));

            g.setColour(juce::Colours::white);
            g.setFont(juce::Font(10.0f, juce::Font::bold));
            g.drawText(titleText, (int)bounds.getX() + 6, (int)bounds.getY() + 2, (int)bounds.getWidth() - 12, 16, juce::Justification::centredLeft);

            // Content
            g.setFont(juce::Font(10.0f));
            g.setColour(juce::Colour(0xffdcdde1));
            const int bodyY = (int)bounds.getY() + 24;
            if (eq.type == "audio_input")
            {
                if (eq.isOverride)
                    g.drawText("Override: " + eq.overrideRefdes, (int)bounds.getX() + 8, bodyY, (int)bounds.getWidth() - 16, 16, juce::Justification::centredLeft);
                else
                    g.drawText(juce::String(eq.peakVolts * 1000.0, 0) + " mV pk stimulus", (int)bounds.getX() + 8, bodyY, (int)bounds.getWidth() - 16, 16, juce::Justification::centredLeft);

                g.setFont(juce::Font(9.0f));
                g.setColour(juce::Colour(0xff95a5a6));
                const auto stimDesc = (eq.stimulusType == "file") ? ("WAV: " + juce::File(eq.audioFilePath).getFileName()) : (juce::String(eq.sineFrequencyHz, 0) + " Hz Sine");
                g.drawText(stimDesc, (int)bounds.getX() + 8, bodyY + 16, (int)bounds.getWidth() - 16, 16, juce::Justification::centredLeft);
            }
            else if (eq.type == "audio_output")
            {
                if (s->hasResult)
                {
                    g.drawText("Peak: " + juce::String(eq.measuredPeak, 3) + " V (" + juce::String(eq.gainDb, 1) + " dB)", (int)bounds.getX() + 8, bodyY, (int)bounds.getWidth() - 16, 16, juce::Justification::centredLeft);
                    g.setFont(juce::Font(9.0f));
                    g.setColour(juce::Colour(0xff95a5a6));
                    g.drawText("RMS: " + juce::String(eq.measuredRms, 3) + " V | DC: " + juce::String(eq.measuredDc, 2) + " V", (int)bounds.getX() + 8, bodyY + 16, (int)bounds.getWidth() - 16, 16, juce::Justification::centredLeft);
                }
                else
                {
                    g.drawText("Monitor / Audition", (int)bounds.getX() + 8, bodyY, (int)bounds.getWidth() - 16, 16, juce::Justification::centredLeft);
                    g.setFont(juce::Font(9.0f));
                    g.setColour(juce::Colour(0xff95a5a6));
                    g.drawText("Ready for test run", (int)bounds.getX() + 8, bodyY + 16, (int)bounds.getWidth() - 16, 16, juce::Justification::centredLeft);
                }
            }
            else if (eq.type == "signal_gen")
            {
                g.drawText(juce::String(eq.sineFrequencyHz, 0) + " Hz Sine Wave", (int)bounds.getX() + 8, bodyY, (int)bounds.getWidth() - 16, 16, juce::Justification::centredLeft);
                g.setFont(juce::Font(9.0f));
                g.setColour(juce::Colour(0xff95a5a6));
                g.drawText(juce::String(eq.peakVolts * 1000.0, 0) + " mV pk", (int)bounds.getX() + 8, bodyY + 16, (int)bounds.getWidth() - 16, 16, juce::Justification::centredLeft);
            }
            else if (eq.type == "probe")
            {
                g.drawText(eq.name, (int)bounds.getX() + 8, bodyY, (int)bounds.getWidth() - 16, 16, juce::Justification::centredLeft);
                g.setFont(juce::Font(9.0f));
                g.setColour(juce::Colour(0xff95a5a6));
                g.drawText("Scope Probe Tip", (int)bounds.getX() + 8, bodyY + 16, (int)bounds.getWidth() - 16, 16, juce::Justification::centredLeft);
            }

            // Terminals
            for (const auto& term : eq.getTerminals())
            {
                const auto pt = eq.pos + term.relativePos;
                g.setColour(headerCol.brighter(0.3f));
                g.fillEllipse(pt.x - 6.0f, pt.y - 6.0f, 12.0f, 12.0f);
                g.setColour(juce::Colours::white);
                g.fillEllipse(pt.x - 2.5f, pt.y - 2.5f, 5.0f, 5.0f);
                g.setFont(juce::Font(8.0f, juce::Font::bold));
                const float textOffset = term.isOutput ? 8.0f : -24.0f;
                g.drawText(term.name, (int)(pt.x + textOffset), (int)pt.y - 6, 20, 12, juce::Justification::centred);
            }
        }
    }

    void drawFixtureConnections(juce::Graphics& g)
    {
        auto* s = owner.getSetup();
        if (s == nullptr) return;

        for (const auto& conn : s->fixtureConnections)
        {
            if (!conn.instancePath.equalsIgnoreCase(owner.currentInstancePath))
                continue;

            const auto* eq = s->getEquipment(conn.equipmentId);
            if (eq == nullptr) continue;

            juce::Point<float> startPt = eq->pos + juce::Point<float>(140.0f, 32.0f);
            for (const auto& t : eq->getTerminals())
                if (t.id.equalsIgnoreCase(conn.terminalId)) { startPt = eq->pos + t.relativePos; break; }

            juce::Point<float> endPt = conn.targetPoint;
            // Resolve pin label if available
            if (conn.targetPinLabel.isNotEmpty())
            {
                for (const auto& comp : circuitData.components)
                {
                    for (const auto& p : comp.pins)
                    {
                        if (p.label.equalsIgnoreCase(conn.targetPinLabel))
                        {
                            endPt = p.pos;
                            break;
                        }
                    }
                }
            }
            else if (conn.targetRefdes.isNotEmpty())
            {
                for (const auto& comp : circuitData.components)
                {
                    if (comp.refdes.equalsIgnoreCase(conn.targetRefdes))
                    {
                        endPt = comp.pos;
                        break;
                    }
                }
            }

            juce::Colour wireCol = juce::Colour(0xffe056fd); // Default magenta
            if (eq->type == "audio_output") wireCol = juce::Colour(0xff00d2d3); // Cyan
            else if (eq->type == "probe") wireCol = juce::Colour(0xfff1c40f); // Gold

            const bool isConnSelected = (conn.id == selectedConnectionId);
            if (isConnSelected)
            {
                g.setColour(juce::Colours::yellow.withAlpha(0.4f));
                drawDoglegWire(g, startPt, endPt, 6.0f);
            }

            g.setColour(wireCol);
            drawDoglegWire(g, startPt, endPt, 2.6f);

            // Glow endpoints
            g.fillEllipse(startPt.x - 4.0f, startPt.y - 4.0f, 8.0f, 8.0f);
            g.fillEllipse(endPt.x - 4.0f, endPt.y - 4.0f, 8.0f, 8.0f);
        }
    }

    void drawDoglegWire(juce::Graphics& g, juce::Point<float> a, juce::Point<float> b, float width)
    {
        const float midX = (a.x + b.x) * 0.5f;
        juce::Path p;
        p.startNewSubPath(a);
        p.lineTo(midX, a.y);
        p.lineTo(midX, b.y);
        p.lineTo(b);
        g.strokePath(p, juce::PathStrokeType(width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void drawPendingWiring(juce::Graphics& g)
    {
        if (!isWiring) return;

        const float dash[] = { 5.0f, 3.0f };
        g.setColour(juce::Colour(0xffe056fd));
        g.drawDashedLine(juce::Line<float>(wiringStartPoint, wiringCurrentPoint), dash, 2, 2.5f);
        g.fillEllipse(wiringCurrentPoint.x - 5.0f, wiringCurrentPoint.y - 5.0f, 10.0f, 10.0f);
    }

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        const float oldZoom = zoom;
        const float factor = (wheel.deltaY > 0) ? 1.12f : 0.89f;
        zoom = juce::jlimit(0.2f, 3.5f, zoom * factor);

        // Zoom relative to mouse
        const auto mousePos = e.position;
        viewOffset = mousePos - (mousePos - viewOffset) * (zoom / oldZoom);
        repaint();
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        const auto canPos = viewToCanvas(e.position);

        if (e.mods.isMiddleButtonDown() || (e.mods.isRightButtonDown() && !owner.breakToolToggle.getToggleState()))
        {
            isPanning = true;
            panStartMouse = e.position;
            panStartOffset = viewOffset;
            return;
        }

        // Connection Break tool click
        if (owner.breakToolToggle.getToggleState() || (e.mods.isRightButtonDown() && owner.breakToolToggle.getToggleState()))
        {
            // Hit test wires
            for (const auto& w : circuitData.wires)
            {
                juce::Line<float> line(w.a, w.b);
                if (line.findNearestPointTo(canPos).getDistanceFrom(canPos) < 14.0f)
                {
                    // Check if already broken
                    bool alreadyBroken = false;
                    auto* s = owner.getSetup();
                    if (s != nullptr)
                    {
                        for (const auto& b : s->connectionBreaks)
                        {
                            if (b.instancePath.equalsIgnoreCase(owner.currentInstancePath))
                            {
                                if (b.wireIndex == w.wireIndex ||
                                    (b.nodeA.equalsIgnoreCase(w.nodeA) && b.nodeB.equalsIgnoreCase(w.nodeB)))
                                {
                                    alreadyBroken = true;
                                    break;
                                }
                            }
                        }
                    }

                    if (alreadyBroken)
                        owner.restoreConnection(false, w.nodeA, w.nodeB, {}, owner.currentInstancePath);
                    else
                        owner.breakConnection(w.nodeA, w.nodeB, {}, w.wireIndex, owner.currentInstancePath);

                    repaint();
                    return;
                }
            }
        }

        auto* s = owner.getSetup();

        // Hit test equipment terminals
        if (s != nullptr)
        {
            for (const auto& eq : s->equipment)
            {
                if (!eq.instancePath.equalsIgnoreCase(owner.currentInstancePath)) continue;
                for (const auto& term : eq.getTerminals())
                {
                    const auto termPt = eq.pos + term.relativePos;
                    if (termPt.getDistanceFrom(canPos) <= 12.0f)
                    {
                        isWiring = true;
                        wiringEquipmentId = eq.id;
                        wiringTerminalId = term.id;
                        wiringStartPoint = termPt;
                        wiringCurrentPoint = canPos;
                        repaint();
                        return;
                    }
                }
            }
        }

        // Hit test equipment bodies
        if (s != nullptr)
        {
            for (auto it = s->equipment.rbegin(); it != s->equipment.rend(); ++it)
            {
                if (!it->instancePath.equalsIgnoreCase(owner.currentInstancePath)) continue;
                if (it->getBounds().contains(canPos))
                {
                    selectedEquipmentId = it->id;
                    selectedConnectionId.clear();
                    draggingEquipmentId = it->id;
                    dragStartMouse = canPos;
                    dragStartEqPos = it->pos;
                    repaint();
                    return;
                }
            }
        }

        // Hit test connection wires
        if (s != nullptr)
        {
            for (const auto& conn : s->fixtureConnections)
            {
                if (!conn.instancePath.equalsIgnoreCase(owner.currentInstancePath)) continue;
                const auto* eq = s->getEquipment(conn.equipmentId);
                if (eq != nullptr)
                {
                    const auto termPt = eq->pos + juce::Point<float>(140.0f, 32.0f);
                    const auto mid = (termPt + conn.targetPoint) * 0.5f;
                    if (mid.getDistanceFrom(canPos) < 18.0f)
                    {
                        selectedConnectionId = conn.id;
                        selectedEquipmentId.clear();
                        repaint();
                        return;
                    }
                }
            }
        }

        // Empty canvas click
        selectedEquipmentId.clear();
        selectedConnectionId.clear();
        isPanning = true;
        panStartMouse = e.position;
        panStartOffset = viewOffset;
        repaint();
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (isPanning)
        {
            viewOffset = panStartOffset + (e.position - panStartMouse);
            repaint();
            return;
        }

        auto* s = owner.getSetup();
        if (draggingEquipmentId.isNotEmpty() && s != nullptr)
        {
            auto* eq = s->getEquipment(draggingEquipmentId);
            if (eq != nullptr)
            {
                const auto canPos = viewToCanvas(e.position);
                eq->pos = dragStartEqPos + (canPos - dragStartMouse);
                // Zero solver calls during drag!
                repaint();
                return;
            }
        }

        if (isWiring)
        {
            wiringCurrentPoint = viewToCanvas(e.position);
            repaint();
            return;
        }
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (isPanning)
            isPanning = false;

        auto* s = owner.getSetup();

        if (draggingEquipmentId.isNotEmpty())
        {
            draggingEquipmentId.clear();
            if (s != nullptr)
                s->syncSetupFromEquipment();
            repaint();
        }

        if (isWiring)
        {
            isWiring = false;
            const auto canPos = viewToCanvas(e.position);

            // Find snap target pin
            juce::String snappedPinLabel;
            juce::String snappedRefdes;
            juce::Point<float> snappedPt = canPos;
            float minDist = 26.0f;

            for (const auto& comp : circuitData.components)
            {
                for (const auto& p : comp.pins)
                {
                    const float d = p.pos.getDistanceFrom(canPos);
                    if (d < minDist)
                    {
                        minDist = d;
                        snappedPinLabel = p.label;
                        snappedRefdes = comp.refdes;
                        snappedPt = p.pos;
                    }
                }
            }

            if (snappedPinLabel.isNotEmpty() && s != nullptr)
            {
                test_mode::FixtureConnection conn;
                conn.id = "conn_" + juce::String(juce::Random::getSystemRandom().nextInt(899999) + 100000);
                conn.equipmentId = wiringEquipmentId;
                conn.terminalId = wiringTerminalId;
                conn.targetPinLabel = snappedPinLabel;
                conn.targetRefdes = snappedRefdes;
                conn.targetPoint = snappedPt;
                conn.instancePath = owner.currentInstancePath;
                s->addConnection(conn);

                if (owner.callbacks.onStatusMessage)
                    owner.callbacks.onStatusMessage("Connected " + wiringEquipmentId + " to " + snappedPinLabel);
            }
            repaint();
        }
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        const auto canPos = viewToCanvas(e.position);

        // 1. Double click on equipment -> show settings dialog
        auto* s = owner.getSetup();
        if (s != nullptr)
        {
            for (const auto& eq : s->equipment)
            {
                if (!eq.instancePath.equalsIgnoreCase(owner.currentInstancePath)) continue;
                if (eq.getBounds().contains(canPos))
                {
                    owner.showEquipmentDialog(eq.id);
                    return;
                }
            }
        }

        // 2. Double click on sub-block -> navigate into subdiagram
        for (const auto& comp : circuitData.components)
        {
            if (comp.isSubBlock && comp.bounds.contains(canPos))
            {
                owner.navigateToSheet(comp.childSheet, comp.refdes);
                return;
            }
        }
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        {
            if (selectedEquipmentId.isNotEmpty())
            {
                owner.removeEquipment(selectedEquipmentId);
                selectedEquipmentId.clear();
                repaint();
                return true;
            }
            if (selectedConnectionId.isNotEmpty())
            {
                owner.disconnectEquipment(selectedConnectionId);
                selectedConnectionId.clear();
                repaint();
                return true;
            }
        }
        return false;
    }

private:
    TestFixturePanel& owner;
    test_fixture::FixtureCircuitData circuitData;

    float zoom = 1.0f;
    juce::Point<float> viewOffset { 50.0f, 50.0f };

    // Interaction state
    bool isPanning = false;
    juce::Point<float> panStartMouse;
    juce::Point<float> panStartOffset;

    juce::String selectedEquipmentId;
    juce::String selectedConnectionId;

    juce::String draggingEquipmentId;
    juce::Point<float> dragStartMouse;
    juce::Point<float> dragStartEqPos;

    bool isWiring = false;
    juce::String wiringEquipmentId;
    juce::String wiringTerminalId;
    juce::Point<float> wiringStartPoint;
    juce::Point<float> wiringCurrentPoint;
};

//==============================================================================
TestFixturePanel::TestFixturePanel(const juce::String& name, test_mode::TestSetup* s, Callbacks cb)
    : fixtureName(name), setup(s), callbacks(std::move(cb))
{
    setWantsKeyboardFocus(true);

    if (setup != nullptr)
        setup->syncEquipmentFromSetup();

    titleLabel.setText("FIXTURE: " + fixtureName, juce::dontSendNotification);
    titleLabel.setFont(juce::Font(13.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xffe056fd));
    addAndMakeVisible(titleLabel);

    // Setup action buttons
    runButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff27ae60));
    runButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    runButton.onClick = [this] {
        auto* s = getSetup();
        if (callbacks.runSimulation != nullptr && s != nullptr)
        {
            juce::String err;
            callbacks.runSimulation(s, false, &err);
            updateResultsDisplay();
            if (canvas != nullptr)
            {
                s->syncEquipmentFromSetup();
                canvas->repaint();
            }
        }
    };
    addAndMakeVisible(runButton);

    auditionButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2980b9));
    auditionButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    auditionButton.onClick = [this] {
        auto* s = getSetup();
        if (s != nullptr && !s->lastRenderedOutput.empty() && callbacks.auditionBuffer != nullptr)
        {
            callbacks.auditionBuffer(s->lastRenderedOutput, s->lastSampleRate);
        }
        else if (callbacks.runSimulation != nullptr && s != nullptr)
        {
            juce::String err;
            callbacks.runSimulation(s, true, &err);
            updateResultsDisplay();
            if (canvas != nullptr)
            {
                s->syncEquipmentFromSetup();
                canvas->repaint();
            }
        }
    };
    addAndMakeVisible(auditionButton);

    stopButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffc0392b));
    stopButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    stopButton.onClick = [this] {
        if (callbacks.stopSimulation != nullptr)
            callbacks.stopSimulation();
    };
    addAndMakeVisible(stopButton);

    exportButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff34495e));
    exportButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    exportButton.onClick = [this] {
        auto* s = getSetup();
        if (s == nullptr) return;
        const auto defaultFile = juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile(sanitizeId(fixtureName) + "_output.wav");
        fileChooser = std::make_unique<juce::FileChooser>("Export Rendered Test Audio WAV...", defaultFile, "*.wav");
        fileChooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
            [this](const juce::FileChooser& fc) {
                const auto target = fc.getResult();
                auto* cur = getSetup();
                if (target != juce::File() && callbacks.exportAudio != nullptr && cur != nullptr)
                {
                    juce::String err;
                    callbacks.exportAudio(cur, target.getFullPathName(), &err);
                    if (callbacks.onStatusMessage)
                        callbacks.onStatusMessage("Exported WAV audio to " + target.getFileName());
                }
            });
    };
    addAndMakeVisible(exportButton);

    saveButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff16a085));
    saveButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    saveButton.onClick = [this] { saveFixture(); };
    addAndMakeVisible(saveButton);

    // Equipment creation buttons
    addAudioInButton.onClick = [this] { placeEquipment("audio_input", "Audio Input", 80.0f, 200.0f, currentInstancePath); };
    addAudioSinkButton.onClick = [this] { placeEquipment("audio_output", "Audio Sink", 760.0f, 200.0f, currentInstancePath); };
    addSigGenButton.onClick = [this] { placeEquipment("signal_gen", "Signal Generator", 80.0f, 320.0f, currentInstancePath); };
    addProbeButton.onClick = [this] { placeEquipment("probe", "Voltage Probe", 450.0f, 80.0f, currentInstancePath); };

    addAndMakeVisible(addAudioInButton);
    addAndMakeVisible(addAudioSinkButton);
    addAndMakeVisible(addSigGenButton);
    addAndMakeVisible(addProbeButton);

    breakToolToggle.setColour(juce::ToggleButton::textColourId, juce::Colour(0xffff7675));
    breakToolToggle.setTooltip("When checked, click any schematic wire to break or restore it.");
    addAndMakeVisible(breakToolToggle);

    resultsLabel.setFont(juce::Font(11.0f));
    resultsLabel.setColour(juce::Label::textColourId, juce::Colour(0xff1dd1a1));
    addAndMakeVisible(resultsLabel);
    updateResultsDisplay();

    canvas = std::make_unique<CanvasView>(*this);
    addAndMakeVisible(canvas.get());
    canvas->loadCircuitData();
}

TestFixturePanel::~TestFixturePanel()
{
    if (callbacks.onClose != nullptr)
        callbacks.onClose(this);
}

void TestFixturePanel::updateFromSetup()
{
    auto* s = getSetup();
    if (s != nullptr)
        s->syncEquipmentFromSetup();
    updateResultsDisplay();
    if (canvas != nullptr)
        canvas->repaint();
}

void TestFixturePanel::updateResultsDisplay()
{
    auto* s = getSetup();
    if (s == nullptr || !s->hasResult)
    {
        resultsLabel.setText("No simulation results yet. Click [▶ Run] to simulate.", juce::dontSendNotification);
        return;
    }

    juce::String text = "In: " + juce::String(s->lastInputPeak * 1000.0, 0) + " mV pk  |  Out: " +
                        juce::String(s->lastOutputPeak, 3) + " V pk  |  Gain: +" +
                        juce::String(s->lastGainDb, 1) + " dB (" +
                        juce::String(s->lastGainLinear, 1) + "x)  |  RMS: " +
                        juce::String(s->lastOutputRms, 3) + " V";
    resultsLabel.setText(text, juce::dontSendNotification);
}

bool TestFixturePanel::saveFixture()
{
    auto* s = getSetup();
    if (s == nullptr) return false;
    juce::File targetFile;
    if (callbacks.getAuthoritativeDiagramFile != nullptr)
    {
        const auto diagFile = callbacks.getAuthoritativeDiagramFile();
        if (diagFile.existsAsFile())
            targetFile = diagFile.getSiblingFile(diagFile.getFileNameWithoutExtension() + "." + sanitizeId(fixtureName) + ".fixture.json");
    }

    if (!targetFile.existsAsFile() && callbacks.onSaveFixture != nullptr)
    {
        return callbacks.onSaveFixture(fixtureName, targetFile);
    }

    if (callbacks.onSaveFixture != nullptr)
        return callbacks.onSaveFixture(fixtureName, targetFile);

    return false;
}

bool TestFixturePanel::placeEquipment(const juce::String& type, const juce::String& name, float x, float y, const juce::String& instancePath)
{
    auto* s = getSetup();
    if (s == nullptr) return false;

    test_mode::FixtureEquipment eq;
    eq.id = sanitizeId(type) + "_" + juce::String(juce::Random::getSystemRandom().nextInt(89999) + 10000);
    eq.type = type;
    eq.name = name.isNotEmpty() ? name : type;
    eq.pos = { x, y };
    eq.instancePath = instancePath;

    if (type == "audio_input")
    {
        eq.stimulusType = "file";
        eq.peakVolts = 0.1;
        eq.isOverride = true;
        eq.overrideRefdes = "VIN";
    }
    else if (type == "signal_gen")
    {
        eq.stimulusType = "sine";
        eq.sineFrequencyHz = 1000.0;
        eq.peakVolts = 0.1;
        eq.isOverride = false;
    }

    s->addEquipment(eq);
    if (canvas != nullptr)
        canvas->repaint();
    return true;
}

bool TestFixturePanel::moveEquipment(const juce::String& eqId, float x, float y)
{
    auto* s = getSetup();
    if (s == nullptr) return false;
    auto* eq = s->getEquipment(eqId);
    if (eq == nullptr) return false;
    eq->pos = { x, y };
    s->syncSetupFromEquipment();
    if (canvas != nullptr)
        canvas->repaint();
    return true;
}

bool TestFixturePanel::removeEquipment(const juce::String& eqId)
{
    auto* s = getSetup();
    if (s == nullptr) return false;
    const bool ok = s->removeEquipment(eqId);
    if (canvas != nullptr)
        canvas->repaint();
    return ok;
}

bool TestFixturePanel::connectEquipment(const juce::String& eqId, const juce::String& terminalId,
                                        const juce::String& targetPinLabel, const juce::String& targetRefdes,
                                        int wireIndex, const juce::String& instancePath)
{
    auto* s = getSetup();
    if (s == nullptr) return false;
    test_mode::FixtureConnection conn;
    conn.id = "conn_" + juce::String(juce::Random::getSystemRandom().nextInt(899999) + 100000);
    conn.equipmentId = eqId;
    conn.terminalId = terminalId;
    conn.targetPinLabel = targetPinLabel;
    conn.targetRefdes = targetRefdes;
    conn.targetWireIndex = wireIndex;
    conn.instancePath = instancePath;

    s->addConnection(conn);
    if (canvas != nullptr)
        canvas->repaint();
    return true;
}

bool TestFixturePanel::disconnectEquipment(const juce::String& eqIdOrConnId)
{
    auto* s = getSetup();
    if (s == nullptr) return false;
    const bool ok = s->removeConnection(eqIdOrConnId);
    if (canvas != nullptr)
        canvas->repaint();
    return ok;
}

bool TestFixturePanel::configureEquipment(const juce::String& eqId, const juce::var& config)
{
    auto* s = getSetup();
    if (s == nullptr || !config.isObject()) return false;
    auto* eq = s->getEquipment(eqId);
    if (eq == nullptr) return false;

    if (config.hasProperty("name")) eq->name = config["name"].toString();
    if (config.hasProperty("audioFilePath")) eq->audioFilePath = config["audioFilePath"].toString();
    if (config.hasProperty("stimulusType")) eq->stimulusType = config["stimulusType"].toString();
    if (config.hasProperty("peakVolts")) eq->peakVolts = (double)config["peakVolts"];
    if (config.hasProperty("sineFrequencyHz")) eq->sineFrequencyHz = (double)config["sineFrequencyHz"];
    if (config.hasProperty("sineDurationSec")) eq->sineDurationSec = (double)config["sineDurationSec"];
    if (config.hasProperty("isOverride")) eq->isOverride = (bool)config["isOverride"];
    if (config.hasProperty("overrideRefdes")) eq->overrideRefdes = config["overrideRefdes"].toString();

    s->syncSetupFromEquipment();
    if (canvas != nullptr)
        canvas->repaint();
    return true;
}

bool TestFixturePanel::breakConnection(const juce::String& nodeA, const juce::String& nodeB,
                                       const juce::String& pin, int wireIndex,
                                       const juce::String& instancePath)
{
    auto* s = getSetup();
    if (s == nullptr) return false;
    test_mode::HierarchicalBreak b;
    b.instancePath = instancePath.isNotEmpty() ? instancePath : currentInstancePath;
    b.nodeA = nodeA;
    b.nodeB = nodeB;
    b.pin = pin;
    b.wireIndex = wireIndex;

    const bool ok = s->addConnectionBreak(b);
    if (canvas != nullptr)
        canvas->repaint();
    return ok;
}

bool TestFixturePanel::restoreConnection(bool restoreAll, const juce::String& nodeA,
                                         const juce::String& nodeB, const juce::String& pin,
                                         const juce::String& instancePath)
{
    auto* s = getSetup();
    if (s == nullptr) return false;
    const auto inst = instancePath.isNotEmpty() ? instancePath : currentInstancePath;
    if (restoreAll)
        s->clearBreaks(inst);
    else if (pin.isNotEmpty())
        s->removePinBreak(inst, pin);
    else if (nodeA.isNotEmpty() && nodeB.isNotEmpty())
        s->removeConnectionBreak(inst, nodeA, nodeB);
    else
        s->clearBreaks(inst);

    if (canvas != nullptr)
        canvas->repaint();
    return true;
}

void TestFixturePanel::navigateToSheet(const juce::String& sheetName, const juce::String& instanceRefdes)
{
    currentSheet = sheetName;
    currentInstancePath = instanceRefdes;
    if (canvas != nullptr)
        canvas->loadCircuitData();
    repaint();
}

void TestFixturePanel::showEquipmentDialog(const juce::String& eqId)
{
    auto* s = getSetup();
    if (s == nullptr) return;
    auto* eq = s->getEquipment(eqId);
    if (eq == nullptr) return;

    if (eq->type == "audio_input")
    {
        fileChooser = std::make_unique<juce::FileChooser>("Select Audio Source WAV file...", juce::File(eq->audioFilePath), "*.wav");
        fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this, eqId](const juce::FileChooser& fc) {
                const auto res = fc.getResult();
                auto* cur = getSetup();
                if (res != juce::File() && cur != nullptr)
                {
                    if (auto* e = cur->getEquipment(eqId))
                    {
                        e->audioFilePath = res.getFullPathName();
                        cur->syncSetupFromEquipment();
                        if (canvas != nullptr) canvas->repaint();
                    }
                }
            });
    }
}

void TestFixturePanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff121820));

    // Breadcrumb bar rendering at top
    auto bBounds = getLocalBounds().removeFromTop(36).reduced(10, 6);
    g.setFont(juce::Font(12.0f, juce::Font::bold));

    breadcrumbAreas.clear();
    int curX = bBounds.getX();

    auto drawCrumb = [&](const juce::String& label, const juce::String& sheet, const juce::String& inst, bool active) {
        const int w = (int)juce::Font(12.0f, juce::Font::bold).getStringWidth(label) + 16;
        const juce::Rectangle<int> r(curX, bBounds.getY(), w, bBounds.getHeight());
        breadcrumbAreas.push_back({ r, sheet });

        g.setColour(active ? juce::Colour(0xff2d98da) : juce::Colour(0xff222f3e));
        g.fillRoundedRectangle(r.toFloat(), 4.0f);
        g.setColour(active ? juce::Colours::white : juce::Colour(0xff8395a7));
        g.drawText(label, r, juce::Justification::centred);

        curX += w + 6;
        if (!active)
        {
            g.setColour(juce::Colour(0xff576574));
            g.drawText(">", curX - 4, bBounds.getY(), 12, bBounds.getHeight(), juce::Justification::centred);
            curX += 8;
        }
    };

    drawCrumb("Root Circuit", {}, {}, currentSheet.isEmpty());
    if (currentSheet.isNotEmpty())
    {
        drawCrumb(currentInstancePath.isNotEmpty() ? currentInstancePath : currentSheet, currentSheet, currentInstancePath, true);
    }
}

void TestFixturePanel::resized()
{
    auto b = getLocalBounds();
    auto topBar = b.removeFromTop(40).reduced(8, 6);

    titleLabel.setBounds(topBar.removeFromLeft(180));
    topBar.removeFromLeft(10);

    runButton.setBounds(topBar.removeFromLeft(70));
    topBar.removeFromLeft(4);
    auditionButton.setBounds(topBar.removeFromLeft(90));
    topBar.removeFromLeft(4);
    stopButton.setBounds(topBar.removeFromLeft(65));
    topBar.removeFromLeft(4);
    exportButton.setBounds(topBar.removeFromLeft(95));
    topBar.removeFromLeft(4);
    saveButton.setBounds(topBar.removeFromLeft(95));
    topBar.removeFromLeft(12);

    addAudioInButton.setBounds(topBar.removeFromLeft(85));
    topBar.removeFromLeft(4);
    addAudioSinkButton.setBounds(topBar.removeFromLeft(90));
    topBar.removeFromLeft(4);
    addSigGenButton.setBounds(topBar.removeFromLeft(80));
    topBar.removeFromLeft(4);
    addProbeButton.setBounds(topBar.removeFromLeft(70));
    topBar.removeFromLeft(12);

    breakToolToggle.setBounds(topBar.removeFromLeft(85));
    topBar.removeFromLeft(12);

    resultsLabel.setBounds(topBar);

    auto breadcrumbRow = b.removeFromTop(32);
    // canvas occupies the rest
    if (canvas != nullptr)
        canvas->setBounds(b);
}
