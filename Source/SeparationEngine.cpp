#include "SeparationEngine.h"
#include <cmath>
#include <limits>

namespace
{
    constexpr float tiny = 1.0e-20f;

    // Analysis tuning
    constexpr float powerSmoothing      = 0.70f;  // recursive smoothing of |X|^2 for noise tracking
    constexpr float minStatsWindowSec   = 1.5f;   // minimum statistics search window
    constexpr float noiseBias           = 1.8f;   // compensates the downward bias of a minimum
    constexpr float noiseSmoothing      = 0.90f;
    constexpr float assumedRt60Sec      = 0.6f;  // room decay model used for late reverb
    constexpr float lateReverbStartSec  = 0.05f;  // energy older than this is "late reverb"
    constexpr float maskAttack          = 0.20f;  // mask smoothing (lower = faster)
    constexpr float maskRelease         = 0.50f;
    constexpr float ddAlpha             = 0.94f;  // decision-directed SNR smoothing
    constexpr float xiFloor             = 1.0e-3f;
    constexpr float overSubtraction     = 2.5f;
    constexpr float gainSmoothing       = 0.70f;
    constexpr float levelSmoothing      = 0.80f;
}

void SeparationEngine::prepare (double sampleRate, int numChannels)
{
    sr    = sampleRate > 0.0 ? sampleRate : 48000.0;
    numCh = juce::jmax (1, numChannels);

    fftOrder = sr <= 50000.0 ? 11 : (sr <= 100000.0 ? 12 : 13);
    fftSize  = 1 << fftOrder;
    hop      = fftSize / 4;
    numBins  = fftSize / 2 + 1;

    fft = std::make_unique<juce::dsp::FFT> (fftOrder);

    // Periodic Hann used for analysis and synthesis (75% overlap -> constant overlap-add)
    window.resize ((size_t) fftSize);
    windowEnergy = 0.0f;
    for (int n = 0; n < fftSize; ++n)
    {
        window[(size_t) n] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) n / (float) fftSize);
        windowEnergy += window[(size_t) n] * window[(size_t) n];
    }

    float ola = 0.0f;
    for (int k = 0; k < fftSize / hop; ++k)
        ola += window[(size_t) (k * hop)] * window[(size_t) (k * hop)];
    // sum of w^2 at any position is constant (1.5 for Hann/4); guard just in case
    olaScale = ola > 0.0f ? 1.0f / ola : 1.0f;

    auto alloc = [] (std::vector<std::vector<float>>& v, int a, int b)
    {
        v.assign ((size_t) a, std::vector<float> ((size_t) b, 0.0f));
    };

    alloc (inBuf,    numCh, fftSize);
    alloc (outAccum, numCh, fftSize);
    alloc (outReady, numCh, hop);
    alloc (fftData,  numCh, fftSize * 2);

    const auto nb = (size_t) numBins;
    power.assign (nb, 0.0f);
    smoothPower.assign (nb, 0.0f);
    noise.assign (nb, 0.0f);
    noiseSmooth.assign (nb, 0.0f);
    curMin.assign (nb, std::numeric_limits<float>::max());
    alloc (subMins, numSubWindows, numBins);

    const float framesPerSec = (float) (sr / hop);
    subLen = juce::jmax (1, (int) std::round (minStatsWindowSec * framesPerSec / (float) numSubWindows));

    reverbDelayFrames = juce::jmax (1, (int) std::round (lateReverbStartSec * framesPerSec));
    const float delta = 3.0f * std::log (10.0f) / assumedRt60Sec;
    reverbFactor = std::exp (-2.0f * delta * (float) reverbDelayFrames * (float) hop / (float) sr);
    alloc (history, reverbDelayFrames + 1, numBins);

    maskV.assign (nb, 1.0f);
    maskA.assign (nb, 0.0f);
    maskR.assign (nb, 0.0f);
    tmpV.assign (nb, 0.0f);
    tmpA.assign (nb, 0.0f);
    tmpR.assign (nb, 0.0f);
    prevVoicePower.assign (nb, 0.0f);

    // Speech prior: full weight 100 Hz - 9 kHz, tapering outside.
    // Energy below ~60 Hz is mostly rumble/handling noise, so it leans to "ambience".
    voicePrior.assign (nb, 1.0f);
    for (int k = 0; k < numBins; ++k)
    {
        const float f = (float) k * (float) sr / (float) fftSize;
        float w = 1.0f;
        if (f < 100.0f)       w = juce::jmap (juce::jlimit (30.0f, 100.0f, f), 30.0f, 100.0f, 0.15f, 1.0f);
        else if (f > 9000.0f) w = juce::jmap (juce::jlimit (9000.0f, 18000.0f, f), 9000.0f, 18000.0f, 1.0f, 0.6f);
        voicePrior[(size_t) k] = w;
    }

    // display bands (log spaced); a band narrower than a bin uses the nearest bin
    bandStart.assign (numBands, 0);
    bandEnd.assign (numBands, 0);
    const float fMax = juce::jmin (bandMaxHz, (float) sr * 0.5f);
    const float binHz = (float) sr / (float) fftSize;
    for (int b = 0; b < numBands; ++b)
    {
        const float f0 = bandMinHz * std::pow (fMax / bandMinHz, (float) b / numBands);
        const float f1 = bandMinHz * std::pow (fMax / bandMinHz, (float) (b + 1) / numBands);
        int k0 = (int) std::ceil (f0 / binHz), k1 = (int) std::ceil (f1 / binHz);
        k0 = juce::jlimit (1, numBins - 1, k0);
        k1 = juce::jlimit (1, numBins, k1);
        if (k1 <= k0)
        {
            k0 = juce::jlimit (1, numBins - 1, (int) std::round (std::sqrt (f0 * f1) / binHz));
            k1 = k0 + 1;
        }
        bandStart[(size_t) b] = k0;
        bandEnd[(size_t) b] = k1;
    }

    reset();
}

void SeparationEngine::reset()
{
    for (auto* group : { &inBuf, &outAccum, &outReady, &fftData, &subMins, &history })
        for (auto& v : *group)
            std::fill (v.begin(), v.end(), 0.0f);

    std::fill (curMin.begin(), curMin.end(), std::numeric_limits<float>::max());
    for (auto& v : subMins)
        std::fill (v.begin(), v.end(), std::numeric_limits<float>::max());

    std::fill (maskV.begin(), maskV.end(), 1.0f);
    std::fill (maskA.begin(), maskA.end(), 0.0f);
    std::fill (maskR.begin(), maskR.end(), 0.0f);
    std::fill (prevVoicePower.begin(), prevVoicePower.end(), 0.0f);

    hopCount = 0;
    subIndex = subCount = histPos = 0;
    firstFrame = true;

    gV = targetVoice.load();
    gA = targetAmb.load();
    gR = targetRev.load();
    levelV = levelA = levelR = 0.0f;
}

void SeparationEngine::setGains (float voice, float ambience, float reverb) noexcept
{
    targetVoice.store (voice);
    targetAmb.store (ambience);
    targetRev.store (reverb);
}

void SeparationEngine::process (juce::AudioBuffer<float>& buffer) noexcept
{
    const int chans = juce::jmin (buffer.getNumChannels(), numCh);
    const int n     = buffer.getNumSamples();
    const int writeBase = fftSize - hop;

    for (int i = 0; i < n; ++i)
    {
        for (int ch = 0; ch < chans; ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            inBuf[(size_t) ch][(size_t) (writeBase + hopCount)] = d[i];
            d[i] = outReady[(size_t) ch][(size_t) hopCount];
        }

        // If the host gives fewer channels than prepared, keep the others silent
        for (int ch = chans; ch < numCh; ++ch)
            inBuf[(size_t) ch][(size_t) (writeBase + hopCount)] = 0.0f;

        if (++hopCount == hop)
        {
            hopCount = 0;
            processFrame();
        }
    }
}

void SeparationEngine::processFrame() noexcept
{
    const auto N  = (size_t) fftSize;
    const auto H  = (size_t) hop;
    const auto NB = (size_t) numBins;

    // ---- 1. Forward transforms ----------------------------------------------
    for (int ch = 0; ch < numCh; ++ch)
    {
        auto& in = inBuf[(size_t) ch];
        auto& fd = fftData[(size_t) ch];
        for (size_t i = 0; i < N; ++i)
            fd[i] = in[i] * window[i];
        std::fill (fd.begin() + (long) N, fd.end(), 0.0f);
        fft->performRealOnlyForwardTransform (fd.data(), true);
    }

    // ---- 2. Power spectrum (shared by all channels -> stereo image preserved)
    const float chNorm = 1.0f / (float) numCh;
    for (size_t k = 0; k < NB; ++k)
    {
        float p = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
        {
            const float re = fftData[(size_t) ch][2 * k];
            const float im = fftData[(size_t) ch][2 * k + 1];
            p += re * re + im * im;
        }
        power[k] = p * chNorm;
    }

    if (firstFrame)
    {
        std::copy (power.begin(), power.end(), smoothPower.begin());
        std::copy (power.begin(), power.end(), noiseSmooth.begin());
        firstFrame = false;
    }

    // ---- 3. Stationary noise (ambience) with minimum statistics --------------
    for (size_t k = 0; k < NB; ++k)
    {
        smoothPower[k] = powerSmoothing * smoothPower[k] + (1.0f - powerSmoothing) * power[k];
        curMin[k] = juce::jmin (curMin[k], smoothPower[k]);

        float m = curMin[k];
        for (int u = 0; u < numSubWindows; ++u)
            m = juce::jmin (m, subMins[(size_t) u][k]);

        noise[k] = noiseBias * m;
        noiseSmooth[k] = noiseSmoothing * noiseSmooth[k] + (1.0f - noiseSmoothing) * noise[k];
    }

    if (++subCount >= subLen)
    {
        subCount = 0;
        std::copy (curMin.begin(), curMin.end(), subMins[(size_t) subIndex].begin());
        std::fill (curMin.begin(), curMin.end(), std::numeric_limits<float>::max());
        subIndex = (subIndex + 1) % numSubWindows;
    }

    // ---- 4. Late reverb estimate + masks -------------------------------------
    auto& histWrite = history[(size_t) histPos];
    const auto& histRead = history[(size_t) ((histPos + 1) % (reverbDelayFrames + 1))]; // Lf frames ago
    histPos = (histPos + 1) % (reverbDelayFrames + 1);

    for (size_t k = 0; k < NB; ++k)
    {
        const float nz  = noiseSmooth[k];
        const float sig = juce::jmax (0.0f, smoothPower[k] - nz);   // direct + reverb
        const float rev = juce::jmin (reverbFactor * histRead[k], sig);

        // Decision-directed a-priori SNR of the direct voice against (noise + late reverb)
        const float interf = nz + rev + tiny;
        const float judged = overSubtraction * nz + rev + tiny;   // a bit pessimistic -> less residual noise
        const float post   = power[k] / judged;
        float xi = ddAlpha * (prevVoicePower[k] / judged) + (1.0f - ddAlpha) * juce::jmax (0.0f, post - 1.0f);
        xi = juce::jmax (xi * voicePrior[k], xiFloor);

        const float mv   = xi / (1.0f + xi);
        const float rest = 1.0f - mv;
        tmpV[k] = mv;
        tmpA[k] = rest * nz / interf;
        tmpR[k] = rest * (rev + tiny) / interf;
    }

    // temporal smoothing (fast attack for the voice so consonants survive)
    for (size_t k = 0; k < NB; ++k)
    {
        auto smooth = [] (float prev, float now)
        {
            const float c = now > prev ? maskAttack : maskRelease;
            return c * prev + (1.0f - c) * now;
        };
        maskV[k] = smooth (maskV[k], tmpV[k]);
        maskA[k] = smooth (maskA[k], tmpA[k]);
        maskR[k] = smooth (maskR[k], tmpR[k]);
    }

    // frequency smoothing (reduces "musical noise") + renormalise so V+A+R = 1
    for (size_t k = 0; k < NB; ++k)
    {
        const size_t a = k > 0 ? k - 1 : k;
        const size_t b = k + 1 < NB ? k + 1 : k;
        const float v = 0.25f * maskV[a] + 0.5f * maskV[k] + 0.25f * maskV[b];
        const float m = 0.25f * maskA[a] + 0.5f * maskA[k] + 0.25f * maskA[b];
        const float r = 0.25f * maskR[a] + 0.5f * maskR[k] + 0.25f * maskR[b];
        const float s = v + m + r + tiny;
        tmpV[k] = v / s;
        tmpA[k] = m / s;
        tmpR[k] = r / s;
    }

    // ---- 5. Gains -----------------------------------------------------------
    gV = gainSmoothing * gV + (1.0f - gainSmoothing) * targetVoice.load (std::memory_order_relaxed);
    gA = gainSmoothing * gA + (1.0f - gainSmoothing) * targetAmb.load (std::memory_order_relaxed);
    gR = gainSmoothing * gR + (1.0f - gainSmoothing) * targetRev.load (std::memory_order_relaxed);

    float eV = 0.0f, eA = 0.0f, eR = 0.0f;
    for (size_t k = 0; k < NB; ++k)
    {
        const float p = power[k];
        prevVoicePower[k] = tmpV[k] * tmpV[k] * p;
        // non-noise energy of this frame (confidence-weighted) feeds the late-reverb model
        histWrite[k] = (tmpV[k] + tmpR[k]) * (tmpV[k] + tmpR[k]) * smoothPower[k];
        eV += tmpV[k] * tmpV[k] * p;
        eA += tmpA[k] * tmpA[k] * p;
        eR += tmpR[k] * tmpR[k] * p;

        const float g = juce::jmin (8.0f, gV * tmpV[k] + gA * tmpA[k] + gR * tmpR[k]);
        for (int ch = 0; ch < numCh; ++ch)
        {
            fftData[(size_t) ch][2 * k]     *= g;
            fftData[(size_t) ch][2 * k + 1] *= g;
        }
    }

    for (int b = 0; b < numBands; ++b)
    {
        float sv = 0.0f, sa = 0.0f, sr2 = 0.0f;
        const int k0 = bandStart[(size_t) b], k1 = bandEnd[(size_t) b];
        for (int k = k0; k < k1; ++k)
        {
            const float p = power[(size_t) k];
            sv  += tmpV[(size_t) k] * tmpV[(size_t) k] * p;
            sa  += tmpA[(size_t) k] * tmpA[(size_t) k] * p;
            sr2 += tmpR[(size_t) k] * tmpR[(size_t) k] * p;
        }
        const float inv = 2.0f / ((float) fftSize * windowEnergy * (float) juce::jmax (1, k1 - k0));
        spectrum.voice[(size_t) b].store (sv * inv, std::memory_order_relaxed);
        spectrum.ambience[(size_t) b].store (sa * inv, std::memory_order_relaxed);
        spectrum.reverb[(size_t) b].store (sr2 * inv, std::memory_order_relaxed);
    }

    // mean-square estimate of each component (Parseval with window compensation)
    const float msScale = 2.0f / ((float) fftSize * windowEnergy);
    levelV = levelSmoothing * levelV + (1.0f - levelSmoothing) * eV * msScale;
    levelA = levelSmoothing * levelA + (1.0f - levelSmoothing) * eA * msScale;
    levelR = levelSmoothing * levelR + (1.0f - levelSmoothing) * eR * msScale;
    levels.voice.store (levelV, std::memory_order_relaxed);
    levels.ambience.store (levelA, std::memory_order_relaxed);
    levels.reverb.store (levelR, std::memory_order_relaxed);

    // ---- 6. Inverse transforms + overlap-add -------------------------------
    for (int ch = 0; ch < numCh; ++ch)
    {
        auto& fd = fftData[(size_t) ch];

        // restore conjugate symmetry for the negative frequencies
        for (size_t k = 1; k < N / 2; ++k)
        {
            fd[2 * (N - k)]     =  fd[2 * k];
            fd[2 * (N - k) + 1] = -fd[2 * k + 1];
        }

        fft->performRealOnlyInverseTransform (fd.data());

        auto& acc = outAccum[(size_t) ch];
        for (size_t i = 0; i < N; ++i)
            acc[i] += fd[i] * window[i] * olaScale;

        auto& ready = outReady[(size_t) ch];
        std::copy (acc.begin(), acc.begin() + (long) H, ready.begin());
        std::copy (acc.begin() + (long) H, acc.end(), acc.begin());
        std::fill (acc.end() - (long) H, acc.end(), 0.0f);

        auto& in = inBuf[(size_t) ch];
        std::copy (in.begin() + (long) H, in.end(), in.begin());
    }
}
