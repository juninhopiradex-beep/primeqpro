// =============================================================================
//  PRIMEQ PRO by Piradex — grafico principal: espectro pre/post (FFT real), curvas calculadas dos COEFICIENTES
//  atuais (inclui ganho dinamico), nos interativos. Nada aqui e desenhado por splines desligadas do DSP.
// =============================================================================
#pragma once
#include "LookAndFeel.h"
#include "../Plugin/PluginProcessor.h"
#include <functional>

#include "Overlay.h"
class EqGraph : public juce::Component
{
public:
    explicit EqGraph (PrimeqProcessor& p) : proc (p)
    {
        setWantsKeyboardFocus (true); setMouseClickGrabsKeyboardFocus (true);
        pq::BandParams a[pq::kNumBands]; proc.readBands (a); for (int i = 0; i < pq::kNumBands; ++i) bands[i] = a[i];
    }

    std::function<void (int)> onSelect;
    int selected = 0;
    std::vector<pq::DeresProposal> preview; bool showPreview = false;
    std::vector<OverlayZone> zones; bool showZones = true;       // zonas de masking/espaco vindas do Assistente
    void setZones (const std::vector<OverlayZone>& z) { zones = z; repaint(); }

    // chamado pelo timer do editor (thread de mensagens)
    void tick()
    {
        proc.readBands (bands);
        for (int i = 0; i < pq::kNumBands; ++i) dyn[i] = proc.dynTel[(size_t) i].load (std::memory_order_relaxed);
        const int fi = (int) proc.getParamReal ("fftsize"); proc.analyzer.setFftSize (fi == 0 ? 2048 : (fi == 2 ? 8192 : 4096));
        proc.analyzer.update ((pq::SpecView) (int) proc.getParamReal ("specview"), proc.getParamReal ("freeze") > 0.5f, proc.getParamReal ("peakhold") > 0.5f);
        const int r = (int) proc.getParamReal ("graphrange"); rangeDb = r == 0 ? 6.0 : (r == 1 ? 12.0 : (r == 2 ? 18.0 : 30.0));
        tiltDbOct = (int) proc.getParamReal ("tilt") == 0 ? 0.0 : ((int) proc.getParamReal ("tilt") == 1 ? 3.0 : 4.5);
        repaint();
    }

    // ---------------------------------------------------------------- geometria
    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().withTrimmedLeft (38.f).withTrimmedBottom (20.f).withTrimmedTop (6.f).withTrimmedRight (8.f); }
    float xOf (double f) const { auto p = plot(); return p.getX() + (float) (std::log (f / 10.0) / std::log (3000.0)) * p.getWidth(); }
    double fOf (float x) const { auto p = plot(); return 10.0 * std::pow (3000.0, (double) (x - p.getX()) / p.getWidth()); }
    float yOf (double db) const { auto p = plot(); return p.getCentreY() - (float) (db / rangeDb) * (p.getHeight() * 0.5f); }
    double dbOf (float y) const { auto p = plot(); return (double) (p.getCentreY() - y) / (p.getHeight() * 0.5) * rangeDb; }
    float ySpec (double db) const { auto p = plot(); return p.getBottom() - (float) ((db + 100.0) / 100.0) * p.getHeight(); }

    double effGain (int b) const { const auto& p = bands[b]; return pq::clampd (p.gain + ((p.dyn && pq::typeHasGain (p.type)) ? dyn[b] : 0.0), -36.0, 36.0); }
    pq::Cascade cascadeOf (int b) const { const auto& p = bands[b]; return pq::designBand (p.type, p.slope, proc.sampleRateNow(), p.freq, effGain (b), p.q); }
    juce::Point<float> nodePos (int b) const { const auto& p = bands[b]; return { xOf (p.freq), yOf (pq::typeHasGain (p.type) ? p.gain : 0.0) }; }
    int hitNode (juce::Point<float> pt) const
    {
        int best = -1; float bd = 13.f;
        for (int b = pq::kNumBands - 1; b >= 0; --b) if (bands[b].on) { const float d = nodePos (b).getDistanceFrom (pt); if (d < bd) { bd = d; best = b; } }
        return best;
    }

    // ---------------------------------------------------------------- pintura
    void paint (juce::Graphics& g) override
    {
        using namespace pqui;
        auto area = getLocalBounds().toFloat(); auto p = plot(); const double fs = proc.sampleRateNow();
        g.setColour (kPanel); g.fillRoundedRectangle (area, 8.f);
        g.saveState(); g.reduceClipRegion (p.toNearestInt());
        { juce::ColourGradient bg (kPanel2.brighter (0.03f), p.getX(), p.getY(), kPanel.darker (0.2f), p.getX(), p.getBottom(), false); g.setGradientFill (bg); g.fillRect (p); }

        // grelha
        g.setFont (fontOf (10.f));
        for (double f : { 20., 30., 40., 50., 60., 70., 80., 90., 100., 200., 300., 400., 500., 600., 700., 800., 900., 1000., 2000., 3000., 4000., 5000., 6000., 7000., 8000., 9000., 10000., 20000. })
        { const bool major = (f == 100. || f == 1000. || f == 10000.); g.setColour (major ? kGrid.brighter (0.35f) : kGrid); g.drawVerticalLine ((int) xOf (f), p.getY(), p.getBottom()); }
        const double step = rangeDb <= 6 ? 3.0 : (rangeDb <= 12 ? 6.0 : (rangeDb <= 18 ? 6.0 : 10.0));
        for (double d = -rangeDb; d <= rangeDb + 1e-6; d += step) { g.setColour (std::fabs (d) < 1e-6 ? kGrid.brighter (0.7f) : kGrid); g.drawHorizontalLine ((int) yOf (d), p.getX(), p.getRight()); }

        // espectro (independente da escala do EQ)
        const auto& pre = proc.analyzer.preDb(); const auto& post = proc.analyzer.postDb(); const auto& pk = proc.analyzer.peakDb();
        auto specPath = [&] (const std::vector<float>& v, bool fill) {
            juce::Path sp; bool started = false; const int N = proc.analyzer.fftSize(); const double binHz = proc.analyzer.sampleRate() / N; const int nb = (int) v.size();
            if (nb < 8) return sp;
            for (float x = p.getX(); x <= p.getRight(); x += 2.f)
            {
                const double f0 = fOf (x), f1 = fOf (x + 2.f);
                if (f1 > fs * 0.5) break;
                const double b0 = f0 / binHz, b1 = f1 / binHz; float db;
                if (b1 - b0 > 1.0) { db = -200.f; for (int k = std::max (1, (int) b0); k <= std::min (nb - 1, (int) b1); ++k) db = std::max (db, v[(size_t) k]); }
                else { const double bc = std::max (1.0, 0.5 * (b0 + b1)); const int k = std::min (nb - 2, (int) bc); const float fr = (float) (bc - k); db = v[(size_t) k] * (1.f - fr) + v[(size_t) k + 1] * fr; }
                db += (float) (tiltDbOct * std::log2 (0.5 * (f0 + f1) / 1000.0));
                const float y = ySpec (db);
                if (! started) { sp.startNewSubPath (x, fill ? p.getBottom() : y); if (fill) sp.lineTo (x, y); started = true; } else sp.lineTo (x, y);
            }
            if (fill && started) { sp.lineTo (sp.getCurrentPosition().x, p.getBottom()); sp.closeSubPath(); }
            return sp;
        };
        { auto s = specPath (pre, true); g.setColour (kDim.withAlpha (0.16f)); g.fillPath (s); }
        { auto s = specPath (post, true); g.setColour (kTeal.withAlpha (0.20f)); g.fillPath (s); auto l = specPath (post, false); g.setColour (kTeal.withAlpha (0.65f)); g.strokePath (l, juce::PathStrokeType (1.2f)); }
        if (proc.getParamReal ("peakhold") > 0.5f) { auto l = specPath (pk, false); g.setColour (kWarn.withAlpha (0.55f)); g.strokePath (l, juce::PathStrokeType (1.f)); }

        // zonas do Assistente (masking = vermelho, espaco = violeta), por baixo das curvas
        if (showZones && ! zones.empty())
        {
            g.setFont (fontOf (9.5f));
            for (const auto& z : zones)
            {
                const float x0 = juce::jlimit (p.getX(), p.getRight(), xOf (z.f0)), x1 = juce::jlimit (p.getX(), p.getRight(), xOf (z.f1));
                if (x1 - x0 < 2.f) continue;
                const auto c = z.kind == OverlayZone::Masking ? kRed : juce::Colour (0xffa78bfa); const float a = 0.10f + 0.24f * juce::jlimit (0.f, 1.f, z.severity);
                g.setGradientFill (juce::ColourGradient (c.withAlpha (a), 0.f, p.getY(), c.withAlpha (a * 0.35f), 0.f, p.getBottom(), false)); g.fillRect (x0, p.getY(), x1 - x0, p.getHeight());
                g.setColour (c.withAlpha (0.55f)); g.drawVerticalLine ((int) x0, p.getY(), p.getBottom()); g.drawVerticalLine ((int) x1, p.getY(), p.getBottom());
                if (z.label.isNotEmpty()) { g.setColour (c.brighter (0.4f).withAlpha (0.95f)); g.drawText (z.label, (int) x0 + 2, (int) p.getY() + 2, std::max (30, (int) (x1 - x0)), 12, juce::Justification::topLeft, false); }
            }
        }

        // curvas individuais (a partir dos coeficientes) e total
        const int npts = std::max (16, (int) (p.getWidth() / 2.f));
        std::vector<double> total ((size_t) npts + 1, 0.0);
        for (int b = 0; b < pq::kNumBands; ++b)
        {
            if (! bands[b].on) continue;
            const auto c = cascadeOf (b); juce::Path bp; const auto col = bandColour (b); bool st = false;
            for (int i = 0; i <= npts; ++i)
            {
                const float x = p.getX() + p.getWidth() * i / npts; const double f = fOf (x);
                double db = 0; if (f < fs * 0.49) db = pq::magDb (c, 2.0 * M_PI * f / fs);
                total[(size_t) i] += db;
                const float y = yOf (db); if (! st) { bp.startNewSubPath (x, y); st = true; } else bp.lineTo (x, y);
            }
            const bool sel = b == selected;
            if (sel) { juce::Path fillP (bp); fillP.lineTo (p.getRight(), yOf (0)); fillP.lineTo (p.getX(), yOf (0)); fillP.closeSubPath(); g.setColour (col.withAlpha (0.16f)); g.fillPath (fillP); }
            g.setColour (col.withAlpha (sel ? 0.95f : 0.38f)); g.strokePath (bp, juce::PathStrokeType (sel ? 1.8f : 1.1f));
        }
        { juce::Path tp; for (int i = 0; i <= npts; ++i) { const float x = p.getX() + p.getWidth() * i / npts, y = yOf (total[(size_t) i]); if (i == 0) tp.startNewSubPath (x, y); else tp.lineTo (x, y); }
          g.setColour (juce::Colours::white.withAlpha (0.92f)); g.strokePath (tp, juce::PathStrokeType (2.f)); }

        // pre-visualizacao Smart Ops (tracejada): soma das propostas, calculada com os mesmos filtros
        if (showPreview && ! preview.empty())
        {
            juce::Path pv, dashed; for (int i = 0; i <= npts; ++i)
            {
                const float x = p.getX() + p.getWidth() * i / npts; const double f = fOf (x); double db = 0;
                if (f < fs * 0.49) for (const auto& pr : preview) db += pq::magDb (pq::designBand (pq::BandType::Bell, 1, fs, pr.freq, pr.gainDb, pr.q), 2.0 * M_PI * f / fs);
                const float y = yOf (db); if (i == 0) pv.startNewSubPath (x, y); else pv.lineTo (x, y);
            }
            const float dl[2] = { 6.f, 4.f }; juce::PathStrokeType (2.f).createDashedStroke (dashed, pv, dl, 2);
            g.setColour (kWarn); g.fillPath (dashed);
        }

        // nos
        for (int b = 0; b < pq::kNumBands; ++b)
        {
            if (! bands[b].on) continue;
            const auto pos = nodePos (b); const auto col = bandColour (b); const bool sel = b == selected, hov = b == hover;
            const float r = sel ? 10.f : (hov ? 10.f : 8.5f);
            if (bands[b].dyn && pq::typeHasGain (bands[b].type))
            {
                const auto gp = juce::Point<float> (pos.x, yOf (effGain (b)));
                g.setColour (col.withAlpha (0.5f)); g.drawLine (pos.x, pos.y, gp.x, gp.y, 1.f); g.drawEllipse (gp.x - 4.f, gp.y - 4.f, 8.f, 8.f, 1.4f);
            }
            if (sel) { g.setColour (col.withAlpha (0.35f)); g.fillEllipse (pos.x - r - 4, pos.y - r - 4, 2 * (r + 4), 2 * (r + 4)); }
            g.setColour (col); g.fillEllipse (pos.x - r, pos.y - r, 2 * r, 2 * r);
            g.setColour (kBg.withAlpha (0.9f)); g.drawEllipse (pos.x - r, pos.y - r, 2 * r, 2 * r, 1.4f);
            g.setColour (juce::Colour (0xff0c0e10)); g.setFont (fontOf (11.f, true)); g.drawText (juce::String (b + 1), (int) (pos.x - r), (int) (pos.y - r), (int) (2 * r), (int) (2 * r), juce::Justification::centred);
        }
        g.restoreState();

        // eixos
        g.setFont (fontOf (10.f)); g.setColour (kDim);
        for (double f : { 20., 50., 100., 200., 500., 1000., 2000., 5000., 10000., 20000. })
            g.drawText (f >= 1000 ? juce::String (f / 1000.0, f >= 10000 ? 0 : (f == 1000 || f == 2000 || f == 5000 ? 0 : 1)) + "k" : juce::String ((int) f), (int) xOf (f) - 18, (int) p.getBottom() + 3, 36, 14, juce::Justification::centred);
        for (double d = -rangeDb; d <= rangeDb + 1e-6; d += step) g.drawText ((d > 0 ? "+" : "") + juce::String ((int) d), 0, (int) yOf (d) - 7, 34, 14, juce::Justification::centredRight);

        // dica sob o cursor
        if (cursorIn)
        {
            const double f = fOf (cursor.x); double cents = 0; const auto note = pq::noteName (f, &cents);
            const auto& post2 = proc.analyzer.postDb(); const int N = proc.analyzer.fftSize(); const double binHz = proc.analyzer.sampleRate() / N;
            const int k = std::clamp ((int) std::lround (f / binHz), 1, (int) post2.size() - 1);
            juce::String s = (f >= 1000 ? juce::String (f / 1000.0, 2) + " kHz" : juce::String (f, 1) + " Hz") + "   " + juce::String (note) + (std::fabs (cents) < 0.5 ? "" : (cents > 0 ? " +" : " ") + juce::String (cents, 0) + "c")
                            + "   EQ " + juce::String (dbOf (cursor.y), 1) + " dB   FFT " + juce::String (post2[(size_t) k] + (float) (tiltDbOct * std::log2 (f / 1000.0)), 1) + " dB";
            g.setColour (juce::Colour (0xcc0c0e10)); g.fillRoundedRectangle (p.getX() + 8.f, p.getY() + 8.f, 330.f, 20.f, 5.f);
            g.setColour (kText); g.setFont (fontOf (11.f)); g.drawText (s, (int) p.getX() + 14, (int) p.getY() + 8, 322, 20, juce::Justification::centredLeft);
        }
    }

    // ---------------------------------------------------------------- rato
    void mouseMove (const juce::MouseEvent& e) override { cursor = e.position; cursorIn = true; hover = hitNode (e.position); setMouseCursor (hover >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::CrosshairCursor); }
    void mouseExit (const juce::MouseEvent&) override { cursorIn = false; hover = -1; }
    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus(); cursor = e.position;
        const int n = hitNode (e.position);
        if (n >= 0) select (n);
        if (e.mods.isPopupMenu()) { if (n >= 0) showMenu (n); return; }
        if (n < 0) { dragBand = -1; return; }
        dragBand = n; dragStart = e.position; axis = 0; proc.undo.beginNewTransaction ("Band edit");
        for (const char* k : { "freq", "gain" }) if (auto* pp = proc.apvts.getParameter (juce::String (pq::bid (n, k)))) pp->beginChangeGesture();
        dragFreq0 = bands[n].freq; dragGain0 = bands[n].gain;
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        cursor = e.position; if (dragBand < 0) return;
        const int b = dragBand; const auto d = e.position - dragStart;
        if (e.mods.isShiftDown() && axis == 0 && (std::fabs (d.x) > 4 || std::fabs (d.y) > 4)) axis = std::fabs (d.x) >= std::fabs (d.y) ? 1 : 2;
        if (! e.mods.isShiftDown()) axis = 0;
        float x = e.position.x, y = e.position.y;
        if (axis == 1) y = yOf (pq::typeHasGain (bands[b].type) ? dragGain0 : 0.0); else if (axis == 2) x = xOf (dragFreq0);
        setNorm (juce::String (pq::bid (b, "freq")), (float) pq::clampd (fOf (x), pq::kMinFreq, std::min (pq::kMaxFreq, proc.sampleRateNow() * 0.49)));
        if (pq::typeHasGain (bands[b].type)) setNorm (juce::String (pq::bid (b, "gain")), (float) pq::clampd (dbOf (y), -pq::kMaxGainDb, pq::kMaxGainDb));
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragBand >= 0) for (const char* k : { "freq", "gain" }) if (auto* pp = proc.apvts.getParameter (juce::String (pq::bid (dragBand, k)))) pp->endChangeGesture();
        dragBand = -1;
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int n = hitNode (e.position); proc.undo.beginNewTransaction ("Band add/remove");
        if (n >= 0) { setNorm (juce::String (pq::bid (n, "on")), 0.f); return; }
        if (! plot().contains (e.position)) return;
        for (int b = 0; b < pq::kNumBands; ++b) if (! bands[b].on)
        {
            pq::BandParams p; p.on = true; p.type = pq::BandType::Bell; p.freq = pq::clampd (fOf (e.position.x), pq::kMinFreq, std::min (pq::kMaxFreq, proc.sampleRateNow() * 0.49));
            p.gain = pq::clampd (dbOf (e.position.y), -pq::kMaxGainDb, pq::kMaxGainDb); p.q = 1.0;
            proc.setBand (b, p); select (b); return;
        }
    }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        const int b = hover >= 0 ? hover : selected; if (b < 0 || ! bands[b].on) return;
        proc.undo.beginNewTransaction ("Q");
        if (pq::typeIsCut (bands[b].type) && pq::kSlopeOrder[bands[b].slope] != 2)
        {
            wheelAcc += w.deltaY; if (std::fabs (wheelAcc) < 0.15f) return;
            const int ns = std::clamp (bands[b].slope + (wheelAcc > 0 ? 1 : -1), 0, pq::kNumSlopes - 1); wheelAcc = 0;
            setNorm (juce::String (pq::bid (b, "slope")), (float) ns); return;
        }
        setNorm (juce::String (pq::bid (b, "q")), (float) pq::clampd (bands[b].q * std::exp (w.deltaY * 1.6), pq::kMinQ, pq::kMaxQ));
    }
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) { if (selected >= 0) { proc.undo.beginNewTransaction ("Delete band"); setNorm (juce::String (pq::bid (selected, "on")), 0.f); return true; } }
        if (k == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0)) { proc.undo.undo(); return true; }
        if (k == juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)) { proc.undo.redo(); return true; }
        return false;
    }

    void select (int b) { selected = b; if (onSelect) onSelect (b); repaint(); }
    static inline pq::BandParams clipboard; static inline bool hasClip = false;

private:
    void setNorm (const juce::String& id, float real) { if (auto* pp = proc.apvts.getParameter (id)) pp->setValueNotifyingHost (pp->convertTo0to1 (real)); }
    void showMenu (int b)
    {
        juce::PopupMenu m, ty, pl, sl;
        const char* tn[8] = { "Bell", "Low Shelf", "High Shelf", "High Pass", "Low Pass", "Notch", "Band Pass", "All Pass" };
        for (int i = 0; i < 8; ++i) ty.addItem (100 + i, tn[i], true, (int) bands[b].type == i);
        const char* pn[5] = { "Stereo", "Left", "Right", "Mid", "Side" };
        for (int i = 0; i < 5; ++i) pl.addItem (200 + i, pn[i], true, (int) bands[b].place == i);
        for (int i = 0; i < pq::kNumSlopes; ++i) sl.addItem (300 + i, pq::kSlopeDb[i] ? juce::String (pq::kSlopeDb[i]) + " dB/oct" : juce::String ("Brickwall"), pq::typeIsCut (bands[b].type), bands[b].slope == i);
        m.addSubMenu ("Tipo", ty); m.addSubMenu ("Canal", pl); m.addSubMenu ("Declive", sl); m.addSeparator();
        m.addItem (400, "Dinamica", pq::typeHasGain (bands[b].type), bands[b].dyn);
        m.addItem (401, "Ouvir (Listen)", true, proc.listenBand.load() == b);
        m.addItem (402, "Copiar banda"); m.addItem (403, "Colar banda", hasClip); m.addSeparator(); m.addItem (404, "Apagar banda");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (juce::Rectangle<int> ((int) cursor.x, (int) cursor.y, 1, 1))), [this, b] (int r)
        {
            if (r == 0) return; proc.undo.beginNewTransaction ("Band menu");
            if (r >= 100 && r < 108) setNorm (juce::String (pq::bid (b, "type")), (float) (r - 100));
            else if (r >= 200 && r < 205) setNorm (juce::String (pq::bid (b, "place")), (float) (r - 200));
            else if (r >= 300 && r < 309) setNorm (juce::String (pq::bid (b, "slope")), (float) (r - 300));
            else if (r == 400) setNorm (juce::String (pq::bid (b, "dyn")), bands[b].dyn ? 0.f : 1.f);
            else if (r == 401) proc.listenBand.store (proc.listenBand.load() == b ? -1 : b);
            else if (r == 402) { clipboard = bands[b]; hasClip = true; }
            else if (r == 403 && hasClip) { auto c = clipboard; c.on = true; proc.setBand (b, c); }
            else if (r == 404) setNorm (juce::String (pq::bid (b, "on")), 0.f);
        });
    }

    PrimeqProcessor& proc;
    pq::BandParams bands[pq::kNumBands]; double dyn[pq::kNumBands] {};
    double rangeDb = 12.0, tiltDbOct = 3.0, dragFreq0 = 1000, dragGain0 = 0; float wheelAcc = 0.f;
    int hover = -1, dragBand = -1, axis = 0; juce::Point<float> dragStart, cursor; bool cursorIn = false;
};
