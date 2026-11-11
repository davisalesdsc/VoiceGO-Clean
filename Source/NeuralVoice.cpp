#include "NeuralVoice.h"
#include "voicego_dfn.h"

#include <algorithm>
#include <cmath>
#include <complex>

//==============================================================================
void StreamResampler::prepare (int inR, int outR)
{
    inRate = std::max (1, inR);
    outRate = std::max (1, outR);
    passThrough = inRate == outRate;

    // anti-aliasing cutoff relative to the input Nyquist
    const double fc = 0.92 * std::min (1.0, (double) outRate / (double) inRate);
    constexpr double pi = 3.14159265358979323846;
    const int taps = 2 * halfTaps;
    table.assign ((size_t) (phases + 1) * (size_t) taps, 0.0f);

    for (int p = 0; p <= phases; ++p)
    {
        const double frac = (double) p / phases;
        double sum = 0.0;
        for (int j = 0; j < taps; ++j)
        {
            const int k = j - halfTaps + 1;            // tap offset from i0: -H+1 .. H
            const double u = (double) k - frac;        // distance to the evaluation point
            const double s = std::abs (u) < 1e-9 ? 1.0 : std::sin (pi * fc * u) / (pi * fc * u);
            // Blackman-Harris window over [-H, H]
            const double w0 = (u + halfTaps) / (2.0 * halfTaps);
            const double w = (w0 <= 0.0 || w0 >= 1.0) ? 0.0
                           : 0.35875 - 0.48829 * std::cos (2 * pi * w0) + 0.14128 * std::cos (4 * pi * w0)
                                     - 0.01168 * std::cos (6 * pi * w0);
            const double h = fc * s * w;
            table[(size_t) p * taps + (size_t) j] = (float) h;
            sum += h;
        }
        for (int j = 0; j < taps; ++j)                 // unity DC gain for every phase
            table[(size_t) p * taps + (size_t) j] = (float) (table[(size_t) p * taps + (size_t) j] / sum);
    }

    int size = 1;
    while (size < 4 * taps + 64) size <<= 1;
    buf.assign ((size_t) size, 0.0f);
    mask = size - 1;
    reset();
}

void StreamResampler::reset()
{
    std::fill (buf.begin(), buf.end(), 0.0f);
    inCount = 0;
    outCount = 0;
}

float StreamResampler::interpolate (int64_t i0, float frac) const noexcept
{
    const int taps = 2 * halfTaps;
    const float fp = frac * phases;
    const int p = std::min (phases - 1, (int) fp);
    const float a = fp - (float) p;
    const float* r0 = table.data() + (size_t) p * taps;
    const float* r1 = r0 + taps;

    float acc = 0.0f;
    for (int j = 0; j < taps; ++j)
    {
        const int64_t idx = i0 + (j - halfTaps + 1);
        const float x = idx < 0 ? 0.0f : buf[(size_t) (idx & mask)];
        acc += x * (r0[j] + a * (r1[j] - r0[j]));
    }
    return acc;
}

//==============================================================================
NeuralVoice::NeuralVoice() = default;

NeuralVoice::~NeuralVoice()
{
    if (dfn != nullptr)
        vg_dfn_destroy (dfn);
}

bool NeuralVoice::prepare (double hostRate)
{
    if (dfn == nullptr)
        dfn = vg_dfn_create (100.0f);       // no attenuation limit: the knobs decide
    if (dfn == nullptr)
        return false;

    hop = (int) vg_dfn_hop_size (dfn);
    modelRate = (int) vg_dfn_sample_rate (dfn);
    const int host = (int) std::lround (hostRate);
    const double r = (double) host / (double) modelRate;

    // enhanced(x[n]) leaves the chain at host index n + modelDelay * host / model
    modelDelayHost = (int) std::lround ((double) vg_dfn_delay (dfn) * r);

    up.prepare (host, modelRate);
    down.prepare (modelRate, host);

    // How long after x[n] arrives the aligned output is guaranteed to exist:
    // up-resampler look-ahead + one model hop + down-resampler look-ahead.
    if (up.isPassThrough())
        extraDelay = hop;
    else
        extraDelay = (int) std::ceil (StreamResampler::halfTaps + (hop + StreamResampler::halfTaps) * r) + 4;

    int size = 1;
    while (size < 4 * (getTotalDelay() + hop * 4 + 64)) size <<= 1;
    zRing.assign ((size_t) size, 0.0f);
    zMask = size - 1;

    hopIn.assign ((size_t) hop, 0.0f);
    hopOut.assign ((size_t) hop, 0.0f);
    reset();
    return true;
}

void NeuralVoice::reset()
{
    up.reset();
    down.reset();
    std::fill (zRing.begin(), zRing.end(), 0.0f);
    std::fill (hopIn.begin(), hopIn.end(), 0.0f);
    hopFill = 0;
    zCount = inCount = underruns = 0;

    if (dfn != nullptr && dirty)
    {
        // a fresh model state, so a transport restart sounds identical every time
        vg_dfn_destroy (dfn);
        dfn = vg_dfn_create (100.0f);
    }
    dirty = false;
}

float NeuralVoice::process (float x) noexcept
{
    if (dfn == nullptr)
        return x;

    dirty = true;
    up.push (x, [this] (float y)
    {
        hopIn[(size_t) hopFill++] = y;
        if (hopFill == hop)
        {
            hopFill = 0;
            vg_dfn_process (dfn, hopIn.data(), hopOut.data());
            for (int i = 0; i < hop; ++i)
                down.push (hopOut[(size_t) i], [this] (float z)
                {
                    zRing[(size_t) (zCount & zMask)] = z;
                    ++zCount;
                });
        }
    });

    // z[n] = enhanced(x[n - modelDelayHost]); we return z[t - extraDelay]
    const int64_t want = inCount - extraDelay;
    ++inCount;
    if (want < 0)
        return 0.0f;
    if (want >= zCount)
    {
        ++underruns;
        return 0.0f;
    }
    return zRing[(size_t) (want & zMask)];
}

//==============================================================================
std::vector<float> NeuralVoice::chainMagnitude (int hostRate, int modelRate, int fftOrder)
{
    const int N = 1 << fftOrder;
    std::vector<float> mag ((size_t) N / 2 + 1, 1.0f);
    if (hostRate == modelRate || hostRate <= 0 || modelRate <= 0)
        return mag;

    StreamResampler up, down;
    up.prepare (hostRate, modelRate);
    down.prepare (modelRate, hostRate);

    // impulse response of the chain (linear phase, centred on the impulse)
    const int centre = 256;
    std::vector<float> ir;
    ir.reserve ((size_t) (centre * 2 + N));
    for (int i = 0; i < centre * 2 + N; ++i)
        up.push (i == centre ? 1.0f : 0.0f, [&] (float y) { down.push (y, [&] (float z) { ir.push_back (z); }); });

    // DFT of the response around its centre (direct evaluation: runs once in prepare)
    const int half = 128;
    constexpr double pi = 3.14159265358979323846;
    for (int k = 0; k <= N / 2; ++k)
    {
        std::complex<double> acc (0.0, 0.0);
        for (int n = -half; n <= half; ++n)
        {
            const int idx = centre + n;
            if (idx >= 0 && idx < (int) ir.size())
                acc += (double) ir[(size_t) idx] * std::polar (1.0, -2.0 * pi * k * n / N);
        }
        mag[(size_t) k] = (float) std::abs (acc);
    }
    return mag;
}
