# Stepper reference components

The Electromechanical library contains three motors and ULN2003, TMC2209 and
DM542T V4 functional driver references. Their schematic symbols, property editors,
agent tools and saved parameters use the existing part architecture and diagram
format. The same construction helper serves direct schematic analysis and nested
schematic assembly compilation. Canonical component IDs and verified specifications
match DjehutiCAD; see `assets/stepper-motor-reference.json` for provenance/unknowns.

| Symbol ID | Motor data used by the reference model |
|---|---|
| `stepper_28byj48_12v` | Supplier-unconfirmed 12 V motor, nominal 64:1 reduction. Winding R/L, rotor J, electrical cycles and Kt/Ke must be supplied explicitly. No 5 V defaults. |
| `stepper_17hs19_2004s1` | STEPPERONLINE: 1.6 Ω, 3 mH, 2 A/phase, 0.59 N·m holding, rotor J=8.2e-6 kg·m², 1.8° full step. |
| `stepper_23hs30_2804s` | STEPPERONLINE current drawing: 1.17 Ω, 4.36 mH, 2.8 A/phase, 2.0 N·m holding, J=3e-5 kg·m², 1.8° full step. |

The two identified motors default to **Holding-torque sine reference**. This is
explicitly an approximation: equal rated currents in two sinusoidal phases imply
Kt=Ke=holding/(sqrt(2)·phaseCurrent). It is not a measured commercial motor constant
or a validated speed/torque curve. Select **User coefficients** for measured Kt/Ke.
Zero damping/load defaults mean an ideal frictionless unloaded model, not measured
friction. The light motor refuses simulation until its missing inputs are entered.

`StepperMotor` uses the existing internal circuit solver: each winding has R/L,
e_k=Ke·f_k(theta)·omega and torque=Σ Kt·f_k(theta)·i_k, with
f_k=−sin(N·theta−k·π/2). Shaft inertia, load and angle use the existing mechanical
mobility analogy and solver clock, including accepted/rejected-step rollback.
Back-EMF sources have a 0.1 mΩ numerical series regularization. N=90/full-step
degrees for the bipolar reference; no ambiguous 28BYJ excitation interval is
treated as a verified full-step specification.

The ideal gearbox has separate rotor/output speed and angle quantities. Output
load inertia/damping reflect by 1/g² and load torque by 1/g. Set load parameters
on the motor; buffered telemetry nodes are not arbitrary mechanical connection
ports. Gear loss, backlash, detent torque, saturation, temperature and measured
torque-speed behavior are outside this model. Four unipolar phases connect from
COM to A/B/C/D; bipolar windings connect A+/A− and B+/B−. Phase order determines
positive rotor direction.

Driver behavior:

| Symbol | Executed behavior | Specification-only or omitted behavior |
|---|---|---|
| `driver_uln2003` | Four existing low-side switches and clamp diodes to COM; editable threshold, Ron and saturation drop | 50 V/0.5 A single-channel ratings are not enforced thermal limits; aggregate dissipation, exact carrier pins and transistor characterization absent. This is not a current chopper. |
| `driver_tmc2209` | 4.75–29 V supply validation; averaged current-limited bridge; rising STEP/DIR/disable; 1..256 power-of-two microsteps; ≥100 ns STEP high/low and ≥20 ns DIR setup | Functional ports, not a selected carrier. Nominal 3.3 V logic threshold approximation, optional UART, interpolation, PWM, sense resistor, thermal/current package limits and diagnostics omitted. Maximum configured peak 2.8 A is conditional, not a promise of carrier capability. |
| `driver_dm542t_v4` | 18–50 V operating range; ≤4.5 A configured peak; V4 microstep choices; ≥2.5 μs high/low and ≥5 μs DIR setup; differential controls with S2=5 V; 3.3 V cannot assert high | Factory 24 V control mode, 200 ms enable timing, idle-current reduction, alarms, protection, optical input current and actual PWM omitted. |

Bridge terminals are VM/GND, STEP+/−, DIR+/−, DIS+/−, A+/− and B+/−. DIS high
disables; TMC's single-ended interface uses negative controls at logic ground.
DM DIS corresponds to the active-disable ENA pair. Use peak current, not the RMS
DIP number. The averaged bridge respects bus compliance, recirculates winding
energy through regularized ideal freewheel paths (including disabled state), and
accounts for ideal bus power. It does not reproduce individual switching edges.
Time steps must resolve pulse transitions; sub-step pulses are not captured.
DIR hold timing is not enforced. Unknown/intermediate logic levels are thresholded,
not an analog optocoupler or metastability model.

Use existing `schematic_place_symbol`, property tools and connection tools after consulting
their schemas. Generic tools expose all six symbol IDs; no motor-specific command
or separate simulation engine was added. Save/reopen uses ordinary diagram JSON.
Position sensors can refer to the motor refdes through its existing angle mapping.

Sources: [NEMA 17](https://files.omc-stepperonline.com/17HS19-2004S1_Full_Datasheet.pdf),
[NEMA 23](https://files.omc-stepperonline.com/23HS30-2804S_Full_Datasheet.pdf),
[ULN2003A](https://www.ti.com/lit/ds/symlink/uln2003a.pdf),
[TMC2209 rev 1.09](https://www.analog.com/media/en/technical-documentation/data-sheets/TMC2209_datasheet_rev1.09.pdf),
[DM542T V4](https://www.omc-stepperonline.com/download/DM542T_V4.0_user_manual.pdf).

Verification: the Debug application builds. Focused numerical tests and previews
were dropped at the user's request; no numerical pass claim is made. Native GUI
placement, simulation and save/reopen were not exercised in DEWB. The optional
SchematicPreview build exposed existing missing vacuum-tube catalog link
dependencies; that separate target was left unchanged. No DEWB GUI was launched.
