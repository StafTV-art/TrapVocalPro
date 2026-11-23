#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <vector>
#include "DSP.h"

struct FactoryPreset
{
    const char* name;
    float cleanup, body, slam, deess, air, wide, space, echo;
    bool echo8;
};

inline const FactoryPreset factoryPresets[7] =
{
    //                                            cleanup body  slam  deess air   wide  space echo  1/8
    { "01. Main Rap Vocal (In-Your-Face)",         0.60f, 0.35f, 0.80f, 0.60f, 0.75f, 0.00f, 0.15f, 0.10f, false },
    { "02. Melodic Trap / Auto-Tune Vocal",        0.50f, 0.55f, 0.50f, 0.60f, 0.65f, 0.25f, 0.35f, 0.30f, false },
    { "03. Wide Chorus / Hook",                    0.50f, 0.50f, 0.60f, 0.55f, 0.80f, 0.85f, 0.50f, 0.30f, true  },
    { "04. Aggressive Drill / Hard Rap",           0.70f, 0.30f, 0.95f, 0.70f, 0.60f, 0.00f, 0.10f, 0.10f, true  },
    { "05. Oldschool / Boom Bap Vocal",            0.50f, 0.75f, 0.55f, 0.45f, 0.25f, 0.10f, 0.40f, 0.15f, false },
    { "06. Backing / Doubles (Tucked)",            0.60f, 0.55f, 0.60f, 0.65f, 0.40f, 0.90f, 0.40f, 0.20f, true  },
    { "07. Podcast / Dry Voice Cleanup",           0.80f, 0.50f, 0.30f, 0.50f, 0.20f, 0.00f, 0.00f, 0.00f, false }
};

class TrapVocalProcessor : public juce::AudioProcessor
{
public:
    TrapVocalProcessor();
    ~TrapVocalProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "TRAPVOCAL PRO"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    void applyPreset (int index);

    juce::AudioProcessorValueTreeState apvts;

    // Meter-Werte (linear bzw. dB), werden vom Editor gelesen
    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f }, grMax { 0.0f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void setParam (const char* id, float value);
    void updateControls (float cleanup, float body, float slam, float deess, float air, float echo);

    double sr = 44100.0;
    int maxBlock = 512;

    // Cleanup
    tvp::Biquad hp1[2], hp2[2];
    tvp::Gate gate;
    // De-Esser
    tvp::Biquad dsBand[2];
    tvp::DeEsser deEsser;
    // EQ
    tvp::Biquad eqLow[2], eqBox[2], eqPres[2];
    // Kompression
    tvp::FetComp fet;
    tvp::OptoComp opto;
    // Exciter
    tvp::Biquad midHP[2], midPost[2], highHP[2], highPost[2], airPeak[2], airShelf[2];
    juce::dsp::Oversampling<float> osEx   { (size_t) 4, (size_t) 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true };
    juce::dsp::Oversampling<float> osClip { (size_t) 2, (size_t) 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true };
    // FX
    tvp::Doubler doubler;
    tvp::StereoDelay echoDelay;
    tvp::PlateReverb reverb;
    tvp::Ducker delayDucker, reverbDucker;
    // Latenz-Ausgleich
    tvp::DelayLine exAlign[2], dryAlign[2];
    int exLat = 0, totalLat = 0;

    // Arbeitsbuffer
    juce::AudioBuffer<float> exScratch, clipScratch;
    std::vector<float> wl, wr, dryL, dryR, sibv, fxL, fxR, revInL, revInR, revOutL, revOutR, revDuck;

    // Steuerwerte
    float lastCleanup = -1, lastBody = -1, lastSlam = -1, lastDeess = -1, lastAir = -1, lastEcho = -1;
    float makeupLin = 1.0f, mhGain = 0.0f, hiGain = 0.0f, satAmt = 0.0f, echoFb = 0.4f;

    // Bypass / Loudness-Match
    float bypassMix = 0.0f, msDry = 0.0f, msProc = 0.0f, matchSm = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrapVocalProcessor)
};
