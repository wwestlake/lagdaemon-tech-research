#include <lang/compiler.h>
#include <lang/diagnostics.h>
#include <lang/sema.h>

#include <iostream>
#include <sstream>
#include <stdexcept>

namespace
{
void fail(const std::string& message)
{
    std::cerr << message << std::endl;
    throw std::runtime_error(message);
}
}

int main()
{
    try
    {
        std::istringstream input(R"(
            var g: float = 2.5;

            func Twice(x: float) -> float {
                return x * 2.0;
            }

            func Main(self: entity, dt: float) -> float {
                var local: float = Twice(g);
                if (local > 4.0) {
                    return local;
                }
                return 0.0;
            }
        )");

        ce::lang::AstArena arena;
        ce::lang::DiagnosticEngine diagnostics;
        auto* program = ce::lang::ParseProgram(input, arena, diagnostics);
        if (program == nullptr)
            fail("ParseProgram returned null.");

        if (diagnostics.HasErrors())
            fail("ParseProgram produced unexpected diagnostics.");

        if (! ce::lang::AnalyzeProgram(*program, diagnostics))
            fail("AnalyzeProgram failed on valid CEL source.");

        if (diagnostics.HasErrors())
            fail("AnalyzeProgram produced unexpected diagnostics.");

        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "CelFrontendSmoke failure: " << exception.what() << std::endl;
        return 1;
    }
}
