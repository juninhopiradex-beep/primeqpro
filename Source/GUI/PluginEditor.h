// =============================================================================
//  PRIMEQ pro by Piradex — editor: cabecalho, grafico, inspector de banda, tira global, Smart Ops, login
// =============================================================================
#pragma once
#include "LookAndFeel.h"
#include "EqGraph.h"
#include "../Shared/Units.h"

using pqui::T8;

using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

// ------------------------------------------------------------------------------------------ medidor L/R com clip hold
class LevelMeter : public juce::Component
{
public:
    explicit LevelMeter (PrimeqProcessor& p) : proc (p) {}
    void tick()
    {
        const float a = proc.meterL.exchange (0.f), b = proc.meterR.exchange (0.f);
        lv[0] = std::max (a, lv[0] * 0.86f); lv[1] = std::max (b, lv[1] * 0.86f);
        const auto now = juce::Time::getMillisecondCounter();
        if (a >= 1.0f || b >= 1.0f) clipUntil = now + 2500;
        clip = now < clipUntil; repaint();
    }
    void mouseDown (const juce::MouseEvent&) override { clipUntil = 0; }
    void paint (juce::Graphics& g) override
    {
        using namespace pqui;
        auto r = getLocalBounds().toFloat();
        for (int c = 0; c < 2; ++c)
        {
            auto bar = juce::Rectangle<float> (r.getX() + c * (r.getWidth() * 0.5f), r.getY(), r.getWidth() * 0.5f - 2.f, r.getHeight() - 12.f);
            g.setColour (kPanel2); g.fillRoundedRectangle (bar, 3.f);
            const float db = juce::Decibels::gainToDecibels (lv[c], -90.f), t = juce::jlimit (0.f, 1.f, (db + 60.f) / 60.f);
            auto fill = bar.withTrimmedTop (bar.getHeight() * (1.f - t));
            g.setColour (db > -1.f ? kRed : (db > -9.f ? kWarn : kTeal)); g.fillRoundedRectangle (fill, 3.f);
        }
        g.setColour (clip ? kRed : kPanel2); g.fillRoundedRectangle (r.getX(), r.getBottom() - 10.f, r.getWidth() - 2.f, 9.f, 3.f);
        g.setColour (clip ? juce::Colours::white : kDim); g.setFont (fontOf (8.f, true)); g.drawText ("CLIP", getLocalBounds().removeFromBottom (10), juce::Justification::centred);
    }
private:
    PrimeqProcessor& proc; float lv[2] { 0.f, 0.f }; juce::uint32 clipUntil = 0; bool clip = false;
};

// ------------------------------------------------------------------------------------------ inspector da banda selecionada
class BandPanel : public juce::Component
{
public:
    explicit BandPanel (PrimeqProcessor& p) : proc (p)
    {
        using namespace pqui;
        auto knob = [this] (Knob& k, const char* name, const char* id, bool bipolar, juce::Colour c) {
            k.s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag); k.s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
            k.s.setColour (juce::Slider::rotarySliderFillColourId, c); k.s.getProperties().set ("bipolar", bipolar);
            k.s.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
            k.l.setText (name, juce::dontSendNotification); k.l.setJustificationType (juce::Justification::centred); k.l.setColour (juce::Label::textColourId, kDim); k.l.setFont (fontOf (11.f));
            k.id = id; addAndMakeVisible (k.s); addAndMakeVisible (k.l);
        };
        knob (freq, "FREQ", "freq", false, kTeal); knob (gain, "GAIN", "gain", true, kTeal); knob (q, "Q", "q", false, kTeal);
        knob (thr, "THRESH", "thr", false, kWarn); knob (range, "RANGE", "range", true, kWarn); knob (ratio, "RATIO", "ratio", false, kWarn);
        knob (attack, "ATTACK", "attack", false, kWarn); knob (hold, "HOLD", "hold", false, kWarn); knob (release, "RELEASE", "release", false, kWarn); knob (knee, "KNEE", "knee", false, kWarn);
        auto fmtDb = [] (double v) { return juce::String (v, 1) + " dB"; };
        freq.fmt = [] (double v) { return juce::String (pq::units::formatFreq (v)); };
        freq.parse = [] (const juce::String& t) { return pq::units::parseFreq (t.toStdString(), 1000.0); };
        for (auto* k : { &gain, &thr, &range, &knee }) { k->fmt = fmtDb; k->parse = [] (const juce::String& t) { return pq::units::parseDb (t.toStdString(), 0.0); }; }
        q.fmt = [] (double v) { return juce::String (v, v < 1.0 ? 3 : 2); }; q.parse = [] (const juce::String& t) { return pq::units::parsePlain (t.toStdString(), 1.0); };
        ratio.fmt = [] (double v) { return juce::String (v, 1) + ":1"; }; ratio.parse = [] (const juce::String& t) { return pq::units::parsePlain (t.toStdString(), 2.0); };
        for (auto* k : { &attack, &hold, &release }) { k->fmt = [] (double v) { return v >= 100 ? juce::String (v, 0) + " ms" : juce::String (v, 1) + " ms"; }; k->parse = [] (const juce::String& t) { return pq::units::parseMs (t.toStdString(), 10.0); }; }

        fill (typeBox, { "Bell", "Low Shelf", "High Shelf", "High Pass", "Low Pass", "Notch", "Band Pass", "All Pass" });
        fill (placeBox, { "Stereo", "Left", "Right", "Mid", "Side" });
        fill (slopeBox, { "6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct", "72 dB/oct", "96 dB/oct", "Brickwall" });
        fill (detBox, { "Peak", "RMS" }); fill (dirBox, { "Above", "Below" });
        for (auto* b : { &onBtn, &dynBtn, &listenBtn }) addAndMakeVisible (b);
        for (auto* b : { &copyBtn, &pasteBtn, &delBtn }) addAndMakeVisible (b);
        listenBtn.onClick = [this] { proc.listenBand.store (listenBtn.getToggleState() ? band : -1); };
        copyBtn.onClick = [this] { EqGraph::clipboard = proc.getBand (band); EqGraph::hasClip = true; };
        pasteBtn.onClick = [this] { if (EqGraph::hasClip) { proc.undo.beginNewTransaction ("Paste band"); auto c = EqGraph::clipboard; c.on = true; proc.setBand (band, c); } };
        delBtn.onClick = [this] { proc.undo.beginNewTransaction ("Delete band"); proc.setParamReal (juce::String (pq::bid (band, "on")), 0.f); };
        bind (0);
    }

    void bind (int b)
    {
        band = b;
        for (auto* k : { &freq, &gain, &q, &thr, &range, &ratio, &attack, &hold, &release, &knee })
        {
            k->att.reset(); k->att = std::make_unique<SliderAtt> (proc.apvts, juce::String (pq::bid (b, k->id)), k->s);
            k->s.textFromValueFunction = k->fmt; k->s.valueFromTextFunction = k->parse; k->s.updateText();   // o attachment substitui os formatadores
            if (auto* pp = proc.apvts.getParameter (juce::String (pq::bid (b, k->id)))) k->s.setDoubleClickReturnValue (true, pp->convertFrom0to1 (pp->getDefaultValue()));
        }
        auto A = [&] (std::unique_ptr<ComboAtt>& a, juce::ComboBox& c, const char* k) { a.reset(); a = std::make_unique<ComboAtt> (proc.apvts, juce::String (pq::bid (b, k)), c); };
        auto Bt = [&] (std::unique_ptr<ButtonAtt>& a, juce::Button& c, const char* k) { a.reset(); a = std::make_unique<ButtonAtt> (proc.apvts, juce::String (pq::bid (b, k)), c); };
        A (aType, typeBox, "type"); A (aPlace, placeBox, "place"); A (aSlope, slopeBox, "slope"); A (aDet, detBox, "det"); A (aDir, dirBox, "dir");
        Bt (aOn, onBtn, "on"); Bt (aDyn, dynBtn, "dyn");
        listenBtn.setToggleState (proc.listenBand.load() == b, juce::dontSendNotification);
        refresh(); repaint();
    }

    void refresh()   // chamado pelo timer
    {
        const auto p = proc.getBand (band); const bool cut = pq::typeIsCut (p.type), gt = pq::typeHasGain (p.type);
        slopeBox.setVisible (cut); gain.s.setEnabled (gt); q.s.setEnabled (! cut || pq::kSlopeOrder[p.slope] == 2);
        const bool dynOn = gt && p.dyn && p.on;
        for (auto* k : { &thr, &range, &ratio, &attack, &hold, &release, &knee }) k->s.setEnabled (dynOn);
        detBox.setEnabled (dynOn); dirBox.setEnabled (dynOn); dynBtn.setEnabled (gt);
        const bool l = proc.listenBand.load() == band; if (listenBtn.getToggleState() != l) listenBtn.setToggleState (l, juce::dontSendNotification);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 6); auto top = r.removeFromTop (28);
        onBtn.setBounds (top.removeFromLeft (46)); top.removeFromLeft (14);
        top.removeFromLeft (70); typeBox.setBounds (top.removeFromLeft (112)); top.removeFromLeft (6);
        placeBox.setBounds (top.removeFromLeft (84)); top.removeFromLeft (6);
        slopeBox.setBounds (top.removeFromLeft (104)); top.removeFromLeft (14);
        dynBtn.setBounds (top.removeFromLeft (52)); top.removeFromLeft (6); detBox.setBounds (top.removeFromLeft (70)); top.removeFromLeft (6); dirBox.setBounds (top.removeFromLeft (78));
        delBtn.setBounds (top.removeFromRight (64)); top.removeFromRight (6); pasteBtn.setBounds (top.removeFromRight (56)); top.removeFromRight (6); copyBtn.setBounds (top.removeFromRight (62)); top.removeFromRight (6); listenBtn.setBounds (top.removeFromRight (64));
        r.removeFromTop (4);
        const int n = 10, w = r.getWidth() / n; int x = r.getX();
        for (auto* k : { &freq, &gain, &q, &thr, &range, &ratio, &attack, &hold, &release, &knee })
        { auto c = juce::Rectangle<int> (x, r.getY(), w, r.getHeight()); k->l.setBounds (c.removeFromTop (14)); k->s.setBounds (c); x += w; }
    }
    void paint (juce::Graphics& g) override
    {
        using namespace pqui;
        g.setColour (kPanel); g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.f);
        g.setColour (bandColour (band)); g.fillEllipse (60.f, 13.f, 12.f, 12.f);
        g.setColour (kText); g.setFont (fontOf (12.f, true)); g.drawText ("Banda " + juce::String (band + 1), 76, 6, 80, 26, juce::Justification::centredLeft);
        g.setColour (kLine); g.drawVerticalLine ((int) ((float) freq.s.getX() + 3 * (float) freq.s.getWidth() - 4), 40.f, (float) getHeight() - 8.f);
    }
private:
    struct Knob { juce::Slider s; juce::Label l; std::unique_ptr<SliderAtt> att; const char* id = ""; std::function<juce::String (double)> fmt; std::function<double (const juce::String&)> parse; };
    void fill (juce::ComboBox& c, std::initializer_list<const char*> items) { int i = 1; for (auto* it : items) c.addItem (it, i++); addAndMakeVisible (c); }
    PrimeqProcessor& proc; int band = 0;
    Knob freq, gain, q, thr, range, ratio, attack, hold, release, knee;
    juce::ToggleButton onBtn { "ON" }, dynBtn { "DYN" }, listenBtn { "LISTEN" };
    juce::TextButton copyBtn { "Copiar" }, pasteBtn { "Colar" }, delBtn { "Apagar" };
    juce::ComboBox typeBox, placeBox, slopeBox, detBox, dirBox;
    std::unique_ptr<ComboAtt> aType, aPlace, aSlope, aDet, aDir; std::unique_ptr<ButtonAtt> aOn, aDyn;
};

// ------------------------------------------------------------------------------------------ seccao global
class GlobalStrip : public juce::Component
{
public:
    explicit GlobalStrip (PrimeqProcessor& p) : proc (p), meter (p)
    {
        using namespace pqui;
        outK.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag); outK.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
        outK.setColour (juce::Slider::rotarySliderFillColourId, kTeal); outK.getProperties().set ("bipolar", true);
        outK.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
        outK.textFromValueFunction = [] (double v) { return juce::String (v, 1) + " dB"; }; outK.valueFromTextFunction = [] (const juce::String& t) { return pq::units::parseDb (t.toStdString(), 0.0); };
        addAndMakeVisible (outK); aOut = std::make_unique<SliderAtt> (p.apvts, "out", outK);
        outK.textFromValueFunction = [] (double v) { return juce::String (v, 1) + " dB"; }; outK.valueFromTextFunction = [] (const juce::String& t) { return pq::units::parseDb (t.toStdString(), 0.0); }; outK.updateText(); outK.setDoubleClickReturnValue (true, 0.0);
        outL.setText ("OUTPUT", juce::dontSendNotification); outL.setJustificationType (juce::Justification::centred); outL.setColour (juce::Label::textColourId, kDim); outL.setFont (fontOf (11.f)); addAndMakeVisible (outL);
        auto tog = [this] (juce::ToggleButton& b, std::unique_ptr<ButtonAtt>& a, const char* id) { addAndMakeVisible (b); a = std::make_unique<ButtonAtt> (proc.apvts, id, b); };
        tog (autoB, aAuto, "autogain"); tog (deltaB, aDelta, "delta"); tog (scB, aSc, "scext"); tog (phB, aPh, "peakhold"); tog (frB, aFr, "freeze");
        auto combo = [this] (juce::ComboBox& c, std::unique_ptr<ComboAtt>& a, const char* id, juce::Label& l, const char* cap) {
            if (auto* pc = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (id))) c.addItemList (pc->choices, 1);
            addAndMakeVisible (c); a = std::make_unique<ComboAtt> (proc.apvts, id, c);
            l.setText (cap, juce::dontSendNotification); l.setFont (fontOf (10.f)); l.setColour (juce::Label::textColourId, kDim); addAndMakeVisible (l); };
        combo (fftC, aFft, "fftsize", lFft, "FFT"); combo (viewC, aView, "specview", lView, "VISTA"); combo (tiltC, aTilt, "tilt", lTilt, "TILT"); combo (rngC, aRng, "graphrange", lRng, "ESCALA");
        addAndMakeVisible (meter); info.setColour (juce::Label::textColourId, kDim); info.setFont (fontOf (11.f)); info.setJustificationType (juce::Justification::centredRight); addAndMakeVisible (info);
        autoB.setButtonText ("AUTO GAIN"); deltaB.setButtonText ("DELTA"); scB.setButtonText ("SIDECHAIN"); phB.setButtonText ("PEAK HOLD"); frB.setButtonText ("FREEZE");
        autoB.setTooltip (T8 ("Compensa o ganho médio da curva (40 Hz–14 kHz)")); deltaB.setTooltip (T8 ("Ouve só a diferença entre processado e original"));
    }
    void tick()
    {
        meter.tick();
        const double fs = proc.sampleRateNow(); const int lat = proc.currentLatency();
        info.setText ("Latencia " + juce::String (lat) + " smp (" + juce::String (1000.0 * lat / fs, 1) + " ms)    CPU " + juce::String (proc.cpuLoad.load() * 100.f, 1) + " %", juce::dontSendNotification);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 6);
        auto o = r.removeFromLeft (84); outL.setBounds (o.removeFromTop (14)); outK.setBounds (o); r.removeFromLeft (8);
        meter.setBounds (r.removeFromLeft (30).reduced (0, 2)); r.removeFromLeft (14);
        auto tb = r.removeFromLeft (3 * 104 + 12); auto row1 = tb.removeFromTop (28); tb.removeFromTop (6); auto row2 = tb.removeFromTop (28);
        autoB.setBounds (row1.removeFromLeft (100)); row1.removeFromLeft (6); deltaB.setBounds (row1.removeFromLeft (100)); row1.removeFromLeft (6); scB.setBounds (row1.removeFromLeft (100));
        phB.setBounds (row2.removeFromLeft (100)); row2.removeFromLeft (6); frB.setBounds (row2.removeFromLeft (100));
        r.removeFromLeft (16);
        auto cb = [&] (juce::Label& l, juce::ComboBox& c, int w) { auto a = r.removeFromLeft (w); l.setBounds (a.removeFromTop (14)); c.setBounds (a.removeFromTop (26)); r.removeFromLeft (8); };
        cb (lFft, fftC, 74); cb (lView, viewC, 84); cb (lTilt, tiltC, 92); cb (lRng, rngC, 82);
        info.setBounds (r);
    }
    void paint (juce::Graphics& g) override { g.setColour (pqui::kPanel); g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.f); }
private:
    PrimeqProcessor& proc; LevelMeter meter;
    juce::Slider outK; juce::Label outL, info, lFft, lView, lTilt, lRng;
    juce::ToggleButton autoB, deltaB, scB, phB, frB; juce::ComboBox fftC, viewC, tiltC, rngC;
    std::unique_ptr<SliderAtt> aOut; std::unique_ptr<ButtonAtt> aAuto, aDelta, aSc, aPh, aFr; std::unique_ptr<ComboAtt> aFft, aView, aTilt, aRng;
};

// ------------------------------------------------------------------------------------------ Smart Ops
class SmartPanel : public juce::Component
{
public:
    SmartPanel (PrimeqProcessor& p, EqGraph& g) : proc (p), graph (g)
    {
        using namespace pqui;
        opBox.addItem ("Deresonate", 1); opBox.setSelectedId (1, juce::dontSendNotification); addAndMakeVisible (opBox);
        for (auto* b : { &learnB, &stopB, &applyB, &revertB }) addAndMakeVisible (b);
        auto sl = [this] (juce::Slider& s, double lo, double hi, double v, double step, const char* suf) {
            s.setSliderStyle (juce::Slider::LinearHorizontal); s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 18); s.setRange (lo, hi, step); s.setValue (v, juce::dontSendNotification);
            s.setTextValueSuffix (suf); s.setColour (juce::Slider::thumbColourId, kTeal); s.setColour (juce::Slider::trackColourId, kTealDim); s.setColour (juce::Slider::backgroundColourId, kLine);
            s.onValueChange = [this] { recompute(); }; addAndMakeVisible (s); };
        sl (sens, 2.0, 12.0, 4.0, 0.5, " dB"); sl (maxCut, 3.0, 18.0, 9.0, 0.5, " dB"); sl (maxBands, 1, 8, 6, 1, "");
        for (auto* l : { &lSens, &lCut, &lBands, &status, &note }) { l->setColour (juce::Label::textColourId, kDim); l->setFont (fontOf (11.f)); addAndMakeVisible (l); }
        lSens.setText (T8 ("Sensibilidade"), juce::dontSendNotification); lCut.setText (T8 ("Corte máx."), juce::dontSendNotification); lBands.setText (T8 ("Bandas máx."), juce::dontSendNotification);
        note.setText (T8 ("Sequência: Learn → Stop → Preview → Apply → Revert. Nenhum áudio é alterado até Apply. Deteção por picos locais (determinística, não é IA): sons tonais sustentados podem dar falsos positivos."), juce::dontSendNotification);
        note.setMinimumHorizontalScale (1.f);
        learnB.onClick = [this] { proc.analyzer.learnStart(); graph.showPreview = false; graph.preview.clear(); learned = false; setStatus (T8 ("A aprender… toque o material mais denso (≥ 2 s).")); };
        stopB.onClick = [this] { if (! proc.analyzer.isLearning()) return; proc.analyzer.learnStop(); learned = true; recompute(); };
        applyB.onClick = [this] { if (graph.preview.empty()) return; const int n = proc.smartApply (graph.preview); graph.showPreview = false; setStatus (juce::String (n) + T8 (" banda(s) criada(s). Revert desfaz.")); };
        revertB.onClick = [this] { proc.smartRevert(); setStatus (T8 ("Revertido.")); };
        setStatus (T8 ("Escolha a operação e carregue em Learn."));
    }
    void tick()
    {
        auto& d = proc.analyzer.deresonate();
        if (proc.analyzer.isLearning()) setStatus (T8 ("A aprender: ") + juce::String (d.learnedSeconds(), 1) + " s" + (d.ready() ? T8 ("  — pode parar") : T8 ("  (mín. 2 s)")));
        stopB.setEnabled (proc.analyzer.isLearning()); learnB.setEnabled (! proc.analyzer.isLearning());
        applyB.setEnabled (! graph.preview.empty() && ! proc.analyzer.isLearning()); revertB.setEnabled (proc.smartCanRevert());
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 10); r.removeFromTop (22);
        opBox.setBounds (r.removeFromTop (28)); r.removeFromTop (8);
        auto row = r.removeFromTop (28); learnB.setBounds (row.removeFromLeft (row.getWidth() / 2 - 3)); row.removeFromLeft (6); stopB.setBounds (row);
        r.removeFromTop (6); row = r.removeFromTop (28); applyB.setBounds (row.removeFromLeft (row.getWidth() / 2 - 3)); row.removeFromLeft (6); revertB.setBounds (row);
        r.removeFromTop (12);
        for (auto pr : { std::pair<juce::Label*, juce::Slider*> { &lSens, &sens }, { &lCut, &maxCut }, { &lBands, &maxBands } })
        { pr.first->setBounds (r.removeFromTop (16)); pr.second->setBounds (r.removeFromTop (24)); r.removeFromTop (4); }
        r.removeFromTop (6); status.setBounds (r.removeFromTop (34)); status.setMinimumHorizontalScale (1.f); r.removeFromTop (6); note.setBounds (r);
    }
    void paint (juce::Graphics& g) override
    {
        using namespace pqui; g.setColour (kPanel); g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.f);
        g.setColour (kText); g.setFont (fontOf (13.f, true)); g.drawText ("SMART OPS", 12, 8, 200, 20, juce::Justification::centredLeft);
    }
private:
    void recompute()
    {
        if (! learned) return;
        graph.preview = proc.analyzer.deresonate().propose (sens.getValue(), maxCut.getValue(), (int) maxBands.getValue());
        graph.showPreview = ! graph.preview.empty(); graph.repaint();
        juce::String s = juce::String ((int) graph.preview.size()) + T8 (" ressonância(s): ");
        for (size_t i = 0; i < graph.preview.size() && i < 4; ++i) s << pq::units::formatFreq (graph.preview[i].freq) << " (" << juce::String (graph.preview[i].gainDb, 1) << " dB)  ";
        setStatus (graph.preview.empty() ? T8 ("Nada acima da sensibilidade (ou captura < 2 s).") : s);
    }
    void setStatus (const juce::String& s) { status.setText (s, juce::dontSendNotification); }
    PrimeqProcessor& proc; EqGraph& graph; bool learned = false;
    juce::ComboBox opBox; juce::TextButton learnB { "Learn" }, stopB { "Stop" }, applyB { "Apply" }, revertB { "Revert" };
    juce::Slider sens, maxCut, maxBands; juce::Label lSens, lCut, lBands, status, note;
};

// ------------------------------------------------------------------------------------------ login
class LoginOverlay : public juce::Component
{
public:
    explicit LoginOverlay (std::function<void()> onSuccess) : onOk (std::move (onSuccess))
    {
        using namespace pqui;
        pw.setPasswordCharacter ((juce::juce_wchar) 0x2022); pw.setTextToShowWhenEmpty ("Senha de acesso", kDim); pw.setJustification (juce::Justification::centred);
        pw.setFont (fontOf (16.f)); pw.onReturnKey = [this] { submit(); };
        remember.setToggleState (true, juce::dontSendNotification); remember.setButtonText (T8 ("Lembrar neste computador"));
        enter.setButtonText ("Entrar"); enter.onClick = [this] { submit(); };
        msg.setJustificationType (juce::Justification::centred); msg.setFont (fontOf (12.f)); msg.setColour (juce::Label::textColourId, kRed);
        addAndMakeVisible (pw); addAndMakeVisible (remember); addAndMakeVisible (enter); addAndMakeVisible (msg);
    }
    void resized() override
    {
        auto r = getLocalBounds().withSizeKeepingCentre (380, 300).reduced (30, 0);
        pw.setBounds (r.getX(), r.getY() + 110, r.getWidth(), 40); remember.setBounds (r.getX(), r.getY() + 160, r.getWidth(), 28);
        enter.setBounds (r.getX(), r.getY() + 196, r.getWidth(), 40); msg.setBounds (r.getX() - 20, r.getY() + 242, r.getWidth() + 40, 20);
    }
    void paint (juce::Graphics& g) override
    {
        using namespace pqui;
        g.fillAll (kBg.withAlpha (0.97f));
        auto card = getLocalBounds().withSizeKeepingCentre (380, 300).toFloat();
        g.setColour (kPanel); g.fillRoundedRectangle (card, 18.f); g.setColour (kLine); g.drawRoundedRectangle (card.reduced (0.5f), 18.f, 1.f);
        g.setGradientFill (juce::ColourGradient (kTeal, card.getX(), card.getY(), juce::Colour (0xffe6e9ec), card.getRight(), card.getY(), false));
        g.setFont (fontOf (34.f, true)); g.drawText ("PRIMEQ pro", card.withTrimmedTop (28.f).withHeight (46.f).toNearestInt(), juce::Justification::centred);
        g.setColour (kDim); g.setFont (fontOf (12.f)); g.drawText (T8 ("by Piradex  ·  Parametric + Dynamic EQ"), card.withTrimmedTop (76.f).withHeight (18.f).toNearestInt(), juce::Justification::centred);
    }
private:
    void submit()
    {
        if (busy) return;
        const juce::String pass = pw.getText();
        if (pass.trim().isEmpty()) { msg.setText ("Escreva a senha.", juce::dontSendNotification); return; }
        busy = true; enter.setEnabled (false); msg.setColour (juce::Label::textColourId, pqui::kDim); msg.setText ("A verificar...", juce::dontSendNotification);
        const bool rem = remember.getToggleState(); juce::Component::SafePointer<LoginOverlay> safe (this);
        juce::Thread::launch ([safe, pass, rem]
        {
            const auto m = AccessManager::matchPassword (pass);   // PBKDF2 fora da thread de mensagens
            juce::MessageManager::callAsync ([safe, m, rem]
            {
                if (safe == nullptr) return;
                const auto res = AccessManager::get().applyLogin (m, rem);
                safe->busy = false; safe->enter.setEnabled (true); safe->msg.setColour (juce::Label::textColourId, pqui::kRed);
                if (res == AccessManager::LoginResult::Ok) { safe->pw.clear(); safe->msg.setText ({}, juce::dontSendNotification); if (safe->onOk) safe->onOk(); }
                else if (res == AccessManager::LoginResult::Expired) safe->msg.setText (pqui::T8 ("Esta senha beta já expirou."), juce::dontSendNotification);
                else safe->msg.setText ("Senha incorreta.", juce::dontSendNotification);
            });
        });
    }
    std::function<void()> onOk; juce::TextEditor pw; juce::ToggleButton remember; juce::TextButton enter; juce::Label msg; bool busy = false;
};

// ------------------------------------------------------------------------------------------ editor
class PrimeqEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PrimeqEditor (PrimeqProcessor&);
    ~PrimeqEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    EqGraph& getGraph() { return graph; }
    void selectBand (int b) { graph.select (b); }
    void showSmart (bool s) { smartBtn.setToggleState (s, juce::dontSendNotification); smart.setVisible (s); resized(); }
private:
    void timerCallback() override;
    void presetMenu();
    PrimeqProcessor& proc; pqui::Lnf lnf;
    EqGraph graph; BandPanel band; GlobalStrip strip; SmartPanel smart; LoginOverlay login;
    juce::TextButton presetBtn { "Presets" }, undoBtn { "Undo" }, redoBtn { "Redo" }, abBtn { "A" }, logoutBtn { "Sair" };
    juce::ToggleButton bypassBtn { "BYPASS" }, smartBtn { "SMART OPS" }; juce::ComboBox phaseBox; juce::Label phaseLbl;
    std::unique_ptr<ComboAtt> aPhase; std::unique_ptr<ButtonAtt> aBypass;
    juce::String presetName { "Init (Flat)" }; bool wasUnlocked = true; int lastSel = -1;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PrimeqEditor)
};
