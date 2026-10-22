#include "SpiceLibrary.h"
#include "PartCatalog.h"
#include "OscilloscopeInput.h"

#include "CircuitSolver.h"
#include "StepperCatalog.h"
#include "Pololu25DMotor.h"

#include <map>

namespace parts
{
namespace
{
ParamSpec quantity(juce::String key, juce::String label, juce::String unit, juce::String def,
                   Storage storage = Storage::Param, juce::String help = {})
{
    return { key, label, Kind::Quantity, unit, {}, def, storage, help };
}

ParamSpec text(juce::String key, juce::String label, juce::String def, Storage storage = Storage::Param, juce::String help = {})
{
    return { key, label, Kind::Text, {}, {}, def, storage, help };
}

ParamSpec choice(juce::String key, juce::String label, juce::StringArray options, juce::String def,
                 Storage storage = Storage::Param, juce::String help = {})
{
    return { key, label, Kind::Choice, {}, options, def, storage, help };
}

ParamSpec toggle(juce::String key, juce::String label, juce::String off, juce::String on, juce::String def, juce::String help = {})
{
    return { key, label, Kind::Toggle, {}, { off, on }, def, Storage::Param, help };
}

ParamSpec fraction(juce::String key, juce::String label, juce::String def, juce::String help = {})
{
    return { key, label, Kind::Fraction, {}, {}, def, Storage::Param, help };
}

// Saved with the part and settable by tools, but not shown in the properties pane.
ParamSpec hidden(ParamSpec spec)
{
    spec.hidden = true;
    return spec;
}

ParamSpec when(ParamSpec spec, juce::String condition)
{
    spec.showWhen = std::move(condition);
    return spec;
}

std::vector<ParamSpec> waveformSource(const juce::String& unit)
{
    const auto one = unit == "A" ? juce::String("1m") : juce::String("1");
    return {
        choice("waveform", "Waveform", { "Sine", "Square", "Pulse", "PWL", "Exp" }, "Sine"),
        when(quantity("amplitude", "Amplitude (peak)", unit, one, Storage::Value), "waveform=Sine|Square"),
        when(quantity("frequency", "Frequency", "Hz", "1k", Storage::Frequency), "waveform=Sine|Square"),
        when(quantity("offset", "DC offset", unit, "0"), "waveform=Sine|Square"),
        when(quantity("phase", "Phase", "deg", "0"), "waveform=Sine|Square"),
        when(quantity("duty", "Duty cycle", "", "0.5"), "waveform=Square"),
        when(quantity("offset", "Initial value (V1)", unit, "0"), "waveform=Pulse|Exp"),
        when(quantity("pulsed_value", "Pulsed value (V2)", unit, one), "waveform=Pulse|Exp"),
        when(quantity("delay", "Delay", "s", "0"), "waveform=Pulse|Exp"),
        when(quantity("rise", "Rise time", "s", "1n"), "waveform=Pulse"),
        when(quantity("fall", "Fall time", "s", "1n"), "waveform=Pulse"),
        when(quantity("width", "Pulse width", "s", "500u"), "waveform=Pulse"),
        when(quantity("period", "Period (0 = single pulse)", "s", "1m"), "waveform=Pulse"),
        when(quantity("tau1", "Rise time constant", "s", "100u"), "waveform=Exp"),
        when(quantity("delay2", "Fall starts at", "s", "1m"), "waveform=Exp"),
        when(quantity("tau2", "Fall time constant", "s", "100u"), "waveform=Exp"),
        when(text("pwl", "Points (time value, ...)", "0 0, 1m " + one + ", 2m " + one + ", 3m 0", Storage::Param,
                  "Time-value pairs separated by commas, times increasing; the value holds after the last point."), "waveform=PWL"),
    };
}

std::map<juce::String, std::vector<ParamSpec>> buildCatalog()
{
    std::map<juce::String, std::vector<ParamSpec>> c;
    c["resistor"] = { quantity("value", "Resistance", "ohm", "10k", Storage::Value),
                      quantity("tempco", "Temperature coefficient", "ppm/K", "0", Storage::Param, "Used by temperature analyses (models are at 27 C)") };
    c["potentiometer"] = { quantity("value", "Total resistance", "ohm", "10k", Storage::Value),
                           fraction("position", "Wiper position", "0.5", "0 = pin 1 end, 1 = pin 2 end") };
    c["capacitor"] = { quantity("value", "Capacitance", "F", "1u", Storage::Value),
                       quantity("initial_voltage", "Initial voltage", "V", "", Storage::Param, "Blank = unspecified; measured from pin 1 to pin 2") };
    c["capacitor_polarized"] = { quantity("value", "Capacitance", "F", "10u", Storage::Value),
                                 quantity("initial_voltage", "Initial voltage", "V", "", Storage::Param, "Blank = unspecified; measured from + to -") };
    c["variable_capacitor"] = { quantity("value", "Capacitance", "F", "100p", Storage::Value) };
    c["inductor"] = { quantity("value", "Inductance", "H", "10m", Storage::Value),
                      quantity("initial_current", "Initial current", "A", "", Storage::Param, "Blank = unspecified; positive from pin 1 to pin 2") };
    c["coupled_inductor"] = { quantity("value", "Inductance (each)", "H", "10m", Storage::Value),
                              quantity("coupling", "Coupling k", "", "0.99", Storage::Param, "0 to 1") };
    c["transformer"] = { text("value", "Turns ratio (primary:secondary)", "1:1", Storage::Value),
                         quantity("primary_inductance", "Primary inductance", "H", "1"),
                         quantity("coupling", "Coupling k", "", "0.999") };
    c["diode"] = { text("value", "Model", "1N4148", Storage::Value),
                   quantity("saturation_current", "Saturation current Is", "A", "2.52n"),
                   quantity("emission", "Emission coefficient n", "", "1.752"),
                   quantity("cj0", "Junction capacitance Cj0", "F", "4p"),
                   quantity("transit_time", "Transit time TT", "s", "20n") };
    c["zener_diode"] = { quantity("value", "Zener voltage", "V", "5.1", Storage::Value),
                         quantity("saturation_current", "Saturation current Is", "A", "1n"),
                         quantity("emission", "Emission coefficient n", "", "1.5"),
                         quantity("cj0", "Junction capacitance Cj0", "F", "100p"),
                         quantity("transit_time", "Transit time TT", "s", "0") };
    c["schottky_diode"] = { text("value", "Model", "BAT54", Storage::Value),
                            quantity("saturation_current", "Saturation current Is", "A", "100n"),
                            quantity("emission", "Emission coefficient n", "", "1.05"),
                            quantity("cj0", "Junction capacitance Cj0", "F", "10p"),
                            quantity("transit_time", "Transit time TT", "s", "0") };
    c["led"] = { choice("value", "Colour", { "Red", "Green", "Yellow", "Blue", "White" }, "Red", Storage::Value) };
    c["battery"] = { quantity("value", "Voltage", "V", "9", Storage::Value) };
    c["voltage_source"] = { quantity("value", "Voltage", "V", "5", Storage::Value) };
    c["current_source"] = { quantity("value", "Current", "A", "1m", Storage::Value) };
    c["ac_voltage_source"] = waveformSource("V");
    c["signal_source"] = waveformSource("V");
    c["ac_current_source"] = waveformSource("A");
    c["behavioral_voltage_source"] = { text("value", "Voltage expression", "V(CTRL)", Storage::Value, "Xyce expression for source voltage, e.g. 2*V(IN).") };
    c["behavioral_current_source"] = { text("value", "Current expression", "V(CTRL)/1k", Storage::Value, "Xyce expression for source current, e.g. V(IN)/1k.") };
    c["vcvs"] = { quantity("value", "Voltage gain", "V/V", "10", Storage::Value) };
    c["vccs"] = { quantity("value", "Transconductance", "A/V", "1m", Storage::Value) };
    c["ccvs"] = { quantity("value", "Transresistance", "V/A", "1k", Storage::Value) };
    c["cccs"] = { quantity("value", "Current gain", "A/A", "10", Storage::Value) };
    c["opamp_741"] = { text("value", "Model", "uA741", Storage::Value),
                       quantity("gain", "Open-loop gain", "V/V", "200k"),
                       quantity("gbw", "Gain-bandwidth product", "Hz", "1meg", Storage::Param, "Dominant pole at GBW / open-loop gain; 0 = no roll-off"),
                       quantity("headroom", "Output headroom from rails", "V", "1.5") };
    c["opamp_generic"] = { text("value", "Model", "generic_opamp", Storage::Value),
                           quantity("gain", "Open-loop gain", "V/V", "100k"),
                           quantity("headroom", "Output headroom from rails", "V", "0") };
    c["comparator_generic"] = { text("value", "Model", "generic_comparator", Storage::Value),
                                quantity("gain", "Open-loop gain", "V/V", "1meg"),
                                quantity("headroom", "Output headroom from rails", "V", "0") };
    c["comparator_lm311"] = { text("value", "Model", "LM311", Storage::Value),
                              quantity("gain", "Internal preview gain", "V/V", "1meg"),
                              quantity("headroom", "Output headroom from rails", "V", "0.2") };
    c["regulator_fixed_generic"] = { text("value", "Model", "generic_regulator_5v", Storage::Value) };
    c["regulator_adjustable_generic"] = { text("value", "Model", "generic_regulator_adjustable", Storage::Value) };
    c["regulator_lm317"] = { text("value", "Model", "LM317_TRANS", Storage::Value) };
    // Logic gates (LogicGates.h): a first-pass electrical model with an
    // ideal supply (no supply pins). Defaults: a 5 V CMOS-like gate.
    for (const auto* id : { "logic_not", "logic_buffer", "logic_and", "logic_nand", "logic_or", "logic_nor", "logic_xor", "logic_xnor" })
    {
        std::vector<ParamSpec> gate;
        if (juce::String(id) != "logic_not" && juce::String(id) != "logic_buffer")
            gate.push_back(choice("inputs", "Inputs", { "2", "3", "4", "5", "6", "7", "8" }, "2", Storage::Param,
                                  "Number of inputs (A, B, C...); wires on inputs removed by lowering it are removed."));
        gate.push_back(quantity("voh", "Output HIGH level", "V", "5"));
        gate.push_back(quantity("vol", "Output LOW level", "V", "0"));
        gate.push_back(quantity("threshold", "Input switching threshold", "V", "2.5"));
        gate.push_back(quantity("hysteresis", "Input hysteresis", "V", "0", Storage::Param,
                                "Rising switch point at threshold + h/2, falling at threshold - h/2 (Schmitt input when > 0)."));
        gate.push_back(quantity("rout", "Output resistance", "ohm", "50"));
        gate.push_back(quantity("rin", "Input resistance", "ohm", "1G"));
        gate.push_back(quantity("tpd", "Propagation delay", "s", "10n", Storage::Param,
                                "Input crossing to the start of the output change (inertial: shorter input pulses are filtered). Must be > 0."));
        gate.push_back(quantity("tr", "Output transition time", "s", "4n", Storage::Param, "Time the output takes to move between levels."));
        c[id] = gate;
    }
    // Brushed DC motor (Mechanical.h): V = Ra I + La dI/dt + Ke w, J dw/dt =
    // Kt I - B w - Tload, SI units. Defaults: a small 12 V motor (no-load
    // ~597 rad/s, stall 6 A / 0.12 N*m, mechanical time constant ~50 ms).
    for (const auto* id : { "dc_motor", "dc_motor_tach", "dc_motor_servo" })
    {
        std::vector<ParamSpec> m {
            quantity("ra", "Armature resistance", "ohm", "2"),
            quantity("la", "Armature inductance", "H", "1m", Storage::Param, "0 is allowed (no electrical time constant)."),
            quantity("kt", "Torque constant Kt", "N*m/A", "20m", Storage::Param, "Torque per ampere. Equal to Ke in SI units for an ideal motor."),
            quantity("ke", "Back-EMF constant Ke", "V*s/rad", "20m", Storage::Param, "Volts per rad/s. Equal to Kt in SI units for an ideal motor."),
            quantity("j", "Rotor inertia", "kg*m^2", "10u"),
            quantity("b", "Rotor viscous damping", "N*m*s/rad", "1u"),
            quantity("load_torque", "Load torque", "N*m", "0", Storage::Param,
                     "Constant torque on the shaft; positive opposes forward rotation, negative drives the shaft (an overhauling load)."),
            text("load_torque_pwl", "Load torque profile (PWL)", "", Storage::Param,
                 "Optional time-varying load torque in N*m as time value pairs, e.g. 0 0, 100m 0, 101m 0.05; replaces Load torque when set."),
            quantity("load_inertia", "Load inertia", "kg*m^2", "0", Storage::Param, "Added to the rotor inertia."),
            quantity("load_damping", "Load viscous damping", "N*m*s/rad", "0", Storage::Param, "Added to the rotor damping."),
            quantity("friction", "Coulomb friction torque", "N*m", "0", Storage::Param,
                     "Opposes rotation in either direction: friction * tanh(speed / smoothing). No stiction."),
            quantity("friction_smoothing", "Friction smoothing speed", "rad/s", "0.1"),
            quantity("initial_speed", "Initial speed", "rad/s", "0", Storage::Param, "Every transient starts from it (0: at rest)."),
            quantity("initial_position", "Initial angle", "rad", "0"),
            quantity("initial_current", "Initial armature current", "A", "0", Storage::Param, "With inductance > 0; every transient starts from it."),
            quantity("min_angle", "End stop, minimum angle", "rad", "", Storage::Param, "Empty: no stop (continuous rotation)."),
            quantity("max_angle", "End stop, maximum angle", "rad", "", Storage::Param, "Empty: no stop (continuous rotation)."),
            quantity("stop_stiffness", "End-stop stiffness", "N*m/rad", "100"),
            quantity("stop_damping", "End-stop damping", "N*m*s/rad", "10m"),
        };
        if (juce::String(id) == "dc_motor_tach" || juce::String(id) == "dc_motor_servo")
        {
            m.push_back(quantity("tach_constant", "Tachometer constant", "V/(rad/s)", "10m", Storage::Param,
                                 "T+ - T- = constant * speed (10m: 10 V at 1000 rad/s)."));
            m.push_back(quantity("tach_rout", "Tachometer output resistance", "ohm", "100"));
        }
        if (juce::String(id) == "dc_motor_servo")
        {
            m.push_back(quantity("pos_constant", "Position sensor constant", "V/rad", "1.0", Storage::Param,
                                 "P+ - P- = constant * angle (1.0: 1 V per radian)."));
            m.push_back(quantity("pos_rout", "Position sensor output resistance", "ohm", "100"));
        }
        c[id] = m;
    }
    for(const auto& reference:stepper_catalog::motors())
    {
        const bool unipolar=stepper_catalog::isUnipolar(reference.symbol);
        c[reference.symbol]={
            hidden(text("component_id","Shared CAD/DEWB component ID",reference.canonical)),
            text("part_number","Manufacturer part",reference.part),
            text("data_status","Specification confidence",unipolar?"Supplier unknown; Maintex family geometry only; winding/motion coefficients required":"Manufacturer nominal R/L/inertia; sinusoidal reference model, not a torque-speed prediction"),
            choice("calibration","Electromechanical calibration",{"User coefficients","Holding-torque sine reference"},unipolar?"User coefficients":"Holding-torque sine reference",Storage::Param,"Holding reference assumes two equal rated phase currents and sinusoidal flux: Kt=Ke=holding/(sqrt(2)*rated current). This is a modeling assumption, not a measured Kt/Ke."),
            quantity("phase_r","Phase resistance","ohm",stepper_catalog::value(reference.resistance)),
            quantity("phase_l","Phase inductance","H",stepper_catalog::value(reference.inductance)),
            quantity("rotor_j","Rotor inertia","kg*m^2",stepper_catalog::value(reference.inertia)),
            quantity("electrical_cycles","Electrical cycles / rotor revolution","1",reference.fullStep>0?stepper_catalog::value(90/reference.fullStep):juce::String(),Storage::Param,"Bipolar sinusoidal reference: 90/full-step angle. Unipolar 5.625-degree excitation interval does not establish this value; supply the actual motor's value."),
            quantity("holding_torque","Published holding torque","N*m",stepper_catalog::value(reference.holding)),
            quantity("rated_current","Published phase current","A",stepper_catalog::value(reference.current)),
            quantity("kt","User torque constant","N*m/A","",Storage::Param,"Required with User coefficients. Not inferred as a commercial motor specification."),
            quantity("ke","User back-EMF constant","V*s/rad","",Storage::Param,"Required with User coefficients. For energy-consistent ideal conversion use Ke=Kt."),
            quantity("gear_ratio","Separate ideal output reduction","1",stepper_catalog::value(reference.ratio),Storage::Param,"Rotor/output speed ratio. Nominal 64 is unverified for an unidentified 28BYJ; ideal lossless gear only. Output load reflects to the rotor; backlash/efficiency are not modeled."),
            quantity("damping","Ideal rotor viscous damping","N*m*s/rad","0",Storage::Param,"Zero is an explicit frictionless model assumption; commercial damping is unknown."),
            quantity("load_inertia","Output load inertia","kg*m^2","0"),quantity("load_damping","Output viscous damping","N*m*s/rad","0"),quantity("load_torque","Output load torque","N*m","0"),
            quantity("initial_position","Initial rotor angle","rad","0"),quantity("initial_speed","Initial rotor speed","rad/s","0")
        };
    }
    for(const auto* id:{"driver_tmc2209","driver_dm542t_v4"})
    {
        const bool dm=juce::String(id)=="driver_dm542t_v4";
        c[id]={hidden(text("component_id","Shared driver ID",dm?"driver-dm542t-v4":"driver-tmc2209-reference")),
            text("model_scope","Modeled behavior","Averaged current-limited bridge, STEP/DIR/disable and pulse timing. No PWM switching, UART, interpolation, thermal/protection or alarm simulation."),
            quantity("current_peak","Configured peak phase current","A",dm?"2.84":"1.414",Storage::Param,"Peak, not RMS. Approximate sinusoidal RMS=peak/sqrt(2). TMC carrier/sense resistor/cooling are unspecified; this is a configurable reference driver, not a particular module."),
            quantity("microsteps","Microsteps per full step","1","16",Storage::Param,dm?"Supported DM542T V4 DIP choices: 1,2,4,5,8,10,16,20,25,32,40,50,64,100,125,128.":"Reference command resolution 1..256; actual IC strap/UART and interpolation are specification-only."),
            quantity("logic_high","High input threshold (reference)","V",dm?"4.5":"2",Storage::Param,"DM differential control assumes S2=5V. Direct 3.3V input is not accepted. TMC uses a nominal 3.3V interface; actual module VIO/pinout must be verified."),
            quantity("min_pulse","Minimum STEP high/low interval","s",dm?"2.5u":"100n"),quantity("dir_setup","DIR setup before STEP","s",dm?"5u":"20n"),
            text("source","Driver datasheet",dm?"https://www.omc-stepperonline.com/download/DM542T_V4.0_user_manual.pdf":"https://www.analog.com/media/en/technical-documentation/data-sheets/TMC2209_datasheet_rev1.09.pdf")};
    }
    c["driver_uln2003"]={hidden(text("component_id","Shared driver ID","driver-uln2003a-reference")),text("model_scope","Modeled behavior","Four low-side switches with clamp diodes. No current regulation; no package thermal/current-limit enforcement. Carrier pinout unknown."),quantity("threshold","Logic threshold (reference)","V","2.4"),quantity("ron","On resistance (reference)","ohm","1"),quantity("vsat","Darlington drop (reference)","V","1",Storage::Param,"Illustrative regularized saturation drop; not a characterized semiconductor model."),text("ratings","Specification-only ratings","ULN2003A: 50V outputs, 0.5A single channel; aggregate current depends on package/duty/temperature."),text("source","Datasheet","https://www.ti.com/lit/ds/symlink/uln2003a.pdf")};
    for(const auto& reference:pololu25d::references())
    {
        const auto m=pololu25d::endpointModel(reference);
        const auto number=[](double v){return juce::String(v,12);};
        auto& p=c[reference.symbol];p={
            hidden(text("component_id","Shared CAD/DEWB identity",reference.id)),text("part_number","Pololu manufacturer part",reference.part),
            hidden(text("reference_revision","Source snapshot","Pololu 25D 2026-10-22; datasheet July 2026 rev 2.0")),
            hidden(quantity("reference_rpm","Published typical no-load rpm","rpm",number(reference.rpm))),
            hidden(quantity("reference_i0","Published typical no-load current","A",number(reference.noLoadCurrent))),
            hidden(quantity("reference_is","Published extrapolated stall current","A",number(reference.stallCurrent))),
            hidden(quantity("reference_ts_kgcm","Published extrapolated stall torque","kg*cm",number(reference.stallKgCm))),
            hidden(quantity("reference_fit_speed","Published fitted speed slope","rpm/(kg*mm)",number(reference.fitSpeedSlope))),
            hidden(quantity("reference_fit_i0","Published fitted current intercept","A",number(reference.fitCurrentIntercept))),
            hidden(quantity("reference_fit_i_slope","Published fitted current slope","A/(kg*mm)",number(reference.fitCurrentSlope))),
            text("data_status","Model scope","Output-shaft equivalent. Verified typical endpoints; derived terminal coefficients. Unknown intrinsic R/L, rotor/gear inertia and friction. Transient timing uncalibrated."),
            text("nominal_data","Published 12V endpoints",number(reference.rpm)+" rpm; "+number(reference.noLoadCurrent)+" A no load; "+number(reference.stallCurrent)+" A and "+number(reference.stallKgCm)+" kg*cm extrapolated stall; "+number(reference.maxPower)+" W max output"),
            choice("calibration","DC model calibration",{"Published typical endpoints","Datasheet fitted lines","User coefficients"},"Published typical endpoints",Storage::Param,"Output-equivalent R=12/Is; Kt=Ts/Is; Ke=(12-R*I0)/w0; B=Kt*I0/w0. Fit mode derives Is/Ts from the published fitted speed/current lines instead of product typicals. Neither is intrinsic motor/gearbox identification."),
            when(quantity("ra","Effective terminal resistance (derived)","ohm",number(m.ra)),"calibration=User coefficients"),
            when(quantity("kt","Effective output torque constant (derived)","N*m/A",number(m.kt)),"calibration=User coefficients"),
            when(quantity("ke","Effective output back-EMF constant (derived)","V*s/rad",number(m.ke)),"calibration=User coefficients"),
            when(quantity("b","Effective output viscous loss (derived)","N*m*s/rad",number(m.b)),"calibration=User coefficients"),
            quantity("la","Armature inductance (unknown; 0 neglects)","H","0",Storage::Param,"Manufacturer winding inductance is unpublished. Default 0 explicitly neglects electrical transients."),
            quantity("j","Output-equivalent inertia (reference assumption)","kg*m^2","10u",Storage::Param,"Unpublished. Default 1e-5 is a numerical reference inertia, not measured; transient timing requires user calibration."),
            quantity("load_inertia","Added output load inertia","kg*m^2","0"),quantity("load_damping","Output load damping","N*m*s/rad","0"),quantity("load_torque","Opposing output load torque","N*m","0"),
            quantity("initial_position","Initial gearbox output angle","rad","0"),quantity("initial_speed","Initial gearbox output speed","rad/s","0"),
            text("gearbox_limits","Published operating guidance","46.85:1 rounded ratio; continuous <=4 kg*cm and intermittent <=8 kg*cm gearbox torque; continuous current generally <=25% of stall current. No thermal/damage model."),
            text("curve_reference","Complete manufacturer performance curves","assets/manufacturers/pololu/pololu-25d-metal-gearmotors-rev-2-0.pdf, page "+juce::String(reference.curvePage)+". Graph-fit no-load currents differ from product typicals; retained in pololu-25d-reference.json."),
            text("source","Manufacturer source",juce::String("https://www.pololu.com/product/")+reference.part+"/specs")};
        if(reference.encoder)
        {
            p.push_back(quantity("gear_ratio","Encoder-count-consistent ratio","1",number(pololu25d::encoderRatio),Storage::Param,"Calculated 2248.86/48, preserving Pololu's published output counts. Not a tooth-count-derived exact rational ratio; rounded product ratio is 46.85."));
            p.push_back(quantity("encoder_rout","Encoder output resistance (reference)","ohm","100",Storage::Param,"Unpublished output drive impedance; explicit ideal push-pull reference approximation, not a manufacturer rating."));
            p.push_back(text("encoder_spec","Encoder specifications","3.5-20V, max 10mA; 48 counts/rotor revolution (both edges/both channels), 12 cycles; 2248.86 counts/output rev. Red M1, black M2, green GND, blue VCC, yellow A, white B. A/B ideal sampled quadrature; no timing jitter/frequency/current-limit model."));
        }
    }
    c["position_sensor"] = {
        text("motor", "Target motor refdes", "M1", Storage::Param, "Refdes of the motor whose shaft angle is measured."),
        quantity("constant", "Sensor constant", "V/rad", "1.0", Storage::Param, "Output voltage per radian of shaft angle."),
        quantity("rout", "Output resistance", "ohm", "100")
    };
    for (const auto* id : { "npn", "pnp" })
        c[id] = { text("value", "Model", juce::String(id) == "npn" ? "generic_npn" : "generic_pnp", Storage::Value),
                  quantity("beta", "Current gain (beta)", "", "100"),
                  quantity("saturation_current", "Saturation current Is", "A", "10f"),
                  quantity("early_voltage", "Early voltage VAF", "V", "100", Storage::Param, "0 = no Early effect"),
                  quantity("transit_time", "Forward transit time TF", "s", "300p", Storage::Param, "Sets fT with the junction capacitances"),
                  quantity("cje", "B-E junction capacitance", "F", "4.5p"),
                  quantity("cjc", "B-C junction capacitance", "F", "3.5p") };
    for (const auto* id : { "nmos", "pmos" })
        c[id] = { text("value", "Model", juce::String(id) == "nmos" ? "generic_nmos" : "generic_pmos", Storage::Value),
                  quantity("threshold", "Threshold voltage |Vth|", "V", "2"),
                  quantity("k", "Transconductance K", "A/V^2", "20m"),
                  quantity("lambda", "Channel-length modulation", "1/V", "0.01"),
                  quantity("cgs", "Gate-source capacitance", "F", "20p"),
                  quantity("cgd", "Gate-drain capacitance", "F", "5p") };
    for (const auto* id : { "njfet", "pjfet" })
        c[id] = { text("value", "Model", juce::String(id) == "njfet" ? "generic_njfet" : "generic_pjfet", Storage::Value),
                  quantity("idss", "Idss", "A", "10m"),
                  quantity("pinchoff", "Pinch-off |Vp|", "V", "2") };
    c["tube_12ax7"] = {
        choice("section", "Section (Unit)", { "Unit 1", "Unit 2" }, "Unit 1", Storage::Param, "Dual-triode section (pins 6,7,8 or pins 1,2,3)"),
        quantity("mu", "Amplification factor (mu)", "", "100"),
        quantity("ex", "Space-charge exponent (EX)", "", "1.4"),
        quantity("kg1", "Transconductance scale (KG1)", "", "1060"),
        quantity("kp", "Knee cutoff curvature (KP)", "", "600"),
        quantity("kvb", "Knee voltage (KVB)", "V", "300"),
        quantity("rgi", "Grid conduction resistance (RGI)", "ohm", "2k"),
        quantity("cgp", "Grid-to-plate capacitance (Cgp)", "F", "1.7p"),
        quantity("cgk", "Grid-to-cathode capacitance (Cgk)", "F", "1.6p"),
        quantity("cpk", "Plate-to-cathode capacitance (Cpk)", "F", "0.46p")
    };
    c["tube_12au7"] = {
        choice("section", "Section (Unit)", { "Unit 1", "Unit 2" }, "Unit 1", Storage::Param, "Dual-triode section (pins 6,7,8 or pins 1,2,3)"),
        quantity("mu", "Amplification factor (mu)", "", "20"),
        quantity("ex", "Space-charge exponent (EX)", "", "1.3"),
        quantity("kg1", "Transconductance scale (KG1)", "", "1250"),
        quantity("kp", "Knee cutoff curvature (KP)", "", "220"),
        quantity("kvb", "Knee voltage (KVB)", "V", "240"),
        quantity("rgi", "Grid conduction resistance (RGI)", "ohm", "2k"),
        quantity("cgp", "Grid-to-plate capacitance (Cgp)", "F", "1.5p"),
        quantity("cgk", "Grid-to-cathode capacitance (Cgk)", "F", "1.6p"),
        quantity("cpk", "Plate-to-cathode capacitance (Cpk)", "F", "0.4p")
    };
    c["tube_12at7"] = {
        choice("section", "Section (Unit)", { "Unit 1", "Unit 2" }, "Unit 1", Storage::Param, "Dual-triode section (pins 6,7,8 or pins 1,2,3)"),
        quantity("mu", "Amplification factor (mu)", "", "60"),
        quantity("ex", "Space-charge exponent (EX)", "", "1.3"),
        quantity("kg1", "Transconductance scale (KG1)", "", "560"),
        quantity("kp", "Knee cutoff curvature (KP)", "", "300"),
        quantity("kvb", "Knee voltage (KVB)", "V", "300"),
        quantity("rgi", "Grid conduction resistance (RGI)", "ohm", "2k"),
        quantity("cgp", "Grid-to-plate capacitance (Cgp)", "F", "1.5p"),
        quantity("cgk", "Grid-to-cathode capacitance (Cgk)", "F", "2.2p"),
        quantity("cpk", "Plate-to-cathode capacitance (Cpk)", "F", "0.45p")
    };
    c["tube_12ay7"] = {
        choice("section", "Section (Unit)", { "Unit 1", "Unit 2" }, "Unit 1", Storage::Param, "Dual-triode section (pins 6,7,8 or pins 1,2,3)"),
        quantity("mu", "Amplification factor (mu)", "", "46"),
        quantity("ex", "Space-charge exponent (EX)", "", "1.25"),
        quantity("kg1", "Transconductance scale (KG1)", "", "1250"),
        quantity("kp", "Knee cutoff curvature (KP)", "", "200"),
        quantity("kvb", "Knee voltage (KVB)", "V", "600"),
        quantity("rgi", "Grid conduction resistance (RGI)", "ohm", "2k"),
        quantity("cgp", "Grid-to-plate capacitance (Cgp)", "F", "1.5p"),
        quantity("cgk", "Grid-to-cathode capacitance (Cgk)", "F", "1.3p"),
        quantity("cpk", "Plate-to-cathode capacitance (Cpk)", "F", "0.5p")
    };
    c["tube_triode"] = {
        choice("value", "Tube Type", { "12AX7", "12AU7", "12AT7", "12AY7", "generic" }, "12AX7", Storage::Value),
        choice("section", "Section (Unit)", { "Unit 1", "Unit 2" }, "Unit 1", Storage::Param),
        quantity("mu", "Amplification factor (mu)", "", "100"),
        quantity("ex", "Space-charge exponent (EX)", "", "1.4"),
        quantity("kg1", "Transconductance scale (KG1)", "", "1060"),
        quantity("kp", "Knee cutoff curvature (KP)", "", "600"),
        quantity("kvb", "Knee voltage (KVB)", "V", "300"),
        quantity("rgi", "Grid conduction resistance (RGI)", "ohm", "2k"),
        quantity("cgp", "Grid-to-plate capacitance (Cgp)", "F", "1.7p"),
        quantity("cgk", "Grid-to-cathode capacitance (Cgk)", "F", "1.6p"),
        quantity("cpk", "Plate-to-cathode capacitance (Cpk)", "F", "0.46p")
    };
    c["switch_spst"] = { toggle("state", "Contacts", "Open", "Closed", "Open") };
    c["switch_spdt"] = { choice("state", "Common connects to", { "A", "B" }, "A") };
    c["voltage_controlled_switch"] = { quantity("ron", "On resistance", "ohm", "1"),
                                       quantity("roff", "Off resistance", "ohm", "1G"),
                                       quantity("threshold", "Switch threshold", "V", "2.5"),
                                       quantity("hysteresis", "Hysteresis", "V", "0") };
    c["current_controlled_switch"] = { quantity("ron", "On resistance", "ohm", "1"),
                                       quantity("roff", "Off resistance", "ohm", "1G"),
                                       quantity("threshold", "Switch threshold", "A", "1m"),
                                       quantity("hysteresis", "Hysteresis", "A", "0") };
    c["relay_spst"] = { toggle("state", "Contacts", "Open", "Closed", "Open"),
                        quantity("coil_resistance", "Coil resistance", "ohm", "100") };
    c["fuse"] = { quantity("value", "Rating", "A", "1", Storage::Value) };
    c["power_port"] = { text("busName", "Supply net name", "+5V", Storage::BusName, "Ports with the same name are one net; a leading - draws it pointing down") };
    c["net_label"] = { text("busName", "Label", "NET1", Storage::BusName, "Labels with the same name are one net"),
                       quantity("initial_voltage", "Initial voltage", "V", "", Storage::Param, "Blank = unspecified; emits .IC for this net") };
    c["power_bus"] = { text("busName", "Rail name", "+V", Storage::BusName) };
    c["audio_in"] = { choice("source", "Input Type", { "Hardware", "File" }, "Hardware"), text("file", "File Path", "") };
    c["audio_out"] = { choice("sink", "Output Type", { "Hardware", "File" }, "Hardware"), text("file", "File Path", "") };
    c["ground_bus"] = { text("busName", "Rail name", "0", Storage::BusName) };
    c["oscilloscope_2ch"] = { quantity("time_per_div", "Time / division", "s", "200u"),
        quantity("horizontal_position", "Horizontal position", "s", "0"),
        quantity("capture_duration", "Record duration", "s", "20m"), quantity("capture_step", "Maximum sample step", "s", "10u"),
        choice("trigger_source", "Trigger source", { "CH1", "CH2" }, "CH1"),
        choice("trigger_mode", "Trigger mode", { "Auto", "Normal", "Single" }, "Auto"),
        quantity("trigger_level", "Trigger level", "V", "0"),
        quantity("trigger_position", "Trigger position", "%", "20"),
        choice("trigger_slope", "Trigger slope", { "Rising", "Falling" }, "Rising"),
        choice("cursors", "Measurement cursors", { "Off", "On" }, "Off"),
        choice("cursor_source", "Cursor voltage channel", { "CH1", "CH2" }, "CH1"),
        quantity("cursor_t1", "Time cursor 1", "s", "0"), quantity("cursor_t2", "Time cursor 2", "s", "1m"),
        quantity("cursor_v1", "Voltage cursor 1", "V", "0"), quantity("cursor_v2", "Voltage cursor 2", "V", "1"),
        choice("markers", "Sample markers", { "Off", "On" }, "Off"),
        choice("math", "Waveform math", { "Off", "CH1 + CH2", "CH1 - CH2" }, "Off"),
        quantity("math_scale", "Math gain", "", "1"), quantity("math_volts_per_div", "Math volts / division", "V", "1") };
    for (int channel = 1; channel <= 2; ++channel)
    {
        const auto prefix = "ch" + juce::String(channel) + "_";
        const auto title = "CH" + juce::String(channel) + " ";
        auto& scope = c["oscilloscope_2ch"];
        scope.push_back(choice(prefix + "enabled", title + "trace", { "On", "Off" }, "On"));
        scope.push_back(choice(prefix + "coupling", title + "coupling", { "DC", "AC" }, "DC"));
        scope.push_back(quantity(prefix + "volts_per_div", title + "volts / division", "V", "500m"));
        scope.push_back(quantity(prefix + "offset", title + "display offset", "V", "0"));
        scope.push_back(quantity(prefix + "position", title + "position", "div", "0"));
        scope.push_back(quantity(prefix + "scale", title + "input scaling", "", "1"));
        scope.push_back(choice(prefix + "impedance_enabled", title + "input impedance", { "Off", "On" }, "On",
            Storage::Param, "Off: infinite impedance, no load. On: resistive load to REF. Independent of trace visibility."));
        scope.push_back(quantity(prefix + "impedance", title + "finite input impedance", "ohm", "10M",
            Storage::Param, "Positive resistance, e.g. 50, 1k, 10M or 1G. M means mega. Retained while Off."));
    }
    c["digital_multimeter"] = { choice("value", "Function", { "DC V", "AC V", "DC A", "Ohms" }, "DC V", Storage::Value) };
    c["bode_analyzer"] = { quantity("start_frequency", "Start frequency", "Hz", "10"),
                           quantity("stop_frequency", "Stop frequency", "Hz", "100k"),
                           quantity("points_per_decade", "Points per decade", "", "20") };
    // Plotter channels A/B/C measure their + pin against their - pin (ground when unwired).
    c["xyz_plotter"] = { choice("mode", "Display", { "Time", "XY", "XYZ" }, "XY", Storage::Param, "Time: traces against time. XY / XYZ: one signal per axis, plotted against each other."),
                         when(text("x", "X signal", "A", Storage::Param, "A channel (A, B, C), V(net), V(net1,net2) or I(part)"), "mode=XY|XYZ"),
                         when(text("y", "Y signal", "B", Storage::Param, "A channel (A, B, C), V(net), V(net1,net2) or I(part)"), "mode=XY|XYZ"),
                         when(text("z", "Z signal", "C", Storage::Param, "A channel (A, B, C), V(net), V(net1,net2) or I(part)"), "mode=XYZ"),
                         when(text("traces", "Traces", "A, B", Storage::Param, "Comma-separated channels, V(net), V(net1,net2) or I(part)"), "mode=Time"),
                         quantity("stop", "Stop time", "s", "10m"),
                         quantity("step", "Max time step", "s", "10u"),
                         quantity("start", "Plot from", "s", "0", Storage::Param, "Samples before this time are not plotted (lets start-up transients settle)."),
                         choice("engine", "Simulator", { "Internal", "Xyce" }, "Internal"),
                         toggle("markers", "Sample markers", "Off", "On", "Off"),
                         when(choice("projection", "Projection", { "Perspective", "Orthographic" }, "Perspective"), "mode=XYZ"),
                         text("x_min", "X / time min", "auto"), text("x_max", "X / time max", "auto"),
                         text("y_min", "Y min", "auto"), text("y_max", "Y max", "auto"),
                         when(text("z_min", "Z min", "auto"), "mode=XYZ"), when(text("z_max", "Z max", "auto"), "mode=XYZ"),
                         hidden(text("view", "View", "yaw=-35 pitch=25 zoom=1 panx=0 pany=0")) };
    c["annotation_text"] = { text("value", "Text", "Note", Storage::Value) };
    return c;
}

const std::map<juce::String, std::vector<ParamSpec>>& catalog()
{
    spice_library::initialize(); static std::map<juce::String, std::vector<ParamSpec>> c = buildCatalog();
    return c;
}
}

const std::vector<ParamSpec>& paramsFor(const juce::String& symbolId)
{
    static const std::vector<ParamSpec> none;
    const auto found = catalog().find(symbolId);
    return found != catalog().end() ? found->second : none;
}

bool isShown(const ParamSpec& spec, const std::function<juce::String(const juce::String&)>& valueOf)
{
    if (spec.hidden)
        return false;
    if (spec.showWhen.isEmpty())
        return true;
    const auto key = spec.showWhen.upToFirstOccurrenceOf("=", false, false).trim();
    const auto allowed = juce::StringArray::fromTokens(spec.showWhen.fromFirstOccurrenceOf("=", false, false), "|", "");
    return allowed.contains(valueOf(key).trim());
}
const ParamSpec* findParam(const juce::String& symbolId, const juce::String& key)
{
    for (const auto& spec : paramsFor(symbolId))
        if (spec.key == key)
            return &spec;
    return nullptr;
}

juce::String displayName(const juce::String& id)
{
    if(const auto* r=pololu25d::find(id.toStdString()))return r->name;
    if(const auto* reference=stepper_catalog::find(id))return reference->name;
    if(stepper_catalog::isDriver(id))return id=="driver_uln2003"?"ULN2003A unipolar driver reference":id=="driver_tmc2209"?"TMC2209 driver reference":"STEPPERONLINE DM542T V4 driver";
    static const std::map<juce::String, juce::String> names {
        { "resistor", "Resistor" }, { "potentiometer", "Potentiometer" }, { "capacitor", "Capacitor" },
        { "capacitor_polarized", "Polarized Capacitor" }, { "variable_capacitor", "Variable Capacitor" },
        { "inductor", "Inductor" }, { "coupled_inductor", "Coupled Inductors" }, { "transformer", "Transformer" },
        { "diode", "Diode" }, { "zener_diode", "Zener Diode" }, { "schottky_diode", "Schottky Diode" }, { "led", "LED" },
        { "battery", "Battery" }, { "voltage_source", "DC Voltage Source" }, { "current_source", "DC Current Source" },
        { "ac_voltage_source", "AC Voltage Source" }, { "signal_source", "Signal Source" }, { "ac_current_source", "AC Current Source" },
        { "vcvs", "Voltage-Controlled Voltage Source" }, { "vccs", "Voltage-Controlled Current Source" },
        { "ccvs", "Current-Controlled Voltage Source" }, { "cccs", "Current-Controlled Current Source" },
        { "opamp_generic", "Generic Op Amp" }, { "opamp_741", "741 Op Amp" },
        { "comparator_generic", "Generic Comparator" }, { "comparator_lm311", "LM311 Comparator" },
        { "regulator_fixed_generic", "Generic Fixed Regulator" }, { "regulator_adjustable_generic", "Generic Adjustable Regulator" },
        { "regulator_lm317", "LM317 Regulator" },
        { "npn", "NPN Transistor" }, { "pnp", "PNP Transistor" },
        { "nmos", "N-Channel MOSFET" }, { "pmos", "P-Channel MOSFET" }, { "njfet", "N-Channel JFET" }, { "pjfet", "P-Channel JFET" },
        { "tube_12ax7", "12AX7 / ECC83 Dual Triode" }, { "tube_12au7", "12AU7 / ECC82 Dual Triode" }, { "tube_triode", "Triode Vacuum Tube" },
        { "switch_spst", "SPST Switch" }, { "switch_spdt", "SPDT Switch" },
        { "voltage_controlled_switch", "Voltage-Controlled Switch" }, { "current_controlled_switch", "Current-Controlled Switch" },
        { "relay_spst", "SPST Relay" }, { "fuse", "Fuse" },
        { "connector_2", "2-Pin Connector" }, { "connector_3", "3-Pin Connector" }, { "test_point", "Test Point" },
        { "ground", "Ground" }, { "power_port", "Supply Port" }, { "audio_in", "Audio Input" }, { "audio_out", "Audio Output" }, { "net_label", "Net Label" },
        { "power_bus", "Power Bus" }, { "ground_bus", "Ground Bus" },
        { "oscilloscope_2ch", "Oscilloscope" }, { "digital_multimeter", "Digital Multimeter" }, { "bode_analyzer", "Frequency Analyzer" }, { "xyz_plotter", "2D/3D Plotter" },
        { "sub_block", "Sub-diagram Block" }, { "block_port", "Sub-diagram Port" },
        { "frust_component", "FRust Programmable Component" },
        { "annotation_text", "Text Note" },
        { "logic_not", "Inverter" }, { "logic_buffer", "Buffer" }, { "logic_and", "AND Gate" }, { "logic_or", "OR Gate" },
        { "logic_nand", "NAND Gate" }, { "logic_nor", "NOR Gate" }, { "logic_xor", "XOR Gate" }, { "logic_xnor", "XNOR Gate" },
        { "dc_motor", "DC Motor" }, { "dc_motor_tach", "DC Motor with Tachometer" },
        { "dc_motor_servo", "DC Servomotor" }, { "position_sensor", "Rotary Position Sensor" } };
    if (const auto* tube = findVacuumTubeSpec(id))
        return tube->name;
    const auto found = names.find(id);
    return found != names.end() ? found->second : id;
}

juce::String simulationFidelity(const juce::String& id)
{
    if(pololu25d::isMotor(id.toStdString()))return "manufacturer_endpoint_reference";
    if(stepper_catalog::isMotor(id)||stepper_catalog::isDriver(id))return "reference_model";
    if (id == "opamp_741" || id == "comparator_lm311" || id == "regulator_lm317")
        return "vendor_model";
    if (id == "opamp_generic" || id == "comparator_generic"
        || id == "regulator_fixed_generic" || id == "regulator_adjustable_generic")
        return "generic_model";
    if (id == "voltage_controlled_switch" || id == "current_controlled_switch")
        return "generic_model";
    if (const auto* tube = findVacuumTubeSpec(id))
        return tube->simulated ? "generic_model" : "unsupported";
    if (id == "diode" || id == "zener_diode" || id == "schottky_diode" || id == "led"
        || id == "npn" || id == "pnp" || id == "nmos" || id == "pmos" || id == "njfet" || id == "pjfet"
        || id == "tube_12ax7" || id == "tube_12au7" || id == "tube_12at7" || id == "tube_12ay7" || id == "tube_triode")
        return "generic_model";
    // Logic gates: levels, thresholds, drive and delay (internal solver).
    if (id.startsWith("logic_"))
        return "generic_model";
    // Electromechanical: the motor's equations on the internal solver.
    if (id == "dc_motor" || id == "dc_motor_tach" || id == "dc_motor_servo" || id == "position_sensor")
        return "generic_model";
    if (id == "switch_spst" || id == "switch_spdt" || id == "fuse")
        return "ideal";
    // Its program runs in the internal solver at every Newton iteration (Xyce cannot call it).
    if (id == "frust_component")
        return "programmable";
    if (id == "relay_spst" || id == "oscilloscope_2ch" || id == "digital_multimeter" || id == "bode_analyzer" || id == "xyz_plotter"
        || id == "audio_out" || id == "sub_block" || id == "block_port" || id == "annotation_text")
        return "unsupported";
    return "primitive";
}

bool validate(const ParamSpec& spec, const juce::String& value, juce::String& error)
{
    if (spec.key == "ch1_impedance" || spec.key == "ch2_impedance")
    {
        double ohms = 0;
        if (oscilloscope::parseInputOhms(value.toStdString(), ohms)) return true;
        error = "Input impedance must be a finite resistance greater than zero. Use 50, 1k, 10M or 1G ohm.";
        return false;
    }
    switch (spec.kind)
    {
        case Kind::Quantity:
        {
            double parsed = 0.0;
            auto textValue = value.trim();
            if (textValue.isEmpty() && spec.defaultValue.trim().isEmpty())
                return true;
            if (textValue.startsWithChar('{') && textValue.endsWithChar('}'))
                return true;
            // European voltage notation 5V1 = 5.1 V.
            if (spec.unit == "V" && textValue.containsChar('V') && textValue.upToFirstOccurrenceOf("V", false, false).isNotEmpty()
                && textValue.upToFirstOccurrenceOf("V", false, false).containsOnly("0123456789")
                && textValue.fromFirstOccurrenceOf("V", false, false).containsOnly("0123456789") && textValue.fromFirstOccurrenceOf("V", false, false).isNotEmpty())
                textValue = textValue.replace("V", ".");
            auto reject = [&] {
                error = "\"" + value + "\" is not a number" + (spec.unit.isNotEmpty() ? " in " + spec.unit : juce::String())
                      + ". Use forms like 4.7k, 10u, 2.2n, 1meg.";
                return false;
            };
            if (!circuit_sim::parseValue(textValue.toStdString(), parsed))
                return reject();

            // Anything after the number and its prefix must be this value's unit.
            int pos = 0;
            while (pos < textValue.length() && juce::String("0123456789.+-eE").containsChar(textValue[pos]))
            {
                // An 'e' only belongs to the number when digits follow (1e3), not in "meg".
                if ((textValue[pos] == 'e' || textValue[pos] == 'E')
                    && !(pos + 1 < textValue.length() && juce::String("0123456789+-").containsChar(textValue[pos + 1])))
                    break;
                ++pos;
            }
            auto suffix = textValue.substring(pos).trim();
            if (suffix.startsWithIgnoreCase("meg")) suffix = suffix.substring(3);
            else if (suffix.isNotEmpty() && juce::String("TGMkKmunpf").containsChar(suffix[0])) suffix = suffix.substring(1);
            else if (suffix.startsWith(juce::String::fromUTF8("Âµ"))) suffix = suffix.substring(1);
            while (suffix.isNotEmpty() && juce::CharacterFunctions::isDigit(suffix[0])) suffix = suffix.substring(1); // 4k7
            if (suffix.isEmpty())
                return true;
            juce::StringArray accepted { spec.unit };
            if (spec.unit == "ohm") accepted.addArray({ "ohms", "r", juce::String::fromUTF8("Î©") });
            if (spec.unit == "deg") accepted.add(juce::String::fromUTF8("Â°"));
            for (const auto& unit : accepted)
                if (unit.isNotEmpty() && suffix.equalsIgnoreCase(unit))
                    return true;
            return reject();
        }
        case Kind::Choice:
        case Kind::Toggle:
            if (!spec.options.contains(value))
            {
                error = "Choose one of: " + spec.options.joinIntoString(", ") + ".";
                return false;
            }
            return true;
        case Kind::Fraction:
        {
            const auto v = value.getDoubleValue();
            if (!value.trim().containsOnly("0123456789.") || v < 0.0 || v > 1.0)
            {
                error = "Enter a number from 0 to 1.";
                return false;
            }
            return true;
        }
        case Kind::Text:
            if (value.trim().isEmpty())
            {
                if (spec.defaultValue.trim().isEmpty())
                    return true;
                error = spec.label + " cannot be empty.";
                return false;
            }
            return true;
    }
    return true;
}
}





