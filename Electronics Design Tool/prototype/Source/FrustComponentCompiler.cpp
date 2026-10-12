#include "FrustComponentCompiler.h"

#include "NodeDesignerPanel.h"

namespace frust_component
{

juce::File programFileFor(const Definition& definition,
                          const juce::File& definitionFile,
                          const juce::File& projectFolder)
{
    if (definition.programFile.isEmpty())
        return {};

    if (juce::File::isAbsolutePath(definition.programFile))
        return juce::File(definition.programFile);

    if (definitionFile != juce::File())
        return definitionFile.getSiblingFile(definition.programFile);

    if (projectFolder != juce::File())
        return projectFolder.getChildFile(definition.programFile);

    return juce::File(definition.programFile);
}

bool compileProgramFile(const Definition& definition,
                        const juce::File& programFile,
                        juce::String& error,
                        juce::String* source,
                        juce::StringArray* notes)
{
    if (!programFile.existsAsFile())
    {
        error = "Component " + definition.name + ": program file not found: " + programFile.getFullPathName();
        return false;
    }

    const auto definitionText = juce::JSON::toString(toVar(definition), true);
    const auto programText = programFile.loadFileAsString();
    const auto inputs = definitionText + "\n" + programText;
    const auto key = (juce::String::toHexString(inputs.hashCode64()) + ":" + juce::String(inputs.length())).toStdString();

    if (source == nullptr && Library::instance().isCompiled(definition.name, key))
        return true;

    NodeDesignerPanel compiler;
    juce::String problems;
    if (!compiler.openFile(programFile, problems))
    {
        error = "Component " + definition.name + ": its program " + programFile.getFileName()
              + " does not open: " + problems;
        return false;
    }

    compiler.setComponentContext(definition.compilerContext());
    juce::String message;
    if (!compiler.compileProgram(message))
    {
        error = "Component " + definition.name + " (program " + programFile.getFileName() + "): " + message;
        return false;
    }

    if (source != nullptr)
        *source = compiler.generatedProgramSource();

    if (notes != nullptr)
        for (const auto& p : compiler.validateGraph())
            if (p.contains("does not reach"))
                notes->add(p);

    return Library::instance().compile(definition, compiler.generatedProgramSource().toStdString(), key, error);
}

}
