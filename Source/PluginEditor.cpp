#include "PluginEditor.h"

namespace buscomp
{

static juce::Colour colBackground()  { return juce::Colour (0xff1e2126); }
static juce::Colour colPanel()       { return juce::Colour (0xff262a31); }
static juce::Colour colPanelEdge()   { return juce::Colour (0xff353a44); }
static juce::Colour colText()        { return juce::Colour (0xffd5d9e0); }
static juce::Colour colTextDim()     { return juce::Colour (0xff8a919e); }
static juce::Colour colAccent()      { return juce::Colour (0xffe0a030); }
static juce::Colour colMeterGreen()  { return juce::Colour (0xff58b368); }
static juce::Colour colMeterRed()    { return juce::Colour (0xffe05545); }

//==============================================================================
BusCompLookAndFeel::BusCompLookAndFeel()
{
    setColour (juce::Slider::rotarySliderFillColourId, colAccent());
    setColour (juce::Slider::rotarySliderOutlineColourId, colPanelEdge());
    setColour (juce::Slider::thumbColourId, colText());
    setColour (juce::ComboBox::backgroundColourId, colPanel());
    setColour (juce::ComboBox::outlineColourId, colPanelEdge());
    setColour (juce::ComboBox::textColourId, colText());
    setColour (juce::TextButton::buttonColourId, colPanel());
    setColour (juce::TextButton::buttonOnColourId, colAccent());
    setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    setColour (juce::TextButton::textColourOffId, colText());
    setColour (juce::ToggleButton::textColourId, colText());
    setColour (juce::Label::textColourId, colText());
}

void BusCompLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                           juce::Slider& slider)
{
    auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (8.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const juce::Point<float> centre (bounds.getCentreX(), bounds.getCentreY());

    g.setColour (colPanelEdge());
    juce::Path backgroundArc;
    backgroundArc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                                 rotaryStartAngle, rotaryEndAngle, false);
    g.strokePath (backgroundArc, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (slider.isEnabled())
    {
        const float posAngle = juce::jmap (sliderPos, rotaryStartAngle, rotaryEndAngle);
        g.setColour (colAccent());
        juce::Path valueArc;
        valueArc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                                rotaryStartAngle, posAngle, false);
        g.strokePath (valueArc, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path pointer;
        const float pointerLength = radius * 0.72f;
        pointer.startNewSubPath (centre.x, centre.y);
        pointer.lineTo (centre.x + std::cos (posAngle - juce::MathConstants<float>::halfPi) * pointerLength,
                        centre.y + std::sin (posAngle - juce::MathConstants<float>::halfPi) * pointerLength);
        g.setColour (colText());
        g.strokePath (pointer, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    g.setColour (colPanelEdge().darker (0.2f));
    g.drawEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre), 1.0f);
}

void BusCompLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                           bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (2.0f);
    const bool on = button.getToggleState();

    g.setColour (on ? colAccent() : colPanelEdge());
    g.fillRoundedRectangle (bounds, 4.0f);

    g.setColour (on ? juce::Colours::black : colTextDim());
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawFittedText (button.getButtonText(), bounds.toNearestInt(),
                      juce::Justification::centred, 1);
    (void) shouldDrawButtonAsHighlighted;
    (void) shouldDrawButtonAsDown;
}

//==============================================================================
KnobComponent::KnobComponent (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId)
{
    def = findParamDef (paramId);
    jassert (def != nullptr);

    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setPopupDisplayEnabled (true, true, this);
    if (def != nullptr)
    {
        slider.setRange (def->minValue, def->maxValue, def->step);
        slider.setValue (def->defaultValue);
    }
    slider.setColour (juce::Slider::textBoxTextColourId, colText());
    addAndMakeVisible (slider);

    nameLabel.setText (def != nullptr ? def->name.toUpperCase() : paramId.toUpperCase(), juce::dontSendNotification);
    nameLabel.setJustificationType (juce::Justification::centred);
    nameLabel.setColour (juce::Label::textColourId, colTextDim());
    nameLabel.setFont (juce::Font (juce::FontOptions (11.0f).withStyle ("Bold")));
    addAndMakeVisible (nameLabel);

    valueLabel.setJustificationType (juce::Justification::centred);
    valueLabel.setColour (juce::Label::textColourId, colAccent());
    valueLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    addAndMakeVisible (valueLabel);
    updateValueLabel();

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, paramId, slider);
}

void KnobComponent::resized()
{
    auto area = getLocalBounds();
    nameLabel.setBounds (area.removeFromBottom (16));
    valueLabel.setBounds (area.removeFromBottom (16));
    slider.setBounds (area.reduced (4));
}

void KnobComponent::updateValueLabel()
{
    if (def != nullptr)
        valueLabel.setText (formatParamValue (*def, slider.getValue()), juce::dontSendNotification);
}

//==============================================================================
void MeterComponent::setValues (float newGrDb, float newGrPeakDb,
                                const float* newInDb, const float* newInHoldDb,
                                const float* newOutDb, const float* newOutHoldDb,
                                bool inClip0, bool inClip1, bool outClip0, bool outClip1)
{
    grDb = newGrDb;
    grPeakDb = newGrPeakDb;
    for (int ch = 0; ch < 2; ++ch)
    {
        inDb[ch] = newInDb[ch];       inHoldDb[ch] = newInHoldDb[ch];
        outDb[ch] = newOutDb[ch];     outHoldDb[ch] = newOutHoldDb[ch];
    }
    inClip[0] = inClip0; inClip[1] = inClip1;
    outClip[0] = outClip0; outClip[1] = outClip1;
    repaint();
}

float MeterComponent::dbToNorm (float db, float minDb, float maxDb) noexcept
{
    return juce::jlimit (0.0f, 1.0f, (db - minDb) / (maxDb - minDb));
}

void MeterComponent::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.fillAll (colPanel());

    const float pad = 8.0f;
    area.reduce (pad, pad);

    // ---- gain reduction meter (left, large) ----
    auto grArea = area.removeFromLeft (70.0f).reduced (4.0f, 0.0f);
    const float grTop = grArea.getY();
    const float grBottom = grArea.getBottom();
    const float grHeight = grBottom - grTop;

    g.setColour (colPanelEdge());
    g.fillRoundedRectangle (grArea, 3.0f);

    constexpr float grMax = 20.0f; // 0..-20 dB display range
    const float grNorm = dbToNorm (-grDb, -grMax, 0.0f);
    const float grY = grBottom - grNorm * grHeight;
    juce::Rectangle<float> grBar (grArea.getX(), grY, grArea.getWidth(), grBottom - grY);
    auto grad = juce::ColourGradient (colMeterGreen(), grArea.getX(), grBottom,
                                       colMeterRed(), grArea.getX(), grTop, false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (grBar, 3.0f);

    // zero mark
    g.setColour (colText());
    g.drawLine (grArea.getX(), grTop, grArea.getRight(), grTop, 1.5f);
    // grid lines every 5 dB
    g.setColour (colPanelEdge().brighter (0.4f));
    g.setFont (juce::Font (juce::FontOptions (9.0f)));
    for (int db = 5; db <= 20; db += 5)
    {
        const float y = grBottom - (db / grMax) * grHeight;
        g.drawLine (grArea.getX(), y, grArea.getRight(), y, 1.0f);
        g.setColour (colTextDim());
        g.drawText (juce::String (-db), (int) grArea.getRight() + 2, (int) y - 6, 24, 12,
                    juce::Justification::left);
        g.setColour (colPanelEdge().brighter (0.4f));
    }

    // peak-hold tick
    const float peakY = grBottom - dbToNorm (-grPeakDb, -grMax, 0.0f) * grHeight;
    g.setColour (colAccent());
    g.drawLine (grArea.getX() - 2, peakY, grArea.getRight() + 2, peakY, 2.0f);

    // numeric readout
    g.setColour (colText());
    g.setFont (juce::Font (juce::FontOptions (13.0f).withStyle ("Bold")));
    g.drawFittedText (juce::String (-grDb, 1) + " dB", grArea.withY (grBottom + 2).toNearestInt(),
                      juce::Justification::centred, 1);

    g.setColour (colTextDim());
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    g.drawFittedText ("GR", grArea.withY (grTop - 14).toNearestInt(), juce::Justification::centred, 1);

    // ---- input / output meters (right) ----
    auto rest = area.removeFromLeft (area.getWidth() - 70.0f - pad);
    auto inArea  = rest.removeFromLeft (rest.getWidth() * 0.5f).reduced (4.0f, 0.0f);
    auto outArea = rest.reduced (4.0f, 0.0f);

    constexpr float lvlMin = -60.0f, lvlMax = 0.0f;

    auto drawLevelMeter = [&] (juce::Rectangle<float> mArea, const float* db, const float* holdDb,
                               bool clip0, bool clip1, const juce::String& title)
    {
        g.setColour (colPanelEdge());
        g.fillRoundedRectangle (mArea, 3.0f);

        const float barW = (mArea.getWidth() - 12.0f) * 0.5f;
        for (int ch = 0; ch < 2; ++ch)
        {
            juce::Rectangle<float> bar (mArea.getX() + 3.0f + ch * (barW + 6.0f), mArea.getY(),
                                        barW, mArea.getHeight());
            const float norm = dbToNorm (db[ch], lvlMin, lvlMax);
            const float y = bar.getBottom() - norm * bar.getHeight();

            auto grad = juce::ColourGradient (colMeterGreen(), bar.getX(), bar.getBottom(),
                                               colMeterRed(), bar.getX(), bar.getY(), false);
            g.setGradientFill (grad);
            g.fillRoundedRectangle (juce::Rectangle<float> (bar.getX(), y, barW, bar.getBottom() - y), 2.0f);

            const float holdY = bar.getBottom() - dbToNorm (holdDb[ch], lvlMin, lvlMax) * bar.getHeight();
            g.setColour (colText());
            g.drawLine (bar.getX(), holdY, bar.getRight(), holdY, 1.5f);

            if (ch == 0 ? clip0 : clip1)
            {
                g.setColour (colMeterRed());
                g.fillEllipse (bar.getCentreX() - 4.0f, bar.getY() - 2.0f, 8.0f, 8.0f);
            }
        }

        // scale marks
        g.setColour (colPanelEdge().brighter (0.4f));
        g.setFont (juce::Font (juce::FontOptions (9.0f)));
        for (int db = -60; db <= 0; db += 20)
        {
            const float y = mArea.getBottom() - ((db - lvlMin) / (lvlMax - lvlMin)) * mArea.getHeight();
            g.drawLine (mArea.getX(), y, mArea.getRight(), y, 1.0f);
        }

        g.setColour (colTextDim());
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.drawFittedText (title, mArea.withY (mArea.getBottom() + 2).toNearestInt(),
                          juce::Justification::centred, 1);
    };

    drawLevelMeter (inArea,  inDb,  inHoldDb,  inClip[0],  inClip[1],  "IN");
    drawLevelMeter (outArea, outDb, outHoldDb, outClip[0], outClip[1], "OUT");
}

//==============================================================================
BusCompEditor::BusCompEditor (BusCompProcessor& p)
    : juce::AudioProcessorEditor (&p), processorRef (p)
{
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (titleLabel);
    titleLabel.setText ("BUS COMP", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions (22.0f).withStyle ("Bold")));
    titleLabel.setColour (juce::Label::textColourId, colText());
    titleLabel.setJustificationType (juce::Justification::centredLeft);

    auto names = processorRef.presetManager.getPresetNames();
    for (int i = 0; i < names.size(); ++i)
        presetCombo.addItem (names[i], i + 1);
    presetCombo.setSelectedId (processorRef.presetManager.getPresetIndex (processorRef.getCurrentPresetName()) + 1,
                               juce::dontSendNotification);
    presetCombo.onChange = [this]
    {
        const int index = presetCombo.getSelectedId() - 1;
        if (index >= 0)
            processorRef.loadPreset (index);
    };
    addAndMakeVisible (presetCombo);

    abButton.setButtonText ("A/B");
    abButton.setClickingTogglesState (true);
    abButton.onClick = [this]
    {
        processorRef.toggleAB();
        abButton.setToggleState (processorRef.isABActive(), juce::dontSendNotification);
    };
    addAndMakeVisible (abButton);

    const auto* osDef = findParamDef ("oversampling");
    for (int i = 0; i < osDef->choices.size(); ++i)
        osCombo.addItem (osDef->choices[i], i + 1);
    osCombo.setSelectedId ((int) processorRef.getParamValue ("oversampling") + 1, juce::dontSendNotification);
    osAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processorRef.apvts, "oversampling", osCombo);
    addAndMakeVisible (osCombo);

    const auto* hpfDef = findParamDef ("scHpf");
    scHpfLabel.setText ("SC HPF", juce::dontSendNotification);
    scHpfLabel.setColour (juce::Label::textColourId, colTextDim());
    scHpfLabel.setFont (juce::Font (juce::FontOptions (10.0f).withStyle ("Bold")));
    scHpfLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (scHpfLabel);
    for (int i = 0; i < hpfDef->choices.size(); ++i)
        scHpfCombo.addItem (hpfDef->choices[i], i + 1);
    scHpfCombo.setSelectedId ((int) processorRef.getParamValue ("scHpf") + 1, juce::dontSendNotification);
    scHpfAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processorRef.apvts, "scHpf", scHpfCombo);
    addAndMakeVisible (scHpfCombo);

    for (const auto* id : { "attack", "release", "threshold", "ratio", "makeup", "mix", "stereoLink" })
    {
        knobs.push_back (std::make_unique<KnobComponent> (processorRef.apvts, id));
        addAndMakeVisible (*knobs.back());
    }

    for (const auto* id : { "autoRelease", "softKnee", "autoGain" })
    {
        const auto* def = findParamDef (id);
        auto button = std::make_unique<juce::ToggleButton> (def->name.toUpperCase());
        toggleAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            processorRef.apvts, id, *button));
        addAndMakeVisible (*button);
        toggles.push_back (std::move (button));
    }

    addAndMakeVisible (meterComponent);

    resetButton.onClick = [this] { processorRef.resetMeters(); };
    addAndMakeVisible (resetButton);

    setSize (1000, 600);
    setResizable (true, true);
    setResizeLimits (760, 460, 1600, 1000);

    startTimerHz (30);
}

BusCompEditor::~BusCompEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void BusCompEditor::paint (juce::Graphics& g)
{
    g.fillAll (colBackground());

    auto header = getLocalBounds().removeFromTop (56);
    g.setColour (colPanel());
    g.fillRect (header);
    g.setColour (colPanelEdge());
    g.drawLine ((float) header.getX(), (float) header.getBottom(), (float) header.getRight(),
                (float) header.getBottom(), 1.0f);
}

void BusCompEditor::resized()
{
    auto area = getLocalBounds();

    auto header = area.removeFromTop (56).reduced (10, 8);
    titleLabel.setBounds (header.removeFromLeft (130));
    header.removeFromLeft (10);
    abButton.setBounds (header.removeFromRight (52).reduced (2));
    osCombo.setBounds (header.removeFromRight (64).reduced (2));
    presetCombo.setBounds (header.removeFromRight (180).reduced (2));

    auto meters = area.removeFromRight (juce::jmax (220, area.getWidth() / 4));
    auto resetArea = meters.removeFromBottom (30).reduced (8, 4);
    resetButton.setBounds (resetArea);
    meterComponent.setBounds (meters.reduced (6));

    auto controls = area.reduced (10);
    auto row2 = controls.removeFromBottom (juce::jmax (120, controls.getHeight() / 3));

    juce::FlexBox knobRow;
    knobRow.flexDirection = juce::FlexBox::Direction::row;
    knobRow.justifyContent = juce::FlexBox::JustifyContent::spaceBetween;
    for (size_t i = 0; i < 5 && i < knobs.size(); ++i)
        knobRow.items.add (juce::FlexItem (*knobs[i]).withFlex (1.0f).withMinWidth (90.0f));
    knobRow.performLayout (controls.toFloat());

    juce::FlexBox lowerRow;
    lowerRow.flexDirection = juce::FlexBox::Direction::row;
    lowerRow.justifyContent = juce::FlexBox::JustifyContent::spaceBetween;
    lowerRow.alignItems = juce::FlexBox::AlignItems::center;
    for (size_t i = 5; i < knobs.size(); ++i)
        lowerRow.items.add (juce::FlexItem (*knobs[i]).withFlex (1.0f).withMinWidth (90.0f));

    for (auto& t : toggles)
        lowerRow.items.add (juce::FlexItem (*t).withWidth (86.0f).withHeight (26.0f).withMargin (4.0f));
    lowerRow.items.add (juce::FlexItem (scHpfLabel).withWidth (60.0f).withHeight (16.0f));
    lowerRow.items.add (juce::FlexItem (scHpfCombo).withWidth (86.0f).withHeight (26.0f).withMargin (4.0f));
    lowerRow.performLayout (row2.toFloat());
}

void BusCompEditor::timerCallback()
{
    for (auto& k : knobs)
        k->updateValueLabel();

    float inDb[2], inHold[2], outDb[2], outHold[2];
    for (int ch = 0; ch < 2; ++ch)
    {
        inDb[ch]   = processorRef.getInputDb (ch);
        inHold[ch] = processorRef.getInputHoldDb (ch);
        outDb[ch]  = processorRef.getOutputDb (ch);
        outHold[ch] = processorRef.getOutputHoldDb (ch);
    }
    meterComponent.setValues (processorRef.getGainReductionDb(), processorRef.getGainReductionPeakDb(),
                              inDb, inHold, outDb, outHold,
                              processorRef.isInputClipping (0), processorRef.isInputClipping (1),
                              processorRef.isOutputClipping (0), processorRef.isOutputClipping (1));

    const auto name = processorRef.getCurrentPresetName();
    if (presetCombo.getText() != name)
    {
        const int index = processorRef.presetManager.getPresetIndex (name);
        presetCombo.setSelectedId (index >= 0 ? index + 1 : 0, juce::dontSendNotification);
    }
    abButton.setToggleState (processorRef.isABActive(), juce::dontSendNotification);
}

} // namespace buscomp
