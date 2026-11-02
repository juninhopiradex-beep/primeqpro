// =============================================================================
//  PRIMEQ pro by Piradex — Spatial Frequency Intelligence (deterministico, sem ML)
//
//  Entrada: por pista, curva P50 por banda (MixResult.curve) e estatisticas espaciais (panorama, correlacao, perda em mono).
//  Saida: tipo de fonte, achados (centro congestionado, risco mono, corte so no Mid) e propostas EQ aplicaveis.
//  LIMITES (dizer ao utilizador): um EQ nao desloca instrumentos dentro de uma mistura estereo ja somada; "Pan candidato" e uma
//  SUGESTAO para fazer no panner da pista — o plugin nao tem panner. Correlacao/perda mono sao medidas sobre L/R, nao audicao.
// =============================================================================
#pragma once
#include "MixAnalysis.h"
#include <cstdio>

namespace pq
{
enum class SrcKind { Mono = 0, Stereo, Wide, Mix };
inline const char* srcKindName (SrcKind k) { return k == SrcKind::Mono ? "Mono" : (k == SrcKind::Stereo ? "Est\xC3\xA9reo" : (k == SrcKind::Wide ? "Est\xC3\xA9reo largo" : "Mistura/stem")); }

struct SpaceFinding
{
    enum Kind { CenterCrowd = 0, MonoRisk, MidCut, MonoGlobal } kind = CenterCrowd;
    int track = -1, other = -1, bandLo = 0, bandHi = 0; std::string text; std::vector<DeresProposal> props;   // props aplicam-se a 'track'
};
struct SpaceResult { std::vector<SrcKind> kind; std::vector<SpaceFinding> findings; };

inline SrcKind classifySource (const SpatialStats& st, const std::array<double, kMixBands>& curve)
{
    int act = 0; for (double v : curve) if (v > -119) ++act;
    if (act >= 25 && st.corrAll < 0.97f) return SrcKind::Mix;
    if (st.corrAll > 0.985f) return SrcKind::Mono;
    return st.corrAll < 0.45f ? SrcKind::Wide : SrcKind::Stereo;
}

inline std::string fmtHz (double f) { char b[32]; std::snprintf (b, sizeof b, "%.0f Hz", f); return b; }

inline SpaceResult analyseSpace (const std::vector<MixTrack>& tr, const MixResult& res)
{
    SpaceResult S; const size_t N = tr.size(); S.kind.resize (N);
    for (size_t i = 0; i < N && i < res.curve.size(); ++i) S.kind[i] = classifySource (tr[i].st, res.curve[i]);
    std::vector<double> peakOf (N, -120.0); for (size_t t = 0; t < N && t < res.curve.size(); ++t) for (double v : res.curve[t]) peakOf[t] = std::max (peakOf[t], v);
    auto centred = [&] (size_t t, int b) { return res.curve[t][(size_t) b] > std::max (-50.0, peakOf[t] - 24.0) && std::fabs (tr[t].st.pan[b]) < 0.25f; };

    // 1) centro congestionado: dois ou mais instrumentos centrados na mesma zona (200 Hz – 6 kHz)
    for (size_t i = 0; i < N && i < res.curve.size(); ++i)
        for (size_t j = i + 1; j < N && j < res.curve.size(); ++j)
        {
            int n = 0, lo = kMixBands, hi = 0;
            for (int b = 0; b < kMixBands; ++b)
            {
                const double f = mixBandFreq (b); if (f < 200 || f > 6000) continue;
                if (centred (i, b) && centred (j, b) && std::fabs (res.curve[i][(size_t) b] - res.curve[j][(size_t) b]) < 12.0) { ++n; lo = std::min (lo, b); hi = std::max (hi, b); }
            }
            if (n < 4) continue;
            const size_t mover = tr[i].priority <= tr[j].priority ? i : j, keep = mover == i ? j : i;
            SpaceFinding f; f.kind = SpaceFinding::CenterCrowd; f.track = (int) mover; f.other = (int) keep; f.bandLo = lo; f.bandHi = hi;
            char b[420]; const double amt = std::min (0.6, 0.30 + 0.05 * (n - 4)); const bool left = (mover % 2) == 0;
            if (tr[mover].priority >= 4) std::snprintf (b, sizeof b, "\"%s\" e \"%s\" ocupam o centro entre %s e %s. Ambas t\xC3\xAAm prioridade alta: mantenha-as no centro e use o Masking (EQ/sidechain) para as separar.", tr[i].name.c_str(), tr[j].name.c_str(), fmtHz (mixBandFreq (lo)).c_str(), fmtHz (mixBandFreq (hi)).c_str());
            else if (tr[mover].st.corrAll < 0.4f) std::snprintf (b, sizeof b, "\"%s\" e \"%s\" ocupam o centro entre %s e %s; \"%s\" j\xC3\xA1 \xC3\xA9 larga (corr. %.2f) \xE2\x80\x94 sem sugest\xC3\xA3o de pan.", tr[i].name.c_str(), tr[j].name.c_str(), fmtHz (mixBandFreq (lo)).c_str(), fmtHz (mixBandFreq (hi)).c_str(), tr[mover].name.c_str(), (double) tr[mover].st.corrAll);
            else std::snprintf (b, sizeof b, "\"%s\" e \"%s\" ocupam o centro entre %s e %s (%d bandas). Pan candidato: \"%s\" para %s %.0f %% \xE2\x80\x94 fa\xC3\xA7""a no panner da pista (o plugin n\xC3\xA3o tem panner). Confirme em mono.", tr[i].name.c_str(), tr[j].name.c_str(), fmtHz (mixBandFreq (lo)).c_str(), fmtHz (mixBandFreq (hi)).c_str(), n, tr[mover].name.c_str(), left ? "a esquerda" : "a direita", 100 * amt);
            f.text = b; f.bandHi = hi; f.bandLo = lo; S.findings.push_back (f);
        }
    { std::vector<SpaceFinding> crowd, rest; for (auto& f : S.findings) (f.kind == SpaceFinding::CenterCrowd ? crowd : rest).push_back (f);
      std::sort (crowd.begin(), crowd.end(), [] (const SpaceFinding& a, const SpaceFinding& b) { return (a.bandHi - a.bandLo) > (b.bandHi - b.bandLo); }); if (crowd.size() > 3) crowd.resize (3); S.findings = crowd; }

    // 2) risco mono: graves com baixa correlacao / perda em mono
    for (size_t t = 0; t < N && t < res.curve.size(); ++t)
    {
        int bad = 0, lo = kMixBands, hi = 0; double worst = 0;
        for (int b = 0; b < kMixBands; ++b) { if (mixBandFreq (b) > 250 || res.curve[t][(size_t) b] < -60) continue; if (tr[t].st.corr[b] < 0.3f || tr[t].st.monoLoss[b] < -3.f) { ++bad; lo = std::min (lo, b); hi = std::max (hi, b); worst = std::min (worst, (double) tr[t].st.monoLoss[b]); } }
        if (bad >= 2)
        {
            SpaceFinding f; f.kind = SpaceFinding::MonoRisk; f.track = (int) t; f.bandLo = lo; f.bandHi = hi; char b[300];
            std::snprintf (b, sizeof b, "\"%s\": graves (%s\xE2\x80\x93%s) com fase inst\xC3\xA1vel \xE2\x80\x94 perdem at\xC3\xA9 %.1f dB em mono. Sugest\xC3\xA3o: passa-alto s\xC3\xB3 no Side a 140 Hz (mant\xC3\xA9m o centro dos graves).", tr[t].name.c_str(), fmtHz (mixBandFreq (lo)).c_str(), fmtHz (mixBandFreq (hi)).c_str(), -worst);
            f.text = b; DeresProposal p; p.type = 3; p.place = 4; p.freq = 140; p.q = 0.7; p.gainDb = 0; p.why = "Mono Safety: HP no Side"; f.props.push_back (p); S.findings.push_back (f);
        }
        else if (tr[t].st.corrAll < 0.2f && tr[t].st.monoLossAll < -3.f && res.curve[t][0] > -119)
        {
            SpaceFinding f; f.kind = SpaceFinding::MonoGlobal; f.track = (int) t; char b[260];
            std::snprintf (b, sizeof b, "\"%s\": compatibilidade mono fraca (correla\xC3\xA7\xC3\xA3o %.2f, perde %.1f dB na soma). Verifique em mono antes de alargar mais.", tr[t].name.c_str(), (double) tr[t].st.corrAll, (double) -tr[t].st.monoLossAll);
            f.text = b; S.findings.push_back (f);
        }
    }

    // 3) corte so no Mid: conflito entre duas fontes centradas onde a vitima tem conteudo estereo
    for (const auto& pr : res.pairs)
    {
        const size_t V = (size_t) pr.victimIdx, P = (size_t) pr.protectedIdx; if (V >= N || P >= N) continue;
        if (tr[V].st.corrAll > 0.85f || tr[V].st.monoLossAll < -2.f) continue;
        for (const auto& c : pr.conflicts)
        {
            if (std::fabs (tr[V].st.pan[(size_t) c.band]) > 0.3f || std::fabs (tr[P].st.pan[(size_t) c.band]) > 0.3f) continue;
            SpaceFinding f; f.kind = SpaceFinding::MidCut; f.track = (int) V; f.other = (int) P; f.bandLo = f.bandHi = c.band; char b[300];
            std::snprintf (b, sizeof b, "\"%s\" cede a \"%s\" a %s: corte s\xC3\xB3 no Mid preserva a largura (Side) de \"%s\" \xE2\x80\x94 menos dano timbrico que um corte est\xC3\xA9reo.", tr[V].name.c_str(), tr[P].name.c_str(), fmtHz (c.freq).c_str(), tr[V].name.c_str());
            f.text = b; DeresProposal p = c.pEq; p.place = 3; p.why = "Corte so no Mid"; f.props.push_back (p); S.findings.push_back (f);
        }
    }
    { std::vector<SpaceFinding> a, m; for (auto& f : S.findings) (f.kind == SpaceFinding::MidCut ? m : a).push_back (f); if (m.size() > 3) m.resize (3); a.insert (a.end(), m.begin(), m.end()); S.findings = a; }
    return S;
}
} // namespace pq
