// =============================================================================
//  PRIMEQ pro by Piradex — MixMeter: nivel e espaco por banda (28 bandas, 80 Hz–12 kHz, Q=3) a cada 25 ms (C++ puro)
//
//  Corre na thread de audio SO quando o utilizador liga "Analisar esta pista". Por banda mede:
//   * pico do sinal mono (L+R)/2 filtrado (mesma definicao do detetor da banda dinamica) por janela de 25 ms, em dBFS;
//   * energias L, R e L*R (media movel ~1.5 s) => panorama, correlacao e perda em mono por banda.
//  Globais: correlacao L/R, perda em mono, RMS e fator de crista. Sem alocacao em process().
// =============================================================================
#pragma once
#include "BandDesign.h"
#include <array>
#include <cmath>

namespace pq
{
constexpr int kMixBands = 28;
constexpr double kMixFmin = 80.0, kMixFmax = 12000.0, kMixQ = 3.0;
inline double mixBandFreq (int b) { return kMixFmin * std::pow (kMixFmax / kMixFmin, (double) b / (kMixBands - 1)); }

struct SpatialStats
{
    float pan[kMixBands] {};        // -1 (esquerda) .. +1 (direita)
    float corr[kMixBands] {};       // correlacao L/R por banda (-1..1)
    float monoLoss[kMixBands] {};   // dB: nivel da soma mono vs nivel estereo (0 = sem perda, negativo = perde)
    float corrAll = 1.f, monoLossAll = 0.f, rmsDb = -120.f, crestDb = 0.f;
    SpatialStats() { for (int b = 0; b < kMixBands; ++b) { pan[b] = 0.f; corr[b] = 1.f; monoLoss[b] = 0.f; } }
};

class MixMeter
{
public:
    void prepare (double fs)
    {
        win = std::max (64, (int) std::lround (0.025 * fs)); alpha = 1.0 - std::exp (-(double) win / (1.5 * fs));
        for (int b = 0; b < kMixBands; ++b) c[(size_t) b] = biquadRBJ (BandType::BandPass, fs, mixBandFreq (b), 0.0, kMixQ);
        reset();
    }
    void reset()
    {
        for (auto& s : stL) s = BiquadState(); for (auto& s : stR) s = BiquadState();
        pk.fill (0.0); wLL.fill (0.0); wRR.fill (0.0); wLR.fill (0.0); eLL.fill (0.0); eRR.fill (0.0); eLR.fill (0.0);
        allPk = 0.0; inWin = 0; gLL = gRR = gLR = 0.0; eGLL = eGRR = eGLR = 0.0; ePk = 0.0;
    }
    double windowSeconds (double fs) const { return win / fs; }

    // sink (const float* db28, float overallDb) chamado uma vez por janela completa; stats() ja esta atualizado nessa altura
    template <class Sink> void process (const float* L, const float* R, int n, Sink&& sink)
    {
        for (int i = 0; i < n; ++i)
        {
            const double l = (double) L[i], r = (double) (R ? R[i] : L[i]), x = 0.5 * (l + r);
            allPk = std::max (allPk, std::max (std::fabs (l), std::fabs (r))); gLL += l * l; gRR += r * r; gLR += l * r;
            for (int b = 0; b < kMixBands; ++b)
            {
                const double al = tick (c[(size_t) b], stL[(size_t) b], l), ar = tick (c[(size_t) b], stR[(size_t) b], r), y = std::fabs (0.5 * (al + ar));
                if (y > pk[(size_t) b]) pk[(size_t) b] = y; wLL[(size_t) b] += al * al; wRR[(size_t) b] += ar * ar; wLR[(size_t) b] += al * ar;
            }
            (void) x;
            if (++inWin >= win)
            {
                float db[kMixBands]; const double inv = 1.0 / win;
                for (int b = 0; b < kMixBands; ++b)
                {
                    db[b] = (float) (20.0 * std::log10 (std::max (pk[(size_t) b], 1e-9))); pk[(size_t) b] = 0.0;
                    eLL[(size_t) b] += alpha * (wLL[(size_t) b] * inv - eLL[(size_t) b]); eRR[(size_t) b] += alpha * (wRR[(size_t) b] * inv - eRR[(size_t) b]); eLR[(size_t) b] += alpha * (wLR[(size_t) b] * inv - eLR[(size_t) b]);
                    wLL[(size_t) b] = wRR[(size_t) b] = wLR[(size_t) b] = 0.0;
                }
                eGLL += alpha * (gLL * inv - eGLL); eGRR += alpha * (gRR * inv - eGRR); eGLR += alpha * (gLR * inv - eGLR); ePk += alpha * (allPk - ePk);
                gLL = gRR = gLR = 0.0;
                sink (db, (float) (20.0 * std::log10 (std::max (allPk, 1e-9)))); allPk = 0.0; inWin = 0;
            }
        }
    }
    void stats (SpatialStats& s) const
    {
        for (int b = 0; b < kMixBands; ++b)
        {
            const double ll = eLL[(size_t) b], rr = eRR[(size_t) b], lr = eLR[(size_t) b];
            if (ll + rr < 1e-14) { s.pan[b] = 0.f; s.corr[b] = 1.f; s.monoLoss[b] = 0.f; continue; }
            const double a = std::sqrt (std::max (ll, 0.0)), d = std::sqrt (std::max (rr, 0.0));
            s.pan[b] = (float) ((d - a) / (d + a + 1e-30)); s.corr[b] = (float) clampd (lr / (a * d + 1e-30), -1.0, 1.0);
            s.monoLoss[b] = (float) (10.0 * std::log10 (std::max (1e-9, (ll + rr + 2.0 * lr) / 4.0) / std::max (1e-14, (ll + rr) / 2.0)));
        }
        const double a = std::sqrt (std::max (eGLL, 0.0)), d = std::sqrt (std::max (eGRR, 0.0)); const double tot = (eGLL + eGRR) / 2.0;
        s.corrAll = tot < 1e-14 ? 1.f : (float) clampd (eGLR / (a * d + 1e-30), -1.0, 1.0);
        s.monoLossAll = tot < 1e-14 ? 0.f : (float) (10.0 * std::log10 (std::max (1e-9, (eGLL + eGRR + 2.0 * eGLR) / 4.0) / tot));
        s.rmsDb = (float) (10.0 * std::log10 (std::max (tot, 1e-12))); s.crestDb = tot < 1e-14 ? 0.f : (float) (20.0 * std::log10 (std::max (ePk, 1e-9) / std::sqrt (tot)));
    }
private:
    int win = 1200, inWin = 0; double alpha = 0.01;
    std::array<Coefs, kMixBands> c {}; std::array<BiquadState, kMixBands> stL {}, stR {};
    std::array<double, kMixBands> pk {}, wLL {}, wRR {}, wLR {}, eLL {}, eRR {}, eLR {}; double allPk = 0, gLL = 0, gRR = 0, gLR = 0, eGLL = 0, eGRR = 0, eGLR = 0, ePk = 0;
};
} // namespace pq
