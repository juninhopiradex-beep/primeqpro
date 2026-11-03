// =============================================================================
//  PRIMEQ PRO by Piradex — Character: tres circuitos de saturacao ORIGINAIS (nao modelam hardware especifico)
//
//   Clean : u/sqrt(1+u^2)                  -> simetrico, so harmonicos impares (3a dominante), distorcao muito baixa
//   Subtle: tanh(u+b)-tanh(b), b>0         -> assimetrico, 2a harmonica presente (+ 3a), DC removido
//   Warm  : meia-onda +/- com declives diferentes + passa-baixo de 1a ordem (~9 kHz)
//                                          -> mais 2a harmonica e THD, agudos suavizados
//  Ganho de pequeno sinal = 1 (o Drive so acrescenta nao-linearidade). Sobreamostragem configuravel (1x/2x/4x),
//  bloqueio de DC (HP 1a ordem ~7 Hz), mistura dry/wet com o dry atrasado a latencia exata da sobreamostragem.
// =============================================================================
#pragma once
#include "Oversampler.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace pq
{
enum class CharMode { Off = 0, Clean, Subtle, Warm };

class Character
{
public:
    static constexpr int kMaxBlock = Oversampler::kMaxBlock;

    void prepare (double sampleRate)
    {
        fs = sampleRate; dcR = std::exp (-2.0 * M_PI * 7.0 / fs); lpA = 1.0 - std::exp (-2.0 * M_PI * 9000.0 / (fs * 1.0));
        reset();
    }
    void reset()
    {
        for (int c = 0; c < 2; ++c) { os[c].reset(); dcX[c] = dcY[c] = 0; lpS[c] = 0; dry[c].assign (kDry, 0.0); }
        dryW = 0; driveS = driveT; mixS = mixT;
    }
    // drive01, mix01 em [0,1]; factor 1/2/4 (mudar o factor muda a latencia: o chamador faz reset() e relata a latencia).
    void setup (CharMode m, double drive01, double mix01, int factor)
    {
        mode = m; driveT = std::clamp (drive01, 0.0, 1.0); mixT = std::clamp (mix01, 0.0, 1.0);
        if (factor != os[0].getFactor()) { os[0].setFactor (factor); os[1].setFactor (factor); }
    }
    int latency() const { return os[0].latency(); }
    int factor() const { return os[0].getFactor(); }

    void process (double* L, double* R, int n)
    {
        if (mode == CharMode::Off) return;
        if (dry[0].empty()) reset();
        double* ch[2] = { L, R };
        const int lat = latency();
        for (int off = 0; off < n; off += kMaxBlock)
        {
            const int m = std::min (kMaxBlock, n - off);
            driveS += (driveT - driveS) * 0.5;                                  // suavizacao por bloco
            const double gain = 0.25 + 3.75 * driveS * driveS;                  // ganho de entrada do shaper
            for (int c = 0; c < 2; ++c)
            {
                double* x = ch[c] + off;
                for (int i = 0; i < m; ++i) dry[c][(size_t) ((dryW + i) & (kDry - 1))] = x[i];
                os[c].process (x, m, [&] (double v) { return shape (v, gain); });
                for (int i = 0; i < m; ++i)
                {
                    double y = x[i];
                    if (mode == CharMode::Warm) { lpS[c] += (y - lpS[c]) * lpA; y = lpS[c]; }
                    const double hp = y - dcX[c] + dcR * dcY[c]; dcX[c] = y; dcY[c] = hp; y = hp;
                    const double mv = mixS + (mixT - mixS) * ((i + 1.0) / m);
                    const double d = dry[c][(size_t) ((dryW + i - lat) & (kDry - 1))];
                    x[i] = d + (y - d) * mv;
                }
            }
            mixS = mixT; dryW = (dryW + m) & (kDry - 1);
        }
    }

private:
    // k = ganho de entrada por circuito: Clean 0,5 (muito linear), Subtle 1,0, Warm 1,6 (mais saturacao)
    double shape (double x, double s) const
    {
        switch (mode)
        {
            case CharMode::Clean:  { const double sk = s * 0.5; const double u = x * sk; return (u / std::sqrt (1.0 + u * u)) / sk; }
            case CharMode::Subtle: { const double u = x * s, b = 0.18; const double t = std::tanh (b), g = 1.0 - t * t; return (std::tanh (u + b) - t) / (g * s); }
            case CharMode::Warm:   { const double sk = s * 1.6; const double u = x * sk; const double up = u >= 0 ? std::tanh (u) : std::tanh (0.65 * u) / 0.65; return up / sk; }
            default: return x;
        }
    }
    double fs = 48000, dcR = 0.999, lpA = 0.5, driveS = 0, driveT = 0, mixS = 1, mixT = 1;
    CharMode mode = CharMode::Off; Oversampler os[2];
    static constexpr int kDry = 2048;
    double dcX[2] {}, dcY[2] {}, lpS[2] {}; std::vector<double> dry[2]; int dryW = 0;
};
} // namespace pq
