#pragma once

// Physical units on bus signals (decision F7). A unit is a dimension vector
// plus a scale and offset to SI: value_SI = value * scale + offset.
//
// Dimensions: M, L, T, I, Θ, N, J (SI base) plus plane angle as an eighth
// dimension, so rad/s and rpm convert (rev = 2π rad) while Hz and rad/s do
// not silently mix (a 2π mistake). Incompatible dimensions never connect;
// compatible units with a different scale or offset connect only through an
// explicit conversion. Nothing converts implicitly.

#include <array>
#include <cstdint>
#include <string>

namespace sim
{
struct Dimension
{
    enum Base { Mass, Length, Time, Current, Temperature, Amount, Luminosity, Angle, Count };
    std::array<std::int8_t, Count> exponent {};
    bool operator==(const Dimension&) const = default;
};

struct Unit
{
    std::string symbol = "1";
    Dimension dimension;
    double scale = 1.0;
    double offset = 0.0;   // only for affine units (degC, degF); never in products
};

// Parses "V", "mA", "rad/s", "rpm", "N*m", "kg*m^2", "m/s^2", "degC", "1", "%".
bool parseUnit(const std::string& symbol, Unit& out, std::string& error);

// The dimension in base symbols, e.g. "kg·m^2·s^-3·A^-1" (for error messages).
std::string describeDimension(const Dimension& d);

struct Conversion
{
    double scale = 1.0;
    double offset = 0.0;
    double value(double v) const { return v * scale + offset; }
    double derivative(double d) const { return d * scale; } // rates convert without the offset
    bool identity() const { return scale == 1.0 && offset == 0.0; }
};

// The conversion from `from` to `to`; false when the dimensions differ.
bool conversionBetween(const Unit& from, const Unit& to, Conversion& out);
}
