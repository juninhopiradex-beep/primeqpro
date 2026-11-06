#include "PluginProcessor.h"
#include "FactoryPresets.h"
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
    L.add (std::make_unique<APC> (PID ("look"), "Lookahead", juce::StringArray { "Off", "1 ms", "2 ms", "3 ms", "5 ms", "10 ms" }, 0));
    L.add (std::make_unique<APB> (PID ("dh_on"), "De-harsh On", false));
    L.add (std::make_unique<APF> (PID ("dh_amount"), "De-harsh Amount", juce::NormalisableRange<float> (0.f, 100.f, 0.1f), 50.f, FA().withLabel ("%")));
    L.add (std::make_unique<APF> (PID ("dh_lo"), "De-harsh Low", skewed (1000.f, 8000.f, 2500.f), 2500.f, FA().withLabel ("Hz")));
    L.add (std::make_unique<APF> (PID ("dh_hi"), "De-harsh High", skewed (3000.f, 18000.f, 9000.f), 10000.f, FA().withLabel ("Hz")));
    L.add (std::make_unique<APF> (PID ("dh_sens"), "De-harsh Tolerance", juce::NormalisableRange<float> (0.f, 12.f, 0.1f), 4.f, FA().withLabel ("dB")));
    L.add (std::make_unique<APF> (PID ("dh_attack"), "De-harsh Attack", skewed (1.f, 100.f, 10.f), 8.f, FA().withLabel ("ms")));
    L.add (std::make_unique<APF> (PID ("dh_release"), "De-harsh Release", skewed (10.f, 500.f, 80.f), 80.f, FA().withLabel ("ms")));
    L.add (std::make_unique<APB> (PID ("dh_delta"), "De-harsh Delta (ouvir o removido)", false));
    L.add (std::make_unique<APC> (PID ("ch_mode"), "Character", juce::StringArray { "Off", "Clean", "Subtle", "Warm" }, 0));
    L.add (std::make_unique<APF> (PID ("ch_drive"), "Character Drive", juce::NormalisableRange<float> (0.f, 100.f, 0.1f), 40.f, FA().withLabel ("%")));
    L.add (std::make_unique<APF> (PID ("ch_mix"), "Character Mix", juce::NormalisableRange<float> (0.f, 100.f, 0.1f), 100.f, FA().withLabel ("%")));
    L.add (std::make_unique<APC> (PID ("ch_os"), "Character Oversampling", juce::StringArray { "1x", "2x", "4x" }, 1));
    L.add (std::make_unique<APB> (PID ("morph_on"), "Morph On", false));
    L.add (std::make_unique<APF> (PID ("morph"), "Morph A-B", juce::NormalisableRange<float> (0.f, 100.f, 0.1f), 0.f, FA().withLabel ("%")));
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
    auto G = [&] (const char* k) { return apvts.getRawParameterValue (k); };
    pLook = G ("look"); pDhOn = G ("dh_on"); pDhAmt = G ("dh_amount"); pDhLo = G ("dh_lo"); pDhHi = G ("dh_hi"); pDhSens = G ("dh_sens"); pDhAtk = G ("dh_attack"); pDhRel = G ("dh_release"); pDhDelta = G ("dh_delta");
    pChMode = G ("ch_mode"); pChDrive = G ("ch_drive"); pChMix = G ("ch_mix"); pChOs = G ("ch_os"); pMorphOn = G ("morph_on"); pMorph = G ("morph");
    for (auto& d : dynTel) d.store (0.f);
    for (const char* id : { "phase", "look", "dh_on", "ch_mode", "ch_os" }) apvts.addParameterListener (id, this);
    mixSlotId = pq::MixRegistry::get().add (this);
    if (juce::MessageManager::getInstanceWithoutCreating() != nullptr) mixTimer.startTimer (150);
    worker = std::make_unique<FirWorker> (*this);
    worker->startThread (juce::Thread::Priority::background);
}

PrimeqProcessor::~PrimeqProcessor()
{
    mixTimer.stopTimer(); pq::MixRegistry::get().remove (mixSlotId);
    for (const char* id : { "phase", "look", "dh_on", "ch_mode", "ch_os" }) apvts.removeParameterListener (id, this);
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
int PrimeqProcessor::lookSamplesFor (int idx) const
{
    static const double ms[6] = { 0.0, 1.0, 2.0, 3.0, 5.0, 10.0 };
    return (int) std::lround (ms[std::clamp (idx, 0, 5)] * 0.001 * sr);
}
int PrimeqProcessor::stageLatency (const Stage& st) const
{
    return (st.path ? firLatencyFor (sr) : 0) + st.look + (st.dh ? DeHarsh::kLatency : 0) + (st.os > 0 ? Oversampler::latencyFor (st.os) : 0);
}
int PrimeqProcessor::declaredLatency() const
{
    Stage t; t.path = pPhase->load() > 0.5f ? 1 : 0; t.look = lookSamplesFor ((int) (pLook->load() + 0.5f)); t.dh = pDhOn->load() > 0.5f;
    const int cm = (int) (pChMode->load() + 0.5f); t.os = cm > 0 ? (1 << std::clamp ((int) (pChOs->load() + 0.5f), 0, 2)) : 0;
    return stageLatency (t);
}
void PrimeqProcessor::updateLatency() { const int lat = declaredLatency(); if (lat != latencySamples.load()) { latencySamples.store (lat); setLatencySamples (lat); } }

void PrimeqProcessor::setTrackName (const juce::String& n, bool byUser)
{
    auto& rg = pq::MixRegistry::get(); rg.setName (mixSlotId, n.toStdString()); if (byUser) nameByUser = true;
    if (byUser) rg.setPriority (mixSlotId, pq::guessPriority (n.toStdString()));
}
void PrimeqProcessor::updateTrackProperties (const TrackProperties& tp)
{
    if (nameByUser || ! tp.name.has_value() || tp.name->isEmpty()) return;
    setTrackName (*tp.name, false); pq::MixRegistry::get().setPriority (mixSlotId, pq::guessPriority (tp.name->toStdString()));
}
void PrimeqProcessor::pollMixMail()
{
    auto& rg = pq::MixRegistry::get(); pq::MixMail m; int guard = 0;
    while (guard++ < 8 && rg.takeMail (mixSlotId, m)) smartApply (m.props, m.undoName.c_str());
}

void PrimeqProcessor::parameterChanged (const juce::String&, float) { updateLatency(); }

void PrimeqProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    {
        const juce::ScopedLock sl (firLock);
        sr = sampleRate; maxBlock = std::max (1, samplesPerBlock);
        { const juce::SpinLock::ScopedLockType xl (xfxLock); if (xlatDevice.load() > 0) xfx.setup (xlatDevice.load() - 1, sr); }
        core.prepare (sr);
        for (auto* v : { &dL, &dR, &sL, &sR }) v->assign ((size_t) kChunk, 0.0);
        for (int c = 0; c < 2; ++c) { fIn[c].assign ((size_t) kChunk, 0.f); fOut[c].assign ((size_t) kChunk, 0.f); dryRing[c].assign ((size_t) kDryCap, 0.f); }
        for (auto* v : { &oL, &oR, &pL, &pR, &fTmp }) v->assign ((size_t) kChunk, 0.f);
        dryW = 0;
        const juce::dsp::ProcessSpec spec { sr, (juce::uint32) kChunk, 1 };
        for (auto& c : conv) { c.prepare (spec); c.reset(); }
        firValid.store (false); firCross.store (false); lastKey.clear(); firFs.store ((int) sr);
        analyzer.prepare (sr);
        deharsh.prepare (sr); character.prepare (sr); mixMeter.prepare (sr); rmsPreE = rmsPostE = 0; pq::MixRegistry::get().setAnalysing (mixSlotId, pq::MixRegistry::get().analysing (mixSlotId), sr); mixWas = false;
        lkL.assign ((size_t) kLookRing, 0.0); lkR.assign ((size_t) kLookRing, 0.0); uL.assign ((size_t) kChunk, 0.0); uR.assign ((size_t) kChunk, 0.0); lkW = 0;
        for (auto& v : scBuf) v.assign ((size_t) kChunk, 0.f);
        fadeGain = fadeTarget = 1.f; path = 0; primeLeft = 0; bypState = pBypass->load() > 0.5f ? 1.f : 0.f; gainState = 1.f; agState = 0.f;
        // arranca ja com a configuracao pedida: a latencia declarada e a real coincidem desde o primeiro bloco
        applied = Stage(); applied.path = pPhase->load() > 0.5f ? 1 : 0; applied.look = lookSamplesFor ((int) (pLook->load() + 0.5f)); applied.dh = pDhOn->load() > 0.5f;
        { const int cm = (int) (pChMode->load() + 0.5f); applied.os = cm > 0 ? (1 << std::clamp ((int) (pChOs->load() + 0.5f), 0, 2)) : 0; }
        if (applied.path == 1) applied.path = 0;    // o FIR so entra quando o desenho estiver pronto (transicao segura)
        procLat = stageLatency (applied);
        if (applied.os > 0) character.setup (CharMode::Clean, 0.0, 1.0, applied.os);
        primeLeft = procLat;
    }
    const int lat = declaredLatency();
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

void PrimeqProcessor::resetChain()
{
    core.reset(); for (auto& c : conv) c.reset();
    std::fill (lkL.begin(), lkL.end(), 0.0); std::fill (lkR.begin(), lkR.end(), 0.0); lkW = 0;
    deharsh.reset();
    if (applied.os > 0) character.setup (CharMode::Clean, 0.0, 1.0, applied.os);
    character.reset();
}

void PrimeqProcessor::applyMorph (BandParams* io) const
{
    if (pMorphOn->load (std::memory_order_relaxed) < 0.5f) return;
    const juce::SpinLock::ScopedTryLockType l (morphLock);                 // nunca espera na thread de audio
    if (l.isLocked() && morphSet[0] && morphSet[1]) morphBands (morphA, morphB, pMorph->load (std::memory_order_relaxed) / 100.0, io);
}

void PrimeqProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const auto t0 = juce::Time::getHighResolutionTicks();
    const int total = buffer.getNumSamples();
    const int nch = std::min (2, getMainBusNumInputChannels());
    if (nch < 1 || total < 1) return;

    BandParams bands[kNumBands]; readBands (bands); applyMorph (bands);
    const int abxS = abxSide.load (std::memory_order_relaxed); const float abxC = abxComp.load (std::memory_order_relaxed);
    const int abxBd = abxBand.load (std::memory_order_relaxed); const bool bandAbx = abxS != 0 && abxBd >= 0 && abxBd < kNumBands;
    if (bandAbx && abxS == 1) bands[abxBd].on = false;      // ABX por banda: A = EQ sem a banda k; B = EQ completo
    core.setParams (bands);
    core.setListen (listenBand.load (std::memory_order_relaxed));
    const int phaseIdx = std::clamp ((int) (pPhase->load() + 0.5f), 0, 2);
    const bool bypass = bandAbx ? false : (abxS == 1 ? true : (abxS == 2 ? false : pBypass->load() > 0.5f)), delta = pDelta->load() > 0.5f, autoG = pAuto->load() > 0.5f;
    Stage tgt; tgt.path = phaseIdx != 0 ? 1 : 0; tgt.look = lookSamplesFor ((int) (pLook->load() + 0.5f)); tgt.dh = pDhOn->load() > 0.5f;
    const int chModeIdx = std::clamp ((int) (pChMode->load() + 0.5f), 0, 3);
    tgt.os = chModeIdx > 0 ? (1 << std::clamp ((int) (pChOs->load() + 0.5f), 0, 2)) : 0;
    const bool unlocked = AccessManager::get().isUnlocked();
    const bool scAvail = getBusCount (true) > 1 && getBus (true, 1) != nullptr && getBus (true, 1)->isEnabled();
    const bool scExt = pScExt->load() > 0.5f && scAvail;
    const int scBase = scAvail ? getChannelIndexInProcessBlockBuffer (true, 1, 0) : -1;
    const int scCh = scAvail ? getBus (true, 1)->getNumberOfChannels() : 0;
    const bool wantFir = tgt.path == 1;
    const int latDry = std::min (stageLatency (tgt), kDryCap - kChunk - 1);
    const float fadeStep = (float) (1.0 / (0.008 * sr)), bk = (float) (1.0 - std::exp (-1.0 / (0.005 * sr))), gk = (float) (1.0 - std::exp (-1.0 / (0.02 * sr)));
    const double outDb = pOut->load();
    if (tgt.dh)
    {
        DeHarshParams hp; hp.amount = pDhAmt->load() / 100.0; hp.loHz = pDhLo->load(); hp.hiHz = pDhHi->load(); hp.sensDb = pDhSens->load();
        hp.attackMs = pDhAtk->load(); hp.releaseMs = pDhRel->load(); hp.delta = pDhDelta->load() > 0.5f; deharsh.setParams (hp);
    }
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
        if (mixSlotId >= 0 && pq::MixRegistry::get().analysing (mixSlotId))      // Mix Unmask (opt-in): niveis por banda a cada 25 ms
        {
            if (! mixWas) { mixMeter.reset(); mixWas = true; }
            mixMeter.process (pL.data(), pR.data(), m, [this] (const float* db, float all) { pq::MixRegistry::get().pushFrame (mixSlotId, db, all); pq::SpatialStats st; mixMeter.stats (st); pq::MixRegistry::get().pushStats (mixSlotId, st); });
        }
        else mixWas = false;

        // sidechain externo: alimenta so os detetores (e a captura Unmask); nunca e misturado na saida
        const double *scLp = nullptr, *scRp = nullptr;
        if (scAvail && scBase >= 0)
        {
            const float* s0 = buffer.getReadPointer (scBase); const float* s1 = scCh > 1 ? buffer.getReadPointer (scBase + 1) : s0;
            for (int i = 0; i < m; ++i) { sL[(size_t) i] = s0[off + i]; sR[(size_t) i] = s1[off + i]; scBuf[0][(size_t) i] = s0[off + i]; scBuf[1][(size_t) i] = s1[off + i]; }
            analyzer.pushSc (scBuf[0].data(), scBuf[1].data(), m);
            if (scExt) { scLp = sL.data(); scRp = sR.data(); }
        }

        if (unlocked)
        {
            // transicao segura: qualquer mudanca que altere a latencia (fase, lookahead, De-harsh, Character/oversampling)
            // desvanece para o dry alinhado, troca a configuracao, enche as linhas de atraso e volta
            if (primeLeft > 0) { fadeTarget = 0.f; primeLeft -= m; }
            else if (! (tgt == applied))
            {
                fadeTarget = 0.f;
                const bool firOk = ! wantFir || (firValid.load() && firFs.load() == (int) sr);
                if (fadeGain <= 1e-4f && firOk)
                {
                    applied = tgt; path = applied.path; procLat = std::min (stageLatency (applied), kDryCap - kChunk - 1);
                    resetChain(); primeLeft = procLat; if (primeLeft == 0) fadeTarget = 1.f;
                }
            }
            else fadeTarget = 1.f;

            // lookahead: o programa e atrasado; os detetores veem o sinal SEM atraso (ou o sidechain externo)
            const double *detL = scLp, *detR = scRp;
            if (applied.look > 0)
            {
                const int k = applied.look;
                for (int i = 0; i < m; ++i)
                {
                    uL[(size_t) i] = dL[(size_t) i]; uR[(size_t) i] = dR[(size_t) i];
                    lkL[(size_t) ((lkW + i) & (kLookRing - 1))] = dL[(size_t) i]; lkR[(size_t) ((lkW + i) & (kLookRing - 1))] = dR[(size_t) i];
                    dL[(size_t) i] = lkL[(size_t) ((lkW + i - k) & (kLookRing - 1))]; dR[(size_t) i] = lkR[(size_t) ((lkW + i - k) & (kLookRing - 1))];
                }
                lkW = (lkW + m) & (kLookRing - 1);
                if (! scExt) { detL = uL.data(); detR = uR.data(); }
            }

            if (path == 0) core.process (dL.data(), dR.data(), m, detL, detR, true);
            else { core.process (dL.data(), dR.data(), m, detL, detR, false); firProcess (dL.data(), dR.data(), m); }
            if (applied.dh) { deharsh.process (dL.data(), dR.data(), m); dhRedDb.store ((float) deharsh.lastMaxReductionDb(), std::memory_order_relaxed); }
            if (applied.os > 0)
            {
                character.setup ((CharMode) chModeIdx, pChDrive->load() / 100.0, pChMix->load() / 100.0, applied.os);
                character.process (dL.data(), dR.data(), m);
            }
            if (autoG) agState += ((float) std::clamp (core.autoGainDb(), -24.0, 24.0) - agState) * 0.05f; else agState += (0.f - agState) * 0.05f;
        }

        const double gTarget = std::pow (10.0, (outDb + agState) / 20.0);
        const int lat = latDry;   // o dry alinha SEMPRE com a latencia declarada ao anfitriao (PDC); o wet so a alcanca apos a transicao
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
                if (abxS != 0) { abxAcc += wL * wL + wR * wR; ++abxN; abxState += (abxC - abxState) * 0.002f; wL *= abxState; wR *= abxState; }
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
        if (abxS != 0 && abxN > 0) { const double e = abxAcc / (2.0 * abxN), k = 1.0 - std::exp (-(double) m / (1.5 * sr)); double& s = abxE[abxS - 1]; s += k * (e - s); abxLvlDb[abxS - 1].store ((float) (10.0 * std::log10 (std::max (1e-12, s))), std::memory_order_relaxed); }
        abxAcc = 0; abxN = 0;
        analyzer.pushPost (oL.data(), oR.data(), m);
        {
            double sp = 0, so = 0; for (int i = 0; i < m; ++i) { sp += (double) pL[(size_t) i] * pL[(size_t) i] + (double) pR[(size_t) i] * pR[(size_t) i]; so += (double) oL[(size_t) i] * oL[(size_t) i] + (double) oR[(size_t) i] * oR[(size_t) i]; }
            const double k = 1.0 - std::exp (-(double) m / (1.5 * sr)); rmsPreE += k * (sp / m - rmsPreE); rmsPostE += k * (so / m - rmsPostE);
            if (rmsPreE > 1e-10 && rmsPostE > 1e-10) loudDeltaDb.store ((float) (10.0 * std::log10 (rmsPostE / rmsPreE)), std::memory_order_relaxed);
        }
    }
    if (xlatDevice.load (std::memory_order_relaxed) > 0)
    {
        const juce::SpinLock::ScopedTryLockType xl (xfxLock);
        if (xl.isLocked()) xfx.process (buffer.getWritePointer (0), wr, total);
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
    BandParams b[kNumBands]; readBands (b); applyMorph (b); { const int ab = abxBand.load (std::memory_order_relaxed); if (abxSide.load (std::memory_order_relaxed) == 1 && ab >= 0 && ab < kNumBands) b[ab].on = false; } double dyn[kNumBands];
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
    root.setAttribute ("uiW", uiWidth); root.setAttribute ("uiH", uiHeight); root.setAttribute ("slot", slot); root.setAttribute ("mode", uiMode); root.setAttribute ("mixGroup", pq::MixRegistry::get().group (mixSlotId));
    { auto& rg = pq::MixRegistry::get(); root.setAttribute ("mixName", juce::String (rg.name (mixSlotId))); root.setAttribute ("mixPri", rg.priority (mixSlotId)); root.setAttribute ("mixOn", rg.analysing (mixSlotId) ? 1 : 0); root.setAttribute ("mixUser", nameByUser ? 1 : 0); }
    if (auto x = st.createXml()) root.addChildElement (x.release());
    for (int i = 0; i < 2; ++i) if (slots[i].isValid()) { auto* e = new juce::XmlElement ("AB"); e->setAttribute ("i", i); if (auto x = slots[i].createXml()) e->addChildElement (x.release()); root.addChildElement (e); }
    {
        const juce::SpinLock::ScopedLockType l (morphLock);
        for (int w = 0; w < 2; ++w)
        {
            if (! morphSet[w]) continue;
            auto* e = new juce::XmlElement ("MORPH"); e->setAttribute ("w", w); const BandParams* src = w == 0 ? morphA : morphB;
            for (int b = 0; b < kNumBands; ++b)
            {
                const BandParams& p = src[b];
                e->setAttribute ("b" + juce::String (b), juce::String::formatted ("%d,%d,%.6f,%.6f,%.6f,%d,%d,%d,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%d,%d", p.on ? 1 : 0, (int) p.type, p.freq, p.gain, p.q, p.slope, (int) p.place, p.dyn ? 1 : 0,
                                 p.thr, p.range, p.ratio, p.attackMs, p.holdMs, p.releaseMs, p.kneeDb, (int) p.det, (int) p.dir));
            }
            root.addChildElement (e);
        }
    }
    {
        const juce::SpinLock::ScopedLockType l (decLock);
        auto* ds = new juce::XmlElement ("DECISIONS");
        for (const auto& d : decLog) { auto* e = new juce::XmlElement ("D"); e->setAttribute ("t", juce::String ((juce::int64) d.ms)); e->setAttribute ("pt", juce::String (juce::CharPointer_UTF8 (d.pt.c_str()))); e->setAttribute ("en", juce::String (juce::CharPointer_UTF8 (d.en.c_str()))); e->setAttribute ("k", juce::String (d.kind)); e->setAttribute ("b", juce::String (d.bands)); ds->addChildElement (e); }
        if (hasCustom) { juce::String s; for (double v : customTarget) s << juce::String (v, 3) << ","; ds->setAttribute ("custom", s); }
        root.addChildElement (ds);
    }
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
        slot = xml->getIntAttribute ("slot", 0) & 1; uiMode = std::clamp (xml->getIntAttribute ("mode", 1), 0, 2); pq::MixRegistry::get().setGroup (mixSlotId, xml->getIntAttribute ("mixGroup", 1));
        if (xml->hasAttribute ("mixName")) { auto& rg = pq::MixRegistry::get(); nameByUser = xml->getIntAttribute ("mixUser", 0) != 0; if (nameByUser) rg.setName (mixSlotId, xml->getStringAttribute ("mixName").toStdString()); rg.setPriority (mixSlotId, xml->getIntAttribute ("mixPri", 2)); rg.setAnalysing (mixSlotId, xml->getIntAttribute ("mixOn", 0) != 0, sr); }
        if (auto* ds = xml->getChildByName ("DECISIONS"))
        {
            const juce::SpinLock::ScopedLockType l (decLock); decLog.clear();
            for (auto* e : ds->getChildWithTagNameIterator ("D")) { pq::studio::Decision d; d.ms = e->getStringAttribute ("t").getLargeIntValue(); d.pt = e->getStringAttribute ("pt").toStdString(); d.en = e->getStringAttribute ("en").toStdString(); d.kind = e->getStringAttribute ("k").toStdString(); d.bands = e->getStringAttribute ("b").toStdString(); if (decLog.size() < 40) decLog.push_back (std::move (d)); }
            hasCustom = false; const auto t = juce::StringArray::fromTokens (ds->getStringAttribute ("custom"), ",", "");
            if (t.size() >= pq::kMixBands) { for (int b = 0; b < pq::kMixBands; ++b) customTarget[(size_t) b] = t[b].getDoubleValue(); hasCustom = true; }
        }
        for (auto* e : xml->getChildWithTagNameIterator ("AB")) if (auto* c = e->getFirstChildElement()) slots[e->getIntAttribute ("i") & 1] = juce::ValueTree::fromXml (*c);
        {
            const juce::SpinLock::ScopedLockType l (morphLock); morphSet[0] = morphSet[1] = false;
            for (auto* e : xml->getChildWithTagNameIterator ("MORPH"))
            {
                const int w = e->getIntAttribute ("w") & 1; BandParams* dst = w == 0 ? morphA : morphB; bool ok = true;
                for (int b = 0; b < kNumBands; ++b)
                {
                    const auto t = juce::StringArray::fromTokens (e->getStringAttribute ("b" + juce::String (b)), ",", ""); if (t.size() < 17) { ok = false; break; }
                    BandParams& p = dst[b]; p.on = t[0].getIntValue() != 0; p.type = (BandType) std::clamp (t[1].getIntValue(), 0, 7); p.freq = t[2].getDoubleValue(); p.gain = t[3].getDoubleValue(); p.q = t[4].getDoubleValue();
                    p.slope = std::clamp (t[5].getIntValue(), 0, kNumSlopes - 1); p.place = (Placement) std::clamp (t[6].getIntValue(), 0, 4); p.dyn = t[7].getIntValue() != 0;
                    p.thr = t[8].getDoubleValue(); p.range = t[9].getDoubleValue(); p.ratio = t[10].getDoubleValue(); p.attackMs = t[11].getDoubleValue(); p.holdMs = t[12].getDoubleValue();
                    p.releaseMs = t[13].getDoubleValue(); p.kneeDb = t[14].getDoubleValue(); p.det = (DetMode) std::clamp (t[15].getIntValue(), 0, 1); p.dir = (DynDir) std::clamp (t[16].getIntValue(), 0, 1);
                }
                morphSet[w] = ok;
            }
        }
    }
    undo.clearUndoHistory();
    updateLatency();
}

void PrimeqProcessor::morphCapture (int which)
{
    BandParams cur[kNumBands]; readBands (cur);
    const juce::SpinLock::ScopedLockType l (morphLock);
    BandParams* dst = (which & 1) ? morphB : morphA; for (int b = 0; b < kNumBands; ++b) dst[b] = cur[b];
    morphSet[which & 1] = true;
}
void PrimeqProcessor::morphClear() { const juce::SpinLock::ScopedLockType l (morphLock); morphSet[0] = morphSet[1] = false; }

void PrimeqProcessor::resetFx()
{
    for (const char* k : { "look", "dh_on", "dh_amount", "dh_lo", "dh_hi", "dh_sens", "dh_attack", "dh_release", "dh_delta", "ch_mode", "ch_drive", "ch_mix", "ch_os", "morph_on", "morph" })
        if (auto* p = apvts.getParameter (k)) { p->beginChangeGesture(); p->setValueNotifyingHost (p->getDefaultValue()); p->endChangeGesture(); }
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
    static const std::vector<FactoryPreset> v = []
    {
    std::vector<FactoryPreset> r {
        { "Init (Flat)", [] (PrimeqProcessor& p) { p.resetAllBands(); p.resetFx(); } },
        { "Vocal Presence", [] (PrimeqProcessor& p) { p.resetAllBands(); p.setBand (0, band (BandType::HighPass, 80, 0, 0.707, 3)); p.setBand (1, band (BandType::Bell, 300, -2.5, 1.2)); p.setBand (2, band (BandType::Bell, 3500, 2.0, 1.0)); p.setBand (3, band (BandType::HighShelf, 10000, 2.0, 0.707)); } },
        { "Master - Gentle Smile", [] (PrimeqProcessor& p) { p.resetAllBands(); p.setBand (0, band (BandType::LowShelf, 60, 1.2, 0.707)); p.setBand (1, band (BandType::Bell, 350, -1.0, 0.8)); p.setBand (2, band (BandType::HighShelf, 12000, 1.5, 0.707)); } },
        { "Low Cut 30 Hz (48 dB/oct)", [] (PrimeqProcessor& p) { p.resetAllBands(); p.setBand (0, band (BandType::HighPass, 30, 0, 0.707, 5)); } },
        { "Dynamic De-Mud (280 Hz)", [] (PrimeqProcessor& p) { p.resetAllBands(); auto b = band (BandType::Bell, 280, 0, 1.4); b.dyn = true; b.thr = -28; b.range = -4; b.ratio = 2.5; b.attackMs = 15; b.releaseMs = 150; p.setBand (0, b); } },
        { "Bass - Tighten & Clarity", [] (PrimeqProcessor& p) { p.resetAllBands(); p.setBand (0, band (BandType::HighPass, 30, 0, 0.707, 4)); p.setBand (1, band (BandType::Bell, 250, -2.0, 1.4)); p.setBand (2, band (BandType::Bell, 900, 1.5, 1.0)); } },
        { "Kick - Punch & Carve", [] (PrimeqProcessor& p) { p.resetAllBands(); p.setBand (0, band (BandType::HighPass, 28, 0, 0.707, 3)); p.setBand (1, band (BandType::Bell, 60, 2.5, 1.2)); p.setBand (2, band (BandType::Bell, 350, -3.5, 1.5)); p.setBand (3, band (BandType::Bell, 4500, 2.0, 1.0)); } },
        { "Vocal - De-Mud + Air", [] (PrimeqProcessor& p) { p.resetAllBands(); p.setBand (0, band (BandType::HighPass, 90, 0, 0.707, 3)); p.setBand (1, band (BandType::Bell, 250, -2.5, 1.1)); p.setBand (2, band (BandType::HighShelf, 11000, 2.5, 0.707)); } },
        { "Master - Warm Glue (Character Subtle)", [] (PrimeqProcessor& p) { p.resetAllBands(); p.resetFx(); p.setBand (0, band (BandType::LowShelf, 70, 1.0, 0.707)); p.setBand (1, band (BandType::HighShelf, 12000, 1.2, 0.707));
            p.setParamReal ("ch_mode", 2.f); p.setParamReal ("ch_drive", 35.f); p.setParamReal ("ch_mix", 60.f); p.setParamReal ("ch_os", 2.f); } },
        { "Harsh Vocal Tamer (De-harsh)", [] (PrimeqProcessor& p) { p.resetAllBands(); p.resetFx(); p.setBand (0, band (BandType::HighPass, 80, 0, 0.707, 3));
            p.setParamReal ("dh_on", 1.f); p.setParamReal ("dh_amount", 55.f); p.setParamReal ("dh_lo", 2500.f); p.setParamReal ("dh_hi", 9000.f); p.setParamReal ("dh_sens", 4.f); } },
        { "Dynamic De-Ess (7 kHz)", [] (PrimeqProcessor& p) { p.resetAllBands(); auto b = band (BandType::Bell, 7000, 0, 3.0); b.dyn = true; b.thr = -35; b.range = -6; b.ratio = 4; b.attackMs = 1; b.releaseMs = 60; b.det = DetMode::Peak; p.setBand (0, b); } },
    };
    for (const auto& d : pq::fp::defs())          // presets originais organizados por categoria ("Categoria/Nome")
        r.push_back ({ d.name, [&d] (PrimeqProcessor& p)
        {
            p.resetAllBands(); p.resetFx();
            for (size_t i = 0; i < d.bands.size() && i < (size_t) kNumBands; ++i) p.setBand ((int) i, d.bands[i]);
            const auto& f = d.fx; if (f.phase) p.setParamReal ("phase", (float) f.phase);
            if (f.ch) { p.setParamReal ("ch_mode", (float) f.ch); p.setParamReal ("ch_drive", f.drive); p.setParamReal ("ch_mix", f.mix); p.setParamReal ("ch_os", (float) f.os); }
            if (f.dh) { p.setParamReal ("dh_on", 1.f); p.setParamReal ("dh_amount", f.dhAmt); p.setParamReal ("dh_lo", f.dhLo); p.setParamReal ("dh_hi", f.dhHi); p.setParamReal ("dh_sens", f.dhSens); }
        } });
    return r;
    }();
    return v;
}
void PrimeqProcessor::loadFactoryPreset (int idx)
{
    const auto& v = factoryPresets(); if (idx < 0 || idx >= (int) v.size()) return;
    undo.beginNewTransaction (v[(size_t) idx].name); resetFx(); v[(size_t) idx].apply (*this);
    logDecision (std::string ("Preset: ") + v[(size_t) idx].name, std::string ("Preset: ") + v[(size_t) idx].name, "preset");
}

// ------------------------------------------------------------------------------------------ STUDIO
void PrimeqProcessor::setTranslator (int dev)
{
    const juce::SpinLock::ScopedLockType l (xfxLock);
    if (dev > 0) xfx.setup (std::clamp (dev, 1, 5) - 1, sr);
    xlatDevice.store (std::clamp (dev, 0, 5));
}
bool PrimeqProcessor::ownCurve (pq::studio::Curve& out, int minFrames) const
{
    out = pq::studio::emptyCurve(); if (mixSlotId < 0) return false;
    std::vector<std::array<float, pq::kMixBands>> fr; pq::MixRegistry::get().copyFrames (mixSlotId, pq::kMixHist - 2, fr); if ((int) fr.size() < minFrames) return false;
    int ok = 0;
    for (int b = 0; b < pq::kMixBands; ++b)
    {
        std::vector<double> a; for (const auto& f : fr) if (f[(size_t) b] > -90.f) a.push_back (f[(size_t) b]);   // so quadros com sinal
        if ((int) a.size() >= minFrames) { out[(size_t) b] = pq::percentileOf (a, 0.5); ++ok; }
    }
    return ok >= 12;
}
void PrimeqProcessor::logDecision (const std::string& pt, const std::string& en, const char* kind)
{
    BandParams cur[kNumBands]; readBands (cur);
    pq::studio::Decision d; d.ms = juce::Time::currentTimeMillis(); d.pt = pt; d.en = en; d.kind = kind; d.bands = pq::studio::encodeBands (cur);
    const juce::SpinLock::ScopedLockType l (decLock); decLog.push_back (std::move (d)); while (decLog.size() > 40) decLog.erase (decLog.begin());
}
bool PrimeqProcessor::restoreDecision (int i)
{
    pq::studio::Decision d; { const juce::SpinLock::ScopedLockType l (decLock); if (i < 0 || i >= (int) decLog.size()) return false; d = decLog[(size_t) i]; }
    BandParams tgt[kNumBands]; pq::studio::decodeBands (d.bands, tgt);
    undo.beginNewTransaction ("Cofre: restaurar");
    for (int b = 0; b < kNumBands; ++b) { BandParams x = tgt[b]; if (! x.on) { BandParams cur = getBand (b); if (! cur.on) continue; x = cur; x.on = false; } setBand (b, x); }
    return true;
}

// ------------------------------------------------------------------------------------------ Smart Ops (aplicar / reverter)
int PrimeqProcessor::smartApply (const std::vector<DeresProposal>& props, const char* undoName)
{
    undo.beginNewTransaction (undoName);
    smartBackup.clear(); smartScChanged = false; int created = 0, b = kNumBands - 1;
    const std::string uname = undoName;
    BandParams cur[kNumBands]; readBands (cur);
    bool needSc = false;
    for (const auto& pr : props)
    {
        while (b >= 0 && cur[b].on) --b;
        if (b < 0) break;
        smartBackup.push_back ({ b, cur[b] });
        BandParams p = cur[b]; p.on = true; p.type = (BandType) std::clamp (pr.type, 0, 7); p.freq = pr.freq; p.q = pr.q; p.place = (Placement) std::clamp (pr.place, 0, 4); if (pr.slope >= 0) p.slope = std::clamp (pr.slope, 0, kNumSlopes - 1);
        if (pr.dynamic)
        {
            p.gain = 0.0; p.dyn = true; p.range = pr.gainDb; p.thr = pr.thrDb; p.ratio = pr.ratio; p.attackMs = pr.attackMs; p.releaseMs = pr.releaseMs;
            p.holdMs = 0.0; p.kneeDb = 6.0; p.det = DetMode::Peak; p.dir = DynDir::Above; needSc = needSc || pr.needsSidechain;
        }
        else { p.gain = pr.gainDb; p.dyn = false; }
        setBand (b, p); cur[b].on = true; ++created; --b;
    }
    if (needSc && created > 0)
    {
        smartScOld = getParamReal ("scext"); smartScChanged = smartScOld < 0.5f;
        if (smartScChanged) setParamReal ("scext", 1.f);
    }
    if (created > 0) logDecision (uname + ": " + std::to_string (created) + " banda(s)", uname + ": " + std::to_string (created) + " band(s)", "smart");
    return created;
}
void PrimeqProcessor::smartRevert()
{
    undo.beginNewTransaction ("Smart Ops: Revert");
    for (const auto& bk : smartBackup) setBand (bk.band, bk.p);
    if (smartScChanged) setParamReal ("scext", smartScOld);
    smartBackup.clear(); smartScChanged = false;
}

juce::AudioProcessorEditor* PrimeqProcessor::createEditor() { return new PrimeqEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PrimeqProcessor(); }
