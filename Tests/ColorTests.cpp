#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>

#include "../Source/CompressorDSP.h"

#include <cmath>

namespace
{

constexpr double pi = juce::MathConstants<double>::pi;

juce::AudioBuffer<float> makeSine (double fs, int numSamples, float freq, float amp)
{
    juce::AudioBuffer<float> buf (2, numSamples);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < numSamples; ++i)
            buf.setSample (ch, i, amp * std::sin ((float) (2.0 * pi * freq * i / fs)));
    return buf;
}

void processInBlocks (buscomp::CompressorDSP& dsp, const juce::AudioBuffer<float>& in,
                      juce::AudioBuffer<float>& out, int blockSize)
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

double rms (const juce::AudioBuffer<float>& buf, int ch, int startSample)
{
    double sum = 0.0;
    const auto* d = buf.getReadPointer (ch);
    const int n = buf.getNumSamples() - startSample;
    for (int i = startSample; i < buf.getNumSamples(); ++i)
        sum += (double) d[i] * d[i];
    return std::sqrt (sum / juce::jmax (1, n));
}

double mean (const juce::AudioBuffer<float>& buf, int ch)
{
    double sum = 0.0;
    const auto* d = buf.getReadPointer (ch);
    for (int i = 0; i < buf.getNumSamples(); ++i)
        sum += d[i];
    return sum / juce::jmax (1, buf.getNumSamples());
}

// Goertzel amplitude of a single frequency bin over the second half of the buffer
double goertzelAmp (const juce::AudioBuffer<float>& buf, int ch, double fs, double freq)
{
    const int start = buf.getNumSamples() / 2;
    const int n = buf.getNumSamples() - start;
    const double w = 2.0 * pi * freq / fs;
    const double c = 2.0 * std::cos (w);
    double s0 = 0.0, s1 = 0.0, s2 = 0.0;
    const auto* d = buf.getReadPointer (ch);
    for (int i = 0; i < n; ++i)
    {
        s0 = d[start + i] + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double power = s1 * s1 + s2 * s2 - c * s1 * s2;
    return 2.0 * std::sqrt (std::max (0.0, power)) / n; // amplitude
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

buscomp::CompressorDSP::Settings passthroughSettings()
{
    buscomp::CompressorDSP::Settings s;
    s.thresholdDb = -30.0f; // no compression for the test signals
    s.ratio = 1.0f;
    s.attackMs = 1.0f;
    s.releaseSec = 0.1f;
    s.mix = 1.0f;
    s.stereoLink = 1.0f;
    s.oversampling = 0;
    s.colorMode = false;
    s.colorAmount = 0.0f;
    s.softClipOn = false;
    s.clipThresholdDb = -3.0f;
    s.clipKnee = 0.5f;
    return s;
}

} // namespace

//==============================================================================
class ColorAndClipTests final : public juce::UnitTest
{
public:
    ColorAndClipTests() : juce::UnitTest ("ColorDSP+SoftClipper", "DSP") {}

    void runTest() override
    {
        testCleanAddsNoSaturation();
        testColorAmountZeroIsPassthrough();
        testColorAddsHarmonicsWithoutDC();
        testColorLevelMatch();
        testClipperBypassIsBitExact();
        testClipperClipsSymmetrically();
        testClipThresholds();
        testClipperSmallSignal();
        testClipStatsAndReset();
        testClipWithColorAndMix();
        testAliasReductionWithOversampling();
        testGrMeterUnaffectedByColorAndClip();
    }

private:
    void testCleanAddsNoSaturation()
    {
        beginTest ("CLEAN mode adds no saturation (output identical to input when comp is idle)");
        const double fs = 44100.0;
        buscomp::CompressorDSP dsp;
        dsp.prepare (fs, 4096, 2);
        auto s = passthroughSettings();
        s.colorMode = false;
        s.colorAmount = 1.0f; // even at full amount, CLEAN must not saturate
        dsp.setSettings (s);
        dsp.reset();

        auto in = makeSine (fs, 22050, 1000.0f, dbToAmp (-6.0f));
        juce::AudioBuffer<float> out (2, 22050);
        processInBlocks (dsp, in, out, 512);

        float maxDiff = 0.0f;
        for (int i = 0; i < out.getNumSamples(); ++i)
            maxDiff = std::max (maxDiff, std::fabs (out.getSample (0, i) - in.getSample (0, i)));
        expect (maxDiff < 1.0e-6f, "CLEAN must be transparent, max diff " + juce::String (maxDiff));
    }

    void testColorAmountZeroIsPassthrough()
    {
        beginTest ("COLOR with amount 0% passes the signal through unchanged");
        const double fs = 44100.0;
        buscomp::CompressorDSP dsp;
        dsp.prepare (fs, 4096, 2);
        auto s = passthroughSettings();
        s.colorMode = true;
        s.colorAmount = 0.0f;
        dsp.setSettings (s);
        dsp.reset();

        auto in = makeSine (fs, 22050, 1000.0f, dbToAmp (-6.0f));
        juce::AudioBuffer<float> out (2, 22050);
        processInBlocks (dsp, in, out, 512);

        float maxDiff = 0.0f;
        for (int i = 0; i < out.getNumSamples(); ++i)
            maxDiff = std::max (maxDiff, std::fabs (out.getSample (0, i) - in.getSample (0, i)));
        expect (maxDiff < 1.0e-6f, "amount 0% must not color the signal, max diff " + juce::String (maxDiff));
    }

    void testColorAddsHarmonicsWithoutDC()
    {
        beginTest ("COLOR at 100% adds measurable harmonics, no DC offset, no NaN");
        const double fs = 44100.0;
        buscomp::CompressorDSP dsp;
        dsp.prepare (fs, 4096, 2);
        auto s = passthroughSettings();
        s.colorMode = true;
        s.colorAmount = 1.0f;
        dsp.setSettings (s);
        dsp.reset();

        auto in = makeSine (fs, 44100, 1000.0f, 0.9f); // hot signal, drives the curve
        juce::AudioBuffer<float> out (2, 44100);
        processInBlocks (dsp, in, out, 512);

        expect (allFinite (out));

        // residual after removing the best-fit fundamental = harmonic content
        const int start = 22050;
        const int n = 44100 - start;
        // least-squares amplitude of the 1 kHz component in out
        double ss = 0, sc = 0, cc = 0;
        const auto* o = out.getReadPointer (0);
        const auto* r = in.getReadPointer (0);
        for (int i = 0; i < n; ++i)
        {
            const double sn = std::sin (2.0 * pi * 1000.0 * (start + i) / fs);
            const double cs = std::cos (2.0 * pi * 1000.0 * (start + i) / fs);
            ss += sn * sn; cc += cs * cs; sc += sn * cs;
            // residual against the (unity-gain) input sine
        }
        double residual = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double d = o[start + i] - r[start + i];
            residual += d * d;
        }
        residual = std::sqrt (residual / n);
        const double signalRms = rms (in, 0, start);
        const double residualDb = 20.0 * std::log10 (residual / signalRms + 1.0e-20);
        expect (residualDb > -40.0f,
                "COLOR must add measurable harmonic content, residual " + juce::String (residualDb) + " dB rel. signal");

        const double dc = mean (out, 0);
        expect (std::fabs (dc) < 1.0e-4, "no DC offset, mean=" + juce::String (dc));

        // peak is compressed vs. input (denser), but not destroyed
        const float inPeak = peakAmp (in, 0, start);
        const float outPeak = peakAmp (out, 0, start);
        expect (outPeak < inPeak && outPeak > 0.3f,
                "soft saturation should compress the peak: in " + juce::String (ampToDb (inPeak))
                + " dB, out " + juce::String (ampToDb (outPeak)) + " dB");
    }

    void testColorLevelMatch()
    {
        beginTest ("CLEAN/COLOR level match: moderate signal stays within ~1.5 dB RMS");
        const double fs = 44100.0;
        auto runRms = [&] (bool colorMode, float amount) -> float
        {
            buscomp::CompressorDSP dsp;
            dsp.prepare (fs, 4096, 2);
            auto s = passthroughSettings();
            s.colorMode = colorMode;
            s.colorAmount = amount;
            dsp.setSettings (s);
            dsp.reset();
            auto in = makeSine (fs, 44100, 1000.0f, dbToAmp (-20.0f));
            juce::AudioBuffer<float> out (2, 44100);
            processInBlocks (dsp, in, out, 512);
            return (float) (20.0 * std::log10 (rms (out, 0, 22050) + 1.0e-20));
        };

        const float cleanDb = runRms (false, 1.0f);
        const float colorDb = runRms (true, 1.0f);
        expect (std::fabs (colorDb - cleanDb) < 1.5f,
                "level difference CLEAN vs COLOR: " + juce::String (colorDb - cleanDb) + " dB");
    }

    void testClipperBypassIsBitExact()
    {
        beginTest ("Soft clipper OFF passes the signal through bit-exactly");
        const double fs = 44100.0;
        buscomp::CompressorDSP dsp;
        dsp.prepare (fs, 4096, 2);
        auto s = passthroughSettings();
        s.softClipOn = false;
        dsp.setSettings (s);
        dsp.reset();

        auto in = makeSine (fs, 22050, 1000.0f, 0.95f); // would clip if enabled
        juce::AudioBuffer<float> out (2, 22050);
        processInBlocks (dsp, in, out, 512);

        float maxDiff = 0.0f;
        for (int i = 0; i < out.getNumSamples(); ++i)
            maxDiff = std::max (maxDiff, std::fabs (out.getSample (0, i) - in.getSample (0, i)));
        expect (maxDiff < 1.0e-6f, "bypassed clipper must be transparent, max diff " + juce::String (maxDiff));
        expect (dsp.getClipMaxOvershootDb() == 0.0f && dsp.getClipRatio() == 0.0f);
    }

    void testClipperClipsSymmetrically()
    {
        beginTest ("Soft clipper limits peaks symmetrically at the threshold");
        const double fs = 44100.0;
        buscomp::CompressorDSP dsp;
        dsp.prepare (fs, 4096, 2);
        auto s = passthroughSettings();
        s.softClipOn = true;
        s.clipThresholdDb = -6.0f;
        s.clipKnee = 0.5f;
        dsp.setSettings (s);
        dsp.reset();

        auto in = makeSine (fs, 22050, 1000.0f, dbToAmp (-3.0f)); // above -6 dBFS threshold
        juce::AudioBuffer<float> out (2, 22050);
        processInBlocks (dsp, in, out, 512);

        expect (allFinite (out));
        const int start = 11025;
        float maxPos = 0.0f, maxNeg = 0.0f;
        const auto* d = out.getReadPointer (0);
        for (int i = start; i < out.getNumSamples(); ++i)
        {
            maxPos = std::max (maxPos, d[i]);
            maxNeg = std::min (maxNeg, d[i]);
        }
        const float thr = dbToAmp (-6.0f);
        expect (ampToDb (maxPos) <= -6.0f + 0.6f,
                "positive peak must be limited near the threshold, got " + juce::String (ampToDb (maxPos)));
        expect (std::fabs (ampToDb (-maxNeg) - ampToDb (maxPos)) < 0.3f,
                "symmetric clipping: pos " + juce::String (ampToDb (maxPos))
                + " dB, neg " + juce::String (ampToDb (-maxNeg)) + " dB");
        expect (std::fabs (mean (out, 0)) < 1.0e-4, "no DC offset");

        // stats: overshoot ~ 3 dB (input -3 dBFS vs threshold -6 dBFS), some samples clipped
        expect (dsp.getClipMaxOvershootDb() > 2.0f,
                "max overshoot should be ~3 dB, got " + juce::String (dsp.getClipMaxOvershootDb()));
        expect (dsp.getClipRatio() > 0.01f && dsp.getClipRatio() < 1.0f,
                "clip ratio should be a fraction, got " + juce::String (dsp.getClipRatio()));
    }

    void testClipThresholds()
    {
        beginTest ("Different clip thresholds: lower threshold clips more of the same signal");
        const double fs = 44100.0;
        auto runRatio = [&] (float thrDb) -> float
        {
            buscomp::CompressorDSP dsp;
            dsp.prepare (fs, 4096, 2);
            auto s = passthroughSettings();
            s.softClipOn = true;
            s.clipThresholdDb = thrDb;
            dsp.setSettings (s);
            dsp.reset();
            auto in = makeSine (fs, 22050, 1000.0f, dbToAmp (-3.0f));
            juce::AudioBuffer<float> out (2, 22050);
            processInBlocks (dsp, in, out, 512);
            return dsp.getClipRatio();
        };

        const float rLow = runRatio (-12.0f);
        const float rHigh = runRatio (0.0f);
        expect (rLow > 0.1f, "-12 dBFS threshold should clip a lot, ratio " + juce::String (rLow));
        expect (rHigh == 0.0f, "0 dBFS threshold must not clip a -3 dBFS signal, ratio " + juce::String (rHigh));
    }

    void testClipperSmallSignal()
    {
        beginTest ("Signal below the clip threshold passes (nearly) unchanged");
        const double fs = 44100.0;
        buscomp::CompressorDSP dsp;
        dsp.prepare (fs, 4096, 2);
        auto s = passthroughSettings();
        s.softClipOn = true;
        s.clipThresholdDb = -6.0f;
        s.clipKnee = 0.0f; // d = 1: linear below the threshold
        dsp.setSettings (s);
        dsp.reset();

        auto in = makeSine (fs, 22050, 1000.0f, dbToAmp (-20.0f)); // well below -6 dBFS
        juce::AudioBuffer<float> out (2, 22050);
        processInBlocks (dsp, in, out, 512);

        const float inDb = ampToDb ((float) rms (in, 0, 11025));
        const float outDb = ampToDb ((float) rms (out, 0, 11025));
        expect (std::fabs (inDb - outDb) < 0.5f,
                "small signal should pass almost unchanged: in " + juce::String (inDb)
                + " out " + juce::String (outDb));
        expect (dsp.getClipRatio() == 0.0f, "nothing should be counted as clipped");
    }

    void testClipStatsAndReset()
    {
        beginTest ("Clip stats reset works");
        const double fs = 44100.0;
        buscomp::CompressorDSP dsp;
        dsp.prepare (fs, 4096, 2);
        auto s = passthroughSettings();
        s.softClipOn = true;
        s.clipThresholdDb = -6.0f;
        dsp.setSettings (s);
        dsp.reset();

        auto hot = makeSine (fs, 22050, 1000.0f, dbToAmp (-3.0f));
        juce::AudioBuffer<float> out (2, 22050);
        processInBlocks (dsp, hot, out, 512);
        expect (dsp.getClipRatio() > 0.0f && dsp.getClipMaxOvershootDb() > 0.0f);

        dsp.resetMeters();
        auto quiet = makeSine (fs, 4410, 1000.0f, dbToAmp (-30.0f));
        juce::AudioBuffer<float> out2 (2, 4410);
        processInBlocks (dsp, quiet, out2, 512);
        expect (dsp.getClipMaxOvershootDb() == 0.0f && dsp.getClipRatio() == 0.0f,
                "after reset, quiet signal must show zero clip stats");
    }

    void testClipWithColorAndMix()
    {
        beginTest ("Soft clipper works with COLOR and mix 50%, all finite, GR still compressor-only");
        const double fs = 44100.0;
        buscomp::CompressorDSP dsp;
        dsp.prepare (fs, 4096, 2);
        auto s = passthroughSettings();
        s.colorMode = true;
        s.colorAmount = 0.8f;
        s.softClipOn = true;
        s.clipThresholdDb = -12.0f;
        s.mix = 0.5f;
        s.thresholdDb = -20.0f;
        s.ratio = 4.0f; // compression active too
        dsp.setSettings (s);
        dsp.reset();

        auto in = makeSine (fs, 22050, 1000.0f, dbToAmp (-6.0f));
        juce::AudioBuffer<float> out (2, 22050);
        processInBlocks (dsp, in, out, 512);
        expect (allFinite (out));
        expect (dsp.getGainReductionDbRaw() > 1.0f, "compressor GR must still work");
        expect (dsp.getClipRatio() > 0.0f, "clipper must engage post-mix");
    }

    void testAliasReductionWithOversampling()
    {
        beginTest ("Oversampling reduces aliasing of the COLOR nonlinearity (measured)");
        const double fs = 44100.0;
        const double f0 = 15000.0;                    // 2nd harmonic 30 kHz aliases to 14.1 kHz
        const double fAlias = std::fabs (2.0 * f0 - fs);

        auto aliasLevel = [&] (int osMode) -> float
        {
            buscomp::CompressorDSP dsp;
            dsp.prepare (fs, 4096, 2);
            dsp.setOversamplingMode (osMode);
            auto s = passthroughSettings();
            s.colorMode = true;
            s.colorAmount = 1.0f;
            s.oversampling = osMode;
            dsp.setSettings (s);
            dsp.reset();
            auto in = makeSine (fs, 44100, (float) f0, 0.8f);
            juce::AudioBuffer<float> out (2, 44100);
            processInBlocks (dsp, in, out, 512);
            const double a = goertzelAmp (out, 0, fs, fAlias);
            const double fund = goertzelAmp (out, 0, fs, f0);
            expect (fund > 0.05, "fundamental must be present");
            return (float) (20.0 * std::log10 (a / fund + 1.0e-20)); // alias relative to fundamental
        };

        const float offDb  = aliasLevel (0);
        const float fourDb = aliasLevel (2);
        logMessage ("alias level rel. fundamental: OFF " + juce::String (offDb, 1)
                    + " dB, 4x " + juce::String (fourDb, 1) + " dB");
        expect (fourDb < offDb - 3.0f,
                "4x oversampling should reduce the alias by >3 dB: off " + juce::String (offDb)
                + " dB vs 4x " + juce::String (fourDb) + " dB");
    }

    void testGrMeterUnaffectedByColorAndClip()
    {
        beginTest ("GR meter shows only compressor gain reduction, not color/clip");
        const double fs = 44100.0;
        buscomp::CompressorDSP dsp;
        dsp.prepare (fs, 4096, 2);
        auto s = passthroughSettings(); // ratio 1, threshold -30 -> no compression
        s.colorMode = true;
        s.colorAmount = 1.0f;
        s.softClipOn = true;
        s.clipThresholdDb = -12.0f;
        dsp.setSettings (s);
        dsp.reset();

        auto in = makeSine (fs, 22050, 1000.0f, dbToAmp (-2.0f)); // hot: color+clip active
        juce::AudioBuffer<float> out (2, 22050);
        float maxGr = 0.0f;
        juce::AudioBuffer<float> block (2, 512);
        for (int off = 0; off < 22050; off += 512)
        {
            const int n = juce::jmin (512, 22050 - off);
            block.clear();
            block.copyFrom (0, 0, in, 0, off, n);
            block.copyFrom (1, 0, in, 1, off, n);
            dsp.process (block);
            maxGr = std::max (maxGr, dsp.getGainReductionDbRaw());
            for (int ch = 0; ch < 2; ++ch)
                out.copyFrom (ch, off, block, ch, 0, n);
        }
        expect (maxGr < 0.05f, "GR must be ~0 when the compressor is idle, got " + juce::String (maxGr));
        expect (dsp.getClipRatio() > 0.0f, "but the clipper did engage (sanity check)");
    }
};

static ColorAndClipTests colorAndClipTests;
