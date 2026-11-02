# Arquitectura
Layout: `Source/{DSP,Analysis,SmartOps,GUI,Plugin,Shared}`, `Tests/{DSP,Integration}`, `Packaging`, `.github/workflows`.
- **DSP/BandDesign.h** – biquads RBJ em double; HP/LP em cascata Butterworth (kMaxStages=16).
- **DSP/EqCore.h** – 24 bandas, sub-blocos de 32 amostras, colocação por banda, detector dinâmico, sidechain, auto-gain.
- **DSP/FirDesign.h** – resposta H_b^α = |H_b|·exp(jα·φ_b); α=1 ZL, 0.5 Natural, 0 Linear; IFFT, rotação N/2 e janela; taps 8192/16384/32768 conforme taxa. Quatro `juce::dsp::Convolution` (ll, rl, lr, rr); IRs preparadas por `FirWorker` (thread de fundo). Transição ZL↔FIR: fade para dry, troca, preenchimento, fade in.
- **Analysis** – `StereoRing` lock-free + STFT.
- **Plugin** – APVTS (~420 parâmetros, 17×24 + globais), UndoManager, telemetria atómica; áudio sem alocações (verificado em 200 blocos).
- **Access.cpp / Crypto.h / Licensing.h** – licenciamento offline.
