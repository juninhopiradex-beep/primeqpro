// =============================================================================
//  PrimEQ Pro by Piradex — desenho dos filtros de banda (double) e resposta analitica
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

// ---- Design "matched" (Vicanek): polos por matched-z do prototipo analogico e zeros
// escolhidos para igualar |H| do prototipo em DC, w0 e Nyquist. Elimina o cramping do
// RBJ perto de Nyquist (a 44.1 kHz, bell a 10-18 kHz desviava 4-6 dB). Devolve false
// se o sistema for mal condicionado (o chamador usa RBJ).
inline bool matchedBiquad (const double n[3], const double d[3], double w0, Coefs& out)
{
    using cd = std::complex<double>;
    auto mag2 = [&] (double W)
    {
        const cd s (0.0, W), nn = n[0] * s * s + n[1] * s + n[2], dd = d[0] * s * s + d[1] * s + d[2];
        return std::norm (nn) / std::norm (dd);
    };
    const cd disc = std::sqrt (cd (d[1] * d[1] - 4.0 * d[0] * d[2], 0.0));
    const cd p1 = std::exp (w0 * (-d[1] + disc) / (2.0 * d[0])), p2 = std::exp (w0 * (-d[1] - disc) / (2.0 * d[0]));
    const double a1 = -(p1 + p2).real(), a2 = (p1 * p2).real();
    const double G0 = mag2 (0.0), Gn = mag2 (M_PI / w0), Gc = mag2 (1.0);
    const double B0 = (1 + a1 + a2) * (1 + a1 + a2), B1 = (1 - a1 + a2) * (1 - a1 + a2), B2 = -4.0 * a2;
    const double s2 = std::sin (w0 * 0.5) * std::sin (w0 * 0.5), f0 = 1.0 - s2, f1 = s2, f2 = 4.0 * f0 * f1;
    if (f2 < 1e-9) return false;
    const double A0 = G0 * B0, A1 = Gn * B1;
    const double A2 = (Gc * (B0 * f0 + B1 * f1 + B2 * f2) - A0 * f0 - A1 * f1) / f2;
    const double r0 = std::sqrt (A0), r1 = std::sqrt (A1), W = 0.5 * (r0 + r1), disc2 = W * W + A2;
    if (disc2 < 0.0) return false;
    const double b0 = 0.5 * (W + std::sqrt (disc2)), b1 = 0.5 * (r0 - r1);
    if (b0 < 1e-12) return false;
    const double b2 = -A2 / (4.0 * b0);
    if (! std::isfinite (b0 + b1 + b2 + a1 + a2) || std::fabs (a2) >= 1.0) return false;
    out.b0 = b0; out.b1 = b1; out.b2 = b2; out.a1 = a1; out.a2 = a2;
    return true;
}

inline Coefs biquadMatched (BandType t, double fs, double f, double gainDb, double q)
{
    const Coefs rbj = biquadRBJ (t, fs, f, gainDb, q);
    if (t != BandType::Bell) return rbj;   // shelves: matched-z nao melhorou (medido), ficam RBJ
    f = safeFreq (f, fs); q = clampd (q, kMinQ, kMaxQ);
    const double r = f / fs, lo = 0.015, hi = 0.03;
    if (r < lo || std::fabs (gainDb) < 1e-6) return rbj;
    const double A = std::pow (10.0, clampd (gainDb, -60.0, 60.0) / 40.0), sA = std::sqrt (A);
    double n[3], d[3];
    if (t == BandType::Bell)          { n[0] = 1; n[1] = A / q; n[2] = 1; d[0] = 1; d[1] = 1.0 / (A * q); d[2] = 1; }
    else if (t == BandType::LowShelf) { n[0] = A; n[1] = A * sA / q; n[2] = A * A; d[0] = A; d[1] = sA / q; d[2] = 1; }
    else                              { n[0] = A * A; n[1] = A * sA / q; n[2] = A; d[0] = 1; d[1] = sA / q; d[2] = A; }
    Coefs m;
    if (t == BandType::Bell && gainDb < 0.0)
    {
        // corte = inverso exacto do reforco simetrico (mesmo erro em dB, sem polos largos mal mapeados)
        const double Ab = 1.0 / A;
        const double nb[3] { 1.0, Ab / q, 1.0 }, db[3] { 1.0, 1.0 / (Ab * q), 1.0 };
        Coefs bst;
        if (! matchedBiquad (nb, db, 2.0 * M_PI * r, bst) || bst.b0 < 1e-9) return rbj;
        m.b0 = 1.0 / bst.b0; m.b1 = bst.a1 / bst.b0; m.b2 = bst.a2 / bst.b0;
        m.a1 = bst.b1 / bst.b0; m.a2 = bst.b2 / bst.b0;
        if (std::fabs (m.a2) >= 1.0 || std::fabs (m.a1) >= 1.0 + m.a2) return rbj;   // zeros do reforco fora do circulo
    }
    else if (! matchedBiquad (n, d, 2.0 * M_PI * r, m)) return rbj;
    if (r >= hi) return m;
    const double k = (r - lo) / (hi - lo);   // transicao suave RBJ -> matched
    Coefs c;
    c.b0 = rbj.b0 + k * (m.b0 - rbj.b0); c.b1 = rbj.b1 + k * (m.b1 - rbj.b1); c.b2 = rbj.b2 + k * (m.b2 - rbj.b2);
    c.a1 = rbj.a1 + k * (m.a1 - rbj.a1); c.a2 = rbj.a2 + k * (m.a2 - rbj.a2);
    return c;
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
    k.c[k.n++] = biquadMatched (t, fs, f, typeHasGain (t) ? gainDb : 0.0, q);
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
