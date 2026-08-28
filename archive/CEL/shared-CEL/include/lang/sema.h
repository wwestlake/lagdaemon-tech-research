#pragma once

#include "lang/ast.h"
#include "lang/diagnostics.h"
#include "lang/type.h"

namespace ce::lang {

// Type-checks and validates a parsed Program in place: resolves every
// Expr::type, Stmt::resolvedType (VarDecl), and GlobalVarDecl::resolvedType;
// validates scoping, function signatures/arity, operator/assignment type
// rules, control flow (return-on-all-paths, break/continue-in-loop-only),
// and the "string literal only as an intrinsic argument" rule. Reports
// CEL20xx diagnostics into `diagnostics` -- doesn't stop at the first
// error, so a single celc invocation surfaces as many real problems as
// it can find in one pass, the same way the parser already does.
//
// `allowedDomains` (GS-Interop, default All()) is the host's capability
// profile -- see lang/type.h's IntrinsicDomain. Every intrinsic is still
// seeded into the symbol table regardless (a user function still can't
// shadow a built-in name even in a host that blocks its domain), but a
// CALL to one outside `allowedDomains` is rejected with
// DiagCode::IntrinsicNotAvailableInDomain instead of proceeding. The
// default matches every existing caller's actual behavior today --
// nothing is gated unless a host opts in.
//
// Returns true iff the program is well-typed (equivalent to
// !diagnostics.HasErrors() after the call, but lets a caller avoid
// needing to know that detail).
bool AnalyzeProgram(Program& program, DiagnosticEngine& diagnostics,
                     IntrinsicDomainSet allowedDomains = IntrinsicDomainSet::All());

} // namespace ce::lang
