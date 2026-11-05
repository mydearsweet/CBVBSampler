#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "NcwAudioFormat.h"
#include "SfzParser.h"
#include <algorithm>
#include <map>

namespace
{
enum PIdx { Vol, Pitch, Root, Atk, Dec, Sus, Rel,
            LowSel, LowBoost, LowAtten, HiSel, HiBoost, HiBw, HiAttSel, HiAtten,
            LpkSel, LpkBoost, DipSel, DipCut, HpkSel, HpkBoost,
            DlyTime, DlyFb, DlyDrive, DlyWow, DlyMix, FixedVelocity, NumP };

const char* const paramIds[NumP] = {
    ids::volume, ids::pitch, ids::root, ids::attack, ids::decay, ids::sustain, ids::release,
    ids::lowSel, ids::lowBoost, ids::lowAtten, ids::hiSel, ids::hiBoost, ids::hiBw, ids::hiAttSel, ids::hiAtten,
    ids::lpkSel, ids::lpkBoost, ids::dipSel, ids::dipCut, ids::hpkSel, ids::hpkBoost,
    ids::dlyTime, ids::dlyFb, ids::dlyDrive, ids::dlyWow, ids::dlyMix, ids::fixedVelocity };

template <typename V>
float pick (const V& v, float index)
{
    return v[(size_t) juce::jlimit (0, (int) v.size() - 1, (int) std::lround (index))];
}
} // namespace

CbvbSamplerAudioProcessor::CbvbSamplerAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "CBVB_SAMPLER", createParameterLayout())
{
    formats.registerBasicFormats();                      // wav, aiff, flac, ogg (+ platform codecs)
    formats.registerFormat (new NcwAudioFormat(), false); // Native Instruments .ncw (read-only)
    for (int i = 0; i < NumP; ++i)
        p[i] = apvts.getRawParameterValue (paramIds[i]);
}

bool CbvbSamplerAudioProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void CbvbSamplerAudioProcessor::prepareToPlay (double sr, int blockSize)
{
    engine.prepare (sr);
    eq.prepare (sr);
    delay.prepare (sr);

    juce::dsp::ProcessSpec spec { sr, (juce::uint32) juce::jmax (1, blockSize), 2 };
    limiter.prepare (spec);
    limiter.setThreshold (-0.3f);
    limiter.setRelease (60.0f);
    limiter.reset();
    lastGain = juce::Decibels::decibelsToGain (p[Vol]->load());
}

// ---------------------------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------------------------
void CbvbSamplerAudioProcessor::renderVoices (juce::AudioBuffer<float>& buf, int start, int num)
{
    if (num > 0)
        engine.render (buf, start, num);
}

void CbvbSamplerAudioProcessor::handleMidi (const juce::MidiMessage& m)
{
    if (m.isNoteOn())                       engine.noteOn (m.getNoteNumber(), m.getVelocity());
    else if (m.isNoteOff())                 engine.noteOff (m.getNoteNumber());
    else if (m.isAllNotesOff())             engine.allNotesOff();
    else if (m.isAllSoundOff())             engine.soundOff();
    else if (m.isPitchWheel())              engine.setPitchBend ((float) (m.getPitchWheelValue() - 8192) / 8192.0f * 2.0f);
    else if (m.isControllerOfType (64))     engine.setSustain (m.getControllerValue() >= 64);
}

void CbvbSamplerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int total = buffer.getNumSamples();
    buffer.clear();

    keyboardState.processNextMidiBuffer (midi, 0, total, true);

    // --- parameters -----------------------------------------------------------------------
    SamplerEngine::Params sp;
    sp.attackMs = p[Atk]->load();
    sp.decayMs = p[Dec]->load();
    sp.sustain01 = p[Sus]->load() * 0.01f;
    sp.releaseMs = p[Rel]->load();
    sp.rootNote = (int) std::lround (p[Root]->load());
    sp.pitchSemis = p[Pitch]->load();
    sp.fixedVelocity = p[FixedVelocity]->load() >= 0.5f;
    engine.setParams (sp);

    PultecMeqEq::Params ep;
    ep.lowHz = pick (freqs::lowCps, p[LowSel]->load());
    ep.lowBoost = p[LowBoost]->load();
    ep.lowAtten = p[LowAtten]->load();
    ep.hiBoostHz = pick (freqs::hiBoost, p[HiSel]->load());
    ep.hiBoost = p[HiBoost]->load();
    ep.hiQ = p[HiBw]->load();
    ep.hiAttenHz = pick (freqs::hiAtten, p[HiAttSel]->load());
    ep.hiAtten = p[HiAtten]->load();
    ep.lpkHz = pick (freqs::lowPeak, p[LpkSel]->load());
    ep.lpkBoost = p[LpkBoost]->load();
    ep.dipHz = pick (freqs::dip, p[DipSel]->load());
    ep.dipCut = p[DipCut]->load();
    ep.hpkHz = pick (freqs::hiPeak, p[HpkSel]->load());
    ep.hpkBoost = p[HpkBoost]->load();
    eq.setParams (ep);

    delay.setParams (p[DlyTime]->load(), p[DlyFb]->load() * 0.01f, p[DlyDrive]->load() * 0.01f,
                     p[DlyWow]->load() * 0.01f, p[DlyMix]->load() * 0.01f);

    // --- pick up a newly loaded instrument (never blocks the audio thread) --------------------
    {
        juce::SpinLock::ScopedTryLockType lock (instLock);
        if (lock.isLocked() && ! engine.isCurrent (current))
            engine.setInstrument (current);
    }

    // --- voices, sample-accurate MIDI -------------------------------------------------------------
    int cursor = 0;
    for (const auto meta : midi)
    {
        const int pos = juce::jlimit (cursor, total, meta.samplePosition);
        renderVoices (buffer, cursor, pos - cursor);
        cursor = pos;
        handleMidi (meta.getMessage());
    }
    renderVoices (buffer, cursor, total - cursor);

    // --- Pultec + MEQ -> tape delay -> master ------------------------------------------------
    eq.process (buffer);
    delay.process (buffer);

    const float g = juce::Decibels::decibelsToGain (p[Vol]->load());
    buffer.applyGainRamp (0, total, lastGain, g);
    lastGain = g;

    juce::dsp::AudioBlock<float> block (buffer);
    limiter.process (juce::dsp::ProcessContextReplacing<float> (block));
}

// ---------------------------------------------------------------------------------------------
// Loading (message thread)
// ---------------------------------------------------------------------------------------------
std::shared_ptr<const juce::AudioBuffer<float>>
CbvbSamplerAudioProcessor::readAudio (const juce::File& f, double& sr, juce::String& error)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));
    if (reader == nullptr)
    {
        error = "Could not read audio file: " + f.getFileName();
        return nullptr;
    }
    if (reader->lengthInSamples <= 0 || reader->lengthInSamples > (juce::int64) (reader->sampleRate * 600.0))
    {
        error = "Sample is empty or longer than 10 minutes: " + f.getFileName();
        return nullptr;
    }

    auto buf = std::make_shared<juce::AudioBuffer<float>> ((int) juce::jmin (2u, reader->numChannels),
                                                           (int) reader->lengthInSamples);
    if (! reader->read (buf.get(), 0, (int) reader->lengthInSamples, 0, true, buf->getNumChannels() > 1))
    {
        error = "Failed while decoding " + f.getFileName();
        return nullptr;
    }
    sr = reader->sampleRate;
    return buf;
}

void CbvbSamplerAudioProcessor::finishInstrument (Instrument& inst)
{
    constexpr int columns = 1024;
    inst.peaks.assign (columns, 0.f);
    if (inst.zones.empty())
        return;

    const auto& b = *inst.zones.front().audio;
    const int n = b.getNumSamples();
    for (int c = 0; c < columns; ++c)
    {
        const int s0 = (int) ((juce::int64) n * c / columns);
        const int s1 = juce::jmax (s0 + 1, (int) ((juce::int64) n * (c + 1) / columns));
        float m = 0.f;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            const auto r = b.findMinMax (ch, s0, juce::jmin (s1, n) - s0);
            m = juce::jmax (m, std::abs (r.getStart()), std::abs (r.getEnd()));
        }
        inst.peaks[(size_t) c] = m;
    }
}

std::shared_ptr<Instrument> CbvbSamplerAudioProcessor::buildFromAudioFile (const juce::File& f, juce::String& error)
{
    double sr = 44100.0;
    auto audio = readAudio (f, sr, error);
    if (audio == nullptr)
        return nullptr;

    auto inst = std::make_shared<Instrument>();
    inst->name = f.getFileName();
    Zone z;
    z.audio = audio;
    z.sampleRate = sr;
    z.useRootParam = true;
    inst->zones.push_back (std::move (z));
    return inst;
}

std::shared_ptr<Instrument> CbvbSamplerAudioProcessor::buildFromDecentSampler (const juce::File& f, juce::String& error)
{
    auto xml = juce::XmlDocument::parse (f);
    if (xml == nullptr || ! xml->hasTagName ("DecentSampler"))
    {
        error = "Not a valid .dspreset file: " + f.getFileName();
        return nullptr;
    }

    auto* groups = xml->getChildByName ("groups");
    if (groups == nullptr)
    {
        error = ".dspreset contains no <groups>";
        return nullptr;
    }

    auto inst = std::make_shared<Instrument>();
    inst->name = f.getFileName();
    std::map<juce::String, std::pair<std::shared_ptr<const juce::AudioBuffer<float>>, double>> cache;
    const auto base = f.getParentDirectory();
    juce::String firstError;

    for (auto* group : groups->getChildWithTagNameIterator ("group"))
        for (auto* s : group->getChildWithTagNameIterator ("sample"))
        {
            const auto path = s->getStringAttribute ("path");
            if (path.isEmpty())
                continue;

            if (cache.find (path) == cache.end())
            {
                juce::String err;
                double sr = 44100.0;
                auto audio = readAudio (base.getChildFile (path), sr, err);
                if (audio == nullptr && firstError.isEmpty())
                    firstError = err;
                cache[path] = { audio, sr };
            }

            auto& entry = cache[path];
            if (entry.first == nullptr)
                continue;

            Zone z;
            z.audio = entry.first;
            z.sampleRate = entry.second;
            z.root = s->getIntAttribute ("rootNote", group->getIntAttribute ("rootNote", 60));
            z.lo = s->getIntAttribute ("loNote", group->getIntAttribute ("loNote", 0));
            z.hi = s->getIntAttribute ("hiNote", group->getIntAttribute ("hiNote", 127));
            z.loVel = s->getIntAttribute ("loVel", group->getIntAttribute ("loVel", 0));
            z.hiVel = s->getIntAttribute ("hiVel", group->getIntAttribute ("hiVel", 127));
            z.useRootParam = false;
            inst->zones.push_back (std::move (z));
        }

    if (inst->zones.empty())
    {
        error = firstError.isNotEmpty() ? firstError : ".dspreset contains no playable samples";
        return nullptr;
    }
    return inst;
}

std::shared_ptr<Instrument> CbvbSamplerAudioProcessor::buildFromSfz (const juce::File& f, juce::String& error)
{
    const auto base = f.getParentDirectory();
    const auto text = f.loadFileAsString();
    if (text.isEmpty())
    {
        error = "Could not read .sfz file: " + f.getFileName();
        return nullptr;
    }

    const auto parsed = sfz::parse (text.toStdString(), [&base] (const std::string& rel)
    {
        return base.getChildFile (juce::String::fromUTF8 (rel.c_str())).loadFileAsString().toStdString();
    });

    if (parsed.regions.empty())
    {
        error = "No playable regions found in " + f.getFileName();
        return nullptr;
    }

    auto inst = std::make_shared<Instrument>();
    inst->name = f.getFileName();
    std::map<juce::String, std::pair<std::shared_ptr<const juce::AudioBuffer<float>>, double>> cache;
    juce::String firstError;

    for (const auto& r : parsed.regions)
    {
        const auto path = juce::String::fromUTF8 (r.sample.c_str());
        if (cache.find (path) == cache.end())
        {
            juce::String err;
            double sr = 44100.0;
            auto audio = readAudio (base.getChildFile (path), sr, err);
            if (audio == nullptr && firstError.isEmpty())
                firstError = err;
            cache[path] = { audio, sr };
        }

        auto& entry = cache[path];
        if (entry.first == nullptr)
            continue;

        Zone z;
        z.audio = entry.first;
        z.sampleRate = entry.second;
        z.root = r.root;
        z.lo = r.lokey;
        z.hi = r.hikey;
        z.loVel = r.lovel;
        z.hiVel = r.hivel;
        z.useRootParam = false;
        inst->zones.push_back (std::move (z));
    }

    if (inst->zones.empty())
    {
        error = firstError.isNotEmpty() ? firstError : ".sfz contains no loadable samples";
        return nullptr;
    }
    return inst;
}

juce::String CbvbSamplerAudioProcessor::loadFile (const juce::File& file)
{
    const auto ext = file.getFileExtension().toLowerCase();

    if (ext == ".nki" || ext == ".nkm" || ext == ".nkb" || ext == ".nkx" || ext == ".nkc" || ext == ".nkr")
        return "Kontakt instruments (.nki etc.) can't be read directly. Convert to .sfz or .dspreset "
               "(e.g. with ConvertWithMoss), or load the audio (.wav / .ncw).";

    if (! file.existsAsFile())
        return "File not found: " + file.getFullPathName();

    juce::String error;
    std::shared_ptr<Instrument> inst;
    if (ext == ".dspreset")  inst = buildFromDecentSampler (file, error);
    else if (ext == ".sfz")  inst = buildFromSfz (file, error);
    else                     inst = buildFromAudioFile (file, error);
    if (inst == nullptr)
        return error.isNotEmpty() ? error : "Could not load file";

    finishInstrument (*inst);
    lastPath = file.getFullPathName();
    publish (inst);
    return {};
}

void CbvbSamplerAudioProcessor::publish (std::shared_ptr<const Instrument> inst)
{
    {
        const juce::SpinLock::ScopedLockType lock (instLock);
        if (current != nullptr)
            retired.push_back (current);
        current = std::move (inst);
    }

    // Drop retired instruments the audio thread no longer references (only this thread frees them).
    retired.erase (std::remove_if (retired.begin(), retired.end(),
                                   [] (const auto& r) { return r.use_count() == 1; }),
                   retired.end());
    sendChangeMessage();
}

std::shared_ptr<const Instrument> CbvbSamplerAudioProcessor::getInstrument() const
{
    const juce::SpinLock::ScopedLockType lock (instLock);
    return current;
}

// ---------------------------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------------------------
void CbvbSamplerAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("samplePath", lastPath, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void CbvbSamplerAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto vt = juce::ValueTree::fromXml (*xml);
            apvts.replaceState (vt);
            const auto path = vt.getProperty ("samplePath").toString();
            if (path.isNotEmpty() && juce::File (path).existsAsFile())
                loadFile (juce::File (path));
        }
}

juce::AudioProcessorEditor* CbvbSamplerAudioProcessor::createEditor()
{
    return new CbvbSamplerEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CbvbSamplerAudioProcessor();
}
