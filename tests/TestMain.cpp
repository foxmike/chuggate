// =============================================================================
// ChugGate - Unit Test Runner Entry Point
// =============================================================================
// Console entry point that runs every registered juce::UnitTest and returns a
// non-zero process exit code if any test fails. No third-party testing
// framework is used; this relies solely on juce::UnitTest / UnitTestRunner.
// =============================================================================

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <vector>

int main()
{
    // Some JUCE unit tests touch the message thread / timers, so make sure a
    // MessageManager exists for the lifetime of the runner.
    const juce::ScopedJuceInitialiser_GUI juceInitialiser;

    juce::UnitTestRunner runner;
    // Pass non-zero to run all tests (the int arg is unused beyond being
    // truthy in this overload signature-free variant).
    runner.runAllTests();

    int totalFailures = 0;

    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        if (const auto* result = runner.getResult (i))
        {
            const auto failures = result->failures;
            const auto passes = result->passes;

            juce::Logger::writeToLog (
                juce::String { result->unitTestName }
                + " :: " + result->subcategoryName
                + "  passes=" + juce::String { passes }
                + " failures=" + juce::String { failures });

            totalFailures += failures;
        }
    }

    if (totalFailures > 0)
    {
        juce::Logger::writeToLog (
            "ChugGate unit tests FAILED (" + juce::String { totalFailures } + " failure(s)).");
        return 1;
    }

    juce::Logger::writeToLog ("ChugGate unit tests passed.");
    return 0;
}
