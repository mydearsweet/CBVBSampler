#include "PluginEditor.h"

void WaveformView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (BakeliteLookAndFeel::panel());
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (dragOver ? palette::at (0) : BakeliteLookAndFeel::ink().withAlpha (0.35f));
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, dragOver ? 3.0f : 1.0f);

    auto wf = r.reduced (12.0f, 10.0f);
    auto textArea = wf.removeFromTop (18.0f);
    g.setFont (juce::FontOptions (13.0f));

    if (inst == nullptr || inst->zones.empty())
    {
        g.setColour (BakeliteLookAndFeel::ink().withAlpha (0.75f));
        g.drawText (message.isNotEmpty() ? message : "Drop a .wav / .flac / .ogg / .ncw / .sfz / .dspreset here",
                    r.toNearestInt(), juce::Justification::centred);
        return;
    }

    const auto& z = inst->zones.front();
    const double dur = (double) z.audio->getNumSamples() / z.sampleRate;
    juce::String info = inst->name + "   |   " + juce::String (z.audio->getNumChannels()) + " ch   |   "
                        + juce::String ((int) z.sampleRate) + " Hz   |   " + juce::String (dur, 2) + " s";
    if (inst->zones.size() > 1)
        info << "   |   " << (int) inst->zones.size() << " zones (first shown)";

    g.setColour (BakeliteLookAndFeel::ink());
    g.drawText (info, textArea.toNearestInt(), juce::Justification::centredLeft);
    if (message.isNotEmpty())
    {
        g.setColour (BakeliteLookAndFeel::alert());
        g.drawText (message, textArea.toNearestInt(), juce::Justification::centredRight);
    }

    const float midY = wf.getCentreY(), half = wf.getHeight() * 0.5f;
    juce::Path path;
    const int cols = (int) inst->peaks.size();
    const int px = juce::jmax (1, (int) wf.getWidth());
    for (int x = 0; x < px; ++x)
    {
        const float peak = inst->peaks[(size_t) juce::jmin (cols - 1, x * cols / px)];
        const float fx = wf.getX() + (float) x;
        path.addLineSegment ({ fx, midY - peak * half, fx, midY + peak * half }, 0.5f);
    }
    g.setColour (BakeliteLookAndFeel::ink().withAlpha (0.85f));
    g.fillPath (path);
    g.setColour (BakeliteLookAndFeel::ink().withAlpha (0.3f));
    g.drawHorizontalLine ((int) midY, wf.getX(), wf.getRight());
}

CbvbSamplerEditor::CbvbSamplerEditor (CbvbSamplerAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    setLookAndFeel (&laf);

    addAndMakeVisible (wave);
    addAndMakeVisible (loadButton);
    addAndMakeVisible (keyboard);
    keyboard.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, palette::at (15));
    keyboard.setColour (juce::MidiKeyboardComponent::blackNoteColourId, BakeliteLookAndFeel::ink());
    keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, BakeliteLookAndFeel::ink().withAlpha (0.35f));
    keyboard.setColour (juce::MidiKeyboardComponent::textLabelColourId, BakeliteLookAndFeel::ink().withAlpha (0.7f));
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, palette::at (0).withAlpha (0.8f));
    keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, palette::at (2).withAlpha (0.6f));
    keyboard.setLowestVisibleKey (36);

    loadButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Load a sample, .sfz or .dspreset", juce::File(),
                                                       "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.ncw;*.sfz;*.dspreset");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  if (fc.getResult().existsAsFile())
                                      loadAndReport (fc.getResult());
                              });
    };

    // Voice & tuning
    voice.knobs = { addKnob (ids::volume, "VOLUME", " dB", 1), addKnob (ids::pitch, "PITCH", " st", 2),
                    addKnob (ids::root, "ROOT NOTE", "", 0),   addKnob (ids::attack, "ATTACK", " ms", 0),
                    addKnob (ids::decay, "DECAY", " ms", 0),   addKnob (ids::sustain, "SUSTAIN", " %", 0),
                    addKnob (ids::release, "RELEASE", " ms", 0) };
    voice.knobs[2]->slider.textFromValueFunction = [] (double v)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const int n = (int) std::lround (v);
        return juce::String (names[n % 12]) + juce::String (n / 12 - 2) + "  (" + juce::String (n) + ")";
    };
    voice.knobs[2]->slider.updateText();

    // A two-position switch, saved in the preset and available for DAW automation.
    velocityModeLabel.setText ("VELOCITY MODE", juce::dontSendNotification);
    velocityModeLabel.setJustificationType (juce::Justification::centredLeft);
    velocityModeLabel.setColour (juce::Label::textColourId, BakeliteLookAndFeel::ink());
    addAndMakeVisible (velocityModeLabel);
    velocityModeButton.setClickingTogglesState (true);
    velocityModeButton.onStateChange = [this]
    {
        velocityModeButton.setButtonText (velocityModeButton.getToggleState()
                                           ? "FIXED VELOCITY" : "TOUCH SENSITIVE");
    };
    addAndMakeVisible (velocityModeButton);
    velocityModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
                                 (proc.apvts, ids::fixedVelocity, velocityModeButton);
    velocityModeButton.onStateChange();

    // EQP-1A
    eqp.knobs = { addSelector (ids::lowSel, "LOW CPS", freqs::labels (freqs::lowCps)),
                  addKnob (ids::lowBoost, "LOW BOOST", " dB", 1),
                  addKnob (ids::lowAtten, "LOW ATTEN", " dB", 1),
                  addSelector (ids::hiSel, "HI BOOST KCS", freqs::labels (freqs::hiBoost)),
                  addKnob (ids::hiBoost, "HI BOOST", " dB", 1),
                  addKnob (ids::hiBw, "BANDWIDTH", " Q", 2),
                  addSelector (ids::hiAttSel, "HI ATTEN KCS", freqs::labels (freqs::hiAtten)),
                  addKnob (ids::hiAtten, "HI ATTEN", " dB", 1) };

    // MEQ-5
    meq.knobs = { addSelector (ids::lpkSel, "LOW PEAK", freqs::labels (freqs::lowPeak)),
                  addKnob (ids::lpkBoost, "PEAK BOOST", " dB", 1),
                  addSelector (ids::dipSel, "DIP FREQ", freqs::labels (freqs::dip)),
                  addKnob (ids::dipCut, "DIP CUT", " dB", 1),
                  addSelector (ids::hpkSel, "HIGH PEAK", freqs::labels (freqs::hiPeak)),
                  addKnob (ids::hpkBoost, "PEAK BOOST", " dB", 1) };

    // Tape
    tape.knobs = { addKnob (ids::dlyTime, "TIME", " ms", 0), addKnob (ids::dlyFb, "FEEDBACK", " %", 0),
                   addKnob (ids::dlyDrive, "DRIVE", " %", 0), addKnob (ids::dlyWow, "WOW/FLUTTER", " %", 2),
                   addKnob (ids::dlyMix, "MIX", " %", 0) };

    proc.addChangeListener (this);
    wave.setInstrument (proc.getInstrument());

    // Slider text boxes bake in their colours when created, i.e. before they were parented to this
    // editor, so re-apply our LookAndFeel to every child now that they all exist.
    sendLookAndFeelChange();

    // Must come last: setSize() triggers resized(), which lays out the knobs created above.
    setSize (1100, 650);
}

CbvbSamplerEditor::~CbvbSamplerEditor()
{
    proc.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

CbvbSamplerEditor::Knob* CbvbSamplerEditor::addKnob (const juce::String& id, const juce::String& caption,
                                                     const juce::String& unit, int decimals)
{
    auto k = std::make_unique<Knob>();
    k->slider.getProperties().set ("steps", 10);
    k->slider.getProperties().set ("knobColour", (int) palette::at ((int) knobs.size()).getARGB());
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 78, 16);
    k->slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    k->slider.textFromValueFunction = [unit, decimals] (double v) { return juce::String (v, decimals) + unit; };
    k->label.setText (caption, juce::dontSendNotification);
    k->label.setJustificationType (juce::Justification::centred);
    k->label.setFont (juce::FontOptions (11.5f));
    addAndMakeVisible (k->slider);
    addAndMakeVisible (k->label);
    k->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k->slider);
    if (auto* prm = proc.apvts.getParameter (id))
        k->slider.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
    k->slider.updateText();
    knobs.push_back (std::move (k));
    return knobs.back().get();
}

CbvbSamplerEditor::Knob* CbvbSamplerEditor::addSelector (const juce::String& id, const juce::String& caption,
                                                         const juce::StringArray& labels)
{
    auto k = std::make_unique<Knob>();
    k->slider.getProperties().set ("steps", labels.size() - 1);
    k->slider.getProperties().set ("knobColour", (int) palette::at ((int) knobs.size()).getARGB());
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, true, 78, 16);
    k->slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    k->slider.textFromValueFunction = [labels] (double v)
    {
        return labels[juce::jlimit (0, labels.size() - 1, (int) std::lround (v))];
    };
    k->label.setText (caption, juce::dontSendNotification);
    k->label.setJustificationType (juce::Justification::centred);
    k->label.setFont (juce::FontOptions (11.5f));
    addAndMakeVisible (k->slider);
    addAndMakeVisible (k->label);
    k->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k->slider);
    k->slider.updateText();
    knobs.push_back (std::move (k));
    return knobs.back().get();
}

void CbvbSamplerEditor::layoutSection (Section& s)
{
    auto inner = s.bounds.reduced (10).withTrimmedTop (22);
    const int slot = inner.getWidth() / juce::jmax (1, (int) s.knobs.size());
    if (&s == &voice)
    {
        auto switchRow = inner.removeFromBottom (38);
        velocityModeLabel.setBounds (switchRow.removeFromLeft (117));
        velocityModeButton.setBounds (switchRow.removeFromLeft (175).reduced (0, 4));
    }
    for (auto* k : s.knobs)
    {
        auto cell = inner.removeFromLeft (slot);
        k->label.setBounds (cell.removeFromTop (16));
        k->slider.setBounds (cell);
    }
}

void CbvbSamplerEditor::resized()
{
    auto area = getLocalBounds().reduced (16);

    auto kb = area.removeFromBottom (64);
    keyboard.setBounds (kb);
    area.removeFromBottom (10);

    auto top = area.removeFromTop (128);
    wave.setBounds (top);
    loadButton.setBounds (top.getRight() - 86, top.getY() + 8, 76, 22);
    area.removeFromTop (10);

    const int rowH = (area.getHeight() - 10) / 2;
    auto row1 = area.removeFromTop (rowH);
    area.removeFromTop (10);
    auto row2 = area;

    // row 1: voice (7 knobs) | tape (5 knobs); row 2: EQP (8) | MEQ (6)
    const int w1 = row1.getWidth() * 7 / 12;
    voice.bounds = row1.removeFromLeft (w1);
    row1.removeFromLeft (10);
    tape.bounds = row1;

    const int w2 = row2.getWidth() * 8 / 14;
    eqp.bounds = row2.removeFromLeft (w2);
    row2.removeFromLeft (10);
    meq.bounds = row2;

    for (auto* s : { &voice, &eqp, &meq, &tape })
        layoutSection (*s);
}

void CbvbSamplerEditor::paint (juce::Graphics& g)
{
    g.fillAll (BakeliteLookAndFeel::chassis());

    for (auto* s : { &voice, &eqp, &meq, &tape })
    {
        auto b = s->bounds.toFloat();
        constexpr float corner = 6.0f, headerH = 24.0f;

        // coloured header strip, cream body
        g.setColour (palette::at (s->paletteIndex));
        g.fillRoundedRectangle (b, corner);
        juce::Path body;
        body.addRoundedRectangle (b.getX(), b.getY() + headerH, b.getWidth(), b.getHeight() - headerH,
                                  corner, corner, false, false, true, true);
        g.setColour (BakeliteLookAndFeel::panel());
        g.fillPath (body);

        g.setColour (BakeliteLookAndFeel::ink().withAlpha (0.35f));
        g.drawRoundedRectangle (b.reduced (0.5f), corner, 1.0f);
        g.drawHorizontalLine ((int) (b.getY() + headerH), b.getX() + 0.5f, b.getRight() - 0.5f);

        g.setColour (BakeliteLookAndFeel::ink());
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (s->title, b.withHeight (headerH).reduced (12.0f, 0.0f), juce::Justification::centredLeft);
    }
}

void CbvbSamplerEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    wave.setInstrument (proc.getInstrument());
}

void CbvbSamplerEditor::loadAndReport (const juce::File& f)
{
    const auto err = proc.loadFile (f);
    if (err.isNotEmpty())
        wave.setMessage (err);
}

void CbvbSamplerEditor::filesDropped (const juce::StringArray& files, int, int)
{
    wave.setDragOver (false);
    if (files.size() > 0)
        loadAndReport (juce::File (files[0]));
}
