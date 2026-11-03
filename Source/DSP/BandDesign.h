// =============================================================================
//  PRIMEQ PRO by Piradex — desenho dos filtros de banda (double) e resposta analitica
//
//  • Peaking / shelves / notch / band-pass / all-pass: formulas RBJ (Audio EQ Cookbook), forma com Q.
//  • HP / LP: cascata de Butterworth (6..96 dB/oct) + modo "Brickwall" FINITO = Butterworth de ordem 32
//    (~192 dB/oct; tem ringing e atraso de grupo elevado junto a fc — nao e um corte ideal no tempo).
//  • A curva do grafico e o desenho FIR usam EXATAMENTE estes coeficientes (response()).
// =============================================================================
#pragma once
#include "../Shared/Params.h"
#include <algorithm>
#include <cmath>
#include <complex>

namespace pq
{
struct Coefs { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
constexpr int kMaxStages = 16;
struct Cascade { int n = 0; Coefs c[kMaxStages]; };

inline double clampd (double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline double dbToLin (double db) { return std::pow (10.0, db / 20.0); }

inline double safeFreq (double f, double fs) { return clampd (f, kMinFreq, std::min (kMaxFreq, 0.49 * fs)); }

inline Coefs biquadRBJ (BandType t, double fs, double f, double gainDb, double q)
{
    f = safeFreq (f, fs); q = clampd (q, kMinQ, kMaxQ);
    const double w0 = 2.0 * M_PI * f / fs, cs = std::cos (w0), sn = std::sin (w0), alpha = sn / (2.0 * q);
    const double A = std::pow (10.0, clampd (gainDb, -60.0, 60.0) / 40.0), sA = std::sqrt (A);
    double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
    switch (t)
    {
        case BandType::Bell:
            b0 = 1 + alpha * A; b1 = -2 * cs; b2 = 1 - alpha * A; a0 = 1 + alpha / A; a1 = -2 * cs; a2 = 1 - alpha / A; break;
        case BandType::LowShelf:
            b0 = A * ((A + 1) - (A - 1) * cs + 2 * sA * alpha); b1 = 2 * A * ((A - 1) - (A + 1) * cs); b2 = A * ((A + 1) - (A - 1) * cs - 2 * sA * alpha);
            a0 = (A + 1) + (A - 1) * cs + 2 * sA * alpha; a1 = -2 * ((A - 1) + (A + 1) * cs); a2 = (A + 1) + (A - 1) * cs - 2 * sA * alpha; break;
        case BandType::HighShelf:
            b0 = A * ((A + 1) + (A - 1) * cs + 2 * sA * alpha); b1 = -2 * A * ((A - 1) + (A + 1) * cs); b2 = A * ((A + 1) + (A - 1) * cs - 2 * sA * alpha);
            a0 = (A + 1) - (A - 1) * cs + 2 * sA * alpha; a1 = 2 * ((A - 1) - (A + 1) * cs); a2 = (A + 1) - (A - 1) * cs - 2 * sA * alpha; break;
        case BandType::LowPass:
            b0 = (1 - cs) / 2; b1 = 1 - cs; b2 = (1 - cs) / 2; a0 = 1 + alpha; a1 = -2 * cs; a2 = 1 - alpha; break;
        case BandType::HighPass:
            b0 = (1 + cs) / 2; b1 = -(1 + cs); b2 = (1 + cs) / 2; a0 = 1 + alpha; a1 = -2 * cs; a2 = 1 - alpha; break;
        case BandType::Notch:
            b0 = 1; b1 = -2 * cs; b2 = 1; a0 = 1 + alpha; a1 = -2 * cs; a2 = 1 - alpha; break;
        case BandType::BandPass:
            b0 = alpha; b1 = 0; b2 = -alpha; a0 = 1 + alpha; a1 = -2 * cs; a2 = 1 - alpha; break;
        case BandType::AllPass:
            b0 = 1 - alpha; b1 = -2 * cs; b2 = 1 + alpha; a0 = 1 + alpha; a1 = -2 * cs; a2 = 1 - alpha; break;
    }
    Coefs c; c.b0 = b0 / a0; c.b1 = b1 / a0; c.b2 = b2 / a0; c.a1 = a1 / a0; c.a2 = a2 / a0; return c;
}

inline Coefs firstOrder (bool highpass, double fs, double f)
{
    f = safeFreq (f, fs);
    const double K = std::tan (M_PI * f / fs), n = 1.0 / (1.0 + K);
    Coefs c; c.a1 = (K - 1.0) * n; c.a2 = 0; c.b2 = 0;
    if (highpass) { c.b0 = n; c.b1 = -n; } else { c.b0 = K * n; c.b1 = K * n; }
    return c;
}

// Desenha a cascata de uma banda. gainDb = ganho efetivo (estatico + dinamico).
inline Cascade designBand (BandType t, int slopeIdx, double fs, double f, double gainDb, double q)
{
    Cascade k;
    if (typeIsCut (t))
    {
        const int order = kSlopeOrder[std::clamp (slopeIdx, 0, kNumSlopes - 1)];
        const bool hp = (t == BandType::HighPass);
        if (order == 2) { k.c[k.n++] = biquadRBJ (t, fs, f, 0.0, q); return k; }   // 12 dB/oct: o Q do utilizador manda
        for (int i = 1; i <= order / 2; ++i)
        {
            const double qi = 1.0 / (2.0 * std::sin ((2.0 * i - 1.0) * M_PI / (2.0 * order)));
            k.c[k.n++] = biquadRBJ (t, fs, f, 0.0, qi);
        }
        if (order & 1) k.c[k.n++] = firstOrder (hp, fs, f);
        return k;
    }
    k.c[k.n++] = biquadRBJ (t, fs, f, typeHasGain (t) ? gainDb : 0.0, q);
    return k;
}

inline std::complex<double> stageResponse (const Coefs& c, double w)
{
    const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
    return (c.b0 + c.b1 * z1 + c.b2 * z2) / (1.0 + c.a1 * z1 + c.a2 * z2);
}
inline std::complex<double> response (const Cascade& k, double w)
{
    std::complex<double> h (1.0, 0.0);
    for (int i = 0; i < k.n; ++i) h *= stageResponse (k.c[i], w);
    return h;
}
inline double magDb (const Cascade& k, double w) { return 20.0 * std::log10 (std::max (1e-12, std::abs (response (k, w)))); }

struct BiquadState { double z1 = 0, z2 = 0; };
inline double tick (const Coefs& c, BiquadState& s, double x)   // forma transposta II
{
    const double y = c.b0 * x + s.z1;
    s.z1 = c.b1 * x - c.a1 * y + s.z2;
    s.z2 = c.b2 * x - c.a2 * y;
    return y;
}
} // namespace pq
