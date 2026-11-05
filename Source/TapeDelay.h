#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <vector>
#include "Biquad.h"

// J37 / EchoBoy-flavoured tape delay.
//   ring buffer (cubic Hermite read) -> head roll-off (12 dB LP, ~4.5 kHz) -> tanh saturation -> feedback
// Wow & flutter modulate the read position; L and R run their LFOs in different phases.
class TapeDelay
{
public:
    static constexpr float kHeadHz = 4500.f;
    static constexpr float kWowHz = 0.7f, kFlutterHz = 7.3f;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        size = (int) (fs * 1.12) + 8; // 1000 ms + 3 % modulation + guard
        for (auto& b : ring)
            b.assign ((size_t) size, 0.f);
        w = 0;
        time.reset (fs, 0.15);
        fb.reset (fs, 0.03);
        drive.reset (fs, 0.03);
        wow.reset (fs, 0.03);
        mix.reset (fs, 0.03);
        head = dsp_util::makeLowpass (fs, kHeadHz, 0.70710678);
        for (auto& s : headState)
            s.reset();
        wowPh = flutPh = 0.0;
        first = true;
    }

    void setParams (float timeMs, float feedback01, float drive01, float wow01, float mix01)
    {
        if (first)
        {
            time.setCurrentAndTargetValue (timeMs);
            fb.setCurrentAndTargetValue (feedback01);
            drive.setCurrentAndTargetValue (drive01);
            wow.setCurrentAndTargetValue (wow01);
            mix.setCurrentAndTargetValue (mix01);
            first = false;
        }
        else
        {
            time.setTargetValue (timeMs);
            fb.setTargetValue (feedback01);
            drive.setTargetValue (drive01);
            wow.setTargetValue (wow01);
            mix.setTargetValue (mix01);
        }
    }

    void process (juce::AudioBuffer<float>& buf)
    {
        if (buf.getNumChannels() < 2)
            return;

        float* io[2] = { buf.getWritePointer (0), buf.getWritePointer (1) };
        const int n = buf.getNumSamples();
        constexpr double twoPi = 6.283185307179586;
        const double wowInc = twoPi * kWowHz / fs, flutInc = twoPi * kFlutterHz / fs;
        const double phaseOffset[2] = { 0.0, 1.5707963267948966 };

        for (int i = 0; i < n; ++i)
        {
            const float tMs = time.getNextValue();
            const float fbk = fb.getNextValue();
            const float g = 1.0f + 7.0f * drive.getNextValue();
            const float depth = wow.getNextValue();
            const float m = mix.getNextValue();
            const float baseDelay = tMs * 0.001f * (float) fs;

            wowPh += wowInc;
            flutPh += flutInc;
            if (wowPh > twoPi) wowPh -= twoPi;
            if (flutPh > twoPi) flutPh -= twoPi;

            for (int ch = 0; ch < 2; ++ch)
            {
                const float mod = depth * (0.65f * (float) std::sin (wowPh + phaseOffset[ch])
                                           + 0.35f * (float) std::sin (flutPh + 1.7 * phaseOffset[ch]));
                const float d = juce::jlimit (3.0f, (float) (size - 4), baseDelay * (1.0f + mod));

                const float delayed = readHermite (ring[(size_t) ch], d);
                const float head1 = headState[ch].process (head, delayed);
                const float sat = std::tanh (g * head1) / g; // unity small-signal gain, compresses loud repeats

                const float in = io[ch][i];
                ring[(size_t) ch][(size_t) w] = in + fbk * sat;
                io[ch][i] = in * (1.0f - m) + sat * m;
            }

            if (++w >= size)
                w = 0;
        }
    }

private:
    float readHermite (const std::vector<float>& b, float delaySamples) const noexcept
    {
        double rp = (double) w - (double) delaySamples;
        if (rp < 0.0)
            rp += size;
        const int i1 = juce::jmin ((int) rp, size - 1);
        const float t = (float) (rp - i1);
        const int i0 = (i1 - 1 + size) % size, i2 = (i1 + 1) % size, i3 = (i1 + 2) % size;

        const float xm1 = b[(size_t) i0], x0 = b[(size_t) i1], x1 = b[(size_t) i2], x2 = b[(size_t) i3];
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + x0;
    }

    double fs = 44100.0;
    int size = 0, w = 0;
    std::array<std::vector<float>, 2> ring;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> time { 350.f };
    juce::SmoothedValue<float> fb { 0.f }, drive { 0.f }, wow { 0.f }, mix { 0.f };
    dsp_util::BiquadCoeffs head;
    std::array<dsp_util::BiquadState, 2> headState;
    double wowPh = 0.0, flutPh = 0.0;
    bool first = true;
};
