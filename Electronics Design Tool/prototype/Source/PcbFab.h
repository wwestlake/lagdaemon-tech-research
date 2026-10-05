#pragma once

#include "PcbLayout.h"

// Fabrication and assembly files for the board, in the formats fabs and CAM
// stations take: Gerber X2 (copper per layer, solder mask, paste, silkscreen,
// board profile), Excellon drills (plated and non-plated), a Gerber job file,
// an IPC-D-356A netlist for the fab's electrical test, pick-and-place and BOM
// CSVs, and a zip of them all. Every Gerber, drill and netlist file is read
// back after writing and compared with the board; a mismatch fails the export.
namespace pcb::fab
{
struct ExportResult
{
    bool ok = false;           // written and every read-back check passed
    bool readyForFab = false;  // ok, routed completely, no DRC violations, matches the schematic
    juce::File folder, zip;
    juce::StringArray files;        // file names written
    juce::StringArray checks;       // read-back results, one line per file
    juce::StringArray problems;     // why it failed, or why it is not ready for fab
    juce::String summary;
};

// Writes into `folder` (created, files of the same names replaced). `name`
// prefixes every file. `schematicMatches` / `schematicSummary` come from the
// board-versus-schematic check and are reported, not re-run here.
ExportResult exportFab(const Layout& layout, const BoardDesign& board, const juce::File& folder, const juce::String& name,
                       bool schematicMatches, const juce::String& schematicSummary);
}
