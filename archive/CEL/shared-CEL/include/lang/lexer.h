#pragma once

// FlexLexer.h's own multiple-inclusion guard is keyed on yyFlexLexer
// already being defined, not a normal #pragma once/include-guard, per
// Flex's own documented pattern for a project with more than one
// flex-generated scanner (only relevant here if a future GS milestone
// adds a second one) -- this mirrors Flex's official recommendation
// verbatim.
#if !defined(yyFlexLexer) && !defined(FlexLexerOnce)
#include <FlexLexer.h>
#endif

#include <string>

#include "lang/diagnostics.h"
#include "lang/source_location.h"
#include "parser.hpp"

namespace ce::lang {

// Wraps the Flex-generated scanner body (see cel.l's YY_DECL redefine)
// as a member function returning Parser::symbol_type directly --
// api.token.constructor lets the grammar's %token declarations double
// as symbol_type factory functions (Parser::make_IDENT(...), etc.),
// which NextToken() below calls once per matched token instead of the
// classic yylval-out-parameter style.
class Lexer final : public yyFlexLexer {
public:
    Lexer(std::istream* in, DiagnosticEngine& diagnostics);

    Parser::symbol_type NextToken();

private:
    std::string UnescapeString(const char* raw, int length) const;

    DiagnosticEngine& diagnostics_;
    Parser::location_type loc_;
};

} // namespace ce::lang
