// =============================================================================
//  PrimEQ Pro by Piradex — identidade visual: grafite mate, teal discreto, curvas de banda subtis
// =============================================================================
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>
#include <algorithm>
#include <atomic>
#include <string>
#include <unordered_map>

namespace pqui
{
// --- skins: paleta global mutavel (so a thread de mensagens pinta). 0 = Prime (teal), 1 = Ouro, 2 = Indigo (seguro para daltonismo) ---
struct Palette
{
    juce::uint32 bg, panel, panel2, line, grid, text, dim, accent, accentDim, warn, red; juce::uint32 band[12];
    const char* name;
};
inline const Palette& paletteOf (int i)
{
    static const Palette p[3] = {
        { 0xff101215, 0xff171a1e, 0xff1d2126, 0xff2a2f36, 0xff23282e, 0xffe4e8ec, 0xff8b939c, 0xff3cc9b8, 0xff1f6f66, 0xffe0a458, 0xffe5646f,
          { 0xff4fd1c5, 0xffe0a458, 0xffe07a8c, 0xff9b8cf0, 0xff63b3ed, 0xff9ccc65, 0xfff08a5d, 0xffd77fd6, 0xff56c2e6, 0xffe6c35c, 0xff74d9a0, 0xfff0867a }, "PRIME" },
        { 0xff12100e, 0xff1a1713, 0xff231f1a, 0xff352f27, 0xff2a251f, 0xffefe9de, 0xff9d9486, 0xffe3b341, 0xff7a5f1c, 0xff5fb8cf, 0xffe5646f,
          { 0xffe3b341, 0xff5fb8cf, 0xffe8806f, 0xffb59af0, 0xff8fc96a, 0xfff0a35c, 0xffd98fc4, 0xff6fd1b5, 0xfff0d27a, 0xff7aa8f0, 0xffd96f5f, 0xffa8d96f }, "OURO" },
        { 0xff0e1117, 0xff141922, 0xff1b2230, 0xff2a3345, 0xff222a39, 0xffe8ecf4, 0xff8f99ad, 0xff56b4e9, 0xff24577a, 0xffe69f00, 0xffd55e00,
          { 0xff56b4e9, 0xffe69f00, 0xff009e73, 0xfff0e442, 0xff3d8fd0, 0xffd55e00, 0xffcc79a7, 0xff8fd4f5, 0xfff3c14d, 0xff4fc9a0, 0xfff08a4d, 0xffe0a0c4 }, "INDIGO" } };
    return p[std::clamp (i, 0, 2)];
}
inline juce::Colour kBg, kPanel, kPanel2, kLine, kGrid, kText, kDim, kTeal, kTealDim, kWarn, kRed;
inline int& skinIndex() { static int s = -1; return s; }
inline void applySkin (int i)
{
    i = std::clamp (i, 0, 2); const auto& p = paletteOf (i); skinIndex() = i;
    kBg = juce::Colour (p.bg); kPanel = juce::Colour (p.panel); kPanel2 = juce::Colour (p.panel2); kLine = juce::Colour (p.line); kGrid = juce::Colour (p.grid);
    kText = juce::Colour (p.text); kDim = juce::Colour (p.dim); kTeal = juce::Colour (p.accent); kTealDim = juce::Colour (p.accentDim); kWarn = juce::Colour (p.warn); kRed = juce::Colour (p.red);
}
inline const bool skinInit = (applySkin (0), true);
inline const char* skinName (int i) { return paletteOf (i).name; }
inline juce::Colour bandColour (int b) { return juce::Colour (paletteOf (skinIndex() < 0 ? 0 : skinIndex()).band[((b % 12) + 12) % 12]); }
// troca de skin: reatribui as cores especificadas nos componentes (de oldSkin para a atual)
inline void remapTree (juce::Component& c, int oldSkin)
{
    const auto& o = paletteOf (oldSkin); const auto& n = paletteOf (skinIndex());
    const juce::uint32 ov[11] = { o.bg, o.panel, o.panel2, o.line, o.grid, o.text, o.dim, o.accent, o.accentDim, o.warn, o.red };
    const juce::uint32 nv[11] = { n.bg, n.panel, n.panel2, n.line, n.grid, n.text, n.dim, n.accent, n.accentDim, n.warn, n.red };
    static const int ids[] = { juce::Slider::rotarySliderFillColourId, juce::Slider::thumbColourId, juce::Slider::trackColourId, juce::Slider::backgroundColourId,
                               juce::Slider::textBoxTextColourId, juce::Label::textColourId, juce::ToggleButton::textColourId, juce::ToggleButton::tickColourId,
                               juce::TextButton::textColourOffId, juce::TextEditor::textColourId, juce::TextEditor::backgroundColourId, juce::TextEditor::outlineColourId };
    for (int id : ids)
        if (c.isColourSpecified (id)) { const auto cur = c.findColour (id).getARGB(); for (int k = 0; k < 11; ++k) if (cur == ov[k]) { c.setColour (id, juce::Colour (nv[k])); break; } }
    for (auto* ch : c.getChildren()) remapTree (*ch, oldSkin);
}
inline juce::Font fontOf (float h, bool bold = false) { return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain)); }
// --- i18n PT/EN: o texto-fonte esta em PT; em EN procura-se no dicionario (I18nEn.inc). Textos sem entrada ficam como estao. ---
namespace i18n
{
    inline std::atomic<int>& lang() { static std::atomic<int> l { 0 }; return l; }          // 0 = PT, 1 = EN
    inline bool& forcePt() { thread_local bool f = false; return f; }                       // usado durante a construcao do editor
    inline bool isEn() { return ! forcePt() && lang().load() == 1; }
    inline const char* lookup (const std::string& pt)
    {
        static const std::unordered_map<std::string, const char*> m = []
        {
            std::unordered_map<std::string, const char*> r;
            static const char* const tab[][2] = {
#include "I18nEn.inc"
            };
            for (auto& p : tab) r.emplace (p[0], p[1]);
            return r;
        }();
        auto it = m.find (pt); return it == m.end() ? nullptr : it->second;
    }
}
inline juce::String T8 (const char* s)
{
    if (i18n::isEn()) if (const char* e = i18n::lookup (s)) return juce::String (juce::CharPointer_UTF8 (e));
    return juce::String (juce::CharPointer_UTF8 (s));
}
inline juce::String tr (const juce::String& s)
{
    if (i18n::isEn()) if (const char* e = i18n::lookup (s.toStdString())) return juce::String (juce::CharPointer_UTF8 (e));
    return s;
}

class Lnf : public juce::LookAndFeel_V4
{
public:
    Lnf() { refresh(); setDefaultSansSerifTypefaceName ("Helvetica Neue"); }
    void refresh()
    {
        setColour (juce::ResizableWindow::backgroundColourId, kBg);
        setColour (juce::Label::textColourId, kText);
        setColour (juce::Slider::textBoxTextColourId, kText);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::ComboBox::backgroundColourId, kPanel2); setColour (juce::ComboBox::outlineColourId, kLine);
        setColour (juce::ComboBox::textColourId, kText); setColour (juce::ComboBox::arrowColourId, kDim);
        setColour (juce::PopupMenu::backgroundColourId, kPanel2); setColour (juce::PopupMenu::textColourId, kText);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, kTealDim); setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        setColour (juce::TextButton::buttonColourId, kPanel2); setColour (juce::TextButton::buttonOnColourId, kTealDim);
        setColour (juce::TextButton::textColourOffId, kText); setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        setColour (juce::TextEditor::backgroundColourId, kPanel2); setColour (juce::TextEditor::textColourId, kText);
        setColour (juce::TextEditor::outlineColourId, kLine); setColour (juce::TextEditor::focusedOutlineColourId, kTeal);
        setColour (juce::ToggleButton::textColourId, kText); setColour (juce::ToggleButton::tickColourId, kTeal);
        setColour (juce::Slider::thumbColourId, kTeal); setColour (juce::Slider::trackColourId, kTealDim); setColour (juce::Slider::backgroundColourId, kLine);
        setColour (juce::TooltipWindow::backgroundColourId, kPanel2); setColour (juce::TooltipWindow::textColourId, kText); setColour (juce::TooltipWindow::outlineColourId, kLine);
    }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return fontOf (12.f); }
    juce::Font getLabelFont (juce::Label&) override { return fontOf (12.f); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return fontOf (12.f, true); }
    juce::Font getPopupMenuFont() override { return fontOf (13.f); }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) override
    {
        const float d = (float) std::min (w, h) - 8.f, r = d * 0.5f, cx = x + w * 0.5f, cy = y + h * 0.5f;
        const auto col = s.findColour (juce::Slider::rotarySliderFillColourId);
        juce::Path track; track.addCentredArc (cx, cy, r, r, 0.f, a0, a1, true);
        g.setColour (kLine); g.strokePath (track, juce::PathStrokeType (3.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const float ang = a0 + pos * (a1 - a0);
        const double zero = s.getProperties().getWithDefault ("bipolar", false) ? 0.5 : 0.0;
        const float zAng = a0 + (float) zero * (a1 - a0);
        juce::Path val; val.addCentredArc (cx, cy, r, r, 0.f, std::min (zAng, ang), std::max (zAng, ang), true);
        if (s.isEnabled()) { g.setColour (col.withAlpha (0.18f)); g.strokePath (val, juce::PathStrokeType (7.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); }
        g.setColour (s.isEnabled() ? col : kDim.withAlpha (0.4f)); g.strokePath (val, juce::PathStrokeType (3.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        { const float kr = r * 0.62f; g.setGradientFill (juce::ColourGradient (kPanel2.brighter (0.22f), cx, cy - kr, kPanel2.darker (0.35f), cx, cy + kr, false)); g.fillEllipse (cx - kr, cy - kr, kr * 2.f, kr * 2.f);
          g.setColour (kLine.withAlpha (0.9f)); g.drawEllipse (cx - kr, cy - kr, kr * 2.f, kr * 2.f, 1.f); }
        juce::Path p; p.addRoundedRectangle (-1.2f, -r * 0.58f, 2.4f, r * 0.34f, 1.f); g.setColour (s.isEnabled() ? kText : kDim);
        g.fillPath (p, juce::AffineTransform::rotation (ang).translated (cx, cy));
    }
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool hover, bool) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (1.f);
        const bool on = b.getToggleState();
        { const auto base = on ? kTealDim : kPanel2; g.setGradientFill (juce::ColourGradient (base.brighter (0.12f), 0.f, r.getY(), base.darker (0.12f), 0.f, r.getBottom(), false)); g.fillRoundedRectangle (r, 5.f); }
        if (on) { g.setColour (kTeal.withAlpha (0.18f)); g.drawRoundedRectangle (r.expanded (1.5f), 6.f, 2.f); }
        g.setColour (on ? kTeal : (hover ? kDim : kLine)); g.drawRoundedRectangle (r, 5.f, 1.f);
        g.setColour (on ? juce::Colours::white : kDim); g.setFont (fontOf (12.f, true)); g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
    }
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hover, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (1.f);
        { const auto base = b.getToggleState() ? kTealDim : (down ? kLine : kPanel2); g.setGradientFill (juce::ColourGradient (base.brighter (hover ? 0.2f : 0.12f), 0.f, r.getY(), base.darker (0.12f), 0.f, r.getBottom(), false)); g.fillRoundedRectangle (r, 5.f); }
        g.setColour (b.getToggleState() ? kTeal : (hover ? kDim : kLine)); g.drawRoundedRectangle (r, 5.f, 1.f);
    }
    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& b) override
    {
        auto r = juce::Rectangle<float> (0.f, 0.f, (float) w, (float) h).reduced (0.5f);
        g.setColour (kPanel2); g.fillRoundedRectangle (r, 5.f); g.setColour (b.hasKeyboardFocus (true) ? kTeal : kLine); g.drawRoundedRectangle (r, 5.f, 1.f);
        juce::Path p; const float cx = (float) w - 12.f, cy = h * 0.5f; p.addTriangle (cx - 4, cy - 2, cx + 4, cy - 2, cx, cy + 3); g.setColour (kDim); g.fillPath (p);
    }
    void positionComboBoxText (juce::ComboBox& b, juce::Label& l) override { l.setBounds (6, 1, b.getWidth() - 22, b.getHeight() - 2); l.setFont (getComboBoxFont (b)); }
};
} // namespace pqui
