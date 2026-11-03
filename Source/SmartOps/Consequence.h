// =============================================================================
//  PRIMEQ PRO by Piradex — Mix Consequence Engine (V1): 3 cenarios por decisao, com impactos MENSURAVEIS
//
//  Par (P protegida, V cede) e os seus conflitos. Os cenarios sao simulados sobre os MESMOS quadros de nivel por banda (25 ms):
//    A = corte estatico em V (EQ)           B = corte coordenado: so quando P toca (sidechain)
//    C = combinacao de pequenas mudancas: 1/3 EQ estatico + 2/3 sidechain + 0,5 dB de nivel em V
//  Metricas: masking restante, variacao de nivel RMS (pista e soma), maior alteracao tonal media, pico e dinamica da banda,
//  corte desnecessario e compensacao de loudness para A/B justo. NAO e previsao de qualidade sonora: a preferencia exige escuta.
//  Limitacoes: usa niveis por banda (nao forma de onda), logo nao ve crest factor de banda larga nem fase/latencia.
// =============================================================================
#pragma once
#include "IntentEq.h"

namespace pq
{
struct ScenarioMetrics
{
    std::string name; double maskRemainPct = 100, victimRmsDb = 0, mixRmsDb = 0, maxTonalDb = 0, peakDb = 0, dynDb = 0, collateralPct = 0, loudComp = 0;
    std::vector<DeresProposal> props;
};
struct ConsequenceResult { std::vector<ScenarioMetrics> sc; std::string message; int frames = 0; };

inline ConsequenceResult runConsequences (const MixTrack& P, const MixTrack& V, const MixPair& pair)
{
    ConsequenceResult R; if (pair.conflicts.empty()) { R.message = "Sem conflitos neste par."; return R; }
    const int K = (int) std::min (P.frames.size(), V.frames.size()); if (K < 80) { R.message = "Poucos dados (menos de 2 s)."; return R; }
    R.frames = K; const int op = (int) P.frames.size() - K, ov = (int) V.frames.size() - K; const double kAct = -65.0, kMargin = 6.0;
    struct Resp { std::array<double, kMixBands> r {}; };
    std::vector<Resp> resp; for (const auto& c : pair.conflicts) { Resp x; DeresProposal pp = c.pEq; for (int b = 0; b < kMixBands; ++b) x.r[(size_t) b] = filterDbAt (pp, mixBandFreq (b)) / std::max (0.1, std::fabs (pp.gainDb)) * 1.0; resp.push_back (x); }   // dB por dB de corte
    auto v = [&] (int t, int b) { return (double) V.frames[(size_t) (ov + t)][(size_t) b]; };
    auto p = [&] (int t, int b) { return (double) P.frames[(size_t) (op + t)][(size_t) b]; };
    for (int scn = 0; scn < 4; ++scn)
    {
        ScenarioMetrics M; M.name = scn == 0 ? "Antes" : (scn == 1 ? "A \xC2\xB7 EQ est\xC3\xA1tico" : (scn == 2 ? "B \xC2\xB7 Sidechain" : "C \xC2\xB7 Combina\xC3\xA7\xC3\xA3o"));
        double powV0 = 0, powV1 = 0, powP = 0, depth0 = 0, depth1 = 0, unnec = 0, nAct = 0; std::array<double, kMixBands> gSum {}, gN {}; std::vector<double> peak0, peak1, dyn0, dyn1;
        for (int t = 0; t < K; ++t)
        {
            std::array<double, kMixBands> g {}; if (scn == 3) for (int b = 0; b < kMixBands; ++b) g[(size_t) b] -= 0.5;
            for (size_t ci = 0; ci < pair.conflicts.size(); ++ci)
            {
                const auto& c = pair.conflicts[ci]; const int bc = c.band; const bool aV = v (t, bc) > kAct, aP = p (t, bc) > kAct; double w = 0;
                if (scn == 1) w = aV ? c.cutDb : 0; else if (scn == 2) w = (aV && aP && p (t, bc) >= c.pSc.thrDb) ? c.cutDb : 0;
                else if (scn == 3) w = (aV ? c.cutDb / 3.0 : 0.0) + ((aV && aP && p (t, bc) >= c.pSc.thrDb) ? 2.0 * c.cutDb / 3.0 : 0.0);
                if (w > 0) for (int b = 0; b < kMixBands; ++b) g[(size_t) b] += w * resp[ci].r[(size_t) b];       // resp e negativo
                if (aV && aP) { const double d0 = std::min (15.0, std::max (0.0, v (t, bc) - (p (t, bc) - kMargin))), d1 = std::min (15.0, std::max (0.0, v (t, bc) + g[(size_t) bc] - (p (t, bc) - kMargin))); depth0 += d0; depth1 += d1; }
                if (aV) { ++nAct; if (w > 0 && std::min (15.0, std::max (0.0, v (t, bc) - (p (t, bc) - kMargin))) <= 0.0 && aP) ++unnec; else if (w > 0 && ! aP) ++unnec; }
            }
            for (int b = 0; b < kMixBands; ++b)
            {
                const double lv = v (t, b); if (lv > kAct) { powV0 += std::pow (10.0, lv / 10.0); powV1 += std::pow (10.0, (lv + g[(size_t) b]) / 10.0); gSum[(size_t) b] += g[(size_t) b]; gN[(size_t) b] += 1; }
                const double lp = p (t, b); if (lp > kAct) powP += std::pow (10.0, lp / 10.0);
            }
            const int bc0 = pair.conflicts[0].band; if (v (t, bc0) > kAct) { peak0.push_back (v (t, bc0)); peak1.push_back (v (t, bc0) + g[(size_t) bc0]); }
        }
        M.victimRmsDb = powV0 > 0 ? 10.0 * std::log10 (std::max (1e-12, powV1) / powV0) : 0.0;
        M.mixRmsDb = (powV0 + powP) > 0 ? 10.0 * std::log10 ((powV1 + powP) / (powV0 + powP)) : 0.0;
        for (int b = 0; b < kMixBands; ++b) if (gN[(size_t) b] > 0) M.maxTonalDb = std::min (M.maxTonalDb, gSum[(size_t) b] / gN[(size_t) b]);
        M.maskRemainPct = depth0 > 0 ? 100.0 * depth1 / depth0 : 0.0; M.collateralPct = nAct > 0 ? 100.0 * unnec / nAct : 0.0;
        if (! peak0.empty()) { M.peakDb = percentileOf (peak1, 0.95) - percentileOf (peak0, 0.95); M.dynDb = (percentileOf (peak1, 0.95) - percentileOf (peak1, 0.5)) - (percentileOf (peak0, 0.95) - percentileOf (peak0, 0.5)); }
        M.loudComp = -M.mixRmsDb;
        if (scn == 0) { M.maskRemainPct = 100; }
        else for (const auto& c : pair.conflicts)
        {
            if (scn == 1) M.props.push_back (c.pEq); else if (scn == 2) M.props.push_back (c.pSc);
            else { DeresProposal a = c.pEq; a.gainDb = c.pEq.gainDb / 3.0; DeresProposal b = c.pSc; b.gainDb = 2.0 * c.pSc.gainDb / 3.0; M.props.push_back (a); M.props.push_back (b); }
        }
        R.sc.push_back (M);
    }
    return R;
}
} // namespace pq
