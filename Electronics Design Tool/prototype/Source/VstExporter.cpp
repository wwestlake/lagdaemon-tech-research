#include "VstExporter.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace vst_exporter
{

namespace
{

std::string sanitizeFileStem(const std::string& name)
{
    std::string out;
    out.reserve(name.size());
    for (char c : name)
    {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == ' ')
            out.push_back(c);
        else
            out.push_back('_');
    }
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out.empty() ? "DjehutiCircuitVst" : out;
}

std::string escapeCppStringLiteral(const std::string& s)
{
    std::ostringstream oss;
    oss << "\"";
    for (std::size_t i = 0; i < s.size(); ++i)
    {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        switch (c)
        {
            case '\\': oss << "\\\\"; break;
            case '\"': oss << "\\\""; break;
            case '\n': oss << "\\n\"\n\""; break;
            case '\r': break;
            case '\t': oss << "\\t"; break;
            default:
                if (c >= 32 && c < 127)
                    oss << static_cast<char>(c);
                else
                    oss << "\\x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(c) << std::dec;
                break;
        }
    }
    oss << "\"";
    return oss.str();
}

std::string formatPluginCodeHex(const std::string& code4)
{
    std::string padded = code4;
    while (padded.size() < 4) padded.push_back('0');
    const std::uint32_t v =
        (static_cast<std::uint32_t>(static_cast<unsigned char>(padded[0])) << 24) |
        (static_cast<std::uint32_t>(static_cast<unsigned char>(padded[1])) << 16) |
        (static_cast<std::uint32_t>(static_cast<unsigned char>(padded[2])) << 8)  |
        (static_cast<std::uint32_t>(static_cast<unsigned char>(padded[3])));
    std::ostringstream oss;
    oss << "0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << v;
    return oss.str();
}

constexpr const char* kTemplateBundleName = "Djehuti Circuit VST.vst3";
constexpr const char* kDeveloperBuildConfig = "Debug";

// Where the .vst3 binary of an export comes from.
//  - DeveloperBuild: a development checkout (the app or test runs from <prototype>/bin, or the
//    request names a CMake build directory); the DjehutiCircuitVst_VST3 target is rebuilt there
//    with the requested identity.
//  - PackagedTemplate: an installed copy; the precompiled Release template shipped next to the
//    executable is copied and the requested circuit is supplied through Contents/Resources.
struct Vst3BinaryPlan
{
    enum class Source { DeveloperBuild, PackagedTemplate };
    Source source = Source::PackagedTemplate;
    juce::File buildDir;
    juce::File templateBinary;
};

juce::File locateDevelopmentPrototypeRoot()
{
    // Only the executable's own location identifies a development checkout. The working
    // directory and fixed checkout paths are deliberately not consulted, so an installed copy
    // never discovers (and silently depends on) a developer's source tree.
    auto exeDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
    for (int depth = 0; depth < 4; ++depth)
    {
        if (exeDir.getChildFile("CMakeLists.txt").existsAsFile() &&
            exeDir.getChildFile("Source").isDirectory())
            return exeDir;
        exeDir = exeDir.getParentDirectory();
    }

#if defined(DJEHUTI_PROTOTYPE_SOURCE_DIR)
    // Developer test executables are compiled with their checkout location.
    const juce::File defDir(DJEHUTI_PROTOTYPE_SOURCE_DIR);
    if (defDir.getChildFile("CMakeLists.txt").existsAsFile())
        return defDir;
#endif

    return {};
}

juce::File packagedTemplateBinary()
{
    return juce::File::getSpecialLocation(juce::File::currentExecutableFile)
        .getParentDirectory()
        .getChildFile("templates")
        .getChildFile("vst3")
        .getChildFile(kTemplateBundleName)
        .getChildFile("Contents")
        .getChildFile("x86_64-win")
        .getChildFile(kTemplateBundleName);
}

bool resolveVst3BinaryPlan(const juce::File& requestedBuildDir, Vst3BinaryPlan& plan, std::string& outError)
{
    if (requestedBuildDir != juce::File())
    {
        plan.source = Vst3BinaryPlan::Source::DeveloperBuild;
        plan.buildDir = requestedBuildDir;
    }
    else if (const auto devRoot = locateDevelopmentPrototypeRoot(); devRoot != juce::File())
    {
        plan.source = Vst3BinaryPlan::Source::DeveloperBuild;
        plan.buildDir = devRoot.getChildFile("build");
    }
    else
    {
        plan.source = Vst3BinaryPlan::Source::PackagedTemplate;
        plan.templateBinary = packagedTemplateBinary();
        if (!plan.templateBinary.existsAsFile())
        {
            outError = "This installation is missing its packaged VST3 runtime template (expected " +
                       plan.templateBinary.getFullPathName().toStdString() +
                       "). Repair or reinstall Djehuti Electronics Workbench. Installed copies export from the "
                       "packaged template only and do not build plugins from a development checkout.";
            return false;
        }
        return true;
    }

    if (!plan.buildDir.getChildFile("DjehutiCircuitVst_VST3.vcxproj").existsAsFile())
    {
        outError = "Developer VST3 export needs a configured CMake build directory containing "
                   "DjehutiCircuitVst_VST3.vcxproj, but none was found at " +
                   plan.buildDir.getFullPathName().toStdString() + ". Configure the prototype with CMake first.";
        return false;
    }
    return true;
}

bool writeGeneratedIdentityAndPayload(const juce::File& buildDir,
                                      const dsp_artifact::ExecutableArtifact& artifact,
                                      std::string& outError)
{
    auto genDir = buildDir.getChildFile("GeneratedVst");
    if (!genDir.isDirectory() && !genDir.createDirectory())
    {
        outError = "Failed to create GeneratedVst directory in build directory: " + genDir.getFullPathName().toStdString();
        return false;
    }

    const auto identityFile = genDir.getChildFile("GeneratedPluginIdentity.h");
    const auto payloadFile = genDir.getChildFile("GeneratedPluginPayload.cpp");

    std::ostringstream idStream;
    idStream << "#pragma once\n\n"
             << "// Auto-generated by Djehuti Electronics Workbench VST3 Exporter.\n"
             << "#ifdef DJEHUTI_VST_PACKAGE_ID\n #undef DJEHUTI_VST_PACKAGE_ID\n#endif\n"
             << "#define DJEHUTI_VST_PACKAGE_ID        " << escapeCppStringLiteral(artifact.packageId) << "\n"
             << "#ifdef DJEHUTI_VST_PRODUCT_NAME\n #undef DJEHUTI_VST_PRODUCT_NAME\n#endif\n"
             << "#define DJEHUTI_VST_PRODUCT_NAME      " << escapeCppStringLiteral(artifact.productName) << "\n"
             << "#ifdef DJEHUTI_VST_DESCRIPTION\n #undef DJEHUTI_VST_DESCRIPTION\n#endif\n"
             << "#define DJEHUTI_VST_DESCRIPTION       " << escapeCppStringLiteral(artifact.productName + " (Djehuti Circuit VST3)") << "\n"
             << "#ifdef DJEHUTI_VST_MANUFACTURER\n #undef DJEHUTI_VST_MANUFACTURER\n#endif\n"
             << "#define DJEHUTI_VST_MANUFACTURER      " << escapeCppStringLiteral(artifact.manufacturerName) << "\n"
             << "#ifdef DJEHUTI_VST_VERSION_STRING\n #undef DJEHUTI_VST_VERSION_STRING\n#endif\n"
             << "#define DJEHUTI_VST_VERSION_STRING    " << escapeCppStringLiteral(artifact.productVersion) << "\n"
             << "#ifdef DJEHUTI_VST_VERSION_HEX\n #undef DJEHUTI_VST_VERSION_HEX\n#endif\n"
             << "#define DJEHUTI_VST_VERSION_HEX       0x00010000\n"
             << "#ifdef DJEHUTI_VST_PLUGIN_CODE\n #undef DJEHUTI_VST_PLUGIN_CODE\n#endif\n"
             << "#define DJEHUTI_VST_PLUGIN_CODE       " << formatPluginCodeHex(artifact.pluginCode4CC) << "\n\n"
             << "#ifdef JucePlugin_Name\n #undef JucePlugin_Name\n#endif\n"
             << "#define JucePlugin_Name               DJEHUTI_VST_PRODUCT_NAME\n\n"
             << "#ifdef JucePlugin_Desc\n #undef JucePlugin_Desc\n#endif\n"
             << "#define JucePlugin_Desc               DJEHUTI_VST_DESCRIPTION\n\n"
             << "#ifdef JucePlugin_Manufacturer\n #undef JucePlugin_Manufacturer\n#endif\n"
             << "#define JucePlugin_Manufacturer       DJEHUTI_VST_MANUFACTURER\n\n"
             << "#ifdef JucePlugin_VersionString\n #undef JucePlugin_VersionString\n#endif\n"
             << "#define JucePlugin_VersionString      DJEHUTI_VST_VERSION_STRING\n\n"
             << "#ifdef JucePlugin_VersionCode\n #undef JucePlugin_VersionCode\n#endif\n"
             << "#define JucePlugin_VersionCode        DJEHUTI_VST_VERSION_HEX\n\n"
             << "#ifdef JucePlugin_PluginCode\n #undef JucePlugin_PluginCode\n#endif\n"
             << "#define JucePlugin_PluginCode         DJEHUTI_VST_PLUGIN_CODE\n";

    auto writeIfChanged = [](const juce::File& file, const std::string& newContent, std::string& err) -> bool {
        if (file.existsAsFile())
        {
            const std::string existing = file.loadFileAsString().toStdString();
            if (existing == newContent)
                return true;
        }
        if (!file.replaceWithText(juce::String(newContent)))
        {
            err = "Failed to write " + file.getFullPathName().toStdString();
            return false;
        }
        return true;
    };

    if (!writeIfChanged(identityFile, idStream.str(), outError))
        return false;

    // Artifact JSON is loaded from Contents/Resources/model.frust-il packaged within the VST3 bundle.
    // Emitting an empty string here avoids MSVC C2026 (string literal >65,535 bytes) while keeping
    // the linkage interface intact.
    std::ostringstream payStream;
    payStream << "#include \"CircuitVstPlugin.h\"\n\n"
              << "namespace djehuti_circuit_vst\n{\n\n"
              << "const char* getEmbeddedArtifactJson() noexcept\n{\n"
              << "    return \"\";\n"
              << "}\n\n"
              << "} // namespace djehuti_circuit_vst\n";

    if (!writeIfChanged(payloadFile, payStream.str(), outError))
        return false;

    return true;
}

bool buildAndPackageVst3Target(const Vst3BinaryPlan& plan,
                               const std::string& productName,
                               const juce::File& packageDir,
                               const juce::String& artifactJson,
                               const juce::String& uiJson,
                               const juce::String& manifestJsonText,
                               juce::File& outBundleDir,
                               juce::File& outBinaryFile,
                               std::string& outLog,
                               std::string& outError)
{
    juce::File sourceBinary = plan.templateBinary;

    if (plan.source == Vst3BinaryPlan::Source::DeveloperBuild)
    {
        // Developer workflow: single-core (/m:1) build of the VST3 target with the generated identity
        juce::StringArray cmd;
        cmd.add("cmake");
        cmd.add("--build");
        cmd.add(plan.buildDir.getFullPathName());
        cmd.add("--config");
        cmd.add(kDeveloperBuildConfig);
        cmd.add("--target");
        cmd.add("DjehutiCircuitVst_VST3");
        cmd.add("--");
        cmd.add("/m:1");
        cmd.add("/nologo");

        juce::ChildProcess proc;
        if (!proc.start(cmd))
        {
            outError = "Failed to launch cmake --build for DjehutiCircuitVst_VST3. Make sure CMake is on PATH.";
            return false;
        }
        const juce::String buildOutput = proc.readAllProcessOutput();
        outLog += buildOutput.toStdString();
        if (proc.getExitCode() != 0)
        {
            outError = "VST3 MSBuild failed with exit code " + std::to_string(proc.getExitCode()) + ":\n" +
                       buildOutput.toStdString();
            return false;
        }

        // The known output of that build; no other configuration or plugin is substituted.
        sourceBinary = plan.buildDir
            .getChildFile("DjehutiCircuitVst_artefacts")
            .getChildFile(kDeveloperBuildConfig)
            .getChildFile("VST3")
            .getChildFile(kTemplateBundleName)
            .getChildFile("Contents")
            .getChildFile("x86_64-win")
            .getChildFile(kTemplateBundleName);
    }

    if (!sourceBinary.existsAsFile())
    {
        outError = "VST3 runtime binary was not found at " + sourceBinary.getFullPathName().toStdString();
        return false;
    }

    const std::string safeStem = sanitizeFileStem(productName);
    outBundleDir = packageDir.getChildFile(juce::String(safeStem) + ".vst3");
    const juce::File win64Dir = outBundleDir.getChildFile("Contents").getChildFile("x86_64-win");
    const juce::File resourcesDir = outBundleDir.getChildFile("Contents").getChildFile("Resources");

    win64Dir.createDirectory();
    resourcesDir.createDirectory();

    outBinaryFile = win64Dir.getChildFile(juce::String(safeStem) + ".vst3");
    if (!sourceBinary.copyFileTo(outBinaryFile))
    {
        outError = "Failed to copy VST3 binary to " + outBinaryFile.getFullPathName().toStdString();
        return false;
    }

    // Embed self-contained resources inside the VST3 bundle
    if (!resourcesDir.getChildFile("model.frust-il").replaceWithText(artifactJson) ||
        !resourcesDir.getChildFile("ui.json").replaceWithText(uiJson) ||
        !resourcesDir.getChildFile("manifest.json").replaceWithText(manifestJsonText))
    {
        outError = "Failed to write bundle resources to " + resourcesDir.getFullPathName().toStdString();
        return false;
    }

    return true;
}

ExportResult exportSingleBundle(const ExportRequest& req,
                                const dsp_artifact::BuildArtifactInput& artInput,
                                const juce::File& packageDir)
{
    ExportResult res;
    if (!packageDir.isDirectory() && !packageDir.createDirectory())
    {
        res.error = "Failed to create export directory: " + packageDir.getFullPathName().toStdString();
        return res;
    }

    const auto built = dsp_artifact::buildExecutableArtifact(artInput);
    if (!built.ok)
    {
        res.error = built.error;
        return res;
    }

    ExportedPluginBundleInfo info;
    info.packageId = built.artifact.packageId;
    info.productName = built.artifact.productName;
    info.pluginCode4CC = built.artifact.pluginCode4CC;
    info.vst3ComponentUidHex = built.artifact.vst3ComponentUidHex;
    info.packageDirectory = packageDir;
    info.artifact = built.artifact;

    // 1. Write executable .frust-il artifact
    info.artifactFile = packageDir.getChildFile("model.frust-il");
    if (!info.artifactFile.replaceWithText(built.serializedJson))
    {
        res.error = "Failed to write model.frust-il to " + info.artifactFile.getFullPathName().toStdString();
        return res;
    }

    // 2. Write optional development FRust source reference
    if (req.retainDevelopmentSource && !built.artifact.slots.empty() &&
        !built.artifact.slots[0].optionalFrustSource.empty())
    {
        info.frustSourceFile = packageDir.getChildFile("model.frust");
        std::ostringstream srcStream;
        for (const auto& slot : built.artifact.slots)
        {
            srcStream << "// === Slot: " << slot.slotId << " (" << slot.slotName << ") ===\n"
                      << slot.optionalFrustSource << "\n";
        }
        info.frustSourceFile.replaceWithText(juce::String(srcStream.str()));
    }

    // 3. Write runtime manifest.json
    info.manifestFile = packageDir.getChildFile("manifest.json");
    const juce::String manifestText = !built.artifact.slots.empty()
                                          ? juce::String(built.artifact.slots[0].runtimeManifestJson)
                                          : juce::String("{}");
    info.manifestFile.replaceWithText(manifestText);

    // 4. Write UI & parameter schema ui.json
    info.uiSchemaFile = packageDir.getChildFile("ui.json");
    const juce::String uiJsonText = buildUiSchemaJson(built.artifact);
    info.uiSchemaFile.replaceWithText(uiJsonText);

    // 5. Write persistent vst_export_config.json
    info.exportConfigFile = packageDir.getChildFile("vst_export_config.json");
    saveExportConfigToFile(req, info.exportConfigFile);

    // 6. Package the .vst3 bundle: developer checkouts rebuild the target with the generated
    //    identity; installed copies copy the packaged Release template.
    if (req.buildVst3Bundle)
    {
        Vst3BinaryPlan plan;
        std::string planErr;
        if (!resolveVst3BinaryPlan(req.cmakeBuildDirectory, plan, planErr))
        {
            res.error = planErr;
            return res;
        }

        if (plan.source == Vst3BinaryPlan::Source::DeveloperBuild)
        {
            info.vst3BinarySource = "developer_build";
            std::string genErr;
            if (!writeGeneratedIdentityAndPayload(plan.buildDir, built.artifact, genErr))
            {
                res.error = genErr;
                return res;
            }
        }
        else
        {
            info.vst3BinarySource = "packaged_release_template";
            info.identityLimitation =
                "The packaged template's compiled VST3 class identity (factory name 'Djehuti Circuit VST', "
                "plugin code DjCv and its component UID) is used. The requested product name, controls, "
                "effects and state come from Contents/Resources, but hosts identify every template-based "
                "export as the same plugin class, so two such exports cannot be told apart by a host.";
        }

        std::string buildLog;
        std::string buildErr;
        if (!buildAndPackageVst3Target(plan,
                                       built.artifact.productName,
                                       packageDir,
                                       built.serializedJson,
                                       uiJsonText,
                                       manifestText,
                                       info.vst3BundleDir,
                                       info.vst3BinaryFile,
                                       buildLog,
                                       buildErr))
        {
            res.diagnosticsLog = buildLog;
            res.error = buildErr;
            return res;
        }
        res.diagnosticsLog = buildLog;
    }

    res.bundles.push_back(std::move(info));
    res.ok = true;
    return res;
}

} // namespace

juce::String buildUiSchemaJson(const dsp_artifact::ExecutableArtifact& artifact)
{
    auto* root = new juce::DynamicObject();
    root->setProperty("schemaVersion", 2);
    root->setProperty("packageId", juce::String(artifact.packageId));
    root->setProperty("productName", juce::String(artifact.productName));
    root->setProperty("productVersion", juce::String(artifact.productVersion));
    root->setProperty("manufacturerName", juce::String(artifact.manufacturerName));
    root->setProperty("pluginCode4CC", juce::String(artifact.pluginCode4CC));
    root->setProperty("vst3ComponentUidHex", juce::String(artifact.vst3ComponentUidHex));
    root->setProperty("effectTopology", juce::String(artifact.effectTopology));

    juce::Array<juce::var> effectsArr;
    for (const auto& eff : artifact.effects)
    {
        auto* eo = new juce::DynamicObject();
        eo->setProperty("id", juce::String(eff.id));
        eo->setProperty("name", juce::String(eff.name));
        eo->setProperty("description", juce::String(eff.description));
        eo->setProperty("modelSlotIndex", eff.modelSlotIndex);
        effectsArr.add(juce::var(eo));
    }
    root->setProperty("effects", effectsArr);

    juce::Array<juce::var> controlsArr;
    auto addFramingControl = [&](const char* id, const char* name, const char* kind, const char* unit,
                                 double minV, double maxV, double defV)
    {
        auto* co = new juce::DynamicObject();
        co->setProperty("id", juce::String(id));
        co->setProperty("name", juce::String(name));
        co->setProperty("kind", juce::String(kind));
        co->setProperty("unit", juce::String(unit));
        co->setProperty("min", minV);
        co->setProperty("max", maxV);
        co->setProperty("default", defV);
        controlsArr.add(juce::var(co));
    };

    addFramingControl("bypass", "Bypass", "toggle", "", 0.0, 1.0, 0.0);
    addFramingControl("input_gain_db", "Input Gain", "rotary_knob", "dB", -24.0, 24.0, 0.0);
    if (artifact.effects.size() > 1)
        addFramingControl("effect_select", "Effect Selector", "segmented_selector", "", 0.0,
                          static_cast<double>(artifact.effects.size() - 1), 0.0);

    for (const auto& p : artifact.parameters)
    {
        if (p.hiddenInModeSelector)
            continue;
        auto* co = new juce::DynamicObject();
        co->setProperty("id", juce::String(p.id));
        co->setProperty("name", juce::String(p.name));
        co->setProperty("kind", juce::String(p.kind == "switch" ? "toggle" : "rotary_knob"));
        co->setProperty("unit", juce::String(p.unit));
        co->setProperty("min", p.minValue);
        co->setProperty("max", p.maxValue);
        co->setProperty("default", p.defaultValue);
        co->setProperty("livePortId", juce::String(p.livePortId));
        controlsArr.add(juce::var(co));
    }

    addFramingControl("mix", "Dry/Wet Mix", "rotary_knob", "%", 0.0, 1.0, 1.0);
    addFramingControl("output_level_db", "Output Level", "rotary_knob", "dB", -24.0, 24.0, 0.0);
    root->setProperty("controls", controlsArr);

    return juce::JSON::toString(juce::var(root), false);
}

juce::String serializeExportConfigToJson(const ExportRequest& req)
{
    auto* root = new juce::DynamicObject();
    root->setProperty("schemaVersion", 1);
    root->setProperty("packageId", juce::String(req.packageId));
    root->setProperty("productName", juce::String(req.productName));
    root->setProperty("productVersion", juce::String(req.productVersion));
    root->setProperty("manufacturerName", juce::String(req.manufacturerName));
    root->setProperty("exportMode", static_cast<int>(req.mode));
    root->setProperty("outputDirectory", req.outputDirectory.getFullPathName());
    root->setProperty("buildVst3Bundle", req.buildVst3Bundle);
    root->setProperty("retainDevelopmentSource", req.retainDevelopmentSource);

    juce::Array<juce::var> circuitsArr;
    for (const auto& c : req.circuits)
    {
        auto* co = new juce::DynamicObject();
        co->setProperty("slotId", juce::String(c.slotId));
        co->setProperty("slotName", juce::String(c.slotName));
        co->setProperty("circuit", dsp_artifact::serializeCircuitToVar(c.circuit, c.dspConfig));
        circuitsArr.add(juce::var(co));
    }
    root->setProperty("circuits", circuitsArr);

    juce::Array<juce::var> effectsArr;
    for (const auto& e : req.effects)
    {
        auto* eo = new juce::DynamicObject();
        eo->setProperty("id", juce::String(e.id));
        eo->setProperty("name", juce::String(e.name));
        eo->setProperty("description", juce::String(e.description));
        eo->setProperty("modelSlotIndex", e.modelSlotIndex);
        auto* ov = new juce::DynamicObject();
        for (const auto& kv : e.livePortOverrides)
            ov->setProperty(juce::Identifier(kv.first), kv.second);
        eo->setProperty("livePortOverrides", juce::var(ov));
        effectsArr.add(juce::var(eo));
    }
    root->setProperty("effects", effectsArr);

    juce::Array<juce::var> paramsArr;
    for (const auto& p : req.customParameters)
    {
        auto* po = new juce::DynamicObject();
        po->setProperty("id", juce::String(p.id));
        po->setProperty("name", juce::String(p.name));
        po->setProperty("unit", juce::String(p.unit));
        po->setProperty("kind", juce::String(p.kind));
        po->setProperty("minValue", p.minValue);
        po->setProperty("maxValue", p.maxValue);
        po->setProperty("defaultValue", p.defaultValue);
        po->setProperty("step", p.step);
        po->setProperty("slotIndex", p.slotIndex);
        po->setProperty("livePortId", juce::String(p.livePortId));
        po->setProperty("baseResistance", p.baseResistance);
        po->setProperty("isWiperToPin2", p.isWiperToPin2);
        po->setProperty("hiddenInModeSelector", p.hiddenInModeSelector);
        paramsArr.add(juce::var(po));
    }
    root->setProperty("customParameters", paramsArr);

    return juce::JSON::toString(juce::var(root), false);
}

bool loadExportConfigFromJson(const juce::String& jsonText, ExportRequest& outReq, std::string& outError)
{
    juce::var parsed;
    const auto pr = juce::JSON::parse(jsonText, parsed);
    if (pr.failed() || !parsed.isObject())
    {
        outError = "Invalid vst_export_config.json: " + pr.getErrorMessage().toStdString();
        return false;
    }

    const auto* root = parsed.getDynamicObject();
    outReq.packageId = root->getProperty("packageId").toString().toStdString();
    outReq.productName = root->getProperty("productName").toString().toStdString();
    outReq.productVersion = root->getProperty("productVersion").toString().toStdString();
    outReq.manufacturerName = root->getProperty("manufacturerName").toString().toStdString();
    outReq.mode = static_cast<ExportMode>(static_cast<int>(root->getProperty("exportMode")));
    const juce::String outDirStr = root->getProperty("outputDirectory").toString();
    if (outDirStr.isNotEmpty())
        outReq.outputDirectory = juce::File(outDirStr);
    outReq.buildVst3Bundle = static_cast<bool>(root->getProperty("buildVst3Bundle"));
    outReq.retainDevelopmentSource = static_cast<bool>(root->getProperty("retainDevelopmentSource"));

    outReq.circuits.clear();
    if (const auto* ca = root->getProperty("circuits").getArray())
    {
        for (const auto& cv : *ca)
        {
            const auto* co = cv.getDynamicObject();
            if (co == nullptr) continue;
            dsp_artifact::BuildArtifactInput::CircuitInputSlot slot;
            slot.slotId = co->getProperty("slotId").toString().toStdString();
            slot.slotName = co->getProperty("slotName").toString().toStdString();
            std::string cErr;
            if (!dsp_artifact::deserializeCircuitFromVar(co->getProperty("circuit"), slot.circuit, slot.dspConfig, cErr))
            {
                outError = "Circuit deserialization failed in export config: " + cErr;
                return false;
            }
            outReq.circuits.push_back(std::move(slot));
        }
    }

    outReq.effects.clear();
    if (const auto* ea = root->getProperty("effects").getArray())
    {
        for (const auto& ev : *ea)
        {
            const auto* eo = ev.getDynamicObject();
            if (eo == nullptr) continue;
            dsp_artifact::EffectPresetSlot eff;
            eff.id = eo->getProperty("id").toString().toStdString();
            eff.name = eo->getProperty("name").toString().toStdString();
            eff.description = eo->getProperty("description").toString().toStdString();
            eff.modelSlotIndex = static_cast<int>(eo->getProperty("modelSlotIndex"));
            if (const auto* ov = eo->getProperty("livePortOverrides").getDynamicObject())
            {
                for (const auto& prop : ov->getProperties())
                    eff.livePortOverrides[prop.name.toString().toStdString()] = static_cast<double>(prop.value);
            }
            outReq.effects.push_back(std::move(eff));
        }
    }

    outReq.customParameters.clear();
    if (const auto* pa = root->getProperty("customParameters").getArray())
    {
        for (const auto& pv : *pa)
        {
            const auto* po = pv.getDynamicObject();
            if (po == nullptr) continue;
            dsp_artifact::ParameterSpec p;
            p.id = po->getProperty("id").toString().toStdString();
            p.name = po->getProperty("name").toString().toStdString();
            p.unit = po->getProperty("unit").toString().toStdString();
            p.kind = po->getProperty("kind").toString().toStdString();
            p.minValue = static_cast<double>(po->getProperty("minValue"));
            p.maxValue = static_cast<double>(po->getProperty("maxValue"));
            p.defaultValue = static_cast<double>(po->getProperty("defaultValue"));
            p.step = static_cast<double>(po->getProperty("step"));
            p.slotIndex = static_cast<int>(po->getProperty("slotIndex"));
            p.livePortId = po->getProperty("livePortId").toString().toStdString();
            p.baseResistance = static_cast<double>(po->getProperty("baseResistance"));
            p.isWiperToPin2 = static_cast<bool>(po->getProperty("isWiperToPin2"));
            p.hiddenInModeSelector = static_cast<bool>(po->getProperty("hiddenInModeSelector"));
            outReq.customParameters.push_back(std::move(p));
        }
    }

    return true;
}

bool saveExportConfigToFile(const ExportRequest& req, const juce::File& file)
{
    file.getParentDirectory().createDirectory();
    return file.replaceWithText(serializeExportConfigToJson(req));
}

bool loadExportConfigFromFile(const juce::File& file, ExportRequest& outReq, std::string& outError)
{
    if (!file.existsAsFile())
    {
        outError = "Export config file does not exist: " + file.getFullPathName().toStdString();
        return false;
    }
    return loadExportConfigFromJson(file.loadFileAsString(), outReq, outError);
}

ExportResult runExport(const ExportRequest& request)
{
    ExportResult finalRes;
    if (request.circuits.empty())
    {
        finalRes.error = "Export request contains no circuits.";
        return finalRes;
    }

    const juce::File baseOutDir = (request.outputDirectory != juce::File())
        ? request.outputDirectory
        : juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
              .getChildFile("DjehutiElectronicsLab")
              .getChildFile("exported_vst3");

    if (request.mode == ExportMode::ThreeSeparatePlugins)
    {
        // Export 3 separate VST3 plugins, each with its own stable identity, 4CC, and artifact
        const std::size_t count = std::max<std::size_t>(3, request.effects.size());
        for (std::size_t i = 0; i < std::min<std::size_t>(3, count); ++i)
        {
            dsp_artifact::BuildArtifactInput subInput;
            const std::string suffix = std::to_string(i + 1);
            const std::string effName = (i < request.effects.size() && !request.effects[i].name.empty())
                                            ? request.effects[i].name
                                            : ("Effect " + suffix);

            subInput.packageId = request.packageId + ".effect_" + suffix;
            subInput.productName = request.productName + " - " + effName;
            subInput.productVersion = request.productVersion;
            subInput.manufacturerName = request.manufacturerName;
            subInput.effectTopology = "single";
            subInput.retainDevelopmentSource = request.retainDevelopmentSource;

            const std::size_t slotIdx = (i < request.effects.size())
                ? static_cast<std::size_t>(std::max(0, std::min(static_cast<int>(request.circuits.size()) - 1,
                                                                request.effects[i].modelSlotIndex)))
                : std::min(i, request.circuits.size() - 1);

            subInput.circuits.push_back(request.circuits[slotIdx]);

            dsp_artifact::EffectPresetSlot singleEff;
            singleEff.id = "effect_1";
            singleEff.name = effName;
            singleEff.modelSlotIndex = 0;
            if (i < request.effects.size())
            {
                singleEff.description = request.effects[i].description;
                singleEff.livePortOverrides = request.effects[i].livePortOverrides;
            }
            subInput.effects.push_back(std::move(singleEff));

            const juce::File subDir = baseOutDir.getChildFile(juce::String(sanitizeFileStem(subInput.productName)));
            ExportRequest subReq = request;
            subReq.packageId = subInput.packageId;
            subReq.productName = subInput.productName;
            subReq.mode = ExportMode::SingleEffect;
            subReq.outputDirectory = subDir;

            auto subRes = exportSingleBundle(subReq, subInput, subDir);
            finalRes.diagnosticsLog += subRes.diagnosticsLog;
            if (!subRes.ok)
            {
                finalRes.error = "Failed exporting separate plugin #" + suffix + " ('" + subInput.productName + "'): " + subRes.error;
                return finalRes;
            }
            for (auto& b : subRes.bundles)
                finalRes.bundles.push_back(std::move(b));
        }

        finalRes.ok = true;
        return finalRes;
    }

    // Single VST3 plugin (with 1 effect, 3 coupled effects, or 3 independent circuit models)
    dsp_artifact::BuildArtifactInput artInput;
    artInput.packageId = request.packageId;
    artInput.productName = request.productName;
    artInput.productVersion = request.productVersion;
    artInput.manufacturerName = request.manufacturerName;
    artInput.circuits = request.circuits;
    artInput.effects = request.effects;
    artInput.customParameters = request.customParameters;
    artInput.retainDevelopmentSource = request.retainDevelopmentSource;

    if (request.mode == ExportMode::ThreeEffectsCoupled)
        artInput.effectTopology = "three_coupled";
    else if (request.mode == ExportMode::ThreeEffectsIndependent)
        artInput.effectTopology = "three_independent";
    else
        artInput.effectTopology = "single";

    return exportSingleBundle(request, artInput, baseOutDir);
}

dsp_artifact::BuildArtifactInput::CircuitInputSlot makeLinearRcToneCircuit(double sampleRate)
{
    dsp_artifact::BuildArtifactInput::CircuitInputSlot slot;
    slot.slotId = "linear_rc_tone";
    slot.slotName = "Linear RC Tone Filter";

    circuit_sim::Circuit& c = slot.circuit;
    const auto nIn = c.addNode();
    const auto nMid = c.addNode();
    const auto nOut = c.addNode();

    circuit_sim::Waveform w;
    w.kind = circuit_sim::Waveform::Kind::Dc;
    w.offset = 0.0;
    const int vIn = c.addVoltageSource("Vin", nIn, 0, w);

    c.addResistor("R_series", nIn, nMid, 1000.0);
    c.addVariableResistor("POT_tone", nMid, nOut, 10000.0, "tone_pot", false);
    c.addCapacitor("C_shunt", nOut, 0, 47e-9);
    c.addResistor("R_load", nOut, 0, 100000.0);

    slot.dspConfig.sampleRate = sampleRate;
    slot.dspConfig.audioInputElement = vIn;
    slot.dspConfig.audioOutputNode = nOut;
    slot.dspConfig.inputVolts = 1.0;
    slot.dspConfig.outputFullScaleVolts = 1.0;
    return slot;
}

dsp_artifact::BuildArtifactInput::CircuitInputSlot makeNonlinearDiodeClipperCircuit(double sampleRate)
{
    dsp_artifact::BuildArtifactInput::CircuitInputSlot slot;
    slot.slotId = "nonlinear_diode_clipper";
    slot.slotName = "Nonlinear Diode Clipper Overdrive";

    circuit_sim::Circuit& c = slot.circuit;
    const auto nIn = c.addNode();
    const auto nDrive = c.addNode();
    const auto nClip = c.addNode();
    const auto nAsym = c.addNode();
    const auto nOut = c.addNode();

    circuit_sim::Waveform w;
    w.kind = circuit_sim::Waveform::Kind::Dc;
    w.offset = 0.0;
    const int vIn = c.addVoltageSource("Vin", nIn, 0, w);

    c.addResistor("R_in", nIn, nDrive, 1000.0);
    c.addVariableResistor("POT_drive", nDrive, nClip, 10000.0, "drive_pot", true);

    // Anti-parallel silicon clipping pair with switchable asymmetry
    circuit_sim::DiodeModel dmSi;
    dmSi.saturationCurrent = 1e-14;
    dmSi.emission = 1.0;
    c.addDiode("D_pos", nClip, 0, dmSi);
    c.addDiode("D_neg1", 0, nAsym, dmSi);
    c.addDiode("D_neg2", nAsym, nClip, dmSi);
    c.addSwitch("SW_symmetry", nAsym, nClip, "sym_sw");
    c.addResistor("R_bleed_asym", nAsym, 0, 100e6);

    c.addCapacitor("C_hf", nClip, 0, 10e-9);
    c.addVariableResistor("POT_tone", nClip, nOut, 4700.0, "tone_pot", false);
    c.addCapacitor("C_tone", nOut, 0, 22e-9);
    c.addResistor("R_load", nOut, 0, 47000.0);

    slot.dspConfig.sampleRate = sampleRate;
    slot.dspConfig.audioInputElement = vIn;
    slot.dspConfig.audioOutputNode = nOut;
    slot.dspConfig.inputVolts = 2.0;
    slot.dspConfig.outputFullScaleVolts = 1.0;
    return slot;
}

dsp_artifact::BuildArtifactInput::CircuitInputSlot makeCoupledThreeModeClipperCircuit(
    double sampleRate,
    std::vector<dsp_artifact::EffectPresetSlot>& outThreeEffects)
{
    dsp_artifact::BuildArtifactInput::CircuitInputSlot slot;
    slot.slotId = "coupled_three_mode_clipper";
    slot.slotName = "Coupled 3-Mode Analog Clipper";

    circuit_sim::Circuit& c = slot.circuit;
    const auto nIn = c.addNode();
    const auto nDrive = c.addNode();
    const auto nClip = c.addNode();
    const auto nSiBranch = c.addNode();
    const auto nAsymMid = c.addNode();
    const auto nOut = c.addNode();

    circuit_sim::Waveform w;
    w.kind = circuit_sim::Waveform::Kind::Dc;
    w.offset = 0.0;
    const int vIn = c.addVoltageSource("Vin", nIn, 0, w);

    c.addResistor("R_in", nIn, nDrive, 680.0);
    c.addVariableResistor("POT_drive", nDrive, nClip, 15000.0, "drive_pot", true);

    circuit_sim::DiodeModel dmSi;
    dmSi.saturationCurrent = 1e-14;
    dmSi.emission = 1.0;

    // Mode switch A engages the clipping network into nSiBranch
    c.addSwitch("SW_clip_engage", nClip, nSiBranch, "mode_sw_clip");
    c.addDiode("D_pos", nSiBranch, 0, dmSi);
    c.addDiode("D_neg_a", 0, nAsymMid, dmSi);
    c.addDiode("D_neg_b", nAsymMid, nSiBranch, dmSi);
    // Mode switch B shorts D_neg_b for symmetrical clipping, or opens it for 2-diode asymmetrical clipping
    c.addSwitch("SW_sym_mode", nAsymMid, nSiBranch, "mode_sw_sym");
    c.addResistor("R_bleed_clip", nSiBranch, 0, 100e6);
    c.addResistor("R_bleed_asym", nAsymMid, 0, 100e6);

    c.addCapacitor("C_body", nClip, 0, 15e-9);
    c.addVariableResistor("POT_tone", nClip, nOut, 6800.0, "tone_pot", false);
    c.addCapacitor("C_tone", nOut, 0, 33e-9);
    c.addResistor("R_load", nOut, 0, 47000.0);

    slot.dspConfig.sampleRate = sampleRate;
    slot.dspConfig.audioInputElement = vIn;
    slot.dspConfig.audioOutputNode = nOut;
    slot.dspConfig.inputVolts = 2.5;
    slot.dspConfig.outputFullScaleVolts = 1.0;

    outThreeEffects.clear();
    {
        dsp_artifact::EffectPresetSlot e1;
        e1.id = "silicon_sym";
        e1.name = "Silicon Hard Clip";
        e1.description = "Symmetrical anti-parallel silicon diode clipping";
        e1.modelSlotIndex = 0;
        e1.livePortOverrides["mode_sw_clip"] = 1.0; // Closed: diodes engaged
        e1.livePortOverrides["mode_sw_sym"] = 1.0;  // Closed: symmetrical 1v1 diodes
        outThreeEffects.push_back(std::move(e1));
    }
    {
        dsp_artifact::EffectPresetSlot e2;
        e2.id = "asym_warm";
        e2.name = "Asymmetric Tube Drive";
        e2.description = "Asymmetrical 1-vs-2 diode harmonic overdrive";
        e2.modelSlotIndex = 0;
        e2.livePortOverrides["mode_sw_clip"] = 1.0; // Closed: diodes engaged
        e2.livePortOverrides["mode_sw_sym"] = 0.0;  // Open: asymmetrical 1v2 diodes
        outThreeEffects.push_back(std::move(e2));
    }
    {
        dsp_artifact::EffectPresetSlot e3;
        e3.id = "clean_filter";
        e3.name = "Dynamic Clean Preamp";
        e3.description = "Diodes lifted; pure reactive RC tone network";
        e3.modelSlotIndex = 0;
        e3.livePortOverrides["mode_sw_clip"] = 0.0; // Open: diodes lifted
        e3.livePortOverrides["mode_sw_sym"] = 0.0;
        outThreeEffects.push_back(std::move(e3));
    }

    return slot;
}

dsp_artifact::BuildArtifactInput::CircuitInputSlot makeResonantPresenceVoicingCircuit(double sampleRate)
{
    dsp_artifact::BuildArtifactInput::CircuitInputSlot slot;
    slot.slotId = "resonant_presence_voicing";
    slot.slotName = "RLC Resonant Presence Preamp";

    circuit_sim::Circuit& c = slot.circuit;
    const auto nIn = c.addNode();
    const auto nTank = c.addNode();
    const auto nInd = c.addNode();
    const auto nOut = c.addNode();

    circuit_sim::Waveform w;
    w.kind = circuit_sim::Waveform::Kind::Dc;
    w.offset = 0.0;
    const int vIn = c.addVoltageSource("Vin", nIn, 0, w);

    c.addResistor("R_source", nIn, nTank, 1200.0);
    c.addInductor("L_presence", nTank, nInd, 0.085);
    c.addCapacitor("C_presence", nInd, 0, 68e-9);
    c.addVariableResistor("POT_q_damp", nTank, nInd, 8200.0, "presence_q", false);

    circuit_sim::DiodeModel dmLed;
    dmLed.saturationCurrent = 1e-18;
    dmLed.emission = 1.85;
    c.addDiode("D_soft_a", nTank, 0, dmLed);
    c.addDiode("D_soft_b", 0, nTank, dmLed);

    c.addVariableResistor("POT_contour", nTank, nOut, 10000.0, "contour_pot", false);
    c.addCapacitor("C_air", nOut, 0, 15e-9);
    c.addResistor("R_out", nOut, 0, 68000.0);

    slot.dspConfig.sampleRate = sampleRate;
    slot.dspConfig.audioInputElement = vIn;
    slot.dspConfig.audioOutputNode = nOut;
    slot.dspConfig.inputVolts = 2.2;
    slot.dspConfig.outputFullScaleVolts = 1.0;
    return slot;
}

} // namespace vst_exporter
