# Electronics Workbench — Handoff Brief (Claude → Gemini, 2026-10-11)

This brief is for an agent that has not seen the earlier sessions. It tags each statement as one of:
- **[verified]**: established by tests or by running the app.
- **[code]**: read in the source, not separately tested.
- **[suspected]**: an inference that you need to confirm.

---

## 1. Repository and current state

| Item | Value |
|---|---|
| Repository | `https://github.com/wwestlake/lagdaemon-tech-research.git`, checked out at `D:\000 Tech Research` |
| Workbench project | `D:\000 Tech Research\Electronics Design Tool\prototype` (everything below is relative to it unless stated) |
| Branch | `master` (pushing directly to master is the established practice here) |
| Latest pushed commit | `980698a` P2a (2/2): DC motor in the Workbench; analyses off the UI thread |
| Before it | `7adac00` P2a (1/2): electromechanical foundation and the brushed DC motor in circuit_sim |
| Earlier, same session | `cd2cb55`, `401dd0f`, `6099623` (logic gates); `4e2d5f6`, `87c6680` (P1 device event timing); `de578f0`, `8b1bbf7` (programmable components) |
| Working tree | Clean for tracked files at `980698a` |

**Untracked files you must not touch** (they belong to other work or to the user):
- Under `D:\000 Tech Research`: `Conversations/` and `Electronics Design Tool/HANDOFF_REPORT.md`.
- Under `prototype/`: `.frust/`, `bordodynov.html`, `crash_log.txt`, `dsp_test.cpp`, `fix.py`, `lib.zip`, `out.txt`, `patch.ps1`.
- In `D:\FrustLang`: `sim/` and `status.txt`.

**Related repositories**
- **FrustLang**, at `D:\FrustLang` (its own git repo, branch `development`). It supplies the FRust language, runtime, `frate`, `ai_provider` and **CreationDock** (`D:\FrustLang\projects\03_creation_dock`). The Workbench pulls them in through `FRUSTLANG_ROOT` in `prototype/CMakeLists.txt`.
  - **FrustLang has been treated as read-only for Workbench work.** Changing it, CreationDock included, needs the user's explicit authorisation. FrustLang has its own `AGENTS.md`; read it before any git action there.
- **Xyce**, the external SPICE engine, is optional and read-only. `prototype/tools/xyce/` holds helpers.
- **vcpkg LLVM** at `D:/000 Creation Suite/apps/CreationEngine/vcpkg_installed/x64-windows` and **WinFlexBison** at `D:/tools/winflexbison` are machine installs; never vendor them into the repo.

### Build (Debug only, single core, explicit target, one build at a time)

```powershell
cmake --build "D:\000 Tech Research\Electronics Design Tool\prototype\build" --config Debug --target DjehutiElectronicsLab -- /m:1 /v:minimal
```

Never build without `--target`, and never use parallel builds: this is the user's own machine. The app builds to `prototype\bin\Debug\djehuti_electronics_lab.exe`.

Test targets:
- `CircuitSolverTests`
- `SimCoreTests` / `SimCoreTestsUnopt`
- `CircuitStepperTests` / `CircuitStepperTestsUnopt`
- `LogicGateTests` / `LogicGateTestsUnopt`
- `MotorTests` / `MotorTestsUnopt`
- `AudioDspTests`
- `SchematicRoutingTests`
- `PlotInstrumentTests`
- `KnowledgeTests` (about 100 s)
- `FrustDebuggerTests`
- `ProgrammableComponentTests`
- `NodeDesignerTests`
- `GeneratedGainSmoke`
- `CookbookValidation`

If a link fails with `LNK1104` (cannot open file), retry the build. It has been a transient file lock, not a code problem.

### Tests

```powershell
cd "D:\000 Tech Research\Electronics Design Tool\prototype\build"
ctest -C Debug
```

Run one test with `ctest -C Debug -R MotorTests`. The two motor test builds can be compared directly: `bin\Debug\motor_tests.exe --compare <outA> <outB>`. The gate and stepper tests have the same `--compare` mode.

**Transient digests (bit-identical regression):** set the environment variable `CIRCUIT_SIM_TRANSIENT_DIGEST=<file>`, then run `circuit_solver_tests`, `audio_dsp_tests_3`, `plot_instrument_tests`, `knowledge_tests` and `programmable_component_tests`. Each transient appends one digest line to that file. Sort each file and compare it against a baseline taken before your change. Claude's script for this was in its session scratchpad and is gone; it's about ten lines to recreate.

### Launching the app for testing

```powershell
Start-Process "D:\000 Tech Research\Electronics Design Tool\prototype\bin\Debug\djehuti_electronics_lab.exe" -WorkingDirectory "D:\000 Tech Research\Electronics Design Tool\prototype\bin\Debug" -PassThru
```

- Record the PID and close only that process when you finish. **Never touch an instance the user started.**
- **Do not launch the app under `cdb` (or any debugger) for normal testing** [verified]. A process started under a debugger gets the Windows debug heap. The solver allocates in every Newton iteration, so a 2.3 s transient took more than 10 minutes. This froze the user's screen twice.
- Drive the app through the **local agent API**:
  - the discovery file is `%APPDATA%\DjehutiElectronicsLab\agent-api.json` (`baseUrl`, `token`);
  - `POST {baseUrl}/v1/tools/call` with body `{"name": tool, "arguments": {...}}` and header `Authorization: Bearer <token>`;
  - the response JSON has a `result` field;
  - `POST /v1/messages` with `{"content": "..."}` sends a plain-language task to the in-app agent and returns a `requestId`; poll `GET /v1/requests/<id>`.
  - The API serves **one request at a time**, and a gateway times out after about 120 s.

---

## 2. Architecture and implementation

Paths below are under `prototype/Source/` unless stated.

### Application and UI (JUCE)
- `Main.cpp` is the entry point.
- `ElectronicsWorkbench.cpp/.h` (about 20,000 lines) is the main component. It contains:
  - the schematic model (instances, wires, probes) and the netlist builder (`buildSimNetlist`, about line 7870);
  - the palette (`ComponentLibraryPanel::add({...})`, about line 1840);
  - the ERC checks (about line 2800);
  - the agent panel (`AgentPanel`: `startLocalApi`, `executeToolNow`, `executeToolFromWorker`; about lines 14,000–15,000);
  - every agent tool handler (`schematicTool`, `analyticsTool`, `projectTool`, `nodeProgramTool`, `pcbLayoutTool`...) and the tool-spec tables (`schematicToolSpecs`, `analyticsToolSpecs`).
- Docking comes from **CreationDock** (FrustLang repo), included as `<CreationDock/DockManager.h>`.
- Other panels:
  - `AnalyticsPanel` (analyses);
  - `NodeDesignerPanel` (node programs);
  - `FrustPanel`, `FrustDebuggerPanel`;
  - `PcbPanel`;
  - `PlotInstrumentView` (plotter instrument);
  - `CircuitHierarchyPanel` (sub-diagrams);
  - `PinLayoutEditor`;
  - `Preferences`.

### Schematic, components and symbols
- `SchematicSymbols.cpp/.h`:
  - `supportedSymbolIds()` is the component registry;
  - `symbolFor(id)` gives pins and bounds;
  - `refdesPrefixFor`, the drawing code (`id == "..."` branches), and logic-gate symbols built from an input count (`logicGateSymbol`).
- `PartCatalog.cpp/.h` holds each part's parameters (`ParamSpec`: quantity, choice, text, toggle; with units, defaults and help), `displayName`, `simulationFidelity` and validation.
- Supporting files:
  - `data/component_seed.json` (palette seed);
  - `PcbLayout.cpp` (footprints and pin maps);
  - `SchematicLayout`, `SchematicRouter` and `RouteEditing` (wire routing, built on libavoid);
  - `CircuitHierarchy` (sub-diagram blocks and ports).
- A part's registration points: the palette, `archetypeFor`, `defaultValue`, the netlist branch, ERC exceptions, the catalog, symbols, seed and footprints. Grep an existing id such as `relay_spst` to see every one.

### Electrical solver (`libs/circuit_sim`, sources in `Source/`)
- `CircuitSolver.cpp/.h`:
  - modified nodal analysis with a dense LU and Newton (junction limiting; `maxStepVolts` clamp; gmin and source stepping; an adaptive-gmin last resort);
  - elements: R, L, C, coupling, V and I sources (DC, sine, square, pulse, PWL, exp), E/G/H/F, diode, BJT, MOSFET, JFET, op amp, switches, and `Programmable` (a `ProgrammableDevice` interface evaluated at every Newton iteration with a finite-difference Jacobian);
  - behavioural B-sources are Xyce-only.
- Analyses: operating point, DC sweep, AC (linearise), noise, transient, sensitivity and others; `Analytics.cpp` lists them.
- **Transient:** `TransientStepper` (resumable; trapezoidal, with a two-half-step backward-Euler restart after discontinuities; supports rollback with `saveState`/`restoreState`) and `solveTransient` (offline), which lands exactly on source corners.
- **P1 device events** [verified]:
  - a device reports `nextEventTime`; the stepper splits at it, restarts, and records the sample there;
  - a step that schedules an event inside itself is rolled back and re-taken to land on it;
  - a zero-time loop is reported as an error after 64 landings.
- **Logic gates:** `LogicGates.cpp/.h` (`GateDevice`). One shared truth function; levels behind an output resistance; threshold with hysteresis; inertial `tpd`; output transition time `tr`.
- **Mechanical (P2a):** `Mechanical.cpp/.h`; see below.
- The P0 scheduler and its participant (`ElectricalParticipant`, `sim_core_tests`) are used only by the system-simulator tests. The app uses `solveTransient`.

### Internal solver versus Xyce
- `XyceBackend.cpp/.h` lowers the netlist to a Xyce deck and runs it as an external process.
- `AnalyticsPanel::prepare()` chooses the engine. Any `Programmable` element forces the internal solver: FRust components, logic gates (`modelName` `LOGIC_*`) and motor shafts (`MECH_SHAFT`).
- `programmableNote()` in `AnalyticsPanel.cpp` adds a note to the result saying why. There is **no co-simulation** and no state exchange with Xyce.

### FRust, Node Designer and programmable components
- `FrustEngine`, `FrustExecution`, `FrustPanel` and `FrustDebuggerPanel` compile and run FRust inside the app, using the FrustLang libraries.
- `NodeCompiler.cpp/.h` turns node graphs into FRust source. **Node ids become FRust identifiers** (see the defects in section 4).
- `NodeDesignerPanel.cpp/.h` is the graph editor, the Nodes list and `validateGraph`.
- `FrustComponent.cpp/.h` holds programmable-component definitions in the project: pins with roles, plus a program. Its device runs the compiled program at every Newton iteration through the pin nodes (`pc_pin_voltage`, `pc_drive`, `pc_set_resistance`, `pc_state_*`).
- Agent tools for this: `component_create/compile/place/open_program` and `node_program_*`. `node_program_node_types` lists the node types.

### Agent integration and knowledge
- `CapabilityCatalog.cpp` produces `workbench_capabilities`: sections components, instruments, analyses, engines, programmable, logic and electromechanical, with query matching.
- Guidance cards in `knowledge/cards/*.jsonl` are loaded by `ElectronicsKnowledge.cpp`; there are tool cards and process cards.
- `LocalAgentApi.cpp` is the HTTP API (a single thread that accepts connections one at a time).
- Long-running tools use `agentTools.longTool` (`ElectronicsWorkbench.cpp`, about line 15,536) with a completion callback. Currently: `frust_run`, `node_program_run`, `node_debug_*`, `pcb_route` and all `analytics_<analysis>` tools.
- The in-app agent model is chosen in the app's settings; the default was gpt-4o. `ai_provider` comes from FrustLang.

### P2a electromechanical (`Mechanical.cpp/.h`) [verified]
- **Mobility analogy, all in the one circuit:**
  - the shaft node's value is ω (rad/s);
  - current into it is torque (N·m);
  - capacitance to ground is inertia J;
  - conductance to ground is damping B;
  - a current source out of it is the load torque (any `Waveform`, PWL steps included).
- `addDcMotor()` builds, all named `M1.*`:
  - an armature: Ra, then La (its branch current is I; with La = 0 it's a 0 V sense source), then a VCVS for the back EMF (Ke·ω);
  - a CCCS for the torque Kt·I, J (initial condition ω0), B and the load.
- `ShaftDevice` is a `ProgrammableDevice` on the shaft and angle nodes. It covers:
  - the angle θ (trapezoidal, implicit inside Newton, saved and restored);
  - Coulomb friction (tanh-smoothed, no stiction);
  - end stops (a spring-damper that only pushes).
- `addTachometer()`: T+ − T− = k·ω behind Rout.
- **Solver additions, narrowly scoped:**
  - `Circuit::addQuantityNode()`: the Newton voltage clamp doesn't apply to these nodes;
  - `Element::holdInitialCondition`: the transient's initial operating point holds the motor's speed and current at their initial values, and the first step is a backward-Euler restart.
- Derived quantities (`mech::Quantity`): speed, rpm, position, current, voltage, backemf, torque, power_in, power_copper, power_converted, power_damping, power_load, energy_kinetic. In the Workbench they are analysis outputs named `M1.speed` and so on. `Analytics.cpp` (`Output::quantity`, `unitOf`) handles them in DC, transient, AC (linear ones only) and measurements. The operating point shows them in an "Electromechanical quantities" table.
- Parts are `dc_motor` and `dc_motor_tach`. Parameters are in `PartCatalog.cpp`; the netlist branch is `id == "dc_motor"` in `buildSimNetlist`.

### Analytics, CSV and visualisation
- `Analytics.cpp/.h`:
  - analysis definitions (`fieldsFor`), `run`, output parsing (`outputsOf`: `V(net)`, `I(part)`, quantity names), plots, tables and measurements;
  - `toCsv` and `toJson`.
- `AnalyticsPanel.cpp/.h`:
  - `prepare` (gathers inputs on the message thread), `compute` (anywhere), `writeFilesTo` (CSV and `result.json` under `<project>/outputs/<diagram>/analytics/<timestamp>_<analysis>/`) and `finishRun`;
  - `runSelected` (the Run button) and `runAsync` (agent tools) both run on the panel's `juce::ThreadPool pool{1}`;
  - `runNow` (synchronous) still exists, but agent analyses no longer use it.

### Save and load
- `ProjectStore.cpp/.h` and `ElectronicsWorkbenchProjects.cpp` handle projects (a folder of diagrams, component definitions and outputs).
- Diagrams are JSON and keep instances with their parameters, wires and probes.
- Agent tools: `project_create/open` and `diagram_create/open/save`.
- [verified] Motors, gates and controllers survive save and reopen.

---

## 3. Outstanding UI repairs (not started)

None of these has been investigated beyond the notes below. Every location here is **[suspected]**.

1. **Backspace and Delete in text fields.**
   - Likely cause: a global key handler (`keyPressed` on the main component, or a `KeyListener` or `ApplicationCommandManager` mapping Delete/Backspace to "delete selected part") consumes the key before the focused `juce::TextEditor` gets it.
   - Look in `ElectronicsWorkbench.cpp` for `keyPressed`, `KeyPress::deleteKey`, `backspaceKey` and `addKeyListener`. Also check CreationDock's DockManager for key handling.
   - Fix: let text-entry components keep their keys. For example, return false when a `TextEditor` (or any component that wants keyboard focus) has focus.
2. **Node Designer Nodes list scrolling.** In `NodeDesignerPanel.cpp`, the Nodes list probably isn't inside a `juce::Viewport`, or its content height isn't set, or the canvas steals `mouseWheelMove`. Make the list a viewport whose content is sized to every item, and check it at narrow widths.
3. **Docking tab overflow arrows.** This lives in **CreationDock** (`D:\FrustLang\projects\03_creation_dock`). It needs left/right buttons when the tab strip is wider than the panel, scrolling that keeps the active tab visible on selection, and no change to selecting or reordering tabs.
   - **FrustLang needs the user's write authorisation before you edit CreationDock.** Ask first. Commit in the FrustLang repo (branch `development`; follow its `AGENTS.md`), then rebuild the Workbench, which builds CreationDock through `add_subdirectory`.
4. **Clear (×) on search fields:** the palette or component search in `ComponentLibraryPanel` (`ElectronicsWorkbench.cpp`), the Node Designer node search, and any other `TextEditor` used for searching. Use a small button inside or next to the editor that empties it and refreshes the filter.
5. **Tab drag threshold of about 8 px.** Also in **CreationDock**, so the same authorisation is needed. Start a reorder or undock only after `e.getDistanceFromDragStart() > 8`; a click with a small movement must just select the tab.

The sixth item from the user's list, analysis responsiveness, **is done** in `980698a` [verified]. The window kept responding (Windows `Responding = True`) through a 19 s, 200,000-step run.

---

## 4. Known defects and limitations

Confirmed defects (not fixed):
- **Slow CSV writing in Debug builds** [verified]. `analytics::toCsv` builds the file by appending to a `juce::String`; 40,000 samples × 5 traces took minutes. It now runs on the worker, so the UI no longer freezes, but the writer itself is still slow. Fix it separately; keep the default `max_points` at 4000.
- **Node ids become FRust variable names without keyword escaping** [verified]. A node with id `out` gives "syntax error, unexpected out" (`NodeCompiler.cpp`). Fix it by escaping or renaming reserved words.
- **The local agent API handles one request at a time** [verified]. A second call waits for the first. Requests time out after about 120 s.
- **The part catalog rescans the SPICE models folder on every lookup** [code]. `parts::catalog()` calls `spice_library::initialize()`, and `partValue()` goes through it, so hot paths must not call `partValue`.
- **Opening the diagram you're already in reverts it to the saved file, silently losing unsaved edits** [verified].
- **The debugger's heap slows simulation by hundreds of times** [verified]. This is environmental, not a code bug; see section 1.

Deliberate limitations of P2a:
- One shaft per motor: no gears, couplings or shared shafts (the shaft-node design allows them later).
- No stiction, cogging, brush drop or saturation, and no thermal model.
- No position encoder part.
- Motors run only on the internal solver; Xyce falls back.
- At t = 0 with an initial current of 0, the terminal voltage shows the held state, for example 0 V on an open circuit; it follows within nanoseconds. This is expected.

Earlier limitations:
- Logic gates have no supply pins; there are no flip-flop parts; gate footprints exist only for 1- and 2-input gates.
- The stepper's real-time CTest check is sensitive to machine load.

---

## 5. Verification baseline (as of `980698a`, not rerun)

- `MotorTests` 40/40 and `MotorTestsUnopt` 40/40; `--compare` 6/6 with largest difference 0. Source: `tools/motor_tests/MotorTests.cpp`. The checks are against an independent exact solution (a 2×2 matrix exponential):
  - startup and steady state;
  - an operating point at 11,000 rad/s through a diode;
  - load step and reversal;
  - back EMF on open terminals, and generating with an overhauling load;
  - load inertia; viscous and Coulomb coast-down; end-stop stall;
  - energy balance (residual 1.7e-6 J of 11.2 J);
  - an edge driven exactly by a logic gate, and PWM with a freewheel diode;
  - bit-identical reruns and rollback;
  - a proportional controller through a tachometer;
  - failure paths.
- Transient digests: all 24 existing transients are bit-identical to the pre-motor (`after_gates`) baseline.
- CTest: 18/18.
- In the app [verified]:
  - operating point 597.015 rad/s at 29.85 mA (exact);
  - startup transient deviations 4.9e-7 (speed) and 5e-5 (current), relative to scale;
  - load step 586.4 → 348.8 rad/s and 0.137 → 2.512 A (exact);
  - reversal passes zero at 0.2342 s and reaches −594.31 rad/s at 0.5 s (exact);
  - inertia 0 is refused with M1 named;
  - save and reopen keep the parameters and give an identical steady state;
  - a FRust controller (`DRIVE = clamp(20·(3 − TACH), 0, 12)`) regulates to 272.5 rad/s;
  - the backend note names DC motors and FRust components.
- Agent task (gpt-4o, no tool names given; "12 V motor, add 0.05 N·m at 200 ms, report speed and current before the load and at 500 ms"):
  - its circuit and simulation were correct;
  - it reported the 500 ms values correctly (348.81 rad/s, 2.51 A);
  - it reported the pre-load values from about 187 ms (583.5 rad/s, 0.166 A) instead of 200 ms (586.6, 0.135). The guidance now says to read the CSV row nearest the requested time.
- Logic gates: LogicGateTests 27/27 in both builds; in the app, a NAND truth table and an SR latch with exact edges, before and after save and reopen.
- P1 events: CircuitStepperTests 51/51 (optimised) and 50/50 (unoptimised).

---

## 6. Your first major assignment: a complete capability inventory

Before building anything substantial, produce an evidence-based inventory of **everything the Workbench supports today**: the whole application, not just the motor work.

For each feature, record:
- name and purpose;
- what it actually does and which operations it supports;
- where it is in the UI and how it's used;
- its inputs, parameters, outputs and supported configurations;
- the implementation files and tests;
- its dependencies and how it works with other features;
- its status: **verified**, **implemented but unverified**, **partial**, or **planned**;
- known restrictions and defects.

Cover at least:
- the schematic system (placement, wiring, routing, sub-diagrams, labels and buses, ERC);
- every component in `supportedSymbolIds()` with its parameters and simulation fidelity;
- the electrical models;
- every analysis in `Analytics.cpp` and both engines;
- instruments (oscilloscope, DMM, Bode, plotter);
- the Node Designer, FRust execution and debugging, and programmable components;
- every agent tool (the tool-spec tables in `ElectronicsWorkbench.cpp`);
- the knowledge cards and capability catalog;
- persistence and projects;
- PCB layout, routing and fabrication export (`Pcb*.cpp`);
- audio DSP, VST generation and live audio (`AudioDsp*`, `AudioPipeline`, `GeneratedVst`);
- mechanical devices;
- export facilities;
- anything else you find.

Work from the code and from the running app, not from README files or existing documents; this repo's docs are often stale. Don't assume a planned feature exists. **Gather facts only; don't implement anything during the inventory.** Write it up so that ChatGPT can turn it into *"Electronics Workbench — Complete Capabilities and Technical Reference"*.

---

## 7. Working practices

- Make small, focused changes, and don't refactor unrelated code.
- Use practical, targeted tests, not exhaustive campaigns. A successful build is not verification, and neither is an agent's own account of its results.
- Build in Debug, on one core, one build at a time, always with an explicit target.
- Don't run performance-sensitive Debug builds under `cdb`.
- Never disturb an app instance that belongs to the user, and close your own.
- Never run anything that can freeze or crash the user's desktop. When the user says stop, stop immediately: run no further commands.
- Preserve existing behaviour and diagram connectivity; existing transient digests must stay bit-identical unless the user approves a change.
- Make no architectural changes without approval: present the alternatives and ask first.
- FrustLang (including CreationDock) and Xyce are read-only unless the user authorises changes.
- Windows only.
- Commit and push work once it is verified; never force-push and never use `--no-verify`.
- Always separate what tests established from what you assume.

**Recommended order:** the capability inventory first. Then UI repairs 1, 2 and 4 in the Workbench. Then ask for CreationDock authorisation and do repairs 3 and 5. Then the defects in section 4, CSV writer and node-id escaping first.
