// =============================================================================
//  PRIMEQ pro by Piradex — De-harsh: atenuacao espectral de picos locais (STFT + overlap-add)
//
//  • STFT N=2048, hop 512 (75 % de sobreposicao), janela sqrt-Hann na analise e na sintese: reconstrucao perfeita
//    quando o ganho e 1 (testado: erro < 1e-9). Latencia fixa e declarada: 2048 amostras.
//  • Deteccao: espectro de potencia conjunto L+R (mascara partilhada => imagem estereo estavel). Linha de base =
//    media em dB numa janela de +-1/3 de oitava. Excesso e[k] = P_dB - base_dB, so dentro de [lo, hi] (bordas suaves).
//  • Reducao por bin (dB) = -amount * clamp(e - sens, 0, maxRed), suavizada em frequencia (3 taps) e no tempo
//    (attack/release por quadro). Fase intocada (ganho real por bin).
//  • "Delta": ouve apenas o que foi removido (entrada atrasada - saida).
//  Sem alocacao em process(); o trabalho pesado (4 FFT por hop) corre no callback com buffers pre-alocados.
// =============================================================================
#pragma once
#include "Fft.h"
#include "../Shared/Params.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace pq
{
struct DeHarshParams
{
    double amount = 0.5;      // 0..1
    double loHz = 2500.0, hiHz = 10000.0;
    double sensDb = 4.0;      // tolerancia acima da linha de base
    double attackMs = 8.0, releaseMs = 80.0;
    double maxRedDb = 12.0;
    bool   delta = false;
};

class DeHarsh
{
public:
    static constexpr int N = 2048, H = 512, kLatency = 2048, kBins = N / 2 + 1, kRing = 4096;

    void prepare (double sampleRate)
    {
        fs = sampleRate; fft.setSize (N);
        win.resize (N); for (int i = 0; i < N; ++i) win[(size_t) i] = std::sqrt (0.5 - 0.5 * std::cos (2.0 * M_PI * i / N));   // sqrt-Hann periodica
        for (int c = 0; c < 2; ++c) { inR[c].assign (kRing, 0.0); outR[c].assign (kRing, 0.0); spec[c].assign (N, Fft::C()); }
        pw.assign (kBins, 0.0); pdb.assign (kBins, 0.0); pre.assign ((size_t) kBins + 1, 0.0); gr.assign (kBins, 0.0); grS.assign (kBins, 0.0); grT.assign (kBins, 0.0);
        reset();
    }
    void reset()
    {
        for (int c = 0; c < 2; ++c) { std::fill (inR[c].begin(), inR[c].end(), 0.0); std::fill (outR[c].begin(), outR[c].end(), 0.0); }
        std::fill (grS.begin(), grS.end(), 0.0); w = 0; hopCount = 0;
    }
    void setParams (const DeHarshParams& p) { prm = p; }

    // L/R no lugar. Atraso exato: kLatency amostras.
    void process (double* L, double* R, int n)
    {
        double* ch[2] = { L, R };
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < 2; ++c) inR[c][(size_t) (w & (kRing - 1))] = ch[c][i];
            ++w;
            if (++hopCount == H) { hopCount = 0; frame(); }
            const int rd = w - 1 - kLatency;   // posicao (cronologica) da amostra de saida neste instante
            for (int c = 0; c < 2; ++c)
            {
                double& o = outR[c][(size_t) (rd & (kRing - 1))];
                const double wet = o; o = 0.0;
                ch[c][i] = prm.delta ? (inR[c][(size_t) (rd & (kRing - 1))] - wet) : wet;
            }
        }
    }
    // ultima reducao maxima (dB, <= 0) — telemetria honesta para a GUI
    double lastMaxReductionDb() const { return lastMaxRed; }

private:
    void frame()
    {
        const double binHz = fs / N;
        const double lo = std::max (60.0, prm.loHz), hi = std::min (0.45 * fs, std::max (lo * 1.1, prm.hiHz));
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < N; ++i) spec[c][(size_t) i] = Fft::C (inR[c][(size_t) ((w - N + i) & (kRing - 1))] * win[(size_t) i], 0.0);
        fft.forward (spec[0].data()); fft.forward (spec[1].data());
        for (int k = 0; k < kBins; ++k) { pw[(size_t) k] = std::norm (spec[0][(size_t) k]) + std::norm (spec[1][(size_t) k]); pdb[(size_t) k] = 10.0 * std::log10 (pw[(size_t) k] + 1e-12); }
        pre[0] = 0; for (int k = 0; k < kBins; ++k) pre[(size_t) k + 1] = pre[(size_t) k] + pdb[(size_t) k];

        const double aC = 1.0 - std::exp (-(double) H / (fs * std::max (0.5, prm.attackMs) * 0.001));
        const double rC = 1.0 - std::exp (-(double) H / (fs * std::max (1.0, prm.releaseMs) * 0.001));
        const double edge = 0.25;   // oitavas de transicao nas bordas
        std::fill (gr.begin(), gr.end(), 0.0);
        const int kLo = std::max (2, (int) (lo / binHz * std::pow (2.0, -edge))), kHi = std::min (kBins - 2, (int) (hi / binHz * std::pow (2.0, edge)));
        for (int k = kLo; k <= kHi; ++k)
        {
            const double f = k * binHz;
            const int a = std::max (1, (int) std::floor (k * 0.794)), b = std::min (kBins - 1, (int) std::ceil (k * 1.26));   // +-1/3 oitava
            const double base = (pre[(size_t) b + 1] - pre[(size_t) a]) / (b - a + 1);
            const double e = pdb[(size_t) k] - base;
            double wgt = 1.0;
            if (f < lo) wgt = 0.5 - 0.5 * std::cos (M_PI * clampd (std::log2 (f / (lo * std::pow (2.0, -edge))) / edge, 0.0, 1.0));
            else if (f > hi) wgt = 0.5 - 0.5 * std::cos (M_PI * clampd (std::log2 ((hi * std::pow (2.0, edge)) / f) / edge, 0.0, 1.0));
            gr[(size_t) k] = -wgt * prm.amount * clampd (e - prm.sensDb, 0.0, prm.maxRedDb);
        }
        // suavizacao em frequencia (preserva o minimo: nunca reduz mais do que o pedido localmente)
        for (int k = 1; k < kBins - 1; ++k) grT[(size_t) k] = 0.25 * gr[(size_t) k - 1] + 0.5 * gr[(size_t) k] + 0.25 * gr[(size_t) k + 1];
        grT[0] = gr[0]; grT[(size_t) kBins - 1] = gr[(size_t) kBins - 1];
        double mn = 0;
        for (int k = 0; k < kBins; ++k)
        {
            const double t = grT[(size_t) k]; double& s = grS[(size_t) k];
            s += (t - s) * (t < s ? aC : rC); mn = std::min (mn, s);
        }
        lastMaxRed = mn;
        for (int c = 0; c < 2; ++c)
        {
            for (int k = 0; k < kBins; ++k)
            {
                const double g = std::pow (10.0, grS[(size_t) k] / 20.0);
                spec[c][(size_t) k] *= g; if (k > 0 && k < N / 2) spec[c][(size_t) (N - k)] *= g;
            }
            fft.inverse (spec[c].data());
            for (int i = 0; i < N; ++i) outR[c][(size_t) ((w - N + i) & (kRing - 1))] += spec[c][(size_t) i].real() * win[(size_t) i] * 0.5;   // sum(hann) = 2 para hop N/4
        }
    }
    double fs = 48000, lastMaxRed = 0; Fft fft { 2048 }; DeHarshParams prm;
    std::vector<double> win, pw, pdb, pre, gr, grS, grT, inR[2], outR[2]; std::vector<Fft::C> spec[2];
    int w = 0, hopCount = 0;
};
} // namespace pq
