# Estado das funcionalidades (honesto)
## Pronto e testado (Linux, testes automáticos)
- 24 bandas; tipos Bell, Low/High Shelf, Notch, Band-pass, Tilt, HP, LP; HP/LP em cascata Butterworth (ordens 1,2,3,4,6,8,12,16) e Brickwall = ordem 32 (finito, não ideal).
- Colocação por banda: Stereo / L / R / Mid / Side.
- Fase Zero Latency (IIR), Natural (α=0.5) e Linear (α=0) via FIR; latência reportada (4096 amostras @48k) e dry alinhado para bypass/delta.
- EQ dinâmico: detector band-pass Peak/RMS, joelho suave, range com sinal, attack/hold/release, sidechain externo (alimenta só detectores), Listen, auto-gain.
- Analisador FFT (Blackman-Harris, 2048/4096/8192), Freeze/Peak hold/tilt.
- Smart Ops (determinísticos, não ML): Deresonate, De-Ess, Unmask (sidechain), Reference Match (ficheiro WAV/AIFF/FLAC), Morph A↔B. Propostas dinâmicas e com sidechain; Revert repõe bandas e o estado do sidechain.
- De-harsh (STFT 2048, latência 2048, modo Delta), Character (Clean/Subtle/Warm, oversampling 1×/2×/4×, alias −70 dB a 15 kHz com 2×/4×), Lookahead (1–10 ms). Latência total declarada = FIR + lookahead + De-harsh + oversampling.
- **Assistente (dossiê EQNOVA X)**, 3 modos: Simple (Intenção + Masking), Advanced (+ Espaço + Doutor), Expert (+ Cenários + relatório .md). Separadores:
  - Masking: conflitos por par, comparação EQ/Dinâmica/Sidechain, aplicar por pista, guia de routing.
  - Espaço: mapa frequência × panorama, tipo de fonte, centro congestionado (pan candidato = sugestão), Mono Safety (HP no Side), corte só no Mid.
  - Doutor: 5 sintomas → evidências medidas → hipóteses etiquetadas ("a investigar" / "compatível com os dados" / "não confirmado", sem probabilidades) → ações aplicáveis.
  - Intenção: Clarity/Punch/Warmth/Space/Smooth × papel (Geral/Voz/Kick/Baixo) × intensidade; gera bandas normais editáveis, explicação, previsão antes/depois; ganhos ≤ 4 dB; Revert; indicador de Δ loudness e Auto Gain para A/B justo.
  - Cenários: A (EQ), B (sidechain), C (1/3 EQ + 2/3 sidechain + 0,5 dB) com masking restante, nível da pista/soma, alteração tonal, pico, dinâmica, corte desnecessário e compensação de loudness.
  - Grupos G1–G4: só pistas do mesmo grupo se veem (isolamento manual entre projectos/sessões).
- Mix Unmask (aba própria): registo de pistas entre instâncias no mesmo processo do anfitrião, nome e prioridade editáveis, análise de mascaramento por par e comparação EQ vs Dinâmica vs Sidechain, aplicação por correio à pista alvo, guia de routing.
- A/B, presets de fábrica e de utilizador (.pqpreset), undo/redo, estado versionado.
- Licenciamento offline PBKDF2 (master/beta), trial até 31/12/2026, guarda contra recuo do relógio.
## Parcial / limitações
- Em modos FIR a dinâmica actualiza a ~25 Hz (tarefa de fundo); a transição de modo demora ~100 ms.
- Mix Unmask: só vê instâncias no mesmo processo do anfitrião; análise opt-in (usa CPU); estimativa sobre níveis medidos (25 ms), não é audição; sidechain exige routing no DAW; não existe ajuste automático de volume por pista.
- Reference Match: a referência não é guardada no projecto. De-Ess/De-harsh têm falsos positivos (hi-hats, sons tonais sustentados). Character são circuitos originais, não emulam hardware.
- Brickwall é um filtro finito (ordem 32), não uma parede ideal.
- PCC: stub desactivado (`PQ_PCC_ENABLED=0`), nenhum plugin actual chama o PCC; ver LICENSING_PCC.md.
- Assistente: Doutor e Intenção são regras DSP determinísticas (V1 do dossiê), não ML; não medem reverb, dicção nem preferência. "Pan candidato" é sugestão (o plugin não tem panner). Cenários limitados a 2–3 pistas e a um conflito de cada vez; usam níveis por banda (25 ms), não forma de onda — não veem fase/latência/crest factor de banda larga. Sem isolamento automático entre projectos no mesmo processo do DAW (use os grupos). Não existem ainda testes cegos de preferência auditiva, nem prova de superioridade face a concorrentes.
## Não implementado
AAX (precisa do SDK da Avid), integração PCC real (falta o contrato da API).
## Não verificado
Compilação macOS/Windows (só no CI), validação AU (auval), carregamento em DAWs reais, AAX.
