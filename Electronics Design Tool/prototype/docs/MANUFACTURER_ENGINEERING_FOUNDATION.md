# Manufacturer engineering references

DEWB consumes the same `djehuti-component-library` version 1 records as DjehutiCAD. The optional `engineering.specification` version 1 extension adds source/variant/condition/curve/model-readiness semantics to existing typed properties. The original record and original assets are mirrored in `assets/manufacturer-reference-library.json` and `assets/manufacturer-foundation`, and copied beside the built application.

The shared IDs are `motor-dc-pololu-2365`, `actuator-linear-actuonix-l12-50-100-6-p`, and `driver-pololu-2130`. These are reference definitions, not new simulation symbols. Existing Pololu 25D/stepper definitions and solvers are unchanged.

Use the `manufacturer_reference` tool through the existing in-app agent/local API:

```json
{"action":"list"}
{"action":"inspect","componentId":"motor-dc-pololu-2365","verifyAssets":true}
{"action":"bind","componentId":"motor-dc-pololu-2365","refdes":"M1"}
{"action":"inspect","refdes":"M1","verifyAssets":true}
{"action":"clear","refdes":"M1"}
```

An optional absolute `libraryPath` selects another portable library. Binding verifies all referenced assets, refuses an existing incompatible component ID/manufacturer part number, and snapshots the complete record into the existing part `params` map (`engineering_record`, `engineering_asset_root`, `component_id`). It uses normal schematic undo and parameter persistence. Binding/clearing happens in the authoritative schematic, not a fixture view. Clearing removes the engineering reference and retains physical identity and simulator values. Inspection detects a subsequent identity mismatch. No resistance, inductance, torque or other simulator coefficients are automatically overwritten. The tool's description and schema flow through the existing tool registry, manifest and help-card infrastructure.

The record keeps manufacturer identity, exact part number and variant, source filename/URL/revision/SHA-256, numerical value, quantity, original unit, tolerance, conditions and provenance. Published, CAD-derived, measured, analytically derived, assumed and unknown data remain distinct. Unknowns are null. Conflicting nominal and measured/fitted claims are retained as separate references requiring review. Source hashes verify byte integrity; reviewed transcription is not independent product measurement.

Interpretation validates dimensional compatibility and returns SI quantities without changing originals. The initial checked derivation is effective resistance = voltage/current at identical conditions, with explicit assumptions. Other operations and units require an explicit validator extension. Curves retain original assets, axes, units and operating conditions; optional sampled points require matching dimensions and digitization notes. Original Pololu PDF curves and Actuonix force-speed image are retained; no invented current curve or automatic PDF extraction is provided. Model readiness reports available, derived, assumed, unknown and needs-review inputs. Even complete listed inputs do not claim a validated solver.

The Pololu motor retains 420 rpm nominal and 390 rpm fitted-curve claims separately; its approximate 6/0.67 = 8.9552 ohm effective stall resistance is derived, and inductance/inertia/gear efficiency remain unknown. The Actuonix record is the five-wire P variant. Pololu carrier #2130 uses approximately 1.2 A continuous/channel and 2 A peak/channel; TI bare-IC and paralleled ratings are retained only in the original related datasheet, not substituted into the carrier's claims.

`Source/EngineeringSpecification.h`, `Source/EngineeringSpecificationChecks.h` and the portable JSON are byte-identical to CAD. Keep those copies synchronized when extending the contract. Assets use relative package paths; snapshots survive missing original assets, but explicit asset verification then fails and reports the missing/changed path. Rebind from a relocated verified package to update the local asset directory.

Focused verification: build `manufacturer_reference_checks` in Debug with `/m:1`, then run it with the absolute library JSON and report JSON paths. It checks semantic errors, dimensional derivation, source hashes, identity conflicts, parameter-map serialization and preservation of existing simulation coefficients. It does not exercise native GUI controls, the whole schematic save/open workflow, or new physics. No new simulation models are authorized by this foundation.

Validation status for this handoff: the focused DEWB check passed. The full DEWB application target was not rebuilt after the final source edits because the session time budget changed; the lightweight target did compile successfully. Native GUI operation was not performed.
