#include <JuceHeader.h>

#include "ElectronicsWorkbench.h"

class ElectronicsLabApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Djehuti Electronics Lab"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String&) override
    {
        mainWindow = std::make_unique<MainWindow>(getApplicationName());
    }

    void shutdown() override
    {
        mainWindow = nullptr;
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted(const juce::String&) override {}

private:
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        explicit MainWindow(const juce::String& name)
            : DocumentWindow(name,
                             juce::Colour(0xff171b20),
                             juce::DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new ElectronicsWorkbench(), true);
            setResizable(true, true);
            setResizeLimits(1100, 720, 3840, 2160);
            centreWithSize(1500, 920);
            setVisible(true);
        }

        void closeButtonPressed() override
        {
            if (auto* workbench = dynamic_cast<ElectronicsWorkbench*>(getContentComponent()))
                workbench->confirmCloseThen([] { juce::JUCEApplication::getInstance()->quit(); });
            else
                juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
    };

    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(ElectronicsLabApplication)
