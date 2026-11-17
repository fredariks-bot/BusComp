#include "CompressorDSP.h"
#include <cmath>
#include <algorithm>

namespace buscomp
{

static constexpr float kneeWidthDb = 6.0f;
static constexpr float minLevelDb  = -240.0f;

CompressorDSP::CompressorDSP()
    : os2 (maxChannels, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true),
      os4 (maxChannels, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true)
{
}

void CompressorDSP::prepare (double newSampleRate, int newMaxBlock, int newNumChannels)
{
    sampleRate  = newSampleRate;
    maxBlock    = juce::jmax (16, newMaxBlock);
    numChannels = juce::jlimit (1, maxChannels, newNumChannels);

    scFilter.prepare (sampleRate, maxChannels);
    colorStage.prepare (sampleRate);
    clipper.prepare (sampleRate);

    os2.initProcessing ((size_t) maxBlock);
    os4.initProcessing ((size_t) maxBlock);
    os2.reset();
    os4.reset();

    coreBuffer.setSize (maxChannels, maxBlock * 4, false, false, true);
    coreBuffer.clear();

    inputMeter.prepare (sampleRate);
    outputMeter.prepare (sampleRate);
    grMeter.prepare (sampleRate);

    reset();
}

void CompressorDSP::reset()
{
    for (int ch = 0; ch < maxChannels; ++ch)
        gr[ch] = 0.0f;
    holdTime = 0.0f;
    tRelSmoothed = tRelease.load();
    inRms = outRms = smAutoMakeup = 0.0f;
    smThreshold = tThreshold.load(); smRatio = tRatio.load();
    smAttackMs = tAttack.load();     smReleaseSec = tRelease.load();
    smMakeup = tMakeup.load();       smMix = tMix.load();
    smLink = tLink.load();           smScHpf = tScHpf.load();
    scFilter.reset();
    colorStage.reset();
    clipper.reset();
    os2.reset();
    os4.reset();
    inputMeter.reset();
    outputMeter.reset();
    grMeter.reset();
}

void CompressorDSP::setSettings (const Settings& s)
{
    tThreshold.store (s.thresholdDb, std::memory_order_relaxed);
    tRatio.store (s.ratio, std::memory_order_relaxed);
    tAttack.store (s.attackMs, std::memory_order_relaxed);
    tRelease.store (s.releaseSec, std::memory_order_relaxed);
    tAutoRelease.store (s.autoRelease, std::memory_order_relaxed);
    tMakeup.store (s.makeupDb, std::memory_order_relaxed);
    tAutoGain.store (s.autoGain, std::memory_order_relaxed);
    tMix.store (s.mix, std::memory_order_relaxed);
    tLink.store (s.stereoLink, std::memory_order_relaxed);
    tScHpf.store (s.scHpfHz, std::memory_order_relaxed);
    tSoftKnee.store (s.softKnee, std::memory_order_relaxed);
    tColorMode.store (s.colorMode, std::memory_order_relaxed);
    tColorAmount.store (s.colorAmount, std::memory_order_relaxed);
    tSoftClip.store (s.softClipOn, std::memory_order_relaxed);
    tClipThreshold.store (s.clipThresholdDb, std::memory_order_relaxed);
    tClipKnee.store (s.clipKnee, std::memory_order_relaxed);

    colorStage.setEnabled (s.colorMode);
    colorStage.setAmount (s.colorAmount);
    clipper.setEnabled (s.softClipOn);
    clipper.setThresholdDb (s.clipThresholdDb);
    clipper.setKnee (s.clipKnee);
}

void CompressorDSP::setOversamplingMode (int stages) noexcept
{
    pendingMode.store (juce::jlimit (0, 2, stages), std::memory_order_release);
}

int CompressorDSP::getLatencySamples() const noexcept
{
    return getLatencySamplesForMode (activeMode.load (std::memory_order_acquire));
}

int CompressorDSP::getLatencySamplesForMode (int mode) const noexcept
{
    switch (juce::jlimit (0, 2, mode))
    {
        case 1:  return (int) std::ceil (os2.getLatencyInSamples());
        case 2:  return (int) std::ceil (os4.getLatencyInSamples());
        default: return 0;
    }
}

void CompressorDSP::applyOversamplingMode (int mode)
{
    activeMode.store (mode, std::memory_order_release);
    if (mode == 1)
        os2.reset();
    else if (mode == 2)
        os4.reset();
}

float CompressorDSP::gainComputer (float levelDb, float thresholdDb, float ratio, bool softKnee) noexcept
{
    const float r = juce::jmax (1.0f, ratio);
    const float over = levelDb - thresholdDb;
    const float slope = 1.0f - 1.0f / r; // dB of reduction per dB over threshold

    if (! softKnee)
        return juce::jmax (0.0f, over) * slope;

    // 6 dB soft knee, quadratic between threshold-W/2 and threshold+W/2;
    // continuous in value and slope at both edges of the knee region.
    if (over <= -kneeWidthDb * 0.5f)
        return 0.0f;
    if (over >= kneeWidthDb * 0.5f)
        return over * slope;
    const float d = over + kneeWidthDb * 0.5f;
    return slope * d * d / (2.0f * kneeWidthDb);
}

void CompressorDSP::process (juce::AudioBuffer<float>& buffer)
{
    const int mode = pendingMode.load (std::memory_order_acquire);
    if (mode != activeMode.load (std::memory_order_acquire))
        applyOversamplingMode (mode); // allocation-free: reset() only clears filter state

    const int numCh = juce::jmin (buffer.getNumChannels(), maxChannels);
    const int n     = buffer.getNumSamples();
    if (numCh == 0 || n == 0)
        return;

    for (int ch = 0; ch < numCh; ++ch)
        inputMeter.processChannel (ch, buffer.getReadPointer (ch), n);

    if (mode == 0)
    {
        juce::dsp::AudioBlock<float> block (buffer);
        processCore (block, sampleRate);
    }
    else
    {
        auto* os = mode == 1 ? &os2 : &os4;

        coreBuffer.clear (0, n);
        for (int ch = 0; ch < maxChannels; ++ch)
            coreBuffer.copyFrom (ch, 0, buffer.getReadPointer (juce::jmin (ch, numCh - 1)), n);

        juce::dsp::AudioBlock<float> coreBlock (coreBuffer);
        coreBlock = coreBlock.getSubBlock (0, (size_t) n); // only the current block
        auto upBlock = os->processSamplesUp (coreBlock);    // mutable view, internal buffer
        processCore (upBlock, sampleRate * (mode == 1 ? 2.0 : 4.0));

        juce::dsp::AudioBlock<float> outBlock (buffer);
        os->processSamplesDown (outBlock);
    }

    for (int ch = 0; ch < numCh; ++ch)
        outputMeter.processChannel (ch, buffer.getReadPointer (ch), n);
}

void CompressorDSP::processCore (juce::dsp::AudioBlock<float>& block, double rate)
{
    const int n     = (int) block.getNumSamples();
    const int numCh = juce::jmin ((int) block.getNumChannels(), maxChannels);

    const float targetThreshold = tThreshold.load (std::memory_order_relaxed);
    const float targetRatio     = tRatio.load (std::memory_order_relaxed);
    const float targetAttack    = tAttack.load (std::memory_order_relaxed);
    const float targetRelease   = tRelease.load (std::memory_order_relaxed);
    const bool  autoRel         = tAutoRelease.load (std::memory_order_relaxed);
    const float targetMakeup    = tMakeup.load (std::memory_order_relaxed);
    const bool  autoGainOn      = tAutoGain.load (std::memory_order_relaxed);
    const float targetMix       = tMix.load (std::memory_order_relaxed);
    const float targetLink      = tLink.load (std::memory_order_relaxed);
    const float targetScHpf     = tScHpf.load (std::memory_order_relaxed);
    const bool  softKnee        = tSoftKnee.load (std::memory_order_relaxed);

    const float paramCoeff = 1.0f - std::exp (-1.0f / (float) (0.02 * rate)); // 20 ms parameter smoothing
    const float rmsCoeff   = 1.0f - std::exp (-1.0f / (float) (0.3 * rate));  // 300 ms RMS window
    const float autoCoeff  = 1.0f - std::exp (-1.0f / (float) (1.0 * rate));  // 1 s auto-gain smoothing

    scFilter.updateFrequency (targetScHpf);

    for (int i = 0; i < n; ++i)
    {
        smThreshold  += (targetThreshold - smThreshold) * paramCoeff;
        smRatio      += (targetRatio - smRatio) * paramCoeff;
        smAttackMs   += (targetAttack - smAttackMs) * paramCoeff;
        smReleaseSec += (targetRelease - smReleaseSec) * paramCoeff;
        smMakeup     += (targetMakeup - smMakeup) * paramCoeff;
        smMix        += (targetMix - smMix) * paramCoeff;
        smLink       += (targetLink - smLink) * paramCoeff;
        smScHpf      += (targetScHpf - smScHpf) * paramCoeff;
        scFilter.updateFrequency (smScHpf);

        const float attackSec  = 0.001f * smAttackMs;
        const float releaseSec = smReleaseSec;

        float levelDb[maxChannels];
        float x[maxChannels];

        for (int ch = 0; ch < numCh; ++ch)
        {
            const float in = block.getSample (ch, i);
            x[ch] = in;
            const float det = scFilter.processSample (ch, in);
            const float lvl = std::fabs (det);
            levelDb[ch] = juce::jmax (minLevelDb, 20.0f * std::log10 (lvl + 1.0e-12f));
        }

        // stereo-linked detector: peak (max) of the channel levels, blended
        // with the independent per-channel detector by the link amount
        float maxDb = levelDb[0];
        for (int ch = 1; ch < numCh; ++ch)
            maxDb = std::max (maxDb, levelDb[ch]);

        const float grLinked = gainComputer (maxDb, smThreshold, smRatio, softKnee);

        // auto release: program-dependent time constant driven by how long
        // the detector has been near/above threshold (gain-reduction history)
        if (autoRel)
        {
            if (maxDb > smThreshold - 6.0f)
                holdTime = std::min (2.0f, holdTime + (float) (1.0 / rate));
            else
                holdTime = std::max (0.0f, holdTime - (float) (1.5 / rate));

            const float tFast = 0.06f, tSlow = 0.9f;
            const float tRelTarget = tFast + (tSlow - tFast) * juce::jlimit (0.0f, 1.0f, holdTime / 0.25f);
            tRelSmoothed += (tRelTarget - tRelSmoothed) * (1.0f - std::exp (-1.0f / (float) (0.05 * rate)));
        }

        float grOut = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
        {
            const float grOwn  = gainComputer (levelDb[ch], smThreshold, smRatio, softKnee);
            const float target = smLink * grLinked + (1.0f - smLink) * grOwn;

            float coeff;
            if (target > gr[ch])
            {
                coeff = std::exp (-1.0f / (juce::jmax (1.0e-5f, attackSec) * (float) rate));
            }
            else
            {
                const float rel = autoRel ? tRelSmoothed : releaseSec;
                coeff = std::exp (-1.0f / (juce::jmax (1.0e-5f, rel) * (float) rate));
            }
            gr[ch] += (target - gr[ch]) * (1.0f - coeff);
            grOut = std::max (grOut, gr[ch]);

            float wet = x[ch] * std::pow (10.0f, -gr[ch] / 20.0f);

            // CLEAN = passthrough, COLOR = soft saturation (shared oversampling chain)
            wet = colorStage.processSample (wet, rate);

            // auto-gain measurement: dry input RMS vs. wet (post-color, pre-mix) RMS
            inRms  += (x[ch] * x[ch] - inRms) * rmsCoeff;
            outRms += (wet * wet - outRms) * rmsCoeff;

            // parallel mix: linear, 100% = wet only, 0% = dry only
            float mixed = wet * smMix + x[ch] * (1.0f - smMix);

            // optional soft clipper (post-mix, pre-makeup)
            mixed = clipper.processSample (mixed, rate);

            // final output gain: manual makeup + optional auto makeup
            const float makeupDb = smMakeup + (autoGainOn ? smAutoMakeup : 0.0f);
            mixed *= std::pow (10.0f, makeupDb / 20.0f);

            block.setSample (ch, i, mixed);
        }

        // auto makeup from the measured level difference; clamped to 0..+12 dB
        // and gated at -60 dBFS input so silence/quiet material gets no boost
        const float inDb  = 10.0f * std::log10 (inRms + 1.0e-12f);
        const float outDb = 10.0f * std::log10 (outRms + 1.0e-12f);
        const float autoTarget = (inDb > -60.0f) ? juce::jlimit (0.0f, 12.0f, inDb - outDb) : 0.0f;
        smAutoMakeup += (autoTarget - smAutoMakeup) * autoCoeff;

        grMeter.processSample (grOut, rate);
    }
}

} // namespace buscomp
