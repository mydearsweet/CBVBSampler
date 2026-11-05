#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include "Parameters.h"
#include "SampleEngine.h"
#include "PultecMeqEq.h"
#include "TapeDelay.h"

class CbvbSamplerAudioProcessor : public juce::AudioProcessor,
                                  public juce::ChangeBroadcaster
{
public:
    CbvbSamplerAudioProcessor();
    ~CbvbSamplerAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "CBVB Sampler"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // Message-thread API. Returns an empty string on success, otherwise a user-facing error.
    juce::String loadFile (const juce::File& file);
    std::shared_ptr<const Instrument> getInstrument() const;

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;

private:
    std::shared_ptr<Instrument> buildFromAudioFile (const juce::File&, juce::String& error);
    std::shared_ptr<Instrument> buildFromDecentSampler (const juce::File&, juce::String& error);
    std::shared_ptr<Instrument> buildFromSfz (const juce::File&, juce::String& error);
    std::shared_ptr<const juce::AudioBuffer<float>> readAudio (const juce::File&, double& sr, juce::String& error);
    void finishInstrument (Instrument&);
    void publish (std::shared_ptr<const Instrument>);
    void handleMidi (const juce::MidiMessage&);
    void renderVoices (juce::AudioBuffer<float>&, int start, int num);

    juce::AudioFormatManager formats;
    mutable juce::SpinLock instLock;
    std::shared_ptr<const Instrument> current;
    std::vector<std::shared_ptr<const Instrument>> retired; // keeps old instruments alive until the audio thread lets go
    juce::String lastPath;

    SamplerEngine engine;
    PultecMeqEq eq;
    TapeDelay delay;
    juce::dsp::Limiter<float> limiter;
    float lastGain = 1.0f;

    // cached raw parameter pointers
    std::atomic<float>* p[32] {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CbvbSamplerAudioProcessor)
};
