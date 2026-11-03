// =============================================================================
//  PRIMEQ PRO by Piradex — Morph entre dois estados de EQ (A e B) guardados pelo utilizador
//
//  Interpolacao por banda, m em [0,1] (0 = A, 1 = B):
//    freq, Q, ratio, attack, hold(+1), release : geometrica (log) ; ganho, threshold, range, knee : linear ;
//    tipo, declive, placement, dyn, detetor, direcao : do estado mais proximo (m < 0,5 -> A).
//  Banda so num dos estados: se tem ganho (Bell/shelf) o ganho e atenuado linearmente (entra/sai suavemente);
//  senao (cortes, notch, band-pass...) fica ligada enquanto o seu peso for > 0,5. Nao e ML.
// =============================================================================
#pragma once
#include "../Shared/Params.h"
#include <algorithm>
#include <cmath>

namespace pq
{
inline double lerpD (double a, double b, double t) { return a + (b - a) * t; }
inline double geoD  (double a, double b, double t) { return std::exp (std::log (std::max (1e-9, a)) * (1 - t) + std::log (std::max (1e-9, b)) * t); }

inline BandParams morphBand (const BandParams& A, const BandParams& B, double m)
{
    m = std::clamp (m, 0.0, 1.0);
    if (A.on && B.on)
    {
        BandParams o = (m < 0.5) ? A : B;
        o.freq = geoD (A.freq, B.freq, m); o.q = geoD (A.q, B.q, m); o.gain = lerpD (A.gain, B.gain, m);
        o.thr = lerpD (A.thr, B.thr, m); o.range = lerpD (A.range, B.range, m); o.ratio = geoD (A.ratio, B.ratio, m); o.kneeDb = lerpD (A.kneeDb, B.kneeDb, m);
        o.attackMs = geoD (A.attackMs, B.attackMs, m); o.releaseMs = geoD (A.releaseMs, B.releaseMs, m); o.holdMs = lerpD (A.holdMs, B.holdMs, m);
        return o;
    }
    if (! A.on && ! B.on) return (m < 0.5) ? A : B;
    BandParams o = A.on ? A : B; const double wgt = A.on ? (1.0 - m) : m;
    if (typeHasGain (o.type)) { o.gain *= wgt; o.on = wgt > 1e-3; if (o.dyn) o.range *= wgt; }
    else o.on = wgt > 0.5;
    return o;
}
inline void morphBands (const BandParams* A, const BandParams* B, double m, BandParams* out) { for (int i = 0; i < kNumBands; ++i) out[i] = morphBand (A[i], B[i], m); }
} // namespace pq
