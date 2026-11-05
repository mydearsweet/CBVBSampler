#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace ids
{
inline constexpr const char* volume = "volume";
inline constexpr const char* pitch = "pitch";
inline constexpr const char* root = "root";
inline constexpr const char* attack = "attack";
inline constexpr const char* decay = "decay";
inline constexpr const char* sustain = "sustain";
inline constexpr const char* release = "release";
inline constexpr const char* fixedVelocity = "fixedVelocity";

inline constexpr const char* lowSel = "lowSel";
inline constexpr const char* lowBoost = "lowBoost";
inline constexpr const char* lowAtten = "lowAtten";
inline constexpr const char* hiSel = "hiSel";
inline constexpr const char* hiBoost = "hiBoost";
inline constexpr const char* hiBw = "hiBw";
inline constexpr const char* hiAttSel = "hiAttSel";
inline constexpr const char* hiAtten = "hiAtten";

inline constexpr const char* lpkSel = "lpkSel";
inline constexpr const char* lpkBoost = "lpkBoost";
inline constexpr const char* dipSel = "dipSel";
inline constexpr const char* dipCut = "dipCut";
inline constexpr const char* hpkSel = "hpkSel";
inline constexpr const char* hpkBoost = "hpkBoost";

inline constexpr const char* dlyTime = "dlyTime";
inline constexpr const char* dlyFb = "dlyFb";
inline constexpr const char* dlyDrive = "dlyDrive";
inline constexpr const char* dlyWow = "dlyWow";
inline constexpr const char* dlyMix = "dlyMix";
} // namespace ids

// Hardware stepped frequencies (Hz).
namespace freqs
{
inline const std::vector<float> lowCps   { 20.f, 30.f, 60.f, 100.f };
inline const std::vector<float> hiBoost  { 3000.f, 4000.f, 5000.f, 8000.f, 10000.f, 12000.f, 16000.f };
inline const std::vector<float> hiAtten  { 5000.f, 10000.f, 20000.f };
inline const std::vector<float> lowPeak  { 200.f, 300.f, 500.f, 700.f, 1000.f };
inline const std::vector<float> dip      { 200.f, 300.f, 500.f, 700.f, 1000.f, 1500.f, 2000.f, 3000.f, 4500.f, 7000.f };
inline const std::vector<float> hiPeak   { 1500.f, 2000.f, 3000.f, 4000.f, 5000.f };

inline juce::String label (float hz)
{
    return hz >= 1000.f ? juce::String (hz / 1000.f, hz == 1500.f || hz == 4500.f ? 1 : 0) + " kHz"
                        : juce::String ((int) hz) + " Hz";
}

inline juce::StringArray labels (const std::vector<float>& v)
{
    juce::StringArray a;
    for (auto f : v)
        a.add (label (f));
    return a;
}
} // namespace freqs

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    using Range = NormalisableRange<float>;
    using Attr = AudioParameterFloatAttributes;
    AudioProcessorValueTreeState::ParameterLayout l;

    auto flt = [&] (const char* id, const char* name, Range r, float def, const char* unit = "")
    {
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def, Attr().withLabel (unit)));
    };
    auto choice = [&] (const char* id, const char* name, const std::vector<float>& f, int def)
    {
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, freqs::labels (f), def));
    };

    // Voice & tuning
    flt (ids::volume, "Volume", Range (-36.f, 6.f, 0.01f), -6.f, "dB");
    flt (ids::pitch, "Pitch", Range (-24.f, 24.f, 0.01f), 0.f, "st");
    l.add (std::make_unique<AudioParameterInt> (ParameterID { ids::root, 1 }, "Root Note", 0, 127, 60));
    flt (ids::attack, "Attack", Range (0.f, 2000.f, 0.01f, 0.35f), 5.f, "ms");
    flt (ids::decay, "Decay", Range (0.f, 2000.f, 0.01f, 0.35f), 200.f, "ms");
    flt (ids::sustain, "Sustain", Range (0.f, 100.f, 0.1f), 100.f, "%");
    flt (ids::release, "Release", Range (0.f, 5000.f, 0.01f, 0.35f), 300.f, "ms");

    // Pultec EQP-1A
    choice (ids::lowSel, "Low CPS", freqs::lowCps, 2);
    flt (ids::lowBoost, "Low Boost", Range (0.f, 16.f, 0.01f), 0.f, "dB");
    flt (ids::lowAtten, "Low Atten", Range (0.f, 16.f, 0.01f), 0.f, "dB");
    choice (ids::hiSel, "High Boost KCS", freqs::hiBoost, 3);
    flt (ids::hiBoost, "High Boost", Range (0.f, 16.f, 0.01f), 0.f, "dB");
    flt (ids::hiBw, "Bandwidth (Q)", Range (0.5f, 3.f, 0.01f), 1.2f);
    choice (ids::hiAttSel, "High Atten KCS", freqs::hiAtten, 1);
    flt (ids::hiAtten, "High Atten", Range (0.f, 16.f, 0.01f), 0.f, "dB");

    // Pultec MEQ-5
    choice (ids::lpkSel, "Low Peak Freq", freqs::lowPeak, 2);
    flt (ids::lpkBoost, "Low Peak Boost", Range (0.f, 12.f, 0.01f), 0.f, "dB");
    choice (ids::dipSel, "Dip Freq", freqs::dip, 5);
    flt (ids::dipCut, "Dip Cut", Range (0.f, 16.f, 0.01f), 0.f, "dB");
    choice (ids::hpkSel, "High Peak Freq", freqs::hiPeak, 2);
    flt (ids::hpkBoost, "High Peak Boost", Range (0.f, 12.f, 0.01f), 0.f, "dB");

    // Tape delay
    flt (ids::dlyTime, "Delay Time", Range (1.f, 1000.f, 0.1f, 0.5f), 350.f, "ms");
    flt (ids::dlyFb, "Feedback", Range (0.f, 95.f, 0.1f), 35.f, "%");
    flt (ids::dlyDrive, "Tape Drive", Range (0.f, 100.f, 0.1f), 30.f, "%");
    flt (ids::dlyWow, "Wow & Flutter", Range (0.f, 3.f, 0.01f), 0.5f, "%");
    flt (ids::dlyMix, "Delay Mix", Range (0.f, 100.f, 0.1f), 0.f, "%");

    // Default OFF preserves the original velocity-sensitive playback in existing projects.
    l.add (std::make_unique<AudioParameterBool> (ParameterID { ids::fixedVelocity, 1 },
                                                   "Fixed Velocity", false));

    return l;
}
