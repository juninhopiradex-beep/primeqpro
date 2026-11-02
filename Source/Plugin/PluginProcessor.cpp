#include "PluginProcessor.h"
#include "../GUI/PluginEditor.h"
#include "AppPaths.h"

using namespace pq;

// ------------------------------------------------------------------------------------------ trabalhador FIR
class FirWorker : public juce::Thread
{
public:
    explicit FirWorker (PrimeqProcessor& p) : juce::Thread ("PRIMEQ FIR designer"), proc (p) {}
    void run() override { while (! threadShouldExit()) { proc.firDesignTick(); wait (25); } }
private:
    PrimeqProcessor& proc;
};

// ------------------------------------------------------------------------------------------ parametros
namespace
{
    using APF = juce::AudioParameterFloat; using APB = juce::AudioParameterBool; using APC = juce::AudioParameterChoice;
    using FA = juce::AudioParameterFloatAttributes; using BA = juce::AudioParameterBoolAttributes; using CA = juce::AudioParameterChoiceAttributes;
    juce::ParameterID PID (const std::string& s) { return juce::ParameterID (juce::String (s), 1); }
    juce::NormalisableRange<float> skewed (float lo, float hi, float centre) { juce::NormalisableRange<float> r (lo, hi, 0.f); r.setSkewForCentre (centre); return r; }
    inline void atomicMax (std::atomic<float>& a, float v) { float c = a.load (std::memory_order_relaxed); while (v > c && ! a.compare_exchange_weak (c, v, std::memory_order_relaxed)) {} }
}

juce::AudioProcessorValueTreeState::ParameterLayout PrimeqProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout L;
    const juce::StringArray types { "Bell", "Low Shelf", "High Shelf", "High Pass", "Low Pass", "Notch", "Band Pass", "All Pass" };
    const juce::StringArray slopes { "6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct", "72 dB/oct", "96 dB/oct", "Brickwall" };
    const juce::StringArray places { "Stereo", "Left", "Right", "Mid", "Side" };
    for (int b = 0; b < kNumBands; ++b)
    {
        const juce::String n = "Band " + juce::String (b + 1) + " ";
        const float f0 = (float) (30.0 * std::pow (16000.0 / 30.0, b / 23.0));
        L.add (std::make_unique<APB> (PID (bid (b, "on")), n + "On", false));
        L.add (std::make_unique<APC> (PID (bid (b, "type")), n + "Type", types, 0));
        L.add (std::make_unique<APF> (PID (bid (b, "freq")), n + "Freq", skewed ((float) kMinFreq, (float) kMaxFreq, 1000.f), f0, FA().withLabel ("Hz")));
        L.add (std::make_unique<APF> (PID (bid (b, "gain")), n + "Gain", juce::NormalisableRange<float> (-30.f, 30.f, 0.01f), 0.f, FA().withLabel ("dB")));
        L.add (std::make_unique<APF> (PID (bid (b, "q")), n + "Q", skewed ((float) kMinQ, (float) kMaxQ, 1.f), 1.f));
        L.add (std::make_unique<APC> (PID (bid (b, "slope")), n + "Slope", slopes, 1));
        L.add (std::make_unique<APC> (PID (bid (b, "place")), n + "Placement", places, 0));
        L.add (std::make_unique<APB> (PID (bid (b, "dyn")), n + "Dynamic", false));
        L.add (std::make_unique<APF> (PID (bid (b, "thr")), n + "Dyn Threshold", juce::NormalisableRange<float> (-80.f, 0.f, 0.1f), -30.f, FA().withLabel ("dB")));
        L.add (std::make_unique<APF> (PID (bid (b, "range")), n + "Dyn Range", juce::NormalisableRange<float> (-30.f, 30.f, 0.1f), -6.f, FA().withLabel ("dB")));
        L.add (std::make_unique<APF> (PID (bid (b, "ratio")), n + "Dyn Ratio", skewed (1.f, 20.f, 3.f), 2.f));
        L.add (std::make_unique<APF> (PID (bid (b, "attack")), n + "Dyn Attack", skewed (0.1f, 500.f, 10.f), 10.f, FA().withLabel ("ms")));
        L.add (std::make_unique<APF> (PID (bid (b, "hold")), n + "Dyn Hold", skewed (0.f, 500.f, 50.f), 0.f, FA().withLabel ("ms")));
        L.add (std::make_unique<APF> (PID (bid (b, "release")), n + "Dyn Release", skewed (1.f, 2000.f, 100.f), 100.f, FA().withLabel ("ms")));
        L.add (std::make_unique<APF> (PID (bid (b, "knee")), n + "Dyn Knee", juce::NormalisableRange<float> (0.f, 24.f, 0.1f), 6.f, FA().withLabel ("dB")));
        L.add (std::make_unique<APC> (PID (bid (b, "det")), n + "Dyn Detector", juce::StringArray { "Peak", "RMS" }, 0));
        L.add (std::make_unique<APC> (PID (bid (b, "dir")), n + "Dyn Direction", juce::StringArray { "Above", "Below" }, 0));
    }
    L.add (std::make_unique<APC> (PID ("phase"), "Phase Mode", juce::StringArray { "Zero Latency", "Natural Phase", "Linear Phase" }, 0));
    L.add (std::make_unique<APB> (PID ("bypass"), "Bypass", false));
    L.add (std::make_unique<APF> (PID ("out"), "Output Gain", juce::NormalisableRange<float> (-24.f, 24.f, 0.01f), 0.f, FA().withLabel ("dB")));
    L.add (std::make_unique<APB> (PID ("autogain"), "Auto Gain", false));
    L.add (std::make_unique<APB> (PID ("delta"), "Delta", false));
    L.add (std::make_unique<APB> (PID ("scext"), "External Sidechain", false));
    const auto meta = [] { return CA().withAutomatable (false).withMeta (true); };
    L.add (std::make_unique<APC> (PID ("fftsize"), "FFT Size", juce::StringArray { "2048", "4096", "8192" }, 1, meta()));
    L.add (std::make_unique<APC> (PID ("specview"), "Spectrum View", juce::StringArray { "Stereo", "Left", "Right", "Mid", "Side" }, 0, meta()));
    L.add (std::make_unique<APC> (PID ("tilt"), "Spectrum Tilt", juce::StringArray { "0 dB/oct", "3 dB/oct", "4.5 dB/oct" }, 1, meta()));
    L.add (std::make_unique<APC> (PID ("graphrange"), "Graph Range", juce::StringArray { "+-6 dB", "+-12 dB", "+-18 dB", "+-30 dB" }, 1, meta()));
    L.add (std::make_unique<APB> (PID ("peakhold"), "Peak Hold", false, BA().withAutomatable (false).withMeta (true)));
    L.add (std::make_unique<APB> (PID ("freeze"), "Freeze Spectrum", false, BA().withAutomatable (false).withMeta (true)));
    return L;
}

// ------------------------------------------------------------------------------------------ construcao
PrimeqProcessor::PrimeqProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                                       .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, &undo, "PRIMEQ", createLayout())
{
    for (int b = 0; b < kNumBands; ++b)
    {
        auto g = [&] (const char* k) { return apvts.getRawParameterValue (juce::String (bid (b, k))); };
        bp[(size_t) b] = { g ("on"), g ("type"), g ("freq"), g ("gain"), g ("q"), g ("slope"), g ("place"), g ("dyn"), g ("thr"), g ("range"), g ("ratio"), g ("attack"), g ("hold"), g ("release"), g ("knee"), g ("det"), g ("dir") };
    }
    pPhase = apvts.getRawParameterValue ("phase"); pBypass = apvts.getRawParameterValue ("bypass"); pOut = apvts.getRawParameterValue ("out");
    pAuto = apvts.getRawParameterValue ("autogain"); pDelta = apvts.getRawParameterValue ("delta"); pScExt = apvts.getRawParameterValue ("scext");
    for (auto& d : dynTel) d.store (0.f);
    apvts.addParameterListener ("phase", this);
    worker = std::make_unique<FirWorker> (*this);
    worker->startThread (juce::Thread::Priority::background);
}

PrimeqProcessor::~PrimeqProcessor()
{
    apvts.removeParameterListener ("phase", this);
    worker->stopThread (2000);
}

bool PrimeqProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto m = l.getMainOutputChannelSet();
    if (m != juce::AudioChannelSet::mono() && m != juce::AudioChannelSet::stereo()) return false;
    if (l.getMainInputChannelSet() != m) return false;
    const auto sc = l.getChannelSet (true, 1);
    return sc.isDisabled() || sc == juce::AudioChannelSet::mono() || sc == juce::AudioChannelSet::stereo();
}

int PrimeqProcessor::latencyFor (int phaseIdx) const { return phaseIdx <= 0 ? 0 : firLatencyFor (sr); }

void PrimeqProcessor::parameterChanged (const juce::String& id, float v)
{
    if (id == "phase") { const int lat = latencyFor ((int) (v + 0.5f)); latencySamples.store (lat); setLatencySamples (lat); }
}

void PrimeqProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    {
        const juce::ScopedLock sl (firLock);
        sr = sampleRate; maxBlock = std::max (1, samplesPerBlock);
        core.prepare (sr);
        for (auto* v : { &dL, &dR, &sL, &sR }) v->assign ((size_t) kChunk, 0.0);
        for (int c = 0; c < 2; ++c) { fIn[c].assign ((size_t) kChunk, 0.f); fOut[c].assign ((size_t) kChunk, 0.f); dryRing[c].assign ((size_t) kDryCap, 0.f); }
        for (auto* v : { &oL, &oR, &pL, &pR, &fTmp }) v->assign ((size_t) kChunk, 0.f);
        dryW = 0;
        const juce::dsp::ProcessSpec spec { sr, (juce::uint32) kChunk, 1 };
        for (auto& c : conv) { c.prepare (spec); c.reset(); }
        firValid.store (false); firCross.store (false); lastKey.clear(); firFs.store ((int) sr);
        analyzer.prepare (sr);
        fadeGain = fadeTarget = 1.f; path = 0; primeLeft = 0; bypState = pBypass->load() > 0.5f ? 1.f : 0.f; gainState = 1.f; agState = 0.f;
    }
    const int lat = latencyFor ((int) (pPhase->load() + 0.5f));
    latencySamples.store (lat); setLatencySamples (lat);
}

// ------------------------------------------------------------------------------------------ parametros -> modelo
void PrimeqProcessor::readBands (BandParams* out) const
{
    auto I = [] (const std::atomic<float>* a) { return (int) (a->load (std::memory_order_relaxed) + 0.5f); };
    auto D = [] (const std::atomic<float>* a) { return (double) a->load (std::memory_order_relaxed); };
    for (int b = 0; b < kNumBands; ++b)
    {
        const auto& q = bp[(size_t) b]; BandParams& o = out[b];
        o.on = D (q.on) > 0.5; o.type = (BandType) std::clamp (I (q.type), 0, 7); o.freq = D (q.freq); o.gain = D (q.gain); o.q = D (q.q);
        o.slope = std::clamp (I (q.slope), 0, kNumSlopes - 1); o.place = (Placement) std::clamp (I (q.place), 0, 4);
        o.dyn = D (q.dyn) > 0.5; o.thr = D (q.thr); o.range = D (q.range); o.ratio = D (q.ratio); o.attackMs = D (q.attack); o.holdMs = D (q.hold);
        o.releaseMs = D (q.release); o.kneeDb = D (q.knee); o.det = (DetMode) std::clamp (I (q.det), 0, 1); o.dir = (DynDir) std::clamp (I (q.dir), 0, 1);
    }
}

void PrimeqProcessor::setParamReal (const juce::String& id, float v)
{
    if (auto* p = apvts.getParameter (id)) { p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (v)); p->endChangeGesture(); }
}
float PrimeqProcessor::getParamReal (const juce::String& id) const
{
    if (auto* p = apvts.getParameter (id)) return p->convertFrom0to1 (p->getValue());
    return 0.f;
}

void PrimeqProcessor::setBand (int b, const BandParams& p)
{
    auto S = [&] (const char* k, double v) { setParamReal (juce::String (bid (b, k)), (float) v); };
    S ("on", p.on ? 1 : 0); S ("type", (int) p.type); S ("freq", p.freq); S ("gain", p.gain); S ("q", p.q); S ("slope", p.slope); S ("place", (int) p.place);
    S ("dyn", p.dyn ? 1 : 0); S ("thr", p.thr); S ("range", p.range); S ("ratio", p.ratio); S ("attack", p.attackMs); S ("hold", p.holdMs);
    S ("release", p.releaseMs); S ("knee", p.kneeDb); S ("det", (int) p.det); S ("dir", (int) p.dir);
}

void PrimeqProcessor::resetAllBands()
{
    for (int b = 0; b < kNumBands; ++b)
        for (const char* k : { "on", "type", "freq", "gain", "q", "slope", "place", "dyn", "thr", "range", "ratio", "attack", "hold", "release", "knee", "det", "dir" })
            if (auto* p = apvts.getParameter (juce::String (bid (b, k)))) { p->beginChangeGesture(); p->setValueNotifyingHost (p->getDefaultValue()); p->endChangeGesture(); }
}

// ------------------------------------------------------------------------------------------ audio
void PrimeqProcessor::firProcess (double* L, double* R, int m)
{
    using namespace juce::dsp;
    for (int i = 0; i < m; ++i) { fIn[0][(size_t) i] = (float) L[i]; fIn[1][(size_t) i] = (float) R[i]; }
    // L' = ll(L) + rl(R) ; R' = lr(L) + rr(R)   (cada convolucao e mono; buffers pre-alocados)
    float* tmp = fTmp.data();
    auto convolve = [&] (int idx, const std::vector<float>& src, float* dst)
    {
        std::memcpy (dst, src.data(), (size_t) m * sizeof (float));
        float* ch[1] = { dst }; AudioBlock<float> blk (ch, 1, (size_t) m);
        conv[idx].process (ProcessContextReplacing<float> (blk));
    };
    convolve (0, fIn[0], fOut[0].data());        // ll
    convolve (3, fIn[1], fOut[1].data());        // rr
    if (firCross.load (std::memory_order_relaxed))
    {
        convolve (1, fIn[1], tmp); for (int i = 0; i < m; ++i) fOut[0][(size_t) i] += tmp[i];   // rl
        convolve (2, fIn[0], tmp); for (int i = 0; i < m; ++i) fOut[1][(size_t) i] += tmp[i];   // lr
    }
    for (int i = 0; i < m; ++i) { L[i] = fOut[0][(size_t) i]; R[i] = fOut[1][(size_t) i]; }
}

void PrimeqProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const auto t0 = juce::Time::getHighResolutionTicks();
    const int total = buffer.getNumSamples();
    const int nch = std::min (2, getMainBusNumInputChannels());
    if (nch < 1 || total < 1) return;

    BandParams bands[kNumBands]; readBands (bands); core.setParams (bands);
    core.setListen (listenBand.load (std::memory_order_relaxed));
    const int phaseIdx = std::clamp ((int) (pPhase->load() + 0.5f), 0, 2);
    const bool bypass = pBypass->load() > 0.5f, delta = pDelta->load() > 0.5f, autoG = pAuto->load() > 0.5f;
    const int lat = std::min (latencyFor (phaseIdx), kDryCap - kChunk - 1);
    const bool unlocked = AccessManager::get().isUnlocked();
    const bool scExt = pScExt->load() > 0.5f && getBusCount (true) > 1 && getBus (true, 1) != nullptr && getBus (true, 1)->isEnabled();
    const int scBase = scExt ? getChannelIndexInProcessBlockBuffer (true, 1, 0) : -1;
    const int scCh = scExt ? getBus (true, 1)->getNumberOfChannels() : 0;
    const bool wantFir = phaseIdx != 0;
    const float fadeStep = (float) (1.0 / (0.008 * sr)), bk = (float) (1.0 - std::exp (-1.0 / (0.005 * sr))), gk = (float) (1.0 - std::exp (-1.0 / (0.02 * sr)));
    const double outDb = pOut->load();
    float pkL = 0.f, pkR = 0.f;

    float* wl = buffer.getWritePointer (0); float* wr = nch > 1 ? buffer.getWritePointer (1) : nullptr;

    for (int off = 0; off < total; off += kChunk)
    {
        const int m = std::min (kChunk, total - off);
        for (int i = 0; i < m; ++i)
        {
            const float a = wl[off + i], b = wr ? wr[off + i] : a;
            pL[(size_t) i] = a; pR[(size_t) i] = b; dL[(size_t) i] = a; dR[(size_t) i] = b;
            dryRing[0][(size_t) ((dryW + i) & (kDryCap - 1))] = a; dryRing[1][(size_t) ((dryW + i) & (kDryCap - 1))] = b;
        }
        analyzer.pushPre (pL.data(), pR.data(), m);

        // sidechain externo (so alimenta os detetores)
        const double *scLp = nullptr, *scRp = nullptr;
        if (scExt && scBase >= 0)
        {
            const float* s0 = buffer.getReadPointer (scBase); const float* s1 = scCh > 1 ? buffer.getReadPointer (scBase + 1) : s0;
            for (int i = 0; i < m; ++i) { sL[(size_t) i] = s0[off + i]; sR[(size_t) i] = s1[off + i]; }
            scLp = sL.data(); scRp = sR.data();
        }

        if (unlocked)
        {
            // transicao segura entre caminhos (IIR <-> FIR): desvanece para o dry, troca, volta
            if (primeLeft > 0) { fadeTarget = 0.f; primeLeft -= m; }   // FIR a encher (latencia): sai o dry alinhado, nunca silencio
            else if ((path == 1) != wantFir)
            {
                fadeTarget = 0.f;
                if (fadeGain <= 1e-4f && (! wantFir || (firValid.load() && firFs.load() == (int) sr)))
                {
                    path = wantFir ? 1 : 0; core.reset(); for (auto& c : conv) c.reset();
                    if (path == 1) primeLeft = lat; else fadeTarget = 1.f;
                }
            }
            else fadeTarget = 1.f;

            if (path == 0) core.process (dL.data(), dR.data(), m, scLp, scRp, true);
            else { core.process (dL.data(), dR.data(), m, scLp, scRp, false); firProcess (dL.data(), dR.data(), m); }
            if (autoG) agState += ((float) std::clamp (core.autoGainDb(), -24.0, 24.0) - agState) * 0.05f; else agState += (0.f - agState) * 0.05f;
        }

        const double gTarget = std::pow (10.0, (outDb + agState) / 20.0);
        for (int i = 0; i < m; ++i)
        {
            const int rd = (dryW + i - lat) & (kDryCap - 1);
            const double dryL = dryRing[0][(size_t) rd], dryR = dryRing[1][(size_t) rd];
            double oLd = dryL, oRd = dryR;
            if (unlocked)
            {
                if (fadeGain < fadeTarget) fadeGain = std::min (fadeTarget, fadeGain + fadeStep); else if (fadeGain > fadeTarget) fadeGain = std::max (fadeTarget, fadeGain - fadeStep);
                gainState += ((float) gTarget - gainState) * gk;
                double wL = delta ? (dL[(size_t) i] - dryL) * gainState : dL[(size_t) i] * gainState;
                double wR = delta ? (dR[(size_t) i] - dryR) * gainState : dR[(size_t) i] * gainState;
                wL = fadeGain * wL + (1.0 - fadeGain) * dryL; wR = fadeGain * wR + (1.0 - fadeGain) * dryR;
                const float bt = bypass ? 1.f : 0.f; bypState += (bt - bypState) * bk; if (std::fabs (bt - bypState) < 1e-5f) bypState = bt;
                if (std::fabs ((float) gTarget - gainState) < 1e-6f) gainState = (float) gTarget;
                oLd = bypState * dryL + (1.0 - bypState) * wL; oRd = bypState * dryR + (1.0 - bypState) * wR;
                if (! std::isfinite (oLd)) oLd = 0.0; if (! std::isfinite (oRd)) oRd = 0.0;
            }
            oL[(size_t) i] = (float) oLd; oR[(size_t) i] = (float) oRd;
            wl[off + i] = (float) oLd; if (wr) wr[off + i] = (float) oRd;
            pkL = std::max (pkL, std::fabs ((float) oLd)); pkR = std::max (pkR, std::fabs ((float) oRd));
        }
        dryW = (dryW + m) & (kDryCap - 1);
        analyzer.pushPost (oL.data(), oR.data(), m);
    }
    atomicMax (meterL, pkL); atomicMax (meterR, wr ? pkR : pkL);
    for (int b = 0; b < kNumBands; ++b) dynTel[(size_t) b].store ((float) core.dynDb (b), std::memory_order_relaxed);
    const double secs = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
    const float load = (float) (secs / ((double) total / sr)); cpuLoad.store (cpuLoad.load (std::memory_order_relaxed) * 0.9f + load * 0.1f, std::memory_order_relaxed);
}

// ------------------------------------------------------------------------------------------ FIR (thread de fundo)
void PrimeqProcessor::firDesignTick()
{
    const juce::ScopedLock sl (firLock);
    const int phaseIdx = std::clamp ((int) (pPhase->load() + 0.5f), 0, 2);
    if (phaseIdx == 0 || sr <= 0.0 || firFs.load() != (int) sr) return;
    BandParams b[kNumBands]; readBands (b); double dyn[kNumBands];
    for (int i = 0; i < kNumBands; ++i) dyn[i] = std::round (dynTel[(size_t) i].load (std::memory_order_relaxed) * 10.f) / 10.0;
    const double alpha = alphaFor ((PhaseMode) phaseIdx);
    std::vector<double> key; key.reserve (kNumBands * 12 + 3); key.push_back (alpha); key.push_back (sr);
    for (int i = 0; i < kNumBands; ++i)
    {
        if (! b[i].on) { key.push_back (0); continue; }
        key.insert (key.end(), { 1.0, (double) (int) b[i].type, b[i].freq, b[i].gain, b[i].q, (double) b[i].slope, (double) (int) b[i].place, b[i].dyn ? 1.0 : 0.0, b[i].dyn ? dyn[i] : 0.0 });
    }
    if (key == lastKey && firValid.load()) return;
    designer.design (b, dyn, sr, alpha, kernels);
    auto load = [&] (int idx, const std::vector<float>& k)
    {
        juce::AudioBuffer<float> buf (1, (int) k.size()); std::memcpy (buf.getWritePointer (0), k.data(), k.size() * sizeof (float));
        conv[idx].loadImpulseResponse (std::move (buf), sr, juce::dsp::Convolution::Stereo::no, juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
    };
    load (0, kernels.ll); load (3, kernels.rr);
    if (kernels.cross) { load (1, kernels.rl); load (2, kernels.lr); }
    firCross.store (kernels.cross); lastKey = key; firValid.store (true);
}

// ------------------------------------------------------------------------------------------ estado, A/B, presets
void PrimeqProcessor::migrateState (juce::ValueTree& vt, int fromVersion)
{
    // v1 e a primeira versao de estado. Futuras migracoes: if (fromVersion < 2) { ... renomear/converter parametros ... }
    (void) vt; (void) fromVersion;
}

void PrimeqProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto st = apvts.copyState(); st.setProperty (kStateVersionKey, kStateVersion, nullptr);
    juce::XmlElement root ("PRIMEQPRO");
    root.setAttribute ("uiW", uiWidth); root.setAttribute ("uiH", uiHeight); root.setAttribute ("slot", slot);
    if (auto x = st.createXml()) root.addChildElement (x.release());
    for (int i = 0; i < 2; ++i) if (slots[i].isValid()) { auto* e = new juce::XmlElement ("AB"); e->setAttribute ("i", i); if (auto x = slots[i].createXml()) e->addChildElement (x.release()); root.addChildElement (e); }
    copyXmlToBinary (root, dest);
}

void PrimeqProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr) return;
    juce::XmlElement* params = xml->hasTagName ("PRIMEQPRO") ? xml->getChildByName (apvts.state.getType().toString()) : xml.get();
    if (params == nullptr) return;
    auto vt = juce::ValueTree::fromXml (*params);
    if (! vt.isValid()) return;
    migrateState (vt, (int) vt.getProperty (kStateVersionKey, 0));
    apvts.replaceState (vt);
    if (xml->hasTagName ("PRIMEQPRO"))
    {
        uiWidth = std::clamp (xml->getIntAttribute ("uiW", uiWidth), 760, 3000); uiHeight = std::clamp (xml->getIntAttribute ("uiH", uiHeight), 480, 2000);
        slot = xml->getIntAttribute ("slot", 0) & 1;
        for (auto* e : xml->getChildWithTagNameIterator ("AB")) if (auto* c = e->getFirstChildElement()) slots[e->getIntAttribute ("i") & 1] = juce::ValueTree::fromXml (*c);
    }
    undo.clearUndoHistory();
}

void PrimeqProcessor::toggleAB()
{
    slots[slot] = apvts.copyState();
    slot ^= 1;
    if (slots[slot].isValid()) { undo.beginNewTransaction ("A/B"); apvts.replaceState (slots[slot].createCopy()); }
}

void PrimeqProcessor::saveUserPreset (const juce::String& name)
{
    auto f = pqpaths::userPresetDir().getChildFile (juce::File::createLegalFileName (name) + ".pqpreset");
    auto st = apvts.copyState(); st.setProperty (kStateVersionKey, kStateVersion, nullptr);
    if (auto x = st.createXml()) x->writeTo (f);
}
juce::StringArray PrimeqProcessor::userPresetNames() const
{
    juce::StringArray n; for (auto& f : pqpaths::userPresetDir().findChildFiles (juce::File::findFiles, false, "*.pqpreset")) n.add (f.getFileNameWithoutExtension());
    n.sort (true); return n;
}
bool PrimeqProcessor::loadUserPreset (const juce::String& name)
{
    auto f = pqpaths::userPresetDir().getChildFile (name + ".pqpreset");
    if (auto x = juce::XmlDocument::parse (f)) { auto vt = juce::ValueTree::fromXml (*x); if (vt.isValid()) { migrateState (vt, (int) vt.getProperty (kStateVersionKey, 0)); undo.beginNewTransaction ("Preset"); apvts.replaceState (vt); return true; } }
    return false;
}

namespace
{
    BandParams band (BandType t, double f, double g, double q, int slope = 1)
    { BandParams p; p.on = true; p.type = t; p.freq = f; p.gain = g; p.q = q; p.slope = slope; return p; }
}
const std::vector<PrimeqProcessor::FactoryPreset>& PrimeqProcessor::factoryPresets()
{
    static const std::vector<FactoryPreset> v {
        { "Init (Flat)", [] (PrimeqProcessor& p) { p.resetAllBands(); } },
        { "Vocal Presence", [] (PrimeqProcessor& p) { p.resetAllBands(); p.setBand (0, band (BandType::HighPass, 80, 0, 0.707, 3)); p.setBand (1, band (BandType::Bell, 300, -2.5, 1.2)); p.setBand (2, band (BandType::Bell, 3500, 2.0, 1.0)); p.setBand (3, band (BandType::HighShelf, 10000, 2.0, 0.707)); } },
        { "Master - Gentle Smile", [] (PrimeqProcessor& p) { p.resetAllBands(); p.setBand (0, band (BandType::LowShelf, 60, 1.2, 0.707)); p.setBand (1, band (BandType::Bell, 350, -1.0, 0.8)); p.setBand (2, band (BandType::HighShelf, 12000, 1.5, 0.707)); } },
        { "Low Cut 30 Hz (48 dB/oct)", [] (PrimeqProcessor& p) { p.resetAllBands(); p.setBand (0, band (BandType::HighPass, 30, 0, 0.707, 5)); } },
        { "Dynamic De-Mud (280 Hz)", [] (PrimeqProcessor& p) { p.resetAllBands(); auto b = band (BandType::Bell, 280, 0, 1.4); b.dyn = true; b.thr = -28; b.range = -4; b.ratio = 2.5; b.attackMs = 15; b.releaseMs = 150; p.setBand (0, b); } },
        { "Dynamic De-Ess (7 kHz)", [] (PrimeqProcessor& p) { p.resetAllBands(); auto b = band (BandType::Bell, 7000, 0, 3.0); b.dyn = true; b.thr = -35; b.range = -6; b.ratio = 4; b.attackMs = 1; b.releaseMs = 60; b.det = DetMode::Peak; p.setBand (0, b); } },
    };
    return v;
}
void PrimeqProcessor::loadFactoryPreset (int idx)
{
    const auto& v = factoryPresets(); if (idx < 0 || idx >= (int) v.size()) return;
    undo.beginNewTransaction (v[(size_t) idx].name); v[(size_t) idx].apply (*this);
}

// ------------------------------------------------------------------------------------------ Smart Ops (aplicar / reverter)
int PrimeqProcessor::smartApply (const std::vector<DeresProposal>& props)
{
    undo.beginNewTransaction ("Smart Ops: Deresonate");
    smartBackup.clear(); int created = 0, b = kNumBands - 1;
    BandParams cur[kNumBands]; readBands (cur);
    for (const auto& pr : props)
    {
        while (b >= 0 && cur[b].on) --b;
        if (b < 0) break;
        smartBackup.push_back ({ b, cur[b] });
        BandParams p = cur[b]; p.on = true; p.type = BandType::Bell; p.freq = pr.freq; p.gain = pr.gainDb; p.q = pr.q; p.place = Placement::Stereo; p.dyn = false;
        setBand (b, p); cur[b].on = true; ++created; --b;
    }
    return created;
}
void PrimeqProcessor::smartRevert()
{
    undo.beginNewTransaction ("Smart Ops: Revert");
    for (const auto& bk : smartBackup) setBand (bk.band, bk.p);
    smartBackup.clear();
}

juce::AudioProcessorEditor* PrimeqProcessor::createEditor() { return new PrimeqEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PrimeqProcessor(); }
