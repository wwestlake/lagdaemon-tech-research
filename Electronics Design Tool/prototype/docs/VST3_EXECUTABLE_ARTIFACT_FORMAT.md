# Djehuti Electronics Workbench — Executable `.frust-il` Artifact Format (v2)

## Overview

The `.frust-il` executable artifact format (`magic = "DJEHUTI_FRUST_EXECUTABLE_IL"`, `schemaVersion = 2`, `status = "executable_object_ready"`) packages compiled circuit DSP models for direct execution inside standalone Windows x64 VST3 plugins without invoking the FRust source parser or compiler at plugin runtime.

Legacy metadata placeholders marked `status = "source_ready_il_lowering_pending"` are explicitly rejected during validation.

---

## Top-Level JSON Schema

| Field | Type | Description |
|---|---|---|
| `magic` | `string` | Must equal `"DJEHUTI_FRUST_EXECUTABLE_IL"`. |
| `schemaVersion` | `int` | Must equal `2`. |
| `status` | `string` | Must equal `"executable_object_ready"`. |
| `packageId` | `string` | Stable product identifier used to derive `pluginCode4CC` and `vst3ComponentUidHex`. |
| `productName` | `string` | Human-readable VST3 plugin name. |
| `productVersion` | `string` | Semantic version string (e.g., `"1.0.0"`). |
| `manufacturerName` | `string` | Manufacturer string (e.g., `"Djehuti Audio"`). |
| `pluginCode4CC` | `string` | Deterministic 4-character plugin code derived from `packageId`. |
| `vst3ComponentUidHex` | `string` | Deterministic 32-hex-character VST3 component GUID derived from `packageId`. |
| `hostSchemaVersion` | `int` | FRust plugin host ABI schema version (`1`). |
| `minHostSchemaVersion` | `int` | Minimum compatible host schema version (`1`). |
| `maxHostSchemaVersion` | `int` | Maximum compatible host schema version (`1`). |
| `compilerVersion` | `string` | FRust compiler version (`"0.1.0"`). |
| `targetArch` | `string` | Must equal `"x86_64-pc-windows-msvc"`. |
| `objectFormat` | `string` | Must equal `"COFF-x86_64"`. |
| `effectTopology` | `string` | `"single"`, `"three_coupled"`, or `"three_independent"`. |
| `slots` | `array` | Compiled DSP model slots (`CompiledModelSlot`). |
| `effects` | `array` | Selectable effect presets (`EffectPresetSlot`, 1 or 3 entries). |
| `parameters` | `array` | Exposed circuit control schema (`ParameterSpec`). |
| `artifactIntegrityHashHex` | `string` | 64-bit FNV-1a hex digest covering headers, slots, object hashes, parameters, and effects. |

---

## Compiled Model Slot (`slots[]`)

Each entry in `slots[]` represents one reduced MNA/DK circuit model compiled via `::frust::Compile` (`emitObject = true`):

- `slotId`, `slotName`: Identifier and label for the slot.
- `workspaceSize`: Required length of the per-channel `double` workspace array (`stateCount + portCount + 10`).
- `coeffSize`: Required length of the reduced `double` coefficient array (`portCount * affineCols + portCount^2 + stateCount * affineCols + stateCount * portCount + affineCols + portCount`).
- `stateCount`, `inputCount`, `portCount`, `livePortCount`: Exact topological dimensions baked into the compiled native code.
- `entryPoints`: Required exported symbols (must include `"process_sample"` with signature `extern "C" double process_sample(double audio_in, double* ws, double* coeffs)`).
- `requiredHostFunctions`: Host functions linked into the JITDylib (`"djehuti_dsp_exp"`, `"djehuti_dsp_log"`).
- `runtimeManifestJson`: JSON manifest validated by `frust_plugin_load_object`.
- `objectByteSize`: Exact byte length of the raw COFF object file.
- `objectHashHex`: 64-bit FNV-1a hex digest of the raw COFF object bytes.
- `objectCodeBase64`: Base64-encoded Windows x86_64 COFF `.obj` binary.
- `circuit`: Serialized `circuit_sim::Circuit` and `audio_dsp::Config`. Because the compiled `process_sample` entry point takes `coeffs` as a parameter and only hardcodes topological dimensions (`stateCount`, `portCount`, `inputCount`), `prepareToPlay(sampleRate, samplesPerBlock)` rebuilds `audio_dsp::Model` from `circuit` at the host's exact sample rate (44.1 kHz, 48 kHz, 96 kHz, etc.) and updates the base discretization matrices without recompiling FRust source.
- `optionalFrustSource`: Optional human-readable FRust source retained for diagnostics only; never compiled at runtime.

---

## Multi-Effect Topologies (`effects[]`)

The artifact supports both required multi-effect architectures:

1. **`"single"`**: 1 compiled model slot, 1 effect preset.
2. **`"three_coupled"`**: 1 compiled model slot containing a single coupled schematic with mode-selection switches (`LivePort::Kind::Switch`). Three entries in `effects[]` specify `livePortOverrides` for each mode so switching effects preserves shared reactive history (`ws[0..stateCount)`) and inter-stage loading.
3. **`"three_independent"`**: 3 compiled model slots (`slots[0..2]`) and 3 entries in `effects[]` (`modelSlotIndex = 0, 1, 2`), allowing 3 completely distinct circuit topologies in a single VST3 plugin with crossfaded transitions on switching.

---

## Validation & Integrity Checks

Before any code is linked or executed, `dsp_artifact::loadAndValidateArtifactFromJson` and `frust_plugin_load_object` verify:
1. `status != "source_ready_il_lowering_pending"` and `magic == "DJEHUTI_FRUST_EXECUTABLE_IL"`.
2. `schemaVersion == 2` and `hostSchemaVersion` compatibility.
3. `targetArch == "x86_64-pc-windows-msvc"` and COFF x86_64 object header architecture match.
4. Base64 decoding succeeds, `objectBytes.size() == objectByteSize`, and `computeHashHex(objectBytes) == objectHashHex`.
5. Every declared entry point (`process_sample`) exists as a defined symbol in the COFF symbol table.
6. Reconstructed circuit model dimensions (`workspaceSize`, `coeffSize`) match the compiled ABI.
7. `artifactIntegrityHashHex` matches the computed digest of the entire artifact structure.
