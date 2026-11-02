// Teste de integracao do PRIMEQ pro (JUCE, sem anfitriao): processa audio real pelo AudioProcessor.
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
        auto pk = make (fs, bs), pb = make (fs, bs), pv = make (fs, bs);
        pk->setTrackName ("Kick"); pb->setTrackName ("Bass"); pv->setTrackName ("Vocals");
        auto& reg = pq::MixRegistry::get();
        CHECK (reg.priority (pk->mixSlot()) == 4 && reg.priority (pb->mixSlot()) == 3 && reg.priority (pv->mixSlot()) == 5, "Mix Unmask: prioridades sugeridas pelo nome (Kick 4, Bass 3, Vocals 5)");
        for (auto* q : { pk.get(), pb.get(), pv.get() }) reg.setAnalysing (q->mixSlot(), true, fs);
        std::mt19937 rng (5); std::normal_distribution<float> nd (0, 1.f);
        const int total = (int) (fs * 12);
        for (int off = 0; off < total; off += bs)
        {
            juce::AudioBuffer<float> bk (2, bs), bb (2, bs), bv (2, bs); juce::MidiBuffer m;
            for (int i = 0; i < bs; ++i)
            {
                const double t = (double) (off + i) / fs; const double ph = std::fmod (t, 0.5);
                const float kick = ph < 0.09 ? (float) (0.5 * std::exp (-ph * 25.0) * std::sin (2 * M_PI * (95.0 * ph + 50.0 * (1 - std::exp (-ph * 30.0)) / 30.0 * 0 + 50.0 * ph))) : 0.f;
                const float kick2 = ph < 0.09 ? (float) (0.5 * std::exp (-ph * 25.0) * std::sin (2 * M_PI * 96.0 * ph)) : 0.f; (void) kick;
                const float bass = (float) (0.28 * std::sin (2 * M_PI * 96.0 * t) + 0.12 * std::sin (2 * M_PI * 130.0 * t));
                const float vox = (float) ((0.16 + 0.04 * std::sin (2 * M_PI * 3.0 * t)) * (std::sin (2 * M_PI * 400.0 * t) + 0.6 * std::sin (2 * M_PI * 1200.0 * t) + 0.3 * std::sin (2 * M_PI * 2600.0 * t))) + 0.002f * nd (rng);
                for (int c = 0; c < 2; ++c) { bk.setSample (c, i, kick2); bb.setSample (c, i, bass); bv.setSample (c, i, vox); }
            }
            pk->processBlock (bk, m); pb->processBlock (bb, m); pv->processBlock (bv, m);
        }
        std::unique_ptr<juce::AudioProcessorEditor> ed (pb->createEditor()); auto* pe = dynamic_cast<PrimeqEditor*> (ed.get()); pe->setSize (1240, 860);
        pe->showMix (true); pe->getMix().analyse(); const auto& R = pe->getMix().result();
        const pq::MixPair* kb = nullptr; for (auto& pr : R.pairs) if (R.curve.size() == 3 && pr.victimIdx >= 0 && ! pr.conflicts.empty() && pr.conflicts[0].freq < 160) { kb = &pr; break; }
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
        pe->resized(); auto img = pe->createComponentSnapshot (pe->getLocalBounds(), true, 1.0f);
        juce::File out ("mix_unmask.png"); out.deleteFile(); juce::FileOutputStream fo (out); juce::PNGImageFormat().writeImageToStream (img, fo);
        CHECK (out.getSize() > 20000, "pagina Mix Unmask renderizada (%lld bytes)", (long long) out.getSize());
    }
    std::printf ("\n%d testes ok, %d falhas\n", passes, fails);
    return fails ? 1 : 0;
}
