#include "PcbPanel.h"

#include <djehuti_route/outline.h>

#include <cmath>
#include <thread>
#include <limits>

namespace
{
const juce::Colour background(0xff10161d), sideColour(0xff151a20), raised(0xff1d2731), border(0xff33424d);
const juce::Colour textColour(0xffdce9ee), muted(0xff93a7b0), faint(0xff71808c), accent(0xff78dcca), warning(0xffffb36b);
const juce::Colour boardFill(0xff1e4a2c), edgeGold(0xffd2b45a), gridMinor(0xff18212a), gridMajor(0xff222e39);
const juce::Colour padCopper(0xffc9a227), silk(0xffe6e6e6), ratsnestColour(0xffffd24a);

// Copper layer colours: top red, bottom blue, inner layers after.
juce::Colour layerColour(int layer, int layerCount)
{
    static const juce::Colour inner[] { juce::Colour(0xff4fbf6a), juce::Colour(0xffd8a33f), juce::Colour(0xffb05fd8), juce::Colour(0xff3fd8c8) };
    if (layer == 0) return juce::Colour(0xffd8553f);
    if (layer == layerCount - 1) return juce::Colour(0xff3f7fd8);
    return inner[(layer - 1) % 4];
}

// The board as routing sees it (thickness does not matter to copper).
juce::String routeKey(const pcb::BoardDesign& b)
{
    auto v = b.toVar();
    if (auto* o = v.getDynamicObject()) o->removeProperty("thickness");
    return juce::JSON::toString(v, true);
}

juce::String mmText(double v, int decimals = 2)
{
    return juce::String(v, decimals) + " mm";
}

void styleLabel(juce::Label& l, float size, juce::Colour c, bool bold = false)
{
    l.setFont(juce::Font(size, bold ? juce::Font::bold : juce::Font::plain));
    l.setColour(juce::Label::textColourId, c);
    l.setJustificationType(juce::Justification::topLeft);
    l.setMinimumHorizontalScale(1.0f);
}

void styleCombo(juce::ComboBox& c)
{
    c.setColour(juce::ComboBox::backgroundColourId, background);
    c.setColour(juce::ComboBox::outlineColourId, border);
    c.setColour(juce::ComboBox::textColourId, textColour);
    c.setColour(juce::ComboBox::arrowColourId, muted);
}

void styleEditor(juce::TextEditor& e)
{
    e.setMultiLine(false);
    e.setFont(juce::Font("Consolas", 13.0f, juce::Font::plain));
    e.setColour(juce::TextEditor::backgroundColourId, background);
    e.setColour(juce::TextEditor::outlineColourId, border);
    e.setColour(juce::TextEditor::textColourId, textColour);
}

void styleButton(juce::TextButton& b)
{
    b.setColour(juce::TextButton::buttonColourId, raised);
    b.setColour(juce::TextButton::textColourOffId, textColour);
}

double parseMm(const juce::String& text, double fallback)
{
    const auto t = text.trim().trimCharactersAtEnd("m ").trim();
    return t.containsOnly("0123456789.-+eE") && t.isNotEmpty() ? t.getDoubleValue() : fallback;
}

bool insidePolygon(const std::vector<pcb::Point>& poly, pcb::Point p)
{
    bool inside = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
        if ((poly[i].y > p.y) != (poly[j].y > p.y)
            && p.x < (poly[j].x - poly[i].x) * (p.y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x)
            inside = !inside;
    return inside;
}

bool isCopperLayer(const juce::String& layer) { return layer.endsWith(".Cu"); }

double segmentDistance(pcb::Point p, pcb::Point a, pcb::Point b, pcb::Point* closest = nullptr)
{
    const auto d = b - a;
    const double len2 = d.x * d.x + d.y * d.y;
    double t = len2 > 0.0 ? ((p.x - a.x) * d.x + (p.y - a.y) * d.y) / len2 : 0.0;
    t = juce::jlimit(0.0, 1.0, t);
    const pcb::Point c { a.x + t * d.x, a.y + t * d.y };
    if (closest != nullptr) *closest = c;
    return p.getDistanceFrom(c);
}

}

// ============================================================================

class PcbPanel::Canvas final : public juce::Component
{
public:
    explicit Canvas(PcbPanel& o) : owner(o)
    {
        setWantsKeyboardFocus(true);
    }

    double snap = 0.5;
    bool drawing = false;

    juce::String drawMode { "outline" }; // outline, or a graphic kind: line, rect, circle, arc, polygon

    void startArt(const juce::String& kind)
    {
        drawMode = kind;
        drawing = true;
        drawPoints.clear();
        selection = {};
        repaint();
    }

    void startDrawing()
    {
        drawMode = "outline";
        drawing = true;
        drawPoints.clear();
        selection = {};
        repaint();
    }

    // The view follows the board until the user zooms or pans; Zoom to fit
    // hands it back. A fit asked for while the tab is hidden (no size yet)
    // happens when the canvas is laid out.
    bool autoFit = true;

    void resized() override
    {
        if (autoFit) fit();
    }

    void fit()
    {
        autoFit = true;
        const auto b = owner.board.bounds();
        if (b.isEmpty() || getWidth() < 20 || getHeight() < 20)
        {
            zoom = 5.0;
            origin = { 60.0, getHeight() - 60.0 };
            repaint();
            return;
        }
        // Room for the dimension lines, but never more than a slice of a narrow canvas.
        const double margin = std::min(70.0, 0.15 * std::min(getWidth(), getHeight()));
        zoom = std::min((getWidth() - 2 * margin) / std::max(1.0, b.getWidth()), (getHeight() - 2 * margin) / std::max(1.0, b.getHeight()));
        zoom = juce::jlimit(0.2, 400.0, zoom);
        origin.x = getWidth() / 2.0 - (b.getCentreX()) * zoom;
        origin.y = getHeight() / 2.0 + (b.getCentreY()) * zoom;
        repaint();
    }

    juce::String selectedPart() const
    {
        if (selection.kind == Kind::Part && selection.index < (int)owner.layout.parts.size())
            return owner.layout.parts[(size_t)selection.index].refdes;
        return {};
    }

    void selectPart(const juce::String& refdes)
    {
        for (size_t i = 0; i < owner.layout.parts.size(); ++i)
            if (owner.layout.parts[i].refdes == refdes) { selection = { Kind::Part, (int)i, 0 }; repaint(); return; }
        selection = {};
        repaint();
    }

    int selectedTextIndex() const { return selection.kind == Kind::Text ? selection.index : -1; }
    int selectedGraphicIndex() const { return selection.kind == Kind::Graphic ? selection.index : -1; }
    void selectText(int i) { selection = { Kind::Text, i, 0 }; repaint(); }
    void selectGraphic(int i) { selection = { Kind::Graphic, i, 0 }; repaint(); }

    void drawArt(juce::Graphics& g, const pcb::Artwork& art) const
    {
        for (const auto& f : art.fills)
        {
            juce::Path p;
            addPolygon(p, f);
            g.fillPath(p);
        }
        for (const auto& s : art.strokes)
        {
            if (s.points.size() < 2) continue;
            juce::Path p;
            p.startNewSubPath(toScreen(s.points[0]));
            for (size_t i = 1; i < s.points.size(); ++i) p.lineTo(toScreen(s.points[i]));
            g.strokePath(p, juce::PathStrokeType(std::max(1.0f, (float)(s.width * zoom)), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    // Draws with the given areas cut out (silkscreen stops at the mask openings, as in the Gerber).
    void drawClipped(juce::Graphics& g, const std::vector<pcb::Opening>& areas, const std::function<void()>& draw) const
    {
        juce::Path clip;
        clip.setUsingNonZeroWinding(false);
        clip.addRectangle(getLocalBounds().toFloat());
        for (const auto& a : areas)
        {
            const auto c2 = toScreen(a.centre);
            const float w = (float)(a.w * zoom), h = (float)(a.h * zoom);
            if (a.round) clip.addEllipse(c2.x - w / 2, c2.y - w / 2, w, w);
            else clip.addRectangle(c2.x - w / 2, c2.y - h / 2, w, h);
        }
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(clip);
        draw();
    }

    void clearSelection()
    {
        selection = {};
        repaint();
    }

    void rotateSelected()
    {
        if (selection.kind != Kind::Part || selection.index >= (int)owner.layout.parts.size()) return;
        auto next = owner.layout;
        auto& part = next.parts[(size_t)selection.index];
        part.rotation = (part.rotation + 90) % 360;
        next.clearRoute();
        owner.editedLayout(next);
    }

    void drawLayout(juce::Graphics& g) const
    {
        const auto& l = owner.layout;
        const int layers = owner.board.layers;
        // Copper: bottom first, top last.
        for (int layer = layers - 1; layer >= 0; --layer)
        {
            g.setColour(layerColour(layer, layers).withAlpha(layer == 0 ? 0.9f : 0.75f));
            for (const auto& t : l.tracks)
            {
                if (t.layer != layer || t.points.size() < 2) continue;
                juce::Path path;
                path.startNewSubPath(toScreen(t.points[0]));
                for (size_t i = 1; i < t.points.size(); ++i) path.lineTo(toScreen(t.points[i]));
                g.strokePath(path, juce::PathStrokeType(std::max(1.0f, (float)(t.width * zoom)), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        }
        // Copper text and graphics.
        for (const auto& layer : { juce::String("B.Cu"), juce::String("F.Cu") })
        {
            if (layer == "B.Cu" && layers < 2) continue;
            g.setColour(layerColour(pcb::copperLayerIndex(layer, layers), layers).withAlpha(0.9f));
            drawArt(g, pcb::userArtwork(l, layer));
        }
        // Parts: pads, courtyard.
        for (size_t i = 0; i < l.parts.size(); ++i)
        {
            const auto& part = l.parts[i];
            const auto r = pcb::courtyardOf(part);
            const juce::Rectangle<float> box(toScreen({ r.getX(), r.getBottom() }), toScreen({ r.getRight(), r.getY() }));
            const bool selected = selection.kind == Kind::Part && selection.index == (int)i;
            for (const auto& pad : pcb::padsOf(part))
            {
                const auto c = toScreen(pad.centre);
                const float w = (float)(pad.w * zoom), h = (float)(pad.h * zoom);
                g.setColour(pad.drill > 0.0 ? padCopper : padCopper.interpolatedWith(layerColour(0, layers), 0.25f));
                if (pad.round) g.fillEllipse(c.x - w / 2, c.y - w / 2, w, w);
                else g.fillRect(c.x - w / 2, c.y - h / 2, w, h);
                if (pad.drill > 0.0)
                {
                    const float d = (float)(pad.drill * zoom);
                    g.setColour(background);
                    g.fillEllipse(c.x - d / 2, c.y - d / 2, d, d);
                }
                if (pad.number == "1" && part.footprint != "TestPoint-THT")
                {
                    // Pin 1 mark on the silkscreen.
                    g.setColour(silk.withAlpha(0.8f));
                    const float m = std::max(2.0f, (float)(0.35 * zoom));
                    const float off = std::max(w, h) / 2 + m;
                    g.fillEllipse(c.x - off - m / 2, c.y - off - m / 2, m, m);
                }
            }
            g.setColour(selected ? accent : silk.withAlpha(0.15f));
            g.drawRect(box, selected ? 2.0f : 1.0f);
        }
        // Silkscreen as printed: bottom (seen through the board) then top.
        g.setColour(juce::Colour(0xffc8b8ff).withAlpha(0.5f));
        drawClipped(g, pcb::silkClearAreas(l, false), [&] { drawArt(g, pcb::silkscreen(l, false)); });
        g.setColour(silk.withAlpha(0.92f));
        drawClipped(g, pcb::silkClearAreas(l, true), [&] { drawArt(g, pcb::silkscreen(l, true)); });
        // Selected text or graphic.
        juce::Rectangle<double> sel;
        if (selection.kind == Kind::Text && selection.index < (int)l.texts.size())
            sel = pcb::boundsOf(pcb::textArtwork(l.texts[(size_t)selection.index]));
        else if (selection.kind == Kind::Graphic && selection.index < (int)l.graphics.size())
            sel = pcb::boundsOf(pcb::graphicArtwork(l.graphics[(size_t)selection.index]));
        if (!sel.isEmpty())
        {
            g.setColour(accent);
            g.drawRect(juce::Rectangle<float>(toScreen({ sel.getX(), sel.getBottom() }), toScreen({ sel.getRight(), sel.getY() })).expanded(3.0f), 1.5f);
        }
        for (const auto& v : l.vias)
        {
            const auto c = toScreen(v.at);
            const float d = std::max(4.0f, (float)(v.diameter * zoom)), h = std::max(1.5f, (float)(v.drill * zoom));
            g.setColour(juce::Colour(0xffdddddd));
            g.fillEllipse(c.x - d / 2, c.y - d / 2, d, d);
            g.setColour(background);
            g.fillEllipse(c.x - h / 2, c.y - h / 2, h, h);
        }
        g.setColour(ratsnestColour.withAlpha(0.85f));
        for (const auto& [a, b2] : pcb::ratsnest(l))
            g.drawLine(juce::Line<float>(toScreen(a), toScreen(b2)), 1.0f);
        for (const auto& m : l.violations)
        {
            if (!m.located) continue;
            const auto c = toScreen(m.at);
            g.setColour(warning);
            g.drawEllipse(c.x - 7, c.y - 7, 14, 14, 2.0f);
            g.drawLine(c.x - 4, c.y - 4, c.x + 4, c.y + 4, 1.5f);
            g.drawLine(c.x - 4, c.y + 4, c.x + 4, c.y - 4, 1.5f);
        }
    }


    void deleteSelection()
    {
        if (selection.kind == Kind::Text || selection.kind == Kind::Graphic)
        {
            auto next = owner.layout;
            bool copper = false;
            if (selection.kind == Kind::Text && selection.index < (int)next.texts.size())
            {
                copper = isCopperLayer(next.texts[(size_t)selection.index].layer);
                next.texts.erase(next.texts.begin() + selection.index);
            }
            else if (selection.kind == Kind::Graphic && selection.index < (int)next.graphics.size())
            {
                copper = isCopperLayer(next.graphics[(size_t)selection.index].layer);
                next.graphics.erase(next.graphics.begin() + selection.index);
            }
            else
                return;
            selection = {};
            owner.commitArt(next, copper);
            owner.selectionChanged();
            return;
        }
        auto next = owner.board;
        bool changed = false;
        if (selection.kind == Kind::Hole && selection.index < (int)next.holes.size())
        {
            next.holes.erase(next.holes.begin() + selection.index);
            changed = true;
        }
        else if (selection.kind == Kind::Cutout && selection.index < (int)next.cutouts.size())
        {
            next.cutouts.erase(next.cutouts.begin() + selection.index);
            changed = true;
        }
        else if (selection.kind == Kind::Vertex && next.outline.size() > 3 && selection.index < (int)next.outline.size())
        {
            next.outline.erase(next.outline.begin() + selection.index);
            next.source = "custom";
            changed = true;
        }
        else if (selection.kind == Kind::CutoutVertex && selection.index < (int)next.cutouts.size()
                 && next.cutouts[(size_t)selection.index].size() > 3)
        {
            auto& c = next.cutouts[(size_t)selection.index];
            c.erase(c.begin() + selection.sub);
            changed = true;
        }
        if (changed)
        {
            selection = {};
            owner.edited(next, false);
        }
    }

    juce::String selectionText() const
    {
        const auto& b = owner.board;
        switch (selection.kind)
        {
            case Kind::Hole:
                if (selection.index < (int)b.holes.size())
                {
                    const auto& h = b.holes[(size_t)selection.index];
                    return "Hole " + juce::String(selection.index + 1) + ": centre (" + juce::String(h.centre.x, 2) + ", " + juce::String(h.centre.y, 2)
                         + ") mm, diameter " + mmText(h.diameter) + ". Drag to move, Delete to remove.";
                }
                break;
            case Kind::Vertex:
                if (selection.index < (int)b.outline.size())
                    return "Corner " + juce::String(selection.index + 1) + " at (" + juce::String(b.outline[(size_t)selection.index].x, 2) + ", "
                         + juce::String(b.outline[(size_t)selection.index].y, 2) + ") mm. Drag to move, Delete to remove.";
                break;
            case Kind::Cutout:
            case Kind::CutoutVertex:
                return "Cutout " + juce::String(selection.index + 1) + ". Drag to move it, drag its corners, Delete to remove.";
            case Kind::Text:
                if (selection.index < (int)owner.layout.texts.size())
                    return "Text \"" + owner.layout.texts[(size_t)selection.index].text + "\" on " + owner.layout.texts[(size_t)selection.index].layer
                         + ". Drag to move; edit it in the sidebar; Delete removes it.";
                break;
            case Kind::Graphic:
                if (selection.index < (int)owner.layout.graphics.size())
                    return "Graphic (" + owner.layout.graphics[(size_t)selection.index].kind + ") on " + owner.layout.graphics[(size_t)selection.index].layer
                         + ". Drag to move; edit it in the sidebar; Delete removes it.";
                break;
            case Kind::Part:
                if (selection.index < (int)owner.layout.parts.size())
                {
                    const auto& part = owner.layout.parts[(size_t)selection.index];
                    return part.refdes + " " + part.value + " (" + part.footprint + ") at (" + juce::String(part.at.x, 2) + ", " + juce::String(part.at.y, 2)
                         + ") mm, " + juce::String(part.rotation) + " deg. Drag to move, R or right-click to rotate.";
                }
                break;
            default:
                break;
        }
        return {};
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(background);
        drawGrid(g);
        const auto& b = owner.board;
        const bool valid = b.problems().isEmpty();

        if (b.outline.size() >= 3)
        {
            juce::Path board;
            board.setUsingNonZeroWinding(false); // cutouts and holes become holes in the fill
            addPolygon(board, b.outline);
            for (const auto& c : b.cutouts) if (c.size() >= 3) addPolygon(board, c);
            for (const auto& h : b.holes)
            {
                const auto c = toScreen(h.centre);
                const auto r = (float)(h.diameter / 2.0 * zoom);
                board.addEllipse(c.x - r, c.y - r, 2 * r, 2 * r);
            }
            g.setColour(boardFill);
            g.fillPath(board);
            g.setColour(valid ? edgeGold : warning);
            g.strokePath(board, juce::PathStrokeType(2.0f));
            for (const auto& h : b.holes)
            {
                const auto c = toScreen(h.centre);
                g.setColour(edgeGold.withAlpha(0.6f));
                g.drawLine(c.x - 4, c.y, c.x + 4, c.y, 1.0f);
                g.drawLine(c.x, c.y - 4, c.x, c.y + 4, 1.0f);
            }
            drawDimensions(g);
        }
        drawLayout(g);
        if (b.outline.size() < 3 && !drawing)
        {
            g.setColour(faint);
            g.setFont(14.0f);
            g.drawText("No outline yet: choose a standard board or a shape, or draw one.", getLocalBounds(), juce::Justification::centred);
        }

        // Corner handles (edit mode).
        if (!drawing)
        {
            for (size_t i = 0; i < b.outline.size(); ++i)
                drawHandle(g, toScreen(b.outline[i]), selection.kind == Kind::Vertex && selection.index == (int)i);
            for (size_t c = 0; c < b.cutouts.size(); ++c)
                for (size_t i = 0; i < b.cutouts[c].size(); ++i)
                    drawHandle(g, toScreen(b.cutouts[c][i]), selection.kind == Kind::CutoutVertex && selection.index == (int)c && selection.sub == (int)i);
            if (selection.kind == Kind::Hole && selection.index < (int)b.holes.size())
            {
                const auto& h = b.holes[(size_t)selection.index];
                const auto c = toScreen(h.centre);
                const auto r = (float)(h.diameter / 2.0 * zoom) + 3.0f;
                g.setColour(accent);
                g.drawEllipse(c.x - r, c.y - r, 2 * r, 2 * r, 1.5f);
            }
            if (selection.kind == Kind::Cutout && selection.index < (int)b.cutouts.size())
            {
                juce::Path p;
                addPolygon(p, b.cutouts[(size_t)selection.index]);
                g.setColour(accent);
                g.strokePath(p, juce::PathStrokeType(1.5f));
            }
            if (hoverEdge >= 0 && hoverEdge < (int)b.outline.size())
            {
                const auto a = b.outline[(size_t)hoverEdge];
                const auto c = b.outline[((size_t)hoverEdge + 1) % b.outline.size()];
                const auto mid = toScreen((a + c) / 2.0);
                g.setColour(accent);
                g.drawLine(juce::Line<float>(toScreen(a), toScreen(c)), 2.0f);
                g.setFont(12.0f);
                g.drawText(mmText(a.getDistanceFrom(c)), juce::Rectangle<float>(mid.x - 50, mid.y - 22, 100, 16), juce::Justification::centred);
            }
        }

        if (!drawing && selection.kind != Kind::None)
        {
            g.setColour(textColour);
            g.setFont(12.5f);
            g.drawText(selectionText(), getLocalBounds().removeFromTop(26).reduced(10, 4), juce::Justification::centredLeft);
        }

        // A graphic being drawn: its shape with the cursor as the next point.
        if (drawing && drawMode != "outline")
        {
            auto pts = drawPoints;
            pts.push_back(cursorMm);
            g.setColour(accent.withAlpha(0.8f));
            drawArt(g, pcb::graphicArtwork(graphicFrom(pts)));
            for (const auto& p : drawPoints) drawHandle(g, toScreen(p), false);
            g.setColour(muted);
            g.setFont(12.5f);
            const juce::String help = drawMode == "line" ? "Line: click the start, then the end."
                                    : drawMode == "rect" ? "Rectangle: click one corner, then the opposite corner."
                                    : drawMode == "circle" ? "Circle: click the centre, then a point on the circle."
                                    : drawMode == "arc" ? "Arc: click the centre, then the start, then the end (counter-clockwise)."
                                                        : "Polygon: click corners, click the first corner (or double-click) to close.";
            g.drawText(help + " Esc cancels.", getLocalBounds().removeFromTop(26).reduced(10, 4), juce::Justification::centredLeft);
        }
        // The outline being drawn.
        if (drawing && drawMode == "outline")
        {
            g.setColour(accent);
            for (size_t i = 1; i < drawPoints.size(); ++i)
                g.drawLine(juce::Line<float>(toScreen(drawPoints[i - 1]), toScreen(drawPoints[i])), 2.0f);
            if (!drawPoints.empty())
            {
                g.setColour(accent.withAlpha(0.5f));
                g.drawLine(juce::Line<float>(toScreen(drawPoints.back()), toScreen(cursorMm)), 1.5f);
                g.setFont(12.0f);
                g.drawText(mmText(drawPoints.back().getDistanceFrom(cursorMm)), juce::Rectangle<float>(toScreen(cursorMm).x + 10, toScreen(cursorMm).y - 18, 110, 16), juce::Justification::left);
                const auto first = toScreen(drawPoints.front());
                g.setColour(drawPoints.size() >= 3 ? accent : faint);
                g.drawEllipse(first.x - 7, first.y - 7, 14, 14, 1.5f);
            }
            for (const auto& p : drawPoints) drawHandle(g, toScreen(p), false);
            g.setColour(muted);
            g.setFont(12.5f);
            g.drawText("Drawing: click to place corners, click the first corner (or double-click) to close, Esc to cancel.",
                       getLocalBounds().removeFromTop(26).reduced(10, 4), juce::Justification::centredLeft);
        }

        g.setColour(faint);
        g.setFont(12.0f);
        g.drawText("x " + juce::String(cursorMm.x, 2) + "  y " + juce::String(cursorMm.y, 2) + " mm    snap " + juce::String(snap, 2) + " mm    "
                   + (drawing ? juce::String() : juce::String("drag corners, click an edge to add a corner, right-click to delete, wheel to zoom, middle-drag to pan")),
                   getLocalBounds().removeFromBottom(22).reduced(10, 2), juce::Justification::centredLeft);
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        cursorMm = snapPoint(toMm(e.position));
        hoverEdge = drawing ? -1 : edgeAt(e.position);
        repaint();
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        lastMouse = e.position;
        if (e.mods.isMiddleButtonDown()) { panning = true; return; }
        const auto& b = owner.board;
        if (drawing)
        {
            if (e.mods.isRightButtonDown()) return;
            const auto p = snapPoint(toMm(e.position));
            if (drawPoints.size() >= 3 && toScreen(drawPoints.front()).getDistanceFrom(e.position) <= 8.0f
                && (drawMode == "outline" || drawMode == "polygon"))
            {
                if (drawMode == "outline") finishDrawing();
                else finishArt();
                return;
            }
            if (drawMode != "outline" && drawMode != "polygon")
            {
                if (drawPoints.empty() || drawPoints.back() != p) drawPoints.push_back(p);
                const size_t needed = drawMode == "arc" ? 3 : 2;
                if (drawPoints.size() >= needed) finishArt();
                repaint();
                return;
            }
            if (drawPoints.empty() || drawPoints.back() != p)
                drawPoints.push_back(p);
            repaint();
            return;
        }
        if (e.mods.isRightButtonDown() || e.mods.isPopupMenu())
        {
            selection = hitTest(e.position);
            owner.selectionChanged();
            if (selection.kind == Kind::Part) rotateSelected();
            else deleteSelection();
            return;
        }
        selection = hitTest(e.position);
        owner.selectionChanged();
        if (selection.kind == Kind::None)
        {
            // An edge: insert a corner there and drag it.
            const auto edge = edgeAt(e.position);
            if (edge >= 0)
            {
                auto next = b;
                pcb::Point closest;
                segmentDistance(toMm(e.position), b.outline[(size_t)edge], b.outline[((size_t)edge + 1) % b.outline.size()], &closest);
                next.outline.insert(next.outline.begin() + edge + 1, snapPoint(closest));
                next.source = "custom";
                owner.edited(next, false);
                selection = { Kind::Vertex, edge + 1, 0 };
            }
            else
            {
                panning = true;
                return;
            }
        }
        before = owner.board;
        beforeLayout = owner.layout;
        dragStartMm = snapPoint(toMm(e.position));
        dragging = true;
        repaint();
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (panning)
        {
            origin += { (double)(e.position.x - lastMouse.x), (double)(e.position.y - lastMouse.y) };
            autoFit = false;
            lastMouse = e.position;
            repaint();
            return;
        }
        if (!dragging) return;
        const auto p = snapPoint(toMm(e.position));
        cursorMm = p;
        auto& b = owner.board;
        const auto delta = p - dragStartMm;
        switch (selection.kind)
        {
            case Kind::Vertex:
                if (selection.index < (int)b.outline.size()) { b.outline[(size_t)selection.index] = p; b.source = "custom"; }
                break;
            case Kind::CutoutVertex:
                b.cutouts[(size_t)selection.index][(size_t)selection.sub] = p;
                break;
            case Kind::Hole:
                b.holes[(size_t)selection.index].centre = before.holes[(size_t)selection.index].centre + delta;
                break;
            case Kind::Cutout:
                for (size_t i = 0; i < b.cutouts[(size_t)selection.index].size(); ++i)
                    b.cutouts[(size_t)selection.index][i] = before.cutouts[(size_t)selection.index][i] + delta;
                break;
            case Kind::Part:
                if (selection.index < (int)owner.layout.parts.size())
                    owner.layout.parts[(size_t)selection.index].at = beforeLayout.parts[(size_t)selection.index].at + delta;
                break;
            case Kind::Text:
                if (selection.index < (int)owner.layout.texts.size())
                    owner.layout.texts[(size_t)selection.index].at = beforeLayout.texts[(size_t)selection.index].at + delta;
                break;
            case Kind::Graphic:
                if (selection.index < (int)owner.layout.graphics.size())
                {
                    auto& gr = owner.layout.graphics[(size_t)selection.index];
                    const auto& was = beforeLayout.graphics[(size_t)selection.index];
                    for (size_t k = 0; k < gr.points.size(); ++k) gr.points[k] = was.points[k] + delta;
                }
                break;
            default:
                break;
        }
        owner.updateInfo();
        repaint();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        if (dragging && (selection.kind == Kind::Text || selection.kind == Kind::Graphic))
        {
            dragging = false;
            if (juce::JSON::toString(owner.layout.toVar(), true) != juce::JSON::toString(beforeLayout.toVar(), true))
            {
                auto after = owner.layout;
                const bool copper = selection.kind == Kind::Text ? isCopperLayer(after.texts[(size_t)selection.index].layer)
                                                                 : isCopperLayer(after.graphics[(size_t)selection.index].layer);
                owner.layout = beforeLayout;
                owner.commitArt(after, copper);
            }
        }
        if (dragging && selection.kind == Kind::Part)
        {
            dragging = false;
            if (selection.index < (int)owner.layout.parts.size()
                && owner.layout.parts[(size_t)selection.index].at != beforeLayout.parts[(size_t)selection.index].at)
            {
                auto after = owner.layout;
                after.clearRoute(); // the copper no longer matches the parts
                owner.layout = beforeLayout;
                owner.editedLayout(after);
            }
        }
        if (dragging)
        {
            dragging = false;
            if (juce::JSON::toString(owner.board.toVar(), true) != juce::JSON::toString(before.toVar(), true))
            {
                auto after = owner.board;
                owner.board = before;
                owner.edited(after, false); // one undo step for the whole drag
            }
        }
        panning = false;
        repaint();
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        if (drawing)
        {
            if (drawPoints.size() >= 3)
            {
                if (drawMode == "outline") finishDrawing();
                else if (drawMode == "polygon") finishArt();
            }
            return;
        }
        if (hitTest(e.position).kind == Kind::None && edgeAt(e.position) < 0)
            fit();
    }

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        const auto anchor = toMm(e.position);
        zoom = juce::jlimit(0.2, 400.0, zoom * std::pow(1.15, wheel.deltaY * 6.0));
        autoFit = false;
        origin.x = e.position.x - anchor.x * zoom;
        origin.y = e.position.y + anchor.y * zoom;
        repaint();
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey && drawing)
        {
            drawing = false;
            drawPoints.clear();
            repaint();
            return true;
        }
        if ((key.getKeyCode() == 'R' || key.getKeyCode() == 'r') && !key.getModifiers().isCommandDown() && selection.kind == Kind::Part)
        {
            rotateSelected();
            return true;
        }
        if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        {
            deleteSelection();
            return true;
        }
        if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'Z')
        {
            owner.undo();
            return true;
        }
        return false;
    }

private:
    enum class Kind { None, Vertex, Hole, Cutout, CutoutVertex, Part, Text, Graphic };

    // The graphic the clicked points describe (also the drawing preview).
    pcb::BoardGraphic graphicFrom(const std::vector<pcb::Point>& pts) const
    {
        pcb::BoardGraphic gr;
        gr.kind = drawMode;
        gr.layer = owner.artLayer;
        gr.lineWidth = owner.artLineWidth;
        gr.filled = owner.artFilled && drawMode != "line" && drawMode != "arc";
        if (pts.empty()) return gr;
        if (drawMode == "circle" || drawMode == "arc")
        {
            gr.points = { pts[0] };
            gr.radius = pts.size() >= 2 ? pts[0].getDistanceFrom(pts[1]) : 0.0;
            if (drawMode == "arc" && pts.size() >= 2)
            {
                gr.startAngle = juce::radiansToDegrees(std::atan2(pts[1].y - pts[0].y, pts[1].x - pts[0].x));
                gr.endAngle = pts.size() >= 3 ? juce::radiansToDegrees(std::atan2(pts[2].y - pts[0].y, pts[2].x - pts[0].x)) : gr.startAngle;
                if (gr.endAngle <= gr.startAngle) gr.endAngle += 360.0;
            }
        }
        else
            gr.points = pts;
        return gr;
    }

    void finishArt()
    {
        auto gr = graphicFrom(drawPoints);
        drawing = false;
        drawPoints.clear();
        const bool ok = gr.kind == "polygon" ? gr.points.size() >= 3 : (gr.kind == "circle" || gr.kind == "arc") ? gr.radius > 0.0 : gr.points.size() >= 2;
        if (!ok) { repaint(); return; }
        auto next = owner.layout;
        next.graphics.push_back(gr);
        owner.commitArt(next, isCopperLayer(gr.layer));
        selection = { Kind::Graphic, (int)owner.layout.graphics.size() - 1, 0 };
        owner.selectionChanged();
    }
    struct Selection
    {
        Kind kind = Kind::None;
        int index = 0;
        int sub = 0;
    };

    PcbPanel& owner;
    double zoom = 5.0;               // px per mm
    pcb::Point origin { 60.0, 400.0 }; // screen position of (0, 0) mm
    pcb::Point cursorMm;
    juce::Point<float> lastMouse;
    bool panning = false, dragging = false;
    Selection selection;
    pcb::BoardDesign before;
    pcb::Layout beforeLayout;
    pcb::Point dragStartMm;
    std::vector<pcb::Point> drawPoints;
    int hoverEdge = -1;

    juce::Point<float> toScreen(pcb::Point p) const { return { (float)(origin.x + p.x * zoom), (float)(origin.y - p.y * zoom) }; }
    pcb::Point toMm(juce::Point<float> s) const { return { (s.x - origin.x) / zoom, (origin.y - s.y) / zoom }; }
    pcb::Point snapPoint(pcb::Point p) const
    {
        if (snap <= 0.0) return p;
        return { std::round(p.x / snap) * snap, std::round(p.y / snap) * snap };
    }

    void addPolygon(juce::Path& path, const std::vector<pcb::Point>& pts) const
    {
        if (pts.empty()) return;
        path.startNewSubPath(toScreen(pts[0]));
        for (size_t i = 1; i < pts.size(); ++i) path.lineTo(toScreen(pts[i]));
        path.closeSubPath();
    }

    void drawHandle(juce::Graphics& g, juce::Point<float> p, bool selected) const
    {
        const float s = selected ? 9.0f : 7.0f;
        g.setColour(selected ? accent : textColour);
        g.fillRect(p.x - s / 2, p.y - s / 2, s, s);
        g.setColour(background);
        g.drawRect(p.x - s / 2, p.y - s / 2, s, s, 1.0f);
    }

    void drawGrid(juce::Graphics& g) const
    {
        static const double steps[] = { 0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 20.0, 50.0, 100.0 };
        double minor = 100.0;
        for (auto s : steps) if (s * zoom >= 9.0) { minor = s; break; }
        const double major = minor * 10.0;
        const auto tl = toMm({ 0.0f, 0.0f }), br = toMm({ (float)getWidth(), (float)getHeight() });
        for (int pass = 0; pass < 2; ++pass)
        {
            const double step = pass == 0 ? minor : major;
            g.setColour(pass == 0 ? gridMinor : gridMajor);
            for (double x = std::floor(tl.x / step) * step; x <= br.x; x += step)
                g.drawVerticalLine((int)toScreen({ x, 0 }).x, 0.0f, (float)getHeight());
            for (double y = std::floor(br.y / step) * step; y <= tl.y; y += step)
                g.drawHorizontalLine((int)toScreen({ 0, y }).y, 0.0f, (float)getWidth());
        }
        // Axes through the origin.
        g.setColour(border);
        g.drawVerticalLine((int)origin.x, 0.0f, (float)getHeight());
        g.drawHorizontalLine((int)origin.y, 0.0f, (float)getWidth());
    }

    void drawDimensions(juce::Graphics& g) const
    {
        const auto b = owner.board.bounds();
        const auto bl = toScreen({ b.getX(), b.getY() }), br = toScreen({ b.getRight(), b.getY() }), tl = toScreen({ b.getX(), b.getBottom() });
        g.setColour(muted);
        g.setFont(12.5f);
        const float below = bl.y + 22.0f, left = bl.x - 22.0f;
        g.drawLine(bl.x, below, br.x, below, 1.0f);
        g.drawLine(bl.x, below - 5, bl.x, below + 5, 1.0f);
        g.drawLine(br.x, below - 5, br.x, below + 5, 1.0f);
        g.drawText(mmText(b.getWidth()), juce::Rectangle<float>((bl.x + br.x) / 2 - 60, below + 3, 120, 16), juce::Justification::centred);
        g.drawLine(left, bl.y, left, tl.y, 1.0f);
        g.drawLine(left - 5, bl.y, left + 5, bl.y, 1.0f);
        g.drawLine(left - 5, tl.y, left + 5, tl.y, 1.0f);
        juce::Graphics::ScopedSaveState state(g);
        g.addTransform(juce::AffineTransform::rotation(-juce::MathConstants<float>::halfPi, left - 8, (bl.y + tl.y) / 2));
        g.drawText(mmText(b.getHeight()), juce::Rectangle<float>(left - 68, (bl.y + tl.y) / 2 - 8, 120, 16), juce::Justification::centred);
    }

    Selection hitTest(juce::Point<float> s) const
    {
        const auto& b = owner.board;
        for (size_t i = 0; i < b.holes.size(); ++i)
            if (toScreen(b.holes[i].centre).getDistanceFrom(s) <= (float)(b.holes[i].diameter / 2.0 * zoom) + 4.0f)
                return { Kind::Hole, (int)i, 0 };
        for (size_t i = 0; i < b.outline.size(); ++i)
            if (toScreen(b.outline[i]).getDistanceFrom(s) <= 7.0f)
                return { Kind::Vertex, (int)i, 0 };
        for (size_t c = 0; c < b.cutouts.size(); ++c)
            for (size_t i = 0; i < b.cutouts[c].size(); ++i)
                if (toScreen(b.cutouts[c][i]).getDistanceFrom(s) <= 7.0f)
                    return { Kind::CutoutVertex, (int)c, (int)i };
        const auto p = toMm(s);
        const auto& texts = owner.layout.texts;
        for (int i = (int)texts.size() - 1; i >= 0; --i)
            if (pcb::boundsOf(pcb::textArtwork(texts[(size_t)i])).expanded(0.3).contains(p))
                return { Kind::Text, i, 0 };
        const auto& graphics = owner.layout.graphics;
        for (int i = (int)graphics.size() - 1; i >= 0; --i)
        {
            const auto art = pcb::graphicArtwork(graphics[(size_t)i]);
            for (const auto& f : art.fills)
                if (f.size() >= 3 && insidePolygon(f, p)) return { Kind::Graphic, i, 0 };
            for (const auto& st : art.strokes)
                for (size_t k = 1; k < st.points.size(); ++k)
                    if (segmentDistance(p, st.points[k - 1], st.points[k]) <= std::max(st.width / 2.0, 5.0 / zoom))
                        return { Kind::Graphic, i, 0 };
        }
        const auto& parts = owner.layout.parts;
        for (int i = (int)parts.size() - 1; i >= 0; --i)
            if (pcb::courtyardOf(parts[(size_t)i]).contains(p))
                return { Kind::Part, i, 0 };
        for (size_t c = 0; c < b.cutouts.size(); ++c)
        {
            juce::Path path;
            addPolygon(path, b.cutouts[c]);
            if (path.contains(s)) return { Kind::Cutout, (int)c, 0 };
        }
        juce::ignoreUnused(p);
        return {};
    }

    int edgeAt(juce::Point<float> s) const
    {
        const auto& b = owner.board;
        const auto p = toMm(s);
        for (size_t i = 0; i < b.outline.size(); ++i)
        {
            const auto a = b.outline[i], c = b.outline[(i + 1) % b.outline.size()];
            if (segmentDistance(p, a, c) * zoom <= 5.0) return (int)i;
        }
        return -1;
    }

    void finishDrawing()
    {
        auto next = owner.board;
        next.source = "custom";
        next.outline = drawPoints;
        drawing = false;
        drawPoints.clear();
        owner.edited(next, false);
        fit();
    }
};

// ============================================================================

PcbPanel::PcbPanel()
{
    board = pcb::BoardDesign::standard("fab-100");
    canvas = std::make_unique<Canvas>(*this);
    addAndMakeVisible(*canvas);
    sidebarViewport.setViewedComponent(&sidebar, false);
    sidebarViewport.setScrollBarsShown(true, false);
    sidebarViewport.setScrollBarThickness(9);
    addAndMakeVisible(sidebarViewport);
    styleLabel(info, 12.5f, muted);
    styleLabel(problems, 12.5f, warning);
    styleLabel(routeInfo, 12.5f, muted);
    styleLabel(verifyInfo, 12.5f, muted);
    styleLabel(exportInfo, 12.5f, muted);
    rebuildSidebar();
}

PcbPanel::~PcbPanel() = default;

void PcbPanel::setDesign(const pcb::BoardDesign& design, bool fromFile)
{
    if (!fromFile)
    {
        edited(design);
        rebuildSidebar();
        canvas->fit();
        return;
    }
    board = design;
    undoStack.clear();
    rebuildSidebar();
    canvas->fit();
}

void PcbPanel::edited(const pcb::BoardDesign& next, bool refit)
{
    const auto before = board.bounds();
    undoStack.push_back({ board, layout });
    if (undoStack.size() > 200) undoStack.pop_front();
    if (layout.routed && routeKey(board) != routeKey(next))
        layout.clearRoute(); // the copper was routed for the old board
    board = next;
    verify();
    updateInfo();
    if (refit && board.bounds() != before) canvas->fit();
    canvas->repaint();
    if (onChanged) onChanged();
}

void PcbPanel::undo()
{
    if (undoStack.empty()) return;
    board = undoStack.back().board;
    layout = undoStack.back().layout;
    undoStack.pop_back();
    rebuildSidebar();
    if (canvas->autoFit) canvas->fit();
    canvas->repaint();
    if (onChanged) onChanged();
}

void PcbPanel::editedLayout(const pcb::Layout& next)
{
    undoStack.push_back({ board, layout });
    if (undoStack.size() > 200) undoStack.pop_front();
    layout = next;
    verify();
    updateInfo();
    canvas->repaint();
    if (onChanged) onChanged();
}

const pcb::NetlistCheck& PcbPanel::verify()
{
    verification = getSchematicParts ? pcb::verifyNetlist(layout, board, getSchematicParts()) : pcb::NetlistCheck {};
    juce::StringArray lines;
    lines.add(verification.summary);
    for (int i = 0; i < verification.problems.size() && i < 8; ++i) lines.add("- " + verification.problems[i]);
    if (verification.problems.size() > 8) lines.add("- ... " + juce::String(verification.problems.size() - 8) + " more (pcb_verify_netlist lists all)");
    if (verification.routed) lines.add(pcb::netlistCheckNote());
    verifyInfo.setText(lines.joinIntoString("\n"), juce::dontSendNotification);
    verifyInfo.setColour(juce::Label::textColourId, verification.matches ? accent : verification.routed ? warning : muted);
    return verification;
}

void PcbPanel::visibilityChanged()
{
    // The schematic may have changed while another tab was showing.
    if (isShowing()) { verify(); updateInfo(); }
}

void PcbPanel::setLayout(const pcb::Layout& next, bool fromFile)
{
    if (fromFile)
    {
        layout = next;
        canvas->clearSelection();
        selectedPart.clear();
        lastReport.clear();
        verify();
        updateInfo();
        canvas->repaint();
    }
    else
        editedLayout(next);
    juce::Component::SafePointer<PcbPanel> safe(this);
    juce::MessageManager::callAsync([safe] { if (safe != nullptr) safe->rebuildSidebar(); });
}

pcb::SyncReport PcbPanel::syncFromSchematic()
{
    pcb::SyncReport report;
    if (!getSchematicParts) return report;
    auto next = layout;
    report = pcb::syncFromSchematic(next, getSchematicParts(), board);
    juce::StringArray lines;
    lines.add("From the schematic: " + juce::String(report.added.size()) + " added, " + juce::String(report.updated.size()) + " updated, "
              + juce::String(report.removed.size()) + " removed.");
    for (const auto& s2 : report.skipped) lines.add("Not on the board: " + s2);
    lastReport = lines.joinIntoString("\n");
    setLayout(next);
    return report;
}

juce::StringArray PcbPanel::autoPlace()
{
    auto next = layout;
    const auto notPlaced = pcb::autoPlace(next, board);
    lastReport = notPlaced.isEmpty() ? juce::String("Placed " + juce::String((int)next.parts.size()) + " parts.")
                                     : "Did not fit, set beside the board: " + notPlaced.joinIntoString(", ") + ". Make the board bigger or move parts.";
    setLayout(next);
    return notPlaced;
}

namespace
{
// Parts and rules, without the routing: what a route result belongs to.
juce::String layoutKey(pcb::Layout l)
{
    l.clearRoute();
    return juce::JSON::toString(l.toVar(), true);
}
}

void PcbPanel::route(std::optional<pcb::RouteRules> rules, std::function<void()> finished)
{
    if (routing)
    {
        if (finished) finished();
        return;
    }
    routing = true;
    auto start = layout;
    if (rules) start.rules = *rules;
    const auto keyBefore = layoutKey(layout);
    const auto boardBefore = board;
    lastReport = "Routing...";
    updateInfo();
    juce::Component::SafePointer<PcbPanel> safe(this);
    std::thread([safe, start, keyBefore, boardBefore, finished] {
        auto next = start;
        pcb::routeLayout(next, boardBefore);
        juce::MessageManager::callAsync([safe, next, keyBefore, boardBefore, finished] {
            if (safe != nullptr)
            {
                auto& self = *safe;
                self.routing = false;
                if (layoutKey(self.layout) == keyBefore && routeKey(self.board) == routeKey(boardBefore))
                {
                    self.lastReport.clear();
                    self.setLayout(next);
                }
                else
                {
                    self.lastReport = "The parts or board changed while routing; route again.";
                    self.updateInfo();
                }
            }
            if (finished) finished();
        });
    }).detach();
}

void PcbPanel::commitArt(pcb::Layout next, bool copperTouched)
{
    if (copperTouched) next.clearRoute(); // the routing assumed the old copper art
    editedLayout(next);
    juce::Component::SafePointer<PcbPanel> safe(this);
    juce::MessageManager::callAsync([safe] { if (safe != nullptr) safe->rebuildSidebar(); });
}

pcb::fab::ExportResult PcbPanel::exportFab()
{
    const auto check = verify();
    const auto folder = fabFolder ? fabFolder() : juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("djehuti_fab");
    const auto result = pcb::fab::exportFab(layout, board, folder, boardName ? boardName() : juce::String("board"), check.matches, check.summary);
    juce::StringArray lines;
    lines.add(result.summary);
    for (int i = 0; i < result.problems.size() && i < 6; ++i) lines.add("- " + result.problems[i]);
    if (result.zip.existsAsFile()) lines.add("Zip: " + result.zip.getFullPathName());
    lastExportText = lines.joinIntoString("\n");
    exportInfo.setText(lastExportText, juce::dontSendNotification);
    exportInfo.setColour(juce::Label::textColourId, result.readyForFab ? accent : warning);
    return result;
}

void PcbPanel::selectionChanged()
{
    const auto now = canvas->selectedPart();
    const int text = canvas->selectedTextIndex(), graphic = canvas->selectedGraphicIndex();
    if (now == selectedPart && text == selectedText && graphic == selectedGraphic) return;
    selectedPart = now;
    selectedText = text;
    selectedGraphic = graphic;
    juce::Component::SafePointer<PcbPanel> safe(this);
    juce::MessageManager::callAsync([safe] { if (safe != nullptr) safe->rebuildSidebar(); });
}

void PcbPanel::zoomToFit()
{
    canvas->fit();
}

bool PcbPanel::keyPressed(const juce::KeyPress& key)
{
    return canvas->keyPressed(key);
}

void PcbPanel::paint(juce::Graphics& g)
{
    g.fillAll(background);
    g.setColour(sideColour);
    g.fillRect(getLocalBounds().removeFromLeft(300));
    g.setColour(border);
    g.drawVerticalLine(300, 0.0f, (float)getHeight());
}

void PcbPanel::resized()
{
    auto area = getLocalBounds();
    sidebarViewport.setBounds(area.removeFromLeft(300).reduced(2, 0));
    sidebar.setSize(sidebarViewport.getWidth() - sidebarViewport.getScrollBarThickness() - 2, sidebar.getHeight());
    canvas->setBounds(area.withTrimmedLeft(1));
    rebuildSidebar();
}

void PcbPanel::updateInfo()
{
    const auto b = board.bounds();
    info.setText("Size " + juce::String(b.getWidth(), 2) + " x " + juce::String(b.getHeight(), 2) + " mm\n"
                 + "Area " + juce::String(board.areaMm2(), 1) + " mm2, perimeter " + juce::String(board.perimeterMm(), 1) + " mm\n"
                 + juce::String((int)board.outline.size()) + " corners, " + juce::String((int)board.holes.size()) + " hole(s), "
                 + juce::String((int)board.cutouts.size()) + " cutout(s), " + juce::String(board.layers) + " copper layer(s)",
                 juce::dontSendNotification);
    auto list = board.problems();
    list.addArray(pcb::placementProblems(layout, board));
    list.addArray(pcb::artworkWarnings(layout));
    problems.setText(list.isEmpty() ? juce::String() : list.joinIntoString("\n"), juce::dontSendNotification);

    juce::StringArray r;
    if (lastReport.isNotEmpty()) r.add(lastReport);
    if (layout.parts.empty())
        r.add("No parts on the board yet. Update parts from schematic puts the diagram's parts on it.");
    else
    {
        int toMake = 0;
        std::map<juce::String, int> padsPerNet;
        for (const auto& pad : pcb::allPads(layout))
            if (pad.net.isNotEmpty()) ++padsPerNet[pad.net];
        for (const auto& [net, n] : padsPerNet) toMake += n - 1;
        r.add(juce::String((int)layout.parts.size()) + " parts, " + juce::String((int)padsPerNet.size()) + " nets, " + juce::String(toMake) + " connections.");
        if (layout.routeError.isNotEmpty())
            r.add("Not routed: " + layout.routeError);
        else if (layout.routed)
        {
            double length = 0.0;
            for (const auto& t : layout.tracks)
                for (size_t i = 1; i < t.points.size(); ++i) length += t.points[i - 1].getDistanceFrom(t.points[i]);
            r.add("Routed " + juce::String(layout.routedConnections) + " of " + juce::String(layout.connections) + " connections ("
                  + juce::String(layout.iterations) + " passes, " + juce::String(layout.seconds, 2) + " s): " + juce::String(length, 1) + " mm of track, "
                  + juce::String((int)layout.vias.size()) + " vias.");
            r.add("Design rules: " + (layout.violations.empty() ? juce::String("no violations.") : juce::String((int)layout.violations.size()) + " violation(s):"));
            for (size_t i = 0; i < layout.violations.size() && i < 6; ++i) r.add("  " + layout.violations[i].message);
            for (int i = 0; i < layout.unrouted.size() && i < 6; ++i) r.add("Unrouted " + layout.unrouted[i]);
        }
        else
            r.add("Not routed yet: yellow lines are the connections to make.");
    }
    routeInfo.setText(r.joinIntoString("\n"), juce::dontSendNotification);
}

void PcbPanel::rebuildSidebar()
{
    if (rebuilding) return;
    rebuilding = true;
    sidebarItems.clear();
    sidebar.removeAllChildren();
    const int w = std::max(200, sidebar.getWidth() > 0 ? sidebar.getWidth() : 280) - 20;
    int y = 8;
    auto heading = [&](const juce::String& text) {
        auto* l = new juce::Label({}, text);
        sidebarItems.add(l);
        styleLabel(*l, 12.5f, accent, true);
        l->setBounds(10, y, w, 18);
        sidebar.addAndMakeVisible(l);
        y += 24;
    };
    auto label = [&](const juce::String& text, int height = 16, juce::Colour c = muted) {
        auto* l = new juce::Label({}, text);
        sidebarItems.add(l);
        styleLabel(*l, 12.0f, c);
        l->setBounds(10, y, w, height);
        sidebar.addAndMakeVisible(l);
        y += height + 2;
        return l;
    };
    auto combo = [&](const juce::StringArray& items, int selected, std::function<void(int)> onChange) {
        auto* c = new juce::ComboBox();
        sidebarItems.add(c);
        styleCombo(*c);
        for (int i = 0; i < items.size(); ++i) c->addItem(items[i], i + 1);
        c->setSelectedItemIndex(juce::jmax(0, selected), juce::dontSendNotification);
        c->onChange = [c, onChange] { onChange(c->getSelectedItemIndex()); };
        c->setBounds(10, y, w, 26);
        sidebar.addAndMakeVisible(c);
        y += 32;
        return c;
    };
    auto field = [&](const juce::String& caption, double value, std::function<void(double)> apply) {
        label(caption);
        auto* e = new juce::TextEditor();
        sidebarItems.add(e);
        styleEditor(*e);
        e->setText(juce::String(value, 3).trimCharactersAtEnd("0").trimCharactersAtEnd("."), false);
        auto commit = [e, apply, value] { apply(parseMm(e->getText(), value)); };
        e->onReturnKey = commit;
        e->onFocusLost = commit;
        e->setBounds(10, y, w, 26);
        sidebar.addAndMakeVisible(e);
        y += 32;
    };
    auto button = [&](const juce::String& text, std::function<void()> click) {
        auto* b = new juce::TextButton(text);
        sidebarItems.add(b);
        styleButton(*b);
        b->onClick = click;
        b->setBounds(10, y, w, 28);
        sidebar.addAndMakeVisible(b);
        y += 34;
    };

    heading("BOARD");
    const juce::StringArray sources { "Standard board", "Shape", "Draw your own" };
    const int sourceIndex = board.source == "standard" ? 0 : board.source == "shape" ? 1 : 2;
    // Changing the source rebuilds this sidebar, so it runs after the combo's callback returns.
    combo(sources, sourceIndex, [this](int i) {
        juce::Component::SafePointer<PcbPanel> safe(this);
        juce::MessageManager::callAsync([safe, i] {
            if (safe == nullptr) return;
            auto& self = *safe;
            if (i == 0) self.setDesign(pcb::BoardDesign::standard(self.board.standardId));
            else if (i == 1) self.setDesign(pcb::BoardDesign::fromShape(self.board.shape, self.board.shapeParams));
            else
            {
                auto next = self.board;
                next.source = "custom";
                self.edited(next);
                self.rebuildSidebar();
            }
        });
    });

    if (board.source == "standard")
    {
        juce::StringArray names;
        int selected = 0;
        const auto& list = djehuti::route::standardBoards();
        for (size_t i = 0; i < list.size(); ++i)
        {
            names.add(list[i].name);
            if (list[i].id == board.standardId.toStdString()) selected = (int)i;
        }
        combo(names, selected, [this](int i) {
            const auto& list2 = djehuti::route::standardBoards();
            if (i >= 0 && i < (int)list2.size()) juce::MessageManager::callAsync([this, id = juce::String(list2[(size_t)i].id)] { setDesign(pcb::BoardDesign::standard(id)); });
        });
        if (const auto* spec = djehuti::route::findStandardBoard(board.standardId.toStdString()))
            label(spec->description, 64, faint)->setJustificationType(juce::Justification::topLeft);
    }
    else if (board.source == "shape")
    {
        juce::StringArray names;
        int selected = 0;
        for (size_t i = 0; i < pcb::shapes().size(); ++i)
        {
            names.add(pcb::shapes()[i].name);
            if (pcb::shapes()[i].id == board.shape) selected = (int)i;
        }
        combo(names, selected, [this](int i) {
            if (i >= 0 && i < (int)pcb::shapes().size())
                juce::MessageManager::callAsync([this, id = pcb::shapes()[(size_t)i].id] { setDesign(pcb::BoardDesign::fromShape(id, {})); });
        });
        if (const auto* spec = pcb::findShape(board.shape))
            for (const auto& p : spec->params)
            {
                const auto found = board.shapeParams.find(p.key);
                const double value = found != board.shapeParams.end() ? found->second : p.defaultValue;
                field(p.label + (p.key == "sides" ? juce::String() : juce::String(" (mm)")), value, [this, key = p.key](double v) {
                    auto params = board.shapeParams;
                    if (std::abs(params[key] - v) < 1e-12) return;
                    params[key] = v;
                    auto next = pcb::BoardDesign::fromShape(board.shape, params);
                    next.holes = board.holes;
                    next.cutouts = board.cutouts;
                    next.layers = board.layers;
                    next.thickness = board.thickness;
                    next.edgeClearance = board.edgeClearance;
                    edited(next);
                    juce::MessageManager::callAsync([this] { canvas->fit(); });
                });
            }
    }
    else
    {
        label("Any straight-edged outline. Drag corners, click an edge to add a corner, right-click a corner to delete it.", 50, faint);
        button("Draw a new outline", [this] { canvas->startDrawing(); });
    }

    heading("HOLES AND CUTOUTS");
    static double holeDiameter = 3.2;
    field("Mounting hole diameter (mm)", holeDiameter, [](double v) { holeDiameter = std::max(0.1, v); });
    button("Add mounting hole", [this] {
        auto next = board;
        const auto c = board.bounds().getCentre();
        next.holes.push_back({ { std::round(c.x * 2.0) / 2.0, std::round(c.y * 2.0) / 2.0 }, holeDiameter });
        edited(next);
    });
    button("Add 10 x 5 mm cutout", [this] {
        auto next = board;
        const auto c = board.bounds().getCentre();
        const double x = std::round(c.x) - 5.0, yy = std::round(c.y) - 2.5;
        next.cutouts.push_back({ { x, yy }, { x + 10.0, yy }, { x + 10.0, yy + 5.0 }, { x, yy + 5.0 } });
        edited(next);
    });

    heading("STACKUP");
    const juce::StringArray layerChoices { "1", "2", "4", "6", "8" };
    combo(layerChoices, juce::jmax(0, layerChoices.indexOf(juce::String(board.layers))), [this, layerChoices](int i) {
        auto next = board;
        next.layers = layerChoices[i].getIntValue();
        edited(next);
    });
    field("Board thickness (mm)", board.thickness, [this](double v) { if (std::abs(v - board.thickness) > 1e-9) { auto next = board; next.thickness = v; edited(next); } });
    field("Copper to edge clearance (mm)", board.edgeClearance, [this](double v) { if (std::abs(v - board.edgeClearance) > 1e-9) { auto next = board; next.edgeClearance = v; edited(next); } });

    heading("PARTS AND ROUTING");
    auto later = [this](std::function<void(PcbPanel&)> action) {
        // Buttons rebuild this sidebar, so their work runs after the click returns.
        juce::Component::SafePointer<PcbPanel> safe(this);
        return [safe, action] { juce::MessageManager::callAsync([safe, action] { if (safe != nullptr) action(*safe); }); };
    };
    button("Update parts from schematic", later([](PcbPanel& p) { p.syncFromSchematic(); }));
    button("Auto place all parts", later([](PcbPanel& p) { p.autoPlace(); }));
    if (const auto* part = layout.find(selectedPart))
    {
        label(part->refdes + "  " + part->value + "  (" + part->symbolId + ")", 16, textColour);
        const auto options = pcb::footprintsFor(part->symbolId);
        combo(options, options.indexOf(part->footprint), [this, options, refdes = part->refdes](int i) {
            juce::Component::SafePointer<PcbPanel> safe(this);
            juce::MessageManager::callAsync([safe, i, options, refdes] {
                if (safe == nullptr || i < 0 || i >= options.size()) return;
                auto next = safe->layout;
                if (auto* p = next.find(refdes); p != nullptr && p->footprint != options[i])
                {
                    p->footprint = options[i];
                    next.clearRoute();
                    safe->setLayout(next);
                }
            });
        });
        if (const auto* fp = pcb::findFootprint(part->footprint))
            label(fp->description, 34, faint);
        button("Rotate 90 deg (R)", later([](PcbPanel& p) { p.canvas->rotateSelected(); p.rebuildSidebar(); }));
    }
    auto rule = [&](const juce::String& caption, double value, std::function<void(pcb::RouteRules&, double)> set) {
        field(caption, value, [this, set, value](double v) {
            if (v <= 0.0 || std::abs(v - value) < 1e-9) return;
            auto next = layout;
            set(next.rules, v);
            next.clearRoute();
            editedLayout(next);
        });
    };
    rule("Track width (mm)", layout.rules.trackWidth, [](pcb::RouteRules& r, double v) { r.trackWidth = v; });
    rule("Clearance (mm)", layout.rules.clearance, [](pcb::RouteRules& r, double v) { r.clearance = v; });
    rule("Via diameter (mm)", layout.rules.viaDiameter, [](pcb::RouteRules& r, double v) { r.viaDiameter = v; });
    rule("Via drill (mm)", layout.rules.viaDrill, [](pcb::RouteRules& r, double v) { r.viaDrill = v; });
    button("Route board", later([](PcbPanel& p) { p.route(); }));
    button("Clear routing", later([](PcbPanel& p) { auto next = p.layout; next.clearRoute(); p.lastReport.clear(); p.setLayout(next); }));
    routeInfo.setBounds(10, y, w, 190);
    sidebar.addAndMakeVisible(routeInfo);
    y += 196;
    button("Check board against schematic", later([](PcbPanel& p) { p.verify(); p.rebuildSidebar(); }));
    verifyInfo.setBounds(10, y, w, 200);
    sidebar.addAndMakeVisible(verifyInfo);
    y += 206;

    heading("TEXT AND GRAPHICS");
    auto textField = [&](const juce::String& caption, const juce::String& value, std::function<void(juce::String)> apply) {
        label(caption);
        auto* e = new juce::TextEditor();
        sidebarItems.add(e);
        styleEditor(*e);
        e->setText(value, false);
        auto commit = [e, apply, value] { if (e->getText() != value) apply(e->getText()); };
        e->onReturnKey = commit;
        e->onFocusLost = commit;
        e->setBounds(10, y, w, 26);
        sidebar.addAndMakeVisible(e);
        y += 32;
    };
    const auto& layerNames = pcb::artLayers();
    const juce::StringArray layerLabels { "Top silkscreen", "Bottom silkscreen", "Top copper", "Bottom copper" };
    label("New text and graphics go on:");
    combo(layerLabels, layerNames.indexOf(artLayer), [this, layerNames](int i) { artLayer = layerNames[i]; });
    field("Line width (mm)", artLineWidth, [this](double v) { if (v > 0.0) artLineWidth = v; });
    combo({ "Outlined", "Filled" }, artFilled ? 1 : 0, [this](int i) { artFilled = i == 1; });
    button("Add text", later([](PcbPanel& p) {
        auto next = p.layout;
        pcb::BoardText t;
        t.at = p.board.bounds().getCentre();
        t.layer = p.artLayer;
        t.lineWidth = std::max(0.15, p.artLineWidth);
        next.texts.push_back(t);
        p.commitArt(next, t.layer.endsWith(".Cu"));
        p.canvas->selectText((int)p.layout.texts.size() - 1);
        p.selectionChanged();
    }));
    for (const auto& [kind, caption] : std::vector<std::pair<juce::String, juce::String>> {
             { "line", "Draw line" }, { "rect", "Draw rectangle" }, { "circle", "Draw circle" }, { "arc", "Draw arc" }, { "polygon", "Draw polygon" } })
        button(caption, [this, kind = kind] { canvas->startArt(kind); });

    if (selectedText >= 0 && selectedText < (int)layout.texts.size())
    {
        const auto t = layout.texts[(size_t)selectedText];
        const int index = selectedText;
        label("Selected text " + juce::String(index + 1), 16, textColour);
        auto editText = [this, index](std::function<void(pcb::BoardText&)> change) {
            auto next = layout;
            if (index >= (int)next.texts.size()) return;
            const bool wasCopper = next.texts[(size_t)index].layer.endsWith(".Cu");
            change(next.texts[(size_t)index]);
            commitArt(next, wasCopper || next.texts[(size_t)index].layer.endsWith(".Cu"));
        };
        textField("Text", t.text, [editText](juce::String v) { editText([v](pcb::BoardText& x) { x.text = v; }); });
        field("Height (mm)", t.height, [editText](double v) { if (v > 0.0) editText([v](pcb::BoardText& x) { x.height = v; }); });
        field("Line width (mm)", t.lineWidth, [editText](double v) { if (v > 0.0) editText([v](pcb::BoardText& x) { x.lineWidth = v; }); });
        field("Rotation (deg)", t.rotation, [editText](double v) { editText([v](pcb::BoardText& x) { x.rotation = v; }); });
        combo({ "Align left", "Align centre", "Align right" }, t.align + 1, [editText](int i) {
            juce::MessageManager::callAsync([editText, i] { editText([i](pcb::BoardText& x) { x.align = i - 1; }); });
        });
        combo(layerLabels, layerNames.indexOf(t.layer), [editText, layerNames](int i) {
            juce::MessageManager::callAsync([editText, layerNames, i] { editText([layerNames, i](pcb::BoardText& x) { x.layer = layerNames[i]; }); });
        });
        button("Delete text", later([index](PcbPanel& p) {
            auto next = p.layout;
            if (index >= (int)next.texts.size()) return;
            const bool copper = next.texts[(size_t)index].layer.endsWith(".Cu");
            next.texts.erase(next.texts.begin() + index);
            p.canvas->clearSelection();
            p.commitArt(next, copper);
            p.selectionChanged();
        }));
    }
    if (selectedGraphic >= 0 && selectedGraphic < (int)layout.graphics.size())
    {
        const auto gr = layout.graphics[(size_t)selectedGraphic];
        const int index = selectedGraphic;
        label("Selected graphic " + juce::String(index + 1) + " (" + gr.kind + ")", 16, textColour);
        auto editGraphic = [this, index](std::function<void(pcb::BoardGraphic&)> change) {
            auto next = layout;
            if (index >= (int)next.graphics.size()) return;
            const bool wasCopper = next.graphics[(size_t)index].layer.endsWith(".Cu");
            change(next.graphics[(size_t)index]);
            commitArt(next, wasCopper || next.graphics[(size_t)index].layer.endsWith(".Cu"));
        };
        field("Line width (mm)", gr.lineWidth, [editGraphic](double v) { if (v > 0.0) editGraphic([v](pcb::BoardGraphic& x) { x.lineWidth = v; }); });
        if (gr.kind == "circle" || gr.kind == "arc")
            field("Radius (mm)", gr.radius, [editGraphic](double v) { if (v > 0.0) editGraphic([v](pcb::BoardGraphic& x) { x.radius = v; }); });
        if (gr.kind != "line" && gr.kind != "arc")
            combo({ "Outlined", "Filled" }, gr.filled ? 1 : 0, [editGraphic](int i) {
                juce::MessageManager::callAsync([editGraphic, i] { editGraphic([i](pcb::BoardGraphic& x) { x.filled = i == 1; }); });
            });
        combo(layerLabels, layerNames.indexOf(gr.layer), [editGraphic, layerNames](int i) {
            juce::MessageManager::callAsync([editGraphic, layerNames, i] { editGraphic([layerNames, i](pcb::BoardGraphic& x) { x.layer = layerNames[i]; }); });
        });
        button("Delete graphic", later([index](PcbPanel& p) {
            auto next = p.layout;
            if (index >= (int)next.graphics.size()) return;
            const bool copper = next.graphics[(size_t)index].layer.endsWith(".Cu");
            next.graphics.erase(next.graphics.begin() + index);
            p.canvas->clearSelection();
            p.commitArt(next, copper);
            p.selectionChanged();
        }));
    }

    heading("FAB OUTPUT");
    auto fabRule = [&](const juce::String& caption, double value, std::function<void(pcb::FabRules&, double)> set) {
        field(caption, value, [this, set, value](double v) {
            if (v < 0.0 || std::abs(v - value) < 1e-9) return;
            auto next = layout;
            set(next.fab, v);
            editedLayout(next);
        });
    };
    fabRule("Solder mask expansion (mm)", layout.fab.maskExpansion, [](pcb::FabRules& f, double v) { f.maskExpansion = v; });
    fabRule("Paste reduction (mm)", layout.fab.pasteReduction, [](pcb::FabRules& f, double v) { f.pasteReduction = v; });
    fabRule("Silkscreen line width (mm)", layout.fab.silkLineWidth, [](pcb::FabRules& f, double v) { if (v > 0.0) f.silkLineWidth = v; });
    fabRule("Part label height (mm)", layout.fab.labelHeight, [](pcb::FabRules& f, double v) { if (v > 0.0) f.labelHeight = v; });
    combo({ "Vias covered by mask (tented)", "Vias open" }, layout.fab.tentVias ? 0 : 1, [this](int i) {
        juce::MessageManager::callAsync([this, i] { auto next = layout; next.fab.tentVias = i == 0; editedLayout(next); rebuildSidebar(); });
    });
    combo({ "Part labels on silkscreen", "No part labels" }, layout.fab.partLabels ? 0 : 1, [this](int i) {
        juce::MessageManager::callAsync([this, i] { auto next = layout; next.fab.partLabels = i == 0; editedLayout(next); rebuildSidebar(); });
    });
    button("Export fab files", later([](PcbPanel& p) { p.exportFab(); p.rebuildSidebar(); }));
    exportInfo.setText(lastExportText, juce::dontSendNotification);
    exportInfo.setBounds(10, y, w, 150);
    sidebar.addAndMakeVisible(exportInfo);
    y += 156;

    heading("SNAP");
    const juce::StringArray snaps { "0.1", "0.25", "0.5", "1", "1.27", "2.54" };
    combo(snaps, juce::jmax(0, snaps.indexOf(juce::String(canvas->snap).trimCharactersAtEnd("0").trimCharactersAtEnd("."))), [this, snaps](int i) {
        canvas->snap = snaps[i].getDoubleValue();
    });
    button("Zoom to fit", [this] { canvas->fit(); });
    button("Undo (Ctrl+Z)", [this] { undo(); });

    updateInfo();
    info.setBounds(10, y, w, 52);
    sidebar.addAndMakeVisible(info);
    y += 56;
    problems.setBounds(10, y, w, 90);
    sidebar.addAndMakeVisible(problems);
    y += 96;
    sidebar.setSize(std::max(200, sidebarViewport.getWidth() - sidebarViewport.getScrollBarThickness() - 2), y);
    rebuilding = false;
}
