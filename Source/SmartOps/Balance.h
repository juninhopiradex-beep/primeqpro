// =============================================================================
//  PRIMEQ PRO by Piradex — Balance Assistant do Visual Mixer: ponto de partida de nivel, pan e largura (deterministico, sem IA)
//  Entrada: nivel medido de cada pista analisada (RMS dB, ANALISAR ligado), prioridade (5 = voz, 3 = baixo, ...) e se e kick.
//  Regras: voz = 0 dB (referencia); alvos relativos a voz: kick -4, baixo -5, prioridade 4 -6, 2 -9, 1 -12 dB. O ganho proposto
//  e (alvo - diferenca medida), limitado a [-18, +9] dB. Kick, baixo e voz ficam ao centro (baixo/kick com largura 60 %);
//  as restantes alternam L/R com 25, 35 e 45 %. E um ponto de partida para validar de ouvido, nao uma mistura automatica.
// =============================================================================
#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace pq
{
struct BalanceIn  { int slot = -1; std::string name; int priority = 2; bool kick = false; double levelDb = -120.0; };
struct BalanceOut { int slot = -1; float gainDb = 0.f, panPct = 0.f, widthPct = 100.f; };

inline std::vector<BalanceOut> balancePropose (const std::vector<BalanceIn>& in)
{
    std::vector<BalanceOut> out; std::vector<const BalanceIn*> v;
    for (const auto& t : in) if (t.levelDb > -90.0) v.push_back (&t);
    if (v.size() < 2) return out;
    const BalanceIn* lead = nullptr;
    for (auto* t : v) if (t->priority >= 5 && (lead == nullptr || t->levelDb > lead->levelDb)) lead = t;
    if (lead == nullptr) for (auto* t : v) if (lead == nullptr || t->levelDb > lead->levelDb) lead = t;
    auto target = [] (const BalanceIn& t) { return t.kick ? -4.0 : (t.priority >= 5 ? 0.0 : (t.priority == 3 ? -5.0 : (t.priority == 4 ? -6.0 : (t.priority == 2 ? -9.0 : -12.0)))); };
    int k = 0;
    for (auto* t : v)
    {
        BalanceOut o; o.slot = t->slot;
        o.gainDb = (float) std::clamp (target (*t) - (t->levelDb - lead->levelDb) - target (*lead), -18.0, 9.0);
        if (t == lead) o.gainDb = 0.f;
        const bool centre = t->kick || t->priority >= 5 || t->priority == 3 || t == lead;
        if (centre) { o.panPct = 0.f; o.widthPct = (t->kick || t->priority == 3) ? 60.f : 100.f; }
        else { const float mag = 25.f + 10.f * (float) ((k / 2) % 3); o.panPct = (k % 2 == 0) ? -mag : mag; o.widthPct = 100.f; ++k; }
        out.push_back (o);
    }
    return out;
}
} // namespace pq
