// =============================================================================
//  PrimEQ Pro by Piradex — Smart Ops: Deresonate (motor deterministico, SEM aprendizagem automatica)
//
//  Sequencia: Learn (>= 2 s, acumula o espectro medio) -> Stop -> Preview (curva tracejada) -> Apply -> Revert.
//  Nenhum audio e alterado ate "Apply". Algoritmo: espectro medio de potencia (dB) -> linha de base = envolvente
//  inferior suavizada a 1/2 oitava -> picos locais acima da base por > sensibilidade -> Q a partir da largura a meia altura.
//  Limitacoes assumidas (falsos positivos): notas sustentadas / sons tonais estacionarios (baixo, pads, vozes de coro)
//  sao tratados como ressonancias; material muito curto ou com pouca energia gera propostas pouco fiaveis.
// =============================================================================
#pragma once
#include <array>
#include <string>
#include <algorithm>
#include <cmath>
#include <vector>

namespace pq
{
// Proposta de banda. Estatica por omissao; 'dynamic' = banda dinamica (gainDb passa a ser o RANGE, o ganho estatico fica 0).
struct DeresProposal
{
    double freq = 0, gainDb = 0, q = 4, excessDb = 0;
    bool dynamic = false, needsSidechain = false;
    double thrDb = -30, ratio = 2, attackMs = 10, releaseMs = 100;
    int type = 0, place = 0, slope = -1;          // BandType (0 = Bell) e Placement (0 = Stereo, 3 = Mid, 4 = Side)
    std::string why;                  // explicacao curta mostrada ao utilizador
};

class Deresonate
{
public:
    void begin (int fftBins, double sampleRate)
    {
        bins = fftBins; fs = sampleRate; frames = 0; acc.assign ((size_t) bins, 0.0); hopSeconds = 0; for (auto& a : half) a.assign ((size_t) bins, 0.0); fh[0] = fh[1] = 0;
    }
    void addPower (const float* pw, double secondsCovered)
    {
        const int h = (int) (((long) hopSeconds) & 1L);                           // blocos alternados de 1 s (fiabilidade split-half)
        for (int k = 0; k < bins; ++k) { acc[(size_t) k] += pw[k]; half[(size_t) h][(size_t) k] += pw[k]; }
        ++frames; ++fh[h]; hopSeconds += secondsCovered;
    }
    double learnedSeconds() const { return hopSeconds; }
    // espectro medio de potencia (amplitude^2 por bin) — usado tambem pelo Reference Match
    std::vector<double> meanPower() const { std::vector<double> m (acc); if (frames > 0) for (auto& v : m) v /= frames; return m; }
    int numBins() const { return bins; } double sampleRate() const { return fs; }
    int frameCount() const { return frames; }
    bool ready() const { return frames >= 4 && hopSeconds >= 2.0; }

    std::vector<DeresProposal> propose (double sensitivityDb = 4.0, double maxCutDb = 9.0, int maxBands = 8,
                                        double fmin = 80.0, double fmax = 16000.0) const
    {
        if (! ready()) return {};
        return proposeOn (acc, frames, sensitivityDb, maxCutDb, maxBands, fmin, fmax);
    }
    // metade h (0/1) do material, para medir a estabilidade das propostas
    std::vector<DeresProposal> proposeHalf (int h, double sensitivityDb, double maxCutDb, int maxBands, double fmin = 80.0, double fmax = 16000.0) const
    {
        if (fh[h] < 2) return {};
        return proposeOn (half[(size_t) h], fh[h], sensitivityDb, maxCutDb, maxBands, fmin, fmax);
    }
    int framesHalf (int h) const { return fh[h]; }
    // forma espectral (dB, sem media de 100 Hz-10 kHz) de cada metade, em bins FFT — correlacao entre metades = fiabilidade para o Match
    double halfCorrelation() const
    {
        if (fh[0] < 2 || fh[1] < 2 || bins < 16) return 0.0;
        const int N = (bins - 1) * 2; const double binHz = fs / N; std::vector<double> a, b;
        for (int k = 1; k < bins; ++k)
        {
            const double f = k * binHz; if (f < 100.0 || f > 10000.0) continue;
            a.push_back (10.0 * std::log10 (std::max (1e-18, half[0][(size_t) k] / fh[0]))); b.push_back (10.0 * std::log10 (std::max (1e-18, half[1][(size_t) k] / fh[1])));
        }
        return pearson (a, b);
    }
    static double pearson (const std::vector<double>& a, const std::vector<double>& b)
    {
        const size_t n = std::min (a.size(), b.size()); if (n < 3) return 0.0;
        double ma = 0, mb = 0; for (size_t i = 0; i < n; ++i) { ma += a[i]; mb += b[i]; } ma /= n; mb /= n;
        double sab = 0, saa = 0, sbb = 0; for (size_t i = 0; i < n; ++i) { sab += (a[i] - ma) * (b[i] - mb); saa += (a[i] - ma) * (a[i] - ma); sbb += (b[i] - mb) * (b[i] - mb); }
        return saa > 1e-12 && sbb > 1e-12 ? std::clamp (sab / std::sqrt (saa * sbb), 0.0, 1.0) : 0.0;
    }
private:
    std::vector<DeresProposal> proposeOn (const std::vector<double>& acc, int frames, double sensitivityDb, double maxCutDb, int maxBands, double fmin, double fmax) const
    {
        std::vector<DeresProposal> out;
        const int N = (bins - 1) * 2; const double binHz = fs / N;
        std::vector<double> db ((size_t) bins);
        for (int k = 0; k < bins; ++k) db[(size_t) k] = 10.0 * std::log10 (std::max (1e-18, acc[(size_t) k] / frames));

        // suavizacao em oitava (janela de +-1/4 de oitava) e envolvente inferior
        std::vector<double> base ((size_t) bins);
        for (int pass = 0; pass < 3; ++pass)
        {
            const std::vector<double>& src = pass == 0 ? db : base; std::vector<double> next ((size_t) bins);
            for (int k = 1; k < bins; ++k)
            {
                const double f = k * binHz; const int lo = std::max (1, (int) std::floor (f * 0.84 / binHz)), hi = std::min (bins - 1, (int) std::ceil (f * 1.19 / binHz));
                double s = 0; for (int j = lo; j <= hi; ++j) s += src[(size_t) j];
                next[(size_t) k] = s / (hi - lo + 1);
            }
            next[0] = next[1];
            if (pass == 0) base = next; else for (int k = 0; k < bins; ++k) base[(size_t) k] = std::min (base[(size_t) k], next[(size_t) k]);
        }
        std::vector<double> ex ((size_t) bins);
        for (int k = 0; k < bins; ++k) ex[(size_t) k] = db[(size_t) k] - base[(size_t) k];

        std::vector<DeresProposal> cand;
        for (int k = 2; k < bins - 2; ++k)
        {
            const double f = k * binHz; if (f < fmin || f > fmax) continue;
            if (ex[(size_t) k] < sensitivityDb) continue;
            const int w = std::max (2, (int) std::round (f / 24.0 / binHz));     // vizinhanca ~1/12 de oitava
            bool isMax = true;
            for (int j = std::max (1, k - w); j <= std::min (bins - 1, k + w) && isMax; ++j) if (ex[(size_t) j] > ex[(size_t) k]) isMax = false;
            if (! isMax) continue;
            const double half = ex[(size_t) k] * 0.5;
            int a = k, b = k; while (a > 1 && ex[(size_t) a] > half) --a; while (b < bins - 2 && ex[(size_t) b] > half) ++b;
            const double bw = std::max (binHz, (b - a) * binHz), q = std::clamp (f / bw, 2.0, 20.0);
            DeresProposal p; p.freq = f; p.excessDb = ex[(size_t) k]; p.q = q; p.gainDb = -std::min (maxCutDb, 0.8 * ex[(size_t) k]);
            cand.push_back (p);
        }
        std::sort (cand.begin(), cand.end(), [] (const DeresProposal& x, const DeresProposal& y) { return x.excessDb > y.excessDb; });
        for (const auto& c : cand)
        {
            bool close = false;
            for (const auto& o : out) if (std::fabs (std::log2 (c.freq / o.freq)) < 1.0 / 6.0) { close = true; break; }
            if (! close) out.push_back (c);
            if ((int) out.size() >= maxBands) break;
        }
        std::sort (out.begin(), out.end(), [] (const DeresProposal& x, const DeresProposal& y) { return x.freq < y.freq; });
        return out;
    }
    int bins = 0, frames = 0; double fs = 48000, hopSeconds = 0; std::vector<double> acc; std::array<std::vector<double>, 2> half; int fh[2] = { 0, 0 };
};
} // namespace pq
