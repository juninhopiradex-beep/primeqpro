// =============================================================================
//  PRIMEQ PRO by Piradex — editor: cabecalho, grafico, inspector de banda, tira global, Smart Ops, login
// =============================================================================
#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include "LookAndFeel.h"
#include "EqGraph.h"
#include "../Shared/Units.h"
#include "../SmartOps/ReferenceMatch.h"
#include "MixPanel.h"

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

// ------------------------------------------------------------------------------------------ De-harsh / Character / Lookahead
class FxPanel : public juce::Component
{
public:
    explicit FxPanel (PrimeqProcessor& p) : proc (p)
    {
        using namespace pqui;
        auto knob = [this] (Knob& k, const char* name, const char* id, juce::Colour c, std::function<juce::String (double)> fmt, std::function<double (const juce::String&)> parse) {
            k.s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag); k.s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 62, 15);
            k.s.setColour (juce::Slider::rotarySliderFillColourId, c); k.s.getProperties().set ("bipolar", false);
            k.s.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
            k.l.setText (name, juce::dontSendNotification); k.l.setJustificationType (juce::Justification::centred); k.l.setColour (juce::Label::textColourId, kDim); k.l.setFont (fontOf (10.f));
            addAndMakeVisible (k.s); addAndMakeVisible (k.l);
            k.att = std::make_unique<SliderAtt> (proc.apvts, id, k.s); k.s.textFromValueFunction = fmt; k.s.valueFromTextFunction = parse; k.s.updateText();
            if (auto* pp = proc.apvts.getParameter (id)) k.s.setDoubleClickReturnValue (true, pp->convertFrom0to1 (pp->getDefaultValue()));
        };
        const auto pct = [] (double v) { return juce::String (v, 0) + " %"; }; const auto pPct = [] (const juce::String& t) { return pq::units::parsePlain (t.toStdString(), 50.0); };
        const auto hz = [] (double v) { return juce::String (pq::units::formatFreq (v)); }; const auto pHz = [] (const juce::String& t) { return pq::units::parseFreq (t.toStdString(), 2500.0); };
        const auto db = [] (double v) { return juce::String (v, 1) + " dB"; }; const auto pDb = [] (const juce::String& t) { return pq::units::parseDb (t.toStdString(), 4.0); };
        const auto ms = [] (double v) { return v >= 100 ? juce::String (v, 0) + " ms" : juce::String (v, 1) + " ms"; }; const auto pMs = [] (const juce::String& t) { return pq::units::parseMs (t.toStdString(), 10.0); };
        knob (dhAmt, "AMOUNT", "dh_amount", kWarn, pct, pPct); knob (dhLo, "DE", "dh_lo", kWarn, hz, pHz); knob (dhHi, "ATÉ", "dh_hi", kWarn, hz, pHz);
        knob (dhSens, "TOLERÂNCIA", "dh_sens", kWarn, db, pDb); knob (dhAtk, "ATTACK", "dh_attack", kWarn, ms, pMs); knob (dhRel, "RELEASE", "dh_release", kWarn, ms, pMs);
        knob (chDrive, "DRIVE", "ch_drive", kTeal, pct, pPct); knob (chMix, "MIX", "ch_mix", kTeal, pct, pPct);
        for (auto* b : { &dhOn, &dhDelta }) addAndMakeVisible (b);
        aDhOn = std::make_unique<ButtonAtt> (proc.apvts, "dh_on", dhOn); aDhDelta = std::make_unique<ButtonAtt> (proc.apvts, "dh_delta", dhDelta);
        dhOn.setButtonText ("DE-HARSH"); dhDelta.setButtonText ("DELTA");
        dhOn.setTooltip (T8 ("Atenua picos espectrais ásperos (STFT, +2048 amostras de latência)")); dhDelta.setTooltip (T8 ("Ouve apenas o que o De-harsh remove"));
        auto combo = [this] (juce::ComboBox& c, std::unique_ptr<ComboAtt>& a, const char* id, juce::Label& l, const char* cap) {
            if (auto* pc = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (id))) c.addItemList (pc->choices, 1);
            addAndMakeVisible (c); a = std::make_unique<ComboAtt> (proc.apvts, id, c);
            l.setText (cap, juce::dontSendNotification); l.setFont (fontOf (10.f)); l.setColour (juce::Label::textColourId, kDim); addAndMakeVisible (l); };
        combo (chMode, aChMode, "ch_mode", lMode, "CHARACTER"); combo (chOs, aChOs, "ch_os", lOs, "OVERSAMPLING"); combo (look, aLook, "look", lLook, "LOOKAHEAD");
        info.setColour (juce::Label::textColourId, kDim); info.setFont (fontOf (10.f)); info.setJustificationType (juce::Justification::centredLeft); addAndMakeVisible (info);
        chMode.setTooltip (T8 ("Clean: só harmónicos ímpares, muito linear · Subtle: 2.ª harmónica suave · Warm: mais saturação e agudos suavizados. Circuitos originais — não emulam hardware."));
        look.setTooltip (T8 ("Atrasa o áudio para os detetores dinâmicos reagirem antes do transiente (adiciona latência)"));
    }
    void tick()
    {
        const float r = proc.dhRedDb.load();
        info.setText (dhOn.getToggleState() ? T8 ("Redução máx. agora: ") + juce::String (r, 1) + " dB" : T8 ("De-harsh desligado"), juce::dontSendNotification);
        for (auto* k : { &dhAmt, &dhLo, &dhHi, &dhSens, &dhAtk, &dhRel }) k->s.setEnabled (dhOn.getToggleState());
        const bool chOn = chMode.getSelectedItemIndex() > 0; chDrive.s.setEnabled (chOn); chMix.s.setEnabled (chOn); chOs.setEnabled (chOn);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 6);
        auto left = r.removeFromLeft (r.getWidth() * 0.57f); r.removeFromLeft (10);
        { auto top = left.removeFromTop (22); dhOn.setBounds (top.removeFromLeft (100)); top.removeFromLeft (6); dhDelta.setBounds (top.removeFromLeft (76)); top.removeFromLeft (10); info.setBounds (top);
          const int w = left.getWidth() / 6; int x = left.getX();
          for (auto* k : { &dhAmt, &dhLo, &dhHi, &dhSens, &dhAtk, &dhRel }) { auto c = juce::Rectangle<int> (x, left.getY(), w, left.getHeight()); k->l.setBounds (c.removeFromTop (12)); k->s.setBounds (c); x += w; } }
        auto mid = r.removeFromLeft (r.getWidth() * 0.66f); r.removeFromLeft (10);
        { auto top = mid.removeFromTop (22); lMode.setBounds (top.removeFromLeft (70)); chMode.setBounds (top.removeFromLeft (92)); top.removeFromLeft (8); lOs.setBounds (top.removeFromLeft (92)); chOs.setBounds (top.removeFromLeft (60));
          const int w = mid.getWidth() / 2; int x = mid.getX();
          for (auto* k : { &chDrive, &chMix }) { auto c = juce::Rectangle<int> (x, mid.getY(), w, mid.getHeight()); k->l.setBounds (c.removeFromTop (12)); k->s.setBounds (c); x += w; } }
        { lLook.setBounds (r.removeFromTop (22).withTrimmedTop (4)); look.setBounds (r.removeFromTop (26)); }
    }
    void paint (juce::Graphics& g) override
    {
        using namespace pqui; g.setColour (kPanel); g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.f);
        g.setColour (kLine); g.drawVerticalLine ((int) (getWidth() * 0.57f + 6), 10.f, (float) getHeight() - 10.f); g.drawVerticalLine ((int) (getWidth() * 0.57f + 10 + (getWidth() - 20) * 0.43f * 0.66f + 5), 10.f, (float) getHeight() - 10.f);
    }
private:
    struct Knob { juce::Slider s; juce::Label l; std::unique_ptr<SliderAtt> att; };
    PrimeqProcessor& proc; Knob dhAmt, dhLo, dhHi, dhSens, dhAtk, dhRel, chDrive, chMix;
    juce::ToggleButton dhOn, dhDelta; juce::ComboBox chMode, chOs, look; juce::Label info, lMode, lOs, lLook;
    std::unique_ptr<ButtonAtt> aDhOn, aDhDelta; std::unique_ptr<ComboAtt> aChMode, aChOs, aLook;
};

// ------------------------------------------------------------------------------------------ Smart Ops
class SmartPanel : public juce::Component
{
public:
    enum Op { Deres = 1, DeEssOp, MatchOp, UnmaskOp, MorphOp };

    SmartPanel (PrimeqProcessor& p, EqGraph& g) : proc (p), graph (g)
    {
        using namespace pqui;
        opBox.addItem ("Deresonate", Deres); opBox.addItem ("De-Ess", DeEssOp); opBox.addItem ("Reference Match", MatchOp); opBox.addItem ("Unmask (sidechain)", UnmaskOp); opBox.addItem ("Morph A \xE2\x86\x94 B", MorphOp);
        opBox.setSelectedId (Deres, juce::dontSendNotification); opBox.onChange = [this] { configureOp(); }; addAndMakeVisible (opBox);
        for (auto* b : { &learnB, &stopB, &applyB, &revertB, &loadRefB, &capA, &capB, &clearMorphB }) addAndMakeVisible (b);
        addAndMakeVisible (morphOnB); morphOnB.setButtonText ("MORPH ATIVO"); aMorphOn = std::make_unique<ButtonAtt> (proc.apvts, "morph_on", morphOnB);
        morphS.setSliderStyle (juce::Slider::LinearHorizontal); morphS.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 18); morphS.setColour (juce::Slider::thumbColourId, kTeal); morphS.setColour (juce::Slider::trackColourId, kTealDim); morphS.setColour (juce::Slider::backgroundColourId, kLine);
        addAndMakeVisible (morphS); aMorph = std::make_unique<SliderAtt> (proc.apvts, "morph", morphS); morphS.setTextValueSuffix (" % B");
        auto sl = [this] (juce::Slider& s) {
            s.setSliderStyle (juce::Slider::LinearHorizontal); s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 18);
            s.setColour (juce::Slider::thumbColourId, kTeal); s.setColour (juce::Slider::trackColourId, kTealDim); s.setColour (juce::Slider::backgroundColourId, kLine);
            s.onValueChange = [this] { recompute(); }; addAndMakeVisible (s); };
        sl (sens); sl (maxCut); sl (maxBands);
        for (auto* l : { &lSens, &lCut, &lBands, &status, &note, &lMorph }) { l->setColour (juce::Label::textColourId, kDim); l->setFont (fontOf (11.f)); addAndMakeVisible (l); }
        note.setMinimumHorizontalScale (1.f);
        learnB.onClick = [this]
        {
            proc.analyzer.learnStart(); graph.showPreview = false; graph.preview.clear(); learned = false;
            setStatus (op() == UnmaskOp ? T8 ("A aprender… toque a pista E o sidechain ao mesmo tempo (≥ 4 s).") : (op() == DeEssOp ? T8 ("A aprender… toque a voz com sibilância (≥ 4 s).") : T8 ("A aprender… toque o material mais denso (≥ 2 s).")));
        };
        stopB.onClick = [this] { if (! proc.analyzer.isLearning()) return; proc.analyzer.learnStop(); learned = true; recompute(); };
        applyB.onClick = [this] { if (graph.preview.empty()) return; const int n = proc.smartApply (graph.preview, opBox.getText().toRawUTF8()); graph.showPreview = false; setStatus (juce::String (n) + T8 (" banda(s) criada(s). Revert desfaz.")); };
        revertB.onClick = [this] { proc.smartRevert(); setStatus (T8 ("Revertido.")); };
        loadRefB.onClick = [this] { chooseReference(); };
        capA.onClick = [this] { proc.morphCapture (0); refreshMorphStatus(); }; capB.onClick = [this] { proc.morphCapture (1); refreshMorphStatus(); };
        clearMorphB.onClick = [this] { proc.morphClear(); proc.setParamReal ("morph_on", 0.f); refreshMorphStatus(); };
        capA.setTooltip (T8 ("Guarda as 24 bandas atuais como estado A")); capB.setTooltip (T8 ("Guarda as 24 bandas atuais como estado B"));
        configureOp();
    }
    int op() const { return opBox.getSelectedId(); }

    void tick()
    {
        auto& an = proc.analyzer; const bool learning = an.isLearning();
        if (learning) setStatus (T8 ("A aprender: ") + juce::String (learnSeconds(), 1) + " s" + (opReady() ? T8 ("  — pode parar") : T8 ("  (ainda a recolher…)")) + (op() == UnmaskOp ? (an.unmaskEngine().hasSidechain() ? T8 ("  · sidechain OK") : T8 ("  · SIDECHAIN NÃO DETETADO")) : juce::String()));
        stopB.setEnabled (learning); learnB.setEnabled (! learning);
        applyB.setEnabled (! graph.preview.empty() && ! learning); revertB.setEnabled (proc.smartCanRevert());
        if (op() == MorphOp) refreshMorphStatus();
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 10); r.removeFromTop (22);
        opBox.setBounds (r.removeFromTop (28)); r.removeFromTop (8);
        const bool morph = op() == MorphOp, match = op() == MatchOp;
        for (auto* c : std::initializer_list<juce::Component*> { &learnB, &stopB, &applyB, &revertB, &sens, &maxCut, &maxBands, &lSens, &lCut, &lBands }) c->setVisible (! morph);
        for (auto* c : std::initializer_list<juce::Component*> { &capA, &capB, &clearMorphB, &morphOnB, &morphS, &lMorph }) c->setVisible (morph);
        loadRefB.setVisible (match);
        if (morph)
        {
            auto row = r.removeFromTop (28); capA.setBounds (row.removeFromLeft (row.getWidth() / 2 - 3)); row.removeFromLeft (6); capB.setBounds (row);
            r.removeFromTop (8); morphOnB.setBounds (r.removeFromTop (26)); r.removeFromTop (6); morphS.setBounds (r.removeFromTop (26)); r.removeFromTop (6);
            clearMorphB.setBounds (r.removeFromTop (26)); r.removeFromTop (8); lMorph.setBounds (r.removeFromTop (34)); r.removeFromTop (6);
            status.setBounds (0, 0, 0, 0); note.setBounds (r); return;
        }
        if (match) { loadRefB.setBounds (r.removeFromTop (28)); r.removeFromTop (6); }
        auto row = r.removeFromTop (28); learnB.setBounds (row.removeFromLeft (row.getWidth() / 2 - 3)); row.removeFromLeft (6); stopB.setBounds (row);
        r.removeFromTop (6); row = r.removeFromTop (28); applyB.setBounds (row.removeFromLeft (row.getWidth() / 2 - 3)); row.removeFromLeft (6); revertB.setBounds (row);
        r.removeFromTop (10);
        const bool showBands = op() != DeEssOp; maxBands.setVisible (showBands); lBands.setVisible (showBands);
        for (auto pr : { std::pair<juce::Label*, juce::Slider*> { &lSens, &sens }, { &lCut, &maxCut }, { &lBands, &maxBands } })
        { if (pr.second == &maxBands && ! showBands) continue; pr.first->setBounds (r.removeFromTop (16)); pr.second->setBounds (r.removeFromTop (24)); r.removeFromTop (4); }
        r.removeFromTop (4); status.setBounds (r.removeFromTop (46)); status.setMinimumHorizontalScale (1.f); r.removeFromTop (4); note.setBounds (r);
    }
    void paint (juce::Graphics& g) override
    {
        using namespace pqui; g.setColour (kPanel); g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.f);
        g.setColour (kText); g.setFont (fontOf (13.f, true)); g.drawText ("SMART OPS", 12, 8, 200, 20, juce::Justification::centredLeft);
    }
private:
    double learnSeconds() const
    {
        auto& an = proc.analyzer;
        switch (op()) { case DeEssOp: return an.deEss().seconds(); case UnmaskOp: return an.unmaskEngine().seconds(); default: return an.deresonate().learnedSeconds(); }
    }
    bool opReady() const
    {
        auto& an = proc.analyzer;
        switch (op()) { case DeEssOp: return an.deEss().ready(); case UnmaskOp: return an.unmaskEngine().ready(); case MatchOp: return an.deresonate().learnedSeconds() >= 8.0; default: return an.deresonate().ready(); }
    }
    void configureOp()
    {
        using namespace pqui;
        graph.showPreview = false; graph.preview.clear(); learned = false;
        auto cfg = [&] (juce::Slider& s, juce::Label& l, const char* name, double lo, double hi, double v, double step, const char* suf) { l.setText (T8 (name), juce::dontSendNotification); s.setRange (lo, hi, step); s.setValue (v, juce::dontSendNotification); s.setTextValueSuffix (suf); };
        switch (op())
        {
            case Deres:
                cfg (sens, lSens, "Sensibilidade", 2.0, 12.0, 4.0, 0.5, " dB"); cfg (maxCut, lCut, "Corte máx.", 3.0, 18.0, 9.0, 0.5, " dB"); cfg (maxBands, lBands, "Bandas máx.", 1, 8, 6, 1, "");
                note.setText (T8 ("Sequência: Learn → Stop → Preview → Apply → Revert. Nenhum áudio é alterado até Apply. Deteção por picos locais (determinística, não é IA): sons tonais sustentados podem dar falsos positivos."), juce::dontSendNotification); break;
            case DeEssOp:
                cfg (sens, lSens, "Limiar (percentil)", 50.0, 90.0, 75.0, 1.0, " %"); cfg (maxCut, lCut, "Redução máx.", 3.0, 15.0, 9.0, 0.5, " dB");
                note.setText (T8 ("Aprende a sibilância da voz (3,5–12 kHz) e cria UMA banda Bell dinâmica, editável. Falsos positivos: pratos e hi-hats podem ser lidos como sibilância. Sem rajadas claras não propõe nada."), juce::dontSendNotification); break;
            case MatchOp:
                cfg (sens, lSens, "Força", 10.0, 100.0, 70.0, 1.0, " %"); cfg (maxCut, lCut, "Ganho máx.", 3.0, 15.0, 9.0, 0.5, " dB"); cfg (maxBands, lBands, "Bandas máx.", 1, 8, 6, 1, "");
                note.setText (T8 ("1) Carregue uma referência (WAV/AIFF/FLAC). 2) Learn com o seu material (≥ 8 s). 3) Stop → Preview → Apply. Iguala o balanço tonal médio (não o volume, a dinâmica nem o estéreo). Recomenda-se força < 100 %."), juce::dontSendNotification); break;
            case UnmaskOp:
                cfg (sens, lSens, "Conflito mínimo", 1.0, 8.0, 2.0, 0.5, " dB"); cfg (maxCut, lCut, "Corte máx.", 2.0, 12.0, 6.0, 0.5, " dB"); cfg (maxBands, lBands, "Bandas máx.", 1, 6, 4, 1, "");
                note.setText (T8 ("Envie a pista que MASCARA para a entrada de sidechain deste plugin (no DAW) e toque ambas durante o Learn. Cria cortes dinâmicos que só atuam quando o sidechain está presente. Não há comunicação entre instâncias; Auto Gain/Output compensam o volume."), juce::dontSendNotification); break;
            case MorphOp:
                lMorph.setText (T8 ("Guarde o EQ atual como A, mude-o, guarde como B e ative o Morph. O cursor interpola as bandas (freq/Q geométricas, ganho linear)."), juce::dontSendNotification);
                note.setText (T8 ("Bandas que só existem num dos estados entram/saem suavemente (Bell/shelf) ou comutam a 50 % (cortes). Todos os parâmetros são automatizáveis."), juce::dontSendNotification); refreshMorphStatus(); break;
        }
        status.setText (op() == MorphOp ? juce::String() : T8 ("Escolha a operação e carregue em Learn."), juce::dontSendNotification);
        resized(); repaint();
    }
    void refreshMorphStatus()
    {
        const bool a = proc.morphHas (0), b = proc.morphHas (1);
        lMorph.setText (T8 ("Estado A: ") + (a ? T8 ("guardado") : T8 ("vazio")) + T8ish ("   ·   Estado B: ") + (b ? T8 ("guardado") : T8 ("vazio")) + (a && b ? T8ish ("\nPronto: ative o MORPH.") : T8ish ("\nGuarde A e B para ativar.")), juce::dontSendNotification);
    }
    static juce::String T8ish (const char* s) { return pqui::T8 (s); }
    void chooseReference()
    {
        chooser = std::make_unique<juce::FileChooser> (T8 ("Escolha a referência (WAV, AIFF ou FLAC)"), juce::File(), "*.wav;*.aif;*.aiff;*.flac");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult(); if (! f.existsAsFile()) return;
            setStatus (T8 ("A analisar a referência…"));
            juce::Component::SafePointer<SmartPanel> safe (this); PrimeqProcessor* pr = &proc;
            juce::Thread::launch ([safe, f, pr]
            {
                juce::AudioFormatManager fm; fm.registerBasicFormats();
                std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f));
                std::vector<double> db; juce::String err; double secs = 0;
                if (rd == nullptr || rd->lengthInSamples < (juce::int64) rd->sampleRate * 3) err = T8 ("Ficheiro ilegível ou com menos de 3 s.");
                else
                {
                    pq::RefAnalyzer ra; ra.begin (rd->sampleRate); juce::AudioBuffer<float> buf ((int) rd->numChannels, 65536); std::vector<float> mono (65536);
                    const juce::int64 maxN = std::min<juce::int64> (rd->lengthInSamples, (juce::int64) (rd->sampleRate * 600.0));
                    for (juce::int64 pos = 0; pos < maxN; pos += 65536)
                    {
                        const int n = (int) std::min<juce::int64> (65536, maxN - pos); rd->read (&buf, 0, n, pos, true, true);
                        for (int i = 0; i < n; ++i) { float s = 0; for (int c = 0; c < buf.getNumChannels(); ++c) s += buf.getSample (c, i); mono[(size_t) i] = s / (float) std::max (1, buf.getNumChannels()); }
                        ra.feed (mono.data(), n);
                    }
                    secs = ra.seconds(); db = ra.logDb();
                }
                juce::MessageManager::callAsync ([safe, db, err, f, secs, pr]
                {
                    if (! err.isEmpty()) { if (safe != nullptr) safe->setStatus (err); return; }
                    pr->matchRefDb = db; pr->matchRefName = f.getFileName();
                    if (safe != nullptr) { safe->setStatus (T8 ("Referência: ") + f.getFileName() + " (" + juce::String (secs, 0) + " s). Agora Learn."); safe->recompute(); }
                });
            });
        });
    }
    void recompute()
    {
        if (! learned) return;
        auto& an = proc.analyzer; juce::String head;
        switch (op())
        {
            case Deres: graph.preview = an.deresonate().propose (sens.getValue(), maxCut.getValue(), (int) maxBands.getValue()); head = T8 (" ressonância(s): "); break;
            case DeEssOp: graph.preview = an.deEss().propose (sens.getValue() / 100.0, maxCut.getValue()); head = T8 (" banda de de-ess: "); break;
            case UnmaskOp: graph.preview = an.unmaskEngine().propose (sens.getValue(), maxCut.getValue(), (int) maxBands.getValue()); head = T8 (" corte(s) dinâmico(s): "); break;
            case MatchOp:
                graph.preview.clear();
                if (proc.matchRefDb.empty()) { setStatus (T8 ("Falta carregar a referência.")); graph.showPreview = false; graph.repaint(); return; }
                graph.preview = pq::matchPropose (proc.matchRefDb, pq::toLogDb (an.deresonate().meanPower(), an.sampleRate()), sens.getValue() / 100.0, (int) maxBands.getValue(), maxCut.getValue(), an.sampleRate());
                head = T8 (" banda(s) para igualar ") + proc.matchRefName + ": "; break;
            default: return;
        }
        graph.showPreview = ! graph.preview.empty(); graph.repaint();
        juce::String s = juce::String ((int) graph.preview.size()) + head;
        for (size_t i = 0; i < graph.preview.size() && i < 4; ++i) s << pq::units::formatFreq (graph.preview[i].freq) << " (" << juce::String (graph.preview[i].gainDb, 1) << " dB)  ";
        setStatus (graph.preview.empty() ? T8 ("Nada a propor (material insuficiente, sem sibilância/conflito, ou abaixo da sensibilidade).") : s);
    }
    void setStatus (const juce::String& s) { status.setText (s, juce::dontSendNotification); }
    PrimeqProcessor& proc; EqGraph& graph; bool learned = false;
    juce::ComboBox opBox; juce::TextButton learnB { "Learn" }, stopB { "Stop" }, applyB { "Apply" }, revertB { "Revert" }, loadRefB { "Carregar refer\xC3\xAAncia..." }, capA { "Guardar A" }, capB { "Guardar B" }, clearMorphB { "Limpar A/B" };
    juce::ToggleButton morphOnB; juce::Slider sens, maxCut, maxBands, morphS; juce::Label lSens, lCut, lBands, status, note, lMorph;
    std::unique_ptr<ButtonAtt> aMorphOn; std::unique_ptr<SliderAtt> aMorph; std::unique_ptr<juce::FileChooser> chooser;
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
        g.setFont (fontOf (34.f, true)); g.drawText ("PRIMEQ PRO", card.withTrimmedTop (28.f).withHeight (46.f).toNearestInt(), juce::Justification::centred);
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
    void showMix (bool s) { mixBtn.setToggleState (s, juce::dontSendNotification); mix.setVisible (s); for (auto* c : std::initializer_list<juce::Component*> { &graph, &band, &strip }) c->setVisible (! s); fx.setVisible (! s && proc.uiMode != 0); if (s) smart.setVisible (false); else smart.setVisible (smartBtn.getToggleState()); resized(); }
    MixPanel& getMix() { return mix; }
    void showSmart (bool s) { smartBtn.setToggleState (s, juce::dontSendNotification); smart.setVisible (s && ! mix.isVisible()); resized(); }
private:
    void timerCallback() override;
    void applyMode();
    void presetMenu();
    PrimeqProcessor& proc; pqui::Lnf lnf;
    EqGraph graph; BandPanel band; GlobalStrip strip; FxPanel fx; SmartPanel smart; MixPanel mix; LoginOverlay login;
    juce::TextButton presetBtn { "Presets" }, undoBtn { "Undo" }, redoBtn { "Redo" }, abBtn { "A" }, logoutBtn { "Sair" };
    juce::ToggleButton bypassBtn { "BYPASS" }, smartBtn { "SMART OPS" }, mixBtn { "ASSISTENTE" }; juce::ComboBox modeBox; juce::ComboBox phaseBox; juce::Label phaseLbl;
    std::unique_ptr<ComboAtt> aPhase; std::unique_ptr<ButtonAtt> aBypass;
    juce::String presetName { "Init (Flat)" }; bool wasUnlocked = true; int lastSel = -1;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PrimeqEditor)
};
