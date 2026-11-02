// =============================================================================
//  PRIMEQ pro by Piradex — MixMeter: nivel por banda (28 bandas, 80 Hz–12 kHz, Q=3) a cada 25 ms (C++ puro)
//
//  Corre na thread de audio SO quando o utilizador liga "Analisar esta pista". Mede o pico |y| do sinal mono (L+R)/2
//  filtrado por band-pass (mesma definicao do detetor da banda dinamica) por janela de 25 ms, em dBFS.
//  Sem alocacao em process().
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

class MixMeter
{
public:
    void prepare (double fs)
    {
        win = std::max (64, (int) std::lround (0.025 * fs));
        for (int b = 0; b < kMixBands; ++b) c[(size_t) b] = biquadRBJ (BandType::BandPass, fs, mixBandFreq (b), 0.0, kMixQ);
        reset();
    }
    void reset() { for (auto& s : st) s = BiquadState(); pk.fill (0.0); allPk = 0.0; inWin = 0; }
    double windowSeconds (double fs) const { return win / fs; }

    // sink (const float* db28, float overallDb) chamado uma vez por janela completa
    template <class Sink> void process (const float* L, const float* R, int n, Sink&& sink)
    {
        for (int i = 0; i < n; ++i)
        {
            const double x = 0.5 * ((double) L[i] + (double) (R ? R[i] : L[i]));
            allPk = std::max (allPk, std::fabs (x));
            for (int b = 0; b < kMixBands; ++b) { const double y = std::fabs (tick (c[(size_t) b], st[(size_t) b], x)); if (y > pk[(size_t) b]) pk[(size_t) b] = y; }
            if (++inWin >= win)
            {
                float db[kMixBands]; for (int b = 0; b < kMixBands; ++b) { db[b] = (float) (20.0 * std::log10 (std::max (pk[(size_t) b], 1e-9))); pk[(size_t) b] = 0.0; }
                sink (db, (float) (20.0 * std::log10 (std::max (allPk, 1e-9)))); allPk = 0.0; inWin = 0;
            }
        }
    }
private:
    int win = 1200, inWin = 0; std::array<Coefs, kMixBands> c {}; std::array<BiquadState, kMixBands> st {}; std::array<double, kMixBands> pk {}; double allPk = 0;
};
} // namespace pq
