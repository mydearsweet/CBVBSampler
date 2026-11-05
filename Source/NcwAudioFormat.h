#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include "NcwDecoder.h"

// Read-only JUCE AudioFormat for Native Instruments .ncw files.  The whole file is decoded up
// front (samples are held as 32-bit words, left-aligned PCM or raw float bits), after which
// readSamples() is a plain copy.  Registering it with an AudioFormatManager makes .ncw work
// everywhere a .wav does, including inside .sfz / .dspreset sample references.
class NcwAudioFormatReader final : public juce::AudioFormatReader
{
public:
    NcwAudioFormatReader (juce::InputStream* in, ncw::Decoded&& d)
        : juce::AudioFormatReader (in, "Native Instruments NCW"), decoded (std::move (d))
    {
        sampleRate = decoded.sampleRate;
        bitsPerSample = (unsigned int) decoded.bitsPerSample;
        lengthInSamples = decoded.numFrames;
        numChannels = (unsigned int) decoded.channels;
        usesFloatingPointData = decoded.isFloat;
    }

    bool readSamples (int* const* dest, int numDest, int startOffsetInDest,
                      juce::int64 startSampleInSource, int numSamples) override
    {
        if (dest == nullptr || startSampleInSource < 0)
            return false;

        const int available = (int) juce::jlimit<juce::int64> (0, (juce::int64) numSamples, lengthInSamples - startSampleInSource);

        for (int i = 0; i < numDest; ++i)
        {
            if (dest[i] == nullptr)
                continue;

            int* out = dest[i] + startOffsetInDest;
            const auto& src = decoded.data[(size_t) juce::jmin (i, decoded.channels - 1)];
            for (int s = 0; s < available; ++s)
                out[s] = src[(size_t) (startSampleInSource + s)];
            for (int s = available; s < numSamples; ++s)
                out[s] = 0; // zero bits are silence for both PCM and float
        }
        return true;
    }

private:
    ncw::Decoded decoded;
};

class NcwAudioFormat final : public juce::AudioFormat
{
public:
    NcwAudioFormat() : juce::AudioFormat ("Native Instruments NCW", ".ncw") {}

    juce::Array<int> getPossibleSampleRates() override { return {}; }
    juce::Array<int> getPossibleBitDepths() override   { return {}; }
    bool canDoStereo() override { return true; }
    bool canDoMono() override   { return true; }
    bool isCompressed() override { return true; }

    juce::AudioFormatReader* createReaderFor (juce::InputStream* in, bool deleteStreamIfOpeningFails) override
    {
        if (in == nullptr)
            return nullptr;

        juce::MemoryBlock bytes;
        in->setPosition (0);
        in->readIntoMemoryBlock (bytes);

        ncw::Decoded d;
        std::string error;
        if (! ncw::decode (static_cast<const uint8_t*> (bytes.getData()), bytes.getSize(), d, error))
        {
            DBG ("NCW: " << error);
            if (deleteStreamIfOpeningFails)
                delete in;
            return nullptr;
        }
        return new NcwAudioFormatReader (in, std::move (d));
    }

    juce::AudioFormatWriter* createWriterFor (juce::OutputStream*, double, unsigned int, int,
                                              const juce::StringPairArray&, int) override
    {
        return nullptr; // read-only
    }

    // The base-class overload taking an AudioChannelSet would otherwise be hidden by the override above.
    using juce::AudioFormat::createWriterFor;
};
