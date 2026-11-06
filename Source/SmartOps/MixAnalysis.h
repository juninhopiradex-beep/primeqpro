// =============================================================================
//  PrimEQ Pro by Piradex — Mix Unmask: analise de mascaramento entre pistas e comparacao de metodos (deterministico)
//
//  Entrada: para cada pista, os quadros (25 ms) de nivel por banda (28 bandas) dos ultimos segundos.
//  Para cada par (P = pista PROTEGIDA, V = a que cede; decide a prioridade) e cada banda b:
//     profundidade de mascaramento  d_t = max(0, v_t - (p_t - 6 dB))  nos quadros em que ambas estao ativas (> -65 dBFS)
//     conflito S(b) = soma(d_t) / quadros em que P (a protegida) esta ativa.
//  Para cada conflito simula-se, sobre os MESMOS quadros, tres metodos de corte G dB em V:
//     EQ        — corte estatico, sempre ligado
//     DINAMICA  — EQ dinamico com detetor na propria V (corta quando V ultrapassa o seu P60)
//     SIDECHAIN — EQ dinamico com detetor em P (corta quando P ultrapassa o seu P10 ativo) — exige routing no DAW
//  beneficio  = fracao do mascaramento removida ; colateral = fracao dos quadros ativos de V cortados sem necessidade
//  pontuacao  = beneficio - 0,6 * colateral  (0..1)
//  ESTIMATIVA sobre medidas de nivel — nao e audicao. Ignora fase, transientes finos e o tempo de ataque/release real.
// =============================================================================
#pragma once
#include "../DSP/MixMeter.h"
#include "Deresonate.h"
#include <algorithm>
#include <cctype>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace pq
{
inline int guessPriority (std::string n)
{
    for (auto& ch : n) ch = (char) std::tolower ((unsigned char) ch);
    auto has = [&] (const char* k) { return n.find (k) != std::string::npos; };
    if (has ("vocal") || has ("vox") || has ("voz") || has ("lead")) return 5;
    if (has ("kick") || has ("bumbo") || has ("snare") || has ("caixa")) return 4;
    if (has ("bass") || has ("baixo") || has ("sub")) return 3;
    return 2;
}
struct MixTrack { std::string name; int priority = 2; std::vector<std::array<float, kMixBands>> frames; SpatialStats st; };
struct MixScore { double benefit = 0, collateral = 0, score = 0; };
enum class MixMethod { Eq = 0, Dynamic, Sidechain };
struct MixConflict
{
    int band = 0; double freq = 0, q = 3, cutDb = 3, depthDb = 0, overlap = 0;
    MixScore eq, dyn, sc; MixMethod best = MixMethod::Eq;
    DeresProposal pEq, pDyn, pSc;
};
struct MixPair { int protectedIdx = 0, victimIdx = 0; std::vector<MixConflict> conflicts; MixMethod best = MixMethod::Eq; double totalDepth = 0; };
struct MixResult
{
    std::vector<std::array<double, kMixBands>> curve;     // P50 por banda e pista (dBFS, -120 = sem dados)
    std::vector<MixPair> pairs; std::vector<int> frames;  // quadros usados por pista
    std::string message;
};

inline double percentileOf (std::vector<double> v, double p)
{
    if (v.empty()) return -120.0; std::sort (v.begin(), v.end()); const size_t i = std::min (v.size() - 1, (size_t) std::floor (p * (double) (v.size() - 1) + 0.5)); return v[i];
}

inline MixResult analyseMix (const std::vector<MixTrack>& tr, double minConflictDb = 1.5, int maxPerPair = 3)
{
    MixResult R; const double kAct = -65.0, kMargin = 6.0;
    R.curve.assign (tr.size(), {}); R.frames.assign (tr.size(), 0);
    for (size_t i = 0; i < tr.size(); ++i)
    {
        R.frames[i] = (int) tr[i].frames.size();
        for (int b = 0; b < kMixBands; ++b)
        {
            std::vector<double> a; for (const auto& f : tr[i].frames) if (f[(size_t) b] > kAct) a.push_back (f[(size_t) b]);
            R.curve[i][(size_t) b] = a.size() >= 10 ? percentileOf (a, 0.5) : -120.0;
        }
    }
    if (tr.size() < 2) { R.message = "Precisa de pelo menos 2 pistas com 'Analisar esta pista' ligado."; return R; }
    int minF = 1 << 30; for (const auto& t : tr) minF = std::min (minF, (int) t.frames.size());
    if (minF < 80) { R.message = "Poucos dados: toque o material durante pelo menos 2 s em todas as pistas (tem " + std::to_string (minF * 25 / 1000) + " s)."; return R; }
    const int K = minF;

    for (size_t i = 0; i < tr.size(); ++i)
        for (size_t j = i + 1; j < tr.size(); ++j)
        {
            size_t P = i, V = j;
            if (tr[i].priority < tr[j].priority) { P = j; V = i; }
            else if (tr[i].priority == tr[j].priority)
            {
                double mi = 0, mj = 0; int ci = 0, cj = 0;
                for (int b = 0; b < kMixBands; ++b) { if (R.curve[i][(size_t) b] > -119) { mi += R.curve[i][(size_t) b]; ++ci; } if (R.curve[j][(size_t) b] > -119) { mj += R.curve[j][(size_t) b]; ++cj; } }
                if (ci && cj && mi / ci < mj / cj) { P = j; V = i; }
            }
            MixPair pair; pair.protectedIdx = (int) P; pair.victimIdx = (int) V;
            const auto& fp = tr[P].frames; const auto& fv = tr[V].frames; const int op = (int) fp.size() - K, ov = (int) fv.size() - K;
            struct Cand { int b; double S, overlap; };
            std::vector<Cand> cands; std::vector<double> S ((size_t) kMixBands, 0.0), ovl ((size_t) kMixBands, 0.0);
            double pkP = -120, pkV = -120; for (int b = 0; b < kMixBands; ++b) { pkP = std::max (pkP, R.curve[P][(size_t) b]); pkV = std::max (pkV, R.curve[V][(size_t) b]); }
            for (int b = 0; b < kMixBands; ++b)
            {
                if (R.curve[P][(size_t) b] < pkP - 30.0 || R.curve[V][(size_t) b] < pkV - 30.0) continue;      // ignora zonas onde uma das pistas esta 30 dB abaixo do seu proprio pico (fugas do filtro, nao conteudo)
                double sum = 0; int nAny = 0, nBoth = 0;
                for (int t = 0; t < K; ++t)
                {
                    const double p = fp[(size_t) (op + t)][(size_t) b], v = fv[(size_t) (ov + t)][(size_t) b]; const bool ap = p > kAct, av = v > kAct;
                    if (ap) ++nAny;
                    if (ap && av) { ++nBoth; sum += std::min (15.0, std::max (0.0, v - (p - kMargin))); }
                }
                S[(size_t) b] = nAny ? sum / nAny : 0.0; ovl[(size_t) b] = nAny ? (double) nBoth / nAny : 0.0;
                if (S[(size_t) b] >= minConflictDb && nBoth >= 20) cands.push_back ({ b, S[(size_t) b], ovl[(size_t) b] });
            }
            std::sort (cands.begin(), cands.end(), [] (const Cand& x, const Cand& y) { return x.S > y.S; });
            std::vector<int> picked;
            for (const auto& c : cands)
            {
                bool near = false; for (int o : picked) if (std::abs (o - c.b) < 5) near = true;
                bool isMax = true; for (int k = std::max (0, c.b - 2); k <= std::min (kMixBands - 1, c.b + 2); ++k) if (S[(size_t) k] > c.S) isMax = false;
                if (near || ! isMax) continue;
                picked.push_back (c.b); if ((int) picked.size() >= maxPerPair) break;
            }
            std::sort (picked.begin(), picked.end());
            double sumScore[3] = { 0, 0, 0 };
            for (int b : picked)
            {
                MixConflict c; c.band = b; c.freq = mixBandFreq (b); c.q = kMixQ; c.depthDb = S[(size_t) b]; c.overlap = ovl[(size_t) b];
                std::vector<double> pv, vv, dep ((size_t) K); double sumD = 0, nBoth = 0;
                std::vector<double> pAct, vAct; std::vector<char> aP ((size_t) K), aV ((size_t) K); std::vector<double> pArr ((size_t) K), vArr ((size_t) K);
                for (int t = 0; t < K; ++t)
                {
                    pArr[(size_t) t] = fp[(size_t) (op + t)][(size_t) b]; vArr[(size_t) t] = fv[(size_t) (ov + t)][(size_t) b]; aP[(size_t) t] = pArr[(size_t) t] > kAct; aV[(size_t) t] = vArr[(size_t) t] > kAct;
                    if (aP[(size_t) t]) pAct.push_back (pArr[(size_t) t]); if (aV[(size_t) t]) vAct.push_back (vArr[(size_t) t]);
                    dep[(size_t) t] = (aP[(size_t) t] && aV[(size_t) t]) ? std::min (15.0, std::max (0.0, vArr[(size_t) t] - (pArr[(size_t) t] - kMargin))) : 0.0;
                    if (aP[(size_t) t] && aV[(size_t) t]) { sumD += dep[(size_t) t]; ++nBoth; }
                }
                const double meanD = nBoth > 0 ? sumD / nBoth : 0.0; c.cutDb = std::min (8.0, std::max (2.0, 0.8 * meanD + 1.0));
                const double thV = percentileOf (vAct, 0.60), thP = percentileOf (pAct, 0.10);
                auto sim = [&] (int method)
                {
                    double after = 0, before = 0, unnecessary = 0, nVact = 0;
                    for (int t = 0; t < K; ++t)
                    {
                        if (aV[(size_t) t]) ++nVact;
                        bool cut = false;
                        if (method == 0) cut = aV[(size_t) t];
                        else if (method == 1) cut = aV[(size_t) t] && vArr[(size_t) t] > thV;
                        else cut = aV[(size_t) t] && aP[(size_t) t] && pArr[(size_t) t] >= thP;
                        before += dep[(size_t) t];
                        if (aP[(size_t) t] && aV[(size_t) t]) after += std::min (15.0, std::max (0.0, (vArr[(size_t) t] - (cut ? c.cutDb : 0.0)) - (pArr[(size_t) t] - kMargin)));
                        if (cut && dep[(size_t) t] <= 0.0) ++unnecessary;
                    }
                    MixScore s; s.benefit = before > 0 ? 1.0 - after / before : 0.0; s.collateral = nVact > 0 ? unnecessary / nVact : 0.0;
                    s.score = std::min (1.0, std::max (0.0, s.benefit - 0.6 * s.collateral)); return s;
                };
                c.eq = sim (0); c.dyn = sim (1); c.sc = sim (2);
                c.best = MixMethod::Eq; double bs = c.eq.score;
                if (c.dyn.score > bs + 0.03) { bs = c.dyn.score; c.best = MixMethod::Dynamic; }
                if (c.sc.score > bs + 0.03) { bs = c.sc.score; c.best = MixMethod::Sidechain; }
                c.pEq.freq = c.freq; c.pEq.q = c.q; c.pEq.gainDb = -c.cutDb; c.pEq.excessDb = c.depthDb;
                c.pDyn = c.pEq; c.pDyn.dynamic = true; c.pDyn.thrDb = thV; c.pDyn.ratio = 3.0; c.pDyn.attackMs = 5.0; c.pDyn.releaseMs = 120.0;
                c.pSc = c.pDyn; c.pSc.needsSidechain = true; c.pSc.thrDb = thP;
                sumScore[0] += c.eq.score * c.depthDb; sumScore[1] += c.dyn.score * c.depthDb; sumScore[2] += c.sc.score * c.depthDb; pair.totalDepth += c.depthDb;
                pair.conflicts.push_back (c);
            }
            pair.best = MixMethod::Eq; double bb = sumScore[0];
            if (sumScore[1] > bb + 0.03 * pair.totalDepth) { bb = sumScore[1]; pair.best = MixMethod::Dynamic; }
            if (sumScore[2] > bb + 0.03 * pair.totalDepth) { bb = sumScore[2]; pair.best = MixMethod::Sidechain; }
            R.pairs.push_back (pair);
        }
    std::sort (R.pairs.begin(), R.pairs.end(), [] (const MixPair& a, const MixPair& b) { return a.totalDepth > b.totalDepth; });
    return R;
}
} // namespace pq
