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
};
inline const std::array<Reference,4>& references()
{
    static const std::array<Reference,4> data{{
        {"4865","motor_pololu_4865","motor-dc-pololu-4865","Pololu 4865: 47:1 25D MP 12V with encoder","MP",true,170,.100,1.8,6.4,2.8,.101,39,2.6,.042,.027},
        {"4885","motor_pololu_4885","motor-dc-pololu-4885","Pololu 4885: 47:1 25D LP 12V with encoder","LP",true,120,.060,.9,4.8,1.4,.101,29,2.4,.029,.018},
        {"4845","motor_pololu_4845","motor-dc-pololu-4845","Pololu 4845: 47:1 25D HP 12V with encoder","HP",true,220,.300,5,15,8.4,.101,48,1.4,.16,.031},
        {"3229","motor_pololu_3229","motor-dc-pololu-3229","Pololu 3229: 47:1 25D MP 12V without encoder","MP",false,170,.080,1.8,6.4,2.8,.088,39,2.6,.042,.027}
    }};return data;
}
inline const Reference* find(const std::string& symbol){for(const auto& r:references())if(symbol==r.symbol)return &r;return nullptr;}
inline bool isMotor(const std::string& symbol){return find(symbol)!=nullptr;}
constexpr double kgCmToNm=.0980665;
// Encoder-count-consistent ratio, calculated from Pololu's 2248.86 output
// counts and 48 motor counts. Not an asserted exact rational tooth ratio.
constexpr double encoderRatio=2248.86/48.0;
}
