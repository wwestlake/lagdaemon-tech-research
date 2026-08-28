#pragma once

#include <JuceHeader.h>

namespace ce {

// GS7: minimal syntax highlighting for CEL source in ScriptPanel's
// juce::CodeEditorComponent. Deliberately NOT a reuse of the real CEL
// lexer (Language/grammar/cel.l) -- that's a Flex-generated, reentrant
// C++ scanner producing Bison tokens for the actual compiler; this is
// just enough hand-written classification (comment/string/number/
// keyword/identifier/bracket/operator) to color the editor, matching
// the same shape juce::CPlusPlusCodeTokeniser uses for its own language.
// The keyword list below is kept in sync with grammar/cel.l's actual
// keyword tokens by hand -- there are only 11 of them (var, func, if,
// else, while, for, break, continue, return, true, false), plus CEL's
// type names (int, float, bool, vec3, entity, void, string), which
// aren't reserved words at the lexer level (ParseTypeName resolves them
// from a plain identifier) but are still worth coloring distinctly.
class CelCodeTokeniser final : public juce::CodeTokeniser {
public:
    int readNextToken(juce::CodeDocument::Iterator& source) override;
    juce::CodeEditorComponent::ColourScheme getDefaultColourScheme() override;

    enum TokenType { tokenType_error, tokenType_comment, tokenType_keyword, tokenType_type, tokenType_operator,
                      tokenType_identifier, tokenType_number, tokenType_string, tokenType_bracket,
                      tokenType_punctuation };
};

} // namespace ce
