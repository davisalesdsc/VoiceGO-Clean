#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SeparationEngine.h"

namespace ParamIDs
{
    inline constexpr auto ambience  = "ambience";
    inline constexpr auto voice     = "voice";
    inline constexpr auto reverb    = "reverb";
    inline constexpr auto input     = "input";
    inline constexpr auto output    = "output";
    inline constexpr auto bypass    = "bypass";
    inline constexpr auto soloAmb   = "solo_ambience";
    inline constexpr auto soloVoice = "solo_voice";
    inline constexpr auto soloRev   = "solo_reverb";
    inline constexpr auto muteAmb   = "mute_ambience";
    inline constexpr auto muteVoice = "mute_voice";
    inline constexpr auto muteRev   = "mute_reverb";
}

/** Knob curve shared by the processor and the editor:
    0..0.7 of the travel covers OFF..0 dB, 0.7..1 covers 0..+12 dB. */
namespace KnobCurve
{
    inline constexpr float minDb = -60.0f;
    inline constexpr float maxDb = 12.0f;
    inline constexpr float unityPos = 0.7f;

    float toDb (float pos);
    float toPos (float db);
    float dbToGain (float db);
    juce::String format (float db);
}

class VoiceGOCleanProcessor final : public juce::AudioProcessor
{
public:
    VoiceGOCleanProcessor();
    ~VoiceGOCleanProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    SeparationEngine engine;

    //==============================================================================
    /** Waveform history for the display: one (input, output) peak pair per
        ~5 ms, input delayed by the plugin latency so both line up. */
    struct WaveFifo
    {
        static constexpr int capacity = 4096;
        juce::AbstractFifo fifo { capacity };
        std::array<float, capacity> in {}, out {};
    };
    WaveFifo wave;

    std::atomic<float> inputPeak { 0.0f }, outputPeak { 0.0f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void pushWave (float inPk, float outPk) noexcept;

    std::atomic<float>* pAmb = nullptr;
    std::atomic<float>* pVoice = nullptr;
    std::atomic<float>* pRev = nullptr;
    std::atomic<float>* pIn = nullptr;
    std::atomic<float>* pOut = nullptr;
    std::atomic<float>* pBypass = nullptr;
    std::atomic<float>* pSolo[3] {};
    std::atomic<float>* pMute[3] {};
    juce::AudioParameterBool* bypassParam = nullptr;

    juce::SmoothedValue<float> inGain, outGain;

    // waveform chunking
    int chunkLen = 256, chunkPos = 0;
    float chunkIn = 0.0f, chunkOut = 0.0f;
    std::array<float, 64> inDelay {};
    int inDelayPos = 0, inDelayLen = 8;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoiceGOCleanProcessor)
};
