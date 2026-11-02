# Estado das funcionalidades (honesto)
## Pronto e testado (Linux, testes automáticos)
- 24 bandas; tipos Bell, Low/High Shelf, Notch, Band-pass, Tilt, HP, LP; HP/LP em cascata Butterworth (ordens 1,2,3,4,6,8,12,16) e Brickwall = ordem 32 (finito, não ideal).
- Colocação por banda: Stereo / L / R / Mid / Side.
- Fase Zero Latency (IIR), Natural (α=0.5) e Linear (α=0) via FIR; latência reportada (4096 amostras @48k) e dry alinhado para bypass/delta.
- EQ dinâmico: detector band-pass Peak/RMS, joelho suave, range com sinal, attack/hold/release, sidechain externo (alimenta só detectores), Listen, auto-gain.
- Analisador FFT (Blackman-Harris, 2048/4096/8192), Freeze/Peak hold/tilt.
- Smart Ops Deresonate (determinístico, não ML).
- A/B, presets de fábrica e de utilizador (.pqpreset), undo/redo, estado versionado.
- Licenciamento offline PBKDF2 (master/beta), trial até 31/12/2026, guarda contra recuo do relógio.
## Parcial / limitações
- Em modos FIR a dinâmica actualiza a ~25 Hz (tarefa de fundo); a transição de modo demora ~100 ms.
- Brickwall é um filtro finito (ordem 32), não uma parede ideal.
- PCC: stub desactivado (`PQ_PCC_ENABLED=0`), nenhum plugin actual chama o PCC; ver LICENSING_PCC.md.
## Não implementado
De-harsh, Character, Reference Match, Unmask, Morph, De-ess, lookahead.
## Não verificado
Compilação macOS/Windows (só no CI), validação AU (auval), carregamento em DAWs reais, AAX.
