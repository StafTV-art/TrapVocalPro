#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <cmath>
#include <algorithm>

namespace tvp
{
//==============================================================================
inline float dbToLin (float db) { return std::pow (10.0f, db * 0.05f); }
inline float linToDb (float x)  { return 20.0f * std::log10 (x + 1.0e-9f); }
inline float tc (float ms, double sr) { return std::exp (-1.0f / (0.001f * ms * (float) sr)); }

//==============================================================================
// Biquad (RBJ Cookbook), ohne Speicherallokation -> sicher im Audio-Thread
struct Coeffs { float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };

inline Coeffs normCoeffs (double b0, double b1, double b2, double a0, double a1, double a2)
{
    Coeffs c;
    c.b0 = (float) (b0 / a0); c.b1 = (float) (b1 / a0); c.b2 = (float) (b2 / a0);
    c.a1 = (float) (a1 / a0); c.a2 = (float) (a2 / a0);
    return c;
}

inline Coeffs makeHP (double sr, double f, double q = 0.70710678)
{
    f = std::min (f, sr * 0.45);
    const double w = juce::MathConstants<double>::twoPi * f / sr, cs = std::cos (w), al = std::sin (w) / (2.0 * q);
    return normCoeffs ((1 + cs) / 2, -(1 + cs), (1 + cs) / 2, 1 + al, -2 * cs, 1 - al);
}
inline Coeffs makeLP (double sr, double f, double q = 0.70710678)
{
    f = std::min (f, sr * 0.45);
    const double w = juce::MathConstants<double>::twoPi * f / sr, cs = std::cos (w), al = std::sin (w) / (2.0 * q);
    return normCoeffs ((1 - cs) / 2, 1 - cs, (1 - cs) / 2, 1 + al, -2 * cs, 1 - al);
}
inline Coeffs makeBP (double sr, double f, double q)
{
    f = std::min (f, sr * 0.45);
    const double w = juce::MathConstants<double>::twoPi * f / sr, cs = std::cos (w), al = std::sin (w) / (2.0 * q);
    return normCoeffs (al, 0, -al, 1 + al, -2 * cs, 1 - al);
}
inline Coeffs makePeak (double sr, double f, double q, double gainDb)
{
    f = std::min (f, sr * 0.45);
    const double A = std::pow (10.0, gainDb / 40.0);
    const double w = juce::MathConstants<double>::twoPi * f / sr, cs = std::cos (w), al = std::sin (w) / (2.0 * q);
    return normCoeffs (1 + al * A, -2 * cs, 1 - al * A, 1 + al / A, -2 * cs, 1 - al / A);
}
inline Coeffs makeLowShelf (double sr, double f, double q, double gainDb)
{
    f = std::min (f, sr * 0.45);
    const double A = std::pow (10.0, gainDb / 40.0);
    const double w = juce::MathConstants<double>::twoPi * f / sr, cs = std::cos (w), al = std::sin (w) / (2.0 * q);
    const double sA = 2.0 * std::sqrt (A) * al;
    return normCoeffs (A * ((A + 1) - (A - 1) * cs + sA),
                       2 * A * ((A - 1) - (A + 1) * cs),
                       A * ((A + 1) - (A - 1) * cs - sA),
                       (A + 1) + (A - 1) * cs + sA,
                       -2 * ((A - 1) + (A + 1) * cs),
                       (A + 1) + (A - 1) * cs - sA);
}
inline Coeffs makeHighShelf (double sr, double f, double q, double gainDb)
{
    f = std::min (f, sr * 0.45);
    const double A = std::pow (10.0, gainDb / 40.0);
    const double w = juce::MathConstants<double>::twoPi * f / sr, cs = std::cos (w), al = std::sin (w) / (2.0 * q);
    const double sA = 2.0 * std::sqrt (A) * al;
    return normCoeffs (A * ((A + 1) + (A - 1) * cs + sA),
                       -2 * A * ((A - 1) + (A + 1) * cs),
                       A * ((A + 1) + (A - 1) * cs - sA),
                       (A + 1) - (A - 1) * cs + sA,
                       2 * ((A - 1) - (A + 1) * cs),
                       (A + 1) - (A - 1) * cs - sA);
}

struct Biquad
{
    Coeffs c;
    float z1 = 0.0f, z2 = 0.0f;
    void set (const Coeffs& n) { c = n; }
    void reset() { z1 = z2 = 0.0f; }
    inline float process (float x)
    {
        const float y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return y;
    }
};

//==============================================================================
// Gain-Computer mit Soft-Knee. Liefert Gain in dB (<= 0)
inline float compGainDb (float levelDb, float thr, float ratio, float knee)
{
    const float over = levelDb - thr;
    if (2.0f * over < -knee) return 0.0f;
    if (2.0f * std::abs (over) <= knee)
    {
        const float x = over + knee * 0.5f;
        return (1.0f / ratio - 1.0f) * x * x / (2.0f * knee);
    }
    return (1.0f / ratio - 1.0f) * over;
}

//==============================================================================
class DelayLine
{
public:
    void prepare (int maxSamples) { buf.assign ((size_t) maxSamples + 8, 0.0f); w = 0; }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
    void write (float x) { buf[(size_t) w] = x; if (++w >= (int) buf.size()) w = 0; }

    // Gibt das Sample zurueck, das vor "d" Schreibvorgaengen geschrieben wurde
    float read (float d) const
    {
        const int sz = (int) buf.size();
        float rp = (float) w - d;
        while (rp < 0.0f) rp += (float) sz;
        int i0 = (int) rp;
        const float fr = rp - (float) i0;
        if (i0 >= sz) i0 -= sz;
        int i1 = i0 + 1; if (i1 >= sz) i1 -= sz;
        return buf[(size_t) i0] * (1.0f - fr) + buf[(size_t) i1] * fr;
    }

    // Festes Ganzzahl-Delay (schreiben + lesen)
    float tap (float x, int d)
    {
        write (x);
        return d <= 0 ? x : read ((float) d + 1.0f);
    }

private:
    std::vector<float> buf;
    int w = 0;
};

//==============================================================================
// Expander/Gate: weich, mit Hold, oeffnet schnell -> keine abgeschnittenen Wortanfaenge
class Gate
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        relEnv = tc (60.0f, sr); aOpen = tc (0.8f, sr); aClose = tc (140.0f, sr);
        holdSamples = (int) (0.05 * sr);
        env = 0.0f; curDb = 0.0f; holdCnt = 0;
    }
    void set (float thr, float range) { thrDb = thr; rangeDb = range; }

    float process (float level)
    {
        env = level > env ? level : relEnv * env + (1.0f - relEnv) * level;
        const float envDb = linToDb (env);
        float targetDb = 0.0f;
        if (envDb < thrDb)
        {
            if (holdCnt > 0) --holdCnt;
            else targetDb = -std::min (rangeDb, (thrDb - envDb) * 1.5f);
        }
        else holdCnt = holdSamples;

        const float c = targetDb > curDb ? aOpen : aClose;
        curDb = c * curDb + (1.0f - c) * targetDb;
        return dbToLin (curDb);
    }

private:
    double sr = 44100.0;
    float relEnv = 0, aOpen = 0, aClose = 0, env = 0, curDb = 0, thrDb = -70, rangeDb = 10;
    int holdSamples = 0, holdCnt = 0;
};

//==============================================================================
class DeEsser
{
public:
    void prepare (double sr)
    {
        aEnv = tc (0.3f, sr); rEnv = tc (40.0f, sr);
        aGain = tc (0.5f, sr); rGain = tc (35.0f, sr);
        env = 0.0f; curDb = 0.0f;
    }
    void set (float thr, float maxGr, bool isActive) { thrDb = thr; maxGrDb = maxGr; active = isActive; }

    // bandLevel = Pegel des Zisch-Bands (5-9 kHz). Rueckgabe: Gain fuer dieses Band
    float process (float bandLevel)
    {
        env = bandLevel > env ? aEnv * env + (1.0f - aEnv) * bandLevel
                              : rEnv * env + (1.0f - rEnv) * bandLevel;
        float target = 0.0f;
        if (active)
        {
            const float over = linToDb (env) - thrDb;
            if (over > 0.0f) target = -std::min (maxGrDb, over * 0.8f);
        }
        const float c = target < curDb ? aGain : rGain;
        curDb = c * curDb + (1.0f - c) * target;
        return dbToLin (curDb);
    }
    float getEnv() const { return env; }

private:
    float aEnv = 0, rEnv = 0, aGain = 0, rGain = 0, env = 0, curDb = 0, thrDb = -30, maxGrDb = 10;
    bool active = false;
};

//==============================================================================
// Stufe 1: FET-Peak-Catcher (1176-Charakter): sehr schneller Attack, schnelle Release
class FetComp
{
public:
    void prepare (double sr) { aC = tc (0.15f, sr); rC = tc (70.0f, sr); env = 0.0f; grDb = 0.0f; }
    void set (float t, float r) { thr = t; ratio = r; }
    float process (float level)
    {
        env = level > env ? aC * env + (1.0f - aC) * level : rC * env + (1.0f - rC) * level;
        grDb = compGainDb (linToDb (env), thr, ratio, 3.0f);
        return dbToLin (grDb);
    }
    float getGrDb() const { return -grDb; }   // positiv
private:
    float aC = 0, rC = 0, env = 0, grDb = 0, thr = -10, ratio = 4;
};

// Stufe 2: Opto-Leveler (LA-2A-Charakter): weich, programmabhaengige Release
class OptoComp
{
public:
    void prepare (double sr)
    {
        pa = tc (15.0f, sr); pr = tc (60.0f, sr);
        gAtk = tc (25.0f, sr); relFast = tc (90.0f, sr); relSlow = tc (700.0f, sr);
        pw = 0.0f; grCur = 0.0f;
    }
    void set (float t, float r) { thr = t; ratio = r; }
    float process (float level)
    {
        const float sq = level * level;
        pw = sq > pw ? pa * pw + (1.0f - pa) * sq : pr * pw + (1.0f - pr) * sq;
        const float lvlDb = 10.0f * std::log10 (pw + 1.0e-12f);
        const float target = -compGainDb (lvlDb, thr, ratio, 10.0f);   // positiv
        const float rel = relFast + (relSlow - relFast) * juce::jlimit (0.0f, 1.0f, grCur / 10.0f);
        const float c = target > grCur ? gAtk : rel;
        grCur = c * grCur + (1.0f - c) * target;
        return dbToLin (-grCur);
    }
    float getGrDb() const { return grCur; }
private:
    float pa = 0, pr = 0, gAtk = 0, relFast = 0, relSlow = 0, pw = 0, grCur = 0, thr = -15, ratio = 3;
};

//==============================================================================
// Ducker: Effektsignal wird leiser, solange die Stimme laeuft
class Ducker
{
public:
    void prepare (double sr, float atkMs, float relMs) { a = tc (atkMs, sr); r = tc (relMs, sr); env = 0.0f; }
    float process (float level, float thrDb, float maxRedDb)
    {
        env = level > env ? a * env + (1.0f - a) * level : r * env + (1.0f - r) * level;
        const float red = juce::jlimit (0.0f, maxRedDb, (linToDb (env) - thrDb) * 1.2f);
        return dbToLin (-red);
    }
private:
    float a = 0, r = 0, env = 0;
};

//==============================================================================
// Doubler: zwei leicht modulierte Mikro-Delays hart links/rechts, Mitte bleibt stabil
class Doubler
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        for (int c = 0; c < 2; ++c)
        {
            dl[c].prepare ((int) (0.08 * sr));
            lp[c].set (makeLP (sr, 9500.0));
            lp[c].reset();
        }
        phase[0] = 0.0f; phase[1] = 0.37f;
    }
    void process (float inL, float inR, float amount, float& outL, float& outR)
    {
        const float mono = 0.5f * (inL + inR);
        const float base[2] = { 17.0f, 26.0f };
        const float rate[2] = { 0.31f, 0.43f };
        float v[2];
        for (int c = 0; c < 2; ++c)
        {
            dl[c].write (mono);
            const float ms = base[c] + 1.6f * std::sin (juce::MathConstants<float>::twoPi * phase[c]);
            v[c] = lp[c].process (dl[c].read (ms * 0.001f * (float) sr));
            phase[c] += rate[c] / (float) sr;
            if (phase[c] >= 1.0f) phase[c] -= 1.0f;
        }
        outL = inL + amount * 0.7f * v[0];
        outR = inR + amount * 0.7f * v[1];
    }
private:
    double sr = 44100.0;
    DelayLine dl[2];
    Biquad lp[2];
    float phase[2] = { 0.0f, 0.37f };
};

//==============================================================================
// Ping-Pong-Delay mit Telephone/Dark-Filtern im Feedback
class StereoDelay
{
public:
    void prepare (double sr)
    {
        maxS = (int) (2.2 * sr);
        for (int c = 0; c < 2; ++c)
        {
            dl[c].prepare (maxS);
            hp[c].set (makeHP (sr, 280.0)); lp[c].set (makeLP (sr, 4200.0));
            hp[c].reset(); lp[c].reset();
        }
        cur = target = (float) sr * 0.5f;
    }
    void setDelay (float samples) { target = juce::jlimit (16.0f, (float) (maxS - 4), samples); }
    void setFeedback (float f) { fb = f; }

    void process (float in, float& wetL, float& wetR)
    {
        cur += (target - cur) * 0.0008f;
        const float tl = dl[0].read (cur), tr = dl[1].read (cur);
        const float fl = lp[0].process (hp[0].process (tl));
        const float fr = lp[1].process (hp[1].process (tr));
        dl[0].write (0.7f * in + fr * fb);
        dl[1].write (0.3f * in + fl * fb);
        wetL = tl; wetR = tr;
    }
private:
    DelayLine dl[2];
    Biquad hp[2], lp[2];
    int maxS = 1;
    float cur = 1, target = 1, fb = 0.4f;
};

//==============================================================================
// Plate/Hall-Reverb (juce::Reverb) mit Pre-Delay und Filtern
class PlateReverb
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        rev.setSampleRate (sr);
        juce::Reverb::Parameters p;
        p.roomSize = 0.80f; p.damping = 0.45f; p.wetLevel = 0.33f;
        p.dryLevel = 0.0f; p.width = 1.0f; p.freezeMode = 0.0f;
        rev.setParameters (p);
        rev.reset();
        for (int c = 0; c < 2; ++c)
        {
            pre[c].prepare ((int) (0.1 * sr));
            hp[c].set (makeHP (sr, 180.0)); lp[c].set (makeLP (sr, 9000.0));
            hp[c].reset(); lp[c].reset();
        }
        preSamples = (int) (0.028 * sr);
    }
    void process (const float* inL, const float* inR, float* outL, float* outR, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            outL[i] = lp[0].process (hp[0].process (pre[0].tap (inL[i], preSamples)));
            outR[i] = lp[1].process (hp[1].process (pre[1].tap (inR[i], preSamples)));
        }
        rev.processStereo (outL, outR, n);
    }
private:
    double sr = 44100.0;
    juce::Reverb rev;
    DelayLine pre[2];
    Biquad hp[2], lp[2];
    int preSamples = 1;
};

} // namespace tvp
