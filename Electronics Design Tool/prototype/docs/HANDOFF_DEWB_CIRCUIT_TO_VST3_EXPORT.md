# HANDOFF: DEWB Circuit-to-VST3 Export System

**Date**: 2026-10-16  
**Status**: Core Architecture, Code, Precompiled Object Loader, Runtime, Exporter, and Documented Artifact Format Completed & Committed to Isolated Git Branches.

---

## 1. Git Worktrees & Branch Isolation (Protecting Concurrent Work)

Concurrent work on workspace layouts was strictly protected by using dedicated isolated worktrees and feature branches:

| Repository / Worktree | Branch | Head Commit | Description |
| :--- | :--- | :--- | :--- |
| `D:\FrustLang-VST` | `feat/precompiled-object-load` | `9178f96` | Shared FRust Precompiled COFF-x64 object loader & isolated host symbol registration. |
| `D:\000 Tech Research-VST` | `feat/dewb-vst3-export` | *(pushed)* | Full DEWB VST3 pipeline: runtime processor, `.frust-il` v2, exporter, and workbench UI wiring. |
| `D:\000 Tech Research\Electronics Design Tool\prototype` | `master` | *Untouched* | Main repository checkout untouched. |

---

## 2. Key Architectural Deliverables Implemented

### A. Precompiled Machine Object Loader (No FRust Source at Runtime)
- **Files**: [`FrustPluginHost.h`](file:///D:/FrustLang-VST/projects/09_frust_plugin_host/include/frust_plugin_host/FrustPluginHost.h) & [`FrustPluginHost.cpp`](file:///D:/FrustLang-VST/projects/09_frust_plugin_host/src/FrustPluginHost.cpp)
- **Function**: `frust_plugin_load_object(const char* name, const char* manifestJson, const void* objectBytes, size_t objectSize)`
- Validates manifest JSON and system requirements (architecture, host functions, entry points).
- Parses raw COFF-x64 binary object bytes via `llvm::object::ObjectFile::createObjectFile`.
- Enforces architecture validation (`IMAGE_FILE_MACHINE_AMD64`) and checks required exported symbols (`process_sample`).
- Links the object directly into an isolated `llvm::orc::JITDylib` via `s.jit->addObjectFile`.
- Avoids duplicate host symbol definition errors across multiple plugin instances.

### B. Executable DSP Artifact Format (`.frust-il` v2)
- **Specification**: [`docs/VST3_EXECUTABLE_ARTIFACT_FORMAT.md`](file:///D:/000%20Tech%20Research-VST/Electronics%20Design%20Tool/prototype/docs/VST3_EXECUTABLE_ARTIFACT_FORMAT.md)
- **Serializer / Validator**: [`Source/DspArtifactFormat.h`](file:///D:/000%20Tech%20Research-VST/Electronics%20Design%20Tool/prototype/Source/DspArtifactFormat.h) & [`Source/DspArtifactFormat.cpp`](file:///D:/000%20Tech%20Research-VST/Electronics%20Design%20Tool/prototype/Source/DspArtifactFormat.cpp)
- **Magic**: `DJEHUTI_FRUST_EXECUTABLE_IL`, `status: executable_object_ready`.
- Stores precompiled COFF-x64 machine object bytes encoded as Base64.
- 64-bit FNV-1a object hash and full artifact integrity hash to prevent tampering.
- Contains serialized `circuit_sim::Circuit` structures for sample-rate re-discretization without FRust recompilation.
- Rejection of legacy `source_ready_il_lowering_pending` placeholder files with explicit diagnostics.
- Supports 3 effect topologies: `single`, `three_coupled`, and `three_independent`.

### C. Self-Contained Real-Time JUCE VST3 Audio Processor
- **Files**: [`Source/GeneratedVst/CircuitVstPlugin.h`](file:///D:/000%20Tech%20Research-VST/Electronics%20Design%20Tool/prototype/Source/GeneratedVst/CircuitVstPlugin.h) & [`Source/GeneratedVst/CircuitVstPlugin.cpp`](file:///D:/000%20Tech%20Research-VST/Electronics%20Design%20Tool/prototype/Source/GeneratedVst/CircuitVstPlugin.cpp)
- **Real-Time Safety**:
  - `computeLiveCoefficientsNoAlloc`: Zero memory allocation on audio callback. Live potentiometer/switch values are loaded via atomic pointers and mapped into matrix coefficients in place.
  - Non-allocating per-channel state buffers (`channelWorkspaces`, up to 8 channels).
  - 5 ms equal-power crossfading on effect mode switching to prevent audible clicks.
  - Divergence and runaway protection: checks for non-finite values (`NaN`, `Inf`) and resets state gracefully without audio glitches.
  - Dynamic sample-rate adjustment during `prepareToPlay`: re-calculates bilinear transform coefficients when host changes sample rate without requiring LLVM compilation.
  - Clean JUCE UI Editor (`CircuitVstEditor`) displaying effect selectors, live potentiometer dials, switches, and a status bar.

### D. IDE Export Workflow
- **Files**: [`Source/VstExporter.h`](file:///D:/000%20Tech%20Research-VST/Electronics%20Design%20Tool/prototype/Source/VstExporter.h) & [`Source/VstExporter.cpp`](file:///D:/000%20Tech%20Research-VST/Electronics%20Design%20Tool/prototype/Source/VstExporter.cpp)
- Supports exporting:
  1. A single circuit-derived effect plugin.
  2. A single plugin with 3 selectable effects (coupled or independent).
  3. 3 separate plugins from a multi-mode configuration, each with its own stable 4CC plugin ID and identity.
- Persists export configuration to `.vst-export.json` for reproducible builds.
- Generates Windows x64 `.vst3` bundle directory structure: `<Product>.vst3/Contents/x86_64-win/<Product>.vst3`.
- Integrated directly into DEWB main menu: **Tools -> Export VST3 Plugin...** and BYOK agent tool `vst_export`.

### E. Solver & Circuit Fixes
- [`Source/AudioDsp.cpp`](file:///D:/000%20Tech%20Research-VST/Electronics%20Design%20Tool/prototype/Source/AudioDsp.cpp):
  - In `Model::frustSource()`, replaced pseudo-`if` loops (`while (abs_r <= abs_c) ...`) with native FRust `if` statements to eliminate double-precision runaway loops during partial pivoting.
  - Added support for linear `Vcvs`, `Vccs`, and op-amp gains.
  - Added per-diode saturation currents and emission coefficients.
- [`Source/VstExporter.cpp`](file:///D:/000%20Tech%20Research-VST/Electronics%20Design%20Tool/prototype/Source/VstExporter.cpp):
  - Added 100 MΩ bleeder resistors to intermediate nodes between series clipping diodes in `makeNonlinearDiodeClipperCircuit` and `makeCoupledThreeModeClipperCircuit` to eliminate floating nodes when symmetry switches are open.

---

## 3. CMake Targets Added to `CMakeLists.txt`

1. **`DjehutiCircuitVst`**:
   - Generates the `Djehuti Circuit VST_VST3` bundle.
   - Force-includes `GeneratedPluginIdentity.h` to override JUCE plugin metadata.
   - Embeds precompiled artifact into `GeneratedPluginPayload.cpp`.
2. **`VstExportTests`**:
   - Lightweight console test exercising linear, nonlinear diode clipping, 3-mode effect switching, parameter automation, state recall, and host instantiation via `juce::VST3PluginFormat`.

---

## 4. Next Steps for Fresh Conversation

1. Build `DjehutiElectronicsLab` incrementally (with `/m:1`) in the isolated worktree to verify the UI export action.
2. Launch the application or run the quick headless preview tool (`schematic_preview`).
3. Merge `feat/dewb-vst3-export` into `master` after UI layout repairs in the primary workspace have been committed.
