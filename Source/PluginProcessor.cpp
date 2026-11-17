#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace buscomp
{

static const juce::String stateTreeType = "BUSCOMP_STATE";

BusCompProcessor::BusCompProcessor()
    : juce::AudioProcessor (BusesProperties()
                            .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                            .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, stateTreeType, createParameterLayout())
{
    for (const auto& def : parameterDefs())
        apvts.addParameterListener (def.id, this);
}

BusCompProcessor::~BusCompProcessor()
{
    for (const auto& def : parameterDefs())
        apvts.removeParameterListener (def.id, this);
}

juce::AudioProcessorValueTreeState::ParameterLayout BusCompProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (const auto& def : parameterDefs())
    {
        const juce::ParameterID id { def.id, 1 };
        switch (def.type)
        {
            case ParamType::Bool:
                layout.add (std::make_unique<juce::AudioParameterBool> (
                    id, def.name, def.defaultValue >= 0.5f));
                break;
            case ParamType::Choice:
                layout.add (std::make_unique<juce::AudioParameterChoice> (
                    id, def.name, def.choices, 0));
                break;
            case ParamType::Float:
            default:
                layout.add (std::make_unique<juce::AudioParameterFloat> (
                    id, def.name,
                    juce::NormalisableRange<float> (def.minValue, def.maxValue, def.step),
                    def.defaultValue));
                break;
        }
    }
    return layout;
}

//==============================================================================
void BusCompProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    dsp.prepare (sampleRate, samplesPerBlock, getTotalNumOutputChannels() > 1 ? 2 : 1);
    dsp.setOversamplingMode (pendingOsMode);
    setLatencySamples (dsp.getLatencySamplesForMode (pendingOsMode));
}

void BusCompProcessor::releaseResources()
{
    dsp.reset();
}

bool BusCompProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mainIn  = layouts.getMainInputChannelSet();
    const auto mainOut = layouts.getMainOutputChannelSet();
    if (mainOut.isDisabled())
        return false;
    return (mainIn == juce::AudioChannelSet::mono() || mainIn == juce::AudioChannelSet::stereo())
        && (mainOut == juce::AudioChannelSet::mono() || mainOut == juce::AudioChannelSet::stereo());
}

CompressorDSP::Settings BusCompProcessor::readSettings() const
{
    CompressorDSP::Settings s;
    s.thresholdDb  = getParamValue ("threshold");
    s.ratio        = getParamValue ("ratio");
    s.attackMs     = getParamValue ("attack");
    s.releaseSec   = getParamValue ("release");
    s.autoRelease  = getParamValue ("autoRelease") >= 0.5f;
    s.makeupDb     = getParamValue ("makeup");
    s.autoGain     = getParamValue ("autoGain") >= 0.5f;
    s.mix          = getParamValue ("mix") / 100.0f;
    s.stereoLink   = getParamValue ("stereoLink") / 100.0f;

    static const float hpfTable[] = { 0.0f, 20.0f, 30.0f, 40.0f, 50.0f, 60.0f, 80.0f,
                                      100.0f, 120.0f, 150.0f, 200.0f, 250.0f, 300.0f };
    const int hpfIndex = (int) juce::jlimit (0.0f, 12.0f, getParamValue ("scHpf"));
    s.scHpfHz      = hpfTable[hpfIndex];

    s.softKnee     = getParamValue ("softKnee") >= 0.5f;
    s.oversampling = (int) juce::jlimit (0.0f, 2.0f, getParamValue ("oversampling"));
    return s;
}

void BusCompProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto settings = readSettings();
    dsp.setSettings (settings);
    if (settings.oversampling != dsp.getOversamplingMode())
        dsp.setOversamplingMode (settings.oversampling);

    dsp.process (buffer);
}

void BusCompProcessor::processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&)
{
}

//==============================================================================
juce::AudioProcessorEditor* BusCompProcessor::createEditor()
{
    return new BusCompEditor (*this);
}

//==============================================================================
int BusCompProcessor::getNumPrograms()
{
    return presetManager.getNumPresets();
}

int BusCompProcessor::getCurrentProgram()
{
    return juce::jmax (0, presetManager.getPresetIndex (currentPresetName));
}

void BusCompProcessor::setCurrentProgram (int index)
{
    loadPreset (index);
}

const juce::String BusCompProcessor::getProgramName (int index)
{
    return presetManager.getPresetName (index);
}

//==============================================================================
float BusCompProcessor::getParamValue (const juce::String& id) const
{
    if (auto* p = apvts.getRawParameterValue (id))
        return p->load (std::memory_order_relaxed);
    return 0.0f;
}

void BusCompProcessor::setParamValue (const juce::String& id, float value)
{
    if (auto* p = apvts.getParameter (id))
    {
        const float norm = p->getNormalisableRange().convertTo0to1 (value);
        p->setValueNotifyingHost (norm);
    }
}

void BusCompProcessor::loadPreset (int index)
{
    auto preset = presetManager.getPresetState (index);
    if (! preset.isValid())
        return;

    loadingPreset = true;
    for (const auto& def : parameterDefs())
        if (preset.hasProperty (def.id))
            setParamValue (def.id, (float) preset.getProperty (def.id));
    loadingPreset = false;

    currentPresetName = presetManager.getPresetName (index);
    pendingOsMode = (int) juce::jlimit (0.0f, 2.0f, getParamValue ("oversampling"));
    dsp.setOversamplingMode (pendingOsMode);
    setLatencySamples (dsp.getLatencySamplesForMode (pendingOsMode));
}

void BusCompProcessor::loadPreset (const juce::String& name)
{
    loadPreset (presetManager.getPresetIndex (name));
}

void BusCompProcessor::snapshotAB()
{
    abSnapshot = apvts.copyState();
    abActive = false;
}

void BusCompProcessor::toggleAB()
{
    if (! abSnapshot.isValid())
        snapshotAB();

    auto current = apvts.copyState();
    apvts.replaceState (abSnapshot);
    abSnapshot = current;
    abActive = ! abActive;
}

//==============================================================================
void BusCompProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("presetName", currentPresetName, nullptr);
    state.setProperty ("version", 1, nullptr);
    destData = PresetManager::stateToBinary (state);
}

void BusCompProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto incoming = PresetManager::stateFromBinary (juce::MemoryBlock (data, (size_t) sizeInBytes));
    if (! incoming.isValid())
        return; // corrupt data: keep current state

    // The APVTS state is a tree of "PARAM" child nodes (properties "id" and
    // "value"). replaceState() updates every parameter present in the incoming
    // tree; parameters missing from older/partial states keep their current
    // (default) values, and all values are clamped to the parameter ranges.
    loadingPreset = true;
    apvts.replaceState (incoming.createCopy());
    for (const auto& def : parameterDefs())
        if (auto* child = findParamChild (incoming, def.id))
            setParamValue (def.id, (float) child->getProperty ("value"));
    loadingPreset = false;

    if (incoming.hasProperty ("presetName"))
        currentPresetName = incoming.getProperty ("presetName").toString();

    pendingOsMode = (int) juce::jlimit (0.0f, 2.0f, getParamValue ("oversampling"));
    dsp.setOversamplingMode (pendingOsMode);
    setLatencySamples (dsp.getLatencySamplesForMode (pendingOsMode));
}

//==============================================================================
void BusCompProcessor::parameterChanged (const juce::String& parameterID, float newValue)
{
    if (parameterID == "oversampling")
    {
        pendingOsMode = (int) juce::jlimit (0.0f, 2.0f, newValue);
        dsp.setOversamplingMode (pendingOsMode);
        setLatencySamples (dsp.getLatencySamplesForMode (pendingOsMode));
    }
    else if (! loadingPreset && parameterID != "presetName")
    {
        currentPresetName = "(custom)";
    }
}

} // namespace buscomp

//==============================================================================
// VST3 entry point
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new buscomp::BusCompProcessor();
}
