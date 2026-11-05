#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include "Biquad.h"

// One EQ band: a smoothed biquad.  Frequency, gain and Q are smoothed and the coefficients are
// refreshed every sub-block while any of them is moving, so stepped-frequency jumps and knob
// moves never zipper.
class EqBand
{
public:
    enum class Type { LowShelf, HighShelf, Peak };

    void prepare (double sampleRate, Type t)
    {
        fs = sampleRate;
        type = t;
        freq.reset (fs, 0.03);
        gain.reset (fs, 0.03);
        q.reset (fs, 0.03);
        first = true;
        stateL.reset();
        stateR.reset();
    }

    void setTarget (float f, float gainDb, float qv)
    {
        if (first)
        {
            freq.setCurrentAndTargetValue (f);
            gain.setCurrentAndTargetValue (gainDb);
            q.setCurrentAndTargetValue (qv);
            first = false;
            dirty = true;
        }
        else
        {
            freq.setTargetValue (f);
            gain.setTargetValue (gainDb);
            q.setTargetValue (qv);
        }
    }

    bool active() const noexcept
    {
        return std::abs (gain.getCurrentValue()) > 0.005f || std::abs (gain.getTargetValue()) > 0.005f;
    }

    void advance (int n)
    {
        if (dirty || freq.isSmoothing() || gain.isSmoothing() || q.isSmoothing())
        {
            const float f = freq.skip (n), g = gain.skip (n), qq = q.skip (n);
            switch (type)
            {
                case Type::LowShelf:  c = dsp_util::makeLowShelf (fs, f, qq, g); break;
                case Type::HighShelf: c = dsp_util::makeHighShelf (fs, f, qq, g); break;
                case Type::Peak:      c = dsp_util::makePeak (fs, f, qq, g); break;
            }
            dirty = false;
        }
    }

    void process (float* l, float* r, int n)
    {
        if (! active())
        {
            stateL.reset();
            stateR.reset();
            return;
        }
        for (int i = 0; i < n; ++i)
        {
            l[i] = stateL.process (c, l[i]);
            r[i] = stateR.process (c, r[i]);
        }
    }

private:
    double fs = 44100.0;
    Type type = Type::Peak;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> freq { 1000.f };
    juce::SmoothedValue<float> gain { 0.f }, q { 1.f };
    dsp_util::BiquadCoeffs c;
    dsp_util::BiquadState stateL, stateR;
    bool first = true, dirty = true;
};

// ---------------------------------------------------------------------------------------------
// Combined Pultec EQP-1A + MEQ-5.  Every boost and every cut is its own filter (no bipolar
// controls), so the classic simultaneous boost/atten interaction works.
// ---------------------------------------------------------------------------------------------
class PultecMeqEq
{
public:
    struct Params
    {
        float lowHz = 60.f, lowBoost = 0.f, lowAtten = 0.f;
        float hiBoostHz = 8000.f, hiBoost = 0.f, hiQ = 1.2f;
        float hiAttenHz = 10000.f, hiAtten = 0.f;
        float lpkHz = 500.f, lpkBoost = 0.f;
        float dipHz = 2000.f, dipCut = 0.f;
        float hpkHz = 3000.f, hpkBoost = 0.f;
    };

    // Voicing constants (starting points to tune by ear against your hardware or reference plugin):
    static constexpr float kLowShelfQ = 0.5f;       // broad, gentle shelves
    static constexpr float kAttenCornerRatio = 1.6f; // the atten shelf's effective corner sits above the boost corner
    static constexpr float kHiAttenQ = 0.6f;
    static constexpr float kLowPeakQ = 1.2f;
    static constexpr float kDipQ = 1.4f;
    static constexpr float kHiPeakQ = 1.2f;
    static constexpr float kTubeDrive = 1.1f;        // very mild output-stage soft saturation

    void prepare (double sampleRate)
    {
        bands[LowBoost].prepare (sampleRate, EqBand::Type::LowShelf);
        bands[LowAtten].prepare (sampleRate, EqBand::Type::LowShelf);
        bands[LowPeak].prepare (sampleRate, EqBand::Type::Peak);
        bands[Dip].prepare (sampleRate, EqBand::Type::Peak);
        bands[HiPeak].prepare (sampleRate, EqBand::Type::Peak);
        bands[HiBoost].prepare (sampleRate, EqBand::Type::Peak);
        bands[HiAtten].prepare (sampleRate, EqBand::Type::HighShelf);
    }

    void setParams (const Params& p)
    {
        bands[LowBoost].setTarget (p.lowHz, p.lowBoost, kLowShelfQ);
        bands[LowAtten].setTarget (p.lowHz * kAttenCornerRatio, -p.lowAtten, kLowShelfQ);
        bands[LowPeak].setTarget (p.lpkHz, p.lpkBoost, kLowPeakQ);
        bands[Dip].setTarget (p.dipHz, -p.dipCut, kDipQ);
        bands[HiPeak].setTarget (p.hpkHz, p.hpkBoost, kHiPeakQ);
        bands[HiBoost].setTarget (p.hiBoostHz, p.hiBoost, p.hiQ);
        bands[HiAtten].setTarget (p.hiAttenHz, -p.hiAtten, kHiAttenQ);
    }

    void process (juce::AudioBuffer<float>& buf)
    {
        if (buf.getNumChannels() < 2)
            return;

        float* l = buf.getWritePointer (0);
        float* r = buf.getWritePointer (1);
        const int n = buf.getNumSamples();

        for (int i = 0; i < n; i += kSub)
        {
            const int c = juce::jmin (kSub, n - i);
            for (auto& b : bands)
            {
                b.advance (c);
                b.process (l + i, r + i, c);
            }
        }

        constexpr float inv = 1.0f / kTubeDrive;
        for (int i = 0; i < n; ++i)
        {
            l[i] = std::tanh (kTubeDrive * l[i]) * inv;
            r[i] = std::tanh (kTubeDrive * r[i]) * inv;
        }
    }

private:
    enum { LowBoost, LowAtten, LowPeak, Dip, HiPeak, HiBoost, HiAtten, NumBands };
    static constexpr int kSub = 16;
    std::array<EqBand, NumBands> bands;
};
