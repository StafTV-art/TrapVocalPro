#pragma once
#include "PluginProcessor.h"

//==============================================================================
class NeonLookAndFeel : public juce::LookAndFeel_V4
{
public:
    NeonLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
};

//==============================================================================
class LevelMeter : public juce::Component
{
public:
    explicit LevelMeter (bool isReduction = false) : reduction (isReduction) {}
    void setValue (float v) { value = v; repaint(); }
    void paint (juce::Graphics& g) override;
private:
    bool reduction;
    float value = 0.0f;
};

//==============================================================================
class TrapVocalEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit TrapVocalEditor (TrapVocalProcessor&);
    ~TrapVocalEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAtt> att;
    };

    void initKnob (Knob& k, const juce::String& id, const juce::String& text, bool isDb);
    void timerCallback() override;

    TrapVocalProcessor& proc;
    NeonLookAndFeel laf;

    Knob kIn, kOut, kCleanup, kBody, kSlam, kDeess, kAir, kWide, kSpace, kEcho;
    juce::ComboBox presetBox;
    juce::TextButton bypassBtn { "BYPASS (LOUDNESS MATCH)" };
    juce::TextButton echoDivBtn { "1/4" };
    std::unique_ptr<ButtonAtt> bypassAtt, echoAtt;
    LevelMeter meterIn, meterGr { true }, meterOut;
    float smIn = -60.0f, smOut = -60.0f, smGr = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrapVocalEditor)
};
