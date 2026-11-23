#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace tvp;

//==============================================================================
TrapVocalProcessor::TrapVocalProcessor()
    : AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout TrapVocalProcessor::createLayout()
{
    using FP = juce::AudioParameterFloat;
    using NR = juce::NormalisableRange<float>;
    juce::AudioProcessorValueTreeState::ParameterLayout l;

    l.add (std::make_unique<FP> (juce::ParameterID { "in", 1 },  "Input Gain",  NR (-12.0f, 12.0f, 0.1f), 0.0f));
    l.add (std::make_unique<FP> (juce::ParameterID { "out", 1 }, "Output Volume", NR (-12.0f, 12.0f, 0.1f), 0.0f));
    l.add (std::make_unique<FP> (juce::ParameterID { "cleanup", 1 }, "Cleanup",       NR (0.0f, 1.0f, 0.001f), 0.5f));
    l.add (std::make_unique<FP> (juce::ParameterID { "body", 1 },    "Body / Tone",   NR (0.0f, 1.0f, 0.001f), 0.5f));
    l.add (std::make_unique<FP> (juce::ParameterID { "slam", 1 },    "Slam",          NR (0.0f, 1.0f, 0.001f), 0.5f));
    l.add (std::make_unique<FP> (juce::ParameterID { "deess", 1 },   "De-Ess",        NR (0.0f, 1.0f, 0.001f), 0.5f));
    l.add (std::make_unique<FP> (juce::ParameterID { "air", 1 },     "Fresh Air",     NR (0.0f, 1.0f, 0.001f), 0.5f));
    l.add (std::make_unique<FP> (juce::ParameterID { "wide", 1 },    "Wide / Chorus", NR (0.0f, 1.0f, 0.001f), 0.0f));
    l.add (std::make_unique<FP> (juce::ParameterID { "space", 1 },   "Space",         NR (0.0f, 1.0f, 0.001f), 0.2f));
    l.add (std::make_unique<FP> (juce::ParameterID { "echo", 1 },    "Echo",          NR (0.0f, 1.0f, 0.001f), 0.15f));
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "echo8", 1 },  "Echo 1/8", false));
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "bypass", 1 }, "Bypass",   false));
    return l;
}

void TrapVocalProcessor::setParam (const char* id, float value)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

void TrapVocalProcessor::applyPreset (int index)
{
    if (index < 0 || index >= 7) return;
    const auto& p = factoryPresets[index];
    setParam ("cleanup", p.cleanup);
    setParam ("body", p.body);
    setParam ("slam", p.slam);
    setParam ("deess", p.deess);
    setParam ("air", p.air);
    setParam ("wide", p.wide);
    setParam ("space", p.space);
    setParam ("echo", p.echo);
    setParam ("echo8", p.echo8 ? 1.0f : 0.0f);
    apvts.state.setProperty ("preset", index, nullptr);
}

//==============================================================================
void TrapVocalProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    maxBlock = juce::jmax (samplesPerBlock, 64);

    exScratch.setSize (4, maxBlock);
    clipScratch.setSize (2, maxBlock);
    for (auto* v : { &wl, &wr, &dryL, &dryR, &sibv, &fxL, &fxR, &revInL, &revInR, &revOutL, &revOutR, &revDuck })
        v->assign ((size_t) maxBlock, 0.0f);

    osEx.initProcessing ((size_t) maxBlock);   osEx.reset();
    osClip.initProcessing ((size_t) maxBlock); osClip.reset();
    exLat = (int) std::lround (osEx.getLatencyInSamples());
    const int clipLat = (int) std::lround (osClip.getLatencyInSamples());
    totalLat = exLat + clipLat;
    setLatencySamples (totalLat);

    for (int c = 0; c < 2; ++c)
    {
        exAlign[c].prepare (exLat + 8);   exAlign[c].clear();
        dryAlign[c].prepare (totalLat + 8); dryAlign[c].clear();
        for (auto* b : { &hp1[c], &hp2[c], &dsBand[c], &eqLow[c], &eqBox[c], &eqPres[c],
                         &midHP[c], &midPost[c], &highHP[c], &highPost[c], &airPeak[c], &airShelf[c] })
            b->reset();
        dsBand[c].set (makeBP (sr, 6800.0, 1.1));
        midHP[c].set (makeHP (sr, 3000.0));  midPost[c].set (makeHP (sr, 3000.0));
        highHP[c].set (makeHP (sr, 8000.0)); highPost[c].set (makeHP (sr, 8000.0));
    }

    gate.prepare (sr);
    deEsser.prepare (sr);
    fet.prepare (sr);
    opto.prepare (sr);
    doubler.prepare (sr);
    echoDelay.prepare (sr);
    reverb.prepare (sr);
    delayDucker.prepare (sr, 3.0f, 180.0f);
    reverbDucker.prepare (sr, 3.0f, 260.0f);

    bypassMix = 0.0f; msDry = 0.0f; msProc = 0.0f; matchSm = 1.0f;
    lastCleanup = lastBody = lastSlam = lastDeess = lastAir = lastEcho = -1.0f;
}

bool TrapVocalProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return out == layouts.getMainInputChannelSet();
}

//==============================================================================
void TrapVocalProcessor::updateControls (float cleanup, float body, float slam, float deess, float air, float echo)
{
    if (cleanup != lastCleanup)
    {
        const auto c = makeHP (sr, 60.0 + 60.0 * cleanup);          // 60-120 Hz, 24 dB/Okt
        for (int ch = 0; ch < 2; ++ch) { hp1[ch].set (c); hp2[ch].set (c); }
        gate.set (-80.0f + 40.0f * cleanup, 6.0f + 18.0f * cleanup);
        lastCleanup = cleanup;
    }
    if (body != lastBody)
    {
        const auto low  = makeLowShelf (sr, 140.0, 0.7, (body - 0.5) * 6.0);              // -3..+3 dB Fundament
        const auto box  = makePeak (sr, 320.0, 1.1, -2.0 - 2.5 * (1.0 - body));          // Boxiness-Cut
        const auto pres = makePeak (sr, 4000.0, 0.8, 2.0 + 2.0 * (1.0 - body));          // Praesenz
        for (int ch = 0; ch < 2; ++ch) { eqLow[ch].set (low); eqBox[ch].set (box); eqPres[ch].set (pres); }
        lastBody = body;
    }
    if (slam != lastSlam)
    {
        const float thr1 = -6.0f - 18.0f * slam, r1 = 4.0f + 6.0f * slam;
        const float thr2 = -10.0f - 14.0f * slam, r2 = 2.0f + 2.0f * slam;
        fet.set (thr1, r1);
        opto.set (thr2, r2);
        const float makeupDb = 0.15f * ((-thr1) * (1.0f - 1.0f / r1) + (-thr2) * (1.0f - 1.0f / r2));
        makeupLin = dbToLin (makeupDb);
        satAmt = slam * slam * 0.5f;
        lastSlam = slam;
    }
    if (deess != lastDeess)
    {
        deEsser.set (-24.0f - 10.0f * deess, 4.0f + 14.0f * deess, deess > 0.001f);
        lastDeess = deess;
    }
    if (air != lastAir)
    {
        const auto pk = makePeak (sr, 4500.0, 0.7, air * 1.5);
        const auto sh = makeHighShelf (sr, 10000.0, 0.7, air * 2.5);
        for (int ch = 0; ch < 2; ++ch) { airPeak[ch].set (pk); airShelf[ch].set (sh); }
        mhGain = air * 0.6f;
        hiGain = air * 0.9f;
        lastAir = air;
    }
    if (echo != lastEcho)
    {
        echoFb = 0.35f + 0.2f * echo;
        echoDelay.setFeedback (echoFb);
        lastEcho = echo;
    }
}

static inline float softClip (float x)
{
    const float t = 0.7f, headroom = 0.27f;
    const float a = std::abs (x);
    if (a <= t) return x;
    const float y = t + headroom * std::tanh ((a - t) / headroom);
    return x < 0.0f ? -y : y;
}

//==============================================================================
void TrapVocalProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh = juce::jmin (buffer.getNumChannels(), 2);
    const int total = buffer.getNumSamples();
    if (numCh == 0 || total == 0) return;
    const bool stereo = numCh > 1;

    auto P = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    const float inGain  = dbToLin (P ("in"));
    const float outGain = dbToLin (P ("out"));
    const float cleanup = P ("cleanup"), body = P ("body"), slam = P ("slam"), deess = P ("deess");
    const float air = P ("air"), wide = P ("wide"), space = P ("space"), echo = P ("echo");
    const bool echo8 = P ("echo8") > 0.5f;
    const bool bypass = P ("bypass") > 0.5f;

    updateControls (cleanup, body, slam, deess, air, echo);

    // Tempo vom Host
    double bpm = 120.0;
    if (auto* ph = getPlayHead())
    {
        auto pos = ph->getPosition();
        if (pos.hasValue())
        {
            auto b = pos->getBpm();
            if (b.hasValue() && *b > 20.0) bpm = *b;
        }
    }
    echoDelay.setDelay ((float) (sr * 60.0 / bpm * (echo8 ? 0.5 : 1.0)));

    const float echoAmt  = echo * 0.9f;
    const float spaceAmt = space * 0.8f;
    const float kBypass  = 1.0f / (0.02f * (float) sr);
    const float kMs      = 1.0f / (0.4f * (float) sr);

    float pkIn = 0.0f, pkOut = 0.0f, grBlock = 0.0f;

    int done = 0;
    while (done < total)
    {
        const int n = juce::jmin (maxBlock, total - done);
        const float* inL = buffer.getReadPointer (0, done);
        const float* inR = stereo ? buffer.getReadPointer (1, done) : inL;

        //---------------- Stufe A: Cleanup, De-Esser, EQ, Kompression (pro Sample)
        for (int i = 0; i < n; ++i)
        {
            dryL[(size_t) i] = inL[i];
            dryR[(size_t) i] = inR[i];

            float l = inL[i] * inGain;
            float r = inR[i] * inGain;
            pkIn = juce::jmax (pkIn, std::abs (l), std::abs (r));

            l = hp2[0].process (hp1[0].process (l));
            r = hp2[1].process (hp1[1].process (r));

            const float g = gate.process (juce::jmax (std::abs (l), std::abs (r)));
            l *= g; r *= g;

            const float bl = dsBand[0].process (l), br = dsBand[1].process (r);
            const float dg = deEsser.process (juce::jmax (std::abs (bl), std::abs (br)));
            l -= bl * (1.0f - dg);
            r -= br * (1.0f - dg);
            sibv[(size_t) i] = deEsser.getEnv();

            l = eqPres[0].process (eqBox[0].process (eqLow[0].process (l)));
            r = eqPres[1].process (eqBox[1].process (eqLow[1].process (r)));

            const float g1 = fet.process (juce::jmax (std::abs (l), std::abs (r)));
            l *= g1; r *= g1;
            const float g2 = opto.process (juce::jmax (std::abs (l), std::abs (r)));
            l *= g2 * makeupLin; r *= g2 * makeupLin;
            grBlock = juce::jmax (grBlock, fet.getGrDb() + opto.getGrDb());

            wl[(size_t) i] = l;
            wr[(size_t) i] = r;
        }

        //---------------- Stufe B: Fresh-Air-Exciter (4x Oversampling)
        exScratch.clear (0, n);
        for (int i = 0; i < n; ++i)
        {
            const float l = wl[(size_t) i], r = wr[(size_t) i];
            exScratch.setSample (0, i, midHP[0].process (l));
            exScratch.setSample (1, i, midHP[1].process (r));
            exScratch.setSample (2, i, highHP[0].process (l));
            exScratch.setSample (3, i, highHP[1].process (r));
            wl[(size_t) i] = airShelf[0].process (airPeak[0].process (l));
            wr[(size_t) i] = airShelf[1].process (airPeak[1].process (r));
        }
        {
            juce::dsp::AudioBlock<float> block (exScratch.getArrayOfWritePointers(), (size_t) 4, (size_t) n);
            auto up = osEx.processSamplesUp (block);
            const size_t un = up.getNumSamples();
            for (size_t c = 0; c < 4; ++c)
            {
                float* p = up.getChannelPointer (c);
                const float k = (c < 2) ? 3.0f : 4.0f;
                for (size_t i = 0; i < un; ++i)
                {
                    const float s = std::tanh (p[i] * k + 0.15f) - std::tanh (0.15f);
                    p[i] = s / k;
                }
            }
            osEx.processSamplesDown (block);
        }
        for (int i = 0; i < n; ++i)
        {
            // Zischlaute nicht zusaetzlich anheben: Exciter wird bei hoher Zisch-Energie leiser
            const float dyn = 1.0f / (1.0f + 6.0f * sibv[(size_t) i]);
            const float dynMid = 0.5f + 0.5f * dyn;
            const float ml = midPost[0].process (exScratch.getSample (0, i));
            const float mr = midPost[1].process (exScratch.getSample (1, i));
            const float hl = highPost[0].process (exScratch.getSample (2, i));
            const float hr = highPost[1].process (exScratch.getSample (3, i));
            const float baseL = exAlign[0].tap (wl[(size_t) i], exLat);
            const float baseR = exAlign[1].tap (wr[(size_t) i], exLat);
            wl[(size_t) i] = baseL + mhGain * dynMid * ml + hiGain * dyn * hl;
            wr[(size_t) i] = baseR + mhGain * dynMid * mr + hiGain * dyn * hr;
        }

        //---------------- Stufe C: Doubler, Delay, Reverb (mit Ducking)
        for (int i = 0; i < n; ++i)
        {
            const float l = wl[(size_t) i], r = wr[(size_t) i];
            float ol = l, orr = r;
            if (stereo && wide > 0.001f)
                doubler.process (l, r, wide, ol, orr);

            const float mono = 0.5f * (l + r);
            const float lvl = juce::jmax (std::abs (l), std::abs (r));

            const float dGain = delayDucker.process (lvl, -34.0f, 16.0f);
            float wetL, wetR;
            echoDelay.process (mono, wetL, wetR);
            ol  += wetL * echoAmt * dGain;
            orr += wetR * echoAmt * dGain;

            fxL[(size_t) i] = ol;
            fxR[(size_t) i] = orr;
            revInL[(size_t) i] = mono;
            revInR[(size_t) i] = mono;
            revDuck[(size_t) i] = reverbDucker.process (lvl, -34.0f, 12.0f);
        }
        reverb.process (revInL.data(), revInR.data(), revOutL.data(), revOutR.data(), n);
        for (int i = 0; i < n; ++i)
        {
            fxL[(size_t) i] += revOutL[(size_t) i] * spaceAmt * revDuck[(size_t) i];
            fxR[(size_t) i] += revOutR[(size_t) i] * spaceAmt * revDuck[(size_t) i];
        }

        //---------------- Stufe D: Saettigung + Safety-Limiter (4x Oversampling)
        for (int i = 0; i < n; ++i)
        {
            const float a = fxL[(size_t) i], b = fxR[(size_t) i];
            clipScratch.setSample (0, i, stereo ? a : 0.5f * (a + b));
            clipScratch.setSample (1, i, stereo ? b : 0.5f * (a + b));
        }
        {
            juce::dsp::AudioBlock<float> block (clipScratch.getArrayOfWritePointers(), (size_t) 2, (size_t) n);
            auto up = osClip.processSamplesUp (block);
            const size_t un = up.getNumSamples();
            for (size_t c = 0; c < 2; ++c)
            {
                float* p = up.getChannelPointer (c);
                for (size_t i = 0; i < un; ++i)
                {
                    float x = p[i];
                    x = (1.0f - satAmt) * x + satAmt * std::tanh (1.5f * x) / 1.5f;
                    p[i] = softClip (x);
                }
            }
            osClip.processSamplesDown (block);
        }

        //---------------- Ausgang: Output-Gain, Loudness-Match-Bypass, Meter
        float* outL = buffer.getWritePointer (0, done);
        float* outR = stereo ? buffer.getWritePointer (1, done) : nullptr;
        for (int i = 0; i < n; ++i)
        {
            const float pl = clipScratch.getSample (0, i) * outGain;
            const float pr = (stereo ? clipScratch.getSample (1, i) : clipScratch.getSample (0, i)) * outGain;

            msProc += (0.5f * (pl * pl + pr * pr) - msProc) * kMs;
            const float dl = dryL[(size_t) i], dr = dryR[(size_t) i];
            msDry  += (0.5f * (dl * dl + dr * dr) - msDry) * kMs;
            if (msDry > 1.0e-7f)
            {
                const float target = juce::jlimit (0.25f, 4.0f, std::sqrt ((msProc + 1.0e-10f) / (msDry + 1.0e-10f)));
                matchSm += (target - matchSm) * 0.0004f;
            }
            bypassMix += ((bypass ? 1.0f : 0.0f) - bypassMix) * kBypass;

            const float aL = dryAlign[0].tap (dl, totalLat) * matchSm;
            const float aR = dryAlign[1].tap (dr, totalLat) * matchSm;
            const float ol = pl * (1.0f - bypassMix) + aL * bypassMix;
            const float orr = pr * (1.0f - bypassMix) + aR * bypassMix;

            outL[i] = ol;
            if (outR != nullptr) outR[i] = orr;
            pkOut = juce::jmax (pkOut, std::abs (ol), std::abs (orr));
        }
        done += n;
    }

    inPeak.store (juce::jmax (inPeak.load(), pkIn));
    outPeak.store (juce::jmax (outPeak.load(), pkOut));
    grMax.store (juce::jmax (grMax.load(), grBlock));
}

//==============================================================================
juce::AudioProcessorEditor* TrapVocalProcessor::createEditor()
{
    return new TrapVocalEditor (*this);
}

void TrapVocalProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void TrapVocalProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TrapVocalProcessor();
}
