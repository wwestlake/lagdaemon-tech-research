# Vacuum Tube Amplifier Example Circuits & Validation Testbenches

This directory contains representative vacuum tube circuit examples and testbenches for **Djehuti Electronics Workbench (DEWB)**, featuring physically modeled small-signal dual triodes (**12AX7 / ECC83** and **12AU7 / ECC82**) running on DEWB's internal nonlinear Newton-Raphson solver.

---

## 1. `12ax7_common_cathode.diagram.json`

### Overview
A classic high-gain Class-A voltage preamplifier stage utilizing section 1 of the **12AX7 / ECC83** high-$\mu$ dual triode. This topology forms the standard input and gain stage of iconic instrument and audio equipment (e.g., Fender, Marshall, and RCA preamps).

### Circuit Parameters
- **Supply Voltage ($V_{cc}$):** $+250\text{ V}$ DC
- **Anode Load ($R_a$):** $100\text{ k}\Omega$ ($1/2\text{ W}$)
- **Cathode Resistor ($R_k$):** $1.5\text{ k}\Omega$ (provides cathode self-bias)
- **Cathode Bypass ($C_k$):** $22\ \mu\text{F}$ (bypasses audio frequencies to eliminate negative AC current feedback)
- **Grid-Stopper ($R_{grid}$):** $68\text{ k}\Omega$ (interacts with tube Miller capacitance $C_{gp}$ to suppress RF oscillation and shape high-frequency bandwidth)
- **Grid-Leak ($R_{leak}$):** $1\text{ M}\Omega$ (references control grid to ground DC potential)
- **Coupling Capacitors:** $22\text{ nF}$ input / output
- **Load Resistor ($R_L$):** $1\text{ M}\Omega$

### Measured Live Simulation Behavior (DEWB Internal Solver)
- **DC Quiescent Point:**
  - Plate Voltage ($V_p$): $170.10\text{ V}$
  - Cathode Voltage ($V_k$): $1.20\text{ V}$
  - Grid-to-Cathode Bias ($V_{gk}$): $-1.198\text{ V}$
  - Plate-to-Cathode Voltage ($V_{pk}$): $168.90\text{ V}$
  - Quiescent Plate Current ($I_p$): $798.98\ \mu\text{A}$ ($0.799\text{ mA}$)
  - Cathode Current ($I_k$): $798.98\ \mu\text{A}$ ($I_k = I_p + I_g = 798.98\ \mu\text{A}$, exact Kirchhoff conservation to $< 10^{-14}\text{ A}$)
  - Grid Current ($I_g$): $0.00\text{ A}$ (negative grid cutoff)
  - Anode Resistor Drop ($V_{Ra}$): $79.90\text{ V}$ ($P_{Ra} = 63.8\text{ mW}$)
  - Cathode Resistor Drop ($V_{Rk}$): $1.20\text{ V}$ ($P_{Rk} = 0.96\text{ mW}$)
  - Plate Dissipation ($P_p$): $134.9\text{ mW}$ (well below $1.0\text{ W}$ maximum rating)
  - Small-Signal Parameters: $g_m = 1.755\text{ mA/V}$, $r_p = 52.78\text{ k}\Omega$, $\mu = 92.64$
- **AC Small-Signal Frequency Response (Bode):**
  - Midband Voltage Gain at 1 kHz: **$34.79\text{ dB}$ ($54.85\text{ V/V}$)**
  - Low-Frequency Corner ($f_L$): **$11.38\text{ Hz}$** (defined by cathode bypass $C_k = 22\ \mu\text{F}$ and coupling capacitors)
  - High-Frequency Corner ($f_H$): **$24.04\text{ kHz}$** (defined by Miller capacitance $C_{in} = C_{gk} + (1 + |A_v|) C_{gp} \approx 98.7\text{ pF}$ against $68\text{ k}\Omega$ grid stopper, theoretical corner $24.18\text{ kHz}$)
- **Transient Analysis:**
  - Small-signal input ($0.1\text{ V}_\text{peak}$ at 1 kHz): clean sinusoidal output of $5.48\text{ V}_\text{peak}$, inverting phase ($180^\circ$), linear gain $54.85\text{ V/V}$.
  - Overdrive input ($1.5\text{ V}_\text{peak}$ at 1 kHz): pronounced asymmetric compression and soft clipping. Negative output swing rounds smoothly into tube saturation/grid conduction, while positive peak flattens at supply cutoff ($+250\text{ V}$ rail limit), demonstrating realistic harmonic tube character.

---

## 2. `12au7_driver_stage.diagram.json`

### Overview
A medium-$\mu$, higher-current Class-A driver and buffer stage utilizing section 1 of the **12AU7 / ECC82** dual triode. Suitable for driving tone stacks, long coaxial cables, or power tube grids.

### Circuit Parameters
- **Supply Voltage ($V_{cc}$):** $+250\text{ V}$ DC
- **Anode Load ($R_a$):** $22\text{ k}\Omega$ ($2\text{ W}$)
- **Cathode Resistor ($R_k$):** $820\ \Omega$ ($1/2\text{ W}$)
- **Cathode Bypass ($C_k$):** $47\ \mu\text{F}$
- **Grid-Leak ($R_{leak}$):** $470\text{ k}\Omega$
- **Coupling Capacitors:** $47\text{ nF}$ input / output
- **Load Resistor ($R_L$):** $100\text{ k}\Omega$

### Measured Live Simulation Behavior (DEWB Internal Solver)
- **DC Quiescent Point:**
  - Plate Voltage ($V_p$): $136.87\text{ V}$
  - Cathode Voltage ($V_k$): $4.22\text{ V}$
  - Grid-to-Cathode Bias ($V_{gk}$): $-4.217\text{ V}$
  - Plate-to-Cathode Voltage ($V_{pk}$): $132.65\text{ V}$
  - Quiescent Plate Current ($I_p$): $5.142\text{ mA}$
  - Anode Resistor Drop ($V_{Ra}$): $113.13\text{ V}$ ($P_{Ra} = 582\text{ mW}$)
  - Cathode Resistor Drop ($V_{Rk}$): $4.22\text{ V}$ ($P_{Rk} = 21.7\text{ mW}$)
  - Plate Dissipation ($P_p$): $682.1\text{ mW}$ (within $2.75\text{ W}$ maximum rating)
  - Small-Signal Parameters: $g_m = 2.659\text{ mA/V}$, $r_p = 7.476\text{ k}\Omega$, $\mu = 19.88$
- **AC Small-Signal Gain:**
  - Midband Voltage Gain: **$22.95\text{ dB}$ ($14.04\text{ V/V}$)**
  - Matches theoretical dynamic formula $A_v = g_m (r_p \parallel R_a \parallel R_L) = 2.659 \times 5.277 = 14.03\text{ V/V}$ exactly.
  - Substantially lower output impedance ($Z_{out} \approx 5.58\text{ k}\Omega$) providing high drive capability without slew rate degradation.
- **Transient Analysis:**
  - Input signal $0.5\text{ V}_\text{peak}$ at 1 kHz produces $7.08\text{ V}_\text{peak}$ clean output with minimal distortion ($A_v = 14.16\text{ V/V}$).

---

## 3. `triode_curve_tracer.diagram.json`

### Overview
A reusable test fixture designed for characterization and validation of triode devices against manufacturer published curves. Contains independent variable DC voltage sources $V_p$ (plate voltage) and $V_g$ (grid voltage) with cathode tied to ground reference ($V_k = 0\text{ V}$).

### Capabilities
- **2D Parametric DC Sweep:**
  - Primary sweep: $V_p$ from $0\text{ V}$ to $400\text{ V}$ (or $500\text{ V}$) in $1\text{ V}$ or $2\text{ V}$ steps.
  - Secondary stepped parameter: $V_g$ from $0.0\text{ V}$ to $-4.0\text{ V}$ (in $-0.5\text{ V}$ or $-1.0\text{ V}$ steps for 12AX7) and $0.0\text{ V}$ to $-20.0\text{ V}$ (in $-2.0\text{ V}$ steps for 12AU7).
- Automatically produces family-of-curves plots identical to RCA RC-30 and GE datasheet graphs.

---

## 4. Evidence Gallery (`evidence/`)

The `evidence/` directory contains high-resolution verification artifacts generated directly from live DEWB simulation runs:

| File | Content | Description |
|---|---|---|
| `12ax7_schematic.png` | Schematic Layout | Verified schematic layout of the 12AX7 preamp stage with headless validation (0 crossings, 0 overlaps, 0 body hits, 0 unrouted). |
| `12ax7_dc_operating_point.png` | DC Instrument Display | Live DC node voltages, device currents, tube internal operating point ($V_{pk}, V_{gk}, I_p, g_m, r_p, \mu, P_p$), and exact Kirchhoff conservation. |
| `12ax7_ac_frequency_response.png` | AC Bode Plot | Frequency response from $1\text{ Hz}$ to $1\text{ MHz}$, showing $34.79\text{ dB}$ midband gain, $11.38\text{ Hz}$ low corner ($C_k$), and $24.04\text{ kHz}$ high corner (Miller effect). |
| `12ax7_transient_waveforms.png` | Oscilloscope Waveforms | Small-signal transient response ($0.1\text{ V}_\text{pk}$ input, $5.48\text{ V}_\text{pk}$ output, $180^\circ$ phase inversion). |
| `12ax7_transient_overdrive_clipping.png` | Oscilloscope Waveforms | Large-signal transient response ($1.5\text{ V}_\text{pk}$ input) demonstrating asymmetric soft clipping, cutoff limiting at $+250\text{ V}$, and musical saturation rounding. |
| `triode_characteristic_curves_12ax7.png` | Characteristic Curves | Simulated $I_p$ vs $V_p$ curves for 12AX7 from $V_g = 0\text{ V}$ down to $-4\text{ V}$, overlaid with RCA RC-30 nominal operating point ($250\text{ V}, -2.0\text{ V}, 1.20\text{ mA}$). |
| `triode_characteristic_curves_12au7.png` | Characteristic Curves | Simulated $I_p$ vs $V_p$ curves for 12AU7 from $V_g = 0\text{ V}$ down to $-20\text{ V}$, overlaid with RCA RC-30 nominal operating point ($250\text{ V}, -8.5\text{ V}, 10.5\text{ mA}$). |

---

## 5. How to Run in DEWB

### Through the DEWB GUI
1. Launch `djehuti_electronics_lab.exe`.
2. Open project `Vacuum Tube Amplifiers`.
3. Open any schematic:
   - `12ax7_common_cathode.diagram.json`
   - `12au7_driver_stage.diagram.json`
   - `triode_curve_tracer.diagram.json`
4. In the **Simulation / Analytics** panel:
   - Select **Internal Solver** (`engine: "internal"`).
   - For DC Operating Point: Run **DC Operating Point**.
   - For AC Sweep: Select input source `VIN`, start frequency $1\text{ Hz}$, stop frequency $1\text{ MHz}$, points per decade $20$.
   - For Transient: Set stop time to $5\text{ ms}$, step to $10\ \mu\text{s}$.

### Through the DEWB Local Agent API
Run commands against `%APPDATA%\DjehutiElectronicsLab\agent-api.json`:
- `{"action": "open_project", "params": {"project_path": "D:\\000 Tech Research\\Electronics Design Tool\\prototype\\examples\\tube_amplifiers"}}`
- `{"action": "open_diagram", "params": {"filename": "12ax7_common_cathode.diagram.json"}}`
- `{"action": "run_analytics", "params": {"analysis": "dc_op", "engine": "internal"}}`
