// Offline checks for the VoiceGO Clean engine.
//
//   VoiceGOCleanTest selftest                 -> synthetic tests of the DSP engine, exit code 0 on success
//   VoiceGOCleanTest plumbing / speech ...      -> neural path tests (Tools/NeuralTests.cpp)
//   VoiceGOCleanTest process in.wav out.wav <voiceDb> <ambienceDb> <reverbDb>
//                                           (use "off" for a removed component)

#include <juce_audio_formats/juce_audio_formats.h>
#include "SeparationEngine.h"
#include <cstdio>

namespace
{
    float dbArg (const juce::String& s)
    {
        return s.equalsIgnoreCase ("off") ? 0.0f : juce::Decibels::decibelsToGain (s.getFloatValue());
    }

    void run (SeparationEngine& e, juce::AudioBuffer<float>& buf, int block = 512)
    {
        for (int pos = 0; pos < buf.getNumSamples(); pos += block)
        {
            const int n = juce::jmin (block, buf.getNumSamples() - pos);
            juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), buf.getNumChannels(), pos, n);
            e.process (view);
        }
    }

    double rms (const juce::AudioBuffer<float>& b, int start, int len)
    {
        double s = 0.0;
        int count = 0;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = start; i < juce::jmin (start + len, b.getNumSamples()); ++i, ++count)
                s += (double) b.getSample (ch, i) * b.getSample (ch, i);
        return std::sqrt (s / juce::jmax (1, count));
    }

    double db (double x) { return 20.0 * std::log10 (x + 1e-12); }

    // "Voice": harmonic vowel-like tone bursts with vibrato, 0.4 s on / 0.6 s off
    // "Room":  exponentially decaying noise impulse response (RT60 ~ 0.7 s)
    // "Noise": steady pink-ish hiss
    struct Scene
    {
        juce::AudioBuffer<float> dry, wet, noise, mix;
    };

    Scene makeScene (double sr, int seconds)
    {
        const int n = (int) sr * seconds;
        Scene s;
        s.dry.setSize (2, n);   s.dry.clear();
        s.noise.setSize (2, n); s.noise.clear();
        juce::Random rng (1234);

        for (int i = 0; i < n; ++i)
        {
            const double t = i / sr;
            const double cyc = std::fmod (t, 1.0);
            if (cyc < 0.4)
            {
                const double env = std::sin (juce::MathConstants<double>::pi * cyc / 0.4);
                const double f0 = 140.0 * (1.0 + 0.02 * std::sin (2 * juce::MathConstants<double>::pi * 5.0 * t));
                double v = 0.0;
                for (int h = 1; h <= 25; ++h)
                {
                    const double fh = f0 * h;
                    const double formant = std::exp (-std::pow ((fh - 700.0) / 500.0, 2)) + 0.5 * std::exp (-std::pow ((fh - 1800.0) / 600.0, 2)) + 0.05;
                    v += formant * std::sin (2 * juce::MathConstants<double>::pi * fh * t) / std::sqrt ((double) h);
                }
                s.dry.setSample (0, i, (float) (0.12 * env * v));
                s.dry.setSample (1, i, (float) (0.12 * env * v));
            }
        }

        // synthetic room: 6 parallel feedback combs (RT60 ~0.7 s) + 2 allpass diffusers
        s.wet.makeCopyOf (s.dry);
        {
            const double rt60 = 0.7;
            const double delaysMs[] = { 29.7, 37.1, 41.1, 43.7, 53.3, 61.9 };
            std::vector<std::vector<float>> lines;
            std::vector<int> idx;
            std::vector<float> fb;
            for (double d : delaysMs)
            {
                const int len = (int) (d * 0.001 * sr);
                lines.emplace_back ((size_t) len, 0.0f);
                idx.push_back (0);
                fb.push_back ((float) std::pow (10.0, -3.0 * (len / sr) / rt60));
            }
            const int apLen[] = { (int) (0.005 * sr), (int) (0.0017 * sr) };
            std::vector<float> ap0 ((size_t) apLen[0], 0.0f), ap1 ((size_t) apLen[1], 0.0f);
            int a0 = 0, a1 = 0;
            for (int i = 0; i < n; ++i)
            {
                const float x = s.dry.getSample (0, i);
                float y = 0.0f;
                for (size_t c = 0; c < lines.size(); ++c)
                {
                    float& cell = lines[c][(size_t) idx[c]];
                    const float out = cell;
                    cell = x + fb[c] * out;
                    idx[c] = (idx[c] + 1) % (int) lines[c].size();
                    y += out;
                }
                y *= 0.12f;
                auto allpass = [] (std::vector<float>& buf, int& p, float in)
                {
                    const float d = buf[(size_t) p];
                    const float o = -0.6f * in + d;
                    buf[(size_t) p] = in + 0.6f * o;
                    p = (p + 1) % (int) buf.size();
                    return o;
                };
                y = allpass (ap0, a0, y);
                y = allpass (ap1, a1, y);
                s.wet.setSample (0, i, x + y);
                s.wet.setSample (1, i, x + y);
            }
        }

        float b0 = 0, b1 = 0, b2 = 0;
        for (int i = 0; i < n; ++i)
        {
            const float w = rng.nextFloat() * 2.0f - 1.0f;
            b0 = 0.99765f * b0 + w * 0.0990460f;
            b1 = 0.96300f * b1 + w * 0.2965164f;
            b2 = 0.57000f * b2 + w * 1.0526913f;
            const float pink = (b0 + b1 + b2 + w * 0.1848f) * 0.004f;
            s.noise.setSample (0, i, pink);
            s.noise.setSample (1, i, pink);
        }

        s.mix.makeCopyOf (s.wet);
        for (int ch = 0; ch < 2; ++ch)
            s.mix.addFrom (ch, 0, s.noise, ch, 0, n);
        return s;
    }

    int selftest()
    {
        int failures = 0;
        auto check = [&] (bool ok, const char* what, double value)
        {
            std::printf ("[%s] %-58s %8.2f\n", ok ? " OK " : "FAIL", what, value);
            if (! ok) ++failures;
        };

        for (double sr : { 44100.0, 48000.0, 96000.0 })
        {
            std::printf ("\n--- sample rate %.0f ---\n", sr);
            auto scene = makeScene (sr, 8);
            const int n = scene.mix.getNumSamples();

            SeparationEngine e;
            e.setNeuralEnabled (false);   // these tests cover the DSP engine
            e.prepare (sr, 2);
            const int L = e.getLatencySamples();

            // 1. Transparency: all gains at unity -> delayed copy of the input
            {
                juce::AudioBuffer<float> b; b.makeCopyOf (scene.mix);
                e.setGains (1, 1, 1); e.reset();
                run (e, b, 333);
                double err = 0.0, ref = 0.0;
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = L; i < n; ++i)
                    {
                        const double d = b.getSample (ch, i) - scene.mix.getSample (ch, i - L);
                        err += d * d; ref += (double) scene.mix.getSample (ch, i - L) * scene.mix.getSample (ch, i - L);
                    }
                check (db (std::sqrt (err / ref)) < -90.0, "null test at unity (residual dB, must be < -90)", db (std::sqrt (err / ref)));
            }

            // 2. Ambience OFF: noise in the gaps between phrases must drop a lot
            {
                juce::AudioBuffer<float> b; b.makeCopyOf (scene.mix);
                e.setGains (1, 0, 1); e.reset();
                run (e, b);
                // gap 0.85..0.95 s of each cycle (well after reverb has died, seconds 3..7)
                double before = 0, after = 0;
                for (int s = 3; s < 7; ++s)
                {
                    const int st = (int) ((s + 0.85) * sr), len = (int) (0.1 * sr);
                    before += rms (scene.mix, st, len);
                    after  += rms (b, st + L, len);
                }
                check (db (after / before) < -12.0, "ambience OFF: noise reduction in gaps (dB)", db (after / before));

                // voice should survive: compare level in the middle of phrases
                double vin = 0, vout = 0;
                for (int s = 3; s < 7; ++s)
                {
                    const int st = (int) ((s + 0.15) * sr), len = (int) (0.1 * sr);
                    vin += rms (scene.mix, st, len);
                    vout += rms (b, st + L, len);
                }
                check (db (vout / vin) > -3.0, "ambience OFF: voice level kept (dB, must be > -3)", db (vout / vin));
            }

            // 2b. Noise only, ambience OFF: steady-state suppression of stationary noise
            {
                juce::AudioBuffer<float> b; b.makeCopyOf (scene.noise);
                e.setGains (1, 0, 1); e.reset();
                run (e, b);
                const int st = (int) (3.0 * sr), len = (int) (4.0 * sr);
                const double r = rms (b, st + L, len) / rms (scene.noise, st, len);
                check (db (r) < -15.0, "noise only, ambience OFF: suppression (dB, < -15)", db (r));
            }

            // 2c. Ambience + reverb OFF: what is left in the gaps between phrases
            {
                juce::AudioBuffer<float> b; b.makeCopyOf (scene.mix);
                e.setGains (1, 0, 0); e.reset();
                run (e, b);
                double before = 0, after = 0;
                for (int s = 3; s < 7; ++s)
                {
                    const int st = (int) ((s + 0.85) * sr), len = (int) (0.1 * sr);
                    before += rms (scene.mix, st, len);
                    after  += rms (b, st + L, len);
                }
                check (db (after / before) < -15.0, "ambience+reverb OFF: residual in gaps (dB, < -15)", db (after / before));
            }

            // 3. Reverb OFF: tail right after each phrase must drop
            {
                juce::AudioBuffer<float> b; b.makeCopyOf (scene.mix);
                e.setGains (1, 1, 0); e.reset();
                run (e, b);
                double before = 0, after = 0;
                for (int s = 3; s < 7; ++s)
                {
                    const int st = (int) ((s + 0.45) * sr), len = (int) (0.2 * sr);
                    before += rms (scene.mix, st, len);
                    after  += rms (b, st + L, len);
                }
                check (db (after / before) < -4.0, "reverb OFF: tail energy after phrases (dB)", db (after / before));
            }

            // 4. Voice OFF: phrases must drop strongly
            {
                juce::AudioBuffer<float> b; b.makeCopyOf (scene.mix);
                e.setGains (0, 1, 1); e.reset();
                run (e, b);
                double vin = 0, vout = 0;
                for (int s = 3; s < 7; ++s)
                {
                    const int st = (int) ((s + 0.1) * sr), len = (int) (0.2 * sr);
                    vin += rms (scene.mix, st, len);
                    vout += rms (b, st + L, len);
                }
                check (db (vout / vin) < -6.0, "voice OFF: level during phrases (dB)", db (vout / vin));
            }

            // 5. Mono and odd block sizes must not crash / produce NaN
            {
                SeparationEngine m; m.setNeuralEnabled (false); m.prepare (sr, 1);
                juce::AudioBuffer<float> b (1, n); b.copyFrom (0, 0, scene.mix, 0, 0, n);
                m.setGains (1.5f, 0.0f, 0.3f);
                run (m, b, 17);
                bool finite = true;
                for (int i = 0; i < n; ++i) finite &= std::isfinite (b.getSample (0, i));
                check (finite, "mono, block size 17, extreme gains: output finite", 1.0);
            }
        }

        std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED", failures, failures == 1 ? "" : "s");
        return failures == 0 ? 0 : 1;
    }

    int processFile (const juce::File& in, const juce::File& out, float gv, float ga, float gr, bool neural = true)
    {
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (in));
        if (reader == nullptr) { std::printf ("cannot read %s\n", in.getFullPathName().toRawUTF8()); return 1; }

        const int chans = (int) juce::jmin (2u, reader->numChannels);
        const int len = (int) reader->lengthInSamples;
        SeparationEngine e; e.setNeuralEnabled (neural); e.prepare (reader->sampleRate, chans);
        const int L = e.getLatencySamples();

        juce::AudioBuffer<float> b (chans, len + L);
        b.clear();
        reader->read (&b, 0, len, 0, true, chans > 1);
        e.setGains (gv, ga, gr); e.reset();
        run (e, b);

        juce::AudioBuffer<float> trimmed (chans, len);
        for (int ch = 0; ch < chans; ++ch) trimmed.copyFrom (ch, 0, b, ch, L, len);

        out.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os (new juce::FileOutputStream (out));
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os,
                                                    juce::AudioFormatWriterOptions{}.withSampleRate (reader->sampleRate).withNumChannels (chans).withBitsPerSample (24)));
        w->writeFromAudioSampleBuffer (trimmed, 0, len);
        std::printf ("wrote %s  (engine %s, room RT60 estimate %.2f s)\n", out.getFullPathName().toRawUTF8(),
                     e.isNeuralActive() ? "NEURAL" : "DSP", e.roomRt60.load());
        return 0;
    }
}

int plumbingTests();
int speechTests (const juce::File&, const juce::File&, const juce::File&, const juce::File&);

int main (int argc, char** argv)
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);   // keep the output even if something crashes
    juce::StringArray args;
    for (int i = 1; i < argc; ++i) args.add (argv[i]);

    if (args.size() >= 1 && args[0] == "selftest")
        return selftest();

    auto file = [] (const juce::String& p) { return juce::File::getCurrentWorkingDirectory().getChildFile (p); };

    if (args.size() >= 1 && args[0] == "plumbing")
        return plumbingTests();

    if (args.size() >= 4 && args[0] == "speech")
        return speechTests (file (args[1]), file (args[2]), file (args[3]), args.size() >= 5 ? file (args[4]) : juce::File());

    if ((args.size() == 6 || args.size() == 7) && args[0] == "process")
        return processFile (juce::File::getCurrentWorkingDirectory().getChildFile (args[1]),
                            juce::File::getCurrentWorkingDirectory().getChildFile (args[2]),
                            dbArg (args[3]), dbArg (args[4]), dbArg (args[5]), args[6] != "dsp");

    std::printf ("usage: VoiceGOCleanTest selftest\n"
                 "       VoiceGOCleanTest plumbing                 (build with VOICEGO_DFN=stub)\n"
                 "       VoiceGOCleanTest speech clean.wav noise.wav rir.wav [outDir]\n"
                 "       VoiceGOCleanTest process in.wav out.wav <voiceDb|off> <ambienceDb|off> <reverbDb|off> [dsp]\n");
    return 2;
}
