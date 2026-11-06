#include "PluginEditor.h"
#include <map>

using namespace pqui;

PrimeqEditor::PrimeqEditor (PrimeqProcessor& p)
    : juce::AudioProcessorEditor (p), proc (p), graph (p), band (p), strip (p), fx (p), smart (p, graph), mix (p), abx (p), studio (p, graph),
      login ([this] { timerCallback(); })
{
    if (AccessManager::get().pccConfigured()) juce::Thread::launch ([] { AccessManager::get().revalidatePcc(); });   // 1x/dia, em segundo plano
    openEditors().push_back (this); setLookAndFeel (&lnf);
    addAndMakeVisible (graph); addAndMakeVisible (band); addAndMakeVisible (strip); addAndMakeVisible (fx); addChildComponent (smart); addChildComponent (mix); addAndMakeVisible (mixBtn); addAndMakeVisible (modeBox); modeBox.addItem ("Simple", 1); modeBox.addItem ("Advanced", 2); modeBox.addItem ("Expert", 3); modeBox.setSelectedId (proc.uiMode + 1, juce::dontSendNotification); modeBox.onChange = [this] { proc.uiMode = modeBox.getSelectedId() - 1; applyMode(); }; modeBox.setTooltip (pqui::T8 ("Simple: essencial · Advanced: tudo · Expert: + cenários e relatório"));
    for (auto* b : { &presetBtn, &undoBtn, &redoBtn, &abBtn, &logoutBtn }) addAndMakeVisible (b);
    addAndMakeVisible (bypassBtn); addAndMakeVisible (smartBtn); addAndMakeVisible (phaseBox); addAndMakeVisible (phaseLbl);
    if (auto* pc = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter ("phase"))) phaseBox.addItemList (pc->choices, 1);
    aPhase = std::make_unique<ComboAtt> (proc.apvts, "phase", phaseBox); aBypass = std::make_unique<ButtonAtt> (proc.apvts, "bypass", bypassBtn);
    phaseLbl.setText ("FASE", juce::dontSendNotification); phaseLbl.setColour (juce::Label::textColourId, kDim); phaseLbl.setFont (fontOf (10.f, true)); phaseLbl.setJustificationType (juce::Justification::centredRight);
    presetBtn.onClick = [this] { presetMenu(); };
    undoBtn.onClick = [this] { proc.undo.undo(); }; redoBtn.onClick = [this] { proc.undo.redo(); };
    abBtn.onClick = [this] { proc.toggleAB(); abBtn.setButtonText (proc.abSlot() == 0 ? "A" : "B"); };
    abBtn.setTooltip ("A/B: alterna entre duas definicoes completas"); abBtn.setButtonText (proc.abSlot() == 0 ? "A" : "B");
    logoutBtn.onClick = [this] { AccessManager::get().logout(); timerCallback(); };
    smartBtn.onClick = [this] { showSmart (smartBtn.getToggleState()); };
    mixBtn.onClick = [this] { showMix (mixBtn.getToggleState()); };
    graph.onSelect = [this] (int b) { band.bind (b); };
    mix.onZones = [this] (const std::vector<OverlayZone>& z) { graph.setZones (z); zonesBtn.setVisible (! z.empty() || true); };
    addChildComponent (guide); guide.onClose = [] { AccessManager::get().setOnboardingSeen(); };
    addAndMakeVisible (skinBtn); skinBtn.setButtonText (pqui::skinName (shownSkin)); skinBtn.setTooltip (juce::String (juce::CharPointer_UTF8 ("Cor / skin: Prime, Ouro, \xC3\x8D""ndigo (seguro para daltonismo)")));
    skinBtn.onClick = [this] { changeSkin ((pqui::skinIndex() + 1) % 3); };
    addAndMakeVisible (langBtn); langBtn.onClick = [this] { setLanguage (pqui::i18n::lang().load() == 0 ? 1 : 0); };
    addAndMakeVisible (helpBtn); helpBtn.setTooltip (pqui::T8 ("Guia rápido")); helpBtn.onClick = [this] { guide.open(); resized(); };
    addChildComponent (studio); addAndMakeVisible (studioBtn); studioBtn.setTooltip (juce::String (juce::CharPointer_UTF8 ("Alvo por g\xC3\xA9nero, Tradutor, Palavras, Cofre de decis\xC3\xB5""es e Ensino"))); studioBtn.onClick = [this] { showStudio (studioBtn.getToggleState()); };
    addChildComponent (abx); addAndMakeVisible (abxBtn); abxBtn.setTooltip ("Teste ABX cego: original vs processado, loudness igualado"); abxBtn.onClick = [this] { showAbx (abxBtn.getToggleState()); };
    addAndMakeVisible (zonesBtn); zonesBtn.setToggleState (true, juce::dontSendNotification); zonesBtn.setTooltip (pqui::T8 ("Mostra no gráfico as zonas de masking e espaço detectadas pelo Assistente")); zonesBtn.onClick = [this] { graph.showZones = zonesBtn.getToggleState(); graph.repaint(); };
    addChildComponent (login); wasUnlocked = AccessManager::get().isUnlocked(); login.setVisible (! wasUnlocked); lastSel = 0; logoutBtn.setVisible (wasUnlocked && ! AccessManager::inTrial());
    applyMode();
    captureLanguage(); pqui::i18n::forcePt() = false;
    pqui::i18n::lang() = AccessManager::get().language(); applyLanguage();
    setResizable (true, true); setResizeLimits (1000, 740, 2600, 1700);
    setSize (proc.uiWidth, proc.uiHeight);
    startTimerHz (30);
}

PrimeqEditor::~PrimeqEditor() { stopTimer(); { auto& v = openEditors(); v.erase (std::remove (v.begin(), v.end(), this), v.end()); } proc.listenBand.store (-1); setLookAndFeel (nullptr); }

void PrimeqEditor::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (kBg.brighter (0.05f), 0.f, 0.f, kBg.darker (0.25f), 0.f, (float) getHeight(), false)); g.fillAll();
    { g.setGradientFill (juce::ColourGradient (kPanel2.withAlpha (0.9f), 0.f, 0.f, kBg.withAlpha (0.f), 0.f, 46.f, false)); g.fillRect (0, 0, getWidth(), 46);
      g.setGradientFill (juce::ColourGradient (kTeal.withAlpha (0.0f), 0.f, 0.f, kTeal.withAlpha (0.0f), (float) getWidth(), 0.f, false));
      juce::ColourGradient ln (kTeal.withAlpha (0.0f), 0.f, 0.f, kTeal.withAlpha (0.0f), (float) getWidth(), 0.f, false); ln.addColour (0.12, kTeal.withAlpha (0.75f)); ln.addColour (0.5, kTeal.withAlpha (0.18f));
      g.setGradientFill (ln); g.fillRect (0.f, 45.f, (float) getWidth(), 1.5f); }
    g.setGradientFill (juce::ColourGradient (kTeal, 18.f, 8.f, kText, 190.f, 8.f, false));
    g.setFont (fontOf (22.f, true)); g.drawText ("PRIMEQ PRO", 16, 6, 150, 34, juce::Justification::centredLeft);
    g.setColour (kDim); g.setFont (fontOf (11.f)); g.drawText ("by Piradex", 138, 12, 80, 28, juce::Justification::centredLeft);
    g.setColour (kDim); g.setFont (fontOf (11.f));
    g.drawText (AccessManager::get().roleText() + "   v" + juce::String (JucePlugin_VersionString), getWidth() - 330, getHeight() - 16, 320, 14, juce::Justification::centredRight);
}

void PrimeqEditor::resized()
{
    auto r = getLocalBounds().reduced (10, 6);
    auto head = r.removeFromTop (38);
    head.removeFromLeft (220);
    presetBtn.setBounds (head.removeFromLeft (140)); head.removeFromLeft (6);
    undoBtn.setBounds (head.removeFromLeft (52)); head.removeFromLeft (4); redoBtn.setBounds (head.removeFromLeft (52)); head.removeFromLeft (6); abBtn.setBounds (head.removeFromLeft (38));
    langBtn.setBounds (head.removeFromRight (40)); head.removeFromRight (4); skinBtn.setBounds (head.removeFromRight (62)); head.removeFromRight (6); logoutBtn.setBounds (head.removeFromRight (54)); head.removeFromRight (6); helpBtn.setBounds (head.removeFromRight (30)); head.removeFromRight (6); smartBtn.setBounds (head.removeFromRight (88)); head.removeFromRight (4); studioBtn.setBounds (head.removeFromRight (78)); head.removeFromRight (4); mixBtn.setBounds (head.removeFromRight (96)); head.removeFromRight (4); zonesBtn.setBounds (head.removeFromRight (62)); head.removeFromRight (4); abxBtn.setBounds (head.removeFromRight (46)); head.removeFromRight (4); modeBox.setBounds (head.removeFromRight (84)); head.removeFromRight (4); bypassBtn.setBounds (head.removeFromRight (76)); head.removeFromRight (6);
    phaseBox.setBounds (head.removeFromRight (110)); phaseLbl.setBounds (head.removeFromRight (34));
    r.removeFromTop (6);
    r.removeFromBottom (14);
    if (mix.isVisible()) { mix.setBounds (r); login.setBounds (getLocalBounds()); proc.uiWidth = getWidth(); proc.uiHeight = getHeight(); return; }
    strip.setBounds (r.removeFromBottom (70)); r.removeFromBottom (6);
    if (fx.isVisible()) { fx.setBounds (r.removeFromBottom (92)); r.removeFromBottom (6); }
    band.setBounds (r.removeFromBottom (176)); r.removeFromBottom (6);
    if (smart.isVisible()) { smart.setBounds (r.removeFromRight (290)); r.removeFromRight (6); }
    if (studio.isVisible()) { studio.setBounds (r.removeFromRight (360)); r.removeFromRight (6); }
    graph.setBounds (r);
    abx.setBounds (r.getRight() - 470, r.getY() + 10, 460, 352); abx.toFront (false);
    login.setBounds (getLocalBounds()); guide.setBounds (getLocalBounds()); if (guide.isVisible()) guide.toFront (false); else if (login.isVisible()) login.toFront (false);
    proc.uiWidth = getWidth(); proc.uiHeight = getHeight();
}

void PrimeqEditor::applyMode()
{
    const bool simple = proc.uiMode == 0;
    smartBtn.setVisible (! simple); if (simple) { smart.setVisible (false); smartBtn.setToggleState (false, juce::dontSendNotification); }
    fx.setVisible (! simple && ! mix.isVisible()); resized();
}

void PrimeqEditor::timerCallback()
{
    AccessManager::get().checkExpiry();
    const bool unlocked = AccessManager::get().isUnlocked();
    if (unlocked && ! login.isVisible() && ! guide.isVisible() && ! AccessManager::get().onboardingSeen()) { guide.open(); resized(); }
    if (unlocked != wasUnlocked || lastSel < 0) { wasUnlocked = unlocked; login.setVisible (! unlocked); logoutBtn.setVisible (unlocked && ! AccessManager::inTrial()); login.toFront (false); repaint(); lastSel = 0; }
    if (! unlocked) return;
    graph.tick(); band.refresh(); strip.tick(); fx.tick(); smart.tick(); if (mix.isVisible()) mix.tick();
    undoBtn.setEnabled (proc.undo.canUndo()); redoBtn.setEnabled (proc.undo.canRedo());
}

void PrimeqEditor::presetMenu()
{
    juce::PopupMenu m; const auto& fp = PrimeqProcessor::factoryPresets();
    juce::PopupMenu f; { std::map<juce::String, juce::PopupMenu> cats; std::vector<juce::String> order; std::vector<int> geral;
        for (size_t i = 0; i < fp.size(); ++i)
        {
            const auto full = juce::String (juce::CharPointer_UTF8 (fp[i].name)); const int sl = full.indexOfChar ('/');
            if (sl < 0) { geral.push_back ((int) i); continue; }
            const auto cat = full.substring (0, sl); if (cats.find (cat) == cats.end()) order.push_back (cat); cats[cat].addItem ((int) i + 1, full.substring (sl + 1));
        }
        for (int i : geral) f.addItem (i + 1, juce::String (juce::CharPointer_UTF8 (fp[(size_t) i].name))); f.addSeparator();
        for (const auto& c : order) f.addSubMenu (c, cats[c]); }
    m.addSubMenu (pqui::T8 ("Fabrica"), f);
    juce::PopupMenu u; const auto names = proc.userPresetNames(); for (int i = 0; i < names.size(); ++i) u.addItem (1000 + i, names[i]);
    if (names.isEmpty()) u.addItem (-1, pqui::T8 ("(vazio)"), false); m.addSubMenu (pqui::T8 ("Utilizador"), u);
    m.addSeparator(); m.addItem (9000, pqui::T8 ("Guardar preset..."));
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetBtn), [this, names] (int r)
    {
        if (r >= 1 && r < 1000) { proc.loadFactoryPreset (r - 1); presetName = juce::String (juce::CharPointer_UTF8 (PrimeqProcessor::factoryPresets()[(size_t) r - 1].name)); presetBtn.setButtonText (presetName); }
        else if (r >= 1000 && r < 9000) { if (proc.loadUserPreset (names[r - 1000])) { presetName = names[r - 1000]; presetBtn.setButtonText (presetName); } }
        else if (r == 9000)
        {
            auto* w = new juce::AlertWindow (pqui::T8 ("Guardar preset"), pqui::T8 ("Nome do preset:"), juce::MessageBoxIconType::NoIcon);
            w->addTextEditor ("name", presetName, ""); w->addButton (pqui::T8 ("Guardar"), 1, juce::KeyPress (juce::KeyPress::returnKey)); w->addButton (pqui::T8 ("Cancelar"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
            w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int res) {
                if (res == 1) { const auto n = w->getTextEditorContents ("name").trim(); if (n.isNotEmpty()) { proc.saveUserPreset (n); presetName = n; presetBtn.setButtonText (n); } } }), true);
        }
    });
}
