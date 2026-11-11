// Renders the real editor while feeding it synthetic speech, and saves PNGs.
//   VoiceGOCleanShot <outDir>
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"

static void setParam (VoiceGOCleanProcessor& p, const char* id, float value)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (value));
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    const double sr = 48000.0;
    const int block = 512;

    for (auto view : { "wave", "split" })
    {
        VoiceGOCleanProcessor p;
        p.setPlayConfigDetails (2, 2, sr, block);
        p.prepareToPlay (sr, block);
        setParam (p, ParamIDs::ambience, -22.0f);
        setParam (p, ParamIDs::voice, 1.5f);
        setParam (p, ParamIDs::reverb, -9.0f);
        setParam (p, ParamIDs::output, 1.0f);
        p.apvts.state.setProperty ("view", view, nullptr);

        std::unique_ptr<VoiceGOCleanEditor> ed (dynamic_cast<VoiceGOCleanEditor*> (p.createEditor()));
        ed->setSize (780, 500);

        juce::Random rng (7);
        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;
        double t = 0.0;
        float rv = 0.0f, lp = 0.0f;
        std::vector<float> combs[4];
        int cpos[4] {};
        const int clen[4] = { 1427, 1601, 1867, 2053 };
        for (int c = 0; c < 4; ++c) combs[c].assign ((size_t) clen[c], 0.0f);

        const int totalBlocks = (int) (5.0 * sr / block);
        for (int b = 0; b < totalBlocks; ++b)
        {
            for (int i = 0; i < block; ++i, t += 1.0 / sr)
            {
                // syllables of varying length/pitch
                const double syl = std::fmod (t * 3.1, 1.0);
                const int word = (int) (t * 3.1);
                const bool on = syl < 0.62 && (word % 5) != 4;
                const double env = on ? std::pow (std::sin (juce::MathConstants<double>::pi * syl / 0.62), 0.7) * (0.6 + 0.4 * std::sin (word * 1.7)) : 0.0;
                const double f0 = 120.0 + 25.0 * std::sin (word * 2.3) + 8.0 * std::sin (t * 30.0);
                double v = 0.0;
                for (int h = 1; h <= 30; ++h)
                {
                    const double fh = f0 * h;
                    const double f1 = 500.0 + 300.0 * std::sin (word * 1.1);
                    const double fm = std::exp (-std::pow ((fh - f1) / 350.0, 2)) + 0.5 * std::exp (-std::pow ((fh - 1900.0) / 500.0, 2)) + 0.04;
                    v += fm * std::sin (2.0 * juce::MathConstants<double>::pi * fh * t) / std::sqrt ((double) h);
                }
                const float dry = (float) (0.22 * env * v);
                float wet = 0.0f;
                for (int c = 0; c < 4; ++c)
                {
                    float& cell = combs[c][(size_t) cpos[c]];
                    wet += cell;
                    cell = dry + 0.83f * cell;
                    cpos[c] = (cpos[c] + 1) % clen[c];
                }
                rv = 0.12f * wet;
                const float w = rng.nextFloat() * 2.0f - 1.0f;
                lp = 0.97f * lp + 0.03f * w;
                const float noise = 0.012f * w + 0.05f * lp;
                buf.setSample (0, i, dry + rv + noise);
                buf.setSample (1, i, dry + 0.9f * rv + noise);
            }
            p.processBlock (buf, midi);
            if (b % 3 == 0)
                ed->refreshNow();
        }
        ed->refreshNow();

        const auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
        const auto file = outDir.getChildFile (juce::String ("voicego-clean-") + view + ".png");
        file.deleteFile();
        juce::FileOutputStream os (file);
        juce::PNGImageFormat().writeImageToStream (img, os);
        std::printf ("wrote %s\n", file.getFullPathName().toRawUTF8());
        p.releaseResources();
    }
    return 0;
}
