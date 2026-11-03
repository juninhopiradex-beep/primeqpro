// =============================================================================
//  PRIMEQ PRO by Piradex — guia de primeiro arranque (6 passos). Aparece uma vez; reabre com o botao "?".
// =============================================================================
#pragma once
#include "LookAndFeel.h"

class OnboardingOverlay : public juce::Component
{
public:
    std::function<void()> onClose;
    OnboardingOverlay()
    {
        using namespace pqui;
        addAndMakeVisible (next); addAndMakeVisible (skip); next.onClick = [this] { if (++page >= kPages) close(); else refresh(); }; skip.onClick = [this] { close(); };
        skip.setButtonText (T8 ("Saltar")); refresh();
    }
    void open() { page = 0; refresh(); setVisible (true); toFront (true); }
    int pages() const { return kPages; }
    void gotoPage (int p) { page = juce::jlimit (0, kPages - 1, p); refresh(); }
    void paint (juce::Graphics& g) override
    {
        using namespace pqui;
        g.fillAll (juce::Colours::black.withAlpha (0.62f));
        auto card = getLocalBounds().withSizeKeepingCentre (560, 330).toFloat();
        g.setColour (kPanel); g.fillRoundedRectangle (card, 16.f); g.setColour (kLine); g.drawRoundedRectangle (card.reduced (0.5f), 16.f, 1.f);
        const auto& s = slide (page);
        g.setColour (kTeal); g.setFont (fontOf (12.f, true)); g.drawText (juce::String (page + 1) + " / " + juce::String (kPages), card.withTrimmedTop (16.f).withHeight (16.f).toNearestInt().withTrimmedLeft (24), juce::Justification::centredLeft);
        g.setColour (kText); g.setFont (fontOf (24.f, true)); g.drawText (T8 (s.title), card.withTrimmedTop (38.f).withHeight (36.f).toNearestInt().withTrimmedLeft (24), juce::Justification::centredLeft);
        g.setColour (kDim); g.setFont (fontOf (14.f)); g.drawFittedText (T8 (s.body), card.withTrimmedTop (86.f).withTrimmedBottom (70.f).toNearestInt().reduced (24, 0), juce::Justification::topLeft, 9);
        for (int i = 0; i < kPages; ++i) { g.setColour (i == page ? kTeal : kLine); g.fillEllipse (card.getX() + 26.f + (float) i * 14.f, card.getBottom() - 38.f, 7.f, 7.f); }
    }
    void resized() override
    {
        auto card = getLocalBounds().withSizeKeepingCentre (560, 330).reduced (20, 0); auto row = card.removeFromBottom (54).removeFromTop (34);
        next.setBounds (row.removeFromRight (120)); row.removeFromRight (8); skip.setBounds (row.removeFromRight (90));
    }
    void mouseDown (const juce::MouseEvent&) override {}      // bloqueia cliques no resto da UI enquanto aberto
private:
    struct Slide { const char* title; const char* body; };
    static constexpr int kPages = 6;
    static const Slide& slide (int i)
    {
        static const Slide s[kPages] = {
            { "Bem-vindo ao PRIMEQ PRO", "Equalizador paramétrico e dinâmico de 24 bandas.\n\n\xE2\x80\xA2 Duplo clique no gráfico: cria uma banda. Duplo clique numa banda: apaga.\n\xE2\x80\xA2 Arraste o ponto: frequência e ganho (Shift: só um eixo).\n\xE2\x80\xA2 Roda do rato sobre a banda: Q (nos filtros HP/LP, o declive).\n\xE2\x80\xA2 Clique direito: menu da banda.   Delete: apaga.   Ctrl/Cmd+Z: desfaz." },
            { "Três modos, o mesmo plugin", "Simple: só o essencial, ideal para começar e para decidir depressa.\nAdvanced: tudo, incluindo o Espaço e o Doutor do Assistente.\nExpert: acrescenta Cenários e o relatório exportável.\n\nMuda no menu junto ao BYPASS, a qualquer momento. As tuas definições não mudam, só o que está visível." },
            { "114 presets de fábrica", "No menu Presets: Vocals, Drums, Bass, Guitar, Keys, Synth, Orquestra, Mixbus, Master, Corretivo, Géneros (Kizomba, Semba, Kuduro, Amapiano, Afrobeat, Hip-Hop, Trap), Sends e Utility.\n\nSão pontos de partida: carrega, ouve e ajusta.\nA/B compara duas definições completas; Undo/Redo guarda o histórico." },
            { "Assistente: mistura em vez de uma pista", "Abre o PRIMEQ PRO em cada pista, liga ANALISAR no Assistente e toca a música.\n\nO Assistente mostra onde as pistas se mascaram, como estão no espaço estéreo, diagnostica sintomas (voz pouco clara, kick sem impacto...) e propõe cortes que podes aplicar e desfazer.\n\nO botão ZONAS desenha no gráfico principal onde a tua pista é mascarada (vermelho) e achados de espaço (violeta)." },
            { "Teste ABX cego", "O botão ABX compara original (A) e processado (B) com o loudness igualado, para o ouvido não ser enganado por \"mais alto = melhor\".\n\nEm cada tentativa ouves A, B e um X escondido, e dizes se X é A ou B. Ao fim de 10 tentativas vês quantas acertaste e se a diferença é estatisticamente audível." },
            { "Licença e ajuda", "Até 31/12/2026 o plugin abre sem senha. Depois, entra com a tua licença Piradex (validada online no PCC, com cache para trabalhar offline) ou com a senha master.\n\nManual completo: Docs/MANUAL.md. Para rever este guia, carrega no botão \"?\" no topo.\n\nBoa mistura!" }
        };
        return s[i];
    }
    void close() { setVisible (false); if (onClose) onClose(); }
    void refresh() { next.setButtonText (page == kPages - 1 ? "Começar" : "Seguinte"); repaint(); }
    int page = 0; juce::TextButton next, skip;
};
