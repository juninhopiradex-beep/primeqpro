// =============================================================================
//  PrimEQ Pro by Piradex — separacao espectral Transiente / Tonal (HPSS por medianas, Fitzgerald 2010)
//  C++ puro, sem alocacoes no processamento. STFT de 2048 com hop 512 (75 %), janela raiz-de-Hann.
//  A magnitude do canal medio e filtrada por mediana no TEMPO (realca o tonal/sustentado) e na FREQUENCIA (realca o transiente);
//  a mascara suave P^b / (P^b + H^b) (b = 1..4 pela "separacao") reparte cada bin. Transiente = Y, Tonal = x - Y, por isso T + S = x exato.
//  Latencia fixa: kLatency = N + 3 hops (3584 amostras): 3 frames futuros para a mediana no tempo + a janela.
//  Truque de eficiencia: L e R entram numa unica FFT complexa (z = L + iR); como a mascara e real e simetrica, Y = M.Z devolve L (real) e R (imag).
// =============================================================================
#pragma once
#include "Fft.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace pq
{
// linha de atraso simples (anel) para alinhar detetores / sidechain com a latencia do separador
class DelayLine
{
public:
    void prepare (int maxDelay) { int n = 1; while (n < maxDelay + 2) n <<= 1; buf.assign ((size_t) n, 0.0); mask = n - 1; w = 0; }
    void reset() { std::fill (buf.begin(), buf.end(), 0.0); w = 0; }
    void process (const double* in, double* out, int m, int d)
    {
        for (int i = 0; i < m; ++i) { buf[(size_t) ((w + i) & mask)] = in[i]; out[i] = buf[(size_t) ((w + i - d) & mask)]; }
        w = (w + m) & mask;
    }
private:
    std::vector<double> buf; int mask = 0, w = 0;
};

class Hpss
{
public:
    static constexpr int N = 2048, H = 512, K = N / 2 + 1, kTimeHalf = 3, kFreqHalf = 8, kRing = 8;
    static constexpr int kLatency = N + 3 * H;

    Hpss() : fft (N)
    {
        win.resize ((size_t) N);
        for (int j = 0; j < N; ++j) win[(size_t) j] = std::sqrt (0.5 * (1.0 - std::cos (2.0 * M_PI * j / N)));
        z.resize ((size_t) N); prepare();
    }
    void prepare()
    {
        for (auto& c : inb) c.assign ((size_t) N, 0.0);
        for (auto& c : oa) c.assign ((size_t) kOa, 0.0);
        for (auto& c : dry) c.assign ((size_t) kOa, 0.0);
        for (auto& f : mag) f.assign ((size_t) K, 0.f);
        for (auto& f : pm) f.assign ((size_t) K, 0.f);
        for (auto& f : zf) f.assign ((size_t) N, Fft::C (0, 0));
        reset();
    }
    void reset()
    {
        for (auto& c : inb) std::fill (c.begin(), c.end(), 0.0);
        for (auto& c : oa) std::fill (c.begin(), c.end(), 0.0);
        for (auto& c : dry) std::fill (c.begin(), c.end(), 0.0);
        for (auto& f : mag) std::fill (f.begin(), f.end(), 0.f);
        for (auto& f : pm) std::fill (f.begin(), f.end(), 0.f);
        for (auto& f : zf) std::fill (f.begin(), f.end(), Fft::C (0, 0));
        t = 0;
    }
    void setSeparation (double sep01) { beta = (float) (1.0 + 3.0 * std::clamp (sep01, 0.0, 1.0)); }

    // l/r -> tl/tr (transiente) e sl/sr (tonal); ambos atrasados kLatency amostras e tl + sl = entrada atrasada.
    void process (const double* l, const double* r, int m, double* tl, double* tr, double* sl, double* sr)
    {
        for (int i = 0; i < m; ++i)
        {
            const size_t ip = (size_t) (t & (N - 1)), dp = (size_t) (t & (kOa - 1));
            inb[0][ip] = l[i]; inb[1][ip] = r[i]; dry[0][dp] = l[i]; dry[1][dp] = r[i];
            ++t;
            if (t >= N && (t % H) == 0) frame();
            const long long n = t - 1 - kLatency;                                   // indice da amostra de saida (final)
            if (n >= 0)
            {
                const size_t op = (size_t) (n & (kOa - 1));
                const double dl = dry[0][op], dr = dry[1][op], yl = oa[0][op], yr = oa[1][op];
                tl[i] = yl; tr[i] = yr; sl[i] = dl - yl; sr[i] = dr - yr; oa[0][op] = oa[1][op] = 0.0;
            }
            else { tl[i] = tr[i] = sl[i] = sr[i] = 0.0; }
        }
    }

private:
    static constexpr int kOa = 8192;
    void frame()
    {
        const long long f = t / H - N / H;                                          // indice do frame que acaba de completar-se
        const int fi = (int) (f & (kRing - 1));
        // z = (L + iR) janelado; FFT
        for (int j = 0; j < N; ++j)
        {
            const size_t p = (size_t) ((t - N + j) & (N - 1));
            z[(size_t) j] = Fft::C (inb[0][p] * win[(size_t) j], inb[1][p] * win[(size_t) j]);
        }
        fft.forward (z.data());
        zf[(size_t) fi] = z;
        auto& mg = mag[(size_t) fi];
        for (int k = 0; k < K; ++k)                                                  // magnitude do canal medio: |XL + XR| / 2
        {
            const Fft::C a = z[(size_t) k], b = std::conj (z[(size_t) ((N - k) & (N - 1))]);
            const Fft::C xl = 0.5 * (a + b), xr = Fft::C (0, -0.5) * (a - b);
            mg[(size_t) k] = (float) (0.5 * std::abs (xl + xr));
        }
        // mediana em frequencia (17 bins) -> realce do transiente
        auto& pf = pm[(size_t) fi];
        for (int k = 0; k < K; ++k)
        {
            float v[2 * kFreqHalf + 1]; int c = 0;
            for (int d = -kFreqHalf; d <= kFreqHalf; ++d) v[c++] = mg[(size_t) std::clamp (k + d, 0, K - 1)];
            std::nth_element (v, v + kFreqHalf, v + c); pf[(size_t) k] = v[kFreqHalf];
        }
        const long long g = f - kTimeHalf;                                           // frame cuja mascara ja pode ser calculada (tem 3 futuros)
        if (g < 0) return;
        const int gi = (int) (g & (kRing - 1));
        const bool b1 = beta < 1.001f, b2 = std::fabs (beta - 2.f) < 0.001f;
        auto& zg = zf[(size_t) gi]; auto& pg = pm[(size_t) gi];
        std::array<float, 2 * kTimeHalf + 1> v;
        for (int k = 0; k < K; ++k)
        {
            for (int d = -kTimeHalf; d <= kTimeHalf; ++d) v[(size_t) (d + kTimeHalf)] = (g + d >= 0) ? mag[(size_t) ((g + d) & (kRing - 1))][(size_t) k] : 0.f;
            std::nth_element (v.begin(), v.begin() + kTimeHalf, v.end());
            const float hm = v[(size_t) kTimeHalf], pp = pg[(size_t) k];
            float mp;
            if (b1) mp = pp / (pp + hm + 1e-12f);
            else if (b2) { const float a = pp * pp, b = hm * hm; mp = a / (a + b + 1e-20f); }
            else { const float a = std::pow (pp, beta), b = std::pow (hm, beta); mp = a / (a + b + 1e-20f); }
            mk[(size_t) k] = mp;
        }
        for (int k = 0; k < N; ++k) z[(size_t) k] = zg[(size_t) k] * (double) mk[(size_t) (k <= N / 2 ? k : N - k)];
        fft.inverse (z.data());
        const long long start = g * H;
        for (int j = 0; j < N; ++j)
        {
            const size_t p = (size_t) ((start + j) & (kOa - 1)); const double w = win[(size_t) j] * 0.5;   // soma de Hann a 75 % = 2
            oa[0][p] += z[(size_t) j].real() * w; oa[1][p] += z[(size_t) j].imag() * w;
        }
    }

    Fft fft; std::vector<double> win; std::vector<Fft::C> z;
    std::array<std::vector<double>, 2> inb, oa, dry;
    std::array<std::vector<float>, kRing> mag, pm; std::array<std::vector<Fft::C>, kRing> zf; std::array<float, K> mk {};
    long long t = 0; float beta = 3.4f;
};
} // namespace pq
