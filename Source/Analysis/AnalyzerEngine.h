// =============================================================================
//  PrimEQ Pro by Piradex — analisador FFT real (pre/post) e captura do Smart Ops
//  A thread de audio so escreve nos aneis (push). Toda a FFT corre na thread da GUI (update()).
//  O analisador visual nunca altera o sinal.
// =============================================================================
#pragma once
#include "SpectrumCalc.h"
#include "StereoRing.h"
#include "../SmartOps/Deresonate.h"
#include "../SmartOps/DeEss.h"
#include "../SmartOps/Unmask.h"
#include <algorithm>
#include <memory>

namespace pq
{
enum class SpecView { Stereo = 0, Left, Right, Mid, Side };

class AnalyzerEngine
{
public:
    static constexpr int kHist = 32768;

    AnalyzerEngine() { for (auto* h : { &pl, &pr, &ql, &qr, &sl, &sr2 }) h->assign ((size_t) kHist, 0.f); setFftSize (4096); }

    void prepare (double sr) { fs = sr; for (auto* h : { &pl, &pr, &ql, &qr, &sl, &sr2 }) std::fill (h->begin(), h->end(), 0.f); hw = 0; hws = 0; resetDisplay(); }

    // ---- thread de audio
    void pushPre  (const float* a, const float* b, int n) noexcept { pre.push (a, b, n); }
    void pushPost (const float* a, const float* b, int n) noexcept { post.push (a, b, n); }
    void pushSc   (const float* a, const float* b, int n) noexcept { sc.push (a, b, n); }

    // ---- thread da GUI
    void setFftSize (int n) { if (n == fftN) return; fftN = n; calc.setSize (n); bins = calc.numBins(); resetDisplay(); }
    int  fftSize() const { return fftN; }
    int  numBins() const { return bins; }
    double sampleRate() const { return fs; }
    const std::vector<float>& preDb()  const { return dispPre; }
    const std::vector<float>& postDb() const { return dispPost; }
    const std::vector<float>& peakDb() const { return peak; }
    void resetPeak() { std::fill (peak.begin(), peak.end(), -200.f); }

    void update (SpecView view, bool freeze, bool peakHold)
    {
        static thread_local std::vector<float> ta, tb; ta.resize (4096); tb.resize (4096);
        int n; bool any = false;
        while ((n = pre.pull (ta.data(), tb.data(), 4096)) > 0)  { append (pl, pr, ta.data(), tb.data(), n); any = true; learnFeed (n); }
        while ((n = post.pull (ta.data(), tb.data(), 4096)) > 0) { appendPost (ta.data(), tb.data(), n); }
        while ((n = sc.pull (ta.data(), tb.data(), 4096)) > 0) { for (int i = 0; i < n; ++i) { sl[(size_t) hws] = ta[(size_t) i]; sr2[(size_t) hws] = tb[(size_t) i]; hws = (hws + 1) % kHist; } learnFeedSc (n); }
        if (freeze) return;
        (void) any;
        compute (pl, pr, view, dispPre);
        compute (ql, qr, view, dispPost);
        if (peakHold) for (int k = 0; k < bins; ++k) peak[(size_t) k] = std::max (peak[(size_t) k], dispPost[(size_t) k]);
    }

    // ---- Smart Ops: captura do espectro medio da ENTRADA
    void learnStart() { deres.begin (4097, fs); deess.begin (fs); unmask.begin (fs); learning = true; sinceLast = 0; sinceLastSc = 0; }
    void learnStop() { learning = false; }
    bool isLearning() const { return learning; }
    Deresonate& deresonate() { return deres; }
    DeEss& deEss() { return deess; }
    Unmask& unmaskEngine() { return unmask; }

private:
    void resetDisplay()
    {
        dispPre.assign ((size_t) bins, -120.f); dispPost.assign ((size_t) bins, -120.f); peak.assign ((size_t) bins, -200.f);
        tmpDb.assign ((size_t) bins, 0.f); tmpDb2.assign ((size_t) bins, 0.f); win.assign ((size_t) fftN, 0.f); win2.assign ((size_t) fftN, 0.f);
    }
    void append (std::vector<float>& a, std::vector<float>& b, const float* x, const float* y, int n)
    {
        for (int i = 0; i < n; ++i) { a[(size_t) hw] = x[i]; b[(size_t) hw] = y[i]; hw = (hw + 1) % kHist; }
    }
    void appendPost (const float* x, const float* y, int n)
    {
        for (int i = 0; i < n; ++i) { ql[(size_t) hwq] = x[i]; qr[(size_t) hwq] = y[i]; hwq = (hwq + 1) % kHist; }
    }
    void latest (const std::vector<float>& h, int head, float* dst, int count, bool side = false, const std::vector<float>* other = nullptr, int mode = 0) const
    {
        (void) side; (void) other; (void) mode;
        for (int i = 0; i < count; ++i) dst[i] = h[(size_t) ((head - count + i + 2 * kHist) % kHist)];
    }
    void compute (const std::vector<float>& a, const std::vector<float>& b, SpecView v, std::vector<float>& out)
    {
        const int head = (&a == &pl) ? hw : hwq;
        latest (a, head, win.data(), fftN); latest (b, head, win2.data(), fftN);
        switch (v)
        {
            case SpecView::Left:  calc.compute (win.data(), tmpDb.data()); break;
            case SpecView::Right: calc.compute (win2.data(), tmpDb.data()); break;
            case SpecView::Mid:  for (int i = 0; i < fftN; ++i) win[(size_t) i] = 0.5f * (win[(size_t) i] + win2[(size_t) i]); calc.compute (win.data(), tmpDb.data()); break;
            case SpecView::Side: for (int i = 0; i < fftN; ++i) win[(size_t) i] = 0.5f * (win[(size_t) i] - win2[(size_t) i]); calc.compute (win.data(), tmpDb.data()); break;
            default:
                calc.compute (win.data(), tmpDb.data()); calc.compute (win2.data(), tmpDb2.data());
                for (int k = 0; k < bins; ++k) { const float p = 0.5f * (std::pow (10.f, tmpDb[(size_t) k] / 10.f) + std::pow (10.f, tmpDb2[(size_t) k] / 10.f)); tmpDb[(size_t) k] = 10.f * std::log10 (std::max (p, 1e-18f)); }
                break;
        }
        for (int k = 0; k < bins; ++k) { const float t = tmpDb[(size_t) k]; float& d = out[(size_t) k]; d += (t - d) * (t > d ? 0.65f : 0.22f); }
    }
    void learnFeed (int n)
    {
        if (! learning) return;
        sinceLast += n;
        while (sinceLast >= 4096)
        {
            sinceLast -= 4096;
            static thread_local std::vector<float> w1, w2, db, pw; w1.resize (8192); w2.resize (8192); db.resize (4097); pw.resize (4097);
            latest (pl, hw, w1.data(), 8192); latest (pr, hw, w2.data(), 8192);
            for (int i = 0; i < 8192; ++i) w1[(size_t) i] = 0.5f * (w1[(size_t) i] + w2[(size_t) i]);   // mono (L+R)/2
            static thread_local SpectrumCalc c8 (8192);
            c8.compute (w1.data(), db.data(), pw.data());
            deres.addPower (pw.data(), 4096.0 / fs);
            deess.feed (w1.data() + 4096, 4096); unmask.feedMain (w1.data() + 4096, 4096);   // ultimos 4096 = fluxo contiguo
        }
    }
    void learnFeedSc (int n)
    {
        if (! learning) return;
        sinceLastSc += n;
        while (sinceLastSc >= 4096)
        {
            sinceLastSc -= 4096;
            static thread_local std::vector<float> m4; m4.resize (4096);
            for (int i = 0; i < 4096; ++i) { const size_t k = (size_t) ((hws - 4096 + i + 2 * kHist) % kHist); m4[(size_t) i] = 0.5f * (sl[k] + sr2[k]); }
            unmask.feedSide (m4.data(), 4096);
        }
    }

    StereoRing pre, post, sc;
    std::vector<float> pl, pr, ql, qr, sl, sr2; int hw = 0, hwq = 0, hws = 0, sinceLastSc = 0;
    double fs = 48000; int fftN = 4096, bins = 2049; SpectrumCalc calc { 4096 };
    std::vector<float> dispPre, dispPost, peak, tmpDb, tmpDb2, win, win2;
    Deresonate deres; DeEss deess; Unmask unmask; bool learning = false; int sinceLast = 0;
};
} // namespace pq
