#include "SchematicSymbols.h"

#include <algorithm>
#include <cmath>

namespace schematic
{
namespace
{
const juce::Colour lineColour { 0xffe8f1f2 };
const juce::Colour instrumentFill { 0xff17212b };
constexpr float stroke = 1.8f;

using P = juce::Point<float>;

void line(juce::Graphics& g, P a, P b, float width = stroke)
{
    g.drawLine(a.x, a.y, b.x, b.y, width);
}

void strokePath(juce::Graphics& g, const juce::Path& path, float width = stroke)
{
    g.strokePath(path, juce::PathStrokeType(width, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
}

// Filled arrow head with its tip at `tip`, pointing away from `from`.
void arrowHead(juce::Graphics& g, P from, P tip, float length = 9.0f, float halfWidth = 4.5f)
{
    const auto dir = tip - from;
    const auto len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
    if (len <= 0.001f) return;
    const P u { dir.x / len, dir.y / len };
    const P n { -u.y, u.x };
    const auto base = tip - u * length;
    juce::Path head;
    head.addTriangle(tip.x, tip.y,
                     base.x + n.x * halfWidth, base.y + n.y * halfWidth,
                     base.x - n.x * halfWidth, base.y - n.y * halfWidth);
    g.fillPath(head);
}

void text(juce::Graphics& g, const juce::String& s, juce::Rectangle<float> area, float size = 12.0f,
          juce::Justification just = juce::Justification::centred)
{
    g.setFont(juce::Font(size, juce::Font::bold));
    g.drawText(s, area.toNearestInt(), just);
}

void sineIn(juce::Graphics& g, juce::Rectangle<float> r, float cycles = 1.0f)
{
    juce::Path wave;
    for (int i = 0; i <= 32; ++i)
    {
        const auto t = (float)i / 32.0f;
        const auto x = r.getX() + t * r.getWidth();
        const auto y = r.getCentreY() - std::sin(t * cycles * juce::MathConstants<float>::twoPi) * r.getHeight() * 0.5f;
        if (i == 0) wave.startNewSubPath(x, y); else wave.lineTo(x, y);
    }
    strokePath(g, wave, 1.6f);
}

void zigzag(juce::Graphics& g, float x0, float x1, float amplitude)
{
    juce::Path z;
    z.startNewSubPath(x0, 0.0f);
    const int peaks = 6;
    for (int i = 0; i < peaks; ++i)
    {
        const auto x = x0 + ((float)i + 0.5f) * (x1 - x0) / (float)peaks;
        z.lineTo(x, i % 2 == 0 ? -amplitude : amplitude);
    }
    z.lineTo(x1, 0.0f);
    strokePath(g, z);
}

void coilHorizontal(juce::Graphics& g, float x0, float x1, float y, float bump, int turns = 4)
{
    juce::Path coil;
    coil.startNewSubPath(x0, y);
    const auto w = (x1 - x0) / (float)turns;
    for (int i = 0; i < turns; ++i)
    {
        const auto x = x0 + (float)i * w;
        coil.cubicTo(x + w * 0.1f, y + bump, x + w * 0.9f, y + bump, x + w, y);
    }
    strokePath(g, coil);
}

void coilVertical(juce::Graphics& g, float x, float y0, float y1, float bump, int turns = 4)
{
    juce::Path coil;
    coil.startNewSubPath(x, y0);
    const auto h = (y1 - y0) / (float)turns;
    for (int i = 0; i < turns; ++i)
    {
        const auto y = y0 + (float)i * h;
        coil.cubicTo(x + bump, y + h * 0.1f, x + bump, y + h * 0.9f, x, y + h);
    }
    strokePath(g, coil);
}

// Horizontal two-terminal diode body between x = -12 and x = 12, anode left.
void diodeBody(juce::Graphics& g, const juce::String& id)
{
    line(g, { -48, 0 }, { -12, 0 });
    line(g, { 12, 0 }, { 48, 0 });
    juce::Path tri;
    tri.addTriangle(-12.0f, -12.0f, -12.0f, 12.0f, 12.0f, 0.0f);
    strokePath(g, tri);
    line(g, { 12, -12 }, { 12, 12 });

    if (id == "zener_diode")
    {
        line(g, { 12, -12 }, { 6, -16 });
        line(g, { 12, 12 }, { 18, 16 });
    }
    else if (id == "schottky_diode")
    {
        juce::Path s;
        s.startNewSubPath(6.0f, -8.0f);
        s.lineTo(6.0f, -12.0f);
        s.lineTo(12.0f, -12.0f);
        s.startNewSubPath(12.0f, 12.0f);
        s.lineTo(18.0f, 12.0f);
        s.lineTo(18.0f, 8.0f);
        strokePath(g, s);
    }
    else if (id == "led")
    {
        for (float dx : { -2.0f, 6.0f })
        {
            const P from { dx, -14.0f };
            const P to { dx + 9.0f, -23.0f };
            line(g, from, to, 1.4f);
            arrowHead(g, from, to, 6.0f, 3.0f);
        }
    }
}

// Bipolar transistor, base left. emitterDown: NPN style (C top, E bottom).
void bjtBody(juce::Graphics& g, bool npn)
{
    g.drawEllipse(-22.0f, -28.0f, 56.0f, 56.0f, 1.4f);
    line(g, { -48, 0 }, { -10, 0 });
    line(g, { -10, -16 }, { -10, 16 }, 2.4f);
    // NPN: collector top, emitter bottom. PNP: emitter top, collector bottom.
    const P upper { 24, -26 };
    const P lower { 24, 26 };
    line(g, { -10, -7 }, upper);
    line(g, { -10, 7 }, lower);
    line(g, upper, { 24, -48 });
    line(g, lower, { 24, 48 });
    if (npn)
        arrowHead(g, { -10, 7 }, { 21, 24 });      // out of the base, on the emitter
    else
        arrowHead(g, upper, { -7, -9 });           // into the base, on the emitter
}

// MOSFET, gate left. sourceDown: NMOS (D top, S bottom); PMOS has S top.
void mosBody(juce::Graphics& g, bool nChannel)
{
    line(g, { -48, 0 }, { -14, 0 });
    line(g, { -14, -16 }, { -14, 16 });
    for (float y : { -16.0f, -3.0f, 10.0f })
        line(g, { -6, y }, { -6, y + 6 }, 2.4f);
    const auto top = -13.0f;
    const auto bottom = 13.0f;
    line(g, { -6, top }, { 24, top });
    line(g, { -6, bottom }, { 24, bottom });
    line(g, { 24, top }, { 24, -48 });
    line(g, { 24, bottom }, { 24, 48 });
    // Body connection to the source terminal.
    const auto sourceY = nChannel ? bottom : top;
    line(g, { -6, 0 }, { 24, 0 });
    line(g, { 24, 0 }, { 24, sourceY });
    if (nChannel)
        arrowHead(g, { 16, 0 }, { -4, 0 });
    else
        arrowHead(g, { -4, 0 }, { 16, 0 });
}

void jfetBody(juce::Graphics& g, bool nChannel)
{
    line(g, { -6, -18 }, { -6, 18 }, 2.4f);
    line(g, { -48, 0 }, { -6, 0 });
    if (nChannel)
        arrowHead(g, { -30, 0 }, { -7, 0 });
    else
        arrowHead(g, { -8, 0 }, { -30, 0 });
    line(g, { -6, -12 }, { 24, -12 });
    line(g, { -6, 12 }, { 24, 12 });
    line(g, { 24, -12 }, { 24, -48 });
    line(g, { 24, 12 }, { 24, 48 });
}

void circleSource(juce::Graphics& g)
{
    line(g, { 0, -48 }, { 0, -24 });
    line(g, { 0, 24 }, { 0, 48 });
    g.drawEllipse(-24.0f, -24.0f, 48.0f, 48.0f, stroke);
}

void diamondSource(juce::Graphics& g, bool currentOutput, const juce::String& posName, const juce::String& negName)
{
    juce::Path d;
    d.startNewSubPath(0.0f, -24.0f);
    d.lineTo(24.0f, 0.0f);
    d.lineTo(0.0f, 24.0f);
    d.lineTo(-24.0f, 0.0f);
    d.closeSubPath();
    strokePath(g, d);
    line(g, { 0, -48 }, { 0, -24 });
    line(g, { 0, 24 }, { 0, 48 });
    if (currentOutput)
    {
        line(g, { 0, -12 }, { 0, 12 });
        arrowHead(g, { 0, -12 }, { 0, 14 });
    }
    else
    {
        text(g, "+", { -8, -20, 16, 14 });
        text(g, "-", { -8, 4, 16, 14 });
    }
    // Controlling terminals on the left.
    line(g, { -48, -24 }, { -32, -24 });
    line(g, { -48, 24 }, { -32, 24 });
    g.drawEllipse(-32.0f, -27.0f, 6.0f, 6.0f, 1.4f);
    g.drawEllipse(-32.0f, 21.0f, 6.0f, 6.0f, 1.4f);
    juce::ignoreUnused(posName, negName);
}

void contact(juce::Graphics& g, P c)
{
    g.drawEllipse(c.x - 3.0f, c.y - 3.0f, 6.0f, 6.0f, 1.4f);
}

void gateInputs(juce::Graphics& g, float bodyLeft)
{
    line(g, { -48, -24 }, { bodyLeft, -24 });
    line(g, { -48, 24 }, { bodyLeft, 24 });
}

void andShape(juce::Graphics& g)
{
    juce::Path p;
    p.startNewSubPath(-30.0f, -30.0f);
    p.lineTo(0.0f, -30.0f);
    p.addCentredArc(0.0f, 0.0f, 30.0f, 30.0f, 0.0f, 0.0f, juce::MathConstants<float>::pi, false);
    p.lineTo(-30.0f, 30.0f);
    p.closeSubPath();
    strokePath(g, p);
}

void orShape(juce::Graphics& g, bool exclusive)
{
    juce::Path p;
    p.startNewSubPath(-30.0f, -30.0f);
    p.quadraticTo(10.0f, -30.0f, 30.0f, 0.0f);
    p.quadraticTo(10.0f, 30.0f, -30.0f, 30.0f);
    p.quadraticTo(-14.0f, 0.0f, -30.0f, -30.0f);
    strokePath(g, p);
    if (exclusive)
    {
        juce::Path x;
        x.startNewSubPath(-38.0f, -30.0f);
        x.quadraticTo(-22.0f, 0.0f, -38.0f, 30.0f);
        strokePath(g, x);
    }
}

void bubble(juce::Graphics& g, float x)
{
    g.drawEllipse(x, -5.0f, 10.0f, 10.0f, stroke);
}

SymbolDef make(const juce::String& id, const juce::String& title, juce::Rectangle<float> bounds,
               std::vector<PinDef> pins, bool showPinNames = false)
{
    SymbolDef def;
    def.id = id;
    def.title = title;
    def.bounds = bounds;
    def.pins = std::move(pins);
    def.showPinNames = showPinNames;
    return def;
}
}

const juce::StringArray& supportedSymbolIds()
{
    static const juce::StringArray ids {
        "resistor", "potentiometer", "capacitor", "capacitor_polarized", "variable_capacitor",
        "inductor", "coupled_inductor", "transformer", "diode", "zener_diode", "led",
        "schottky_diode", "power_bus", "ground_bus", "power_port", "net_label", "sub_block", "block_port", "battery", "voltage_source", "audio_in", "audio_out",
        "ac_voltage_source", "current_source", "ac_current_source", "vcvs", "vccs",
        "ccvs", "cccs", "signal_source", "ground", "opamp_741", "npn", "pnp",
        "nmos", "pmos", "njfet", "pjfet", "switch_spst", "switch_spdt", "relay_spst",
        "fuse", "connector_2", "connector_3", "test_point", "logic_not", "logic_and",
        "logic_or", "logic_nand", "logic_nor", "logic_xor", "oscilloscope_2ch",
        "digital_multimeter", "bode_analyzer", "annotation_text"
    };
    return ids;
}

bool isSupportedSymbol(const juce::String& symbolId)
{
    return supportedSymbolIds().contains(symbolId);
}

SymbolDef symbolFor(const juce::String& id)
{
    if (id == "resistor")            return make(id, "R", { -30, -12, 60, 24 }, { { "1", { -48, 0 } }, { "2", { 48, 0 } } });
    if (id == "potentiometer")       return make(id, "POT", { -30, -24, 60, 36 }, { { "1", { -48, 0 } }, { "2", { 48, 0 } }, { "W", { 0, -48 } } });
    if (id == "capacitor")           return make(id, "C", { -12, -24, 24, 48 }, { { "1", { -48, 0 } }, { "2", { 48, 0 } } });
    if (id == "capacitor_polarized") return make(id, "C+", { -12, -24, 24, 48 }, { { "+", { -48, 0 } }, { "-", { 48, 0 } } });
    if (id == "variable_capacitor")  return make(id, "CV", { -18, -24, 36, 48 }, { { "1", { -48, 0 } }, { "2", { 48, 0 } } });
    if (id == "inductor")            return make(id, "L", { -36, -12, 72, 12 }, { { "1", { -48, 0 } }, { "2", { 48, 0 } } });
    if (id == "coupled_inductor")    return make(id, "Lx2", { -36, -36, 72, 72 }, { { "1A", { -48, -24 } }, { "1B", { 48, -24 } }, { "2A", { -48, 24 } }, { "2B", { 48, 24 } } }, true);
    if (id == "transformer")         return make(id, "XFMR", { -36, -36, 72, 72 }, { { "P1", { -72, -24 } }, { "P2", { -72, 24 } }, { "S1", { 72, -24 } }, { "S2", { 72, 24 } } }, true);
    if (id == "diode" || id == "zener_diode" || id == "schottky_diode")
                                     return make(id, id == "diode" ? "D" : id == "zener_diode" ? "ZD" : "SD", { -18, -18, 36, 36 }, { { "A", { -48, 0 } }, { "K", { 48, 0 } } });
    if (id == "led")                 return make(id, "LED", { -18, -24, 36, 42 }, { { "A", { -48, 0 } }, { "K", { 48, 0 } } });
    if (id == "power_bus")           return make(id, "PWR", { -210, -8, 420, 16 }, { { "VBUS", { 0, 0 } } });
    if (id == "ground_bus")          return make(id, "GND BUS", { -210, -8, 420, 16 }, { { "0", { 0, 0 } } });
    if (id == "power_port")          return make(id, "PWR", { -18, -24, 36, 24 }, { { "1", { 0, 0 } } });
    if (id == "net_label")           return make(id, "LABEL", { -12, -24, 24, 24 }, { { "1", { 0, 0 } } });
    // Port bubble inside a sub-diagram; the pin is the bubble's right tip.
    // Rotation 180 puts the bubble on the right (output ports).
    if (id == "block_port")          return make(id, "PORT", { -120, -12, 120, 24 }, { { "1", { 0, 0 } } });
    // Pins depend on the block's ports; see blockSymbol().
    if (id == "sub_block")           return make(id, "BLOCK", { -72, -48, 144, 72 }, {});
    if (id == "ground")              return make(id, "GND", { -18, 0, 36, 30 }, { { "0", { 0, 0 } } });
    if (id == "battery")             return make(id, "BAT", { -24, -18, 48, 36 }, { { "+", { 0, -48 } }, { "-", { 0, 48 } } });
    if (id == "audio_in")          return make(id, "IN", { -24, -24, 48, 48 }, { { "+", { 0, -48 } }, { "-", { 0, 48 } } });
    if (id == "audio_out")         return make(id, "OUT", { -24, -24, 48, 48 }, { { "1", { 0, 0 } } });
    if (id == "voltage_source")      return make(id, "V", { -24, -24, 48, 48 }, { { "+", { 0, -48 } }, { "-", { 0, 48 } } });
    if (id == "ac_voltage_source")   return make(id, "AC", { -24, -24, 48, 48 }, { { "+", { 0, -48 } }, { "-", { 0, 48 } } });
    if (id == "current_source")      return make(id, "I", { -24, -24, 48, 48 }, { { "+", { 0, -48 } }, { "-", { 0, 48 } } });
    if (id == "ac_current_source")   return make(id, "IAC", { -24, -24, 48, 48 }, { { "+", { 0, -48 } }, { "-", { 0, 48 } } });
    if (id == "vcvs")                return make(id, "E", { -36, -30, 60, 60 }, { { "+", { 0, -48 } }, { "-", { 0, 48 } }, { "CP+", { -48, -24 } }, { "CP-", { -48, 24 } } }, true);
    if (id == "vccs")                return make(id, "G", { -36, -30, 60, 60 }, { { "+", { 0, -48 } }, { "-", { 0, 48 } }, { "CP+", { -48, -24 } }, { "CP-", { -48, 24 } } }, true);
    if (id == "ccvs")                return make(id, "H", { -36, -30, 60, 60 }, { { "+", { 0, -48 } }, { "-", { 0, 48 } }, { "S+", { -48, -24 } }, { "S-", { -48, 24 } } }, true);
    if (id == "cccs")                return make(id, "F", { -36, -30, 60, 60 }, { { "+", { 0, -48 } }, { "-", { 0, 48 } }, { "S+", { -48, -24 } }, { "S-", { -48, 24 } } }, true);
    if (id == "signal_source")       return make(id, "SIG", { -24, -24, 48, 48 }, { { "OUT", { 48, 0 } }, { "REF", { -48, 0 } } }, true);
    if (id == "opamp_741")           return make(id, "uA741", { -48, -48, 96, 96 }, { { "IN+", { -72, -24 } }, { "IN-", { -72, 24 } }, { "OUT", { 72, 0 } }, { "V+", { 0, -72 } }, { "V-", { 0, 72 } } });
    if (id == "npn")                 return make(id, "NPN", { -24, -30, 60, 60 }, { { "B", { -48, 0 } }, { "C", { 24, -48 } }, { "E", { 24, 48 } } });
    if (id == "pnp")                 return make(id, "PNP", { -24, -30, 60, 60 }, { { "B", { -48, 0 } }, { "E", { 24, -48 } }, { "C", { 24, 48 } } });
    if (id == "nmos")                return make(id, "NMOS", { -24, -24, 54, 48 }, { { "G", { -48, 0 } }, { "D", { 24, -48 } }, { "S", { 24, 48 } } });
    if (id == "pmos")                return make(id, "PMOS", { -24, -24, 54, 48 }, { { "G", { -48, 0 } }, { "S", { 24, -48 } }, { "D", { 24, 48 } } });
    if (id == "njfet")               return make(id, "NJFET", { -24, -24, 54, 48 }, { { "G", { -48, 0 } }, { "D", { 24, -48 } }, { "S", { 24, 48 } } });
    if (id == "pjfet")               return make(id, "PJFET", { -24, -24, 54, 48 }, { { "G", { -48, 0 } }, { "S", { 24, -48 } }, { "D", { 24, 48 } } });
    if (id == "switch_spst")         return make(id, "SW", { -30, -24, 60, 30 }, { { "1", { -48, 0 } }, { "2", { 48, 0 } } });
    if (id == "switch_spdt")         return make(id, "SWDT", { -30, -30, 60, 60 }, { { "C", { -48, 0 } }, { "A", { 48, -24 } }, { "B", { 48, 24 } } }, true);
    if (id == "relay_spst")          return make(id, "K", { -48, -36, 96, 72 }, { { "COIL+", { -72, -24 } }, { "COIL-", { -72, 24 } }, { "1", { 72, -24 } }, { "2", { 72, 24 } } }, true);
    if (id == "fuse")                return make(id, "FUSE", { -30, -12, 60, 24 }, { { "1", { -48, 0 } }, { "2", { 48, 0 } } });
    if (id == "connector_2")         return make(id, "J2", { -24, -18, 36, 60 }, { { "1", { -48, 0 } }, { "2", { -48, 24 } } }, true);
    if (id == "connector_3")         return make(id, "J3", { -24, -42, 36, 84 }, { { "1", { -48, -24 } }, { "2", { -48, 0 } }, { "3", { -48, 24 } } }, true);
    if (id == "test_point")          return make(id, "TP", { -12, -12, 24, 24 }, { { "1", { -24, 0 } } });
    if (id == "logic_not")           return make(id, "NOT", { -30, -24, 60, 48 }, { { "A", { -48, 0 } }, { "Y", { 48, 0 } } });
    if (id == "logic_and" || id == "logic_or" || id == "logic_nand" || id == "logic_nor" || id == "logic_xor")
    {
        const auto title = id.fromFirstOccurrenceOf("logic_", false, false).toUpperCase();
        return make(id, title, { -36, -30, 76, 60 }, { { "A", { -48, -24 } }, { "B", { -48, 24 } }, { "Y", { 48, 0 } } });
    }
    if (id == "oscilloscope_2ch")    return make(id, "SCOPE", { -60, -48, 120, 96 }, { { "CH1", { -72, -24 } }, { "CH2", { -72, 24 } }, { "REF", { 0, 72 } } }, true);
    if (id == "bode_analyzer")       return make(id, "BODE", { -60, -48, 120, 96 }, { { "IN", { -72, -24 } }, { "OUT", { -72, 24 } }, { "REF", { 0, 72 } } }, true);
    if (id == "digital_multimeter")  return make(id, "DMM", { -48, -36, 96, 72 }, { { "HI", { -72, -24 } }, { "LO", { -72, 24 } } }, true);
    if (id == "annotation_text")     return make(id, "NOTE", { -96, -42, 192, 84 }, {});
    return {};
}

juce::String refdesPrefixFor(const juce::String& id)
{
    if (id == "resistor" || id == "potentiometer") return "R";
    if (id.startsWith("capacitor") || id == "variable_capacitor") return "C";
    if (id == "inductor" || id == "coupled_inductor") return "L";
    if (id == "transformer") return "T";
    if (id == "diode" || id == "zener_diode" || id == "schottky_diode" || id == "led") return "D";
    if (id == "battery") return "BT";
    if (id == "audio_in" || id == "audio_out") return "AUDIO";
    if (id == "voltage_source" || id == "ac_voltage_source" || id == "signal_source") return "V";
    if (id == "current_source" || id == "ac_current_source") return "I";
    if (id == "vcvs") return "E";
    if (id == "vccs") return "G";
    if (id == "ccvs") return "H";
    if (id == "cccs") return "F";
    if (id == "opamp_741" || id.startsWith("logic_")) return "U";
    if (id == "npn" || id == "pnp" || id == "nmos" || id == "pmos" || id == "njfet" || id == "pjfet") return "Q";
    if (id.startsWith("switch_")) return "SW";
    if (id.startsWith("relay_")) return "K";
    if (id == "fuse") return "F";
    if (id.startsWith("connector_")) return "J";
    if (id == "test_point") return "TP";
    if (id == "ground") return "GND";
    if (id == "power_port") return "PWR";
    if (id == "net_label") return "LBL";
    if (id == "sub_block") return "A";      // IEEE 315: assembly / subassembly
    if (id == "block_port") return "PORT";
    if (id == "ground_bus") return "GBUS";
    if (id == "power_bus") return "PBUS";
    if (id == "oscilloscope_2ch") return "SCOPE";
    if (id == "digital_multimeter") return "DMM";
    if (id == "bode_analyzer") return "FRA";
    if (id == "annotation_text") return "NOTE";
    return "U";
}

bool isPowerSymbol(const juce::String& id)
{
    return id == "ground" || id == "power_port" || id == "net_label";
}

bool isInstrumentSymbol(const juce::String& id)
{
    return id == "oscilloscope_2ch" || id == "digital_multimeter" || id == "bode_analyzer";
}

bool isRailBus(const juce::String& id)
{
    return id == "power_bus" || id == "ground_bus";
}

int normalizedRotation(int rotation)
{
    const auto r = ((rotation % 360) + 360) % 360;
    return (int)(std::round((float)r / 90.0f) * 90.0f) % 360;
}

juce::Point<float> rotateOffset(juce::Point<float> offset, int rotation)
{
    switch (normalizedRotation(rotation))
    {
        case 90:  return { -offset.y, offset.x };
        case 180: return { -offset.x, -offset.y };
        case 270: return { offset.y, -offset.x };
        default:  return offset;
    }
}

juce::Rectangle<float> rotateBounds(juce::Rectangle<float> bounds, int rotation)
{
    const auto a = rotateOffset(bounds.getTopLeft(), rotation);
    const auto b = rotateOffset(bounds.getBottomRight(), rotation);
    return juce::Rectangle<float>(a, b);
}

juce::Rectangle<float> extentBounds(const SymbolDef& symbol)
{
    // Not Rectangle::getUnion: it ignores zero-size rectangles (points).
    auto left = symbol.bounds.getX(), top = symbol.bounds.getY();
    auto right = symbol.bounds.getRight(), bottom = symbol.bounds.getBottom();
    for (const auto& pin : symbol.pins)
    {
        left = std::min(left, pin.offset.x);
        right = std::max(right, pin.offset.x);
        top = std::min(top, pin.offset.y);
        bottom = std::max(bottom, pin.offset.y);
    }
    return { left, top, right - left, bottom - top };
}

juce::Point<float> pinLeadDirection(const SymbolDef& symbol, int pinIndex)
{
    if (pinIndex < 0 || pinIndex >= (int)symbol.pins.size())
        return {};

    // Net markers (ground, supply ports, labels) take a wire from any side.
    if (isPowerSymbol(symbol.id))
        return {};

    const auto p = symbol.pins[(size_t)pinIndex].offset;
    const auto body = symbol.bounds;

    // Pick the side the pin sticks out of furthest beyond the body.
    const float beyond[] = { body.getX() - p.x, p.x - body.getRight(), body.getY() - p.y, p.y - body.getBottom() };
    const juce::Point<float> dirs[] = { { -1.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, -1.0f }, { 0.0f, 1.0f } };
    int best = 0;
    for (int i = 1; i < 4; ++i)
        if (beyond[i] > beyond[best])
            best = i;
    if (beyond[best] > 0.0f)
        return dirs[best];

    // Pin on or inside the body edge: use the nearest extent edge.
    const auto extent = extentBounds(symbol);
    const float distance[] = { p.x - extent.getX(), extent.getRight() - p.x, p.y - extent.getY(), extent.getBottom() - p.y };
    best = 0;
    for (int i = 1; i < 4; ++i)
        if (distance[i] < distance[best])
            best = i;
    return dirs[best];
}

juce::Rectangle<float> portBubbleRect()
{
    return { -118.0f, -11.0f, 108.0f, 22.0f };
}

SymbolDef blockSymbol(const std::vector<BlockPort>& ports)
{
    std::vector<const BlockPort*> left, right;
    for (const auto& port : ports)
        (port.rightSide ? right : left).push_back(&port);
    const auto rows = std::max<size_t>({ left.size(), right.size(), (size_t)1 });
    const auto half = (float)rows * 24.0f;

    std::vector<PinDef> pins;
    // Pins in port order so pin index == port index.
    for (const auto& port : ports)
    {
        const auto& side = port.rightSide ? right : left;
        const auto n = (int)side.size();
        const auto k = (int)(std::find(side.begin(), side.end(), &port) - side.begin());
        pins.push_back({ port.name, { port.rightSide ? 96.0f : -96.0f, (float)(2 * k - (n - 1)) * 24.0f } });
    }

    SymbolDef def;
    def.id = "sub_block";
    def.title = "BLOCK";
    def.bounds = { -72.0f, -half - 24.0f, 144.0f, half * 2.0f + 24.0f };
    def.pins = std::move(pins);
    def.showPinNames = false; // drawBlockArt writes them inside the box
    return def;
}

void drawBlockArt(juce::Graphics& g, const SymbolDef& block, const juce::String& name)
{
    const auto body = block.bounds;
    g.setColour(juce::Colour(0xff17212b));
    g.fillRoundedRectangle(body, 6.0f);
    g.setColour(juce::Colour(0xff78dcca));
    g.drawRoundedRectangle(body, 6.0f, 2.0f);
    const auto band = body.withHeight(24.0f);
    g.setColour(juce::Colour(0xff78dcca).withAlpha(0.18f));
    g.fillRect(band.reduced(2.0f, 2.0f));
    g.setColour(juce::Colour(0xffe8f1f2));
    g.setFont(juce::Font(13.0f, juce::Font::bold));
    g.drawText(name, band.reduced(8.0f, 0.0f).toNearestInt(), juce::Justification::centred, true);

    g.setFont(juce::Font(11.0f));
    for (const auto& pin : block.pins)
    {
        const auto onRight = pin.offset.x > 0.0f;
        const auto edge = onRight ? body.getRight() : body.getX();
        g.setColour(lineColour);
        line(g, pin.offset, { edge, pin.offset.y });
        g.setColour(juce::Colour(0xff93a7b0));
        const auto textArea = juce::Rectangle<float>(onRight ? edge - 66.0f : edge + 6.0f, pin.offset.y - 8.0f, 60.0f, 16.0f);
        g.drawText(pin.name, textArea.toNearestInt(), onRight ? juce::Justification::centredRight : juce::Justification::centredLeft, true);
    }
}

LabelRects labelRectsFor(const SymbolDef& symbol, int rotation)
{
    // Ports and blocks carry their names inside their own art.
    if (symbol.id == "block_port" || symbol.id == "sub_block")
        return {};

    constexpr float w = 96.0f, h = 15.0f, gap = 4.0f;
    const auto body = rotateBounds(symbol.bounds, rotation);
    bool up = false, down = false, left = false, right = false;
    for (int p = 0; p < (int)symbol.pins.size(); ++p)
    {
        const auto d = rotateOffset(pinLeadDirection(symbol, p), rotation);
        up |= d.y < -0.5f;
        down |= d.y > 0.5f;
        left |= d.x < -0.5f;
        right |= d.x > 0.5f;
    }

    LabelRects r;
    if (!up && !down)
    {
        r.refdes = { body.getCentreX() - w * 0.5f, body.getY() - gap - h, w, h };
        r.value = { body.getCentreX() - w * 0.5f, body.getBottom() + gap, w, h };
        r.justification = juce::Justification::centred;
    }
    else if (!right)
    {
        r.refdes = { body.getRight() + gap * 2.0f, body.getCentreY() - h - 1.0f, w, h };
        r.value = { body.getRight() + gap * 2.0f, body.getCentreY() + 1.0f, w, h };
        r.justification = juce::Justification::centredLeft;
    }
    else if (!left)
    {
        r.refdes = { body.getX() - gap * 2.0f - w, body.getCentreY() - h - 1.0f, w, h };
        r.value = { body.getX() - gap * 2.0f - w, body.getCentreY() + 1.0f, w, h };
        r.justification = juce::Justification::centredRight;
    }
    else
    {
        // Pins on every side (op amp): the corner right of the top pin.
        r.refdes = { body.getCentreX() + gap * 2.0f, body.getY() - h, w, h };
        r.value = { body.getCentreX() + gap * 2.0f, body.getY() + 1.0f, w, h };
        r.justification = juce::Justification::centredLeft;
    }
    return r;
}

void drawSymbolArt(juce::Graphics& g, const SymbolDef& symbol, const juce::String& readout)
{
    const auto& id = symbol.id;
    g.setColour(lineColour);

    if (id == "annotation_text")
    {
        const juce::Rectangle<float> body { -96.0f, -42.0f, 192.0f, 84.0f };
        g.setColour(juce::Colour(0xff17222b));
        g.fillRoundedRectangle(body, 4.0f);
        g.setColour(juce::Colour(0xff78dcca));
        g.drawRoundedRectangle(body, 4.0f, 1.6f);
        g.setColour(juce::Colour(0xffdce9ee));
        g.setFont(juce::Font(12.0f));
        g.drawFittedText(readout.isNotEmpty() ? readout : juce::String("Note"),
                         body.reduced(8.0f).toNearestInt(),
                         juce::Justification::centredLeft,
                         4);
    }
    else if (id == "resistor")
    {
        line(g, { -48, 0 }, { -30, 0 });
        line(g, { 30, 0 }, { 48, 0 });
        zigzag(g, -30.0f, 30.0f, 9.0f);
    }
    else if (id == "potentiometer")
    {
        line(g, { -48, 0 }, { -30, 0 });
        line(g, { 30, 0 }, { 48, 0 });
        zigzag(g, -30.0f, 30.0f, 9.0f);
        line(g, { 0, -48 }, { 0, -12 });
        arrowHead(g, { 0, -24 }, { 0, -10 });
    }
    else if (id == "capacitor" || id == "capacitor_polarized" || id == "variable_capacitor")
    {
        line(g, { -48, 0 }, { -5, 0 });
        line(g, { 5, 0 }, { 48, 0 });
        line(g, { -5, -18 }, { -5, 18 }, 2.4f);
        if (id == "capacitor_polarized")
        {
            juce::Path curved;
            curved.startNewSubPath(9.0f, -18.0f);
            curved.quadraticTo(3.0f, 0.0f, 9.0f, 18.0f);
            strokePath(g, curved, 2.4f);
            text(g, "+", { -22, -26, 12, 12 });
        }
        else
        {
            line(g, { 5, -18 }, { 5, 18 }, 2.4f);
        }
        if (id == "variable_capacitor")
        {
            line(g, { -16, 16 }, { 14, -16 }, 1.4f);
            arrowHead(g, { -16, 16 }, { 16, -18 }, 7.0f, 3.5f);
        }
    }
    else if (id == "inductor")
    {
        line(g, { -48, 0 }, { -36, 0 });
        line(g, { 36, 0 }, { 48, 0 });
        coilHorizontal(g, -36.0f, 36.0f, 0.0f, -16.0f);
    }
    else if (id == "coupled_inductor")
    {
        line(g, { -48, -24 }, { -36, -24 });
        line(g, { 36, -24 }, { 48, -24 });
        line(g, { -48, 24 }, { -36, 24 });
        line(g, { 36, 24 }, { 48, 24 });
        coilHorizontal(g, -36.0f, 36.0f, -12.0f, -14.0f);
        coilHorizontal(g, -36.0f, 36.0f, 12.0f, 14.0f);
        line(g, { -36, -24 }, { -36, -12 });
        line(g, { 36, -24 }, { 36, -12 });
        line(g, { -36, 24 }, { -36, 12 });
        line(g, { 36, 24 }, { 36, 12 });
        line(g, { -32, -3 }, { 32, -3 }, 1.4f);
        line(g, { -32, 3 }, { 32, 3 }, 1.4f);
    }
    else if (id == "transformer")
    {
        line(g, { -72, -24 }, { -18, -24 });
        line(g, { -72, 24 }, { -18, 24 });
        line(g, { 18, -24 }, { 72, -24 });
        line(g, { 18, 24 }, { 72, 24 });
        coilVertical(g, -18.0f, -24.0f, 24.0f, 14.0f);
        coilVertical(g, 18.0f, -24.0f, 24.0f, -14.0f);
        line(g, { -3, -28 }, { -3, 28 }, 1.4f);
        line(g, { 3, -28 }, { 3, 28 }, 1.4f);
    }
    else if (id == "diode" || id == "zener_diode" || id == "schottky_diode" || id == "led")
    {
        diodeBody(g, id);
    }
    else if (id == "power_port")
    {
        line(g, { 0, 0 }, { 0, -14 });
        line(g, { -14, -14 }, { 14, -14 }, 2.2f);
        juce::Path arrow;
        arrow.addTriangle(-7.0f, -16.0f, 7.0f, -16.0f, 0.0f, -24.0f);
        g.fillPath(arrow);
    }
    else if (id == "block_port")
    {
        // Rounded bubble; the canvas writes the port name inside it.
        g.setColour(juce::Colour(0xffffc857));
        g.drawRoundedRectangle(portBubbleRect(), 11.0f, 1.8f);
        g.setColour(lineColour);
        line(g, { -10, 0 }, { 0, 0 });
    }
    else if (id == "net_label")
    {
        // Flag on a short stem; the net name is drawn beside it by the caller.
        line(g, { 0, 0 }, { 0, -12 });
        juce::Path flag;
        flag.startNewSubPath(0.0f, -12.0f);
        flag.lineTo(0.0f, -24.0f);
        flag.lineTo(10.0f, -24.0f);
        flag.lineTo(14.0f, -18.0f);
        flag.lineTo(10.0f, -12.0f);
        flag.closeSubPath();
        g.setColour(juce::Colour(0xff78dcca));
        strokePath(g, flag, 1.6f);
        g.setColour(lineColour);
    }
    else if (id == "ground")
    {
        line(g, { 0, 0 }, { 0, 12 });
        line(g, { -18, 12 }, { 18, 12 }, 2.0f);
        line(g, { -11, 19 }, { 11, 19 }, 2.0f);
        line(g, { -4, 26 }, { 4, 26 }, 2.0f);
    }
    else if (id == "battery")
    {
        line(g, { 0, -48 }, { 0, -6 });
        line(g, { 0, 6 }, { 0, 48 });
        line(g, { -18, -6 }, { 18, -6 }, 2.0f);
        line(g, { -9, 6 }, { 9, 6 }, 3.0f);
        text(g, "+", { 10, -24, 14, 14 });
    }
    else if (id == "audio_in")
    {
        circleSource(g);
        text(g, "IN", { -16, -16, 32, 32 });
    }
    else if (id == "audio_out")
    {
        g.drawEllipse(-12, -12, 24, 24, 2.0f);
        text(g, "OUT", { -16, -16, 32, 32 });
    }
    else if (id == "voltage_source")
    {
        circleSource(g);
        text(g, "+", { -8, -20, 16, 14 });
        text(g, "-", { -8, 4, 16, 14 });
    }
    else if (id == "ac_voltage_source")
    {
        circleSource(g);
        sineIn(g, { -14, -7, 28, 14 });
    }
    else if (id == "current_source" || id == "ac_current_source")
    {
        circleSource(g);
        if (id == "ac_current_source")
        {
            sineIn(g, { -14, -18, 28, 10 });
            line(g, { 0, -2 }, { 0, 14 });
            arrowHead(g, { 0, -2 }, { 0, 16 });
        }
        else
        {
            line(g, { 0, -14 }, { 0, 14 });
            arrowHead(g, { 0, -14 }, { 0, 16 });
        }
    }
    else if (id == "vcvs" || id == "ccvs")
    {
        diamondSource(g, false, symbol.pins[2].name, symbol.pins[3].name);
    }
    else if (id == "vccs" || id == "cccs")
    {
        diamondSource(g, true, symbol.pins[2].name, symbol.pins[3].name);
    }
    else if (id == "signal_source")
    {
        line(g, { -48, 0 }, { -24, 0 });
        line(g, { 24, 0 }, { 48, 0 });
        g.drawEllipse(-24.0f, -24.0f, 48.0f, 48.0f, stroke);
        sineIn(g, { -14, -7, 28, 14 });
    }
    else if (id == "opamp_741")
    {
        juce::Path tri;
        tri.addTriangle(-48.0f, -48.0f, -48.0f, 48.0f, 48.0f, 0.0f);
        strokePath(g, tri);
        line(g, { -72, -24 }, { -48, -24 });
        line(g, { -72, 24 }, { -48, 24 });
        line(g, { 48, 0 }, { 72, 0 });
        line(g, { 0, -72 }, { 0, -24 });
        line(g, { 0, 24 }, { 0, 72 });
        text(g, "+", { -44, -32, 16, 16 }, 15.0f);
        text(g, "-", { -44, 16, 16, 16 }, 15.0f);
    }
    else if (id == "npn" || id == "pnp")
    {
        bjtBody(g, id == "npn");
    }
    else if (id == "nmos" || id == "pmos")
    {
        mosBody(g, id == "nmos");
    }
    else if (id == "njfet" || id == "pjfet")
    {
        jfetBody(g, id == "njfet");
    }
    else if (id == "switch_spst")
    {
        line(g, { -48, 0 }, { -24, 0 });
        line(g, { 24, 0 }, { 48, 0 });
        contact(g, { -21, 0 });
        contact(g, { 21, 0 });
        line(g, { -19, -2 }, { 18, -18 });
    }
    else if (id == "switch_spdt")
    {
        line(g, { -48, 0 }, { -24, 0 });
        line(g, { 24, -24 }, { 48, -24 });
        line(g, { 24, 24 }, { 48, 24 });
        contact(g, { -21, 0 });
        contact(g, { 21, -24 });
        contact(g, { 21, 24 });
        line(g, { -19, -2 }, { 18, -20 });
    }
    else if (id == "relay_spst")
    {
        line(g, { -72, -24 }, { -30, -24 });
        line(g, { -72, 24 }, { -30, 24 });
        line(g, { -30, -24 }, { -30, -14 });
        line(g, { -30, 24 }, { -30, 14 });
        g.drawRect(-42.0f, -14.0f, 24.0f, 28.0f, stroke);
        line(g, { -42, 14 }, { -18, -14 }, 1.4f);
        line(g, { 72, -24 }, { 30, -24 });
        line(g, { 72, 24 }, { 30, 24 });
        contact(g, { 30, -24 });
        contact(g, { 30, 24 });
        line(g, { 30, 21 }, { 18, -18 });
        const float dashes[] = { 4.0f, 4.0f };
        g.drawDashedLine(juce::Line<float>(-18.0f, 0.0f, 24.0f, 0.0f), dashes, 2, 1.2f);
    }
    else if (id == "fuse")
    {
        line(g, { -48, 0 }, { -24, 0 });
        line(g, { 24, 0 }, { 48, 0 });
        g.drawRect(-24.0f, -9.0f, 48.0f, 18.0f, stroke);
        line(g, { -24, 0 }, { 24, 0 }, 1.2f);
    }
    else if (id == "connector_2" || id == "connector_3")
    {
        const auto& pins = symbol.pins;
        const auto top = pins.front().offset.y - 12.0f;
        const auto bottom = pins.back().offset.y + 12.0f;
        g.drawRect(-12.0f, top, 18.0f, bottom - top, stroke);
        for (const auto& pin : pins)
        {
            line(g, pin.offset, { -16, pin.offset.y });
            g.drawEllipse(-16.0f, pin.offset.y - 3.0f, 6.0f, 6.0f, 1.4f);
        }
    }
    else if (id == "test_point")
    {
        line(g, { -24, 0 }, { -8, 0 });
        g.drawEllipse(-8.0f, -8.0f, 16.0f, 16.0f, stroke);
    }
    else if (id == "logic_not")
    {
        line(g, { -48, 0 }, { -30, 0 });
        juce::Path tri;
        tri.addTriangle(-30.0f, -22.0f, -30.0f, 22.0f, 14.0f, 0.0f);
        strokePath(g, tri);
        bubble(g, 14.0f);
        line(g, { 24, 0 }, { 48, 0 });
    }
    else if (id == "logic_and" || id == "logic_nand")
    {
        gateInputs(g, -30.0f);
        andShape(g);
        if (id == "logic_nand") { bubble(g, 30.0f); line(g, { 40, 0 }, { 48, 0 }); }
        else line(g, { 30, 0 }, { 48, 0 });
    }
    else if (id == "logic_or" || id == "logic_nor" || id == "logic_xor")
    {
        gateInputs(g, id == "logic_xor" ? -34.0f : -24.0f);
        orShape(g, id == "logic_xor");
        if (id == "logic_nor") { bubble(g, 30.0f); line(g, { 40, 0 }, { 48, 0 }); }
        else line(g, { 30, 0 }, { 48, 0 });
    }
    else if (id == "oscilloscope_2ch")
    {
        const juce::Rectangle<float> body { -60, -48, 120, 96 };
        g.setColour(instrumentFill);
        g.fillRoundedRectangle(body, 6.0f);
        g.setColour(juce::Colour(0xffff6b6b));
        g.drawRoundedRectangle(body, 6.0f, 2.0f);
        g.setColour(juce::Colour(0xff26323d));
        const auto screen = body.reduced(12.0f, 18.0f).translated(0.0f, 6.0f);
        g.fillRoundedRectangle(screen, 4.0f);
        g.setColour(juce::Colour(0xff78dcca));
        sineIn(g, screen.reduced(6.0f, 10.0f), 2.0f);
        text(g, "SCOPE", body.withHeight(20.0f), 11.0f);
        g.setColour(lineColour);
        line(g, { -72, -24 }, { -60, -24 });
        line(g, { -72, 24 }, { -60, 24 });
        line(g, { 0, 48 }, { 0, 72 });
    }
    else if (id == "bode_analyzer")
    {
        const juce::Rectangle<float> body { -60, -48, 120, 96 };
        g.setColour(instrumentFill);
        g.fillRoundedRectangle(body, 6.0f);
        g.setColour(juce::Colour(0xff9b8cff));
        g.drawRoundedRectangle(body, 6.0f, 2.0f);
        const auto screen = body.reduced(12.0f, 18.0f).translated(0.0f, 6.0f);
        g.setColour(juce::Colour(0xff26323d));
        g.fillRoundedRectangle(screen, 4.0f);
        juce::Path curve; // a low-pass magnitude response
        curve.startNewSubPath(screen.getX() + 4.0f, screen.getY() + 12.0f);
        curve.lineTo(screen.getCentreX(), screen.getY() + 12.0f);
        curve.quadraticTo(screen.getCentreX() + 14.0f, screen.getY() + 12.0f, screen.getRight() - 6.0f, screen.getBottom() - 6.0f);
        g.setColour(juce::Colour(0xff9b8cff));
        strokePath(g, curve, 1.6f);
        text(g, "BODE", body.withHeight(20.0f), 11.0f);
        g.setColour(lineColour);
        line(g, { -72, -24 }, { -60, -24 });
        line(g, { -72, 24 }, { -60, 24 });
        line(g, { 0, 48 }, { 0, 72 });
    }
    else if (id == "digital_multimeter")
    {
        const juce::Rectangle<float> body { -48, -36, 96, 72 };
        g.setColour(instrumentFill);
        g.fillRoundedRectangle(body, 6.0f);
        g.setColour(juce::Colour(0xffffc857));
        g.drawRoundedRectangle(body, 6.0f, 2.0f);
        const auto display = body.reduced(12.0f, 12.0f).withHeight(24.0f);
        g.setColour(juce::Colour(0xff0e141a));
        g.fillRoundedRectangle(display, 4.0f);
        g.setColour(juce::Colour(0xff78dcca));
        text(g, readout.isNotEmpty() ? readout : "DCV", display, 13.0f);
        g.setColour(lineColour);
        text(g, "DMM", body.withTrimmedTop(36.0f), 12.0f);
        line(g, { -72, -24 }, { -48, -24 });
        line(g, { -72, 24 }, { -48, 24 });
    }
}
}


