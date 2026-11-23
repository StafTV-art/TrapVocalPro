#include "PluginEditor.h"

static const juce::Colour kCyan   (0xff22e6ff);
static const juce::Colour kPurple (0xffb04dff);
static const juce::Colour kBgTop  (0xff0d0f16);
static const juce::Colour kBgBot  (0xff181326);

//==============================================================================
NeonLookAndFeel::NeonLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, juce::Colours::white);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, juce::Colours::white);
    setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff1b1e2b));
    setColour (juce::ComboBox::textColourId, juce::Colours::white);
    setColour (juce::ComboBox::outlineColourId, kPurple.withAlpha (0.6f));
    setColour (juce::ComboBox::arrowColourId, kCyan);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff14161f));
    setColour (juce::PopupMenu::textColourId, juce::Colours::white);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, kPurple.withAlpha (0.5f));
    setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1b1e2b));
    setColour (juce::TextButton::buttonOnColourId, kPurple);
    setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
}

void NeonLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                        float startAngle, float endAngle, juce::Slider&)
{
    auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (6.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);
    const float lw = 5.0f;
    const float arcR = radius - lw * 0.5f;

    juce::Path bg;
    bg.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (juce::Colour (0xff2a2f3d));
    g.strokePath (bg, juce::PathStrokeType (lw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (pos > 0.001f)
    {
        juce::Path val;
        val.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, angle, true);
        g.setGradientFill (juce::ColourGradient (kCyan, centre.x - radius, centre.y,
                                                 kPurple, centre.x + radius, centre.y, false));
        g.strokePath (val, juce::PathStrokeType (lw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    const float kr = radius - 12.0f;
    g.setColour (juce::Colour (0xff12141c));
    g.fillEllipse (centre.x - kr, centre.y - kr, kr * 2.0f, kr * 2.0f);
    g.setColour (juce::Colour (0xff3a3f52));
    g.drawEllipse (centre.x - kr, centre.y - kr, kr * 2.0f, kr * 2.0f, 1.5f);

    juce::Path pointer;
    pointer.addRoundedRectangle (-1.8f, -kr + 3.0f, 3.6f, kr * 0.5f, 1.5f);
    g.setColour (juce::Colours::white);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));
}

//==============================================================================
void LevelMeter::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff10121a));
    g.fillRoundedRectangle (b, 3.0f);

    auto inner = b.reduced (2.0f);
    const float frac = reduction ? juce::jlimit (0.0f, 1.0f, value / 18.0f)
                                 : juce::jlimit (0.0f, 1.0f, (value + 60.0f) / 60.0f);
    if (reduction)
    {
        auto bar = inner.removeFromTop (inner.getHeight() * frac);
        g.setColour (kPurple);
        g.fillRoundedRectangle (bar, 2.0f);
    }
    else
    {
        auto full = inner;
        auto bar = inner.removeFromBottom (inner.getHeight() * frac);
        g.setGradientFill (juce::ColourGradient (kCyan, 0.0f, full.getBottom(),
                                                 kPurple, 0.0f, full.getY(), false));
        g.fillRoundedRectangle (bar, 2.0f);
    }
}

//==============================================================================
TrapVocalEditor::TrapVocalEditor (TrapVocalProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&laf);

    initKnob (kIn,      "in",      "INPUT",          true);
    initKnob (kOut,     "out",     "OUTPUT",         true);
    initKnob (kCleanup, "cleanup", "CLEANUP",        false);
    initKnob (kBody,    "body",    "BODY / TONE",    false);
    initKnob (kSlam,    "slam",    "SLAM",           false);
    initKnob (kDeess,   "deess",   "DE-ESS",         false);
    initKnob (kAir,     "air",     "FRESH AIR",      false);
    initKnob (kWide,    "wide",    "WIDE / CHORUS",  false);
    initKnob (kSpace,   "space",   "SPACE",          false);
    initKnob (kEcho,    "echo",    "ECHO",           false);

    for (int i = 0; i < 7; ++i)
        presetBox.addItem (factoryPresets[i].name, i + 1);
    presetBox.setTextWhenNothingSelected ("Preset waehlen ...");
    const int cur = (int) proc.apvts.state.getProperty ("preset", -1);
    if (cur >= 0 && cur < 7) presetBox.setSelectedItemIndex (cur, juce::dontSendNotification);
    presetBox.onChange = [this]
    {
        const int idx = presetBox.getSelectedItemIndex();
        if (idx >= 0) proc.applyPreset (idx);
    };
    addAndMakeVisible (presetBox);

    bypassBtn.setClickingTogglesState (true);
    addAndMakeVisible (bypassBtn);
    bypassAtt = std::make_unique<ButtonAtt> (proc.apvts, "bypass", bypassBtn);

    echoDivBtn.setClickingTogglesState (true);
    addAndMakeVisible (echoDivBtn);
    echoAtt = std::make_unique<ButtonAtt> (proc.apvts, "echo8", echoDivBtn);

    addAndMakeVisible (meterIn);
    addAndMakeVisible (meterGr);
    addAndMakeVisible (meterOut);

    setSize (820, 560);
    startTimerHz (30);
}

TrapVocalEditor::~TrapVocalEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void TrapVocalEditor::initKnob (Knob& k, const juce::String& id, const juce::String& text, bool isDb)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 22);
    k.slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f,
                                  juce::MathConstants<float>::pi * 2.8f, true);
    if (isDb)
    {
        k.slider.textFromValueFunction = [] (double v) { return juce::String (v, 1) + " dB"; };
        k.slider.setDoubleClickReturnValue (true, 0.0);
    }
    else
    {
        k.slider.textFromValueFunction = [] (double v) { return juce::String ((int) std::lround (v * 100.0)) + " %"; };
    }
    addAndMakeVisible (k.slider);

    k.label.setText (text, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::Font (juce::FontOptions (isDb ? 13.0f : 15.0f, juce::Font::bold)));
    k.label.setColour (juce::Label::textColourId, isDb ? juce::Colours::lightgrey : kCyan);
    addAndMakeVisible (k.label);

    k.att = std::make_unique<SliderAtt> (proc.apvts, id, k.slider);
}

//==============================================================================
void TrapVocalEditor::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (kBgTop, 0.0f, 0.0f, kBgBot, 0.0f, (float) getHeight(), false));
    g.fillAll();

    g.setColour (kPurple.withAlpha (0.35f));
    g.drawRect (getLocalBounds().toFloat().reduced (1.0f), 1.5f);

    auto top = getLocalBounds().withTrimmedLeft (130).withTrimmedRight (130).removeFromTop (60);
    g.setColour (kCyan);
    g.setFont (juce::Font (juce::FontOptions (30.0f, juce::Font::bold)));
    g.drawText ("TRAPVOCAL", top.removeFromLeft (top.getWidth() / 2 + 20), juce::Justification::centredRight);
    g.setColour (kPurple);
    g.drawText (" PRO", top, juce::Justification::centredLeft);

    g.setColour (juce::Colours::grey);
    g.setFont (11.0f);
    g.drawText ("IN", meterIn.getBounds().withY (meterIn.getBottom()).withHeight (14), juce::Justification::centred);
    g.drawText ("GR", meterGr.getBounds().withY (meterGr.getBottom()).withHeight (14), juce::Justification::centred);
    g.drawText ("OUT", meterOut.getBounds().withY (meterOut.getBottom()).withHeight (14), juce::Justification::centred);
}

void TrapVocalEditor::resized()
{
    auto area = getLocalBounds().reduced (12);

    // Obere Leiste: Input links, Output rechts, Preset/Bypass in der Mitte
    auto top = area.removeFromTop (130);
    auto left = top.removeFromLeft (120);
    kIn.label.setBounds (left.removeFromTop (20));
    kIn.slider.setBounds (left);
    auto right = top.removeFromRight (120);
    kOut.label.setBounds (right.removeFromTop (20));
    kOut.slider.setBounds (right);
    auto mid = top.reduced (20, 0);
    mid.removeFromTop (58);
    presetBox.setBounds (mid.removeFromTop (30).withSizeKeepingCentre (320, 30));
    mid.removeFromTop (8);
    bypassBtn.setBounds (mid.removeFromTop (28).withSizeKeepingCentre (220, 28));

    // Meter links/rechts
    area.removeFromTop (6);
    auto leftM = area.removeFromLeft (50);
    meterIn.setBounds (leftM.withSizeKeepingCentre (22, area.getHeight() - 40));
    auto rightM = area.removeFromRight (80);
    meterGr.setBounds (rightM.removeFromLeft (36).withSizeKeepingCentre (22, area.getHeight() - 40));
    meterOut.setBounds (rightM.withSizeKeepingCentre (22, area.getHeight() - 40));

    // 4 x 2 Raster
    Knob* grid[8] = { &kCleanup, &kBody, &kSlam, &kDeess, &kAir, &kWide, &kSpace, &kEcho };
    const int cw = area.getWidth() / 4, rh = area.getHeight() / 2;
    for (int i = 0; i < 8; ++i)
    {
        auto cell = juce::Rectangle<int> (area.getX() + (i % 4) * cw, area.getY() + (i / 4) * rh, cw, rh).reduced (6);
        grid[i]->label.setBounds (cell.removeFromTop (24));
        if (i == 7)
            echoDivBtn.setBounds (cell.removeFromBottom (26).withSizeKeepingCentre (60, 24));
        grid[i]->slider.setBounds (cell);
    }
}

void TrapVocalEditor::timerCallback()
{
    const float inP = proc.inPeak.exchange (0.0f);
    const float outP = proc.outPeak.exchange (0.0f);
    const float gr = proc.grMax.exchange (0.0f);

    const float inDb = juce::Decibels::gainToDecibels (inP, -60.0f);
    const float outDb = juce::Decibels::gainToDecibels (outP, -60.0f);
    smIn  = inDb  > smIn  ? inDb  : juce::jmax (-60.0f, smIn  - 2.0f);
    smOut = outDb > smOut ? outDb : juce::jmax (-60.0f, smOut - 2.0f);
    smGr  = gr > smGr ? gr : smGr * 0.85f;

    meterIn.setValue (smIn);
    meterOut.setValue (smOut);
    meterGr.setValue (smGr);

    echoDivBtn.setButtonText (echoDivBtn.getToggleState() ? "1/8" : "1/4");
}
