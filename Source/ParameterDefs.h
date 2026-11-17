#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace buscomp
{

/** Pure-data description of every automatable plugin parameter.
    Kept free of juce_audio_processors dependencies so the automated tests
    can validate the layout without instantiating a full AudioProcessor. */
enum class ParamType { Float, Bool, Choice };

struct ParamDef
{
    juce::String id;
    juce::String name;
    ParamType   type       = ParamType::Float;
    float       minValue   = 0.0f;
    float       maxValue   = 1.0f;
    float       defaultValue = 0.0f;
    float       step       = 0.0f;      // 0 = continuous
    juce::StringArray choices;          // Choice only; default is index 0
    juce::String unit;
};

/** The single source of truth for all parameter ids/ranges/defaults. */
const std::vector<ParamDef>& parameterDefs();

const ParamDef* findParamDef (const juce::String& id);

float clampToDef (const ParamDef& def, float value);

juce::String formatParamValue (const ParamDef& def, float value);

} // namespace buscomp
