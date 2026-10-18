# Handoff Report: DEWB — DCAD Motion Integration Refinement

**Date**: 2026-10-18  
**Repository**: `D:\000 Tech Research`  
**Application**: `Electronics Design Tool/prototype`  
**Branch**: `master` (merged from `feat/dewb-dcad-motion-refinement`, pushed to `origin/master`)

---

## 1. Summary of Changes & Fixes

All four core correctness issues in `DcadMotionClient` have been resolved and verified with 73 unit tests in `dcad_motion_tests.exe`:

1. **Real Declared Metadata (No Name/Substring Heuristics)**:
   - Eliminated `lower.find(...)` guessing in `DcadMotionClient::refreshCatalogFromActiveSession()`.
   - Populated `SignalDef` and `CompiledAssemblyCircuit::SignalMeta` with authoritative `quantity` (`angular_velocity`, `angle`, `voltage`, etc.), `unit` (`rad/s`, `rad`, `V`, etc.), and `kind` (`"rate"` vs `"state"`).
   - Only active simulation outputs produced by the active `SystemSimulationSession` (e.g. `setupDefaultMotorSystem()` or compiled `SystemAssembly`) are advertised in the `SignalInterface`. Generic non-existent signals (`carriage.position`, `spindle.speed`) have been removed.

2. **No Invented Motion Fallbacks**:
   - Removed artificial test motion fallbacks (fake 30 rpm constant rotation, synthetic sine waves, or zero substitutions).
   - If a mapped signal is missing or non-finite during coordinate evaluation, an actionable error is logged and coordinate generation returns `false`. `sendStateFrame` skips publishing invalid frames without crashing or halting the underlying simulation solver.

3. **Coordinates Derived Strictly in Simulation Time**:
   - Trapezoidal integration now runs exclusively against accepted simulation steps: $\Delta t_{sim} = t_{sim} - t_{last}$.
   - Removed the fixed 33 ms wall-clock step assumption.
   - When the simulation is paused or stopped ($\Delta t_{sim} = 0$), movement is strictly zero.
   - For companion positions: `mech::ShaftDevice` in `mech::addDcMotor` computes exact trapezoidal integration of shaft speed into `motor.angle` at every accepted solver step. When `motor.speed` is bound, `DcadMotionClient` prefers and synchronizes with the companion `motor.angle` signal, completely eliminating numerical drift.

4. **Preserved State Across Transports & Rebinding**:
   - `connectToPipe()` and `disconnect()` preserve existing `rateIntegrals_`, `timelineGeneration_`, and `sequence_`. Reconnecting to a new pipe server maintains the ongoing simulation timeline and accumulated coordinates.
   - `handleBindingProposal()` retains coordinates for already-mapped joints and sends a new `SynchronizeState` on the current timeline rather than re-zeroing the joint.
   - Mid-run binding of rate sources without historical integration or companion position is rejected cleanly with `LinkStatus` error code `integrationUnavailable`, and `LinkState` reverts to `Advertising` rather than a fatal error, allowing CAD to adjust its proposal.

---

## 2. Test Verification

- Target: `DcadMotionTests` (`dcad_motion_tests.exe`)
- Result: **73 / 73 checks passed**.
  - Verified catalog from active session contains real `motor.speed` and `motor.angle`, zero synthetic signals.
  - Verified proposal rejection on unknown signals (`unknownSignal`).
  - Verified proposal acceptance and state synchronization on valid `motor.speed` binding.
  - Verified paused stationary state ($\Delta t_{sim} = 0 \implies \Delta \theta = 0$).
  - Verified disconnect and reconnection preserves accumulated coordinates and timeline generation.
- Full application target: `DjehutiElectronicsLab` compiled and linked cleanly with single-core MSBuild (`/m:1`).

---

## 3. Operating DEWB & DjehutiCAD Together (Codex Instructions)

### A. Recommended Models
- **DEWB Model**: Built-in default motor system (`setupDefaultMotorSystem()`) or `examples/model_driven_motor/nominal/saved_motor_controller.machine.json`.
- **DjehutiCAD Assembly**: `D:\DjehutiCAD\examples\shaft_carriage\shaft_and_carriage.dcad.json` (or any assembly with a continuous revolute joint `joint-spindle`).

### B. Valid Mappings
| CAD Joint ID | DEWB Signal Name | Quantity | Unit | Kind | Scale / Offset |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `joint-spindle` | `motor.speed` | `angular_velocity` | `rad/s` | `rate` | `scale: 1.0`, `offset: 0.0` |
| `joint-spindle` | `motor.angle` | `angle` | `rad` | `state` | `scale: 1.0`, `offset: 0.0` |

### C. Step-by-Step Procedure
1. **Start DjehutiCAD**:
   - Open DjehutiCAD and load the shaft and carriage assembly.
   - Open the **Motion Link** dock panel and click **Start Listening**. Note the generated pipe endpoint (written to `%LOCALAPPDATA%\DjehutiCAD\MotionEndpoints\*.json`).
2. **Start DEWB**:
   - Launch `bin/Debug/djehuti_electronics_lab.exe`.
   - Open the default motor machine or run the default motor simulation from the Control Panel.
   - In the Motion Link dialog, connect to the discovered CAD endpoint.
3. **Binding & Execution**:
   - DEWB advertises `motor.speed` (`angular_velocity`, `rad/s`, `rate`) and `motor.angle` (`angle`, `rad`, `state`).
   - CAD proposes mapping `joint-spindle` to `motor.speed` or `motor.angle`.
   - DEWB accepts, issues `SynchronizeState`, and begins streaming `StateFrame`s.
   - Test lifecycle controls (Play, Pause, Step, Stop) from either CAD or DEWB to verify joint motion and stationary pause states.
