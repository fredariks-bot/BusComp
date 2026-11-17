#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "CompressorDSP.h"
#include "PresetManager.h"
#include "ParameterDefs.h"

namespace buscomp
{

/** BUS COMP - VCA-style bus compressor audio processor.

    Owns the APVTS parameter state, the DSP engine and the preset manager.
    The editor only reads lock-free meter atomics and talks to the
    processor on the message thread. */
class BusCompProcessor final : public juce::AudioProcessor,
                               private juce::AudioProcessorValueTreeState::Listener
{
public:
    BusCompProcessor();
    ~BusCompProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    void processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    //==============================================================================
    const juce::String getName() const override { return "BUS COMP"; }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override {}

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    juce::AudioProcessorValueTreeState apvts;
    PresetManager presetManager;
    CompressorDSP dsp;

    float getParamValue (const juce::String& id) const;
    void  setParamValue (const juce::String& id, float value);

    void loadPreset (int index);
    void loadPreset (const juce::String& name);
    juce::String getCurrentPresetName() const { return currentPresetName; }

    void snapshotAB();
    void toggleAB();
    bool isABActive() const noexcept { return abActive; }

    // meter access for the editor (lock-free)
    float getGainReductionDb() const noexcept { return dsp.getGainReductionDb(); }
    float getGainReductionPeakDb() const noexcept { return dsp.getGainReductionPeakDb(); }
    float getInputDb (int ch) const noexcept { return dsp.getInputDb (ch); }
    float getInputHoldDb (int ch) const noexcept { return dsp.getInputHoldDb (ch); }
    float getInputRmsDb (int ch) const noexcept { return dsp.getInputRmsDb (ch); }
    float getOutputDb (int ch) const noexcept { return dsp.getOutputDb (ch); }
    float getOutputHoldDb (int ch) const noexcept { return dsp.getOutputHoldDb (ch); }
    float getOutputRmsDb (int ch) const noexcept { return dsp.getOutputRmsDb (ch); }
    bool  isInputClipping (int ch) const noexcept { return dsp.isInputClipping (ch); }
    bool  isOutputClipping (int ch) const noexcept { return dsp.isOutputClipping (ch); }
    void  resetMeters() noexcept { dsp.resetMeters(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void parameterChanged (const juce::String& parameterID, float newValue) override;

    CompressorDSP::Settings readSettings() const;

    static const juce::ValueTree* findParamChild (const juce::ValueTree& state, const juce::String& id)
    {
        for (auto child : state)
            if (child.hasType ("PARAM") && child.getProperty ("id").toString() == id)
                return &child;
        return nullptr;
    }

    juce::String currentPresetName { "Gentle Glue" };
    juce::ValueTree abSnapshot; // invalid until snapshotAB() is called
    bool abActive = false;
    bool loadingPreset = false;
    int  pendingOsMode = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BusCompProcessor)
};

} // namespace buscomp
