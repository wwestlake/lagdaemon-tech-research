#pragma once

#include <istream>

#include "lang/ast.h"
#include "lang/diagnostics.h"

namespace ce::lang {

// Top-level entry point: parses `input` as a CEL program into `arena`,
// reporting any lexer/parser diagnostics into `diagnostics`. Returns
// nullptr on failure -- check diagnostics.HasErrors() regardless, since
// a program with only warnings (once sema exists to raise any) still
// returns non-null.
Program* ParseProgram(std::istream& input, AstArena& arena, DiagnosticEngine& diagnostics);

} // namespace ce::lang
