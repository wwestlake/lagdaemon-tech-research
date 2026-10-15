#include "DjehutiMenuBarComponent.h"
#include "Auth/ProfileAvatarComponent.h"

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

void DjehutiMenuBarComponent::openMenu(int itemIndex)
{
    if (activeMenuIndex >= 0 && activeMenuIndex != itemIndex)
        juce::PopupMenu::dismissAllActiveMenus();

    activeMenuIndex = itemIndex;
    juce::MenuBarComponent::showMenu(itemIndex);
}

void DjehutiMenuBarComponent::closeActiveMenu()
{
    activeMenuIndex = -1;
    juce::PopupMenu::dismissAllActiveMenus();
    repaint();
}

void DjehutiMenuBarComponent::mouseDown(const juce::MouseEvent& e)
{
    const auto p = e.getEventRelativeTo(this).getPosition();
    const int item = getItemIndexAt(p);

    if (item >= 0)
    {
        if (item == getActiveMenuIndex())
        {
            closeActiveMenu();
        }
        else
        {
            openMenu(item);
        }
    }
    else
    {
        closeActiveMenu();
    }
}

void DjehutiMenuBarComponent::mouseUp(const juce::MouseEvent&)
{
}

void DjehutiMenuBarComponent::mouseMove(const juce::MouseEvent& e)
{
    const auto p = e.getEventRelativeTo(this).getPosition();
    const int item = getItemIndexAt(p);

    if (item != hoveredItemIndex)
    {
        hoveredItemIndex = item;
        repaint();
    }
}

void DjehutiMenuBarComponent::mouseDrag(const juce::MouseEvent&)
{
}

void DjehutiMenuBarComponent::mouseEnter(const juce::MouseEvent&)
{
}

void DjehutiMenuBarComponent::mouseExit(const juce::MouseEvent&)
{
    if (hoveredItemIndex != -1)
    {
        hoveredItemIndex = -1;
        repaint();
    }
}

void DjehutiMenuBarComponent::handleCommandMessage(int commandId)
{
    juce::MenuBarComponent::handleCommandMessage(commandId);
    if (juce::Component::getCurrentlyModalComponent() == nullptr)
    {
        activeMenuIndex = -1;
        repaint();
    }
}

//==============================================================================
MenuActivationWatcher::MenuActivationWatcher(DjehutiMenuBarComponent& bar, ProfileAvatarComponent& profile)
    : menuBar(bar), profileControl(profile)
{
    juce::Desktop::getInstance().addGlobalMouseListener(this);
}

MenuActivationWatcher::~MenuActivationWatcher()
{
    juce::Desktop::getInstance().removeGlobalMouseListener(this);
}

void MenuActivationWatcher::mouseDown(const juce::MouseEvent& e)
{
    if (juce::Component::getCurrentlyModalComponent() == nullptr
        && menuBar.getActiveMenuIndex() < 0
        && !profileControl.isMenuOpen())
    {
        return;
    }

    const auto screenPos = e.getScreenPosition();

    if (menuBar.isShowing())
    {
        const auto barLocal = menuBar.getLocalPoint(nullptr, screenPos);
        if (menuBar.reallyContains(barLocal, true))
        {
            const int clickedIdx = menuBar.getItemIndexAt(barLocal);
            if (clickedIdx >= 0)
            {
                if (clickedIdx == menuBar.getActiveMenuIndex())
                {
                    juce::PopupMenu::dismissAllActiveMenus();
                    menuBar.setActiveMenuIndex(-1);
                }
                else
                {
                    juce::PopupMenu::dismissAllActiveMenus();
                    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<DjehutiMenuBarComponent>(&menuBar), clickedIdx] {
                        if (safe != nullptr)
                            safe->openMenu(clickedIdx);
                    });
                }
                return;
            }
        }
    }

    if (profileControl.isShowing())
    {
        const auto profLocal = profileControl.getLocalPoint(nullptr, screenPos);
        if (profileControl.reallyContains(profLocal, true))
        {
            if (profileControl.isMenuOpen())
            {
                juce::PopupMenu::dismissAllActiveMenus();
                profileControl.setMenuOpen(false);
            }
            else
            {
                juce::PopupMenu::dismissAllActiveMenus();
                juce::MessageManager::callAsync([safe = juce::Component::SafePointer<ProfileAvatarComponent>(&profileControl)] {
                    if (safe != nullptr)
                        safe->showAccountMenu();
                });
            }
            return;
        }
    }
}
