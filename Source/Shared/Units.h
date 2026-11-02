// =============================================================================
//  PRIMEQ pro by Piradex — conversao de texto digitado com unidades (virgula decimal aceite) — C++ puro
//  "1,5k" "1.5 kHz" "440" "a4" "c#3" -> Hz ; "-3,5 dB" -> dB ; "10 ms" "0,5 s" -> ms
// =============================================================================
#pragma once
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace pq { namespace units
{
inline std::string norm (const std::string& in)
{
    std::string s; for (char c : in) { if (c == ',') s += '.'; else if (! std::isspace ((unsigned char) c)) s += (char) std::tolower ((unsigned char) c); }
    return s;
}
inline bool endsWith (const std::string& s, const char* suf) { const std::string t (suf); return s.size() >= t.size() && s.compare (s.size() - t.size(), t.size(), t) == 0; }
inline bool parseNote (const std::string& s, double& hz)
{
    if (s.size() < 2 || s[0] < 'a' || s[0] > 'g') return false;
    static const int semis[7] = { 9, 11, 0, 2, 4, 5, 7 };   // a b c d e f g (dentro da oitava a partir de C)
    int n = semis[s[0] - 'a']; size_t i = 1;
    if (i < s.size() && (s[i] == '#' || s[i] == 'b')) { n += (s[i] == '#') ? 1 : -1; ++i; }
    if (i >= s.size()) return false;
    char* end = nullptr; const long oct = std::strtol (s.c_str() + i, &end, 10);
    if (end == s.c_str() + i || *end != '\0') return false;
    const int midi = (int) (12 * (oct + 1) + n);
    hz = 440.0 * std::pow (2.0, (midi - 69) / 12.0); return true;
}
inline double parseFreq (const std::string& in, double fallback)
{
    std::string s = norm (in); double hz;
    if (parseNote (s, hz)) return hz;
    double mult = 1.0;
    if (endsWith (s, "khz")) { mult = 1000; s.resize (s.size() - 3); } else if (endsWith (s, "hz")) s.resize (s.size() - 2); else if (endsWith (s, "k")) { mult = 1000; s.resize (s.size() - 1); }
    char* end = nullptr; const double v = std::strtod (s.c_str(), &end);
    return (end == s.c_str()) ? fallback : v * mult;
}
inline double parseDb (const std::string& in, double fallback)
{
    std::string s = norm (in); if (endsWith (s, "db")) s.resize (s.size() - 2);
    char* end = nullptr; const double v = std::strtod (s.c_str(), &end); return (end == s.c_str()) ? fallback : v;
}
inline double parseMs (const std::string& in, double fallback)
{
    std::string s = norm (in); double mult = 1.0;
    if (endsWith (s, "ms")) s.resize (s.size() - 2); else if (endsWith (s, "s")) { mult = 1000; s.resize (s.size() - 1); }
    char* end = nullptr; const double v = std::strtod (s.c_str(), &end); return (end == s.c_str()) ? fallback : v * mult;
}
inline double parsePlain (const std::string& in, double fallback)
{
    std::string s = norm (in); if (endsWith (s, ":1")) s.resize (s.size() - 2);
    char* end = nullptr; const double v = std::strtod (s.c_str(), &end); return (end == s.c_str()) ? fallback : v;
}
inline std::string formatFreq (double hz)
{
    char b[32]; if (hz >= 1000.0) std::snprintf (b, sizeof b, "%.2f kHz", hz / 1000.0); else if (hz >= 100.0) std::snprintf (b, sizeof b, "%.0f Hz", hz); else std::snprintf (b, sizeof b, "%.1f Hz", hz);
    return b;
}
}} // namespace pq::units
