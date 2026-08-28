#include "Views/CelCodeTokeniser.h"

#include <array>

namespace ce {

namespace {

bool IsIdentifierStart(juce_wchar c) { return juce::CharacterFunctions::isLetter(c) || c == '_'; }
bool IsIdentifierBody(juce_wchar c) { return juce::CharacterFunctions::isLetterOrDigit(c) || c == '_'; }
bool IsDigit(juce_wchar c) { return c >= '0' && c <= '9'; }

// See CelCodeTokeniser.h's own comment -- kept in sync with
// grammar/cel.l's keyword tokens by hand.
bool IsKeyword(const juce::String& token) {
    static const std::array<const char*, 11> kKeywords = { "var",    "func",  "if",     "else",  "while", "for",
                                                            "break",  "continue", "return", "true",  "false" };
    for (const char* kw : kKeywords) {
        if (token == kw) {
            return true;
        }
    }
    return false;
}

// CEL's built-in type names (type.h's Type enum) -- not reserved words
// at the lexer level (a variable could theoretically be named "int",
// sema would just fail to resolve it as a type where one's expected),
// but worth coloring distinctly from an ordinary identifier.
bool IsTypeName(const juce::String& token) {
    static const std::array<const char*, 7> kTypes = { "int", "float", "bool", "vec3", "entity", "void", "string" };
    for (const char* ty : kTypes) {
        if (token == ty) {
            return true;
        }
    }
    return false;
}

void SkipQuotedString(juce::CodeDocument::Iterator& source) {
    const auto quote = source.nextChar();
    for (;;) {
        const auto c = source.nextChar();
        if (c == quote || c == 0) {
            break;
        }
        if (c == '\\') {
            source.skip();
        }
    }
}

// CEL block comments don't nest (see cel.l) -- a plain "scan for the
// next */" is exactly correct, not a simplification.
void SkipBlockComment(juce::CodeDocument::Iterator& source) {
    bool lastWasStar = false;
    for (;;) {
        const auto c = source.nextChar();
        if (c == 0 || (c == '/' && lastWasStar)) {
            break;
        }
        lastWasStar = (c == '*');
    }
}

int ParseIdentifierOrKeyword(juce::CodeDocument::Iterator& source) {
    juce::String token;
    while (IsIdentifierBody(source.peekNextChar())) {
        token += source.nextChar();
    }
    if (IsKeyword(token)) {
        return CelCodeTokeniser::tokenType_keyword;
    }
    if (IsTypeName(token)) {
        return CelCodeTokeniser::tokenType_type;
    }
    return CelCodeTokeniser::tokenType_identifier;
}

// CEL numeric literals are decimal int/float only -- no hex/octal (see
// type.h/the grammar), so this is deliberately simpler than a C-family
// tokeniser's number parsing.
int ParseNumber(juce::CodeDocument::Iterator& source) {
    while (IsDigit(source.peekNextChar())) {
        source.skip();
    }
    if (source.peekNextChar() == '.') {
        source.skip();
        while (IsDigit(source.peekNextChar())) {
            source.skip();
        }
    }
    return CelCodeTokeniser::tokenType_number;
}

} // namespace

int CelCodeTokeniser::readNextToken(juce::CodeDocument::Iterator& source) {
    source.skipWhitespace();
    const auto firstChar = source.peekNextChar();

    if (firstChar == 0) {
        return tokenType_error;
    }
    if (IsDigit(firstChar)) {
        return ParseNumber(source);
    }
    if (firstChar == '.') {
        source.skip();
        return IsDigit(source.peekNextChar()) ? ParseNumber(source) : tokenType_punctuation;
    }
    if (firstChar == '"') {
        SkipQuotedString(source);
        return tokenType_string;
    }
    if (IsIdentifierStart(firstChar)) {
        return ParseIdentifierOrKeyword(source);
    }
    if (firstChar == '(' || firstChar == ')' || firstChar == '{' || firstChar == '}' || firstChar == '[' ||
        firstChar == ']') {
        source.skip();
        return tokenType_bracket;
    }
    if (firstChar == ',' || firstChar == ';' || firstChar == ':') {
        source.skip();
        return tokenType_punctuation;
    }
    if (firstChar == '/') {
        source.skip();
        const auto next = source.peekNextChar();
        if (next == '/') {
            source.skipToEndOfLine();
            return tokenType_comment;
        }
        if (next == '*') {
            source.skip();
            SkipBlockComment(source);
            return tokenType_comment;
        }
        if (next == '=') {
            source.skip();
        }
        return tokenType_operator;
    }

    // Everything else (+ - * % = ! < > & | ^ ...) is a plain
    // one-or-two-character operator -- CEL has no operator complex
    // enough to need its own case (see the grammar's binary/unary/assign
    // productions), so a single skip (plus an optional trailing '=' for
    // compound/comparison forms) covers it.
    source.skip();
    if (source.peekNextChar() == '=') {
        source.skip();
    }
    return tokenType_operator;
}

juce::CodeEditorComponent::ColourScheme CelCodeTokeniser::getDefaultColourScheme() {
    struct Entry {
        const char* name;
        juce::uint32 colour;
    };
    static const std::array<Entry, 9> kEntries = { { { "Error", 0xffcc0000 },
                                                      { "Comment", 0xff6a9955 },
                                                      { "Keyword", 0xff569cd6 },
                                                      { "Type", 0xff4ec9b0 },
                                                      { "Operator", 0xffd4d4d4 },
                                                      { "Identifier", 0xffd4d4d4 },
                                                      { "Number", 0xffb5cea8 },
                                                      { "String", 0xffce9178 },
                                                      { "Bracket", 0xffd4d4d4 } } };

    juce::CodeEditorComponent::ColourScheme scheme;
    for (const auto& entry : kEntries) {
        scheme.set(entry.name, juce::Colour(entry.colour));
    }
    return scheme;
}

} // namespace ce
