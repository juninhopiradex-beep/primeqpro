// =============================================================================
//  PRIMEQ PRO by Piradex — pastas do utilizador (definicoes, sessao lembrada)
//  macOS ~/Library/Application Support/Piradex/PRIMEQpro · Windows %APPDATA%\Piradex\PRIMEQpro
// =============================================================================
#pragma once
#include <juce_core/juce_core.h>

namespace pqpaths
{
inline juce::File userDir()
{
    auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    base = base.getChildFile ("Application Support");
   #endif
    auto d = base.getChildFile ("Piradex").getChildFile ("PRIMEQpro");
    d.createDirectory();
    return d;
}
inline juce::File settingsFile() { return userDir().getChildFile ("settings.xml"); }
inline juce::File userPresetDir() { auto d = userDir().getChildFile ("Presets"); d.createDirectory(); return d; }
}
