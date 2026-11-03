// =============================================================================
//  PRIMEQ PRO by Piradex — Smart Ops: De-Ess (deterministico, SEM aprendizagem automatica)
//
//  Learn: medir o nivel por janela (5 ms) em 8 bandas 3,5–12 kHz (Q=2,5, igual ao detetor da banda dinamica).
//  Propor: a banda com maior "explosividade" (P95 - P50) e o centro da sibilancia; limiar = percentil escolhido;
//  range = -(reducao necessaria aos P95), razao 4:1, ataque 1 ms, release 60 ms. Aplica-se como UMA banda
//  Bell DINAMICA (ganho estatico 0) — usa o motor dinamico normal, por isso o resultado e editavel.
//  Assumidos: voz/fala com sibilancia clara; material sem rajadas nessa zona devolve "nenhuma sibilancia".
//  Falsos positivos: pratos/hi-hats (rajadas) podem ser tratados como sibilancia.
// =============================================================================
#pragma once
#include "LevelBank.h"
#include "Deresonate.h"
#include <vector>

namespace pq
{
class DeEss
{
public:
    static constexpr int kBands = 8; static constexpr double kQ = 2.5;
    void begin (double fs) { bank.begin (fs, 3500.0, 12000.0, kBands, kQ); }
    void feed (const float* x, int n) { bank.feed (x, n); }
    double seconds() const { return bank.seconds(); }
    bool ready() const { return bank.seconds() >= 4.0; }
    const LevelBank& levels() const { return bank; }

    // thrPercentile: 0.5..0.9 (mais baixo = actua mais cedo); maxCutDb: reducao maxima permitida
    std::vector<DeresProposal> propose (double thrPercentile = 0.75, double maxCutDb = 9.0, double minBurstDb = 6.0) const
    {
        std::vector<DeresProposal> out; if (! ready()) return out;
        // candidatas: bandas "explosivas" (P95 - P50 >= minBurst); a escolhida e a de MAIOR nivel P95 (centro da sibilancia)
        int best = -1; double bestBurst = 0, bestP95 = -1e9;
        for (int b = 0; b < bank.bands(); ++b)
        {
            if (bank.activeWindows (b) < 200) continue;
            const double p95 = bank.percentile (b, 0.95), burst = p95 - bank.percentile (b, 0.50);
            if (burst >= minBurstDb && p95 > bestP95) { bestP95 = p95; bestBurst = burst; best = b; }
        }
        if (best < 0 || bestBurst < minBurstDb) return out;
        const double thr = bank.percentile (best, thrPercentile), p95 = bank.percentile (best, 0.95);
        const double needed = 0.75 * std::max (0.0, p95 - thr);          // ratio 4:1 -> reducao = 0,75 * excesso
        DeresProposal p; p.freq = bank.freq (best); p.q = kQ; p.excessDb = bestBurst;
        p.dynamic = true; p.gainDb = -std::clamp (needed + 1.0, 2.0, maxCutDb); p.thrDb = thr; p.ratio = 4.0; p.attackMs = 1.0; p.releaseMs = 60.0;
        out.push_back (p); return out;
    }
private:
    LevelBank bank;
};
} // namespace pq
