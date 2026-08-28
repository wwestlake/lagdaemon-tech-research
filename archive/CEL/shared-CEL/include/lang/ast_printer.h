#pragma once

#include <ostream>

#include "lang/ast.h"

namespace ce::lang {

// Pretty-prints a parsed Program as an indented tree -- celc --dump-ast's
// output, and what Language/tests/parse/*.cel's .expected fixtures are
// diffed against. Deterministic (no pointer addresses, nothing
// non-reproducible), so it's safe to commit as test fixtures.
void PrintAst(const Program& program, std::ostream& out);

} // namespace ce::lang
