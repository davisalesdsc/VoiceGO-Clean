#pragma once

#include <cstdint>
#include <vector>

struct VgDfn;

//==============================================================================
/** Streaming windowed-sinc resampler with an exact, zero-phase time mapping:
    output sample m is the input signal evaluated at m * inRate / outRate.
    Integer rates are tracked with integer arithmetic, so there is no drift. */
class StreamResampler
{
public:
    void prepare (int inRate, int outRate);
    void reset();
    bool isPassThrough() const noexcept { return passThrough; }

    /** Feeds one input sample; calls emit(float) for every output sample that
        became computable. */
    template <typename Emit>
    void push (float x, Emit&& emit)
    {
        if (passThrough) { emit (x); return; }
        buf[(size_t) (inCount & mask)] = x;
        ++inCount;
        for (;;)
        {
            const int64_t num = outCount * (int64_t) inRate;
            const int64_t i0  = num / outRate;
            if (i0 + halfTaps >= inCount)        // needs x[i0 - H + 1 .. i0 + H]
                break;
            const float frac = (float) (num - i0 * (int64_t) outRate) / (float) outRate;
            emit (interpolate (i0, frac));
            ++outCount;
        }
    }

    static constexpr int halfTaps = 16;

private:
    float interpolate (int64_t i0, float frac) const noexcept;

    int inRate = 48000, outRate = 48000;
    bool passThrough = true;
    static constexpr int phases = 512;
    std::vector<float> table;            // (phases + 1) rows of 2*halfTaps taps
    std::vector<float> buf;              // input history ring
    int64_t mask = 0, inCount = 0, outCount = 0;
};

//==============================================================================
/** DeepFilterNet3 voice estimate, delivered sample-aligned with a delayed copy
    of the input:  process(x[t]) returns enhanced(x[t - getTotalDelay()]).   */
class NeuralVoice
{
public:
    NeuralVoice();
    ~NeuralVoice();

    /** Returns false when the neural model is not available (engine then falls back to DSP). */
    bool prepare (double hostRate);
    void reset();
    bool isActive() const noexcept { return dfn != nullptr; }

    /** Delay (host samples) between the input and the aligned enhanced output. */
    int getTotalDelay() const noexcept { return modelDelayHost + extraDelay; }

    float process (float x) noexcept;

    /** Magnitude response of the host -> model -> host resampling chain at each
        of the fftSize/2+1 bins (all 1 when the host runs at the model rate). */
    static std::vector<float> chainMagnitude (int hostRate, int modelRate, int fftOrder);
    int getModelRate() const noexcept { return modelRate; }

    /** Number of times the aligned output was not ready (should stay 0). */
    int64_t getUnderruns() const noexcept { return underruns; }

private:
    VgDfn* dfn = nullptr;
    int hop = 480, modelRate = 48000;
    int modelDelayHost = 0, extraDelay = 0;

    StreamResampler up, down;
    std::vector<float> hopIn, hopOut;
    int hopFill = 0;

    std::vector<float> zRing;
    int64_t zMask = 0, zCount = 0, inCount = 0, underruns = 0;
    bool dirty = false;
};
