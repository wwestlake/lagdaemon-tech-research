#include "CircuitHierarchyPanel.h"

class CircuitHierarchyPanel::SheetItem final : public juce::TreeViewItem
{
public:
    SheetItem(CircuitHierarchyPanel& ownerIn, const circuit_hierarchy::Row& rowIn)
        : owner(ownerIn), row(rowIn), id(juce::String(rowIn.id)) {}

    bool mightContainSubItems() override { return getNumSubItems() > 0; }
    juce::String getUniqueName() const override { return "sheet:" + id; }
    int getItemHeight() const override { return 22; }

    void paintItem(juce::Graphics& g, int width, int height) override
    {
        const bool active = id == owner.current;
        if (active)
        {
            g.setColour(juce::Colour(0xff1f6f8b));
            g.fillRoundedRectangle(juce::Rectangle<float>(0.0f, 1.0f, (float)width - 2.0f, (float)height - 2.0f), 3.0f);
        }
        auto name = juce::String(row.name).isNotEmpty() ? juce::String(row.name) : id;
        if (row.missingParent)
            name << "  (parent sheet missing)";
        g.setColour(active ? juce::Colours::white : row.missingParent ? juce::Colour(0xffd9a441) : juce::Colour(0xffc9d4dd));
        g.setFont(juce::Font(13.0f, active ? juce::Font::bold : juce::Font::plain));
        g.drawText(name, 6, 0, width - 8, height, juce::Justification::centredLeft, true);
    }

    void itemClicked(const juce::MouseEvent&) override
    {
        if (owner.source.openSheet)
            owner.source.openSheet(id);
    }

    void itemOpennessChanged(bool isNowOpen) override
    {
        if (isNowOpen) owner.collapsed.erase(id);
        else owner.collapsed.insert(id);
    }

private:
    CircuitHierarchyPanel& owner;
    circuit_hierarchy::Row row;
    juce::String id;
};

CircuitHierarchyPanel::CircuitHierarchyPanel(Source sourceIn) : source(std::move(sourceIn))
{
    tree.setRootItemVisible(true);
    tree.setDefaultOpenness(true);
    tree.setIndentSize(16);
    tree.setColour(juce::TreeView::backgroundColourId, juce::Colour(0xff0e141a));
    tree.setColour(juce::TreeView::linesColourId, juce::Colour(0xff3a4a58));
    addAndMakeVisible(tree);
    timerCallback();
    startTimer(300);
}

CircuitHierarchyPanel::~CircuitHierarchyPanel()
{
    stopTimer();
    tree.setRootItem(nullptr);
}

void CircuitHierarchyPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff0e141a));
}

void CircuitHierarchyPanel::resized()
{
    tree.setBounds(getLocalBounds().reduced(4));
}

void CircuitHierarchyPanel::timerCallback()
{
    const auto sheets = source.sheets ? source.sheets() : std::vector<circuit_hierarchy::Sheet> {};
    const auto rootName = source.rootName ? source.rootName() : juce::String("Main");
    const auto rows = circuit_hierarchy::flatten(sheets, rootName.toStdString());
    const auto now = source.currentSheet ? source.currentSheet() : juce::String();

    juce::String sig;
    for (const auto& r : rows)
        sig << r.depth << '|' << juce::String(r.id) << '|' << juce::String(r.name) << '|' << (int)r.missingParent << '\n';
    if (sig != signature)
    {
        signature = sig;
        current = now;
        rebuild(rows);
    }
    else if (now != current)
    {
        current = now;
        tree.repaint();
    }
}

void CircuitHierarchyPanel::rebuild(const std::vector<circuit_hierarchy::Row>& rows)
{
    tree.setRootItem(nullptr);
    root.reset();
    if (rows.empty())
        return;

    // Rows are depth-first, so each row's parent is the latest row one level up.
    root = std::make_unique<SheetItem>(*this, rows.front());
    std::vector<juce::TreeViewItem*> stack { root.get() };
    for (size_t i = 1; i < rows.size(); ++i)
    {
        const auto depth = (size_t)std::max(1, rows[i].depth);
        stack.resize(std::min(stack.size(), depth));
        auto* item = new SheetItem(*this, rows[i]);
        stack.back()->addSubItem(item);
        stack.push_back(item);
    }
    tree.setRootItem(root.get());

    std::function<void(juce::TreeViewItem*)> open = [&](juce::TreeViewItem* item) {
        auto* sheetItem = dynamic_cast<SheetItem*>(item);
        const auto id = sheetItem != nullptr ? item->getUniqueName().fromFirstOccurrenceOf("sheet:", false, false) : juce::String();
        const bool keepOpen = collapsed.count(id) == 0;
        for (int c = 0; c < item->getNumSubItems(); ++c)
            open(item->getSubItem(c));
        item->setOpen(keepOpen);
    };
    open(root.get());
    tree.repaint();
}
