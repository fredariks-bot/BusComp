#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <vector>
#include <utility>

namespace buscomp
{

/** Factory preset management.

    Presets are stored as ValueTrees of type "PARAMS" with one float property
    per parameter id, so they can be merged into / extracted from the
    AudioProcessorValueTreeState and serialised with the plugin state.
    No file I/O is performed here; persistence goes through the processor's
    getStateInformation()/setStateInformation(). */
class PresetManager
{
public:
    static constexpr const char* presetTreeType = "PARAMS";

    PresetManager();

    int getNumPresets() const noexcept { return (int) presets.size(); }
    juce::StringArray getPresetNames() const;
    juce::String getPresetName (int index) const;
    int getPresetIndex (const juce::String& name) const noexcept;

    /** Full parameter state of a preset (type "PARAMS"). */
    juce::ValueTree getPresetState (int index) const;
    juce::ValueTree getPresetState (const juce::String& name) const;
    juce::ValueTree getDefaultPresetState() const { return getPresetState (0); }

    /** Serialises a state tree to binary (for getStateInformation). */
    static juce::MemoryBlock stateToBinary (const juce::ValueTree& state);
    /** Parses binary state; returns an invalid ValueTree on corrupt input. */
    static juce::ValueTree stateFromBinary (const juce::MemoryBlock& data);

private:
    void addPreset (const juce::String& name,
                    std::initializer_list<std::pair<juce::String, float>> values);

    std::vector<std::pair<juce::String, juce::ValueTree>> presets;
};

} // namespace buscomp
