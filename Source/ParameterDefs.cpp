#include "ParameterDefs.h"

namespace buscomp
{

const std::vector<ParamDef>& parameterDefs()
{
    static const std::vector<ParamDef> defs = {
        { "threshold",    "Threshold",     ParamType::Float, -30.0f,   0.0f, -18.0f, 0.1f,  {}, "dB"  },
        { "ratio",        "Ratio",         ParamType::Float,   1.0f,  20.0f,   4.0f, 0.1f,  {}, ":1"  },
        { "attack",       "Attack",        ParamType::Float,   0.1f,  30.0f,  10.0f, 0.01f, {}, "ms"  },
        { "release",      "Release",       ParamType::Float,   0.1f,   1.2f,   0.4f, 0.001f,{}, "s"   },
        { "autoRelease",  "Auto Release",  ParamType::Bool,    0.0f,   1.0f,   0.0f, 0.0f,  {}, ""    },
        { "makeup",       "Makeup Gain",   ParamType::Float,   0.0f,  12.0f,   0.0f, 0.1f,  {}, "dB"  },
        { "autoGain",     "Auto Gain",     ParamType::Bool,    0.0f,   1.0f,   0.0f, 0.0f,  {}, ""    },
        { "mix",          "Mix",           ParamType::Float,   0.0f, 100.0f, 100.0f, 0.1f,  {}, "%"   },
        { "stereoLink",   "Stereo Link",   ParamType::Float,   0.0f, 100.0f, 100.0f, 0.1f,  {}, "%"   },
        { "scHpf",        "Sidechain HPF", ParamType::Choice,  0.0f,   0.0f,   0.0f, 0.0f,
          { "Off", "20 Hz", "30 Hz", "40 Hz", "50 Hz", "60 Hz", "80 Hz", "100 Hz",
            "120 Hz", "150 Hz", "200 Hz", "250 Hz", "300 Hz" }, "" },
        { "oversampling", "Oversampling",  ParamType::Choice,  0.0f,   0.0f,   0.0f, 0.0f,
          { "Off", "2x", "4x" }, "" },
        { "softKnee",     "Soft Knee",     ParamType::Bool,    0.0f,   1.0f,   0.0f, 0.0f,  {}, ""    },
        { "mode",         "Mode",          ParamType::Choice,  0.0f,   0.0f,   0.0f, 0.0f,
          { "CLEAN", "COLOR" }, "" },
        { "colorAmount",  "Color Amount",  ParamType::Float,   0.0f, 100.0f,  50.0f, 0.1f,  {}, "%"   },
        { "softClip",     "Soft Clip",     ParamType::Bool,    0.0f,   1.0f,   0.0f, 0.0f,  {}, ""    },
        { "clipThreshold","Clip Threshold",ParamType::Float, -12.0f,   0.0f,  -3.0f, 0.1f,  {}, "dBFS"},
        { "clipKnee",     "Clip Knee",     ParamType::Float,   0.0f, 100.0f,  50.0f, 0.1f,  {}, "%"   },
    };
    return defs;
}

const ParamDef* findParamDef (const juce::String& id)
{
    for (const auto& d : parameterDefs())
        if (d.id == id)
            return &d;
    return nullptr;
}

float clampToDef (const ParamDef& def, float value)
{
    if (def.type == ParamType::Choice)
        return juce::jlimit (0.0f, (float) juce::jmax (0, def.choices.size() - 1), value);
    return juce::jlimit (def.minValue, def.maxValue, value);
}

juce::String formatParamValue (const ParamDef& def, float value)
{
    switch (def.type)
    {
        case ParamType::Bool:
            return value >= 0.5f ? "On" : "Off";
        case ParamType::Choice:
        {
            auto index = (int) juce::jlimit (0.0f, (float) juce::jmax (0, def.choices.size() - 1),
                                            def.type == ParamType::Choice ? value : 0.0f);
            return def.choices[juce::jlimit (0, def.choices.size() - 1, index)];
        }
        case ParamType::Float:
        default:
            return juce::String (value, 2) + (def.unit.isEmpty() ? "" : " " + def.unit);
    }
}

} // namespace buscomp
