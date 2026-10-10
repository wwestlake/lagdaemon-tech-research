#include "PinLayoutEditor.h"

class PinLayoutEditor::Rows final : public juce::ListBoxModel
{
public:
    explicit Rows(PinLayoutEditor& o) : owner(o) {}

    int getNumRows() override { return (int)owner.ports.size(); }

    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (row < 0 || row >= (int)owner.ports.size())
            return;
        if (selected)
            g.fillAll(juce::Colour(0xff23394a));
        const auto& p = owner.ports[(size_t)row];
        const auto onSide = schematic::portsOnSide(owner.ports, p.side);
        const auto position = (int)(std::find(onSide.begin(), onSide.end(), row) - onSide.begin()) + 1;
        g.setColour(juce::Colour(0xffdce9ee));
        g.setFont(juce::Font("Consolas", 13.0f, juce::Font::plain));
        g.drawText(juce::String(row + 1) + "  " + p.name, 6, 0, width / 2, height, juce::Justification::centredLeft, true);
        g.setColour(juce::Colour(0xff93a7b0));
        g.drawText(schematic::pinSideName(p.side) + " #" + juce::String(position), width / 2, 0, width / 2 - 6, height,
                   juce::Justification::centredRight, true);
    }

    void selectedRowsChanged(int row) override { owner.select(row); }

private:
    PinLayoutEditor& owner;
};

namespace
{
void style(juce::TextButton& b, bool primary)
{
    b.setColour(juce::TextButton::buttonColourId, primary ? juce::Colour(0xff2f7f73) : juce::Colour(0xff263942));
    b.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
}

const schematic::PinSide sides[] { schematic::PinSide::Left, schematic::PinSide::Right, schematic::PinSide::Top, schematic::PinSide::Bottom };
}

PinLayoutEditor::PinLayoutEditor(juce::String n, juce::String id, std::vector<schematic::BlockPort> p, SaveFn fn, bool editable)
    : name(std::move(n)), symbolId(std::move(id)), ports(std::move(p)), onSave(std::move(fn)), pinsEditable(editable)
{
    schematic::normalizePinOrders(ports);
    rows = std::make_unique<Rows>(*this);
    list.setModel(rows.get());
    list.setRowHeight(22);
    list.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff151a20));
    addAndMakeVisible(list);

    side.addItemList({ "Left", "Right", "Top", "Bottom" }, 1);
    side.onChange = [this] {
        if (selected < 0 || selected >= (int)ports.size()) return;
        const auto s = sides[juce::jlimit(0, 3, side.getSelectedId() - 1)];
        if (ports[(size_t)selected].side != s)
        {
            schematic::placePort(ports, selected, s, -1);
            refresh();
        }
    };
    addAndMakeVisible(side);

    for (auto* b : { &earlier, &later, &cancel })
    {
        style(*b, false);
        addAndMakeVisible(*b);
    }
    style(save, true);
    addAndMakeVisible(save);
    earlier.onClick = [this] { move(-1); };
    later.onClick = [this] { move(1); };
    cancel.onClick = [this] {
        if (auto* w = findParentComponentOfClass<juce::DialogWindow>()) w->exitModalState(0);
    };
    save.onClick = [this] {
        juce::String error;
        if (onSave != nullptr && !onSave(ports, error))
        {
            hint.setColour(juce::Label::textColourId, juce::Colour(0xffffb36b));
            hint.setText(error, juce::dontSendNotification);
            return;
        }
        if (auto* w = findParentComponentOfClass<juce::DialogWindow>()) w->exitModalState(1);
    };

    if (pinsEditable)
    {
        pinName.setTextToShowWhenEmpty("pin name", juce::Colour(0xff71808c));
        pinName.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff151a20));
        pinName.setColour(juce::TextEditor::textColourId, juce::Colour(0xffdce9ee));
        pinName.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff33424d));
        pinName.onReturnKey = [this] { addPin(); };
        addAndMakeVisible(pinName);
        for (auto* b : { &addButton, &renameButton, &removeButton })
        {
            style(*b, false);
            addAndMakeVisible(*b);
        }
        addButton.onClick = [this] { addPin(); };
        renameButton.onClick = [this] { renamePin(); };
        removeButton.onClick = [this] { removePin(); };
    }

    hint.setColour(juce::Label::textColourId, juce::Colour(0xff93a7b0));
    hint.setText(pinsEditable ? "A rename keeps every wire; a removed pin takes only its own wires. Changes apply on Save."
                              : "Pins keep their identity and connections; only where they are drawn changes.",
                 juce::dontSendNotification);
    addAndMakeVisible(hint);

    setSize(760, pinsEditable ? 520 : 480);
    list.selectRow(0);
    select(0);
}

PinLayoutEditor::~PinLayoutEditor()
{
    list.setModel(nullptr);
}

void PinLayoutEditor::show(juce::String n, juce::String id, std::vector<schematic::BlockPort> p, SaveFn fn, bool editable)
{
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(new PinLayoutEditor(n, std::move(id), std::move(p), std::move(fn), editable));
    options.dialogTitle = "Pin layout - " + n;
    options.dialogBackgroundColour = juce::Colour(0xff10161d);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.launchAsync();
}

void PinLayoutEditor::showError(const juce::String& text)
{
    hint.setColour(juce::Label::textColourId, juce::Colour(0xffffb36b));
    hint.setText(text, juce::dontSendNotification);
}

// The canvas checks again on Save; this catches a clash while typing.
bool PinLayoutEditor::checkName(const juce::String& text, int except)
{
    const auto n = text.trim();
    if (n.isEmpty()) { showError("Type a pin name first."); return false; }
    if (n.containsAnyOf(" .\t\"'")) { showError("Pin names cannot contain spaces, dots or quotes."); return false; }
    for (int k = 0; k < (int)ports.size(); ++k)
        if (k != except && ports[(size_t)k].name.equalsIgnoreCase(n))
        {
            showError("There is already a pin named " + ports[(size_t)k].name + "; pin names are unique on a block.");
            return false;
        }
    return true;
}

void PinLayoutEditor::addPin()
{
    if (!checkName(pinName.getText(), -1)) return;
    const auto s = selected >= 0 && selected < (int)ports.size() ? ports[(size_t)selected].side : schematic::PinSide::Left;
    ports.push_back({ pinName.getText().trim(), s }); // no id: a new pin
    schematic::placePort(ports, (int)ports.size() - 1, s, -1);
    selected = (int)ports.size() - 1;
    pinName.clear();
    hint.setColour(juce::Label::textColourId, juce::Colour(0xff93a7b0));
    hint.setText("Added " + ports.back().name + " (inside the block it gets a port bubble on Save).", juce::dontSendNotification);
    refresh();
    list.selectRow(selected);
}

void PinLayoutEditor::renamePin()
{
    if (selected < 0 || selected >= (int)ports.size()) return;
    if (!checkName(pinName.getText(), selected)) return;
    ports[(size_t)selected].name = pinName.getText().trim();
    pinName.clear();
    refresh();
}

void PinLayoutEditor::removePin()
{
    if (selected < 0 || selected >= (int)ports.size()) return;
    const auto gone = ports[(size_t)selected].name;
    ports.erase(ports.begin() + selected);
    schematic::normalizePinOrders(ports);
    selected = juce::jmin(selected, (int)ports.size() - 1);
    hint.setColour(juce::Label::textColourId, juce::Colour(0xff93a7b0));
    hint.setText("Removed " + gone + "; on Save its port bubble inside and the wires on it go too.", juce::dontSendNotification);
    refresh();
    if (selected >= 0) list.selectRow(selected);
}

void PinLayoutEditor::select(int pin)
{
    selected = pin;
    if (pin >= 0 && pin < (int)ports.size())
        for (int k = 0; k < 4; ++k)
            if (sides[k] == ports[(size_t)pin].side)
                side.setSelectedId(k + 1, juce::dontSendNotification);
    repaint();
}

void PinLayoutEditor::move(int delta)
{
    if (selected < 0 || selected >= (int)ports.size()) return;
    const auto onSide = schematic::portsOnSide(ports, ports[(size_t)selected].side);
    const auto at = (int)(std::find(onSide.begin(), onSide.end(), selected) - onSide.begin());
    schematic::placePort(ports, selected, ports[(size_t)selected].side, juce::jmax(0, at + delta));
    refresh();
}

void PinLayoutEditor::refresh()
{
    list.updateContent();
    list.repaint();
    select(selected);
}

void PinLayoutEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff10161d));
    g.setColour(juce::Colour(0xff151a20));
    g.fillRect(previewArea);
    // The symbol as the schematic draws it, scaled into the preview.
    const auto symbol = schematic::blockSymbol(ports, symbolId);
    const auto extent = schematic::extentBounds(symbol).expanded(24.0f);
    const auto area = previewArea.toFloat().reduced(10.0f);
    const auto scale = juce::jmin(1.5f, area.getWidth() / extent.getWidth(), area.getHeight() / extent.getHeight());
    juce::Graphics::ScopedSaveState state(g);
    g.addTransform(juce::AffineTransform::translation(-extent.getCentreX(), -extent.getCentreY())
                       .scaled(scale)
                       .translated(area.getCentreX(), area.getCentreY()));
    schematic::drawBlockArt(g, symbol, name);
    if (selected >= 0 && selected < (int)symbol.pins.size())
    {
        g.setColour(juce::Colour(0xffffd60a));
        g.fillEllipse(juce::Rectangle<float>(10.0f, 10.0f).withCentre(symbol.pins[(size_t)selected].offset));
    }
}

void PinLayoutEditor::resized()
{
    auto area = getLocalBounds().reduced(10);
    auto bottom = area.removeFromBottom(32);
    save.setBounds(bottom.removeFromRight(90).reduced(0, 2));
    bottom.removeFromRight(6);
    cancel.setBounds(bottom.removeFromRight(90).reduced(0, 2));
    hint.setBounds(bottom);
    area.removeFromBottom(6);
    auto left = area.removeFromLeft(280);
    if (pinsEditable)
    {
        auto edits = left.removeFromBottom(32);
        pinName.setBounds(edits.removeFromLeft(90).reduced(0, 3));
        edits.removeFromLeft(4);
        addButton.setBounds(edits.removeFromLeft(64).reduced(0, 2));
        edits.removeFromLeft(4);
        renameButton.setBounds(edits.removeFromLeft(56).reduced(0, 2));
        edits.removeFromLeft(4);
        removeButton.setBounds(edits.reduced(0, 2));
        left.removeFromBottom(6);
    }
    auto controls = left.removeFromBottom(32);
    side.setBounds(controls.removeFromLeft(100).reduced(0, 3));
    controls.removeFromLeft(6);
    earlier.setBounds(controls.removeFromLeft(80).reduced(0, 2));
    controls.removeFromLeft(4);
    later.setBounds(controls.removeFromLeft(80).reduced(0, 2));
    left.removeFromBottom(6);
    list.setBounds(left);
    area.removeFromLeft(10);
    previewArea = area;
}
