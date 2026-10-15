#include "DjehutiMenuBarComponent.h"

//==============================================================================
DjehutiHeaderLookAndFeel::DjehutiHeaderLookAndFeel()
{
}

int DjehutiHeaderLookAndFeel::getDefaultMenuBarHeight()
{
    return 42;
}

juce::Font DjehutiHeaderLookAndFeel::getMenuBarFont(juce::MenuBarComponent&, int, const juce::String&)
{
    return juce::Font("Segoe UI", 14.5f, juce::Font::plain);
}

int DjehutiHeaderLookAndFeel::getMenuBarItemWidth(juce::MenuBarComponent& bar, int itemIndex, const juce::String& itemText)
{
    return getMenuBarFont(bar, itemIndex, itemText).getStringWidth(itemText) + 28;
}

void DjehutiHeaderLookAndFeel::drawMenuBarBackground(juce::Graphics& g, int, int, bool, juce::MenuBarComponent&)
{
    g.fillAll(juce::Colour(0xff1b2028));
}

void DjehutiHeaderLookAndFeel::drawMenuBarItem(juce::Graphics& g, int width, int height,
                                               int itemIndex, const juce::String& itemText,
                                               bool isMouseOverItem, bool isMenuOpen,
                                               bool, juce::MenuBarComponent& menuBar)
{
    if (isMenuOpen)
    {
        g.setColour(juce::Colour(0xff33424d));
        g.fillRoundedRectangle(juce::Rectangle<float>(2.0f, 5.0f, (float)width - 4.0f, (float)height - 10.0f), 4.0f);
        g.setColour(juce::Colours::white);
    }
    else if (isMouseOverItem)
    {
        g.setColour(juce::Colour(0xff26303c));
        g.fillRoundedRectangle(juce::Rectangle<float>(2.0f, 5.0f, (float)width - 4.0f, (float)height - 10.0f), 4.0f);
        g.setColour(juce::Colour(0xffe2e8f0));
    }
    else
    {
        g.setColour(juce::Colour(0xff94a3b8));
    }

    g.setFont(getMenuBarFont(menuBar, itemIndex, itemText));
    g.drawFittedText(itemText, 0, 0, width, height, juce::Justification::centred, 1);
}

//==============================================================================
DjehutiMenuBarComponent::DjehutiMenuBarComponent(juce::MenuBarModel* model)
    : juce::MenuBarComponent(model),
      headerLookAndFeel(std::make_unique<DjehutiHeaderLookAndFeel>())
{
    setLookAndFeel(headerLookAndFeel.get());
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
}

DjehutiMenuBarComponent::~DjehutiMenuBarComponent()
{
    setLookAndFeel(nullptr);
}

int DjehutiMenuBarComponent::getActiveMenuIndex() const noexcept
{
    return activeMenuIndex;
}

void DjehutiMenuBarComponent::setActiveMenuIndex(int idx)
{
    activeMenuIndex = idx;
    repaint();
}

int DjehutiMenuBarComponent::getItemIndexAt(juce::Point<int> p) const
{
    if (p.x < 0 || p.y < 0 || p.x >= getWidth() || p.y >= getHeight())
        return -1;

    auto* m = getModel();
    if (m == nullptr)
        return -1;

    const auto names = m->getMenuBarNames();
    int x = 0;
    auto& lf = const_cast<DjehutiMenuBarComponent*>(this)->getLookAndFeel();

    for (int i = 0; i < names.size(); ++i)
    {
        const int w = lf.getMenuBarItemWidth(*const_cast<DjehutiMenuBarComponent*>(this), i, names[i]);
        if (p.x >= x && p.x < x + w)
            return i;
        x += w;
    }

    return -1;
}

juce::Rectangle<int> DjehutiMenuBarComponent::getItemBounds(int itemIndex) const
{
    auto* m = getModel();
    if (m == nullptr || itemIndex < 0)
        return {};

    const auto names = m->getMenuBarNames();
    if (itemIndex >= names.size())
        return {};

    int x = 0;
    auto& lf = const_cast<DjehutiMenuBarComponent*>(this)->getLookAndFeel();

    for (int i = 0; i < itemIndex; ++i)
        x += lf.getMenuBarItemWidth(*const_cast<DjehutiMenuBarComponent*>(this), i, names[i]);

    const int w = lf.getMenuBarItemWidth(*const_cast<DjehutiMenuBarComponent*>(this), itemIndex, names[itemIndex]);
    return { x, 0, w, getHeight() };
}

void DjehutiMenuBarComponent::openMenu(int itemIndex)
{
    if (itemIndex < 0)
    {
        closeActiveMenu();
        return;
    }

    auto* m = getModel();
    if (m == nullptr)
        return;

    const auto names = m->getMenuBarNames();
    if (!juce::isPositiveAndBelow(itemIndex, names.size()))
    {
        closeActiveMenu();
        return;
    }

    if (activeMenuIndex < 0)
        m->handleMenuBarActivate(true);

    // Dismiss any currently open popup menu
    juce::PopupMenu::dismissAllActiveMenus();

    activeMenuIndex = itemIndex;
    hoveredItemIndex = itemIndex;
    repaint();

    const int seq = ++currentMenuSequence;

    auto menu = m->getMenuForIndex(itemIndex, names[itemIndex]);
    menu.setLookAndFeel(&getLookAndFeel());
    const auto itemBounds = getItemBounds(itemIndex);
    const auto screenBounds = localAreaToGlobal(itemBounds);

    menu.showMenuAsync(
        juce::PopupMenu::Options()
            .withTargetScreenArea(screenBounds)
            .withTargetComponent(nullptr)
            .withMinimumWidth(itemBounds.getWidth()),
        [safeThis = juce::Component::SafePointer<DjehutiMenuBarComponent>(this), itemIndex, seq](int result)
        {
            if (safeThis == nullptr || seq != safeThis->currentMenuSequence)
                return;

            safeThis->menuDismissed(itemIndex, result);
        });
}

void DjehutiMenuBarComponent::closeActiveMenu()
{
    ++currentMenuSequence;
    const bool wasActive = (activeMenuIndex >= 0);
    activeMenuIndex = -1;
    hoveredItemIndex = -1;
    juce::PopupMenu::dismissAllActiveMenus();
    repaint();

    if (wasActive)
    {
        if (auto* m = getModel())
            m->handleMenuBarActivate(false);
    }
}

void DjehutiMenuBarComponent::menuDismissed(int itemIndex, int result)
{
    justDismissedMenuIndex = itemIndex;
    justDismissedTime = juce::Time::getMillisecondCounter();

    if (activeMenuIndex == itemIndex)
    {
        activeMenuIndex = -1;
        hoveredItemIndex = -1;
        repaint();

        if (auto* m = getModel())
            m->handleMenuBarActivate(false);
    }

    if (result != 0)
    {
        if (auto* m = getModel())
            m->menuItemSelected(result, itemIndex);
    }
}

void DjehutiMenuBarComponent::paint(juce::Graphics& g)
{
    auto& lf = getLookAndFeel();
    lf.drawMenuBarBackground(g, getWidth(), getHeight(), isMouseOver(true), *this);

    auto* m = getModel();
    if (m == nullptr)
        return;

    const auto names = m->getMenuBarNames();
    int x = 0;

    for (int i = 0; i < names.size(); ++i)
    {
        const int w = lf.getMenuBarItemWidth(*this, i, names[i]);
        const bool isOpen = (i == activeMenuIndex);
        const bool isHovered = (i == hoveredItemIndex);

        juce::Graphics::ScopedSaveState ss(g);
        g.setOrigin(x, 0);
        g.reduceClipRegion(0, 0, w, getHeight());

        lf.drawMenuBarItem(g, w, getHeight(), i, names[i], isHovered, isOpen, isMouseOver(true), *this);

        x += w;
    }
}

void DjehutiMenuBarComponent::mouseDown(const juce::MouseEvent& e)
{
    const auto localPos = e.getEventRelativeTo(this).getPosition();
    const bool isInsideBar = (localPos.x >= 0 && localPos.y >= 0 && localPos.x < getWidth() && localPos.y < getHeight());

    if (!isInsideBar)
    {
        closeActiveMenu();
        return;
    }

    const int clickedItem = getItemIndexAt(localPos);
    if (clickedItem < 0)
    {
        closeActiveMenu();
        return;
    }

    const auto now = juce::Time::getMillisecondCounter();
    const bool wasJustDismissedByThisClick = (clickedItem == justDismissedMenuIndex && (now - justDismissedTime < 150));

    if (wasJustDismissedByThisClick || clickedItem == activeMenuIndex)
    {
        // Toggle close: clicking the active menu title closes it cleanly
        closeActiveMenu();
        justDismissedMenuIndex = -1;
        return;
    }

    // Direct single-click opening and switching between menus
    openMenu(clickedItem);
}

void DjehutiMenuBarComponent::mouseUp(const juce::MouseEvent&)
{
}

void DjehutiMenuBarComponent::mouseMove(const juce::MouseEvent& e)
{
    // Pointer movement alone must never open or switch menus.
    if (activeMenuIndex < 0)
    {
        const auto p = e.getEventRelativeTo(this).getPosition();
        const int item = getItemIndexAt(p);
        if (item != hoveredItemIndex)
        {
            hoveredItemIndex = item;
            repaint();
        }
    }
}

void DjehutiMenuBarComponent::mouseDrag(const juce::MouseEvent&)
{
}

void DjehutiMenuBarComponent::mouseEnter(const juce::MouseEvent& e)
{
    if (activeMenuIndex < 0)
    {
        const auto p = e.getEventRelativeTo(this).getPosition();
        const int item = getItemIndexAt(p);
        if (item != hoveredItemIndex)
        {
            hoveredItemIndex = item;
            repaint();
        }
    }
}

void DjehutiMenuBarComponent::mouseExit(const juce::MouseEvent&)
{
    if (hoveredItemIndex != -1 && activeMenuIndex < 0)
    {
        hoveredItemIndex = -1;
        repaint();
    }
}

void DjehutiMenuBarComponent::handleCommandMessage(int commandId)
{
    if (commandId != 0)
    {
        if (auto* m = getModel())
            m->menuItemSelected(commandId, activeMenuIndex >= 0 ? activeMenuIndex : 0);
    }

    closeActiveMenu();
}

bool DjehutiMenuBarComponent::keyPressed(const juce::KeyPress& key)
{
    auto* m = getModel();
    if (m == nullptr)
        return false;

    const auto names = m->getMenuBarNames();
    const int numMenus = names.size();
    if (numMenus <= 0)
        return false;

    if (activeMenuIndex >= 0)
    {
        if (key.isKeyCode(juce::KeyPress::leftKey))
        {
            openMenu((activeMenuIndex + numMenus - 1) % numMenus);
            return true;
        }

        if (key.isKeyCode(juce::KeyPress::rightKey))
        {
            openMenu((activeMenuIndex + 1) % numMenus);
            return true;
        }

        if (key.isKeyCode(juce::KeyPress::escapeKey))
        {
            closeActiveMenu();
            return true;
        }
    }

    return false;
}
