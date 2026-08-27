#include <juce_gui_basics/juce_gui_basics.h>
#include "HarmoniaApp.h"

class HarmoniaApplication : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "Harmonia"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }
    
    void initialise(const juce::String&) override {
        mainWindow = std::make_unique<MainWindow>(getApplicationName());
    }
    void shutdown() override { mainWindow = nullptr; }
    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted(const juce::String&) override {}
    
    class MainWindow : public juce::DocumentWindow {
    public:
        MainWindow(const juce::String& name)
            : DocumentWindow(name, juce::Colour(0xff050510), DocumentWindow::allButtons) {
            setUsingNativeTitleBar(true);
            setContentOwned(new Harmonia::HarmoniaApp(), true);
            setResizable(true, true);
            centreWithSize(1600, 1000);
            setVisible(true);
        }
        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }
    };
    
private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(HarmoniaApplication)
