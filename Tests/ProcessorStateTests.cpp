#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "../Source/PluginProcessor.h"

#include <cmath>

//==============================================================================
// Processor-level tests: real APVTS parameters, automation entry points,
// state save/restore and preset handling through BusCompProcessor.
class ProcessorStateTests final : public juce::UnitTest
{
public:
    ProcessorStateTests() : juce::UnitTest ("BusCompProcessor", "State") {}

    void runTest() override
    {
        testParameterAccess();
        testAutomationEntryPoint();
        testProcessBlock();
        testStateRoundtrip();
        testCorruptState();
        testPrograms();
        testABCompare();
        testOversamplingLatency();
    }

private:
    static juce::AudioBuffer<float> makeSine (double fs, int n, float freq, float amp)
    {
        juce::AudioBuffer<float> buf (2, n);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
                buf.setSample (ch, i, amp * std::sin (2.0 * juce::MathConstants<double>::pi * freq * i / fs));
        return buf;
    }

    void testParameterAccess()
    {
        beginTest ("Parameters are readable/writable through the APVTS");
        buscomp::BusCompProcessor proc;
        expect (std::fabs (proc.getParamValue ("threshold") - (-18.0f)) < 0.01f, "default threshold");
        proc.setParamValue ("threshold", -25.0f);
        expect (std::fabs (proc.getParamValue ("threshold") - (-25.0f)) < 0.05f);
        proc.setParamValue ("ratio", 10.0f);
        expect (std::fabs (proc.getParamValue ("ratio") - 10.0f) < 0.1f);
        // out-of-range values are clamped by the parameter range
        proc.setParamValue ("threshold", -100.0f);
        expect (proc.getParamValue ("threshold") >= -30.0f - 0.01f);
    }

    void testAutomationEntryPoint()
    {
        beginTest ("DAW automation (setValueNotifyingHost) reaches the parameter values");
        buscomp::BusCompProcessor proc;
        auto* ratio = proc.apvts.getParameter ("ratio");
        expect (ratio != nullptr);
        ratio->setValueNotifyingHost (0.0f); // normalized 0 -> 1:1
        expect (std::fabs (proc.getParamValue ("ratio") - 1.0f) < 0.05f);
        ratio->setValueNotifyingHost (1.0f); // -> 20:1
        expect (std::fabs (proc.getParamValue ("ratio") - 20.0f) < 0.2f);

        auto* os = proc.apvts.getParameter ("oversampling");
        expect (os != nullptr);
        os->setValueNotifyingHost (os->convertTo0to1 (2.0f));
        expect ((int) proc.getParamValue ("oversampling") == 2);
    }

    void testProcessBlock()
    {
        beginTest ("processBlock produces real gain reduction and finite audio");
        buscomp::BusCompProcessor proc;
        proc.prepareToPlay (44100.0, 512);
        proc.setParamValue ("threshold", -20.0f);
        proc.setParamValue ("ratio", 4.0f);
        proc.setParamValue ("attack", 1.0f);
        proc.setParamValue ("release", 0.1f);

        auto in = makeSine (44100.0, 512 * 40, 1000.0f, 0.3162f); // -10 dBFS
        juce::MidiBuffer midi;
        for (int b = 0; b < 40; ++b)
        {
            juce::AudioBuffer<float> block (2, 512);
            block.copyFrom (0, 0, in, 0, b * 512, 512);
            block.copyFrom (1, 0, in, 1, b * 512, 512);
            proc.processBlock (block, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    expect (std::isfinite (block.getSample (ch, i)));
        }
        expect (proc.getGainReductionDb() > 5.0f,
                "expected ~7.5 dB GR, meter shows " + juce::String (proc.getGainReductionDb()));
        expect (proc.getInputDb (0) > -20.0f && proc.getInputDb (0) < -5.0f,
                "input meter should show ~-10 dBFS, got " + juce::String (proc.getInputDb (0)));
        expect (proc.getOutputDb (0) < proc.getInputDb (0), "output must be quieter than input here");
        proc.releaseResources();
    }

    void testStateRoundtrip()
    {
        beginTest ("getStateInformation/setStateInformation roundtrip preserves all parameters");
        buscomp::BusCompProcessor proc;
        proc.prepareToPlay (44100.0, 512);
        proc.setParamValue ("threshold", -27.5f);
        proc.setParamValue ("ratio", 7.0f);
        proc.setParamValue ("attack", 3.0f);
        proc.setParamValue ("release", 0.8f);
        proc.setParamValue ("autoRelease", 1.0f);
        proc.setParamValue ("makeup", 5.0f);
        proc.setParamValue ("mix", 75.0f);
        proc.setParamValue ("stereoLink", 60.0f);
        proc.setParamValue ("softKnee", 1.0f);

        juce::MemoryBlock data;
        proc.getStateInformation (data);
        expect (data.getSize() > 0);

        buscomp::BusCompProcessor proc2;
        proc2.prepareToPlay (44100.0, 512);
        proc2.setStateInformation (data.getData(), (int) data.getSize());

        for (const auto& def : buscomp::parameterDefs())
            expect (std::fabs (proc2.getParamValue (def.id) - proc.getParamValue (def.id)) < 0.05f,
                    "state mismatch for " + def.id + ": " + juce::String (proc2.getParamValue (def.id))
                    + " vs " + juce::String (proc.getParamValue (def.id)));
    }

    void testCorruptState()
    {
        beginTest ("Corrupt/legacy state does not crash and keeps valid values");
        buscomp::BusCompProcessor proc;
        proc.prepareToPlay (44100.0, 512);
        proc.setParamValue ("threshold", -22.0f);

        const char garbage[] = "not xml {{{";
        proc.setStateInformation (garbage, (int) sizeof (garbage));
        expect (std::fabs (proc.getParamValue ("threshold") - (-22.0f)) < 0.05f,
                "corrupt state must be ignored");

        // partial state: only one PARAM child set -> others keep defaults
        juce::ValueTree partial ("BUSCOMP_STATE");
        juce::ValueTree paramChild ("PARAM");
        paramChild.setProperty ("id", "threshold", nullptr);
        paramChild.setProperty ("value", -12.0, nullptr);
        partial.appendChild (paramChild, nullptr);
        auto binary = buscomp::PresetManager::stateToBinary (partial);
        proc.setStateInformation (binary.getData(), (int) binary.getSize());
        expect (std::fabs (proc.getParamValue ("threshold") - (-12.0f)) < 0.05f);
        expect (std::fabs (proc.getParamValue ("ratio") - 4.0f) < 0.05f, "missing props keep defaults");
    }

    void testPrograms()
    {
        beginTest ("Programs map to the factory presets");
        buscomp::BusCompProcessor proc;
        expect (proc.getNumPrograms() == 8);
        proc.setCurrentProgram (1); // Drum Bus
        expect (proc.getCurrentProgram() == 1);
        expect (std::fabs (proc.getParamValue ("ratio") - 4.0f) < 0.05f);
        expect (proc.getProgramName (1) == "Drum Bus");
        expect (proc.getCurrentPresetName() == "Drum Bus");
    }

    void testABCompare()
    {
        beginTest ("A/B snapshot and toggle restores parameter states");
        buscomp::BusCompProcessor proc;
        proc.prepareToPlay (44100.0, 512);
        proc.setParamValue ("threshold", -20.0f);
        proc.snapshotAB();
        proc.setParamValue ("threshold", -5.0f);
        proc.toggleAB();
        expect (std::fabs (proc.getParamValue ("threshold") - (-20.0f)) < 0.05f);
        expect (proc.isABActive());
        proc.toggleAB();
        expect (std::fabs (proc.getParamValue ("threshold") - (-5.0f)) < 0.05f);
        expect (! proc.isABActive());
    }

    void testOversamplingLatency()
    {
        beginTest ("Oversampling modes report latency to the host after processing");
        buscomp::BusCompProcessor proc;
        proc.prepareToPlay (44100.0, 512);
        expect (proc.getLatencySamples() == 0, "off mode: zero latency");

        proc.setParamValue ("oversampling", 1.0f); // 2x
        juce::AudioBuffer<float> block (2, 512);
        juce::MidiBuffer midi;
        proc.processBlock (block, midi);
        expect (proc.getLatencySamples() > 0, "2x mode must report latency, got " + juce::String (proc.getLatencySamples()));

        const int lat2 = proc.getLatencySamples();
        proc.setParamValue ("oversampling", 2.0f); // 4x
        proc.processBlock (block, midi);
        expect (proc.getLatencySamples() > lat2, "4x should have more latency than 2x");

        proc.setParamValue ("oversampling", 0.0f);
        proc.processBlock (block, midi);
        expect (proc.getLatencySamples() == 0, "back to off: zero latency");
    }
};

static ProcessorStateTests processorStateTests;
