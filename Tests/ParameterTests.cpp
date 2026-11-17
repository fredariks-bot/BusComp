#include <juce_core/juce_core.h>

#include "../Source/ParameterDefs.h"
#include "../Source/PresetManager.h"

#include <set>

//==============================================================================
class ParameterDefTests final : public juce::UnitTest
{
public:
    ParameterDefTests() : juce::UnitTest ("ParameterDefs", "Parameters") {}

    void runTest() override
    {
        beginTest ("All required parameters exist");
        const juce::StringArray required {
            "threshold", "ratio", "attack", "release", "autoRelease", "makeup",
            "autoGain", "mix", "stereoLink", "scHpf", "oversampling", "softKnee"
        };
        for (const auto& id : required)
            expect (buscomp::findParamDef (id) != nullptr, "missing parameter id: " + id);

        beginTest ("Parameter definitions are valid (unique ids, sane ranges, defaults in range)");
        std::set<juce::String> seen;
        for (const auto& def : buscomp::parameterDefs())
        {
            expect (def.id.isNotEmpty(), "empty id");
            expect (seen.insert (def.id).second, "duplicate id: " + def.id);
            expect (def.name.isNotEmpty(), "empty name for " + def.id);

            switch (def.type)
            {
                case buscomp::ParamType::Float:
                    expect (def.minValue < def.maxValue, def.id + ": min < max");
                    expect (def.step >= 0.0f, def.id + ": step >= 0");
                    expect (def.defaultValue >= def.minValue && def.defaultValue <= def.maxValue,
                            def.id + ": default in range");
                    break;
                case buscomp::ParamType::Bool:
                    expect (def.minValue == 0.0f && def.maxValue == 1.0f, def.id + ": bool range 0..1");
                    expect (def.defaultValue == 0.0f || def.defaultValue == 1.0f, def.id + ": bool default");
                    break;
                case buscomp::ParamType::Choice:
                    expect (def.choices.size() >= 2, def.id + ": at least 2 choices");
                    break;
            }
        }

        beginTest ("Required value ranges from the specification");
        auto rangeOf = [] (const juce::String& id) { return buscomp::findParamDef (id); };
        expect (rangeOf ("threshold")->minValue == -30.0f && rangeOf ("threshold")->maxValue == 0.0f);
        expect (rangeOf ("ratio")->minValue == 1.0f && rangeOf ("ratio")->maxValue == 20.0f);
        expect (rangeOf ("attack")->minValue == 0.1f && rangeOf ("attack")->maxValue == 30.0f);
        expect (rangeOf ("release")->minValue == 0.1f && rangeOf ("release")->maxValue == 1.2f);
        expect (rangeOf ("makeup")->minValue == 0.0f && rangeOf ("makeup")->maxValue == 12.0f);
        expect (rangeOf ("mix")->minValue == 0.0f && rangeOf ("mix")->maxValue == 100.0f);
        expect (rangeOf ("stereoLink")->minValue == 0.0f && rangeOf ("stereoLink")->maxValue == 100.0f);

        const auto* hpf = rangeOf ("scHpf");
        expect (hpf->type == buscomp::ParamType::Choice && hpf->choices[0] == "Off");
        expect (hpf->choices.contains ("300 Hz"));
        const auto* os = rangeOf ("oversampling");
        expect (os->choices.size() == 3 && os->choices[0] == "Off"
                && os->choices[1] == "2x" && os->choices[2] == "4x");
    }
};

static ParameterDefTests parameterDefTests;

//==============================================================================
class PresetManagerTests final : public juce::UnitTest
{
public:
    PresetManagerTests() : juce::UnitTest ("PresetManager", "State") {}

    void runTest() override
    {
        buscomp::PresetManager pm;

        beginTest ("All required factory presets exist");
        const juce::StringArray required {
            "Gentle Glue", "Drum Bus", "Mix Bus", "Fast Punch", "Heavy Compression", "Transparent Master Bus"
        };
        const auto names = pm.getPresetNames();
        expect (pm.getNumPresets() >= 6);
        for (const auto& r : required)
            expect (names.contains (r), "missing preset: " + r);

        beginTest ("Preset values reference known parameters and stay in range");
        for (int i = 0; i < pm.getNumPresets(); ++i)
        {
            auto state = pm.getPresetState (i);
            expect (state.isValid());
            int count = 0;
            for (const auto& def : buscomp::parameterDefs())
            {
                if (! state.hasProperty (def.id))
                    continue;
                ++count;
                {
                    const float v = (float) state.getProperty (def.id);
                    if (def.type == buscomp::ParamType::Choice)
                        expect (v >= 0.0f && v <= (float) (def.choices.size() - 1),
                                pm.getPresetName (i) + "/" + def.id + ": choice index in range");
                    else
                        expect (v >= def.minValue - 1.0e-4f && v <= def.maxValue + 1.0e-4f,
                                pm.getPresetName (i) + "/" + def.id + ": value " + juce::String (v)
                                + " in [" + juce::String (def.minValue) + ", " + juce::String (def.maxValue) + "]");
                }
            }
            expect (count >= 10, pm.getPresetName (i) + ": preset should set most parameters");
        }

        beginTest ("Preset index/name lookup roundtrip");
        for (int i = 0; i < pm.getNumPresets(); ++i)
            expect (pm.getPresetIndex (pm.getPresetName (i)) == i);
        expect (pm.getPresetIndex ("does not exist") == -1);

        beginTest ("State serialisation roundtrip (binary, as used by getStateInformation)");
        auto state = pm.getPresetState (0);
        state.setProperty ("presetName", pm.getPresetName (0), nullptr);
        state.setProperty ("version", 1, nullptr);
        auto binary = buscomp::PresetManager::stateToBinary (state);
        expect (binary.getSize() > 0);
        auto restored = buscomp::PresetManager::stateFromBinary (binary);
        expect (restored.isValid());
        expect (restored.getProperty ("presetName").toString() == pm.getPresetName (0));
        for (const auto& def : buscomp::parameterDefs())
            if (state.hasProperty (def.id))
                expect (std::fabs ((float) restored.getProperty (def.id) - (float) state.getProperty (def.id)) < 1.0e-5f,
                        "roundtrip mismatch for " + def.id);

        beginTest ("Corrupt or empty state data is rejected without crashing");
        juce::MemoryBlock garbage;
        garbage.append ("this is not xml at all {{{{{", 30);
        expect (! buscomp::PresetManager::stateFromBinary (garbage).isValid());
        expect (! buscomp::PresetManager::stateFromBinary ({}).isValid());
    }
};

static PresetManagerTests presetManagerTests;
