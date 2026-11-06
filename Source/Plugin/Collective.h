// =============================================================================
//  PRIMEQ PRO by Piradex — vista colectiva das pistas (mesmo processo): niveis medios por banda, pan e zonas de choque.
//  Logica pura (sem JUCE): serve o Mapa 3D e o overlay colectivo do grafico. So considera instancias com "ANALISAR" ligado.
// =============================================================================
#pragma once
#include "MixRegistry.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace pq
{
struct CollTrack
{
    int slot = -1; std::string name; bool self = false; int group = 1;
    std::array<float, kMixBands> db {}, pan {}; float peakDb = -120.f;
};

struct Collective
{
    std::vector<CollTrack> tracks;
    std::array<float, kMixBands> clash {};      // 0..1 por banda: duas ou mais pistas fortes e proximas em nivel
    float refDb = -120.f; int clashBands = 0;
    static constexpr float kRange = 54.f, kClashWindow = 30.f, kClashDiff = 6.f;

    // nivel medio (em potencia) dos ultimos 'avgFrames' quadros de 25 ms
    static Collective gather (const MixRegistry& reg, const void* self, int avgFrames = 40, bool sameGroupOnly = false)
    {
        Collective c; const auto list = reg.list (self); int myGroup = 1;
        for (auto& t : list) if (t.isSelf) myGroup = t.group;
        std::vector<std::array<float, kMixBands>> fr;
        for (auto& t : list)
        {
            if (! t.analysing || t.head < 4) continue; if (sameGroupOnly && t.group != myGroup) continue;
            const int n = reg.copyFrames (t.slot, avgFrames, fr); if (n < 4) continue;
            CollTrack ct; ct.slot = t.slot; ct.name = t.name; ct.self = t.isSelf; ct.group = t.group;
            for (int b = 0; b < kMixBands; ++b)
            {
                double s = 0.0; for (const auto& f : fr) s += std::pow (10.0, (double) std::max (-120.f, f[(size_t) b]) / 10.0);
                ct.db[(size_t) b] = (float) (10.0 * std::log10 (s / (double) fr.size() + 1e-12)); ct.peakDb = std::max (ct.peakDb, ct.db[(size_t) b]);
            }
            const SpatialStats st = reg.stats (t.slot); for (int b = 0; b < kMixBands; ++b) ct.pan[(size_t) b] = std::clamp (st.pan[b], -1.f, 1.f);
            c.tracks.push_back (std::move (ct));
        }
        analyse (c); return c;
    }

    static void analyse (Collective& c)
    {
        c.refDb = -120.f; c.clashBands = 0; c.clash.fill (0.f);
        for (const auto& t : c.tracks) c.refDb = std::max (c.refDb, t.peakDb);
        if (c.tracks.size() < 2) return;
        for (int b = 0; b < kMixBands; ++b)
        {
            float l1 = -1e9f, l2 = -1e9f;
            for (const auto& t : c.tracks) { const float v = t.db[(size_t) b]; if (v > l1) { l2 = l1; l1 = v; } else if (v > l2) l2 = v; }
            if (l2 < c.refDb - kClashWindow) continue;
            const float diff = l1 - l2; if (diff >= kClashDiff) continue;
            const float sev = (1.f - diff / kClashDiff) * std::clamp ((l2 - (c.refDb - kClashWindow)) / 20.f, 0.f, 1.f);
            c.clash[(size_t) b] = sev; if (sev > 0.15f) ++c.clashBands;
        }
    }
    // nivel relativo ao mais forte (<= 0), saturado em -kRange
    float rel (float db) const { return std::max (-kRange, db - refDb); }
    float norm01 (float db) const { return std::clamp ((rel (db) + kRange) / kRange, 0.f, 1.f); }
};
} // namespace pq
