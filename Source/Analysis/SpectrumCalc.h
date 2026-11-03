// =============================================================================
//  PRIMEQ PRO by Piradex — calculo do espectro (STFT de visualizacao, separado do processamento)
//  Janela Blackman-Harris (4 termos), calibracao: seno de amplitude 1.0 (0 dBFS) => 0 dB no pico.
// =============================================================================
#pragma once
#include "../DSP/Fft.h"
#include <cmath>
#include <string>
#include <vector>

namespace pq
{
class SpectrumCalc
{
public:
    explicit SpectrumCalc (int n = 4096) { setSize (n); }
    void setSize (int n)
    {
        size = n; fft.setSize (n); buf.assign ((size_t) n, Fft::C()); win.resize ((size_t) n);
        double sum = 0;
        for (int i = 0; i < n; ++i)
        {
            const double a = 2.0 * M_PI * i / n;
            win[(size_t) i] = 0.35875 - 0.48829 * std::cos (a) + 0.14128 * std::cos (2 * a) - 0.01168 * std::cos (3 * a);
            sum += win[(size_t) i];
        }
        scale = 2.0 / sum;
    }
    int getSize() const { return size; }
    int numBins() const { return size / 2 + 1; }

    // x: 'size' amostras. outDb: numBins() valores em dBFS (pico de seno). outPow (opcional): potencia linear (amplitude^2) por bin.
    void compute (const float* x, float* outDb, float* outPow = nullptr)
    {
        for (int i = 0; i < size; ++i) buf[(size_t) i] = Fft::C (x[i] * win[(size_t) i], 0.0);
        fft.forward (buf.data());
        for (int k = 0; k <= size / 2; ++k)
        {
            const double a = std::abs (buf[(size_t) k]) * scale * (k == 0 || k == size / 2 ? 0.5 : 1.0);
            outDb[k] = (float) (20.0 * std::log10 (std::max (a, 1e-9)));
            if (outPow) outPow[k] = (float) (a * a);
        }
    }
private:
    int size = 0; Fft fft { 1024 }; std::vector<Fft::C> buf; std::vector<double> win; double scale = 1;
};

inline std::string noteName (double hz, double* centsOut = nullptr)
{
    static const char* n[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    if (hz <= 0) return "-";
    const double midi = 69.0 + 12.0 * std::log2 (hz / 440.0);
    const int r = (int) std::lround (midi);
    if (centsOut) *centsOut = (midi - r) * 100.0;
    return std::string (n[((r % 12) + 12) % 12]) + std::to_string (r / 12 - 1);
}
} // namespace pq
