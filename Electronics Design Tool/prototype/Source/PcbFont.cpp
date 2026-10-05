#include "PcbFont.h"

#include <cmath>
#include <map>

namespace pcb::font
{
namespace
{
// Each glyph: strokes separated by '|', points "x,y" separated by spaces.
const std::map<juce::juce_wchar, const char*>& glyphSource()
{
    static const std::map<juce::juce_wchar, const char*> g {
        { 'A', "0,0 2,6 4,0|0.7,2 3.3,2" },
        { 'B', "0,0 0,6 3,6 4,5 4,4 3,3 0,3|3,3 4,2 4,1 3,0 0,0" },
        { 'C', "4,5 3,6 1,6 0,5 0,1 1,0 3,0 4,1" },
        { 'D', "0,0 0,6 2.5,6 4,4.5 4,1.5 2.5,0 0,0" },
        { 'E', "4,6 0,6 0,0 4,0|0,3 3,3" },
        { 'F', "4,6 0,6 0,0|0,3 3,3" },
        { 'G', "4,5 3,6 1,6 0,5 0,1 1,0 3,0 4,1 4,3 2,3" },
        { 'H', "0,0 0,6|4,0 4,6|0,3 4,3" },
        { 'I', "1,6 3,6|2,6 2,0|1,0 3,0" },
        { 'J', "1,6 4,6|3,6 3,1 2,0 1,0 0,1" },
        { 'K', "0,0 0,6|4,6 0,2|1.5,3.5 4,0" },
        { 'L', "0,6 0,0 4,0" },
        { 'M', "0,0 0,6 2,3 4,6 4,0" },
        { 'N', "0,0 0,6 4,0 4,6" },
        { 'O', "1,0 0,1 0,5 1,6 3,6 4,5 4,1 3,0 1,0" },
        { 'P', "0,0 0,6 3,6 4,5 4,4 3,3 0,3" },
        { 'Q', "1,0 0,1 0,5 1,6 3,6 4,5 4,1 3,0 1,0|2.5,1.5 4,0" },
        { 'R', "0,0 0,6 3,6 4,5 4,4 3,3 0,3|2,3 4,0" },
        { 'S', "4,5 3,6 1,6 0,5 0,4 1,3 3,3 4,2 4,1 3,0 1,0 0,1" },
        { 'T', "0,6 4,6|2,6 2,0" },
        { 'U', "0,6 0,1 1,0 3,0 4,1 4,6" },
        { 'V', "0,6 2,0 4,6" },
        { 'W', "0,6 1,0 2,4 3,0 4,6" },
        { 'X', "0,0 4,6|0,6 4,0" },
        { 'Y', "0,6 2,3 4,6|2,3 2,0" },
        { 'Z', "0,6 4,6 0,0 4,0" },
        { 'a', "4,4 4,0|4,3 3,4 1,4 0,3 0,1 1,0 3,0 4,1" },
        { 'b', "0,6 0,0|0,3 1,4 3,4 4,3 4,1 3,0 1,0 0,1" },
        { 'c', "4,3.5 3,4 1,4 0,3 0,1 1,0 3,0 4,0.5" },
        { 'd', "4,6 4,0|4,3 3,4 1,4 0,3 0,1 1,0 3,0 4,1" },
        { 'e', "0,2 4,2 4,3 3,4 1,4 0,3 0,1 1,0 3.5,0" },
        { 'f', "3.5,6 2.5,6 1.5,5 1.5,0|0,4 3,4" },
        { 'g', "4,4 4,-1 3,-2 1,-2|4,3 3,4 1,4 0,3 0,1 1,0 3,0 4,1" },
        { 'h', "0,6 0,0|0,3 1,4 3,4 4,3 4,0" },
        { 'i', "2,4 2,0|2,5.2 2,5.8" },
        { 'j', "3,4 3,-1 2,-2 1,-2|3,5.2 3,5.8" },
        { 'k', "0,6 0,0|4,4 0,1.5|1.5,2.5 4,0" },
        { 'l', "1,6 2,6 2,0|1,0 3,0" },
        { 'm', "0,4 0,0|0,3 1,4 2,3 2,0|2,3 3,4 4,3 4,0" },
        { 'n', "0,4 0,0|0,3 1,4 3,4 4,3 4,0" },
        { 'o', "1,0 0,1 0,3 1,4 3,4 4,3 4,1 3,0 1,0" },
        { 'p', "0,4 0,-2|0,3 1,4 3,4 4,3 4,1 3,0 1,0 0,1" },
        { 'q', "4,4 4,-2|4,3 3,4 1,4 0,3 0,1 1,0 3,0 4,1" },
        { 'r', "0,4 0,0|0,2.5 1.5,4 3,4 4,3.5" },
        { 's', "4,3.5 3,4 1,4 0,3.2 1,2 3,2 4,0.8 3,0 1,0 0,0.5" },
        { 't', "1.5,6 1.5,1 2.5,0 3.5,0|0,4 3,4" },
        { 'u', "0,4 0,1 1,0 3,0 4,1|4,4 4,0" },
        { 'v', "0,4 2,0 4,4" },
        { 'w', "0,4 1,0 2,3 3,0 4,4" },
        { 'x', "0,0 4,4|0,4 4,0" },
        { 'y', "0,4 2,0|4,4 1,-2" },
        { 'z', "0,4 4,4 0,0 4,0" },
        { '0', "1,0 0,1 0,5 1,6 3,6 4,5 4,1 3,0 1,0|1,1.5 3,4.5" },
        { '1', "1,5 2,6 2,0|1,0 3,0" },
        { '2', "0,5 1,6 3,6 4,5 4,4 0,0 4,0" },
        { '3', "0,5 1,6 3,6 4,5 4,4 3,3 4,2 4,1 3,0 1,0 0,1|1.5,3 3,3" },
        { '4', "3,0 3,6 0,2 4,2" },
        { '5', "4,6 0,6 0,3.5 3,3.5 4,2.5 4,1 3,0 1,0 0,1" },
        { '6', "4,5 3,6 1,6 0,5 0,1 1,0 3,0 4,1 4,2 3,3 0,3" },
        { '7', "0,6 4,6 1.5,0" },
        { '8', "1,3 0,4 0,5 1,6 3,6 4,5 4,4 3,3 1,3 0,2 0,1 1,0 3,0 4,1 4,2 3,3" },
        { '9', "4,3 1,3 0,4 0,5 1,6 3,6 4,5 4,1 3,0 1,0 0,1" },
        { '.', "2,0 2,0.4" },
        { ',', "2,0.4 2,0 1.5,-1" },
        { ':', "2,0 2,0.4|2,3 2,3.4" },
        { ';', "2,3 2,3.4|2,0.4 2,0 1.5,-1" },
        { '-', "0.5,3 3.5,3" },
        { '+', "0.5,3 3.5,3|2,1.5 2,4.5" },
        { '=', "0.5,2 3.5,2|0.5,4 3.5,4" },
        { '/', "0,0 4,6" },
        { '\\', "0,6 4,0" },
        { '(', "3,6.5 2,5 1.5,3 2,1 3,-0.5" },
        { ')', "1,6.5 2,5 2.5,3 2,1 1,-0.5" },
        { '[', "3,6.5 1.5,6.5 1.5,-0.5 3,-0.5" },
        { ']', "1,6.5 2.5,6.5 2.5,-0.5 1,-0.5" },
        { '{', "3,6.5 2,6 2,3.5 1,3 2,2.5 2,0 3,-0.5" },
        { '}', "1,6.5 2,6 2,3.5 3,3 2,2.5 2,0 1,-0.5" },
        { '_', "0,-1 4,-1" },
        { '\'', "2,6 2,4.5" },
        { '"', "1.3,6 1.3,4.5|2.7,6 2.7,4.5" },
        { '!', "2,6 2,1.8|2,0 2,0.4" },
        { '?', "0,5 1,6 3,6 4,5 4,4 2,2.5 2,1.6|2,0 2,0.4" },
        { '#', "1,0 1.5,6|2.5,0 3,6|0,2 4,2|0,4 4,4" },
        { '%', "0,0 4,6|0.5,6 0,5.5 0.5,5 1,5.5 0.5,6|3.5,1 3,0.5 3.5,0 4,0.5 3.5,1" },
        { '&', "4,0 1,4 1,5 2,6 3,5 3,4 0,2 0,1 1,0 2,0 4,2" },
        { '*', "2,1.5 2,4.5|0.7,2.2 3.3,3.8|0.7,3.8 3.3,2.2" },
        { '<', "4,5 0,3 4,1" },
        { '>', "0,5 4,3 0,1" },
        { '@', "3,2 3,4 1.5,4 1,3 1.5,2 3,2 4,2.5 4,5 3,6 1,6 0,5 0,1 1,0 3.5,0" },
        { '~', "0,3 1,4 3,2 4,3" },
        { '^', "0,4 2,6 4,4" },
        { '|', "2,6.5 2,-0.5" },
        { '$', "4,5 3,6 1,6 0,5 0,4 1,3 3,3 4,2 4,1 3,0 1,0 0,1|2,6.8 2,-0.8" },
        { 0x00B0, "1.5,6 1,5.5 1,4.5 1.5,4 2.5,4 3,4.5 3,5.5 2.5,6 1.5,6" },              // degree
        { 0x00B5, "0,4 0,-2|0,1 1,0 3,0 4,1|4,4 4,0" },                                    // micro
        { 0x03BC, "0,4 0,-2|0,1 1,0 3,0 4,1|4,4 4,0" },                                    // mu
        { 0x03A9, "0,0 1.5,0 1.5,1 0,2.5 0,4.5 1,6 3,6 4,4.5 4,2.5 2.5,1 2.5,0 4,0" },     // Omega
        { 0x2126, "0,0 1.5,0 1.5,1 0,2.5 0,4.5 1,6 3,6 4,4.5 4,2.5 2.5,1 2.5,0 4,0" },     // ohm sign
        { 0x00B1, "0.5,3.5 3.5,3.5|2,2 2,5|0.5,0.5 3.5,0.5" },                             // plus-minus
    };
    return g;
}

std::vector<Stroke> parse(const char* source)
{
    std::vector<Stroke> strokes;
    for (const auto& part : juce::StringArray::fromTokens(source, "|", {}))
    {
        Stroke s;
        for (const auto& pt : juce::StringArray::fromTokens(part, " ", {}))
            if (pt.containsChar(','))
                s.push_back({ pt.upToFirstOccurrenceOf(",", false, false).getDoubleValue(), pt.fromFirstOccurrenceOf(",", false, false).getDoubleValue() });
        if (s.size() >= 2) strokes.push_back(s);
    }
    return strokes;
}
}

const std::vector<Stroke>& glyph(juce::juce_wchar c)
{
    static std::map<juce::juce_wchar, std::vector<Stroke>> cache;
    static const std::vector<Stroke> space;
    if (c == ' ') return space;
    auto found = cache.find(c);
    if (found != cache.end()) return found->second;
    const auto& src = glyphSource();
    auto s = src.find(c);
    if (s == src.end()) s = src.find('?');
    return cache[c] = parse(s->second);
}

std::vector<Stroke> layout(const juce::String& text, Point at, double height, double rotationDeg, int align, bool mirror)
{
    const double unit = height / capUnits;
    const auto lines = juce::StringArray::fromLines(text);
    const int lineCount = std::max(1, lines.size());
    // Block: cap height of the first line down to the baseline of the last.
    const double blockHeight = capUnits + (lineCount - 1) * lineUnits;
    const double angle = juce::degreesToRadians(rotationDeg);
    const double ca = std::cos(angle), sa = std::sin(angle);
    std::vector<Stroke> out;
    for (int li = 0; li < lineCount; ++li)
    {
        const auto line = lines[li];
        const int n = line.length();
        const double width = n > 0 ? (n - 1) * advanceUnits + 4.0 : 0.0;
        const double x0 = align < 0 ? 0.0 : align > 0 ? -width : -width / 2.0;
        const double y0 = blockHeight / 2.0 - capUnits - li * lineUnits; // baseline of this line
        for (int i = 0; i < n; ++i)
            for (const auto& g : glyph(line[i]))
            {
                Stroke s;
                for (const auto& p : g)
                {
                    double x = (x0 + i * advanceUnits + p.x) * unit, y = (y0 + p.y) * unit;
                    if (mirror) x = -x;
                    s.push_back({ at.x + x * ca - y * sa, at.y + x * sa + y * ca });
                }
                out.push_back(s);
            }
    }
    return out;
}
}
