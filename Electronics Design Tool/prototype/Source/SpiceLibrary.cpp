#include "SpiceLibrary.h"

namespace spice_library
{

static std::map<juce::String, ModelDef> models;

void initialize()
{
    models.clear();
#ifdef ELECTRONICS_RESEARCH_ROOT
    const auto dir = juce::File(ELECTRONICS_RESEARCH_ROOT)
        .getChildFile("prototype")
        .getChildFile("data")
        .getChildFile("models");
#else
    const auto dir = juce::File::getCurrentWorkingDirectory().getChildFile("data").getChildFile("models");
#endif

    if (!dir.exists())
        dir.createDirectory();

    for (const auto& file : dir.findChildFiles(juce::File::findFiles, false, "*"))
    {
        if (file.getFileExtension().toLowerCase() != ".lib" &&
            file.getFileExtension().toLowerCase() != ".mod" &&
            file.getFileExtension().toLowerCase() != ".txt")
            continue;

        juce::StringArray lines;
        file.readLines(lines);
        for (int i = 0; i < lines.size(); ++i)
        {
            auto line = lines[i].trim();
            if (line.startsWithIgnoreCase(".model "))
            {
                // .model 2N3904 NPN (IS=1e-14 BF=100 ...)
                auto tokens = juce::StringArray::fromTokens(line, " \t()", "\"");
                if (tokens.size() >= 3)
                {
                    ModelDef def;
                    def.name = tokens[1];
                    def.kind = tokens[2].toUpperCase();
                    def.rawText = line;

                    for (int j = 3; j < tokens.size(); ++j)
                    {
                        const auto kv = tokens[j];
                        if (kv.contains("="))
                        {
                            auto key = kv.upToFirstOccurrenceOf("=", false, false).toUpperCase();
                            auto val = kv.fromFirstOccurrenceOf("=", false, false);
                            def.parameters[key] = val;
                        }
                    }
                    models[def.name] = def;
                }
            }
            else if (line.startsWithIgnoreCase(".subckt "))
            {
                // .subckt TL072 1 2 3 4 5
                auto tokens = juce::StringArray::fromTokens(line, " \t", "\"");
                if (tokens.size() >= 2)
                {
                    ModelDef def;
                    def.name = tokens[1];
                    def.kind = "SUBCKT";
                    
                    // Accumulate lines until .ends
                    juce::String subcktText = line + "\n";
                    for (int j = i + 1; j < lines.size(); ++j)
                    {
                        subcktText += lines[j] + "\n";
                        if (lines[j].trim().startsWithIgnoreCase(".ends"))
                        {
                            i = j;
                            break;
                        }
                    }
                    def.rawText = subcktText.trimEnd();
                    models[def.name] = def;
                }
            }
        }
    }
}

const ModelDef* findModel(const juce::String& name)
{
    auto it = models.find(name);
    return it != models.end() ? &it->second : nullptr;
}

const ModelDef* findModelIgnoreCase(const juce::String& name)
{
    if (const auto* exact = findModel(name))
        return exact;
    for (const auto& [modelName, def] : models)
        if (modelName.equalsIgnoreCase(name))
            return &def;
    return nullptr;
}

std::vector<juce::String> availableModelsFor(const juce::String& kind)
{
    std::vector<juce::String> result;
    for (const auto& [name, def] : models)
    {
        if (def.kind.equalsIgnoreCase(kind))
            result.push_back(name);
    }
    return result;
}

juce::String resolveModelText(const juce::String& name)
{
    const auto* def = findModel(name);
    return def ? def->rawText : "";
}

void appendModelTextWithDependencies(const ModelDef& def, juce::StringArray& seen, juce::String& out)
{
    if (seen.contains(def.name, true))
        return;
    seen.add(def.name);
    out << def.rawText.trimEnd() << "\n";

    juce::StringArray lines;
    lines.addLines(def.rawText);
    for (const auto& line : lines)
    {
        const auto trimmed = line.trim();
        if (!trimmed.startsWithIgnoreCase("X"))
            continue;
        auto tokens = juce::StringArray::fromTokens(trimmed, " \t", "\"");
        if (tokens.size() < 2)
            continue;
        if (const auto* dep = findModelIgnoreCase(tokens[tokens.size() - 1]))
            appendModelTextWithDependencies(*dep, seen, out);
    }
}

juce::String resolveModelTextWithDependencies(const juce::String& name)
{
    const auto* def = findModelIgnoreCase(name);
    if (def == nullptr)
        return {};
    juce::StringArray seen;
    juce::String out;
    appendModelTextWithDependencies(*def, seen, out);
    return out.trimEnd();
}

}
