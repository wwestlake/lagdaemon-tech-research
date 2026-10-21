# Laboratory instrumentation stages

Authorization for this change is the focused inventory and first oscilloscope
milestone from the October 21, 2026 Professional Laboratory Instrumentation work
order. Subsequent instruments require a separate bounded implementation stage.

## Existing implementation inventory

| Instrument | Reusable implementation | Gaps / smallest needed extension |
|---|---|---|
| Oscilloscope | `SchematicCanvasPanel::captureScope`, `ScopeView`, fixture `lastProbeTraces`, `PartCatalog`, independent floating windows | Retain full acquired records, separate acquisition from presentation, add a grouped right-side UI and sampled-data measurements. This is the current milestone. |
| Function generator | Fixture `signal_gen`, `TestSetup::syncSetupFromEquipment`, `runTestSetup`, `audio_test::render`, independent source replacement and virtual source conflict checks. Native waveform sources already have sine/square/pulse/PWL support. | Fixture generator configuration and render stimulus generation are sine-oriented; waveform, offset, DC, phase and duty controls need a common source specification. Define ideal versus finite output impedance explicitly and preserve source/return consistency. Do not replace the existing electrical solver. |
| Multimeter | Native `MeterView` and `readMeter`: DC V, AC V, series DC A, and Ohms. `buildSimNetlist(ohmmeter)` disables independent excitation and supplies a test current. | Fixture integration is incomplete. Validate series current connections, expand AC/RMS current and voltage modes, and verify Ohms against actual series/parallel networks. Open/short, floating networks, reactive steady-state assumptions and nonlinear operating conditions need explicit status rather than an optimistic numeric result. |
| Spectrum analyzer | `BodeView`/`sweepBode` perform real swept small-signal transfer analysis. `Analytics::runFourier` and `circuit_sim::fourier` provide harmonic amplitudes and THD from transient samples. | A spectrum instrument UI and fixture input adapter are absent. Build a shared time-signal spectral acquisition/measurement model with window normalization, marker/bandwidth/harmonic measurements and valid averaging/peak hold. Keep this separate from AC transfer sweeps; define bin spacing and window equivalent-noise bandwidth honestly. |

Placement, terminal wiring, fixture sidecars, source validity checks, the local
Agent API and the instrument-window host are existing systems to preserve.
Schematic instruments remain schematic instances; fixture instrument settings
remain on fixture equipment. Neither instrument presentation nor a fixture run
is permission to rewrite the circuit's authoritative source values.

## Proposed stage boundaries

1. **Oscilloscope milestone 1 — this stage.** Retained simulation records;
   right-side grouped controls; independent channel visibility, volts/div,
   display offset, position, equivalent input scaling, and DC/digital AC coupling;
   high-voltage ranges; timebase and pan/zoom; automatic scaling and record fit;
   sample-derived trigger presentation with Auto/Normal and single-record hold;
   numerical and draggable time/voltage cursors; visible-sample measurements;
   channel math and full-record CSV export; matching agent operations. Verify
   numerical formulas, focused component behavior, actual preamp captures, and
   a known biased-sine divider through the existing transient backend.
2. **Generator stage.** A common waveform/excitation specification for DC, sine,
   square, triangle, sawtooth and pulse; waveform units and offset/phase/duty;
   explicit output impedance; actual simulation source insertion/override;
   source conflicts and return leads; amplifier injection acceptance workflow.
3. **Multimeter stage.** Valid current measurement topology, AC/RMS modes and
   range/status presentation; test-excited equivalent resistance; series,
   parallel, open/short and nonlinear/reactive condition checks; fixture wiring
   parity and authoritative-schematic preservation.
4. **Spectrum stage.** Time-signal acquisition and normalized spectral processing;
   span/center/start-stop, magnitude scales, markers and delta/peak navigation;
   window/resolution/averaging/peak hold; fundamental, harmonic, bandwidth and
   applicable THD measurements; independent AC-transfer presentation.
5. **Cross-instrument acceptance.** After those stages are authorized and built,
   exercise generator-to-amplifier-to-scope/spectrum, network resistance, valid
   DC current and voltage, and cursor measurements with real user circuits.

## Oscilloscope acquisition contract

- Native scopes request a configured transient record after a bounded settling
  interval through the existing solver. Fixture scopes use the actual fixture
  renderer's samples and source configuration.
- Run/Acquire requests a simulation and follows subsequent completed records.
  Stop holds a captured record. Single requests one record and holds it when a
  matching edge exists; otherwise it remains armed for a later matching completed
  record. These are offline simulation semantics, not continuous hardware or
  streaming acquisition. No artificial noise, events or triggers are generated.
- Display scales, coupling, offsets, cursor gestures, zoom, pan and trigger
  presentation reuse captured samples. Native record duration and sample step
  are acquisition settings; fixture record length/rate belong to its stimulus.
- AC coupling means full-record digital mean removal. The scope does not claim
  an analog input bandwidth or analog coupling time constant. Native schematic
  scopes retain their existing 10 MOhm input resistance per channel. Fixture
  scope traces currently observe nodes without inserting that load. Unifying
  fixture loading with native instrument models is a distinct common-integration
  extension to settle before the later multi-instrument acceptance stage.
  Input scaling multiplies already simulated volts; its default is 1x and it
  does not change either input model.
- Automatic measurements use visible coupled samples. RMS includes DC under DC
  coupling. Edge time uses observed min/max 10–90% levels; pulse width uses the
  midrange crossing. Missing complete edges are unavailable, not zero. Timing
  shorter than two maximum sample intervals is flagged as sample-limited.
- Cursors are acquisition-relative seconds and selected coupled-channel volts.
  Display offset changes the coordinate mapping, not measured sample values.
  CSV includes full-record raw volts and coupled/scaled volts, with aligned math
  output when both channels are available.

Windowed spectral processing and additional waveform source models are the
smallest numerical extensions identified for later stages. They do not require
an unrelated docking, schematic, toolbox or electrical-solver redesign.
