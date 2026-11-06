// =============================================================================
//  PrimEQ Pro by Piradex — Smart Ops: Unmask (deterministico)
//
//  Fonte do "mascarador": SIDECHAIN EXTERNO do plugin (outra faixa enviada para a entrada de sidechain).
//  A comunicacao entre instancias por metadados NAO esta implementada.
//  Learn: toca-se a faixa principal e o mascarador ao mesmo tempo; mede-se o nivel P50 por banda (1/3 de oitava,
//  80 Hz–12 kHz, Q=3) em ambos. Conflito c(f) = min(a(f), s(f)), onde a, s = niveis menos a media (formas espectrais).
//  Zonas em que AMBOS estao acima da media = disputa. Propoe cortes DINAMICOS na faixa principal, com detetor
//  no sidechain externo (cortam so quando o mascarador esta presente).
//  Assumidos: ambos os sinais tocam em simultaneo; nao compensa diferencas de volume global (so comparam-se formas).
//  A "compensacao de volume" automatica NAO e feita (use Auto Gain / Output).
// =============================================================================
#pragma once
#include "LevelBank.h"
#include "Deresonate.h"
#include <vector>

namespace pq
{
class Unmask
{
public:
    static constexpr int kBands = 28; static constexpr double kQ = 3.0;
    void begin (double fs) { main.begin (fs, 80.0, 12000.0, kBands, kQ); side.begin (fs, 80.0, 12000.0, kBands, kQ); }
    void feedMain (const float* x, int n) { main.feed (x, n); }
    void feedSide (const float* x, int n) { side.feed (x, n); }
    double seconds() const { return std::min (main.seconds(), side.seconds()); }
    bool hasSidechain() const { return side.seconds() > 1.0 && side.activeWindows (kBands / 2) > 50; }
    bool ready() const { return seconds() >= 4.0 && hasSidechain(); }

    std::vector<DeresProposal> propose (double minConflictDb = 2.0, double maxCutDb = 6.0, int maxBands = 4) const
    {
        std::vector<DeresProposal> out; if (! ready()) return out;
        std::vector<double> a ((size_t) kBands), s ((size_t) kBands), c ((size_t) kBands); double ma = 0, ms = 0; int cn = 0;
        for (int b = 0; b < kBands; ++b)
        {
            a[(size_t) b] = main.percentile (b, 0.5); s[(size_t) b] = side.percentile (b, 0.5);
            if (a[(size_t) b] > -75 && s[(size_t) b] > -75) { ma += a[(size_t) b]; ms += s[(size_t) b]; ++cn; }
        }
        if (cn < 6) return out;
        ma /= cn; ms /= cn;
        for (int b = 0; b < kBands; ++b)
            c[(size_t) b] = (a[(size_t) b] > -75 && s[(size_t) b] > -75) ? std::min (a[(size_t) b] - ma, s[(size_t) b] - ms) : -99.0;
        std::vector<int> idx; for (int b = 0; b < kBands; ++b) if (c[(size_t) b] >= minConflictDb) idx.push_back (b);
        std::sort (idx.begin(), idx.end(), [&] (int x, int y) { return c[(size_t) x] > c[(size_t) y]; });
        std::vector<int> pick;
        for (int b : idx)
        {
            bool near = false; for (int o : pick) if (std::abs (o - b) < 5) { near = true; break; }     // >= ~2/3 oitava entre bandas
            bool isMax = true; for (int j = std::max (0, b - 2); j <= std::min (kBands - 1, b + 2); ++j) if (c[(size_t) j] > c[(size_t) b]) isMax = false;
            if (! near && isMax) pick.push_back (b);
            if ((int) pick.size() >= maxBands) break;
        }
        std::sort (pick.begin(), pick.end());
        for (int b : pick)
        {
            DeresProposal p; p.freq = main.freq (b); p.q = kQ; p.excessDb = c[(size_t) b];
            p.dynamic = true; p.needsSidechain = true; p.gainDb = -std::min (maxCutDb, 0.7 * c[(size_t) b] + 1.0);
            p.thrDb = side.percentile (b, 0.5) - 2.0; p.ratio = 3.0; p.attackMs = 5.0; p.releaseMs = 120.0;
            out.push_back (p);
        }
        return out;
    }
    const LevelBank& mainBank() const { return main; }
    const LevelBank& sideBank() const { return side; }
private:
    LevelBank main, side;
};
} // namespace pq
