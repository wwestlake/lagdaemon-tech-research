# Vacuum Tube Amplifier Example Circuits

This directory contains representative vacuum tube circuit examples for Djehuti Electronics Workbench (DEWB), featuring physically modeled small-signal dual triodes.

---

## 1. `12ax7_common_cathode.diagram.json`

### Overview
A classic high-gain Class-A voltage preamplifier stage utilizing section 1 of the **12AX7 / ECC83** high-$\mu$ dual triode. This topology forms the input and gain stage of iconic instrument and audio equipment (e.g. Fender, Marshall, and RCA preamps).

### Circuit Parameters
- **Supply Voltage ($V_{cc}$):** $+250\text{ V}$ DC
- **Anode Load ($R_a$):** $100\text{ k}\Omega$ ($1/2\text{ W}$)
- **Cathode Resistor ($R_k$):** $1.5\text{ k}\Omega$ (provides cathode self-bias)
- **Cathode Bypass ($C_k$):** $22\ \mu\text{F}$ (bypasses audio frequencies to eliminate negative AC current feedback)
- **Grid-Stopper ($R_{grid}$):** $68\text{ k}\Omega$ (interacts with tube Miller capacitance $C_{gp}$ to suppress RF oscillation and shape high-frequency bandwidth)
- **Grid-Leak ($R_{leak}$):** $1\text{ M}\Omega$ (references control grid to ground DC potential)
- **Coupling Capacitors:** $22\text{ nF}$ input / output

### Typical Simulated Operating Behavior
- **DC Quiescent Point:** $V_p \approx 170\text{ V}$, $V_k \approx 1.20\text{ V}$, $I_p \approx 0.80\text{ mA}$, $V_{pk} \approx 169\text{ V}$, $V_{gk} \approx -1.20\text{ V}$.
- **Midband Voltage Gain ($A_v$):** $\approx 56\text{ V/V}$ ($+35\text{ dB}$).
- **Bandwidth & Miller Rolloff:** $f_{-3\text{dB}} \approx 22\text{ kHz}$ shaped by $C_{in} = C_{gk} + (1 + |A_v|) C_{gp} \approx 98\text{ pF}$.
- **Overdrive / Harmonic Signature:** Soft, asymmetric clipping when driven beyond $1.5\text{ V}_\text{peak}$, with cutoff limiting near $+250\text{ V}$ and saturation rounding, creating musical 2nd-harmonic distortion.

---

## 2. `12au7_driver_stage.diagram.json`

### Overview
A medium-$\mu$, higher-current Class-A driver and buffer stage utilizing section 1 of the **12AU7 / ECC82** dual triode. Ideal for driving tone stacks, long lines, or output power tube grids (e.g., phase splitters and driver stages).

### Circuit Parameters
- **Supply Voltage ($V_{cc}$):** $+250\text{ V}$ DC
- **Anode Load ($R_a$):** $22\text{ k}\Omega$ ($2\text{ W}$)
- **Cathode Resistor ($R_k$):** $820\ \Omega$
- **Cathode Bypass ($C_k$):** $47\ \mu\text{F}$
- **Grid-Leak ($R_{leak}$):** $470\text{ k}\Omega$
- **Coupling Capacitors:** $47\text{ nF}$

### Typical Simulated Operating Behavior
- **DC Quiescent Point:** $V_p \approx 150\text{ V}$, $V_k \approx 3.7\text{ V}$, $I_p \approx 4.5\text{ mA}$, $V_{pk} \approx 146\text{ V}$.
- **Midband Voltage Gain ($A_v$):** $\approx 14\text{ V/V}$ ($+23\text{ dB}$).
- **Output Impedance ($Z_{out}$):** $\approx r_p \parallel R_a \approx 5.5\text{ k}\Omega$ (substantially lower than 12AX7, enabling greater capacitive drive capability).
