#include <JuceHeader.h>

#include "ElectronicsWorkbench.h"

#if JUCE_DEBUG && JUCE_WINDOWS
 #include <crtdbg.h>
namespace
{
// Debug builds: a C runtime assertion (STL range check, assert()) is written
// with a stack trace to debug_assertions.log and the process then breaks,
// instead of a modal Abort/Retry dialog appearing on the desktop.
void logDebugAssertion(const juce::String& message)
{
    const auto file = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                          .getChildFile("DjehutiElectronicsLab").getChildFile("debug_assertions.log");
    file.appendText(juce::Time::getCurrentTime().toISO8601(true) + "\n" + message.trim() + "\n"
                    + juce::SystemStats::getStackBacktrace() + "\n\n");
}

int reportHook(int type, char* message, int* returnValue)
{
    if (type == _CRT_WARN)
        return 0;
    logDebugAssertion(message != nullptr ? juce::String(message) : juce::String());
    *returnValue = 1;
    return 1; // handled: no dialog
}

int reportHookW(int type, wchar_t* message, int* returnValue)
{
    if (type == _CRT_WARN)
        return 0;
    logDebugAssertion(message != nullptr ? juce::String(message) : juce::String());
    *returnValue = 1;
    return 1; // handled: no dialog
}
}
#endif

class ElectronicsLabApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Djehuti Electronics Lab"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    // One copy at a time: starting the app again brings the running one forward.
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String&) override
    {
       #if JUCE_DEBUG && JUCE_WINDOWS
        _CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, reportHook);
        _CrtSetReportHookW2(_CRT_RPTHOOK_INSTALL, reportHookW);
       #endif
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

    void anotherInstanceStarted(const juce::String&) override
    {
        if (mainWindow == nullptr)
            return;
        if (mainWindow->isMinimised())
            mainWindow->setMinimised(false);
        mainWindow->setVisible(true);
        mainWindow->toFront(true);
    }

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
