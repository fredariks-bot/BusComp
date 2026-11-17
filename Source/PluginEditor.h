#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"

namespace buscomp
{

class BusCompLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    BusCompLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool shouldDrawButtonAsHighlighted,
                           bool shouldDrawButtonAsDown) override;
};

class KnobComponent final : public juce::Component
{
public:
    KnobComponent (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId);
    juce::String paramId;
    void resized() override;
    void updateValueLabel();

    juce::Slider slider;
    juce::Label  nameLabel;
    juce::Label  valueLabel;
    const ParamDef* def = nullptr;

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

class MeterComponent final : public juce::Component
{
public:
    void setValues (float grDb, float grPeakDb,
                    const float* inDb, const float* inHoldDb,
                    const float* outDb, const float* outHoldDb,
                    bool inClip0, bool inClip1, bool outClip0, bool outClip1);
    void paint (juce::Graphics&) override;

private:
    static float dbToNorm (float db, float minDb, float maxDb) noexcept;

    float grDb = 0.0f, grPeakDb = 0.0f;
    float inDb[2] { -120.0f, -120.0f }, inHoldDb[2] { -120.0f, -120.0f };
    float outDb[2] { -120.0f, -120.0f }, outHoldDb[2] { -120.0f, -120.0f };
    bool inClip[2] { false, false }, outClip[2] { false, false };
};

class BusCompEditor final : public juce::AudioProcessorEditor,
                            private juce::Timer
{
public:
    explicit BusCompEditor (BusCompProcessor& processor);
    ~BusCompEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    BusCompProcessor& processorRef;
    BusCompLookAndFeel lookAndFeel;

    juce::Label  titleLabel { {}, "BUS COMP" };
    juce::ComboBox presetCombo;
    juce::ToggleButton abButton { "A/B" };
    juce::ComboBox modeCombo;
    juce::ComboBox osCombo;
    juce::Label  scHpfLabel { {}, "SC HPF" };
    juce::ComboBox scHpfCombo;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> osAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> scHpfAttachment;

    std::vector<std::unique_ptr<KnobComponent>> knobs;
    std::vector<std::unique_ptr<juce::ToggleButton>> toggles;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> toggleAttachments;
    std::vector<juce::Label*> toggleLabels;

    MeterComponent meterComponent;
    juce::Label  clipLabel { {}, "CLIP" };
    juce::TextButton resetButton { "RESET" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BusCompEditor)
};

} // namespace buscomp
