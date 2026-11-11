#include "PluginEditor.h"

namespace
{
    constexpr float waveFloorDb = -54.0f;

    float peakToNorm (float pk)
    {
        const float db = juce::Decibels::gainToDecibels (pk, -100.0f);
        return juce::jlimit (0.0f, 1.0f, (db - waveFloorDb) / -waveFloorDb);
    }

    juce::String trimText (float db)
    {
        return (db > 0.05f ? "+" : "") + juce::String (db, 1) + " dB";
    }
}

//==============================================================================
VoiceGOCleanLookAndFeel::VoiceGOCleanLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, theme::bg);
    setColour (juce::TooltipWindow::backgroundColourId, theme::panel);
    setColour (juce::TooltipWindow::textColourId, theme::text);
    setColour (juce::TooltipWindow::outlineColourId, theme::border);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
}

void VoiceGOCleanLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                                              float pos, float startAngle, float endAngle, juce::Slider& s)
{
    const auto accent  = s.findColour (juce::Slider::rotarySliderFillColourId);
    const float unity  = (float) (double) s.getProperties().getWithDefault ("unityPos", 0.5);
    const bool small   = (bool) s.getProperties().getWithDefault ("small", false);

    const auto bounds  = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (small ? 3.0f : 4.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre  = bounds.getCentre();
    const float tickOuter = radius;
    const float ringR  = small ? radius - 2.0f : radius - 9.0f;
    const float ringW  = small ? 3.0f : 4.5f;
    const float angle  = startAngle + pos * (endAngle - startAngle);
    const float unityA = startAngle + unity * (endAngle - startAngle);

    // scale ticks
    if (! small)
    {
        const int numTicks = 31;
        for (int i = 0; i < numTicks; ++i)
        {
            const float t = (float) i / (float) (numTicks - 1);
            const float a = startAngle + t * (endAngle - startAngle);
            const bool major = i == 0 || i == numTicks - 1 || std::abs (t - unity) < 0.017f;
            const bool lit = (a >= juce::jmin (unityA, angle) - 0.001f && a <= juce::jmax (unityA, angle) + 0.001f && pos > 0.0005f);
            const float len = major ? 6.0f : 3.5f;
            g.setColour (lit ? accent.withAlpha (0.9f) : (major ? theme::textMuted : juce::Colour (0xff35262a)));
            g.drawLine ({ centre.getPointOnCircumference (tickOuter - len, a),
                          centre.getPointOnCircumference (tickOuter, a) }, major ? 1.6f : 1.1f);
        }
    }

    // track
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, ringR, ringR, 0.0f, startAngle, endAngle, true);
    g.setColour (juce::Colour (0xff1f1517));
    g.strokePath (track, { ringW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });

    // value arc from the unity point, with glow
    const float a0 = juce::jmin (unityA, angle), a1 = juce::jmax (unityA, angle);
    if (a1 - a0 > 0.002f && pos > 0.0005f)
    {
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, ringR, ringR, 0.0f, a0, a1, true);
        g.setColour (accent.withAlpha (0.18f));
        g.strokePath (arc, { ringW * 3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
        g.setColour (accent);
        g.strokePath (arc, { ringW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
    }

    // body
    const float bodyR = ringR - ringW - (small ? 3.0f : 7.0f);
    auto body = juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (centre);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillEllipse (body.translated (0.0f, 2.5f).expanded (1.5f));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2d2427), body.getX(), body.getY(),
                                             juce::Colour (0xff0c0a0b), body.getRight(), body.getBottom(), false));
    g.fillEllipse (body);
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.drawEllipse (body.reduced (0.75f), 1.2f);
    if (! small)
    {
        auto inner = body.reduced (bodyR * 0.16f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1c1618), inner.getX(), inner.getY(),
                                                 juce::Colour (0xff121011), inner.getRight(), inner.getBottom(), false));
        g.fillEllipse (inner);
    }

    // pointer
    const bool off = pos <= 0.0005f;
    const auto tip  = centre.getPointOnCircumference (bodyR * 0.86f, angle);
    const auto base = centre.getPointOnCircumference (bodyR * (small ? 0.25f : 0.45f), angle);
    g.setColour (off ? theme::textMuted : accent);
    g.drawLine ({ base, tip }, small ? 2.0f : 3.0f);
    if (! off && ! small)
    {
        g.setColour (accent.withAlpha (0.25f));
        g.drawLine ({ base, tip }, 7.0f);
    }
}

void VoiceGOCleanLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                                  bool hover, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const auto kind = b.getProperties().getWithDefault ("kind", "").toString();
    const bool on = b.getToggleState();
    const float rad = 4.0f;

    if (on)
    {
        const auto top = kind == "mute" ? theme::accentDk.brighter (0.2f) : theme::accentHv;
        const auto bot = kind == "mute" ? theme::accentDk.darker (0.3f)   : theme::accent;
        g.setGradientFill (juce::ColourGradient (top, r.getX(), r.getY(), bot, r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, rad);
        g.setColour (kind == "mute" ? theme::accentHv.withAlpha (0.7f) : juce::Colours::black.withAlpha (0.4f));
        g.drawRoundedRectangle (r, rad, 1.0f);
        if (kind != "mute")
        {
            g.setColour (theme::accentHv.withAlpha (0.18f));
            g.drawRoundedRectangle (r.expanded (1.5f), rad + 1.5f, 2.0f);
        }
    }
    else
    {
        g.setColour (down ? juce::Colours::black : (hover ? juce::Colour (0xff1c1216) : juce::Colour (0xff121013)));
        g.fillRoundedRectangle (r, rad);
        g.setColour (hover ? theme::accentHv.withAlpha (0.8f) : juce::Colour (0xff35262a));
        g.drawRoundedRectangle (r, rad, 1.0f);
    }
}

void VoiceGOCleanLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool hover, bool)
{
    g.setFont (theme::font (juce::jmin (12.0f, (float) b.getHeight() * 0.58f), true));
    g.setColour (b.getToggleState() ? theme::text : (hover ? theme::text : theme::textDim));
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
}

//==============================================================================
void PowerButton::paintButton (juce::Graphics& g, bool hover, bool)
{
    const bool bypassed = getToggleState();
    auto r = getLocalBounds().toFloat().reduced (4.0f);
    const auto c = r.getCentre();
    const float rad = juce::jmin (r.getWidth(), r.getHeight()) * 0.38f;
    const auto col = bypassed ? (hover ? theme::textDim : theme::textMuted) : theme::accentHv;

    juce::Path p;
    p.addCentredArc (c.x, c.y, rad, rad, 0.0f, juce::degreesToRadians (35.0f), juce::degreesToRadians (325.0f), true);
    p.startNewSubPath (c.x, c.y - rad * 1.25f);
    p.lineTo (c.x, c.y - rad * 0.15f);

    if (! bypassed)
    {
        g.setColour (col.withAlpha (0.22f));
        g.strokePath (p, { 7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
    }
    g.setColour (col);
    g.strokePath (p, { 2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
}

//==============================================================================
Display::Display (VoiceGOCleanProcessor& p) : proc (p)
{
    histIn.assign (historySize, 0.0f);
    histOut.assign (historySize, 0.0f);
    specV.assign (SeparationEngine::numBands, -120.0f);
    specA.assign (SeparationEngine::numBands, -120.0f);
    specR.assign (SeparationEngine::numBands, -120.0f);
    setOpaque (false);
}

void Display::update()
{
    auto& w = proc.wave;
    int s1, n1, s2, n2;
    const int ready = w.fifo.getNumReady();
    w.fifo.prepareToRead (ready, s1, n1, s2, n2);
    auto take = [this, &w] (int start, int count)
    {
        for (int i = 0; i < count; ++i)
        {
            histIn[(size_t) writePos]  = w.in[(size_t) (start + i)];
            histOut[(size_t) writePos] = w.out[(size_t) (start + i)];
            writePos = (writePos + 1) % historySize;
        }
    };
    take (s1, n1);
    take (s2, n2);
    w.fifo.finishedRead (n1 + n2);

    // spectra: 3 dB/oct tilt (around 1 kHz) so speech reads flat-ish; fast attack, slow release
    auto& sp = proc.engine.spectrum;
    const float fMax = juce::jmin (SeparationEngine::bandMaxHz, (float) juce::jmax (1.0, proc.getSampleRate()) * 0.5f);
    for (int b = 0; b < SeparationEngine::numBands; ++b)
    {
        const float fc = SeparationEngine::bandMinHz
                         * std::pow (fMax / SeparationEngine::bandMinHz, ((float) b + 0.5f) / SeparationEngine::numBands);
        const float tilt = 3.0f * std::log2 (fc / 1000.0f);
        auto upd = [&] (std::vector<float>& dst, float v)
        {
            const float db = 10.0f * std::log10 (v + 1.0e-14f) + tilt;
            float& d = dst[(size_t) b];
            d = db > d ? 0.5f * d + 0.5f * db : juce::jmax (db, d - 1.6f);
        };
        upd (specV, sp.voice[(size_t) b].load (std::memory_order_relaxed));
        upd (specA, sp.ambience[(size_t) b].load (std::memory_order_relaxed));
        upd (specR, sp.reverb[(size_t) b].load (std::memory_order_relaxed));
    }

    repaint();
}

void Display::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff050405));
    g.fillRoundedRectangle (r, 10.0f);

    {
        juce::Graphics::ScopedSaveState ss (g);
        juce::Path clip;
        clip.addRoundedRectangle (r.reduced (1.0f), 9.0f);
        g.reduceClipRegion (clip);

        if (mode == Mode::wave) paintWave (g, r);
        else                    paintSplit (g, r);

        if (proc.apvts.getRawParameterValue (ParamIDs::bypass)->load() > 0.5f)
        {
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.fillRect (r);
            g.setColour (theme::textDim);
            g.setFont (theme::font (22.0f, true));
            g.drawText ("BYPASS", r, juce::Justification::centred);
        }
    }

    g.setColour (theme::border);
    g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
}

void Display::paintWave (juce::Graphics& g, juce::Rectangle<float> r)
{
    const float midY = r.getCentreY() - 8.0f;          // a bit higher: the panel curve covers the bottom
    const float halfH = r.getHeight() * 0.40f;

    // grid
    for (float db : { -6.0f, -18.0f, -30.0f, -42.0f })
    {
        const float off = juce::jlimit (0.0f, 1.0f, (db - waveFloorDb) / -waveFloorDb) * halfH;
        g.setColour (juce::Colour (0xff160d0f));
        g.drawHorizontalLine ((int) (midY - off), r.getX(), r.getRight());
        g.drawHorizontalLine ((int) (midY + off), r.getX(), r.getRight());
    }
    g.setColour (juce::Colour (0xff24161a));
    g.drawHorizontalLine ((int) midY, r.getX(), r.getRight());

    const int n = historySize;
    const float dx = r.getWidth() / (float) (n - 1);

    auto envelope = [&] (const std::vector<float>& h)
    {
        juce::Path p;
        for (int i = 0; i < n; ++i)
        {
            const float v = peakToNorm (h[(size_t) ((writePos + i) % n)]) * halfH;
            const float xx = r.getX() + (float) i * dx;
            if (i == 0) p.startNewSubPath (xx, midY - v);
            else        p.lineTo (xx, midY - v);
        }
        for (int i = n - 1; i >= 0; --i)
        {
            const float v = peakToNorm (h[(size_t) ((writePos + i) % n)]) * halfH;
            p.lineTo (r.getX() + (float) i * dx, midY + v);
        }
        p.closeSubPath();
        return p;
    };

    // input (what came in) – dark red ghost
    const auto pin = envelope (histIn);
    g.setColour (theme::accentDk.withAlpha (0.45f));
    g.fillPath (pin);
    g.setColour (theme::accentDk.brighter (0.4f).withAlpha (0.7f));
    g.strokePath (pin, juce::PathStrokeType (1.0f));

    // output – bright red with a hot core
    const auto pout = envelope (histOut);
    juce::ColourGradient grad (theme::accent.withAlpha (0.85f), r.getX(), midY - halfH,
                               theme::accent.withAlpha (0.85f), r.getX(), midY + halfH, false);
    grad.addColour (0.5, theme::accentHv);
    grad.addColour (0.42, theme::accentHv.withAlpha (0.95f));
    grad.addColour (0.58, theme::accentHv.withAlpha (0.95f));
    g.setGradientFill (grad);
    g.fillPath (pout);
    g.setColour (theme::accentHv.withAlpha (0.25f));
    g.strokePath (pout, juce::PathStrokeType (3.0f));
    g.setColour (juce::Colour (0xffff8a8a));
    g.strokePath (pout, juce::PathStrokeType (0.9f));

    // fade the oldest part on the left
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff050405), r.getX(), 0.0f,
                                             juce::Colour (0x00050405), r.getX() + 90.0f, 0.0f, false));
    g.fillRect (r.withWidth (90.0f));

    // legend
    auto leg = juce::Rectangle<float> (r.getX() + 14.0f, r.getY() + 12.0f, 200.0f, 14.0f);
    g.setFont (theme::font (10.5f, true));
    g.setColour (theme::accentDk.brighter (0.4f));
    g.fillRoundedRectangle (leg.removeFromLeft (10.0f).withSizeKeepingCentre (10.0f, 10.0f), 2.0f);
    leg.removeFromLeft (6.0f);
    g.setColour (theme::textDim);
    g.drawText ("INPUT", leg.removeFromLeft (44.0f), juce::Justification::centredLeft);
    g.setColour (theme::accentHv);
    g.fillRoundedRectangle (leg.removeFromLeft (10.0f).withSizeKeepingCentre (10.0f, 10.0f), 2.0f);
    leg.removeFromLeft (6.0f);
    g.setColour (theme::textDim);
    g.drawText ("OUTPUT", leg, juce::Justification::centredLeft);
}

void Display::paintSplit (juce::Graphics& g, juce::Rectangle<float> r)
{
    const float w = r.getWidth() / 3.0f;
    auto a = r.removeFromLeft (w);
    auto v = r.removeFromLeft (w);
    auto rv = r;

    paintSpectrum (g, a,  specA, theme::ambience, "AMBIENCE",     ParamIDs::ambience);
    paintSpectrum (g, v,  specV, theme::voice,    "VOICE",        ParamIDs::voice);
    paintSpectrum (g, rv, specR, theme::reverb,   "VOICE REVERB", ParamIDs::reverb);

    g.setColour (theme::border);
    g.drawVerticalLine ((int) v.getX(), v.getY() + 8.0f, v.getBottom() - 8.0f);
    g.drawVerticalLine ((int) rv.getX(), rv.getY() + 8.0f, rv.getBottom() - 8.0f);
}

void Display::paintSpectrum (juce::Graphics& g, juce::Rectangle<float> r, const std::vector<float>& bands,
                             juce::Colour col, const juce::String& name, const juce::String& paramID)
{
    auto inner = r.reduced (10.0f, 0.0f);
    auto header = inner.removeFromTop (30.0f);
    inner.removeFromBottom (54.0f);   // room for the panel curve + frequency labels
    auto plot = inner;

    const float topDb = -20.0f, botDb = -110.0f;
    auto yOf = [&] (float db) { return juce::jmap (juce::jlimit (botDb, topDb, db), botDb, topDb, plot.getBottom(), plot.getY()); };

    // frequency grid: 100 Hz, 1 kHz, 10 kHz
    const float fMax = juce::jmin (SeparationEngine::bandMaxHz, (float) juce::jmax (1.0, proc.getSampleRate()) * 0.5f);
    auto xOf = [&] (float f)
    {
        return plot.getX() + plot.getWidth() * std::log (f / SeparationEngine::bandMinHz) / std::log (fMax / SeparationEngine::bandMinHz);
    };
    g.setFont (theme::font (9.5f));
    for (auto [f, label] : { std::pair<float, const char*> { 100.0f, "100" }, { 1000.0f, "1k" }, { 10000.0f, "10k" } })
    {
        const float x = xOf (f);
        g.setColour (juce::Colour (0xff160d0f));
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        g.setColour (theme::textMuted);
        g.drawText (label, juce::Rectangle<float> (x - 15.0f, plot.getBottom() + 1.0f, 30.0f, 12.0f), juce::Justification::centred);
    }

    // spectrum
    const int nb = (int) bands.size();
    juce::Path fill, line;
    fill.startNewSubPath (plot.getX(), plot.getBottom());
    for (int b = 0; b < nb; ++b)
    {
        const float x = plot.getX() + plot.getWidth() * ((float) b + 0.5f) / (float) nb;
        const float y = yOf (bands[(size_t) b]);
        fill.lineTo (x, y);
        if (b == 0) line.startNewSubPath (plot.getX(), y);
        line.lineTo (x, y);
    }
    line.lineTo (plot.getRight(), yOf (bands[(size_t) nb - 1]));
    fill.lineTo (plot.getRight(), yOf (bands[(size_t) nb - 1]));
    fill.lineTo (plot.getRight(), plot.getBottom());
    fill.closeSubPath();

    g.setGradientFill (juce::ColourGradient (col.withAlpha (0.55f), 0.0f, plot.getY(),
                                             col.withAlpha (0.05f), 0.0f, plot.getBottom(), false));
    g.fillPath (fill);
    g.setColour (col.withAlpha (0.3f));
    g.strokePath (line, juce::PathStrokeType (3.0f));
    g.setColour (col.brighter (0.3f));
    g.strokePath (line, juce::PathStrokeType (1.2f));

    // header / footer text
    g.setFont (theme::font (11.5f, true));
    g.setColour (col);
    g.drawText (name, header.withTrimmedTop (6.0f), juce::Justification::centredLeft);

    // measured room (neural mode): shown under the reverb title
    if (paramID == ParamIDs::reverb && proc.engine.isNeuralActive())
    {
        g.setFont (theme::font (10.0f));
        g.setColour (theme::textDim);
        g.drawText ("SALA ~" + juce::String (proc.engine.roomRt60.load(), 2) + " s",
                    header.translated (0.0f, 16.0f).withTrimmedTop (6.0f), juce::Justification::centredLeft);
    }

    const float db = proc.apvts.getRawParameterValue (paramID)->load();
    g.setFont (theme::font (14.0f, true));
    g.setColour (db <= KnobCurve::minDb + 0.05f ? theme::textMuted : theme::text);
    g.drawText (KnobCurve::format (db), header.withTrimmedTop (6.0f), juce::Justification::centredRight);
}

//==============================================================================
KnobStrip::KnobStrip (const juce::String& t, const juce::String& tip, juce::Colour c,
                      juce::AudioProcessorValueTreeState& state, const juce::String& paramID,
                      const juce::String& soloID, const juce::String& muteID)
    : title (t), accent (c),
      knobAtt (state, paramID, knob),
      soloAtt (state, soloID, solo),
      muteAtt (state, muteID, mute)
{
    knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    knob.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    knob.setColour (juce::Slider::rotarySliderFillColourId, accent);
    knob.getProperties().set ("unityPos", (double) KnobCurve::unityPos);
    knob.setDoubleClickReturnValue (true, 0.0);
    knob.setMouseDragSensitivity (240);
    knob.setTooltip (tip + "\nDuplo clique: volta para 0 dB.");
    knob.onValueChange = [this] { repaint (valueArea); };
    addAndMakeVisible (knob);

    for (auto* b : { &solo, &mute })
    {
        b->setClickingTogglesState (true);
        b->getProperties().set ("kind", b == &solo ? "solo" : "mute");
        addAndMakeVisible (b);
    }
    solo.setTooltip ("Solo: ouvir só " + title.toLowerCase());
    mute.setTooltip ("Mute: remover " + title.toLowerCase());
    solo.onClick = mute.onClick = [this] { repaint(); };
}

void KnobStrip::resized()
{
    auto r = getLocalBounds();
    titleArea = r.removeFromTop (20);
    auto buttons = r.removeFromTop (22).withSizeKeepingCentre (62, 20);
    solo.setBounds (buttons.removeFromLeft (28));
    mute.setBounds (buttons.removeFromRight (28));
    r.removeFromTop (4);
    valueArea = r.removeFromBottom (24);
    const int s = juce::jmin (r.getWidth(), r.getHeight());
    knob.setBounds (r.withSizeKeepingCentre (s, s));
}

void KnobStrip::paint (juce::Graphics& g)
{
    g.setFont (theme::font (12.5f, true));
    g.setColour (theme::text);
    g.drawText (title.toUpperCase(), titleArea, juce::Justification::centred);

    const float db = (float) knob.getValue();
    const auto txt = KnobCurve::format (db);
    g.setFont (theme::font (15.0f, true));
    g.setColour (txt == "OFF" ? theme::textMuted : theme::text);
    g.drawText (txt, valueArea, juce::Justification::centred);
}

//==============================================================================
TrimStrip::TrimStrip (const juce::String& t, juce::AudioProcessorValueTreeState& state, const juce::String& paramID)
    : title (t), att (state, paramID, knob)
{
    knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    knob.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    knob.setColour (juce::Slider::rotarySliderFillColourId, theme::accentHv);
    knob.getProperties().set ("unityPos", 0.5);
    knob.getProperties().set ("small", true);
    knob.setDoubleClickReturnValue (true, 0.0);
    knob.setMouseDragSensitivity (200);
    knob.setTooltip (title + " (duplo clique: 0 dB)");
    knob.onValueChange = [this] { repaint (valueArea); };
    addAndMakeVisible (knob);
}

void TrimStrip::setPeak (float pk)
{
    const float db = juce::Decibels::gainToDecibels (pk, -100.0f);
    meterDb = db > meterDb ? db : juce::jmax (db, meterDb - 1.5f);
    if (db >= holdDb) { holdDb = db; holdFrames = 45; }
    else if (--holdFrames <= 0) holdDb = juce::jmax (meterDb, holdDb - 1.0f);
    repaint (meterArea.getSmallestIntegerContainer().expanded (2));
}

void TrimStrip::resized()
{
    auto r = getLocalBounds();
    titleArea = r.removeFromTop (20);
    valueArea = r.removeFromBottom (22);
    r.removeFromTop (6);
    auto row = r.withSizeKeepingCentre (juce::jmin (r.getWidth(), 82), r.getHeight());
    meterArea = row.removeFromRight (10).toFloat().reduced (0.0f, 4.0f);
    row.removeFromRight (6);
    knob.setBounds (row.withSizeKeepingCentre (row.getWidth(), row.getWidth()));
}

void TrimStrip::paint (juce::Graphics& g)
{
    g.setFont (theme::font (11.5f, true));
    g.setColour (theme::textDim);
    g.drawText (title.toUpperCase(), titleArea, juce::Justification::centred);

    g.setFont (theme::font (12.5f, true));
    g.setColour (theme::text);
    g.drawText (trimText ((float) knob.getValue()), valueArea, juce::Justification::centred);

    // vertical meter -60..+6 dBFS
    auto m = meterArea;
    g.setColour (juce::Colour (0xff050405));
    g.fillRoundedRectangle (m, 3.0f);
    auto norm = [] (float db) { return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f); };
    const float h = m.getHeight() * norm (meterDb);
    if (h > 0.5f)
    {
        auto bar = m.withTop (m.getBottom() - h);
        g.setGradientFill (juce::ColourGradient (theme::accentHv, 0.0f, m.getY(),
                                                 theme::accentDk, 0.0f, m.getBottom(), false));
        g.fillRoundedRectangle (bar, 3.0f);
    }
    const float hy = m.getBottom() - m.getHeight() * norm (holdDb);
    if (holdDb > -60.0f)
    {
        g.setColour (holdDb > 0.0f ? juce::Colours::white : theme::accentHv);
        g.fillRect (m.getX(), hy - 1.0f, m.getWidth(), 2.0f);
    }
    g.setColour (theme::border);
    g.drawRoundedRectangle (m, 3.0f, 1.0f);
    // 0 dB mark
    const float y0 = m.getBottom() - m.getHeight() * norm (0.0f);
    g.setColour (theme::textMuted);
    g.drawHorizontalLine ((int) y0, m.getX() - 3.0f, m.getX());
}

//==============================================================================
MainContent::MainContent (VoiceGOCleanProcessor& p)
    : proc (p), display (p),
      powerAtt (p.apvts, ParamIDs::bypass, power),
      ambience ("Ambience", "Ruido de fundo constante: chiado, zumbido, ar-condicionado, som da sala.",
                theme::ambience, p.apvts, ParamIDs::ambience, ParamIDs::soloAmb, ParamIDs::muteAmb),
      voice    ("Voice", "A voz direta (seca).",
                theme::voice, p.apvts, ParamIDs::voice, ParamIDs::soloVoice, ParamIDs::muteVoice),
      reverb   ("Voice Reverb", "As reflexoes da sala sobre a voz (reverb).",
                theme::reverb, p.apvts, ParamIDs::reverb, ParamIDs::soloRev, ParamIDs::muteRev),
      input  ("Input",  p.apvts, ParamIDs::input),
      output ("Output", p.apvts, ParamIDs::output)
{
    addAndMakeVisible (display);
    addAndMakeVisible (panelShape);
    for (auto* c : std::initializer_list<juce::Component*> { &ambience, &voice, &reverb, &input, &output, &power })
        addAndMakeVisible (c);

    power.setTooltip ("Liga / desliga o VoiceGO Clean (bypass sem mudar a latencia)");
    power.onClick = [this] { repaint(); };

    const bool split = proc.apvts.state.getProperty ("view", "wave").toString() == "split";
    for (auto* b : { &waveBtn, &splitBtn })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (101);
        b->getProperties().set ("kind", "view");
        addAndMakeVisible (b);
    }
    (split ? splitBtn : waveBtn).setToggleState (true, juce::dontSendNotification);
    display.setMode (split ? Display::Mode::split : Display::Mode::wave);

    waveBtn.onClick = [this]
    {
        if (! waveBtn.getToggleState()) return;
        display.setMode (Display::Mode::wave);
        proc.apvts.state.setProperty ("view", "wave", nullptr);
    };
    splitBtn.onClick = [this]
    {
        if (! splitBtn.getToggleState()) return;
        display.setMode (Display::Mode::split);
        proc.apvts.state.setProperty ("view", "split", nullptr);
    };
    waveBtn.setTooltip ("Forma de onda: entrada x saida");
    splitBtn.setTooltip ("Espectro de cada componente separado");

    setSize (baseW, baseH);
}

void MainContent::tick()
{
    display.update();
    input.setPeak (proc.inputPeak.exchange (0.0f, std::memory_order_relaxed));
    output.setPeak (proc.outputPeak.exchange (0.0f, std::memory_order_relaxed));
}

void MainContent::paint (juce::Graphics& g)
{
    const auto W = (float) getWidth();
    g.fillAll (theme::bg);

    // ---- header -------------------------------------------------------------
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff141012), 0.0f, 0.0f, theme::bg, 0.0f, 52.0f, false));
    g.fillRect (0.0f, 0.0f, W, 52.0f);

    // logo: waveform bars inside a ring
    {
        auto ring = juce::Rectangle<float> (16.0f, 9.0f, 34.0f, 34.0f);
        g.setColour (theme::accent.withAlpha (0.2f));
        g.drawEllipse (ring.expanded (1.5f), 4.0f);
        g.setColour (theme::accent);
        g.drawEllipse (ring, 1.6f);
        const float hs[] = { 0.3f, 0.6f, 1.0f, 0.75f, 0.4f };
        const float bw = 3.0f, gap = 2.2f;
        const float total = 5 * bw + 4 * gap;
        for (int i = 0; i < 5; ++i)
        {
            const float bh = 18.0f * hs[i];
            g.setColour (i == 2 ? theme::accentHv : theme::text.withAlpha (0.85f));
            g.fillRoundedRectangle (ring.getCentreX() - total * 0.5f + (float) i * (bw + gap),
                                    ring.getCentreY() - bh * 0.5f, bw, bh, 1.5f);
        }
    }

    g.setFont (theme::font (23.0f, true));
    const auto f = g.getCurrentFont();
    const float wFirst  = (float) juce::GlyphArrangement::getStringWidthInt (f, "VoiceGO ");
    const float wSecond = (float) juce::GlyphArrangement::getStringWidthInt (f, "Clean");
    g.setColour (theme::text);
    g.drawText ("VoiceGO", juce::Rectangle<float> (60.0f, 0.0f, wFirst + 2.0f, 52.0f), juce::Justification::centredLeft);
    g.setColour (theme::accentHv);
    g.drawText ("Clean", juce::Rectangle<float> (60.0f + wFirst, 0.0f, wSecond + 4.0f, 52.0f), juce::Justification::centredLeft);
    g.setFont (theme::font (10.0f));
    g.setColour (theme::textMuted);
    g.drawText ("v" JucePlugin_VersionString, juce::Rectangle<float> (60.0f + wFirst + wSecond + 10.0f, 4.0f, 60.0f, 52.0f),
                juce::Justification::centredLeft);

    // engine badge: NEURAL (DeepFilterNet3) or DSP (fallback)
    {
        const bool neural = proc.engine.isNeuralActive();
        auto badge = juce::Rectangle<float> (60.0f + wFirst + wSecond + 54.0f, 17.0f, neural ? 62.0f : 40.0f, 18.0f);
        g.setColour (neural ? theme::accent.withAlpha (0.18f) : theme::panelAlt);
        g.fillRoundedRectangle (badge, 9.0f);
        g.setColour (neural ? theme::accentHv : theme::textMuted);
        g.drawRoundedRectangle (badge.reduced (0.5f), 9.0f, 1.0f);
        g.setFont (theme::font (10.0f, true));
        g.drawText (neural ? "NEURAL" : "DSP", badge, juce::Justification::centred);
    }

}

void PanelShape::paint (juce::Graphics& g)
{
    const auto W = (float) getWidth(), H = (float) getHeight();
    // ---- control panel with the centre bump ------------------------------------
    const float cx = W * 0.5f;
    juce::Path panelPath;
    panelPath.startNewSubPath (0.0f, top);
    panelPath.lineTo (cx - 190.0f, top);
    panelPath.cubicTo (cx - 130.0f, top, cx - 140.0f, bump, cx - 70.0f, bump);
    panelPath.lineTo (cx + 70.0f, bump);
    panelPath.cubicTo (cx + 140.0f, bump, cx + 130.0f, top, cx + 190.0f, top);
    panelPath.lineTo (W, top);
    juce::Path filled (panelPath);
    filled.lineTo (W, H);
    filled.lineTo (0.0f, H);
    filled.closeSubPath();

    g.setGradientFill (juce::ColourGradient (theme::panelAlt, 0.0f, bump, theme::bg, 0.0f, H, false));
    g.fillPath (filled);
    g.setColour (theme::accent.withAlpha (0.18f));
    g.strokePath (panelPath, juce::PathStrokeType (6.0f));
    g.setColour (theme::accent);
    g.strokePath (panelPath, juce::PathStrokeType (1.6f));

    // separators between the trims and the knobs
    g.setColour (theme::border);
    g.drawVerticalLine (118, top + 22.0f, H - 18.0f);
    g.drawVerticalLine ((int) W - 118, top + 22.0f, H - 18.0f);
}

void MainContent::resized()
{
    const int W = getWidth();
    power.setBounds (W - 54, 9, 36, 36);
    splitBtn.setBounds (W - 128, 15, 60, 22);
    waveBtn.setBounds (W - 192, 15, 60, 22);

    display.setBounds (12, 56, W - 24, 240);
    panelShape.setBounds (getLocalBounds());

    input.setBounds (14, 326, 98, 160);
    output.setBounds (W - 112, 326, 98, 160);

    const int cx = W / 2;
    voice.setBounds (cx - 95, 280, 190, 212);
    ambience.setBounds (cx - 95 - 170, 320, 160, 172);
    reverb.setBounds (cx + 95 + 10, 320, 160, 172);
}

//==============================================================================
VoiceGOCleanEditor::VoiceGOCleanEditor (VoiceGOCleanProcessor& p)
    : AudioProcessorEditor (p), content (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (content);

    setResizable (true, true);
    setResizeLimits (MainContent::baseW * 7 / 10, MainContent::baseH * 7 / 10,
                     MainContent::baseW * 2, MainContent::baseH * 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) MainContent::baseW / MainContent::baseH);
    setSize (MainContent::baseW, MainContent::baseH);

    startTimerHz (30);
}

VoiceGOCleanEditor::~VoiceGOCleanEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void VoiceGOCleanEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg);
}

void VoiceGOCleanEditor::resized()
{
    const float scale = (float) getWidth() / (float) MainContent::baseW;
    content.setTransform (juce::AffineTransform::scale (scale));
}

void VoiceGOCleanEditor::timerCallback()
{
    content.tick();
}
