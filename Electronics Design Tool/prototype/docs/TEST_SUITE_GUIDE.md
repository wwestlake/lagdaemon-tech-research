# Djehuti Electronics Lab — Test Suite Architecture & Validation Guide

## 1. Permanent Validation Policy (MANDATORY)

**A successful targeted test is sufficient for an ordinary localized change.**

1. **Determine test impact from actual changed files and dependencies.**
2. **Build only the affected targets.**
3. **Run only tests relevant to the changes.**
4. **NEVER automatically build or execute monolithic / extended simulation suites** (e.g. `MachineBuilderSmokeTests`).
   These suites run heavy ODE solvers, numerical motor optimization loops, and multi-second physical stepping that tie up the machine for hours. They are strictly opt-in and must NEVER be run unless explicitly ordered by the user.
5. **Never run exhaustive solver testing unless explicitly authorized.**
6. **Never rebuild an unchanged target merely to repeat validation.**
7. **Never run Debug and Release test matrices without a specific justification.**
8. **Do not repeatedly poll build logs or narrate routine build progress.**
9. **For UI changes, run ONLY the lightweight UI test suite.**
10. **Single-core builds only (`/m:1`).**

---

## 2. Test Targets & Responsibilities

| Test Target | Binary Name | Subsystem / Scope | Approx Runtime | When to Run |
|---|---|---|---|---|
| **`ControlPanelUiTests`** | `bin/control_panel_ui_tests.exe` | Operator console, multi-panel creation/switching/renaming/removal, widget authoring (+ Add Element), widget bounds & placement, properties dialog, splitter & plot mouse isolation, layout JSON persistence | **< 50 ms** | Any change to `ControlPanelComponent.*`, `ControlPanelModel.*`, or operator panel UI |
| **`MachineBuilderUiTests`** | `bin/machine_builder_ui_tests.exe` | Graphical assembly UI, toolbar groups & overflow, tooltips, zoom/pan navigation, system ports, connection wiring, `.machine.json` model save/load, template reset | **< 150 ms** | Any change to `MachineBuilderPanel.*`, assembly graph UI, or `.machine.json` format |
| **`FastSmokeTests`** | `bin/fast_smoke_tests.exe` | Multi-subsystem integration sanity check (Circuit solver DC, Control panel model, Machine design model, Schematic symbols) | **< 80 ms** | Routine smoke check before commits |
| **`CircuitSolverTests`** | `bin/circuit_solver_tests.exe` | Linear circuit solver matrix formulation, node voltages, RLC transients | **< 1 s** | Changes to `CircuitSolver.*` |
| **`PlotInstrumentTests`** | `bin/plot_instrument_tests.exe` | Probes, signal acquisition thinning, 2D/3D visual plotting instruments | **< 1 s** | Changes to `PlotInstrument.*` or `PlotInstrumentView.*` |
| **`SchematicRoutingTests`** | `bin/schematic_routing_tests.exe` | Wire routing, collision avoidance, schematic connectivity | **< 1 s** | Changes to schematic layout or router |
| **`ProgrammableComponentTests`** | `bin/programmable_component_tests.exe` | FRust programmable components & simulation hooks | **< 2 s** | Changes to `FrustComponent.*` |
| **`NodeDesignerTests`** | `bin/node_designer_tests.exe` | Node Designer panel and node compilation | **< 2 s** | Changes to `NodeDesignerPanel.*` or `NodeCompiler.*` |
| **`MachineBuilderSmokeTests`** | `bin/machine_builder_smoke_tests.exe` | **EXTENDED SIMULATION ONLY (OPT-IN)**: 100+ end-to-end physics simulation runs, ODE integration, motor optimization loops | **10 - 60+ min** | **NEVER RUN unless specifically ordered by the user.** |

---

## 3. How to Build and Run Targeted Tests

All builds must use single-core `/m:1` and Debug configuration:

### For Control Panel / UI fixes:
```powershell
cmake --build build --config Debug --target ControlPanelUiTests -- /m:1
.\bin\Debug\control_panel_ui_tests.exe
```

### For Machine Builder assembly fixes:
```powershell
cmake --build build --config Debug --target MachineBuilderUiTests -- /m:1
.\bin\Debug\machine_builder_ui_tests.exe
```

### For general integration sanity check:
```powershell
cmake --build build --config Debug --target FastSmokeTests -- /m:1
.\bin\Debug\fast_smoke_tests.exe
```
