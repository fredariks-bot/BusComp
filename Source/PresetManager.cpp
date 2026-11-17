#include "PresetManager.h"
#include "ParameterDefs.h"

namespace buscomp
{

PresetManager::PresetManager()
{
    // The presets are sensible starting points, not objectively optimal
    // settings. Ranges: threshold -30..0 dB, ratio 1..20, attack 0.1..30 ms,
    // release 0.1..1.2 s, makeup 0..+12 dB, mix/link 0..100 %.
    addPreset ("Gentle Glue",
    {
        { "threshold", -20.0f }, { "ratio", 2.0f }, { "attack", 10.0f },
        { "release", 0.4f }, { "autoRelease", 1.0f }, { "makeup", 0.0f },
        { "autoGain", 0.0f }, { "mix", 100.0f }, { "stereoLink", 100.0f },
        { "scHpf", 0.0f }, { "oversampling", 0.0f }, { "softKnee", 0.0f }, { "mode", 0.0f }, { "colorAmount", 50.0f }, { "softClip", 0.0f }, { "clipThreshold", -3.0f }, { "clipKnee", 50.0f }
    });
    addPreset ("Drum Bus",
    {
        { "threshold", -14.0f }, { "ratio", 4.0f }, { "attack", 3.0f },
        { "release", 0.2f }, { "autoRelease", 1.0f }, { "makeup", 3.0f },
        { "autoGain", 0.0f }, { "mix", 100.0f }, { "stereoLink", 100.0f },
        { "scHpf", 7.0f }, { "oversampling", 0.0f }, { "softKnee", 0.0f }, // 100 Hz
    });
    addPreset ("Mix Bus",
    {
        { "threshold", -16.0f }, { "ratio", 3.0f }, { "attack", 10.0f },
        { "release", 0.5f }, { "autoRelease", 1.0f }, { "makeup", 2.0f },
        { "autoGain", 0.0f }, { "mix", 100.0f }, { "stereoLink", 100.0f },
        { "scHpf", 0.0f }, { "oversampling", 0.0f }, { "softKnee", 0.0f }, { "mode", 0.0f }, { "colorAmount", 50.0f }, { "softClip", 0.0f }, { "clipThreshold", -3.0f }, { "clipKnee", 50.0f }
    });
    addPreset ("Fast Punch",
    {
        { "threshold", -10.0f }, { "ratio", 10.0f }, { "attack", 0.1f },
        { "release", 0.1f }, { "autoRelease", 0.0f }, { "makeup", 4.0f },
        { "autoGain", 0.0f }, { "mix", 100.0f }, { "stereoLink", 100.0f },
        { "scHpf", 7.0f }, { "oversampling", 0.0f }, { "softKnee", 0.0f }, // 100 Hz
    });
    addPreset ("Heavy Compression",
    {
        { "threshold", -24.0f }, { "ratio", 10.0f }, { "attack", 3.0f },
        { "release", 0.8f }, { "autoRelease", 0.0f }, { "makeup", 6.0f },
        { "autoGain", 0.0f }, { "mix", 80.0f }, { "stereoLink", 100.0f },
        { "scHpf", 0.0f }, { "oversampling", 0.0f }, { "softKnee", 1.0f }, { "mode", 0.0f }, { "colorAmount", 50.0f }, { "softClip", 0.0f }, { "clipThreshold", -3.0f }, { "clipKnee", 50.0f }
    });
    addPreset ("Clean Master",
    {
        { "threshold", -22.0f }, { "ratio", 2.0f }, { "attack", 20.0f },
        { "release", 0.5f }, { "autoRelease", 1.0f }, { "makeup", 1.0f },
        { "autoGain", 0.0f }, { "mix", 100.0f }, { "stereoLink", 100.0f },
        { "scHpf", 0.0f }, { "oversampling", 0.0f }, { "softKnee", 0.0f },
        { "mode", 0.0f }, { "colorAmount", 50.0f }, { "softClip", 0.0f },
        { "clipThreshold", -3.0f }, { "clipKnee", 50.0f },
    });
    addPreset ("Color Master",
    {
        { "threshold", -20.0f }, { "ratio", 3.0f }, { "attack", 10.0f },
        { "release", 0.4f }, { "autoRelease", 1.0f }, { "makeup", 2.0f },
        { "autoGain", 0.0f }, { "mix", 100.0f }, { "stereoLink", 100.0f },
        { "scHpf", 0.0f }, { "oversampling", 1.0f }, { "softKnee", 0.0f },
        { "mode", 1.0f }, { "colorAmount", 60.0f }, { "softClip", 1.0f },
        { "clipThreshold", -3.0f }, { "clipKnee", 50.0f },
    });
    addPreset ("Transparent Master Bus",
    {
        { "threshold", -24.0f }, { "ratio", 1.5f }, { "attack", 30.0f },
        { "release", 0.6f }, { "autoRelease", 1.0f }, { "makeup", 1.0f },
        { "autoGain", 0.0f }, { "mix", 100.0f }, { "stereoLink", 100.0f },
        { "scHpf", 0.0f }, { "oversampling", 1.0f }, { "softKnee", 0.0f }, // 2x
    });
}

void PresetManager::addPreset (const juce::String& name,
                               std::initializer_list<std::pair<juce::String, float>> values)
{
    juce::ValueTree tree (presetTreeType);
    for (const auto& [id, value] : values)
    {
        if (const auto* def = findParamDef (id))
            tree.setProperty (id, (float) clampToDef (*def, value), nullptr);
        else
            tree.setProperty (id, value, nullptr); // unknown ids are kept but ignored on load
    }
    presets.emplace_back (name, tree);
}

juce::StringArray PresetManager::getPresetNames() const
{
    juce::StringArray names;
    for (const auto& [name, tree] : presets)
        names.add (name);
    return names;
}

juce::String PresetManager::getPresetName (int index) const
{
    if (juce::isPositiveAndBelow (index, (int) presets.size()))
        return presets[(size_t) index].first;
    return {};
}

int PresetManager::getPresetIndex (const juce::String& name) const noexcept
{
    for (size_t i = 0; i < presets.size(); ++i)
        if (presets[i].first == name)
            return (int) i;
    return -1;
}

juce::ValueTree PresetManager::getPresetState (int index) const
{
    if (juce::isPositiveAndBelow (index, (int) presets.size()))
        return presets[(size_t) index].second;
    return {};
}

juce::ValueTree PresetManager::getPresetState (const juce::String& name) const
{
    return getPresetState (getPresetIndex (name));
}

juce::MemoryBlock PresetManager::stateToBinary (const juce::ValueTree& state)
{
    juce::MemoryBlock block;
    if (auto xml = state.createXml())
    {
        juce::MemoryOutputStream stream (block, false);
        xml->writeTo (stream, juce::XmlElement::TextFormat().withoutHeader());
        stream.flush();
    }
    return block;
}

juce::ValueTree PresetManager::stateFromBinary (const juce::MemoryBlock& data)
{
    if (data.getSize() == 0)
        return {};
    if (auto xml = juce::XmlDocument::parse (data.toString()))
        return juce::ValueTree::fromXml (*xml);
    return {};
}

} // namespace buscomp
