// =============================================================================
//  PrimEQ Pro by Piradex — VISUAL MIXER: superficie 2D de controlo de todas as pistas (mesmo processo)
//  X = pan (-100..+100 %), Y = ganho da pista (-24..+12 dB, parametro "Output Gain"), largura do no = Width (0..200 %, M/S).
//  Arrastar o no altera a pista; arrastar as pontas altera a largura; duplo clique repoe (0 dB, centro, 100 %).
//  Snapshots A/B/C (Set guarda, letra repoe), Balance Assistant (proposta com Desfazer). Aneis vermelhos = pistas em choque (do Mapa 3D).
//  Limites: so instancias do mesmo processo; snapshots vivem enquanto o projecto esta aberto (nao sao guardados no estado do plugin).
// =============================================================================
#pragma once
#include "MixMap3D.h"
#include "../SmartOps/Balance.h"
#include "../SmartOps/IntentEq.h"
#include <map>

namespace pqui
{
struct MixVals { float gain = 0.f, pan = 0.f, width = 100.f; };

class VisualMixer : public juce::Component, private juce::Timer
{
public:
    static constexpr float kYTop = 12.f, kYBot = -24.f;
    explicit VisualMixer (PrimeqProcessor& p) : proc (p) { setOpaque (false); }
    void setActive (bool on) { if (on) { refresh(); startTimerHz (10); } else stopTimer(); }
    std::function<void()> onClose;
    struct Node { int slot; juce::String name; bool self, analysing; float level; MixVals v; int idx; bool clash; };
    const std::vector<Node>& nodes() const { return nd; }
    int selected = -1;

    // ---- API (tambem usada nos testes)
    void setTrack (int slot, MixVals v)
    {
        v.gain = juce::jlimit (kYBot, kYTop, v.gain); v.pan = juce::jlimit (-100.f, 100.f, v.pan); v.width = juce::jlimit (0.f, 200.f, v.width);
        if (slot == proc.mixSlot()) { proc.setParamReal ("out", v.gain); proc.setParamReal ("trk_pan", v.pan); proc.setParamReal ("trk_width", v.width); pq::MixRegistry::get().publishMixer (slot, v.gain, v.pan, v.width); }
        else pq::MixRegistry::get().requestMixer (slot, v.gain, v.pan, v.width);
        for (auto& n : nd) if (n.slot == slot) n.v = v;
        repaint();
    }
    void saveSnapshot (int s) { snap[(size_t) s].clear(); for (auto& n : nd) snap[(size_t) s][n.slot] = n.v; }
    bool hasSnapshot (int s) const { return ! snap[(size_t) s].empty(); }
    void recallSnapshot (int s) { if (! hasSnapshot (s)) return; for (auto& n : std::vector<Node> (nd)) { auto it = snap[(size_t) s].find (n.slot); if (it != snap[(size_t) s].end()) setTrack (n.slot, it->second); } }
    // Balance Assistant: devolve quantas pistas foram propostas (0 = faltam pistas analisadas)
    int runAssistant()
    {
        std::vector<pq::BalanceIn> in; auto& reg = pq::MixRegistry::get();
        for (auto& t : reg.list (&proc)) if (t.analysing) { pq::BalanceIn b; b.slot = t.slot; b.name = t.name; b.priority = t.priority; b.kick = pq::guessRole (t.name) == pq::Role::Kick; b.levelDb = reg.stats (t.slot).rmsDb; in.push_back (b); }
        const auto out = pq::balancePropose (in); if (out.empty()) return 0;
        undoSet.clear(); for (auto& n : nd) undoSet[n.slot] = n.v;
        for (auto& o : out) setTrack (o.slot, { o.gainDb, o.panPct, o.widthPct });
        return (int) out.size();
    }
    void undoAssistant() { auto u = undoSet; undoSet.clear(); for (auto& kv : u) setTrack (kv.first, kv.second); }
    bool canUndo() const { return ! undoSet.empty(); }
    void refresh()
    {
        auto& reg = pq::MixRegistry::get(); const auto list = reg.list (&proc); const auto col = pq::Collective::gather (reg, &proc, 40, false);
        std::vector<int> clashSlots;
        for (int b = 0; b < pq::kMixBands; ++b) if (col.clash[(size_t) b] > 0.3f)
        { int a = -1, c = -1; for (size_t i = 0; i < col.tracks.size(); ++i) { const float v = col.tracks[i].db[(size_t) b]; if (a < 0 || v > col.tracks[(size_t) a].db[(size_t) b]) { c = a; a = (int) i; } else if (c < 0 || v > col.tracks[(size_t) c].db[(size_t) b]) c = (int) i; }
          if (a >= 0) clashSlots.push_back (col.tracks[(size_t) a].slot); if (c >= 0) clashSlots.push_back (col.tracks[(size_t) c].slot); }
        std::vector<Node> n; int i = 0;
        for (auto& t : list)
        {
            Node x; x.slot = t.slot; x.name = juce::String (t.name); x.self = t.isSelf; x.analysing = t.analysing; x.level = t.analysing ? reg.stats (t.slot).rmsDb : -120.f; x.idx = i++;
            float m[3]; reg.mixer (t.slot, m); x.v = { m[0], m[1], m[2] }; x.clash = std::find (clashSlots.begin(), clashSlots.end(), t.slot) != clashSlots.end();
            if (dragSlot == t.slot) for (auto& o : nd) if (o.slot == t.slot) x.v = o.v;      // durante o arrasto manda o valor local
            n.push_back (x);
        }
        nd = std::move (n); if (selected >= (int) nd.size()) selected = -1; repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat(); g.setColour (kPanel); g.fillRoundedRectangle (area, 8.f); g.setColour (kLine); g.drawRoundedRectangle (area.reduced (0.5f), 8.f, 1.f);
        layout(); const auto pl = plot();
        g.setColour (kTeal); g.setFont (fontOf (13.f, true)); g.drawText (T8 ("VISUAL MIXER"), 14, 8, 160, 22, juce::Justification::centredLeft, false);
        chip (g, closeChip, "X"); chip (g, balChip, T8 ("BALANCE ASSISTANT")); if (canUndo()) chip (g, undoChip, T8 ("DESFAZER"));
        g.setColour (kDim); g.setFont (fontOf (10.f)); g.drawText ("Mix Snapshots", snapLbl, juce::Justification::centredRight, false);
        for (int s = 0; s < 3; ++s) { chip (g, setChip[s], "Set"); chip (g, letChip[s], juce::String::charToString ((juce::juce_wchar) ('A' + s)), hasSnapshot (s)); }
        // grelha
        g.setColour (kBg.withAlpha (0.6f)); g.fillRoundedRectangle (pl, 6.f);
        g.setFont (fontOf (10.f));
        for (float d = kYTop; d >= kYBot - 0.1f; d -= 6.f) { const float y = yOf (d); g.setColour (std::fabs (d) < 0.1f ? kGrid.brighter (0.7f) : kGrid); g.drawHorizontalLine ((int) y, pl.getX(), pl.getRight()); g.setColour (kDim); g.drawText (juce::String ((int) d) + " dB", (int) pl.getX() + 4, (int) y - 12, 50, 12, juce::Justification::centredLeft, false); }
        g.setColour (kGrid.brighter (0.5f)); g.drawVerticalLine ((int) xOf (0.f), pl.getY(), pl.getBottom());
        g.setColour (kDim); g.setFont (fontOf (13.f, true)); g.drawText ("L", (int) pl.getX() + 6, (int) pl.getCentreY() - 8, 16, 16, juce::Justification::centred, false); g.drawText ("R", (int) pl.getRight() - 22, (int) pl.getCentreY() - 8, 16, 16, juce::Justification::centred, false);
        if (nd.empty()) { g.setColour (kDim); g.setFont (fontOf (12.f)); g.drawFittedText (T8 ("Sem pistas: abre o PrimEQ Pro nas pistas do projecto (mesmo processo da DAW)."), pl.toNearestInt().reduced (60), juce::Justification::centred, 2); }
        for (const auto& n : nd) drawNode (g, n);
        // lista
        g.setColour (kPanel2); g.fillRoundedRectangle (listArea().toFloat(), 6.f);
        for (size_t i = 0; i < nd.size(); ++i)
        {
            const auto r = rowOf ((int) i); const auto& n = nd[i]; if ((int) i == selected) { g.setColour (kTeal.withAlpha (0.2f)); g.fillRect (r); }
            g.setColour (trackColour (n.idx)); g.fillEllipse ((float) r.getX() + 6, (float) r.getCentreY() - 4.f, 8.f, 8.f);
            g.setColour (n.self ? kText : kDim.brighter (0.3f)); g.setFont (fontOf (11.f, n.self)); g.drawText (n.name, r.getX() + 20, r.getY(), r.getWidth() - 24, 16, juce::Justification::centredLeft, true);
            g.setColour (kDim); g.setFont (fontOf (9.5f)); g.drawText (valText (n.v), r.getX() + 20, r.getY() + 14, r.getWidth() - 24, 12, juce::Justification::centredLeft, true);
        }
        g.setColour (status.isNotEmpty() ? kTeal : kDim); g.setFont (fontOf (10.f));
        g.drawText (status.isNotEmpty() ? status : T8 ("arrastar o nó = pan e ganho · arrastar as pontas = largura · duplo clique = repor"), 14, getHeight() - 22, getWidth() - 220, 16, juce::Justification::centredLeft, false);
    }
    void resized() override { layout(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto p = e.getPosition(); status = {}; dragMode = 0; dragSlot = -1;
        if (closeChip.contains (p)) { if (onClose) onClose(); return; }
        if (balChip.contains (p)) { const int n = runAssistant(); status = n ? T8 ("Proposta aplicada a ") + juce::String (n) + " " + T8 ("pistas") + " — " + T8 ("verifica de ouvido; DESFAZER repõe") : T8 ("Liga ANALISAR em pelo menos 2 pistas e põe-nas a tocar (de preferência um refrão)."); repaint(); return; }
        if (canUndo() && undoChip.contains (p)) { undoAssistant(); status = T8 ("Valores anteriores repostos."); repaint(); return; }
        for (int s = 0; s < 3; ++s) { if (setChip[s].contains (p)) { saveSnapshot (s); status = T8 ("Snapshot guardado: ") + juce::String::charToString ((juce::juce_wchar) ('A' + s)); repaint(); return; } if (letChip[s].contains (p)) { if (hasSnapshot (s)) recallSnapshot (s); else status = T8 ("Snapshot vazio: usa Set primeiro."); repaint(); return; } }
        for (size_t i = 0; i < nd.size(); ++i) if (rowOf ((int) i).contains (p)) { selected = (int) i; repaint(); return; }
        for (int i = (int) nd.size() - 1; i >= 0; --i)
        {
            const auto& n = nd[(size_t) i]; const auto c = centreOf (n);
            if ((int) i == selected) { const float h = halfWidthPx (n.v.width); if (p.toFloat().getDistanceFrom ({ c.x - h, c.y }) < 9.f) { selected = i; dragMode = 2; dragSlot = n.slot; dragSign = -1; return; } if (p.toFloat().getDistanceFrom ({ c.x + h, c.y }) < 9.f) { selected = i; dragMode = 2; dragSlot = n.slot; dragSign = 1; return; } }
            if (p.toFloat().getDistanceFrom (c) < 16.f) { selected = i; dragMode = 1; dragSlot = n.slot; repaint(); return; }
        }
        selected = -1; repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragMode == 0 || dragSlot < 0) return; MixVals v; bool f = false; for (auto& n : nd) if (n.slot == dragSlot) { v = n.v; f = true; } if (! f) return;
        const auto pl = plot(); const auto p = e.getPosition().toFloat();
        if (dragMode == 1) { v.pan = std::round (juce::jmap (p.x, xOf (-100.f), xOf (100.f), -100.f, 100.f)); if (std::fabs (v.pan) < 3.f) v.pan = 0.f; v.gain = std::round (juce::jmap (p.y, pl.getY() + 10.f, pl.getBottom() - 10.f, kYTop, kYBot) * 2.f) / 2.f; }
        else { const auto c = centreOf (nd[(size_t) selected]); v.width = std::round (std::fabs (p.x - c.x) / halfWidthPx (100.f) * 100.f / 5.f) * 5.f; (void) dragSign; }
        setTrack (dragSlot, v);
    }
    void mouseUp (const juce::MouseEvent&) override { dragMode = 0; dragSlot = -1; }
    void mouseDoubleClick (const juce::MouseEvent& e) override { for (auto& n : std::vector<Node> (nd)) if (e.getPosition().toFloat().getDistanceFrom (centreOf (n)) < 16.f) { setTrack (n.slot, {}); return; } }

private:
    void timerCallback() override { if (isVisible() && dragMode == 0) refresh(); }
    juce::Rectangle<float> plot() const { auto b = getLocalBounds().toFloat().reduced (12.f, 0.f); b.removeFromTop (40.f); b.removeFromBottom (28.f); b.removeFromRight (196.f); return b; }
    juce::Rectangle<int> listArea() const { auto b = getLocalBounds().reduced (12, 0); b.removeFromTop (40); b.removeFromBottom (28); return b.removeFromRight (190); }
    juce::Rectangle<int> rowOf (int i) const { auto l = listArea(); return { l.getX(), l.getY() + 2 + i * 30, l.getWidth(), 30 }; }
    float xOf (float pan) const { const auto pl = plot(); return pl.getCentreX() + pan / 100.f * (pl.getWidth() * 0.5f - 24.f); }
    float yOf (float gain) const { const auto pl = plot(); return juce::jmap (gain, kYTop, kYBot, pl.getY() + 10.f, pl.getBottom() - 10.f); }
    float halfWidthPx (float widthPct) const { return widthPct / 100.f * plot().getWidth() * 0.035f; }
    juce::Point<float> centreOf (const Node& n) const { return { xOf (n.v.pan), yOf (n.v.gain) }; }
    static juce::String valText (const MixVals& v) { return juce::String (v.gain, 1) + " dB · " + (std::fabs (v.pan) < 0.5f ? juce::String ("C") : juce::String (v.pan < 0 ? "L " : "R ") + juce::String ((int) std::fabs (v.pan)) + "%") + " · W " + juce::String ((int) v.width) + "%"; }
    void layout()
    {
        auto b = getLocalBounds().reduced (10, 8); auto top = b.removeFromTop (24);
        closeChip = top.removeFromRight (24); top.removeFromRight (10);
        for (int s = 2; s >= 0; --s) { letChip[s] = top.removeFromRight (24); top.removeFromRight (2); setChip[s] = top.removeFromRight (34); top.removeFromRight (10); }
        snapLbl = top.removeFromRight (96); top.removeFromRight (14);
        balChip = top.removeFromRight (150); top.removeFromRight (6); undoChip = top.removeFromRight (80);
    }
    void chip (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& t, bool on = false)
    { g.setColour (on ? kTeal.withAlpha (0.28f) : kLine); g.fillRoundedRectangle (r.toFloat(), 5.f); g.setColour (on ? kTeal : kText); g.setFont (fontOf (10.5f, true)); g.drawText (t, r, juce::Justification::centred, false); }
    void drawNode (juce::Graphics& g, const Node& n)
    {
        const auto c = centreOf (n); const auto col = trackColour (n.idx); const bool sel = n.idx == selected;
        const float h = halfWidthPx (n.v.width);
        g.setColour (col.withAlpha (sel ? 0.9f : 0.45f)); g.drawLine (c.x - h, c.y, c.x + h, c.y, sel ? 2.f : 1.2f);
        if (sel) { g.setColour (kWarn); g.fillEllipse (c.x - h - 4.f, c.y - 4.f, 8.f, 8.f); g.fillEllipse (c.x + h - 4.f, c.y - 4.f, 8.f, 8.f); }
        const float act = n.analysing ? juce::jlimit (0.f, 1.f, (n.level + 60.f) / 60.f) : 0.f; const float r = 9.f + 7.f * act;
        g.setColour ((n.clash ? kRed : col).withAlpha (0.25f + 0.4f * act)); g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
        g.setColour (n.clash ? kRed : col); g.drawEllipse (c.x - r, c.y - r, 2 * r, 2 * r, sel ? 2.4f : 1.5f);
        g.setColour (kBg); g.fillEllipse (c.x - 5.f, c.y - 5.f, 10.f, 10.f); g.setColour (n.self ? kText : col.brighter (0.4f)); g.fillEllipse (c.x - 2.5f, c.y - 2.5f, 5.f, 5.f);
        g.setColour (sel ? kWarn : (n.self ? kText : kDim.brighter (0.4f))); g.setFont (fontOf (11.f, sel || n.self)); g.drawText (n.name, (int) c.x - 60, (int) (c.y + r + 2.f), 120, 14, juce::Justification::centred, false);
    }

    PrimeqProcessor& proc; std::vector<Node> nd; std::array<std::map<int, MixVals>, 3> snap; std::map<int, MixVals> undoSet;
    juce::Rectangle<int> closeChip, balChip, undoChip, snapLbl, setChip[3], letChip[3]; juce::String status;
    int dragMode = 0, dragSlot = -1, dragSign = 1;
};
} // namespace pqui
