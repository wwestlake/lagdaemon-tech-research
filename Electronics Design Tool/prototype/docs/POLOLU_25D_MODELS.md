# Pololu 25D manufacturer profiles

The ordinary Electromechanical browser exposes `motor_pololu_4865`,
`motor_pololu_4885`, `motor_pololu_4845` and `motor_pololu_3229`. Canonical IDs
`motor-dc-pololu-<part>` match DjehutiCAD. No existing ID or document format was
changed. Each placement freezes its defaults and identity in the existing params
map; normal schematic tools/property editors and nested schematic compilation
reuse the same profile builder. Source curves and original HTML/PDF files are in
`assets/manufacturers/pololu`; `assets/pololu-25d-reference.json` matches CAD's
manifest byte for byte, including input hashes and source provenance.

The source snapshot revision, typical calibration inputs and fitted-line inputs
are frozen in hidden ordinary parameters as well. Future catalog updates cannot
silently recalibrate a saved occurrence. No new serialization schema is needed.

All four are 12 V, nominal 47:1 (published rounded ratio 46.85:1).

| Part | Configuration | No-load rpm / A | Extrapolated stall A / kg·cm | Published maximum output |
|---|---|---|---|---|
| 4865 | MP, encoder | 170 / 0.100 | 1.8 / 6.4 | 2.8 W |
| 4885 | LP, encoder | 120 / 0.060 | 0.9 / 4.8 | 1.4 W |
| 4845 | HP, encoder | 220 / 0.300 | 5.0 / 15 | 8.4 W |
| 3229 | MP, no encoder | 170 / 0.080 | 1.8 / 6.4 | 2.8 W |

Typical speed variation is ±20%, no-load current ±50%. Stall values are theoretical
extrapolations and change with heating. Manufacturer guidance recommends gearbox
torque ≤4 kg·cm continuous / ≤8 intermittent and continuous current generally ≤25%
of stall current. These limits are metadata, not simulated thermal or damage limits.

## Executed equations

`Pololu25DMotor` parameterizes the existing `mech::addDcMotor` and `ShaftDevice`.
There is no new solver, time integrator or competing mechanical state system.
The modeled shaft is the **gearbox output**, using effective terminal/output
coefficients rather than claiming identified internal rotor constants:

    R = V / Is
    Kt_output = Ts / Is
    Ke_output = (V − R·I0) / omega0
    B_output = Kt_output·I0 / omega0

These feed the existing V=R·I+L·dI/dt+Ke·omega and
J·domega/dt=Kt·I−B·omega−Tload equations. At 12 V their steady solution is
omega=omega0·(1−Tload/Ts), I=I0+(Is−I0)·Tload/Ts. Convert kg·cm using 0.0980665 N·m
per kg·cm; graph torque is kg·mm (0.00980665 N·m). Kt_output and Ke_output differ
because this is an effective lossy gearmotor model, not a lossless intrinsic rotor.

Three calibration choices are implemented:

- **Published typical endpoints** uses each exact product's table above (default).
- **Datasheet fitted lines** uses the actual graph fits below. Zero-speed torque
  is rpm_intercept / speed_slope and its current is current_intercept +
  current_slope·zero_speed_torque; those effective endpoints parameterize the same
  existing DC equations. Their differences from product typicals are intentional.
- **User coefficients** allows editing effective R/Kt/Ke/B. Inductance, inertia,
  initial angle/speed and output load parameters remain editable in all modes.

| Datasheet page | Motor family | Published fitted rpm(τ) | Published fitted I(τ), A |
|---|---|---|---|
| 29 | LP (4885) | 120 − 2.4τ | 0.029 + 0.018τ |
| 39 | MP (3229/4865) | 170 − 2.6τ | 0.042 + 0.027τ |
| 48 | HP (4845) | 220 − 1.4τ | 0.16 + 0.031τ |

τ is kg·mm. Graph sample no-load currents (0.045, 0.076, 0.20 A) are neither
the fitted intercepts nor the product typicals. The full original 64-page PDF,
including measured sample points, power and efficiency curves, is preserved.
No invented digitized measurement CSV is supplied.

Unpublished L defaults to 0, explicitly neglecting electrical transients.
Unpublished output-equivalent inertia defaults to 1e-5 kg·m², a positive numerical
reference required by the existing dynamic interface. It is not measured, and
transient timing is **uncalibrated** until replaced. Intrinsic winding resistance,
rotor/gear inertia, individual friction/loss, backlash and thermal parameters remain
unknown. Steady calibration does not identify those quantities. No separate
gearbox efficiency or internal rotor dynamic state is invented.

## Encoder behavior

Only 4865/4885/4845 have VCC/GND/A/B pins, in addition to M1/M2. 3229 has M1/M2
only and creates no encoder device. Published wiring: red M1, black M2, green GND,
blue VCC, yellow A, white B. No physical connector pin numbering is inferred.

The encoder operates at the **motor shaft**: theta_motor = g·theta_output.
The default g=2248.86/48=46.85125 preserves Pololu's published 2248.86 output counts,
rather than multiplying 48 by rounded 46.85. This is calculated from published
counts, not a claimed exact rational tooth ratio. Twelve ideal electrical cycles
per motor revolution generate A/B signals 90° apart: both edges of both channels
give 48 counts/motor revolution. Angle phase zero is an arbitrary reference.

Powered outputs follow ground/VCC for a 3.5–20 V supply; below 3.5 V they are high
impedance, above 20 V or reversed supply is rejected. Supply/output rail currents
are coupled in the existing circuit through an ideal push-pull model. Output
resistance defaults to an explicitly assumed 100 Ω and is editable; actual output
drive, quiescent supply current (published maximum 10 mA), timing/jitter and maximum
frequency are uncharacterized. The 10 mA value is not a per-channel current limiter.
Time steps must resolve encoder transitions; no hidden edge counter or interpolated
sub-step pulse history is manufactured. Stateless angle evaluation supports rollback.

Use `workbench_capabilities`, `schematic_place_symbol`, `schematic_get_parameters`,
`schematic_set_parameters` and `schematic_connect`. Existing position sensors can
use the motor refdes's output-angle mapping. No robot assembly was added.

Focused numerical validation is selected explicitly with
`motor_tests --pololu25d --out <folder>`; this bypasses the full motor/controller
campaign. It checks 12 V no-load, half-stall load and theoretical stall endpoints
for all four profiles, each fitted-curve calibration at 0.2 N·m, encoder Gray-code
counts/power/rollback and missing encoder configuration on 3229. No transient
time-scale validation or GUI verification is implied by these checks.

Verified results: 27 focused checks passed. Typical-endpoint examples at 12 V:
4865 no-load 170 rpm / 0.100 A, half-stall load 85 rpm / 0.950 A;
4885 half-stall 60 rpm / 0.480 A; 4845 half-stall 110 rpm / 2.650 A;
3229 no-load 170 rpm / 0.080 A. Calculated values match those analytical targets
within 1e-4 rpm and 1e-6 A. Theoretical stall speed residuals are below 1e-10 rpm.
These are reference-model checks, not physical motor measurements.

All four new schematic previews and the eight existing reference circuits have
zero crossings, overlaps, body hits, unrouted wires and off-grid routing points.
The preview target's missing existing VacuumTubeCatalog link dependency was added
so the actual symbol registry could be checked. Visual inspection still shows
crowding between generic oscilloscope net labels in the encoder fixture; that
instrument-label issue is outside this component integration and was not refactored.
Native GUI placement/simulation/save-reopen were not exercised. CAD's separate
headless native document persistence check passed; DEWB persistence here is
supported by the existing parameter serialization and the inspected snapshot path.
