#include "OscilloscopeView.h"
#include "CircuitSolver.h"
#include <algorithm>
#include <cmath>

namespace
{
juce::Colour colour(int ch) { return ch == 0 ? juce::Colour(0xfff4d35e) : juce::Colour(0xff78dcca); }
juce::String measured(const signal_measure::Result& result, const juce::String& unit)
{ return result.ok ? juce::String(circuit_sim::formatValue(result.value, unit.toStdString(), 5)) : "N/A"; }
}

OscilloscopeView::OscilloscopeView(Callbacks cb) : callbacks(std::move(cb))
{
    controls.setViewedComponent(&controlPanel, false);
    controls.setScrollBarsShown(true, false);
    addAndMakeVisible(controls);
    measurements.setMultiLine(true);
    measurements.setReadOnly(true);
    measurements.setScrollbarsShown(true);
    measurements.setFont(juce::Font("Consolas", 12.0f, juce::Font::plain));
    measurements.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff111a22));
    measurements.setColour(juce::TextEditor::textColourId, juce::Colour(0xffdce9ee));
    addAndMakeVisible(measurements);
    group("ACQUISITION");
    for (const auto& item : std::vector<std::pair<juce::String, juce::String>> {
             {"Run / Acquire", "run"}, {"Stop", "stop"}, {"Single", "single"},
             {"Auto Scale", "autoscale"}, {"Fit Record", "fit"}, {"Export CSV", "export"} })
    {
        auto* b = buttons.add(new juce::TextButton(item.first));
        b->onClick = [this, a = item.second] { action(a); };
        controlPanel.addAndMakeVisible(b);
    }
    field("capture_duration", "Record duration (s)", "Schematic acquisition length. Fixture acquisitions use the stimulus duration.");
    field("capture_step", "Max sample step (s)", "Schematic transient step. Fixture rate comes from its audio renderer. Navigation never resimulates.");
    group("HORIZONTAL / NAVIGATION");
    field("time_per_div", "Time / division (s)", "Engineering notation accepted, e.g. 200u or 2m. Mouse wheel zooms at the pointer.");
    field("horizontal_position", "Pan from trigger (s)", "Drag the plot horizontally to pan through the retained record.");
    for (int ch = 1; ch <= 2; ++ch)
    {
        const auto prefix = "ch" + juce::String(ch) + "_";
        group("CHANNEL " + juce::String(ch) + (ch == 1 ? "  /  YELLOW" : "  /  TEAL"));
        choice(prefix + "enabled", "Trace", { "On", "Off" });
        choice(prefix + "coupling", "Coupling", { "DC", "AC" }, "AC removes the full-record mean. This is ideal digital DC removal, not an analog high-pass model.");
        field(prefix + "volts_per_div", "Volts / division", "No 10 V/div ceiling: use 50, 100, 200, 500 V/div or an engineering value.");
        field(prefix + "offset", "Display offset (V)", "Subtracts volts for display centering only; measured data is preserved.");
        field(prefix + "position", "Vertical position (div)");
        field(prefix + "scale", "Input scaling (x)", "Multiplies the simulated volts for probe-equivalent scaling. Solver node voltages already use volts; default is 1x.");
    }
    group("TRIGGER");
    choice("trigger_source", "Source", { "CH1", "CH2" });
    choice("trigger_mode", "Mode", { "Auto", "Normal", "Single" }, "Auto shows untriggered data. Normal waits for a matching edge. Single freezes one matching simulation record.");
    choice("trigger_slope", "Edge", { "Rising", "Falling" });
    field("trigger_level", "Level (coupled V)");
    field("trigger_position", "Screen position (%)", "Requested pretrigger fraction. Limited by samples actually present in the record.");
    group("MEASUREMENT CURSORS");
    choice("cursors", "Cursors", { "Off", "On" }, "Drag the four cursor lines on the plot, or enter exact coordinates here.");
    choice("cursor_source", "Voltage channel", { "CH1", "CH2" });
    field("cursor_t1", "Time cursor 1 (s)"); field("cursor_t2", "Time cursor 2 (s)");
    field("cursor_v1", "Voltage cursor 1 (V)"); field("cursor_v2", "Voltage cursor 2 (V)");
    choice("markers", "Sample markers", { "Off", "On" });
    group("WAVEFORM MATH");
    choice("math", "Operation", { "Off", "CH1 + CH2", "CH1 - CH2" });
    field("math_scale", "Result gain (x)"); field("math_volts_per_div", "Math volts / div");
    setSize(1280, 820);
    refresh();
    startTimerHz(3);
}

void OscilloscopeView::group(const juce::String& name)
{
    auto* label = labels.add(new juce::Label({}, name));
    label->setFont(juce::Font(12.0f, juce::Font::bold));
    label->setColour(juce::Label::textColourId, juce::Colour(0xff97beca));
    controlPanel.addAndMakeVisible(label);
    rows.push_back({ {}, label, nullptr, 1 });
}
void OscilloscopeView::field(const juce::String& key, const juce::String& text, const juce::String& help)
{
    auto* label = labels.add(new juce::Label({}, text));
    label->setFont(juce::Font(12.0f));
    auto* editor = new juce::TextEditor(); fields.add(editor);
    editor->setFont(juce::Font("Consolas", 13.0f, juce::Font::plain));
    editor->setTooltip(help); label->setTooltip(help);
    auto save = [this, editor, key] { if (!synchronising) commit(key, editor->getText().trim()); };
    editor->onReturnKey = save; editor->onFocusLost = save;
    controlPanel.addAndMakeVisible(label); controlPanel.addAndMakeVisible(editor);
    rows.push_back({ key, label, editor });
}
void OscilloscopeView::choice(const juce::String& key, const juce::String& text, juce::StringArray options, const juce::String& help)
{
    auto* label = labels.add(new juce::Label({}, text));
    label->setFont(juce::Font(12.0f));
    auto* box = new juce::ComboBox(); fields.add(box);
    box->addItemList(options, 1); box->setTooltip(help); label->setTooltip(help);
    box->onChange = [this, box, key] { if (!synchronising) commit(key, box->getText()); };
    controlPanel.addAndMakeVisible(label); controlPanel.addAndMakeVisible(box);
    rows.push_back({ key, label, box });
}
void OscilloscopeView::commit(const juce::String& key, const juce::String& value)
{
    if (callbacks.get && callbacks.get(key) == value) return;
    actionMessage = callbacks.set ? callbacks.set(key, value) : "Instrument has been closed.";
    refresh();
}
void OscilloscopeView::action(const juce::String& name)
{
    if (name == "export")
    {
        fileChooser = std::make_unique<juce::FileChooser>("Export captured scope samples",
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("scope_capture.csv"), "*.csv", true);
        fileChooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
            [safe = juce::Component::SafePointer<OscilloscopeView>(this)](const juce::FileChooser& fc) {
                if (safe != nullptr && fc.getResult() != juce::File())
                { safe->actionMessage = safe->callbacks.action("export", fc.getResult().getFullPathName()); safe->refresh(); }
            });
        return;
    }
    actionMessage = callbacks.action ? callbacks.action(name, {}) : "Instrument has been closed.";
    refresh();
}

juce::String OscilloscopeView::number(double value, const juce::String& unit)
{ return juce::String(circuit_sim::formatValue(value, unit.toStdString(), 5)); }
void OscilloscopeView::refresh()
{
    if (!callbacks.read) return;
    display = callbacks.read();
    lastRevision = callbacks.revision ? callbacks.revision() : juce::String();
    synchronising = true;
    for (auto& row : rows) if (row.control != nullptr)
    {
        const auto value = callbacks.get(row.key);
        if (auto* editor = dynamic_cast<juce::TextEditor*>(row.control))
        { if (!editor->hasKeyboardFocus(false)) editor->setText(value, false); }
        else if (auto* box = dynamic_cast<juce::ComboBox*>(row.control)) box->setText(value, juce::dontSendNotification);
        if (row.key == "capture_duration" || row.key == "capture_step")
            row.control->setEnabled(display.status.find("Fixture") == std::string::npos);
    }
    synchronising = false;
    juce::String text;
    for (size_t ch = 0; ch < 2; ++ch)
    {
        text << "CH" << (int)(ch + 1) << (display.settings.channel[ch].enabled ? "  " : " [hidden]  ");
        if (!display.record || !display.record->connected[ch]) { text << "Not connected\n"; continue; }
        if (display.frame.waiting) { text << "Waiting for trigger; no accepted visible measurement.\n"; continue; }
        const auto& m = display.frame.measurements[ch];
        text << "Min " << measured(m.minimum, "V") << "  Max " << measured(m.maximum, "V")
             << "  Mean " << measured(m.mean, "V") << "  Vpp " << measured(m.peakToPeak, "V") << "  RMS " << measured(m.rms, "V")
             << "\n     Freq " << measured(m.frequency, "Hz") << "  Period " << measured(m.period, "s")
             << "  Rise " << measured(m.riseTime, "s") << "  Fall " << measured(m.fallTime, "s")
             << "  +Width " << measured(m.pulseWidth, "s") << "  Duty " << measured(m.dutyPercent, "%")
             << (m.edgeResolutionLimited ? " [edge timing sample-limited]" : "") << "\n";
    }
    text << "Measurements: visible coupled samples; RMS includes DC in DC coupling. Timing uses interpolated thresholds. N/A = insufficient complete edges.\n";
    if (display.settings.cursors)
    {
        const auto& s = display.settings;
        text << "Cursors: t1 " << number(s.t1, "s") << "  t2 " << number(s.t2, "s") << "  dt " << number(s.t2 - s.t1, "s")
             << "  1/|dt| " << (s.t1 == s.t2 ? "N/A" : number(oscilloscope::cursorFrequency(s), "Hz"))
             << "\n         V1 " << number(s.v1, "V") << "  V2 " << number(s.v2, "V") << "  dV " << number(s.v2 - s.v1, "V") << "\n";
    }
    measurements.setText(text, false);
    repaint();
}
void OscilloscopeView::timerCallback()
{ if (!callbacks.revision || callbacks.revision() != lastRevision) refresh(); }

void OscilloscopeView::resized()
{
    const auto right = std::min(360, getWidth() / 3);
    controls.setBounds(getWidth() - right, 0, right, getHeight());
    const int w = right - controls.getScrollBarThickness() - 16;
    int y = 8;
    for (auto& row : rows)
    {
        if (row.group == 1)
        {
            row.label->setBounds(8, y, w, 26); y += 28;
            if (row.label == rows.front().label)
            {
                for (int i = 0; i < buttons.size(); ++i) buttons[i]->setBounds(8 + (i % 3) * (w / 3), y + (i / 3) * 30, w / 3 - 4, 26);
                y += 64;
            }
        }
        else
        {
            row.label->setBounds(8, y, w * 53 / 100, 27);
            row.control->setBounds(8 + w * 53 / 100, y + 1, w * 47 / 100, 25); y += 29;
        }
    }
    controlPanel.setSize(right - controls.getScrollBarThickness(), y + 12);
    measurements.setBounds(12, getHeight() - 180, getWidth() - right - 24, 168);
}
juce::Rectangle<float> OscilloscopeView::plotArea() const
{ return { 70.0f, 75.0f, (float)(getWidth() - controls.getWidth() - 140), (float)(getHeight() - 310) }; }
float OscilloscopeView::timeX(double t) const
{ const auto r = plotArea(); return r.getX() + (float)((t - display.frame.start) / (display.settings.timePerDiv * 10.0) * r.getWidth()); }
float OscilloscopeView::voltsY(double v, int ch) const
{ const auto r = plotArea(); return r.getCentreY() - (float)(oscilloscope::divisions(v, display.settings.channel[(size_t)ch]) * r.getHeight() / 8.0); }
double OscilloscopeView::timeAt(float x) const
{ const auto r = plotArea(); return display.frame.start + (x - r.getX()) / r.getWidth() * display.settings.timePerDiv * 10.0; }
double OscilloscopeView::voltsAt(float y, int ch) const
{ const auto r = plotArea(); return oscilloscope::voltsAtDivisions((r.getCentreY() - y) / r.getHeight() * 8.0, display.settings.channel[(size_t)ch]); }

void OscilloscopeView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff10161d));
    const auto r = plotArea();
    g.setColour(juce::Colour(0xff070b0f)); g.fillRect(r);
    g.setFont(juce::Font(12.0f));
    g.setColour(display.running ? juce::Colour(0xff78dcca) : juce::Colour(0xffffcc80));
    g.drawFittedText(juce::String(display.status), 14, 8, getWidth() - controls.getWidth() - 28, 32, juce::Justification::centredLeft, 2);
    g.setColour(juce::Colour(0xffc8d6e5));
    g.drawText(actionMessage.isNotEmpty() ? actionMessage : "Drag a cursor to measure; drag empty plot to pan; wheel to zoom at pointer.",
        14, 43, getWidth() - controls.getWidth() - 28, 22, juce::Justification::centredLeft);
    for (int i = 0; i <= 10; ++i)
    {
        const auto x = r.getX() + r.getWidth() * i / 10.0f;
        g.setColour(juce::Colour(i == 5 ? 0xff3a4a56 : 0xff1f2a33)); g.drawVerticalLine((int)x, r.getY(), r.getBottom());
        if (i % 2 == 0)
        {
            g.setColour(juce::Colour(0xff93a7b0));
            g.drawText(number(display.frame.start + i * display.settings.timePerDiv, "s"), (int)x - 34, (int)r.getBottom() + 5, 68, 18, juce::Justification::centred);
        }
    }
    for (int i = 0; i <= 8; ++i)
    { g.setColour(juce::Colour(i == 4 ? 0xff3a4a56 : 0xff1f2a33)); g.drawHorizontalLine((int)(r.getY() + r.getHeight() * i / 8.0f), r.getX(), r.getRight()); }
    if (!display.frame.ok || display.frame.waiting || !display.record)
    {
        g.setColour(juce::Colour(0xffffaa88));
        g.drawFittedText(display.frame.waiting ? "Waiting for a matching edge in a completed simulation capture.\nRun / Acquire again after changing the circuit or stimulus."
            : juce::String(display.frame.error), r.reduced(24).toNearestInt(), juce::Justification::centred, 4);
        return;
    }
    auto trace = [&](const std::vector<double>& values, int ch) {
        if (values.empty()) return;
        juce::Path line;
        const auto indices = oscilloscope::envelope(display.record->time, values, display.frame.start, display.frame.stop, (int)r.getWidth());
        auto y = [&](double v) { return ch < 2 ? voltsY(v, ch) : r.getCentreY() - (float)(v / display.settings.mathVoltsPerDiv * r.getHeight() / 8.0); };
        for (size_t i = 0; i < indices.size(); ++i)
        {
            const auto k = indices[i];
            const auto p = juce::Point<float>(timeX(display.record->time[k]), juce::jlimit(r.getY() - 10, r.getBottom() + 10, y(values[k])));
            if (i == 0) line.startNewSubPath(p); else line.lineTo(p);
        }
        g.saveState(); g.reduceClipRegion(r.toNearestInt()); g.setColour(ch < 2 ? colour(ch) : juce::Colour(0xffe69cf4));
        g.strokePath(line, juce::PathStrokeType(1.5f));
        if (display.settings.markers && indices.size() < 1500)
            for (auto k : indices) g.fillEllipse(timeX(display.record->time[k]) - 2, y(values[k]) - 2, 4, 4);
        g.restoreState();
    };
    for (int ch = 0; ch < 2; ++ch)
    {
        const auto& c = display.settings.channel[(size_t)ch];
        if (!c.enabled || !display.record->connected[(size_t)ch]) continue;
        trace(display.frame.values[(size_t)ch], ch);
        g.setColour(colour(ch));
        g.drawText("CH" + juce::String(ch + 1) + " " + number(c.voltsPerDiv, "V") + "/div  " + (c.acCoupled ? "AC" : "DC")
            + "  offset " + number(c.offset, "V") + "  x" + juce::String(c.scale),
            (int)r.getX() + ch * (int)r.getWidth() / 2, (int)r.getBottom() + 25, (int)r.getWidth() / 2, 20, juce::Justification::centredLeft);
        for (int i : { 0, 4, 8 })
        {
            const auto y = r.getY() + i * r.getHeight() / 8.0f;
            g.drawText(number(oscilloscope::voltsAtDivisions(4 - i, c), "V"), ch == 0 ? 0 : (int)r.getRight() + 4, (int)y - 9, 66, 18,
                ch == 0 ? juce::Justification::centredRight : juce::Justification::centredLeft);
        }
    }
    trace(display.frame.mathValues, 2);
    if (display.frame.triggered)
    {
        g.setColour(juce::Colour(0xffffaa88));
        const auto x = timeX(display.frame.triggerTime);
        if (x >= r.getX() && x <= r.getRight()) g.drawLine(x, r.getY(), x, r.getY() + 12, 2);
    }
    if (display.settings.cursors)
    {
        const auto& s = display.settings; const int ch = s.cursorChannel;
        g.saveState(); g.reduceClipRegion(r.toNearestInt());
        g.setColour(juce::Colour(0xffb4b9ff));
        for (double t : { s.t1, s.t2 }) g.drawVerticalLine((int)timeX(t), r.getY(), r.getBottom());
        for (double v : { s.v1, s.v2 }) g.drawHorizontalLine((int)voltsY(v, ch), r.getX(), r.getRight());
        g.restoreState();
    }
}

void OscilloscopeView::mouseDown(const juce::MouseEvent& e)
{
    if (!plotArea().contains(e.position) || !display.frame.ok) return;
    dragStart = e.position; dragSettings = display.settings; dragCursor = -1;
    if (display.settings.cursors)
    {
        const std::array<float, 4> distances { std::abs(e.position.x - timeX(dragSettings.t1)), std::abs(e.position.x - timeX(dragSettings.t2)),
            std::abs(e.position.y - voltsY(dragSettings.v1, dragSettings.cursorChannel)), std::abs(e.position.y - voltsY(dragSettings.v2, dragSettings.cursorChannel)) };
        const auto closest = std::min_element(distances.begin(), distances.end());
        if (*closest < 8) dragCursor = (int)(closest - distances.begin());
    }
}
void OscilloscopeView::mouseDrag(const juce::MouseEvent& e)
{
    if (!plotArea().contains(dragStart) || !display.frame.ok) return;
    if (dragCursor < 0)
        commit("horizontal_position", juce::String(dragSettings.horizontalPosition - (e.position.x - dragStart.x) / plotArea().getWidth() * dragSettings.timePerDiv * 10, 12));
    else if (dragCursor < 2) commit(dragCursor == 0 ? "cursor_t1" : "cursor_t2", juce::String(timeAt(e.position.x), 12));
    else commit(dragCursor == 2 ? "cursor_v1" : "cursor_v2", juce::String(voltsAt(e.position.y, dragSettings.cursorChannel), 12));
}
void OscilloscopeView::mouseUp(const juce::MouseEvent&) { dragStart = { -1, -1 }; dragCursor = -1; }
void OscilloscopeView::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (!plotArea().contains(e.position) || !display.frame.ok) return;
    const auto old = display.settings;
    const auto anchor = timeAt(e.position.x);
    const auto newDiv = std::clamp(old.timePerDiv * std::pow(1.5, -wheel.deltaY * 4), 1e-9, 1000.0);
    const auto desiredStart = anchor - (e.position.x - plotArea().getX()) / plotArea().getWidth() * newDiv * 10;
    commit("time_per_div", juce::String(newDiv, 12));
    commit("horizontal_position", juce::String(display.settings.horizontalPosition + desiredStart - display.frame.start, 12));
}
