#pragma once
#include "PluginProcessor.h"

// User palette (16 pastels).  Everything in the GUI is drawn from these, plus one dark ink
// colour for text/ticks/outlines, since pastel-on-pastel text would be unreadable.
namespace palette
{
inline juce::Colour at (int i)
{
    static const juce::uint32 c[16] = {
        0xffE6B8A5, 0xffF6D8C8, 0xffE8C9A1, 0xffD9C88E,
        0xffC5D6A8, 0xffAFC9B5, 0xffB8D4CE, 0xffAFCBD9,
        0xffB8C2E0, 0xffC5B4D8, 0xffD8B5CE, 0xffD9AFAF,
        0xffE7BDAA, 0xffC8B9A5, 0xffB8C5B0, 0xffE5D9C5 };
    return juce::Colour (c[((i % 16) + 16) % 16]);
}
} // namespace palette

// Pastel rotary dial: each knob body takes its own palette colour (set via the "knobColour" property).
class BakeliteLookAndFeel : public juce::LookAndFeel_V4
{
public:
    static juce::Colour chassis() { return palette::at (13); }               // #C8B9A5 taupe
    static juce::Colour panel()   { return palette::at (15); }               // #E5D9C5 cream
    static juce::Colour ink()     { return juce::Colour (0xff3b322c); }      // dark warm brown (text, ticks)
    static juce::Colour alert()   { return juce::Colour (0xff9c3d2e); }      // error text

    BakeliteLookAndFeel()
    {
        setColour (juce::Slider::textBoxTextColourId, ink());
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Label::textColourId, ink());
        setColour (juce::TextButton::buttonColourId, palette::at (4));        // #C5D6A8
        setColour (juce::TextButton::buttonOnColourId, palette::at (5));
        setColour (juce::TextButton::textColourOffId, ink());
        setColour (juce::TextButton::textColourOnId, ink());
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        auto area = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
        const float d = juce::jmin (area.getWidth(), area.getHeight());
        const auto c = area.getCentre();
        const float outer = d * 0.5f, body = outer - 7.0f;

        // tick marks (one per step for stepped selectors, 11 for continuous knobs)
        const int steps = (int) s.getProperties().getWithDefault ("steps", 10);
        g.setColour (ink().withAlpha (0.7f));
        for (int i = 0; i <= steps; ++i)
        {
            const float a = startAngle + (endAngle - startAngle) * (float) i / (float) steps;
            const float s0 = std::sin (a), c0 = -std::cos (a);
            g.drawLine (c.x + s0 * (body + 2.5f), c.y + c0 * (body + 2.5f),
                        c.x + s0 * (outer - 0.5f), c.y + c0 * (outer - 0.5f), 1.4f);
        }

        // knob body in this knob's palette colour
        const auto base = juce::Colour ((juce::uint32) (int) s.getProperties().getWithDefault ("knobColour", (int) palette::at (1).getARGB()));
        g.setGradientFill (juce::ColourGradient (base.brighter (0.35f), c.x - body * 0.4f, c.y - body * 0.5f,
                                                 base.darker (0.22f), c.x + body * 0.6f, c.y + body * 0.8f, true));
        g.fillEllipse (c.x - body, c.y - body, body * 2.f, body * 2.f);
        g.setColour (ink().withAlpha (0.8f));
        g.drawEllipse (c.x - body, c.y - body, body * 2.f, body * 2.f, 1.5f);
        g.setColour (ink().withAlpha (0.16f));
        g.drawEllipse (c.x - body * 0.62f, c.y - body * 0.62f, body * 1.24f, body * 1.24f, 1.0f);

        // pointer
        const float a = startAngle + pos * (endAngle - startAngle);
        juce::Path p;
        p.startNewSubPath (c.x + std::sin (a) * body * 0.3f, c.y - std::cos (a) * body * 0.3f);
        p.lineTo (c.x + std::sin (a) * body * 0.88f, c.y - std::cos (a) * body * 0.88f);
        g.setColour (ink());
        g.strokePath (p, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
};

class WaveformView : public juce::Component
{
public:
    void setInstrument (std::shared_ptr<const Instrument> i) { inst = std::move (i); message.clear(); repaint(); }
    void setMessage (const juce::String& m) { message = m; repaint(); }
    void setDragOver (bool b) { dragOver = b; repaint(); }
    void paint (juce::Graphics& g) override;

private:
    std::shared_ptr<const Instrument> inst;
    juce::String message;
    bool dragOver = false;
};

class CbvbSamplerEditor : public juce::AudioProcessorEditor,
                          public juce::FileDragAndDropTarget,
                          private juce::ChangeListener
{
public:
    explicit CbvbSamplerEditor (CbvbSamplerAudioProcessor&);
    ~CbvbSamplerEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void fileDragEnter (const juce::StringArray&, int, int) override { wave.setDragOver (true); }
    void fileDragExit (const juce::StringArray&) override { wave.setDragOver (false); }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    struct Section
    {
        juce::String title;
        std::vector<Knob*> knobs;
        juce::Rectangle<int> bounds;
        int paletteIndex = 0; // header strip colour
    };

    Knob* addKnob (const juce::String& paramId, const juce::String& caption,
                   const juce::String& unit, int decimals);
    Knob* addSelector (const juce::String& paramId, const juce::String& caption, const juce::StringArray& labels);
    void layoutSection (Section&);
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void loadAndReport (const juce::File&);

    CbvbSamplerAudioProcessor& proc;
    BakeliteLookAndFeel laf;
    WaveformView wave;
    juce::TextButton loadButton { "LOAD..." };
    juce::Label velocityModeLabel;
    juce::TextButton velocityModeButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> velocityModeAttachment;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::MidiKeyboardComponent keyboard;

    std::vector<std::unique_ptr<Knob>> knobs;
    Section voice { "VOICE & TUNING", {}, {}, 7 }, eqp { "PULTEC EQP-1A", {}, {}, 0 },
            meq { "PULTEC MEQ-5", {}, {}, 9 }, tape { "TAPE DELAY", {}, {}, 5 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CbvbSamplerEditor)
};
