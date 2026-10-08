#include "ElectronicsWorkbench.h"
#include "ElectronicsKnowledge.h"
#include "LocalAgentApi.h"
#include "SchematicSymbols.h"
#include "SchematicLayout.h"
#include "SchematicRouter.h"
#include "PartCatalog.h"
#include "CircuitSolver.h"
#include "Analytics.h"
#include "AnalyticsPanel.h"
#include "FrustPanel.h"
#include "PcbPanel.h"
#include <djehuti_route/outline.h>
#include "Preferences.h"
#include "AudioPipeline.h"
#include "AudioDsp.h"
#include "SpiceLibrary.h"

#include <ai_provider/AiConfig.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <memory>
#include <map>
#include <set>
#include <thread>

namespace
{
constexpr int toolbarHeight = 36;
constexpr int menuHeight = 24;

void styleTextEditor(juce::TextEditor& editor, bool mono = false)
{
    editor.setMultiLine(true);
    editor.setReturnKeyStartsNewLine(true);
    editor.setScrollbarsShown(true);
    editor.setFont(juce::Font(mono ? "Consolas" : "Segoe UI", mono ? 14.0f : 15.0f, juce::Font::plain));
    editor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff10161d));
    editor.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff33424d));
    editor.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour(0xff5aa7c8));
    editor.setColour(juce::TextEditor::textColourId, juce::Colour(0xffdce9ee));
}

void showCursorForEvent(const juce::MouseEvent& event, juce::MouseCursor cursor)
{
    auto source = event.source;
    if (source.hasMouseCursor())
        source.showMouseCursor(cursor);
}

juce::Colour dmmLeadColour(bool positive)
{
    return positive ? juce::Colour(0xffd85f5f) : juce::Colour(0xff2c333b);
}

juce::Colour scopeChannelColour(int channelIndex)
{
    static const juce::Colour colours[] = {
        juce::Colour(0xffc86a6a),
        juce::Colour(0xff6fac7d),
        juce::Colour(0xff6687c6),
        juce::Colour(0xffc4ad58)
    };

    return colours[(size_t)juce::jlimit(0, 3, channelIndex % 4)];
}

// Schematic tools handled generically by SchematicCanvasPanel::runSchematicTool.
// One table feeds both the agent's tool definitions and the tool manifest.
struct SchematicToolSpec
{
    const char* name;
    const char* description;
    const char* schema;
};

const SchematicToolSpec schematicToolSpecs[] = {
    {
        "schematic_group_create",
        "Draw a named group box around existing components. Group boxes only label a region of the diagram; wiring and the circuit are unchanged. The name is shown in the box's top-left corner.",
        R"({"type":"object","properties":{"name":{"type":"string","description":"Group name shown in the box corner, such as Output Stage."},"members":{"type":"array","items":{"type":"string"},"description":"Reference designators of the components to enclose, such as [\"Q1\", \"Q2\", \"R3\"]."},"category":{"type":"string","description":"Optional category such as amplifier_stage or bias_network."},"notes":{"type":"string","description":"Optional notes."}},"required":["name","members"],"additionalProperties":false})"
    },
    {
        "schematic_group_update",
        "Rename a group box, change its category or notes, or add/remove member components. Omitted fields stay unchanged.",
        R"({"type":"object","properties":{"group":{"type":"string","description":"Group id (G1) or current name."},"name":{"type":"string","description":"New name."},"category":{"type":"string"},"notes":{"type":"string"},"addMembers":{"type":"array","items":{"type":"string"},"description":"Reference designators to add."},"removeMembers":{"type":"array","items":{"type":"string"},"description":"Reference designators to remove."}},"required":["group"],"additionalProperties":false})"
    },
    {
        "schematic_group_delete",
        "Remove a group box. Its components and wiring stay exactly as they are.",
        R"({"type":"object","properties":{"group":{"type":"string","description":"Group id (G1) or name."}},"required":["group"],"additionalProperties":false})"
    },
    {
        "schematic_subdiagram_create",
        "Fold components into a sub-diagram block to make a complex schematic easier to read. The block shows one pin per signal net crossing its edge (ground and named supplies stay global and never become pins); double-clicking it opens its own sheet, where each pin appears as a port bubble with the pin name. Nothing about the circuit changes. Give members (reference designators on the sheet being viewed) or an existing group. Blocks can contain blocks.",
        R"({"type":"object","properties":{"name":{"type":"string","description":"Block name, such as Output Stage."},"members":{"type":"array","items":{"type":"string"},"description":"Reference designators to fold into the block."},"group":{"type":"string","description":"Alternatively, a group id or name whose members become the block (the group box is replaced by the block)."}},"required":["name"],"additionalProperties":false})"
    },
    {
        "schematic_subdiagram_expand",
        "Put a sub-diagram block's contents back on the sheet it sits on, centred where the block was, inside a group box with the block's name.",
        R"({"type":"object","properties":{"block":{"type":"string","description":"Block reference designator (A1) or name."}},"required":["block"],"additionalProperties":false})"
    },
    {
        "schematic_subdiagram_open",
        "Change which sheet is shown and edited: open a block's sheet, go up one level, or return to the main sheet. Placement, wiring, auto layout, and grouping tools act on the sheet being viewed.",
        R"({"type":"object","properties":{"target":{"type":"string","description":"Block refdes or name to open, 'up' for the parent sheet, or 'main'."}},"required":["target"],"additionalProperties":false})"
    },
    {
        "schematic_subdiagram_rename",
        "Rename a sub-diagram block.",
        R"({"type":"object","properties":{"block":{"type":"string","description":"Block refdes or current name."},"name":{"type":"string","description":"New name."}},"required":["block","name"],"additionalProperties":false})"
    },
    {
        "schematic_subdiagram_rename_port",
        "Rename one of a sub-diagram block's pins; the matching port bubble inside the block is renamed with it.",
        R"({"type":"object","properties":{"block":{"type":"string","description":"Block refdes or name."},"port":{"type":"string","description":"Current pin name, such as IN or OUT2."},"name":{"type":"string","description":"New pin name."}},"required":["block","port","name"],"additionalProperties":false})"
    },
    {
        "schematic_subdiagram_expose_parameter",
        "Expose an internal component parameter on a sub-diagram block so users and the assistant can set it from the outside block properties. This is explicit: choose the internal part and parameter key to expose.",
        R"({"type":"object","properties":{"block":{"type":"string","description":"Block refdes or name."},"parameterName":{"type":"string","description":"Outside-facing parameter name, such as voltage or internal_resistance."},"targetRefdes":{"type":"string","description":"Internal component refdes, such as V1 or R1."},"targetParameter":{"type":"string","description":"Internal parameter key, such as value, frequency, busName, or a key returned by schematic_get_parameters."}},"required":["block","parameterName","targetRefdes","targetParameter"],"additionalProperties":false})"
    },
    {
        "schematic_subdiagram_list",
        "List every sub-diagram block with its name, the sheet it sits on, its pins, and its member components, plus which sheet is being viewed.",
        R"({"type":"object","properties":{},"additionalProperties":false})"
    },
    {
        "library_save_block",
        "Save an existing sub-diagram block into the user's reusable circuit-block library. Saved blocks are copy templates: placing one later creates a new editable sub-diagram copy, not a linked/shared definition.",
        R"({"type":"object","properties":{"block":{"type":"string","description":"Sub-diagram block refdes (A1) or name to save."},"name":{"type":"string","description":"Optional library name; defaults to the block name."},"description":{"type":"string","description":"Optional notes for the user and assistant."},"category":{"type":"string","description":"Optional category such as Filters, Amplifiers, Power, or Utilities."}},"required":["block"],"additionalProperties":false})"
    },
    {
        "library_list_blocks",
        "List reusable circuit blocks saved in the user's library.",
        R"({"type":"object","properties":{"query":{"type":"string","description":"Optional text to filter by name, category, description, or port name."}},"additionalProperties":false})"
    },
    {
        "library_place_block",
        "Place a reusable circuit block from the user's library as a fresh editable sub-diagram copy on the current sheet.",
        R"({"type":"object","properties":{"name":{"type":"string","description":"Saved library block name."},"x":{"type":"number","description":"Schematic x coordinate."},"y":{"type":"number","description":"Schematic y coordinate."},"instanceName":{"type":"string","description":"Optional display name for this copy."}},"required":["name"],"additionalProperties":false})"
    },
    {
        "schematic_delete_components",
        "Delete components from the diagram, with the wires attached to them, exactly as pressing Delete on a selected part does. Sub-diagram blocks are refused: expand them first with schematic_subdiagram_expand.",
        R"({"type":"object","properties":{"refdes":{"type":"array","items":{"type":"string"},"description":"Reference designators to delete, such as [\"R5\", \"GND7\"]."}},"required":["refdes"],"additionalProperties":false})"
    },
    {
        "schematic_export_image",
        "Render a schematic sheet to a PNG file exactly as it is drawn, to check readability or share it. Defaults to the sheet being viewed.",
        R"({"type":"object","properties":{"sheet":{"type":"string","description":"Optional: block refdes or name to render its sheet, or 'main'."}},"additionalProperties":false})"
    },
    {
        "project_create",
        "Create a new named project (a folder holding named diagrams) and make it the open project. Then create its first diagram with diagram_create.",
        R"({"type":"object","properties":{"name":{"type":"string","description":"Project name; also the folder name."},"location":{"type":"string","description":"Optional parent folder; defaults to Documents\\Djehuti Electronics Lab\\Projects."}},"required":["name"],"additionalProperties":false})"
    },
    {
        "project_open",
        "Open a project by name (recent or in the default projects folder) or by folder path. Its last diagram opens; the current diagram is saved first.",
        R"({"type":"object","properties":{"project":{"type":"string","description":"Project name or folder path."}},"required":["project"],"additionalProperties":false})"
    },
    {
        "project_list",
        "List known projects (recent and in the default projects folder) and show the open project.",
        R"({"type":"object","properties":{},"additionalProperties":false})"
    },
    {
        "project_info",
        "Show the open project: name, folder, diagrams, current diagram, and whether it has unsaved changes.",
        R"({"type":"object","properties":{},"additionalProperties":false})"
    },
    {
        "project_rename",
        "Rename the open project (its folder is renamed too).",
        R"({"type":"object","properties":{"name":{"type":"string","description":"New project name."}},"required":["name"],"additionalProperties":false})"
    },
    {
        "diagram_create",
        "Create a new blank named diagram in the open project, save it, and open it. The current diagram is saved first.",
        R"({"type":"object","properties":{"name":{"type":"string","description":"Diagram name."}},"required":["name"],"additionalProperties":false})"
    },
    {
        "diagram_open",
        "Open another diagram of the open project by name. The current diagram is saved first.",
        R"({"type":"object","properties":{"name":{"type":"string","description":"Diagram name."}},"required":["name"],"additionalProperties":false})"
    },
    {
        "diagram_list",
        "List the open project's diagrams and which one is open.",
        R"({"type":"object","properties":{},"additionalProperties":false})"
    },
    {
        "diagram_save",
        "Save the open diagram (all sheets, sub-diagrams, groups, wiring) to its file in the project.",
        R"({"type":"object","properties":{},"additionalProperties":false})"
    },
    {
        "diagram_save_as",
        "Save the open diagram under a new name in the same project and switch to the new copy.",
        R"({"type":"object","properties":{"name":{"type":"string","description":"New diagram name."}},"required":["name"],"additionalProperties":false})"
    },
    {
        "diagram_rename",
        "Rename a diagram of the open project (defaults to the open diagram).",
        R"({"type":"object","properties":{"name":{"type":"string","description":"Diagram to rename; omit for the open diagram."},"newName":{"type":"string","description":"New name."}},"required":["newName"],"additionalProperties":false})"
    },
    {
        "diagram_duplicate",
        "Copy a saved diagram to a new name in the same project (defaults to the open diagram, saved first). The copy is not opened.",
        R"({"type":"object","properties":{"name":{"type":"string","description":"Diagram to copy; omit for the open diagram."},"newName":{"type":"string","description":"Name of the copy."}},"required":["newName"],"additionalProperties":false})"
    },
    {
        "diagram_delete",
        "Remove a diagram from the open project. Its file is moved to the project's deleted folder, not erased.",
        R"({"type":"object","properties":{"name":{"type":"string","description":"Diagram name."}},"required":["name"],"additionalProperties":false})"
    },
    {
        "pcb_board_get",
        "The board in the PCB tab: where its outline came from (standard board, shape and parameters, or drawn), outline corners in mm, mounting holes, cutouts, copper layers, thickness, edge clearance, size, area, and any problems (crossing edges, holes off the board).",
        R"({"type":"object","properties":{},"additionalProperties":false})"
    },
    {
        "pcb_board_list_options",
        "List the standard board sizes (id, name, description) and the parametric shapes (id, parameters with defaults) the board can be made from.",
        R"({"type":"object","properties":{},"additionalProperties":false})"
    },
    {
        "pcb_board_use_standard",
        "Make the board a standard size by id (from pcb_board_list_options), with its mounting holes: eurocard, half-eurocard, double-eurocard, rpi-hat, arduino-uno-shield, credit-card, fab-100, fab-50.",
        R"({"type":"object","properties":{"id":{"type":"string"}},"required":["id"],"additionalProperties":false})"
    },
    {
        "pcb_board_set_shape",
        "Make the board a parametric shape: rectangle, rounded_rectangle, chamfered_rectangle, l_shape, u_shape, t_shape, circle, polygon (regular: sides and across_flats; a hexagon is sides 6). Parameters in mm, missing ones take their defaults. Holes, cutouts and stackup are kept.",
        R"({"type":"object","properties":{"shape":{"type":"string"},"params":{"type":"object","description":"Parameter name to millimetres, such as {\"width\": 80, \"height\": 60, \"notch_width\": 30, \"notch_height\": 20}.","additionalProperties":{"type":"number"}}},"required":["shape"],"additionalProperties":false})"
    },
    {
        "pcb_board_set_outline",
        "Set any straight-edged board outline from its corners in order (mm, y up), for shapes the presets do not cover. The outline must not cross itself; holes, cutouts and stackup are kept.",
        R"({"type":"object","properties":{"points":{"type":"array","items":{"type":"array","items":{"type":"number"},"minItems":2,"maxItems":2},"description":"Corners [[x, y], ...] in mm."}},"required":["points"],"additionalProperties":false})"
    },
    {
        "pcb_board_add_hole",
        "Add a round mounting hole (non-plated) through the board at x, y (mm) with a diameter (mm, e.g. 3.2 for M3).",
        R"({"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"diameter":{"type":"number"}},"required":["x","y","diameter"],"additionalProperties":false})"
    },
    {
        "pcb_board_add_cutout",
        "Cut a straight-edged opening through the board (slot, window): its corners in order, in mm. It must be inside the board without touching the edge.",
        R"({"type":"object","properties":{"points":{"type":"array","items":{"type":"array","items":{"type":"number"},"minItems":2,"maxItems":2}}},"required":["points"],"additionalProperties":false})"
    },
    {
        "pcb_board_remove",
        "Remove a mounting hole or a cutout by its number (1-based, as pcb_board_get lists them).",
        R"({"type":"object","properties":{"kind":{"type":"string","description":"hole or cutout"},"index":{"type":"integer"}},"required":["kind","index"],"additionalProperties":false})"
    },
    {
        "pcb_board_set_stackup",
        "Set the copper layer count (1, 2, 4, 6, 8), board thickness (mm) and copper-to-edge clearance (mm). Omitted fields stay as they are.",
        R"({"type":"object","properties":{"layers":{"type":"integer"},"thickness":{"type":"number"},"edge_clearance":{"type":"number"}},"additionalProperties":false})"
    },
    {
        "pcb_layout_get",
        "The parts on the board in the PCB tab: each part's refdes, value, footprint, position (mm, y up), rotation and every pad with its position and net; the route rules; the routing result (connections routed of total, track length, vias, unrouted pads with reasons, design-rule violations) and placement problems.",
        R"({"type":"object","properties":{},"additionalProperties":false})"
    },
    {
        "pcb_footprints_list",
        "List the footprints (id, description, pads with positions, sizes and drills, courtyard). With symbol_id, also the footprints that fit that schematic symbol, the default first.",
        R"({"type":"object","properties":{"symbol_id":{"type":"string","description":"Schematic symbol id such as resistor, npn, opamp_741."}},"additionalProperties":false})"
    },
    {
        "pcb_sync_from_schematic",
        "Put the open diagram's parts on the board as footprints with the schematic's nets on their pads. New parts are placed in free space (the first time, the whole board is laid out); parts already there keep their place; removed parts come off; changed nets apply. Instruments and ideal controlled sources are left off (listed as skipped). Clears the routing.",
        R"({"type":"object","properties":{},"additionalProperties":false})"
    },
    {
        "pcb_auto_place",
        "Place every part again automatically: most-connected first, each where its pads land nearest the pads already placed on the same nets, inside the outline and clear of holes, cutouts, the board edge and other parts. Parts that do not fit are set beside the board and listed. Clears the routing.",
        R"({"type":"object","properties":{},"additionalProperties":false})"
    },
    {
        "pcb_place_part",
        "Move a part on the board: its footprint origin to x, y (mm, y up) and optionally its rotation (0, 90, 180, 270 degrees counter-clockwise). Clears the routing; returns placement problems.",
        R"({"type":"object","properties":{"refdes":{"type":"string"},"x":{"type":"number"},"y":{"type":"number"},"rotation":{"type":"integer"}},"required":["refdes","x","y"],"additionalProperties":false})"
    },
    {
        "pcb_set_footprint",
        "Change a part's footprint to one that fits its symbol (see pcb_footprints_list with symbol_id), e.g. a resistor to 1206 or Axial-P10.16. Clears the routing.",
        R"({"type":"object","properties":{"refdes":{"type":"string"},"footprint":{"type":"string"}},"required":["refdes","footprint"],"additionalProperties":false})"
    },
    {
        "pcb_set_route_rules",
        "Set the routing rules in mm: track width, copper clearance, via diameter and via drill. Omitted fields stay. Clears the routing.",
        R"({"type":"object","properties":{"track_width":{"type":"number"},"clearance":{"type":"number"},"via_diameter":{"type":"number"},"via_drill":{"type":"number"}},"additionalProperties":false})"
    },
    {
        "pcb_route",
        "Route every net on the board with the DjehutiRoute autorouter (A* on a grid at track width + clearance, negotiated congestion, vias between layers) using the current rules, or the rules given here, then check the copper with the exact design-rule check. Returns connections routed of total, track length, vias, unrouted pads and violations. The board must be valid and parts placed without problems.",
        R"({"type":"object","properties":{"track_width":{"type":"number"},"clearance":{"type":"number"},"via_diameter":{"type":"number"},"via_drill":{"type":"number"}},"additionalProperties":false})"
    },
    {
        "pcb_verify_netlist",
        "Check that the board is electrically identical to the schematic as it is now: connectivity is extracted from the copper geometry alone (pads, tracks and vias that touch are one node, whatever net the router assigned), each pad is mapped to its part and pin by the footprint's pin map, and every pin is compared with the schematic's nets. Reports opens (a schematic net split on the board), shorts (copper joining different nets or a not-connected pad), parts missing or extra, changed symbols and pins without a pad. Runs automatically after every route.",
        R"({"type":"object","properties":{},"additionalProperties":false})"
    },
    {
        "pcb_text_add",
        "Add text to the board, printed on the silkscreen or etched in copper, drawn with the board's vector font exactly as it goes into the Gerber files. Position (mm, y up) is the anchor; height is the cap height (fabs usually need 0.8 mm or more, line width 0.15 mm or more); rotation in degrees counter-clockwise; align left, centre or right; layer F.SilkS (default), B.SilkS, F.Cu or B.Cu (bottom layers are mirrored so they read from underneath). Copper text is kept clear by the router and clears the routing. Returns its number.",
        R"({"type":"object","properties":{"text":{"type":"string","description":"Text; \\n starts a new line."},"x":{"type":"number"},"y":{"type":"number"},"height":{"type":"number"},"line_width":{"type":"number"},"rotation":{"type":"number"},"align":{"type":"string","description":"left, centre or right"},"layer":{"type":"string"}},"required":["text","x","y"],"additionalProperties":false})"
    },
    {
        "pcb_text_edit",
        "Change a board text by its number (1-based, as pcb_layout_get lists texts): any of text, x, y, height, line_width, rotation, align, layer.",
        R"({"type":"object","properties":{"index":{"type":"integer"},"text":{"type":"string"},"x":{"type":"number"},"y":{"type":"number"},"height":{"type":"number"},"line_width":{"type":"number"},"rotation":{"type":"number"},"align":{"type":"string"},"layer":{"type":"string"}},"required":["index"],"additionalProperties":false})"
    },
    {
        "pcb_text_remove",
        "Remove a board text by its number (1-based).",
        R"({"type":"object","properties":{"index":{"type":"integer"}},"required":["index"],"additionalProperties":false})"
    },
    {
        "pcb_graphic_add",
        "Add a simple graphic to the board on F.SilkS (default), B.SilkS, F.Cu or B.Cu: line (points [start, end]), rect (points [corner, opposite corner]), circle (points [centre], radius), arc (points [centre], radius, start_angle to end_angle in degrees counter-clockwise), polygon (points, 3 or more corners). Outlined with line_width (default 0.15 mm) or filled. Copper graphics are kept clear by the router and clear the routing. Returns its number.",
        R"({"type":"object","properties":{"kind":{"type":"string"},"points":{"type":"array","items":{"type":"array","items":{"type":"number"},"minItems":2,"maxItems":2}},"radius":{"type":"number"},"start_angle":{"type":"number"},"end_angle":{"type":"number"},"line_width":{"type":"number"},"filled":{"type":"boolean"},"layer":{"type":"string"}},"required":["kind","points"],"additionalProperties":false})"
    },
    {
        "pcb_graphic_edit",
        "Change a board graphic by its number (1-based, as pcb_layout_get lists graphics): any of points, radius, start_angle, end_angle, line_width, filled, layer.",
        R"({"type":"object","properties":{"index":{"type":"integer"},"points":{"type":"array","items":{"type":"array","items":{"type":"number"},"minItems":2,"maxItems":2}},"radius":{"type":"number"},"start_angle":{"type":"number"},"end_angle":{"type":"number"},"line_width":{"type":"number"},"filled":{"type":"boolean"},"layer":{"type":"string"}},"required":["index"],"additionalProperties":false})"
    },
    {
        "pcb_graphic_remove",
        "Remove a board graphic by its number (1-based).",
        R"({"type":"object","properties":{"index":{"type":"integer"}},"required":["index"],"additionalProperties":false})"
    },
    {
        "pcb_set_fab_rules",
        "Set how the fabrication layers are made, in mm: solder mask expansion beyond pads, paste reduction inside SMD pads, silkscreen line width, part label height; tent_vias (vias covered by mask) and part_labels (reference designators on the silkscreen). Omitted fields stay.",
        R"({"type":"object","properties":{"mask_expansion":{"type":"number"},"paste_reduction":{"type":"number"},"silk_line_width":{"type":"number"},"label_height":{"type":"number"},"tent_vias":{"type":"boolean"},"part_labels":{"type":"boolean"}},"additionalProperties":false})"
    },
    {
        "pcb_export_fab",
        "Write the fabrication and assembly files to the diagram's outputs/fab folder and zip them: Gerber X2 copper per layer, solder mask, paste, silkscreen (top and bottom) and board profile; Excellon drills (plated and non-plated); Gerber job file; IPC-D-356A netlist for the fab's electrical test; pick-and-place and BOM CSVs. Every Gerber, drill and netlist file is read back and compared with the board. Reports ok (files correct), readyForFab (also fully routed, no DRC violations, matches the schematic), files, zip path, per-file checks and problems.",
        R"({"type":"object","properties":{},"additionalProperties":false})"
    },
    {
        "pcb_clear_routes",
        "Remove all tracks and vias from the board (parts stay).",
        R"({"type":"object","properties":{},"additionalProperties":false})"
    },
    {
        "pcb_drc",
        "Re-check the board as it is now with the exact design-rule check: track, via and pad clearances, shorts, copper to board edge and cutouts, open connections, and placement problems (parts off the board, overlapping, over holes).",
        R"({"type":"object","properties":{},"additionalProperties":false})"
    },
    {
        "frust_run",
        "Compile Frust source with the app's embedded Frust compiler (in memory, LLVM JIT) and run it in the app. The source must define `pub fn run() -> String`; print_line(text: String) -> i64 adds output lines. Returns the output, or compile diagnostics with line numbers. The code is shown in the Frust panel.",
        R"({"type":"object","properties":{"source":{"type":"string","description":"Frust source defining pub fn run() -> String."}},"required":["source"],"additionalProperties":false})"
    },
    {
        "frust_check",
        "Compile Frust source with the embedded compiler without running it and return any diagnostics (file, line, column, message). Same source rules as frust_run.",
        R"({"type":"object","properties":{"source":{"type":"string","description":"Frust source defining pub fn run() -> String."}},"required":["source"],"additionalProperties":false})"
    },
    {
        "instrument_read",
        "Read an instrument node exactly as its window shows it: oscilloscope channel Vpp/Vrms/mean/frequency at its time/div and trigger, multimeter reading in its function (DC V, AC V, DC A, Ohms), or frequency analyzer peak gain and -3 dB points. Change instrument settings with schematic_set_parameters.",
        R"({"type":"object","properties":{"refdes":{"type":"string","description":"Instrument reference designator, such as SCOPE1, DMM1, or FRA1."}},"required":["refdes"],"additionalProperties":false})"
    },
    {
        "preferences_list",
        "List the user's preferences (the Preferences window), optionally filtered by a search word: key, category, label, current value, choices and what each does. Categories: Layout, Display, Units, Projects.",
        R"({"type":"object","properties":{"query":{"type":"string","description":"Optional search text, such as rail, spacing, grid, label."}},"additionalProperties":false})"
    },
    {
        "preferences_set",
        "Change one of the user's preferences by key, exactly like the Preferences window. Toggles take true/false; choices must match an option from preferences_list. Only change preferences the user asked for.",
        R"({"type":"object","properties":{"key":{"type":"string","description":"Preference key, such as layout.supply_symbols or display.grid."},"value":{"type":"string"}},"required":["key","value"],"additionalProperties":false})"
    },
    {
        "schematic_convert_supply",
        "Switch how a supply or ground is drawn, like the right-click menu: a power/ground rail becomes a supply port or ground symbol at every pin it feeds (to: symbols), or the supply ports of one net / the ground symbols on a sheet become one rail with a tap to each pin (to: rail). The circuit is unchanged.",
        R"({"type":"object","properties":{"refdes":{"type":"string","description":"A rail (PBUS1, GBUS1) or one of the symbols (PWR3, GND2)."},"to":{"type":"string","description":"rail or symbols."}},"required":["refdes","to"],"additionalProperties":false})"
    },
    {
        "schematic_get_parameters",
        "List a part's editable properties (key, label, current value, unit, choices), its rotation and its sheet - the same fields the Properties pane shows.",
        R"({"type":"object","properties":{"refdes":{"type":"string","description":"Reference designator such as R1, V1, Q2, SCOPE1."}},"required":["refdes"],"additionalProperties":false})"
    },
    {
        "schematic_set_parameters",
        "Set one or more of a part's properties by key, validated like the Properties pane: values take units and prefixes (4.7k, 10u, 1meg), choices must match an option. Use schematic_get_parameters to see the keys. Covers component values, source waveform/amplitude/frequency/offset, transistor and diode models, switch state, net and label names, and instrument settings (time_per_div, ch1_volts_per_div, trigger_level, meter function in value, start_frequency).",
        R"({"type":"object","properties":{"refdes":{"type":"string"},"params":{"type":"object","description":"Property key to new value, such as {\"value\": \"4.7k\"} or {\"waveform\": \"Square\", \"frequency\": \"500\"}.","additionalProperties":{"type":"string"}}},"required":["refdes","params"],"additionalProperties":false})"
    },
    {
        "simulation_parameter_expose",
        "Expose a schematic property as a canonical simulation parameter. The definition is saved with the diagram and each backend reports whether it can consume it as Live, Prepared, Rerun, Rebuild, or Unsupported.",
        R"({"type":"object","properties":{"id":{"type":"string","description":"Stable parameter id, such as tone_cutoff or drive_gain. Letters, digits, _ and - only."},"refdes":{"type":"string","description":"Reference designator of the component that owns the property."},"property":{"type":"string","description":"Property key returned by schematic_get_parameters, such as value, position, amplitude, frequency, beta."},"label":{"type":"string","description":"User-facing label."},"min":{"type":"string","description":"Optional minimum value with units/prefixes."},"max":{"type":"string","description":"Optional maximum value with units/prefixes."},"scaling":{"type":"string","description":"linear, log, discrete, or toggle."},"control":{"type":"string","description":"Preferred control such as knob, slider, switch, dropdown, or text."}},"required":["id","refdes","property"],"additionalProperties":false})"
    },
    {
        "simulation_parameter_list",
        "List canonical simulation parameters for the open schematic and their backend capability in a context: live_audio, spice, vst, or all.",
        R"({"type":"object","properties":{"context":{"type":"string","description":"live_audio, spice, vst, or all. Defaults to all."}},"additionalProperties":false})"
    },
    {
        "simulation_parameter_set",
        "Set a canonical simulation parameter by id. This changes the owning schematic property; backends consume the changed value according to their capability, e.g. live coefficient publication or SPICE rerun.",
        R"({"type":"object","properties":{"id":{"type":"string"},"value":{"type":"string"}},"required":["id","value"],"additionalProperties":false})"
    },
    {
        "schematic_rename_component",
        "Change a part's reference designator (letters, digits, underscore; must be unique).",
        R"({"type":"object","properties":{"refdes":{"type":"string"},"newRefdes":{"type":"string"}},"required":["refdes","newRefdes"],"additionalProperties":false})"
    },
    {
        "schematic_rotate_component",
        "Set a part's rotation to 0, 90, 180 or 270 degrees.",
        R"({"type":"object","properties":{"refdes":{"type":"string"},"rotation":{"type":"string","description":"0, 90, 180 or 270."}},"required":["refdes","rotation"],"additionalProperties":false})"
    },
    {
        "schematic_group_list",
        "List the group boxes on the schematic with their ids, names, categories, notes, and member reference designators.",
        R"({"type":"object","properties":{},"additionalProperties":false})"
    },
};

// Analytics tools: one per analysis, with every setting from the same field
// table the Analytics window builds its form from.
struct ToolSpecText
{
    juce::String name, description, schema;
};

const std::vector<ToolSpecText>& analyticsToolSpecs()
{
    static const auto specs = [] {
        std::vector<ToolSpecText> list;
        auto stringProperty = [](const juce::String& description) {
            auto* p = new juce::DynamicObject();
            p->setProperty("type", "string");
            p->setProperty("description", description);
            return juce::var(p);
        };
        auto schemaOf = [](juce::DynamicObject* properties, const juce::StringArray& required = {}) {
            auto* schema = new juce::DynamicObject();
            schema->setProperty("type", "object");
            schema->setProperty("properties", juce::var(properties));
            if (!required.isEmpty())
            {
                juce::Array<juce::var> names;
                for (const auto& r : required) names.add(r);
                schema->setProperty("required", names);
            }
            schema->setProperty("additionalProperties", false);
            return juce::JSON::toString(juce::var(schema), true);
        };
        for (const auto& a : analytics::analyses())
        {
            auto* props = new juce::DynamicObject();
            for (const auto& f : analytics::fieldsFor(a.id))
            {
                juce::String d = f.label + (f.unit.isNotEmpty() ? " (" + f.unit + ")" : juce::String()) + ".";
                if (f.help.isNotEmpty()) d << " " << f.help;
                switch (f.kind)
                {
                    case analytics::FieldKind::Net: d << " A net name from analytics_list, or GND."; break;
                    case analytics::FieldKind::Nets: d << " Net names and I(part) currents, comma separated."; break;
                    case analytics::FieldKind::Source: d << " Reference designator of an independent source, such as V1."; break;
                    case analytics::FieldKind::Target: d << " part.parameter (R1.value, V1.dc, Q1.beta, C2.value) or TEMP; None for no step."; break;
                    default: break;
                }
                if (!f.options.isEmpty()) d << " One of: " << f.options.joinIntoString(", ") << ".";
                if (f.defaultValue.isNotEmpty()) d << " Default " << f.defaultValue << ".";
                props->setProperty(juce::Identifier(f.key), stringProperty(d));
            }
            list.push_back({ "analytics_" + a.key,
                             a.title + ": " + a.description + " Runs on the open diagram exactly as it is (never changes a part), "
                             "shows the result in the Analytics window, and returns the summary, tables, trace summaries and CSV files. Every setting is optional.",
                             schemaOf(props) });
        }
        list.push_back({ "analytics_list",
                         "List what can be analysed on the open diagram: every analysis with its settings (key, label, default, choices), the nets, the independent sources, and the part parameters that can be swept or stepped.",
                         schemaOf(new juce::DynamicObject()) });
        {
            auto* props = new juce::DynamicObject();
            juce::StringArray kinds;
            for (auto k : signal_measure::allKinds()) kinds.add(signal_measure::kindName(k));
            props->setProperty("trace", stringProperty("Trace name from the latest result, such as V(out) or I(R1). For phase or gain margin use the magnitude trace."));
            props->setProperty("measurement", stringProperty("One of: " + kinds.joinIntoString(", ") + "."));
            props->setProperty("from", stringProperty("Optional window start on the x axis (time, frequency or swept value)."));
            props->setProperty("to", stringProperty("Optional window end."));
            props->setProperty("at", stringProperty("For Value at: the x position."));
            props->setProperty("level", stringProperty("For When crosses: the level."));
            props->setProperty("nth", stringProperty("For When crosses: which crossing, default 1."));
            props->setProperty("edge", stringProperty("For When crosses: Rising, Falling or Either."));
            props->setProperty("low_percent", stringProperty("Rise/fall lower threshold, default 10."));
            props->setProperty("high_percent", stringProperty("Rise/fall upper threshold, default 90."));
            props->setProperty("band_percent", stringProperty("Settling band, default 2."));
            list.push_back({ "analytics_measure",
                             "Measure a trace of the latest Analytics result, like SPICE .MEAS: min, max, peak-to-peak, average, RMS, integral, value at, crossing time, rise/fall time, overshoot, settling time, frequency, period, -3 dB bandwidth and corners, unity-gain frequency, phase margin, gain margin. The measurement is also listed in the Analytics window.",
                             schemaOf(props, { "trace", "measurement" }) });
        }
        {
            auto* props = new juce::DynamicObject();
            props->setProperty("index", stringProperty("0 = latest (default), 1 = the one before, and so on."));
            list.push_back({ "analytics_result", "Return an Analytics result from this session's history again, with its settings, summary, tables and traces.", schemaOf(props) });
        }
        {
            auto* props = new juce::DynamicObject();
            props->setProperty("analysis", stringProperty("Analysis key: operating_point, dc_sweep, ac, transient, fourier, noise, transfer_function, sensitivity, pole_zero, temperature, monte_carlo."));
            auto* settings = new juce::DynamicObject();
            settings->setProperty("type", "object");
            settings->setProperty("description", "Optional settings to fill in, same keys as the analytics_<analysis> tool.");
            settings->setProperty("additionalProperties", juce::var(new juce::DynamicObject()));
            props->setProperty("settings", juce::var(settings));
            list.push_back({ "analytics_open", "Show the Analytics window with an analysis selected and, optionally, its settings filled in, without running it.", schemaOf(props, { "analysis" }) });
        }
        return list;
    }();
    return specs;
}

bool isSchematicTool(const juce::String& name)
{
    for (const auto& spec : schematicToolSpecs)
        if (name == spec.name)
            return true;
    for (const auto& spec : analyticsToolSpecs())
        if (name == spec.name)
            return true;
    return false;
}

juce::String jsonQuote(const juce::String& text)
{
    return "\"" + text.replace("\\", "\\\\").replace("\"", "\\\"") + "\"";
}

juce::String htmlEscape(const juce::String& text)
{
    return text.replace("&", "&amp;")
               .replace("<", "&lt;")
               .replace(">", "&gt;")
               .replace("\"", "&quot;");
}

struct LiveCoeffs
{
    std::vector<double> buffers[3];
    std::atomic<int> published{0};
    std::atomic<int> reading{0};
    
    LiveCoeffs(size_t size) {
        for (int i = 0; i < 3; ++i)
            buffers[i].resize(std::max<size_t>(1, size), 0.0);
    }
    
    const double* acquireRead() {
        int fresh = published.load(std::memory_order_acquire);
        reading.store(fresh, std::memory_order_relaxed);
        return buffers[fresh].data();
    }
    
    void publish(const std::vector<double>& newCoeffs) {
        int r = reading.load(std::memory_order_acquire);
        int p = published.load(std::memory_order_relaxed);
        int next = 0;
        while (next == r || next == p) next++;
        buffers[next] = newCoeffs;
        published.store(next, std::memory_order_release);
    }
};

class LiveParameterThread : public juce::Thread
{
public:
    LiveParameterThread(std::shared_ptr<audio_dsp::Model> model, std::shared_ptr<LiveCoeffs> coeffs)
        : juce::Thread("LiveParameterThread"), dspModel(std::move(model)), liveCoeffs(std::move(coeffs))
    {
        startThread();
    }

    ~LiveParameterThread() override
    {
        signalThreadShouldExit();
        notify();
        stopThread(2000);
    }

    void update(const std::unordered_map<std::string, double>& newParams)
    {
        const juce::ScopedLock lock(mutex);
        params = newParams;
        dirty = true;
        notify();
    }

    void run() override
    {
        while (!threadShouldExit())
        {
            std::unordered_map<std::string, double> currentParams;
            {
                const juce::ScopedLock lock(mutex);
                if (!dirty) {
                    wait(500);
                    continue;
                }
                currentParams = params;
                dirty = false;
            }

            auto newCoeffs = dspModel->computeLiveCoefficients(currentParams);
            if (!newCoeffs.empty()) {
                liveCoeffs->publish(newCoeffs);
            }
        }
    }

private:
    std::shared_ptr<audio_dsp::Model> dspModel;
    std::shared_ptr<LiveCoeffs> liveCoeffs;
    juce::CriticalSection mutex;
    std::unordered_map<std::string, double> params;
    bool dirty = false;
};

class LiveParameterPoller : public juce::Timer
{
public:
    LiveParameterPoller(std::shared_ptr<audio_dsp::Model> m, std::shared_ptr<LiveCoeffs> c, std::function<std::unordered_map<std::string, double>()> g)
        : dspModel(std::move(m)), liveCoeffs(std::move(c)), getLiveParams(std::move(g))
    {
        startTimer(50);
        thread = std::make_unique<LiveParameterThread>(dspModel, liveCoeffs);
    }
    
    void timerCallback() override
    {
        if (!getLiveParams) return;
        auto newParams = getLiveParams();
        if (newParams != lastParams) {
            lastParams = newParams;
            thread->update(newParams);
        }
    }
    
private:
    std::shared_ptr<audio_dsp::Model> dspModel;
    std::shared_ptr<LiveCoeffs> liveCoeffs;
    std::unique_ptr<LiveParameterThread> thread;
    std::function<std::unordered_map<std::string, double>()> getLiveParams;
    std::unordered_map<std::string, double> lastParams;
};

juce::String htmlDecode(juce::String text)
{
    return text.replace("&amp;", "&")
               .replace("&quot;", "\"")
               .replace("&#39;", "'")
               .replace("&#x27;", "'")
               .replace("&lt;", "<")
               .replace("&gt;", ">")
               .replace("&nbsp;", " ");
}

juce::String stripHtmlTags(const juce::String& html)
{
    juce::String result;
    bool insideTag = false;
    for (int i = 0; i < html.length(); ++i)
    {
        const auto ch = html[i];
        if (ch == '<')
        {
            insideTag = true;
            continue;
        }
        if (ch == '>')
        {
            insideTag = false;
            continue;
        }
        if (!insideTag)
            result << ch;
    }
    return htmlDecode(result).trim();
}

juce::String decodeDuckDuckGoRedirect(juce::String url)
{
    url = htmlDecode(url.trim());
    if (url.startsWith("//"))
        url = "https:" + url;

    const auto marker = juce::String("uddg=");
    const auto index = url.indexOf(marker);
    if (index >= 0)
    {
        const auto encoded = url.substring(index + marker.length())
            .upToFirstOccurrenceOf("&", false, false);
        return juce::URL::removeEscapeChars(encoded);
    }

    return url;
}

void collectDuckDuckGoHtmlResults(const juce::String& html, int limit, juce::StringArray& rows)
{
    int cursor = 0;
    while (rows.size() < limit)
    {
        const auto anchorStart = html.indexOf(cursor, "class=\"result__a\"");
        if (anchorStart < 0)
            break;

        const auto hrefStart = html.indexOf(anchorStart, "href=\"");
        const auto hrefEnd = hrefStart >= 0 ? html.indexOf(hrefStart + 6, "\"") : -1;
        const auto titleStart = hrefEnd >= 0 ? html.indexOf(hrefEnd, ">") : -1;
        const auto titleEnd = titleStart >= 0 ? html.indexOf(titleStart, "</a>") : -1;
        if (hrefStart < 0 || hrefEnd < 0 || titleStart < 0 || titleEnd < 0)
            break;

        const auto url = decodeDuckDuckGoRedirect(html.substring(hrefStart + 6, hrefEnd));
        const auto title = stripHtmlTags(html.substring(titleStart + 1, titleEnd));
        cursor = titleEnd + 4;

        juce::String snippet;
        const auto snippetClass = html.indexOf(cursor, "class=\"result__snippet\"");
        const auto nextAnchor = html.indexOf(cursor, "class=\"result__a\"");
        if (snippetClass >= 0 && (nextAnchor < 0 || snippetClass < nextAnchor))
        {
            const auto snippetStart = html.indexOf(snippetClass, ">");
            const auto snippetEnd = snippetStart >= 0 ? html.indexOf(snippetStart, "</a>") : -1;
            if (snippetStart >= 0 && snippetEnd >= 0)
                snippet = stripHtmlTags(html.substring(snippetStart + 1, snippetEnd));
        }

        if (title.isNotEmpty() && url.startsWithIgnoreCase("http"))
        {
            juce::String row;
            row << "{ \"title\": " << jsonQuote(title)
                << ", \"snippet\": " << jsonQuote(snippet)
                << ", \"url\": " << jsonQuote(url) << " }";
            rows.add(row);
        }
    }
}

juce::String inlineMarkdownToHtml(juce::String text)
{
    text = htmlEscape(text);

    juce::String out;
    bool inCode = false;
    bool inMath = false;
    for (int i = 0; i < text.length(); ++i)
    {
        const auto c = text[i];
        if (c == '`')
        {
            out << (inCode ? "</code>" : "<code>");
            inCode = !inCode;
        }
        else if (c == '$')
        {
            out << (inMath ? "\\)" : "\\(");
            inMath = !inMath;
        }
        else
        {
            out << juce::String::charToString(c);
        }
    }
    if (inCode) out << "</code>";
    if (inMath) out << "\\)";
    return out;
}

juce::String markdownToHtmlDocument(const juce::String& title, const juce::String& markdown)
{
    const auto lines = juce::StringArray::fromLines(markdown);
    juce::String body;
    bool inList = false;
    bool inCode = false;
    bool inMathBlock = false;

    auto closeList = [&] {
        if (inList)
        {
            body << "</ul>\n";
            inList = false;
        }
    };

    for (const auto& rawLine : lines)
    {
        const auto trimmed = rawLine.trim();

        if (trimmed.startsWith("```"))
        {
            closeList();
            body << (inCode ? "</code></pre>\n" : "<pre><code>");
            inCode = !inCode;
            continue;
        }
        if (inCode)
        {
            body << htmlEscape(rawLine) << "\n";
            continue;
        }

        if (trimmed == "$$")
        {
            closeList();
            body << (inMathBlock ? "\\]</div>\n" : "<div class=\"math-block\">\\[");
            inMathBlock = !inMathBlock;
            continue;
        }
        if (inMathBlock)
        {
            body << htmlEscape(rawLine) << "\n";
            continue;
        }

        if (trimmed.isEmpty())
        {
            closeList();
            continue;
        }

        if (trimmed.startsWith("# "))
        {
            closeList();
            body << "<h1>" << inlineMarkdownToHtml(trimmed.substring(2)) << "</h1>\n";
        }
        else if (trimmed.startsWith("## "))
        {
            closeList();
            body << "<h2>" << inlineMarkdownToHtml(trimmed.substring(3)) << "</h2>\n";
        }
        else if (trimmed.startsWith("### "))
        {
            closeList();
            body << "<h3>" << inlineMarkdownToHtml(trimmed.substring(4)) << "</h3>\n";
        }
        else if (trimmed.startsWith("- ") || trimmed.startsWith("* "))
        {
            if (!inList)
            {
                body << "<ul>\n";
                inList = true;
            }
            body << "<li>" << inlineMarkdownToHtml(trimmed.substring(2)) << "</li>\n";
        }
        else
        {
            closeList();
            body << "<p>" << inlineMarkdownToHtml(trimmed) << "</p>\n";
        }
    }

    closeList();
    if (inCode) body << "</code></pre>\n";
    if (inMathBlock) body << "\\]</div>\n";

    juce::String html;
    html << "<!doctype html><html><head><meta charset=\"utf-8\"><meta http-equiv=\"X-UA-Compatible\" content=\"IE=edge\">"
         << "<title>" << htmlEscape(title) << "</title>"
         << "<link rel=\"stylesheet\" href=\"https://cdn.jsdelivr.net/npm/katex@0.11.1/dist/katex.min.css\">"
         << "<script src=\"https://cdn.jsdelivr.net/npm/katex@0.11.1/dist/katex.min.js\"></script>"
         << "<script src=\"https://cdn.jsdelivr.net/npm/katex@0.11.1/dist/contrib/auto-render.min.js\"></script>"
         << "<script>window.onload=function(){if(window.renderMathInElement){renderMathInElement(document.body,{delimiters:[{left:'\\\\[',right:'\\\\]',display:true},{left:'\\\\(',right:'\\\\)',display:false}]});}}</script>"
         << "<style>"
         << "body{margin:0;padding:24px;background:#10161d;color:#dce9ee;font:15px/1.55 Segoe UI,Arial,sans-serif;}"
         << "h1,h2,h3{color:#78dcca;margin:0 0 12px;}h1{font-size:24px;}h2{font-size:19px;margin-top:24px;}h3{font-size:16px;margin-top:18px;}"
         << "p,ul{max-width:920px;}code{background:#1d2a33;border:1px solid #33424d;border-radius:4px;padding:1px 4px;}"
         << "pre{background:#0b1117;border:1px solid #33424d;border-radius:6px;padding:12px;overflow:auto;}"
         << ".math-block{margin:16px 0;padding:12px;background:#0b1117;border-left:3px solid #78dcca;overflow:auto;}"
         << "a{color:#8fd8ff;}</style></head><body>" << body << "</body></html>";
    return html;
}

struct RlcHighPassDesign
{
    double cutoffHz = 10.0;
    double impedanceOhms = 8.0;
    double sourceOhms = 8.0;
    double loadOhms = 8.0;
    double capacitanceFarads = 0.0;
    double inductanceHenries = 0.0;
};

struct RlcAcSample
{
    double frequencyHz = 0.0;
    double rawGainDb = 0.0;
    double normalizedGainDb = 0.0;
    double phaseDegrees = 0.0;
    double inputImpedanceReal = 0.0;
    double inputImpedanceImag = 0.0;
    double inputImpedanceMag = 0.0;
};

RlcHighPassDesign makeRlcHighPassDesign(double cutoffHz, double impedanceOhms)
{
    RlcHighPassDesign design;
    design.cutoffHz = std::max(0.001, cutoffHz);
    design.impedanceOhms = std::max(0.001, impedanceOhms);
    design.sourceOhms = design.impedanceOhms;
    design.loadOhms = design.impedanceOhms;

    const auto omega = juce::MathConstants<double>::twoPi * design.cutoffHz;
    const auto butterworthQ = std::sqrt(2.0);
    design.capacitanceFarads = 1.0 / (omega * design.impedanceOhms * butterworthQ);
    design.inductanceHenries = design.impedanceOhms / (omega * butterworthQ);
    return design;
}

juce::String numberText(double value, int decimals = 6)
{
    return juce::String(value, decimals).trimCharactersAtEnd("0").trimCharactersAtEnd(".");
}

juce::String spiceCapacitance(double farads)
{
    return numberText(farads * 1000.0, 6) + "m";
}

juce::String spiceInductance(double henries)
{
    return numberText(henries * 1000.0, 6) + "m";
}

juce::String humanCapacitance(double farads)
{
    return numberText(farads * 1000000.0, 2) + " uF";
}

juce::String humanInductance(double henries)
{
    return numberText(henries * 1000.0, 3) + " mH";
}

double dbFromMagnitude(double magnitude)
{
    return 20.0 * std::log10(std::max(1.0e-12, magnitude));
}

std::vector<RlcAcSample> sweepRlcHighPass(const RlcHighPassDesign& design)
{
    std::vector<RlcAcSample> samples;
    constexpr int sampleCount = 481;
    const auto startHz = std::max(0.01, design.cutoffHz / 100.0);
    const auto stopHz = design.cutoffHz * 100.0;
    const auto logStart = std::log10(startHz);
    const auto logStop = std::log10(stopHz);
    const std::complex<double> j(0.0, 1.0);
    const auto passbandDivider = design.loadOhms / (design.sourceOhms + design.loadOhms);

    samples.reserve(sampleCount);
    for (int index = 0; index < sampleCount; ++index)
    {
        const auto t = (double)index / (double)(sampleCount - 1);
        const auto frequency = std::pow(10.0, logStart + (logStop - logStart) * t);
        const auto omega = juce::MathConstants<double>::twoPi * frequency;
        const auto zc = 1.0 / (j * omega * design.capacitanceFarads);
        const auto zl = j * omega * design.inductanceHenries;
        const auto zLoad = std::complex<double>(design.loadOhms, 0.0);
        const auto zParallel = 1.0 / (1.0 / zLoad + 1.0 / zl);
        const auto inputImpedance = zc + zParallel;
        const auto transfer = zParallel / (std::complex<double>(design.sourceOhms, 0.0) + inputImpedance);
        const auto normalized = transfer / passbandDivider;

        RlcAcSample sample;
        sample.frequencyHz = frequency;
        sample.rawGainDb = dbFromMagnitude(std::abs(transfer));
        sample.normalizedGainDb = dbFromMagnitude(std::abs(normalized));
        sample.phaseDegrees = std::atan2(normalized.imag(), normalized.real()) * 180.0 / juce::MathConstants<double>::pi;
        sample.inputImpedanceReal = inputImpedance.real();
        sample.inputImpedanceImag = inputImpedance.imag();
        sample.inputImpedanceMag = std::abs(inputImpedance);
        samples.push_back(sample);
    }
    return samples;
}

RlcAcSample sampleRlcHighPassAt(const RlcHighPassDesign& design, double frequency)
{
    const std::complex<double> j(0.0, 1.0);
    const auto omega = juce::MathConstants<double>::twoPi * frequency;
    const auto zc = 1.0 / (j * omega * design.capacitanceFarads);
    const auto zl = j * omega * design.inductanceHenries;
    const auto zLoad = std::complex<double>(design.loadOhms, 0.0);
    const auto zParallel = 1.0 / (1.0 / zLoad + 1.0 / zl);
    const auto inputImpedance = zc + zParallel;
    const auto passbandDivider = design.loadOhms / (design.sourceOhms + design.loadOhms);
    const auto transfer = zParallel / (std::complex<double>(design.sourceOhms, 0.0) + inputImpedance);
    const auto normalized = transfer / passbandDivider;

    RlcAcSample sample;
    sample.frequencyHz = frequency;
    sample.rawGainDb = dbFromMagnitude(std::abs(transfer));
    sample.normalizedGainDb = dbFromMagnitude(std::abs(normalized));
    sample.phaseDegrees = std::atan2(normalized.imag(), normalized.real()) * 180.0 / juce::MathConstants<double>::pi;
    sample.inputImpedanceReal = inputImpedance.real();
    sample.inputImpedanceImag = inputImpedance.imag();
    sample.inputImpedanceMag = std::abs(inputImpedance);
    return sample;
}

juce::String buildRlcHighPassSpiceNetlist(const RlcHighPassDesign& design)
{
    juce::String netlist;
    netlist << "* Djehuti Electronics Lab generated AC analysis netlist\n";
    netlist << "* 2nd order passive RLC high-pass, Butterworth alignment\n";
    netlist << "* Cutoff: " << numberText(design.cutoffHz, 4) << " Hz, impedance: "
            << numberText(design.impedanceOhms, 4) << " ohm\n\n";
    netlist << "VIN vin 0 AC 1\n";
    netlist << "RS vin in " << numberText(design.sourceOhms, 6) << "\n";
    netlist << "C1 in out " << spiceCapacitance(design.capacitanceFarads) << "\n";
    netlist << "L1 out 0 " << spiceInductance(design.inductanceHenries) << "\n";
    netlist << "RL out 0 " << numberText(design.loadOhms, 6) << "\n\n";
    netlist << ".AC DEC 80 " << numberText(std::max(0.01, design.cutoffHz / 100.0), 6)
            << " " << numberText(design.cutoffHz * 100.0, 6) << "\n";
    netlist << ".PRINT AC VM(out) VP(out) VM(in)\n";
    netlist << ".END\n";
    return netlist;
}

juce::String buildRlcHighPassCsv(const std::vector<RlcAcSample>& samples)
{
    juce::String csv;
    csv << "frequency_hz,normalized_gain_db,raw_gain_db,phase_deg,input_impedance_mag_ohm,input_impedance_real_ohm,input_impedance_imag_ohm\n";
    for (const auto& sample : samples)
    {
        csv << numberText(sample.frequencyHz, 8) << ","
            << numberText(sample.normalizedGainDb, 8) << ","
            << numberText(sample.rawGainDb, 8) << ","
            << numberText(sample.phaseDegrees, 8) << ","
            << numberText(sample.inputImpedanceMag, 8) << ","
            << numberText(sample.inputImpedanceReal, 8) << ","
            << numberText(sample.inputImpedanceImag, 8) << "\n";
    }
    return csv;
}

juce::String buildRlcHighPassSvg(const RlcHighPassDesign& design,
                                 const std::vector<RlcAcSample>& samples)
{
    constexpr double width = 1100.0;
    constexpr double height = 640.0;
    constexpr double left = 82.0;
    constexpr double right = 36.0;
    constexpr double top = 44.0;
    constexpr double bottom = 76.0;
    constexpr double minDb = -60.0;
    constexpr double maxDb = 3.0;
    const auto plotWidth = width - left - right;
    const auto plotHeight = height - top - bottom;
    const auto startHz = std::max(0.01, design.cutoffHz / 100.0);
    const auto stopHz = design.cutoffHz * 100.0;
    const auto logStart = std::log10(startHz);
    const auto logStop = std::log10(stopHz);

    auto xFor = [&](double frequency) {
        return left + (std::log10(std::clamp(frequency, startHz, stopHz)) - logStart) / (logStop - logStart) * plotWidth;
    };
    auto yFor = [&](double db) {
        const auto clamped = std::clamp(db, minDb, maxDb);
        return top + (maxDb - clamped) / (maxDb - minDb) * plotHeight;
    };

    juce::String points;
    for (const auto& sample : samples)
        points << numberText(xFor(sample.frequencyHz), 2) << "," << numberText(yFor(sample.normalizedGainDb), 2) << " ";

    juce::String svg;
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << (int)width
        << "\" height=\"" << (int)height << "\" viewBox=\"0 0 " << (int)width << " " << (int)height << "\">\n";
    svg << "<rect width=\"100%\" height=\"100%\" fill=\"#0e141a\"/>\n";
    svg << "<text x=\"32\" y=\"28\" fill=\"#e8f1f2\" font-family=\"Segoe UI, Arial\" font-size=\"20\" font-weight=\"700\">RLC 2nd Order High-Pass Response</text>\n";
    svg << "<text x=\"32\" y=\"54\" fill=\"#93a7b0\" font-family=\"Segoe UI, Arial\" font-size=\"13\">"
        << numberText(design.cutoffHz, 3) << " Hz cutoff, " << numberText(design.impedanceOhms, 3)
        << " ohm matched source/load, C1 " << humanCapacitance(design.capacitanceFarads)
        << ", L1 " << humanInductance(design.inductanceHenries) << "</text>\n";
    svg << "<rect x=\"" << numberText(left, 1) << "\" y=\"" << numberText(top, 1)
        << "\" width=\"" << numberText(plotWidth, 1) << "\" height=\"" << numberText(plotHeight, 1)
        << "\" fill=\"#111922\" stroke=\"#33424d\" stroke-width=\"1\"/>\n";

    for (double db : { 0.0, -3.0, -10.0, -20.0, -40.0, -60.0 })
    {
        const auto y = yFor(db);
        svg << "<line x1=\"" << numberText(left, 1) << "\" y1=\"" << numberText(y, 1)
            << "\" x2=\"" << numberText(left + plotWidth, 1) << "\" y2=\"" << numberText(y, 1)
            << "\" stroke=\"" << (db == -3.0 ? "#ffc857" : "#26323d") << "\" stroke-width=\"" << (db == -3.0 ? "1.4" : "1") << "\"/>\n";
        svg << "<text x=\"18\" y=\"" << numberText(y + 4.0, 1) << "\" fill=\"#93a7b0\" font-family=\"Segoe UI, Arial\" font-size=\"12\">"
            << numberText(db, 0) << " dB</text>\n";
    }

    for (double frequency : { startHz, design.cutoffHz / 10.0, design.cutoffHz, design.cutoffHz * 10.0, stopHz })
    {
        if (frequency < startHz * 0.999 || frequency > stopHz * 1.001)
            continue;
        const auto x = xFor(frequency);
        svg << "<line x1=\"" << numberText(x, 1) << "\" y1=\"" << numberText(top, 1)
            << "\" x2=\"" << numberText(x, 1) << "\" y2=\"" << numberText(top + plotHeight, 1)
            << "\" stroke=\"" << (std::abs(frequency - design.cutoffHz) < 0.001 ? "#78dcca" : "#26323d")
            << "\" stroke-width=\"" << (std::abs(frequency - design.cutoffHz) < 0.001 ? "1.5" : "1") << "\"/>\n";
        svg << "<text x=\"" << numberText(x - 22.0, 1) << "\" y=\"" << numberText(top + plotHeight + 24.0, 1)
            << "\" fill=\"#93a7b0\" font-family=\"Segoe UI, Arial\" font-size=\"12\">"
            << numberText(frequency, frequency < 1.0 ? 2 : 0) << " Hz</text>\n";
    }

    svg << "<polyline fill=\"none\" stroke=\"#78dcca\" stroke-width=\"3\" points=\"" << points.trim() << "\"/>\n";
    svg << "<circle cx=\"" << numberText(xFor(design.cutoffHz), 2) << "\" cy=\"" << numberText(yFor(-3.01029995664), 2)
        << "\" r=\"5\" fill=\"#ffc857\"/>\n";
    svg << "<text x=\"" << numberText(left + plotWidth - 314.0, 1) << "\" y=\"" << numberText(top + 28.0, 1)
        << "\" fill=\"#dce9ee\" font-family=\"Segoe UI, Arial\" font-size=\"13\">Normalized to matched passband, 0 dB at high frequency</text>\n";
    svg << "<text x=\"" << numberText(left + plotWidth - 314.0, 1) << "\" y=\"" << numberText(top + 48.0, 1)
        << "\" fill=\"#ffc857\" font-family=\"Segoe UI, Arial\" font-size=\"13\">Cutoff marker: -3.01 dB at "
        << numberText(design.cutoffHz, 3) << " Hz</text>\n";
    svg << "<text x=\"" << numberText(left + plotWidth * 0.45, 1) << "\" y=\"" << numberText(height - 22.0, 1)
        << "\" fill=\"#dce9ee\" font-family=\"Segoe UI, Arial\" font-size=\"14\">Frequency (log scale)</text>\n";
    svg << "<text transform=\"translate(20 " << numberText(top + plotHeight * 0.63, 1)
        << ") rotate(-90)\" fill=\"#dce9ee\" font-family=\"Segoe UI, Arial\" font-size=\"14\">Gain (dB)</text>\n";
    svg << "</svg>\n";
    return svg;
}

class NotesPanel : public juce::Component
{
public:
    NotesPanel(const juce::String& heading, const juce::String& body)
    {
        title.setText(heading, juce::dontSendNotification);
        title.setFont(juce::Font(16.0f, juce::Font::bold));
        title.setColour(juce::Label::textColourId, juce::Colour(0xff78dcca));
        addAndMakeVisible(title);

        styleTextEditor(text);
        text.setText(body);
        addAndMakeVisible(text);
    }

    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff151a20)); }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8);
        title.setBounds(area.removeFromTop(24));
        area.removeFromTop(6);
        text.setBounds(area);
    }

private:
    juce::Label title;
    juce::TextEditor text;
};

class ComponentLibraryPanel final : public juce::Component
{
public:
    struct SymbolInfo
    {
        juce::String id;
        juce::String name;
        juce::String category;
    };

    explicit ComponentLibraryPanel(std::function<void(juce::String)> onSelection)
        : onSymbolSelected(std::move(onSelection))
    {
        filter.setTextToShowWhenEmpty("Search components, MPNs, aliases...", juce::Colour(0xff71808c));
        styleTextEditor(filter);
        filter.setMultiLine(false);
        filter.onTextChange = [this] { content.rebuild(); };
        addAndMakeVisible(filter);

        viewport.setViewedComponent(&content, false);
        viewport.setScrollBarsShown(true, false);
        viewport.setScrollBarThickness(10);
        addAndMakeVisible(viewport);

        if (!loadSeedLibrary())
            addFallbackLibrary();
        // Placeable net markers that the seed list does not carry.
        if (std::none_of(allSymbols.begin(), allSymbols.end(), [](const SymbolInfo& s) { return s.id == "power_port"; }))
            add({ "power_port", "Supply Port (+V / -V)", "Power & Ground" });
        if (std::none_of(allSymbols.begin(), allSymbols.end(), [](const SymbolInfo& s) { return s.id == "net_label"; }))
            add({ "net_label", "Net Label", "Power & Ground" });
        if (std::none_of(allSymbols.begin(), allSymbols.end(), [](const SymbolInfo& s) { return s.id == "bode_analyzer"; }))
            add({ "bode_analyzer", "Frequency Analyzer (Bode)", "Instrument" });
        content.rebuild();

        if (onSymbolSelected != nullptr)
            onSymbolSelected("resistor");
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff151a20));
        g.setColour(juce::Colour(0xffdce9ee));
        g.setFont(juce::Font(14.0f, juce::Font::bold));
        g.drawText("Component Library", getLocalBounds().removeFromTop(24).reduced(8, 0), juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8);
        area.removeFromTop(24);
        filter.setBounds(area.removeFromTop(28));
        area.removeFromTop(8);
        viewport.setBounds(area);
        content.layoutToWidth(area.getWidth() - viewport.getScrollBarThickness());
    }

private:
    // Display groups, in order. Seed categories map onto these; "Discrete"
    // splits into diodes and transistors.
    static juce::String groupFor(const SymbolInfo& symbol)
    {
        const auto& id = symbol.id;
        const auto& c = symbol.category;
        if (c == "Passive") return "Passives";
        if (c == "Magnetics") return "Magnetics";
        if (id.contains("diode") || id == "led") return "Diodes";
        if (c == "Discrete") return "Transistors";
        if (c == "Source") return "Sources";
        if (c == "Controlled Source") return "Controlled Sources";
        if (c == "Bus" || c == "Reference" || c == "Power & Ground") return "Power & Ground";
        if (c == "Analog IC") return "Analog ICs";
        if (c == "Digital") return "Digital Logic";
        if (c == "Switch") return "Switches & Relays";
        if (c == "Protection") return "Protection";
        if (c == "Connector") return "Connectors & Test Points";
        if (c == "Instrument") return "Instruments";
        return c.isNotEmpty() ? c : juce::String("Other");
    }

    static const juce::StringArray& groupOrder()
    {
        static const juce::StringArray order { "Passives", "Magnetics", "Diodes", "Transistors", "Sources",
                                               "Controlled Sources", "Power & Ground", "Analog ICs", "Digital Logic",
                                               "Switches & Relays", "Protection", "Connectors & Test Points", "Instruments" };
        return order;
    }

    static juce::Colour swatchFor(const juce::String& group)
    {
        if (group == "Sources" || group == "Controlled Sources") return juce::Colour(0xfff4d35e);
        if (group == "Power & Ground") return juce::Colour(0xff78dcca);
        if (group == "Analog ICs" || group == "Digital Logic") return juce::Colour(0xffffc857);
        if (group == "Instruments") return juce::Colour(0xffff6b6b);
        return juce::Colour(0xff5aa7c8);
    }

    // The scrolling list: group headers (click to collapse/expand) and rows.
    class ListContent final : public juce::Component
    {
    public:
        explicit ListContent(ComponentLibraryPanel& ownerPanel) : owner(ownerPanel) {}

        struct Entry
        {
            bool header = false;
            juce::String group;
            int symbol = -1; // index into allSymbols
            int count = 0;   // header: rows in the group
            juce::Rectangle<int> bounds;
        };

        void rebuild()
        {
            const auto needle = owner.filter.getText().trim().toLowerCase();
            std::map<juce::String, std::vector<int>> byGroup;
            for (int i = 0; i < (int)owner.allSymbols.size(); ++i)
            {
                const auto& symbol = owner.allSymbols[(size_t)i];
                const auto haystack = (symbol.name + " " + symbol.id + " " + symbol.category + " " + groupFor(symbol)).toLowerCase();
                if (needle.isEmpty() || haystack.contains(needle))
                    byGroup[groupFor(symbol)].push_back(i);
            }

            juce::StringArray groups = groupOrder();
            for (const auto& [group, rows] : byGroup)
                groups.addIfNotAlreadyThere(group);

            entries.clear();
            for (const auto& group : groups)
            {
                const auto found = byGroup.find(group);
                if (found == byGroup.end())
                    continue;
                entries.push_back({ true, group, -1, (int)found->second.size(), {} });
                // Searching shows every match; otherwise collapsed groups hide their rows.
                if (needle.isEmpty() && collapsed.contains(group))
                    continue;
                for (int index : found->second)
                    entries.push_back({ false, group, index, 0, {} });
            }
            layoutToWidth(getWidth() > 0 ? getWidth() : 200);
        }

        void layoutToWidth(int width)
        {
            int y = 2;
            for (auto& entry : entries)
            {
                const auto height = entry.header ? 26 : 36;
                entry.bounds = { 2, y, std::max(40, width - 4), height };
                y += height + (entry.header ? 2 : 1);
            }
            setSize(std::max(40, width), y + 4);
            repaint();
        }

        void paint(juce::Graphics& g) override
        {
            g.fillAll(juce::Colour(0xff10161d));
            for (int i = 0; i < (int)entries.size(); ++i)
            {
                const auto& entry = entries[(size_t)i];
                auto area = entry.bounds;
                if (entry.header)
                {
                    g.setColour(juce::Colour(0xff1d2731));
                    g.fillRoundedRectangle(area.toFloat(), 4.0f);
                    const auto open = owner.filter.getText().trim().isNotEmpty() || !collapsed.contains(entry.group);
                    juce::Path arrow;
                    const auto c = juce::Point<float>((float)area.getX() + 12.0f, (float)area.getCentreY());
                    if (open)
                        arrow.addTriangle(c.x - 4.0f, c.y - 2.0f, c.x + 4.0f, c.y - 2.0f, c.x, c.y + 3.0f);
                    else
                        arrow.addTriangle(c.x - 2.0f, c.y - 4.0f, c.x - 2.0f, c.y + 4.0f, c.x + 3.0f, c.y);
                    g.setColour(juce::Colour(0xff93a7b0));
                    g.fillPath(arrow);
                    g.setColour(juce::Colour(0xffdce9ee));
                    g.setFont(juce::Font(13.0f, juce::Font::bold));
                    g.drawText(entry.group, area.withTrimmedLeft(24), juce::Justification::centredLeft, true);
                    g.setColour(juce::Colour(0xff71808c));
                    g.setFont(juce::Font(12.0f));
                    g.drawText(juce::String(entry.count), area.withTrimmedRight(8), juce::Justification::centredRight);
                    continue;
                }

                const auto& symbol = owner.allSymbols[(size_t)entry.symbol];
                if (symbol.id == owner.selectedId)
                {
                    g.setColour(juce::Colour(0xff23394a));
                    g.fillRoundedRectangle(area.toFloat(), 4.0f);
                }
                else if (i == hover)
                {
                    g.setColour(juce::Colour(0xff202b35));
                    g.fillRoundedRectangle(area.toFloat(), 4.0f);
                }
                const auto swatch = juce::Rectangle<int>(10, 10).withCentre({ area.getX() + 22, area.getCentreY() });
                g.setColour(swatchFor(entry.group));
                g.fillRoundedRectangle(swatch.toFloat(), 3.0f);
                g.setColour(juce::Colour(0xffdce9ee));
                g.setFont(juce::Font(13.5f, juce::Font::bold));
                g.drawText(symbol.name, area.withTrimmedLeft(34).withTrimmedBottom(16), juce::Justification::centredLeft, true);
                g.setColour(juce::Colour(0xff93a7b0));
                g.setFont(juce::Font(11.5f));
                g.drawText(symbol.id, area.withTrimmedLeft(34).withTrimmedTop(18), juce::Justification::centredLeft, true);
            }
        }

        int entryAt(juce::Point<int> p) const
        {
            for (int i = 0; i < (int)entries.size(); ++i)
                if (entries[(size_t)i].bounds.contains(p))
                    return i;
            return -1;
        }

        void mouseMove(const juce::MouseEvent& event) override
        {
            const auto i = entryAt(event.getPosition());
            if (i != hover) { hover = i; repaint(); }
        }

        void mouseExit(const juce::MouseEvent&) override
        {
            hover = -1;
            repaint();
        }

        void mouseDown(const juce::MouseEvent& event) override
        {
            const auto i = entryAt(event.getPosition());
            if (i < 0)
                return;
            const auto entry = entries[(size_t)i];
            if (entry.header)
            {
                if (owner.filter.getText().trim().isNotEmpty())
                    return;
                if (collapsed.contains(entry.group)) collapsed.removeString(entry.group);
                else collapsed.add(entry.group);
                rebuild();
                return;
            }

            const auto& symbol = owner.allSymbols[(size_t)entry.symbol];
            owner.selectedId = symbol.id;
            if (owner.onSymbolSelected != nullptr)
                owner.onSymbolSelected(symbol.id);
            repaint();
            if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this))
            {
                showCursorForEvent(event, juce::MouseCursor::DraggingHandCursor);
                container->startDragging("symbol:" + symbol.id, this);
            }
        }

    private:
        ComponentLibraryPanel& owner;
        std::vector<Entry> entries;
        juce::StringArray collapsed;
        int hover = -1;
    };

    juce::TextEditor filter;
    juce::Viewport viewport;
    ListContent content { *this };
    std::vector<SymbolInfo> allSymbols;
    std::function<void(juce::String)> onSymbolSelected;
    juce::String selectedId { "resistor" };

    void add(SymbolInfo item) { allSymbols.push_back(std::move(item)); }

    bool loadSeedLibrary()
    {
        const auto seedFile = juce::File(ELECTRONICS_RESEARCH_ROOT)
            .getChildFile("prototype")
            .getChildFile("data")
            .getChildFile("component_seed.json");
        if (!seedFile.existsAsFile())
            return false;

        const auto parsed = juce::JSON::parse(seedFile);
        const auto* root = parsed.getDynamicObject();
        if (root == nullptr || !root->hasProperty("librarySymbols"))
            return false;

        const auto* symbols = root->getProperty("librarySymbols").getArray();
        if (symbols == nullptr)
            return false;

        for (const auto& entry : *symbols)
        {
            const auto* object = entry.getDynamicObject();
            if (object == nullptr)
                continue;

            const auto id = object->getProperty("id").toString();
            const auto name = object->getProperty("name").toString();
            const auto category = object->getProperty("category").toString();
            if (id.isNotEmpty() && name.isNotEmpty())
                add({ id, name, category });
        }
        return !allSymbols.empty();
    }

    void addFallbackLibrary()
    {
        add({ "resistor", "Resistor", "Passive" });
        add({ "potentiometer", "Potentiometer", "Passive" });
        add({ "capacitor", "Capacitor", "Passive" });
        add({ "capacitor_polarized", "Polarized Capacitor", "Passive" });
        add({ "variable_capacitor", "Variable Capacitor", "Passive" });
        add({ "inductor", "Inductor", "Passive" });
        add({ "coupled_inductor", "Coupled Inductor", "Magnetics" });
        add({ "transformer", "Transformer", "Magnetics" });
        add({ "diode", "Diode", "Discrete" });
        add({ "zener_diode", "Zener Diode", "Discrete" });
        add({ "led", "LED", "Discrete" });
        add({ "schottky_diode", "Schottky Diode", "Discrete" });
        add({ "power_bus", "Power Bus", "Bus" });
        add({ "ground_bus", "Ground Bus", "Bus" });
        add({ "battery", "Battery", "Source" });
        add({ "voltage_source", "DC Voltage Source", "Source" });
        add({ "audio_in", "Audio Input", "Source" });
        add({ "audio_out", "Audio Output", "Source" });
        add({ "ac_voltage_source", "AC Voltage Source", "Source" });
        add({ "current_source", "DC Current Source", "Source" });
        add({ "ac_current_source", "AC Current Source", "Source" });
        add({ "behavioral_voltage_source", "Behavioral Voltage Source", "Controlled Source" });
        add({ "behavioral_current_source", "Behavioral Current Source", "Controlled Source" });
        add({ "vcvs", "Voltage-Controlled Voltage Source", "Controlled Source" });
        add({ "vccs", "Voltage-Controlled Current Source", "Controlled Source" });
        add({ "ccvs", "Current-Controlled Voltage Source", "Controlled Source" });
        add({ "cccs", "Current-Controlled Current Source", "Controlled Source" });
        add({ "signal_source", "Signal Source", "Source" });
        add({ "ground", "Ground", "Reference" });
        add({ "opamp_generic", "Generic Op Amp", "Analog IC" });
        add({ "opamp_741", "741 Op Amp", "Analog IC" });
        add({ "comparator_generic", "Generic Comparator", "Analog IC" });
        add({ "comparator_lm311", "LM311 Comparator", "Analog IC" });
        add({ "npn", "NPN Transistor - generic", "Discrete" });
        add({ "pnp", "PNP Transistor - generic", "Discrete" });
        add({ "nmos", "N-Channel MOSFET - generic", "Discrete" });
        add({ "pmos", "P-Channel MOSFET - generic", "Discrete" });
        add({ "njfet", "N-Channel JFET - generic", "Discrete" });
        add({ "pjfet", "P-Channel JFET - generic", "Discrete" });
        add({ "switch_spst", "SPST Switch", "Switch" });
        add({ "switch_spdt", "SPDT Switch", "Switch" });
        add({ "relay_spst", "SPST Relay", "Switch" });
        add({ "fuse", "Fuse", "Protection" });
        add({ "connector_2", "2-Pin Connector", "Connector" });
        add({ "connector_3", "3-Pin Connector", "Connector" });
        add({ "test_point", "Test Point", "Connector" });
        add({ "logic_not", "Logic Inverter - behavioral", "Digital" });
        add({ "logic_and", "AND Gate - behavioral", "Digital" });
        add({ "logic_or", "OR Gate - behavioral", "Digital" });
        add({ "logic_nand", "NAND Gate - behavioral", "Digital" });
        add({ "logic_nor", "NOR Gate - behavioral", "Digital" });
        add({ "logic_xor", "XOR Gate - behavioral", "Digital" });
        add({ "oscilloscope_2ch", "2-Channel Oscilloscope", "Instrument" });
        add({ "digital_multimeter", "Digital Multimeter", "Instrument" });
        add({ "bode_analyzer", "Frequency Analyzer (Bode)", "Instrument" });
        add({ "annotation_text", "Text Note", "Annotation" });
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ComponentLibraryPanel)
};

class SchematicCanvasPanel final : public juce::Component,
                                   public juce::DragAndDropTarget
{
public:
    SchematicCanvasPanel(std::function<juce::String()> getSelectedSymbol,
                         std::function<bool()> getStampMode,
                         std::function<void(juce::String)> onMessage)
        : getSelectedSymbolId(std::move(getSelectedSymbol)),
          getStampPlacementEnabled(std::move(getStampMode)),
          onStatus(std::move(onMessage))
    {
        setWantsKeyboardFocus(true);
        snapEnabled = prefs::isOn("display.snap_default");
        preferenceListener = prefs::addListener([safe = juce::Component::SafePointer<SchematicCanvasPanel>(this)](const juce::String&) {
            if (safe == nullptr) return;
            safe->routeSignature.clear();
            safe->repaint();
        });
    }

    ~SchematicCanvasPanel() override
    {
        prefs::removeListener(preferenceListener);
    }

    static schematic::routing::Style routingStyle()
    {
        schematic::routing::Style style;
        style.segmentPenalty = prefs::get("layout.wire_style") == "Shortest wires" ? 12.0 : 85.0;
        style.wireGapGrids = prefs::get("layout.wire_gap") == "2 grid steps" ? 2.25f : 1.25f;
        return style;
    }

    static schematic::layout::Options layoutOptions()
    {
        schematic::layout::Options options;
        options.stackVerticalChains = prefs::isOn("layout.stack_vertical_chains");
        options.supplyBlock = prefs::isOn("layout.supply_block");
        options.instrumentLabels = prefs::isOn("layout.instrument_labels");
        const auto spacing = prefs::get("layout.spacing");
        options.spacing = spacing == "Compact" ? 0.85f : spacing == "Roomy" ? 1.75f : 1.2f;
        return options;
    }

    static juce::String ohmText() { return prefs::get("units.ohm_symbol"); }

    void setSelectionListener(std::function<void(int, juce::String, juce::String, juce::String, juce::String, juce::String, juce::String, juce::String)> listener)
    {
        onSelectionChanged = std::move(listener);
        notifySelection();
    }

    void setProbeListener(std::function<void(juce::String, juce::String, juce::String)> listener)
    {
        onProbeChanged = std::move(listener);
    }

    void setInstrumentOpenListener(std::function<void(juce::String, juce::String)> listener)
    {
        onInstrumentOpen = std::move(listener);
    }

    void setSnapEnabled(bool enabled)
    {
        snapEnabled = enabled;
        repaint();
    }

    bool isSnapEnabled() const
    {
        return snapEnabled;
    }

    void setCanvasZoom(float newZoom)
    {
        canvasZoom = std::clamp(newZoom, 0.5f, 2.5f);
        repaint();
    }

    float getCanvasZoom() const
    {
        return canvasZoom;
    }

    void rotateSelected()
    {
        if (selectedInstance < 0 || selectedInstance >= (int)instances.size())
            return;

        auto& instance = instances[(size_t)selectedInstance];
        instance.rotation = schematic::normalizedRotation(instance.rotation + 90);
        if (onStatus) onStatus("Rotated " + instance.refdes + " to " + juce::String(instance.rotation) + " degrees.");
        notifySelection();
        repaint();
    }

    void clearCircuit()
    {
        clearModel();
        if (onStatus) onStatus("New electronics research project initialized.");
    }

    bool loadCircuitJson(const juce::String& json, juce::String& error)
    {
        const auto parsed = juce::JSON::parse(json);
        const auto* root = parsed.getDynamicObject();
        if (root == nullptr)
        {
            error = "Project file is not valid JSON.";
            return false;
        }

        const auto* componentArray = root->getProperty("components").getArray();
        if (componentArray == nullptr)
        {
            error = "Project file has no components array.";
            return false;
        }

        std::vector<Instance> loadedInstances;
        std::vector<juce::Point<float>> loadedJunctions;
        std::vector<juce::String> loadedJunctionSheets;
        std::vector<Wire> loadedWires;
        std::vector<Probe> loadedProbes;
        std::vector<Group> loadedGroups;
        std::vector<SimulationParameter> loadedSimulationParameters;

        for (const auto& entry : *componentArray)
        {
            const auto* object = entry.getDynamicObject();
            if (object == nullptr)
                continue;

            Instance instance;
            instance.symbolId = stringProperty(*object, "symbol", {});
            if (!schematic::isSupportedSymbol(instance.symbolId))
            {
                error = "Project uses unsupported symbol '" + instance.symbolId + "'; no substitute was loaded.";
                return false;
            }
            instance.refdes = stringProperty(*object, "id", "U" + juce::String((int)loadedInstances.size() + 1));
            instance.value = stringProperty(*object, "value", defaultValueFor(instance.symbolId));
            instance.frequency = stringProperty(*object, "frequency", defaultFrequencyFor(instance.symbolId));
            instance.busName = stringProperty(*object, "busName", defaultBusNameFor(instance.symbolId));
            instance.position = { floatProperty(*object, "x", 120.0f), floatProperty(*object, "y", 120.0f) };
            instance.rotation = schematic::normalizedRotation((int)floatProperty(*object, "rotation", 0.0f));
            instance.busLength = floatProperty(*object, "length", isRailBus(instance.symbolId) ? 420.0f : 0.0f);
            instance.sheet = stringProperty(*object, "sheet", {});
            if (const auto* params = object->getProperty("params").getDynamicObject())
                for (const auto& property : params->getProperties())
                    instance.params[property.name.toString()] = property.value.toString();
            instance.childSheet = stringProperty(*object, "childSheet", {});
            if (const auto* portArray = object->getProperty("ports").getArray())
                for (const auto& port : *portArray)
                    instance.ports.push_back({ port.getProperty("name", {}).toString(),
                                               port.getProperty("side", {}).toString() == "right" });

            if (const auto* component = object->getProperty("component").getDynamicObject())
            {
                instance.family = stringProperty(*component, "family", familyFor(instance.symbolId));
                instance.manufacturerPart = stringProperty(*component, "manufacturerPart", {});
            }
            else
            {
                instance.family = familyFor(instance.symbolId);
            }

            loadedInstances.push_back(std::move(instance));
        }

        if (const auto* junctionArray = root->getProperty("junctions").getArray())
        {
            for (const auto& entry : *junctionArray)
            {
                const auto* object = entry.getDynamicObject();
                if (object == nullptr)
                    continue;
                loadedJunctions.push_back({ floatProperty(*object, "x", 0.0f),
                                            floatProperty(*object, "y", 0.0f) });
                loadedJunctionSheets.push_back(stringProperty(*object, "sheet", {}));
            }
        }

        auto nodeForLabel = [&](const juce::String& label, WireNode& result) -> bool {
            if (label.startsWith("N") && label.substring(1).containsOnly("0123456789"))
            {
                const auto index = label.substring(1).getIntValue() - 1;
                if (index >= 0 && index < (int)loadedJunctions.size())
                {
                    result = WireNode::forJunction(index);
                    return true;
                }
                return false;
            }

            const auto dot = label.lastIndexOfChar('.');
            if (dot <= 0 || dot >= label.length() - 1)
                return false;

            const auto refdes = label.substring(0, dot);
            const auto pinName = label.substring(dot + 1);
            for (int i = 0; i < (int)loadedInstances.size(); ++i)
            {
                if (loadedInstances[(size_t)i].refdes != refdes)
                    continue;

                const auto symbol = symbolForInstance(loadedInstances[(size_t)i]);
                for (int p = 0; p < (int)symbol.pins.size(); ++p)
                {
                    if (symbol.pins[(size_t)p].name == pinName)
                    {
                        result = WireNode::forPin({ i, p });
                        return true;
                    }
                }
                return false;
            }
            return false;
        };

        if (const auto* wireArray = root->getProperty("wires").getArray())
        {
            for (const auto& entry : *wireArray)
            {
                const auto* object = entry.getDynamicObject();
                if (object == nullptr)
                    continue;

                Wire wire;
                const auto a = stringProperty(*object, "a", {});
                const auto b = stringProperty(*object, "b", {});
                if (!nodeForLabel(a, wire.a) || !nodeForLabel(b, wire.b))
                {
                    error = "Project wire references an unknown node: " + a + " -> " + b;
                    return false;
                }
                loadedWires.push_back(wire);
            }
        }

        if (const auto* probeArray = root->getProperty("probes").getArray())
        {
            for (const auto& entry : *probeArray)
            {
                const auto* object = entry.getDynamicObject();
                if (object == nullptr)
                    continue;

                const auto id = stringProperty(*object, "id", {});
                const auto label = probeLabel(id);
                if (id.isEmpty() || label.isEmpty())
                    continue;

                WireNode target;
                const auto targetLabel = stringProperty(*object, "target", {});
                if (!nodeForLabel(targetLabel, target))
                {
                    error = "Project probe references an unknown node: " + targetLabel;
                    return false;
                }

                loadedProbes.push_back({ id, label, probeRole(id), target, probeColour(id) });
            }
        }

        if (const auto* groupArray = root->getProperty("groups").getArray())
        {
            for (const auto& entry : *groupArray)
            {
                const auto* object = entry.getDynamicObject();
                if (object == nullptr)
                    continue;

                Group group;
                group.id = stringProperty(*object, "id", "G" + juce::String((int)loadedGroups.size() + 1));
                group.name = stringProperty(*object, "name", "Group " + juce::String((int)loadedGroups.size() + 1));
                group.category = stringProperty(*object, "category", "user_group");
                group.notes = stringProperty(*object, "notes", {});
                if (const auto* members = object->getProperty("members").getArray())
                {
                    for (const auto& member : *members)
                    {
                        const auto refdes = member.toString();
                        for (int i = 0; i < (int)loadedInstances.size(); ++i)
                            if (loadedInstances[(size_t)i].refdes == refdes)
                                group.memberInstances.push_back(i);
                    }
                }
                if (!group.memberInstances.empty())
                    loadedGroups.push_back(std::move(group));
            }
        }

        if (const auto* parameterArray = root->getProperty("simulationParameters").getArray())
        {
            for (const auto& entry : *parameterArray)
            {
                const auto* object = entry.getDynamicObject();
                if (object == nullptr)
                    continue;

                SimulationParameter p;
                p.id = stringProperty(*object, "id", {});
                p.refdes = stringProperty(*object, "refdes", {});
                p.property = stringProperty(*object, "property", {});
                p.label = stringProperty(*object, "label", p.id);
                p.unit = stringProperty(*object, "unit", {});
                p.defaultValue = stringProperty(*object, "default", {});
                p.minValue = stringProperty(*object, "min", {});
                p.maxValue = stringProperty(*object, "max", {});
                p.scaling = stringProperty(*object, "scaling", "linear");
                p.control = stringProperty(*object, "control", "slider");
                if (p.id.isNotEmpty() && p.refdes.isNotEmpty() && p.property.isNotEmpty())
                    loadedSimulationParameters.push_back(std::move(p));
            }
        }

        const auto previousProbes = probes;
        instances = std::move(loadedInstances);
        junctions = std::move(loadedJunctions);
        junctionSheets = std::move(loadedJunctionSheets);
        currentSheet = {};
        wires = std::move(loadedWires);
        probes = std::move(loadedProbes);
        groups = std::move(loadedGroups);
        simulationParameters = std::move(loadedSimulationParameters);
        selectedInstance = instances.empty() ? -1 : 0;
        selectedInstances.clear();
        if (selectedInstance >= 0)
            selectedInstances.add(selectedInstance);
        selectedGroup = -1;
        wireDragging = false;
        draggingInstance = false;
        resizingRail = false;
        notifySelection();

        if (onProbeChanged)
        {
            for (const auto& probe : previousProbes)
                onProbeChanged(probe.id, probe.label, {});
            for (const auto& probe : probes)
                onProbeChanged(probe.id, probe.label, nodeLabel(probe.node));
        }

        repaint();
        return true;
    }

    juce::String buildCircuitJson() const
    {
        const auto netNames = computeNetNames();
        juce::String text;
        text << "{\n";
        text << "  \"schemaVersion\": 2,\n";
        text << "  \"kind\": \"electronics_circuit\",\n";
        text << "  \"components\": [\n";
        for (size_t i = 0; i < instances.size(); ++i)
        {
            const auto& instance = instances[i];
            const auto symbol = symbolForInstance(instance);
            if (i != 0) text << ",\n";
            text << "    {\n";
            text << "      \"id\": " << quote(instance.refdes) << ",\n";
            text << "      \"symbol\": " << quote(instance.symbolId) << ",\n";
            text << "      \"component\": {\n";
            text << "        \"archetype\": " << quote(archetypeFor(instance.symbolId)) << ",\n";
            text << "        \"family\": " << nullableQuote(instance.family) << ",\n";
            text << "        \"manufacturerPart\": " << nullableQuote(instance.manufacturerPart) << ",\n";
            text << "        \"datasheetStatus\": \"generic\"\n";
            text << "      },\n";
            text << "      \"x\": " << instance.position.x << ",\n";
            text << "      \"y\": " << instance.position.y << ",\n";
            text << "      \"rotation\": " << instance.rotation << ",\n";
            if (instance.sheet.isNotEmpty())
                text << "      \"sheet\": " << quote(instance.sheet) << ",\n";
            if (instance.symbolId == "sub_block")
            {
                text << "      \"childSheet\": " << quote(instance.childSheet) << ",\n";
                text << "      \"ports\": [";
                for (size_t k = 0; k < instance.ports.size(); ++k)
                    text << (k == 0 ? "" : ", ") << "{ \"name\": " << quote(instance.ports[k].name)
                         << ", \"side\": " << quote(instance.ports[k].rightSide ? "right" : "left") << " }";
                text << "],\n";
            }
            if (isRailBus(instance.symbolId))
                text << "      \"length\": " << instance.busLength << ",\n";
            text << "      \"value\": " << quote(instance.value) << ",\n";
            text << "      \"frequency\": " << quote(instance.frequency) << ",\n";
            text << "      \"busName\": " << quote(instance.busName) << ",\n";
            if (!instance.params.empty())
            {
                text << "      \"params\": {";
                bool firstParam = true;
                for (const auto& [key, val] : instance.params)
                {
                    text << (firstParam ? " " : ", ") << quote(key) << ": " << quote(val);
                    firstParam = false;
                }
                text << " },\n";
            }
            text << "      \"parameters\": " << parametersJsonFor(instance) << ",\n";
            text << "      \"pins\": {\n";
            for (size_t p = 0; p < symbol.pins.size(); ++p)
            {
                if (p != 0) text << ",\n";
                text << "        " << quote(symbol.pins[p].name) << ": "
                     << quote(netFor({ (int)i, (int)p }, netNames));
            }
            text << "\n      }\n";
            text << "    }";
        }
        text << "\n  ],\n";
        text << "  \"wires\": [\n";
        for (size_t i = 0; i < wires.size(); ++i)
        {
            const auto& wire = wires[i];
            if (i != 0) text << ",\n";
            text << "    { \"a\": " << quote(nodeLabel(wire.a))
                 << ", \"b\": " << quote(nodeLabel(wire.b)) << " }";
        }
        text << "\n  ],\n";
        text << "  \"junctions\": [\n";
        for (size_t i = 0; i < junctions.size(); ++i)
        {
            const auto& junction = junctions[i];
            if (i != 0) text << ",\n";
            text << "    { \"id\": " << quote("N" + juce::String((int)i + 1))
                 << ", \"x\": " << junction.x
                 << ", \"y\": " << junction.y
                 << ", \"sheet\": " << quote(i < junctionSheets.size() ? junctionSheets[i] : juce::String()) << " }";
        }
        text << "\n  ],\n";
        text << "  \"groups\": [\n";
        for (size_t i = 0; i < groups.size(); ++i)
        {
            const auto& group = groups[i];
            if (i != 0) text << ",\n";
            text << "    {\n";
            text << "      \"id\": " << quote(group.id) << ",\n";
            text << "      \"name\": " << quote(group.name) << ",\n";
            text << "      \"category\": " << quote(group.category) << ",\n";
            text << "      \"notes\": " << quote(group.notes) << ",\n";
            text << "      \"members\": [";
            bool first = true;
            for (int member : group.memberInstances)
            {
                if (member < 0 || member >= (int)instances.size())
                    continue;
                if (!first) text << ", ";
                first = false;
                text << quote(instances[(size_t)member].refdes);
            }
            text << "]\n";
            text << "    }";
        }
        text << "\n  ],\n";
        text << "  \"simulationParameters\": [\n";
        for (size_t i = 0; i < simulationParameters.size(); ++i)
        {
            const auto& p = simulationParameters[i];
            if (i != 0) text << ",\n";
            text << "    { \"id\": " << quote(p.id)
                 << ", \"refdes\": " << quote(p.refdes)
                 << ", \"property\": " << quote(p.property)
                 << ", \"label\": " << quote(p.label)
                 << ", \"unit\": " << quote(p.unit)
                 << ", \"default\": " << quote(p.defaultValue)
                 << ", \"min\": " << quote(p.minValue)
                 << ", \"max\": " << quote(p.maxValue)
                 << ", \"scaling\": " << quote(p.scaling)
                 << ", \"control\": " << quote(p.control)
                 << " }";
        }
        text << "\n  ],\n";
        text << "  \"probes\": [\n";
        for (size_t i = 0; i < probes.size(); ++i)
        {
            const auto& probe = probes[i];
            if (i != 0) text << ",\n";
            text << "    { \"id\": " << quote(probe.id)
                 << ", \"label\": " << quote(probe.label)
                 << ", \"role\": " << quote(probe.role)
                 << ", \"target\": " << quote(nodeLabel(probe.node))
                 << ", \"net\": " << quote(netForNode(probe.node, netNames))
                 << " }";
        }
        text << "\n  ]\n";
        text << "}\n";
        return text;
    }

    void runHierarchicalTest()
    {
        instances.clear();
        wires.clear();
        groups.clear();
        junctions.clear();
        currentSheet = "Main";

        Instance v1; v1.symbolId = "voltage_source"; v1.refdes = "V1"; v1.sheet = "Main"; instances.push_back(v1);

        Instance b1; b1.symbolId = "sub_block"; b1.refdes = "A1"; b1.sheet = "Main"; b1.childSheet = "S1"; 
        b1.ports.push_back({"IN", false}); b1.ports.push_back({"OUT", true}); instances.push_back(b1);

        Instance b2; b2.symbolId = "sub_block"; b2.refdes = "A2"; b2.sheet = "Main"; b2.childSheet = "S2"; 
        b2.ports.push_back({"IN", false}); b2.ports.push_back({"OUT", true}); instances.push_back(b2);

        Instance p1_in; p1_in.symbolId = "block_port"; p1_in.sheet = "S1"; p1_in.busName = "IN"; instances.push_back(p1_in);
        Instance p1_out; p1_out.symbolId = "block_port"; p1_out.sheet = "S1"; p1_out.busName = "OUT"; instances.push_back(p1_out);
        Instance r1; r1.symbolId = "resistor"; r1.refdes = "R1"; r1.sheet = "S1"; instances.push_back(r1);

        Instance p2_in; p2_in.symbolId = "block_port"; p2_in.sheet = "S2"; p2_in.busName = "IN"; instances.push_back(p2_in);
        Instance p2_out; p2_out.symbolId = "block_port"; p2_out.sheet = "S2"; p2_out.busName = "OUT"; instances.push_back(p2_out);
        Instance r2; r2.symbolId = "resistor"; r2.refdes = "R1"; r2.sheet = "S2"; instances.push_back(r2); 

        wires.push_back({ WireNode::forPin({0, 0}), WireNode::forPin({1, 0}) }); 
        wires.push_back({ WireNode::forPin({1, 1}), WireNode::forPin({2, 0}) }); 
        wires.push_back({ WireNode::forPin({2, 1}), WireNode::forPin({0, 1}) }); 
        
        wires.push_back({ WireNode::forPin({3, 0}), WireNode::forPin({5, 0}) }); 
        wires.push_back({ WireNode::forPin({5, 1}), WireNode::forPin({4, 0}) }); 

        wires.push_back({ WireNode::forPin({6, 0}), WireNode::forPin({8, 0}) }); 
        wires.push_back({ WireNode::forPin({8, 1}), WireNode::forPin({7, 0}) }); 

        auto sim = buildSimNetlist();
        juce::String xyce = buildXyceNetlist();
        
        juce::File logFile("C:/Users/wwestlake/Documents/hierarchy_test_output.txt");
        logFile.replaceWithText("XYCE NETLIST:\n" + xyce + "\n\nSIM NETLIST PARTS:\n");
        for (const auto& [refdes, id] : sim.elementOfPart)
            logFile.appendText(refdes + " -> " + juce::String(id) + "\n");
        juce::MessageManager::getInstance()->stopDispatchLoop();
    }

    juce::String buildXyceNetlist() const
    {
        const auto netNames = computeNetNames();
        juce::String netlist;
        netlist << "* Djehuti Electronics Lab generated Xyce netlist\n";
        netlist << "* Research prototype output. Circuit JSON remains authoritative.\n\n";

        bool hasGround = false;
        bool hasProbe = false;
        std::set<juce::String> modelLines;

        for (size_t i = 0; i < instances.size(); ++i)
        {
            const auto& instance = instances[i];
            const auto symbol = symbolForInstance(instance);
            auto pinNet = [&](const juce::String& pinName) {
                for (size_t p = 0; p < symbol.pins.size(); ++p)
                    if (symbol.pins[p].name == pinName)
                        return netFor({ (int)i, (int)p }, netNames);
                return juce::String("floating");
            };

            if (instance.symbolId == "ground" || instance.symbolId == "ground_bus")
            {
                hasGround = true;
                continue;
            }
            if (instance.symbolId == "power_bus")
            {
                netlist << "* " << instance.refdes << " " << instance.busName << " power bus on net " << pinNet("VBUS") << "\n";
                continue;
            }
            if (instance.symbolId == "power_port" || instance.symbolId == "net_label"
                || instance.symbolId == "sub_block" || instance.symbolId == "block_port")
                continue;

            const auto* def = spice_library::findModel(partValue(instance, "value"));
            if (def != nullptr)
                modelLines.insert(def->rawText);

            if (instance.symbolId == "resistor")
            {
                netlist << instance.refdes << " " << pinNet("1") << " " << pinNet("2") << " " << instance.value << "\n";
                hasProbe = true;
            }
            else if (instance.symbolId == "capacitor")
            {
                netlist << instance.refdes << " " << pinNet("1") << " " << pinNet("2") << " " << instance.value << "\n";
            }
            else if (instance.symbolId == "inductor")
            {
                netlist << instance.refdes << " " << pinNet("1") << " " << pinNet("2") << " " << instance.value << "\n";
            }
            else if (instance.symbolId == "diode" || instance.symbolId == "schottky_diode" || instance.symbolId == "zener_diode" || instance.symbolId == "led")
            {
                juce::String name = def ? def->name : instance.value;
                netlist << "D" << instance.refdes << " " << pinNet("A") << " " << pinNet("K") << " " << name << "\n";
                if (!def) modelLines.insert(".MODEL " + name + " D");
                hasProbe = true;
            }
            else if (instance.symbolId == "npn" || instance.symbolId == "pnp")
            {
                juce::String name = def ? def->name : instance.value;
                netlist << "Q" << instance.refdes << " " << pinNet("C") << " " << pinNet("B") << " " << pinNet("E") << " " << name << "\n";
                if (!def) modelLines.insert(".MODEL " + name + " " + (instance.symbolId == "npn" ? "NPN" : "PNP"));
            }
            else if (instance.symbolId == "njfet" || instance.symbolId == "pjfet")
            {
                juce::String name = def ? def->name : instance.value;
                netlist << "J" << instance.refdes << " " << pinNet("D") << " " << pinNet("G") << " " << pinNet("S") << " " << name << "\n";
                if (!def) modelLines.insert(".MODEL " + name + " " + (instance.symbolId == "njfet" ? "NJF" : "PJF"));
            }
            else if (instance.symbolId == "nmos" || instance.symbolId == "pmos")
            {
                juce::String name = def ? def->name : instance.value;
                netlist << "M" << instance.refdes << " " << pinNet("D") << " " << pinNet("G") << " " << pinNet("S") << " " << pinNet("S") << " " << name << "\n";
                if (!def) modelLines.insert(".MODEL " + name + " " + (instance.symbolId == "nmos" ? "NMOS" : "PMOS"));
            }
            else if (instance.symbolId == "opamp_741")
            {
                juce::String name = def ? def->name : instance.value;
                netlist << "X" << instance.refdes << " " << pinNet("IN+") << " " << pinNet("IN-") << " " << pinNet("V+") << " " << pinNet("V-") << " " << pinNet("OUT") << " " << name << "\n";
            }
            else if (instance.symbolId == "voltage_source")
            {
                netlist << instance.refdes << " " << pinNet("+") << " " << pinNet("-") << " DC " << instance.value << "\n";
                hasProbe = true;
            }
            else if (instance.symbolId == "battery")
            {
                netlist << instance.refdes << " " << pinNet("+") << " " << pinNet("-") << " DC " << instance.value << "\n";
                hasProbe = true;
            }
            else if (instance.symbolId == "ac_voltage_source")
            {
                netlist << instance.refdes << " " << pinNet("+") << " " << pinNet("-") << " AC " << instance.value
                        << " SIN(0 " << instance.value << " " << instance.frequency << ")\n";
                hasProbe = true;
            }
            else if (instance.symbolId == "signal_source")
            {
                netlist << instance.refdes << " " << pinNet("OUT") << " " << pinNet("REF") << " AC " << instance.value
                        << " SIN(0 " << instance.value << " " << instance.frequency << ")\n";
                hasProbe = true;
            }
            else
            {
                netlist << "* " << instance.refdes << " (" << instance.symbolId << ") not lowered to Xyce yet\n";
            }
        }

        if (!modelLines.empty())
        {
            netlist << "\n* SPICE Models\n";
            for (const auto& line : modelLines)
                netlist << line << "\n";
        }

        netlist << "\n.OP\n";
        netlist << ".PRINT DC";
        const auto printableNets = printableNetNames(netNames);
        if (printableNets.empty())
        {
            netlist << " V(0)";
        }
        else
        {
            for (const auto& net : printableNets)
                netlist << " V(" << net << ")";
        }
        netlist << "\n.END\n";

        if (!hasGround)
            netlist = "* WARNING: no ground symbol found; generated netlist may not solve.\n" + netlist;
        if (!hasProbe)
            netlist = "* WARNING: no lowered source/resistor found; this netlist is mostly structural.\n" + netlist;

        return netlist;
    }

    juce::String buildErcReport() const
    {
        const auto netNames = computeNetNames();
        const auto totalPins = pinCount();
        const auto totalNodes = totalPins + (int)junctions.size();
        std::vector<int> nodeDegree((size_t)std::max(0, totalNodes), 0);

        for (const auto& wire : wires)
        {
            const auto a = nodeOrdinal(wire.a);
            const auto b = nodeOrdinal(wire.b);
            if (a >= 0 && a < totalNodes) ++nodeDegree[(size_t)a];
            if (b >= 0 && b < totalNodes) ++nodeDegree[(size_t)b];
        }

        int errors = 0;
        int warnings = 0;
        int infos = 0;
        juce::String findings;

        auto addFinding = [&](const juce::String& severity, const juce::String& message) {
            if (severity == "ERROR") ++errors;
            else if (severity == "WARN") ++warnings;
            else ++infos;

            findings << "- [" << severity << "] " << message << "\n";
        };

        auto pinNet = [&](int instanceIndex, const juce::String& pinName) {
            const auto symbol = symbolForInstance(instances[(size_t)instanceIndex]);
            for (int p = 0; p < (int)symbol.pins.size(); ++p)
                if (symbol.pins[(size_t)p].name == pinName)
                    return netFor({ instanceIndex, p }, netNames);
            return juce::String("floating");
        };

        auto unsupportedForXyce = [](const juce::String& symbolId) {
            return symbolId.startsWith("logic_");
        };

        if (instances.empty())
            addFinding("ERROR", "No components are placed on the schematic.");

        bool hasGround = false;
        bool hasLoweredPrimitive = false;
        for (const auto& instance : instances)
        {
            if (instance.symbolId == "ground" || instance.symbolId == "ground_bus")
                hasGround = true;
            if (instance.symbolId == "resistor"
                || instance.symbolId == "capacitor"
                || instance.symbolId == "inductor"
                || instance.symbolId == "diode"
                || instance.symbolId == "voltage_source"
                || instance.symbolId == "battery"
                || instance.symbolId == "ac_voltage_source"
                || instance.symbolId == "signal_source"
                || instance.symbolId == "behavioral_voltage_source"
                || instance.symbolId == "behavioral_current_source"
                || instance.symbolId == "vcvs"
                || instance.symbolId == "vccs"
                || instance.symbolId == "ccvs"
                || instance.symbolId == "cccs")
                hasLoweredPrimitive = true;
        }

        if (!hasGround)
            addFinding("ERROR", "No ground reference is present. Add a ground or ground bus before running solver-backed analysis.");
        if (!hasLoweredPrimitive && !instances.empty())
            addFinding("WARN", "No currently lowered Xyce primitive is present. The generated netlist will be mostly structural.");

        std::set<juce::String> refdesSeen;
        for (int i = 0; i < (int)instances.size(); ++i)
        {
            const auto& instance = instances[(size_t)i];
            const auto symbol = symbolForInstance(instance);

            if (refdesSeen.count(instance.refdes) != 0)
                addFinding("ERROR", "Duplicate reference designator found: " + instance.refdes + ".");
            refdesSeen.insert(instance.refdes);

            if (instance.value.trim().isEmpty()
                && instance.symbolId != "ground"
                && instance.symbolId != "ground_bus"
                && instance.symbolId != "power_bus"
                && instance.symbolId != "power_port"
                && instance.symbolId != "net_label"
                && instance.symbolId != "block_port")
                addFinding("WARN", instance.refdes + " has no value or model text.");

            if (instance.symbolId == "net_label" && instance.busName.trim().isEmpty())
                addFinding("ERROR", instance.refdes + " is a net label with no name.");

            if (instance.symbolId == "power_port" && instance.busName.trim().isEmpty())
                addFinding("ERROR", instance.refdes + " is a supply port with no net name.");

            if (unsupportedForXyce(instance.symbolId))
                addFinding("INFO", instance.refdes + " (" + instance.symbolId + ") is captured in the model but not lowered to Xyce yet.");

            for (int p = 0; p < (int)symbol.pins.size(); ++p)
            {
                const auto ordinal = pinOrdinal({ i, p });
                if (ordinal >= 0 && ordinal < (int)nodeDegree.size() && nodeDegree[(size_t)ordinal] == 0)
                    addFinding("WARN", instance.refdes + "." + symbol.pins[(size_t)p].name + " is not wired.");
            }

            if (symbol.pins.size() == 2
                && (instance.symbolId == "resistor"
                    || instance.symbolId == "capacitor"
                    || instance.symbolId == "inductor"
                    || instance.symbolId == "diode"))
            {
                const auto a = netFor({ i, 0 }, netNames);
                const auto b = netFor({ i, 1 }, netNames);
                if (a == b)
                    addFinding("WARN", instance.refdes + " has both pins on " + a + ".");
            }

            if ((instance.symbolId == "voltage_source" || instance.symbolId == "battery" || instance.symbolId == "ac_voltage_source" || instance.symbolId == "audio_in")
                && pinNet(i, "+") == pinNet(i, "-"))
                addFinding("ERROR", instance.refdes + " has positive and negative terminals on the same net.");
            if (instance.symbolId == "signal_source" && pinNet(i, "OUT") == pinNet(i, "REF"))
                addFinding("ERROR", instance.refdes + " has OUT and REF on the same net.");
        }

        for (const auto& wire : wires)
        {
            if (sameNode(wire.a, wire.b))
                addFinding("WARN", "A wire loops back to " + nodeLabel(wire.a) + ".");
        }

        // Every named supply net needs something that actually sets its voltage.
        std::set<juce::String> supplyNets;
        std::set<juce::String> drivenNets;
        for (int i = 0; i < (int)instances.size(); ++i)
        {
            const auto& instance = instances[(size_t)i];
            if (instance.symbolId == "power_port" && instance.busName.trim().isNotEmpty())
                supplyNets.insert(pinNet(i, "1"));
            if (instance.symbolId == "voltage_source" || instance.symbolId == "battery")
            {
                drivenNets.insert(pinNet(i, "+"));
                drivenNets.insert(pinNet(i, "-"));
            }
        }
        for (const auto& net : supplyNets)
            if (drivenNets.count(net) == 0)
                addFinding("ERROR", "Supply net " + net + " has ports but no voltage source or battery driving it.");

        if (probes.empty() && !instances.empty())
            addFinding("INFO", "No lab probes are assigned yet, so instruments do not have schematic targets.");

        juce::String report;
        report << "# Electrical Rule Check\n\n";
        report << "- Components: " << (int)instances.size() << "\n";
        report << "- Wires: " << (int)wires.size() << "\n";
        report << "- Junctions: " << (int)junctions.size() << "\n";
        report << "- Probes: " << (int)probes.size() << "\n";
        report << "- Errors: " << errors << "\n";
        report << "- Warnings: " << warnings << "\n";
        report << "- Info: " << infos << "\n\n";

        if (findings.isEmpty())
            report << "No ERC findings.\n";
        else
            report << "## Findings\n\n" << findings;

        return report;
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff0e141a));

        g.saveState();
        g.addTransform(juce::AffineTransform::scale(canvasZoom).translated(viewOffset.x, viewOffset.y));
        drawGrid(g);
        drawWires(g);
        drawGroups(g);
        drawInstances(g);
        drawPendingWire(g);
        drawSelectionBox(g);
        drawErcAnnotations(g);
        g.restoreState();
        drawBreadcrumb(g);

        g.setColour(juce::Colour(0xff93a7b0));
        g.setFont(juce::Font(13.0f));
        const auto stampOn = getStampPlacementEnabled != nullptr && getStampPlacementEnabled();
        const auto modeText = snapEnabled ? juce::String("Snap on") : juce::String("Snap off");
        const auto zoomText = juce::String((int)std::round(canvasZoom * 100.0f)) + "%";
        const auto hintText = juce::String(stampOn ? "Stamp mode: click empty canvas to place selected symbols, drag selected parts to move, R rotates."
                                                   : "Drag pins/wires/rails to connect. Select a rail and drag its end handles to resize. Delete removes selected parts.")
            + "  " + modeText + "  Zoom " + zoomText;
        g.drawText(hintText,
                   getLocalBounds().reduced(12).removeFromBottom(24),
                   juce::Justification::centredLeft);

        if (dragHover)
            g.drawText(dragMessage, getLocalBounds().reduced(12).removeFromBottom(24), juce::Justification::centredRight);
    }

    void drawErcAnnotations(juce::Graphics& g)
    {
        const auto totalPins = pinCount();
        const auto totalNodes = totalPins + (int)junctions.size();
        std::vector<int> nodeDegree((size_t)std::max(0, totalNodes), 0);
        for (const auto& wire : wires)
        {
            const auto a = nodeOrdinal(wire.a);
            const auto b = nodeOrdinal(wire.b);
            if (a >= 0 && a < totalNodes) ++nodeDegree[(size_t)a];
            if (b >= 0 && b < totalNodes) ++nodeDegree[(size_t)b];
        }

        g.setColour(juce::Colour(0xffff4040).withAlpha(0.8f));
        g.setFont(juce::Font(20.0f, juce::Font::bold));

        for (int i = 0; i < (int)instances.size(); ++i)
        {
            if (!nodeOnSheet(WireNode::forPin({ i, 0 }))) continue;
            const auto& instance = instances[(size_t)i];
            const auto symbol = symbolForInstance(instance);
            if (symbol.id == "sub_block" || symbol.id == "block_port" || symbol.id == "annotation_text")
                continue;
            
            for (int p = 0; p < (int)symbol.pins.size(); ++p)
            {
                const auto ord = nodeOrdinal(WireNode::forPin({ i, p }));
                if (ord >= 0 && ord < totalNodes && nodeDegree[(size_t)ord] == 0)
                {
                    const auto pt = instance.position + schematic::rotateOffset(symbol.pins[(size_t)p].offset, instance.rotation);
                    g.drawEllipse(pt.x - 6.0f, pt.y - 6.0f, 12.0f, 12.0f, 2.0f);
                    g.drawText("!", pt.x - 10.0f, pt.y - 28.0f, 20.0f, 20.0f, juce::Justification::centred);
                }
            }
        }
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        grabKeyboardFocus();
        for (const auto& [area, sheet] : breadcrumbAreas)
            if (area.contains(event.position))
            {
                openSheet(sheet);
                return;
            }
        const auto modelPosition = viewToCanvas(event.position);
        const auto p = snapPoint(modelPosition);
        if (event.mods.isMiddleButtonDown())
        {
            beginPan(event.position);
            return;
        }

        if (event.mods.isRightButtonDown())
        {
            if (const auto groupIndex = hitTestGroup(modelPosition); groupIndex >= 0)
                selectGroup(groupIndex);
            showContextMenu(modelPosition);
            return;
        }

        if (beginRailResize(modelPosition))
        {
            repaint();
            return;
        }

        if (auto rail = hitTestRailBus(modelPosition); rail >= 0)
        {
            pushUndoSnapshot();
            beginWireDrag(createRailTap(rail, p), modelPosition);
            repaint();
            return;
        }

        if (auto pin = hitTestPin(modelPosition); pin.instanceIndex >= 0)
        {
            pushUndoSnapshot();
            beginWireDrag(WireNode::forPin(pin), modelPosition);
            repaint();
            return;
        }

        if (auto junction = hitTestJunction(modelPosition); junction >= 0)
        {
            pushUndoSnapshot();
            beginWireDrag(WireNode::forJunction(junction), modelPosition);
            repaint();
            return;
        }

        if (auto wireIndex = hitTestWire(modelPosition); wireIndex >= 0)
        {
            pushUndoSnapshot();
            beginWireDrag(createJunctionOnWire(wireIndex, p), modelPosition);
            repaint();
            return;
        }

        if (const auto groupIndex = hitTestGroup(modelPosition); groupIndex >= 0)
        {
            selectGroup(groupIndex);
            draggingInstance = true;
            dragStartMouse = modelPosition;
            dragStartPosition = groupBounds(groups[(size_t)groupIndex]).getPosition();
            captureSelectedDragStarts();
            notifySelection();
            repaint();
            return;
        }

        if (const auto instanceIndex = hitTestInstance(modelPosition); instanceIndex >= 0)
        {
            selectedInstance = instanceIndex;
            if (!event.mods.isShiftDown() && !isInstanceSelected(instanceIndex))
                selectedInstances.clear();
            if (event.mods.isShiftDown())
                toggleInstanceSelection(instanceIndex);
            else
                selectedInstances.addIfNotAlreadyThere(instanceIndex);
            draggingInstance = true;
            dragSnapshotTaken = false;
            dragStartMouse = modelPosition;
            dragStartPosition = instances[(size_t)selectedInstance].position;
            captureSelectedDragStarts();
            notifySelection();
            repaint();
            return;
        }

        if (getStampPlacementEnabled != nullptr && getStampPlacementEnabled())
        {
            const auto selected = getSelectedSymbolId != nullptr ? getSelectedSymbolId() : juce::String("resistor");
            pushUndoSnapshot();
            placeSymbol(selected, p);
            repaint();
            return;
        }

        beginSelectionBox(modelPosition, event.mods.isShiftDown());
    }

    void mouseDoubleClick(const juce::MouseEvent& event) override
    {
        const auto modelPosition = viewToCanvas(event.position);
        if (const auto instanceIndex = hitTestInstance(modelPosition); instanceIndex >= 0)
        {
            selectedInstance = instanceIndex;
            notifySelection();

            const auto& instance = instances[(size_t)instanceIndex];
            if (instance.symbolId == "sub_block")
            {
                openSheet(instance.childSheet);
                return;
            }
            if (isInstrumentNode(instance.symbolId) && onInstrumentOpen)
            {
                onInstrumentOpen(instance.refdes, instance.symbolId);
                if (onStatus) onStatus("Opened instrument panel for " + instance.refdes + ".");
            }
            repaint();
        }
    }

    void resized() override {}

    void mouseMove(const juce::MouseEvent& event) override
    {
        const auto modelPosition = viewToCanvas(event.position);

        if (hitTestPin(modelPosition).instanceIndex >= 0 || hitTestRailBus(modelPosition) >= 0 || hitTestJunction(modelPosition) >= 0 || hitTestWire(modelPosition) >= 0)
            setMouseCursor(juce::MouseCursor::CrosshairCursor);
        else if (hitTestInstance(modelPosition) >= 0 || hitTestGroup(modelPosition) >= 0)
            setMouseCursor(juce::MouseCursor::PointingHandCursor);
        else
            setMouseCursor(juce::MouseCursor::NormalCursor);
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (panning)
        {
            viewOffset = panStartOffset + (event.position - panStartMouse);
            repaint();
            return;
        }

        const auto modelPosition = viewToCanvas(event.position);
        if (resizingRail)
        {
            resizeRail(modelPosition);
            repaint();
            return;
        }

        if (wireDragging)
        {
            wireDragPosition = modelPosition;
            repaint();
            return;
        }

        if (selectingBox)
        {
            selectionBoxEnd = modelPosition;
            updateSelectionFromBox();
            repaint();
            return;
        }

        if (!draggingInstance || selectedInstance < 0 || selectedInstance >= (int)instances.size())
            return;

        if (!dragSnapshotTaken)
        {
            pushUndoSnapshot();
            dragSnapshotTaken = true;
        }
        const auto delta = snapPoint(dragStartPosition + (modelPosition - dragStartMouse)) - dragStartPosition;
        if (selectedInstances.size() > 1)
            moveSelectedInstances(delta);
        else
            instances[(size_t)selectedInstance].position = snapPoint(dragStartPosition + (modelPosition - dragStartMouse));
        notifySelection();
        repaint();
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        const auto modelPosition = viewToCanvas(event.position);
        if (wireDragging)
        {
            finishWireDrag(modelPosition);
            repaint();
        }

        draggingInstance = false;
        dragSnapshotTaken = false;
        selectingBox = false;
        resizingRail = false;
        resizingRailInstance = -1;
        panning = false;
    }

    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
    {
        if (std::abs(wheel.deltaY) <= 0.0001f)
            return;

        const auto before = viewToCanvas(event.position);
        const auto factor = wheel.deltaY > 0.0f ? 1.1f : (1.0f / 1.1f);
        canvasZoom = std::clamp(canvasZoom * factor, 0.5f, 2.5f);
        viewOffset += event.position - canvasToView(before);
        repaint();
    }

    void showContextMenu(juce::Point<float> modelPosition)
    {
        juce::PopupMenu menu;
        menu.addItem(1, "Auto Layout Diagram");
        menu.addItem(2, "Auto Layout Selection", !selectedInstances.isEmpty());
        menu.addItem(5, "Create Group Box from Selection...", !selectedInstances.isEmpty());
        menu.addItem(6, "Rename / Edit Group Box...", selectedGroup >= 0 && selectedGroup < (int)groups.size());
        menu.addItem(7, "Ungroup", selectedGroup >= 0 && selectedGroup < (int)groups.size());
        menu.addSeparator();
        const auto blockUnderMouse = [&] {
            const auto hit = hitTestInstance(modelPosition);
            return hit >= 0 && instances[(size_t)hit].symbolId == "sub_block" ? hit : -1;
        }();
        menu.addItem(8, (selectedGroup >= 0 ? "Make Sub-Diagram from Group..." : "Make Sub-Diagram from Selection..."),
                     !selectedInstances.isEmpty() || selectedGroup >= 0);
        menu.addItem(9, "Open Sub-Diagram", blockUnderMouse >= 0);
        menu.addItem(10, "Rename Sub-Diagram...", blockUnderMouse >= 0);
        menu.addItem(11, "Expand Sub-Diagram", blockUnderMouse >= 0);
        menu.addItem(15, "Save Sub-Diagram to User Library...", blockUnderMouse >= 0);
        menu.addItem(16, "Expose Block Parameter...", blockUnderMouse >= 0);
        menu.addItem(12, "Up One Level", currentSheet.isNotEmpty());
        const auto libraryBlocks = userBlocks();
        if (!libraryBlocks.empty())
        {
            juce::PopupMenu libraryMenu;
            std::map<juce::String, juce::PopupMenu> categories;
            for (int i = 0; i < (int)libraryBlocks.size(); ++i)
                categories[libraryBlocks[(size_t)i].category].addItem(1000 + i, libraryBlocks[(size_t)i].name);
            for (auto& [category, submenu] : categories)
                libraryMenu.addSubMenu(category, submenu);
            menu.addSubMenu("Place User Library Block", libraryMenu);
        }
        const auto supplyUnderMouse = [&] {
            for (int i = (int)instances.size() - 1; i >= 0; --i)
            {
                const auto& instance = instances[(size_t)i];
                if (instance.sheet != currentSheet) continue;
                if (isRailBus(instance.symbolId) && railBounds(instance).expanded(0.0f, hitDistance(12.0f)).contains(modelPosition)) return i;
                if ((instance.symbolId == "ground" || instance.symbolId == "power_port")
                    && orientedBounds(instance, symbolForInstance(instance)).expanded(hitDistance(6.0f)).contains(modelPosition)) return i;
            }
            return -1;
        }();
        if (supplyUnderMouse >= 0)
        {
            menu.addSeparator();
            const auto& supply = instances[(size_t)supplyUnderMouse];
            if (supply.symbolId == "power_bus") menu.addItem(13, "Change Rail to Supply Ports");
            else if (supply.symbolId == "ground_bus") menu.addItem(13, "Change Rail to Ground Symbols");
            else if (supply.symbolId == "ground") menu.addItem(14, "Change Ground Symbols to Ground Rail");
            else menu.addItem(14, "Change " + supply.busName + " Ports to a Rail");
        }
        menu.addSeparator();
        menu.addItem(20, "Copy Parts List");
        menu.addItem(21, "Copy Recursive Parts List");
        menu.addItem(22, "Save Parts List as TXT...");
        menu.addItem(23, "Save Recursive Parts List as TXT...");
        menu.addSeparator();
        menu.addItem(3, "Disconnect Here");

        const auto supplyRefdes = supplyUnderMouse >= 0 ? instances[(size_t)supplyUnderMouse].refdes : juce::String();
        menu.showMenuAsync(juce::PopupMenu::Options(), [this, modelPosition, blockUnderMouse, supplyRefdes, libraryBlocks](int result) {
            const auto blockRefdes = blockUnderMouse >= 0 ? instances[(size_t)blockUnderMouse].refdes : juce::String();
            if (result >= 1000 && result < 1000 + (int)libraryBlocks.size())
            {
                juce::String error;
                const auto placed = placeUserBlock(libraryBlocks[(size_t)(result - 1000)].name, modelPosition, {}, error);
                if (placed.isEmpty() && onStatus) onStatus("Could not place library block: " + error);
                return;
            }
            if (result == 13 || result == 14)
            {
                juce::String error;
                const auto index = instanceIndexForRefdesAnySheet(supplyRefdes);
                const auto done = result == 13 ? railToSymbols(index, error) : symbolsToRail(index, error);
                if (done.isEmpty() && onStatus) onStatus("Could not change the supply form: " + error);
            }
            if (result == 8)
                promptSubDiagramFromSelection();
            else if (result == 9 && blockIndexFor(blockRefdes) >= 0)
                openSheet(instances[(size_t)blockIndexFor(blockRefdes)].childSheet);
            else if (result == 10 && blockIndexFor(blockRefdes) >= 0)
                promptRenameBlock(blockIndexFor(blockRefdes));
            else if (result == 11 && blockIndexFor(blockRefdes) >= 0)
            {
                juce::String error;
                expandSubDiagram(blockIndexFor(blockRefdes), error);
            }
            else if (result == 15 && blockIndexFor(blockRefdes) >= 0)
                promptSaveBlockToLibrary(blockIndexFor(blockRefdes));
            else if (result == 16 && blockIndexFor(blockRefdes) >= 0)
                promptExposeBlockParameter(blockIndexFor(blockRefdes));
            else if (result == 12)
            {
                const auto block = blockForSheet(currentSheet);
                openSheet(block >= 0 ? instances[(size_t)block].sheet : juce::String());
            }
            if (result == 1)
                autoLayoutFromTool();
            else if (result == 2)
                autoLayoutSelectionFromTool();
            else if (result == 5)
                createGroupFromSelection();
            else if (result == 6)
                editSelectedGroupMetadata();
            else if (result == 7)
                ungroupSelectedGroup();
            else if (result == 20)
                copyPartsList(false);
            else if (result == 21)
                copyPartsList(true);
            else if (result == 22)
                savePartsListAsTxt(false);
            else if (result == 23)
                savePartsListAsTxt(true);
            else if (result == 3)
                disconnectAt(modelPosition);
            else if (result == 4)
                releaseProbeAt(modelPosition);
            repaint();
        });
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key.getModifiers().isCommandDown() && !key.getModifiers().isShiftDown()
            && (key.getTextCharacter() == 'z' || key.getTextCharacter() == 'Z'))
            return undoEdit();
        if ((key.getModifiers().isCommandDown() && (key.getTextCharacter() == 'y' || key.getTextCharacter() == 'Y'))
            || (key.getModifiers().isCommandDown() && key.getModifiers().isShiftDown()
                && (key.getTextCharacter() == 'z' || key.getTextCharacter() == 'Z')))
            return redoEdit();
        if (key == juce::KeyPress::backspaceKey && currentSheet.isNotEmpty())
        {
            const auto block = blockForSheet(currentSheet);
            openSheet(block >= 0 ? instances[(size_t)block].sheet : juce::String());
            return true;
        }
        if (key == juce::KeyPress::escapeKey)
        {
            wireDragging = false;
            repaint();
            return true;
        }
        if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        {
            deleteSelected();
            return true;
        }
        if (key.getTextCharacter() == 'r' || key.getTextCharacter() == 'R')
        {
            rotateSelected();
            return true;
        }
        return false;
    }

    bool isInterestedInDragSource(const SourceDetails& details) override
    {
        const auto description = details.description.toString();
        return description.startsWith("symbol:") || description.startsWith("probe:");
    }

    void itemDragEnter(const SourceDetails& details) override
    {
        dragHover = true;
        dragMessage = details.description.toString().startsWith("probe:")
            ? "Drop probe on a pin or wire"
            : "Drop symbol on schematic";
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        repaint();
    }

    void itemDragExit(const SourceDetails&) override
    {
        dragHover = false;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        repaint();
    }

    void itemDropped(const SourceDetails& details) override
    {
        dragHover = false;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        const auto description = details.description.toString();
        const auto modelPosition = viewToCanvas(details.localPosition.toFloat());
        const auto p = snapPoint(modelPosition);
        if (description.startsWith("probe:"))
        {
            const auto node = nodeAt(modelPosition, p);
            if (!node.isValid())
            {
                if (onStatus) onStatus("Probe drop needs a pin, junction, or wire.");
                repaint();
                return;
            }

            assignProbe(description.fromFirstOccurrenceOf("probe:", false, false), node);
            repaint();
            return;
        }

        if (!description.startsWith("symbol:"))
            return;

        const auto symbolId = description.fromFirstOccurrenceOf("symbol:", false, false);
        placeSymbol(symbolId, p);
        repaint();
    }

    juce::String placeSymbolFromTool(const juce::String& symbolId,
                                     float x,
                                     float y,
                                     const juce::String& value,
                                     const juce::String& frequency,
                                     const juce::String& busName)
    {
        const auto requestedSymbol = symbolId.trim();
        if (requestedSymbol == "sub_block" || requestedSymbol == "block_port")
            return "{ \"ok\": false, \"error\": \"Sub-diagram blocks and ports are created with schematic_subdiagram_create, not placed directly.\" }";
        if (!schematic::isSupportedSymbol(requestedSymbol))
            return "{ \"ok\": false, \"error\": \"Unsupported symbolId; no substitute was placed.\", \"requestedSymbolId\": "
                + quote(requestedSymbol) + " }";

        const auto before = instances.size();
        placeSymbol(requestedSymbol, snapPoint({ x, y }));
        if (instances.size() == before)
            return "{ \"ok\": false, \"error\": \"Could not place symbol.\" }";

        auto& instance = instances.back();
        if (value.trim().isNotEmpty())
            instance.value = value.trim();
        if (frequency.trim().isNotEmpty())
            instance.frequency = frequency.trim();
        if (busName.trim().isNotEmpty())
            instance.busName = busName.trim();

        notifySelection();
        repaint();

        juce::String result;
        result << "{\n";
        result << "  \"ok\": true,\n";
        result << "  \"tool\": \"schematic_place_symbol\",\n";
        result << "  \"displayTool\": \"schematic.place_symbol\",\n";
        result << "  \"refdes\": " << quote(instance.refdes) << ",\n";
        result << "  \"symbolId\": " << quote(instance.symbolId) << ",\n";
        result << "  \"x\": " << instance.position.x << ",\n";
        result << "  \"y\": " << instance.position.y << "\n";
        result << "}";
        return result;
    }

    juce::String createRlcHighPassFilterFromTool(double cutoffHz, double impedanceOhms)
    {
        if (cutoffHz <= 0.0 || impedanceOhms <= 0.0)
            return toolFailure("filter_design_high_pass", "cutoffHz and impedanceOhms must be positive.");

        const auto design = makeRlcHighPassDesign(cutoffHz, impedanceOhms);
        clearModel();

        auto place = [this](const juce::String& symbolId,
                            const juce::String& refdes,
                            juce::Point<float> position,
                            const juce::String& value,
                            const juce::String& frequency = {},
                            int rotation = 0) {
            placeSymbol(symbolId, position);
            auto& instance = instances.back();
            instance.refdes = refdes;
            instance.value = value;
            instance.frequency = frequency;
            instance.rotation = rotation;
            return refdes;
        };

        place("ac_voltage_source", "VIN1", { 120.0f, 288.0f }, "1", numberText(design.cutoffHz, 3));
        place("resistor", "RS1", { 312.0f, 216.0f }, numberText(design.sourceOhms, 3));
        place("capacitor", "C1", { 504.0f, 216.0f }, spiceCapacitance(design.capacitanceFarads));
        place("inductor", "L1", { 648.0f, 336.0f }, spiceInductance(design.inductanceHenries), {}, 90);
        place("resistor", "RL1", { 816.0f, 336.0f }, numberText(design.loadOhms, 3), {}, 90);
        place("ground", "GND_SRC", { 120.0f, 384.0f }, "0", {}, 0);
        place("ground", "GND_L", { 648.0f, 480.0f }, "0", {}, 0);
        place("ground", "GND_LOAD", { 816.0f, 480.0f }, "0", {}, 0);
        place("ground", "GND_SCOPE", { 1032.0f, 360.0f }, "0", {}, 0);
        place("oscilloscope_2ch", "SCOPE1", { 1032.0f, 216.0f }, "2ch", {}, 0);

        connectNodesFromTool("VIN1.-", "GND_SRC.0");
        connectNodesFromTool("VIN1.+", "RS1.1");
        connectNodesFromTool("RS1.2", "C1.1");
        connectNodesFromTool("C1.2", "L1.1");
        connectNodesFromTool("C1.2", "RL1.1");
        connectNodesFromTool("C1.2", "SCOPE1.CH1");
        connectNodesFromTool("VIN1.+", "SCOPE1.CH2");
        connectNodesFromTool("L1.2", "GND_L.0");
        connectNodesFromTool("RL1.2", "GND_LOAD.0");
        connectNodesFromTool("SCOPE1.REF", "GND_SCOPE.0");

        if (prefs::isOn("layout.after_design_tools"))
            autoLayoutInstances({});
        selectedInstance = instanceIndexForRefdes("C1");
        notifySelection();
        forceDeferredRepaint();

        juce::String result;
        result << "{\n";
        result << "  \"ok\": true,\n";
        result << "  \"tool\": \"filter_design_high_pass\",\n";
        result << "  \"displayTool\": \"filter.design_high_pass\",\n";
        result << "  \"topology\": \"source_8ohm_series_cap_shunt_inductor_load_8ohm\",\n";
        result << "  \"cutoffHz\": " << numberText(design.cutoffHz, 6) << ",\n";
        result << "  \"impedanceOhms\": " << numberText(design.impedanceOhms, 6) << ",\n";
        result << "  \"sourceOhms\": " << numberText(design.sourceOhms, 6) << ",\n";
        result << "  \"loadOhms\": " << numberText(design.loadOhms, 6) << ",\n";
        result << "  \"capacitanceFarads\": " << numberText(design.capacitanceFarads, 12) << ",\n";
        result << "  \"inductanceHenries\": " << numberText(design.inductanceHenries, 12) << ",\n";
        result << "  \"capacitanceLabel\": " << quote(humanCapacitance(design.capacitanceFarads)) << ",\n";
        result << "  \"inductanceLabel\": " << quote(humanInductance(design.inductanceHenries)) << "\n";
        result << "}";
        return result;
    }

    juce::String createPushPullAmplifierFromTool()
    {
        clearModel();

        auto place = [this](const juce::String& symbolId,
                            const juce::String& refdes,
                            juce::Point<float> position,
                            const juce::String& value,
                            const juce::String& frequency = {},
                            const juce::String& busName = {},
                            int rotation = 0) {
            placeSymbol(symbolId, position);
            auto& instance = instances.back();
            instance.refdes = refdes;
            instance.value = value;
            instance.frequency = frequency;
            instance.busName = busName;
            instance.rotation = rotation;
            return refdes;
        };

        auto junction = [this](juce::Point<float> p) {
            addJunction(p);
            return "N" + juce::String((int)junctions.size());
        };

        // Class AB complementary emitter follower, drawn the textbook way:
        // NPN above PNP with the output node between their emitters, the
        // diode bias string between the bases, supplies as named ports at
        // the pins that need them, and ground symbols at each ground pin.
        // Rotation 90 turns a horizontal two-pin part vertical with pin 1
        // (anode for diodes) on top.

        // Input: AC source coupled through C1 into the middle of the bias string.
        place("ac_voltage_source", "V1", { 168.0f, 384.0f }, "0.25", "1k", "Input");
        place("ground", "GND1", { 168.0f, 456.0f }, "0");
        place("capacitor", "C1", { 312.0f, 336.0f }, "10u");

        // Bias string: R1 from +12V, D1 and D2, R2 to -12V.
        place("power_port", "PWR1", { 408.0f, 0.0f }, {}, {}, "+12V");
        place("resistor", "R1", { 408.0f, 72.0f }, "2.2k", {}, {}, 90);
        place("diode", "D1", { 408.0f, 192.0f }, "1N4148", {}, {}, 90);
        place("diode", "D2", { 408.0f, 480.0f }, "1N4148", {}, {}, 90);
        place("resistor", "R2", { 408.0f, 600.0f }, "2.2k", {}, {}, 90);
        place("power_port", "PWR2", { 408.0f, 672.0f }, {}, {}, "-12V", 180);

        // Output pair with emitter ballast resistors.
        place("power_port", "PWR3", { 576.0f, 48.0f }, {}, {}, "+12V");
        place("npn", "Q1", { 552.0f, 144.0f }, "generic_npn");
        place("resistor", "R3", { 576.0f, 264.0f }, "0.47", {}, {}, 90);
        place("resistor", "R4", { 576.0f, 408.0f }, "0.47", {}, {}, 90);
        place("pnp", "Q2", { 552.0f, 528.0f }, "generic_pnp");
        place("power_port", "PWR4", { 576.0f, 624.0f }, {}, {}, "-12V", 180);

        // Load.
        place("resistor", "RL1", { 720.0f, 384.0f }, "8", {}, {}, 90);
        place("ground", "GND2", { 720.0f, 456.0f }, "0");

        // Supplies: stacked sources around a grounded midpoint.
        place("voltage_source", "V2", { 168.0f, 600.0f }, "12");
        place("power_port", "PWR5", { 168.0f, 528.0f }, {}, {}, "+12V");
        place("voltage_source", "V3", { 168.0f, 744.0f }, "12");
        place("power_port", "PWR6", { 168.0f, 816.0f }, {}, {}, "-12V", 180);
        place("ground", "GND3", { 240.0f, 672.0f }, "0");

        // Scope: CH1 on the input drive, CH2 on the output, REF to ground.
        place("oscilloscope_2ch", "SCOPE1", { 912.0f, 360.0f }, "2ch");
        place("ground", "GND4", { 912.0f, 456.0f }, "0");

        const auto inputNode = junction({ 408.0f, 336.0f });
        const auto outputNode = junction({ 576.0f, 336.0f });
        const auto supplyMid = junction({ 168.0f, 672.0f });

        connectNodesFromTool("V1.-", "GND1");
        connectNodesFromTool("V1.+", "C1.1");
        connectNodesFromTool("C1.2", inputNode);

        connectNodesFromTool("PWR1", "R1.1");
        connectNodesFromTool("R1.2", "D1.A");
        connectNodesFromTool("D1.K", inputNode);
        connectNodesFromTool(inputNode, "D2.A");
        connectNodesFromTool("D2.K", "R2.1");
        connectNodesFromTool("R2.2", "PWR2");

        connectNodesFromTool("D1.A", "Q1.B");
        connectNodesFromTool("D2.K", "Q2.B");
        connectNodesFromTool("PWR3", "Q1.C");
        connectNodesFromTool("Q1.E", "R3.1");
        connectNodesFromTool("R3.2", outputNode);
        connectNodesFromTool(outputNode, "R4.1");
        connectNodesFromTool("R4.2", "Q2.E");
        connectNodesFromTool("Q2.C", "PWR4");

        connectNodesFromTool(outputNode, "RL1.1");
        connectNodesFromTool("RL1.2", "GND2");

        connectNodesFromTool("PWR5", "V2.+");
        connectNodesFromTool("V2.-", supplyMid);
        connectNodesFromTool(supplyMid, "V3.+");
        connectNodesFromTool(supplyMid, "GND3");
        connectNodesFromTool("V3.-", "PWR6");

        connectNodesFromTool("SCOPE1.CH1", "V1.+");
        connectNodesFromTool("SCOPE1.CH2", "RL1.1");
        connectNodesFromTool("SCOPE1.REF", "GND4");

        if (prefs::isOn("layout.after_design_tools"))
            autoLayoutInstances({});
        selectedInstance = instanceIndexForRefdes("Q1");
        selectedInstances.clear();
        if (selectedInstance >= 0)
            selectedInstances.add(selectedInstance);
        notifySelection();
        forceDeferredRepaint();

        juce::String result;
        result << "{\n";
        result << "  \"ok\": true,\n";
        result << "  \"tool\": \"amplifier_design_push_pull\",\n";
        result << "  \"displayTool\": \"amplifier.design_push_pull\",\n";
        result << "  \"topology\": \"class_ab_complementary_emitter_follower\",\n";
        result << "  \"componentCount\": " << (int)instances.size() << ",\n";
        result << "  \"wireCount\": " << (int)wires.size() << ",\n";
        result << "  \"loadOhms\": 8,\n";
        result << "  \"inputFrequencyHz\": 1000,\n";
        result << "  \"note\": \"Transistor symbols are structurally represented; full BJT SPICE lowering is still a future solver capability.\"\n";
        result << "}";
        return result;
    }

    juce::String autoLayoutFromTool()
    {
        if (instances.empty())
            return "{ \"ok\": false, \"error\": \"No schematic components to lay out.\" }";
        return autoLayoutInstances({});
    }

    juce::String autoLayoutSelectionFromTool()
    {
        if (selectedInstances.isEmpty())
            return "{ \"ok\": false, \"error\": \"No selected components to lay out.\" }";
        juce::Array<int> selected = selectedInstances;
        return autoLayoutInstances(selected);
    }

    static bool isNetMarker(const juce::String& symbolId)
    {
        return schematic::isPowerSymbol(symbolId) || schematic::isRailBus(symbolId);
    }

    static juce::String supplyVoltsText(const juce::String& value)
    {
        auto text = value.trim();
        if (text.endsWithIgnoreCase("V"))
            text = text.dropLastCharacters(1).trim();
        if (text.startsWith("+") || text.startsWith("-"))
            text = text.substring(1);
        return text.isEmpty() ? juce::String("V") : text;
    }

    struct LayoutNets
    {
        std::map<juce::String, int> indexOf; // model net name -> layout net index
        std::vector<schematic::layout::Net> nets;
    };

    // Ground is net "0". Supply nets are named by their ports or rails, or,
    // failing that, derived from a DC source terminal whose other terminal is
    // grounded ("+12V", "-12V"). Everything else is a signal net. Signal nets
    // that touch only one pin are left out (that pin is unconnected).
    LayoutNets classifyLayoutNets(const std::map<int, juce::String>& netNames) const
    {
        std::map<juce::String, juce::String> supplyName;
        for (int i = 0; i < (int)instances.size(); ++i)
        {
            const auto& instance = instances[(size_t)i];
            if ((instance.symbolId == "power_port" || instance.symbolId == "power_bus") && instance.busName.trim().isNotEmpty())
                supplyName[netFor({ i, 0 }, netNames)] = instance.busName.trim();
        }
        for (int i = 0; i < (int)instances.size(); ++i)
        {
            const auto& instance = instances[(size_t)i];
            if (instance.symbolId != "voltage_source" && instance.symbolId != "battery")
                continue;
            const auto symbol = symbolForInstance(instance);
            juce::String plusNet, minusNet;
            for (int p = 0; p < (int)symbol.pins.size(); ++p)
            {
                if (symbol.pins[(size_t)p].name == "+") plusNet = netFor({ i, p }, netNames);
                if (symbol.pins[(size_t)p].name == "-") minusNet = netFor({ i, p }, netNames);
            }
            const auto volts = supplyVoltsText(instance.value);
            if (minusNet == "0" && plusNet != "0" && supplyName.count(plusNet) == 0)
                supplyName[plusNet] = "+" + volts + "V";
            if (plusNet == "0" && minusNet != "0" && supplyName.count(minusNet) == 0)
                supplyName[minusNet] = "-" + volts + "V";
        }

        std::map<juce::String, int> pinsOnNet;
        for (int i = 0; i < (int)instances.size(); ++i)
        {
            if (isNetMarker(instances[(size_t)i].symbolId))
                continue;
            const auto symbol = symbolForInstance(instances[(size_t)i]);
            for (int p = 0; p < (int)symbol.pins.size(); ++p)
                ++pinsOnNet[netFor({ i, p }, netNames)];
        }

        LayoutNets result;
        for (const auto& [net, count] : pinsOnNet)
        {
            if (net == "floating")
                continue;
            schematic::layout::Net layoutNet;
            if (net == "0")
            {
                layoutNet.name = "0";
                layoutNet.kind = schematic::layout::NetKind::Ground;
            }
            else if (const auto named = supplyName.find(net); named != supplyName.end())
            {
                layoutNet.name = named->second;
                layoutNet.kind = schematic::layout::NetKind::Supply;
            }
            else
            {
                if (count < 2)
                    continue;
                layoutNet.name = net;
                layoutNet.kind = schematic::layout::NetKind::Signal;
            }
            result.indexOf[net] = (int)result.nets.size();
            result.nets.push_back(layoutNet);
        }
        return result;
    }

    // Auto layout: connectivity-driven placement (SchematicLayout), fresh
    // ground symbols and supply ports at every pin that needs them, and
    // signal-net wiring topology chosen by libavoid's hyperedge router
    // (SchematicRouter). Ground symbols, supply ports, rails, junctions and
    // wires are net markers and get rebuilt; parts and their nets are kept.
    juce::String autoLayoutInstances(juce::Array<int> scope)
    {
        if (instances.empty())
            return "{ \"ok\": false, \"error\": \"No schematic components to lay out.\" }";

        std::set<int> scopeSet;
        for (int index : scope)
            if (index >= 0 && index < (int)instances.size())
                scopeSet.insert(index);
        int onThisSheet = 0;
        for (int i = 0; i < (int)instances.size(); ++i)
            if (onSheet(i)) ++onThisSheet;
        const bool wholeDiagram = scopeSet.empty() || (int)scopeSet.size() >= onThisSheet;

        const auto netNames = computeNetNames();
        const auto layoutNets = classifyLayoutNets(netNames);

        std::vector<int> partInstance;
        std::vector<schematic::layout::Part> parts;
        for (int i = 0; i < (int)instances.size(); ++i)
        {
            const auto& instance = instances[(size_t)i];
            if (instance.sheet != currentSheet || isNetMarker(instance.symbolId) || (!wholeDiagram && scopeSet.count(i) == 0))
                continue;
            schematic::layout::Part part;
            part.refdes = instance.refdes;
            part.symbol = symbolForInstance(instance);
            part.originalPosition = instance.position;
            if (instance.symbolId == "block_port")
            {
                // Inputs enter on the left, outputs leave on the right.
                const auto output = schematic::normalizedRotation(instance.rotation) == 180;
                part.pinnedColumn = output ? 1 : -1;
                part.fixedRotation = output ? 180 : 0;
            }
            else if (instance.symbolId == "audio_in")
            {
                part.pinnedColumn = -1;
            }
            else if (instance.symbolId == "audio_out")
            {
                part.pinnedColumn = 1;
            }
            for (int p = 0; p < (int)part.symbol.pins.size(); ++p)
            {
                const auto found = layoutNets.indexOf.find(netFor({ i, p }, netNames));
                part.pinNets.push_back(found != layoutNets.indexOf.end() ? found->second : -1);
            }
            partInstance.push_back(i);
            parts.push_back(std::move(part));
        }
        if (parts.empty())
            return "{ \"ok\": false, \"error\": \"No schematic parts to lay out.\" }";

        const auto placement = schematic::layout::layoutSchematic(parts, layoutNets.nets, schematic::gridSize, layoutOptions());

        if (!wholeDiagram)
        {
            // Selection: move the chosen parts as a block into the area they
            // came from; wiring stays as it is and is rerouted.
            juce::Point<float> before { std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
            juce::Point<float> after = before;
            for (size_t k = 0; k < parts.size(); ++k)
            {
                const auto oldPoint = instances[(size_t)partInstance[k]].position;
                const auto newPoint = placement.positions[k];
                before = { std::min(before.x, oldPoint.x), std::min(before.y, oldPoint.y) };
                after = { std::min(after.x, newPoint.x), std::min(after.y, newPoint.y) };
            }
            const auto delta = snapToGrid(before - after);
            for (size_t k = 0; k < parts.size(); ++k)
            {
                auto& instance = instances[(size_t)partInstance[k]];
                instance.position = placement.positions[k] + delta;
                instance.rotation = placement.rotations[k];
            }
            notifySelection();
            forceDeferredRepaint();
            if (onStatus) onStatus("Auto-laid out " + juce::String((int)parts.size()) + " selected component(s).");
            return "{ \"ok\": true, \"tool\": \"schematic_auto_layout\", \"displayTool\": \"schematic.auto_layout\", \"scope\": \"selection\", \"componentCount\": "
                + juce::String((int)parts.size()) + ", \"style\": \"layered_signal_flow\" }";
        }

        // Probes on this sheet's junctions move to a part pin on the same
        // net, since this sheet's junctions are rebuilt.
        for (auto& probe : probes)
        {
            if (!probe.node.isJunction() || !nodeOnSheet(probe.node))
                continue;
            const auto net = netForNode(probe.node, netNames);
            for (size_t k = 0; k < partInstance.size() && probe.node.isJunction(); ++k)
                for (int p = 0; p < (int)parts[k].symbol.pins.size(); ++p)
                    if (netFor({ partInstance[k], p }, netNames) == net)
                    {
                        probe.node = WireNode::forPin({ partInstance[k], p });
                        break;
                    }
        }

        for (size_t k = 0; k < parts.size(); ++k)
        {
            instances[(size_t)partInstance[k]].position = placement.positions[k];
            instances[(size_t)partInstance[k]].rotation = placement.rotations[k];
        }

        // With "use symbols for rails" off, rails placed on this sheet come
        // back as rails after layout (re-tapped to the pins they feed).
        juce::StringArray keptPowerRails;
        bool keptGroundRail = false;
        if (!prefs::isOn("layout.supply_symbols"))
            for (const auto& instance : instances)
                if (instance.sheet == currentSheet)
                {
                    if (instance.symbolId == "power_bus") keptPowerRails.addIfNotAlreadyThere(instance.busName);
                    if (instance.symbolId == "ground_bus") keptGroundRail = true;
                }

        // Net labels the user named survive: each comes back beside a pin of
        // its net (instrument probe labels are the layout's own and are rebuilt).
        struct KeptLabel { juce::String name, net; int partSlot = -1, pin = -1; };
        std::vector<KeptLabel> keptLabels;
        {
            std::set<juce::String> instrumentRefdes;
            for (const auto& instance : instances)
                if (schematic::isInstrumentSymbol(instance.symbolId))
                    instrumentRefdes.insert(instance.refdes);
            std::set<juce::String> seen;
            for (int i = 0; i < (int)instances.size(); ++i)
            {
                const auto& instance = instances[(size_t)i];
                const auto name = instance.busName.trim();
                if (instance.sheet != currentSheet || instance.symbolId != "net_label" || name.isEmpty() || seen.count(name) != 0)
                    continue;
                if (name.containsChar('.') && instrumentRefdes.count(name.upToFirstOccurrenceOf(".", false, false)) != 0)
                    continue;
                KeptLabel kept { name, netFor({ i, 0 }, netNames) };
                for (size_t k = 0; k < parts.size() && kept.partSlot < 0; ++k)
                {
                    if (schematic::isInstrumentSymbol(parts[k].symbol.id))
                        continue;
                    for (int p = 0; p < (int)parts[k].symbol.pins.size(); ++p)
                        if (netFor({ partInstance[k], p }, netNames) == kept.net)
                        {
                            kept.partSlot = (int)k;
                            kept.pin = p;
                            break;
                        }
                }
                if (kept.partSlot >= 0)
                {
                    seen.insert(name);
                    keptLabels.push_back(kept);
                }
            }
        }

        // This sheet's net markers, wires and junctions are rebuilt; every
        // other sheet is left exactly as it is.
        std::set<int> deadInstances, deadJunctions;
        for (int i = 0; i < (int)instances.size(); ++i)
            if (instances[(size_t)i].sheet == currentSheet && isNetMarker(instances[(size_t)i].symbolId))
                deadInstances.insert(i);
        for (int j = 0; j < (int)junctions.size(); ++j)
            if (junctionSheet(j) == currentSheet)
                deadJunctions.insert(j);
        wires.erase(std::remove_if(wires.begin(), wires.end(), [this](const Wire& w) { return wireOnSheet(w); }), wires.end());
        juce::StringArray droppedProbes;
        const auto oldToNew = removeInstancesAndJunctions(deadInstances, deadJunctions, droppedProbes);
        std::vector<int> partIndex(parts.size());
        for (size_t k = 0; k < parts.size(); ++k)
            partIndex[k] = oldToNew[(size_t)partInstance[k]];

        std::map<juce::String, int> nextNumber;
        for (const auto& instance : instances)
        {
            const auto prefix = schematic::refdesPrefixFor(instance.symbolId);
            if (isNetMarker(instance.symbolId) && instance.refdes.startsWith(prefix)
                && instance.refdes.substring(prefix.length()).containsOnly("0123456789"))
                nextNumber[prefix] = std::max(nextNumber[prefix], instance.refdes.substring(prefix.length()).getIntValue());
        }
        std::vector<std::pair<int, PinRef>> markerWires;
        std::map<int, std::vector<int>> labelsOnNet; // layout net -> label instance index
        for (const auto& marker : placement.markers)
        {
            Instance instance;
            instance.symbolId = marker.symbolId;
            const auto prefix = schematic::refdesPrefixFor(marker.symbolId);
            instance.refdes = prefix + juce::String(++nextNumber[prefix]);
            instance.value = marker.symbolId == "ground" ? juce::String("0") : juce::String();
            instance.busName = marker.symbolId == "ground" ? defaultBusNameFor("ground") : marker.netName;
            if (marker.symbolId == "net_label")
                instance.value = marker.netName;
            instance.family = familyFor(marker.symbolId);
            instance.position = marker.position;
            instance.rotation = marker.rotation;
            instance.busLength = 0.0f;
            instance.sheet = currentSheet;
            if (marker.onNet)
                labelsOnNet[marker.net].push_back((int)instances.size());
            else
                markerWires.push_back({ (int)instances.size(), PinRef { partIndex[(size_t)marker.part], marker.pin } });
            instances.push_back(instance);
        }

        for (const auto& [markerIndex, pin] : markerWires)
            wires.push_back({ WireNode::forPin({ markerIndex, 0 }), WireNode::forPin(pin) });

        for (const auto& kept : keptLabels)
        {
            const PinRef pin { partIndex[(size_t)kept.partSlot], kept.pin };
            auto direction = nodeLeadDirection(WireNode::forPin(pin));
            if (direction.getDistanceFromOrigin() < 0.5f)
                direction = { 1.0f, 0.0f };
            Instance label;
            label.symbolId = "net_label";
            const auto prefix = schematic::refdesPrefixFor("net_label");
            label.refdes = prefix + juce::String(++nextNumber[prefix]);
            label.busName = kept.name;
            label.value = kept.name;
            label.family = familyFor("net_label");
            label.position = snapToGrid(pinPosition(pin) + direction * 96.0f + juce::Point<float>(direction.y, -direction.x) * 48.0f);
            label.rotation = direction.x < -0.5f ? 180 : 0;
            label.busLength = 0.0f;
            label.sheet = currentSheet;
            const auto netIndex = layoutNets.indexOf.find(kept.net);
            if (netIndex != layoutNets.indexOf.end() && layoutNets.nets[(size_t)netIndex->second].kind == schematic::layout::NetKind::Signal)
                labelsOnNet[netIndex->second].push_back((int)instances.size());
            else
                wires.push_back({ WireNode::forPin({ (int)instances.size(), 0 }), WireNode::forPin(pin) });
            instances.push_back(label);
        }

        // Signal nets: libavoid picks each net's tree and its junctions.
        std::vector<int> obstacleInstance;
        const auto obstacles = buildRoutingObstacles(obstacleInstance);
        std::vector<int> instanceObstacle(instances.size(), -1);
        for (int o = 0; o < (int)obstacleInstance.size(); ++o)
            instanceObstacle[(size_t)obstacleInstance[(size_t)o]] = o;

        std::vector<schematic::routing::NetTerminals> signalNets;
        for (int net = 0; net < (int)layoutNets.nets.size(); ++net)
        {
            if (layoutNets.nets[(size_t)net].kind != schematic::layout::NetKind::Signal)
                continue;
            schematic::routing::NetTerminals terminals;
            for (size_t k = 0; k < parts.size(); ++k)
            {
                if (schematic::isInstrumentSymbol(parts[k].symbol.id) && prefs::isOn("layout.instrument_labels"))
                    continue; // instruments connect through their probe labels
                const auto obstacle = instanceObstacle[(size_t)partIndex[k]];
                for (int p = 0; p < (int)parts[k].pinNets.size(); ++p)
                    if (parts[k].pinNets[(size_t)p] == net && obstacle >= 0)
                        terminals.terminals.push_back(schematic::routing::Endpoint::forPin(obstacle, p));
            }
            for (int label : labelsOnNet[net])
                if (instanceObstacle[(size_t)label] >= 0)
                    terminals.terminals.push_back(schematic::routing::Endpoint::forPin(instanceObstacle[(size_t)label], 0));
            if (terminals.terminals.size() >= 2)
                signalNets.push_back(terminals);
        }

        const auto trees = schematic::routing::routeNetTrees(obstacles, signalNets, schematic::gridSize, routingStyle());
        for (const auto& tree : trees)
        {
            const auto base = (int)junctions.size();
            for (const auto& j : tree.junctions)
                addJunction(j);
            auto toNode = [&](const schematic::routing::Endpoint& e) {
                return e.isJunction() ? WireNode::forJunction(base + e.junction)
                                      : WireNode::forPin({ obstacleInstance[(size_t)e.obstacle], e.pin });
            };
            for (const auto& edge : tree.edges)
                wires.push_back({ toNode(edge.a), toNode(edge.b) });
        }

        for (const auto& railName : keptPowerRails)
            for (int i = 0; i < (int)instances.size(); ++i)
                if (instances[(size_t)i].sheet == currentSheet && instances[(size_t)i].symbolId == "power_port" && instances[(size_t)i].busName == railName)
                {
                    juce::String error;
                    symbolsToRail(i, error);
                    break;
                }
        if (keptGroundRail)
            for (int i = 0; i < (int)instances.size(); ++i)
                if (instances[(size_t)i].sheet == currentSheet && instances[(size_t)i].symbolId == "ground")
                {
                    juce::String error;
                    symbolsToRail(i, error);
                    break;
                }

        // One full routing pass slides each junction onto the T its wires
        // actually form; keep those positions in the model.
        {
            routeSignature.clear();
            ensureRoutes();
            if (!routedJunctions.empty() && routedJunctions.size() == junctions.size())
                junctions = routedJunctions;
        }

        selectedInstance = -1;
        selectedInstances.clear();
        selectedGroup = -1;
        notifySelection();
        for (const auto& probeId : droppedProbes)
            if (onProbeChanged) onProbeChanged(probeId, {}, {});
        forceDeferredRepaint();

        juce::String result;
        result << "{\n";
        result << "  \"ok\": true,\n";
        result << "  \"tool\": \"schematic_auto_layout\",\n";
        result << "  \"displayTool\": \"schematic.auto_layout\",\n";
        result << "  \"scope\": \"diagram\",\n";
        result << "  \"componentCount\": " << (int)parts.size() << ",\n";
        result << "  \"powerSymbols\": " << (int)placement.markers.size() << ",\n";
        result << "  \"wireCount\": " << (int)wires.size() << ",\n";
        result << "  \"junctionCount\": " << (int)junctions.size() << ",\n";
        result << "  \"style\": \"layered_signal_flow_libavoid_routed\"\n";
        result << "}";
        if (onStatus) onStatus("Auto-laid out " + juce::String((int)parts.size()) + " component(s) with "
                               + juce::String((int)placement.markers.size()) + " power symbol(s).");
        return result;
    }

    static juce::Point<float> snapToGrid(juce::Point<float> p)
    {
        return { std::round(p.x / schematic::gridSize) * schematic::gridSize,
                 std::round(p.y / schematic::gridSize) * schematic::gridSize };
    }

    // Every non-rail instance as a routing obstacle: its full footprint
    // (body plus pin ends) with a connection pin at each pin end.
    std::vector<schematic::routing::Obstacle> buildRoutingObstacles(std::vector<int>& obstacleInstance) const
    {
        std::vector<schematic::routing::Obstacle> obstacles;
        obstacleInstance.clear();
        for (int i = 0; i < (int)instances.size(); ++i)
        {
            const auto& instance = instances[(size_t)i];
            if (isRailBus(instance.symbolId) || instance.sheet != currentSheet)
                continue;
            const auto symbol = symbolForInstance(instance);
            schematic::routing::Obstacle obstacle;
            obstacle.bounds = schematic::rotateBounds(schematic::extentBounds(symbol), instance.rotation)
                                  .translated(instance.position.x, instance.position.y)
                                  .expanded(schematic::gridSize * 0.60f);
            for (int p = 0; p < (int)symbol.pins.size(); ++p)
                obstacle.pins.push_back({ pinPosition({ i, p }),
                                          rotateOffset(schematic::pinLeadDirection(symbol, p), instance.rotation) });
            obstacles.push_back(std::move(obstacle));
            obstacleInstance.push_back(i);
        }
        return obstacles;
    }

    juce::String connectNodesFromTool(const juce::String& firstLabel, const juce::String& secondLabel)
    {
        WireNode first;
        WireNode second;
        juce::String error;
        if (!nodeFromLabel(firstLabel, first, error))
            return toolFailure("schematic_connect", error);
        if (!nodeFromLabel(secondLabel, second, error))
            return toolFailure("schematic_connect", error);
        if (sameNode(first, second))
            return toolFailure("schematic_connect", "Cannot connect a node to itself: " + nodeLabel(first) + ".");
        if (nodeSheet(first) != nodeSheet(second))
            return toolFailure("schematic_connect", nodeLabel(first) + " and " + nodeLabel(second)
                + " are on different sheets. Connect through the sub-diagram block's pins and port bubbles instead.");

        const auto alreadyConnected = std::any_of(wires.begin(), wires.end(), [&](const Wire& wire) {
            return (sameNode(wire.a, first) && sameNode(wire.b, second))
                || (sameNode(wire.a, second) && sameNode(wire.b, first));
        });
        if (!alreadyConnected)
            wires.push_back({ first, second });

        const auto firstResolved = nodeLabel(first);
        const auto secondResolved = nodeLabel(second);
        if (onStatus)
            onStatus(alreadyConnected
                ? "Connection already exists: " + firstResolved + " to " + secondResolved + "."
                : "Connected " + firstResolved + " to " + secondResolved + ".");
        forceDeferredRepaint();

        juce::String result;
        result << "{\n";
        result << "  \"ok\": true,\n";
        result << "  \"tool\": \"schematic_connect\",\n";
        result << "  \"displayTool\": \"schematic.connect\",\n";
        result << "  \"alreadyConnected\": " << (alreadyConnected ? "true" : "false") << ",\n";
        result << "  \"a\": " << quote(firstResolved) << ",\n";
        result << "  \"b\": " << quote(secondResolved) << ",\n";
        result << "  \"wireCount\": " << (int)wires.size() << "\n";
        result << "}";
        return result;
    }

    juce::String openInstrumentFromTool(const juce::String& refdes)
    {
        const auto index = instanceIndexForRefdes(refdes);
        if (index < 0)
            return toolFailure("instrument_open_panel", "No placed instance has reference designator " + refdes + ".");

        const auto& instance = instances[(size_t)index];
        if (!isInstrumentNode(instance.symbolId))
            return toolFailure("instrument_open_panel", instance.refdes + " is not an instrument node.");
        const auto symbol = symbolForInstance(instance);
        int connectedPins = 0;
        for (int pin = 0; pin < (int)symbol.pins.size(); ++pin)
            if (wireCountAtPin({ index, pin }) > 0)
                ++connectedPins;
        if (connectedPins == 0)
            return toolFailure("instrument_open_panel", instance.refdes + " is an unconnected instrument node. Wire the instrument on the schematic first; users open panels by double-clicking placed instruments.");
        if (!onInstrumentOpen)
            return toolFailure("instrument_open_panel", "Instrument window host is unavailable.");

        onInstrumentOpen(instance.refdes, instance.symbolId);
        if (onStatus)
            onStatus("Opened instrument panel for " + instance.refdes + ".");

        juce::String result;
        result << "{\n";
        result << "  \"ok\": true,\n";
        result << "  \"tool\": \"instrument_open_panel\",\n";
        result << "  \"displayTool\": \"instrument.open_panel\",\n";
        result << "  \"refdes\": " << quote(instance.refdes) << ",\n";
        result << "  \"symbolId\": " << quote(instance.symbolId) << ",\n";
        result << "  \"connectedPins\": " << connectedPins << "\n";
        result << "}";
        return result;
    }

    std::unordered_map<std::string, double> getLiveParams() const
    {
        std::unordered_map<std::string, double> params;
        for (const auto& inst : instances)
        {
            if (inst.symbolId == "potentiometer")
            {
                params[inst.refdes.toStdString() + "_position"] = juce::jlimit(0.0, 1.0, partValue(inst, "position").getDoubleValue());
            }
            else if (inst.symbolId == "switch_spst" || inst.symbolId == "relay_spst")
            {
                params[inst.refdes.toStdString() + "_state"] = (partValue(inst, "state") == "Closed" ? 1.0 : 0.0);
            }
            else if (inst.symbolId == "switch_spdt")
            {
                params[inst.refdes.toStdString() + "_state"] = (partValue(inst, "state") == "B" ? 1.0 : 0.0);
            }
        }
        return params;
    }

private:
    using PinDef = schematic::PinDef;
    using SymbolDef = schematic::SymbolDef;

    struct Instance
    {
        juce::String symbolId;
        juce::String refdes;
        juce::String value;
        juce::String frequency;
        juce::String busName;
        juce::String family;
        juce::String manufacturerPart;
        juce::Point<float> position;
        int rotation = 0;
        float busLength = 420.0f;
        juce::String sheet;                         // "" = top level, else a sub-diagram sheet id
        juce::String childSheet;                    // sub_block: the sheet it opens
        std::vector<schematic::BlockPort> ports;    // sub_block: its ports, pin index == port index
        std::map<juce::String, juce::String> params; // part properties beyond the fixed fields (PartCatalog)
    };

    struct PinRef
    {
        int instanceIndex = -1;
        int pinIndex = -1;
    };

    struct WireNode
    {
        PinRef pin;
        int junctionIndex = -1;

        static WireNode forPin(PinRef pinRef)
        {
            WireNode node;
            node.pin = pinRef;
            return node;
        }

        static WireNode forJunction(int index)
        {
            WireNode node;
            node.junctionIndex = index;
            return node;
        }

        bool isPin() const { return pin.instanceIndex >= 0 && pin.pinIndex >= 0; }
        bool isJunction() const { return junctionIndex >= 0; }
        bool isValid() const { return isPin() || isJunction(); }
    };

    struct Wire
    {
        WireNode a;
        WireNode b;
    };

    struct Probe
    {
        juce::String id;
        juce::String label;
        juce::String role;
        WireNode node;
        juce::Colour colour;
    };

    struct Group
    {
        juce::String id;
        juce::String name;
        juce::String category { "user_group" };
        juce::String notes;
        std::vector<int> memberInstances;
        juce::Colour colour { 0xff78dcca };
    };

    struct SimulationParameter
    {
        juce::String id;
        juce::String refdes;
        juce::String property;
        juce::String label;
        juce::String unit;
        juce::String defaultValue;
        juce::String minValue;
        juce::String maxValue;
        juce::String scaling { "linear" };
        juce::String control { "slider" };
    };

    struct DisjointSet
    {
        std::vector<int> parent;

        explicit DisjointSet(int count)
        {
            parent.resize((size_t)count);
            for (int i = 0; i < count; ++i) parent[(size_t)i] = i;
        }

        int find(int x)
        {
            auto& p = parent[(size_t)x];
            if (p == x) return x;
            p = find(p);
            return p;
        }

        void unite(int a, int b)
        {
            const auto ra = find(a);
            const auto rb = find(b);
            if (ra != rb) parent[(size_t)rb] = ra;
        }
    };

    std::vector<Instance> instances;
    std::vector<Wire> wires;
    std::vector<juce::Point<float>> junctions;
    std::vector<juce::String> junctionSheets;       // parallel to junctions
    int preferenceListener = 0;
    juce::String currentSheet;                      // sheet shown in the canvas
    std::vector<Probe> probes;
    std::vector<Group> groups;
    std::vector<SimulationParameter> simulationParameters;
    std::unique_ptr<juce::FileChooser> partsListChooser;
    WireNode wireDragStart;
    int selectedInstance = -1;
    juce::Array<int> selectedInstances;
    int selectedGroup = -1;
    bool dragHover = false;
    juce::String dragMessage = "Drop symbol on schematic";
    bool wireDragging = false;
    bool draggingInstance = false;
    bool selectingBox = false;
    bool resizingRail = false;
    bool resizingLeftRailEnd = false;
    bool snapEnabled = true;
    bool panning = false;
    int resizingRailInstance = -1;
    float fixedRailEndX = 0.0f;
    float canvasZoom = 1.0f;
    juce::Point<float> viewOffset { 0.0f, 0.0f };
    juce::Point<float> panStartMouse;
    juce::Point<float> selectionBoxStart;
    juce::Point<float> selectionBoxEnd;
    std::vector<juce::Point<float>> selectedDragStartPositions;
    juce::Point<float> panStartOffset;
    juce::Point<float> dragStartMouse;
    juce::Point<float> dragStartPosition;
    juce::Point<float> wireDragPosition;
    juce::StringArray undoStack;
    juce::StringArray redoStack;
    bool dragSnapshotTaken = false;
    bool selectionBoxAppend = false;
    std::function<juce::String()> getSelectedSymbolId;
    std::function<bool()> getStampPlacementEnabled;
    std::function<void(juce::String)> onStatus;
    std::function<void(juce::String, juce::String, juce::String)> onProbeChanged;
    std::function<void(juce::String, juce::String)> onInstrumentOpen;
    std::function<void(int, juce::String, juce::String, juce::String, juce::String, juce::String, juce::String, juce::String)> onSelectionChanged;

    static juce::String quote(const juce::String& text)
    {
        return "\"" + text.replace("\\", "\\\\").replace("\"", "\\\"") + "\"";
    }

    static juce::String toolFailure(const juce::String& toolName, const juce::String& message)
    {
        return "{ \"ok\": false, \"tool\": " + quote(toolName)
            + ", \"error\": " + quote(message) + " }";
    }

    void forceDeferredRepaint()
    {
        repaint();
        juce::Component::SafePointer<juce::Component> safeThis(this);
        juce::MessageManager::callAsync([safeThis] {
            if (safeThis == nullptr)
                return;

            safeThis->repaint();
            if (auto* topLevel = safeThis->getTopLevelComponent())
                topLevel->repaint();
        });
    }

    static juce::String nullableQuote(const juce::String& text)
    {
        return text.isEmpty() ? juce::String("null") : quote(text);
    }

    void pushUndoSnapshot()
    {
        const auto snapshot = buildCircuitJson();
        if (undoStack.isEmpty() || undoStack[undoStack.size() - 1] != snapshot)
        {
            undoStack.add(snapshot);
            while (undoStack.size() > 80)
                undoStack.remove(0);
        }
        redoStack.clear();
    }

    bool restoreSnapshot(const juce::String& snapshot)
    {
        juce::String error;
        if (!loadCircuitJson(snapshot, error))
        {
            if (onStatus) onStatus("Could not restore schematic: " + error);
            return false;
        }
        return true;
    }

    bool undoEdit()
    {
        if (undoStack.isEmpty())
        {
            if (onStatus) onStatus("Nothing to undo.");
            return true;
        }
        redoStack.add(buildCircuitJson());
        const auto snapshot = undoStack[undoStack.size() - 1];
        undoStack.remove(undoStack.size() - 1);
        if (restoreSnapshot(snapshot) && onStatus) onStatus("Undo.");
        return true;
    }

    bool redoEdit()
    {
        if (redoStack.isEmpty())
        {
            if (onStatus) onStatus("Nothing to redo.");
            return true;
        }
        undoStack.add(buildCircuitJson());
        const auto snapshot = redoStack[redoStack.size() - 1];
        redoStack.remove(redoStack.size() - 1);
        if (restoreSnapshot(snapshot) && onStatus) onStatus("Redo.");
        return true;
    }

    static juce::String safeFileStem(juce::String text)
    {
        text = text.trim();
        juce::String safe;
        for (int i = 0; i < text.length(); ++i)
        {
            const auto c = text[i];
            safe << (juce::CharacterFunctions::isLetterOrDigit(c) ? juce::String::charToString(c) : "_");
        }
        while (safe.contains("__"))
            safe = safe.replace("__", "_");
        safe = safe.trimCharactersAtStart("_").trimCharactersAtEnd("_");
        return safe.isNotEmpty() ? safe : juce::String("block");
    }

    static juce::File userBlockLibraryFolder()
    {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("DjehutiElectronicsLab")
            .getChildFile("user_library")
            .getChildFile("blocks");
    }

    static juce::String stringProperty(const juce::DynamicObject& object,
                                       const char* name,
                                       const juce::String& fallback)
    {
        const auto property = juce::Identifier(name);
        if (!object.hasProperty(property))
            return fallback;

        const auto value = object.getProperty(property);
        if (value.isVoid() || value.isUndefined())
            return fallback;

        return value.toString();
    }

    static float floatProperty(const juce::DynamicObject& object, const char* name, float fallback)
    {
        const auto property = juce::Identifier(name);
        if (!object.hasProperty(property))
            return fallback;

        const auto value = object.getProperty(property);
        if (value.isVoid() || value.isUndefined())
            return fallback;

        return (float)(double)value;
    }

    void clearModel()
    {
        const auto previousProbes = probes;
        instances.clear();
        wires.clear();
        junctions.clear();
        junctionSheets.clear();
        currentSheet = {};
        probes.clear();
        groups.clear();
        simulationParameters.clear();
        selectedInstance = -1;
        selectedInstances.clear();
        selectedGroup = -1;
        wireDragging = false;
        draggingInstance = false;
        resizingRail = false;
        resizingRailInstance = -1;
        notifySelection();

        if (onProbeChanged)
            for (const auto& probe : previousProbes)
                onProbeChanged(probe.id, probe.label, {});

        repaint();
    }

    void drawGrid(juce::Graphics& g)
    {
        const auto topLeft = viewToCanvas({ 0.0f, 0.0f });
        const auto bottomRight = viewToCanvas({ (float)getWidth(), (float)getHeight() });
        const auto startX = (int)std::floor(topLeft.x / 24.0f) * 24 - 24;
        const auto endX = (int)std::ceil(bottomRight.x / 24.0f) * 24 + 24;
        const auto startY = (int)std::floor(topLeft.y / 24.0f) * 24 - 24;
        const auto endY = (int)std::ceil(bottomRight.y / 24.0f) * 24 + 24;
        const auto style = prefs::get("display.grid");
        if (style == "Hidden")
            return;
        if (style == "Dots")
        {
            g.setColour(juce::Colour(0xff2a3742));
            for (int x = startX; x <= endX; x += 24)
                for (int y = startY; y <= endY; y += 24)
                    g.fillRect((float)x - 0.75f, (float)y - 0.75f, 1.5f, 1.5f);
            return;
        }
        g.setColour(juce::Colour(0xff18222b));
        for (int x = startX; x <= endX; x += 24) g.drawVerticalLine(x, (float)startY, (float)endY);
        for (int y = startY; y <= endY; y += 24) g.drawHorizontalLine(y, (float)startX, (float)endX);
        g.setColour(juce::Colour(0xff26323d));
        for (int x = ((startX / 120) - 1) * 120; x <= endX; x += 120) g.drawVerticalLine(x, (float)startY, (float)endY);
        for (int y = ((startY / 120) - 1) * 120; y <= endY; y += 120) g.drawHorizontalLine(y, (float)startX, (float)endX);
    }

    static juce::Point<float> snap(juce::Point<float> p)
    {
        return { std::round(p.x / 24.0f) * 24.0f, std::round(p.y / 24.0f) * 24.0f };
    }

    juce::Point<float> snapPoint(juce::Point<float> p) const
    {
        return snapEnabled ? snap(p) : p;
    }

    juce::Point<float> viewToCanvas(juce::Point<float> p) const
    {
        const auto scale = std::max(0.001f, canvasZoom);
        return { (p.x - viewOffset.x) / scale, (p.y - viewOffset.y) / scale };
    }

    juce::Point<float> canvasToView(juce::Point<float> p) const
    {
        return { p.x * canvasZoom + viewOffset.x, p.y * canvasZoom + viewOffset.y };
    }

    void beginPan(juce::Point<float> position)
    {
        panning = true;
        draggingInstance = false;
        wireDragging = false;
        resizingRail = false;
        panStartMouse = position;
        panStartOffset = viewOffset;
        if (onStatus) onStatus("Pan schematic canvas.");
    }

    float hitDistance(float modelPixels) const
    {
        return modelPixels / std::max(0.001f, canvasZoom);
    }

    static bool isRailBus(const juce::String& symbolId)
    {
        return schematic::isRailBus(symbolId);
    }

    static bool isInstrumentNode(const juce::String& symbolId)
    {
        return schematic::isInstrumentSymbol(symbolId);
    }

    SymbolDef symbolForInstance(const Instance& instance) const
    {
        if (instance.symbolId == "sub_block")
            return schematic::blockSymbol(instance.ports);
        return symbolFor(instance.symbolId);
    }

    SymbolDef symbolFor(const juce::String& id) const
    {
        // Unknown ids yield a pinless invalid def; placement and loading
        // reject unknown symbols before they reach the model.
        return schematic::symbolFor(id);
    }

    juce::String defaultValueFor(const juce::String& symbolId) const
    {
        if (symbolId == "resistor") return "10k";
        if (symbolId == "potentiometer") return "10k";
        if (symbolId == "capacitor") return "1u";
        if (symbolId == "capacitor_polarized") return "10u";
        if (symbolId == "variable_capacitor") return "100p";
        if (symbolId == "inductor") return "10m";
        if (symbolId == "coupled_inductor") return "10m";
        if (symbolId == "transformer") return "1:1";
        if (symbolId == "diode") return "1N4148";
        if (symbolId == "zener_diode") return "5V1";
        if (symbolId == "led") return "LED";
        if (symbolId == "schottky_diode") return "BAT54";
        if (symbolId == "power_bus") return "+V";
        if (symbolId == "ground_bus") return "0";
        if (symbolId == "battery") return "9";
        if (symbolId == "voltage_source") return "10";
        if (symbolId == "ac_voltage_source") return "1";
        if (symbolId == "current_source") return "1m";
        if (symbolId == "ac_current_source") return "1m";
        if (symbolId == "behavioral_voltage_source") return "V(CTRL)";
        if (symbolId == "behavioral_current_source") return "V(CTRL)/1k";
        if (symbolId == "vcvs" || symbolId == "vccs" || symbolId == "ccvs" || symbolId == "cccs") return "1";
        if (symbolId == "signal_source") return "1";
        if (symbolId == "opamp_generic") return "generic_opamp";
        if (symbolId == "opamp_741") return "uA741";
        if (symbolId == "comparator_generic") return "generic_comparator";
        if (symbolId == "comparator_lm311") return "LM311";
        if (symbolId == "npn") return "generic_npn";
        if (symbolId == "pnp") return "generic_pnp";
        if (symbolId == "nmos") return "generic_nmos";
        if (symbolId == "pmos") return "generic_pmos";
        if (symbolId == "njfet") return "generic_njfet";
        if (symbolId == "pjfet") return "generic_pjfet";
        if (symbolId == "fuse") return "1A";
        if (symbolId == "oscilloscope_2ch") return "2ch";
        if (symbolId == "digital_multimeter") return "DC V";
        if (symbolId == "annotation_text") return "Note";
        return "";
    }

    juce::String archetypeFor(const juce::String& symbolId) const
    {
        if (symbolId == "resistor") return "passive.resistor";
        if (symbolId == "potentiometer") return "passive.potentiometer";
        if (symbolId == "capacitor") return "passive.capacitor";
        if (symbolId == "capacitor_polarized") return "passive.capacitor.polarized";
        if (symbolId == "variable_capacitor") return "passive.capacitor.variable";
        if (symbolId == "inductor") return "passive.inductor";
        if (symbolId == "coupled_inductor") return "magnetics.coupled_inductor";
        if (symbolId == "transformer") return "magnetics.transformer";
        if (symbolId == "diode") return "discrete.diode";
        if (symbolId == "zener_diode") return "discrete.diode.zener";
        if (symbolId == "led") return "discrete.diode.led";
        if (symbolId == "schottky_diode") return "discrete.diode.schottky";
        if (symbolId == "power_bus") return "net.power_bus";
        if (symbolId == "ground" || symbolId == "ground_bus") return "net.ground_reference";
        if (symbolId == "battery") return "source.battery";
        if (symbolId == "voltage_source") return "source.dc_voltage";
        if (symbolId == "ac_voltage_source") return "source.ac_voltage";
        if (symbolId == "current_source") return "source.dc_current";
        if (symbolId == "ac_current_source") return "source.ac_current";
        if (symbolId == "behavioral_voltage_source") return "source.behavioral_voltage";
        if (symbolId == "behavioral_current_source") return "source.behavioral_current";
        if (symbolId == "vcvs") return "source.controlled.vcvs";
        if (symbolId == "vccs") return "source.controlled.vccs";
        if (symbolId == "ccvs") return "source.controlled.ccvs";
        if (symbolId == "cccs") return "source.controlled.cccs";
        if (symbolId == "signal_source") return "source.signal";
        if (symbolId == "opamp_generic" || symbolId == "opamp_741") return "analog.op_amp";
        if (symbolId == "comparator_generic" || symbolId == "comparator_lm311") return "analog.comparator";
        if (symbolId == "npn") return "discrete.bjt.npn";
        if (symbolId == "pnp") return "discrete.bjt.pnp";
        if (symbolId == "nmos") return "discrete.fet.nmos";
        if (symbolId == "pmos") return "discrete.fet.pmos";
        if (symbolId == "njfet") return "discrete.fet.njfet";
        if (symbolId == "pjfet") return "discrete.fet.pjfet";
        if (symbolId == "switch_spst") return "switch.spst";
        if (symbolId == "switch_spdt") return "switch.spdt";
        if (symbolId == "relay_spst") return "switch.relay.spst";
        if (symbolId == "fuse") return "protection.fuse";
        if (symbolId == "connector_2") return "connector.2pin";
        if (symbolId == "connector_3") return "connector.3pin";
        if (symbolId == "test_point") return "connector.test_point";
        if (symbolId == "logic_not") return "digital.logic.not";
        if (symbolId == "logic_and") return "digital.logic.and";
        if (symbolId == "logic_or") return "digital.logic.or";
        if (symbolId == "logic_nand") return "digital.logic.nand";
        if (symbolId == "logic_nor") return "digital.logic.nor";
        if (symbolId == "logic_xor") return "digital.logic.xor";
        if (symbolId == "oscilloscope_2ch") return "instrument.oscilloscope";
        if (symbolId == "digital_multimeter") return "instrument.multimeter";
        if (symbolId == "annotation_text") return "documentation.annotation";
        return "unknown";
    }

    juce::String familyFor(const juce::String& symbolId) const
    {
        if (symbolId == "opamp_generic") return "generic_opamp";
        if (symbolId == "opamp_741") return "741";
        if (symbolId == "comparator_generic") return "generic_comparator";
        if (symbolId == "comparator_lm311") return "LM311";
        if (symbolId == "npn") return "generic_npn";
        if (symbolId == "pnp") return "generic_pnp";
        if (symbolId == "nmos") return "generic_nmos";
        if (symbolId == "pmos") return "generic_pmos";
        if (symbolId == "njfet") return "generic_njfet";
        if (symbolId == "pjfet") return "generic_pjfet";
        return "";
    }

    juce::String defaultFrequencyFor(const juce::String& symbolId) const
    {
        if (symbolId == "ac_voltage_source" || symbolId == "signal_source") return "1k";
        return "";
    }

    juce::String defaultBusNameFor(const juce::String& symbolId) const
    {
        if (symbolId == "power_bus") return "+V";
        if (symbolId == "power_port") return "+5V";
        if (symbolId == "net_label") return "NET1";
        if (symbolId == "ground" || symbolId == "ground_bus") return "0";
        return "";
    }

    juce::String parametersJsonFor(const Instance& instance) const
    {
        const auto& symbolId = instance.symbolId;
        if (symbolId == "sub_block")
        {
            juce::String text = "{";
            bool first = true;
            for (const auto& [key, target] : instance.params)
            {
                if (!key.startsWith("paramTarget."))
                    continue;
                const auto paramKey = key.fromFirstOccurrenceOf("paramTarget.", false, false);
                const auto value = instance.params.count(paramKey) != 0 ? instance.params.at(paramKey) : juce::String();
                text << (first ? " " : ", ") << quote(paramKey) << ": { \"value\": " << quote(value) << " }";
                first = false;
            }
            text << (first ? "" : " ") << "}";
            return text;
        }
        if (symbolId == "resistor")
            return "{ \"resistance\": { \"value\": " + quote(instance.value) + ", \"unit\": \"ohm\" } }";
        if (symbolId == "capacitor")
            return "{ \"capacitance\": { \"value\": " + quote(instance.value) + ", \"unit\": \"F\" } }";
        if (symbolId == "inductor")
            return "{ \"inductance\": { \"value\": " + quote(instance.value) + ", \"unit\": \"H\" } }";
        if (symbolId == "diode")
            return "{ \"model\": " + quote(instance.value) + " }";
        if (symbolId == "power_bus")
            return "{ \"name\": " + quote(instance.busName) + ", \"voltageHint\": null }";
        if (symbolId == "ground" || symbolId == "ground_bus")
            return "{ \"name\": " + quote(instance.busName) + ", \"voltageHint\": \"0V\" }";
        if (symbolId == "battery")
            return "{ \"voltage\": { \"value\": " + quote(instance.value) + ", \"unit\": \"V\" } }";
        if (symbolId == "voltage_source")
            return "{ \"dcVoltage\": { \"value\": " + quote(instance.value) + ", \"unit\": \"V\" } }";
        if (symbolId == "ac_voltage_source")
            return "{ \"amplitude\": { \"value\": " + quote(instance.value) + ", \"unit\": \"V\" }, \"frequency\": { \"value\": " + quote(instance.frequency) + ", \"unit\": \"Hz\" }, \"offset\": { \"value\": \"0\", \"unit\": \"V\" } }";
        if (symbolId == "signal_source")
            return "{ \"waveform\": \"sine\", \"amplitude\": { \"value\": " + quote(instance.value) + ", \"unit\": \"V\" }, \"frequency\": { \"value\": " + quote(instance.frequency) + ", \"unit\": \"Hz\" } }";
        if (symbolId == "oscilloscope_2ch")
            return "{ \"instrumentType\": \"digital_oscilloscope\", \"channels\": [\"CH1\", \"CH2\"], \"reference\": \"REF\", \"windowMode\": \"floating_preferred\" }";
        if (symbolId == "digital_multimeter")
            return "{ \"instrumentType\": \"digital_multimeter\", \"function\": " + quote(instance.value) + ", \"connections\": [\"HI\", \"LO\"], \"windowMode\": \"floating_preferred\" }";
        return "{}";
    }

    juce::String simulationFidelityFor(const Instance& instance) const
    {
        if (instance.symbolId == "opamp_generic" || instance.symbolId == "opamp_741"
            || instance.symbolId == "comparator_generic" || instance.symbolId == "comparator_lm311")
        {
            const auto selected = partValue(instance, "value");
            if (selected.equalsIgnoreCase("generic_opamp") || selected.equalsIgnoreCase("generic_comparator"))
                return "generic_model";

            static const bool initialized = [] {
                spice_library::initialize();
                return true;
            }();
            juce::ignoreUnused(initialized);
            if (const auto* def = spice_library::findModel(selected); def != nullptr && def->kind.equalsIgnoreCase("SUBCKT"))
                return "vendor_model";
            if (instance.symbolId == "opamp_741")
                for (const auto* name : { "UA741", "uA741", "LM741" })
                    if (const auto* def = spice_library::findModel(name); def != nullptr && def->kind.equalsIgnoreCase("SUBCKT"))
                        return "vendor_model";
            return "unsupported";
        }

        if (instance.symbolId == "npn" || instance.symbolId == "pnp"
            || instance.symbolId == "nmos" || instance.symbolId == "pmos"
            || instance.symbolId == "njfet" || instance.symbolId == "pjfet")
        {
            juce::String expectedKind;
            if (instance.symbolId == "npn") expectedKind = "NPN";
            else if (instance.symbolId == "pnp") expectedKind = "PNP";
            else if (instance.symbolId == "nmos") expectedKind = "NMOS";
            else if (instance.symbolId == "pmos") expectedKind = "PMOS";
            else if (instance.symbolId == "njfet") expectedKind = "NJF";
            else if (instance.symbolId == "pjfet") expectedKind = "PJF";

            const auto selected = partValue(instance, "value");
            if (selected.equalsIgnoreCase(familyFor(instance.symbolId)))
                return "generic_model";

            static const bool initialized = [] {
                spice_library::initialize();
                return true;
            }();
            juce::ignoreUnused(initialized);
            if (const auto* def = spice_library::findModel(selected); def != nullptr)
            {
                if (def->kind.equalsIgnoreCase(expectedKind))
                    return "vendor_model";
                if (instance.symbolId == "nmos" && selected.equalsIgnoreCase("Si4778DY") && def->kind.equalsIgnoreCase("SUBCKT"))
                    return "vendor_model";
            }
            return "unsupported";
        }
        return parts::simulationFidelity(instance.symbolId);
    }

    int pinOrdinal(const PinRef& pin) const
    {
        int ordinal = 0;
        for (int i = 0; i < pin.instanceIndex; ++i)
            ordinal += (int)symbolForInstance(instances[(size_t)i]).pins.size();
        return ordinal + pin.pinIndex;
    }

    int pinCount() const
    {
        int count = 0;
        for (const auto& instance : instances)
            count += (int)symbolForInstance(instance).pins.size();
        return count;
    }

    int nodeOrdinal(const WireNode& node) const
    {
        if (node.isPin())
            return pinOrdinal(node.pin);
        if (node.isJunction())
            return pinCount() + node.junctionIndex;
        return -1;
    }

    std::map<int, juce::String> computeNetNames() const
    {
        const auto totalPins = pinCount();
        const auto totalNodes = totalPins + (int)junctions.size();

        std::map<int, juce::String> result;
        if (totalNodes <= 0)
            return result;

        DisjointSet sets(totalNodes);
        for (const auto& wire : wires)
        {
            const auto a = nodeOrdinal(wire.a);
            const auto b = nodeOrdinal(wire.b);
            if (a < 0 || b < 0)
                continue;
            sets.unite(a, b);
        }

        // Named supply ports and power rails with the same name are one net
        // (SCH-P2), even with no wire between them.
        std::map<juce::String, int> namedSupplyPins;
        for (size_t i = 0; i < instances.size(); ++i)
        {
            const auto& instance = instances[i];
            if (instance.symbolId != "power_port" && instance.symbolId != "power_bus")
                continue;
            const auto name = instance.busName.trim();
            if (name.isEmpty())
                continue;
            const auto ordinal = pinOrdinal({ (int)i, 0 });
            const auto found = namedSupplyPins.find(name);
            if (found == namedSupplyPins.end())
                namedSupplyPins[name] = ordinal;
            else
                sets.unite(found->second, ordinal);
        }

        std::map<int, juce::String> supplyNetNames;
        for (const auto& [name, ordinal] : namedSupplyPins)
            supplyNetNames[sets.find(ordinal)] = spiceNetName(name);

        // A sub-diagram block pin and the port bubble of the same name inside
        // its sheet are one net.
        for (size_t i = 0; i < instances.size(); ++i)
        {
            const auto& block = instances[i];
            if (block.symbolId != "sub_block")
                continue;
            for (size_t k = 0; k < block.ports.size(); ++k)
                for (size_t j = 0; j < instances.size(); ++j)
                    if (instances[j].symbolId == "block_port" && instances[j].sheet == block.childSheet
                        && instances[j].busName == block.ports[k].name)
                        sets.unite(pinOrdinal({ (int)i, (int)k }), pinOrdinal({ (int)j, 0 }));
        }

        // Net labels with the same name are one net (probe connections).
        std::map<juce::String, int> labelPins;
        for (size_t i = 0; i < instances.size(); ++i)
        {
            const auto& instance = instances[i];
            if (instance.symbolId != "net_label" || instance.busName.trim().isEmpty())
                continue;
            const auto ordinal = pinOrdinal({ (int)i, 0 });
            const auto found = labelPins.find(instance.busName.trim());
            if (found == labelPins.end())
                labelPins[instance.busName.trim()] = ordinal;
            else
                sets.unite(found->second, ordinal);
        }

        std::set<int> groundRoots;
        for (size_t i = 0; i < instances.size(); ++i)
        {
            if (instances[i].symbolId != "ground" && instances[i].symbolId != "ground_bus")
                continue;
            const auto symbol = symbolForInstance(instances[i]);
            for (size_t p = 0; p < symbol.pins.size(); ++p)
                groundRoots.insert(sets.find(pinOrdinal({ (int)i, (int)p })));
        }

        std::map<int, int> assigned;
        int nextNet = 1;
        for (int pin = 0; pin < totalNodes; ++pin)
        {
            const auto root = sets.find(pin);
            if (groundRoots.count(root) != 0)
            {
                result[pin] = "0";
                continue;
            }
            if (const auto named = supplyNetNames.find(root); named != supplyNetNames.end())
            {
                result[pin] = named->second;
                continue;
            }
            if (assigned.count(root) == 0)
                assigned[root] = nextNet++;
            result[pin] = "n" + juce::String(assigned[root]);
        }
        return result;
    }

    static juce::String spiceNetName(const juce::String& supplyName)
    {
        juce::String result;
        for (auto c : supplyName)
        {
            if (c == '+') result << "P";
            else if (c == '-') result << "N";
            else if (juce::CharacterFunctions::isLetterOrDigit(c)) result << juce::String::charToString(c);
            else result << "_";
        }
        return result.isEmpty() ? juce::String("VSUPPLY") : result;
    }

    juce::String netFor(const PinRef& pin, const std::map<int, juce::String>& netNames) const
    {
        if (pin.instanceIndex < 0 || pin.instanceIndex >= (int)instances.size())
            return "floating";
        const auto ordinal = pinOrdinal(pin);
        const auto found = netNames.find(ordinal);
        return found != netNames.end() ? found->second : "floating";
    }

    juce::String netForNode(const WireNode& node, const std::map<int, juce::String>& netNames) const
    {
        const auto ordinal = nodeOrdinal(node);
        if (ordinal < 0)
            return "floating";
        const auto found = netNames.find(ordinal);
        return found != netNames.end() ? found->second : "floating";
    }

    std::vector<juce::String> printableNetNames(const std::map<int, juce::String>& netNames) const
    {
        std::set<juce::String> unique;
        for (const auto& entry : netNames)
            if (entry.second != "0")
                unique.insert(entry.second);

        std::vector<juce::String> result;
        result.reserve(unique.size());
        for (const auto& net : unique)
            result.push_back(net);
        return result;
    }

    juce::Point<float> pinPosition(const PinRef& pin) const
    {
        if (pin.instanceIndex < 0 || pin.instanceIndex >= (int)instances.size()) return {};
        const auto& instance = instances[(size_t)pin.instanceIndex];
        const auto symbol = symbolForInstance(instance);
        if (pin.pinIndex < 0 || pin.pinIndex >= (int)symbol.pins.size()) return instance.position;
        return instance.position + rotateOffset(symbol.pins[(size_t)pin.pinIndex].offset, instance.rotation);
    }

    juce::String pinLabel(const PinRef& pin) const
    {
        if (pin.instanceIndex < 0 || pin.instanceIndex >= (int)instances.size()) return {};
        const auto& instance = instances[(size_t)pin.instanceIndex];
        const auto symbol = symbolForInstance(instance);
        if (pin.pinIndex < 0 || pin.pinIndex >= (int)symbol.pins.size()) return instance.refdes;
        return instance.refdes + "." + symbol.pins[(size_t)pin.pinIndex].name;
    }

    int instanceIndexForRefdes(const juce::String& refdes) const
    {
        const auto requested = refdes.trim();
        for (int index = 0; index < (int)instances.size(); ++index)
            if (instances[(size_t)index].refdes.equalsIgnoreCase(requested))
                return index;
        return -1;
    }

    juce::String availableNodeSummary() const
    {
        juce::StringArray labels;
        for (int i = 0; i < (int)instances.size(); ++i)
        {
            const auto symbol = symbolForInstance(instances[(size_t)i]);
            for (int pin = 0; pin < (int)symbol.pins.size(); ++pin)
                labels.add(pinLabel({ i, pin }));
        }
        for (int i = 0; i < (int)junctions.size(); ++i)
            labels.add("N" + juce::String(i + 1));
        return labels.joinIntoString(", ");
    }

    bool nodeFromLabel(const juce::String& suppliedLabel, WireNode& node, juce::String& error) const
    {
        const auto label = suppliedLabel.trim();
        if (label.isEmpty())
        {
            error = "Node label is empty. Available labels: " + availableNodeSummary();
            return false;
        }

        if (label.startsWithIgnoreCase("N") && label.substring(1).containsOnly("0123456789"))
        {
            const auto index = label.substring(1).getIntValue() - 1;
            if (index >= 0 && index < (int)junctions.size())
            {
                node = WireNode::forJunction(index);
                return true;
            }
        }

        const auto dot = label.lastIndexOfChar('.');
        const auto refdes = dot > 0 ? label.substring(0, dot).trim() : label;
        const auto pinName = dot > 0 ? label.substring(dot + 1).trim() : juce::String();
        const auto instanceIndex = instanceIndexForRefdes(refdes);
        if (instanceIndex < 0)
        {
            error = "Unknown instance " + refdes + ". Available labels: " + availableNodeSummary();
            return false;
        }

        const auto symbol = symbolForInstance(instances[(size_t)instanceIndex]);
        if (pinName.isEmpty() && symbol.pins.size() == 1)
        {
            node = WireNode::forPin({ instanceIndex, 0 });
            return true;
        }

        for (int pin = 0; pin < (int)symbol.pins.size(); ++pin)
        {
            if (symbol.pins[(size_t)pin].name.equalsIgnoreCase(pinName))
            {
                node = WireNode::forPin({ instanceIndex, pin });
                return true;
            }
        }

        error = "Unknown pin label " + label + ". Available labels: " + availableNodeSummary();
        return false;
    }

    // ---- Sheets: the flat circuit is viewed one sheet at a time ----

    void addJunction(juce::Point<float> p)
    {
        junctionSheets.resize(junctions.size());
        junctions.push_back(p);
        junctionSheets.push_back(currentSheet);
    }

    juce::String junctionSheet(int j) const
    {
        return j >= 0 && j < (int)junctionSheets.size() ? junctionSheets[(size_t)j] : juce::String();
    }

    bool onSheet(int instanceIndex) const
    {
        return instanceIndex >= 0 && instanceIndex < (int)instances.size()
            && instances[(size_t)instanceIndex].sheet == currentSheet;
    }

    juce::String nodeSheet(const WireNode& node) const
    {
        if (node.isPin() && node.pin.instanceIndex < (int)instances.size())
            return instances[(size_t)node.pin.instanceIndex].sheet;
        if (node.isJunction())
            return junctionSheet(node.junctionIndex);
        return currentSheet;
    }

    bool nodeOnSheet(const WireNode& node) const { return nodeSheet(node) == currentSheet; }
    bool wireOnSheet(const Wire& wire) const { return nodeOnSheet(wire.a) && nodeOnSheet(wire.b); }

    bool groupOnSheet(const Group& group) const
    {
        for (int member : group.memberInstances)
            if (onSheet(member))
                return true;
        return false;
    }

    // Removes instances and junctions, dropping wires and probes that touch
    // them and remapping every index in wires, probes, groups. Returns the
    // old-to-new instance index map (-1 for removed).
    std::vector<int> removeInstancesAndJunctions(const std::set<int>& deadInstances,
                                                 const std::set<int>& deadJunctions,
                                                 juce::StringArray& droppedProbes)
    {
        std::vector<int> instanceMap(instances.size(), -1);
        std::vector<Instance> keptInstances;
        for (int i = 0; i < (int)instances.size(); ++i)
            if (deadInstances.count(i) == 0)
            {
                instanceMap[(size_t)i] = (int)keptInstances.size();
                keptInstances.push_back(instances[(size_t)i]);
            }

        junctionSheets.resize(junctions.size());
        std::vector<int> junctionMap(junctions.size(), -1);
        std::vector<juce::Point<float>> keptJunctions;
        std::vector<juce::String> keptJunctionSheets;
        for (int j = 0; j < (int)junctions.size(); ++j)
            if (deadJunctions.count(j) == 0)
            {
                junctionMap[(size_t)j] = (int)keptJunctions.size();
                keptJunctions.push_back(junctions[(size_t)j]);
                keptJunctionSheets.push_back(junctionSheets[(size_t)j]);
            }

        auto remap = [&](WireNode& node) {
            if (node.isPin())
            {
                node.pin.instanceIndex = node.pin.instanceIndex < (int)instanceMap.size() ? instanceMap[(size_t)node.pin.instanceIndex] : -1;
                return node.pin.instanceIndex >= 0;
            }
            if (node.isJunction())
            {
                node.junctionIndex = node.junctionIndex < (int)junctionMap.size() ? junctionMap[(size_t)node.junctionIndex] : -1;
                return node.junctionIndex >= 0;
            }
            return false;
        };

        std::vector<Wire> keptWires;
        for (auto wire : wires)
            if (remap(wire.a) && remap(wire.b))
                keptWires.push_back(wire);

        std::vector<Probe> keptProbes;
        for (auto probe : probes)
        {
            if (remap(probe.node))
                keptProbes.push_back(probe);
            else
                droppedProbes.add(probe.id);
        }

        for (auto& group : groups)
        {
            std::vector<int> members;
            for (int member : group.memberInstances)
                if (member >= 0 && member < (int)instanceMap.size() && instanceMap[(size_t)member] >= 0)
                    members.push_back(instanceMap[(size_t)member]);
            group.memberInstances = members;
        }
        groups.erase(std::remove_if(groups.begin(), groups.end(), [](const Group& group) {
            return group.memberInstances.empty();
        }), groups.end());

        instances = std::move(keptInstances);
        junctions = std::move(keptJunctions);
        junctionSheets = std::move(keptJunctionSheets);
        wires = std::move(keptWires);
        probes = std::move(keptProbes);
        selectedInstance = -1;
        selectedInstances.clear();
        selectedGroup = -1;
        return instanceMap;
    }

    juce::Point<float> nodePosition(const WireNode& node) const
    {
        if (node.isPin())
            return pinPosition(node.pin);
        if (node.isJunction() && node.junctionIndex < (int)junctions.size())
            return junctions[(size_t)node.junctionIndex];
        return {};
    }

    juce::String nodeLabel(const WireNode& node) const
    {
        if (node.isPin())
            return pinLabel(node.pin);
        if (node.isJunction())
            return "N" + juce::String(node.junctionIndex + 1);
        return {};
    }

    static bool sameNode(const WireNode& a, const WireNode& b)
    {
        if (a.isPin() && b.isPin())
            return a.pin.instanceIndex == b.pin.instanceIndex && a.pin.pinIndex == b.pin.pinIndex;
        if (a.isJunction() && b.isJunction())
            return a.junctionIndex == b.junctionIndex;
        return false;
    }

    bool isRailAnchorNode(const WireNode& node) const
    {
        return node.isPin()
            && node.pin.instanceIndex >= 0
            && node.pin.instanceIndex < (int)instances.size()
            && isRailBus(instances[(size_t)node.pin.instanceIndex].symbolId);
    }

    bool isInternalRailTapWire(const Wire& wire) const
    {
        return (isRailAnchorNode(wire.a) && wire.b.isJunction())
            || (isRailAnchorNode(wire.b) && wire.a.isJunction());
    }

    juce::Point<float> nodeLeadDirection(const WireNode& node) const
    {
        if (!node.isPin() || node.pin.instanceIndex < 0 || node.pin.instanceIndex >= (int)instances.size())
            return {};

        const auto& instance = instances[(size_t)node.pin.instanceIndex];
        const auto symbol = symbolForInstance(instance);
        if (node.pin.pinIndex < 0 || node.pin.pinIndex >= (int)symbol.pins.size())
            return {};

        return rotateOffset(schematic::pinLeadDirection(symbol, node.pin.pinIndex), instance.rotation);
    }

    std::vector<juce::Point<float>> routedWirePoints(const WireNode& a, const WireNode& b) const
    {
        ensureRoutes();
        for (size_t i = 0; i < wires.size() && i < routeCache.size(); ++i)
        {
            const auto& wire = wires[i];
            const bool match = (sameNode(wire.a, a) && sameNode(wire.b, b)) || (sameNode(wire.a, b) && sameNode(wire.b, a));
            if (!match || routeCache[i].size() < 2)
                continue;
            if (sameNode(wire.a, a))
                return routeCache[i];
            return { routeCache[i].rbegin(), routeCache[i].rend() };
        }
        return routedWirePoints(a, nodePosition(b), nodeLeadDirection(b));
    }

    // Whole-diagram routing, recomputed only when geometry or wiring changes.
    mutable juce::String routeSignature;
    mutable std::vector<std::vector<juce::Point<float>>> routeCache;
    mutable std::vector<juce::Point<float>> routedJunctions;

    juce::String currentRouteSignature() const
    {
        juce::String sig;
        sig.preallocateBytes(instances.size() * 32 + wires.size() * 16 + junctions.size() * 12);
        sig << currentSheet << '#';
        for (const auto& instance : instances)
            sig << instance.symbolId << ':' << instance.sheet << ':' << instance.busName << ':' << (int)instance.position.x << ',' << (int)instance.position.y
                << ',' << instance.rotation << ',' << (int)instance.busLength << ';';
        sig << '|';
        auto node = [&sig](const WireNode& n) {
            sig << n.pin.instanceIndex << '.' << n.pin.pinIndex << '.' << n.junctionIndex << ' ';
        };
        for (const auto& wire : wires) { node(wire.a); node(wire.b); sig << ';'; }
        sig << '|';
        for (const auto& j : junctions)
            sig << (int)j.x << ',' << (int)j.y << ';';
        return sig;
    }

    void ensureRoutes() const
    {
        auto signature = currentRouteSignature();
        if (signature == routeSignature && routeCache.size() == wires.size())
            return;
        routeSignature = std::move(signature);

        std::vector<int> obstacleInstance;
        schematic::routing::Problem problem;
        problem.obstacles = buildRoutingObstacles(obstacleInstance);
        problem.junctions = junctions;
        std::vector<int> instanceObstacle(instances.size(), -1);
        for (int o = 0; o < (int)obstacleInstance.size(); ++o)
            instanceObstacle[(size_t)obstacleInstance[(size_t)o]] = o;

        const auto netNames = computeNetNames();
        std::map<juce::String, int> netIds;
        auto endpointFor = [&](const WireNode& node, schematic::routing::Endpoint& out) {
            if (node.isJunction())
            {
                if (node.junctionIndex >= (int)junctions.size()) return false;
                out = schematic::routing::Endpoint::forJunction(node.junctionIndex);
                return true;
            }
            if (!node.isPin() || node.pin.instanceIndex >= (int)instances.size()) return false;
            const auto obstacle = instanceObstacle[(size_t)node.pin.instanceIndex];
            out = obstacle >= 0 ? schematic::routing::Endpoint::forPin(obstacle, node.pin.pinIndex)
                                : schematic::routing::Endpoint::forPoint(nodePosition(node));
            return true;
        };

        std::vector<int> connectionOfWire(wires.size(), -1);
        for (size_t i = 0; i < wires.size(); ++i)
        {
            const auto& wire = wires[i];
            if (isInternalRailTapWire(wire) || !wireOnSheet(wire))
                continue;
            schematic::routing::Connection connection;
            if (!endpointFor(wire.a, connection.a) || !endpointFor(wire.b, connection.b))
                continue;
            const auto net = netForNode(wire.a, netNames);
            const auto found = netIds.find(net);
            connection.net = found != netIds.end() ? found->second : (netIds[net] = (int)netIds.size());
            connectionOfWire[i] = (int)problem.connections.size();
            problem.connections.push_back(connection);
        }

        const auto routes = schematic::routing::routeConnections(problem, schematic::gridSize, &routedJunctions, routingStyle());
        routeCache.assign(wires.size(), {});
        for (size_t i = 0; i < wires.size(); ++i)
            if (connectionOfWire[i] >= 0)
                routeCache[i] = routes[(size_t)connectionOfWire[i]];
    }

    std::vector<juce::Point<float>> routedWirePoints(const WireNode& a,
                                                     juce::Point<float> b,
                                                     juce::Point<float> bLead) const
    {
        constexpr auto leadLength = 24.0f;
        const auto start = nodePosition(a);
        const auto aLead = nodeLeadDirection(a);
        const auto startRun = aLead == juce::Point<float>() ? start : start + aLead * leadLength;
        const auto endRun = bLead == juce::Point<float>() ? b : b + bLead * leadLength;

        std::vector<juce::Point<float>> points;
        points.push_back(start);
        if (startRun.getDistanceFrom(start) > 0.1f)
            points.push_back(startRun);

        if (std::abs(startRun.x - endRun.x) <= 0.1f || std::abs(startRun.y - endRun.y) <= 0.1f)
        {
            points.push_back(endRun);
        }
        else
        {
            const auto midX = std::round((startRun.x + endRun.x) * 0.5f / 24.0f) * 24.0f;
            points.push_back({ midX, startRun.y });
            points.push_back({ midX, endRun.y });
            points.push_back(endRun);
        }

        if (b.getDistanceFrom(endRun) > 0.1f)
            points.push_back(b);

        return points;
    }

    PinRef hitTestPin(juce::Point<float> p) const
    {
        for (int i = (int)instances.size() - 1; i >= 0; --i)
        {
            if (isRailBus(instances[(size_t)i].symbolId) || !onSheet(i))
                continue;

            const auto symbol = symbolForInstance(instances[(size_t)i]);
            for (int j = 0; j < (int)symbol.pins.size(); ++j)
            {
                const auto pin = pinPosition({ i, j });
                if (pin.getDistanceFrom(p) <= hitDistance(14.0f))
                    return { i, j };
            }
        }
        return {};
    }

    int hitTestJunction(juce::Point<float> p) const
    {
        for (int i = (int)junctions.size() - 1; i >= 0; --i)
            if (junctionSheet(i) == currentSheet && junctions[(size_t)i].getDistanceFrom(p) <= hitDistance(12.0f))
                return i;
        return -1;
    }

    static float distanceToSegment(juce::Point<float> p, juce::Point<float> a, juce::Point<float> b)
    {
        const auto ab = b - a;
        const auto ap = p - a;
        const auto lengthSquared = ab.x * ab.x + ab.y * ab.y;
        if (lengthSquared <= 0.001f)
            return p.getDistanceFrom(a);

        const auto t = std::clamp((ap.x * ab.x + ap.y * ab.y) / lengthSquared, 0.0f, 1.0f);
        return p.getDistanceFrom({ a.x + ab.x * t, a.y + ab.y * t });
    }

    float distanceToWire(juce::Point<float> p, const Wire& wire) const
    {
        if (isInternalRailTapWire(wire))
            return std::numeric_limits<float>::max();

        const auto points = routedWirePoints(wire.a, wire.b);
        auto best = std::numeric_limits<float>::max();
        for (size_t i = 1; i < points.size(); ++i)
            best = std::min(best, distanceToSegment(p, points[i - 1], points[i]));
        return best;
    }

    int hitTestWire(juce::Point<float> p) const
    {
        for (int i = (int)wires.size() - 1; i >= 0; --i)
            if (wireOnSheet(wires[(size_t)i]) && distanceToWire(p, wires[(size_t)i]) <= hitDistance(8.0f))
                return i;
        return -1;
    }

    int hitTestInstance(juce::Point<float> p) const
    {
        for (int i = (int)instances.size() - 1; i >= 0; --i)
        {
            const auto& instance = instances[(size_t)i];
            if (instance.sheet != currentSheet)
                continue;
            const auto symbol = symbolForInstance(instance);
            if (orientedBounds(instance, symbol).expanded(hitDistance(4.0f)).contains(p))
                return i;
        }
        return -1;
    }

    bool isInstanceSelected(int index) const
    {
        return selectedInstances.contains(index);
    }

    void toggleInstanceSelection(int index)
    {
        if (selectedInstances.contains(index))
            selectedInstances.removeFirstMatchingValue(index);
        else
            selectedInstances.add(index);

        selectedInstance = selectedInstances.isEmpty() ? -1 : selectedInstances.getLast();
    }

    void beginSelectionBox(juce::Point<float> start, bool append)
    {
        selectionBoxAppend = append;
        if (!selectionBoxAppend)
        {
            selectedInstances.clear();
            selectedInstance = -1;
        }
        selectingBox = true;
        selectionBoxStart = start;
        selectionBoxEnd = start;
        notifySelection();
        repaint();
    }

    juce::Rectangle<float> currentSelectionBox() const
    {
        return juce::Rectangle<float>::leftTopRightBottom(std::min(selectionBoxStart.x, selectionBoxEnd.x),
                                                          std::min(selectionBoxStart.y, selectionBoxEnd.y),
                                                          std::max(selectionBoxStart.x, selectionBoxEnd.x),
                                                          std::max(selectionBoxStart.y, selectionBoxEnd.y));
    }

    void updateSelectionFromBox()
    {
        const auto box = currentSelectionBox();
        if (!selectionBoxAppend)
            selectedInstances.clear();
        for (int i = 0; i < (int)instances.size(); ++i)
        {
            if (!onSheet(i))
                continue;
            const auto symbol = symbolForInstance(instances[(size_t)i]);
            if (box.intersects(orientedBounds(instances[(size_t)i], symbol).expanded(4.0f)))
                selectedInstances.addIfNotAlreadyThere(i);
        }
        selectedInstance = selectedInstances.isEmpty() ? -1 : selectedInstances.getLast();
        notifySelection();
    }

    void captureSelectedDragStarts()
    {
        selectedDragStartPositions.clear();
        selectedDragStartPositions.resize((size_t)instances.size());
        for (int i = 0; i < (int)instances.size(); ++i)
            selectedDragStartPositions[(size_t)i] = instances[(size_t)i].position;
    }

    void moveSelectedInstances(juce::Point<float> delta)
    {
        for (int index : selectedInstances)
        {
            if (index >= 0 && index < (int)instances.size())
                instances[(size_t)index].position = snapPoint(selectedDragStartPositions[(size_t)index] + delta);
        }
    }

    void drawSelectionBox(juce::Graphics& g)
    {
        if (!selectingBox)
            return;

        const auto box = currentSelectionBox();
        g.setColour(juce::Colour(0x3329b6f6));
        g.fillRect(box);
        g.setColour(juce::Colour(0xff29b6f6));
        g.drawRect(box, 1.5f);
    }

    // Box around the members' full footprints (body, pin ends, labels),
    // with a strip on top for the name tab so it never covers a part.
    juce::Rectangle<float> groupBounds(const Group& group) const
    {
        float left = std::numeric_limits<float>::max(), top = left;
        float right = std::numeric_limits<float>::lowest(), bottom = right;
        bool any = false;
        for (int index : group.memberInstances)
        {
            if (index < 0 || index >= (int)instances.size())
                continue;
            const auto& instance = instances[(size_t)index];
            const auto symbol = symbolForInstance(instance);
            auto box = schematic::rotateBounds(schematic::extentBounds(symbol), instance.rotation);
            if (!schematic::isPowerSymbol(instance.symbolId))
            {
                const auto labels = schematic::labelRectsFor(symbol, instance.rotation);
                box = box.getUnion(labels.refdes).getUnion(labels.value);
            }
            box = box.translated(instance.position.x, instance.position.y);
            left = std::min(left, box.getX());
            top = std::min(top, box.getY());
            right = std::max(right, box.getRight());
            bottom = std::max(bottom, box.getBottom());
            any = true;
        }
        if (!any)
            return {};
        constexpr float pad = 16.0f, titleStrip = 22.0f;
        return { left - pad, top - pad - titleStrip, right - left + pad * 2.0f, bottom - top + pad * 2.0f + titleStrip };
    }

    void drawGroups(juce::Graphics& g)
    {
        for (int i = 0; i < (int)groups.size(); ++i)
        {
            const auto& group = groups[(size_t)i];
            if (!groupOnSheet(group))
                continue;
            const auto bounds = groupBounds(group);
            if (bounds.isEmpty())
                continue;

            const auto selected = i == selectedGroup;
            const auto edge = selected ? juce::Colour(0xffffc857) : group.colour.withAlpha(0.75f);
            g.setColour(group.colour.withAlpha(0.06f));
            g.fillRoundedRectangle(bounds, 6.0f);
            g.setColour(edge);
            g.drawRoundedRectangle(bounds, 6.0f, selected ? 2.5f : 1.5f);

            // Name tab in the top-left corner.
            const juce::Font font(13.0f, juce::Font::bold);
            const auto textWidth = font.getStringWidthFloat(group.name);
            const juce::Rectangle<float> tab { bounds.getX(), bounds.getY(), textWidth + 20.0f, 20.0f };
            juce::Path tabPath;
            tabPath.addRoundedRectangle(tab.getX(), tab.getY(), tab.getWidth(), tab.getHeight(), 6.0f, 6.0f, true, false, false, true);
            g.setColour(edge.withAlpha(selected ? 0.9f : 0.55f));
            g.fillPath(tabPath);
            g.setColour(juce::Colour(0xff0e141a));
            g.setFont(font);
            g.drawText(group.name, tab.reduced(10.0f, 0.0f), juce::Justification::centredLeft, false);
        }
    }

    int hitTestGroup(juce::Point<float> position) const
    {
        for (int i = (int)groups.size() - 1; i >= 0; --i)
        {
            if (!groupOnSheet(groups[(size_t)i]))
                continue;
            const auto bounds = groupBounds(groups[(size_t)i]);
            if (bounds.isEmpty())
                continue;
            if (bounds.expanded(hitDistance(4.0f)).contains(position)
                && !bounds.reduced(18.0f).contains(position))
                return i;
        }
        return -1;
    }

    void selectGroup(int groupIndex)
    {
        if (groupIndex < 0 || groupIndex >= (int)groups.size())
            return;

        selectedGroup = groupIndex;
        selectedInstances.clear();
        for (int member : groups[(size_t)groupIndex].memberInstances)
            if (member >= 0 && member < (int)instances.size())
                selectedInstances.addIfNotAlreadyThere(member);
        selectedInstance = selectedInstances.isEmpty() ? -1 : selectedInstances.getLast();
    }

    // ---- Group boxes: shared by the UI and the agent's schematic tools ----

    juce::String nextGroupId() const
    {
        int highest = 0;
        for (const auto& group : groups)
            if (group.id.startsWith("G") && group.id.substring(1).containsOnly("0123456789"))
                highest = std::max(highest, group.id.substring(1).getIntValue());
        return "G" + juce::String(highest + 1);
    }

    int groupIndexFor(const juce::String& idOrName) const
    {
        const auto key = idOrName.trim();
        for (int i = 0; i < (int)groups.size(); ++i)
            if (groups[(size_t)i].id.equalsIgnoreCase(key))
                return i;
        for (int i = 0; i < (int)groups.size(); ++i)
            if (groups[(size_t)i].name.equalsIgnoreCase(key))
                return i;
        return -1;
    }

    juce::String groupJson(const Group& group) const
    {
        juce::String text;
        text << "{ \"id\": " << quote(group.id)
             << ", \"name\": " << quote(group.name)
             << ", \"category\": " << quote(group.category)
             << ", \"notes\": " << quote(group.notes)
             << ", \"members\": [";
        bool first = true;
        for (int member : group.memberInstances)
        {
            if (member < 0 || member >= (int)instances.size())
                continue;
            if (!first) text << ", ";
            first = false;
            text << quote(instances[(size_t)member].refdes);
        }
        text << "] }";
        return text;
    }

    // Resolves refdes strings to instance indices; fills `error` with the
    // first unknown one.
    bool membersFromRefdes(const juce::var& list, std::vector<int>& members, juce::String& error) const
    {
        if (list.isVoid())
            return true;
        const auto* array = list.getArray();
        if (array == nullptr)
        {
            error = "Members must be an array of reference designators.";
            return false;
        }
        for (const auto& entry : *array)
        {
            const auto index = instanceIndexForRefdes(entry.toString().trim());
            if (index < 0)
            {
                error = "No placed component has reference designator " + entry.toString().trim() + ".";
                return false;
            }
            if (std::find(members.begin(), members.end(), index) == members.end())
                members.push_back(index);
        }
        return true;
    }

    // ---- Sub-diagrams: blocks are views of the one flat circuit ----

    juce::String nextSheetId() const
    {
        int highest = 0;
        for (const auto& instance : instances)
            if (instance.childSheet.startsWith("S") && instance.childSheet.substring(1).containsOnly("0123456789"))
                highest = std::max(highest, instance.childSheet.substring(1).getIntValue());
        return "S" + juce::String(highest + 1);
    }

    int blockIndexFor(const juce::String& key) const
    {
        const auto k = key.trim();
        for (int i = 0; i < (int)instances.size(); ++i)
            if (instances[(size_t)i].symbolId == "sub_block" && instances[(size_t)i].refdes.equalsIgnoreCase(k))
                return i;
        for (int i = 0; i < (int)instances.size(); ++i)
            if (instances[(size_t)i].symbolId == "sub_block" && instances[(size_t)i].value.equalsIgnoreCase(k))
                return i;
        return -1;
    }

    int blockForSheet(const juce::String& sheet) const
    {
        for (int i = 0; i < (int)instances.size(); ++i)
            if (instances[(size_t)i].symbolId == "sub_block" && instances[(size_t)i].childSheet == sheet)
                return i;
        return -1;
    }

    juce::String sheetName(const juce::String& sheet) const
    {
        if (sheet.isEmpty())
            return "Main";
        const auto block = blockForSheet(sheet);
        return block >= 0 ? instances[(size_t)block].value : sheet;
    }

    // Sheets from the top level down to the one being viewed.
    juce::StringArray sheetPath() const
    {
        juce::StringArray path;
        auto sheet = currentSheet;
        for (int guard = 0; guard < 64; ++guard)
        {
            path.insert(0, sheet);
            if (sheet.isEmpty())
                break;
            const auto block = blockForSheet(sheet);
            sheet = block >= 0 ? instances[(size_t)block].sheet : juce::String();
        }
        return path;
    }

    static juce::String partsListCell(juce::String text)
    {
        return text.replace("\t", " ").replace("\r\n", " ").replace("\n", " ").replace("\r", " ").trim();
    }

    bool includeInPartsList(const Instance& instance) const
    {
        return instance.symbolId != "block_port" && instance.symbolId != "annotation_text" && !isNetMarker(instance.symbolId);
    }

    juce::String primaryPartsListValue(const Instance& instance) const
    {
        if (instance.symbolId == "sub_block")
            return instance.value;
        auto value = partValue(instance, "value");
        if (value.isEmpty())
            value = partValue(instance, "busName");
        if (value.isEmpty())
            value = partValue(instance, "frequency");
        return value;
    }

    juce::String modelInfoForPartsList(const Instance& instance) const
    {
        const auto fidelity = simulationFidelityFor(instance);
        const auto value = partValue(instance, "value");
        if (fidelity.contains("model") && value.isNotEmpty())
            return value;
        if (instance.manufacturerPart.isNotEmpty())
            return instance.manufacturerPart;
        return instance.family;
    }

    void appendPartsListRow(juce::String& text, const Instance& instance, const juce::String& path) const
    {
        text << partsListCell(path) << "\t"
             << partsListCell(sheetName(instance.sheet)) << "\t"
             << partsListCell(instance.refdes) << "\t"
             << partsListCell(parts::displayName(instance.symbolId)) << "\t"
             << partsListCell(instance.symbolId) << "\t"
             << partsListCell(primaryPartsListValue(instance)) << "\t"
             << partsListCell(modelInfoForPartsList(instance)) << "\t"
             << partsListCell(simulationFidelityFor(instance)) << "\t"
             << partsListCell(instance.family) << "\t"
             << partsListCell(instance.manufacturerPart) << "\n";
    }

    void appendPartsListForSheet(juce::String& text, const juce::String& sheet, const juce::String& path,
                                 bool recursive, juce::StringArray& activeSheets) const
    {
        if (activeSheets.contains(sheet))
        {
            text << "# Recursion stopped at " << sheetName(sheet) << " to avoid a subdiagram loop.\n\n";
            return;
        }

        activeSheets.add(sheet);
        text << "# Diagram: " << path << "\n";
        text << "Path\tSheet\tRefdes\tType\tSymbol\tValue\tModel\tFidelity\tFamily\tManufacturer Part\n";
        for (const auto& instance : instances)
            if (instance.sheet == sheet && includeInPartsList(instance))
                appendPartsListRow(text, instance, path);
        text << "\n";

        if (recursive)
        {
            for (const auto& instance : instances)
                if (instance.sheet == sheet && instance.symbolId == "sub_block" && instance.childSheet.isNotEmpty())
                {
                    const auto childPath = path + " > " + instance.refdes + " " + instance.value;
                    text << "# Subdiagram: " << instance.refdes << " " << instance.value << " -> " << sheetName(instance.childSheet) << "\n";
                    appendPartsListForSheet(text, instance.childSheet, childPath, true, activeSheets);
                    text << "# End Subdiagram: " << instance.refdes << " " << instance.value << "\n\n";
                }
        }
        activeSheets.removeString(sheet);
    }

    juce::String partsListText(bool recursive) const
    {
        juce::String text;
        text << (recursive ? "Recursive Parts List" : "Parts List") << "\n";
        text << "Starting diagram\t" << sheetName(currentSheet) << "\n\n";
        juce::StringArray activeSheets;
        appendPartsListForSheet(text, currentSheet, sheetName(currentSheet), recursive, activeSheets);
        return text;
    }

    void copyPartsList(bool recursive) const
    {
        juce::SystemClipboard::copyTextToClipboard(partsListText(recursive));
        if (onStatus) onStatus(recursive ? "Copied recursive parts list." : "Copied parts list.");
    }

    juce::File defaultPartsListFile(bool recursive) const
    {
        const auto folder = outputDirectory != nullptr ? outputDirectory() : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
        const auto suffix = recursive ? "_recursive_parts_list.txt" : "_parts_list.txt";
        return folder.getChildFile(juce::File::createLegalFileName(sheetName(currentSheet)).replaceCharacter(' ', '_') + suffix);
    }

    void savePartsListAsTxt(bool recursive)
    {
        const auto text = partsListText(recursive);
        partsListChooser = std::make_unique<juce::FileChooser>(recursive ? "Save Recursive Parts List" : "Save Parts List",
                                                               defaultPartsListFile(recursive), "*.txt");
        partsListChooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                                      [safe = juce::Component::SafePointer<SchematicCanvasPanel>(this), text](const juce::FileChooser& fc) {
            if (safe == nullptr)
                return;
            auto file = fc.getResult();
            if (file == juce::File())
                return;
            if (!file.hasFileExtension("txt"))
                file = file.withFileExtension("txt");
            if (file.replaceWithText(text) && safe->onStatus)
                safe->onStatus(file.getFileName() + " saved.");
            else if (safe->onStatus)
                safe->onStatus("Could not save parts list: " + file.getFullPathName());
        });
    }

    void openSheet(const juce::String& sheet)
    {
        currentSheet = sheet;
        selectedInstance = -1;
        selectedInstances.clear();
        selectedGroup = -1;
        wireDragging = false;
        draggingInstance = false;
        routeSignature.clear();
        notifySelection();
        juce::StringArray names;
        for (const auto& s : sheetPath())
            names.add(sheetName(s));
        if (onStatus) onStatus("Viewing " + names.joinIntoString(" > ") + ".");
        forceDeferredRepaint();
    }

    // Wires a set of pins on the current sheet together as libavoid trees.
    void wireNetsOnCurrentSheet(const std::vector<std::vector<PinRef>>& nets)
    {
        std::vector<int> obstacleInstance;
        const auto obstacles = buildRoutingObstacles(obstacleInstance);
        std::vector<int> instanceObstacle(instances.size(), -1);
        for (int o = 0; o < (int)obstacleInstance.size(); ++o)
            instanceObstacle[(size_t)obstacleInstance[(size_t)o]] = o;

        std::vector<schematic::routing::NetTerminals> terminals;
        for (const auto& net : nets)
        {
            schematic::routing::NetTerminals t;
            for (const auto& pin : net)
                if (pin.instanceIndex >= 0 && instanceObstacle[(size_t)pin.instanceIndex] >= 0)
                    t.terminals.push_back(schematic::routing::Endpoint::forPin(instanceObstacle[(size_t)pin.instanceIndex], pin.pinIndex));
            if (t.terminals.size() >= 2)
                terminals.push_back(t);
        }

        const auto trees = schematic::routing::routeNetTrees(obstacles, terminals, schematic::gridSize, routingStyle());
        for (const auto& tree : trees)
        {
            const auto base = (int)junctions.size();
            for (const auto& j : tree.junctions)
                addJunction(j);
            auto toNode = [&](const schematic::routing::Endpoint& e) {
                return e.isJunction() ? WireNode::forJunction(base + e.junction)
                                      : WireNode::forPin({ obstacleInstance[(size_t)e.obstacle], e.pin });
            };
            for (const auto& edge : tree.edges)
                wires.push_back({ toNode(edge.a), toNode(edge.b) });
        }
    }

    // Folds `members` (on the current sheet) into a block. Every signal net
    // that crosses the boundary becomes a block pin outside and a port
    // bubble of the same name inside. Selected net markers are intentional
    // terminals, so they also become pins even for ground and named supplies.
    juce::String createSubDiagram(std::vector<int> members, juce::String name, juce::String& error)
    {
        std::set<int> inside;
        for (int m : members)
            if (onSheet(m) && instances[(size_t)m].symbolId != "block_port")
                inside.insert(m);
        if (inside.empty())
        {
            error = "Choose at least one component on the sheet being viewed.";
            return {};
        }
        name = name.trim().isNotEmpty() ? name.trim() : juce::String("Sub-diagram");
        pushUndoSnapshot();

        // Ground symbols, supply ports and labels wired only to members go inside.
        for (int i = 0; i < (int)instances.size(); ++i)
        {
            if (!onSheet(i) || inside.count(i) != 0 || !schematic::isPowerSymbol(instances[(size_t)i].symbolId))
                continue;
            bool any = false, allInside = true;
            for (const auto& wire : wires)
            {
                const WireNode* other = nullptr;
                if (wire.a.isPin() && wire.a.pin.instanceIndex == i) other = &wire.b;
                if (wire.b.isPin() && wire.b.pin.instanceIndex == i) other = &wire.a;
                if (other == nullptr) continue;
                any = true;
                allInside &= other->isPin() && inside.count(other->pin.instanceIndex) != 0;
            }
            if (any && allInside)
                inside.insert(i);
        }

        const auto netNames = computeNetNames();
        std::set<juce::String> supplyNets { "0" };
        for (int i = 0; i < (int)instances.size(); ++i)
            if (instances[(size_t)i].symbolId == "power_port" || instances[(size_t)i].symbolId == "power_bus")
                supplyNets.insert(netFor({ i, 0 }, netNames));

        struct NetSide { std::vector<PinRef> in, out; juce::String label; };
        std::map<juce::String, NetSide> nets;
        for (int i = 0; i < (int)instances.size(); ++i)
        {
            if (!onSheet(i))
                continue;
            const auto symbol = symbolForInstance(instances[(size_t)i]);
            for (int p = 0; p < (int)symbol.pins.size(); ++p)
            {
                const auto net = netFor({ i, p }, netNames);
                if (net == "floating")
                    continue;
                auto& side = nets[net];
                (inside.count(i) != 0 ? side.in : side.out).push_back({ i, p });
                // A user's net label names the pin; instrument probe labels
                // (SCOPE1.CH1) do not.
                if (instances[(size_t)i].symbolId == "net_label" && side.label.isEmpty()
                    && !instances[(size_t)i].busName.containsChar('.'))
                    side.label = instances[(size_t)i].busName;
            }
        }

        juce::Rectangle<float> innerBox;
        bool first = true;
        for (int i : inside)
        {
            const auto p = instances[(size_t)i].position;
            innerBox = first ? juce::Rectangle<float>(p.x, p.y, 1.0f, 1.0f) : innerBox.getUnion(juce::Rectangle<float>(p.x, p.y, 1.0f, 1.0f));
            first = false;
        }

        struct Crossing { juce::String net; schematic::BlockPort port; std::vector<PinRef> in, out; };
        std::map<juce::String, schematic::BlockPort> explicitPorts;
        for (int i : inside)
        {
            const auto& instance = instances[(size_t)i];
            if (!schematic::isPowerSymbol(instance.symbolId))
                continue;
            const auto net = netFor({ i, 0 }, netNames);
            if (net == "floating")
                continue;
            auto name = instance.symbolId == "ground" ? juce::String("GND") : instance.busName.trim();
            if (name.isEmpty())
                name = instance.value.trim().isNotEmpty() ? instance.value.trim() : net;
            const auto rightSide = instance.symbolId == "power_port" ? true
                                 : instance.symbolId == "ground" ? false
                                 : instance.position.x >= innerBox.getCentreX();
            explicitPorts[net] = { name, rightSide };
        }

        std::vector<Crossing> crossings;
        int inputs = 0, outputs = 0;
        std::set<juce::String> usedNames;
        for (const auto& [net, side] : nets)
        {
            const auto explicitPort = explicitPorts.find(net);
            if (side.in.empty() || (side.out.empty() && explicitPort == explicitPorts.end())
                || (supplyNets.count(net) != 0 && explicitPort == explicitPorts.end()))
                continue;
            float outX = 0.0f;
            for (const auto& pin : side.out) outX += pinPosition(pin).x;
            const auto rightSide = explicitPort != explicitPorts.end()
                ? explicitPort->second.rightSide
                : (outX / (float)side.out.size()) > innerBox.getCentreX();
            auto portName = explicitPort != explicitPorts.end() ? explicitPort->second.name
                          : side.label.isNotEmpty() ? side.label
                          : rightSide ? (++outputs == 1 ? juce::String("OUT") : "OUT" + juce::String(outputs))
                                      : (++inputs == 1 ? juce::String("IN") : "IN" + juce::String(inputs));
            while (usedNames.count(portName) != 0)
                portName << "_";
            usedNames.insert(portName);
            crossings.push_back({ net, { portName, rightSide }, side.in, side.out });
        }

        const auto child = nextSheetId();

        // Wires and junctions of crossing nets on this sheet are rebuilt on
        // both sides; junctions of nets wholly inside move with the parts.
        std::set<juce::String> crossingNets;
        for (const auto& c : crossings) crossingNets.insert(c.net);
        std::set<int> deadJunctions;
        for (int j = 0; j < (int)junctions.size(); ++j)
        {
            if (junctionSheet(j) != currentSheet)
                continue;
            const auto net = netForNode(WireNode::forJunction(j), netNames);
            if (crossingNets.count(net) != 0)
                deadJunctions.insert(j);
            else if (nets.count(net) != 0 && nets[net].out.empty() && !nets[net].in.empty())
            {
                junctionSheets.resize(junctions.size());
                junctionSheets[(size_t)j] = child;
            }
        }
        wires.erase(std::remove_if(wires.begin(), wires.end(), [&](const Wire& w) {
            return wireOnSheet(w) && crossingNets.count(netForNode(w.a, netNames)) != 0;
        }), wires.end());

        for (int i : inside)
            instances[(size_t)i].sheet = child;

        // Mixed groups lose the members that moved; wholly inside groups move along.
        for (auto& group : groups)
        {
            const auto allInside = std::all_of(group.memberInstances.begin(), group.memberInstances.end(), [&](int m) { return inside.count(m) != 0; });
            if (!allInside)
                group.memberInstances.erase(std::remove_if(group.memberInstances.begin(), group.memberInstances.end(),
                                                           [&](int m) { return inside.count(m) != 0; }),
                                            group.memberInstances.end());
        }

        juce::StringArray droppedProbes;
        const auto map = removeInstancesAndJunctions({}, deadJunctions, droppedProbes);
        auto remapPins = [&](std::vector<PinRef>& pins) {
            for (auto& pin : pins) pin.instanceIndex = map[(size_t)pin.instanceIndex];
        };
        for (auto& c : crossings) { remapPins(c.in); remapPins(c.out); }

        // The block on this sheet.
        Instance block;
        block.symbolId = "sub_block";
        block.refdes = nextRefdesFor("sub_block");
        block.value = name;
        block.family = familyFor("sub_block");
        block.position = snapToGrid(innerBox.getCentre());
        block.sheet = currentSheet;
        block.childSheet = child;
        for (const auto& c : crossings)
            block.ports.push_back(c.port);
        const auto blockIndex = (int)instances.size();
        instances.push_back(block);

        // Port bubbles inside: inputs left of the parts, outputs right.
        std::vector<int> bubbleIndex;
        int leftRow = 0, rightRow = 0;
        for (const auto& c : crossings)
        {
            Instance bubble;
            bubble.symbolId = "block_port";
            bubble.busName = c.port.name;
            bubble.value = c.port.name;
            bubble.family = familyFor("block_port");
            bubble.sheet = child;
            bubble.rotation = c.port.rightSide ? 180 : 0;
            const auto row = (float)(c.port.rightSide ? rightRow++ : leftRow++);
            bubble.position = snapToGrid({ c.port.rightSide ? innerBox.getRight() + 168.0f : innerBox.getX() - 168.0f,
                                           innerBox.getY() + row * 72.0f });
            bubble.refdes = nextRefdesFor("block_port");
            bubbleIndex.push_back((int)instances.size());
            instances.push_back(bubble);
        }

        // Outside: each crossing net now runs to its block pin.
        std::vector<std::vector<PinRef>> outsideNets;
        for (size_t k = 0; k < crossings.size(); ++k)
        {
            auto pins = crossings[k].out;
            pins.push_back({ blockIndex, (int)k });
            if (pins.size() >= 2)
                outsideNets.push_back(pins);
        }
        wireNetsOnCurrentSheet(outsideNets);

        // Inside: each crossing net runs to its port bubble, then the sheet
        // is laid out on its own.
        const auto parentSheet = currentSheet;
        currentSheet = child;
        std::vector<std::vector<PinRef>> insideNets;
        for (size_t k = 0; k < crossings.size(); ++k)
        {
            auto pins = crossings[k].in;
            pins.push_back({ bubbleIndex[k], 0 });
            if (pins.size() >= 2)
                insideNets.push_back(pins);
        }
        wireNetsOnCurrentSheet(insideNets);
        if (prefs::isOn("layout.subdiagram_inner_layout"))
            autoLayoutInstances({});
        currentSheet = parentSheet;
        routeSignature.clear();
        const auto finalBlock = blockForSheet(child); // the inner layout renumbered instances
        if (finalBlock < 0)
        {
            error = "Sub-diagram was created, but its block could not be found after layout.";
            forceDeferredRepaint();
            return {};
        }

        for (const auto& probeId : droppedProbes)
            if (onProbeChanged) onProbeChanged(probeId, {}, {});
        if (onStatus) onStatus("Made sub-diagram " + name + " (" + instances[(size_t)finalBlock].refdes + ") with "
                               + juce::String((int)crossings.size()) + " pin(s).");
        forceDeferredRepaint();
        return blockJson(finalBlock);
    }

    // Puts a block's contents back on its parent sheet, centred where the
    // block was, inside a group box of the same name.
    juce::String expandSubDiagram(int blockIndex, juce::String& error)
    {
        if (blockIndex < 0 || blockIndex >= (int)instances.size() || instances[(size_t)blockIndex].symbolId != "sub_block")
        {
            error = "Not a sub-diagram block.";
            return {};
        }
        const auto block = instances[(size_t)blockIndex];
        const auto parent = block.sheet;
        const auto child = block.childSheet;
        const auto netNames = computeNetNames();

        std::set<juce::String> portNets;
        for (int k = 0; k < (int)block.ports.size(); ++k)
            portNets.insert(netFor({ blockIndex, k }, netNames));

        std::set<int> dead { blockIndex }, moved;
        juce::Rectangle<float> box;
        bool first = true;
        for (int i = 0; i < (int)instances.size(); ++i)
        {
            if (instances[(size_t)i].sheet != child)
                continue;
            if (instances[(size_t)i].symbolId == "block_port") { dead.insert(i); continue; }
            moved.insert(i);
            const auto p = instances[(size_t)i].position;
            box = first ? juce::Rectangle<float>(p.x, p.y, 1.0f, 1.0f) : box.getUnion(juce::Rectangle<float>(p.x, p.y, 1.0f, 1.0f));
            first = false;
        }
        const auto offset = snapToGrid(block.position - box.getCentre());

        // Terminals of each port net on the merged sheet, before indices change.
        std::vector<std::vector<PinRef>> portNetPins(portNets.size());
        {
            int n = 0;
            for (const auto& net : portNets)
            {
                for (int i = 0; i < (int)instances.size(); ++i)
                {
                    if (dead.count(i) != 0 || (instances[(size_t)i].sheet != parent && moved.count(i) == 0))
                        continue;
                    const auto symbol = symbolForInstance(instances[(size_t)i]);
                    for (int p = 0; p < (int)symbol.pins.size(); ++p)
                        if (netFor({ i, p }, netNames) == net)
                            portNetPins[(size_t)n].push_back({ i, p });
                }
                ++n;
            }
        }

        std::set<int> deadJunctions;
        for (int j = 0; j < (int)junctions.size(); ++j)
        {
            const auto sheet = junctionSheet(j);
            if (sheet != parent && sheet != child)
                continue;
            if (portNets.count(netForNode(WireNode::forJunction(j), netNames)) != 0)
                deadJunctions.insert(j);
            else if (sheet == child)
            {
                junctionSheets.resize(junctions.size());
                junctionSheets[(size_t)j] = parent;
                junctions[(size_t)j] += offset;
            }
        }
        wires.erase(std::remove_if(wires.begin(), wires.end(), [&](const Wire& w) {
            const auto sheet = nodeSheet(w.a);
            return (sheet == parent || sheet == child) && portNets.count(netForNode(w.a, netNames)) != 0;
        }), wires.end());

        for (int i : moved)
        {
            instances[(size_t)i].sheet = parent;
            instances[(size_t)i].position += offset;
        }

        juce::StringArray droppedProbes;
        const auto map = removeInstancesAndJunctions(dead, deadJunctions, droppedProbes);
        for (auto& pins : portNetPins)
            for (auto& pin : pins)
                pin.instanceIndex = map[(size_t)pin.instanceIndex];

        const auto viewing = currentSheet;
        currentSheet = parent;
        wireNetsOnCurrentSheet(portNetPins);

        Group group;
        group.id = nextGroupId();
        group.name = block.value;
        group.category = "expanded_subdiagram";
        for (int i : moved)
            if (map[(size_t)i] >= 0)
                group.memberInstances.push_back(map[(size_t)i]);
        if (!group.memberInstances.empty())
            groups.push_back(group);

        if (prefs::isOn("layout.relayout_after_expand"))
        {
            const auto keep = currentSheet;
            currentSheet = parent;
            autoLayoutInstances({});
            currentSheet = keep;
        }
        currentSheet = viewing == child ? parent : viewing;
        routeSignature.clear();
        for (const auto& probeId : droppedProbes)
            if (onProbeChanged) onProbeChanged(probeId, {}, {});
        if (onStatus) onStatus("Expanded sub-diagram " + block.value + " back onto " + sheetName(parent) + ".");
        forceDeferredRepaint();
        return "{ \"expanded\": " + quote(block.refdes) + ", \"name\": " + quote(block.value)
             + ", \"sheet\": " + quote(sheetName(parent)) + ", \"group\": " + quote(group.id) + " }";
    }

    juce::String blockJson(int blockIndex) const
    {
        const auto& block = instances[(size_t)blockIndex];
        juce::String text;
        text << "{ \"refdes\": " << quote(block.refdes)
             << ", \"name\": " << quote(block.value)
             << ", \"onSheet\": " << quote(sheetName(block.sheet))
             << ", \"childSheet\": " << quote(block.childSheet)
             << ", \"ports\": [";
        for (size_t k = 0; k < block.ports.size(); ++k)
            text << (k == 0 ? "" : ", ") << "{ \"name\": " << quote(block.ports[k].name)
                 << ", \"side\": " << quote(block.ports[k].rightSide ? "output" : "input") << " }";
        text << "], \"members\": [";
        bool firstMember = true;
        for (const auto& instance : instances)
            if (instance.sheet == block.childSheet && instance.symbolId != "block_port" && !schematic::isPowerSymbol(instance.symbolId))
            {
                text << (firstMember ? "" : ", ") << quote(instance.refdes);
                firstMember = false;
            }
        text << "] }";
        return text;
    }

    juce::File fileForUserBlockName(const juce::String& name) const
    {
        return userBlockLibraryFolder().getChildFile(safeFileStem(name) + ".block.json");
    }

    juce::File findUserBlockFile(const juce::String& name) const
    {
        const auto direct = fileForUserBlockName(name);
        if (direct.existsAsFile())
            return direct;

        const auto files = userBlockLibraryFolder().findChildFiles(juce::File::findFiles, false, "*.block.json");
        for (const auto& file : files)
        {
            const auto parsed = juce::JSON::parse(file);
            if (const auto* object = parsed.getDynamicObject())
                if (stringProperty(*object, "name", {}).equalsIgnoreCase(name))
                    return file;
        }
        return {};
    }

    juce::String saveUserBlock(int blockIndex, juce::String name, const juce::String& description, const juce::String& category, juce::String& error) const
    {
        if (blockIndex < 0 || blockIndex >= (int)instances.size() || instances[(size_t)blockIndex].symbolId != "sub_block")
        {
            error = "Choose a sub-diagram block to save.";
            return {};
        }

        const auto& block = instances[(size_t)blockIndex];
        name = name.trim().isNotEmpty() ? name.trim() : block.value;
        const auto parsedCircuit = juce::JSON::parse(buildCircuitJson());
        if (parsedCircuit.getDynamicObject() == nullptr)
        {
            error = "The current circuit could not be serialized.";
            return {};
        }

        auto* root = new juce::DynamicObject();
        root->setProperty("schema", "djehuti_electronics_user_block");
        root->setProperty("version", 1);
        root->setProperty("name", name);
        root->setProperty("description", description);
        root->setProperty("category", category);
        root->setProperty("sourceBlock", block.refdes);
        root->setProperty("sourceSheet", block.childSheet);
        root->setProperty("circuit", parsedCircuit);

        auto folder = userBlockLibraryFolder();
        if (!folder.createDirectory())
        {
            error = "Could not create user block library folder: " + folder.getFullPathName();
            return {};
        }

        const auto file = fileForUserBlockName(name);
        if (!file.replaceWithText(juce::JSON::toString(juce::var(root), true)))
        {
            error = "Could not write " + file.getFullPathName();
            return {};
        }

        return "{ \"name\": " + quote(name) + ", \"file\": " + quote(file.getFullPathName())
            + ", \"block\": " + blockJson(blockIndex) + " }";
    }

    juce::String listUserBlocks(const juce::String& query) const
    {
        const auto q = query.trim().toLowerCase();
        auto files = userBlockLibraryFolder().findChildFiles(juce::File::findFiles, false, "*.block.json");
        juce::String list = "\"blocks\": [";
        bool first = true;
        for (const auto& file : files)
        {
            const auto parsed = juce::JSON::parse(file);
            const auto* object = parsed.getDynamicObject();
            if (object == nullptr)
                continue;
            const auto name = stringProperty(*object, "name", file.getFileNameWithoutExtension());
            const auto description = stringProperty(*object, "description", {});
            const auto category = stringProperty(*object, "category", {});
            const auto haystack = (name + " " + description + " " + category + " " + stringProperty(*object, "sourceSheet", {})).toLowerCase();
            if (q.isNotEmpty() && !haystack.contains(q))
                continue;
            list << (first ? "" : ", ") << "{ \"name\": " << quote(name)
                 << ", \"category\": " << quote(category)
                 << ", \"description\": " << quote(description)
                 << ", \"file\": " << quote(file.getFullPathName()) << " }";
            first = false;
        }
        list << "]";
        return list;
    }

    juce::String placeUserBlock(const juce::String& libraryName, juce::Point<float> position, const juce::String& instanceName, juce::String& error)
    {
        const auto file = findUserBlockFile(libraryName);
        if (!file.existsAsFile())
        {
            error = "No saved user block named " + libraryName + ".";
            return {};
        }

        const auto root = juce::JSON::parse(file);
        const auto* object = root.getDynamicObject();
        const auto* circuit = object != nullptr ? object->getProperty("circuit").getDynamicObject() : nullptr;
        const auto* componentArray = circuit != nullptr ? circuit->getProperty("components").getArray() : nullptr;
        if (object == nullptr || circuit == nullptr || componentArray == nullptr)
        {
            error = "User block file is not valid: " + file.getFullPathName();
            return {};
        }

        const auto sourceSheet = stringProperty(*object, "sourceSheet", {});
        const auto sourceBlockRef = stringProperty(*object, "sourceBlock", {});
        const auto savedName = stringProperty(*object, "name", libraryName);

        const juce::DynamicObject* sourceBlockObject = nullptr;
        std::set<juce::String> sourceSheets { sourceSheet };
        for (const auto& entry : *componentArray)
            if (const auto* c = entry.getDynamicObject())
                if (stringProperty(*c, "id", {}) == sourceBlockRef)
                    sourceBlockObject = c;

        bool changed = true;
        while (changed)
        {
            changed = false;
            for (const auto& entry : *componentArray)
                if (const auto* c = entry.getDynamicObject())
                    if (sourceSheets.count(stringProperty(*c, "sheet", {})) != 0)
                    {
                        const auto child = stringProperty(*c, "childSheet", {});
                        if (child.isNotEmpty() && sourceSheets.insert(child).second)
                            changed = true;
                    }
        }

        if (sourceBlockObject == nullptr || sourceSheet.isEmpty())
        {
            error = "The saved block has no source sub-diagram.";
            return {};
        }

        int highestSheet = 0;
        for (const auto& instance : instances)
            if (instance.childSheet.startsWith("S") && instance.childSheet.substring(1).containsOnly("0123456789"))
                highestSheet = std::max(highestSheet, instance.childSheet.substring(1).getIntValue());
        std::map<juce::String, juce::String> sheetMap;
        for (const auto& sheet : sourceSheets)
            sheetMap[sheet] = "S" + juce::String(++highestSheet);

        std::map<juce::String, int> refMap;
        std::map<juce::String, int> junctionMap;

        Instance block;
        block.symbolId = "sub_block";
        block.refdes = nextRefdesFor("sub_block");
        block.value = instanceName.trim().isNotEmpty() ? instanceName.trim() : savedName;
        block.family = familyFor("sub_block");
        block.position = snapToGrid(position);
        block.sheet = currentSheet;
        block.childSheet = sheetMap[sourceSheet];
        if (const auto* ports = sourceBlockObject->getProperty("ports").getArray())
            for (const auto& port : *ports)
                block.ports.push_back({ port.getProperty("name", {}).toString(),
                                        port.getProperty("side", {}).toString() == "right" });
        if (const auto* params = sourceBlockObject->getProperty("params").getDynamicObject())
            for (const auto& property : params->getProperties())
                block.params[property.name.toString()] = property.value.toString();
        const auto blockIndex = (int)instances.size();
        instances.push_back(block);

        for (const auto& entry : *componentArray)
        {
            const auto* c = entry.getDynamicObject();
            if (c == nullptr || sourceSheets.count(stringProperty(*c, "sheet", {})) == 0)
                continue;

            Instance instance;
            instance.symbolId = stringProperty(*c, "symbol", {});
            if (!schematic::isSupportedSymbol(instance.symbolId))
                continue;
            const auto oldRef = stringProperty(*c, "id", {});
            instance.refdes = nextRefdesFor(instance.symbolId);
            instance.value = stringProperty(*c, "value", defaultValueFor(instance.symbolId));
            instance.frequency = stringProperty(*c, "frequency", defaultFrequencyFor(instance.symbolId));
            instance.busName = stringProperty(*c, "busName", defaultBusNameFor(instance.symbolId));
            instance.position = { floatProperty(*c, "x", 120.0f), floatProperty(*c, "y", 120.0f) };
            instance.rotation = schematic::normalizedRotation((int)floatProperty(*c, "rotation", 0.0f));
            instance.busLength = floatProperty(*c, "length", isRailBus(instance.symbolId) ? 420.0f : 0.0f);
            instance.sheet = sheetMap[stringProperty(*c, "sheet", {})];
            const auto childSheet = stringProperty(*c, "childSheet", {});
            if (sheetMap.count(childSheet) != 0)
                instance.childSheet = sheetMap[childSheet];
            if (const auto* ports = c->getProperty("ports").getArray())
                for (const auto& port : *ports)
                    instance.ports.push_back({ port.getProperty("name", {}).toString(),
                                               port.getProperty("side", {}).toString() == "right" });
            if (const auto* params = c->getProperty("params").getDynamicObject())
                for (const auto& property : params->getProperties())
                    instance.params[property.name.toString()] = property.value.toString();
            if (const auto* component = c->getProperty("component").getDynamicObject())
            {
                instance.family = stringProperty(*component, "family", familyFor(instance.symbolId));
                instance.manufacturerPart = stringProperty(*component, "manufacturerPart", {});
            }
            else
            {
                instance.family = familyFor(instance.symbolId);
            }

            refMap[oldRef] = (int)instances.size();
            instances.push_back(std::move(instance));
        }

        for (auto& [key, value] : instances[(size_t)blockIndex].params)
        {
            if (!key.startsWith("paramTarget."))
                continue;
            const auto dot = value.indexOfChar('.');
            if (dot <= 0)
                continue;
            const auto oldRef = value.substring(0, dot);
            if (refMap.count(oldRef) != 0)
                value = instances[(size_t)refMap[oldRef]].refdes + value.substring(dot);
        }

        if (const auto* junctionArray = circuit->getProperty("junctions").getArray())
        {
            for (int j = 0; j < (int)junctionArray->size(); ++j)
            {
                const auto* entry = (*junctionArray)[j].getDynamicObject();
                if (entry == nullptr || sourceSheets.count(stringProperty(*entry, "sheet", {})) == 0)
                    continue;
                const auto newIndex = (int)junctions.size();
                junctions.push_back({ floatProperty(*entry, "x", 0.0f), floatProperty(*entry, "y", 0.0f) });
                junctionSheets.push_back(sheetMap[stringProperty(*entry, "sheet", {})]);
                junctionMap["N" + juce::String(j + 1)] = newIndex;
            }
        }

        auto nodeFromLabel = [&](const juce::String& label, WireNode& node) -> bool {
            if (junctionMap.count(label) != 0)
            {
                node = WireNode::forJunction(junctionMap[label]);
                return true;
            }
            const auto dot = label.lastIndexOfChar('.');
            if (dot <= 0 || dot >= label.length() - 1)
                return false;
            const auto oldRef = label.substring(0, dot);
            if (refMap.count(oldRef) == 0)
                return false;
            const auto pinName = label.substring(dot + 1);
            const auto newIndex = refMap[oldRef];
            const auto symbol = symbolForInstance(instances[(size_t)newIndex]);
            for (int p = 0; p < (int)symbol.pins.size(); ++p)
                if (symbol.pins[(size_t)p].name == pinName)
                {
                    node = WireNode::forPin({ newIndex, p });
                    return true;
                }
            return false;
        };

        if (const auto* wireArray = circuit->getProperty("wires").getArray())
        {
            for (const auto& entry : *wireArray)
            {
                const auto* w = entry.getDynamicObject();
                if (w == nullptr)
                    continue;
                Wire wire;
                if (nodeFromLabel(stringProperty(*w, "a", {}), wire.a) && nodeFromLabel(stringProperty(*w, "b", {}), wire.b))
                    wires.push_back(wire);
            }
        }

        if (const auto* groupArray = circuit->getProperty("groups").getArray())
        {
            for (const auto& entry : *groupArray)
            {
                const auto* g = entry.getDynamicObject();
                const auto* members = g != nullptr ? g->getProperty("members").getArray() : nullptr;
                if (g == nullptr || members == nullptr)
                    continue;
                Group group;
                group.id = nextGroupId();
                group.name = stringProperty(*g, "name", "Group");
                group.category = stringProperty(*g, "category", "user_group");
                group.notes = stringProperty(*g, "notes", {});
                for (const auto& member : *members)
                    if (refMap.count(member.toString()) != 0)
                        group.memberInstances.push_back(refMap[member.toString()]);
                if (!group.memberInstances.empty())
                    groups.push_back(group);
            }
        }

        selectedInstance = blockIndex;
        selectedInstances.clear();
        selectedInstances.add(blockIndex);
        selectedGroup = -1;
        routeSignature.clear();
        notifySelection();
        if (onStatus) onStatus("Placed user library block " + block.value + " as " + block.refdes + ".");
        forceDeferredRepaint();
        return blockJson(blockIndex);
    }

    struct UserBlockInfo
    {
        juce::String name;
        juce::String category;
    };

    std::vector<UserBlockInfo> userBlocks() const
    {
        std::vector<UserBlockInfo> blocks;
        const auto files = userBlockLibraryFolder().findChildFiles(juce::File::findFiles, false, "*.block.json");
        for (const auto& file : files)
        {
            const auto parsed = juce::JSON::parse(file);
            if (const auto* object = parsed.getDynamicObject())
            {
                auto category = stringProperty(*object, "category", "User Blocks").trim();
                if (category.isEmpty())
                    category = "User Blocks";
                blocks.push_back({ stringProperty(*object, "name", file.getFileNameWithoutExtension()), category });
            }
        }
        std::sort(blocks.begin(), blocks.end(), [](const UserBlockInfo& a, const UserBlockInfo& b) {
            const auto categoryCompare = a.category.compareIgnoreCase(b.category);
            return categoryCompare == 0 ? a.name.compareIgnoreCase(b.name) < 0 : categoryCompare < 0;
        });
        return blocks;
    }

    void promptSaveBlockToLibrary(int blockIndex)
    {
        if (blockIndex < 0 || blockIndex >= (int)instances.size() || instances[(size_t)blockIndex].symbolId != "sub_block")
            return;

        auto* dialog = new juce::AlertWindow("Save User Library Block",
                                             "This saves the sub-diagram as a reusable copy template.",
                                             juce::AlertWindow::NoIcon);
        dialog->addTextEditor("name", instances[(size_t)blockIndex].value, "Name");
        dialog->addTextEditor("category", "User Blocks", "Category");
        dialog->addTextEditor("description", {}, "Description");
        dialog->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
        dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        juce::Component::SafePointer<juce::AlertWindow> safe(dialog);
        dialog->enterModalState(true, juce::ModalCallbackFunction::create([this, safe, blockIndex](int result) {
            if (result != 1 || safe == nullptr)
                return;
            juce::String error;
            const auto saved = saveUserBlock(blockIndex,
                                             safe->getTextEditor("name")->getText(),
                                             safe->getTextEditor("description")->getText(),
                                             safe->getTextEditor("category")->getText(),
                                             error);
            if (onStatus)
                onStatus(saved.isNotEmpty() ? "Saved user library block." : "Could not save library block: " + error);
        }), true);
    }

    void promptExposeBlockParameter(int blockIndex)
    {
        if (blockIndex < 0 || blockIndex >= (int)instances.size() || instances[(size_t)blockIndex].symbolId != "sub_block")
            return;

        auto* dialog = new juce::AlertWindow("Expose Block Parameter",
                                             "Choose an internal component parameter to show on this block.",
                                             juce::AlertWindow::NoIcon);
        dialog->addTextEditor("name", "voltage", "Block parameter name");
        dialog->addTextEditor("targetRefdes", {}, "Internal component refdes");
        dialog->addTextEditor("targetParameter", "value", "Internal parameter key");
        dialog->addButton("Expose", 1, juce::KeyPress(juce::KeyPress::returnKey));
        dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        juce::Component::SafePointer<juce::AlertWindow> safe(dialog);
        const auto blockRefdes = instances[(size_t)blockIndex].refdes;
        dialog->enterModalState(true, juce::ModalCallbackFunction::create([this, safe, blockRefdes](int result) {
            if (result != 1 || safe == nullptr)
                return;
            juce::String error;
            const auto ok = exposeBlockParameter(blockRefdes,
                                                 safe->getTextEditor("name")->getText(),
                                                 safe->getTextEditor("targetRefdes")->getText(),
                                                 safe->getTextEditor("targetParameter")->getText(),
                                                 error);
            if (onStatus)
                onStatus(ok ? "Exposed block parameter." : "Could not expose parameter: " + error);
        }), true);
    }

    void promptSubDiagramFromSelection()
    {
        std::vector<int> members;
        for (int index : selectedInstances)
            members.push_back(index);
        auto defaultName = juce::String("Sub-diagram");
        int fromGroup = -1;
        if (selectedGroup >= 0 && selectedGroup < (int)groups.size())
        {
            fromGroup = selectedGroup;
            defaultName = groups[(size_t)selectedGroup].name;
            members = groups[(size_t)selectedGroup].memberInstances;
        }
        auto* dialog = new juce::AlertWindow("Make Sub-Diagram", "The selection becomes one block; wires crossing its edge become pins.", juce::AlertWindow::NoIcon);
        dialog->addTextEditor("name", defaultName, "Name");
        dialog->addButton("Create", 1, juce::KeyPress(juce::KeyPress::returnKey));
        dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        dialog->enterModalState(true, juce::ModalCallbackFunction::create([this, dialog, members, fromGroup](int result) {
            std::unique_ptr<juce::AlertWindow> owner(dialog);
            if (result != 1)
                return;
            if (fromGroup >= 0 && fromGroup < (int)groups.size())
                groups.erase(groups.begin() + fromGroup);
            juce::String error;
            if (createSubDiagram(members, owner->getTextEditor("name")->getText(), error).isEmpty() && onStatus)
                onStatus("Could not make sub-diagram: " + error);
        }), true);
    }

    void promptRenameBlock(int blockIndex)
    {
        auto* dialog = new juce::AlertWindow("Rename Sub-Diagram", {}, juce::AlertWindow::NoIcon);
        dialog->addTextEditor("name", instances[(size_t)blockIndex].value, "Name");
        dialog->addButton("Apply", 1, juce::KeyPress(juce::KeyPress::returnKey));
        dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        const auto refdes = instances[(size_t)blockIndex].refdes;
        dialog->enterModalState(true, juce::ModalCallbackFunction::create([this, dialog, refdes](int result) {
            std::unique_ptr<juce::AlertWindow> owner(dialog);
            const auto index = blockIndexFor(refdes);
            const auto name = owner->getTextEditor("name")->getText().trim();
            if (result == 1 && index >= 0 && name.isNotEmpty())
            {
                instances[(size_t)index].value = name;
                forceDeferredRepaint();
            }
        }), true);
    }

    // Breadcrumb hit areas in view coordinates, rebuilt every paint.
    std::vector<std::pair<juce::Rectangle<float>, juce::String>> breadcrumbAreas;

    void drawBreadcrumb(juce::Graphics& g)
    {
        breadcrumbAreas.clear();
        if (currentSheet.isEmpty())
            return;
        const juce::Font font(13.0f, juce::Font::bold);
        g.setFont(font);
        float x = 12.0f;
        const float y = 10.0f;
        const auto path = sheetPath();
        for (int i = 0; i < path.size(); ++i)
        {
            const auto label = sheetName(path[i]);
            const auto w = font.getStringWidthFloat(label) + 16.0f;
            const juce::Rectangle<float> r { x, y, w, 22.0f };
            const auto last = i == path.size() - 1;
            g.setColour(last ? juce::Colour(0xff78dcca) : juce::Colour(0xff26323d));
            g.fillRoundedRectangle(r, 5.0f);
            g.setColour(last ? juce::Colour(0xff0e141a) : juce::Colour(0xffdce9ee));
            g.drawText(label, r.toNearestInt(), juce::Justification::centred);
            if (!last)
            {
                breadcrumbAreas.push_back({ r, path[i] });
                g.setColour(juce::Colour(0xff93a7b0));
                g.drawText(">", juce::Rectangle<float>(r.getRight(), y, 16.0f, 22.0f).toNearestInt(), juce::Justification::centred);
            }
            x = r.getRight() + 16.0f;
        }
    }

    // ---- Simulation: the schematic as a circuit_sim::Circuit ----

    // A part's property value through the part catalog (stored field or param,
    // falling back to the catalog default).
    juce::String partValue(const Instance& instance, const juce::String& key) const
    {
        const auto* spec = parts::findParam(instance.symbolId, key);
        juce::String value;
        if (spec != nullptr)
        {
            switch (spec->storage)
            {
                case parts::Storage::Value: value = instance.value; break;
                case parts::Storage::Frequency: value = instance.frequency; break;
                case parts::Storage::BusName: value = instance.busName; break;
                case parts::Storage::Family: value = instance.family; break;
                case parts::Storage::ManufacturerPart: value = instance.manufacturerPart; break;
                case parts::Storage::Param:
                {
                    const auto found = instance.params.find(key);
                    if (found != instance.params.end()) value = found->second;
                    break;
                }
            }
            if (value.trim().isEmpty())
                value = spec->defaultValue;
        }
        else if (const auto found = instance.params.find(key); found != instance.params.end())
            value = found->second;
        return value.trim();
    }

    void setPartValue(Instance& instance, const juce::String& key, const juce::String& value)
    {
        const auto* spec = parts::findParam(instance.symbolId, key);
        const auto storage = spec != nullptr ? spec->storage : parts::Storage::Param;
        switch (storage)
        {
            case parts::Storage::Value: instance.value = value; break;
            case parts::Storage::Frequency: instance.frequency = value; break;
            case parts::Storage::BusName: instance.busName = value; break;
            case parts::Storage::Family: instance.family = value; break;
            case parts::Storage::ManufacturerPart: instance.manufacturerPart = value; break;
            case parts::Storage::Param: instance.params[key] = value; break;
        }
    }

    static double parseQuantity(juce::String text, double fallback, bool* ok = nullptr)
    {
        text = text.trim();
        if (text.containsChar('V') && text.upToFirstOccurrenceOf("V", false, false).containsOnly("0123456789")
            && text.fromFirstOccurrenceOf("V", false, false).containsOnly("0123456789") && text.fromFirstOccurrenceOf("V", false, false).isNotEmpty())
            text = text.replace("V", "."); // 5V1 = 5.1
        double value = 0.0;
        const auto parsed = circuit_sim::parseValue(text.toStdString(), value);
        if (ok != nullptr) *ok = parsed;
        return parsed ? value : fallback;
    }

    public:
    struct SimNetlist
    {
        circuit_sim::Circuit circuit;
        std::map<juce::String, circuit_sim::Node> nodeOfNet;
        std::map<juce::String, int> elementOfPart; // refdes -> element (ammeter source, source)
        juce::StringArray warnings;
        int audioInputBranch = -1;
        int audioOutputNode = -1;
    };

    // `ohmmeter` >= 0 builds the resistance-measurement circuit for that
    // multimeter: independent sources off, a 1 mA test current into HI.
    SimNetlist buildSimNetlist(int ohmmeter = -1) const
    {
        SimNetlist sim;
        const auto netNames = computeNetNames();
        auto node = [&](int instanceIndex, const juce::String& pinName) -> circuit_sim::Node {
            const auto symbol = symbolForInstance(instances[(size_t)instanceIndex]);
            for (int p = 0; p < (int)symbol.pins.size(); ++p)
                if (symbol.pins[(size_t)p].name == pinName)
                {
                    const auto net = netFor({ instanceIndex, p }, netNames);
                    if (net == "0")
                        return 0;
                    if (net == "floating")
                        return sim.circuit.addNode();
                    const auto found = sim.nodeOfNet.find(net);
                    if (found != sim.nodeOfNet.end())
                        return found->second;
                    const auto n = sim.circuit.addNode();
                    sim.nodeOfNet[net] = n;
                    return n;
                }
            return sim.circuit.addNode();
        };
        auto number = [&](const Instance& inst, const juce::String& key, double fallback) {
            bool ok = true;
            juce::String val = partValue(inst, key);
            double v = parseQuantity(val, fallback, &ok);
            if (ok) return v;

            juce::String modelName = partValue(inst, "value");
            if (const auto* def = spice_library::findModel(modelName))
            {
                auto it = def->parameters.find(key.toUpperCase());
                if (it != def->parameters.end())
                {
                    bool ok2 = true;
                    double v2 = parseQuantity(it->second, fallback, &ok2);
                    if (ok2) return v2;
                }
            }

            sim.warnings.add(inst.refdes + ": \"" + val + "\" is not a valid " + key + " (and not found in model " + modelName + "); using " + juce::String(fallback));
            return fallback;
        };
        auto waveform = [&](const Instance& inst) {
            circuit_sim::Waveform w;
            const auto kind = partValue(inst, "waveform");
            using K = circuit_sim::Waveform::Kind;
            w.kind = kind == "Square" ? K::Square : kind == "Pulse" ? K::Pulse : kind == "PWL" ? K::Pwl : kind == "Exp" ? K::Exp : K::Sine;
            w.amplitude = number(inst, "amplitude", 1.0);
            w.frequency = number(inst, "frequency", 1000.0);
            w.offset = number(inst, "offset", 0.0);
            w.phaseDegrees = number(inst, "phase", 0.0);
            w.duty = number(inst, "duty", 0.5);
            w.pulsed = number(inst, "pulsed_value", 1.0);
            w.delay = number(inst, "delay", 0.0);
            w.rise = number(inst, "rise", 1e-9);
            w.fall = number(inst, "fall", 1e-9);
            w.width = number(inst, "width", 0.5e-3);
            w.period = number(inst, "period", 1e-3);
            w.tau1 = number(inst, "tau1", 1e-4);
            w.delay2 = number(inst, "delay2", 1e-3);
            w.tau2 = number(inst, "tau2", 1e-4);
            if (w.kind == K::Pwl)
            {
                for (const auto& pair : juce::StringArray::fromTokens(partValue(inst, "pwl"), ",;", ""))
                {
                    const auto parts = juce::StringArray::fromTokens(pair.trim(), " \t", "");
                    double t = 0.0, v = 0.0;
                    if (parts.size() >= 2 && circuit_sim::parseValue(parts[0].toStdString(), t) && circuit_sim::parseValue(parts[1].toStdString(), v))
                        if (w.points.empty() || t > w.points.back().first)
                            w.points.push_back({ t, v });
                }
                if (w.points.empty())
                    sim.warnings.add(inst.refdes + ": the PWL points are empty; use pairs like 0 0, 1m 5, 2m 5.");
            }
            w.acMagnitude = w.amplitude != 0.0 ? w.amplitude : 1.0;
            return w;
        };
        auto dcWave = [](double v) { circuit_sim::Waveform w; w.offset = v; return w; };
        const bool measuringOhms = ohmmeter >= 0;

        for (int i = 0; i < (int)instances.size(); ++i)
        {
            const auto& inst = instances[(size_t)i];
            const auto& id = inst.symbolId;
            const auto name = inst.refdes.toStdString();
            auto& c = sim.circuit;
            int element = -1;

            if (id == "resistor")
            {
                element = c.addResistor(name, node(i, "1"), node(i, "2"), number(inst, "value", 10e3));
                c.elements()[(size_t)element].tc1 = number(inst, "tempco", 0.0) * 1e-6;
            }
            else if (id == "potentiometer")
            {
                const auto total = number(inst, "value", 10e3);
                // The DSP engine parameter is the position. We pass the instance ID + "_position" as param ID
                c.addVariableResistor(name + "_a", node(i, "1"), node(i, "W"), total, name + "_position", false);
                element = c.addVariableResistor(name + "_b", node(i, "W"), node(i, "2"), total, name + "_position", true);
            }
            else if (id == "capacitor" || id == "variable_capacitor")
                element = c.addCapacitor(name, node(i, "1"), node(i, "2"), number(inst, "value", 1e-6));
            else if (id == "capacitor_polarized")
                element = c.addCapacitor(name, node(i, "+"), node(i, "-"), number(inst, "value", 10e-6));
            else if (id == "inductor")
                element = c.addInductor(name, node(i, "1"), node(i, "2"), number(inst, "value", 10e-3));
            else if (id == "coupled_inductor")
            {
                const auto l = number(inst, "value", 10e-3);
                const auto a = c.addInductor(name + "_1", node(i, "1A"), node(i, "1B"), l);
                const auto b = c.addInductor(name + "_2", node(i, "2A"), node(i, "2B"), l);
                element = c.addCoupling(name, a, b, juce::jlimit(0.0, 0.9999, number(inst, "coupling", 0.99)));
            }
            else if (id == "transformer")
            {
                const auto ratio = partValue(inst, "value");
                const auto primaryTurns = std::max(1e-6, ratio.upToFirstOccurrenceOf(":", false, false).getDoubleValue());
                const auto secondaryTurns = std::max(1e-6, ratio.fromFirstOccurrenceOf(":", false, false).getDoubleValue());
                const auto lp = number(inst, "primary_inductance", 1.0);
                const auto ls = lp * std::pow(secondaryTurns / primaryTurns, 2.0);
                const auto a = c.addInductor(name + "_p", node(i, "P1"), node(i, "P2"), lp);
                const auto b = c.addInductor(name + "_s", node(i, "S1"), node(i, "S2"), ls);
                element = c.addCoupling(name, a, b, juce::jlimit(0.0, 0.9999, number(inst, "coupling", 0.999)));
            }
            else if (id == "diode" || id == "schottky_diode" || id == "zener_diode")
            {
                circuit_sim::DiodeModel m;
                m.saturationCurrent = number(inst, "saturation_current", 1e-14);
                m.emission = number(inst, "emission", 1.0);
                m.transitTime = number(inst, "transit_time", 0.0);
                if (id == "zener_diode")
                    m.breakdownVoltage = number(inst, "value", 5.1);
                element = c.addDiode(name, node(i, "A"), node(i, "K"), m);
                if (element >= 0)
                {
                    auto selected = partValue(inst, "value").trim();
                    if (id == "zener_diode" && (selected.equalsIgnoreCase("5V1") || selected == "5.1"))
                        selected = "ZENER_5V1";
                    c.elements()[(size_t)element].modelName = selected.toStdString();
                }
                if (const auto cj = number(inst, "cj0", 0.0); cj > 0.0)
                    c.addCapacitor(name + ".cj", node(i, "A"), node(i, "K"), cj);
            }
            else if (id == "led")
            {
                // Forward drop near 10 mA by colour.
                const auto colour = partValue(inst, "value");
                const auto vf = colour == "Green" ? 2.1 : colour == "Yellow" ? 2.0 : (colour == "Blue" || colour == "White") ? 3.0 : 1.8;
                circuit_sim::DiodeModel m;
                m.emission = 2.0;
                m.saturationCurrent = 0.01 / std::exp(vf / (m.emission * 0.025852));
                element = c.addDiode(name, node(i, "A"), node(i, "K"), m);
                if (element >= 0)
                    c.elements()[(size_t)element].modelName = ("LED_" + colour.toUpperCase()).toStdString();
            }
            else if (id == "audio_out")
            {
                sim.audioOutputNode = node(i, "1");
            }
            else if (id == "battery" || id == "voltage_source" || id == "audio_in")
            {
                double dcVal = (id == "audio_in") ? 0.0 : number(inst, "value", 5.0);
                element = c.addVoltageSource(name, node(i, "+"), node(i, "-"), dcWave(measuringOhms ? 0.0 : dcVal));
                if (id == "audio_in") sim.audioInputBranch = element;
            }
            else if (id == "current_source")
            {
                if (!measuringOhms)
                    element = c.addCurrentSource(name, node(i, "+"), node(i, "-"), dcWave(number(inst, "value", 1e-3)));
            }
            else if (id == "ac_voltage_source" || id == "signal_source")
            {
                auto w = measuringOhms ? dcWave(0.0) : waveform(inst);
                element = id == "signal_source" ? c.addVoltageSource(name, node(i, "OUT"), node(i, "REF"), w)
                                                : c.addVoltageSource(name, node(i, "+"), node(i, "-"), w);
            }
            else if (id == "ac_current_source")
            {
                if (!measuringOhms)
                    element = c.addCurrentSource(name, node(i, "+"), node(i, "-"), waveform(inst));
            }
            else if (id == "behavioral_voltage_source")
                element = c.addBehavioralVoltageSource(name, node(i, "+"), node(i, "-"), partValue(inst, "value").toStdString());
            else if (id == "behavioral_current_source")
                element = c.addBehavioralCurrentSource(name, node(i, "+"), node(i, "-"), partValue(inst, "value").toStdString());
            else if (id == "vcvs")
                element = c.addVcvs(name, node(i, "+"), node(i, "-"), node(i, "CP+"), node(i, "CP-"), number(inst, "value", 10.0));
            else if (id == "vccs")
                element = c.addVccs(name, node(i, "+"), node(i, "-"), node(i, "CP+"), node(i, "CP-"), number(inst, "value", 1e-3));
            else if (id == "ccvs" || id == "cccs")
            {
                const auto sense = c.addVoltageSource(name + "_sense", node(i, "S+"), node(i, "S-"), dcWave(0.0));
                element = id == "ccvs" ? c.addCcvs(name, node(i, "+"), node(i, "-"), sense, number(inst, "value", 1e3))
                                       : c.addCccs(name, node(i, "+"), node(i, "-"), sense, number(inst, "value", 10.0));
            }
            else if (id == "opamp_generic" || id == "opamp_741" || id == "comparator_generic" || id == "comparator_lm311")
            {
                circuit_sim::OpAmpModel m;
                m.gain = number(inst, "gain", id.startsWith("comparator") ? 1e6 : 2e5);
                m.railDrop = number(inst, "headroom", 1.5);
                const auto gbw = number(inst, "gbw", 1e6);
                const auto outPin = id == "comparator_lm311" ? "COL_OUT" : "OUT";
                if (id == "opamp_741" && gbw > 0.0 && m.gain > 0.0)
                {
                    // Like a real op amp: a linear gain stage, the dominant pole at GBW / A0 on
                    // the internal node, then an output stage that limits at the rails.
                    const auto stage = c.addNode(), pole = c.addNode();
                    auto gainStage = m;
                    gainStage.limited = false;
                    element = c.addOpAmp(name, node(i, "IN+"), node(i, "IN-"), stage, node(i, "V+"), node(i, "V-"), gainStage);
                    c.addResistor(name + ".rp", stage, pole, 1e3);
                    c.addCapacitor(name + ".cp", pole, 0, 1.0 / (2.0 * juce::MathConstants<double>::pi * (gbw / m.gain) * 1e3));
                    circuit_sim::OpAmpModel output;
                    output.gain = 1.0;
                    output.railDrop = m.railDrop;
                    c.addOpAmp(name + ".out", pole, 0, node(i, "OUT"), node(i, "V+"), node(i, "V-"), output);
                    c.elements()[(size_t)c.find(name + ".rp")].noiseless = true;
                }
                else
                    element = c.addOpAmp(name, node(i, "IN+"), node(i, "IN-"), node(i, outPin), node(i, "V+"), node(i, "V-"), m);
                if (element >= 0)
                    c.elements()[(size_t)element].modelName = partValue(inst, "value").toStdString();
            }
            else if (id == "npn" || id == "pnp")
            {
                circuit_sim::BjtModel m;
                m.betaForward = number(inst, "beta", 100.0);
                m.saturationCurrent = number(inst, "saturation_current", 1e-14);
                m.earlyVoltage = number(inst, "early_voltage", 0.0);
                m.transitTime = number(inst, "transit_time", 0.0);
                element = c.addBjt(name, id == "npn", node(i, "C"), node(i, "B"), node(i, "E"), m);
                if (element >= 0)
                    c.elements()[(size_t)element].modelName = partValue(inst, "value").toStdString();
                if (const auto cje = number(inst, "cje", 0.0); cje > 0.0)
                    c.addCapacitor(name + ".cje", node(i, "B"), node(i, "E"), cje);
                if (const auto cjc = number(inst, "cjc", 0.0); cjc > 0.0)
                    c.addCapacitor(name + ".cjc", node(i, "B"), node(i, "C"), cjc);
            }
            else if (id == "nmos" || id == "pmos")
            {
                circuit_sim::MosModel m;
                m.threshold = std::abs(number(inst, "threshold", 2.0));
                m.transconductance = number(inst, "k", 20e-3);
                m.lambda = number(inst, "lambda", 0.01);
                element = c.addMosfet(name, id == "nmos", node(i, "D"), node(i, "G"), node(i, "S"), m);
                if (element >= 0)
                    c.elements()[(size_t)element].modelName = partValue(inst, "value").toStdString();
                if (const auto cgs = number(inst, "cgs", 0.0); cgs > 0.0)
                    c.addCapacitor(name + ".cgs", node(i, "G"), node(i, "S"), cgs);
                if (const auto cgd = number(inst, "cgd", 0.0); cgd > 0.0)
                    c.addCapacitor(name + ".cgd", node(i, "G"), node(i, "D"), cgd);
            }
            else if (id == "njfet" || id == "pjfet")
            {
                const auto idss = number(inst, "idss", 10e-3);
                const auto vp = std::max(0.05, std::abs(number(inst, "pinchoff", 2.0)));
                circuit_sim::JfetModel m;
                m.pinchoff = vp;
                m.idss = idss;
                m.lambda = 0.0;
                element = c.addJfet(name, id == "njfet", node(i, "D"), node(i, "G"), node(i, "S"), m);
                if (element >= 0)
                    c.elements()[(size_t)element].modelName = partValue(inst, "value").toStdString();
            }
            else if (id == "switch_spst")
            {
                element = c.addSwitch(name, node(i, "1"), node(i, "2"), name + "_state");
            }
            else if (id == "switch_spdt")
                element = c.addResistor(name, node(i, "C"), node(i, partValue(inst, "state") == "B" ? "B" : "A"), 1e-3);
            else if (id == "relay_spst")
            {
                c.addResistor(name + "_coil", node(i, "COIL+"), node(i, "COIL-"), number(inst, "coil_resistance", 100.0));
                if (partValue(inst, "state") == "Closed")
                    element = c.addResistor(name, node(i, "1"), node(i, "2"), 1e-3);
            }
            else if (id == "fuse")
                element = c.addResistor(name, node(i, "1"), node(i, "2"), 0.01);
            else if (id == "oscilloscope_2ch")
            {
                c.elements()[(size_t)c.addResistor(name + ".ch1", node(i, "CH1"), node(i, "REF"), 10e6)].noiseless = true;
                element = c.addResistor(name + ".ch2", node(i, "CH2"), node(i, "REF"), 10e6);
                c.elements()[(size_t)element].noiseless = true;
            }
            else if (id == "bode_analyzer")
            {
                c.elements()[(size_t)c.addResistor(name + ".in", node(i, "IN"), node(i, "REF"), 10e6)].noiseless = true;
                element = c.addResistor(name + ".out", node(i, "OUT"), node(i, "REF"), 10e6);
                c.elements()[(size_t)element].noiseless = true;
            }
            else if (id == "digital_multimeter")
            {
                if (i == ohmmeter)
                    element = c.addCurrentSource(name, node(i, "LO"), node(i, "HI"), dcWave(1e-3));
                else if (partValue(inst, "value") == "DC A")
                    element = c.addVoltageSource(name, node(i, "HI"), node(i, "LO"), dcWave(0.0));
                else
                {
                    element = c.addResistor(name + ".in", node(i, "HI"), node(i, "LO"), 10e6);
                    c.elements()[(size_t)element].noiseless = true;
                }
            }
            else if (id.startsWith("logic_"))
                sim.warnings.add(inst.refdes + " (" + parts::displayName(id) + ") is not simulated yet.");

            if (element >= 0)
                sim.elementOfPart[inst.refdes] = element;
        }
        return sim;
    }

public:
    // The circuit for SPICE analytics, with readable net names and the parts
    // the user placed (instruments are loads, not analysis targets).
    analytics::Netlist analyticsNetlist() const
    {
        auto sim = buildSimNetlist();
        analytics::Netlist n;
        n.circuit = sim.circuit;
        n.warnings = sim.warnings;
        const auto netNames = computeNetNames();
        std::set<juce::String> used;
        for (const auto& [net, node] : sim.nodeOfNet)
        {
            auto name = netDisplayName(net, netNames).trim();
            if (name.isEmpty() || used.count(name.toLowerCase()) != 0)
                name = net;
            used.insert(name.toLowerCase());
            n.nets.push_back({ name, node, pinsOnNet(net, netNames).joinIntoString(" ") });
        }
        std::sort(n.nets.begin(), n.nets.end(), [](const analytics::NetInfo& a, const analytics::NetInfo& b) {
            const bool aRaw = a.name.startsWith("n") && a.name.substring(1).containsOnly("0123456789");
            const bool bRaw = b.name.startsWith("n") && b.name.substring(1).containsOnly("0123456789");
            if (aRaw != bRaw) return !aRaw;
            if (aRaw) return a.name.substring(1).getIntValue() < b.name.substring(1).getIntValue();
            return a.name.compareIgnoreCase(b.name) < 0;
        });
        for (const auto& inst : instances)
        {
            if (schematic::isInstrumentSymbol(inst.symbolId))
                continue;
            const auto found = sim.elementOfPart.find(inst.refdes);
            if (found != sim.elementOfPart.end())
                n.parts.push_back({ inst.refdes, inst.symbolId, found->second });
        }
        return n;
    }

    // Every part with each pin's net (readable names, as Analytics shows them),
    // for the PCB. Net markers and sub-diagram plumbing are not parts.
    std::vector<pcb::SchematicPart> pcbParts() const
    {
        const auto netNames = computeNetNames();
        std::map<juce::String, juce::String> names;
        std::set<juce::String> used;
        auto boardNet = [&](const juce::String& net) -> juce::String {
            if (net.isEmpty() || net == "floating") return {};
            if (const auto found = names.find(net); found != names.end()) return found->second;
            auto name = netDisplayName(net, netNames).trim();
            if (name.isEmpty() || used.count(name.toLowerCase()) != 0) name = net;
            used.insert(name.toLowerCase());
            names[net] = name;
            return name;
        };
        std::vector<pcb::SchematicPart> parts;
        for (int i = 0; i < (int)instances.size(); ++i)
        {
            const auto& inst = instances[(size_t)i];
            if (isNetMarker(inst.symbolId) || inst.symbolId == "sub_block" || inst.symbolId == "block_port" || inst.symbolId == "annotation_text")
                continue;
            pcb::SchematicPart part { inst.refdes, inst.symbolId, inst.value, {} };
            const auto symbol = symbolForInstance(inst);
            for (int p = 0; p < (int)symbol.pins.size(); ++p)
                part.pins.push_back({ symbol.pins[(size_t)p].name, boardNet(netFor({ i, p }, netNames)) });
            parts.push_back(part);
        }
        return parts;
    }

private:
    // Pin label (R1.2, SCOPE1.CH1) -> solver node, or -1.
    int simNodeForLabel(const SimNetlist& sim, const juce::String& label, juce::String& error) const
    {
        WireNode n;
        if (!nodeFromLabel(label, n, error))
            return -1;
        const auto net = netForNode(n, computeNetNames());
        if (net == "0")
            return 0;
        const auto found = sim.nodeOfNet.find(net);
        if (found == sim.nodeOfNet.end())
        {
            error = label + " is not connected to anything the simulator sees.";
            return -1;
        }
        return found->second;
    }

    struct WaveStats
    {
        double minimum = 0, maximum = 0, mean = 0, rms = 0, frequency = 0;
    };

    static WaveStats statsOf(const std::vector<double>& t, const std::vector<double>& v, double fromTime)
    {
        WaveStats s;
        std::vector<double> tt, vv;
        for (size_t i = 0; i < t.size(); ++i)
            if (t[i] >= fromTime) { tt.push_back(t[i]); vv.push_back(v[i]); }
        if (vv.empty())
            return s;
        s.minimum = *std::min_element(vv.begin(), vv.end());
        s.maximum = *std::max_element(vv.begin(), vv.end());
        double sum = 0, sq = 0;
        for (auto x : vv) { sum += x; sq += x * x; }
        s.mean = sum / (double)vv.size();
        s.rms = std::sqrt(sq / (double)vv.size());
        // Frequency from rising crossings of the mean.
        std::vector<double> crossings;
        for (size_t i = 1; i < vv.size(); ++i)
            if (vv[i - 1] < s.mean && vv[i] >= s.mean)
            {
                const auto f = (s.mean - vv[i - 1]) / (vv[i] - vv[i - 1]);
                crossings.push_back(tt[i - 1] + f * (tt[i] - tt[i - 1]));
            }
        if (crossings.size() >= 2 && (s.maximum - s.minimum) > 1e-9)
            s.frequency = (double)(crossings.size() - 1) / (crossings.back() - crossings.front());
        return s;
    }

    juce::String netDisplayName(const juce::String& net, const std::map<int, juce::String>& netNames) const
    {
        if (net == "0")
            return "GND";
        for (int i = 0; i < (int)instances.size(); ++i)
            if ((instances[(size_t)i].symbolId == "power_port" || instances[(size_t)i].symbolId == "net_label"
                 || instances[(size_t)i].symbolId == "power_bus")
                && netFor({ i, 0 }, netNames) == net)
                return instances[(size_t)i].busName;
        return net;
    }

    juce::StringArray pinsOnNet(const juce::String& net, const std::map<int, juce::String>& netNames, int limit = 6) const
    {
        juce::StringArray pins;
        for (int i = 0; i < (int)instances.size() && pins.size() < limit; ++i)
        {
            if (isNetMarker(instances[(size_t)i].symbolId))
                continue;
            const auto symbol = symbolForInstance(instances[(size_t)i]);
            for (int p = 0; p < (int)symbol.pins.size() && pins.size() < limit; ++p)
                if (netFor({ i, p }, netNames) == net)
                    pins.add(pinLabel({ i, p }));
        }
        return pins;
    }

    juce::File simulationOutputFile(const juce::String& name) const
    {
        const auto folder = outputDirectory != nullptr ? outputDirectory() : juce::File::getSpecialLocation(juce::File::tempDirectory);
        folder.createDirectory();
        return folder.getChildFile(name);
    }

public:
    // ---- Supply forms: a rail, or a symbol at every pin (same net either way) ----

    void promptExposeBlockParameterFor(const juce::String& blockRefdes)
    {
        const auto block = blockIndexFor(blockRefdes);
        if (block >= 0)
            promptExposeBlockParameter(block);
    }

    // Pins wired to a rail through its taps or its anchor.
    std::vector<PinRef> railAttachedPins(int rail, std::set<int>& taps) const
    {
        for (const auto& wire : wires)
        {
            if (nodeTouchesInstance(wire.a, rail) && wire.b.isJunction()) taps.insert(wire.b.junctionIndex);
            if (nodeTouchesInstance(wire.b, rail) && wire.a.isJunction()) taps.insert(wire.a.junctionIndex);
        }
        std::vector<PinRef> pins;
        auto attached = [&](const WireNode& n) { return nodeTouchesInstance(n, rail) || (n.isJunction() && taps.count(n.junctionIndex) != 0); };
        for (const auto& wire : wires)
        {
            if (attached(wire.a) && wire.b.isPin() && wire.b.pin.instanceIndex != rail) pins.push_back(wire.b.pin);
            if (attached(wire.b) && wire.a.isPin() && wire.a.pin.instanceIndex != rail) pins.push_back(wire.a.pin);
        }
        return pins;
    }

    // A ground symbol or named supply port one grid step off `pin`, wired to it,
    // pointing the way SCH-P1/P2 want (ground and negative supplies down).
    void addSupplySymbol(const PinRef& pin, const juce::String& symbolId, const juce::String& netName)
    {
        const auto at = pinPosition(pin);
        const auto dir = nodeLeadDirection(WireNode::forPin(pin));
        const auto down = symbolId == "ground" || netName.startsWith("-");
        const juce::Point<float> want { 0.0f, down ? 1.0f : -1.0f };
        auto position = at + dir * schematic::gridSize;
        if (!(dir.x * want.x + dir.y * want.y > 0.5f || std::abs(dir.x) > 0.5f))
            position += juce::Point<float>(schematic::gridSize * 2.0f, 0.0f);

        Instance marker;
        marker.symbolId = symbolId;
        marker.refdes = nextRefdesFor(symbolId);
        marker.value = symbolId == "ground" ? juce::String("0") : juce::String();
        marker.busName = symbolId == "ground" ? defaultBusNameFor("ground") : netName;
        marker.family = familyFor(symbolId);
        marker.position = snapToGrid(position);
        marker.rotation = symbolId == "power_port" && down ? 180 : 0;
        marker.busLength = 0.0f;
        marker.sheet = currentSheet;
        instances.push_back(marker);
        wires.push_back({ WireNode::forPin({ (int)instances.size() - 1, 0 }), WireNode::forPin(pin) });
    }

    juce::String railToSymbols(int rail, juce::String& error)
    {
        if (rail < 0 || rail >= (int)instances.size() || !isRailBus(instances[(size_t)rail].symbolId))
        {
            error = "Not a power or ground rail.";
            return {};
        }
        const auto ground = instances[(size_t)rail].symbolId == "ground_bus";
        const auto netName = instances[(size_t)rail].busName;
        const auto railName = instances[(size_t)rail].refdes;
        std::set<int> taps;
        auto pins = railAttachedPins(rail, taps);
        if (pins.empty())
        {
            error = railName + " has nothing connected to it.";
            return {};
        }
        juce::StringArray dropped;
        const auto map = removeInstancesAndJunctions({ rail }, taps, dropped);
        for (auto& pin : pins)
            pin.instanceIndex = map[(size_t)pin.instanceIndex];
        for (const auto& pin : pins)
            addSupplySymbol(pin, ground ? "ground" : "power_port", netName);
        notifySelection();
        forceDeferredRepaint();
        if (onStatus) onStatus("Changed " + railName + " into " + juce::String((int)pins.size()) + (ground ? " ground symbols." : " supply ports."));
        return "{ \"converted\": " + quote(railName) + ", \"to\": \"symbols\", \"symbols\": " + juce::String((int)pins.size()) + " }";
    }

    juce::String symbolsToRail(int marker, juce::String& error)
    {
        if (marker < 0 || marker >= (int)instances.size()
            || (instances[(size_t)marker].symbolId != "ground" && instances[(size_t)marker].symbolId != "power_port"))
        {
            error = "Not a ground symbol or supply port.";
            return {};
        }
        const auto ground = instances[(size_t)marker].symbolId == "ground";
        const auto netName = instances[(size_t)marker].busName;
        const auto sheet = instances[(size_t)marker].sheet;

        // Every symbol of this net on the sheet becomes part of one rail.
        std::set<int> markers;
        for (int i = 0; i < (int)instances.size(); ++i)
            if (instances[(size_t)i].sheet == sheet && instances[(size_t)i].symbolId == instances[(size_t)marker].symbolId
                && (ground || instances[(size_t)i].busName == netName))
                markers.insert(i);
        std::vector<PinRef> pins;
        for (const auto& wire : wires)
        {
            if (wire.a.isPin() && markers.count(wire.a.pin.instanceIndex) != 0 && wire.b.isPin() && markers.count(wire.b.pin.instanceIndex) == 0) pins.push_back(wire.b.pin);
            if (wire.b.isPin() && markers.count(wire.b.pin.instanceIndex) != 0 && wire.a.isPin() && markers.count(wire.a.pin.instanceIndex) == 0) pins.push_back(wire.a.pin);
        }
        if (pins.empty())
        {
            error = "Those symbols are not wired to any pins.";
            return {};
        }

        float minX = std::numeric_limits<float>::max(), maxX = std::numeric_limits<float>::lowest();
        float minY = minX, maxY = maxX;
        for (const auto& pin : pins)
        {
            const auto p = pinPosition(pin);
            minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
            minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
        }

        juce::StringArray dropped;
        const auto map = removeInstancesAndJunctions(markers, {}, dropped);
        for (auto& pin : pins)
            pin.instanceIndex = map[(size_t)pin.instanceIndex];

        Instance rail;
        rail.symbolId = ground ? "ground_bus" : "power_bus";
        rail.refdes = nextRefdesFor(rail.symbolId);
        rail.busName = ground ? defaultBusNameFor("ground_bus") : netName;
        rail.value = rail.busName;
        rail.family = familyFor(rail.symbolId);
        rail.busLength = std::max(120.0f, std::round((maxX - minX + 96.0f) / 48.0f) * 48.0f);
        rail.position = snapToGrid({ (minX + maxX) * 0.5f, ground ? maxY + 72.0f : minY - 72.0f });
        rail.sheet = sheet;
        const auto railIndex = (int)instances.size();
        instances.push_back(rail);
        const auto viewing = currentSheet;
        currentSheet = sheet;
        for (const auto& pin : pins)
        {
            const auto tap = createRailTap(railIndex, { pinPosition(pin).x, instances[(size_t)railIndex].position.y });
            wires.push_back({ WireNode::forPin(pin), tap });
        }
        currentSheet = viewing;
        notifySelection();
        forceDeferredRepaint();
        if (onStatus) onStatus("Changed " + juce::String((int)markers.size()) + (ground ? " ground symbols" : " " + netName + " ports") + " into rail " + rail.refdes + ".");
        return "{ \"converted\": " + juce::String((int)markers.size()) + ", \"to\": \"rail\", \"rail\": " + quote(rail.refdes) + " }";
    }

    // ---- Part editing: shared by the properties pane and the agent ----

    struct PartView
    {
        bool ok = false;
        juce::String refdes, symbolId, sheet;
        juce::String simulationFidelity;
        juce::StringArray groups;
        int rotation = 0;
        std::vector<std::pair<juce::String, juce::String>> pinVoltages; // pin, DC voltage text
    };

    PartView partView(const juce::String& refdes) const
    {
        PartView view;
        const auto index = instanceIndexForRefdesAnySheet(refdes);
        if (index < 0)
            return view;
        const auto& instance = instances[(size_t)index];
        view.ok = true;
        view.refdes = instance.refdes;
        view.symbolId = instance.symbolId;
        view.sheet = sheetName(instance.sheet);
        view.simulationFidelity = simulationFidelityFor(instance);
        view.rotation = schematic::normalizedRotation(instance.rotation);
        for (const auto& group : groups)
            if (std::find(group.memberInstances.begin(), group.memberInstances.end(), index) != group.memberInstances.end())
                view.groups.add(group.name);

        if (!isNetMarker(instance.symbolId) && instance.symbolId != "sub_block" && instance.symbolId != "block_port")
        {
            auto sim = buildSimNetlist();
            const auto op = circuit_sim::solveOperatingPoint(sim.circuit);
            const auto symbol = symbolForInstance(instance);
            for (const auto& pin : symbol.pins)
            {
                const auto n = simNodeOfPin(sim, index, pin.name);
                view.pinVoltages.push_back({ pin.name, op.ok && n >= 0 ? juce::String(circuit_sim::formatValue(op.voltages[(size_t)n], "V", 4))
                                                                       : juce::String("--") });
            }
        }
        return view;
    }

    juce::String selectedRefdes() const
    {
        return selectedInstance >= 0 && selectedInstance < (int)instances.size() && selectedInstances.size() <= 1
            ? instances[(size_t)selectedInstance].refdes : juce::String();
    }

    bool setPartParameter(const juce::String& refdes, const juce::String& key, const juce::String& value, juce::String& error)
    {
        const auto index = instanceIndexForRefdesAnySheet(refdes);
        if (index < 0)
        {
            error = "No part " + refdes + ".";
            return false;
        }
        auto& instance = instances[(size_t)index];
        if (instance.symbolId == "sub_block" && instance.params.count("paramTarget." + key) != 0)
        {
            parts::ParamSpec spec;
            spec.key = key;
            spec.label = key;
            spec.kind = parts::Kind::Quantity;
            spec.unit = key.containsIgnoreCase("resistance") ? juce::String("ohm") : juce::String("V");
            if (!parts::validate(spec, value, error))
                return false;

            const auto target = instance.params["paramTarget." + key];
            const auto dot = target.indexOfChar('.');
            if (dot <= 0 || dot >= target.length() - 1)
            {
                error = "Block parameter " + key + " has an invalid target.";
                return false;
            }

            const auto targetRef = target.substring(0, dot);
            const auto targetKey = target.substring(dot + 1);
            if (!setPartParameter(targetRef, targetKey, value, error))
                return false;

            instance.params[key] = value.trim();
            notifySelection();
            forceDeferredRepaint();
            return true;
        }

        const auto* spec = parts::findParam(instance.symbolId, key);
        if (spec == nullptr)
        {
            juce::StringArray keys;
            for (const auto& s : parts::paramsFor(instance.symbolId)) keys.add(s.key);
            error = parts::displayName(instance.symbolId) + " has no property \"" + key + "\"."
                  + (keys.isEmpty() ? juce::String(" It has no editable properties.") : " Properties: " + keys.joinIntoString(", ") + ".");
            return false;
        }
        if (!parts::validate(*spec, value, error))
            return false;
        setPartValue(instance, key, value.trim());
        notifySelection();
        forceDeferredRepaint();
        return true;
    }

    bool renamePart(const juce::String& refdes, const juce::String& newRefdes, juce::String& error)
    {
        const auto index = instanceIndexForRefdesAnySheet(refdes);
        const auto name = newRefdes.trim();
        if (index < 0) { error = "No part " + refdes + "."; return false; }
        if (name.isEmpty() || !name.containsOnly("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_"))
        {
            error = "A reference designator uses letters, digits and _ only.";
            return false;
        }
        if (name.startsWithIgnoreCase("N") && name.substring(1).containsOnly("0123456789"))
        {
            error = name + " is reserved for junction labels.";
            return false;
        }
        const auto clash = instanceIndexForRefdesAnySheet(name);
        if (clash >= 0 && clash != index)
        {
            error = name + " is already used.";
            return false;
        }
        instances[(size_t)index].refdes = name;
        notifySelection();
        forceDeferredRepaint();
        return true;
    }

    bool setPartRotation(const juce::String& refdes, int degrees, juce::String& error)
    {
        const auto index = instanceIndexForRefdesAnySheet(refdes);
        if (index < 0) { error = "No part " + refdes + "."; return false; }
        if (degrees % 90 != 0) { error = "Rotation is 0, 90, 180 or 270 degrees."; return false; }
        instances[(size_t)index].rotation = schematic::normalizedRotation(degrees);
        notifySelection();
        forceDeferredRepaint();
        return true;
    }

    bool renameBlockPin(const juce::String& blockKey, const juce::String& oldName, const juce::String& newName, juce::String& error)
    {
        const auto block = blockIndexFor(blockKey);
        if (block < 0) { error = "No sub-diagram block " + blockKey + "."; return false; }
        auto& ports = instances[(size_t)block].ports;
        const auto found = std::find_if(ports.begin(), ports.end(), [&](const schematic::BlockPort& port) { return port.name == oldName; });
        if (found == ports.end()) { error = "Block " + instances[(size_t)block].refdes + " has no pin " + oldName + "."; return false; }
        if (newName.trim().isEmpty() || (newName.trim() != oldName && std::any_of(ports.begin(), ports.end(), [&](const schematic::BlockPort& p) { return p.name == newName.trim(); })))
        {
            error = "Pin names must be non-empty and unique on the block.";
            return false;
        }
        found->name = newName.trim();
        for (auto& instance : instances)
            if (instance.symbolId == "block_port" && instance.sheet == instances[(size_t)block].childSheet && instance.busName == oldName)
            {
                instance.busName = newName.trim();
                instance.value = newName.trim();
            }
        forceDeferredRepaint();
        return true;
    }

    bool exposeBlockParameter(const juce::String& blockKey,
                              const juce::String& parameterName,
                              const juce::String& targetRefdes,
                              const juce::String& targetParameter,
                              juce::String& error)
    {
        const auto block = blockIndexFor(blockKey);
        if (block < 0) { error = "No sub-diagram block " + blockKey + "."; return false; }
        const auto exposed = parameterName.trim();
        if (exposed.isEmpty() || !exposed.containsOnly("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_"))
        {
            error = "Parameter names use letters, digits and _ only.";
            return false;
        }
        const auto target = instanceIndexForRefdesAnySheet(targetRefdes.trim());
        if (target < 0) { error = "No internal component " + targetRefdes + "."; return false; }
        if (instances[(size_t)target].sheet != instances[(size_t)block].childSheet)
        {
            error = targetRefdes + " is not directly inside " + instances[(size_t)block].refdes + ".";
            return false;
        }
        const auto targetKey = targetParameter.trim();
        if (targetKey.isEmpty())
        {
            error = "Choose the internal parameter key to expose.";
            return false;
        }
        if (parts::findParam(instances[(size_t)target].symbolId, targetKey) == nullptr
            && instances[(size_t)target].params.count(targetKey) == 0)
        {
            juce::StringArray keys;
            for (const auto& s : parts::paramsFor(instances[(size_t)target].symbolId)) keys.add(s.key);
            error = instances[(size_t)target].refdes + " has no parameter " + targetKey
                  + (keys.isEmpty() ? juce::String(".") : ". Available: " + keys.joinIntoString(", ") + ".");
            return false;
        }

        pushUndoSnapshot();
        auto& blockInstance = instances[(size_t)block];
        blockInstance.params[exposed] = partValue(instances[(size_t)target], targetKey);
        blockInstance.params["paramTarget." + exposed] = instances[(size_t)target].refdes + "." + targetKey;
        notifySelection();
        forceDeferredRepaint();
        return true;
    }

    void openInstrumentFor(const juce::String& refdes)
    {
        const auto index = instanceIndexForRefdesAnySheet(refdes);
        if (index >= 0 && isInstrumentNode(instances[(size_t)index].symbolId) && onInstrumentOpen)
            onInstrumentOpen(instances[(size_t)index].refdes, instances[(size_t)index].symbolId);
    }

    juce::String parametersJson(const juce::String& refdes) const
    {
        const auto index = instanceIndexForRefdesAnySheet(refdes);
        if (index < 0)
            return toolFailure("schematic_get_parameters", "No part " + refdes + ".");
        const auto& instance = instances[(size_t)index];
        const auto fidelity = simulationFidelityFor(instance);
        juce::String list = "[";
        bool first = true;
        list << "{ \"key\": \"simulation_fidelity\", \"label\": \"Simulation fidelity\", \"value\": "
             << quote(fidelity) << ", \"readOnly\": true }";
        first = false;
        if (instance.symbolId == "sub_block")
        {
            for (const auto& [key, target] : instance.params)
            {
                if (!key.startsWith("paramTarget."))
                    continue;
                const auto paramKey = key.fromFirstOccurrenceOf("paramTarget.", false, false);
                const auto value = instance.params.count(paramKey) != 0 ? instance.params.at(paramKey) : juce::String();
                list << (first ? "" : ", ") << "{ \"key\": " << quote(paramKey)
                     << ", \"label\": " << quote(paramKey.replace("_", " "))
                     << ", \"value\": " << quote(value);
                if (paramKey.containsIgnoreCase("resistance")) list << ", \"unit\": \"ohm\"";
                else if (paramKey.containsIgnoreCase("voltage")) list << ", \"unit\": \"V\"";
                list << " }";
                first = false;
            }
        }
        const auto valueOf = [&](const juce::String& key) { return partValue(instance, key); };
        for (const auto& spec : parts::paramsFor(instance.symbolId))
            if (parts::isShown(spec, valueOf))
        {
            list << (first ? "" : ", ") << "{ \"key\": " << quote(spec.key) << ", \"label\": " << quote(spec.label)
                 << ", \"value\": " << quote(partValue(instance, spec.key));
            if (spec.unit.isNotEmpty()) list << ", \"unit\": " << quote(spec.unit);
            if (!spec.options.isEmpty())
            {
                juce::StringArray options;
                for (const auto& o : spec.options) options.add(quote(o));
                list << ", \"options\": [" << options.joinIntoString(", ") << "]";
            }
            list << " }";
            first = false;
        }
        list << "]";
        return "{ \"ok\": true, \"tool\": \"schematic_get_parameters\", \"refdes\": " + quote(instance.refdes)
             + ", \"type\": " + quote(parts::displayName(instance.symbolId)) + ", \"rotation\": " + juce::String(schematic::normalizedRotation(instance.rotation))
             + ", \"sheet\": " + quote(sheetName(instance.sheet)) + ", \"simulationFidelity\": " + quote(fidelity) + ", \"parameters\": " + list + " }";
    }

    const SimulationParameter* simulationParameterForId(const juce::String& id) const
    {
        for (const auto& p : simulationParameters)
            if (p.id == id)
                return &p;
        return nullptr;
    }

    juce::String backendCapability(const SimulationParameter& p, const juce::String& context) const
    {
        const auto index = instanceIndexForRefdesAnySheet(p.refdes);
        if (index < 0)
            return "Unsupported";
        const auto& instance = instances[(size_t)index];
        const auto id = instance.symbolId;
        const auto key = p.property;
        const auto ctx = context.toLowerCase();

        if (ctx == "spice")
        {
            if (isNetMarker(id) || id == "annotation_text" || schematic::isInstrumentSymbol(id))
                return "Unsupported";
            return "Rerun";
        }

        if (ctx == "live_audio")
        {
            if (id == "potentiometer" && key == "position")
                return "Prepared";
            if ((id == "switch_spst" || id == "switch_spdt") && key == "state")
                return "Prepared";
            if (id == "audio_in" || id == "audio_out")
                return "Rebuild";
            if (isNetMarker(id) || schematic::isInstrumentSymbol(id) || id == "annotation_text")
                return "Unsupported";
            return "Rebuild";
        }

        if (ctx == "vst")
        {
            const auto live = backendCapability(p, "live_audio");
            return live == "Unsupported" ? "Unsupported" : live;
        }

        return "Unknown";
    }

    juce::String simulationParameterJson(const SimulationParameter& p, const juce::String& context) const
    {
        const auto index = instanceIndexForRefdesAnySheet(p.refdes);
        const auto current = index >= 0 ? partValue(instances[(size_t)index], p.property) : juce::String();
        auto text = juce::String("{ \"id\": ") + quote(p.id)
                  + ", \"refdes\": " + quote(p.refdes)
                  + ", \"property\": " + quote(p.property)
                  + ", \"label\": " + quote(p.label)
                  + ", \"unit\": " + quote(p.unit)
                  + ", \"value\": " + quote(current)
                  + ", \"default\": " + quote(p.defaultValue)
                  + ", \"min\": " + quote(p.minValue)
                  + ", \"max\": " + quote(p.maxValue)
                  + ", \"scaling\": " + quote(p.scaling)
                  + ", \"control\": " + quote(p.control);
        if (context.isNotEmpty() && context != "all")
            text << ", \"capability\": " << quote(backendCapability(p, context));
        else
        {
            text << ", \"capabilities\": { \"live_audio\": " << quote(backendCapability(p, "live_audio"))
                 << ", \"spice\": " << quote(backendCapability(p, "spice"))
                 << ", \"vst\": " << quote(backendCapability(p, "vst")) << " }";
        }
        text << " }";
        return text;
    }

    juce::String simulationParametersJson(juce::String context) const
    {
        context = context.trim().toLowerCase();
        if (context.isEmpty()) context = "all";
        if (context != "all" && context != "live_audio" && context != "spice" && context != "vst")
            return toolFailure("simulation_parameter_list", "context must be live_audio, spice, vst, or all.");

        juce::String list = "[";
        bool first = true;
        for (const auto& p : simulationParameters)
        {
            const auto capability = context == "all" ? juce::String() : backendCapability(p, context);
            if (context != "all" && capability == "Unsupported")
                continue;
            list << (first ? "" : ", ") << simulationParameterJson(p, context);
            first = false;
        }
        list << "]";
        return "{ \"ok\": true, \"tool\": \"simulation_parameter_list\", \"context\": " + quote(context) + ", \"parameters\": " + list + " }";
    }

    bool exposeSimulationParameter(const juce::String& id,
                                   const juce::String& refdes,
                                   const juce::String& property,
                                   const juce::String& label,
                                   const juce::String& minValue,
                                   const juce::String& maxValue,
                                   const juce::String& scaling,
                                   const juce::String& control,
                                   juce::String& error)
    {
        const auto cleanId = id.trim();
        if (cleanId.isEmpty() || !cleanId.containsOnly("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-"))
        {
            error = "Simulation parameter ids use letters, digits, _ and - only.";
            return false;
        }
        const auto index = instanceIndexForRefdesAnySheet(refdes.trim());
        if (index < 0)
        {
            error = "No part " + refdes + ".";
            return false;
        }
        const auto& instance = instances[(size_t)index];
        const auto key = property.trim();
        const auto* spec = parts::findParam(instance.symbolId, key);
        if (spec == nullptr && instance.params.count(key) == 0)
        {
            juce::StringArray keys;
            for (const auto& s : parts::paramsFor(instance.symbolId)) keys.add(s.key);
            error = instance.refdes + " has no property " + key
                  + (keys.isEmpty() ? juce::String(".") : ". Available: " + keys.joinIntoString(", ") + ".");
            return false;
        }

        SimulationParameter p;
        p.id = cleanId;
        p.refdes = instance.refdes;
        p.property = key;
        p.label = label.trim().isNotEmpty() ? label.trim() : instance.refdes + " " + (spec != nullptr ? spec->label : key);
        p.unit = spec != nullptr ? spec->unit : juce::String();
        p.defaultValue = partValue(instance, key);
        p.minValue = minValue.trim();
        p.maxValue = maxValue.trim();
        p.scaling = scaling.trim().isNotEmpty() ? scaling.trim().toLowerCase() : juce::String("linear");
        p.control = control.trim().isNotEmpty() ? control.trim().toLowerCase()
                  : spec != nullptr && spec->kind == parts::Kind::Choice ? juce::String("dropdown")
                  : spec != nullptr && spec->kind == parts::Kind::Toggle ? juce::String("switch")
                  : spec != nullptr && spec->kind == parts::Kind::Fraction ? juce::String("knob")
                  : juce::String("slider");

        pushUndoSnapshot();
        for (auto& existing : simulationParameters)
            if (existing.id == cleanId)
            {
                existing = p;
                forceDeferredRepaint();
                return true;
            }
        simulationParameters.push_back(std::move(p));
        forceDeferredRepaint();
        return true;
    }

    bool setSimulationParameter(const juce::String& id, const juce::String& value, juce::String& error)
    {
        const auto* p = simulationParameterForId(id.trim());
        if (p == nullptr)
        {
            error = "No simulation parameter " + id + ".";
            return false;
        }
        return setPartParameter(p->refdes, p->property, value, error);
    }

    juce::String blockName(const juce::String& refdes) const
    {
        const auto block = blockIndexFor(refdes);
        return block >= 0 ? instances[(size_t)block].value : juce::String();
    }

    bool setBlockName(const juce::String& refdes, const juce::String& name, juce::String& error)
    {
        const auto block = blockIndexFor(refdes);
        if (block < 0) { error = "No sub-diagram block " + refdes + "."; return false; }
        if (name.trim().isEmpty()) { error = "A block needs a name."; return false; }
        instances[(size_t)block].value = name.trim();
        forceDeferredRepaint();
        return true;
    }

    std::vector<std::pair<juce::String, juce::String>> blockPins(const juce::String& refdes) const
    {
        std::vector<std::pair<juce::String, juce::String>> pins;
        const auto block = blockIndexFor(refdes);
        if (block >= 0)
            for (const auto& port : instances[(size_t)block].ports)
                pins.push_back({ port.name, port.rightSide ? "Output pin" : "Input pin" });
        return pins;
    }

    // ---- Instruments: measurements behind the instrument windows ----

    int instanceIndexForRefdesAnySheet(const juce::String& refdes) const
    {
        for (int i = 0; i < (int)instances.size(); ++i)
            if (instances[(size_t)i].refdes.equalsIgnoreCase(refdes.trim()))
                return i;
        return -1;
    }

    // Changes whenever anything that affects a measurement changes.
    juce::int64 modelRevision() const
    {
        return buildCircuitJson().hashCode64();
    }

    juce::String instrumentSetting(const juce::String& refdes, const juce::String& key) const
    {
        const auto index = instanceIndexForRefdesAnySheet(refdes);
        return index >= 0 ? partValue(instances[(size_t)index], key) : juce::String();
    }

    void setInstrumentSetting(const juce::String& refdes, const juce::String& key, const juce::String& value)
    {
        const auto index = instanceIndexForRefdesAnySheet(refdes);
        if (index < 0)
            return;
        setPartValue(instances[(size_t)index], key, value);
        notifySelection();
        repaint();
    }

    circuit_sim::Node simNodeOfPin(const SimNetlist& sim, int instanceIndex, const juce::String& pinName) const
    {
        const auto netNames = computeNetNames();
        const auto symbol = symbolForInstance(instances[(size_t)instanceIndex]);
        for (int p = 0; p < (int)symbol.pins.size(); ++p)
            if (symbol.pins[(size_t)p].name == pinName)
            {
                const auto net = netFor({ instanceIndex, p }, netNames);
                if (net == "0") return 0;
                const auto found = sim.nodeOfNet.find(net);
                return found != sim.nodeOfNet.end() ? found->second : -1;
            }
        return -1;
    }

    // Lowest frequency of any periodic source, 0 if none.
    double lowestSourceFrequency(const SimNetlist& sim) const
    {
        double lowest = 0.0;
        for (const auto& e : sim.circuit.elements())
            if ((e.type == circuit_sim::Element::Type::VoltageSource || e.type == circuit_sim::Element::Type::CurrentSource)
                && e.wave.kind != circuit_sim::Waveform::Kind::Dc && e.wave.frequency > 0.0)
                lowest = lowest == 0.0 ? e.wave.frequency : std::min(lowest, e.wave.frequency);
        return lowest;
    }

    struct ScopeCapture
    {
        bool ok = false;
        juce::String error;
        double timePerDiv = 1e-3;
        bool triggered = false;
        std::vector<double> time;            // 0 .. 10 divisions
        std::array<std::vector<double>, 2> channel;
        std::array<bool, 2> connected { false, false };
        juce::StringArray warnings;
    };

    ScopeCapture captureScope(const juce::String& refdes) const
    {
        ScopeCapture cap;
        const auto index = instanceIndexForRefdesAnySheet(refdes);
        if (index < 0 || instances[(size_t)index].symbolId != "oscilloscope_2ch")
        {
            cap.error = "No oscilloscope " + refdes + ".";
            return cap;
        }
        const auto& scope = instances[(size_t)index];
        cap.timePerDiv = std::max(1e-7, parseQuantity(partValue(scope, "time_per_div"), 1e-3));
        const auto window = 10.0 * cap.timePerDiv;
        auto sim = buildSimNetlist();
        cap.warnings = sim.warnings;
        const auto ref = simNodeOfPin(sim, index, "REF");
        const std::array<circuit_sim::Node, 2> nodes { simNodeOfPin(sim, index, "CH1"), simNodeOfPin(sim, index, "CH2") };

        // Let the circuit settle for a few signal periods, then capture one
        // screen after the trigger point.
        const auto fmin = lowestSourceFrequency(sim);
        const auto step = window / 500.0;
        auto settle = std::max(2.0 * window, fmin > 0.0 ? 5.0 / fmin : 0.0);
        settle = std::min(settle, step * 16000.0);
        const auto tr = circuit_sim::solveTransient(sim.circuit, settle + 2.0 * window, step, {}, 1 << 20);
        if (!tr.ok)
        {
            cap.error = tr.error;
            return cap;
        }
        auto value = [&](size_t sample, int ch) {
            if (nodes[(size_t)ch] < 0) return 0.0;
            return tr.voltages[sample][(size_t)nodes[(size_t)ch]] - (ref >= 0 ? tr.voltages[sample][(size_t)ref] : 0.0);
        };

        const auto trigChannel = partValue(scope, "trigger_source") == "CH2" ? 1 : 0;
        const auto level = parseQuantity(partValue(scope, "trigger_level"), 0.0);
        const auto rising = partValue(scope, "trigger_slope") != "Falling";
        size_t start = 0;
        for (size_t s = 0; s < tr.time.size(); ++s)
            if (tr.time[s] >= settle) { start = s; break; }
        size_t trigger = start;
        for (size_t s = start + 1; s < tr.time.size() && tr.time[s] < settle + window; ++s)
        {
            const auto a = value(s - 1, trigChannel), b = value(s, trigChannel);
            if (rising ? (a < level && b >= level) : (a > level && b <= level))
            {
                trigger = s;
                cap.triggered = true;
                break;
            }
        }
        for (size_t s = trigger; s < tr.time.size() && tr.time[s] <= tr.time[trigger] + window + step * 0.5; ++s)
        {
            cap.time.push_back(tr.time[s] - tr.time[trigger]);
            for (int ch = 0; ch < 2; ++ch)
                cap.channel[(size_t)ch].push_back(value(s, ch));
        }
        for (int ch = 0; ch < 2; ++ch)
            cap.connected[(size_t)ch] = nodes[(size_t)ch] >= 0;
        cap.ok = true;
        return cap;
    }

    struct MeterReading
    {
        bool ok = false;
        juce::String error;
        juce::String function;
        double value = 0.0;
        juce::String unit;
        juce::String display;
    };

    MeterReading readMeter(const juce::String& refdes) const
    {
        MeterReading reading;
        const auto index = instanceIndexForRefdesAnySheet(refdes);
        if (index < 0 || instances[(size_t)index].symbolId != "digital_multimeter")
        {
            reading.error = "No multimeter " + refdes + ".";
            return reading;
        }
        reading.function = partValue(instances[(size_t)index], "value");
        auto finish = [&](double value, const juce::String& unit) {
            reading.ok = true;
            reading.value = value;
            reading.unit = unit;
            reading.display = juce::String(circuit_sim::formatValue(value, (unit == "ohm" ? ohmText() : unit).toStdString(), 4));
            return reading;
        };

        if (reading.function == "Ohms")
        {
            auto sim = buildSimNetlist(index);
            const auto op = circuit_sim::solveOperatingPoint(sim.circuit);
            if (!op.ok) { reading.error = op.error; return reading; }
            const auto hi = simNodeOfPin(sim, index, "HI"), lo = simNodeOfPin(sim, index, "LO");
            if (hi < 0 || lo < 0) { reading.ok = true; reading.display = "OL"; reading.unit = "ohm"; reading.value = INFINITY; return reading; }
            const auto r = (op.voltages[(size_t)hi] - op.voltages[(size_t)lo]) / 1e-3;
            if (r > 100e6) { reading.ok = true; reading.display = "OL"; reading.unit = "ohm"; reading.value = INFINITY; return reading; }
            return finish(r, "ohm");
        }

        auto sim = buildSimNetlist();
        if (reading.function == "DC A")
        {
            const auto op = circuit_sim::solveOperatingPoint(sim.circuit);
            if (!op.ok) { reading.error = op.error; return reading; }
            const auto found = sim.elementOfPart.find(instances[(size_t)index].refdes);
            return finish(found != sim.elementOfPart.end() ? op.sourceCurrents[(size_t)found->second] : 0.0, "A");
        }

        const auto hi = simNodeOfPin(sim, index, "HI"), lo = simNodeOfPin(sim, index, "LO");
        if (reading.function == "AC V")
        {
            const auto fmin = lowestSourceFrequency(sim);
            if (fmin <= 0.0)
                return finish(0.0, "V");
            const auto period = 1.0 / fmin;
            const auto tr = circuit_sim::solveTransient(sim.circuit, 20.0 * period, period / 200.0, {}, 1 << 20);
            if (!tr.ok) { reading.error = tr.error; return reading; }
            std::vector<double> v;
            for (size_t s = 0; s < tr.time.size(); ++s)
                if (tr.time[s] >= 10.0 * period)
                    v.push_back((hi >= 0 ? tr.voltages[s][(size_t)hi] : 0.0) - (lo >= 0 ? tr.voltages[s][(size_t)lo] : 0.0));
            double mean = 0.0, sq = 0.0;
            for (auto x : v) mean += x;
            mean /= std::max<size_t>(1, v.size());
            for (auto x : v) sq += (x - mean) * (x - mean);
            return finish(std::sqrt(sq / std::max<size_t>(1, v.size())), "V");
        }

        const auto op = circuit_sim::solveOperatingPoint(sim.circuit);
        if (!op.ok) { reading.error = op.error; return reading; }
        return finish((hi >= 0 ? op.voltages[(size_t)hi] : 0.0) - (lo >= 0 ? op.voltages[(size_t)lo] : 0.0), "V");
    }

    struct BodeSweep
    {
        bool ok = false;
        juce::String error;
        std::vector<double> frequency, gainDb, phaseDeg;
        double peakDb = 0.0;
        std::vector<double> corners;
    };

    BodeSweep sweepBode(const juce::String& refdes) const
    {
        BodeSweep result;
        const auto index = instanceIndexForRefdesAnySheet(refdes);
        if (index < 0 || instances[(size_t)index].symbolId != "bode_analyzer")
        {
            result.error = "No frequency analyzer " + refdes + ".";
            return result;
        }
        const auto& fra = instances[(size_t)index];
        auto sim = buildSimNetlist();
        const auto in = simNodeOfPin(sim, index, "IN"), out = simNodeOfPin(sim, index, "OUT"), ref = simNodeOfPin(sim, index, "REF");
        if (in < 0 || out < 0)
        {
            result.error = "Connect IN and OUT to the circuit.";
            return result;
        }
        const auto ac = circuit_sim::solveAc(sim.circuit, parseQuantity(partValue(fra, "start_frequency"), 10.0),
                                             parseQuantity(partValue(fra, "stop_frequency"), 100e3),
                                             std::max(1, partValue(fra, "points_per_decade").getIntValue()));
        if (!ac.ok)
        {
            result.error = ac.error;
            return result;
        }
        for (size_t k = 0; k < ac.frequency.size(); ++k)
        {
            const auto& v = ac.voltages[k];
            const auto r = ref >= 0 ? v[(size_t)ref] : std::complex<double>(0.0);
            const auto vin = v[(size_t)in] - r;
            const auto h = std::abs(vin) > 1e-15 ? (v[(size_t)out] - r) / vin : std::complex<double>(0.0);
            result.frequency.push_back(ac.frequency[k]);
            result.gainDb.push_back(20.0 * std::log10(std::max(1e-15, std::abs(h))));
            result.phaseDeg.push_back(std::arg(h) * 180.0 / juce::MathConstants<double>::pi);
        }
        result.peakDb = *std::max_element(result.gainDb.begin(), result.gainDb.end());
        for (size_t k = 1; k < result.gainDb.size(); ++k)
        {
            const auto a = result.gainDb[k - 1] - (result.peakDb - 3.0103), b = result.gainDb[k] - (result.peakDb - 3.0103);
            if ((a < 0) != (b < 0))
                result.corners.push_back(std::exp(std::log(result.frequency[k - 1])
                                                  + (std::log(result.frequency[k]) - std::log(result.frequency[k - 1])) * (a / (a - b))));
        }
        result.ok = true;
        return result;
    }

    static WaveStats channelStatsFor(const ScopeCapture& cap, int ch)
    {
        return statsOf(cap.time, cap.channel[(size_t)ch], 0.0);
    }

    juce::String instrumentReadJson(const juce::String& refdes) const
    {
        const auto index = instanceIndexForRefdesAnySheet(refdes);
        if (index < 0)
            return toolFailure("instrument_read", "No instrument " + refdes + ".");
        const auto& id = instances[(size_t)index].symbolId;
        if (id == "oscilloscope_2ch")
        {
            const auto cap = captureScope(refdes);
            if (!cap.ok) return toolFailure("instrument_read", cap.error);
            juce::String channels = "[";
            for (int ch = 0; ch < 2; ++ch)
            {
                const auto s = channelStatsFor(cap, ch);
                channels << (ch == 0 ? "" : ", ") << "{ \"channel\": \"CH" << (ch + 1) << "\", \"connected\": " << (cap.connected[(size_t)ch] ? "true" : "false")
                         << ", \"peakToPeak\": " << juce::String(s.maximum - s.minimum, 6) << ", \"rms\": " << juce::String(s.rms, 6)
                         << ", \"mean\": " << juce::String(s.mean, 6) << ", \"frequencyHz\": " << juce::String(s.frequency, 6) << " }";
            }
            channels << "]";
            return "{ \"ok\": true, \"tool\": \"instrument_read\", \"instrument\": \"oscilloscope\", \"refdes\": " + quote(refdes)
                 + ", \"timePerDiv\": " + juce::String(cap.timePerDiv, 9) + ", \"triggered\": " + (cap.triggered ? "true" : "false")
                 + ", \"channels\": " + channels + " }";
        }
        if (id == "digital_multimeter")
        {
            const auto r = readMeter(refdes);
            if (!r.ok) return toolFailure("instrument_read", r.error);
            return "{ \"ok\": true, \"tool\": \"instrument_read\", \"instrument\": \"multimeter\", \"refdes\": " + quote(refdes)
                 + ", \"function\": " + quote(r.function) + ", \"display\": " + quote(r.display)
                 + ", \"value\": " + (std::isfinite(r.value) ? juce::String(r.value, 9) : juce::String("null")) + ", \"unit\": " + quote(r.unit) + " }";
        }
        if (id == "bode_analyzer")
        {
            const auto b = sweepBode(refdes);
            if (!b.ok) return toolFailure("instrument_read", b.error);
            juce::StringArray corners;
            for (auto f : b.corners) corners.add(juce::String(f, 4));
            return "{ \"ok\": true, \"tool\": \"instrument_read\", \"instrument\": \"frequency_analyzer\", \"refdes\": " + quote(refdes)
                 + ", \"peakGainDb\": " + juce::String(b.peakDb, 4) + ", \"minus3dBFrequenciesHz\": [" + corners.joinIntoString(", ") + "] }";
        }
        return toolFailure("instrument_read", refdes + " is not an instrument.");
    }

    // Renders one sheet to a PNG exactly as the canvas draws it.
    juce::String exportSheetImage(const juce::String& sheet, juce::File& written)
    {
        const auto viewing = currentSheet;
        const auto savedSelection = selectedInstances;
        const auto savedInstance = selectedInstance;
        const auto savedGroup = selectedGroup;
        currentSheet = sheet;
        selectedInstances.clear();
        selectedInstance = -1;
        selectedGroup = -1;
        routeSignature.clear();
        ensureRoutes();

        juce::Rectangle<float> box;
        bool first = true;
        auto add = [&](juce::Rectangle<float> r) { box = first ? r : box.getUnion(r); first = false; };
        for (int i = 0; i < (int)instances.size(); ++i)
            if (onSheet(i))
            {
                const auto symbol = symbolForInstance(instances[(size_t)i]);
                add(schematic::rotateBounds(schematic::extentBounds(symbol), instances[(size_t)i].rotation)
                        .translated(instances[(size_t)i].position.x, instances[(size_t)i].position.y).expanded(40.0f));
            }
        for (const auto& group : groups)
            if (groupOnSheet(group))
                add(groupBounds(group));
        for (size_t w = 0; w < wires.size() && w < routeCache.size(); ++w)
            for (const auto& point : routeCache[w])
                add({ point.x - 1.0f, point.y - 1.0f, 2.0f, 2.0f });

        juce::String result;
        if (!first)
        {
            box = box.expanded(48.0f);
            const auto width = juce::jlimit(64, 8000, (int)box.getWidth());
            const auto height = juce::jlimit(64, 8000, (int)box.getHeight());
            juce::Image image(juce::Image::ARGB, width, height, true);
            {
                juce::Graphics g(image);
                g.fillAll(juce::Colour(0xff0e141a));
                g.addTransform(juce::AffineTransform::translation(-box.getX(), -box.getY()));
                g.setColour(juce::Colour(0xff1b2630));
                for (float x = std::floor(box.getX() / 24.0f) * 24.0f; x < box.getRight(); x += 24.0f)
                    for (float y = std::floor(box.getY() / 24.0f) * 24.0f; y < box.getBottom(); y += 24.0f)
                        g.fillRect(x - 0.5f, y - 0.5f, 1.5f, 1.5f);
                drawWires(g);
                drawGroups(g);
                drawInstances(g);
                drawProbes(g);
            }
            written.getParentDirectory().createDirectory();
            written.deleteFile();
            juce::FileOutputStream out(written);
            if (out.openedOk() && juce::PNGImageFormat().writeImageToStream(image, out))
                result = written.getFullPathName();
        }

        currentSheet = viewing;
        selectedInstances = savedSelection;
        selectedInstance = savedInstance;
        selectedGroup = savedGroup;
        routeSignature.clear();
        forceDeferredRepaint();
        return result;
    }

    // Where generated files for the open diagram go (set by the workbench).
    std::function<juce::File()> outputDirectory;

    juce::File schematicImageFile(const juce::String& sheetLabel) const
    {
        const auto folder = outputDirectory != nullptr ? outputDirectory()
                                                       : juce::File::getSpecialLocation(juce::File::tempDirectory);
        return folder.getChildFile("schematic_" + juce::File::createLegalFileName(sheetLabel).replaceCharacter(' ', '_') + ".png");
    }

    juce::String runSchematicTool(const juce::String& name, const juce::var& args)
    {
        if (name == "instrument_read")
            return instrumentReadJson(args.getProperty("refdes", {}).toString());
        auto arg = [&](const char* key) { return args.getProperty(key, {}).toString().trim(); };
        if (name == "preferences_list")
        {
            const auto query = arg("query").toLowerCase();
            juce::String list = "[";
            bool first = true;
            for (const auto& setting : prefs::all())
            {
                const auto haystack = (setting.key + " " + setting.category + " " + setting.label + " " + setting.description).toLowerCase();
                if (query.isNotEmpty() && !haystack.contains(query))
                    continue;
                list << (first ? "" : ", ") << "{ \"key\": " << quote(setting.key) << ", \"category\": " << quote(setting.category)
                     << ", \"label\": " << quote(setting.label) << ", \"value\": " << quote(prefs::get(setting.key))
                     << ", \"description\": " << quote(setting.description);
                if (setting.kind == prefs::Kind::Toggle) list << ", \"options\": [\"true\", \"false\"]";
                if (setting.kind == prefs::Kind::Choice)
                {
                    juce::StringArray options;
                    for (const auto& o : setting.options) options.add(quote(o));
                    list << ", \"options\": [" << options.joinIntoString(", ") << "]";
                }
                list << " }";
                first = false;
            }
            list << "]";
            return "{ \"ok\": true, \"tool\": " + quote(name) + ", \"preferences\": " + list + " }";
        }
        if (name == "preferences_set")
        {
            juce::String error;
            if (!prefs::set(arg("key"), arg("value"), error))
                return toolFailure(name, error);
            return "{ \"ok\": true, \"tool\": " + quote(name) + ", \"key\": " + quote(arg("key")) + ", \"value\": " + quote(prefs::get(arg("key"))) + " }";
        }
        if (name == "schematic_convert_supply")
        {
            juce::String error;
            const auto index = instanceIndexForRefdesAnySheet(arg("refdes"));
            const auto to = arg("to").toLowerCase();
            const auto done = to == "rail" ? symbolsToRail(index, error) : to == "symbols" ? railToSymbols(index, error)
                                                                          : juce::String();
            if (done.isEmpty())
                return toolFailure(name, error.isNotEmpty() ? error : juce::String("to must be \"rail\" or \"symbols\"."));
            return "{ \"ok\": true, \"tool\": " + quote(name) + ", \"result\": " + done + " }";
        }
        if (name == "schematic_get_parameters")
            return parametersJson(arg("refdes"));
        if (name == "schematic_set_parameters")
        {
            const auto refdes = arg("refdes");
            const auto* params = args.getProperty("params", {}).getDynamicObject();
            if (params == nullptr || params->getProperties().isEmpty())
                return toolFailure(name, "params must be an object such as {\"value\": \"4.7k\"}.");
            for (const auto& property : params->getProperties())
            {
                juce::String error;
                if (!setPartParameter(refdes, property.name.toString(), property.value.toString(), error))
                    return toolFailure(name, error);
            }
            return parametersJson(refdes).replace("\"schematic_get_parameters\"", "\"schematic_set_parameters\"");
        }
        if (name == "simulation_parameter_expose")
        {
            juce::String error;
            if (!exposeSimulationParameter(arg("id"), arg("refdes"), arg("property"), arg("label"),
                                           arg("min"), arg("max"), arg("scaling"), arg("control"), error))
                return toolFailure(name, error);
            return "{ \"ok\": true, \"tool\": " + quote(name) + ", \"parameter\": "
                 + simulationParameterJson(*simulationParameterForId(arg("id")), "all") + " }";
        }
        if (name == "simulation_parameter_list")
            return simulationParametersJson(arg("context"));
        if (name == "simulation_parameter_set")
        {
            juce::String error;
            if (!setSimulationParameter(arg("id"), arg("value"), error))
                return toolFailure(name, error);
            return "{ \"ok\": true, \"tool\": " + quote(name) + ", \"parameter\": "
                 + simulationParameterJson(*simulationParameterForId(arg("id")), "all") + " }";
        }
        if (name == "schematic_rename_component")
        {
            juce::String error;
            if (!renamePart(arg("refdes"), arg("newRefdes"), error))
                return toolFailure(name, error);
            return "{ \"ok\": true, \"tool\": " + quote(name) + ", \"refdes\": " + quote(arg("newRefdes")) + " }";
        }
        if (name == "schematic_delete_components")
        {
            const auto* list = args.getProperty("refdes", {}).getArray();
            if (list == nullptr || list->isEmpty())
                return toolFailure(name, "refdes must be a list such as [\"R5\"].");
            juce::StringArray deleted;
            for (const auto& item : *list)
            {
                const auto refdes = item.toString().trim();
                const auto index = instanceIndexForRefdesAnySheet(refdes);
                if (index < 0)
                    return toolFailure(name, "No part " + refdes + (deleted.isEmpty() ? juce::String(".") : ". Already deleted: " + deleted.joinIntoString(", ")));
                if (instances[(size_t)index].symbolId == "sub_block")
                    return toolFailure(name, refdes + " is a sub-diagram block; expand it first with schematic_subdiagram_expand.");
                // Exactly this part: deleteSelected() prefers the UI's multi-selection,
                // which would delete whatever the user has selected instead.
                selectedInstances.clear();
                selectedInstance = index;
                deleteSelected();
                deleted.add(refdes);
            }
            routeSignature.clear();
            forceDeferredRepaint();
            juce::StringArray quoted;
            for (const auto& refdes : deleted)
                quoted.add(quote(refdes));
            return "{ \"ok\": true, \"tool\": " + quote(name) + ", \"deleted\": [" + quoted.joinIntoString(", ") + "] }";
        }
        if (name == "schematic_rotate_component")
        {
            juce::String error;
            if (!setPartRotation(arg("refdes"), arg("rotation").getIntValue(), error))
                return toolFailure(name, error);
            return "{ \"ok\": true, \"tool\": " + quote(name) + ", \"refdes\": " + quote(arg("refdes")) + ", \"rotation\": " + juce::String(schematic::normalizedRotation(arg("rotation").getIntValue())) + " }";
        }
        auto text = [&](const juce::String& key) { return args.getProperty(juce::Identifier(key), {}).toString().trim(); };
        auto number = [&](const juce::String& key, float fallback) {
            const auto* object = args.getDynamicObject();
            return object != nullptr ? floatProperty(*object, key.toRawUTF8(), fallback) : fallback;
        };
        auto ok = [&](const juce::String& body) {
            forceDeferredRepaint();
            return "{ \"ok\": true, \"tool\": " + quote(name) + ", " + body + " }";
        };

        if (name == "schematic_group_create")
        {
            std::vector<int> members;
            juce::String error;
            if (!membersFromRefdes(args.getProperty("members", {}), members, error))
                return toolFailure(name, error);
            if (members.empty())
                return toolFailure(name, "A group needs at least one member component.");

            Group group;
            group.id = nextGroupId();
            group.name = text("name").isNotEmpty() ? text("name") : group.id;
            group.category = text("category").isNotEmpty() ? text("category") : juce::String("user_group");
            group.notes = text("notes");
            group.memberInstances = members;
            groups.push_back(group);
            selectedGroup = (int)groups.size() - 1;
            if (onStatus) onStatus("Created group " + group.name + ".");
            return ok("\"group\": " + groupJson(groups.back()));
        }

        if (name == "schematic_group_update")
        {
            const auto index = groupIndexFor(text("group"));
            if (index < 0)
                return toolFailure(name, "No group with id or name " + text("group") + ".");

            std::vector<int> add, remove;
            juce::String error;
            if (!membersFromRefdes(args.getProperty("addMembers", {}), add, error)
                || !membersFromRefdes(args.getProperty("removeMembers", {}), remove, error))
                return toolFailure(name, error);

            auto updated = groups[(size_t)index];
            if (args.hasProperty("name") && text("name").isNotEmpty()) updated.name = text("name");
            if (args.hasProperty("category") && text("category").isNotEmpty()) updated.category = text("category");
            if (args.hasProperty("notes")) updated.notes = text("notes");
            for (int member : add)
                if (std::find(updated.memberInstances.begin(), updated.memberInstances.end(), member) == updated.memberInstances.end())
                    updated.memberInstances.push_back(member);
            updated.memberInstances.erase(std::remove_if(updated.memberInstances.begin(), updated.memberInstances.end(),
                                                         [&](int m) { return std::find(remove.begin(), remove.end(), m) != remove.end(); }),
                                          updated.memberInstances.end());
            if (updated.memberInstances.empty())
                return toolFailure(name, "That would leave the group empty; use schematic_group_delete instead.");

            groups[(size_t)index] = updated;
            if (onStatus) onStatus("Updated group " + updated.name + ".");
            return ok("\"group\": " + groupJson(updated));
        }

        if (name == "schematic_group_delete")
        {
            const auto index = groupIndexFor(text("group"));
            if (index < 0)
                return toolFailure(name, "No group with id or name " + text("group") + ".");
            const auto removed = groups[(size_t)index];
            groups.erase(groups.begin() + index);
            if (selectedGroup == index) selectedGroup = -1;
            else if (selectedGroup > index) --selectedGroup;
            if (onStatus) onStatus("Removed group box " + removed.name + "; components and wiring unchanged.");
            return ok("\"removed\": " + groupJson(removed));
        }

        if (name == "schematic_group_list")
        {
            juce::String list = "\"groups\": [";
            for (size_t i = 0; i < groups.size(); ++i)
                list << (i == 0 ? "" : ", ") << groupJson(groups[i]);
            list << "]";
            return "{ \"ok\": true, \"tool\": " + quote(name) + ", " + list + " }";
        }

        if (name == "schematic_export_image")
        {
            auto sheet = currentSheet;
            const auto target = text("sheet");
            if (target.equalsIgnoreCase("main"))
                sheet = {};
            else if (target.isNotEmpty())
            {
                const auto block = blockIndexFor(target);
                if (block < 0)
                    return toolFailure(name, "No sub-diagram block " + target + ". Use a block refdes or name, or 'main'.");
                sheet = instances[(size_t)block].childSheet;
            }
            auto file = schematicImageFile(sheetName(sheet));
            const auto path = exportSheetImage(sheet, file);
            if (path.isEmpty())
                return toolFailure(name, "Nothing to render on " + sheetName(sheet) + ", or the image could not be written.");
            if (onStatus) onStatus("Exported schematic image " + path);
            return "{ \"ok\": true, \"tool\": " + quote(name) + ", \"sheet\": " + quote(sheetName(sheet)) + ", \"image\": " + quote(path) + " }";
        }

        if (name == "schematic_subdiagram_create")
        {
            std::vector<int> members;
            juce::String error;
            int groupIndex = -1;
            if (text("group").isNotEmpty())
            {
                groupIndex = groupIndexFor(text("group"));
                if (groupIndex < 0)
                    return toolFailure(name, "No group with id or name " + text("group") + ".");
                members = groups[(size_t)groupIndex].memberInstances;
            }
            else if (!membersFromRefdes(args.getProperty("members", {}), members, error))
                return toolFailure(name, error);
            auto blockName = text("name");
            if (blockName.isEmpty() && groupIndex >= 0)
                blockName = groups[(size_t)groupIndex].name;
            if (groupIndex >= 0)
                groups.erase(groups.begin() + groupIndex);
            const auto block = createSubDiagram(members, blockName, error);
            if (block.isEmpty())
                return toolFailure(name, error);
            return ok("\"block\": " + block);
        }

        if (name == "schematic_subdiagram_expand")
        {
            juce::String error;
            const auto result = expandSubDiagram(blockIndexFor(text("block")), error);
            if (result.isEmpty())
                return toolFailure(name, "No sub-diagram block " + text("block") + ".");
            return ok("\"result\": " + result);
        }

        if (name == "schematic_subdiagram_open")
        {
            const auto target = text("target");
            if (target.equalsIgnoreCase("main") || target.equalsIgnoreCase("top") || target.equalsIgnoreCase("root"))
                openSheet({});
            else if (target.equalsIgnoreCase("up"))
            {
                const auto block = blockForSheet(currentSheet);
                openSheet(block >= 0 ? instances[(size_t)block].sheet : juce::String());
            }
            else
            {
                const auto block = blockIndexFor(target);
                if (block < 0)
                    return toolFailure(name, "No sub-diagram block " + target + ". Use a block refdes or name, 'up', or 'main'.");
                openSheet(instances[(size_t)block].childSheet);
            }
            juce::StringArray names;
            for (const auto& sheet : sheetPath())
                names.add(quote(sheetName(sheet)));
            return ok("\"viewing\": " + quote(sheetName(currentSheet)) + ", \"path\": [" + names.joinIntoString(", ") + "]");
        }

        if (name == "schematic_subdiagram_rename")
        {
            const auto block = blockIndexFor(text("block"));
            if (block < 0)
                return toolFailure(name, "No sub-diagram block " + text("block") + ".");
            if (text("name").isEmpty())
                return toolFailure(name, "A new name is required.");
            instances[(size_t)block].value = text("name");
            return ok("\"block\": " + blockJson(block));
        }

        if (name == "schematic_subdiagram_rename_port")
        {
            juce::String error;
            if (!renameBlockPin(text("block"), text("port"), text("name"), error))
                return toolFailure(name, error);
            return ok("\"block\": " + blockJson(blockIndexFor(text("block"))));
        }

        if (name == "schematic_subdiagram_expose_parameter")
        {
            juce::String error;
            if (!exposeBlockParameter(text("block"), text("parameterName"), text("targetRefdes"), text("targetParameter"), error))
                return toolFailure(name, error);
            return ok("\"block\": " + blockJson(blockIndexFor(text("block"))) + ", \"parameter\": " + quote(text("parameterName")));
        }

        if (name == "schematic_subdiagram_list")
        {
            juce::String list = "\"viewing\": " + quote(sheetName(currentSheet)) + ", \"blocks\": [";
            bool first = true;
            for (int i = 0; i < (int)instances.size(); ++i)
                if (instances[(size_t)i].symbolId == "sub_block")
                {
                    list << (first ? "" : ", ") << blockJson(i);
                    first = false;
                }
            list << "]";
            return "{ \"ok\": true, \"tool\": " + quote(name) + ", " + list + " }";
        }

        if (name == "library_save_block")
        {
            juce::String error;
            const auto saved = saveUserBlock(blockIndexFor(text("block")), text("name"), text("description"), text("category"), error);
            if (saved.isEmpty())
                return toolFailure(name, error);
            return "{ \"ok\": true, \"tool\": " + quote(name) + ", \"saved\": " + saved + " }";
        }

        if (name == "library_list_blocks")
            return "{ \"ok\": true, \"tool\": " + quote(name) + ", " + listUserBlocks(text("query")) + " }";

        if (name == "library_place_block")
        {
            juce::String error;
            const auto placed = placeUserBlock(text("name"),
                                               { number("x", 160.0f), number("y", 160.0f) },
                                               text("instanceName"),
                                               error);
            if (placed.isEmpty())
                return toolFailure(name, error);
            return ok("\"block\": " + placed);
        }

        return toolFailure(name, "Unknown schematic tool.");
    }

private:

    void createGroupFromSelection()
    {
        if (selectedInstances.isEmpty())
        {
            if (onStatus) onStatus("Select the components to put in the group box.");
            return;
        }

        Group group;
        group.id = nextGroupId();
        group.name = "Group " + group.id.substring(1);
        group.category = "user_group";
        for (int index : selectedInstances)
            if (index >= 0 && index < (int)instances.size())
                group.memberInstances.push_back(index);

        if (group.memberInstances.empty())
            return;

        groups.push_back(std::move(group));
        selectedGroup = (int)groups.size() - 1;
        if (onStatus) onStatus("Created " + groups.back().name + " from " + juce::String((int)groups.back().memberInstances.size()) + " component(s).");
        repaint();
        editSelectedGroupMetadata(); // name it now
    }

    void editSelectedGroupMetadata()
    {
        if (selectedGroup < 0 || selectedGroup >= (int)groups.size())
            return;

        const auto groupIndex = selectedGroup;
        auto* editor = new juce::AlertWindow("Group Box", "Name shown in the box corner.", juce::AlertWindow::NoIcon);
        editor->addTextEditor("name", groups[(size_t)groupIndex].name, "Name");
        editor->addTextEditor("category", groups[(size_t)groupIndex].category, "Category");
        editor->addTextEditor("notes", groups[(size_t)groupIndex].notes, "Notes");
        editor->addButton("Apply", 1, juce::KeyPress(juce::KeyPress::returnKey));
        editor->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        editor->enterModalState(true, juce::ModalCallbackFunction::create([this, editor, groupIndex](int result) {
            std::unique_ptr<juce::AlertWindow> owner(editor);
            if (result != 1 || groupIndex < 0 || groupIndex >= (int)groups.size())
                return;

            auto& group = groups[(size_t)groupIndex];
            group.name = owner->getTextEditor("name")->getText().trim();
            group.category = owner->getTextEditor("category")->getText().trim();
            group.notes = owner->getTextEditor("notes")->getText().trim();
            if (group.name.isEmpty())
                group.name = group.id;
            if (group.category.isEmpty())
                group.category = "user_group";
            if (onStatus) onStatus("Updated group metadata for " + group.name + ".");
            repaint();
        }), true);
    }

    void ungroupSelectedGroup()
    {
        if (selectedGroup < 0 || selectedGroup >= (int)groups.size())
            return;

        const auto name = groups[(size_t)selectedGroup].name;
        groups.erase(groups.begin() + selectedGroup);
        selectedGroup = -1;
        if (onStatus) onStatus("Removed group box " + name + "; component wiring was unchanged.");
        repaint();
    }

    WireNode nodeAt(juce::Point<float> rawPosition, juce::Point<float> snappedPosition)
    {
        if (auto pin = hitTestPin(rawPosition); pin.instanceIndex >= 0)
            return WireNode::forPin(pin);

        if (auto junction = hitTestJunction(rawPosition); junction >= 0)
            return WireNode::forJunction(junction);

        if (auto rail = hitTestRailBus(rawPosition); rail >= 0)
            return createRailTap(rail, snappedPosition);

        if (auto wireIndex = hitTestWire(rawPosition); wireIndex >= 0)
            return createJunctionOnWire(wireIndex, snappedPosition);

        return {};
    }

    static juce::Point<float> rotateOffset(juce::Point<float> offset, int rotation)
    {
        return schematic::rotateOffset(offset, rotation);
    }

    juce::Rectangle<float> orientedBounds(const Instance& instance, const SymbolDef& symbol) const
    {
        if (isRailBus(instance.symbolId))
            return railBounds(instance).expanded(0.0f, 10.0f);

        return schematic::rotateBounds(symbol.bounds, instance.rotation)
            .translated(instance.position.x, instance.position.y);
    }

    juce::Rectangle<float> railBounds(const Instance& instance) const
    {
        const auto length = std::max(120.0f, instance.busLength);
        return { instance.position.x - length * 0.5f, instance.position.y - 1.5f, length, 3.0f };
    }

    int hitTestRailBus(juce::Point<float> p) const
    {
        for (int i = (int)instances.size() - 1; i >= 0; --i)
        {
            const auto& instance = instances[(size_t)i];
            if (!isRailBus(instance.symbolId) || instance.sheet != currentSheet)
                continue;

            if (railBounds(instance).expanded(0.0f, hitDistance(12.0f)).contains(p))
                return i;
        }
        return -1;
    }

    juce::Rectangle<float> railHandleBounds(const Instance& instance, bool left) const
    {
        const auto rail = railBounds(instance);
        const auto x = left ? rail.getX() : rail.getRight();
        return { x - 7.0f, instance.position.y - 12.0f, 14.0f, 24.0f };
    }

    bool beginRailResize(juce::Point<float> p)
    {
        if (selectedInstance < 0 || selectedInstance >= (int)instances.size())
            return false;

        const auto& instance = instances[(size_t)selectedInstance];
        if (!isRailBus(instance.symbolId))
            return false;

        if (railHandleBounds(instance, true).expanded(hitDistance(4.0f)).contains(p))
        {
            resizingRail = true;
            resizingLeftRailEnd = true;
            resizingRailInstance = selectedInstance;
            fixedRailEndX = railBounds(instance).getRight();
            return true;
        }

        if (railHandleBounds(instance, false).expanded(hitDistance(4.0f)).contains(p))
        {
            resizingRail = true;
            resizingLeftRailEnd = false;
            resizingRailInstance = selectedInstance;
            fixedRailEndX = railBounds(instance).getX();
            return true;
        }

        return false;
    }

    void resizeRail(juce::Point<float> p)
    {
        if (resizingRailInstance < 0 || resizingRailInstance >= (int)instances.size())
            return;

        auto& instance = instances[(size_t)resizingRailInstance];
        if (!isRailBus(instance.symbolId))
            return;

        constexpr auto minLength = 120.0f;
        auto movingX = snapPoint(p).x;
        if (std::abs(movingX - fixedRailEndX) < minLength)
            movingX = fixedRailEndX + (movingX < fixedRailEndX ? -minLength : minLength);

        const auto left = std::min(movingX, fixedRailEndX);
        const auto right = std::max(movingX, fixedRailEndX);
        instance.busLength = right - left;
        instance.position.x = (left + right) * 0.5f;
        notifySelection();
    }

    void beginWireDrag(WireNode node, juce::Point<float> position)
    {
        if (!node.isValid())
            return;

        wireDragStart = node;
        wireDragPosition = position;
        wireDragging = true;
        draggingInstance = false;
        if (onStatus) onStatus("Wire drag: " + nodeLabel(node) + ". Release on another pin or wire.");
    }

    void finishWireDrag(juce::Point<float> position)
    {
        wireDragging = false;
        if (!wireDragStart.isValid())
            return;

        const auto target = nodeAt(position, snapPoint(position));
        if (!target.isValid())
        {
            if (onStatus) onStatus("Wire cancelled.");
            wireDragStart = {};
            return;
        }

        if (sameNode(wireDragStart, target))
        {
            if (onStatus) onStatus("Wire cancelled: start and end are the same connector.");
            wireDragStart = {};
            return;
        }

        wires.push_back({ wireDragStart, target });
        if (onStatus) onStatus("Connected " + nodeLabel(wireDragStart) + " to " + nodeLabel(target) + ".");
        wireDragStart = {};
    }

    void disconnectAt(juce::Point<float> position)
    {
        if (auto wireIndex = hitTestWire(position); wireIndex >= 0)
        {
            const auto removed = wires[(size_t)wireIndex];
            wires.erase(wires.begin() + wireIndex);
            if (onStatus) onStatus("Disconnected wire " + nodeLabel(removed.a) + " to " + nodeLabel(removed.b) + ".");
            return;
        }

        if (auto pin = hitTestPin(position); pin.instanceIndex >= 0)
        {
            disconnectNode(WireNode::forPin(pin));
            return;
        }

        if (auto junction = hitTestJunction(position); junction >= 0)
        {
            disconnectNode(WireNode::forJunction(junction));
            return;
        }

        if (onStatus) onStatus("Nothing to disconnect here.");
    }

    void disconnectNode(WireNode node)
    {
        const auto before = wires.size();
        wires.erase(std::remove_if(wires.begin(), wires.end(), [&](const Wire& wire) {
            return sameNode(wire.a, node) || sameNode(wire.b, node);
        }), wires.end());

        const auto removed = before - wires.size();
        if (onStatus)
            onStatus(removed > 0 ? "Disconnected " + juce::String((int)removed) + " wire(s) from " + nodeLabel(node) + "."
                                 : "No wires connected to " + nodeLabel(node) + ".");
    }

    void assignProbe(const juce::String& id, WireNode node)
    {
        const auto label = probeLabel(id);
        if (label.isEmpty())
            return;

        const auto role = probeRole(id);
        const auto colour = probeColour(id);

        auto found = std::find_if(probes.begin(), probes.end(), [&](const Probe& probe) { return probe.id == id; });
        if (found != probes.end())
        {
            found->node = node;
            found->colour = colour;
        }
        else
        {
            probes.push_back({ id, label, role, node, colour });
        }

        if (onStatus) onStatus("Assigned " + label + " to " + nodeLabel(node) + ".");
        if (onProbeChanged) onProbeChanged(id, label, nodeLabel(node));
    }

    bool releaseProbeAt(juce::Point<float> position)
    {
        for (int i = (int)probes.size() - 1; i >= 0; --i)
        {
            const auto p = nodePosition(probes[(size_t)i].node);
            if (p.getDistanceFrom(position) > hitDistance(16.0f))
                continue;

            const auto id = probes[(size_t)i].id;
            const auto label = probes[(size_t)i].label;
            probes.erase(probes.begin() + i);
            if (onStatus) onStatus("Removed " + label + " from schematic.");
            if (onProbeChanged) onProbeChanged(id, label, {});
            return true;
        }
        return false;
    }

    static juce::String probeLabel(const juce::String& id)
    {
        if (id == "DMM_HI") return "DMM+";
        if (id == "DMM_LO") return "DMM-";
        if (id == "SCOPE_CH1") return "CH1";
        if (id == "SCOPE_CH2") return "CH2";
        return {};
    }

    static juce::String probeRole(const juce::String& id)
    {
        if (id == "DMM_HI") return "dmm.high";
        if (id == "DMM_LO") return "dmm.low";
        if (id == "SCOPE_CH1") return "scope.channel1";
        if (id == "SCOPE_CH2") return "scope.channel2";
        return {};
    }

    static juce::Colour probeColour(const juce::String& id)
    {
        if (id == "DMM_HI") return dmmLeadColour(true);
        if (id == "DMM_LO") return dmmLeadColour(false);
        if (id == "SCOPE_CH1") return scopeChannelColour(0);
        if (id == "SCOPE_CH2") return scopeChannelColour(1);
        return juce::Colour(0xffdce9ee);
    }

    WireNode createJunctionOnWire(int wireIndex, juce::Point<float> position)
    {
        const auto junctionIndex = (int)junctions.size();
        addJunction(position);
        const auto junction = WireNode::forJunction(junctionIndex);

        if (wireIndex >= 0 && wireIndex < (int)wires.size())
        {
            const auto existing = wires[(size_t)wireIndex];
            wires.erase(wires.begin() + wireIndex);
            wires.push_back({ existing.a, junction });
            wires.push_back({ junction, existing.b });
        }

        if (onStatus) onStatus("Added junction " + nodeLabel(junction) + ".");
        return junction;
    }

    WireNode createRailTap(int instanceIndex, juce::Point<float> position)
    {
        if (instanceIndex < 0 || instanceIndex >= (int)instances.size())
            return {};

        auto& instance = instances[(size_t)instanceIndex];
        const auto rail = railBounds(instance);
        position.x = std::clamp(position.x, rail.getX(), rail.getRight());
        position.y = instance.position.y;

        const auto junctionIndex = (int)junctions.size();
        addJunction(position);
        const auto junction = WireNode::forJunction(junctionIndex);
        wires.push_back({ WireNode::forPin({ instanceIndex, 0 }), junction });

        if (onStatus) onStatus("Added tap on " + instance.refdes + " " + instance.busName + ".");
        return junction;
    }

    static bool nodeTouchesInstance(const WireNode& node, int instanceIndex)
    {
        return node.isPin() && node.pin.instanceIndex == instanceIndex;
    }

    static void adjustNodeAfterDeletingInstance(WireNode& node, int instanceIndex)
    {
        if (node.isPin() && node.pin.instanceIndex > instanceIndex)
            --node.pin.instanceIndex;
    }

    void deleteSelected()
    {
        std::set<int> deadInstances;
        for (int index : selectedInstances)
            if (index >= 0 && index < (int)instances.size())
                deadInstances.insert(index);
        if (deadInstances.empty() && selectedInstance >= 0 && selectedInstance < (int)instances.size())
            deadInstances.insert(selectedInstance);

        if (deadInstances.empty())
        {
            if (onStatus) onStatus("Nothing selected to delete.");
            return;
        }

        pushUndoSnapshot();
        std::set<juce::String> deadSheets;
        bool changed = true;
        while (changed)
        {
            changed = false;
            for (int index : deadInstances)
                if (index >= 0 && index < (int)instances.size() && instances[(size_t)index].symbolId == "sub_block")
                    deadSheets.insert(instances[(size_t)index].childSheet);
            for (int i = 0; i < (int)instances.size(); ++i)
                if (deadSheets.count(instances[(size_t)i].sheet) != 0 && deadInstances.insert(i).second)
                    changed = true;
        }

        std::set<int> deadJunctions;
        for (int j = 0; j < (int)junctions.size(); ++j)
            if (deadSheets.count(junctionSheet(j)) != 0)
                deadJunctions.insert(j);

        juce::StringArray names;
        for (int index : deadInstances)
            if (index >= 0 && index < (int)instances.size() && instances[(size_t)index].sheet == currentSheet)
                names.add(instances[(size_t)index].refdes);

        juce::StringArray returnedProbes;
        removeInstancesAndJunctions(deadInstances, deadJunctions, returnedProbes);
        for (const auto& probeId : returnedProbes)
            if (onProbeChanged) onProbeChanged(probeId, {}, {});
        notifySelection();
        if (onStatus) onStatus("Deleted " + juce::String(names.size()) + " selected item(s)"
                               + (names.isEmpty() ? juce::String(".") : ": " + names.joinIntoString(", ") + "."));
        forceDeferredRepaint();
    }

    void notifySelection()
    {
        if (onSelectionChanged == nullptr)
            return;

        if (selectedInstance < 0 || selectedInstance >= (int)instances.size())
        {
            onSelectionChanged(-1, {}, {}, {}, {}, {}, {}, {});
            return;
        }

        const auto& instance = instances[(size_t)selectedInstance];
        onSelectionChanged(selectedInstance, instance.refdes, instance.symbolId, instance.value,
                           instance.frequency, instance.busName, instance.family, instance.manufacturerPart);
    }

    juce::String nextRefdesFor(const juce::String& symbolId) const
    {
        const auto prefix = schematic::refdesPrefixFor(symbolId);
        int highest = 0;
        for (const auto& instance : instances)
        {
            if (!instance.refdes.startsWith(prefix))
                continue;
            const auto suffix = instance.refdes.substring(prefix.length());
            if (suffix.isNotEmpty() && suffix.containsOnly("0123456789"))
                highest = std::max(highest, suffix.getIntValue());
        }
        return prefix + juce::String(highest + 1);
    }

    void placeSymbol(const juce::String& symbolId, juce::Point<float> p)
    {
        const auto symbol = symbolFor(symbolId);
        if (!symbol.isValid())
            return;
        instances.push_back({ symbol.id,
                              nextRefdesFor(symbol.id),
                              defaultValueFor(symbol.id),
                              defaultFrequencyFor(symbol.id),
                              defaultBusNameFor(symbol.id),
                              familyFor(symbol.id),
                              {},
                              p,
                              0,
                              isRailBus(symbol.id) ? 420.0f : 0.0f });
        instances.back().sheet = currentSheet;
        selectedInstance = (int)instances.size() - 1;
        selectedInstances.clear();
        selectedInstances.add(selectedInstance);
        selectedGroup = -1;
        notifySelection();
        if (onStatus) onStatus("Placed " + symbol.title + (snapEnabled ? " at schematic grid." : "."));
    }

    juce::String displayValueFor(const Instance& instance) const
    {
        if (instance.symbolId == "ground" || instance.symbolId == "ground_bus")
            return instance.busName;
        if (instance.symbolId == "power_bus")
            return instance.busName;
        if (instance.symbolId == "ac_voltage_source" || instance.symbolId == "signal_source")
            return instance.value + " @ " + instance.frequency;
        return instance.value;
    }

    void drawSymbolBody(juce::Graphics& g, const Instance& instance, const SymbolDef& symbol)
    {
        const auto selectionBounds = orientedBounds(instance, symbol).expanded(8.0f);
        const auto instanceIndex = (int)(&instance - instances.data());
        if (instanceIndex >= 0 && instanceIndex < (int)instances.size() && isInstanceSelected(instanceIndex))
        {
            g.setColour(juce::Colours::dodgerblue);
            const auto r = selectionBounds;
            const auto s = 8.0f;
            g.drawLine(r.getX(), r.getY(), r.getX() + s, r.getY(), 1.5f);
            g.drawLine(r.getX(), r.getY(), r.getX(), r.getY() + s, 1.5f);
            g.drawLine(r.getRight(), r.getY(), r.getRight() - s, r.getY(), 1.5f);
            g.drawLine(r.getRight(), r.getY(), r.getRight(), r.getY() + s, 1.5f);
            g.drawLine(r.getX(), r.getBottom(), r.getX() + s, r.getBottom(), 1.5f);
            g.drawLine(r.getX(), r.getBottom(), r.getX(), r.getBottom() - s, 1.5f);
            g.drawLine(r.getRight(), r.getBottom(), r.getRight() - s, r.getBottom(), 1.5f);
            g.drawLine(r.getRight(), r.getBottom(), r.getRight(), r.getBottom() - s, 1.5f);

            if (isRailBus(instance.symbolId))
            {
                g.setColour(juce::Colour(0xff0e141a));
                g.fillRoundedRectangle(railHandleBounds(instance, true), 3.0f);
                g.fillRoundedRectangle(railHandleBounds(instance, false), 3.0f);
                g.setColour(juce::Colours::dodgerblue);
                g.drawRoundedRectangle(railHandleBounds(instance, true), 3.0f, 1.5f);
                g.drawRoundedRectangle(railHandleBounds(instance, false), 3.0f, 1.5f);
            }
        }

        if (isRailBus(instance.symbolId))
        {
            drawRailBus(g, instance);
            return;
        }

        g.saveState();
        g.addTransform(juce::AffineTransform::rotation(juce::degreesToRadians((float)instance.rotation))
                           .translated(instance.position.x, instance.position.y));
        if (instance.symbolId == "sub_block")
            schematic::drawBlockArt(g, symbol, instance.value);
        else
            schematic::drawSymbolArt(g, symbol, instance.value);
        g.restoreState();

        drawSymbolLabels(g, instance, symbol);
    }

    void drawRailBus(juce::Graphics& g, const Instance& instance)
    {
        const auto ground = instance.symbolId == "ground_bus";
        g.saveState();
        g.addTransform(juce::AffineTransform::rotation(juce::degreesToRadians((float)instance.rotation))
                           .translated(instance.position.x, instance.position.y));
        g.setColour(ground ? juce::Colour(0xff78dcca) : juce::Colour(0xffffc857));
        const auto length = std::max(120.0f, instance.busLength);
        g.drawLine(-length * 0.5f, 0.0f, length * 0.5f, 0.0f, 3.0f);
        for (float x = -length * 0.5f; x <= length * 0.5f + 0.1f; x += 48.0f)
            g.drawLine(x, -7.0f, x, 7.0f, 1.2f);
        g.setFont(juce::Font(12.0f, juce::Font::bold));
        g.drawText(instance.busName.isNotEmpty() ? instance.busName : (ground ? "0" : "PWR"),
                   juce::Rectangle<float>(-length * 0.5f, ground ? 6.0f : -24.0f, length, 18.0f).toNearestInt(),
                   juce::Justification::centredLeft);
        g.restoreState();
    }

    // Reference designator and value beside the body, always reading left
    // to right (SCH-T1). Wide symbols carry labels above and below; tall
    // symbols carry them stacked on the right. Power symbols show only
    // their net name.
    void drawSymbolLabels(juce::Graphics& g, const Instance& instance, const SymbolDef& symbol)
    {
        const auto bounds = orientedBounds(instance, symbol);
        g.setFont(juce::Font(12.0f));

        if (instance.symbolId == "ground")
            return;

        if (instance.symbolId == "sub_block")
            return; // name and pin names are drawn inside the block

        if (instance.symbolId == "annotation_text")
            return; // note text is drawn inside the symbol body

        if (instance.symbolId == "block_port")
        {
            // Port name inside the bubble, always upright.
            const auto bubble = schematic::rotateBounds(schematic::portBubbleRect(), instance.rotation)
                                    .translated(instance.position.x, instance.position.y);
            g.setColour(juce::Colour(0xffffc857));
            g.setFont(juce::Font(12.0f, juce::Font::bold));
            g.drawText(instance.busName, bubble.toNearestInt(), juce::Justification::centred, true);
            return;
        }

        if (instance.symbolId == "net_label")
        {
            g.setColour(juce::Colour(0xff78dcca));
            g.setFont(juce::Font(11.0f, juce::Font::bold));
            g.drawText(instance.busName, juce::Rectangle<float>(instance.position.x + 16.0f, instance.position.y - 26.0f, 120.0f, 14.0f).toNearestInt(),
                       juce::Justification::centredLeft);
            return;
        }

        if (instance.symbolId == "power_port")
        {
            const auto pointsDown = schematic::normalizedRotation(instance.rotation) == 180;
            const auto label = instance.busName.isNotEmpty() ? instance.busName : juce::String("VCC");
            g.setColour(juce::Colour(0xffffc857));
            g.setFont(juce::Font(12.0f, juce::Font::bold));
            const auto y = pointsDown ? bounds.getBottom() + 2.0f : bounds.getY() - 16.0f;
            g.drawText(label, juce::Rectangle<float>(bounds.getCentreX() - 40.0f, y, 80.0f, 14.0f).toNearestInt(),
                       juce::Justification::centred);
            return;
        }

        const auto valueText = prefs::isOn("display.show_values") ? displayValueFor(instance) : juce::String();
        const auto labels = schematic::labelRectsFor(symbol, instance.rotation);
        const auto size = prefs::get("display.label_size");
        g.setFont(juce::Font(size == "Small" ? 10.5f : size == "Large" ? 14.0f : 12.0f));
        if (prefs::isOn("display.show_refdes"))
        {
            g.setColour(juce::Colour(0xff93a7b0));
            g.drawText(instance.refdes, labels.refdes.translated(instance.position.x, instance.position.y).toNearestInt(),
                       labels.justification);
        }
        if (valueText.isNotEmpty())
        {
            g.setColour(juce::Colour(0xffdce9ee));
            g.drawText(valueText, labels.value.translated(instance.position.x, instance.position.y).toNearestInt(),
                       labels.justification);
        }
    }

    void drawInstances(juce::Graphics& g)
    {
        for (int instanceIndex = 0; instanceIndex < (int)instances.size(); ++instanceIndex)
        {
            const auto& instance = instances[(size_t)instanceIndex];
            if (instance.sheet != currentSheet)
                continue;
            const auto symbol = symbolForInstance(instance);
            drawSymbolBody(g, instance, symbol);

            if (isRailBus(instance.symbolId))
                continue;

            for (size_t i = 0; i < symbol.pins.size(); ++i)
            {
                const PinRef ref { instanceIndex, (int)i };
                const auto pin = pinPosition(ref);

                // Unconnected pins get a small open marker so dangling ends
                // are visible; connected pins draw nothing (SCH-J3).
                if (wireCountAtPin(ref) == 0)
                {
                    g.setColour(juce::Colour(0xffff8a65));
                    g.drawEllipse(pin.x - 3.0f, pin.y - 3.0f, 6.0f, 6.0f, 1.2f);
                }

                const auto pinNames = prefs::get("display.pin_names");
                if (pinNames == "Always" || (pinNames == "Auto" && symbol.showPinNames))
                {
                    auto toward = instance.position - pin;
                    const auto len = std::sqrt(toward.x * toward.x + toward.y * toward.y);
                    if (len > 0.001f)
                        toward = toward / len;
                    const auto at = pin + toward * 20.0f;
                    g.setColour(juce::Colour(0xff93a7b0));
                    g.setFont(juce::Font(10.0f));
                    g.drawText(symbol.pins[i].name, (int)at.x - 18, (int)at.y - 12, 36, 12, juce::Justification::centred);
                }
            }
        }
    }

    int wireCountAtPin(const PinRef& pin) const
    {
        int count = 0;
        for (const auto& wire : wires)
        {
            if (wire.a.isPin() && wire.a.pin.instanceIndex == pin.instanceIndex && wire.a.pin.pinIndex == pin.pinIndex) ++count;
            if (wire.b.isPin() && wire.b.pin.instanceIndex == pin.instanceIndex && wire.b.pin.pinIndex == pin.pinIndex) ++count;
        }
        return count;
    }

    void drawRightAngleWire(juce::Graphics& g, juce::Point<float> a, juce::Point<float> b, juce::Colour colour, float width)
    {
        const auto midX = std::round((a.x + b.x) * 0.5f / 24.0f) * 24.0f;
        juce::Path path;
        path.startNewSubPath(a);
        path.lineTo(midX, a.y);
        path.lineTo(midX, b.y);
        path.lineTo(b);
        g.setColour(colour);
        g.strokePath(path, juce::PathStrokeType(width));
    }

    void drawRoutedWire(juce::Graphics& g, const std::vector<juce::Point<float>>& points, juce::Colour colour, float width)
    {
        if (points.size() < 2)
            return;

        juce::Path path;
        path.startNewSubPath(points.front());
        for (size_t i = 1; i < points.size(); ++i)
            path.lineTo(points[i]);

        g.setColour(colour);
        g.strokePath(path, juce::PathStrokeType(width));
    }

    bool instrumentLeadColourForNode(const WireNode& node, juce::Colour& colour) const
    {
        if (!node.isPin() || node.pin.instanceIndex < 0 || node.pin.instanceIndex >= (int)instances.size())
            return false;

        const auto& instance = instances[(size_t)node.pin.instanceIndex];
        const auto symbol = symbolForInstance(instance);
        if (node.pin.pinIndex < 0 || node.pin.pinIndex >= (int)symbol.pins.size())
            return false;

        const auto pinName = symbol.pins[(size_t)node.pin.pinIndex].name;
        if (instance.symbolId == "digital_multimeter")
        {
            if (pinName == "HI")
            {
                colour = dmmLeadColour(true);
                return true;
            }
            if (pinName == "LO")
            {
                colour = dmmLeadColour(false);
                return true;
            }
        }
        else if (instance.symbolId == "oscilloscope_2ch")
        {
            if (pinName == "CH1")
            {
                colour = scopeChannelColour(0);
                return true;
            }
            if (pinName == "CH2")
            {
                colour = scopeChannelColour(1);
                return true;
            }
            if (pinName == "REF")
            {
                colour = juce::Colour(0xffb5bdc5);
                return true;
            }
        }

        return false;
    }

    juce::Colour schematicWireColour(const Wire& wire) const
    {
        juce::Colour colour;
        if (instrumentLeadColourForNode(wire.a, colour))
            return colour;
        if (instrumentLeadColourForNode(wire.b, colour))
            return colour;
        return juce::Colour(0xfff4d35e);
    }

    void drawWires(juce::Graphics& g)
    {
        for (const auto& wire : wires)
        {
            if (isInternalRailTapWire(wire) || !wireOnSheet(wire))
                continue;

            drawRoutedWire(g, routedWirePoints(wire.a, wire.b), schematicWireColour(wire), 2.2f);
        }

        g.setColour(juce::Colour(0xffffc857));
        for (int i = 0; i < (int)junctions.size(); ++i)
        {
            if (junctionSheet(i) != currentSheet)
                continue;
            int degree = 0;
            for (const auto& wire : wires)
                degree += (wire.a.isJunction() && wire.a.junctionIndex == i ? 1 : 0)
                        + (wire.b.isJunction() && wire.b.junctionIndex == i ? 1 : 0);
            if (degree >= 3)
                g.fillEllipse(junctions[(size_t)i].x - 4.0f, junctions[(size_t)i].y - 4.0f, 8.0f, 8.0f);
        }

        for (int instanceIndex = 0; instanceIndex < (int)instances.size(); ++instanceIndex)
        {
            if (isRailBus(instances[(size_t)instanceIndex].symbolId) || !onSheet(instanceIndex))
                continue;
            const auto symbol = symbolForInstance(instances[(size_t)instanceIndex]);
            for (int p = 0; p < (int)symbol.pins.size(); ++p)
            {
                const PinRef ref { instanceIndex, p };
                if (wireCountAtPin(ref) >= 2)
                {
                    const auto pin = pinPosition(ref);
                    g.fillEllipse(pin.x - 4.0f, pin.y - 4.0f, 8.0f, 8.0f);
                }
            }
        }
    }

    void drawProbes(juce::Graphics& g)
    {
        for (const auto& probe : probes)
        {
            if (!nodeOnSheet(probe.node))
                continue;
            const auto p = nodePosition(probe.node);
            if (p == juce::Point<float>())
                continue;

            g.setColour(probe.colour.withAlpha(0.24f));
            g.fillEllipse(p.x - 13.0f, p.y - 13.0f, 26.0f, 26.0f);
            g.setColour(probe.colour);
            g.drawEllipse(p.x - 10.0f, p.y - 10.0f, 20.0f, 20.0f, 2.0f);
            g.fillEllipse(p.x - 3.0f, p.y - 3.0f, 6.0f, 6.0f);

            auto label = juce::Rectangle<int>((int)p.x + 10, (int)p.y - 20, 64, 18);
            g.setColour(juce::Colour(0xdd0e141a));
            g.fillRoundedRectangle(label.toFloat(), 3.0f);
            g.setColour(probe.colour);
            g.setFont(juce::Font(11.0f, juce::Font::bold));
            g.drawText(probe.label, label.reduced(4, 0), juce::Justification::centredLeft);
        }
    }

    void drawPendingWire(juce::Graphics& g)
    {
        if (!wireDragging || !wireDragStart.isValid()) return;
        drawRoutedWire(g,
                       routedWirePoints(wireDragStart, wireDragPosition, {}),
                       juce::Colour(0x99f4d35e),
                       1.5f);
    }

    /*
    void drawOpAmp(juce::Graphics& g, juce::Rectangle<float> r)
    {
        juce::Path tri;
        tri.startNewSubPath(r.getX(), r.getY());
        tri.lineTo(r.getX(), r.getBottom());
        tri.lineTo(r.getRight(), r.getCentreY());
        tri.closeSubPath();
        g.setColour(juce::Colour(0xff17212b));
        g.fillPath(tri);
        g.setColour(juce::Colour(0xff78dcca));
        g.strokePath(tri, juce::PathStrokeType(2.0f));
        g.setFont(juce::Font(13.0f, juce::Font::bold));
        g.drawText("uA741", r.toNearestInt(), juce::Justification::centred);
        g.drawText("+", (int)r.getX() - 22, (int)r.getY() + 18, 18, 18, juce::Justification::centred);
        g.drawText("-", (int)r.getX() - 22, (int)r.getBottom() - 38, 18, 18, juce::Justification::centred);
    }

    void drawSampleCircuit(juce::Graphics& g)
    {
        const auto c = getLocalBounds().getCentre();
        juce::Point<float> op { (float)c.x - 30.0f, (float)c.y - 55.0f };
        drawOpAmp(g, { op.x, op.y, 130.0f, 110.0f });

        g.setColour(juce::Colour(0xffe8f1f2));
        g.drawLine(op.x - 120.0f, op.y + 28.0f, op.x, op.y + 28.0f, 2.0f);
        g.drawLine(op.x - 120.0f, op.y + 82.0f, op.x, op.y + 82.0f, 2.0f);
        g.drawLine(op.x + 130.0f, op.y + 55.0f, op.x + 245.0f, op.y + 55.0f, 2.0f);

        g.setFont(juce::Font(14.0f, juce::Font::bold));
        g.drawText("IN+", (int)op.x - 170, (int)op.y + 16, 48, 22, juce::Justification::centredRight);
        g.drawText("IN-", (int)op.x - 170, (int)op.y + 70, 48, 22, juce::Justification::centredRight);
        g.drawText("OUT", (int)op.x + 250, (int)op.y + 44, 56, 22, juce::Justification::centredLeft);
    }
    */
};

class FloatingInstrumentWindow final : public juce::DocumentWindow
{
public:
    explicit FloatingInstrumentWindow(const juce::String& name)
        : DocumentWindow(name, juce::Colour(0xff171b20), juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        setResizable(true, true);
        setResizeLimits(360, 240, 2400, 1600);
    }

    void closeButtonPressed() override
    {
        setVisible(false);
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FloatingInstrumentWindow)
};

// ---- Instrument windows: each one is a view of an instrument node ----------

void styleCombo(juce::ComboBox& box)
{
    box.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff1d2731));
    box.setColour(juce::ComboBox::textColourId, juce::Colour(0xffdce9ee));
    box.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff33424d));
    box.setColour(juce::ComboBox::arrowColourId, juce::Colour(0xff93a7b0));
}

void styleSmallLabel(juce::Label& label, const juce::String& text)
{
    label.setText(text, juce::dontSendNotification);
    label.setFont(juce::Font(12.0f));
    label.setColour(juce::Label::textColourId, juce::Colour(0xff93a7b0));
}

void styleField(juce::TextEditor& editor)
{
    styleTextEditor(editor);
    editor.setMultiLine(false);
    editor.setFont(juce::Font("Consolas", 13.0f, juce::Font::plain));
}

// Re-measures when the circuit or this instrument's settings change.
class InstrumentView : public juce::Component, private juce::Timer
{
public:
    InstrumentView(SchematicCanvasPanel* canvasPanel, juce::String instrumentRef)
        : canvas(canvasPanel), refdes(std::move(instrumentRef))
    {
        startTimerHz(3);
    }

protected:
    juce::String setting(const juce::String& key) const { return canvas != nullptr ? canvas->instrumentSetting(refdes, key) : juce::String(); }
    void setSetting(const juce::String& key, const juce::String& value)
    {
        if (canvas != nullptr)
            canvas->setInstrumentSetting(refdes, key, value);
        timerCallback();
    }

    // Fills a combo from the part catalog's choices and wires it to a setting.
    void bindChoice(juce::ComboBox& box, const juce::String& symbolId, const juce::String& key)
    {
        styleCombo(box);
        if (const auto* spec = parts::findParam(symbolId, key))
            for (int i = 0; i < spec->options.size(); ++i)
                box.addItem(spec->options[i], i + 1);
        box.setText(setting(key), juce::dontSendNotification);
        box.onChange = [this, &box, key] { setSetting(key, box.getText()); };
        addAndMakeVisible(box);
    }

    void bindField(juce::TextEditor& editor, const juce::String& key)
    {
        styleField(editor);
        editor.setText(setting(key), false);
        auto commit = [this, &editor, key] { setSetting(key, editor.getText().trim()); };
        editor.onReturnKey = commit;
        editor.onFocusLost = commit;
        addAndMakeVisible(editor);
    }

    virtual void measure() = 0;

    juce::Component::SafePointer<SchematicCanvasPanel> canvas;
    juce::String refdes;

private:
    void timerCallback() override
    {
        if (canvas == nullptr)
            return;
        const auto revision = canvas->modelRevision();
        if (revision == lastRevision)
            return;
        lastRevision = revision;
        measure();
        repaint();
    }

    juce::int64 lastRevision = 0;
};

class ScopeView final : public InstrumentView
{
public:
    ScopeView(SchematicCanvasPanel* canvasPanel, juce::String scopeRef)
        : InstrumentView(canvasPanel, std::move(scopeRef))
    {
        styleSmallLabel(timeLabel, "Time/div");
        styleSmallLabel(triggerLabel, "Trigger");
        styleSmallLabel(levelLabel, "Level");
        styleSmallLabel(ch1Label, "CH1 V/div");
        styleSmallLabel(ch2Label, "CH2 V/div");
        for (auto* label : { &timeLabel, &triggerLabel, &levelLabel, &ch1Label, &ch2Label })
            addAndMakeVisible(*label);
        bindChoice(timePerDiv, "oscilloscope_2ch", "time_per_div");
        bindChoice(triggerSource, "oscilloscope_2ch", "trigger_source");
        bindChoice(triggerSlope, "oscilloscope_2ch", "trigger_slope");
        bindField(triggerLevel, "trigger_level");
        bindChoice(ch1VoltsPerDiv, "oscilloscope_2ch", "ch1_volts_per_div");
        bindChoice(ch2VoltsPerDiv, "oscilloscope_2ch", "ch2_volts_per_div");
        for (auto* slider : { &ch1Position, &ch2Position })
        {
            slider->setSliderStyle(juce::Slider::LinearVertical);
            slider->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
            slider->setRange(-4.0, 4.0, 0.25);
            slider->setColour(juce::Slider::trackColourId, juce::Colour(0xff33424d));
            addAndMakeVisible(*slider);
        }
        ch1Position.setColour(juce::Slider::thumbColourId, channelColour(0));
        ch2Position.setColour(juce::Slider::thumbColourId, channelColour(1));
        ch1Position.setValue(setting("ch1_position").getDoubleValue(), juce::dontSendNotification);
        ch2Position.setValue(setting("ch2_position").getDoubleValue(), juce::dontSendNotification);
        ch1Position.onValueChange = [this] { setSetting("ch1_position", juce::String(ch1Position.getValue())); };
        ch2Position.onValueChange = [this] { setSetting("ch2_position", juce::String(ch2Position.getValue())); };
        ch1Position.setTooltip("CH1 vertical position");
        ch2Position.setTooltip("CH2 vertical position");
        setSize(900, 560);
    }

    static juce::Colour channelColour(int ch) { return ch == 0 ? juce::Colour(0xfff4d35e) : juce::Colour(0xff78dcca); }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff10161d));
        const auto plot = plotArea();
        g.setColour(juce::Colour(0xff070b0f));
        g.fillRoundedRectangle(plot, 6.0f);

        // 10 x 8 graticule with minor ticks on the centre lines.
        for (int i = 0; i <= 10; ++i)
        {
            const auto x = plot.getX() + plot.getWidth() * (float)i / 10.0f;
            g.setColour(i == 5 ? juce::Colour(0xff3a4a56) : juce::Colour(0xff1f2a33));
            g.drawVerticalLine((int)x, plot.getY(), plot.getBottom());
        }
        for (int i = 0; i <= 8; ++i)
        {
            const auto y = plot.getY() + plot.getHeight() * (float)i / 8.0f;
            g.setColour(i == 4 ? juce::Colour(0xff3a4a56) : juce::Colour(0xff1f2a33));
            g.drawHorizontalLine((int)y, plot.getX(), plot.getRight());
        }
        g.setColour(juce::Colour(0xff3a4a56));
        for (int i = 0; i <= 50; ++i)
        {
            const auto x = plot.getX() + plot.getWidth() * (float)i / 50.0f;
            g.drawLine(x, plot.getCentreY() - 3.0f, x, plot.getCentreY() + 3.0f, 1.0f);
        }

        if (!capture.ok)
        {
            g.setColour(juce::Colour(0xffff8a65));
            g.setFont(juce::Font(14.0f));
            g.drawFittedText(capture.error.isNotEmpty() ? capture.error : juce::String("Waiting for the simulation..."),
                             plot.reduced(20.0f).toNearestInt(), juce::Justification::centred, 3);
            return;
        }

        const auto window = 10.0 * capture.timePerDiv;
        for (int ch = 0; ch < 2; ++ch)
        {
            if (!capture.connected[(size_t)ch] || capture.channel[(size_t)ch].empty())
                continue;
            const auto vdiv = std::max(1e-6, voltsPerDiv(ch));
            const auto position = (ch == 0 ? ch1Position : ch2Position).getValue();
            auto yOf = [&](double v) { return plot.getCentreY() - (float)((v / vdiv + position) * plot.getHeight() / 8.0); };

            juce::Path trace;
            for (size_t s = 0; s < capture.time.size(); ++s)
            {
                const auto x = plot.getX() + (float)(capture.time[s] / window) * plot.getWidth();
                const auto y = juce::jlimit(plot.getY() - 2.0f, plot.getBottom() + 2.0f, yOf(capture.channel[(size_t)ch][s]));
                if (s == 0) trace.startNewSubPath(x, y); else trace.lineTo(x, y);
            }
            g.saveState();
            g.reduceClipRegion(plot.toNearestInt());
            g.setColour(channelColour(ch));
            g.strokePath(trace, juce::PathStrokeType(1.8f));
            g.restoreState();

            // Ground marker for the channel at the left edge.
            const auto groundY = juce::jlimit(plot.getY(), plot.getBottom(), yOf(0.0));
            juce::Path marker;
            marker.addTriangle(plot.getX() - 10.0f, groundY - 5.0f, plot.getX() - 10.0f, groundY + 5.0f, plot.getX() - 2.0f, groundY);
            g.fillPath(marker);
        }

        // Trigger level marker on the right edge.
        {
            const auto ch = setting("trigger_source") == "CH2" ? 1 : 0;
            const auto vdiv = std::max(1e-6, voltsPerDiv(ch));
            const auto position = (ch == 0 ? ch1Position : ch2Position).getValue();
            double level = 0.0;
            circuit_sim::parseValue(setting("trigger_level").toStdString(), level);
            const auto y = juce::jlimit(plot.getY(), plot.getBottom(), plot.getCentreY() - (float)((level / vdiv + position) * plot.getHeight() / 8.0));
            juce::Path marker;
            marker.addTriangle(plot.getRight() + 10.0f, y - 5.0f, plot.getRight() + 10.0f, y + 5.0f, plot.getRight() + 2.0f, y);
            g.setColour(capture.triggered ? juce::Colour(0xffff8a65) : juce::Colour(0xff71808c));
            g.fillPath(marker);
        }

        // Status and measurements.
        auto info = getLocalBounds().toFloat().withTop(plot.getBottom() + 8.0f).reduced(16.0f, 0.0f);
        g.setFont(juce::Font("Consolas", 13.0f, juce::Font::plain));
        g.setColour(capture.triggered ? juce::Colour(0xff6fac7d) : juce::Colour(0xffff8a65));
        g.drawText(capture.triggered ? "TRIG'D" : "AUTO", info.removeFromLeft(70.0f).withHeight(18.0f).toNearestInt(), juce::Justification::centredLeft);
        for (int ch = 0; ch < 2; ++ch)
        {
            auto row = info.removeFromTop(20.0f);
            g.setColour(channelColour(ch));
            if (!capture.connected[(size_t)ch])
            {
                g.drawText("CH" + juce::String(ch + 1) + "  not connected", row.toNearestInt(), juce::Justification::centredLeft);
                continue;
            }
            const auto s = SchematicCanvasPanel::channelStatsFor(capture, ch);
            g.drawText("CH" + juce::String(ch + 1)
                           + "   Vpp " + juce::String(circuit_sim::formatValue(s.maximum - s.minimum, "V", 4))
                           + "   Vrms " + juce::String(circuit_sim::formatValue(s.rms, "V", 4))
                           + "   Mean " + juce::String(circuit_sim::formatValue(s.mean, "V", 4))
                           + "   Freq " + (s.frequency > 0.0 ? juce::String(circuit_sim::formatValue(s.frequency, "Hz", 4)) : juce::String("--")),
                       row.toNearestInt(), juce::Justification::centredLeft);
        }
        if (!capture.warnings.isEmpty())
        {
            g.setColour(juce::Colour(0xffff8a65));
            g.drawText(capture.warnings[0], info.removeFromTop(18.0f).toNearestInt(), juce::Justification::centredLeft);
        }
    }

    void resized() override
    {
        auto top = getLocalBounds().reduced(16).removeFromTop(28);
        auto place = [&](juce::Label& label, juce::Component& control, int labelWidth, int width) {
            label.setBounds(top.removeFromLeft(labelWidth));
            control.setBounds(top.removeFromLeft(width).reduced(0, 2));
            top.removeFromLeft(14);
        };
        place(timeLabel, timePerDiv, 56, 82);
        place(ch1Label, ch1VoltsPerDiv, 64, 74);
        place(ch2Label, ch2VoltsPerDiv, 64, 74);
        place(triggerLabel, triggerSource, 50, 66);
        triggerSlope.setBounds(top.removeFromLeft(86).reduced(0, 2));
        top.removeFromLeft(10);
        place(levelLabel, triggerLevel, 38, 70);

        const auto plot = plotArea();
        ch1Position.setBounds((int)plot.getRight() + 16, (int)plot.getY(), 18, (int)plot.getHeight());
        ch2Position.setBounds((int)plot.getRight() + 38, (int)plot.getY(), 18, (int)plot.getHeight());
    }

private:
    juce::Rectangle<float> plotArea() const
    {
        return getLocalBounds().toFloat().reduced(16.0f).withTrimmedTop(44.0f).withTrimmedLeft(14.0f).withTrimmedRight(66.0f).withTrimmedBottom(70.0f);
    }

    double voltsPerDiv(int ch) const
    {
        double v = 0.1;
        circuit_sim::parseValue(setting(ch == 0 ? "ch1_volts_per_div" : "ch2_volts_per_div").toStdString(), v);
        return v;
    }

    void measure() override
    {
        if (canvas != nullptr)
            capture = canvas->captureScope(refdes);
    }

    juce::Label timeLabel, triggerLabel, levelLabel, ch1Label, ch2Label;
    juce::ComboBox timePerDiv, triggerSource, triggerSlope, ch1VoltsPerDiv, ch2VoltsPerDiv;
    juce::TextEditor triggerLevel;
    juce::Slider ch1Position, ch2Position;
    SchematicCanvasPanel::ScopeCapture capture;
};

class MeterView final : public InstrumentView
{
public:
    MeterView(SchematicCanvasPanel* canvasPanel, juce::String meterRef)
        : InstrumentView(canvasPanel, std::move(meterRef))
    {
        const auto* spec = parts::findParam("digital_multimeter", "value");
        for (const auto& option : spec->options)
        {
            auto* button = functionButtons.add(new juce::TextButton(option));
            button->setClickingTogglesState(true);
            button->setRadioGroupId(1001);
            button->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1d2731));
            button->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffffc857));
            button->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffdce9ee));
            button->setColour(juce::TextButton::textColourOnId, juce::Colour(0xff0e141a));
            button->setToggleState(setting("value") == option, juce::dontSendNotification);
            button->onClick = [this, option] { setSetting("value", option); };
            addAndMakeVisible(button);
        }
        setSize(440, 300);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff10161d));
        auto lcd = getLocalBounds().toFloat().reduced(20.0f).withTrimmedBottom(60.0f);
        g.setColour(juce::Colour(0xff1a2620));
        g.fillRoundedRectangle(lcd, 8.0f);
        g.setColour(juce::Colour(0xff33424d));
        g.drawRoundedRectangle(lcd, 8.0f, 1.5f);

        g.setColour(juce::Colour(0xff78dcca));
        g.setFont(juce::Font(14.0f, juce::Font::bold));
        g.drawText(reading.function.isNotEmpty() ? reading.function : setting("value"), lcd.reduced(14.0f).removeFromTop(20.0f).toNearestInt(), juce::Justification::centredLeft);
        g.setFont(juce::Font("Consolas", 46.0f, juce::Font::bold));
        g.setColour(reading.ok ? juce::Colour(0xffb8f2c8) : juce::Colour(0xffff8a65));
        g.drawFittedText(reading.ok ? reading.display : (reading.error.isNotEmpty() ? reading.error : juce::String("----")),
                         lcd.reduced(14.0f).toNearestInt(), juce::Justification::centredRight, 2);
    }

    void resized() override
    {
        auto row = getLocalBounds().reduced(20).removeFromBottom(40);
        const auto w = row.getWidth() / std::max(1, functionButtons.size());
        for (auto* button : functionButtons)
            button->setBounds(row.removeFromLeft(w).reduced(4, 2));
    }

private:
    void measure() override
    {
        if (canvas == nullptr)
            return;
        reading = canvas->readMeter(refdes);
        for (auto* button : functionButtons)
            button->setToggleState(button->getButtonText() == setting("value"), juce::dontSendNotification);
    }

    juce::OwnedArray<juce::TextButton> functionButtons;
    SchematicCanvasPanel::MeterReading reading;
};

class BodeView final : public InstrumentView
{
public:
    BodeView(SchematicCanvasPanel* canvasPanel, juce::String fraRef)
        : InstrumentView(canvasPanel, std::move(fraRef))
    {
        styleSmallLabel(startLabel, "Start");
        styleSmallLabel(stopLabel, "Stop");
        styleSmallLabel(pointsLabel, "Points/decade");
        for (auto* label : { &startLabel, &stopLabel, &pointsLabel })
            addAndMakeVisible(*label);
        bindField(start, "start_frequency");
        bindField(stop, "stop_frequency");
        bindField(points, "points_per_decade");
        setSize(900, 600);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff10161d));
        auto area = getLocalBounds().toFloat().reduced(16.0f).withTrimmedTop(44.0f).withTrimmedBottom(26.0f);
        auto magnitude = area.removeFromTop(area.getHeight() * 0.6f).withTrimmedLeft(56.0f).withTrimmedBottom(10.0f);
        auto phase = area.withTrimmedLeft(56.0f).withTrimmedTop(10.0f);

        if (!sweep.ok)
        {
            g.setColour(juce::Colour(0xffff8a65));
            g.setFont(juce::Font(14.0f));
            g.drawFittedText(sweep.error.isNotEmpty() ? sweep.error : juce::String("Waiting for the simulation..."),
                             magnitude.toNearestInt(), juce::Justification::centred, 3);
            return;
        }

        const auto fMin = sweep.frequency.front(), fMax = sweep.frequency.back();
        auto xOf = [&](juce::Rectangle<float> r, double f) {
            return r.getX() + (float)((std::log10(f) - std::log10(fMin)) / std::max(1e-9, std::log10(fMax) - std::log10(fMin))) * r.getWidth();
        };
        const auto dbMax = std::ceil((sweep.peakDb + 3.0) / 10.0) * 10.0;
        const auto dbMin = std::max(dbMax - 80.0, std::floor(*std::min_element(sweep.gainDb.begin(), sweep.gainDb.end()) / 10.0) * 10.0);
        drawPlot(g, magnitude, fMin, fMax, dbMin, dbMax, 10.0, "dB", sweep.gainDb, juce::Colour(0xff9b8cff), xOf);
        drawPlot(g, phase, fMin, fMax, -180.0, 180.0, 45.0, "deg", sweep.phaseDeg, juce::Colour(0xff78dcca), xOf);

        // -3 dB markers.
        g.setColour(juce::Colour(0xffff8a65));
        for (auto f : sweep.corners)
        {
            const auto x = xOf(magnitude, f);
            const float dashes[] = { 4.0f, 4.0f };
            g.drawDashedLine(juce::Line<float>(x, magnitude.getY(), x, phase.getBottom()), dashes, 2, 1.0f);
        }

        juce::String summary = "Peak gain " + juce::String(sweep.peakDb, 2) + " dB";
        for (auto f : sweep.corners)
            summary << "     -3 dB at " << juce::String(circuit_sim::formatValue(f, "Hz", 4));
        g.setColour(juce::Colour(0xffdce9ee));
        g.setFont(juce::Font("Consolas", 13.0f, juce::Font::plain));
        g.drawText(summary, getLocalBounds().reduced(16).removeFromBottom(20), juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto top = getLocalBounds().reduced(16).removeFromTop(28);
        auto place = [&](juce::Label& label, juce::TextEditor& field, int labelWidth) {
            label.setBounds(top.removeFromLeft(labelWidth));
            field.setBounds(top.removeFromLeft(90).reduced(0, 2));
            top.removeFromLeft(16);
        };
        place(startLabel, start, 40);
        place(stopLabel, stop, 40);
        place(pointsLabel, points, 92);
    }

private:
    template <typename XOf>
    static void drawPlot(juce::Graphics& g, juce::Rectangle<float> r, double fMin, double fMax, double yMin, double yMax, double yStep,
                         const juce::String& unit, const std::vector<double>& values, juce::Colour colour, XOf xOf)
    {
        g.setColour(juce::Colour(0xff070b0f));
        g.fillRoundedRectangle(r, 4.0f);
        g.setFont(juce::Font(11.0f));
        for (double decade = std::pow(10.0, std::floor(std::log10(fMin))); decade <= fMax * 1.0001; decade *= 10.0)
            for (int m = 1; m < 10; ++m)
            {
                const auto f = decade * m;
                if (f < fMin * 0.9999 || f > fMax * 1.0001) continue;
                const auto x = xOf(r, f);
                g.setColour(m == 1 ? juce::Colour(0xff33424d) : juce::Colour(0xff1a232b));
                g.drawVerticalLine((int)x, r.getY(), r.getBottom());
                if (m == 1)
                {
                    g.setColour(juce::Colour(0xff93a7b0));
                    g.drawText(juce::String(circuit_sim::formatValue(f, "Hz", 3)), juce::Rectangle<float>(x - 30.0f, r.getBottom() + 1.0f, 60.0f, 14.0f).toNearestInt(), juce::Justification::centred);
                }
            }
        auto yOf = [&](double v) { return r.getBottom() - (float)((v - yMin) / (yMax - yMin)) * r.getHeight(); };
        for (double v = yMin; v <= yMax + 1e-9; v += yStep)
        {
            g.setColour(juce::Colour(0xff1f2a33));
            g.drawHorizontalLine((int)yOf(v), r.getX(), r.getRight());
            g.setColour(juce::Colour(0xff93a7b0));
            g.drawText(juce::String((int)std::round(v)) + " " + unit, juce::Rectangle<float>(r.getX() - 56.0f, yOf(v) - 7.0f, 52.0f, 14.0f).toNearestInt(), juce::Justification::centredRight);
        }
        juce::Path path;
        // Frequencies are the shared sweep points, spaced like `values`.
        for (size_t k = 0; k < values.size(); ++k)
        {
            const auto f = fMin * std::pow(fMax / fMin, values.size() > 1 ? (double)k / (double)(values.size() - 1) : 0.0);
            const auto x = xOf(r, f);
            const auto y = juce::jlimit(r.getY(), r.getBottom(), yOf(values[k]));
            if (k == 0) path.startNewSubPath(x, y); else path.lineTo(x, y);
        }
        g.setColour(colour);
        g.strokePath(path, juce::PathStrokeType(2.0f));
    }

    void measure() override
    {
        if (canvas != nullptr)
            sweep = canvas->sweepBode(refdes);
    }

    juce::Label startLabel, stopLabel, pointsLabel;
    juce::TextEditor start, stop, points;
    SchematicCanvasPanel::BodeSweep sweep;
};

// Searchable preferences: categories on the left, settings on the right,
// each with the control its kind calls for. Typing in the search box shows
// matching settings from every category.
class PreferencesView final : public juce::Component
{
public:
    PreferencesView(class AudioPipeline* audioPipeline = nullptr) : audioPipeline(audioPipeline)
    {
        search.setTextToShowWhenEmpty("Search preferences (rail, spacing, grid, units...)", juce::Colour(0xff71808c));
        styleTextEditor(search);
        search.setMultiLine(false);
        search.onTextChange = [this] { rebuild(); };
        addAndMakeVisible(search);

        juce::StringArray cats = prefs::categories();
        cats.add("Audio");
        for (const auto& category : cats)
        {
            auto* button = categoryButtons.add(new juce::TextButton(category));
            button->setClickingTogglesState(true);
            button->setRadioGroupId(2001);
            button->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff151a20));
            button->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff23394a));
            button->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffdce9ee));
            button->setColour(juce::TextButton::textColourOnId, juce::Colour(0xff78dcca));
            button->onClick = [this, category] { selectedCategory = category; search.clear(); rebuild(); };
            addAndMakeVisible(button);
        }
        selectedCategory = cats[0];
        categoryButtons[0]->setToggleState(true, juce::dontSendNotification);

        viewport.setViewedComponent(&content, false);
        viewport.setScrollBarsShown(true, false);
        addAndMakeVisible(viewport);
        rebuild();
        setSize(820, 560);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff10161d));
        g.setColour(juce::Colour(0xff1d2731));
        g.fillRect(getLocalBounds().withTrimmedTop(56).removeFromLeft(170));
    }

    void resized() override
    {
        auto area = getLocalBounds();
        search.setBounds(area.removeFromTop(56).reduced(14, 12));
        auto left = area.removeFromLeft(170).reduced(8);
        for (auto* button : categoryButtons)
            button->setBounds(left.removeFromTop(34).reduced(0, 2));
        viewport.setBounds(area.reduced(8, 4));
        layoutRows();
    }

private:
    struct Row
    {
        juce::String key;     // empty for a category heading
        std::unique_ptr<juce::Label> title, description;
        std::unique_ptr<juce::Component> control;
        std::unique_ptr<juce::TextButton> browse;
    };

    void rebuild()
    {
        rows.clear();
        content.removeAllChildren();
        if (selectedCategory == "Audio")
        {
            if (audioPipeline != nullptr)
            {
                audioSelector = std::make_unique<juce::AudioDeviceSelectorComponent>(
                    audioPipeline->getDeviceManager(), 0, 2, 0, 2, false, false, true, false);
                content.addAndMakeVisible(*audioSelector);
            }
            layoutRows();
            return;
        }
        audioSelector.reset();

        const auto query = search.getText().trim().toLowerCase();
        juce::String lastCategory;
        for (const auto& setting : prefs::all())
        {
            const auto haystack = (setting.key + " " + setting.category + " " + setting.label + " " + setting.description).toLowerCase();
            if (query.isNotEmpty() ? !haystack.contains(query) : setting.category != selectedCategory)
                continue;
            if (setting.category != lastCategory)
            {
                Row heading;
                heading.title = std::make_unique<juce::Label>(juce::String(), setting.category);
                heading.title->setFont(juce::Font(15.0f, juce::Font::bold));
                heading.title->setColour(juce::Label::textColourId, juce::Colour(0xff78dcca));
                content.addAndMakeVisible(*heading.title);
                rows.push_back(std::move(heading));
                lastCategory = setting.category;
            }
            rows.push_back(makeRow(setting));
        }
        if (rows.empty())
        {
            Row none;
            none.title = std::make_unique<juce::Label>(juce::String(), "No preference matches \"" + search.getText() + "\".");
            none.title->setColour(juce::Label::textColourId, juce::Colour(0xff93a7b0));
            content.addAndMakeVisible(*none.title);
            rows.push_back(std::move(none));
        }
        layoutRows();
    }

    Row makeRow(const prefs::Setting& setting)
    {
        Row row;
        row.key = setting.key;
        row.title = std::make_unique<juce::Label>(juce::String(), setting.label);
        row.title->setFont(juce::Font(13.5f, juce::Font::bold));
        row.title->setColour(juce::Label::textColourId, juce::Colour(0xffdce9ee));
        row.description = std::make_unique<juce::Label>(juce::String(), setting.description);
        row.description->setFont(juce::Font(12.0f));
        row.description->setColour(juce::Label::textColourId, juce::Colour(0xff93a7b0));
        row.description->setJustificationType(juce::Justification::topLeft);
        row.description->setMinimumHorizontalScale(1.0f);
        const auto key = setting.key;
        const auto current = prefs::get(key);

        switch (setting.kind)
        {
            case prefs::Kind::Toggle:
            {
                auto* toggle = new juce::TextButton();
                toggle->setClickingTogglesState(true);
                toggle->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1d2731));
                toggle->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff6fac7d));
                toggle->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffdce9ee));
                toggle->setColour(juce::TextButton::textColourOnId, juce::Colour(0xff0e141a));
                toggle->setToggleState(current == "true", juce::dontSendNotification);
                toggle->setButtonText(current == "true" ? "On" : "Off");
                toggle->onClick = [toggle, key] {
                    juce::String error;
                    prefs::set(key, toggle->getToggleState() ? "true" : "false", error);
                    toggle->setButtonText(toggle->getToggleState() ? "On" : "Off");
                };
                row.control.reset(toggle);
                break;
            }
            case prefs::Kind::Choice:
            {
                auto* box = new juce::ComboBox();
                styleCombo(*box);
                for (int i = 0; i < setting.options.size(); ++i)
                    box->addItem(setting.options[i], i + 1);
                box->setText(current, juce::dontSendNotification);
                box->onChange = [box, key] {
                    juce::String error;
                    prefs::set(key, box->getText(), error);
                };
                row.control.reset(box);
                break;
            }
            case prefs::Kind::Folder:
            {
                auto* field = new juce::TextEditor();
                styleTextEditor(*field);
                field->setMultiLine(false);
                field->setText(current, false);
                auto commit = [field, key] {
                    juce::String error;
                    if (!prefs::set(key, field->getText().trim(), error))
                        field->setText(prefs::get(key), false);
                };
                field->onReturnKey = commit;
                field->onFocusLost = commit;
                row.control.reset(field);
                row.browse = std::make_unique<juce::TextButton>("Browse...");
                auto* browse = row.browse.get();
                browse->onClick = [this, field, key] {
                    chooser = std::make_unique<juce::FileChooser>("Choose a folder", juce::File(field->getText()));
                    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                         [field, key](const juce::FileChooser& fc) {
                                             if (fc.getResult() == juce::File()) return;
                                             juce::String error;
                                             if (prefs::set(key, fc.getResult().getFullPathName(), error))
                                                 field->setText(fc.getResult().getFullPathName(), false);
                                         });
                };
                content.addAndMakeVisible(*row.browse);
                break;
            }
        }
        content.addAndMakeVisible(*row.title);
        content.addAndMakeVisible(*row.description);
        content.addAndMakeVisible(*row.control);
        return row;
    }

    void layoutRows()
    {
        const auto width = std::max(300, viewport.getWidth() - viewport.getScrollBarThickness() - 4);
        int y = 6;
        if (audioSelector != nullptr)
        {
            audioSelector->setBounds(8, y, width - 16, 400);
            content.setSize(width, y + 420);
            return;
        }

        for (auto& row : rows)
        {
            if (row.key.isEmpty())
            {
                row.title->setBounds(8, y + 6, width - 16, 22);
                y += 36;
                continue;
            }
            const auto controlWidth = row.browse != nullptr ? 300 : 170;
            const auto textWidth = width - controlWidth - 40;
            row.title->setBounds(8, y, textWidth, 20);
            row.description->setBounds(8, y + 20, textWidth, 36);
            if (row.browse != nullptr)
            {
                row.control->setBounds(width - controlWidth - 8, y + 6, controlWidth - 96, 28);
                row.browse->setBounds(width - 98, y + 6, 90, 28);
            }
            else
                row.control->setBounds(width - controlWidth - 8, y + 6, controlWidth, 28);
            y += 66;
        }
        content.setSize(width, y + 8);
    }

    juce::TextEditor search;
    juce::OwnedArray<juce::TextButton> categoryButtons;
    juce::String selectedCategory;
    juce::Viewport viewport;
    juce::Component content;
    std::vector<Row> rows;
    std::unique_ptr<juce::FileChooser> chooser;
    class AudioPipeline* audioPipeline = nullptr;
    std::unique_ptr<juce::AudioDeviceSelectorComponent> audioSelector;
};

class ConsolePanel final : public juce::Component
{
public:
    ConsolePanel(juce::TextEditor*& externalLog)
    {
        styleTextEditor(console, true);
        console.setReadOnly(true);
        externalLog = &console;
        addAndMakeVisible(console);
    }

    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff10161d)); }
    void resized() override { console.setBounds(getLocalBounds().reduced(6)); }

private:
    juce::TextEditor console;
};

struct LogEntry {
    juce::Time time;
    juce::String reason;
    juce::String code;
    juce::String details;
};

class LogPanel final : public juce::Component, public juce::TableListBoxModel
{
public:
    LogPanel()
    {
        table.setModel(this);
        table.getHeader().addColumn("Time", 1, 120);
        table.getHeader().addColumn("Reason", 2, 120);
        table.getHeader().addColumn("Code", 3, 100);
        table.getHeader().addColumn("Details", 4, 400);
        table.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff10161d));
        table.setColour(juce::ListBox::outlineColourId, juce::Colour(0xff33424d));
        table.setHeaderHeight(24);
        table.setRowHeight(22);
        table.setMultipleSelectionEnabled(true);
        addAndMakeVisible(table);
    }
    
    void addEntry(const juce::String& reason, const juce::String& code, const juce::String& details)
    {
        LogEntry e;
        e.time = juce::Time::getCurrentTime();
        e.reason = reason;
        e.code = code;
        e.details = details;
        juce::MessageManager::callAsync([this, e]() {
            entries.push_back(e);
            table.updateContent();
            table.scrollToEnsureRowIsOnscreen((int)entries.size() - 1);
        });
    }
    
    int getNumRows() override { return (int)entries.size(); }
    void paintRowBackground(juce::Graphics& g, int rowNumber, int width, int height, bool rowIsSelected) override
    {
        if (rowIsSelected) g.fillAll(juce::Colour(0xff2a3f54));
        else if (rowNumber % 2 == 0) g.fillAll(juce::Colour(0xff151a20));
        else g.fillAll(juce::Colour(0xff10161d));
    }
    void paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override
    {
        if (rowNumber < 0 || rowNumber >= entries.size()) return;
        auto& e = entries[(size_t)rowNumber];
        g.setColour(juce::Colour(0xffdce9ee));
        g.setFont(14.0f);
        juce::String text;
        if (columnId == 1) text = e.time.formatted("%H:%M:%S") + juce::String::formatted(".%03d", (int)(e.time.toMilliseconds() % 1000));
        else if (columnId == 2) text = e.reason;
        else if (columnId == 3) text = e.code;
        else if (columnId == 4) text = e.details;
        
        g.drawText(text, 6, 0, width - 12, height, juce::Justification::centredLeft, true);
    }

    void cellClicked(int rowNumber, int, const juce::MouseEvent& event) override
    {
        if (event.mods.isPopupMenu())
            showPopup(rowNumber);
    }

    void cellDoubleClicked(int rowNumber, int, const juce::MouseEvent&) override
    {
        showDetails(rowNumber);
    }
    
    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff10161d)); }
    void resized() override { table.setBounds(getLocalBounds().reduced(2)); }

private:
    static juce::String entryTime(const LogEntry& e)
    {
        return e.time.formatted("%Y-%m-%d %H:%M:%S") + juce::String::formatted(".%03d", (int)(e.time.toMilliseconds() % 1000));
    }

    static juce::var entryVar(const LogEntry& e)
    {
        auto* object = new juce::DynamicObject();
        object->setProperty("time", entryTime(e));
        object->setProperty("reason", e.reason);
        object->setProperty("code", e.code);
        object->setProperty("details", e.details);
        return object;
    }

    juce::Array<juce::var> selectedEntryVars() const
    {
        juce::Array<juce::var> rows;
        for (int i = 0; i < (int)entries.size(); ++i)
            if (table.isRowSelected(i))
                rows.add(entryVar(entries[(size_t)i]));
        return rows;
    }

    juce::String entriesJson(bool selectedOnly) const
    {
        juce::Array<juce::var> rows;
        if (selectedOnly)
            rows = selectedEntryVars();
        else
            for (const auto& e : entries)
                rows.add(entryVar(e));
        return juce::JSON::toString(juce::var(rows), true);
    }

    juce::String entriesTsv(bool selectedOnly) const
    {
        auto clean = [](juce::String text) {
            return text.replace("\t", " ").replace("\r\n", " ").replace("\n", " ").replace("\r", " ");
        };
        juce::String text = "Time\tReason\tCode\tDetails\n";
        for (int i = 0; i < (int)entries.size(); ++i)
        {
            if (selectedOnly && !table.isRowSelected(i))
                continue;
            const auto& e = entries[(size_t)i];
            text << clean(entryTime(e)) << "\t" << clean(e.reason) << "\t" << clean(e.code) << "\t" << clean(e.details) << "\n";
        }
        return text;
    }

    void copyJson(bool selectedOnly)
    {
        juce::SystemClipboard::copyTextToClipboard(entriesJson(selectedOnly));
    }

    void copyData(bool selectedOnly)
    {
        juce::SystemClipboard::copyTextToClipboard(entriesTsv(selectedOnly));
    }

    void showDetails(int rowNumber)
    {
        if (rowNumber < 0 || rowNumber >= (int)entries.size())
            return;

        const auto json = juce::JSON::toString(entryVar(entries[(size_t)rowNumber]), true);
        auto* dialog = new juce::AlertWindow("Log Entry Details", json, juce::AlertWindow::NoIcon);
        dialog->addButton("Copy JSON", 1);
        dialog->addButton("Close", 0);
        dialog->enterModalState(true, juce::ModalCallbackFunction::create([json](int result) {
            if (result == 1)
                juce::SystemClipboard::copyTextToClipboard(json);
        }), true);
    }

    void showPopup(int rowNumber)
    {
        juce::PopupMenu menu;
        if (rowNumber >= 0 && rowNumber < (int)entries.size())
            menu.addItem(1, "Open entry details");
        menu.addItem(2, "Copy selected as JSON", table.getNumSelectedRows() > 0);
        menu.addItem(3, "Copy whole log as JSON", !entries.empty());
        menu.addItem(4, "Copy Data for selected log rows", table.getNumSelectedRows() > 0);
        menu.addItem(5, "Copy Data for whole log", !entries.empty());
        menu.showMenuAsync(juce::PopupMenu::Options(), [this, rowNumber](int result) {
            if (result == 1) showDetails(rowNumber);
            else if (result == 2) copyJson(true);
            else if (result == 3) copyJson(false);
            else if (result == 4) copyData(true);
            else if (result == 5) copyData(false);
        });
    }

    juce::TableListBox table;
    std::vector<LogEntry> entries;
};

// Properties for the selected part, with controls chosen from the part
// catalog: values with units get validated fields, choices get dropdowns,
// on/off settings get toggles, the potentiometer wiper gets a slider.
class PropertiesPanel final : public juce::Component, private juce::Timer
{
public:
    explicit PropertiesPanel(SchematicCanvasPanel* canvasPanel) : canvas(canvasPanel)
    {
        viewport.setViewedComponent(&content, false);
        viewport.setScrollBarsShown(true, false);
        viewport.setScrollBarThickness(10);
        addAndMakeVisible(viewport);
        saveButton.setTooltip("Apply the edited fields to the part (Enter in a field does the same)");
        saveButton.onClick = [this] { commitPending(); };
        addAndMakeVisible(saveButton);
        updateSaveButton();
        startTimerHz(2);
        rebuild();
    }

    void showPart(const juce::String& refdes)
    {
        if (refdes == current && !rows.empty())
        {
            refreshValues();
            return;
        }
        current = refdes;
        rebuild();
    }

    // Applies every edited text field that has not been applied yet.
    void commitPending()
    {
        for (size_t i = 0; i < rows.size(); ++i)
            if (rows[i].dirty)
                commitField(i);
        updateSaveButton();
    }

    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff151a20)); }

    void resized() override
    {
        auto area = getLocalBounds();
        auto top = area.removeFromTop(38).reduced(10, 6);
        saveButton.setBounds(top.removeFromRight(110));
        viewport.setBounds(area);
        layout();
    }

private:
    static juce::String unitSymbol(const juce::String& unit)
    {
        if (unit == "ohm") return prefs::get("units.ohm_symbol");
        return unit;
    }

    struct Row
    {
        juce::String key;              // catalog key, or "#refdes", "#pin:<name>"
        parts::ParamSpec spec;
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::Component> control;
        std::unique_ptr<juce::Label> hint;
        int height = 50;
        bool dirty = false;    // text edited but not applied yet
    };

    juce::TextButton saveButton { "Save" };

    bool anyDirty() const
    {
        for (const auto& row : rows)
            if (row.dirty) return true;
        return false;
    }

    void updateSaveButton()
    {
        const auto dirty = anyDirty();
        saveButton.setEnabled(dirty);
        saveButton.setButtonText(dirty ? "Save changes" : "Saved");
        saveButton.setColour(juce::TextButton::buttonColourId, dirty ? juce::Colour(0xff2f7f73) : juce::Colour(0xff1d2731));
        saveButton.setColour(juce::TextButton::textColourOffId, dirty ? juce::Colours::white : juce::Colour(0xff71808c));
    }

    // The value a text row stands for in the model.
    juce::String storedValue(const Row& row) const
    {
        if (canvas == nullptr) return {};
        if (row.key == "#refdes") return current;
        if (row.key.startsWith("#pin:")) return row.key.fromFirstOccurrenceOf("#pin:", false, false);
        if (row.key == "#blockname") return canvas->blockName(current);
        return canvas->instrumentSetting(current, row.key);
    }

    // Wires a text row: Enter applies it, edits light up Save.
    void wireTextRow(juce::TextEditor* editor, size_t rowIndex, bool validate)
    {
        editor->onReturnKey = [this, rowIndex] { 
            for (size_t i = rowIndex + 1; i < rows.size(); ++i) {
                if (auto* nextEditor = dynamic_cast<juce::TextEditor*>(rows[i].control.get())) {
                    nextEditor->grabKeyboardFocus();
                    return;
                }
            }
            saveButton.grabKeyboardFocus();
        };
        editor->onTextChange = [this, rowIndex, validate] {
            auto& r = rows[rowIndex];
            const auto text = dynamic_cast<juce::TextEditor*>(r.control.get())->getText();
            r.dirty = text.trim() != storedValue(r);
            if (validate)
            {
                juce::String error;
                const auto ok = parts::validate(r.spec, text, error);
                setHint(r, ok ? describe(r.spec, text) + (r.dirty ? "   (not saved)" : juce::String()) : error, !ok);
            }
            else
                setHint(r, r.dirty ? juce::String("(not saved)") : juce::String(), false);
            updateSaveButton();
        };
    }

    juce::Label* makeLabel(const juce::String& text, float size, juce::Colour colour, bool bold = false)
    {
        auto* label = new juce::Label({}, text);
        label->setFont(juce::Font(size, bold ? juce::Font::bold : juce::Font::plain));
        label->setColour(juce::Label::textColourId, colour);
        label->setMinimumHorizontalScale(1.0f);
        return label;
    }

    void addHeading(const juce::String& text)
    {
        Row row;
        row.key = "#heading";
        row.label.reset(makeLabel(text, 12.0f, juce::Colour(0xff78dcca), true));
        row.height = 26;
        content.addAndMakeVisible(*row.label);
        rows.push_back(std::move(row));
    }

    juce::TextEditor* makeField(const juce::String& text)
    {
        auto* editor = new juce::TextEditor();
        styleTextEditor(*editor);
        editor->setMultiLine(false);
        editor->setFont(juce::Font("Consolas", 13.5f, juce::Font::plain));
        editor->setText(text, false);
        return editor;
    }

    void addReadOnlyRow(const juce::String& label, const juce::String& value, const juce::String& hint)
    {
        Row row;
        row.key = "#readonly:" + label;
        row.label.reset(makeLabel(label, 12.5f, juce::Colour(0xff93a7b0)));
        row.control.reset(makeLabel(value, 13.5f, juce::Colour(0xffdce9ee)));
        row.hint.reset(makeLabel(hint, 11.0f, juce::Colour(0xff71808c)));
        row.height = 64;
        content.addAndMakeVisible(*row.label);
        content.addAndMakeVisible(*row.control);
        content.addAndMakeVisible(*row.hint);
        rows.push_back(std::move(row));
    }

    void setHint(Row& row, const juce::String& text, bool error)
    {
        if (row.hint == nullptr) return;
        row.hint->setText(text, juce::dontSendNotification);
        row.hint->setColour(juce::Label::textColourId, error ? juce::Colour(0xffff8a65) : juce::Colour(0xff71808c));
        if (auto* editor = dynamic_cast<juce::TextEditor*>(row.control.get()))
            editor->setColour(juce::TextEditor::outlineColourId, error ? juce::Colour(0xffff8a65) : (row.dirty ? juce::Colour(0xffe74c3c) : juce::Colour(0xff33424d)));
    }

    juce::String describe(const parts::ParamSpec& spec, const juce::String& value) const
    {
        if (spec.kind != parts::Kind::Quantity)
            return spec.help;
        juce::String error;
        if (!parts::validate(spec, value, error))
            return error;
        double v = 0.0;
        auto text = value.trim();
        if (spec.unit == "V" && text.containsChar('V') && text.upToFirstOccurrenceOf("V", false, false).containsOnly("0123456789")
            && text.fromFirstOccurrenceOf("V", false, false).containsOnly("0123456789") && text.fromFirstOccurrenceOf("V", false, false).isNotEmpty())
            text = text.replace("V", ".");
        circuit_sim::parseValue(text.toStdString(), v);
        auto shown = spec.unit.isEmpty() ? juce::String(v, 4).trimCharactersAtEnd("0").trimCharactersAtEnd(".")
                                         : juce::String(circuit_sim::formatValue(v, unitSymbol(spec.unit).toStdString(), 4));
        return "= " + shown + (spec.help.isNotEmpty() ? "   " + spec.help : juce::String());
    }

    void addParamRow(const parts::ParamSpec& originalSpec)
    {
        parts::ParamSpec spec = originalSpec;
        if (spec.key == "value" && spec.label == "Model")
        {
            juce::String kind;
            if (view.symbolId == "njfet") kind = "NJF";
            else if (view.symbolId == "pjfet") kind = "PJF";
            else if (view.symbolId == "npn") kind = "NPN";
            else if (view.symbolId == "pnp") kind = "PNP";
            else if (view.symbolId == "nmos") kind = "NMOS";
            else if (view.symbolId == "pmos") kind = "PMOS";
            else if (view.symbolId == "diode") kind = "D";
            else if (view.symbolId == "opamp_generic" || view.symbolId == "opamp_741"
                     || view.symbolId == "comparator_generic" || view.symbolId == "comparator_lm311")
                kind = "SUBCKT";

            if (kind.isNotEmpty())
            {
                auto models = spice_library::availableModelsFor(kind);
                if (!models.empty())
                {
                    spec.kind = parts::Kind::Choice;
                    spec.options.clear();
                    spec.options.add(originalSpec.defaultValue);
                    for (const auto& m : models)
                        if (m != originalSpec.defaultValue)
                            spec.options.add(m);
                    if (view.symbolId == "nmos" && spice_library::findModel("Si4778DY") != nullptr)
                        spec.options.addIfNotAlreadyThere("Si4778DY");
                }
            }
        }

        Row row;
        row.key = spec.key;
        row.spec = spec;
        row.label.reset(makeLabel(spec.label + (spec.unit.isNotEmpty() && spec.kind == parts::Kind::Quantity ? " (" + unitSymbol(spec.unit) + ")" : juce::String()),
                                  12.5f, juce::Colour(0xff93a7b0)));
        row.hint.reset(makeLabel({}, 11.0f, juce::Colour(0xff71808c)));
        const auto value = canvas->instrumentSetting(current, spec.key);
        const auto key = spec.key;

        switch (spec.kind)
        {
            case parts::Kind::Quantity:
            case parts::Kind::Text:
            {
                auto* editor = makeField(value);
                row.control.reset(editor);
                wireTextRow(editor, rows.size(), true);
                row.height = 64;
                break;
            }
            case parts::Kind::Choice:
            {
                auto* box = new juce::ComboBox();
                styleCombo(*box);
                for (int i = 0; i < spec.options.size(); ++i)
                    box->addItem(spec.options[i], i + 1);
                box->setText(value, juce::dontSendNotification);
                box->onChange = [this, box, key] {
                    commit(key, box->getText());
                    if (key == "waveform")
                    {
                        // The waveform decides which source properties apply.
                        juce::Component::SafePointer<PropertiesPanel> safe(this);
                        juce::MessageManager::callAsync([safe] { if (safe != nullptr) safe->rebuild(); });
                    }
                };
                row.control.reset(box);
                row.height = 58;
                break;
            }
            case parts::Kind::Toggle:
            {
                auto* button = new juce::TextButton();
                button->setClickingTogglesState(true);
                button->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1d2731));
                button->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff6fac7d));
                button->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffdce9ee));
                button->setColour(juce::TextButton::textColourOnId, juce::Colour(0xff0e141a));
                const auto on = value == spec.options[1];
                button->setToggleState(on, juce::dontSendNotification);
                button->setButtonText(on ? spec.options[1] : spec.options[0]);
                const auto options = spec.options;
                button->onClick = [this, button, key, options] {
                    const auto state = button->getToggleState() ? options[1] : options[0];
                    button->setButtonText(state);
                    commit(key, state);
                };
                row.control.reset(button);
                row.height = 58;
                break;
            }
            case parts::Kind::Fraction:
            {
                auto* slider = new juce::Slider(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
                slider->setRange(0.0, 1.0, 0.01);
                slider->setValue(value.getDoubleValue(), juce::dontSendNotification);
                slider->setColour(juce::Slider::thumbColourId, juce::Colour(0xffffc857));
                slider->setColour(juce::Slider::trackColourId, juce::Colour(0xff33424d));
                slider->setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffdce9ee));
                slider->setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xff33424d));
                slider->onValueChange = [this, slider, key] { commit(key, juce::String(slider->getValue(), 2)); };
                row.control.reset(slider);
                row.height = 58;
                break;
            }
        }
        setHint(row, describe(spec, value), false);
        content.addAndMakeVisible(*row.label);
        content.addAndMakeVisible(*row.control);
        content.addAndMakeVisible(*row.hint);
        rows.push_back(std::move(row));
    }

    void commitField(size_t rowIndex)
    {
        if (rowIndex >= rows.size() || canvas == nullptr)
            return;
        auto& row = rows[rowIndex];
        auto* editor = dynamic_cast<juce::TextEditor*>(row.control.get());
        if (editor == nullptr)
            return;
        const auto text = editor->getText().trim();
        juce::String error;
        bool ok = false;
        if (row.key == "#refdes")
        {
            ok = text == current || canvas->renamePart(current, text, error);
            if (ok) current = text;
        }
        else if (row.key.startsWith("#pin:"))
        {
            const auto oldName = row.key.fromFirstOccurrenceOf("#pin:", false, false);
            ok = text == oldName || canvas->renameBlockPin(current, oldName, text, error);
            if (ok) row.key = "#pin:" + text;
        }
        else if (row.key == "#blockname")
        {
            ok = canvas->setBlockName(current, text, error);
        }
        else
        {
            if (text == canvas->instrumentSetting(current, row.key))
            {
                row.dirty = false;
                setHint(row, describe(row.spec, text), false);
                return;
            }
            ok = canvas->setPartParameter(current, row.key, text, error);
        }
        if (ok) row.dirty = false;
        setHint(row, ok ? (row.key.startsWith("#") ? juce::String() : describe(row.spec, text)) : error, !ok);
    }

    void commit(const juce::String& key, const juce::String& value)
    {
        if (canvas == nullptr || value == canvas->instrumentSetting(current, key))
            return;
        juce::String error;
        if (!canvas->setPartParameter(current, key, value, error))
            for (auto& row : rows)
                if (row.key == key)
                    setHint(row, error, true);
    }

    void rebuild()
    {
        rows.clear();
        actionButtons.clear();
        updateSaveButton();
        content.removeAllChildren();
        pinLabel.reset();
        title.reset(makeLabel("Properties", 16.0f, juce::Colour(0xff78dcca), true));
        content.addAndMakeVisible(*title);

        view = canvas != nullptr && current.isNotEmpty() ? canvas->partView(current) : SchematicCanvasPanel::PartView {};
        if (!view.ok)
        {
            subtitle.reset(makeLabel("Select one part on the schematic to edit it.", 13.0f, juce::Colour(0xff71808c)));
            content.addAndMakeVisible(*subtitle);
            layout();
            return;
        }

        subtitle.reset(makeLabel(parts::displayName(view.symbolId)
                                     + (view.sheet != "Main" ? "   on " + view.sheet : juce::String())
                                     + (view.groups.isEmpty() ? juce::String() : "   in " + view.groups.joinIntoString(", ")),
                                 13.0f, juce::Colour(0xffdce9ee)));
        content.addAndMakeVisible(*subtitle);

        // Reference designator.
        {
            Row row;
            row.key = "#refdes";
            row.label.reset(makeLabel("Reference designator", 12.5f, juce::Colour(0xff93a7b0)));
            row.hint.reset(makeLabel({}, 11.0f, juce::Colour(0xff71808c)));
            auto* editor = makeField(view.refdes);
            wireTextRow(editor, rows.size(), false);
            row.control.reset(editor);
            row.height = 64;
            content.addAndMakeVisible(*row.label);
            content.addAndMakeVisible(*row.control);
            content.addAndMakeVisible(*row.hint);
            rows.push_back(std::move(row));
        }

        // Orientation.
        if (!schematic::isRailBus(view.symbolId))
        {
            for (const auto& [text, delta] : { std::pair<const char*, int> { "Rotate left", 270 }, { "Rotate right", 90 } })
            {
                auto* button = actionButtons.add(new juce::TextButton(text));
                styleActionButton(*button);
                const auto step = delta;
                button->onClick = [this, step] {
                    juce::String error;
                    canvas->setPartRotation(current, view.rotation + step, error);
                    view = canvas->partView(current);
                };
                content.addAndMakeVisible(button);
            }
        }

        const auto& specs = parts::paramsFor(view.symbolId);
        addReadOnlyRow("Simulation fidelity", view.simulationFidelity.isNotEmpty() ? view.simulationFidelity : parts::simulationFidelity(view.symbolId),
                       "primitive, generic_model, vendor_model, ideal, or unsupported");
        if (view.symbolId == "sub_block")
        {
            addHeading("Sub-diagram");
            Row row;
            row.key = "#blockname";
            row.label.reset(makeLabel("Name", 12.5f, juce::Colour(0xff93a7b0)));
            row.hint.reset(makeLabel({}, 11.0f, juce::Colour(0xff71808c)));
            auto* editor = makeField(canvas->blockName(current));
            wireTextRow(editor, rows.size(), false);
            row.control.reset(editor);
            row.height = 64;
            content.addAndMakeVisible(*row.label);
            content.addAndMakeVisible(*row.control);
            content.addAndMakeVisible(*row.hint);
            rows.push_back(std::move(row));
            addHeading("Pins");
            for (const auto& [pin, voltage] : canvas->blockPins(current))
            {
                Row pinRow;
                pinRow.key = "#pin:" + pin;
                pinRow.label.reset(makeLabel(voltage, 12.5f, juce::Colour(0xff93a7b0)));
                pinRow.hint.reset(makeLabel({}, 11.0f, juce::Colour(0xff71808c)));
                auto* field = makeField(pin);
                wireTextRow(field, rows.size(), false);
                pinRow.control.reset(field);
                pinRow.height = 64;
                content.addAndMakeVisible(*pinRow.label);
                content.addAndMakeVisible(*pinRow.control);
                content.addAndMakeVisible(*pinRow.hint);
                rows.push_back(std::move(pinRow));
            }
            addHeading("Exposed parameters");
            auto* expose = actionButtons.add(new juce::TextButton("Expose internal parameter..."));
            styleActionButton(*expose);
            expose->onClick = [this] { canvas->promptExposeBlockParameterFor(current); };
            content.addAndMakeVisible(expose);
        }
        else if (!specs.empty())
        {
            addHeading(schematic::isInstrumentSymbol(view.symbolId) ? "Settings" : "Parameters");
            const auto valueOf = [this](const juce::String& key) { return canvas->instrumentSetting(current, key); };
            for (const auto& spec : specs)
                if (parts::isShown(spec, valueOf))
                    addParamRow(spec);
        }

        if (schematic::isInstrumentSymbol(view.symbolId))
        {
            auto* button = actionButtons.add(new juce::TextButton("Open " + parts::displayName(view.symbolId)));
            styleActionButton(*button);
            button->onClick = [this] { canvas->openInstrumentFor(current); };
            content.addAndMakeVisible(button);
        }

        if (!view.pinVoltages.empty())
        {
            addHeading("DC operating point");
            pinLabel.reset(makeLabel({}, 12.5f, juce::Colour(0xffdce9ee)));
            pinLabel->setFont(juce::Font("Consolas", 12.5f, juce::Font::plain));
            content.addAndMakeVisible(*pinLabel);
            updatePinText();
        }
        layout();
    }

    void updatePinText()
    {
        if (pinLabel == nullptr) return;
        juce::StringArray lines;
        for (const auto& [pin, voltage] : view.pinVoltages)
            lines.add(pin.paddedRight(' ', 6) + voltage);
        pinLabel->setText(lines.joinIntoString("\n"), juce::dontSendNotification);
    }

    static void styleActionButton(juce::TextButton& button)
    {
        button.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff253341));
        button.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffdce9ee));
    }

    void refreshValues()
    {
        if (canvas == nullptr || current.isEmpty())
            return;
        const auto fresh = canvas->partView(current);
        if (!fresh.ok || fresh.symbolId != view.symbolId)
        {
            rebuild();
            return;
        }
        view = fresh;
        for (auto& row : rows)
        {
            if (row.key.startsWith("#") || row.control == nullptr || row.control->hasKeyboardFocus(true) || row.dirty)
                continue;
            const auto value = canvas->instrumentSetting(current, row.key);
            if (auto* editor = dynamic_cast<juce::TextEditor*>(row.control.get()))
            {
                if (editor->getText() != value) { editor->setText(value, false); setHint(row, describe(row.spec, value), false); }
            }
            else if (auto* box = dynamic_cast<juce::ComboBox*>(row.control.get()))
                box->setText(value, juce::dontSendNotification);
            else if (auto* slider = dynamic_cast<juce::Slider*>(row.control.get()))
                slider->setValue(value.getDoubleValue(), juce::dontSendNotification);
            else if (auto* button = dynamic_cast<juce::TextButton*>(row.control.get()))
            {
                const auto on = row.spec.options.size() == 2 && value == row.spec.options[1];
                button->setToggleState(on, juce::dontSendNotification);
                button->setButtonText(on ? row.spec.options[1] : row.spec.options[0]);
            }
        }
        updatePinText();
    }

    void timerCallback() override
    {
        if (canvas == nullptr)
            return;
        const auto selected = canvas->selectedRefdes();
        if (selected != current)
        {
            current = selected;
            rebuild();
            return;
        }
        const auto revision = canvas->modelRevision();
        if (revision != lastRevision)
        {
            lastRevision = revision;
            refreshValues();
        }
    }

    void layout()
    {
        const auto width = std::max(160, viewport.getWidth() - viewport.getScrollBarThickness() - 4);
        int y = 8;
        const int x = 10, w = width - 20;
        if (title != nullptr) { title->setBounds(x, y, w - 120, 24); y += 26; }
        if (subtitle != nullptr) { subtitle->setBounds(x, y, w, 20); y += 28; }
        size_t buttonIndex = 0;
        for (size_t i = 0; i < rows.size(); ++i)
        {
            auto& row = rows[i];
            if (row.key == "#heading")
            {
                row.label->setBounds(x, y + 4, w, 20);
                y += row.height;
                continue;
            }
            row.label->setBounds(x, y, w, 18);
            if (row.control != nullptr) row.control->setBounds(x, y + 19, w, 28);
            if (row.hint != nullptr) row.hint->setBounds(x, y + 48, w, 15);
            y += row.height;
            // Rotate buttons sit right after the reference designator.
            if (row.key == "#refdes" && !schematic::isRailBus(view.symbolId))
            {
                for (int b = 0; b < 2 && buttonIndex < (size_t)actionButtons.size(); ++b, ++buttonIndex)
                    actionButtons[(int)buttonIndex]->setBounds(x + b * (w / 2 + 2), y, w / 2 - 2, 26);
                y += 34;
            }
        }
        for (; buttonIndex < (size_t)actionButtons.size(); ++buttonIndex)
        {
            actionButtons[(int)buttonIndex]->setBounds(x, y + 4, w, 28);
            y += 38;
        }
        if (pinLabel != nullptr)
        {
            const auto lines = std::max(1, (int)view.pinVoltages.size());
            pinLabel->setBounds(x, y, w, lines * 17 + 6);
            y += lines * 17 + 12;
        }
        content.setSize(width, y + 12);
    }

    juce::Component::SafePointer<SchematicCanvasPanel> canvas;
    juce::Viewport viewport;
    juce::Component content;
    juce::String current;
    SchematicCanvasPanel::PartView view;
    std::vector<Row> rows;
    juce::OwnedArray<juce::TextButton> actionButtons;
    std::unique_ptr<juce::Label> title, subtitle, pinLabel;
    juce::int64 lastRevision = 0;
};

class AgentPanel final : public juce::Component
{
public:
    using ExternalCompletion = LocalAgentApi::Completion;

    struct HostTools
    {
        std::function<juce::String()> inspectCircuit;
        std::function<juce::String()> runErc;
        std::function<juce::String()> exportArtifacts;
        std::function<juce::String()> exportRealtimePreview;
        std::function<juce::String(const juce::String&, const juce::String&)> writeMarkdown;
        std::function<juce::String(const juce::String&, int)> webSearch;
        std::function<juce::String(const juce::String&, float, float, const juce::String&,
                                   const juce::String&, const juce::String&)> placeSymbol;
        std::function<juce::String(const juce::String&, const juce::String&)> connectNodes;
        std::function<juce::String(const juce::String&)> openInstrument;
        std::function<juce::String(double, double)> designHighPass;
        std::function<juce::String()> designPushPull;
        std::function<juce::String()> autoLayout;
        std::function<juce::String()> autoLayoutSelection;
        std::function<juce::String(const juce::String&, int)> cookbookLookup;
        std::function<juce::String()> cookbookCoverage;
        std::function<juce::String()> cookbookValidate;
        std::function<juce::String(const juce::String&)> cookbookAcceptanceGoals;
        std::function<juce::String(const juce::String&)> cookbookAcceptanceSummary;
        std::function<juce::String(const juce::String&)> cookbookAcceptanceStart;
        std::function<juce::String(const juce::String&, const juce::String&, const juce::String&,
                                   const juce::String&, const juce::String&, const juce::String&)> cookbookAcceptanceRecord;
        std::function<juce::String(const juce::String&, const juce::String&, const juce::String&,
                                   const juce::String&, const juce::String&, const juce::String&)> capabilityGapRecord;
        std::function<juce::String()> toolManifest;
        std::function<juce::String(const juce::String&, const juce::var&)> schematicTool;
        // Long-running tools (PCB routing): called on the message thread; when it
        // returns true it has taken the call and calls `done` with the result later.
        std::function<bool(const juce::String&, const juce::var&, std::function<void(juce::String)>)> longTool;
        std::function<void(const juce::String&)> log;
    };

    explicit AgentPanel(HostTools hostTools)
        : tools(std::move(hostTools)),
          aiConfig(aiConfigFile().getFullPathName().toStdString()),
          transcriptBrowser(juce::WebBrowserComponent::Options()
              .withBackend(juce::WebBrowserComponent::Options::Backend::webview2)
              .withKeepPageLoadedWhenBrowserIsHidden())
    {
        title.setText("BYOK Electronics Agent", juce::dontSendNotification);
        title.setFont(juce::Font(16.0f, juce::Font::bold));
        title.setColour(juce::Label::textColourId, juce::Colour(0xff78dcca));
        addAndMakeVisible(title);

        profileBox.setTooltip("AI account/profile");
        profileBox.onChange = [this] { refreshModelList(); };
        addAndMakeVisible(profileBox);

        modelBox.setEditableText(true);
        modelBox.setTooltip("Model used by the selected BYOK profile");
        addAndMakeVisible(modelBox);

        settingsButton.setButtonText("Settings");
        settingsButton.onClick = [this] { showAiSettingsForSelected(); };
        addAndMakeVisible(settingsButton);

        cardsButton.setButtonText("Cards");
        cardsButton.onClick = [this] { showRetrievedCardsForDraft(); };
        addAndMakeVisible(cardsButton);

        ercButton.setButtonText("Run ERC");
        ercButton.onClick = [this] {
            appendTranscript("tool", executeToolNow(toolCall("circuit_run_erc", "{}")));
        };
        addAndMakeVisible(ercButton);

        exportButton.setButtonText("Export");
        exportButton.onClick = [this] {
            appendTranscript("tool", executeToolNow(toolCall("simulation_export_artifacts", "{}")));
        };
        addAndMakeVisible(exportButton);

        transcriptMarkdown = "BYOK assistant ready.\n";
        addAndMakeVisible(transcriptBrowser);

        styleTextEditor(input);
        input.setTextToShowWhenEmpty("Ask about the circuit, place an instrument node, run ERC...", juce::Colour(0xff71808c));
        input.setMultiLine(true);
        addAndMakeVisible(input);

        sendButton.setButtonText("Send");
        sendButton.onClick = [this] {
            if (requestInFlight)
            {
                requestStop("Stopped by the user.");
                return;
            }
            const auto text = input.getText().trim();
            if (text.isEmpty())
                return;
            input.clear();
            startRequest(text, {});
        };
        addAndMakeVisible(sendButton);

        apiStatus.setJustificationType(juce::Justification::centredLeft);
        apiStatus.setColour(juce::Label::textColourId, juce::Colour(0xff93a7b0));
        addAndMakeVisible(apiStatus);

        refreshProfileList();
        startLocalApi();
        refreshTranscriptBrowser();
    }

    ~AgentPanel() override
    {
        shuttingDown.store(true);
        if (localApi != nullptr)
            localApi->stop();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff151a20));
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8);
        title.setBounds(area.removeFromTop(24));
        area.removeFromTop(6);

        auto top = area.removeFromTop(28);
        profileBox.setBounds(top.removeFromLeft(150));
        top.removeFromLeft(6);
        modelBox.setBounds(top.removeFromLeft(145));
        top.removeFromLeft(6);
        settingsButton.setBounds(top.removeFromLeft(78));
        top.removeFromLeft(6);
        cardsButton.setBounds(top.removeFromLeft(62));

        area.removeFromTop(6);
        auto actions = area.removeFromTop(28);
        ercButton.setBounds(actions.removeFromLeft(86));
        actions.removeFromLeft(6);
        exportButton.setBounds(actions.removeFromLeft(74));
        actions.removeFromLeft(6);
        apiStatus.setBounds(actions);

        area.removeFromTop(8);
        auto bottom = area.removeFromBottom(72);
        sendButton.setBounds(bottom.removeFromRight(72));
        bottom.removeFromRight(6);
        input.setBounds(bottom);

        area.removeFromBottom(8);
        transcriptBrowser.setBounds(area);
    }

    void showAiSettingsForSelected()
    {
        auto profileName = profileBox.getText();
        if (profileName.isEmpty())
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                   "BYOK Agent Settings",
                                                   "Select an AI profile first.");
            return;
        }

        juce::String apiKey;
        juce::String model = modelBox.getText();
        for (const auto& profile : aiConfig.profiles())
        {
            if (profile.name == profileName.toStdString())
            {
                apiKey = profile.apiKey;
                if (model.isEmpty())
                    model = profile.model;
                break;
            }
        }

        auto* dialog = new juce::AlertWindow(
            "BYOK Agent Settings",
            "Profile: " + profileName,
            juce::AlertWindow::NoIcon);
        dialog->addTextEditor("apiKey", apiKey, "API key:", true);
        dialog->addTextEditor("model", model, "Model:");
        dialog->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
        dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

        dialog->enterModalState(true,
            juce::ModalCallbackFunction::create([this, dialog, profileName](int result) {
                if (result != 1)
                    return;

                const auto newKey = dialog->getTextEditorContents("apiKey").trim();
                const auto newModel = dialog->getTextEditorContents("model").trim();
                if (newKey.isEmpty() || newModel.isEmpty())
                {
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                           "BYOK Agent Settings",
                                                           "API key and model are required.");
                    return;
                }

                std::string error;
                if (!aiConfig.updateProfileCredentials(profileName.toStdString(),
                                                       newKey.toStdString(),
                                                       newModel.toStdString(),
                                                       error))
                {
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                           "BYOK Agent Settings",
                                                           juce::String(error));
                    return;
                }

                refreshProfileList();
                modelBox.setText(newModel, juce::dontSendNotification);
                appendTranscript("system", "AI settings saved for " + profileName + ".");
            }),
            true);
    }

    bool submitExternalMessage(const juce::String& content, ExternalCompletion completion)
    {
        if (requestInFlight || content.trim().isEmpty())
            return false;
        startRequest(content.trim(), std::move(completion));
        return true;
    }

    bool configureExternalSession(const juce::var& options, juce::String& error)
    {
        auto selectComboText = [&error](juce::ComboBox& box, const juce::String& requested, const juce::String& label) {
            if (requested.isEmpty())
                return true;
            for (int index = 0; index < box.getNumItems(); ++index)
            {
                if (box.getItemText(index).equalsIgnoreCase(requested))
                {
                    box.setSelectedItemIndex(index, juce::sendNotificationSync);
                    return true;
                }
            }
            if (box.isTextEditable())
            {
                box.setText(requested, juce::sendNotificationSync);
                return true;
            }
            error = "Unknown " + label + ": " + requested;
            return false;
        };

        if (!selectComboText(profileBox, options.getProperty("profile", {}).toString(), "profile"))
            return false;
        if (!selectComboText(modelBox, options.getProperty("model", {}).toString(), "model"))
            return false;
        if ((bool)options.getProperty("newConversation", false))
        {
            history.clear();
            transcriptMarkdown = "BYOK assistant ready.\n";
            refreshTranscriptBrowser();
        }
        return true;
    }

    juce::var externalSessionSnapshot() const
    {
        auto* snapshot = new juce::DynamicObject();
        snapshot->setProperty("profile", profileBox.getText());
        snapshot->setProperty("model", modelBox.getText());
        juce::Array<juce::var> models;
        for (int i = 0; i < modelBox.getNumItems(); ++i)
            models.add(modelBox.getItemText(i));
        snapshot->setProperty("availableModels", models);
        snapshot->setProperty("busy", requestInFlight);
        snapshot->setProperty("discoveryFile", LocalAgentApi::getDiscoveryFile().getFullPathName());
        snapshot->setProperty("knowledgeRoot", electronics_knowledge::getKnowledgeRoot().getFullPathName());

        juce::Array<juce::var> toolNames;
        for (const auto& definition : toolDefinitions())
            toolNames.add(juce::String(definition.name));
        snapshot->setProperty("tools", toolNames);

        if (tools.inspectCircuit != nullptr)
        {
            const auto circuit = juce::JSON::parse(tools.inspectCircuit());
            auto* summary = new juce::DynamicObject();
            if (const auto* circuitObject = circuit.getDynamicObject())
            {
                if (const auto* components = circuitObject->getProperty("components").getArray())
                    summary->setProperty("componentCount", (int)components->size());
                if (const auto* wires = circuitObject->getProperty("wires").getArray())
                    summary->setProperty("wireCount", (int)wires->size());
                if (const auto* junctions = circuitObject->getProperty("junctions").getArray())
                    summary->setProperty("junctionCount", (int)junctions->size());
                if (const auto* groups = circuitObject->getProperty("groups").getArray())
                    summary->setProperty("groupCount", (int)groups->size());
            }
            snapshot->setProperty("circuitSummary", juce::var(summary));
        }
        return juce::var(snapshot);
    }

    bool requestStop(const juce::String& reason)
    {
        if (!requestInFlight)
            return false;
        stopRequested.store(true);
        appendTranscript("system", reason);
        return true;
    }

private:
    HostTools tools;
    ai_provider::AiConfig aiConfig;
    std::unique_ptr<LocalAgentApi> localApi;
    std::vector<ai_provider::ChatMessage> history;
    juce::Label title;
    juce::Label apiStatus;
    juce::ComboBox profileBox;
    juce::ComboBox modelBox;
    juce::TextButton settingsButton { "Settings" };
    juce::TextButton cardsButton { "Cards" };
    juce::TextButton ercButton { "Run ERC" };
    juce::TextButton exportButton { "Export" };
    juce::WebBrowserComponent transcriptBrowser;
    juce::String transcriptMarkdown;
    juce::TextEditor input;
    juce::TextButton sendButton { "Send" };
    bool requestInFlight = false;
    std::atomic<bool> stopRequested { false };
    std::atomic<bool> shuttingDown { false };

    static juce::File aiConfigFile()
    {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("DjehutiElectronicsLab")
            .getChildFile("ai_config.json");
    }

    static ai_provider::ToolCall toolCall(const std::string& name, const std::string& arguments)
    {
        ai_provider::ToolCall call;
        call.id = name + ".local";
        call.name = name;
        call.argumentsJson = arguments;
        return call;
    }

    static juce::String compactText(juce::String text, int maxCharacters)
    {
        text = text.trim();
        if (text.length() <= maxCharacters)
            return text;
        return text.substring(0, maxCharacters).trim() + "\n...";
    }

    void refreshProfileList()
    {
        const auto previous = profileBox.getText();
        profileBox.clear();
        int itemId = 1;
        for (const auto& profile : aiConfig.profiles())
            profileBox.addItem(juce::String(profile.name), itemId++);

        if (profileBox.getNumItems() == 0)
        {
            profileBox.setTextWhenNoChoicesAvailable("No profiles");
            return;
        }

        int selected = 0;
        for (int index = 0; index < profileBox.getNumItems(); ++index)
            if (profileBox.getItemText(index) == previous)
                selected = index;
        profileBox.setSelectedItemIndex(selected, juce::sendNotificationSync);
    }

    void refreshModelList()
    {
        const auto profileName = profileBox.getText();
        juce::String selectedModel;
        for (const auto& profile : aiConfig.profiles())
            if (profile.name == profileName.toStdString())
                selectedModel = profile.model;

        const auto current = selectedModel.isNotEmpty() ? selectedModel : juce::String("gpt-4o-mini");
        modelBox.clear();
        modelBox.addItem(current, 1);
        modelBox.setText(current, juce::dontSendNotification);

        // Ask the provider which models this key can use, off the message
        // thread; the saved model stays selected while the list loads.
        auto provider = std::shared_ptr<ai_provider::AiProvider>(aiConfig.createProvider(profileName.toStdString()));
        if (provider == nullptr)
            return;
        const auto generation = ++modelListGeneration;
        auto safeThis = juce::Component::SafePointer<AgentPanel>(this);
        juce::Thread::launch([provider, generation, safeThis] {
            const auto response = provider->listModels();
            juce::MessageManager::callAsync([response, generation, safeThis] {
                if (safeThis == nullptr || generation != safeThis->modelListGeneration)
                    return;
                safeThis->applyModelList(response);
            });
        });
    }

    // Chat-capable model ids; embeddings, speech, image, moderation and
    // legacy completion models are left out of the picker.
    static bool isChatModelId(const juce::String& id)
    {
        static const char* excluded[] = { "embedding", "tts", "whisper", "dall-e", "moderation", "transcribe",
                                          "audio", "realtime", "image", "search", "davinci", "babbage", "instruct" };
        for (const auto* word : excluded)
            if (id.containsIgnoreCase(word))
                return false;
        return id.startsWith("gpt-") || id.startsWith("chatgpt-") || id.startsWith("o1") || id.startsWith("o3")
            || id.startsWith("o4") || id.startsWith("o5");
    }

    void applyModelList(const ai_provider::ModelListResponse& response)
    {
        const auto current = modelBox.getText();
        if (!response.ok)
        {
            modelBox.setTooltip("Model list unavailable: " + juce::String(response.errorMessage));
            return;
        }

        juce::StringArray models;
        for (const auto& model : response.models)
            if (isChatModelId(model))
                models.add(model);
        if (models.isEmpty())
            for (const auto& model : response.models)
                models.add(model);
        models.addIfNotAlreadyThere(current);
        models.sort(true);

        modelBox.clear(juce::dontSendNotification);
        int itemId = 1;
        for (const auto& model : models)
            modelBox.addItem(model, itemId++);
        modelBox.setText(current, juce::dontSendNotification);
        modelBox.setTooltip("Model used by the selected BYOK profile (" + juce::String(models.size()) + " available)");
    }

    int modelListGeneration = 0;

    void startLocalApi()
    {
        localApi = std::make_unique<LocalAgentApi>();
        localApi->onMessage = [this](const juce::String& content, LocalAgentApi::Completion completion) {
            if (requestInFlight || content.trim().isEmpty())
            {
                completion(false, "The electronics assistant is busy or the message was empty.",
                           externalSessionSnapshot());
                return;
            }
            submitExternalMessage(content, std::move(completion));
        };
        localApi->onSession = [this](const juce::var& options, LocalAgentApi::Completion completion) {
            juce::String error;
            if (!configureExternalSession(options, error))
            {
                completion(false, error, externalSessionSnapshot());
                return;
            }
            completion(true, "Session configured.", externalSessionSnapshot());
        };
        localApi->onCancel = [this] { requestStop("Stop requested through the local agent API."); };
        localApi->onToolCall = [this](const juce::String& name, const juce::var& arguments, LocalAgentApi::Completion completion) {
            ai_provider::ToolCall call;
            call.name = name.toStdString();
            call.argumentsJson = juce::JSON::toString(arguments, true).toStdString();
            if (tools.longTool != nullptr
                && tools.longTool(name.replaceCharacter('.', '_'), arguments, [completion](juce::String result) {
                       const auto parsed = juce::JSON::parse(result);
                       completion(!parsed.isObject() || (bool)parsed.getProperty("ok", true), result, {});
                   }))
                return;
            const auto result = executeToolNow(call);
            const auto parsed = juce::JSON::parse(result);
            completion(!parsed.isObject() || (bool)parsed.getProperty("ok", true), result, {});
        };

        if (localApi->start())
            apiStatus.setText("API ready: " + LocalAgentApi::getDiscoveryFile().getFullPathName(),
                              juce::dontSendNotification);
        else
            apiStatus.setText("API unavailable", juce::dontSendNotification);
    }

    void appendTranscript(const juce::String& speaker, const juce::String& text)
    {
        transcriptMarkdown << "\n\n### [" << speaker << "]\n\n";
        if ((speaker == "tool" || speaker == "system") && text.trim().startsWith("{"))
            transcriptMarkdown << "```json\n" << text.trim() << "\n```\n";
        else
            transcriptMarkdown << text.trim() << "\n";
        refreshTranscriptBrowser();
    }

    juce::File transcriptHtmlFile() const
    {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("DjehutiElectronicsLab")
            .getChildFile("agent-transcript.html");
    }

    void refreshTranscriptBrowser()
    {
        const auto file = transcriptHtmlFile();
        if (!file.getParentDirectory().createDirectory().wasOk())
            return;

        const auto html = markdownToHtmlDocument("BYOK Electronics Agent", transcriptMarkdown);
        if (file.replaceWithText(html))
            transcriptBrowser.goToURL(juce::URL(file).toString(true));
    }

    juce::String systemPrompt() const
    {
        return "You are the embedded BYOK assistant for Djehuti Electronics Lab. "
               "The circuit JSON model is authoritative. Use tools when you need current schematic facts, "
               "electrical checks, exported artifacts, or diagram edits. Create instruments as schematic nodes "
               "for scopes and meters, wire pins by labels such as R1.1 or SCOPE2.CH1, and do not open floating "
               "instrument panels unless the user explicitly asks you to open a panel. In this system, users open "
               "a scope or DMM panel by double-clicking the placed instrument node. "
               "Use filter_design_high_pass when asked to synthesize a matched RLC high-pass filter and produce AC response artifacts. "
               "Use amplifier_design_push_pull when asked for a push-pull, class B, class AB, or complementary emitter-follower audio output stage. "
               "Use schematic_auto_layout after creating or editing a diagram so the result is readable and spaced. "
               "After placing or connecting a generated circuit, use circuit_inspect and circuit_run_erc before claiming the circuit exists or is ready. "
               "If a schematic_connect call fails, correct the pin labels using the available-labels error; do not continue as if it succeeded. "
               "When reporting component counts, wire counts, ERC counts, or artifact paths, copy them from tool results or circuit_inspect; never infer or invent them. "
               "Use cookbook_lookup when requirements imply topology selection, design equations, validation recipes, troubleshooting, "
               "or when you need to compare established circuit candidates before building. "
               "Use cookbook_coverage to inspect cookbook domain coverage and identify missing recipe areas. "
               "Use cookbook_validate to check cookbook schema quality and taxonomy alignment after cookbook edits. "
               "Use cookbook_acceptance_goals to inspect representative engineering goals for agent validation. "
               "Use cookbook_acceptance_summary to inspect existing acceptance run reports before rerunning or claiming progress. "
               "Use cookbook_acceptance_start before executing an acceptance goal so evidence has a durable report scaffold. "
               "Use cookbook_acceptance_record to append retrieved cards, tool calls, artifacts, criteria, notes, and capability gaps to that report. "
               "Use capability_gap_record when a missing reusable app capability blocks a cookbook step or validation claim. "
               "The BYOK Agent window renders markdown and KaTeX math. Use $...$ for inline equations and $$ on separate lines "
               "for display equations when discussing formulas, transfer functions, impedance, gain, cutoff frequency, filters, "
               "component sizing, or any math-heavy design reasoning. "
               "Use agent_write_markdown when producing a durable report, derivation, or equation-rich explanation that should be saved and rendered in this agent window. "
               "Use research_web_search only when current external information, standards references, datasheets, or source links are needed; summarize sources conservatively. "
               "Use filesystem LiteSemRAG cards as retrieved guidance; do not assume Suite VFS storage. "
               "Be concise, report tool results plainly, and do not claim a circuit is complete or ready for solver-backed "
               "analysis until circuit_run_erc has passed or you have explicitly reported the remaining errors/warnings.";
    }

    std::vector<ai_provider::ToolDefinition> toolDefinitions() const
    {
        std::vector<ai_provider::ToolDefinition> definitions {
            {
                "cookbook_lookup",
                "Search structured electronics cookbook cards for topology candidates, design recipes, analysis steps, validation criteria, and known capability gaps.",
                R"({"type":"object","properties":{"query":{"type":"string","description":"Engineering requirement or cookbook topic to retrieve."},"maxCards":{"type":"integer","description":"Maximum number of cookbook/knowledge cards to return."}},"required":["query"],"additionalProperties":false})"
            },
            {
                "cookbook_coverage",
                "Report structured cookbook coverage against the file-backed taxonomy, including missing categories.",
                R"({"type":"object","properties":{},"additionalProperties":false})"
            },
            {
                "cookbook_validate",
                "Validate cookbook entries for required structured fields, taxonomy alignment, duplicate ids, and malformed raw entries.",
                R"({"type":"object","properties":{},"additionalProperties":false})"
            },
            {
                "cookbook_acceptance_goals",
                "List representative agent acceptance goals, optionally filtered by domain or goal id.",
                R"({"type":"object","properties":{"domainOrId":{"type":"string","description":"Optional acceptance domain such as passive_filter, or exact goal id."}},"additionalProperties":false})"
            },
            {
                "cookbook_acceptance_summary",
                "Summarize generated acceptance run reports, optionally filtered by acceptance domain or goal id.",
                R"({"type":"object","properties":{"domainOrId":{"type":"string","description":"Optional acceptance domain such as passive_filter, or exact goal id."}},"additionalProperties":false})"
            },
            {
                "cookbook_acceptance_start",
                "Create a structured acceptance report scaffold for a representative goal id.",
                R"({"type":"object","properties":{"goalId":{"type":"string","description":"Exact acceptance goal id to start."}},"required":["goalId"],"additionalProperties":false})"
            },
            {
                "cookbook_acceptance_record",
                "Append structured evidence to an acceptance report created by cookbook_acceptance_start.",
                R"({"type":"object","properties":{"reportPath":{"type":"string","description":"Path returned as jsonReport by cookbook_acceptance_start."},"evidenceType":{"type":"string","description":"retrieved_card, tool_call, artifact, capability_gap, criterion, or note."},"label":{"type":"string","description":"Short evidence label."},"detail":{"type":"string","description":"Evidence details."},"pathOrValue":{"type":"string","description":"Artifact path, tool result path, card id, or measured value."},"status":{"type":"string","description":"observed, passed, failed, gap, unverified, or not_started."}},"required":["reportPath","evidenceType","label"],"additionalProperties":false})"
            },
            {
                "capability_gap_record",
                "Append a reusable missing-capability record to the project gap registry.",
                R"({"type":"object","properties":{"category":{"type":"string","description":"Gap category such as solver, component_model, analysis, plotting, instrument, ui, or agent_workflow."},"description":{"type":"string","description":"What blocked the engineering step."},"neededCapability":{"type":"string","description":"Reusable tool or app capability needed to close the gap."},"evidence":{"type":"string","description":"Tool result, report path, or observation proving the gap."},"source":{"type":"string","description":"Acceptance goal id, report path, cookbook card id, or user goal that exposed the gap."},"status":{"type":"string","description":"open, planned, in_progress, closed, or deferred."}},"required":["description","neededCapability"],"additionalProperties":false})"
            },
            {
                "circuit_inspect",
                "Read the current authoritative circuit JSON without modifying it.",
                R"({"type":"object","properties":{},"additionalProperties":false})"
            },
            {
                "circuit_run_erc",
                "Run Electrical Rule Check on the current schematic and return machine-readable results.",
                R"({"type":"object","properties":{},"additionalProperties":false})"
            },
            {
                "simulation_export_artifacts",
                "Export circuit JSON, Xyce netlist, lab instruments JSON, and assistant tool manifest.",
                R"({"type":"object","properties":{},"additionalProperties":false})"
            },
            {
                "frust_realtime_preview_export",
                "Export the current schematic as a Djehuti audio model package for the generic Model Player VST3 runtime.",
                R"({"type":"object","properties":{},"additionalProperties":false})"
            },
            {
                "agent_write_markdown",
                "Write a markdown report into the agent workspace and render it in the BYOK Agent window with KaTeX equation support.",
                R"({"type":"object","properties":{"title":{"type":"string","description":"Short report title used for filenames and HTML title."},"markdown":{"type":"string","description":"Markdown content. Use $...$ for inline math and $$ on separate lines for display equations."}},"required":["title","markdown"],"additionalProperties":false})"
            },
            {
                "research_web_search",
                "Search the web for current external references. Use sparingly for standards, datasheets, current facts, or source links; prefer app/cookbook tools for local circuit facts.",
                R"({"type":"object","properties":{"query":{"type":"string","description":"Focused search query."},"maxResults":{"type":"integer","description":"Maximum compact results to return, 1 to 8."}},"required":["query"],"additionalProperties":false})"
            },
            {
                "filter_design_high_pass",
                "Design and draw a matched RLC 2nd order high-pass filter, then run the internal AC sweep and export response artifacts.",
                R"({"type":"object","properties":{"cutoffHz":{"type":"number","description":"Target -3 dB cutoff frequency in Hz."},"impedanceOhms":{"type":"number","description":"Matched source/load impedance in ohms."}},"required":["cutoffHz","impedanceOhms"],"additionalProperties":false})"
            },
            {
                "amplifier_design_push_pull",
                "Design and draw a diode-biased complementary push-pull audio output stage with input source, +/- rails, 8 ohm load, grounds, and a 2-channel oscilloscope.",
                R"({"type":"object","properties":{},"additionalProperties":false})"
            },
            {
                "schematic_auto_layout",
                "Automatically arrange the current schematic into a readable left-to-right diagram using standard spacing conventions.",
                R"({"type":"object","properties":{},"additionalProperties":false})"
            },
            {
                "schematic_auto_layout_selection",
                "Automatically arrange only the currently selected schematic components.",
                R"({"type":"object","properties":{},"additionalProperties":false})"
            },
            {
                "schematic_place_symbol",
                "Place a schematic symbol or instrument node at a grid coordinate. Use deliberate layout spacing: keep symbols at least 144 px apart horizontally or 96 px vertically, arrange signal flow left-to-right, put sources on the left, outputs/load on the right, grounds below, instruments to the far right, and never reuse the same x/y for multiple parts.",
                R"({"type":"object","properties":{"symbolId":{"type":"string","description":"Supported symbol id: resistor, potentiometer, capacitor, capacitor_polarized, variable_capacitor, inductor, coupled_inductor, transformer, diode, zener_diode, led, schottky_diode, power_bus, ground_bus, power_port, net_label, battery, voltage_source, ac_voltage_source, current_source, ac_current_source, vcvs, vccs, ccvs, cccs, signal_source, ground, opamp_generic, opamp_741, comparator_generic, comparator_lm311, npn, pnp, nmos, pmos, njfet, pjfet, switch_spst, switch_spdt, relay_spst, fuse, connector_2, connector_3, test_point, logic_not, logic_and, logic_or, logic_nand, logic_nor, logic_xor, oscilloscope_2ch, digital_multimeter, bode_analyzer, or annotation_text. Use annotation_text for free-form schematic notes. Use ground and power_port symbols at each pin that needs ground or a supply instead of long wires: power_port takes busName like +12V (drawn pointing up) or -12V (place with a leading minus; drawn pointing down); ports with the same busName are the same net. net_label takes busName as its label; labels with the same name are one net. After placing and connecting, call schematic_auto_layout once for a standards-conforming drawing. Unsupported symbols are rejected, not substituted."},"x":{"type":"number","description":"Grid x coordinate. Leave at least 144 px horizontal space from other symbols."},"y":{"type":"number","description":"Grid y coordinate. Leave at least 96 px vertical space from other symbols."},"value":{"type":"string"},"frequency":{"type":"string"},"busName":{"type":"string"}},"required":["symbolId","x","y"],"additionalProperties":false})"
            },
            {
                "schematic_connect",
                "Connect two schematic pins or junctions by label, such as R1.1 to GND2.0.",
                R"({"type":"object","properties":{"a":{"type":"string","description":"First node label such as R1.1, V2.+, GND3.0, SCOPE4.CH1, or N1."},"b":{"type":"string","description":"Second node label."}},"required":["a","b"],"additionalProperties":false})"
            },
            {
                "instrument_open_panel",
                "Open the floating instrument panel for a placed and wired instrument node only when the user explicitly asks to open that panel. Do not call this as part of normal circuit creation; users open panels by double-clicking instrument nodes.",
                R"({"type":"object","properties":{"refdes":{"type":"string","description":"Reference designator of an existing, wired instrument node, such as SCOPE2 or DMM3. Use only after an explicit user request to open the panel."}},"required":["refdes"],"additionalProperties":false})"
            }
        };
        for (const auto& spec : schematicToolSpecs)
            definitions.push_back({ spec.name, spec.description, spec.schema });
        for (const auto& spec : analyticsToolSpecs())
            definitions.push_back({ spec.name.toStdString(), spec.description.toStdString(), spec.schema.toStdString() });
        return definitions;
    }

    juce::String executeToolNow(const ai_provider::ToolCall& call)
    {
        const auto originalName = juce::String(call.name);
        const auto name = originalName.replaceCharacter('.', '_');
        const auto parsed = juce::JSON::parse(juce::String(call.argumentsJson));

        if (name == "circuit_inspect")
        {
            const auto circuit = tools.inspectCircuit != nullptr ? tools.inspectCircuit() : "{}";
            return "{ \"ok\": true, \"tool\": \"circuit_inspect\", \"displayTool\": \"circuit.inspect\", \"circuit\": "
                + (circuit.trim().isEmpty() ? juce::String("{}") : circuit.trim()) + " }";
        }

        if (name == "cookbook_lookup")
        {
            if (!parsed.isObject())
                return "{ \"ok\": false, \"error\": \"cookbook_lookup arguments must be a JSON object.\" }";

            const auto query = parsed.getProperty("query", {}).toString().trim();
            const auto maxCards = (int)parsed.getProperty("maxCards", 6);
            if (query.isEmpty())
                return "{ \"ok\": false, \"error\": \"query is required.\" }";
            return tools.cookbookLookup != nullptr
                ? tools.cookbookLookup(query, maxCards)
                : "{ \"ok\": false, \"error\": \"Cookbook lookup is unavailable.\" }";
        }

        if (name == "cookbook_coverage")
            return tools.cookbookCoverage != nullptr
                ? tools.cookbookCoverage()
                : "{ \"ok\": false, \"error\": \"Cookbook coverage is unavailable.\" }";

        if (name == "cookbook_validate")
            return tools.cookbookValidate != nullptr
                ? tools.cookbookValidate()
                : "{ \"ok\": false, \"error\": \"Cookbook validation is unavailable.\" }";

        if (name == "cookbook_acceptance_goals")
        {
            const auto domainOrId = parsed.isObject()
                ? parsed.getProperty("domainOrId", {}).toString().trim()
                : juce::String();
            return tools.cookbookAcceptanceGoals != nullptr
                ? tools.cookbookAcceptanceGoals(domainOrId)
                : "{ \"ok\": false, \"error\": \"Cookbook acceptance goals are unavailable.\" }";
        }

        if (name == "cookbook_acceptance_summary")
        {
            const auto domainOrId = parsed.isObject()
                ? parsed.getProperty("domainOrId", {}).toString().trim()
                : juce::String();
            return tools.cookbookAcceptanceSummary != nullptr
                ? tools.cookbookAcceptanceSummary(domainOrId)
                : "{ \"ok\": false, \"error\": \"Cookbook acceptance summary is unavailable.\" }";
        }

        if (name == "cookbook_acceptance_start")
        {
            if (!parsed.isObject())
                return "{ \"ok\": false, \"error\": \"cookbook_acceptance_start arguments must be a JSON object.\" }";

            const auto goalId = parsed.getProperty("goalId", {}).toString().trim();
            if (goalId.isEmpty())
                return "{ \"ok\": false, \"error\": \"goalId is required.\" }";
            return tools.cookbookAcceptanceStart != nullptr
                ? tools.cookbookAcceptanceStart(goalId)
                : "{ \"ok\": false, \"error\": \"Cookbook acceptance start is unavailable.\" }";
        }

        if (name == "cookbook_acceptance_record")
        {
            if (!parsed.isObject())
                return "{ \"ok\": false, \"error\": \"cookbook_acceptance_record arguments must be a JSON object.\" }";

            const auto reportPath = parsed.getProperty("reportPath", {}).toString().trim();
            const auto evidenceType = parsed.getProperty("evidenceType", {}).toString().trim();
            const auto label = parsed.getProperty("label", {}).toString().trim();
            const auto detail = parsed.getProperty("detail", {}).toString().trim();
            const auto pathOrValue = parsed.getProperty("pathOrValue", {}).toString().trim();
            const auto status = parsed.getProperty("status", {}).toString().trim();
            if (reportPath.isEmpty() || evidenceType.isEmpty() || label.isEmpty())
                return "{ \"ok\": false, \"error\": \"reportPath, evidenceType, and label are required.\" }";
            return tools.cookbookAcceptanceRecord != nullptr
                ? tools.cookbookAcceptanceRecord(reportPath, evidenceType, label, detail, pathOrValue, status)
                : "{ \"ok\": false, \"error\": \"Cookbook acceptance record is unavailable.\" }";
        }

        if (name == "capability_gap_record")
        {
            if (!parsed.isObject())
                return "{ \"ok\": false, \"error\": \"capability_gap_record arguments must be a JSON object.\" }";

            const auto category = parsed.getProperty("category", {}).toString().trim();
            const auto description = parsed.getProperty("description", {}).toString().trim();
            const auto neededCapability = parsed.getProperty("neededCapability", {}).toString().trim();
            const auto evidence = parsed.getProperty("evidence", {}).toString().trim();
            const auto source = parsed.getProperty("source", {}).toString().trim();
            const auto status = parsed.getProperty("status", {}).toString().trim();
            if (description.isEmpty() || neededCapability.isEmpty())
                return "{ \"ok\": false, \"error\": \"description and neededCapability are required.\" }";
            return tools.capabilityGapRecord != nullptr
                ? tools.capabilityGapRecord(category, description, neededCapability, evidence, source, status)
                : "{ \"ok\": false, \"error\": \"Capability gap recording is unavailable.\" }";
        }

        if (name == "circuit_run_erc")
            return tools.runErc != nullptr ? tools.runErc() : "{ \"ok\": false, \"error\": \"ERC tool unavailable.\" }";

        if (name == "simulation_export_artifacts")
            return tools.exportArtifacts != nullptr ? tools.exportArtifacts() : "{ \"ok\": false, \"error\": \"Export tool unavailable.\" }";

        if (name == "frust_realtime_preview_export")
            return tools.exportRealtimePreview != nullptr ? tools.exportRealtimePreview() : "{ \"ok\": false, \"error\": \"Frust realtime preview export is unavailable.\" }";

        if (name == "agent_write_markdown")
        {
            if (!parsed.isObject())
                return "{ \"ok\": false, \"error\": \"agent_write_markdown arguments must be a JSON object.\" }";
            const auto title = parsed.getProperty("title", {}).toString().trim();
            const auto markdown = parsed.getProperty("markdown", {}).toString();
            if (title.isEmpty() || markdown.trim().isEmpty())
                return "{ \"ok\": false, \"error\": \"title and markdown are required.\" }";
            if (tools.writeMarkdown == nullptr)
                return "{ \"ok\": false, \"error\": \"Markdown writing is unavailable.\" }";

            const auto result = tools.writeMarkdown(title, markdown);
            const auto reportResult = juce::JSON::parse(result);
            if (reportResult.isObject() && (bool)reportResult.getProperty("ok", false))
                appendTranscript("assistant report", markdown);
            return result;
        }

        if (name == "research_web_search")
        {
            if (!parsed.isObject())
                return "{ \"ok\": false, \"error\": \"research_web_search arguments must be a JSON object.\" }";
            const auto query = parsed.getProperty("query", {}).toString().trim();
            const auto maxResults = (int)parsed.getProperty("maxResults", 5);
            if (query.isEmpty())
                return "{ \"ok\": false, \"error\": \"query is required.\" }";
            return tools.webSearch != nullptr
                ? tools.webSearch(query, maxResults)
                : "{ \"ok\": false, \"error\": \"Web search is unavailable.\" }";
        }

        if (name == "filter_design_high_pass")
        {
            if (!parsed.isObject())
                return "{ \"ok\": false, \"error\": \"filter_design_high_pass arguments must be a JSON object.\" }";

            const auto cutoffHz = (double)parsed.getProperty("cutoffHz", 10.0);
            const auto impedanceOhms = (double)parsed.getProperty("impedanceOhms", 8.0);
            return tools.designHighPass != nullptr
                ? tools.designHighPass(cutoffHz, impedanceOhms)
                : "{ \"ok\": false, \"error\": \"High-pass filter design tool unavailable.\" }";
        }

        if (name == "amplifier_design_push_pull")
            return tools.designPushPull != nullptr
                ? tools.designPushPull()
                : "{ \"ok\": false, \"error\": \"Push-pull amplifier design tool unavailable.\" }";

        if (name == "schematic_auto_layout")
            return tools.autoLayout != nullptr
                ? tools.autoLayout()
                : "{ \"ok\": false, \"error\": \"Schematic auto-layout is unavailable.\" }";

        if (name == "schematic_auto_layout_selection")
            return tools.autoLayoutSelection != nullptr
                ? tools.autoLayoutSelection()
                : "{ \"ok\": false, \"error\": \"Schematic selection auto-layout is unavailable.\" }";

        if (name == "schematic_place_symbol")
        {
            if (!parsed.isObject())
                return "{ \"ok\": false, \"error\": \"schematic_place_symbol arguments must be a JSON object.\" }";

            const auto symbolId = parsed.getProperty("symbolId", {}).toString().trim();
            const auto x = (float)(double)parsed.getProperty("x", 120.0);
            const auto y = (float)(double)parsed.getProperty("y", 120.0);
            const auto value = parsed.getProperty("value", {}).toString();
            const auto frequency = parsed.getProperty("frequency", {}).toString();
            const auto busName = parsed.getProperty("busName", {}).toString();
            if (symbolId.isEmpty())
                return "{ \"ok\": false, \"error\": \"symbolId is required.\" }";
            return tools.placeSymbol != nullptr
                ? tools.placeSymbol(symbolId, x, y, value, frequency, busName)
                : "{ \"ok\": false, \"error\": \"Schematic placement tool unavailable.\" }";
        }

        if (name == "schematic_connect")
        {
            if (!parsed.isObject())
                return "{ \"ok\": false, \"error\": \"schematic_connect arguments must be a JSON object.\" }";

            const auto first = parsed.getProperty("a", {}).toString().trim();
            const auto second = parsed.getProperty("b", {}).toString().trim();
            if (first.isEmpty() || second.isEmpty())
                return "{ \"ok\": false, \"error\": \"Both a and b node labels are required.\" }";
            return tools.connectNodes != nullptr
                ? tools.connectNodes(first, second)
                : "{ \"ok\": false, \"error\": \"Schematic wiring tool unavailable.\" }";
        }

        if (name == "instrument_open_panel")
        {
            if (!parsed.isObject())
                return "{ \"ok\": false, \"error\": \"instrument_open_panel arguments must be a JSON object.\" }";

            const auto refdes = parsed.getProperty("refdes", {}).toString().trim();
            if (refdes.isEmpty())
                return "{ \"ok\": false, \"error\": \"refdes is required.\" }";
            return tools.openInstrument != nullptr
                ? tools.openInstrument(refdes)
                : "{ \"ok\": false, \"error\": \"Instrument opening tool unavailable.\" }";
        }

        if (isSchematicTool(name))
        {
            if (!parsed.isObject())
                return "{ \"ok\": false, \"error\": \"" + name + " arguments must be a JSON object.\" }";
            return tools.schematicTool != nullptr
                ? tools.schematicTool(name, parsed)
                : "{ \"ok\": false, \"error\": \"Schematic tools are unavailable.\" }";
        }

        return "{ \"ok\": false, \"error\": \"Unknown tool: " + originalName + "\" }";
    }

    juce::String executeToolFromWorker(const ai_provider::ToolCall& call)
    {
        if (juce::MessageManager::getInstance()->isThisTheMessageThread())
            return executeToolNow(call);

        struct ToolWait
        {
            juce::WaitableEvent done;
            juce::String result;
        };

        auto wait = std::make_shared<ToolWait>();
        auto safeThis = juce::Component::SafePointer<AgentPanel>(this);
        juce::MessageManager::callAsync([safeThis, wait, call] {
            if (safeThis == nullptr)
                wait->result = "{ \"ok\": false, \"error\": \"Assistant panel closed.\" }";
            else
            {
                const auto name = juce::String(call.name).replaceCharacter('.', '_');
                if (safeThis->tools.longTool != nullptr
                    && safeThis->tools.longTool(name, juce::JSON::parse(juce::String(call.argumentsJson)), [wait](juce::String result) {
                           wait->result = result;
                           wait->done.signal();
                       }))
                    return;
                wait->result = safeThis->executeToolNow(call);
            }
            wait->done.signal();
        });

        while (!wait->done.wait(50))
        {
            if (stopRequested.load() || shuttingDown.load())
                return "{ \"ok\": false, \"error\": \"Tool execution stopped.\" }";
        }
        return wait->result;
    }

    void showRetrievedCardsForDraft()
    {
        const auto query = input.getText().trim().isNotEmpty()
            ? input.getText().trim()
            : juce::String("erc instruments circuit model");
        const auto retrieved = electronics_knowledge::retrieve(query);
        appendTranscript("cards", retrieved.context.isNotEmpty()
            ? retrieved.context
            : "No matching cards found in " + electronics_knowledge::getCardsDirectory().getFullPathName());
    }

    void startRequest(const juce::String& userText, ExternalCompletion completion)
    {
        const auto profileName = profileBox.getText();
        const auto modelName = modelBox.getText().trim();
        if (profileName.isEmpty())
        {
            appendTranscript("system", "No AI profile selected. Open Settings and add your API key/model.");
            if (completion)
                completion(false, "No AI profile selected.", externalSessionSnapshot());
            return;
        }

        if (modelName.isNotEmpty())
        {
            for (const auto& profile : aiConfig.profiles())
            {
                if (profile.name != profileName.toStdString() || profile.model == modelName.toStdString())
                    continue;
                std::string error;
                aiConfig.updateProfileCredentials(profile.name, profile.apiKey, modelName.toStdString(), error);
                break;
            }
        }

        auto provider = aiConfig.createProvider(profileName.toStdString());
        if (provider == nullptr)
        {
            appendTranscript("system", "Could not create provider for " + profileName + ".");
            if (completion)
                completion(false, "Could not create provider.", externalSessionSnapshot());
            return;
        }

        appendTranscript("user", userText);
        const auto retrieved = electronics_knowledge::retrieve(userText);
        const auto circuit = tools.inspectCircuit != nullptr ? tools.inspectCircuit() : "{}";

        std::vector<ai_provider::ChatMessage> messages;
        ai_provider::ChatMessage system;
        system.role = "system";
        system.content = systemPrompt().toStdString();
        messages.push_back(system);

        for (const auto& item : history)
            messages.push_back(item);

        juce::String content = userText;
        if (retrieved.context.isNotEmpty())
            content << "\n\n---\n" << retrieved.context;
        content << "\n\n---\nCurrent circuit model snapshot:\n"
                << compactText(circuit, 12000);

        ai_provider::ChatMessage user;
        user.role = "user";
        user.content = content.toStdString();
        messages.push_back(user);

        requestInFlight = true;
        stopRequested.store(false);
        sendButton.setButtonText("Stop");
        appendTranscript("system", retrieved.cards.empty()
            ? "No card context matched this request."
            : "Attached " + juce::String((int)retrieved.cards.size()) + " LiteSemRAG card(s).");

        auto toolDefs = toolDefinitions();
        auto providerPtr = provider.release();
        auto safeThis = juce::Component::SafePointer<AgentPanel>(this);

        std::thread([safeThis, providerPtr, messages = std::move(messages), toolDefs = std::move(toolDefs),
                     completion = std::move(completion)]() mutable {
            std::unique_ptr<ai_provider::AiProvider> owned(providerPtr);
            ai_provider::ChatResponse response;
            bool ok = false;
            juce::String finalText;

            for (int round = 0; round < 12; ++round)
            {
                if (safeThis == nullptr || safeThis->stopRequested.load())
                {
                    response = { false, {}, "Stopped by the user." };
                    break;
                }

                response = owned->sendChat(messages, toolDefs, ai_provider::ToolChoice::autoSelect);
                if (!response.ok)
                    break;

                ai_provider::ChatMessage assistant;
                assistant.role = "assistant";
                assistant.content = response.content;
                assistant.toolCalls = response.toolCalls;
                assistant.providerItemsJson = response.providerItemsJson;
                messages.push_back(assistant);

                if (response.toolCalls.empty())
                {
                    ok = true;
                    finalText = juce::String(response.content);
                    break;
                }

                for (const auto& call : response.toolCalls)
                {
                    if (safeThis == nullptr)
                        break;

                    juce::MessageManager::callAsync([safeThis, name = juce::String(call.name)] {
                        if (safeThis != nullptr)
                            safeThis->appendTranscript("tool", "Running " + name + "...");
                    });

                    const auto result = safeThis->executeToolFromWorker(call);
                    ai_provider::ChatMessage toolMessage;
                    toolMessage.role = "tool";
                    toolMessage.content = result.toStdString();
                    toolMessage.toolCallId = call.id;
                    messages.push_back(toolMessage);
                }
            }

            if (response.ok && !ok && finalText.isEmpty())
            {
                response.ok = false;
                response.errorMessage = "The assistant used too many tool rounds without producing a final answer.";
            }

            juce::MessageManager::callAsync([safeThis, messages = std::move(messages), response,
                                             ok, finalText, completion = std::move(completion)]() mutable {
                if (safeThis == nullptr)
                    return;

                safeThis->history.clear();
                for (size_t index = 1; index < messages.size(); ++index)
                    safeThis->history.push_back(messages[index]);

                const auto visible = ok ? finalText
                                        : "Error: " + juce::String(response.errorMessage);
                safeThis->appendTranscript(ok ? "assistant" : "system", visible);
                safeThis->requestInFlight = false;
                safeThis->sendButton.setButtonText("Send");
                if (completion)
                    completion(ok, visible, safeThis->externalSessionSnapshot());
            });
        }).detach();
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AgentPanel)
};

// (The Analytics window lives in AnalyticsPanel.cpp.)


class SpecIngestionPanel final : public NotesPanel
{
public:
    SpecIngestionPanel()
        : NotesPanel("Component Spec Ingestion",
                     "Workflow:\n"
                     "1. Search local component DB.\n"
                     "2. Query approved providers/manufacturer sources.\n"
                     "3. Fetch datasheet.\n"
                     "4. Extract pins/spec claims/packages/models.\n"
                     "5. Store provenance and confidence.\n"
                     "6. Ask for approval when ambiguous.")
    {
    }
};

class PartsSourcingPanel final : public NotesPanel
{
public:
    PartsSourcingPanel()
        : NotesPanel("Parts Sourcing",
                     "Find buyable parts without confusing marketplace listings with verified specifications.\n\n"
                     "Research targets:\n"
                     "- distributor listings for exact MPNs\n"
                     "- hobby suppliers and breadboard-friendly packages\n"
                     "- Amazon/eBay/AliExpress style consumer listings\n"
                     "- assortment kits and substitutes\n"
                     "- local user inventory\n\n"
                     "Every sourcing result should carry match confidence, source URL, timestamp, package notes, and warnings.")
    {
    }
};

} // namespace

ElectronicsWorkbench::ElectronicsWorkbench()
{
    spice_library::initialize();
    audioPipeline = std::make_unique<AudioPipeline>();

    menuBar = std::make_unique<juce::MenuBarComponent>(this);
    addAndMakeVisible(menuBar.get());

    titleLabel.setText("Djehuti Electronics Lab", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(17.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xffdce9ee));
    addAndMakeVisible(titleLabel);

    
    
    
    
    

    for (auto* b : { &newButton, &openDiagramButton, &ercButton, &transientButton, &compileButton,
                     &zoomOutButton, &zoomResetButton, &zoomInButton })
    {
        b->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff253341));
        b->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffdce9ee));
        addAndMakeVisible(*b);
    }
    stampModeButton.setToggleState(false, juce::dontSendNotification);
    stampModeButton.setColour(juce::ToggleButton::textColourId, juce::Colour(0xffdce9ee));
    stampModeButton.onClick = [this] {
        appendLog(stampModeButton.getToggleState()
            ? "Stamp mode enabled: click the schematic to place the selected component repeatedly."
            : "Stamp mode disabled.");
    };
    addAndMakeVisible(stampModeButton);
    snapModeButton.setToggleState(true, juce::dontSendNotification);
    snapModeButton.setColour(juce::ToggleButton::textColourId, juce::Colour(0xffdce9ee));
    addAndMakeVisible(snapModeButton);

    newButton.onClick = [this] { showNewDiagramDialog(); };
    newButton.setTooltip("New diagram in the open project (creates a project first if none is open)");
    openDiagramButton.onClick = [this] { showOpenDiagramMenu(); };
    openDiagramButton.setTooltip("Open a saved diagram from the current project");
    ercButton.onClick = [this] { runElectricalRuleCheck(); };
    transientButton.onClick = [this] { showAnalytics(); };
    compileButton.onClick = [this] { exportFrustRealtimePreview(); };
    snapModeButton.onClick = [this] {
        if (setSnapEnabled != nullptr)
            setSnapEnabled(snapModeButton.getToggleState());
        appendLog(snapModeButton.getToggleState() ? "Schematic snap enabled." : "Schematic snap disabled.");
    };
    zoomOutButton.onClick = [this] { adjustSchematicZoom(1.0f / 1.2f); };
    zoomResetButton.onClick = [this] { applySchematicZoom(1.0f); };
    zoomInButton.onClick = [this] { adjustSchematicZoom(1.2f); };

    dockManager = std::make_unique<CreationDock::DockManager>(*this);
    addAndMakeVisible(*dockManager);

    dockManager->registerPanel("library", "Component Library",
                               std::make_unique<ComponentLibraryPanel>(
                                   [this](juce::String id) {
                                       selectedSymbolId = id;
                                       appendLog("Selected symbol: " + id + ". Click the schematic to place it, or drag it from the library.");
                                   }),
                               CreationDock::DockTargetZone::Left);
    auto schematic = std::make_unique<SchematicCanvasPanel>(
        [this] { return selectedSymbolId; },
        [this] { return stampModeButton.getToggleState(); },
        [this](juce::String message) { appendLog(message); });
    auto* schematicPanel = schematic.get();
    schematicView = schematicPanel;
    setSnapEnabled = [schematicPanel](bool enabled) {
        schematicPanel->setSnapEnabled(enabled);
    };
    setSchematicZoom = [schematicPanel](float zoom) {
        schematicPanel->setCanvasZoom(zoom);
    };
    getSchematicZoom = [schematicPanel] {
        return schematicPanel->getCanvasZoom();
    };
    auto properties = std::make_unique<PropertiesPanel>(schematicPanel);
    auto* propertiesPanel = properties.get();
    schematicPanel->setSelectionListener([propertiesPanel](int, juce::String refdes, juce::String, juce::String,
                                                           juce::String, juce::String, juce::String, juce::String) {
        propertiesPanel->showPart(refdes);
    });
    auto analyticsOwner = std::make_unique<AnalyticsPanel>();
    analyticsPanel = analyticsOwner.get();
    analyticsPanel->getNetlist = [schematicPanel, propertiesPanel] {
        propertiesPanel->commitPending();
        return schematicPanel->analyticsNetlist();
    };
    analyticsPanel->outputFolder = [this] { return generatedRunDirectory(); };
    analyticsPanel->bringToFront = [this] { showAnalytics(); };
    schematicPanel->setInstrumentOpenListener([this](juce::String refdes, juce::String symbolId) {
        openInstrumentWindow(refdes, symbolId);
    });
    // The board (PCB tab) is saved in the same diagram file, under "pcb".
    auto pcbOwner = std::make_unique<PcbPanel>();
    pcbPanel = pcbOwner.get();
    pcbPanel->fabFolder = [this] { return generatedRunDirectory().getChildFile("fab"); };
    pcbPanel->boardName = [this] { return currentDiagram.isNotEmpty() ? currentDiagram : juce::String("board"); };
    // Read only: the board check runs passively (on load, on showing the tab).
    pcbPanel->getSchematicParts = [schematicPanel] { return schematicPanel->pcbParts(); };
    resetCircuit = [this, panel = schematic.get()] {
        panel->clearCircuit();
        if (pcbPanel != nullptr)
        {
            pcbPanel->setDesign(pcb::BoardDesign::standard("fab-100"), true);
            pcbPanel->setLayout({}, true);
        }
    };
    getSimCircuit = [panel = schematic.get()] {
        auto sim = panel->buildSimNetlist();
        return std::make_tuple(sim.circuit, sim.audioInputBranch, sim.audioOutputNode);
    };
    getLiveParams = [panel = schematic.get()] {
        return panel->getLiveParams();
    };
    getCircuitJson = [this, panel = schematic.get()] {
        auto json = panel->buildCircuitJson().trimEnd();
        if (pcbPanel != nullptr && json.endsWithChar('}'))
            json = json.dropLastCharacters(1).trimEnd() + ",\n  \"pcb\": " + juce::JSON::toString(pcbPanel->design().toVar(), true)
                 + ",\n  \"pcb_layout\": " + juce::JSON::toString(pcbPanel->layoutState().toVar(), true) + "\n}";
        if (analyticsPanel != nullptr && json.endsWithChar('}'))
            json = json.dropLastCharacters(1).trimEnd() + ",\n  \"analyticsSetup\": "
                 + juce::JSON::toString(analyticsPanel->settingsState(), true) + "\n}";
        return json;
    };
    getXyceNetlist = [panel = schematic.get()] { return panel->buildXyceNetlist(); };
    getErcReport = [panel = schematic.get()] { return panel->buildErcReport(); };
    loadCircuitJson = [this, panel = schematic.get()](const juce::String& json, juce::String& error) {
        if (!panel->loadCircuitJson(json, error))
            return false;
        if (pcbPanel != nullptr)
        {
            const auto parsed = juce::JSON::parse(json);
            pcbPanel->setDesign(pcb::BoardDesign::fromVar(parsed.getProperty("pcb", {})), true);
            pcbPanel->setLayout(pcb::Layout::fromVar(parsed.getProperty("pcb_layout", {})), true);
        }
        if (analyticsPanel != nullptr)
        {
            const auto parsed = juce::JSON::parse(json);
            analyticsPanel->restoreSettingsState(parsed.getProperty("analyticsSetup", {}));
        }
        return true;
    };
    placeSymbolTool = [panel = schematic.get()](const juce::String& symbolId,
                                                float x,
                                                float y,
                                                const juce::String& value,
                                                const juce::String& frequency,
                                                const juce::String& busName) {
        return panel->placeSymbolFromTool(symbolId, x, y, value, frequency, busName);
    };
    connectNodesTool = [panel = schematic.get()](const juce::String& firstLabel,
                                                 const juce::String& secondLabel) {
        return panel->connectNodesFromTool(firstLabel, secondLabel);
    };
    openInstrumentTool = [panel = schematic.get()](const juce::String& refdes) {
        return panel->openInstrumentFromTool(refdes);
    };
    designHighPassTool = [this, panel = schematic.get()](double cutoffHz, double impedanceOhms) {
        closeFloatingInstrumentWindows();
        const auto schematicResult = panel->createRlcHighPassFilterFromTool(cutoffHz, impedanceOhms);
        const auto parsed = juce::JSON::parse(schematicResult);
        if (!parsed.isObject() || !(bool)parsed.getProperty("ok", false))
            return schematicResult;
        return designRlcHighPassFilterTool(cutoffHz, impedanceOhms);
    };
    designPushPullTool = [this, panel = schematic.get()] {
        closeFloatingInstrumentWindows();
        const auto schematicResult = panel->createPushPullAmplifierFromTool();
        const auto parsed = juce::JSON::parse(schematicResult);
        if (!parsed.isObject() || !(bool)parsed.getProperty("ok", false))
            return schematicResult;
        return designPushPullAmplifierTool();
    };
    autoLayoutTool = [panel = schematic.get()] {
        return panel->autoLayoutFromTool();
    };
    auto autoLayoutSelectionTool = [panel = schematic.get()] {
        return panel->autoLayoutSelectionFromTool();
    };
    AgentPanel::HostTools agentTools;
    agentTools.inspectCircuit = [this] {
        return getCircuitJson != nullptr ? getCircuitJson() : juce::String("{}");
    };
    agentTools.runErc = [this] { return runElectricalRuleCheckTool(); };
    agentTools.exportArtifacts = [this] { return exportCircuitArtifactsTool(); };
    agentTools.exportRealtimePreview = [this] { return exportFrustRealtimePreviewTool(); };
    agentTools.writeMarkdown = [this](const juce::String& title, const juce::String& markdown) {
        return writeAgentMarkdownTool(title, markdown);
    };
    agentTools.webSearch = [this](const juce::String& query, int maxResults) {
        return researchWebSearchTool(query, maxResults);
    };
    agentTools.placeSymbol = [this](const juce::String& symbolId,
                                    float x,
                                    float y,
                                    const juce::String& value,
                                    const juce::String& frequency,
                                    const juce::String& busName) {
        return placeSymbolTool != nullptr
            ? placeSymbolTool(symbolId, x, y, value, frequency, busName)
            : juce::String("{ \"ok\": false, \"error\": \"Schematic placement is unavailable.\" }");
    };
    agentTools.connectNodes = [this](const juce::String& firstLabel, const juce::String& secondLabel) {
        return connectNodesTool != nullptr
            ? connectNodesTool(firstLabel, secondLabel)
            : juce::String("{ \"ok\": false, \"error\": \"Schematic wiring is unavailable.\" }");
    };
    agentTools.openInstrument = [this](const juce::String& refdes) {
        return openInstrumentTool != nullptr
            ? openInstrumentTool(refdes)
            : juce::String("{ \"ok\": false, \"error\": \"Instrument opening is unavailable.\" }");
    };
    agentTools.designHighPass = [this](double cutoffHz, double impedanceOhms) {
        return designHighPassTool != nullptr
            ? designHighPassTool(cutoffHz, impedanceOhms)
            : juce::String("{ \"ok\": false, \"error\": \"High-pass filter design is unavailable.\" }");
    };
    agentTools.designPushPull = [this] {
        return designPushPullTool != nullptr
            ? designPushPullTool()
            : juce::String("{ \"ok\": false, \"error\": \"Push-pull amplifier design is unavailable.\" }");
    };
    agentTools.autoLayout = [this] {
        return autoLayoutTool != nullptr
            ? autoLayoutTool()
            : juce::String("{ \"ok\": false, \"error\": \"Schematic auto-layout is unavailable.\" }");
    };
    agentTools.autoLayoutSelection = [autoLayoutSelectionTool] {
        return autoLayoutSelectionTool();
    };
    agentTools.cookbookLookup = [this](const juce::String& query, int maxCards) {
        return cookbookLookupTool(query, maxCards);
    };
    agentTools.cookbookCoverage = [this] { return cookbookCoverageTool(); };
    agentTools.cookbookValidate = [this] { return cookbookValidateTool(); };
    agentTools.cookbookAcceptanceGoals = [this](const juce::String& domainOrId) {
        return cookbookAcceptanceGoalsTool(domainOrId);
    };
    agentTools.cookbookAcceptanceSummary = [this](const juce::String& domainOrId) {
        return cookbookAcceptanceSummaryTool(domainOrId);
    };
    agentTools.cookbookAcceptanceStart = [this](const juce::String& goalId) {
        return cookbookAcceptanceStartTool(goalId);
    };
    agentTools.cookbookAcceptanceRecord = [this](const juce::String& reportPath,
                                                 const juce::String& evidenceType,
                                                 const juce::String& label,
                                                 const juce::String& detail,
                                                 const juce::String& pathOrValue,
                                                 const juce::String& status) {
        return cookbookAcceptanceRecordTool(reportPath, evidenceType, label, detail, pathOrValue, status);
    };
    agentTools.capabilityGapRecord = [this](const juce::String& category,
                                            const juce::String& description,
                                            const juce::String& neededCapability,
                                            const juce::String& evidence,
                                            const juce::String& source,
                                            const juce::String& status) {
        return capabilityGapRecordTool(category, description, neededCapability, evidence, source, status);
    };
    agentTools.toolManifest = [this] { return buildAssistantToolManifestJson(); };
    agentTools.longTool = [this](const juce::String& name, const juce::var& args, std::function<void(juce::String)> done) {
        if (name != "pcb_route" || pcbPanel == nullptr)
            return false;
        if (pcbPanel->isRouting())
        {
            done(R"({"ok": false, "tool": "pcb_route", "error": "The board is already being routed."})");
            return true;
        }
        auto rules = pcbPanel->layoutState().rules;
        auto set = [&](const char* key, double& field) {
            if (args.hasProperty(key) && (double)args.getProperty(key, 0.0) > 0.0) field = (double)args.getProperty(key, field);
        };
        set("track_width", rules.trackWidth);
        set("clearance", rules.clearance);
        set("via_diameter", rules.viaDiameter);
        set("via_drill", rules.viaDrill);
        juce::Component::SafePointer<ElectronicsWorkbench> safe(this);
        pcbPanel->route(rules, [safe, done] {
            done(safe != nullptr ? safe->pcbLayoutTool("pcb_route_result", juce::var(new juce::DynamicObject()))
                                 : juce::String(R"({"ok": false, "tool": "pcb_route", "error": "The app closed."})"));
        });
        return true;
    };
    agentTools.schematicTool = [this, schematicPanel](const juce::String& name, const juce::var& args) {
        if (name.startsWith("project_") || name.startsWith("diagram_"))
            return projectTool(name, args);
        if (name.startsWith("analytics_"))
            return analyticsTool(name, args);
        if (name == "frust_check" || name == "frust_run")
            return frustTool(name, args);
        if (name.startsWith("pcb_board_"))
            return pcbTool(name, args);
        if (name.startsWith("pcb_"))
            return pcbLayoutTool(name, args);
        return schematicPanel->runSchematicTool(name, args);
    };
    schematicPanel->outputDirectory = [this] { return generatedRunDirectory(); };
    exportSchematicImage = [schematicPanel] {
        const auto result = juce::JSON::parse(schematicPanel->runSchematicTool("schematic_export_image", juce::var(new juce::DynamicObject())));
        return (bool)result.getProperty("ok", false)
            ? "Exported schematic image " + result.getProperty("image", {}).toString()
            : "Schematic image export failed: " + result.getProperty("error", {}).toString();
    };
    agentTools.log = [this](const juce::String& text) { appendLog(text); };
    auto agent = std::make_unique<AgentPanel>(std::move(agentTools));
    auto* agentPanel = agent.get();
    openAgentSettingsDialog = [agentPanel] { agentPanel->showAiSettingsForSelected(); };
    dockManager->registerPanel("schematic", "Schematic", std::move(schematic), CreationDock::DockTargetZone::CenterTab);
    analyticsDockPanel = dockManager->registerPanel("analytics", "Analytics", std::move(analyticsOwner), CreationDock::DockTargetZone::CenterTab);
    dockManager->registerPanel("pcb", "PCB", std::move(pcbOwner), CreationDock::DockTargetZone::CenterTab);
    auto frustOwner = std::make_unique<FrustPanel>();
    frustPanel = frustOwner.get();
    dockManager->registerPanel("frust", "Frust", std::move(frustOwner), CreationDock::DockTargetZone::Bottom);
    dockManager->registerPanel("console", "Console", std::make_unique<ConsolePanel>(logConsole), CreationDock::DockTargetZone::Bottom);
    auto lp = std::make_unique<LogPanel>(); logPanel = lp.get(); dockManager->registerPanel("log", "Log", std::move(lp), CreationDock::DockTargetZone::Bottom);
    dockManager->registerPanel("agent", "BYOK Agent", std::move(agent), CreationDock::DockTargetZone::Right);
    dockManager->registerPanel("properties", "Properties", std::move(properties), CreationDock::DockTargetZone::Right);
    dockManager->registerPanel("ingestion", "Spec Ingestion", std::make_unique<SpecIngestionPanel>(), CreationDock::DockTargetZone::Right);
    dockManager->registerPanel("sourcing", "Parts Sourcing", std::make_unique<PartsSourcingPanel>(), CreationDock::DockTargetZone::Right);

    dockManager->loadLayoutFromFile(layoutFile());
    appendLog("Electronics research shell initialized.");

    circuit_sim::setCapitalMIsMilli(prefs::get("units.capital_m") != "Mega");
    snapModeButton.setToggleState(prefs::isOn("display.snap_default"), juce::dontSendNotification);
    preferenceListener = prefs::addListener([this](const juce::String& key) {
        if (key == "units.capital_m")
            circuit_sim::setCapitalMIsMilli(prefs::get(key) != "Mega");
        if (key == "display.snap_default")
        {
            snapModeButton.setToggleState(prefs::isOn(key), juce::dontSendNotification);
            if (setSnapEnabled != nullptr) setSnapEnabled(prefs::isOn(key));
        }
        appendLog("Preference " + key + " = " + prefs::get(key));
    });
    auto safeThis = juce::Component::SafePointer<ElectronicsWorkbench>(this);
    juce::MessageManager::callAsync([safeThis] {
        if (safeThis != nullptr)
            safeThis->openMostRecentProject();
    });
}

void ElectronicsWorkbench::showPreferences()
{
    if (preferencesWindow == nullptr)
    {
        auto* window = new FloatingInstrumentWindow("Preferences");
        window->setContentOwned(new PreferencesView(audioPipeline.get()), true);
        window->centreWithSize(820, 560);
        preferencesWindow.reset(window);
    }
    preferencesWindow->setVisible(true);
    preferencesWindow->toFront(true);
}

ElectronicsWorkbench::~ElectronicsWorkbench()
{
    prefs::removeListener(preferenceListener);
    preferencesWindow = nullptr;
    if (dockManager != nullptr)
        dockManager->saveLayoutToFile(layoutFile());
    menuBar = nullptr;
}

void ElectronicsWorkbench::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff171b20));
    g.setColour(juce::Colour(0xff26323d));
    g.fillRect(0, menuHeight, getWidth(), toolbarHeight);
}

void ElectronicsWorkbench::resized()
{
    auto area = getLocalBounds();
    menuBar->setBounds(area.removeFromTop(menuHeight));

    auto toolbar = area.removeFromTop(toolbarHeight).reduced(8, 4);
    titleLabel.setBounds(toolbar.removeFromLeft(230));
    newButton.setBounds(toolbar.removeFromLeft(70));
    toolbar.removeFromLeft(6);
    openDiagramButton.setBounds(toolbar.removeFromLeft(76));
    toolbar.removeFromLeft(6);
    ercButton.setBounds(toolbar.removeFromLeft(70));
    toolbar.removeFromLeft(6);
    transientButton.setBounds(toolbar.removeFromLeft(100));
    toolbar.removeFromLeft(6);
    compileButton.setBounds(toolbar.removeFromLeft(140));
    toolbar.removeFromLeft(10);
    stampModeButton.setBounds(toolbar.removeFromLeft(120));
    snapModeButton.setBounds(toolbar.removeFromLeft(82));
    toolbar.removeFromLeft(8);
    zoomOutButton.setBounds(toolbar.removeFromLeft(34));
    toolbar.removeFromLeft(4);
    zoomResetButton.setBounds(toolbar.removeFromLeft(58));
    toolbar.removeFromLeft(4);
    zoomInButton.setBounds(toolbar.removeFromLeft(34));
    

    if (dockManager != nullptr)
        dockManager->setBounds(area);
}

void ElectronicsWorkbench::applySchematicZoom(float zoom)
{
    const auto clampedZoom = std::clamp(zoom, 0.5f, 2.5f);
    if (setSchematicZoom != nullptr)
        setSchematicZoom(clampedZoom);

    zoomResetButton.setButtonText(juce::String((int)std::round(clampedZoom * 100.0f)) + "%");
    appendLog("Schematic zoom set to " + juce::String((int)std::round(clampedZoom * 100.0f)) + "%.");
}

void ElectronicsWorkbench::adjustSchematicZoom(float factor)
{
    const auto currentZoom = getSchematicZoom != nullptr ? getSchematicZoom() : 1.0f;
    applySchematicZoom(currentZoom * factor);
}

juce::StringArray ElectronicsWorkbench::getMenuBarNames()
{
    return { "File", "Circuit", "Analytics", "Agent", "View", "Help" };
}

juce::PopupMenu ElectronicsWorkbench::getMenuForIndex(int, const juce::String& menuName)
{
    juce::PopupMenu menu;
    if (menuName == "File")
    {
        addProjectMenuItems(menu);
        menu.addSeparator();
        menu.addItem(exportSchematicImageItem, "Export Schematic Image");
        menu.addSeparator();
        menu.addItem(preferencesItem, "Preferences...");
    }
    else if (menuName == "Circuit")
    {
        menu.addItem(importComponent, "Import / Fetch Component Spec...");
        menu.addSeparator();
        menu.addItem(autoLayoutDiagramItem, "Auto Layout Diagram");
        menu.addSeparator();
        menu.addItem(runErc, "Run ERC");
        menu.addSeparator();
        menu.addItem(startAudioSimItem, isAudioSimRunning ? "Stop Audio Pipeline Test" : "Start Audio Pipeline Test");
        menu.addItem(loadAudioSourceItem, "Load Audio File Source...");
    }
    else if (menuName == "Analytics")
    {
        menu.addItem(openAnalyticsItem, "Open Analytics");
        menu.addSeparator();
        for (size_t i = 0; i < analytics::analyses().size(); ++i)
            menu.addItem(analyticsMenuBase + (int)i, analytics::analyses()[i].title + "...");
        menu.addSeparator();
        menu.addItem(runCompiledPreview, "Compile Realtime Preview");
    }
    else if (menuName == "Agent")
    {
        menu.addItem(openAgentSettings, "BYOK Agent Settings...");
        menu.addItem(exportAgentTools, "Export Tool Manifest");
    }
    else if (menuName == "View")
    {
        menu.addItem(toggleStampModeItem, "Stamp Mode", true, stampModeButton.getToggleState());
        menu.addSeparator();
        menu.addItem(resetLayout, "Reset Dock Layout");
    }
    else if (menuName == "Help")
    {
        menu.addItem(openResearchSpec, "Open Research Spec");
    }
    return menu;
}

void ElectronicsWorkbench::menuItemSelected(int menuItemID, int)
{
    if (menuItemID >= analyticsMenuBase && menuItemID < analyticsMenuBase + (int)analytics::analyses().size())
    {
        if (analyticsPanel != nullptr)
            analyticsPanel->selectAnalysis(analytics::analyses()[(size_t)(menuItemID - analyticsMenuBase)].id);
        showAnalytics();
        return;
    }
    if (menuItemID >= recentProjectBase)
    {
        handleProjectMenu(menuItemID);
        return;
    }

    switch (menuItemID)
    {
        case newProject:
        case openProject:
        case saveProject:
        case saveDiagramAsItem:
        case renameProjectItem:
        case newDiagramItem:
        case openDiagramFileItem:
        case renameDiagramItem:
        case duplicateDiagramItem:
        case deleteDiagramItem:
            handleProjectMenu(menuItemID);
            break;
        case preferencesItem: showPreferences(); break;
        case exportSchematicImageItem:
            if (exportSchematicImage != nullptr)
                appendLog(exportSchematicImage());
            break;
        case resetLayout:
            if (dockManager != nullptr) dockManager->resetLayout();
            appendLog("Dock layout reset.");
            break;
        case toggleStampModeItem:
            stampModeButton.setToggleState(!stampModeButton.getToggleState(), juce::sendNotificationSync);
            break;
        case importComponent: appendLog("Component ingestion stub: BYOK agent/provider workflow pending."); break;
        case autoLayoutDiagramItem: autoLayoutDiagram(); break;
        case runErc: runElectricalRuleCheck(); break;
        case openAnalyticsItem: showAnalytics(); break;
        case runCompiledPreview: exportFrustRealtimePreview(); break;
        case loadAudioSourceItem: chooseAudioSourceFile(); break;
        case startAudioSimItem:
        {
            if (isAudioSimRunning)
            {
                audioPipeline->setProcessCallback(nullptr);
                isAudioSimRunning = false;
                appendLog("Audio Pipeline Test stopped.");
                break;
            }

            if (getSimCircuit == nullptr)
                break;
                
            auto data = getSimCircuit();
            auto circuit = std::get<0>(data);
            auto audioIn = std::get<1>(data);
            auto audioOut = std::get<2>(data);
            if (circuit.elements().empty())
            {
                appendLog("Schematic is empty.");
                break;
            }
            
            audio_dsp::Config config;
            config.audioInputElement = audioIn;
            config.audioOutputNode = audioOut;
            config.sampleRate = audioPipeline->getDeviceManager().getAudioDeviceSetup().sampleRate;
            auto model = audio_dsp::build(circuit, config);
            if (!model.ok)
            {
                appendLog("Failed to build DSP model:\n" + juce::String(model.error));
                break;
            }

            // Generate the Frust DSP code
            auto source = juce::String(model.frustSource());
            
            // Ensure manifest is added
            auto manifest = frust_engine::manifestLine("audio_dsp", "Generated DSP processing function.", audio_dsp::Model::requiredHostFunctions());
            source = juce::String(manifest) + "\n" + source;

            // Log the generated source to debug
            appendLog("Generated DSP:\n" + source);

            // Load into our engine
            auto result = audioEngine.load("audio_dsp", source.toStdString());
            if (!result.ok)
            {
                appendLog("Failed to compile audio DSP:\n" + juce::String(result.report()));
                break;
            }

            // Get the function pointer.
            // In Frust, our signature is `pub fn process_sample(audio_in: f64, ws: Array<f64, size>, coeffs: Array<f64, c_size>) -> f64`
            typedef double (*ProcessFn)(double, double*, const double*);
            auto* fn = reinterpret_cast<ProcessFn>(audioEngine.function("audio_dsp", "process_sample"));
            
            if (fn != nullptr)
            {
                appendLog("Audio DSP compiled and loaded. Wiring to pipeline...");
                auto workspace = std::make_shared<std::vector<double>>(model.initialWorkspace);
                if (workspace->size() < (size_t)model.workspaceSize)
                    workspace->resize(model.workspaceSize, 0.0);

                auto dspModel = std::make_shared<audio_dsp::Model>(model);
                auto liveCoeffs = std::make_shared<LiveCoeffs>(model.coeffSize());
                liveCoeffs->publish(dspModel->computeLiveCoefficients({}));

                auto poller = std::make_shared<LiveParameterPoller>(dspModel, liveCoeffs, getLiveParams);

                audioPipeline->setProcessCallback([fn, workspace, liveCoeffs, poller](const float* in, float* out, int samples) {
                    const double* coeffs = liveCoeffs->acquireRead();
                    for (int i = 0; i < samples; ++i) {
                        out[i] = static_cast<float>(fn(static_cast<double>(in[i]), workspace->data(), coeffs));
                    }
                });
                isAudioSimRunning = true;
            }
            else
            {
                appendLog("Compiled audio DSP but couldn't find process_sample function.");
            }
            break;
        }
        case openAgentSettings:
            if (openAgentSettingsDialog != nullptr) openAgentSettingsDialog();
            else appendLog("BYOK agent settings are unavailable.");
            break;
        case exportAgentTools: exportAssistantToolManifest(); break;
        case openResearchSpec: showSpecDocument(); break;
        default: break;
    }
}

juce::File ElectronicsWorkbench::layoutFile() const
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("DjehutiElectronicsLab")
        .getChildFile("layout.json");
}


juce::File ElectronicsWorkbench::generatedRunDirectory() const
{
    if (project.isOpen() && currentDiagram.isNotEmpty())
        return project_store::outputsDirectory(project, currentDiagram);
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("DjehutiElectronicsLab")
        .getChildFile("scratch_outputs");
}

void ElectronicsWorkbench::appendLog(const juce::String& reason, const juce::String& code, const juce::String& details)
{
    if (auto* lp = dynamic_cast<LogPanel*>(logPanel))
        lp->addEntry(reason, code, details);
}

void ElectronicsWorkbench::appendLog(const juce::String& text)
{
    appendLog("System", "0x00", text);
}

void ElectronicsWorkbench::closeFloatingInstrumentWindows()
{
    for (auto* window : floatingInstrumentWindows)
        if (window != nullptr)
            window->setVisible(false);
    floatingInstrumentWindows.clear();
}

void ElectronicsWorkbench::resetResearchState()
{
    closeFloatingInstrumentWindows();
    if (resetCircuit != nullptr)
        resetCircuit();
    else
        appendLog("New electronics research project initialized.");
}

juce::String ElectronicsWorkbench::buildAssistantToolManifestJson() const
{
    const auto runDir = generatedRunDirectory();
    juce::String text;
    text << "{\n";
    text << "  \"schemaVersion\": 1,\n";
    text << "  \"kind\": \"djehuti_assistant_tool_manifest\",\n";
    text << "  \"toolSurface\": \"prototype-local\",\n";
    text << "  \"artifactDirectory\": " << jsonQuote(runDir.getFullPathName()) << ",\n";
    text << "  \"localAgentApi\": {\n";
    text << "    \"schema\": \"djehuti-electronics-agent-api\",\n";
    text << "    \"discoveryFile\": " << jsonQuote(LocalAgentApi::getDiscoveryFile().getFullPathName()) << "\n";
    text << "  },\n";
    text << "  \"tools\": [\n";
    text << "    {\n";
    text << "      \"name\": \"cookbook_lookup\",\n";
    text << "      \"displayName\": \"cookbook.lookup\",\n";
    text << "      \"description\": \"Search structured electronics cookbook cards for topology candidates, equations, analysis recipes, validation criteria, and capability gaps.\",\n";
    text << "      \"mode\": \"read_only_knowledge\",\n";
    text << "      \"inputs\": {\n";
    text << "        \"query\": \"engineering requirement or cookbook topic\",\n";
    text << "        \"maxCards\": \"optional maximum number of cards\"\n";
    text << "      },\n";
    text << "      \"outputs\": { \"cards\": \"matching cookbook/knowledge cards with provenance\" }\n";
    text << "    },\n";
    text << "    {\n";
    text << "      \"name\": \"cookbook_coverage\",\n";
    text << "      \"displayName\": \"cookbook.coverage\",\n";
    text << "      \"description\": \"Report cookbook coverage against the file-backed taxonomy and identify missing recipe categories.\",\n";
    text << "      \"mode\": \"read_only_knowledge\",\n";
    text << "      \"inputs\": {},\n";
    text << "      \"outputs\": { \"coveredCategories\": \"array\", \"missingCategories\": \"array\", \"coverageRatio\": \"number\" }\n";
    text << "    },\n";
    text << "    {\n";
    text << "      \"name\": \"cookbook_validate\",\n";
    text << "      \"displayName\": \"cookbook.validate\",\n";
    text << "      \"description\": \"Validate cookbook entries for required structured fields, taxonomy alignment, duplicate ids, and malformed raw entries.\",\n";
    text << "      \"mode\": \"read_only_knowledge\",\n";
    text << "      \"inputs\": {},\n";
    text << "      \"outputs\": { \"status\": \"passed or failed\", \"errors\": \"array\", \"warnings\": \"array\" }\n";
    text << "    },\n";
    text << "    {\n";
    text << "      \"name\": \"cookbook_acceptance_goals\",\n";
    text << "      \"displayName\": \"cookbook.acceptance_goals\",\n";
    text << "      \"description\": \"List representative agent acceptance goals, optionally filtered by domain or exact goal id.\",\n";
    text << "      \"mode\": \"read_only_knowledge\",\n";
    text << "      \"inputs\": { \"domainOrId\": \"optional domain or exact goal id\" },\n";
    text << "      \"outputs\": { \"goals\": \"matching acceptance goals\", \"requiredDomains\": \"array\" }\n";
    text << "    },\n";
    text << "    {\n";
    text << "      \"name\": \"cookbook_acceptance_summary\",\n";
    text << "      \"displayName\": \"cookbook.acceptance_summary\",\n";
    text << "      \"description\": \"Summarize generated acceptance run reports, evidence counts, and determinations.\",\n";
    text << "      \"mode\": \"read_only_generated_artifacts\",\n";
    text << "      \"inputs\": { \"domainOrId\": \"optional domain or exact goal id\" },\n";
    text << "      \"outputs\": { \"runs\": \"matching generated acceptance report summaries\" }\n";
    text << "    },\n";
    text << "    {\n";
    text << "      \"name\": \"cookbook_acceptance_start\",\n";
    text << "      \"displayName\": \"cookbook.acceptance_start\",\n";
    text << "      \"description\": \"Create a structured acceptance report scaffold for a representative goal id.\",\n";
    text << "      \"mode\": \"write_generated_artifacts\",\n";
    text << "      \"inputs\": { \"goalId\": \"exact acceptance goal id\" },\n";
    text << "      \"outputs\": { \"jsonReport\": \"acceptance report scaffold\", \"markdownReport\": \"human-readable checklist\" }\n";
    text << "    },\n";
    text << "    {\n";
    text << "      \"name\": \"cookbook_acceptance_record\",\n";
    text << "      \"displayName\": \"cookbook.acceptance_record\",\n";
    text << "      \"description\": \"Append structured evidence to an acceptance report created by cookbook_acceptance_start.\",\n";
    text << "      \"mode\": \"write_generated_artifacts\",\n";
    text << "      \"inputs\": {\n";
    text << "        \"reportPath\": \"jsonReport path returned by cookbook_acceptance_start\",\n";
    text << "        \"evidenceType\": \"retrieved_card, tool_call, artifact, capability_gap, criterion, or note\",\n";
    text << "        \"label\": \"short evidence label\",\n";
    text << "        \"detail\": \"evidence details\",\n";
    text << "        \"pathOrValue\": \"artifact path, tool result path, card id, or measured value\",\n";
    text << "        \"status\": \"observed, passed, failed, gap, unverified, or not_started\"\n";
    text << "      },\n";
    text << "      \"outputs\": { \"jsonReport\": \"updated acceptance report\", \"markdownReport\": \"updated human-readable log\" }\n";
    text << "    },\n";
    text << "    {\n";
    text << "      \"name\": \"capability_gap_record\",\n";
    text << "      \"displayName\": \"capability_gap.record\",\n";
    text << "      \"description\": \"Append a reusable missing-capability record to the project gap registry.\",\n";
    text << "      \"mode\": \"write_project_memory\",\n";
    text << "      \"inputs\": {\n";
    text << "        \"category\": \"solver, component_model, analysis, plotting, instrument, ui, or agent_workflow\",\n";
    text << "        \"description\": \"what blocked the engineering step\",\n";
    text << "        \"neededCapability\": \"reusable tool or app capability needed to close the gap\",\n";
    text << "        \"evidence\": \"tool result, report path, or observation proving the gap\",\n";
    text << "        \"source\": \"acceptance goal id, report path, cookbook card id, or user goal\",\n";
    text << "        \"status\": \"open, planned, in_progress, closed, or deferred\"\n";
    text << "      },\n";
    text << "      \"outputs\": { \"gapRegistry\": \"project-local JSONL capability gap registry\" }\n";
    text << "    },\n";
    text << "    {\n";
    text << "      \"name\": \"circuit_inspect\",\n";
    text << "      \"displayName\": \"circuit.inspect\",\n";
    text << "      \"description\": \"Read the current authoritative circuit JSON from the schematic model.\",\n";
    text << "      \"mode\": \"read_only_analysis\",\n";
    text << "      \"inputs\": {},\n";
    text << "      \"outputs\": { \"circuitJson\": \"inline JSON object\" }\n";
    text << "    },\n";
    text << "    {\n";
    text << "      \"name\": \"circuit_run_erc\",\n";
    text << "      \"displayName\": \"circuit.run_erc\",\n";
    text << "      \"description\": \"Run Electrical Rule Check on the current schematic model.\",\n";
    text << "      \"mode\": \"read_only_analysis\",\n";
    text << "      \"inputs\": {},\n";
    text << "      \"outputs\": {\n";
    text << "        \"markdownReport\": " << jsonQuote(runDir.getChildFile("erc_report.md").getFullPathName()) << ",\n";
    text << "        \"jsonResult\": " << jsonQuote(runDir.getChildFile("erc_tool_result.json").getFullPathName()) << "\n";
    text << "      }\n";
    text << "    },\n";
    text << "    {\n";
    text << "      \"name\": \"simulation_export_artifacts\",\n";
    text << "      \"displayName\": \"simulation.export_artifacts\",\n";
    text << "      \"description\": \"Export circuit.json, generated.cir, and lab_instruments.json for solver/dataset work.\",\n";
    text << "      \"mode\": \"write_generated_artifacts\",\n";
    text << "      \"outputs\": {\n";
    text << "        \"circuitJson\": " << jsonQuote(runDir.getChildFile("circuit.json").getFullPathName()) << ",\n";
    text << "        \"xyceNetlist\": " << jsonQuote(runDir.getChildFile("generated.cir").getFullPathName()) << ",\n";
    text << "        \"instrumentJson\": " << jsonQuote(runDir.getChildFile("lab_instruments.json").getFullPathName()) << "\n";
    text << "      }\n";
    text << "    },\n";
    text << "    {\n";
    text << "      \"name\": \"frust_realtime_preview_export\",\n";
    text << "      \"displayName\": \"frust.realtime_preview_export\",\n";
    text << "      \"description\": \"Export the current schematic as a Djehuti audio model package for the generic Model Player VST3 runtime.\",\n";
    text << "      \"mode\": \"write_generated_artifacts\",\n";
    text << "      \"outputs\": {\n";
    text << "        \"manifest\": " << jsonQuote(runDir.getChildFile("frust_realtime_preview").getChildFile("manifest.json").getFullPathName()) << ",\n";
    text << "        \"frustSource\": " << jsonQuote(runDir.getChildFile("frust_realtime_preview").getChildFile("model.fr").getFullPathName()) << ",\n";
    text << "        \"frustIl\": " << jsonQuote(runDir.getChildFile("frust_realtime_preview").getChildFile("model.frust-il").getFullPathName()) << ",\n";
    text << "        \"uiSource\": " << jsonQuote(runDir.getChildFile("frust_realtime_preview").getChildFile("ui.fr").getFullPathName()) << ",\n";
    text << "        \"uiIl\": " << jsonQuote(runDir.getChildFile("frust_realtime_preview").getChildFile("ui.frust-il").getFullPathName()) << ",\n";
    text << "        \"parameters\": " << jsonQuote(runDir.getChildFile("frust_realtime_preview").getChildFile("parameters.json").getFullPathName()) << ",\n";
    text << "        \"ui\": " << jsonQuote(runDir.getChildFile("frust_realtime_preview").getChildFile("ui.json").getFullPathName()) << "\n";
    text << "      }\n";
    text << "    },\n";
    text << "    {\n";
    text << "      \"name\": \"agent_write_markdown\",\n";
    text << "      \"displayName\": \"agent.write_markdown\",\n";
    text << "      \"description\": \"Write markdown into the agent workspace and render it in the BYOK Agent window with KaTeX equation support.\",\n";
    text << "      \"mode\": \"write_generated_artifacts_and_update_agent_transcript\",\n";
    text << "      \"status\": \"active\",\n";
    text << "      \"inputs\": { \"title\": \"report title\", \"markdown\": \"markdown source; use $...$ for inline equations and $$ on separate lines for display equations\" },\n";
    text << "      \"outputs\": { \"markdownPath\": \"written markdown source\", \"htmlPath\": \"rendered HTML report\" }\n";
    text << "    },\n";
    text << "    {\n";
    text << "      \"name\": \"research_web_search\",\n";
    text << "      \"displayName\": \"research.web_search\",\n";
    text << "      \"description\": \"Search the web for current references, standards, datasheets, or source links when local cookbook/app state is not enough.\",\n";
    text << "      \"mode\": \"read_external_web\",\n";
    text << "      \"status\": \"active\",\n";
    text << "      \"inputs\": { \"query\": \"focused search query\", \"maxResults\": \"1 to 8 compact results\" },\n";
    text << "      \"outputs\": { \"results\": \"title/snippet/url entries\" }\n";
    text << "    },\n";
    text << "    {\n";
    text << "      \"name\": \"filter_design_high_pass\",\n";
    text << "      \"displayName\": \"filter.design_high_pass\",\n";
    text << "      \"description\": \"Design a matched RLC 2nd order high-pass filter, draw it on the schematic, run the internal AC sweep, and export graph/report artifacts.\",\n";
    text << "      \"mode\": \"modify_schematic_model_and_write_generated_artifacts\",\n";
    text << "      \"status\": \"active\",\n";
    text << "      \"inputs\": {\n";
    text << "        \"cutoffHz\": \"target cutoff frequency in Hz\",\n";
    text << "        \"impedanceOhms\": \"matched source/load impedance in ohms\"\n";
    text << "      },\n";
    text << "      \"outputs\": {\n";
    text << "        \"spiceNetlist\": " << jsonQuote(runDir.getChildFile("rlc_high_pass_ac.cir").getFullPathName()) << ",\n";
    text << "        \"csv\": " << jsonQuote(runDir.getChildFile("rlc_high_pass_ac.csv").getFullPathName()) << ",\n";
    text << "        \"svg\": " << jsonQuote(runDir.getChildFile("rlc_high_pass_response.svg").getFullPathName()) << ",\n";
    text << "        \"report\": " << jsonQuote(runDir.getChildFile("rlc_high_pass_report.md").getFullPathName()) << "\n";
    text << "      }\n";
    text << "    },\n";
    text << "    {\n";
    text << "      \"name\": \"amplifier_design_push_pull\",\n";
    text << "      \"displayName\": \"amplifier.design_push_pull\",\n";
    text << "      \"description\": \"Design a diode-biased complementary push-pull audio output stage with source, rails, 8 ohm load, grounds, and scope instrumentation.\",\n";
    text << "      \"mode\": \"modify_schematic_model_and_write_generated_artifacts\",\n";
    text << "      \"status\": \"active\",\n";
    text << "      \"inputs\": {},\n";
    text << "      \"outputs\": {\n";
    text << "        \"circuitJson\": " << jsonQuote(runDir.getChildFile("push_pull_amplifier_circuit.json").getFullPathName()) << ",\n";
    text << "        \"previewNetlist\": " << jsonQuote(runDir.getChildFile("push_pull_amplifier_preview.cir").getFullPathName()) << ",\n";
    text << "        \"ercReport\": " << jsonQuote(runDir.getChildFile("push_pull_amplifier_erc.md").getFullPathName()) << ",\n";
    text << "        \"report\": " << jsonQuote(runDir.getChildFile("push_pull_amplifier_report.md").getFullPathName()) << "\n";
    text << "      }\n";
    text << "    },\n";
    text << "    {\n";
    text << "      \"name\": \"schematic_auto_layout\",\n";
    text << "      \"displayName\": \"schematic.auto_layout\",\n";
    text << "      \"description\": \"Arrange the current schematic into a readable left-to-right diagram using standard spacing conventions.\",\n";
    text << "      \"mode\": \"modify_schematic_model\",\n";
    text << "      \"status\": \"active\",\n";
    text << "      \"inputs\": {},\n";
    text << "      \"outputs\": { \"layout\": \"updated component coordinates\" }\n";
    text << "    },\n";
    text << "    {\n";
    text << "      \"name\": \"schematic_auto_layout_selection\",\n";
    text << "      \"displayName\": \"schematic.auto_layout_selection\",\n";
    text << "      \"description\": \"Arrange only the currently selected schematic components.\",\n";
    text << "      \"mode\": \"modify_schematic_model\",\n";
    text << "      \"status\": \"active\",\n";
    text << "      \"inputs\": {},\n";
    text << "      \"outputs\": { \"layout\": \"updated selected component coordinates\" }\n";
    text << "    },\n";
    text << "    {\n";
    text << "      \"name\": \"schematic_place_symbol\",\n";
    text << "      \"displayName\": \"schematic.place_symbol\",\n";
    text << "      \"description\": \"Create or extend diagrams by placing symbols and instrument nodes on the schematic grid.\",\n";
    text << "      \"mode\": \"modify_schematic_model\",\n";
    text << "      \"status\": \"active\",\n";
    text << "      \"inputs\": {\n";
    text << "        \"symbolId\": [\"resistor\", \"capacitor\", \"inductor\", \"diode\", \"voltage_source\", \"ground\", \"oscilloscope_2ch\", \"digital_multimeter\"],\n";
    text << "        \"x\": \"grid coordinate\",\n";
    text << "        \"y\": \"grid coordinate\",\n";
    text << "        \"value\": \"optional component value or instrument mode\",\n";
    text << "        \"frequency\": \"optional source frequency\",\n";
    text << "        \"busName\": \"optional rail/net label\"\n";
    text << "      }\n";
    text << "    },\n";
    text << "    {\n";
    text << "      \"name\": \"schematic_connect\",\n";
    text << "      \"displayName\": \"schematic.connect\",\n";
    text << "      \"description\": \"Connect two schematic pins or junctions by label.\",\n";
    text << "      \"mode\": \"modify_schematic_model\",\n";
    text << "      \"status\": \"active\",\n";
    text << "      \"inputs\": {\n";
    text << "        \"a\": \"node label such as R1.1, V2.+, GND3.0, SCOPE4.CH1, or N1\",\n";
    text << "        \"b\": \"node label\"\n";
    text << "      }\n";
    text << "    },\n";
    text << "    {\n";
    text << "      \"name\": \"instrument_open_panel\",\n";
    text << "      \"displayName\": \"instrument.open_panel\",\n";
    text << "      \"description\": \"Open a floating instrument panel only for an existing wired schematic instrument node, and only after an explicit user request. Normal AI circuit creation should place and wire instrument nodes, not open panels.\",\n";
    text << "      \"status\": \"active\",\n";
    text << "      \"inputs\": { \"refdes\": \"existing wired instrument reference designator\" }\n";
    text << "    }";
    std::vector<ToolSpecText> manifestSpecs;
    for (const auto& spec : schematicToolSpecs)
        manifestSpecs.push_back({ spec.name, spec.description, spec.schema });
    for (const auto& spec : analyticsToolSpecs())
        manifestSpecs.push_back(spec);
    for (const auto& spec : manifestSpecs)
    {
        text << ",\n    {\n";
        text << "      \"name\": " << jsonQuote(spec.name) << ",\n";
        text << "      \"displayName\": " << jsonQuote(spec.name.replaceFirstOccurrenceOf("_", ".")) << ",\n";
        text << "      \"description\": " << jsonQuote(spec.description) << ",\n";
        text << "      \"mode\": " << (spec.name.startsWith("analytics_") ? "\"read_only_analysis\"" : "\"modify_schematic_model\"") << ",\n";
        text << "      \"status\": \"active\",\n";
        text << "      \"inputSchema\": " << spec.schema << "\n";
        text << "    }";
    }
    text << "\n  ],\n";
    text << "  \"instrumentPolicy\": {\n";
    text << "    \"preferredPlacement\": \"schematic_node\",\n";
    text << "    \"panelOpenRule\": \"user_double_clicks_placed_instrument_node\",\n";
    text << "    \"assistantPanelOpenPolicy\": \"only_after_explicit_user_request\",\n";
    text << "    \"dockableLater\": true,\n";
    text << "    \"supportedNodes\": [\"oscilloscope_2ch\", \"digital_multimeter\"]\n";
    text << "  }\n";
    text << "}\n";
    return text;
}

juce::String ElectronicsWorkbench::cookbookLookupTool(const juce::String& query, int maxCards) const
{
    const auto limit = juce::jlimit(1, 12, maxCards <= 0 ? 6 : maxCards);
    const auto retrieved = electronics_knowledge::retrieve(query, limit);

    juce::String result;
    result << "{\n";
    result << "  \"ok\": true,\n";
    result << "  \"schemaVersion\": 1,\n";
    result << "  \"kind\": \"djehuti_assistant_tool_result\",\n";
    result << "  \"tool\": \"cookbook_lookup\",\n";
    result << "  \"displayTool\": \"cookbook.lookup\",\n";
    result << "  \"query\": " << jsonQuote(query.trim()) << ",\n";
    result << "  \"knowledgeRoot\": " << jsonQuote(electronics_knowledge::getKnowledgeRoot().getFullPathName()) << ",\n";
    result << "  \"projectMemoryCards\": " << jsonQuote(electronics_knowledge::getProjectMemoryCardsFile().getFullPathName()) << ",\n";
    result << "  \"capabilityGapRegistry\": " << jsonQuote(electronics_knowledge::getCapabilityGapsFile().getFullPathName()) << ",\n";
    result << "  \"cards\": [\n";

    for (size_t index = 0; index < retrieved.cards.size(); ++index)
    {
        const auto& card = retrieved.cards[index];
        result << "    {\n";
        result << "      \"id\": " << jsonQuote(card.id) << ",\n";
        result << "      \"kind\": " << jsonQuote(card.kind) << ",\n";
        result << "      \"title\": " << jsonQuote(card.title) << ",\n";
        result << "      \"source\": " << jsonQuote(card.source) << ",\n";
        result << "      \"priority\": " << card.priority << ",\n";
        result << "      \"tokens\": [";
        for (int tokenIndex = 0; tokenIndex < card.tokens.size(); ++tokenIndex)
        {
            if (tokenIndex > 0)
                result << ", ";
            result << jsonQuote(card.tokens[tokenIndex]);
        }
        result << "],\n";
        result << "      \"text\": " << jsonQuote(card.text);
        if (card.rawJson.isNotEmpty())
            result << ",\n      \"entry\": " << card.rawJson << "\n";
        else
            result << "\n";
        result << "    }";
        if (index + 1 < retrieved.cards.size())
            result << ",";
        result << "\n";
    }

    result << "  ]\n";
    result << "}\n";
    return result;
}

juce::String ElectronicsWorkbench::cookbookCoverageTool() const
{
    const auto taxonomyFile = electronics_knowledge::getKnowledgeRoot().getChildFile("COOKBOOK_TAXONOMY.json");
    const auto parsedTaxonomy = juce::JSON::parse(taxonomyFile.loadFileAsString());
    if (!parsedTaxonomy.isObject())
    {
        return "{ \"ok\": false, \"tool\": \"cookbook_coverage\", \"displayTool\": \"cookbook.coverage\", \"error\": "
            + jsonQuote("Could not read cookbook taxonomy: " + taxonomyFile.getFullPathName()) + " }";
    }

    juce::StringArray requiredCategories;
    if (auto* categories = parsedTaxonomy.getProperty("categories", {}).getArray())
        for (const auto& category : *categories)
            requiredCategories.add(category.toString());
    requiredCategories.removeEmptyStrings();

    std::map<juce::String, juce::StringArray> entriesByCategory;
    int cookbookEntryCount = 0;
    for (const auto& card : electronics_knowledge::allCards())
    {
        if (!card.kind.startsWithIgnoreCase("cookbook"))
            continue;

        ++cookbookEntryCount;
        const auto parsedCard = juce::JSON::parse(card.rawJson);
        auto category = parsedCard.getProperty("category", {}).toString().trim();
        if (category.isEmpty())
            category = "Uncategorized";
        entriesByCategory[category].add(card.id);
    }

    juce::StringArray missingCategories;
    juce::StringArray coveredCategories;
    for (const auto& category : requiredCategories)
    {
        if (entriesByCategory[category].isEmpty())
            missingCategories.add(category);
        else
            coveredCategories.add(category);
    }

    const auto requiredCount = requiredCategories.size();
    const auto coveredCount = coveredCategories.size();
    const auto coverageRatio = requiredCount == 0 ? 0.0 : (double)coveredCount / (double)requiredCount;

    auto appendStringArray = [](juce::String& out, const juce::StringArray& values) {
        out << "[";
        for (int index = 0; index < values.size(); ++index)
        {
            if (index > 0)
                out << ", ";
            out << jsonQuote(values[index]);
        }
        out << "]";
    };

    juce::String result;
    result << "{\n";
    result << "  \"ok\": true,\n";
    result << "  \"schemaVersion\": 1,\n";
    result << "  \"kind\": \"djehuti_assistant_tool_result\",\n";
    result << "  \"tool\": \"cookbook_coverage\",\n";
    result << "  \"displayTool\": \"cookbook.coverage\",\n";
    result << "  \"taxonomyFile\": " << jsonQuote(taxonomyFile.getFullPathName()) << ",\n";
    result << "  \"cookbookEntryCount\": " << cookbookEntryCount << ",\n";
    result << "  \"requiredCategoryCount\": " << requiredCount << ",\n";
    result << "  \"coveredCategoryCount\": " << coveredCount << ",\n";
    result << "  \"missingCategoryCount\": " << missingCategories.size() << ",\n";
    result << "  \"coverageRatio\": " << numberText(coverageRatio, 6) << ",\n";
    result << "  \"coveredCategories\": ";
    appendStringArray(result, coveredCategories);
    result << ",\n";
    result << "  \"missingCategories\": ";
    appendStringArray(result, missingCategories);
    result << ",\n";
    result << "  \"categories\": [\n";

    for (int index = 0; index < requiredCategories.size(); ++index)
    {
        const auto category = requiredCategories[index];
        const auto ids = entriesByCategory[category];
        result << "    {\n";
        result << "      \"name\": " << jsonQuote(category) << ",\n";
        result << "      \"entryCount\": " << ids.size() << ",\n";
        result << "      \"entryIds\": ";
        appendStringArray(result, ids);
        result << "\n";
        result << "    }";
        if (index + 1 < requiredCategories.size())
            result << ",";
        result << "\n";
    }

    result << "  ]\n";
    result << "}\n";
    return result;
}

juce::String ElectronicsWorkbench::cookbookValidateTool() const
{
    const auto taxonomyFile = electronics_knowledge::getKnowledgeRoot().getChildFile("COOKBOOK_TAXONOMY.json");
    const auto parsedTaxonomy = juce::JSON::parse(taxonomyFile.loadFileAsString());
    if (!parsedTaxonomy.isObject())
    {
        return "{ \"ok\": false, \"tool\": \"cookbook_validate\", \"displayTool\": \"cookbook.validate\", \"error\": "
            + jsonQuote("Could not read cookbook taxonomy: " + taxonomyFile.getFullPathName()) + " }";
    }

    std::set<std::string> taxonomyCategories;
    if (auto* categories = parsedTaxonomy.getProperty("categories", {}).getArray())
        for (const auto& category : *categories)
            taxonomyCategories.insert(category.toString().toStdString());

    juce::StringArray errors;
    juce::StringArray warnings;
    std::set<std::string> seenIds;
    int cookbookEntryCount = 0;

    const juce::StringArray requiredFields {
        "category",
        "subcategory",
        "purpose",
        "whenToUse",
        "whenNotToUse",
        "requiredComponents",
        "designProcedure",
        "toolRecipe",
        "analysisRecipe",
        "validationCriteria",
        "failureModes",
        "iterationRules",
        "capabilityGaps",
        "provenance",
        "text"
    };

    const juce::StringArray recommendedFields {
        "parameters",
        "relatedEntries"
    };

    auto hasUsefulProperty = [](const juce::var& object, const juce::String& name) {
        const auto value = object.getProperty(name, {});
        if (value.isVoid())
            return false;
        if (value.isString())
            return value.toString().trim().isNotEmpty();
        if (auto* array = value.getArray())
            return !array->isEmpty();
        if (auto* dyn = value.getDynamicObject())
            return dyn->getProperties().size() > 0;
        return true;
    };

    for (const auto& card : electronics_knowledge::allCards())
    {
        if (!card.kind.startsWithIgnoreCase("cookbook"))
            continue;

        ++cookbookEntryCount;
        const auto id = card.id.isNotEmpty() ? card.id : juce::String("<missing id>");
        if (!seenIds.insert(id.toStdString()).second)
            errors.add(id + ": duplicate cookbook id.");

        const auto parsedCard = juce::JSON::parse(card.rawJson);
        if (!parsedCard.isObject())
        {
            errors.add(id + ": raw cookbook JSON is malformed.");
            continue;
        }

        const auto category = parsedCard.getProperty("category", {}).toString().trim();
        if (category.isEmpty())
        {
            errors.add(id + ": missing category.");
        }
        else if (taxonomyCategories.count(category.toStdString()) == 0)
        {
            errors.add(id + ": category is not in COOKBOOK_TAXONOMY.json: " + category);
        }

        for (const auto& field : requiredFields)
            if (!hasUsefulProperty(parsedCard, field))
                errors.add(id + ": missing required cookbook field `" + field + "`.");

        for (const auto& field : recommendedFields)
            if (!hasUsefulProperty(parsedCard, field))
                warnings.add(id + ": missing recommended cookbook field `" + field + "`.");

        const auto text = parsedCard.getProperty("text", {}).toString();
        if (text.length() < 120)
            warnings.add(id + ": summary text is short for retrieval.");
    }

    if (cookbookEntryCount == 0)
        errors.add("No cookbook entries were found.");

    auto appendStringArray = [](juce::String& out, const juce::StringArray& values) {
        out << "[";
        for (int index = 0; index < values.size(); ++index)
        {
            if (index > 0)
                out << ", ";
            out << jsonQuote(values[index]);
        }
        out << "]";
    };

    juce::String result;
    result << "{\n";
    result << "  \"ok\": true,\n";
    result << "  \"schemaVersion\": 1,\n";
    result << "  \"kind\": \"djehuti_assistant_tool_result\",\n";
    result << "  \"tool\": \"cookbook_validate\",\n";
    result << "  \"displayTool\": \"cookbook.validate\",\n";
    result << "  \"status\": " << jsonQuote(errors.isEmpty() ? "passed" : "failed") << ",\n";
    result << "  \"cookbookEntryCount\": " << cookbookEntryCount << ",\n";
    result << "  \"errorCount\": " << errors.size() << ",\n";
    result << "  \"warningCount\": " << warnings.size() << ",\n";
    result << "  \"errors\": ";
    appendStringArray(result, errors);
    result << ",\n";
    result << "  \"warnings\": ";
    appendStringArray(result, warnings);
    result << "\n";
    result << "}\n";
    return result;
}

juce::String ElectronicsWorkbench::cookbookAcceptanceGoalsTool(const juce::String& domainOrId) const
{
    const auto acceptanceFile = electronics_knowledge::getKnowledgeRoot().getChildFile("COOKBOOK_ACCEPTANCE_GOALS.json");
    const auto parsedAcceptance = juce::JSON::parse(acceptanceFile.loadFileAsString());
    if (!parsedAcceptance.isObject())
    {
        return "{ \"ok\": false, \"tool\": \"cookbook_acceptance_goals\", \"displayTool\": \"cookbook.acceptance_goals\", \"error\": "
            + jsonQuote("Could not read cookbook acceptance goals: " + acceptanceFile.getFullPathName()) + " }";
    }

    const auto filter = domainOrId.trim();
    const auto requiredDomains = parsedAcceptance.getProperty("requiredDomains", {});
    const auto goals = parsedAcceptance.getProperty("goals", {});
    auto* goalArray = goals.getArray();

    if (goalArray == nullptr)
    {
        return "{ \"ok\": false, \"tool\": \"cookbook_acceptance_goals\", \"displayTool\": \"cookbook.acceptance_goals\", \"error\": "
            + jsonQuote("Acceptance goals file has no goals array: " + acceptanceFile.getFullPathName()) + " }";
    }

    auto appendArray = [](juce::String& out, const juce::var& value) {
        const auto text = juce::JSON::toString(value, true);
        out << (text.isNotEmpty() ? text : "[]");
    };

    juce::String result;
    result << "{\n";
    result << "  \"ok\": true,\n";
    result << "  \"schemaVersion\": 1,\n";
    result << "  \"kind\": \"djehuti_assistant_tool_result\",\n";
    result << "  \"tool\": \"cookbook_acceptance_goals\",\n";
    result << "  \"displayTool\": \"cookbook.acceptance_goals\",\n";
    result << "  \"acceptanceFile\": " << jsonQuote(acceptanceFile.getFullPathName()) << ",\n";
    result << "  \"filter\": " << jsonQuote(filter) << ",\n";
    result << "  \"requiredDomains\": ";
    appendArray(result, requiredDomains);
    result << ",\n";
    result << "  \"goals\": [\n";

    int matched = 0;
    for (const auto& goal : *goalArray)
    {
        const auto id = goal.getProperty("id", {}).toString();
        const auto domain = goal.getProperty("domain", {}).toString();
        const auto include = filter.isEmpty()
            || id.equalsIgnoreCase(filter)
            || domain.equalsIgnoreCase(filter);
        if (!include)
            continue;

        if (matched > 0)
            result << ",\n";
        result << juce::JSON::toString(goal, true);
        ++matched;
    }

    result << "\n  ],\n";
    result << "  \"goalCount\": " << matched << "\n";
    result << "}\n";
    return result;
}

juce::String ElectronicsWorkbench::cookbookAcceptanceSummaryTool(const juce::String& domainOrId) const
{
    const auto acceptanceRoot = generatedRunDirectory().getChildFile("acceptance");
    juce::Array<juce::File> reports;
    if (acceptanceRoot.exists())
        acceptanceRoot.findChildFiles(reports, juce::File::findFiles, true, "acceptance_report.json");
    reports.sort();

    const auto filter = domainOrId.trim();
    auto arrayCount = [](const juce::var& value) {
        if (auto* array = value.getArray())
            return array->size();
        return 0;
    };

    auto evidenceCount = [&arrayCount](const juce::var& evidence, const char* name) {
        return arrayCount(evidence.getProperty(name, {}));
    };

    auto ratioText = [](int actual, int required) {
        if (required <= 0)
            return juce::String("1");
        return numberText(juce::jlimit(0.0, 1.0, (double)actual / (double)required), 6);
    };

    juce::String result;
    result << "{\n";
    result << "  \"ok\": true,\n";
    result << "  \"schemaVersion\": 1,\n";
    result << "  \"kind\": \"djehuti_assistant_tool_result\",\n";
    result << "  \"tool\": \"cookbook_acceptance_summary\",\n";
    result << "  \"displayTool\": \"cookbook.acceptance_summary\",\n";
    result << "  \"acceptanceRoot\": " << jsonQuote(acceptanceRoot.getFullPathName()) << ",\n";
    result << "  \"filter\": " << jsonQuote(filter) << ",\n";
    result << "  \"runs\": [\n";

    int matched = 0;
    int unreadable = 0;
    for (const auto& report : reports)
    {
        const auto parsed = juce::JSON::parse(report.loadFileAsString());
        if (!parsed.isObject())
        {
            ++unreadable;
            continue;
        }

        const auto goal = parsed.getProperty("goal", {});
        const auto goalId = goal.getProperty("id", report.getParentDirectory().getFileName()).toString();
        const auto domain = goal.getProperty("domain", {}).toString();
        const auto include = filter.isEmpty()
            || goalId.equalsIgnoreCase(filter)
            || domain.equalsIgnoreCase(filter);
        if (!include)
            continue;

        const auto evidence = parsed.getProperty("evidence", {});
        const auto requiredCards = arrayCount(goal.getProperty("mustRetrieve", {}));
        const auto requiredTools = arrayCount(goal.getProperty("requiredToolEvidence", {}));
        const auto requiredCriteria = arrayCount(goal.getProperty("passCriteria", {}));
        const auto criteriaCount = arrayCount(parsed.getProperty("criteriaResults", {}));
        const auto retrievedCards = evidenceCount(evidence, "retrievedCards");
        const auto toolCalls = evidenceCount(evidence, "toolCalls");
        const auto artifacts = evidenceCount(evidence, "artifacts");
        const auto capabilityGaps = evidenceCount(evidence, "capabilityGaps");
        const auto notes = evidenceCount(evidence, "notes");
        const auto retrievalRatio = requiredCards <= 0 ? 1.0 : juce::jlimit(0.0, 1.0, (double)retrievedCards / (double)requiredCards);
        const auto toolRatio = requiredTools <= 0 ? 1.0 : juce::jlimit(0.0, 1.0, (double)toolCalls / (double)requiredTools);
        const auto criteriaRatio = requiredCriteria <= 0 ? 1.0 : juce::jlimit(0.0, 1.0, (double)criteriaCount / (double)requiredCriteria);
        const auto readinessScore = (retrievalRatio + toolRatio + criteriaRatio) / 3.0;

        if (matched > 0)
            result << ",\n";
        result << "    {\n";
        result << "      \"goalId\": " << jsonQuote(goalId) << ",\n";
        result << "      \"domain\": " << jsonQuote(domain) << ",\n";
        result << "      \"status\": " << jsonQuote(parsed.getProperty("status", "unknown").toString()) << ",\n";
        result << "      \"finalDetermination\": " << jsonQuote(parsed.getProperty("finalDetermination", "unverified").toString()) << ",\n";
        result << "      \"reportPath\": " << jsonQuote(report.getFullPathName()) << ",\n";
        result << "      \"markdownReport\": " << jsonQuote(report.getSiblingFile("acceptance_report.md").getFullPathName()) << ",\n";
        result << "      \"requiredEvidenceCounts\": {\n";
        result << "        \"retrievedCards\": " << requiredCards << ",\n";
        result << "        \"toolCalls\": " << requiredTools << ",\n";
        result << "        \"criteriaResults\": " << requiredCriteria << "\n";
        result << "      },\n";
        result << "      \"evidenceCounts\": {\n";
        result << "        \"retrievedCards\": " << retrievedCards << ",\n";
        result << "        \"toolCalls\": " << toolCalls << ",\n";
        result << "        \"artifacts\": " << artifacts << ",\n";
        result << "        \"capabilityGaps\": " << capabilityGaps << ",\n";
        result << "        \"criteriaResults\": " << criteriaCount << ",\n";
        result << "        \"notes\": " << notes << "\n";
        result << "      },\n";
        result << "      \"evidenceReadiness\": {\n";
        result << "        \"score\": " << numberText(readinessScore, 6) << ",\n";
        result << "        \"retrievalRatio\": " << ratioText(retrievedCards, requiredCards) << ",\n";
        result << "        \"toolEvidenceRatio\": " << ratioText(toolCalls, requiredTools) << ",\n";
        result << "        \"criteriaRatio\": " << ratioText(criteriaCount, requiredCriteria) << ",\n";
        result << "        \"needsRetrievedCards\": " << (retrievedCards < requiredCards ? "true" : "false") << ",\n";
        result << "        \"needsToolEvidence\": " << (toolCalls < requiredTools ? "true" : "false") << ",\n";
        result << "        \"needsCriteriaResults\": " << (criteriaCount < requiredCriteria ? "true" : "false") << "\n";
        result << "      }\n";
        result << "    }";
        ++matched;
    }

    result << "\n  ],\n";
    result << "  \"runCount\": " << matched << ",\n";
    result << "  \"reportFileCount\": " << reports.size() << ",\n";
    result << "  \"unreadableReportCount\": " << unreadable << "\n";
    result << "}\n";
    return result;
}

juce::String ElectronicsWorkbench::cookbookAcceptanceStartTool(const juce::String& goalId) const
{
    const auto acceptanceFile = electronics_knowledge::getKnowledgeRoot().getChildFile("COOKBOOK_ACCEPTANCE_GOALS.json");
    const auto parsedAcceptance = juce::JSON::parse(acceptanceFile.loadFileAsString());
    auto* goals = parsedAcceptance.getProperty("goals", {}).getArray();
    if (!parsedAcceptance.isObject() || goals == nullptr)
    {
        return "{ \"ok\": false, \"tool\": \"cookbook_acceptance_start\", \"displayTool\": \"cookbook.acceptance_start\", \"error\": "
            + jsonQuote("Could not read cookbook acceptance goals: " + acceptanceFile.getFullPathName()) + " }";
    }

    juce::var selectedGoal;
    for (const auto& goal : *goals)
    {
        if (goal.getProperty("id", {}).toString().equalsIgnoreCase(goalId))
        {
            selectedGoal = goal;
            break;
        }
    }

    if (!selectedGoal.isObject())
    {
        return "{ \"ok\": false, \"tool\": \"cookbook_acceptance_start\", \"displayTool\": \"cookbook.acceptance_start\", \"error\": "
            + jsonQuote("Unknown acceptance goal id: " + goalId) + " }";
    }

    auto safeName = goalId.retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-");
    if (safeName.isEmpty())
        safeName = "acceptance_goal";

    const auto runDir = generatedRunDirectory().getChildFile("acceptance").getChildFile(safeName);
    if (!runDir.createDirectory())
    {
        return "{ \"ok\": false, \"tool\": \"cookbook_acceptance_start\", \"displayTool\": \"cookbook.acceptance_start\", \"error\": "
            + jsonQuote("Could not create acceptance run directory: " + runDir.getFullPathName()) + " }";
    }

    const auto jsonReport = runDir.getChildFile("acceptance_report.json");
    const auto markdownReport = runDir.getChildFile("acceptance_report.md");

    auto appendJsonArray = [](juce::String& out, const juce::var& value) {
        const auto text = juce::JSON::toString(value, true);
        out << (text.isNotEmpty() ? text : "[]");
    };

    auto markdownChecklist = [](const juce::var& value) {
        juce::String text;
        if (auto* array = value.getArray())
        {
            for (const auto& item : *array)
                text << "- [ ] " << item.toString() << "\n";
        }
        return text;
    };

    juce::String json;
    json << "{\n";
    json << "  \"schemaVersion\": 1,\n";
    json << "  \"kind\": \"djehuti_acceptance_run_report\",\n";
    json << "  \"status\": \"not_started\",\n";
    json << "  \"goal\": ";
    appendJsonArray(json, selectedGoal);
    json << ",\n";
    json << "  \"evidence\": {\n";
    json << "    \"retrievedCards\": [],\n";
    json << "    \"toolCalls\": [],\n";
    json << "    \"artifacts\": [],\n";
    json << "    \"capabilityGaps\": [],\n";
    json << "    \"notes\": []\n";
    json << "  },\n";
    json << "  \"criteriaResults\": [],\n";
    json << "  \"finalDetermination\": \"unverified\"\n";
    json << "}\n";

    juce::String markdown;
    markdown << "# Acceptance Run: " << selectedGoal.getProperty("id", goalId).toString() << "\n\n";
    markdown << "- Domain: `" << selectedGoal.getProperty("domain", {}).toString() << "`\n";
    markdown << "- Status: `not_started`\n";
    markdown << "- Determination: `unverified`\n\n";
    markdown << "## Prompt\n\n" << selectedGoal.getProperty("prompt", {}).toString() << "\n\n";
    markdown << "## Required Cookbook Retrieval\n\n" << markdownChecklist(selectedGoal.getProperty("mustRetrieve", {})) << "\n";
    markdown << "## Required Tool Evidence\n\n" << markdownChecklist(selectedGoal.getProperty("requiredToolEvidence", {})) << "\n";
    markdown << "## Pass Criteria\n\n" << markdownChecklist(selectedGoal.getProperty("passCriteria", {})) << "\n";
    markdown << "## Expected Capability Gaps\n\n" << markdownChecklist(selectedGoal.getProperty("expectedCapabilityGaps", {})) << "\n";
    markdown << "## Evidence Log\n\n- [ ] Record cookbook cards retrieved.\n- [ ] Record tool calls and result paths.\n- [ ] Record generated plots/data and whether they came from real tool output.\n- [ ] Record capability gaps instead of filling missing steps manually.\n";

    if (!jsonReport.replaceWithText(json))
    {
        return "{ \"ok\": false, \"tool\": \"cookbook_acceptance_start\", \"displayTool\": \"cookbook.acceptance_start\", \"error\": "
            + jsonQuote("Could not write acceptance JSON report: " + jsonReport.getFullPathName()) + " }";
    }

    if (!markdownReport.replaceWithText(markdown))
    {
        return "{ \"ok\": false, \"tool\": \"cookbook_acceptance_start\", \"displayTool\": \"cookbook.acceptance_start\", \"error\": "
            + jsonQuote("Could not write acceptance markdown report: " + markdownReport.getFullPathName()) + " }";
    }

    juce::String result;
    result << "{\n";
    result << "  \"ok\": true,\n";
    result << "  \"schemaVersion\": 1,\n";
    result << "  \"kind\": \"djehuti_assistant_tool_result\",\n";
    result << "  \"tool\": \"cookbook_acceptance_start\",\n";
    result << "  \"displayTool\": \"cookbook.acceptance_start\",\n";
    result << "  \"status\": \"started\",\n";
    result << "  \"goalId\": " << jsonQuote(goalId) << ",\n";
    result << "  \"artifactDirectory\": " << jsonQuote(runDir.getFullPathName()) << ",\n";
    result << "  \"jsonReport\": " << jsonQuote(jsonReport.getFullPathName()) << ",\n";
    result << "  \"markdownReport\": " << jsonQuote(markdownReport.getFullPathName()) << "\n";
    result << "}\n";
    return result;
}

juce::String ElectronicsWorkbench::cookbookAcceptanceRecordTool(const juce::String& reportPath,
                                                                const juce::String& evidenceType,
                                                                const juce::String& label,
                                                                const juce::String& detail,
                                                                const juce::String& pathOrValue,
                                                                const juce::String& status) const
{
    const auto report = juce::File(reportPath);
    if (!report.existsAsFile())
    {
        return "{ \"ok\": false, \"tool\": \"cookbook_acceptance_record\", \"displayTool\": \"cookbook.acceptance_record\", \"error\": "
            + jsonQuote("Acceptance report does not exist: " + reportPath) + " }";
    }

    if (!report.getFileName().equalsIgnoreCase("acceptance_report.json"))
    {
        return "{ \"ok\": false, \"tool\": \"cookbook_acceptance_record\", \"displayTool\": \"cookbook.acceptance_record\", \"error\": "
            + jsonQuote("Expected an acceptance_report.json path, got: " + report.getFileName()) + " }";
    }

    auto parsed = juce::JSON::parse(report.loadFileAsString());
    auto* root = parsed.getDynamicObject();
    if (root == nullptr)
    {
        return "{ \"ok\": false, \"tool\": \"cookbook_acceptance_record\", \"displayTool\": \"cookbook.acceptance_record\", \"error\": "
            + jsonQuote("Could not parse acceptance report: " + report.getFullPathName()) + " }";
    }

    auto normalizedType = evidenceType.trim().replaceCharacter('.', '_').replaceCharacter('-', '_').toLowerCase();
    if (normalizedType.isEmpty())
        normalizedType = "note";

    auto normalizedStatus = status.trim().toLowerCase();
    if (normalizedStatus.isEmpty())
        normalizedStatus = normalizedType == "capability_gap" ? "gap" : "observed";

    auto* item = new juce::DynamicObject();
    item->setProperty("type", normalizedType);
    item->setProperty("label", label.trim());
    item->setProperty("detail", detail.trim());
    item->setProperty("pathOrValue", pathOrValue.trim());
    item->setProperty("status", normalizedStatus);

    auto appendToArrayProperty = [](juce::DynamicObject* object, const juce::Identifier& property, const juce::var& value) {
        juce::Array<juce::var> array;
        if (auto* existing = object->getProperty(property).getArray())
            array = *existing;
        array.add(value);
        object->setProperty(property, juce::var(array));
    };

    auto evidence = root->getProperty("evidence");
    if (!evidence.isObject())
    {
        auto* evidenceObject = new juce::DynamicObject();
        evidenceObject->setProperty("retrievedCards", juce::Array<juce::var>());
        evidenceObject->setProperty("toolCalls", juce::Array<juce::var>());
        evidenceObject->setProperty("artifacts", juce::Array<juce::var>());
        evidenceObject->setProperty("capabilityGaps", juce::Array<juce::var>());
        evidenceObject->setProperty("notes", juce::Array<juce::var>());
        root->setProperty("evidence", juce::var(evidenceObject));
        evidence = root->getProperty("evidence");
    }

    auto* evidenceObject = evidence.getDynamicObject();
    juce::String targetArray = "notes";
    if (normalizedType == "retrieved_card" || normalizedType == "retrieved_cards" || normalizedType == "card")
        targetArray = "retrievedCards";
    else if (normalizedType == "tool_call" || normalizedType == "tool_calls" || normalizedType == "tool")
        targetArray = "toolCalls";
    else if (normalizedType == "artifact" || normalizedType == "artifacts")
        targetArray = "artifacts";
    else if (normalizedType == "capability_gap" || normalizedType == "gap")
        targetArray = "capabilityGaps";
    else if (normalizedType == "criterion" || normalizedType == "criteria" || normalizedType == "criteria_result")
        targetArray = "criteriaResults";

    if (targetArray == "criteriaResults")
        appendToArrayProperty(root, "criteriaResults", juce::var(item));
    else if (evidenceObject != nullptr)
        appendToArrayProperty(evidenceObject, targetArray, juce::var(item));

    if (root->getProperty("status").toString().isEmpty()
        || root->getProperty("status").toString().equalsIgnoreCase("not_started"))
        root->setProperty("status", "in_progress");

    if (normalizedType == "capability_gap" || normalizedStatus == "gap")
        root->setProperty("finalDetermination", "capability_gap");
    else if (normalizedStatus == "failed")
        root->setProperty("finalDetermination", "failed");

    if (!report.replaceWithText(juce::JSON::toString(parsed, true)))
    {
        return "{ \"ok\": false, \"tool\": \"cookbook_acceptance_record\", \"displayTool\": \"cookbook.acceptance_record\", \"error\": "
            + jsonQuote("Could not update acceptance report: " + report.getFullPathName()) + " }";
    }

    const auto markdownReport = report.getSiblingFile("acceptance_report.md");
    bool markdownUpdated = false;
    if (markdownReport.existsAsFile())
    {
        juce::String line;
        line << "\n- [`" << normalizedStatus << "`] `" << normalizedType << "`: " << label.trim();
        if (detail.trim().isNotEmpty())
            line << " - " << detail.trim();
        if (pathOrValue.trim().isNotEmpty())
            line << " (" << pathOrValue.trim() << ")";
        line << "\n";
        markdownUpdated = markdownReport.appendText(line);
    }

    juce::String result;
    result << "{\n";
    result << "  \"ok\": true,\n";
    result << "  \"schemaVersion\": 1,\n";
    result << "  \"kind\": \"djehuti_assistant_tool_result\",\n";
    result << "  \"tool\": \"cookbook_acceptance_record\",\n";
    result << "  \"displayTool\": \"cookbook.acceptance_record\",\n";
    result << "  \"reportPath\": " << jsonQuote(report.getFullPathName()) << ",\n";
    result << "  \"markdownReport\": " << jsonQuote(markdownReport.getFullPathName()) << ",\n";
    result << "  \"markdownUpdated\": " << (markdownUpdated ? "true" : "false") << ",\n";
    result << "  \"evidenceType\": " << jsonQuote(normalizedType) << ",\n";
    result << "  \"targetArray\": " << jsonQuote(targetArray) << ",\n";
    result << "  \"status\": " << jsonQuote(normalizedStatus) << "\n";
    result << "}\n";
    return result;
}

juce::String ElectronicsWorkbench::capabilityGapRecordTool(const juce::String& category,
                                                           const juce::String& description,
                                                           const juce::String& neededCapability,
                                                           const juce::String& evidence,
                                                           const juce::String& source,
                                                           const juce::String& status) const
{
    const auto trimmedDescription = description.trim();
    const auto trimmedCapability = neededCapability.trim();
    if (trimmedDescription.isEmpty() || trimmedCapability.isEmpty())
    {
        return "{ \"ok\": false, \"tool\": \"capability_gap_record\", \"displayTool\": \"capability_gap.record\", \"error\": \"description and neededCapability are required.\" }";
    }

    const auto memoryDir = electronics_knowledge::getCapabilityGapsFile().getParentDirectory();
    if (!memoryDir.createDirectory())
    {
        return "{ \"ok\": false, \"tool\": \"capability_gap_record\", \"displayTool\": \"capability_gap.record\", \"error\": "
            + jsonQuote("Could not create project memory directory: " + memoryDir.getFullPathName()) + " }";
    }

    const auto registry = memoryDir.getChildFile("CAPABILITY_GAPS.jsonl");
    auto normalizedCategory = category.trim().replaceCharacter(' ', '_').replaceCharacter('-', '_').toLowerCase();
    if (normalizedCategory.isEmpty())
        normalizedCategory = "unspecified";

    auto normalizedStatus = status.trim().replaceCharacter(' ', '_').replaceCharacter('-', '_').toLowerCase();
    if (normalizedStatus.isEmpty())
        normalizedStatus = "open";

    const auto now = juce::Time::getCurrentTime().toISO8601(true);
    const auto seed = trimmedDescription + "|" + trimmedCapability + "|" + source.trim();
    const auto id = "gap."
        + juce::String::toHexString((int)std::abs((int)seed.hashCode())).paddedLeft('0', 8)
        + "."
        + juce::String(juce::Time::getCurrentTime().toMilliseconds());

    auto* gap = new juce::DynamicObject();
    gap->setProperty("schemaVersion", 1);
    gap->setProperty("kind", "djehuti_capability_gap");
    gap->setProperty("id", id);
    gap->setProperty("createdAt", now);
    gap->setProperty("status", normalizedStatus);
    gap->setProperty("category", normalizedCategory);
    gap->setProperty("description", trimmedDescription);
    gap->setProperty("neededCapability", trimmedCapability);
    gap->setProperty("evidence", evidence.trim());
    gap->setProperty("source", source.trim());

    const auto line = juce::JSON::toString(juce::var(gap), false).trim() + "\n";
    if (!registry.appendText(line))
    {
        return "{ \"ok\": false, \"tool\": \"capability_gap_record\", \"displayTool\": \"capability_gap.record\", \"error\": "
            + jsonQuote("Could not append capability gap registry: " + registry.getFullPathName()) + " }";
    }

    juce::String result;
    result << "{\n";
    result << "  \"ok\": true,\n";
    result << "  \"schemaVersion\": 1,\n";
    result << "  \"kind\": \"djehuti_assistant_tool_result\",\n";
    result << "  \"tool\": \"capability_gap_record\",\n";
    result << "  \"displayTool\": \"capability_gap.record\",\n";
    result << "  \"gapId\": " << jsonQuote(id) << ",\n";
    result << "  \"status\": " << jsonQuote(normalizedStatus) << ",\n";
    result << "  \"category\": " << jsonQuote(normalizedCategory) << ",\n";
    result << "  \"gapRegistry\": " << jsonQuote(registry.getFullPathName()) << "\n";
    result << "}\n";
    return result;
}

void ElectronicsWorkbench::exportAssistantToolManifest()
{
    const auto runDir = generatedRunDirectory();
    if (!runDir.createDirectory())
    {
        appendLog("Could not create run directory: " + runDir.getFullPathName());
        return;
    }

    const auto file = runDir.getChildFile("assistant_tools.json");
    if (!file.replaceWithText(buildAssistantToolManifestJson()))
    {
        appendLog("Could not write assistant tool manifest: " + file.getFullPathName());
        return;
    }

    appendLog("Exported assistant tool manifest to " + file.getFullPathName());
}

juce::String ElectronicsWorkbench::runElectricalRuleCheckTool()
{
    if (getErcReport == nullptr)
    {
        return "{ \"ok\": false, \"tool\": \"circuit_run_erc\", \"displayTool\": \"circuit.run_erc\", \"error\": \"No ERC engine is available.\" }";
    }

    const auto runDir = generatedRunDirectory();
    if (!runDir.createDirectory())
    {
        return "{ \"ok\": false, \"tool\": \"circuit_run_erc\", \"displayTool\": \"circuit.run_erc\", \"error\": "
            + jsonQuote("Could not create run directory: " + runDir.getFullPathName()) + " }";
    }

    const auto report = getErcReport();
    const auto reportFile = runDir.getChildFile("erc_report.md");
    if (!reportFile.replaceWithText(report))
    {
        return "{ \"ok\": false, \"tool\": \"circuit_run_erc\", \"displayTool\": \"circuit.run_erc\", \"error\": "
            + jsonQuote("Could not write ERC report: " + reportFile.getFullPathName()) + " }";
    }
    const auto manifestFile = runDir.getChildFile("assistant_tools.json");
    manifestFile.replaceWithText(buildAssistantToolManifestJson());

    const auto errors = report.fromFirstOccurrenceOf("- Errors: ", false, false)
                             .upToFirstOccurrenceOf("\n", false, false)
                             .trim();
    const auto warnings = report.fromFirstOccurrenceOf("- Warnings: ", false, false)
                               .upToFirstOccurrenceOf("\n", false, false)
                               .trim();
    const auto resultFile = runDir.getChildFile("erc_tool_result.json");
    juce::String result;
    result << "{\n";
    result << "  \"ok\": true,\n";
    result << "  \"schemaVersion\": 1,\n";
    result << "  \"kind\": \"djehuti_assistant_tool_result\",\n";
    result << "  \"tool\": \"circuit_run_erc\",\n";
    result << "  \"displayTool\": \"circuit.run_erc\",\n";
    result << "  \"status\": " << jsonQuote(errors == "0" ? "passed" : "failed") << ",\n";
    result << "  \"errors\": " << errors << ",\n";
    result << "  \"warnings\": " << warnings << ",\n";
    result << "  \"reportPath\": " << jsonQuote(reportFile.getFullPathName()) << "\n";
    result << "}\n";
    resultFile.replaceWithText(result);
    return result;
}

void ElectronicsWorkbench::runElectricalRuleCheck()
{
    const auto result = runElectricalRuleCheckTool();
    const auto parsed = juce::JSON::parse(result);
    if (!parsed.isObject() || !(bool)parsed.getProperty("ok", false))
    {
        appendLog("ERC failed: " + parsed.getProperty("error", result).toString());
        return;
    }

    appendLog("ERC complete: "
              + parsed.getProperty("errors", 0).toString()
              + " error(s), "
              + parsed.getProperty("warnings", 0).toString()
              + " warning(s). Report: "
              + parsed.getProperty("reportPath", {}).toString());
}

void ElectronicsWorkbench::openInstrumentWindow(juce::String refdes, juce::String symbolId)
{
    auto* canvas = dynamic_cast<SchematicCanvasPanel*>(schematicView.getComponent());
    if (canvas == nullptr)
        return;
    const auto instrumentName = parts::displayName(symbolId);
    const auto title = refdes + " " + instrumentName;
    for (auto* existing : floatingInstrumentWindows)
        if (existing->getName() == title)
        {
            existing->setVisible(true);
            existing->toFront(true);
            return;
        }

    juce::Component* view = nullptr;
    if (symbolId == "oscilloscope_2ch") view = new ScopeView(canvas, refdes);
    else if (symbolId == "digital_multimeter") view = new MeterView(canvas, refdes);
    else if (symbolId == "bode_analyzer") view = new BodeView(canvas, refdes);
    if (view == nullptr)
        return;

    auto* window = new FloatingInstrumentWindow(title);
    window->setContentOwned(view, true);
    window->centreWithSize(view->getWidth(), view->getHeight());
    window->setVisible(true);
    window->toFront(true);
    floatingInstrumentWindows.add(window);
    appendLog("Opened " + title + ".");
}

juce::String ElectronicsWorkbench::exportCircuitArtifactsTool()
{
    if (getCircuitJson == nullptr || getXyceNetlist == nullptr)
    {
        return "{ \"ok\": false, \"tool\": \"simulation_export_artifacts\", \"displayTool\": \"simulation.export_artifacts\", \"error\": \"No schematic exporter is available.\" }";
    }

    const auto runDir = generatedRunDirectory();
    if (!runDir.createDirectory())
    {
        return "{ \"ok\": false, \"tool\": \"simulation_export_artifacts\", \"displayTool\": \"simulation.export_artifacts\", \"error\": "
            + jsonQuote("Could not create run directory: " + runDir.getFullPathName()) + " }";
    }

    const auto circuitFile = runDir.getChildFile("circuit.json");
    const auto netlistFile = runDir.getChildFile("generated.cir");
    const auto circuitJson = getCircuitJson();
    const auto netlist = getXyceNetlist();

    if (!circuitFile.replaceWithText(circuitJson))
    {
        return "{ \"ok\": false, \"tool\": \"simulation_export_artifacts\", \"displayTool\": \"simulation.export_artifacts\", \"error\": "
            + jsonQuote("Could not write circuit JSON: " + circuitFile.getFullPathName()) + " }";
    }
    if (!netlistFile.replaceWithText(netlist))
    {
        return "{ \"ok\": false, \"tool\": \"simulation_export_artifacts\", \"displayTool\": \"simulation.export_artifacts\", \"error\": "
            + jsonQuote("Could not write Xyce netlist: " + netlistFile.getFullPathName()) + " }";
    }
    const auto manifestFile = runDir.getChildFile("assistant_tools.json");
    manifestFile.replaceWithText(buildAssistantToolManifestJson());

    juce::String result;
    result << "{\n";
    result << "  \"ok\": true,\n";
    result << "  \"schemaVersion\": 1,\n";
    result << "  \"kind\": \"djehuti_assistant_tool_result\",\n";
    result << "  \"tool\": \"simulation_export_artifacts\",\n";
    result << "  \"displayTool\": \"simulation.export_artifacts\",\n";
    result << "  \"status\": \"exported\",\n";
    result << "  \"artifactDirectory\": " << jsonQuote(runDir.getFullPathName()) << ",\n";
    result << "  \"circuitJson\": " << jsonQuote(circuitFile.getFullPathName()) << ",\n";
    result << "  \"xyceNetlist\": " << jsonQuote(netlistFile.getFullPathName()) << ",\n";
    result << "  \"toolManifest\": " << jsonQuote(manifestFile.getFullPathName()) << "\n";
    result << "}\n";
    return result;
}

juce::String ElectronicsWorkbench::exportFrustRealtimePreviewTool()
{
    if (getCircuitJson == nullptr)
        return "{ \"ok\": false, \"tool\": \"frust_realtime_preview_export\", \"displayTool\": \"frust.realtime_preview_export\", \"error\": \"No schematic exporter is available.\" }";

    const auto runDir = generatedRunDirectory().getChildFile("frust_realtime_preview");
    if (!runDir.createDirectory())
    {
        return "{ \"ok\": false, \"tool\": \"frust_realtime_preview_export\", \"displayTool\": \"frust.realtime_preview_export\", \"error\": "
            + jsonQuote("Could not create preview directory: " + runDir.getFullPathName()) + " }";
    }

    const auto circuitJson = getCircuitJson();
    const auto parsed = juce::JSON::parse(circuitJson);
    const auto* root = parsed.getDynamicObject();
    const auto* components = root != nullptr ? root->getProperty("components").getArray() : nullptr;
    if (components == nullptr)
        return "{ \"ok\": false, \"tool\": \"frust_realtime_preview_export\", \"displayTool\": \"frust.realtime_preview_export\", \"error\": \"Circuit JSON has no components array.\" }";

    auto frustIdent = [](juce::String text) {
        juce::String out;
        for (auto ch : text)
        {
            if (juce::CharacterFunctions::isLetterOrDigit(ch))
                out << juce::String::charToString(ch).toLowerCase();
            else
                out << "_";
        }
        while (out.contains("__"))
            out = out.replace("__", "_");
        out = out.trimCharactersAtStart("_").trimCharactersAtEnd("_");
        if (out.isEmpty() || juce::CharacterFunctions::isDigit(out[0]))
            out = "p_" + out;
        return out;
    };

    auto isRealtimeSymbol = [](const juce::String& symbol) {
        return symbol == "resistor" || symbol == "potentiometer" || symbol == "capacitor"
            || symbol == "capacitor_polarized" || symbol == "variable_capacitor"
            || symbol == "diode" || symbol == "led" || symbol == "schottky_diode"
            || symbol == "opamp_generic" || symbol == "opamp_741" || symbol == "comparator_generic" || symbol == "comparator_lm311" || symbol == "npn" || symbol == "pnp"
            || symbol == "nmos" || symbol == "pmos" || symbol == "njfet" || symbol == "pjfet"
            || symbol == "signal_source" || symbol == "ac_voltage_source" || symbol == "voltage_source"
            || symbol == "sub_block" || symbol == "block_port" || symbol == "ground" || symbol == "power_port"
            || symbol == "net_label" || symbol == "annotation_text";
    };

    juce::StringArray parameters;
    juce::StringArray parameterLabels;
    juce::StringArray supported;
    juce::StringArray unsupported;
    auto addParameter = [&](const juce::String& rawName, const juce::String& label) {
        const auto id = frustIdent(rawName);
        if (!parameters.contains(id))
        {
            parameters.add(id);
            parameterLabels.add(label.isNotEmpty() ? label : rawName);
        }
    };
    for (const auto& entry : *components)
    {
        const auto* c = entry.getDynamicObject();
        if (c == nullptr)
            continue;
        const auto id = c->getProperty("id").toString();
        const auto symbol = c->getProperty("symbol").toString();
        if (isRealtimeSymbol(symbol)) supported.add(id + ":" + symbol);
        else unsupported.add(id + ":" + symbol);

        if (const auto* params = c->getProperty("parameters").getDynamicObject())
        {
            for (const auto& property : params->getProperties())
            {
                const auto key = property.name.toString();
                if (!key.startsWith("paramTarget."))
                    addParameter(id + "_" + key, id + " " + key);
            }
        }
        if (symbol == "potentiometer")
            addParameter(id + "_position", id + " Wiper");
        const auto value = c->getProperty("value").toString();
        if (value.isNotEmpty() && (symbol == "sub_block" || symbol == "potentiometer"))
            addParameter(id + "_value", id + " Value");
    }

    juce::String frust;
    frust << "// Generated by Djehuti Electronics Lab Compile Preview.\n";
    frust << "// Model package source for the Djehuti generic VST3 runtime.\n";
    frust << "// This is a realtime-audio lowering target, not a SPICE validation netlist.\n\n";
    frust << "use frust_linalg::audio;\n\n";
    frust << "struct DjehutiModelParams {\n";
    if (parameters.isEmpty())
        frust << "    gain: f32\n";
    else
        for (int i = 0; i < parameters.size(); ++i)
            frust << "    " << parameters[i] << ": f32" << (i + 1 < parameters.size() ? "," : "") << "\n";
    frust << "}\n\n";
    frust << "pub fn process(input: audio::AudioBlock256f, params: DjehutiModelParams) -> audio::AudioBlock256f = {\n";
    frust << "    let mut y: audio::AudioBlock256f = input;\n";
    frust << "    // TODO: generated circuit solve/DSP graph goes here. Current export establishes\n";
    frust << "    // the Frust ABI, live parameter names, and model package contract.\n";
    frust << "    y\n";
    frust << "}\n";

    juce::String parametersJson;
    parametersJson << "{\n";
    parametersJson << "  \"schemaVersion\": 1,\n";
    parametersJson << "  \"kind\": \"djehuti_model_parameters\",\n";
    parametersJson << "  \"fixedSlotStrategy\": \"Expose stable slots P01..P128 to the VST host; map visible labels and ranges from this file.\",\n";
    parametersJson << "  \"parameters\": [\n";
    for (int i = 0; i < parameters.size(); ++i)
    {
        const auto isWiper = parameters[i].contains("position") || parameterLabels[i].containsIgnoreCase("wiper");
        parametersJson << "    { \"slot\": \"P" << juce::String(i + 1).paddedLeft('0', 2)
                       << "\", \"id\": " << jsonQuote(parameters[i])
                       << ", \"label\": " << jsonQuote(parameterLabels[i])
                       << ", \"default\": " << (isWiper ? "0.5" : "1.0")
                       << ", \"min\": 0.0, \"max\": " << (isWiper ? "1.0" : "1.0")
                       << ", \"unit\": " << jsonQuote(isWiper ? "fraction" : "normalized")
                       << ", \"taper\": " << jsonQuote(isWiper ? "linear" : "linear") << " }";
        parametersJson << (i + 1 < parameters.size() ? "," : "") << "\n";
    }
    parametersJson << "  ]\n";
    parametersJson << "}\n";

    juce::String uiJson;
    uiJson << "{\n";
    uiJson << "  \"schemaVersion\": 1,\n";
    uiJson << "  \"kind\": \"djehuti_model_ui\",\n";
    uiJson << "  \"title\": \"Djehuti Circuit Model\",\n";
    uiJson << "  \"layout\": \"generated_grid\",\n";
    uiJson << "  \"controls\": [\n";
    for (int i = 0; i < parameters.size(); ++i)
    {
        const auto isWiper = parameters[i].contains("position") || parameterLabels[i].containsIgnoreCase("wiper");
        uiJson << "    { \"param\": " << jsonQuote(parameters[i])
               << ", \"slot\": \"P" << juce::String(i + 1).paddedLeft('0', 2)
               << "\", \"label\": " << jsonQuote(parameterLabels[i])
               << ", \"kind\": " << jsonQuote(isWiper ? "knob" : "slider") << " }";
        uiJson << (i + 1 < parameters.size() ? "," : "") << "\n";
    }
    uiJson << "  ]\n";
    uiJson << "}\n";

    juce::String uiFrust;
    uiFrust << "// Generated model-owned UI source for Djehuti Model Player VST3.\n";
    uiFrust << "// Runs on the plugin editor/UI thread, not the realtime audio thread.\n\n";
    uiFrust << "struct UiDescription {\n";
    uiFrust << "    title: string,\n";
    uiFrust << "    control_count: i64\n";
    uiFrust << "}\n\n";
    uiFrust << "struct UiEvent {\n";
    uiFrust << "    control_id: string,\n";
    uiFrust << "    value: f32\n";
    uiFrust << "}\n\n";
    uiFrust << "struct UiEventResult {\n";
    uiFrust << "    parameter_id: string,\n";
    uiFrust << "    value: f32\n";
    uiFrust << "}\n\n";
    uiFrust << "pub fn describe_ui() -> UiDescription = {\n";
    uiFrust << "    UiDescription { title: \"Djehuti Circuit Model\", control_count: " << parameters.size() << " }\n";
    uiFrust << "}\n\n";
    uiFrust << "pub fn render_ui_json() -> string = {\n";
    uiFrust << "    // The generic VST may use ui.json directly or ask this UI module\n";
    uiFrust << "    // for a runtime-generated layout when the model changes mode.\n";
    uiFrust << "    \"ui.json\"\n";
    uiFrust << "}\n\n";
    uiFrust << "pub fn handle_ui_event(event: UiEvent) -> UiEventResult = {\n";
    uiFrust << "    UiEventResult { parameter_id: event.control_id, value: event.value }\n";
    uiFrust << "}\n\n";
    uiFrust << "pub fn sync_parameter(parameter_id: string, value: f32) -> UiEventResult = {\n";
    uiFrust << "    UiEventResult { parameter_id: parameter_id, value: value }\n";
    uiFrust << "}\n";

    juce::String il;
    il << "{\n";
    il << "  \"schemaVersion\": 1,\n";
    il << "  \"kind\": \"djehuti_frust_il_placeholder\",\n";
    il << "  \"entryPoint\": \"process\",\n";
    il << "  \"blockSize\": 256,\n";
    il << "  \"channels\": { \"inputs\": 1, \"outputs\": 1 },\n";
    il << "  \"source\": \"model.fr\",\n";
    il << "  \"status\": \"source_ready_il_lowering_pending\"\n";
    il << "}\n";

    juce::String uiIl;
    uiIl << "{\n";
    uiIl << "  \"schemaVersion\": 1,\n";
    uiIl << "  \"kind\": \"djehuti_frust_ui_il_placeholder\",\n";
    uiIl << "  \"source\": \"ui.fr\",\n";
    uiIl << "  \"thread\": \"ui\",\n";
    uiIl << "  \"entryPoints\": [\"describe_ui\", \"render_ui_json\", \"handle_ui_event\", \"sync_parameter\"],\n";
    uiIl << "  \"status\": \"source_ready_il_lowering_pending\"\n";
    uiIl << "}\n";

    juce::String manifest;
    manifest << "{\n";
    manifest << "  \"schemaVersion\": 1,\n";
    manifest << "  \"kind\": \"djehuti_audio_model_package\",\n";
    manifest << "  \"packageName\": \"Djehuti Circuit Model\",\n";
    manifest << "  \"status\": " << jsonQuote(unsupported.isEmpty() ? "ready_for_lowering" : "partial") << ",\n";
    manifest << "  \"runtime\": \"Djehuti Model Player VST3\",\n";
    manifest << "  \"frustSource\": \"model.fr\",\n";
    manifest << "  \"frustIl\": \"model.frust-il\",\n";
    manifest << "  \"uiSource\": \"ui.fr\",\n";
    manifest << "  \"uiIl\": \"ui.frust-il\",\n";
    manifest << "  \"parameters\": \"parameters.json\",\n";
    manifest << "  \"ui\": \"ui.json\",\n";
    manifest << "  \"sourceCircuit\": \"circuit.json\",\n";
    manifest << "  \"audio\": { \"inputs\": 1, \"outputs\": 1, \"blockSize\": 256 },\n";
    manifest << "  \"uiRuntime\": {\n";
    manifest << "    \"thread\": \"ui\",\n";
    manifest << "    \"modelOwned\": true,\n";
    manifest << "    \"entryPoints\": [\"describe_ui\", \"render_ui_json\", \"handle_ui_event\", \"sync_parameter\"],\n";
    manifest << "    \"audioThreadAccess\": false\n";
    manifest << "  },\n";
    manifest << "  \"supportedComponents\": [";
    for (int i = 0; i < supported.size(); ++i)
        manifest << (i == 0 ? "" : ", ") << jsonQuote(supported[i]);
    manifest << "],\n";
    manifest << "  \"unsupportedComponents\": [";
    for (int i = 0; i < unsupported.size(); ++i)
        manifest << (i == 0 ? "" : ", ") << jsonQuote(unsupported[i]);
    manifest << "],\n";
    manifest << "  \"liveParameters\": [";
    for (int i = 0; i < parameters.size(); ++i)
        manifest << (i == 0 ? "" : ", ") << jsonQuote(parameters[i]);
    manifest << "],\n";
    manifest << "  \"nextImplementationStep\": \"Lower supported RC, gain, clipping, tone, and potentiometer cells into the process() DSP graph and connect it to the JUCE audio pipeline.\"\n";
    manifest << "}\n";

    const auto circuitFile = runDir.getChildFile("circuit.json");
    const auto frustFile = runDir.getChildFile("model.fr");
    const auto ilFile = runDir.getChildFile("model.frust-il");
    const auto uiSourceFile = runDir.getChildFile("ui.fr");
    const auto uiIlFile = runDir.getChildFile("ui.frust-il");
    const auto parametersFile = runDir.getChildFile("parameters.json");
    const auto uiFile = runDir.getChildFile("ui.json");
    const auto manifestFile = runDir.getChildFile("manifest.json");
    if (!circuitFile.replaceWithText(circuitJson)
        || !frustFile.replaceWithText(frust)
        || !ilFile.replaceWithText(il)
        || !uiSourceFile.replaceWithText(uiFrust)
        || !uiIlFile.replaceWithText(uiIl)
        || !parametersFile.replaceWithText(parametersJson)
        || !uiFile.replaceWithText(uiJson)
        || !manifestFile.replaceWithText(manifest))
        return "{ \"ok\": false, \"tool\": \"frust_realtime_preview_export\", \"displayTool\": \"frust.realtime_preview_export\", \"error\": \"Could not write one or more preview artifacts.\" }";

    juce::String result;
    result << "{\n";
    result << "  \"ok\": true,\n";
    result << "  \"schemaVersion\": 1,\n";
    result << "  \"tool\": \"frust_realtime_preview_export\",\n";
    result << "  \"displayTool\": \"frust.realtime_preview_export\",\n";
    result << "  \"status\": " << jsonQuote(unsupported.isEmpty() ? "ready_for_lowering" : "partial") << ",\n";
    result << "  \"artifactDirectory\": " << jsonQuote(runDir.getFullPathName()) << ",\n";
    result << "  \"frustSource\": " << jsonQuote(frustFile.getFullPathName()) << ",\n";
    result << "  \"frustIl\": " << jsonQuote(ilFile.getFullPathName()) << ",\n";
    result << "  \"uiSource\": " << jsonQuote(uiSourceFile.getFullPathName()) << ",\n";
    result << "  \"uiIl\": " << jsonQuote(uiIlFile.getFullPathName()) << ",\n";
    result << "  \"parameters\": " << jsonQuote(parametersFile.getFullPathName()) << ",\n";
    result << "  \"ui\": " << jsonQuote(uiFile.getFullPathName()) << ",\n";
    result << "  \"manifest\": " << jsonQuote(manifestFile.getFullPathName()) << ",\n";
    result << "  \"supportedComponentCount\": " << supported.size() << ",\n";
    result << "  \"unsupportedComponentCount\": " << unsupported.size() << ",\n";
    result << "  \"liveParameterCount\": " << parameters.size() << "\n";
    result << "}\n";
    return result;
}

juce::String ElectronicsWorkbench::writeAgentMarkdownTool(const juce::String& title, const juce::String& markdown)
{
    const auto runDir = generatedRunDirectory().getChildFile("agent_reports");
    if (!runDir.createDirectory())
    {
        return "{ \"ok\": false, \"tool\": \"agent_write_markdown\", \"displayTool\": \"agent.write_markdown\", \"error\": "
            + jsonQuote("Could not create report directory: " + runDir.getFullPathName()) + " }";
    }

    auto safeName = title.retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_").trim();
    if (safeName.isEmpty())
        safeName = "agent-report";
    safeName = safeName.replace(" ", "_").substring(0, 64);

    const auto stamp = juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S");
    const auto markdownFile = runDir.getChildFile(safeName + "_" + stamp + ".md");
    const auto htmlFile = runDir.getChildFile(safeName + "_" + stamp + ".html");
    const auto html = markdownToHtmlDocument(title, markdown);

    if (!markdownFile.replaceWithText(markdown))
    {
        return "{ \"ok\": false, \"tool\": \"agent_write_markdown\", \"displayTool\": \"agent.write_markdown\", \"error\": "
            + jsonQuote("Could not write markdown: " + markdownFile.getFullPathName()) + " }";
    }
    if (!htmlFile.replaceWithText(html))
    {
        return "{ \"ok\": false, \"tool\": \"agent_write_markdown\", \"displayTool\": \"agent.write_markdown\", \"error\": "
            + jsonQuote("Could not write rendered HTML: " + htmlFile.getFullPathName()) + " }";
    }

    juce::String result;
    result << "{\n";
    result << "  \"ok\": true,\n";
    result << "  \"schemaVersion\": 1,\n";
    result << "  \"kind\": \"djehuti_assistant_tool_result\",\n";
    result << "  \"tool\": \"agent_write_markdown\",\n";
    result << "  \"displayTool\": \"agent.write_markdown\",\n";
    result << "  \"status\": \"written_and_rendered\",\n";
    result << "  \"markdownPath\": " << jsonQuote(markdownFile.getFullPathName()) << ",\n";
    result << "  \"htmlPath\": " << jsonQuote(htmlFile.getFullPathName()) << ",\n";
    result << "  \"renderer\": \"markdown_html_katex\"\n";
    result << "}\n";
    return result;
}

juce::String ElectronicsWorkbench::researchWebSearchTool(const juce::String& query, int maxResults) const
{
    const auto limit = juce::jlimit(1, 8, maxResults <= 0 ? 5 : maxResults);
    const auto instantUrl = juce::URL("https://api.duckduckgo.com/?q="
        + juce::URL::addEscapeChars(query, true)
        + "&format=json&no_html=1&skip_disambig=1");

    const auto response = instantUrl.readEntireTextStream(false);
    if (response.trim().isEmpty())
    {
        return "{ \"ok\": false, \"tool\": \"research_web_search\", \"displayTool\": \"research.web_search\", \"error\": "
            + jsonQuote("No response for query: " + query) + " }";
    }

    const auto parsed = juce::JSON::parse(response);
    if (!parsed.isObject())
    {
        return "{ \"ok\": false, \"tool\": \"research_web_search\", \"displayTool\": \"research.web_search\", \"error\": \"Search response was not JSON.\" }";
    }

    juce::StringArray rows;
    const auto abstractText = parsed.getProperty("AbstractText", {}).toString().trim();
    const auto abstractUrl = parsed.getProperty("AbstractURL", {}).toString().trim();
    const auto heading = parsed.getProperty("Heading", {}).toString().trim();
    if (abstractText.isNotEmpty())
    {
        juce::String row;
        row << "{ \"title\": " << jsonQuote(heading.isNotEmpty() ? heading : query)
            << ", \"snippet\": " << jsonQuote(abstractText)
            << ", \"url\": " << jsonQuote(abstractUrl) << " }";
        rows.add(row);
    }

    std::function<void(const juce::var&)> collect;
    collect = [&](const juce::var& item) {
        if (rows.size() >= limit)
            return;
        if (const auto* object = item.getDynamicObject())
        {
            if (object->hasProperty("Topics"))
            {
                if (const auto* nested = object->getProperty("Topics").getArray())
                    for (const auto& child : *nested)
                        collect(child);
                return;
            }

            const auto text = object->getProperty("Text").toString().trim();
            const auto firstUrl = object->getProperty("FirstURL").toString().trim();
            if (text.isNotEmpty())
            {
                juce::String row;
                row << "{ \"title\": " << jsonQuote(text.upToFirstOccurrenceOf(" - ", false, false))
                    << ", \"snippet\": " << jsonQuote(text)
                    << ", \"url\": " << jsonQuote(firstUrl) << " }";
                rows.add(row);
            }
        }
    };

    if (const auto* topics = parsed.getProperty("RelatedTopics", {}).getArray())
        for (const auto& item : *topics)
            collect(item);

    juce::String source = "DuckDuckGo Instant Answer API";
    if (rows.isEmpty())
    {
        const auto htmlUrl = juce::URL("https://html.duckduckgo.com/html/?q="
            + juce::URL::addEscapeChars(query, true));
        const auto html = htmlUrl.readEntireTextStream(false);
        collectDuckDuckGoHtmlResults(html, limit, rows);
        if (!rows.isEmpty())
            source = "DuckDuckGo HTML search";
    }

    juce::String result;
    result << "{\n";
    result << "  \"ok\": true,\n";
    result << "  \"schemaVersion\": 1,\n";
    result << "  \"kind\": \"djehuti_assistant_tool_result\",\n";
    result << "  \"tool\": \"research_web_search\",\n";
    result << "  \"displayTool\": \"research.web_search\",\n";
    result << "  \"query\": " << jsonQuote(query) << ",\n";
    result << "  \"source\": " << jsonQuote(source) << ",\n";
    result << "  \"results\": [";
    for (int i = 0; i < rows.size(); ++i)
    {
        if (i != 0) result << ", ";
        result << rows[i];
    }
    result << "]\n";
    result << "}\n";
    return result;
}

juce::String ElectronicsWorkbench::designRlcHighPassFilterTool(double cutoffHz, double impedanceOhms)
{
    if (getCircuitJson == nullptr)
    {
        return "{ \"ok\": false, \"tool\": \"filter_design_high_pass\", \"displayTool\": \"filter.design_high_pass\", \"error\": \"No schematic model is available.\" }";
    }

    const auto design = makeRlcHighPassDesign(cutoffHz, impedanceOhms);
    const auto samples = sweepRlcHighPass(design);
    const auto cutoffSample = sampleRlcHighPassAt(design, design.cutoffHz);
    const auto runDir = generatedRunDirectory();
    if (!runDir.createDirectory())
    {
        return "{ \"ok\": false, \"tool\": \"filter_design_high_pass\", \"displayTool\": \"filter.design_high_pass\", \"error\": "
            + jsonQuote("Could not create run directory: " + runDir.getFullPathName()) + " }";
    }

    const auto circuitFile = runDir.getChildFile("rlc_high_pass_circuit.json");
    const auto netlistFile = runDir.getChildFile("rlc_high_pass_ac.cir");
    const auto csvFile = runDir.getChildFile("rlc_high_pass_ac.csv");
    const auto svgFile = runDir.getChildFile("rlc_high_pass_response.svg");
    const auto reportFile = runDir.getChildFile("rlc_high_pass_report.md");
    const auto manifestFile = runDir.getChildFile("assistant_tools.json");

    juce::String report;
    report << "# RLC 2nd Order High-Pass Filter\n\n";
    report << "- Topology: 8 ohm source, series capacitor, shunt inductor, 8 ohm load.\n";
    report << "- Alignment: 2nd order Butterworth high-pass, normalized to matched passband.\n";
    report << "- Cutoff: " << numberText(design.cutoffHz, 4) << " Hz.\n";
    report << "- Matched impedance: " << numberText(design.impedanceOhms, 4) << " ohm.\n";
    report << "- Source resistor: " << numberText(design.sourceOhms, 4) << " ohm.\n";
    report << "- Load resistor: " << numberText(design.loadOhms, 4) << " ohm.\n";
    report << "- Series capacitor C1: " << humanCapacitance(design.capacitanceFarads)
           << " (`" << spiceCapacitance(design.capacitanceFarads) << "` F in SPICE suffix notation).\n";
    report << "- Shunt inductor L1: " << humanInductance(design.inductanceHenries)
           << " (`" << spiceInductance(design.inductanceHenries) << "` H in SPICE suffix notation).\n\n";
    report << "## AC Sweep Result\n\n";
    report << "- Gain at cutoff, normalized to matched passband: "
           << numberText(cutoffSample.normalizedGainDb, 4) << " dB.\n";
    report << "- Raw V(out)/V(source) at cutoff: " << numberText(cutoffSample.rawGainDb, 4)
           << " dB. The extra ~6 dB loss is the intentional 8 ohm source/load division.\n";
    report << "- Input impedance magnitude at cutoff: " << numberText(cutoffSample.inputImpedanceMag, 4)
           << " ohm.\n";
    report << "- Input impedance at cutoff: " << numberText(cutoffSample.inputImpedanceReal, 4)
           << " + j" << numberText(cutoffSample.inputImpedanceImag, 4) << " ohm.\n\n";
    report << "## Artifacts\n\n";
    report << "- SPICE AC netlist: `" << netlistFile.getFullPathName() << "`\n";
    report << "- Sweep CSV: `" << csvFile.getFullPathName() << "`\n";
    report << "- Frequency response SVG: `" << svgFile.getFullPathName() << "`\n";
    report << "- Schematic circuit JSON: `" << circuitFile.getFullPathName() << "`\n";

    struct OutputFile
    {
        juce::File file;
        juce::String text;
        juce::String label;
    };

    const OutputFile files[] = {
        { circuitFile, getCircuitJson(), "circuit JSON" },
        { netlistFile, buildRlcHighPassSpiceNetlist(design), "SPICE netlist" },
        { csvFile, buildRlcHighPassCsv(samples), "AC sweep CSV" },
        { svgFile, buildRlcHighPassSvg(design, samples), "frequency response SVG" },
        { reportFile, report, "analysis report" },
        { manifestFile, buildAssistantToolManifestJson(), "assistant tool manifest" }
    };

    for (const auto& output : files)
    {
        if (!output.file.replaceWithText(output.text))
        {
            return "{ \"ok\": false, \"tool\": \"filter_design_high_pass\", \"displayTool\": \"filter.design_high_pass\", \"error\": "
                + jsonQuote("Could not write " + output.label + ": " + output.file.getFullPathName()) + " }";
        }
    }


    juce::String result;
    result << "{\n";
    result << "  \"ok\": true,\n";
    result << "  \"schemaVersion\": 1,\n";
    result << "  \"kind\": \"djehuti_assistant_tool_result\",\n";
    result << "  \"tool\": \"filter_design_high_pass\",\n";
    result << "  \"displayTool\": \"filter.design_high_pass\",\n";
    result << "  \"status\": \"designed_and_analyzed\",\n";
    result << "  \"analysisEngine\": \"internal_linear_ac_sweep\",\n";
    result << "  \"cutoffHz\": " << numberText(design.cutoffHz, 6) << ",\n";
    result << "  \"impedanceOhms\": " << numberText(design.impedanceOhms, 6) << ",\n";
    result << "  \"capacitanceFarads\": " << numberText(design.capacitanceFarads, 12) << ",\n";
    result << "  \"inductanceHenries\": " << numberText(design.inductanceHenries, 12) << ",\n";
    result << "  \"capacitanceLabel\": " << jsonQuote(humanCapacitance(design.capacitanceFarads)) << ",\n";
    result << "  \"inductanceLabel\": " << jsonQuote(humanInductance(design.inductanceHenries)) << ",\n";
    result << "  \"cutoffGainDb\": " << numberText(cutoffSample.normalizedGainDb, 8) << ",\n";
    result << "  \"cutoffInputImpedanceOhms\": " << numberText(cutoffSample.inputImpedanceMag, 8) << ",\n";
    result << "  \"artifactDirectory\": " << jsonQuote(runDir.getFullPathName()) << ",\n";
    result << "  \"circuitJson\": " << jsonQuote(circuitFile.getFullPathName()) << ",\n";
    result << "  \"spiceNetlist\": " << jsonQuote(netlistFile.getFullPathName()) << ",\n";
    result << "  \"csv\": " << jsonQuote(csvFile.getFullPathName()) << ",\n";
    result << "  \"responseSvg\": " << jsonQuote(svgFile.getFullPathName()) << ",\n";
    result << "  \"report\": " << jsonQuote(reportFile.getFullPathName()) << "\n";
    result << "}\n";
    return result;
}

juce::String ElectronicsWorkbench::designPushPullAmplifierTool()
{
    if (getCircuitJson == nullptr || getXyceNetlist == nullptr || getErcReport == nullptr)
        return "{ \"ok\": false, \"tool\": \"amplifier_design_push_pull\", \"displayTool\": \"amplifier.design_push_pull\", \"error\": \"No schematic model is available.\" }";

    const auto runDir = generatedRunDirectory();
    if (!runDir.createDirectory())
    {
        return "{ \"ok\": false, \"tool\": \"amplifier_design_push_pull\", \"displayTool\": \"amplifier.design_push_pull\", \"error\": "
            + jsonQuote("Could not create run directory: " + runDir.getFullPathName()) + " }";
    }

    const auto circuitFile = runDir.getChildFile("push_pull_amplifier_circuit.json");
    const auto netlistFile = runDir.getChildFile("push_pull_amplifier_preview.cir");
    const auto ercFile = runDir.getChildFile("push_pull_amplifier_erc.md");
    const auto reportFile = runDir.getChildFile("push_pull_amplifier_report.md");
    const auto manifestFile = runDir.getChildFile("assistant_tools.json");

    const auto ercReport = getErcReport();
    const auto errors = ercReport.fromFirstOccurrenceOf("- Errors: ", false, false)
                            .upToFirstOccurrenceOf("\n", false, false).getIntValue();
    const auto warnings = ercReport.fromFirstOccurrenceOf("- Warnings: ", false, false)
                              .upToFirstOccurrenceOf("\n", false, false).getIntValue();

    juce::String report;
    report << "# Push-Pull Audio Output Stage\n\n";
    report << "- Topology: diode-biased class AB complementary emitter follower.\n";
    report << "- Supplies: +12V and -12V named rails from two stacked 12 V sources around ground.\n";
    report << "- Input: 0.25 V AC source at 1 kHz, coupled through 10u C1 into the middle of the bias string.\n";
    report << "- Bias: R1 2.2k from +12V and R2 2.2k to -12V feed two 1N4148 diodes; the diode drops hold\n";
    report << "  the NPN base about 0.6 V above and the PNP base about 0.6 V below the drive node.\n";
    report << "- Output pair: generic NPN (collector to +12V) and PNP (collector to -12V) with 0.47 ohm\n";
    report << "  emitter ballast resistors R3/R4 into the output node.\n";
    report << "- Load: 8 ohm resistor from output node to ground.\n";
    report << "- Instrumentation: 2-channel oscilloscope on input and output.\n\n";
    report << "## Verification\n\n";
    report << "- ERC errors: " << errors << "\n";
    report << "- ERC warnings: " << warnings << "\n";
    report << "- Note: transistor devices are represented structurally; full BJT SPICE model lowering is still a planned solver capability.\n\n";
    report << "## Artifacts\n\n";
    report << "- Circuit JSON: `" << circuitFile.getFullPathName() << "`\n";
    report << "- Preview netlist: `" << netlistFile.getFullPathName() << "`\n";
    report << "- ERC report: `" << ercFile.getFullPathName() << "`\n";

    struct OutputFile
    {
        juce::File file;
        juce::String text;
        juce::String label;
    };

    const OutputFile files[] = {
        { circuitFile, getCircuitJson(), "circuit JSON" },
        { netlistFile, getXyceNetlist(), "preview netlist" },
        { ercFile, ercReport, "ERC report" },
        { reportFile, report, "analysis report" },
        { manifestFile, buildAssistantToolManifestJson(), "assistant tool manifest" }
    };

    for (const auto& output : files)
    {
        if (!output.file.replaceWithText(output.text))
        {
            return "{ \"ok\": false, \"tool\": \"amplifier_design_push_pull\", \"displayTool\": \"amplifier.design_push_pull\", \"error\": "
                + jsonQuote("Could not write " + output.label + ": " + output.file.getFullPathName()) + " }";
        }
    }

    juce::String result;
    result << "{\n";
    result << "  \"ok\": true,\n";
    result << "  \"schemaVersion\": 1,\n";
    result << "  \"kind\": \"djehuti_assistant_tool_result\",\n";
    result << "  \"tool\": \"amplifier_design_push_pull\",\n";
    result << "  \"displayTool\": \"amplifier.design_push_pull\",\n";
    result << "  \"status\": \"designed_and_exported\",\n";
    result << "  \"topology\": \"class_ab_complementary_emitter_follower\",\n";
    result << "  \"loadOhms\": 8,\n";
    result << "  \"ercErrors\": " << errors << ",\n";
    result << "  \"ercWarnings\": " << warnings << ",\n";
    result << "  \"artifactDirectory\": " << jsonQuote(runDir.getFullPathName()) << ",\n";
    result << "  \"circuitJson\": " << jsonQuote(circuitFile.getFullPathName()) << ",\n";
    result << "  \"previewNetlist\": " << jsonQuote(netlistFile.getFullPathName()) << ",\n";
    result << "  \"ercReport\": " << jsonQuote(ercFile.getFullPathName()) << ",\n";
    result << "  \"report\": " << jsonQuote(reportFile.getFullPathName()) << "\n";
    result << "}\n";
    return result;
}

void ElectronicsWorkbench::exportCircuitArtifacts()
{
    const auto result = exportCircuitArtifactsTool();
    const auto parsed = juce::JSON::parse(result);
    if (!parsed.isObject() || !(bool)parsed.getProperty("ok", false))
    {
        appendLog("Export failed: " + parsed.getProperty("error", result).toString());
        return;
    }

    appendLog("Exported circuit JSON, Xyce netlist, and lab instruments to "
              + parsed.getProperty("artifactDirectory", generatedRunDirectory().getFullPathName()).toString());
}

void ElectronicsWorkbench::exportFrustRealtimePreview()
{
    const auto result = exportFrustRealtimePreviewTool();
    const auto parsed = juce::JSON::parse(result);
    if (!parsed.isObject() || !(bool)parsed.getProperty("ok", false))
    {
        appendLog("Frust realtime package export failed: " + parsed.getProperty("error", result).toString());
        return;
    }

    appendLog("Exported Djehuti audio model package to "
              + parsed.getProperty("artifactDirectory", generatedRunDirectory().getFullPathName()).toString()
              + " with " + parsed.getProperty("liveParameterCount", 0).toString()
              + " live parameter(s).");
}

void ElectronicsWorkbench::chooseAudioSourceFile()
{
    audioSourceChooser = std::make_unique<juce::FileChooser>("Select Audio File", juce::File(), "*.wav;*.mp3;*.mp4;*.ogg;*.flac");
    audioSourceChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& chooser) {
            auto result = chooser.getResult();
            if (result.existsAsFile()) {
                audioPipeline->setRouting("File", result.getFullPathName(), "Hardware", "");
                appendLog("System", "0x00", "Audio input routed to looping file: " + result.getFullPathName());
            }
        });
}

// Analytics is a main window of its own: the dock panel floats out into a large
// resizable window (it can still be docked back as a tab by dragging).
void ElectronicsWorkbench::showAnalytics()
{
    auto* panel = analyticsDockPanel.getComponent();
    if (dockManager == nullptr || panel == nullptr)
        return;
    auto* window = dynamic_cast<CreationDock::FloatingDockWindow*>(panel->getTopLevelComponent());
    if (window == nullptr)
    {
        dockManager->floatPanel(panel);
        window = dynamic_cast<CreationDock::FloatingDockWindow*>(panel->getTopLevelComponent());
        if (window != nullptr)
            if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
            {
                const auto area = display->userArea;
                window->setBounds(area.reduced(area.getWidth() / 14, area.getHeight() / 14));
            }
    }
    if (window != nullptr)
    {
        window->setVisible(true);
        window->toFront(true);
    }
    else if (auto* zone = panel->findParentComponentOfClass<CreationDock::DockZone>())
        zone->setActivePanel(panel);
}

juce::String ElectronicsWorkbench::analyticsTool(const juce::String& name, const juce::var& args)
{
    auto quoteJson = [](const juce::String& t) { return juce::JSON::toString(juce::var(t)); };
    auto fail = [&](const juce::String& error) {
        return "{ \"ok\": false, \"tool\": " + quoteJson(name) + ", \"error\": " + quoteJson(error) + " }";
    };
    if (analyticsPanel == nullptr)
        return fail("The Analytics window is unavailable.");
    auto settingsFrom = [](const juce::var& object) {
        analytics::Settings s;
        if (const auto* o = object.getDynamicObject())
            for (const auto& p : o->getProperties())
                s[p.name.toString()] = p.value.toString();
        return s;
    };
    auto translateCanonicalParameters = [&](analytics::Settings s) {
        std::map<juce::String, juce::String> targets;
        if (getCircuitJson != nullptr)
        {
            const auto parsed = juce::JSON::parse(getCircuitJson());
            if (const auto* root = parsed.getDynamicObject())
                if (const auto* params = root->getProperty("simulationParameters").getArray())
                    for (const auto& item : *params)
                        if (const auto* p = item.getDynamicObject())
                        {
                            const auto id = p->getProperty("id").toString();
                            const auto refdes = p->getProperty("refdes").toString();
                            const auto property = p->getProperty("property").toString();
                            if (id.isNotEmpty() && refdes.isNotEmpty() && property.isNotEmpty())
                                targets["param:" + id] = refdes + "." + property;
                        }
        }
        for (auto& [key, value] : s)
            if (value.startsWithIgnoreCase("param:"))
                if (const auto found = targets.find(value); found != targets.end())
                    value = found->second;
        return s;
    };

    if (name == "analytics_list")
    {
        const auto netlist = analyticsPanel->getNetlist != nullptr ? analyticsPanel->getNetlist() : analytics::Netlist {};
        auto* root = new juce::DynamicObject();
        root->setProperty("ok", true);
        root->setProperty("tool", name);
        juce::Array<juce::var> list;
        for (const auto& a : analytics::analyses())
        {
            auto* o = new juce::DynamicObject();
            o->setProperty("tool", "analytics_" + a.key);
            o->setProperty("title", a.title);
            o->setProperty("description", a.description);
            juce::Array<juce::var> fields;
            for (const auto& f : analytics::fieldsFor(a.id))
            {
                auto* fo = new juce::DynamicObject();
                fo->setProperty("key", f.key);
                fo->setProperty("label", f.label);
                fo->setProperty("group", f.group);
                if (f.unit.isNotEmpty()) fo->setProperty("unit", f.unit);
                if (f.defaultValue.isNotEmpty()) fo->setProperty("default", f.defaultValue);
                if (!f.options.isEmpty()) fo->setProperty("choices", juce::var(f.options));
                if (f.help.isNotEmpty()) fo->setProperty("help", f.help);
                fields.add(juce::var(fo));
            }
            o->setProperty("settings", fields);
            list.add(juce::var(o));
        }
        root->setProperty("analyses", list);
        root->setProperty("activeSetup", analyticsPanel->settingsState());
        juce::Array<juce::var> nets;
        for (const auto& n : netlist.nets)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty("net", n.name);
            o->setProperty("pins", n.pins);
            nets.add(juce::var(o));
        }
        root->setProperty("nets", nets);
        root->setProperty("sources", juce::var(analytics::sourceChoices(netlist)));
        root->setProperty("sweepTargets", juce::var(analytics::targetChoices(netlist, true)));
        if (getCircuitJson != nullptr)
        {
            juce::Array<juce::var> canonical;
            const auto parsed = juce::JSON::parse(getCircuitJson());
            if (const auto* circuit = parsed.getDynamicObject())
            {
                std::map<juce::String, juce::String> symbols;
                if (const auto* components = circuit->getProperty("components").getArray())
                    for (const auto& item : *components)
                        if (const auto* c = item.getDynamicObject())
                            symbols[c->getProperty("id").toString()] = c->getProperty("symbol").toString();
                if (const auto* params = circuit->getProperty("simulationParameters").getArray())
                    for (const auto& item : *params)
                    {
                        const auto* p = item.getDynamicObject();
                        if (p == nullptr)
                            continue;
                        const auto refdes = p->getProperty("refdes").toString();
                        const auto symbol = symbols[refdes];
                        if (schematic::isInstrumentSymbol(symbol) || symbol == "annotation_text" || symbol == "ground"
                            || symbol == "power_port" || symbol == "net_label" || symbol == "power_bus" || symbol == "ground_bus")
                            continue;
                        auto* o = new juce::DynamicObject();
                        o->setProperty("target", "param:" + p->getProperty("id").toString());
                        o->setProperty("id", p->getProperty("id").toString());
                        o->setProperty("refdes", refdes);
                        o->setProperty("property", p->getProperty("property").toString());
                        o->setProperty("label", p->getProperty("label").toString());
                        o->setProperty("unit", p->getProperty("unit").toString());
                        o->setProperty("capability", "Rerun");
                        canonical.add(juce::var(o));
                    }
            }
            root->setProperty("simulationParameters", canonical);
        }
        root->setProperty("warnings", juce::var(netlist.warnings));
        return juce::JSON::toString(juce::var(root), true);
    }
    if (name == "analytics_result")
    {
        const auto& runs = analyticsPanel->history();
        const auto index = args.getProperty("index", "0").toString().getIntValue();
        if (runs.empty()) return fail("There is no Analytics result yet.");
        if (index < 0 || index >= (int)runs.size()) return fail("History holds " + juce::String((int)runs.size()) + " result(s).");
        const auto& run = runs[runs.size() - 1 - (size_t)index];
        return analytics::toJson(run.result, run.files);
    }
    if (name == "analytics_open")
    {
        const auto* info = analytics::findAnalysis(args.getProperty("analysis", {}).toString());
        if (info == nullptr) return fail("Unknown analysis. Use one of the keys from analytics_list.");
        analyticsPanel->selectAnalysis(info->id);
        analyticsPanel->setSettings(info->id, translateCanonicalParameters(settingsFrom(args.getProperty("settings", {}))));
        showAnalytics();
        return "{ \"ok\": true, \"tool\": " + quoteJson(name) + ", \"analysis\": " + quoteJson(info->key) + " }";
    }
    if (name == "analytics_measure")
    {
        const auto kindName = args.getProperty("measurement", {}).toString().trim();
        signal_measure::Request request;
        bool known = false;
        for (auto k : signal_measure::allKinds())
            if (kindName.equalsIgnoreCase(signal_measure::kindName(k))) { request.kind = k; known = true; }
        if (!known)
        {
            juce::StringArray kinds;
            for (auto k : signal_measure::allKinds()) kinds.add(signal_measure::kindName(k));
            return fail("Unknown measurement. Use one of: " + kinds.joinIntoString(", "));
        }
        auto number = [&](const char* key, double& out) {
            const auto t = args.getProperty(key, {}).toString().trim();
            double v = 0.0;
            if (t.isNotEmpty() && circuit_sim::parseValue(t.toStdString(), v)) { out = v; return true; }
            return false;
        };
        number("from", request.from);
        number("to", request.to);
        number("at", request.at);
        number("level", request.level);
        double nth = 1.0;
        if (number("nth", nth)) request.nth = juce::jmax(1, (int)nth);
        number("low_percent", request.lowPercent);
        number("high_percent", request.highPercent);
        number("band_percent", request.bandPercent);
        const auto edge = args.getProperty("edge", "Rising").toString();
        request.edge = edge.equalsIgnoreCase("Falling") ? signal_measure::Edge::Falling
                     : edge.equalsIgnoreCase("Either") ? signal_measure::Edge::Either : signal_measure::Edge::Rising;
        juce::String label;
        const auto r = analyticsPanel->measureLatest(args.getProperty("trace", {}).toString(), request, label);
        if (!r.ok) return fail(juce::String(r.error));
        return "{ \"ok\": true, \"tool\": " + quoteJson(name) + ", \"value\": " + juce::String(r.value, 12)
             + ", \"text\": " + quoteJson(label) + " }";
    }
    const auto* info = analytics::findAnalysis(name.fromFirstOccurrenceOf("analytics_", false, false));
    if (info == nullptr)
        return fail("Unknown analytics tool.");
    const auto& run = analyticsPanel->runNow(info->id, translateCanonicalParameters(settingsFrom(args)));
    return analytics::toJson(run.result, run.files);
}

juce::String ElectronicsWorkbench::pcbTool(const juce::String& name, const juce::var& args)
{
    auto reply = [&](bool ok, const juce::String& error) {
        auto* root = new juce::DynamicObject();
        root->setProperty("ok", ok);
        root->setProperty("tool", name);
        if (error.isNotEmpty()) root->setProperty("error", error);
        if (pcbPanel != nullptr)
        {
            const auto& d = pcbPanel->design();
            root->setProperty("board", d.toVar());
            const auto b = d.bounds();
            root->setProperty("widthMm", b.getWidth());
            root->setProperty("heightMm", b.getHeight());
            root->setProperty("areaMm2", d.areaMm2());
            root->setProperty("problems", juce::var(d.problems()));
        }
        return juce::JSON::toString(juce::var(root), true);
    };
    if (pcbPanel == nullptr)
        return reply(false, "The PCB tab is unavailable.");
    auto points = [](const juce::var& v) {
        std::vector<pcb::Point> list;
        if (const auto* a = v.getArray())
            for (const auto& p : *a)
                if (const auto* xy = p.getArray(); xy != nullptr && xy->size() >= 2)
                    list.push_back({ (double)(*xy)[0], (double)(*xy)[1] });
        return list;
    };
    auto next = pcbPanel->design();
    if (name == "pcb_board_get")
        return reply(true, {});
    if (name == "pcb_board_list_options")
    {
        auto* root = new juce::DynamicObject();
        root->setProperty("ok", true);
        root->setProperty("tool", name);
        juce::Array<juce::var> standards;
        for (const auto& s : djehuti::route::standardBoards())
        {
            auto* o = new juce::DynamicObject();
            o->setProperty("id", juce::String(s.id));
            o->setProperty("name", juce::String(s.name));
            o->setProperty("description", juce::String(s.description));
            standards.add(juce::var(o));
        }
        root->setProperty("standards", standards);
        juce::Array<juce::var> shapeList;
        for (const auto& s : pcb::shapes())
        {
            auto* o = new juce::DynamicObject();
            o->setProperty("id", s.id);
            o->setProperty("name", s.name);
            auto* params = new juce::DynamicObject();
            for (const auto& p : s.params) params->setProperty(juce::Identifier(p.key), p.defaultValue);
            o->setProperty("params", juce::var(params));
            shapeList.add(juce::var(o));
        }
        root->setProperty("shapes", shapeList);
        return juce::JSON::toString(juce::var(root), true);
    }
    if (name == "pcb_board_use_standard")
    {
        const auto id = args.getProperty("id", {}).toString();
        if (djehuti::route::findStandardBoard(id.toStdString()) == nullptr)
            return reply(false, "No standard board " + id + "; see pcb_board_list_options.");
        auto board = pcb::BoardDesign::standard(id);
        board.layers = next.layers;
        board.thickness = next.thickness;
        board.edgeClearance = next.edgeClearance;
        pcbPanel->setDesign(board);
        return reply(true, {});
    }
    if (name == "pcb_board_set_shape")
    {
        const auto shape = args.getProperty("shape", {}).toString();
        if (pcb::findShape(shape) == nullptr)
            return reply(false, "No shape " + shape + "; see pcb_board_list_options.");
        std::map<juce::String, double> params;
        if (const auto* o = args.getProperty("params", {}).getDynamicObject())
            for (const auto& p : o->getProperties())
                params[p.name.toString()] = (double)p.value;
        auto board = pcb::BoardDesign::fromShape(shape, params);
        board.holes = next.holes;
        board.cutouts = next.cutouts;
        board.layers = next.layers;
        board.thickness = next.thickness;
        board.edgeClearance = next.edgeClearance;
        pcbPanel->setDesign(board);
        return reply(true, {});
    }
    if (name == "pcb_board_set_outline")
    {
        const auto pts = points(args.getProperty("points", {}));
        if (pts.size() < 3)
            return reply(false, "An outline needs at least 3 corners.");
        next.source = "custom";
        next.outline = pts;
        pcbPanel->setDesign(next);
        return reply(next.problems().isEmpty(), next.problems().joinIntoString(" "));
    }
    if (name == "pcb_board_add_hole")
    {
        const double d = (double)args.getProperty("diameter", 0.0);
        if (d <= 0.0)
            return reply(false, "The diameter must be positive.");
        next.holes.push_back({ { (double)args.getProperty("x", 0.0), (double)args.getProperty("y", 0.0) }, d });
        pcbPanel->setDesign(next);
        return reply(next.problems().isEmpty(), next.problems().joinIntoString(" "));
    }
    if (name == "pcb_board_add_cutout")
    {
        const auto pts = points(args.getProperty("points", {}));
        if (pts.size() < 3)
            return reply(false, "A cutout needs at least 3 corners.");
        next.cutouts.push_back(pts);
        pcbPanel->setDesign(next);
        return reply(next.problems().isEmpty(), next.problems().joinIntoString(" "));
    }
    if (name == "pcb_board_remove")
    {
        const auto kind = args.getProperty("kind", {}).toString();
        const int index = (int)args.getProperty("index", 0) - 1;
        if (kind == "hole" && index >= 0 && index < (int)next.holes.size())
            next.holes.erase(next.holes.begin() + index);
        else if (kind == "cutout" && index >= 0 && index < (int)next.cutouts.size())
            next.cutouts.erase(next.cutouts.begin() + index);
        else
            return reply(false, "No " + kind + " number " + juce::String(index + 1) + ".");
        pcbPanel->setDesign(next);
        return reply(true, {});
    }
    if (name == "pcb_board_set_stackup")
    {
        if (args.hasProperty("layers"))
        {
            const int layers = (int)args.getProperty("layers", 2);
            if (layers != 1 && layers != 2 && layers != 4 && layers != 6 && layers != 8)
                return reply(false, "Layers must be 1, 2, 4, 6 or 8.");
            next.layers = layers;
        }
        if (args.hasProperty("thickness")) next.thickness = (double)args.getProperty("thickness", 1.6);
        if (args.hasProperty("edge_clearance")) next.edgeClearance = (double)args.getProperty("edge_clearance", 0.3);
        pcbPanel->setDesign(next);
        return reply(true, {});
    }
    return reply(false, "Unknown PCB tool.");
}

juce::String ElectronicsWorkbench::frustTool(const juce::String& name, const juce::var& args)
{
    auto* root = new juce::DynamicObject();
    root->setProperty("tool", name);
    if (frustPanel == nullptr)
    {
        root->setProperty("ok", false);
        root->setProperty("error", "The Frust panel is unavailable.");
        return juce::JSON::toString(juce::var(root));
    }
    const auto source = args.getProperty("source", {}).toString();
    const auto result = name == "frust_run" ? frustPanel->runNow(source) : frustPanel->checkNow(source);
    root->setProperty("ok", result.ok);
    if (name == "frust_run" && result.ok)
        root->setProperty("output", juce::String(result.output));
    if (!result.error.empty())
        root->setProperty("error", juce::String(result.error));
    juce::Array<juce::var> diagnostics;
    for (const auto& d : result.diagnostics)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("severity", d.error ? "error" : "warning");
        o->setProperty("line", d.line);
        o->setProperty("column", d.column);
        o->setProperty("message", juce::String(d.message));
        diagnostics.add(juce::var(o));
    }
    root->setProperty("diagnostics", diagnostics);
    root->setProperty("compileMs", result.compileMs);
    if (name == "frust_run") root->setProperty("runMs", result.runMs);
    return juce::JSON::toString(juce::var(root), true);
}

juce::String ElectronicsWorkbench::autoLayoutDiagramTool()
{
    return autoLayoutTool != nullptr
        ? autoLayoutTool()
        : juce::String("{ \"ok\": false, \"tool\": \"schematic_auto_layout\", \"displayTool\": \"schematic.auto_layout\", \"error\": \"Schematic auto-layout is unavailable.\" }");
}

void ElectronicsWorkbench::autoLayoutDiagram()
{
    const auto result = autoLayoutDiagramTool();
    const auto parsed = juce::JSON::parse(result);
    if (!parsed.isObject() || !(bool)parsed.getProperty("ok", false))
    {
        appendLog("Auto layout failed: " + parsed.getProperty("error", result).toString());
        return;
    }

    appendLog("Auto-laid out "
              + parsed.getProperty("componentCount", {}).toString()
              + " schematic component(s).");
}

void ElectronicsWorkbench::showSpecDocument()
{
    const auto spec = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
        .getParentDirectory()
        .getParentDirectory()
        .getParentDirectory()
        .getParentDirectory()
        .getChildFile("ELECTRONICS_DESIGN_TOOL_SPEC.md");
    appendLog("Research spec path: " + spec.getFullPathName());
}


// The parts on the board and their routing (pcb_* tools other than pcb_board_*).
juce::String ElectronicsWorkbench::pcbLayoutTool(const juce::String& name, const juce::var& args)
{
    auto pointVar = [](pcb::Point p) {
        juce::Array<juce::var> a { std::round(p.x * 1000.0) / 1000.0, std::round(p.y * 1000.0) / 1000.0 };
        return juce::var(a);
    };
    auto markersVar = [&](const std::vector<pcb::Marker>& list) {
        juce::Array<juce::var> a;
        for (const auto& m : list)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty("kind", m.kind);
            o->setProperty("message", m.message);
            if (m.located) o->setProperty("at", pointVar(m.at));
            a.add(juce::var(o));
        }
        return a;
    };
    auto reply = [&](bool ok, const juce::String& error, bool withPads, juce::DynamicObject* extra) {
        auto* root = extra != nullptr ? extra : new juce::DynamicObject();
        root->setProperty("ok", ok);
        root->setProperty("tool", name == "pcb_route_result" ? juce::String("pcb_route") : name);
        if (error.isNotEmpty()) root->setProperty("error", error);
        if (pcbPanel != nullptr)
        {
            const auto& l = pcbPanel->layoutState();
            juce::Array<juce::var> parts;
            for (const auto& p : l.parts)
            {
                auto* o = new juce::DynamicObject();
                o->setProperty("refdes", p.refdes);
                o->setProperty("symbol", p.symbolId);
                o->setProperty("value", p.value);
                o->setProperty("footprint", p.footprint);
                o->setProperty("at", pointVar(p.at));
                o->setProperty("rotation", p.rotation);
                if (withPads)
                {
                    juce::Array<juce::var> pads;
                    for (const auto& pad : pcb::padsOf(p))
                    {
                        auto* q = new juce::DynamicObject();
                        q->setProperty("pad", pad.number);
                        q->setProperty("pin", pad.pin);
                        q->setProperty("net", pad.net);
                        q->setProperty("at", pointVar(pad.centre));
                        pads.add(juce::var(q));
                    }
                    o->setProperty("pads", pads);
                }
                parts.add(juce::var(o));
            }
            root->setProperty("parts", parts);
            auto* rules = new juce::DynamicObject();
            rules->setProperty("track_width", l.rules.trackWidth);
            rules->setProperty("clearance", l.rules.clearance);
            rules->setProperty("via_diameter", l.rules.viaDiameter);
            rules->setProperty("via_drill", l.rules.viaDrill);
            root->setProperty("rules", juce::var(rules));
            root->setProperty("nets", juce::var(pcb::netNames(l)));
            root->setProperty("placementProblems", juce::var(pcb::placementProblems(l, pcbPanel->design())));
            root->setProperty("routed", l.routed);
            if (l.routeError.isNotEmpty()) root->setProperty("routeError", l.routeError);
            if (withPads)
            {
                const auto lv = l.toVar();
                root->setProperty("texts", lv.getProperty("texts", {}));
                root->setProperty("graphics", lv.getProperty("graphics", {}));
                root->setProperty("fab", lv.getProperty("fab", {}));
            }
            root->setProperty("artworkWarnings", juce::var(pcb::artworkWarnings(l)));
            if (name == "pcb_route" || name == "pcb_route_result" || name == "pcb_layout_get" || name == "pcb_verify_netlist")
            {
                const auto& v = name == "pcb_verify_netlist" ? pcbPanel->verify() : pcbPanel->lastVerification();
                auto* o = new juce::DynamicObject();
                o->setProperty("matches", v.matches);
                o->setProperty("summary", v.summary);
                o->setProperty("pinsCompared", v.pinsCompared);
                o->setProperty("schematicNets", v.schematicNets);
                o->setProperty("boardNodes", v.boardNodes);
                o->setProperty("problems", juce::var(v.problems));
                o->setProperty("note", pcb::netlistCheckNote());
                root->setProperty("schematicCheck", juce::var(o));
            }
            if (l.routed)
            {
                double length = 0.0;
                for (const auto& t : l.tracks)
                    for (size_t i = 1; i < t.points.size(); ++i) length += t.points[i - 1].getDistanceFrom(t.points[i]);
                root->setProperty("connections", l.connections);
                root->setProperty("routedConnections", l.routedConnections);
                root->setProperty("passes", l.iterations);
                root->setProperty("seconds", l.seconds);
                root->setProperty("trackLengthMm", std::round(length * 100.0) / 100.0);
                root->setProperty("tracks", (int)l.tracks.size());
                root->setProperty("vias", (int)l.vias.size());
                root->setProperty("unrouted", juce::var(l.unrouted));
                root->setProperty("violations", markersVar(l.violations));
            }
        }
        return juce::JSON::toString(juce::var(root), true);
    };
    if (pcbPanel == nullptr)
        return reply(false, "The PCB tab is unavailable.", false, nullptr);

    auto applyRules = [&](pcb::Layout& l) {
        auto set = [&](const char* key, double& field) {
            if (args.hasProperty(key) && (double)args.getProperty(key, 0.0) > 0.0) field = (double)args.getProperty(key, field);
        };
        set("track_width", l.rules.trackWidth);
        set("clearance", l.rules.clearance);
        set("via_diameter", l.rules.viaDiameter);
        set("via_drill", l.rules.viaDrill);
    };

    if (name == "pcb_layout_get")
        return reply(true, {}, true, nullptr);
    if (name == "pcb_footprints_list")
    {
        auto* root = new juce::DynamicObject();
        root->setProperty("ok", true);
        root->setProperty("tool", name);
        juce::Array<juce::var> list;
        for (const auto& f : pcb::footprints())
        {
            auto* o = new juce::DynamicObject();
            o->setProperty("id", f.id);
            o->setProperty("name", f.name);
            o->setProperty("description", f.description);
            o->setProperty("courtyard", pointVar({ f.courtyardW, f.courtyardH }));
            juce::Array<juce::var> pads;
            for (const auto& d : f.pads)
            {
                auto* q = new juce::DynamicObject();
                q->setProperty("pad", d.number);
                q->setProperty("at", pointVar({ d.x, d.y }));
                q->setProperty("size", pointVar({ d.w, d.h }));
                q->setProperty("shape", d.round ? "round" : "rect");
                if (d.drill > 0.0) q->setProperty("drill", d.drill);
                pads.add(juce::var(q));
            }
            o->setProperty("pads", pads);
            list.add(juce::var(o));
        }
        root->setProperty("footprints", list);
        const auto symbol = args.getProperty("symbol_id", "").toString();
        if (symbol.isNotEmpty())
        {
            root->setProperty("symbol_id", symbol);
            root->setProperty("fits", juce::var(pcb::footprintsFor(symbol)));
            const auto reason = pcb::notOnBoardReason(symbol);
            if (reason.isNotEmpty()) root->setProperty("notOnBoard", reason);
        }
        return juce::JSON::toString(juce::var(root), true);
    }
    if (name == "pcb_sync_from_schematic")
    {
        const auto report = pcbPanel->syncFromSchematic();
        auto* extra = new juce::DynamicObject();
        extra->setProperty("added", juce::var(report.added));
        extra->setProperty("updated", juce::var(report.updated));
        extra->setProperty("removed", juce::var(report.removed));
        extra->setProperty("skipped", juce::var(report.skipped));
        return reply(true, {}, false, extra);
    }
    if (name == "pcb_auto_place")
    {
        const auto notPlaced = pcbPanel->autoPlace();
        auto* extra = new juce::DynamicObject();
        extra->setProperty("didNotFit", juce::var(notPlaced));
        return reply(true, {}, false, extra);
    }
    if (name == "pcb_place_part" || name == "pcb_set_footprint")
    {
        auto next = pcbPanel->layoutState();
        const auto refdes = args.getProperty("refdes", "").toString();
        auto* part = next.find(refdes);
        if (part == nullptr)
            return reply(false, "No part " + refdes + " on the board; see pcb_layout_get (or pcb_sync_from_schematic first).", false, nullptr);
        if (name == "pcb_place_part")
        {
            part->at = { (double)args.getProperty("x", part->at.x), (double)args.getProperty("y", part->at.y) };
            if (args.hasProperty("rotation"))
            {
                const int r = (int)args.getProperty("rotation", 0);
                if (r != 0 && r != 90 && r != 180 && r != 270)
                    return reply(false, "Rotation must be 0, 90, 180 or 270.", false, nullptr);
                part->rotation = r;
            }
        }
        else
        {
            const auto footprint = args.getProperty("footprint", "").toString();
            const auto options = pcb::footprintsFor(part->symbolId);
            const int index = options.indexOf(footprint, true);
            if (index < 0)
                return reply(false, footprint + " does not fit " + part->symbolId + "; fits: " + options.joinIntoString(", ") + ".", false, nullptr);
            part->footprint = options[index];
        }
        next.clearRoute();
        pcbPanel->setLayout(next);
        return reply(true, {}, false, nullptr);
    }
    if (name == "pcb_set_route_rules")
    {
        auto next = pcbPanel->layoutState();
        applyRules(next);
        next.clearRoute();
        pcbPanel->setLayout(next);
        return reply(true, {}, false, nullptr);
    }
    if (name == "pcb_route")
    {
        auto next = pcbPanel->layoutState();
        applyRules(next);
        pcb::routeLayout(next, pcbPanel->design());
        pcbPanel->setLayout(next);
        const auto& l = pcbPanel->layoutState();
        return reply(l.routed, l.routeError, false, nullptr);
    }
    if (name == "pcb_verify_netlist")
    {
        const auto& v = pcbPanel->verify();
        return reply(v.matches, {}, false, nullptr);
    }
    if (name == "pcb_route_result")
    {
        const auto& l = pcbPanel->layoutState();
        return reply(l.routed, l.routeError, false, nullptr);
    }
    if (name == "pcb_clear_routes")
    {
        auto next = pcbPanel->layoutState();
        next.clearRoute();
        pcbPanel->setLayout(next);
        return reply(true, {}, false, nullptr);
    }
    if (name == "pcb_drc")
    {
        const auto markers = pcb::checkLayout(pcbPanel->layoutState(), pcbPanel->design());
        auto* extra = new juce::DynamicObject();
        extra->setProperty("drcViolations", (int)markers.size());
        extra->setProperty("drc", markersVar(markers));
        return reply(true, {}, false, extra);
    }
    auto layerOk = [](const juce::String& layer) { return pcb::artLayers().contains(layer); };
    auto layerError = juce::String("layer must be one of F.SilkS, B.SilkS, F.Cu, B.Cu.");
    auto pointList = [](const juce::var& v) {
        std::vector<pcb::Point> list;
        if (const auto* a = v.getArray())
            for (const auto& p : *a)
                if (const auto* xy = p.getArray(); xy != nullptr && xy->size() >= 2)
                    list.push_back({ (double)(*xy)[0], (double)(*xy)[1] });
        return list;
    };
    if (name == "pcb_text_add" || name == "pcb_text_edit")
    {
        auto next = pcbPanel->layoutState();
        pcb::BoardText t;
        int index = -1;
        if (name == "pcb_text_edit")
        {
            index = (int)args.getProperty("index", 0) - 1;
            if (index < 0 || index >= (int)next.texts.size())
                return reply(false, "No text " + juce::String(index + 1) + "; the board has " + juce::String((int)next.texts.size()) + ".", false, nullptr);
            t = next.texts[(size_t)index];
        }
        const bool wasCopper = t.layer.endsWith(".Cu") && index >= 0;
        if (args.hasProperty("text")) t.text = args.getProperty("text", "").toString();
        if (args.hasProperty("x")) t.at.x = (double)args.getProperty("x", 0.0);
        if (args.hasProperty("y")) t.at.y = (double)args.getProperty("y", 0.0);
        if (args.hasProperty("height")) t.height = (double)args.getProperty("height", t.height);
        if (args.hasProperty("line_width")) t.lineWidth = (double)args.getProperty("line_width", t.lineWidth);
        if (args.hasProperty("rotation")) t.rotation = (double)args.getProperty("rotation", 0.0);
        if (args.hasProperty("align"))
        {
            const auto a = args.getProperty("align", "").toString().toLowerCase();
            t.align = a.startsWith("l") ? -1 : a.startsWith("r") ? 1 : 0;
        }
        if (args.hasProperty("layer")) t.layer = args.getProperty("layer", "").toString();
        if (t.text.trim().isEmpty()) return reply(false, "text is empty.", false, nullptr);
        if (t.height <= 0.0 || t.lineWidth <= 0.0) return reply(false, "height and line_width must be positive.", false, nullptr);
        if (!layerOk(t.layer)) return reply(false, layerError, false, nullptr);
        if (index < 0) next.texts.push_back(t);
        else next.texts[(size_t)index] = t;
        if (wasCopper || t.layer.endsWith(".Cu")) next.clearRoute();
        pcbPanel->setLayout(next);
        auto* extra = new juce::DynamicObject();
        extra->setProperty("index", index < 0 ? (int)next.texts.size() : index + 1);
        const auto b = pcb::boundsOf(pcb::textArtwork(t));
        extra->setProperty("boundsMm", juce::Array<juce::var> { b.getX(), b.getY(), b.getRight(), b.getBottom() });
        return reply(true, {}, true, extra);
    }
    if (name == "pcb_graphic_add" || name == "pcb_graphic_edit")
    {
        auto next = pcbPanel->layoutState();
        pcb::BoardGraphic g;
        int index = -1;
        if (name == "pcb_graphic_edit")
        {
            index = (int)args.getProperty("index", 0) - 1;
            if (index < 0 || index >= (int)next.graphics.size())
                return reply(false, "No graphic " + juce::String(index + 1) + "; the board has " + juce::String((int)next.graphics.size()) + ".", false, nullptr);
            g = next.graphics[(size_t)index];
        }
        const bool wasCopper = g.layer.endsWith(".Cu") && index >= 0;
        if (args.hasProperty("kind")) g.kind = args.getProperty("kind", "").toString().toLowerCase();
        if (args.hasProperty("points")) g.points = pointList(args.getProperty("points", {}));
        if (args.hasProperty("radius")) g.radius = (double)args.getProperty("radius", 0.0);
        if (args.hasProperty("start_angle")) g.startAngle = (double)args.getProperty("start_angle", 0.0);
        if (args.hasProperty("end_angle")) g.endAngle = (double)args.getProperty("end_angle", 0.0);
        if (args.hasProperty("line_width")) g.lineWidth = (double)args.getProperty("line_width", g.lineWidth);
        if (args.hasProperty("filled")) g.filled = (bool)args.getProperty("filled", false);
        if (args.hasProperty("layer")) g.layer = args.getProperty("layer", "").toString();
        const juce::StringArray kinds { "line", "rect", "circle", "arc", "polygon" };
        if (!kinds.contains(g.kind)) return reply(false, "kind must be line, rect, circle, arc or polygon.", false, nullptr);
        if (!layerOk(g.layer)) return reply(false, layerError, false, nullptr);
        if (g.lineWidth <= 0.0) return reply(false, "line_width must be positive.", false, nullptr);
        const size_t need = g.kind == "polygon" ? 3 : (g.kind == "line" || g.kind == "rect") ? 2 : 1;
        if (g.points.size() < need) return reply(false, g.kind + " needs " + juce::String((int)need) + " point(s).", false, nullptr);
        if ((g.kind == "circle" || g.kind == "arc") && g.radius <= 0.0) return reply(false, g.kind + " needs a positive radius.", false, nullptr);
        if (index < 0) next.graphics.push_back(g);
        else next.graphics[(size_t)index] = g;
        if (wasCopper || g.layer.endsWith(".Cu")) next.clearRoute();
        pcbPanel->setLayout(next);
        auto* extra = new juce::DynamicObject();
        extra->setProperty("index", index < 0 ? (int)next.graphics.size() : index + 1);
        return reply(true, {}, true, extra);
    }
    if (name == "pcb_text_remove" || name == "pcb_graphic_remove")
    {
        auto next = pcbPanel->layoutState();
        const bool isText = name == "pcb_text_remove";
        const int index = (int)args.getProperty("index", 0) - 1;
        const int count = isText ? (int)next.texts.size() : (int)next.graphics.size();
        if (index < 0 || index >= count)
            return reply(false, "No " + juce::String(isText ? "text " : "graphic ") + juce::String(index + 1) + ".", false, nullptr);
        const auto layer = isText ? next.texts[(size_t)index].layer : next.graphics[(size_t)index].layer;
        if (isText) next.texts.erase(next.texts.begin() + index);
        else next.graphics.erase(next.graphics.begin() + index);
        if (layer.endsWith(".Cu")) next.clearRoute();
        pcbPanel->setLayout(next);
        return reply(true, {}, true, nullptr);
    }
    if (name == "pcb_set_fab_rules")
    {
        auto next = pcbPanel->layoutState();
        auto& f = next.fab;
        if (args.hasProperty("mask_expansion")) f.maskExpansion = std::max(0.0, (double)args.getProperty("mask_expansion", f.maskExpansion));
        if (args.hasProperty("paste_reduction")) f.pasteReduction = std::max(0.0, (double)args.getProperty("paste_reduction", f.pasteReduction));
        if (args.hasProperty("silk_line_width") && (double)args.getProperty("silk_line_width", 0.0) > 0.0) f.silkLineWidth = (double)args.getProperty("silk_line_width", 0.0);
        if (args.hasProperty("label_height") && (double)args.getProperty("label_height", 0.0) > 0.0) f.labelHeight = (double)args.getProperty("label_height", 0.0);
        if (args.hasProperty("tent_vias")) f.tentVias = (bool)args.getProperty("tent_vias", true);
        if (args.hasProperty("part_labels")) f.partLabels = (bool)args.getProperty("part_labels", true);
        pcbPanel->setLayout(next);
        return reply(true, {}, true, nullptr);
    }
    if (name == "pcb_export_fab")
    {
        const auto r = pcbPanel->exportFab();
        auto* extra = new juce::DynamicObject();
        extra->setProperty("filesOk", r.ok);
        extra->setProperty("readyForFab", r.readyForFab);
        extra->setProperty("summary", r.summary);
        extra->setProperty("folder", r.folder.getFullPathName());
        extra->setProperty("zip", r.zip.getFullPathName());
        extra->setProperty("files", juce::var(r.files));
        extra->setProperty("checks", juce::var(r.checks));
        extra->setProperty("problems", juce::var(r.problems));
        return reply(r.ok, r.ok ? juce::String() : r.summary, false, extra);
    }
    return reply(false, "Unknown PCB tool " + name + ".", false, nullptr);
}













