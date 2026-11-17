#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>

#include "../Source/CompressorDSP.h"
#include "../Source/SidechainFilter.h"

#include <cmath>
#include <random>

namespace
{

constexpr double pi = juce::MathConstants<double>::pi;

juce::AudioBuffer<float> makeSine (double fs, int numSamples, int numChannels, float freq, float ampL, float ampR = -1.0f)
{
    if (ampR < 0.0f) ampR = ampL;
    juce::AudioBuffer<float> buf (numChannels, numSamples);
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float amp = ch == 0 ? ampL : ampR;
        auto* d = buf.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
            d[i] = amp * std::sin ((float) (2.0 * pi * freq * i / fs));
    }
    return buf;
}

juce::AudioBuffer<float> makeConstant (int numSamples, int numChannels, float valueL, float valueR = -999.0f)
{
    if (valueR == -999.0f) valueR = valueL;
    juce::AudioBuffer<float> buf (numChannels, numSamples);
    for (int ch = 0; ch < numChannels; ++ch)
        for (int i = 0; i < numSamples; ++i)
            buf.setSample (ch, i, ch == 0 ? valueL : valueR);
    return buf;
}

// processes `in` through the engine in small blocks and returns the output;
// maxGr (if given) receives the maximum raw gain reduction seen while processing
void processInBlocks (buscomp::CompressorDSP& dsp, const juce::AudioBuffer<float>& in,
                      juce::AudioBuffer<float>& out, int blockSize, float* maxGr = nullptr)
{
    out.clear();
    juce::AudioBuffer<float> block (in.getNumChannels(), blockSize);
    for (int off = 0; off < in.getNumSamples(); off += blockSize)
    {
        const int n = juce::jmin (blockSize, in.getNumSamples() - off);
        block.clear();
        for (int ch = 0; ch < in.getNumChannels(); ++ch)
            block.copyFrom (ch, 0, in, ch, off, n);
        dsp.process (block);
        if (maxGr != nullptr)
            *maxGr = std::max (*maxGr, dsp.getGainReductionDbRaw());
        for (int ch = 0; ch < in.getNumChannels(); ++ch)
            out.copyFrom (ch, off, block, ch, 0, n);
    }
}

float peakAmp (const juce::AudioBuffer<float>& buf, int ch, int startSample)
{
    float peak = 0.0f;
    const auto* d = buf.getReadPointer (ch);
    for (int i = startSample; i < buf.getNumSamples(); ++i)
        peak = std::max (peak, std::fabs (d[i]));
    return peak;
}

float rmsDb (const juce::AudioBuffer<float>& buf, int ch, int startSample)
{
    double sum = 0.0;
    const auto* d = buf.getReadPointer (ch);
    const int n = buf.getNumSamples() - startSample;
    for (int i = startSample; i < buf.getNumSamples(); ++i)
        sum += (double) d[i] * d[i];
    return (float) (10.0 * std::log10 (sum / juce::jmax (1, n) + 1.0e-20));
}

bool allFinite (const juce::AudioBuffer<float>& buf)
{
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        for (int i = 0; i < buf.getNumSamples(); ++i)
            if (! std::isfinite (buf.getSample (ch, i)))
                return false;
    return true;
}

float dbToAmp (float db) { return std::pow (10.0f, db / 20.0f); }
float ampToDb (float amp) { return 20.0f * std::log10 (amp + 1.0e-12f); }

buscomp::CompressorDSP::Settings baseSettings()
{
    buscomp::CompressorDSP::Settings s;
    s.thresholdDb = -20.0f;
    s.ratio = 4.0f;
    s.attackMs = 1.0f;
    s.releaseSec = 0.1f;
    s.autoRelease = false;
    s.makeupDb = 0.0f;
    s.autoGain = false;
    s.mix = 1.0f;
    s.stereoLink = 1.0f;
    s.scHpfHz = 0.0f;
    s.softKnee = false;
    s.oversampling = 0;
    return s;
}

} // namespace

//==============================================================================
class CompressorDSPTests final : public juce::UnitTest
{
public:
    CompressorDSPTests() : juce::UnitTest ("CompressorDSP", "DSP") {}

    void runTest() override
    {
        testGainComputer();
        testBelowThreshold();
        testAtThreshold();
        testRatios();
        testAttackTiming();
        testFixedReleaseTiming();
        testAutoReleaseIsSignalDependent();
        testSoftKnee();
        testStereoLink();
        testIdenticalStereo();
        testMono();
        testSidechainHpf();
        testSidechainFilterResponse();
        testMix();
        testMakeup();
        testAutoGain();
        testOversampling();
        testSampleRates();
        testSilenceAndNoise();
        testParameterRamp();
    }

private:
    void testGainComputer()
    {
        beginTest ("Gain computer: hard knee math");
        using buscomp::CompressorDSP;
        // below threshold: no reduction
        expect (CompressorDSP::gainComputer (-40.0f, -20.0f, 4.0f, false) == 0.0f);
        // exactly at threshold: no reduction
        expect (std::fabs (CompressorDSP::gainComputer (-20.0f, -20.0f, 4.0f, false)) < 1.0e-6f);
        // above threshold: out = T + (in - T)/R  =>  GR = (in - T) * (1 - 1/R)
        expect (std::fabs (CompressorDSP::gainComputer (-10.0f, -20.0f, 2.0f, false) - 5.0f) < 1.0e-4f);
        expect (std::fabs (CompressorDSP::gainComputer (-10.0f, -20.0f, 4.0f, false) - 7.5f) < 1.0e-4f);
        expect (std::fabs (CompressorDSP::gainComputer (-10.0f, -20.0f, 10.0f, false) - 9.0f) < 1.0e-4f);
        // ratio 1:1 = no compression
        expect (CompressorDSP::gainComputer (-5.0f, -20.0f, 1.0f, false) == 0.0f);

        beginTest ("Gain computer: soft knee continuity and values");
        // knee width 6 dB: at over = -3 -> 0, over = +3 -> 3*slope, over = 0 -> 0.75*slope
        expect (std::fabs (CompressorDSP::gainComputer (-23.0f, -20.0f, 4.0f, true)) < 1.0e-6f);
        expect (std::fabs (CompressorDSP::gainComputer (-17.0f, -20.0f, 4.0f, true) - 2.25f) < 1.0e-4f);
        expect (std::fabs (CompressorDSP::gainComputer (-20.0f, -20.0f, 4.0f, true) - 0.5625f) < 1.0e-4f);
        // continuity: no jumps larger than 0.01 dB across the knee edges
        bool continuous = true;
        for (float over = -4.0f; over <= 4.0f; over += 0.001f)
        {
            const float a = CompressorDSP::gainComputer (-20.0f + over, -20.0f, 4.0f, true);
            const float b = CompressorDSP::gainComputer (-20.0f + over + 0.001f, -20.0f, 4.0f, true);
            if (std::fabs (a - b) > 0.01f)
                continuous = false;
        }
        expect (continuous, "soft knee must be continuous");
    }

    void testBelowThreshold()
    {
        beginTest ("Signal below threshold is untouched");
        const double fs = 44100.0;
        buscomp::CompressorDSP dsp;
        dsp.prepare (fs, 4096, 2);
        auto s = baseSettings();
        s.thresholdDb = -30.0f;
        dsp.setSettings (s);

        auto in = makeSine (fs, 22050, 2, 1000.0f, dbToAmp (-40.0f));
        juce::AudioBuffer<float> out (2, 22050);
        processInBlocks (dsp, in, out, 512);

        expect (dsp.getGainReductionDbRaw() < 0.05f, "no gain reduction below threshold");
        float maxDiff = 0.0f;
        for (int i = 0; i < out.getNumSamples(); ++i)
            maxDiff = std::max (maxDiff, std::fabs (out.getSample (0, i) - in.getSample (0, i)));
        expect (maxDiff < 1.0e-4f, "output must equal input below threshold");
    }

    void testAtThreshold()
    {
        beginTest ("Signal exactly at threshold: no reduction (hard knee)");
        const double fs = 44100.0;
        buscomp::CompressorDSP dsp;
        dsp.prepare (fs, 4096, 2);
        auto s = baseSettings();
        s.thresholdDb = -30.0f;
        dsp.setSettings (s);

        auto in = makeSine (fs, 22050, 2, 1000.0f, dbToAmp (-30.0f));
        juce::AudioBuffer<float> out (2, 22050);
        processInBlocks (dsp, in, out, 512);
        expect (std::fabs (dsp.getGainReductionDbRaw()) < 0.15f,
                "GR at threshold should be ~0 dB, got " + juce::String (dsp.getGainReductionDbRaw()));
    }

    void testRatios()
    {
        beginTest ("Ratios 2:1, 4:1, 10:1 produce correct steady-state gain reduction");
        const double fs = 44100.0;
        for (float ratio : { 2.0f, 4.0f, 10.0f })
        {
            buscomp::CompressorDSP dsp;
            dsp.prepare (fs, 4096, 2);
            auto s = baseSettings();
            s.thresholdDb = -20.0f;
            s.ratio = ratio;
            s.attackMs = 1.0f;
            dsp.setSettings (s);

            dsp.reset(); // snap smoothed params to the new settings
            // constant -10 dBFS signal -> constant detector level -> exact GR
            auto in = makeConstant (22050, 2, dbToAmp (-10.0f));
            juce::AudioBuffer<float> out (2, 22050);
            float maxGr = 0.0f;
            processInBlocks (dsp, in, out, 512, &maxGr);

            const float expectedGr = 10.0f * (1.0f - 1.0f / ratio); // (in - T) * (1 - 1/R)
            const float gr = maxGr;
            expect (std::fabs (gr - expectedGr) < 0.4f,
                    "ratio " + juce::String (ratio) + ": expected GR " + juce::String (expectedGr)
                    + " dB, got " + juce::String (gr));

            const float outDb = ampToDb (peakAmp (out, 0, 11025));
            expect (std::fabs (outDb - (-10.0f - expectedGr)) < 0.6f,
                    "output level mismatch: " + juce::String (outDb));
        }
    }

    void testAttackTiming()
    {
        beginTest ("Attack time controls the envelope rise (sample-rate independent one-pole)");
        const double fs = 44100.0;
        for (float attackMs : { 1.0f, 30.0f })
        {
            buscomp::CompressorDSP dsp;
            dsp.prepare (fs, 4096, 2);
            auto s = baseSettings();
            s.thresholdDb = -20.0f;
            s.ratio = 4.0f;
            s.attackMs = attackMs;
            dsp.setSettings (s);
            dsp.reset(); // snap smoothed attack time to the test value

            // constant signal (DC-like) at -10 dBFS, processed one sample at a time
            const float finalGr = 7.5f;
            int samplesTo90 = -1;
            juce::AudioBuffer<float> one (2, 1);
            for (int i = 0; i < (int) (fs * 0.5); ++i)
            {
                one.setSample (0, 0, dbToAmp (-10.0f));
                one.setSample (1, 0, dbToAmp (-10.0f));
                dsp.process (one);
                if (samplesTo90 < 0 && dsp.getGainReductionDbRaw() >= 0.9f * finalGr)
                {
                    samplesTo90 = i + 1;
                    break;
                }
            }
            // one-pole: t90 = -ln(0.1) * tau = 2.303 * attack
            const double expectedSamples = 2.303 * attackMs * 0.001 * fs;
            expect (samplesTo90 > 0, "attack never reached 90%");
            expect (std::fabs (samplesTo90 - expectedSamples) < 0.6 * expectedSamples,
                    "attack " + juce::String (attackMs) + " ms: t90 = " + juce::String (samplesTo90)
                    + " samples, expected ~" + juce::String ((int) expectedSamples));
        }
    }

    void testFixedReleaseTiming()
    {
        beginTest ("Fixed release times: 100 ms recovers much faster than 1200 ms");
        const double fs = 44100.0;
        float t10[2] = { -1.0f, -1.0f };
        const float releases[2] = { 0.1f, 1.2f };
        for (int k = 0; k < 2; ++k)
        {
            buscomp::CompressorDSP dsp;
            dsp.prepare (fs, 4096, 2);
            auto s = baseSettings();
            s.thresholdDb = -20.0f;
            s.ratio = 4.0f;
            s.attackMs = 1.0f;
            s.releaseSec = releases[k];
            s.autoRelease = false;
            dsp.setSettings (s);

            const int loudSamples = (int) (fs * 0.5);
            const int tailSamples = (int) (fs * 3.5);
            juce::AudioBuffer<float> one (2, 1);
            float grAtStop = 0.0f;
            for (int i = 0; i < loudSamples + tailSamples; ++i)
            {
                const float v = i < loudSamples ? dbToAmp (-10.0f) : 0.0f;
                one.setSample (0, 0, v);
                one.setSample (1, 0, v);
                dsp.process (one);
                if (i == loudSamples - 1)
                    grAtStop = dsp.getGainReductionDbRaw();
                if (i >= loudSamples && t10[k] < 0.0f && dsp.getGainReductionDbRaw() <= 0.1f * grAtStop)
                    t10[k] = (float) (i - loudSamples + 1) / (float) fs;
            }
            expect (grAtStop > 6.0f, "expected ~7.5 dB GR during loud part, got " + juce::String (grAtStop));
            expect (t10[k] > 0.0f, "release never fell to 10%");
            // one-pole: t10 = -ln(0.1) * release
            const double expected = 2.303 * releases[k];
            expect (std::fabs (t10[k] - expected) < 0.45 * expected,
                    "release " + juce::String (releases[k]) + " s: t10 = " + juce::String (t10[k])
                    + " s, expected ~" + juce::String (expected));
        }
        expect (t10[0] < t10[1] * 0.25f, "100 ms release must be much faster than 1200 ms");
    }

    void testAutoReleaseIsSignalDependent()
    {
        beginTest ("Auto release is program-dependent (short transient vs. sustained signal)");
        const double fs = 44100.0;

        auto measureFall = [&] (int loudSamples) -> float
        {
            buscomp::CompressorDSP dsp;
            dsp.prepare (fs, 4096, 2);
            auto s = baseSettings();
            s.thresholdDb = -20.0f;
            s.ratio = 10.0f;
            s.attackMs = 1.0f;
            s.autoRelease = true;
            dsp.setSettings (s);

            const int tailSamples = (int) (fs * 4.0);
            juce::AudioBuffer<float> one (2, 1);
            for (int i = 0; i < loudSamples + tailSamples; ++i)
            {
                const float v = i < loudSamples ? dbToAmp (-6.0f) : 0.0f;
                one.setSample (0, 0, v);
                one.setSample (1, 0, v);
                dsp.process (one);
                if (i >= loudSamples && dsp.getGainReductionDbRaw() < 1.0f)
                    return (float) (i - loudSamples + 1) / (float) fs;
            }
            return -1.0f;
        };

        const float tShort = measureFall ((int) (fs * 0.05));  // 50 ms transient
        const float tLong  = measureFall ((int) (fs * 1.5));   // 1.5 s sustained

        expect (tShort > 0.0f, "short transient: GR never fell below 1 dB");
        expect (tLong > 0.0f, "sustained signal: GR never fell below 1 dB");
        expect (tShort < 0.9f, "short transient should recover quickly, took " + juce::String (tShort) + " s");
        expect (tLong > 0.9f, "sustained signal should recover slowly, took " + juce::String (tLong) + " s");
        expect (tLong > tShort * 1.5f,
                "auto release must be signal-dependent: short=" + juce::String (tShort)
                + " s, sustained=" + juce::String (tLong) + " s");
    }

    void testSoftKnee()
    {
        beginTest ("Soft knee via DSP: levels inside the knee get partial reduction");
        const double fs = 44100.0;
        struct Case { float inDb; float expectedGr; };
        const Case cases[] = { { -23.0f, 0.0f }, { -20.0f, 0.5625f }, { -17.0f, 2.25f }, { -10.0f, 7.5f } };
        for (const auto& c : cases)
        {
            buscomp::CompressorDSP dsp;
            dsp.prepare (fs, 4096, 2);
            auto s = baseSettings();
            s.thresholdDb = -20.0f;
            s.ratio = 4.0f;
            s.softKnee = true;
            dsp.setSettings (s);
            dsp.reset();
            auto in = makeConstant (22050, 2, dbToAmp (c.inDb));
            juce::AudioBuffer<float> out (2, 22050);
            float maxGr = 0.0f;
            processInBlocks (dsp, in, out, 512, &maxGr);
            const float gr = maxGr;
            expect (std::fabs (gr - c.expectedGr) < 0.4f,
                    "soft knee at " + juce::String (c.inDb) + " dBFS: expected " + juce::String (c.expectedGr)
                    + " dB, got " + juce::String (gr));
        }
    }

    void testStereoLink()
    {
        beginTest ("Stereo link: 100% couples both channels, 0% independent, 50% blended");
        const double fs = 44100.0;
        const float linkValues[3] = { 1.0f, 0.0f, 0.5f };
        for (float link : linkValues)
        {
            buscomp::CompressorDSP dsp;
            dsp.prepare (fs, 4096, 2);
            auto s = baseSettings();
            s.thresholdDb = -20.0f;
            s.ratio = 4.0f;
            s.attackMs = 1.0f;
            s.stereoLink = link;
            dsp.setSettings (s);

            // L = -10 dBFS (above threshold), R = -40 dBFS (below threshold)
            auto in = makeSine (fs, 22050, 2, 1000.0f, dbToAmp (-10.0f), dbToAmp (-40.0f));
            juce::AudioBuffer<float> out (2, 22050);
            processInBlocks (dsp, in, out, 512);

            const int start = 11025;
            const float grL = ampToDb (peakAmp (in, 0, start)) - ampToDb (peakAmp (out, 0, start));
            const float grR = ampToDb (peakAmp (in, 1, start)) - ampToDb (peakAmp (out, 1, start));

            const float grLinked = 7.5f; // from the loud channel
            if (link == 1.0f)
            {
                expect (std::fabs (grL - grLinked) < 0.5f, "link 100%: L GR " + juce::String (grL));
                expect (std::fabs (grR - grLinked) < 0.5f,
                        "link 100%: R must get the same GR as L (stable centre), got " + juce::String (grR));
            }
            else if (link == 0.0f)
            {
                expect (std::fabs (grL - grLinked) < 0.5f, "link 0%: L GR " + juce::String (grL));
                expect (grR < 0.2f, "link 0%: quiet channel must stay untouched, GR " + juce::String (grR));
            }
            else
            {
                expect (std::fabs (grR - 0.5f * grLinked) < 0.6f,
                        "link 50%: R GR should be blended, got " + juce::String (grR));
            }
        }
    }

    void testIdenticalStereo()
    {
        beginTest ("Identical L/R signals stay identical (no centre shift)");
        const double fs = 44100.0;
        buscomp::CompressorDSP dsp;
        dsp.prepare (fs, 4096, 2);
        auto s = baseSettings();
        s.thresholdDb = -15.0f;
        s.ratio = 10.0f;
        s.attackMs = 3.0f;
        s.stereoLink = 1.0f;
        dsp.setSettings (s);

        auto in = makeSine (fs, 22050, 2, 500.0f, dbToAmp (-8.0f));
        juce::AudioBuffer<float> out (2, 22050);
        processInBlocks (dsp, in, out, 512);

        float maxDiff = 0.0f;
        for (int i = 0; i < out.getNumSamples(); ++i)
            maxDiff = std::max (maxDiff, std::fabs (out.getSample (0, i) - out.getSample (1, i)));
        expect (maxDiff < 1.0e-6f, "L/R must be bit-identical, diff=" + juce::String (maxDiff));
    }

    void testMono()
    {
        beginTest ("Mono (1-channel) processing works");
        const double fs = 44100.0;
        buscomp::CompressorDSP dsp;
        dsp.prepare (fs, 4096, 1);
        auto s = baseSettings();
        s.thresholdDb = -20.0f;
        s.ratio = 4.0f;
        dsp.setSettings (s);
        dsp.reset();

        auto in = makeConstant (22050, 1, dbToAmp (-10.0f));
        juce::AudioBuffer<float> out (1, 22050);
        float maxGr = 0.0f;
        processInBlocks (dsp, in, out, 512, &maxGr);
        expect (std::fabs (maxGr - 7.5f) < 0.4f);
        expect (allFinite (out));
    }

    void testSidechainHpf()
    {
        beginTest ("Sidechain HPF filters only the detector, not the audio path");
        const double fs = 44100.0;

        // 50 Hz sine at -6 dBFS: without HPF it triggers heavy compression,
        // with a 200 Hz HPF in the detector it must not trigger at all.
        for (bool hpfOn : { false, true })
        {
            buscomp::CompressorDSP dsp;
            dsp.prepare (fs, 4096, 2);
            auto s = baseSettings();
            s.thresholdDb = -20.0f;
            s.ratio = 10.0f;
            s.scHpfHz = hpfOn ? 200.0f : 0.0f;
            dsp.setSettings (s);
            dsp.reset();

            auto in = makeSine (fs, 22050, 2, 50.0f, dbToAmp (-6.0f));
            juce::AudioBuffer<float> out (2, 22050);
            float maxGr = 0.0f;
            processInBlocks (dsp, in, out, 512, &maxGr);

            if (! hpfOn)
            {
                expect (maxGr > 10.0f, "without HPF the 50 Hz signal should trigger ~12.6 dB, got " + juce::String (maxGr));
            }
            else
            {
                expect (maxGr < 0.5f, "with 200 Hz HPF the 50 Hz signal must not trigger, got " + juce::String (maxGr));
                // audio path is untouched by the sidechain filter: output ~= input
                const float inDb = ampToDb (peakAmp (in, 0, 11025));
                const float outDb = ampToDb (peakAmp (out, 0, 11025));
                expect (std::fabs (inDb - outDb) < 0.6f,
                        "HPF must not filter the audible signal: in " + juce::String (inDb)
                        + " dB, out " + juce::String (outDb) + " dB");
            }
        }
    }

    void testSidechainFilterResponse()
    {
        beginTest ("Sidechain filter frequency response (4th-order highpass)");
        const double fs = 48000.0;
        const float fc = 200.0f;

        auto measureGainAt = [&] (float freq) -> float
        {
            buscomp::SidechainFilter filter;
            filter.prepare (fs, 1);
            filter.setFrequency (fc);
            const int n = 48000;
            juce::AudioBuffer<float> buf = makeSine (fs, n, 1, freq, 0.5f);
            // skip filter transient
            juce::AudioBuffer<float> head (1, 4800);
            head.copyFrom (0, 0, buf, 0, 0, 4800);
            filter.processBlock (head);
            juce::AudioBuffer<float> tail (1, n - 4800);
            tail.copyFrom (0, 0, buf, 0, 4800, n - 4800);
            filter.processBlock (tail);
            return ampToDb (peakAmp (tail, 0, 0) / 0.5f);
        };

        const float atFc   = measureGainAt (fc);
        const float at2Fc  = measureGainAt (2.0f * fc);
        const float atHalf = measureGainAt (0.5f * fc);
        expect (std::fabs (atFc - (-6.0f)) < 1.0f, "at fc expect ~-6 dB, got " + juce::String (atFc));
        expect (std::fabs (at2Fc) < 0.8f, "at 2*fc expect ~0 dB, got " + juce::String (at2Fc));
        expect (atHalf < -20.0f, "at 0.5*fc expect ~-24 dB, got " + juce::String (atHalf));
    }

    void testMix()
    {
        beginTest ("Parallel mix: 0% dry, 100% wet, 50% blend");
        const double fs = 44100.0;
        const float mixes[3] = { 0.0f, 1.0f, 0.5f };
        for (float mix : mixes)
        {
            buscomp::CompressorDSP dsp;
            dsp.prepare (fs, 4096, 2);
            auto s = baseSettings();
            s.thresholdDb = -20.0f;
            s.ratio = 10.0f;
            s.attackMs = 1.0f;
            s.mix = mix;
            dsp.setSettings (s);

            auto in = makeSine (fs, 22050, 2, 1000.0f, dbToAmp (-10.0f));
            juce::AudioBuffer<float> out (2, 22050);
            processInBlocks (dsp, in, out, 512);

            const int start = 11025;
            const float inDb = ampToDb (peakAmp (in, 0, start));
            const float outDb = ampToDb (peakAmp (out, 0, start));
            const float wetDb = -10.0f - 9.0f; // -19 dBFS with 9 dB GR

            if (mix == 0.0f)
            {
                float maxDiff = 0.0f;
                for (int i = start; i < out.getNumSamples(); ++i)
                    maxDiff = std::max (maxDiff, std::fabs (out.getSample (0, i) - in.getSample (0, i)));
                expect (maxDiff < 1.0e-5f, "mix 0% must pass the dry signal untouched");
            }
            else if (mix == 1.0f)
            {
                expect (std::fabs (outDb - wetDb) < 0.7f, "mix 100% must be fully wet: " + juce::String (outDb));
            }
            else
            {
                const float expectedAmp = 0.5f * (dbToAmp (inDb) + dbToAmp (wetDb));
                expect (std::fabs (ampToDb (peakAmp (out, 0, start)) - ampToDb (expectedAmp)) < 0.7f,
                        "mix 50% must be a linear blend");
            }
        }
    }

    void testMakeup()
    {
        beginTest ("Manual makeup gain applies the set amount");
        const double fs = 44100.0;
        buscomp::CompressorDSP dsp;
        dsp.prepare (fs, 4096, 2);
        auto s = baseSettings();
        s.thresholdDb = -30.0f; // no compression for a -40 dBFS signal
        s.makeupDb = 6.0f;
        dsp.setSettings (s);

        auto in = makeSine (fs, 22050, 2, 1000.0f, dbToAmp (-40.0f));
        juce::AudioBuffer<float> out (2, 22050);
        processInBlocks (dsp, in, out, 512);

        const float outDb = ampToDb (peakAmp (out, 0, 11025));
        expect (std::fabs (outDb - (-34.0f)) < 0.3f, "+6 dB makeup expected, out = " + juce::String (outDb));
    }

    void testAutoGain()
    {
        beginTest ("Auto gain compensates the measured level difference; silent input gets no boost");
        const double fs = 44100.0;

        auto run = [&] (bool autoGainOn, bool silence) -> float
        {
            buscomp::CompressorDSP dsp;
            dsp.prepare (fs, 4096, 2);
            auto s = baseSettings();
            s.thresholdDb = -20.0f;
            s.ratio = 10.0f;
            s.attackMs = 1.0f;
            s.autoGain = autoGainOn;
            dsp.setSettings (s);
            dsp.reset();

            const int total = (int) (fs * 3.0); // 3 s so the 1 s auto-gain smoothing settles
            juce::AudioBuffer<float> in (2, total);
            in.clear();
            if (! silence)
            {
                auto sine = makeSine (fs, total, 2, 1000.0f, dbToAmp (-10.0f));
                in.copyFrom (0, 0, sine, 0, 0, total);
                in.copyFrom (1, 0, sine, 1, 0, total);
            }
            juce::AudioBuffer<float> out (2, total);
            processInBlocks (dsp, in, out, 512);
            if (silence)
                return peakAmp (out, 0, 0);
            return rmsDb (out, 0, total - 44100) - rmsDb (in, 0, total - 44100); // last second
        };

        const float withAuto = run (true, false);
        const float withoutAuto = run (false, false);
        expect (std::fabs (withAuto) < 2.5f,
                "auto gain should level-match output to input (delta " + juce::String (withAuto) + " dB)");
        expect (withoutAuto < -6.0f,
                "without auto gain the 9 dB compression must remain (delta " + juce::String (withoutAuto) + " dB)");
        const float silenceOut = run (true, true);
        expect (silenceOut < 1.0e-5f, "silence must not be boosted, peak=" + juce::String (silenceOut));
    }

    void testOversampling()
    {
        beginTest ("Oversampling off/2x/4x: correct gain, finite output, latency reported");
        const double fs = 44100.0;
        float grPerMode[3] = { 0, 0, 0 };
        float outDbPerMode[3] = { 0, 0, 0 };

        for (int mode = 0; mode < 3; ++mode)
        {
            buscomp::CompressorDSP dsp;
            dsp.prepare (fs, 4096, 2);
            dsp.setOversamplingMode (mode);
            auto s = baseSettings();
            s.thresholdDb = -20.0f;
            s.ratio = 4.0f;
            s.oversampling = mode;
            dsp.setSettings (s);
            dsp.reset();

            auto in = makeConstant (22050, 2, dbToAmp (-10.0f));
            juce::AudioBuffer<float> out (2, 22050);
            float maxGr = 0.0f;
            processInBlocks (dsp, in, out, 512, &maxGr);

            grPerMode[mode] = maxGr;
            outDbPerMode[mode] = ampToDb (peakAmp (out, 0, 11025));
            expect (allFinite (out), "mode " + juce::String (mode) + ": output must be finite");
            expect (std::fabs (grPerMode[mode] - 7.5f) < 0.8f,
                    "mode " + juce::String (mode) + ": GR should be ~7.5 dB, got " + juce::String (grPerMode[mode]));

            const int latency = dsp.getLatencySamplesForMode (mode);
            if (mode == 0)
                expect (latency == 0, "off mode must be latency-free");
            else
                expect (latency > 0, "oversampling mode must report latency");
        }
        for (int mode = 1; mode < 3; ++mode)
            expect (std::fabs (outDbPerMode[mode] - outDbPerMode[0]) < 1.5f,
                    "oversampled output level should match the off mode");

        beginTest ("Switching oversampling mode during processing is click/NaN-free");
        buscomp::CompressorDSP dsp;
        dsp.prepare (fs, 4096, 2);
        auto s = baseSettings();
        s.thresholdDb = -20.0f;
        s.ratio = 4.0f;
        dsp.setSettings (s);
        dsp.setOversamplingMode (0);

        auto in = makeSine (fs, 44100, 2, 1000.0f, dbToAmp (-10.0f));
        juce::AudioBuffer<float> out (2, 44100);
        processInBlocks (dsp, in, out, 512); // warm up in off mode
        dsp.setOversamplingMode (2);          // switch to 4x mid-stream
        juce::AudioBuffer<float> rest (2, 22050);
        rest.copyFrom (0, 0, in, 0, 22050, 22050);
        rest.copyFrom (1, 0, in, 1, 22050, 22050);
        juce::AudioBuffer<float> restOut (2, 22050);
        processInBlocks (dsp, rest, restOut, 512);
        expect (allFinite (restOut), "no NaN/Inf after switching oversampling mode");
        expect (std::fabs (dsp.getGainReductionDbRaw() - 7.5f) < 1.0f,
                "GR must recover after mode switch, got " + juce::String (dsp.getGainReductionDbRaw()));
    }

    void testSampleRates()
    {
        beginTest ("Sample rates 44.1k - 192k all produce correct, finite results");
        for (double fs : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
        {
            buscomp::CompressorDSP dsp;
            dsp.prepare (fs, 4096, 2);
            auto s = baseSettings();
            s.thresholdDb = -20.0f;
            s.ratio = 4.0f;
            dsp.setSettings (s);
            dsp.reset();
            const int n = (int) (fs * 0.5);
            auto in = makeConstant (n, 2, dbToAmp (-10.0f));
            juce::AudioBuffer<float> out (2, n);
            float maxGr = 0.0f;
            processInBlocks (dsp, in, out, 512, &maxGr);
            expect (allFinite (out), "fs=" + juce::String (fs) + ": finite output");
            expect (std::fabs (maxGr - 7.5f) < 0.5f,
                    "fs=" + juce::String (fs) + ": GR " + juce::String (maxGr));
        }
    }

    void testSilenceAndNoise()
    {
        beginTest ("Silence in = silence out, GR 0");
        buscomp::CompressorDSP dsp;
        dsp.prepare (44100.0, 4096, 2);
        auto s = baseSettings();
        s.thresholdDb = -30.0f;
        s.ratio = 20.0f;
        s.makeupDb = 12.0f;
        s.autoGain = true;
        dsp.setSettings (s);
        juce::AudioBuffer<float> zeros (2, 22050);
        zeros.clear();
        juce::AudioBuffer<float> out (2, 22050);
        processInBlocks (dsp, zeros, out, 512);
        expect (peakAmp (out, 0, 0) < 1.0e-6f, "silence must stay silence (no auto-gain boost)");
        expect (dsp.getGainReductionDbRaw() < 0.01f);

        beginTest ("Loud noise with extreme settings: no NaN/Inf, bounded output");
        std::mt19937 rng (42);
        std::uniform_real_distribution<float> dist (-0.9f, 0.9f);
        juce::AudioBuffer<float> noise (2, 44100);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 44100; ++i)
                noise.setSample (ch, i, dist (rng));

        buscomp::CompressorDSP dsp2;
        dsp2.prepare (44100.0, 4096, 2);
        dsp2.setOversamplingMode (2);
        auto s2 = baseSettings();
        s2.thresholdDb = -30.0f;
        s2.ratio = 20.0f;
        s2.attackMs = 0.1f;
        s2.releaseSec = 0.1f;
        s2.makeupDb = 12.0f;
        s2.mix = 0.3f;
        s2.stereoLink = 0.0f;
        s2.scHpfHz = 300.0f;
        s2.softKnee = true;
        s2.oversampling = 2;
        dsp2.setSettings (s2);
        juce::AudioBuffer<float> out2 (2, 44100);
        processInBlocks (dsp2, noise, out2, 512);
        expect (allFinite (out2), "extreme settings must not produce NaN/Inf");
        expect (peakAmp (out2, 0, 0) < 6.0f, "output must stay bounded (makeup 12 dB post-mix + OS overshoot)");
    }

    void testParameterRamp()
    {
        beginTest ("Parameter changes during processing cause no NaN or unbounded jumps");
        const double fs = 44100.0;
        buscomp::CompressorDSP dsp;
        dsp.prepare (fs, 4096, 2);
        auto s = baseSettings();
        s.thresholdDb = -30.0f;
        dsp.setSettings (s);

        auto in = makeSine (fs, 44100, 2, 1000.0f, dbToAmp (-10.0f));
        juce::AudioBuffer<float> out (2, 44100);
        juce::AudioBuffer<float> block (2, 512);
        float prev = 0.0f, maxJump = 0.0f;
        for (int off = 0; off < 44100; off += 512)
        {
            block.clear();
            for (int ch = 0; ch < 2; ++ch)
                block.copyFrom (ch, 0, in, ch, off, 512);
            // ramp the threshold from -30 to 0 over the whole second
            s.thresholdDb = -30.0f + 30.0f * (float) off / 44100.0f;
            dsp.setSettings (s);
            dsp.process (block);
            for (int i = 0; i < 512; ++i)
            {
                const float v = block.getSample (0, i);
                maxJump = std::max (maxJump, std::fabs (v - prev));
                prev = v;
                out.setSample (0, off + i, v);
            }
        }
        expect (allFinite (out));
        expect (maxJump < 0.5f, "no click-sized jumps while ramping, max jump " + juce::String (maxJump));
    }
};

static CompressorDSPTests compressorDSPTests;
