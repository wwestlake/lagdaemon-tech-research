// Headless check that the embedded Frust compiler compiles Frust source in
// memory and the app can call the compiled code. Expected values are worked
// out by hand in the comments.

#include "FrustEngine.h"

#include <cmath>
#include <crtdbg.h>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace
{
int failures = 0;

void check(bool ok, const std::string& what, const std::string& detail = {})
{
    std::printf("%s  %s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) ++failures;
}
}

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);

    // 1. A script returns a string.
    {
        const auto r = frust_engine::runScript("pub fn run() -> String = { \"hello from Frust\" }\n");
        check(r.ok && r.output == "hello from Frust", "script returns its string", "(" + r.output + r.report() + ")");
        std::printf("      compile %.1f ms, run %.3f ms\n", r.compileMs, r.runMs);
    }

    // 2. print_line lines come before the returned value.
    {
        const auto r = frust_engine::runScript("pub fn run() -> String = {\n    print_line(\"first\");\n    \"done\"\n}\n");
        check(r.ok && r.output == "first\ndone", "print_line output, then the result", "(" + r.output + r.report() + ")");
    }

    // 3. A syntax error on the script's line 2 is reported on line 2.
    {
        const auto r = frust_engine::runScript("pub fn run() -> String = {\n    let x = ;\n    \"b\"\n}\n");
        bool line2 = false;
        for (const auto& d : r.diagnostics) if (d.line == 2) line2 = true;
        check(!r.ok && !r.diagnostics.empty() && line2, "syntax error reported on the script's line 2", "(" + r.report() + ")");
    }

    // 4. A unit with f64 functions, called directly: gain(0.5, 3) = 1.5.
    //    One-pole low-pass y += a (x - y), a = 0.5, step input 1, 3 steps:
    //    0.5, 0.75, 0.875.
    {
        frust_engine::Engine engine;
        const auto source = frust_engine::manifestLine("dsp_check", "smoke test")
                          + "pub fn gain(x: f64, g: f64) -> f64 = { x * g }\n"
                            "pub fn one_pole(y: f64, x: f64, a: f64) -> f64 = { y + a * (x - y) }\n";
        const auto r = engine.load("dsp", source);
        check(r.ok, "unit with f64 functions compiles and loads", "(" + r.report() + ")");
        using Gain = double (*)(double, double);
        using OnePole = double (*)(double, double, double);
        auto* gain = reinterpret_cast<Gain>(engine.function("dsp", "gain"));
        auto* onePole = reinterpret_cast<OnePole>(engine.function("dsp", "one_pole"));
        check(gain != nullptr && onePole != nullptr, "compiled functions are found by name");
        if (gain != nullptr)
            check(std::abs(gain(0.5, 3.0) - 1.5) < 1e-15, "gain(0.5, 3) = 1.5", "(" + std::to_string(gain(0.5, 3.0)) + ")");
        if (onePole != nullptr)
        {
            double y = 0.0;
            for (int i = 0; i < 3; ++i) y = onePole(y, 1.0, 0.5);
            check(std::abs(y - 0.875) < 1e-15, "one-pole low-pass after 3 steps = 0.875", "(" + std::to_string(y) + ")");
        }

        // 5. Loading the key again replaces the code: gain now adds 1.
        const auto v2 = frust_engine::manifestLine("dsp_check", "smoke test")
                      + "pub fn gain(x: f64, g: f64) -> f64 = { x * g + 1.0 }\n";
        const auto r2 = engine.load("dsp", v2);
        auto* gain2 = reinterpret_cast<Gain>(engine.function("dsp", "gain"));
        check(r2.ok && gain2 != nullptr && std::abs(gain2(0.5, 3.0) - 2.5) < 1e-15, "reloaded unit runs the new code: 2.5",
              "(" + r2.report() + ")");
    }

    std::printf("\n%s: %d failure(s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
