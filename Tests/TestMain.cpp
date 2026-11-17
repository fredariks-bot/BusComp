#include <juce_core/juce_core.h>

// Console test runner: executes every registered juce::UnitTest and exits
// non-zero if any expectation failed. Used by CI (ctest) and locally.
int main()
{
    juce::UnitTestRunner runner;
    runner.runAllTests();

    int failures = 0;
    int passes = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        const auto* result = runner.getResult (i);
        failures += result->failures;
        passes   += result->passes;
        if (result->failures > 0)
        {
            juce::String msg = "FAILED: " + result->unitTestName + " / " + result->subcategoryName + "\n";
            for (const auto& m : result->messages)
                msg += "  " + m + "\n";
            juce::Logger::writeToLog (msg);
        }
    }

    juce::Logger::writeToLog (juce::String (passes) + " expectations passed, "
                              + juce::String (failures) + " failed.");
    return failures > 0 ? 1 : 0;
}
