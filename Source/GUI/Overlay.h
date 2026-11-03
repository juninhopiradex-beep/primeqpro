#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

// zona do espectro assinalada pelo Assistente no grafico principal
struct OverlayZone
{
    enum Kind { Masking = 0, Space = 1 };
    double f0 = 0, f1 = 0; float severity = 0.5f; int kind = Masking; juce::String label;
};
