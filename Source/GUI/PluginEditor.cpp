#include "PluginEditor.h"
#include <map>

using namespace pqui;

PrimeqEditor::PrimeqEditor (PrimeqProcessor& p)
    : juce::AudioProcessorEditor (p), proc (p), graph (p), band (p), strip (p), fx (p), smart (p, graph), mix (p),
      login ([this] { timerCallback(); })
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (graph); addAndMakeVisible (band); addAndMakeVisible (strip); addAndMakeVisible (fx); addChildComponent (smart); addChildComponent (mix); addAndMakeVisible (mixBtn); addAndMakeVisible (modeBox); modeBox.addItem ("Simple", 1); modeBox.addItem ("Advanced", 2); modeBox.addItem ("Expert", 3); modeBox.setSelectedId (proc.uiMode + 1, juce::dontSendNotification); modeBox.onChange = [this] { proc.uiMode = modeBox.getSelectedId() - 1; applyMode(); }; modeBox.setTooltip ("Simple: essencial Â· Advanced: tudo Â· Expert: + cenÃ¡rios e relatÃ³rio");
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
    addChildComponent (login); wasUnlocked = AccessManager::get().isUnlocked(); login.setVisible (! wasUnlocked); lastSel = 0; logoutBtn.setVisible (wasUnlocked && ! AccessManager::inTrial());
    applyMode();
    setResizable (true, true); setResizeLimits (1000, 740, 2600, 1700);
    setSize (proc.uiWidth, proc.uiHeight);
    startTimerHz (30);
}

PrimeqEditor::~PrimeqEditor() { stopTimer(); proc.listenBand.store (-1); setLookAndFeel (nullptr); }

void PrimeqEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    g.setGradientFill (juce::ColourGradient (kTeal, 18.f, 8.f, juce::Colour (0xffe6e9ec), 190.f, 8.f, false));
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
    presetBtn.setBounds (head.removeFromLeft (170)); head.removeFromLeft (6);
    undoBtn.setBounds (head.removeFromLeft (52)); head.removeFromLeft (4); redoBtn.setBounds (head.removeFromLeft (52)); head.removeFromLeft (6); abBtn.setBounds (head.removeFromLeft (38));
    logoutBtn.setBounds (head.removeFromRight (54)); head.removeFromRight (6); smartBtn.setBounds (head.removeFromRight (100)); head.removeFromRight (6); mixBtn.setBounds (head.removeFromRight (110)); head.removeFromRight (6); modeBox.setBounds (head.removeFromRight (96)); head.removeFromRight (6); bypassBtn.setBounds (head.removeFromRight (84)); head.removeFromRight (10);
    phaseBox.setBounds (head.removeFromRight (150)); phaseLbl.setBounds (head.removeFromRight (40));
    r.removeFromTop (6);
    r.removeFromBottom (14);
    if (mix.isVisible()) { mix.setBounds (r); login.setBounds (getLocalBounds()); proc.uiWidth = getWidth(); proc.uiHeight = getHeight(); return; }
    strip.setBounds (r.removeFromBottom (70)); r.removeFromBottom (6);
    if (fx.isVisible()) { fx.setBounds (r.removeFromBottom (92)); r.removeFromBottom (6); }
    band.setBounds (r.removeFromBottom (176)); r.removeFromBottom (6);
    if (smart.isVisible()) { smart.setBounds (r.removeFromRight (290)); r.removeFromRight (6); }
    graph.setBounds (r);
    login.setBounds (getLocalBounds());
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
    m.addSubMenu ("Fabrica", f);
    juce::PopupMenu u; const auto names = proc.userPresetNames(); for (int i = 0; i < names.size(); ++i) u.addItem (1000 + i, names[i]);
    if (names.isEmpty()) u.addItem (-1, "(vazio)", false); m.addSubMenu ("Utilizador", u);
    m.addSeparator(); m.addItem (9000, "Guardar preset...");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetBtn), [this, names] (int r)
    {
        if (r >= 1 && r < 1000) { proc.loadFactoryPreset (r - 1); presetName = juce::String (juce::CharPointer_UTF8 (PrimeqProcessor::factoryPresets()[(size_t) r - 1].name)); presetBtn.setButtonText (presetName); }
        else if (r >= 1000 && r < 9000) { if (proc.loadUserPreset (names[r - 1000])) { presetName = names[r - 1000]; presetBtn.setButtonText (presetName); } }
        else if (r == 9000)
        {
            auto* w = new juce::AlertWindow ("Guardar preset", "Nome do preset:", juce::MessageBoxIconType::NoIcon);
            w->addTextEditor ("name", presetName, ""); w->addButton ("Guardar", 1, juce::KeyPress (juce::KeyPress::returnKey)); w->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int res) {
                if (res == 1) { const auto n = w->getTextEditorContents ("name").trim(); if (n.isNotEmpty()) { proc.saveUserPreset (n); presetName = n; presetBtn.setButtonText (n); } } }), true);
        }
    });
}
