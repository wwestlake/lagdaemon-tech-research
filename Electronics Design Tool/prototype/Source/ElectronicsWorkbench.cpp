#include "ElectronicsWorkbench.h"
#include "ElectronicsKnowledge.h"
#include "LocalAgentApi.h"
#include "SchematicSymbols.h"

#include <ai_provider/AiConfig.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <memory>
#include <map>
#include <set>
#include <thread>

namespace
{
constexpr int toolbarHeight = 36;
constexpr int menuHeight = 24;

void styleTextEditor(juce::TextEditor& editor, bool mono = false)
{
    editor.setMultiLine(true);
    editor.setReturnKeyStartsNewLine(true);
    editor.setScrollbarsShown(true);
    editor.setFont(juce::Font(mono ? "Consolas" : "Segoe UI", mono ? 14.0f : 15.0f, juce::Font::plain));
    editor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff10161d));
    editor.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff33424d));
    editor.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour(0xff5aa7c8));
    editor.setColour(juce::TextEditor::textColourId, juce::Colour(0xffdce9ee));
}

void showCursorForEvent(const juce::MouseEvent& event, juce::MouseCursor cursor)
{
    auto source = event.source;
    if (source.hasMouseCursor())
        source.showMouseCursor(cursor);
}

juce::Colour dmmLeadColour(bool positive)
{
    return positive ? juce::Colour(0xffd85f5f) : juce::Colour(0xff1a1f25);
}

juce::Colour scopeChannelColour(int channelIndex)
{
    static const juce::Colour colours[] = {
        juce::Colour(0xffc86a6a),
        juce::Colour(0xff6fac7d),
        juce::Colour(0xff6687c6),
        juce::Colour(0xffc4ad58)
    };

    return colours[(size_t)juce::jlimit(0, 3, channelIndex % 4)];
}

juce::String jsonQuote(const juce::String& text)
{
    return "\"" + text.replace("\\", "\\\\").replace("\"", "\\\"") + "\"";
}

juce::String htmlEscape(const juce::String& text)
{
    return text.replace("&", "&amp;")
               .replace("<", "&lt;")
               .replace(">", "&gt;")
               .replace("\"", "&quot;");
}

juce::String htmlDecode(juce::String text)
{
    return text.replace("&amp;", "&")
               .replace("&quot;", "\"")
               .replace("&#39;", "'")
               .replace("&#x27;", "'")
               .replace("&lt;", "<")
               .replace("&gt;", ">")
               .replace("&nbsp;", " ");
}

juce::String stripHtmlTags(const juce::String& html)
{
    juce::String result;
    bool insideTag = false;
    for (int i = 0; i < html.length(); ++i)
    {
        const auto ch = html[i];
        if (ch == '<')
        {
            insideTag = true;
            continue;
        }
        if (ch == '>')
        {
            insideTag = false;
            continue;
        }
        if (!insideTag)
            result << ch;
    }
    return htmlDecode(result).trim();
}

juce::String decodeDuckDuckGoRedirect(juce::String url)
{
    url = htmlDecode(url.trim());
    if (url.startsWith("//"))
        url = "https:" + url;

    const auto marker = juce::String("uddg=");
    const auto index = url.indexOf(marker);
    if (index >= 0)
    {
        const auto encoded = url.substring(index + marker.length())
            .upToFirstOccurrenceOf("&", false, false);
        return juce::URL::removeEscapeChars(encoded);
    }

    return url;
}

void collectDuckDuckGoHtmlResults(const juce::String& html, int limit, juce::StringArray& rows)
{
    int cursor = 0;
    while (rows.size() < limit)
    {
        const auto anchorStart = html.indexOf(cursor, "class=\"result__a\"");
        if (anchorStart < 0)
            break;

        const auto hrefStart = html.indexOf(anchorStart, "href=\"");
        const auto hrefEnd = hrefStart >= 0 ? html.indexOf(hrefStart + 6, "\"") : -1;
        const auto titleStart = hrefEnd >= 0 ? html.indexOf(hrefEnd, ">") : -1;
        const auto titleEnd = titleStart >= 0 ? html.indexOf(titleStart, "</a>") : -1;
        if (hrefStart < 0 || hrefEnd < 0 || titleStart < 0 || titleEnd < 0)
            break;

        const auto url = decodeDuckDuckGoRedirect(html.substring(hrefStart + 6, hrefEnd));
        const auto title = stripHtmlTags(html.substring(titleStart + 1, titleEnd));
        cursor = titleEnd + 4;

        juce::String snippet;
        const auto snippetClass = html.indexOf(cursor, "class=\"result__snippet\"");
        const auto nextAnchor = html.indexOf(cursor, "class=\"result__a\"");
        if (snippetClass >= 0 && (nextAnchor < 0 || snippetClass < nextAnchor))
        {
            const auto snippetStart = html.indexOf(snippetClass, ">");
            const auto snippetEnd = snippetStart >= 0 ? html.indexOf(snippetStart, "</a>") : -1;
            if (snippetStart >= 0 && snippetEnd >= 0)
                snippet = stripHtmlTags(html.substring(snippetStart + 1, snippetEnd));
        }

        if (title.isNotEmpty() && url.startsWithIgnoreCase("http"))
        {
            juce::String row;
            row << "{ \"title\": " << jsonQuote(title)
                << ", \"snippet\": " << jsonQuote(snippet)
                << ", \"url\": " << jsonQuote(url) << " }";
            rows.add(row);
        }
    }
}

juce::String inlineMarkdownToHtml(juce::String text)
{
    text = htmlEscape(text);

    juce::String out;
    bool inCode = false;
    bool inMath = false;
    for (int i = 0; i < text.length(); ++i)
    {
        const auto c = text[i];
        if (c == '`')
        {
            out << (inCode ? "</code>" : "<code>");
            inCode = !inCode;
        }
        else if (c == '$')
        {
            out << (inMath ? "\\)" : "\\(");
            inMath = !inMath;
        }
        else
        {
            out << juce::String::charToString(c);
        }
    }
    if (inCode) out << "</code>";
    if (inMath) out << "\\)";
    return out;
}

juce::String markdownToHtmlDocument(const juce::String& title, const juce::String& markdown)
{
    const auto lines = juce::StringArray::fromLines(markdown);
    juce::String body;
    bool inList = false;
    bool inCode = false;
    bool inMathBlock = false;

    auto closeList = [&] {
        if (inList)
        {
            body << "</ul>\n";
            inList = false;
        }
    };

    for (const auto& rawLine : lines)
    {
        const auto trimmed = rawLine.trim();

        if (trimmed.startsWith("```"))
        {
            closeList();
            body << (inCode ? "</code></pre>\n" : "<pre><code>");
            inCode = !inCode;
            continue;
        }
        if (inCode)
        {
            body << htmlEscape(rawLine) << "\n";
            continue;
        }

        if (trimmed == "$$")
        {
            closeList();
            body << (inMathBlock ? "\\]</div>\n" : "<div class=\"math-block\">\\[");
            inMathBlock = !inMathBlock;
            continue;
        }
        if (inMathBlock)
        {
            body << htmlEscape(rawLine) << "\n";
            continue;
        }

        if (trimmed.isEmpty())
        {
            closeList();
            continue;
        }

        if (trimmed.startsWith("# "))
        {
            closeList();
            body << "<h1>" << inlineMarkdownToHtml(trimmed.substring(2)) << "</h1>\n";
        }
        else if (trimmed.startsWith("## "))
        {
            closeList();
            body << "<h2>" << inlineMarkdownToHtml(trimmed.substring(3)) << "</h2>\n";
        }
        else if (trimmed.startsWith("### "))
        {
            closeList();
            body << "<h3>" << inlineMarkdownToHtml(trimmed.substring(4)) << "</h3>\n";
        }
        else if (trimmed.startsWith("- ") || trimmed.startsWith("* "))
        {
            if (!inList)
            {
                body << "<ul>\n";
                inList = true;
            }
            body << "<li>" << inlineMarkdownToHtml(trimmed.substring(2)) << "</li>\n";
        }
        else
        {
            closeList();
            body << "<p>" << inlineMarkdownToHtml(trimmed) << "</p>\n";
        }
    }

    closeList();
    if (inCode) body << "</code></pre>\n";
    if (inMathBlock) body << "\\]</div>\n";

    juce::String html;
    html << "<!doctype html><html><head><meta charset=\"utf-8\"><meta http-equiv=\"X-UA-Compatible\" content=\"IE=edge\">"
         << "<title>" << htmlEscape(title) << "</title>"
         << "<link rel=\"stylesheet\" href=\"https://cdn.jsdelivr.net/npm/katex@0.11.1/dist/katex.min.css\">"
         << "<script src=\"https://cdn.jsdelivr.net/npm/katex@0.11.1/dist/katex.min.js\"></script>"
         << "<script src=\"https://cdn.jsdelivr.net/npm/katex@0.11.1/dist/contrib/auto-render.min.js\"></script>"
         << "<script>window.onload=function(){if(window.renderMathInElement){renderMathInElement(document.body,{delimiters:[{left:'\\\\[',right:'\\\\]',display:true},{left:'\\\\(',right:'\\\\)',display:false}]});}}</script>"
         << "<style>"
         << "body{margin:0;padding:24px;background:#10161d;color:#dce9ee;font:15px/1.55 Segoe UI,Arial,sans-serif;}"
         << "h1,h2,h3{color:#78dcca;margin:0 0 12px;}h1{font-size:24px;}h2{font-size:19px;margin-top:24px;}h3{font-size:16px;margin-top:18px;}"
         << "p,ul{max-width:920px;}code{background:#1d2a33;border:1px solid #33424d;border-radius:4px;padding:1px 4px;}"
         << "pre{background:#0b1117;border:1px solid #33424d;border-radius:6px;padding:12px;overflow:auto;}"
         << ".math-block{margin:16px 0;padding:12px;background:#0b1117;border-left:3px solid #78dcca;overflow:auto;}"
         << "a{color:#8fd8ff;}</style></head><body>" << body << "</body></html>";
    return html;
}

struct RlcHighPassDesign
{
    double cutoffHz = 10.0;
    double impedanceOhms = 8.0;
    double sourceOhms = 8.0;
    double loadOhms = 8.0;
    double capacitanceFarads = 0.0;
    double inductanceHenries = 0.0;
};

struct RlcAcSample
{
    double frequencyHz = 0.0;
    double rawGainDb = 0.0;
    double normalizedGainDb = 0.0;
    double phaseDegrees = 0.0;
    double inputImpedanceReal = 0.0;
    double inputImpedanceImag = 0.0;
    double inputImpedanceMag = 0.0;
};

RlcHighPassDesign makeRlcHighPassDesign(double cutoffHz, double impedanceOhms)
{
    RlcHighPassDesign design;
    design.cutoffHz = std::max(0.001, cutoffHz);
    design.impedanceOhms = std::max(0.001, impedanceOhms);
    design.sourceOhms = design.impedanceOhms;
    design.loadOhms = design.impedanceOhms;

    const auto omega = juce::MathConstants<double>::twoPi * design.cutoffHz;
    const auto butterworthQ = std::sqrt(2.0);
    design.capacitanceFarads = 1.0 / (omega * design.impedanceOhms * butterworthQ);
    design.inductanceHenries = design.impedanceOhms / (omega * butterworthQ);
    return design;
}

juce::String numberText(double value, int decimals = 6)
{
    return juce::String(value, decimals).trimCharactersAtEnd("0").trimCharactersAtEnd(".");
}

juce::String spiceCapacitance(double farads)
{
    return numberText(farads * 1000.0, 6) + "m";
}

juce::String spiceInductance(double henries)
{
    return numberText(henries * 1000.0, 6) + "m";
}

juce::String humanCapacitance(double farads)
{
    return numberText(farads * 1000000.0, 2) + " uF";
}

juce::String humanInductance(double henries)
{
    return numberText(henries * 1000.0, 3) + " mH";
}

double dbFromMagnitude(double magnitude)
{
    return 20.0 * std::log10(std::max(1.0e-12, magnitude));
}

std::vector<RlcAcSample> sweepRlcHighPass(const RlcHighPassDesign& design)
{
    std::vector<RlcAcSample> samples;
    constexpr int sampleCount = 481;
    const auto startHz = std::max(0.01, design.cutoffHz / 100.0);
    const auto stopHz = design.cutoffHz * 100.0;
    const auto logStart = std::log10(startHz);
    const auto logStop = std::log10(stopHz);
    const std::complex<double> j(0.0, 1.0);
    const auto passbandDivider = design.loadOhms / (design.sourceOhms + design.loadOhms);

    samples.reserve(sampleCount);
    for (int index = 0; index < sampleCount; ++index)
    {
        const auto t = (double)index / (double)(sampleCount - 1);
        const auto frequency = std::pow(10.0, logStart + (logStop - logStart) * t);
        const auto omega = juce::MathConstants<double>::twoPi * frequency;
        const auto zc = 1.0 / (j * omega * design.capacitanceFarads);
        const auto zl = j * omega * design.inductanceHenries;
        const auto zLoad = std::complex<double>(design.loadOhms, 0.0);
        const auto zParallel = 1.0 / (1.0 / zLoad + 1.0 / zl);
        const auto inputImpedance = zc + zParallel;
        const auto transfer = zParallel / (std::complex<double>(design.sourceOhms, 0.0) + inputImpedance);
        const auto normalized = transfer / passbandDivider;

        RlcAcSample sample;
        sample.frequencyHz = frequency;
        sample.rawGainDb = dbFromMagnitude(std::abs(transfer));
        sample.normalizedGainDb = dbFromMagnitude(std::abs(normalized));
        sample.phaseDegrees = std::atan2(normalized.imag(), normalized.real()) * 180.0 / juce::MathConstants<double>::pi;
        sample.inputImpedanceReal = inputImpedance.real();
        sample.inputImpedanceImag = inputImpedance.imag();
        sample.inputImpedanceMag = std::abs(inputImpedance);
        samples.push_back(sample);
    }
    return samples;
}

RlcAcSample sampleRlcHighPassAt(const RlcHighPassDesign& design, double frequency)
{
    const std::complex<double> j(0.0, 1.0);
    const auto omega = juce::MathConstants<double>::twoPi * frequency;
    const auto zc = 1.0 / (j * omega * design.capacitanceFarads);
    const auto zl = j * omega * design.inductanceHenries;
    const auto zLoad = std::complex<double>(design.loadOhms, 0.0);
    const auto zParallel = 1.0 / (1.0 / zLoad + 1.0 / zl);
    const auto inputImpedance = zc + zParallel;
    const auto passbandDivider = design.loadOhms / (design.sourceOhms + design.loadOhms);
    const auto transfer = zParallel / (std::complex<double>(design.sourceOhms, 0.0) + inputImpedance);
    const auto normalized = transfer / passbandDivider;

    RlcAcSample sample;
    sample.frequencyHz = frequency;
    sample.rawGainDb = dbFromMagnitude(std::abs(transfer));
    sample.normalizedGainDb = dbFromMagnitude(std::abs(normalized));
    sample.phaseDegrees = std::atan2(normalized.imag(), normalized.real()) * 180.0 / juce::MathConstants<double>::pi;
    sample.inputImpedanceReal = inputImpedance.real();
    sample.inputImpedanceImag = inputImpedance.imag();
    sample.inputImpedanceMag = std::abs(inputImpedance);
    return sample;
}

juce::String buildRlcHighPassSpiceNetlist(const RlcHighPassDesign& design)
{
    juce::String netlist;
    netlist << "* Djehuti Electronics Lab generated AC analysis netlist\n";
    netlist << "* 2nd order passive RLC high-pass, Butterworth alignment\n";
    netlist << "* Cutoff: " << numberText(design.cutoffHz, 4) << " Hz, impedance: "
            << numberText(design.impedanceOhms, 4) << " ohm\n\n";
    netlist << "VIN vin 0 AC 1\n";
    netlist << "RS vin in " << numberText(design.sourceOhms, 6) << "\n";
    netlist << "C1 in out " << spiceCapacitance(design.capacitanceFarads) << "\n";
    netlist << "L1 out 0 " << spiceInductance(design.inductanceHenries) << "\n";
    netlist << "RL out 0 " << numberText(design.loadOhms, 6) << "\n\n";
    netlist << ".AC DEC 80 " << numberText(std::max(0.01, design.cutoffHz / 100.0), 6)
            << " " << numberText(design.cutoffHz * 100.0, 6) << "\n";
    netlist << ".PRINT AC VM(out) VP(out) VM(in)\n";
    netlist << ".END\n";
    return netlist;
}

juce::String buildRlcHighPassCsv(const std::vector<RlcAcSample>& samples)
{
    juce::String csv;
    csv << "frequency_hz,normalized_gain_db,raw_gain_db,phase_deg,input_impedance_mag_ohm,input_impedance_real_ohm,input_impedance_imag_ohm\n";
    for (const auto& sample : samples)
    {
        csv << numberText(sample.frequencyHz, 8) << ","
            << numberText(sample.normalizedGainDb, 8) << ","
            << numberText(sample.rawGainDb, 8) << ","
            << numberText(sample.phaseDegrees, 8) << ","
            << numberText(sample.inputImpedanceMag, 8) << ","
            << numberText(sample.inputImpedanceReal, 8) << ","
            << numberText(sample.inputImpedanceImag, 8) << "\n";
    }
    return csv;
}

juce::String buildRlcHighPassSvg(const RlcHighPassDesign& design,
                                 const std::vector<RlcAcSample>& samples)
{
    constexpr double width = 1100.0;
    constexpr double height = 640.0;
    constexpr double left = 82.0;
    constexpr double right = 36.0;
    constexpr double top = 44.0;
    constexpr double bottom = 76.0;
    constexpr double minDb = -60.0;
    constexpr double maxDb = 3.0;
    const auto plotWidth = width - left - right;
    const auto plotHeight = height - top - bottom;
    const auto startHz = std::max(0.01, design.cutoffHz / 100.0);
    const auto stopHz = design.cutoffHz * 100.0;
    const auto logStart = std::log10(startHz);
    const auto logStop = std::log10(stopHz);

    auto xFor = [&](double frequency) {
        return left + (std::log10(std::clamp(frequency, startHz, stopHz)) - logStart) / (logStop - logStart) * plotWidth;
    };
    auto yFor = [&](double db) {
        const auto clamped = std::clamp(db, minDb, maxDb);
        return top + (maxDb - clamped) / (maxDb - minDb) * plotHeight;
    };

    juce::String points;
    for (const auto& sample : samples)
        points << numberText(xFor(sample.frequencyHz), 2) << "," << numberText(yFor(sample.normalizedGainDb), 2) << " ";

    juce::String svg;
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << (int)width
        << "\" height=\"" << (int)height << "\" viewBox=\"0 0 " << (int)width << " " << (int)height << "\">\n";
    svg << "<rect width=\"100%\" height=\"100%\" fill=\"#0e141a\"/>\n";
    svg << "<text x=\"32\" y=\"28\" fill=\"#e8f1f2\" font-family=\"Segoe UI, Arial\" font-size=\"20\" font-weight=\"700\">RLC 2nd Order High-Pass Response</text>\n";
    svg << "<text x=\"32\" y=\"54\" fill=\"#93a7b0\" font-family=\"Segoe UI, Arial\" font-size=\"13\">"
        << numberText(design.cutoffHz, 3) << " Hz cutoff, " << numberText(design.impedanceOhms, 3)
        << " ohm matched source/load, C1 " << humanCapacitance(design.capacitanceFarads)
        << ", L1 " << humanInductance(design.inductanceHenries) << "</text>\n";
    svg << "<rect x=\"" << numberText(left, 1) << "\" y=\"" << numberText(top, 1)
        << "\" width=\"" << numberText(plotWidth, 1) << "\" height=\"" << numberText(plotHeight, 1)
        << "\" fill=\"#111922\" stroke=\"#33424d\" stroke-width=\"1\"/>\n";

    for (double db : { 0.0, -3.0, -10.0, -20.0, -40.0, -60.0 })
    {
        const auto y = yFor(db);
        svg << "<line x1=\"" << numberText(left, 1) << "\" y1=\"" << numberText(y, 1)
            << "\" x2=\"" << numberText(left + plotWidth, 1) << "\" y2=\"" << numberText(y, 1)
            << "\" stroke=\"" << (db == -3.0 ? "#ffc857" : "#26323d") << "\" stroke-width=\"" << (db == -3.0 ? "1.4" : "1") << "\"/>\n";
        svg << "<text x=\"18\" y=\"" << numberText(y + 4.0, 1) << "\" fill=\"#93a7b0\" font-family=\"Segoe UI, Arial\" font-size=\"12\">"
            << numberText(db, 0) << " dB</text>\n";
    }

    for (double frequency : { startHz, design.cutoffHz / 10.0, design.cutoffHz, design.cutoffHz * 10.0, stopHz })
    {
        if (frequency < startHz * 0.999 || frequency > stopHz * 1.001)
            continue;
        const auto x = xFor(frequency);
        svg << "<line x1=\"" << numberText(x, 1) << "\" y1=\"" << numberText(top, 1)
            << "\" x2=\"" << numberText(x, 1) << "\" y2=\"" << numberText(top + plotHeight, 1)
            << "\" stroke=\"" << (std::abs(frequency - design.cutoffHz) < 0.001 ? "#78dcca" : "#26323d")
            << "\" stroke-width=\"" << (std::abs(frequency - design.cutoffHz) < 0.001 ? "1.5" : "1") << "\"/>\n";
        svg << "<text x=\"" << numberText(x - 22.0, 1) << "\" y=\"" << numberText(top + plotHeight + 24.0, 1)
            << "\" fill=\"#93a7b0\" font-family=\"Segoe UI, Arial\" font-size=\"12\">"
            << numberText(frequency, frequency < 1.0 ? 2 : 0) << " Hz</text>\n";
    }

    svg << "<polyline fill=\"none\" stroke=\"#78dcca\" stroke-width=\"3\" points=\"" << points.trim() << "\"/>\n";
    svg << "<circle cx=\"" << numberText(xFor(design.cutoffHz), 2) << "\" cy=\"" << numberText(yFor(-3.01029995664), 2)
        << "\" r=\"5\" fill=\"#ffc857\"/>\n";
    svg << "<text x=\"" << numberText(left + plotWidth - 314.0, 1) << "\" y=\"" << numberText(top + 28.0, 1)
        << "\" fill=\"#dce9ee\" font-family=\"Segoe UI, Arial\" font-size=\"13\">Normalized to matched passband, 0 dB at high frequency</text>\n";
    svg << "<text x=\"" << numberText(left + plotWidth - 314.0, 1) << "\" y=\"" << numberText(top + 48.0, 1)
        << "\" fill=\"#ffc857\" font-family=\"Segoe UI, Arial\" font-size=\"13\">Cutoff marker: -3.01 dB at "
        << numberText(design.cutoffHz, 3) << " Hz</text>\n";
    svg << "<text x=\"" << numberText(left + plotWidth * 0.45, 1) << "\" y=\"" << numberText(height - 22.0, 1)
        << "\" fill=\"#dce9ee\" font-family=\"Segoe UI, Arial\" font-size=\"14\">Frequency (log scale)</text>\n";
    svg << "<text transform=\"translate(20 " << numberText(top + plotHeight * 0.63, 1)
        << ") rotate(-90)\" fill=\"#dce9ee\" font-family=\"Segoe UI, Arial\" font-size=\"14\">Gain (dB)</text>\n";
    svg << "</svg>\n";
    return svg;
}

class NotesPanel : public juce::Component
{
public:
    NotesPanel(const juce::String& heading, const juce::String& body)
    {
        title.setText(heading, juce::dontSendNotification);
        title.setFont(juce::Font(16.0f, juce::Font::bold));
        title.setColour(juce::Label::textColourId, juce::Colour(0xff78dcca));
        addAndMakeVisible(title);

        styleTextEditor(text);
        text.setText(body);
        addAndMakeVisible(text);
    }

    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff151a20)); }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8);
        title.setBounds(area.removeFromTop(24));
        area.removeFromTop(6);
        text.setBounds(area);
    }

private:
    juce::Label title;
    juce::TextEditor text;
};

class ComponentLibraryPanel final : public juce::Component
{
public:
    struct SymbolInfo
    {
        juce::String id;
        juce::String name;
        juce::String category;
    };

    explicit ComponentLibraryPanel(std::function<void(juce::String)> onSelection)
        : onSymbolSelected(std::move(onSelection))
    {
        filter.setTextToShowWhenEmpty("Search components, MPNs, aliases...", juce::Colour(0xff71808c));
        styleTextEditor(filter);
        filter.setMultiLine(false);
        filter.onTextChange = [this] { refreshFilter(); };
        addAndMakeVisible(filter);

        if (!loadSeedLibrary())
            addFallbackLibrary();
        refreshFilter();

        if (onSymbolSelected != nullptr)
            onSymbolSelected("resistor");
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff151a20));
        g.setColour(juce::Colour(0xffdce9ee));
        g.setFont(juce::Font(14.0f, juce::Font::bold));
        g.drawText("Component Library", getLocalBounds().removeFromTop(24).reduced(8, 0), juce::Justification::centredLeft);

        rowBounds.clear();
        auto listArea = getLocalBounds().reduced(8);
        listArea.removeFromTop(24);
        listArea.removeFromTop(28);
        listArea.removeFromTop(8);
        g.setColour(juce::Colour(0xff10161d));
        g.fillRect(listArea);

        int y = listArea.getY() + 4;
        for (int row = 0; row < (int)listModel.items.size(); ++row)
        {
            auto rowArea = juce::Rectangle<int>(listArea.getX() + 4, y, listArea.getWidth() - 8, 42);
            rowBounds.push_back(rowArea);
            paintSymbolRow(g, row, rowArea);
            y += 44;
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8);
        area.removeFromTop(24);
        filter.setBounds(area.removeFromTop(28));
    }

    void mouseMove(const juce::MouseEvent& event) override
    {
        const auto row = rowAt(event.getPosition());
        if (row != hoverRow)
        {
            hoverRow = row;
            repaint();
        }
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        hoverRow = -1;
        repaint();
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        const auto row = rowAt(event.getPosition());
        if (row < 0 || row >= (int)listModel.items.size())
            return;

        selectedRow = row;
        const auto& symbol = listModel.items[(size_t)row];
        if (onSymbolSelected != nullptr)
            onSymbolSelected(symbol.id);

        repaint();
        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this))
        {
            showCursorForEvent(event, juce::MouseCursor::DraggingHandCursor);
            container->startDragging("symbol:" + symbol.id, this);
        }
    }

    void mouseDoubleClick(const juce::MouseEvent& event) override
    {
        const auto row = rowAt(event.getPosition());
        if (row < 0 || row >= (int)listModel.items.size())
            return;

        selectedRow = row;
        const auto& symbol = listModel.items[(size_t)row];
        if (onSymbolSelected != nullptr)
            onSymbolSelected(symbol.id);
        repaint();
    }

private:
    class ComponentList final : public juce::ListBoxModel
    {
    public:
        std::vector<SymbolInfo> items;
        std::function<void(juce::String)> onSelected;

        int getNumRows() override { return (int)items.size(); }

        void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override
        {
            if (selected) g.fillAll(juce::Colour(0xff23394a));
            if (row < 0 || row >= (int)items.size()) return;
            const auto& item = items[(size_t)row];
            g.setColour(item.id == "opamp_741" ? juce::Colour(0xffffc857) : juce::Colour(0xffdce9ee));
            g.setFont(juce::Font(14.0f, juce::Font::bold));
            g.drawText(item.name, 8, 2, width - 16, height / 2, juce::Justification::centredLeft);
            g.setColour(juce::Colour(0xff93a7b0));
            g.setFont(juce::Font(12.0f));
            g.drawText(item.category, 8, height / 2 - 1, width - 16, height / 2, juce::Justification::centredLeft);
        }

        int getRowHeight() const { return 42; }

        void selectedRowsChanged(int lastRowSelected) override
        {
            if (lastRowSelected >= 0 && lastRowSelected < (int)items.size() && onSelected != nullptr)
                onSelected(items[(size_t)lastRowSelected].id);
        }
    };

    class SymbolListBox final : public juce::ListBox
    {
    public:
        explicit SymbolListBox(ComponentList& model)
            : juce::ListBox("components", &model), listModel(model)
        {
        }

        void mouseDown(const juce::MouseEvent& event) override
        {
            juce::ListBox::mouseDown(event);
            dragStartRow = getRowContainingPosition(event.x, event.y);
            if (dragStartRow < 0 || dragStartRow >= (int)listModel.items.size())
                return;

            selectRow(dragStartRow);
            if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this))
            {
                const auto& symbol = listModel.items[(size_t)dragStartRow];
                showCursorForEvent(event, juce::MouseCursor::DraggingHandCursor);
                container->startDragging("symbol:" + symbol.id, this);
            }
        }

        void mouseDrag(const juce::MouseEvent& event) override
        {
            juce::ListBox::mouseDrag(event);
        }

        void mouseUp(const juce::MouseEvent& event) override
        {
            juce::ListBox::mouseUp(event);
            showCursorForEvent(event, juce::MouseCursor::NormalCursor);
            dragStarted = false;
            dragStartRow = -1;
        }

    private:
        ComponentList& listModel;
        int dragStartRow = -1;
        bool dragStarted = false;
    };

    juce::TextEditor filter;
    ComponentList listModel;
    SymbolListBox components { listModel };
    std::vector<SymbolInfo> allSymbols;
    std::vector<juce::Rectangle<int>> rowBounds;
    std::function<void(juce::String)> onSymbolSelected;
    int hoverRow = -1;
    int selectedRow = 0;

    void add(SymbolInfo item) { allSymbols.push_back(std::move(item)); }

    bool loadSeedLibrary()
    {
        const auto seedFile = juce::File(ELECTRONICS_RESEARCH_ROOT)
            .getChildFile("prototype")
            .getChildFile("data")
            .getChildFile("component_seed.json");
        if (!seedFile.existsAsFile())
            return false;

        const auto parsed = juce::JSON::parse(seedFile);
        const auto* root = parsed.getDynamicObject();
        if (root == nullptr || !root->hasProperty("librarySymbols"))
            return false;

        const auto* symbols = root->getProperty("librarySymbols").getArray();
        if (symbols == nullptr)
            return false;

        for (const auto& entry : *symbols)
        {
            const auto* object = entry.getDynamicObject();
            if (object == nullptr)
                continue;

            const auto id = object->getProperty("id").toString();
            const auto name = object->getProperty("name").toString();
            const auto category = object->getProperty("category").toString();
            if (id.isNotEmpty() && name.isNotEmpty())
                add({ id, name, category });
        }
        return !allSymbols.empty();
    }

    void addFallbackLibrary()
    {
        add({ "resistor", "Resistor", "Passive" });
        add({ "potentiometer", "Potentiometer", "Passive" });
        add({ "capacitor", "Capacitor", "Passive" });
        add({ "capacitor_polarized", "Polarized Capacitor", "Passive" });
        add({ "variable_capacitor", "Variable Capacitor", "Passive" });
        add({ "inductor", "Inductor", "Passive" });
        add({ "coupled_inductor", "Coupled Inductor", "Magnetics" });
        add({ "transformer", "Transformer", "Magnetics" });
        add({ "diode", "Diode", "Discrete" });
        add({ "zener_diode", "Zener Diode", "Discrete" });
        add({ "led", "LED", "Discrete" });
        add({ "schottky_diode", "Schottky Diode", "Discrete" });
        add({ "power_bus", "Power Bus", "Bus" });
        add({ "ground_bus", "Ground Bus", "Bus" });
        add({ "battery", "Battery", "Source" });
        add({ "voltage_source", "DC Voltage Source", "Source" });
        add({ "ac_voltage_source", "AC Voltage Source", "Source" });
        add({ "current_source", "DC Current Source", "Source" });
        add({ "ac_current_source", "AC Current Source", "Source" });
        add({ "vcvs", "Voltage-Controlled Voltage Source", "Controlled Source" });
        add({ "vccs", "Voltage-Controlled Current Source", "Controlled Source" });
        add({ "ccvs", "Current-Controlled Voltage Source", "Controlled Source" });
        add({ "cccs", "Current-Controlled Current Source", "Controlled Source" });
        add({ "signal_source", "Signal Source", "Source" });
        add({ "ground", "Ground", "Reference" });
        add({ "opamp_741", "741 Op Amp - provisional", "Analog IC" });
        add({ "npn", "NPN Transistor - generic", "Discrete" });
        add({ "pnp", "PNP Transistor - generic", "Discrete" });
        add({ "nmos", "N-Channel MOSFET - generic", "Discrete" });
        add({ "pmos", "P-Channel MOSFET - generic", "Discrete" });
        add({ "njfet", "N-Channel JFET - generic", "Discrete" });
        add({ "pjfet", "P-Channel JFET - generic", "Discrete" });
        add({ "switch_spst", "SPST Switch", "Switch" });
        add({ "switch_spdt", "SPDT Switch", "Switch" });
        add({ "relay_spst", "SPST Relay", "Switch" });
        add({ "fuse", "Fuse", "Protection" });
        add({ "connector_2", "2-Pin Connector", "Connector" });
        add({ "connector_3", "3-Pin Connector", "Connector" });
        add({ "test_point", "Test Point", "Connector" });
        add({ "logic_not", "Logic Inverter - behavioral", "Digital" });
        add({ "logic_and", "AND Gate - behavioral", "Digital" });
        add({ "logic_or", "OR Gate - behavioral", "Digital" });
        add({ "logic_nand", "NAND Gate - behavioral", "Digital" });
        add({ "logic_nor", "NOR Gate - behavioral", "Digital" });
        add({ "logic_xor", "XOR Gate - behavioral", "Digital" });
        add({ "oscilloscope_2ch", "2-Channel Oscilloscope", "Instrument" });
        add({ "digital_multimeter", "Digital Multimeter", "Instrument" });
    }

    void refreshFilter()
    {
        const auto needle = filter.getText().trim().toLowerCase();
        listModel.items.clear();
        for (const auto& symbol : allSymbols)
        {
            const auto haystack = (symbol.name + " " + symbol.id + " " + symbol.category).toLowerCase();
            if (needle.isEmpty() || haystack.contains(needle))
                listModel.items.push_back(symbol);
        }
        listModel.onSelected = onSymbolSelected;
        selectedRow = listModel.items.empty() ? -1 : std::clamp(selectedRow, 0, (int)listModel.items.size() - 1);
        repaint();
    }

    int rowAt(juce::Point<int> position) const
    {
        for (int i = 0; i < (int)rowBounds.size(); ++i)
            if (rowBounds[(size_t)i].contains(position))
                return i;
        return -1;
    }

    void paintSymbolRow(juce::Graphics& g, int row, juce::Rectangle<int> area)
    {
        if (row < 0 || row >= (int)listModel.items.size())
            return;

        const auto& item = listModel.items[(size_t)row];
        if (row == selectedRow)
        {
            g.setColour(juce::Colour(0xff23394a));
            g.fillRoundedRectangle(area.toFloat(), 4.0f);
        }
        else if (row == hoverRow)
        {
            g.setColour(juce::Colour(0xff202b35));
            g.fillRoundedRectangle(area.toFloat(), 4.0f);
        }

        const auto swatch = area.withWidth(10).withHeight(10).withCentre({ area.getX() + 13, area.getCentreY() });
        g.setColour(item.category == "Source" ? juce::Colour(0xfff4d35e)
                    : item.category == "Bus" ? juce::Colour(0xff78dcca)
                    : item.category == "Analog IC" ? juce::Colour(0xffffc857)
                    : item.category == "Instrument" ? juce::Colour(0xffff6b6b)
                    : juce::Colour(0xff5aa7c8));
        g.fillRoundedRectangle(swatch.toFloat(), 3.0f);

        g.setColour(juce::Colour(0xffdce9ee));
        g.setFont(juce::Font(14.0f, juce::Font::bold));
        g.drawText(item.name, area.withTrimmedLeft(26).withTrimmedBottom(18), juce::Justification::centredLeft, true);
        g.setColour(juce::Colour(0xff93a7b0));
        g.setFont(juce::Font(12.0f));
        g.drawText(item.category + "  " + item.id, area.withTrimmedLeft(26).withTrimmedTop(20), juce::Justification::centredLeft, true);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ComponentLibraryPanel)
};

class SchematicCanvasPanel final : public juce::Component,
                                   public juce::DragAndDropTarget
{
public:
    SchematicCanvasPanel(std::function<juce::String()> getSelectedSymbol,
                         std::function<bool()> getStampMode,
                         std::function<void(juce::String)> onMessage)
        : getSelectedSymbolId(std::move(getSelectedSymbol)),
          getStampPlacementEnabled(std::move(getStampMode)),
          onStatus(std::move(onMessage))
    {
        setWantsKeyboardFocus(true);
    }

    void setSelectionListener(std::function<void(int, juce::String, juce::String, juce::String, juce::String, juce::String, juce::String, juce::String)> listener)
    {
        onSelectionChanged = std::move(listener);
        notifySelection();
    }

    void setProbeListener(std::function<void(juce::String, juce::String, juce::String)> listener)
    {
        onProbeChanged = std::move(listener);
    }

    void setInstrumentOpenListener(std::function<void(juce::String, juce::String)> listener)
    {
        onInstrumentOpen = std::move(listener);
    }

    void setSnapEnabled(bool enabled)
    {
        snapEnabled = enabled;
        repaint();
    }

    bool isSnapEnabled() const
    {
        return snapEnabled;
    }

    void setCanvasZoom(float newZoom)
    {
        canvasZoom = std::clamp(newZoom, 0.5f, 2.5f);
        repaint();
    }

    float getCanvasZoom() const
    {
        return canvasZoom;
    }

    void updateSelectedProperties(const juce::String& value,
                                  const juce::String& frequency,
                                  const juce::String& busName,
                                  const juce::String& family,
                                  const juce::String& manufacturerPart)
    {
        if (selectedInstance < 0 || selectedInstance >= (int)instances.size())
            return;

        auto& instance = instances[(size_t)selectedInstance];
        instance.value = value.trim();
        instance.frequency = frequency.trim();
        instance.busName = busName.trim();
        instance.family = family.trim();
        instance.manufacturerPart = manufacturerPart.trim();
        if (onStatus) onStatus("Updated " + instance.refdes + " properties.");
        notifySelection();
        forceDeferredRepaint();
    }

    void rotateSelected()
    {
        if (selectedInstance < 0 || selectedInstance >= (int)instances.size())
            return;

        auto& instance = instances[(size_t)selectedInstance];
        instance.rotation = schematic::normalizedRotation(instance.rotation + 90);
        if (onStatus) onStatus("Rotated " + instance.refdes + " to " + juce::String(instance.rotation) + " degrees.");
        notifySelection();
        repaint();
    }

    void clearCircuit()
    {
        clearModel();
        if (onStatus) onStatus("New electronics research project initialized.");
    }

    bool loadCircuitJson(const juce::String& json, juce::String& error)
    {
        const auto parsed = juce::JSON::parse(json);
        const auto* root = parsed.getDynamicObject();
        if (root == nullptr)
        {
            error = "Project file is not valid JSON.";
            return false;
        }

        const auto* componentArray = root->getProperty("components").getArray();
        if (componentArray == nullptr)
        {
            error = "Project file has no components array.";
            return false;
        }

        std::vector<Instance> loadedInstances;
        std::vector<juce::Point<float>> loadedJunctions;
        std::vector<Wire> loadedWires;
        std::vector<Probe> loadedProbes;
        std::vector<Group> loadedGroups;

        for (const auto& entry : *componentArray)
        {
            const auto* object = entry.getDynamicObject();
            if (object == nullptr)
                continue;

            Instance instance;
            instance.symbolId = stringProperty(*object, "symbol", {});
            if (!schematic::isSupportedSymbol(instance.symbolId))
            {
                error = "Project uses unsupported symbol '" + instance.symbolId + "'; no substitute was loaded.";
                return false;
            }
            instance.refdes = stringProperty(*object, "id", "U" + juce::String((int)loadedInstances.size() + 1));
            instance.value = stringProperty(*object, "value", defaultValueFor(instance.symbolId));
            instance.frequency = stringProperty(*object, "frequency", defaultFrequencyFor(instance.symbolId));
            instance.busName = stringProperty(*object, "busName", defaultBusNameFor(instance.symbolId));
            instance.position = { floatProperty(*object, "x", 120.0f), floatProperty(*object, "y", 120.0f) };
            instance.rotation = schematic::normalizedRotation((int)floatProperty(*object, "rotation", 0.0f));
            instance.busLength = floatProperty(*object, "length", isRailBus(instance.symbolId) ? 420.0f : 0.0f);

            if (const auto* component = object->getProperty("component").getDynamicObject())
            {
                instance.family = stringProperty(*component, "family", familyFor(instance.symbolId));
                instance.manufacturerPart = stringProperty(*component, "manufacturerPart", {});
            }
            else
            {
                instance.family = familyFor(instance.symbolId);
            }

            loadedInstances.push_back(std::move(instance));
        }

        if (const auto* junctionArray = root->getProperty("junctions").getArray())
        {
            for (const auto& entry : *junctionArray)
            {
                const auto* object = entry.getDynamicObject();
                if (object == nullptr)
                    continue;
                loadedJunctions.push_back({ floatProperty(*object, "x", 0.0f),
                                            floatProperty(*object, "y", 0.0f) });
            }
        }

        auto nodeForLabel = [&](const juce::String& label, WireNode& result) -> bool {
            if (label.startsWith("N") && label.substring(1).containsOnly("0123456789"))
            {
                const auto index = label.substring(1).getIntValue() - 1;
                if (index >= 0 && index < (int)loadedJunctions.size())
                {
                    result = WireNode::forJunction(index);
                    return true;
                }
                return false;
            }

            const auto dot = label.lastIndexOfChar('.');
            if (dot <= 0 || dot >= label.length() - 1)
                return false;

            const auto refdes = label.substring(0, dot);
            const auto pinName = label.substring(dot + 1);
            for (int i = 0; i < (int)loadedInstances.size(); ++i)
            {
                if (loadedInstances[(size_t)i].refdes != refdes)
                    continue;

                const auto symbol = symbolFor(loadedInstances[(size_t)i].symbolId);
                for (int p = 0; p < (int)symbol.pins.size(); ++p)
                {
                    if (symbol.pins[(size_t)p].name == pinName)
                    {
                        result = WireNode::forPin({ i, p });
                        return true;
                    }
                }
                return false;
            }
            return false;
        };

        if (const auto* wireArray = root->getProperty("wires").getArray())
        {
            for (const auto& entry : *wireArray)
            {
                const auto* object = entry.getDynamicObject();
                if (object == nullptr)
                    continue;

                Wire wire;
                const auto a = stringProperty(*object, "a", {});
                const auto b = stringProperty(*object, "b", {});
                if (!nodeForLabel(a, wire.a) || !nodeForLabel(b, wire.b))
                {
                    error = "Project wire references an unknown node: " + a + " -> " + b;
                    return false;
                }
                loadedWires.push_back(wire);
            }
        }

        if (const auto* probeArray = root->getProperty("probes").getArray())
        {
            for (const auto& entry : *probeArray)
            {
                const auto* object = entry.getDynamicObject();
                if (object == nullptr)
                    continue;

                const auto id = stringProperty(*object, "id", {});
                const auto label = probeLabel(id);
                if (id.isEmpty() || label.isEmpty())
                    continue;

                WireNode target;
                const auto targetLabel = stringProperty(*object, "target", {});
                if (!nodeForLabel(targetLabel, target))
                {
                    error = "Project probe references an unknown node: " + targetLabel;
                    return false;
                }

                loadedProbes.push_back({ id, label, probeRole(id), target, probeColour(id) });
            }
        }

        if (const auto* groupArray = root->getProperty("groups").getArray())
        {
            for (const auto& entry : *groupArray)
            {
                const auto* object = entry.getDynamicObject();
                if (object == nullptr)
                    continue;

                Group group;
                group.id = stringProperty(*object, "id", "G" + juce::String((int)loadedGroups.size() + 1));
                group.name = stringProperty(*object, "name", "Group " + juce::String((int)loadedGroups.size() + 1));
                group.category = stringProperty(*object, "category", "user_group");
                group.notes = stringProperty(*object, "notes", {});
                if (const auto* members = object->getProperty("members").getArray())
                {
                    for (const auto& member : *members)
                    {
                        const auto refdes = member.toString();
                        for (int i = 0; i < (int)loadedInstances.size(); ++i)
                            if (loadedInstances[(size_t)i].refdes == refdes)
                                group.memberInstances.push_back(i);
                    }
                }
                if (!group.memberInstances.empty())
                    loadedGroups.push_back(std::move(group));
            }
        }

        const auto previousProbes = probes;
        instances = std::move(loadedInstances);
        junctions = std::move(loadedJunctions);
        wires = std::move(loadedWires);
        probes = std::move(loadedProbes);
        groups = std::move(loadedGroups);
        selectedInstance = instances.empty() ? -1 : 0;
        selectedInstances.clear();
        if (selectedInstance >= 0)
            selectedInstances.add(selectedInstance);
        selectedGroup = -1;
        wireDragging = false;
        draggingInstance = false;
        resizingRail = false;
        notifySelection();

        if (onProbeChanged)
        {
            for (const auto& probe : previousProbes)
                onProbeChanged(probe.id, probe.label, {});
            for (const auto& probe : probes)
                onProbeChanged(probe.id, probe.label, nodeLabel(probe.node));
        }

        repaint();
        return true;
    }

    juce::String buildCircuitJson() const
    {
        const auto netNames = computeNetNames();
        juce::String text;
        text << "{\n";
        text << "  \"schemaVersion\": 2,\n";
        text << "  \"kind\": \"electronics_circuit\",\n";
        text << "  \"components\": [\n";
        for (size_t i = 0; i < instances.size(); ++i)
        {
            const auto& instance = instances[i];
            const auto symbol = symbolFor(instance.symbolId);
            if (i != 0) text << ",\n";
            text << "    {\n";
            text << "      \"id\": " << quote(instance.refdes) << ",\n";
            text << "      \"symbol\": " << quote(instance.symbolId) << ",\n";
            text << "      \"component\": {\n";
            text << "        \"archetype\": " << quote(archetypeFor(instance.symbolId)) << ",\n";
            text << "        \"family\": " << nullableQuote(instance.family) << ",\n";
            text << "        \"manufacturerPart\": " << nullableQuote(instance.manufacturerPart) << ",\n";
            text << "        \"datasheetStatus\": \"generic\"\n";
            text << "      },\n";
            text << "      \"x\": " << instance.position.x << ",\n";
            text << "      \"y\": " << instance.position.y << ",\n";
            text << "      \"rotation\": " << instance.rotation << ",\n";
            if (isRailBus(instance.symbolId))
                text << "      \"length\": " << instance.busLength << ",\n";
            text << "      \"value\": " << quote(instance.value) << ",\n";
            text << "      \"frequency\": " << quote(instance.frequency) << ",\n";
            text << "      \"busName\": " << quote(instance.busName) << ",\n";
            text << "      \"parameters\": " << parametersJsonFor(instance) << ",\n";
            text << "      \"pins\": {\n";
            for (size_t p = 0; p < symbol.pins.size(); ++p)
            {
                if (p != 0) text << ",\n";
                text << "        " << quote(symbol.pins[p].name) << ": "
                     << quote(netFor({ (int)i, (int)p }, netNames));
            }
            text << "\n      }\n";
            text << "    }";
        }
        text << "\n  ],\n";
        text << "  \"wires\": [\n";
        for (size_t i = 0; i < wires.size(); ++i)
        {
            const auto& wire = wires[i];
            if (i != 0) text << ",\n";
            text << "    { \"a\": " << quote(nodeLabel(wire.a))
                 << ", \"b\": " << quote(nodeLabel(wire.b)) << " }";
        }
        text << "\n  ],\n";
        text << "  \"junctions\": [\n";
        for (size_t i = 0; i < junctions.size(); ++i)
        {
            const auto& junction = junctions[i];
            if (i != 0) text << ",\n";
            text << "    { \"id\": " << quote("N" + juce::String((int)i + 1))
                 << ", \"x\": " << junction.x
                 << ", \"y\": " << junction.y << " }";
        }
        text << "\n  ],\n";
        text << "  \"groups\": [\n";
        for (size_t i = 0; i < groups.size(); ++i)
        {
            const auto& group = groups[i];
            if (i != 0) text << ",\n";
            text << "    {\n";
            text << "      \"id\": " << quote(group.id) << ",\n";
            text << "      \"name\": " << quote(group.name) << ",\n";
            text << "      \"category\": " << quote(group.category) << ",\n";
            text << "      \"notes\": " << quote(group.notes) << ",\n";
            text << "      \"members\": [";
            bool first = true;
            for (int member : group.memberInstances)
            {
                if (member < 0 || member >= (int)instances.size())
                    continue;
                if (!first) text << ", ";
                first = false;
                text << quote(instances[(size_t)member].refdes);
            }
            text << "]\n";
            text << "    }";
        }
        text << "\n  ],\n";
        text << "  \"probes\": [\n";
        for (size_t i = 0; i < probes.size(); ++i)
        {
            const auto& probe = probes[i];
            if (i != 0) text << ",\n";
            text << "    { \"id\": " << quote(probe.id)
                 << ", \"label\": " << quote(probe.label)
                 << ", \"role\": " << quote(probe.role)
                 << ", \"target\": " << quote(nodeLabel(probe.node))
                 << ", \"net\": " << quote(netForNode(probe.node, netNames))
                 << " }";
        }
        text << "\n  ]\n";
        text << "}\n";
        return text;
    }

    juce::String buildXyceNetlist() const
    {
        const auto netNames = computeNetNames();
        juce::String netlist;
        netlist << "* Djehuti Electronics Lab generated Xyce netlist\n";
        netlist << "* Research prototype output. Circuit JSON remains authoritative.\n\n";

        bool hasGround = false;
        bool hasProbe = false;

        for (size_t i = 0; i < instances.size(); ++i)
        {
            const auto& instance = instances[i];
            const auto symbol = symbolFor(instance.symbolId);
            auto pinNet = [&](const juce::String& pinName) {
                for (size_t p = 0; p < symbol.pins.size(); ++p)
                    if (symbol.pins[p].name == pinName)
                        return netFor({ (int)i, (int)p }, netNames);
                return juce::String("floating");
            };

            if (instance.symbolId == "ground" || instance.symbolId == "ground_bus")
            {
                hasGround = true;
                continue;
            }
            if (instance.symbolId == "power_bus")
            {
                netlist << "* " << instance.refdes << " " << instance.busName << " power bus on net " << pinNet("VBUS") << "\n";
                continue;
            }
            if (instance.symbolId == "power_port")
                continue;
            if (instance.symbolId == "resistor")
            {
                netlist << instance.refdes << " " << pinNet("1") << " " << pinNet("2") << " " << instance.value << "\n";
                hasProbe = true;
            }
            else if (instance.symbolId == "capacitor")
            {
                netlist << instance.refdes << " " << pinNet("1") << " " << pinNet("2") << " " << instance.value << "\n";
            }
            else if (instance.symbolId == "inductor")
            {
                netlist << instance.refdes << " " << pinNet("1") << " " << pinNet("2") << " " << instance.value << "\n";
            }
            else if (instance.symbolId == "diode")
            {
                netlist << instance.refdes << " " << pinNet("A") << " " << pinNet("K") << " " << instance.value << "\n";
                netlist << ".MODEL " << instance.value << " D\n";
                hasProbe = true;
            }
            else if (instance.symbolId == "voltage_source")
            {
                netlist << instance.refdes << " " << pinNet("+") << " " << pinNet("-") << " DC " << instance.value << "\n";
                hasProbe = true;
            }
            else if (instance.symbolId == "battery")
            {
                netlist << instance.refdes << " " << pinNet("+") << " " << pinNet("-") << " DC " << instance.value << "\n";
                hasProbe = true;
            }
            else if (instance.symbolId == "ac_voltage_source")
            {
                netlist << instance.refdes << " " << pinNet("+") << " " << pinNet("-") << " AC " << instance.value
                        << " SIN(0 " << instance.value << " " << instance.frequency << ")\n";
                hasProbe = true;
            }
            else if (instance.symbolId == "signal_source")
            {
                netlist << instance.refdes << " " << pinNet("OUT") << " " << pinNet("REF") << " AC " << instance.value
                        << " SIN(0 " << instance.value << " " << instance.frequency << ")\n";
                hasProbe = true;
            }
            else
            {
                netlist << "* " << instance.refdes << " (" << instance.symbolId << ") not lowered to Xyce yet\n";
            }
        }

        netlist << "\n.OP\n";
        netlist << ".PRINT DC";
        const auto printableNets = printableNetNames(netNames);
        if (printableNets.empty())
        {
            netlist << " V(0)";
        }
        else
        {
            for (const auto& net : printableNets)
                netlist << " V(" << net << ")";
        }
        netlist << "\n.END\n";

        if (!hasGround)
            netlist = "* WARNING: no ground symbol found; generated netlist may not solve.\n" + netlist;
        if (!hasProbe)
            netlist = "* WARNING: no lowered source/resistor found; this netlist is mostly structural.\n" + netlist;

        return netlist;
    }

    juce::String buildErcReport() const
    {
        const auto netNames = computeNetNames();
        const auto totalPins = pinCount();
        const auto totalNodes = totalPins + (int)junctions.size();
        std::vector<int> nodeDegree((size_t)std::max(0, totalNodes), 0);

        for (const auto& wire : wires)
        {
            const auto a = nodeOrdinal(wire.a);
            const auto b = nodeOrdinal(wire.b);
            if (a >= 0 && a < totalNodes) ++nodeDegree[(size_t)a];
            if (b >= 0 && b < totalNodes) ++nodeDegree[(size_t)b];
        }

        int errors = 0;
        int warnings = 0;
        int infos = 0;
        juce::String findings;

        auto addFinding = [&](const juce::String& severity, const juce::String& message) {
            if (severity == "ERROR") ++errors;
            else if (severity == "WARN") ++warnings;
            else ++infos;

            findings << "- [" << severity << "] " << message << "\n";
        };

        auto pinNet = [&](int instanceIndex, const juce::String& pinName) {
            const auto symbol = symbolFor(instances[(size_t)instanceIndex].symbolId);
            for (int p = 0; p < (int)symbol.pins.size(); ++p)
                if (symbol.pins[(size_t)p].name == pinName)
                    return netFor({ instanceIndex, p }, netNames);
            return juce::String("floating");
        };

        auto unsupportedForXyce = [](const juce::String& symbolId) {
            return symbolId == "opamp_741" || symbolId == "npn" || symbolId == "pnp" || symbolId == "logic_not";
        };

        if (instances.empty())
            addFinding("ERROR", "No components are placed on the schematic.");

        bool hasGround = false;
        bool hasLoweredPrimitive = false;
        for (const auto& instance : instances)
        {
            if (instance.symbolId == "ground" || instance.symbolId == "ground_bus")
                hasGround = true;
            if (instance.symbolId == "resistor"
                || instance.symbolId == "capacitor"
                || instance.symbolId == "inductor"
                || instance.symbolId == "diode"
                || instance.symbolId == "voltage_source"
                || instance.symbolId == "battery"
                || instance.symbolId == "ac_voltage_source"
                || instance.symbolId == "signal_source")
                hasLoweredPrimitive = true;
        }

        if (!hasGround)
            addFinding("ERROR", "No ground reference is present. Add a ground or ground bus before running solver-backed analysis.");
        if (!hasLoweredPrimitive && !instances.empty())
            addFinding("WARN", "No currently lowered Xyce primitive is present. The generated netlist will be mostly structural.");

        std::set<juce::String> refdesSeen;
        for (int i = 0; i < (int)instances.size(); ++i)
        {
            const auto& instance = instances[(size_t)i];
            const auto symbol = symbolFor(instance.symbolId);

            if (refdesSeen.count(instance.refdes) != 0)
                addFinding("ERROR", "Duplicate reference designator found: " + instance.refdes + ".");
            refdesSeen.insert(instance.refdes);

            if (instance.value.trim().isEmpty()
                && instance.symbolId != "ground"
                && instance.symbolId != "ground_bus"
                && instance.symbolId != "power_bus"
                && instance.symbolId != "power_port")
                addFinding("WARN", instance.refdes + " has no value or model text.");

            if (instance.symbolId == "power_port" && instance.busName.trim().isEmpty())
                addFinding("ERROR", instance.refdes + " is a supply port with no net name.");

            if (unsupportedForXyce(instance.symbolId))
                addFinding("INFO", instance.refdes + " (" + instance.symbolId + ") is captured in the model but not lowered to Xyce yet.");

            for (int p = 0; p < (int)symbol.pins.size(); ++p)
            {
                const auto ordinal = pinOrdinal({ i, p });
                if (ordinal >= 0 && ordinal < (int)nodeDegree.size() && nodeDegree[(size_t)ordinal] == 0)
                    addFinding("WARN", instance.refdes + "." + symbol.pins[(size_t)p].name + " is not wired.");
            }

            if (symbol.pins.size() == 2
                && (instance.symbolId == "resistor"
                    || instance.symbolId == "capacitor"
                    || instance.symbolId == "inductor"
                    || instance.symbolId == "diode"))
            {
                const auto a = netFor({ i, 0 }, netNames);
                const auto b = netFor({ i, 1 }, netNames);
                if (a == b)
                    addFinding("WARN", instance.refdes + " has both pins on " + a + ".");
            }

            if ((instance.symbolId == "voltage_source" || instance.symbolId == "battery" || instance.symbolId == "ac_voltage_source")
                && pinNet(i, "+") == pinNet(i, "-"))
                addFinding("ERROR", instance.refdes + " has positive and negative terminals on the same net.");
            if (instance.symbolId == "signal_source" && pinNet(i, "OUT") == pinNet(i, "REF"))
                addFinding("ERROR", instance.refdes + " has OUT and REF on the same net.");
        }

        for (const auto& wire : wires)
        {
            if (sameNode(wire.a, wire.b))
                addFinding("WARN", "A wire loops back to " + nodeLabel(wire.a) + ".");
        }

        // Every named supply net needs something that actually sets its voltage.
        std::set<juce::String> supplyNets;
        std::set<juce::String> drivenNets;
        for (int i = 0; i < (int)instances.size(); ++i)
        {
            const auto& instance = instances[(size_t)i];
            if (instance.symbolId == "power_port" && instance.busName.trim().isNotEmpty())
                supplyNets.insert(pinNet(i, "1"));
            if (instance.symbolId == "voltage_source" || instance.symbolId == "battery")
            {
                drivenNets.insert(pinNet(i, "+"));
                drivenNets.insert(pinNet(i, "-"));
            }
        }
        for (const auto& net : supplyNets)
            if (drivenNets.count(net) == 0)
                addFinding("ERROR", "Supply net " + net + " has ports but no voltage source or battery driving it.");

        if (probes.empty() && !instances.empty())
            addFinding("INFO", "No lab probes are assigned yet, so instruments do not have schematic targets.");

        juce::String report;
        report << "# Electrical Rule Check\n\n";
        report << "- Components: " << (int)instances.size() << "\n";
        report << "- Wires: " << (int)wires.size() << "\n";
        report << "- Junctions: " << (int)junctions.size() << "\n";
        report << "- Probes: " << (int)probes.size() << "\n";
        report << "- Errors: " << errors << "\n";
        report << "- Warnings: " << warnings << "\n";
        report << "- Info: " << infos << "\n\n";

        if (findings.isEmpty())
            report << "No ERC findings.\n";
        else
            report << "## Findings\n\n" << findings;

        return report;
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff0e141a));

        g.saveState();
        g.addTransform(juce::AffineTransform::scale(canvasZoom).translated(viewOffset.x, viewOffset.y));
        drawGrid(g);
        drawWires(g);
        drawGroups(g);
        drawInstances(g);
        drawProbes(g);
        drawPendingWire(g);
        drawSelectionBox(g);
        g.restoreState();

        g.setColour(juce::Colour(0xff93a7b0));
        g.setFont(juce::Font(13.0f));
        const auto stampOn = getStampPlacementEnabled != nullptr && getStampPlacementEnabled();
        const auto modeText = snapEnabled ? juce::String("Snap on") : juce::String("Snap off");
        const auto zoomText = juce::String((int)std::round(canvasZoom * 100.0f)) + "%";
        const auto hintText = juce::String(stampOn ? "Stamp mode: click empty canvas to place selected symbols, drag selected parts to move, R rotates."
                                                   : "Drag pins/wires/rails to connect. Select a rail and drag its end handles to resize. Delete removes selected parts.")
            + "  " + modeText + "  Zoom " + zoomText;
        g.drawText(hintText,
                   getLocalBounds().reduced(12).removeFromBottom(24),
                   juce::Justification::centredLeft);

        if (dragHover)
            g.drawText(dragMessage, getLocalBounds().reduced(12).removeFromBottom(24), juce::Justification::centredRight);
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        grabKeyboardFocus();
        const auto modelPosition = viewToCanvas(event.position);
        const auto p = snapPoint(modelPosition);
        if (event.mods.isMiddleButtonDown())
        {
            beginPan(event.position);
            return;
        }

        if (event.mods.isRightButtonDown())
        {
            if (const auto groupIndex = hitTestGroup(modelPosition); groupIndex >= 0)
                selectGroup(groupIndex);
            showContextMenu(modelPosition);
            return;
        }

        if (beginRailResize(modelPosition))
        {
            repaint();
            return;
        }

        if (auto rail = hitTestRailBus(modelPosition); rail >= 0)
        {
            beginWireDrag(createRailTap(rail, p), modelPosition);
            repaint();
            return;
        }

        if (auto pin = hitTestPin(modelPosition); pin.instanceIndex >= 0)
        {
            beginWireDrag(WireNode::forPin(pin), modelPosition);
            repaint();
            return;
        }

        if (auto junction = hitTestJunction(modelPosition); junction >= 0)
        {
            beginWireDrag(WireNode::forJunction(junction), modelPosition);
            repaint();
            return;
        }

        if (auto wireIndex = hitTestWire(modelPosition); wireIndex >= 0)
        {
            beginWireDrag(createJunctionOnWire(wireIndex, p), modelPosition);
            repaint();
            return;
        }

        if (const auto groupIndex = hitTestGroup(modelPosition); groupIndex >= 0)
        {
            selectGroup(groupIndex);
            draggingInstance = true;
            dragStartMouse = modelPosition;
            dragStartPosition = groupBounds(groups[(size_t)groupIndex]).getPosition();
            captureSelectedDragStarts();
            notifySelection();
            repaint();
            return;
        }

        if (const auto instanceIndex = hitTestInstance(modelPosition); instanceIndex >= 0)
        {
            selectedInstance = instanceIndex;
            if (!event.mods.isShiftDown() && !isInstanceSelected(instanceIndex))
                selectedInstances.clear();
            if (event.mods.isShiftDown())
                toggleInstanceSelection(instanceIndex);
            else
                selectedInstances.addIfNotAlreadyThere(instanceIndex);
            draggingInstance = true;
            dragStartMouse = modelPosition;
            dragStartPosition = instances[(size_t)selectedInstance].position;
            captureSelectedDragStarts();
            notifySelection();
            repaint();
            return;
        }

        if (getStampPlacementEnabled != nullptr && getStampPlacementEnabled())
        {
            const auto selected = getSelectedSymbolId != nullptr ? getSelectedSymbolId() : juce::String("resistor");
            placeSymbol(selected, p);
            repaint();
            return;
        }

        beginSelectionBox(modelPosition);
    }

    void mouseDoubleClick(const juce::MouseEvent& event) override
    {
        const auto modelPosition = viewToCanvas(event.position);
        if (const auto instanceIndex = hitTestInstance(modelPosition); instanceIndex >= 0)
        {
            selectedInstance = instanceIndex;
            notifySelection();

            const auto& instance = instances[(size_t)instanceIndex];
            if (isInstrumentNode(instance.symbolId) && onInstrumentOpen)
            {
                onInstrumentOpen(instance.refdes, instance.symbolId);
                if (onStatus) onStatus("Opened instrument panel for " + instance.refdes + ".");
            }
            repaint();
        }
    }

    void resized() override {}

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (panning)
        {
            viewOffset = panStartOffset + (event.position - panStartMouse);
            repaint();
            return;
        }

        const auto modelPosition = viewToCanvas(event.position);
        if (resizingRail)
        {
            resizeRail(modelPosition);
            repaint();
            return;
        }

        if (wireDragging)
        {
            wireDragPosition = modelPosition;
            repaint();
            return;
        }

        if (selectingBox)
        {
            selectionBoxEnd = modelPosition;
            updateSelectionFromBox();
            repaint();
            return;
        }

        if (!draggingInstance || selectedInstance < 0 || selectedInstance >= (int)instances.size())
            return;

        const auto delta = snapPoint(dragStartPosition + (modelPosition - dragStartMouse)) - dragStartPosition;
        if (selectedInstances.size() > 1)
            moveSelectedInstances(delta);
        else
            instances[(size_t)selectedInstance].position = snapPoint(dragStartPosition + (modelPosition - dragStartMouse));
        notifySelection();
        repaint();
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        const auto modelPosition = viewToCanvas(event.position);
        if (wireDragging)
        {
            finishWireDrag(modelPosition);
            repaint();
        }

        draggingInstance = false;
        selectingBox = false;
        resizingRail = false;
        resizingRailInstance = -1;
        panning = false;
    }

    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
    {
        if (std::abs(wheel.deltaY) <= 0.0001f)
            return;

        const auto before = viewToCanvas(event.position);
        const auto factor = wheel.deltaY > 0.0f ? 1.1f : (1.0f / 1.1f);
        canvasZoom = std::clamp(canvasZoom * factor, 0.5f, 2.5f);
        viewOffset += event.position - canvasToView(before);
        repaint();
    }

    void showContextMenu(juce::Point<float> modelPosition)
    {
        juce::PopupMenu menu;
        menu.addItem(1, "Auto Layout Diagram");
        menu.addItem(2, "Auto Layout Selection", !selectedInstances.isEmpty());
        menu.addItem(5, "Create Group from Selection", selectedInstances.size() >= 2);
        menu.addItem(6, "Edit Group Metadata...", selectedGroup >= 0 && selectedGroup < (int)groups.size());
        menu.addItem(7, "Ungroup", selectedGroup >= 0 && selectedGroup < (int)groups.size());
        menu.addSeparator();
        menu.addItem(3, "Disconnect Here");
        menu.addItem(4, "Release Probe Here");

        menu.showMenuAsync(juce::PopupMenu::Options(), [this, modelPosition](int result) {
            if (result == 1)
                autoLayoutFromTool();
            else if (result == 2)
                autoLayoutSelectionFromTool();
            else if (result == 5)
                createGroupFromSelection();
            else if (result == 6)
                editSelectedGroupMetadata();
            else if (result == 7)
                ungroupSelectedGroup();
            else if (result == 3)
                disconnectAt(modelPosition);
            else if (result == 4)
                releaseProbeAt(modelPosition);
            repaint();
        });
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey)
        {
            wireDragging = false;
            repaint();
            return true;
        }
        if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        {
            deleteSelected();
            return true;
        }
        if (key.getTextCharacter() == 'r' || key.getTextCharacter() == 'R')
        {
            rotateSelected();
            return true;
        }
        return false;
    }

    bool isInterestedInDragSource(const SourceDetails& details) override
    {
        const auto description = details.description.toString();
        return description.startsWith("symbol:") || description.startsWith("probe:");
    }

    void itemDragEnter(const SourceDetails& details) override
    {
        dragHover = true;
        dragMessage = details.description.toString().startsWith("probe:")
            ? "Drop probe on a pin or wire"
            : "Drop symbol on schematic";
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        repaint();
    }

    void itemDragExit(const SourceDetails&) override
    {
        dragHover = false;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        repaint();
    }

    void itemDropped(const SourceDetails& details) override
    {
        dragHover = false;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        const auto description = details.description.toString();
        const auto modelPosition = viewToCanvas(details.localPosition.toFloat());
        const auto p = snapPoint(modelPosition);
        if (description.startsWith("probe:"))
        {
            const auto node = nodeAt(modelPosition, p);
            if (!node.isValid())
            {
                if (onStatus) onStatus("Probe drop needs a pin, junction, or wire.");
                repaint();
                return;
            }

            assignProbe(description.fromFirstOccurrenceOf("probe:", false, false), node);
            repaint();
            return;
        }

        if (!description.startsWith("symbol:"))
            return;

        const auto symbolId = description.fromFirstOccurrenceOf("symbol:", false, false);
        placeSymbol(symbolId, p);
        repaint();
    }

    juce::String placeSymbolFromTool(const juce::String& symbolId,
                                     float x,
                                     float y,
                                     const juce::String& value,
                                     const juce::String& frequency,
                                     const juce::String& busName)
    {
        const auto requestedSymbol = symbolId.trim();
        if (!schematic::isSupportedSymbol(requestedSymbol))
            return "{ \"ok\": false, \"error\": \"Unsupported symbolId; no substitute was placed.\", \"requestedSymbolId\": "
                + quote(requestedSymbol) + " }";

        const auto before = instances.size();
        placeSymbol(requestedSymbol, snapPoint({ x, y }));
        if (instances.size() == before)
            return "{ \"ok\": false, \"error\": \"Could not place symbol.\" }";

        auto& instance = instances.back();
        if (value.trim().isNotEmpty())
            instance.value = value.trim();
        if (frequency.trim().isNotEmpty())
            instance.frequency = frequency.trim();
        if (busName.trim().isNotEmpty())
            instance.busName = busName.trim();

        notifySelection();
        repaint();

        juce::String result;
        result << "{\n";
        result << "  \"ok\": true,\n";
        result << "  \"tool\": \"schematic_place_symbol\",\n";
        result << "  \"displayTool\": \"schematic.place_symbol\",\n";
        result << "  \"refdes\": " << quote(instance.refdes) << ",\n";
        result << "  \"symbolId\": " << quote(instance.symbolId) << ",\n";
        result << "  \"x\": " << instance.position.x << ",\n";
        result << "  \"y\": " << instance.position.y << "\n";
        result << "}";
        return result;
    }

    juce::String setComponentPropertiesFromTool(const juce::String& refdes,
                                                const juce::String& value,
                                                const juce::String& frequency,
                                                const juce::String& busName,
                                                const juce::String& family,
                                                const juce::String& manufacturerPart)
    {
        const auto index = instanceIndexForRefdes(refdes.trim());
        if (index < 0)
            return toolFailure("schematic_set_component_properties", "No placed instance has reference designator " + refdes.trim() + ".");

        auto& instance = instances[(size_t)index];
        auto applyIfNotVoid = [](const juce::String& incoming, juce::String& target) {
            if (incoming.isNotEmpty())
                target = incoming.trim();
        };

        applyIfNotVoid(value, instance.value);
        applyIfNotVoid(frequency, instance.frequency);
        applyIfNotVoid(busName, instance.busName);
        applyIfNotVoid(family, instance.family);
        applyIfNotVoid(manufacturerPart, instance.manufacturerPart);

        selectedInstance = index;
        selectedInstances.clear();
        selectedInstances.add(index);
        selectedGroup = -1;
        notifySelection();
        forceDeferredRepaint();

        juce::String result;
        result << "{\n";
        result << "  \"ok\": true,\n";
        result << "  \"tool\": \"schematic_set_component_properties\",\n";
        result << "  \"displayTool\": \"schematic.set_component_properties\",\n";
        result << "  \"refdes\": " << quote(instance.refdes) << ",\n";
        result << "  \"symbolId\": " << quote(instance.symbolId) << ",\n";
        result << "  \"value\": " << quote(instance.value) << ",\n";
        result << "  \"frequency\": " << quote(instance.frequency) << ",\n";
        result << "  \"busName\": " << quote(instance.busName) << ",\n";
        result << "  \"family\": " << quote(instance.family) << ",\n";
        result << "  \"manufacturerPart\": " << quote(instance.manufacturerPart) << "\n";
        result << "}";
        return result;
    }

    juce::String createRlcHighPassFilterFromTool(double cutoffHz, double impedanceOhms)
    {
        if (cutoffHz <= 0.0 || impedanceOhms <= 0.0)
            return toolFailure("filter_design_high_pass", "cutoffHz and impedanceOhms must be positive.");

        const auto design = makeRlcHighPassDesign(cutoffHz, impedanceOhms);
        clearModel();

        auto place = [this](const juce::String& symbolId,
                            const juce::String& refdes,
                            juce::Point<float> position,
                            const juce::String& value,
                            const juce::String& frequency = {},
                            int rotation = 0) {
            placeSymbol(symbolId, position);
            auto& instance = instances.back();
            instance.refdes = refdes;
            instance.value = value;
            instance.frequency = frequency;
            instance.rotation = rotation;
            return refdes;
        };

        place("ac_voltage_source", "VIN1", { 120.0f, 288.0f }, "1", numberText(design.cutoffHz, 3));
        place("resistor", "RS1", { 312.0f, 216.0f }, numberText(design.sourceOhms, 3));
        place("capacitor", "C1", { 504.0f, 216.0f }, spiceCapacitance(design.capacitanceFarads));
        place("inductor", "L1", { 648.0f, 336.0f }, spiceInductance(design.inductanceHenries), {}, 90);
        place("resistor", "RL1", { 816.0f, 336.0f }, numberText(design.loadOhms, 3), {}, 90);
        place("ground", "GND_SRC", { 120.0f, 384.0f }, "0", {}, 0);
        place("ground", "GND_L", { 648.0f, 480.0f }, "0", {}, 0);
        place("ground", "GND_LOAD", { 816.0f, 480.0f }, "0", {}, 0);
        place("ground", "GND_SCOPE", { 1032.0f, 360.0f }, "0", {}, 0);
        place("oscilloscope_2ch", "SCOPE1", { 1032.0f, 216.0f }, "2ch", {}, 0);

        connectNodesFromTool("VIN1.-", "GND_SRC.0");
        connectNodesFromTool("VIN1.+", "RS1.1");
        connectNodesFromTool("RS1.2", "C1.1");
        connectNodesFromTool("C1.2", "L1.1");
        connectNodesFromTool("C1.2", "RL1.1");
        connectNodesFromTool("C1.2", "SCOPE1.CH1");
        connectNodesFromTool("VIN1.+", "SCOPE1.CH2");
        connectNodesFromTool("L1.2", "GND_L.0");
        connectNodesFromTool("RL1.2", "GND_LOAD.0");
        connectNodesFromTool("SCOPE1.REF", "GND_SCOPE.0");

        selectedInstance = instanceIndexForRefdes("C1");
        notifySelection();
        forceDeferredRepaint();

        juce::String result;
        result << "{\n";
        result << "  \"ok\": true,\n";
        result << "  \"tool\": \"filter_design_high_pass\",\n";
        result << "  \"displayTool\": \"filter.design_high_pass\",\n";
        result << "  \"topology\": \"source_8ohm_series_cap_shunt_inductor_load_8ohm\",\n";
        result << "  \"cutoffHz\": " << numberText(design.cutoffHz, 6) << ",\n";
        result << "  \"impedanceOhms\": " << numberText(design.impedanceOhms, 6) << ",\n";
        result << "  \"sourceOhms\": " << numberText(design.sourceOhms, 6) << ",\n";
        result << "  \"loadOhms\": " << numberText(design.loadOhms, 6) << ",\n";
        result << "  \"capacitanceFarads\": " << numberText(design.capacitanceFarads, 12) << ",\n";
        result << "  \"inductanceHenries\": " << numberText(design.inductanceHenries, 12) << ",\n";
        result << "  \"capacitanceLabel\": " << quote(humanCapacitance(design.capacitanceFarads)) << ",\n";
        result << "  \"inductanceLabel\": " << quote(humanInductance(design.inductanceHenries)) << "\n";
        result << "}";
        return result;
    }

    juce::String createPushPullAmplifierFromTool()
    {
        clearModel();

        auto place = [this](const juce::String& symbolId,
                            const juce::String& refdes,
                            juce::Point<float> position,
                            const juce::String& value,
                            const juce::String& frequency = {},
                            const juce::String& busName = {},
                            int rotation = 0) {
            placeSymbol(symbolId, position);
            auto& instance = instances.back();
            instance.refdes = refdes;
            instance.value = value;
            instance.frequency = frequency;
            instance.busName = busName;
            instance.rotation = rotation;
            return refdes;
        };

        auto junction = [this](juce::Point<float> p) {
            junctions.push_back(p);
            return "N" + juce::String((int)junctions.size());
        };

        // Class AB complementary emitter follower, drawn the textbook way:
        // NPN above PNP with the output node between their emitters, the
        // diode bias string between the bases, supplies as named ports at
        // the pins that need them, and ground symbols at each ground pin.
        // Rotation 90 turns a horizontal two-pin part vertical with pin 1
        // (anode for diodes) on top.

        // Input: AC source coupled through C1 into the middle of the bias string.
        place("ac_voltage_source", "V1", { 168.0f, 384.0f }, "0.25", "1k", "Input");
        place("ground", "GND1", { 168.0f, 456.0f }, "0");
        place("capacitor", "C1", { 312.0f, 336.0f }, "10u");

        // Bias string: R1 from +12V, D1 and D2, R2 to -12V.
        place("power_port", "PWR1", { 408.0f, 0.0f }, {}, {}, "+12V");
        place("resistor", "R1", { 408.0f, 72.0f }, "2.2k", {}, {}, 90);
        place("diode", "D1", { 408.0f, 192.0f }, "1N4148", {}, {}, 90);
        place("diode", "D2", { 408.0f, 480.0f }, "1N4148", {}, {}, 90);
        place("resistor", "R2", { 408.0f, 600.0f }, "2.2k", {}, {}, 90);
        place("power_port", "PWR2", { 408.0f, 672.0f }, {}, {}, "-12V", 180);

        // Output pair with emitter ballast resistors.
        place("power_port", "PWR3", { 576.0f, 48.0f }, {}, {}, "+12V");
        place("npn", "Q1", { 552.0f, 144.0f }, "generic_npn");
        place("resistor", "R3", { 576.0f, 264.0f }, "0.47", {}, {}, 90);
        place("resistor", "R4", { 576.0f, 408.0f }, "0.47", {}, {}, 90);
        place("pnp", "Q2", { 552.0f, 528.0f }, "generic_pnp");
        place("power_port", "PWR4", { 576.0f, 624.0f }, {}, {}, "-12V", 180);

        // Load.
        place("resistor", "RL1", { 720.0f, 384.0f }, "8", {}, {}, 90);
        place("ground", "GND2", { 720.0f, 456.0f }, "0");

        // Supplies: stacked sources around a grounded midpoint.
        place("voltage_source", "V2", { 168.0f, 600.0f }, "12");
        place("power_port", "PWR5", { 168.0f, 528.0f }, {}, {}, "+12V");
        place("voltage_source", "V3", { 168.0f, 744.0f }, "12");
        place("power_port", "PWR6", { 168.0f, 816.0f }, {}, {}, "-12V", 180);
        place("ground", "GND3", { 240.0f, 672.0f }, "0");

        // Scope: CH1 on the input drive, CH2 on the output, REF to ground.
        place("oscilloscope_2ch", "SCOPE1", { 912.0f, 360.0f }, "2ch");
        place("ground", "GND4", { 912.0f, 456.0f }, "0");

        const auto inputNode = junction({ 408.0f, 336.0f });
        const auto outputNode = junction({ 576.0f, 336.0f });
        const auto supplyMid = junction({ 168.0f, 672.0f });

        connectNodesFromTool("V1.-", "GND1");
        connectNodesFromTool("V1.+", "C1.1");
        connectNodesFromTool("C1.2", inputNode);

        connectNodesFromTool("PWR1", "R1.1");
        connectNodesFromTool("R1.2", "D1.A");
        connectNodesFromTool("D1.K", inputNode);
        connectNodesFromTool(inputNode, "D2.A");
        connectNodesFromTool("D2.K", "R2.1");
        connectNodesFromTool("R2.2", "PWR2");

        connectNodesFromTool("D1.A", "Q1.B");
        connectNodesFromTool("D2.K", "Q2.B");
        connectNodesFromTool("PWR3", "Q1.C");
        connectNodesFromTool("Q1.E", "R3.1");
        connectNodesFromTool("R3.2", outputNode);
        connectNodesFromTool(outputNode, "R4.1");
        connectNodesFromTool("R4.2", "Q2.E");
        connectNodesFromTool("Q2.C", "PWR4");

        connectNodesFromTool(outputNode, "RL1.1");
        connectNodesFromTool("RL1.2", "GND2");

        connectNodesFromTool("PWR5", "V2.+");
        connectNodesFromTool("V2.-", supplyMid);
        connectNodesFromTool(supplyMid, "V3.+");
        connectNodesFromTool(supplyMid, "GND3");
        connectNodesFromTool("V3.-", "PWR6");

        connectNodesFromTool("SCOPE1.CH1", "V1.+");
        connectNodesFromTool("SCOPE1.CH2", "RL1.1");
        connectNodesFromTool("SCOPE1.REF", "GND4");

        selectedInstance = instanceIndexForRefdes("Q1");
        selectedInstances.clear();
        if (selectedInstance >= 0)
            selectedInstances.add(selectedInstance);
        notifySelection();
        forceDeferredRepaint();

        juce::String result;
        result << "{\n";
        result << "  \"ok\": true,\n";
        result << "  \"tool\": \"amplifier_design_push_pull\",\n";
        result << "  \"displayTool\": \"amplifier.design_push_pull\",\n";
        result << "  \"topology\": \"class_ab_complementary_emitter_follower\",\n";
        result << "  \"componentCount\": " << (int)instances.size() << ",\n";
        result << "  \"wireCount\": " << (int)wires.size() << ",\n";
        result << "  \"loadOhms\": 8,\n";
        result << "  \"inputFrequencyHz\": 1000,\n";
        result << "  \"note\": \"Transistor symbols are structurally represented; full BJT SPICE lowering is still a future solver capability.\"\n";
        result << "}";
        return result;
    }

    juce::String autoLayoutFromTool()
    {
        if (instances.empty())
            return "{ \"ok\": false, \"error\": \"No schematic components to lay out.\" }";
        return autoLayoutInstances({});
    }

    juce::String autoLayoutSelectionFromTool()
    {
        if (selectedInstances.isEmpty())
            return "{ \"ok\": false, \"error\": \"No selected components to lay out.\" }";
        juce::Array<int> selected = selectedInstances;
        return autoLayoutInstances(selected);
    }

    juce::String autoLayoutInstances(juce::Array<int> scope)
    {
        if (instances.empty())
            return "{ \"ok\": false, \"error\": \"No schematic components to lay out.\" }";

        if (scope.isEmpty())
        {
            for (int i = 0; i < (int)instances.size(); ++i)
                scope.add(i);
        }

        auto roleColumn = [this](const Instance& instance) {
            const auto id = instance.symbolId;
            if (id == "power_bus") return 1;
            if (id == "voltage_source" || id == "ac_voltage_source" || id == "signal_source"
                || id == "current_source" || id == "ac_current_source" || id == "battery"
                || id.startsWith("connector"))
                return 0;
            if (id == "ground" || id == "ground_bus") return 2;
            if (isInstrumentNode(id)) return 5;
            if (id == "resistor" || id == "capacitor" || id == "inductor" || id == "diode"
                || id == "zener_diode" || id == "led" || id == "schottky_diode"
                || id == "potentiometer" || id == "fuse" || id.startsWith("switch"))
                return 2;
            if (id == "npn" || id == "pnp" || id == "nmos" || id == "pmos" || id == "njfet"
                || id == "pjfet" || id == "opamp_741" || id.startsWith("logic")
                || id == "vcvs" || id == "vccs" || id == "ccvs" || id == "cccs")
                return 3;
            if (id == "transformer" || id == "coupled_inductor" || id.startsWith("relay"))
                return 3;
            return 4;
        };

        std::array<int, 6> rowCounts {};
        for (int index : scope)
        {
            if (index < 0 || index >= (int)instances.size())
                continue;
            auto& instance = instances[(size_t)index];
            const auto column = std::clamp(roleColumn(instance), 0, 5);
            const auto row = rowCounts[(size_t)column]++;
            float x = 144.0f + (float)column * 168.0f;
            float y = 144.0f + (float)row * 120.0f;
            if (instance.symbolId == "power_bus") y = 72.0f;
            if (instance.symbolId == "ground" || instance.symbolId == "ground_bus") y += 96.0f;
            if (isInstrumentNode(instance.symbolId)) y = 144.0f + (float)row * 144.0f;
            instance.position = snapPoint({ x, y });
        }

        selectedInstance = selectedInstances.isEmpty() ? (instances.empty() ? -1 : 0) : selectedInstances.getLast();
        if (selectedInstances.isEmpty() && selectedInstance >= 0)
            selectedInstances.add(selectedInstance);
        notifySelection();
        forceDeferredRepaint();

        juce::String result;
        result << "{\n";
        result << "  \"ok\": true,\n";
        result << "  \"tool\": \"schematic_auto_layout\",\n";
        result << "  \"displayTool\": \"schematic.auto_layout\",\n";
        result << "  \"scope\": " << quote(scope.size() == (int)instances.size() ? "diagram" : "selection") << ",\n";
        result << "  \"componentCount\": " << scope.size() << ",\n";
        result << "  \"style\": \"left_to_right_standard_grid\"\n";
        result << "}";
        if (onStatus) onStatus("Auto-laid out " + juce::String(scope.size()) + " schematic component(s).");
        return result;
    }

    juce::String connectNodesFromTool(const juce::String& firstLabel, const juce::String& secondLabel)
    {
        WireNode first;
        WireNode second;
        juce::String error;
        if (!nodeFromLabel(firstLabel, first, error))
            return toolFailure("schematic_connect", error);
        if (!nodeFromLabel(secondLabel, second, error))
            return toolFailure("schematic_connect", error);
        if (sameNode(first, second))
            return toolFailure("schematic_connect", "Cannot connect a node to itself: " + nodeLabel(first) + ".");

        const auto alreadyConnected = std::any_of(wires.begin(), wires.end(), [&](const Wire& wire) {
            return (sameNode(wire.a, first) && sameNode(wire.b, second))
                || (sameNode(wire.a, second) && sameNode(wire.b, first));
        });
        if (!alreadyConnected)
            wires.push_back({ first, second });

        const auto firstResolved = nodeLabel(first);
        const auto secondResolved = nodeLabel(second);
        if (onStatus)
            onStatus(alreadyConnected
                ? "Connection already exists: " + firstResolved + " to " + secondResolved + "."
                : "Connected " + firstResolved + " to " + secondResolved + ".");
        forceDeferredRepaint();

        juce::String result;
        result << "{\n";
        result << "  \"ok\": true,\n";
        result << "  \"tool\": \"schematic_connect\",\n";
        result << "  \"displayTool\": \"schematic.connect\",\n";
        result << "  \"alreadyConnected\": " << (alreadyConnected ? "true" : "false") << ",\n";
        result << "  \"a\": " << quote(firstResolved) << ",\n";
        result << "  \"b\": " << quote(secondResolved) << ",\n";
        result << "  \"wireCount\": " << (int)wires.size() << "\n";
        result << "}";
        return result;
    }

    juce::String openInstrumentFromTool(const juce::String& refdes)
    {
        const auto index = instanceIndexForRefdes(refdes);
        if (index < 0)
            return toolFailure("instrument_open_panel", "No placed instance has reference designator " + refdes + ".");

        const auto& instance = instances[(size_t)index];
        if (!isInstrumentNode(instance.symbolId))
            return toolFailure("instrument_open_panel", instance.refdes + " is not an instrument node.");
        if (!onInstrumentOpen)
            return toolFailure("instrument_open_panel", "Instrument window host is unavailable.");

        onInstrumentOpen(instance.refdes, instance.symbolId);
        if (onStatus)
            onStatus("Opened instrument panel for " + instance.refdes + ".");

        juce::String result;
        result << "{\n";
        result << "  \"ok\": true,\n";
        result << "  \"tool\": \"instrument_open_panel\",\n";
        result << "  \"displayTool\": \"instrument.open_panel\",\n";
        result << "  \"refdes\": " << quote(instance.refdes) << ",\n";
        result << "  \"symbolId\": " << quote(instance.symbolId) << "\n";
        result << "}";
        return result;
    }

private:
    using PinDef = schematic::PinDef;
    using SymbolDef = schematic::SymbolDef;

    struct Instance
    {
        juce::String symbolId;
        juce::String refdes;
        juce::String value;
        juce::String frequency;
        juce::String busName;
        juce::String family;
        juce::String manufacturerPart;
        juce::Point<float> position;
        int rotation = 0;
        float busLength = 420.0f;
    };

    struct PinRef
    {
        int instanceIndex = -1;
        int pinIndex = -1;
    };

    struct WireNode
    {
        PinRef pin;
        int junctionIndex = -1;

        static WireNode forPin(PinRef pinRef)
        {
            WireNode node;
            node.pin = pinRef;
            return node;
        }

        static WireNode forJunction(int index)
        {
            WireNode node;
            node.junctionIndex = index;
            return node;
        }

        bool isPin() const { return pin.instanceIndex >= 0 && pin.pinIndex >= 0; }
        bool isJunction() const { return junctionIndex >= 0; }
        bool isValid() const { return isPin() || isJunction(); }
    };

    struct Wire
    {
        WireNode a;
        WireNode b;
    };

    struct Probe
    {
        juce::String id;
        juce::String label;
        juce::String role;
        WireNode node;
        juce::Colour colour;
    };

    struct Group
    {
        juce::String id;
        juce::String name;
        juce::String category { "user_group" };
        juce::String notes;
        std::vector<int> memberInstances;
        juce::Colour colour { 0xff78dcca };
    };

    struct DisjointSet
    {
        std::vector<int> parent;

        explicit DisjointSet(int count)
        {
            parent.resize((size_t)count);
            for (int i = 0; i < count; ++i) parent[(size_t)i] = i;
        }

        int find(int x)
        {
            auto& p = parent[(size_t)x];
            if (p == x) return x;
            p = find(p);
            return p;
        }

        void unite(int a, int b)
        {
            const auto ra = find(a);
            const auto rb = find(b);
            if (ra != rb) parent[(size_t)rb] = ra;
        }
    };

    std::vector<Instance> instances;
    std::vector<Wire> wires;
    std::vector<juce::Point<float>> junctions;
    std::vector<Probe> probes;
    std::vector<Group> groups;
    WireNode wireDragStart;
    int selectedInstance = -1;
    juce::Array<int> selectedInstances;
    int selectedGroup = -1;
    bool dragHover = false;
    juce::String dragMessage = "Drop symbol on schematic";
    bool wireDragging = false;
    bool draggingInstance = false;
    bool selectingBox = false;
    bool resizingRail = false;
    bool resizingLeftRailEnd = false;
    bool snapEnabled = true;
    bool panning = false;
    int resizingRailInstance = -1;
    float fixedRailEndX = 0.0f;
    float canvasZoom = 1.0f;
    juce::Point<float> viewOffset { 0.0f, 0.0f };
    juce::Point<float> panStartMouse;
    juce::Point<float> selectionBoxStart;
    juce::Point<float> selectionBoxEnd;
    std::vector<juce::Point<float>> selectedDragStartPositions;
    juce::Point<float> panStartOffset;
    juce::Point<float> dragStartMouse;
    juce::Point<float> dragStartPosition;
    juce::Point<float> wireDragPosition;
    std::function<juce::String()> getSelectedSymbolId;
    std::function<bool()> getStampPlacementEnabled;
    std::function<void(juce::String)> onStatus;
    std::function<void(juce::String, juce::String, juce::String)> onProbeChanged;
    std::function<void(juce::String, juce::String)> onInstrumentOpen;
    std::function<void(int, juce::String, juce::String, juce::String, juce::String, juce::String, juce::String, juce::String)> onSelectionChanged;

    static juce::String quote(const juce::String& text)
    {
        return "\"" + text.replace("\\", "\\\\").replace("\"", "\\\"") + "\"";
    }

    static juce::String toolFailure(const juce::String& toolName, const juce::String& message)
    {
        return "{ \"ok\": false, \"tool\": " + quote(toolName)
            + ", \"error\": " + quote(message) + " }";
    }

    void forceDeferredRepaint()
    {
        repaint();
        juce::Component::SafePointer<juce::Component> safeThis(this);
        juce::MessageManager::callAsync([safeThis] {
            if (safeThis == nullptr)
                return;

            safeThis->repaint();
            if (auto* topLevel = safeThis->getTopLevelComponent())
                topLevel->repaint();
        });
    }

    static juce::String nullableQuote(const juce::String& text)
    {
        return text.isEmpty() ? juce::String("null") : quote(text);
    }

    static juce::String stringProperty(const juce::DynamicObject& object,
                                       const char* name,
                                       const juce::String& fallback)
    {
        const auto property = juce::Identifier(name);
        if (!object.hasProperty(property))
            return fallback;

        const auto value = object.getProperty(property);
        if (value.isVoid() || value.isUndefined())
            return fallback;

        return value.toString();
    }

    static float floatProperty(const juce::DynamicObject& object, const char* name, float fallback)
    {
        const auto property = juce::Identifier(name);
        if (!object.hasProperty(property))
            return fallback;

        const auto value = object.getProperty(property);
        if (value.isVoid() || value.isUndefined())
            return fallback;

        return (float)(double)value;
    }

    void clearModel()
    {
        const auto previousProbes = probes;
        instances.clear();
        wires.clear();
        junctions.clear();
        probes.clear();
        groups.clear();
        selectedInstance = -1;
        selectedInstances.clear();
        selectedGroup = -1;
        wireDragging = false;
        draggingInstance = false;
        resizingRail = false;
        resizingRailInstance = -1;
        notifySelection();

        if (onProbeChanged)
            for (const auto& probe : previousProbes)
                onProbeChanged(probe.id, probe.label, {});

        repaint();
    }

    void drawGrid(juce::Graphics& g)
    {
        const auto topLeft = viewToCanvas({ 0.0f, 0.0f });
        const auto bottomRight = viewToCanvas({ (float)getWidth(), (float)getHeight() });
        const auto startX = (int)std::floor(topLeft.x / 24.0f) * 24 - 24;
        const auto endX = (int)std::ceil(bottomRight.x / 24.0f) * 24 + 24;
        const auto startY = (int)std::floor(topLeft.y / 24.0f) * 24 - 24;
        const auto endY = (int)std::ceil(bottomRight.y / 24.0f) * 24 + 24;
        g.setColour(juce::Colour(0xff18222b));
        for (int x = startX; x <= endX; x += 24) g.drawVerticalLine(x, (float)startY, (float)endY);
        for (int y = startY; y <= endY; y += 24) g.drawHorizontalLine(y, (float)startX, (float)endX);
        g.setColour(juce::Colour(0xff26323d));
        for (int x = ((startX / 120) - 1) * 120; x <= endX; x += 120) g.drawVerticalLine(x, (float)startY, (float)endY);
        for (int y = ((startY / 120) - 1) * 120; y <= endY; y += 120) g.drawHorizontalLine(y, (float)startX, (float)endX);
    }

    static juce::Point<float> snap(juce::Point<float> p)
    {
        return { std::round(p.x / 24.0f) * 24.0f, std::round(p.y / 24.0f) * 24.0f };
    }

    juce::Point<float> snapPoint(juce::Point<float> p) const
    {
        return snapEnabled ? snap(p) : p;
    }

    juce::Point<float> viewToCanvas(juce::Point<float> p) const
    {
        const auto scale = std::max(0.001f, canvasZoom);
        return { (p.x - viewOffset.x) / scale, (p.y - viewOffset.y) / scale };
    }

    juce::Point<float> canvasToView(juce::Point<float> p) const
    {
        return { p.x * canvasZoom + viewOffset.x, p.y * canvasZoom + viewOffset.y };
    }

    void beginPan(juce::Point<float> position)
    {
        panning = true;
        draggingInstance = false;
        wireDragging = false;
        resizingRail = false;
        panStartMouse = position;
        panStartOffset = viewOffset;
        if (onStatus) onStatus("Pan schematic canvas.");
    }

    float hitDistance(float modelPixels) const
    {
        return modelPixels / std::max(0.001f, canvasZoom);
    }

    static bool isRailBus(const juce::String& symbolId)
    {
        return schematic::isRailBus(symbolId);
    }

    static bool isInstrumentNode(const juce::String& symbolId)
    {
        return schematic::isInstrumentSymbol(symbolId);
    }

    SymbolDef symbolFor(const juce::String& id) const
    {
        // Unknown ids yield a pinless invalid def; placement and loading
        // reject unknown symbols before they reach the model.
        return schematic::symbolFor(id);
    }

    juce::String defaultValueFor(const juce::String& symbolId) const
    {
        if (symbolId == "resistor") return "10k";
        if (symbolId == "potentiometer") return "10k";
        if (symbolId == "capacitor") return "1u";
        if (symbolId == "capacitor_polarized") return "10u";
        if (symbolId == "variable_capacitor") return "100p";
        if (symbolId == "inductor") return "10m";
        if (symbolId == "coupled_inductor") return "10m";
        if (symbolId == "transformer") return "1:1";
        if (symbolId == "diode") return "1N4148";
        if (symbolId == "zener_diode") return "5V1";
        if (symbolId == "led") return "LED";
        if (symbolId == "schottky_diode") return "BAT54";
        if (symbolId == "power_bus") return "+V";
        if (symbolId == "ground_bus") return "0";
        if (symbolId == "battery") return "9";
        if (symbolId == "voltage_source") return "10";
        if (symbolId == "ac_voltage_source") return "1";
        if (symbolId == "current_source") return "1m";
        if (symbolId == "ac_current_source") return "1m";
        if (symbolId == "vcvs" || symbolId == "vccs" || symbolId == "ccvs" || symbolId == "cccs") return "1";
        if (symbolId == "signal_source") return "1";
        if (symbolId == "opamp_741") return "uA741";
        if (symbolId == "npn") return "generic_npn";
        if (symbolId == "pnp") return "generic_pnp";
        if (symbolId == "nmos") return "generic_nmos";
        if (symbolId == "pmos") return "generic_pmos";
        if (symbolId == "njfet") return "generic_njfet";
        if (symbolId == "pjfet") return "generic_pjfet";
        if (symbolId == "fuse") return "1A";
        if (symbolId == "oscilloscope_2ch") return "2ch";
        if (symbolId == "digital_multimeter") return "DCV";
        return "";
    }

    juce::String archetypeFor(const juce::String& symbolId) const
    {
        if (symbolId == "resistor") return "passive.resistor";
        if (symbolId == "potentiometer") return "passive.potentiometer";
        if (symbolId == "capacitor") return "passive.capacitor";
        if (symbolId == "capacitor_polarized") return "passive.capacitor.polarized";
        if (symbolId == "variable_capacitor") return "passive.capacitor.variable";
        if (symbolId == "inductor") return "passive.inductor";
        if (symbolId == "coupled_inductor") return "magnetics.coupled_inductor";
        if (symbolId == "transformer") return "magnetics.transformer";
        if (symbolId == "diode") return "discrete.diode";
        if (symbolId == "zener_diode") return "discrete.diode.zener";
        if (symbolId == "led") return "discrete.diode.led";
        if (symbolId == "schottky_diode") return "discrete.diode.schottky";
        if (symbolId == "power_bus") return "net.power_bus";
        if (symbolId == "ground" || symbolId == "ground_bus") return "net.ground_reference";
        if (symbolId == "battery") return "source.battery";
        if (symbolId == "voltage_source") return "source.dc_voltage";
        if (symbolId == "ac_voltage_source") return "source.ac_voltage";
        if (symbolId == "current_source") return "source.dc_current";
        if (symbolId == "ac_current_source") return "source.ac_current";
        if (symbolId == "vcvs") return "source.controlled.vcvs";
        if (symbolId == "vccs") return "source.controlled.vccs";
        if (symbolId == "ccvs") return "source.controlled.ccvs";
        if (symbolId == "cccs") return "source.controlled.cccs";
        if (symbolId == "signal_source") return "source.signal";
        if (symbolId == "opamp_741") return "analog.op_amp";
        if (symbolId == "npn") return "discrete.bjt.npn";
        if (symbolId == "pnp") return "discrete.bjt.pnp";
        if (symbolId == "nmos") return "discrete.fet.nmos";
        if (symbolId == "pmos") return "discrete.fet.pmos";
        if (symbolId == "njfet") return "discrete.fet.njfet";
        if (symbolId == "pjfet") return "discrete.fet.pjfet";
        if (symbolId == "switch_spst") return "switch.spst";
        if (symbolId == "switch_spdt") return "switch.spdt";
        if (symbolId == "relay_spst") return "switch.relay.spst";
        if (symbolId == "fuse") return "protection.fuse";
        if (symbolId == "connector_2") return "connector.2pin";
        if (symbolId == "connector_3") return "connector.3pin";
        if (symbolId == "test_point") return "connector.test_point";
        if (symbolId == "logic_not") return "digital.logic.not";
        if (symbolId == "logic_and") return "digital.logic.and";
        if (symbolId == "logic_or") return "digital.logic.or";
        if (symbolId == "logic_nand") return "digital.logic.nand";
        if (symbolId == "logic_nor") return "digital.logic.nor";
        if (symbolId == "logic_xor") return "digital.logic.xor";
        if (symbolId == "oscilloscope_2ch") return "instrument.oscilloscope";
        if (symbolId == "digital_multimeter") return "instrument.multimeter";
        return "unknown";
    }

    juce::String familyFor(const juce::String& symbolId) const
    {
        if (symbolId == "opamp_741") return "741";
        if (symbolId == "npn") return "generic_npn";
        if (symbolId == "pnp") return "generic_pnp";
        if (symbolId == "nmos") return "generic_nmos";
        if (symbolId == "pmos") return "generic_pmos";
        if (symbolId == "njfet") return "generic_njfet";
        if (symbolId == "pjfet") return "generic_pjfet";
        return "";
    }

    juce::String defaultFrequencyFor(const juce::String& symbolId) const
    {
        if (symbolId == "ac_voltage_source" || symbolId == "signal_source") return "1k";
        return "";
    }

    juce::String defaultBusNameFor(const juce::String& symbolId) const
    {
        if (symbolId == "power_bus") return "+V";
        if (symbolId == "ground" || symbolId == "ground_bus") return "0";
        return "";
    }

    juce::String parametersJsonFor(const Instance& instance) const
    {
        const auto& symbolId = instance.symbolId;
        if (symbolId == "resistor")
            return "{ \"resistance\": { \"value\": " + quote(instance.value) + ", \"unit\": \"ohm\" } }";
        if (symbolId == "capacitor")
            return "{ \"capacitance\": { \"value\": " + quote(instance.value) + ", \"unit\": \"F\" } }";
        if (symbolId == "inductor")
            return "{ \"inductance\": { \"value\": " + quote(instance.value) + ", \"unit\": \"H\" } }";
        if (symbolId == "diode")
            return "{ \"model\": " + quote(instance.value) + " }";
        if (symbolId == "power_bus")
            return "{ \"name\": " + quote(instance.busName) + ", \"voltageHint\": null }";
        if (symbolId == "ground" || symbolId == "ground_bus")
            return "{ \"name\": " + quote(instance.busName) + ", \"voltageHint\": \"0V\" }";
        if (symbolId == "battery")
            return "{ \"voltage\": { \"value\": " + quote(instance.value) + ", \"unit\": \"V\" } }";
        if (symbolId == "voltage_source")
            return "{ \"dcVoltage\": { \"value\": " + quote(instance.value) + ", \"unit\": \"V\" } }";
        if (symbolId == "ac_voltage_source")
            return "{ \"amplitude\": { \"value\": " + quote(instance.value) + ", \"unit\": \"V\" }, \"frequency\": { \"value\": " + quote(instance.frequency) + ", \"unit\": \"Hz\" }, \"offset\": { \"value\": \"0\", \"unit\": \"V\" } }";
        if (symbolId == "signal_source")
            return "{ \"waveform\": \"sine\", \"amplitude\": { \"value\": " + quote(instance.value) + ", \"unit\": \"V\" }, \"frequency\": { \"value\": " + quote(instance.frequency) + ", \"unit\": \"Hz\" } }";
        if (symbolId == "oscilloscope_2ch")
            return "{ \"instrumentType\": \"digital_oscilloscope\", \"channels\": [\"CH1\", \"CH2\"], \"reference\": \"REF\", \"windowMode\": \"floating_preferred\" }";
        if (symbolId == "digital_multimeter")
            return "{ \"instrumentType\": \"digital_multimeter\", \"function\": " + quote(instance.value) + ", \"connections\": [\"HI\", \"LO\"], \"windowMode\": \"floating_preferred\" }";
        return "{}";
    }

    int pinOrdinal(const PinRef& pin) const
    {
        int ordinal = 0;
        for (int i = 0; i < pin.instanceIndex; ++i)
            ordinal += (int)symbolFor(instances[(size_t)i].symbolId).pins.size();
        return ordinal + pin.pinIndex;
    }

    int pinCount() const
    {
        int count = 0;
        for (const auto& instance : instances)
            count += (int)symbolFor(instance.symbolId).pins.size();
        return count;
    }

    int nodeOrdinal(const WireNode& node) const
    {
        if (node.isPin())
            return pinOrdinal(node.pin);
        if (node.isJunction())
            return pinCount() + node.junctionIndex;
        return -1;
    }

    std::map<int, juce::String> computeNetNames() const
    {
        const auto totalPins = pinCount();
        const auto totalNodes = totalPins + (int)junctions.size();

        std::map<int, juce::String> result;
        if (totalNodes <= 0)
            return result;

        DisjointSet sets(totalNodes);
        for (const auto& wire : wires)
        {
            const auto a = nodeOrdinal(wire.a);
            const auto b = nodeOrdinal(wire.b);
            if (a < 0 || b < 0)
                continue;
            sets.unite(a, b);
        }

        // Named supply ports and power rails with the same name are one net
        // (SCH-P2), even with no wire between them.
        std::map<juce::String, int> namedSupplyPins;
        for (size_t i = 0; i < instances.size(); ++i)
        {
            const auto& instance = instances[i];
            if (instance.symbolId != "power_port" && instance.symbolId != "power_bus")
                continue;
            const auto name = instance.busName.trim();
            if (name.isEmpty())
                continue;
            const auto ordinal = pinOrdinal({ (int)i, 0 });
            const auto found = namedSupplyPins.find(name);
            if (found == namedSupplyPins.end())
                namedSupplyPins[name] = ordinal;
            else
                sets.unite(found->second, ordinal);
        }

        std::map<int, juce::String> supplyNetNames;
        for (const auto& [name, ordinal] : namedSupplyPins)
            supplyNetNames[sets.find(ordinal)] = spiceNetName(name);

        std::set<int> groundRoots;
        for (size_t i = 0; i < instances.size(); ++i)
        {
            if (instances[i].symbolId != "ground" && instances[i].symbolId != "ground_bus")
                continue;
            const auto symbol = symbolFor(instances[i].symbolId);
            for (size_t p = 0; p < symbol.pins.size(); ++p)
                groundRoots.insert(sets.find(pinOrdinal({ (int)i, (int)p })));
        }

        std::map<int, int> assigned;
        int nextNet = 1;
        for (int pin = 0; pin < totalNodes; ++pin)
        {
            const auto root = sets.find(pin);
            if (groundRoots.count(root) != 0)
            {
                result[pin] = "0";
                continue;
            }
            if (const auto named = supplyNetNames.find(root); named != supplyNetNames.end())
            {
                result[pin] = named->second;
                continue;
            }
            if (assigned.count(root) == 0)
                assigned[root] = nextNet++;
            result[pin] = "n" + juce::String(assigned[root]);
        }
        return result;
    }

    static juce::String spiceNetName(const juce::String& supplyName)
    {
        juce::String result;
        for (auto c : supplyName)
        {
            if (c == '+') result << "P";
            else if (c == '-') result << "N";
            else if (juce::CharacterFunctions::isLetterOrDigit(c)) result << juce::String::charToString(c);
            else result << "_";
        }
        return result.isEmpty() ? juce::String("VSUPPLY") : result;
    }

    juce::String netFor(const PinRef& pin, const std::map<int, juce::String>& netNames) const
    {
        if (pin.instanceIndex < 0 || pin.instanceIndex >= (int)instances.size())
            return "floating";
        const auto ordinal = pinOrdinal(pin);
        const auto found = netNames.find(ordinal);
        return found != netNames.end() ? found->second : "floating";
    }

    juce::String netForNode(const WireNode& node, const std::map<int, juce::String>& netNames) const
    {
        const auto ordinal = nodeOrdinal(node);
        if (ordinal < 0)
            return "floating";
        const auto found = netNames.find(ordinal);
        return found != netNames.end() ? found->second : "floating";
    }

    std::vector<juce::String> printableNetNames(const std::map<int, juce::String>& netNames) const
    {
        std::set<juce::String> unique;
        for (const auto& entry : netNames)
            if (entry.second != "0")
                unique.insert(entry.second);

        std::vector<juce::String> result;
        result.reserve(unique.size());
        for (const auto& net : unique)
            result.push_back(net);
        return result;
    }

    juce::Point<float> pinPosition(const PinRef& pin) const
    {
        if (pin.instanceIndex < 0 || pin.instanceIndex >= (int)instances.size()) return {};
        const auto& instance = instances[(size_t)pin.instanceIndex];
        const auto symbol = symbolFor(instance.symbolId);
        if (pin.pinIndex < 0 || pin.pinIndex >= (int)symbol.pins.size()) return instance.position;
        return instance.position + rotateOffset(symbol.pins[(size_t)pin.pinIndex].offset, instance.rotation);
    }

    juce::String pinLabel(const PinRef& pin) const
    {
        if (pin.instanceIndex < 0 || pin.instanceIndex >= (int)instances.size()) return {};
        const auto& instance = instances[(size_t)pin.instanceIndex];
        const auto symbol = symbolFor(instance.symbolId);
        if (pin.pinIndex < 0 || pin.pinIndex >= (int)symbol.pins.size()) return instance.refdes;
        return instance.refdes + "." + symbol.pins[(size_t)pin.pinIndex].name;
    }

    int instanceIndexForRefdes(const juce::String& refdes) const
    {
        const auto requested = refdes.trim();
        for (int index = 0; index < (int)instances.size(); ++index)
            if (instances[(size_t)index].refdes.equalsIgnoreCase(requested))
                return index;
        return -1;
    }

    juce::String availableNodeSummary() const
    {
        juce::StringArray labels;
        for (int i = 0; i < (int)instances.size(); ++i)
        {
            const auto symbol = symbolFor(instances[(size_t)i].symbolId);
            for (int pin = 0; pin < (int)symbol.pins.size(); ++pin)
                labels.add(pinLabel({ i, pin }));
        }
        for (int i = 0; i < (int)junctions.size(); ++i)
            labels.add("N" + juce::String(i + 1));
        return labels.joinIntoString(", ");
    }

    bool nodeFromLabel(const juce::String& suppliedLabel, WireNode& node, juce::String& error) const
    {
        const auto label = suppliedLabel.trim();
        if (label.isEmpty())
        {
            error = "Node label is empty. Available labels: " + availableNodeSummary();
            return false;
        }

        if (label.startsWithIgnoreCase("N") && label.substring(1).containsOnly("0123456789"))
        {
            const auto index = label.substring(1).getIntValue() - 1;
            if (index >= 0 && index < (int)junctions.size())
            {
                node = WireNode::forJunction(index);
                return true;
            }
        }

        const auto dot = label.lastIndexOfChar('.');
        const auto refdes = dot > 0 ? label.substring(0, dot).trim() : label;
        const auto pinName = dot > 0 ? label.substring(dot + 1).trim() : juce::String();
        const auto instanceIndex = instanceIndexForRefdes(refdes);
        if (instanceIndex < 0)
        {
            error = "Unknown instance " + refdes + ". Available labels: " + availableNodeSummary();
            return false;
        }

        const auto symbol = symbolFor(instances[(size_t)instanceIndex].symbolId);
        if (pinName.isEmpty() && symbol.pins.size() == 1)
        {
            node = WireNode::forPin({ instanceIndex, 0 });
            return true;
        }

        for (int pin = 0; pin < (int)symbol.pins.size(); ++pin)
        {
            if (symbol.pins[(size_t)pin].name.equalsIgnoreCase(pinName))
            {
                node = WireNode::forPin({ instanceIndex, pin });
                return true;
            }
        }

        error = "Unknown pin label " + label + ". Available labels: " + availableNodeSummary();
        return false;
    }

    juce::Point<float> nodePosition(const WireNode& node) const
    {
        if (node.isPin())
            return pinPosition(node.pin);
        if (node.isJunction() && node.junctionIndex < (int)junctions.size())
            return junctions[(size_t)node.junctionIndex];
        return {};
    }

    juce::String nodeLabel(const WireNode& node) const
    {
        if (node.isPin())
            return pinLabel(node.pin);
        if (node.isJunction())
            return "N" + juce::String(node.junctionIndex + 1);
        return {};
    }

    static bool sameNode(const WireNode& a, const WireNode& b)
    {
        if (a.isPin() && b.isPin())
            return a.pin.instanceIndex == b.pin.instanceIndex && a.pin.pinIndex == b.pin.pinIndex;
        if (a.isJunction() && b.isJunction())
            return a.junctionIndex == b.junctionIndex;
        return false;
    }

    bool isRailAnchorNode(const WireNode& node) const
    {
        return node.isPin()
            && node.pin.instanceIndex >= 0
            && node.pin.instanceIndex < (int)instances.size()
            && isRailBus(instances[(size_t)node.pin.instanceIndex].symbolId);
    }

    bool isInternalRailTapWire(const Wire& wire) const
    {
        return (isRailAnchorNode(wire.a) && wire.b.isJunction())
            || (isRailAnchorNode(wire.b) && wire.a.isJunction());
    }

    juce::Point<float> nodeLeadDirection(const WireNode& node) const
    {
        if (!node.isPin() || node.pin.instanceIndex < 0 || node.pin.instanceIndex >= (int)instances.size())
            return {};

        const auto& instance = instances[(size_t)node.pin.instanceIndex];
        const auto symbol = symbolFor(instance.symbolId);
        if (node.pin.pinIndex < 0 || node.pin.pinIndex >= (int)symbol.pins.size())
            return {};

        const auto offset = rotateOffset(symbol.pins[(size_t)node.pin.pinIndex].offset, instance.rotation);
        const auto length = std::sqrt(offset.x * offset.x + offset.y * offset.y);
        if (length <= 0.001f)
            return {};

        return { offset.x / length, offset.y / length };
    }

    std::vector<juce::Point<float>> routedWirePoints(const WireNode& a, const WireNode& b) const
    {
        return routedWirePoints(a, nodePosition(b), nodeLeadDirection(b));
    }

    std::vector<juce::Point<float>> routedWirePoints(const WireNode& a,
                                                     juce::Point<float> b,
                                                     juce::Point<float> bLead) const
    {
        constexpr auto leadLength = 24.0f;
        const auto start = nodePosition(a);
        const auto aLead = nodeLeadDirection(a);
        const auto startRun = aLead == juce::Point<float>() ? start : start + aLead * leadLength;
        const auto endRun = bLead == juce::Point<float>() ? b : b + bLead * leadLength;

        std::vector<juce::Point<float>> points;
        points.push_back(start);
        if (startRun.getDistanceFrom(start) > 0.1f)
            points.push_back(startRun);

        if (std::abs(startRun.x - endRun.x) <= 0.1f || std::abs(startRun.y - endRun.y) <= 0.1f)
        {
            points.push_back(endRun);
        }
        else
        {
            const auto midX = std::round((startRun.x + endRun.x) * 0.5f / 24.0f) * 24.0f;
            points.push_back({ midX, startRun.y });
            points.push_back({ midX, endRun.y });
            points.push_back(endRun);
        }

        if (b.getDistanceFrom(endRun) > 0.1f)
            points.push_back(b);

        return points;
    }

    PinRef hitTestPin(juce::Point<float> p) const
    {
        for (int i = (int)instances.size() - 1; i >= 0; --i)
        {
            if (isRailBus(instances[(size_t)i].symbolId))
                continue;

            const auto symbol = symbolFor(instances[(size_t)i].symbolId);
            for (int j = 0; j < (int)symbol.pins.size(); ++j)
            {
                const auto pin = pinPosition({ i, j });
                if (pin.getDistanceFrom(p) <= hitDistance(14.0f))
                    return { i, j };
            }
        }
        return {};
    }

    int hitTestJunction(juce::Point<float> p) const
    {
        for (int i = (int)junctions.size() - 1; i >= 0; --i)
            if (junctions[(size_t)i].getDistanceFrom(p) <= hitDistance(12.0f))
                return i;
        return -1;
    }

    static float distanceToSegment(juce::Point<float> p, juce::Point<float> a, juce::Point<float> b)
    {
        const auto ab = b - a;
        const auto ap = p - a;
        const auto lengthSquared = ab.x * ab.x + ab.y * ab.y;
        if (lengthSquared <= 0.001f)
            return p.getDistanceFrom(a);

        const auto t = std::clamp((ap.x * ab.x + ap.y * ab.y) / lengthSquared, 0.0f, 1.0f);
        return p.getDistanceFrom({ a.x + ab.x * t, a.y + ab.y * t });
    }

    float distanceToWire(juce::Point<float> p, const Wire& wire) const
    {
        if (isInternalRailTapWire(wire))
            return std::numeric_limits<float>::max();

        const auto points = routedWirePoints(wire.a, wire.b);
        auto best = std::numeric_limits<float>::max();
        for (size_t i = 1; i < points.size(); ++i)
            best = std::min(best, distanceToSegment(p, points[i - 1], points[i]));
        return best;
    }

    int hitTestWire(juce::Point<float> p) const
    {
        for (int i = (int)wires.size() - 1; i >= 0; --i)
            if (distanceToWire(p, wires[(size_t)i]) <= hitDistance(8.0f))
                return i;
        return -1;
    }

    int hitTestInstance(juce::Point<float> p) const
    {
        for (int i = (int)instances.size() - 1; i >= 0; --i)
        {
            const auto& instance = instances[(size_t)i];
            const auto symbol = symbolFor(instance.symbolId);
            if (orientedBounds(instance, symbol).expanded(hitDistance(4.0f)).contains(p))
                return i;
        }
        return -1;
    }

    bool isInstanceSelected(int index) const
    {
        return selectedInstances.contains(index);
    }

    void toggleInstanceSelection(int index)
    {
        if (selectedInstances.contains(index))
            selectedInstances.removeFirstMatchingValue(index);
        else
            selectedInstances.add(index);

        selectedInstance = selectedInstances.isEmpty() ? -1 : selectedInstances.getLast();
    }

    void beginSelectionBox(juce::Point<float> start)
    {
        selectedInstances.clear();
        selectedInstance = -1;
        selectingBox = true;
        selectionBoxStart = start;
        selectionBoxEnd = start;
        notifySelection();
        repaint();
    }

    juce::Rectangle<float> currentSelectionBox() const
    {
        return juce::Rectangle<float>::leftTopRightBottom(std::min(selectionBoxStart.x, selectionBoxEnd.x),
                                                          std::min(selectionBoxStart.y, selectionBoxEnd.y),
                                                          std::max(selectionBoxStart.x, selectionBoxEnd.x),
                                                          std::max(selectionBoxStart.y, selectionBoxEnd.y));
    }

    void updateSelectionFromBox()
    {
        const auto box = currentSelectionBox();
        selectedInstances.clear();
        for (int i = 0; i < (int)instances.size(); ++i)
        {
            const auto symbol = symbolFor(instances[(size_t)i].symbolId);
            if (box.intersects(orientedBounds(instances[(size_t)i], symbol).expanded(4.0f)))
                selectedInstances.add(i);
        }
        selectedInstance = selectedInstances.isEmpty() ? -1 : selectedInstances.getLast();
        notifySelection();
    }

    void captureSelectedDragStarts()
    {
        selectedDragStartPositions.clear();
        selectedDragStartPositions.resize((size_t)instances.size());
        for (int i = 0; i < (int)instances.size(); ++i)
            selectedDragStartPositions[(size_t)i] = instances[(size_t)i].position;
    }

    void moveSelectedInstances(juce::Point<float> delta)
    {
        for (int index : selectedInstances)
        {
            if (index >= 0 && index < (int)instances.size())
                instances[(size_t)index].position = snapPoint(selectedDragStartPositions[(size_t)index] + delta);
        }
    }

    void drawSelectionBox(juce::Graphics& g)
    {
        if (!selectingBox)
            return;

        const auto box = currentSelectionBox();
        g.setColour(juce::Colour(0x3329b6f6));
        g.fillRect(box);
        g.setColour(juce::Colour(0xff29b6f6));
        g.drawRect(box, 1.5f);
    }

    juce::Rectangle<float> groupBounds(const Group& group) const
    {
        juce::Rectangle<float> bounds;
        bool hasBounds = false;
        for (int index : group.memberInstances)
        {
            if (index < 0 || index >= (int)instances.size())
                continue;
            const auto symbol = symbolFor(instances[(size_t)index].symbolId);
            const auto itemBounds = orientedBounds(instances[(size_t)index], symbol).expanded(28.0f);
            bounds = hasBounds ? bounds.getUnion(itemBounds) : itemBounds;
            hasBounds = true;
        }
        return hasBounds ? bounds.expanded(12.0f) : juce::Rectangle<float>();
    }

    void drawGroups(juce::Graphics& g)
    {
        for (int i = 0; i < (int)groups.size(); ++i)
        {
            const auto& group = groups[(size_t)i];
            const auto bounds = groupBounds(group);
            if (bounds.isEmpty())
                continue;

            g.setColour(group.colour.withAlpha(0.08f));
            g.fillRoundedRectangle(bounds, 6.0f);
            g.setColour(i == selectedGroup ? juce::Colour(0xffffc857) : group.colour.withAlpha(0.7f));
            g.drawRoundedRectangle(bounds, 6.0f, i == selectedGroup ? 2.5f : 1.5f);
            g.setFont(juce::Font(13.0f, juce::Font::bold));
            g.drawText(group.name, bounds.reduced(8.0f).removeFromTop(18.0f), juce::Justification::centredLeft);
        }
    }

    int hitTestGroup(juce::Point<float> position) const
    {
        for (int i = (int)groups.size() - 1; i >= 0; --i)
        {
            const auto bounds = groupBounds(groups[(size_t)i]);
            if (bounds.isEmpty())
                continue;
            if (bounds.expanded(hitDistance(4.0f)).contains(position)
                && !bounds.reduced(18.0f).contains(position))
                return i;
        }
        return -1;
    }

    void selectGroup(int groupIndex)
    {
        if (groupIndex < 0 || groupIndex >= (int)groups.size())
            return;

        selectedGroup = groupIndex;
        selectedInstances.clear();
        for (int member : groups[(size_t)groupIndex].memberInstances)
            if (member >= 0 && member < (int)instances.size())
                selectedInstances.addIfNotAlreadyThere(member);
        selectedInstance = selectedInstances.isEmpty() ? -1 : selectedInstances.getLast();
    }

    void createGroupFromSelection()
    {
        if (selectedInstances.size() < 2)
        {
            if (onStatus) onStatus("Select two or more components to create a group.");
            return;
        }

        Group group;
        group.id = "GRP" + juce::String((int)groups.size() + 1);
        group.name = "Group " + juce::String((int)groups.size() + 1);
        group.category = "user_group";
        for (int index : selectedInstances)
            if (index >= 0 && index < (int)instances.size())
                group.memberInstances.push_back(index);

        if (group.memberInstances.size() < 2)
            return;

        groups.push_back(std::move(group));
        selectedGroup = (int)groups.size() - 1;
        if (onStatus) onStatus("Created " + groups.back().name + " from " + juce::String((int)groups.back().memberInstances.size()) + " component(s).");
        repaint();
    }

    void editSelectedGroupMetadata()
    {
        if (selectedGroup < 0 || selectedGroup >= (int)groups.size())
            return;

        const auto groupIndex = selectedGroup;
        auto* editor = new juce::AlertWindow("Group Metadata", "Edit the visual group box.", juce::AlertWindow::NoIcon);
        editor->addTextEditor("name", groups[(size_t)groupIndex].name, "Title");
        editor->addTextEditor("category", groups[(size_t)groupIndex].category, "Category");
        editor->addTextEditor("notes", groups[(size_t)groupIndex].notes, "Notes");
        editor->addButton("Apply", 1, juce::KeyPress(juce::KeyPress::returnKey));
        editor->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        editor->enterModalState(true, juce::ModalCallbackFunction::create([this, editor, groupIndex](int result) {
            std::unique_ptr<juce::AlertWindow> owner(editor);
            if (result != 1 || groupIndex < 0 || groupIndex >= (int)groups.size())
                return;

            auto& group = groups[(size_t)groupIndex];
            group.name = owner->getTextEditor("name")->getText().trim();
            group.category = owner->getTextEditor("category")->getText().trim();
            group.notes = owner->getTextEditor("notes")->getText().trim();
            if (group.name.isEmpty())
                group.name = group.id;
            if (group.category.isEmpty())
                group.category = "user_group";
            if (onStatus) onStatus("Updated group metadata for " + group.name + ".");
            repaint();
        }), true);
    }

    void ungroupSelectedGroup()
    {
        if (selectedGroup < 0 || selectedGroup >= (int)groups.size())
            return;

        const auto name = groups[(size_t)selectedGroup].name;
        groups.erase(groups.begin() + selectedGroup);
        selectedGroup = -1;
        if (onStatus) onStatus("Removed group box " + name + "; component wiring was unchanged.");
        repaint();
    }

    WireNode nodeAt(juce::Point<float> rawPosition, juce::Point<float> snappedPosition)
    {
        if (auto pin = hitTestPin(rawPosition); pin.instanceIndex >= 0)
            return WireNode::forPin(pin);

        if (auto junction = hitTestJunction(rawPosition); junction >= 0)
            return WireNode::forJunction(junction);

        if (auto rail = hitTestRailBus(rawPosition); rail >= 0)
            return createRailTap(rail, snappedPosition);

        if (auto wireIndex = hitTestWire(rawPosition); wireIndex >= 0)
            return createJunctionOnWire(wireIndex, snappedPosition);

        return {};
    }

    static juce::Point<float> rotateOffset(juce::Point<float> offset, int rotation)
    {
        return schematic::rotateOffset(offset, rotation);
    }

    juce::Rectangle<float> orientedBounds(const Instance& instance, const SymbolDef& symbol) const
    {
        if (isRailBus(instance.symbolId))
            return railBounds(instance).expanded(0.0f, 10.0f);

        return schematic::rotateBounds(symbol.bounds, instance.rotation)
            .translated(instance.position.x, instance.position.y);
    }

    juce::Rectangle<float> railBounds(const Instance& instance) const
    {
        const auto length = std::max(120.0f, instance.busLength);
        return { instance.position.x - length * 0.5f, instance.position.y - 1.5f, length, 3.0f };
    }

    int hitTestRailBus(juce::Point<float> p) const
    {
        for (int i = (int)instances.size() - 1; i >= 0; --i)
        {
            const auto& instance = instances[(size_t)i];
            if (!isRailBus(instance.symbolId))
                continue;

            if (railBounds(instance).expanded(0.0f, hitDistance(12.0f)).contains(p))
                return i;
        }
        return -1;
    }

    juce::Rectangle<float> railHandleBounds(const Instance& instance, bool left) const
    {
        const auto rail = railBounds(instance);
        const auto x = left ? rail.getX() : rail.getRight();
        return { x - 7.0f, instance.position.y - 12.0f, 14.0f, 24.0f };
    }

    bool beginRailResize(juce::Point<float> p)
    {
        if (selectedInstance < 0 || selectedInstance >= (int)instances.size())
            return false;

        const auto& instance = instances[(size_t)selectedInstance];
        if (!isRailBus(instance.symbolId))
            return false;

        if (railHandleBounds(instance, true).expanded(hitDistance(4.0f)).contains(p))
        {
            resizingRail = true;
            resizingLeftRailEnd = true;
            resizingRailInstance = selectedInstance;
            fixedRailEndX = railBounds(instance).getRight();
            return true;
        }

        if (railHandleBounds(instance, false).expanded(hitDistance(4.0f)).contains(p))
        {
            resizingRail = true;
            resizingLeftRailEnd = false;
            resizingRailInstance = selectedInstance;
            fixedRailEndX = railBounds(instance).getX();
            return true;
        }

        return false;
    }

    void resizeRail(juce::Point<float> p)
    {
        if (resizingRailInstance < 0 || resizingRailInstance >= (int)instances.size())
            return;

        auto& instance = instances[(size_t)resizingRailInstance];
        if (!isRailBus(instance.symbolId))
            return;

        constexpr auto minLength = 120.0f;
        auto movingX = snapPoint(p).x;
        if (std::abs(movingX - fixedRailEndX) < minLength)
            movingX = fixedRailEndX + (movingX < fixedRailEndX ? -minLength : minLength);

        const auto left = std::min(movingX, fixedRailEndX);
        const auto right = std::max(movingX, fixedRailEndX);
        instance.busLength = right - left;
        instance.position.x = (left + right) * 0.5f;
        notifySelection();
    }

    void beginWireDrag(WireNode node, juce::Point<float> position)
    {
        if (!node.isValid())
            return;

        wireDragStart = node;
        wireDragPosition = position;
        wireDragging = true;
        draggingInstance = false;
        if (onStatus) onStatus("Wire drag: " + nodeLabel(node) + ". Release on another pin or wire.");
    }

    void finishWireDrag(juce::Point<float> position)
    {
        wireDragging = false;
        if (!wireDragStart.isValid())
            return;

        const auto target = nodeAt(position, snapPoint(position));
        if (!target.isValid())
        {
            if (onStatus) onStatus("Wire cancelled.");
            wireDragStart = {};
            return;
        }

        if (sameNode(wireDragStart, target))
        {
            if (onStatus) onStatus("Wire cancelled: start and end are the same connector.");
            wireDragStart = {};
            return;
        }

        wires.push_back({ wireDragStart, target });
        if (onStatus) onStatus("Connected " + nodeLabel(wireDragStart) + " to " + nodeLabel(target) + ".");
        wireDragStart = {};
    }

    void disconnectAt(juce::Point<float> position)
    {
        if (auto wireIndex = hitTestWire(position); wireIndex >= 0)
        {
            const auto removed = wires[(size_t)wireIndex];
            wires.erase(wires.begin() + wireIndex);
            if (onStatus) onStatus("Disconnected wire " + nodeLabel(removed.a) + " to " + nodeLabel(removed.b) + ".");
            return;
        }

        if (auto pin = hitTestPin(position); pin.instanceIndex >= 0)
        {
            disconnectNode(WireNode::forPin(pin));
            return;
        }

        if (auto junction = hitTestJunction(position); junction >= 0)
        {
            disconnectNode(WireNode::forJunction(junction));
            return;
        }

        if (onStatus) onStatus("Nothing to disconnect here.");
    }

    void disconnectNode(WireNode node)
    {
        const auto before = wires.size();
        wires.erase(std::remove_if(wires.begin(), wires.end(), [&](const Wire& wire) {
            return sameNode(wire.a, node) || sameNode(wire.b, node);
        }), wires.end());

        const auto removed = before - wires.size();
        if (onStatus)
            onStatus(removed > 0 ? "Disconnected " + juce::String((int)removed) + " wire(s) from " + nodeLabel(node) + "."
                                 : "No wires connected to " + nodeLabel(node) + ".");
    }

    void assignProbe(const juce::String& id, WireNode node)
    {
        const auto label = probeLabel(id);
        if (label.isEmpty())
            return;

        const auto role = probeRole(id);
        const auto colour = probeColour(id);

        auto found = std::find_if(probes.begin(), probes.end(), [&](const Probe& probe) { return probe.id == id; });
        if (found != probes.end())
        {
            found->node = node;
            found->colour = colour;
        }
        else
        {
            probes.push_back({ id, label, role, node, colour });
        }

        if (onStatus) onStatus("Assigned " + label + " to " + nodeLabel(node) + ".");
        if (onProbeChanged) onProbeChanged(id, label, nodeLabel(node));
    }

    bool releaseProbeAt(juce::Point<float> position)
    {
        for (int i = (int)probes.size() - 1; i >= 0; --i)
        {
            const auto p = nodePosition(probes[(size_t)i].node);
            if (p.getDistanceFrom(position) > hitDistance(16.0f))
                continue;

            const auto id = probes[(size_t)i].id;
            const auto label = probes[(size_t)i].label;
            probes.erase(probes.begin() + i);
            if (onStatus) onStatus("Removed " + label + " from schematic.");
            if (onProbeChanged) onProbeChanged(id, label, {});
            return true;
        }
        return false;
    }

    static juce::String probeLabel(const juce::String& id)
    {
        if (id == "DMM_HI") return "DMM+";
        if (id == "DMM_LO") return "DMM-";
        if (id == "SCOPE_CH1") return "CH1";
        if (id == "SCOPE_CH2") return "CH2";
        return {};
    }

    static juce::String probeRole(const juce::String& id)
    {
        if (id == "DMM_HI") return "dmm.high";
        if (id == "DMM_LO") return "dmm.low";
        if (id == "SCOPE_CH1") return "scope.channel1";
        if (id == "SCOPE_CH2") return "scope.channel2";
        return {};
    }

    static juce::Colour probeColour(const juce::String& id)
    {
        if (id == "DMM_HI") return dmmLeadColour(true);
        if (id == "DMM_LO") return dmmLeadColour(false);
        if (id == "SCOPE_CH1") return scopeChannelColour(0);
        if (id == "SCOPE_CH2") return scopeChannelColour(1);
        return juce::Colour(0xffdce9ee);
    }

    WireNode createJunctionOnWire(int wireIndex, juce::Point<float> position)
    {
        const auto junctionIndex = (int)junctions.size();
        junctions.push_back(position);
        const auto junction = WireNode::forJunction(junctionIndex);

        if (wireIndex >= 0 && wireIndex < (int)wires.size())
        {
            const auto existing = wires[(size_t)wireIndex];
            wires.erase(wires.begin() + wireIndex);
            wires.push_back({ existing.a, junction });
            wires.push_back({ junction, existing.b });
        }

        if (onStatus) onStatus("Added junction " + nodeLabel(junction) + ".");
        return junction;
    }

    WireNode createRailTap(int instanceIndex, juce::Point<float> position)
    {
        if (instanceIndex < 0 || instanceIndex >= (int)instances.size())
            return {};

        auto& instance = instances[(size_t)instanceIndex];
        const auto rail = railBounds(instance);
        position.x = std::clamp(position.x, rail.getX(), rail.getRight());
        position.y = instance.position.y;

        const auto junctionIndex = (int)junctions.size();
        junctions.push_back(position);
        const auto junction = WireNode::forJunction(junctionIndex);
        wires.push_back({ WireNode::forPin({ instanceIndex, 0 }), junction });

        if (onStatus) onStatus("Added tap on " + instance.refdes + " " + instance.busName + ".");
        return junction;
    }

    static bool nodeTouchesInstance(const WireNode& node, int instanceIndex)
    {
        return node.isPin() && node.pin.instanceIndex == instanceIndex;
    }

    static void adjustNodeAfterDeletingInstance(WireNode& node, int instanceIndex)
    {
        if (node.isPin() && node.pin.instanceIndex > instanceIndex)
            --node.pin.instanceIndex;
    }

    void deleteSelected()
    {
        if (selectedInstance < 0 || selectedInstance >= (int)instances.size())
        {
            if (onStatus) onStatus("Nothing selected to delete.");
            return;
        }

        const auto removedName = instances[(size_t)selectedInstance].refdes;
        wires.erase(std::remove_if(wires.begin(), wires.end(), [&](const Wire& wire) {
            return nodeTouchesInstance(wire.a, selectedInstance) || nodeTouchesInstance(wire.b, selectedInstance);
        }), wires.end());

        juce::StringArray returnedProbes;
        for (const auto& probe : probes)
            if (nodeTouchesInstance(probe.node, selectedInstance))
                returnedProbes.add(probe.id);
        probes.erase(std::remove_if(probes.begin(), probes.end(), [&](const Probe& probe) {
            return nodeTouchesInstance(probe.node, selectedInstance);
        }), probes.end());

        instances.erase(instances.begin() + selectedInstance);
        for (auto& wire : wires)
        {
            adjustNodeAfterDeletingInstance(wire.a, selectedInstance);
            adjustNodeAfterDeletingInstance(wire.b, selectedInstance);
        }
        for (auto& probe : probes)
            adjustNodeAfterDeletingInstance(probe.node, selectedInstance);
        for (auto& group : groups)
        {
            group.memberInstances.erase(std::remove(group.memberInstances.begin(),
                                                    group.memberInstances.end(),
                                                    selectedInstance),
                                        group.memberInstances.end());
            for (auto& member : group.memberInstances)
                if (member > selectedInstance)
                    --member;
        }
        groups.erase(std::remove_if(groups.begin(), groups.end(), [](const Group& group) {
            return group.memberInstances.size() < 2;
        }), groups.end());

        selectedInstance = -1;
        selectedInstances.clear();
        selectedGroup = -1;
        notifySelection();
        for (const auto& probeId : returnedProbes)
            if (onProbeChanged) onProbeChanged(probeId, {}, {});
        if (onStatus) onStatus("Deleted " + removedName + ".");
        repaint();
    }

    void notifySelection()
    {
        if (onSelectionChanged == nullptr)
            return;

        if (selectedInstance < 0 || selectedInstance >= (int)instances.size())
        {
            onSelectionChanged(-1, {}, {}, {}, {}, {}, {}, {});
            return;
        }

        const auto& instance = instances[(size_t)selectedInstance];
        onSelectionChanged(selectedInstance, instance.refdes, instance.symbolId, instance.value,
                           instance.frequency, instance.busName, instance.family, instance.manufacturerPart);
    }

    juce::String nextRefdesFor(const juce::String& symbolId) const
    {
        const auto prefix = schematic::refdesPrefixFor(symbolId);
        int highest = 0;
        for (const auto& instance : instances)
        {
            if (!instance.refdes.startsWith(prefix))
                continue;
            const auto suffix = instance.refdes.substring(prefix.length());
            if (suffix.isNotEmpty() && suffix.containsOnly("0123456789"))
                highest = std::max(highest, suffix.getIntValue());
        }
        return prefix + juce::String(highest + 1);
    }

    void placeSymbol(const juce::String& symbolId, juce::Point<float> p)
    {
        const auto symbol = symbolFor(symbolId);
        if (!symbol.isValid())
            return;
        instances.push_back({ symbol.id,
                              nextRefdesFor(symbol.id),
                              defaultValueFor(symbol.id),
                              defaultFrequencyFor(symbol.id),
                              defaultBusNameFor(symbol.id),
                              familyFor(symbol.id),
                              {},
                              p,
                              0,
                              isRailBus(symbol.id) ? 420.0f : 0.0f });
        selectedInstance = (int)instances.size() - 1;
        selectedInstances.clear();
        selectedInstances.add(selectedInstance);
        selectedGroup = -1;
        notifySelection();
        if (onStatus) onStatus("Placed " + symbol.title + (snapEnabled ? " at schematic grid." : "."));
    }

    juce::String displayValueFor(const Instance& instance) const
    {
        if (instance.symbolId == "ground" || instance.symbolId == "ground_bus")
            return instance.busName;
        if (instance.symbolId == "power_bus")
            return instance.busName;
        if (instance.symbolId == "ac_voltage_source" || instance.symbolId == "signal_source")
            return instance.value + " @ " + instance.frequency;
        return instance.value;
    }

    void drawSymbolBody(juce::Graphics& g, const Instance& instance, const SymbolDef& symbol)
    {
        const auto selectionBounds = orientedBounds(instance, symbol).expanded(8.0f);
        const auto instanceIndex = (int)(&instance - instances.data());
        if (instanceIndex >= 0 && instanceIndex < (int)instances.size() && isInstanceSelected(instanceIndex))
        {
            g.setColour(juce::Colour(0xffffc857));
            const auto r = selectionBounds;
            const auto s = 8.0f;
            g.drawLine(r.getX(), r.getY(), r.getX() + s, r.getY(), 1.5f);
            g.drawLine(r.getX(), r.getY(), r.getX(), r.getY() + s, 1.5f);
            g.drawLine(r.getRight(), r.getY(), r.getRight() - s, r.getY(), 1.5f);
            g.drawLine(r.getRight(), r.getY(), r.getRight(), r.getY() + s, 1.5f);
            g.drawLine(r.getX(), r.getBottom(), r.getX() + s, r.getBottom(), 1.5f);
            g.drawLine(r.getX(), r.getBottom(), r.getX(), r.getBottom() - s, 1.5f);
            g.drawLine(r.getRight(), r.getBottom(), r.getRight() - s, r.getBottom(), 1.5f);
            g.drawLine(r.getRight(), r.getBottom(), r.getRight(), r.getBottom() - s, 1.5f);

            if (isRailBus(instance.symbolId))
            {
                g.setColour(juce::Colour(0xff0e141a));
                g.fillRoundedRectangle(railHandleBounds(instance, true), 3.0f);
                g.fillRoundedRectangle(railHandleBounds(instance, false), 3.0f);
                g.setColour(juce::Colour(0xffffc857));
                g.drawRoundedRectangle(railHandleBounds(instance, true), 3.0f, 1.5f);
                g.drawRoundedRectangle(railHandleBounds(instance, false), 3.0f, 1.5f);
            }
        }

        if (isRailBus(instance.symbolId))
        {
            drawRailBus(g, instance);
            return;
        }

        g.saveState();
        g.addTransform(juce::AffineTransform::rotation(juce::degreesToRadians((float)instance.rotation))
                           .translated(instance.position.x, instance.position.y));
        schematic::drawSymbolArt(g, symbol, instance.value);
        g.restoreState();

        drawSymbolLabels(g, instance, symbol);
    }

    void drawRailBus(juce::Graphics& g, const Instance& instance)
    {
        const auto ground = instance.symbolId == "ground_bus";
        g.saveState();
        g.addTransform(juce::AffineTransform::rotation(juce::degreesToRadians((float)instance.rotation))
                           .translated(instance.position.x, instance.position.y));
        g.setColour(ground ? juce::Colour(0xff78dcca) : juce::Colour(0xffffc857));
        const auto length = std::max(120.0f, instance.busLength);
        g.drawLine(-length * 0.5f, 0.0f, length * 0.5f, 0.0f, 3.0f);
        for (float x = -length * 0.5f; x <= length * 0.5f + 0.1f; x += 48.0f)
            g.drawLine(x, -7.0f, x, 7.0f, 1.2f);
        g.setFont(juce::Font(12.0f, juce::Font::bold));
        g.drawText(instance.busName.isNotEmpty() ? instance.busName : (ground ? "0" : "PWR"),
                   juce::Rectangle<float>(-length * 0.5f, ground ? 6.0f : -24.0f, length, 18.0f).toNearestInt(),
                   juce::Justification::centredLeft);
        g.restoreState();
    }

    // Reference designator and value beside the body, always reading left
    // to right (SCH-T1). Wide symbols carry labels above and below; tall
    // symbols carry them stacked on the right. Power symbols show only
    // their net name.
    void drawSymbolLabels(juce::Graphics& g, const Instance& instance, const SymbolDef& symbol)
    {
        const auto bounds = orientedBounds(instance, symbol);
        g.setFont(juce::Font(12.0f));

        if (instance.symbolId == "ground")
            return;

        if (instance.symbolId == "power_port")
        {
            const auto pointsDown = schematic::normalizedRotation(instance.rotation) == 180;
            const auto label = instance.busName.isNotEmpty() ? instance.busName : juce::String("VCC");
            g.setColour(juce::Colour(0xffffc857));
            g.setFont(juce::Font(12.0f, juce::Font::bold));
            const auto y = pointsDown ? bounds.getBottom() + 2.0f : bounds.getY() - 16.0f;
            g.drawText(label, juce::Rectangle<float>(bounds.getCentreX() - 40.0f, y, 80.0f, 14.0f).toNearestInt(),
                       juce::Justification::centred);
            return;
        }

        const auto valueText = displayValueFor(instance);
        const auto tall = bounds.getHeight() > bounds.getWidth() * 1.2f;
        if (tall)
        {
            const auto x = (int)bounds.getRight() + 6;
            const auto midY = (int)bounds.getCentreY();
            g.setColour(juce::Colour(0xff93a7b0));
            g.drawText(instance.refdes, x, midY - (valueText.isNotEmpty() ? 16 : 8), 96, 15, juce::Justification::centredLeft);
            if (valueText.isNotEmpty())
            {
                g.setColour(juce::Colour(0xffdce9ee));
                g.drawText(valueText, x, midY + 1, 96, 15, juce::Justification::centredLeft);
            }
            return;
        }

        g.setColour(juce::Colour(0xff93a7b0));
        g.drawText(instance.refdes, (int)bounds.getCentreX() - 60, (int)bounds.getY() - 17, 120, 15,
                   juce::Justification::centred);
        if (valueText.isNotEmpty())
        {
            g.setColour(juce::Colour(0xffdce9ee));
            g.drawText(valueText, (int)bounds.getCentreX() - 60, (int)bounds.getBottom() + 2, 120, 15,
                       juce::Justification::centred);
        }
    }

    void drawInstances(juce::Graphics& g)
    {
        for (int instanceIndex = 0; instanceIndex < (int)instances.size(); ++instanceIndex)
        {
            const auto& instance = instances[(size_t)instanceIndex];
            const auto symbol = symbolFor(instance.symbolId);
            drawSymbolBody(g, instance, symbol);

            if (isRailBus(instance.symbolId))
                continue;

            for (size_t i = 0; i < symbol.pins.size(); ++i)
            {
                const PinRef ref { instanceIndex, (int)i };
                const auto pin = pinPosition(ref);

                // Unconnected pins get a small open marker so dangling ends
                // are visible; connected pins draw nothing (SCH-J3).
                if (wireCountAtPin(ref) == 0)
                {
                    g.setColour(juce::Colour(0xffff8a65));
                    g.drawEllipse(pin.x - 3.0f, pin.y - 3.0f, 6.0f, 6.0f, 1.2f);
                }

                if (symbol.showPinNames)
                {
                    auto toward = instance.position - pin;
                    const auto len = std::sqrt(toward.x * toward.x + toward.y * toward.y);
                    if (len > 0.001f)
                        toward = toward / len;
                    const auto at = pin + toward * 20.0f;
                    g.setColour(juce::Colour(0xff93a7b0));
                    g.setFont(juce::Font(10.0f));
                    g.drawText(symbol.pins[i].name, (int)at.x - 18, (int)at.y - 12, 36, 12, juce::Justification::centred);
                }
            }
        }
    }

    int wireCountAtPin(const PinRef& pin) const
    {
        int count = 0;
        for (const auto& wire : wires)
        {
            if (wire.a.isPin() && wire.a.pin.instanceIndex == pin.instanceIndex && wire.a.pin.pinIndex == pin.pinIndex) ++count;
            if (wire.b.isPin() && wire.b.pin.instanceIndex == pin.instanceIndex && wire.b.pin.pinIndex == pin.pinIndex) ++count;
        }
        return count;
    }

    void drawRightAngleWire(juce::Graphics& g, juce::Point<float> a, juce::Point<float> b, juce::Colour colour, float width)
    {
        const auto midX = std::round((a.x + b.x) * 0.5f / 24.0f) * 24.0f;
        juce::Path path;
        path.startNewSubPath(a);
        path.lineTo(midX, a.y);
        path.lineTo(midX, b.y);
        path.lineTo(b);
        g.setColour(colour);
        g.strokePath(path, juce::PathStrokeType(width));
    }

    void drawRoutedWire(juce::Graphics& g, const std::vector<juce::Point<float>>& points, juce::Colour colour, float width)
    {
        if (points.size() < 2)
            return;

        juce::Path path;
        path.startNewSubPath(points.front());
        for (size_t i = 1; i < points.size(); ++i)
            path.lineTo(points[i]);

        g.setColour(colour);
        g.strokePath(path, juce::PathStrokeType(width));
    }

    void drawWires(juce::Graphics& g)
    {
        for (const auto& wire : wires)
        {
            if (isInternalRailTapWire(wire))
                continue;

            drawRoutedWire(g, routedWirePoints(wire.a, wire.b), juce::Colour(0xfff4d35e), 2.0f);
        }

        g.setColour(juce::Colour(0xffffc857));
        for (int i = 0; i < (int)junctions.size(); ++i)
        {
            int degree = 0;
            for (const auto& wire : wires)
                degree += (wire.a.isJunction() && wire.a.junctionIndex == i ? 1 : 0)
                        + (wire.b.isJunction() && wire.b.junctionIndex == i ? 1 : 0);
            if (degree >= 3)
                g.fillEllipse(junctions[(size_t)i].x - 4.0f, junctions[(size_t)i].y - 4.0f, 8.0f, 8.0f);
        }

        for (int instanceIndex = 0; instanceIndex < (int)instances.size(); ++instanceIndex)
        {
            if (isRailBus(instances[(size_t)instanceIndex].symbolId))
                continue;
            const auto symbol = symbolFor(instances[(size_t)instanceIndex].symbolId);
            for (int p = 0; p < (int)symbol.pins.size(); ++p)
            {
                const PinRef ref { instanceIndex, p };
                if (wireCountAtPin(ref) >= 2)
                {
                    const auto pin = pinPosition(ref);
                    g.fillEllipse(pin.x - 4.0f, pin.y - 4.0f, 8.0f, 8.0f);
                }
            }
        }
    }

    void drawProbes(juce::Graphics& g)
    {
        for (const auto& probe : probes)
        {
            const auto p = nodePosition(probe.node);
            if (p == juce::Point<float>())
                continue;

            g.setColour(probe.colour.withAlpha(0.24f));
            g.fillEllipse(p.x - 13.0f, p.y - 13.0f, 26.0f, 26.0f);
            g.setColour(probe.colour);
            g.drawEllipse(p.x - 10.0f, p.y - 10.0f, 20.0f, 20.0f, 2.0f);
            g.fillEllipse(p.x - 3.0f, p.y - 3.0f, 6.0f, 6.0f);

            auto label = juce::Rectangle<int>((int)p.x + 10, (int)p.y - 20, 64, 18);
            g.setColour(juce::Colour(0xdd0e141a));
            g.fillRoundedRectangle(label.toFloat(), 3.0f);
            g.setColour(probe.colour);
            g.setFont(juce::Font(11.0f, juce::Font::bold));
            g.drawText(probe.label, label.reduced(4, 0), juce::Justification::centredLeft);
        }
    }

    void drawPendingWire(juce::Graphics& g)
    {
        if (!wireDragging || !wireDragStart.isValid()) return;
        drawRoutedWire(g,
                       routedWirePoints(wireDragStart, wireDragPosition, {}),
                       juce::Colour(0x99f4d35e),
                       1.5f);
    }

    /*
    void drawOpAmp(juce::Graphics& g, juce::Rectangle<float> r)
    {
        juce::Path tri;
        tri.startNewSubPath(r.getX(), r.getY());
        tri.lineTo(r.getX(), r.getBottom());
        tri.lineTo(r.getRight(), r.getCentreY());
        tri.closeSubPath();
        g.setColour(juce::Colour(0xff17212b));
        g.fillPath(tri);
        g.setColour(juce::Colour(0xff78dcca));
        g.strokePath(tri, juce::PathStrokeType(2.0f));
        g.setFont(juce::Font(13.0f, juce::Font::bold));
        g.drawText("uA741", r.toNearestInt(), juce::Justification::centred);
        g.drawText("+", (int)r.getX() - 22, (int)r.getY() + 18, 18, 18, juce::Justification::centred);
        g.drawText("-", (int)r.getX() - 22, (int)r.getBottom() - 38, 18, 18, juce::Justification::centred);
    }

    void drawSampleCircuit(juce::Graphics& g)
    {
        const auto c = getLocalBounds().getCentre();
        juce::Point<float> op { (float)c.x - 30.0f, (float)c.y - 55.0f };
        drawOpAmp(g, { op.x, op.y, 130.0f, 110.0f });

        g.setColour(juce::Colour(0xffe8f1f2));
        g.drawLine(op.x - 120.0f, op.y + 28.0f, op.x, op.y + 28.0f, 2.0f);
        g.drawLine(op.x - 120.0f, op.y + 82.0f, op.x, op.y + 82.0f, 2.0f);
        g.drawLine(op.x + 130.0f, op.y + 55.0f, op.x + 245.0f, op.y + 55.0f, 2.0f);

        g.setFont(juce::Font(14.0f, juce::Font::bold));
        g.drawText("IN+", (int)op.x - 170, (int)op.y + 16, 48, 22, juce::Justification::centredRight);
        g.drawText("IN-", (int)op.x - 170, (int)op.y + 70, 48, 22, juce::Justification::centredRight);
        g.drawText("OUT", (int)op.x + 250, (int)op.y + 44, 56, 22, juce::Justification::centredLeft);
    }
    */
};

class InstrumentPanel final : public juce::Component,
                              public juce::Timer
{
public:
    InstrumentPanel()
    {
        title.setText("Lab Bench", juce::dontSendNotification);
        title.setFont(juce::Font(16.0f, juce::Font::bold));
        title.setColour(juce::Label::textColourId, juce::Colour(0xff78dcca));
        addAndMakeVisible(title);

        psuTitle.setText("Programmable Power Supply", juce::dontSendNotification);
        psuTitle.setFont(juce::Font(14.0f, juce::Font::bold));
        psuTitle.setColour(juce::Label::textColourId, juce::Colour(0xffdce9ee));
        addAndMakeVisible(psuTitle);

        mode.addItem("DC", 1);
        mode.addItem("AC", 2);
        mode.setSelectedId(1, juce::dontSendNotification);
        mode.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff253341));
        mode.setColour(juce::ComboBox::textColourId, juce::Colour(0xffdce9ee));
        addAndMakeVisible(mode);

        for (auto* editor : { &positiveNet, &negativeNet, &voltage, &frequency, &currentLimit, &internalResistance })
        {
            styleTextEditor(*editor);
            editor->setMultiLine(false);
            addAndMakeVisible(*editor);
        }

        positiveNet.setText("+9V", juce::dontSendNotification);
        negativeNet.setText("0", juce::dontSendNotification);
        voltage.setText("9", juce::dontSendNotification);
        frequency.setText("60", juce::dontSendNotification);
        currentLimit.setText("100m", juce::dontSendNotification);
        internalResistance.setText("0.2", juce::dontSendNotification);

        positiveNet.setTextToShowWhenEmpty("+9V, +12V, +18V", juce::Colour(0xff71808c));
        negativeNet.setTextToShowWhenEmpty("0", juce::Colour(0xff71808c));
        voltage.setTextToShowWhenEmpty("9", juce::Colour(0xff71808c));
        frequency.setTextToShowWhenEmpty("60", juce::Colour(0xff71808c));
        currentLimit.setTextToShowWhenEmpty("100m", juce::Colour(0xff71808c));
        internalResistance.setTextToShowWhenEmpty("0.2", juce::Colour(0xff71808c));

        outputEnabled.setButtonText("Output enabled");
        outputEnabled.setToggleState(true, juce::dontSendNotification);
        outputEnabled.setColour(juce::ToggleButton::textColourId, juce::Colour(0xffdce9ee));
        addAndMakeVisible(outputEnabled);

        currentLimited.setButtonText("Current limit active");
        currentLimited.setToggleState(true, juce::dontSendNotification);
        currentLimited.setColour(juce::ToggleButton::textColourId, juce::Colour(0xffdce9ee));
        addAndMakeVisible(currentLimited);

        psuPreset.addItem("9V pedal supply", 1);
        psuPreset.addItem("12V pedal supply", 2);
        psuPreset.addItem("18V pedal supply", 3);
        psuPreset.addItem("Dying 9V battery", 4);
        psuPreset.setSelectedId(1, juce::dontSendNotification);
        psuPreset.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff253341));
        psuPreset.setColour(juce::ComboBox::textColourId, juce::Colour(0xffdce9ee));
        psuPreset.onChange = [this] { applySelectedPreset(); };
        addAndMakeVisible(psuPreset);

        dmmTitle.setText("Precision Digital Multimeter", juce::dontSendNotification);
        dmmTitle.setFont(juce::Font(14.0f, juce::Font::bold));
        dmmTitle.setColour(juce::Label::textColourId, juce::Colour(0xffdce9ee));
        addAndMakeVisible(dmmTitle);

        for (const auto& item : { "DC Voltage", "AC Voltage", "DC Current", "AC Current", "Resistance",
                                  "4-Wire Resistance", "Continuity", "Diode", "Capacitance", "Frequency",
                                  "Period", "Duty Cycle", "Temperature", "AC+DC Voltage", "AC+DC Current",
                                  "Ratio", "dB", "dBm", "Digitizer Voltage", "Digitizer Current" })
            dmmFunction.addItem(item, dmmFunction.getNumItems() + 1);
        dmmFunction.setSelectedId(1, juce::dontSendNotification);
        dmmFunction.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff253341));
        dmmFunction.setColour(juce::ComboBox::textColourId, juce::Colour(0xffdce9ee));
        addAndMakeVisible(dmmFunction);

        for (const auto& item : { "Auto", "100 mV", "1 V", "10 V", "100 V", "1000 V",
                                  "1 uA", "100 uA", "1 mA", "10 mA", "100 mA", "1 A", "10 A",
                                  "100 Ohm", "1 kOhm", "10 kOhm", "100 kOhm", "1 MOhm", "100 MOhm",
                                  "1 nF", "10 nF", "100 nF", "1 uF", "100 uF", "Hz" })
            dmmRange.addItem(item, dmmRange.getNumItems() + 1);
        dmmRange.setSelectedId(1, juce::dontSendNotification);
        dmmRange.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff253341));
        dmmRange.setColour(juce::ComboBox::textColourId, juce::Colour(0xffdce9ee));
        addAndMakeVisible(dmmRange);

        for (auto* editor : { &dmmHighNet, &dmmLowNet, &dmmNplc, &dmmSampleRate })
        {
            styleTextEditor(*editor);
            editor->setMultiLine(false);
            addAndMakeVisible(*editor);
        }
        dmmHighNet.setText("probe", juce::dontSendNotification);
        dmmLowNet.setText("bench", juce::dontSendNotification);
        dmmNplc.setText("10", juce::dontSendNotification);
        dmmSampleRate.setText("1000", juce::dontSendNotification);
        dmmHighNet.setReadOnly(true);
        dmmLowNet.setReadOnly(true);

        dmmDisplay.setText("+0.000000 V", juce::dontSendNotification);
        dmmDisplay.setFont(juce::Font(20.0f, juce::Font::bold));
        dmmDisplay.setJustificationType(juce::Justification::centredRight);
        dmmDisplay.setColour(juce::Label::textColourId, juce::Colour(0xff78dcca));
        dmmDisplay.setColour(juce::Label::backgroundColourId, juce::Colour(0xff0e141a));
        addAndMakeVisible(dmmDisplay);

        for (auto* toggle : { &dmmTrueRms, &dmmAutoRange, &dmmHold, &dmmRelative, &dmmMinMax,
                              &dmmPeakMinMax, &dmmLowPass, &dmmLoZ, &dmmContinuityBeep })
        {
            toggle->setColour(juce::ToggleButton::textColourId, juce::Colour(0xffdce9ee));
            addAndMakeVisible(*toggle);
        }
        dmmTrueRms.setToggleState(true, juce::dontSendNotification);
        dmmAutoRange.setToggleState(true, juce::dontSendNotification);
        dmmContinuityBeep.setToggleState(true, juce::dontSendNotification);

        stripRecord.setButtonText("Record");
        stripRecord.setToggleState(true, juce::dontSendNotification);
        stripRecord.setColour(juce::ToggleButton::textColourId, juce::Colour(0xffdce9ee));
        addAndMakeVisible(stripRecord);

        stripScale.addItem("Per channel", 1);
        stripScale.addItem("Shared", 2);
        stripScale.setSelectedId(1, juce::dontSendNotification);
        stripScale.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff253341));
        stripScale.setColour(juce::ComboBox::textColourId, juce::Colour(0xffdce9ee));
        addAndMakeVisible(stripScale);

        startTimerHz(5);
    }

    void setProbeTarget(const juce::String& id, const juce::String& target)
    {
        if (id == "DMM_HI")
            dmmHighNet.setText(target.isEmpty() ? "bench" : target, juce::dontSendNotification);
        else if (id == "DMM_LO")
            dmmLowNet.setText(target.isEmpty() ? "bench" : target, juce::dontSendNotification);
        else if (id == "SCOPE_CH1")
            scopeCh1Target = target;
        else if (id == "SCOPE_CH2")
            scopeCh2Target = target;

        repaint();
    }

    juce::String buildInstrumentJson() const
    {
        juce::String text;
        text << "{\n";
        text << "  \"schemaVersion\": 1,\n";
        text << "  \"kind\": \"djehuti_lab_instruments\",\n";
        text << "  \"instruments\": [\n";
        text << "    {\n";
        text << "      \"id\": \"PSU1\",\n";
        text << "      \"type\": \"programmable_power_supply\",\n";
        text << "      \"mode\": " << quote(mode.getText().toLowerCase()) << ",\n";
        text << "      \"channels\": [\n";
        text << "        {\n";
        text << "          \"name\": \"CH1\",\n";
        text << "          \"positiveNet\": " << quote(positiveNet.getText().trim()) << ",\n";
        text << "          \"negativeNet\": " << quote(negativeNet.getText().trim()) << ",\n";
        text << "          \"voltage\": " << quote(voltage.getText().trim() + "V") << ",\n";
        text << "          \"frequency\": " << quote(frequency.getText().trim() + "Hz") << ",\n";
        text << "          \"currentLimit\": " << quote(currentLimit.getText().trim() + "A") << ",\n";
        text << "          \"currentLimitEnabled\": " << (currentLimited.getToggleState() ? "true" : "false") << ",\n";
        text << "          \"internalResistance\": " << quote(internalResistance.getText().trim() + "ohm") << ",\n";
        text << "          \"enabled\": " << (outputEnabled.getToggleState() ? "true" : "false") << "\n";
        text << "        }\n";
        text << "      ]\n";
        text << "    },\n";
        text << "    {\n";
        text << "      \"id\": \"DMM1\",\n";
        text << "      \"type\": \"precision_digital_multimeter\",\n";
        text << "      \"function\": " << quote(dmmFunction.getText()) << ",\n";
        text << "      \"range\": " << quote(dmmRange.getText()) << ",\n";
        text << "      \"connections\": {\n";
        text << "        \"high\": " << quote(dmmHighNet.getText().trim()) << ",\n";
        text << "        \"low\": " << quote(dmmLowNet.getText().trim()) << "\n";
        text << "      },\n";
        text << "      \"features\": {\n";
        text << "        \"trueRms\": " << (dmmTrueRms.getToggleState() ? "true" : "false") << ",\n";
        text << "        \"autoRange\": " << (dmmAutoRange.getToggleState() ? "true" : "false") << ",\n";
        text << "        \"hold\": " << (dmmHold.getToggleState() ? "true" : "false") << ",\n";
        text << "        \"relative\": " << (dmmRelative.getToggleState() ? "true" : "false") << ",\n";
        text << "        \"minMaxRecording\": " << (dmmMinMax.getToggleState() ? "true" : "false") << ",\n";
        text << "        \"peakMinMax\": " << (dmmPeakMinMax.getToggleState() ? "true" : "false") << ",\n";
        text << "        \"lowPassFilter\": " << (dmmLowPass.getToggleState() ? "true" : "false") << ",\n";
        text << "        \"lowImpedanceMode\": " << (dmmLoZ.getToggleState() ? "true" : "false") << ",\n";
        text << "        \"continuityBeep\": " << (dmmContinuityBeep.getToggleState() ? "true" : "false") << "\n";
        text << "      },\n";
        text << "      \"acquisition\": {\n";
        text << "        \"nplc\": " << quote(dmmNplc.getText().trim()) << ",\n";
        text << "        \"sampleRate\": " << quote(dmmSampleRate.getText().trim() + "Sa/s") << ",\n";
        text << "        \"digitizer\": { \"enabled\": true, \"resolutionBits\": 16 },\n";
        text << "        \"slowRecording\": { \"enabled\": " << (stripRecord.getToggleState() ? "true" : "false")
             << ", \"sampleRateHz\": 5, \"stripChart\": { \"enabled\": true, \"scaleMode\": "
             << quote(stripScale.getSelectedId() == 2 ? "shared" : "per_channel") << ", \"windowSeconds\": 60 } },\n";
        text << "        \"statistics\": [\"min\", \"max\", \"average\", \"peakToPeak\", \"standardDeviation\"],\n";
        text << "        \"logging\": true,\n";
        text << "        \"graphing\": true,\n";
        text << "        \"displayViews\": [\"numeric\", \"trend\", \"histogram\", \"bar\", \"waveform\", \"strip_chart\"]\n";
        text << "      }\n";
        text << "    },\n";
        text << "    {\n";
        text << "      \"id\": \"SCOPE1\",\n";
        text << "      \"type\": \"digital_oscilloscope\",\n";
        text << "      \"channels\": [\n";
        text << "        { \"name\": \"CH1\", \"target\": " << quote(scopeCh1Target.isEmpty() ? "bench" : scopeCh1Target)
             << ", \"colour\": \"#c86a6a\" },\n";
        text << "        { \"name\": \"CH2\", \"target\": " << quote(scopeCh2Target.isEmpty() ? "bench" : scopeCh2Target)
             << ", \"colour\": \"#6fac7d\" }\n";
        text << "      ],\n";
        text << "      \"display\": { \"view\": \"strip_chart\", \"grid\": true, \"recording\": "
             << (stripRecord.getToggleState() ? "true" : "false") << ", \"scaleMode\": "
             << quote(stripScale.getSelectedId() == 2 ? "shared" : "per_channel")
             << ", \"windowSeconds\": 60, \"channelsExpandable\": true }\n";
        text << "    }\n";
        text << "  ]\n";
        text << "}\n";
        return text;
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff10161d));
        auto area = getLocalBounds().reduced(12);
        drawZone(g, psuZone, "Power Source");
        drawZone(g, dmmZone, "Meter Setup");
        drawZone(g, meterOptionsZone, "Meter Options");
        drawZone(g, scopeZone, "Scope Preview");

        g.setColour(juce::Colour(0xff93a7b0));
        g.setFont(juce::Font(11.5f, juce::Font::bold));
        g.drawText("Mode", 12, 64, 78, 16, juce::Justification::centredLeft);
        g.drawText("Preset", 380, 64, 150, 16, juce::Justification::centredLeft);
        g.drawText("+ net", 12, 104, 110, 16, juce::Justification::centredLeft);
        g.drawText("- net", 130, 104, 110, 16, juce::Justification::centredLeft);
        g.drawText("Volts", 12, 160, 92, 16, juce::Justification::centredLeft);
        g.drawText("Freq", 112, 160, 92, 16, juce::Justification::centredLeft);
        g.drawText("Limit", 212, 160, 92, 16, juce::Justification::centredLeft);
        g.drawText("Internal R", 312, 160, 92, 16, juce::Justification::centredLeft);
        g.drawText("Function", 12, 240, 150, 16, juce::Justification::centredLeft);
        g.drawText("Range", 170, 240, 86, 16, juce::Justification::centredLeft);
        g.drawText("Leads", 12, 280, 228, 16, juce::Justification::centredLeft);
        g.drawText("NPLC", 248, 280, 70, 16, juce::Justification::centredLeft);
        g.drawText("Sa/s", 326, 280, 90, 16, juce::Justification::centredLeft);

        drawProbeLead(g, dmmHiLead, "DMM+", dmmLeadColour(true), dmmHighNet.getText().trim());
        drawProbeLead(g, dmmLoLead, "DMM-", dmmLeadColour(false), dmmLowNet.getText().trim());
        drawProbeLead(g, scopeCh1Lead, "CH1", scopeChannelColour(0), scopeCh1Target);
        drawProbeLead(g, scopeCh2Lead, "CH2", scopeChannelColour(1), scopeCh2Target);

        area.removeFromTop(scopeZone.getY() - 12);
        g.setColour(juce::Colour(0xffdce9ee));
        g.setFont(juce::Font(15.0f, juce::Font::bold));
        g.drawText("Slow Strip Recorder", area.removeFromTop(24), juce::Justification::centredLeft);
        drawStripChart(g, area.reduced(0, 10).toFloat());
    }

    void timerCallback() override
    {
        if (!stripRecord.getToggleState())
            return;

        stripTimeSeconds += 0.2;

        const auto t = stripTimeSeconds;
        StripSample sample;
        sample.timeSeconds = t;
        sample.dmmHi = (float)(1.8 + std::sin(t * 0.42) * 0.38 + std::sin(t * 0.07) * 0.18);
        sample.dmmLo = (float)(std::sin(t * 0.11) * 0.018);
        sample.scopeCh1 = (float)(std::sin(t * 1.15) * 1.6 + std::sin(t * 0.18) * 0.42);
        sample.scopeCh2 = (float)(std::cos(t * 0.77) * 0.95 + std::sin(t * 0.31) * 0.35);
        stripSamples.push_back(sample);

        while (!stripSamples.empty() && stripSamples.front().timeSeconds < stripTimeSeconds - stripWindowSeconds)
            stripSamples.erase(stripSamples.begin());

        dmmDisplay.setText(juce::String(sample.dmmHi - sample.dmmLo, 3) + " V", juce::dontSendNotification);
        repaint(scopeZone);
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        const auto p = event.getPosition();
        const auto probeId = probeAt(p);
        if (probeId.isEmpty())
            return;

        if (probeIsInUse(probeId))
            return;

        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this))
        {
            showCursorForEvent(event, juce::MouseCursor::DraggingHandCursor);
            container->startDragging("probe:" + probeId, this);
        }
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        showCursorForEvent(event, juce::MouseCursor::NormalCursor);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(12);
        title.setBounds(area.removeFromTop(24));
        area.removeFromTop(6);
        psuTitle.setBounds(area.removeFromTop(22));
        psuZone = juce::Rectangle<int>(8, 44, getWidth() - 16, 166);

        auto topRow = area.removeFromTop(28);
        addField(topRow, mode, 78);
        topRow.removeFromLeft(8);
        outputEnabled.setBounds(topRow.removeFromLeft(140));
        currentLimited.setBounds(topRow.removeFromLeft(150));
        topRow.removeFromLeft(8);
        psuPreset.setBounds(topRow.removeFromLeft(170));

        area.removeFromTop(8);
        auto nets = area.removeFromTop(48);
        layoutEditor(nets, "Positive net", positiveNet);
        nets.removeFromLeft(8);
        layoutEditor(nets, "Negative net", negativeNet);

        area.removeFromTop(8);
        auto electrical = area.removeFromTop(48);
        layoutEditor(electrical, "Voltage", voltage);
        electrical.removeFromLeft(8);
        layoutEditor(electrical, "Frequency", frequency);
        electrical.removeFromLeft(8);
        layoutEditor(electrical, "Current limit", currentLimit);
        electrical.removeFromLeft(8);
        layoutEditor(electrical, "Internal R", internalResistance);

        area.removeFromTop(8);
        area.removeFromTop(18);
        dmmZone = juce::Rectangle<int>(8, area.getY() - 8, getWidth() - 16, 112);
        dmmTitle.setBounds(area.removeFromTop(22));
        auto dmmTop = area.removeFromTop(32);
        dmmFunction.setBounds(dmmTop.removeFromLeft(150));
        dmmTop.removeFromLeft(8);
        dmmRange.setBounds(dmmTop.removeFromLeft(86));
        dmmTop.removeFromLeft(8);
        dmmDisplay.setBounds(dmmTop.removeFromLeft(190));

        area.removeFromTop(8);
        auto dmmNets = area.removeFromTop(32);
        dmmHiLead = dmmNets.removeFromLeft(52);
        dmmNets.removeFromLeft(6);
        dmmLoLead = dmmNets.removeFromLeft(52);
        dmmNets.removeFromLeft(8);
        dmmHighNet.setBounds(dmmNets.removeFromLeft(110));
        dmmNets.removeFromLeft(8);
        dmmLowNet.setBounds(dmmNets.removeFromLeft(110));
        dmmNets.removeFromLeft(8);
        dmmNplc.setBounds(dmmNets.removeFromLeft(70));
        dmmNets.removeFromLeft(8);
        dmmSampleRate.setBounds(dmmNets.removeFromLeft(90));

        area.removeFromTop(12);
        meterOptionsZone = juce::Rectangle<int>(8, area.getY() - 6, getWidth() - 16, 48);
        auto toggles = area.removeFromTop(58);
        dmmTrueRms.setBounds(toggles.removeFromLeft(96));
        dmmAutoRange.setBounds(toggles.removeFromLeft(104));
        dmmHold.setBounds(toggles.removeFromLeft(72));
        dmmRelative.setBounds(toggles.removeFromLeft(82));
        dmmMinMax.setBounds(toggles.removeFromLeft(88));
        dmmPeakMinMax.setBounds(toggles.removeFromLeft(92));
        dmmLowPass.setBounds(toggles.removeFromLeft(92));
        dmmLoZ.setBounds(toggles.removeFromLeft(72));
        dmmContinuityBeep.setBounds(toggles.removeFromLeft(80));
        area.removeFromTop(10);
        scopeZone = juce::Rectangle<int>(8, area.getY(), getWidth() - 16, getHeight() - area.getY() - 8);
        auto scopeLeads = scopeZone.reduced(10).removeFromTop(34).removeFromRight(126);
        scopeCh1Lead = scopeLeads.removeFromLeft(58);
        scopeLeads.removeFromLeft(10);
        scopeCh2Lead = scopeLeads.removeFromLeft(58);

        auto scopeControls = scopeZone.reduced(10).removeFromTop(34);
        scopeControls.removeFromLeft(150);
        stripRecord.setBounds(scopeControls.removeFromLeft(84));
        scopeControls.removeFromLeft(8);
        stripScale.setBounds(scopeControls.removeFromLeft(128));
    }

private:
    struct StripSample
    {
        double timeSeconds = 0.0;
        float dmmHi = 0.0f;
        float dmmLo = 0.0f;
        float scopeCh1 = 0.0f;
        float scopeCh2 = 0.0f;
    };

    static juce::String quote(const juce::String& text)
    {
        return "\"" + text.replace("\\", "\\\\").replace("\"", "\\\"") + "\"";
    }

    static void addField(juce::Rectangle<int>& area, juce::Component& component, int width)
    {
        component.setBounds(area.removeFromLeft(width));
    }

    static void layoutEditor(juce::Rectangle<int>& area, const juce::String& label, juce::TextEditor& editor)
    {
        auto column = area.removeFromLeft(std::max(92, area.getWidth() / 4));
        juce::ignoreUnused(label);
        editor.setBounds(column.removeFromBottom(28));
    }

    static void drawZone(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& label)
    {
        if (area.isEmpty())
            return;

        auto r = area.toFloat();
        g.setColour(juce::Colour(0xff121a22));
        g.fillRoundedRectangle(r, 5.0f);
        g.setColour(juce::Colour(0xff31404b));
        g.drawRoundedRectangle(r, 5.0f, 1.0f);
        g.setColour(juce::Colour(0xff78dcca));
        g.setFont(juce::Font(11.5f, juce::Font::bold));
        g.drawText(label, area.reduced(8, 2).removeFromTop(16), juce::Justification::centredLeft);
    }

    static juce::Colour disabledProbeColour(juce::Colour colour)
    {
        return colour.withAlpha(0.22f);
    }

    void drawProbeLead(juce::Graphics& g,
                       juce::Rectangle<int> area,
                       const juce::String& label,
                       juce::Colour colour,
                       const juce::String& target)
    {
        if (area.isEmpty())
            return;

        const auto inUse = target.isNotEmpty() && target != "bench" && target != "probe";
        const auto activeColour = inUse ? disabledProbeColour(colour) : colour;
        auto body = area.toFloat().reduced(2.0f);
        g.setColour(juce::Colour(0xff0e141a));
        g.fillRoundedRectangle(body, 7.0f);
        g.setColour(activeColour);
        g.drawRoundedRectangle(body, 7.0f, 1.5f);

        const auto jack = juce::Rectangle<float>(body.getX() + 7.0f, body.getCentreY() - 5.0f, 10.0f, 10.0f);
        g.setColour(activeColour);
        g.drawEllipse(jack, 1.6f);
        if (!inUse)
            g.fillEllipse(jack.reduced(3.0f));

        g.setFont(juce::Font(11.0f, juce::Font::bold));
        g.setColour(activeColour.contrasting(0.82f));
        g.drawText(inUse ? "out" : label, area.withTrimmedLeft(20), juce::Justification::centredLeft, true);
    }

    void drawStripChart(juce::Graphics& g, juce::Rectangle<float> graph)
    {
        if (graph.isEmpty())
            return;

        auto plot = graph.withTrimmedLeft(82.0f).reduced(0.0f, 4.0f);
        g.setColour(juce::Colour(0xff0d1319));
        g.fillRoundedRectangle(graph, 5.0f);
        g.setColour(juce::Colour(0xff26323d));
        g.drawRoundedRectangle(graph, 5.0f, 1.0f);

        for (int i = 0; i <= 6; ++i)
        {
            const auto x = plot.getX() + plot.getWidth() * (float)i / 6.0f;
            g.setColour(i == 6 ? juce::Colour(0xff465866) : juce::Colour(0xff22303a));
            g.drawVerticalLine((int)x, plot.getY(), plot.getBottom());
        }

        for (int i = 1; i < 4; ++i)
        {
            const auto y = plot.getY() + plot.getHeight() * (float)i / 4.0f;
            g.setColour(juce::Colour(0xff22303a));
            g.drawHorizontalLine((int)y, plot.getX(), plot.getRight());
        }

        const Channel channels[] = {
            { "DMM+", dmmLeadColour(true), getProbeDisplayTarget(dmmHighNet.getText().trim()), &StripSample::dmmHi, "V" },
            { "DMM-", dmmLeadColour(false), getProbeDisplayTarget(dmmLowNet.getText().trim()), &StripSample::dmmLo, "V" },
            { "CH1", scopeChannelColour(0), getProbeDisplayTarget(scopeCh1Target), &StripSample::scopeCh1, "V" },
            { "CH2", scopeChannelColour(1), getProbeDisplayTarget(scopeCh2Target), &StripSample::scopeCh2, "V" }
        };

        g.setFont(juce::Font(11.0f, juce::Font::bold));
        const auto laneHeight = plot.getHeight() / 4.0f;
        float sharedAbs = 0.001f;
        if (stripScale.getSelectedId() == 2)
        {
            for (const auto& sample : stripSamples)
                for (const auto& channel : channels)
                    sharedAbs = std::max(sharedAbs, std::abs(sample.*(channel.member)));
        }

        for (int i = 0; i < 4; ++i)
        {
            const auto lane = juce::Rectangle<float>(plot.getX(), plot.getY() + laneHeight * (float)i,
                                                     plot.getWidth(), laneHeight);
            drawStripChannel(g, lane, graph.getX(), channels[i], sharedAbs);
        }

        g.setColour(juce::Colour(0xff93a7b0));
        g.setFont(juce::Font(10.5f));
        g.drawText("60 s", (int)plot.getX(), (int)plot.getBottom() - 18, 60, 14, juce::Justification::centredLeft);
        g.drawText("now", (int)plot.getRight() - 42, (int)plot.getBottom() - 18, 40, 14, juce::Justification::centredRight);
    }

    struct Channel
    {
        const char* label;
        juce::Colour colour;
        juce::String target;
        float StripSample::* member;
        const char* unit;
    };

    void drawStripChannel(juce::Graphics& g,
                          juce::Rectangle<float> lane,
                          float labelLeft,
                          const Channel& channel,
                          float sharedAbs)
    {
        const auto centreY = lane.getCentreY();
        g.setColour(juce::Colour(0xff1b2730));
        g.drawHorizontalLine((int)centreY, lane.getX(), lane.getRight());

        g.setColour(channel.colour);
        g.fillRoundedRectangle(labelLeft + 10.0f, centreY - 5.0f, 10.0f, 10.0f, 2.0f);
        g.setFont(juce::Font(10.5f, juce::Font::bold));
        g.drawText(channel.label, (int)labelLeft + 26, (int)lane.getY() + 4, 48, 14, juce::Justification::centredLeft);

        g.setColour(juce::Colour(0xff93a7b0));
        g.setFont(juce::Font(9.5f));
        g.drawText(channel.target, (int)labelLeft + 26, (int)lane.getY() + 18, 52, 14, juce::Justification::centredLeft, true);

        if (stripSamples.size() < 2)
            return;

        float peak = sharedAbs;
        if (stripScale.getSelectedId() != 2)
        {
            peak = 0.001f;
            for (const auto& sample : stripSamples)
                peak = std::max(peak, std::abs(sample.*(channel.member)));
        }

        g.setColour(juce::Colour(0xff6f7f89));
        g.drawText("+/-" + juce::String(peak, 2) + channel.unit,
                   (int)labelLeft + 26, (int)lane.getBottom() - 16, 52, 12, juce::Justification::centredLeft, true);

        juce::Path trace;
        bool started = false;
        const auto startTime = stripTimeSeconds - stripWindowSeconds;
        for (const auto& sample : stripSamples)
        {
            const auto x = lane.getX() + (float)((sample.timeSeconds - startTime) / stripWindowSeconds) * lane.getWidth();
            const auto normalized = juce::jlimit(-1.0f, 1.0f, (sample.*(channel.member)) / peak);
            const auto y = centreY - normalized * lane.getHeight() * 0.38f;
            if (!started)
            {
                trace.startNewSubPath(x, y);
                started = true;
            }
            else
            {
                trace.lineTo(x, y);
            }
        }

        g.setColour(channel.colour.withAlpha(0.88f));
        g.strokePath(trace, juce::PathStrokeType(1.8f));
    }

    static juce::String getProbeDisplayTarget(const juce::String& target)
    {
        if (target.isEmpty() || target == "bench" || target == "probe")
            return "floating";
        return target;
    }

    juce::String probeAt(juce::Point<int> p) const
    {
        if (dmmHiLead.contains(p)) return "DMM_HI";
        if (dmmLoLead.contains(p)) return "DMM_LO";
        if (scopeCh1Lead.contains(p)) return "SCOPE_CH1";
        if (scopeCh2Lead.contains(p)) return "SCOPE_CH2";
        return {};
    }

    bool probeIsInUse(const juce::String& id) const
    {
        if (id == "DMM_HI") return !isProbeHome(dmmHighNet.getText().trim());
        if (id == "DMM_LO") return !isProbeHome(dmmLowNet.getText().trim());
        if (id == "SCOPE_CH1") return scopeCh1Target.isNotEmpty();
        if (id == "SCOPE_CH2") return scopeCh2Target.isNotEmpty();
        return true;
    }

    static bool isProbeHome(const juce::String& target)
    {
        return target.isEmpty() || target == "bench" || target == "probe";
    }

    void applySelectedPreset()
    {
        switch (psuPreset.getSelectedId())
        {
            case 2: applyPreset("12", "100m", "0.15"); break;
            case 3: applyPreset("18", "100m", "0.15"); break;
            case 4: applyPreset("6.8", "35m", "25"); break;
            default: applyPreset("9", "100m", "0.2"); break;
        }
    }

    void applyPreset(const juce::String& volts, const juce::String& amps, const juce::String& resistance)
    {
        mode.setSelectedId(1, juce::dontSendNotification);
        positiveNet.setText("+" + volts + "V", juce::dontSendNotification);
        negativeNet.setText("0", juce::dontSendNotification);
        voltage.setText(volts, juce::dontSendNotification);
        currentLimit.setText(amps, juce::dontSendNotification);
        internalResistance.setText(resistance, juce::dontSendNotification);
    }

    juce::Label title;
    juce::Label psuTitle;
    juce::ComboBox mode;
    juce::TextEditor positiveNet;
    juce::TextEditor negativeNet;
    juce::TextEditor voltage;
    juce::TextEditor frequency;
    juce::TextEditor currentLimit;
    juce::TextEditor internalResistance;
    juce::ToggleButton outputEnabled;
    juce::ToggleButton currentLimited;
    juce::ComboBox psuPreset;
    juce::Label dmmTitle;
    juce::ComboBox dmmFunction;
    juce::ComboBox dmmRange;
    juce::Label dmmDisplay;
    juce::TextEditor dmmHighNet;
    juce::TextEditor dmmLowNet;
    juce::TextEditor dmmNplc;
    juce::TextEditor dmmSampleRate;
    juce::ToggleButton dmmTrueRms { "True RMS" };
    juce::ToggleButton dmmAutoRange { "Auto" };
    juce::ToggleButton dmmHold { "Hold" };
    juce::ToggleButton dmmRelative { "Rel" };
    juce::ToggleButton dmmMinMax { "Min/Max" };
    juce::ToggleButton dmmPeakMinMax { "Peak" };
    juce::ToggleButton dmmLowPass { "LPF" };
    juce::ToggleButton dmmLoZ { "LoZ" };
    juce::ToggleButton dmmContinuityBeep { "Beep" };
    juce::ToggleButton stripRecord;
    juce::ComboBox stripScale;
    juce::String scopeCh1Target;
    juce::String scopeCh2Target;
    std::vector<StripSample> stripSamples;
    double stripTimeSeconds = 0.0;
    static constexpr double stripWindowSeconds = 60.0;
    juce::Rectangle<int> dmmHiLead;
    juce::Rectangle<int> dmmLoLead;
    juce::Rectangle<int> scopeCh1Lead;
    juce::Rectangle<int> scopeCh2Lead;
    juce::Rectangle<int> psuZone;
    juce::Rectangle<int> dmmZone;
    juce::Rectangle<int> meterOptionsZone;
    juce::Rectangle<int> scopeZone;
};

class FloatingInstrumentWindow final : public juce::DocumentWindow
{
public:
    explicit FloatingInstrumentWindow(const juce::String& name)
        : DocumentWindow(name, juce::Colour(0xff171b20), juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        setResizable(true, true);
        setResizeLimits(680, 480, 2200, 1400);
    }

    void closeButtonPressed() override
    {
        setVisible(false);
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FloatingInstrumentWindow)
};

class ConsolePanel final : public juce::Component
{
public:
    ConsolePanel(juce::TextEditor*& externalLog)
    {
        styleTextEditor(console, true);
        console.setReadOnly(false);
        console.setText("// Embedded Frust math console research stub\n"
                        "// Future: circuit API, datasets, FFT, solvers, plots.\n\n"
                        "> ");
        externalLog = &console;
        addAndMakeVisible(console);
    }

    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff10161d)); }
    void resized() override { console.setBounds(getLocalBounds().reduced(6)); }

private:
    juce::TextEditor console;
};

class PropertiesPanel final : public juce::Component
{
public:
    PropertiesPanel()
    {
        title.setText("Properties", juce::dontSendNotification);
        title.setFont(juce::Font(16.0f, juce::Font::bold));
        title.setColour(juce::Label::textColourId, juce::Colour(0xff78dcca));
        addAndMakeVisible(title);

        for (auto* label : { &selectedLabel, &valueLabel, &frequencyLabel, &busLabel, &familyLabel, &manufacturerLabel })
        {
            label->setFont(juce::Font(12.5f, juce::Font::bold));
            label->setColour(juce::Label::textColourId, juce::Colour(0xff93a7b0));
            addAndMakeVisible(*label);
        }

        selectedLabel.setText("Selected", juce::dontSendNotification);
        valueLabel.setText("Value / amplitude", juce::dontSendNotification);
        frequencyLabel.setText("Frequency", juce::dontSendNotification);
        busLabel.setText("Bus / net name", juce::dontSendNotification);
        familyLabel.setText("Part family", juce::dontSendNotification);
        manufacturerLabel.setText("Manufacturer part", juce::dontSendNotification);

        for (auto* editor : { &selected, &value, &frequency, &busName, &family, &manufacturerPart })
        {
            styleTextEditor(*editor);
            editor->setMultiLine(false);
            addAndMakeVisible(*editor);
        }

        selected.setReadOnly(true);
        selected.setTextToShowWhenEmpty("Select a placed component", juce::Colour(0xff71808c));
        value.setTextToShowWhenEmpty("10k, 1u, 9, 1...", juce::Colour(0xff71808c));
        frequency.setTextToShowWhenEmpty("1k", juce::Colour(0xff71808c));
        busName.setTextToShowWhenEmpty("+5V, +12V, VREF, 0", juce::Colour(0xff71808c));
        family.setTextToShowWhenEmpty("2N2222, LM741, NE555...", juce::Colour(0xff71808c));
        manufacturerPart.setTextToShowWhenEmpty("vendor-specific MPN", juce::Colour(0xff71808c));

        apply.setButtonText("Apply");
        apply.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff253341));
        apply.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffdce9ee));
        apply.onClick = [this] {
            if (onApply != nullptr && selectedIndex >= 0)
                onApply(value.getText(), frequency.getText(), busName.getText(), family.getText(), manufacturerPart.getText());
        };
        addAndMakeVisible(apply);

        rotate.setButtonText("Rotate 90");
        rotate.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff253341));
        rotate.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffdce9ee));
        rotate.onClick = [this] {
            if (onRotate != nullptr && selectedIndex >= 0)
                onRotate();
        };
        addAndMakeVisible(rotate);

        setSelection(-1, {}, {}, {}, {}, {}, {}, {});
    }

    void setSelection(int index,
                      const juce::String& refdes,
                      const juce::String& symbol,
                      const juce::String& newValue,
                      const juce::String& newFrequency,
                      const juce::String& newBusName,
                      const juce::String& newFamily,
                      const juce::String& newManufacturerPart)
    {
        selectedIndex = index;
        const auto hasSelection = selectedIndex >= 0;
        selected.setText(hasSelection ? refdes + "  (" + symbol + ")" : juce::String(), juce::dontSendNotification);
        value.setText(newValue, juce::dontSendNotification);
        frequency.setText(newFrequency, juce::dontSendNotification);
        busName.setText(newBusName, juce::dontSendNotification);
        family.setText(newFamily, juce::dontSendNotification);
        manufacturerPart.setText(newManufacturerPart, juce::dontSendNotification);

        for (auto* editor : { &value, &frequency, &busName, &family, &manufacturerPart })
            editor->setEnabled(hasSelection);
        apply.setEnabled(hasSelection);
        rotate.setEnabled(hasSelection);
    }

    std::function<void(juce::String, juce::String, juce::String, juce::String, juce::String)> onApply;
    std::function<void()> onRotate;

    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff151a20)); }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8);
        title.setBounds(area.removeFromTop(24));
        area.removeFromTop(8);
        layoutRow(area, selectedLabel, selected);
        layoutRow(area, valueLabel, value);
        layoutRow(area, frequencyLabel, frequency);
        layoutRow(area, busLabel, busName);
        layoutRow(area, familyLabel, family);
        layoutRow(area, manufacturerLabel, manufacturerPart);
        area.removeFromTop(8);
        auto buttons = area.removeFromTop(30);
        apply.setBounds(buttons.removeFromLeft(90));
        buttons.removeFromLeft(8);
        rotate.setBounds(buttons.removeFromLeft(110));
    }

private:
    static void layoutRow(juce::Rectangle<int>& area, juce::Label& label, juce::TextEditor& editor)
    {
        label.setBounds(area.removeFromTop(18));
        editor.setBounds(area.removeFromTop(28));
        area.removeFromTop(8);
    }

    int selectedIndex = -1;
    juce::Label title;
    juce::Label selectedLabel;
    juce::Label valueLabel;
    juce::Label frequencyLabel;
    juce::Label busLabel;
    juce::Label familyLabel;
    juce::Label manufacturerLabel;
    juce::TextEditor selected;
    juce::TextEditor value;
    juce::TextEditor frequency;
    juce::TextEditor busName;
    juce::TextEditor family;
    juce::TextEditor manufacturerPart;
    juce::TextButton apply;
    juce::TextButton rotate;
};

class AgentPanel final : public juce::Component
{
public:
    using ExternalCompletion = LocalAgentApi::Completion;

    struct HostTools
    {
        std::function<juce::String()> inspectCircuit;
        std::function<juce::String()> runErc;
        std::function<juce::String()> exportArtifacts;
        std::function<juce::String(const juce::String&, const juce::String&)> writeMarkdown;
        std::function<juce::String(const juce::String&, int)> webSearch;
        std::function<juce::String(const juce::String&, float, float, const juce::String&,
                                   const juce::String&, const juce::String&)> placeSymbol;
        std::function<juce::String(const juce::String&, const juce::String&, const juce::String&,
                                   const juce::String&, const juce::String&, const juce::String&)> setComponentProperties;
        std::function<juce::String(const juce::String&, const juce::String&)> connectNodes;
        std::function<juce::String(const juce::String&)> openInstrument;
        std::function<juce::String(double, double)> designHighPass;
        std::function<juce::String()> designPushPull;
        std::function<juce::String()> autoLayout;
        std::function<juce::String()> autoLayoutSelection;
        std::function<juce::String(const juce::String&, int)> cookbookLookup;
        std::function<juce::String()> cookbookCoverage;
        std::function<juce::String()> cookbookValidate;
        std::function<juce::String(const juce::String&)> cookbookAcceptanceGoals;
        std::function<juce::String(const juce::String&)> cookbookAcceptanceSummary;
        std::function<juce::String(const juce::String&)> cookbookAcceptanceStart;
        std::function<juce::String(const juce::String&, const juce::String&, const juce::String&,
                                   const juce::String&, const juce::String&, const juce::String&)> cookbookAcceptanceRecord;
        std::function<juce::String(const juce::String&, const juce::String&, const juce::String&,
                                   const juce::String&, const juce::String&, const juce::String&)> capabilityGapRecord;
        std::function<juce::String()> toolManifest;
        std::function<void(const juce::String&)> log;
    };

    explicit AgentPanel(HostTools hostTools)
        : tools(std::move(hostTools)),
          aiConfig(aiConfigFile().getFullPathName().toStdString()),
          transcriptBrowser(juce::WebBrowserComponent::Options()
              .withBackend(juce::WebBrowserComponent::Options::Backend::webview2)
              .withKeepPageLoadedWhenBrowserIsHidden())
    {
        title.setText("BYOK Electronics Agent", juce::dontSendNotification);
        title.setFont(juce::Font(16.0f, juce::Font::bold));
        title.setColour(juce::Label::textColourId, juce::Colour(0xff78dcca));
        addAndMakeVisible(title);

        profileBox.setTooltip("AI account/profile");
        profileBox.onChange = [this] { refreshModelList(); };
        addAndMakeVisible(profileBox);

        modelBox.setEditableText(true);
        modelBox.setTooltip("Model used by the selected BYOK profile");
        addAndMakeVisible(modelBox);

        settingsButton.setButtonText("Settings");
        settingsButton.onClick = [this] { showAiSettingsForSelected(); };
        addAndMakeVisible(settingsButton);

        cardsButton.setButtonText("Cards");
        cardsButton.onClick = [this] { showRetrievedCardsForDraft(); };
        addAndMakeVisible(cardsButton);

        ercButton.setButtonText("Run ERC");
        ercButton.onClick = [this] {
            appendTranscript("tool", executeToolNow(toolCall("circuit_run_erc", "{}")));
        };
        addAndMakeVisible(ercButton);

        exportButton.setButtonText("Export");
        exportButton.onClick = [this] {
            appendTranscript("tool", executeToolNow(toolCall("simulation_export_artifacts", "{}")));
        };
        addAndMakeVisible(exportButton);

        transcriptMarkdown = "BYOK assistant ready.\n";
        addAndMakeVisible(transcriptBrowser);

        styleTextEditor(input);
        input.setTextToShowWhenEmpty("Ask about the circuit, place an instrument node, run ERC...", juce::Colour(0xff71808c));
        input.setMultiLine(true);
        addAndMakeVisible(input);

        sendButton.setButtonText("Send");
        sendButton.onClick = [this] {
            if (requestInFlight)
            {
                requestStop("Stopped by the user.");
                return;
            }
            const auto text = input.getText().trim();
            if (text.isEmpty())
                return;
            input.clear();
            startRequest(text, {});
        };
        addAndMakeVisible(sendButton);

        apiStatus.setJustificationType(juce::Justification::centredLeft);
        apiStatus.setColour(juce::Label::textColourId, juce::Colour(0xff93a7b0));
        addAndMakeVisible(apiStatus);

        refreshProfileList();
        startLocalApi();
        refreshTranscriptBrowser();
    }

    ~AgentPanel() override
    {
        shuttingDown.store(true);
        if (localApi != nullptr)
            localApi->stop();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff151a20));
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8);
        title.setBounds(area.removeFromTop(24));
        area.removeFromTop(6);

        auto top = area.removeFromTop(28);
        profileBox.setBounds(top.removeFromLeft(150));
        top.removeFromLeft(6);
        modelBox.setBounds(top.removeFromLeft(145));
        top.removeFromLeft(6);
        settingsButton.setBounds(top.removeFromLeft(78));
        top.removeFromLeft(6);
        cardsButton.setBounds(top.removeFromLeft(62));

        area.removeFromTop(6);
        auto actions = area.removeFromTop(28);
        ercButton.setBounds(actions.removeFromLeft(86));
        actions.removeFromLeft(6);
        exportButton.setBounds(actions.removeFromLeft(74));
        actions.removeFromLeft(6);
        apiStatus.setBounds(actions);

        area.removeFromTop(8);
        auto bottom = area.removeFromBottom(72);
        sendButton.setBounds(bottom.removeFromRight(72));
        bottom.removeFromRight(6);
        input.setBounds(bottom);

        area.removeFromBottom(8);
        transcriptBrowser.setBounds(area);
    }

    void showAiSettingsForSelected()
    {
        auto profileName = profileBox.getText();
        if (profileName.isEmpty())
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                   "BYOK Agent Settings",
                                                   "Select an AI profile first.");
            return;
        }

        juce::String apiKey;
        juce::String model = modelBox.getText();
        for (const auto& profile : aiConfig.profiles())
        {
            if (profile.name == profileName.toStdString())
            {
                apiKey = profile.apiKey;
                if (model.isEmpty())
                    model = profile.model;
                break;
            }
        }

        auto* dialog = new juce::AlertWindow(
            "BYOK Agent Settings",
            "Profile: " + profileName,
            juce::AlertWindow::NoIcon);
        dialog->addTextEditor("apiKey", apiKey, "API key:", true);
        dialog->addTextEditor("model", model, "Model:");
        dialog->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
        dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

        dialog->enterModalState(true,
            juce::ModalCallbackFunction::create([this, dialog, profileName](int result) {
                if (result != 1)
                    return;

                const auto newKey = dialog->getTextEditorContents("apiKey").trim();
                const auto newModel = dialog->getTextEditorContents("model").trim();
                if (newKey.isEmpty() || newModel.isEmpty())
                {
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                           "BYOK Agent Settings",
                                                           "API key and model are required.");
                    return;
                }

                std::string error;
                if (!aiConfig.updateProfileCredentials(profileName.toStdString(),
                                                       newKey.toStdString(),
                                                       newModel.toStdString(),
                                                       error))
                {
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                           "BYOK Agent Settings",
                                                           juce::String(error));
                    return;
                }

                refreshProfileList();
                modelBox.setText(newModel, juce::dontSendNotification);
                appendTranscript("system", "AI settings saved for " + profileName + ".");
            }),
            true);
    }

    bool submitExternalMessage(const juce::String& content, ExternalCompletion completion)
    {
        if (requestInFlight || content.trim().isEmpty())
            return false;
        startRequest(content.trim(), std::move(completion));
        return true;
    }

    bool configureExternalSession(const juce::var& options, juce::String& error)
    {
        auto selectComboText = [&error](juce::ComboBox& box, const juce::String& requested, const juce::String& label) {
            if (requested.isEmpty())
                return true;
            for (int index = 0; index < box.getNumItems(); ++index)
            {
                if (box.getItemText(index).equalsIgnoreCase(requested))
                {
                    box.setSelectedItemIndex(index, juce::sendNotificationSync);
                    return true;
                }
            }
            if (box.isTextEditable())
            {
                box.setText(requested, juce::sendNotificationSync);
                return true;
            }
            error = "Unknown " + label + ": " + requested;
            return false;
        };

        if (!selectComboText(profileBox, options.getProperty("profile", {}).toString(), "profile"))
            return false;
        if (!selectComboText(modelBox, options.getProperty("model", {}).toString(), "model"))
            return false;
        if ((bool)options.getProperty("newConversation", false))
        {
            history.clear();
            transcriptMarkdown = "BYOK assistant ready.\n";
            refreshTranscriptBrowser();
        }
        return true;
    }

    juce::var externalSessionSnapshot() const
    {
        auto* snapshot = new juce::DynamicObject();
        snapshot->setProperty("profile", profileBox.getText());
        snapshot->setProperty("model", modelBox.getText());
        snapshot->setProperty("busy", requestInFlight);
        snapshot->setProperty("discoveryFile", LocalAgentApi::getDiscoveryFile().getFullPathName());
        snapshot->setProperty("knowledgeRoot", electronics_knowledge::getKnowledgeRoot().getFullPathName());

        juce::Array<juce::var> toolNames;
        for (const auto& definition : toolDefinitions())
            toolNames.add(juce::String(definition.name));
        snapshot->setProperty("tools", toolNames);
        return juce::var(snapshot);
    }

    bool requestStop(const juce::String& reason)
    {
        if (!requestInFlight)
            return false;
        stopRequested.store(true);
        appendTranscript("system", reason);
        return true;
    }

private:
    HostTools tools;
    ai_provider::AiConfig aiConfig;
    std::unique_ptr<LocalAgentApi> localApi;
    std::vector<ai_provider::ChatMessage> history;
    juce::Label title;
    juce::Label apiStatus;
    juce::ComboBox profileBox;
    juce::ComboBox modelBox;
    juce::TextButton settingsButton { "Settings" };
    juce::TextButton cardsButton { "Cards" };
    juce::TextButton ercButton { "Run ERC" };
    juce::TextButton exportButton { "Export" };
    juce::WebBrowserComponent transcriptBrowser;
    juce::String transcriptMarkdown;
    juce::TextEditor input;
    juce::TextButton sendButton { "Send" };
    bool requestInFlight = false;
    std::atomic<bool> stopRequested { false };
    std::atomic<bool> shuttingDown { false };

    static juce::File aiConfigFile()
    {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("DjehutiElectronicsLab")
            .getChildFile("ai_config.json");
    }

    static ai_provider::ToolCall toolCall(const std::string& name, const std::string& arguments)
    {
        ai_provider::ToolCall call;
        call.id = name + ".local";
        call.name = name;
        call.argumentsJson = arguments;
        return call;
    }

    static juce::String compactText(juce::String text, int maxCharacters)
    {
        text = text.trim();
        if (text.length() <= maxCharacters)
            return text;
        return text.substring(0, maxCharacters).trim() + "\n...";
    }

    void refreshProfileList()
    {
        const auto previous = profileBox.getText();
        profileBox.clear();
        int itemId = 1;
        for (const auto& profile : aiConfig.profiles())
            profileBox.addItem(juce::String(profile.name), itemId++);

        if (profileBox.getNumItems() == 0)
        {
            profileBox.setTextWhenNoChoicesAvailable("No profiles");
            return;
        }

        int selected = 0;
        for (int index = 0; index < profileBox.getNumItems(); ++index)
            if (profileBox.getItemText(index) == previous)
                selected = index;
        profileBox.setSelectedItemIndex(selected, juce::sendNotificationSync);
    }

    void refreshModelList()
    {
        const auto profileName = profileBox.getText();
        juce::String selectedModel;
        for (const auto& profile : aiConfig.profiles())
            if (profile.name == profileName.toStdString())
                selectedModel = profile.model;

        modelBox.clear();
        if (selectedModel.isNotEmpty())
            modelBox.addItem(selectedModel, 1);
        modelBox.addItem("gpt-4o-mini", 2);
        modelBox.setText(selectedModel.isNotEmpty() ? selectedModel : "gpt-4o-mini",
                         juce::dontSendNotification);
    }

    void startLocalApi()
    {
        localApi = std::make_unique<LocalAgentApi>();
        localApi->onMessage = [this](const juce::String& content, LocalAgentApi::Completion completion) {
            if (requestInFlight || content.trim().isEmpty())
            {
                completion(false, "The electronics assistant is busy or the message was empty.",
                           externalSessionSnapshot());
                return;
            }
            submitExternalMessage(content, std::move(completion));
        };
        localApi->onSession = [this](const juce::var& options, LocalAgentApi::Completion completion) {
            juce::String error;
            if (!configureExternalSession(options, error))
            {
                completion(false, error, externalSessionSnapshot());
                return;
            }
            completion(true, "Session configured.", externalSessionSnapshot());
        };
        localApi->onCancel = [this] { requestStop("Stop requested through the local agent API."); };

        if (localApi->start())
            apiStatus.setText("API ready: " + LocalAgentApi::getDiscoveryFile().getFullPathName(),
                              juce::dontSendNotification);
        else
            apiStatus.setText("API unavailable", juce::dontSendNotification);
    }

    void appendTranscript(const juce::String& speaker, const juce::String& text)
    {
        transcriptMarkdown << "\n\n### [" << speaker << "]\n\n";
        if ((speaker == "tool" || speaker == "system") && text.trim().startsWith("{"))
            transcriptMarkdown << "```json\n" << text.trim() << "\n```\n";
        else
            transcriptMarkdown << text.trim() << "\n";
        refreshTranscriptBrowser();
    }

    juce::File transcriptHtmlFile() const
    {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("DjehutiElectronicsLab")
            .getChildFile("agent-transcript.html");
    }

    void refreshTranscriptBrowser()
    {
        const auto file = transcriptHtmlFile();
        if (!file.getParentDirectory().createDirectory().wasOk())
            return;

        const auto html = markdownToHtmlDocument("BYOK Electronics Agent", transcriptMarkdown);
        if (file.replaceWithText(html))
            transcriptBrowser.goToURL(juce::URL(file).toString(true));
    }

    juce::String systemPrompt() const
    {
        return "You are the embedded BYOK assistant for Djehuti Electronics Lab. "
               "The circuit JSON model is authoritative. Use tools when you need current schematic facts, "
               "electrical checks, exported artifacts, or diagram edits. Prefer schematic instrument nodes "
               "for scopes and meters, wire pins by labels such as R1.1 or SCOPE2.CH1, and remember "
               "that floating instrument windows are preferred. "
               "Use filter_design_high_pass when asked to synthesize a matched RLC high-pass filter and produce AC response artifacts. "
               "Use amplifier_design_push_pull when asked for a push-pull, class B, class AB, or complementary emitter-follower audio output stage. "
               "Use schematic_auto_layout after creating or editing a diagram so the result is readable and spaced. "
               "Use cookbook_lookup when requirements imply topology selection, design equations, validation recipes, troubleshooting, "
               "or when you need to compare established circuit candidates before building. "
               "Use cookbook_coverage to inspect cookbook domain coverage and identify missing recipe areas. "
               "Use cookbook_validate to check cookbook schema quality and taxonomy alignment after cookbook edits. "
               "Use cookbook_acceptance_goals to inspect representative engineering goals for agent validation. "
               "Use cookbook_acceptance_summary to inspect existing acceptance run reports before rerunning or claiming progress. "
               "Use cookbook_acceptance_start before executing an acceptance goal so evidence has a durable report scaffold. "
               "Use cookbook_acceptance_record to append retrieved cards, tool calls, artifacts, criteria, notes, and capability gaps to that report. "
               "Use capability_gap_record when a missing reusable app capability blocks a cookbook step or validation claim. "
               "The BYOK Agent window renders markdown and KaTeX math. Use $...$ for inline equations and $$ on separate lines "
               "for display equations when discussing formulas, transfer functions, impedance, gain, cutoff frequency, filters, "
               "component sizing, or any math-heavy design reasoning. "
               "Use agent_write_markdown when producing a durable report, derivation, or equation-rich explanation that should be saved and rendered in this agent window. "
               "Use research_web_search only when current external information, standards references, datasheets, or source links are needed; summarize sources conservatively. "
               "Use filesystem LiteSemRAG cards as retrieved guidance; do not assume Suite VFS storage. "
               "Be concise, report tool results plainly, and do not claim a circuit is ready for solver-backed "
               "analysis until circuit_run_erc has passed or you have explained the remaining warnings.";
    }

    std::vector<ai_provider::ToolDefinition> toolDefinitions() const
    {
        return {
            {
                "cookbook_lookup",
                "Search structured electronics cookbook cards for topology candidates, design recipes, analysis steps, validation criteria, and known capability gaps.",
                R"({"type":"object","properties":{"query":{"type":"string","description":"Engineering requirement or cookbook topic to retrieve."},"maxCards":{"type":"integer","description":"Maximum number of cookbook/knowledge cards to return."}},"required":["query"],"additionalProperties":false})"
            },
            {
                "cookbook_coverage",
                "Report structured cookbook coverage against the file-backed taxonomy, including missing categories.",
                R"({"type":"object","properties":{},"additionalProperties":false})"
            },
            {
                "cookbook_validate",
                "Validate cookbook entries for required structured fields, taxonomy alignment, duplicate ids, and malformed raw entries.",
                R"({"type":"object","properties":{},"additionalProperties":false})"
            },
            {
                "cookbook_acceptance_goals",
                "List representative agent acceptance goals, optionally filtered by domain or goal id.",
                R"({"type":"object","properties":{"domainOrId":{"type":"string","description":"Optional acceptance domain such as passive_filter, or exact goal id."}},"additionalProperties":false})"
            },
            {
                "cookbook_acceptance_summary",
                "Summarize generated acceptance run reports, optionally filtered by acceptance domain or goal id.",
                R"({"type":"object","properties":{"domainOrId":{"type":"string","description":"Optional acceptance domain such as passive_filter, or exact goal id."}},"additionalProperties":false})"
            },
            {
                "cookbook_acceptance_start",
                "Create a structured acceptance report scaffold for a representative goal id.",
                R"({"type":"object","properties":{"goalId":{"type":"string","description":"Exact acceptance goal id to start."}},"required":["goalId"],"additionalProperties":false})"
            },
            {
                "cookbook_acceptance_record",
                "Append structured evidence to an acceptance report created by cookbook_acceptance_start.",
                R"({"type":"object","properties":{"reportPath":{"type":"string","description":"Path returned as jsonReport by cookbook_acceptance_start."},"evidenceType":{"type":"string","description":"retrieved_card, tool_call, artifact, capability_gap, criterion, or note."},"label":{"type":"string","description":"Short evidence label."},"detail":{"type":"string","description":"Evidence details."},"pathOrValue":{"type":"string","description":"Artifact path, tool result path, card id, or measured value."},"status":{"type":"string","description":"observed, passed, failed, gap, unverified, or not_started."}},"required":["reportPath","evidenceType","label"],"additionalProperties":false})"
            },
            {
                "capability_gap_record",
                "Append a reusable missing-capability record to the project gap registry.",
                R"({"type":"object","properties":{"category":{"type":"string","description":"Gap category such as solver, component_model, analysis, plotting, instrument, ui, or agent_workflow."},"description":{"type":"string","description":"What blocked the engineering step."},"neededCapability":{"type":"string","description":"Reusable tool or app capability needed to close the gap."},"evidence":{"type":"string","description":"Tool result, report path, or observation proving the gap."},"source":{"type":"string","description":"Acceptance goal id, report path, cookbook card id, or user goal that exposed the gap."},"status":{"type":"string","description":"open, planned, in_progress, closed, or deferred."}},"required":["description","neededCapability"],"additionalProperties":false})"
            },
            {
                "circuit_inspect",
                "Read the current authoritative circuit JSON without modifying it.",
                R"({"type":"object","properties":{},"additionalProperties":false})"
            },
            {
                "circuit_run_erc",
                "Run Electrical Rule Check on the current schematic and return machine-readable results.",
                R"({"type":"object","properties":{},"additionalProperties":false})"
            },
            {
                "simulation_export_artifacts",
                "Export circuit JSON, Xyce netlist, lab instruments JSON, and assistant tool manifest.",
                R"({"type":"object","properties":{},"additionalProperties":false})"
            },
            {
                "agent_write_markdown",
                "Write a markdown report into the agent workspace and render it in the BYOK Agent window with KaTeX equation support.",
                R"({"type":"object","properties":{"title":{"type":"string","description":"Short report title used for filenames and HTML title."},"markdown":{"type":"string","description":"Markdown content. Use $...$ for inline math and $$ on separate lines for display equations."}},"required":["title","markdown"],"additionalProperties":false})"
            },
            {
                "research_web_search",
                "Search the web for current external references. Use sparingly for standards, datasheets, current facts, or source links; prefer app/cookbook tools for local circuit facts.",
                R"({"type":"object","properties":{"query":{"type":"string","description":"Focused search query."},"maxResults":{"type":"integer","description":"Maximum compact results to return, 1 to 8."}},"required":["query"],"additionalProperties":false})"
            },
            {
                "filter_design_high_pass",
                "Design and draw a matched RLC 2nd order high-pass filter, then run the internal AC sweep and export response artifacts.",
                R"({"type":"object","properties":{"cutoffHz":{"type":"number","description":"Target -3 dB cutoff frequency in Hz."},"impedanceOhms":{"type":"number","description":"Matched source/load impedance in ohms."}},"required":["cutoffHz","impedanceOhms"],"additionalProperties":false})"
            },
            {
                "amplifier_design_push_pull",
                "Design and draw a diode-biased complementary push-pull audio output stage with input source, +/- rails, 8 ohm load, grounds, and a 2-channel oscilloscope.",
                R"({"type":"object","properties":{},"additionalProperties":false})"
            },
            {
                "schematic_auto_layout",
                "Automatically arrange the current schematic into a readable left-to-right diagram using standard spacing conventions.",
                R"({"type":"object","properties":{},"additionalProperties":false})"
            },
            {
                "schematic_auto_layout_selection",
                "Automatically arrange only the currently selected schematic components.",
                R"({"type":"object","properties":{},"additionalProperties":false})"
            },
            {
                "schematic_place_symbol",
                "Place a schematic symbol or instrument node at a grid coordinate. Use deliberate layout spacing: keep symbols at least 144 px apart horizontally or 96 px vertically, arrange signal flow left-to-right, put sources on the left, outputs/load on the right, grounds below, instruments to the far right, and never reuse the same x/y for multiple parts.",
                R"({"type":"object","properties":{"symbolId":{"type":"string","description":"Supported symbol id: resistor, potentiometer, capacitor, capacitor_polarized, variable_capacitor, inductor, coupled_inductor, transformer, diode, zener_diode, led, schottky_diode, power_bus, ground_bus, power_port, battery, voltage_source, ac_voltage_source, current_source, ac_current_source, vcvs, vccs, ccvs, cccs, signal_source, ground, opamp_741, npn, pnp, nmos, pmos, njfet, pjfet, switch_spst, switch_spdt, relay_spst, fuse, connector_2, connector_3, test_point, logic_not, logic_and, logic_or, logic_nand, logic_nor, logic_xor, oscilloscope_2ch, or digital_multimeter. Use ground and power_port symbols at each pin that needs ground or a supply instead of long wires: power_port takes busName like +12V (drawn pointing up) or -12V (place with a leading minus; drawn pointing down); ports with the same busName are the same net. Unsupported symbols are rejected, not substituted."},"x":{"type":"number","description":"Grid x coordinate. Leave at least 144 px horizontal space from other symbols."},"y":{"type":"number","description":"Grid y coordinate. Leave at least 96 px vertical space from other symbols."},"value":{"type":"string"},"frequency":{"type":"string"},"busName":{"type":"string"}},"required":["symbolId","x","y"],"additionalProperties":false})"
            },
            {
                "schematic_set_component_properties",
                "Set value, source frequency, bus/net name, family, or manufacturer part for an existing schematic component by reference designator. Omitted or empty properties leave the existing value unchanged.",
                R"({"type":"object","properties":{"refdes":{"type":"string","description":"Reference designator such as R1, C2, V1, PWR3, or SCOPE1."},"value":{"type":"string","description":"Component value, source amplitude, model name, or instrument function."},"frequency":{"type":"string","description":"Source frequency such as 1k or 10."},"busName":{"type":"string","description":"Net label or rail name such as +12V, -12V, Input, or Output."},"family":{"type":"string","description":"Component family/model family."},"manufacturerPart":{"type":"string","description":"Specific manufacturer part number."}},"required":["refdes"],"additionalProperties":false})"
            },
            {
                "schematic_connect",
                "Connect two schematic pins or junctions by label, such as R1.1 to GND2.0.",
                R"({"type":"object","properties":{"a":{"type":"string","description":"First node label such as R1.1, V2.+, GND3.0, SCOPE4.CH1, or N1."},"b":{"type":"string","description":"Second node label."}},"required":["a","b"],"additionalProperties":false})"
            },
            {
                "instrument_open_panel",
                "Open the floating instrument panel for a placed instrument node.",
                R"({"type":"object","properties":{"refdes":{"type":"string","description":"Reference designator of a placed instrument node, such as SCOPE2 or DMM3."}},"required":["refdes"],"additionalProperties":false})"
            }
        };
    }

    juce::String executeToolNow(const ai_provider::ToolCall& call)
    {
        const auto originalName = juce::String(call.name);
        const auto name = originalName.replaceCharacter('.', '_');
        const auto parsed = juce::JSON::parse(juce::String(call.argumentsJson));

        if (name == "circuit_inspect")
        {
            const auto circuit = tools.inspectCircuit != nullptr ? tools.inspectCircuit() : "{}";
            return "{ \"ok\": true, \"tool\": \"circuit_inspect\", \"displayTool\": \"circuit.inspect\", \"circuit\": "
                + (circuit.trim().isEmpty() ? juce::String("{}") : circuit.trim()) + " }";
        }

        if (name == "cookbook_lookup")
        {
            if (!parsed.isObject())
                return "{ \"ok\": false, \"error\": \"cookbook_lookup arguments must be a JSON object.\" }";

            const auto query = parsed.getProperty("query", {}).toString().trim();
            const auto maxCards = (int)parsed.getProperty("maxCards", 6);
            if (query.isEmpty())
                return "{ \"ok\": false, \"error\": \"query is required.\" }";
            return tools.cookbookLookup != nullptr
                ? tools.cookbookLookup(query, maxCards)
                : "{ \"ok\": false, \"error\": \"Cookbook lookup is unavailable.\" }";
        }

        if (name == "cookbook_coverage")
            return tools.cookbookCoverage != nullptr
                ? tools.cookbookCoverage()
                : "{ \"ok\": false, \"error\": \"Cookbook coverage is unavailable.\" }";

        if (name == "cookbook_validate")
            return tools.cookbookValidate != nullptr
                ? tools.cookbookValidate()
                : "{ \"ok\": false, \"error\": \"Cookbook validation is unavailable.\" }";

        if (name == "cookbook_acceptance_goals")
        {
            const auto domainOrId = parsed.isObject()
                ? parsed.getProperty("domainOrId", {}).toString().trim()
                : juce::String();
            return tools.cookbookAcceptanceGoals != nullptr
                ? tools.cookbookAcceptanceGoals(domainOrId)
                : "{ \"ok\": false, \"error\": \"Cookbook acceptance goals are unavailable.\" }";
        }

        if (name == "cookbook_acceptance_summary")
        {
            const auto domainOrId = parsed.isObject()
                ? parsed.getProperty("domainOrId", {}).toString().trim()
                : juce::String();
            return tools.cookbookAcceptanceSummary != nullptr
                ? tools.cookbookAcceptanceSummary(domainOrId)
                : "{ \"ok\": false, \"error\": \"Cookbook acceptance summary is unavailable.\" }";
        }

        if (name == "cookbook_acceptance_start")
        {
            if (!parsed.isObject())
                return "{ \"ok\": false, \"error\": \"cookbook_acceptance_start arguments must be a JSON object.\" }";

            const auto goalId = parsed.getProperty("goalId", {}).toString().trim();
            if (goalId.isEmpty())
                return "{ \"ok\": false, \"error\": \"goalId is required.\" }";
            return tools.cookbookAcceptanceStart != nullptr
                ? tools.cookbookAcceptanceStart(goalId)
                : "{ \"ok\": false, \"error\": \"Cookbook acceptance start is unavailable.\" }";
        }

        if (name == "cookbook_acceptance_record")
        {
            if (!parsed.isObject())
                return "{ \"ok\": false, \"error\": \"cookbook_acceptance_record arguments must be a JSON object.\" }";

            const auto reportPath = parsed.getProperty("reportPath", {}).toString().trim();
            const auto evidenceType = parsed.getProperty("evidenceType", {}).toString().trim();
            const auto label = parsed.getProperty("label", {}).toString().trim();
            const auto detail = parsed.getProperty("detail", {}).toString().trim();
            const auto pathOrValue = parsed.getProperty("pathOrValue", {}).toString().trim();
            const auto status = parsed.getProperty("status", {}).toString().trim();
            if (reportPath.isEmpty() || evidenceType.isEmpty() || label.isEmpty())
                return "{ \"ok\": false, \"error\": \"reportPath, evidenceType, and label are required.\" }";
            return tools.cookbookAcceptanceRecord != nullptr
                ? tools.cookbookAcceptanceRecord(reportPath, evidenceType, label, detail, pathOrValue, status)
                : "{ \"ok\": false, \"error\": \"Cookbook acceptance record is unavailable.\" }";
        }

        if (name == "capability_gap_record")
        {
            if (!parsed.isObject())
                return "{ \"ok\": false, \"error\": \"capability_gap_record arguments must be a JSON object.\" }";

            const auto category = parsed.getProperty("category", {}).toString().trim();
            const auto description = parsed.getProperty("description", {}).toString().trim();
            const auto neededCapability = parsed.getProperty("neededCapability", {}).toString().trim();
            const auto evidence = parsed.getProperty("evidence", {}).toString().trim();
            const auto source = parsed.getProperty("source", {}).toString().trim();
            const auto status = parsed.getProperty("status", {}).toString().trim();
            if (description.isEmpty() || neededCapability.isEmpty())
                return "{ \"ok\": false, \"error\": \"description and neededCapability are required.\" }";
            return tools.capabilityGapRecord != nullptr
                ? tools.capabilityGapRecord(category, description, neededCapability, evidence, source, status)
                : "{ \"ok\": false, \"error\": \"Capability gap recording is unavailable.\" }";
        }

        if (name == "circuit_run_erc")
            return tools.runErc != nullptr ? tools.runErc() : "{ \"ok\": false, \"error\": \"ERC tool unavailable.\" }";

        if (name == "simulation_export_artifacts")
            return tools.exportArtifacts != nullptr ? tools.exportArtifacts() : "{ \"ok\": false, \"error\": \"Export tool unavailable.\" }";

        if (name == "agent_write_markdown")
        {
            if (!parsed.isObject())
                return "{ \"ok\": false, \"error\": \"agent_write_markdown arguments must be a JSON object.\" }";
            const auto title = parsed.getProperty("title", {}).toString().trim();
            const auto markdown = parsed.getProperty("markdown", {}).toString();
            if (title.isEmpty() || markdown.trim().isEmpty())
                return "{ \"ok\": false, \"error\": \"title and markdown are required.\" }";
            if (tools.writeMarkdown == nullptr)
                return "{ \"ok\": false, \"error\": \"Markdown writing is unavailable.\" }";

            const auto result = tools.writeMarkdown(title, markdown);
            const auto reportResult = juce::JSON::parse(result);
            if (reportResult.isObject() && (bool)reportResult.getProperty("ok", false))
                appendTranscript("assistant report", markdown);
            return result;
        }

        if (name == "research_web_search")
        {
            if (!parsed.isObject())
                return "{ \"ok\": false, \"error\": \"research_web_search arguments must be a JSON object.\" }";
            const auto query = parsed.getProperty("query", {}).toString().trim();
            const auto maxResults = (int)parsed.getProperty("maxResults", 5);
            if (query.isEmpty())
                return "{ \"ok\": false, \"error\": \"query is required.\" }";
            return tools.webSearch != nullptr
                ? tools.webSearch(query, maxResults)
                : "{ \"ok\": false, \"error\": \"Web search is unavailable.\" }";
        }

        if (name == "filter_design_high_pass")
        {
            if (!parsed.isObject())
                return "{ \"ok\": false, \"error\": \"filter_design_high_pass arguments must be a JSON object.\" }";

            const auto cutoffHz = (double)parsed.getProperty("cutoffHz", 10.0);
            const auto impedanceOhms = (double)parsed.getProperty("impedanceOhms", 8.0);
            return tools.designHighPass != nullptr
                ? tools.designHighPass(cutoffHz, impedanceOhms)
                : "{ \"ok\": false, \"error\": \"High-pass filter design tool unavailable.\" }";
        }

        if (name == "amplifier_design_push_pull")
            return tools.designPushPull != nullptr
                ? tools.designPushPull()
                : "{ \"ok\": false, \"error\": \"Push-pull amplifier design tool unavailable.\" }";

        if (name == "schematic_auto_layout")
            return tools.autoLayout != nullptr
                ? tools.autoLayout()
                : "{ \"ok\": false, \"error\": \"Schematic auto-layout is unavailable.\" }";

        if (name == "schematic_auto_layout_selection")
            return tools.autoLayoutSelection != nullptr
                ? tools.autoLayoutSelection()
                : "{ \"ok\": false, \"error\": \"Schematic selection auto-layout is unavailable.\" }";

        if (name == "schematic_place_symbol")
        {
            if (!parsed.isObject())
                return "{ \"ok\": false, \"error\": \"schematic_place_symbol arguments must be a JSON object.\" }";

            const auto symbolId = parsed.getProperty("symbolId", {}).toString().trim();
            const auto x = (float)(double)parsed.getProperty("x", 120.0);
            const auto y = (float)(double)parsed.getProperty("y", 120.0);
            const auto value = parsed.getProperty("value", {}).toString();
            const auto frequency = parsed.getProperty("frequency", {}).toString();
            const auto busName = parsed.getProperty("busName", {}).toString();
            if (symbolId.isEmpty())
                return "{ \"ok\": false, \"error\": \"symbolId is required.\" }";
            return tools.placeSymbol != nullptr
                ? tools.placeSymbol(symbolId, x, y, value, frequency, busName)
                : "{ \"ok\": false, \"error\": \"Schematic placement tool unavailable.\" }";
        }

        if (name == "schematic_set_component_properties")
        {
            if (!parsed.isObject())
                return "{ \"ok\": false, \"error\": \"schematic_set_component_properties arguments must be a JSON object.\" }";

            const auto refdes = parsed.getProperty("refdes", {}).toString().trim();
            if (refdes.isEmpty())
                return "{ \"ok\": false, \"error\": \"refdes is required.\" }";

            return tools.setComponentProperties != nullptr
                ? tools.setComponentProperties(refdes,
                                               parsed.getProperty("value", {}).toString(),
                                               parsed.getProperty("frequency", {}).toString(),
                                               parsed.getProperty("busName", {}).toString(),
                                               parsed.getProperty("family", {}).toString(),
                                               parsed.getProperty("manufacturerPart", {}).toString())
                : "{ \"ok\": false, \"error\": \"Schematic property editing is unavailable.\" }";
        }

        if (name == "schematic_connect")
        {
            if (!parsed.isObject())
                return "{ \"ok\": false, \"error\": \"schematic_connect arguments must be a JSON object.\" }";

            const auto first = parsed.getProperty("a", {}).toString().trim();
            const auto second = parsed.getProperty("b", {}).toString().trim();
            if (first.isEmpty() || second.isEmpty())
                return "{ \"ok\": false, \"error\": \"Both a and b node labels are required.\" }";
            return tools.connectNodes != nullptr
                ? tools.connectNodes(first, second)
                : "{ \"ok\": false, \"error\": \"Schematic wiring tool unavailable.\" }";
        }

        if (name == "instrument_open_panel")
        {
            if (!parsed.isObject())
                return "{ \"ok\": false, \"error\": \"instrument_open_panel arguments must be a JSON object.\" }";

            const auto refdes = parsed.getProperty("refdes", {}).toString().trim();
            if (refdes.isEmpty())
                return "{ \"ok\": false, \"error\": \"refdes is required.\" }";
            return tools.openInstrument != nullptr
                ? tools.openInstrument(refdes)
                : "{ \"ok\": false, \"error\": \"Instrument opening tool unavailable.\" }";
        }

        return "{ \"ok\": false, \"error\": \"Unknown tool: " + originalName + "\" }";
    }

    juce::String executeToolFromWorker(const ai_provider::ToolCall& call)
    {
        if (juce::MessageManager::getInstance()->isThisTheMessageThread())
            return executeToolNow(call);

        struct ToolWait
        {
            juce::WaitableEvent done;
            juce::String result;
        };

        auto wait = std::make_shared<ToolWait>();
        auto safeThis = juce::Component::SafePointer<AgentPanel>(this);
        juce::MessageManager::callAsync([safeThis, wait, call] {
            if (safeThis == nullptr)
                wait->result = "{ \"ok\": false, \"error\": \"Assistant panel closed.\" }";
            else
                wait->result = safeThis->executeToolNow(call);
            wait->done.signal();
        });

        while (!wait->done.wait(50))
        {
            if (stopRequested.load() || shuttingDown.load())
                return "{ \"ok\": false, \"error\": \"Tool execution stopped.\" }";
        }
        return wait->result;
    }

    void showRetrievedCardsForDraft()
    {
        const auto query = input.getText().trim().isNotEmpty()
            ? input.getText().trim()
            : juce::String("erc instruments circuit model");
        const auto retrieved = electronics_knowledge::retrieve(query);
        appendTranscript("cards", retrieved.context.isNotEmpty()
            ? retrieved.context
            : "No matching cards found in " + electronics_knowledge::getCardsDirectory().getFullPathName());
    }

    void startRequest(const juce::String& userText, ExternalCompletion completion)
    {
        const auto profileName = profileBox.getText();
        const auto modelName = modelBox.getText().trim();
        if (profileName.isEmpty())
        {
            appendTranscript("system", "No AI profile selected. Open Settings and add your API key/model.");
            if (completion)
                completion(false, "No AI profile selected.", externalSessionSnapshot());
            return;
        }

        if (modelName.isNotEmpty())
        {
            for (const auto& profile : aiConfig.profiles())
            {
                if (profile.name != profileName.toStdString() || profile.model == modelName.toStdString())
                    continue;
                std::string error;
                aiConfig.updateProfileCredentials(profile.name, profile.apiKey, modelName.toStdString(), error);
                break;
            }
        }

        auto provider = aiConfig.createProvider(profileName.toStdString());
        if (provider == nullptr)
        {
            appendTranscript("system", "Could not create provider for " + profileName + ".");
            if (completion)
                completion(false, "Could not create provider.", externalSessionSnapshot());
            return;
        }

        appendTranscript("user", userText);
        const auto retrieved = electronics_knowledge::retrieve(userText);
        const auto circuit = tools.inspectCircuit != nullptr ? tools.inspectCircuit() : "{}";

        std::vector<ai_provider::ChatMessage> messages;
        ai_provider::ChatMessage system;
        system.role = "system";
        system.content = systemPrompt().toStdString();
        messages.push_back(system);

        for (const auto& item : history)
            messages.push_back(item);

        juce::String content = userText;
        if (retrieved.context.isNotEmpty())
            content << "\n\n---\n" << retrieved.context;
        content << "\n\n---\nCurrent circuit model snapshot:\n"
                << compactText(circuit, 12000);

        ai_provider::ChatMessage user;
        user.role = "user";
        user.content = content.toStdString();
        messages.push_back(user);

        requestInFlight = true;
        stopRequested.store(false);
        sendButton.setButtonText("Stop");
        appendTranscript("system", retrieved.cards.empty()
            ? "No card context matched this request."
            : "Attached " + juce::String((int)retrieved.cards.size()) + " LiteSemRAG card(s).");

        auto toolDefs = toolDefinitions();
        auto providerPtr = provider.release();
        auto safeThis = juce::Component::SafePointer<AgentPanel>(this);

        std::thread([safeThis, providerPtr, messages = std::move(messages), toolDefs = std::move(toolDefs),
                     completion = std::move(completion)]() mutable {
            std::unique_ptr<ai_provider::AiProvider> owned(providerPtr);
            ai_provider::ChatResponse response;
            bool ok = false;
            juce::String finalText;

            for (int round = 0; round < 12; ++round)
            {
                if (safeThis == nullptr || safeThis->stopRequested.load())
                {
                    response = { false, {}, "Stopped by the user." };
                    break;
                }

                response = owned->sendChat(messages, toolDefs, ai_provider::ToolChoice::autoSelect);
                if (!response.ok)
                    break;

                ai_provider::ChatMessage assistant;
                assistant.role = "assistant";
                assistant.content = response.content;
                assistant.toolCalls = response.toolCalls;
                assistant.providerItemsJson = response.providerItemsJson;
                messages.push_back(assistant);

                if (response.toolCalls.empty())
                {
                    ok = true;
                    finalText = juce::String(response.content);
                    break;
                }

                for (const auto& call : response.toolCalls)
                {
                    if (safeThis == nullptr)
                        break;

                    juce::MessageManager::callAsync([safeThis, name = juce::String(call.name)] {
                        if (safeThis != nullptr)
                            safeThis->appendTranscript("tool", "Running " + name + "...");
                    });

                    const auto result = safeThis->executeToolFromWorker(call);
                    ai_provider::ChatMessage toolMessage;
                    toolMessage.role = "tool";
                    toolMessage.content = result.toStdString();
                    toolMessage.toolCallId = call.id;
                    messages.push_back(toolMessage);
                }
            }

            if (response.ok && !ok && finalText.isEmpty())
            {
                response.ok = false;
                response.errorMessage = "The assistant used too many tool rounds without producing a final answer.";
            }

            juce::MessageManager::callAsync([safeThis, messages = std::move(messages), response,
                                             ok, finalText, completion = std::move(completion)]() mutable {
                if (safeThis == nullptr)
                    return;

                safeThis->history.clear();
                for (size_t index = 1; index < messages.size(); ++index)
                    safeThis->history.push_back(messages[index]);

                const auto visible = ok ? finalText
                                        : "Error: " + juce::String(response.errorMessage);
                safeThis->appendTranscript(ok ? "assistant" : "system", visible);
                safeThis->requestInFlight = false;
                safeThis->sendButton.setButtonText("Send");
                if (completion)
                    completion(ok, visible, safeThis->externalSessionSnapshot());
            });
        }).detach();
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AgentPanel)
};

class SimulationPanel final : public NotesPanel
{
public:
    SimulationPanel()
        : NotesPanel("Simulation Setup",
                     "Analysis modes:\n"
                     "- DC operating point\n"
                     "- DC sweep\n"
                     "- transient\n"
                     "- AC small signal\n"
                     "- compiled Frust realtime preview\n\n"
                     "Simulation output should become datasets consumed by instruments and the Frust console.")
    {
    }
};

class FrequencyResponsePanel final : public juce::Component
{
public:
    FrequencyResponsePanel()
    {
        title.setText("Analysis / Frequency Response", juce::dontSendNotification);
        title.setFont(juce::Font(16.0f, juce::Font::bold));
        title.setColour(juce::Label::textColourId, juce::Colour(0xff78dcca));
        addAndMakeVisible(title);

        runButton.setButtonText("Run 10 Hz / 8 Ohm HPF");
        runButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff253341));
        runButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffdce9ee));
        runButton.onClick = [this] {
            if (onRun != nullptr)
                onRun();
        };
        addAndMakeVisible(runButton);

        status.setText("No sweep loaded.", juce::dontSendNotification);
        status.setColour(juce::Label::textColourId, juce::Colour(0xff93a7b0));
        status.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(status);
    }

    void setResponse(const juce::File& csvFile,
                     const juce::File& reportFile,
                     double cutoffHz,
                     double impedanceOhms,
                     double capacitanceFarads,
                     double inductanceHenries)
    {
        samples.clear();
        const auto lines = juce::StringArray::fromLines(csvFile.loadFileAsString());
        for (int index = 1; index < lines.size(); ++index)
        {
            const auto columns = juce::StringArray::fromTokens(lines[index], ",", "");
            if (columns.size() < 2)
                continue;
            samples.push_back({ columns[0].getDoubleValue(), columns[1].getDoubleValue() });
        }

        currentCutoffHz = cutoffHz;
        currentImpedanceOhms = impedanceOhms;
        currentCapacitanceFarads = capacitanceFarads;
        currentInductanceHenries = inductanceHenries;
        currentCsv = csvFile;
        currentReport = reportFile;
        status.setText(samples.empty()
            ? "Sweep CSV had no samples: " + csvFile.getFullPathName()
            : "Showing " + juce::String((int)samples.size()) + " AC sweep points from " + csvFile.getFileName(),
            juce::dontSendNotification);
        repaint();
    }

    std::function<void()> onRun;

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff10161d));
        auto area = getLocalBounds().reduced(12);
        area.removeFromTop(68);

        g.setColour(juce::Colour(0xffdce9ee));
        g.setFont(juce::Font(14.0f, juce::Font::bold));
        g.drawText("2nd Order RLC High-Pass AC Analysis", area.removeFromTop(24), juce::Justification::centredLeft);

        g.setColour(juce::Colour(0xff93a7b0));
        g.setFont(juce::Font(12.5f));
        const auto summary = samples.empty()
            ? juce::String("Run the analysis to draw the response curve here.")
            : "fc " + numberText(currentCutoffHz, 3) + " Hz, Z0 " + numberText(currentImpedanceOhms, 3)
                + " ohm, C1 " + humanCapacitance(currentCapacitanceFarads)
                + ", L1 " + humanInductance(currentInductanceHenries);
        g.drawText(summary, area.removeFromTop(22), juce::Justification::centredLeft);
        area.removeFromTop(8);

        auto graph = area.removeFromTop(std::max(260, area.getHeight() - 86)).toFloat();
        drawGraph(g, graph);

        area.removeFromTop(8);
        g.setColour(juce::Colour(0xff93a7b0));
        g.setFont(juce::Font(11.5f));
        if (currentReport.existsAsFile())
            g.drawText("Report: " + currentReport.getFullPathName(), area.removeFromTop(18), juce::Justification::centredLeft, true);
        if (currentCsv.existsAsFile())
            g.drawText("CSV: " + currentCsv.getFullPathName(), area.removeFromTop(18), juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8);
        auto header = area.removeFromTop(28);
        title.setBounds(header.removeFromLeft(245));
        header.removeFromLeft(8);
        runButton.setBounds(header.removeFromLeft(180));
        header.removeFromLeft(8);
        status.setBounds(header);
    }

private:
    struct Point
    {
        double frequencyHz = 0.0;
        double gainDb = 0.0;
    };

    void drawGraph(juce::Graphics& g, juce::Rectangle<float> graph)
    {
        g.setColour(juce::Colour(0xff111922));
        g.fillRect(graph);
        g.setColour(juce::Colour(0xff33424d));
        g.drawRect(graph, 1.0f);

        constexpr double minDb = -60.0;
        constexpr double maxDb = 3.0;
        const auto startHz = samples.empty() ? 0.1 : std::max(0.001, samples.front().frequencyHz);
        const auto stopHz = samples.empty() ? 1000.0 : std::max(startHz * 10.0, samples.back().frequencyHz);
        const auto logStart = std::log10(startHz);
        const auto logStop = std::log10(stopHz);

        auto xFor = [&](double frequency) {
            return graph.getX() + (float)((std::log10(std::clamp(frequency, startHz, stopHz)) - logStart) / (logStop - logStart)) * graph.getWidth();
        };
        auto yFor = [&](double db) {
            const auto clamped = std::clamp(db, minDb, maxDb);
            return graph.getY() + (float)((maxDb - clamped) / (maxDb - minDb)) * graph.getHeight();
        };

        g.setFont(juce::Font(11.0f));
        for (double db : { 0.0, -3.0, -10.0, -20.0, -40.0, -60.0 })
        {
            const auto y = yFor(db);
            g.setColour(db == -3.0 ? juce::Colour(0xffffc857) : juce::Colour(0xff26323d));
            g.drawHorizontalLine((int)y, graph.getX(), graph.getRight());
            g.setColour(juce::Colour(0xff93a7b0));
            g.drawText(numberText(db, 0) + " dB", (int)graph.getX() + 6, (int)y - 14, 62, 14, juce::Justification::centredLeft);
        }

        for (double frequency : { startHz, currentCutoffHz / 10.0, currentCutoffHz, currentCutoffHz * 10.0, stopHz })
        {
            if (frequency < startHz * 0.999 || frequency > stopHz * 1.001)
                continue;
            const auto x = xFor(frequency);
            g.setColour(std::abs(frequency - currentCutoffHz) < 0.001 ? juce::Colour(0xff78dcca) : juce::Colour(0xff26323d));
            g.drawVerticalLine((int)x, graph.getY(), graph.getBottom());
            g.setColour(juce::Colour(0xff93a7b0));
            g.drawText(numberText(frequency, frequency < 1.0 ? 2 : 0) + " Hz", (int)x - 25, (int)graph.getBottom() - 18, 58, 14, juce::Justification::centred);
        }

        if (samples.empty())
        {
            g.setColour(juce::Colour(0xff93a7b0));
            g.setFont(juce::Font(15.0f));
            g.drawText("No AC sweep loaded", graph.toNearestInt(), juce::Justification::centred);
            return;
        }

        juce::Path curve;
        for (size_t index = 0; index < samples.size(); ++index)
        {
            const auto x = xFor(samples[index].frequencyHz);
            const auto y = yFor(samples[index].gainDb);
            if (index == 0)
                curve.startNewSubPath(x, y);
            else
                curve.lineTo(x, y);
        }

        g.setColour(juce::Colour(0xff78dcca));
        g.strokePath(curve, juce::PathStrokeType(2.5f));
        g.setColour(juce::Colour(0xffffc857));
        g.fillEllipse(xFor(currentCutoffHz) - 4.5f, yFor(-3.01029995664) - 4.5f, 9.0f, 9.0f);

        g.setColour(juce::Colour(0xffdce9ee));
        g.setFont(juce::Font(13.0f, juce::Font::bold));
        g.drawText("Normalized gain response", graph.withTrimmedLeft(14.0f).withTrimmedTop(10.0f).toNearestInt(),
                   juce::Justification::topLeft);
    }

    juce::Label title;
    juce::Label status;
    juce::TextButton runButton;
    std::vector<Point> samples;
    double currentCutoffHz = 10.0;
    double currentImpedanceOhms = 8.0;
    double currentCapacitanceFarads = 0.0;
    double currentInductanceHenries = 0.0;
    juce::File currentCsv;
    juce::File currentReport;
};

class SpecIngestionPanel final : public NotesPanel
{
public:
    SpecIngestionPanel()
        : NotesPanel("Component Spec Ingestion",
                     "Workflow:\n"
                     "1. Search local component DB.\n"
                     "2. Query approved providers/manufacturer sources.\n"
                     "3. Fetch datasheet.\n"
                     "4. Extract pins/spec claims/packages/models.\n"
                     "5. Store provenance and confidence.\n"
                     "6. Ask for approval when ambiguous.")
    {
    }
};

class PartsSourcingPanel final : public NotesPanel
{
public:
    PartsSourcingPanel()
        : NotesPanel("Parts Sourcing",
                     "Find buyable parts without confusing marketplace listings with verified specifications.\n\n"
                     "Research targets:\n"
                     "- distributor listings for exact MPNs\n"
                     "- hobby suppliers and breadboard-friendly packages\n"
                     "- Amazon/eBay/AliExpress style consumer listings\n"
                     "- assortment kits and substitutes\n"
                     "- local user inventory\n\n"
                     "Every sourcing result should carry match confidence, source URL, timestamp, package notes, and warnings.")
    {
    }
};

} // namespace

ElectronicsWorkbench::ElectronicsWorkbench()
{
    menuBar = std::make_unique<juce::MenuBarComponent>(this);
    addAndMakeVisible(menuBar.get());

    titleLabel.setText("Djehuti Electronics Lab", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(17.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xffdce9ee));
    addAndMakeVisible(titleLabel);

    statusLabel.setText("Research shell ready", juce::dontSendNotification);
    statusLabel.setFont(juce::Font(13.0f));
    statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xff93a7b0));
    statusLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(statusLabel);

    for (auto* b : { &newButton, &ercButton, &transientButton, &compileButton,
                     &zoomOutButton, &zoomResetButton, &zoomInButton })
    {
        b->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff253341));
        b->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffdce9ee));
        addAndMakeVisible(*b);
    }
    stampModeButton.setToggleState(false, juce::dontSendNotification);
    stampModeButton.setColour(juce::ToggleButton::textColourId, juce::Colour(0xffdce9ee));
    addAndMakeVisible(stampModeButton);
    snapModeButton.setToggleState(true, juce::dontSendNotification);
    snapModeButton.setColour(juce::ToggleButton::textColourId, juce::Colour(0xffdce9ee));
    addAndMakeVisible(snapModeButton);

    newButton.onClick = [this] { resetResearchState(); };
    ercButton.onClick = [this] { runElectricalRuleCheck(); };
    transientButton.onClick = [this] { exportCircuitArtifacts(); };
    compileButton.onClick = [this] { appendLog("Compiled Frust preview stub: circuit IR -> Frust lowering pending."); };
    snapModeButton.onClick = [this] {
        if (setSnapEnabled != nullptr)
            setSnapEnabled(snapModeButton.getToggleState());
        appendLog(snapModeButton.getToggleState() ? "Schematic snap enabled." : "Schematic snap disabled.");
    };
    zoomOutButton.onClick = [this] { adjustSchematicZoom(1.0f / 1.2f); };
    zoomResetButton.onClick = [this] { applySchematicZoom(1.0f); };
    zoomInButton.onClick = [this] { adjustSchematicZoom(1.2f); };

    dockManager = std::make_unique<CreationDock::DockManager>(*this);
    addAndMakeVisible(*dockManager);

    dockManager->registerPanel("library", "Component Library",
                               std::make_unique<ComponentLibraryPanel>(
                                   [this](juce::String id) {
                                       selectedSymbolId = id;
                                       appendLog("Selected symbol: " + id + ". Click the schematic to place it, or drag it from the library.");
                                   }),
                               CreationDock::DockTargetZone::Left);
    auto schematic = std::make_unique<SchematicCanvasPanel>(
        [this] { return selectedSymbolId; },
        [this] { return stampModeButton.getToggleState(); },
        [this](juce::String message) { appendLog(message); });
    auto* schematicPanel = schematic.get();
    setSnapEnabled = [schematicPanel](bool enabled) {
        schematicPanel->setSnapEnabled(enabled);
    };
    setSchematicZoom = [schematicPanel](float zoom) {
        schematicPanel->setCanvasZoom(zoom);
    };
    getSchematicZoom = [schematicPanel] {
        return schematicPanel->getCanvasZoom();
    };
    auto properties = std::make_unique<PropertiesPanel>();
    auto* propertiesPanel = properties.get();
    schematicPanel->setSelectionListener([propertiesPanel](int index,
                                                           juce::String refdes,
                                                           juce::String symbol,
                                                           juce::String value,
                                                           juce::String frequency,
                                                           juce::String busName,
                                                           juce::String family,
                                                           juce::String manufacturerPart) {
        propertiesPanel->setSelection(index, refdes, symbol, value, frequency, busName, family, manufacturerPart);
    });
    propertiesPanel->onApply = [schematicPanel](juce::String value,
                                                juce::String frequency,
                                                juce::String busName,
                                                juce::String family,
                                                juce::String manufacturerPart) {
        schematicPanel->updateSelectedProperties(value, frequency, busName, family, manufacturerPart);
    };
    propertiesPanel->onRotate = [schematicPanel] {
        schematicPanel->rotateSelected();
    };
    auto instruments = std::make_unique<InstrumentPanel>();
    auto* instrumentPanel = instruments.get();
    auto analysis = std::make_unique<FrequencyResponsePanel>();
    auto* analysisPanel = analysis.get();
    analysisPanel->onRun = [this] { designRlcHighPassFilter(); };
    showFrequencyResponse = [analysisPanel](const juce::File& csvFile,
                                            const juce::File& reportFile,
                                            double cutoffHz,
                                            double impedanceOhms,
                                            double capacitanceFarads,
                                            double inductanceHenries) {
        analysisPanel->setResponse(csvFile, reportFile, cutoffHz, impedanceOhms, capacitanceFarads, inductanceHenries);
    };
    schematicPanel->setProbeListener([instrumentPanel](juce::String id, juce::String, juce::String target) {
        instrumentPanel->setProbeTarget(id, target);
    });
    schematicPanel->setInstrumentOpenListener([this](juce::String refdes, juce::String symbolId) {
        openInstrumentWindow(refdes, symbolId);
    });
    resetCircuit = [panel = schematic.get()] { panel->clearCircuit(); };
    getCircuitJson = [panel = schematic.get()] { return panel->buildCircuitJson(); };
    getXyceNetlist = [panel = schematic.get()] { return panel->buildXyceNetlist(); };
    getLabInstrumentsJson = [instrumentPanel] { return instrumentPanel->buildInstrumentJson(); };
    getErcReport = [panel = schematic.get()] { return panel->buildErcReport(); };
    loadCircuitJson = [panel = schematic.get()](const juce::String& json, juce::String& error) {
        return panel->loadCircuitJson(json, error);
    };
    placeSymbolTool = [panel = schematic.get()](const juce::String& symbolId,
                                                float x,
                                                float y,
                                                const juce::String& value,
                                                const juce::String& frequency,
                                                const juce::String& busName) {
        return panel->placeSymbolFromTool(symbolId, x, y, value, frequency, busName);
    };
    setComponentPropertiesTool = [panel = schematic.get()](const juce::String& refdes,
                                                           const juce::String& value,
                                                           const juce::String& frequency,
                                                           const juce::String& busName,
                                                           const juce::String& family,
                                                           const juce::String& manufacturerPart) {
        return panel->setComponentPropertiesFromTool(refdes, value, frequency, busName, family, manufacturerPart);
    };
    connectNodesTool = [panel = schematic.get()](const juce::String& firstLabel,
                                                 const juce::String& secondLabel) {
        return panel->connectNodesFromTool(firstLabel, secondLabel);
    };
    openInstrumentTool = [panel = schematic.get()](const juce::String& refdes) {
        return panel->openInstrumentFromTool(refdes);
    };
    designHighPassTool = [this, panel = schematic.get()](double cutoffHz, double impedanceOhms) {
        const auto schematicResult = panel->createRlcHighPassFilterFromTool(cutoffHz, impedanceOhms);
        const auto parsed = juce::JSON::parse(schematicResult);
        if (!parsed.isObject() || !(bool)parsed.getProperty("ok", false))
            return schematicResult;
        return designRlcHighPassFilterTool(cutoffHz, impedanceOhms);
    };
    designPushPullTool = [this, panel = schematic.get()] {
        const auto schematicResult = panel->createPushPullAmplifierFromTool();
        const auto parsed = juce::JSON::parse(schematicResult);
        if (!parsed.isObject() || !(bool)parsed.getProperty("ok", false))
            return schematicResult;
        return designPushPullAmplifierTool();
    };
    autoLayoutTool = [panel = schematic.get()] {
        return panel->autoLayoutFromTool();
    };
    auto autoLayoutSelectionTool = [panel = schematic.get()] {
        return panel->autoLayoutSelectionFromTool();
    };
    AgentPanel::HostTools agentTools;
    agentTools.inspectCircuit = [this] {
        return getCircuitJson != nullptr ? getCircuitJson() : juce::String("{}");
    };
    agentTools.runErc = [this] { return runElectricalRuleCheckTool(); };
    agentTools.exportArtifacts = [this] { return exportCircuitArtifactsTool(); };
    agentTools.writeMarkdown = [this](const juce::String& title, const juce::String& markdown) {
        return writeAgentMarkdownTool(title, markdown);
    };
    agentTools.webSearch = [this](const juce::String& query, int maxResults) {
        return researchWebSearchTool(query, maxResults);
    };
    agentTools.placeSymbol = [this](const juce::String& symbolId,
                                    float x,
                                    float y,
                                    const juce::String& value,
                                    const juce::String& frequency,
                                    const juce::String& busName) {
        return placeSymbolTool != nullptr
            ? placeSymbolTool(symbolId, x, y, value, frequency, busName)
            : juce::String("{ \"ok\": false, \"error\": \"Schematic placement is unavailable.\" }");
    };
    agentTools.setComponentProperties = [this](const juce::String& refdes,
                                               const juce::String& value,
                                               const juce::String& frequency,
                                               const juce::String& busName,
                                               const juce::String& family,
                                               const juce::String& manufacturerPart) {
        return setComponentPropertiesTool != nullptr
            ? setComponentPropertiesTool(refdes, value, frequency, busName, family, manufacturerPart)
            : juce::String("{ \"ok\": false, \"error\": \"Schematic property editing is unavailable.\" }");
    };
    agentTools.connectNodes = [this](const juce::String& firstLabel, const juce::String& secondLabel) {
        return connectNodesTool != nullptr
            ? connectNodesTool(firstLabel, secondLabel)
            : juce::String("{ \"ok\": false, \"error\": \"Schematic wiring is unavailable.\" }");
    };
    agentTools.openInstrument = [this](const juce::String& refdes) {
        return openInstrumentTool != nullptr
            ? openInstrumentTool(refdes)
            : juce::String("{ \"ok\": false, \"error\": \"Instrument opening is unavailable.\" }");
    };
    agentTools.designHighPass = [this](double cutoffHz, double impedanceOhms) {
        return designHighPassTool != nullptr
            ? designHighPassTool(cutoffHz, impedanceOhms)
            : juce::String("{ \"ok\": false, \"error\": \"High-pass filter design is unavailable.\" }");
    };
    agentTools.designPushPull = [this] {
        return designPushPullTool != nullptr
            ? designPushPullTool()
            : juce::String("{ \"ok\": false, \"error\": \"Push-pull amplifier design is unavailable.\" }");
    };
    agentTools.autoLayout = [this] {
        return autoLayoutTool != nullptr
            ? autoLayoutTool()
            : juce::String("{ \"ok\": false, \"error\": \"Schematic auto-layout is unavailable.\" }");
    };
    agentTools.autoLayoutSelection = [autoLayoutSelectionTool] {
        return autoLayoutSelectionTool();
    };
    agentTools.cookbookLookup = [this](const juce::String& query, int maxCards) {
        return cookbookLookupTool(query, maxCards);
    };
    agentTools.cookbookCoverage = [this] { return cookbookCoverageTool(); };
    agentTools.cookbookValidate = [this] { return cookbookValidateTool(); };
    agentTools.cookbookAcceptanceGoals = [this](const juce::String& domainOrId) {
        return cookbookAcceptanceGoalsTool(domainOrId);
    };
    agentTools.cookbookAcceptanceSummary = [this](const juce::String& domainOrId) {
        return cookbookAcceptanceSummaryTool(domainOrId);
    };
    agentTools.cookbookAcceptanceStart = [this](const juce::String& goalId) {
        return cookbookAcceptanceStartTool(goalId);
    };
    agentTools.cookbookAcceptanceRecord = [this](const juce::String& reportPath,
                                                 const juce::String& evidenceType,
                                                 const juce::String& label,
                                                 const juce::String& detail,
                                                 const juce::String& pathOrValue,
                                                 const juce::String& status) {
        return cookbookAcceptanceRecordTool(reportPath, evidenceType, label, detail, pathOrValue, status);
    };
    agentTools.capabilityGapRecord = [this](const juce::String& category,
                                            const juce::String& description,
                                            const juce::String& neededCapability,
                                            const juce::String& evidence,
                                            const juce::String& source,
                                            const juce::String& status) {
        return capabilityGapRecordTool(category, description, neededCapability, evidence, source, status);
    };
    agentTools.toolManifest = [this] { return buildAssistantToolManifestJson(); };
    agentTools.log = [this](const juce::String& text) { appendLog(text); };
    auto agent = std::make_unique<AgentPanel>(std::move(agentTools));
    auto* agentPanel = agent.get();
    openAgentSettingsDialog = [agentPanel] { agentPanel->showAiSettingsForSelected(); };
    dockManager->registerPanel("schematic", "Schematic", std::move(schematic), CreationDock::DockTargetZone::CenterTab);
    dockManager->registerPanel("simulation", "Simulation", std::move(analysis), CreationDock::DockTargetZone::CenterTab);
    dockManager->registerPanel("console", "Frust Math Console", std::make_unique<ConsolePanel>(logConsole), CreationDock::DockTargetZone::Bottom);
    dockManager->registerPanel("agent", "BYOK Agent", std::move(agent), CreationDock::DockTargetZone::Right);
    dockManager->registerPanel("properties", "Properties", std::move(properties), CreationDock::DockTargetZone::Right);
    dockManager->registerPanel("ingestion", "Spec Ingestion", std::make_unique<SpecIngestionPanel>(), CreationDock::DockTargetZone::Right);
    dockManager->registerPanel("sourcing", "Parts Sourcing", std::make_unique<PartsSourcingPanel>(), CreationDock::DockTargetZone::Right);
    dockManager->registerPanel("lab_bench", "Lab Bench", std::move(instruments), CreationDock::DockTargetZone::Right);

    dockManager->loadLayoutFromFile(layoutFile());
    appendLog("Electronics research shell initialized.");
}

ElectronicsWorkbench::~ElectronicsWorkbench()
{
    if (dockManager != nullptr)
        dockManager->saveLayoutToFile(layoutFile());
    menuBar = nullptr;
}

void ElectronicsWorkbench::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff171b20));
    g.setColour(juce::Colour(0xff26323d));
    g.fillRect(0, menuHeight, getWidth(), toolbarHeight);
}

void ElectronicsWorkbench::resized()
{
    auto area = getLocalBounds();
    menuBar->setBounds(area.removeFromTop(menuHeight));

    auto toolbar = area.removeFromTop(toolbarHeight).reduced(8, 4);
    titleLabel.setBounds(toolbar.removeFromLeft(230));
    newButton.setBounds(toolbar.removeFromLeft(70));
    toolbar.removeFromLeft(6);
    ercButton.setBounds(toolbar.removeFromLeft(70));
    toolbar.removeFromLeft(6);
    transientButton.setBounds(toolbar.removeFromLeft(100));
    toolbar.removeFromLeft(6);
    compileButton.setBounds(toolbar.removeFromLeft(140));
    toolbar.removeFromLeft(10);
    stampModeButton.setBounds(toolbar.removeFromLeft(90));
    snapModeButton.setBounds(toolbar.removeFromLeft(82));
    toolbar.removeFromLeft(8);
    zoomOutButton.setBounds(toolbar.removeFromLeft(34));
    toolbar.removeFromLeft(4);
    zoomResetButton.setBounds(toolbar.removeFromLeft(58));
    toolbar.removeFromLeft(4);
    zoomInButton.setBounds(toolbar.removeFromLeft(34));
    statusLabel.setBounds(toolbar);

    if (dockManager != nullptr)
        dockManager->setBounds(area);
}

void ElectronicsWorkbench::applySchematicZoom(float zoom)
{
    const auto clampedZoom = std::clamp(zoom, 0.5f, 2.5f);
    if (setSchematicZoom != nullptr)
        setSchematicZoom(clampedZoom);

    zoomResetButton.setButtonText(juce::String((int)std::round(clampedZoom * 100.0f)) + "%");
    appendLog("Schematic zoom set to " + juce::String((int)std::round(clampedZoom * 100.0f)) + "%.");
}

void ElectronicsWorkbench::adjustSchematicZoom(float factor)
{
    const auto currentZoom = getSchematicZoom != nullptr ? getSchematicZoom() : 1.0f;
    applySchematicZoom(currentZoom * factor);
}

juce::StringArray ElectronicsWorkbench::getMenuBarNames()
{
    return { "File", "Circuit", "Simulation", "Agent", "View", "Help" };
}

juce::PopupMenu ElectronicsWorkbench::getMenuForIndex(int, const juce::String& menuName)
{
    juce::PopupMenu menu;
    if (menuName == "File")
    {
        menu.addItem(newProject, "New Research Project");
        menu.addItem(openProject, "Open Project...");
        menu.addItem(saveProject, "Save Project");
    }
    else if (menuName == "Circuit")
    {
        menu.addItem(importComponent, "Import / Fetch Component Spec...");
        menu.addSeparator();
        menu.addItem(autoLayoutDiagramItem, "Auto Layout Diagram");
        menu.addSeparator();
        menu.addItem(runErc, "Run ERC");
    }
    else if (menuName == "Simulation")
    {
        menu.addItem(designRlcHighPass, "Design 10 Hz / 8 Ohm RLC High-Pass");
        menu.addSeparator();
        menu.addItem(runOperatingPoint, "Run Operating Point");
        menu.addItem(runTransient, "Run Transient");
        menu.addItem(runCompiledPreview, "Compile Realtime Preview");
    }
    else if (menuName == "Agent")
    {
        menu.addItem(openAgentSettings, "BYOK Agent Settings...");
        menu.addItem(exportAgentTools, "Export Tool Manifest");
    }
    else if (menuName == "View")
    {
        menu.addItem(resetLayout, "Reset Dock Layout");
    }
    else if (menuName == "Help")
    {
        menu.addItem(openResearchSpec, "Open Research Spec");
    }
    return menu;
}

void ElectronicsWorkbench::menuItemSelected(int menuItemID, int)
{
    switch (menuItemID)
    {
        case newProject: resetResearchState(); break;
        case saveProject: saveProjectFile(); break;
        case openProject: openProjectFile(); break;
        case resetLayout:
            if (dockManager != nullptr) dockManager->resetLayout();
            appendLog("Dock layout reset.");
            break;
        case importComponent: appendLog("Component ingestion stub: BYOK agent/provider workflow pending."); break;
        case autoLayoutDiagramItem: autoLayoutDiagram(); break;
        case runErc: runElectricalRuleCheck(); break;
        case designRlcHighPass: designRlcHighPassFilter(); break;
        case runOperatingPoint: exportCircuitArtifacts(); break;
        case runTransient: exportCircuitArtifacts(); break;
        case runCompiledPreview: appendLog("Compiled preview stub: circuit IR -> Frust backend pending."); break;
        case openAgentSettings:
            if (openAgentSettingsDialog != nullptr) openAgentSettingsDialog();
            else appendLog("BYOK agent settings are unavailable.");
            break;
        case exportAgentTools: exportAssistantToolManifest(); break;
        case openResearchSpec: showSpecDocument(); break;
        default: break;
    }
}

juce::File ElectronicsWorkbench::layoutFile() const
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("DjehutiElectronicsLab")
        .getChildFile("layout.json");
}

juce::File ElectronicsWorkbench::savedProjectFile() const
{
    return juce::File::getSpecialLocation(juce::File::currentExecutableFile)
        .getParentDirectory()
        .getParentDirectory()
        .getParentDirectory()
        .getChildFile("projects")
        .getChildFile("current")
        .getChildFile("circuit.json");
}

juce::File ElectronicsWorkbench::generatedRunDirectory() const
{
    return juce::File::getSpecialLocation(juce::File::currentExecutableFile)
        .getParentDirectory()
        .getParentDirectory()
        .getParentDirectory()
        .getChildFile("sim")
        .getChildFile("xyce")
        .getChildFile("runs")
        .getChildFile("generated");
}

void ElectronicsWorkbench::appendLog(const juce::String& text)
{
    statusLabel.setText(text, juce::dontSendNotification);
    if (logConsole != nullptr)
        logConsole->insertTextAtCaret("\n// " + text + "\n> ");
}

void ElectronicsWorkbench::resetResearchState()
{
    if (resetCircuit != nullptr)
        resetCircuit();
    else
        appendLog("New electronics research project initialized.");
}

void ElectronicsWorkbench::saveProjectFile()
{
    if (getCircuitJson == nullptr)
    {
        appendLog("No schematic exporter is available.");
        return;
    }

    const auto file = savedProjectFile();
    if (!file.getParentDirectory().createDirectory())
    {
        appendLog("Could not create project directory: " + file.getParentDirectory().getFullPathName());
        return;
    }

    if (!file.replaceWithText(getCircuitJson()))
    {
        appendLog("Could not save project file: " + file.getFullPathName());
        return;
    }

    appendLog("Saved project circuit to " + file.getFullPathName());
}

void ElectronicsWorkbench::openProjectFile()
{
    if (loadCircuitJson == nullptr)
    {
        appendLog("No project loader is available.");
        return;
    }

    const auto file = savedProjectFile();
    if (!file.existsAsFile())
    {
        appendLog("No saved project found yet: " + file.getFullPathName());
        return;
    }

    juce::String error;
    if (!loadCircuitJson(file.loadFileAsString(), error))
    {
        appendLog("Could not open project: " + error);
        return;
    }

    appendLog("Opened project circuit from " + file.getFullPathName());
}

juce::String ElectronicsWorkbench::buildAssistantToolManifestJson() const
{
    const auto runDir = generatedRunDirectory();
    juce::String text;
    text << "{\n";
    text << "  \"schemaVersion\": 1,\n";
    text << "  \"kind\": \"djehuti_assistant_tool_manifest\",\n";
    text << "  \"toolSurface\": \"prototype-local\",\n";
    text << "  \"artifactDirectory\": " << jsonQuote(runDir.getFullPathName()) << ",\n";
    text << "  \"localAgentApi\": {\n";
    text << "    \"schema\": \"djehuti-electronics-agent-api\",\n";
    text << "    \"discoveryFile\": " << jsonQuote(LocalAgentApi::getDiscoveryFile().getFullPathName()) << "\n";
    text << "  },\n";
    text << "  \"tools\": [\n";
    text << "    {\n";
    text << "      \"name\": \"cookbook_lookup\",\n";
    text << "      \"displayName\": \"cookbook.lookup\",\n";
    text << "      \"description\": \"Search structured electronics cookbook cards for topology candidates, equations, analysis recipes, validation criteria, and capability gaps.\",\n";
    text << "      \"mode\": \"read_only_knowledge\",\n";
    text << "      \"inputs\": {\n";
    text << "        \"query\": \"engineering requirement or cookbook topic\",\n";
    text << "        \"maxCards\": \"optional maximum number of cards\"\n";
    text << "      },\n";
    text << "      \"outputs\": { \"cards\": \"matching cookbook/knowledge cards with provenance\" }\n";
    text << "    },\n";
    text << "    {\n";
    text << "      \"name\": \"cookbook_coverage\",\n";
    text << "      \"displayName\": \"cookbook.coverage\",\n";
    text << "      \"description\": \"Report cookbook coverage against the file-backed taxonomy and identify missing recipe categories.\",\n";
    text << "      \"mode\": \"read_only_knowledge\",\n";
    text << "      \"inputs\": {},\n";
    text << "      \"outputs\": { \"coveredCategories\": \"array\", \"missingCategories\": \"array\", \"coverageRatio\": \"number\" }\n";
    text << "    },\n";
    text << "    {\n";
    text << "      \"name\": \"cookbook_validate\",\n";
    text << "      \"displayName\": \"cookbook.validate\",\n";
    text << "      \"description\": \"Validate cookbook entries for required structured fields, taxonomy alignment, duplicate ids, and malformed raw entries.\",\n";
    text << "      \"mode\": \"read_only_knowledge\",\n";
    text << "      \"inputs\": {},\n";
    text << "      \"outputs\": { \"status\": \"passed or failed\", \"errors\": \"array\", \"warnings\": \"array\" }\n";
    text << "    },\n";
    text << "    {\n";
    text << "      \"name\": \"cookbook_acceptance_goals\",\n";
    text << "      \"displayName\": \"cookbook.acceptance_goals\",\n";
    text << "      \"description\": \"List representative agent acceptance goals, optionally filtered by domain or exact goal id.\",\n";
    text << "      \"mode\": \"read_only_knowledge\",\n";
    text << "      \"inputs\": { \"domainOrId\": \"optional domain or exact goal id\" },\n";
    text << "      \"outputs\": { \"goals\": \"matching acceptance goals\", \"requiredDomains\": \"array\" }\n";
    text << "    },\n";
    text << "    {\n";
    text << "      \"name\": \"cookbook_acceptance_summary\",\n";
    text << "      \"displayName\": \"cookbook.acceptance_summary\",\n";
    text << "      \"description\": \"Summarize generated acceptance run reports, evidence counts, and determinations.\",\n";
    text << "      \"mode\": \"read_only_generated_artifacts\",\n";
    text << "      \"inputs\": { \"domainOrId\": \"optional domain or exact goal id\" },\n";
    text << "      \"outputs\": { \"runs\": \"matching generated acceptance report summaries\" }\n";
    text << "    },\n";
    text << "    {\n";
    text << "      \"name\": \"cookbook_acceptance_start\",\n";
    text << "      \"displayName\": \"cookbook.acceptance_start\",\n";
    text << "      \"description\": \"Create a structured acceptance report scaffold for a representative goal id.\",\n";
    text << "      \"mode\": \"write_generated_artifacts\",\n";
    text << "      \"inputs\": { \"goalId\": \"exact acceptance goal id\" },\n";
    text << "      \"outputs\": { \"jsonReport\": \"acceptance report scaffold\", \"markdownReport\": \"human-readable checklist\" }\n";
    text << "    },\n";
    text << "    {\n";
    text << "      \"name\": \"cookbook_acceptance_record\",\n";
    text << "      \"displayName\": \"cookbook.acceptance_record\",\n";
    text << "      \"description\": \"Append structured evidence to an acceptance report created by cookbook_acceptance_start.\",\n";
    text << "      \"mode\": \"write_generated_artifacts\",\n";
    text << "      \"inputs\": {\n";
    text << "        \"reportPath\": \"jsonReport path returned by cookbook_acceptance_start\",\n";
    text << "        \"evidenceType\": \"retrieved_card, tool_call, artifact, capability_gap, criterion, or note\",\n";
    text << "        \"label\": \"short evidence label\",\n";
    text << "        \"detail\": \"evidence details\",\n";
    text << "        \"pathOrValue\": \"artifact path, tool result path, card id, or measured value\",\n";
    text << "        \"status\": \"observed, passed, failed, gap, unverified, or not_started\"\n";
    text << "      },\n";
    text << "      \"outputs\": { \"jsonReport\": \"updated acceptance report\", \"markdownReport\": \"updated human-readable log\" }\n";
    text << "    },\n";
    text << "    {\n";
    text << "      \"name\": \"capability_gap_record\",\n";
    text << "      \"displayName\": \"capability_gap.record\",\n";
    text << "      \"description\": \"Append a reusable missing-capability record to the project gap registry.\",\n";
    text << "      \"mode\": \"write_project_memory\",\n";
    text << "      \"inputs\": {\n";
    text << "        \"category\": \"solver, component_model, analysis, plotting, instrument, ui, or agent_workflow\",\n";
    text << "        \"description\": \"what blocked the engineering step\",\n";
    text << "        \"neededCapability\": \"reusable tool or app capability needed to close the gap\",\n";
    text << "        \"evidence\": \"tool result, report path, or observation proving the gap\",\n";
    text << "        \"source\": \"acceptance goal id, report path, cookbook card id, or user goal\",\n";
    text << "        \"status\": \"open, planned, in_progress, closed, or deferred\"\n";
    text << "      },\n";
    text << "      \"outputs\": { \"gapRegistry\": \"project-local JSONL capability gap registry\" }\n";
    text << "    },\n";
    text << "    {\n";
    text << "      \"name\": \"circuit_inspect\",\n";
    text << "      \"displayName\": \"circuit.inspect\",\n";
    text << "      \"description\": \"Read the current authoritative circuit JSON from the schematic model.\",\n";
    text << "      \"mode\": \"read_only_analysis\",\n";
    text << "      \"inputs\": {},\n";
    text << "      \"outputs\": { \"circuitJson\": \"inline JSON object\" }\n";
    text << "    },\n";
    text << "    {\n";
    text << "      \"name\": \"circuit_run_erc\",\n";
    text << "      \"displayName\": \"circuit.run_erc\",\n";
    text << "      \"description\": \"Run Electrical Rule Check on the current schematic model.\",\n";
    text << "      \"mode\": \"read_only_analysis\",\n";
    text << "      \"inputs\": {},\n";
    text << "      \"outputs\": {\n";
    text << "        \"markdownReport\": " << jsonQuote(runDir.getChildFile("erc_report.md").getFullPathName()) << ",\n";
    text << "        \"jsonResult\": " << jsonQuote(runDir.getChildFile("erc_tool_result.json").getFullPathName()) << "\n";
    text << "      }\n";
    text << "    },\n";
    text << "    {\n";
    text << "      \"name\": \"simulation_export_artifacts\",\n";
    text << "      \"displayName\": \"simulation.export_artifacts\",\n";
    text << "      \"description\": \"Export circuit.json, generated.cir, and lab_instruments.json for solver/dataset work.\",\n";
    text << "      \"mode\": \"write_generated_artifacts\",\n";
    text << "      \"outputs\": {\n";
    text << "        \"circuitJson\": " << jsonQuote(runDir.getChildFile("circuit.json").getFullPathName()) << ",\n";
    text << "        \"xyceNetlist\": " << jsonQuote(runDir.getChildFile("generated.cir").getFullPathName()) << ",\n";
    text << "        \"instrumentJson\": " << jsonQuote(runDir.getChildFile("lab_instruments.json").getFullPathName()) << "\n";
    text << "      }\n";
    text << "    },\n";
    text << "    {\n";
    text << "      \"name\": \"agent_write_markdown\",\n";
    text << "      \"displayName\": \"agent.write_markdown\",\n";
    text << "      \"description\": \"Write markdown into the agent workspace and render it in the BYOK Agent window with KaTeX equation support.\",\n";
    text << "      \"mode\": \"write_generated_artifacts_and_update_agent_transcript\",\n";
    text << "      \"status\": \"active\",\n";
    text << "      \"inputs\": { \"title\": \"report title\", \"markdown\": \"markdown source; use $...$ for inline equations and $$ on separate lines for display equations\" },\n";
    text << "      \"outputs\": { \"markdownPath\": \"written markdown source\", \"htmlPath\": \"rendered HTML report\" }\n";
    text << "    },\n";
    text << "    {\n";
    text << "      \"name\": \"research_web_search\",\n";
    text << "      \"displayName\": \"research.web_search\",\n";
    text << "      \"description\": \"Search the web for current references, standards, datasheets, or source links when local cookbook/app state is not enough.\",\n";
    text << "      \"mode\": \"read_external_web\",\n";
    text << "      \"status\": \"active\",\n";
    text << "      \"inputs\": { \"query\": \"focused search query\", \"maxResults\": \"1 to 8 compact results\" },\n";
    text << "      \"outputs\": { \"results\": \"title/snippet/url entries\" }\n";
    text << "    },\n";
    text << "    {\n";
    text << "      \"name\": \"filter_design_high_pass\",\n";
    text << "      \"displayName\": \"filter.design_high_pass\",\n";
    text << "      \"description\": \"Design a matched RLC 2nd order high-pass filter, draw it on the schematic, run the internal AC sweep, and export graph/report artifacts.\",\n";
    text << "      \"mode\": \"modify_schematic_model_and_write_generated_artifacts\",\n";
    text << "      \"status\": \"active\",\n";
    text << "      \"inputs\": {\n";
    text << "        \"cutoffHz\": \"target cutoff frequency in Hz\",\n";
    text << "        \"impedanceOhms\": \"matched source/load impedance in ohms\"\n";
    text << "      },\n";
    text << "      \"outputs\": {\n";
    text << "        \"spiceNetlist\": " << jsonQuote(runDir.getChildFile("rlc_high_pass_ac.cir").getFullPathName()) << ",\n";
    text << "        \"csv\": " << jsonQuote(runDir.getChildFile("rlc_high_pass_ac.csv").getFullPathName()) << ",\n";
    text << "        \"svg\": " << jsonQuote(runDir.getChildFile("rlc_high_pass_response.svg").getFullPathName()) << ",\n";
    text << "        \"report\": " << jsonQuote(runDir.getChildFile("rlc_high_pass_report.md").getFullPathName()) << "\n";
    text << "      }\n";
    text << "    },\n";
    text << "    {\n";
    text << "      \"name\": \"amplifier_design_push_pull\",\n";
    text << "      \"displayName\": \"amplifier.design_push_pull\",\n";
    text << "      \"description\": \"Design a diode-biased complementary push-pull audio output stage with source, rails, 8 ohm load, grounds, and scope instrumentation.\",\n";
    text << "      \"mode\": \"modify_schematic_model_and_write_generated_artifacts\",\n";
    text << "      \"status\": \"active\",\n";
    text << "      \"inputs\": {},\n";
    text << "      \"outputs\": {\n";
    text << "        \"circuitJson\": " << jsonQuote(runDir.getChildFile("push_pull_amplifier_circuit.json").getFullPathName()) << ",\n";
    text << "        \"previewNetlist\": " << jsonQuote(runDir.getChildFile("push_pull_amplifier_preview.cir").getFullPathName()) << ",\n";
    text << "        \"ercReport\": " << jsonQuote(runDir.getChildFile("push_pull_amplifier_erc.md").getFullPathName()) << ",\n";
    text << "        \"report\": " << jsonQuote(runDir.getChildFile("push_pull_amplifier_report.md").getFullPathName()) << "\n";
    text << "      }\n";
    text << "    },\n";
    text << "    {\n";
    text << "      \"name\": \"schematic_auto_layout\",\n";
    text << "      \"displayName\": \"schematic.auto_layout\",\n";
    text << "      \"description\": \"Arrange the current schematic into a readable left-to-right diagram using standard spacing conventions.\",\n";
    text << "      \"mode\": \"modify_schematic_model\",\n";
    text << "      \"status\": \"active\",\n";
    text << "      \"inputs\": {},\n";
    text << "      \"outputs\": { \"layout\": \"updated component coordinates\" }\n";
    text << "    },\n";
    text << "    {\n";
    text << "      \"name\": \"schematic_auto_layout_selection\",\n";
    text << "      \"displayName\": \"schematic.auto_layout_selection\",\n";
    text << "      \"description\": \"Arrange only the currently selected schematic components.\",\n";
    text << "      \"mode\": \"modify_schematic_model\",\n";
    text << "      \"status\": \"active\",\n";
    text << "      \"inputs\": {},\n";
    text << "      \"outputs\": { \"layout\": \"updated selected component coordinates\" }\n";
    text << "    },\n";
    text << "    {\n";
    text << "      \"name\": \"schematic_place_symbol\",\n";
    text << "      \"displayName\": \"schematic.place_symbol\",\n";
    text << "      \"description\": \"Create or extend diagrams by placing symbols and instrument nodes on the schematic grid.\",\n";
    text << "      \"mode\": \"modify_schematic_model\",\n";
    text << "      \"status\": \"active\",\n";
    text << "      \"inputs\": {\n";
    text << "        \"symbolId\": [\"resistor\", \"capacitor\", \"inductor\", \"diode\", \"voltage_source\", \"ground\", \"oscilloscope_2ch\", \"digital_multimeter\"],\n";
    text << "        \"x\": \"grid coordinate\",\n";
    text << "        \"y\": \"grid coordinate\",\n";
    text << "        \"value\": \"optional component value or instrument mode\",\n";
    text << "        \"frequency\": \"optional source frequency\",\n";
    text << "        \"busName\": \"optional rail/net label\"\n";
    text << "      }\n";
    text << "    },\n";
    text << "    {\n";
    text << "      \"name\": \"schematic_set_component_properties\",\n";
    text << "      \"displayName\": \"schematic.set_component_properties\",\n";
    text << "      \"description\": \"Set component value, source frequency, bus/net name, family, or manufacturer part by reference designator.\",\n";
    text << "      \"mode\": \"modify_schematic_model\",\n";
    text << "      \"status\": \"active\",\n";
    text << "      \"inputs\": {\n";
    text << "        \"refdes\": \"existing component reference designator\",\n";
    text << "        \"value\": \"optional component value or instrument mode\",\n";
    text << "        \"frequency\": \"optional source frequency\",\n";
    text << "        \"busName\": \"optional rail/net label\",\n";
    text << "        \"family\": \"optional component family\",\n";
    text << "        \"manufacturerPart\": \"optional manufacturer part number\"\n";
    text << "      }\n";
    text << "    },\n";
    text << "    {\n";
    text << "      \"name\": \"schematic_connect\",\n";
    text << "      \"displayName\": \"schematic.connect\",\n";
    text << "      \"description\": \"Connect two schematic pins or junctions by label.\",\n";
    text << "      \"mode\": \"modify_schematic_model\",\n";
    text << "      \"status\": \"active\",\n";
    text << "      \"inputs\": {\n";
    text << "        \"a\": \"node label such as R1.1, V2.+, GND3.0, SCOPE4.CH1, or N1\",\n";
    text << "        \"b\": \"node label\"\n";
    text << "      }\n";
    text << "    },\n";
    text << "    {\n";
    text << "      \"name\": \"instrument_open_panel\",\n";
    text << "      \"displayName\": \"instrument.open_panel\",\n";
    text << "      \"description\": \"Open a floating instrument panel for a schematic instrument node.\",\n";
    text << "      \"status\": \"active\",\n";
    text << "      \"inputs\": { \"refdes\": \"instrument reference designator\" }\n";
    text << "    }\n";
    text << "  ],\n";
    text << "  \"instrumentPolicy\": {\n";
    text << "    \"preferredPlacement\": \"schematic_node\",\n";
    text << "    \"preferredWindowMode\": \"floating\",\n";
    text << "    \"dockableLater\": true,\n";
    text << "    \"supportedNodes\": [\"oscilloscope_2ch\", \"digital_multimeter\"]\n";
    text << "  }\n";
    text << "}\n";
    return text;
}

juce::String ElectronicsWorkbench::cookbookLookupTool(const juce::String& query, int maxCards) const
{
    const auto limit = juce::jlimit(1, 12, maxCards <= 0 ? 6 : maxCards);
    const auto retrieved = electronics_knowledge::retrieve(query, limit);

    juce::String result;
    result << "{\n";
    result << "  \"ok\": true,\n";
    result << "  \"schemaVersion\": 1,\n";
    result << "  \"kind\": \"djehuti_assistant_tool_result\",\n";
    result << "  \"tool\": \"cookbook_lookup\",\n";
    result << "  \"displayTool\": \"cookbook.lookup\",\n";
    result << "  \"query\": " << jsonQuote(query.trim()) << ",\n";
    result << "  \"knowledgeRoot\": " << jsonQuote(electronics_knowledge::getKnowledgeRoot().getFullPathName()) << ",\n";
    result << "  \"projectMemoryCards\": " << jsonQuote(electronics_knowledge::getProjectMemoryCardsFile().getFullPathName()) << ",\n";
    result << "  \"capabilityGapRegistry\": " << jsonQuote(electronics_knowledge::getCapabilityGapsFile().getFullPathName()) << ",\n";
    result << "  \"cards\": [\n";

    for (size_t index = 0; index < retrieved.cards.size(); ++index)
    {
        const auto& card = retrieved.cards[index];
        result << "    {\n";
        result << "      \"id\": " << jsonQuote(card.id) << ",\n";
        result << "      \"kind\": " << jsonQuote(card.kind) << ",\n";
        result << "      \"title\": " << jsonQuote(card.title) << ",\n";
        result << "      \"source\": " << jsonQuote(card.source) << ",\n";
        result << "      \"priority\": " << card.priority << ",\n";
        result << "      \"tokens\": [";
        for (int tokenIndex = 0; tokenIndex < card.tokens.size(); ++tokenIndex)
        {
            if (tokenIndex > 0)
                result << ", ";
            result << jsonQuote(card.tokens[tokenIndex]);
        }
        result << "],\n";
        result << "      \"text\": " << jsonQuote(card.text);
        if (card.rawJson.isNotEmpty())
            result << ",\n      \"entry\": " << card.rawJson << "\n";
        else
            result << "\n";
        result << "    }";
        if (index + 1 < retrieved.cards.size())
            result << ",";
        result << "\n";
    }

    result << "  ]\n";
    result << "}\n";
    return result;
}

juce::String ElectronicsWorkbench::cookbookCoverageTool() const
{
    const auto taxonomyFile = electronics_knowledge::getKnowledgeRoot().getChildFile("COOKBOOK_TAXONOMY.json");
    const auto parsedTaxonomy = juce::JSON::parse(taxonomyFile.loadFileAsString());
    if (!parsedTaxonomy.isObject())
    {
        return "{ \"ok\": false, \"tool\": \"cookbook_coverage\", \"displayTool\": \"cookbook.coverage\", \"error\": "
            + jsonQuote("Could not read cookbook taxonomy: " + taxonomyFile.getFullPathName()) + " }";
    }

    juce::StringArray requiredCategories;
    if (auto* categories = parsedTaxonomy.getProperty("categories", {}).getArray())
        for (const auto& category : *categories)
            requiredCategories.add(category.toString());
    requiredCategories.removeEmptyStrings();

    std::map<juce::String, juce::StringArray> entriesByCategory;
    int cookbookEntryCount = 0;
    for (const auto& card : electronics_knowledge::allCards())
    {
        if (!card.kind.startsWithIgnoreCase("cookbook"))
            continue;

        ++cookbookEntryCount;
        const auto parsedCard = juce::JSON::parse(card.rawJson);
        auto category = parsedCard.getProperty("category", {}).toString().trim();
        if (category.isEmpty())
            category = "Uncategorized";
        entriesByCategory[category].add(card.id);
    }

    juce::StringArray missingCategories;
    juce::StringArray coveredCategories;
    for (const auto& category : requiredCategories)
    {
        if (entriesByCategory[category].isEmpty())
            missingCategories.add(category);
        else
            coveredCategories.add(category);
    }

    const auto requiredCount = requiredCategories.size();
    const auto coveredCount = coveredCategories.size();
    const auto coverageRatio = requiredCount == 0 ? 0.0 : (double)coveredCount / (double)requiredCount;

    auto appendStringArray = [](juce::String& out, const juce::StringArray& values) {
        out << "[";
        for (int index = 0; index < values.size(); ++index)
        {
            if (index > 0)
                out << ", ";
            out << jsonQuote(values[index]);
        }
        out << "]";
    };

    juce::String result;
    result << "{\n";
    result << "  \"ok\": true,\n";
    result << "  \"schemaVersion\": 1,\n";
    result << "  \"kind\": \"djehuti_assistant_tool_result\",\n";
    result << "  \"tool\": \"cookbook_coverage\",\n";
    result << "  \"displayTool\": \"cookbook.coverage\",\n";
    result << "  \"taxonomyFile\": " << jsonQuote(taxonomyFile.getFullPathName()) << ",\n";
    result << "  \"cookbookEntryCount\": " << cookbookEntryCount << ",\n";
    result << "  \"requiredCategoryCount\": " << requiredCount << ",\n";
    result << "  \"coveredCategoryCount\": " << coveredCount << ",\n";
    result << "  \"missingCategoryCount\": " << missingCategories.size() << ",\n";
    result << "  \"coverageRatio\": " << numberText(coverageRatio, 6) << ",\n";
    result << "  \"coveredCategories\": ";
    appendStringArray(result, coveredCategories);
    result << ",\n";
    result << "  \"missingCategories\": ";
    appendStringArray(result, missingCategories);
    result << ",\n";
    result << "  \"categories\": [\n";

    for (int index = 0; index < requiredCategories.size(); ++index)
    {
        const auto category = requiredCategories[index];
        const auto ids = entriesByCategory[category];
        result << "    {\n";
        result << "      \"name\": " << jsonQuote(category) << ",\n";
        result << "      \"entryCount\": " << ids.size() << ",\n";
        result << "      \"entryIds\": ";
        appendStringArray(result, ids);
        result << "\n";
        result << "    }";
        if (index + 1 < requiredCategories.size())
            result << ",";
        result << "\n";
    }

    result << "  ]\n";
    result << "}\n";
    return result;
}

juce::String ElectronicsWorkbench::cookbookValidateTool() const
{
    const auto taxonomyFile = electronics_knowledge::getKnowledgeRoot().getChildFile("COOKBOOK_TAXONOMY.json");
    const auto parsedTaxonomy = juce::JSON::parse(taxonomyFile.loadFileAsString());
    if (!parsedTaxonomy.isObject())
    {
        return "{ \"ok\": false, \"tool\": \"cookbook_validate\", \"displayTool\": \"cookbook.validate\", \"error\": "
            + jsonQuote("Could not read cookbook taxonomy: " + taxonomyFile.getFullPathName()) + " }";
    }

    std::set<std::string> taxonomyCategories;
    if (auto* categories = parsedTaxonomy.getProperty("categories", {}).getArray())
        for (const auto& category : *categories)
            taxonomyCategories.insert(category.toString().toStdString());

    juce::StringArray errors;
    juce::StringArray warnings;
    std::set<std::string> seenIds;
    int cookbookEntryCount = 0;

    const juce::StringArray requiredFields {
        "category",
        "subcategory",
        "purpose",
        "whenToUse",
        "whenNotToUse",
        "requiredComponents",
        "designProcedure",
        "toolRecipe",
        "analysisRecipe",
        "validationCriteria",
        "failureModes",
        "iterationRules",
        "capabilityGaps",
        "provenance",
        "text"
    };

    const juce::StringArray recommendedFields {
        "parameters",
        "relatedEntries"
    };

    auto hasUsefulProperty = [](const juce::var& object, const juce::String& name) {
        const auto value = object.getProperty(name, {});
        if (value.isVoid())
            return false;
        if (value.isString())
            return value.toString().trim().isNotEmpty();
        if (auto* array = value.getArray())
            return !array->isEmpty();
        if (auto* dyn = value.getDynamicObject())
            return dyn->getProperties().size() > 0;
        return true;
    };

    for (const auto& card : electronics_knowledge::allCards())
    {
        if (!card.kind.startsWithIgnoreCase("cookbook"))
            continue;

        ++cookbookEntryCount;
        const auto id = card.id.isNotEmpty() ? card.id : juce::String("<missing id>");
        if (!seenIds.insert(id.toStdString()).second)
            errors.add(id + ": duplicate cookbook id.");

        const auto parsedCard = juce::JSON::parse(card.rawJson);
        if (!parsedCard.isObject())
        {
            errors.add(id + ": raw cookbook JSON is malformed.");
            continue;
        }

        const auto category = parsedCard.getProperty("category", {}).toString().trim();
        if (category.isEmpty())
        {
            errors.add(id + ": missing category.");
        }
        else if (taxonomyCategories.count(category.toStdString()) == 0)
        {
            errors.add(id + ": category is not in COOKBOOK_TAXONOMY.json: " + category);
        }

        for (const auto& field : requiredFields)
            if (!hasUsefulProperty(parsedCard, field))
                errors.add(id + ": missing required cookbook field `" + field + "`.");

        for (const auto& field : recommendedFields)
            if (!hasUsefulProperty(parsedCard, field))
                warnings.add(id + ": missing recommended cookbook field `" + field + "`.");

        const auto text = parsedCard.getProperty("text", {}).toString();
        if (text.length() < 120)
            warnings.add(id + ": summary text is short for retrieval.");
    }

    if (cookbookEntryCount == 0)
        errors.add("No cookbook entries were found.");

    auto appendStringArray = [](juce::String& out, const juce::StringArray& values) {
        out << "[";
        for (int index = 0; index < values.size(); ++index)
        {
            if (index > 0)
                out << ", ";
            out << jsonQuote(values[index]);
        }
        out << "]";
    };

    juce::String result;
    result << "{\n";
    result << "  \"ok\": true,\n";
    result << "  \"schemaVersion\": 1,\n";
    result << "  \"kind\": \"djehuti_assistant_tool_result\",\n";
    result << "  \"tool\": \"cookbook_validate\",\n";
    result << "  \"displayTool\": \"cookbook.validate\",\n";
    result << "  \"status\": " << jsonQuote(errors.isEmpty() ? "passed" : "failed") << ",\n";
    result << "  \"cookbookEntryCount\": " << cookbookEntryCount << ",\n";
    result << "  \"errorCount\": " << errors.size() << ",\n";
    result << "  \"warningCount\": " << warnings.size() << ",\n";
    result << "  \"errors\": ";
    appendStringArray(result, errors);
    result << ",\n";
    result << "  \"warnings\": ";
    appendStringArray(result, warnings);
    result << "\n";
    result << "}\n";
    return result;
}

juce::String ElectronicsWorkbench::cookbookAcceptanceGoalsTool(const juce::String& domainOrId) const
{
    const auto acceptanceFile = electronics_knowledge::getKnowledgeRoot().getChildFile("COOKBOOK_ACCEPTANCE_GOALS.json");
    const auto parsedAcceptance = juce::JSON::parse(acceptanceFile.loadFileAsString());
    if (!parsedAcceptance.isObject())
    {
        return "{ \"ok\": false, \"tool\": \"cookbook_acceptance_goals\", \"displayTool\": \"cookbook.acceptance_goals\", \"error\": "
            + jsonQuote("Could not read cookbook acceptance goals: " + acceptanceFile.getFullPathName()) + " }";
    }

    const auto filter = domainOrId.trim();
    const auto requiredDomains = parsedAcceptance.getProperty("requiredDomains", {});
    const auto goals = parsedAcceptance.getProperty("goals", {});
    auto* goalArray = goals.getArray();

    if (goalArray == nullptr)
    {
        return "{ \"ok\": false, \"tool\": \"cookbook_acceptance_goals\", \"displayTool\": \"cookbook.acceptance_goals\", \"error\": "
            + jsonQuote("Acceptance goals file has no goals array: " + acceptanceFile.getFullPathName()) + " }";
    }

    auto appendArray = [](juce::String& out, const juce::var& value) {
        const auto text = juce::JSON::toString(value, true);
        out << (text.isNotEmpty() ? text : "[]");
    };

    juce::String result;
    result << "{\n";
    result << "  \"ok\": true,\n";
    result << "  \"schemaVersion\": 1,\n";
    result << "  \"kind\": \"djehuti_assistant_tool_result\",\n";
    result << "  \"tool\": \"cookbook_acceptance_goals\",\n";
    result << "  \"displayTool\": \"cookbook.acceptance_goals\",\n";
    result << "  \"acceptanceFile\": " << jsonQuote(acceptanceFile.getFullPathName()) << ",\n";
    result << "  \"filter\": " << jsonQuote(filter) << ",\n";
    result << "  \"requiredDomains\": ";
    appendArray(result, requiredDomains);
    result << ",\n";
    result << "  \"goals\": [\n";

    int matched = 0;
    for (const auto& goal : *goalArray)
    {
        const auto id = goal.getProperty("id", {}).toString();
        const auto domain = goal.getProperty("domain", {}).toString();
        const auto include = filter.isEmpty()
            || id.equalsIgnoreCase(filter)
            || domain.equalsIgnoreCase(filter);
        if (!include)
            continue;

        if (matched > 0)
            result << ",\n";
        result << juce::JSON::toString(goal, true);
        ++matched;
    }

    result << "\n  ],\n";
    result << "  \"goalCount\": " << matched << "\n";
    result << "}\n";
    return result;
}

juce::String ElectronicsWorkbench::cookbookAcceptanceSummaryTool(const juce::String& domainOrId) const
{
    const auto acceptanceRoot = generatedRunDirectory().getChildFile("acceptance");
    juce::Array<juce::File> reports;
    if (acceptanceRoot.exists())
        acceptanceRoot.findChildFiles(reports, juce::File::findFiles, true, "acceptance_report.json");
    reports.sort();

    const auto filter = domainOrId.trim();
    auto arrayCount = [](const juce::var& value) {
        if (auto* array = value.getArray())
            return array->size();
        return 0;
    };

    auto evidenceCount = [&arrayCount](const juce::var& evidence, const char* name) {
        return arrayCount(evidence.getProperty(name, {}));
    };

    auto ratioText = [](int actual, int required) {
        if (required <= 0)
            return juce::String("1");
        return numberText(juce::jlimit(0.0, 1.0, (double)actual / (double)required), 6);
    };

    juce::String result;
    result << "{\n";
    result << "  \"ok\": true,\n";
    result << "  \"schemaVersion\": 1,\n";
    result << "  \"kind\": \"djehuti_assistant_tool_result\",\n";
    result << "  \"tool\": \"cookbook_acceptance_summary\",\n";
    result << "  \"displayTool\": \"cookbook.acceptance_summary\",\n";
    result << "  \"acceptanceRoot\": " << jsonQuote(acceptanceRoot.getFullPathName()) << ",\n";
    result << "  \"filter\": " << jsonQuote(filter) << ",\n";
    result << "  \"runs\": [\n";

    int matched = 0;
    int unreadable = 0;
    for (const auto& report : reports)
    {
        const auto parsed = juce::JSON::parse(report.loadFileAsString());
        if (!parsed.isObject())
        {
            ++unreadable;
            continue;
        }

        const auto goal = parsed.getProperty("goal", {});
        const auto goalId = goal.getProperty("id", report.getParentDirectory().getFileName()).toString();
        const auto domain = goal.getProperty("domain", {}).toString();
        const auto include = filter.isEmpty()
            || goalId.equalsIgnoreCase(filter)
            || domain.equalsIgnoreCase(filter);
        if (!include)
            continue;

        const auto evidence = parsed.getProperty("evidence", {});
        const auto requiredCards = arrayCount(goal.getProperty("mustRetrieve", {}));
        const auto requiredTools = arrayCount(goal.getProperty("requiredToolEvidence", {}));
        const auto requiredCriteria = arrayCount(goal.getProperty("passCriteria", {}));
        const auto criteriaCount = arrayCount(parsed.getProperty("criteriaResults", {}));
        const auto retrievedCards = evidenceCount(evidence, "retrievedCards");
        const auto toolCalls = evidenceCount(evidence, "toolCalls");
        const auto artifacts = evidenceCount(evidence, "artifacts");
        const auto capabilityGaps = evidenceCount(evidence, "capabilityGaps");
        const auto notes = evidenceCount(evidence, "notes");
        const auto retrievalRatio = requiredCards <= 0 ? 1.0 : juce::jlimit(0.0, 1.0, (double)retrievedCards / (double)requiredCards);
        const auto toolRatio = requiredTools <= 0 ? 1.0 : juce::jlimit(0.0, 1.0, (double)toolCalls / (double)requiredTools);
        const auto criteriaRatio = requiredCriteria <= 0 ? 1.0 : juce::jlimit(0.0, 1.0, (double)criteriaCount / (double)requiredCriteria);
        const auto readinessScore = (retrievalRatio + toolRatio + criteriaRatio) / 3.0;

        if (matched > 0)
            result << ",\n";
        result << "    {\n";
        result << "      \"goalId\": " << jsonQuote(goalId) << ",\n";
        result << "      \"domain\": " << jsonQuote(domain) << ",\n";
        result << "      \"status\": " << jsonQuote(parsed.getProperty("status", "unknown").toString()) << ",\n";
        result << "      \"finalDetermination\": " << jsonQuote(parsed.getProperty("finalDetermination", "unverified").toString()) << ",\n";
        result << "      \"reportPath\": " << jsonQuote(report.getFullPathName()) << ",\n";
        result << "      \"markdownReport\": " << jsonQuote(report.getSiblingFile("acceptance_report.md").getFullPathName()) << ",\n";
        result << "      \"requiredEvidenceCounts\": {\n";
        result << "        \"retrievedCards\": " << requiredCards << ",\n";
        result << "        \"toolCalls\": " << requiredTools << ",\n";
        result << "        \"criteriaResults\": " << requiredCriteria << "\n";
        result << "      },\n";
        result << "      \"evidenceCounts\": {\n";
        result << "        \"retrievedCards\": " << retrievedCards << ",\n";
        result << "        \"toolCalls\": " << toolCalls << ",\n";
        result << "        \"artifacts\": " << artifacts << ",\n";
        result << "        \"capabilityGaps\": " << capabilityGaps << ",\n";
        result << "        \"criteriaResults\": " << criteriaCount << ",\n";
        result << "        \"notes\": " << notes << "\n";
        result << "      },\n";
        result << "      \"evidenceReadiness\": {\n";
        result << "        \"score\": " << numberText(readinessScore, 6) << ",\n";
        result << "        \"retrievalRatio\": " << ratioText(retrievedCards, requiredCards) << ",\n";
        result << "        \"toolEvidenceRatio\": " << ratioText(toolCalls, requiredTools) << ",\n";
        result << "        \"criteriaRatio\": " << ratioText(criteriaCount, requiredCriteria) << ",\n";
        result << "        \"needsRetrievedCards\": " << (retrievedCards < requiredCards ? "true" : "false") << ",\n";
        result << "        \"needsToolEvidence\": " << (toolCalls < requiredTools ? "true" : "false") << ",\n";
        result << "        \"needsCriteriaResults\": " << (criteriaCount < requiredCriteria ? "true" : "false") << "\n";
        result << "      }\n";
        result << "    }";
        ++matched;
    }

    result << "\n  ],\n";
    result << "  \"runCount\": " << matched << ",\n";
    result << "  \"reportFileCount\": " << reports.size() << ",\n";
    result << "  \"unreadableReportCount\": " << unreadable << "\n";
    result << "}\n";
    return result;
}

juce::String ElectronicsWorkbench::cookbookAcceptanceStartTool(const juce::String& goalId) const
{
    const auto acceptanceFile = electronics_knowledge::getKnowledgeRoot().getChildFile("COOKBOOK_ACCEPTANCE_GOALS.json");
    const auto parsedAcceptance = juce::JSON::parse(acceptanceFile.loadFileAsString());
    auto* goals = parsedAcceptance.getProperty("goals", {}).getArray();
    if (!parsedAcceptance.isObject() || goals == nullptr)
    {
        return "{ \"ok\": false, \"tool\": \"cookbook_acceptance_start\", \"displayTool\": \"cookbook.acceptance_start\", \"error\": "
            + jsonQuote("Could not read cookbook acceptance goals: " + acceptanceFile.getFullPathName()) + " }";
    }

    juce::var selectedGoal;
    for (const auto& goal : *goals)
    {
        if (goal.getProperty("id", {}).toString().equalsIgnoreCase(goalId))
        {
            selectedGoal = goal;
            break;
        }
    }

    if (!selectedGoal.isObject())
    {
        return "{ \"ok\": false, \"tool\": \"cookbook_acceptance_start\", \"displayTool\": \"cookbook.acceptance_start\", \"error\": "
            + jsonQuote("Unknown acceptance goal id: " + goalId) + " }";
    }

    auto safeName = goalId.retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-");
    if (safeName.isEmpty())
        safeName = "acceptance_goal";

    const auto runDir = generatedRunDirectory().getChildFile("acceptance").getChildFile(safeName);
    if (!runDir.createDirectory())
    {
        return "{ \"ok\": false, \"tool\": \"cookbook_acceptance_start\", \"displayTool\": \"cookbook.acceptance_start\", \"error\": "
            + jsonQuote("Could not create acceptance run directory: " + runDir.getFullPathName()) + " }";
    }

    const auto jsonReport = runDir.getChildFile("acceptance_report.json");
    const auto markdownReport = runDir.getChildFile("acceptance_report.md");

    auto appendJsonArray = [](juce::String& out, const juce::var& value) {
        const auto text = juce::JSON::toString(value, true);
        out << (text.isNotEmpty() ? text : "[]");
    };

    auto markdownChecklist = [](const juce::var& value) {
        juce::String text;
        if (auto* array = value.getArray())
        {
            for (const auto& item : *array)
                text << "- [ ] " << item.toString() << "\n";
        }
        return text;
    };

    juce::String json;
    json << "{\n";
    json << "  \"schemaVersion\": 1,\n";
    json << "  \"kind\": \"djehuti_acceptance_run_report\",\n";
    json << "  \"status\": \"not_started\",\n";
    json << "  \"goal\": ";
    appendJsonArray(json, selectedGoal);
    json << ",\n";
    json << "  \"evidence\": {\n";
    json << "    \"retrievedCards\": [],\n";
    json << "    \"toolCalls\": [],\n";
    json << "    \"artifacts\": [],\n";
    json << "    \"capabilityGaps\": [],\n";
    json << "    \"notes\": []\n";
    json << "  },\n";
    json << "  \"criteriaResults\": [],\n";
    json << "  \"finalDetermination\": \"unverified\"\n";
    json << "}\n";

    juce::String markdown;
    markdown << "# Acceptance Run: " << selectedGoal.getProperty("id", goalId).toString() << "\n\n";
    markdown << "- Domain: `" << selectedGoal.getProperty("domain", {}).toString() << "`\n";
    markdown << "- Status: `not_started`\n";
    markdown << "- Determination: `unverified`\n\n";
    markdown << "## Prompt\n\n" << selectedGoal.getProperty("prompt", {}).toString() << "\n\n";
    markdown << "## Required Cookbook Retrieval\n\n" << markdownChecklist(selectedGoal.getProperty("mustRetrieve", {})) << "\n";
    markdown << "## Required Tool Evidence\n\n" << markdownChecklist(selectedGoal.getProperty("requiredToolEvidence", {})) << "\n";
    markdown << "## Pass Criteria\n\n" << markdownChecklist(selectedGoal.getProperty("passCriteria", {})) << "\n";
    markdown << "## Expected Capability Gaps\n\n" << markdownChecklist(selectedGoal.getProperty("expectedCapabilityGaps", {})) << "\n";
    markdown << "## Evidence Log\n\n- [ ] Record cookbook cards retrieved.\n- [ ] Record tool calls and result paths.\n- [ ] Record generated plots/data and whether they came from real tool output.\n- [ ] Record capability gaps instead of filling missing steps manually.\n";

    if (!jsonReport.replaceWithText(json))
    {
        return "{ \"ok\": false, \"tool\": \"cookbook_acceptance_start\", \"displayTool\": \"cookbook.acceptance_start\", \"error\": "
            + jsonQuote("Could not write acceptance JSON report: " + jsonReport.getFullPathName()) + " }";
    }

    if (!markdownReport.replaceWithText(markdown))
    {
        return "{ \"ok\": false, \"tool\": \"cookbook_acceptance_start\", \"displayTool\": \"cookbook.acceptance_start\", \"error\": "
            + jsonQuote("Could not write acceptance markdown report: " + markdownReport.getFullPathName()) + " }";
    }

    juce::String result;
    result << "{\n";
    result << "  \"ok\": true,\n";
    result << "  \"schemaVersion\": 1,\n";
    result << "  \"kind\": \"djehuti_assistant_tool_result\",\n";
    result << "  \"tool\": \"cookbook_acceptance_start\",\n";
    result << "  \"displayTool\": \"cookbook.acceptance_start\",\n";
    result << "  \"status\": \"started\",\n";
    result << "  \"goalId\": " << jsonQuote(goalId) << ",\n";
    result << "  \"artifactDirectory\": " << jsonQuote(runDir.getFullPathName()) << ",\n";
    result << "  \"jsonReport\": " << jsonQuote(jsonReport.getFullPathName()) << ",\n";
    result << "  \"markdownReport\": " << jsonQuote(markdownReport.getFullPathName()) << "\n";
    result << "}\n";
    return result;
}

juce::String ElectronicsWorkbench::cookbookAcceptanceRecordTool(const juce::String& reportPath,
                                                                const juce::String& evidenceType,
                                                                const juce::String& label,
                                                                const juce::String& detail,
                                                                const juce::String& pathOrValue,
                                                                const juce::String& status) const
{
    const auto report = juce::File(reportPath);
    if (!report.existsAsFile())
    {
        return "{ \"ok\": false, \"tool\": \"cookbook_acceptance_record\", \"displayTool\": \"cookbook.acceptance_record\", \"error\": "
            + jsonQuote("Acceptance report does not exist: " + reportPath) + " }";
    }

    if (!report.getFileName().equalsIgnoreCase("acceptance_report.json"))
    {
        return "{ \"ok\": false, \"tool\": \"cookbook_acceptance_record\", \"displayTool\": \"cookbook.acceptance_record\", \"error\": "
            + jsonQuote("Expected an acceptance_report.json path, got: " + report.getFileName()) + " }";
    }

    auto parsed = juce::JSON::parse(report.loadFileAsString());
    auto* root = parsed.getDynamicObject();
    if (root == nullptr)
    {
        return "{ \"ok\": false, \"tool\": \"cookbook_acceptance_record\", \"displayTool\": \"cookbook.acceptance_record\", \"error\": "
            + jsonQuote("Could not parse acceptance report: " + report.getFullPathName()) + " }";
    }

    auto normalizedType = evidenceType.trim().replaceCharacter('.', '_').replaceCharacter('-', '_').toLowerCase();
    if (normalizedType.isEmpty())
        normalizedType = "note";

    auto normalizedStatus = status.trim().toLowerCase();
    if (normalizedStatus.isEmpty())
        normalizedStatus = normalizedType == "capability_gap" ? "gap" : "observed";

    auto* item = new juce::DynamicObject();
    item->setProperty("type", normalizedType);
    item->setProperty("label", label.trim());
    item->setProperty("detail", detail.trim());
    item->setProperty("pathOrValue", pathOrValue.trim());
    item->setProperty("status", normalizedStatus);

    auto appendToArrayProperty = [](juce::DynamicObject* object, const juce::Identifier& property, const juce::var& value) {
        juce::Array<juce::var> array;
        if (auto* existing = object->getProperty(property).getArray())
            array = *existing;
        array.add(value);
        object->setProperty(property, juce::var(array));
    };

    auto evidence = root->getProperty("evidence");
    if (!evidence.isObject())
    {
        auto* evidenceObject = new juce::DynamicObject();
        evidenceObject->setProperty("retrievedCards", juce::Array<juce::var>());
        evidenceObject->setProperty("toolCalls", juce::Array<juce::var>());
        evidenceObject->setProperty("artifacts", juce::Array<juce::var>());
        evidenceObject->setProperty("capabilityGaps", juce::Array<juce::var>());
        evidenceObject->setProperty("notes", juce::Array<juce::var>());
        root->setProperty("evidence", juce::var(evidenceObject));
        evidence = root->getProperty("evidence");
    }

    auto* evidenceObject = evidence.getDynamicObject();
    juce::String targetArray = "notes";
    if (normalizedType == "retrieved_card" || normalizedType == "retrieved_cards" || normalizedType == "card")
        targetArray = "retrievedCards";
    else if (normalizedType == "tool_call" || normalizedType == "tool_calls" || normalizedType == "tool")
        targetArray = "toolCalls";
    else if (normalizedType == "artifact" || normalizedType == "artifacts")
        targetArray = "artifacts";
    else if (normalizedType == "capability_gap" || normalizedType == "gap")
        targetArray = "capabilityGaps";
    else if (normalizedType == "criterion" || normalizedType == "criteria" || normalizedType == "criteria_result")
        targetArray = "criteriaResults";

    if (targetArray == "criteriaResults")
        appendToArrayProperty(root, "criteriaResults", juce::var(item));
    else if (evidenceObject != nullptr)
        appendToArrayProperty(evidenceObject, targetArray, juce::var(item));

    if (root->getProperty("status").toString().isEmpty()
        || root->getProperty("status").toString().equalsIgnoreCase("not_started"))
        root->setProperty("status", "in_progress");

    if (normalizedType == "capability_gap" || normalizedStatus == "gap")
        root->setProperty("finalDetermination", "capability_gap");
    else if (normalizedStatus == "failed")
        root->setProperty("finalDetermination", "failed");

    if (!report.replaceWithText(juce::JSON::toString(parsed, true)))
    {
        return "{ \"ok\": false, \"tool\": \"cookbook_acceptance_record\", \"displayTool\": \"cookbook.acceptance_record\", \"error\": "
            + jsonQuote("Could not update acceptance report: " + report.getFullPathName()) + " }";
    }

    const auto markdownReport = report.getSiblingFile("acceptance_report.md");
    bool markdownUpdated = false;
    if (markdownReport.existsAsFile())
    {
        juce::String line;
        line << "\n- [`" << normalizedStatus << "`] `" << normalizedType << "`: " << label.trim();
        if (detail.trim().isNotEmpty())
            line << " - " << detail.trim();
        if (pathOrValue.trim().isNotEmpty())
            line << " (" << pathOrValue.trim() << ")";
        line << "\n";
        markdownUpdated = markdownReport.appendText(line);
    }

    juce::String result;
    result << "{\n";
    result << "  \"ok\": true,\n";
    result << "  \"schemaVersion\": 1,\n";
    result << "  \"kind\": \"djehuti_assistant_tool_result\",\n";
    result << "  \"tool\": \"cookbook_acceptance_record\",\n";
    result << "  \"displayTool\": \"cookbook.acceptance_record\",\n";
    result << "  \"reportPath\": " << jsonQuote(report.getFullPathName()) << ",\n";
    result << "  \"markdownReport\": " << jsonQuote(markdownReport.getFullPathName()) << ",\n";
    result << "  \"markdownUpdated\": " << (markdownUpdated ? "true" : "false") << ",\n";
    result << "  \"evidenceType\": " << jsonQuote(normalizedType) << ",\n";
    result << "  \"targetArray\": " << jsonQuote(targetArray) << ",\n";
    result << "  \"status\": " << jsonQuote(normalizedStatus) << "\n";
    result << "}\n";
    return result;
}

juce::String ElectronicsWorkbench::capabilityGapRecordTool(const juce::String& category,
                                                           const juce::String& description,
                                                           const juce::String& neededCapability,
                                                           const juce::String& evidence,
                                                           const juce::String& source,
                                                           const juce::String& status) const
{
    const auto trimmedDescription = description.trim();
    const auto trimmedCapability = neededCapability.trim();
    if (trimmedDescription.isEmpty() || trimmedCapability.isEmpty())
    {
        return "{ \"ok\": false, \"tool\": \"capability_gap_record\", \"displayTool\": \"capability_gap.record\", \"error\": \"description and neededCapability are required.\" }";
    }

    const auto memoryDir = savedProjectFile().getParentDirectory().getChildFile(".djehuti");
    if (!memoryDir.createDirectory())
    {
        return "{ \"ok\": false, \"tool\": \"capability_gap_record\", \"displayTool\": \"capability_gap.record\", \"error\": "
            + jsonQuote("Could not create project memory directory: " + memoryDir.getFullPathName()) + " }";
    }

    const auto registry = memoryDir.getChildFile("CAPABILITY_GAPS.jsonl");
    auto normalizedCategory = category.trim().replaceCharacter(' ', '_').replaceCharacter('-', '_').toLowerCase();
    if (normalizedCategory.isEmpty())
        normalizedCategory = "unspecified";

    auto normalizedStatus = status.trim().replaceCharacter(' ', '_').replaceCharacter('-', '_').toLowerCase();
    if (normalizedStatus.isEmpty())
        normalizedStatus = "open";

    const auto now = juce::Time::getCurrentTime().toISO8601(true);
    const auto seed = trimmedDescription + "|" + trimmedCapability + "|" + source.trim();
    const auto id = "gap."
        + juce::String::toHexString((int)std::abs((int)seed.hashCode())).paddedLeft('0', 8)
        + "."
        + juce::String(juce::Time::getCurrentTime().toMilliseconds());

    auto* gap = new juce::DynamicObject();
    gap->setProperty("schemaVersion", 1);
    gap->setProperty("kind", "djehuti_capability_gap");
    gap->setProperty("id", id);
    gap->setProperty("createdAt", now);
    gap->setProperty("status", normalizedStatus);
    gap->setProperty("category", normalizedCategory);
    gap->setProperty("description", trimmedDescription);
    gap->setProperty("neededCapability", trimmedCapability);
    gap->setProperty("evidence", evidence.trim());
    gap->setProperty("source", source.trim());

    const auto line = juce::JSON::toString(juce::var(gap), false).trim() + "\n";
    if (!registry.appendText(line))
    {
        return "{ \"ok\": false, \"tool\": \"capability_gap_record\", \"displayTool\": \"capability_gap.record\", \"error\": "
            + jsonQuote("Could not append capability gap registry: " + registry.getFullPathName()) + " }";
    }

    juce::String result;
    result << "{\n";
    result << "  \"ok\": true,\n";
    result << "  \"schemaVersion\": 1,\n";
    result << "  \"kind\": \"djehuti_assistant_tool_result\",\n";
    result << "  \"tool\": \"capability_gap_record\",\n";
    result << "  \"displayTool\": \"capability_gap.record\",\n";
    result << "  \"gapId\": " << jsonQuote(id) << ",\n";
    result << "  \"status\": " << jsonQuote(normalizedStatus) << ",\n";
    result << "  \"category\": " << jsonQuote(normalizedCategory) << ",\n";
    result << "  \"gapRegistry\": " << jsonQuote(registry.getFullPathName()) << "\n";
    result << "}\n";
    return result;
}

void ElectronicsWorkbench::exportAssistantToolManifest()
{
    const auto runDir = generatedRunDirectory();
    if (!runDir.createDirectory())
    {
        appendLog("Could not create run directory: " + runDir.getFullPathName());
        return;
    }

    const auto file = runDir.getChildFile("assistant_tools.json");
    if (!file.replaceWithText(buildAssistantToolManifestJson()))
    {
        appendLog("Could not write assistant tool manifest: " + file.getFullPathName());
        return;
    }

    appendLog("Exported assistant tool manifest to " + file.getFullPathName());
}

juce::String ElectronicsWorkbench::runElectricalRuleCheckTool()
{
    if (getErcReport == nullptr)
    {
        return "{ \"ok\": false, \"tool\": \"circuit_run_erc\", \"displayTool\": \"circuit.run_erc\", \"error\": \"No ERC engine is available.\" }";
    }

    const auto runDir = generatedRunDirectory();
    if (!runDir.createDirectory())
    {
        return "{ \"ok\": false, \"tool\": \"circuit_run_erc\", \"displayTool\": \"circuit.run_erc\", \"error\": "
            + jsonQuote("Could not create run directory: " + runDir.getFullPathName()) + " }";
    }

    const auto report = getErcReport();
    const auto reportFile = runDir.getChildFile("erc_report.md");
    if (!reportFile.replaceWithText(report))
    {
        return "{ \"ok\": false, \"tool\": \"circuit_run_erc\", \"displayTool\": \"circuit.run_erc\", \"error\": "
            + jsonQuote("Could not write ERC report: " + reportFile.getFullPathName()) + " }";
    }
    const auto manifestFile = runDir.getChildFile("assistant_tools.json");
    manifestFile.replaceWithText(buildAssistantToolManifestJson());

    const auto errors = report.fromFirstOccurrenceOf("- Errors: ", false, false)
                             .upToFirstOccurrenceOf("\n", false, false)
                             .trim();
    const auto warnings = report.fromFirstOccurrenceOf("- Warnings: ", false, false)
                               .upToFirstOccurrenceOf("\n", false, false)
                               .trim();
    const auto resultFile = runDir.getChildFile("erc_tool_result.json");
    juce::String result;
    result << "{\n";
    result << "  \"ok\": true,\n";
    result << "  \"schemaVersion\": 1,\n";
    result << "  \"kind\": \"djehuti_assistant_tool_result\",\n";
    result << "  \"tool\": \"circuit_run_erc\",\n";
    result << "  \"displayTool\": \"circuit.run_erc\",\n";
    result << "  \"status\": " << jsonQuote(errors == "0" ? "passed" : "failed") << ",\n";
    result << "  \"errors\": " << errors << ",\n";
    result << "  \"warnings\": " << warnings << ",\n";
    result << "  \"reportPath\": " << jsonQuote(reportFile.getFullPathName()) << "\n";
    result << "}\n";
    resultFile.replaceWithText(result);
    return result;
}

void ElectronicsWorkbench::runElectricalRuleCheck()
{
    const auto result = runElectricalRuleCheckTool();
    const auto parsed = juce::JSON::parse(result);
    if (!parsed.isObject() || !(bool)parsed.getProperty("ok", false))
    {
        appendLog("ERC failed: " + parsed.getProperty("error", result).toString());
        return;
    }

    appendLog("ERC complete: "
              + parsed.getProperty("errors", 0).toString()
              + " error(s), "
              + parsed.getProperty("warnings", 0).toString()
              + " warning(s). Report: "
              + parsed.getProperty("reportPath", {}).toString());
}

void ElectronicsWorkbench::openInstrumentWindow(juce::String refdes, juce::String symbolId)
{
    const auto instrumentName = symbolId == "oscilloscope_2ch" ? juce::String("Oscilloscope")
                              : symbolId == "digital_multimeter" ? juce::String("Digital Multimeter")
                              : juce::String("Instrument");
    auto* window = new FloatingInstrumentWindow(refdes + " " + instrumentName);
    window->setContentOwned(new InstrumentPanel(), true);
    window->centreWithSize(920, 680);
    window->setVisible(true);
    window->toFront(true);
    floatingInstrumentWindows.add(window);
    appendLog("Opened floating " + instrumentName + " panel for " + refdes + ".");
}

juce::String ElectronicsWorkbench::exportCircuitArtifactsTool()
{
    if (getCircuitJson == nullptr || getXyceNetlist == nullptr || getLabInstrumentsJson == nullptr)
    {
        return "{ \"ok\": false, \"tool\": \"simulation_export_artifacts\", \"displayTool\": \"simulation.export_artifacts\", \"error\": \"No schematic exporter is available.\" }";
    }

    const auto runDir = generatedRunDirectory();
    if (!runDir.createDirectory())
    {
        return "{ \"ok\": false, \"tool\": \"simulation_export_artifacts\", \"displayTool\": \"simulation.export_artifacts\", \"error\": "
            + jsonQuote("Could not create run directory: " + runDir.getFullPathName()) + " }";
    }

    const auto circuitFile = runDir.getChildFile("circuit.json");
    const auto netlistFile = runDir.getChildFile("generated.cir");
    const auto instrumentsFile = runDir.getChildFile("lab_instruments.json");
    const auto circuitJson = getCircuitJson();
    const auto netlist = getXyceNetlist();
    const auto instrumentsJson = getLabInstrumentsJson();

    if (!circuitFile.replaceWithText(circuitJson))
    {
        return "{ \"ok\": false, \"tool\": \"simulation_export_artifacts\", \"displayTool\": \"simulation.export_artifacts\", \"error\": "
            + jsonQuote("Could not write circuit JSON: " + circuitFile.getFullPathName()) + " }";
    }
    if (!netlistFile.replaceWithText(netlist))
    {
        return "{ \"ok\": false, \"tool\": \"simulation_export_artifacts\", \"displayTool\": \"simulation.export_artifacts\", \"error\": "
            + jsonQuote("Could not write Xyce netlist: " + netlistFile.getFullPathName()) + " }";
    }
    if (!instrumentsFile.replaceWithText(instrumentsJson))
    {
        return "{ \"ok\": false, \"tool\": \"simulation_export_artifacts\", \"displayTool\": \"simulation.export_artifacts\", \"error\": "
            + jsonQuote("Could not write lab instruments JSON: " + instrumentsFile.getFullPathName()) + " }";
    }
    const auto manifestFile = runDir.getChildFile("assistant_tools.json");
    manifestFile.replaceWithText(buildAssistantToolManifestJson());

    juce::String result;
    result << "{\n";
    result << "  \"ok\": true,\n";
    result << "  \"schemaVersion\": 1,\n";
    result << "  \"kind\": \"djehuti_assistant_tool_result\",\n";
    result << "  \"tool\": \"simulation_export_artifacts\",\n";
    result << "  \"displayTool\": \"simulation.export_artifacts\",\n";
    result << "  \"status\": \"exported\",\n";
    result << "  \"artifactDirectory\": " << jsonQuote(runDir.getFullPathName()) << ",\n";
    result << "  \"circuitJson\": " << jsonQuote(circuitFile.getFullPathName()) << ",\n";
    result << "  \"xyceNetlist\": " << jsonQuote(netlistFile.getFullPathName()) << ",\n";
    result << "  \"instrumentJson\": " << jsonQuote(instrumentsFile.getFullPathName()) << ",\n";
    result << "  \"toolManifest\": " << jsonQuote(manifestFile.getFullPathName()) << "\n";
    result << "}\n";
    return result;
}

juce::String ElectronicsWorkbench::writeAgentMarkdownTool(const juce::String& title, const juce::String& markdown)
{
    const auto runDir = generatedRunDirectory().getChildFile("agent_reports");
    if (!runDir.createDirectory())
    {
        return "{ \"ok\": false, \"tool\": \"agent_write_markdown\", \"displayTool\": \"agent.write_markdown\", \"error\": "
            + jsonQuote("Could not create report directory: " + runDir.getFullPathName()) + " }";
    }

    auto safeName = title.retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_").trim();
    if (safeName.isEmpty())
        safeName = "agent-report";
    safeName = safeName.replace(" ", "_").substring(0, 64);

    const auto stamp = juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S");
    const auto markdownFile = runDir.getChildFile(safeName + "_" + stamp + ".md");
    const auto htmlFile = runDir.getChildFile(safeName + "_" + stamp + ".html");
    const auto html = markdownToHtmlDocument(title, markdown);

    if (!markdownFile.replaceWithText(markdown))
    {
        return "{ \"ok\": false, \"tool\": \"agent_write_markdown\", \"displayTool\": \"agent.write_markdown\", \"error\": "
            + jsonQuote("Could not write markdown: " + markdownFile.getFullPathName()) + " }";
    }
    if (!htmlFile.replaceWithText(html))
    {
        return "{ \"ok\": false, \"tool\": \"agent_write_markdown\", \"displayTool\": \"agent.write_markdown\", \"error\": "
            + jsonQuote("Could not write rendered HTML: " + htmlFile.getFullPathName()) + " }";
    }

    juce::String result;
    result << "{\n";
    result << "  \"ok\": true,\n";
    result << "  \"schemaVersion\": 1,\n";
    result << "  \"kind\": \"djehuti_assistant_tool_result\",\n";
    result << "  \"tool\": \"agent_write_markdown\",\n";
    result << "  \"displayTool\": \"agent.write_markdown\",\n";
    result << "  \"status\": \"written_and_rendered\",\n";
    result << "  \"markdownPath\": " << jsonQuote(markdownFile.getFullPathName()) << ",\n";
    result << "  \"htmlPath\": " << jsonQuote(htmlFile.getFullPathName()) << ",\n";
    result << "  \"renderer\": \"markdown_html_katex\"\n";
    result << "}\n";
    return result;
}

juce::String ElectronicsWorkbench::researchWebSearchTool(const juce::String& query, int maxResults) const
{
    const auto limit = juce::jlimit(1, 8, maxResults <= 0 ? 5 : maxResults);
    const auto instantUrl = juce::URL("https://api.duckduckgo.com/?q="
        + juce::URL::addEscapeChars(query, true)
        + "&format=json&no_html=1&skip_disambig=1");

    const auto response = instantUrl.readEntireTextStream(false);
    if (response.trim().isEmpty())
    {
        return "{ \"ok\": false, \"tool\": \"research_web_search\", \"displayTool\": \"research.web_search\", \"error\": "
            + jsonQuote("No response for query: " + query) + " }";
    }

    const auto parsed = juce::JSON::parse(response);
    if (!parsed.isObject())
    {
        return "{ \"ok\": false, \"tool\": \"research_web_search\", \"displayTool\": \"research.web_search\", \"error\": \"Search response was not JSON.\" }";
    }

    juce::StringArray rows;
    const auto abstractText = parsed.getProperty("AbstractText", {}).toString().trim();
    const auto abstractUrl = parsed.getProperty("AbstractURL", {}).toString().trim();
    const auto heading = parsed.getProperty("Heading", {}).toString().trim();
    if (abstractText.isNotEmpty())
    {
        juce::String row;
        row << "{ \"title\": " << jsonQuote(heading.isNotEmpty() ? heading : query)
            << ", \"snippet\": " << jsonQuote(abstractText)
            << ", \"url\": " << jsonQuote(abstractUrl) << " }";
        rows.add(row);
    }

    std::function<void(const juce::var&)> collect;
    collect = [&](const juce::var& item) {
        if (rows.size() >= limit)
            return;
        if (const auto* object = item.getDynamicObject())
        {
            if (object->hasProperty("Topics"))
            {
                if (const auto* nested = object->getProperty("Topics").getArray())
                    for (const auto& child : *nested)
                        collect(child);
                return;
            }

            const auto text = object->getProperty("Text").toString().trim();
            const auto firstUrl = object->getProperty("FirstURL").toString().trim();
            if (text.isNotEmpty())
            {
                juce::String row;
                row << "{ \"title\": " << jsonQuote(text.upToFirstOccurrenceOf(" - ", false, false))
                    << ", \"snippet\": " << jsonQuote(text)
                    << ", \"url\": " << jsonQuote(firstUrl) << " }";
                rows.add(row);
            }
        }
    };

    if (const auto* topics = parsed.getProperty("RelatedTopics", {}).getArray())
        for (const auto& item : *topics)
            collect(item);

    juce::String source = "DuckDuckGo Instant Answer API";
    if (rows.isEmpty())
    {
        const auto htmlUrl = juce::URL("https://html.duckduckgo.com/html/?q="
            + juce::URL::addEscapeChars(query, true));
        const auto html = htmlUrl.readEntireTextStream(false);
        collectDuckDuckGoHtmlResults(html, limit, rows);
        if (!rows.isEmpty())
            source = "DuckDuckGo HTML search";
    }

    juce::String result;
    result << "{\n";
    result << "  \"ok\": true,\n";
    result << "  \"schemaVersion\": 1,\n";
    result << "  \"kind\": \"djehuti_assistant_tool_result\",\n";
    result << "  \"tool\": \"research_web_search\",\n";
    result << "  \"displayTool\": \"research.web_search\",\n";
    result << "  \"query\": " << jsonQuote(query) << ",\n";
    result << "  \"source\": " << jsonQuote(source) << ",\n";
    result << "  \"results\": [";
    for (int i = 0; i < rows.size(); ++i)
    {
        if (i != 0) result << ", ";
        result << rows[i];
    }
    result << "]\n";
    result << "}\n";
    return result;
}

juce::String ElectronicsWorkbench::designRlcHighPassFilterTool(double cutoffHz, double impedanceOhms)
{
    if (getCircuitJson == nullptr)
    {
        return "{ \"ok\": false, \"tool\": \"filter_design_high_pass\", \"displayTool\": \"filter.design_high_pass\", \"error\": \"No schematic model is available.\" }";
    }

    const auto design = makeRlcHighPassDesign(cutoffHz, impedanceOhms);
    const auto samples = sweepRlcHighPass(design);
    const auto cutoffSample = sampleRlcHighPassAt(design, design.cutoffHz);
    const auto runDir = generatedRunDirectory();
    if (!runDir.createDirectory())
    {
        return "{ \"ok\": false, \"tool\": \"filter_design_high_pass\", \"displayTool\": \"filter.design_high_pass\", \"error\": "
            + jsonQuote("Could not create run directory: " + runDir.getFullPathName()) + " }";
    }

    const auto circuitFile = runDir.getChildFile("rlc_high_pass_circuit.json");
    const auto netlistFile = runDir.getChildFile("rlc_high_pass_ac.cir");
    const auto csvFile = runDir.getChildFile("rlc_high_pass_ac.csv");
    const auto svgFile = runDir.getChildFile("rlc_high_pass_response.svg");
    const auto reportFile = runDir.getChildFile("rlc_high_pass_report.md");
    const auto manifestFile = runDir.getChildFile("assistant_tools.json");

    juce::String report;
    report << "# RLC 2nd Order High-Pass Filter\n\n";
    report << "- Topology: 8 ohm source, series capacitor, shunt inductor, 8 ohm load.\n";
    report << "- Alignment: 2nd order Butterworth high-pass, normalized to matched passband.\n";
    report << "- Cutoff: " << numberText(design.cutoffHz, 4) << " Hz.\n";
    report << "- Matched impedance: " << numberText(design.impedanceOhms, 4) << " ohm.\n";
    report << "- Source resistor: " << numberText(design.sourceOhms, 4) << " ohm.\n";
    report << "- Load resistor: " << numberText(design.loadOhms, 4) << " ohm.\n";
    report << "- Series capacitor C1: " << humanCapacitance(design.capacitanceFarads)
           << " (`" << spiceCapacitance(design.capacitanceFarads) << "` F in SPICE suffix notation).\n";
    report << "- Shunt inductor L1: " << humanInductance(design.inductanceHenries)
           << " (`" << spiceInductance(design.inductanceHenries) << "` H in SPICE suffix notation).\n\n";
    report << "## AC Sweep Result\n\n";
    report << "- Gain at cutoff, normalized to matched passband: "
           << numberText(cutoffSample.normalizedGainDb, 4) << " dB.\n";
    report << "- Raw V(out)/V(source) at cutoff: " << numberText(cutoffSample.rawGainDb, 4)
           << " dB. The extra ~6 dB loss is the intentional 8 ohm source/load division.\n";
    report << "- Input impedance magnitude at cutoff: " << numberText(cutoffSample.inputImpedanceMag, 4)
           << " ohm.\n";
    report << "- Input impedance at cutoff: " << numberText(cutoffSample.inputImpedanceReal, 4)
           << " + j" << numberText(cutoffSample.inputImpedanceImag, 4) << " ohm.\n\n";
    report << "## Artifacts\n\n";
    report << "- SPICE AC netlist: `" << netlistFile.getFullPathName() << "`\n";
    report << "- Sweep CSV: `" << csvFile.getFullPathName() << "`\n";
    report << "- Frequency response SVG: `" << svgFile.getFullPathName() << "`\n";
    report << "- Schematic circuit JSON: `" << circuitFile.getFullPathName() << "`\n";

    struct OutputFile
    {
        juce::File file;
        juce::String text;
        juce::String label;
    };

    const OutputFile files[] = {
        { circuitFile, getCircuitJson(), "circuit JSON" },
        { netlistFile, buildRlcHighPassSpiceNetlist(design), "SPICE netlist" },
        { csvFile, buildRlcHighPassCsv(samples), "AC sweep CSV" },
        { svgFile, buildRlcHighPassSvg(design, samples), "frequency response SVG" },
        { reportFile, report, "analysis report" },
        { manifestFile, buildAssistantToolManifestJson(), "assistant tool manifest" }
    };

    for (const auto& output : files)
    {
        if (!output.file.replaceWithText(output.text))
        {
            return "{ \"ok\": false, \"tool\": \"filter_design_high_pass\", \"displayTool\": \"filter.design_high_pass\", \"error\": "
                + jsonQuote("Could not write " + output.label + ": " + output.file.getFullPathName()) + " }";
        }
    }

    if (showFrequencyResponse != nullptr)
        showFrequencyResponse(csvFile, reportFile, design.cutoffHz, design.impedanceOhms,
                              design.capacitanceFarads, design.inductanceHenries);

    juce::String result;
    result << "{\n";
    result << "  \"ok\": true,\n";
    result << "  \"schemaVersion\": 1,\n";
    result << "  \"kind\": \"djehuti_assistant_tool_result\",\n";
    result << "  \"tool\": \"filter_design_high_pass\",\n";
    result << "  \"displayTool\": \"filter.design_high_pass\",\n";
    result << "  \"status\": \"designed_and_analyzed\",\n";
    result << "  \"analysisEngine\": \"internal_linear_ac_sweep\",\n";
    result << "  \"cutoffHz\": " << numberText(design.cutoffHz, 6) << ",\n";
    result << "  \"impedanceOhms\": " << numberText(design.impedanceOhms, 6) << ",\n";
    result << "  \"capacitanceFarads\": " << numberText(design.capacitanceFarads, 12) << ",\n";
    result << "  \"inductanceHenries\": " << numberText(design.inductanceHenries, 12) << ",\n";
    result << "  \"capacitanceLabel\": " << jsonQuote(humanCapacitance(design.capacitanceFarads)) << ",\n";
    result << "  \"inductanceLabel\": " << jsonQuote(humanInductance(design.inductanceHenries)) << ",\n";
    result << "  \"cutoffGainDb\": " << numberText(cutoffSample.normalizedGainDb, 8) << ",\n";
    result << "  \"cutoffInputImpedanceOhms\": " << numberText(cutoffSample.inputImpedanceMag, 8) << ",\n";
    result << "  \"artifactDirectory\": " << jsonQuote(runDir.getFullPathName()) << ",\n";
    result << "  \"circuitJson\": " << jsonQuote(circuitFile.getFullPathName()) << ",\n";
    result << "  \"spiceNetlist\": " << jsonQuote(netlistFile.getFullPathName()) << ",\n";
    result << "  \"csv\": " << jsonQuote(csvFile.getFullPathName()) << ",\n";
    result << "  \"responseSvg\": " << jsonQuote(svgFile.getFullPathName()) << ",\n";
    result << "  \"report\": " << jsonQuote(reportFile.getFullPathName()) << "\n";
    result << "}\n";
    return result;
}

juce::String ElectronicsWorkbench::designPushPullAmplifierTool()
{
    if (getCircuitJson == nullptr || getXyceNetlist == nullptr || getErcReport == nullptr)
        return "{ \"ok\": false, \"tool\": \"amplifier_design_push_pull\", \"displayTool\": \"amplifier.design_push_pull\", \"error\": \"No schematic model is available.\" }";

    const auto runDir = generatedRunDirectory();
    if (!runDir.createDirectory())
    {
        return "{ \"ok\": false, \"tool\": \"amplifier_design_push_pull\", \"displayTool\": \"amplifier.design_push_pull\", \"error\": "
            + jsonQuote("Could not create run directory: " + runDir.getFullPathName()) + " }";
    }

    const auto circuitFile = runDir.getChildFile("push_pull_amplifier_circuit.json");
    const auto netlistFile = runDir.getChildFile("push_pull_amplifier_preview.cir");
    const auto ercFile = runDir.getChildFile("push_pull_amplifier_erc.md");
    const auto reportFile = runDir.getChildFile("push_pull_amplifier_report.md");
    const auto manifestFile = runDir.getChildFile("assistant_tools.json");

    const auto ercReport = getErcReport();
    const auto errors = ercReport.fromFirstOccurrenceOf("- Errors: ", false, false)
                            .upToFirstOccurrenceOf("\n", false, false).getIntValue();
    const auto warnings = ercReport.fromFirstOccurrenceOf("- Warnings: ", false, false)
                              .upToFirstOccurrenceOf("\n", false, false).getIntValue();

    juce::String report;
    report << "# Push-Pull Audio Output Stage\n\n";
    report << "- Topology: diode-biased class AB complementary emitter follower.\n";
    report << "- Supplies: +12V and -12V named rails from two stacked 12 V sources around ground.\n";
    report << "- Input: 0.25 V AC source at 1 kHz, coupled through 10u C1 into the middle of the bias string.\n";
    report << "- Bias: R1 2.2k from +12V and R2 2.2k to -12V feed two 1N4148 diodes; the diode drops hold\n";
    report << "  the NPN base about 0.6 V above and the PNP base about 0.6 V below the drive node.\n";
    report << "- Output pair: generic NPN (collector to +12V) and PNP (collector to -12V) with 0.47 ohm\n";
    report << "  emitter ballast resistors R3/R4 into the output node.\n";
    report << "- Load: 8 ohm resistor from output node to ground.\n";
    report << "- Instrumentation: 2-channel oscilloscope on input and output.\n\n";
    report << "## Verification\n\n";
    report << "- ERC errors: " << errors << "\n";
    report << "- ERC warnings: " << warnings << "\n";
    report << "- Note: transistor devices are represented structurally; full BJT SPICE model lowering is still a planned solver capability.\n\n";
    report << "## Artifacts\n\n";
    report << "- Circuit JSON: `" << circuitFile.getFullPathName() << "`\n";
    report << "- Preview netlist: `" << netlistFile.getFullPathName() << "`\n";
    report << "- ERC report: `" << ercFile.getFullPathName() << "`\n";

    struct OutputFile
    {
        juce::File file;
        juce::String text;
        juce::String label;
    };

    const OutputFile files[] = {
        { circuitFile, getCircuitJson(), "circuit JSON" },
        { netlistFile, getXyceNetlist(), "preview netlist" },
        { ercFile, ercReport, "ERC report" },
        { reportFile, report, "analysis report" },
        { manifestFile, buildAssistantToolManifestJson(), "assistant tool manifest" }
    };

    for (const auto& output : files)
    {
        if (!output.file.replaceWithText(output.text))
        {
            return "{ \"ok\": false, \"tool\": \"amplifier_design_push_pull\", \"displayTool\": \"amplifier.design_push_pull\", \"error\": "
                + jsonQuote("Could not write " + output.label + ": " + output.file.getFullPathName()) + " }";
        }
    }

    juce::String result;
    result << "{\n";
    result << "  \"ok\": true,\n";
    result << "  \"schemaVersion\": 1,\n";
    result << "  \"kind\": \"djehuti_assistant_tool_result\",\n";
    result << "  \"tool\": \"amplifier_design_push_pull\",\n";
    result << "  \"displayTool\": \"amplifier.design_push_pull\",\n";
    result << "  \"status\": \"designed_and_exported\",\n";
    result << "  \"topology\": \"class_ab_complementary_emitter_follower\",\n";
    result << "  \"loadOhms\": 8,\n";
    result << "  \"ercErrors\": " << errors << ",\n";
    result << "  \"ercWarnings\": " << warnings << ",\n";
    result << "  \"artifactDirectory\": " << jsonQuote(runDir.getFullPathName()) << ",\n";
    result << "  \"circuitJson\": " << jsonQuote(circuitFile.getFullPathName()) << ",\n";
    result << "  \"previewNetlist\": " << jsonQuote(netlistFile.getFullPathName()) << ",\n";
    result << "  \"ercReport\": " << jsonQuote(ercFile.getFullPathName()) << ",\n";
    result << "  \"report\": " << jsonQuote(reportFile.getFullPathName()) << "\n";
    result << "}\n";
    return result;
}

void ElectronicsWorkbench::exportCircuitArtifacts()
{
    const auto result = exportCircuitArtifactsTool();
    const auto parsed = juce::JSON::parse(result);
    if (!parsed.isObject() || !(bool)parsed.getProperty("ok", false))
    {
        appendLog("Export failed: " + parsed.getProperty("error", result).toString());
        return;
    }

    appendLog("Exported circuit JSON, Xyce netlist, and lab instruments to "
              + parsed.getProperty("artifactDirectory", generatedRunDirectory().getFullPathName()).toString());
}

void ElectronicsWorkbench::designRlcHighPassFilter()
{
    const auto result = designHighPassTool != nullptr
        ? designHighPassTool(10.0, 8.0)
        : juce::String("{ \"ok\": false, \"error\": \"High-pass filter design tool is unavailable.\" }");
    const auto parsed = juce::JSON::parse(result);
    if (!parsed.isObject() || !(bool)parsed.getProperty("ok", false))
    {
        appendLog("High-pass design failed: " + parsed.getProperty("error", result).toString());
        return;
    }

    appendLog("Designed 10 Hz / 8 ohm RLC high-pass filter. "
              "C1=" + parsed.getProperty("capacitanceLabel", {}).toString()
              + ", L1=" + parsed.getProperty("inductanceLabel", {}).toString()
              + ", cutoff gain=" + parsed.getProperty("cutoffGainDb", {}).toString()
              + " dB. Graph: " + parsed.getProperty("responseSvg", {}).toString());
}

juce::String ElectronicsWorkbench::autoLayoutDiagramTool()
{
    return autoLayoutTool != nullptr
        ? autoLayoutTool()
        : juce::String("{ \"ok\": false, \"tool\": \"schematic_auto_layout\", \"displayTool\": \"schematic.auto_layout\", \"error\": \"Schematic auto-layout is unavailable.\" }");
}

void ElectronicsWorkbench::autoLayoutDiagram()
{
    const auto result = autoLayoutDiagramTool();
    const auto parsed = juce::JSON::parse(result);
    if (!parsed.isObject() || !(bool)parsed.getProperty("ok", false))
    {
        appendLog("Auto layout failed: " + parsed.getProperty("error", result).toString());
        return;
    }

    appendLog("Auto-laid out "
              + parsed.getProperty("componentCount", {}).toString()
              + " schematic component(s).");
}

void ElectronicsWorkbench::showSpecDocument()
{
    const auto spec = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
        .getParentDirectory()
        .getParentDirectory()
        .getParentDirectory()
        .getParentDirectory()
        .getChildFile("ELECTRONICS_DESIGN_TOOL_SPEC.md");
    appendLog("Research spec path: " + spec.getFullPathName());
}

