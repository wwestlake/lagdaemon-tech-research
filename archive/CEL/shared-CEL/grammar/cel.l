%{
#include <cstdlib>
#include <string>

#include "lang/ast.h"
#include "lang/lexer.h"
#include "parser.hpp"

#undef YY_DECL
#define YY_DECL ce::lang::Parser::symbol_type ce::lang::Lexer::NextToken()

// Advances the location's *end* position by the just-matched token's
// length before that token's own action runs -- the standard Bison C++
// lexer-integration pattern (mirrors Bison's own "calc++" example).
// loc_.step() (called at the top of the rules section, and after every
// newline) then collapses begin up to end, so the next token starts
// counting from where this one left off.
#define YY_USER_ACTION loc_.columns(yyleng);
%}

%option noyywrap nounput noinput batch never-interactive nounistd
%option c++
%option yyclass="ce::lang::Lexer"

%x BLOCK_COMMENT

DIGIT  [0-9]
IDCHAR [A-Za-z_]
ID     {IDCHAR}({IDCHAR}|{DIGIT})*

%%

%{
  loc_.step();
%}

"//"[^\n]*              { /* line comment -- ignore */ }
"/*"                    { BEGIN(BLOCK_COMMENT); }
<BLOCK_COMMENT>"*/"     { BEGIN(INITIAL); }
<BLOCK_COMMENT>\n       { loc_.lines(1); loc_.step(); }
<BLOCK_COMMENT>.        { /* consume */ }
<BLOCK_COMMENT><<EOF>>  {
    diagnostics_.Report(DiagCode::UnterminatedBlockComment, DiagnosticSeverity::Error,
                         SourceLocation{ loc_.begin.line, loc_.begin.column }, "unterminated block comment");
    BEGIN(INITIAL);
}

[ \t\r]+   { loc_.step(); }
\n         { loc_.lines(1); loc_.step(); }

"var"       { return Parser::make_VAR(loc_); }
"func"      { return Parser::make_FUNC(loc_); }
"if"        { return Parser::make_IF(loc_); }
"else"      { return Parser::make_ELSE(loc_); }
"while"     { return Parser::make_WHILE(loc_); }
"for"       { return Parser::make_FOR(loc_); }
"break"     { return Parser::make_BREAK(loc_); }
"continue"  { return Parser::make_CONTINUE(loc_); }
"return"    { return Parser::make_RETURN(loc_); }
"true"      { return Parser::make_TRUE(loc_); }
"false"     { return Parser::make_FALSE(loc_); }

{DIGIT}+"."{DIGIT}+ { return Parser::make_FLOAT_LITERAL(std::strtof(yytext, nullptr), loc_); }
{DIGIT}+            { return Parser::make_INT_LITERAL(std::strtoll(yytext, nullptr, 10), loc_); }

\"([^\"\\\n]|\\.)*\" { return Parser::make_STRING_LITERAL(UnescapeString(yytext, yyleng), loc_); }
\"([^\"\\\n]|\\.)*   {
    diagnostics_.Report(DiagCode::UnterminatedString, DiagnosticSeverity::Error,
                         SourceLocation{ loc_.begin.line, loc_.begin.column }, "unterminated string literal");
    return Parser::make_STRING_LITERAL(std::string(), loc_);
}

{ID}  { return Parser::make_IDENT(std::string(yytext, yyleng), loc_); }

"->"  { return Parser::make_ARROW(loc_); }
"=="  { return Parser::make_EQ(loc_); }
"!="  { return Parser::make_NEQ(loc_); }
"<="  { return Parser::make_LE(loc_); }
">="  { return Parser::make_GE(loc_); }
"&&"  { return Parser::make_ANDAND(loc_); }
"||"  { return Parser::make_OROR(loc_); }
"+="  { return Parser::make_PLUSEQ(loc_); }
"-="  { return Parser::make_MINUSEQ(loc_); }
"*="  { return Parser::make_STAREQ(loc_); }
"/="  { return Parser::make_SLASHEQ(loc_); }

"{"  { return Parser::make_LBRACE(loc_); }
"}"  { return Parser::make_RBRACE(loc_); }
"("  { return Parser::make_LPAREN(loc_); }
")"  { return Parser::make_RPAREN(loc_); }
","  { return Parser::make_COMMA(loc_); }
";"  { return Parser::make_SEMI(loc_); }
":"  { return Parser::make_COLON(loc_); }
"."  { return Parser::make_DOT(loc_); }
"="  { return Parser::make_ASSIGN(loc_); }
"<"  { return Parser::make_LT(loc_); }
">"  { return Parser::make_GT(loc_); }
"+"  { return Parser::make_PLUS(loc_); }
"-"  { return Parser::make_MINUS(loc_); }
"*"  { return Parser::make_STAR(loc_); }
"/"  { return Parser::make_SLASH(loc_); }
"%"  { return Parser::make_PERCENT(loc_); }
"!"  { return Parser::make_NOT(loc_); }

.  {
    diagnostics_.Report(DiagCode::InvalidCharacter, DiagnosticSeverity::Error,
                         SourceLocation{ loc_.begin.line, loc_.begin.column },
                         std::string("invalid character '") + yytext + "'");
}

<<EOF>>  { return Parser::make_YYEOF(loc_); }

%%

namespace ce::lang {

Lexer::Lexer(std::istream* in, DiagnosticEngine& diagnostics) : yyFlexLexer(in), diagnostics_(diagnostics) {}

std::string Lexer::UnescapeString(const char* raw, int length) const {
    // Strips the surrounding quotes and resolves \n \t \\ \" -- the only
    // escapes v1 needs (string literals are intrinsic-argument-only per
    // the language spec, not a general-purpose string type, so this
    // doesn't need to be exhaustive).
    std::string result;
    result.reserve(static_cast<std::size_t>(length));
    for (int i = 1; i < length - 1; ++i) {
        if (raw[i] == '\\' && i + 1 < length - 1) {
            ++i;
            switch (raw[i]) {
                case 'n': result += '\n'; break;
                case 't': result += '\t'; break;
                case '\\': result += '\\'; break;
                case '"': result += '"'; break;
                default: result += raw[i]; break;
            }
        } else {
            result += raw[i];
        }
    }
    return result;
}

} // namespace ce::lang
