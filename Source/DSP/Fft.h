// =============================================================================
//  PRIMEQ PRO by Piradex — FFT complexa radix-2 em double (C++ puro)
//  Usada pelo analisador, pelo desenho dos filtros FIR e pelos testes. Nunca na thread de audio sem pre-alocacao.
// =============================================================================
#pragma once
#include <cmath>
#include <complex>
#include <vector>

namespace pq
{
class Fft
{
public:
    using C = std::complex<double>;
    explicit Fft (int sizePow2 = 1024) { setSize (sizePow2); }

    void setSize (int n)
    {
        size = n; tw.resize ((size_t) n / 2); rev.resize ((size_t) n);
        for (int i = 0; i < n / 2; ++i) tw[(size_t) i] = std::polar (1.0, -2.0 * M_PI * i / n);
        int bits = 0; while ((1 << bits) < n) ++bits;
        for (int i = 0; i < n; ++i) { int r = 0; for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b); rev[(size_t) i] = r; }
    }
    int getSize() const { return size; }

    void forward (C* x) const { run (x, false); }
    void inverse (C* x) const { run (x, true); const double s = 1.0 / size; for (int i = 0; i < size; ++i) x[i] *= s; }

private:
    void run (C* x, bool inv) const
    {
        for (int i = 0; i < size; ++i) { const int j = rev[(size_t) i]; if (j > i) std::swap (x[i], x[j]); }
        for (int len = 2; len <= size; len <<= 1)
        {
            const int half = len / 2, step = size / len;
            for (int i = 0; i < size; i += len)
                for (int k = 0; k < half; ++k)
                {
                    C w = tw[(size_t) (k * step)]; if (inv) w = std::conj (w);
                    const C u = x[i + k], v = x[i + k + half] * w;
                    x[i + k] = u + v; x[i + k + half] = u - v;
                }
        }
    }
    int size = 0; std::vector<C> tw; std::vector<int> rev;
};
} // namespace pq
