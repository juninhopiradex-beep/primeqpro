// =============================================================================
//  PRIMEQ pro by Piradex — Smart Ops: Reference Match (deterministico, SEM aprendizagem automatica)
//
//  1) Espectro medio (LTAS) de uma referencia (WAV/AIFF/FLAC, lido fora da thread de audio) e do material aprendido,
//     ambos pelo MESMO calculo (FFT 8192, Blackman-Harris) -> a normalizacao anula-se na diferenca.
//  2) Ambos passam para uma grelha logaritmica (24 pontos/oitava, 20 Hz–20 kHz) e suavizam-se a +-1/6 de oitava.
//  3) diferenca = ref - entrada; retira-se a mediana (100 Hz–10 kHz): iguala o espectro, NAO o volume.
//  4) ajuste guloso por bandas Bell (ate N): maximo |residuo| -> largura a meia altura -> Q; subtrai-se a resposta analitica.
//  Assumidos: a referencia e o material tem conteudo comparavel; a forma espectral media nao diz nada sobre dinamica,
//  estereo ou arranjo. Forca < 100 % e recomendavel. Nao reproduz o som de outra faixa — aproxima o balanço tonal.
// =============================================================================
#pragma once
#include "Deresonate.h"
#include "../Analysis/SpectrumCalc.h"
#include "../DSP/BandDesign.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace pq
{
struct LogGrid
{
    static constexpr int N = 240, perOct = 24; static constexpr double f0 = 20.0;
    static double freq (int i) { return f0 * std::pow (2.0, (double) i / perOct); }
};

// potencia media por bin (amplitude^2) -> dB na grelha logaritmica
inline std::vector<double> toLogDb (const std::vector<double>& meanPow, double fs)
{
    const int bins = (int) meanPow.size(); std::vector<double> out ((size_t) LogGrid::N, -120.0); if (bins < 3) return out;
    const double binHz = fs / ((bins - 1) * 2.0);
    for (int i = 0; i < LogGrid::N; ++i)
    {
        const double f = LogGrid::freq (i), lo = f * std::pow (2.0, -0.5 / LogGrid::perOct), hi = f * std::pow (2.0, 0.5 / LogGrid::perOct);
        if (f > 0.49 * fs) { out[(size_t) i] = -120.0; continue; }
        const int a = (int) std::ceil (lo / binHz), b = (int) std::floor (hi / binHz);
        if (b >= a && a >= 1) { double s = 0; for (int k = a; k <= std::min (b, bins - 1); ++k) s += meanPow[(size_t) k]; out[(size_t) i] = 10.0 * std::log10 (std::max (1e-18, s / (std::min (b, bins - 1) - a + 1))); }
        else
        {
            const double kf = f / binHz; const int k0 = std::clamp ((int) std::floor (kf), 1, bins - 2); const double t = kf - k0;
            const double d0 = 10.0 * std::log10 (std::max (1e-18, meanPow[(size_t) k0])), d1 = 10.0 * std::log10 (std::max (1e-18, meanPow[(size_t) k0 + 1]));
            out[(size_t) i] = d0 + t * (d1 - d0);
        }
    }
    return out;
}

inline std::vector<double> smoothLog (const std::vector<double>& x, int halfWidth)
{
    std::vector<double> y (x.size()); const int n = (int) x.size();
    for (int i = 0; i < n; ++i) { double s = 0; int c = 0; for (int j = std::max (0, i - halfWidth); j <= std::min (n - 1, i + halfWidth); ++j) if (x[(size_t) j] > -119.0) { s += x[(size_t) j]; ++c; } y[(size_t) i] = c ? s / c : -120.0; }
    return y;
}

// acumulador do LTAS de um ficheiro de referencia (alimentado em blocos mono)
class RefAnalyzer
{
public:
    void begin (double sampleRate) { fs = sampleRate; calc.setSize (8192); acc.assign (4097, 0.0); frames = 0; fifo.clear(); fifo.reserve (16384); pw.assign (4097, 0.f); db.assign (4097, 0.f); }
    void feed (const float* mono, int n)
    {
        fifo.insert (fifo.end(), mono, mono + n);
        size_t pos = 0;
        while (fifo.size() - pos >= 8192)
        {
            calc.compute (fifo.data() + pos, db.data(), pw.data());
            for (size_t k = 0; k < 4097; ++k) acc[k] += pw[k];
            ++frames; pos += 4096;
        }
        fifo.erase (fifo.begin(), fifo.begin() + (long) pos);
    }
    double seconds() const { return frames * 4096.0 / std::max (1.0, fs); }
    int frameCount() const { return frames; }
    std::vector<double> logDb() const { std::vector<double> m (acc); if (frames > 0) for (auto& v : m) v /= frames; return toLogDb (m, fs); }
private:
    double fs = 48000; int frames = 0; SpectrumCalc calc { 8192 }; std::vector<double> acc; std::vector<float> fifo, pw, db;
};

inline double bellQFromOctaves (double bwOct) { return clampd (1.0 / (2.0 * std::sinh (bwOct * std::log (2.0) * 0.5)), 0.4, 8.0); }

inline std::vector<DeresProposal> matchPropose (const std::vector<double>& refDb, const std::vector<double>& inDb, double strength = 0.7,
                                                int maxBands = 8, double maxGainDb = 9.0, double fsDesign = 48000.0)
{
    std::vector<DeresProposal> out;
    if ((int) refDb.size() != LogGrid::N || (int) inDb.size() != LogGrid::N) return out;
    const auto R = smoothLog (refDb, 4), I = smoothLog (inDb, 4);
    std::vector<double> d ((size_t) LogGrid::N, 0.0), med;
    for (int i = 0; i < LogGrid::N; ++i)
    {
        const double f = LogGrid::freq (i);
        if (R[(size_t) i] > -119 && I[(size_t) i] > -119 && f >= 30.0 && f <= 18000.0) d[(size_t) i] = R[(size_t) i] - I[(size_t) i];
        if (f >= 100.0 && f <= 10000.0 && R[(size_t) i] > -119 && I[(size_t) i] > -119) med.push_back (d[(size_t) i]);
    }
    if (med.size() < 40) return out;
    std::nth_element (med.begin(), med.begin() + (long) med.size() / 2, med.end()); const double off = med[med.size() / 2];
    std::vector<double> r ((size_t) LogGrid::N, 0.0);
    for (int i = 0; i < LogGrid::N; ++i)
    {
        const double f = LogGrid::freq (i);
        if (f < 30.0 || f > 18000.0 || R[(size_t) i] <= -119 || I[(size_t) i] <= -119) continue;
        r[(size_t) i] = clampd ((d[(size_t) i] - off) * strength, -maxGainDb, maxGainDb);
    }
    for (int it = 0; it < maxBands; ++it)
    {
        int im = -1; double best = 0;
        for (int i = 0; i < LogGrid::N; ++i) if (std::fabs (r[(size_t) i]) > best) { best = std::fabs (r[(size_t) i]); im = i; }
        if (im < 0 || best < 0.8) break;
        const double sgn = r[(size_t) im] > 0 ? 1.0 : -1.0; int a = im, b = im;
        while (a > 0 && sgn * r[(size_t) a] > 0.5 * best) --a;
        while (b < LogGrid::N - 1 && sgn * r[(size_t) b] > 0.5 * best) ++b;
        const double bw = std::max (1.0 / 3.0, (double) (b - a) / LogGrid::perOct);
        DeresProposal p; p.freq = LogGrid::freq (im); p.q = bellQFromOctaves (bw); p.gainDb = r[(size_t) im]; p.excessDb = best;
        const Cascade c = designBand (BandType::Bell, 1, fsDesign, p.freq, p.gainDb, p.q);
        for (int i = 0; i < LogGrid::N; ++i) { const double f = LogGrid::freq (i); if (f < 0.49 * fsDesign) r[(size_t) i] -= magDb (c, 2.0 * M_PI * f / fsDesign); }
        out.push_back (p);
    }
    std::sort (out.begin(), out.end(), [] (const DeresProposal& x, const DeresProposal& y) { return x.freq < y.freq; });
    return out;
}
} // namespace pq
