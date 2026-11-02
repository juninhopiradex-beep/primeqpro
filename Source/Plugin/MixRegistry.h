// =============================================================================
//  PRIMEQ pro by Piradex — registo de pistas partilhado entre instancias NO MESMO PROCESSO do anfitriao
//
//  Cada instancia regista-se com um nome editavel (Kick, Bass, Vocals…), uma prioridade (quem e protegido) e publica,
//  a cada 25 ms, os niveis das 28 bandas (so se "Analisar esta pista" estiver ligado). A thread de audio so escreve
//  em std::atomic (relaxed) — sem locks. Nomes/prioridades/correio usam um mutex, apenas em threads de mensagens/trabalho.
//  LIMITES: so ve instancias carregadas no mesmo processo do mesmo plugin (DAWs com plugins em processos separados,
//  p.ex. alguns modos sandbox, isolam as instancias). Nao troca audio entre instancias.
// =============================================================================
#pragma once
#include "../DSP/MixMeter.h"
#include "../SmartOps/Deresonate.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace pq
{
constexpr int kMixHist = 400;       // 400 x 25 ms = 10 s
constexpr int kMixSlots = 32;

struct MixMail { std::vector<DeresProposal> props; std::string undoName; };
struct MixTrackInfo { int group = 1; int slot = -1; std::string name; int priority = 2; bool analysing = false, isSelf = false; double sampleRate = 48000; uint32_t head = 0; float lastDb = -120.f; };

class MixRegistry
{
public:
    static MixRegistry& get() { static MixRegistry r; return r; }

    int add (const void* owner)
    {
        std::lock_guard<std::mutex> g (m);
        for (int i = 0; i < kMixSlots; ++i)
            if (! slots[(size_t) i].used.load())
            {
                Slot& s = slots[(size_t) i]; s.owner = owner; s.name = "Pista " + std::to_string(++counter); s.priority = 2; s.group.store (1); s.mail.clear(); s.head.store (0); s.analysing.store (false); s.hasMail.store (false); s.used.store (true);
                for (auto& f : s.lv) f.store (-120.f, std::memory_order_relaxed);
                return i;
            }
        return -1;
    }
    void remove (int slot) { if (slot < 0) return; std::lock_guard<std::mutex> g (m); slots[(size_t) slot].used.store (false); slots[(size_t) slot].analysing.store (false); slots[(size_t) slot].mail.clear(); slots[(size_t) slot].hasMail.store (false); }

    void setName (int slot, const std::string& n) { if (slot < 0) return; std::lock_guard<std::mutex> g (m); slots[(size_t) slot].name = n.empty() ? std::string ("Pista") : n.substr (0, 32); }
    std::string name (int slot) const { if (slot < 0) return {}; std::lock_guard<std::mutex> g (m); return slots[(size_t) slot].name; }
    void setPriority (int slot, int p) { if (slot < 0) return; std::lock_guard<std::mutex> g (m); slots[(size_t) slot].priority = std::min (5, std::max (1, p)); }
    int  priority (int slot) const { if (slot < 0) return 2; std::lock_guard<std::mutex> g (m); return slots[(size_t) slot].priority; }
    bool analysing (int slot) const noexcept { return slot >= 0 && slots[(size_t) slot].analysing.load (std::memory_order_relaxed); }
    void setAnalysing (int slot, bool on, double fs) { if (slot < 0) return; slots[(size_t) slot].analysing.store (on); slots[(size_t) slot].sampleRate.store (fs); }

    // ---- thread de audio: sem locks
    void pushFrame (int slot, const float* db28, float overallDb) noexcept
    {
        if (slot < 0) return; Slot& s = slots[(size_t) slot]; const uint32_t h = s.head.load (std::memory_order_relaxed); const size_t base = (size_t) (h % kMixHist) * kMixBands;
        for (int b = 0; b < kMixBands; ++b) s.lv[base + (size_t) b].store (db28[b], std::memory_order_relaxed);
        s.overall.store (overallDb, std::memory_order_relaxed); s.head.store (h + 1, std::memory_order_release);
    }

    std::vector<MixTrackInfo> list (const void* self) const
    {
        std::vector<MixTrackInfo> out; std::lock_guard<std::mutex> g (m);
        for (int i = 0; i < kMixSlots; ++i)
        {
            const Slot& s = slots[(size_t) i]; if (! s.used.load()) continue;
            MixTrackInfo t; t.slot = i; t.name = s.name; t.priority = s.priority; t.analysing = s.analysing.load(); t.group = s.group.load(); t.isSelf = (s.owner == self); t.sampleRate = s.sampleRate.load(); t.head = s.head.load (std::memory_order_acquire); t.lastDb = s.overall.load (std::memory_order_relaxed);
            out.push_back (t);
        }
        return out;
    }
    // copia ate 'maxFrames' quadros mais recentes (o mais recente no fim); devolve quantos
    int copyFrames (int slot, int maxFrames, std::vector<std::array<float, kMixBands>>& out) const
    {
        out.clear(); if (slot < 0) return 0; const Slot& s = slots[(size_t) slot];
        const uint32_t h = s.head.load (std::memory_order_acquire); const int n = (int) std::min<uint32_t> ((uint32_t) std::min (maxFrames, kMixHist - 2), h);
        for (int k = n; k >= 1; --k)
        {
            const size_t base = (size_t) ((h - (uint32_t) k) % kMixHist) * kMixBands; std::array<float, kMixBands> f;
            for (int b = 0; b < kMixBands; ++b) f[(size_t) b] = s.lv[base + (size_t) b].load (std::memory_order_relaxed);
            out.push_back (f);
        }
        return n;
    }
    void pushStats (int slot, const SpatialStats& st) noexcept
    {
        if (slot < 0) return; Slot& s = slots[(size_t) slot];
        for (int b = 0; b < kMixBands; ++b) { s.sp[(size_t) b].store (st.pan[b], std::memory_order_relaxed); s.sp[(size_t) (kMixBands + b)].store (st.corr[b], std::memory_order_relaxed); s.sp[(size_t) (2 * kMixBands + b)].store (st.monoLoss[b], std::memory_order_relaxed); }
        s.sg[0].store (st.corrAll, std::memory_order_relaxed); s.sg[1].store (st.monoLossAll, std::memory_order_relaxed); s.sg[2].store (st.rmsDb, std::memory_order_relaxed); s.sg[3].store (st.crestDb, std::memory_order_relaxed);
    }
    SpatialStats stats (int slot) const
    {
        SpatialStats st; if (slot < 0) return st; const Slot& s = slots[(size_t) slot];
        for (int b = 0; b < kMixBands; ++b) { st.pan[b] = s.sp[(size_t) b].load (std::memory_order_relaxed); st.corr[b] = s.sp[(size_t) (kMixBands + b)].load (std::memory_order_relaxed); st.monoLoss[b] = s.sp[(size_t) (2 * kMixBands + b)].load (std::memory_order_relaxed); }
        st.corrAll = s.sg[0].load(); st.monoLossAll = s.sg[1].load(); st.rmsDb = s.sg[2].load(); st.crestDb = s.sg[3].load(); return st;
    }
    void setGroup (int slot, int g) { if (slot >= 0) slots[(size_t) slot].group.store (std::min (4, std::max (1, g))); }
    int  group (int slot) const { return slot >= 0 ? slots[(size_t) slot].group.load() : 1; }
    void clearFrames (int slot) { if (slot >= 0) slots[(size_t) slot].head.store (0); }

    // ---- correio: uma instancia pede a outra para aplicar propostas (executado na thread de mensagens do alvo)
    void post (int target, MixMail mail) { if (target < 0) return; std::lock_guard<std::mutex> g (m); Slot& s = slots[(size_t) target]; if (! s.used.load()) return; s.mail.push_back (std::move (mail)); s.hasMail.store (true); }
    bool hasMail (int slot) const { return slot >= 0 && slots[(size_t) slot].hasMail.load(); }
    bool takeMail (int slot, MixMail& out)
    {
        if (slot < 0) return false; std::lock_guard<std::mutex> g (m); Slot& s = slots[(size_t) slot];
        if (s.mail.empty()) { s.hasMail.store (false); return false; }
        out = std::move (s.mail.front()); s.mail.erase (s.mail.begin()); s.hasMail.store (! s.mail.empty()); return true;
    }
private:
    struct Slot
    {
        std::atomic<int> group { 1 }; std::array<std::atomic<float>, (size_t) kMixBands * 3> sp {}; std::array<std::atomic<float>, 4> sg {}; std::atomic<bool> used { false }, analysing { false }, hasMail { false }; std::atomic<uint32_t> head { 0 }; std::atomic<float> overall { -120.f }, sampleRate { 48000.f };
        std::array<std::atomic<float>, (size_t) kMixBands * kMixHist> lv {}; const void* owner = nullptr; std::string name; int priority = 2; std::vector<MixMail> mail;
    };
    mutable std::mutex m; std::array<Slot, kMixSlots> slots; int counter = 0;
};
} // namespace pq
