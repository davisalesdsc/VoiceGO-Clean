#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
float KnobCurve::toDb (float pos)
{
    pos = juce::jlimit (0.0f, 1.0f, pos);
    if (pos <= unityPos)
    {
        const float t = 1.0f - pos / unityPos;    // 1 at OFF, 0 at unity
        return minDb * t * t;
    }
    return maxDb * (pos - unityPos) / (1.0f - unityPos);
}

float KnobCurve::toPos (float db)
{
    db = juce::jlimit (minDb, maxDb, db);
    if (db <= 0.0f)
        return unityPos * (1.0f - std::sqrt (db / minDb));
    return unityPos + (1.0f - unityPos) * db / maxDb;
}

float KnobCurve::dbToGain (float db)
{
    return db <= minDb + 0.05f ? 0.0f : juce::Decibels::decibelsToGain (db, -1000.0f);
}

juce::String KnobCurve::format (float db)
{
    if (db <= minDb + 0.05f)
        return "OFF";
    if (std::abs (db) < 0.05f)
        return "0.0 dB";
    return (db > 0 ? "+" : "") + juce::String (db, 1) + " dB";
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout VoiceGOCleanProcessor::createLayout()
{
    juce::NormalisableRange<float> range (
        KnobCurve::minDb, KnobCurve::maxDb,
        [] (float, float, float pos) { return KnobCurve::toDb (pos); },
        [] (float, float, float db)  { return KnobCurve::toPos (db); },
        [] (float, float, float db)  { return std::round (db * 10.0f) / 10.0f; });

    auto attrs = juce::AudioParameterFloatAttributes()
                     .withLabel ("dB")
                     .withStringFromValueFunction ([] (float v, int) { return KnobCurve::format (v); })
                     .withValueFromStringFunction ([] (const juce::String& s)
                     {
                         if (s.trim().equalsIgnoreCase ("off"))
                             return KnobCurve::minDb;
                         return s.getFloatValue();
                     });

    auto trimAttrs = juce::AudioParameterFloatAttributes()
                         .withLabel ("dB")
                         .withStringFromValueFunction ([] (float v, int)
                         {
                             return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB";
                         });

    using P = juce::ParameterID;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterFloat> (P { ParamIDs::ambience, 1 }, "Ambience",     range, 0.0f, attrs));
    layout.add (std::make_unique<juce::AudioParameterFloat> (P { ParamIDs::voice,    1 }, "Voice",        range, 0.0f, attrs));
    layout.add (std::make_unique<juce::AudioParameterFloat> (P { ParamIDs::reverb,   1 }, "Voice Reverb", range, 0.0f, attrs));
    layout.add (std::make_unique<juce::AudioParameterFloat> (P { ParamIDs::input,    1 }, "Input",
                                                             juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, trimAttrs));
    layout.add (std::make_unique<juce::AudioParameterFloat> (P { ParamIDs::output,   1 }, "Output",
                                                             juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, trimAttrs));
    layout.add (std::make_unique<juce::AudioParameterBool>  (P { ParamIDs::bypass,    1 }, "Bypass", false));
    layout.add (std::make_unique<juce::AudioParameterBool>  (P { ParamIDs::soloAmb,   1 }, "Solo Ambience", false));
    layout.add (std::make_unique<juce::AudioParameterBool>  (P { ParamIDs::soloVoice, 1 }, "Solo Voice", false));
    layout.add (std::make_unique<juce::AudioParameterBool>  (P { ParamIDs::soloRev,   1 }, "Solo Voice Reverb", false));
    layout.add (std::make_unique<juce::AudioParameterBool>  (P { ParamIDs::muteAmb,   1 }, "Mute Ambience", false));
    layout.add (std::make_unique<juce::AudioParameterBool>  (P { ParamIDs::muteVoice, 1 }, "Mute Voice", false));
    layout.add (std::make_unique<juce::AudioParameterBool>  (P { ParamIDs::muteRev,   1 }, "Mute Voice Reverb", false));
    return layout;
}

VoiceGOCleanProcessor::VoiceGOCleanProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "VoiceGOCleanState", createLayout())
{
    pAmb    = apvts.getRawParameterValue (ParamIDs::ambience);
    pVoice  = apvts.getRawParameterValue (ParamIDs::voice);
    pRev    = apvts.getRawParameterValue (ParamIDs::reverb);
    pIn     = apvts.getRawParameterValue (ParamIDs::input);
    pOut    = apvts.getRawParameterValue (ParamIDs::output);
    pBypass = apvts.getRawParameterValue (ParamIDs::bypass);
    pSolo[0] = apvts.getRawParameterValue (ParamIDs::soloAmb);
    pSolo[1] = apvts.getRawParameterValue (ParamIDs::soloVoice);
    pSolo[2] = apvts.getRawParameterValue (ParamIDs::soloRev);
    pMute[0] = apvts.getRawParameterValue (ParamIDs::muteAmb);
    pMute[1] = apvts.getRawParameterValue (ParamIDs::muteVoice);
    pMute[2] = apvts.getRawParameterValue (ParamIDs::muteRev);
    bypassParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (ParamIDs::bypass));
}

bool VoiceGOCleanProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void VoiceGOCleanProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate, juce::jmax (1, getTotalNumOutputChannels()));
    engine.setGains (KnobCurve::dbToGain (pVoice->load()),
                     KnobCurve::dbToGain (pAmb->load()),
                     KnobCurve::dbToGain (pRev->load()));
    engine.reset();
    setLatencySamples (engine.getLatencySamples());

    inGain.reset (sampleRate, 0.05);
    outGain.reset (sampleRate, 0.05);
    inGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pIn->load()));
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pOut->load()));

    chunkLen = juce::jmax (32, (int) std::round (sampleRate * 0.0055));
    chunkPos = 0;
    chunkIn = chunkOut = 0.0f;
    inDelay.fill (0.0f);
    inDelayPos = 0;
    inDelayLen = juce::jlimit (1, (int) inDelay.size() - 1,
                               (int) std::round ((double) engine.getLatencySamples() / chunkLen));
}

double VoiceGOCleanProcessor::getTailLengthSeconds() const
{
    return getSampleRate() > 0 ? (double) engine.getLatencySamples() / getSampleRate() : 0.05;
}

void VoiceGOCleanProcessor::pushWave (float inPk, float outPk) noexcept
{
    // align the input with the (delayed) output
    inDelay[(size_t) inDelayPos] = inPk;
    const float delayedIn = inDelay[(size_t) ((inDelayPos + (int) inDelay.size() - inDelayLen) % (int) inDelay.size())];
    inDelayPos = (inDelayPos + 1) % (int) inDelay.size();

    int s1, n1, s2, n2;
    wave.fifo.prepareToWrite (1, s1, n1, s2, n2);
    if (n1 > 0)
    {
        wave.in[(size_t) s1] = delayedIn;
        wave.out[(size_t) s1] = outPk;
        wave.fifo.finishedWrite (1);
    }
}

void VoiceGOCleanProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int chans = getTotalNumOutputChannels();

    for (int ch = getTotalNumInputChannels(); ch < chans; ++ch)
        buffer.clear (ch, 0, n);

    const bool bypassed = pBypass->load() > 0.5f;

    // component gains, with solo / mute
    float g[3] = { KnobCurve::dbToGain (pAmb->load()),
                   KnobCurve::dbToGain (pVoice->load()),
                   KnobCurve::dbToGain (pRev->load()) };
    const bool anySolo = pSolo[0]->load() > 0.5f || pSolo[1]->load() > 0.5f || pSolo[2]->load() > 0.5f;
    for (int i = 0; i < 3; ++i)
    {
        if (pMute[i]->load() > 0.5f || (anySolo && pSolo[i]->load() <= 0.5f))
            g[i] = 0.0f;
        if (bypassed)
            g[i] = 1.0f;
    }
    engine.setGains (g[1], g[0], g[2]);

    // bypass keeps the latency (engine at unity == delayed dry signal)
    inGain.setTargetValue  (bypassed ? 1.0f : juce::Decibels::decibelsToGain (pIn->load()));
    outGain.setTargetValue (bypassed ? 1.0f : juce::Decibels::decibelsToGain (pOut->load()));

    if (inGain.isSmoothing())
        inGain.applyGain (buffer, n);
    else
        buffer.applyGain (inGain.getTargetValue());

    // input peaks (per waveform chunk) — collected before processing
    float inPk = 0.0f;
    std::array<float, 64> chunkInPeaks {};
    {
        int pos = chunkPos, idx = 0;
        float pk = chunkIn;
        for (int i = 0; i < n; ++i)
        {
            float a = 0.0f;
            for (int ch = 0; ch < chans; ++ch)
                a = juce::jmax (a, std::abs (buffer.getSample (ch, i)));
            pk = juce::jmax (pk, a);
            inPk = juce::jmax (inPk, a);
            if (++pos >= chunkLen)
            {
                if (idx < (int) chunkInPeaks.size()) chunkInPeaks[(size_t) idx++] = pk;
                pk = 0.0f; pos = 0;
            }
        }
        chunkIn = pk;
    }

    engine.process (buffer);

    if (outGain.isSmoothing())
        outGain.applyGain (buffer, n);
    else
        buffer.applyGain (outGain.getTargetValue());

    // output peaks + waveform push
    float outPk = 0.0f;
    {
        int idx = 0;
        for (int i = 0; i < n; ++i)
        {
            float a = 0.0f;
            for (int ch = 0; ch < chans; ++ch)
                a = juce::jmax (a, std::abs (buffer.getSample (ch, i)));
            chunkOut = juce::jmax (chunkOut, a);
            outPk = juce::jmax (outPk, a);
            if (++chunkPos >= chunkLen)
            {
                pushWave (idx < (int) chunkInPeaks.size() ? chunkInPeaks[(size_t) idx] : 0.0f, chunkOut);
                ++idx;
                chunkOut = 0.0f; chunkPos = 0;
            }
        }
    }

    inputPeak.store (juce::jmax (inputPeak.load (std::memory_order_relaxed), inPk), std::memory_order_relaxed);
    outputPeak.store (juce::jmax (outputPeak.load (std::memory_order_relaxed), outPk), std::memory_order_relaxed);
}

juce::AudioProcessorEditor* VoiceGOCleanProcessor::createEditor()
{
    return new VoiceGOCleanEditor (*this);
}

void VoiceGOCleanProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void VoiceGOCleanProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VoiceGOCleanProcessor();
}
