// =============================================================================
//  PRIMEQ PRO by Piradex — Mix Doctor: sintoma -> evidencias MEDIDAS -> hipoteses -> acoes (regras DSP deterministicas, V1)
//
//  Regras do dossie: separar evidencia observada de hipotese; sem probabilidades inventadas — etiquetas
//  "a investigar" / "compativel com os dados" / "nao confirmado"; o que o plugin nao mede e dito como tal.
// =============================================================================
#pragma once
#include "MixSpace.h"
#include <cstdarg>

namespace pq
{
enum class Symptom { VocalUnclear = 0, KickNoImpact, MuddyMix, HarshMix, BassMono };
inline const char* symptomName (Symptom s)
{
    switch (s) { case Symptom::VocalUnclear: return "Vocal pouco intelig\xC3\xADvel"; case Symptom::KickNoImpact: return "Kick sem impacto"; case Symptom::MuddyMix: return "Mix turva";
                 case Symptom::HarshMix: return "Mix agressiva"; default: return "Graves somem em mono"; }
}
enum class Verdict { Investigate = 0, Compatible, Unconfirmed };
inline const char* verdictName (Verdict v) { return v == Verdict::Compatible ? "compat\xC3\xADvel com os dados" : (v == Verdict::Investigate ? "a investigar" : "n\xC3\xA3o confirmado"); }

struct DocHyp { std::string text; Verdict v = Verdict::Investigate; };
struct DocAction { std::string text; int track = -1; std::vector<DeresProposal> props; };
struct DocReport { Symptom symptom = Symptom::VocalUnclear; std::vector<std::string> evidence; std::vector<DocHyp> hyps; std::vector<DocAction> actions; std::string note; };

inline std::string sfmt (const char* f, ...) { char b[512]; va_list a; va_start (a, f); std::vsnprintf (b, sizeof b, f, a); va_end (a); return b; }
inline int findRole (const std::vector<MixTrack>& tr, std::initializer_list<const char*> keys)
{
    for (size_t i = 0; i < tr.size(); ++i) { std::string n = tr[i].name; for (auto& c : n) c = (char) std::tolower ((unsigned char) c); for (auto* k : keys) if (n.find (k) != std::string::npos) return (int) i; }
    return -1;
}
inline int topPriority (const std::vector<MixTrack>& tr) { int b = -1; for (size_t i = 0; i < tr.size(); ++i) if (b < 0 || tr[i].priority > tr[(size_t) b].priority) b = (int) i; return b; }
inline double medianBands (const std::array<double, kMixBands>& c, double f0, double f1)
{
    std::vector<double> v; for (int b = 0; b < kMixBands; ++b) { const double f = mixBandFreq (b); if (f >= f0 && f <= f1 && c[(size_t) b] > -119) v.push_back (c[(size_t) b]); }
    return v.empty() ? -120.0 : percentileOf (v, 0.5);
}
inline const MixPair* pairWithProtected (const MixResult& r, int p, int v = -1)
{ for (const auto& x : r.pairs) if (x.protectedIdx == p && (v < 0 || x.victimIdx == v) && ! x.conflicts.empty()) return &x; return nullptr; }
inline std::vector<DeresProposal> bestProps (const MixPair& p) { std::vector<DeresProposal> v; for (const auto& c : p.conflicts) v.push_back (p.best == MixMethod::Eq ? c.pEq : (p.best == MixMethod::Dynamic ? c.pDyn : c.pSc)); return v; }
inline const char* methodLabel (MixMethod m) { return m == MixMethod::Eq ? "EQ" : (m == MixMethod::Dynamic ? "din\xC3\xA2mica" : "sidechain"); }

inline DocReport diagnose (Symptom s, const std::vector<MixTrack>& tr, const MixResult& res, const SpaceResult& sp)
{
    DocReport R; R.symptom = s; const int N = (int) tr.size();
    if (N < 1 || res.curve.size() != tr.size()) { R.note = "Sem dados: ligue \"Analisar\" nas pistas, toque o material e carregue em ANALISAR MIX."; return R; }
    if (s == Symptom::VocalUnclear)
    {
        int v = findRole (tr, { "vocal", "vox", "voz", "lead" }); if (v < 0) v = topPriority (tr);
        const auto& cv = res.curve[(size_t) v]; int K = (int) tr[(size_t) v].frames.size(); for (const auto& t : tr) K = std::min (K, (int) t.frames.size());
        std::vector<int> cnt ((size_t) N, 0); int act = 0, masked = 0;
        for (int b = 0; b < kMixBands; ++b)
        {
            const double f = mixBandFreq (b); if (f < 1000 || f > 4000) continue;
            for (int t = 0; t < K; ++t)
            {
                const double lv = tr[(size_t) v].frames[tr[(size_t) v].frames.size() - (size_t) K + (size_t) t][(size_t) b]; if (lv < -65) continue; ++act; bool m = false;
                for (int o = 0; o < N; ++o) if (o != v && tr[(size_t) o].frames[tr[(size_t) o].frames.size() - (size_t) K + (size_t) t][(size_t) b] > lv - 6.0) { m = true; ++cnt[(size_t) o]; }
                if (m) ++masked;
            }
        }
        const double frac = act ? (double) masked / act : 0.0; int top = -1; for (int o = 0; o < N; ++o) if (o != v && (top < 0 || cnt[(size_t) o] > cnt[(size_t) top])) top = o;
        R.evidence.push_back (sfmt ("Entre 1 e 4 kHz, outra pista fica a menos de 6 dB da voz \"%s\" em %.0f %% das medi\xC3\xA7\xC3\xB5""es em que a voz toca.", tr[(size_t) v].name.c_str(), 100 * frac));
        if (top >= 0 && cnt[(size_t) top] > 0) R.evidence.push_back (sfmt ("Maior concorrente nessa zona: \"%s\".", tr[(size_t) top].name.c_str()));
        const double pres = medianBands (cv, 2000, 4000), mud = medianBands (cv, 200, 500);
        if (pres > -119 && mud > -119) R.evidence.push_back (sfmt ("Presen\xC3\xA7""a (2\xE2\x80\x93""4 kHz) vs low-mid (200\xE2\x80\x93""500 Hz) da voz: %+.1f dB.", pres - mud));
        double others = 0; int no = 0; for (int o = 0; o < N; ++o) if (o != v) { others += tr[(size_t) o].st.rmsDb; ++no; }
        const double rel = no ? tr[(size_t) v].st.rmsDb - others / no : 0.0; if (no) R.evidence.push_back (sfmt ("N\xC3\xADvel RMS da voz vs m\xC3\xA9""dia das outras pistas: %+.1f dB.", rel));
        R.hyps.push_back ({ "Masking por outra pista na zona de presen\xC3\xA7""a", frac > 0.30 ? Verdict::Compatible : Verdict::Unconfirmed });
        R.hyps.push_back ({ "N\xC3\xADvel insuficiente da voz na mistura", no && rel < -3.0 ? Verdict::Compatible : Verdict::Unconfirmed });
        R.hyps.push_back ({ "Excesso de low-mid / falta de presen\xC3\xA7""a na pr\xC3\xB3pria voz", (pres > -119 && mud > -119 && pres - mud < -6.0) ? Verdict::Compatible : Verdict::Unconfirmed });
        R.hyps.push_back ({ "Reverb ou articula\xC3\xA7\xC3\xA3o (dic\xC3\xA7\xC3\xA3o) \xE2\x80\x94 o plugin n\xC3\xA3o mede isto", Verdict::Investigate });
        if (const auto* p = pairWithProtected (res, v)) R.actions.push_back ({ sfmt ("Abrir espa\xC3\xA7o na \"%s\" (%s, %d banda(s)) \xE2\x80\x94 o m\xC3\xA9todo recomendado \xC3\xA9 %s.", tr[(size_t) p->victimIdx].name.c_str(), "Masking", (int) p->conflicts.size(), methodLabel (p->best)), p->victimIdx, bestProps (*p) });
        R.note = "Percentagens medidas sobre n\xC3\xADveis por banda a cada 25 ms; n\xC3\xA3o medem inteligibilidade.";
    }
    else if (s == Symptom::KickNoImpact)
    {
        int k = findRole (tr, { "kick", "bumbo" }); const int b = findRole (tr, { "bass", "baixo", "sub" }); if (k < 0) { R.note = "Nenhuma pista chamada Kick/Bumbo: renomeie a pista no painel da esquerda."; return R; }
        const auto& st = tr[(size_t) k].st; R.evidence.push_back (sfmt ("Fator de crista do kick: %.1f dB (percussivo saud\xC3\xA1vel costuma ser > 12 dB).", (double) st.crestDb));
        const MixPair* p = b >= 0 ? pairWithProtected (res, k, b) : nullptr;
        if (b >= 0) { const double lk = medianBands (res.curve[(size_t) k], 40, 140), lb = medianBands (res.curve[(size_t) b], 40, 140); if (lk > -119 && lb > -119) R.evidence.push_back (sfmt ("Zona 80\xE2\x80\x93""140 Hz: kick %+.1f dB em rela\xC3\xA7\xC3\xA3o ao baixo (mediana quando toca).", lk - lb)); }
        if (p) R.evidence.push_back (sfmt ("Baixo mascara o kick em %d banda(s); maior profundidade m\xC3\xA9""dia %.1f dB.", (int) p->conflicts.size(), p->conflicts[0].depthDb));
        R.hyps.push_back ({ "Transiente fraco / comprimido", st.crestDb < 9.f ? Verdict::Compatible : Verdict::Unconfirmed });
        R.hyps.push_back ({ "Baixo concorrente na mesma zona", p ? Verdict::Compatible : Verdict::Unconfirmed });
        R.hyps.push_back ({ "Din\xC3\xA2mica do pr\xC3\xB3prio kick/ataque \xE2\x80\x94 verificar na escuta", Verdict::Investigate });
        if (p) R.actions.push_back ({ sfmt ("Dar espa\xC3\xA7o ao kick na \"%s\" (%s recomendado).", tr[(size_t) p->victimIdx].name.c_str(), methodLabel (p->best)), p->victimIdx, bestProps (*p) });
        R.note = "Alternativa: Inten\xC3\xA7\xC3\xA3o \"Punch\" na pista do kick.";
    }
    else if (s == Symptom::MuddyMix)
    {
        double pl = 0, pm = 0; int nl = 0, nm = 0; std::vector<std::pair<double, int>> contrib;
        for (int t = 0; t < N; ++t) for (int b = 0; b < kMixBands; ++b)
        {
            const double f = mixBandFreq (b), v = res.curve[(size_t) t][(size_t) b]; if (v < -119) continue; const double pw = std::pow (10.0, v / 10.0);
            if (f >= 150 && f <= 500) { pl += pw; ++nl; } else if (f > 500 && f <= 4000) { pm += pw; ++nm; }
        }
        for (int t = 0; t < N; ++t) { const double v = medianBands (res.curve[(size_t) t], 150, 500); if (v > -119) contrib.push_back ({ v, t }); }
        std::sort (contrib.rbegin(), contrib.rend());
        const double d = (nl && nm) ? 10.0 * std::log10 ((pl / nl) / (pm / nm)) : 0.0;
        R.evidence.push_back (sfmt ("Energia m\xC3\xA9""dia por banda em 150\xE2\x80\x93""500 Hz vs 500 Hz\xE2\x80\x93""4 kHz (soma das pistas): %+.1f dB.", d));
        for (size_t i = 0; i < contrib.size() && i < 3; ++i) R.evidence.push_back (sfmt ("Contribuidor %d: \"%s\" a %.0f dBFS (mediana 150\xE2\x80\x93""500 Hz).", (int) i + 1, tr[(size_t) contrib[i].second].name.c_str(), contrib[i].first));
        R.hyps.push_back ({ "Acumula\xC3\xA7\xC3\xA3o de low-mid em v\xC3\xA1rias pistas", (d > 3.0 && contrib.size() >= 3) ? Verdict::Compatible : Verdict::Unconfirmed });
        R.hyps.push_back ({ "Uma \xC3\xBAnica pista domina o low-mid", (contrib.size() >= 2 && contrib[0].first - contrib[1].first > 6.0) ? Verdict::Compatible : Verdict::Unconfirmed });
        R.hyps.push_back ({ "Reverb/ressonância de sala \xE2\x80\x94 n\xC3\xA3o medido", Verdict::Investigate });
        for (size_t i = 0; i < contrib.size() && i < 2; ++i)
        {
            const int t = contrib[i].second; int bb = 0; double bv = -999; for (int b = 0; b < kMixBands; ++b) { const double f = mixBandFreq (b); if (f >= 150 && f <= 500 && res.curve[(size_t) t][(size_t) b] > bv) { bv = res.curve[(size_t) t][(size_t) b]; bb = b; } }
            DeresProposal p; p.freq = mixBandFreq (bb); p.q = 1.2; p.gainDb = -2.0; p.why = "Low-mid"; R.actions.push_back ({ sfmt ("\"%s\": corte suave de 2 dB a %.0f Hz.", tr[(size_t) t].name.c_str(), p.freq), t, { p } });
        }
        R.note = "Cortes pequenos e est\xC3\xA1ticos; reavalie com A/B a n\xC3\xADvel igualado.";
    }
    else if (s == Symptom::HarshMix)
    {
        for (int t = 0; t < N; ++t)
        {
            const double hi = medianBands (res.curve[(size_t) t], 2500, 8000), all = medianBands (res.curve[(size_t) t], 80, 12000);
            std::vector<double> a; double bestBurst = 0; int bb = -1;
            for (int b = 0; b < kMixBands; ++b) { const double f = mixBandFreq (b); if (f < 3500 || f > 10000) continue; std::vector<double> x; for (const auto& fr : tr[(size_t) t].frames) if (fr[(size_t) b] > -65) x.push_back (fr[(size_t) b]); if (x.size() < 40) continue; const double burst = percentileOf (x, 0.95) - percentileOf (x, 0.5); if (burst > bestBurst) { bestBurst = burst; bb = b; } }
            if (hi > -119 && all > -119) R.evidence.push_back (sfmt ("\"%s\": 2,5\xE2\x80\x93""8 kHz %+.1f dB vs mediana global da pista%s.", tr[(size_t) t].name.c_str(), hi - all, bestBurst >= 6.0 ? sfmt (" \xC2\xB7 picos curtos de %.0f dB (compat\xC3\xADvel com sibil\xC3\xA2ncia)", bestBurst).c_str() : ""));
            if (bb >= 0 && bestBurst >= 6.0)
            {
                std::vector<double> x; for (const auto& fr : tr[(size_t) t].frames) if (fr[(size_t) bb] > -65) x.push_back (fr[(size_t) bb]);
                DeresProposal p; p.freq = mixBandFreq (bb); p.q = 2.5; p.gainDb = -4.0; p.dynamic = true; p.thrDb = percentileOf (x, 0.75); p.ratio = 3; p.attackMs = 1; p.releaseMs = 60; p.why = "De-ess";
                R.actions.push_back ({ sfmt ("\"%s\": EQ din\xC3\xA2mico (de-ess) a %.0f Hz, s\xC3\xB3 atua nos picos.", tr[(size_t) t].name.c_str(), p.freq), t, { p } });
                R.hyps.push_back ({ sfmt ("Sibil\xC3\xA2ncia/transientes agudos em \"%s\"", tr[(size_t) t].name.c_str()), Verdict::Compatible });
            }
        }
        R.hyps.push_back ({ "Excesso tonal sustentado (ressonâncias) \xE2\x80\x94 use Smart Ops \xE2\x80\xBA Deresonate na pista", Verdict::Investigate });
        R.hyps.push_back ({ "Distor\xC3\xA7\xC3\xA3o/saturação excessiva \xE2\x80\x94 n\xC3\xA3o medido", Verdict::Investigate });
        if (R.evidence.empty()) R.note = "Sem dados suficientes nas bandas agudas.";
    }
    else
    {
        for (int t = 0; t < N; ++t)
        {
            double c = 0, l = 0; int n = 0; for (int b = 0; b < kMixBands; ++b) if (mixBandFreq (b) <= 250 && res.curve[(size_t) t][(size_t) b] > -60) { c += tr[(size_t) t].st.corr[b]; l += tr[(size_t) t].st.monoLoss[b]; ++n; }
            if (! n) continue; c /= n; l /= n;
            R.evidence.push_back (sfmt ("\"%s\": graves (<250 Hz) com correla\xC3\xA7\xC3\xA3o %.2f e perda mono %.1f dB.", tr[(size_t) t].name.c_str(), c, -l));
            if (c < 0.3 || l < -3.0) R.hyps.push_back ({ sfmt ("Graves de \"%s\" com fase/est\xC3\xA9reo inst\xC3\xA1vel", tr[(size_t) t].name.c_str()), Verdict::Compatible });
        }
        for (const auto& f : sp.findings) if (f.kind == SpaceFinding::MonoRisk) R.actions.push_back ({ f.text, f.track, f.props });
        R.hyps.push_back ({ "Cancelamento entre pistas diferentes (fase relativa) \xE2\x80\x94 n\xC3\xA3o medido entre pistas", Verdict::Investigate });
        if (R.hyps.size() == 1) R.hyps.insert (R.hyps.begin(), { "Fase/estéreo dos graves em alguma pista", Verdict::Unconfirmed });
    }
    return R;
}
} // namespace pq
