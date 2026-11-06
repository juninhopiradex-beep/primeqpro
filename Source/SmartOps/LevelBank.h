// =============================================================================
//  PRIMEQ PRO by Piradex — banco de medidores de nivel por banda (dominio do tempo, determinístico)
//
//  Mede o MESMO que o detetor da banda dinamica ve: sinal -> band-pass RBJ (Q) -> pico |y| por janela de ~5 ms (dBFS).
//  Guarda um histograma por banda (0,5 dB) para percentis (P50/P90...). Usado por De-Ess e Unmask.
//  Assume: material mono (L+R)/2; janelas abaixo de -80 dBFS sao ignoradas (silencio).
// =============================================================================
#pragma once
#include "../DSP/BandDesign.h"
#include <algorithm>
#include <cmath>
#include <array>
#include <vector>

namespace pq
{
class LevelBank
{
public:
    static constexpr double kFloorDb = -80.0, kStepDb = 0.5;
    static constexpr int kHistN = 161;   // -80 .. 0 dB

    void begin (double sampleRate, double fmin, double fmax, int nBands, double q)
    {
        fs = sampleRate; n = nBands; qv = q; win = std::max (64, (int) (0.005 * fs));
        freqs.resize ((size_t) n); coef.resize ((size_t) n); st.assign ((size_t) n, BiquadState()); pk.assign ((size_t) n, 0.0); hist.assign ((size_t) n * kHistN, 0); cnt.assign ((size_t) n, 0); for (auto& hh : histH) hh.assign ((size_t) n * kHistN, 0); for (auto& cc : cntH) cc.assign ((size_t) n, 0);
        for (int b = 0; b < n; ++b)
        {
            freqs[(size_t) b] = n > 1 ? fmin * std::pow (fmax / fmin, (double) b / (n - 1)) : fmin;
            coef[(size_t) b] = biquadRBJ (BandType::BandPass, fs, freqs[(size_t) b], 0.0, qv);
        }
        inWin = 0; samples = 0;
    }
    void feed (const float* x, int len)
    {
        for (int i = 0; i < len; ++i)
        {
            const double v = x[i];
            for (int b = 0; b < n; ++b) { const double y = std::fabs (tick (coef[(size_t) b], st[(size_t) b], v)); if (y > pk[(size_t) b]) pk[(size_t) b] = y; }
            if (++inWin >= win)
            {
                inWin = 0; const int half = (int) (((long) ((double) (samples + i) / std::max (1.0, fs))) & 1L);
                for (int b = 0; b < n; ++b)
                {
                    const double db = 20.0 * std::log10 (std::max (pk[(size_t) b], 1e-9)); pk[(size_t) b] = 0;
                    if (db < kFloorDb) continue;
                    const int h = std::clamp ((int) std::lround ((db - kFloorDb) / kStepDb), 0, kHistN - 1);
                    ++hist[(size_t) b * kHistN + (size_t) h]; ++cnt[(size_t) b]; ++histH[(size_t) half][(size_t) b * kHistN + (size_t) h]; ++cntH[(size_t) half][(size_t) b];
                }
            }
        }
        samples += len;
    }
    int bands() const { return n; }
    double freq (int b) const { return freqs[(size_t) b]; }
    double q() const { return qv; }
    double seconds() const { return fs > 0 ? samples / fs : 0.0; }
    long activeWindows (int b) const { return cnt[(size_t) b]; }
    // metade 0/1 = blocos alternados de 1 s (mesma musica, tempos diferentes): base da fiabilidade "split-half"
    long activeWindowsHalf (int h, int b) const { return cntH[(size_t) h][(size_t) b]; }
    double percentileHalf (int h, int b, double p) const
    {
        const long total = cntH[(size_t) h][(size_t) b]; if (total <= 0) return -120.0;
        const long target = (long) std::ceil (p * (double) total); long acc = 0;
        for (int k = 0; k < kHistN; ++k) { acc += histH[(size_t) h][(size_t) b * kHistN + (size_t) k]; if (acc >= std::max (1L, target)) return kFloorDb + k * kStepDb; }
        return 0.0;
    }
    // percentil p em [0,1] dos niveis de janela ativos (dBFS); -120 se nao ha dados
    double percentile (int b, double p) const
    {
        const long total = cnt[(size_t) b]; if (total <= 0) return -120.0;
        const long target = (long) std::ceil (p * (double) total); long acc = 0;
        for (int h = 0; h < kHistN; ++h) { acc += hist[(size_t) b * kHistN + (size_t) h]; if (acc >= std::max (1L, target)) return kFloorDb + h * kStepDb; }
        return 0.0;
    }
private:
    double fs = 48000, qv = 3; int n = 0, win = 240, inWin = 0; long samples = 0;
    std::vector<double> freqs, pk; std::vector<Coefs> coef; std::vector<BiquadState> st; std::vector<long> hist, cnt; std::array<std::vector<long>, 2> histH, cntH;
};
} // namespace pq
