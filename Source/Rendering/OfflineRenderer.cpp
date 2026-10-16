#include "OfflineRenderer.h"

namespace dali
{
OfflineRenderer::OfflineRenderer() = default;

void OfflineRenderer::ensure(double sr, QualityMode q, int block)
{
    if (eng_ == nullptr || sr != sr_ || q != q_ || block > block_)
    {
        eng_ = std::make_unique<CandyEngine>(); block_ = std::max(block, 512); sr_ = sr; q_ = q;
        eng_->prepare(sr, block_, q);
    }
    else eng_->reset();
    eng_->setMasterOverride(false, 1.0f, 0.0f);
    eng_->setAdaptive(0, false, 1000, 0, 1, 1, 0);
}

float OfflineRenderer::automaticTailMs(const CandyDNA& d)
{
    const auto& fx = d.fx; float tail = 80.0f;
    bool anyRev = false, anyDel = false, anyMod = false;
    for (auto& l : d.layers) if (l.active) { anyRev = anyRev || l.sendReverb > 0.02f; anyDel = anyDel || l.sendDelay > 0.02f; anyMod = anyMod || l.sendMod > 0.02f; }
    if (anyRev) tail = std::max(tail, 400.0f + 4500.0f * fx.reverbSize * fx.reverbSize);
    if (anyDel) { const float stepMs = (float) divisionBeats(fx.delayDiv, 0) * 60000.0f / 140.0f; tail = std::max(tail, stepMs * (2.0f + 6.0f * fx.delayFeedback / 0.92f)); }
    if (anyMod) tail = std::max(tail, 250.0f);
    return std::min(tail, 9000.0f);
}

float OfflineRenderer::peakOf(const juce::AudioBuffer<float>& b)
{
    float pk = 0; for (int c = 0; c < b.getNumChannels(); ++c) pk = std::max(pk, b.getMagnitude(c, 0, b.getNumSamples())); return pk;
}

juce::AudioBuffer<float> OfflineRenderer::renderOneShot(const CandyDNA& dnaIn, const RenderOptions& o)
{
    CandyDNA dna = dnaIn;
    if (o.soloLayer >= 0) for (int l = 0; l < kMaxLayers; ++l) dna.layers[(size_t) l].mute = (l != o.soloLayer);
    ensure(o.sampleRate, o.quality, o.blockSize);
    eng_->setBypassSends(o.dry);
    if (o.external != nullptr) { auto s = std::make_shared<SourceBuffer>(*o.external); eng_->externalSource().publish(s); } else eng_->externalSource().clear();
    std::array<const CandyDNA*, kNumSoundSlots> arr {}; arr[0] = &dna;
    eng_->publishBank(buildSoundBank(arr, o.sampleRate));
    HostInfo h; h.bpm = o.bpm; h.ppq = 0; h.playing = true; eng_->setHost(h);

    float bodyMs = 0.0f;
    for (auto& l : dna.layers) if (l.active) bodyMs = std::max(bodyMs, l.startOffsetMs + l.durationMs + l.releaseMs);
    const float tail = o.dry ? 20.0f : (o.tailMs >= 0.0f ? o.tailMs : automaticTailMs(dna));
    const int total = std::max(64, (int) ((bodyMs + tail) * 0.001 * o.sampleRate));
    juce::AudioBuffer<float> out(2, total); out.clear();
    std::vector<float> L((size_t) block_), R((size_t) block_);
    int pos = 0; bool first = true;
    while (pos < total)
    {
        const int n = std::min(o.blockSize, total - pos);
        h.ppq = (double) pos / o.sampleRate * o.bpm / 60.0; eng_->setHost(h);
        if (first) { EventRequest e; e.sampleOffset = 0; e.velocity = o.velocity; e.pitchOffset = o.pitchOffset; e.eventSeed = o.eventSeed; e.slot = 0; eng_->queueEvent(e); first = false; }
        eng_->process(L.data(), R.data(), n);
        std::copy(L.begin(), L.begin() + n, out.getWritePointer(0) + pos); std::copy(R.begin(), R.begin() + n, out.getWritePointer(1) + pos);
        pos += n;
    }
    eng_->externalSource().clear();
    if (o.trimSilence)
    {
        int last = total; const float thr = 3.2e-5f;     // -90 dB
        while (last > 64 && std::abs(out.getSample(0, last - 1)) < thr && std::abs(out.getSample(1, last - 1)) < thr) --last;
        out.setSize(2, last, true);
    }
    return out;
}

juce::AudioBuffer<float> OfflineRenderer::renderSequence(const std::array<const CandyDNA*, kNumSoundSlots>& slots, const std::vector<Ev>& events,
                                                         double lengthBeats, const RenderOptions& o, bool loopWrap, const std::function<bool()>& cancelled)
{
    std::array<CandyDNA, kNumSoundSlots> copies; std::array<const CandyDNA*, kNumSoundSlots> arr {};
    float tailMs = 0.0f;
    for (size_t i = 0; i < kNumSoundSlots; ++i)
        if (slots[i] != nullptr)
        {
            copies[i] = *slots[i];
            if (o.soloLayer >= 0) for (int l = 0; l < kMaxLayers; ++l) copies[i].layers[(size_t) l].mute = (l != o.soloLayer);
            arr[i] = &copies[i];
            float body = 0; for (auto& l : copies[i].layers) if (l.active) body = std::max(body, l.startOffsetMs + l.durationMs + l.releaseMs);
            tailMs = std::max(tailMs, body + (o.tailMs >= 0.0f ? o.tailMs : automaticTailMs(copies[i])));
        }
    if (arr[0] == nullptr) return juce::AudioBuffer<float>(2, 0);
    if (o.dry) tailMs = std::min(tailMs, 500.0f);
    ensure(o.sampleRate, o.quality, o.blockSize);
    eng_->setBypassSends(o.dry);
    if (o.external != nullptr) eng_->externalSource().publish(std::make_shared<SourceBuffer>(*o.external)); else eng_->externalSource().clear();
    eng_->publishBank(buildSoundBank(arr, o.sampleRate));
    const double sps = o.sampleRate * 60.0 / o.bpm;                     // samples per beat
    const int bodySamples = (int) (lengthBeats * sps), tailSamples = (int) (tailMs * 0.001 * o.sampleRate);
    const int total = bodySamples + (loopWrap ? tailSamples : tailSamples);
    juce::AudioBuffer<float> out(2, std::max(1, total)); out.clear();
    std::vector<float> L((size_t) block_), R((size_t) block_);
    std::vector<Ev> evs = events; std::stable_sort(evs.begin(), evs.end(), [](const Ev& a, const Ev& b) { return a.beat < b.beat; });
    size_t next = 0; int pos = 0; HostInfo h; h.bpm = o.bpm; h.playing = true;
    while (pos < total)
    {
        if (cancelled && (pos & 8191) < o.blockSize && cancelled()) return juce::AudioBuffer<float>(2, 0);
        const int n = std::min(o.blockSize, total - pos);
        h.ppq = (double) pos / sps; eng_->setHost(h);
        while (next < evs.size())
        {
            const int sample = (int) std::llround(evs[next].beat * sps);
            if (sample >= pos + n) break;
            if (sample >= pos && sample < bodySamples)
            { EventRequest e; e.sampleOffset = sample - pos; e.velocity = evs[next].velocity; e.slot = evs[next].slot; e.pitchOffset = evs[next].pitchOffset + o.pitchOffset;
              e.lengthScale = std::max(0.05f, evs[next].lengthBeats); e.eventSeed = evs[next].seed; eng_->queueEvent(e); }
            ++next;
        }
        eng_->process(L.data(), R.data(), n);
        std::copy(L.begin(), L.begin() + n, out.getWritePointer(0) + pos); std::copy(R.begin(), R.begin() + n, out.getWritePointer(1) + pos);
        pos += n;
    }
    eng_->externalSource().clear();
    if (loopWrap)
    {   // fold the tail back onto the start so that the loop is seamless
        const int fold = std::min(tailSamples, bodySamples);
        for (int c = 0; c < 2; ++c) { float* d = out.getWritePointer(c); for (int i = 0; i < fold && bodySamples + i < total; ++i) d[i % std::max(1, bodySamples)] += d[bodySamples + i]; }
        out.setSize(2, std::max(1, bodySamples), true);
    }
    return out;
}
} // namespace dali
