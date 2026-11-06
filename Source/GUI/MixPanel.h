// =============================================================================
//  PRIMEQ PRO by Piradex — ASSISTENTE: pistas da sessao + 5 separadores
//    Masking (conflitos e comparacao EQ/Dinamica/Sidechain) · Espaco (mapa frequencia x panorama, mono safety, M/S)
//    Doutor (sintoma -> evidencias -> hipoteses -> acoes) · Intencao (Clarity/Punch/Warmth/Space/Smooth) · Cenarios (A/B/C com metricas)
//  Modos: Simple (Intencao + Masking) · Advanced (+ Espaco + Doutor) · Expert (+ Cenarios + relatorio)
// =============================================================================
#pragma once
#include "LookAndFeel.h"
#include "Overlay.h"
#include "../Plugin/PluginProcessor.h"
#include "../SmartOps/Consequence.h"

using pqui::T8;

class MixPanel : public juce::Component
{
public:
    enum Tab { TMask = 0, TSpace, TDoctor, TIntent, TScen };

    explicit MixPanel (PrimeqProcessor& p) : proc (p)
    {
        using namespace pqui;
        for (auto* b : { &analyseB, &clearB, &applyAllB, &intApplyB, &intRevertB, &autoGainB, &reportB }) addAndMakeVisible (b);
        for (auto& b : applyB) addAndMakeVisible (b);
        for (auto& b : actB) addAndMakeVisible (b);
        for (auto& b : scB) addAndMakeVisible (b);
        analyseB.setButtonText (T8 ("ANALISAR MIX")); clearB.setButtonText ("LIMPAR DADOS"); applyAllB.setButtonText (T8 ("APLICAR RECOMENDADO (todas as bandas)"));
        intApplyB.setButtonText (T8 ("APLICAR NESTA PISTA")); intRevertB.setButtonText ("REVERTER"); autoGainB.setButtonText (T8 ("IGUALAR LOUDNESS (Auto Gain)")); reportB.setButtonText (T8 ("EXPORTAR RELATÓRIO"));
        analyseB.onClick = [this] { analyse(); };
        clearB.onClick = [this] { for (auto& t : pq::MixRegistry::get().list (&proc)) pq::MixRegistry::get().clearFrames (t.slot); res = {}; space = {}; cons = {}; docR = {}; tracks.clear(); slotOf.clear(); status = T8 ("Dados limpos."); relayout(); repaint(); };
        applyAllB.onClick = [this] { applyRecommended(); };
        intApplyB.onClick = [this] { if (! plan.props.empty()) { const int n = proc.smartApply (plan.props, "Intent EQ"); status = juce::String (n) + T8 (" banda(s) criada(s) — são bandas normais do EQ, edite à vontade. REVERTER desfaz."); repaint(); } };
        intRevertB.onClick = [this] { proc.smartRevert(); status = T8 ("Revertido."); repaint(); };
        autoGainB.onClick = [this] { proc.setParamReal ("autogain", proc.getParamReal ("autogain") > 0.5f ? 0.f : 1.f); repaint(); };
        reportB.onClick = [this] { exportReport(); };
        for (int i = 0; i < 9; ++i) { const int ci = i / 3, m = i % 3; applyB[(size_t) i].onClick = [this, ci, m] { applyConflict (ci, m); }; }
        for (int i = 0; i < 6; ++i) { actB[(size_t) i].onClick = [this, i] { applyAction (i); }; }
        for (int i = 0; i < 3; ++i) { scB[(size_t) i].onClick = [this, i] { if (cons.sc.size() == 4) send (res.pairs[(size_t) selPair].victimIdx, cons.sc[(size_t) i + 1].props); }; }
        relabel();
        amount.setSliderStyle (juce::Slider::LinearHorizontal); amount.setRange (0, 100, 1); amount.setValue (50, juce::dontSendNotification); amount.setTextBoxStyle (juce::Slider::TextBoxRight, false, 54, 20); amount.setTextValueSuffix (" %");
        amount.setColour (juce::Slider::thumbColourId, kTeal); amount.setColour (juce::Slider::trackColourId, kTealDim); amount.setColour (juce::Slider::backgroundColourId, kLine);
        amount.onValueChange = [this] { refreshIntent(); }; addAndMakeVisible (amount);
        status = T8 ("Ligue \"ANALISAR\" em cada pista, toque o material e carregue em ANALISAR MIX."); role = pq::guessRole (proc.trackName().toStdString());
        refreshIntent(); relayout();
    }

    void tick() { if (++cnt % 15 == 0) { rows = visibleRows(); if (tab == TIntent && ownAnalysing()) refreshIntent(); repaint(); } if (modeSeen != proc.uiMode) { modeSeen = proc.uiMode; ensureTab(); relayout(); repaint(); } }
    void setTab (int t) { tab = (Tab) t; ensureTab(); if (tab == TIntent) refreshIntent(); relayout(); repaint(); }
    int  currentTab() const { return (int) tab; }
    void setSymptom (int s) { symptom = s; docR = pq::diagnose ((pq::Symptom) symptom, tracks, res, space); relayout(); repaint(); }
    void setIntent (int i, int r, int pct) { intent = i; role = (pq::Role) r; amount.setValue (pct, juce::dontSendNotification); refreshIntent(); repaint(); }
    void setPair (int i) { selPair = juce::jlimit (0, std::max (0, (int) res.pairs.size() - 1), i); selConf = 0; recomputePair(); relayout(); repaint(); }
    const pq::MixResult& result() const { return res; }
    const pq::SpaceResult& spaceResult() const { return space; }
    const pq::DocReport& doctor() const { return docR; }
    const pq::IntentPlan& intentPlan() const { return plan; }
    const pq::ConsequenceResult& consequences() const { return cons; }

    std::function<void (const std::vector<OverlayZone>&)> onZones;     // alimenta o grafico principal
    void analyse()
    {
        auto& reg = pq::MixRegistry::get(); rows = visibleRows(); tracks.clear(); slotOf.clear();
        for (const auto& t : rows) if (t.analysing)
        {
            pq::MixTrack mt; mt.name = t.name; mt.priority = t.priority; reg.copyFrames (t.slot, pq::kMixHist - 2, mt.frames); mt.st = reg.stats (t.slot);
            if (! mt.frames.empty()) { tracks.push_back (std::move (mt)); slotOf.push_back (t.slot); }
        }
        res = pq::analyseMix (tracks); selPair = 0; selConf = 0;
        space = res.curve.size() == tracks.size() && ! tracks.empty() ? pq::analyseSpace (tracks, res) : pq::SpaceResult {};
        docR = pq::diagnose ((pq::Symptom) symptom, tracks, res, space); recomputePair(); refreshIntent();
        if (! res.message.empty()) status = juce::String (juce::CharPointer_UTF8 (res.message.c_str()));
        else if (res.pairs.empty()) status = T8 ("Sem conflitos relevantes entre as pistas analisadas. Boa separação.");
        else status = juce::String ((int) res.pairs.size()) + T8 (" par(es) com mascaramento · ") + juce::String ((int) space.findings.size()) + T8 (" achado(s) espaciais.");
        publishZones(); relayout(); repaint();
    }
    // zonas desta instancia: onde a sua pista e mascarada (vermelho) e achados espaciais (violeta)
    void publishZones()
    {
        if (! onZones) return;
        std::vector<OverlayZone> z; int me = -1; for (size_t i = 0; i < slotOf.size(); ++i) if (slotOf[i] == proc.mixSlot()) me = (int) i;
        if (me >= 0)
        {
            for (const auto& pr : res.pairs) if (pr.victimIdx == me)
                for (const auto& c : pr.conflicts) { const double hw = std::max (0.12, 0.75 / std::max (1.0, c.q)); OverlayZone o; o.f0 = c.freq * std::pow (2.0, -hw); o.f1 = c.freq * std::pow (2.0, hw); o.severity = (float) std::min (1.0, c.depthDb / 12.0); o.kind = OverlayZone::Masking; o.label = juce::String (tracks[(size_t) pr.protectedIdx].name) + " " + juce::String (juce::roundToInt (c.depthDb)) + " dB"; z.push_back (o); }
            for (const auto& f : space.findings) if (f.track == me && f.kind != pq::SpaceFinding::MonoGlobal)
            { OverlayZone o; o.f0 = pq::mixBandFreq (f.bandLo) / 1.12; o.f1 = pq::mixBandFreq (f.bandHi) * 1.12; o.severity = 0.5f; o.kind = OverlayZone::Space; o.label = f.kind == pq::SpaceFinding::MonoRisk ? "mono" : (f.kind == pq::SpaceFinding::MidCut ? "mid" : "centro"); z.push_back (o); }
        }
        onZones (z);
    }
    juce::String buildReport() const
    {
        juce::String r; const auto nl = "\n";
        r << "# PRIMEQ PRO by Piradex - relatorio do Assistente" << nl << nl << "Estimativas sobre niveis medidos (25 ms) e estatisticas L/R. Nao substitui a escuta." << nl << nl << "## Pistas" << nl;
        for (size_t i = 0; i < tracks.size(); ++i) r << "- " << tracks[i].name << " | prioridade " << tracks[i].priority << " | " << pq::srcKindName (i < space.kind.size() ? space.kind[i] : pq::SrcKind::Stereo) << " | corr " << juce::String (tracks[i].st.corrAll, 2) << " | perda mono " << juce::String (-tracks[i].st.monoLossAll, 1) << " dB | crista " << juce::String (tracks[i].st.crestDb, 1) << " dB" << nl;
        r << nl << "## Masking" << nl; if (! res.message.empty()) r << res.message << nl;
        for (const auto& p : res.pairs) { r << "### " << nameOf (p.protectedIdx) << " (protegida) -> " << nameOf (p.victimIdx) << " | melhor metodo: " << pq::methodLabel (p.best) << nl;
            for (const auto& c : p.conflicts) r << "- " << juce::String ((int) std::lround (c.freq)) << " Hz, mascaramento " << juce::String (c.depthDb, 1) << " dB, corte sugerido " << juce::String (c.cutDb, 1) << " dB | EQ " << juce::String (100 * c.eq.score, 0) << "% | Dinamica " << juce::String (100 * c.dyn.score, 0) << "% | Sidechain " << juce::String (100 * c.sc.score, 0) << "%" << nl; }
        r << nl << "## Espaco" << nl; for (const auto& f : space.findings) r << "- " << juce::String (juce::CharPointer_UTF8 (f.text.c_str())) << nl;
        r << nl << "## Doutor: " << juce::String (juce::CharPointer_UTF8 (pq::symptomName ((pq::Symptom) symptom))) << nl; for (const auto& e : docR.evidence) r << "- Evidencia: " << juce::String (juce::CharPointer_UTF8 (e.c_str())) << nl;
        for (const auto& h : docR.hyps) r << "- Hipotese (" << juce::String (juce::CharPointer_UTF8 (pq::verdictName (h.v))) << "): " << juce::String (juce::CharPointer_UTF8 (h.text.c_str())) << nl;
        if (cons.sc.size() == 4) { r << nl << "## Cenarios" << nl; for (const auto& s : cons.sc) r << "- " << juce::String (juce::CharPointer_UTF8 (s.name.c_str())) << ": masking restante " << juce::String (s.maskRemainPct, 0) << "%, nivel da soma " << juce::String (s.mixRmsDb, 2) << " dB, corte desnecessario " << juce::String (s.collateralPct, 0) << "%" << nl; }
        r << nl << "## Limitacoes" << nl << T8 ("- So ve instancias no mesmo processo e grupo. O plugin nao tem panner: pan candidato e sugestao. Nao mede reverb, dicção ou preferencia auditiva.") << nl;
        return r;
    }

    void relabel()
    {
        for (int i = 0; i < 9; ++i) { const int m = i % 3; applyB[(size_t) i].setButtonText (m == 0 ? T8 ("Aplicar EQ") : (m == 1 ? T8 ("Aplicar Dinâmica") : T8 ("Aplicar Sidechain"))); }
        for (int i = 0; i < 6; ++i) actB[(size_t) i].setButtonText (T8 ("Aplicar"));
        for (int i = 0; i < 3; ++i) scB[(size_t) i].setButtonText (T8 ("Aplicar ") + juce::String::charToString ("ABC"[i]));
        repaint();
    }
    void resized() override { relayout(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto p = e.getPosition(); auto& reg = pq::MixRegistry::get();
        if (groupChip().contains (p)) { reg.setGroup (proc.mixSlot(), reg.group (proc.mixSlot()) % 4 + 1); rows = visibleRows(); repaint(); return; }
        for (size_t i = 0; i < rows.size() && i < 9; ++i)
        {
            const auto r = rowRect ((int) i); if (! r.contains (p)) continue; const auto& t = rows[i];
            if (chipPri (r).contains (p)) reg.setPriority (t.slot, e.mods.isRightButtonDown() ? (t.priority <= 1 ? 5 : t.priority - 1) : (t.priority >= 5 ? 1 : t.priority + 1));
            else if (chipOn (r).contains (p)) reg.setAnalysing (t.slot, ! t.analysing, t.sampleRate);
            else if (p.x < chipPri (r).getX()) rename (t.slot, t.name);
            rows = visibleRows(); repaint(); return;
        }
        { const auto tr = tabRects(); for (size_t i = 0; i < tr.size(); ++i) if (tr[i].second.contains (p)) { setTab (tr[i].first); return; } }
        if (tab == TMask || tab == TScen) { const auto pc = pairChipRects(); for (size_t i = 0; i < pc.size(); ++i) if (pc[i].contains (p)) { setPair ((int) i); return; } }
        if (tab == TScen) { for (int i = 0; i < 3; ++i) if (confChip (i).contains (p) && res.pairs.size() > (size_t) selPair && (size_t) i < res.pairs[(size_t) selPair].conflicts.size()) { selConf = i; recomputePair(); repaint(); return; } }
        if (tab == TDoctor) for (int i = 0; i < 5; ++i) if (symChip (i).contains (p)) { setSymptom (i); return; }
        if (tab == TIntent)
        {
            for (int i = 0; i < 5; ++i) if (intentChip (i).contains (p)) { intent = i; refreshIntent(); repaint(); return; }
            for (int i = 0; i < 4; ++i) if (roleChip (i).contains (p)) { role = (pq::Role) i; refreshIntent(); repaint(); return; }
        }
    }

    void paint (juce::Graphics& g) override
    {
        using namespace pqui;
        g.setColour (kPanel); g.fillRoundedRectangle (leftArea().toFloat(), 8.f); g.fillRoundedRectangle (rightArea().toFloat(), 8.f);
        paintTracks (g); paintTabs (g);
        switch (tab) { case TMask: paintMask (g); break; case TSpace: paintSpace (g); break; case TDoctor: paintDoctor (g); break; case TIntent: paintIntent (g); break; default: paintScen (g); break; }
    }

private:
    PrimeqProcessor& proc; juce::TextButton analyseB, clearB, applyAllB, intApplyB, intRevertB, autoGainB, reportB; std::array<juce::TextButton, 9> applyB; std::array<juce::TextButton, 6> actB; std::array<juce::TextButton, 3> scB; juce::Slider amount;
    std::vector<pq::MixTrackInfo> rows; std::vector<pq::MixTrack> tracks; std::vector<int> slotOf; pq::MixResult res; pq::SpaceResult space; pq::DocReport docR; pq::IntentPlan plan; pq::ConsequenceResult cons;
    Tab tab = TMask; int selPair = 0, selConf = 0, cnt = 0, symptom = 0, intent = 0, modeSeen = -1; pq::Role role = pq::Role::General; juce::String status; std::unique_ptr<juce::FileChooser> chooser;
    std::vector<std::vector<pq::DeresProposal>> actionProps; std::vector<int> actionTargets; std::vector<juce::Rectangle<int>> actionRects;

    // ---------- geometria
    juce::Rectangle<int> leftArea() const { return { 0, 0, 330, getHeight() }; }
    juce::Rectangle<int> rightArea() const { return { 338, 0, getWidth() - 338, getHeight() }; }
    juce::Rectangle<int> content() const { return rightArea().withTrimmedTop (44).reduced (12, 6); }
    juce::Rectangle<int> groupChip() const { return { 250, 8, 70, 22 }; }
    juce::Rectangle<int> rowRect (int i) const { return { 10, 50 + i * 52, 310, 48 }; }
    static juce::Rectangle<int> chipPri (juce::Rectangle<int> r) { return { r.getRight() - 128, r.getY() + 12, 34, 24 }; }
    static juce::Rectangle<int> chipOn (juce::Rectangle<int> r) { return { r.getRight() - 88, r.getY() + 12, 82, 24 }; }
    std::vector<std::pair<int, juce::Rectangle<int>>> tabRects() const
    {
        std::vector<std::pair<int, juce::Rectangle<int>>> v; auto r = rightArea().withHeight (40).reduced (12, 8); static const int w[5] = { 112, 112, 112, 112, 112 };
        for (int t = 0; t < 5; ++t) if (tabAvailable (t)) { v.push_back ({ t, r.removeFromLeft (w[t]) }); r.removeFromLeft (6); }
        return v;
    }
    bool tabAvailable (int t) const { const int m = proc.uiMode; return m == 2 || (m == 1 && t != TScen) || (m == 0 && (t == TMask || t == TIntent)); }
    void ensureTab() { if (! tabAvailable ((int) tab)) tab = TIntent; }
    static const char* tabName (int t) { static const char* n[5] = { "MASKING", "ESPA\xC3\x87O", "DOUTOR", "INTEN\xC3\x87\xC3\x83O", "CEN\xC3\x81RIOS" }; return n[t]; }
    std::vector<pq::MixTrackInfo> visibleRows() const { std::vector<pq::MixTrackInfo> v; auto& reg = pq::MixRegistry::get(); const int g = reg.group (proc.mixSlot()); for (auto& t : reg.list (&proc)) if (t.group == g) v.push_back (t); return v; }
    bool ownAnalysing() const { return pq::MixRegistry::get().analysing (proc.mixSlot()); }
    juce::Rectangle<int> pairRow() const { auto r = content(); r.removeFromTop (236); return r.removeFromTop (28); }
    std::vector<juce::Rectangle<int>> pairChipRects() const
    {
        std::vector<juce::Rectangle<int>> v; auto r = tab == TScen ? content().removeFromTop (28) : pairRow(); for (size_t i = 0; i < res.pairs.size() && i < 5; ++i) { v.push_back (r.removeFromLeft (150)); r.removeFromLeft (6); } return v;
    }
    juce::Rectangle<int> maskCard (int i) const { auto r = content(); r.removeFromTop (236 + 34 + i * 120); return r.removeFromTop (114); }
    juce::Rectangle<int> maskGuide() const { auto r = content(); r.removeFromTop (236 + 34 + 3 * 120); return r; }
    juce::Rectangle<int> symChip (int i) const { auto r = content().removeFromTop (30); r.removeFromLeft (i * 174); return r.removeFromLeft (168); }
    juce::Rectangle<int> intentChip (int i) const { auto r = content().removeFromTop (34); r.removeFromLeft (i * 150); return r.removeFromLeft (144); }
    juce::Rectangle<int> roleChip (int i) const { auto r = content(); r.removeFromTop (56); r = r.removeFromTop (28); r.removeFromLeft (i * 100); return r.removeFromLeft (94); }
    juce::Rectangle<int> confChip (int i) const { auto r = content(); r.removeFromTop (34); r = r.removeFromTop (28); r.removeFromLeft (i * 150); return r.removeFromLeft (144); }
    juce::String nameOf (int idx) const { return idx >= 0 && idx < (int) tracks.size() ? juce::String (tracks[(size_t) idx].name) : juce::String(); }
    static juce::Colour trackColour (int i) { static const juce::uint32 c[6] = { 0xffe07a8c, 0xff63b3ed, 0xff9ccc65, 0xffe0a458, 0xff9b8cf0, 0xff4fd1c5 }; return juce::Colour (c[i % 6]); }
    static juce::String methodName (int m) { return m == 0 ? juce::String ("EQ") : (m == 1 ? juce::String (juce::CharPointer_UTF8 ("DIN\xC3\x82MICA")) : juce::String ("SIDECHAIN")); }
    static juce::String U (const std::string& s) { return juce::String (juce::CharPointer_UTF8 (s.c_str())); }

    void relayout()
    {
        auto l = leftArea().reduced (10, 0); analyseB.setBounds (l.getX(), getHeight() - 82, l.getWidth(), 32); clearB.setBounds (l.getX(), getHeight() - 44, l.getWidth(), 28);
        auto vis = [] (juce::Component& c, bool v) { c.setVisible (v); };
        for (auto& b : applyB) vis (b, false); for (auto& b : actB) vis (b, false); for (auto& b : scB) vis (b, false);
        for (auto* c : std::initializer_list<juce::Component*> { &applyAllB, &intApplyB, &intRevertB, &autoGainB, &reportB, &amount }) vis (*c, false);
        const bool has = ! res.pairs.empty() && selPair < (int) res.pairs.size();
        if (tab == TMask)
        {
            const int nC = has ? (int) res.pairs[(size_t) selPair].conflicts.size() : 0;
            for (int c = 0; c < 3; ++c) { auto row = maskCard (c).reduced (10, 0).removeFromBottom (30); for (int m = 0; m < 3; ++m) { auto& b = applyB[(size_t) (c * 3 + m)]; b.setBounds (row.removeFromLeft (150)); row.removeFromLeft (8); vis (b, c < nC); } }
            auto g = maskGuide(); applyAllB.setBounds (g.removeFromBottom (32).removeFromRight (360)); vis (applyAllB, has && nC > 0);
        }
        else if (tab == TSpace || tab == TDoctor)
        {
            // acoes: lista de propostas com botao (posicao = cartao/linha onde aparecem)
            actionProps.clear(); actionTargets.clear(); actionRects.clear(); const auto c0 = content();
            if (tab == TSpace) { int k = 0; for (const auto& f : space.findings) { if (k >= 5) break; if (! f.props.empty() && actionProps.size() < 6) { actionProps.push_back (f.props); actionTargets.push_back (f.track); actionRects.push_back ({ c0.getRight() - 90, c0.getY() + 20 + k * 76 + 36, 84, 26 }); } ++k; } }
            else for (size_t i = 0; i < docR.actions.size() && i < 6; ++i) { const auto& a = docR.actions[i]; if (a.props.empty()) continue; actionProps.push_back (a.props); actionTargets.push_back (a.track); actionRects.push_back ({ c0.getRight() - 96, c0.getY() + 376 + (int) i * 40 + 5, 88, 26 }); }
            for (size_t i = 0; i < actionProps.size(); ++i) { actB[i].setBounds (actionRects[i]); vis (actB[i], true); }
        }
        else if (tab == TIntent)
        {
            auto c = content(); c.removeFromTop (34 + 22 + 28 + 8); amount.setBounds (c.removeFromTop (26).withTrimmedLeft (120).withWidth (420));
            vis (amount, true); const int y = content().getY() + 456;
            intApplyB.setBounds (content().getX(), y + 0, 210, 32); intRevertB.setBounds (content().getX() + 218, y, 110, 32); autoGainB.setBounds (content().getX() + 336, y, 250, 32); vis (intApplyB, ! plan.props.empty()); vis (intRevertB, true); vis (autoGainB, true);
        }
        else
        {
            for (int i = 0; i < 3; ++i) { scB[(size_t) i].setBounds (scenCol (i + 1).withY (content().getY() + 34 + 28 + 10 + 8 * 24 + 8).withHeight (30)); vis (scB[(size_t) i], cons.sc.size() == 4); }
            reportB.setBounds (content().getRight() - 220, getHeight() - 52, 220, 30); vis (reportB, true);
        }
    }
    juce::Rectangle<int> scenCol (int c) const { auto r = content(); return { r.getX() + 230 + c * 150, r.getY(), 140, 24 }; }

    // ---------- acoes
    void rename (int slot, const std::string& cur)
    {
        auto* w = new juce::AlertWindow (T8 ("Nome da pista"), T8 ("Nome desta pista (ex.: Kick, Bass, Vocals):"), juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("n", juce::String (cur), ""); w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey)); w->addButton (T8 ("Cancelar"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, slot] (int r)
        {
            if (r != 1) return; const auto n = w->getTextEditorContents ("n").trim(); if (n.isEmpty()) return;
            if (slot == proc.mixSlot()) { proc.setTrackName (n, true); role = pq::guessRole (n.toStdString()); refreshIntent(); } else { pq::MixRegistry::get().setName (slot, n.toStdString()); pq::MixRegistry::get().setPriority (slot, pq::guessPriority (n.toStdString())); }
            rows = visibleRows(); repaint();
        }), true);
    }
    void send (int victimIdx, const std::vector<pq::DeresProposal>& props)
    {
        if (victimIdx < 0 || victimIdx >= (int) slotOf.size() || props.empty()) return;
        pq::MixMail m; m.props = props; m.undoName = "Assistente"; pq::MixRegistry::get().post (slotOf[(size_t) victimIdx], m);
        status = T8 ("Enviado para \"") + nameOf (victimIdx) + T8 ("\": ") + juce::String ((int) props.size()) + T8 (" banda(s). Em Smart Ops › Revert nessa pista desfaz."); repaint();
    }
    static const pq::DeresProposal& propOf (const pq::MixConflict& c, int m) { return m == 0 ? c.pEq : (m == 1 ? c.pDyn : c.pSc); }
    void applyConflict (int ci, int m)
    {
        if (selPair >= (int) res.pairs.size()) return; const auto& pr = res.pairs[(size_t) selPair]; if (ci >= (int) pr.conflicts.size()) return;
        send (pr.victimIdx, { propOf (pr.conflicts[(size_t) ci], m) });
    }
    void applyRecommended() { if (selPair >= (int) res.pairs.size()) return; const auto& pr = res.pairs[(size_t) selPair]; std::vector<pq::DeresProposal> v; for (const auto& c : pr.conflicts) v.push_back (propOf (c, (int) pr.best)); send (pr.victimIdx, v); }
    void applyAction (int i) { if (i < (int) actionProps.size()) send (actionTargets[(size_t) i], actionProps[(size_t) i]); }
    void recomputePair()
    {
        cons = {}; if (res.pairs.empty() || selPair >= (int) res.pairs.size()) return; const auto& pr = res.pairs[(size_t) selPair];
        cons = pq::runConsequences (tracks[(size_t) pr.protectedIdx], tracks[(size_t) pr.victimIdx], pr);
    }
    bool ownCurve (std::array<double, pq::kMixBands>& out) const
    {
        std::vector<std::array<float, pq::kMixBands>> fr; if (pq::MixRegistry::get().copyFrames (proc.mixSlot(), pq::kMixHist - 2, fr) < 80) return false;
        for (int b = 0; b < pq::kMixBands; ++b) { std::vector<double> a; for (auto& f : fr) if (f[(size_t) b] > -65.f) a.push_back (f[(size_t) b]); out[(size_t) b] = a.size() >= 10 ? pq::percentileOf (a, 0.5) : -120.0; }
        return true;
    }
    void refreshIntent()
    {
        std::array<double, pq::kMixBands> c {}; const bool have = ownAnalysing() && ownCurve (c);
        const pq::MixConflict* conf = nullptr; if (have && ! res.pairs.empty()) for (const auto& pr : res.pairs) if (pr.victimIdx >= 0 && pr.victimIdx < (int) slotOf.size() && slotOf[(size_t) pr.victimIdx] == proc.mixSlot() && ! pr.conflicts.empty()) { conf = &pr.conflicts[0]; break; }
        plan = pq::planIntent ((pq::Intent) intent, role, amount.getValue() / 100.0, have ? &c : nullptr, conf); curve = c; haveCurve = have;
    }
    std::array<double, pq::kMixBands> curve {}; bool haveCurve = false;
    void exportReport()
    {
        chooser = std::make_unique<juce::FileChooser> (T8 ("Guardar relatorio"), juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("PRIMEQpro_relatorio.md"), "*.md");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting, [this] (const juce::FileChooser& fc)
        { auto f = fc.getResult(); if (f != juce::File()) { f.replaceWithText (buildReport()); status = T8 ("Relatório guardado em ") + f.getFullPathName(); repaint(); } });
    }

    // ---------- pintura comum
    void txt (juce::Graphics& g, const juce::String& s, juce::Rectangle<int> r, float h, juce::Colour c, bool bold = false, juce::Justification j = juce::Justification::centredLeft) { g.setColour (c); g.setFont (pqui::fontOf (h, bold)); g.drawText (pqui::tr (s), r, j); }
    void wrap (juce::Graphics& g, const juce::String& s, juce::Rectangle<int> r, float h, juce::Colour c, int lines = 3) { g.setColour (c); g.setFont (pqui::fontOf (h)); g.drawFittedText (s, r, juce::Justification::topLeft, lines, 1.f); }
    void paintTracks (juce::Graphics& g)
    {
        using namespace pqui; txt (g, T8 ("PISTAS NA SESSÃO"), { 14, 8, 220, 22 }, 13.f, kText, true);
        auto gc = groupChip (); g.setColour (kLine); g.fillRoundedRectangle (gc.toFloat(), 5.f); txt (g, T8 ("GRUPO G") + juce::String (pq::MixRegistry::get().group (proc.mixSlot())), gc, 10.f, kTeal, true, juce::Justification::centred);
        txt (g, T8 ("nome: clique · P = prioridade (quem é protegido) · só pistas do mesmo grupo"), { 14, 30, 310, 14 }, 9.5f, kDim);
        for (size_t i = 0; i < rows.size() && i < 9; ++i)
        {
            const auto& t = rows[i]; const auto r = rowRect ((int) i); g.setColour (kPanel2); g.fillRoundedRectangle (r.toFloat(), 6.f);
            int ti = -1; for (size_t k = 0; k < slotOf.size(); ++k) if (slotOf[k] == t.slot) ti = (int) k;
            g.setColour (ti >= 0 ? trackColour (ti) : kDim); g.fillRoundedRectangle ((float) r.getX(), (float) r.getY() + 6, 4.f, (float) r.getHeight() - 12, 2.f);
            txt (g, juce::String (t.name) + (t.isSelf ? T8 ("  (esta pista)") : juce::String()), { r.getX() + 14, r.getY() + 5, r.getWidth() - 160, 20 }, 13.f, kText, true);
            txt (g, t.analysing ? (juce::String (std::min<int> (t.head, pq::kMixHist - 2) / 40.0, 1) + T8 (" s medidos · pico ") + juce::String ((int) std::lround (std::max (-120.f, t.lastDb))) + " dBFS") : T8 ("análise desligada"), { r.getX() + 14, r.getY() + 26, r.getWidth() - 160, 16 }, 10.f, kDim);
            auto cp = chipPri (r); g.setColour (kLine); g.fillRoundedRectangle (cp.toFloat(), 5.f); txt (g, "P" + juce::String (t.priority), cp, 11.f, kText, true, juce::Justification::centred);
            auto co = chipOn (r); g.setColour (t.analysing ? kTealDim : kLine); g.fillRoundedRectangle (co.toFloat(), 5.f); txt (g, t.analysing ? "A ANALISAR" : "ANALISAR", co, 10.f, t.analysing ? kTeal : kDim, true, juce::Justification::centred);
        }
        if (rows.empty()) txt (g, T8 ("(sem pistas — abra o PRIMEQ PRO em mais pistas)"), { 14, 62, 300, 20 }, 11.f, kDim);
        wrap (g, T8 ("Só vê instâncias no mesmo processo do anfitrião. A medição é opt-in (usa CPU)."), { 14, getHeight() - 132, 304, 40 }, 10.f, kDim);
        const auto ss = status; wrap (g, ss, { 14, getHeight() - 182, 304, 46 }, 10.f, kText, 4);
    }
    void paintTabs (juce::Graphics& g)
    {
        using namespace pqui; for (const auto& t : tabRects()) { const bool sel = t.first == (int) tab; g.setColour (sel ? kTealDim : kLine); g.fillRoundedRectangle (t.second.toFloat(), 6.f); txt (g, T8 (tabName (t.first)), t.second, 11.f, sel ? kTeal : kDim, true, juce::Justification::centred); }
        txt (g, proc.uiMode == 0 ? "MODO SIMPLE" : (proc.uiMode == 1 ? "MODO ADVANCED" : "MODO EXPERT"), rightArea().withHeight (40).reduced (14, 8), 10.f, kDim, false, juce::Justification::centredRight);
    }
    static float fxHz (double f, juce::Rectangle<float> r) { return r.getX() + (float) (std::log (f / 80.0) / std::log (12000.0 / 80.0)) * r.getWidth(); }

    // ---------- MASKING
    void paintMask (juce::Graphics& g)
    {
        using namespace pqui; auto pr = content().removeFromTop (230).toFloat(); auto inner = pr.withTrimmedLeft (34).withTrimmedBottom (18).withTrimmedTop (22);
        g.setColour (kPanel2); g.fillRoundedRectangle (pr, 6.f); txt (g, T8 ("ESPECTRO POR PISTA (mediana quando toca) — zonas vermelhas = mascaramento"), { (int) pr.getX() + 10, (int) pr.getY() + 3, 700, 18 }, 12.f, kText, true);
        for (double f : { 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 }) { const float x = fxHz (f, inner); g.setColour (kGrid); g.drawVerticalLine ((int) x, inner.getY(), inner.getBottom()); txt (g, f >= 1000 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f), { (int) x - 18, (int) inner.getBottom() + 2, 36, 12 }, 9.f, kDim, false, juce::Justification::centred); }
        for (double d : { 0.0, -20.0, -40.0, -60.0 }) { const float y = inner.getY() + (float) (-d / 70.0) * inner.getHeight(); g.setColour (kGrid); g.drawHorizontalLine ((int) y, inner.getX(), inner.getRight()); txt (g, juce::String ((int) d), { (int) pr.getX() + 2, (int) y - 6, 30, 12 }, 9.f, kDim, false, juce::Justification::centredRight); }
        if (selPair < (int) res.pairs.size() && ! res.pairs.empty()) for (const auto& c : res.pairs[(size_t) selPair].conflicts)
        { const float x0 = fxHz (c.freq / 1.3, inner), x1 = fxHz (c.freq * 1.3, inner); g.setColour (kRed.withAlpha (0.20f)); g.fillRect (x0, inner.getY(), x1 - x0, inner.getHeight()); txt (g, juce::String ((int) std::lround (c.freq)) + " Hz", { (int) (x0 + x1) / 2 - 30, (int) inner.getY() + 1, 60, 12 }, 10.f, kRed, true, juce::Justification::centred); }
        int lx = (int) pr.getRight() - 8;
        for (int t = (int) res.curve.size() - 1; t >= 0; --t)
        {
            const auto col = trackColour (t); juce::Path p, fill; bool open = false; float lastX = 0;
            for (int b = 0; b < pq::kMixBands; ++b)
            {
                const double v = res.curve[(size_t) t][(size_t) b]; if (v < -119) { if (open) { fill.lineTo (lastX, inner.getBottom()); fill.closeSubPath(); open = false; } continue; }
                const float x = fxHz (pq::mixBandFreq (b), inner), y = inner.getY() + (float) juce::jlimit (0.0, 1.0, -v / 70.0) * inner.getHeight();
                if (! open) { p.startNewSubPath (x, y); fill.startNewSubPath (x, inner.getBottom()); fill.lineTo (x, y); open = true; } else { p.lineTo (x, y); fill.lineTo (x, y); } lastX = x;
            }
            if (open) { fill.lineTo (lastX, inner.getBottom()); fill.closeSubPath(); }
            g.setColour (col.withAlpha (0.14f)); g.fillPath (fill); g.setColour (col); g.strokePath (p, juce::PathStrokeType (2.f));
            const auto nm = nameOf (t); const int w = juce::GlyphArrangement::getStringWidthInt (fontOf (11.f, true), nm) + 22; lx -= w; g.setColour (col); g.fillEllipse ((float) lx, pr.getY() + 8, 8.f, 8.f); txt (g, nm, { lx + 12, (int) pr.getY() + 3, w - 12, 18 }, 11.f, kText, true);
        }
        if (res.pairs.empty()) { wrap (g, status, content().withTrimmedTop (250).withHeight (60), 12.f, kDim); return paintMaskGuide (g, nullptr); }
        const auto chips = pairChipRects();
        for (size_t i = 0; i < chips.size(); ++i) { const auto& p = res.pairs[i]; const bool sel = (int) i == selPair; g.setColour (sel ? kTealDim : kLine); g.fillRoundedRectangle (chips[i].toFloat(), 6.f); txt (g, nameOf (p.protectedIdx) + juce::String (juce::CharPointer_UTF8 (" \xE2\x86\x92 ")) + nameOf (p.victimIdx), chips[i], 11.f, sel ? kTeal : kDim, true, juce::Justification::centred); }
        { auto r = pairRow(); r.removeFromLeft ((int) chips.size() * 156); txt (g, T8 ("protegida → cede · melhor método do par: ") + methodName ((int) res.pairs[(size_t) selPair].best), r, 10.f, kDim); }
        const auto& pr2 = res.pairs[(size_t) selPair];
        for (size_t ci = 0; ci < pr2.conflicts.size() && ci < 3; ++ci)
        {
            const auto& c = pr2.conflicts[ci]; auto cr = maskCard ((int) ci); g.setColour (kPanel2); g.fillRoundedRectangle (cr.toFloat(), 8.f); auto in = cr.reduced (12, 6);
            txt (g, juce::String ((int) std::lround (c.freq)) + T8 (" Hz · ") + T8 ("mascaramento médio ") + juce::String (c.depthDb, 1) + T8 (" dB · ") + T8 ("corte sugerido ") + juce::String (c.cutDb, 1) + " dB (Q " + juce::String (c.q, 1) + ")", in.removeFromTop (18), 13.f, kText, true);
            const pq::MixScore* sc[3] = { &c.eq, &c.dyn, &c.sc }; auto bars = in.removeFromTop (58);
            for (int m = 0; m < 3; ++m)
            {
                auto row = bars.removeFromTop (19); const bool best = m == (int) c.best; txt (g, methodName (m), row.removeFromLeft (84), 10.f, best ? kTeal : kDim, true);
                auto bar = row.removeFromLeft (340).reduced (0, 5).toFloat(); g.setColour (kLine); g.fillRoundedRectangle (bar, 3.f); g.setColour (best ? kTeal : kTealDim); g.fillRoundedRectangle (bar.withWidth (std::max (3.f, bar.getWidth() * (float) sc[m]->score)), 3.f);
                txt (g, juce::String ((int) std::lround (100 * sc[m]->score)) + T8 ("% · remove ") + juce::String ((int) std::lround (100 * sc[m]->benefit)) + T8 ("% · corte desnecessário ") + juce::String ((int) std::lround (100 * sc[m]->collateral)) + "%", row.withTrimmedLeft (8), 10.f, kText);
                if (best) txt (g, "RECOMENDADO", { cr.getRight() - 110, cr.getY() + 8, 100, 14 }, 10.f, kTeal, true, juce::Justification::centredRight);
            }
        }
        paintMaskGuide (g, &pr2);
    }
    void paintMaskGuide (juce::Graphics& g, const pq::MixPair* pr)
    {
        using namespace pqui; auto r = maskGuide(); r.removeFromBottom (36); if (r.getHeight() < 30) return; g.setColour (kPanel2); g.fillRoundedRectangle (r.toFloat(), 8.f); auto in = r.reduced (12, 8);
        txt (g, "COMO LIGAR NO DAW", in.removeFromTop (16), 12.f, kText, true);
        const juce::String P = pr ? nameOf (pr->protectedIdx) : juce::String ("protegida"), V = pr ? nameOf (pr->victimIdx) : juce::String ("cede");
        wrap (g, T8 ("EQ e Dinâmica: sem routing — o corte é feito na pista \"") + V + T8 ("\".\nSidechain: em \"") + V + T8 ("\" ative a entrada sidechain do PRIMEQ PRO e envie-lhe o áudio de \"") + P + T8 ("\". Logic: menu Side Chain · Ableton: Audio From › sidechain · Cubase: Ativar Side-Chain + Send · Pro Tools: Key Input (bus) · Reaper: send para canais 3/4 · FL Studio: Sidechain to this track · Studio One: Sidechain no plugin. Aplicar já liga \"scext\"."), in, 10.f, kDim, 5);
    }

    // ---------- ESPACO
    void paintSpace (juce::Graphics& g)
    {
        using namespace pqui; auto c = content(); auto map = c.removeFromLeft (420).toFloat(); auto inner = map.withTrimmedTop (22).withTrimmedBottom (20).withTrimmedLeft (36).withTrimmedRight (8);
        g.setColour (kPanel2); g.fillRoundedRectangle (map, 6.f); txt (g, T8 ("MAPA FREQUÊNCIA × PANORAMA"), { (int) map.getX() + 10, (int) map.getY() + 3, 300, 18 }, 12.f, kText, true);
        g.setColour (kGrid); g.drawVerticalLine ((int) inner.getCentreX(), inner.getY(), inner.getBottom());
        for (double f : { 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 }) { const float y = inner.getBottom() - (float) (std::log (f / 80.0) / std::log (12000.0 / 80.0)) * inner.getHeight(); g.setColour (kGrid); g.drawHorizontalLine ((int) y, inner.getX(), inner.getRight()); txt (g, f >= 1000 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f), { (int) map.getX() + 2, (int) y - 6, 32, 12 }, 9.f, kDim, false, juce::Justification::centredRight); }
        txt (g, "L", { (int) inner.getX(), (int) map.getBottom() - 16, 20, 14 }, 10.f, kDim); txt (g, "C", { (int) inner.getCentreX() - 10, (int) map.getBottom() - 16, 20, 14 }, 10.f, kDim, false, juce::Justification::centred); txt (g, "R", { (int) inner.getRight() - 20, (int) map.getBottom() - 16, 20, 14 }, 10.f, kDim, false, juce::Justification::centredRight);
        for (size_t t = 0; t < tracks.size() && t < res.curve.size(); ++t) for (int b = 0; b < pq::kMixBands; ++b)
        {
            const double lv = res.curve[t][(size_t) b]; if (lv < -60) continue; const float x = inner.getCentreX() + tracks[t].st.pan[b] * inner.getWidth() * 0.5f, y = inner.getBottom() - (float) (std::log (pq::mixBandFreq (b) / 80.0) / std::log (12000.0 / 80.0)) * inner.getHeight(); const float rad = juce::jlimit (2.f, 13.f, (float) ((lv + 60.0) / 60.0 * 14.0));
            g.setColour (trackColour ((int) t).withAlpha (0.45f)); g.fillEllipse (x - rad, y - rad, 2 * rad, 2 * rad);
        }
        int ly = (int) map.getY() + 3; int lx = (int) map.getRight() - 8; for (int t = (int) tracks.size() - 1; t >= 0; --t) { const int w = juce::GlyphArrangement::getStringWidthInt (fontOf (10.f, true), nameOf (t)) + 18; lx -= w; g.setColour (trackColour (t)); g.fillEllipse ((float) lx, (float) ly + 5, 8.f, 8.f); txt (g, nameOf (t), { lx + 11, ly, w - 11, 18 }, 10.f, kText, true); }
        auto r = content().withTrimmedLeft (430); txt (g, T8 ("ACHADOS ESPACIAIS"), r.removeFromTop (20), 12.f, kText, true);
        if (tracks.empty()) { wrap (g, status, r.withHeight (60), 12.f, kDim); return; }
        int shown = 0;
        for (const auto& f : space.findings)
        {
            if (shown >= 5) break; auto card = juce::Rectangle<int> (r.getX(), r.getY() + shown * 76, r.getWidth(), 70); g.setColour (kPanel2); g.fillRoundedRectangle (card.toFloat(), 8.f);
            const char* kind = f.kind == pq::SpaceFinding::CenterCrowd ? "CENTRO CONGESTIONADO" : (f.kind == pq::SpaceFinding::MidCut ? "CORTE S\xC3\x93 NO MID" : "MONO SAFETY"); txt (g, T8 (kind), { card.getX() + 10, card.getY() + 4, 220, 14 }, 10.f, f.kind == pq::SpaceFinding::MonoRisk || f.kind == pq::SpaceFinding::MonoGlobal ? kRed : kTeal, true);
            wrap (g, U (f.text), card.reduced (10, 0).withTrimmedTop (20).withTrimmedRight (f.props.empty() ? 0 : 96), 10.f, kDim, 3); ++shown;
        }
        if (shown == 0) wrap (g, T8 ("Sem achados: nenhuma sobreposição central relevante nem risco mono nas pistas analisadas."), r.withHeight (40), 11.f, kDim);
        auto tb = juce::Rectangle<int> (r.getX(), content().getY() + 20 + 5 * 76 + 4, r.getWidth(), 120); g.setColour (kPanel2); g.fillRoundedRectangle (tb.toFloat(), 8.f); auto ti = tb.reduced (10, 6);
        txt (g, T8 ("PISTA · TIPO · CORRELAÇÃO · PERDA EM MONO"), ti.removeFromTop (16), 10.f, kText, true);
        for (size_t i = 0; i < tracks.size() && i < 4; ++i) txt (g, nameOf ((int) i) + T8 (" · ") + U (pq::srcKindName (i < space.kind.size() ? space.kind[i] : pq::SrcKind::Stereo)) + T8 (" · ") + juce::String (tracks[i].st.corrAll, 2) + T8 (" · ") + juce::String (-tracks[i].st.monoLossAll, 1) + " dB", ti.removeFromTop (18), 10.5f, kDim);
        wrap (g, T8 ("O plugin não tem panner: o \"pan candidato\" é uma sugestão para fazer na pista. Um EQ M/S não desloca instrumentos já somados numa mistura estéreo."), { r.getX(), tb.getBottom() + 6, r.getWidth(), 34 }, 9.5f, kDim, 2);
    }

    // ---------- DOUTOR
    void paintDoctor (juce::Graphics& g)
    {
        using namespace pqui; for (int i = 0; i < 5; ++i) { const bool sel = i == symptom; g.setColour (sel ? kTealDim : kLine); g.fillRoundedRectangle (symChip (i).toFloat(), 6.f); txt (g, U (pq::symptomName ((pq::Symptom) i)), symChip (i), 10.5f, sel ? kTeal : kDim, true, juce::Justification::centred); }
        auto c = content(); c.removeFromTop (38);
        if (docR.evidence.empty() && docR.hyps.empty()) { wrap (g, docR.note.empty() ? status : U (docR.note), c.withHeight (60), 12.f, kDim); return; }
        txt (g, T8 ("EVIDÊNCIAS MEDIDAS"), c.removeFromTop (18), 11.f, kTeal, true);
        { auto b = c.removeFromTop (150); g.setColour (kPanel2); g.fillRoundedRectangle (b.toFloat(), 8.f); b.reduce (10, 6); for (size_t i = 0; i < docR.evidence.size() && i < 6; ++i) wrap (g, juce::String (juce::CharPointer_UTF8 ("\xE2\x80\xA2 ")) + U (docR.evidence[i]), b.removeFromTop (24), 10.5f, kText, 2); }
        c.removeFromTop (8); txt (g, T8 ("HIPÓTESES (não são certezas)"), c.removeFromTop (18), 11.f, kTeal, true);
        { auto b = c.removeFromTop (118); g.setColour (kPanel2); g.fillRoundedRectangle (b.toFloat(), 8.f); b.reduce (10, 4);
          for (size_t i = 0; i < docR.hyps.size() && i < 5; ++i) { auto row = b.removeFromTop (22); const auto v = docR.hyps[i].v; const auto col = v == pq::Verdict::Compatible ? kWarn : (v == pq::Verdict::Investigate ? kTeal : kDim); txt (g, U (pq::verdictName (v)), row.removeFromRight (160), 10.f, col, true, juce::Justification::centredRight); txt (g, U (docR.hyps[i].text), row, 10.5f, kText); } }
        c.removeFromTop (8); txt (g, T8 ("AÇÕES SUGERIDAS"), c.removeFromTop (18), 11.f, kTeal, true);
        for (size_t i = 0; i < docR.actions.size() && i < 6; ++i) { auto row = juce::Rectangle<int> (c.getX(), c.getY() + (int) i * 40, c.getWidth(), 36); g.setColour (kPanel2); g.fillRoundedRectangle (row.toFloat(), 6.f); wrap (g, U (docR.actions[i].text), row.reduced (10, 3).withTrimmedRight (100), 10.5f, kText, 2); }
        if (! docR.note.empty()) wrap (g, U (docR.note), { c.getX(), getHeight() - 56, c.getWidth(), 30 }, 10.f, kDim, 2);
    }

    // ---------- INTENCAO
    void paintIntent (juce::Graphics& g)
    {
        using namespace pqui; for (int i = 0; i < 5; ++i) { const bool sel = i == intent; g.setColour (sel ? kTealDim : kLine); g.fillRoundedRectangle (intentChip (i).toFloat(), 6.f); txt (g, pq::intentName ((pq::Intent) i), intentChip (i), 12.f, sel ? kTeal : kDim, true, juce::Justification::centred); }
        txt (g, U (pq::intentBlurb ((pq::Intent) intent)), content().withTrimmedTop (36).withHeight (16), 10.f, kDim);
        for (int i = 0; i < 4; ++i) { const bool sel = (int) role == i; g.setColour (sel ? kTealDim : kLine); g.fillRoundedRectangle (roleChip (i).toFloat(), 6.f); txt (g, pq::roleName ((pq::Role) i), roleChip (i), 11.f, sel ? kTeal : kDim, true, juce::Justification::centred); }
        auto c = content(); c.removeFromTop (34 + 22 + 28 + 8); txt (g, "INTENSIDADE", c.removeFromTop (26).withWidth (110), 10.f, kDim, true);
        c.removeFromTop (8); auto pr = c.removeFromTop (186).toFloat(); auto inner = pr.withTrimmedLeft (34).withTrimmedBottom (32).withTrimmedTop (22);
        g.setColour (kPanel2); g.fillRoundedRectangle (pr, 6.f); txt (g, haveCurve ? T8 ("ANTES → DEPOIS (previsão sobre a curva medida desta pista)") : T8 ("VARIAÇÃO PREVISTA (dB) — sem medição desta pista"), { (int) pr.getX() + 10, (int) pr.getY() + 3, 560, 18 }, 11.f, kText, true);
        for (double f : { 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 }) { const float x = fxHz (f, inner); g.setColour (kGrid); g.drawVerticalLine ((int) x, inner.getY(), inner.getBottom()); txt (g, f >= 1000 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f), { (int) x - 18, (int) inner.getBottom() + 2, 36, 12 }, 9.f, kDim, false, juce::Justification::centred); }
        auto yOf = [&] (double v) { return haveCurve ? inner.getY() + (float) juce::jlimit (0.0, 1.0, -v / 70.0) * inner.getHeight() : inner.getCentreY() - (float) juce::jlimit (-6.0, 6.0, v) / 6.f * inner.getHeight() * 0.5f; };
        if (! haveCurve) { g.setColour (kGrid); g.drawHorizontalLine ((int) yOf (0), inner.getX(), inner.getRight()); for (int d : { -6, -3, 3, 6 }) txt (g, juce::String (d), { (int) pr.getX() + 2, (int) yOf (d) - 6, 30, 12 }, 9.f, kDim, false, juce::Justification::centredRight); }
        juce::Path before, after, dyn; bool o1 = false, o2 = false;
        for (int b = 0; b < pq::kMixBands; ++b)
        {
            const float x = fxHz (pq::mixBandFreq (b), inner); const double sd = plan.staticDelta[(size_t) b], dm = plan.dynMax[(size_t) b];
            if (haveCurve) { const double v = curve[(size_t) b]; if (v < -119) { o1 = o2 = false; continue; } if (! o1) { before.startNewSubPath (x, yOf (v)); o1 = true; } else before.lineTo (x, yOf (v)); if (! o2) { after.startNewSubPath (x, yOf (v + sd)); o2 = true; } else after.lineTo (x, yOf (v + sd)); }
            else { if (b == 0) after.startNewSubPath (x, yOf (sd)); else after.lineTo (x, yOf (sd)); }
            if (std::fabs (dm) > 0.05) { const double base = haveCurve ? curve[(size_t) b] + sd : sd; if (dyn.isEmpty()) dyn.startNewSubPath (x, yOf (base + dm)); else dyn.lineTo (x, yOf (base + dm)); }
        }
        if (haveCurve) { g.setColour (kDim); g.strokePath (before, juce::PathStrokeType (1.6f)); }
        g.setColour (kTeal); g.strokePath (after, juce::PathStrokeType (2.2f)); if (! dyn.isEmpty()) { float dl[] = { 4.f, 3.f }; juce::Path d; juce::PathStrokeType (1.6f).createDashedStroke (d, dyn, dl, 2); g.setColour (kWarn); g.fillPath (d); }
        txt (g, T8 ("cinza = antes · verde = depois (estático) · tracejado âmbar = efeito máximo das bandas dinâmicas"), { (int) pr.getX() + 10, (int) pr.getBottom() - 16, 600, 14 }, 9.f, kDim);
        auto t = content().withTrimmedTop (34 + 22 + 28 + 8 + 26 + 8 + 186 + 8); txt (g, T8 ("O QUE VAI FAZER (bandas normais, editáveis)"), t.removeFromTop (18), 11.f, kTeal, true);
        wrap (g, U (plan.explanation) + "  " + U (plan.basis), t.removeFromTop (46), 10.5f, kText, 3);
        for (size_t i = 0; i < plan.props.size() && i < 4; ++i) { const auto& p = plan.props[i]; txt (g, juce::String (juce::CharPointer_UTF8 ("\xE2\x80\xA2 ")) + juce::String ((int) std::lround (p.freq)) + T8 (" Hz · ") + juce::String (p.gainDb, 1) + T8 (" dB · Q ") + juce::String (p.q, 1) + (p.dynamic ? T8 (" · dinâmico (limiar ") + juce::String (p.thrDb, 0) + " dB)" : juce::String()) + (p.why.empty() ? juce::String() : T8 (" · ") + U (p.why)), t.removeFromTop (16), 10.5f, kDim); }
        const float ld = proc.loudDeltaDb.load(); auto bot = juce::Rectangle<int> (content().getX(), content().getY() + 250 + 40 + 0, content().getWidth(), 20);
        txt (g, T8 ("Nível pós vs pré: ") + juce::String (ld, 1) + T8 (" dB — para ouvir A/B justo ative IGUALAR LOUDNESS (Auto Gain compensa) — ") + (proc.getParamReal ("autogain") > 0.5f ? "ligado" : "desligado"), { content().getX() + 596, content().getY() + 452, content().getWidth() - 600, 40 }, 10.f, kDim);
        (void) bot;
        if (! ownAnalysing()) wrap (g, T8 ("Dica: ligue \"ANALISAR\" nesta pista e toque o material — a intenção passa a usar a medição real em vez de valores genéricos."), { content().getX(), getHeight() - 56, content().getWidth(), 30 }, 10.f, kWarn, 2);
    }

    // ---------- CENARIOS
    void paintScen (juce::Graphics& g)
    {
        using namespace pqui; auto c = content();
        if (res.pairs.empty() || cons.sc.size() != 4) { wrap (g, cons.message.empty() ? status : U (cons.message), c.withHeight (50), 12.f, kDim); return; }
        const auto chips = pairChipRects(); for (size_t i = 0; i < chips.size(); ++i) { const bool sel = (int) i == selPair; g.setColour (sel ? kTealDim : kLine); g.fillRoundedRectangle (chips[i].toFloat(), 6.f); txt (g, nameOf (res.pairs[i].protectedIdx) + juce::String (juce::CharPointer_UTF8 (" \xE2\x86\x92 ")) + nameOf (res.pairs[i].victimIdx), chips[i], 11.f, sel ? kTeal : kDim, true, juce::Justification::centred); }
        const auto& pr = res.pairs[(size_t) selPair]; for (int i = 0; i < 3 && i < (int) pr.conflicts.size(); ++i) { const bool sel = i == selConf; g.setColour (sel ? kTealDim : kLine); g.fillRoundedRectangle (confChip (i).toFloat(), 6.f); txt (g, T8 ("Conflito ") + juce::String ((int) std::lround (pr.conflicts[(size_t) i].freq)) + " Hz", confChip (i), 11.f, sel ? kTeal : kDim, true, juce::Justification::centred); }
        auto tb = juce::Rectangle<int> (c.getX(), c.getY() + 34 + 28 + 10, c.getWidth(), 8 * 24 + 4); g.setColour (kPanel2); g.fillRoundedRectangle (tb.toFloat(), 8.f);
        static const char* lab[8] = { "Masking restante", "N\xC3\xADvel da pista (RMS)", "N\xC3\xADvel da soma (RMS)", "Maior altera\xC3\xA7\xC3\xA3o tonal m\xC3\xA9""dia", "Pico da banda (P95)", "Din\xC3\xA2mica da banda", "Corte desnecess\xC3\xA1rio", "Compensa\xC3\xA7\xC3\xA3o p/ A/B justo" };
        for (int r = 0; r < 8; ++r) txt (g, T8 (lab[r]), { c.getX() + 10, tb.getY() + 4 + r * 24, 220, 22 }, 10.5f, kDim, r == 0);
        for (int col = 0; col < 4; ++col)
        {
            const auto& m = cons.sc[(size_t) col]; auto x = scenCol (col).getX(); const bool bestMask = col > 0 && m.maskRemainPct <= std::min ({ cons.sc[1].maskRemainPct, cons.sc[2].maskRemainPct, cons.sc[3].maskRemainPct }) + 0.5;
            txt (g, U (m.name), { x, c.getY() + 34 + 28 - 22 + 22, 140, 18 }, 10.5f, col == 0 ? kDim : kTeal, true, juce::Justification::centred);
            const juce::String v[8] = { juce::String ((int) std::lround (m.maskRemainPct)) + " %", juce::String (m.victimRmsDb, 2) + " dB", juce::String (m.mixRmsDb, 2) + " dB", juce::String (m.maxTonalDb, 1) + " dB", juce::String (m.peakDb, 1) + " dB", juce::String (m.dynDb, 1) + " dB", juce::String ((int) std::lround (m.collateralPct)) + " %", col == 0 ? juce::String ("-") : juce::String (m.loudComp, 2) + " dB" };
            for (int r = 0; r < 8; ++r) txt (g, v[r], { x, tb.getY() + 4 + r * 24, 140, 22 }, 11.f, (r == 0 && bestMask) ? kTeal : kText, r == 0, juce::Justification::centred);
        }
        wrap (g, T8 ("Os cenários simulam os mesmos quadros de nível por banda (25 ms). A = corte estático · B = só corta quando a protegida toca (sidechain) · C = 1/3 EQ + 2/3 sidechain + 0,5 dB de nível. Não é previsão de qualidade sonora: a preferência exige escuta — use A/B com o nível igualado. Limitações: não vê fase, latência nem crest factor de banda larga."), { c.getX(), tb.getBottom() + 52, c.getWidth(), 56 }, 10.f, kDim, 4);
        const float ld = proc.loudDeltaDb.load(); txt (g, T8 ("Nível pós vs pré desta pista: ") + juce::String (ld, 1) + " dB", { c.getX(), tb.getBottom() + 112, 400, 18 }, 10.5f, kDim);
    }
};
