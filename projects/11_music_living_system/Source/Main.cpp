#include <juce_gui_basics/juce_gui_basics.h>
#include "MainComponent.h"

class MusicLivingSystemApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "Music Living System"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override           { return true; }

    void initialise(const juce::String&) override
    {
        mainWindow = std::make_unique<MainWindow>(getApplicationName());
    }

    void shutdown() override { mainWindow = nullptr; }

    void systemRequestedQuit() override { quit(); }

    void anotherInstanceStarted(const juce::String&) override {}

    //==========================================================================
    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow(const juce::String& name)
            : DocumentWindow(name,
                             juce::Colour(0xff0d0d1a),
                             DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(), true);
            setResizable(true, true);
            centreWithSize(1400, 900);
            setVisible(true);
        }

        void closeButtonPressed() override
        {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(MusicLivingSystemApp)
