#include "PcbPanel.h"

#include <djehuti_route/outline.h>

#include <cmath>
#include <limits>

namespace
{
const juce::Colour background(0xff10161d), sideColour(0xff151a20), raised(0xff1d2731), border(0xff33424d);
const juce::Colour textColour(0xffdce9ee), muted(0xff93a7b0), faint(0xff71808c), accent(0xff78dcca), warning(0xffffb36b);
const juce::Colour boardFill(0xff1e4a2c), edgeGold(0xffd2b45a), gridMinor(0xff18212a), gridMajor(0xff222e39);

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

    void startDrawing()
    {
        drawing = true;
        drawPoints.clear();
        selection = {};
        repaint();
    }

    void fit()
    {
        const auto b = owner.board.bounds();
        if (b.isEmpty() || getWidth() < 50 || getHeight() < 50)
        {
            zoom = 5.0;
            origin = { 60.0, getHeight() - 60.0 };
            repaint();
            return;
        }
        const double margin = 70.0;
        zoom = std::min((getWidth() - 2 * margin) / std::max(1.0, b.getWidth()), (getHeight() - 2 * margin) / std::max(1.0, b.getHeight()));
        zoom = juce::jlimit(0.2, 400.0, zoom);
        origin.x = getWidth() / 2.0 - (b.getCentreX()) * zoom;
        origin.y = getHeight() / 2.0 + (b.getCentreY()) * zoom;
        repaint();
    }

    void deleteSelection()
    {
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
            owner.edited(next);
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
        else if (!drawing)
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

        // The outline being drawn.
        if (drawing)
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
            if (drawPoints.size() >= 3 && toScreen(drawPoints.front()).getDistanceFrom(e.position) <= 8.0f)
            {
                finishDrawing();
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
            deleteSelection();
            return;
        }
        selection = hitTest(e.position);
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
                owner.edited(next);
                selection = { Kind::Vertex, edge + 1, 0 };
            }
            else
            {
                panning = true;
                return;
            }
        }
        before = owner.board;
        dragStartMm = snapPoint(toMm(e.position));
        dragging = true;
        repaint();
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (panning)
        {
            origin += { (double)(e.position.x - lastMouse.x), (double)(e.position.y - lastMouse.y) };
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
            default:
                break;
        }
        owner.updateInfo();
        repaint();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        if (dragging)
        {
            dragging = false;
            if (juce::JSON::toString(owner.board.toVar(), true) != juce::JSON::toString(before.toVar(), true))
            {
                auto after = owner.board;
                owner.board = before;
                owner.edited(after); // one undo step for the whole drag
            }
        }
        panning = false;
        repaint();
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        if (drawing)
        {
            if (drawPoints.size() >= 3) finishDrawing();
            return;
        }
        if (hitTest(e.position).kind == Kind::None && edgeAt(e.position) < 0)
            fit();
    }

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        const auto anchor = toMm(e.position);
        zoom = juce::jlimit(0.2, 400.0, zoom * std::pow(1.15, wheel.deltaY * 6.0));
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
    enum class Kind { None, Vertex, Hole, Cutout, CutoutVertex };
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
        owner.edited(next);
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

void PcbPanel::edited(const pcb::BoardDesign& next)
{
    undoStack.push_back(board);
    if (undoStack.size() > 200) undoStack.pop_front();
    board = next;
    updateInfo();
    canvas->repaint();
    if (onChanged) onChanged();
}

void PcbPanel::undo()
{
    if (undoStack.empty()) return;
    board = undoStack.back();
    undoStack.pop_back();
    rebuildSidebar();
    canvas->repaint();
    if (onChanged) onChanged();
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
    const auto list = board.problems();
    problems.setText(list.isEmpty() ? juce::String() : list.joinIntoString("\n"), juce::dontSendNotification);
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
