#include "lang/compiler.h"

#include "lang/lexer.h"
#include "parser.hpp"

namespace ce::lang {

Program* ParseProgram(std::istream& input, AstArena& arena, DiagnosticEngine& diagnostics) {
    Lexer lexer(&input, diagnostics);
    Program* result = nullptr;
    Parser parser(lexer, arena, diagnostics, result);
    const int rc = parser.parse();
    if (rc != 0) {
        return nullptr;
    }
    return result;
}

} // namespace ce::lang
