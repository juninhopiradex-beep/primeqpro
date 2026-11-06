// =============================================================================
//  PRIMEQ PRO by Piradex — identidade visual: grafite mate, teal discreto, curvas de banda subtis
// =============================================================================
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>
#include <atomic>
#include <string>
#include <unordered_map>

namespace pqui
{
inline const juce::Colour kBg      { 0xff101215 };
inline const juce::Colour kPanel   { 0xff171a1e };
inline const juce::Colour kPanel2  { 0xff1d2126 };
inline const juce::Colour kLine    { 0xff2a2f36 };
inline const juce::Colour kGrid    { 0xff23282e };
inline const juce::Colour kText    { 0xffe4e8ec };
inline const juce::Colour kDim     { 0xff8b939c };
inline const juce::Colour kTeal    { 0xff3cc9b8 };
inline const juce::Colour kTealDim { 0xff1f6f66 };
inline const juce::Colour kWarn    { 0xffe0a458 };
inline const juce::Colour kRed     { 0xffe5646f };

inline juce::Colour bandColour (int b)
{
    static const juce::uint32 c[12] = { 0xff4fd1c5, 0xffe0a458, 0xffe07a8c, 0xff9b8cf0, 0xff63b3ed, 0xff9ccc65,
                                        0xfff08a5d, 0xffd77fd6, 0xff56c2e6, 0xffe6c35c, 0xff74d9a0, 0xfff0867a };
    return juce::Colour (c[b % 12]);
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
    Lnf()
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
        setDefaultSansSerifTypefaceName ("Helvetica Neue");
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
        g.setColour (s.isEnabled() ? col : kDim.withAlpha (0.4f)); g.strokePath (val, juce::PathStrokeType (3.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (kPanel2); g.fillEllipse (cx - r * 0.62f, cy - r * 0.62f, r * 1.24f, r * 1.24f);
        juce::Path p; p.addRoundedRectangle (-1.2f, -r * 0.58f, 2.4f, r * 0.34f, 1.f); g.setColour (s.isEnabled() ? kText : kDim);
        g.fillPath (p, juce::AffineTransform::rotation (ang).translated (cx, cy));
    }
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool hover, bool) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (1.f);
        const bool on = b.getToggleState();
        g.setColour (on ? kTealDim : kPanel2); g.fillRoundedRectangle (r, 5.f);
        g.setColour (on ? kTeal : (hover ? kDim : kLine)); g.drawRoundedRectangle (r, 5.f, 1.f);
        g.setColour (on ? juce::Colours::white : kDim); g.setFont (fontOf (12.f, true)); g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
    }
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hover, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (1.f);
        g.setColour (b.getToggleState() ? kTealDim : (down ? kLine : kPanel2)); g.fillRoundedRectangle (r, 5.f);
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
