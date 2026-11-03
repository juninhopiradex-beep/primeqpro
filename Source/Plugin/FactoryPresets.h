// =============================================================================
//  PRIMEQ PRO by Piradex — presets de fabrica ORIGINAIS (organizados por categoria, "Categoria/Nome")
//
//  Pontos de partida baseados em praticas gerais de mistura/masterizacao e no repertorio local (Kizomba, Semba, Kuduro, Amapiano, Afrobeat).
//  Sao ponto de partida: ajuste sempre de ouvido. Nenhum valor foi copiado de produtos de terceiros.
// =============================================================================
#pragma once
#include "../Shared/Params.h"
#include <vector>

namespace pq::fp
{
struct Fx { int phase = 0, ch = 0, os = 1; float drive = 35, mix = 60; bool dh = false; float dhAmt = 50, dhLo = 2500, dhHi = 9000, dhSens = 4; };
struct Def { const char* name; std::vector<BandParams> bands; Fx fx; };

inline BandParams mk (BandType t, double f, double g, double q, int slope = 1) { BandParams b; b.on = true; b.type = t; b.freq = f; b.gain = g; b.q = q; b.slope = slope; return b; }
inline BandParams Bl (double f, double g, double q = 1.0) { return mk (BandType::Bell, f, g, q); }
inline BandParams LS (double f, double g, double q = 0.707) { return mk (BandType::LowShelf, f, g, q); }
inline BandParams HS (double f, double g, double q = 0.707) { return mk (BandType::HighShelf, f, g, q); }
inline BandParams HP (double f, int slope = 3) { return mk (BandType::HighPass, f, 0, 0.707, slope); }
inline BandParams LP (double f, int slope = 3) { return mk (BandType::LowPass, f, 0, 0.707, slope); }
inline BandParams Nt (double f, double q = 8.0) { return mk (BandType::Notch, f, 0, q); }
inline BandParams Dn (double f, double range, double q, double thr, double ratio = 2.5, double att = 10, double rel = 120)
{ BandParams b = mk (BandType::Bell, f, 0, q); b.dyn = true; b.range = range; b.thr = thr; b.ratio = ratio; b.attackMs = att; b.releaseMs = rel; b.kneeDb = 6; return b; }
inline BandParams M (BandParams b) { b.place = Placement::Mid; return b; }
inline BandParams S (BandParams b) { b.place = Placement::Side; return b; }
inline Fx Ph (int p) { Fx f; f.phase = p; return f; }
inline Fx Ch (int mode, float drive, float mix) { Fx f; f.ch = mode; f.drive = drive; f.mix = mix; return f; }
inline Fx Dh (float amt) { Fx f; f.dh = true; f.dhAmt = amt; return f; }

inline const std::vector<Def>& defs()
{
    static const std::vector<Def> v {
        // ------------------------------------------------------------------ Vocals
        { "Vocals/Lead - Presen\xC3\xA7""a Natural", { HP (80), Bl (280, -2.0, 1.2), Bl (3200, 2.0, 0.9), HS (11000, 1.5) }, {} },
        { "Vocals/Lead - De-Mud + Ar", { HP (90), Bl (250, -3.0, 1.1), Bl (500, -1.0, 1.5), HS (12000, 2.5) }, {} },
        { "Vocals/Lead - R\xC3\xA1""dio Brilhante", { HP (110), Bl (350, -2.0, 1.0), Bl (4000, 3.0, 0.9), HS (10000, 3.0), LP (18000, 1) }, {} },
        { "Vocals/Lead - Calor \xC3\x8Dntimo", { HP (70), LS (180, 1.5), Bl (2800, -1.5, 1.2), HS (9000, -1.0) }, {} },
        { "Vocals/Lead - Pop Polido", { HP (95), Bl (260, -2.5, 1.2), Bl (1800, 1.0, 1.0), Bl (5000, 1.5, 1.1), HS (13000, 2.0), Dn (7500, -4.0, 3.0, -34, 4, 1, 60) }, {} },
        { "Vocals/Rap - Clareza e Peso", { HP (75), LS (140, 1.5), Bl (300, -2.5, 1.3), Bl (3500, 2.5, 1.0), HS (10000, 1.5) }, {} },
        { "Vocals/Rap - Corte Agressivo", { HP (100), Bl (400, -3.0, 1.0), Bl (2500, 2.0, 1.2), Bl (5500, 2.0, 1.5), Dn (8000, -5.0, 3.0, -32, 4, 1, 60) }, {} },
        { "Vocals/Backing - Sentar na Mistura", { HP (140), Bl (300, -2.0, 1.0), Bl (2500, -2.0, 0.8), LP (14000, 1) }, {} },
        { "Vocals/Telefone (Lo-Fi)", { HP (400, 4), LP (3400, 4), Bl (1200, 3.0, 1.0) }, {} },
        { "Vocals/De-Ess Din\xC3\xA2mico 6-8 kHz", { HP (80), Dn (6500, -6.0, 2.5, -34, 4, 1, 60), Dn (8500, -4.0, 3.0, -36, 4, 1, 60) }, {} },
        { "Vocals/Domar Aspereza (De-harsh)", { HP (85), Bl (300, -1.5, 1.2) }, Dh (55) },
        { "Vocals/Voz Larga e Segura em Mono", { HP (90), S (HP (200, 2)), Bl (300, -2.0, 1.2), S (Bl (4000, 1.5, 0.8)) }, {} },
        // ------------------------------------------------------------------ Drums
        { "Drums/Kick - Punch e Click", { HP (30, 4), Bl (60, 2.5, 1.2), Bl (350, -3.5, 1.5), Bl (4500, 2.0, 1.0) }, {} },
        { "Drums/Kick - Sub Profundo", { HP (25, 4), Bl (50, 3.0, 1.0), Bl (120, -1.5, 1.2), Bl (400, -3.0, 1.2), LP (6000, 2) }, {} },
        { "Drums/Kick - Seco e Curto", { HP (35, 4), Bl (80, 1.5, 1.5), Bl (250, -4.0, 1.6), Bl (3500, 2.5, 1.0) }, {} },
        { "Drums/Snare - Estalo", { HP (90), Bl (200, 1.5, 1.2), Bl (500, -2.0, 1.5), Bl (5000, 3.0, 1.0), HS (10000, 1.5) }, {} },
        { "Drums/Snare - Corpo 200", { HP (80), Bl (200, 3.0, 1.3), Bl (800, -2.0, 1.6), Bl (4000, 1.5, 1.0) }, {} },
        { "Drums/Snare Top - Limpeza", { HP (150, 4), Bl (400, -3.0, 1.5), Bl (900, -1.5, 2.0), Bl (6000, 2.0, 1.0) }, {} },
        { "Drums/Tom - Floor", { HP (50), Bl (100, 3.0, 1.2), Bl (350, -3.0, 1.5), Bl (5000, 2.0, 1.0) }, {} },
        { "Drums/Tom - Rack", { HP (70), Bl (180, 2.5, 1.2), Bl (500, -3.0, 1.5), Bl (4500, 2.0, 1.0) }, {} },
        { "Drums/Hi-Hat - Suave", { HP (300, 4), Bl (3500, -1.5, 1.5), Dn (8000, -3.0, 2.0, -34, 3, 1, 50), LP (16000, 2) }, {} },
        { "Drums/Overheads - Ar e Brilho", { HP (180, 3), Bl (500, -2.0, 1.0), HS (10000, 3.0), Bl (3500, -1.0, 1.5) }, {} },
        { "Drums/Room - Controlar Caixa", { HP (100), Bl (300, -3.0, 1.0), Bl (2000, -1.5, 1.0), LP (12000, 2) }, {} },
        // ------------------------------------------------------------------ Bass
        { "Bass/Tight e Claro", { HP (30, 4), Bl (250, -2.0, 1.4), Bl (900, 1.5, 1.0) }, {} },
        { "Bass/Sub Foundation", { HP (28, 4), LS (60, 2.0), Bl (200, -2.5, 1.2), LP (4000, 2) }, {} },
        { "Bass/Growl Mid", { HP (35, 3), Bl (120, 1.0, 1.0), Bl (700, 2.5, 1.2), Bl (1800, 2.0, 1.0) }, {} },
        { "Bass/808 - Limpar Lama", { HP (25, 4), Bl (100, 1.5, 1.2), Bl (300, -4.0, 1.4), Bl (1500, 1.0, 0.9) }, {} },
        { "Bass/808 - Controlar Sub", { HP (22, 4), Dn (55, -5.0, 2.0, -26, 3, 10, 150), Bl (250, -2.0, 1.2) }, {} },
        { "Bass/El\xC3\xA9trico - Slap", { HP (40, 3), Bl (90, 1.5, 1.0), Bl (400, -2.5, 1.2), Bl (2500, 3.0, 1.0) }, {} },
        { "Bass/Anti-Boom Din\xC3\xA2mico", { HP (30, 4), Dn (100, -4.0, 2.0, -24, 3, 15, 150), Bl (300, -1.5, 1.2) }, {} },
        { "Bass/Sub Mono (Side cortado)", { HP (28, 4), S (HP (120, 4)), Bl (250, -2.0, 1.3) }, {} },
        // ------------------------------------------------------------------ Guitar
        { "Guitar/Ac\xC3\xBAstica - Brilho", { HP (90), Bl (250, -2.0, 1.3), Bl (3000, 1.5, 1.0), HS (10000, 2.5) }, {} },
        { "Guitar/Ac\xC3\xBAstica - Corpo", { HP (70), LS (160, 1.5), Bl (450, -1.5, 1.3), Bl (2500, 1.0, 1.0) }, {} },
        { "Guitar/El\xC3\xA9trica - Crunch Presen\xC3\xA7""a", { HP (85), Bl (300, -2.0, 1.2), Bl (2200, 2.0, 1.0), LP (11000, 3) }, {} },
        { "Guitar/El\xC3\xA9trica - Solo Corte", { HP (100), Bl (400, -2.0, 1.2), Bl (1500, 2.5, 1.0), Bl (3500, 1.5, 1.0), LP (12000, 3) }, {} },
        { "Guitar/Clean - Chime", { HP (110), Bl (350, -2.0, 1.2), Bl (4500, 2.0, 1.0), HS (11000, 2.0) }, {} },
        { "Guitar/Anti-Lama", { HP (90), Bl (220, -2.5, 1.2), Bl (450, -1.5, 1.5), Bl (2800, 1.0, 1.0) }, {} },
        { "Guitar/Nylon - Calor", { HP (80), LS (200, 1.0), Bl (1000, -1.0, 1.0), Bl (4000, 1.5, 1.0), LP (13000, 2) }, {} },
        // ------------------------------------------------------------------ Keys
        { "Keys/Piano - Natural", { HP (40, 2), Bl (300, -1.5, 1.0), Bl (3500, 1.5, 0.9), HS (11000, 1.5) }, {} },
        { "Keys/Piano - Brilhante", { HP (50, 2), Bl (250, -2.0, 1.0), Bl (2500, 2.0, 1.0), HS (9000, 3.0) }, {} },
        { "Keys/Rhodes - Quente", { HP (50, 2), LS (200, 1.5), Bl (700, -1.5, 1.2), Bl (3000, 1.5, 1.0) }, {} },
        { "Keys/\xC3\x93rg\xC3\xA3o - Limpeza", { HP (60, 3), Bl (250, -2.0, 1.2), Bl (1500, -1.0, 1.2), LP (10000, 3) }, {} },
        { "Keys/Pad - Abrir Espa\xC3\xA7o", { HP (120, 3), Bl (300, -3.0, 0.9), Bl (2000, -2.0, 0.8), HS (9000, 1.5) }, {} },
        { "Keys/Pad - Largura (Side)", { HP (100, 3), S (Bl (3000, 2.0, 0.8)), S (HS (9000, 2.0)), M (Bl (300, -2.0, 1.0)) }, {} },
        // ------------------------------------------------------------------ Synth
        { "Synth/Lead - Presen\xC3\xA7""a", { HP (150, 3), Bl (500, -2.0, 1.2), Bl (2500, 2.0, 1.0), HS (10000, 1.5) }, {} },
        { "Synth/Pluck - Click", { HP (120, 3), Bl (350, -2.0, 1.3), Bl (5000, 3.0, 1.0) }, {} },
        { "Synth/Arp - Limpar", { HP (200, 4), Bl (500, -3.0, 1.3), Bl (3000, 1.5, 1.0), LP (14000, 2) }, {} },
        { "Synth/Baixo Sint\xC3\xA9tico - Foco", { HP (32, 4), Bl (180, -2.5, 1.2), Bl (1200, 2.0, 1.0), LP (9000, 3) }, {} },
        { "Synth/FX - Passa-alto 150", { HP (150, 4), Bl (400, -1.5, 1.0) }, {} },
        // ------------------------------------------------------------------ Strings & Brass
        { "Orquestra/Cordas - Seda", { HP (80, 2), Bl (350, -2.0, 1.0), Bl (3000, 1.0, 0.8), HS (10000, 2.0) }, {} },
        { "Orquestra/Cordas - Anti-Aspereza", { HP (70, 2), Dn (3200, -4.0, 1.5, -32, 3, 5, 100), Bl (400, -1.5, 1.0) }, {} },
        { "Orquestra/Violoncelo - Corpo", { HP (50, 3), Bl (180, 2.0, 1.0), Bl (600, -1.5, 1.2) }, {} },
        { "Orquestra/Metais - Calor", { HP (80, 3), Bl (400, 1.0, 1.0), Dn (1800, -3.0, 1.5, -30, 3, 5, 100), HS (9000, -1.0) }, {} },
        { "Orquestra/Metais - Anti-Honk", { HP (90, 3), Dn (1000, -4.0, 2.0, -28, 3, 5, 100), Bl (2500, 1.0, 1.0) }, {} },
        { "Orquestra/Sax - Doce", { HP (100, 3), Bl (500, -1.5, 1.0), Dn (3000, -3.0, 1.5, -30, 3, 5, 100), HS (10000, 1.0) }, {} },
        // ------------------------------------------------------------------ Mixbus
        { "Mixbus/Cola Suave", { HP (25, 2), Bl (300, -1.0, 0.8), HS (12000, 1.0) }, Ch (2, 25, 50) },
        { "Mixbus/Limpar Low-Mid", { HP (28, 2), Bl (250, -1.5, 0.9), Bl (450, -1.0, 1.0) }, {} },
        { "Mixbus/Abrir Topo", { Bl (3500, 0.8, 0.8), HS (12000, 2.0) }, {} },
        { "Mixbus/Graves em Mono", { HP (25, 2), S (HP (120, 4)) }, {} },
        { "Mixbus/Inclina\xC3\xA7\xC3\xA3o Brilhante (Tilt)", { LS (200, -1.0, 0.5), HS (4000, 1.0, 0.5) }, {} },
        { "Mixbus/Bus Bateria - Punch", { HP (35, 3), Bl (70, 1.5, 1.0), Bl (350, -2.0, 1.2), Bl (4500, 1.5, 1.0) }, {} },
        { "Mixbus/Bus M\xC3\xBAsica - Espa\xC3\xA7o p/ Voz", { Dn (2500, -2.5, 1.0, -30, 2, 10, 150), Bl (300, -1.0, 0.9) }, {} },
        { "Mixbus/Bus Vocal - Brilho", { HP (80), Bl (300, -1.5, 1.2), HS (11000, 2.0) }, {} },
        // ------------------------------------------------------------------ Master
        { "Master/Sorriso Suave", { LS (60, 1.2), Bl (350, -1.0, 0.8), HS (12000, 1.5) }, {} },
        { "Master/Limpo e Aberto", { HP (22, 2), Bl (300, -1.0, 0.8), Bl (3500, 0.7, 0.8), HS (13000, 1.2) }, Ph (1) },
        { "Master/Prepara\xC3\xA7\xC3\xA3o de Loudness", { HP (28, 3), Bl (250, -0.8, 0.8), Dn (3500, -2.0, 1.5, -26, 2.5, 10, 120), S (HP (100, 3)) }, {} },
        { "Master/Escuro para Brilhante", { Bl (200, -0.8, 0.7), Bl (4000, 1.0, 0.7), HS (11000, 2.0) }, {} },
        { "Master/Brilhante para Quente", { LS (100, 1.0), HS (8000, -1.5), Bl (3500, -0.8, 0.8) }, {} },
        { "Master/Mono Low (Side HP 120)", { S (HP (120, 4)) }, {} },
        { "Master/Tilt Mais Brilho", { LS (250, -0.8, 0.5), HS (3000, 1.2, 0.5) }, {} },
        { "Master/Tilt Mais Quente", { LS (250, 1.0, 0.5), HS (3000, -1.0, 0.5) }, {} },
        { "Master/Polimento Fase Linear", { HP (20, 2), Bl (120, 0.6, 0.8), Bl (320, -0.7, 0.9), HS (14000, 1.0) }, Ph (2) },
        { "Master/Cola Quente (Character)", { LS (70, 1.0), HS (12000, 1.2) }, Ch (2, 35, 60) },
        // ------------------------------------------------------------------ Corretivo
        { "Corretivo/Notch - Resson\xC3\xA2ncia (3 kHz)", { Nt (3000, 10.0) }, {} },
        { "Corretivo/Infrassom 20 Hz", { HP (20, 5) }, {} },
        { "Corretivo/Suavizar Digital (LP 18 kHz)", { LP (18000, 1) }, {} },
        { "Corretivo/De-Box 400 Hz", { Dn (400, -4.0, 1.6, -28, 2.5, 10, 120) }, {} },
        { "Corretivo/De-Boom 120 Hz", { Dn (120, -4.0, 1.5, -26, 2.5, 15, 150) }, {} },
        { "Corretivo/De-Nasal 1 kHz", { Dn (1000, -3.5, 2.0, -30, 2.5, 8, 100) }, {} },
        { "Corretivo/Zumbido 50 Hz + Harm\xC3\xB3nicos", { Nt (50, 25), Nt (100, 25), Nt (150, 25), Nt (200, 25) }, {} },
        { "Corretivo/Zumbido 60 Hz + Harm\xC3\xB3nicos", { Nt (60, 25), Nt (120, 25), Nt (180, 25), Nt (240, 25) }, {} },
        // ------------------------------------------------------------------ G\xC3\xA9neros locais
        { "G\xC3\xA9neros/Kizomba - Voz Suave", { HP (90), Bl (280, -2.0, 1.2), Bl (3000, 1.5, 0.9), HS (10000, 1.0), Dn (7000, -3.0, 3.0, -34, 4, 1, 60) }, {} },
        { "G\xC3\xA9neros/Kizomba - Baixo Redondo", { HP (32, 4), LS (80, 1.5), Bl (250, -2.5, 1.2), Bl (800, 1.0, 1.0), LP (5000, 3) }, {} },
        { "G\xC3\xA9neros/Kizomba - Bus Quente", { HP (25, 2), LS (90, 1.0), Bl (350, -1.0, 0.8), HS (11000, 1.0) }, Ch (2, 30, 55) },
        { "G\xC3\xA9neros/Semba - Guitarra Brilho", { HP (100), Bl (300, -2.0, 1.2), Bl (3500, 2.0, 1.0), HS (10000, 2.0) }, {} },
        { "G\xC3\xA9neros/Kuduro - Kick Agressivo", { HP (32, 4), Bl (55, 3.0, 1.1), Bl (300, -3.5, 1.5), Bl (3500, 3.0, 1.1) }, {} },
        { "G\xC3\xA9neros/Kuduro - Synth Corte", { HP (180, 4), Bl (500, -2.5, 1.3), Bl (2500, 2.5, 1.0), HS (9000, 1.5) }, {} },
        { "G\xC3\xA9neros/Amapiano - Log Drum", { HP (35, 4), Bl (70, 2.0, 1.3), Bl (250, -2.5, 1.3), Bl (1200, 2.0, 1.2), LP (6000, 2) }, {} },
        { "G\xC3\xA9neros/Amapiano - Baixo Sub", { HP (28, 4), LS (55, 2.0), Bl (180, -2.0, 1.2), S (HP (110, 4)) }, {} },
        { "G\xC3\xA9neros/Afrobeat - Percuss\xC3\xA3o Brilho", { HP (200, 3), Bl (500, -2.0, 1.2), Bl (4500, 2.5, 1.0), HS (11000, 2.0) }, {} },
        { "G\xC3\xA9neros/Afrobeat - Voz Aberta", { HP (85), Bl (260, -2.0, 1.2), Bl (2800, 1.5, 0.9), HS (12000, 2.0) }, {} },
        { "G\xC3\xA9neros/Hip-Hop - Master Punch", { HP (25, 3), LS (60, 1.5), Bl (320, -1.2, 0.9), Bl (3500, 1.0, 0.9) }, Ch (3, 25, 40) },
        { "G\xC3\xA9neros/Trap - Controlar 808", { HP (24, 4), Dn (52, -5.0, 2.0, -24, 3, 10, 160), Bl (280, -2.5, 1.3), Bl (1800, 1.0, 1.0) }, {} },
        // ------------------------------------------------------------------ Sends
        { "Sends/Reverb - Limpar", { HP (200, 3), LP (8000, 3), Bl (400, -2.0, 1.0) }, {} },
        { "Sends/Delay - Escuro", { HP (300, 3), LP (5000, 3) }, {} },
        { "Sends/Paralelo - Brilho", { HP (150, 3), HS (6000, 3.0), Bl (3000, 1.5, 0.8) }, {} },
        { "Sends/R\xC3\xA1""dio Lo-Fi", { HP (350, 4), LP (6500, 4), Bl (1800, 2.0, 1.0) }, {} },
        // ------------------------------------------------------------------ Utility
        { "Utility/Mid - Passa-alto Solo", { M (HP (30, 3)) }, {} },
        { "Utility/Side - Passa-alto 150", { S (HP (150, 4)) }, {} },
        { "Utility/Verifica\xC3\xA7\xC3\xA3o Mono (Side cortado)", { S (Bl (1000, -12.0, 0.4)) }, {} },
        { "Utility/Tilt Escuro", { LS (300, 2.0, 0.5), HS (3000, -2.0, 0.5) }, {} },
        { "Utility/Tilt Brilhante", { LS (300, -2.0, 0.5), HS (3000, 2.0, 0.5) }, {} },
        { "Utility/Passa-baixo 12 kHz (Referencia)", { LP (12000, 3) }, {} },
    };
    return v;
}
} // namespace pq::fp
