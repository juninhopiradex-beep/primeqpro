// =============================================================================
//  PrimEQ Pro by Piradex — logica do teste ABX cego (pura, testavel)
//  A = som original (EQ ignorado), B = som processado (com compensacao de nivel). Em cada tentativa o X e
//  sorteado em segredo; o utilizador diz se X e A ou B. No fim: acertos, p-valor binomial unilateral
//  (probabilidade de acertar >= k por sorte, p0 = 0.5) e veredicto.
// =============================================================================
#pragma once
#include <cmath>
#include <random>
#include <string>
#include <vector>

namespace pq
{
inline double binomTailP (int n, int k)                   // P(X >= k), X ~ Bin(n, 0.5)
{
    if (k <= 0) return 1.0; if (k > n) return 0.0;
    double p = 0; for (int i = k; i <= n; ++i) { double lc = std::lgamma (n + 1.0) - std::lgamma (i + 1.0) - std::lgamma (n - i + 1.0); p += std::exp (lc - n * std::log (2.0)); }
    return std::min(1.0, p);
}

class AbxSession
{
public:
    void start (int trials, unsigned seed)
    {
        n = std::max (1, trials); std::mt19937 rng (seed); xIsB.assign ((size_t) n, false); answers.assign ((size_t) n, -1);
        for (int i = 0; i < n; ++i) xIsB[(size_t) i] = (rng() & 1u) != 0; cur = 0;
    }
    int  trials() const { return n; }
    int  current() const { return cur; }
    bool finished() const { return cur >= n; }
    bool xIsBNow() const { return ! finished() && xIsB[(size_t) cur]; }      // so a UI deve usar isto para ligar o X (nunca mostrar)
    void answer (bool saysB) { if (finished()) return; answers[(size_t) cur] = saysB ? 1 : 0; ++cur; }
    int  correct() const { int c = 0; for (int i = 0; i < std::min (cur, n); ++i) if (answers[(size_t) i] == (xIsB[(size_t) i] ? 1 : 0)) ++c; return c; }
    int  answered() const { return std::min (cur, n); }
    double pValue() const { return binomTailP (answered(), correct()); }
    std::string verdict() const
    {
        const int a = answered(), c = correct(); if (a == 0) return "Sem respostas.";
        const double p = pValue(); char b[256];
        if (a < 8) std::snprintf (b, sizeof b, "%d/%d certas (p = %.3f). Poucas tentativas: use pelo menos 10 para uma conclusao fiavel.", c, a, p);
        else if (p < 0.01) std::snprintf (b, sizeof b, "%d/%d certas (p = %.3f): diferenca CLARAMENTE audivel.", c, a, p);
        else if (p < 0.05) std::snprintf (b, sizeof b, "%d/%d certas (p = %.3f): diferenca audivel (estatisticamente significativa).", c, a, p);
        else std::snprintf (b, sizeof b, "%d/%d certas (p = %.3f): nao ha prova de que se oica diferenca (pode ser sorte).", c, a, p);
        return b;
    }
private:
    int n = 0, cur = 0; std::vector<bool> xIsB; std::vector<int> answers;
};
}
