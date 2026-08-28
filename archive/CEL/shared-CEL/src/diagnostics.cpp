#include "lang/diagnostics.h"

namespace ce::lang {

void DiagnosticEngine::Report(DiagCode code, DiagnosticSeverity severity, SourceLocation loc, std::string message) {
    diagnostics_.push_back(Diagnostic{ code, severity, loc, std::move(message) });
}

bool DiagnosticEngine::HasErrors() const {
    for (const auto& d : diagnostics_) {
        if (d.severity == DiagnosticSeverity::Error) {
            return true;
        }
    }
    return false;
}

void DiagnosticEngine::PrintAll(std::ostream& out) const {
    for (const auto& d : diagnostics_) {
        out << (d.severity == DiagnosticSeverity::Error ? "error" : "warning") << " CEL" << static_cast<int>(d.code)
            << " at " << d.loc.ToString() << ": " << d.message << "\n";
    }
}

} // namespace ce::lang
