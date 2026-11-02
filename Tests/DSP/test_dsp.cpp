// Testes DSP do PRIMEQ pro (C++ puro, sem JUCE): respostas analiticas vs medidas, identidade, M/S, dinamica,
// sidechain, estabilidade, FFT, FIR (simetria / magnitude), Deresonate e CPU.
#include "../../Source/DSP/EqCore.h"
#include "../../Source/DSP/FirDesign.h"
#include "../../Source/Analysis/SpectrumCalc.h"
#include "../../Source/SmartOps/Deresonate.h"
#include "../../Source/SmartOps/DeEss.h"
#include "../../Source/SmartOps/Unmask.h"
#include "../../Source/SmartOps/ReferenceMatch.h"
#include "../../Source/SmartOps/Morph.h"
#include "../../Source/DSP/Oversampler.h"
#include "../../Source/DSP/Character.h"
#include "../../Source/DSP/DeHarsh.h"
#include "../../Source/DSP/MixMeter.h"
#include "../../Source/Plugin/MixRegistry.h"
#include "../../Source/SmartOps/Consequence.h"
#include "../../Source/Shared/Units.h"
#include <chrono>
#include <cstdio>
#include <random>

using namespace pq;
static int fails = 0, passes = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; std::printf ("FALHOU  "); std::printf (__VA_ARGS__); std::printf ("\n"); } else { ++passes; std::printf ("ok      "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

static const double FS = 48000.0;

struct Rig
{
    EqCore eq; BandParams b[kNumBands];
    Rig (double fs = FS) { eq.prepare (fs); }
    void push() { eq.setParams (b); }
};

// RMS (dB) do canal depois de 'settle' amostras
static double rmsDb (const std::vector<double>& x, size_t from)
{
    double s = 0; for (size_t i = from; i < x.size(); ++i) s += x[i] * x[i];
    return 10.0 * std::log10 (std::max (1e-30, s / (double) (x.size() - from)));
}
static double measureGainDb (Rig& r, double f, double fs = FS, Placement /*unused*/ = Placement::Stereo)
{
    const size_t n = (size_t) (fs * 1.0);
    std::vector<double> L (n), R (n), inp (n);
    for (size_t i = 0; i < n; ++i) { L[i] = R[i] = inp[i] = 0.25 * std::sin (2.0 * M_PI * f * (double) i / fs); }
    r.eq.reset(); r.push();
    for (size_t off = 0; off < n; off += 512) { const int m = (int) std::min<size_t> (512, n - off); r.push(); r.eq.process (L.data() + off, R.data() + off, m, nullptr, nullptr, true); }
    return rmsDb (L, n / 2) - rmsDb (inp, n / 2);
}

int main()
{
    // 1) Bell +6 dB, 1 kHz, Q=1: medido vs analitico
    {
        Rig r; auto& b = r.b[0]; b.on = true; b.type = BandType::Bell; b.freq = 1000; b.gain = 6; b.q = 1;
        double worst = 0;
        for (double f : { 100.0, 500.0, 800.0, 1000.0, 1250.0, 2000.0, 8000.0 })
        {
            const double meas = measureGainDb (r, f), ana = magDb (designBand (BandType::Bell, 1, FS, 1000, 6, 1), 2 * M_PI * f / FS);
            worst = std::max (worst, std::fabs (meas - ana));
        }
        CHECK (worst < 0.02, "Bell +6 dB @1k Q=1: |medido-analitico| max = %.4f dB", worst);
        CHECK (std::fabs (measureGainDb (r, 1000.0) - 6.0) < 0.02, "Bell +6 dB: ganho real a 1 kHz = %.3f dB", measureGainDb (r, 1000.0));
    }
    // 2) HP 80 Hz 24 dB/oct: 40 / 80 / 160 Hz
    {
        Rig r; auto& b = r.b[0]; b.on = true; b.type = BandType::HighPass; b.freq = 80; b.slope = 3;   // 24 dB/oct
        const double g40 = measureGainDb (r, 40), g80 = measureGainDb (r, 80), g160 = measureGainDb (r, 160);
        CHECK (std::fabs (g80 + 3.01) < 0.1, "HP80 24dB/oct: a 80 Hz = %.2f dB (esperado -3.01)", g80);
        CHECK (std::fabs (g40 + 24.1) < 0.4, "HP80 24dB/oct: a 40 Hz = %.2f dB (esperado ~-24.1)", g40);
        CHECK (std::fabs (g160) < 0.15, "HP80 24dB/oct: a 160 Hz = %.2f dB (esperado ~-0.07)", g160);
    }
    // 3) declives 6..96 e Brickwall: atenuacao uma oitava abaixo de fc
    {
        for (int s = 0; s < kNumSlopes; ++s)
        {
            Rig r; auto& b = r.b[0]; b.on = true; b.type = BandType::LowPass; b.freq = 4000; b.slope = s;
            const double ana = magDb (designBand (BandType::LowPass, s, FS, 4000, 0, 0.707), 2 * M_PI * 8000 / FS);
            const double meas = measureGainDb (r, 8000);
            const double warp = std::tan (M_PI * 8000.0 / FS) / std::tan (M_PI * 4000.0 / FS);   // deformacao da transformada bilinear
            const double expected = -10.0 * std::log10 (1.0 + std::pow (warp, 2.0 * kSlopeOrder[s]));
            if (kSlopeOrder[s] != 2 && kSlopeOrder[s] <= 16)
                CHECK (std::fabs (ana - expected) < 1.0 && std::fabs (meas - ana) < 0.3, "LP 4k, %s: oitava acima = %.1f dB (Butterworth bilinear ~%.1f), medido-analitico %.2f", kSlopeDb[s] ? (std::to_string (kSlopeDb[s]) + " dB/oct").c_str() : "Brickwall", meas, expected, meas - ana);
            else CHECK (std::isfinite (meas) && meas < -10, "LP 4k, %s: oitava acima = %.1f dB", kSlopeDb[s] ? (std::to_string (kSlopeDb[s]) + " dB/oct").c_str() : "Brickwall", meas);
        }
    }
    // 4) 0 dB = identidade; todos os tipos com ganho 0
    {
        Rig r; for (int i = 0; i < 8; ++i) { auto& b = r.b[i]; b.on = true; b.type = (BandType) (i < 3 ? i : 0); b.freq = 200.0 * (i + 1); b.gain = 0; b.q = 1; }
        r.eq.reset();
        std::mt19937 rng (1); std::uniform_real_distribution<double> d (-0.5, 0.5);
        std::vector<double> L (4096), R (4096), L0, R0; for (auto& v : L) v = d (rng); for (auto& v : R) v = d (rng); L0 = L; R0 = R;
        r.push(); r.eq.process (L.data(), R.data(), 4096, nullptr, nullptr, true);
        double e = 0; for (size_t i = 0; i < L.size(); ++i) e = std::max ({ e, std::fabs (L[i] - L0[i]), std::fabs (R[i] - R0[i]) });
        CHECK (e < 1e-9, "identidade 0 dB (8 bandas, ruido): erro max = %.2e", e);
    }
    // 5) Mid/Side: reconstrucao unitaria e seletividade
    {
        Rig r; auto& b = r.b[0]; b.on = true; b.type = BandType::Bell; b.freq = 1000; b.gain = 6; b.q = 1; b.place = Placement::Mid;
        const size_t n = 48000; std::vector<double> L (n), R (n);
        for (size_t i = 0; i < n; ++i) { const double s = 0.25 * std::sin (2 * M_PI * 1000.0 * (double) i / FS); L[i] = s; R[i] = -s; }   // so Side
        r.eq.reset(); r.push(); r.eq.process (L.data(), R.data(), (int) n, nullptr, nullptr, true);
        CHECK (std::fabs (rmsDb (L, n / 2) - rmsDb (std::vector<double> (n, 0.25 * std::sqrt (0.5)), 0)) < 0.01, "Mid +6 dB nao toca num sinal so-Side (L/R em oposicao)");
        std::vector<double> L2 (n), R2 (n); for (size_t i = 0; i < n; ++i) L2[i] = R2[i] = 0.25 * std::sin (2 * M_PI * 1000.0 * (double) i / FS);
        r.eq.reset(); r.push(); r.eq.process (L2.data(), R2.data(), (int) n, nullptr, nullptr, true);
        CHECK (std::fabs (rmsDb (L2, n / 2) - rmsDb (std::vector<double> (n, 0.25 * std::sqrt (0.5)), 0) - 6.0) < 0.05, "Mid +6 dB sobe 6 dB num sinal mono (medido %.2f)", rmsDb (L2, n / 2) - rmsDb (std::vector<double> (n, 0.25 * std::sqrt (0.5)), 0));
        Rig r2; r2.b[0] = b; r2.b[0].gain = 0; r2.push(); r2.eq.reset(); std::mt19937 rng (3); std::uniform_real_distribution<double> d (-0.5, 0.5);
        std::vector<double> A (2048), B (2048), A0, B0; for (auto& v : A) v = d (rng); for (auto& v : B) v = d (rng); A0 = A; B0 = B;
        r2.eq.process (A.data(), B.data(), 2048, nullptr, nullptr, true);
        double e = 0; for (size_t i = 0; i < A.size(); ++i) e = std::max ({ e, std::fabs (A[i] - A0[i]), std::fabs (B[i] - B0[i]) });
        CHECK (e < 1e-12, "M/S encode/decode com 0 dB: passagem unitaria (erro %.2e)", e);
    }
    // 6) Dinamica: reducao medida, attack/release, sidechain
    {
        Rig r; auto& b = r.b[0]; b.on = true; b.type = BandType::Bell; b.freq = 1000; b.gain = 0; b.q = 1; b.dyn = true;
        b.thr = -30; b.range = -12; b.ratio = 100; b.kneeDb = 0; b.attackMs = 10; b.releaseMs = 100; b.det = DetMode::Peak;
        const size_t n = (size_t) (FS * 2); std::vector<double> L (n), R (n);
        for (size_t i = 0; i < n; ++i) { const double a = i < n / 2 ? 0.001 : 0.5; L[i] = R[i] = a * std::sin (2 * M_PI * 1000.0 * (double) i / FS); }  // -60 dBFS -> -6 dBFS
        std::vector<double> gr; r.eq.reset();
        for (size_t off = 0; off < n; off += 64) { r.push(); r.eq.process (L.data() + off, R.data() + off, 64, nullptr, nullptr, true); gr.push_back (r.eq.dynDb (0)); }
        const double grEnd = gr.back(), grStart = gr[100];
        CHECK (std::fabs (grStart) < 0.01, "dinamica: abaixo do threshold, GR = %.3f dB", grStart);
        CHECK (std::fabs (grEnd + 12.0) < 0.2, "dinamica: acima do threshold (ratio 100), GR = %.2f dB (esperado -12)", grEnd);
        const double outDb = rmsDb (std::vector<double> (L.begin() + (long) (n * 0.9), L.end()), 0) - rmsDb (std::vector<double> (n / 10, 0.5 * std::sqrt (0.5)), 0);
        CHECK (std::fabs (outDb + 12.0) < 0.5, "dinamica: ganho de audio medido = %.2f dB (esperado ~-12)", outDb);
        // tempo de ataque: 63% de -12 dB
        size_t idx63 = 0; for (size_t i = (n / 2) / 64; i < gr.size(); ++i) if (gr[i] <= -12.0 * 0.632) { idx63 = i; break; }
        const double t63 = ((double) idx63 * 64.0 - (double) n / 2) / FS * 1000.0;
        CHECK (t63 > 3.0 && t63 < 25.0, "dinamica: tempo a 63%% do ataque = %.1f ms (attack 10 ms)", t63);
    }
    {   // sidechain externo nao vaza para a saida
        Rig r; auto& b = r.b[0]; b.on = true; b.type = BandType::Bell; b.freq = 1000; b.gain = 0; b.q = 1; b.dyn = true; b.thr = -40; b.range = -12; b.ratio = 100; b.kneeDb = 0;
        const size_t n = 48000; std::vector<double> L (n, 0.0), R (n, 0.0), sc (n);
        for (size_t i = 0; i < n; ++i) sc[i] = 0.5 * std::sin (2 * M_PI * 1000.0 * (double) i / FS);
        r.eq.reset(); r.push();
        for (size_t off = 0; off < n; off += 128) r.eq.process (L.data() + off, R.data() + off, 128, sc.data() + off, sc.data() + off, true);
        double mx = 0; for (double v : L) mx = std::max (mx, std::fabs (v));
        CHECK (mx == 0.0, "sidechain externo: saida (entrada silenciosa) = 0 -> sem fuga (max %.3e)", mx);
        CHECK (std::fabs (r.eq.dynDb (0) + 12.0) < 0.3, "sidechain externo: GR dirigido pelo sidechain = %.2f dB", r.eq.dynDb (0));
    }
    // 7) estabilidade: 24 bandas em modo extremo, ruido, silencio, NaN
    {
        Rig r; std::mt19937 rng (7); std::uniform_real_distribution<double> u (0, 1);
        for (int pass = 0; pass < 20; ++pass)
        {
            for (int i = 0; i < kNumBands; ++i)
            {
                auto& b = r.b[i]; b.on = true; b.type = (BandType) (int) (u (rng) * 8); b.freq = std::exp (std::log (10.0) + u (rng) * std::log (3000.0));
                b.gain = (u (rng) * 2 - 1) * 30; b.q = std::exp (std::log (0.025) + u (rng) * std::log (1600.0)); b.slope = (int) (u (rng) * kNumSlopes);
                b.place = (Placement) (int) (u (rng) * 5); b.dyn = u (rng) > 0.5; b.thr = -60 + u (rng) * 60; b.range = (u (rng) * 2 - 1) * 30; b.ratio = 1 + u (rng) * 19;
                b.attackMs = 0.1 + u (rng) * 100; b.releaseMs = 5 + u (rng) * 500; b.det = u (rng) > 0.5 ? DetMode::Rms : DetMode::Peak;
            }
            std::vector<double> L (4096), R (4096); for (auto& v : L) v = (u (rng) * 2 - 1) * 0.9; for (auto& v : R) v = (u (rng) * 2 - 1) * 0.9;
            for (int blk = 0; blk < 6; ++blk) { r.push(); r.eq.process (L.data(), R.data(), 4096, nullptr, nullptr, true); }
            bool bad = false; for (size_t i = 0; i < L.size(); ++i) if (! std::isfinite (L[i]) || ! std::isfinite (R[i])) bad = true;
            if (bad) { CHECK (false, "estabilidade: NaN/Inf na passagem %d", pass); break; }
        }
        CHECK (true, "estabilidade: 20 configuracoes aleatorias de 24 bandas (todos os tipos/Q/ganhos/slopes/placements/dinamicas): sem NaN/Inf");
        std::vector<double> L (65536, 0.0), R (65536, 0.0); r.eq.reset(); for (int blk = 0; blk < 16; ++blk) { r.push(); r.eq.process (L.data() + blk * 4096, R.data() + blk * 4096, 4096, nullptr, nullptr, true); }
        double mx = 0; for (double v : L) mx = std::max (mx, std::fabs (v));
        CHECK (mx < 1e-12 && std::isfinite (mx), "silencio -> silencio (max %.2e) apos estado de ruido", mx);
    }
    // 8) FFT / analisador: precisao do pico e calibracao
    {
        for (int N : { 2048, 4096, 8192 })
        {
            SpectrumCalc sc (N); std::vector<float> x ((size_t) N), db ((size_t) sc.numBins());
            const double f = 440.0;
            for (int i = 0; i < N; ++i) x[(size_t) i] = (float) (0.5 * std::sin (2 * M_PI * f * i / FS));
            sc.compute (x.data(), db.data());
            int pk = 0; for (int k = 1; k < sc.numBins(); ++k) if (db[(size_t) k] > db[(size_t) pk]) pk = k;
            const double binHz = FS / N;
            CHECK (std::fabs (pk * binHz - f) <= binHz, "FFT %d: pico a %.1f Hz (esperado 440 +-%.1f), nota %s", N, pk * binHz, binHz, noteName (pk * binHz).c_str());
            CHECK (std::fabs (db[(size_t) pk] - (-6.02)) < 1.5, "FFT %d: calibracao pico = %.2f dB (seno 0.5 = -6.02 dBFS; scalloping BH <= ~0.8 dB)", N, db[(size_t) pk]);
        }
        CHECK (noteName (440.0) == "A4" && noteName (261.63) == "C4", "nome de nota: 440 Hz = %s, 261.63 Hz = %s", noteName (440).c_str(), noteName (261.63).c_str());
    }
    // 9) FIR: simetria (linear), magnitude vs analitico, Natural != Linear, latencia
    {
        FirDesigner fd; FirKernels k; BandParams bands[kNumBands]; double dyn[kNumBands] = {};
        bands[0].on = true; bands[0].type = BandType::Bell; bands[0].freq = 1000; bands[0].gain = 9; bands[0].q = 2;
        bands[1].on = true; bands[1].type = BandType::HighPass; bands[1].freq = 60; bands[1].slope = 3;
        fd.design (bands, dyn, FS, 0.0, k);
        const int N = k.taps; double asym = 0, pk = 0;
        for (int d = 1; d < N / 2 - N / 8; ++d) { asym = std::max (asym, (double) std::fabs (k.ll[(size_t) (N / 2 + d)] - k.ll[(size_t) (N / 2 - d)])); pk = std::max (pk, (double) std::fabs (k.ll[(size_t) (N / 2 + d)])); }
        CHECK (asym < 1e-6, "FIR linear: kernel simetrico em torno de N/2 (assimetria max %.2e, pico %.3f)", asym, pk);
        CHECK (! k.cross, "FIR: sem Mid/Side -> matriz diagonal");
        // magnitude do FIR em bins vs analitico
        Fft f (N); std::vector<Fft::C> buf ((size_t) N); for (int i = 0; i < N; ++i) buf[(size_t) i] = Fft::C (k.ll[(size_t) i], 0);
        f.forward (buf.data());
        double worst = 0;
        for (double fr : { 100.0, 500.0, 800.0, 1000.0, 1200.0, 2000.0, 6000.0 })
        {
            const int bin = (int) std::lround (fr * N / FS); const double w = 2 * M_PI * bin / N;
            const double ana = magDb (designBand (bands[0].type, 1, FS, 1000, 9, 2), w) + magDb (designBand (BandType::HighPass, 3, FS, 60, 0, 0.7), w);
            const double got = 20 * std::log10 (std::abs (buf[(size_t) bin]));
            worst = std::max (worst, std::fabs (got - ana));
        }
        CHECK (worst < 0.3, "FIR linear: magnitude vs analitico (Bell+HP) max erro = %.3f dB", worst);
        FirKernels kn; fd.design (bands, dyn, FS, 0.5, kn);
        double diff = 0; for (int i = 0; i < N; ++i) diff = std::max (diff, (double) std::fabs (kn.ll[(size_t) i] - k.ll[(size_t) i]));
        CHECK (diff > 1e-4, "Natural (alpha 0.5) difere do Linear: diferenca max de kernel = %.4f", diff);
        CHECK (firLatencyFor (48000) == 4096 && firLatencyFor (96000) == 8192 && firLatencyFor (192000) == 16384, "latencia FIR: 48k=%d 96k=%d 192k=%d amostras", firLatencyFor (48000), firLatencyFor (96000), firLatencyFor (192000));
        // Mid/Side ativa matriz cheia
        bands[2].on = true; bands[2].place = Placement::Mid; bands[2].gain = 6; fd.design (bands, dyn, FS, 0.0, k);
        CHECK (k.cross, "FIR: banda Mid -> matriz cheia (cross = true)");
    }
    // 10) Deresonate: acha uma ressonancia sintetica
    {
        const int bins = 4097; Deresonate d; d.begin (bins, FS);
        std::vector<float> pw ((size_t) bins);
        for (int k = 1; k < bins; ++k) { const double f = k * FS / 8192.0; double a = 1.0 / std::sqrt (f); if (std::fabs (f - 2500.0) < 60) a *= 4.0; if (std::fabs (f - 400.0) < 25) a *= 3.0; pw[(size_t) k] = (float) (a * a * 1e-4); }
        pw[0] = 0;
        for (int i = 0; i < 20; ++i) d.addPower (pw.data(), 0.17);
        auto pr = d.propose();
        bool f2500 = false, f400 = false; for (auto& p : pr) { if (std::fabs (p.freq - 2500) < 150) f2500 = true; if (std::fabs (p.freq - 400) < 60) f400 = true; }
        CHECK (d.ready() && f2500 && f400, "Deresonate: encontrou as ressonancias de 400 Hz e 2.5 kHz (%zu propostas)", pr.size());
        Deresonate d2; d2.begin (bins, FS); d2.addPower (pw.data(), 0.5);
        CHECK (! d2.ready() && d2.propose().empty(), "Deresonate: sem 2 s de captura nao propoe nada");
    }
    // 11) CPU: 24 bandas, 10 s de audio estereo
    {
        Rig r; for (int i = 0; i < kNumBands; ++i) { auto& b = r.b[i]; b.on = true; b.type = (BandType) (i % 3); b.freq = 80.0 * std::pow (1.28, i); b.gain = (i % 2 ? 4 : -4); b.q = 1.5; b.dyn = (i % 2 == 0); b.thr = -40; b.range = -6; b.ratio = 3; b.place = (Placement) (i % 5); }
        const int blocks = (int) (FS * 10 / 512); std::vector<double> L (512), R (512); std::mt19937 rng (5); std::uniform_real_distribution<double> d (-0.3, 0.3);
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < blocks; ++i) { for (auto& v : L) v = d (rng); for (auto& v : R) v = d (rng); r.push(); r.eq.process (L.data(), R.data(), 512, nullptr, nullptr, true); }
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("INFO    CPU: 24 bandas (12 dinamicas, M/S misturados), 10 s de audio a 48k estereo em %.3f s => %.1f %% de 1 nucleo\n", secs, secs / 10.0 * 100.0);
        CHECK (secs < 10.0, "CPU: processa 10 s de audio em tempo real (%.2f s)", secs);
    }
    // 12) texto com unidades (virgula decimal, k/kHz, notas, dB, ms)
    {
        using namespace pq::units;
        CHECK (std::fabs (parseFreq ("1,5k", 0) - 1500) < 1e-9 && std::fabs (parseFreq ("2.2 kHz", 0) - 2200) < 1e-9 && std::fabs (parseFreq ("440", 0) - 440) < 1e-9 && std::fabs (parseFreq ("100 Hz", 0) - 100) < 1e-9, "unidades: 1,5k / 2.2 kHz / 440 / 100 Hz");
        CHECK (std::fabs (parseFreq ("a4", 0) - 440) < 1e-6 && std::fabs (parseFreq ("c#3", 0) - 138.5913) < 1e-3 && std::fabs (parseFreq ("eb2", 0) - 77.7817) < 1e-3, "unidades: notas a4=440, c#3=138.59, eb2=77.78");
        CHECK (std::fabs (parseDb ("-3,5 dB", 0) + 3.5) < 1e-9 && std::fabs (parseMs ("0,5 s", 0) - 500) < 1e-9 && std::fabs (parseMs ("12 ms", 0) - 12) < 1e-9 && std::fabs (parsePlain ("4:1", 0) - 4) < 1e-9, "unidades: -3,5 dB / 0,5 s / 12 ms / 4:1");
        CHECK (parseFreq ("lixo", 123) == 123 && formatFreq (1250) == "1.25 kHz" && formatFreq (440) == "440 Hz", "unidades: entrada invalida mantem o valor; formatacao");
    }
    // ======================================================================= v1.1: novos modulos
    // 13) Sobreamostragem: latencia inteira exata e resposta plana
    {
        for (int f : { 2, 4 })
        {
            Oversampler os; os.prepare (f); std::vector<double> x (2048, 0.0); x[300] = 1.0;
            os.process (x.data(), 2048, [] (double v) { return v; });
            int pk = 0; for (int i = 0; i < 2048; ++i) if (std::fabs (x[(size_t) i]) > std::fabs (x[(size_t) pk])) pk = i;
            CHECK (pk - 300 == Oversampler::latencyFor (f) && x[(size_t) pk] > 0.5, "Oversampler %dx: impulso sai com atraso %d (esperado %d), amplitude %.4f", f, pk - 300, Oversampler::latencyFor (f), x[(size_t) pk]);
        }
        Oversampler os; os.prepare (2); double worst = 0;
        for (double f : { 100.0, 1000.0, 8000.0, 15000.0 })
        {
            os.reset(); std::vector<double> y (16384); for (size_t i = 0; i < y.size(); ++i) y[i] = 0.5 * std::sin (2 * M_PI * f * (double) i / FS);
            os.process (y.data(), 16384, [] (double v) { return v; });
            std::vector<double> t (y.begin() + 4096, y.end()); double s = 0; for (double v : t) s += v * v; worst = std::max (worst, std::fabs (10 * std::log10 (s / t.size() / 0.125)));
        }
        CHECK (worst < 0.05, "Oversampler 2x: resposta plana ate 15 kHz (desvio max %.3f dB)", worst);
    }
    // 14) Character: modos mensuravelmente distintos, aliasing, DC, bypass de dry, ganho de pequeno sinal
    {
        auto harm = [&] (CharMode m, double drive, int os, double f0, double amp, double* h2, double* h3, double* thd, double* dc)
        {
            Character c; c.prepare (FS); c.setup (m, drive, 1.0, os); c.reset();
            const int n = 48000; std::vector<double> L (n), R (n);
            for (int i = 0; i < n; ++i) L[(size_t) i] = R[(size_t) i] = amp * std::sin (2 * M_PI * f0 * i / FS);
            for (int off = 0; off < n; off += 256) c.process (L.data() + off, R.data() + off, std::min (256, n - off));
            const int s0 = 24000, len = 16384; // multiplo exato do periodo nao e necessario: usa DFT por correlacao
            auto mag = [&] (double f) { double re = 0, im = 0; for (int i = 0; i < len; ++i) { const double w = 0.5 - 0.5 * std::cos (2 * M_PI * i / len), p = 2 * M_PI * f * (s0 + i) / FS; re += w * L[(size_t) (s0 + i)] * std::cos (p); im += w * L[(size_t) (s0 + i)] * std::sin (p); } return 2.0 * std::sqrt (re * re + im * im) / (len * 0.5); };
            const double a1 = mag (f0); *h2 = 20 * std::log10 (mag (2 * f0) / a1 + 1e-12); *h3 = 20 * std::log10 (mag (3 * f0) / a1 + 1e-12);
            double q = 0; for (int k = 2; k <= 8; ++k) { const double m = mag (k * f0); q += m * m; } *thd = 100.0 * std::sqrt (q) / a1;
            double s = 0; for (int i = 0; i < len; ++i) s += L[(size_t) (s0 + i)]; *dc = s / len;
        };
        double h2c, h3c, tc, dcc, h2s, h3s, ts, dcs, h2w, h3w, tw, dcw;
        harm (CharMode::Clean, 0.6, 2, 1000, 0.5, &h2c, &h3c, &tc, &dcc);
        harm (CharMode::Subtle, 0.6, 2, 1000, 0.5, &h2s, &h3s, &ts, &dcs);
        harm (CharMode::Warm, 0.6, 2, 1000, 0.5, &h2w, &h3w, &tw, &dcw);
        std::printf ("INFO    Character @1k, -6 dBFS, drive 60%%: Clean H2 %.1f H3 %.1f THD %.2f%% | Subtle H2 %.1f H3 %.1f THD %.2f%% | Warm H2 %.1f H3 %.1f THD %.2f%%\n", h2c, h3c, tc, h2s, h3s, ts, h2w, h3w, tw);
        CHECK (h3c > h2c + 25.0, "Character Clean: so impares (H3 %.1f dB >> H2 %.1f dB)", h3c, h2c);
        CHECK (h2s > h2c + 25.0 && h2s > -60.0, "Character Subtle: 2a harmonica presente (%.1f dB) vs Clean (%.1f dB)", h2s, h2c);
        CHECK (tw > ts && ts > tc, "Character: THD cresce Clean (%.2f%%) < Subtle (%.2f%%) < Warm (%.2f%%)", tc, ts, tw);
        CHECK (std::fabs (dcs) < 2e-3 && std::fabs (dcw) < 2e-3, "Character: DC removido (Subtle %.5f, Warm %.5f)", dcs, dcw);
        // aliasing: seno 15 kHz com drive alto; harmonica 3 (45 kHz) dobra para 3 kHz
        double a1x, a2x, a4x, dummy;
        auto aliasDb = [&] (int os)
        {
            Character c; c.prepare (FS); c.setup (CharMode::Clean, 1.0, 1.0, os); c.reset(); const int n = 48000; std::vector<double> L (n), R (n);
            for (int i = 0; i < n; ++i) L[(size_t) i] = R[(size_t) i] = 0.9 * std::sin (2 * M_PI * 15000.0 * i / FS);
            for (int off = 0; off < n; off += 256) c.process (L.data() + off, R.data() + off, std::min (256, n - off));
            const int s0 = 24000, len = 16384; auto mag = [&] (double f) { double re = 0, im = 0; for (int i = 0; i < len; ++i) { const double w = 0.5 - 0.5 * std::cos (2 * M_PI * i / len), p = 2 * M_PI * f * (s0 + i) / FS; re += w * L[(size_t) (s0 + i)] * std::cos (p); im += w * L[(size_t) (s0 + i)] * std::sin (p); } return 2.0 * std::sqrt (re * re + im * im) / (len * 0.5); };
            return 20 * std::log10 (mag (3000.0) / mag (15000.0) + 1e-12);
        };
        a1x = aliasDb (1); a2x = aliasDb (2); a4x = aliasDb (4); (void) dummy;
        std::printf ("INFO    Aliasing (15 kHz, drive 100%%, alias a 3 kHz re: fundamental): 1x %.1f dB | 2x %.1f dB | 4x %.1f dB\n", a1x, a2x, a4x);
        CHECK (a2x < a1x - 15.0 && a4x < a2x + 1.0, "Character: sobreamostragem reduz o aliasing (1x %.1f, 2x %.1f, 4x %.1f dB)", a1x, a2x, a4x);
        // pequeno sinal ~ unitario, mix 0 = dry atrasado exato
        {
            Character c; c.prepare (FS); c.setup (CharMode::Clean, 0.5, 1.0, 2); c.reset(); const int n = 24000; std::vector<double> L (n), R (n);
            for (int i = 0; i < n; ++i) L[(size_t) i] = R[(size_t) i] = 0.001 * std::sin (2 * M_PI * 1000.0 * i / FS);
            for (int off = 0; off < n; off += 256) c.process (L.data() + off, R.data() + off, std::min (256, n - off));
            double s = 0; for (int i = 12000; i < n; ++i) s += L[(size_t) i] * L[(size_t) i]; const double g = 10 * std::log10 (s / (n - 12000) / (0.5e-6));
            CHECK (std::fabs (g) < 0.1, "Character: ganho de pequeno sinal = %.3f dB (esperado 0)", g);
            Character d; d.prepare (FS); d.setup (CharMode::Warm, 1.0, 0.0, 4); d.reset(); std::vector<double> x (4096, 0.0), y (4096, 0.0); x[100] = 1.0; y = x;
            d.process (x.data(), y.data(), 4096); int pk = 0; for (int i = 0; i < 4096; ++i) if (std::fabs (x[(size_t) i]) > std::fabs (x[(size_t) pk])) pk = i;
            CHECK (pk == 100 + d.latency() && std::fabs (x[(size_t) pk] - 1.0) < 1e-9, "Character: mix 0%% = dry atrasado %d amostras exatas", d.latency());
        }
    }
    // 15) De-harsh: reconstrucao perfeita, latencia exata, atenuacao seletiva, delta, estabilidade
    {
        DeHarsh dh; dh.prepare (FS); DeHarshParams p; p.amount = 0; dh.setParams (p);
        std::mt19937 rng (7); std::normal_distribution<double> nd (0.0, 0.1);
        const int n = 48000; std::vector<double> L (n), R (n), L0, R0; for (int i = 0; i < n; ++i) { L[(size_t) i] = nd (rng); R[(size_t) i] = nd (rng); } L0 = L; R0 = R;
        for (int off = 0; off < n; off += 480) dh.process (L.data() + off, R.data() + off, std::min (480, n - off));
        double err = 0; for (int i = DeHarsh::kLatency + 2048; i < n; ++i) err = std::max (err, std::fabs (L[(size_t) i] - L0[(size_t) (i - DeHarsh::kLatency)]));
        CHECK (err < 1e-9, "De-harsh: reconstrucao STFT perfeita com ganho 1 (erro max %.2e) e latencia exata %d", err, DeHarsh::kLatency);
        // seletividade: ruido + seno 5 kHz forte
        DeHarsh d2; d2.prepare (FS); p.amount = 1.0; p.sensDb = 4; p.loHz = 2500; p.hiHz = 10000; d2.setParams (p);
        const int m = 96000; std::vector<double> a (m), b (m), a0; std::normal_distribution<double> nd2 (0.0, 0.05);
        for (int i = 0; i < m; ++i) { const double t = i / FS; const double s = 0.05 * std::sin (2 * M_PI * 5000.0 * t) + 0.05 * std::sin (2 * M_PI * 300.0 * t); a[(size_t) i] = nd2 (rng) + s; b[(size_t) i] = a[(size_t) i]; }
        a0 = a; for (int off = 0; off < m; off += 512) d2.process (a.data() + off, b.data() + off, std::min (512, m - off));
        auto tone = [&] (const std::vector<double>& x, int shift, double f) { const int s0 = 48000, len = 16384; double re = 0, im = 0; for (int i = 0; i < len; ++i) { const double w = 0.5 - 0.5 * std::cos (2 * M_PI * i / len), ph = 2 * M_PI * f * (s0 + i) / FS; re += w * x[(size_t) (s0 + i + shift)] * std::cos (ph); im += w * x[(size_t) (s0 + i + shift)] * std::sin (ph); } return 20 * std::log10 (2.0 * std::sqrt (re * re + im * im) / (len * 0.5) + 1e-12); };
        const double red5k = tone (a, 0, 5000.0) - tone (a0, -DeHarsh::kLatency, 5000.0), red300 = tone (a, 0, 300.0) - tone (a0, -DeHarsh::kLatency, 300.0);
        std::printf ("INFO    De-harsh: pico 5 kHz %.1f dB, tom 300 Hz (fora da zona) %.2f dB, reducao maxima reportada %.1f dB\n", red5k, red300, d2.lastMaxReductionDb());
        CHECK (red5k < -4.0 && std::fabs (red300) < 0.2, "De-harsh: reduz o pico de 5 kHz (%.1f dB) e deixa os 300 Hz intactos (%.2f dB)", red5k, red300);
        // delta: entrada atrasada = saida wet + delta
        DeHarsh w1, w2; w1.prepare (FS); w2.prepare (FS); p.delta = false; w1.setParams (p); DeHarshParams pd = p; pd.delta = true; w2.setParams (pd);
        std::vector<double> x1 (m), y1, x2, y2; x1 = a0; y1 = a0; x2 = a0; y2 = a0;
        for (int off = 0; off < m; off += 512) { const int mm = std::min (512, m - off); w1.process (x1.data() + off, y1.data() + off, mm); w2.process (x2.data() + off, y2.data() + off, mm); }
        double de = 0; for (int i = 4096; i < m; ++i) de = std::max (de, std::fabs ((x1[(size_t) i] + x2[(size_t) i]) - a0[(size_t) (i - DeHarsh::kLatency)]));
        CHECK (de < 1e-9, "De-harsh: wet + delta = entrada atrasada (erro %.2e)", de);
        bool finite = true; for (double v : a) if (! std::isfinite (v)) finite = false;
        CHECK (finite, "De-harsh: saida finita");
    }
    // 16) LevelBank / De-Ess / Unmask
    {
        std::mt19937 rng (11); std::normal_distribution<double> nd (0.0, 1.0);
        // sinal de voz sintetico: ruido de baixa frequencia constante + rajadas de ruido em 7 kHz
        const int n = (int) (FS * 12); std::vector<float> x ((size_t) n);
        Coefs lp = biquadRBJ (BandType::LowPass, FS, 1500, 0, 0.707), bp = biquadRBJ (BandType::BandPass, FS, 7000, 0, 2.5); BiquadState s1, s2;
        for (int i = 0; i < n; ++i)
        {
            const double t = (double) i / FS; const bool burst = std::fmod (t, 0.5) < 0.08;
            x[(size_t) i] = (float) (0.1 * tick (lp, s1, nd (rng)) + (burst ? 0.5 * tick (bp, s2, nd (rng)) : 0.01 * tick (bp, s2, nd (rng))));
        }
        DeEss de; de.begin (FS); for (int off = 0; off < n; off += 4096) de.feed (x.data() + off, std::min (4096, n - off));
        auto pr = de.propose();
        CHECK (de.ready() && pr.size() == 1 && std::fabs (std::log2 (pr[0].freq / 7000.0)) < 0.3 && pr[0].dynamic && pr[0].gainDb < 0, "De-Ess: encontrou a sibilancia perto de 7 kHz (%.0f Hz, range %.1f dB, thr %.1f dB)", pr.empty() ? 0.0 : pr[0].freq, pr.empty() ? 0.0 : pr[0].gainDb, pr.empty() ? 0.0 : pr[0].thrDb);
        std::vector<float> flat ((size_t) n); for (int i = 0; i < n; ++i) flat[(size_t) i] = (float) (0.05 * nd (rng));
        DeEss d2; d2.begin (FS); for (int off = 0; off < n; off += 4096) d2.feed (flat.data() + off, std::min (4096, n - off));
        CHECK (d2.propose().empty(), "De-Ess: ruido estacionario nao tem sibilancia (0 propostas)");
        // Unmask: principal e sidechain ambos fortes a ~2 kHz
        std::vector<float> a ((size_t) n), s ((size_t) n); Coefs pa = biquadRBJ (BandType::BandPass, FS, 2000, 0, 1.2); Coefs lw = biquadRBJ (BandType::LowPass, FS, 600, 0, 0.7), hg = biquadRBJ (BandType::HighPass, FS, 6000, 0, 0.7);
        BiquadState q1, q2, q3, q4;
        for (int i = 0; i < n; ++i) { const double w = nd (rng), w2 = nd (rng); a[(size_t) i] = (float) (0.02 * tick (lw, q1, w) + 0.1 * tick (pa, q2, w2)); s[(size_t) i] = (float) (0.02 * tick (hg, q3, nd (rng)) + 0.1 * tick (pa, q4, nd (rng))); }
        Unmask um; um.begin (FS); for (int off = 0; off < n; off += 4096) { const int k = std::min (4096, n - off); um.feedMain (a.data() + off, k); um.feedSide (s.data() + off, k); }
        auto up = um.propose();
        bool near2k = false; for (auto& p : up) if (std::fabs (std::log2 (p.freq / 2000.0)) < 0.35 && p.needsSidechain && p.dynamic && p.gainDb < 0) near2k = true;
        bool none600 = true; for (auto& p : up) if (p.freq < 400 || p.freq > 9000) none600 = false;
        CHECK (um.ready() && near2k && none600, "Unmask: conflito detetado a ~2 kHz, sem cortes onde so um dos sinais existe (%zu propostas)", up.size());
        Unmask um2; um2.begin (FS); for (int off = 0; off < n; off += 4096) um2.feedMain (a.data() + off, std::min (4096, n - off));
        CHECK (! um2.ready() && um2.propose().empty(), "Unmask: sem sidechain ligado nao propoe nada");
    }
    // 17) Reference Match: referencia = ruido rosa + Bell +6 dB @3 kHz => propoe ~+6 dB perto de 3 kHz
    {
        std::mt19937 rng (21); std::normal_distribution<double> nd (0.0, 0.1); const int n = (int) (FS * 20);
        std::vector<double> w ((size_t) n), pinkA ((size_t) n); for (auto& v : w) v = nd (rng);
        double b0 = 0, b1 = 0, b2 = 0; for (int i = 0; i < n; ++i) { b0 = 0.99765 * b0 + w[(size_t) i] * 0.0990460; b1 = 0.96300 * b1 + w[(size_t) i] * 0.2965164; b2 = 0.57000 * b2 + w[(size_t) i] * 1.0526913; pinkA[(size_t) i] = b0 + b1 + b2 + w[(size_t) i] * 0.1848; }
        std::vector<double> pinkB ((size_t) n); for (auto& v : w) v = nd (rng); b0 = b1 = b2 = 0;
        for (int i = 0; i < n; ++i) { b0 = 0.99765 * b0 + w[(size_t) i] * 0.0990460; b1 = 0.96300 * b1 + w[(size_t) i] * 0.2965164; b2 = 0.57000 * b2 + w[(size_t) i] * 1.0526913; pinkB[(size_t) i] = b0 + b1 + b2 + w[(size_t) i] * 0.1848; }
        Cascade cas = designBand (BandType::Bell, 1, FS, 3000, 6.0, 1.0); std::vector<BiquadState> st (1); for (int i = 0; i < n; ++i) pinkB[(size_t) i] = tick (cas.c[0], st[0], pinkB[(size_t) i]);
        auto ltas = [&] (const std::vector<double>& s) { RefAnalyzer ra; ra.begin (FS); std::vector<float> f (s.begin(), s.end()); for (int off = 0; off < n; off += 4096) ra.feed (f.data() + off, std::min (4096, n - off)); return ra.logDb(); };
        const auto A = ltas (pinkA), B = ltas (pinkB);
        auto pr = matchPropose (B, A, 1.0, 8, 12.0, FS);
        double g3k = 0; bool found = false; for (auto& p : pr) if (std::fabs (std::log2 (p.freq / 3000.0)) < 0.25) { found = true; g3k = p.gainDb; }
        CHECK (found && std::fabs (g3k - 6.0) < 1.8, "Reference Match: encontra o Bell de +6 dB @3 kHz (proposta %.1f dB; %zu bandas)", g3k, pr.size());
        auto same = matchPropose (A, A, 1.0, 8, 12.0, FS);
        CHECK (same.empty(), "Reference Match: referencia igual ao material => nenhuma banda");
        auto weak = matchPropose (B, A, 0.5, 8, 12.0, FS); double g3w = 0; for (auto& p : weak) if (std::fabs (std::log2 (p.freq / 3000.0)) < 0.25) g3w = p.gainDb;
        CHECK (g3w > 1.5 && g3w < g3k, "Reference Match: forca 50%% reduz a proposta (%.1f dB < %.1f dB)", g3w, g3k);
    }
    // 18) Morph
    {
        BandParams A[kNumBands], B[kNumBands], O[kNumBands];
        A[0].on = true; A[0].type = BandType::Bell; A[0].freq = 200; A[0].gain = -4; A[0].q = 1;
        B[0].on = true; B[0].type = BandType::Bell; B[0].freq = 800; B[0].gain = 4; B[0].q = 4;
        A[1].on = true; A[1].type = BandType::Bell; A[1].freq = 3000; A[1].gain = 6;                       // so em A
        B[2].on = true; B[2].type = BandType::HighPass; B[2].freq = 40;                                    // so em B (corte)
        morphBands (A, B, 0.0, O); const bool is0 = std::fabs (O[0].freq - 200) < 1e-6 && O[0].gain == -4 && O[1].gain == 6 && ! O[2].on;
        morphBands (A, B, 1.0, O); const bool is1 = std::fabs (O[0].freq - 800) < 1e-6 && O[0].gain == 4 && ! O[1].on && O[2].on;
        morphBands (A, B, 0.5, O);
        CHECK (is0 && is1, "Morph: m=0 devolve A e m=1 devolve B");
        CHECK (std::fabs (O[0].freq - 400.0) < 1e-6 && std::fabs (O[0].gain) < 1e-9 && std::fabs (O[0].q - 2.0) < 1e-6 && std::fabs (O[1].gain - 3.0) < 1e-9, "Morph: m=0,5: freq 400 Hz (geometrica), ganho 0, Q 2, banda so-em-A com metade do ganho (%.2f)", O[1].gain);
        double prev = -1e9; bool mono = true; for (int i = 0; i <= 20; ++i) { morphBands (A, B, i / 20.0, O); if (O[0].gain < prev - 1e-12) mono = false; prev = O[0].gain; }
        CHECK (mono, "Morph: ganho evolui monotonicamente de A para B");
    }
    // 19) Mix Unmask: MixMeter, registo e analise (kick / bass / vocals sinteticos)
    {
        MixMeter mm; mm.prepare (FS); int frames = 0; float lastDb[kMixBands] {}; std::vector<float> x (48000);
        for (size_t i = 0; i < x.size(); ++i) x[i] = 0.5f * (float) std::sin (2 * M_PI * 1000.0 * (double) i / FS);
        mm.process (x.data(), x.data(), (int) x.size(), [&] (const float* db, float) { ++frames; std::copy (db, db + kMixBands, lastDb); });
        int bm = 0; for (int b = 0; b < kMixBands; ++b) if (lastDb[b] > lastDb[bm]) bm = b;
        CHECK (frames == 40 && std::fabs (mixBandFreq (bm) - 1000.0) < 120.0 && std::fabs (lastDb[bm] - (-6.0)) < 1.0, "MixMeter: 1 s => %d quadros de 25 ms; seno 1 kHz a -6 dBFS lido na banda %.0f Hz = %.1f dB", frames, mixBandFreq (bm), lastDb[bm]);
        // registo
        auto& reg = MixRegistry::get(); int a = reg.add (&mm), b2 = reg.add (&x);
        reg.setName (a, "Kick"); reg.setPriority (a, 4); reg.setAnalysing (a, true, FS);
        for (int k = 0; k < 10; ++k) { float d[kMixBands]; for (int i = 0; i < kMixBands; ++i) d[i] = -20.f - (float) k; reg.pushFrame (a, d, -10.f); }
        std::vector<std::array<float, kMixBands>> fr; const int n = reg.copyFrames (a, 100, fr);
        auto lst = reg.list (&mm); bool self = false; for (auto& t : lst) if (t.slot == a && t.isSelf && t.name == "Kick" && t.priority == 4 && t.analysing) self = true;
        CHECK (n == 10 && fr.back()[0] == -29.f && fr.front()[0] == -20.f && self, "MixRegistry: nome/prioridade/quadros (%d quadros, ultimo %.0f dB)", n, fr.back()[0]);
        MixMail ml; ml.undoName = "x"; reg.post (b2, ml); MixMail got; const bool had = reg.hasMail (b2) && reg.takeMail (b2, got) && ! reg.hasMail (b2) && got.undoName == "x";
        CHECK (had, "MixRegistry: correio entre instancias");
        reg.remove (a); reg.remove (b2);
    }
    {
        // quadros sinteticos: 12 s (480 quadros). Kick: rajadas de 80 ms em cada 500 ms nas bandas 60-110 Hz. Bass: continuo 40-130 Hz. Vocal: continuo 300 Hz-3 kHz com ondulacao.
        auto bandOf = [] (double f) { int best = 0; for (int b = 0; b < kMixBands; ++b) if (std::fabs (std::log (mixBandFreq (b) / f)) < std::fabs (std::log (mixBandFreq (best) / f))) best = b; return best; };
        const int b60 = bandOf (60), b110 = bandOf (110), b40 = bandOf (80), b130 = bandOf (130), b300 = bandOf (300), b3k = bandOf (3000);
        std::mt19937 rng (3); std::uniform_real_distribution<float> rip (-1.5f, 1.5f);
        MixTrack kick, bass, vox, pad; kick.name = "Kick"; kick.priority = 4; bass.name = "Bass"; bass.priority = 3; vox.name = "Vocals"; vox.priority = 5; pad.name = "Pad"; pad.priority = 2;
        for (int t = 0; t < 480; ++t)
        {
            std::array<float, kMixBands> k, ba, vo, pd; k.fill (-120.f); ba.fill (-120.f); vo.fill (-120.f); pd.fill (-120.f);
            const bool hit = (t % 20) < 3;
            for (int b = b60; b <= b110; ++b) if (hit) k[(size_t) b] = -6.f + rip (rng); else k[(size_t) b] = -75.f;
            for (int b = b40; b <= b130; ++b) ba[(size_t) b] = -9.f + rip (rng);
            for (int b = b300; b <= b3k; ++b) { vo[(size_t) b] = -14.f + rip (rng); pd[(size_t) b] = -15.f + rip (rng); }
            kick.frames.push_back (k); bass.frames.push_back (ba); vox.frames.push_back (vo); pad.frames.push_back (pd);
        }
        auto res = analyseMix ({ kick, bass, vox }, 1.5, 3);
        const MixPair* kb = nullptr; const MixPair* vb = nullptr; for (auto& p : res.pairs) { if (p.protectedIdx == 0 && p.victimIdx == 1) kb = &p; if (p.protectedIdx == 2 && p.victimIdx == 1) vb = &p; }
        CHECK (kb != nullptr && ! kb->conflicts.empty() && kb->conflicts[0].freq > 55 && kb->conflicts[0].freq < 135, "Mix Unmask: Kick protegido vs Bass => conflito em %.0f Hz", kb && ! kb->conflicts.empty() ? kb->conflicts[0].freq : 0.0);
        if (kb && ! kb->conflicts.empty())
        {
            const auto& c = kb->conflicts[0];
            std::printf ("INFO    Kick->Bass @%.0f Hz: corte %.1f dB | EQ %.0f%% (colateral %.0f%%) | Dinamica %.0f%% | Sidechain %.0f%% (colateral %.0f%%) => melhor: %s", c.freq, c.cutDb, 100 * c.eq.score, 100 * c.eq.collateral, 100 * c.dyn.score, 100 * c.sc.score, 100 * c.sc.collateral, c.best == MixMethod::Sidechain ? "SIDECHAIN" : (c.best == MixMethod::Dynamic ? "DINAMICA" : "EQ")); std::printf ("\n");
            CHECK (kb->best == MixMethod::Sidechain && c.sc.score > c.eq.score + 0.2, "Mix Unmask: Kick percussivo vs Bass continuo => melhor metodo = sidechain (SC %.2f vs EQ %.2f)", c.sc.score, c.eq.score);
            CHECK (c.pSc.needsSidechain && c.pSc.dynamic && c.pEq.gainDb < 0 && ! c.pEq.dynamic, "Mix Unmask: propostas geradas (EQ estatico, dinamico, sidechain)");
        }
        CHECK (vb == nullptr || vb->conflicts.empty(), "Mix Unmask: Vocal (300 Hz-3 kHz) e Bass (40-130 Hz) nao se sobrepoem => sem conflitos");
        // dois sinais continuos na mesma zona => EQ estatico basta
        auto res2 = analyseMix ({ vox, pad }, 1.5, 3); bool eqBest = ! res2.pairs.empty() && ! res2.pairs[0].conflicts.empty() && res2.pairs[0].best == MixMethod::Eq;
        CHECK (eqBest, "Mix Unmask: Vocal vs Pad, ambos continuos na mesma zona => melhor metodo = EQ estatico (colateral %.0f%%)", eqBest ? 100 * res2.pairs[0].conflicts[0].eq.collateral : -1.0);
        CHECK (res2.pairs[0].protectedIdx == 0, "Mix Unmask: a pista de maior prioridade (Vocals) e a protegida");
        MixTrack few = kick; few.frames.resize (20); auto res3 = analyseMix ({ few, bass }, 1.5, 3); CHECK (res3.pairs.empty() && ! res3.message.empty(), "Mix Unmask: poucos dados => aviso em vez de resultado");
    }
    // 20) Assistente EQNOVA: MixMeter espacial, Spatial, Mix Doctor, Intent, Consequence
    {
        MixMeter mm; mm.prepare (FS); std::vector<float> L (96000), R (96000);
        for (size_t i = 0; i < L.size(); ++i) { const double t = (double) i / FS; L[i] = (float) (0.4 * std::sin (2 * M_PI * 1000 * t)); R[i] = (float) (0.1 * std::sin (2 * M_PI * 1000 * t)); }
        mm.process (L.data(), R.data(), (int) L.size(), [] (const float*, float) {}); SpatialStats st; mm.stats (st);
        int bm = 0; for (int b = 0; b < kMixBands; ++b) if (std::fabs (mixBandFreq (b) - 1000) < std::fabs (mixBandFreq (bm) - 1000)) bm = b;
        CHECK (std::fabs (st.pan[bm] + 0.6f) < 0.05f && st.corr[bm] > 0.98f && st.corrAll > 0.98f, "SpatialMeter: L=0.4 R=0.1 => pan %.2f (esperado -0.60), correlacao %.2f", (double) st.pan[bm], (double) st.corr[bm]);
        mm.reset(); for (size_t i = 0; i < L.size(); ++i) R[i] = -L[i]; mm.process (L.data(), R.data(), (int) L.size(), [] (const float*, float) {}); mm.stats (st);
        CHECK (st.corrAll < -0.98f && st.monoLossAll < -30.f, "SpatialMeter: sinal em oposicao de fase => corr %.2f, perda mono %.0f dB", (double) st.corrAll, (double) st.monoLossAll);
    }
    {
        auto bandOf = [] (double f) { int best = 0; for (int b = 0; b < kMixBands; ++b) if (std::fabs (std::log (mixBandFreq (b) / f)) < std::fabs (std::log (mixBandFreq (best) / f))) best = b; return best; };
        std::mt19937 rng (11); std::uniform_real_distribution<float> rip (-1.0f, 1.0f);
        auto mk = [&] (const char* n, int pri, double f0, double f1, float lvl, bool burst, float pan, float corr, float crest) {
            MixTrack t; t.name = n; t.priority = pri; const int b0 = bandOf (f0), b1 = bandOf (f1);
            for (int k = 0; k < 480; ++k) { std::array<float, kMixBands> fr; fr.fill (-120.f); const bool hit = (k % 20) < 3; for (int b = b0; b <= b1; ++b) fr[(size_t) b] = burst ? (hit ? lvl : -75.f) : lvl + rip (rng); t.frames.push_back (fr); }
            for (int b = 0; b < kMixBands; ++b) { t.st.pan[b] = pan; t.st.corr[b] = corr; t.st.monoLoss[b] = corr < 0.3f ? -6.f : -0.5f; } t.st.corrAll = corr; t.st.monoLossAll = corr < 0.3f ? -6.f : -0.5f; t.st.rmsDb = lvl - 6; t.st.crestDb = crest; return t; };
        MixTrack kick = mk ("Kick", 4, 60, 110, -6, true, 0, 0.99f, 14), bass = mk ("Bass", 3, 80, 130, -9, false, 0, 0.99f, 5), vox = mk ("Vocals", 5, 300, 3000, -14, false, 0, 0.99f, 9), gtr = mk ("Guitar", 2, 300, 3000, -15, false, 0, 0.99f, 8);
        std::vector<MixTrack> T { kick, bass, vox, gtr }; auto res = analyseMix (T, 1.5, 3); auto sp = analyseSpace (T, res);
        bool crowd = false, pan = false; for (auto& f : sp.findings) if (f.kind == SpaceFinding::CenterCrowd) { crowd = true; if (T[(size_t) f.track].name == "Guitar" && f.text.find ("Pan candidato") != std::string::npos) pan = true; }
        CHECK (crowd && pan, "Spatial: Vocals e Guitar centrados na mesma zona => centro congestionado, pan candidato = Guitar (a de menor prioridade)");
        CHECK (sp.kind.size() == 4 && sp.kind[0] == SrcKind::Mono, "Spatial: tipo de fonte (Kick = %s)", srcKindName (sp.kind[0]));
        MixTrack wide = mk ("Pad", 2, 80, 200, -12, false, 0, 0.05f, 6); std::vector<MixTrack> T2 { wide, vox }; auto r2 = analyseMix (T2, 1.5, 3); auto s2 = analyseSpace (T2, r2);
        bool mono = false; for (auto& f : s2.findings) if (f.kind == SpaceFinding::MonoRisk && ! f.props.empty() && f.props[0].place == 4 && f.props[0].type == 3) mono = true;
        CHECK (mono, "Spatial: graves com correlacao 0.05 => risco mono + proposta HP no Side");
        MixTrack stV = mk ("Synth", 2, 300, 3000, -13, false, 0, 0.6f, 8); std::vector<MixTrack> T3 { vox, stV }; auto r3 = analyseMix (T3, 1.5, 3); auto s3 = analyseSpace (T3, r3);
        bool mid = false; for (auto& f : s3.findings) if (f.kind == SpaceFinding::MidCut && f.props[0].place == 3) mid = true;
        CHECK (mid, "Spatial: vitima estereo (corr 0.6) => proposta de corte so no Mid");
        auto dv = diagnose (Symptom::VocalUnclear, T, res, sp); bool ev = false, hy = false;
        for (auto& e : dv.evidence) if (e.find ("%") != std::string::npos && e.find ("Vocals") != std::string::npos) ev = true; for (auto& h : dv.hyps) if (h.text.find ("Masking") != std::string::npos && h.v == Verdict::Compatible) hy = true;
        CHECK (ev && hy && ! dv.hyps.empty(), "Mix Doctor: vocal pouco inteligivel => evidencia medida + hipotese masking 'compativel com os dados' (%d hipoteses, %d acoes)", (int) dv.hyps.size(), (int) dv.actions.size());
        auto dk = diagnose (Symptom::KickNoImpact, T, res, sp); bool kact = ! dk.actions.empty() && dk.actions[0].track == 1;
        CHECK (kact, "Mix Doctor: kick sem impacto => acao na pista do baixo (concorrente)");
        auto dm = diagnose (Symptom::BassMono, T2, r2, s2); CHECK (! dm.evidence.empty() && ! dm.actions.empty(), "Mix Doctor: graves somem em mono => evidencia e acao (HP no Side)");
        for (int sy = 0; sy < 5; ++sy) { auto d = diagnose ((Symptom) sy, T, res, sp); CHECK (! d.evidence.empty() || ! d.note.empty(), "Mix Doctor: sintoma %d devolve resultado", sy); }
        std::array<double, kMixBands> cur; for (int b = 0; b < kMixBands; ++b) cur[(size_t) b] = res.curve[2][(size_t) b];
        auto pc = planIntent (Intent::Clarity, Role::Voice, 0.5, &cur); double d3k = pc.staticDelta[(size_t) bandOf (3000)], d300 = pc.staticDelta[(size_t) bandOf (300)];
        CHECK (pc.props.size() == 2 && d3k > 0.3 && d300 < -0.3 && pc.measured, "Intent Clarity (Voz): %+.1f dB a 3 kHz, %+.1f dB a 300 Hz (previsao sobre a curva medida)", d3k, d300);
        bool within = true; for (int i = 0; i < 5; ++i) for (double a : { 0.0, 0.5, 1.0 }) for (int r = 0; r < 4; ++r) { auto pl = planIntent ((Intent) i, (Role) r, a, &cur); for (auto& q : pl.props) if (std::fabs (q.gainDb) > 4.0001) within = false; if (pl.props.empty() || pl.explanation.empty()) within = false; }
        CHECK (within, "Intent: 5 intencoes x 4 papeis x 3 intensidades => sempre propostas, explicacao e |ganho| <= 4 dB");
        auto p0 = planIntent (Intent::Clarity, Role::Voice, 0.0, nullptr), p1 = planIntent (Intent::Clarity, Role::Voice, 1.0, nullptr); CHECK (std::fabs (p1.props[0].gainDb) > std::fabs (p0.props[0].gainDb), "Intent: a intensidade escala o ganho (%.1f -> %.1f dB)", p0.props[0].gainDb, p1.props[0].gainDb);
        auto sm = planIntent (Intent::Smooth, Role::General, 0.5, &cur); CHECK (! sm.props.empty() && sm.props[0].dynamic, "Intent Smooth: cortes dinamicos");
        const MixPair* kb = nullptr; for (auto& pr : res.pairs) if (pr.protectedIdx == 0 && pr.victimIdx == 1) kb = &pr;
        CHECK (kb != nullptr, "Consequencias: par Kick->Bass encontrado");
        if (kb)
        {
            auto cr = runConsequences (T[0], T[1], *kb); const auto& A = cr.sc[1]; const auto& B = cr.sc[2]; const auto& C = cr.sc[3];
            std::printf ("INFO    Consequencias Kick->Bass: masking restante A %.0f%% B %.0f%% C %.0f%% | colateral A %.0f%% B %.0f%% C %.0f%% | nivel mix A %.2f B %.2f C %.2f dB\n", A.maskRemainPct, B.maskRemainPct, C.maskRemainPct, A.collateralPct, B.collateralPct, C.collateralPct, A.mixRmsDb, B.mixRmsDb, C.mixRmsDb);
            CHECK (cr.sc.size() == 4 && B.collateralPct < A.collateralPct && B.maskRemainPct < 100 && A.mixRmsDb <= 0 && std::fabs (A.loudComp + A.mixRmsDb) < 1e-9, "Consequencias: sidechain tem menos corte desnecessario que EQ estatico; compensacao de loudness = inverso da variacao");
            CHECK (! C.props.empty() && C.props.size() == 2 * kb->conflicts.size(), "Consequencias: cenario C = EQ 1/3 + sidechain 2/3 (%d propostas)", (int) C.props.size());
        }
    }
    std::printf ("\n%d testes ok, %d falhas\n", passes, fails);
    return fails ? 1 : 0;
}
