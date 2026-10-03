ELECTRONICS COOKBOOK CONTENT

Create a cookbook/reference collection covering the following common electronic circuits, topologies, and design schemes. Each item should contain a useful description of what the circuit is, what it does, typical applications, basic operating principles, and the important design equations/component relationships where appropriate.

1. PASSIVE CIRCUITS
- Voltage divider — Produces a fraction of an input voltage using resistors.
- Current divider — Determines current distribution through parallel branches.
- Resistive attenuator — Reduces signal amplitude while maintaining desired impedance.
- Potentiometer circuits — Adjustable voltage division, gain, bias, and control.
- Pull-up and pull-down networks — Establish defined logic or bias states.
- RC networks — Resistor-capacitor timing and frequency-dependent circuits.
- RL networks — Resistor-inductor transient and filtering circuits.
- RLC networks — Combined resistor/inductor/capacitor frequency-selective circuits.
- Series resonance — Series RLC circuit exhibiting minimum impedance at resonance.
- Parallel resonance — Parallel resonant circuit exhibiting high impedance at resonance.
- LC tank circuits — Resonant energy exchange between inductance and capacitance.
- Damping networks — Reduce ringing and unwanted resonance.
- Snubbers — RC/RCD networks for suppressing switching transients and ringing.
- Decoupling networks — Isolate circuit sections from supply disturbances.
- Bypass networks — Provide low-impedance AC paths around circuit elements.
- Wheatstone bridge — Precision resistance measurement and sensor interface.
- Kelvin/four-wire connections — Eliminate lead resistance from precision measurements.
- Impedance-matching networks — Transform source and load impedances.

2. DIODE AND RECTIFIER CIRCUITS
- Half-wave rectifier — Rectifies one half-cycle of an AC waveform.
- Full-wave center-tapped rectifier — Rectifies both AC half-cycles using a center-tapped transformer.
- Bridge rectifier — Full-wave rectification using four diodes.
- Precision rectifier — Op-amp-assisted rectification for small signals.
- Peak detector — Captures and holds waveform peak voltage.
- Envelope detector — Recovers the amplitude envelope of a modulated waveform.
- Diode clipper — Limits waveform amplitude.
- Diode clamper — Shifts waveform DC level.
- Voltage limiter — Prevents signals from exceeding defined voltage limits.
- Diode OR circuit — Combines multiple sources through diode isolation.
- Flyback diode — Suppresses inductive voltage spikes.
- Reverse-polarity protection — Protects circuits from reversed supplies.
- Zener regulator — Simple voltage regulation/reference using Zener breakdown.
- Voltage doubler — Produces approximately twice the AC peak voltage.
- Voltage multiplier — Cascaded diode-capacitor voltage multiplication.
- Charge pump — Generates higher, lower, or inverted voltages using switched capacitors.

3. LINEAR POWER SUPPLIES AND REGULATORS
- Unregulated DC supply — Transformer/rectifier/filter DC supply.
- Capacitor-input filter — Smooths rectified DC using reservoir capacitance.
- Choke-input filter — Uses inductance to reduce rectifier ripple.
- Series regulator — Controls output through a series pass element.
- Shunt regulator — Regulates voltage by diverting excess current.
- Linear regulator — Feedback-controlled linear voltage regulation.
- LDO regulator — Linear regulator operating with small input/output differential.
- Tracking regulator — Produces supply rails that follow another reference.
- Constant-current regulator — Maintains approximately constant load current.
- Constant-voltage regulator — Maintains approximately constant output voltage.
- Foldback current limiter — Reduces current limit during severe overload.
- Electronic fuse — Electronic overcurrent disconnection/protection.
- Soft-start circuit — Limits startup current or gradually raises output.
- Power sequencing circuit — Controls the order in which supply rails start and stop.

4. SWITCHING POWER CONVERTERS
- Buck converter — Step-down DC/DC converter.
- Boost converter — Step-up DC/DC converter.
- Buck-boost converter — Produces output above or below input.
- Inverting buck-boost — Produces an inverted regulated output.
- SEPIC converter — Non-inverting step-up/step-down converter.
- Ćuk converter — Continuous-current step-up/step-down topology.
- Flyback converter — Transformer-coupled isolated or non-isolated converter.
- Forward converter — Transformer-isolated power converter transferring energy directly during switch conduction.
- Push-pull converter — Alternately drives transformer windings for power conversion.
- Half-bridge converter — Two-switch bridge power topology.
- Full-bridge converter — Four-switch bridge power topology.
- Resonant converter — Uses resonant networks for efficient soft switching.
- Synchronous rectifier — Replaces rectifier diodes with controlled MOSFETs.
- Multiphase converter — Parallel interleaved converter phases for higher current and reduced ripple.

5. VOLTAGE REFERENCES AND CURRENT SOURCES
- Zener reference — Basic regulated reference voltage.
- Bandgap reference — Temperature-stable semiconductor voltage reference.
- Constant-current source — Supplies approximately fixed current.
- Constant-current sink — Draws approximately fixed current.
- BJT current mirror — Replicates reference current.
- MOSFET current mirror — FET implementation of current replication.
- Wilson current mirror — Higher-output-resistance current mirror.
- Widlar current source — Generates small currents using emitter degeneration.
- Temperature-compensated reference — Reduces reference variation with temperature.

6. BJT AMPLIFIERS
- Common-emitter amplifier — Voltage amplifier with phase inversion.
- Common-collector/emitter follower — Buffer with high input and low output impedance.
- Common-base amplifier — Low-input-impedance, high-frequency amplifier.
- BJT differential pair — Amplifies the difference between two inputs.
- Darlington pair — Cascaded BJTs providing high current gain.
- Cascode amplifier — Improves bandwidth and isolation.
- Current-mirror active-load amplifier — Uses active loads for increased gain.
- Complementary amplifier — Uses NPN and PNP devices together.
- Multistage amplifier — Cascades amplifier stages for additional gain/function.
- Push-pull amplifier — Complementary devices drive opposite waveform halves.
- Class A amplifier — Device conducts throughout the waveform cycle.
- Class B amplifier — Devices each conduct approximately half the cycle.
- Class AB amplifier — Biases devices slightly on to reduce crossover distortion.
- Class C amplifier — Conduction for less than half-cycle, commonly used in tuned RF circuits.

7. FET AMPLIFIERS
- Common-source amplifier — FET voltage amplifier analogous to common emitter.
- Source follower — FET buffer analogous to emitter follower.
- Common-gate amplifier — Low-input-impedance FET amplifier.
- MOSFET differential pair — Differential amplifier using MOSFETs.
- JFET input amplifier — High-input-impedance, low-current amplifier.
- FET cascode — High-bandwidth/high-gain cascoded FET arrangement.
- Complementary MOS amplifier — Uses N-channel and P-channel MOSFETs together.
- CMOS inverter amplifier — Uses a CMOS inverter in its analog transition region.

8. POWER AMPLIFIERS
- Class A power amplifier
- Class B power amplifier
- Class AB power amplifier
- Class C tuned power amplifier
- Class D switching amplifier
- Class E resonant switching amplifier
- Class F harmonic-tuned amplifier
- Bridge-tied-load amplifier
- Complementary transistor output stage
- MOSFET power output stage
- Headphone amplifier
- Loudspeaker power amplifier

9. OP-AMP CIRCUITS
- Inverting amplifier — Controlled negative voltage gain.
- Non-inverting amplifier — Controlled positive voltage gain.
- Voltage follower — Unity-gain buffer.
- Summing amplifier — Adds multiple input signals.
- Difference amplifier — Amplifies difference between inputs.
- Instrumentation amplifier — High-precision differential amplification.
- Integrator — Output proportional to time integral of input.
- Differentiator — Output proportional to input rate of change.
- Op-amp comparator — Compares two voltage levels.
- Schmitt trigger — Comparator with hysteresis.
- Transimpedance amplifier — Converts current into voltage.
- Voltage-to-current converter — Produces current proportional to input voltage.
- Precision rectifier — Accurate diode-like rectification of small signals.
- Active peak detector — Precision waveform peak detection.
- Log amplifier — Output proportional to logarithm of input.
- Antilog amplifier — Exponential/antilogarithmic conversion.

10. FILTERS
- First-order RC low-pass filter
- First-order RC high-pass filter
- RL low-pass filter
- RL high-pass filter
- RLC low-pass filter
- RLC high-pass filter
- Band-pass filter
- Band-stop filter
- Notch filter
- All-pass filter
- Butterworth filter — Maximally flat passband.
- Bessel filter — Optimized phase/group-delay behavior.
- Chebyshev Type I filter — Sharper transition with passband ripple.
- Chebyshev Type II filter — Stopband ripple with flat passband.
- Elliptic filter — Very sharp transition with passband and stopband ripple.
- Sallen-Key filter — Common active second-order filter topology.
- Multiple-feedback filter — Op-amp filter topology useful for higher Q.
- State-variable filter — Simultaneously provides multiple filter responses.
- Biquad filter — General second-order active filter section.
- Linkwitz-Riley crossover — Audio crossover with complementary summed response.
- Twin-T notch filter — Deep rejection around one frequency.
- Passive crossover network — Frequency division for loudspeaker drivers.
- Active crossover network — Amplified/filter-based frequency division.
- Switched-capacitor filter — Filter whose effective resistance is established by switching.

11. OSCILLATORS
- RC phase-shift oscillator
- Wien-bridge oscillator
- Hartley oscillator
- Colpitts oscillator
- Clapp oscillator
- LC oscillator
- Crystal oscillator
- Pierce crystal oscillator
- Relaxation oscillator
- Ring oscillator
- Voltage-controlled oscillator
- Astable multivibrator

12. TIMING AND PULSE CIRCUITS
- 555 astable oscillator
- 555 monostable one-shot
- 555 bistable circuit
- Transistor astable multivibrator
- Monostable multivibrator
- Bistable multivibrator
- Delay circuit
- Pulse stretcher
- Missing-pulse detector
- Switch debounce circuit
- Edge detector
- Clock divider
- Watchdog timer

13. WAVEFORM GENERATORS
- Square-wave generator
- Triangle-wave generator
- Sawtooth generator
- Ramp generator
- Sine-wave generator
- Pulse generator
- PWM generator
- Function generator
- Sweep generator
- Direct-digital-synthesis fundamentals

14. COMPARATORS AND DETECTORS
- Basic comparator
- Comparator with hysteresis
- Schmitt trigger
- Zero-crossing detector
- Window comparator
- Threshold detector
- Level detector
- Undervoltage detector
- Overvoltage detector
- Brownout detector

15. CONTROL SYSTEMS
- Open-loop control
- Closed-loop control
- Negative-feedback control
- Positive-feedback systems
- Proportional controller
- Integral controller
- Derivative controller
- PI controller
- PD controller
- PID controller
- Lead compensator
- Lag compensator
- Lead-lag compensator
- Feed-forward control
- Cascaded control loops
- State-feedback control
- Servo control
- Tracking control

16. CONTROL-SYSTEM BUILDING BLOCKS
- Error amplifier
- Summing junction
- Integrator
- Differentiator
- Loop filter
- Compensation network
- Voltage feedback loop
- Current feedback loop
- Position feedback loop
- Velocity feedback loop
- Temperature feedback loop
- Phase-locked loop

17. MOTOR AND ACTUATOR CIRCUITS
- Brushed DC motor driver
- Low-side motor driver
- Half-bridge motor driver
- H-bridge motor driver
- PWM motor-speed controller
- Motor current controller
- Dynamic-braking circuit
- Regenerative-braking circuit
- BLDC commutation circuit
- Stepper motor driver
- Servo motor controller
- Solenoid driver
- Peak-and-hold solenoid driver
- Proportional solenoid controller

18. TRANSISTOR SWITCHES AND DRIVERS
- BJT low-side switch
- MOSFET low-side switch
- BJT high-side switch
- MOSFET high-side switch
- Complementary transistor switch
- MOSFET gate driver
- Bootstrap high-side driver
- Push-pull gate driver
- Level shifter
- Dead-time generator
- Inductive-load driver

19. DIGITAL LOGIC CIRCUITS
- RTL logic
- DTL logic
- TTL logic
- CMOS logic
- Inverter
- Buffer
- Tri-state buffer
- AND gate
- OR gate
- NAND gate
- NOR gate
- XOR gate
- XNOR gate
- SR latch
- D latch
- D flip-flop
- JK flip-flop
- T flip-flop
- Binary counter
- Decade counter
- Ring counter
- Johnson counter
- Shift register
- Multiplexer
- Demultiplexer
- Encoder
- Priority encoder
- Decoder

20. CLOCKING AND SEQUENTIAL CIRCUITS
- Finite-state-machine fundamentals
- Synchronous counter
- Asynchronous/ripple counter
- Clock-domain synchronizer
- Power-on reset
- Reset supervisor
- Crystal clock oscillator
- Clock buffer
- Clock divider
- Frequency multiplier
- PLL
- DLL fundamentals
- Clock recovery circuit
- Jitter filtering

21. ADC AND DAC CIRCUITS
- Flash ADC
- Successive-approximation ADC
- Integrating ADC
- Dual-slope ADC
- Sigma-delta ADC fundamentals
- Resistor-string DAC
- Binary-weighted DAC
- R-2R ladder DAC
- PWM DAC
- Sample-and-hold
- Track-and-hold
- Anti-alias filter
- Reconstruction filter

22. SENSOR INTERFACES
- Thermistor interface
- RTD interface
- Thermocouple amplifier
- Strain-gauge bridge
- Load-cell amplifier
- Photodiode amplifier
- Phototransistor interface
- LDR interface
- Hall-effect sensor interface
- Current-shunt amplifier
- Capacitive sensor interface
- Inductive sensor interface
- Wheatstone-bridge sensor interface

23. PRECISION MEASUREMENT CIRCUITS
- Instrumentation amplifier
- Differential amplifier
- Kelvin/four-wire measurement
- Guarding circuits
- Driven guard
- Current shunt measurement
- High-side current sensing
- Low-side current sensing
- Precision voltage measurement
- Low-noise preamplifier

24. AUDIO CIRCUITS
- Dynamic microphone preamp
- Electret microphone preamp
- Balanced audio input
- Balanced audio output
- Differential audio receiver
- Line-level preamplifier
- Headphone amplifier
- Speaker amplifier
- Baxandall tone control
- Graphic equalizer
- Parametric equalizer
- Active audio crossover
- Passive audio crossover
- Compressor
- Limiter
- Envelope follower
- Audio mixer/summing amplifier

25. RF CIRCUITS
- L impedance-matching network
- Pi matching network
- T matching network
- Balun
- Tuned RF amplifier
- RF low-noise amplifier
- RF power amplifier
- Mixer
- Product detector
- Envelope detector
- RF modulator
- RF demodulator
- Tuned LC filter
- Antenna matching network

26. COMMUNICATION CIRCUITS
- AM modulator
- AM detector
- FM modulator
- FM detector
- ASK modulator/detector
- FSK modulator/detector
- PSK fundamentals
- PLL demodulator
- Manchester encoder/decoder
- Line driver
- Differential line receiver

27. WIRED DATA INTERFACES
- UART electrical interface
- RS-232 transmitter/receiver
- RS-485 transceiver/termination
- CAN transceiver/termination
- I2C pull-up and buffering networks
- SPI level/interface circuits
- USB protection/interface fundamentals
- Ethernet magnetics and termination fundamentals

28. ISOLATION CIRCUITS
- Optocoupler input/output
- Digital isolator interface
- Transformer signal isolation
- Isolation amplifier
- Isolated gate driver
- Isolated DC/DC supply

29. PROTECTION CIRCUITS
- Fuse protection
- Resettable PTC protection
- TVS transient suppression
- MOV surge protection
- Crowbar overvoltage protection
- Current limiter
- Overcurrent shutdown
- Overvoltage shutdown
- Undervoltage lockout
- Reverse-current protection
- Reverse-polarity protection
- ESD protection
- Thermal shutdown

30. BATTERY CIRCUITS
- Constant-current charger
- Constant-voltage charger
- CC/CV charger
- Battery overcharge protection
- Battery overdischarge protection
- Cell balancing
- Battery monitoring
- Fuel-gauge fundamentals
- Load-sharing circuit
- Ideal-diode power path
- Battery cutoff

31. POWER SWITCHING
- Mechanical relay driver
- Solid-state relay
- SCR switch
- TRIAC switch
- Thyristor control
- Zero-cross AC switch
- Phase-angle controller
- MOSFET power switch
- IGBT power switch

32. SIGNAL CONDITIONING
- Signal amplifier
- Signal attenuator
- DC offset circuit
- Level shifter
- Signal scaler
- Buffer
- Linearizer
- Differential-to-single-ended converter
- Single-ended-to-differential converter
- Signal isolation
- Sensor excitation circuit

33. ANALOG COMPUTING CIRCUITS
- Analog summer
- Analog subtractor
- Integrator
- Differentiator
- Analog multiplier
- Analog divider
- Absolute-value circuit
- RMS converter
- Peak detector
- Sample-and-hold

34. FEEDBACK AND COMPENSATION
- Negative-feedback amplifier
- Positive-feedback circuit
- Dominant-pole compensation
- Miller compensation
- Pole-zero compensation
- Lead compensation
- Lag compensation
- Lead-lag compensation
- Loop compensation
- Load compensation
- RC snubber
- RCD snubber

35. THERMAL CONTROL
- Thermostat
- Temperature sensor amplifier
- Fan controller
- Thermal shutdown
- Heater driver
- Proportional heater controller
- PI temperature controller
- PID temperature controller

36. LED AND LIGHTING CIRCUITS
- LED series-current limiter
- Linear constant-current LED driver
- Switching LED driver
- PWM LED dimmer
- Analog LED dimmer
- LED matrix driver
- High-power LED driver

37. MAGNETIC CIRCUITS
- Mains transformer circuits
- Pulse transformer circuits
- Current transformer interface
- Coupled inductors
- Common-mode choke
- Flyback transformer
- Impedance-matching transformer

38. EMI/EMC CIRCUITS
- Common-mode input filter
- Differential-mode input filter
- Ferrite suppression
- Feedthrough-capacitor filtering
- Power-line input filter
- Output EMI filter
- Cable common-mode suppression
- Ground-noise filtering

39. GROUNDING AND SHIELDING SCHEMES
- Star grounding
- Single-point grounding
- Multipoint grounding
- Analog/digital ground strategy
- Chassis grounding
- Earth grounding
- Ground-loop suppression
- Cable shield termination
- Guard-ring techniques

40. TRANSMISSION-LINE CIRCUITS
- Source termination
- Parallel termination
- Thevenin termination
- AC termination
- Differential termination
- Stub termination
- Controlled-impedance interconnection
- Differential-pair interconnection

41. TEST AND MEASUREMENT CIRCUITS
- Oscilloscope attenuator probe
- Differential probe
- Current probe/interface
- DMM input divider
- Electronic load
- Dummy load
- Signal injector
- Logic probe
- Frequency counter input
- Current measurement shunt
- Precision voltage divider

42. GENERAL ELECTRONICS DESIGN SCHEMES
- Biasing networks
- Level shifting
- Buffering
- Impedance transformation
- Gain stages
- Cascaded gain stages
- Differential signaling
- Common-mode rejection
- Feedback stabilization
- Current limiting
- Voltage limiting
- Noise filtering
- Power-supply decoupling
- Transient suppression
- Thermal compensation
- Component tolerance compensation