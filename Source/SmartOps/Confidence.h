// =============================================================================
//  PrimEQ Pro by Piradex — Smart Ops: confianca MEDIDA das propostas (deterministica, sem IA)
//
//  O material aprendido e dividido em blocos alternados de 1 s (metade A = segundos pares, metade B = impares): mesma musica, momentos
//  diferentes. A proposta e recalculada em cada metade e compara-se (fiabilidade split-half). Score 0..1 =
//      sqrt(material) * (0,6 * estabilidade + 0,4 * proeminencia)
//   material     = segundos aprendidos / segundos necessarios para a operacao (limitado a 1)
//   estabilidade = Deres: fracao de picos propostos que reaparecem (a 1/6 de oitava) nas DUAS metades;
//                  De-ess / Unmask: correlacao entre os vetores de "explosividade" / "conflito" das duas metades; Match: correlacao entre as formas espectrais
//   proeminencia = quao acima do limiar de atuacao esta o achado (limitada a 1)
//  Niveis: >= 0,65 alta, >= 0,40 media, abaixo baixa. Nao prova que o corte soa melhor: mede se o achado e repetivel e claro.
// =============================================================================
#pragma once
#include "Deresonate.h"
#include "DeEss.h"
#include "Unmask.h"
#include <cmath>

namespace pq
{
struct Confidence
{
    double score = 0, stability = -1, seconds = 0, prominenceDb = 0; int agree = -1, total = 0; bool any = false;
    int level() const { return score >= 0.65 ? 2 : (score >= 0.40 ? 1 : 0); }
};
namespace conf
{
inline double finish (Confidence& c, double need, double prom01)
{
    const double material = std::clamp (c.seconds / need, 0.0, 1.0);
    c.score = c.any ? std::sqrt (material) * (0.6 * std::clamp (c.stability, 0.0, 1.0) + 0.4 * std::clamp (prom01, 0.0, 1.0)) : 0.0;
    return c.score;
}
inline Confidence deres (const Deresonate& d, double sens, double maxCut, int maxBands, double fmin = 80.0, double fmax = 16000.0)
{
    Confidence c; c.seconds = d.learnedSeconds(); const auto full = d.propose (sens, maxCut, maxBands, fmin, fmax); c.any = ! full.empty(); if (! c.any) return c;
    const auto a = d.proposeHalf (0, sens, maxCut, maxBands, fmin, fmax), b = d.proposeHalf (1, sens, maxCut, maxBands, fmin, fmax);
    auto has = [] (const std::vector<DeresProposal>& v, double f) { for (const auto& p : v) if (std::fabs (std::log2 (p.freq / f)) < 1.0 / 6.0) return true; return false; };
    int ag = 0; double ex = 0; for (const auto& p : full) { if (has (a, p.freq) && has (b, p.freq)) ++ag; ex += p.excessDb; }
    c.agree = ag; c.total = (int) full.size(); c.stability = (d.framesHalf (0) >= 2 && d.framesHalf (1) >= 2) ? (double) ag / (double) full.size() : 0.0;
    c.prominenceDb = ex / (double) full.size(); finish (c, 10.0, c.prominenceDb / (2.0 * std::max (0.5, sens))); return c;
}
inline Confidence deess (const DeEss& e, double minBurstDb = 6.0)
{
    Confidence c; const auto& bk = e.levels(); c.seconds = e.seconds(); std::vector<double> A, B; double best = 0;
    for (int b = 0; b < bk.bands(); ++b)
    {
        const double burst = bk.percentile (b, 0.95) - bk.percentile (b, 0.50); if (bk.activeWindows (b) >= 200) best = std::max (best, burst);
        auto bh = [&] (int h) { return bk.activeWindowsHalf (h, b) >= 60 ? bk.percentileHalf (h, b, 0.95) - bk.percentileHalf (h, b, 0.50) : 0.0; };
        A.push_back (bh (0)); B.push_back (bh (1));
    }
    c.any = best >= minBurstDb; c.stability = Deresonate::pearson (A, B); c.prominenceDb = best; finish (c, 8.0, best / (2.0 * minBurstDb)); return c;
}
inline Confidence unmask (const Unmask& u, double minConflictDb = 2.0)
{
    Confidence c; c.seconds = u.seconds(); const auto& m = u.mainBank(); const auto& s = u.sideBank(); const int n = m.bands();
    auto vec = [&] (int h)
    {
        std::vector<double> a ((size_t) n), sd ((size_t) n), cf ((size_t) n, -99.0); double ma = 0, ms = 0; int cn = 0;
        for (int b = 0; b < n; ++b) { a[(size_t) b] = h < 0 ? m.percentile (b, 0.5) : m.percentileHalf (h, b, 0.5); sd[(size_t) b] = h < 0 ? s.percentile (b, 0.5) : s.percentileHalf (h, b, 0.5); if (a[(size_t) b] > -75 && sd[(size_t) b] > -75) { ma += a[(size_t) b]; ms += sd[(size_t) b]; ++cn; } }
        if (cn < 6) return cf; ma /= cn; ms /= cn;
        for (int b = 0; b < n; ++b) if (a[(size_t) b] > -75 && sd[(size_t) b] > -75) cf[(size_t) b] = std::min (a[(size_t) b] - ma, sd[(size_t) b] - ms);
        return cf;
    };
    const auto full = vec (-1), A = vec (0), B = vec (1); double best = 0; for (double v : full) best = std::max (best, v);
    c.any = best >= minConflictDb; c.stability = Deresonate::pearson (A, B); c.prominenceDb = best; finish (c, 12.0, best / (2.0 * minConflictDb)); return c;
}
inline Confidence match (const Deresonate& d, const std::vector<DeresProposal>& props)
{
    Confidence c; c.seconds = d.learnedSeconds(); c.any = ! props.empty(); if (! c.any) return c;
    double g = 0; for (const auto& p : props) g += std::fabs (p.gainDb); g /= (double) props.size();
    c.stability = d.halfCorrelation(); c.prominenceDb = g; finish (c, 20.0, g / 3.0); return c;
}
} // namespace conf
} // namespace pq
