// =============================================================================
//  PrimEQ Pro by Piradex — Intent-Based EQ: objetivo musical -> estrategia DSP explicita e editavel (deterministico)
//
//  Cada intencao devolve bandas NORMAIS do EQ (frequencia/Q/ganho/tipo/dinamica) + explicacao + previsao do efeito sobre a curva
//  medida da pista (resposta dos filtros somada a curva P50). Ganhos limitados a +-4 dB. Com medicao, a intensidade e ajustada
//  ao que a pista realmente precisa (ex.: so corta low-mid se ele estiver acima do medio). Sem medicao usa valores genericos (e diz-o).
// =============================================================================
#pragma once
#include "MixDoctor.h"
#include <complex>

namespace pq
{
enum class Intent { Clarity = 0, Punch, Warmth, Space, Smooth };
enum class Role { General = 0, Voice, Kick, Bass };
inline const char* intentName (Intent i) { static const char* n[5] = { "Clarity", "Punch", "Warmth", "Space", "Smooth" }; return n[(int) i]; }
inline const char* intentBlurb (Intent i)
{
    switch (i) { case Intent::Clarity: return "Mais presen\xC3\xA7""a e inteligibilidade sem dureza"; case Intent::Punch: return "Mais ataque e peso sem subir picos \xC3\xA0 toa"; case Intent::Warmth: return "Mais corpo sem acumular low-mid";
                 case Intent::Space: return "Abre espa\xC3\xA7o para outra fonte, processamento m\xC3\xADnimo"; default: return "Domar ressonâncias e agressividade, s\xC3\xB3 quando surgem"; }
}
inline const char* roleName (Role r) { static const char* n[4] = { "Geral", "Voz", "Kick", "Baixo" }; return n[(int) r]; }
inline Role guessRole (const std::string& name) { const int p = guessPriority (name); std::string n = name; for (auto& c : n) c = (char) std::tolower ((unsigned char) c);
    if (n.find ("kick") != std::string::npos || n.find ("bumbo") != std::string::npos) return Role::Kick; if (p == 5) return Role::Voice; if (p == 3) return Role::Bass; return Role::General; }

struct IntentPlan
{
    std::string title, explanation, basis; std::vector<DeresProposal> props;
    std::array<double, kMixBands> staticDelta {}, dynMax {};      // previsao em dB por banda (estatico / maximo das bandas dinamicas)
    bool measured = false;
};

inline double filterDbAt (const DeresProposal& p, double f, double fs = 48000.0)
{
    Cascade k; k.n = 1; k.c[0] = biquadRBJ ((BandType) p.type, fs, p.freq, p.gainDb, p.q);
    return magDb (k, 2.0 * M_PI * f / fs);
}

inline IntentPlan planIntent (Intent it, Role role, double amount01, const std::array<double, kMixBands>* curve = nullptr, const MixConflict* conflict = nullptr)
{
    IntentPlan P; const double a = clampd (amount01, 0.0, 1.0); P.title = std::string (intentName (it)) + " \xC2\xB7 " + roleName (role); P.measured = curve != nullptr;
    auto cap = [] (double g) { return clampd (g, -4.0, 4.0); };
    auto bell = [&] (double f, double q, double g, const char* why) { DeresProposal p; p.freq = f; p.q = q; p.gainDb = cap (g); p.why = why; P.props.push_back (p); };
    const double mid = curve ? medianBands (*curve, 1000, 4000) : -120.0, mud = curve ? medianBands (*curve, 200, 500) : -120.0, low = curve ? medianBands (*curve, 80, 200) : -120.0;
    const bool haveMud = curve && mid > -119 && mud > -119; const double mudExcess = haveMud ? mud - mid : 0.0;   // >0: low-mid acima do medio
    const double mudScale = haveMud ? clampd (0.4 + mudExcess / 8.0, 0.25, 1.0) : 0.7;
    char buf[400];
    switch (it)
    {
        case Intent::Clarity:
        {
            bell (300, 1.1, -(1.0 + 2.5 * a) * mudScale, "tirar lama");
            const double pres = haveMud ? medianBands (*curve, 2000, 4000) - mud : 0.0; const double ps = (haveMud && pres > 3.0) ? 0.5 : 1.0;
            bell (role == Role::Voice ? 3000.0 : 3500.0, 0.9, (0.8 + 1.7 * a) * ps, "presen\xC3\xA7""a");
            std::snprintf (buf, sizeof buf, "Corta o low-mid (300 Hz) para tirar lama e real\xC3\xA7""a a presen\xC3\xA7""a. %s", haveMud ? (mudExcess > 0 ? "A medi\xC3\xA7\xC3\xA3o mostra low-mid acima do m\xC3\xA9""dio \xE2\x80\x94 corte a toda a for\xC3\xA7""a." : "A medi\xC3\xA7\xC3\xA3o mostra low-mid j\xC3\xA1 abaixo do m\xC3\xA9""dio \xE2\x80\x94 corte reduzido.") : "Sem medi\xC3\xA7\xC3\xA3o desta pista: valores gen\xC3\xA9ricos."); break;
        }
        case Intent::Punch:
            if (role == Role::Kick) { bell (70, 1.1, 1.0 + 2.0 * a, "corpo"); bell (300, 1.5, -(1.0 + 2.0 * a) * mudScale, "caixa de papel\xC3\xA3o"); bell (3500, 1.2, 1.0 + 1.5 * a, "ataque (click)"); }
            else if (role == Role::Bass) { bell (100, 1.0, 0.8 + 1.2 * a, "peso"); bell (800, 1.0, 0.8 + 1.0 * a, "articula\xC3\xA7\xC3\xA3o"); }
            else { bell (90, 0.8, 0.8 + 1.5 * a, "peso"); bell (3500, 1.0, 0.5 + 1.0 * a, "ataque"); }
            std::snprintf (buf, sizeof buf, "Real\xC3\xA7""a o ataque e o peso com bandas estreitas. Ganhos baixos para n\xC3\xA3o subir picos sem necessidade. %s", curve ? "" : "Sem medi\xC3\xA7\xC3\xA3o: valores gen\xC3\xA9ricos."); break;
        case Intent::Warmth:
        {
            const double boostScale = (haveMud && mudExcess > 2.0) ? 0.5 : 1.0;
            bell (150, 0.7, (0.8 + 2.0 * a) * boostScale, "corpo"); bell (350, 1.2, -(0.5 + 1.5 * a), "evitar acumular low-mid");
            std::snprintf (buf, sizeof buf, "Corpo a 150 Hz com compensa\xC3\xA7\xC3\xA3o a 350 Hz para n\xC3\xA3o acumular low-mid. %s", (haveMud && mudExcess > 2.0) ? "J\xC3\xA1 h\xC3\xA1 low-mid forte nesta pista: refor\xC3\xA7o reduzido a metade." : (curve ? "" : "Sem medi\xC3\xA7\xC3\xA3o: valores gen\xC3\xA9ricos.")); (void) low; break;
        }
        case Intent::Space:
        {
            DeresProposal p; if (conflict) { p = conflict->pDyn; p.gainDb = -(2.0 + 4.0 * a); p.why = "espa\xC3\xA7o"; }
            else { int bb = 14; double bv = -999; if (curve) for (int b = 0; b < kMixBands; ++b) { const double f = mixBandFreq (b); if (f >= 800 && f <= 4000 && (*curve)[(size_t) b] > bv) { bv = (*curve)[(size_t) b]; bb = b; } }
                   p.freq = mixBandFreq (bb); p.q = 1.2; p.gainDb = -(2.0 + 4.0 * a); p.dynamic = true; p.thrDb = curve ? bv : -30.0; p.ratio = 2.5; p.attackMs = 10; p.releaseMs = 150; p.why = "espa\xC3\xA7o"; }
            p.gainDb = cap (p.gainDb); P.props.push_back (p);
            std::snprintf (buf, sizeof buf, "Corte din\xC3\xA2mico suave a %.0f Hz: s\xC3\xB3 atua quando a pista passa do limiar (%s). Para abrir espa\xC3\xA7o a uma pista concreta use o separador Masking.", p.freq, conflict ? "zona de conflito medida" : (curve ? "pico da pr\xC3\xB3pria curva" : "valor gen\xC3\xA9rico 2 kHz")); break;
        }
        default:
        {
            std::vector<int> picks;
            if (curve)
            {
                std::vector<std::pair<double, int>> ex; for (int b = 1; b < kMixBands - 1; ++b) { const double f = mixBandFreq (b); if (f < 2000 || f > 9000 || (*curve)[(size_t) b] < -119) continue;
                    std::vector<double> nb; for (int k = std::max (0, b - 4); k <= std::min (kMixBands - 1, b + 4); ++k) if (k != b && (*curve)[(size_t) k] > -119) nb.push_back ((*curve)[(size_t) k]);
                    if (nb.size() >= 3) ex.push_back ({ (*curve)[(size_t) b] - percentileOf (nb, 0.5), b }); }
                std::sort (ex.rbegin(), ex.rend()); for (size_t i = 0; i < ex.size() && picks.size() < 2; ++i) if (ex[i].first > 1.0 && (picks.empty() || std::abs (picks[0] - ex[i].second) > 4)) picks.push_back (ex[i].second);
            }
            if (picks.empty()) { for (double f : { 3500.0, 6500.0 }) { DeresProposal p; p.freq = f; p.q = 2.5; p.gainDb = cap (-(2.0 + 3.0 * a)); p.dynamic = true; p.thrDb = -30; p.ratio = 3; p.attackMs = 5; p.releaseMs = 80; p.why = "suavizar"; P.props.push_back (p); }
                std::snprintf (buf, sizeof buf, "%s", curve ? "N\xC3\xA3o encontrei picos acima dos vizinhos: aplico dois cortes din\xC3\xA2micos gen\xC3\xA9ricos (3,5 e 6,5 kHz)." : "Sem medi\xC3\xA7\xC3\xA3o: dois cortes din\xC3\xA2micos gen\xC3\xA9ricos (3,5 e 6,5 kHz)."); }
            else { for (int b : picks) { DeresProposal p; p.freq = mixBandFreq (b); p.q = 2.5; p.gainDb = cap (-(2.0 + 3.0 * a)); p.dynamic = true; p.thrDb = (*curve)[(size_t) b] + 1.0; p.ratio = 3; p.attackMs = 5; p.releaseMs = 80; p.why = "suavizar"; P.props.push_back (p); }
                   std::snprintf (buf, sizeof buf, "Cortes din\xC3\xA2micos nos %d picos mais salientes da curva medida (acima dos vizinhos); s\xC3\xB3 atuam quando o n\xC3\xADvel ultrapassa o limiar.", (int) picks.size()); }
        }
    }
    P.explanation = buf;
    for (const auto& p : P.props) for (int b = 0; b < kMixBands; ++b) { const double d = filterDbAt (p, mixBandFreq (b)); if (p.dynamic) P.dynMax[(size_t) b] += (p.gainDb < 0 ? -1.0 : 1.0) * std::fabs (d); else P.staticDelta[(size_t) b] += d; }
    P.basis = curve ? "Previs\xC3\xA3o = curva P50 medida + resposta dos filtros." : "Previs\xC3\xA3o = apenas a resposta dos filtros (sem medi\xC3\xA7\xC3\xA3o da pista).";
    return P;
}
} // namespace pq
