# Testes e resultados
Executados em Linux (Release): `test_dsp` 46/46, `PrimeqSmoke` 36/36 (xvfb).
Medições: Bell +6 dB @1 kHz = 6.000 dB (ZL) / 5.998 dB (FIR); HP 80 Hz 24 dB/oct = −24.1/−3.0/−0.02 dB @40/80/160 Hz; PDC Linear/Natural exacta (impulso 3000 → 3000+4096); sidechain não vaza para a saída; GR −8.93 dB vs −9 esperado; 0 alocações em 200 processBlock (ZL e Linear); CPU ≈2.4% (ZL) / 3.8% (Linear) de um núcleo (24 bandas, estéreo, 48k/512); editor abre/fecha 20× sem crash.
**Não testado:** macOS, Windows, auval, DAWs reais, audição.
