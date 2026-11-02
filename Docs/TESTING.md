# Testes e resultados
Executados em Linux (Release): `test_dsp` 80/80 (também com ASAN/UBSAN), `PrimeqSmoke` 65/65 (xvfb).
Medições: Bell +6 dB @1 kHz = 6.000 dB (ZL) / 5.998 dB (FIR); HP 80 Hz 24 dB/oct = −24.1/−3.0/−0.02 dB @40/80/160 Hz; PDC Linear/Natural exacta (impulso 3000 → 3000+4096); sidechain não vaza para a saída; GR −8.93 dB vs −9 esperado; 0 alocações em 200 processBlock (ZL e Linear); CPU ≈2.4% (ZL) / 3.8% (Linear) de um núcleo (24 bandas, estéreo, 48k/512); editor abre/fecha 20× sem crash.
Novos: Character alias 1× −16 dB vs 2×/4× −70 dB; De-harsh −10 dB num pico de 5 kHz sem tocar 300 Hz; lookahead 5 ms baixa o pico de rajada de −7.5 para −13.9 dBFS; CPU com tudo ligado ≈5% de um núcleo; latência total 2564 amostras (480+2048+36); Mix Unmask: kick percussivo vs baixo contínuo ⇒ sidechain 77% vs EQ 38%, contínuo vs contínuo ⇒ EQ.
**Não testado:** macOS, Windows, auval, DAWs reais, audição.
