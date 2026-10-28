#include "TestHarness.h"

#include <juce_events/juce_events.h>

#include <chrono>
#include <cstdio>
#include <string>

// Usage: labq8_tests [name-filter]
int main (int argc, char** argv)
{
    // APVTS needs a MessageManager (timers, listeners) even headless.
    juce::ScopedJuceInitialiser_GUI juceInit;

    const std::string filter = argc > 1 ? argv[1] : "";
    int run = 0, failed = 0, checks = 0;

    for (const auto& test : labq8::test::registry())
    {
        if (! filter.empty() && test.name.find (filter) == std::string::npos)
            continue;

        labq8::test::Context ctx;
        const auto start = std::chrono::steady_clock::now();
        test.body (ctx);
        const auto ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - start).count();

        ++run;
        checks += ctx.checks;

        if (ctx.failures.empty())
        {
            std::printf ("[PASS] %s  (%d checks, %.0f ms)\n", test.name.c_str(), ctx.checks, ms);
        }
        else
        {
            ++failed;
            std::printf ("[FAIL] %s  (%zu of %d checks failed)\n", test.name.c_str(), ctx.failures.size(), ctx.checks);

            for (size_t i = 0; i < ctx.failures.size() && i < 10; ++i)
                std::printf ("       - %s\n", ctx.failures[i].c_str());

            if (ctx.failures.size() > 10)
                std::printf ("       ... %zu more\n", ctx.failures.size() - 10);
        }
    }

    std::printf ("\n%d tests, %d failed, %d checks\n", run, failed, checks);
    return (run > 0 && failed == 0) ? 0 : 1;
}
