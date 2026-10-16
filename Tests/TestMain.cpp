#include "TestFramework.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <chrono>

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    std::string filter = argc > 1 ? argv[1] : "";
    int run = 0, failedCases = 0;
    const auto t0 = std::chrono::steady_clock::now();
    for (auto& c : dtest::registry())
    {
        if (! filter.empty() && c.name.find(filter) == std::string::npos) continue;
        const int before = dtest::failures();
        const auto s = std::chrono::steady_clock::now();
        std::printf("[ RUN  ] %s\n", c.name.c_str()); std::fflush(stdout);
        try { c.fn(); }
        catch (const std::exception& e) { dtest::report(false, "exception", __FILE__, __LINE__, e.what()); }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - s).count();
        const bool ok = dtest::failures() == before;
        std::printf("[ %s ] %s (%.0f ms)\n", ok ? " OK " : "FAIL", c.name.c_str(), ms);
        ++run; if (! ok) ++failedCases;
    }
    const double total = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("\n%d test cases run, %d failed, %d checks, %d failed checks (%.1f s)\n", run, failedCases, dtest::checks(), dtest::failures(), total);
    return dtest::failures() == 0 ? 0 : 1;
}
