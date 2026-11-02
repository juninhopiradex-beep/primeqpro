// Testes DSP do PRIMEQ pro (C++ puro, sem JUCE): respostas analiticas vs medidas, identidade, M/S, dinamica,
// sidechain, estabilidade, FFT, FIR (simetria / magnitude), Deresonate e CPU.
#include "../../Source/DSP/EqCore.h"
#include "../../Source/DSP/FirDesign.h"
#include "../../Source/Analysis/SpectrumCalc.h"
#include "../../Source/SmartOps/Deresonate.h"
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
    std::printf ("\n%d testes ok, %d falhas\n", passes, fails);
    return fails ? 1 : 0;
}
