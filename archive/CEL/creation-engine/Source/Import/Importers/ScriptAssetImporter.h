#pragma once

#include "Import/AssetImporter.h"

namespace ce::import {

// Stages a `.cel` file's source text into the live ScriptCatalog
// (Source/Scene/ScriptCatalog.h), under a name derived from the file
// name -- the "asset" being imported is just text, so (like
// AudioAssetImporter) there's no GPU-resource step and this runs
// entirely on whichever thread calls Import() (the message thread, via
// the Import Hub's drag-and-drop).
//
// Deliberately does NOT compile the script (parse+sema+IR+JIT) at
// import time -- that needs a live ce::engine::IScriptRuntime and is
// ScriptPanel's job, on demand, against a real attached entity (see
// ScriptCatalog::ScriptAsset's own comment for why). It DOES run a
// quick parse+sema check purely to give the Import Hub's log a useful
// message ("N errors" vs "OK") -- a script that fails that check is
// still staged successfully; only a file that can't be read at all is a
// real Failed result.
class ScriptAssetImporter final : public AssetImporter {
public:
    juce::String DisplayName() const override { return "CEL Script"; }
    std::vector<juce::String> SupportedExtensions() const override { return { "cel" }; }
    ImportResult Import(const juce::File& sourceFile, ImportContext& context) override;
};

} // namespace ce::import
