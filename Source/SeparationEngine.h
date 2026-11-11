#pragma once

#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <vector>
#include <array>
#include "NeuralVoice.h"

/**
    VoiceGO Clean separation engine.

    Splits the incoming signal, bin by bin in the STFT domain, into three
    components and lets each one be turned up or down independently:

      - Voice     : the direct (dry) part of the signal
      - Ambience  : stationary background noise (hiss, hum, air conditioning,
                    room tone), tracked with minimum statistics
      - Reverb    : late reverberation of the voice, estimated with a
                    statistical room decay model (Lebart / Habets style)

    The three masks always add up to 1, so with every gain at 0 dB the output
    is a bit-exact (up to float rounding) delayed copy of the input.

    Two analysis modes:
      - Neural (default): DeepFilterNet3 estimates the clean voice; its
        magnitude ratio against the input becomes the voice mask, and the
        late-reverb model then runs on that clean voice only.
        Latency = FFT size (1024 at 44.1/48 kHz) + neural delay (~40 ms).
      - DSP (fallback when the model is unavailable): minimum statistics +
        decision-directed Wiener mask. Latency = FFT size (2048 at 48 kHz).
*/
class SeparationEngine
{
public:
    SeparationEngine() = default;

    /** Call before prepare(); true = use the neural voice model when available. */
    void setNeuralEnabled (bool shouldUse) noexcept { neuralWanted = shouldUse; }
    bool isNeuralActive() const noexcept { return useNeural; }
    const NeuralVoice& getNeural() const noexcept { return neural; }

    void prepare (double sampleRate, int numChannels);
    void reset();

    int getLatencySamples() const noexcept { return fftSize + (useNeural ? neuralDelay : 0); }

    /** Linear gains (0 = removed, 1 = unchanged). Thread-safe. */
    void setGains (float voice, float ambience, float reverb) noexcept;

    /** Processes up to the number of channels given to prepare(), in place. */
    void process (juce::AudioBuffer<float>& buffer) noexcept;

    /** Mean-square level of each separated component on the input (for meters). */
    struct Levels
    {
        std::atomic<float> voice { 0.0f }, ambience { 0.0f }, reverb { 0.0f };
    };

    Levels levels;

    /** Per-component spectrum (mean power per log band, 40 Hz - 20 kHz), for the display. */
    static constexpr int numBands = 64;
    struct Spectrum
    {
        std::array<std::atomic<float>, numBands> voice {}, ambience {}, reverb {};
    };
    Spectrum spectrum;
    static constexpr float bandMinHz = 40.0f, bandMaxHz = 20000.0f;

private:
    void processFrame() noexcept;

    // --- configuration -----------------------------------------------------
    double sr = 48000.0;
    int numCh = 2;
    int fftOrder = 11, fftSize = 2048, hop = 512, numBins = 1025;
    float olaScale = 1.0f;
    float windowEnergy = 1.0f;

    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window;

    // --- per-channel streaming buffers ---------------------------------------
    std::vector<std::vector<float>> inBuf, outAccum, outReady, fftData;
    int hopCount = 0;

    // --- per-bin analysis state ----------------------------------------------
    std::vector<float> power, smoothPower, noise, noiseSmooth;
    std::vector<float> curMin;                 // running minimum of current sub-window
    std::vector<std::vector<float>> subMins;   // minima of the last U sub-windows
    int subIndex = 0, subCount = 0, subLen = 16;
    static constexpr int numSubWindows = 8;

    std::vector<std::vector<float>> history;   // ring of (direct + reverb) power
    int histPos = 0, reverbDelayFrames = 4;
    float reverbFactor = 0.2f;

    std::vector<float> maskV, maskA, maskR;    // temporally smoothed masks
    std::vector<float> tmpV, tmpA, tmpR;
    std::vector<float> prevVoicePower;         // last frame's voice estimate (decision-directed)
    std::vector<float> voicePrior;
    std::vector<int> bandStart, bandEnd;       // bin range of each display band
    bool firstFrame = true;

    // --- neural path ---------------------------------------------------------------
    void neuralMasks() noexcept;
    NeuralVoice neural;
    bool neuralWanted = true, useNeural = false;
    int neuralDelay = 0, delayPos = 0;
    std::vector<std::vector<float>> delayLine;  // input delayed to line up with the neural estimate
    std::vector<float> zBuf, zData;             // aligned neural voice (time) and its spectrum
    std::vector<float> neuralAmp, cleanSmooth, revFrac;
    std::vector<float> chainGain;               // resampling chain response (compensated in the mask)
    int neuralTopBin = 0;                       // last bin the network actually covers

    // --- gains -----------------------------------------------------------------
    std::atomic<float> targetVoice { 1.0f }, targetAmb { 1.0f }, targetRev { 1.0f };
    float gV = 1.0f, gA = 1.0f, gR = 1.0f;
    float levelV = 0.0f, levelA = 0.0f, levelR = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SeparationEngine)
};
