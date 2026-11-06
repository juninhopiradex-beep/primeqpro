// =============================================================================
//  PRIMEQ PRO by Piradex — painel STUDIO: alvo por genero, Tradutor, Palavras, Cofre de decisoes (+ficha, assinatura), Ensino
//  Tudo deterministico (ver SmartOps/Studio.h). As curvas vem da medicao desta pista (ANALISAR ligado); sem medicao o painel diz-o.
// =============================================================================
#pragma once
#include "LookAndFeel.h"
#include "EqGraph.h"
#include "../Plugin/PluginProcessor.h"
#include "../SmartOps/Studio.h"

class StudioPanel : public juce::Component, private juce::Timer
{
public:
    enum Tab { TTarget = 0, TTrans, TWords, TVault, TTeach, kTabs };
    StudioPanel (PrimeqProcessor& p, EqGraph& g) : proc (p), graph (g)
    {
        using namespace pqui;
        static const char* tn[kTabs] = { "ALVO", "TRADUTOR", "PALAVRAS", "COFRE", "ENSINO" };
        for (int i = 0; i < kTabs; ++i) { tabB[i].setButtonText (tn[i]); tabB[i].setClickingTogglesState (false); tabB[i].onClick = [this, i] { setTab (i); }; addAndMakeVisible (tabB[i]); }
        addAndMakeVisible (analyseB); analyseB.setButtonText ("ANALISAR"); analyseB.setTooltip ("Mede esta pista (necess\xC3\xA1rio para o alvo e o Tradutor). Toque o material durante alguns segundos.");
        analyseB.onClick = [this] { proc.setAnalysing (analyseB.getToggleState()); };
        // --- alvo
        for (const auto& ge : pq::studio::genres()) genreBox.addItem (juce::String (juce::CharPointer_UTF8 (ge.pt)), genreBox.getNumItems() + 1);
        genreBox.addItem ("A minha (gravada)", 100); genreBox.setSelectedId (2, juce::dontSendNotification); genreBox.onChange = [this] { recompute(); }; addAndMakeVisible (genreBox);
        struct SD { juce::Slider* s; juce::Label* l; const char* name; double lo, hi; };
        for (auto& d : { SD { &sTilt, &lTilt, "Inclina\xC3\xA7\xC3\xA3o", -1.0, 1.0 }, SD { &sLow, &lLow, "Graves", -6.0, 6.0 }, SD { &sPres, &lPres, "Presen\xC3\xA7""a", -6.0, 6.0 }, SD { &sAir, &lAir, "Ar", -6.0, 6.0 } })
        {
            d.s->setSliderStyle (juce::Slider::LinearHorizontal); d.s->setRange (d.lo, d.hi, 0.1); d.s->setValue (0.0, juce::dontSendNotification); d.s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 18);
            d.s->setColour (juce::Slider::thumbColourId, kTeal); d.s->setColour (juce::Slider::trackColourId, kTealDim); d.s->setColour (juce::Slider::textBoxTextColourId, kText);
            d.s->onValueChange = [this] { recompute(); }; d.l->setText (T8 (d.name), juce::dontSendNotification); d.l->setColour (juce::Label::textColourId, kDim); d.l->setFont (fontOf (11.f));
            addAndMakeVisible (d.s); addAndMakeVisible (d.l);
        }
        recB.setButtonText ("GRAVAR A MINHA"); recB.setTooltip ("Guarda a forma tonal desta pista como alvo pessoal (fica no projeto)."); recB.onClick = [this] { recordMine(); };
        propB.setButtonText ("PROPOR BANDAS"); propB.onClick = [this] { proposeTarget(); }; applyB.setButtonText ("APLICAR"); applyB.onClick = [this] { applyPending ("Alvo", "Target"); };
        revB.setButtonText ("REVERTER"); revB.onClick = [this] { proc.smartRevert(); graph.showPreview = false; graph.repaint(); };
        sugB.setButtonText ("SUGERIR PRESET"); sugB.setTooltip ("O preset de f\xC3\xA1""brica que mais aproxima esta pista do alvo."); sugB.onClick = [this] { suggestPresets(); };
        for (auto* b : { &recB, &propB, &applyB, &revB, &sugB }) addAndMakeVisible (b);
        for (auto& b : pickB) { addChildComponent (b); }
        // --- tradutor
        for (int i = 0; i < 5; ++i) { devB[i].setButtonText (juce::String (juce::CharPointer_UTF8 (pq::studio::devices()[(size_t) i].pt))); devB[i].setClickingTogglesState (false); devB[i].onClick = [this, i] { dev = i; if (listenB.getToggleState()) proc.setTranslator (dev + 1); recompute(); }; addAndMakeVisible (devB[i]); }
        listenB.setButtonText ("OUVIR ASSIM"); listenB.setTooltip ("Filtra a sa\xC3\xADda do plugin como esse equipamento (s\xC3\xB3 monitoriza\xC3\xA7\xC3\xA3o; desliga ao fechar o plugin)."); listenB.onClick = [this] { proc.setTranslator (listenB.getToggleState() ? dev + 1 : 0); recompute(); }; addAndMakeVisible (listenB);
        // --- palavras
        wordsEd.setMultiLine (false); wordsEd.setTextToShowWhenEmpty ("ex.: menos lama, mais presen\xC3\xA7""a e brilho", kDim); wordsEd.setColour (juce::TextEditor::backgroundColourId, kLine); wordsEd.setColour (juce::TextEditor::textColourId, kText); wordsEd.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        wordsEd.setFont (fontOf (13.f)); wordsEd.onReturnKey = [this] { interpretWords(); }; addAndMakeVisible (wordsEd);
        sWords.setSliderStyle (juce::Slider::LinearHorizontal); sWords.setRange (0, 100, 1); sWords.setValue (50, juce::dontSendNotification); sWords.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 18); sWords.setColour (juce::Slider::thumbColourId, kTeal); sWords.setColour (juce::Slider::trackColourId, kTealDim); sWords.setColour (juce::Slider::textBoxTextColourId, kText);
        sWords.onValueChange = [this] { if (wordsPlanned) interpretWords(); }; addAndMakeVisible (sWords); lWords.setText (T8 ("INTENSIDADE"), juce::dontSendNotification); lWords.setColour (juce::Label::textColourId, kDim); lWords.setFont (fontOf (10.f, true)); addAndMakeVisible (lWords);
        wIntB.setButtonText ("INTERPRETAR"); wIntB.onClick = [this] { interpretWords(); }; wApB.setButtonText ("APLICAR"); wApB.onClick = [this] { applyPending ("Palavras", "Words"); }; wRevB.setButtonText ("REVERTER"); wRevB.onClick = [this] { proc.smartRevert(); graph.showPreview = false; graph.repaint(); };
        for (auto* b : { &wIntB, &wApB, &wRevB }) addAndMakeVisible (b);
        // --- cofre
        noteB.setButtonText ("NOTA..."); noteB.setTooltip ("Guarda o estado atual com o motivo que escrever."); noteB.onClick = [this] { addNote(); };
        restB.setButtonText ("RESTAURAR"); restB.setTooltip ("Volta ao estado das bandas desse momento (com Undo)."); restB.onClick = [this] { if (selDec >= 0) { proc.restoreDecision (selDec); proc.logDecision ("Restaurado: decis\xC3\xA3o " + std::to_string (selDec + 1), "Restored: decision " + std::to_string (selDec + 1), "restore"); repaint(); } };
        sheetB.setButtonText ("FICHA..."); sheetB.setTooltip ("Exporta a ficha da sess\xC3\xA3o (HTML bilingue) para enviar ao cliente."); sheetB.onClick = [this] { exportSheet(); };
        clrB.setButtonText ("LIMPAR"); clrB.onClick = [this] { proc.clearDecisions(); selDec = -1; repaint(); };
        sigSendB.setButtonText ("ENVIAR ASSINATURA"); sigSendB.setTooltip ("Copia as bandas desta pista para a pista escolhida (fica reversível l\xC3\xA1)."); sigSendB.onClick = [this] { sendSignature (false); };
        sigGrpB.setButtonText ("AO GRUPO"); sigGrpB.onClick = [this] { sendSignature (true); };
        for (auto* b : { &noteB, &restB, &sheetB, &clrB, &sigSendB, &sigGrpB }) addAndMakeVisible (b); addAndMakeVisible (sigBox);
        // --- ensino
        teachB.setButtonText ("EXPLICAR AO VIVO"); teachB.setClickingTogglesState (true); teachB.setToggleState (true, juce::dontSendNotification); addAndMakeVisible (teachB);
        setTab (TTarget); startTimerHz (4);
    }
    ~StudioPanel() override { proc.setTranslator (0); graph.setStudioOverlay ({}, false, {}); }
    void setActive (bool on)
    {
        setVisible (on); if (on) { analyseB.setToggleState (proc.analysing(), juce::dontSendNotification); recompute(); } else { proc.setTranslator (0); listenB.setToggleState (false, juce::dontSendNotification); graph.setStudioOverlay ({}, false, {}); graph.showPreview = false; }
    }
    // para testes
    void setTab (int t)
    {
        tab = t; for (int i = 0; i < kTabs; ++i) tabB[i].setToggleState (i == t, juce::dontSendNotification);
        const bool a = t == TTarget, b = t == TTrans, c = t == TWords, d = t == TVault, e = t == TTeach;
        for (auto* x : std::initializer_list<juce::Component*> { &genreBox, &sTilt, &sLow, &sPres, &sAir, &lTilt, &lLow, &lPres, &lAir, &recB, &propB, &applyB, &revB, &sugB }) x->setVisible (a);
        for (auto& x : devB) x.setVisible (b); listenB.setVisible (b);
        for (auto* x : std::initializer_list<juce::Component*> { &wordsEd, &sWords, &lWords, &wIntB, &wApB, &wRevB }) x->setVisible (c);
        for (auto* x : std::initializer_list<juce::Component*> { &noteB, &restB, &sheetB, &clrB, &sigSendB, &sigGrpB, &sigBox }) x->setVisible (d);
        teachB.setVisible (e); for (auto& x : pickB) x.setVisible (a && ! picks.empty());
        resized(); recompute(); repaint();
    }
    int currentTab() const { return tab; }
    const pq::studio::Deviation& deviation() const { return devn; }
    const pq::studio::Translation& translation() const { return trn; }
    const std::vector<std::string>& lines() const { return linesNow; }
    std::string sheet() { return buildSheet(); }
    void setWords (const juce::String& s) { wordsEd.setText (s); interpretWords(); }
    int pendingBands() const { return (int) pending.size(); }
    void pickTarget (int genreId) { genreBox.setSelectedId (genreId, juce::sendNotification); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 10); r.removeFromTop (26);
        auto tr = r.removeFromTop (26); const int w = tr.getWidth() / kTabs; for (int i = 0; i < kTabs; ++i) tabB[i].setBounds (i == kTabs - 1 ? tr : tr.removeFromLeft (w).reduced (1, 0));
        r.removeFromTop (6); analyseB.setBounds (r.removeFromTop (22)); r.removeFromTop (6); body = r;
        auto c = body;
        switch (tab)
        {
            case TTarget:
            {
                genreBox.setBounds (c.removeFromTop (26)); c.removeFromTop (4);
                juce::Slider* ss[4] = { &sTilt, &sLow, &sPres, &sAir }; juce::Label* ll[4] = { &lTilt, &lLow, &lPres, &lAir };
                for (int i = 0; i < 4; ++i) { auto row = c.removeFromTop (22); ll[i]->setBounds (row.removeFromLeft (84)); ss[i]->setBounds (row); }
                c.removeFromTop (6); auto r1 = c.removeFromTop (28); recB.setBounds (r1.removeFromLeft (r1.getWidth() / 2 - 3)); r1.removeFromLeft (6); sugB.setBounds (r1);
                c.removeFromTop (4); auto r2 = c.removeFromTop (28); const int w3 = (r2.getWidth() - 12) / 3; propB.setBounds (r2.removeFromLeft (w3)); r2.removeFromLeft (6); applyB.setBounds (r2.removeFromLeft (w3)); r2.removeFromLeft (6); revB.setBounds (r2);
                c.removeFromTop (6); textArea = c.removeFromTop (std::max (60, c.getHeight() - 96));
                for (auto& b : pickB) b.setBounds (c.removeFromTop (28).reduced (0, 2)); break;
            }
            case TTrans:
            {
                for (auto& b : devB) { b.setBounds (c.removeFromTop (26).reduced (0, 1)); }
                c.removeFromTop (4); listenB.setBounds (c.removeFromTop (28)); c.removeFromTop (6); textArea = c; break;
            }
            case TWords:
            {
                wordsEd.setBounds (c.removeFromTop (30)); c.removeFromTop (4); auto row = c.removeFromTop (22); lWords.setBounds (row.removeFromLeft (84)); sWords.setBounds (row); c.removeFromTop (4);
                auto r2 = c.removeFromTop (28); const int w3 = (r2.getWidth() - 12) / 3; wIntB.setBounds (r2.removeFromLeft (w3)); r2.removeFromLeft (6); wApB.setBounds (r2.removeFromLeft (w3)); r2.removeFromLeft (6); wRevB.setBounds (r2);
                c.removeFromTop (6); textArea = c; break;
            }
            case TVault:
            {
                auto r1 = c.removeFromTop (28); const int w4 = (r1.getWidth() - 18) / 4; noteB.setBounds (r1.removeFromLeft (w4)); r1.removeFromLeft (6); restB.setBounds (r1.removeFromLeft (w4)); r1.removeFromLeft (6); sheetB.setBounds (r1.removeFromLeft (w4)); r1.removeFromLeft (6); clrB.setBounds (r1);
                auto bot = c.removeFromBottom (62); sigBox.setBounds (bot.removeFromTop (26)); bot.removeFromTop (4); auto r2 = bot.removeFromTop (28); sigSendB.setBounds (r2.removeFromLeft (r2.getWidth() * 2 / 3 - 3)); r2.removeFromLeft (6); sigGrpB.setBounds (r2);
                c.removeFromTop (6); listArea = c.removeFromTop (std::max (60, c.getHeight() - 6)); break;
            }
            default: teachB.setBounds (c.removeFromTop (26)); c.removeFromTop (6); textArea = c; break;
        }
    }
    void paint (juce::Graphics& g) override
    {
        using namespace pqui; const bool en = i18n::isEn();
        g.setColour (kPanel); g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.f); g.setColour (kLine); g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 8.f, 1.f);
        g.setColour (kText); g.setFont (fontOf (13.f, true)); g.drawText ("STUDIO", 14, 8, 200, 20, juce::Justification::centredLeft);
        g.setColour (kDim); g.setFont (fontOf (11.f));
        auto draw = [&] (juce::Rectangle<int> a, const std::string& s) { g.drawFittedText (juce::String (juce::CharPointer_UTF8 (s.c_str())), a, juce::Justification::topLeft, 40, 1.f); };
        if (tab == TVault)
        {
            const auto D = proc.decisions(); int y = listArea.getY(); const int rh = 36; g.setColour (kDim);
            if (D.empty()) { draw (listArea, en ? "No decisions yet. Presets, Smart Ops and Assistant actions are recorded automatically; use NOTE to record why you changed something." : "Ainda sem decis\xC3\xB5""es. Presets, Smart Ops e a\xC3\xA7\xC3\xB5""es do Assistente ficam registados automaticamente; use NOTA para registar o motivo de uma altera\xC3\xA7\xC3\xA3o."); return; }
            const int maxRows = std::max (1, listArea.getHeight() / rh); const int first = std::max (0, (int) D.size() - maxRows);
            for (int i = first; i < (int) D.size(); ++i)
            {
                juce::Rectangle<int> row (listArea.getX(), y, listArea.getWidth(), rh - 2); y += rh;
                g.setColour (i == selDec ? kTealDim : kLine); g.fillRoundedRectangle (row.toFloat(), 5.f);
                const auto t = juce::Time (D[(size_t) i].ms).toString (false, true, false); g.setColour (kTeal); g.setFont (fontOf (10.f, true)); g.drawText (juce::String (i + 1) + "  " + t, row.getX() + 6, row.getY() + 2, 120, 12, juce::Justification::centredLeft);
                g.setColour (kText); g.setFont (fontOf (11.f)); g.drawFittedText (juce::String (juce::CharPointer_UTF8 ((en && ! D[(size_t) i].en.empty() ? D[(size_t) i].en : D[(size_t) i].pt).c_str())), row.getX() + 6, row.getY() + 14, row.getWidth() - 12, 20, juce::Justification::topLeft, 1);
            }
            return;
        }
        if (tab == TTeach)
        {
            pq::BandParams a[pq::kNumBands]; proc.readBands (a); std::string s; int n = 0; const int sel = graph.selected;
            if (sel >= 0 && sel < pq::kNumBands && a[sel].on) s += (en ? "SELECTED BAND\n" : "BANDA SELECIONADA\n") + pq::studio::explainBand (a[sel], en) + "\n\n";
            else s += en ? "Click a band on the graph to see what it does.\n\n" : "Clique numa banda no gr\xC3\xA1""fico para ver o que ela faz.\n\n";
            s += en ? "ALL ACTIVE BANDS\n" : "TODAS AS BANDAS ATIVAS\n";
            for (int i = 0; i < pq::kNumBands; ++i) if (a[i].on) { s += std::to_string (i + 1) + ". " + pq::studio::bandSummary (a[i], en) + "\n"; ++n; }
            if (n == 0) s += en ? "(none)" : "(nenhuma)";
            draw (textArea, s); return;
        }
        draw (textArea, text);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (tab != TVault || ! listArea.contains (e.getPosition())) return;
        const auto D = proc.decisions(); const int rh = 36; const int maxRows = std::max (1, listArea.getHeight() / rh); const int first = std::max (0, (int) D.size() - maxRows);
        const int i = first + (e.y - listArea.getY()) / rh; selDec = (i >= 0 && i < (int) D.size()) ? i : -1; repaint();
    }
    void refreshLanguage()
    {
        using namespace pqui; const int keep = genreBox.getSelectedId(); genreBox.clear (juce::dontSendNotification);
        for (const auto& ge : pq::studio::genres()) genreBox.addItem (T8 (ge.pt), genreBox.getNumItems() + 1);
        genreBox.addItem (T8 ("A minha (gravada)"), 100); genreBox.setSelectedId (keep > 0 ? keep : 2, juce::dontSendNotification);
        wordsEd.setTextToShowWhenEmpty (i18n::isEn() ? "e.g.: less mud, more presence and brightness" : "ex.: menos lama, mais presen\xC3\xA7""a e brilho", kDim);
        for (int i = 0; i < 5; ++i) devB[i].setButtonText (T8 (pq::studio::devices()[(size_t) i].pt)); refreshSigBox(); recompute(); repaint();
    }

private:
    void timerCallback() override
    {
        if (! isVisible()) return;
        if (tab == TTarget || tab == TTrans) { if (++tickN % 4 == 0 || ! haveCurve) recompute(); }
        else if (tab == TTeach || tab == TVault) repaint();
        if (tab == TVault) refreshSigBox();
    }
    pq::studio::TargetAdjust adjust() const { return { sTilt.getValue(), sLow.getValue(), sPres.getValue(), sAir.getValue() }; }
    pq::studio::Curve currentTarget() const
    {
        using namespace pq::studio; const int id = genreBox.getSelectedId();
        if (id == 100) { Curve c; if (proc.customTargetGet (c)) return c; return emptyCurve(); }
        return targetCurve (id - 1, adjust());
    }
    juce::String targetName (bool en) const { const int id = genreBox.getSelectedId(); if (id == 100) return en ? "My recorded curve" : "A minha curva gravada"; const auto& g = pq::studio::genres()[(size_t) std::clamp (id - 1, 0, (int) pq::studio::genres().size() - 1)]; return juce::String (juce::CharPointer_UTF8 (en ? g.en : g.pt)); }
    void recompute()
    {
        using namespace pq::studio; const bool en = pqui::i18n::isEn(); std::string s; haveCurve = proc.ownCurve (curve);
        std::vector<std::pair<double, double>> lost; Curve devOv = emptyCurve(); bool showDev = false;
        if (! haveCurve)
        {
            s = en ? "No measurement yet. Turn on ANALYSE above and play the material for a few seconds (this track's tonal profile is measured in 28 bands, 80 Hz\xE2\x80\x93""12 kHz)."
                   : "Ainda sem medi\xC3\xA7\xC3\xA3o. Ligue ANALISAR acima e toque o material durante alguns segundos (mede-se o perfil tonal desta pista em 28 bandas, 80 Hz\xE2\x80\x93""12 kHz).";
            devn = Deviation(); trn = Translation();
        }
        else if (tab == TTarget)
        {
            const Curve tg = currentTarget(); devn = compare (curve, tg);
            if (! devn.ok) s = en ? "This target has no data yet (record your curve first)." : "Este alvo ainda n\xC3\xA3o tem dados (grave primeiro a sua curva).";
            else
            {
                char b[200]; std::snprintf (b, sizeof b, en ? "Target: %s \xC2\xB7 deviation %.1f dB RMS\n\n" : "Alvo: %s \xC2\xB7 desvio %.1f dB RMS\n\n", targetName (en).toRawUTF8(), devn.rms); s = b;
                for (const auto& l : (en ? devn.linesEn : devn.linesPt)) s += "\xE2\x80\xA2 " + l + "\n";
                s += en ? "\nThe curve on the graph shows where the track is ABOVE (amber) or BELOW (blue) the target, aligned by median level. Targets are heuristic starting points, not rules."
                        : "\nA curva no gr\xC3\xA1""fico mostra onde a pista est\xC3\xA1 ACIMA (\xC3\xA2mbar) ou ABAIXO (azul) do alvo, alinhada pelo n\xC3\xADvel mediano. Os alvos s\xC3\xA3o pontos de partida heur\xC3\xADsticos, n\xC3\xA3o regras.";
                devOv = devn.dev; showDev = true;
            }
        }
        else if (tab == TTrans)
        {
            pq::SpatialStats st; const bool hs = proc.ownStats (st); trn = translate (curve, dev, hs ? &st : nullptr);
            if (! trn.ok) s = en ? "Not enough data." : "Dados insuficientes.";
            else
            {
                char b[200]; std::snprintf (b, sizeof b, en ? "%s: %.0f%% of the measured energy stays audible.\n\n" : "%s: %.0f%% da energia medida continua aud\xC3\xADvel.\n\n", juce::String (juce::CharPointer_UTF8 (en ? devices()[(size_t) dev].en : devices()[(size_t) dev].pt)).toRawUTF8(), trn.retainedPct); s = b;
                for (const auto& l : (en ? trn.linesEn : trn.linesPt)) s += "\xE2\x80\xA2 " + l + "\n";
                s += en ? "\nGeneric response of an equipment CATEGORY with an estimated ambient noise floor \xE2\x80\x94 not a measurement of any specific model. Use OUVIR ASSIM to hear it."
                        : "\nResposta gen\xC3\xA9rica de uma CATEGORIA de equipamento, com ru\xC3\xADdo ambiente estimado \xE2\x80\x94 n\xC3\xA3o \xC3\xA9 a medi\xC3\xA7\xC3\xA3o de um modelo concreto. Use OUVIR ASSIM para ouvir.";
                int b0 = 0; while (b0 < pq::kMixBands) { if (trn.status[(size_t) b0] != 2) { ++b0; continue; } int e2 = b0; while (e2 + 1 < pq::kMixBands && trn.status[(size_t) e2 + 1] == 2) ++e2; lost.push_back (std::make_pair (pq::mixBandFreq (b0), pq::mixBandFreq (e2))); b0 = e2 + 1; }
            }
        }
        for (int i = 0; i < 5; ++i) devB[i].setToggleState (i == dev, juce::dontSendNotification);
        text = s; linesNow = en ? devn.linesEn : devn.linesPt; if (tab == TTrans) linesNow = en ? trn.linesEn : trn.linesPt;
        graph.setStudioOverlay (devOv, showDev, lost); repaint();
    }
    void recordMine()
    {
        pq::studio::Curve c; if (! proc.ownCurve (c)) { text = pqui::i18n::isEn() ? "Nothing to record: no measurement yet." : "Nada para gravar: ainda sem medi\xC3\xA7\xC3\xA3o."; repaint(); return; }
        proc.customTargetSet (pq::studio::normalised (c)); genreBox.setSelectedId (100, juce::dontSendNotification);
        proc.logDecision ("Alvo pessoal gravado a partir desta pista", "Personal target recorded from this track", "target"); recompute();
    }
    void proposeTarget()
    {
        using namespace pq::studio; if (! haveCurve) return; pending = proposeToTarget (curve, currentTarget(), 0.7, 6, 6.0); graph.preview = pending; graph.showPreview = ! pending.empty(); graph.repaint();
        const bool en = pqui::i18n::isEn(); text += (en ? "\n\n" : "\n\n") + std::string (pending.empty() ? (en ? "Nothing to propose: the track is already close to the target." : "Nada a propor: a pista j\xC3\xA1 est\xC3\xA1 perto do alvo.") : (en ? std::to_string (pending.size()) + " band(s) proposed (preview on the graph). APPLY creates normal, editable bands." : std::to_string (pending.size()) + " banda(s) proposta(s) (pr\xC3\xA9-visualiza\xC3\xA7\xC3\xA3o no gr\xC3\xA1""fico). APLICAR cria bandas normais e edit\xC3\xA1veis."));
        repaint();
    }
    void applyPending (const char* nPt, const char* nEn)
    {
        if (pending.empty()) return; const bool en = pqui::i18n::isEn(); const int n = proc.smartApply (pending, en ? nEn : nPt); graph.showPreview = false; graph.repaint(); pending.clear();
        text += (en ? "\n\nApplied: " : "\n\nAplicado: ") + std::to_string (n) + (en ? " band(s). REVERT undoes." : " banda(s). REVERTER desfaz."); repaint();
    }
    void suggestPresets()
    {
        using namespace pq::studio; const bool en = pqui::i18n::isEn(); picks.clear();
        if (devn.ok) picks = rankPresets (devn, 3, (int) PrimeqProcessor::factoryPresets().size() - (int) pq::fp::defs().size());
        for (size_t i = 0; i < 3; ++i)
        {
            if (i < picks.size()) { char b[200]; std::snprintf (b, sizeof b, "%s  (%.1f\xE2\x86\x92%.1f dB)", picks[i].name.c_str(), picks[i].errBefore, picks[i].errAfter); pickB[i].setButtonText (juce::String (juce::CharPointer_UTF8 (b))); const int idx = picks[i].index; pickB[i].onClick = [this, idx] { proc.loadFactoryPreset (idx); }; pickB[i].setVisible (tab == TTarget); }
            else pickB[i].setVisible (false);
        }
        text += std::string ("\n\n") + (! devn.ok ? (en ? "Measure the track and choose a target first." : "MeÃ§a a pista e escolha um alvo primeiro.") : picks.empty() ? (en ? "No factory preset brings this track closer to the target." : "Nenhum preset de f\xC3\xA1""brica aproxima esta pista do alvo.") : (en ? "Closest presets (deviation vs target, before \xE2\x86\x92 after). Presets are starting points: listen." : "Presets mais pr\xC3\xB3ximos (desvio vs alvo, antes \xE2\x86\x92 depois). S\xC3\xA3o pontos de partida: ou\xC3\xA7""a."));
        resized(); repaint();
    }
    void interpretWords()
    {
        using namespace pq::studio; const bool en = pqui::i18n::isEn(); const auto w = planWords (wordsEd.getText().toStdString(), sWords.getValue() / 100.0, haveCurve ? &curve : nullptr);
        pending = w.props; wordsPlanned = true; graph.preview = pending; graph.showPreview = ! pending.empty(); graph.repaint();
        std::string s = en ? w.explEn : w.explPt;
        if (! w.recognised.empty()) { s += en ? "\n\nRecognised: " : "\n\nReconhecido: "; for (size_t i = 0; i < w.recognised.size(); ++i) s += (i ? ", " : "") + w.recognised[i]; }
        if (! w.unknown.empty()) { s += en ? "\nNot understood: " : "\nN\xC3\xA3o percebi: "; for (size_t i = 0; i < w.unknown.size(); ++i) s += (i ? "; " : "") + w.unknown[i]; s += en ? "\n(try: less mud, more presence, brighter, more bass, warmer, less harsh, more space)" : "\n(experimente: menos lama, mais presen\xC3\xA7""a, mais brilho, mais graves, mais calor, menos \xC3\xA1spero, mais espa\xC3\xA7o)"; }
        for (const auto& p : w.props) s += "\n\xE2\x80\xA2 " + bandSummary ([&] { pq::BandParams b; b.on = true; b.type = (pq::BandType) p.type; b.freq = p.freq; b.gain = p.gainDb; b.q = p.q; b.dyn = p.dynamic; b.range = p.gainDb; b.thr = p.thrDb; return b; }(), en);
        text = s; repaint();
    }
    void addNote()
    {
        auto* w = new juce::AlertWindow (pqui::T8 ("Motivo da decisão"), pqui::T8 ("Porque mudou isto? (ex.: cortei 250 Hz por masking com o baixo)"), juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("n", "", ""); w->addButton (pqui::T8 ("Guardar"), 1, juce::KeyPress (juce::KeyPress::returnKey)); w->addButton (pqui::T8 ("Cancelar"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int r) { if (r == 1) { const auto t = w->getTextEditorContents ("n").trim().toStdString(); if (! t.empty()) proc.logDecision (t, t, "note"); repaint(); } }), true);
    }
    void refreshSigBox()
    {
        const auto l = pq::MixRegistry::get().list (&proc); int n = 0; for (const auto& t : l) if (! t.isSelf) ++n;
        if (n == lastTrackCount && sigBox.getNumItems() > 0) return; lastTrackCount = n; sigBox.clear (juce::dontSendNotification); sigSlots.clear();
        for (const auto& t : l) if (! t.isSelf) { sigSlots.push_back (t.slot); sigBox.addItem (juce::String (t.name) + "  (G" + juce::String (t.group) + ")", (int) sigSlots.size()); }
        if (sigSlots.empty()) sigBox.setTextWhenNothingSelected (pqui::T8 ("(sem outras pistas)")); else sigBox.setSelectedId (1, juce::dontSendNotification);
    }
    void sendSignature (bool group)
    {
        pq::BandParams a[pq::kNumBands]; proc.readBands (a); const auto props = pq::studio::toProposals (a, "assinatura"); if (props.empty()) return;
        std::vector<int> targets; const int idx = sigBox.getSelectedId() - 1;
        if (group) { const int g = pq::MixRegistry::get().group (proc.mixSlot()); for (const auto& t : pq::MixRegistry::get().list (&proc)) if (! t.isSelf && t.group == g) targets.push_back (t.slot); }
        else if (idx >= 0 && idx < (int) sigSlots.size()) targets.push_back (sigSlots[(size_t) idx]);
        for (int s : targets) { pq::MixMail m; m.props = props; m.undoName = "Assinatura de " + proc.trackName().toStdString(); pq::MixRegistry::get().post (s, m); }
        const bool en = pqui::i18n::isEn(); text = (en ? "Signature sent to " : "Assinatura enviada para ") + std::to_string (targets.size()) + (en ? " track(s) (their Smart Ops \xE2\x80\xBA Revert undoes it)." : " pista(s) (Smart Ops \xE2\x80\xBA Revert nessa pista desfaz).");
        proc.logDecision (text, text, "signature"); repaint();
    }
    std::string buildSheet()
    {
        using namespace pq::studio; SheetData S; S.track = proc.trackName().toStdString(); S.date = juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M").toStdString(); S.decisions = proc.decisions();
        proc.readBands (S.now); if (! S.decisions.empty()) { decodeBands (S.decisions.front().bands, S.first); S.hasFirst = true; }
        Curve c; if (proc.ownCurve (c)) { S.measured = c; const Curve tg = currentTarget(); S.dev = compare (c, tg); S.targetNamePt = targetName (false).toStdString(); S.targetNameEn = targetName (true).toStdString();
            pq::SpatialStats st; const bool hs = proc.ownStats (st); const auto tr = translate (c, dev, hs ? &st : nullptr); if (tr.ok) { S.transPt = tr.linesPt; S.transEn = tr.linesEn; S.retained = tr.retainedPct; S.transDevPt = devices()[(size_t) dev].pt; S.transDevEn = devices()[(size_t) dev].en; } }
        return sheetHtml (S);
    }
    void exportSheet()
    {
        const auto html = buildSheet(); chooser = std::make_unique<juce::FileChooser> (pqui::T8 ("Guardar ficha da sessão"), juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("PRIMEQpro_ficha.html"), "*.html");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting, [this, html] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult(); if (f == juce::File()) return; f.replaceWithText (juce::String (juce::CharPointer_UTF8 (html.c_str())), false, false, "\n");
            text = (pqui::i18n::isEn() ? "Sheet saved to " : "Ficha guardada em ") + f.getFullPathName().toStdString(); repaint();
        });
    }

    PrimeqProcessor& proc; EqGraph& graph; int tab = TTarget, dev = 0, selDec = -1, tickN = 0, lastTrackCount = -1; bool haveCurve = false, wordsPlanned = false;
    pq::studio::Curve curve = pq::studio::emptyCurve(); pq::studio::Deviation devn; pq::studio::Translation trn; std::vector<pq::DeresProposal> pending; std::vector<pq::studio::PresetPick> picks;
    std::string text; std::vector<std::string> linesNow; std::vector<int> sigSlots; juce::Rectangle<int> body, textArea, listArea; std::unique_ptr<juce::FileChooser> chooser;
    juce::TextButton tabB[kTabs]; juce::ToggleButton analyseB;
    juce::ComboBox genreBox; juce::Slider sTilt, sLow, sPres, sAir; juce::Label lTilt, lLow, lPres, lAir; juce::TextButton recB, propB, applyB, revB, sugB, pickB[3];
    juce::TextButton devB[5]; juce::ToggleButton listenB;
    juce::TextEditor wordsEd; juce::Slider sWords; juce::Label lWords; juce::TextButton wIntB, wApB, wRevB;
    juce::TextButton noteB, restB, sheetB, clrB, sigSendB, sigGrpB; juce::ComboBox sigBox; juce::ToggleButton teachB;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StudioPanel)
};
