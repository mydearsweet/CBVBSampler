#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <memory>
#include <vector>
#include <cmath>

// ---------------------------------------------------------------------------------------------
// Data model: an Instrument is one or more Zones (a plain .wav is one zone spanning the keyboard,
// a .dspreset can map many).  Instruments are immutable once built, so the audio thread can read
// them while the message thread builds the next one.
// ---------------------------------------------------------------------------------------------
struct Zone
{
    std::shared_ptr<const juce::AudioBuffer<float>> audio;
    double sampleRate = 44100.0;
    int lo = 0, hi = 127, root = 60;
    int loVel = 0, hiVel = 127;
    bool useRootParam = true; // single-file instruments follow the Root Note knob
};

struct Instrument
{
    std::vector<Zone> zones;
    juce::String name;
    std::vector<float> peaks; // abs-max overview of the first zone for the GUI
};

namespace sampler
{
inline float hermite (float xm1, float x0, float x1, float x2, float t) noexcept
{
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + x0;
}

inline void fetchStereo (const Zone& z, double pos, float& l, float& r) noexcept
{
    const auto& b = *z.audio;
    const int n = b.getNumSamples();
    const int i = (int) pos;
    const float t = (float) (pos - i);
    auto get = [n] (const float* d, int k) { return (k >= 0 && k < n) ? d[k] : 0.0f; };

    const float* dl = b.getReadPointer (0);
    l = hermite (get (dl, i - 1), get (dl, i), get (dl, i + 1), get (dl, i + 2), t);
    if (b.getNumChannels() > 1)
    {
        const float* dr = b.getReadPointer (1);
        r = hermite (get (dr, i - 1), get (dr, i), get (dr, i + 1), get (dr, i + 2), t);
    }
    else
        r = l;
}
} // namespace sampler

// ---------------------------------------------------------------------------------------------
// 8-voice polyphonic engine.  Allocation order: free voice -> same note (retrigger) ->
// oldest voice already in release -> oldest voice.  A stolen voice is handed to a "ghost" that
// fades the old sound out over a few ms so stealing never clicks.
// ---------------------------------------------------------------------------------------------
class SamplerEngine
{
public:
    static constexpr int kVoices = 8;
    static constexpr int kChunk = 512;
    static constexpr int kFade = 96;

    struct Params
    {
        float attackMs = 5.f, decayMs = 200.f, sustain01 = 1.f, releaseMs = 300.f;
        int rootNote = 60;
        float pitchSemis = 0.f;
        bool fixedVelocity = false;
    };

    void prepare (double hostSampleRate)
    {
        hostSR = hostSampleRate;
        for (auto& v : voices)
            v.adsr.setSampleRate (hostSR);
        pitchSmooth.reset (hostSR, 0.03);
        pitchSmooth.setCurrentAndTargetValue (params.pitchSemis);
        soundOff();
    }

    void setInstrument (std::shared_ptr<const Instrument> i)
    {
        soundOff();
        inst = std::move (i);
    }

    bool isCurrent (const std::shared_ptr<const Instrument>& i) const noexcept { return inst == i; }

    void setParams (const Params& p)
    {
        params = p;
        pitchSmooth.setTargetValue (p.pitchSemis);

        const float a = juce::jmax (0.5f, p.attackMs) * 0.001f;
        const float d = juce::jmax (0.5f, p.decayMs) * 0.001f;
        const float r = juce::jmax (0.5f, p.releaseMs) * 0.001f;
        juce::ADSR::Parameters ap { a, d, juce::jlimit (0.f, 1.f, p.sustain01), r };

        if (ap.attack != adsrParams.attack || ap.decay != adsrParams.decay
            || ap.sustain != adsrParams.sustain || ap.release != adsrParams.release)
        {
            adsrParams = ap;
            for (auto& v : voices)
                v.adsr.setParameters (ap);
        }
    }

    void setPitchBend (float semis) noexcept { bendSemis = semis; }

    void setSustain (bool down)
    {
        sustainDown = down;
        if (! down)
            for (auto& v : voices)
                if (v.active && v.sustained)
                {
                    v.sustained = false;
                    v.adsr.noteOff();
                }
    }

    void noteOn (int note, int velocity127)
    {
        if (inst == nullptr)
            return;

        // In fixed mode both zone selection and note loudness ignore incoming key velocity.
        // MIDI note-on/off semantics remain untouched.
        const int effectiveVelocity = params.fixedVelocity ? 100 : velocity127;
        const Zone* z = findZone (note, effectiveVelocity);
        if (z == nullptr)
            return;

        Voice* v = allocate (note);
        if (v->active)
            spawnGhost (*v);

        v->zone = z;
        v->note = note;
        v->vel = (float) effectiveVelocity / 127.0f;
        v->pos = 0.0;
        v->keyDown = true;
        v->sustained = false;
        v->active = true;
        v->age = ++ageCounter;
        v->lastGain = 0.f;
        v->adsr.setParameters (adsrParams);
        v->adsr.reset();
        v->adsr.noteOn();
    }

    void noteOff (int note)
    {
        for (auto& v : voices)
            if (v.active && v.keyDown && v.note == note)
            {
                v.keyDown = false;
                if (sustainDown)
                    v.sustained = true;
                else
                    v.adsr.noteOff();
            }
    }

    void allNotesOff()
    {
        for (auto& v : voices)
            if (v.active)
            {
                v.keyDown = false;
                v.sustained = false;
                v.adsr.noteOff();
            }
    }

    void soundOff()
    {
        for (auto& v : voices)
        {
            v.active = false;
            v.adsr.reset();
        }
        for (auto& g : ghosts)
            g.active = false;
        sustainDown = false;
    }

    // Adds the sound of all voices into out[start, start+num) (stereo).
    void render (juce::AudioBuffer<float>& out, int start, int num)
    {
        jassert (out.getNumChannels() >= 2);
        float* L = out.getWritePointer (0) + start;
        float* R = out.getWritePointer (1) + start;

        while (num > 0)
        {
            const int n = juce::jmin (num, kChunk);
            renderChunk (L, R, n);
            L += n;
            R += n;
            num -= n;
        }
    }

private:
    struct Voice
    {
        bool active = false, keyDown = false, sustained = false;
        int note = -1;
        float vel = 0.f, lastGain = 0.f;
        const Zone* zone = nullptr;
        double pos = 0.0;
        juce::uint64 age = 0;
        juce::ADSR adsr;
    };

    struct Ghost
    {
        bool active = false;
        const Zone* zone = nullptr;
        double pos = 0.0, ratio = 1.0;
        float gain = 0.f, step = 0.f;
        int remaining = 0;
    };

    double baseRatio (const Zone& z, int note) const noexcept
    {
        const int root = z.useRootParam ? params.rootNote : z.root;
        return (z.sampleRate / hostSR) * std::exp2 ((double) (note - root) / 12.0);
    }

    const Zone* findZone (int note, int vel) const
    {
        const Zone* best = nullptr;
        int bestDist = 1 << 30;
        for (const auto& z : inst->zones)
        {
            if (note < z.lo || note > z.hi || vel < z.loVel || vel > z.hiVel)
                continue;
            const int root = z.useRootParam ? params.rootNote : z.root;
            const int dist = std::abs (note - root);
            if (dist < bestDist)
            {
                bestDist = dist;
                best = &z;
            }
        }
        return best;
    }

    Voice* allocate (int note)
    {
        for (auto& v : voices)
            if (! v.active)
                return &v;

        for (auto& v : voices)
            if (v.note == note)
                return &v;

        Voice* best = nullptr;
        for (auto& v : voices)
            if (! v.keyDown && ! v.sustained && (best == nullptr || v.age < best->age))
                best = &v;
        if (best != nullptr)
            return best;

        best = &voices[0];
        for (auto& v : voices)
            if (v.age < best->age)
                best = &v;
        return best;
    }

    void spawnGhost (const Voice& v)
    {
        Ghost* g = nullptr;
        for (auto& gh : ghosts)
            if (! gh.active)
            {
                g = &gh;
                break;
            }
        if (g == nullptr)
        {
            g = &ghosts[0];
            for (auto& gh : ghosts)
                if (gh.remaining < g->remaining)
                    g = &gh;
        }
        g->active = true;
        g->zone = v.zone;
        g->pos = v.pos;
        g->ratio = baseRatio (*v.zone, v.note);
        g->gain = v.lastGain;
        g->step = v.lastGain / (float) kFade;
        g->remaining = kFade;
    }

    void renderChunk (float* L, float* R, int n)
    {
        for (int i = 0; i < n; ++i)
            pitchRatio[(size_t) i] = std::exp2 ((pitchSmooth.getNextValue() + bendSemis) * (1.0f / 12.0f));

        for (auto& g : ghosts)
        {
            if (! g.active)
                continue;
            const int len = g.zone->audio->getNumSamples();
            for (int i = 0; i < n && g.remaining > 0; ++i, --g.remaining)
            {
                if (g.pos < len)
                {
                    float l, r;
                    sampler::fetchStereo (*g.zone, g.pos, l, r);
                    L[i] += l * g.gain;
                    R[i] += r * g.gain;
                    g.pos += g.ratio;
                }
                g.gain = juce::jmax (0.f, g.gain - g.step);
            }
            if (g.remaining <= 0)
                g.active = false;
        }

        for (auto& v : voices)
        {
            if (! v.active)
                continue;

            const Zone& z = *v.zone;
            const int len = z.audio->getNumSamples();
            const double base = baseRatio (z, v.note);

            for (int i = 0; i < n; ++i)
            {
                const float env = v.adsr.getNextSample();
                if (! v.adsr.isActive() || v.pos >= len)
                {
                    v.active = false;
                    break;
                }

                float l, r;
                sampler::fetchStereo (z, v.pos, l, r);
                const float g = env * v.vel;
                v.lastGain = g;
                L[i] += l * g;
                R[i] += r * g;
                v.pos += base * (double) pitchRatio[(size_t) i];
            }
        }
    }

    double hostSR = 44100.0;
    Params params;
    juce::ADSR::Parameters adsrParams { 0.005f, 0.2f, 1.f, 0.3f };
    std::shared_ptr<const Instrument> inst;
    std::array<Voice, kVoices> voices;
    std::array<Ghost, kVoices> ghosts;
    std::array<float, kChunk> pitchRatio {};
    juce::SmoothedValue<float> pitchSmooth;
    float bendSemis = 0.f;
    bool sustainDown = false;
    juce::uint64 ageCounter = 0;
};
