#pragma once
#include <array>
#include <string>

namespace pololu25d
{
struct Reference
{
    const char* part;const char* symbol;const char* id;const char* name;const char* power;
    bool encoder;double rpm,noLoadCurrent,stallCurrent,stallKgCm,maxPower,massKg;int curvePage;
    double fitSpeedSlope,fitCurrentIntercept,fitCurrentSlope; // torque in kg*mm
    double supply=12,encRatio=2248.86/48.0; // rated supply [V]; motor-to-output ratio used by the encoder (defaults: 25D family)
    bool micro=false; // 6 V micro/toy gearmotors added from the robotics acquisition; same output-equivalent DC model
};
inline const std::array<Reference,8>& references()
{
    static const std::array<Reference,8> data{{
        {"4865","motor_pololu_4865","motor-dc-pololu-4865","Pololu 4865: 47:1 25D MP 12V with encoder","MP",true,170,.100,1.8,6.4,2.8,.101,39,2.6,.042,.027,12,2248.86/48.0,false},
        {"4885","motor_pololu_4885","motor-dc-pololu-4885","Pololu 4885: 47:1 25D LP 12V with encoder","LP",true,120,.060,.9,4.8,1.4,.101,29,2.4,.029,.018,12,2248.86/48.0,false},
        {"4845","motor_pololu_4845","motor-dc-pololu-4845","Pololu 4845: 47:1 25D HP 12V with encoder","HP",true,220,.300,5,15,8.4,.101,48,1.4,.16,.031,12,2248.86/48.0,false},
        {"3229","motor_pololu_3229","motor-dc-pololu-3229","Pololu 3229: 47:1 25D MP 12V without encoder","MP",false,170,.080,1.8,6.4,2.8,.088,39,2.6,.042,.027,12,2248.86/48.0,false},
        // Micro metal MP 6 V (Rev 6.2 p.22 fit: speed=390-72*T[kg*mm], I=.053+.11*T); encoder is 12 cycles/motor rev, 51.45:1. Mass unpublished (0).
        {"2365","motor_pololu_2365","motor-dc-pololu-2365","Pololu 2365: 50:1 Micro Metal MP 6V","MP",false,420,.070,.67,.54,0,0,22,72,.053,.11,6,51.45,true},
        {"2379","motor_pololu_2379","motor-dc-pololu-2379","Pololu 2379: 50:1 Micro Metal MP 6V, extended shaft","MP",false,420,.070,.67,.54,0,0,22,72,.053,.11,6,51.45,true},
        {"5134","motor_pololu_5134","motor-dc-pololu-5134","Pololu 5134: 50:1 Micro Metal MP 6V with 12 CPR encoder","MP",true,420,.070,.67,.54,0,0,22,72,.053,.11,6,51.45,true},
        // DFRobot FIT0016 TT 1:120: 160 rpm/170 mA @6 V, 2.8 A stall, 0.8 kg*cm max torque (catalog limits, not measured). Fit slopes are endpoint-equivalent, not a plotted fit.
        {"FIT0016","motor_dfrobot_fit0016","motor-dc-dfrobot-fit0016","DFRobot FIT0016: TT 120:1 gearmotor 6V","TT",false,160,.170,2.8,.8,0,0,0,20,.17,.3287,6,120,true}
    }};return data;
}
inline const Reference* find(const std::string& symbol){for(const auto& r:references())if(symbol==r.symbol)return &r;return nullptr;}
inline bool isMotor(const std::string& symbol){return find(symbol)!=nullptr;}
constexpr double kgCmToNm=.0980665;
// Encoder-count-consistent ratio, calculated from Pololu's 2248.86 output
// counts and 48 motor counts. Not an asserted exact rational tooth ratio.
constexpr double encoderRatio=2248.86/48.0;
}
