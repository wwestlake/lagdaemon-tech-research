#pragma once

#include <ostream>
#include <string>
#include <vector>

#include "lang/source_location.h"

namespace ce::lang {

enum class DiagnosticSeverity { Error, Warning };

// Numbered diagnostic codes -- CEL1xxx = lexer/parser (syntax) errors.
// Later phases add their own ranges (CEL2xxx semantic, CEL9xxx
// runtime) without renumbering these; kept as one enum rather than
// per-phase enums so a code's number stays stable even if a later
// refactor moves which phase actually detects it.
enum class DiagCode {
    // Lexer (CEL10xx)
    UnterminatedString = 1001,
    UnterminatedBlockComment = 1002,
    InvalidCharacter = 1003,
    // Parser (CEL11xx)
    SyntaxError = 1101,
    // Semantic (CEL20xx) -- GS3.
    UndefinedIdentifier = 2001,
    TypeMismatch = 2002,
    UndefinedFunction = 2003,
    NonLiteralStringArgument = 2004,
    NonBoolCondition = 2005,
    BreakOutsideLoop = 2006,
    ContinueOutsideLoop = 2007,
    ReturnValueInVoidFunction = 2008,
    MissingReturnValue = 2009,
    ReturnTypeMismatch = 2010,
    MissingReturnOnAllPaths = 2011,
    UnknownType = 2012,
    DuplicateFunction = 2013,
    DuplicateGlobal = 2014,
    DuplicateParam = 2015,
    DuplicateLocal = 2016,
    WrongArgumentCount = 2017,
    InvalidLvalue = 2018,
    InvalidOperator = 2019,
    DivisionByZero = 2020,
    NoMemberAccess = 2021,
    UnknownMember = 2022,
    IntrinsicNotAvailableInDomain = 2023, // GS-Interop: cross-app capability gating -- see lang/type.h's IntrinsicDomain.
};

struct Diagnostic {
    DiagCode code;
    DiagnosticSeverity severity;
    SourceLocation loc;
    std::string message;
};

class DiagnosticEngine {
public:
    void Report(DiagCode code, DiagnosticSeverity severity, SourceLocation loc, std::string message);

    bool HasErrors() const;
    const std::vector<Diagnostic>& Diagnostics() const { return diagnostics_; }

    void PrintAll(std::ostream& out) const;

private:
    std::vector<Diagnostic> diagnostics_;
};

} // namespace ce::lang
