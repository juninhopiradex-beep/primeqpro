// =============================================================================
//  PRIMEQ pro by Piradex — desenho dos kernels FIR para os modos Natural e Linear Phase
//
//  Definicao matematica (a mesma resposta de magnitude do modo Zero Latency):
//     Hb^alpha(w) = |Hb(w)| * exp( j * alpha * phi_b(w) )     phi_b = fase DESENROLADA do IIR da banda b
//     alpha = 1   -> Zero Latency (fase minima, o proprio IIR)
//     alpha = 0.5 -> NATURAL: metade da fase minima de cada banda (fase mista, pre-ringing reduzido)
//     alpha = 0   -> LINEAR : fase zero + atraso fixo de N/2 amostras => FIR simetrico (fase linear)
//  Colocacao Stereo/L/R/Mid/Side por banda entra como matriz 2x2 por frequencia; as bandas em serie multiplicam-se.
//  Kernel = IFFT(T) rodado N/2 e com janela de bordas (Tukey 12,5 % por lado) — latencia = N/2.
// =============================================================================
#pragma once
#include "BandDesign.h"
#include "Fft.h"
#include <vector>

namespace pq
{
inline int firTapsFor (double fs) { return fs <= 50000.0 ? 8192 : (fs <= 100000.0 ? 16384 : 32768); }
inline int firLatencyFor (double fs) { return firTapsFor (fs) / 2; }
inline double alphaFor (PhaseMode m) { return m == PhaseMode::Linear ? 0.0 : (m == PhaseMode::Natural ? 0.5 : 1.0); }

struct FirKernels
{
    std::vector<float> ll, rl, lr, rr;   // saida_L = ll*in_L + rl*in_R ; saida_R = lr*in_L + rr*in_R
    bool cross = false;                  // true se alguma banda usa Mid/Side (matriz nao diagonal)
    int taps = 0;
};

class FirDesigner
{
public:
    using C = std::complex<double>;

    // bands: 24 params; dynDb: reducao/aumento dinamico atual por banda (dB)
    void design (const BandParams* bands, const double* dynDb, double fs, double alpha, FirKernels& out)
    {
        const int N = firTapsFor (fs), K = N / 2 + 1;
        if (fft.getSize() != N) fft.setSize (N);
        if ((int) buf.size() != N) buf.assign ((size_t) N, C());
        T.assign ((size_t) K * 4, C());
        for (int k = 0; k < K; ++k) { T[(size_t) k * 4 + 0] = 1; T[(size_t) k * 4 + 3] = 1; }   // [ll rl; lr rr] = identidade
        out.cross = false; out.taps = N;
        H.resize ((size_t) K);

        for (int b = 0; b < kNumBands; ++b)
        {
            const BandParams& p = bands[b];
            if (! p.on) continue;
            const double g = clampd (p.gain + ((p.dyn && typeHasGain (p.type)) ? dynDb[b] : 0.0), -36.0, 36.0);
            const Cascade c = designBand (p.type, p.slope, fs, p.freq, g, p.q);

            // resposta alpha-escalada, fase desenrolada ao longo da grelha
            double prevPh = 0.0; bool first = true;
            for (int k = 0; k < K; ++k)
            {
                const double w = 2.0 * M_PI * k / N;
                double mag = 1.0, ph = 0.0;
                for (int s = 0; s < c.n; ++s)
                {
                    const C h = stageResponse (c.c[s], w);
                    mag *= std::abs (h);
                    double a = std::arg (h);
                    ph += a;
                }
                // desenrolar (a soma dos arg principais pode saltar 2*pi)
                if (! first) { while (ph - prevPh > M_PI) ph -= 2.0 * M_PI; while (ph - prevPh < -M_PI) ph += 2.0 * M_PI; }
                first = false; prevPh = ph;
                H[(size_t) k] = std::polar (mag, alpha * ph);
            }

            const bool ms = (p.place == Placement::Mid || p.place == Placement::Side);
            if (ms) out.cross = true;
            for (int k = 0; k < K; ++k)
            {
                const C h = H[(size_t) k];
                C m[4];   // ll rl lr rr
                switch (p.place)
                {
                    case Placement::Stereo: m[0] = h; m[1] = 0; m[2] = 0; m[3] = h; break;
                    case Placement::Left:   m[0] = h; m[1] = 0; m[2] = 0; m[3] = 1; break;
                    case Placement::Right:  m[0] = 1; m[1] = 0; m[2] = 0; m[3] = h; break;
                    case Placement::Mid:    m[0] = 0.5 * (1.0 + h); m[1] = 0.5 * (h - 1.0); m[2] = m[1]; m[3] = m[0]; break;
                    default:                m[0] = 0.5 * (1.0 + h); m[1] = 0.5 * (1.0 - h); m[2] = m[1]; m[3] = m[0]; break;
                }
                C* t = &T[(size_t) k * 4];
                // T = M * T  (out_L = m0*in_L' + m1*in_R' ...)
                const C a0 = m[0] * t[0] + m[1] * t[2], a1 = m[0] * t[1] + m[1] * t[3];
                const C b0 = m[2] * t[0] + m[3] * t[2], b1 = m[2] * t[1] + m[3] * t[3];
                t[0] = a0; t[1] = a1; t[2] = b0; t[3] = b1;
            }
        }
        // t layout: [0]=ll [1]=rl [2]=lr [3]=rr
        toKernel (0, N, K, out.ll); toKernel (1, N, K, out.rl); toKernel (2, N, K, out.lr); toKernel (3, N, K, out.rr);
    }

private:
    void toKernel (int idx, int N, int K, std::vector<float>& dst)
    {
        for (int k = 0; k < K; ++k) buf[(size_t) k] = T[(size_t) k * 4 + (size_t) idx];
        buf[0] = C (buf[0].real(), 0.0); buf[(size_t) N / 2] = C (buf[(size_t) N / 2].real(), 0.0);
        for (int k = 1; k < N / 2; ++k) buf[(size_t) (N - k)] = std::conj (buf[(size_t) k]);
        fft.inverse (buf.data());
        dst.assign ((size_t) N, 0.f);
        const int taper = N / 8;
        for (int n = 0; n < N; ++n)
        {
            const double v = buf[(size_t) ((n + N / 2) % N)].real();
            double w = 1.0;
            if (n < taper) w = 0.5 - 0.5 * std::cos (M_PI * n / taper);
            else if (n >= N - taper) w = 0.5 - 0.5 * std::cos (M_PI * (N - 1 - n) / taper);
            dst[(size_t) n] = (float) (v * w);
        }
    }
    Fft fft { 8192 }; std::vector<C> buf, T, H;
};
} // namespace pq
