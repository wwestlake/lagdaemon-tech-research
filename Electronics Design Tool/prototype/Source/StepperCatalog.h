#pragma once
#include <JuceHeader.h>
#include <array>

// Matches assets/stepper-motor-reference.json and the CAD canonical IDs.
// Zero in this descriptor means unavailable; editors show an empty required
// value, and simulation validation refuses it rather than using zero physics.
namespace stepper_catalog
{
struct MotorReference
{
    const char* symbol;const char* canonical;const char* name;const char* part;
    double resistance,inductance,inertia,fullStep,holding,current,ratio;
};
inline const std::array<MotorReference,3>& motors()
{
    static const std::array<MotorReference,3> values{{
        {"stepper_28byj48_12v","motor-stepper-28byj48-12v-reference","Light stepper: 28BYJ-48 12V reference","28BYJ-48 12V (supplier unconfirmed)",0,0,0,0,0,0,64},
        {"stepper_17hs19_2004s1","motor-stepper-stepperonline-17hs19-2004s1","Medium stepper: STEPPERONLINE 17HS19-2004S1","17HS19-2004S1",1.6,.003,8.2e-6,1.8,.59,2,1},
        {"stepper_23hs30_2804s","motor-stepper-stepperonline-23hs30-2804s","Heavy stepper: STEPPERONLINE 23HS30-2804S","23HS30-2804S",1.17,.00436,3e-5,1.8,2,2.8,1}
    }};return values;
}
inline const MotorReference* find(const juce::String& symbol){for(const auto& r:motors())if(symbol==r.symbol)return &r;return nullptr;}
inline bool isMotor(const juce::String& symbol){return find(symbol)!=nullptr;}
inline bool isDriver(const juce::String& symbol){return symbol=="driver_uln2003"||symbol=="driver_tmc2209"||symbol=="driver_dm542t_v4";}
inline bool isUnipolar(const juce::String& symbol){return symbol=="stepper_28byj48_12v";}
inline juce::String value(double v){return v>0?juce::String(v,12):juce::String();}
}
