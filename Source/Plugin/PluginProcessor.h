// =============================================================================
//  PRIMEQ pro by Piradex — AudioProcessor (adaptador JUCE)
//  Camadas: Plugin Host Adapter -> Parameter/State Model (APVTS) -> DSP (EqCore / FIR) -> Analysis -> GUI
// =============================================================================
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cstdio>
#include <atomic>
#include <memory>
#include <vector>
#include "../DSP/EqCore.h"
#include "../DSP/FirDesign.h"
#include "../Analysis/AnalyzerEngine.h"
#include "Access.h"

namespace pq
{
inline std::string bid (int band0, const char* key) { char b[48]; std::snprintf (b, sizeof b, "b%02d_%s", band0 + 1, key); return b; }
inline const char* kStateVersionKey = "stateVersion";
constexpr int kStateVersion = 1;
}

class FirWorker;

class PrimeqProcessor : public juce::AudioProcessor, private juce::AudioProcessorValueTreeState::Listener
{
public:
    PrimeqProcessor();
    ~PrimeqProcessor() override;

    // ---- AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "PRIMEQ pro by Piradex"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- modelo de parametros
    juce::UndoManager undo;
    juce::AudioProcessorValueTreeState apvts;
    void readBands (pq::BandParams* out) const;                     // seguro em qualquer thread
    pq::BandParams getBand (int b) const { pq::BandParams a[pq::kNumBands]; readBands (a); return a[b]; }
    void setBand (int b, const pq::BandParams& p);                  // thread de mensagens (undo)
    void setParamReal (const juce::String& id, float realValue);
    float getParamReal (const juce::String& id) const;
    void resetAllBands();

    // ---- presets / A-B
    struct FactoryPreset { const char* name; void (*apply) (PrimeqProcessor&); };
    static const std::vector<FactoryPreset>& factoryPresets();
    void loadFactoryPreset (int idx);
    int  abSlot() const { return slot; }
    void toggleAB();
    void saveUserPreset (const juce::String& name);
    juce::StringArray userPresetNames() const;
    bool loadUserPreset (const juce::String& name);

    // ---- Smart Ops
    int  smartApply (const std::vector<pq::DeresProposal>& p);      // devolve quantas bandas criou
    void smartRevert();
    bool smartCanRevert() const { return ! smartBackup.empty(); }

    // ---- telemetria (lock-free)
    pq::AnalyzerEngine analyzer;
    std::array<std::atomic<float>, pq::kNumBands> dynTel {};
    std::atomic<float> meterL { 0.f }, meterR { 0.f }, cpuLoad { 0.f };
    std::atomic<int> listenBand { -1 };
    int  latencyFor (int phaseIdx) const;
    int  currentLatency() const { return latencySamples.load(); }
    double sampleRateNow() const { return sr; }
    int  uiWidth = 1180, uiHeight = 720;

    // ---- usado pelo trabalhador FIR
    void firDesignTick();

private:
    void parameterChanged (const juce::String& id, float v) override;
    void firProcess (double* L, double* R, int m);
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void migrateState (juce::ValueTree& vt, int fromVersion);

    struct BandPtrs { std::atomic<float> *on, *type, *freq, *gain, *q, *slope, *place, *dyn, *thr, *range, *ratio, *attack, *hold, *release, *knee, *det, *dir; };
    std::array<BandPtrs, pq::kNumBands> bp {};
    std::atomic<float> *pPhase = nullptr, *pBypass = nullptr, *pOut = nullptr, *pAuto = nullptr, *pDelta = nullptr, *pScExt = nullptr;

    pq::EqCore core;
    double sr = 48000.0; int maxBlock = 512;
    static constexpr int kChunk = 256, kDryCap = 1 << 16;
    std::vector<double> dL, dR, sL, sR; std::vector<float> fIn[2], fOut[2], dryRing[2], oL, oR, pL, pR, fTmp;
    int dryW = 0, primeLeft = 0;
    float fadeGain = 1.f, fadeTarget = 1.f, bypState = 0.f, gainState = 1.f, agState = 0.f; int path = 0;

    // FIR
    juce::dsp::ConvolutionMessageQueue convQueue { 128 };
    juce::dsp::Convolution conv[4] { juce::dsp::Convolution (convQueue), juce::dsp::Convolution (convQueue), juce::dsp::Convolution (convQueue), juce::dsp::Convolution (convQueue) };
    std::unique_ptr<FirWorker> worker;
    juce::CriticalSection firLock;
    pq::FirDesigner designer; pq::FirKernels kernels; std::vector<double> lastKey;
    std::atomic<bool> firValid { false }, firCross { false }; std::atomic<int> firFs { 0 };
    std::atomic<int> latencySamples { 0 };

    // A/B e Smart Ops
    juce::ValueTree slots[2]; int slot = 0;
    struct Backup { int band; pq::BandParams p; };
    std::vector<Backup> smartBackup;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PrimeqProcessor)
};
