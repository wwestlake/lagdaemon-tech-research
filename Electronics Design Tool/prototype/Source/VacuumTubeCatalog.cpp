#include "VacuumTubeCatalog.h"

namespace parts
{

namespace
{
const std::vector<VacuumTubeSpec>& getCatalog()
{
    static const std::vector<VacuumTubeSpec> catalog = {
        // ==========================================
        // 1. Small-Signal Triodes (8 types)
        // ==========================================
        {
            "tube_12ax7",
            "12AX7 / ECC83 Dual Triode",
            "Small-Signal Triodes",
            "High-mu twin triode (voltage amplifier)",
            "Glass miniature envelope (T-6 1/2), Noval 9-pin base (E9-1 / B9A)",
            "Sec 1: Pin 1=Plate, Pin 2=Grid, Pin 3=Cathode; Sec 2: Pin 6=Plate, Pin 7=Grid, Pin 8=Cathode; Heater: Pins 4-5 (12.6V) or Pins 4/5-9 (6.3V center-tap)",
            "6.3 V @ 300 mA (parallel) or 12.6 V @ 150 mA (series); indirectly heated oxide cathode",
            "Vp(max): 330 V; Pp(max): 1.0 W per section, 1.2 W total; Ik(max): 8 mA; Vhk(max): 180 V (cathode positive/negative)",
            "Vp = 250 V, Vg = -2.0 V -> Ip = 1.2 mA, gm = 1.6 mA/V (1600 umho), rp = 62.5 kOhm, mu = 100\n"
            "Vp = 100 V, Vg = -1.0 V -> Ip = 0.5 mA, gm = 1.25 mA/V (1250 umho), rp = 80 kOhm, mu = 100",
            "RCA Receiving Tube Manual RC-30 (1975), Philips/Mullard Technical Handbook Book 2 Part 1",
            true
        },
        {
            "tube_12au7",
            "12AU7 / ECC82 Dual Triode",
            "Small-Signal Triodes",
            "Medium-mu twin triode (driver / multivibrator)",
            "Glass miniature envelope (T-6 1/2), Noval 9-pin base (E9-1 / B9A)",
            "Sec 1: Pin 1=Plate, Pin 2=Grid, Pin 3=Cathode; Sec 2: Pin 6=Plate, Pin 7=Grid, Pin 8=Cathode; Heater: Pins 4-5 (12.6V) or Pins 4/5-9 (6.3V center-tap)",
            "6.3 V @ 300 mA (parallel) or 12.6 V @ 150 mA (series); indirectly heated oxide cathode",
            "Vp(max): 330 V; Pp(max): 2.75 W per section; Ik(max): 20 mA; Vhk(max): 180 V",
            "Vp = 250 V, Vg = -8.5 V -> Ip = 10.5 mA, gm = 2.2 mA/V (2200 umho), rp = 7.7 kOhm, mu = 17\n"
            "Vp = 100 V, Vg = 0 V -> Ip = 11.8 mA, gm = 3.1 mA/V (3100 umho), rp = 6.25 kOhm, mu = 19.5",
            "RCA Receiving Tube Manual RC-30 (1975), Mullard Data Sheet ECC82 (1969)",
            true
        },
        {
            "tube_12at7",
            "12AT7 / ECC81 Dual Triode",
            "Small-Signal Triodes",
            "High-mu twin triode (phase inverter / RF oscillator)",
            "Glass miniature envelope (T-6 1/2), Noval 9-pin base (E9-1 / B9A)",
            "Sec 1: Pin 1=Plate, Pin 2=Grid, Pin 3=Cathode; Sec 2: Pin 6=Plate, Pin 7=Grid, Pin 8=Cathode; Heater: Pins 4-5 (12.6V) or Pins 4/5-9 (6.3V center-tap)",
            "6.3 V @ 300 mA (parallel) or 12.6 V @ 150 mA (series); indirectly heated oxide cathode",
            "Vp(max): 300 V; Pp(max): 2.5 W per section; Ik(max): 20 mA; Vhk(max): 90 V",
            "Vp = 250 V, Vg = -2.0 V (Rk = 200 Ohm) -> Ip = 10.0 mA, gm = 5.5 mA/V (5500 umho), rp = 10.9 kOhm, mu = 60\n"
            "Vp = 100 V, Vg = -1.0 V (Rk = 270 Ohm) -> Ip = 3.7 mA, gm = 4.0 mA/V (4000 umho), rp = 15.0 kOhm, mu = 60",
            "RCA Receiving Tube Manual RC-30 (1975), Mullard Technical Handbook ECC81",
            true
        },
        {
            "tube_12ay7",
            "12AY7 / 6072 Dual Triode",
            "Small-Signal Triodes",
            "Medium-mu twin triode (low-noise audio preamplifier)",
            "Glass miniature envelope (T-6 1/2), Noval 9-pin base (E9-1 / B9A)",
            "Sec 1: Pin 1=Plate, Pin 2=Grid, Pin 3=Cathode; Sec 2: Pin 6=Plate, Pin 7=Grid, Pin 8=Cathode; Heater: Pins 4-5 (12.6V) or Pins 4/5-9 (6.3V center-tap)",
            "6.3 V @ 300 mA (parallel) or 12.6 V @ 150 mA (series); indirectly heated oxide cathode",
            "Vp(max): 300 V; Pp(max): 1.5 W per section; Ik(max): 10 mA; Vhk(max): 90 V",
            "Vp = 250 V, Vg = -4.0 V -> Ip = 3.0 mA, gm = 1.75 mA/V (1750 umho), rp = 25.0 kOhm, mu = 44\n"
            "Vp = 150 V, Vg = -2.0 V -> Ip = 2.5 mA, gm = 1.6 mA/V (1600 umho), rp = 27.0 kOhm, mu = 43",
            "GE Essential Characteristics (1973), Tung-Sol 12AY7/6072 Technical Data",
            true
        },
        {
            "tube_5751",
            "5751 High-Reliability Dual Triode",
            "Small-Signal Triodes",
            "High-mu twin triode (low-noise, ruggedized audio/military)",
            "Glass miniature envelope (T-6 1/2), Noval 9-pin base (B9A)",
            "Sec 1: Pin 1=Plate, Pin 2=Grid, Pin 3=Cathode; Sec 2: Pin 6=Plate, Pin 7=Grid, Pin 8=Cathode; Heater: Pins 4-5 (12.6V) or Pins 4/5-9 (6.3V)",
            "6.3 V @ 350 mA (parallel) or 12.6 V @ 175 mA (series); indirectly heated oxide cathode",
            "Vp(max): 330 V; Pp(max): 1.0 W per section; Ik(max): 9 mA; Vhk(max): 100 V",
            "Vp = 250 V, Vg = -3.0 V -> Ip = 1.0 mA, gm = 1.2 mA/V (1200 umho), rp = 58.0 kOhm, mu = 70\n"
            "Vp = 100 V, Vg = -1.0 V -> Ip = 0.4 mA, gm = 1.0 mA/V (1000 umho), rp = 70.0 kOhm, mu = 70",
            "General Electric 5751 Specification Sheet ET-T1267 (1962), RCA 5751 Data",
            false
        },
        {
            "tube_6sl7gt",
            "6SL7GT / ECC35 Dual Triode",
            "Small-Signal Triodes",
            "High-mu octal twin triode (voltage amplifier)",
            "Glass octal envelope (T-9), Octal 8-pin base (8-pin phenolic)",
            "Sec 1: Pin 1=Grid, Pin 2=Plate, Pin 3=Cathode; Sec 2: Pin 4=Grid, Pin 5=Plate, Pin 6=Cathode; Heater: Pins 7-8 (6.3V)",
            "6.3 V @ 300 mA; indirectly heated oxide cathode",
            "Vp(max): 300 V; Pp(max): 1.0 W per section; Ik(max): 5 mA; Vhk(max): 90 V",
            "Vp = 250 V, Vg = -2.0 V -> Ip = 2.3 mA, gm = 1.6 mA/V (1600 umho), rp = 44.0 kOhm, mu = 70\n"
            "Vp = 100 V, Vg = -1.0 V -> Ip = 0.8 mA, gm = 1.2 mA/V (1200 umho), rp = 58.0 kOhm, mu = 70",
            "RCA Receiving Tube Manual RC-30 (1975), Sylvania 6SL7GT Data",
            false
        },
        {
            "tube_6dj8",
            "6DJ8 / ECC88 Dual Triode",
            "Small-Signal Triodes",
            "High-transconductance frame-grid twin triode (VHF/low-noise audio)",
            "Glass miniature envelope (T-6 1/2), Noval 9-pin base (B9A)",
            "Sec 1: Pin 1=Plate, Pin 2=Grid, Pin 3=Cathode; Sec 2: Pin 6=Plate, Pin 7=Grid, Pin 8=Cathode; Pin 9=Internal Shield; Heater: Pins 4-5 (6.3V)",
            "6.3 V @ 365 mA; indirectly heated oxide cathode",
            "Vp(max): 130 V (working), 550 V (cold); Pp(max): 1.8 W per section; Ik(max): 25 mA; Vhk(max): 50 V",
            "Vp = 90 V, Vg = -1.3 V (Rk = 82 Ohm) -> Ip = 15.0 mA, gm = 12.5 mA/V (12500 umho), rp = 2.6 kOhm, mu = 33",
            "Amperex Electronic Corporation Data Sheet 6DJ8 (1964), Philips ECC88",
            false
        },
        {
            "tube_6922",
            "6922 / E88CC Premium Dual Triode",
            "Small-Signal Triodes",
            "Special-quality ruggedized frame-grid twin triode (gold pins, 10,000 hr life)",
            "Glass miniature envelope (T-6 1/2), Noval 9-pin base (B9A gold-plated)",
            "Sec 1: Pin 1=Plate, Pin 2=Grid, Pin 3=Cathode; Sec 2: Pin 6=Plate, Pin 7=Grid, Pin 8=Cathode; Pin 9=Internal Shield; Heater: Pins 4-5 (6.3V)",
            "6.3 V @ 300 mA (+/-5%); indirectly heated oxide cathode",
            "Vp(max): 220 V (working), 550 V (cold); Pp(max): 1.5 W per section; Ik(max): 20 mA; Vhk(max): 120 V",
            "Vp = 90 V, Vg = -1.2 V (Rk = 82 Ohm) -> Ip = 15.0 mA, gm = 12.5 mA/V (12500 umho), rp = 2.64 kOhm, mu = 33\n"
            "Vp = 100 V, Vg = -9.0 V -> Ip = 1.0 uA (cutoff reference)",
            "Siemens & Halske E88CC/6922 Data (1970), JJ Electronic E88CC (2024)",
            false
        },

        // ==========================================
        // 2. Driver Triodes (3 types)
        // ==========================================
        {
            "tube_12bh7a",
            "12BH7A Medium-Mu Driver Triode",
            "Driver Triodes",
            "Medium-mu twin triode (vertical deflection amplifier / audio driver)",
            "Glass miniature envelope (T-6 1/2), Noval 9-pin base (B9A)",
            "Sec 1: Pin 1=Plate, Pin 2=Grid, Pin 3=Cathode; Sec 2: Pin 6=Plate, Pin 7=Grid, Pin 8=Cathode; Heater: Pins 4-5 (12.6V) or Pins 4/5-9 (6.3V)",
            "6.3 V @ 600 mA (parallel) or 12.6 V @ 300 mA (series); indirectly heated oxide cathode",
            "Vp(max): 500 V; Pp(max): 3.5 W per section, 5.0 W total; Ik(max): 35 mA; Vhk(max): 200 V",
            "Vp = 250 V, Vg = -10.5 V -> Ip = 11.5 mA, gm = 3.1 mA/V (3100 umho), rp = 5.3 kOhm, mu = 16.5\n"
            "Vp = 85 V, Vg = 0 V -> Ip = 20.0 mA, gm = 4.0 mA/V, rp = 4.1 kOhm, mu = 16.5",
            "RCA Receiving Tube Manual RC-30 (1975), GE 12BH7A Data Sheet",
            false
        },
        {
            "tube_6sn7gt",
            "6SN7GT / ECC32 Dual Triode",
            "Driver Triodes",
            "Medium-mu octal twin triode (phase inverter / audio driver / buffer)",
            "Glass octal envelope (T-9), Octal 8-pin base (8-pin phenolic)",
            "Sec 1: Pin 1=Grid, Pin 2=Plate, Pin 3=Cathode; Sec 2: Pin 4=Grid, Pin 5=Plate, Pin 6=Cathode; Heater: Pins 7-8 (6.3V)",
            "6.3 V @ 600 mA; indirectly heated oxide cathode",
            "Vp(max): 450 V; Pp(max): 3.5 W per section, 5.0 W total; Ik(max): 20 mA; Vhk(max): 200 V",
            "Vp = 250 V, Vg = -8.0 V -> Ip = 9.0 mA, gm = 2.6 mA/V (2600 umho), rp = 7.7 kOhm, mu = 20\n"
            "Vp = 100 V, Vg = 0 V -> Ip = 9.3 mA, gm = 3.0 mA/V, rp = 6.7 kOhm, mu = 20",
            "RCA Receiving Tube Manual RC-30 (1975), Tung-Sol 6SN7GT Data",
            false
        },
        {
            "tube_12dw7",
            "12DW7 / ECC832 Dissimilar Dual Triode",
            "Driver Triodes",
            "Dissimilar twin triode (Sec 1: high-mu 12AX7 unit, Sec 2: medium-mu 12AU7 unit)",
            "Glass miniature envelope (T-6 1/2), Noval 9-pin base (B9A)",
            "Sec 1 (High-mu): Pin 6=Plate, Pin 7=Grid, Pin 8=Cathode; Sec 2 (Med-mu): Pin 1=Plate, Pin 2=Grid, Pin 3=Cathode; Heater: Pins 4-5 (12.6V) or Pins 4/5-9 (6.3V)",
            "6.3 V @ 300 mA (parallel) or 12.6 V @ 150 mA (series); indirectly heated oxide cathode",
            "Sec 1: Vp(max) 330 V, Pp(max) 1.0 W; Sec 2: Vp(max) 330 V, Pp(max) 2.75 W; Vhk(max): 180 V",
            "Sec 1: Vp = 250 V, Vg = -2.0 V -> Ip = 1.2 mA, gm = 1.6 mA/V, rp = 62.5 kOhm, mu = 100\n"
            "Sec 2: Vp = 250 V, Vg = -8.5 V -> Ip = 10.5 mA, gm = 2.2 mA/V, rp = 7.7 kOhm, mu = 17",
            "RCA Receiving Tube Manual RC-30 (1975), JJ Electronic ECC832 Data (2024)",
            false
        },

        // ==========================================
        // 3. Power Triodes (2 types)
        // ==========================================
        {
            "tube_300b",
            "300B Directly-Heated Power Triode",
            "Power Triodes",
            "Directly-heated audio power triode (Single-Ended Triode audio output)",
            "Large glass bulb (ST-19 / dome), 4-pin UX4 ceramic/phenolic base (bayonet pin)",
            "Pin 1=Filament(+), Pin 2=Plate, Pin 3=Grid, Pin 4=Filament(-); Directly heated filament",
            "5.0 V @ 1.2 A (DC or AC); directly heated thoriated-tungsten or oxide filament",
            "Vp(max): 450 V; Pp(max): 40.0 W; Ik(max): 100 mA; Grid circuit resistance: 250 kOhm (fixed bias), 500 kOhm (cathode self-bias)",
            "Vp = 300 V, Vg = -61 V -> Ip = 60 mA, gm = 5.3 mA/V (5300 umho), rp = 700 Ohm, mu = 3.85, RL = 3.0 kOhm, Po = 6.0 W\n"
            "Vp = 350 V, Vg = -74 V -> Ip = 60 mA, gm = 5.5 mA/V, rp = 700 Ohm, mu = 3.85, RL = 4.0 kOhm, Po = 8.0 W\n"
            "Vp = 400 V, Vg = -87 V -> Ip = 60 mA, gm = 5.5 mA/V, rp = 700 Ohm, mu = 3.85, RL = 5.0 kOhm, Po = 11.5 W",
            "Western Electric 300B Characteristic Sheet (1939/1950), Electro-Harmonix 300B Data",
            false
        },
        {
            "tube_2a3",
            "2A3 Directly-Heated Power Triode",
            "Power Triodes",
            "Directly-heated audio power triode (classic SET and push-pull output)",
            "Large glass bulb (ST-16), 4-pin UX4 base",
            "Pin 1=Filament(+), Pin 2=Plate, Pin 3=Grid, Pin 4=Filament(-); Directly heated filament",
            "2.5 V @ 2.5 A (AC or DC); directly heated filament",
            "Vp(max): 300 V; Pp(max): 15.0 W; Ik(max): 70 mA",
            "Vp = 250 V, Vg = -45 V -> Ip = 60 mA, gm = 5.25 mA/V (5250 umho), rp = 800 Ohm, mu = 4.2, RL = 2.5 kOhm, Po = 3.5 W (Single Class A)\n"
            "Push-Pull Class AB1: Vp = 300 V, Vg = -62 V -> Ip(0) = 80 mA, RL(p-p) = 3.0 kOhm, Po = 15.0 W",
            "RCA Receiving Tube Manual RC-30 (1975), Sylvania 2A3 Engineering Data",
            false
        },

        // ==========================================
        // 4. Small-Signal Pentodes (2 types)
        // ==========================================
        {
            "tube_ef86",
            "EF86 / 6267 Audio Pentode",
            "Small-Signal Pentodes",
            "High-gain low-noise audio voltage amplifier pentode (bifilar heater, low hum/microphonics)",
            "Glass miniature envelope (T-6 1/2), Noval 9-pin base (B9A)",
            "Pin 1=Grid 2 (Screen), Pin 2=Grid 1 (Control Grid), Pin 3=Cathode, Pin 4-5=Heater (6.3V), Pin 6=Plate, Pin 7=Internal Shield, Pin 8=Grid 3 (Suppressor), Pin 9=Internal Shield",
            "6.3 V @ 200 mA; indirectly heated oxide cathode",
            "Vp(max): 300 V; Vg2(max): 200 V; Pp(max): 1.0 W; Pg2(max): 0.2 W; Ik(max): 6.0 mA; Vhk(max): 100 V",
            "Vp = 250 V, Vg2 = 140 V, Vg1 = -2.0 V -> Ip = 3.0 mA, Ig2 = 0.6 mA, gm = 2.2 mA/V (2200 umho), rp = 2.5 MOhm, mu(g1-g2) = 38\n"
            "Triode-connected: Vp = 250 V, Vg1 = -2.0 V -> Ip+g2 = 3.6 mA, gm = 2.5 mA/V, rp = 15.0 kOhm, mu = 38",
            "Mullard Technical Handbook Book 2 Part 1 EF86 (1970), Telefunken Laboratory Manual",
            false
        },
        {
            "tube_6au6a",
            "6AU6A / EF94 Miniature Pentode",
            "Small-Signal Pentodes",
            "Sharp-cutoff miniature pentode (RF/IF amplifier, limiter, audio voltage amp)",
            "Glass miniature envelope (T-5 1/2), 7-pin miniature base (B7G)",
            "Pin 1=Grid 1 (Control Grid), Pin 2=Grid 3 (Suppressor) + Internal Shield, Pin 3-4=Heater (6.3V), Pin 5=Plate, Pin 6=Grid 2 (Screen), Pin 7=Cathode",
            "6.3 V @ 300 mA; indirectly heated oxide cathode",
            "Vp(max): 330 V; Vg2(max): 165 V; Pp(max): 3.2 W; Pg2(max): 0.65 W; Ik(max): 12 mA; Vhk(max): 200 V",
            "Vp = 250 V, Vg2 = 150 V, Vg1 = -1.0 V (Rk = 68 Ohm) -> Ip = 10.6 mA, Ig2 = 4.3 mA, gm = 5.2 mA/V (5200 umho), rp = 1.0 MOhm, mu(g1-g2) = 36",
            "RCA Receiving Tube Manual RC-30 (1975), GE 6AU6A Technical Sheet",
            false
        },

        // ==========================================
        // 5. Output Pentodes (2 types)
        // ==========================================
        {
            "tube_el84",
            "EL84 / 6BQ5 Power Pentode",
            "Output Pentodes",
            "High-sensitivity power pentode (9-pin miniature audio output)",
            "Glass miniature envelope (T-6 1/2), Noval 9-pin base (B9A)",
            "Pin 2=Grid 1 (Control Grid), Pin 3=Cathode + Grid 3 (Suppressor), Pin 4-5=Heater (6.3V), Pin 7=Plate, Pin 9=Grid 2 (Screen); Pins 1,6,8=Internal Connection / NC",
            "6.3 V @ 760 mA; indirectly heated oxide cathode",
            "Vp(max): 300 V; Vg2(max): 300 V; Pp(max): 12.0 W; Pg2(max): 2.0 W; Ik(max): 65 mA; Vhk(max): 100 V",
            "Single Class A: Vp = 250 V, Vg2 = 250 V, Vg1 = -7.3 V (Rk = 135 Ohm) -> Ip = 48 mA, Ig2 = 5.5 mA, gm = 11.3 mA/V (11300 umho), rp = 38 kOhm, RL = 5.2 kOhm, Po = 5.7 W (THD 10%)\n"
            "Push-Pull Class AB: Vp = 300 V, Vg2 = 300 V, Rk = 130 Ohm -> Ip(0) = 2x36 mA, Ip(max) = 2x46 mA, RL(p-p) = 8.0 kOhm, Po = 17.0 W",
            "Philips Data Handbook EL84 (1969), Mullard Technical Handbook (1972)",
            false
        },
        {
            "tube_el34",
            "EL34 / 6CA7 Power Pentode",
            "Output Pentodes",
            "High-power audio output pentode (classic British guitar/hifi amplifier tube)",
            "Glass octal envelope (T-11 / tubular), Octal 8-pin base (8-pin metal rim / phenolic)",
            "Pin 1=Grid 3 (Suppressor, brought out separately), Pin 2=Heater (6.3V), Pin 3=Plate, Pin 4=Grid 2 (Screen), Pin 5=Grid 1 (Control Grid), Pin 7=Heater (6.3V), Pin 8=Cathode",
            "6.3 V @ 1.5 A; indirectly heated oxide cathode",
            "Vp(max): 800 V; Vg2(max): 425 V; Pp(max): 25.0 W; Pg2(max): 8.0 W; Ik(max): 150 mA; Vhk(max): 100 V",
            "Single Class A: Vp = 250 V, Vg2 = 250 V, Vg1 = -13.5 V (Rk = 106 Ohm) -> Ip = 100 mA, Ig2 = 15 mA, gm = 11.0 mA/V, rp = 15 kOhm, RL = 2.0 kOhm, Po = 11.0 W\n"
            "Push-Pull Class AB (Fixed Bias): Vp = 450 V, Vg2 = 425 V, Vg1 = -38 V -> Ip(0) = 2x30 mA, Ip(max) = 2x100 mA, RL(p-p) = 3.4 kOhm, Po = 50.0 W",
            "Mullard Technical Handbook Book 2 Part 1 EL34 (1971), JJ Electronic EL34 Data (2024)",
            false
        },

        // ==========================================
        // 6. Beam Power Tubes (4 types)
        // ==========================================
        {
            "tube_6l6gc",
            "6L6GC Beam Power Tube",
            "Beam Power Tubes",
            "Beam power tetrode (classic American push-pull high-fidelity and musical instrument output)",
            "Glass octal envelope (T-12), Octal 8-pin base with metal sleeve or phenolic base",
            "Pin 2=Heater (6.3V), Pin 3=Plate, Pin 4=Grid 2 (Screen), Pin 5=Grid 1 (Control Grid), Pin 7=Heater (6.3V), Pin 8=Cathode + Beam Plates",
            "6.3 V @ 900 mA; indirectly heated oxide cathode",
            "Vp(max): 500 V; Vg2(max): 450 V; Pp(max): 30.0 W; Pg2(max): 5.0 W; Ik(max): 110 mA; Vhk(max): 200 V",
            "Single Class A: Vp = 250 V, Vg2 = 250 V, Vg1 = -14.0 V -> Ip = 72 mA, Ig2 = 5 mA, gm = 6.0 mA/V, rp = 22.5 kOhm, RL = 2.5 kOhm, Po = 6.5 W\n"
            "Push-Pull Class AB1 (Fixed Bias): Vp = 450 V, Vg2 = 400 V, Vg1 = -37 V -> Ip(0) = 2x28 mA, Ip(max) = 2x105 mA, RL(p-p) = 5.6 kOhm, Po = 55.0 W",
            "RCA Receiving Tube Manual RC-30 (1975), GE 6L6GC Technical Data Sheet",
            false
        },
        {
            "tube_6v6gt",
            "6V6GT Beam Power Tube",
            "Beam Power Tubes",
            "Beam power tetrode (compact audio output stage, highly efficient)",
            "Glass octal envelope (T-9), Octal 8-pin base",
            "Pin 2=Heater (6.3V), Pin 3=Plate, Pin 4=Grid 2 (Screen), Pin 5=Grid 1 (Control Grid), Pin 7=Heater (6.3V), Pin 8=Cathode + Beam Plates",
            "6.3 V @ 450 mA; indirectly heated oxide cathode",
            "Vp(max): 350 V; Vg2(max): 315 V; Pp(max): 12.0 W (design-maximum); Pg2(max): 2.0 W; Ik(max): 60 mA; Vhk(max): 200 V",
            "Single Class A: Vp = 250 V, Vg2 = 250 V, Vg1 = -12.5 V -> Ip = 45 mA, Ig2 = 4.5 mA, gm = 4.1 mA/V, rp = 50 kOhm, RL = 5.0 kOhm, Po = 4.5 W\n"
            "Push-Pull Class AB1 (Cathode Bias): Vp = 285 V, Vg2 = 285 V, Rk = 200 Ohm -> Ip(0) = 2x35 mA, RL(p-p) = 8.0 kOhm, Po = 14.0 W",
            "RCA Receiving Tube Manual RC-30 (1975), Tung-Sol 6V6GT Specifications",
            false
        },
        {
            "tube_kt88",
            "KT88 Kinkless Beam Tetrode",
            "Beam Power Tubes",
            "High-power beam tetrode (kinkless tetrode, premium audio high-fidelity output)",
            "Large glass envelope (TT-21 / dome), Octal 8-pin base with metal collar",
            "Pin 2=Heater (6.3V), Pin 3=Plate, Pin 4=Grid 2 (Screen), Pin 5=Grid 1 (Control Grid), Pin 7=Heater (6.3V), Pin 8=Cathode + Beam Plates; Pins 1,6=NC or base shell",
            "6.3 V @ 1.6 A; indirectly heated oxide cathode",
            "Vp(max): 800 V; Vg2(max): 600 V; Pp(max): 42.0 W (design-center); Pg2(max): 8.0 W; Ik(max): 230 mA; Vhk(max): 250 V",
            "Single Class A: Vp = 250 V, Vg2 = 250 V, Vg1 = -15.0 V -> Ip = 140 mA, Ig2 = 3.0 mA, gm = 12.0 mA/V, rp = 12 kOhm, RL = 1.5 kOhm, Po = 12.0 W\n"
            "Push-Pull Class AB1 (Ultra-Linear 43% tap): Vp = 450 V, Vg2 = 450 V, Vg1 = -48 V -> Ip(0) = 2x50 mA, Ip(max) = 2x150 mA, RL(p-p) = 4.0 kOhm, Po = 70.0 W",
            "GEC / M-O Valve Co. KT88 Data Sheet (1974), JJ Electronic KT88 Data (2024)",
            false
        },
        {
            "tube_6550",
            "6550 Beam Power Pentode",
            "Beam Power Tubes",
            "High-power beam tetrode / pentode (heavy-duty bass and high-fidelity output)",
            "Large glass envelope (ST-16), Octal 8-pin base with metal base band",
            "Pin 2=Heater (6.3V), Pin 3=Plate, Pin 4=Grid 2 (Screen), Pin 5=Grid 1 (Control Grid), Pin 7=Heater (6.3V), Pin 8=Cathode + Beam Plates",
            "6.3 V @ 1.6 A; indirectly heated oxide cathode",
            "Vp(max): 600 V (pentode), 400 V (triode-connected); Vg2(max): 400 V; Pp(max): 35.0 W (6550A: 42 W); Pg2(max): 6.0 W; Ik(max): 175 mA; Vhk(max): 200 V",
            "Push-Pull Class AB1 (Fixed Bias): Vp = 450 V, Vg2 = 300 V, Vg1 = -30 V -> Ip(0) = 2x50 mA, Ip(max) = 2x138 mA, RL(p-p) = 3.5 kOhm, Po = 60.0 W\n"
            "Push-Pull Class AB1: Vp = 600 V, Vg2 = 300 V, Vg1 = -32.5 V -> Po = 100.0 W",
            "Tung-Sol 6550 Engineering Data Sheet (1955), GE 6550A Technical Data",
            false
        },

        // ==========================================
        // 7. Rectifier Tubes (2 types)
        // ==========================================
        {
            "tube_5ar4",
            "5AR4 / GZ34 Full-Wave Rectifier",
            "Rectifier Tubes",
            "High-efficiency indirectly-heated full-wave rectifier (slow controlled warm-up, low internal drop)",
            "Glass octal envelope (T-9), Octal 8-pin base",
            "Pin 2=Cathode + Heater (5.0V), Pin 4=Plate 1 (Anode 1), Pin 6=Plate 2 (Anode 2), Pin 8=Heater (5.0V); Pins 1,3,5,7=NC",
            "5.0 V @ 1.9 A; indirectly heated cathode (connected to heater pin 2 inside envelope)",
            "PIV(max): 1500 V (Peak Inverse Voltage); Io(max): 250 mA DC (capacitor input), 300 mA (choke input); Peak plate current: 750 mA; Input capacitor max: 60 uF",
            "AC Plate Voltage = 2 x 300 V RMS -> DC Output Voltage ~330 V @ 250 mA (Choke input filter)\n"
            "AC Plate Voltage = 2 x 450 V RMS -> DC Output Voltage ~480 V @ 250 mA (Capacitor input filter, C=40uF, Rs=150 Ohm)\n"
            "Internal Forward Voltage Drop: ~17 V @ 225 mA per plate",
            "Mullard Technical Handbook GZ34 (1970), Philips Data Sheet 5AR4",
            false
        },
        {
            "tube_5u4gb",
            "5U4GB Full-Wave Rectifier",
            "Rectifier Tubes",
            "Directly-heated full-wave rectifier (heavy current capability, rugged industrial/audio power)",
            "Glass octal envelope (T-12), Octal 8-pin base",
            "Pin 2=Filament, Pin 4=Plate 1 (Anode 1), Pin 6=Plate 2 (Anode 2), Pin 8=Filament; Pins 1,3,5,7=NC",
            "5.0 V @ 3.0 A; directly heated oxide-coated filament",
            "PIV(max): 1550 V (Peak Inverse Voltage); Io(max): 275 mA DC (capacitor input), 300 mA (choke input); Peak plate current: 1.0 A; Input capacitor max: 40 uF",
            "AC Plate Voltage = 2 x 300 V RMS -> DC Output Voltage ~315 V @ 275 mA (Choke input filter)\n"
            "AC Plate Voltage = 2 x 450 V RMS -> DC Output Voltage ~450 V @ 275 mA (Capacitor input filter, C=40uF, Rs=100 Ohm)\n"
            "Internal Forward Voltage Drop: ~44 V @ 225 mA per plate",
            "RCA Receiving Tube Manual RC-30 (1975), GE 5U4GB Engineering Data",
            false
        }
    };
    return catalog;
}
} // namespace

const VacuumTubeSpec* findVacuumTubeSpec(const juce::String& symbolId)
{
    const auto& catalog = getCatalog();
    for (const auto& spec : catalog)
    {
        if (spec.id == symbolId)
            return &spec;
    }
    return nullptr;
}

const std::vector<VacuumTubeSpec>& getAllVacuumTubeSpecs()
{
    return getCatalog();
}

bool isVacuumTube(const juce::String& symbolId)
{
    return findVacuumTubeSpec(symbolId) != nullptr || symbolId == "tube_triode";
}

} // namespace parts
