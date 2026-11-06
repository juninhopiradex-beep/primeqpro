// Teste de integracao do PRIMEQ PRO (JUCE, sem anfitriao): processa audio real pelo AudioProcessor.
// Verifica: ganho medido, identidade/bypass, latencia/PDC dos 3 modos de fase, FIR vs analitico, dinamica e sidechain,
// automacao rapida, estado/presets, mudancas de sample rate, ausencia de alocacoes na thread de audio, CPU e editor (render PNG).
#include "Plugin/PluginProcessor.h"
#include "GUI/PluginEditor.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <random>
#include <set>

// contador de alocacoes SO da thread de teste (thread_local) durante a medicao
static thread_local bool g_track = false; static thread_local long g_allocs = 0;
void* operator new (std::size_t n) { if (g_track) ++g_allocs; if (void* p = std::malloc (n ? n : 1)) return p; throw std::bad_alloc(); }
void* operator new[] (std::size_t n) { if (g_track) ++g_allocs; if (void* p = std::malloc (n ? n : 1)) return p; throw std::bad_alloc(); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

static int fails = 0, passes = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; std::printf ("FALHOU  "); std::printf (__VA_ARGS__); std::printf ("\n"); } else { ++passes; std::printf ("ok      "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)
#define INFO(...) do { std::printf ("INFO    "); std::printf (__VA_ARGS__); std::printf ("\n"); } while (0)

static void setP (PrimeqProcessor& p, const juce::String& id, float v) { auto* q = p.apvts.getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); }
static void setBandP (PrimeqProcessor& p, int b, const char* k, float v) { setP (p, juce::String (pq::bid (b, k)), v); }

static std::unique_ptr<PrimeqProcessor> make (double fs, int bs, bool sidechain = false)
{
    auto p = std::make_unique<PrimeqProcessor>();
    if (sidechain) { juce::AudioProcessor::BusesLayout l; l.inputBuses.add (juce::AudioChannelSet::stereo()); l.inputBuses.add (juce::AudioChannelSet::stereo()); l.outputBuses.add (juce::AudioChannelSet::stereo()); p->setBusesLayout (l); }
    p->setRateAndBufferSizeDetails (fs, bs); p->prepareToPlay (fs, bs); return p;
}

// processa 'x' (canal L = R = x) em blocos de 'bs'; devolve saida L
static std::vector<float> run (PrimeqProcessor& p, const std::vector<float>& x, int bs, int sleepMs = 0)
{
    std::vector<float> out (x.size()); juce::MidiBuffer midi; const int nCh = std::max (2, p.getTotalNumInputChannels());
    for (size_t off = 0; off < x.size(); off += (size_t) bs)
    {
        const int n = (int) std::min<size_t> ((size_t) bs, x.size() - off); juce::AudioBuffer<float> b (nCh, n); b.clear();
        for (int c = 0; c < 2; ++c) std::copy (x.begin() + (long) off, x.begin() + (long) off + n, b.getWritePointer (c));
        p.processBlock (b, midi); std::copy (b.getReadPointer (0), b.getReadPointer (0) + n, out.begin() + (long) off);
        if (sleepMs) juce::Thread::sleep (sleepMs);
    }
    return out;
}
static double rmsDb (const std::vector<float>& v, size_t from, size_t to) { double s = 0; for (size_t i = from; i < to; ++i) s += (double) v[i] * v[i]; return 10 * std::log10 (std::max (1e-30, s / (double) (to - from))); }
static std::vector<float> sine (double f, double fs, size_t n, float a = 0.25f) { std::vector<float> v (n); for (size_t i = 0; i < n; ++i) v[i] = a * (float) std::sin (2 * M_PI * f * (double) i / fs); return v; }
static bool finite (const std::vector<float>& v) { for (float x : v) if (! std::isfinite (x)) return false; return true; }

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    AccessManager::get().setOnboardingSeen();   // o guia de 1.o arranque nao deve tapar as capturas dos testes
    const double fs = 48000; const int bs = 512;

    // 1) identidade e bypass
    {
        auto p = make (fs, bs); std::mt19937 rng (1); std::uniform_real_distribution<float> d (-0.5f, 0.5f);
        std::vector<float> x (24000); for (auto& v : x) v = d (rng);
        auto y = run (*p, x, bs); double e = 0; for (size_t i = 0; i < x.size(); ++i) e = std::max (e, (double) std::fabs (y[i] - x[i]));
        CHECK (e < 1e-6, "sem bandas: saida == entrada (erro max %.2e), latencia ZL = %d", e, p->currentLatency());
        setBandP (*p, 0, "on", 1); setBandP (*p, 0, "gain", 12); setBandP (*p, 0, "freq", 500); setP (*p, "bypass", 1);
        run (*p, x, bs); auto y2 = run (*p, x, bs); e = 0; for (size_t i = 0; i < x.size(); ++i) e = std::max (e, (double) std::fabs (y2[i] - x[i]));
        CHECK (e < 1e-6, "bypass: null test, erro max %.2e", e);
    }
    // 2) Bell +6 dB a 1 kHz, Q=1 (ZL) — ganho real medido pelo processador
    {
        auto p = make (fs, bs); setBandP (*p, 0, "on", 1); setBandP (*p, 0, "freq", 1000); setBandP (*p, 0, "gain", 6); setBandP (*p, 0, "q", 1);
        auto x = sine (1000, fs, 48000); auto y = run (*p, x, bs);
        const double g = rmsDb (y, 24000, 48000) - rmsDb (x, 24000, 48000);
        CHECK (std::fabs (g - 6.0) < 0.05, "ZL: Bell +6 dB @1 kHz Q=1 -> ganho medido %.3f dB", g);
        setBandP (*p, 0, "on", 0); setBandP (*p, 1, "on", 1); setBandP (*p, 1, "type", 3); setBandP (*p, 1, "freq", 80); setBandP (*p, 1, "slope", 3);
        std::vector<double> gs;
        for (double f : { 40.0, 80.0, 160.0 }) { auto xs = sine (f, fs, 48000); run (*p, xs, bs); auto ys = run (*p, xs, bs); gs.push_back (rmsDb (ys, 24000, 48000) - rmsDb (xs, 24000, 48000)); }
        CHECK (std::fabs (gs[0] + 24.1) < 0.6 && std::fabs (gs[1] + 3.0) < 0.2 && std::fabs (gs[2]) < 0.2, "ZL: HP 80 Hz 24 dB/oct -> 40/80/160 Hz = %.1f / %.1f / %.2f dB", gs[0], gs[1], gs[2]);
    }
    // 3) latencia e PDC dos modos de fase; FIR vs analitico
    for (int mode = 1; mode <= 2; ++mode)
    {
        auto p = make (fs, bs); setP (*p, "phase", (float) mode);
        const int lat = p->currentLatency(); CHECK (lat == 4096, "modo %s: latencia declarada = %d amostras @48k", mode == 1 ? "Natural" : "Linear", lat);
        // impulso -> pico do kernel identidade tem de sair exatamente 'lat' amostras depois (sem bandas)
        std::vector<float> imp (48000, 0.f); imp[2000] = 1.f;
        auto y = run (*p, imp, bs, 8);   // dá tempo ao trabalhador FIR (IR identidade) e a troca de caminho
        size_t pk = 0; for (size_t i = 0; i < y.size(); ++i) if (std::fabs (y[i]) > std::fabs (y[pk])) pk = i;
        CHECK (std::fabs (y[pk]) > 0.9f && pk == 2000 + (size_t) lat, "modo %s: transicao ZL->FIR sem silencio — impulso em 2000 sai em %zu (amp %.3f, dry alinhado ate o FIR encher)", mode == 1 ? "Natural" : "Linear", pk, y[pk]);
        // com a troca ZL->FIR o primeiro impulso pode sair pelo dry; mede de novo com o caminho ja ativo
        std::vector<float> imp2 (24000, 0.f); imp2[3000] = 1.f; auto y2 = run (*p, imp2, bs, 2);
        pk = 0; for (size_t i = 0; i < y2.size(); ++i) if (std::fabs (y2[i]) > std::fabs (y2[pk])) pk = i;
        CHECK (pk == 3000 + (size_t) lat && std::fabs (y2[pk] - 1.f) < 0.02f, "modo %s: PDC — impulso em 3000 sai em %zu (esperado %zu), amplitude %.3f", mode == 1 ? "Natural" : "Linear", pk, 3000 + (size_t) lat, y2[pk]);
        // ganho medido com Bell +6 dB
        setBandP (*p, 0, "on", 1); setBandP (*p, 0, "freq", 1000); setBandP (*p, 0, "gain", 6); setBandP (*p, 0, "q", 1);
        auto xs = sine (1000, fs, 96000); auto ys = run (*p, xs, bs, 3);
        const double g = rmsDb (ys, 80000, 96000) - rmsDb (xs, 60000, 76000);
        CHECK (std::fabs (g - 6.0) < 0.15, "modo %s: Bell +6 dB @1 kHz medido = %.3f dB", mode == 1 ? "Natural" : "Linear", g);
    }
    // 4) sample rates: latencia por taxa e estabilidade apos prepare
    for (double r : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
    {
        auto p = make (r, 512); setP (*p, "phase", 2); p->prepareToPlay (r, 512);
        const int expect = pq::firLatencyFor (r);
        setBandP (*p, 0, "on", 1); setBandP (*p, 0, "gain", 9);
        auto y = run (*p, sine (1000, r, (size_t) (r * 0.6)), 512, 4);
        CHECK (p->currentLatency() == expect && finite (y), "fs %.0f: latencia %d smp (esperado %d), saida finita", r, p->currentLatency(), expect);
    }
    // 5) dinamica por banda + sidechain externo (bus real)
    {
        auto p = make (fs, bs, true); CHECK (p->getBusCount (true) == 2 && p->getBus (true, 1)->isEnabled(), "layout com sidechain activo (%d canais totais)", p->getTotalNumInputChannels());
        setBandP (*p, 0, "on", 1); setBandP (*p, 0, "freq", 1000); setBandP (*p, 0, "gain", 0); setBandP (*p, 0, "dyn", 1); setBandP (*p, 0, "thr", -40); setBandP (*p, 0, "range", -12); setBandP (*p, 0, "ratio", 20); setBandP (*p, 0, "knee", 0);
        setP (*p, "scext", 1);
        // programa silencioso + sidechain forte: main tem de ficar a zero (sem fuga) e o GR deve actuar
        juce::MidiBuffer midi; double maxMain = 0;
        for (int i = 0; i < 200; ++i)
        {
            juce::AudioBuffer<float> b (4, 512); b.clear(); auto sc = sine (1000, fs, 512, 0.5f);
            for (int c = 2; c < 4; ++c) std::copy (sc.begin(), sc.end(), b.getWritePointer (c));
            p->processBlock (b, midi); for (int c = 0; c < 2; ++c) maxMain = std::max (maxMain, (double) b.getMagnitude (c, 0, 512));
        }
        CHECK (maxMain == 0.0, "sidechain: sem fuga para a saida (max %.2e)", maxMain);
        CHECK (std::fabs (p->dynTel[0].load() + 12.f) < 0.5f, "sidechain: GR telemetria = %.2f dB (esperado -12)", p->dynTel[0].load());
    }
    {   // dinamica interna: abaixo/acima do threshold, ganho de audio acompanha a telemetria
        auto p = make (fs, bs); setBandP (*p, 0, "on", 1); setBandP (*p, 0, "freq", 1000); setBandP (*p, 0, "dyn", 1); setBandP (*p, 0, "thr", -30); setBandP (*p, 0, "range", -9); setBandP (*p, 0, "ratio", 20); setBandP (*p, 0, "knee", 0);
        auto lo = sine (1000, fs, 48000, 0.005f); auto yl = run (*p, lo, bs); const double gLo = rmsDb (yl, 24000, 48000) - rmsDb (lo, 24000, 48000);
        auto hi = sine (1000, fs, 48000, 0.5f); auto yh = run (*p, hi, bs); const double gHi = rmsDb (yh, 36000, 48000) - rmsDb (hi, 36000, 48000);
        CHECK (std::fabs (gLo) < 0.1 && std::fabs (gHi + 9.0) < 0.6 && std::fabs (p->dynTel[0].load() + 9.f) < 0.3f, "dinamica: abaixo do thr %.2f dB, acima %.2f dB (esperado -9), telemetria %.2f", gLo, gHi, p->dynTel[0].load());
    }
    // 6) automacao rapida, buffers variados, mono/estereo, 24 bandas em pior caso, sem NaN
    {
        std::mt19937 rng (11); std::uniform_real_distribution<float> u (0, 1); bool bad = false;
        for (int bsz : { 1, 7, 32, 64, 480, 512, 1024, 4096 })
        {
            auto p = make (fs, bsz);
            for (int b = 0; b < pq::kNumBands; ++b) { setBandP (*p, b, "on", 1); setBandP (*p, b, "type", (float) (b % 8)); setBandP (*p, b, "gain", (u (rng) - 0.5f) * 40); setBandP (*p, b, "q", 0.2f + u (rng) * 10); setBandP (*p, b, "dyn", (float) (b % 2)); setBandP (*p, b, "place", (float) (b % 5)); setBandP (*p, b, "slope", (float) (b % 9)); }
            std::vector<float> x (std::max (bsz * 40, 8192)); for (auto& v : x) v = (u (rng) - 0.5f);
            juce::MidiBuffer midi;
            for (size_t off = 0; off + (size_t) bsz <= x.size(); off += (size_t) bsz)
            {
                if ((off / (size_t) bsz) % 3 == 0) { setBandP (*p, (int) (u (rng) * 23.99f), "freq", 20 + u (rng) * 15000); setBandP (*p, (int) (u (rng) * 23.99f), "gain", (u (rng) - 0.5f) * 60); }
                juce::AudioBuffer<float> b (2, bsz); for (int c = 0; c < 2; ++c) std::copy (x.begin() + (long) off, x.begin() + (long) off + bsz, b.getWritePointer (c));
                p->processBlock (b, midi); for (int c = 0; c < 2; ++c) for (int i = 0; i < bsz; ++i) if (! std::isfinite (b.getSample (c, i))) bad = true;
            }
        }
        CHECK (! bad, "24 bandas (todos tipos/placements/slopes/dinamica) + automacao rapida, buffers 1..4096: sem NaN/Inf");
        auto pm = std::make_unique<PrimeqProcessor>(); juce::AudioProcessor::BusesLayout l; l.inputBuses.add (juce::AudioChannelSet::mono()); l.inputBuses.add (juce::AudioChannelSet::disabled()); l.outputBuses.add (juce::AudioChannelSet::mono());
        CHECK (pm->checkBusesLayoutSupported (l), "layout mono suportado");
        pm->setBusesLayout (l); pm->setRateAndBufferSizeDetails (fs, bs); pm->prepareToPlay (fs, bs); setBandP (*pm, 0, "on", 1); setBandP (*pm, 0, "gain", 6); setBandP (*pm, 0, "freq", 1000);
        juce::AudioBuffer<float> mb (1, 4800); auto s = sine (1000, fs, 4800); std::copy (s.begin(), s.end(), mb.getWritePointer (0)); juce::MidiBuffer mi; pm->processBlock (mb, mi); pm->processBlock (mb, mi);
        CHECK (std::isfinite (mb.getMagnitude (0, 4800)), "mono: processa sem erro");
    }
    // 7) estado e presets
    {
        auto p = make (fs, bs); setBandP (*p, 3, "on", 1); setBandP (*p, 3, "freq", 2345.f); setBandP (*p, 3, "gain", -7.5f); setBandP (*p, 3, "q", 3.3f); setBandP (*p, 3, "type", 5); setBandP (*p, 3, "dyn", 1); setP (*p, "phase", 2); setP (*p, "out", -3.f);
        juce::MemoryBlock mb; p->getStateInformation (mb);
        auto q = make (fs, bs); q->setStateInformation (mb.getData(), (int) mb.getSize());
        auto a = p->getBand (3), b = q->getBand (3);
        CHECK (b.on && std::fabs (b.freq - a.freq) < 0.5 && std::fabs (b.gain - a.gain) < 0.01 && b.type == a.type && b.dyn && std::fabs (q->getParamReal ("out") + 3.f) < 0.01f && (int) q->getParamReal ("phase") == 2,
               "recall de estado: banda 4 = %.1f Hz %.2f dB tipo %d, fase=%d, out=%.1f", b.freq, b.gain, (int) b.type, (int) q->getParamReal ("phase"), q->getParamReal ("out"));
        q->loadFactoryPreset (1); const auto v = q->getBand (0);
        CHECK (v.on && v.type == pq::BandType::HighPass && std::fabs (v.freq - 80) < 0.5, "preset de fabrica 'Vocal Presence' carrega: banda 1 = HP %.0f Hz", v.freq);
        q->undo.undo(); CHECK (true, "undo apos preset executa sem erro");
    }
    // 8) tempo real: nenhuma alocacao na thread de audio (ZL e FIR), CPU
    {
        for (int mode : { 0, 2 })
        {
            auto p = make (fs, bs); for (int b = 0; b < pq::kNumBands; ++b) { setBandP (*p, b, "on", 1); setBandP (*p, b, "freq", 60.f * std::pow (1.28f, (float) b)); setBandP (*p, b, "gain", (b % 2) ? 3.f : -3.f); setBandP (*p, b, "dyn", (float) (b % 3 == 0)); setBandP (*p, b, "place", (float) (b % 5)); }
            setP (*p, "phase", (float) mode); auto x = sine (1000, fs, 48000 * 2); run (*p, x, bs, mode ? 4 : 0);
            juce::MidiBuffer midi; juce::AudioBuffer<float> b (2, bs); for (int c = 0; c < 2; ++c) std::copy (x.begin(), x.begin() + bs, b.getWritePointer (c));
            g_allocs = 0; g_track = true; for (int i = 0; i < 200; ++i) p->processBlock (b, midi); g_track = false;
            CHECK (g_allocs == 0, "tempo real: %s — 200 blocos processBlock sem alocacoes na thread de audio (contagem %ld)", mode ? "Linear" : "Zero Latency", g_allocs);
        }
        auto p = make (fs, bs); for (int b = 0; b < pq::kNumBands; ++b) { setBandP (*p, b, "on", 1); setBandP (*p, b, "freq", 60.f * std::pow (1.28f, (float) b)); setBandP (*p, b, "gain", (b % 2) ? 3.f : -3.f); setBandP (*p, b, "dyn", (float) (b % 2 == 0)); setBandP (*p, b, "place", (float) (b % 5)); }
        std::mt19937 rng (5); std::uniform_real_distribution<float> d (-0.3f, 0.3f); juce::AudioBuffer<float> b (2, bs); juce::MidiBuffer midi;
        const int blocks = (int) (fs * 10 / bs); const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < blocks; ++i) { for (int c = 0; c < 2; ++c) for (int k = 0; k < bs; ++k) b.setSample (c, k, d (rng)); p->processBlock (b, midi); }
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        INFO ("CPU ZL: 24 bandas (12 dinamicas, M/S misturado), 10 s estereo @48k/512 => %.2f s = %.1f %% de 1 nucleo (cpuLoad interno %.1f %%)", secs, secs * 10, p->cpuLoad.load() * 100);
        CHECK (secs < 10.0, "CPU: tempo real folgado");
        setP (*p, "phase", 2); for (int i = 0; i < 200; ++i) { p->processBlock (b, midi); juce::Thread::sleep (3); }
        const auto t1 = std::chrono::steady_clock::now(); for (int i = 0; i < blocks / 2; ++i) { for (int c = 0; c < 2; ++c) for (int k = 0; k < bs; ++k) b.setSample (c, k, d (rng)); p->processBlock (b, midi); }
        const double s2 = std::chrono::duration<double> (std::chrono::steady_clock::now() - t1).count();
        INFO ("CPU Linear Phase: 5 s estereo @48k/512 => %.2f s = %.1f %% de 1 nucleo", s2, s2 / 5.0 * 100.0);
    }
    // 9) licenciamento: senha errada e entradas
    {
        const auto m = AccessManager::matchPassword ("senha-errada");
        CHECK (m.index < 0 && ! m.expired, "login: senha errada rejeitada (PBKDF2)");
        CHECK (pq::lic::daysSince2026 (2026, 12, 31) == 364, "licenca: periodo aberto ate 31/12/2026 (dia %d)", pq::lic::daysSince2026 (2026, 12, 31));
    }
    // 11) lookahead: latencia declarada e real; os detetores reagem ANTES do transiente
    {
        auto p = make (fs, bs); setP (*p, "look", 4);   // 5 ms
        const int expect = (int) std::lround (0.005 * fs); setBandP (*p, 0, "on", 1);
        std::vector<float> imp (24000, 0.f); imp[3000] = 1.f; auto y = run (*p, imp, bs, 0); y = run (*p, imp, bs, 0);
        size_t pk = 0; for (size_t i = 0; i < y.size(); ++i) if (std::fabs (y[i]) > std::fabs (y[pk])) pk = i;
        CHECK (p->currentLatency() == expect && pk == 3000 + (size_t) expect && std::fabs (y[pk] - 1.f) < 0.02f, "lookahead 5 ms: latencia %d (esperado %d), impulso em 3000 sai em %zu", p->currentLatency(), expect, pk);
        // rajada de 1 kHz (0,5) com banda dinamica de ataque lento: com lookahead o pico no inicio e menor
        auto burstPeakDb = [&] (int lookIdx)
        {
            auto q = make (fs, bs); setP (*q, "look", (float) lookIdx); setBandP (*q, 0, "on", 1); setBandP (*q, 0, "freq", 1000); setBandP (*q, 0, "q", 0.7f); setBandP (*q, 0, "dyn", 1); setBandP (*q, 0, "thr", -30);
            setBandP (*q, 0, "range", -18); setBandP (*q, 0, "ratio", 20); setBandP (*q, 0, "knee", 0); setBandP (*q, 0, "attack", 3); setBandP (*q, 0, "release", 200);
            std::vector<float> x (48000, 0.f); for (size_t i = 24000; i < x.size(); ++i) x[i] = 0.5f * (float) std::sin (2 * M_PI * 1000.0 * (double) i / fs);
            run (*q, std::vector<float> (24000, 0.f), bs); auto z = run (*q, x, bs);
            const size_t on = 24000 + (size_t) q->currentLatency(); double pkv = 0; for (size_t i = on; i < on + 96; ++i) pkv = std::max (pkv, (double) std::fabs (z[i]));
            return 20 * std::log10 (std::max (1e-9, pkv));
        };
        const double noLook = burstPeakDb (0), withLook = burstPeakDb (4);
        INFO ("pico nos primeiros 2 ms da rajada: sem lookahead %.1f dBFS | lookahead 5 ms %.1f dBFS", noLook, withLook);
        CHECK (withLook < noLook - 3.0, "lookahead: reducao ja aplicada no inicio da rajada (%.1f dB menos de pico)", noLook - withLook);
    }
    // 12) De-harsh: latencia 2048 declarada e real; wet/dry; transicao sem silencio
    {
        auto p = make (fs, bs); setP (*p, "dh_on", 1); setP (*p, "dh_amount", 0.f);
        CHECK (p->currentLatency() == 2048, "De-harsh: latencia declarada = %d", p->currentLatency());
        std::mt19937 rng (4); std::normal_distribution<float> nd (0, 0.05f); std::vector<float> n1 (48000); for (auto& v : n1) v = nd (rng);
        run (*p, n1, bs);
        std::vector<float> imp (24000, 0.f); imp[3000] = 1.f; auto y = run (*p, imp, bs);
        size_t pk = 0; for (size_t i = 0; i < y.size(); ++i) if (std::fabs (y[i]) > std::fabs (y[pk])) pk = i;
        CHECK (pk == 3000 + 2048 && std::fabs (y[pk] - 1.f) < 0.02f, "De-harsh (amount 0): impulso em 3000 sai em %zu (esperado 5048) amp %.3f", pk, y[pk]);
        // com amount alto e sinal duro: reduz o pico de 5 kHz
        setP (*p, "dh_amount", 100.f); setP (*p, "dh_sens", 3.f);
        std::vector<float> h (96000); for (size_t i = 0; i < h.size(); ++i) h[i] = nd (rng) + 0.06f * (float) std::sin (2 * M_PI * 5000.0 * (double) i / fs);
        run (*p, h, bs); auto yh = run (*p, h, bs);
        auto tonedb = [&] (const std::vector<float>& v, size_t from, double f) { double re = 0, im = 0; const size_t len = 16384; for (size_t i = 0; i < len; ++i) { const double w = 0.5 - 0.5 * std::cos (2 * M_PI * (double) i / (double) len), ph = 2 * M_PI * f * (double) (from + i) / fs; re += w * v[from + i] * std::cos (ph); im += w * v[from + i] * std::sin (ph); } return 20 * std::log10 (2 * std::sqrt (re * re + im * im) / (len * 0.5) + 1e-12); };
        const double red = tonedb (yh, 60000, 5000) - tonedb (h, 60000 - 2048, 5000);
        INFO ("De-harsh no plugin: tom de 5 kHz %.1f dB; telemetria %.1f dB", red, p->dhRedDb.load());
        CHECK (red < -4.0 && p->dhRedDb.load() < -4.f, "De-harsh: atenua o pico de 5 kHz (%.1f dB) e a telemetria acompanha (%.1f dB)", red, p->dhRedDb.load());
        setP (*p, "dh_on", 0); CHECK (p->currentLatency() == 0, "De-harsh desligado: latencia volta a %d", p->currentLatency());
    }
    // 13) Character: latencia exata (mix 0 = dry atrasado), estabilidade, modos distintos
    {
        auto p = make (fs, bs); setP (*p, "ch_mode", 3); setP (*p, "ch_os", 2); setP (*p, "ch_mix", 0.f);
        CHECK (p->currentLatency() == 36, "Character 4x: latencia declarada = %d (esperado 36)", p->currentLatency());
        std::vector<float> imp (24000, 0.f); imp[3000] = 1.f; run (*p, imp, bs); auto y = run (*p, imp, bs);
        size_t pk = 0; for (size_t i = 0; i < y.size(); ++i) if (std::fabs (y[i]) > std::fabs (y[pk])) pk = i;
        CHECK (pk == 3036 && std::fabs (y[pk] - 1.f) < 0.02f, "Character (mix 0): impulso sai em %zu (esperado 3036) amp %.3f", pk, y[pk]);
        double rms[4]; for (int m = 0; m < 4; ++m) { auto q = make (fs, bs); setP (*q, "ch_mode", (float) m); setP (*q, "ch_drive", 80.f); setP (*q, "ch_mix", 100.f); auto x = sine (1000, fs, 48000, 0.7f); run (*q, x, bs); auto z = run (*q, x, bs); rms[m] = rmsDb (z, 24000, 48000); CHECK (finite (z), "Character modo %d: saida finita (RMS %.2f dB)", m, rms[m]); }
        CHECK (std::fabs (rms[1] - rms[2]) > 0.05 || std::fabs (rms[2] - rms[3]) > 0.05, "Character: os modos produzem saidas diferentes (RMS %.2f / %.2f / %.2f dB)", rms[1], rms[2], rms[3]);
    }
    // 14) Morph A/B: ganho medido em 0 %, 50 %, 100 %; estado guardado
    {
        auto p = make (fs, bs); setBandP (*p, 0, "on", 1); setBandP (*p, 0, "freq", 1000); setBandP (*p, 0, "q", 1); setBandP (*p, 0, "gain", -6);
        p->morphCapture (0); setBandP (*p, 0, "gain", 6); p->morphCapture (1); setBandP (*p, 0, "gain", 0);
        CHECK (p->morphHas (0) && p->morphHas (1), "Morph: estados A e B guardados");
        setP (*p, "morph_on", 1); double g[3]; int i = 0;
        for (float m : { 0.f, 50.f, 100.f }) { setP (*p, "morph", m); auto x = sine (1000, fs, 48000); run (*p, x, bs); auto z = run (*p, x, bs); g[i++] = rmsDb (z, 24000, 48000) - rmsDb (x, 24000, 48000); }
        CHECK (std::fabs (g[0] + 6.0) < 0.1 && std::fabs (g[1]) < 0.15 && std::fabs (g[2] - 6.0) < 0.1, "Morph: ganho medido A=%.2f dB | meio=%.2f dB | B=%.2f dB", g[0], g[1], g[2]);
        juce::MemoryBlock mb; p->getStateInformation (mb); auto q = make (fs, bs); q->setStateInformation (mb.getData(), (int) mb.getSize());
        setP (*q, "morph", 100.f); auto x = sine (1000, fs, 48000); run (*q, x, bs); auto z = run (*q, x, bs);
        CHECK (q->morphHas (0) && q->morphHas (1) && std::fabs (rmsDb (z, 24000, 48000) - rmsDb (x, 24000, 48000) - 6.0) < 0.1, "Morph: A/B recuperados do estado do projeto e a funcionar");
    }
    // 15) Smart Ops: aplicar propostas dinamicas (De-Ess/Unmask) com sidechain e reverter
    {
        auto p = make (fs, bs); pq::DeresProposal a; a.freq = 7000; a.q = 2.5; a.gainDb = -6; a.dynamic = true; a.thrDb = -35; a.ratio = 4; a.attackMs = 1; a.releaseMs = 60;
        pq::DeresProposal b = a; b.freq = 2000; b.needsSidechain = true;
        CHECK (p->smartApply ({ a, b }, "teste") == 2, "Smart Ops: 2 bandas dinamicas criadas");
        const auto n24 = p->getBand (23), n23 = p->getBand (22);
        CHECK (n24.on && n24.dyn && std::fabs (n24.range + 6) < 0.05 && std::fabs (n24.gain) < 1e-6 && n23.on && n23.dyn && p->getParamReal ("scext") > 0.5f, "Smart Ops: bandas dinamicas (range -6) e sidechain externo ativado");
        p->smartRevert(); CHECK (! p->getBand (23).on && ! p->getBand (22).on && p->getParamReal ("scext") < 0.5f, "Smart Ops: Revert repoe bandas e sidechain");
    }
    // 16) tudo ligado: sem alocacoes na thread de audio e CPU
    {
        auto p = make (fs, bs); for (int b = 0; b < pq::kNumBands; ++b) { setBandP (*p, b, "on", 1); setBandP (*p, b, "freq", 60.f * std::pow (1.28f, (float) b)); setBandP (*p, b, "gain", (b % 2) ? 3.f : -3.f); setBandP (*p, b, "dyn", (float) (b % 3 == 0)); setBandP (*p, b, "place", (float) (b % 5)); }
        setP (*p, "look", 5); setP (*p, "dh_on", 1); setP (*p, "dh_amount", 60.f); setP (*p, "ch_mode", 3); setP (*p, "ch_os", 2); setP (*p, "morph_on", 0); setP (*p, "phase", 0);
        std::mt19937 rng (5); std::uniform_real_distribution<float> d (-0.3f, 0.3f); juce::AudioBuffer<float> b (2, bs); juce::MidiBuffer midi;
        auto fill = [&] { for (int c = 0; c < 2; ++c) for (int k = 0; k < bs; ++k) b.setSample (c, k, d (rng)); };
        for (int i = 0; i < 400; ++i) { fill(); p->processBlock (b, midi); }
        g_allocs = 0; g_track = true; for (int i = 0; i < 200; ++i) { fill(); p->processBlock (b, midi); } g_track = false;
        CHECK (g_allocs == 0, "tempo real: lookahead + De-harsh + Character 4x + 24 bandas — 200 blocos sem alocacoes (contagem %ld)", g_allocs);
        const int blocks = (int) (fs * 10 / bs); const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < blocks; ++i) { fill(); p->processBlock (b, midi); }
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        INFO ("CPU (24 bandas + lookahead 10 ms + De-harsh + Character Warm 4x), 10 s estereo @48k/512 => %.2f s = %.1f %% de 1 nucleo; latencia %d smp", secs, secs * 10, p->currentLatency());
        CHECK (secs < 10.0, "CPU: tempo real folgado com todos os processadores");
        setP (*p, "phase", 2); for (int i = 0; i < 300; ++i) { fill(); p->processBlock (b, midi); juce::Thread::sleep (3); }
        CHECK (p->currentLatency() == 4096 + 480 + 2048 + 36, "latencia total declarada = %d (FIR 4096 + lookahead 480 + De-harsh 2048 + Character 36)", p->currentLatency());
    }
    // 10) editor: abrir/fechar repetidamente e render PNG do plugin real
    {
        auto p = make (fs, bs);
        p->loadFactoryPreset (1); setBandP (*p, 4, "on", 1); setBandP (*p, 4, "freq", 7000); setBandP (*p, 4, "type", 0); setBandP (*p, 4, "q", 3); setBandP (*p, 4, "dyn", 1); setBandP (*p, 4, "thr", -40); setBandP (*p, 4, "range", -8);
        std::mt19937 rng (3); std::normal_distribution<float> nd (0, 0.1f); std::vector<float> pink (48000 * 2); float b0 = 0, b1 = 0, b2 = 0;
        for (auto& v : pink) { const float w = nd (rng); b0 = 0.99765f * b0 + w * 0.0990460f; b1 = 0.96300f * b1 + w * 0.2965164f; b2 = 0.57000f * b2 + w * 1.0526913f; v = (b0 + b1 + b2 + w * 0.1848f) * 0.5f; }
        for (int i = 0; i < 20; ++i) { std::unique_ptr<juce::AudioProcessorEditor> e (p->createEditor()); CHECK (e != nullptr, "editor aberto/fechado %d", i + 1); if (i == 0) break; }
        for (int i = 0; i < 19; ++i) { std::unique_ptr<juce::AudioProcessorEditor> e (p->createEditor()); }
        CHECK (true, "editor: 20 aberturas/fechos repetidos sem crash");
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        auto* pe = dynamic_cast<PrimeqEditor*> (ed.get()); pe->setSize (1240, 860); pe->selectBand (1);
        for (size_t off = 0; off + 512 <= pink.size(); off += 512) { juce::AudioBuffer<float> b (2, 512); for (int c = 0; c < 2; ++c) std::copy (pink.begin() + (long) off, pink.begin() + (long) off + 512, b.getWritePointer (c)); juce::MidiBuffer m; p->processBlock (b, m); if (off % 4096 == 0) pe->getGraph().tick(); }
        pe->getGraph().tick(); pe->showSmart (true);
        auto img = pe->createComponentSnapshot (pe->getLocalBounds(), true, 1.0f);
        juce::File out ("editor_screenshot.png"); out.deleteFile(); juce::FileOutputStream fo (out); juce::PNGImageFormat().writeImageToStream (img, fo);
        CHECK (img.getWidth() == 1240 && out.getSize() > 20000, "editor real renderizado para PNG (%dx%d, %lld bytes) — grafico com FFT real e curvas dos coeficientes", img.getWidth(), img.getHeight(), (long long) out.getSize());
        LoginOverlay lo ([] {}); lo.setSize (900, 600); auto li = lo.createComponentSnapshot (lo.getLocalBounds(), true, 1.0f);
        juce::File out2 ("editor_login.png"); out2.deleteFile(); juce::FileOutputStream fo2 (out2); juce::PNGImageFormat().writeImageToStream (li, fo2);
        CHECK (out2.getSize() > 3000, "ecra de login renderizado");
    }
    // 17) Mix Unmask ponta-a-ponta: 3 instancias reais (Kick, Bass, Vocals) -> registo -> analise -> aplicar via correio -> imagem
    {
        auto pk = make (fs, bs), pb = make (fs, bs), pv = make (fs, bs), pg = make (fs, bs); pg->setTrackName ("Guitar"); pb->uiMode = 2;
        pk->setTrackName ("Kick"); pb->setTrackName ("Bass"); pv->setTrackName ("Vocals");
        auto& reg = pq::MixRegistry::get();
        CHECK (reg.priority (pk->mixSlot()) == 4 && reg.priority (pb->mixSlot()) == 3 && reg.priority (pv->mixSlot()) == 5, "Mix Unmask: prioridades sugeridas pelo nome (Kick 4, Bass 3, Vocals 5)");
        for (auto* q : { pk.get(), pb.get(), pv.get(), pg.get() }) reg.setAnalysing (q->mixSlot(), true, fs);
        std::mt19937 rng (5); std::normal_distribution<float> nd (0, 1.f);
        const int total = (int) (fs * 12);
        for (int off = 0; off < total; off += bs)
        {
            juce::AudioBuffer<float> bk (2, bs), bb (2, bs), bv (2, bs), bg (2, bs); juce::MidiBuffer m;
            for (int i = 0; i < bs; ++i)
            {
                const double t = (double) (off + i) / fs; const double ph = std::fmod (t, 0.5);
                const float kick = ph < 0.09 ? (float) (0.5 * std::exp (-ph * 25.0) * std::sin (2 * M_PI * (95.0 * ph + 50.0 * (1 - std::exp (-ph * 30.0)) / 30.0 * 0 + 50.0 * ph))) : 0.f;
                const float kick2 = ph < 0.09 ? (float) (0.5 * std::exp (-ph * 25.0) * std::sin (2 * M_PI * 96.0 * ph)) : 0.f; (void) kick;
                const float bass = (float) (0.28 * std::sin (2 * M_PI * 96.0 * t) + 0.12 * std::sin (2 * M_PI * 130.0 * t));
                const float vox = (float) ((0.16 + 0.04 * std::sin (2 * M_PI * 3.0 * t)) * (std::sin (2 * M_PI * 400.0 * t) + 0.6 * std::sin (2 * M_PI * 1200.0 * t) + 0.3 * std::sin (2 * M_PI * 2600.0 * t))) + 0.002f * nd (rng);
                for (int c = 0; c < 2; ++c) { bk.setSample (c, i, kick2); bb.setSample (c, i, bass); bv.setSample (c, i, vox); const double phs = c == 0 ? 0.0 : 1.0; bg.setSample (c, i, (float) (0.10 * (std::sin (2 * M_PI * 500.0 * t + phs) + 0.7 * std::sin (2 * M_PI * 1500.0 * t + phs)))); }
            }
            pk->processBlock (bk, m); pb->processBlock (bb, m); pv->processBlock (bv, m); pg->processBlock (bg, m);
        }
        std::unique_ptr<juce::AudioProcessorEditor> ed (pb->createEditor()); auto* pe = dynamic_cast<PrimeqEditor*> (ed.get()); pe->setSize (1240, 860);
        pe->showMix (true); pe->getMix().analyse(); const auto& R = pe->getMix().result();
        const pq::MixPair* kb = nullptr; for (auto& pr : R.pairs) if (R.curve.size() == 4 && pr.victimIdx >= 0 && ! pr.conflicts.empty() && pr.conflicts[0].freq < 160) { kb = &pr; break; }
        {   // overlay no grafico principal: o baixo (vitima do kick) recebe zonas de masking
            const auto& Z = pe->getGraph().zones; bool lowZone = false; for (auto& z : Z) if (z.kind == OverlayZone::Masking && z.f0 < 200 && z.f1 > 60) lowZone = true;
            CHECK (! Z.empty() && lowZone, "Overlay: grafico principal recebe %d zona(s); masking nos graves marcado na pista do baixo", (int) Z.size());
            pe->showMix (false); pe->resized(); auto im = pe->createComponentSnapshot (pe->getLocalBounds(), true, 1.0f); juce::File f ("overlay_zonas.png"); f.deleteFile(); juce::FileOutputStream fo (f); juce::PNGImageFormat().writeImageToStream (im, fo); pe->showMix (true);
        }
        INFO ("Mix Unmask real: %d pares; %s", (int) R.pairs.size(), R.message.c_str());
        CHECK (kb != nullptr, "Mix Unmask: conflito de graves detetado entre as instancias reais (kick/bass)");
        if (kb)
        {
            const auto& c = kb->conflicts[0];
            INFO ("Conflito %.0f Hz: EQ %.0f%% | Dinamica %.0f%% | Sidechain %.0f%% | recomendado %d", c.freq, 100 * c.eq.score, 100 * c.dyn.score, 100 * c.sc.score, (int) c.best);
            CHECK (c.sc.score >= c.eq.score, "Mix Unmask: kick percussivo vs baixo continuo => sidechain >= EQ estatico");
            // aplicar o recomendado ao baixo via correio
            const int vslot = reg.list (nullptr)[0].slot; (void) vslot;
            pq::MixMail ml; ml.undoName = "Mix Unmask"; ml.props = { c.pSc };
            const int nb = pb->smartApply (ml.props, "Mix Unmask");
            CHECK (nb == 1 && pb->getParamReal ("scext") > 0.5f, "Mix Unmask: aplicar sidechain cria 1 banda dinamica e liga sidechain externo (scext)");
            pb->smartRevert(); CHECK (pb->getParamReal ("scext") < 0.5f, "Mix Unmask: revert repoe o sidechain");
        }
        pq::MixMail m1; m1.undoName = "Mix Unmask"; pq::DeresProposal dp; dp.freq = 110; dp.q = 3; dp.gainDb = -3; m1.props = { dp }; reg.post (pb->mixSlot(), m1);
        pb->pollMixMail(); CHECK (pb->getBand (0).on || pb->getBand (1).on || pb->smartCanRevert(), "Mix Unmask: correio entre instancias aplicado na pista alvo");
        {
            auto& mp = pe->getMix(); const auto& SP = mp.spaceResult();
            INFO ("Espaco: %d achados", (int) SP.findings.size()); for (auto& f : SP.findings) INFO ("  - %s", f.text.c_str());
            CHECK (R.curve.size() == 4, "Assistente: 4 pistas analisadas (grupo G1)");
            bool crowd = false; for (auto& f : SP.findings) if (f.kind == pq::SpaceFinding::CenterCrowd || f.kind == pq::SpaceFinding::MidCut) crowd = true;
            CHECK (crowd, "Espaco: vocais e guitarra centrados => achado espacial real a partir de audio");
            mp.setSymptom (0); CHECK (! mp.doctor().evidence.empty(), "Mix Doctor (audio real): %d evidencias, %d hipoteses", (int) mp.doctor().evidence.size(), (int) mp.doctor().hyps.size());
            mp.setIntent (0, 1, 60); CHECK (! mp.intentPlan().props.empty() && ! mp.intentPlan().explanation.empty(), "Intent Clarity: plano com explicacao");
            const int nI = pb->smartApply (mp.intentPlan().props, "Intent EQ"); CHECK (nI >= 2, "Intent: %d bandas normais criadas", nI); pb->smartRevert();
            CHECK (mp.buildReport().contains ("Masking") && mp.buildReport().contains ("Limitacoes"), "Relatorio gerado (%d caracteres)", (int) mp.buildReport().length());
            const char* nm[5] = { "mask", "space", "doctor", "intent", "scen" };
            for (int t = 0; t < 5; ++t)
            {
                if (t == 4 && mp.consequences().sc.size() != 4) continue; mp.setTab (t); if (t == 3) mp.setIntent (0, 1, 60); if (t == 2) mp.setSymptom (0);
                auto im = pe->createComponentSnapshot (pe->getLocalBounds(), true, 1.0f); juce::File f (juce::String ("assist_") + nm[t] + ".png"); f.deleteFile(); juce::FileOutputStream fs2 (f); juce::PNGImageFormat().writeImageToStream (im, fs2);
                CHECK (f.getSize() > 15000, "separador %s renderizado (%lld bytes)", nm[t], (long long) f.getSize());
            }
            for (int md = 0; md < 3; ++md) { pb->uiMode = md; mp.setTab (0); pe->resized(); }
            pb->uiMode = 2; mp.setTab (0);
        }
        pe->resized(); auto img = pe->createComponentSnapshot (pe->getLocalBounds(), true, 1.0f);
        juce::File out ("mix_unmask.png"); out.deleteFile(); juce::FileOutputStream fo (out); juce::PNGImageFormat().writeImageToStream (img, fo);
        CHECK (out.getSize() > 20000, "pagina Mix Unmask renderizada (%lld bytes)", (long long) out.getSize());
    }
    // 18) presets de fabrica: quantidade, unicidade, categorias, carga segura e audio finito
    {
        const auto& fpv = PrimeqProcessor::factoryPresets(); std::set<std::string> names, cats; bool okBands = true, okFinite = true; int nCat = 0;
        auto p = make (fs, bs); std::mt19937 rng (9); std::normal_distribution<float> nd (0, 0.1f);
        for (size_t i = 0; i < fpv.size(); ++i)
        {
            names.insert (fpv[i].name); const std::string n = fpv[i].name; const auto sl = n.find ('/'); if (sl != std::string::npos) cats.insert (n.substr (0, sl));
            p->loadFactoryPreset ((int) i);
            pq::BandParams bp[pq::kNumBands]; p->readBands (bp); int on = 0; for (auto& b : bp) if (b.on) { ++on; if (b.freq < 10 || b.freq > 24000 || std::fabs (b.gain) > 12.01 || b.q < 0.1 || b.q > 40) okBands = false; }
            if (n.find ("Init") == std::string::npos && on == 0 && n.find ('/') != std::string::npos) okBands = false;
            for (int k = 0; k < 3; ++k) { juce::AudioBuffer<float> b (2, bs); for (int c = 0; c < 2; ++c) for (int j = 0; j < bs; ++j) b.setSample (c, j, nd (rng)); juce::MidiBuffer m; p->processBlock (b, m); for (int c = 0; c < 2; ++c) for (int j = 0; j < bs; ++j) if (! std::isfinite (b.getSample (c, j))) okFinite = false; }
        }
        nCat = (int) cats.size();
        INFO ("Presets de fabrica: %d (%d categorias)", (int) fpv.size(), nCat);
        CHECK (fpv.size() >= 100 && names.size() == fpv.size(), "Presets: %d de fabrica, todos com nome unico (pedido: >= 80)", (int) fpv.size());
        CHECK (nCat >= 12, "Presets: %d categorias (Vocals, Drums, Bass, Guitar, Keys, Synth, Orquestra, Mixbus, Master, Corretivo, G\xC3\xA9neros, Sends, Utility)", nCat);
        CHECK (okBands, "Presets: todas as bandas dentro de limites seguros (|ganho| <= 12 dB, Q e frequencia validos)");
        CHECK (okFinite, "Presets: carregar e processar ruido com cada preset => audio finito");
        p->loadFactoryPreset (0); pq::BandParams z[pq::kNumBands]; p->readBands (z); int on0 = 0; for (auto& b : z) if (b.on) ++on0; CHECK (on0 == 0, "Preset Init (Flat) deixa todas as bandas desligadas");
    }
    // 19) stress: sample rates 44.1-192 kHz, blocos 0..4096 aleatorios, automacao em todos os parametros, troca de fase a meio, re-prepare
    {
        std::mt19937 rng (7); bool okFinite = true, okBound = true, okLat = true; double worst = 0; int configs = 0;
        const int sizes[] { 1, 2, 3, 7, 32, 33, 64, 100, 128, 480, 512, 1000, 1024, 2048, 4096 };
        for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
            for (int phase = 0; phase < 3; ++phase)
            {
                auto p = make (sr, 512); setP (*p, "phase", (float) phase); ++configs;
                std::uniform_real_distribution<float> u (0.f, 1.f), nz (-0.5f, 0.5f); juce::MidiBuffer midi;
                const int total = (int) sr; int done = 0; int latencyAtStart = -1;
                while (done < total)
                {
                    const int n = sizes[rng() % (sizeof (sizes) / sizeof (sizes[0]))];
                    for (int k = 0; k < 6; ++k)          // automacao agressiva
                    {
                        const int b = (int) (rng() % 24);
                        setBandP (*p, b, "on", u (rng) > 0.4f ? 1.f : 0.f); setBandP (*p, b, "freq", 20.f * std::pow (1000.f, u (rng)));
                        setBandP (*p, b, "gain", -24.f + 48.f * u (rng)); setBandP (*p, b, "q", 0.1f * std::pow (300.f, u (rng))); setBandP (*p, b, "type", (float) (rng() % 8));
                    }
                    juce::AudioBuffer<float> buf (2, n); for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i) buf.setSample (c, i, nz (rng));
                    p->processBlock (buf, midi);
                    for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i) { const float v = buf.getSample (c, i); if (! std::isfinite (v)) okFinite = false; else worst = std::max (worst, (double) std::fabs (v)); }
                    if (latencyAtStart < 0) latencyAtStart = p->currentLatency(); else if (phase != 2 && p->currentLatency() != latencyAtStart) okLat = latencyAtStart == p->currentLatency();
                    done += n;
                }
                if (worst > 200.0) okBound = false;
                juce::AudioBuffer<float> e (2, 0); p->processBlock (e, midi);   // bloco vazio nao pode rebentar
                p->prepareToPlay (sr * 0.5 + 100.0, 256); p->prepareToPlay (sr, 1024);   // re-prepare com taxas diferentes
            }
        INFO ("Stress: %d configuracoes (5 taxas x 3 modos de fase), pico max %.1f", configs, worst);
        CHECK (okFinite, "Stress: audio sempre finito (sem NaN/Inf) com automacao agressiva e blocos de 1 a 4096");
        CHECK (okBound, "Stress: saida limitada (pico <= 200) mesmo com bandas a +-24 dB e Q extremos");
        CHECK (okLat, "Stress: latencia reportada estavel dentro de cada modo");
    }
    // 20) ABX: lado A = original exacto; lado B = processado mesmo com bypass ligado; compensacao de nivel aplicada
    {
        auto p = make (fs, bs); setBandP (*p, 0, "on", 1); setBandP (*p, 0, "freq", 1000); setBandP (*p, 0, "gain", 9); setBandP (*p, 0, "q", 1);
        auto x = sine (1000, fs, 48000);
        p->abxSide.store (1); run (*p, x, bs); auto yA = run (*p, x, bs); double eA = 0; for (size_t i = 24000; i < x.size(); ++i) eA = std::max (eA, (double) std::fabs (yA[i] - x[i]));
        CHECK (eA < 1e-5, "ABX lado A: sai o original (erro max %.2e) mesmo com a banda a +9 dB", eA);
        setP (*p, "bypass", 1); p->abxSide.store (2); run (*p, x, bs); auto yB = run (*p, x, bs); const double gB = rmsDb (yB, 24000, 48000) - rmsDb (x, 24000, 48000);
        CHECK (std::fabs (gB - 9.0) < 0.3, "ABX lado B: processado mesmo com bypass ligado (%.2f dB)", gB);
        p->abxComp.store (std::pow (10.f, -9.f / 20.f)); for (int k = 0; k < 6; ++k) run (*p, x, bs); auto yC = run (*p, x, bs); const double gC = rmsDb (yC, 24000, 48000) - rmsDb (x, 24000, 48000);
        CHECK (std::fabs (gC) < 0.5, "ABX lado B com loudness igualado: diferenca de nivel %.2f dB", gC);
        p->abxSide.store (0); setP (*p, "bypass", 0);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor()); auto* pe = dynamic_cast<PrimeqEditor*> (ed.get()); pe->setSize (1240, 860);
        CHECK (p->abxSide.load() == 0, "ABX: abrir o editor nao altera o som (painel escondido)");
        pe->showAbx (true); CHECK (p->abxSide.load() == 1 && ! pe->getAbx().isDone(), "ABX: abrir o painel arranca o teste no lado A");
        auto& ab = pe->getAbx(); ab.answer (false); CHECK (ab.session().answered() == 0, "ABX: so se pode responder depois de ouvir X");
        for (int i = 0; i < 10; ++i) { ab.listen (ab.session().xIsBNow() ? 2 : 1, true); ab.answer (ab.session().xIsBNow()); }
        CHECK (ab.isDone() && ab.session().correct() == 10 && p->abxSide.load() == 0, "ABX: 10 tentativas respondidas => concluido e som volta ao normal");
        auto im = pe->createComponentSnapshot (pe->getLocalBounds(), true, 1.0f); juce::File f ("abx_painel.png"); f.deleteFile(); juce::FileOutputStream fo (f); juce::PNGImageFormat().writeImageToStream (im, fo);
        pe->showAbx (false); CHECK (p->abxSide.load() == 0, "ABX: fechar o painel repoe o som normal");
    }
    // 21) onboarding: 6 passos, fecha no fim e avisa (para gravar "visto")
    {
        pqui::i18n::lang() = 0; OnboardingOverlay o; o.setSize (1000, 700); bool closed = false; o.onClose = [&] { closed = true; };
        o.open(); CHECK (o.isVisible() && o.pages() == 6, "Onboarding: abre com %d passos", o.pages());
        for (int i = 0; i < o.pages(); ++i) { o.gotoPage (i); if (i == 0 || i == 3) { auto im = o.createComponentSnapshot (o.getLocalBounds(), true, 1.0f); juce::File f (juce::String ("onboarding_") + juce::String (i + 1) + ".png"); f.deleteFile(); juce::FileOutputStream fo (f); juce::PNGImageFormat().writeImageToStream (im, fo); } }
        o.gotoPage (5); { int nb = 0; for (auto* c : o.getChildren()) if (auto* b = dynamic_cast<juce::TextButton*> (c)) { ++nb; if (b->getButtonText().startsWith ("Come") && b->onClick) b->onClick(); } (void) nb; }
        CHECK (closed && ! o.isVisible(), "Onboarding: 'Come\xC3\xA7""ar' no ultimo passo fecha o guia e notifica");
    }
    // 22) idioma PT/EN: dicionario, troca imediata e persistencia
    {
        using pqui::T8;
        pqui::i18n::lang() = 0; CHECK (T8 ("Guia rápido") == juce::String (juce::CharPointer_UTF8 ("Guia r\xC3\xA1pido")), "i18n PT: texto-fonte intacto");
        pqui::i18n::lang() = 1; CHECK (T8 ("Guia rápido") == "Quick guide" && T8 ("Texto sem entrada") == "Texto sem entrada", "i18n EN: traduz e deixa passar o que nao esta no dicionario");
        CHECK (pqui::tr (juce::String (juce::CharPointer_UTF8 ("ESPA\xC3\x87O"))) == "SPACE", "i18n EN: tr() de texto em tempo de execucao");
        auto p = make (48000.0, 512, false); auto ed = std::unique_ptr<juce::AudioProcessorEditor> (p->createEditor()); auto* pe = dynamic_cast<PrimeqEditor*> (ed.get());
        pqui::i18n::lang() = 0; pe->changeLanguage (1); CHECK (pqui::i18n::lang().load() == 1 && AccessManager::get().language() == 1, "i18n: changeLanguage(EN) aplica e persiste");
        { auto im = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f); juce::File f ("editor_en.png"); f.deleteFile(); juce::FileOutputStream fo (f); juce::PNGImageFormat().writeImageToStream (im, fo); }
        pe->changeLanguage (0); CHECK (pqui::i18n::lang().load() == 0 && AccessManager::get().language() == 0, "i18n: voltar a PT");
        ed.reset();
    }
    // 23) STUDIO: ABX por banda, cofre persistente, assinatura entre instancias, alvo/tradutor com medicao real, palavras, ficha
    {
        pqui::i18n::lang() = 0;
        // ABX por banda: A = sem a banda (0 dB), B = com a banda (+9 dB), mesmo com BYPASS ligado
        {
            auto p = make (fs, bs); setBandP (*p, 0, "on", 1); setBandP (*p, 0, "freq", 1000); setBandP (*p, 0, "gain", 9); setBandP (*p, 0, "q", 1); setBandP (*p, 1, "on", 1); setBandP (*p, 1, "freq", 100); setBandP (*p, 1, "gain", 0); auto x = sine (1000, fs, 48000);
            p->abxBand.store (0); p->abxSide.store (1); run (*p, x, bs); auto yA = run (*p, x, bs); const double gA = rmsDb (yA, 24000, 48000) - rmsDb (x, 24000, 48000);
            setP (*p, "bypass", 1); p->abxSide.store (2); run (*p, x, bs); auto yB = run (*p, x, bs); const double gB = rmsDb (yB, 24000, 48000) - rmsDb (x, 24000, 48000);
            CHECK (std::fabs (gA) < 0.3 && std::fabs (gB - 9.0) < 0.3, "ABX por banda: A sem a banda = %.2f dB, B com a banda = %.2f dB (BYPASS ligado nao interfere)", gA, gB);
            CHECK (p->abxLvlDb[0].load() > -100.f && p->abxLvlDb[1].load() > p->abxLvlDb[0].load() + 6.f, "ABX por banda: processador mede o nivel dos dois lados (A %.1f dB, B %.1f dB)", p->abxLvlDb[0].load(), p->abxLvlDb[1].load());
            p->abxSide.store (0); p->abxBand.store (-1);
        }
        // cofre + persistencia + restauro
        {
            auto p = make (fs, bs); p->loadFactoryPreset (0); setBandP (*p, 5, "on", 1); setBandP (*p, 5, "freq", 2222); p->logDecision ("cortei 250 Hz por masking com o baixo", "cut 250 Hz because of masking with the bass", "note");
            const auto d0 = p->decisions(); CHECK (d0.size() >= 2 && d0.back().pt.find ("masking") != std::string::npos, "Cofre: preset e nota registados (%d entradas)", (int) d0.size());
            p->customTargetSet (pq::studio::normalised (pq::studio::targetCurve (1, pq::studio::TargetAdjust())));
            juce::MemoryBlock mb; p->getStateInformation (mb); auto q = make (fs, bs); q->setStateInformation (mb.getData(), (int) mb.getSize());
            pq::studio::Curve cc; const auto d1 = q->decisions(); CHECK (d1.size() == d0.size() && d1.back().en.find ("bass") != std::string::npos && q->customTargetGet (cc), "Cofre: decisoes e alvo pessoal sobrevivem a gravar/carregar o estado (%d)", (int) d1.size());
            setBandP (*q, 5, "freq", 4000); q->restoreDecision ((int) d1.size() - 1); CHECK (std::fabs (q->getBand (5).freq - 2222) < 1.0, "Cofre: restaurar volta ao estado desse momento (freq %.0f Hz)", q->getBand (5).freq);
        }
        // assinatura: bandas da pista A chegam a pista B pelo correio
        {
            auto a = make (fs, bs), b = make (fs, bs); setBandP (*a, 0, "on", 1); setBandP (*a, 0, "type", 3); setBandP (*a, 0, "freq", 90); setBandP (*a, 0, "slope", 3); setBandP (*a, 1, "on", 1); setBandP (*a, 1, "freq", 3100); setBandP (*a, 1, "gain", 2.5); setBandP (*a, 1, "q", 1.3);
            pq::BandParams ab[pq::kNumBands]; a->readBands (ab); pq::MixMail m; m.props = pq::studio::toProposals (ab); m.undoName = "Assinatura"; pq::MixRegistry::get().post (b->mixSlot(), m); b->pollMixMail();
            int hp = 0, pres = 0; for (int i = 0; i < pq::kNumBands; ++i) { const auto x = b->getBand (i); if (x.on && x.type == pq::BandType::HighPass && std::fabs (x.freq - 90) < 1 && x.slope == 3) ++hp; if (x.on && std::fabs (x.freq - 3100) < 1 && std::fabs (x.gain - 2.5) < 0.01) ++pres; }
            CHECK (hp == 1 && pres == 1, "Assinatura: HP 90 Hz (declive preservado) e Bell 3,1 kHz chegam a outra pista");
        }
        // alvo e tradutor com medicao REAL (ruido rosa filtrado + sinal)
        {
            auto p = make (fs, bs); p->setAnalysing (true); std::vector<float> x (48000 * 6); unsigned st = 12345; double b0 = 0, b1 = 0, b2 = 0;
            for (auto& v : x) { st = st * 1664525u + 1013904223u; const double w = ((st >> 8) / 8388608.0 - 1.0) * 0.5; b0 = 0.99765 * b0 + w * 0.0990460; b1 = 0.96300 * b1 + w * 0.2965164; b2 = 0.57000 * b2 + w * 1.0526913; v = (float) ((b0 + b1 + b2 + w * 0.1848) * 0.35); }
            run (*p, x, bs, 1); juce::Thread::sleep (300); pq::studio::Curve c; const bool got = p->ownCurve (c); CHECK (got, "Studio: pista medida (28 bandas) apos ruido rosa");
            const auto tr = pq::studio::translate (c, 0); CHECK (tr.ok && tr.retainedPct < 100.0, "Studio: tradutor sobre medicao real (%.0f%% audivel no telemovel)", tr.retainedPct);
            const auto dv = pq::studio::compare (c, pq::studio::targetCurve (0, pq::studio::TargetAdjust())); CHECK (dv.ok && dv.rms < 8.0, "Studio: ruido rosa vs alvo neutro, desvio %.1f dB RMS", dv.rms);
            auto ed = std::unique_ptr<juce::AudioProcessorEditor> (p->createEditor()); auto* pe = dynamic_cast<PrimeqEditor*> (ed.get()); ed->setSize (1240, 860);
            pe->showStudio (true); auto& S = pe->getStudio(); S.setTab (StudioPanel::TTarget); CHECK (S.isVisible() && S.deviation().ok && ! S.lines().empty(), "Painel STUDIO: aba ALVO mostra o desvio (%d linhas)", (int) S.lines().size());
            { auto im = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f); juce::File f ("studio_alvo.png"); f.deleteFile(); juce::FileOutputStream fo (f); juce::PNGImageFormat().writeImageToStream (im, fo); }
            S.setTab (StudioPanel::TTrans); CHECK (S.translation().ok, "Painel STUDIO: aba TRADUTOR calcula");
            { auto im = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f); juce::File f ("studio_tradutor.png"); f.deleteFile(); juce::FileOutputStream fo (f); juce::PNGImageFormat().writeImageToStream (im, fo); }
            S.setTab (StudioPanel::TWords); S.setWords (juce::String (juce::CharPointer_UTF8 ("menos lama e mais presen\xC3\xA7""a"))); CHECK (S.pendingBands() >= 2, "Painel STUDIO: PALAVRAS interpreta (%d bandas)", S.pendingBands());
            { auto im = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f); juce::File f ("studio_palavras.png"); f.deleteFile(); juce::FileOutputStream fo (f); juce::PNGImageFormat().writeImageToStream (im, fo); }
            S.setTab (StudioPanel::TVault); { auto im = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f); juce::File f ("studio_cofre.png"); f.deleteFile(); juce::FileOutputStream fo (f); juce::PNGImageFormat().writeImageToStream (im, fo); }
            S.setTab (StudioPanel::TTeach); { auto im = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f); juce::File f ("studio_ensino.png"); f.deleteFile(); juce::FileOutputStream fo (f); juce::PNGImageFormat().writeImageToStream (im, fo); }
            const auto html = S.sheet(); CHECK (html.find ("<svg") != std::string::npos && html.size() > 3000, "Ficha: HTML gerado a partir do painel (%zu bytes)", html.size());
            pqui::i18n::lang() = 1; pe->changeLanguage (1); S.setTab (StudioPanel::TTarget); { auto im = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f); juce::File f ("studio_alvo_en.png"); f.deleteFile(); juce::FileOutputStream fo (f); juce::PNGImageFormat().writeImageToStream (im, fo); }
            pe->changeLanguage (0); pe->showStudio (false);
            p->setTranslator (3); CHECK (p->translatorDevice() == 3, "Tradutor: monitorizacao ligada"); p->setTranslator (0);
        }
    }
    // 24) skins + EQ Transiente/Tonal + modos do Smart
    {
        auto p = make (48000.0, 512); p->loadFactoryPreset (1); setBandP (*p, 1, "on", 1); setBandP (*p, 1, "ts", 1);
        CHECK (p->getBand (1).ts == 1, "T/S: parametro ts por banda lido pelo modelo (ts=%d)", p->getBand (1).ts);
        { pq::BandParams a[pq::kNumBands]; p->readBands (a); p->setBand (2, a[1]); CHECK (p->getBand (2).ts == 1, "T/S: setBand preserva ts"); }
        setBandP (*p, 1, "ts", 2); setP (*p, "ts_sep", 80); setP (*p, "ts_smooth", 20);
        { juce::MemoryBlock mb; p->getStateInformation (mb); auto q = make (48000.0, 512); q->setStateInformation (mb.getData(), (int) mb.getSize()); CHECK (q->getBand (1).ts == 2 && std::fabs (q->getParamReal ("ts_sep") - 80.f) < 0.2f, "T/S: estado guarda e repoe ts e separacao"); }
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor()); auto* pe = dynamic_cast<PrimeqEditor*> (ed.get()); pe->setSize (1240, 860); pe->selectBand (1); pe->showSmart (true);
        const char* names[3] = { "prime", "ouro", "indigo" }; bool allDiff = true; juce::uint32 prev = 0;
        for (int i = 0; i < 3; ++i)
        {
            pe->changeSkin (i); CHECK (pqui::skinIndex() == i && pqui::kTeal.getARGB() == pqui::paletteOf (i).accent && AccessManager::get().skin() == i, "Skin %s aplicada e guardada", pqui::skinName (i));
            if (i > 0 && pqui::kTeal.getARGB() == prev) allDiff = false; prev = pqui::kTeal.getARGB();
            auto im = pe->createComponentSnapshot (pe->getLocalBounds(), true, 1.0f); juce::File f (juce::String ("skin_") + names[i] + ".png"); f.deleteFile(); juce::FileOutputStream fo (f); juce::PNGImageFormat().writeImageToStream (im, fo);
            const auto px = im.getPixelAt (600, 400); CHECK (im.getWidth() == 1240 && px.getAlpha() > 0, "Skin %s: snapshot renderizado", pqui::skinName (i));
        }
        CHECK (allDiff, "Skins: as 3 paletas tem acentos distintos");
        pe->changeSkin (0);
    }
    // 25) motor espectral T/S: latencia declarada e alinhamento (banda neutra => saida = entrada atrasada)
    {
        auto p = make (48000.0, 512); setBandP (*p, 1, "on", 1); setBandP (*p, 1, "gain", 0); setBandP (*p, 1, "ts", 1); setBandP (*p, 2, "on", 1); setBandP (*p, 2, "gain", 0);
        const int lat0 = p->getLatencySamples(); setP (*p, "ts_engine", 1); const int lat1 = p->getLatencySamples();
        CHECK (lat1 - lat0 == pq::Hpss::kLatency, "T/S espectral: latencia declarada sobe %d amostras (%d -> %d)", pq::Hpss::kLatency, lat0, lat1);
        const int n = 512 * 188; std::vector<float> in ((size_t) n), out ((size_t) n); unsigned st = 99; for (int i = 0; i < n; ++i) { st = st * 1664525u + 1013904223u; in[(size_t) i] = 0.2f * std::sin (0.07f * (float) i) + 0.1f * ((st >> 8) / 8388608.f - 1.f); }
        for (int off = 0; off < n; off += 512) { juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer mb; for (int i = 0; i < 512; ++i) { b.setSample (0, i, in[(size_t) (off + i)]); b.setSample (1, i, in[(size_t) (off + i)]); } p->processBlock (b, mb); for (int i = 0; i < 512; ++i) out[(size_t) (off + i)] = b.getSample (0, i); }
        const int L = p->getLatencySamples(); double err = 0, ref = 0; for (int i = 48000; i < n; ++i) { const double d = out[(size_t) i] - in[(size_t) (i - L)]; err += d * d; ref += (double) in[(size_t) (i - L)] * in[(size_t) (i - L)]; }
        CHECK (10.0 * std::log10 (err / ref + 1e-30) < -60.0, "T/S espectral: banda neutra => saida igual a entrada atrasada (%.1f dB)", 10.0 * std::log10 (err / ref + 1e-30));
    }
    // 26) Mapa 3D + overlay colectivo: varias instancias (mesmo processo) com quadros sinteticos
    {
        std::vector<std::unique_ptr<PrimeqProcessor>> ps; const char* nm[4] = { "Kick", "Bass", "Vocals", "Guitar" };
        const int peakBand[4] = { 6, 7, 18, 19 }; const float peakDb[4] = { -12.f, -13.f, -14.f, -30.f };
        for (int i = 0; i < 4; ++i)
        {
            ps.push_back (make (48000.0, 512)); auto& q = *ps.back(); q.setTrackName (nm[i], true); q.setAnalysing (true);
            for (int f = 0; f < 80; ++f)
            {
                float db[pq::kMixBands]; for (int b = 0; b < pq::kMixBands; ++b) db[b] = peakDb[i] - 5.f * std::fabs ((float) (b - peakBand[i]));
                db[peakBand[i]] = peakDb[i]; pq::MixRegistry::get().pushFrame (q.mixSlot(), db, peakDb[i]);
            }
            pq::SpatialStats st; for (int b = 0; b < pq::kMixBands; ++b) st.pan[b] = (i - 1.5f) / 2.f; pq::MixRegistry::get().pushStats (q.mixSlot(), st);
        }
        const auto c = pq::Collective::gather (pq::MixRegistry::get(), ps[0].get());
        CHECK (c.tracks.size() >= 4, "Colectivo: %d pistas recolhidas", (int) c.tracks.size());
        CHECK (std::fabs (c.refDb - (-12.f)) < 0.5f, "Colectivo: referencia = pista mais forte (%.1f dB)", c.refDb);
        float clashKB = 0.f; for (int b = 5; b <= 8; ++b) clashKB = std::max (clashKB, c.clash[(size_t) b]);
        CHECK (clashKB > 0.25f, "Colectivo: choque Kick/Bass detectado (sev %.2f)", clashKB);
        CHECK (c.clash[0] < 0.01f && c.clash[27] < 0.01f, "Colectivo: sem falsos choques nos extremos");
        { pq::Collective one; one.tracks.push_back (c.tracks[0]); pq::Collective::analyse (one); CHECK (one.clashBands == 0, "Colectivo: uma pista so nao tem choques"); }
        std::unique_ptr<juce::AudioProcessorEditor> ed (ps[0]->createEditor()); auto* pe = dynamic_cast<PrimeqEditor*> (ed.get()); pe->setSize (1240, 860);
        pe->showMap (true); pe->getMap().refresh(); CHECK (pe->getMap().current().tracks.size() >= 4, "Mapa 3D: editor recolhe %d pistas", (int) pe->getMap().current().tracks.size());
        for (int m = 0; m < 2; ++m)
        {
            pe->getMap().panMode = m == 1; pe->getMap().repaint(); auto im = pe->createComponentSnapshot (pe->getLocalBounds(), true, 1.0f);
            juce::File f (m ? "mapa3d_pan.png" : "mapa3d_pista.png"); f.deleteFile(); juce::FileOutputStream fo (f); juce::PNGImageFormat().writeImageToStream (im, fo); CHECK (im.getWidth() == 1240, "Mapa 3D: snapshot %s", m ? "pan" : "pista");
        }
        pe->getMap().yaw = 1.2f; pe->getMap().pitch = 0.9f; pe->getMap().repaint(); { auto im = pe->createComponentSnapshot (pe->getLocalBounds(), true, 1.0f); juce::File f ("mapa3d_rodado.png"); f.deleteFile(); juce::FileOutputStream fo (f); juce::PNGImageFormat().writeImageToStream (im, fo); }
        pe->showMap (false); pe->getGraph().setCollective (true); pe->getGraph().tick(); pe->resized();
        CHECK (pe->getGraph().coll.tracks.size() >= 4, "Overlay colectivo: grafico recolhe %d pistas", (int) pe->getGraph().coll.tracks.size());
        { auto im = pe->createComponentSnapshot (pe->getLocalBounds(), true, 1.0f); juce::File f ("overlay_colectivo.png"); f.deleteFile(); juce::FileOutputStream fo (f); juce::PNGImageFormat().writeImageToStream (im, fo); }
        pqui::i18n::lang() = 1; pe->changeLanguage (1); pe->showMap (true); { auto im = pe->createComponentSnapshot (pe->getLocalBounds(), true, 1.0f); juce::File f ("mapa3d_en.png"); f.deleteFile(); juce::FileOutputStream fo (f); juce::PNGImageFormat().writeImageToStream (im, fo); }
        pe->changeLanguage (0); pqui::i18n::lang() = 0; pe->showMap (false);
    }
    // 27) Visual Mixer: pan/width por pista no audio, controlo entre instancias, snapshots, Balance Assistant
    {
        auto run = [&] (PrimeqProcessor& q, float* oL, float* oR) { juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer mb; for (int k = 0; k < 40; ++k) { for (int i = 0; i < 512; ++i) { const float t = (float) (k * 512 + i); b.setSample (0, i, 0.4f * std::sin (0.05f * t)); b.setSample (1, i, 0.2f * std::sin (0.05f * t + 0.5f)); } q.processBlock (b, mb); } for (int i = 0; i < 512; ++i) { oL[i] = b.getSample (0, i); oR[i] = b.getSample (1, i); } };
        float aL[512], aR[512], bL[512], bR[512];
        { auto p0 = make (48000.0, 512); run (*p0, aL, aR); auto p1 = make (48000.0, 512); setP (*p1, "trk_width", 0); run (*p1, bL, bR); double dLR = 0, dm = 0; for (int i = 0; i < 512; ++i) { dLR = std::max (dLR, (double) std::fabs (bL[i] - bR[i])); dm = std::max (dm, (double) std::fabs (bL[i] - 0.5f * (aL[i] + aR[i]))); }
          CHECK (dLR < 1e-5 && dm < 1e-4, "Width 0 %%: L == R == soma mono (|L-R| %.1e, desvio ao M %.1e)", dLR, dm); }
        { auto p1 = make (48000.0, 512); setP (*p1, "trk_pan", 100); run (*p1, bL, bR); double eL = 0, eR = 0, rL = 0, rR = 0; for (int i = 0; i < 512; ++i) { eL += bL[i] * bL[i]; eR += bR[i] * bR[i]; rL += aL[i] * aL[i]; rR += aR[i] * aR[i]; }
          CHECK (eL < 1e-6 * rL && std::fabs (eR - rR) < 1e-6 * rR, "Pan +100 %%: canal esquerdo silenciado, direito intacto (L %.1e, R %.1e)", eL / rL, eR / rR); }
        { auto p0 = make (48000.0, 512); float cL[512], cR[512]; run (*p0, cL, cR); double d = 0; for (int i = 0; i < 512; ++i) d = std::max (d, (double) std::fabs (cL[i] - aL[i]) + std::fabs (cR[i] - aR[i])); CHECK (d == 0.0, "Pan 0 / Width 100 %%: saida identica (neutro)"); }
        std::vector<std::unique_ptr<PrimeqProcessor>> ps; const char* nm[4] = { "Vocals", "Kick", "Bass", "Guitar" }; const float lv[4] = { -14.f, -10.f, -11.f, -18.f };
        for (int i = 0; i < 4; ++i) { ps.push_back (make (48000.0, 512)); auto& q = *ps.back(); q.setTrackName (nm[i], true); pq::MixRegistry::get().setPriority (q.mixSlot(), i == 0 ? 5 : (i == 2 ? 3 : (i == 1 ? 3 : 2))); q.setAnalysing (true);
            for (int f = 0; f < 60; ++f) { float db[pq::kMixBands]; for (int b = 0; b < pq::kMixBands; ++b) db[b] = lv[i] - 4.f * std::fabs ((float) (b - 6 - 4 * i)); pq::MixRegistry::get().pushFrame (q.mixSlot(), db, lv[i]); }
            pq::SpatialStats st; st.rmsDb = lv[i]; pq::MixRegistry::get().pushStats (q.mixSlot(), st); }
        std::unique_ptr<juce::AudioProcessorEditor> ed (ps[0]->createEditor()); auto* pe = dynamic_cast<PrimeqEditor*> (ed.get()); pe->setSize (1240, 860);
        pe->showMixer (true); auto& vm = pe->getVisualMixer(); vm.refresh();
        CHECK (vm.nodes().size() >= 4, "Visual Mixer: %d nos", (int) vm.nodes().size());
        vm.setTrack (ps[1]->mixSlot(), { -3.f, 35.f, 150.f }); ps[1]->pollMixMail();
        CHECK (std::fabs (ps[1]->getParamReal ("out") + 3.f) < 0.05f && std::fabs (ps[1]->getParamReal ("trk_pan") - 35.f) < 0.2f && std::fabs (ps[1]->getParamReal ("trk_width") - 150.f) < 0.2f, "Visual Mixer: pedido chega a outra instancia (gain %.2f pan %.1f width %.1f)", ps[1]->getParamReal ("out"), ps[1]->getParamReal ("trk_pan"), ps[1]->getParamReal ("trk_width"));
        vm.setTrack (ps[0]->mixSlot(), { 2.f, -20.f, 80.f }); CHECK (std::fabs (ps[0]->getParamReal ("trk_pan") + 20.f) < 0.2f, "Visual Mixer: a propria instancia muda directamente");
        vm.refresh(); vm.saveSnapshot (0); vm.setTrack (ps[1]->mixSlot(), { 0.f, 0.f, 100.f }); ps[1]->pollMixMail(); vm.refresh(); vm.recallSnapshot (0); ps[1]->pollMixMail();
        CHECK (std::fabs (ps[1]->getParamReal ("trk_pan") - 35.f) < 0.2f, "Visual Mixer: snapshot A repoe o estado guardado (pan %.1f)", ps[1]->getParamReal ("trk_pan"));
        for (auto& q : ps) q->pollMixMail(); vm.refresh();
        const int n = vm.runAssistant(); for (auto& q : ps) q->pollMixMail();
        CHECK (n >= 4, "Balance Assistant: propoe %d pistas", n);
        CHECK (std::fabs (ps[0]->getParamReal ("out")) < 0.05f && std::fabs (ps[0]->getParamReal ("trk_pan")) < 0.2f, "Balance Assistant: voz fica a 0 dB e ao centro");
        CHECK (std::fabs (ps[1]->getParamReal ("trk_pan")) < 0.2f && std::fabs (ps[3]->getParamReal ("trk_pan")) > 20.f, "Balance Assistant: kick ao centro, guitarra afastada (pan %.0f)", ps[3]->getParamReal ("trk_pan"));
        { auto im = pe->createComponentSnapshot (pe->getLocalBounds(), true, 1.0f); juce::File f ("visual_mixer.png"); f.deleteFile(); juce::FileOutputStream fo (f); juce::PNGImageFormat().writeImageToStream (im, fo); }
        vm.undoAssistant(); for (auto& q : ps) q->pollMixMail(); CHECK (std::fabs (ps[1]->getParamReal ("trk_pan") - 35.f) < 0.2f && std::fabs (ps[0]->getParamReal ("trk_pan") + 20.f) < 0.2f, "Balance Assistant: DESFAZER repoe os valores");
        pqui::i18n::lang() = 1; pe->changeLanguage (1); { auto im = pe->createComponentSnapshot (pe->getLocalBounds(), true, 1.0f); juce::File f ("visual_mixer_en.png"); f.deleteFile(); juce::FileOutputStream fo (f); juce::PNGImageFormat().writeImageToStream (im, fo); }
        pe->changeLanguage (0); pqui::i18n::lang() = 0; pe->showMixer (false);
        { juce::MemoryBlock mb; ps[1]->getStateInformation (mb); auto q = make (48000.0, 512); q->setStateInformation (mb.getData(), (int) mb.getSize()); CHECK (std::fabs (q->getParamReal ("trk_width") - 150.f) < 0.2f, "Pan/Width guardados no estado do plugin"); }
    }
    // 28) pre-visualizacao dos layouts A/B/C
    {
        auto p0 = make (48000.0, 512); p0->setTrackName ("Vocals", true); std::unique_ptr<juce::AudioProcessorEditor> ed (p0->createEditor()); auto* pe = dynamic_cast<PrimeqEditor*> (ed.get()); pe->setSize (1240, 860);
        const char* nmL[4] = { "classico", "A", "B", "C" };
        { const auto& fp = PrimeqProcessor::factoryPresets(); for (size_t i = 0; i < fp.size(); ++i) if (juce::String (fp[i].name).containsIgnoreCase ("Vocal Presence")) { p0->loadFactoryPreset ((int) i); break; } }
        { juce::Random rnd (7); juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer mb; float lp = 0.f; for (int k = 0; k < 120; ++k) { for (int i = 0; i < 512; ++i) { lp += 0.12f * ((rnd.nextFloat() * 2.f - 1.f) - lp); const float v = 0.5f * lp * 4.f + 0.05f * (rnd.nextFloat() * 2.f - 1.f); b.setSample (0, i, v); b.setSample (1, i, v); } p0->processBlock (b, mb); pe->getGraph().tick(); } }
        for (int L = 0; L < 4; ++L)
        {
            pe->setLayout (L); if (L > 0) { pe->setExpert (true); pe->resized(); }
            auto im = pe->createComponentSnapshot (pe->getLocalBounds(), true, 1.0f); juce::File f (juce::String ("layout_") + nmL[L] + ".png"); f.deleteFile(); juce::FileOutputStream fo (f); juce::PNGImageFormat().writeImageToStream (im, fo);
            CHECK (im.getWidth() == 1240 && pe->getLayoutMode() == L, "Layout %s: snapshot gerado", nmL[L]);
        }
        pe->setLayout (1); pe->setExpert (false); { auto im = pe->createComponentSnapshot (pe->getLocalBounds(), true, 1.0f); juce::File f ("layout_A_simples.png"); f.deleteFile(); juce::FileOutputStream fo (f); juce::PNGImageFormat().writeImageToStream (im, fo); }
        pe->setLayout (0);
    }
    std::printf ("\n%d testes ok, %d falhas\n", passes, fails);
    return fails ? 1 : 0;
}
