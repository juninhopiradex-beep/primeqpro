// =============================================================================
//  PrimEQ Pro by Piradex — constantes e modelo de parametros (C++ puro, sem JUCE)
// =============================================================================
#pragma once
#include <cstddef>
#include <cmath>
#include <cstdio>
#ifndef M_PI
 #define M_PI 3.14159265358979323846   // MSVC sem _USE_MATH_DEFINES
#endif

namespace pq
{
constexpr int kNumBands = 24;

enum class BandType  { Bell = 0, LowShelf, HighShelf, HighPass, LowPass, Notch, BandPass, AllPass };
enum class Placement { Stereo = 0, Left, Right, Mid, Side };
enum class PhaseMode { ZeroLatency = 0, Natural, Linear };
enum class DetMode   { Peak = 0, Rms };
enum class DynDir    { Above = 0, Below };

// declives dos cortes (indice -> dB/oitava, ordem Butterworth). 0 dB/oct = Brickwall finito (ordem 32).
constexpr int kNumSlopes = 9;
constexpr int kSlopeDb[kNumSlopes]    = { 6, 12, 18, 24, 36, 48, 72, 96, 0 };
constexpr int kSlopeOrder[kNumSlopes] = { 1, 2, 3, 4, 6, 8, 12, 16, 32 };

constexpr double kMinFreq = 10.0, kMaxFreq = 30000.0;
constexpr double kMaxGainDb = 30.0;
constexpr double kMinQ = 0.025, kMaxQ = 40.0;

inline bool typeHasGain (BandType t) { return t == BandType::Bell || t == BandType::LowShelf || t == BandType::HighShelf; }
inline bool typeIsCut   (BandType t) { return t == BandType::HighPass || t == BandType::LowPass; }

struct BandParams
{
    bool      on = false;
    BandType  type = BandType::Bell;
    double    freq = 1000.0, gain = 0.0, q = 1.0;
    int       slope = 1;                    // indice em kSlopeDb
    Placement place = Placement::Stereo;
    // dinamica
    bool      dyn = false;
    double    thr = -30.0;                  // dB
    double    range = -6.0;                 // dB com sinal: <0 corta, >0 reforca
    double    ratio = 2.0;
    double    attackMs = 10.0, holdMs = 0.0, releaseMs = 100.0;
    double    kneeDb = 6.0;
    DetMode   det = DetMode::Peak;
    DynDir    dir = DynDir::Above;
    int       ts = 0;                       // alvo Transiente/Tonal: 0 = Ambos, 1 = so transientes, 2 = so tonal
};
} // namespace pq
