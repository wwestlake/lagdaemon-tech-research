# Configurable oscilloscope input impedance

Each channel now has **Input Impedance: Off / On** and a finite resistance field in its existing control group. Off observes without adding an electrical load; On adds the configured resistance between that channel and REF. Trace visibility, display coupling, scaling and offset do not change the load.

The field accepts positive engineering values such as `50`, `1k`, `10M`, and `1G`, with optional ohm units. `M` always means mega in this control, including when the application's SPICE capital-M preference means milli. The displayed value includes units. Off disables editing and preserves the previous finite value. Invalid values and units are rejected with feedback, and multi-setting requests validate before applying changes.

Existing behavior is preserved when settings are absent: schematic scopes start On at 10 MΩ; fixture scopes start Off with a stored finite default of 10 MΩ. Both channels can select either behavior independently.

## Electrical integration and persistence

`OscilloscopeInput.h` supplies strict resistance parsing and the shared ordinary resistor insertion used by both hosts. Native netlist construction replaces the two fixed 10 MΩ resistors with the selected loads. Fixture execution adds each enabled, connected channel's resistor to the private run netlist before the existing audio/transient renderer solves it. Its actual REF connection is resolved through the existing circuit and hierarchy mapping; an unwired fixture REF uses the same implicit ground as fixture acquisition. Probe trace collection adds no resistors. Separate connected instruments contribute separate physical loads.

Loading parameters participate in acquisition invalidation. Changed loads cannot be presented as current fixture samples; Run or Single solves the updated circuit. Display-only controls continue to reuse captured samples. Impedance changes through scope controls also discard held scope records.

Native settings use existing part parameters and diagram persistence. Fixture settings use `FixtureEquipment::instrumentSettings` and fixture sidecars. Both retain the independent enabled state and last finite resistance. Invalid settings in a loaded file produce a simulation diagnostic rather than silently substituting a resistance.

Agent parity uses `oscilloscope_control` and `test_fixture_instrument` with `ch1_impedance_enabled` / `ch2_impedance_enabled` (`On` or `Off`) and `ch1_impedance` / `ch2_impedance` (resistance). Descriptions and both tool cards document the controls, defaults and acquisition behavior. Read results include enabled state, saved finite ohms, effective input ohms (null for infinity), and a readable impedance label.

## Focused verification

Debug application builds use `/m:1`, with output redirected to logs. Only affected application sources are rebuilt; no separate JUCE target, Release build, monolithic suite, or exhaustive solver campaign is involved.

The final incremental Debug build passed. Its only warning was the existing MSB8028 shared intermediate-directory warning. The 64 component checks were rerun successfully against that final executable after removing the parser's discarded-result compiler warning. The electrical loading path was unchanged by that validation cleanup.

The component suite has **64 passing checks**, including production controls, units, parser behavior under the capital-M preference, invalid values, transactional rejection, retained finite values, independent field states, exact fixture load element count, serialization, capture invalidation and existing scope controls. It deliberately performs no electrical solves. The first attempt's 30-second process limit was too short; a complete run took about 36 seconds with image rendering.

`Testing/verify_scope_input_impedance.ps1` has **100 passing application-level checks** against actual native and fixture circuit solutions. The benchmark has two independent 1 kΩ / 1 kΩ dividers. For an enabled resistance Z, the expected ratio is `(1000 || Z) / (1000 + (1000 || Z))`; Off gives 0.5. The native source is 10 V DC plus a 1 V peak, 1 kHz sine. The fixture uses its actual 1 V peak sine stimulus through the existing source replacement mechanism.

| Channel input | Expected native DC midpoint | Measured native DC midpoint | Expected Vpp, either host | Measured fixture Vpp |
|---|---:|---:|---:|---:|
| Off (infinite) | 5.000000000 V | 4.999999998 V | 1.000000000 V | 1.000000000 V |
| On, 10 MΩ | 4.999750012 V | 4.999750010 V | 0.999950002 V | 0.999949992 V |
| On, 50 Ω | 0.454545455 V | 0.454545455 V | 0.090909091 V | 0.090909094 V |
| On, 1 kΩ | 3.333333333 V | 3.333333332 V | 0.666666667 V | 0.666666687 V |

The electrical assertions use a 3 µV absolute tolerance, which distinguishes the 10 MΩ load from an ideal input. The DC benchmark uses the observed extrema midpoint because acquisition endpoint rounding can leave a partial cycle in the visible-window mean; the scope correctly retains that actual sample-window mean.

Live checks also cover independent 50 Ω / 1 kΩ channels, disabling and reenabling the previous value, native and fixture disk save/reopen with different On/Off states, rejected invalid requests, unchanged loading with hidden or AC-coupled traces, cursor/frequency controls, stale fixture capture rejection, and a finite load connected to a nonground REF. The fixture's authoritative schematic file hash remains unchanged.

## Application lifecycle and boundaries

The verification script refuses to reuse an existing DEWB process and tracks only its own launched instance. It saves only its generated QA diagram before normal close, attempts normal window closure, and verifies termination. If normal close fails it reports that condition and terminates only its own process.

The initial component timeout required termination of its owned process. The first live attempt stopped on the corrected partial-cycle DC assertion and its unsaved QA diagram prevented normal closure; only that owned process was terminated. The successful full live run closed normally with `forced=false`. All agent-launched application processes and instrument windows are closed before reporting or committing. The lifecycle rule is recorded in the repository's `AGENTS.md` for future assignments.

This extension models the requested resistive input loading. It adds no input capacitance, analog coupling model, or separate solver. Verification uses automated component handlers and the application's local Agent API; physical mouse entry was not separately exercised. The authoritative design is never edited by fixture impedance settings.
