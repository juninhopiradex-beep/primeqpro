// =============================================================================
//  PRIMEQ pro by Piradex — pagina "MIX UNMASK": pistas da sessao, espectro comparado, conflitos e comparacao EQ / Dinamica / Sidechain
// =============================================================================
#pragma once
#include "LookAndFeel.h"
#include "../Plugin/PluginProcessor.h"

using pqui::T8;

class MixPanel : public juce::Component
{
public:
    explicit MixPanel (PrimeqProcessor& p) : proc (p)
    {
        using namespace pqui;
        for (auto* b : { &analyseB, &clearB, &applyAllB }) addAndMakeVisible (b);
        for (auto& b : applyB) addAndMakeVisible (b);
        analyseB.setButtonText (T8 ("ANALISAR MIX")); clearB.setButtonText ("LIMPAR DADOS"); applyAllB.setButtonText (T8 ("APLICAR RECOMENDADO (todas as bandas)"));
        analyseB.onClick = [this] { analyse(); }; clearB.onClick = [this] { for (auto& t : pq::MixRegistry::get().list (&proc)) pq::MixRegistry::get().clearFrames (t.slot); res = {}; tracks.clear(); slotOf.clear(); status = T8 ("Dados limpos."); layoutButtons(); repaint(); };
        applyAllB.onClick = [this] { applyRecommended(); };
        for (int i = 0; i < 9; ++i)
        {
            const int ci = i / 3, m = i % 3; applyB[(size_t) i].setButtonText (m == 0 ? "Aplicar EQ" : (m == 1 ? T8 ("Aplicar Dinâmica") : "Aplicar Sidechain"));
            applyB[(size_t) i].onClick = [this, ci, m] { applyConflict (ci, m); };
        }
        status = T8 ("Ligue \"ANALISAR\" em cada pista, toque o material e carregue em ANALISAR MIX.");
        layoutButtons();
    }

    // dados vivos
    void tick() { if (++cnt % 15 == 0) { rows = pq::MixRegistry::get().list (&proc); repaint (leftArea()); } }
    void analyse()
    {
        auto& reg = pq::MixRegistry::get(); rows = reg.list (&proc); tracks.clear(); slotOf.clear();
        for (const auto& t : rows) if (t.analysing)
        {
            pq::MixTrack mt; mt.name = t.name; mt.priority = t.priority; reg.copyFrames (t.slot, pq::kMixHist - 2, mt.frames);
            if (! mt.frames.empty()) { tracks.push_back (std::move (mt)); slotOf.push_back (t.slot); }
        }
        res = pq::analyseMix (tracks); selPair = 0;
        if (! res.message.empty()) status = juce::String (juce::CharPointer_UTF8 (res.message.c_str()));
        else if (res.pairs.empty()) status = T8 ("Sem conflitos relevantes entre as pistas analisadas. Boa separação.");
        else status = juce::String ((int) res.pairs.size()) + T8 (" par(es) de pistas com mascaramento. Escolha o par e compare os métodos.");
        layoutButtons(); repaint();
    }
    void setPair (int i) { selPair = juce::jlimit (0, std::max (0, (int) res.pairs.size() - 1), i); layoutButtons(); repaint(); }
    const pq::MixResult& result() const { return res; }

    void resized() override { layoutButtons(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto p = e.getPosition();
        for (size_t i = 0; i < rowRects().size(); ++i)
        {
            if (i >= rows.size()) break; const auto r = rowRects()[i]; if (! r.contains (p)) continue;
            auto& reg = pq::MixRegistry::get(); const auto& t = rows[i];
            if (chipPri (r).contains (p)) { reg.setPriority (t.slot, e.mods.isRightButtonDown() ? (t.priority <= 1 ? 5 : t.priority - 1) : (t.priority >= 5 ? 1 : t.priority + 1)); }
            else if (chipOn (r).contains (p)) reg.setAnalysing (t.slot, ! t.analysing, t.sampleRate);
            else if (r.getX() + 8 < p.x && p.x < chipPri (r).getX()) rename (t.slot, t.name);
            rows = reg.list (&proc); repaint(); return;
        }
        // chips de pares
        for (size_t i = 0; i < pairChipRects().size(); ++i) if (pairChipRects()[i].contains (p)) { setPair ((int) i); return; }
    }

    void paint (juce::Graphics& g) override
    {
        using namespace pqui;
        g.setColour (kPanel); g.fillRoundedRectangle (leftArea().toFloat(), 8.f); g.fillRoundedRectangle (rightArea().toFloat(), 8.f);
        paintTracks (g); paintPlot (g); paintPairs (g);
    }

private:
    PrimeqProcessor& proc; juce::TextButton analyseB, clearB, applyAllB; std::array<juce::TextButton, 9> applyB;
    std::vector<pq::MixTrackInfo> rows; std::vector<pq::MixTrack> tracks; std::vector<int> slotOf; pq::MixResult res; int selPair = 0, cnt = 0; juce::String status;

    // ---- geometria
    juce::Rectangle<int> leftArea() const { return { 0, 0, 330, getHeight() }; }
    juce::Rectangle<int> rightArea() const { return { 338, 0, getWidth() - 338, getHeight() }; }
    juce::Rectangle<int> plotRect() const { auto r = rightArea().reduced (12, 10); return r.removeFromTop (236); }
    juce::Rectangle<int> chipsRow() const { auto r = rightArea().reduced (12, 10); r.removeFromTop (244); return r.removeFromTop (28); }
    juce::Rectangle<int> cardRect (int i) const { auto r = rightArea().reduced (12, 10); r.removeFromTop (244 + 34 + i * 124); return r.removeFromTop (118); }
    juce::Rectangle<int> guideRect() const { auto r = rightArea().reduced (12, 10); r.removeFromTop (244 + 34 + 3 * 124); return r; }
    std::vector<juce::Rectangle<int>> rowRects() const { std::vector<juce::Rectangle<int>> v; for (size_t i = 0; i < 9; ++i) v.push_back ({ 10, 46 + (int) i * 52, 310, 48 }); return v; }
    static juce::Rectangle<int> chipPri (juce::Rectangle<int> r) { return { r.getRight() - 128, r.getY() + 12, 34, 24 }; }
    static juce::Rectangle<int> chipOn (juce::Rectangle<int> r) { return { r.getRight() - 88, r.getY() + 12, 82, 24 }; }
    std::vector<juce::Rectangle<int>> pairChipRects() const
    {
        std::vector<juce::Rectangle<int>> v; auto r = chipsRow(); for (size_t i = 0; i < res.pairs.size() && i < 6; ++i) { v.push_back (r.removeFromLeft (chipW())); r.removeFromLeft (6); } return v;
    }
    int chipW() const { return 150; }
    juce::String nameOf (int idx) const { return idx >= 0 && idx < (int) tracks.size() ? juce::String (tracks[(size_t) idx].name) : juce::String(); }
    void layoutButtons()
    {
        auto l = leftArea().reduced (10, 0); analyseB.setBounds (l.getX(), getHeight() - 82, l.getWidth(), 32); clearB.setBounds (l.getX(), getHeight() - 44, l.getWidth(), 28);
        const bool has = selPair < (int) res.pairs.size() && ! res.pairs.empty();
        for (int c = 0; c < 3; ++c)
        {
            const int nC = has ? (int) res.pairs[(size_t) selPair].conflicts.size() : 0;
            auto cr = cardRect (c).reduced (10, 0); auto row = cr.removeFromBottom (30);
            for (int m = 0; m < 3; ++m) { auto& b = applyB[(size_t) (c * 3 + m)]; b.setBounds (row.removeFromLeft (150)); row.removeFromLeft (8); b.setVisible (c < nC); }
        }
        auto g = guideRect(); applyAllB.setBounds (g.removeFromBottom (32).removeFromRight (360)); applyAllB.setVisible (has && ! res.pairs[(size_t) selPair].conflicts.empty());
    }
    static juce::Colour trackColour (int i) { static const juce::uint32 c[6] = { 0xffe07a8c, 0xff63b3ed, 0xff9ccc65, 0xffe0a458, 0xff9b8cf0, 0xff4fd1c5 }; return juce::Colour (c[i % 6]); }
    static juce::String methodName (int m) { return m == 0 ? juce::String ("EQ") : (m == 1 ? juce::String (juce::CharPointer_UTF8 ("DIN\xC3\x82MICA")) : juce::String ("SIDECHAIN")); }

    // ---- accoes
    void rename (int slot, const std::string& cur)
    {
        auto* w = new juce::AlertWindow ("Nome da pista", T8 ("Nome desta pista (ex.: Kick, Bass, Vocals):"), juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("n", juce::String (cur), ""); w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey)); w->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, slot] (int r)
        {
            if (r != 1) return; const auto n = w->getTextEditorContents ("n").trim(); if (n.isEmpty()) return;
            if (slot == proc.mixSlot()) proc.setTrackName (n, true); else { pq::MixRegistry::get().setName (slot, n.toStdString()); pq::MixRegistry::get().setPriority (slot, pq::guessPriority (n.toStdString())); }
            rows = pq::MixRegistry::get().list (&proc); repaint();
        }), true);
    }
    void send (int victimIdx, const std::vector<pq::DeresProposal>& props)
    {
        if (victimIdx < 0 || victimIdx >= (int) slotOf.size() || props.empty()) return;
        pq::MixMail m; m.props = props; m.undoName = "Mix Unmask"; pq::MixRegistry::get().post (slotOf[(size_t) victimIdx], m);
        status = T8 ("Enviado para \"") + nameOf (victimIdx) + T8 ("\": ") + juce::String ((int) props.size()) + T8 (" banda(s). Use Smart Ops › Revert nessa pista para desfazer.");
        repaint();
    }
    static const pq::DeresProposal& propOf (const pq::MixConflict& c, int m) { return m == 0 ? c.pEq : (m == 1 ? c.pDyn : c.pSc); }
    void applyConflict (int ci, int m)
    {
        if (selPair >= (int) res.pairs.size()) return; const auto& pr = res.pairs[(size_t) selPair]; if (ci >= (int) pr.conflicts.size()) return;
        send (pr.victimIdx, { propOf (pr.conflicts[(size_t) ci], m) });
    }
    void applyRecommended()
    {
        if (selPair >= (int) res.pairs.size()) return; const auto& pr = res.pairs[(size_t) selPair]; std::vector<pq::DeresProposal> v;
        for (const auto& c : pr.conflicts) v.push_back (propOf (c, (int) pr.best));
        send (pr.victimIdx, v);
    }

    // ---- pintura
    void paintTracks (juce::Graphics& g)
    {
        using namespace pqui; auto L = leftArea();
        g.setColour (kText); g.setFont (fontOf (13.f, true)); g.drawText (T8 ("PISTAS NA SESSÃO"), 14, 10, 300, 18, juce::Justification::centredLeft);
        g.setColour (kDim); g.setFont (fontOf (10.f)); g.drawText (T8 ("clique no nome para renomear · chip P = prioridade (quem é protegido)"), 14, 28, 310, 14, juce::Justification::centredLeft);
        auto rr = rowRects(); int shown = 0;
        for (size_t i = 0; i < rows.size() && i < rr.size(); ++i)
        {
            const auto& t = rows[i]; const auto r = rr[i]; ++shown;
            g.setColour (kPanel2); g.fillRoundedRectangle (r.toFloat(), 6.f);
            int ti = -1; for (size_t k = 0; k < slotOf.size(); ++k) if (slotOf[k] == t.slot) ti = (int) k;
            const auto col = ti >= 0 ? trackColour (ti) : kDim;
            g.setColour (col); g.fillRoundedRectangle ((float) r.getX(), (float) r.getY() + 6, 4.f, (float) r.getHeight() - 12, 2.f);
            g.setColour (kText); g.setFont (fontOf (13.f, true)); g.drawText (juce::String (t.name) + (t.isSelf ? T8 ("  (esta pista)") : juce::String()), r.getX() + 14, r.getY() + 5, r.getWidth() - 160, 20, juce::Justification::centredLeft);
            g.setColour (kDim); g.setFont (fontOf (10.f));
            g.drawText (t.analysing ? (juce::String (std::min<int> (t.head, pq::kMixHist - 2) / 40.0, 1) + " s medidos · pico " + juce::String ((int) std::lround (std::max (-120.f, t.lastDb))) + " dBFS") : T8 ("análise desligada"), r.getX() + 14, r.getY() + 26, r.getWidth() - 160, 16, juce::Justification::centredLeft);
            auto cp = chipPri (r); g.setColour (kLine); g.fillRoundedRectangle (cp.toFloat(), 5.f); g.setColour (kText); g.setFont (fontOf (11.f, true)); g.drawText ("P" + juce::String (t.priority), cp, juce::Justification::centred);
            auto co = chipOn (r); g.setColour (t.analysing ? kTealDim : kLine); g.fillRoundedRectangle (co.toFloat(), 5.f); g.setColour (t.analysing ? kTeal : kDim); g.setFont (fontOf (10.f, true));
            g.drawText (t.analysing ? "A ANALISAR" : "ANALISAR", co, juce::Justification::centred);
        }
        if (shown == 0) { g.setColour (kDim); g.setFont (fontOf (11.f)); g.drawText (T8 ("(sem pistas — abra o PRIMEQ pro em mais pistas)"), 14, 60, 300, 20, juce::Justification::centredLeft); }
        g.setColour (kDim); g.setFont (fontOf (10.f));
        g.drawFittedText (T8 ("Só vê instâncias abertas no mesmo processo do anfitrião. A medição é opt-in (usa CPU)."), 14, getHeight() - 132, 304, 40, juce::Justification::topLeft, 3);
    }
    static float fx (double f, juce::Rectangle<float> r) { return r.getX() + (float) (std::log (f / 80.0) / std::log (12000.0 / 80.0)) * r.getWidth(); }
    void paintPlot (juce::Graphics& g)
    {
        using namespace pqui; auto pr = plotRect().toFloat(); auto inner = pr.withTrimmedLeft (34).withTrimmedBottom (18).withTrimmedTop (22);
        g.setColour (kPanel2); g.fillRoundedRectangle (pr, 6.f);
        g.setColour (kText); g.setFont (fontOf (13.f, true)); g.drawText (T8 ("ESPECTRO POR PISTA (mediana quando toca) — zonas vermelhas = mascaramento"), (int) pr.getX() + 10, (int) pr.getY() + 3, 700, 18, juce::Justification::centredLeft);
        for (double f : { 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
        { const float x = fx (f, inner); g.setColour (kGrid); g.drawVerticalLine ((int) x, inner.getY(), inner.getBottom()); g.setColour (kDim); g.setFont (fontOf (9.f)); g.drawText (f >= 1000 ? juce::String (f / 1000.0, f == 1000 || f == 2000 || f == 5000 || f == 10000 ? 0 : 1) + "k" : juce::String ((int) f), (int) x - 18, (int) inner.getBottom() + 2, 36, 12, juce::Justification::centred); }
        for (double d : { 0.0, -20.0, -40.0, -60.0 }) { const float y = inner.getY() + (float) (-d / 70.0) * inner.getHeight(); g.setColour (kGrid); g.drawHorizontalLine ((int) y, inner.getX(), inner.getRight()); g.setColour (kDim); g.setFont (fontOf (9.f)); g.drawText (juce::String ((int) d), (int) pr.getX() + 2, (int) y - 6, 30, 12, juce::Justification::centredRight); }
        if (selPair < (int) res.pairs.size() && ! res.pairs.empty())
            for (const auto& c : res.pairs[(size_t) selPair].conflicts)
            { const float x0 = fx (c.freq / 1.30, inner), x1 = fx (c.freq * 1.30, inner); g.setColour (kRed.withAlpha (0.20f)); g.fillRect (x0, inner.getY(), x1 - x0, inner.getHeight()); g.setColour (kRed.withAlpha (0.8f)); g.setFont (fontOf (10.f, true)); g.drawText (juce::String ((int) std::lround (c.freq)) + " Hz", (int) (x0 + x1) / 2 - 30, (int) inner.getY() + 1, 60, 12, juce::Justification::centred); }
        int lx = (int) pr.getRight() - 8;
        for (int t = (int) res.curve.size() - 1; t >= 0; --t)
        {
            const auto col = trackColour (t); juce::Path p, fill; bool open = false; float lastX = 0;
            for (int b = 0; b < pq::kMixBands; ++b)
            {
                const double v = res.curve[(size_t) t][(size_t) b]; if (v < -119) { if (open) { fill.lineTo (lastX, inner.getBottom()); fill.closeSubPath(); open = false; } continue; }
                const float x = fx (pq::mixBandFreq (b), inner), y = inner.getY() + (float) juce::jlimit (0.0, 1.0, -v / 70.0) * inner.getHeight();
                if (! open) { p.startNewSubPath (x, y); fill.startNewSubPath (x, inner.getBottom()); fill.lineTo (x, y); open = true; } else { p.lineTo (x, y); fill.lineTo (x, y); } lastX = x;
            }
            if (open) { fill.lineTo (lastX, inner.getBottom()); fill.closeSubPath(); }
            g.setColour (col.withAlpha (0.14f)); g.fillPath (fill); g.setColour (col); g.strokePath (p, juce::PathStrokeType (2.f));
            const auto nm = nameOf (t); const int w = juce::GlyphArrangement::getStringWidthInt (fontOf (11.f, true), nm) + 22; lx -= w; g.fillEllipse ((float) lx, pr.getY() + 8, 8.f, 8.f); g.setColour (kText); g.setFont (fontOf (11.f, true)); g.drawText (nm, lx + 12, (int) pr.getY() + 3, w - 12, 18, juce::Justification::centredLeft);
        }
        if (res.curve.empty()) { g.setColour (kDim); g.setFont (fontOf (12.f)); g.drawText (status, plotRect().reduced (20), juce::Justification::centred); }
    }
    void paintPairs (juce::Graphics& g)
    {
        using namespace pqui; const auto chips = pairChipRects();
        if (res.pairs.empty())
        {
            g.setColour (kDim); g.setFont (fontOf (12.f)); g.drawFittedText (status, rightArea().reduced (24).withTop (rightArea().getY() + 270).withHeight (60), juce::Justification::topLeft, 3);
            paintGuide (g, nullptr); return;
        }
        for (size_t i = 0; i < chips.size(); ++i)
        {
            const auto& pr = res.pairs[i]; const bool sel = (int) i == selPair; g.setColour (sel ? kTealDim : kLine); g.fillRoundedRectangle (chips[i].toFloat(), 6.f);
            g.setColour (sel ? kTeal : kDim); g.setFont (fontOf (11.f, true)); g.drawText (nameOf (pr.protectedIdx) + juce::String (juce::CharPointer_UTF8 (" \xE2\x86\x92 ")) + nameOf (pr.victimIdx), chips[i], juce::Justification::centred);
        }
        { auto r = chipsRow(); r.removeFromLeft ((int) chips.size() * (chipW() + 6)); g.setColour (kDim); g.setFont (fontOf (10.f)); g.drawText (T8 ("protegida → cede   ·   melhor método para o par: ") + juce::String (methodName ((int) res.pairs[(size_t) selPair].best)), r, juce::Justification::centredLeft); }
        const auto& pr = res.pairs[(size_t) selPair];
        for (size_t ci = 0; ci < pr.conflicts.size() && ci < 3; ++ci)
        {
            const auto& c = pr.conflicts[ci]; auto cr = cardRect ((int) ci); g.setColour (kPanel2); g.fillRoundedRectangle (cr.toFloat(), 8.f);
            auto in = cr.reduced (12, 8);
            g.setColour (kText); g.setFont (fontOf (13.f, true)); g.drawText (juce::String ((int) std::lround (c.freq)) + " Hz   ·   " + T8 ("mascaramento médio ") + juce::String (c.depthDb, 1) + " dB   ·   " + T8 ("corte sugerido ") + juce::String (c.cutDb, 1) + " dB  (Q " + juce::String (c.q, 1) + ")", in.removeFromTop (18), juce::Justification::centredLeft);
            const pq::MixScore* sc[3] = { &c.eq, &c.dyn, &c.sc };
            auto bars = in.removeFromTop (58);
            for (int m = 0; m < 3; ++m)
            {
                auto row = bars.removeFromTop (19); g.setColour (m == (int) c.best ? kTeal : kDim); g.setFont (fontOf (10.f, true)); g.drawText (methodName (m), row.removeFromLeft (84), juce::Justification::centredLeft);
                auto bar = row.removeFromLeft (360).reduced (0, 5).toFloat(); g.setColour (kLine); g.fillRoundedRectangle (bar, 3.f);
                g.setColour (m == (int) c.best ? kTeal : kTealDim); g.fillRoundedRectangle (bar.withWidth (std::max (3.f, bar.getWidth() * (float) sc[m]->score)), 3.f);
                g.setColour (kText); g.setFont (fontOf (10.f)); g.drawText (juce::String ((int) std::lround (100 * sc[m]->score)) + T8 ("%  ·  remove ") + juce::String ((int) std::lround (100 * sc[m]->benefit)) + T8 ("% · corte desnecessário ") + juce::String ((int) std::lround (100 * sc[m]->collateral)) + "%", row.withTrimmedLeft (8), juce::Justification::centredLeft);
                if (m == (int) c.best) { g.setColour (kTeal); g.setFont (fontOf (10.f, true)); g.drawText (T8 ("RECOMENDADO"), cr.getRight() - 110, cr.getY() + 8, 100, 14, juce::Justification::centredRight); }
            }
        }
        paintGuide (g, &pr);
    }
    void paintGuide (juce::Graphics& g, const pq::MixPair* pr)
    {
        using namespace pqui; auto r = guideRect(); r.removeFromBottom (36); if (r.getHeight() < 30) return;
        g.setColour (kPanel2); g.fillRoundedRectangle (r.toFloat(), 8.f); auto in = r.reduced (12, 8);
        g.setColour (kText); g.setFont (fontOf (12.f, true)); g.drawText (T8 ("COMO LIGAR NO DAW"), in.removeFromTop (16), juce::Justification::centredLeft);
        juce::String P = pr ? nameOf (pr->protectedIdx) : juce::String ("protegida"), V = pr ? nameOf (pr->victimIdx) : juce::String ("cede");
        g.setColour (kDim); g.setFont (fontOf (10.f));
        g.drawFittedText (T8 ("EQ e Dinâmica: sem routing — o corte é feito na pista \"") + V + T8 ("\".\nSidechain: em \"") + V + T8 ("\" ative a entrada sidechain do PRIMEQ pro e envie-lhe o áudio de \"") + P + T8 ("\".  Logic: menu Side Chain do plugin · Ableton: Audio From › sidechain · Cubase: Ativar Side-Chain + Send · Pro Tools: Key Input (bus) · Reaper: send para os canais 3/4 · FL Studio: Sidechain to this track · Studio One: Sidechain no plugin. O botão aplicar já liga \"scext\"."), in, juce::Justification::topLeft, 5);
    }
};
