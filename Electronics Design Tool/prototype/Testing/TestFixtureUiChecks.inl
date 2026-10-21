// Debug-only component checks, linked into the affected app to avoid rebuilding
// JUCE in another heavyweight test target. No circuit solves in this UI suite.
int ElectronicsWorkbench::runTestFixtureUiChecks(const juce::File& output, const juce::File& diagram)
{
    output.createDirectory();
    juce::String log;
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const juce::String& name) {
        ++checks;
        if (!ok) ++failures;
        log << (ok ? "PASS " : "FAIL ") << name << "\n";
    };
    auto snapshot = [&](juce::Component& c, const juce::String& name) {
        juce::FileOutputStream stream(output.getChildFile(name));
        juce::PNGImageFormat().writeImageToStream(c.createComponentSnapshot(c.getLocalBounds()), stream);
    };
    test_mode::TestSetup setup;
    setup.name = "Fixture UI Checks";
    TestFixturePanel::Callbacks callbacks;
    callbacks.getSetupByName = [&](const juce::String&) { return &setup; };
    callbacks.getAuthoritativeDiagramFile = [&] { return diagram; };
    TestFixturePanel panel(setup.name, &setup, callbacks);
    panel.setSize(1100, 720);
    auto* c = dynamic_cast<SchematicCanvasPanel*>(panel.getCanvas());
    const auto original = c->buildCircuitJson();
    check(!original.isEmpty(), "Loads representative schematic into fixture canvas");
    const auto sources = c->fixtureAudioSources();
    bool vin = false, vcc = false;
    for (const auto& s : sources)
    {
        if (s.refdes == "VIN") vin = s.eligible && s.label.contains("0.1") && s.label.contains("1000");
        if (s.refdes == "VCC") vcc = !s.eligible && s.label.contains("250");
    }
    check(vin && vcc, "Source chooser distinguishes VIN signal and VCC supply");
    test_mode::FixtureEquipment input;
    input.id = "IN"; input.name = "Audio Input"; input.isOverride = true; input.overrideRefdes = "VIN";
    setup.addEquipment(input);
    test_mode::FixtureEquipment scope;
    scope.id = "SCOPE"; scope.type = "scope"; scope.name = "Oscilloscope"; scope.pos = { 850, 80 };
    setup.addEquipment(scope);
    check(panel.connectEquipment("IN", "gnd", "VIN.-"), "Ground can be wired before signal lead");
    check(panel.connectEquipment("IN", "out", "A1.IN"), "Audio OUT can use equivalent VIN net");
    auto sim = c->buildSimNetlist();
    check(c->validateFixtureInput(setup, *setup.getEquipment("IN"), sim, true).isEmpty(), "VIN replacement validates against actual OUT and ground nets");
    panel.connectEquipment("IN", "out", "VCC.+");
    const auto conflict = c->validateFixtureInput(setup, *setup.getEquipment("IN"), sim, true);
    check(conflict.contains("VCC") && conflict.contains("VIN") && conflict.contains("Reconnect"), "Mismatch identifies both sources and corrective wiring");
    setup.getEquipment("IN")->isOverride = false;
    check(c->validateFixtureInput(setup, *setup.getEquipment("IN"), sim, true).contains("DC supply"), "Virtual VCC input retains electrical conflict diagnostic");
    setup.getEquipment("IN")->isOverride = true;
    setup.getEquipment("IN")->overrideRefdes = "VCC";
    check(c->validateFixtureInput(setup, *setup.getEquipment("IN"), sim, true).contains("DC supply"), "DC supply cannot be selected as audio stimulus");
    setup.getEquipment("IN")->overrideRefdes = "VIN";
    panel.connectEquipment("IN", "out", "A1.IN");
    setup.getEquipment("IN")->isOverride = false;
    setup.syncSetupFromEquipment();
    check(setup.inputNode == "A1.IN", "Ground-first wiring never becomes virtual stimulus target");
    setup.getEquipment("IN")->isOverride = true;
    setup.syncSetupFromEquipment();
    auto* config = new juce::DynamicObject();
    config->setProperty("audioFilePath", output.getChildFile("missing.wav").getFullPathName());
    check(c->configureFixtureEquipment(setup, "IN", juce::var(config)).contains("Browse"), "Missing WAV gives browse instructions");
    const auto bad = output.getChildFile("invalid.wav"); bad.replaceWithText("not a recording");
    config = new juce::DynamicObject(); config->setProperty("audioFilePath", bad.getFullPathName());
    check(c->configureFixtureEquipment(setup, "IN", juce::var(config)).contains("valid WAV"), "Invalid WAV rejected transactionally");
    check(setup.getEquipment("IN")->audioFilePath.isEmpty(), "Failed configuration preserves previous settings");

    auto event = [&](juce::Point<float> p, int mods, int clicks = 1) {
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), p, juce::ModifierKeys(mods),
            1.0f, 0, 0, 0, 0, c, c, juce::Time::getCurrentTime(), p, juce::Time::getCurrentTime(), clicks, false);
    };
    auto body = event({ 900, 95 }, juce::ModifierKeys::leftButtonModifier, 2);
    c->mouseDoubleClick(body);
    check(c->fixtureInstrumentWindows.size() == 1, "Double-click opens the existing oscilloscope window");
    c->mouseDoubleClick(body);
    check(c->fixtureInstrumentWindows.size() == 1, "Repeated activation reuses the scope window");
    check(dynamic_cast<ScopeView*>(c->fixtureInstrumentWindows[0]->getContentComponent()) != nullptr, "Fixture hosts production ScopeView");
    c->mouseDown(body);
    check(c->draggingFixtureEquipment && c->selectedEquipmentId == "SCOPE", "Body selection and dragging retained");
    c->mouseUp(body);
    auto terminal = event({ 868, 116 }, juce::ModifierKeys::leftButtonModifier);
    c->mouseDown(terminal);
    check(c->isWiringFixture && c->wiringFromTerminalId == "in", "CH1 terminal starts a lead instead of body drag");
    c->mouseUp(event({ 1000, 700 }, 0));
    auto popup = event({ 900, 95 }, juce::ModifierKeys::rightButtonModifier);
    c->mouseDown(popup);
    check(!c->draggingFixtureEquipment && !c->isWiringFixture, "Right-click opens a menu without dragging or wiring");
    juce::PopupMenu::dismissAllActiveMenus();

    panel.connectEquipment("SCOPE", "in", "A1.IN");
    panel.connectEquipment("SCOPE", "ch2", "R_LOAD.1");
    panel.connectEquipment("SCOPE", "ref", "VIN.-");
    setup.hasResult = true;
    setup.lastRenderedOutput.assign(480, 0.0f);
    for (const auto& label : { "A1.IN", "R_LOAD.1", "VIN.-" })
    {
        audio_test::Trace trace; trace.label = label;
        for (int i = 0; i < 480; ++i)
            trace.volts.push_back(juce::String(label) == "VIN.-" ? 0.25f : (juce::String(label) == "A1.IN" ? 0.25f : 250.25f)
                + (juce::String(label) == "A1.IN" ? 0.1f : 100.0f) * std::sin((float)i * 0.13f));
        setup.lastProbeTraces.push_back(std::move(trace));
    }
    ++setup.captureRevision;
    const auto capture = c->captureScope("SCOPE");
    check(capture.ok && capture.connected[0] && capture.connected[1], "Both scope channels acquire cached fixture traces");
    check(std::abs(capture.channel[0][0]) <= 0.101, "Scope subtracts actual reference trace");
    check(panel.instrumentAction("SCOPE", "read", {}).contains("vpp"), "Agent reads the same acquired scope channels");
    c->setInstrumentSetting("SCOPE", "time_per_div", "1m");
    check(c->instrumentSetting("SCOPE", "time_per_div") == "1m", "Native scope controls store fixture settings");
    const auto reloaded = test_mode::TestSetup::fromVar(setup.toVar());
    check(reloaded.getEquipment("SCOPE")->instrumentSettings.at("time_per_div") == "1m", "Scope settings survive fixture sidecar round-trip");
    panel.moveEquipment("SCOPE", 850, 90);
    setup.syncSetupFromEquipment();
    check(setup.hasResult, "Moving scope preserves captured samples");
    const auto cached = c->scopeStates["SCOPE"].acquisitions;
    auto settings = juce::JSON::parse(R"({"ch1_volts_per_div":"50m","ch2_volts_per_div":"50","ch2_offset":"250","cursors":"On","cursor_source":"CH2","cursor_t1":"1m","cursor_t2":"3m","cursor_v1":"200","cursor_v2":"300","math":"CH1 - CH2","math_volts_per_div":"100"})");
    check((bool)juce::JSON::parse(panel.instrumentAction("SCOPE", "configure", settings))["ok"], "Scope accepts high-voltage offset, cursor and math controls");
    auto read = juce::JSON::parse(panel.instrumentAction("SCOPE", "read", {}));
    check(std::abs((double)read["cursors"]["frequencyHz"] - 500) < 1e-6 && (double)read["cursors"]["deltaV"] == 100,
        "Agent reports exact cursor deltas with units from instrument settings");
    check(c->scopeStates["SCOPE"].acquisitions == cached, "Display-only controls reuse acquisition instead of rerunning simulation");
    check(!(bool)juce::JSON::parse(panel.instrumentAction("SCOPE", "configure", juce::JSON::parse(R"({"ch2_volts_per_div":"-1"})")))["ok"],
        "Invalid vertical scale is rejected");
    auto* view = dynamic_cast<ScopeView*>(c->fixtureInstrumentWindows[0]->getContentComponent());
    view->refresh();
    const auto bounds = view->waveformBounds();
    auto scopeEvent = [&](juce::Point<float> p) {
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), p, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),
            1, 0, 0, 0, 0, view, view, juce::Time::getCurrentTime(), p, juce::Time::getCurrentTime(), 1, true);
    };
    const auto state = c->scopeDisplay("SCOPE");
    const auto timeX = bounds.getX() + (float)((state.settings.t1 - state.frame.start) / (state.settings.timePerDiv * 10) * bounds.getWidth());
    auto cursorDown = scopeEvent({ timeX, bounds.getY() + 40 });
    view->mouseDown(cursorDown); view->mouseDrag(scopeEvent({ timeX + 20, bounds.getY() + 40 })); view->mouseUp(cursorDown);
    check(c->scopeSettings("SCOPE").t1 > 0.001 && c->scopeStates["SCOPE"].acquisitions == cached,
        "Dragging a time cursor updates its numerical coordinate without resimulation");
    const auto verticalY = bounds.getCentreY() + bounds.getHeight() / 8;
    auto voltageDown = scopeEvent({ bounds.getX() + bounds.getWidth() * 0.8f, verticalY });
    view->mouseDown(voltageDown); view->mouseDrag(scopeEvent({ voltageDown.position.x, verticalY - 20 })); view->mouseUp(voltageDown);
    check(c->scopeSettings("SCOPE").v1 > 200, "Dragging voltage cursor uses channel offset and volts/div correctly");
    view->refresh();
    snapshot(*c->fixtureInstrumentWindows[0]->getContentComponent(), "scope.png");
    view->setSize(1000, 680); snapshot(*view, "scope-minimum-size.png"); view->setSize(1280, 820);
    check((bool)juce::JSON::parse(panel.instrumentAction("SCOPE", "export", {}, output.getChildFile("scope.csv").getFullPathName()))["ok"], "Scope exports its full retained sample record");
    check(output.getChildFile("scope.csv").loadFileAsString().contains("CH2_raw_V"), "CSV identifies raw and coupled voltage columns");
    panel.instrumentAction("SCOPE", "stop", {});
    check(!c->scopeDisplay("SCOPE").running, "Stop holds an acquired record");
    c->requestScopeCapture = [&] { ++setup.captureRevision; return juce::String(); }; // UI suite deliberately uses test records, no solver.
    panel.instrumentAction("SCOPE", "single", {});
    check(!c->scopeDisplay("SCOPE").running && c->scopeStates["SCOPE"].singleComplete, "Single holds one matching completed capture");
    panel.disconnectEquipment("SCOPE");
    check(c->captureScope("SCOPE").ok, "Stopped scope clearly retains the explicitly held record after wiring changes");
    panel.instrumentAction("SCOPE", "run", {});
    check(!setup.hasResult && !c->captureScope("SCOPE").ok, "Changed wiring cannot display stale acquired data");
    const auto message = juce::String("Simulation conflict: ") + juce::String::repeatedString("complete technical detail; ", 80);
    panel.setDiagnostic(message);
    check(juce::JSON::parse(panel.diagnosticAction("read"))["message"].toString() == message, "Diagnostics preserve complete technical text");
    check((bool)juce::JSON::parse(panel.diagnosticAction("read"))["expanded"], "Errors automatically reveal nonmodal panel");
    snapshot(panel, "diagnostics.png");
    check(!(bool)juce::JSON::parse(panel.diagnosticAction("hide"))["expanded"], "Diagnostic drawer can be dismissed");
    check(juce::JSON::parse(panel.diagnosticAction("read"))["message"].toString() == message, "Dismissed errors remain discoverable");
    panel.diagnosticAction("clear");
    check(setup.lastError.isEmpty(), "Clear removes stale errors");
    FixtureAudioInputDialog dialog({}, 0.1, "VIN", sources, [](const juce::var&) { return juce::String(); });
    snapshot(dialog, "audio_configuration.png");
    bool browse = false;
    for (auto* child : dialog.getChildren())
        if (auto* button = dynamic_cast<juce::TextButton*>(child)) browse |= button->getButtonText() == "Browse...";
    check(browse, "Audio configuration exposes WAV Browse control");
    check(original == c->buildCircuitJson(), "Fixture operations preserve authoritative schematic model");
    double inputOhms = 0;
    bool engineeringValues = true;
    for (const auto& value : std::vector<std::pair<std::string, double>> { {"50", 50}, {"1k", 1000}, {"10M", 1e7}, {"1G", 1e9}, {"50 ohm", 50}, {"10 M\xce\xa9", 1e7} })
        engineeringValues &= oscilloscope::parseInputOhms(value.first, inputOhms) && inputOhms == value.second;
    check(engineeringValues, "Input impedance accepts engineering values and resistance units including 50 ohm");
    circuit_sim::setCapitalMIsMilli(true);
    check(oscilloscope::parseInputOhms("10M", inputOhms) && inputOhms == 1e7, "Input impedance M always means mega even with SPICE milli preference");
    circuit_sim::setCapitalMIsMilli(prefs::get("units.capital_m") != "Mega");
    for (const auto& value : { "0", "-50", "nonsense", "50banana", "1kV", "nan", "inf", "1e999", "1e-320" })
        check(!oscilloscope::parseInputOhms(value, inputOhms), "Rejects invalid input impedance: " + juce::String(value));
    panel.connectEquipment("SCOPE", "in", "A1.IN");
    panel.connectEquipment("SCOPE", "ch2", "R_LOAD.1");
    panel.connectEquipment("SCOPE", "ref", "VIN.-");
    auto configureInput = [&](const juce::String& json) {
        return (bool)juce::JSON::parse(panel.instrumentAction("SCOPE", "configure", juce::JSON::parse(json)))["ok"];
    };
    auto fixtureNetlist = c->buildSimNetlist();
    const auto unloadedElements = fixtureNetlist.circuit.elements().size();
    check(c->applyFixtureScopeLoads(setup, fixtureNetlist).isEmpty() && fixtureNetlist.circuit.elements().size() == unloadedElements,
        "Legacy fixture default Off adds no scope load elements");
    check(configureInput(R"({"ch1_impedance_enabled":"On","ch1_impedance":"50","ch2_impedance_enabled":"Off","ch2_impedance":"1G"})"),
        "Configure independent finite impedance and saved Off value");
    fixtureNetlist = c->buildSimNetlist();
    const auto loadError = c->applyFixtureScopeLoads(setup, fixtureNetlist);
    check(loadError.isEmpty() && fixtureNetlist.circuit.elements().size() == unloadedElements + 1
        && fixtureNetlist.circuit.elements().back().value == 50,
        "Fixture netlist adds exactly one ordinary 50 ohm resistor without duplicate probe loads");
    view->refresh();
    std::function<juce::Component*(juce::Component&, const juce::String&)> findControl;
    findControl = [&](juce::Component& root, const juce::String& id) -> juce::Component* {
        if (root.getComponentID() == id) return &root;
        for (auto* child : root.getChildren()) if (auto* found = findControl(*child, id)) return found;
        return nullptr;
    };
    auto* ch1Ohms = dynamic_cast<juce::TextEditor*>(findControl(*view, "ch1_impedance"));
    auto* ch2Ohms = dynamic_cast<juce::TextEditor*>(findControl(*view, "ch2_impedance"));
    check(ch1Ohms != nullptr && ch2Ohms != nullptr && ch1Ohms->isEnabled() && !ch2Ohms->isEnabled()
        && ch1Ohms->getText().contains(juce::String::charToString(0x03a9)), "Production UI independently enables impedance fields and displays resistance units");
    if (ch1Ohms != nullptr)
    {
        ch1Ohms->setText("0", false); ch1Ohms->onReturnKey();
        check(c->instrumentSetting("SCOPE", "ch1_impedance").getDoubleValue() == 50,
            "Invalid UI entry preserves last finite impedance");
    }
    check(!configureInput(R"({"ch1_impedance":"0","ch2_impedance_enabled":"On"})")
        && c->instrumentSetting("SCOPE", "ch2_impedance_enabled") == "Off", "Invalid multi-setting request is rejected transactionally");
    configureInput(R"({"ch1_impedance_enabled":"Off"})");
    view->refresh();
    check(ch1Ohms != nullptr && !ch1Ohms->isEnabled() && c->instrumentSetting("SCOPE", "ch1_impedance").getDoubleValue() == 50,
        "Off disables field and retains previous 50 ohm value");
    configureInput(R"({"ch1_impedance_enabled":"On"})");
    const auto restoredInput = test_mode::TestSetup::fromVar(juce::JSON::parse(juce::JSON::toString(setup.toVar())));
    const auto& savedInput = restoredInput.getEquipment("SCOPE")->instrumentSettings;
    check(savedInput.at("ch1_impedance_enabled") == "On" && savedInput.at("ch1_impedance").getDoubleValue() == 50
        && savedInput.at("ch2_impedance_enabled") == "Off" && savedInput.at("ch2_impedance").getDoubleValue() == 1e9,
        "Fixture serialization restores both independent impedance states and values");
    check(!setup.hasResult && c->scopeStates["SCOPE"].record == nullptr, "Impedance changes discard stale fixture and held scope samples");
    check(original == c->buildCircuitJson(), "Input impedance never modifies authoritative fixture circuit model");
    view->refresh(); snapshot(*view, "scope-input-impedance.png");
    c->fixtureInstrumentWindows.clear();
    log << checks << " checks, " << failures << " failures\n";
    output.getChildFile("ui-checks.txt").replaceWithText(log);
    return failures == 0 ? 0 : 1;
}
