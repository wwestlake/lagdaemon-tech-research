#include "DslPanel.h"

#include <sstream>

#include "lang/ast.h"
#include "lang/compiler.h"
#include "lang/diagnostics.h"
#include "lang/sema.h"

DslPanel::DslPanel()
{
    setName("Code");
    headerLabel.setText("Creation Engine Language (CEL)", juce::dontSendNotification);
    headerLabel.setFont(juce::Font(24.0f).boldened());
    headerLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(headerLabel);

    sourceEditor.setMultiLine(true);
    sourceEditor.setReturnKeyStartsNewLine(true);
    sourceEditor.setText(R"(// Starter CEL patch
func gain(input: float, amount: float) -> float {
    return input * amount;
}
)");
    addAndMakeVisible(sourceEditor);

    outputEditor.setMultiLine(true);
    outputEditor.setReadOnly(true);
    outputEditor.setText("Compile to see CEL diagnostics.");
    addAndMakeVisible(outputEditor);

    compileButton.onClick = [this] { compileSource(); };
    compileButton.setTooltip("Parse and analyze the CEL source");
    addAndMakeVisible(compileButton);

    exportButton.onClick = [this]
    {
        if (lastCompileSucceeded && onSourceExportRequested)
            onSourceExportRequested(sourceEditor.getText(), makeSuggestedFileName());
    };
    exportButton.setTooltip("Export this source as a .cel file");
    addAndMakeVisible(exportButton);

    saveButton.onClick = [this]
    {
        if (lastCompileSucceeded && onSourceSaveToLibraryRequested)
            onSourceSaveToLibraryRequested(sourceEditor.getText(), makeSuggestedFileName());
    };
    saveButton.setTooltip("Save this source to your library");
    addAndMakeVisible(saveButton);

    loadButton.onClick = [this]
    {
        if (onSourceLoadRequested)
            onSourceLoadRequested();
    };
    loadButton.setTooltip("Load a saved .cel file");
    addAndMakeVisible(loadButton);

    compileSource();
}

void DslPanel::setSourceText(const juce::String& text)
{
    sourceEditor.setText(text, juce::dontSendNotification);
}

juce::String DslPanel::getSourceText() const
{
    return sourceEditor.getText();
}

void DslPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff11151c));
}

void DslPanel::resized()
{
    auto area = getLocalBounds().reduced(20);
    headerLabel.setBounds(area.removeFromTop(40));
    area.removeFromTop(10);

    auto left = area.removeFromLeft(area.getWidth() / 2 - 10);
    auto right = area;
    sourceEditor.setBounds(left.withTrimmedBottom(80));
    auto buttonRow = left.removeFromBottom(72);
    compileButton.setBounds(buttonRow.removeFromLeft(160).removeFromTop(34));
    buttonRow.removeFromLeft(10);
    exportButton.setBounds(buttonRow.removeFromLeft(160).removeFromTop(34));
    buttonRow.removeFromLeft(10);
    saveButton.setBounds(buttonRow.removeFromLeft(160).removeFromTop(34));
    buttonRow.removeFromLeft(10);
    loadButton.setBounds(buttonRow.removeFromLeft(140).removeFromTop(34));
    outputEditor.setBounds(right);
}

void DslPanel::compileSource()
{
    std::istringstream stream(sourceEditor.getText().toStdString());
    ce::lang::AstArena arena;
    ce::lang::DiagnosticEngine diagnostics;
    ce::lang::Program* program = ce::lang::ParseProgram(stream, arena, diagnostics);
    if (program != nullptr && !diagnostics.HasErrors())
        ce::lang::AnalyzeProgram(*program, diagnostics);

    lastCompileSucceeded = program != nullptr && !diagnostics.HasErrors();

    juce::String output;
    if (lastCompileSucceeded)
    {
        int functionCount = 0;
        int globalCount = 0;
        for (const auto* decl : program->decls)
        {
            if (decl->kind == ce::lang::DeclKind::Func)
                ++functionCount;
            else
                ++globalCount;
        }

        output << functionCount << " function(s), " << globalCount << " global(s) parsed and analyzed cleanly.";
    }
    else
    {
        for (const auto& diagnostic : diagnostics.Diagnostics())
            output << "Line " << diagnostic.loc.line << ": " << diagnostic.message << "\n";
    }

    outputEditor.setText(output, juce::dontSendNotification);
    exportButton.setEnabled(lastCompileSucceeded);
    saveButton.setEnabled(lastCompileSucceeded);
}

void DslPanel::loadSourceFromFile(const juce::File& sourceFile)
{
    setSourceText(sourceFile.loadFileAsString());
    compileSource();
}

juce::String DslPanel::makeSuggestedFileName() const
{
    return "patch";
}
