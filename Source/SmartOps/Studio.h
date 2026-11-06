// =============================================================================
//  PrimEQ Pro by Piradex — STUDIO: ferramentas de trabalho em torno do EQ (deterministico, sem IA, sem rede)
//
//  1) Curvas-alvo por genero (Kizomba, Amapiano, ...): formas PARAMETRICAS heuristicas (inclinacao + graves + presenca + ar),
//     NAO medidas em faixas comerciais. Sao pontos de partida editaveis; o utilizador pode ajustar ou gravar a sua propria.
//  2) Tradutor: resposta GENERICA de categorias de equipamento (telemovel, colunas pequenas, auriculares, carro, mono) aplicada
//     ao espectro medido; diz o que fica abaixo do ruido ambiente estimado. Nao e a medicao de nenhum modelo concreto.
//  3) Cofre de decisoes: linha do tempo de alteracoes com motivo, restauro por etapas e ficha exportavel (HTML bilingue).
//  4) Sugestao de preset, intencao em palavras, explicacao de bandas (ensino) e assinatura (bandas -> propostas).
//  Tudo opera sobre as 28 bandas do Assistente (80 Hz–12 kHz): abaixo de 80 Hz nao ha medicao.
// =============================================================================
#pragma once
#include "IntentEq.h"
#include "ReferenceMatch.h"
#include "../Plugin/FactoryPresets.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace pq::studio
{
using Curve = std::array<double, kMixBands>;
inline bool valid (double v) { return v > -119.0; }
inline std::string L (bool en, const std::string& pt, const std::string& e) { return en ? e : pt; }

inline Curve emptyCurve() { Curve c; c.fill (-120.0); return c; }
inline double med (std::vector<double> v) { return v.empty() ? 0.0 : percentileOf (std::move (v), 0.5); }
// mediana dos valores validos em [f0,f1]
inline double medianIn (const Curve& c, double f0, double f1) { return medianBands (c, f0, f1); }
inline double interpBands (const Curve& c, double f)    // interpolacao em indice de banda (log-f); fora de 80..12k devolve -120
{
    if (f < kMixFmin || f > kMixFmax) return -120.0;
    const double x = std::log (f / kMixFmin) / std::log (kMixFmax / kMixFmin) * (kMixBands - 1); const int i = std::min (kMixBands - 2, (int) std::floor (x)); const double t = x - i;
    if (! valid (c[(size_t) i]) || ! valid (c[(size_t) i + 1])) return -120.0; return c[(size_t) i] + t * (c[(size_t) i + 1] - c[(size_t) i]);
}

// ======================================================= 1) curvas-alvo
struct Genre { const char* id; const char* pt; const char* en; double tilt, lowDb, lowHz, presDb, presHz, airDb; };
inline const std::vector<Genre>& genres()
{
    static const std::vector<Genre> g {
        { "neutral",  "Neutro",              "Neutral",          -1.5, 0.0, 100, 0.0, 3000, 0.0 },
        { "kizomba",  "Kizomba",             "Kizomba",          -1.8, 2.5,  90, 0.5, 3000, -1.5 },
        { "semba",    "Semba",               "Semba",            -1.6, 1.5, 100, 1.0, 2500, -0.5 },
        { "kuduro",   "Kuduro",              "Kuduro",           -1.2, 4.0,  70, 1.5, 3500,  0.5 },
        { "amapiano", "Amapiano",            "Amapiano",         -1.7, 4.0,  60, 0.0, 3000, -0.5 },
        { "afrobeat", "Afrobeat",            "Afrobeat",         -1.5, 2.0,  80, 1.0, 3000,  0.5 },
        { "hiphop",   "Hip-Hop",             "Hip-Hop",          -1.5, 4.0,  70, 0.5, 3000,  0.0 },
        { "trap",     "Trap",                "Trap",             -1.2, 5.0,  60, 1.0, 4000,  1.0 },
        { "pop",      "Pop",                 "Pop",              -1.4, 2.0,  80, 1.0, 3500,  1.5 },
        { "rock",     "Rock",                "Rock",             -1.5, 1.0, 100, 2.0, 3000,  0.0 },
        { "edm",      "Eletr\xC3\xB3nica",   "Electronic",       -1.2, 3.0,  60, 0.5, 3500,  1.5 },
        { "vocal",    "Voz solo",            "Solo vocal",       -2.5, -3.0, 90, 2.0, 3000,  1.0 }
    };
    return g;
}
struct TargetAdjust { double tilt = 0, low = 0, pres = 0, air = 0; };
inline double genreShape (const Genre& g, const TargetAdjust& a, double f)
{
    const double lo = (g.lowDb + a.low) / std::sqrt (1.0 + std::pow (f / g.lowHz, 4.0));
    const double lg = std::log2 (f / g.presHz), pr = (g.presDb + a.pres) * std::exp (-0.5 * lg * lg / (0.6 * 0.6));
    const double air = (g.airDb + a.air) / std::sqrt (1.0 + std::pow (9000.0 / f, 4.0));
    return (g.tilt + a.tilt) * std::log2 (f / 1000.0) + lo + pr + air;
}
inline Curve targetCurve (int genre, const TargetAdjust& a, const Curve* custom = nullptr)
{
    Curve c = emptyCurve();
    if (genre < 0 && custom) { return *custom; }
    const auto& g = genres()[(size_t) std::clamp (genre, 0, (int) genres().size() - 1)];
    for (int b = 0; b < kMixBands; ++b) c[(size_t) b] = genreShape (g, a, mixBandFreq (b));
    return c;
}
// curva "gravada": mediana-zero (so a forma)
inline Curve normalised (const Curve& c)
{
    std::vector<double> v; for (int b = 0; b < kMixBands; ++b) if (valid (c[(size_t) b])) v.push_back (c[(size_t) b]);
    Curve o = emptyCurve(); if (v.size() < 8) return o; const double m = med (v); for (int b = 0; b < kMixBands; ++b) if (valid (c[(size_t) b])) o[(size_t) b] = c[(size_t) b] - m; return o;
}

struct Deviation
{
    bool ok = false; Curve dev = emptyCurve(); double rms = 0; double region[5] {};   // grave, low-mid, medio, presenca, agudos (dB; + = pista acima do alvo)
    std::vector<std::string> linesPt, linesEn;
};
inline const char* kRegionPt[5] = { "graves (80\xE2\x80\x93""200 Hz)", "low-mid (200\xE2\x80\x93""500 Hz)", "m\xC3\xA9""dios (500 Hz\xE2\x80\x93""2 kHz)", "presen\xC3\xA7""a (2\xE2\x80\x93""5 kHz)", "agudos (5\xE2\x80\x93""12 kHz)" };
inline const char* kRegionEn[5] = { "bass (80\xE2\x80\x93""200 Hz)", "low-mids (200\xE2\x80\x93""500 Hz)", "mids (500 Hz\xE2\x80\x93""2 kHz)", "presence (2\xE2\x80\x93""5 kHz)", "highs (5\xE2\x80\x93""12 kHz)" };
inline Deviation compare (const Curve& measured, const Curve& target)
{
    Deviation D; std::vector<double> d;
    for (int b = 0; b < kMixBands; ++b) { const double f = mixBandFreq (b); if (valid (measured[(size_t) b]) && valid (target[(size_t) b]) && f >= 100 && f <= 8000) d.push_back (measured[(size_t) b] - target[(size_t) b]); }
    if (d.size() < 10) return D;
    const double off = med (d); double ss = 0; int n = 0;
    for (int b = 0; b < kMixBands; ++b) if (valid (measured[(size_t) b]) && valid (target[(size_t) b])) { D.dev[(size_t) b] = measured[(size_t) b] - target[(size_t) b] - off; ss += D.dev[(size_t) b] * D.dev[(size_t) b]; ++n; }
    D.rms = std::sqrt (ss / std::max (1, n)); D.ok = true;
    const double lim[6] = { 80, 200, 500, 2000, 5000, 12001 };
    for (int r = 0; r < 5; ++r)
    {
        std::vector<double> v; for (int b = 0; b < kMixBands; ++b) { const double f = mixBandFreq (b); if (f >= lim[r] && f < lim[r + 1] && valid (D.dev[(size_t) b])) v.push_back (D.dev[(size_t) b]); }
        D.region[r] = v.empty() ? 0.0 : med (v);
        if (v.empty() || std::fabs (D.region[r]) < 1.5) continue;
        char b1[160], b2[160]; const double a = std::fabs (D.region[r]);
        std::snprintf (b1, sizeof b1, "%s: %.1f dB %s do alvo", kRegionPt[r], a, D.region[r] > 0 ? "acima" : "abaixo");
        std::snprintf (b2, sizeof b2, "%s: %.1f dB %s the target", kRegionEn[r], a, D.region[r] > 0 ? "above" : "below");
        D.linesPt.push_back (b1); D.linesEn.push_back (b2);
    }
    if (D.linesPt.empty()) { D.linesPt.push_back ("Todas as zonas est\xC3\xA3o a menos de 1,5 dB do alvo."); D.linesEn.push_back ("All zones are within 1.5 dB of the target."); }
    return D;
}
inline std::vector<double> bandsToLogGrid (const Curve& c)
{
    std::vector<double> g ((size_t) LogGrid::N, -120.0);
    for (int i = 0; i < LogGrid::N; ++i) g[(size_t) i] = interpBands (c, LogGrid::freq (i));
    return g;
}
inline std::vector<DeresProposal> proposeToTarget (const Curve& measured, const Curve& target, double strength = 0.7, int maxBands = 6, double maxGainDb = 6.0)
{
    auto v = matchPropose (bandsToLogGrid (target), bandsToLogGrid (measured), strength, maxBands, maxGainDb);
    for (auto& p : v) p.why = "alvo";
    return v;
}

// ======================================================= 2) tradutor
struct Device { const char* id; const char* pt; const char* en; std::vector<std::pair<double, double>> resp, noise; bool mono; };
inline const std::vector<Device>& devices()
{
    static const std::vector<Device> d {
        { "phone", "Telem\xC3\xB3vel (altifalante)", "Phone speaker",
          { { 60, -40 }, { 100, -30 }, { 200, -18 }, { 300, -10 }, { 400, -5 }, { 600, -1 }, { 1000, 0 }, { 2500, 3 }, { 4000, 2 }, { 6000, -3 }, { 8000, -8 }, { 12000, -18 }, { 16000, -30 } },
          { { 80, -32 }, { 1000, -40 }, { 10000, -48 } }, true },
        { "small", "Colunas pequenas / port\xC3\xA1til", "Small speakers / laptop",
          { { 60, -30 }, { 100, -22 }, { 150, -12 }, { 200, -7 }, { 300, -3 }, { 500, 0 }, { 2000, 0 }, { 4000, 1 }, { 8000, -2 }, { 12000, -6 }, { 16000, -14 } },
          { { 80, -36 }, { 1000, -42 }, { 10000, -50 } }, false },
        { "buds", "Auriculares (consumo)", "Earbuds (consumer)",
          { { 40, -3 }, { 60, 2 }, { 100, 3 }, { 300, 0 }, { 1000, 0 }, { 3000, 4 }, { 5000, 1 }, { 7500, 2 }, { 11000, -4 }, { 16000, -12 } },
          { { 80, -52 }, { 1000, -56 }, { 10000, -58 } }, false },
        { "car", "Carro", "Car",
          { { 40, 4 }, { 60, 5 }, { 100, 4 }, { 200, 1 }, { 350, -3 }, { 500, -3 }, { 1000, 0 }, { 3000, 2 }, { 8000, -1 }, { 12000, -6 }, { 16000, -12 } },
          { { 80, -18 }, { 160, -24 }, { 315, -32 }, { 630, -38 }, { 1250, -42 }, { 2500, -46 }, { 5000, -50 }, { 10000, -55 } }, false },
        { "mono", "Mono (clube / altifalante \xC3\xBAnico)", "Mono (club / single speaker)", { { 20, 0 }, { 20000, 0 } }, { { 80, -50 }, { 10000, -50 } }, true }
    };
    return d;
}
inline double ptsAt (const std::vector<std::pair<double, double>>& p, double f)
{
    if (f <= p.front().first) return p.front().second; if (f >= p.back().first) return p.back().second;
    for (size_t i = 1; i < p.size(); ++i) if (f <= p[i].first) { const double t = std::log (f / p[i - 1].first) / std::log (p[i].first / p[i - 1].first); return p[i - 1].second + t * (p[i].second - p[i - 1].second); }
    return 0.0;
}
struct Translation
{
    bool ok = false; int device = 0; Curve translated = emptyCurve(); std::array<int, kMixBands> status {};   // 0 ok, 1 reduzido, 2 perdido
    double retainedPct = 100.0; std::vector<std::string> linesPt, linesEn;
};
inline const char* zoneNamePt (double f) { return f < 160 ? "fundamental do bumbo/baixo" : f < 315 ? "corpo" : f < 630 ? "zona de lama / corpo dos m\xC3\xA9""dios" : f < 1600 ? "m\xC3\xA9""dios" : f < 5000 ? "presen\xC3\xA7""a e voz" : "brilho / ar"; }
inline const char* zoneNameEn (double f) { return f < 160 ? "kick/bass fundamentals" : f < 315 ? "body" : f < 630 ? "mud zone / midrange body" : f < 1600 ? "mids" : f < 5000 ? "presence and vocals" : "brightness / air"; }
inline Translation translate (const Curve& measured, int dev, const SpatialStats* st = nullptr)
{
    Translation T; T.device = std::clamp (dev, 0, (int) devices().size() - 1); const auto& D = devices()[(size_t) T.device];
    double pk = -200; int nv = 0; for (int b = 0; b < kMixBands; ++b) if (valid (measured[(size_t) b])) { pk = std::max (pk, measured[(size_t) b]); ++nv; }
    if (nv < 8) return T;
    T.ok = true; double eOrig = 0, eKept = 0; const double roomFloor = -50.0;
    for (int b = 0; b < kMixBands; ++b)
    {
        const double f = mixBandFreq (b); const double m = measured[(size_t) b]; if (! valid (m)) { T.translated[(size_t) b] = -120.0; continue; }
        double dd = ptsAt (D.resp, f); if (D.mono && st) dd += std::min (0.0, (double) st->monoLoss[b]);
        T.translated[(size_t) b] = m + dd;
        const double rel = m - pk, relT = rel + dd, floorD = ptsAt (D.noise, f);
        const bool audOrig = rel > roomFloor + 6.0, audDev = relT > floorD + 6.0;
        const double pw = std::pow (10.0, m / 10.0);
        if (audOrig) eOrig += pw;
        if (audOrig && ! audDev) T.status[(size_t) b] = 2; else if (audDev && dd < -9.0) { T.status[(size_t) b] = 1; if (audOrig) eKept += pw; } else if (audOrig) eKept += pw;
    }
    T.retainedPct = eOrig > 0 ? 100.0 * eKept / eOrig : 100.0;
    for (int pass = 2; pass >= 1; --pass)
    {
        int b = 0;
        while (b < kMixBands)
        {
            if (T.status[(size_t) b] != pass) { ++b; continue; }
            int e = b; while (e + 1 < kMixBands && T.status[(size_t) e + 1] == pass) ++e;
            const double f0 = mixBandFreq (b), f1 = mixBandFreq (e), fc = std::sqrt (f0 * f1); char p1[240], p2[240];
            std::snprintf (p1, sizeof p1, "%s: %.0f\xE2\x80\x93%.0f Hz (%s)", pass == 2 ? "Desaparece" : "Muito atenuado", f0, f1, zoneNamePt (fc));
            std::snprintf (p2, sizeof p2, "%s: %.0f\xE2\x80\x93%.0f Hz (%s)", pass == 2 ? "Disappears" : "Heavily reduced", f0, f1, zoneNameEn (fc));
            T.linesPt.push_back (p1); T.linesEn.push_back (p2); b = e + 1;
        }
    }
    if (T.linesPt.empty()) { T.linesPt.push_back ("Nada relevante se perde na zona medida (80 Hz\xE2\x80\x93""12 kHz)."); T.linesEn.push_back ("Nothing relevant is lost in the measured range (80 Hz\xE2\x80\x93""12 kHz)."); }
    if (D.resp.front().first > 30 && T.device != 4) { T.linesPt.push_back ("Abaixo de 80 Hz n\xC3\xA3o h\xC3\xA1 medi\xC3\xA7\xC3\xA3o: o sub-grave n\xC3\xA3o \xC3\xA9 avaliado."); T.linesEn.push_back ("There is no measurement below 80 Hz: sub-bass is not assessed."); }
    return T;
}

// filtro de monitorizacao (so para ouvir; nunca grava no estado)
struct MonitorFx
{
    struct St { double z1 = 0, z2 = 0; };
    struct Spec { BandType t; double f, g, q; };
    int dev = 0; double fs = 48000; int n = 0; Coefs c[6]; St s[2][6]; bool mono = false;
    static std::vector<Spec> specs (int d)
    {
        switch (d)
        {
            case 1: return { { BandType::HighPass, 170, 0, 0.707 }, { BandType::HighPass, 170, 0, 0.707 }, { BandType::Bell, 3500, 1.5, 1.0 }, { BandType::LowPass, 13000, 0, 0.707 } };
            case 2: return { { BandType::LowShelf, 100, 3.0, 0.8 }, { BandType::Bell, 3200, 3.5, 1.0 }, { BandType::Bell, 7500, 2.0, 2.0 }, { BandType::HighShelf, 11000, -4.0, 0.7 } };
            case 3: return { { BandType::LowShelf, 70, 5.0, 0.8 }, { BandType::Bell, 350, -3.0, 0.8 }, { BandType::Bell, 3000, 2.0, 0.9 }, { BandType::HighShelf, 10000, -4.0, 0.7 } };
            case 4: return {};
            default: return { { BandType::HighPass, 380, 0, 0.8 }, { BandType::HighPass, 380, 0, 0.8 }, { BandType::Bell, 2600, 3.0, 0.9 }, { BandType::LowPass, 8500, 0, 0.7 } };
        }
    }
    void setup (int d, double sampleRate)
    {
        dev = d; fs = sampleRate; mono = devices()[(size_t) std::clamp (d, 0, 4)].mono; const auto sp = specs (d); n = (int) sp.size();
        for (int i = 0; i < n; ++i) c[i] = biquadRBJ (sp[(size_t) i].t, fs, sp[(size_t) i].f, sp[(size_t) i].g, sp[(size_t) i].q);
        for (auto& ch : s) for (auto& x : ch) x = St();
    }
    inline float run (int ch, float x)
    {
        double v = x; for (int i = 0; i < n; ++i) { const double y = c[i].b0 * v + s[ch][i].z1; s[ch][i].z1 = c[i].b1 * v - c[i].a1 * y + s[ch][i].z2; s[ch][i].z2 = c[i].b2 * v - c[i].a2 * y; v = y; }
        return (float) v;
    }
    void process (float* l, float* r, int m)
    {
        for (int i = 0; i < m; ++i)
        {
            if (mono && r) { const float mm = 0.5f * (l[i] + r[i]); l[i] = run (0, mm); r[i] = run (1, mm); }
            else { l[i] = run (0, l[i]); if (r) r[i] = run (1, r[i]); }
        }
    }
};

// ======================================================= 3) cofre de decisoes
inline std::string encodeBands (const BandParams* b)
{
    std::string s; char buf[300];
    for (int i = 0; i < kNumBands; ++i) if (b[i].on)
    {
        std::snprintf (buf, sizeof buf, "%d,%d,%.4f,%.4f,%.5f,%d,%d,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%d;", i, (int) b[i].type, b[i].freq, b[i].gain, b[i].q, b[i].slope, (int) b[i].place, b[i].dyn ? 1 : 0,
                       b[i].thr, b[i].range, b[i].ratio, b[i].attackMs, b[i].holdMs, b[i].releaseMs, b[i].kneeDb, (int) b[i].det, (int) b[i].dir);
        s += buf;
    }
    return s;
}
inline void decodeBands (const std::string& s, BandParams* out)
{
    for (int i = 0; i < kNumBands; ++i) out[i] = BandParams();
    std::stringstream ss (s); std::string item;
    while (std::getline (ss, item, ';'))
    {
        int i, ty, sl, pl, dy, de, di; double f, g, q, thr, rg, ra, at, ho, re, kn;
        if (std::sscanf (item.c_str(), "%d,%d,%lf,%lf,%lf,%d,%d,%d,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%d,%d", &i, &ty, &f, &g, &q, &sl, &pl, &dy, &thr, &rg, &ra, &at, &ho, &re, &kn, &de, &di) != 17 || i < 0 || i >= kNumBands) continue;
        BandParams& p = out[i]; p.on = true; p.type = (BandType) std::clamp (ty, 0, 7); p.freq = f; p.gain = g; p.q = q; p.slope = std::clamp (sl, 0, kNumSlopes - 1); p.place = (Placement) std::clamp (pl, 0, 4);
        p.dyn = dy != 0; p.thr = thr; p.range = rg; p.ratio = ra; p.attackMs = at; p.holdMs = ho; p.releaseMs = re; p.kneeDb = kn; p.det = (DetMode) std::clamp (de, 0, 1); p.dir = (DynDir) std::clamp (di, 0, 1);
    }
}
struct Decision { long long ms = 0; std::string pt, en, kind; std::string bands; };   // bands = encodeBands
inline double eqCurveDb (const BandParams* b, double f, double fs = 48000.0)
{
    double s = 0; for (int i = 0; i < kNumBands; ++i) if (b[i].on && ! b[i].dyn) { const Cascade c = designBand (b[i].type, b[i].slope, fs, b[i].freq, b[i].gain, b[i].q); s += magDb (c, 2.0 * M_PI * f / fs); }
    return s;
}
inline std::string bandSummary (const BandParams& p, bool en)
{
    static const char* tn[8] = { "Bell", "Low Shelf", "High Shelf", "High Pass", "Low Pass", "Notch", "Band Pass", "All Pass" };
    char b[200]; if (p.type == BandType::HighPass || p.type == BandType::LowPass) std::snprintf (b, sizeof b, "%s %.0f Hz", tn[(int) p.type], p.freq);
    else if (p.dyn) std::snprintf (b, sizeof b, "%s %.0f Hz (%s %.1f dB, %s %.0f dB)", tn[(int) p.type], p.freq, en ? "dynamic" : "din\xC3\xA2mica", p.range, en ? "thr" : "lim.", p.thr);
    else std::snprintf (b, sizeof b, "%s %.0f Hz %+.1f dB Q %.2f", tn[(int) p.type], p.freq, p.gain, p.q);
    return b;
}
inline std::vector<DeresProposal> toProposals (const BandParams* b, const std::string& why = "assinatura")
{
    std::vector<DeresProposal> v;
    for (int i = 0; i < kNumBands; ++i) if (b[i].on)
    {
        DeresProposal p; p.type = (int) b[i].type; p.freq = b[i].freq; p.q = b[i].q; p.place = (int) b[i].place; p.slope = b[i].slope; p.why = why;
        if (b[i].dyn) { p.dynamic = true; p.gainDb = b[i].range; p.thrDb = b[i].thr; p.ratio = b[i].ratio; p.attackMs = b[i].attackMs; p.releaseMs = b[i].releaseMs; } else p.gainDb = b[i].gain;
        v.push_back (p);
    }
    return v;
}

// ======================================================= 4) sugestao de preset
struct PresetPick { int index = -1; std::string name; double errBefore = 0, errAfter = 0; };
inline Curve presetCurve (const fp::Def& d)
{
    Curve c = emptyCurve(); BandParams arr[kNumBands]; for (int i = 0; i < kNumBands; ++i) arr[i] = i < (int) d.bands.size() ? d.bands[(size_t) i] : BandParams();
    for (int b = 0; b < kMixBands; ++b) c[(size_t) b] = eqCurveDb (arr, mixBandFreq (b));
    return c;
}
// dev = measured - target (ja alinhado). O preset ideal tem curva ~ -dev.
inline std::vector<PresetPick> rankPresets (const Deviation& dv, int maxN = 3, int categoryOffset = 0)
{
    std::vector<PresetPick> out; if (! dv.ok) return out;
    std::vector<double> want; std::vector<int> idx; for (int b = 0; b < kMixBands; ++b) if (valid (dv.dev[(size_t) b])) { want.push_back (-dv.dev[(size_t) b]); idx.push_back (b); }
    double e0 = 0; for (double w : want) e0 += w * w; e0 = std::sqrt (e0 / want.size());
    const auto& defs = fp::defs();
    for (size_t k = 0; k < defs.size(); ++k)
    {
        const Curve pc = presetCurve (defs[k]); std::vector<double> p; for (int b : idx) p.push_back (pc[(size_t) b]); const double m = med (p);
        double e = 0; for (size_t i = 0; i < p.size(); ++i) { const double d = want[i] - (p[i] - m); e += d * d; } e = std::sqrt (e / want.size());
        PresetPick pk; pk.index = (int) k + categoryOffset; pk.name = defs[k].name; pk.errBefore = e0; pk.errAfter = e; out.push_back (pk);
    }
    std::sort (out.begin(), out.end(), [] (const PresetPick& a, const PresetPick& b) { return a.errAfter < b.errAfter; });
    out.erase (std::remove_if (out.begin(), out.end(), [] (const PresetPick& p) { return p.errAfter > p.errBefore - 0.3; }), out.end());
    if ((int) out.size() > maxN) out.resize ((size_t) maxN);
    return out;
}

// ======================================================= 5) intencao em palavras
inline std::string fold (const std::string& s)
{
    static const char* m = "aaaaaaaceeeeiiiidnooooo/ouuuuypy";   // U+00C0..U+00DF (minusculas U+00E0..U+00FF repetem)
    std::string o;
    for (size_t i = 0; i < s.size(); ++i)
    {
        const unsigned char c = (unsigned char) s[i];
        if (c == 0xC3 && i + 1 < s.size()) { const unsigned char d = (unsigned char) s[++i]; o += (d >= 0x80 && d <= 0xBF) ? m[(d - 0x80) & 31] : '?'; }
        else o += (char) std::tolower (c);
    }
    return o;
}
struct WordsPlan { std::vector<DeresProposal> props; std::vector<std::string> recognised, unknown; std::string explPt, explEn; std::array<double, kMixBands> staticDelta {}, dynMax {}; };
inline WordsPlan planWords (const std::string& text, double amount01, const Curve* curve = nullptr)
{
    WordsPlan W; const double a = clampd (amount01, 0.0, 1.0); const std::string t = fold (text);
    std::vector<std::string> clauses; { std::string cur; for (size_t i = 0; i < t.size(); ++i) { const char c = t[i]; if (c == ',' || c == ';' || c == '+') { clauses.push_back (cur); cur.clear(); } else cur += c; } clauses.push_back (cur); }
    // separa tambem em " e " / " and "
    std::vector<std::string> cl2; for (auto& c : clauses) { std::string x = " " + c + " "; size_t p; while ((p = x.find (" e ")) != std::string::npos || (p = x.find (" and ")) != std::string::npos) { const size_t len = x.compare (p, 3, " e ") == 0 ? 3 : 5; cl2.push_back (x.substr (0, p)); x = " " + x.substr (p + len); } cl2.push_back (x); }
    auto has = [] (const std::string& s, std::initializer_list<const char*> ks) { for (auto* k : ks) if (s.find (k) != std::string::npos) return true; return false; };
    auto cap = [] (double g) { return clampd (g, -4.5, 4.5); };
    auto add = [&] (int type, double f, double q, double g, const char* why) { for (auto& p : W.props) if (std::fabs (std::log (p.freq / f)) < 0.05 && p.type == type) return; DeresProposal p; p.type = type; p.freq = f; p.q = q; p.gainDb = cap (g); p.why = why; W.props.push_back (p); };
    for (const auto& raw : cl2)
    {
        const std::string c = " " + raw + " "; if (c.find_first_not_of (' ') == std::string::npos) continue;
        const bool neg = has (c, { "menos", "less", "sem ", "reduz", "reduc", "tira", "remov", "diminu", "cut ", "lower" });
        const double dir = neg ? -1.0 : 1.0; bool hit = true;
        if (has (c, { "lama", "mud", "sujo", "boxy", "caixa de" })) { add (0, 300, 1.1, -(1.0 + 2.5 * a), "lama"); W.recognised.push_back ("lama/mud"); }
        else if (has (c, { "nasal", "honk", "ocarina" })) { add (0, 900, 2.0, -(1.5 + 2.5 * a), "nasal"); W.recognised.push_back ("nasal"); }
        else if (has (c, { "aspero", "asper", "harsh", "duro", "agressiv", "sibil", "dura ", "shrill", "stridente" })) { for (auto& p : planIntent (Intent::Smooth, Role::General, a, curve).props) W.props.push_back (p); W.recognised.push_back ("harsh"); }
        else if (has (c, { "espaco", "space", "aberto", "abrir", "open" })) { for (auto& p : planIntent (Intent::Space, Role::General, a, curve).props) W.props.push_back (p); W.recognised.push_back ("space"); }
        else if (has (c, { "presenca", "presence", "clare", "clarity", "claro", "clear", "inteligib" })) { add (0, 3200, 0.9, dir * (1.2 + 2.3 * a), "presen\xC3\xA7""a"); W.recognised.push_back ("presen\xC3\xA7""a/presence"); }
        else if (has (c, { "brilh", "bright", " ar ", "air", "agudo", "treble", "high" })) { add (2, 10000, 0.7, dir * (1.0 + 3.0 * a), "brilho"); W.recognised.push_back ("brilho/air"); }
        else if (has (c, { "escur", "dark", "opaco", "dull", "abafad", "muffl" })) { add (2, 9000, 0.7, (neg ? -1.0 : 1.0) * (1.0 + 3.0 * a), "escuro"); add (0, 3200, 0.9, (neg ? -1.0 : 1.0) * (0.8 + 1.5 * a), "presen\xC3\xA7""a"); W.recognised.push_back ("escuro/dark"); }
        else if (has (c, { "grave", "bass", "low end", "sub", "lows" })) { add (1, 100, 0.8, dir * (1.0 + 3.0 * a), "graves"); W.recognised.push_back ("graves/bass"); }
        else if (has (c, { "punch", "peso", "weight", "ataque", "attack" })) { add (0, 90, 0.8, dir * (0.8 + 1.5 * a), "peso"); add (0, 3500, 1.0, dir * (0.5 + 1.0 * a), "ataque"); W.recognised.push_back ("punch/peso"); }
        else if (has (c, { "calor", "quente", "warm" })) { add (0, 150, 0.7, dir * (0.8 + 2.0 * a), "corpo"); add (0, 350, 1.2, -dir * (0.5 + 1.5 * a), "low-mid"); W.recognised.push_back ("calor/warm"); }
        else if (has (c, { "corpo", "body", "cheio", "full" })) { add (0, 200, 0.8, dir * (0.8 + 2.0 * a), "corpo"); W.recognised.push_back ("corpo/body"); }
        else { hit = false; W.unknown.push_back (raw.substr (raw.find_first_not_of (' '))); }
        (void) hit;
    }
    char b1[300], b2[300];
    std::snprintf (b1, sizeof b1, "Interpretei %d pedido(s) em %d banda(s) NORMAIS e edit\xC3\xA1veis (ganhos at\xC3\xA9 \xC2\xB1""4,5 dB). Isto \xC3\xA9 um dicion\xC3\xA1rio de palavras-chave, n\xC3\xA3o compreens\xC3\xA3o de linguagem: confira a previs\xC3\xA3o e ou\xC3\xA7""a.", (int) W.recognised.size(), (int) W.props.size());
    std::snprintf (b2, sizeof b2, "Interpreted %d request(s) into %d NORMAL, editable band(s) (gains up to \xC2\xB1""4.5 dB). This is a keyword dictionary, not language understanding: check the prediction and listen.", (int) W.recognised.size(), (int) W.props.size());
    W.explPt = b1; W.explEn = b2;
    for (const auto& p : W.props) for (int b = 0; b < kMixBands; ++b) { const double d = filterDbAt (p, mixBandFreq (b)); if (p.dynamic) W.dynMax[(size_t) b] += (p.gainDb < 0 ? -1.0 : 1.0) * std::fabs (d); else W.staticDelta[(size_t) b] += d; }
    return W;
}

// ======================================================= 6) ensino
inline std::string explainBand (const BandParams& p, bool en)
{
    const double f = p.freq;
    const char* zp = f < 60 ? "sub-grave (sentido mais do que ouvido; gasta headroom)" : f < 120 ? "fundamental do bumbo e do baixo" : f < 250 ? "corpo e calor" : f < 500 ? "zona de lama (muddiness) e 'caixa'" : f < 1000 ? "corpo dos m\xC3\xA9""dios (nasal/honky perto de 800 Hz\xE2\x80\x93""1 kHz)" : f < 2000 ? "inteligibilidade e ataque" : f < 5000 ? "presen\xC3\xA7""a e clareza (zona sens\xC3\xADvel: facilmente \xC3\xA1spera)" : f < 9000 ? "sibil\xC3\xA2ncia e brilho (\xC3\xA1spero se em excesso)" : "ar e abertura";
    const char* ze = f < 60 ? "sub-bass (felt more than heard; eats headroom)" : f < 120 ? "kick and bass fundamentals" : f < 250 ? "body and warmth" : f < 500 ? "mud zone and 'boxiness'" : f < 1000 ? "midrange body (nasal/honky near 800 Hz\xE2\x80\x93""1 kHz)" : f < 2000 ? "intelligibility and attack" : f < 5000 ? "presence and clarity (sensitive zone: easily harsh)" : f < 9000 ? "sibilance and brightness (harsh if excessive)" : "air and openness";
    char b[900]; const char* z = en ? ze : zp; const double aq = p.q;
    switch (p.type)
    {
        case BandType::Bell:
            if (p.dyn) { std::snprintf (b, sizeof b, en ? "%.0f Hz \xE2\x80\x94 %s. DYNAMIC: it only acts when this region exceeds %.0f dB, by up to %.1f dB (ratio %.1f:1). It leaves the sound alone when it is not needed."
                                                      : "%.0f Hz \xE2\x80\x94 %s. DIN\xC3\x82MICA: s\xC3\xB3 atua quando esta zona passa de %.0f dB, at\xC3\xA9 %.1f dB (ratio %.1f:1). N\xC3\xA3o mexe no som quando n\xC3\xA3o \xC3\xA9 preciso.", f, z, p.thr, p.range, p.ratio); }
            else std::snprintf (b, sizeof b, en ? "%.0f Hz \xE2\x80\x94 %s. Bell %+.1f dB, Q %.2f (%s). %s"
                                               : "%.0f Hz \xE2\x80\x94 %s. Bell %+.1f dB, Q %.2f (%s). %s", f, z, p.gain, aq, aq > 3 ? (en ? "very narrow: surgical" : "muito estreito: cir\xC3\xBArgico") : aq > 1.2 ? (en ? "narrow" : "estreito") : (en ? "wide: musical" : "largo: musical"),
                                p.gain > 0 ? (en ? "A boost makes this region more present." : "Subir real\xC3\xA7""a esta zona.") : (en ? "A cut makes room and reduces build-up here." : "Descer abre espa\xC3\xA7o e reduz ac\xC3\xBAmulo aqui.")); break;
        case BandType::LowShelf: std::snprintf (b, sizeof b, en ? "Low shelf at %.0f Hz, %+.1f dB \xE2\x80\x94 raises or lowers EVERYTHING below it (%s)." : "Low shelf a %.0f Hz, %+.1f dB \xE2\x80\x94 sobe ou desce TUDO abaixo (%s).", f, p.gain, z); break;
        case BandType::HighShelf: std::snprintf (b, sizeof b, en ? "High shelf at %.0f Hz, %+.1f dB \xE2\x80\x94 raises or lowers EVERYTHING above it (%s)." : "High shelf a %.0f Hz, %+.1f dB \xE2\x80\x94 sobe ou desce TUDO acima (%s).", f, p.gain, z); break;
        case BandType::HighPass: std::snprintf (b, sizeof b, en ? "High-pass at %.0f Hz \xE2\x80\x94 removes everything below it (%s are cut). Cleans rumble; too high thins the sound." : "High-pass a %.0f Hz \xE2\x80\x94 remove tudo abaixo (corta %s). Limpa rumble; demasiado alto torna o som fino.", f, z); break;
        case BandType::LowPass: std::snprintf (b, sizeof b, en ? "Low-pass at %.0f Hz \xE2\x80\x94 removes everything above it. Tames hiss and harshness; too low dulls the sound." : "Low-pass a %.0f Hz \xE2\x80\x94 remove tudo acima. Doma chiado e aspereza; demasiado baixo torna o som opaco.", f); break;
        case BandType::Notch: std::snprintf (b, sizeof b, en ? "Notch at %.0f Hz \xE2\x80\x94 a very narrow cut for a single resonance or hum (%s)." : "Notch a %.0f Hz \xE2\x80\x94 corte muito estreito para uma ressson\xC3\xA2ncia ou zumbido (%s).", f, z); break;
        case BandType::BandPass: std::snprintf (b, sizeof b, en ? "Band-pass at %.0f Hz \xE2\x80\x94 keeps only this region (useful for listening or special effects)." : "Band-pass a %.0f Hz \xE2\x80\x94 deixa passar s\xC3\xB3 esta zona (\xC3\xBAtil para ouvir ou efeitos).", f); break;
        default: std::snprintf (b, sizeof b, en ? "All-pass at %.0f Hz \xE2\x80\x94 changes phase, not level." : "All-pass a %.0f Hz \xE2\x80\x94 muda a fase, n\xC3\xA3o o n\xC3\xADvel.", f); break;
    }
    std::string s = b;
    if (p.place == Placement::Mid) s += en ? " [acts on the centre (Mid) only]" : " [atua s\xC3\xB3 no centro (Mid)]"; else if (p.place == Placement::Side) s += en ? " [acts on the sides (Side) only]" : " [atua s\xC3\xB3 nos lados (Side)]";
    return s;
}

// ======================================================= 7) ficha da sessao (HTML bilingue, sem dependencias externas)
struct SheetData
{
    std::string track, date, version = "1.6.0";
    BandParams now[kNumBands], first[kNumBands]; bool hasFirst = false;
    Curve measured = emptyCurve(), target = emptyCurve(); std::string targetNamePt, targetNameEn; Deviation dev;
    std::vector<Decision> decisions; std::vector<std::string> transPt, transEn; double retained = -1; std::string transDevPt, transDevEn;
};
inline std::string esc (const std::string& s) { std::string o; for (char c : s) { if (c == '&') o += "&amp;"; else if (c == '<') o += "&lt;"; else if (c == '>') o += "&gt;"; else if (c == '"') o += "&quot;"; else o += c; } return o; }
inline std::string t2 (const std::string& pt, const std::string& en) { return "<span class=pt>" + esc (pt) + "</span><span class=en>" + esc (en) + "</span>"; }
inline std::string sheetHtml (const SheetData& S)
{
    std::ostringstream o;
    o << "<!doctype html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'><title>PrimEQ Pro - " << esc (S.track) << "</title><style>"
         ":root{--bg:#0f1317;--p:#171d23;--t:#e8eef2;--d:#8fa0ab;--a:#4fd1c5;--r:#e07a8c;--g:#9ccc65}@media print{:root{--bg:#fff;--p:#f4f6f8;--t:#14181c;--d:#55626b}}"
         "body{background:var(--bg);color:var(--t);font:15px/1.5 system-ui,Segoe UI,Roboto,sans-serif;margin:0;padding:24px}.w{max-width:960px;margin:auto}h1{font-size:26px;margin:0}h2{font-size:15px;letter-spacing:.08em;color:var(--a);margin:28px 0 8px;text-transform:uppercase}"
         ".c{background:var(--p);border-radius:12px;padding:16px;margin:10px 0}.d{color:var(--d)}.en{display:block;color:var(--d);font-size:.92em}.pt{display:block}body.only-pt .en{display:none}body.only-en .pt{display:none}body.only-en .en{color:var(--t);font-size:1em}"
         "table{border-collapse:collapse;width:100%}td,th{padding:6px 8px;border-bottom:1px solid #2a343c;text-align:left;font-size:14px}.tb button{background:var(--p);color:var(--t);border:1px solid #2a343c;border-radius:8px;padding:4px 10px;margin-right:6px;cursor:pointer}"
         "svg{width:100%;height:auto}@media print{.tb{display:none}}</style></head><body><div class=w>"
         "<div class=tb><button onclick=\"document.body.className=''\">PT + EN</button><button onclick=\"document.body.className='only-pt'\">PT</button><button onclick=\"document.body.className='only-en'\">EN</button></div>"
         "<h1>PrimEQ Pro by Piradex</h1><div class=d>" << t2 ("Ficha da sess\xC3\xA3o", "Session sheet") << " \xC2\xB7 " << esc (S.track) << " \xC2\xB7 " << esc (S.date) << " \xC2\xB7 v" << esc (S.version) << "</div>";
    // curvas
    auto yOf = [] (double db) { return 120.0 - db * 8.0; }; auto xOf = [] (double f) { return 40.0 + 540.0 * std::log (f / 20.0) / std::log (1000.0); };
    o << "<h2>" << t2 ("Curva do EQ: antes e depois", "EQ curve: before and after") << "</h2><div class=c><svg viewBox='0 0 600 240'><rect x=40 y=24 width=540 height=192 fill='none' stroke='#2a343c'/>";
    for (double db : { -12.0, -6.0, 0.0, 6.0, 12.0 }) o << "<line x1=40 x2=580 y1=" << yOf (db) + 24 - 24 << " y2=" << yOf (db) << " stroke='#2a343c'/><text x=6 y=" << yOf (db) + 4 << " fill='#8fa0ab' font-size=10>" << (int) db << "</text>";
    for (double f : { 100.0, 1000.0, 10000.0 }) o << "<line y1=24 y2=216 x1=" << xOf (f) << " x2=" << xOf (f) << " stroke='#2a343c'/><text y=232 x=" << xOf (f) - 12 << " fill='#8fa0ab' font-size=10>" << (f >= 1000 ? std::to_string ((int) (f / 1000)) + "k" : std::to_string ((int) f)) << "</text>";
    auto path = [&] (const BandParams* b, const char* col, const char* extra)
    {
        o << "<polyline fill=none stroke='" << col << "' stroke-width=2 " << extra << " points='";
        for (int i = 0; i <= 160; ++i) { const double f = 20.0 * std::pow (1000.0, i / 160.0); const double y = std::clamp (yOf (eqCurveDb (b, f)), 24.0, 216.0); o << xOf (f) << "," << y << " "; }
        o << "'/>";
    };
    if (S.hasFirst) path (S.first, "#8fa0ab", "stroke-dasharray='5 4'");
    path (S.now, "#4fd1c5", "");
    o << "</svg><div class=d>" << t2 ("Cinzento tracejado = antes (primeiro estado guardado) \xC2\xB7 verde = agora. S\xC3\xB3 bandas est\xC3\xA1ticas; as din\xC3\xA2micas n\xC3\xA3o entram neste gr\xC3\xA1""fico.", "Dashed grey = before (first stored state) \xC2\xB7 teal = now. Static bands only; dynamic bands are not drawn here.") << "</div></div>";
    // bandas
    o << "<h2>" << t2 ("Bandas ativas", "Active bands") << "</h2><div class=c><table><tr><th>#</th><th>" << t2 ("Banda", "Band") << "</th></tr>";
    for (int i = 0; i < kNumBands; ++i) if (S.now[i].on) o << "<tr><td>" << i + 1 << "</td><td>" << esc (bandSummary (S.now[i], false)) << "</td></tr>";
    o << "</table></div>";
    // alvo
    if (S.dev.ok)
    {
        o << "<h2>" << t2 ("Equil\xC3\xAD""brio tonal vs alvo", "Tonal balance vs target") << "</h2><div class=c><div>" << t2 ("Alvo: " + S.targetNamePt, "Target: " + S.targetNameEn) << " \xC2\xB7 RMS " << (int) std::lround (S.dev.rms * 10) / 10.0 << " dB</div><ul>";
        for (size_t i = 0; i < S.dev.linesPt.size(); ++i) o << "<li>" << t2 (S.dev.linesPt[i], S.dev.linesEn[i]) << "</li>";
        o << "</ul><div class=d>" << t2 ("O alvo \xC3\xA9 uma curva heur\xC3\xADstica de partida, n\xC3\xA3o uma regra.", "The target is a heuristic starting curve, not a rule.") << "</div></div>";
    }
    if (S.retained >= 0)
    {
        o << "<h2>" << t2 ("Tradutor", "Translator") << "</h2><div class=c><div>" << t2 (S.transDevPt, S.transDevEn) << ": " << (int) std::lround (S.retained) << "% " << t2 ("da energia medida continua aud\xC3\xADvel", "of the measured energy stays audible") << "</div><ul>";
        for (size_t i = 0; i < S.transPt.size(); ++i) o << "<li>" << t2 (S.transPt[i], S.transEn[i]) << "</li>";
        o << "</ul><div class=d>" << t2 ("Resposta gen\xC3\xA9rica de uma categoria de equipamento, n\xC3\xA3o de um modelo concreto.", "Generic response of an equipment category, not of a specific model.") << "</div></div>";
    }
    o << "<h2>" << t2 ("Decis\xC3\xB5""es", "Decisions") << "</h2><div class=c>";
    if (S.decisions.empty()) o << "<div class=d>" << t2 ("Sem decis\xC3\xB5""es registadas.", "No decisions recorded.") << "</div>";
    for (size_t i = 0; i < S.decisions.size(); ++i) o << "<div style='padding:6px 0;border-bottom:1px solid #2a343c'><b>" << i + 1 << ".</b> " << t2 (S.decisions[i].pt, S.decisions[i].en.empty() ? S.decisions[i].pt : S.decisions[i].en) << "</div>";
    o << "</div><div class=d style='margin-top:24px'>" << t2 ("Gerado pelo PrimEQ Pro by Piradex. Medi\xC3\xA7\xC3\xB5""es e previs\xC3\xB5""es s\xC3\xA3o estimativas; a decis\xC3\xA3o final \xC3\xA9 sempre de ouvido.", "Generated by PrimEQ Pro by Piradex. Measurements and predictions are estimates; the final call is always by ear.") << "</div></div></body></html>";
    return o.str();
}
} // namespace pq::studio
