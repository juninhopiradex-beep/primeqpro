// =============================================================================
//  PRIMEQ pro by Piradex — sobreamostragem 1x/2x/4x com meia-banda FIR de fase linear (C++ puro)
//
//  Cada etapa 2x usa um filtro half-band de 49 taps (janela Kaiser, beta=8). Atraso de grupo exato:
//    2x: 24 amostras (base)   4x: 36 amostras (base)   — numeros inteiros, declarados ao anfitriao.
//  Sem alocacao em process(): buffers fixos dimensionados em prepare().
// =============================================================================
#pragma once
#include <array>
#include <cmath>
#include <vector>
#include "../Shared/Params.h"

namespace pq
{
namespace hb
{
constexpr int kTaps = 49, kCentre = 24, kMask = 63;

inline double bessel0 (double x) { double s = 1, t = 1; for (int k = 1; k < 40; ++k) { t *= (x / (2.0 * k)) * (x / (2.0 * k)); s += t; } return s; }

inline const std::array<double, kTaps>& coefs()
{
    static const std::array<double, kTaps> h = [] {
        std::array<double, kTaps> c {}; const double beta = 8.0, i0 = bessel0 (beta);
        for (int n = 0; n < kTaps; ++n)
        {
            const int k = n - kCentre; double v;
            if (k == 0) v = 0.5; else if ((k & 1) == 0) v = 0.0; else v = std::sin (M_PI * k * 0.5) / (M_PI * k);
            const double r = (double) k / kCentre, w = bessel0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0;
            c[(size_t) n] = v * w;
        }
        return c;
    }();
    return h;
}

// 1 -> 2: x(n) entra, duas amostras saem (y0 = par, y1 = impar)
struct Up
{
    std::array<double, 64> h {}; int w = 0;
    void reset() { h.fill (0.0); w = 0; }
    void push (double x, double& y0, double& y1)
    {
        h[(size_t) (w & kMask)] = x; const auto& c = coefs();
        y0 = h[(size_t) ((w - kCentre / 2) & kMask)];                       // 2 * 0.5 * x(n-12)
        double s = 0; for (int j = 1; j < kTaps; j += 2) s += c[(size_t) j] * h[(size_t) ((w - (j - 1) / 2) & kMask)];
        y1 = 2.0 * s; ++w;
    }
};
// 2 -> 1: duas amostras entram (a = mais antiga), sai uma
struct Down
{
    std::array<double, 64> h {}; int w = 0;
    void reset() { h.fill (0.0); w = 0; }
    // a = amostra mais antiga (par), b = a seguinte. A saida e centrada em 'a' (atraso total = 12 amostras base por par up+down)
    double push (double a, double b)
    {
        h[(size_t) (w & kMask)] = a;
        const auto& c = coefs();
        double s = c[(size_t) kCentre] * h[(size_t) ((w - kCentre) & kMask)];
        for (int j = 1; j < kTaps; j += 2) s += c[(size_t) j] * h[(size_t) ((w - j) & kMask)];
        ++w; h[(size_t) (w & kMask)] = b; ++w;
        return s;
    }
};
} // namespace hb

class Oversampler
{
public:
    static constexpr int kMaxBlock = 512;
    // factor: 1, 2 ou 4
    void prepare (int factor) { setFactor (factor); }
    void setFactor (int f)
    {
        factor = (f >= 4) ? 4 : (f >= 2 ? 2 : 1); reset();
    }
    int  getFactor() const { return factor; }
    // latencia inteira em amostras na taxa base
    static int latencyFor (int f) { return f >= 4 ? 36 : (f >= 2 ? 24 : 0); }
    int  latency() const { return latencyFor (factor); }
    void reset() { up1.reset(); up2.reset(); dn1.reset(); dn2.reset(); }

    // processa 'n' (<= kMaxBlock) amostras no lugar; shape(x) e chamado na taxa sobreamostrada
    template <class F> void process (double* x, int n, F&& shape)
    {
        if (factor == 1) { for (int i = 0; i < n; ++i) x[i] = shape (x[i]); return; }
        if (factor == 2)
        {
            for (int i = 0; i < n; ++i) { double a, b; up1.push (x[i], a, b); x[i] = dn1.push (shape (a), shape (b)); }
            return;
        }
        for (int i = 0; i < n; ++i)
        {
            double a, b; up1.push (x[i], a, b); double r[2];
            const double in2[2] = { a, b };
            for (int k = 0; k < 2; ++k) { double c, d; up2.push (in2[k], c, d); r[k] = dn2.push (shape (c), shape (d)); }
            x[i] = dn1.push (r[0], r[1]);
        }
    }
private:
    int factor = 1; hb::Up up1, up2; hb::Down dn1, dn2;
};
} // namespace pq
