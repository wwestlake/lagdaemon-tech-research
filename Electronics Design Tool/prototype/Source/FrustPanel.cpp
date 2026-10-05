#include "FrustPanel.h"

namespace
{
juce::File sourceFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("DjehutiElectronicsLab").getChildFile("frust_panel.frust");
}

const char* starterSource =
    "// Frust, compiled in memory by the embedded compiler and run in this app.\n"
    "// run() is called; print_line adds a line of output; what run returns is shown.\n"
    "\n"
    "pub fn run() -> String = {\n"
    "    print_line(\"Frust is running inside Djehuti Electronics Lab\");\n"
    "    \"ok\"\n"
    "}\n";

void styleButton(juce::TextButton& b, bool primary)
{
    b.setColour(juce::TextButton::buttonColourId, primary ? juce::Colour(0xff2f7f73) : juce::Colour(0xff1d2731));
    b.setColour(juce::TextButton::textColourOffId, primary ? juce::Colours::white : juce::Colour(0xffdce9ee));
}
}

FrustPanel::FrustPanel()
{
    editor = std::make_unique<juce::CodeEditorComponent>(document, &tokeniser);
    editor->setFont(juce::Font("Consolas", 14.0f, juce::Font::plain));
    editor->setTabSize(4, true);
    editor->setColour(juce::CodeEditorComponent::backgroundColourId, juce::Colour(0xff10161d));
    editor->setColour(juce::CodeEditorComponent::defaultTextColourId, juce::Colour(0xffdce9ee));
    editor->setColour(juce::CodeEditorComponent::lineNumberBackgroundId, juce::Colour(0xff151a20));
    editor->setColour(juce::CodeEditorComponent::lineNumberTextId, juce::Colour(0xff71808c));
    editor->setColour(juce::CodeEditorComponent::highlightColourId, juce::Colour(0xff23394a));
    editor->setColour(juce::CaretComponent::caretColourId, juce::Colour(0xff78dcca));
    addAndMakeVisible(*editor);

    const auto saved = sourceFile().loadFileAsString();
    document.replaceAllContent(saved.trim().isNotEmpty() ? saved : juce::String(starterSource));
    document.clearUndoHistory();

    styleButton(runButton, true);
    styleButton(checkButton, false);
    runButton.setTooltip("Compile with the embedded Frust compiler and run run()");
    checkButton.setTooltip("Compile only and list any errors");
    runButton.onClick = [this] { runAsync(); };
    checkButton.onClick = [this] { show(checkNow(getSource()), false); };
    addAndMakeVisible(runButton);
    addAndMakeVisible(checkButton);

    status.setColour(juce::Label::textColourId, juce::Colour(0xff93a7b0));
    status.setFont(juce::Font(12.5f));
    status.setText("Embedded Frust compiler ready.", juce::dontSendNotification);
    addAndMakeVisible(status);

    output.setMultiLine(true);
    output.setReadOnly(true);
    output.setScrollbarsShown(true);
    output.setFont(juce::Font("Consolas", 13.5f, juce::Font::plain));
    output.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff151a20));
    output.setColour(juce::TextEditor::textColourId, juce::Colour(0xffdce9ee));
    output.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff33424d));
    addAndMakeVisible(output);
}

FrustPanel::~FrustPanel()
{
    saveSource();
    alive.reset();
    pool.removeAllJobs(true, 30000);
}

void FrustPanel::setSource(const juce::String& source)
{
    document.replaceAllContent(source);
    saveSource();
}

juce::String FrustPanel::getSource() const
{
    return document.getAllContent();
}

void FrustPanel::saveSource() const
{
    sourceFile().getParentDirectory().createDirectory();
    sourceFile().replaceWithText(document.getAllContent());
}

frust_engine::Result FrustPanel::runNow(const juce::String& script)
{
    setSource(script);
    const auto result = frust_engine::runScript(script.toStdString());
    show(result, true);
    return result;
}

frust_engine::Result FrustPanel::checkNow(const juce::String& script)
{
    const auto header = frust_engine::manifestLine("electronics_lab_script", "check", { "djehuti_frust_log" })
                      + "extern fn djehuti_frust_log(text: String) -> i64;\n"
                        "pub fn print_line(text: String) -> i64 = { djehuti_frust_log(text) }\n";
    auto result = frust_engine::check("script.frust", header + script.toStdString());
    for (auto& d : result.diagnostics)
        if (d.file == "script.frust" && d.line > 3)
            d.line -= 3;
    return result;
}

void FrustPanel::runAsync()
{
    if (running.load())
        return;
    saveSource();
    running = true;
    runButton.setEnabled(false);
    status.setText("Compiling and running...", juce::dontSendNotification);
    const auto script = getSource().toStdString();
    std::weak_ptr<bool> weak = alive;
    pool.addJob([this, script, weak] {
        auto result = frust_engine::runScript(script);
        juce::MessageManager::callAsync([this, weak, result] {
            if (weak.expired()) return;
            running = false;
            runButton.setEnabled(true);
            show(result, true);
        });
    });
}

void FrustPanel::show(const frust_engine::Result& result, bool ran)
{
    juce::String text;
    if (result.ok)
    {
        text << (ran ? juce::String(result.output) : juce::String("No errors."));
        status.setColour(juce::Label::textColourId, juce::Colour(0xff8fd16b));
        status.setText(ran ? "Ran: compile " + juce::String(result.compileMs, 1) + " ms, run " + juce::String(result.runMs, 3) + " ms"
                           : "Compiles cleanly (" + juce::String(result.compileMs, 1) + " ms).",
                       juce::dontSendNotification);
    }
    else
    {
        text << juce::String(result.report());
        status.setColour(juce::Label::textColourId, juce::Colour(0xffffb36b));
        status.setText(result.diagnostics.empty() ? "Failed." : juce::String((int)result.diagnostics.size()) + " diagnostic(s).",
                       juce::dontSendNotification);
        // Put the caret on the first error's line.
        for (const auto& d : result.diagnostics)
            if (d.line > 0)
            {
                editor->moveCaretTo(juce::CodeDocument::Position(document, d.line - 1, juce::jmax(0, d.column - 1)), false);
                break;
            }
    }
    output.setText(text, false);
}

void FrustPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff10161d));
}

void FrustPanel::resized()
{
    auto area = getLocalBounds().reduced(6);
    auto bar = area.removeFromTop(32);
    runButton.setBounds(bar.removeFromLeft(90).reduced(0, 2));
    bar.removeFromLeft(6);
    checkButton.setBounds(bar.removeFromLeft(90).reduced(0, 2));
    bar.removeFromLeft(10);
    status.setBounds(bar);
    area.removeFromTop(4);
    output.setBounds(area.removeFromBottom(juce::jlimit(80, 260, area.getHeight() / 3)));
    area.removeFromBottom(4);
    editor->setBounds(area);
}
