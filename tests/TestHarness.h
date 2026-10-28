#pragma once

// Minimal self-registering test harness: no external dependency, so the test
// binary builds with nothing beyond JUCE and the standard library.

#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace labq8::test
{
struct Context
{
    int checks = 0;
    std::vector<std::string> failures;

    void check (bool condition, const std::string& message, const char* file, int line)
    {
        ++checks;

        if (! condition)
        {
            std::ostringstream os;
            os << file << ":" << line << "  " << message;
            failures.push_back (os.str());
        }
    }
};

struct TestCase
{
    std::string name;
    std::function<void (Context&)> body;
};

inline std::vector<TestCase>& registry()
{
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar
{
    Registrar (std::string name, std::function<void (Context&)> body)
    {
        registry().push_back ({ std::move (name), std::move (body) });
    }
};

template <typename... Args>
std::string str (const Args&... args)
{
    std::ostringstream os;
    os.precision (10);
    (os << ... << args);
    return os.str();
}
} // namespace labq8::test

#define LQ_CONCAT_INNER(a, b) a##b
#define LQ_CONCAT(a, b) LQ_CONCAT_INNER (a, b)

#define LQ_TEST(name)                                                                    \
    static void LQ_CONCAT (lqTestBody_, __LINE__) (labq8::test::Context&);               \
    static const labq8::test::Registrar LQ_CONCAT (lqTestRegistrar_, __LINE__) (        \
        name, LQ_CONCAT (lqTestBody_, __LINE__));                                        \
    static void LQ_CONCAT (lqTestBody_, __LINE__) ([[maybe_unused]] labq8::test::Context& ctx)

#define LQ_CHECK(condition, message) ctx.check ((condition), (message), __FILE__, __LINE__)
