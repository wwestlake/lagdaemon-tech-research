#include "sim_core/Units.h"

#include <cmath>
#include <cstdlib>
#include <vector>

namespace sim
{
namespace
{
constexpr double pi = 3.14159265358979323846;

struct Atom
{
    const char* symbol;
    std::array<std::int8_t, Dimension::Count> exponent; // M L T I Θ N J Angle
    double scale;
    double offset;
    bool prefixable;
};

// The named units a symbol can be built from.
const std::vector<Atom>& atoms()
{
    static const std::vector<Atom> table {
        { "1",     { 0, 0, 0, 0, 0, 0, 0, 0 }, 1.0, 0.0, false },
        { "%",     { 0, 0, 0, 0, 0, 0, 0, 0 }, 0.01, 0.0, false },
        { "count", { 0, 0, 0, 0, 0, 0, 0, 0 }, 1.0, 0.0, false },
        { "rad",   { 0, 0, 0, 0, 0, 0, 0, 1 }, 1.0, 0.0, true },
        { "deg",   { 0, 0, 0, 0, 0, 0, 0, 1 }, pi / 180.0, 0.0, false },
        { "rev",   { 0, 0, 0, 0, 0, 0, 0, 1 }, 2.0 * pi, 0.0, false },
        { "rpm",   { 0, 0, -1, 0, 0, 0, 0, 1 }, 2.0 * pi / 60.0, 0.0, false },
        { "s",     { 0, 0, 1, 0, 0, 0, 0, 0 }, 1.0, 0.0, true },
        { "min",   { 0, 0, 1, 0, 0, 0, 0, 0 }, 60.0, 0.0, false },
        { "h",     { 0, 0, 1, 0, 0, 0, 0, 0 }, 3600.0, 0.0, false },
        { "Hz",    { 0, 0, -1, 0, 0, 0, 0, 0 }, 1.0, 0.0, true },
        { "m",     { 0, 1, 0, 0, 0, 0, 0, 0 }, 1.0, 0.0, true },
        { "L",     { 0, 3, 0, 0, 0, 0, 0, 0 }, 1e-3, 0.0, true },
        { "g",     { 1, 0, 0, 0, 0, 0, 0, 0 }, 1e-3, 0.0, true },
        { "N",     { 1, 1, -2, 0, 0, 0, 0, 0 }, 1.0, 0.0, true },
        { "J",     { 1, 2, -2, 0, 0, 0, 0, 0 }, 1.0, 0.0, true },
        { "W",     { 1, 2, -3, 0, 0, 0, 0, 0 }, 1.0, 0.0, true },
        { "V",     { 1, 2, -3, -1, 0, 0, 0, 0 }, 1.0, 0.0, true },
        { "A",     { 0, 0, 0, 1, 0, 0, 0, 0 }, 1.0, 0.0, true },
        { "ohm",   { 1, 2, -3, -2, 0, 0, 0, 0 }, 1.0, 0.0, true },
        { "Ohm",   { 1, 2, -3, -2, 0, 0, 0, 0 }, 1.0, 0.0, true },
        { "S",     { -1, -2, 3, 2, 0, 0, 0, 0 }, 1.0, 0.0, true },
        { "F",     { -1, -2, 4, 2, 0, 0, 0, 0 }, 1.0, 0.0, true },
        { "H",     { 1, 2, -2, -2, 0, 0, 0, 0 }, 1.0, 0.0, true },
        { "C",     { 0, 0, 1, 1, 0, 0, 0, 0 }, 1.0, 0.0, true },
        { "Wb",    { 1, 2, -2, -1, 0, 0, 0, 0 }, 1.0, 0.0, true },
        { "Pa",    { 1, -1, -2, 0, 0, 0, 0, 0 }, 1.0, 0.0, true },
        { "bar",   { 1, -1, -2, 0, 0, 0, 0, 0 }, 1e5, 0.0, false },
        { "K",     { 0, 0, 0, 0, 1, 0, 0, 0 }, 1.0, 0.0, false },
        { "degC",  { 0, 0, 0, 0, 1, 0, 0, 0 }, 1.0, 273.15, false },
        { "degF",  { 0, 0, 0, 0, 1, 0, 0, 0 }, 5.0 / 9.0, 273.15 - 32.0 * 5.0 / 9.0, false },
        { "mol",   { 0, 0, 0, 0, 0, 1, 0, 0 }, 1.0, 0.0, true },
        { "cd",    { 0, 0, 0, 0, 0, 0, 1, 0 }, 1.0, 0.0, false },
    };
    return table;
}

struct Prefix { const char* text; double scale; };
const Prefix prefixes[] {
    { "p", 1e-12 }, { "n", 1e-9 }, { "u", 1e-6 }, { "\xC2\xB5", 1e-6 }, { "m", 1e-3 },
    { "c", 1e-2 }, { "k", 1e3 }, { "M", 1e6 }, { "G", 1e9 },
};

const Atom* findAtom(const std::string& text)
{
    for (const auto& a : atoms())
        if (text == a.symbol)
            return &a;
    return nullptr;
}

// One factor such as "mA" or "s": its atom and prefix scale.
bool parseAtom(const std::string& text, const Atom*& atom, double& prefixScale)
{
    prefixScale = 1.0;
    if ((atom = findAtom(text)) != nullptr)
        return true;
    for (const auto& p : prefixes)
    {
        const std::string prefix = p.text;
        if (text.size() > prefix.size() && text.compare(0, prefix.size(), prefix) == 0)
        {
            const auto* base = findAtom(text.substr(prefix.size()));
            if (base != nullptr && base->prefixable)
            {
                atom = base;
                prefixScale = p.scale;
                return true;
            }
        }
    }
    return false;
}
}

bool parseUnit(const std::string& symbol, Unit& out, std::string& error)
{
    Unit unit;
    unit.symbol = symbol.empty() ? std::string("1") : symbol;
    const std::string text = unit.symbol;
    std::size_t i = 0;
    int factors = 0;
    bool affine = false;
    int sign = 1;
    while (i <= text.size())
    {
        // One factor: atom [^ exponent]
        std::size_t start = i;
        while (i < text.size() && text[i] != '*' && text[i] != '/' && text[i] != '^')
            ++i;
        const std::string name = text.substr(start, i - start);
        if (name.empty())
        {
            error = "Unit \"" + text + "\": a factor is missing.";
            return false;
        }
        int exponent = 1;
        if (i < text.size() && text[i] == '^')
        {
            ++i;
            std::size_t e0 = i;
            if (i < text.size() && (text[i] == '-' || text[i] == '+')) ++i;
            while (i < text.size() && text[i] >= '0' && text[i] <= '9') ++i;
            if (i == e0 || (i == e0 + 1 && (text[e0] == '-' || text[e0] == '+')))
            {
                error = "Unit \"" + text + "\": an exponent after ^ is missing.";
                return false;
            }
            exponent = std::atoi(text.substr(e0, i - e0).c_str());
        }
        const Atom* atom = nullptr;
        double prefixScale = 1.0;
        if (!parseAtom(name, atom, prefixScale))
        {
            error = "Unit \"" + text + "\": \"" + name + "\" is not a known unit.";
            return false;
        }
        exponent *= sign;
        if (atom->offset != 0.0)
        {
            if (exponent != 1)
            {
                error = "Unit \"" + text + "\": " + atom->symbol + " has an offset and cannot be raised to a power or divided.";
                return false;
            }
            affine = true;
            unit.offset = atom->offset;
        }
        for (int d = 0; d < Dimension::Count; ++d)
            unit.dimension.exponent[(std::size_t)d] = (std::int8_t)(unit.dimension.exponent[(std::size_t)d] + atom->exponent[(std::size_t)d] * exponent);
        unit.scale *= std::pow(atom->scale * prefixScale, exponent);
        ++factors;
        if (i >= text.size())
            break;
        sign = text[i] == '/' ? -1 : 1;
        ++i;
    }
    if (affine && factors > 1)
    {
        error = "Unit \"" + text + "\": units with an offset (degC, degF) cannot be combined with others.";
        return false;
    }
    out = unit;
    return true;
}

std::string describeDimension(const Dimension& d)
{
    static const char* names[] { "kg", "m", "s", "A", "K", "mol", "cd", "rad" };
    std::string text;
    for (int i = 0; i < Dimension::Count; ++i)
    {
        const int e = d.exponent[(std::size_t)i];
        if (e == 0) continue;
        if (!text.empty()) text += "\xC2\xB7";
        text += names[i];
        if (e != 1) text += "^" + std::to_string(e);
    }
    return text.empty() ? std::string("1") : text;
}

bool conversionBetween(const Unit& from, const Unit& to, Conversion& out)
{
    if (!(from.dimension == to.dimension))
        return false;
    // to = (from * sFrom + oFrom - oTo) / sTo
    out.scale = from.scale / to.scale;
    out.offset = (from.offset - to.offset) / to.scale;
    return true;
}
}
