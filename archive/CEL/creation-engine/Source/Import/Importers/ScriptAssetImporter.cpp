#include "Import/Importers/ScriptAssetImporter.h"

#include <sstream>

#include "lang/ast.h"
#include "lang/compiler.h"
#include "lang/diagnostics.h"
#include "lang/sema.h"
#include "Scene/ScriptCatalog.h"

namespace ce::import {

ImportResult ScriptAssetImporter::Import(const juce::File& sourceFile, ImportContext& context) {
    if (context.scriptCatalog == nullptr) {
        return ImportResult::Failed("Script import needs a live script catalog.");
    }

    const juce::String assetName = sourceFile.getFileNameWithoutExtension();
    if (!context.scriptCatalog->AddFromFile(assetName, sourceFile)) {
        return ImportResult::Failed("Failed to read " + sourceFile.getFileName() + ".");
    }

    // A quick parse+sema pass purely for a useful import-log message --
    // see this importer's own header comment for why a failing script
    // still stages successfully rather than being rejected here.
    const juce::String source = sourceFile.loadFileAsString();
    std::istringstream stream(source.toStdString());
    ce::lang::AstArena arena;
    ce::lang::DiagnosticEngine diagnostics;
    ce::lang::Program* program = ce::lang::ParseProgram(stream, arena, diagnostics);
    if (program != nullptr && !diagnostics.HasErrors()) {
        ce::lang::AnalyzeProgram(*program, diagnostics);
    }

    if (diagnostics.HasErrors()) {
        return ImportResult::Ok(assetName + " staged (" + juce::String((int)diagnostics.Diagnostics().size()) +
                                 " error(s) -- open in the script editor to fix before attaching).");
    }
    return ImportResult::Ok(assetName + " staged and validated cleanly.");
}

} // namespace ce::import
