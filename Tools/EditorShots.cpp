// Headless GUI smoke test and screenshot tool.  Run under xvfb:
//   xvfb-run -a ./DaliCandyEditorShots_artefacts/Release/DaliCandyEditorShots <out-dir>
// Creates the processor + editor, fills the model (generate, import a synthetic track), pages through all five
// pages exercising refresh/tick and writes one PNG per page.  Exit code 0 = no crash and every page rendered.
#include "UI/CandyEditor.h"
#include <cstdio>
#include <cstdlib>

using namespace dali;

static void pump(int ms)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
    while (juce::Time::getMillisecondCounter() < end) juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
}

static void waitIdle(CandyProcessor& p, int maxMs)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) maxMs;
    pump(100);
    while (p.busy() && juce::Time::getMillisecondCounter() < end) pump(50);
    pump(400);
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile(argc > 1 ? argv[1] : "shots");
    out.createDirectory();
    auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("dalicandy_shots_data");
    tmp.createDirectory(); 
#if JUCE_WINDOWS
    _putenv_s("DALICANDY_DATA_DIR", tmp.getFullPathName().toRawUTF8());
#else
    ::setenv("DALICANDY_DATA_DIR", tmp.getFullPathName().toRawUTF8(), 1);
#endif


    int failures = 0;
    for (int fx = 1; fx < 2; ++fx)   // the shipping, unified plug-in
    {
        CandyProcessor proc(fx == 1);
        proc.setPlayConfigDetails(fx ? 2 : 0, 2, 48000.0, 512); proc.prepareToPlay(48000.0, 512);
        const char* tag = fx ? "unified" : "instrument-only";

        // a synthetic 8-bar track (120 bpm): kick on the beat, offbeat hats, a pad
        { const double sr = 44100.0; const int n = (int) (sr * 16.0); juce::AudioBuffer<float> b(1, n); b.clear(); auto* d = b.getWritePointer(0); juce::Random r(7);
          for (int i = 0; i < n; ++i) { const double t = (double) i / sr; d[i] = 0.08f * (float) std::sin(2 * 3.14159265 * 220.0 * t) * (float) (0.6 + 0.4 * std::sin(t * 0.8)); }
          for (int beat = 0; beat < 32; ++beat) { const int s0 = (int) (beat * 0.5 * sr); for (int k = 0; k < 6000 && s0 + k < n; ++k) { const double t = k / sr; d[s0 + k] += 0.7f * (float) std::sin(2 * 3.14159265 * (50.0 + 90.0 * std::exp(-t * 30.0)) * t) * (float) std::exp(-t * 14.0); } }
          for (int beat = 0; beat < 32; ++beat) { const int s0 = (int) ((beat + 0.5) * 0.5 * sr); for (int k = 0; k < 2500 && s0 + k < n; ++k) d[s0 + k] += 0.2f * r.nextFloat() * (float) std::exp(-(k / sr) * 60.0); }
          auto f = tmp.getChildFile("track.wav"); f.deleteFile(); juce::WavAudioFormat wav; std::unique_ptr<juce::FileOutputStream> os(f.createOutputStream());
          std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(os.get(), sr, 1, 16, {}, 0)); if (w) { os.release(); w->writeFromAudioSampleBuffer(b, 0, n); w.reset(); }
          proc.apvts.getParameter(pid::anaBpm)->setValueNotifyingHost(proc.apvts.getParameter(pid::anaBpm)->convertTo0to1(120.0f));
          proc.importAudioFile(f); }
        proc.generate(); waitIdle(proc, 60000);
        if (proc.candidates().size() < 8) { std::printf("[%s] FAIL: only %d candidates\n", tag, (int) proc.candidates().size()); ++failures; }
        proc.selectCandidate(0); pump(300);

        {
            std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
            if (! ed) { std::printf("[%s] FAIL: no editor\n", tag); ++failures; continue; }
            ed->setSize(1280, 820); ed->setVisible(false);
            auto* ce = dynamic_cast<ui::CandyEditor*>(ed.get());
            for (int page = 0; page < 5; ++page)
            {
                ce->showPage(page); pump(500);
                // exercise some controls on the page (model-bound code paths)
                if (page == 2) { proc.editMain([](CandyDNA& d) { d.layers[0].gainDb = -9.0f; }); pump(400); }
                if (page == 1) { proc.regeneratePattern(true); pump(300); }
                const auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
                const auto f = out.getChildFile(juce::String(tag) + "_page" + juce::String(page + 1) + ".png");
                f.deleteFile(); juce::FileOutputStream fos(f); juce::PNGImageFormat png;
                if (img.isValid() && fos.openedOk() && png.writeImageToStream(img, fos)) std::printf("[%s] page %d -> %s (%dx%d)\n", tag, page + 1, f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
                else { std::printf("[%s] FAIL: page %d snapshot\n", tag, page + 1); ++failures; }
            }
            // resize stress
            for (auto sz : { juce::Point<int>(1040, 700), juce::Point<int>(1900, 1200), juce::Point<int>(1280, 820) }) { ed->setSize(sz.x, sz.y); pump(100); }
        }
        pump(200);
    }
    std::printf(failures == 0 ? "EDITOR SMOKE TEST PASSED\n" : "EDITOR SMOKE TEST FAILED (%d)\n", failures);
    return failures == 0 ? 0 : 1;
}
