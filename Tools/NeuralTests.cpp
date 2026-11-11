// Tests of the neural voice path.
//
//   plumbing                     (build with VOICEGO_DFN=stub) resampling + alignment
//   speech <clean> <noise> <rir> [outDir]   (real model) quality on real speech

#include <juce_audio_formats/juce_audio_formats.h>
#include "SeparationEngine.h"
#include "voicego_dfn.h"
#include <chrono>
#include <cstdio>

namespace
{
    using Signal = std::vector<float>;

    int failures = 0;
    void check (bool ok, const char* what, double value)
    {
        std::printf ("[%s] %-64s %8.2f\n", ok ? " OK " : "FAIL", what, value);
        if (! ok) ++failures;
    }
    void info (const char* what, double value) { std::printf ("[INFO] %-64s %8.2f\n", what, value); }

    double dbPow (double x) { return 10.0 * std::log10 (x + 1e-20); }

    double energy (const Signal& s, size_t a = 0, size_t b = SIZE_MAX)
    {
        double e = 0.0;
        const size_t end = std::min (b, s.size());
        for (size_t i = a; i < end; ++i) e += (double) s[i] * s[i];
        return e;
    }

    /** Scale-invariant SDR (dB) of est against ref, ignoring the first `skip` samples. */
    double siSdr (const Signal& est, const Signal& ref, size_t skip = 0)
    {
        const size_t n = std::min (est.size(), ref.size());
        double dot = 0.0, rr = 0.0;
        for (size_t i = skip; i < n; ++i) { dot += (double) est[i] * ref[i]; rr += (double) ref[i] * ref[i]; }
        const double a = dot / (rr + 1e-20);
        double num = 0.0, den = 0.0;
        for (size_t i = skip; i < n; ++i)
        {
            const double t = a * ref[i];
            const double e = est[i] - t;
            num += t * t; den += e * e;
        }
        return dbPow (num / (den + 1e-20));
    }

    /** Mean power (dB) of a signal between f0 and f1, from an averaged spectrum. */
    double bandDb (const Signal& s, double sr, double f0, double f1)
    {
        constexpr int order = 11, N = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> buf (2 * N);
        double acc = 0.0;
        for (size_t pos = 0; pos + N <= s.size(); pos += N / 2)
        {
            for (int i = 0; i < N; ++i)
                buf[(size_t) i] = s[pos + (size_t) i] * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / N));
            std::fill (buf.begin() + N, buf.end(), 0.0f);
            fft.performFrequencyOnlyForwardTransform (buf.data(), true);
            for (int k = (int) (f0 * N / sr); k < (int) (f1 * N / sr) && k < N / 2; ++k)
                acc += (double) buf[(size_t) k] * buf[(size_t) k];
        }
        return dbPow (acc);
    }

    Signal loadMono (const juce::File& f, double& sr)
    {
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
        if (r == nullptr) { std::printf ("cannot read %s\n", f.getFullPathName().toRawUTF8()); std::exit (2); }
        sr = r->sampleRate;
        juce::AudioBuffer<float> b ((int) r->numChannels, (int) r->lengthInSamples);
        r->read (&b, 0, b.getNumSamples(), 0, true, true);
        Signal m ((size_t) b.getNumSamples(), 0.0f);
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                m[(size_t) i] += b.getSample (ch, i) / (float) b.getNumChannels();
        return m;
    }

    void saveWav (const juce::File& f, const Signal& s, double sr)
    {
        f.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os (new juce::FileOutputStream (f));
        auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (1).withBitsPerSample (24));
        juce::AudioBuffer<float> b (1, (int) s.size());
        b.copyFrom (0, 0, s.data(), (int) s.size());
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }

    Signal fftConvolve (const Signal& x, const Signal& h)
    {
        int order = 1;
        while ((1 << order) < (int) (x.size() + h.size())) ++order;
        const int N = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<juce::dsp::Complex<float>> a ((size_t) N), b ((size_t) N), A ((size_t) N), B ((size_t) N);
        for (size_t i = 0; i < x.size(); ++i) a[i] = x[i];
        for (size_t i = 0; i < h.size(); ++i) b[i] = h[i];
        fft.perform (a.data(), A.data(), false);
        fft.perform (b.data(), B.data(), false);
        for (int i = 0; i < N; ++i) A[(size_t) i] *= B[(size_t) i];
        fft.perform (A.data(), a.data(), true);
        Signal y (x.size());
        for (size_t i = 0; i < y.size(); ++i) y[i] = a[i].real();
        return y;
    }

    Signal resample (const Signal& x, int from, int to)
    {
        StreamResampler r; r.prepare (from, to);
        Signal y;
        y.reserve ((size_t) ((double) x.size() * to / from) + 64);
        for (float v : x) r.push (v, [&] (float o) { y.push_back (o); });
        for (int i = 0; i < 64; ++i) r.push (0.0f, [&] (float o) { y.push_back (o); });
        y.resize ((size_t) ((double) x.size() * to / from));
        return y;
    }

    /** Runs the engine on a mono signal (as dual mono) and returns the latency-compensated output. */
    Signal runEngine (const Signal& x, double sr, float gv, float ga, float gr, bool neural,
                      double* rtf = nullptr, int64_t* underruns = nullptr, bool* neuralActive = nullptr)
    {
        SeparationEngine e;
        e.setNeuralEnabled (neural);
        e.prepare (sr, 2);
        e.setGains (gv, ga, gr);
        e.reset();
        if (neuralActive != nullptr) *neuralActive = e.isNeuralActive();
        const int L = e.getLatencySamples();

        const int n = (int) x.size();
        juce::AudioBuffer<float> b (2, n + L);
        b.clear();
        for (int ch = 0; ch < 2; ++ch) b.copyFrom (ch, 0, x.data(), n);

        const auto t0 = std::chrono::steady_clock::now();
        for (int pos = 0; pos < b.getNumSamples(); pos += 512)
        {
            const int len = juce::jmin (512, b.getNumSamples() - pos);
            juce::AudioBuffer<float> view (b.getArrayOfWritePointers(), 2, pos, len);
            e.process (view);
        }
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        if (rtf != nullptr) *rtf = secs / ((double) b.getNumSamples() / sr);
        if (underruns != nullptr) *underruns = e.getNeural().getUnderruns();

        Signal y ((size_t) n);
        for (int i = 0; i < n; ++i) y[(size_t) i] = 0.5f * (b.getSample (0, i + L) + b.getSample (1, i + L));
        return y;
    }

    Signal mix (const Signal& a, const Signal& b, float gb)
    {
        Signal y (a.size());
        for (size_t i = 0; i < a.size(); ++i) y[i] = a[i] + gb * (i < b.size() ? b[i] : 0.0f);
        return y;
    }

    float gainForSnr (const Signal& s, const Signal& n, double snrDb)
    {
        return (float) std::sqrt (energy (s) / (energy (n, 0, s.size()) + 1e-20) / std::pow (10.0, snrDb / 10.0));
    }
}

//==============================================================================
int plumbingTests()
{
    // With the stub, the "network" is an ideal enhancer (identity + its delay):
    // voice + reverb with ambience muted must then give back the input.
    std::printf ("\n--- neural plumbing (stub model) ---\n");
    for (int sr : { 44100, 48000, 96000 })
    {
        juce::Random rng (3);
        Signal x ((size_t) sr * 6);
        for (size_t i = 0; i < x.size(); ++i)
        {
            const double t = (double) i / sr;
            x[i] = (float) (0.2 * std::sin (2 * juce::MathConstants<double>::pi * 220.0 * t) * (0.6 + 0.4 * std::sin (t * 7.0))
                            + 0.05 * std::sin (2 * juce::MathConstants<double>::pi * 3150.0 * t)
                            + 0.02 * (rng.nextFloat() * 2.0f - 1.0f));
        }
        bool active = false;
        int64_t under = 0;
        const auto unity = runEngine (x, sr, 1, 1, 1, true, nullptr, &under, &active);
        check (active, (juce::String (sr) + " Hz: neural path active").toRawUTF8(), active ? 1 : 0);
        check (siSdr (unity, x, (size_t) sr) > 90.0, (juce::String (sr) + " Hz: null test at unity (SI-SDR dB, > 90)").toRawUTF8(), siSdr (unity, x, (size_t) sr));

        const auto voice = runEngine (x, sr, 1, 0, 1, true, nullptr, &under);
        const double s = siSdr (voice, x, (size_t) sr);
        check (s > 35.0, (juce::String (sr) + " Hz: ambience muted, ideal model -> input back (SI-SDR > 35)").toRawUTF8(), s);
        check (under == 0, (juce::String (sr) + " Hz: no alignment underruns").toRawUTF8(), (double) under);
    }
    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "PLUMBING PASSED" : "PLUMBING FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}

//==============================================================================
int speechTests (const juce::File& cleanF, const juce::File& noiseF, const juce::File& rirF, const juce::File& outDir)
{
    double sr = 48000, srN = 48000, srR = 48000;
    const Signal clean = loadMono (cleanF, sr);
    Signal noise = loadMono (noiseF, srN);
    Signal rir = loadMono (rirF, srR);
    if (sr != 48000 || srN != 48000 || srR != 48000) { std::printf ("assets must be 48 kHz\n"); return 2; }
    while (noise.size() < clean.size()) noise.insert (noise.end(), noise.begin(), noise.end());
    noise.resize (clean.size());
    const size_t skip = (size_t) (0.5 * sr);   // ignore the warm-up of the estimators

    std::printf ("\n--- DeepFilterNet3 model ---\n");
    {
        VgDfn* d = vg_dfn_create (100.0f);
        check (d != nullptr, "model loads", d != nullptr);
        if (d == nullptr) return 1;
        const int hop = (int) vg_dfn_hop_size (d), delay = (int) vg_dfn_delay (d);
        info ("hop size (samples @48k)", hop);
        info ("declared delay (samples @48k)", delay);

        Signal out (clean.size(), 0.0f);
        for (size_t pos = 0; pos + (size_t) hop <= clean.size(); pos += (size_t) hop)
            vg_dfn_process (d, clean.data() + pos, out.data() + pos);
        vg_dfn_destroy (d);

        int best = 0; double bestC = -1e30;
        for (int lag = 0; lag < 4000; ++lag)
        {
            double c = 0.0;
            for (size_t i = skip; i + (size_t) lag < clean.size(); i += 3) c += (double) clean[i] * out[i + (size_t) lag];
            if (c > bestC) { bestC = c; best = lag; }
        }
        check (best == delay, "measured delay == declared delay (samples)", best);
    }

    std::printf ("\n--- clean speech only: is the voice preserved? ---\n");
    double rtf = 0.0;
    {
        const auto vr = runEngine (clean, sr, 1, 0, 1, true, &rtf);
        const auto v  = runEngine (clean, sr, 1, 0, 0, true);
        const auto vd = runEngine (clean, sr, 1, 0, 0, false);
        check (siSdr (vr, clean, skip) > 15.0, "ambience muted: SI-SDR vs clean (dB, > 15)", siSdr (vr, clean, skip));
        check (siSdr (v, clean, skip) > 10.0, "ambience + reverb muted: SI-SDR vs clean (dB, > 10)", siSdr (v, clean, skip));
        info ("  same, old DSP engine", siSdr (vd, clean, skip));
        const double hf = bandDb (v, sr, 4000, 12000) - bandDb (clean, sr, 4000, 12000);
        check (std::abs (hf) < 3.0, "voice only: 4-12 kHz 'air' change (dB, |x| < 3)", hf);
        info ("  same, old DSP engine", bandDb (vd, sr, 4000, 12000) - bandDb (clean, sr, 4000, 12000));
        check (rtf < 0.5, "real-time factor at 48 kHz stereo (< 0.5)", rtf);
    }

    std::printf ("\n--- speech + noise (5 dB SNR) ---\n");
    const auto noisy = mix (clean, noise, gainForSnr (clean, noise, 5.0));
    {
        const double base = siSdr (noisy, clean, skip);
        const auto v  = runEngine (noisy, sr, 1, 0, 0, true);
        const auto vr = runEngine (noisy, sr, 1, 0, 1, true);
        const auto vd = runEngine (noisy, sr, 1, 0, 0, false);
        info ("input SI-SDR (dB)", base);
        check (siSdr (vr, clean, skip) - base > 8.0, "ambience muted: SI-SDR gain (dB, > 8)", siSdr (vr, clean, skip) - base);
        check (siSdr (v, clean, skip) - base > 8.0, "ambience + reverb muted: SI-SDR gain (dB, > 8)", siSdr (v, clean, skip) - base);
        info ("  same, old DSP engine", siSdr (vd, clean, skip) - base);

        const auto noisy44 = resample (noisy, 48000, 44100), clean44 = resample (clean, 48000, 44100);
        const auto v44 = runEngine (noisy44, 44100, 1, 0, 0, true);
        check (siSdr (v44, clean44, skip) - siSdr (noisy44, clean44, skip) > 7.0,
               "44.1 kHz host: SI-SDR gain (dB, > 7)", siSdr (v44, clean44, skip) - siSdr (noisy44, clean44, skip));

        if (outDir.isDirectory())
        {
            saveWav (outDir.getChildFile ("1-entrada-voz-com-ruido.wav"), noisy, sr);
            saveWav (outDir.getChildFile ("2-so-voz-novo-neural.wav"), v, sr);
            saveWav (outDir.getChildFile ("3-so-voz-antigo-dsp.wav"), vd, sr);
            saveWav (outDir.getChildFile ("4-so-ambiente.wav"), runEngine (noisy, sr, 0, 1, 0, true), sr);
        }
    }

    std::printf ("\n--- noise only: leakage into the Voice knob ---\n");
    {
        const auto n = mix (Signal (clean.size(), 0.0f), noise, gainForSnr (clean, noise, 5.0));
        const auto v = runEngine (n, sr, 1, 0, 0, true);
        const auto vd = runEngine (n, sr, 1, 0, 0, false);
        const double leak = dbPow (energy (v, skip) / energy (n, skip));
        check (leak < -20.0, "ambience + reverb muted: residual (dB, < -20)", leak);
        info ("  same, old DSP engine", dbPow (energy (vd, skip) / energy (n, skip)));
    }

    std::printf ("\n--- reverberant speech (RT60 0.79 s) + light noise ---\n");
    {
        size_t peak = 0;
        for (size_t i = 0; i < rir.size(); ++i) if (std::abs (rir[i]) > std::abs (rir[peak])) peak = i;
        const float norm = 1.0f / std::abs (rir[peak]);
        for (auto& v : rir) v *= norm;
        Signal dry (clean.size(), 0.0f);           // reference: the direct sound
        for (size_t i = peak; i < clean.size(); ++i) dry[i] = clean[i - peak];
        const auto wet = fftConvolve (clean, rir);
        const auto in = mix (wet, noise, gainForSnr (wet, noise, 25.0));

        const double base = siSdr (in, dry, skip);
        const auto keep = runEngine (in, sr, 1, 0, 1, true);
        const auto cut  = runEngine (in, sr, 1, 0, 0, true);
        info ("input SI-SDR vs direct sound (dB)", base);
        info ("ambience muted, reverb kept", siSdr (keep, dry, skip));
        check (siSdr (cut, dry, skip) >= siSdr (keep, dry, skip) - 0.5, "ambience + reverb muted (not worse than reverb kept)", siSdr (cut, dry, skip));
        const double revDb = dbPow (energy (runEngine (in, sr, 0, 0, 1, true), skip) / energy (in, skip));
        info ("reverb component level vs input (dB)", revDb);

        if (outDir.isDirectory())
        {
            saveWav (outDir.getChildFile ("5-entrada-voz-com-reverb.wav"), in, sr);
            saveWav (outDir.getChildFile ("6-so-voz-sem-reverb.wav"), cut, sr);
            saveWav (outDir.getChildFile ("7-so-reverb.wav"), runEngine (in, sr, 0, 0, 1, true), sr);
        }
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "SPEECH TESTS PASSED" : "SPEECH TESTS FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
