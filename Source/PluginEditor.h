#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

/** Red / black palette (same family as BandChatLIVE). */
namespace theme
{
    const juce::Colour bg        { 0xff0a0a0c };
    const juce::Colour panel     { 0xff131013 };
    const juce::Colour panelAlt  { 0xff1a1518 };
    const juce::Colour border    { 0xff2c1416 };
    const juce::Colour accent    { 0xffc81620 };
    const juce::Colour accentHv  { 0xffff3b3b };
    const juce::Colour accentDk  { 0xff6e0f13 };
    const juce::Colour text      { 0xfff2f2f4 };
    const juce::Colour textDim   { 0xff9a959a };
    const juce::Colour textMuted { 0xff615c60 };

    // component colours (all in the red family, told apart by hue/brightness)
    const juce::Colour ambience  { 0xffff7a6e };
    const juce::Colour voice     { 0xffff2a35 };
    const juce::Colour reverb    { 0xffd6336c };

    inline juce::Font font (float size, bool bold = false)
    {
        return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
    }
}

//==============================================================================
class VoiceGOCleanLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    VoiceGOCleanLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h,
                           float sliderPos, float startAngle, float endAngle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool hover, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool hover, bool down) override;
};

//==============================================================================
class PowerButton final : public juce::Button
{
public:
    PowerButton() : juce::Button ("Power") { setClickingTogglesState (true); }
    void paintButton (juce::Graphics&, bool hover, bool down) override;
};

//==============================================================================
/** Big black display: scrolling waveform (input vs output) or split spectra. */
class Display final : public juce::Component
{
public:
    enum class Mode { wave, split };

    explicit Display (VoiceGOCleanProcessor&);
    void setMode (Mode m) { mode = m; repaint(); }
    Mode getMode() const noexcept { return mode; }

    void update();   // called from the editor timer
    void paint (juce::Graphics&) override;

private:
    void paintWave (juce::Graphics&, juce::Rectangle<float>);
    void paintSplit (juce::Graphics&, juce::Rectangle<float>);
    void paintSpectrum (juce::Graphics&, juce::Rectangle<float>, const std::vector<float>& bands,
                        juce::Colour, const juce::String& name, const juce::String& paramID);

    VoiceGOCleanProcessor& proc;
    Mode mode = Mode::wave;

    static constexpr int historySize = 760;
    std::vector<float> histIn, histOut;
    int writePos = 0;

    std::vector<float> specV, specA, specR;   // smoothed dB per band
};

//==============================================================================
class KnobStrip final : public juce::Component
{
public:
    KnobStrip (const juce::String& title, const juce::String& tooltip, juce::Colour accent,
               juce::AudioProcessorValueTreeState&, const juce::String& paramID,
               const juce::String& soloID, const juce::String& muteID);

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    juce::String title;
    juce::Colour accent;
    juce::Slider knob;
    juce::TextButton solo { "S" }, mute { "M" };
    juce::AudioProcessorValueTreeState::SliderAttachment knobAtt;
    juce::AudioProcessorValueTreeState::ButtonAttachment soloAtt, muteAtt;
    juce::Rectangle<int> titleArea, valueArea;
};

//==============================================================================
class TrimStrip final : public juce::Component
{
public:
    TrimStrip (const juce::String& title, juce::AudioProcessorValueTreeState&, const juce::String& paramID);

    void setPeak (float linearPeak);
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    juce::String title;
    juce::Slider knob;
    juce::AudioProcessorValueTreeState::SliderAttachment att;
    juce::Rectangle<int> titleArea, valueArea;
    juce::Rectangle<float> meterArea;
    float meterDb = -100.0f, holdDb = -100.0f;
    int holdFrames = 0;
};

//==============================================================================
/** The curved control panel, drawn over the bottom of the display. */
class PanelShape final : public juce::Component
{
public:
    PanelShape() { setInterceptsMouseClicks (false, false); }
    void paint (juce::Graphics&) override;
    static constexpr float top = 304.0f, bump = 272.0f;
};

//==============================================================================
/** Fixed-size (780 x 500) content, scaled by the editor when resized. */
class MainContent final : public juce::Component
{
public:
    static constexpr int baseW = 780, baseH = 500;

    explicit MainContent (VoiceGOCleanProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();

private:
    VoiceGOCleanProcessor& proc;
    Display display;
    PanelShape panelShape;
    juce::TextButton waveBtn { "WAVE" }, splitBtn { "SPLIT" };
    PowerButton power;
    juce::AudioProcessorValueTreeState::ButtonAttachment powerAtt;
    KnobStrip ambience, voice, reverb;
    TrimStrip input, output;
};

//==============================================================================
class VoiceGOCleanEditor final : public juce::AudioProcessorEditor,
                               private juce::Timer
{
public:
    explicit VoiceGOCleanEditor (VoiceGOCleanProcessor&);
    ~VoiceGOCleanEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Runs one UI refresh immediately (used by the screenshot tool). */
    void refreshNow() { timerCallback(); }

private:
    void timerCallback() override;

    VoiceGOCleanLookAndFeel lnf;
    MainContent content;
    juce::TooltipWindow tooltips { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoiceGOCleanEditor)
};
