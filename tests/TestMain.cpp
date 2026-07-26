// =============================================================================
// ChugGate - Unit Test Runner Entry Point
// =============================================================================
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

int main()
{
    const juce::ScopedJuceInitialiser_GUI juceInitialiser;

    juce::UnitTestRunner runner;
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
