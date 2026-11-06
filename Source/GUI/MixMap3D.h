// =============================================================================
//  PrimEQ Pro by Piradex — MAPA 3D da mistura: frequencia (x) · nivel relativo (y) · pista ou pan (z)
//  Projecao em perspectiva feita a mao (sem OpenGL). Arrastar = rodar, roda = zoom, duplo clique = repor.
//  Faixas vermelhas = bandas onde duas ou mais pistas fortes competem. So mostra instancias com ANALISAR ligado (mesmo processo).
// =============================================================================
#pragma once
#include "LookAndFeel.h"
#include "../Plugin/PluginProcessor.h"
#include "../Plugin/Collective.h"

namespace pqui
{
inline juce::Colour trackColour (int i) { return bandColour ((i * 5) % 12); }

class MixMap3D : public juce::Component, private juce::Timer
{
public:
    explicit MixMap3D (PrimeqProcessor& p) : proc (p) { setOpaque (false); }
    void setActive (bool on) { if (on) { refresh(); startTimerHz (12); } else stopTimer(); }
    void refresh() { data = pq::Collective::gather (pq::MixRegistry::get(), &proc, 40, false); if (solo >= (int) data.tracks.size()) solo = -1; repaint(); }
    const pq::Collective& current() const { return data; }
    bool panMode = false; float yaw = 0.55f, pitch = 0.42f, zoom = 1.f;
    int solo = -1;

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        g.setColour (kPanel); g.fillRoundedRectangle (area, 8.f);
        g.setColour (kLine); g.drawRoundedRectangle (area.reduced (0.5f), 8.f, 1.f);
        const int n = (int) data.tracks.size();

        // piso + marcas de frequencia
        auto P = [&] (float x, float y, float z, float* d = nullptr) { return project (x, y, z, d); };
        const float zl = 1.f;
        { juce::Path fl; fl.startNewSubPath (P (-1, 0, -zl)); fl.lineTo (P (1, 0, -zl)); fl.lineTo (P (1, 0, zl)); fl.lineTo (P (-1, 0, zl)); fl.closeSubPath();
          g.setColour (kPanel2); g.fillPath (fl); g.setColour (kGrid.brighter (0.4f)); g.strokePath (fl, juce::PathStrokeType (1.f)); }
        g.setFont (fontOf (10.f));
        for (double f : { 50., 100., 200., 500., 1000., 2000., 5000., 10000. })
        {
            const float x = xOfFreq (f); g.setColour (kGrid); g.drawLine (juce::Line<float> (P (x, 0, -zl), P (x, 0, zl)), 1.f);
            const auto q = P (x, 0, zl); g.setColour (kDim); g.drawText (f >= 1000. ? juce::String (f / 1000.) + "k" : juce::String ((int) f), (int) q.x - 18, (int) q.y + 2, 36, 12, juce::Justification::centred, false);
        }

        if (n == 0)
        {
            g.setColour (kText); g.setFont (fontOf (14.f, true)); g.drawFittedText (T8 ("Sem pistas para mostrar"), getLocalBounds().withSizeKeepingCentre (getWidth() - 80, 24).translated (0, -20), juce::Justification::centred, 1);
            g.setColour (kDim); g.setFont (fontOf (12.f)); g.drawFittedText (T8 ("Liga ANALISAR nesta pista e noutras instâncias do PrimEQ Pro no mesmo projecto, e põe-nas a tocar. Cada pista aparece aqui em 3D."), getLocalBounds().withSizeKeepingCentre (getWidth() - 120, 60).translated (0, 24), juce::Justification::centred, 3);
            paintChrome (g, n); return;
        }

        // planos de choque (vermelho): atravessam todas as pistas na banda
        for (int b = 0; b < pq::kMixBands; ++b)
        {
            const float s = data.clash[(size_t) b]; if (s < 0.15f) continue; const float x = xOfBand (b);
            juce::Path q; q.startNewSubPath (P (x, 0, -zl)); q.lineTo (P (x, 1, -zl)); q.lineTo (P (x, 1, zl)); q.lineTo (P (x, 0, zl)); q.closeSubPath();
            g.setColour (kRed.withAlpha (0.10f + 0.28f * s)); g.fillPath (q);
        }

        if (! panMode)
        {
            std::vector<int> order ((size_t) n); for (int i = 0; i < n; ++i) order[(size_t) i] = i;
            auto zOf = [&] (int i) { return n == 1 ? 0.f : -0.8f + 1.6f * (float) i / (float) (n - 1); };
            std::sort (order.begin(), order.end(), [&] (int a, int b) { float da, db; project (0, 0, zOf (a), &da); project (0, 0, zOf (b), &db); return da > db; });   // longe primeiro
            for (int i : order)
            {
                const auto& t = data.tracks[(size_t) i]; const float z = zOf (i); const auto c = trackColour (i); const bool dim = solo >= 0 && solo != i;
                juce::Path ribbon, line;
                for (int b = 0; b < pq::kMixBands; ++b)
                {
                    const auto q = P (xOfBand (b), data.norm01 (t.db[(size_t) b]), z);
                    if (b == 0) { ribbon.startNewSubPath (P (xOfBand (0), 0, z)); ribbon.lineTo (q); line.startNewSubPath (q); } else { ribbon.lineTo (q); line.lineTo (q); }
                }
                ribbon.lineTo (P (xOfBand (pq::kMixBands - 1), 0, z)); ribbon.closeSubPath();
                g.setColour (c.withAlpha (dim ? 0.07f : 0.36f)); g.fillPath (ribbon);
                g.setColour (t.self ? kText.withAlpha (dim ? 0.3f : 0.95f) : c.brighter (0.3f).withAlpha (dim ? 0.25f : 0.95f)); g.strokePath (line, juce::PathStrokeType (t.self ? 2.4f : 1.6f));
            }
        }
        else
        {
            struct Stem { float d; juce::Point<float> a, b; int tr; };
            std::vector<Stem> st;
            for (int i = 0; i < n; ++i) for (int b = 0; b < pq::kMixBands; ++b)
            {
                const auto& t = data.tracks[(size_t) i]; const float h = data.norm01 (t.db[(size_t) b]); if (h < 0.04f) continue;
                const float x = xOfBand (b), z = t.pan[(size_t) b] * 0.9f; float d; const auto top = P (x, h, z, &d);
                st.push_back ({ d, P (x, 0, z), top, i });
            }
            std::sort (st.begin(), st.end(), [] (const Stem& a, const Stem& b) { return a.d > b.d; });
            for (const auto& s : st)
            {
                const auto c = trackColour (s.tr); const bool dim = solo >= 0 && solo != s.tr; const bool self = data.tracks[(size_t) s.tr].self;
                g.setColour (c.withAlpha (dim ? 0.12f : 0.8f)); g.drawLine (juce::Line<float> (s.a, s.b), self ? 3.f : 2.f);
                g.setColour ((self ? kText : c.brighter (0.5f)).withAlpha (dim ? 0.2f : 1.f)); g.fillEllipse (s.b.x - 2.5f, s.b.y - 2.5f, 5.f, 5.f);
            }
            g.setColour (kDim); g.setFont (fontOf (10.f));
            const auto l = P (-1, 0, -0.9f), r = P (-1, 0, 0.9f); g.drawText ("L", (int) l.x - 14, (int) l.y - 6, 12, 12, juce::Justification::centredRight, false); g.drawText ("R", (int) r.x - 14, (int) r.y - 6, 12, 12, juce::Justification::centredRight, false);
        }
        paintChrome (g, n);
    }

    void resized() override { layoutChips(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto p = e.getPosition(); dragStart = p; yaw0 = yaw; pitch0 = pitch; dragging = false;
        if (modeChip.contains (p)) { panMode = ! panMode; repaint(); return; }
        if (resetChip.contains (p)) { resetView(); return; }
        if (anaChip.contains (p)) { proc.setAnalysing (! proc.analysing()); refresh(); return; }
        if (closeChip.contains (p)) { if (onClose) onClose(); return; }
        for (size_t i = 0; i < legend.size(); ++i) if (legend[i].contains (p)) { solo = solo == (int) i ? -1 : (int) i; repaint(); return; }
        dragging = true;
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging) return; const auto d = e.getPosition() - dragStart;
        yaw = yaw0 + d.x * 0.008f; pitch = juce::jlimit (0.05f, 1.45f, pitch0 + d.y * 0.006f); repaint();
    }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override { zoom = juce::jlimit (0.5f, 2.5f, zoom * (1.f + w.deltaY * 0.4f)); repaint(); }
    void mouseDoubleClick (const juce::MouseEvent&) override { resetView(); }
    std::function<void()> onClose;
    void resetView() { yaw = 0.55f; pitch = 0.42f; zoom = 1.f; solo = -1; repaint(); }

private:
    void timerCallback() override { if (isVisible()) refresh(); }
    static float xOfFreq (double f) { return (float) (2.0 * std::log (f / pq::mixBandFreq (0)) / std::log (pq::mixBandFreq (pq::kMixBands - 1) / pq::mixBandFreq (0)) - 1.0); }
    static float xOfBand (int b) { return 2.f * (float) b / (float) (pq::kMixBands - 1) - 1.f; }
    juce::Point<float> project (float x, float y, float z, float* depth = nullptr) const
    {
        const float cy = std::cos (yaw), sy = std::sin (yaw), cp = std::cos (pitch), sp = std::sin (pitch);
        const float X = x * cy + z * sy, Z = -x * sy + z * cy, Y = y - 0.35f;
        const float Y2 = Y * cp - Z * sp, Z2 = Y * sp + Z * cp; const float d = 3.4f / zoom, k = d / (d - Z2);
        if (depth) *depth = Z2;
        const auto b = getLocalBounds().toFloat(); const float s = std::min (b.getWidth() * 0.26f, b.getHeight() * 0.42f) * zoom;
        return { b.getCentreX() + X * s * k, b.getCentreY() - 0.02f * b.getHeight() - Y2 * s * k };
    }
    void layoutChips()
    {
        auto b = getLocalBounds().reduced (10, 8); auto top = b.removeFromTop (22);
        closeChip = top.removeFromRight (24); top.removeFromRight (6); resetChip = top.removeFromRight (60); top.removeFromRight (6);
        modeChip = top.removeFromRight (104); top.removeFromRight (6); anaChip = top.removeFromRight (150);
    }
    void chip (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& t, bool on = false)
    {
        g.setColour (on ? kTeal.withAlpha (0.25f) : kLine); g.fillRoundedRectangle (r.toFloat(), 5.f);
        g.setColour (on ? kTeal : kText); g.setFont (fontOf (10.5f, true)); g.drawText (t, r, juce::Justification::centred, false);
    }
    void paintChrome (juce::Graphics& g, int n)
    {
        layoutChips();
        g.setColour (kTeal); g.setFont (fontOf (13.f, true)); g.drawText (T8 ("MAPA 3D DA MISTURA"), 14, 8, 220, 22, juce::Justification::centredLeft, false);
        chip (g, anaChip, proc.analysing() ? T8 ("ANALISAR: LIGADO") : T8 ("ANALISAR: DESLIGADO"), proc.analysing());
        chip (g, modeChip, panMode ? T8 ("EIXO: PAN") : T8 ("EIXO: PISTA")); chip (g, resetChip, T8 ("REPOR")); chip (g, closeChip, "X");
        legend.clear(); int y = 36;
        for (int i = 0; i < n; ++i)
        {
            const auto& t = data.tracks[(size_t) i]; juce::Rectangle<int> r (12, y, 170, 16); legend.push_back (r); y += 18;
            const bool dim = solo >= 0 && solo != i; g.setColour (trackColour (i).withAlpha (dim ? 0.3f : 1.f)); g.fillRoundedRectangle (r.removeFromLeft (10).toFloat().reduced (0, 3), 2.f); r.removeFromLeft (6);
            g.setColour ((t.self ? kText : kDim).withAlpha (dim ? 0.4f : 1.f)); g.setFont (fontOf (11.f, t.self)); g.drawText (juce::String (t.name) + (t.self ? "  (" + T8 ("esta") + ")" : ""), r, juce::Justification::centredLeft, true);
        }
        juce::String s = juce::String (n) + " " + T8 ("pistas") + " · " + juce::String (data.clashBands) + " " + T8 ("bandas em choque");
        g.setColour (data.clashBands > 0 ? kRed : kDim); g.setFont (fontOf (11.f)); g.drawText (s, 14, getHeight() - 22, 300, 16, juce::Justification::centredLeft, false);
        g.setColour (kDim); g.setFont (fontOf (10.f)); g.drawText (T8 ("arrastar = rodar · roda = zoom · duplo clique = repor · clicar numa pista = isolar"), getWidth() - 470, getHeight() - 22, 460, 16, juce::Justification::centredRight, false);
    }

    PrimeqProcessor& proc; pq::Collective data; std::vector<juce::Rectangle<int>> legend;
    juce::Rectangle<int> modeChip, resetChip, anaChip, closeChip; juce::Point<int> dragStart; float yaw0 = 0.f, pitch0 = 0.f; bool dragging = false;
};
} // namespace pqui
