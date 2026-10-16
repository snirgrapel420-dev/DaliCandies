#pragma once
// Minimal self-contained test framework (no external dependencies).
#include <juce_core/juce_core.h>
#include <functional>
#include <vector>
#include <string>
#include <cstdio>
#include <cmath>

namespace dtest
{
struct Case { std::string name; std::function<void()> fn; };
inline std::vector<Case>& registry() { static std::vector<Case> r; return r; }
inline int& failures() { static int f = 0; return f; }
inline int& checks() { static int c = 0; return c; }
inline std::string& currentName() { static std::string s; return s; }
struct Registrar { Registrar(const char* n, std::function<void()> f) { registry().push_back({ n, std::move(f) }); } };

inline void report(bool ok, const char* expr, const char* file, int line, const std::string& msg = {})
{
    ++checks();
    if (! ok)
    {
        ++failures();
        std::printf("    FAIL  %s:%d  %s %s\n", file, line, expr, msg.c_str());
    }
}
} // namespace dtest

#define DTEST(name) static void name(); static dtest::Registrar reg_##name(#name, name); static void name()
#define CHECK(cond) dtest::report((cond), #cond, __FILE__, __LINE__)
#define CHECK_MSG(cond, msg) dtest::report((cond), #cond, __FILE__, __LINE__, (msg))
