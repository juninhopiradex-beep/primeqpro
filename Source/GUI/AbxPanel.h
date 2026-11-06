// =============================================================================
//  PRIMEQ PRO by Piradex — teste ABX cego (A = original, B = processado, X = um dos dois, sorteado em segredo)
//  Loudness igualado (compensa a diferenca de nivel medida entre original e processado) para o ouvido nao ser
//  enganado por "mais forte = melhor". Resultado com p-valor binomial. Ver SmartOps/Abx.h.
// =============================================================================
#pragma once
#include "LookAndFeel.h"
#include "../Plugin/PluginProcessor.h"
#include "../SmartOps/Abx.h"
#include "../SmartOps/Studio.h"

class AbxPanel : public juce::Component, private juce::Timer
{
public:
    explicit AbxPanel (PrimeqProcessor& p) : proc (p)
    {
        using namespace pqui;
        for (auto* b : { &btA, &btB, &btX, &isA, &isB, &restart }) addAndMakeVisible (b);
        addAndMakeVisible (match); match.setToggleState (true, juce::dontSendNotification); match.setButtonText (T8 ("Igualar loudness (recomendado)"));
        btA.setButtonText (T8 ("A  \xE2\x80\x94 original")); btB.setButtonText (T8 ("B  \xE2\x80\x94 processado")); btX.setButtonText (T8 ("X  \xE2\x80\x94 ?"));
        isA.setButtonText (T8 ("X é A")); isB.setButtonText (T8 ("X é B")); restart.setButtonText (T8 ("Novo teste"));
        for (auto* b : { &btA, &btB, &btX }) b->setClickingTogglesState (false);
        btA.onClick = [this] { listen (1); }; btB.onClick = [this] { listen (2); }; btX.onClick = [this] { listen (s.xIsBNow() ? 2 : 1, true); };
        isA.onClick = [this] { answer (false); }; isB.onClick = [this] { answer (true); }; restart.onClick = [this] { begin(); };
        addAndMakeVisible (bandBox); bandBox.onChange = [this] { proc.abxBand.store (selectedBand()); if (isVisible()) begin(); }; fillBands();
        s.start (10, 1u); startTimerHz (10);       // NAO mexe no processador enquanto o painel esta escondido
    }
    ~AbxPanel() override { proc.abxSide.store (0); proc.abxComp.store (1.f); proc.abxBand.store (-1); }
    void setActive (bool on) { setVisible (on); if (on) { fillBands(); proc.abxBand.store (selectedBand()); begin(); } else { proc.abxSide.store (0); proc.abxComp.store (1.f); proc.abxBand.store (-1); } }
    int selectedBand() const { const int id = bandBox.getSelectedId(); return id >= 100 ? id - 100 : -1; }
    void selectBand (int b) { bandBox.setSelectedId (b >= 0 ? 100 + b : 1, juce::sendNotification); }
    void relabel() { fillBands(); refresh(); }
    // hooks para testes
    pq::AbxSession& session() { return s; }
    void listen (int side, bool isX = false) { proc.abxSide.store (side); onX = isX; heard = true; refresh(); }
    void answer (bool saysB) { if (! heard || s.finished()) return; s.answer (saysB); heard = false; onX = false; proc.abxSide.store (s.finished() ? 0 : 1); refresh(); }
    void begin() { s.start (10, (unsigned) juce::Time::currentTimeMillis()); heard = false; onX = false; proc.abxSide.store (1); refresh(); }
    bool isDone() const { return s.finished(); }

    void paint (juce::Graphics& g) override
    {
        using namespace pqui;
        auto a = getLocalBounds().toFloat(); g.setColour (kPanel); g.fillRoundedRectangle (a, 8.f); g.setColour (kLine); g.drawRoundedRectangle (a.reduced (0.5f), 8.f, 1.f);
        g.setColour (kText); g.setFont (fontOf (16.f, true)); g.drawText (T8 ("TESTE ABX CEGO"), 16, 10, 300, 22, juce::Justification::centredLeft);
        g.setColour (kDim); g.setFont (fontOf (12.f));
        g.drawFittedText (T8 ("Toque o áudio em loop. Ouça A (original) e B (processado) as vezes que quiser; depois ouça X e diga se é A ou B. "
                              "O X é sorteado em segredo a cada tentativa. 10 tentativas dão uma conclusão estatística fiável."), 16, 36, getWidth() - 32, 44, juce::Justification::topLeft, 3);
        g.setColour (kTeal); g.setFont (fontOf (13.f, true));
        g.drawText (s.finished() ? T8 ("Teste concluído") : (T8 ("Tentativa ") + juce::String (s.current() + 1) + " / " + juce::String (s.trials())), 16, 88, 320, 20, juce::Justification::centredLeft);
        g.setColour (kText); g.setFont (fontOf (12.f));
        const juce::String st = s.finished() ? juce::String (juce::CharPointer_UTF8 (s.verdict().c_str()))
                                              : (s.answered() > 0 ? juce::String (s.correct()) + " / " + juce::String (s.answered()) + T8 (" certas até agora") : juce::String());
        g.drawFittedText (st, 16, 110, getWidth() - 32, 40, juce::Justification::topLeft, 3);
        g.setColour (kDim); g.drawText (T8 ("Diferença de nível medida: ") + juce::String (proc.loudDeltaDb.load(), 1) + " dB" + (match.getToggleState() ? T8 ("  (compensada em B)") : T8 ("  (NÃO compensada)")), 16, getHeight() - 26, getWidth() - 32, 18, juce::Justification::centredLeft);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (16); r.removeFromTop (150);
        auto row = r.removeFromTop (34); const int w = (row.getWidth() - 20) / 3; btA.setBounds (row.removeFromLeft (w)); row.removeFromLeft (10); btB.setBounds (row.removeFromLeft (w)); row.removeFromLeft (10); btX.setBounds (row);
        r.removeFromTop (8); auto r2 = r.removeFromTop (30); isA.setBounds (r2.removeFromLeft (w)); r2.removeFromLeft (10); isB.setBounds (r2.removeFromLeft (w)); r2.removeFromLeft (10); restart.setBounds (r2);
        bandBox.setBounds (16, 244, getWidth() - 32, 24);
        match.setBounds (getLocalBounds().reduced (16).removeFromBottom (48).removeFromTop (22));
    }
private:
    void timerCallback() override
    {
        if (! isVisible()) return;
        // compensacao: B (processado) ganha -delta para igualar o nivel; so quando ha sinal medido
        if (selectedBand() >= 0)    // ABX por banda: A e B sao ambos processados; iguala-se o nivel B ao de A (medidos pelo processador)
        {
            const float la = proc.abxLvlDb[0].load(), lb = proc.abxLvlDb[1].load();
            proc.abxComp.store ((match.getToggleState() && la > -100.f && lb > -100.f) ? std::pow (10.f, juce::jlimit (-9.f, 9.f, la - lb) / 20.f) : 1.f);
        }
        else { const float d = proc.loudDeltaDb.load(); proc.abxComp.store (match.getToggleState() ? std::pow (10.f, -juce::jlimit (-12.f, 12.f, d) / 20.f) : 1.f); }
        repaint();
    }
    void refresh()
    {
        const bool fin = s.finished(); const bool bm = selectedBand() >= 0;
        btA.setButtonText (bm ? pqui::T8 ("A  \xE2\x80\x94 sem a banda") : pqui::T8 ("A  \xE2\x80\x94 original")); btB.setButtonText (bm ? pqui::T8 ("B  \xE2\x80\x94 com a banda") : pqui::T8 ("B  \xE2\x80\x94 processado"));
        btA.setEnabled (! fin); btB.setEnabled (! fin); btX.setEnabled (! fin); isA.setEnabled (! fin && heard); isB.setEnabled (! fin && heard);
        const int side = proc.abxSide.load(); btA.setToggleState (! onX && side == 1, juce::dontSendNotification); btB.setToggleState (! onX && side == 2, juce::dontSendNotification); btX.setToggleState (onX, juce::dontSendNotification);
        repaint();
    }
    void fillBands()
    {
        const int keep = bandBox.getSelectedId(); bandBox.clear (juce::dontSendNotification); bandBox.addItem (pqui::T8 ("Todas as bandas (original vs processado)"), 1);
        pq::BandParams a[pq::kNumBands]; proc.readBands (a);
        for (int i = 0; i < pq::kNumBands; ++i) if (a[i].on) bandBox.addItem (pqui::T8 ("Só a banda ") + juce::String (i + 1) + ":  " + juce::String (juce::CharPointer_UTF8 (pq::studio::bandSummary (a[i], pqui::i18n::isEn()).c_str())), 100 + i);
        bandBox.setSelectedId (bandBox.indexOfItemId (keep) >= 0 ? keep : 1, juce::dontSendNotification);
    }
    juce::ComboBox bandBox;
    PrimeqProcessor& proc; pq::AbxSession s; bool heard = false, onX = false;
    juce::TextButton btA, btB, btX, isA, isB, restart; juce::ToggleButton match;
};
