// =============================================================================
//  PRIMEQ PRO by Piradex — nucleo do EQ (24 bandas, IIR double, dinamica por banda)
//
//  Tempo real: sem alocacao em process(); tudo pre-alocado em prepare().
//  Fluxo por sub-bloco de 32 amostras:
//    1) copia a entrada (ou sidechain externo) -> fonte dos detetores (nunca e alterada pelos filtros)
//    2) por banda: detetor (band-pass + Peak/RMS) -> gain computer -> envelope (attack/hold/release) em dB
//    3) suavizacao de freq/ganho/Q + coeficientes novos so se mudaram
//    4) filtragem em serie no canal certo (Stereo / L / R / Mid / Side)
//  O sidechain externo so alimenta o detetor: nunca e misturado na saida.
// =============================================================================
#pragma once
#include "BandDesign.h"
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace pq
{
class EqCore
{
public:
    static constexpr int kSub = 32;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        smoothK = 1.0 - std::exp (-(double) kSub / (fs * 0.005));
        rmsK    = 1.0 - std::exp (-1.0 / (fs * 0.010));
        for (auto& r : rt) r = BandRt();
        for (auto& s : lsn) s = BiquadState();
        lsnKey = -1; tsFast = tsSlow = tsM = 0;
    }
    void reset() { for (auto& r : rt) r.resetStates(); for (auto& s : lsn) s = BiquadState(); }

    void setParams (const BandParams* p) { for (int i = 0; i < kNumBands; ++i) params[(size_t) i] = p[i]; }
    void setListen (int band) { listen = band; }
    // EQ Transiente/Tonal: mascara complementar mT + mS = 1 (zero latencia). sep 0..1 = rigidez da fronteira; smoothMs = libertacao da mascara
    void setTransient (double sep, double smoothMs) { tsSep = clampd (sep, 0.0, 1.0); tsSmoothMs = clampd (smoothMs, 1.0, 100.0); }
    float transientNow() const { return tsNow.load (std::memory_order_relaxed); }

    // L/R processados no lugar. scL/scR: sidechain externo (nullptr = interno). filtering=false: so atualiza as dinamicas.
    void process (double* L, double* R, int n, const double* scL, const double* scR, bool filtering)
    {
        for (int off = 0; off < n; off += kSub)
        {
            const int m = std::min (kSub, n - off);
            double srcL[kSub], srcR[kSub];
            for (int i = 0; i < m; ++i)
            {
                srcL[i] = scL ? scL[off + i] : L[off + i];
                srcR[i] = scR ? scR[off + i] : (scL ? scL[off + i] : R[off + i]);
            }
            double* l = L + off; double* r = R + off;
            double lsrcL[kSub], lsrcR[kSub];
            const bool doListen = filtering && listen >= 0 && params[(size_t) listen].on;
            if (doListen) for (int i = 0; i < m; ++i) { lsrcL[i] = l[i]; lsrcR[i] = r[i]; }

            bool anyTs = false;
            for (int b = 0; b < kNumBands; ++b) if (params[(size_t) b].on && params[(size_t) b].ts != 0) { anyTs = true; break; }
            double mT[kSub];
            if (anyTs && filtering) tsMask (srcL, srcR, m, mT);
            for (int b = 0; b < kNumBands; ++b)
            {
                const BandParams& p = params[(size_t) b]; BandRt& rb = rt[(size_t) b];
                if (! p.on) { if (rb.active) { rb.active = false; rb.env = 0.0; rb.resetStates(); } continue; }
                if (! rb.active || rb.place != p.place) activate (rb, p);

                const bool dynOk = p.dyn && typeHasGain (p.type);
                if (dynOk) runDetector (rb, p, srcL, srcR, m); else decayEnv (rb, m);

                // suavizacao de parametros
                const double tlf = std::log2 (safeFreq (p.freq, fs));
                rb.lf += (tlf - rb.lf) * smoothK; rb.g += (clampd (p.gain, -kMaxGainDb, kMaxGainDb) - rb.g) * smoothK; rb.q += (clampd (p.q, kMinQ, kMaxQ) - rb.q) * smoothK;
                if (std::fabs (tlf - rb.lf) < 1e-7) rb.lf = tlf;
                if (std::fabs (p.gain - rb.g) < 1e-6) rb.g = clampd (p.gain, -kMaxGainDb, kMaxGainDb);
                if (std::fabs (p.q - rb.q) < 1e-6) rb.q = clampd (p.q, kMinQ, kMaxQ);

                const double gEff = clampd (rb.g + rb.env, -36.0, 36.0);
                if (rb.dirty (p, gEff))
                {
                    rb.casc = designBand (p.type, p.slope, fs, std::exp2 (rb.lf), gEff, rb.q);
                    rb.remember (p, gEff);
                }
                if (! filtering) continue;
                if (p.ts != 0 && anyTs && filtering)
                {
                    // o filtro corre sempre sobre o sinal completo (estados coerentes); so o resultado e misturado com a mascara
                    double a0[kSub], b0[kSub]; for (int i = 0; i < m; ++i) { a0[i] = l[i]; b0[i] = r[i]; }
                    filterBand (rb, p.place, l, r, m);
                    for (int i = 0; i < m; ++i) { const double w = p.ts == 1 ? mT[i] : 1.0 - mT[i]; l[i] = a0[i] + w * (l[i] - a0[i]); r[i] = b0[i] + w * (r[i] - b0[i]); }
                }
                else filterBand (rb, p.place, l, r, m);
            }
            if (doListen) listenOut (lsrcL, lsrcR, l, r, m);
        }
    }

    double dynDb (int b) const { return rt[(size_t) b].env; }
    const Cascade& cascade (int b) const { return rt[(size_t) b].casc; }
    bool bandActive (int b) const { return rt[(size_t) b].active; }

    // compensacao automatica: media (em dB, por oitava) da resposta total entre 40 Hz e 14 kHz, com o sinal trocado
    double autoGainDb() const
    {
        double acc = 0; int cnt = 0;
        for (int i = 0; i < 48; ++i)
        {
            const double f = 40.0 * std::pow (14000.0 / 40.0, i / 47.0), w = 2.0 * M_PI * f / fs; double db = 0;
            for (int b = 0; b < kNumBands; ++b)
            {
                if (! rt[(size_t) b].active) continue;
                const double wgt = (params[(size_t) b].place == Placement::Stereo) ? 1.0 : 0.5;
                db += wgt * magDb (rt[(size_t) b].casc, w);
            }
            acc += db; ++cnt;
        }
        return -acc / std::max (1, cnt);
    }

private:
    struct BandRt
    {
        bool active = false; Placement place = Placement::Stereo;
        double lf = 10, g = 0, q = 1, env = 0; int hold = 0;
        Cascade casc;
        BiquadState st[2][kMaxStages];
        // detetor
        Coefs dc; double dfKey = -1, dqKey = -1; BiquadState ds[2]; double ms[2] = { 0, 0 };
        // cache da ultima desenho
        BandType kT = BandType::Bell; int kS = -1; double kLf = -1e9, kG = -1e9, kQ = -1e9;

        void resetStates() { for (auto& c : st) for (auto& s : c) s = BiquadState(); ds[0] = ds[1] = BiquadState(); ms[0] = ms[1] = 0; }
        bool dirty (const BandParams& p, double gEff) const { return p.type != kT || p.slope != kS || lf != kLf || gEff != kG || q != kQ; }
        void remember (const BandParams& p, double gEff) { kT = p.type; kS = p.slope; kLf = lf; kG = gEff; kQ = q; }
    };

    void activate (BandRt& r, const BandParams& p)
    {
        r.active = true; r.place = p.place; r.resetStates(); r.env = 0; r.hold = 0;
        r.lf = std::log2 (safeFreq (p.freq, fs)); r.g = clampd (p.gain, -kMaxGainDb, kMaxGainDb); r.q = clampd (p.q, kMinQ, kMaxQ);
        r.kS = -1; r.dfKey = -1;
    }

    void decayEnv (BandRt& r, int m)
    {
        if (r.env == 0.0) return;
        r.env *= std::exp (-(double) m / (fs * 0.020));
        if (std::fabs (r.env) < 1e-4) r.env = 0.0;
        r.hold = 0;
    }

    static double gainComputer (double level, const BandParams& p)
    {
        double over = (p.dir == DynDir::Above) ? level - p.thr : p.thr - level;
        const double k = std::max(0.0, p.kneeDb);
        double o;
        if (k > 0.0 && 2.0 * over < -k) o = 0.0;
        else if (k > 0.0 && 2.0 * std::fabs (over) <= k) o = (over + k * 0.5) * (over + k * 0.5) / (2.0 * k);
        else o = std::max (0.0, over);
        const double amt = o * (1.0 - 1.0 / std::max (1.0, p.ratio));
        const double mag = std::min (std::fabs (p.range), amt);
        return p.range < 0 ? -mag : mag;
    }

    void runDetector (BandRt& r, const BandParams& p, const double* sl, const double* sr, int m)
    {
        const double df = safeFreq (p.freq, fs), dq = clampd (p.q, 0.5, 6.0);
        if (df != r.dfKey || dq != r.dqKey) { r.dc = biquadRBJ (BandType::BandPass, fs, df, 0.0, dq); r.dfKey = df; r.dqKey = dq; }
        const double aC = 1.0 - std::exp (-1.0 / (std::max (0.01, p.attackMs) * 0.001 * fs));
        const double rC = 1.0 - std::exp (-1.0 / (std::max (1.0, p.releaseMs) * 0.001 * fs));
        const int holdN = (int) (p.holdMs * 0.001 * fs);
        const bool rms = (p.det == DetMode::Rms), stereo = (p.place == Placement::Stereo);
        for (int i = 0; i < m; ++i)
        {
            double x0, x1 = 0;
            switch (p.place)
            {
                case Placement::Left:  x0 = sl[i]; break;
                case Placement::Right: x0 = sr[i]; break;
                case Placement::Mid:   x0 = 0.5 * (sl[i] + sr[i]); break;
                case Placement::Side:  x0 = 0.5 * (sl[i] - sr[i]); break;
                default:               x0 = sl[i]; x1 = sr[i]; break;
            }
            double y0 = tick (r.dc, r.ds[0], x0), lev;
            if (rms) { r.ms[0] += (y0 * y0 - r.ms[0]) * rmsK; lev = std::sqrt (r.ms[0]); } else lev = std::fabs (y0);
            if (stereo)
            {
                const double y1 = tick (r.dc, r.ds[1], x1); double l1;
                if (rms) { r.ms[1] += (y1 * y1 - r.ms[1]) * rmsK; l1 = std::sqrt (r.ms[1]); } else l1 = std::fabs (y1);
                lev = std::max (lev, l1);
            }
            const double levDb = 20.0 * std::log10 (std::max (lev, 1e-9));
            const double T = gainComputer (levDb, p);
            if (std::fabs (T) > std::fabs (r.env) + 1e-12) { r.env += (T - r.env) * aC; r.hold = holdN; }
            else if (r.hold > 0) --r.hold;
            else r.env += (T - r.env) * rC;
        }
        if (std::fabs (r.env) < 1e-6) r.env = 0.0;
    }

    // detetor de transientes: envelope rapido (|x|, 0.2 ms / 20 ms) contra uma versao lenta dele (50 ms); a diferenca em dB, com limiar e
    // largura dependentes de "separation", da a mascara 0..1, suavizada (subida 0.5 ms, descida = smooth)
    void tsMask (const double* sl, const double* sr, int m, double* out)
    {
        const double ka = 1.0 - std::exp (-1.0 / (0.0002 * fs)), kr = 1.0 - std::exp (-1.0 / (0.020 * fs)), ks = 1.0 - std::exp (-1.0 / (0.050 * fs));
        const double mUp = 1.0 - std::exp (-1.0 / (0.0005 * fs)), mDn = 1.0 - std::exp (-1.0 / (tsSmoothMs * 0.001 * fs));
        const double c = 6.0 - 4.5 * tsSep, w = 12.0 - 9.0 * tsSep;
        double acc = 0;
        for (int i = 0; i < m; ++i)
        {
            const double a = std::fabs (0.5 * (sl[i] + sr[i]));
            tsFast += (a - tsFast) * (a > tsFast ? ka : kr);
            tsSlow += (tsFast - tsSlow) * ks;
            const double d = 20.0 * std::log10 ((tsFast + 1e-7) / (tsSlow + 1e-7));
            double t = clampd ((d - c) / w, 0.0, 1.0); t = t * t * (3.0 - 2.0 * t);
            tsM += (t - tsM) * (t > tsM ? mUp : mDn);
            out[i] = tsM; acc += tsM;
        }
        tsNow.store ((float) (acc / std::max (1, m)), std::memory_order_relaxed);
    }

    void filterBand (BandRt& r, Placement pl, double* l, double* rr, int m)
    {
        const int N = r.casc.n;
        switch (pl)
        {
            case Placement::Stereo:
                for (int i = 0; i < m; ++i) { double a = l[i], b = rr[i]; for (int s = 0; s < N; ++s) { a = tick (r.casc.c[s], r.st[0][s], a); b = tick (r.casc.c[s], r.st[1][s], b); } l[i] = a; rr[i] = b; }
                break;
            case Placement::Left:
                for (int i = 0; i < m; ++i) { double a = l[i]; for (int s = 0; s < N; ++s) a = tick (r.casc.c[s], r.st[0][s], a); l[i] = a; }
                break;
            case Placement::Right:
                for (int i = 0; i < m; ++i) { double b = rr[i]; for (int s = 0; s < N; ++s) b = tick (r.casc.c[s], r.st[1][s], b); rr[i] = b; }
                break;
            case Placement::Mid:
                for (int i = 0; i < m; ++i) { double M = 0.5 * (l[i] + rr[i]), S = 0.5 * (l[i] - rr[i]); for (int s = 0; s < N; ++s) M = tick (r.casc.c[s], r.st[0][s], M); l[i] = M + S; rr[i] = M - S; }
                break;
            case Placement::Side:
                for (int i = 0; i < m; ++i) { double M = 0.5 * (l[i] + rr[i]), S = 0.5 * (l[i] - rr[i]); for (int s = 0; s < N; ++s) S = tick (r.casc.c[s], r.st[1][s], S); l[i] = M + S; rr[i] = M - S; }
                break;
        }
    }

    // Listen: ouve so a zona da banda (band-pass), em vez do sinal processado
    void listenOut (const double* inL, const double* inR, double* l, double* r, int m)
    {
        const BandParams& p = params[(size_t) listen];
        if (lsnKey != listen) { lsn[0] = lsn[1] = BiquadState(); lsnKey = listen; }
        lsc = biquadRBJ (BandType::BandPass, fs, p.freq, 0.0, clampd (p.q, 0.5, 8.0));
        for (int i = 0; i < m; ++i)
        {
            switch (p.place)
            {
                case Placement::Left:  { const double y = tick (lsc, lsn[0], inL[i]); l[i] = r[i] = y; break; }
                case Placement::Right: { const double y = tick (lsc, lsn[1], inR[i]); l[i] = r[i] = y; break; }
                case Placement::Mid:   { const double y = tick (lsc, lsn[0], 0.5 * (inL[i] + inR[i])); l[i] = r[i] = y; break; }
                case Placement::Side:  { const double y = tick (lsc, lsn[1], 0.5 * (inL[i] - inR[i])); l[i] = r[i] = y; break; }
                default:               l[i] = tick (lsc, lsn[0], inL[i]); r[i] = tick (lsc, lsn[1], inR[i]); break;
            }
        }
    }

    double fs = 48000, smoothK = 0.1, rmsK = 0.01;
    double tsSep = 0.6, tsSmoothMs = 8.0, tsFast = 0, tsSlow = 0, tsM = 0; std::atomic<float> tsNow { 0.f };
    std::array<BandParams, kNumBands> params {};
    std::array<BandRt, kNumBands> rt {};
    int listen = -1, lsnKey = -1; Coefs lsc; BiquadState lsn[2];
};
} // namespace pq
